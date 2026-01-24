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
 * RndisProcessReceivedPacket
 *
 * Process received RNDIS packet data and deliver to NDIS
 * Uses NdisMEthIndicateReceive for simplicity
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

    DPRINT("USBRNDIS: Received Ethernet frame (%u bytes)\n", EthernetLength);

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

    /* Indicate receive complete */
    NdisMEthIndicateReceiveComplete(Adapter->MiniportAdapterHandle);
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

    /* Copy packet data to TX buffer (after RNDIS header space) */
    PacketLength = 0;
    NdisQueryPacket(Packet, NULL, NULL, &Buffer, NULL);

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

    /* Send via USB bulk endpoint - async operation */
    Status = RndisUsbSubmitBulkWrite(Adapter, Adapter->TxBuffer, TotalLength);

    if (Status == STATUS_PENDING)
    {
        /*
         * URB submitted successfully.
         * NdisMSendComplete will be called from RndisTxComplete completion routine.
         */
        DPRINT("USBRNDIS: TX submitted async (%lu bytes)\n", PacketLength);
        return NDIS_STATUS_PENDING;
    }
    else
    {
        /*
         * Failed to submit URB - clean up and return failure.
         * No completion callback will occur.
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
         * For PENDING status, the packet status is set and completion
         * will be handled by NdisMSendComplete in the TX completion routine.
         * For other statuses, we set the status and call completion immediately.
         */
        NDIS_SET_PACKET_STATUS(PacketArray[i], Status);

        if (Status != NDIS_STATUS_PENDING && Status != NDIS_STATUS_SUCCESS)
        {
            /* Immediately complete failed packets */
            NdisMSendComplete(Adapter->MiniportAdapterHandle, PacketArray[i], Status);
        }
        else if (Status == NDIS_STATUS_SUCCESS)
        {
            /* Shouldn't happen with async model, but handle it */
            NdisMSendComplete(Adapter->MiniportAdapterHandle, PacketArray[i], Status);
        }
        /* NDIS_STATUS_PENDING - completion will be called from RndisTxComplete */
    }
}
