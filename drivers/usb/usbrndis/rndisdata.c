/*
 * PROJECT:     ReactOS USB RNDIS Network Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RNDIS data packet handling (send/receive) - NDIS 6.x
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This file handles RNDIS data packet encapsulation and decapsulation
 * using NDIS 6.x NET_BUFFER_LIST structures.
 * RNDIS wraps Ethernet frames in RNDIS_PACKET_MSG headers for transport
 * over USB bulk endpoints.
 */

#include "usbrndis.h"

/* Enable debug output for troubleshooting */
#include <debug.h>

/*
 * Maximum datagrams in a single NTB for TX batching.
 * Each datagram needs 4 bytes in the NDP16 entry table.
 * Keep this reasonable to avoid excessive latency and buffer usage.
 */
#define NCM_MAX_TX_DATAGRAMS 32

/*
 * NCM TX Datagram descriptor for batching.
 * Used to collect multiple datagrams before building NTB.
 */
typedef struct _NCM_TX_DATAGRAM {
    PUCHAR Data;
    ULONG Length;
} NCM_TX_DATAGRAM, *PNCM_TX_DATAGRAM;

/*
 * RndisAlignOffset
 *
 * Calculate aligned offset given alignment constraints.
 * Finds smallest offset >= CurrentOffset where (offset % Divisor == Remainder).
 */
static
__inline
ULONG
RndisAlignOffset(
    IN ULONG CurrentOffset,
    IN USHORT Divisor,
    IN USHORT Remainder)
{
    if (Divisor == 0 || Divisor == 1)
    {
        return CurrentOffset;
    }

    /* Normalize remainder if it exceeds divisor */
    if (Remainder >= Divisor)
    {
        Remainder = Remainder % Divisor;
    }

    /* Formula: ((offset + divisor - 1 - remainder) / divisor) * divisor + remainder */
    return ((CurrentOffset + Divisor - 1 - Remainder) / Divisor) * Divisor + Remainder;
}

/*
 * RndisBuildNcmNtbMulti
 *
 * Build a CDC-NCM NTB (Network Transfer Block) containing multiple Ethernet frames.
 * Uses NTH16/NDP16 (16-bit pointers) format.
 *
 * NCM NTB layout for multiple datagrams:
 *   [NTH16 (12 bytes)]
 *   [NDP16 header + N+1 entries (8 + (N+1)*4 bytes)]
 *   [Datagram 0 (aligned)]
 *   [Datagram 1 (aligned)]
 *   ...
 *   [Datagram N-1 (aligned)]
 *
 * The NDP16 contains:
 *   - Header (8 bytes): signature, length, next NDP index
 *   - Entry 0..N-1 (4 bytes each): points to each Ethernet frame
 *   - Entry N (4 bytes): terminator (0, 0)
 *
 * Parameters:
 *   Adapter - Adapter context
 *   Datagrams - Array of datagram descriptors
 *   DatagramCount - Number of datagrams (1 to NCM_MAX_TX_DATAGRAMS)
 *   OutputBuffer - Output buffer for NTB (must be at least NcmNtbOutMaxSize bytes)
 *
 * Returns:
 *   Total NTB length on success, 0 on failure.
 */
static
ULONG
RndisBuildNcmNtbMulti(
    IN PRNDIS_ADAPTER Adapter,
    IN PNCM_TX_DATAGRAM Datagrams,
    IN ULONG DatagramCount,
    OUT PUCHAR OutputBuffer)
{
    PNCM_NTH16 Nth16;
    PNCM_NDP16 Ndp16;
    ULONG NdpOffset;
    ULONG DataOffset;
    ULONG TotalLength;
    USHORT NdpLength;
    ULONG i;

    if (DatagramCount == 0 || DatagramCount > NCM_MAX_TX_DATAGRAMS)
    {
        DPRINT1("USBRNDIS: Invalid datagram count %lu\n", DatagramCount);
        return 0;
    }

    /*
     * Calculate NDP offset with alignment.
     * NDP16 follows NTH16.
     */
    NdpOffset = NCM_NTH16_LENGTH;
    if (Adapter->NcmNdpAlignment > 1)
    {
        NdpOffset = (NdpOffset + Adapter->NcmNdpAlignment - 1) &
                    ~(Adapter->NcmNdpAlignment - 1);
    }

    /*
     * Calculate NDP16 size.
     * Header (8 bytes) + N datagram entries (4 bytes each) + terminator (4 bytes)
     */
    NdpLength = (USHORT)(8 + (DatagramCount + 1) * sizeof(NCM_NDP16_ENTRY));

    /*
     * First datagram follows NDP16 with alignment.
     */
    DataOffset = NdpOffset + NdpLength;
    DataOffset = RndisAlignOffset(DataOffset, Adapter->NcmNdpDivisor, Adapter->NcmNdpRemainder);

    /*
     * Calculate total length by iterating through all datagrams.
     * Each datagram is placed with required alignment.
     */
    TotalLength = DataOffset;
    for (i = 0; i < DatagramCount; i++)
    {
        if (i > 0)
        {
            /* Apply alignment for subsequent datagrams */
            TotalLength = RndisAlignOffset(TotalLength, Adapter->NcmNdpDivisor, Adapter->NcmNdpRemainder);
        }
        TotalLength += Datagrams[i].Length;
    }

    /*
     * Validate total length.
     */
    if (TotalLength > Adapter->NcmNtbOutMaxSize || TotalLength > RNDIS_MAX_TRANSFER_SIZE)
    {
        DPRINT1("USBRNDIS: NCM NTB too large (%lu bytes, max=%lu)\n",
                TotalLength, Adapter->NcmNtbOutMaxSize);
        return 0;
    }

    if (TotalLength > 0xFFFF)
    {
        DPRINT1("USBRNDIS: NCM NTB16 block length overflow (%lu > 65535)\n", TotalLength);
        return 0;
    }

    /* Zero the header portion of the buffer */
    NdisZeroMemory(OutputBuffer, NdpOffset + NdpLength);

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

    /* Build NDP entries and copy datagrams */
    DataOffset = NdpOffset + NdpLength;
    DataOffset = RndisAlignOffset(DataOffset, Adapter->NcmNdpDivisor, Adapter->NcmNdpRemainder);

    for (i = 0; i < DatagramCount; i++)
    {
        if (i > 0)
        {
            DataOffset = RndisAlignOffset(DataOffset, Adapter->NcmNdpDivisor, Adapter->NcmNdpRemainder);
        }

        /* Set NDP entry */
        Ndp16->Datagram[i].wDatagramIndex = (USHORT)DataOffset;
        Ndp16->Datagram[i].wDatagramLength = (USHORT)Datagrams[i].Length;

        /* Copy Ethernet frame */
        NdisMoveMemory(OutputBuffer + DataOffset, Datagrams[i].Data, Datagrams[i].Length);

        DataOffset += Datagrams[i].Length;
    }

    /* Terminator entry: both fields zero */
    Ndp16->Datagram[DatagramCount].wDatagramIndex = 0;
    Ndp16->Datagram[DatagramCount].wDatagramLength = 0;

    DPRINT1("USBRNDIS: Built NCM NTB with %lu datagrams: seq=%u len=%lu ndp@%lu\n",
           DatagramCount, Nth16->wSequence, TotalLength, NdpOffset);

    return TotalLength;
}

/*
 * RndisBuildNcmNtb
 *
 * Build a CDC-NCM NTB (Network Transfer Block) containing a single Ethernet frame.
 * This is a convenience wrapper around RndisBuildNcmNtbMulti for single-datagram case.
 */
static
ULONG
RndisBuildNcmNtb(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR EthernetData,
    IN ULONG EthernetLength,
    OUT PUCHAR OutputBuffer)
{
    NCM_TX_DATAGRAM Datagram;

    Datagram.Data = EthernetData;
    Datagram.Length = EthernetLength;

    return RndisBuildNcmNtbMulti(Adapter, &Datagram, 1, OutputBuffer);
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
 * RndisIndicateReceiveNblEx
 *
 * Build and indicate a NET_BUFFER_LIST for a received Ethernet frame.
 * This is the NDIS 6.x replacement for NdisMEthIndicateReceive.
 *
 * Parameters:
 *   Adapter - Pointer to adapter context
 *   EthernetData - Pointer to the Ethernet frame data
 *   EthernetLength - Length of the Ethernet frame
 *   ChecksumInfo - Optional pointer to checksum validation results from device
 */
static
VOID
RndisIndicateReceiveNblEx(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR EthernetData,
    IN ULONG EthernetLength,
    IN PRNDIS_TCPIP_CSUM_INFO ChecksumInfo OPTIONAL)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER Nb;
    PMDL Mdl;
    PUCHAR DataCopy;

    /* Validate Ethernet frame length */
    if (EthernetLength < ETHERNET_HEADER_SIZE || EthernetLength > ETHERNET_MAX_FRAME_SIZE)
    {
        DPRINT1("USBRNDIS: Invalid Ethernet frame length %u\n", EthernetLength);
        Adapter->RxErrorCount++;
        return;
    }

    /* Allocate memory for the data copy - required because USB RX buffer is reused */
    DataCopy = NdisAllocateMemoryWithTagPriority(
                    Adapter->MiniportAdapterHandle,
                    EthernetLength,
                    USBRNDIS_TAG,
                    NormalPoolPriority);

    if (DataCopy == NULL)
    {
        DPRINT1("USBRNDIS: Failed to allocate RX data copy buffer\n");
        Adapter->RxNoBufferCount++;
        return;
    }

    /* Copy the Ethernet frame data */
    NdisMoveMemory(DataCopy, EthernetData, EthernetLength);

    /* Allocate MDL for the data */
    Mdl = NdisAllocateMdl(
            Adapter->MiniportAdapterHandle,
            DataCopy,
            EthernetLength);

    if (Mdl == NULL)
    {
        DPRINT1("USBRNDIS: Failed to allocate RX MDL\n");
        NdisFreeMemory(DataCopy, EthernetLength, 0);
        Adapter->RxNoBufferCount++;
        return;
    }

    /* Allocate NET_BUFFER_LIST with attached NET_BUFFER */
    Nbl = NdisAllocateNetBufferAndNetBufferList(
            Adapter->RxNblPool,
            0,      /* Context size */
            0,      /* Context backfill */
            Mdl,
            0,      /* Data offset */
            EthernetLength);

    if (Nbl == NULL)
    {
        DPRINT1("USBRNDIS: Failed to allocate RX NBL\n");
        NdisFreeMdl(Mdl);
        NdisFreeMemory(DataCopy, EthernetLength, 0);
        Adapter->RxNoBufferCount++;
        return;
    }

    /* Store the data buffer pointer in NBL context for cleanup */
    NET_BUFFER_LIST_INFO(Nbl, MediaSpecificInformation) = DataCopy;

    /* Set source handle */
    Nbl->SourceHandle = Adapter->MiniportAdapterHandle;

    /*
     * Set TCP/IP checksum offload information if provided by device.
     * This tells the network stack whether hardware verified the checksums.
     */
    if (ChecksumInfo != NULL && ChecksumInfo->Value != 0)
    {
        NDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO NblChecksumInfo;

        NblChecksumInfo.Value = 0;

        /*
         * Map RNDIS checksum results to NDIS checksum info.
         * RNDIS uses separate flags for success/failure, NDIS uses succeeded/failed.
         */
        if (ChecksumInfo->Receive.TcpChecksumSucceeded)
        {
            NblChecksumInfo.Receive.TcpChecksumSucceeded = TRUE;
        }
        else if (ChecksumInfo->Receive.TcpChecksumFailed)
        {
            NblChecksumInfo.Receive.TcpChecksumFailed = TRUE;
        }

        if (ChecksumInfo->Receive.UdpChecksumSucceeded)
        {
            NblChecksumInfo.Receive.UdpChecksumSucceeded = TRUE;
        }
        else if (ChecksumInfo->Receive.UdpChecksumFailed)
        {
            NblChecksumInfo.Receive.UdpChecksumFailed = TRUE;
        }

        if (ChecksumInfo->Receive.IpChecksumSucceeded)
        {
            NblChecksumInfo.Receive.IpChecksumSucceeded = TRUE;
        }
        else if (ChecksumInfo->Receive.IpChecksumFailed)
        {
            NblChecksumInfo.Receive.IpChecksumFailed = TRUE;
        }

        if (NblChecksumInfo.Value != 0)
        {
            NET_BUFFER_LIST_INFO(Nbl, TcpIpChecksumNetBufferListInfo) = NblChecksumInfo.Value;
            DPRINT("USBRNDIS: Set RX checksum info: 0x%08X\n", NblChecksumInfo.Value);
        }
    }

    /* Update statistics */
    Adapter->RxOkCount++;
    Adapter->RxBytes += EthernetLength;

    DPRINT("USBRNDIS: Indicating RX NBL (%lu bytes)\n", EthernetLength);

    /* Indicate to NDIS */
    NdisMIndicateReceiveNetBufferLists(
        Adapter->MiniportAdapterHandle,
        Nbl,
        0,      /* Port number */
        1,      /* Number of NBLs */
        0       /* Flags - not at dispatch level from USB completion */
        );
}

/*
 * RndisIndicateReceiveNbl
 *
 * Wrapper for backward compatibility - indicates without checksum info.
 */
static
VOID
RndisIndicateReceiveNbl(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR EthernetData,
    IN ULONG EthernetLength)
{
    RndisIndicateReceiveNblEx(Adapter, EthernetData, EthernetLength, NULL);
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

            /* Indicate the Ethernet frame to NDIS via NBL */
            RndisIndicateReceiveNbl(
                Adapter,
                Data + Entry->wDatagramIndex,
                Entry->wDatagramLength);

            FramesProcessed++;
        }

        /* Move to next NDP16 in chain */
        NdpOffset = Ndp16->wNextNdpIndex;
    }

    DPRINT("USBRNDIS: NCM NTB processing complete: %lu frames\n", FramesProcessed);
    return (FramesProcessed > 0);
}

/*
 * RndisProcessReceivedPacket
 *
 * Process received RNDIS packet data and deliver to NDIS.
 * Handles RNDIS, CDC-ECM, and CDC-NCM formats.
 * Uses NDIS 6.x NET_BUFFER_LIST indication.
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

    DPRINT1("USBRNDIS: Processing received data (%u bytes)\n", Length);

    /* Check if paused */
    if (Adapter->Paused)
    {
        DPRINT("USBRNDIS: Adapter paused, dropping received packet\n");
        return;
    }

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
        RNDIS_TCPIP_CSUM_INFO ChecksumInfo;
        PRNDIS_TCPIP_CSUM_INFO pChecksumInfo = NULL;

        ChecksumInfo.Value = 0;

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

        /*
         * Process per-packet info if present.
         * Per-packet info contains metadata like checksum validation results.
         * PerPacketInfoOffset is relative to the start of DataOffset field (byte 8).
         */
        if (PacketMsg->PerPacketInfoLength > 0 && PacketMsg->PerPacketInfoOffset > 0)
        {
            ULONG PpiOffset = PacketMsg->PerPacketInfoOffset + 8; /* Offset from message start */
            ULONG PpiEndOffset = PpiOffset + PacketMsg->PerPacketInfoLength;
            PRNDIS_PER_PACKET_INFO PpiEntry;

            DPRINT("USBRNDIS: RNDIS packet has per-packet info: offset=%u length=%u\n",
                   PacketMsg->PerPacketInfoOffset, PacketMsg->PerPacketInfoLength);

            /* Validate per-packet info bounds */
            if (PpiEndOffset <= PacketMsg->MessageLength)
            {
                /*
                 * Iterate through per-packet info elements.
                 * Each element has a Size field indicating total size including header.
                 */
                while (PpiOffset + sizeof(RNDIS_PER_PACKET_INFO) <= PpiEndOffset)
                {
                    PpiEntry = (PRNDIS_PER_PACKET_INFO)(Data + PpiOffset);

                    /* Validate element size */
                    if (PpiEntry->Size < sizeof(RNDIS_PER_PACKET_INFO) ||
                        PpiOffset + PpiEntry->Size > PpiEndOffset)
                    {
                        DPRINT1("USBRNDIS: Invalid per-packet info element size %u at offset %u\n",
                                PpiEntry->Size, PpiOffset);
                        break;
                    }

                    DPRINT("USBRNDIS: Per-packet info type=%u size=%u\n",
                           PpiEntry->Type, PpiEntry->Size);

                    /* Process based on type */
                    switch (PpiEntry->Type)
                    {
                        case RNDIS_PKTINFO_TYPE_TCPIP_CSUM:
                            /*
                             * TCP/IP checksum validation results from device.
                             * The data is at PerPacketInfoOffset within this element.
                             */
                            if (PpiEntry->PerPacketInfoOffset > 0 &&
                                PpiEntry->PerPacketInfoOffset + sizeof(RNDIS_TCPIP_CSUM_INFO) <= PpiEntry->Size)
                            {
                                PRNDIS_TCPIP_CSUM_INFO CsumData;
                                CsumData = (PRNDIS_TCPIP_CSUM_INFO)((PUCHAR)PpiEntry + PpiEntry->PerPacketInfoOffset);
                                ChecksumInfo.Value = CsumData->Value;
                                pChecksumInfo = &ChecksumInfo;

                                DPRINT("USBRNDIS: Checksum info from device: 0x%08X\n", ChecksumInfo.Value);
                            }
                            break;

                        case RNDIS_PKTINFO_TYPE_802_1Q_INFO:
                            /* VLAN tag info - not currently processed */
                            DPRINT("USBRNDIS: 802.1Q VLAN info present (not processed)\n");
                            break;

                        default:
                            DPRINT("USBRNDIS: Unknown per-packet info type %u\n", PpiEntry->Type);
                            break;
                    }

                    /* Move to next element */
                    PpiOffset += PpiEntry->Size;
                }
            }
            else
            {
                DPRINT1("USBRNDIS: Per-packet info extends past message (end=%u, msglen=%u)\n",
                        PpiEndOffset, PacketMsg->MessageLength);
            }
        }

        DPRINT("USBRNDIS: Received RNDIS Ethernet frame (%u bytes)\n", EthernetLength);

        /* Indicate Ethernet frame to NDIS using NBL with checksum info */
        RndisIndicateReceiveNblEx(Adapter, EthernetData, EthernetLength, pChecksumInfo);
        return;
    }

    /* Indicate Ethernet frame to NDIS using NBL (CDC-ECM path) */
    RndisIndicateReceiveNbl(Adapter, EthernetData, EthernetLength);
}

/*
 * RndisSendNetBufferLists
 *
 * NDIS 6.x miniport send handler - send NET_BUFFER_LISTs.
 * Since USB RNDIS can only have one TX pending at a time, we send the first
 * NET_BUFFER and complete remaining NBLs with NDIS_STATUS_RESOURCES.
 */
VOID
NTAPI
RndisSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CurrentNbl;
    PNET_BUFFER_LIST NextNbl;
    PNET_BUFFER_LIST FailedNbls = NULL;
    PNET_BUFFER CurrentNb;
    PMDL Mdl;
    PVOID VirtualAddress;
    ULONG DataLength;
    ULONG DataOffset;
    ULONG PacketLength;
    ULONG TotalLength;
    NTSTATUS Status;
    BOOLEAN DispatchLevel;
    BOOLEAN FirstPacketSent = FALSE;

    UNREFERENCED_PARAMETER(PortNumber);

    DPRINT("USBRNDIS: RndisSendNetBufferLists called\n");

    DispatchLevel = NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags);

    /* Check adapter state */
    if (Adapter->State != RndisStateDataInitialized || Adapter->Paused)
    {
        DPRINT1("USBRNDIS: Send called but adapter not ready (state=%d, paused=%d)\n",
                Adapter->State, Adapter->Paused);

        /* Complete all NBLs with failure */
        for (CurrentNbl = NetBufferList; CurrentNbl != NULL; CurrentNbl = NextNbl)
        {
            NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;
            NET_BUFFER_LIST_STATUS(CurrentNbl) = Adapter->Paused ?
                                                 NDIS_STATUS_PAUSED : NDIS_STATUS_FAILURE;
        }

        NdisMSendNetBufferListsComplete(
            Adapter->MiniportAdapterHandle,
            NetBufferList,
            DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
        return;
    }

    /* Check if halting */
    if (Adapter->Halting)
    {
        for (CurrentNbl = NetBufferList; CurrentNbl != NULL; CurrentNbl = NextNbl)
        {
            NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_FAILURE;
        }

        NdisMSendNetBufferListsComplete(
            Adapter->MiniportAdapterHandle,
            NetBufferList,
            DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
        return;
    }

    /* Process each NBL */
    for (CurrentNbl = NetBufferList; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
        NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;

        /* USB RNDIS only supports one TX at a time */
        if (FirstPacketSent)
        {
            /* Queue this NBL for failure completion */
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_RESOURCES;
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = FailedNbls;
            FailedNbls = CurrentNbl;
            continue;
        }

        /* Acquire TX lock */
        NdisAcquireSpinLock(&Adapter->TxLock);

        if (Adapter->TxBusy)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_RESOURCES;
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = FailedNbls;
            FailedNbls = CurrentNbl;
            continue;
        }

        Adapter->TxBusy = TRUE;
        Adapter->PendingTxNbl = CurrentNbl;
        Adapter->PendingTxNblCount = 1;  /* Single NBL, may be updated for NCM batching */
        NdisReleaseSpinLock(&Adapter->TxLock);

        /* Get the first NET_BUFFER from this NBL */
        CurrentNb = NET_BUFFER_LIST_FIRST_NB(CurrentNbl);
        if (CurrentNb == NULL)
        {
            DPRINT1("USBRNDIS: NBL has no NET_BUFFER\n");
            NdisAcquireSpinLock(&Adapter->TxLock);
            Adapter->TxBusy = FALSE;
            Adapter->PendingTxNbl = NULL;
            NdisReleaseSpinLock(&Adapter->TxLock);
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_FAILURE;
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = FailedNbls;
            FailedNbls = CurrentNbl;
            continue;
        }

        /* Get data length and MDL */
        DataLength = NET_BUFFER_DATA_LENGTH(CurrentNb);
        Mdl = NET_BUFFER_CURRENT_MDL(CurrentNb);
        DataOffset = NET_BUFFER_CURRENT_MDL_OFFSET(CurrentNb);

        if (DataLength > ETHERNET_MAX_FRAME_SIZE)
        {
            DPRINT1("USBRNDIS: Packet too large (%u bytes)\n", DataLength);
            NdisAcquireSpinLock(&Adapter->TxLock);
            Adapter->TxBusy = FALSE;
            Adapter->PendingTxNbl = NULL;
            NdisReleaseSpinLock(&Adapter->TxLock);
            Adapter->TxErrorCount++;
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_INVALID_LENGTH;
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = FailedNbls;
            FailedNbls = CurrentNbl;
            continue;
        }

        /* Map the MDL to get virtual address */
        VirtualAddress = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (VirtualAddress == NULL)
        {
            DPRINT1("USBRNDIS: Failed to map MDL\n");
            NdisAcquireSpinLock(&Adapter->TxLock);
            Adapter->TxBusy = FALSE;
            Adapter->PendingTxNbl = NULL;
            NdisReleaseSpinLock(&Adapter->TxLock);
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_RESOURCES;
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = FailedNbls;
            FailedNbls = CurrentNbl;
            continue;
        }

        VirtualAddress = (PUCHAR)VirtualAddress + DataOffset;

        /*
         * Copy packet data to TX buffer and build protocol-specific header.
         */
        if (Adapter->IsCdcEcm)
        {
            /* CDC-ECM: Copy Ethernet frame directly */
            NdisMoveMemory(Adapter->TxBuffer, VirtualAddress, DataLength);
            TotalLength = DataLength;
            DPRINT("USBRNDIS: CDC-ECM TX frame (%u bytes)\n", TotalLength);
        }
        else if (Adapter->IsCdcNcm)
        {
            /*
             * CDC-NCM: Build NTB with NTH16/NDP16 headers.
             *
             * NCM Multi-Datagram TX Batching:
             * Try to collect multiple pending NBLs and batch them into a single NTB.
             * This improves throughput by reducing USB transfer overhead.
             *
             * We collect datagrams from the current NBL and any additional NBLs
             * in the chain until we hit limits (max datagrams, max NTB size, or no more NBLs).
             */
            NCM_TX_DATAGRAM TxDatagrams[NCM_MAX_TX_DATAGRAMS];
            PNET_BUFFER_LIST BatchNbls[NCM_MAX_TX_DATAGRAMS];
            ULONG DatagramCount = 0;
            ULONG TotalDataLength = 0;
            ULONG EstimatedNtbSize;
            ULONG NdpOverhead;
            PNET_BUFFER_LIST BatchNbl;
            PNET_BUFFER BatchNb;
            PMDL BatchMdl;
            PVOID BatchVa;
            ULONG BatchDataLen;
            ULONG BatchDataOff;
            PUCHAR TempDataArea;
            ULONG TempDataOffset;
            ULONG i;

            /*
             * Reserve space at beginning of TxBuffer for NTH16 + NDP16 headers.
             * We'll copy datagram data after this reserved area, then build the NTB.
             * NDP overhead: 8-byte header + 4 bytes per datagram entry + 4-byte terminator
             * Plus some alignment padding.
             */
            NdpOverhead = NCM_NTH16_LENGTH + 8 + (NCM_MAX_TX_DATAGRAMS + 1) * 4 + 64;
            TempDataArea = Adapter->TxBuffer + NdpOverhead;
            TempDataOffset = 0;

            /*
             * First datagram: already have the current NBL data
             */
            if (TempDataOffset + DataLength <= RNDIS_MAX_TRANSFER_SIZE - NdpOverhead)
            {
                TxDatagrams[DatagramCount].Data = TempDataArea + TempDataOffset;
                TxDatagrams[DatagramCount].Length = DataLength;
                NdisMoveMemory(TempDataArea + TempDataOffset, VirtualAddress, DataLength);
                TempDataOffset += DataLength;
                BatchNbls[DatagramCount] = CurrentNbl;
                DatagramCount++;
                TotalDataLength += DataLength;
            }

            /*
             * Try to batch additional NBLs from the chain.
             * We look ahead at NextNbl and subsequent NBLs.
             */
            BatchNbl = NextNbl;
            while (BatchNbl != NULL && DatagramCount < NCM_MAX_TX_DATAGRAMS)
            {
                /* Get the first NET_BUFFER from this NBL */
                BatchNb = NET_BUFFER_LIST_FIRST_NB(BatchNbl);
                if (BatchNb == NULL)
                {
                    break;
                }

                BatchDataLen = NET_BUFFER_DATA_LENGTH(BatchNb);
                BatchMdl = NET_BUFFER_CURRENT_MDL(BatchNb);
                BatchDataOff = NET_BUFFER_CURRENT_MDL_OFFSET(BatchNb);

                /* Validate length */
                if (BatchDataLen > ETHERNET_MAX_FRAME_SIZE)
                {
                    break;
                }

                /* Check if adding this datagram would exceed NTB size limit */
                EstimatedNtbSize = NdpOverhead + TempDataOffset + BatchDataLen + 64; /* +64 for alignment */
                if (EstimatedNtbSize > Adapter->NcmNtbOutMaxSize ||
                    EstimatedNtbSize > RNDIS_MAX_TRANSFER_SIZE)
                {
                    /* Would exceed limits, stop batching */
                    break;
                }

                /* Map the MDL */
                BatchVa = MmGetSystemAddressForMdlSafe(BatchMdl, NormalPagePriority);
                if (BatchVa == NULL)
                {
                    break;
                }
                BatchVa = (PUCHAR)BatchVa + BatchDataOff;

                /* Add this datagram to the batch */
                TxDatagrams[DatagramCount].Data = TempDataArea + TempDataOffset;
                TxDatagrams[DatagramCount].Length = BatchDataLen;
                NdisMoveMemory(TempDataArea + TempDataOffset, BatchVa, BatchDataLen);
                TempDataOffset += BatchDataLen;
                BatchNbls[DatagramCount] = BatchNbl;
                DatagramCount++;
                TotalDataLength += BatchDataLen;

                /* Remove this NBL from the chain - we're now handling it */
                NextNbl = NET_BUFFER_LIST_NEXT_NBL(BatchNbl);
                NET_BUFFER_LIST_NEXT_NBL(BatchNbl) = NULL;

                /* Move to next NBL */
                BatchNbl = NextNbl;
            }

            /* Now build the multi-datagram NTB */
            TotalLength = RndisBuildNcmNtbMulti(Adapter, TxDatagrams, DatagramCount, Adapter->TxBuffer);

            if (TotalLength == 0)
            {
                DPRINT1("USBRNDIS: Failed to build NCM NTB\n");
                NdisAcquireSpinLock(&Adapter->TxLock);
                Adapter->TxBusy = FALSE;
                Adapter->PendingTxNbl = NULL;
                NdisReleaseSpinLock(&Adapter->TxLock);
                Adapter->TxErrorCount++;

                /* Fail all NBLs in the batch */
                for (i = 0; i < DatagramCount; i++)
                {
                    NET_BUFFER_LIST_STATUS(BatchNbls[i]) = NDIS_STATUS_FAILURE;
                    NET_BUFFER_LIST_NEXT_NBL(BatchNbls[i]) = FailedNbls;
                    FailedNbls = BatchNbls[i];
                }
                continue;
            }

            /*
             * Store batch info for completion.
             * We need to complete all NBLs when the USB transfer completes.
             * For now, we store the list in PendingTxNbl chain.
             */
            NdisAcquireSpinLock(&Adapter->TxLock);
            Adapter->PendingTxNbl = BatchNbls[0];
            for (i = 1; i < DatagramCount; i++)
            {
                /* Chain the batched NBLs together for completion */
                NET_BUFFER_LIST_NEXT_NBL(BatchNbls[i-1]) = BatchNbls[i];
            }
            Adapter->PendingTxNblCount = DatagramCount;
            NdisReleaseSpinLock(&Adapter->TxLock);

            /* Update stats for all batched datagrams */
            Adapter->TxBytes += TotalDataLength;

            DPRINT1("USBRNDIS: CDC-NCM TX NTB with %lu datagrams (%lu bytes total, NTB %lu bytes)\n",
                   DatagramCount, TotalDataLength, TotalLength);
        }
        else
        {
            /* RNDIS: Build RNDIS_PACKET_MSG wrapper */
            NdisMoveMemory(Adapter->TxBuffer + sizeof(RNDIS_PACKET_MSG),
                           VirtualAddress, DataLength);

            TotalLength = RndisBuildPacketMessage(Adapter,
                                                  Adapter->TxBuffer + sizeof(RNDIS_PACKET_MSG),
                                                  DataLength,
                                                  Adapter->TxBuffer);

            DPRINT("USBRNDIS: RNDIS TX packet (%lu bytes, frame %lu bytes)\n",
                   TotalLength, DataLength);
        }

        /* Send via USB bulk endpoint - async operation */
        Status = RndisUsbSubmitBulkWrite(Adapter, Adapter->TxBuffer, TotalLength);

        if (Status == STATUS_PENDING)
        {
            /*
             * URB submitted successfully, will complete asynchronously.
             * NdisMSendNetBufferListsComplete will be called from completion routine.
             */
            DPRINT("USBRNDIS: TX submitted async (%lu bytes)\n", DataLength);
            Adapter->TxBytes += DataLength;
            FirstPacketSent = TRUE;
        }
        else if (NT_SUCCESS(Status))
        {
            /*
             * URB completed synchronously (STATUS_SUCCESS).
             * The completion routine has ALREADY run.
             * Mark as success and complete immediately.
             */
            DPRINT("USBRNDIS: TX completed sync (%lu bytes)\n", DataLength);
            Adapter->TxBytes += DataLength;
            FirstPacketSent = TRUE;
            /* Completion callback already handled TxBusy and stats */
        }
        else
        {
            /*
             * Failed to submit URB. Clean up and fail this NBL.
             */
            DPRINT1("USBRNDIS: Failed to submit TX (0x%08X)\n", Status);
            NdisAcquireSpinLock(&Adapter->TxLock);
            Adapter->TxBusy = FALSE;
            Adapter->PendingTxNbl = NULL;
            NdisReleaseSpinLock(&Adapter->TxLock);
            Adapter->TxErrorCount++;
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_FAILURE;
            NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = FailedNbls;
            FailedNbls = CurrentNbl;
        }
    }

    /* Complete failed NBLs */
    if (FailedNbls != NULL)
    {
        NdisMSendNetBufferListsComplete(
            Adapter->MiniportAdapterHandle,
            FailedNbls,
            DispatchLevel ? NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0);
    }
}

/*
 * RndisReturnNetBufferLists
 *
 * NDIS 6.x handler for returning NET_BUFFER_LISTs after receive indication.
 * Called by NDIS when protocol drivers are done with indicated NBLs.
 */
VOID
NTAPI
RndisReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CurrentNbl;
    PNET_BUFFER_LIST NextNbl;
    PNET_BUFFER Nb;
    PMDL Mdl;
    PUCHAR DataBuffer;

    UNREFERENCED_PARAMETER(ReturnFlags);

    DPRINT("USBRNDIS: RndisReturnNetBufferLists called\n");

    /* Process returned NBLs */
    for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);

        /* Retrieve the data buffer pointer we stored during indication */
        DataBuffer = (PUCHAR)NET_BUFFER_LIST_INFO(CurrentNbl, MediaSpecificInformation);

        /* Get the NET_BUFFER and its MDL */
        Nb = NET_BUFFER_LIST_FIRST_NB(CurrentNbl);
        if (Nb != NULL)
        {
            Mdl = NET_BUFFER_FIRST_MDL(Nb);
            if (Mdl != NULL)
            {
                /* Free the MDL */
                NdisFreeMdl(Mdl);
            }
        }

        /* Free the NBL */
        NdisFreeNetBufferList(CurrentNbl);

        /* Free the data buffer */
        if (DataBuffer != NULL)
        {
            NdisFreeMemory(DataBuffer, 0, 0);
        }
    }
}

/*
 * RndisCancelSend
 *
 * NDIS 6.x handler for cancelling pending sends with a matching cancel ID.
 * USB RNDIS has limited cancellation capability since URBs in progress
 * cannot be easily cancelled.
 */
VOID
NTAPI
RndisCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CancelledNbl = NULL;

    DPRINT("USBRNDIS: RndisCancelSend called (CancelId=%p)\n", CancelId);

    /*
     * For USB RNDIS, once the URB is submitted, we cannot cancel it easily.
     * We can only cancel NBLs that are queued but not yet submitted.
     * Since we only have one pending TX at a time and it's immediately
     * submitted to USB, there's nothing to cancel.
     *
     * Check if the pending NBL matches the cancel ID.
     */
    NdisAcquireSpinLock(&Adapter->TxLock);

    if (Adapter->PendingTxNbl != NULL &&
        NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Adapter->PendingTxNbl) == CancelId)
    {
        /*
         * The pending NBL matches, but it's already submitted to USB.
         * We cannot cancel the USB transfer, so we don't do anything here.
         * The completion routine will handle it normally.
         */
        DPRINT("USBRNDIS: Cannot cancel in-flight TX\n");
    }

    NdisReleaseSpinLock(&Adapter->TxLock);

    /* No queued NBLs to cancel in this simple implementation */
    UNREFERENCED_PARAMETER(CancelledNbl);
}
