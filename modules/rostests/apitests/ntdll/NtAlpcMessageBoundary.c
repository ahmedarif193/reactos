/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         ALPC message length, receive retry, and alignment tests
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"

static
NTSTATUS
AlpcBoundaryCreateConnectedPorts(
    _In_ PUNICODE_STRING PortName,
    _In_ SIZE_T MaxMessageLength,
    _Out_ PHANDLE ConnectionPort,
    _Out_ PHANDLE ServerPort,
    _Out_ PHANDLE ClientPort)
{
    PALPC_TEST_CONNECT_CONTEXT Context;
    ALPC_PORT_ATTRIBUTES Attributes;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ALPC_TEST_MESSAGE Request;
    LARGE_INTEGER Timeout;
    SIZE_T MessageLength;
    NTSTATUS Status;
    HANDLE Thread;
    DWORD WaitStatus;

    *ConnectionPort = NULL;
    *ServerPort = NULL;
    *ClientPort = NULL;
    AlpcTestInitializePortAttributes(&Attributes, 0);
    Attributes.MaxMessageLength = MaxMessageLength;
    InitializeObjectAttributes(&ObjectAttributes, PortName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtAlpcCreatePort(ConnectionPort, &ObjectAttributes, &Attributes);
    if (!NT_SUCCESS(Status))
        return Status;

    Context = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (!Context)
    {
        NtClose(*ConnectionPort);
        *ConnectionPort = NULL;
        return STATUS_NO_MEMORY;
    }
    Context->PortName = *PortName;
    Context->PortAttributes = Attributes;
    Context->Status = STATUS_UNSUCCESSFUL;
    Thread = CreateThread(NULL, 0, AlpcTestConnectThread, Context, 0, NULL);
    if (!Thread)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
        NtClose(*ConnectionPort);
        *ConnectionPort = NULL;
        return STATUS_NO_MEMORY;
    }

    RtlZeroMemory(&Request, sizeof(Request));
    MessageLength = sizeof(Request);
    Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    Status = NtAlpcSendWaitReceivePort(*ConnectionPort, 0, NULL, NULL, &Request.Header, &MessageLength, NULL, &Timeout);
    if (NT_SUCCESS(Status))
        Status = NtAlpcAcceptConnectPort(ServerPort, *ConnectionPort, 0, NULL, &Attributes, ALPC_TEST_PORT_CONTEXT, &Request.Header, NULL, TRUE);

    WaitStatus = AlpcTestJoinThread(Thread, ConnectionPort, ServerPort, "boundary connect");
    CloseHandle(Thread);
    if (NT_SUCCESS(Status) && WaitStatus == WAIT_OBJECT_0)
        Status = Context->Status;
    else if (NT_SUCCESS(Status))
        Status = STATUS_TIMEOUT;

    if (NT_SUCCESS(Status))
        *ClientPort = Context->ClientPort;
    else
    {
        if (WaitStatus == WAIT_OBJECT_0 && Context->ClientPort)
            NtClose(Context->ClientPort);
        if (*ServerPort)
            NtClose(*ServerPort);
        if (*ConnectionPort)
            NtClose(*ConnectionPort);
        *ServerPort = NULL;
        *ConnectionPort = NULL;
    }
    if (WaitStatus == WAIT_OBJECT_0)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
    else
        trace("ALPC_OBSERVE thread boundary connect context_quarantined=%p\n", Context);
    return Status;
}

static
PPORT_MESSAGE
AlpcBoundaryAllocateMessage(
    _In_ USHORT TotalLength,
    _In_ USHORT DataLength,
    _In_ USHORT Type)
{
    PPORT_MESSAGE Message;
    SIZE_T AllocationSize = TotalLength;

    if (AllocationSize < sizeof(PORT_MESSAGE))
        AllocationSize = sizeof(PORT_MESSAGE);
    Message = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, AllocationSize);
    if (!Message)
        return NULL;
    Message->u1.s1.TotalLength = TotalLength;
    Message->u1.s1.DataLength = DataLength;
    Message->u2.s2.Type = Type;
    return Message;
}

static
NTSTATUS
AlpcBoundarySendDatagram(
    _In_ HANDLE ClientPort,
    _In_ PPORT_MESSAGE Message)
{
    return NtAlpcSendWaitReceivePort(ClientPort, ALPC_MSGFLG_REPLY_MESSAGE, Message, NULL, NULL, NULL, NULL, NULL);
}

static
NTSTATUS
AlpcBoundaryReceive(
    _In_ HANDLE ConnectionPort,
    _Out_writes_bytes_(BufferCapacity) PPORT_MESSAGE Message,
    _In_ SIZE_T BufferCapacity,
    _Inout_ PSIZE_T BufferLength)
{
    LARGE_INTEGER Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);

    RtlFillMemory(Message, BufferCapacity, 0x55);
    return NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, Message, BufferLength, NULL, &Timeout);
}

static
BOOLEAN
AlpcBoundaryPayloadMatches(
    _In_reads_bytes_(Length) const UCHAR *Payload,
    _In_ SIZE_T Length,
    _In_ UCHAR Expected)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; ++Index)
    {
        if (Payload[Index] != Expected)
            return FALSE;
    }
    return TRUE;
}

static
BOOLEAN
AlpcTestSendLengthMatrix(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ClientPort)
{
    static const USHORT TotalLengths[] = {sizeof(PORT_MESSAGE), sizeof(PORT_MESSAGE) + 1, 0x1000, 0x7fff, 0x8000, 0xfffe, 0xffff};
    PPORT_MESSAGE SendMessage;
    PPORT_MESSAGE ReceiveMessage;
    SIZE_T ReceiveLength;
    NTSTATUS Status;
    ULONG Index;
    UCHAR PayloadByte;

    ReceiveMessage = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, 0x10000);
    ok(ReceiveMessage != NULL, "failed to allocate receive buffer\n");
    if (!ReceiveMessage)
        return FALSE;

    for (Index = 0; Index < RTL_NUMBER_OF(TotalLengths); ++Index)
    {
        SendMessage = AlpcBoundaryAllocateMessage(TotalLengths[Index], TotalLengths[Index] - sizeof(PORT_MESSAGE), LPC_DATAGRAM);
        ok(SendMessage != NULL, "failed to allocate send buffer %u\n", TotalLengths[Index]);
        if (!SendMessage)
            continue;
        PayloadByte = (UCHAR)(0x30 + Index);
        if (TotalLengths[Index] > sizeof(PORT_MESSAGE))
            RtlFillMemory(SendMessage + 1, TotalLengths[Index] - sizeof(PORT_MESSAGE), PayloadByte);
        Status = AlpcBoundarySendDatagram(ClientPort, SendMessage);
        trace("ALPC_OBSERVE status Message.send_length total=%u data=%u status=%08lx\n", TotalLengths[Index], SendMessage->u1.s1.DataLength, Status);
        ok(Status != STATUS_NOT_IMPLEMENTED, "send length %u reached a stub\n", TotalLengths[Index]);
        if (NT_SUCCESS(Status))
        {
            ReceiveLength = 0x10000;
            Status = AlpcBoundaryReceive(ConnectionPort, ReceiveMessage, 0x10000, &ReceiveLength);
            trace("ALPC_OBSERVE status Message.receive_length total=%u status=%08lx returned_length=%Iu received_total=%u received_data=%u type=%04x\n", TotalLengths[Index], Status, ReceiveLength, ReceiveMessage->u1.s1.TotalLength, ReceiveMessage->u1.s1.DataLength, ReceiveMessage->u2.s2.Type);
            alpc_trace_scalar_mutation("Message.receive_length", "buffer_length", 0x10000, ReceiveLength);
            ok_hex(Status, STATUS_SUCCESS);
            ok_eq_size(ReceiveLength, TotalLengths[Index]);
            if (!NT_SUCCESS(Status))
            {
                RtlFreeHeap(RtlGetProcessHeap(), 0, SendMessage);
                RtlFreeHeap(RtlGetProcessHeap(), 0, ReceiveMessage);
                return FALSE;
            }
            else
            {
                ok_eq_ulong(ReceiveMessage->u1.s1.TotalLength, TotalLengths[Index]);
                ok_eq_ulong(ReceiveMessage->u1.s1.DataLength, TotalLengths[Index] - sizeof(PORT_MESSAGE));
                ok(AlpcBoundaryPayloadMatches((const UCHAR *)(ReceiveMessage + 1), TotalLengths[Index] - sizeof(PORT_MESSAGE), PayloadByte), "payload mismatch for total length %u\n", TotalLengths[Index]);
            }
        }
        RtlFreeHeap(RtlGetProcessHeap(), 0, SendMessage);
    }

    SendMessage = AlpcBoundaryAllocateMessage(sizeof(PORT_MESSAGE) - 1, 0, LPC_DATAGRAM);
    ok(SendMessage != NULL, "failed to allocate short-total message\n");
    if (SendMessage)
    {
        Status = AlpcBoundarySendDatagram(ClientPort, SendMessage);
        trace("ALPC_OBSERVE status Message.total_header_minus_1=%08lx\n", Status);
        ok_hex(Status, STATUS_INVALID_PARAMETER);
        RtlFreeHeap(RtlGetProcessHeap(), 0, SendMessage);
    }

    SendMessage = AlpcBoundaryAllocateMessage(sizeof(PORT_MESSAGE), 1, LPC_DATAGRAM);
    ok(SendMessage != NULL, "failed to allocate inconsistent-length message\n");
    if (SendMessage)
    {
        Status = AlpcBoundarySendDatagram(ClientPort, SendMessage);
        trace("ALPC_OBSERVE status Message.data_exceeds_total=%08lx\n", Status);
        ok_hex(Status, STATUS_INVALID_PARAMETER);
        RtlFreeHeap(RtlGetProcessHeap(), 0, SendMessage);
    }

    SendMessage = AlpcBoundaryAllocateMessage(sizeof(PORT_MESSAGE), 0xffff, LPC_DATAGRAM);
    ok(SendMessage != NULL, "failed to allocate overflowing-data-length message\n");
    if (SendMessage)
    {
        Status = AlpcBoundarySendDatagram(ClientPort, SendMessage);
        trace("ALPC_OBSERVE status Message.data_length_ushort_overflow=%08lx\n", Status);
        ok_hex(Status, STATUS_INVALID_PARAMETER);
        RtlFreeHeap(RtlGetProcessHeap(), 0, SendMessage);
    }

    RtlFreeHeap(RtlGetProcessHeap(), 0, ReceiveMessage);
    return TRUE;
}

static
BOOLEAN
AlpcTestReceiveLengthMatrix(
    _In_ HANDLE ConnectionPort,
    _In_ HANDLE ClientPort)
{
    static const SIZE_T ReceiveLengths[] = {0, sizeof(PORT_MESSAGE) - 1, sizeof(PORT_MESSAGE), sizeof(PORT_MESSAGE) + 15, sizeof(PORT_MESSAGE) + 16, sizeof(PORT_MESSAGE) + 17, 0x1000};
    UCHAR ReceiveBuffer[0x1000];
    UCHAR Before[sizeof(ReceiveBuffer)];
    UCHAR ExpectedPayload[16];
    PPORT_MESSAGE SendMessage;
    SIZE_T Length;
    SIZE_T RetryLength;
    NTSTATUS Status;
    NTSTATUS RetryStatus;
    ULONG Index;
    UCHAR PayloadByte;

    for (Index = 0; Index < RTL_NUMBER_OF(ReceiveLengths); ++Index)
    {
        SendMessage = AlpcBoundaryAllocateMessage(sizeof(PORT_MESSAGE) + 16, 16, LPC_DATAGRAM);
        ok(SendMessage != NULL, "failed to allocate receive-capacity message\n");
        if (!SendMessage)
            return FALSE;
        PayloadByte = (UCHAR)(0xa0 + Index);
        RtlFillMemory(ExpectedPayload, sizeof(ExpectedPayload), PayloadByte);
        RtlFillMemory(SendMessage + 1, 16, PayloadByte);
        Status = AlpcBoundarySendDatagram(ClientPort, SendMessage);
        ok_hex(Status, STATUS_SUCCESS);
        RtlFreeHeap(RtlGetProcessHeap(), 0, SendMessage);
        if (!NT_SUCCESS(Status))
            continue;

        RtlFillMemory(ReceiveBuffer, sizeof(ReceiveBuffer), 0x55);
        RtlCopyMemory(Before, ReceiveBuffer, sizeof(Before));
        Length = ReceiveLengths[Index];
        Status = AlpcBoundaryReceive(ConnectionPort, (PPORT_MESSAGE)ReceiveBuffer, sizeof(ReceiveBuffer), &Length);
        trace("ALPC_OBSERVE status Message.receive_capacity capacity=%Iu status=%08lx returned_length=%Iu total=%u data=%u\n", ReceiveLengths[Index], Status, Length, ((PPORT_MESSAGE)ReceiveBuffer)->u1.s1.TotalLength, ((PPORT_MESSAGE)ReceiveBuffer)->u1.s1.DataLength);
        alpc_trace_scalar_mutation("Message.receive_capacity", "buffer_length", ReceiveLengths[Index], Length);
        AlpcTestTraceBufferMutation("Message.receive_capacity", Before, ReceiveBuffer, sizeof(ReceiveBuffer));
        if (ReceiveLengths[Index] < sizeof(PORT_MESSAGE) + 16)
        {
            ok_hex(Status, STATUS_BUFFER_TOO_SMALL);
            ok_eq_size(Length, sizeof(PORT_MESSAGE) + 16);
            ok(!memcmp(Before, ReceiveBuffer, sizeof(ReceiveBuffer)), "capacity %Iu copied a truncated message\n", ReceiveLengths[Index]);

            if (Status == STATUS_BUFFER_TOO_SMALL)
            {
                RtlFillMemory(ReceiveBuffer, sizeof(ReceiveBuffer), 0x55);
                RetryLength = sizeof(ReceiveBuffer);
                RetryStatus = AlpcBoundaryReceive(ConnectionPort, (PPORT_MESSAGE)ReceiveBuffer, sizeof(ReceiveBuffer), &RetryLength);
                trace("ALPC_OBSERVE status Message.receive_capacity_retry capacity=%Iu status=%08lx returned_length=%Iu total=%u data=%u\n", ReceiveLengths[Index], RetryStatus, RetryLength, ((PPORT_MESSAGE)ReceiveBuffer)->u1.s1.TotalLength, ((PPORT_MESSAGE)ReceiveBuffer)->u1.s1.DataLength);
                ok_hex(RetryStatus, STATUS_SUCCESS);
                if (!NT_SUCCESS(RetryStatus))
                    return FALSE;
                ok_eq_size(RetryLength, sizeof(PORT_MESSAGE) + 16);
                ok_eq_ulong(((PPORT_MESSAGE)ReceiveBuffer)->u1.s1.TotalLength, sizeof(PORT_MESSAGE) + 16);
                ok_eq_ulong(((PPORT_MESSAGE)ReceiveBuffer)->u1.s1.DataLength, 16);
                ok(!memcmp(ReceiveBuffer + sizeof(PORT_MESSAGE), ExpectedPayload, sizeof(ExpectedPayload)), "capacity %Iu retry returned the wrong payload\n", ReceiveLengths[Index]);
            }
            else if (!NT_SUCCESS(Status))
                return FALSE;
        }
        else
        {
            ok_hex(Status, STATUS_SUCCESS);
            if (!NT_SUCCESS(Status))
                return FALSE;
            ok_eq_size(Length, sizeof(PORT_MESSAGE) + 16);
            ok_eq_ulong(((PPORT_MESSAGE)ReceiveBuffer)->u1.s1.TotalLength, sizeof(PORT_MESSAGE) + 16);
            ok_eq_ulong(((PPORT_MESSAGE)ReceiveBuffer)->u1.s1.DataLength, 16);
            ok(!memcmp(ReceiveBuffer + sizeof(PORT_MESSAGE), ExpectedPayload, sizeof(ExpectedPayload)), "capacity %Iu returned the wrong payload\n", ReceiveLengths[Index]);
        }
    }
    return TRUE;
}

static
VOID
AlpcTestMisalignedMessages(
    _In_ HANDLE ClientPort)
{
    UCHAR Buffer[sizeof(PORT_MESSAGE) + 16];
    PPORT_MESSAGE Message = (PPORT_MESSAGE)(Buffer + 1);
    NTSTATUS Status;

    if (!AlpcTestNativeObservationEnabled())
    {
        skip("set ALPC_TEST_NATIVE_OBSERVE=1 to run misaligned message probes\n");
        return;
    }

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Message->u1.s1.TotalLength = sizeof(PORT_MESSAGE);
    Message->u1.s1.DataLength = 0;
    Message->u2.s2.Type = LPC_DATAGRAM;
    Status = AlpcBoundarySendDatagram(ClientPort, Message);
    trace("ALPC_OBSERVE status Message.misaligned_plus_1=%08lx\n", Status);
    ok(Status != STATUS_NOT_IMPLEMENTED, "misaligned message reached a stub\n");
}

START_TEST(NtAlpcMessageBoundary)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcMessageBoundary");
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    NTSTATUS Status;

    Status = AlpcBoundaryCreateConnectedPorts(&PortName, ALPC_MAX_ALLOWED_MESSAGE_LENGTH, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    if (AlpcTestSendLengthMatrix(ConnectionPort, ClientPort) && AlpcTestReceiveLengthMatrix(ConnectionPort, ClientPort))
        AlpcTestMisalignedMessages(ClientPort);

    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
}
