/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         End-to-end tests for the ALPC port core
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"

#define TEST_CONNECT_COOKIE  0x434E4E43
#define TEST_REQUEST_COOKIE  0x52514553
#define TEST_REPLY_COOKIE    0x52504C59
#define TEST_ASYNC_COOKIE    0x4153594E
#define TEST_PORT_CONTEXT ((PVOID)(ULONG_PTR)0x12345678)
#define TEST_LPC_CONTINUATION_REQUIRED 0x2000

typedef struct _TEST_ALPC_MESSAGE
{
    PORT_MESSAGE Header;
    ULONG Cookie;
    ULONG Value;
} TEST_ALPC_MESSAGE, *PTEST_ALPC_MESSAGE;

typedef struct _TEST_CLIENT_RESULT
{
    NTSTATUS ConnectStatus;
    NTSTATUS RequestStatus;
    NTSTATUS AsyncStatus;
    NTSTATUS DisconnectStatus;
    ULONG ConnectType;
    SIZE_T ConnectLength;
    ULONG ReplyCookie;
    ULONG ReplyValue;
} TEST_CLIENT_RESULT, *PTEST_CLIENT_RESULT;

static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcPort");
static ALPC_PORT_ATTRIBUTES PortAttributes;
static TEST_CLIENT_RESULT ClientResult;
static HANDLE AsyncReceivedEvent;

static
VOID
InitializeTestMessage(
    _Out_ PTEST_ALPC_MESSAGE Message,
    _In_ ULONG Cookie,
    _In_ ULONG Value)
{
    RtlZeroMemory(Message, sizeof(*Message));
    Message->Header.u1.s1.DataLength = sizeof(*Message) - sizeof(Message->Header);
    Message->Header.u1.s1.TotalLength = sizeof(*Message);
    Message->Cookie = Cookie;
    Message->Value = Value;
}

static
LARGE_INTEGER
RelativeTimeout(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -(LONGLONG)Milliseconds * 10000;
    return Timeout;
}

static
DWORD
WINAPI
AlpcClientThread(
    _In_ PVOID Parameter)
{
    TEST_ALPC_MESSAGE ConnectMessage, RequestMessage, ReplyMessage, AsyncMessage;
    LARGE_INTEGER Timeout;
    SIZE_T BufferLength;
    HANDLE ClientPort = NULL;

    UNREFERENCED_PARAMETER(Parameter);
    RtlZeroMemory(&ClientResult, sizeof(ClientResult));

    InitializeTestMessage(&ConnectMessage, TEST_CONNECT_COOKIE, 1);
    BufferLength = sizeof(ConnectMessage);
    Timeout = RelativeTimeout(10000);
    ClientResult.ConnectStatus = NtAlpcConnectPort(&ClientPort, &PortName, NULL, &PortAttributes, ALPC_SYNC_CONNECTION, NULL, &ConnectMessage.Header, &BufferLength, NULL, NULL, &Timeout);
    ClientResult.ConnectLength = BufferLength;
    ClientResult.ConnectType = ConnectMessage.Header.u2.s2.Type;
    if (!NT_SUCCESS(ClientResult.ConnectStatus))
        return 0;

    InitializeTestMessage(&RequestMessage, TEST_REQUEST_COOKIE, 41);
    RtlZeroMemory(&ReplyMessage, sizeof(ReplyMessage));
    BufferLength = sizeof(ReplyMessage);
    Timeout = RelativeTimeout(10000);
    ClientResult.RequestStatus = NtAlpcSendWaitReceivePort(ClientPort, ALPC_MSGFLG_SYNC_REQUEST, &RequestMessage.Header, NULL, &ReplyMessage.Header, &BufferLength, NULL, &Timeout);
    if (NT_SUCCESS(ClientResult.RequestStatus))
    {
        ClientResult.ReplyCookie = ReplyMessage.Cookie;
        ClientResult.ReplyValue = ReplyMessage.Value;
    }

    InitializeTestMessage(&AsyncMessage, TEST_ASYNC_COOKIE, 73);
    ClientResult.AsyncStatus = NtAlpcSendWaitReceivePort(ClientPort, 0, &AsyncMessage.Header, NULL, NULL, NULL, NULL, NULL);
    if (NT_SUCCESS(ClientResult.AsyncStatus) && AsyncReceivedEvent)
        WaitForSingleObject(AsyncReceivedEvent, 10000);
    ClientResult.DisconnectStatus = NtAlpcDisconnectPort(ClientPort, 0);
    NtClose(ClientPort);
    return 0;
}

START_TEST(NtAlpcPort)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    ALPC_BASIC_INFORMATION BasicInformation;
    TEST_ALPC_MESSAGE Message;
    LARGE_INTEGER Timeout;
    ULONG ReturnLength;
    SIZE_T BufferLength;
    NTSTATUS Status;
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientThread = NULL;
    DWORD WaitStatus = WAIT_OBJECT_0;

    RtlZeroMemory(&PortAttributes, sizeof(PortAttributes));
    PortAttributes.SecurityQos.Length = sizeof(PortAttributes.SecurityQos);
    PortAttributes.SecurityQos.ImpersonationLevel = SecurityImpersonation;
    PortAttributes.SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    PortAttributes.SecurityQos.EffectiveOnly = TRUE;
    PortAttributes.MaxMessageLength = 0x1000;

    InitializeObjectAttributes(&ObjectAttributes, &PortName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtAlpcCreatePort(&ConnectionPort, &ObjectAttributes, &PortAttributes);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    AsyncReceivedEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    ok(AsyncReceivedEvent != NULL, "CreateEventW failed: %lu\n", GetLastError());
    if (!AsyncReceivedEvent)
        goto Cleanup;

    RtlFillMemory(&BasicInformation, sizeof(BasicInformation), 0x55);
    ReturnLength = 0x55555555;
    Status = NtAlpcQueryInformation(ConnectionPort, AlpcBasicInformation, &BasicInformation, sizeof(BasicInformation), &ReturnLength);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, sizeof(BasicInformation));
    ok(BasicInformation.PortContext == NULL, "PortContext is %p, expected NULL\n", BasicInformation.PortContext);

    RtlZeroMemory(&Message, sizeof(Message));
    BufferLength = sizeof(Message);
    Timeout.QuadPart = 0;
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Message.Header, &BufferLength, NULL, &Timeout);
    ok_hex(Status, STATUS_TIMEOUT);

    ClientThread = CreateThread(NULL, 0, AlpcClientThread, NULL, 0, NULL);
    ok(ClientThread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!ClientThread)
        goto Cleanup;

    RtlZeroMemory(&Message, sizeof(Message));
    BufferLength = sizeof(Message);
    Timeout = RelativeTimeout(10000);
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Message.Header, &BufferLength, NULL, &Timeout);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto WaitForClient;

    ok_eq_ulong(Message.Header.u2.s2.Type, TEST_LPC_CONTINUATION_REQUIRED | LPC_CONNECTION_REQUEST);
    ok_eq_ulong(Message.Cookie, TEST_CONNECT_COOKIE);
    ok_eq_ulong(Message.Value, 1);

    Status = NtAlpcAcceptConnectPort(&ServerPort, ConnectionPort, 0, NULL, NULL, TEST_PORT_CONTEXT, &Message.Header, NULL, TRUE);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto WaitForClient;

    RtlZeroMemory(&BasicInformation, sizeof(BasicInformation));
    ReturnLength = 0;
    Status = NtAlpcQueryInformation(ServerPort, AlpcBasicInformation, &BasicInformation, sizeof(BasicInformation), &ReturnLength);
    ok_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, sizeof(BasicInformation));
    ok(BasicInformation.PortContext == TEST_PORT_CONTEXT, "PortContext is %p, expected %p\n", BasicInformation.PortContext, TEST_PORT_CONTEXT);

    RtlZeroMemory(&Message, sizeof(Message));
    BufferLength = sizeof(Message);
    Timeout = RelativeTimeout(10000);
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Message.Header, &BufferLength, NULL, &Timeout);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto WaitForClient;

    ok_eq_ulong(Message.Header.u2.s2.Type, TEST_LPC_CONTINUATION_REQUIRED | LPC_REQUEST);
    ok_eq_ulong(Message.Cookie, TEST_REQUEST_COOKIE);
    ok_eq_ulong(Message.Value, 41);

    Message.Cookie = TEST_REPLY_COOKIE;
    Message.Value++;
    Status = NtAlpcSendWaitReceivePort(ServerPort, ALPC_MSGFLG_REPLY_MESSAGE, &Message.Header, NULL, NULL, NULL, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);

    RtlZeroMemory(&Message, sizeof(Message));
    BufferLength = sizeof(Message);
    Timeout = RelativeTimeout(10000);
    Status = NtAlpcSendWaitReceivePort(ConnectionPort, 0, NULL, NULL, &Message.Header, &BufferLength, NULL, &Timeout);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_ulong(Message.Header.u2.s2.Type, TEST_LPC_CONTINUATION_REQUIRED | LPC_REQUEST);
        ok_eq_ulong(Message.Cookie, TEST_ASYNC_COOKIE);
        ok_eq_ulong(Message.Value, 73);
        SetEvent(AsyncReceivedEvent);
    }

WaitForClient:
    if (AsyncReceivedEvent)
        SetEvent(AsyncReceivedEvent);
    WaitStatus = AlpcTestJoinThread(ClientThread, &ServerPort, &ConnectionPort, "core client");
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    if (WaitStatus == WAIT_OBJECT_0)
    {
        ok_hex(ClientResult.ConnectStatus, STATUS_SUCCESS);
        ok_eq_ulong(ClientResult.ConnectType, LPC_CONNECTION_REPLY);
        ok_eq_ulong(ClientResult.ConnectLength, sizeof(TEST_ALPC_MESSAGE));
        ok_hex(ClientResult.RequestStatus, STATUS_SUCCESS);
        ok_eq_ulong(ClientResult.ReplyCookie, TEST_REPLY_COOKIE);
        ok_eq_ulong(ClientResult.ReplyValue, 42);
        ok_hex(ClientResult.AsyncStatus, STATUS_SUCCESS);
        ok_hex(ClientResult.DisconnectStatus, STATUS_SUCCESS);
    }

Cleanup:
    if (ClientThread)
        CloseHandle(ClientThread);
    if (ServerPort)
    {
        Status = NtAlpcDisconnectPort(ServerPort, 0);
        ok_hex(Status, STATUS_SUCCESS);
        NtClose(ServerPort);
    }
    if (AsyncReceivedEvent && (!ClientThread || WaitStatus == WAIT_OBJECT_0))
    {
        CloseHandle(AsyncReceivedEvent);
        AsyncReceivedEvent = NULL;
    }
    else if (AsyncReceivedEvent)
    {
        trace("ALPC_OBSERVE thread core client event_quarantined=%p\n", AsyncReceivedEvent);
    }
    if (ConnectionPort)
        NtClose(ConnectionPort);
}
