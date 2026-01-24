/*
 * PROJECT:     ReactOS USB RNDIS Network Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     RNDIS control protocol message handling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This file implements the RNDIS control protocol including:
 * - Device initialization (INIT message)
 * - Device halt (HALT message)
 * - OID query/set operations (QUERY/SET messages)
 */

#include "usbrndis.h"

#define NDEBUG
#include <debug.h>

/* External helper functions */
extern PVOID RndisAllocateMemory(IN POOL_TYPE PoolType, IN SIZE_T Size);
extern VOID RndisFreeMemory(IN PVOID Buffer);

/*
 * RndisCommand
 *
 * Send an RNDIS command and wait for the response.
 * This function handles the common pattern of:
 * 1. Send command via SEND_ENCAPSULATED_COMMAND
 * 2. Poll for response via GET_ENCAPSULATED_RESPONSE
 * 3. Handle keepalive messages that may arrive during polling
 *
 * Note: This function acquires ControlLock to serialize access to
 * the shared ControlBuffer. The lock is held for the entire
 * request/response cycle to prevent concurrent control operations.
 */
static
NTSTATUS
RndisCommand(
    IN PRNDIS_ADAPTER Adapter,
    IN PRNDIS_MSG_HEADER Request,
    IN ULONG ExpectedResponseType,
    OUT PULONG BytesReceived)
{
    NTSTATUS Status;
    PRNDIS_MSG_HEADER Response;
    ULONG RequestId;
    ULONG RetryCount;
    ULONG Received;
    LARGE_INTEGER StartTime;
    LARGE_INTEGER CurrentTime;
    LARGE_INTEGER ElapsedMs;
    LARGE_INTEGER Frequency;

    RequestId = Request->RequestId;

    /* Serialize control channel access */
    NdisAcquireSpinLock(&Adapter->ControlLock);

    /* Get start time for timeout tracking */
    KeQueryPerformanceCounter(&Frequency);
    StartTime = KeQueryPerformanceCounter(NULL);

    /* Send the command */
    Status = RndisUsbSendControlMessage(Adapter, Request, Request->MessageLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: Failed to send command type 0x%08X (0x%08X)\n",
                Request->MessageType, Status);
        NdisReleaseSpinLock(&Adapter->ControlLock);
        return Status;
    }

    /* Poll for response with timeout */
    for (RetryCount = 0; RetryCount < 100; RetryCount++)
    {
        /* Check timeout */
        CurrentTime = KeQueryPerformanceCounter(NULL);
        ElapsedMs.QuadPart = ((CurrentTime.QuadPart - StartTime.QuadPart) * 1000) / Frequency.QuadPart;
        if (ElapsedMs.QuadPart >= RNDIS_CONTROL_TIMEOUT_MS)
        {
            DPRINT1("USBRNDIS: Control command timeout after %u ms\n", (ULONG)ElapsedMs.QuadPart);
            NdisReleaseSpinLock(&Adapter->ControlLock);
            return STATUS_IO_TIMEOUT;
        }

        /* Clear buffer before receiving */
        NdisZeroMemory(Adapter->ControlBuffer, RNDIS_CONTROL_BUFFER_SIZE);

        Status = RndisUsbReceiveControlResponse(
            Adapter,
            Adapter->ControlBuffer,
            RNDIS_CONTROL_BUFFER_SIZE,
            &Received);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("USBRNDIS: Failed to receive response (0x%08X)\n", Status);
            /*
             * Some devices need a delay before responding.
             * Use NdisMSleep which is safe at PASSIVE_LEVEL.
             * Control operations should only be called at PASSIVE_LEVEL
             * (during init/halt), so this is safe.
             */
            NdisMSleep(40000); /* 40ms in microseconds */
            continue;
        }

        if (Received < sizeof(RNDIS_MSG_HEADER))
        {
            /* Response too short, retry */
            continue;
        }

        Response = (PRNDIS_MSG_HEADER)Adapter->ControlBuffer;

        DPRINT("USBRNDIS: Response type=0x%08X len=%u reqid=%u status=0x%08X\n",
               Response->MessageType, Response->MessageLength,
               Response->RequestId, Response->Status);

        /* Check if this is our expected response */
        if (Response->MessageType == ExpectedResponseType)
        {
            if (Response->RequestId == RequestId ||
                ExpectedResponseType == RNDIS_MSG_RESET_C)
            {
                *BytesReceived = Received;
                NdisReleaseSpinLock(&Adapter->ControlLock);
                return STATUS_SUCCESS;
            }
            /* Request ID mismatch, might be old response */
            DPRINT("USBRNDIS: Request ID mismatch (got %u, expected %u)\n",
                   Response->RequestId, RequestId);
        }
        /* Handle keepalive message from device */
        else if (Response->MessageType == RNDIS_MSG_KEEPALIVE)
        {
            RNDIS_KEEPALIVE_CMPLT KeepaliveResponse;

            DPRINT("USBRNDIS: Received keepalive, sending response\n");

            /* Send keepalive completion */
            NdisZeroMemory(&KeepaliveResponse, sizeof(KeepaliveResponse));
            KeepaliveResponse.MessageType = RNDIS_MSG_KEEPALIVE_C;
            KeepaliveResponse.MessageLength = sizeof(RNDIS_KEEPALIVE_CMPLT);
            KeepaliveResponse.RequestId = Response->RequestId;
            KeepaliveResponse.Status = RNDIS_STATUS_SUCCESS;

            RndisUsbSendControlMessage(Adapter, &KeepaliveResponse, sizeof(KeepaliveResponse));
            /* Continue polling for our response */
        }
        /* Handle indication message (status change) */
        else if (Response->MessageType == RNDIS_MSG_INDICATE)
        {
            PRNDIS_INDICATE_MSG Indicate = (PRNDIS_INDICATE_MSG)Adapter->ControlBuffer;

            DPRINT("USBRNDIS: Received indication, status=0x%08X\n", Indicate->Status);

            if (Indicate->Status == RNDIS_STATUS_MEDIA_CONNECT)
            {
                Adapter->MediaState = NdisMediaStateConnected;
            }
            else if (Indicate->Status == RNDIS_STATUS_MEDIA_DISCONNECT)
            {
                Adapter->MediaState = NdisMediaStateDisconnected;
            }
            /* Continue polling for our response */
        }
        else
        {
            DPRINT("USBRNDIS: Unexpected response type 0x%08X\n", Response->MessageType);
        }
    }

    DPRINT1("USBRNDIS: Response timeout for command 0x%08X\n", Request->MessageType);
    NdisReleaseSpinLock(&Adapter->ControlLock);
    return STATUS_IO_TIMEOUT;
}

/*
 * RndisInitializeDevice
 *
 * Send RNDIS_MSG_INIT to initialize the device
 */
NTSTATUS
RndisInitializeDevice(
    IN PRNDIS_ADAPTER Adapter)
{
    RNDIS_INIT_MSG InitMsg;
    PRNDIS_INIT_CMPLT InitCmplt;
    NTSTATUS Status;
    ULONG BytesReceived;

    DPRINT("USBRNDIS: Initializing RNDIS device\n");

    /* Build init message */
    NdisZeroMemory(&InitMsg, sizeof(InitMsg));
    InitMsg.MessageType = RNDIS_MSG_INIT;
    InitMsg.MessageLength = sizeof(RNDIS_INIT_MSG);
    InitMsg.RequestId = RNDIS_GET_REQUEST_ID(Adapter);
    InitMsg.MajorVersion = RNDIS_MAJOR_VERSION;
    InitMsg.MinorVersion = RNDIS_MINOR_VERSION;
    InitMsg.MaxTransferSize = RNDIS_MAX_TRANSFER_SIZE;

    /* Send init and get response */
    Status = RndisCommand(Adapter, (PRNDIS_MSG_HEADER)&InitMsg, RNDIS_MSG_INIT_C, &BytesReceived);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: INIT command failed (0x%08X)\n", Status);
        return Status;
    }

    if (BytesReceived < sizeof(RNDIS_INIT_CMPLT))
    {
        DPRINT1("USBRNDIS: INIT response too short (%u bytes)\n", BytesReceived);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    InitCmplt = (PRNDIS_INIT_CMPLT)Adapter->ControlBuffer;

    if (InitCmplt->Status != RNDIS_STATUS_SUCCESS)
    {
        DPRINT1("USBRNDIS: INIT failed with status 0x%08X\n", InitCmplt->Status);
        return STATUS_UNSUCCESSFUL;
    }

    /* Save device parameters */
    Adapter->MaxTransferSize = InitCmplt->MaxTransferSize;
    Adapter->PacketAlignmentFactor = InitCmplt->PacketAlignmentFactor;
    Adapter->MaxPacketsPerMessage = InitCmplt->MaxPacketsPerMessage;

    DPRINT("USBRNDIS: INIT success - MaxTransfer=%u Alignment=%u MaxPkts=%u\n",
           Adapter->MaxTransferSize,
           Adapter->PacketAlignmentFactor,
           Adapter->MaxPacketsPerMessage);

    return STATUS_SUCCESS;
}

/*
 * RndisHaltDevice
 *
 * Send RNDIS_MSG_HALT to stop the device (no response expected)
 */
NTSTATUS
RndisHaltDevice(
    IN PRNDIS_ADAPTER Adapter)
{
    RNDIS_HALT_MSG HaltMsg;
    NTSTATUS Status;

    DPRINT("USBRNDIS: Halting RNDIS device\n");

    /* Build halt message */
    NdisZeroMemory(&HaltMsg, sizeof(HaltMsg));
    HaltMsg.MessageType = RNDIS_MSG_HALT;
    HaltMsg.MessageLength = sizeof(RNDIS_HALT_MSG);
    HaltMsg.RequestId = RNDIS_GET_REQUEST_ID(Adapter);

    /* Send halt - no response expected */
    Status = RndisUsbSendControlMessage(Adapter, &HaltMsg, sizeof(HaltMsg));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: HALT command failed (0x%08X)\n", Status);
    }

    return Status;
}

/*
 * RndisQueryOid
 *
 * Query an OID value from the device
 */
NTSTATUS
RndisQueryOid(
    IN PRNDIS_ADAPTER Adapter,
    IN ULONG Oid,
    OUT PVOID Buffer,
    IN ULONG BufferLength,
    OUT PULONG BytesWritten)
{
    RNDIS_QUERY_MSG QueryMsg;
    PRNDIS_QUERY_CMPLT QueryCmplt;
    NTSTATUS Status;
    ULONG BytesReceived;
    ULONG DataOffset;
    ULONG DataLength;
    PUCHAR DataPtr;

    DPRINT("USBRNDIS: Querying OID 0x%08X\n", Oid);

    *BytesWritten = 0;

    /* Build query message */
    NdisZeroMemory(&QueryMsg, sizeof(QueryMsg));
    QueryMsg.MessageType = RNDIS_MSG_QUERY;
    QueryMsg.MessageLength = sizeof(RNDIS_QUERY_MSG);
    QueryMsg.RequestId = RNDIS_GET_REQUEST_ID(Adapter);
    QueryMsg.Oid = Oid;
    QueryMsg.InformationBufferLength = 0;
    QueryMsg.InformationBufferOffset = 20; /* Offset from RequestId */
    QueryMsg.DeviceVcHandle = 0;

    /* Send query and get response */
    Status = RndisCommand(Adapter, (PRNDIS_MSG_HEADER)&QueryMsg, RNDIS_MSG_QUERY_C, &BytesReceived);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: QUERY command failed (0x%08X)\n", Status);
        return Status;
    }

    if (BytesReceived < sizeof(RNDIS_QUERY_CMPLT))
    {
        DPRINT1("USBRNDIS: QUERY response too short (%u bytes)\n", BytesReceived);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    QueryCmplt = (PRNDIS_QUERY_CMPLT)Adapter->ControlBuffer;

    if (QueryCmplt->Status != RNDIS_STATUS_SUCCESS)
    {
        DPRINT1("USBRNDIS: QUERY OID 0x%08X failed with status 0x%08X\n",
                Oid, QueryCmplt->Status);
        return STATUS_UNSUCCESSFUL;
    }

    /* Extract data from response */
    DataOffset = QueryCmplt->InformationBufferOffset;
    DataLength = QueryCmplt->InformationBufferLength;

    /* Validate response data */
    if (DataOffset + DataLength + 8 > BytesReceived ||
        DataOffset + 8 > RNDIS_CONTROL_BUFFER_SIZE)
    {
        DPRINT1("USBRNDIS: Invalid QUERY response (offset=%u len=%u received=%u)\n",
                DataOffset, DataLength, BytesReceived);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    /* Copy data to caller's buffer */
    DataPtr = (PUCHAR)&QueryCmplt->RequestId + DataOffset;
    if (DataLength > BufferLength)
    {
        DataLength = BufferLength;
    }

    NdisMoveMemory(Buffer, DataPtr, DataLength);
    *BytesWritten = DataLength;

    DPRINT("USBRNDIS: QUERY OID 0x%08X returned %u bytes\n", Oid, DataLength);
    return STATUS_SUCCESS;
}

/*
 * RndisSetOid
 *
 * Set an OID value on the device
 */
NTSTATUS
RndisSetOid(
    IN PRNDIS_ADAPTER Adapter,
    IN ULONG Oid,
    IN PVOID Buffer,
    IN ULONG BufferLength)
{
    PUCHAR SetMsgBuffer;
    PRNDIS_SET_MSG SetMsg;
    PRNDIS_SET_CMPLT SetCmplt;
    NTSTATUS Status;
    ULONG BytesReceived;
    ULONG TotalLength;

    DPRINT("USBRNDIS: Setting OID 0x%08X (%u bytes)\n", Oid, BufferLength);

    /* Calculate total message length */
    TotalLength = sizeof(RNDIS_SET_MSG) + BufferLength;
    if (TotalLength > RNDIS_CONTROL_BUFFER_SIZE)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    /* Allocate buffer for set message */
    SetMsgBuffer = RndisAllocateMemory(NonPagedPool, TotalLength);
    if (!SetMsgBuffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Build set message */
    SetMsg = (PRNDIS_SET_MSG)SetMsgBuffer;
    NdisZeroMemory(SetMsg, sizeof(RNDIS_SET_MSG));
    SetMsg->MessageType = RNDIS_MSG_SET;
    SetMsg->MessageLength = TotalLength;
    SetMsg->RequestId = RNDIS_GET_REQUEST_ID(Adapter);
    SetMsg->Oid = Oid;
    SetMsg->InformationBufferLength = BufferLength;
    SetMsg->InformationBufferOffset = sizeof(RNDIS_SET_MSG) - 8; /* Offset from RequestId */
    SetMsg->DeviceVcHandle = 0;

    /* Copy data after header */
    if (BufferLength > 0 && Buffer)
    {
        NdisMoveMemory(SetMsgBuffer + sizeof(RNDIS_SET_MSG), Buffer, BufferLength);
    }

    /* Send set and get response */
    Status = RndisCommand(Adapter, (PRNDIS_MSG_HEADER)SetMsg, RNDIS_MSG_SET_C, &BytesReceived);

    RndisFreeMemory(SetMsgBuffer);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: SET command failed (0x%08X)\n", Status);
        return Status;
    }

    if (BytesReceived < sizeof(RNDIS_SET_CMPLT))
    {
        DPRINT1("USBRNDIS: SET response too short (%u bytes)\n", BytesReceived);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    SetCmplt = (PRNDIS_SET_CMPLT)Adapter->ControlBuffer;

    if (SetCmplt->Status != RNDIS_STATUS_SUCCESS)
    {
        DPRINT1("USBRNDIS: SET OID 0x%08X failed with status 0x%08X\n",
                Oid, SetCmplt->Status);
        return STATUS_UNSUCCESSFUL;
    }

    DPRINT("USBRNDIS: SET OID 0x%08X success\n", Oid);
    return STATUS_SUCCESS;
}

/*
 * RndisSetPacketFilter
 *
 * Set the packet filter to enable receiving specific packet types
 */
NTSTATUS
RndisSetPacketFilter(
    IN PRNDIS_ADAPTER Adapter,
    IN ULONG PacketFilter)
{
    NTSTATUS Status;

    DPRINT("USBRNDIS: Setting packet filter to 0x%08X\n", PacketFilter);

    Status = RndisSetOid(Adapter, RNDIS_OID_GEN_CURRENT_PACKET_FILTER,
                         &PacketFilter, sizeof(PacketFilter));

    if (NT_SUCCESS(Status))
    {
        Adapter->PacketFilter = PacketFilter;
    }

    return Status;
}

/*
 * RndisGetMacAddress
 *
 * Query the permanent MAC address from the device
 */
NTSTATUS
RndisGetMacAddress(
    IN PRNDIS_ADAPTER Adapter)
{
    NTSTATUS Status;
    ULONG BytesWritten;
    UCHAR MacAddress[ETHERNET_ADDRESS_LENGTH];

    DPRINT("USBRNDIS: Getting MAC address\n");

    /* Query permanent address */
    Status = RndisQueryOid(
        Adapter,
        RNDIS_OID_802_3_PERMANENT_ADDRESS,
        MacAddress,
        ETHERNET_ADDRESS_LENGTH,
        &BytesWritten);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: Failed to get permanent MAC address (0x%08X)\n", Status);

        /* Try current address as fallback */
        Status = RndisQueryOid(
            Adapter,
            RNDIS_OID_802_3_CURRENT_ADDRESS,
            MacAddress,
            ETHERNET_ADDRESS_LENGTH,
            &BytesWritten);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("USBRNDIS: Failed to get current MAC address (0x%08X)\n", Status);
            return Status;
        }
    }

    if (BytesWritten != ETHERNET_ADDRESS_LENGTH)
    {
        DPRINT1("USBRNDIS: Invalid MAC address length (%u)\n", BytesWritten);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    /* Copy MAC address */
    NdisMoveMemory(Adapter->PermanentMacAddress, MacAddress, ETHERNET_ADDRESS_LENGTH);
    NdisMoveMemory(Adapter->CurrentMacAddress, MacAddress, ETHERNET_ADDRESS_LENGTH);

    return STATUS_SUCCESS;
}
