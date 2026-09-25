/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test for NtDuplicateObject
 * COPYRIGHT:   Copyright 2019 Thomas Faber (thomas.faber@reactos.org)
 */

#include "precomp.h"

#define OBJ_PROTECT_CLOSE 0x01

static DWORD WINAPI
TestRestrictedThread(PVOID Parameter)
{
    const ACCESS_MASK Access = THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT | THREAD_SUSPEND_RESUME;
    OBJECT_BASIC_INFORMATION Basic;
    THREAD_BASIC_INFORMATION ThreadBasic;
    OBJECT_ATTRIBUTES Attributes;
    CLIENT_ID ClientId = NtCurrentTeb()->ClientId;
    HANDLE Handle = NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Parameter);

    Status = NtQueryObject(NtCurrentThread(), ObjectBasicInformation, &Basic, sizeof(Basic), NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok_hex(Basic.GrantedAccess & Access, Access);

    Status = NtDuplicateObject(NtCurrentProcess(), NtCurrentThread(), NtCurrentProcess(),
                               &Handle, Access, 0, 0);
    ok_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryInformationThread(Handle, ThreadBasicInformation, &ThreadBasic, sizeof(ThreadBasic), NULL);
        ok_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            ok(ThreadBasic.ClientId.UniqueThread == ClientId.UniqueThread, "Unexpected thread ID %p\n", ThreadBasic.ClientId.UniqueThread);
        NtClose(Handle);
    }

    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    Status = NtOpenThread(&Handle, THREAD_GET_CONTEXT, &Attributes, &ClientId);
    ok_hex(Status, STATUS_ACCESS_DENIED);
    if (NT_SUCCESS(Status)) NtClose(Handle);
    return 0;
}

static VOID
TestCurrentThreadAccess(VOID)
{
    SECURITY_DESCRIPTOR Descriptor;
    SECURITY_ATTRIBUTES Attributes;
    ACL Dacl;
    BOOLEAN DebugEnabled = FALSE;
    NTSTATUS Status, PrivilegeStatus;
    HANDLE Thread;
    DWORD Wait;
    UINT Iteration;

    if (LOBYTE(LOWORD(GetVersion())) < 6)
    {
        skip("Current-thread pseudo handle access changed in Vista\n");
        return;
    }

    Status = RtlCreateSecurityDescriptor(&Descriptor, SECURITY_DESCRIPTOR_REVISION);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = RtlCreateAcl(&Dacl, sizeof(Dacl), ACL_REVISION);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;
    Status = RtlSetDaclSecurityDescriptor(&Descriptor, TRUE, &Dacl, FALSE);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    Attributes.nLength = sizeof(Attributes);
    Attributes.lpSecurityDescriptor = &Descriptor;
    Attributes.bInheritHandle = FALSE;
    PrivilegeStatus = RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, FALSE, FALSE, &DebugEnabled);

    for (Iteration = 0; Iteration < 16; ++Iteration)
    {
        Thread = CreateThread(&Attributes, 0, TestRestrictedThread, NULL, 0, NULL);
        ok(Thread != NULL, "CreateThread failed: %lu\n", GetLastError());
        if (!Thread) break;
        Wait = WaitForSingleObject(Thread, 2000);
        NtClose(Thread);
        ok(Wait == WAIT_OBJECT_0, "Thread wait returned %lu\n", Wait);
        if (Wait != WAIT_OBJECT_0) break;
    }

    if (NT_SUCCESS(PrivilegeStatus))
        RtlAdjustPrivilege(SE_DEBUG_PRIVILEGE, DebugEnabled, FALSE, &DebugEnabled);
}

START_TEST(NtDuplicateObject)
{
    NTSTATUS Status;
    HANDLE Handle;

    Handle = NULL;
    Status = NtDuplicateObject(NtCurrentProcess(),
                               NtCurrentProcess(),
                               NtCurrentProcess(),
                               &Handle,
                               GENERIC_ALL,
                               OBJ_PROTECT_CLOSE,
                               0);
    ok_hex(Status, STATUS_SUCCESS);
    ok(Handle != NULL && Handle != NtCurrentProcess(),
        "Handle = %p\n", Handle);
    Status = NtClose(Handle);
    ok_hex(Status, STATUS_HANDLE_NOT_CLOSABLE);

    TestCurrentThreadAccess();
}
