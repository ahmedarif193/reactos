/*
 * PROJECT:     ReactOS USB RNDIS Network Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RNDIS data packet handling (send/receive)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This file handles RNDIS data packet encapsulation and decapsulation.
 * RNDIS wraps Ethernet frames in RNDIS_PACKET_MSG headers for transport
 * over USB bulk endpoints.
 */

#include "usbrndis.h"

#define NDEBUG
#include <debug.h>

/*
 * RndisBuildNcmNtb
 *
 * Build a CDC-NCM NTB (Network Transfer Block) containing a single Ethernet frame.
 * Uses NTH16/NDP16 (16-bit pointers) format.
 *
 * NCM NTB layout for single datagram:
 *   [NTH16 (12 bytes)] [NDP16 header + 2 entries (16 bytes)] [Ethernet frame]
 *
 * The NDP16 contains:
 *   - Header (8 bytes): signature, length, next NDP index
 *   - Entry 0 (4 bytes): points to Ethernet frame
 *   - Entry 1 (4 bytes): terminator (0, 0)
 */
static
ULONG
RndisBuildNcmNtb(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR EthernetData,
    IN ULONG EthernetLength,
    OUT PUCHAR OutputBuffer)
{
    PNCM_NTH16 Nth16;
    PNCM_NDP16 Ndp16;
    ULONG NdpOffset;
    ULONG DatagramOffset;
    ULONG TotalLength;
    USHORT NdpLength;

    /*
     * Calculate offsets with alignment.
     * NDP16 follows NTH16 with specified alignment.
     * Datagram follows NDP16 with specified alignment.
     */
    NdpOffset = NCM_NTH16_LENGTH;

    /* Align NDP offset if required */
    if (Adapter->NcmNdpAlignment > 1)
    {
        NdpOffset = (NdpOffset + Adapter->NcmNdpAlignment - 1) &
                    ~(Adapter->NcmNdpAlignment - 1);
    }

    /* NDP16 size: header (8 bytes) + 1 datagram entry (4 bytes) + terminator (4 bytes) = 16 bytes */
    NdpLength = 16;

    /* Datagram follows NDP16 */
    DatagramOffset = NdpOffset + NdpLength;

    /*
     * Apply canonical datagram alignment formula.
     * Goal: find smallest offset >= DatagramOffset where (offset % Divisor == Remainder).
     * Formula: ((offset + divisor - 1 - remainder) / divisor) * divisor + remainder
     *
     * Handle edge case where device reports Remainder >= Divisor by taking modulo.
     */
    if (Adapter->NcmNdpDivisor > 0)
    {
        USHORT Divisor = Adapter->NcmNdpDivisor;
        USHORT Remainder = (Adapter->NcmNdpRemainder < Divisor) ?
                           Adapter->NcmNdpRemainder : (Adapter->NcmNdpRemainder % Divisor);
        DatagramOffset = ((DatagramOffset + Divisor - 1 - Remainder) / Divisor) * Divisor + Remainder;
    }

    TotalLength = DatagramOffset + EthernetLength;

    /*
     * Validate total length fits within device's OUT max NTB size.
     * Also cap to our buffer size as a safety check.
     */
    if (TotalLength > Adapter->NcmNtbOutMaxSize || TotalLength > RNDIS_MAX_TRANSFER_SIZE)
    {
        DPRINT1("USBRNDIS: NCM NTB too large (%lu bytes, max=%lu)\n",
                TotalLength, Adapter->NcmNtbOutMaxSize);
        return 0;
    }

    /* Zero the buffer first */
    NdisZeroMemory(OutputBuffer, TotalLength);

    /* Build NTH16 header */
    Nth16 = (PNCM_NTH16)OutputBuffer;
    Nth16->dwSignature = NCM_NTH16_SIGNATURE;
    Nth16->wHeaderLength = NCM_NTH16_LENGTH;
    Nth16->wSequence = Adapter->NcmTxSequence++;
    Nth16->wBlockLength = (USHORT)TotalLength;
    Nth16->wNdpIndex = (USHORT)NdpOffset;

    /* Build NDP16 header */
    Ndp16 = (PNCM_NDP16)(OutputBuffer + NdpOffset);
    Ndp16->dwSignature = NCM_NDP16_SIGNATURE_NOCRC;  /* NCM0 - no CRC */
    Ndp16->wLength = NdpLength;
    Ndp16->wNextNdpIndex = 0;  /* No more NDPs */

    /* First entry: points to our Ethernet frame */
    Ndp16->Datagram[0].wDatagramIndex = (USHORT)DatagramOffset;
    Ndp16->Datagram[0].wDatagramLength = (USHORT)EthernetLength;

    /* Terminator entry: both fields zero */
    Ndp16->Datagram[1].wDatagramIndex = 0;
    Ndp16->Datagram[1].wDatagramLength = 0;

    /* Copy Ethernet frame */
    NdisMoveMemory(OutputBuffer + DatagramOffset, EthernetData, EthernetLength);

    DPRINT("USBRNDIS: Built NCM NTB: seq=%u len=%lu ndp@%lu data@%lu\n",
           Nth16->wSequence, TotalLength, NdpOffset, DatagramOffset);

    return TotalLength;
}

/*
 * RndisBuildPacketMessage
 *
 * Wrap an Ethernet frame in an RNDIS_PACKET_MSG header.
 * Respects the PacketAlignmentFactor reported by the device.
 */
static
ULONG
RndisBuildPacketMessage(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR EthernetData,
    IN ULONG EthernetLength,
    OUT PUCHAR OutputBuffer)
{
    PRNDIS_PACKET_MSG PacketMsg;
    ULONG TotalLength;
    ULONG DataOffset;
    ULONG AlignmentMask;

    /*
     * Calculate aligned data offset.
     * DataOffset is from the start of the DataOffset field (byte 8 of message).
     * The data must be aligned to (1 << PacketAlignmentFactor) bytes.
     * Standard offset without alignment is sizeof(RNDIS_PACKET_MSG) - 8.
     */
    DataOffset = sizeof(RNDIS_PACKET_MSG) - 8;

    if (Adapter->PacketAlignmentFactor > 0 && Adapter->PacketAlignmentFactor <= 7)
    {
        AlignmentMask = (1U << Adapter->PacketAlignmentFactor) - 1;
        DataOffset = (DataOffset + AlignmentMask) & ~AlignmentMask;
    }

    /* Calculate total message length (header + padding + data) */
    TotalLength = 8 + DataOffset + EthernetLength; /* 8 = offset to DataOffset field */

    /* Build RNDIS packet header */
    PacketMsg = (PRNDIS_PACKET_MSG)OutputBuffer;
    NdisZeroMemory(PacketMsg, sizeof(RNDIS_PACKET_MSG));

    PacketMsg->MessageType = RNDIS_MSG_PACKET;
    PacketMsg->MessageLength = TotalLength;
    PacketMsg->DataOffset = DataOffset;
    PacketMsg->DataLength = EthernetLength;
    PacketMsg->OOBDataOffset = 0;
    PacketMsg->OOBDataLength = 0;
    PacketMsg->NumOOBDataElements = 0;
    PacketMsg->PerPacketInfoOffset = 0;
    PacketMsg->PerPacketInfoLength = 0;
    PacketMsg->VcHandle = 0;
    PacketMsg->Reserved = 0;

    /* Copy Ethernet data at aligned offset (DataOffset is from byte 8) */
    NdisMoveMemory(OutputBuffer + 8 + DataOffset, EthernetData, EthernetLength);

    return TotalLength;
}

/*
 * RndisIndicateEthernetFrameInternal
 *
 * Internal helper to indicate a single Ethernet frame to NDIS.
 * Does NOT call NdisMEthIndicateReceiveComplete - caller must do that
 * after indicating all frames in a batch.
 *
 * Returns TRUE if frame was indicated, FALSE on validation error.
 */
static
BOOLEAN
RndisIndicateEthernetFrameInternal(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR EthernetData,
    IN ULONG EthernetLength)
{
    /* Validate Ethernet frame length */
    if (EthernetLength < ETHERNET_HEADER_SIZE || EthernetLength > ETHERNET_MAX_FRAME_SIZE)
    {
        DPRINT1("USBRNDIS: Invalid Ethernet frame length %u\n", EthernetLength);
        Adapter->RxErrorCount++;
        return FALSE;
    }

    /* Update statistics */
    Adapter->RxOkCount++;

    /*
     * Use NdisMEthIndicateReceive to indicate received Ethernet frame.
     * This is simpler than managing packet pools and works well for
     * low-throughput USB devices.
     */
    NdisMEthIndicateReceive(
        Adapter->MiniportAdapterHandle,
        NULL,                           /* MacReceiveContext - not used */
        (PCHAR)EthernetData,            /* HeaderBuffer - Ethernet header */
        ETHERNET_HEADER_SIZE,           /* HeaderBufferSize */
        (PCHAR)EthernetData + ETHERNET_HEADER_SIZE,  /* LookaheadBuffer */
        EthernetLength - ETHERNET_HEADER_SIZE,       /* LookaheadBufferSize */
        EthernetLength - ETHERNET_HEADER_SIZE);      /* PacketSize */

    return TRUE;
}

/*
 * RndisProcessNcmNtb
 *
 * Parse a CDC-NCM NTB (Network Transfer Block) and extract all Ethernet frames.
 * Validates NTH16 and NDP16 headers, then iterates through datagram pointers.
 *
 * Returns TRUE if at least one valid frame was processed, FALSE on error.
 */
static
BOOLEAN
RndisProcessNcmNtb(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR Data,
    IN ULONG Length)
{
    PNCM_NTH16 Nth16;
    PNCM_NDP16 Ndp16;
    PNCM_NDP16_ENTRY Entry;
    ULONG NdpOffset;
    ULONG FramesProcessed = 0;
    ULONG EntryIndex;
    ULONG MaxEntries;

    /* Validate minimum length for NTH16 */
    if (Length < NCM_NTH16_LENGTH)
    {
        DPRINT1("USBRNDIS: NCM data too short for NTH16 (%lu bytes)\n", Length);
        Adapter->RxErrorCount++;
        return FALSE;
    }

    /* Validate NTH16 header */
    Nth16 = (PNCM_NTH16)Data;

    if (Nth16->dwSignature != NCM_NTH16_SIGNATURE)
    {
        DPRINT1("USBRNDIS: Invalid NTH16 signature 0x%08X (expected 0x%08X)\n",
                Nth16->dwSignature, NCM_NTH16_SIGNATURE);
        Adapter->RxErrorCount++;
        return FALSE;
    }

    if (Nth16->wHeaderLength != NCM_NTH16_LENGTH)
    {
        DPRINT1("USBRNDIS: Invalid NTH16 header length %u (expected %u)\n",
                Nth16->wHeaderLength, NCM_NTH16_LENGTH);
        Adapter->RxErrorCount++;
        return FALSE;
    }

    if (Nth16->wBlockLength > Length)
    {
        DPRINT1("USBRNDIS: NTH16 block length %u exceeds received length %lu\n",
                Nth16->wBlockLength, Length);
        Adapter->RxErrorCount++;
        return FALSE;
    }

    /* Get offset to first NDP16 */
    NdpOffset = Nth16->wNdpIndex;
    if (NdpOffset == 0)
    {
        /* No NDP - empty NTB, not an error */
        DPRINT("USBRNDIS: NCM NTB has no NDP (empty)\n");
        return TRUE;
    }

    DPRINT("USBRNDIS: Processing NCM NTB: seq=%u len=%u ndp@%lu\n",
           Nth16->wSequence, Nth16->wBlockLength, NdpOffset);

    /* Process each NDP16 in the chain */
    while (NdpOffset != 0)
    {
        /* Validate NDP16 offset and minimum size */
        if (NdpOffset + NCM_NDP16_MIN_LENGTH > Length)
        {
            DPRINT1("USBRNDIS: NDP16 offset %lu exceeds data length %lu\n",
                    NdpOffset, Length);
            Adapter->RxErrorCount++;
            break;
        }

        Ndp16 = (PNCM_NDP16)(Data + NdpOffset);

        /* Validate NDP16 signature */
        if (Ndp16->dwSignature != NCM_NDP16_SIGNATURE_NOCRC &&
            Ndp16->dwSignature != NCM_NDP16_SIGNATURE_CRC)
        {
            DPRINT1("USBRNDIS: Invalid NDP16 signature 0x%08X\n", Ndp16->dwSignature);
            Adapter->RxErrorCount++;
            break;
        }

        /* Validate NDP16 length */
        if (Ndp16->wLength < NCM_NDP16_MIN_LENGTH ||
            NdpOffset + Ndp16->wLength > Length)
        {
            DPRINT1("USBRNDIS: Invalid NDP16 length %u at offset %lu\n",
                    Ndp16->wLength, NdpOffset);
            Adapter->RxErrorCount++;
            break;
        }

        /*
         * Calculate maximum number of entries in this NDP16.
         * NDP16 header is 8 bytes, each entry is 4 bytes.
         * Must have at least 2 entries (1 datagram + 1 terminator).
         */
        MaxEntries = (Ndp16->wLength - 8) / sizeof(NCM_NDP16_ENTRY);

        DPRINT("USBRNDIS: NDP16 at offset %lu: sig=0x%08X len=%u max_entries=%lu\n",
               NdpOffset, Ndp16->dwSignature, Ndp16->wLength, MaxEntries);

        /* Process datagram entries */
        for (EntryIndex = 0; EntryIndex < MaxEntries; EntryIndex++)
        {
            Entry = &Ndp16->Datagram[EntryIndex];

            /* Terminator entry: both fields are zero */
            if (Entry->wDatagramIndex == 0 && Entry->wDatagramLength == 0)
            {
                break;
            }

            /* Validate datagram bounds */
            if (Entry->wDatagramIndex + Entry->wDatagramLength > Length)
            {
                DPRINT1("USBRNDIS: Datagram[%lu] extends past NTB end (idx=%u len=%u total=%lu)\n",
                        EntryIndex, Entry->wDatagramIndex, Entry->wDatagramLength, Length);
                Adapter->RxErrorCount++;
                continue;
            }

            /* Skip empty datagrams */
            if (Entry->wDatagramLength == 0)
            {
                continue;
            }

            DPRINT("USBRNDIS: NCM datagram[%lu]: offset=%u length=%u\n",
                   EntryIndex, Entry->wDatagramIndex, Entry->wDatagramLength);

            /* Indicate the Ethernet frame to NDIS (no ReceiveComplete yet) */
            if (RndisIndicateEthernetFrameInternal(
                    Adapter,
                    Data + Entry->wDatagramIndex,
                    Entry->wDatagramLength))
            {
                FramesProcessed++;
            }
        }

        /* Move to next NDP16 in chain */
        NdpOffset = Ndp16->wNextNdpIndex;
    }

    /*
     * Call ReceiveComplete once after indicating all frames in this NTB.
     * This is more efficient than calling it per-frame.
     */
    if (FramesProcessed > 0)
    {
        NdisMEthIndicateReceiveComplete(Adapter->MiniportAdapterHandle);
    }

    DPRINT("USBRNDIS: NCM NTB processing complete: %lu frames\n", FramesProcessed);
    return (FramesProcessed > 0);
}

/*
 * RndisProcessReceivedPacket
 *
 * Process received RNDIS packet data and deliver to NDIS
 * Handles RNDIS, CDC-ECM, and CDC-NCM formats.
 */
VOID
RndisProcessReceivedPacket(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR Data,
    IN ULONG Length)
{
    PRNDIS_PACKET_MSG PacketMsg;
    PUCHAR EthernetData;
    ULONG EthernetLength;
    ULONG DataOffset;

    DPRINT("USBRNDIS: Processing received data (%u bytes)\n", Length);

    /*
     * CDC-NCM mode: Parse NTB (Network Transfer Block) structure.
     */
    if (Adapter->IsCdcNcm)
    {
        RndisProcessNcmNtb(Adapter, Data, Length);
        return;
    }

    /*
     * CDC-ECM mode: Data is raw Ethernet frame, no RNDIS header.
     */
    if (Adapter->IsCdcEcm)
    {
        EthernetData = Data;
        EthernetLength = Length;

        /* Validate Ethernet frame length */
        if (EthernetLength < ETHERNET_HEADER_SIZE || EthernetLength > ETHERNET_MAX_FRAME_SIZE)
        {
            DPRINT1("USBRNDIS: Invalid CDC-ECM frame length %u\n", EthernetLength);
            Adapter->RxErrorCount++;
            return;
        }

        DPRINT("USBRNDIS: Received CDC-ECM Ethernet frame (%u bytes)\n", EthernetLength);
    }
    else
    {
        /* RNDIS mode: Unwrap Ethernet frame from RNDIS_PACKET_MSG */
        if (Length < sizeof(RNDIS_PACKET_MSG))
        {
            DPRINT1("USBRNDIS: Received data too short for RNDIS header\n");
            Adapter->RxErrorCount++;
            return;
        }

        PacketMsg = (PRNDIS_PACKET_MSG)Data;

        /* Validate message type */
        if (PacketMsg->MessageType != RNDIS_MSG_PACKET)
        {
            DPRINT1("USBRNDIS: Unexpected message type 0x%08X in data\n", PacketMsg->MessageType);
            Adapter->RxErrorCount++;
            return;
        }

        /* Validate message length */
        if (PacketMsg->MessageLength > Length)
        {
            DPRINT1("USBRNDIS: Message length %u exceeds received length %u\n",
                    PacketMsg->MessageLength, Length);
            Adapter->RxErrorCount++;
            return;
        }

        /* Get data offset and length */
        DataOffset = PacketMsg->DataOffset + 8; /* Offset is from DataOffset field start */
        EthernetLength = PacketMsg->DataLength;

        /* Validate data bounds */
        if (DataOffset + EthernetLength > PacketMsg->MessageLength)
        {
            DPRINT1("USBRNDIS: Data extends past message end (offset=%u len=%u msglen=%u)\n",
                    DataOffset, EthernetLength, PacketMsg->MessageLength);
            Adapter->RxErrorCount++;
            return;
        }

        EthernetData = Data + DataOffset;

        /* Validate Ethernet frame length */
        if (EthernetLength < ETHERNET_HEADER_SIZE || EthernetLength > ETHERNET_MAX_FRAME_SIZE)
        {
            DPRINT1("USBRNDIS: Invalid Ethernet frame length %u\n", EthernetLength);
            Adapter->RxErrorCount++;
            return;
        }

        DPRINT("USBRNDIS: Received RNDIS Ethernet frame (%u bytes)\n", EthernetLength);
    }

    /*
     * Indicate Ethernet frame to NDIS using common helper.
     * Stats are updated inside the helper (single source of truth).
     */
    if (RndisIndicateEthernetFrameInternal(Adapter, EthernetData, EthernetLength))
    {
        /* Complete the receive indication for this single frame */
        NdisMEthIndicateReceiveComplete(Adapter->MiniportAdapterHandle);
    }
}

/*
 * RndisSend
 *
 * NDIS miniport send handler - send a single packet.
 * Returns NDIS_STATUS_PENDING and calls NdisMSendComplete from completion routine.
 */
NDIS_STATUS
NTAPI
RndisSend(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_PACKET Packet,
    IN UINT Flags)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    PNDIS_BUFFER Buffer;
    PVOID VirtualAddress;
    UINT BufferLength;
    ULONG TotalLength;
    UINT PacketTotalLength;
    ULONG PacketLength;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Flags);

    DPRINT("USBRNDIS: RndisSend called\n");

    if (Adapter->State != RndisStateDataInitialized)
    {
        DPRINT1("USBRNDIS: Send called but adapter not initialized\n");
        return NDIS_STATUS_FAILURE;
    }

    /* Check if halting */
    if (Adapter->Halting)
    {
        return NDIS_STATUS_FAILURE;
    }

    /* Acquire TX lock */
    NdisAcquireSpinLock(&Adapter->TxLock);

    if (Adapter->TxBusy)
    {
        NdisReleaseSpinLock(&Adapter->TxLock);
        DPRINT1("USBRNDIS: TX busy\n");
        return NDIS_STATUS_RESOURCES;
    }

    Adapter->TxBusy = TRUE;
    Adapter->PendingTxPacket = Packet;  /* Store packet for completion callback */
    NdisReleaseSpinLock(&Adapter->TxLock);

    /* Calculate total packet length */
    NdisQueryPacket(Packet, NULL, NULL, NULL, &PacketTotalLength);

    if (PacketTotalLength > ETHERNET_MAX_FRAME_SIZE)
    {
        DPRINT1("USBRNDIS: Packet too large (%u bytes)\n", PacketTotalLength);
        NdisAcquireSpinLock(&Adapter->TxLock);
        Adapter->TxBusy = FALSE;
        Adapter->PendingTxPacket = NULL;
        NdisReleaseSpinLock(&Adapter->TxLock);
        Adapter->TxErrorCount++;
        return NDIS_STATUS_FAILURE;
    }

    /*
     * Copy packet data to TX buffer.
     * For RNDIS: Copy after header space, then prepend RNDIS header.
     * For CDC-ECM: Copy directly to start of buffer (no header).
     * For CDC-NCM: Copy to temp area, then build NTB with NTH16/NDP16.
     */
    PacketLength = 0;
    NdisQueryPacket(Packet, NULL, NULL, &Buffer, NULL);

    if (Adapter->IsCdcEcm)
    {
        /* CDC-ECM: Copy Ethernet frame directly */
        while (Buffer)
        {
            NdisQueryBuffer(Buffer, &VirtualAddress, &BufferLength);

            if (PacketLength + BufferLength > ETHERNET_MAX_FRAME_SIZE)
            {
                DPRINT1("USBRNDIS: Packet overflow during copy\n");
                NdisAcquireSpinLock(&Adapter->TxLock);
                Adapter->TxBusy = FALSE;
                Adapter->PendingTxPacket = NULL;
                NdisReleaseSpinLock(&Adapter->TxLock);
                Adapter->TxErrorCount++;
                return NDIS_STATUS_FAILURE;
            }

            NdisMoveMemory(Adapter->TxBuffer + PacketLength, VirtualAddress, BufferLength);
            PacketLength += BufferLength;

            NdisGetNextBuffer(Buffer, &Buffer);
        }

        TotalLength = PacketLength;
        DPRINT("USBRNDIS: CDC-ECM TX frame (%u bytes)\n", TotalLength);
    }
    else if (Adapter->IsCdcNcm)
    {
        /*
         * CDC-NCM: Copy Ethernet frame to temp area in buffer (after max NTB header space),
         * then build NTB from start of buffer. Max NTB overhead is ~64 bytes.
         */
        ULONG TempOffset = 64;  /* Reserve space for NTH16 + NDP16 */

        while (Buffer)
        {
            NdisQueryBuffer(Buffer, &VirtualAddress, &BufferLength);

            if (PacketLength + BufferLength > ETHERNET_MAX_FRAME_SIZE)
            {
                DPRINT1("USBRNDIS: Packet overflow during copy\n");
                NdisAcquireSpinLock(&Adapter->TxLock);
                Adapter->TxBusy = FALSE;
                Adapter->PendingTxPacket = NULL;
                NdisReleaseSpinLock(&Adapter->TxLock);
                Adapter->TxErrorCount++;
                return NDIS_STATUS_FAILURE;
            }

            NdisMoveMemory(Adapter->TxBuffer + TempOffset + PacketLength,
                           VirtualAddress, BufferLength);
            PacketLength += BufferLength;

            NdisGetNextBuffer(Buffer, &Buffer);
        }

        /* Build NCM NTB (this writes NTH16 + NDP16 + copies frame to proper offset) */
        TotalLength = RndisBuildNcmNtb(Adapter,
                                       Adapter->TxBuffer + TempOffset,
                                       PacketLength,
                                       Adapter->TxBuffer);

        if (TotalLength == 0)
        {
            DPRINT1("USBRNDIS: Failed to build NCM NTB\n");
            NdisAcquireSpinLock(&Adapter->TxLock);
            Adapter->TxBusy = FALSE;
            Adapter->PendingTxPacket = NULL;
            NdisReleaseSpinLock(&Adapter->TxLock);
            Adapter->TxErrorCount++;
            return NDIS_STATUS_FAILURE;
        }

        DPRINT("USBRNDIS: CDC-NCM TX NTB (%lu bytes, frame %lu bytes)\n",
               TotalLength, PacketLength);
    }
    else
    {
        /* RNDIS: Copy after header space */
        while (Buffer)
        {
            NdisQueryBuffer(Buffer, &VirtualAddress, &BufferLength);

            if (PacketLength + BufferLength > ETHERNET_MAX_FRAME_SIZE)
            {
                DPRINT1("USBRNDIS: Packet overflow during copy\n");
                NdisAcquireSpinLock(&Adapter->TxLock);
                Adapter->TxBusy = FALSE;
                Adapter->PendingTxPacket = NULL;
                NdisReleaseSpinLock(&Adapter->TxLock);
                Adapter->TxErrorCount++;
                return NDIS_STATUS_FAILURE;
            }

            NdisMoveMemory(Adapter->TxBuffer + sizeof(RNDIS_PACKET_MSG) + PacketLength,
                           VirtualAddress, BufferLength);
            PacketLength += BufferLength;

            NdisGetNextBuffer(Buffer, &Buffer);
        }

        /* Build RNDIS packet message (this overwrites the header area) */
        TotalLength = RndisBuildPacketMessage(Adapter,
                                              Adapter->TxBuffer + sizeof(RNDIS_PACKET_MSG),
                                              PacketLength,
                                              Adapter->TxBuffer);
    }

    /* Send via USB bulk endpoint - async operation */
    Status = RndisUsbSubmitBulkWrite(Adapter, Adapter->TxBuffer, TotalLength);

    if (Status == STATUS_PENDING)
    {
        /*
         * URB submitted successfully, will complete asynchronously.
         * NdisMSendComplete will be called from RndisTxComplete completion routine
         * (because Irp->PendingReturned will be TRUE).
         */
        DPRINT("USBRNDIS: TX submitted async (%lu bytes)\n", PacketLength);
        return NDIS_STATUS_PENDING;
    }
    else if (NT_SUCCESS(Status))
    {
        /*
         * URB completed synchronously (STATUS_SUCCESS).
         * The completion routine has ALREADY run, but did NOT call NdisMSendComplete
         * (because Irp->PendingReturned was FALSE).
         *
         * Return NDIS_STATUS_SUCCESS so RndisSendPackets will call NdisMSendComplete.
         * The completion routine already cleared TxBusy/PendingTxPacket and updated stats.
         */
        DPRINT("USBRNDIS: TX completed sync (%lu bytes)\n", PacketLength);
        return NDIS_STATUS_SUCCESS;
    }
    else
    {
        /*
         * Failed to submit URB (STATUS_INSUFFICIENT_RESOURCES or other error).
         * The completion callback will NOT occur, so clean up here.
         */
        DPRINT1("USBRNDIS: Failed to submit TX (0x%08X)\n", Status);
        NdisAcquireSpinLock(&Adapter->TxLock);
        Adapter->TxBusy = FALSE;
        Adapter->PendingTxPacket = NULL;
        NdisReleaseSpinLock(&Adapter->TxLock);
        Adapter->TxErrorCount++;
        return NDIS_STATUS_FAILURE;
    }
}

/*
 * RndisSendPackets
 *
 * NDIS miniport send packets handler - send multiple packets.
 * Since we can only have one TX pending at a time, we send the first
 * and queue/fail the rest.
 */
VOID
NTAPI
RndisSendPackets(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PPNDIS_PACKET PacketArray,
    IN UINT NumberOfPackets)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    UINT i;
    NDIS_STATUS Status;

    DPRINT("USBRNDIS: RndisSendPackets called (%u packets)\n", NumberOfPackets);

    for (i = 0; i < NumberOfPackets; i++)
    {
        Status = RndisSend(MiniportAdapterContext, PacketArray[i], 0);

        /*
         * Set packet status for NDIS.
         * RndisSend returns:
         *   NDIS_STATUS_PENDING - completion handled by RndisTxComplete callback
         *   NDIS_STATUS_FAILURE - immediate failure, must complete here
         *   NDIS_STATUS_RESOURCES - TX busy, must complete here
         */
        NDIS_SET_PACKET_STATUS(PacketArray[i], Status);

        if (Status != NDIS_STATUS_PENDING)
        {
            /*
             * Immediate completion for non-pending statuses.
             * For PENDING, RndisTxComplete will call NdisMSendComplete.
             */
            NdisMSendComplete(Adapter->MiniportAdapterHandle, PacketArray[i], Status);
        }
    }
}
