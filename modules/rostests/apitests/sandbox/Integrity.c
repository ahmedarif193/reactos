/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Mandatory integrity control on kernel objects, processes, threads and registry keys
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TORTURE_THREADS 4
#define TORTURE_ITERATIONS 200

static HANDLE
CreateTestEvent(PCWSTR Name, PCWSTR Sddl)
{
    SECURITY_ATTRIBUTES Sa = { sizeof(Sa), NULL, FALSE };
    HANDLE Event;

    Sa.lpSecurityDescriptor = Sddl ? SbxSdFromSddl(Sddl) : NULL;
    if (Sddl && !Sa.lpSecurityDescriptor)
    {
        ok(0, "SDDL '%S' failed %lu\n", Sddl, GetLastError());
        return NULL;
    }
    Event = CreateEventW(Sddl ? &Sa : NULL, TRUE, FALSE, Name);
    if (Sa.lpSecurityDescriptor) LocalFree(Sa.lpSecurityDescriptor);
    return Event;
}

static void
CheckOpenAs(DWORD Rid, PCWSTR Name, DWORD Access, BOOL ExpectSuccess, const char *Tag)
{
    HANDLE Token = SbxCreateIntegrityToken(Rid, TokenImpersonation);
    HANDLE Handle;
    DWORD Error;

    ok(Token != NULL, "%s: token creation failed %lu\n", Tag, GetLastError());
    if (!Token) return;
    ok(ImpersonateLoggedOnUser(Token), "%s: impersonation failed %lu\n", Tag, GetLastError());
    Handle = OpenEventW(Access, FALSE, Name);
    Error = GetLastError();
    RevertToSelf();
    if (ExpectSuccess)
    {
        ok(Handle != NULL, "%s: open 0x%lx failed %lu\n", Tag, Access, Error);
    }
    else
    {
        ok(Handle == NULL, "%s: open 0x%lx unexpectedly succeeded\n", Tag, Access);
        if (!Handle) ok(Error == ERROR_ACCESS_DENIED, "%s: error %lu\n", Tag, Error);
    }
    if (Handle) CloseHandle(Handle);
    CloseHandle(Token);
}

static ACCESS_MASK
MaximumAllowedAs(DWORD Rid, PCWSTR Name)
{
    HANDLE Token = SbxCreateIntegrityToken(Rid, TokenImpersonation);
    HANDLE Handle = NULL;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ObjectName;
    WCHAR Path[256];
    OBJECT_BASIC_INFORMATION Basic;
    NTSTATUS Status;
    ACCESS_MASK Granted = 0;

    if (!Token) return 0;
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Sessions\\%lu\\BaseNamedObjects\\%s",
                     NtCurrentPeb()->SessionId, Name);
    RtlInitUnicodeString(&ObjectName, Path);
    InitializeObjectAttributes(&Attributes, &ObjectName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    ok(ImpersonateLoggedOnUser(Token), "impersonation failed %lu\n", GetLastError());
    Status = NtOpenEvent(&Handle, MAXIMUM_ALLOWED, &Attributes);
    RevertToSelf();
    ok(NT_SUCCESS(Status), "NtOpenEvent(MAXIMUM_ALLOWED) failed 0x%lx\n", Status);
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryObject(Handle, ObjectBasicInformation, &Basic, sizeof(Basic), NULL);
        ok(NT_SUCCESS(Status), "NtQueryObject failed 0x%lx\n", Status);
        Granted = Basic.GrantedAccess;
        CloseHandle(Handle);
    }
    CloseHandle(Token);
    return Granted;
}

static void
TestUnlabeledObject(void)
{
    HANDLE Event = CreateTestEvent(L"sbx_mic_plain", NULL);
    DWORD Policy, Rid;

    ok(Event != NULL, "CreateEvent failed %lu\n", GetLastError());
    if (!Event) return;

    Rid = SbxQueryLabelRid(Event, SE_KERNEL_OBJECT, &Policy);
    ok(Rid == 0xFFFFFFFF, "medium-created object carries label 0x%lx\n", Rid);

    CheckOpenAs(SECURITY_MANDATORY_MEDIUM_RID, L"sbx_mic_plain", EVENT_ALL_ACCESS, TRUE, "medium all");
    CheckOpenAs(SECURITY_MANDATORY_HIGH_RID, L"sbx_mic_plain", EVENT_ALL_ACCESS, TRUE, "high all");
    CheckOpenAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_plain", EVENT_MODIFY_STATE, FALSE, "low modify");
    CheckOpenAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_plain", SYNCHRONIZE, TRUE, "low sync");
    CheckOpenAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_plain", READ_CONTROL, TRUE, "low readcontrol");
    CheckOpenAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_plain", DELETE, FALSE, "low delete");
    CheckOpenAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_plain", WRITE_DAC, FALSE, "low writedac");
    CheckOpenAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_plain", WRITE_OWNER, FALSE, "low writeowner");
    CheckOpenAs(SECURITY_MANDATORY_UNTRUSTED_RID, L"sbx_mic_plain", EVENT_MODIFY_STATE, FALSE, "untrusted modify");
    CheckOpenAs(SECURITY_MANDATORY_UNTRUSTED_RID, L"sbx_mic_plain", SYNCHRONIZE, TRUE, "untrusted sync");

    ok(MaximumAllowedAs(SECURITY_MANDATORY_MEDIUM_RID, L"sbx_mic_plain") & EVENT_MODIFY_STATE,
       "medium MAXIMUM_ALLOWED lost modify\n");
    ok(!(MaximumAllowedAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_plain") & (EVENT_MODIFY_STATE | DELETE | WRITE_DAC | WRITE_OWNER)),
       "low MAXIMUM_ALLOWED granted write rights\n");
    ok(MaximumAllowedAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_plain") & SYNCHRONIZE,
       "low MAXIMUM_ALLOWED lost synchronize\n");

    CloseHandle(Event);
}

static void
TestExplicitLabel(void)
{
    HANDLE Event = CreateTestEvent(L"sbx_mic_labeled", NULL);
    HANDLE LowToken;
    DWORD Error, Policy, Rid;

    ok(Event != NULL, "CreateEvent failed %lu\n", GetLastError());
    if (!Event) return;

    Error = SbxSetLabel(Event, SE_KERNEL_OBJECT, SECURITY_MANDATORY_LOW_RID, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP);
    ok(Error == ERROR_SUCCESS, "SetSecurityInfo(label low) failed %lu\n", Error);
    Rid = SbxQueryLabelRid(Event, SE_KERNEL_OBJECT, &Policy);
    ok(Rid == SECURITY_MANDATORY_LOW_RID, "label rid 0x%lx\n", Rid);
    ok(Policy == SYSTEM_MANDATORY_LABEL_NO_WRITE_UP, "label policy 0x%lx\n", Policy);

    CheckOpenAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_labeled", EVENT_MODIFY_STATE, TRUE, "low modify labeled");
    CheckOpenAs(SECURITY_MANDATORY_UNTRUSTED_RID, L"sbx_mic_labeled", EVENT_MODIFY_STATE, FALSE, "untrusted modify labeled");

    Error = SbxSetLabel(Event, SE_KERNEL_OBJECT, SECURITY_MANDATORY_LOW_RID, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP | SYSTEM_MANDATORY_LABEL_NO_READ_UP);
    ok(Error == ERROR_SUCCESS, "SetSecurityInfo(label low noread) failed %lu\n", Error);
    CheckOpenAs(SECURITY_MANDATORY_UNTRUSTED_RID, L"sbx_mic_labeled", SYNCHRONIZE, TRUE, "untrusted sync noread");
    CheckOpenAs(SECURITY_MANDATORY_UNTRUSTED_RID, L"sbx_mic_labeled", EVENT_QUERY_STATE, FALSE, "untrusted query noread");

    Error = SbxSetLabel(Event, SE_KERNEL_OBJECT, SECURITY_MANDATORY_HIGH_RID, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP);
    ok(Error == ERROR_SUCCESS || Error == ERROR_PRIVILEGE_NOT_HELD, "relabel high from medium: %lu\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        CheckOpenAs(SECURITY_MANDATORY_MEDIUM_RID, L"sbx_mic_labeled", EVENT_MODIFY_STATE, FALSE, "medium modify high-labeled");
        CheckOpenAs(SECURITY_MANDATORY_HIGH_RID, L"sbx_mic_labeled", EVENT_MODIFY_STATE, TRUE, "high modify high-labeled");
    }

    LowToken = SbxCreateIntegrityToken(SECURITY_MANDATORY_LOW_RID, TokenImpersonation);
    ok(LowToken != NULL, "low token failed %lu\n", GetLastError());
    if (LowToken)
    {
        ok(ImpersonateLoggedOnUser(LowToken), "impersonation failed %lu\n", GetLastError());
        Error = SbxSetLabel(Event, SE_KERNEL_OBJECT, SECURITY_MANDATORY_LOW_RID, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP);
        ok(Error == ERROR_SUCCESS, "low relabel to low failed %lu\n", Error);
        Error = SbxSetLabel(Event, SE_KERNEL_OBJECT, SECURITY_MANDATORY_MEDIUM_RID, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP);
        ok(Error == ERROR_INVALID_LABEL, "low relabel to medium: %lu\n", Error);
        RevertToSelf();
        CloseHandle(LowToken);
    }

    CloseHandle(Event);
}

static void
TestSddlLabelAtCreation(void)
{
    HANDLE Event = CreateTestEvent(L"sbx_mic_sddl", SBX_LABEL_LOW_SDDL);
    DWORD Policy, Rid;

    ok(Event != NULL, "CreateEvent(sddl) failed %lu\n", GetLastError());
    if (!Event) return;
    Rid = SbxQueryLabelRid(Event, SE_KERNEL_OBJECT, &Policy);
    ok(Rid == SECURITY_MANDATORY_LOW_RID, "sddl label rid 0x%lx\n", Rid);
    CheckOpenAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_sddl", EVENT_ALL_ACCESS, TRUE, "low all sddl");
    CheckOpenAs(SECURITY_MANDATORY_UNTRUSTED_RID, L"sbx_mic_sddl", EVENT_MODIFY_STATE, FALSE, "untrusted modify sddl");
    CloseHandle(Event);
}

static void
TestLowCreatorLabel(void)
{
    HANDLE LowToken = SbxCreateIntegrityToken(SECURITY_MANDATORY_LOW_RID, TokenImpersonation);
    HANDLE Event;
    DWORD Policy, Rid;

    ok(LowToken != NULL, "low token failed %lu\n", GetLastError());
    if (!LowToken) return;
    ok(ImpersonateLoggedOnUser(LowToken), "impersonation failed %lu\n", GetLastError());
    Event = CreateEventW(NULL, TRUE, FALSE, L"sbx_mic_lowcreated");
    RevertToSelf();
    ok(Event != NULL, "low CreateEvent failed %lu\n", GetLastError());
    if (Event)
    {
        Rid = SbxQueryLabelRid(Event, SE_KERNEL_OBJECT, &Policy);
        ok(Rid == SECURITY_MANDATORY_LOW_RID, "low-created object label 0x%lx\n", Rid);
        CheckOpenAs(SECURITY_MANDATORY_LOW_RID, L"sbx_mic_lowcreated", EVENT_ALL_ACCESS, TRUE, "low all lowcreated");
        CheckOpenAs(SECURITY_MANDATORY_UNTRUSTED_RID, L"sbx_mic_lowcreated", EVENT_MODIFY_STATE, FALSE, "untrusted modify lowcreated");
        CloseHandle(Event);
    }
    CloseHandle(LowToken);
}

static void
CheckProcessOpenAs(DWORD ProcessId, DWORD Rid, DWORD Access, BOOL ExpectSuccess, const char *Tag)
{
    HANDLE Token = SbxCreateIntegrityToken(Rid, TokenImpersonation);
    HANDLE Handle;
    DWORD Error;

    if (!Token) return;
    ok(ImpersonateLoggedOnUser(Token), "%s: impersonation failed %lu\n", Tag, GetLastError());
    Handle = OpenProcess(Access, FALSE, ProcessId);
    Error = GetLastError();
    RevertToSelf();
    if (ExpectSuccess)
        ok(Handle != NULL, "%s: OpenProcess 0x%lx failed %lu\n", Tag, Access, Error);
    else
        ok(Handle == NULL && Error == ERROR_ACCESS_DENIED, "%s: OpenProcess 0x%lx -> %p %lu\n", Tag, Access, Handle, Error);
    if (Handle) CloseHandle(Handle);
    CloseHandle(Token);
}

static void
CheckThreadOpenAs(DWORD ThreadId, DWORD Rid, DWORD Access, BOOL ExpectSuccess, const char *Tag)
{
    HANDLE Token = SbxCreateIntegrityToken(Rid, TokenImpersonation);
    HANDLE Handle;
    DWORD Error;

    if (!Token) return;
    ok(ImpersonateLoggedOnUser(Token), "%s: impersonation failed %lu\n", Tag, GetLastError());
    Handle = OpenThread(Access, FALSE, ThreadId);
    Error = GetLastError();
    RevertToSelf();
    if (ExpectSuccess)
        ok(Handle != NULL, "%s: OpenThread 0x%lx failed %lu\n", Tag, Access, Error);
    else
        ok(Handle == NULL && Error == ERROR_ACCESS_DENIED, "%s: OpenThread 0x%lx -> %p %lu\n", Tag, Access, Handle, Error);
    if (Handle) CloseHandle(Handle);
    CloseHandle(Token);
}

static void
TestProcessAndThreadObjects(void)
{
    DWORD Policy, Rid, Own;
    HANDLE OwnToken = SbxOpenToken(TOKEN_QUERY);
    PROCESS_INFORMATION Info;

    Own = OwnToken ? SbxGetTokenIntegrity(OwnToken) : SECURITY_MANDATORY_MEDIUM_RID;
    if (OwnToken) CloseHandle(OwnToken);
    Rid = SbxQueryLabelRid(GetCurrentProcess(), SE_KERNEL_OBJECT, &Policy);
    ok(Rid == Own, "process label 0x%lx (token 0x%lx)\n", Rid, Own);
    ok(Policy == (SYSTEM_MANDATORY_LABEL_NO_WRITE_UP | SYSTEM_MANDATORY_LABEL_NO_READ_UP), "process label policy 0x%lx\n", Policy);

    ok(SbxSpawnChild("Integrity", "mic-target", NULL, NULL, NULL, CREATE_SUSPENDED, &Info),
       "medium target creation failed %lu\n", GetLastError());
    if (!Info.hProcess)
        return;

    CheckProcessOpenAs(Info.dwProcessId, Own, PROCESS_ALL_ACCESS, TRUE, "same-level process all");
    CheckProcessOpenAs(Info.dwProcessId, SECURITY_MANDATORY_LOW_RID, PROCESS_VM_READ, FALSE, "low process vmread");
    CheckProcessOpenAs(Info.dwProcessId, SECURITY_MANDATORY_LOW_RID, PROCESS_VM_WRITE, FALSE, "low process vmwrite");
    CheckProcessOpenAs(Info.dwProcessId, SECURITY_MANDATORY_LOW_RID, PROCESS_TERMINATE, TRUE, "low process terminate");
    CheckProcessOpenAs(Info.dwProcessId, SECURITY_MANDATORY_LOW_RID, PROCESS_QUERY_INFORMATION, FALSE, "low process query");
    CheckProcessOpenAs(Info.dwProcessId, SECURITY_MANDATORY_LOW_RID, PROCESS_QUERY_LIMITED_INFORMATION, TRUE, "low process querylimited");
    CheckProcessOpenAs(Info.dwProcessId, SECURITY_MANDATORY_LOW_RID, SYNCHRONIZE, TRUE, "low process sync");
    CheckProcessOpenAs(Info.dwProcessId, SECURITY_MANDATORY_UNTRUSTED_RID, PROCESS_DUP_HANDLE, FALSE, "untrusted process duphandle");

    CheckThreadOpenAs(Info.dwThreadId, SECURITY_MANDATORY_LOW_RID, THREAD_GET_CONTEXT, FALSE, "low thread getcontext");
    CheckThreadOpenAs(Info.dwThreadId, SECURITY_MANDATORY_LOW_RID, THREAD_SET_CONTEXT, FALSE, "low thread setcontext");
    CheckThreadOpenAs(Info.dwThreadId, SECURITY_MANDATORY_LOW_RID, THREAD_QUERY_LIMITED_INFORMATION, TRUE, "low thread querylimited");

    TerminateProcess(Info.hProcess, 0);
    SbxWaitChild(&Info);
}

static void
TestRegistry(void)
{
    HANDLE Token = SbxCreateIntegrityToken(SECURITY_MANDATORY_LOW_RID, TokenImpersonation);
    HKEY Key = NULL;
    LONG Error;

    ok(Token != NULL, "low token failed %lu\n", GetLastError());
    if (!Token) return;
    ok(ImpersonateLoggedOnUser(Token), "impersonation failed %lu\n", GetLastError());
    Error = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_WRITE, &Key);
    ok(Error == ERROR_ACCESS_DENIED, "low RegOpenKeyEx(KEY_WRITE) -> %ld\n", Error);
    if (Error == ERROR_SUCCESS) RegCloseKey(Key);
    Key = NULL;
    Error = RegOpenKeyExW(HKEY_CURRENT_USER, L"Software", 0, KEY_READ, &Key);
    ok(Error == ERROR_SUCCESS, "low RegOpenKeyEx(KEY_READ) -> %ld\n", Error);
    if (Error == ERROR_SUCCESS) RegCloseKey(Key);
    RevertToSelf();
    CloseHandle(Token);
}

static LONG TortureFailures = 0;

static DWORD WINAPI
TortureThread(PVOID Context)
{
    HANDLE Token = SbxCreateIntegrityToken(SECURITY_MANDATORY_LOW_RID, TokenImpersonation);
    ULONG Iteration;
    WCHAR Name[64];
    HANDLE Event, Opened;

    if (!Token)
    {
        InterlockedIncrement(&TortureFailures);
        return 1;
    }
    StringCchPrintfW(Name, ARRAYSIZE(Name), L"sbx_mic_torture_%lu", (ULONG)(ULONG_PTR)Context);
    for (Iteration = 0; Iteration < TORTURE_ITERATIONS; Iteration++)
    {
        Event = CreateEventW(NULL, TRUE, FALSE, Name);
        if (!Event) { InterlockedIncrement(&TortureFailures); continue; }
        if (!ImpersonateLoggedOnUser(Token)) InterlockedIncrement(&TortureFailures);
        Opened = OpenEventW(EVENT_MODIFY_STATE, FALSE, Name);
        if (Opened) { InterlockedIncrement(&TortureFailures); CloseHandle(Opened); }
        Opened = OpenEventW(SYNCHRONIZE, FALSE, Name);
        if (!Opened) InterlockedIncrement(&TortureFailures); else CloseHandle(Opened);
        RevertToSelf();
        if (SbxSetLabel(Event, SE_KERNEL_OBJECT, SECURITY_MANDATORY_LOW_RID, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP) != ERROR_SUCCESS)
            InterlockedIncrement(&TortureFailures);
        if (!ImpersonateLoggedOnUser(Token)) InterlockedIncrement(&TortureFailures);
        Opened = OpenEventW(EVENT_MODIFY_STATE, FALSE, Name);
        if (!Opened) InterlockedIncrement(&TortureFailures); else CloseHandle(Opened);
        RevertToSelf();
        CloseHandle(Event);
    }
    CloseHandle(Token);
    return 0;
}

static void
TestTorture(void)
{
    HANDLE Threads[TORTURE_THREADS];
    ULONG Index;

    for (Index = 0; Index < TORTURE_THREADS; Index++)
        Threads[Index] = CreateThread(NULL, 0, TortureThread, (PVOID)(ULONG_PTR)Index, 0, NULL);
    for (Index = 0; Index < TORTURE_THREADS; Index++)
    {
        if (Threads[Index])
        {
            WaitForSingleObject(Threads[Index], 60000);
            CloseHandle(Threads[Index]);
        }
    }
    ok(TortureFailures == 0, "integrity torture failures: %ld\n", TortureFailures);
}

START_TEST(Integrity)
{
    TestUnlabeledObject();
    TestExplicitLabel();
    TestSddlLabelAtCreation();
    TestLowCreatorLabel();
    TestProcessAndThreadObjects();
    TestRegistry();
    TestTorture();
}
