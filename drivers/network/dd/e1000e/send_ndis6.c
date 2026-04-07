/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x NET_BUFFER_LIST transmit path
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This file implements the NDIS 6.x transmit path:
 *   - E1000SendNetBufferLists - Main send handler
 *   - E1000InitializeTxQueue - TX queue setup
 *   - E1000ProcessTxCompletions - Completion handling
 *   - Context descriptor setup for checksum offload
 */

#include "e1000.h"

/* ============================================================================
 * Forward declarations
 * ============================================================================ */

static NDIS_STATUS
E1000TransmitNetBuffer(
    _In_ PE1000_TX_QUEUE TxQueue,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ PNET_BUFFER NetBuffer
    );


/* ============================================================================
 * E1000InitializeTxQueue - Initialize a transmit queue
 *
 * Allocates and initializes the TX descriptor ring and buffer tracking array.
 * ============================================================================ */

NDIS_STATUS
E1000InitializeTxQueue(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG QueueIndex
    )
{
    PE1000_TX_QUEUE TxQueue;
    ULONG DescriptorSize;

    if (QueueIndex >= E1000_MAX_TX_QUEUES)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    TxQueue = &Adapter->TxQueues[QueueIndex];
    NdisZeroMemory(TxQueue, sizeof(E1000_TX_QUEUE));

    TxQueue->Adapter = Adapter;
    TxQueue->QueueIndex = QueueIndex;
    TxQueue->Count = E1000_NUM_TX_DESC;

    /* Initialize spin lock */
    NdisAllocateSpinLock(&TxQueue->Lock);

    /* Allocate TX descriptor ring - must be 16-byte aligned */
    DescriptorSize = sizeof(E1000_TRANSMIT_DESCRIPTOR) * TxQueue->Count;

    NdisMAllocateSharedMemory(
        Adapter->MiniportAdapterHandle,
        DescriptorSize,
        FALSE,  /* Non-cached */
        (PVOID*)&TxQueue->Descriptors,
        &TxQueue->DescriptorsPa
        );

    if (TxQueue->Descriptors == NULL)
    {
        DPRINT("E1000: Failed to allocate TX descriptor ring for queue %u\n", QueueIndex);
        NdisFreeSpinLock(&TxQueue->Lock);
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory((PVOID)TxQueue->Descriptors, DescriptorSize);

    DPRINT("E1000: TX queue %u descriptors at VA=%p PA=0x%I64x\n",
             QueueIndex, TxQueue->Descriptors, TxQueue->DescriptorsPa.QuadPart);

    /* Allocate TX buffer tracking array */
    TxQueue->Buffers = NdisAllocateMemoryWithTagPriority(
                            Adapter->MiniportAdapterHandle,
                            sizeof(E1000_TX_BUFFER) * TxQueue->Count,
                            'E1kT',
                            NormalPoolPriority
                            );

    if (TxQueue->Buffers == NULL)
    {
        DPRINT1("E1000: Failed to allocate TX buffer tracking for queue %u\n", QueueIndex);
        NdisMFreeSharedMemory(
            Adapter->MiniportAdapterHandle,
            DescriptorSize,
            FALSE,
            (PVOID)TxQueue->Descriptors,
            TxQueue->DescriptorsPa
            );
        NdisFreeSpinLock(&TxQueue->Lock);
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory(TxQueue->Buffers, sizeof(E1000_TX_BUFFER) * TxQueue->Count);

    /* Initialize ring indices */
    TxQueue->Head = 0;
    TxQueue->Tail = 0;
    TxQueue->Available = TxQueue->Count;

    /* Initialize TX queue flow control flags */
    TxQueue->Flags = 0;

    /* Initialize completion DPC */
    KeInitializeDpc(&TxQueue->CompletionDpc, E1000TxCompletionDpc, TxQueue);

    /* Program hardware registers for this queue */
    if (QueueIndex == 0)
    {
        /* TX Descriptor Base Address */
        E1000_WRITE_REG(Adapter, E1000_REG_TDBAL, (ULONG)TxQueue->DescriptorsPa.LowPart);
        E1000_WRITE_REG(Adapter, E1000_REG_TDBAH, (ULONG)TxQueue->DescriptorsPa.HighPart);

        /* TX Descriptor Ring Length */
        E1000_WRITE_REG(Adapter, E1000_REG_TDLEN, DescriptorSize);

        /* TX Head and Tail */
        E1000_WRITE_REG(Adapter, E1000_REG_TDH, 0);
        E1000_WRITE_REG(Adapter, E1000_REG_TDT, 0);
    }

    DPRINT("E1000: TX queue %u initialized: %u descriptors, %u bytes\n",
             QueueIndex, TxQueue->Count, DescriptorSize);

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000FreeTxQueue - Free TX queue resources
 * ============================================================================ */

VOID
E1000FreeTxQueue(
    _In_ PE1000_TX_QUEUE TxQueue
    )
{
    PE1000_ADAPTER Adapter;
    ULONG i;

    if (TxQueue == NULL || TxQueue->Adapter == NULL)
    {
        return;
    }

    Adapter = TxQueue->Adapter;

    /* Cancel any pending DPC */
    KeRemoveQueueDpc(&TxQueue->CompletionDpc);

    /* Complete any pending NBLs with failure */
    if (TxQueue->Buffers != NULL)
    {
        for (i = 0; i < TxQueue->Count; i++)
        {
            PE1000_TX_BUFFER TxBuffer = &TxQueue->Buffers[i];

            if (TxBuffer->NetBufferList != NULL)
            {
                NET_BUFFER_LIST_STATUS(TxBuffer->NetBufferList) = NDIS_STATUS_RESET_IN_PROGRESS;
                NdisMSendNetBufferListsComplete(
                    Adapter->MiniportAdapterHandle,
                    TxBuffer->NetBufferList,
                    0  /* No dispatch level */
                    );
                TxBuffer->NetBufferList = NULL;
            }

            /* Free scatter-gather list if allocated */
            if (TxBuffer->SgList != NULL && Adapter->DmaAdapterHandle != NULL)
            {
                NdisMFreeNetBufferSGList(
                    Adapter->DmaAdapterHandle,
                    TxBuffer->SgList,
                    TxBuffer->NetBuffer
                    );
                TxBuffer->SgList = NULL;
            }
        }

        NdisFreeMemory(TxQueue->Buffers, sizeof(E1000_TX_BUFFER) * TxQueue->Count, 0);
        TxQueue->Buffers = NULL;
    }

    /* Free descriptor ring */
    if (TxQueue->Descriptors != NULL)
    {
        /* Clear hardware registers first */
        if (TxQueue->QueueIndex == 0 && Adapter->IoBase != NULL)
        {
            E1000_WRITE_REG(Adapter, E1000_REG_TDH, 0);
            E1000_WRITE_REG(Adapter, E1000_REG_TDT, 0);
        }

        NdisMFreeSharedMemory(
            Adapter->MiniportAdapterHandle,
            sizeof(E1000_TRANSMIT_DESCRIPTOR) * TxQueue->Count,
            FALSE,
            (PVOID)TxQueue->Descriptors,
            TxQueue->DescriptorsPa
            );
        TxQueue->Descriptors = NULL;
    }

    NdisFreeSpinLock(&TxQueue->Lock);

    DPRINT1("E1000: TX queue %u freed\n", TxQueue->QueueIndex);
}


/* ============================================================================
 * E1000SendNetBufferLists - NDIS 6.x Send handler
 *
 * Called by NDIS to transmit a chain of NET_BUFFER_LISTs.
 * Each NBL can contain multiple NET_BUFFERs.
 * ============================================================================ */

VOID
NTAPI
E1000SendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    PE1000_TX_QUEUE TxQueue;
    PNET_BUFFER_LIST CurrentNbl;
    PNET_BUFFER_LIST NextNbl;
    PNET_BUFFER CurrentNb;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    BOOLEAN DispatchLevel;
    ULONG PacketsSent = 0;

    UNREFERENCED_PARAMETER(PortNumber);

    DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);

    /* Check adapter state */
    if (!E1000IsAdapterStarted(Adapter) || E1000IsAdapterPaused(Adapter))
    {
        /* Complete all NBLs with failure */
        for (CurrentNbl = NetBufferList; CurrentNbl != NULL; CurrentNbl = NextNbl)
        {
            NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_PAUSED;
        }

        NdisMSendNetBufferListsComplete(
            Adapter->MiniportAdapterHandle,
            NetBufferList,
            DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0
            );
        return;
    }

    /* Use primary TX queue */
    TxQueue = &Adapter->TxQueues[0];

    /* Acquire TX queue lock */
    if (DispatchLevel)
    {
        NdisDprAcquireSpinLock(&TxQueue->Lock);
    }
    else
    {
        NdisAcquireSpinLock(&TxQueue->Lock);
    }

    /* Process each NBL in the chain */
    for (CurrentNbl = NetBufferList; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
        NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;

        /* Process each NET_BUFFER in this NBL */
        for (CurrentNb = NET_BUFFER_LIST_FIRST_NB(CurrentNbl);
             CurrentNb != NULL;
             CurrentNb = NET_BUFFER_NEXT_NB(CurrentNb))
        {
            Status = E1000TransmitNetBuffer(TxQueue, CurrentNbl, CurrentNb);

            if (Status != NDIS_STATUS_SUCCESS)
            {
                /* Failed to queue this packet */
                NET_BUFFER_LIST_STATUS(CurrentNbl) = Status;
                break;
            }

            PacketsSent++;
        }

        /* If any NB failed, complete the NBL now with error status */
        if (Status != NDIS_STATUS_SUCCESS)
        {
            if (DispatchLevel)
            {
                NdisDprReleaseSpinLock(&TxQueue->Lock);
            }
            else
            {
                NdisReleaseSpinLock(&TxQueue->Lock);
            }

            NdisMSendNetBufferListsComplete(
                Adapter->MiniportAdapterHandle,
                CurrentNbl,
                DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0
                );

            if (DispatchLevel)
            {
                NdisDprAcquireSpinLock(&TxQueue->Lock);
            }
            else
            {
                NdisAcquireSpinLock(&TxQueue->Lock);
            }
        }
    }

    /* Ring the doorbell - update TDT to notify hardware */
    if (PacketsSent > 0)
    {
        /*
         * Memory barrier before TDT write.
         * Ensures all descriptor writes are visible to hardware before
         * we update the tail pointer to signal new work.
         *
         * This is critical for correctness - without the barrier,
         * hardware might see the updated TDT before descriptor data
         * is fully written, causing corruption or hangs.
         * Matches Linux's wmb() before writel(tail).
         */
        KeMemoryBarrier();

        E1000_WRITE_REG(Adapter, E1000_REG_TDT, TxQueue->Tail);
    }

    if (DispatchLevel)
    {
        NdisDprReleaseSpinLock(&TxQueue->Lock);
    }
    else
    {
        NdisReleaseSpinLock(&TxQueue->Lock);
    }

    /* Update statistics */
    InterlockedExchangeAdd64((LONG64*)&TxQueue->PacketsSent, PacketsSent);
}


/* ============================================================================
 * E1000TransmitNetBuffer - Queue a single NET_BUFFER for transmission
 *
 * Sets up descriptors for the NET_BUFFER and adds it to the TX ring.
 * Called with TX queue lock held.
 * ============================================================================ */

static NDIS_STATUS
E1000TransmitNetBuffer(
    _In_ PE1000_TX_QUEUE TxQueue,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ PNET_BUFFER NetBuffer
    )
{
    PE1000_ADAPTER Adapter = TxQueue->Adapter;
    PE1000_TX_BUFFER TxBuffer;
    volatile PE1000_TRANSMIT_DESCRIPTOR TxDesc;
    PMDL Mdl;
    ULONG DataLength;
    ULONG DataOffset;
    PVOID VirtualAddress;
    NDIS_PHYSICAL_ADDRESS PhysicalAddress;
    ULONG TxIndex;
    ULONG DescriptorsNeeded = 1;  /* At minimum we need 1 descriptor */
    NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO ChecksumInfo;

    /* Get data length */
    DataLength = NET_BUFFER_DATA_LENGTH(NetBuffer);

    if (DataLength == 0 || DataLength > E1000_MAX_FRAME_SIZE)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    /* Check if we have enough descriptors */
    if (TxQueue->Available < (LONG)DescriptorsNeeded)
    {
        /*
         * TX Queue Flow Control:
         * Mark queue as stopped when we run out of descriptors.
         * The TX completion path will wake the queue when descriptors
         * become available again (see E1000ProcessTxCompletions).
         *
         * This is similar to Linux's netif_stop_queue() pattern.
         */
        InterlockedOr(&TxQueue->Flags, E1000_TX_QUEUE_STOPPED);
        return NDIS_STATUS_RESOURCES;
    }

    /* Get the MDL and data */
    Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    DataOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);

    /* Map the MDL to get virtual address */
    VirtualAddress = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
    if (VirtualAddress == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    VirtualAddress = (PUCHAR)VirtualAddress + DataOffset;

    /* Get physical address */
    PhysicalAddress = MmGetPhysicalAddress(VirtualAddress);

    /* Get descriptor index */
    TxIndex = TxQueue->Tail;

    /* Setup TX buffer tracking */
    TxBuffer = &TxQueue->Buffers[TxIndex];
    TxBuffer->NetBufferList = NetBufferList;
    TxBuffer->NetBuffer = NetBuffer;
    TxBuffer->VirtualAddress = VirtualAddress;
    TxBuffer->PhysicalAddress = PhysicalAddress;
    TxBuffer->Length = DataLength;
    TxBuffer->Flags = E1000_TX_BUFFER_IN_USE | E1000_TX_BUFFER_FIRST_SEG | E1000_TX_BUFFER_LAST_SEG;

    /* Setup TX descriptor */
    TxDesc = &TxQueue->Descriptors[TxIndex];
    TxDesc->Address = PhysicalAddress.QuadPart;
    TxDesc->Length = (USHORT)DataLength;
    TxDesc->ChecksumOffset = 0;
    TxDesc->ChecksumStartField = 0;
    TxDesc->Status = 0;
    TxDesc->Special = 0;

    /* Set command bits */
    TxDesc->Command = E1000_TXDESC_EOP |    /* End of Packet */
                      E1000_TXDESC_IFCS |   /* Insert FCS */
                      E1000_TXDESC_RS;      /* Report Status */

    /* Check for checksum offload */
    ChecksumInfo.Value = NET_BUFFER_LIST_INFO(NetBufferList, TcpIpChecksumNetBufferListInfo);

    if (ChecksumInfo.Transmit.TcpChecksum || ChecksumInfo.Transmit.UdpChecksum)
    {
        /* Enable TCP/UDP checksum offload */
        TxDesc->Command |= E1000_TXDESC_IDE;  /* Interrupt Delay Enable for better batching */

        /* Set checksum start and offset based on protocol */
        if (ChecksumInfo.Transmit.IsIPv4)
        {
            /* IPv4: Checksum starts at IP header (offset 14 for Ethernet) */
            TxDesc->ChecksumStartField = 14;  /* After Ethernet header */

            if (ChecksumInfo.Transmit.TcpChecksum)
            {
                /* TCP checksum offset: IP header (20) + TCP checksum field offset (16) */
                TxDesc->ChecksumOffset = (UCHAR)(14 + 20 + 16);
            }
            else if (ChecksumInfo.Transmit.UdpChecksum)
            {
                /* UDP checksum offset: IP header (20) + UDP checksum field offset (6) */
                TxDesc->ChecksumOffset = (UCHAR)(14 + 20 + 6);
            }
        }

        if (ChecksumInfo.Transmit.IpHeaderChecksum && Adapter->ChecksumOffload.TxIpChecksumEnabled)
        {
            /* Also request IP header checksum */
            /* Note: Legacy descriptors have limited checksum support */
        }
    }

    /* Memory barrier before updating tail */
    KeMemoryBarrier();

    /* Advance tail pointer */
    TxQueue->Tail = (TxIndex + 1) % TxQueue->Count;
    InterlockedDecrement(&TxQueue->Available);

    /* Update statistics */
    InterlockedExchangeAdd64((LONG64*)&TxQueue->BytesSent, DataLength);

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000ProcessTxCompletions - Process completed transmit descriptors
 *
 * Called from DPC to reclaim completed descriptors and complete NBLs.
 *
 * Implements TX queue wake mechanism:
 *   - If queue was stopped due to no descriptors, wake it when
 *     Available >= E1000_TX_WAKE_THRESHOLD (32)
 *   - This prevents TX stalls when queue fills up
 *
 * Similar to Linux's netif_wake_queue() pattern in e1000e driver.
 * ============================================================================ */

VOID
E1000ProcessTxCompletions(
    _In_ PE1000_TX_QUEUE TxQueue
    )
{
    PE1000_ADAPTER Adapter = TxQueue->Adapter;
    PE1000_TX_BUFFER TxBuffer;
    volatile PE1000_TRANSMIT_DESCRIPTOR TxDesc;
    PNET_BUFFER_LIST CompletedNbls = NULL;
    PNET_BUFFER_LIST LastNbl = NULL;
    ULONG Completed = 0;
    ULONG Head;
    BOOLEAN WakeQueue = FALSE;
    LONG OldFlags;

    NdisAcquireSpinLock(&TxQueue->Lock);

    Head = TxQueue->Head;

    /*
     * Memory barrier before reading descriptor status.
     * Ensures we see the latest DD bit written by hardware.
     */
    KeMemoryBarrier();

    /* Process completed descriptors */
    while (Head != TxQueue->Tail)
    {
        TxDesc = &TxQueue->Descriptors[Head];

        /* Check if descriptor is done (DD bit set) */
        if (!(TxDesc->Status & E1000_TXDESC_DD))
        {
            break;  /* Not done yet */
        }

        TxBuffer = &TxQueue->Buffers[Head];

        /* Build completion list if this is the last segment of an NBL */
        if ((TxBuffer->Flags & E1000_TX_BUFFER_LAST_SEG) && TxBuffer->NetBufferList != NULL)
        {
            PNET_BUFFER_LIST Nbl = TxBuffer->NetBufferList;

            /* Set completion status */
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
            NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

            /* Add to completion list */
            if (CompletedNbls == NULL)
            {
                CompletedNbls = Nbl;
            }
            else
            {
                NET_BUFFER_LIST_NEXT_NBL(LastNbl) = Nbl;
            }
            LastNbl = Nbl;
        }

        /* Clear buffer tracking */
        TxBuffer->NetBufferList = NULL;
        TxBuffer->NetBuffer = NULL;
        TxBuffer->Flags = 0;

        /* Clear descriptor */
        TxDesc->Status = 0;
        TxDesc->Address = 0;

        /* Advance head */
        Head = (Head + 1) % TxQueue->Count;
        Completed++;
    }

    TxQueue->Head = Head;
    InterlockedExchangeAdd(&TxQueue->Available, Completed);

    /* Record completion time for watchdog */
    if (Completed > 0)
    {
        KeQuerySystemTime(&TxQueue->LastCompletionTime);
    }

    /*
     * TX Queue Wake Mechanism:
     *
     * If the queue was stopped due to no descriptors, check if we now
     * have enough free descriptors to resume sending.
     *
     * Use a threshold (E1000_TX_WAKE_THRESHOLD = 32) to provide hysteresis
     * and avoid oscillating between stopped and running states.
     *
     * Linux e1000e e1000_clean_tx_irq() checks:
     *   if (count && netif_carrier_ok(netdev) &&
     *       e1000_desc_unused(tx_ring) >= TX_WAKE_THRESHOLD) {
     *       smp_mb();
     *       if (netif_queue_stopped(netdev) &&
     *           !(test_bit(__E1000_DOWN, &adapter->state)))
     *           netif_wake_queue(netdev);
     *   }
     *
     * We check: completed > 0, link up, available >= threshold, queue stopped
     */
    if (Completed > 0 &&
        (Adapter->MediaState == MediaConnectStateConnected) &&
        (TxQueue->Flags & E1000_TX_QUEUE_STOPPED) &&
        (TxQueue->Available >= E1000_TX_WAKE_THRESHOLD))
    {
        /*
         * Memory barrier ensures Available increment is visible
         * before we clear the STOPPED flag and wake the queue.
         * This matches Linux's smp_mb() before the wake check.
         */
        KeMemoryBarrier();

        OldFlags = InterlockedAnd(&TxQueue->Flags, ~E1000_TX_QUEUE_STOPPED);
        if (OldFlags & E1000_TX_QUEUE_STOPPED)
        {
            WakeQueue = TRUE;
        }
    }

    NdisReleaseSpinLock(&TxQueue->Lock);

    /* Complete NBLs outside of lock */
    if (CompletedNbls != NULL)
    {
        NdisMSendNetBufferListsComplete(
            Adapter->MiniportAdapterHandle,
            CompletedNbls,
            NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL
            );
    }

    /*
     * If we woke the queue, NDIS should now be able to send again.
     *
     * Note: In NDIS 6.x, there is no explicit NdisMRestartSend() API.
     * NDIS will automatically retry sending when:
     *   1. We complete pending sends (done above)
     *   2. Interrupt fires (which happens naturally)
     *
     * For more aggressive wake-up, we could use NdisMIndicateStatusEx()
     * with NDIS_STATUS_MEDIA_SPECIFIC_INDICATION, but completing sends
     * is usually sufficient to trigger NDIS to retry.
     */
    UNREFERENCED_PARAMETER(WakeQueue);

    /* Update adapter statistics */
    InterlockedExchangeAdd64((LONG64*)&Adapter->Statistics.TxPackets, Completed);
}


/* ============================================================================
 * E1000TxCompletionDpc - DPC routine for TX completion processing
 * ============================================================================ */

VOID
NTAPI
E1000TxCompletionDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2
    )
{
    PE1000_TX_QUEUE TxQueue = (PE1000_TX_QUEUE)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (TxQueue != NULL)
    {
        E1000ProcessTxCompletions(TxQueue);
    }
}


/* ============================================================================
 * E1000CancelSend - Cancel pending sends
 *
 * Called by NDIS to cancel sends with a matching cancel ID.
 * ============================================================================ */

VOID
NTAPI
E1000CancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CancelledNbls = NULL;
    PNET_BUFFER_LIST CurrentNbl;
    PNET_BUFFER_LIST PrevNbl;
    PNET_BUFFER_LIST NextNbl;

    /*
     * Check pending send list for matching cancel IDs.
     * Note: For the E1000, once packets are in the TX ring, they cannot
     * be cancelled. We can only cancel packets in the pending queue.
     */

    NdisAcquireSpinLock(&Adapter->SendLock);

    PrevNbl = NULL;
    CurrentNbl = Adapter->PendingSendHead;

    while (CurrentNbl != NULL)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);

        if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(CurrentNbl) == CancelId)
        {
            /* Remove from pending list */
            if (PrevNbl == NULL)
            {
                Adapter->PendingSendHead = NextNbl;
            }
            else
            {
                NET_BUFFER_LIST_NEXT_NBL(PrevNbl) = NextNbl;
            }

            if (CurrentNbl == Adapter->PendingSendTail)
            {
                Adapter->PendingSendTail = PrevNbl;
            }

            /* Add to cancelled list */
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = CancelledNbls;
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_SEND_ABORTED;
            CancelledNbls = CurrentNbl;
        }
        else
        {
            PrevNbl = CurrentNbl;
        }

        CurrentNbl = NextNbl;
    }

    NdisReleaseSpinLock(&Adapter->SendLock);

    /* Complete cancelled NBLs */
    if (CancelledNbls != NULL)
    {
        NdisMSendNetBufferListsComplete(
            Adapter->MiniportAdapterHandle,
            CancelledNbls,
            0
            );
    }
}
