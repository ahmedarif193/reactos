/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Shared helpers for native and ReactOS ALPC parity tests
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#pragma once

#define ALPC_TEST_TIMEOUT_MS 10000
#define ALPC_TEST_CHILD_TIMEOUT_MS 30000
#define ALPC_TEST_PORT_CONTEXT ((PVOID)(ULONG_PTR)0x12345678)
#define ALPC_TEST_OBSERVATION_VARIABLE L"ALPC_TEST_NATIVE_OBSERVE"

typedef struct _ALPC_TEST_MESSAGE
{
    PORT_MESSAGE Header;
    ULONG Cookie;
    ULONG Value;
} ALPC_TEST_MESSAGE, *PALPC_TEST_MESSAGE;

typedef struct _ALPC_TEST_CONNECT_CONTEXT
{
    UNICODE_STRING PortName;
    ALPC_PORT_ATTRIBUTES PortAttributes;
    HANDLE ClientPort;
    NTSTATUS Status;
    SIZE_T MessageLength;
    ALPC_TEST_MESSAGE Message;
} ALPC_TEST_CONNECT_CONTEXT, *PALPC_TEST_CONNECT_CONTEXT;

typedef struct _ALPC_TEST_RESERVE_OUTPUT
{
    ULONG ResourceId;
    ULONG Guard;
} ALPC_TEST_RESERVE_OUTPUT, *PALPC_TEST_RESERVE_OUTPUT;

static
BOOLEAN
AlpcTestIsChildMode(
    _In_ PCSTR Mode)
{
    char **Arguments;
    int ArgumentCount;

    ArgumentCount = winetest_get_mainargs(&Arguments);
    return ArgumentCount >= 3 && !strcmp(Arguments[2], Mode);
}

static
BOOLEAN
AlpcTestRunIsolatedCase(
    _In_ PCWSTR TestName,
    _In_ PCWSTR Mode,
    _In_ DWORD TimeoutMilliseconds)
{
    WCHAR ApplicationName[MAX_PATH];
    WCHAR CommandLine[2 * MAX_PATH];
    STARTUPINFOW StartupInfo;
    PROCESS_INFORMATION ProcessInfo;
    HRESULT FormatStatus;
    DWORD ApplicationLength;
    DWORD WaitStatus;
    DWORD TerminationWaitStatus;
    DWORD ExitCode = STILL_ACTIVE;
    BOOL Terminated;
    BOOLEAN Success = FALSE;

    ApplicationLength = GetModuleFileNameW(NULL, ApplicationName, RTL_NUMBER_OF(ApplicationName));
    ok(ApplicationLength != 0 && ApplicationLength < RTL_NUMBER_OF(ApplicationName), "GetModuleFileNameW failed or truncated: %lu\n", GetLastError());
    if (!ApplicationLength || ApplicationLength >= RTL_NUMBER_OF(ApplicationName))
        return FALSE;

    FormatStatus = StringCchPrintfW(CommandLine, RTL_NUMBER_OF(CommandLine), L"\"%ls\" %ls %ls", ApplicationName, TestName, Mode);
    ok(SUCCEEDED(FormatStatus), "failed to format isolated ALPC command line: %08lx\n", FormatStatus);
    if (FAILED(FormatStatus))
        return FALSE;

    RtlZeroMemory(&StartupInfo, sizeof(StartupInfo));
    RtlZeroMemory(&ProcessInfo, sizeof(ProcessInfo));
    StartupInfo.cb = sizeof(StartupInfo);
    if (!CreateProcessW(ApplicationName, CommandLine, NULL, NULL, FALSE, 0, NULL, NULL, &StartupInfo, &ProcessInfo))
    {
        ok(FALSE, "CreateProcessW for %ls/%ls failed: %lu\n", TestName, Mode, GetLastError());
        return FALSE;
    }

    CloseHandle(ProcessInfo.hThread);
    WaitStatus = WaitForSingleObject(ProcessInfo.hProcess, TimeoutMilliseconds);
    if (WaitStatus != WAIT_OBJECT_0)
    {
        ok(FALSE, "isolated ALPC case %ls/%ls wait returned %lu\n", TestName, Mode, WaitStatus);
        Terminated = TerminateProcess(ProcessInfo.hProcess, STATUS_TIMEOUT);
        ok(Terminated, "TerminateProcess for %ls/%ls failed: %lu\n", TestName, Mode, GetLastError());
        TerminationWaitStatus = WaitForSingleObject(ProcessInfo.hProcess, 2000);
        ok_eq_ulong(TerminationWaitStatus, WAIT_OBJECT_0);
        goto Cleanup;
    }

    if (!GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode))
    {
        ok(FALSE, "GetExitCodeProcess for %ls/%ls failed: %lu\n", TestName, Mode, GetLastError());
        goto Cleanup;
    }
    ok_eq_ulong(ExitCode, 0);
    Success = ExitCode == 0;

Cleanup:
    CloseHandle(ProcessInfo.hProcess);
    return Success;
}

static
LARGE_INTEGER
AlpcTestRelativeTimeout(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -(LONGLONG)Milliseconds * 10000;
    return Timeout;
}

static
VOID
AlpcTestInitializeMessage(
    _Out_ PALPC_TEST_MESSAGE Message,
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
VOID
AlpcTestInitializePortAttributes(
    _Out_ PALPC_PORT_ATTRIBUTES Attributes,
    _In_ ULONG Flags)
{
    RtlZeroMemory(Attributes, sizeof(*Attributes));
    Attributes->Flags = Flags;
    Attributes->SecurityQos.Length = sizeof(Attributes->SecurityQos);
    Attributes->SecurityQos.ImpersonationLevel = SecurityImpersonation;
    Attributes->SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
    Attributes->SecurityQos.EffectiveOnly = TRUE;
    Attributes->MaxMessageLength = 0x1000;
    Attributes->MaxSectionSize = 0x100000;
    Attributes->MaxViewSize = 0x100000;
    Attributes->MaxTotalSectionSize = 0x200000;
    Attributes->DupObjectTypes = ALPC_PORFLG_OBJECT_TYPE_ALL_OBJECTS;
}

static
DWORD
AlpcTestJoinThread(
    _In_ HANDLE Thread,
    _In_opt_ PHANDLE WakePort1,
    _In_opt_ PHANDLE WakePort2,
    _In_ PCSTR Label)
{
    DWORD WaitStatus;

    WaitStatus = WaitForSingleObject(Thread, ALPC_TEST_TIMEOUT_MS + 2000);
    if (WaitStatus == WAIT_OBJECT_0)
        return WaitStatus;

    trace("ALPC_OBSERVE thread %s initial_wait=%lu forcing_port_rundown\n", Label, WaitStatus);
    if (WakePort1 && *WakePort1)
    {
        NtAlpcDisconnectPort(*WakePort1, 0);
        NtClose(*WakePort1);
        *WakePort1 = NULL;
    }
    if (WakePort2 && *WakePort2)
    {
        NtAlpcDisconnectPort(*WakePort2, 0);
        NtClose(*WakePort2);
        *WakePort2 = NULL;
    }

    WaitStatus = WaitForSingleObject(Thread, 2000);
    if (WaitStatus == WAIT_OBJECT_0)
        return WaitStatus;

    trace("ALPC_OBSERVE thread %s rundown_wait=%lu forcing_termination=1\n", Label, WaitStatus);
    if (!TerminateThread(Thread, STATUS_TIMEOUT))
    {
        ok(FALSE, "%s: TerminateThread failed: %lu\n", Label, GetLastError());
        return WaitStatus;
    }

    WaitStatus = WaitForSingleObject(Thread, 2000);
    ok_eq_ulong(WaitStatus, WAIT_OBJECT_0);
    return WAIT_TIMEOUT;
}

static
DWORD
WINAPI
AlpcTestConnectThread(
    _In_ PVOID Parameter)
{
    PALPC_TEST_CONNECT_CONTEXT Context = Parameter;
    LARGE_INTEGER Timeout;

    AlpcTestInitializeMessage(&Context->Message, 0x434F4E4E, 1);
    Context->MessageLength = sizeof(Context->Message);
    Timeout = AlpcTestRelativeTimeout(ALPC_TEST_TIMEOUT_MS);
    Context->Status = NtAlpcConnectPort(&Context->ClientPort, &Context->PortName, NULL, &Context->PortAttributes, ALPC_SYNC_CONNECTION, NULL, &Context->Message.Header, &Context->MessageLength, NULL, NULL, &Timeout);
    return 0;
}

static
NTSTATUS
AlpcTestCreateConnectedPorts(
    _In_ PUNICODE_STRING PortName,
    _In_ ULONG PortFlags,
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

    AlpcTestInitializePortAttributes(&Attributes, PortFlags);
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
    {
        Status = NtAlpcAcceptConnectPort(ServerPort, *ConnectionPort, 0, NULL, &Attributes, ALPC_TEST_PORT_CONTEXT, &Request.Header, NULL, TRUE);
    }

    WaitStatus = AlpcTestJoinThread(Thread, ConnectionPort, ServerPort, "connect helper");
    CloseHandle(Thread);
    if (!NT_SUCCESS(Status))
    {
        if (*ServerPort)
        {
            NtClose(*ServerPort);
            *ServerPort = NULL;
        }
    }
    else if (WaitStatus != WAIT_OBJECT_0)
    {
        Status = STATUS_TIMEOUT;
    }
    else
    {
        Status = Context->Status;
    }

    if (NT_SUCCESS(Status))
    {
        *ClientPort = Context->ClientPort;
    }
    else if (WaitStatus == WAIT_OBJECT_0 && Context->ClientPort)
    {
        NtClose(Context->ClientPort);
    }

    if (!NT_SUCCESS(Status))
    {
        if (*ServerPort)
        {
            NtClose(*ServerPort);
            *ServerPort = NULL;
        }
        if (*ConnectionPort)
        {
            NtClose(*ConnectionPort);
            *ConnectionPort = NULL;
        }
    }
    if (WaitStatus == WAIT_OBJECT_0)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
    else
        trace("ALPC_OBSERVE thread connect helper context_quarantined=%p\n", Context);
    return Status;
}

static
VOID
AlpcTestCloseConnectedPorts(
    _In_opt_ HANDLE ConnectionPort,
    _In_opt_ HANDLE ServerPort,
    _In_opt_ HANDLE ClientPort)
{
    if (ClientPort)
    {
        NtAlpcDisconnectPort(ClientPort, 0);
        NtClose(ClientPort);
    }
    if (ServerPort)
    {
        NtAlpcDisconnectPort(ServerPort, 0);
        NtClose(ServerPort);
    }
    if (ConnectionPort)
        NtClose(ConnectionPort);
}

static
BOOLEAN
AlpcTestNativeObservationEnabled(VOID)
{
    WCHAR Value[8];

    return GetEnvironmentVariableW(ALPC_TEST_OBSERVATION_VARIABLE, Value, RTL_NUMBER_OF(Value)) != 0;
}

static
VOID
AlpcTestTraceBufferMutation(
    _In_ PCSTR Label,
    _In_reads_bytes_(Length) const UCHAR *Before,
    _In_reads_bytes_(Length) const UCHAR *After,
    _In_ SIZE_T Length)
{
    SIZE_T First = Length;
    SIZE_T Changed = 0;
    SIZE_T Index;
    ULONG BeforeHash = 2166136261u;
    ULONG AfterHash = 2166136261u;

    for (Index = 0; Index < Length; ++Index)
    {
        BeforeHash = (BeforeHash ^ Before[Index]) * 16777619u;
        AfterHash = (AfterHash ^ After[Index]) * 16777619u;
        if (Before[Index] != After[Index])
        {
            if (First == Length)
                First = Index;
            ++Changed;
        }
    }

    trace("ALPC_OBSERVE mutation %s length=%Iu changed=%Iu first=%Iu before_hash=%08lx after_hash=%08lx\n", Label, Length, Changed, First, BeforeHash, AfterHash);
}

#define alpc_observe_status(Label, Expression) \
    do \
    { \
        Status = (Expression); \
        trace("ALPC_NATIVE_STATUS %s=%08lx\n", (Label), Status); \
        ok(Status != STATUS_NOT_IMPLEMENTED, "%s is still a syscall stub\n", (Label)); \
    } while (0)

#define alpc_expect_status(Label, Expression, Expected) \
    do \
    { \
        Status = (Expression); \
        trace("ALPC_ASSERT_STATUS %s=%08lx expected=%08lx\n", (Label), Status, (NTSTATUS)(Expected)); \
        ok_hex(Status, (Expected)); \
    } while (0)

#define alpc_native_observe_status(Label, Expression) \
    do \
    { \
        if (AlpcTestNativeObservationEnabled()) \
        { \
            Status = (Expression); \
            trace("ALPC_OBSERVE status %s=%08lx\n", (Label), Status); \
            ok(Status != STATUS_NOT_IMPLEMENTED, "%s is still a syscall stub\n", (Label)); \
        } \
        else \
        { \
            skip("set ALPC_TEST_NATIVE_OBSERVE=1 to run native observation %s\n", (Label)); \
        } \
    } while (0)

#define alpc_trace_scalar_mutation(Label, Field, Before, After) \
    trace("ALPC_OBSERVE mutation %s.%s changed=%u before=%I64x after=%I64x\n", (Label), (Field), (ULONG_PTR)(Before) != (ULONG_PTR)(After), (ULONGLONG)(ULONG_PTR)(Before), (ULONGLONG)(ULONG_PTR)(After))

#define alpc_observe_scalar_output(Label, Output, Expression) \
    do \
    { \
        ULONG_PTR AlpcTestOutputBefore = (ULONG_PTR)(Output); \
        Status = (Expression); \
        trace("ALPC_OBSERVE status %s=%08lx\n", (Label), Status); \
        alpc_trace_scalar_mutation((Label), "output", AlpcTestOutputBefore, (Output)); \
        ok(Status != STATUS_NOT_IMPLEMENTED, "%s is still a syscall stub\n", (Label)); \
    } while (0)
