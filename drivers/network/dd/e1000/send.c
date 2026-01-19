/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Sending packets
 * COPYRIGHT:   Copyright 2018 Mark Jansen <mark.jansen@reactos.org>
 *              Copyright 2019 Victor Perevertkin <victor.perevertkin@reactos.org>
 *              Copyright 2024 ReactOS Team - Batch send and checksum offload support
 */

#include "nic.h"

#include <debug.h>

/* ============================================================================
 * Internal Transmit Functions
 * ============================================================================ */

/*
 * NICTransmitPacketInternal - Queue a single packet for transmission
 *
 * This is the internal transmit function that sets up a TX descriptor.
 * Caller must hold TxLock.
 */
static
NDIS_STATUS
NICTransmitPacketInternal(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN UseChecksumOffload,
    _In_ UCHAR ChecksumStart,
    _In_ UCHAR ChecksumOffset)
{
    volatile PE1000_TRANSMIT_DESCRIPTOR TransmitDescriptor;
    ULONG DescIndex;

    NDIS_DbgPrint(MAX_TRACE, ("Called.\n"));

    /* Validate inputs */
    E1000_ASSERT_TX_VALID((PVOID)1, Length);  /* Packet validation done by caller */
    ASSERT(PhysicalAddress.QuadPart != 0);

    DescIndex = Adapter->CurrentTxDesc;
    E1000_ASSERT_TX_DESC_INDEX(DescIndex);

    TransmitDescriptor = Adapter->TransmitDescriptors + DescIndex;

    /* Log packet details */
    E1000_LOG_TX_PACKET(DebugStats.TxAttempts, Length, DescIndex, PhysicalAddress.QuadPart);

    TransmitDescriptor->Address = PhysicalAddress.QuadPart;
    TransmitDescriptor->Length = (USHORT)Length;
    TransmitDescriptor->Status = 0;
    TransmitDescriptor->Special = 0;

    if (UseChecksumOffload && Adapter->ChecksumOffload.TxChecksumEnabled)
    {
        /* Use checksum offload */
        TransmitDescriptor->ChecksumOffset = ChecksumOffset;
        TransmitDescriptor->ChecksumStartField = ChecksumStart;
        TransmitDescriptor->Command = E1000_TDESC_CMD_RS | E1000_TDESC_CMD_IFCS |
                                      E1000_TDESC_CMD_EOP | E1000_TDESC_CMD_IDE |
                                      E1000_TDESC_CMD_IC;  /* Insert Checksum */

        E1000_CSUM_DBG(("TX checksum offload: start=%u offset=%u\n",
                        ChecksumStart, ChecksumOffset));
    }
    else
    {
        /* No checksum offload */
        TransmitDescriptor->ChecksumOffset = 0;
        TransmitDescriptor->ChecksumStartField = 0;
        TransmitDescriptor->Command = E1000_TDESC_CMD_RS | E1000_TDESC_CMD_IFCS |
                                      E1000_TDESC_CMD_EOP | E1000_TDESC_CMD_IDE;
    }

    Adapter->CurrentTxDesc = (Adapter->CurrentTxDesc + 1) % NUM_TRANSMIT_DESCRIPTORS;

    /* Update statistics */
    E1000_STAT_INC(TxAttempts);
    E1000_STAT_ADD(TxBytes, Length);

    if (Adapter->CurrentTxDesc == Adapter->LastTxDesc)
    {
        NDIS_DbgPrint(MID_TRACE, ("All TX descriptors are full now\n"));
        E1000_TX_DBG(("TX ring FULL: current=%u last=%u\n",
                      Adapter->CurrentTxDesc, Adapter->LastTxDesc));
        Adapter->TxFull = TRUE;
        E1000_STAT_INC32(TxRingFull);
    }

    return NDIS_STATUS_SUCCESS;
}


/*
 * NICTransmitPacket - Queue a single packet for transmission (legacy interface)
 *
 * This function is called for single-packet transmissions.
 */
static
NDIS_STATUS
NICTransmitPacket(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG Length)
{
    return NICTransmitPacketInternal(Adapter, PhysicalAddress, Length, FALSE, 0, 0);
}


/* ============================================================================
 * NDIS Miniport Send Handlers
 * ============================================================================ */

/*
 * MiniportSend - Single packet send handler
 *
 * This is the legacy single-packet send interface. It's still used when
 * SendPacketsHandler is not registered or for compatibility.
 */
NDIS_STATUS
NTAPI
MiniportSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_PACKET Packet,
    _In_ UINT Flags)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    PSCATTER_GATHER_LIST sgList;
    ULONG TransmitLength;
    PHYSICAL_ADDRESS TransmitBuffer;
    NDIS_STATUS Status;
    ULONG FreeDesc;

    UNREFERENCED_PARAMETER(Flags);

    E1000_TX_DBG(("MiniportSend: Packet=%p Flags=0x%x\n", Packet, Flags));
    E1000_STAT_INC32(TxSingleCount);

    /* Validate packet */
    if (Packet == NULL)
    {
        NDIS_DbgPrint(MIN_TRACE, ("NULL packet received\n"));
        E1000_STAT_INC(TxFailed);
        return NDIS_STATUS_FAILURE;
    }

    sgList = NDIS_PER_PACKET_INFO_FROM_PACKET(Packet, ScatterGatherListPacketInfo);

    /* Assertions for debugging invalid packet states */
    ASSERT(sgList != NULL);
    if (sgList == NULL)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Packet has no scatter-gather list\n"));
        E1000_STAT_INC(TxFailed);
        return NDIS_STATUS_FAILURE;
    }

    ASSERT(sgList->NumberOfElements >= 1);
    if (sgList->NumberOfElements == 0)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Packet has zero scatter-gather elements\n"));
        E1000_STAT_INC(TxFailed);
        return NDIS_STATUS_FAILURE;
    }

    ASSERT(sgList->Elements[0].Length <= MAXIMUM_FRAME_SIZE);
    if (sgList->Elements[0].Length > MAXIMUM_FRAME_SIZE)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Packet too large: %u > %u\n",
                                  sgList->Elements[0].Length, MAXIMUM_FRAME_SIZE));
        E1000_STAT_INC(TxFailed);
        return NDIS_STATUS_FAILURE;
    }

    if (sgList->Elements[0].Length == 0)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Zero-length packet\n"));
        E1000_STAT_INC(TxFailed);
        return NDIS_STATUS_FAILURE;
    }

    NdisAcquireSpinLock(&Adapter->TxLock);

    /* Check TX ring state before attempting to queue */
    if (Adapter->TxFull)
    {
        NdisReleaseSpinLock(&Adapter->TxLock);
        NDIS_DbgPrint(MIN_TRACE, ("All TX descriptors are full\n"));
        E1000_TX_DBG(("TX FULL: Rejecting packet, current=%u last=%u\n",
                      Adapter->CurrentTxDesc, Adapter->LastTxDesc));
        E1000_STAT_INC(TxDropped);
        return NDIS_STATUS_RESOURCES;
    }

    /* Log TX ring state */
    FreeDesc = NICGetFreeTxDescriptors(Adapter);
    E1000_TX_DBG(("TX ring state: current=%u last=%u free=%u\n",
                  Adapter->CurrentTxDesc, Adapter->LastTxDesc, FreeDesc));

    /*
     * Handle multiple scatter-gather elements by using multiple descriptors.
     * For now, we only use the first element if there's just one, otherwise
     * we need to check if we have enough descriptors.
     */
    if (sgList->NumberOfElements == 1)
    {
        TransmitLength = sgList->Elements[0].Length;
        TransmitBuffer = sgList->Elements[0].Address;
        Adapter->TransmitPackets[Adapter->CurrentTxDesc] = Packet;

        E1000_TX_DBG(("TX single element: len=%u pa=0x%I64x desc=%u\n",
                      TransmitLength, TransmitBuffer.QuadPart, Adapter->CurrentTxDesc));

        Status = NICTransmitPacket(Adapter, TransmitBuffer, TransmitLength);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            NDIS_DbgPrint(MIN_TRACE, ("Transmit packet failed\n"));
            E1000_STAT_INC(TxFailed);
            return Status;
        }
    }
    else
    {
        /* Multiple scatter-gather elements - need multiple descriptors */
        ULONG i;

        E1000_TX_DBG(("TX multi-element: %u elements\n", sgList->NumberOfElements));

        if (FreeDesc < sgList->NumberOfElements)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            NDIS_DbgPrint(MIN_TRACE, ("Not enough TX descriptors for SG list (%u needed, %u free)\n",
                                      sgList->NumberOfElements, FreeDesc));
            E1000_TX_DBG(("TX SG OVERFLOW: need=%u free=%u\n",
                          sgList->NumberOfElements, FreeDesc));
            E1000_STAT_INC(TxDropped);
            return NDIS_STATUS_RESOURCES;
        }

        /* Queue all elements */
        for (i = 0; i < sgList->NumberOfElements; i++)
        {
            volatile PE1000_TRANSMIT_DESCRIPTOR TransmitDescriptor;
            ULONG DescIdx = Adapter->CurrentTxDesc;

            E1000_ASSERT_TX_DESC_INDEX(DescIdx);

            TransmitDescriptor = Adapter->TransmitDescriptors + DescIdx;
            TransmitDescriptor->Address = sgList->Elements[i].Address.QuadPart;
            TransmitDescriptor->Length = (USHORT)sgList->Elements[i].Length;
            TransmitDescriptor->ChecksumOffset = 0;
            TransmitDescriptor->ChecksumStartField = 0;
            TransmitDescriptor->Status = 0;
            TransmitDescriptor->Special = 0;

            E1000_TX_DBG(("TX SG[%u]: len=%u pa=0x%I64x desc=%u\n",
                          i, sgList->Elements[i].Length,
                          sgList->Elements[i].Address.QuadPart, DescIdx));

            /* Set command bits - only EOP on last element */
            if (i == sgList->NumberOfElements - 1)
            {
                /* Last element - set EOP and link to packet */
                TransmitDescriptor->Command = E1000_TDESC_CMD_RS | E1000_TDESC_CMD_IFCS |
                                              E1000_TDESC_CMD_EOP | E1000_TDESC_CMD_IDE;
                Adapter->TransmitPackets[DescIdx] = Packet;
            }
            else
            {
                /* Not last - no EOP, no packet reference */
                TransmitDescriptor->Command = E1000_TDESC_CMD_RS | E1000_TDESC_CMD_IFCS;
                Adapter->TransmitPackets[DescIdx] = NULL;
            }

            E1000_STAT_ADD(TxBytes, sgList->Elements[i].Length);

            Adapter->CurrentTxDesc = (Adapter->CurrentTxDesc + 1) % NUM_TRANSMIT_DESCRIPTORS;
        }

        E1000_STAT_INC(TxAttempts);

        if (Adapter->CurrentTxDesc == Adapter->LastTxDesc)
        {
            Adapter->TxFull = TRUE;
            E1000_STAT_INC32(TxRingFull);
        }

        Status = NDIS_STATUS_SUCCESS;
    }

    /* Update TX tail to notify hardware */
    E1000WriteUlong(Adapter, E1000_REG_TDT, Adapter->CurrentTxDesc);

    E1000_TX_DBG(("TX submitted: TDT=%u\n", Adapter->CurrentTxDesc));

    /* Update timestamp */
#if DBG
    KeQueryTickCount(&DebugStats.LastTxTime);
#endif

    NdisReleaseSpinLock(&Adapter->TxLock);

    return NDIS_STATUS_PENDING;
}


/*
 * MiniportSendPackets - Batch packet send handler
 *
 * This handler allows NDIS to submit multiple packets at once for transmission,
 * which is more efficient than single-packet sends. The driver must complete
 * each packet with NdisMSendComplete when transmission is done.
 */
VOID
NTAPI
MiniportSendPackets(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PPNDIS_PACKET PacketArray,
    _In_ UINT NumberOfPackets)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    UINT i;
    ULONG TotalDescriptorsNeeded = 0;
    ULONG FreeDescriptors;
    ULONG PacketsQueued = 0;
    ULONG TotalBytesQueued = 0;

    NDIS_DbgPrint(MAX_TRACE, ("Called with %u packets.\n", NumberOfPackets));

    E1000_TX_DBG(("MiniportSendPackets: %u packets\n", NumberOfPackets));
    E1000_STAT_INC32(TxBatchCount);

    if (NumberOfPackets == 0)
        return;

    NdisAcquireSpinLock(&Adapter->TxLock);

    FreeDescriptors = NICGetFreeTxDescriptors(Adapter);

    E1000_TX_DBG(("TX batch: free descriptors=%u current=%u last=%u\n",
                  FreeDescriptors, Adapter->CurrentTxDesc, Adapter->LastTxDesc));

    /* First pass: count total descriptors needed */
    for (i = 0; i < NumberOfPackets; i++)
    {
        PSCATTER_GATHER_LIST sgList;
        sgList = NDIS_PER_PACKET_INFO_FROM_PACKET(PacketArray[i], ScatterGatherListPacketInfo);

        if (sgList != NULL)
        {
            TotalDescriptorsNeeded += sgList->NumberOfElements;
        }
        else
        {
            TotalDescriptorsNeeded += 1;  /* Assume at least one descriptor per packet */
        }
    }

    NDIS_DbgPrint(MAX_TRACE, ("Need %u descriptors, have %u free\n",
                              TotalDescriptorsNeeded, FreeDescriptors));

    E1000_TX_DBG(("TX batch needs %u descriptors, have %u\n",
                  TotalDescriptorsNeeded, FreeDescriptors));

    /* Second pass: queue packets */
    for (i = 0; i < NumberOfPackets; i++)
    {
        PNDIS_PACKET Packet = PacketArray[i];
        PSCATTER_GATHER_LIST sgList;
        NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
        ULONG j;
        ULONG PacketBytes = 0;

        /* Validate packet */
        if (Packet == NULL)
        {
            NDIS_DbgPrint(MIN_TRACE, ("Packet %u is NULL\n", i));
            E1000_STAT_INC(TxFailed);
            continue;
        }

        sgList = NDIS_PER_PACKET_INFO_FROM_PACKET(Packet, ScatterGatherListPacketInfo);

        if (sgList == NULL)
        {
            /* No scatter-gather list - fail this packet */
            NDIS_DbgPrint(MIN_TRACE, ("Packet %u has no SG list\n", i));
            E1000_TX_DBG(("TX batch packet %u: no SG list, failing\n", i));
            NdisReleaseSpinLock(&Adapter->TxLock);
            NdisMSendComplete(Adapter->AdapterHandle, Packet, NDIS_STATUS_FAILURE);
            NdisAcquireSpinLock(&Adapter->TxLock);
            E1000_STAT_INC(TxFailed);
            continue;
        }

        if (sgList->NumberOfElements == 0)
        {
            NDIS_DbgPrint(MIN_TRACE, ("Packet %u has zero SG elements\n", i));
            NdisReleaseSpinLock(&Adapter->TxLock);
            NdisMSendComplete(Adapter->AdapterHandle, Packet, NDIS_STATUS_FAILURE);
            NdisAcquireSpinLock(&Adapter->TxLock);
            E1000_STAT_INC(TxFailed);
            continue;
        }

        /* Check if we have enough descriptors for this packet */
        FreeDescriptors = NICGetFreeTxDescriptors(Adapter);
        if (FreeDescriptors < sgList->NumberOfElements)
        {
            /* Not enough descriptors - complete with RESOURCES status */
            NDIS_DbgPrint(MID_TRACE, ("Not enough TX descriptors for packet %u "
                                       "(%u needed, %u free)\n",
                                       i, sgList->NumberOfElements, FreeDescriptors));
            E1000_TX_DBG(("TX batch packet %u: not enough descriptors (need=%u free=%u)\n",
                          i, sgList->NumberOfElements, FreeDescriptors));
            NdisReleaseSpinLock(&Adapter->TxLock);
            NdisMSendComplete(Adapter->AdapterHandle, Packet, NDIS_STATUS_RESOURCES);
            NdisAcquireSpinLock(&Adapter->TxLock);
            E1000_STAT_INC(TxDropped);
            continue;
        }

        /* Queue all scatter-gather elements for this packet */
        for (j = 0; j < sgList->NumberOfElements && Status == NDIS_STATUS_SUCCESS; j++)
        {
            volatile PE1000_TRANSMIT_DESCRIPTOR TransmitDescriptor;
            ULONG DescIdx = Adapter->CurrentTxDesc;

            E1000_ASSERT_TX_DESC_INDEX(DescIdx);

            /* Validate element */
            if (sgList->Elements[j].Length == 0)
            {
                NDIS_DbgPrint(MIN_TRACE, ("Packet %u SG element %u has zero length\n", i, j));
                continue;
            }

            if (sgList->Elements[j].Length > MAXIMUM_FRAME_SIZE)
            {
                NDIS_DbgPrint(MIN_TRACE, ("Packet %u SG element %u too large: %u\n",
                                          i, j, sgList->Elements[j].Length));
                Status = NDIS_STATUS_FAILURE;
                break;
            }

            TransmitDescriptor = Adapter->TransmitDescriptors + DescIdx;
            TransmitDescriptor->Address = sgList->Elements[j].Address.QuadPart;
            TransmitDescriptor->Length = (USHORT)sgList->Elements[j].Length;
            TransmitDescriptor->ChecksumOffset = 0;
            TransmitDescriptor->ChecksumStartField = 0;
            TransmitDescriptor->Status = 0;
            TransmitDescriptor->Special = 0;

            PacketBytes += sgList->Elements[j].Length;

            /* Set command bits */
            if (j == sgList->NumberOfElements - 1)
            {
                /* Last element - set EOP and link to packet for completion */
                TransmitDescriptor->Command = E1000_TDESC_CMD_RS | E1000_TDESC_CMD_IFCS |
                                              E1000_TDESC_CMD_EOP | E1000_TDESC_CMD_IDE;
                Adapter->TransmitPackets[DescIdx] = Packet;

                E1000_TX_DBG(("TX batch pkt %u elem %u (EOP): len=%u pa=0x%I64x desc=%u\n",
                              i, j, sgList->Elements[j].Length,
                              sgList->Elements[j].Address.QuadPart, DescIdx));
            }
            else
            {
                /* Not last element - no EOP, no packet reference */
                TransmitDescriptor->Command = E1000_TDESC_CMD_RS | E1000_TDESC_CMD_IFCS;
                Adapter->TransmitPackets[DescIdx] = NULL;

                E1000_TX_DBG(("TX batch pkt %u elem %u: len=%u pa=0x%I64x desc=%u\n",
                              i, j, sgList->Elements[j].Length,
                              sgList->Elements[j].Address.QuadPart, DescIdx));
            }

            Adapter->CurrentTxDesc = (Adapter->CurrentTxDesc + 1) % NUM_TRANSMIT_DESCRIPTORS;

            if (Adapter->CurrentTxDesc == Adapter->LastTxDesc)
            {
                Adapter->TxFull = TRUE;
                E1000_STAT_INC32(TxRingFull);
                E1000_TX_DBG(("TX ring now FULL\n"));
            }
        }

        if (Status == NDIS_STATUS_SUCCESS)
        {
            PacketsQueued++;
            TotalBytesQueued += PacketBytes;
            E1000_STAT_INC(TxAttempts);
            E1000_STAT_ADD(TxBytes, PacketBytes);

            /* Set packet status to pending - completion will happen via interrupt */
            NDIS_SET_PACKET_STATUS(Packet, NDIS_STATUS_PENDING);
        }
        else
        {
            /* Failed to queue - complete with failure */
            NdisReleaseSpinLock(&Adapter->TxLock);
            NdisMSendComplete(Adapter->AdapterHandle, Packet, Status);
            NdisAcquireSpinLock(&Adapter->TxLock);
            E1000_STAT_INC(TxFailed);
        }
    }

    /* Update TX tail to notify hardware of all queued packets at once */
    if (PacketsQueued > 0)
    {
        E1000WriteUlong(Adapter, E1000_REG_TDT, Adapter->CurrentTxDesc);

        NDIS_DbgPrint(MAX_TRACE, ("Queued %u packets, TDT=%u\n",
                                  PacketsQueued, Adapter->CurrentTxDesc));

        E1000_TX_DBG(("TX batch complete: %u packets, %u bytes, TDT=%u\n",
                      PacketsQueued, TotalBytesQueued, Adapter->CurrentTxDesc));

        /* Update descriptor usage tracking */
#if DBG
        {
            ULONG DescUsed = NUM_TRANSMIT_DESCRIPTORS - NICGetFreeTxDescriptors(Adapter);
            if (DescUsed > DebugStats.TxMaxDescriptorsUsed)
            {
                DebugStats.TxMaxDescriptorsUsed = DescUsed;
            }
            DebugStats.TxDescriptorsUsed = DescUsed;
        }

        /* Update timestamp */
        KeQueryTickCount(&DebugStats.LastTxTime);
#endif
    }
    else
    {
        E1000_TX_DBG(("TX batch: no packets queued\n"));
    }

    NdisReleaseSpinLock(&Adapter->TxLock);
}
