/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x NET_BUFFER_LIST receive path
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This file implements the NDIS 6.x receive path:
 *   - E1000InitializeRxQueue - RX queue setup
 *   - E1000AllocateRxBuffers - Pre-allocate receive buffers
 *   - E1000IndicateReceive - Process received packets
 *   - E1000ReturnNetBufferLists - Handle returned NBLs
 *   - RX checksum status reporting
 */

#include "e1000.h"

/* ============================================================================
 * E1000InitializeRxQueue - Initialize a receive queue
 *
 * Allocates and initializes the RX descriptor ring and buffer pool.
 * ============================================================================ */

NDIS_STATUS
E1000InitializeRxQueue(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG QueueIndex
    )
{
    PE1000_RX_QUEUE RxQueue;
    ULONG DescriptorSize;
    NDIS_STATUS Status;

    if (QueueIndex >= E1000_MAX_RX_QUEUES)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    RxQueue = &Adapter->RxQueues[QueueIndex];
    NdisZeroMemory(RxQueue, sizeof(E1000_RX_QUEUE));

    RxQueue->Adapter = Adapter;
    RxQueue->QueueIndex = QueueIndex;
    RxQueue->Count = E1000_NUM_RX_DESC;

    /* Initialize spin lock */
    NdisAllocateSpinLock(&RxQueue->Lock);

    /* Allocate RX descriptor ring - must be 16-byte aligned */
    DescriptorSize = sizeof(E1000_RECEIVE_DESCRIPTOR) * RxQueue->Count;

    NdisMAllocateSharedMemory(
        Adapter->MiniportAdapterHandle,
        DescriptorSize,
        FALSE,  /* Non-cached */
        (PVOID*)&RxQueue->Descriptors,
        &RxQueue->DescriptorsPa
        );

    if (RxQueue->Descriptors == NULL)
    {
        DPRINT("E1000: Failed to allocate RX descriptor ring for queue %u\n", QueueIndex);
        NdisFreeSpinLock(&RxQueue->Lock);
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory((PVOID)RxQueue->Descriptors, DescriptorSize);

    DPRINT("E1000: RX queue %u descriptors at VA=%p PA=0x%I64x\n",
             QueueIndex, RxQueue->Descriptors, RxQueue->DescriptorsPa.QuadPart);

    /* Allocate RX buffer tracking array */
    RxQueue->Buffers = NdisAllocateMemoryWithTagPriority(
                            Adapter->MiniportAdapterHandle,
                            sizeof(E1000_RX_BUFFER) * RxQueue->Count,
                            'E1kR',
                            NormalPoolPriority
                            );

    if (RxQueue->Buffers == NULL)
    {
        DPRINT1("E1000: Failed to allocate RX buffer tracking for queue %u\n", QueueIndex);
        NdisMFreeSharedMemory(
            Adapter->MiniportAdapterHandle,
            DescriptorSize,
            FALSE,
            (PVOID)RxQueue->Descriptors,
            RxQueue->DescriptorsPa
            );
        NdisFreeSpinLock(&RxQueue->Lock);
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory(RxQueue->Buffers, sizeof(E1000_RX_BUFFER) * RxQueue->Count);

    /* Initialize ring indices */
    RxQueue->Head = 0;
    RxQueue->Tail = 0;

    /* Initialize batched RDT update counter */
    RxQueue->CleanedCount = 0;

    /* Initialize indication DPC */
    KeInitializeDpc(&RxQueue->IndicateDpc, E1000RxIndicateDpc, RxQueue);

    /* Allocate receive buffers and fill descriptors */
    Status = E1000AllocateRxBuffers(RxQueue);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: Failed to allocate RX buffers for queue %u: 0x%08x\n", QueueIndex, Status);
        NdisFreeMemory(RxQueue->Buffers, sizeof(E1000_RX_BUFFER) * RxQueue->Count, 0);
        NdisMFreeSharedMemory(
            Adapter->MiniportAdapterHandle,
            DescriptorSize,
            FALSE,
            (PVOID)RxQueue->Descriptors,
            RxQueue->DescriptorsPa
            );
        NdisFreeSpinLock(&RxQueue->Lock);
        return Status;
    }

    /* Program hardware registers for this queue */
    if (QueueIndex == 0)
    {
        ULONG RctlValue;

        /* RX Descriptor Base Address */
        E1000_WRITE_REG(Adapter, E1000_REG_RDBAL, (ULONG)RxQueue->DescriptorsPa.LowPart);
        E1000_WRITE_REG(Adapter, E1000_REG_RDBAH, (ULONG)RxQueue->DescriptorsPa.HighPart);

        /* RX Descriptor Ring Length */
        E1000_WRITE_REG(Adapter, E1000_REG_RDLEN, DescriptorSize);

        /* RX Head and Tail - tail points to last valid descriptor */
        E1000_WRITE_REG(Adapter, E1000_REG_RDH, 0);
        E1000_WRITE_REG(Adapter, E1000_REG_RDT, RxQueue->Count - 1);
        RxQueue->Tail = RxQueue->Count - 1;

        /* Configure RCTL - will be enabled later */
        RctlValue = E1000_RCTL_EN |             /* Enable receiver */
                    E1000_RCTL_BAM |            /* Accept broadcast */
                    E1000_RCTL_SBP |            /* Store bad packets (for diagnostics) */
                    E1000_RCTL_SECRC |          /* Strip Ethernet CRC */
                    E1000_RCTL_BSIZE_2048;      /* 2KB buffer size */

        /* Enable extended status in RX descriptors for checksum offload */
        if (Adapter->IsPCIe)
        {
            RctlValue |= E1000_RCTL_DTYP_PS;    /* Use packet split descriptors */
        }

        E1000_WRITE_REG(Adapter, E1000_REG_RCTL, RctlValue);
    }

    DPRINT("E1000: RX queue %u initialized: %u descriptors, %u bytes\n",
             QueueIndex, RxQueue->Count, DescriptorSize);

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000AllocateRxBuffers - Allocate receive buffers for a queue
 *
 * Pre-allocates NBLs and memory for receive buffers.
 * ============================================================================ */

NDIS_STATUS
E1000AllocateRxBuffers(
    _In_ PE1000_RX_QUEUE RxQueue
    )
{
    PE1000_ADAPTER Adapter = RxQueue->Adapter;
    ULONG i;
    PE1000_RX_BUFFER RxBuffer;
    volatile PE1000_RECEIVE_DESCRIPTOR RxDesc;
    NDIS_PHYSICAL_ADDRESS BufferPa;
    PVOID BufferVa;

    /* Allocate a contiguous block for all receive buffers */
    for (i = 0; i < RxQueue->Count; i++)
    {
        RxBuffer = &RxQueue->Buffers[i];
        RxDesc = &RxQueue->Descriptors[i];

        /* Allocate shared memory for this buffer */
        NdisMAllocateSharedMemory(
            Adapter->MiniportAdapterHandle,
            E1000_RX_BUFFER_SIZE,
            FALSE,
            &BufferVa,
            &BufferPa
            );

        if (BufferVa == NULL)
        {
            DPRINT1("E1000: Failed to allocate RX buffer %u\n", i);
            /* Free previously allocated buffers */
            while (i > 0)
            {
                i--;
                RxBuffer = &RxQueue->Buffers[i];
                NdisMFreeSharedMemory(
                    Adapter->MiniportAdapterHandle,
                    E1000_RX_BUFFER_SIZE,
                    FALSE,
                    RxBuffer->VirtualAddress,
                    RxBuffer->PhysicalAddress
                    );
            }
            return NDIS_STATUS_RESOURCES;
        }

        RxBuffer->VirtualAddress = BufferVa;
        RxBuffer->PhysicalAddress = BufferPa;
        RxBuffer->BufferLength = E1000_RX_BUFFER_SIZE;
        RxBuffer->Flags = 0;

        /* Setup descriptor to point to this buffer */
        RxDesc->Address = BufferPa.QuadPart;
        RxDesc->Length = 0;
        RxDesc->Checksum = 0;
        RxDesc->Status = 0;
        RxDesc->Errors = 0;
        RxDesc->Special = 0;
    }

    DPRINT1("E1000: Allocated %u RX buffers (each %u bytes)\n",
             RxQueue->Count, E1000_RX_BUFFER_SIZE);

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000FreeRxQueue - Free RX queue resources
 * ============================================================================ */

VOID
E1000FreeRxQueue(
    _In_ PE1000_RX_QUEUE RxQueue
    )
{
    PE1000_ADAPTER Adapter;
    ULONG i;

    if (RxQueue == NULL || RxQueue->Adapter == NULL)
    {
        return;
    }

    Adapter = RxQueue->Adapter;

    /* Cancel any pending DPC */
    KeRemoveQueueDpc(&RxQueue->IndicateDpc);

    /* Disable receive before freeing buffers */
    if (RxQueue->QueueIndex == 0 && Adapter->IoBase != NULL)
    {
        E1000_WRITE_REG(Adapter, E1000_REG_RCTL, 0);
        E1000_WRITE_REG(Adapter, E1000_REG_RDH, 0);
        E1000_WRITE_REG(Adapter, E1000_REG_RDT, 0);
    }

    /* Free receive buffers */
    if (RxQueue->Buffers != NULL)
    {
        for (i = 0; i < RxQueue->Count; i++)
        {
            PE1000_RX_BUFFER RxBuffer = &RxQueue->Buffers[i];

            if (RxBuffer->VirtualAddress != NULL)
            {
                NdisMFreeSharedMemory(
                    Adapter->MiniportAdapterHandle,
                    E1000_RX_BUFFER_SIZE,
                    FALSE,
                    RxBuffer->VirtualAddress,
                    RxBuffer->PhysicalAddress
                    );
                RxBuffer->VirtualAddress = NULL;
            }

            if (RxBuffer->NetBufferList != NULL)
            {
                NdisFreeNetBufferList(RxBuffer->NetBufferList);
                RxBuffer->NetBufferList = NULL;
            }
        }

        NdisFreeMemory(RxQueue->Buffers, sizeof(E1000_RX_BUFFER) * RxQueue->Count, 0);
        RxQueue->Buffers = NULL;
    }

    /* Free descriptor ring */
    if (RxQueue->Descriptors != NULL)
    {
        NdisMFreeSharedMemory(
            Adapter->MiniportAdapterHandle,
            sizeof(E1000_RECEIVE_DESCRIPTOR) * RxQueue->Count,
            FALSE,
            (PVOID)RxQueue->Descriptors,
            RxQueue->DescriptorsPa
            );
        RxQueue->Descriptors = NULL;
    }

    NdisFreeSpinLock(&RxQueue->Lock);

    DPRINT1("E1000: RX queue %u freed\n", RxQueue->QueueIndex);
}


/* ============================================================================
 * E1000IndicateReceive - Indicate received packets to NDIS
 *
 * Called from DPC to process received descriptors and indicate NBLs.
 *
 * Implements budget-based NAPI-style processing:
 *   - Processes up to 'Budget' packets per call
 *   - Returns the number of packets actually processed (work_done)
 *   - If work_done >= Budget, caller should NOT re-enable interrupts
 *
 * This prevents CPU monopolization during high traffic bursts.
 * ============================================================================ */

ULONG
E1000IndicateReceive(
    _In_ PE1000_RX_QUEUE RxQueue,
    _In_ ULONG Budget
    )
{
    PE1000_ADAPTER Adapter = RxQueue->Adapter;
    PE1000_RX_BUFFER RxBuffer;
    volatile PE1000_RECEIVE_DESCRIPTOR RxDesc;
    PNET_BUFFER_LIST NblChain = NULL;
    PNET_BUFFER_LIST LastNbl = NULL;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER Nb;
    PMDL Mdl;
    ULONG Head;
    ULONG PacketLength;
    ULONG PacketsReceived = 0;
    ULONG WorkDone = 0;
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO ChecksumInfo;

    /* Ensure budget is at least 1 */
    if (Budget == 0)
    {
        Budget = 1;
    }

    /* Check adapter state */
    if (!E1000IsAdapterStarted(Adapter) || E1000IsAdapterPaused(Adapter))
    {
        return 0;
    }

    NdisAcquireSpinLock(&RxQueue->Lock);

    Head = RxQueue->Head;

    /*
     * Memory barrier before reading descriptor status.
     * Ensures we see the latest status written by hardware.
     * This matches Linux's dma_rmb() in e1000_clean_rx_irq().
     */
    KeMemoryBarrier();

    /* Process received descriptors up to budget limit */
    while (WorkDone < Budget)
    {
        RxDesc = &RxQueue->Descriptors[Head];

        /* Check if descriptor is done (DD bit set) */
        if (!(RxDesc->Status & E1000_RXDESC_STAT_DD))
        {
            break;  /* No more completed descriptors */
        }

        /*
         * Read barrier after checking DD status.
         * Ensures we read packet data only after confirming DD is set.
         */
        KeMemoryBarrier();

        RxBuffer = &RxQueue->Buffers[Head];
        PacketLength = RxDesc->Length;

        /* Count this as work done regardless of success/failure */
        WorkDone++;

        /* Check for errors */
        if (RxDesc->Errors != 0)
        {
            RxQueue->ReceiveErrors++;
            goto NextDescriptor;
        }

        /* Check for end-of-packet (we don't support multi-descriptor packets) */
        if (!(RxDesc->Status & E1000_RXDESC_STAT_EOP))
        {
            RxQueue->ReceiveErrors++;
            goto NextDescriptor;
        }

        /* Validate packet length */
        if (PacketLength < E1000_MIN_FRAME_SIZE || PacketLength > E1000_MAX_FRAME_SIZE)
        {
            RxQueue->ReceiveErrors++;
            goto NextDescriptor;
        }

        /* Allocate an NBL for this packet */
        Mdl = NdisAllocateMdl(
                Adapter->MiniportAdapterHandle,
                RxBuffer->VirtualAddress,
                PacketLength
                );

        if (Mdl == NULL)
        {
            RxQueue->DroppedPackets++;
            goto NextDescriptor;
        }

        Nbl = NdisAllocateNetBufferAndNetBufferList(
                Adapter->RxNblPool,
                0,      /* Context size */
                0,      /* Context backfill */
                Mdl,
                0,      /* Data offset */
                PacketLength
                );

        if (Nbl == NULL)
        {
            NdisFreeMdl(Mdl);
            RxQueue->DroppedPackets++;
            goto NextDescriptor;
        }

        /* Mark buffer as in use */
        RxBuffer->Flags |= E1000_RX_BUFFER_IN_USE;
        RxBuffer->NetBufferList = Nbl;

        /* Set checksum offload status */
        ChecksumInfo.Value = 0;

        if (RxDesc->Status & E1000_RXDESC_STAT_IPCS)
        {
            /* IP checksum was calculated */
            if (RxDesc->Errors & E1000_RXDESC_ERR_IPE)
            {
                ChecksumInfo.Receive.IpChecksumFailed = 1;
            }
            else
            {
                ChecksumInfo.Receive.IpChecksumSucceeded = 1;
            }
        }

        if (RxDesc->Status & E1000_RXDESC_STAT_TCPCS)
        {
            /* TCP/UDP checksum was calculated */
            if (RxDesc->Errors & E1000_RXDESC_ERR_TCPE)
            {
                ChecksumInfo.Receive.TcpChecksumFailed = 1;
            }
            else
            {
                ChecksumInfo.Receive.TcpChecksumSucceeded = 1;
            }
        }

        NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo) = ChecksumInfo.Value;

        /* Store source port (for RSS) */
        Nbl->SourceHandle = Adapter->MiniportAdapterHandle;

        /* Add to NBL chain */
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        if (NblChain == NULL)
        {
            NblChain = Nbl;
        }
        else
        {
            NET_BUFFER_LIST_NEXT_NBL(LastNbl) = Nbl;
        }
        LastNbl = Nbl;

        PacketsReceived++;
        RxQueue->BytesReceived += PacketLength;

NextDescriptor:
        /* Clear descriptor status for reuse */
        RxDesc->Status = 0;
        RxDesc->Errors = 0;
        RxDesc->Length = 0;

        /* Advance head */
        Head = (Head + 1) % RxQueue->Count;
    }

    RxQueue->Head = Head;
    RxQueue->PacketsReceived += PacketsReceived;

    NdisReleaseSpinLock(&RxQueue->Lock);

    /* Indicate packets to NDIS */
    if (NblChain != NULL)
    {
        NdisMIndicateReceiveNetBufferLists(
            Adapter->MiniportAdapterHandle,
            NblChain,
            0,      /* Port number */
            PacketsReceived,
            NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL
            );
    }

    /* Update adapter statistics */
    InterlockedExchangeAdd64((LONG64*)&Adapter->Statistics.RxPackets, PacketsReceived);

    return WorkDone;
}


/* ============================================================================
 * E1000ReturnNetBufferLists - Return NBLs back to the miniport
 *
 * Called by NDIS when protocol drivers are done with indicated NBLs.
 *
 * Implements batched RDT updates for efficiency:
 *   - Tracks CleanedCount of buffers returned since last RDT write
 *   - Only writes RDT when CleanedCount >= E1000_RX_BUFFER_WRITE (16)
 *   - This reduces register write overhead and improves cache behavior
 *
 * Like Linux e1000_alloc_rx_buffers(), batching RDT updates is important
 * because register writes are expensive (PCIe round-trips).
 * ============================================================================ */

VOID
NTAPI
E1000ReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    PE1000_RX_QUEUE RxQueue = &Adapter->RxQueues[0];  /* Primary queue */
    PNET_BUFFER_LIST CurrentNbl;
    PNET_BUFFER_LIST NextNbl;
    PNET_BUFFER Nb;
    PMDL Mdl;
    ULONG RefillCount = 0;
    ULONG Tail;
    BOOLEAN UpdateRdt = FALSE;

    UNREFERENCED_PARAMETER(ReturnFlags);

    NdisAcquireSpinLock(&RxQueue->Lock);

    Tail = RxQueue->Tail;

    /* Process returned NBLs */
    for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);

        /* Get the NET_BUFFER and its MDL */
        Nb = NET_BUFFER_LIST_FIRST_NB(CurrentNbl);
        if (Nb != NULL)
        {
            Mdl = NET_BUFFER_FIRST_MDL(Nb);
            if (Mdl != NULL)
            {
                /* The MDL was allocated by us, free it */
                NdisFreeMdl(Mdl);
            }
        }

        /* Free the NBL */
        NdisFreeNetBufferList(CurrentNbl);

        /* Find the corresponding RX buffer and mark it available */
        /* For now, we refill from the tail position */
        /* In a more sophisticated implementation, we would track which buffer
         * corresponds to which NBL */

        /* Advance tail to refill descriptors */
        Tail = (Tail + 1) % RxQueue->Count;
        RefillCount++;
    }

    if (RefillCount > 0)
    {
        RxQueue->Tail = Tail;

        /* Accumulate cleaned count for batched RDT updates */
        RxQueue->CleanedCount += RefillCount;

        /*
         * Only write RDT when we have accumulated enough cleaned buffers.
         * This batching reduces register write overhead.
         *
         * Like Linux e1000_alloc_rx_buffers():
         *   if (cleaned_count >= E1000_RX_BUFFER_WRITE) {
         *       writel(i, ring->tail);
         *       cleaned_count = 0;
         *   }
         */
        if (RxQueue->CleanedCount >= E1000_RX_BUFFER_WRITE)
        {
            UpdateRdt = TRUE;
        }
    }

    NdisReleaseSpinLock(&RxQueue->Lock);

    /* Update RDT outside the lock to minimize lock hold time */
    if (UpdateRdt)
    {
        /*
         * Memory barrier before RDT write.
         * Ensures all descriptor updates are visible before telling
         * hardware about new buffers.
         */
        KeMemoryBarrier();

        E1000_WRITE_REG(Adapter, E1000_REG_RDT, Tail);

        /* Reset cleaned count after RDT update */
        InterlockedExchange((LONG*)&RxQueue->CleanedCount, 0);
    }
}


/* ============================================================================
 * E1000RefillRxBuffers - Flush pending RDT update
 *
 * Called periodically or at end of receive indication to ensure any
 * pending cleaned buffers are returned to hardware even if below threshold.
 * This prevents buffer starvation on low-traffic paths.
 * ============================================================================ */

VOID
E1000RefillRxBuffers(
    _In_ PE1000_RX_QUEUE RxQueue
    )
{
    PE1000_ADAPTER Adapter = RxQueue->Adapter;
    ULONG CleanedCount;

    NdisAcquireSpinLock(&RxQueue->Lock);

    CleanedCount = RxQueue->CleanedCount;

    if (CleanedCount > 0)
    {
        /* Memory barrier before RDT write */
        KeMemoryBarrier();

        E1000_WRITE_REG(Adapter, E1000_REG_RDT, RxQueue->Tail);
        RxQueue->CleanedCount = 0;
    }

    NdisReleaseSpinLock(&RxQueue->Lock);
}


/* ============================================================================
 * E1000RxIndicateDpc - DPC routine for RX indication
 *
 * Note: This DPC is typically not used in the main interrupt path.
 * The main interrupt DPC (E1000MiniportInterruptDpc) calls E1000IndicateReceive
 * directly with budget support. This DPC is a fallback for other use cases.
 * ============================================================================ */

VOID
NTAPI
E1000RxIndicateDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
    )
{
    PE1000_RX_QUEUE RxQueue = (PE1000_RX_QUEUE)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (RxQueue != NULL)
    {
        /*
         * When called from standalone DPC (not main interrupt path),
         * use the default budget. The main interrupt handler uses
         * NDIS throttle parameters for budget control.
         */
        E1000IndicateReceive(RxQueue, E1000_RX_DEFAULT_BUDGET);
    }
}
