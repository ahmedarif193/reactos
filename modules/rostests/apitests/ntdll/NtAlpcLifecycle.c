/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         ALPC cancellation, callback, timeout, disconnect, and teardown tests
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"

#define ALPC_LIFECYCLE_REQUEST_COOKIE 0x4c465251
#define ALPC_LIFECYCLE_CALLBACK_COOKIE 0x4c464342
#define ALPC_LIFECYCLE_REPLY_COOKIE 0x4c465250
#define ALPC_LIFECYCLE_CONTEXT ((PVOID)(ULONG_PTR)0x4c49464543545801ULL)

typedef struct _ALPC_LIFECYCLE_CLIENT
{
    HANDLE Port;
    HANDLE StartedEvent;
    NTSTATUS FirstStatus;
    NTSTATUS SecondStatus;
    ULONG FirstCookie;
    ULONG SecondCookie;
    ULONG MessageId;
    ULONG CallbackId;
    USHORT MessageType;
    USHORT DataLength;
    USHORT TotalLength;
} ALPC_LIFECYCLE_CLIENT, *PALPC_LIFECYCLE_CLIENT;

typedef struct _ALPC_BLOCKED_RECEIVER
{
    HANDLE Port;
    HANDLE StartedEvent;
    NTSTATUS Status;
    SIZE_T Length;
    ALPC_TEST_MESSAGE Message;
} ALPC_BLOCKED_RECEIVER, *PALPC_BLOCKED_RECEIVER;

typedef struct _ALPC_AUTO_RELEASE_CLIENT
{
    HANDLE Port;
    HANDLE StartedEvent;
    ULONGLONG SendAttributeBuffer[16];
    ALPC_TEST_MESSAGE Request;
    ALPC_TEST_MESSAGE Reply;
    NTSTATUS Status;
} ALPC_AUTO_RELEASE_CLIENT, *PALPC_AUTO_RELEASE_CLIENT;

static
DWORD
WINAPI
AlpcCancellationClientThread(
    _In_ PVOID Parameter)
{
    PALPC_LIFECYCLE_CLIENT Client = Parameter;
    UCHAR AttributeBuffer[128];
    PALPC_MESSAGE_ATTRIBUTES Attributes;
    PALPC_CONTEXT_ATTR ContextAttribute;
    ALPC_TEST_MESSAGE Request;
    ALPC_TEST_MESSAGE Reply;
    LARGE_INTEGER Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    SIZE_T Length = sizeof(Reply);

    Attributes = (PALPC_MESSAGE_ATTRIBUTES)AttributeBuffer;
    RtlZeroMemory(AttributeBuffer, sizeof(AttributeBuffer));
    AlpcInitializeMessageAttribute(ALPC_MESSAGE_CONTEXT_ATTRIBUTE, Attributes, sizeof(AttributeBuffer), &Length);
    Attributes->ValidAttributes = ALPC_MESSAGE_CONTEXT_ATTRIBUTE;
    ContextAttribute = AlpcGetMessageAttribute(Attributes, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
    ContextAttribute->MessageContext = ALPC_LIFECYCLE_CONTEXT;
    AlpcTestInitializeMessage(&Request, ALPC_LIFECYCLE_REQUEST_COOKIE, 1);
    RtlZeroMemory(&Reply, sizeof(Reply));
    Length = sizeof(Reply);
    SetEvent(Client->StartedEvent);
    Client->FirstStatus = NtAlpcSendWaitReceivePort(Client->Port, ALPC_MSGFLG_SYNC_REQUEST, &Request.Header, Attributes, &Reply.Header, &Length, NULL, &Timeout);
    Client->FirstCookie = Reply.Cookie;
    Client->MessageId = Reply.Header.MessageId;
    Client->CallbackId = Reply.Header.CallbackId;
    Client->MessageType = Reply.Header.u2.s2.Type;
    Client->DataLength = Reply.Header.u1.s1.DataLength;
    Client->TotalLength = Reply.Header.u1.s1.TotalLength;
    return 0;
}

static
DWORD
WINAPI
AlpcCallbackClientThread(
    _In_ PVOID Parameter)
{
    PALPC_LIFECYCLE_CLIENT Client = Parameter;
    ALPC_TEST_MESSAGE Request;
    ALPC_TEST_MESSAGE Callback;
    ALPC_TEST_MESSAGE Reply;
    LARGE_INTEGER Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    SIZE_T Length = sizeof(Callback);

    AlpcTestInitializeMessage(&Request, ALPC_LIFECYCLE_REQUEST_COOKIE, 10);
    RtlZeroMemory(&Callback, sizeof(Callback));
    SetEvent(Client->StartedEvent);
    Client->FirstStatus = NtAlpcSendWaitReceivePort(Client->Port, ALPC_MSGFLG_SYNC_REQUEST, &Request.Header, NULL, &Callback.Header, &Length, NULL, &Timeout);
    Client->FirstCookie = Callback.Cookie;
    Client->MessageId = Callback.Header.MessageId;
    Client->CallbackId = Callback.Header.CallbackId;
    if (!NT_SUCCESS(Client->FirstStatus) || !Callback.Header.CallbackId)
        return 0;

    Callback.Cookie = ALPC_LIFECYCLE_REPLY_COOKIE;
    Callback.Value++;
    RtlZeroMemory(&Reply, sizeof(Reply));
    Length = sizeof(Reply);
    Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    Client->SecondStatus = NtAlpcSendWaitReceivePort(Client->Port, ALPC_MSGFLG_REPLY_MESSAGE, &Callback.Header, NULL, &Reply.Header, &Length, NULL, &Timeout);
    Client->SecondCookie = Reply.Cookie;
    return 0;
}

static
DWORD
WINAPI
AlpcBlockedReceiveThread(
    _In_ PVOID Parameter)
{
    PALPC_BLOCKED_RECEIVER Receiver = Parameter;
    LARGE_INTEGER Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);

    Receiver->Length = sizeof(Receiver->Message);
    RtlZeroMemory(&Receiver->Message, sizeof(Receiver->Message));
    SetEvent(Receiver->StartedEvent);
    Receiver->Status = NtAlpcSendWaitReceivePort(Receiver->Port, 0, NULL, NULL, &Receiver->Message.Header, &Receiver->Length, NULL, &Timeout);
    return 0;
}

static
DWORD
WINAPI
AlpcAutoReleaseClientThread(
    _In_ PVOID Parameter)
{
    PALPC_AUTO_RELEASE_CLIENT Client = Parameter;
    LARGE_INTEGER Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    SIZE_T Length = sizeof(Client->Reply);

    RtlZeroMemory(&Client->Reply, sizeof(Client->Reply));
    SetEvent(Client->StartedEvent);
    Client->Status = NtAlpcSendWaitReceivePort(Client->Port, ALPC_MSGFLG_SYNC_REQUEST, &Client->Request.Header, (PALPC_MESSAGE_ATTRIBUTES)Client->SendAttributeBuffer, &Client->Reply.Header, &Length, NULL, &Timeout);
    return 0;
}

static
VOID
AlpcTestCancellationCase(
    _In_ PCWSTR Name,
    _In_ ULONG CancelFlags,
    _In_ BOOLEAN ProbeMismatch)
{
    UNICODE_STRING PortName;
    UCHAR ReceiveAttributeBuffer[128];
    PALPC_MESSAGE_ATTRIBUTES ReceiveAttributes;
    PALPC_CONTEXT_ATTR ReceivedContext;
    ALPC_CONTEXT_ATTR CancelContext;
    PALPC_LIFECYCLE_CLIENT Client = NULL;
    ALPC_TEST_MESSAGE Request;
    LARGE_INTEGER Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    SIZE_T AttributeSize = 0;
    SIZE_T Length = sizeof(Request);
    NTSTATUS Status;
    NTSTATUS CancelStatus = STATUS_UNSUCCESSFUL;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    HANDLE Thread = NULL;
    DWORD WaitStatus = WAIT_OBJECT_0;
    DWORD StartWaitStatus;
    ULONGLONG CompletionStart;
    ULONGLONG CompletionElapsed;
    NTSTATUS ExpectedCancelStatus;
    NTSTATUS ExpectedLateReplyStatus;
    NTSTATUS ExpectedClientStatus;
    BOOLEAN ExpectReply;

    switch (CancelFlags)
    {
        case 0:
            ExpectedCancelStatus = STATUS_REQUEST_CANCELED;
            ExpectedLateReplyStatus = STATUS_REQUEST_CANCELED;
            ExpectedClientStatus = STATUS_MESSAGE_LOST;
            ExpectReply = FALSE;
            break;
        case ALPC_CANCELFLG_TRY_CANCEL:
            ExpectedCancelStatus = STATUS_MESSAGE_RETRIEVED;
            ExpectedLateReplyStatus = STATUS_SUCCESS;
            ExpectedClientStatus = STATUS_SUCCESS;
            ExpectReply = TRUE;
            break;
        case ALPC_CANCELFLG_NO_CONTEXT_CHECK:
            ExpectedCancelStatus = STATUS_CONTEXT_MISMATCH;
            ExpectedLateReplyStatus = STATUS_REQUEST_CANCELED;
            ExpectedClientStatus = STATUS_MESSAGE_LOST;
            ExpectReply = FALSE;
            break;
        default:
            ExpectedCancelStatus = STATUS_CONTEXT_MISMATCH;
            ExpectedLateReplyStatus = STATUS_SUCCESS;
            ExpectedClientStatus = STATUS_SUCCESS;
            ExpectReply = TRUE;
            break;
    }

    RtlInitUnicodeString(&PortName, Name);
    Status = AlpcTestCreateConnectedPorts(&PortName, 0, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Client = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Client));
    ok(Client != NULL, "cancellation context allocation failed\n");
    if (!Client)
        goto Cleanup;
    Client->Port = ClientPort;
    Client->StartedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Client->StartedEvent != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Client->StartedEvent)
        goto Cleanup;

    Thread = CreateThread(NULL, 0, AlpcCancellationClientThread, Client, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        goto Cleanup;
    StartWaitStatus = WaitForSingleObject(Client->StartedEvent, ALPC_TEST_TIMEOUT_MS);
    ok_eq_ulong(StartWaitStatus, WAIT_OBJECT_0);
    if (StartWaitStatus != WAIT_OBJECT_0)
        goto WaitClient;

    ReceiveAttributes = (PALPC_MESSAGE_ATTRIBUTES)ReceiveAttributeBuffer;
    RtlZeroMemory(ReceiveAttributeBuffer, sizeof(ReceiveAttributeBuffer));
    Status = AlpcInitializeMessageAttribute(ALPC_MESSAGE_CONTEXT_ATTRIBUTE, ReceiveAttributes, sizeof(ReceiveAttributeBuffer), &AttributeSize);
    ok_hex(Status, STATUS_SUCCESS);
    RtlZeroMemory(&Request, sizeof(Request));
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Request.Header, &Length, ReceiveAttributes, &Timeout);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto WaitClient;

    ok((ReceiveAttributes->ValidAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE) != 0, "cancel receive omitted the context attribute: %08lx\n", ReceiveAttributes->ValidAttributes);
    if (!(ReceiveAttributes->ValidAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE))
    {
        NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_REPLY_MESSAGE, &Request.Header, NULL, NULL, NULL, NULL, NULL);
        goto WaitClient;
    }
    ReceivedContext = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
    ok(ReceivedContext != NULL, "cancel receive returned no context attribute pointer\n");
    if (!ReceivedContext)
    {
        NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_REPLY_MESSAGE, &Request.Header, NULL, NULL, NULL, NULL, NULL);
        goto WaitClient;
    }
    CancelContext = *ReceivedContext;
    trace("ALPC_OBSERVE value Cancel.context flags=%08lx port=%p message=%p sequence=%lu message_id=%lu callback_id=%lu\n", CancelFlags, CancelContext.PortContext, CancelContext.MessageContext, CancelContext.Sequence, CancelContext.MessageId, CancelContext.CallbackId);

    if (ProbeMismatch)
    {
        CancelContext.MessageContext = (PVOID)((ULONG_PTR)CancelContext.MessageContext ^ 1);
        alpc_expect_status("Cancel.context_mismatch", NtAlpcCancelMessage(ClientPort, CancelFlags & ~ALPC_CANCELFLG_NO_CONTEXT_CHECK, &CancelContext), STATUS_MESSAGE_RETRIEVED);
        CancelContext = *ReceivedContext;
    }

    alpc_expect_status("Cancel.valid", NtAlpcCancelMessage(ClientPort, CancelFlags, &CancelContext), ExpectedCancelStatus);
    CancelStatus = Status;
    if (CancelFlags == ALPC_CANCELFLG_TRY_CANCEL)
        alpc_expect_status("Cancel.repeated", NtAlpcCancelMessage(ClientPort, CancelFlags, &CancelContext), STATUS_MESSAGE_RETRIEVED);
    alpc_expect_status("Cancel.late_reply", NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_REPLY_MESSAGE, &Request.Header, NULL, NULL, NULL, NULL, NULL), ExpectedLateReplyStatus);

WaitClient:
    CompletionStart = GetTickCount64();
    WaitStatus = AlpcTestJoinThread(Thread, &ClientPort, &ServerPort, "cancellation client");
    CompletionElapsed = GetTickCount64() - CompletionStart;
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus == WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE status Cancel.client_wait flags=%08lx cancel=%08lx status=%08lx type=%04x data_length=%u total_length=%u cookie=%08lx\n", CancelFlags, CancelStatus, Client->FirstStatus, Client->MessageType, Client->DataLength, Client->TotalLength, Client->FirstCookie);
        ok_hex(Client->FirstStatus, ExpectedClientStatus);
        ok(CompletionElapsed < 2000, "cancel client took %I64u ms to complete\n", CompletionElapsed);
        if (ExpectReply)
        {
            ok_eq_ulong(Client->MessageType & 0xff, LPC_REPLY);
            ok_eq_ulong(Client->DataLength, sizeof(ULONG) * 2);
            ok_eq_ulong(Client->TotalLength, sizeof(ALPC_TEST_MESSAGE));
            ok_eq_ulong(Client->FirstCookie, ALPC_LIFECYCLE_REQUEST_COOKIE);
        }
        else
        {
            ok_eq_ulong(Client->MessageType, 0);
            ok_eq_ulong(Client->DataLength, 0);
            ok_eq_ulong(Client->TotalLength, 0);
            ok_eq_ulong(Client->FirstCookie, 0);
        }
    }

Cleanup:
    if (Thread)
        CloseHandle(Thread);
    if (Client && WaitStatus == WAIT_OBJECT_0)
    {
        if (Client->StartedEvent)
            CloseHandle(Client->StartedEvent);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Client);
    }
    else if (Client)
        trace("ALPC_OBSERVE thread cancellation context_quarantined=%p event=%p\n", Client, Client->StartedEvent);
    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
}

static
VOID
AlpcTestCancellation(VOID)
{
    AlpcTestCancellationCase(L"\\RPC Control\\NtdllApitestNtAlpcCancel0", 0, TRUE);
    AlpcTestCancellationCase(L"\\RPC Control\\NtdllApitestNtAlpcCancelTry", ALPC_CANCELFLG_TRY_CANCEL, TRUE);
    AlpcTestCancellationCase(L"\\RPC Control\\NtdllApitestNtAlpcCancelNoContext", ALPC_CANCELFLG_NO_CONTEXT_CHECK, TRUE);
    AlpcTestCancellationCase(L"\\RPC Control\\NtdllApitestNtAlpcCancelBoth", ALPC_CANCELFLG_TRY_CANCEL | ALPC_CANCELFLG_NO_CONTEXT_CHECK, FALSE);
}

static
VOID
AlpcTestTimeoutForms(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcTimeouts");
    ALPC_PORT_ATTRIBUTES Attributes;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ALPC_TEST_MESSAGE Message;
    LARGE_INTEGER Timeout;
    ULONGLONG Start;
    ULONGLONG Elapsed;
    SIZE_T Length;
    NTSTATUS Status;
    HANDLE Port = NULL;

    AlpcTestInitializePortAttributes(&Attributes, 0);
    InitializeObjectAttributes(&ObjectAttributes, &PortName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtAlpcCreatePort(&Port, &ObjectAttributes, &Attributes);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlZeroMemory(&Message, sizeof(Message));
    Length = sizeof(Message);
    Timeout.QuadPart = 0;
    alpc_expect_status("Timeout.absolute_epoch", NtAlpcSendWaitReceivePort(Port, 0, NULL, NULL, &Message.Header, &Length, NULL, &Timeout), STATUS_TIMEOUT);

    Length = sizeof(Message);
    Timeout.QuadPart = -1;
    alpc_expect_status("Timeout.relative_minimum", NtAlpcSendWaitReceivePort(Port, 0, NULL, NULL, &Message.Header, &Length, NULL, &Timeout), STATUS_TIMEOUT);

    NtQuerySystemTime(&Timeout);
    Timeout.QuadPart -= 10000;
    Length = sizeof(Message);
    alpc_expect_status("Timeout.absolute_past", NtAlpcSendWaitReceivePort(Port, 0, NULL, NULL, &Message.Header, &Length, NULL, &Timeout), STATUS_TIMEOUT);

    NtQuerySystemTime(&Timeout);
    Timeout.QuadPart += 500000;
    Length = sizeof(Message);
    Start = GetTickCount64();
    alpc_expect_status("Timeout.absolute_future", NtAlpcSendWaitReceivePort(Port, 0, NULL, NULL, &Message.Header, &Length, NULL, &Timeout), STATUS_TIMEOUT);
    Elapsed = GetTickCount64() - Start;
    trace("ALPC_OBSERVE value Timeout.absolute_future_elapsed_ms=%I64u\n", Elapsed);
    ok(Elapsed >= 10, "50ms absolute timeout returned too early after %I64u ms\n", Elapsed);
    ok(Elapsed < 2000, "50ms absolute timeout took %I64u ms\n", Elapsed);

    NtClose(Port);
}

static
VOID
AlpcTestCloseWhileBlocked(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcCloseBlocked");
    ALPC_PORT_ATTRIBUTES Attributes;
    OBJECT_ATTRIBUTES ObjectAttributes;
    PALPC_BLOCKED_RECEIVER Receiver = NULL;
    NTSTATUS Status;
    HANDLE Port = NULL;
    HANDLE Thread = NULL;
    DWORD WaitStatus = WAIT_OBJECT_0;
    DWORD StartWaitStatus;
    DWORD ProbeWaitStatus;
    ULONGLONG CompletionStart;
    ULONGLONG CompletionElapsed;
    BOOLEAN Blocked;

    AlpcTestInitializePortAttributes(&Attributes, 0);
    InitializeObjectAttributes(&ObjectAttributes, &PortName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtAlpcCreatePort(&Port, &ObjectAttributes, &Attributes);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Receiver = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Receiver));
    ok(Receiver != NULL, "blocked receiver allocation failed\n");
    if (!Receiver)
        goto Cleanup;
    Receiver->Port = Port;
    Receiver->StartedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Receiver->StartedEvent != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Receiver->StartedEvent)
        goto Cleanup;

    Thread = CreateThread(NULL, 0, AlpcBlockedReceiveThread, Receiver, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        goto Cleanup;
    StartWaitStatus = WaitForSingleObject(Receiver->StartedEvent, ALPC_TEST_TIMEOUT_MS);
    ok_eq_ulong(StartWaitStatus, WAIT_OBJECT_0);
    ProbeWaitStatus = StartWaitStatus == WAIT_OBJECT_0 ? WaitForSingleObject(Thread, 100) : WAIT_FAILED;
    trace("ALPC_OBSERVE status Lifecycle.blocked_probe_wait=%lu\n", ProbeWaitStatus);
    Blocked = ProbeWaitStatus == WAIT_TIMEOUT;
    ok(Blocked, "ALPC receive worker completed before the port was closed\n");
    if (Blocked)
    {
        NtClose(Port);
        Port = NULL;
    }

    CompletionStart = GetTickCount64();
    WaitStatus = AlpcTestJoinThread(Thread, Blocked ? NULL : &Port, NULL, "close while blocked");
    CompletionElapsed = GetTickCount64() - CompletionStart;
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus == WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE status Lifecycle.close_while_blocked=%08lx length=%Iu\n", Receiver->Status, Receiver->Length);
        ok(Receiver->Status != STATUS_NOT_IMPLEMENTED, "close while blocked reached a stub\n");
        if (Blocked)
        {
            ok_hex(Receiver->Status, STATUS_PORT_CLOSED);
            ok(CompletionElapsed < 2000, "closed-handle receive took %I64u ms to complete\n", CompletionElapsed);
        }
    }

Cleanup:
    if (Thread)
        CloseHandle(Thread);
    if (Receiver && WaitStatus == WAIT_OBJECT_0)
    {
        if (Receiver->StartedEvent)
            CloseHandle(Receiver->StartedEvent);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Receiver);
    }
    else if (Receiver)
        trace("ALPC_OBSERVE thread blocked receiver context_quarantined=%p event=%p\n", Receiver, Receiver->StartedEvent);
    if (Port)
        NtClose(Port);
}

static
VOID
AlpcTestCallback(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcCallback");
    PALPC_LIFECYCLE_CLIENT Client = NULL;
    ALPC_TEST_MESSAGE Request;
    ALPC_TEST_MESSAGE InvalidCallback;
    ALPC_TEST_MESSAGE Callback;
    ALPC_TEST_MESSAGE CallbackReply;
    ALPC_TEST_MESSAGE InvalidReply;
    LARGE_INTEGER Timeout;
    SIZE_T Length;
    NTSTATUS Status;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    HANDLE Thread = NULL;
    DWORD WaitStatus = WAIT_OBJECT_0;
    DWORD StartWaitStatus;

    Status = AlpcTestCreateConnectedPorts(&PortName, 0, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Client = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Client));
    ok(Client != NULL, "callback context allocation failed\n");
    if (!Client)
        goto Cleanup;
    Client->Port = ClientPort;
    Client->StartedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Client->StartedEvent != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Client->StartedEvent)
        goto Cleanup;

    Thread = CreateThread(NULL, 0, AlpcCallbackClientThread, Client, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        goto Cleanup;
    StartWaitStatus = WaitForSingleObject(Client->StartedEvent, ALPC_TEST_TIMEOUT_MS);
    ok_eq_ulong(StartWaitStatus, WAIT_OBJECT_0);
    if (StartWaitStatus != WAIT_OBJECT_0)
        goto WaitClient;

    RtlZeroMemory(&Request, sizeof(Request));
    Length = sizeof(Request);
    Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Request.Header, &Length, NULL, &Timeout);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto WaitClient;

    InvalidCallback = Request;
    InvalidCallback.Header.MessageId++;
    RtlZeroMemory(&CallbackReply, sizeof(CallbackReply));
    Length = sizeof(CallbackReply);
    Timeout.QuadPart = 0;
    alpc_expect_status("Callback.message_id_mismatch", NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_SYNC_REQUEST, &InvalidCallback.Header, NULL, &CallbackReply.Header, &Length, NULL, &Timeout), STATUS_INVALID_MESSAGE);

    InvalidCallback = Request;
    InvalidCallback.Header.CallbackId++;
    Length = sizeof(CallbackReply);
    Timeout.QuadPart = 0;
    alpc_expect_status("Callback.callback_id_mismatch", NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_SYNC_REQUEST, &InvalidCallback.Header, NULL, &CallbackReply.Header, &Length, NULL, &Timeout), STATUS_INVALID_MESSAGE);

    Callback = Request;
    Callback.Cookie = ALPC_LIFECYCLE_CALLBACK_COOKIE;
    Callback.Value = 20;
    RtlZeroMemory(&CallbackReply, sizeof(CallbackReply));
    Length = sizeof(CallbackReply);
    Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    alpc_expect_status("Callback.nested_sync", NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_SYNC_REQUEST, &Callback.Header, NULL, &CallbackReply.Header, &Length, NULL, &Timeout), STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        trace("ALPC_OBSERVE value Callback.reply cookie=%08lx value=%lu message_id=%lu callback_id=%lu\n", CallbackReply.Cookie, CallbackReply.Value, CallbackReply.Header.MessageId, CallbackReply.Header.CallbackId);
        ok_eq_ulong(CallbackReply.Cookie, ALPC_LIFECYCLE_REPLY_COOKIE);
    }

    Request.Cookie = ALPC_LIFECYCLE_REPLY_COOKIE;
    Request.Value++;
    alpc_expect_status("Callback.original_reply", NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_REPLY_MESSAGE, &Request.Header, NULL, NULL, NULL, NULL, NULL), STATUS_INVALID_MESSAGE);

WaitClient:
    WaitStatus = AlpcTestJoinThread(Thread, &ClientPort, &ServerPort, "callback client");
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus == WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE status Callback.client first=%08lx second=%08lx first_cookie=%08lx second_cookie=%08lx message_id=%lu callback_id=%lu\n", Client->FirstStatus, Client->SecondStatus, Client->FirstCookie, Client->SecondCookie, Client->MessageId, Client->CallbackId);
        ok_hex(Client->FirstStatus, STATUS_SUCCESS);
        ok_hex(Client->SecondStatus, STATUS_TIMEOUT);
        ok_eq_ulong(Client->FirstCookie, ALPC_LIFECYCLE_CALLBACK_COOKIE);
        ok_eq_ulong(Client->SecondCookie, 0);
        ok(Client->MessageId != 0, "callback request returned a zero MessageId\n");
        ok(Client->CallbackId != 0, "callback request returned a zero CallbackId\n");
    }

    if (WaitStatus == WAIT_OBJECT_0)
    {
        InvalidReply = Request;
        InvalidReply.Header.MessageId++;
        alpc_expect_status("Callback.late_reply_id_mismatch", NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_REPLY_MESSAGE, &InvalidReply.Header, NULL, NULL, NULL, NULL, NULL), STATUS_INVALID_MESSAGE);
    }

Cleanup:
    if (Thread)
        CloseHandle(Thread);
    if (Client && WaitStatus == WAIT_OBJECT_0)
    {
        if (Client->StartedEvent)
            CloseHandle(Client->StartedEvent);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Client);
    }
    else if (Client)
        trace("ALPC_OBSERVE thread callback context_quarantined=%p event=%p\n", Client, Client->StartedEvent);
    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
}

static
VOID
AlpcTestLostReply(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcLostReply");
    PALPC_LIFECYCLE_CLIENT Client = NULL;
    ALPC_TEST_MESSAGE Request;
    LARGE_INTEGER Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    SIZE_T Length = sizeof(Request);
    NTSTATUS Status;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    HANDLE Thread = NULL;
    DWORD WaitStatus = WAIT_OBJECT_0;
    DWORD StartWaitStatus;
    ULONGLONG CompletionStart;
    ULONGLONG CompletionElapsed;
    BOOLEAN DisconnectedPendingReply = FALSE;

    Status = AlpcTestCreateConnectedPorts(&PortName, 0, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Client = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Client));
    ok(Client != NULL, "lost-reply context allocation failed\n");
    if (!Client)
        goto Cleanup;
    Client->Port = ClientPort;
    Client->StartedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!Client->StartedEvent)
        goto Cleanup;
    Thread = CreateThread(NULL, 0, AlpcCancellationClientThread, Client, 0, NULL);
    if (!Thread)
        goto Cleanup;
    StartWaitStatus = WaitForSingleObject(Client->StartedEvent, ALPC_TEST_TIMEOUT_MS);
    ok_eq_ulong(StartWaitStatus, WAIT_OBJECT_0);
    if (StartWaitStatus != WAIT_OBJECT_0)
        goto WaitClient;

    RtlZeroMemory(&Request, sizeof(Request));
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Request.Header, &Length, NULL, &Timeout);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        alpc_observe_status("Lifecycle.disconnect_with_pending_reply", NtAlpcDisconnectPort(ServerPort, 0));
        DisconnectedPendingReply = NT_SUCCESS(Status);
        NtClose(ServerPort);
        ServerPort = NULL;
    }

WaitClient:
    CompletionStart = GetTickCount64();
    WaitStatus = AlpcTestJoinThread(Thread, &ClientPort, &ConnectionPort, "lost-reply client");
    CompletionElapsed = GetTickCount64() - CompletionStart;
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus == WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE status Lifecycle.lost_reply_client=%08lx\n", Client->FirstStatus);
        ok(Client->FirstStatus != STATUS_NOT_IMPLEMENTED, "lost reply client reached a stub\n");
        if (DisconnectedPendingReply)
        {
            ok(Client->FirstStatus != STATUS_TIMEOUT, "lost-reply client completed by ordinary timeout\n");
            ok(CompletionElapsed < 2000, "lost-reply client took %I64u ms to complete\n", CompletionElapsed);
        }
    }

Cleanup:
    if (Thread)
        CloseHandle(Thread);
    if (Client && WaitStatus == WAIT_OBJECT_0)
    {
        if (Client->StartedEvent)
            CloseHandle(Client->StartedEvent);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Client);
    }
    else if (Client)
        trace("ALPC_OBSERVE thread lost-reply context_quarantined=%p event=%p\n", Client, Client->StartedEvent);
    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
}

static
NTSTATUS
AlpcLifecycleCreateAutoReleaseView(
    _In_ HANDLE Port,
    _Out_ PALPC_HANDLE Section,
    _Out_ PALPC_DATA_VIEW_ATTR View)
{
    SIZE_T ActualSize = 0;
    NTSTATUS Status;

    *Section = NULL;
    RtlZeroMemory(View, sizeof(*View));
    Status = NtAlpcCreatePortSection(Port, 0, NULL, 2 * PAGE_SIZE, Section, &ActualSize);
    if (!NT_SUCCESS(Status))
        return Status;

    View->SectionHandle = *Section;
    View->ViewSize = PAGE_SIZE;
    Status = NtAlpcCreateSectionView(Port, 0, View);
    if (!NT_SUCCESS(Status))
    {
        NtAlpcDeletePortSection(Port, 0, *Section);
        *Section = NULL;
    }
    return Status;
}

static
PALPC_MESSAGE_ATTRIBUTES
AlpcLifecycleInitializeAutoReleaseAttributes(
    _Out_writes_bytes_(BufferSize) PVOID Buffer,
    _In_ SIZE_T BufferSize,
    _In_ PALPC_DATA_VIEW_ATTR View)
{
    PALPC_MESSAGE_ATTRIBUTES Attributes = Buffer;
    PALPC_DATA_VIEW_ATTR ViewAttribute;
    SIZE_T RequiredSize = 0;
    NTSTATUS Status;

    RtlZeroMemory(Buffer, BufferSize);
    Status = AlpcInitializeMessageAttribute(ALPC_MESSAGE_VIEW_ATTRIBUTE, Attributes, BufferSize, &RequiredSize);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return NULL;
    Attributes->ValidAttributes = ALPC_MESSAGE_VIEW_ATTRIBUTE;
    ViewAttribute = AlpcGetMessageAttribute(Attributes, ALPC_MESSAGE_VIEW_ATTRIBUTE);
    ok(ViewAttribute != NULL, "AUTO_RELEASE view attribute was not allocated\n");
    if (!ViewAttribute)
        return NULL;
    *ViewAttribute = *View;
    ViewAttribute->Flags = ALPC_VIEWFLG_AUTO_RELEASE;
    return Attributes;
}

static
VOID
AlpcLifecycleVerifyAutoReleased(
    _In_ HANDLE Port,
    _In_ PVOID ViewBase,
    _In_ PCSTR Label)
{
    NTSTATUS Status;

    Status = NtAlpcDeleteSectionView(Port, 0, ViewBase);
    trace("ALPC_OBSERVE status %s.delete_after_completion=%08lx\n", Label, Status);
    ok(!NT_SUCCESS(Status), "%s left AUTO_RELEASE view %p registered\n", Label, ViewBase);
}

static
VOID
AlpcTestAutoReleaseTimeoutChild(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestAlpcAutoReleaseTimeout");
    ULONGLONG SendAttributeBuffer[16];
    PALPC_MESSAGE_ATTRIBUTES SendAttributes;
    ALPC_DATA_VIEW_ATTR View;
    ALPC_TEST_MESSAGE Request;
    ALPC_TEST_MESSAGE Reply;
    LARGE_INTEGER Timeout;
    SIZE_T Length;
    ALPC_HANDLE Section = NULL;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    NTSTATUS Status;

    Status = AlpcTestCreateConnectedPorts(&PortName, 0, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = AlpcLifecycleCreateAutoReleaseView(ClientPort, &Section, &View);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    SendAttributes = AlpcLifecycleInitializeAutoReleaseAttributes(SendAttributeBuffer, sizeof(SendAttributeBuffer), &View);
    if (!SendAttributes)
        goto Cleanup;

    AlpcTestInitializeMessage(&Request, ALPC_LIFECYCLE_REQUEST_COOKIE, 0x200);
    RtlZeroMemory(&Reply, sizeof(Reply));
    Length = sizeof(Reply);
    Timeout = AlpcTestRelativeTimeout(250);
    alpc_expect_status("AutoRelease.timeout", NtAlpcSendWaitReceivePort(ClientPort, ALPC_MSGFLG_SYNC_REQUEST, &Request.Header, SendAttributes, &Reply.Header, &Length, NULL, &Timeout), STATUS_TIMEOUT);
    if (Status == STATUS_TIMEOUT)
    {
        AlpcLifecycleVerifyAutoReleased(ClientPort, View.ViewBase, "AutoRelease.timeout");
        View.ViewBase = NULL;
    }

Cleanup:
    if (View.ViewBase)
        NtAlpcDeleteSectionView(ClientPort, 0, View.ViewBase);
    if (Section)
    {
        Status = NtAlpcDeletePortSection(ClientPort, 0, Section);
        ok_hex(Status, STATUS_SUCCESS);
    }
    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
}

static
VOID
AlpcTestAutoReleaseCancelChild(VOID)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestAlpcAutoReleaseCancel");
    ULONGLONG ReceiveAttributeBuffer[16];
    PALPC_MESSAGE_ATTRIBUTES ReceiveAttributes;
    PALPC_CONTEXT_ATTR ReceivedContext;
    PALPC_AUTO_RELEASE_CLIENT Client = NULL;
    ALPC_CONTEXT_ATTR CancelContext;
    ALPC_DATA_VIEW_ATTR View;
    ALPC_TEST_MESSAGE Request;
    LARGE_INTEGER Timeout;
    SIZE_T AttributeSize = 0;
    SIZE_T Length;
    ALPC_HANDLE Section = NULL;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    HANDLE Thread = NULL;
    NTSTATUS Status;
    NTSTATUS CancelStatus = STATUS_UNSUCCESSFUL;
    DWORD WaitStatus = WAIT_OBJECT_0;
    DWORD StartWaitStatus;
    BOOLEAN CancelCompleted = FALSE;

    Status = AlpcTestCreateConnectedPorts(&PortName, 0, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status = AlpcLifecycleCreateAutoReleaseView(ClientPort, &Section, &View);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Client = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Client));
    ok(Client != NULL, "AUTO_RELEASE cancel context allocation failed\n");
    if (!Client)
        goto Cleanup;
    Client->Port = ClientPort;
    Client->StartedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(Client->StartedEvent != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!Client->StartedEvent)
        goto Cleanup;
    if (!AlpcLifecycleInitializeAutoReleaseAttributes(Client->SendAttributeBuffer, sizeof(Client->SendAttributeBuffer), &View))
        goto Cleanup;
    AlpcTestInitializeMessage(&Client->Request, ALPC_LIFECYCLE_REQUEST_COOKIE, 0x201);
    Client->Status = STATUS_UNSUCCESSFUL;

    Thread = CreateThread(NULL, 0, AlpcAutoReleaseClientThread, Client, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
        goto Cleanup;
    StartWaitStatus = WaitForSingleObject(Client->StartedEvent, ALPC_TEST_TIMEOUT_MS);
    ok_eq_ulong(StartWaitStatus, WAIT_OBJECT_0);
    if (StartWaitStatus != WAIT_OBJECT_0)
        goto WaitClient;

    RtlZeroMemory(ReceiveAttributeBuffer, sizeof(ReceiveAttributeBuffer));
    Status = AlpcInitializeMessageAttribute(ALPC_MESSAGE_CONTEXT_ATTRIBUTE, ReceiveAttributes = (PALPC_MESSAGE_ATTRIBUTES)ReceiveAttributeBuffer, sizeof(ReceiveAttributeBuffer), &AttributeSize);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto WaitClient;
    RtlZeroMemory(&Request, sizeof(Request));
    Length = sizeof(Request);
    Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Request.Header, &Length, ReceiveAttributes, &Timeout);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto WaitClient;

    ok((ReceiveAttributes->ValidAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE) != 0, "AUTO_RELEASE cancel receive omitted context: %08lx\n", ReceiveAttributes->ValidAttributes);
    ReceivedContext = AlpcGetMessageAttribute(ReceiveAttributes, ALPC_MESSAGE_CONTEXT_ATTRIBUTE);
    if ((ReceiveAttributes->ValidAttributes & ALPC_MESSAGE_CONTEXT_ATTRIBUTE) && ReceivedContext)
    {
        CancelContext = *ReceivedContext;
        CancelStatus = NtAlpcCancelMessage(ClientPort, 0, &CancelContext);
        ok_hex(CancelStatus, STATUS_MESSAGE_RETRIEVED);
        CancelCompleted = CancelStatus == STATUS_MESSAGE_RETRIEVED;
    }
    if (!CancelCompleted)
        NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_REPLY_MESSAGE, &Request.Header, NULL, NULL, NULL, NULL, NULL);

WaitClient:
    WaitStatus = AlpcTestJoinThread(Thread, &ClientPort, &ServerPort, "AUTO_RELEASE cancel client");
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus != WAIT_OBJECT_0)
    {
        CloseHandle(Thread);
        trace("ALPC_OBSERVE thread AUTO_RELEASE cancel context_quarantined=%p event=%p section=%p view=%p\n", Client, Client->StartedEvent, Section, View.ViewBase);
        return;
    }
    trace("ALPC_OBSERVE status AutoRelease.cancel cancel=%08lx client=%08lx\n", CancelStatus, Client->Status);
    if (CancelCompleted)
    {
        ok_hex(Client->Status, STATUS_MESSAGE_LOST);
        AlpcLifecycleVerifyAutoReleased(ClientPort, View.ViewBase, "AutoRelease.cancel");
        View.ViewBase = NULL;
    }

Cleanup:
    if (Thread)
        CloseHandle(Thread);
    if (Client)
    {
        if (Client->StartedEvent)
            CloseHandle(Client->StartedEvent);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Client);
    }
    if (View.ViewBase)
        NtAlpcDeleteSectionView(ClientPort, 0, View.ViewBase);
    if (Section)
    {
        Status = NtAlpcDeletePortSection(ClientPort, 0, Section);
        ok_hex(Status, STATUS_SUCCESS);
    }
    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
}

START_TEST(NtAlpcLifecycle)
{
    if (AlpcTestIsChildMode("auto-release-timeout"))
    {
        AlpcTestAutoReleaseTimeoutChild();
        return;
    }
    if (AlpcTestIsChildMode("auto-release-cancel"))
    {
        AlpcTestAutoReleaseCancelChild();
        return;
    }
    if (AlpcTestIsChildMode("close-while-blocked"))
    {
        AlpcTestCloseWhileBlocked();
        return;
    }

    AlpcTestTimeoutForms();
    AlpcTestRunIsolatedCase(L"NtAlpcLifecycle", L"auto-release-timeout", ALPC_TEST_CHILD_TIMEOUT_MS);
    AlpcTestRunIsolatedCase(L"NtAlpcLifecycle", L"auto-release-cancel", ALPC_TEST_CHILD_TIMEOUT_MS);
    AlpcTestCancellation();
    AlpcTestCallback();
    AlpcTestLostReply();
    AlpcTestRunIsolatedCase(L"NtAlpcLifecycle", L"close-while-blocked", ALPC_TEST_CHILD_TIMEOUT_MS);
}
