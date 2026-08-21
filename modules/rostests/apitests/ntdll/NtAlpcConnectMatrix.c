/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         ALPC connect, ConnectPortEx, acceptance, and rejection tests
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"

typedef struct _ALPC_CONNECT_MATRIX_CONTEXT
{
    UNICODE_STRING PortName;
    ALPC_PORT_ATTRIBUTES PortAttributes;
    UCHAR RequiredSid[SECURITY_MAX_SID_SIZE];
    BOOLEAN HasRequiredSid;
    BOOLEAN UseConnectEx;
    BOOLEAN NullConnectionMessage;
    BOOLEAN HeaderOnlyRequest;
    HANDLE Port;
    NTSTATUS Status;
    SIZE_T InitialLength;
    SIZE_T Length;
    ALPC_TEST_MESSAGE Message;
    UCHAR MessagePadding[16];
    UCHAR Before[sizeof(ALPC_TEST_MESSAGE)];
} ALPC_CONNECT_MATRIX_CONTEXT, *PALPC_CONNECT_MATRIX_CONTEXT;

static
DWORD
WINAPI
AlpcConnectMatrixThread(
    _In_ PVOID Parameter)
{
    PALPC_CONNECT_MATRIX_CONTEXT Context = Parameter;
    OBJECT_ATTRIBUTES ObjectAttributes;
    LARGE_INTEGER Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);

    AlpcTestInitializeMessage(&Context->Message, 0x434f4e4d, 0x101);
    if (Context->HeaderOnlyRequest)
    {
        Context->Message.Header.u1.s1.TotalLength = sizeof(PORT_MESSAGE);
        Context->Message.Header.u1.s1.DataLength = 0;
    }
    Context->Length = Context->InitialLength;
    RtlCopyMemory(Context->Before, &Context->Message, sizeof(Context->Before));
    if (Context->UseConnectEx)
    {
        InitializeObjectAttributes(&ObjectAttributes, &Context->PortName, OBJ_CASE_INSENSITIVE, NULL, NULL);
        Context->Status = NtAlpcConnectPortEx(&Context->Port, &ObjectAttributes, NULL, &Context->PortAttributes, ALPC_SYNC_CONNECTION, NULL, Context->NullConnectionMessage ? NULL : &Context->Message.Header, &Context->Length, NULL, NULL, &Timeout);
    }
    else
    {
        Context->Status = NtAlpcConnectPort(&Context->Port, &Context->PortName, NULL, &Context->PortAttributes, ALPC_SYNC_CONNECTION, Context->HasRequiredSid ? Context->RequiredSid : NULL, Context->NullConnectionMessage ? NULL : &Context->Message.Header, &Context->Length, NULL, NULL, &Timeout);
    }
    return 0;
}

static
VOID
AlpcTestConnectRoundTrip(
    _In_ PCWSTR Name,
    _In_ BOOLEAN UseConnectEx,
    _In_ BOOLEAN AcceptConnection)
{
    UNICODE_STRING PortName;
    PALPC_CONNECT_MATRIX_CONTEXT Context = NULL;
    ALPC_PORT_ATTRIBUTES Attributes;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ALPC_TEST_MESSAGE Request;
    LARGE_INTEGER Timeout;
    SIZE_T Length;
    NTSTATUS Status;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    HANDLE Thread = NULL;
    DWORD WaitStatus = WAIT_OBJECT_0;

    RtlInitUnicodeString(&PortName, Name);
    AlpcTestInitializePortAttributes(&Attributes, 0);
    InitializeObjectAttributes(&ObjectAttributes, &PortName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtAlpcCreatePort(&ConnectionPort, &ObjectAttributes, &Attributes);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Context = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    ok(Context != NULL, "connection context allocation failed\n");
    if (!Context)
        goto Cleanup;
    Context->PortName = PortName;
    Context->PortAttributes = Attributes;
    Context->UseConnectEx = UseConnectEx;
    Context->Port = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    Context->Status = STATUS_UNSUCCESSFUL;
    Context->InitialLength = sizeof(Context->Message);
    Context->Length = ~(SIZE_T)0;
    Thread = CreateThread(NULL, 0, AlpcConnectMatrixThread, Context, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        goto Cleanup;

    RtlZeroMemory(&Request, sizeof(Request));
    Length = sizeof(Request);
    Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Request.Header, &Length, NULL, &Timeout);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtAlpcAcceptConnectPort(&ServerPort, ConnectionPort, 0, NULL, &Attributes, ALPC_TEST_PORT_CONTEXT, &Request.Header, NULL, AcceptConnection);
        trace("ALPC_OBSERVE status Connect.accept use_ex=%u accept=%u status=%08lx output=%p\n", UseConnectEx, AcceptConnection, Status, ServerPort);
        alpc_trace_scalar_mutation("Connect.accept", "output", (HANDLE)(ULONG_PTR)0x5555555555555555ULL, ServerPort);
        ok(Status != STATUS_NOT_IMPLEMENTED, "accept reached a stub\n");
    }

    WaitStatus = AlpcTestJoinThread(Thread, &ConnectionPort, &ServerPort, "connect matrix");
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus == WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE status Connect.client use_ex=%u accept=%u status=%08lx output=%p length=%Iu type=%04x\n", UseConnectEx, AcceptConnection, Context->Status, Context->Port, Context->Length, Context->Message.Header.u2.s2.Type);
        alpc_trace_scalar_mutation("Connect.client", "output", (HANDLE)(ULONG_PTR)0x5555555555555555ULL, Context->Port);
        alpc_trace_scalar_mutation("Connect.client", "length", sizeof(Context->Message), Context->Length);
        AlpcTestTraceBufferMutation(UseConnectEx ? "ConnectEx.message" : "Connect.message", Context->Before, (const UCHAR *)&Context->Message, sizeof(Context->Message));
        if (AcceptConnection)
            ok_hex(Context->Status, STATUS_SUCCESS);
        else
            ok_hex(Context->Status, STATUS_PORT_CONNECTION_REFUSED);
    }

Cleanup:
    if (Thread)
        CloseHandle(Thread);
    if (Context && WaitStatus == WAIT_OBJECT_0 && Context->Port && Context->Port != (HANDLE)(ULONG_PTR)0x5555555555555555ULL)
    {
        NtAlpcDisconnectPort(Context->Port, 0);
        NtClose(Context->Port);
    }
    if (ServerPort && ServerPort != (HANDLE)(ULONG_PTR)0x5555555555555555ULL)
    {
        NtAlpcDisconnectPort(ServerPort, 0);
        NtClose(ServerPort);
    }
    if (ConnectionPort)
        NtClose(ConnectionPort);
    if (Context && WaitStatus == WAIT_OBJECT_0)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
    else if (Context)
        trace("ALPC_OBSERVE thread connect matrix context_quarantined=%p\n", Context);
}

static
VOID
AlpcTestRequiredServerSidMismatch(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcRequiredSid");
    UCHAR SidBuffer[SECURITY_MAX_SID_SIZE];
    PALPC_CONNECT_MATRIX_CONTEXT Context = NULL;
    ALPC_PORT_ATTRIBUTES Attributes;
    OBJECT_ATTRIBUTES ObjectAttributes;
    DWORD SidSize = sizeof(SidBuffer);
    NTSTATUS Status;
    HANDLE ConnectionPort = NULL;
    HANDLE Thread = NULL;
    DWORD WaitStatus = WAIT_OBJECT_0;

    AlpcTestInitializePortAttributes(&Attributes, 0);
    InitializeObjectAttributes(&ObjectAttributes, &PortName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtAlpcCreatePort(&ConnectionPort, &ObjectAttributes, &Attributes);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    if (!CreateWellKnownSid(WinWorldSid, NULL, SidBuffer, &SidSize))
        goto Cleanup;

    Context = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    ok(Context != NULL, "required SID context allocation failed\n");
    if (!Context)
        goto Cleanup;
    Context->PortName = PortName;
    Context->PortAttributes = Attributes;
    RtlCopyMemory(Context->RequiredSid, SidBuffer, RtlLengthSid(SidBuffer));
    Context->HasRequiredSid = TRUE;
    Context->Port = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    Context->Status = STATUS_UNSUCCESSFUL;
    Context->InitialLength = sizeof(Context->Message);
    Context->Length = ~(SIZE_T)0;
    Thread = CreateThread(NULL, 0, AlpcConnectMatrixThread, Context, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        goto Cleanup;

    WaitStatus = AlpcTestJoinThread(Thread, &ConnectionPort, NULL, "required SID connect");
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus == WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE status Connect.required_sid_world=%08lx output=%p length=%Iu\n", Context->Status, Context->Port, Context->Length);
        alpc_trace_scalar_mutation("Connect.required_sid_world", "output", (HANDLE)(ULONG_PTR)0x5555555555555555ULL, Context->Port);
        alpc_trace_scalar_mutation("Connect.required_sid_world", "length", sizeof(Context->Message), Context->Length);
        ok(Context->Status != STATUS_NOT_IMPLEMENTED, "required SID connect reached a stub\n");
    }

Cleanup:
    if (Thread)
        CloseHandle(Thread);
    if (Context && WaitStatus == WAIT_OBJECT_0 && Context->Port && Context->Port != (HANDLE)(ULONG_PTR)0x5555555555555555ULL)
        NtClose(Context->Port);
    if (ConnectionPort)
        NtClose(ConnectionPort);
    if (Context && WaitStatus == WAIT_OBJECT_0)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
    else if (Context)
        trace("ALPC_OBSERVE thread required SID context_quarantined=%p\n", Context);
}

static
VOID
AlpcTestAcceptedReplyCapacityCase(
    _In_ PCWSTR Name,
    _In_ BOOLEAN NullConnectionMessage,
    _In_ SIZE_T InitialLength,
    _In_ USHORT ReplyLength,
    _In_ NTSTATUS ExpectedStatus)
{
    UNICODE_STRING PortName;
    PALPC_CONNECT_MATRIX_CONTEXT Context = NULL;
    ALPC_PORT_ATTRIBUTES Attributes;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ALPC_TEST_MESSAGE Request;
    LARGE_INTEGER Timeout;
    SIZE_T Length;
    NTSTATUS Status;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE Thread = NULL;
    DWORD WaitStatus = WAIT_OBJECT_0;

    RtlInitUnicodeString(&PortName, Name);
    AlpcTestInitializePortAttributes(&Attributes, 0);
    InitializeObjectAttributes(&ObjectAttributes, &PortName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtAlpcCreatePort(&ConnectionPort, &ObjectAttributes, &Attributes);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Context = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    ok(Context != NULL, "reply-capacity context allocation failed\n");
    if (!Context)
        goto Cleanup;
    Context->PortName = PortName;
    Context->PortAttributes = Attributes;
    Context->NullConnectionMessage = NullConnectionMessage;
    Context->HeaderOnlyRequest = TRUE;
    Context->InitialLength = InitialLength;
    Context->Port = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    Context->Status = STATUS_UNSUCCESSFUL;
    Context->Length = ~(SIZE_T)0;
    Thread = CreateThread(NULL, 0, AlpcConnectMatrixThread, Context, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        goto Cleanup;

    RtlZeroMemory(&Request, sizeof(Request));
    Length = sizeof(Request);
    Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Request.Header, &Length, NULL, &Timeout);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Request.Header.u1.s1.TotalLength = ReplyLength;
        Request.Header.u1.s1.DataLength = ReplyLength - sizeof(PORT_MESSAGE);
        Request.Cookie = 0x52504c59;
        Request.Value = (ULONG)InitialLength;
        Status = NtAlpcAcceptConnectPort(&ServerPort, ConnectionPort, 0, NULL, &Attributes, ALPC_TEST_PORT_CONTEXT, &Request.Header, NULL, TRUE);
        ok_hex(Status, STATUS_SUCCESS);
    }

    WaitStatus = AlpcTestJoinThread(Thread, &ConnectionPort, &ServerPort, "accepted reply capacity");
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus == WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE status Connect.reply_capacity null_message=%u initial=%Iu required=%u status=%08lx output=%p returned_length=%Iu\n", NullConnectionMessage, InitialLength, ReplyLength, Context->Status, Context->Port, Context->Length);
        alpc_trace_scalar_mutation("Connect.reply_capacity", "output", (HANDLE)(ULONG_PTR)0x5555555555555555ULL, Context->Port);
        alpc_trace_scalar_mutation("Connect.reply_capacity", "length", InitialLength, Context->Length);
        AlpcTestTraceBufferMutation("Connect.reply_capacity.message", Context->Before, (const UCHAR *)&Context->Message, sizeof(Context->Message));
        ok_hex(Context->Status, ExpectedStatus);
        ok_eq_size(Context->Length, ReplyLength);
        if (ExpectedStatus == STATUS_BUFFER_TOO_SMALL)
        {
            ok(Context->Port == (HANDLE)(ULONG_PTR)0x5555555555555555ULL, "buffer-too-small connect changed output handle to %p\n", Context->Port);
            if (!NullConnectionMessage)
                ok(!memcmp(Context->Before, &Context->Message, sizeof(Context->Message)), "buffer-too-small connect copied a truncated reply\n");
        }
        else if (NT_SUCCESS(ExpectedStatus))
        {
            ok(Context->Port != NULL && Context->Port != (HANDLE)(ULONG_PTR)0x5555555555555555ULL, "successful connect returned %p\n", Context->Port);
            if (NullConnectionMessage)
                ok(!memcmp(Context->Before, &Context->Message, sizeof(Context->Message)), "NULL-message connect changed private message storage\n");
            else
                ok_eq_ulong(Context->Message.Cookie, 0x52504c59);
        }
    }

Cleanup:
    if (Thread)
        CloseHandle(Thread);
    if (Context && WaitStatus == WAIT_OBJECT_0 && Context->Port && Context->Port != (HANDLE)(ULONG_PTR)0x5555555555555555ULL)
    {
        NtAlpcDisconnectPort(Context->Port, 0);
        NtClose(Context->Port);
    }
    if (ServerPort)
    {
        NtAlpcDisconnectPort(ServerPort, 0);
        NtClose(ServerPort);
    }
    if (ConnectionPort)
        NtClose(ConnectionPort);
    if (Context && WaitStatus == WAIT_OBJECT_0)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
    else if (Context)
        trace("ALPC_OBSERVE thread reply-capacity context_quarantined=%p\n", Context);
}

static
VOID
AlpcTestMalformedConnectionMessages(VOID)
{
    static UNICODE_STRING MissingName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcNoSuchConnectionPort");
    ALPC_PORT_ATTRIBUTES Attributes;
    ALPC_TEST_MESSAGE Message;
    LARGE_INTEGER Timeout;
    SIZE_T Length;
    NTSTATUS Status;
    HANDLE Output;

    AlpcTestInitializePortAttributes(&Attributes, 0);
    Timeout.QuadPart = 0;

    AlpcTestInitializeMessage(&Message, 0x434f4e4d, 1);
    Message.Header.u1.s1.TotalLength = sizeof(PORT_MESSAGE) - 1;
    Length = sizeof(Message);
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_status("Connect.malformed.total_header_minus_1", NtAlpcConnectPort(&Output, &MissingName, NULL, &Attributes, 0, NULL, &Message.Header, &Length, NULL, NULL, &Timeout));
    trace("ALPC_OBSERVE value Connect.malformed.total_header_minus_1 output=%p length=%Iu\n", Output, Length);
    alpc_trace_scalar_mutation("Connect.malformed.total_header_minus_1", "output", (HANDLE)(ULONG_PTR)0x5555555555555555ULL, Output);
    alpc_trace_scalar_mutation("Connect.malformed.total_header_minus_1", "length", sizeof(Message), Length);

    AlpcTestInitializeMessage(&Message, 0x434f4e4d, 1);
    Message.Header.u1.s1.DataLength++;
    Length = sizeof(Message);
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_expect_status("Connect.malformed.data_exceeds_total", NtAlpcConnectPort(&Output, &MissingName, NULL, &Attributes, 0, NULL, &Message.Header, &Length, NULL, NULL, &Timeout), STATUS_OBJECT_NAME_NOT_FOUND);
    trace("ALPC_OBSERVE value Connect.malformed.data_exceeds_total output=%p length=%Iu\n", Output, Length);
    alpc_trace_scalar_mutation("Connect.malformed.data_exceeds_total", "output", (HANDLE)(ULONG_PTR)0x5555555555555555ULL, Output);
    alpc_trace_scalar_mutation("Connect.malformed.data_exceeds_total", "length", sizeof(Message), Length);

    AlpcTestInitializeMessage(&Message, 0x434f4e4d, 1);
    Length = sizeof(PORT_MESSAGE) - 1;
    Output = (HANDLE)(ULONG_PTR)0x5555555555555555ULL;
    alpc_observe_status("Connect.malformed.buffer_length_header_minus_1", NtAlpcConnectPort(&Output, &MissingName, NULL, &Attributes, 0, NULL, &Message.Header, &Length, NULL, NULL, &Timeout));
    trace("ALPC_OBSERVE value Connect.malformed.buffer_length_header_minus_1 output=%p length=%Iu\n", Output, Length);
    alpc_trace_scalar_mutation("Connect.malformed.buffer_length_header_minus_1", "output", (HANDLE)(ULONG_PTR)0x5555555555555555ULL, Output);
    alpc_trace_scalar_mutation("Connect.malformed.buffer_length_header_minus_1", "length", sizeof(PORT_MESSAGE) - 1, Length);
}

START_TEST(NtAlpcConnectMatrix)
{
    AlpcTestConnectRoundTrip(L"\\RPC Control\\NtdllApitestNtAlpcConnectAccept", FALSE, TRUE);
    AlpcTestConnectRoundTrip(L"\\RPC Control\\NtdllApitestNtAlpcConnectReject", FALSE, FALSE);
    AlpcTestConnectRoundTrip(L"\\RPC Control\\NtdllApitestNtAlpcConnectExAccept", TRUE, TRUE);
    AlpcTestConnectRoundTrip(L"\\RPC Control\\NtdllApitestNtAlpcConnectExReject", TRUE, FALSE);
    AlpcTestAcceptedReplyCapacityCase(L"\\RPC Control\\NtdllApitestNtAlpcReplyShort", FALSE, sizeof(ALPC_TEST_MESSAGE) - 1, sizeof(ALPC_TEST_MESSAGE), STATUS_BUFFER_TOO_SMALL);
    AlpcTestAcceptedReplyCapacityCase(L"\\RPC Control\\NtdllApitestNtAlpcReplyExact", FALSE, sizeof(ALPC_TEST_MESSAGE), sizeof(ALPC_TEST_MESSAGE), STATUS_SUCCESS);
    AlpcTestAcceptedReplyCapacityCase(L"\\RPC Control\\NtdllApitestNtAlpcReplyOversized", FALSE, sizeof(ALPC_TEST_MESSAGE) + 1, sizeof(ALPC_TEST_MESSAGE), STATUS_SUCCESS);
    AlpcTestAcceptedReplyCapacityCase(L"\\RPC Control\\NtdllApitestNtAlpcReplyNullShort", TRUE, sizeof(PORT_MESSAGE) - 1, sizeof(PORT_MESSAGE), STATUS_BUFFER_TOO_SMALL);
    AlpcTestAcceptedReplyCapacityCase(L"\\RPC Control\\NtdllApitestNtAlpcReplyNullExact", TRUE, sizeof(PORT_MESSAGE), sizeof(PORT_MESSAGE), STATUS_SUCCESS);
    AlpcTestAcceptedReplyCapacityCase(L"\\RPC Control\\NtdllApitestNtAlpcReplyNullOversized", TRUE, sizeof(PORT_MESSAGE) + 1, sizeof(PORT_MESSAGE), STATUS_SUCCESS);
    AlpcTestRequiredServerSidMismatch();
    AlpcTestMalformedConnectionMessages();
}
