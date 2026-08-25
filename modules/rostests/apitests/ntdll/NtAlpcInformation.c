/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         ALPC query/set information parity and access tests
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF
 */

#include "precomp.h"
#include "alpc_test_utils.h"

typedef struct _ALPC_WAIT_REFERENCES_CONTEXT
{
    HANDLE Port;
    ULONG ReferenceCount;
    ULONG ReturnLength;
    NTSTATUS Status;
} ALPC_WAIT_REFERENCES_CONTEXT, *PALPC_WAIT_REFERENCES_CONTEXT;

static DWORD WINAPI AlpcWaitForReferencesThread(_In_ PVOID Parameter);

static
PSID
AlpcTestGetCurrentUserSid(
    _Out_writes_bytes_(BufferSize) UCHAR *Buffer,
    _In_ ULONG BufferSize)
{
    PTOKEN_USER UserInformation = (PTOKEN_USER)Buffer;
    HANDLE Token = NULL;
    DWORD Required = 0;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
        return NULL;
    if (!GetTokenInformation(Token, TokenUser, Buffer, BufferSize, &Required))
    {
        CloseHandle(Token);
        return NULL;
    }
    CloseHandle(Token);
    return UserInformation->User.Sid;
}

static
VOID
AlpcTestBasicInformationLengths(
    _In_ HANDLE Port)
{
    UCHAR Buffer[sizeof(ALPC_BASIC_INFORMATION) + 16];
    UCHAR Before[sizeof(Buffer)];
    ULONG ReturnLength;
    NTSTATUS Status;

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    RtlCopyMemory(Before, Buffer, sizeof(Buffer));
    ReturnLength = 0x55555555;
    alpc_observe_status("Query.Basic.short", NtAlpcQueryInformation(Port, AlpcBasicInformation, Buffer, sizeof(ALPC_BASIC_INFORMATION) - 1, &ReturnLength));
    trace("ALPC_OBSERVE value Query.Basic.short_return_length=%lu\n", ReturnLength);
    alpc_trace_scalar_mutation("Query.Basic.short", "return_length", 0x55555555, ReturnLength);
    AlpcTestTraceBufferMutation("Query.Basic.short", Before, Buffer, sizeof(Buffer));

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    RtlCopyMemory(Before, Buffer, sizeof(Buffer));
    ReturnLength = 0x55555555;
    alpc_expect_status("Query.Basic.exact", NtAlpcQueryInformation(Port, AlpcBasicInformation, Buffer, sizeof(ALPC_BASIC_INFORMATION), &ReturnLength), STATUS_SUCCESS);
    ok_eq_ulong(ReturnLength, sizeof(ALPC_BASIC_INFORMATION));
    alpc_trace_scalar_mutation("Query.Basic.exact", "return_length", 0x55555555, ReturnLength);
    AlpcTestTraceBufferMutation("Query.Basic.exact", Before, Buffer, sizeof(Buffer));

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    RtlCopyMemory(Before, Buffer, sizeof(Buffer));
    ReturnLength = 0x55555555;
    alpc_observe_status("Query.Basic.oversized", NtAlpcQueryInformation(Port, AlpcBasicInformation, Buffer, sizeof(Buffer), &ReturnLength));
    trace("ALPC_OBSERVE value Query.Basic.oversized_return_length=%lu\n", ReturnLength);
    alpc_trace_scalar_mutation("Query.Basic.oversized", "return_length", 0x55555555, ReturnLength);
    AlpcTestTraceBufferMutation("Query.Basic.oversized", Before, Buffer, sizeof(Buffer));

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    alpc_observe_status("Query.Basic.null_return_length", NtAlpcQueryInformation(Port, AlpcBasicInformation, Buffer, sizeof(ALPC_BASIC_INFORMATION), NULL));
}

static
VOID
AlpcTestConnectedSid(
    _In_ HANDLE Port)
{
    UCHAR TokenBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    UCHAR WrongSidBuffer[SECURITY_MAX_SID_SIZE];
    PSID CurrentSid;
    PSID WrongSid = (PSID)WrongSidBuffer;
    DWORD SidSize = sizeof(WrongSidBuffer);
    ULONG ReturnLength;
    NTSTATUS Status;

    CurrentSid = AlpcTestGetCurrentUserSid(TokenBuffer, sizeof(TokenBuffer));
    ok(CurrentSid != NULL, "failed to query current user SID: %lu\n", GetLastError());
    if (CurrentSid)
    {
        ReturnLength = 0x55555555;
        alpc_observe_status("Query.ConnectedSID.match", NtAlpcQueryInformation(Port, AlpcConnectedSIDInformation, CurrentSid, RtlLengthSid(CurrentSid), &ReturnLength));
        trace("ALPC_OBSERVE value Query.ConnectedSID.match_return_length=%lu\n", ReturnLength);
        alpc_trace_scalar_mutation("Query.ConnectedSID.match", "return_length", 0x55555555, ReturnLength);
    }

    if (CreateWellKnownSid(WinWorldSid, NULL, WrongSid, &SidSize))
    {
        ReturnLength = 0x55555555;
        alpc_observe_status("Query.ConnectedSID.mismatch", NtAlpcQueryInformation(Port, AlpcConnectedSIDInformation, WrongSid, RtlLengthSid(WrongSid), &ReturnLength));
        trace("ALPC_OBSERVE value Query.ConnectedSID.mismatch_return_length=%lu\n", ReturnLength);
        alpc_trace_scalar_mutation("Query.ConnectedSID.mismatch", "return_length", 0x55555555, ReturnLength);
    }
}

static
VOID
AlpcTestWaitForReferences(
    _In_ HANDLE Port)
{
    PALPC_WAIT_REFERENCES_CONTEXT Context;
    HANDLE Thread;
    DWORD WaitStatus;
    DWORD FinalWaitStatus;
    BOOLEAN ForcedTermination = FALSE;
    NTSTATUS Status;

    Context = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    ok(Context != NULL, "wait-for-references context allocation failed\n");
    if (!Context)
        return;
    Status = NtDuplicateObject(NtCurrentProcess(), Port, NtCurrentProcess(), &Context->Port, 0, 0, DUPLICATE_SAME_ACCESS);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
        return;
    }
    Context->ReferenceCount = 0;
    Context->ReturnLength = 0x55555555;
    Context->Status = STATUS_UNSUCCESSFUL;

    Thread = CreateThread(NULL, 0, AlpcWaitForReferencesThread, Context, 0, NULL);
    ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
    if (!Thread)
    {
        NtClose(Context->Port);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
        return;
    }

    WaitStatus = WaitForSingleObject(Thread, 2000);
    if (WaitStatus != WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE thread Query.WaitForReferences initial_wait=%lu forcing_port_rundown port=%p\n", WaitStatus, Context->Port);
        NtAlpcDisconnectPort(Context->Port, 0);
        NtClose(Context->Port);
        Context->Port = NULL;
        WaitStatus = WaitForSingleObject(Thread, 2000);
    }
    if (WaitStatus != WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE thread Query.WaitForReferences rundown_wait=%lu forcing_termination=1\n", WaitStatus);
        ForcedTermination = TRUE;
        ok(TerminateThread(Thread, STATUS_TIMEOUT), "Query.WaitForReferences TerminateThread failed: %lu\n", GetLastError());
        WaitStatus = WaitForSingleObject(Thread, 2000);
    }
    FinalWaitStatus = WaitForSingleObject(Thread, 0);
    ok_eq_ulong(FinalWaitStatus, WAIT_OBJECT_0);
    CloseHandle(Thread);
    if (FinalWaitStatus != WAIT_OBJECT_0)
    {
        trace("ALPC_OBSERVE thread Query.WaitForReferences context_quarantined=%p port=%p\n", Context, Context->Port);
        return;
    }
    if (ForcedTermination)
    {
        if (Context->Port)
            NtClose(Context->Port);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
        return;
    }

    trace("ALPC_NATIVE_STATUS Query.WaitForReferences.zero=%08lx\n", Context->Status);
    trace("ALPC_OBSERVE value Query.WaitForReferences reference_count=%lu return_length=%lu\n", Context->ReferenceCount, Context->ReturnLength);
    alpc_trace_scalar_mutation("Query.WaitForReferences.zero", "reference_count", 0, Context->ReferenceCount);
    alpc_trace_scalar_mutation("Query.WaitForReferences.zero", "return_length", 0x55555555, Context->ReturnLength);
    ok(Context->Status != STATUS_NOT_IMPLEMENTED, "Query.WaitForReferences.zero is still a syscall stub\n");
    if (Context->Port)
        NtClose(Context->Port);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Context);
}

static
DWORD
WINAPI
AlpcWaitForReferencesThread(
    _In_ PVOID Parameter)
{
    PALPC_WAIT_REFERENCES_CONTEXT Context = Parameter;

    Context->Status = NtAlpcQueryInformation(Context->Port, AlpcWaitForPortReferences, &Context->ReferenceCount, sizeof(Context->ReferenceCount), &Context->ReturnLength);
    return 0;
}

static
VOID
AlpcTestServerInformation(
    _In_ HANDLE Port)
{
    UCHAR Buffer[sizeof(ALPC_SERVER_INFORMATION) + 512];
    UCHAR Before[sizeof(Buffer)];
    PALPC_SERVER_INFORMATION Information = (PALPC_SERVER_INFORMATION)Buffer;
    ALPC_SERVER_SESSION_INFORMATION Session;
    ULONG ReturnLength;
    NTSTATUS Status;
    HANDLE ThreadHandle;

    ThreadHandle = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, GetCurrentThreadId());
    ok(ThreadHandle != NULL, "OpenThread failed: %lu\n", GetLastError());
    if (ThreadHandle)
    {
        RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
        Information->In.ThreadHandle = ThreadHandle;
        RtlCopyMemory(Before, Buffer, sizeof(Buffer));
        ReturnLength = 0x55555555;
        alpc_observe_status("Query.ServerInformation.null_port", NtAlpcQueryInformation(NULL, AlpcServerInformation, Buffer, sizeof(Buffer), &ReturnLength));
        AlpcTestTraceBufferMutation("Query.ServerInformation.null_port", Before, Buffer, sizeof(Buffer));
        alpc_trace_scalar_mutation("Query.ServerInformation.null_port", "return_length", 0x55555555, ReturnLength);
        trace("ALPC_OBSERVE value Query.ServerInformation blocked=%u pid=%p name_length=%u name_max=%u name_buffer=%p return_length=%lu\n", Information->Out.ThreadBlocked, Information->Out.ConnectedProcessId, Information->Out.ConnectionPortName.Length, Information->Out.ConnectionPortName.MaximumLength, Information->Out.ConnectionPortName.Buffer, ReturnLength);
        CloseHandle(ThreadHandle);
    }

    RtlFillMemory(&Session, sizeof(Session), 0x55);
    ReturnLength = 0x55555555;
    alpc_observe_status("Query.ServerSession.exact", NtAlpcQueryInformation(Port, AlpcServerSessionInformation, &Session, sizeof(Session), &ReturnLength));
    trace("ALPC_OBSERVE value Query.ServerSession session=%lu process=%lu return_length=%lu\n", Session.SessionId, Session.ProcessId, ReturnLength);
    alpc_trace_scalar_mutation("Query.ServerSession.exact", "return_length", 0x55555555, ReturnLength);

}

static
VOID
AlpcTestSetInformation(
    _In_ HANDLE Port)
{
    static ULONG_PTR ZoneStorage[PAGE_SIZE / sizeof(ULONG_PTR)];
    ALPC_PORT_MESSAGE_ZONE_INFORMATION Zone;
    ALPC_PORT_ATTRIBUTES Attributes;
    ALPC_BASIC_INFORMATION Basic;
    ULONG ReturnLength;
    NTSTATUS Status;

    AlpcTestInitializePortAttributes(&Attributes, ALPC_PORFLG_ALLOW_LPC_REQUESTS);
    alpc_observe_status("Set.PortInformation.enable_lpc", NtAlpcSetInformation(Port, AlpcPortInformation, &Attributes, sizeof(Attributes)));
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Basic, sizeof(Basic));
        ReturnLength = 0;
        alpc_expect_status("Set.PortInformation.query_enabled", NtAlpcQueryInformation(Port, AlpcBasicInformation, &Basic, sizeof(Basic), &ReturnLength), STATUS_SUCCESS);
        trace("ALPC_OBSERVE value Set.PortInformation.flags_after_enable=%08lx\n", Basic.Flags);
    }

    AlpcTestInitializePortAttributes(&Attributes, 0);
    alpc_observe_status("Set.PortInformation.disable_lpc", NtAlpcSetInformation(Port, AlpcPortInformation, &Attributes, sizeof(Attributes)));
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(&Basic, sizeof(Basic));
        ReturnLength = 0;
        alpc_expect_status("Set.PortInformation.query_disabled", NtAlpcQueryInformation(Port, AlpcBasicInformation, &Basic, sizeof(Basic), &ReturnLength), STATUS_SUCCESS);
        trace("ALPC_OBSERVE value Set.PortInformation.flags_after_disable=%08lx\n", Basic.Flags);
    }

    Zone.Buffer = ZoneStorage;
    Zone.Size = sizeof(ZoneStorage);
    alpc_observe_status("Set.MessageZone.exact", NtAlpcSetInformation(Port, AlpcMessageZoneInformation, &Zone, sizeof(Zone)));
}

static
VOID
AlpcTestAccessChecks(
    _In_ HANDLE Port)
{
    ALPC_PORT_ATTRIBUTES Attributes;
    ALPC_BASIC_INFORMATION Basic;
    ULONG ReturnLength = 0x55555555;
    NTSTATUS Status;
    HANDLE NoAccessPort = NULL;

    Status = NtDuplicateObject(NtCurrentProcess(), Port, NtCurrentProcess(), &NoAccessPort, 0, 0, 0);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlFillMemory(&Basic, sizeof(Basic), 0x55);
    alpc_observe_status("Access.Query.no_access_handle", NtAlpcQueryInformation(NoAccessPort, AlpcBasicInformation, &Basic, sizeof(Basic), &ReturnLength));
    AlpcTestInitializePortAttributes(&Attributes, 0);
    alpc_observe_status("Access.Set.no_access_handle", NtAlpcSetInformation(NoAccessPort, AlpcPortInformation, &Attributes, sizeof(Attributes)));
    NtClose(NoAccessPort);
}

START_TEST(NtAlpcInformation)
{
    static UNICODE_STRING PortName = RTL_CONSTANT_STRING(L"\\RPC Control\\NtdllApitestNtAlpcInformation");
    HANDLE ConnectionPort = NULL;
    HANDLE ServerPort = NULL;
    HANDLE ClientPort = NULL;
    NTSTATUS Status;

    alpc_expect_status("QueryPortInformationProcess.constant", NtQueryPortInformationProcess(), STATUS_WAIT_1);

    Status = AlpcTestCreateConnectedPorts(&PortName, 0, &ConnectionPort, &ServerPort, &ClientPort);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    AlpcTestBasicInformationLengths(ServerPort);
    AlpcTestConnectedSid(ServerPort);
    AlpcTestServerInformation(ServerPort);
    AlpcTestSetInformation(ServerPort);
    AlpcTestAccessChecks(ServerPort);
    AlpcTestWaitForReferences(ServerPort);

    AlpcTestCloseConnectedPorts(ConnectionPort, ServerPort, ClientPort);
}
