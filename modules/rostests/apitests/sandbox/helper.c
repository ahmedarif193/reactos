/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Minimal sandbox mitigation target without User32/GDI32 imports
 */

#define WIN32_NO_STATUS
#include <windows.h>
#include <strsafe.h>
#include <string.h>
#include <stdlib.h>

#define CHILD_POLICY_NOT_REPORTED 0x001
#define CHILD_WIN32K_CALL_SUCCEEDED 0x002
#define CHILD_DYNAMIC_ALLOC_SUCCEEDED 0x004
#define CHILD_DYNAMIC_PROTECT_SUCCEEDED 0x008
#define CHILD_RW_ALLOC_FAILED 0x010
#define CHILD_PROCESS_CREATED 0x020
#define CHILD_SET_POLICY_FAILED 0x040
#define CHILD_POLICY_CLEARED 0x080
#define CHILD_WRONG_DENIAL_ERROR 0x100
#define CHILD_SIGNATURE_NOT_REPORTED 0x200
#define CHILD_UNSIGNED_DLL_LOADED 0x400
#define CHILD_SIGNED_DLL_BLOCKED 0x800
#define CHILD_WRONG_SIGNATURE_ERROR 0x1000
#define CHILD_API_MISSING 0x2000
#define CHILD_MITIGATION_NOT_ENFORCED 0x4000
#define CHILD_WRONG_MITIGATION_ERROR 0x8000

#define CHILD_UIPI_BLOCKED_POSTED 0x00010000
#define CHILD_UIPI_WRONG_ERROR 0x00020000
#define CHILD_UIPI_NULL_BLOCKED 0x00040000
#define CHILD_UIPI_GETTEXT_BLOCKED 0x00080000
#define CHILD_UIPI_WINDOW_ALLOW_BLOCKED 0x00100000
#define CHILD_UIPI_PROCESS_ALLOW_BLOCKED 0x00200000
#define CHILD_UIPI_WINDOW_DISALLOW_POSTED 0x00400000
#define CHILD_UIPI_THREAD_BLOCKED 0x00800000
#define CHILD_UIPI_NOTIFY_POSTED 0x01000000
#define CHILD_UIPI_SEND_SUCCEEDED 0x02000000
#define CHILD_UIPI_NO_WINDOW 0x04000000

#define BROKER_NO_THREAD_TOKEN 0x0001
#define BROKER_INITIAL_NOT_UNTRUSTED 0x0002
#define BROKER_REVERT_FAILED 0x0004
#define BROKER_PRIMARY_NOT_UNTRUSTED 0x0008
#define BROKER_PRIMARY_NOT_RESTRICTED 0x0010
#define BROKER_PARENT_VM_READ_OPENED 0x0020
#define BROKER_MEDIUM_EVENT_MODIFIED 0x0040
#define BROKER_LOW_EVENT_DENIED 0x0080
#define BROKER_PROCESS_CREATED 0x0100
#define BROKER_UIPI_POSTED 0x0200
#define BROKER_JOB_NOT_DETECTED 0x0400
#define BROKER_HANDLE_NOT_INHERITED 0x1000
#define BROKER_DESKTOP_MISMATCH 0x2000
#define BROKER_DYNAMIC_CODE_ALLOWED 0x4000
#define BROKER_UNLISTED_HANDLE_INHERITED 0x8000

#define WM_SBX_BROKER (WM_USER + 40)

#define STATUS_INVALID_SYSTEM_SERVICE_VALUE 0xC000001CUL

/* Newer PROCESS_MITIGATION_POLICY values absent from the build SDK. */
#define SBX_PROCESS_SIDE_CHANNEL_ISOLATION_POLICY ((PROCESS_MITIGATION_POLICY)14)
#define SBX_PROCESS_USER_SHADOW_STACK_POLICY      ((PROCESS_MITIGATION_POLICY)15)

static VOID
HelperFail(
    _Inout_ PDWORD Failures,
    _In_ DWORD Bit,
    _In_ PCSTR What,
    _In_ DWORD Error)
{
    CHAR Text[256];
    HANDLE Stream;
    DWORD Written;

    *Failures |= Bit;
    StringCchPrintfA(Text, ARRAYSIZE(Text),
                     "sandbox helper: bit 0x%08lx: %s (error %lu)\r\n",
                     Bit, What, Error);
    OutputDebugStringA(Text);
    Stream = GetStdHandle(STD_ERROR_HANDLE);
    if (Stream && Stream != INVALID_HANDLE_VALUE)
        WriteFile(Stream, Text, (DWORD)strlen(Text), &Written, NULL);
}

static BOOL
IsWin32kCallBlocked(VOID)
{
    typedef HANDLE (WINAPI *PGET_DC)(HWND);
    PGET_DC GetDc;
    HMODULE Module;
    HANDLE Dc;

    Module = LoadLibraryW(L"gdi32.dll");
    if (!Module)
        return GetLastError() == ERROR_DLL_INIT_FAILED || GetLastError() == ERROR_ACCESS_DENIED;
    FreeLibrary(Module);

    Module = LoadLibraryW(L"user32.dll");
    if (!Module)
        return GetLastError() == ERROR_DLL_INIT_FAILED || GetLastError() == ERROR_ACCESS_DENIED;
    GetDc = (PGET_DC)GetProcAddress(Module, "GetDC");
    if (!GetDc)
    {
        FreeLibrary(Module);
        return FALSE;
    }
    Dc = GetDc(NULL);
    FreeLibrary(Module);
    return Dc == NULL;
}

static DWORD
RunWin32kChild(
    _In_ BOOL Runtime)
{
    DWORD Failures = 0;
    PROCESS_MITIGATION_SYSTEM_CALL_DISABLE_POLICY Policy;

    if (Runtime)
    {
        ZeroMemory(&Policy, sizeof(Policy));
        Policy.DisallowWin32kSystemCalls = 1;
        if (!SetProcessMitigationPolicy(ProcessSystemCallDisablePolicy, &Policy, sizeof(Policy)))
            HelperFail(&Failures, CHILD_SET_POLICY_FAILED,
                       "SetProcessMitigationPolicy(SystemCallDisable)", GetLastError());
    }

    ZeroMemory(&Policy, sizeof(Policy));
    if (!GetProcessMitigationPolicy(GetCurrentProcess(),
                                    ProcessSystemCallDisablePolicy,
                                    &Policy,
                                    sizeof(Policy)) ||
        !Policy.DisallowWin32kSystemCalls)
    {
        HelperFail(&Failures, CHILD_POLICY_NOT_REPORTED,
                   "GetProcessMitigationPolicy(SystemCallDisable)", GetLastError());
    }

    if (!IsWin32kCallBlocked())
        HelperFail(&Failures, CHILD_WIN32K_CALL_SUCCEEDED,
                   "win32k call succeeded under lockdown", GetLastError());

    if (Runtime)
    {
        ZeroMemory(&Policy, sizeof(Policy));
        if (SetProcessMitigationPolicy(ProcessSystemCallDisablePolicy, &Policy, sizeof(Policy)))
            HelperFail(&Failures, CHILD_POLICY_CLEARED, "lockdown could be cleared", 0);
    }
    return Failures;
}

static DWORD
RunDynamicCodeChild(VOID)
{
    DWORD Failures = 0;
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY Policy;
    PVOID Memory;
    DWORD OldProtect;

    ZeroMemory(&Policy, sizeof(Policy));
    if (!GetProcessMitigationPolicy(GetCurrentProcess(),
                                    ProcessDynamicCodePolicy,
                                    &Policy,
                                    sizeof(Policy)) ||
        !Policy.ProhibitDynamicCode)
    {
        HelperFail(&Failures, CHILD_POLICY_NOT_REPORTED,
                   "GetProcessMitigationPolicy(DynamicCode)", GetLastError());
    }

    Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (Memory)
    {
        HelperFail(&Failures, CHILD_DYNAMIC_ALLOC_SUCCEEDED,
                   "VirtualAlloc(PAGE_EXECUTE_READWRITE) succeeded", 0);
        VirtualFree(Memory, 0, MEM_RELEASE);
    }

    Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!Memory)
    {
        HelperFail(&Failures, CHILD_RW_ALLOC_FAILED,
                   "VirtualAlloc(PAGE_READWRITE) failed", GetLastError());
    }
    else
    {
        if (VirtualProtect(Memory, 4096, PAGE_EXECUTE_READ, &OldProtect))
            HelperFail(&Failures, CHILD_DYNAMIC_PROTECT_SUCCEEDED,
                       "VirtualProtect(PAGE_EXECUTE_READ) succeeded", 0);
        VirtualFree(Memory, 0, MEM_RELEASE);
    }
    return Failures;
}

static VOID
CheckChildCreationDenied(
    _Inout_ PDWORD Failures,
    _Inout_ PWSTR CommandLine,
    _In_ PCSTR Description)
{
    PROCESS_INFORMATION Info;
    STARTUPINFOW Startup;
    DWORD Error;

    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    ZeroMemory(&Info, sizeof(Info));
    SetLastError(0xdeadbeef);
    if (CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                       NULL, NULL, &Startup, &Info))
    {
        HelperFail(Failures, CHILD_PROCESS_CREATED, Description, 0);
        TerminateProcess(Info.hProcess, 0);
        CloseHandle(Info.hThread);
        CloseHandle(Info.hProcess);
        return;
    }

    Error = GetLastError();
    if (Error != ERROR_CHILD_PROCESS_BLOCKED)
        HelperFail(Failures, CHILD_WRONG_DENIAL_ERROR, Description, Error);
}

static DWORD
RunChildPolicyChild(VOID)
{
    DWORD Failures = 0;
    PROCESS_MITIGATION_CHILD_PROCESS_POLICY Policy;
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH * 2];

    ZeroMemory(&Policy, sizeof(Policy));
    if (!GetProcessMitigationPolicy(GetCurrentProcess(),
                                    ProcessChildProcessPolicy,
                                    &Policy,
                                    sizeof(Policy)) ||
        !Policy.NoChildProcessCreation)
    {
        HelperFail(&Failures, CHILD_POLICY_NOT_REPORTED,
                   "GetProcessMitigationPolicy(ChildProcess)", GetLastError());
    }

    if (GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)) &&
        SUCCEEDED(StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine),
                                   L"\"%s\" Mitigations child noop", Application)))
    {
        CheckChildCreationDenied(&Failures, CommandLine, "sandbox helper creation was not blocked correctly");
    }

    if (GetSystemDirectoryW(Application, ARRAYSIZE(Application)) &&
        SUCCEEDED(StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine),
                                   L"\"%s\\cmd.exe\" /c exit 0", Application)))
    {
        CheckChildCreationDenied(&Failures, CommandLine, "cmd.exe creation was not blocked correctly");
    }
    return Failures;
}

static DWORD
RunSignatureChild(VOID)
{
    DWORD Failures = 0, Error;
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY Policy;
    WCHAR Path[MAX_PATH], *FileName;
    HMODULE Module;

    ZeroMemory(&Policy, sizeof(Policy));
    if (!GetProcessMitigationPolicy(GetCurrentProcess(), ProcessSignaturePolicy,
                                    &Policy, sizeof(Policy)) ||
        !Policy.MicrosoftSignedOnly)
    {
        HelperFail(&Failures, CHILD_SIGNATURE_NOT_REPORTED,
                   "GetProcessMitigationPolicy(Signature)", GetLastError());
    }

    if (!GetModuleFileNameW(NULL, Path, ARRAYSIZE(Path)))
        return Failures | CHILD_UNSIGNED_DLL_LOADED;
    FileName = wcsrchr(Path, L'\\');
    if (!FileName || FAILED(StringCchCopyW(FileName + 1,
                                           ARRAYSIZE(Path) - (FileName + 1 - Path),
                                           L"sandbox_unsigned.dll")))
        return Failures | CHILD_UNSIGNED_DLL_LOADED;

    SetLastError(0xdeadbeef);
    Module = LoadLibraryW(Path);
    Error = GetLastError();
    if (Module)
    {
        HelperFail(&Failures, CHILD_UNSIGNED_DLL_LOADED,
                   "unsigned DLL loaded under MicrosoftSignedOnly", 0);
        FreeLibrary(Module);
    }
    else if (Error != ERROR_INVALID_IMAGE_HASH)
    {
        HelperFail(&Failures, CHILD_WRONG_SIGNATURE_ERROR,
                   "unsigned DLL rejection error", Error);
    }

    if (!GetSystemDirectoryW(Path, ARRAYSIZE(Path)) ||
        FAILED(StringCchCatW(Path, ARRAYSIZE(Path), L"\\version.dll")))
        return Failures | CHILD_SIGNED_DLL_BLOCKED;
    Module = LoadLibraryW(Path);
    if (!Module)
        HelperFail(&Failures, CHILD_SIGNED_DLL_BLOCKED,
                   "Microsoft system DLL blocked", GetLastError());
    else
        FreeLibrary(Module);
    return Failures;
}

static DWORD
RunBasicCreationMitigationsChild(VOID)
{
    DWORD Failures = 0;
    PROCESS_MITIGATION_DEP_POLICY Dep;

    ZeroMemory(&Dep, sizeof(Dep));
    if (!GetProcessMitigationPolicy(GetCurrentProcess(), ProcessDEPPolicy,
                                    &Dep, sizeof(Dep)) ||
        !Dep.Enable || !Dep.Permanent)
    {
        HelperFail(&Failures, CHILD_POLICY_NOT_REPORTED,
                   "64-bit permanent DEP policy", GetLastError());
    }

    /* This is Chromium's post-startup path for MITIGATION_HEAP_TERMINATE. */
    if (!HeapSetInformation(NULL, HeapEnableTerminationOnCorruption, NULL, 0))
    {
        HelperFail(&Failures, CHILD_SET_POLICY_FAILED,
                   "HeapEnableTerminationOnCorruption", GetLastError());
    }
    return Failures;
}

static DWORD
RunDllSearchOrderChild(VOID)
{
    WCHAR Source[MAX_PATH], OriginalDirectory[MAX_PATH], TempPath[MAX_PATH];
    WCHAR TestDirectory[MAX_PATH], BeforePath[MAX_PATH], AfterPath[MAX_PATH];
    WCHAR *FileName;
    HMODULE Module;
    DWORD Failures = 0, Error;
    BOOL DirectoryCreated = FALSE, DirectoryChanged = FALSE;

    if (!GetModuleFileNameW(NULL, Source, ARRAYSIZE(Source)) ||
        !GetCurrentDirectoryW(ARRAYSIZE(OriginalDirectory), OriginalDirectory) ||
        !GetTempPathW(ARRAYSIZE(TempPath), TempPath))
    {
        HelperFail(&Failures, CHILD_API_MISSING,
                   "prepare DLL-search-order test", GetLastError());
        return Failures;
    }

    FileName = wcsrchr(Source, L'\\');
    if (!FileName ||
        FAILED(StringCchCopyW(FileName + 1,
                              ARRAYSIZE(Source) - (FileName + 1 - Source),
                              L"sandbox_unsigned.dll")) ||
        FAILED(StringCchPrintfW(TestDirectory, ARRAYSIZE(TestDirectory),
                                L"%sros-sbx-dll-%lu", TempPath,
                                GetCurrentProcessId())) ||
        FAILED(StringCchPrintfW(BeforePath, ARRAYSIZE(BeforePath),
                                L"%s\\sbx-current-before.dll", TestDirectory)) ||
        FAILED(StringCchPrintfW(AfterPath, ARRAYSIZE(AfterPath),
                                L"%s\\sbx-current-after.dll", TestDirectory)))
    {
        HelperFail(&Failures, CHILD_API_MISSING,
                   "build DLL-search-order paths", ERROR_INSUFFICIENT_BUFFER);
        return Failures;
    }

    RemoveDirectoryW(TestDirectory);
    if (!CreateDirectoryW(TestDirectory, NULL))
    {
        HelperFail(&Failures, CHILD_API_MISSING,
                   "create DLL-search-order directory", GetLastError());
        return Failures;
    }
    DirectoryCreated = TRUE;

    if (!CopyFileW(Source, BeforePath, FALSE) ||
        !CopyFileW(Source, AfterPath, FALSE) ||
        !SetCurrentDirectoryW(TestDirectory))
    {
        HelperFail(&Failures, CHILD_API_MISSING,
                   "stage DLL-search-order probes", GetLastError());
        goto Cleanup;
    }
    DirectoryChanged = TRUE;

    Module = LoadLibraryW(L"sbx-current-before.dll");
    if (!Module)
    {
        HelperFail(&Failures, CHILD_API_MISSING,
                   "standard search did not include current directory",
                   GetLastError());
    }
    else
    {
        FreeLibrary(Module);
    }

    if (!SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_DEFAULT_DIRS))
    {
        HelperFail(&Failures, CHILD_API_MISSING,
                   "SetDefaultDllDirectories", GetLastError());
        goto Cleanup;
    }

    SetLastError(0xdeadbeef);
    Module = LoadLibraryW(L"sbx-current-after.dll");
    Error = GetLastError();
    if (Module)
    {
        HelperFail(&Failures, CHILD_MITIGATION_NOT_ENFORCED,
                   "current-directory DLL loaded after search lockdown", 0);
        FreeLibrary(Module);
    }
    else if (Error != ERROR_MOD_NOT_FOUND)
    {
        HelperFail(&Failures, CHILD_WRONG_MITIGATION_ERROR,
                   "DLL-search lockdown rejection error", Error);
    }

Cleanup:
    if (DirectoryChanged)
        SetCurrentDirectoryW(OriginalDirectory);
    DeleteFileW(BeforePath);
    DeleteFileW(AfterPath);
    if (DirectoryCreated)
        RemoveDirectoryW(TestDirectory);
    return Failures;
}

static DWORD
RunKtmComponentFilterChild(VOID)
{
    typedef HANDLE (WINAPI *PCREATE_TRANSACTION_MANAGER)(
        LPSECURITY_ATTRIBUTES, LPWSTR, ULONG, ULONG);
    HMODULE KtmW32;
    PCREATE_TRANSACTION_MANAGER CreateTransactionManagerFn;
    HANDLE TransactionManager;
    DWORD Failures = 0, Error;

    KtmW32 = LoadLibraryW(L"ktmw32.dll");
    if (!KtmW32)
    {
        HelperFail(&Failures, CHILD_API_MISSING,
                   "LoadLibrary(ktmw32)", GetLastError());
        return Failures;
    }
    CreateTransactionManagerFn =
        (PCREATE_TRANSACTION_MANAGER)GetProcAddress(KtmW32,
                                                    "CreateTransactionManager");
    if (!CreateTransactionManagerFn)
    {
        HelperFail(&Failures, CHILD_API_MISSING,
                   "CreateTransactionManager lookup", GetLastError());
        FreeLibrary(KtmW32);
        return Failures;
    }

    SetLastError(0xdeadbeef);
    TransactionManager = CreateTransactionManagerFn(NULL, NULL, 1, 0);
    Error = GetLastError();
    if (TransactionManager != INVALID_HANDLE_VALUE)
    {
        HelperFail(&Failures, CHILD_MITIGATION_NOT_ENFORCED,
                   "KTM creation allowed under component filter", 0);
        if (TransactionManager)
            CloseHandle(TransactionManager);
    }
    else if (Error != ERROR_ACCESS_DENIED)
    {
        HelperFail(&Failures, CHILD_WRONG_MITIGATION_ERROR,
                   "KTM component-filter rejection error", Error);
    }

    FreeLibrary(KtmW32);
    return Failures;
}

static DWORD
RunFsctlMitigationChild(VOID)
{
    DWORD Failures = 0, Policy = 0;

    if (!GetProcessMitigationPolicy(GetCurrentProcess(),
                                    ProcessSystemCallDisablePolicy,
                                    &Policy, sizeof(Policy)) ||
        !(Policy & 0x4))
    {
        HelperFail(&Failures, CHILD_POLICY_NOT_REPORTED,
                   "FSCTL system-call-disable policy", GetLastError());
    }
    return Failures;
}

static DWORD
RunCoreSharingMitigationChild(VOID)
{
    DWORD Failures = 0, Policy = 0, Error;

    if (!GetProcessMitigationPolicy(GetCurrentProcess(),
                                    SBX_PROCESS_SIDE_CHANNEL_ISOLATION_POLICY,
                                    &Policy, sizeof(Policy)))
    {
        HelperFail(&Failures, CHILD_POLICY_NOT_REPORTED,
                   "side-channel isolation policy", GetLastError());
        return Failures;
    }

    if (!(Policy & 0x10))
    {
        Policy |= 0x10;
        SetLastError(ERROR_SUCCESS);
        if (SetProcessMitigationPolicy(SBX_PROCESS_SIDE_CHANNEL_ISOLATION_POLICY,
                                       &Policy, sizeof(Policy)))
        {
            HelperFail(&Failures, CHILD_SET_POLICY_FAILED,
                       "core-sharing creation policy was silently omitted", 0);
        }
        else
        {
            Error = GetLastError();
            if (Error != ERROR_NOT_SUPPORTED)
                HelperFail(&Failures, CHILD_SET_POLICY_FAILED,
                           "core-sharing unsupported result", Error);
        }
    }
    return Failures;
}

static DWORD
RunCetMitigationChild(_In_ ULONG Mode)
{
    DWORD Failures = 0, Policy = 0;

    if (!GetProcessMitigationPolicy(GetCurrentProcess(),
                                    SBX_PROCESS_USER_SHADOW_STACK_POLICY,
                                    &Policy, sizeof(Policy)))
    {
        HelperFail(&Failures, CHILD_POLICY_NOT_REPORTED,
                   "CET user-shadow-stack policy", GetLastError());
        return Failures;
    }

    if (Mode == 0 && (Policy & 0x1))
        HelperFail(&Failures, CHILD_POLICY_CLEARED,
                   "CET was not disabled", Policy);
    else if (Mode == 1 && (!(Policy & 0x1) || !(Policy & 0x10)))
        HelperFail(&Failures, CHILD_POLICY_NOT_REPORTED,
                   "CET strict mode", Policy);
    else if (Mode == 2 && (!(Policy & 0x1) || (Policy & 0x100)))
        HelperFail(&Failures, CHILD_POLICY_NOT_REPORTED,
                   "CET dynamic API relaxation", Policy);
    return Failures;
}

typedef BOOL (WINAPI *PHELPER_OPEN_THREAD_TOKEN)(HANDLE, DWORD, BOOL, PHANDLE);
typedef BOOL (WINAPI *PHELPER_OPEN_PROCESS_TOKEN)(HANDLE, DWORD, PHANDLE);
typedef BOOL (WINAPI *PHELPER_GET_TOKEN_INFORMATION)(HANDLE, TOKEN_INFORMATION_CLASS,
                                                     PVOID, DWORD, PDWORD);
typedef BOOL (WINAPI *PHELPER_IS_TOKEN_RESTRICTED)(HANDLE);
typedef BOOL (WINAPI *PHELPER_REVERT_TO_SELF)(VOID);
typedef HDESK (WINAPI *PHELPER_GET_THREAD_DESKTOP)(DWORD);
typedef BOOL (WINAPI *PHELPER_GET_USER_OBJECT_INFORMATION)(HANDLE, int, PVOID, DWORD, PDWORD);
typedef BOOL (WINAPI *PHELPER_POST_MESSAGE)(HWND, UINT, WPARAM, LPARAM);

static DWORD
GetTokenIntegrityRid(
    _In_ PHELPER_GET_TOKEN_INFORMATION GetTokenInformationFn,
    _In_ HANDLE Token)
{
    PTOKEN_MANDATORY_LABEL Label;
    PISID Sid;
    DWORD Length = 0, Rid = MAXDWORD;

    GetTokenInformationFn(Token, TokenIntegrityLevel, NULL, 0, &Length);
    Label = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Label ||
        !GetTokenInformationFn(Token, TokenIntegrityLevel, Label, Length, &Length))
    {
        if (Label)
            HeapFree(GetProcessHeap(), 0, Label);
        return MAXDWORD;
    }
    Sid = (PISID)Label->Label.Sid;
    if (Sid && Sid->SubAuthorityCount)
        Rid = Sid->SubAuthority[Sid->SubAuthorityCount - 1];
    HeapFree(GetProcessHeap(), 0, Label);
    return Rid;
}

static DWORD
RunBrokerChild(int argc, char **argv)
{
    DWORD Failures = 0, ParentPid, Length, Rid;
    HWND ParentWindow;
    HANDLE InheritedEvent, UnlistedEvent, UntrustedEvent, Token = NULL, Process, Event;
    BOOL InJob = FALSE;
    PVOID Memory;
    HDESK Desktop;
    WCHAR DesktopName[64], Application[MAX_PATH], CommandLine[MAX_PATH * 2];
    STARTUPINFOW Startup;
    PROCESS_INFORMATION Info;
    HMODULE Advapi32, User32;
    PHELPER_OPEN_THREAD_TOKEN OpenThreadTokenFn;
    PHELPER_OPEN_PROCESS_TOKEN OpenProcessTokenFn;
    PHELPER_GET_TOKEN_INFORMATION GetTokenInformationFn;
    PHELPER_IS_TOKEN_RESTRICTED IsTokenRestrictedFn;
    PHELPER_REVERT_TO_SELF RevertToSelfFn;
    PHELPER_GET_THREAD_DESKTOP GetThreadDesktopFn;
    PHELPER_GET_USER_OBJECT_INFORMATION GetUserObjectInformationFn;
    PHELPER_POST_MESSAGE PostMessageFn;

    if (argc < 9)
        return ERROR_INVALID_PARAMETER;
    ParentPid = strtoul(argv[4], NULL, 10);
    ParentWindow = (HWND)(ULONG_PTR)_strtoui64(argv[5], NULL, 10);
    InheritedEvent = (HANDLE)(ULONG_PTR)_strtoui64(argv[6], NULL, 10);
    UnlistedEvent = (HANDLE)(ULONG_PTR)_strtoui64(argv[7], NULL, 10);
    UntrustedEvent = (HANDLE)(ULONG_PTR)_strtoui64(argv[8], NULL, 10);

    Advapi32 = LoadLibraryW(L"advapi32.dll");
    User32 = LoadLibraryW(L"user32.dll");
    if (!Advapi32)
    {
        HelperFail(&Failures, BROKER_NO_THREAD_TOKEN,
                   "LoadLibrary(advapi32)", GetLastError());
        return Failures;
    }
    if (!User32)
    {
        HelperFail(&Failures, BROKER_DESKTOP_MISMATCH,
                   "LoadLibrary(user32)", GetLastError());
        FreeLibrary(Advapi32);
        return Failures;
    }

    OpenThreadTokenFn = (PHELPER_OPEN_THREAD_TOKEN)GetProcAddress(Advapi32, "OpenThreadToken");
    OpenProcessTokenFn = (PHELPER_OPEN_PROCESS_TOKEN)GetProcAddress(Advapi32, "OpenProcessToken");
    GetTokenInformationFn = (PHELPER_GET_TOKEN_INFORMATION)GetProcAddress(Advapi32, "GetTokenInformation");
    IsTokenRestrictedFn = (PHELPER_IS_TOKEN_RESTRICTED)GetProcAddress(Advapi32, "IsTokenRestricted");
    RevertToSelfFn = (PHELPER_REVERT_TO_SELF)GetProcAddress(Advapi32, "RevertToSelf");
    GetThreadDesktopFn = (PHELPER_GET_THREAD_DESKTOP)GetProcAddress(User32, "GetThreadDesktop");
    GetUserObjectInformationFn = (PHELPER_GET_USER_OBJECT_INFORMATION)GetProcAddress(User32, "GetUserObjectInformationW");
    PostMessageFn = (PHELPER_POST_MESSAGE)GetProcAddress(User32, "PostMessageW");
    if (!OpenThreadTokenFn || !OpenProcessTokenFn || !GetTokenInformationFn ||
        !IsTokenRestrictedFn || !RevertToSelfFn || !GetThreadDesktopFn ||
        !GetUserObjectInformationFn || !PostMessageFn)
    {
        HelperFail(&Failures, BROKER_NO_THREAD_TOKEN,
                   "broker API lookup", GetLastError());
        FreeLibrary(User32);
        FreeLibrary(Advapi32);
        return Failures;
    }

    if (!OpenThreadTokenFn(GetCurrentThread(), TOKEN_QUERY, FALSE, &Token))
    {
        HelperFail(&Failures, BROKER_NO_THREAD_TOKEN,
                   "no initial thread token", GetLastError());
    }
    else
    {
        Rid = GetTokenIntegrityRid(GetTokenInformationFn, Token);
        if (Rid != SECURITY_MANDATORY_UNTRUSTED_RID)
            HelperFail(&Failures, BROKER_INITIAL_NOT_UNTRUSTED,
                       "initial token integrity", Rid);
        CloseHandle(Token);
        Token = NULL;
    }

    Desktop = GetThreadDesktopFn(GetCurrentThreadId());
    if (!Desktop ||
        !GetUserObjectInformationFn(Desktop, UOI_NAME, DesktopName,
                                    sizeof(DesktopName), &Length) ||
        _wcsicmp(DesktopName, L"sbx_broker_desktop") != 0)
    {
        HelperFail(&Failures, BROKER_DESKTOP_MISMATCH,
                   "alternate desktop not applied", GetLastError());
    }

    Event = OpenEventW(EVENT_MODIFY_STATE, FALSE, L"Global\\sbx_broker_medium");
    if (Event)
    {
        HelperFail(&Failures, BROKER_MEDIUM_EVENT_MODIFIED,
                   "medium event opened for modify", 0);
        CloseHandle(Event);
    }
    if (!RevertToSelfFn())
        HelperFail(&Failures, BROKER_REVERT_FAILED,
                   "RevertToSelf", GetLastError());

    if (!OpenProcessTokenFn(GetCurrentProcess(), TOKEN_QUERY, &Token))
    {
        HelperFail(&Failures, BROKER_PRIMARY_NOT_UNTRUSTED,
                   "OpenProcessToken", GetLastError());
    }
    else
    {
        Rid = GetTokenIntegrityRid(GetTokenInformationFn, Token);
        if (Rid != SECURITY_MANDATORY_UNTRUSTED_RID)
            HelperFail(&Failures, BROKER_PRIMARY_NOT_UNTRUSTED,
                       "primary token integrity", Rid);
        if (!IsTokenRestrictedFn(Token))
            HelperFail(&Failures, BROKER_PRIMARY_NOT_RESTRICTED,
                       "primary token not restricted", 0);
        CloseHandle(Token);
    }

    Process = OpenProcess(PROCESS_VM_READ, FALSE, ParentPid);
    if (Process)
    {
        HelperFail(&Failures, BROKER_PARENT_VM_READ_OPENED,
                   "parent opened for VM_READ", 0);
        CloseHandle(Process);
    }
    Process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, ParentPid);
    if (Process)
        CloseHandle(Process);

    if (!SetEvent(InheritedEvent))
        HelperFail(&Failures, BROKER_HANDLE_NOT_INHERITED,
                   "inherited event handle unusable", GetLastError());
    if (!SetEvent(UntrustedEvent))
        HelperFail(&Failures, BROKER_LOW_EVENT_DENIED,
                   "broker IPC event handle unusable", GetLastError());

    SetLastError(0xdeadbeef);
    if (SetEvent(UnlistedEvent))
    {
        HelperFail(&Failures, BROKER_UNLISTED_HANDLE_INHERITED,
                   "handle outside HANDLE_LIST remained usable", 0);
    }
    else if (GetLastError() != ERROR_INVALID_HANDLE)
    {
        HelperFail(&Failures, BROKER_UNLISTED_HANDLE_INHERITED,
                   "unlisted handle failed with wrong error", GetLastError());
    }

    if (!IsProcessInJob(GetCurrentProcess(), NULL, &InJob) || !InJob)
        HelperFail(&Failures, BROKER_JOB_NOT_DETECTED,
                   "process not in sandbox job", GetLastError());

    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    ZeroMemory(&Info, sizeof(Info));
    if (GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)) &&
        SUCCEEDED(StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine),
                                   L"\"%s\" Broker child noop", Application)) &&
        CreateProcessW(NULL, CommandLine, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                       NULL, NULL, &Startup, &Info))
    {
        HelperFail(&Failures, BROKER_PROCESS_CREATED,
                   "child process creation allowed", 0);
        TerminateProcess(Info.hProcess, 0);
        CloseHandle(Info.hThread);
        CloseHandle(Info.hProcess);
    }

    Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE,
                          PAGE_EXECUTE_READWRITE);
    if (Memory)
    {
        HelperFail(&Failures, BROKER_DYNAMIC_CODE_ALLOWED,
                   "executable allocation allowed", 0);
        VirtualFree(Memory, 0, MEM_RELEASE);
    }

    if (ParentWindow && PostMessageFn(ParentWindow, WM_SBX_BROKER, 0, 0))
        HelperFail(&Failures, BROKER_UIPI_POSTED,
                   "UIPI allowed post to broker window", 0);
    if (ParentWindow)
        PostMessageFn(ParentWindow, WM_NULL, 0, 0);

    FreeLibrary(User32);
    FreeLibrary(Advapi32);
    return Failures;
}

static DWORD
RunUipiChild(int argc, char **argv)
{
    typedef BOOL (WINAPI *PIS_WINDOW)(HWND);
    typedef BOOL (WINAPI *PPOST_MESSAGE)(HWND, UINT, WPARAM, LPARAM);
    typedef LRESULT (WINAPI *PSEND_MESSAGE_TIMEOUT)(HWND, UINT, WPARAM, LPARAM,
                                                    UINT, UINT, PDWORD_PTR);
    typedef BOOL (WINAPI *PPOST_THREAD_MESSAGE)(DWORD, UINT, WPARAM, LPARAM);
    typedef BOOL (WINAPI *PSEND_NOTIFY_MESSAGE)(HWND, UINT, WPARAM, LPARAM);
    DWORD Failures = 0, TargetThread;
    DWORD_PTR Result;
    HWND Target;
    BOOL ExpectBlocked, Filtered, Posted;
    HMODULE User32;
    PIS_WINDOW IsWindowFn;
    PPOST_MESSAGE PostMessageFn;
    PSEND_MESSAGE_TIMEOUT SendMessageTimeoutFn;
    PPOST_THREAD_MESSAGE PostThreadMessageFn;
    PSEND_NOTIFY_MESSAGE SendNotifyMessageFn;

    if (argc < 8)
        return ERROR_INVALID_PARAMETER;
    Target = (HWND)(ULONG_PTR)_strtoui64(argv[4], NULL, 10);
    TargetThread = strtoul(argv[5], NULL, 10);
    ExpectBlocked = strtoul(argv[6], NULL, 10) != 0;
    Filtered = strtoul(argv[7], NULL, 10) != 0;

    User32 = LoadLibraryW(L"user32.dll");
    if (!User32)
    {
        DWORD Error = GetLastError();
        HelperFail(&Failures, CHILD_UIPI_NO_WINDOW,
                   "LoadLibrary(user32)", Error);
        return Failures | (Error & 0xFFFF);
    }
    IsWindowFn = (PIS_WINDOW)GetProcAddress(User32, "IsWindow");
    PostMessageFn = (PPOST_MESSAGE)GetProcAddress(User32, "PostMessageW");
    SendMessageTimeoutFn = (PSEND_MESSAGE_TIMEOUT)GetProcAddress(User32, "SendMessageTimeoutW");
    PostThreadMessageFn = (PPOST_THREAD_MESSAGE)GetProcAddress(User32, "PostThreadMessageW");
    SendNotifyMessageFn = (PSEND_NOTIFY_MESSAGE)GetProcAddress(User32, "SendNotifyMessageW");
    if (!IsWindowFn || !PostMessageFn || !SendMessageTimeoutFn ||
        !PostThreadMessageFn || !SendNotifyMessageFn || !IsWindowFn(Target))
    {
        HelperFail(&Failures, CHILD_UIPI_NO_WINDOW,
                   "target window or User32 exports", GetLastError());
        FreeLibrary(User32);
        return Failures;
    }

    SetLastError(0);
    Posted = PostMessageFn(Target, WM_USER + 1, 0, 0);
    if (ExpectBlocked)
    {
        if (Posted)
            HelperFail(&Failures, CHILD_UIPI_BLOCKED_POSTED,
                       "blocked message posted", 0);
        else if (GetLastError() != ERROR_ACCESS_DENIED)
            HelperFail(&Failures, CHILD_UIPI_WRONG_ERROR,
                       "blocked message error", GetLastError());
    }
    else if (!Posted)
    {
        HelperFail(&Failures, CHILD_UIPI_BLOCKED_POSTED,
                   "same-level message blocked", GetLastError());
    }

    if (!PostMessageFn(Target, WM_NULL, 0, 0))
        HelperFail(&Failures, CHILD_UIPI_NULL_BLOCKED,
                   "WM_NULL blocked", GetLastError());
    SetLastError(0);
    if (!SendMessageTimeoutFn(Target, WM_GETTEXTLENGTH, 0, 0,
                              SMTO_NORMAL, 5000, &Result))
        HelperFail(&Failures, CHILD_UIPI_GETTEXT_BLOCKED,
                   "WM_GETTEXTLENGTH blocked", GetLastError());
    SetLastError(0);
    if (SendMessageTimeoutFn(Target, WM_USER + 1, 0, 0,
                             SMTO_NORMAL, 5000, &Result) && ExpectBlocked)
        HelperFail(&Failures, CHILD_UIPI_SEND_SUCCEEDED,
                   "blocked SendMessageTimeout succeeded", 0);
    if (Filtered)
    {
        if (!PostMessageFn(Target, WM_USER + 2, 0, 0))
            HelperFail(&Failures, CHILD_UIPI_WINDOW_ALLOW_BLOCKED,
                       "window-allowed message blocked", GetLastError());
        if (!PostMessageFn(Target, WM_USER + 3, 0, 0))
            HelperFail(&Failures, CHILD_UIPI_PROCESS_ALLOW_BLOCKED,
                       "process-allowed message blocked", GetLastError());

        Posted = PostMessageFn(Target, WM_USER + 4, 0, 0);
        if ((ExpectBlocked && Posted) || (!ExpectBlocked && !Posted))
            HelperFail(&Failures, CHILD_UIPI_WINDOW_DISALLOW_POSTED,
                       "window-disallowed message result", GetLastError());
    }
    if (!PostThreadMessageFn(TargetThread, WM_USER + 5, 0, 0))
        HelperFail(&Failures, CHILD_UIPI_THREAD_BLOCKED,
                   "PostThreadMessage blocked", GetLastError());
    Posted = SendNotifyMessageFn(Target, WM_USER + 1, 0, 0);
    if (ExpectBlocked && Posted)
        HelperFail(&Failures, CHILD_UIPI_NOTIFY_POSTED,
                   "blocked SendNotifyMessage succeeded", 0);
    if (Filtered)
        PostMessageFn(Target, WM_USER + 6, 0, 0);
    FreeLibrary(User32);
    return Failures;
}

int
__cdecl
main(int argc, char **argv)
{
    PCSTR Mode;
    DWORD Failures = 0;

    if (argc >= 4 && !strcmp(argv[2], "child"))
        Mode = argv[3];
    else if (argc >= 2)
        Mode = argv[1];
    else
        return ERROR_INVALID_PARAMETER;

    if (!strcmp(Mode, "noop"))
        return 0;
    if (!strcmp(Mode, "win32k"))
        return RunWin32kChild(FALSE);
    if (!strcmp(Mode, "win32krt"))
        return RunWin32kChild(TRUE);
    if (!strcmp(Mode, "dyncode"))
        return RunDynamicCodeChild();
    if (!strcmp(Mode, "childpolicy"))
        return RunChildPolicyChild();
    if (!strcmp(Mode, "signature"))
        return RunSignatureChild();
    if (!strcmp(Mode, "basic"))
        return RunBasicCreationMitigationsChild();
    if (!strcmp(Mode, "dllsearch"))
        return RunDllSearchOrderChild();
    if (!strcmp(Mode, "ktm"))
        return RunKtmComponentFilterChild();
    if (!strcmp(Mode, "policy2"))
        return 0;
    if (!strcmp(Mode, "fsctl"))
        return RunFsctlMitigationChild();
    if (!strcmp(Mode, "core"))
        return RunCoreSharingMitigationChild();
    if (!strcmp(Mode, "cetoff"))
        return RunCetMitigationChild(0);
    if (!strcmp(Mode, "cetstrict"))
        return RunCetMitigationChild(1);
    if (!strcmp(Mode, "cetdynamic"))
        return RunCetMitigationChild(2);
    if (!strcmp(Mode, "broker"))
        return RunBrokerChild(argc, argv);
    if (!strcmp(Mode, "uipi"))
        return RunUipiChild(argc, argv);
    if (!strcmp(Mode, "combined"))
    {
        Failures |= RunWin32kChild(FALSE);
        Failures |= RunDynamicCodeChild();
        Failures |= RunChildPolicyChild();
        return Failures;
    }
    return ERROR_INVALID_PARAMETER;
}
