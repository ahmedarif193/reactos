/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     End-to-end browser-style sandbox: lockdown token, initial token, job, labels, mitigations
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

#define CHILD_NO_THREAD_TOKEN 0x0001
#define CHILD_INITIAL_NOT_UNTRUSTED 0x0002
#define CHILD_REVERT_FAILED 0x0004
#define CHILD_PRIMARY_NOT_UNTRUSTED 0x0008
#define CHILD_PRIMARY_NOT_RESTRICTED 0x0010
#define CHILD_PARENT_VM_READ_OPENED 0x0020
#define CHILD_MEDIUM_EVENT_MODIFIED 0x0040
#define CHILD_LOW_EVENT_DENIED 0x0080
#define CHILD_PROCESS_CREATED 0x0100
#define CHILD_UIPI_POSTED 0x0200
#define CHILD_JOB_NOT_DETECTED 0x0400
#define CHILD_PARENT_SYNC_DENIED 0x0800
#define CHILD_HANDLE_NOT_INHERITED 0x1000
#define CHILD_DESKTOP_MISMATCH 0x2000
#define CHILD_DYNAMIC_CODE_ALLOWED 0x4000
#define CHILD_UNLISTED_HANDLE_INHERITED 0x8000

#define WM_SBX_BROKER (WM_USER + 40)
#define BROKER_SEQUENTIAL_RUNS 3
#define BROKER_CONCURRENT_RUNS 3

static HWND gBrokerWindow;
static LONG gBrokerMessages;

static LRESULT CALLBACK
BrokerWindowProc(HWND hwnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Msg == WM_SBX_BROKER)
    {
        InterlockedIncrement(&gBrokerMessages);
        return 0;
    }
    return DefWindowProcW(hwnd, Msg, wParam, lParam);
}

static HANDLE
CreateLockdownToken(HANDLE Base, PSID UniqueSid)
{
    SID_IDENTIFIER_AUTHORITY NullAuthority = SECURITY_NULL_SID_AUTHORITY;
    static SID OwnerRightsSid = {SID_REVISION, 1, {SECURITY_CREATOR_SID_AUTHORITY}, {SECURITY_CREATOR_OWNER_RIGHTS_RID}};
    PSID NullSid = NULL;
    SID_AND_ATTRIBUTES Restrict[2];
    HANDLE Token = NULL;

    if (!AllocateAndInitializeSid(&NullAuthority, 1, SECURITY_NULL_RID, 0, 0, 0, 0, 0, 0, 0, &NullSid))
        return NULL;
    Restrict[0].Sid = NullSid;
    Restrict[0].Attributes = 0;
    Restrict[1].Sid = UniqueSid;
    Restrict[1].Attributes = 0;
    if (CreateRestrictedToken(Base, DISABLE_MAX_PRIVILEGE | SANDBOX_INERT, 0, NULL, 0, NULL, 2, Restrict, &Token))
    {
        if (!SbxAddDefaultDaclSid(Token, UniqueSid, GENERIC_ALL) ||
            !SbxAddDefaultDaclSid(Token, &OwnerRightsSid, READ_CONTROL) ||
            !SbxSetTokenIntegrity(Token, SECURITY_MANDATORY_UNTRUSTED_RID))
        {
            CloseHandle(Token);
            Token = NULL;
        }
    }
    FreeSid(NullSid);
    return Token;
}

static HANDLE
CreateInitialToken(HANDLE Base, PSID UniqueSid)
{
    PTOKEN_GROUPS Groups = NULL;
    PTOKEN_USER User = NULL;
    PSID_AND_ATTRIBUTES Restricting = NULL;
    HANDLE Restricted = NULL, Token = NULL;
    DWORD Length = 0, Count = 0, Index;

    GetTokenInformation(Base, TokenGroups, NULL, 0, &Length);
    Groups = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Groups || !GetTokenInformation(Base, TokenGroups, Groups, Length, &Length)) goto Cleanup;
    GetTokenInformation(Base, TokenUser, NULL, 0, &Length);
    User = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!User || !GetTokenInformation(Base, TokenUser, User, Length, &Length)) goto Cleanup;
    Restricting = HeapAlloc(GetProcessHeap(), 0, (Groups->GroupCount + 1) * sizeof(*Restricting));
    if (!Restricting) goto Cleanup;
    Restricting[Count].Sid = User->User.Sid;
    Restricting[Count++].Attributes = 0;
    for (Index = 0; Index < Groups->GroupCount; ++Index)
    {
        if (Groups->Groups[Index].Attributes & (SE_GROUP_INTEGRITY | SE_GROUP_USE_FOR_DENY_ONLY))
            continue;
        Restricting[Count].Sid = Groups->Groups[Index].Sid;
        Restricting[Count++].Attributes = 0;
    }
    if (!CreateRestrictedToken(Base, 0, 0, NULL, 0, NULL, Count, Restricting, &Restricted)) goto Cleanup;
    if (!SbxAddDefaultDaclSid(Restricted, UniqueSid, GENERIC_ALL)) goto Cleanup;
    SbxSetTokenIntegrity(Restricted, SECURITY_MANDATORY_UNTRUSTED_RID);
    DuplicateTokenEx(Restricted, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &Token);

Cleanup:
    if (Restricted) CloseHandle(Restricted);
    if (Restricting) HeapFree(GetProcessHeap(), 0, Restricting);
    if (User) HeapFree(GetProcessHeap(), 0, User);
    if (Groups) HeapFree(GetProcessHeap(), 0, Groups);
    return Token;
}

static HANDLE
CreateSandboxJob(void)
{
    HANDLE Job = CreateJobObjectW(NULL, NULL);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Limits;
    JOBOBJECT_BASIC_UI_RESTRICTIONS Ui;

    if (!Job) return NULL;
    ZeroMemory(&Limits, sizeof(Limits));
    Limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION;
    Limits.BasicLimitInformation.ActiveProcessLimit = 1;
    ok(SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &Limits, sizeof(Limits)), "job limits failed %lu\n", GetLastError());
    Ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_ALL;
    ok(SetInformationJobObject(Job, JobObjectBasicUIRestrictions, &Ui, sizeof(Ui)), "job UI limits failed %lu\n", GetLastError());
    return Job;
}

static HANDLE
CreateEventWithSddl(PCWSTR Name, PCWSTR Sddl, BOOL Inherit)
{
    SECURITY_ATTRIBUTES Sa = { sizeof(Sa), NULL, Inherit };
    HANDLE Event;

    Sa.lpSecurityDescriptor = Sddl ? SbxSdFromSddl(Sddl) : NULL;
    Event = CreateEventW(&Sa, TRUE, FALSE, Name);
    if (Sa.lpSecurityDescriptor) LocalFree(Sa.lpSecurityDescriptor);
    return Event;
}

static DWORD
RunChild(DWORD ParentPid, HWND ParentWindow, HANDLE InheritedEvent, HANDLE UnlistedEvent)
{
    DWORD Failures = 0;
    HANDLE Token, Process, Event;
    BOOL InJob = FALSE;
    PROCESS_INFORMATION Info;
    PVOID Memory;
    HDESK Desktop;
    WCHAR DesktopName[64];
    DWORD Length;

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, FALSE, &Token))
        SbxChildFail(&Failures, CHILD_NO_THREAD_TOKEN, "no initial thread token", GetLastError());
    else
    {
        if (SbxGetTokenIntegrity(Token) != SECURITY_MANDATORY_UNTRUSTED_RID)
            SbxChildFail(&Failures, CHILD_INITIAL_NOT_UNTRUSTED, "initial token integrity", SbxGetTokenIntegrity(Token));
        CloseHandle(Token);
    }

    Desktop = GetThreadDesktop(GetCurrentThreadId());
    if (!Desktop || !GetUserObjectInformationW(Desktop, UOI_NAME, DesktopName, sizeof(DesktopName), &Length) ||
        _wcsicmp(DesktopName, L"sbx_broker_desktop") != 0)
        SbxChildFail(&Failures, CHILD_DESKTOP_MISMATCH, "alternate desktop not applied", GetLastError());

    if (!RevertToSelf())
        SbxChildFail(&Failures, CHILD_REVERT_FAILED, "RevertToSelf", GetLastError());

    Token = SbxOpenToken(TOKEN_QUERY);
    if (Token)
    {
        if (SbxGetTokenIntegrity(Token) != SECURITY_MANDATORY_UNTRUSTED_RID)
            SbxChildFail(&Failures, CHILD_PRIMARY_NOT_UNTRUSTED, "primary token integrity", SbxGetTokenIntegrity(Token));
        if (!IsTokenRestricted(Token))
            SbxChildFail(&Failures, CHILD_PRIMARY_NOT_RESTRICTED, "primary token not restricted", 0);
        CloseHandle(Token);
    }

    Process = OpenProcess(PROCESS_VM_READ, FALSE, ParentPid);
    if (Process) { SbxChildFail(&Failures, CHILD_PARENT_VM_READ_OPENED, "parent opened for VM_READ", 0); CloseHandle(Process); }
    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ParentPid);
    if (Process) CloseHandle(Process);

    Event = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Global\\sbx_broker_medium");
    if (Event) { SbxChildFail(&Failures, CHILD_MEDIUM_EVENT_MODIFIED, "medium event opened for modify", 0); CloseHandle(Event); }
    Event = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Global\\sbx_broker_untrusted");
    if (!Event) SbxChildFail(&Failures, CHILD_LOW_EVENT_DENIED, "untrusted-labeled event denied", GetLastError());
    else { SetEvent(Event); CloseHandle(Event); }

    if (InheritedEvent)
    {
        if (!SetEvent(InheritedEvent))
            SbxChildFail(&Failures, CHILD_HANDLE_NOT_INHERITED, "inherited event handle unusable", GetLastError());
    }

    if (UnlistedEvent)
    {
        SetLastError(0xdeadbeef);
        if (SetEvent(UnlistedEvent))
            SbxChildFail(&Failures, CHILD_UNLISTED_HANDLE_INHERITED,
                         "inheritable handle outside HANDLE_LIST remained usable", 0);
        else if (GetLastError() != ERROR_INVALID_HANDLE)
            SbxChildFail(&Failures, CHILD_UNLISTED_HANDLE_INHERITED,
                         "unlisted handle failed with the wrong error", GetLastError());
    }

    if (!IsProcessInJob(GetCurrentProcess(), NULL, &InJob) || !InJob)
        SbxChildFail(&Failures, CHILD_JOB_NOT_DETECTED, "not in job", GetLastError());

    if (SbxSpawnChild("Broker", "noop", NULL, NULL, NULL, CREATE_NO_WINDOW, &Info))
    {
        SbxChildFail(&Failures, CHILD_PROCESS_CREATED, "child process creation allowed", 0);
        SbxWaitChild(&Info);
    }

    Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (Memory) { SbxChildFail(&Failures, CHILD_DYNAMIC_CODE_ALLOWED, "executable allocation allowed", 0); VirtualFree(Memory, 0, MEM_RELEASE); }

    if (ParentWindow && PostMessageW(ParentWindow, WM_SBX_BROKER, 0, 0))
        SbxChildFail(&Failures, CHILD_UIPI_POSTED, "UIPI allowed a post to the broker window", 0);
    if (ParentWindow) PostMessageW(ParentWindow, WM_NULL, 0, 0);

    return Failures;
}

typedef struct _BROKER_RUN
{
    PROCESS_INFORMATION Info;
    HANDLE Job;
    HANDLE Inherited;
    HANDLE NotInherited;
    HANDLE Untrusted;
} BROKER_RUN, *PBROKER_RUN;

static BOOL
LaunchBroker(HANDLE Lockdown, HANDLE Initial, HANDLE SharedUntrusted,
             HDESK Desktop, PBROKER_RUN Run, ULONG Index)
{
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    SIZE_T Size = 0;
    ULONGLONG Mitigations = PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE |
                            PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON;
    DWORD ChildPolicy = PROCESS_CREATION_CHILD_PROCESS_RESTRICTED;
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH * 2], *FileName;
    WCHAR DesktopPath[] = L"WinSta0\\sbx_broker_desktop";
    STARTUPINFOEXW Startup;
    BOOL Success;
    HANDLE Inherit[2];

    ZeroMemory(Run, sizeof(*Run));
    Run->Job = CreateSandboxJob();
    Run->Inherited = CreateEventWithSddl(NULL, L"D:(A;;GA;;;WD)S:(ML;;NW;;;S-1-16-0)", TRUE);
    Run->NotInherited = CreateEventWithSddl(NULL, L"D:(A;;GA;;;WD)S:(ML;;NW;;;S-1-16-0)", TRUE);
    Run->Untrusted = SharedUntrusted;
    ok(Run->Job && Run->Inherited && Run->NotInherited, "job/event creation failed %lu\n", GetLastError());
    if (!Run->Job || !Run->Inherited || !Run->NotInherited) return FALSE;

    InitializeProcThreadAttributeList(NULL, 4, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Attributes || !InitializeProcThreadAttributeList(Attributes, 4, 0, &Size)) return FALSE;
    Inherit[0] = Run->Inherited;
    Inherit[1] = Run->Untrusted;
    Success = UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &Mitigations, sizeof(Mitigations), NULL, NULL) &&
              UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, &ChildPolicy, sizeof(ChildPolicy), NULL, NULL) &&
              UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, Inherit, sizeof(Inherit), NULL, NULL) &&
              UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &Run->Job, sizeof(Run->Job), NULL, NULL);
    ok(Success, "attribute setup failed %lu\n", GetLastError());

    if (!GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)) ||
        !(FileName = wcsrchr(Application, L'\\')) ||
        FAILED(StringCchCopyW(FileName + 1,
                              ARRAYSIZE(Application) - (FileName + 1 - Application),
                              L"sandbox_helper.exe")))
    {
        ok(0, "run %lu: helper path setup failed %lu\n", Index, GetLastError());
        DeleteProcThreadAttributeList(Attributes);
        HeapFree(GetProcessHeap(), 0, Attributes);
        return FALSE;
    }
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" Broker child broker %lu %Iu %Iu %Iu %Iu",
                     Application, GetCurrentProcessId(), (SIZE_T)(ULONG_PTR)gBrokerWindow,
                     (SIZE_T)(ULONG_PTR)Run->Inherited, (SIZE_T)(ULONG_PTR)Run->NotInherited,
                     (SIZE_T)(ULONG_PTR)Run->Untrusted);
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.StartupInfo.cb = sizeof(Startup);
    Startup.StartupInfo.lpDesktop = Desktop ? DesktopPath : NULL;
    Startup.lpAttributeList = Attributes;
    Success = CreateProcessAsUserW(Lockdown, Application, CommandLine, NULL, NULL, TRUE,
                                   CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW,
                                   NULL, NULL, &Startup.StartupInfo, &Run->Info);
    ok(Success, "run %lu: CreateProcessAsUser(lockdown) failed %lu\n", Index, GetLastError());
    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);
    if (!Success) return FALSE;

    ok(SetThreadToken(&Run->Info.hThread, Initial), "run %lu: SetThreadToken failed %lu\n", Index, GetLastError());
    ok(ResumeThread(Run->Info.hThread) == 1, "run %lu: ResumeThread failed %lu\n", Index, GetLastError());
    return TRUE;
}

static void
FinishBroker(PBROKER_RUN Run, ULONG Index)
{
    DWORD ExitCode;

    if (Run->Info.hProcess)
    {
        ExitCode = SbxWaitChild(&Run->Info);
        ok(ExitCode == 0, "run %lu: sandboxed child failed with 0x%lx\n", Index, ExitCode);
        ok(WaitForSingleObject(Run->Inherited, 0) == WAIT_OBJECT_0, "run %lu: inherited event not signaled\n", Index);
        ok(WaitForSingleObject(Run->NotInherited, 0) == WAIT_TIMEOUT,
           "run %lu: unlisted inheritable event was signaled\n", Index);
    }
    if (Run->Inherited) CloseHandle(Run->Inherited);
    if (Run->NotInherited) CloseHandle(Run->NotInherited);
    if (Run->Job) CloseHandle(Run->Job);
}

static void
PumpMessages(DWORD Milliseconds)
{
    MSG Msg;
    DWORD Start = GetTickCount();

    while (GetTickCount() - Start < Milliseconds)
    {
        while (PeekMessageW(&Msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Msg);
            DispatchMessageW(&Msg);
        }
        Sleep(20);
    }
}

START_TEST(Broker)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);
    HANDLE Base, Lockdown = NULL, Initial = NULL, Medium = NULL, Untrusted = NULL;
    PSID UniqueSid = NULL;
    LPWSTR UniqueSidString = NULL;
    HDESK Desktop = NULL;
    WCHAR UntrustedSddl[512];
    WNDCLASSW Class;
    BROKER_RUN Runs[BROKER_CONCURRENT_RUNS];
    ULONG Index;
    DWORD Error;
    SECURITY_IMPERSONATION_LEVEL ImpersonationLevel;
    DWORD Length;

    if (SbxIsChild(Arguments, Count, "noop")) ExitProcess(0);
    if (SbxIsChild(Arguments, Count, "target") && Count >= 8)
    {
        DWORD ParentPid = strtoul(Arguments[4], NULL, 10);
        HWND Window = (HWND)(ULONG_PTR)_strtoui64(Arguments[5], NULL, 10);
        HANDLE Inherited = (HANDLE)(ULONG_PTR)_strtoui64(Arguments[6], NULL, 10);
        HANDLE NotInherited = (HANDLE)(ULONG_PTR)_strtoui64(Arguments[7], NULL, 10);
        ExitProcess(RunChild(ParentPid, Window, Inherited, NotInherited));
    }

    Base = SbxOpenToken(TOKEN_ALL_ACCESS);
    ok(Base != NULL, "OpenProcessToken failed %lu\n", GetLastError());
    if (!Base) return;

    UniqueSid = SbxCreateUniqueSid();
    ok(UniqueSid != NULL, "unique SID allocation failed %lu\n", GetLastError());
    if (!UniqueSid) goto Cleanup;
    Lockdown = CreateLockdownToken(Base, UniqueSid);
    Initial = CreateInitialToken(Base, UniqueSid);
    ok(Lockdown != NULL, "lockdown token failed %lu\n", GetLastError());
    ok(Initial != NULL, "initial token failed %lu\n", GetLastError());
    if (!Lockdown || !Initial) goto Cleanup;
    ok(GetTokenInformation(Initial, TokenImpersonationLevel, &ImpersonationLevel,
                           sizeof(ImpersonationLevel), &Length) &&
       ImpersonationLevel == SecurityImpersonation,
       "initial token impersonation level %u (%lu)\n", ImpersonationLevel, GetLastError());

    Medium = CreateEventWithSddl(L"Global\\sbx_broker_medium", NULL, FALSE);
    if (ConvertSidToStringSidW(UniqueSid, &UniqueSidString) &&
        SUCCEEDED(StringCchPrintfW(UntrustedSddl, ARRAYSIZE(UntrustedSddl),
                                  L"D:(A;;GA;;;WD)(A;;GA;;;S-1-0-0)(A;;GA;;;%s)S:(ML;;NW;;;S-1-16-0)",
                                  UniqueSidString)))
    {
        Untrusted = CreateEventWithSddl(L"Global\\sbx_broker_untrusted",
                                        UntrustedSddl, TRUE);
    }
    ok(Medium && Untrusted, "broker events failed %lu\n", GetLastError());

    Desktop = CreateDesktopW(L"sbx_broker_desktop", NULL, NULL, 0, GENERIC_ALL, NULL);
    ok(Desktop != NULL, "CreateDesktop failed %lu\n", GetLastError());
    if (Desktop)
    {
        Error = SbxSetLabel(Desktop, SE_WINDOW_OBJECT, SECURITY_MANDATORY_UNTRUSTED_RID, SYSTEM_MANDATORY_LABEL_NO_WRITE_UP);
        ok(Error == ERROR_SUCCESS, "desktop label failed %lu\n", Error);
        ok(SbxQueryLabelRid(Desktop, SE_WINDOW_OBJECT, NULL) == SECURITY_MANDATORY_UNTRUSTED_RID, "desktop label not readable back\n");
    }

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = BrokerWindowProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"SbxBrokerWindow";
    RegisterClassW(&Class);
    gBrokerWindow = CreateWindowExW(0, L"SbxBrokerWindow", L"broker", WS_OVERLAPPED, 0, 0, 50, 50, NULL, NULL, Class.hInstance, NULL);
    ok(gBrokerWindow != NULL, "broker window failed %lu\n", GetLastError());

    for (Index = 0; Index < BROKER_SEQUENTIAL_RUNS; Index++)
    {
        ResetEvent(Untrusted);
        if (LaunchBroker(Lockdown, Initial, Untrusted, Desktop, &Runs[0], Index))
        {
            PumpMessages(500);
            FinishBroker(&Runs[0], Index);
            ok(WaitForSingleObject(Untrusted, 0) == WAIT_OBJECT_0, "run %lu: untrusted event not signaled by child\n", Index);
        }
        else
        {
            FinishBroker(&Runs[0], Index);
        }
    }

    for (Index = 0; Index < BROKER_CONCURRENT_RUNS; Index++)
        LaunchBroker(Lockdown, Initial, Untrusted, Desktop, &Runs[Index], 100 + Index);
    PumpMessages(1000);
    for (Index = 0; Index < BROKER_CONCURRENT_RUNS; Index++)
        FinishBroker(&Runs[Index], 100 + Index);

    PumpMessages(200);
    ok(gBrokerMessages == 0, "UIPI let %ld broker messages through\n", gBrokerMessages);

    if (gBrokerWindow) DestroyWindow(gBrokerWindow);
    UnregisterClassW(L"SbxBrokerWindow", Class.hInstance);

Cleanup:
    if (Desktop) CloseDesktop(Desktop);
    if (Medium) CloseHandle(Medium);
    if (Untrusted) CloseHandle(Untrusted);
    if (Initial) CloseHandle(Initial);
    if (Lockdown) CloseHandle(Lockdown);
    if (UniqueSid) FreeSid(UniqueSid);
    if (UniqueSidString) LocalFree(UniqueSidString);
    CloseHandle(Base);
}
