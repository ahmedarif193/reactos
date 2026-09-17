/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     KernelBase sandbox surface: mitigation policies, token membership, AppContainer paths
 */

#include "precomp.h"
#include <pseh/pseh2.h>

#define CHILD_ASLR_MISMATCH 0x1
#define CHILD_EP_MISMATCH 0x2
#define CHILD_FONT_MISMATCH 0x4
#define CHILD_IMAGELOAD_MISMATCH 0x8
#define CHILD_DYNCODE_MISMATCH 0x10
#define CHILD_FONT_NOT_ENFORCED 0x20
#define CHILD_SYSTEM_FONT_BLOCKED 0x40
#define CHILD_APPINIT_LOADED 0x80
#define CHILD_APPINIT_NOT_LOADED 0x100
#define CHILD_POLICY_SET_FAILED 0x200
#define CHILD_POLICY_GET_MISMATCH 0x400
#define CHILD_POLICY_CLEAR_ALLOWED 0x800
#define CHILD_POLICY_INVALID_ACCEPTED 0x1000
#define CHILD_THREAD_OPTOUT_FAILED 0x2000
#define CHILD_CFG_SET_ACCEPTED 0x4000
#define CHILD_PREFER_MISMATCH 0x8000
#define CHILD_NO_FONT 0x10000
#define CHILD_FONT_TOGGLE_FAILED 0x20000
#define CHILD_STRICT_SET_FAILED 0x40000
#define CHILD_STRICT_QUERY_MISMATCH 0x80000
#define CHILD_STRICT_NO_EXCEPTION 0x100000
#define CHILD_STRICT_VALID_HANDLE_RAISED 0x200000
#define CHILD_STRICT_DISABLE_ALLOWED 0x400000

#define POLICY_ASLR_SET_FAILED 0x00000001
#define POLICY_ASLR_GET_FAILED 0x00000002
#define POLICY_EP_SET_FAILED 0x00000004
#define POLICY_EP_GET_FAILED 0x00000008
#define POLICY_EP_CLEAR_ALLOWED 0x00000010
#define POLICY_FONT_SET_FAILED 0x00000020
#define POLICY_FONT_GET_FAILED 0x00000040
#define POLICY_FONT_TOGGLE_FAILED 0x00000080
#define POLICY_IMAGE_SET_FAILED 0x00000100
#define POLICY_IMAGE_GET_FAILED 0x00000200
#define POLICY_IMAGE_INVALID_ACCEPTED 0x00000400
#define POLICY_PAYLOAD_SET_FAILED 0x00000800
#define POLICY_PAYLOAD_GET_FAILED 0x00001000
#define POLICY_SYSCALL_SET_FAILED 0x00002000
#define POLICY_SYSCALL_GET_FAILED 0x00004000
#define POLICY_CFG_SET_ACCEPTED 0x00008000
#define POLICY_DYNAMIC_SET_FAILED 0x00010000
#define POLICY_DYNAMIC_CODE_ALLOWED 0x00020000
#define POLICY_THREAD_OPTOUT_FAILED 0x00040000

#define APPINIT_KEY L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Windows"
#define APPINIT_DLL L"mscms.dll"

typedef BOOL (WINAPI *PFN_CheckTokenMembershipEx)(HANDLE, PSID, DWORD, PBOOL);
typedef BOOL (WINAPI *PFN_CheckTokenCapability)(HANDLE, PSID, PBOOL);
typedef BOOL (WINAPI *PFN_GetAppContainerNamedObjectPath)(HANDLE, PSID, ULONG, LPWSTR, PULONG);
typedef BOOL (WINAPI *PFN_SetThreadInformation)(HANDLE, THREAD_INFORMATION_CLASS, LPVOID, DWORD);
typedef BOOL (WINAPI *PFN_DeriveCapabilitySidsFromName)(LPCWSTR, PSID **, PDWORD, PSID **, PDWORD);
typedef HRESULT (WINAPI *PFN_AppContainerRegisterSid)(PSID, LPCWSTR, LPCWSTR);
typedef HRESULT (WINAPI *PFN_AppContainerUnregisterSid)(PSID);
typedef HRESULT (WINAPI *PFN_AppContainerLookupMoniker)(PSID, LPWSTR *);
typedef VOID (WINAPI *PFN_AppContainerFreeMemory)(PVOID);

static DWORD
GetPolicy(PROCESS_MITIGATION_POLICY Policy)
{
    DWORD Value = 0;
    if (!GetProcessMitigationPolicy(GetCurrentProcess(), Policy, &Value, sizeof(Value)))
        return 0xFFFFFFFF;
    return Value;
}

static BOOL
SetPolicy(PROCESS_MITIGATION_POLICY Policy, DWORD Value)
{
    return SetProcessMitigationPolicy(Policy, &Value, sizeof(Value));
}

static BOOL
FindSystemFont(LPWSTR Path, DWORD Length)
{
    WCHAR Pattern[MAX_PATH];
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    GetWindowsDirectoryW(Pattern, ARRAYSIZE(Pattern));
    StringCchCatW(Pattern, ARRAYSIZE(Pattern), L"\\Fonts\\*.ttf");
    Find = FindFirstFileW(Pattern, &Data);
    if (Find == INVALID_HANDLE_VALUE) return FALSE;
    GetWindowsDirectoryW(Path, Length);
    StringCchCatW(Path, Length, L"\\Fonts\\");
    StringCchCatW(Path, Length, Data.cFileName);
    FindClose(Find);
    return TRUE;
}

static BOOL
CopyFontToTemp(LPWSTR SystemFont, DWORD SystemLength, LPWSTR TempFont, DWORD TempLength)
{
    if (!FindSystemFont(SystemFont, SystemLength)) return FALSE;
    GetTempPathW(TempLength, TempFont);
    StringCchCatW(TempFont, TempLength, L"sbx_font_copy.ttf");
    return CopyFileW(SystemFont, TempFont, FALSE);
}

static DWORD
RunPolicyChild(void)
{
    DWORD Failures = 0, Bad = 0x100, ThreadPolicy = 1;
    PROCESS_MITIGATION_DYNAMIC_CODE_POLICY Dynamic;
    PFN_SetThreadInformation pSetThreadInformation;
    PVOID Memory;

    if (!SetPolicy(ProcessASLRPolicy, 1)) SbxChildFail(&Failures, POLICY_ASLR_SET_FAILED, "set ASLR", GetLastError());
    if (!(GetPolicy(ProcessASLRPolicy) & 1)) SbxChildFail(&Failures, POLICY_ASLR_GET_FAILED, "get ASLR", GetPolicy(ProcessASLRPolicy));

    if (!SetPolicy(ProcessExtensionPointDisablePolicy, 1)) SbxChildFail(&Failures, POLICY_EP_SET_FAILED, "set extension point", GetLastError());
    if (GetPolicy(ProcessExtensionPointDisablePolicy) != 1) SbxChildFail(&Failures, POLICY_EP_GET_FAILED, "get extension point", GetPolicy(ProcessExtensionPointDisablePolicy));
    if (SetPolicy(ProcessExtensionPointDisablePolicy, 0) || GetLastError() != ERROR_ACCESS_DENIED)
        SbxChildFail(&Failures, POLICY_EP_CLEAR_ALLOWED, "clear extension point", GetLastError());

    if (!SetPolicy(ProcessFontDisablePolicy, 1)) SbxChildFail(&Failures, POLICY_FONT_SET_FAILED, "set font", GetLastError());
    if (GetPolicy(ProcessFontDisablePolicy) != 1) SbxChildFail(&Failures, POLICY_FONT_GET_FAILED, "get font", GetPolicy(ProcessFontDisablePolicy));
    SetLastError(0xdeadbeef);
    if (SetPolicy(ProcessFontDisablePolicy, 0) || GetLastError() != ERROR_ACCESS_DENIED ||
        GetPolicy(ProcessFontDisablePolicy) != 1)
        SbxChildFail(&Failures, POLICY_FONT_TOGGLE_FAILED, "toggle font", GetLastError());

    if (!SetPolicy(ProcessImageLoadPolicy, 1)) SbxChildFail(&Failures, POLICY_IMAGE_SET_FAILED, "set image load", GetLastError());
    if (GetPolicy(ProcessImageLoadPolicy) != 1) SbxChildFail(&Failures, POLICY_IMAGE_GET_FAILED, "get image load", GetPolicy(ProcessImageLoadPolicy));
    if (SetProcessMitigationPolicy(ProcessImageLoadPolicy, &Bad, sizeof(Bad)) || GetLastError() != ERROR_INVALID_PARAMETER)
        SbxChildFail(&Failures, POLICY_IMAGE_INVALID_ACCEPTED, "invalid image load flags", GetLastError());

    SetLastError(0xdeadbeef);
    if (SetPolicy(ProcessPayloadRestrictionPolicy, 1) || GetLastError() != ERROR_INVALID_PARAMETER)
        SbxChildFail(&Failures, POLICY_PAYLOAD_SET_FAILED, "set payload", GetLastError());
    if (GetPolicy(ProcessPayloadRestrictionPolicy) != 0)
        SbxChildFail(&Failures, POLICY_PAYLOAD_GET_FAILED, "get payload", GetPolicy(ProcessPayloadRestrictionPolicy));

    SetLastError(0xdeadbeef);
    if (SetPolicy(ProcessSystemCallFilterPolicy, 1) || GetLastError() != ERROR_INVALID_PARAMETER)
        SbxChildFail(&Failures, POLICY_SYSCALL_SET_FAILED, "set syscall filter", GetLastError());
    if (GetPolicy(ProcessSystemCallFilterPolicy) != 0)
        SbxChildFail(&Failures, POLICY_SYSCALL_GET_FAILED, "get syscall filter", GetPolicy(ProcessSystemCallFilterPolicy));

    if (SetPolicy(ProcessControlFlowGuardPolicy, 1))
        SbxChildFail(&Failures, POLICY_CFG_SET_ACCEPTED, "runtime CFG set accepted", 0);

    ZeroMemory(&Dynamic, sizeof(Dynamic));
    Dynamic.ProhibitDynamicCode = 1;
    Dynamic.AllowThreadOptOut = 1;
    if (!SetProcessMitigationPolicy(ProcessDynamicCodePolicy, &Dynamic, sizeof(Dynamic)))
        SbxChildFail(&Failures, POLICY_DYNAMIC_SET_FAILED, "set dynamic code with opt-out", GetLastError());
    Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (Memory)
    {
        SbxChildFail(&Failures, POLICY_DYNAMIC_CODE_ALLOWED, "dynamic code allowed before opt-out", 0);
        VirtualFree(Memory, 0, MEM_RELEASE);
    }
    pSetThreadInformation = (PFN_SetThreadInformation)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetThreadInformation");
    if (!pSetThreadInformation || !pSetThreadInformation(GetCurrentThread(), ThreadDynamicCodePolicy, &ThreadPolicy, sizeof(ThreadPolicy)))
        SbxChildFail(&Failures, POLICY_THREAD_OPTOUT_FAILED, "SetThreadInformation(ThreadDynamicCodePolicy)", GetLastError());
    else
    {
        Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
        if (!Memory) SbxChildFail(&Failures, POLICY_THREAD_OPTOUT_FAILED, "dynamic code after thread opt-out", GetLastError());
        else VirtualFree(Memory, 0, MEM_RELEASE);
    }
    return Failures;
}

static DWORD
RunStrictHandleChild(BOOL EnableAtRuntime)
{
    DWORD Failures = 0, Value = 0;
    NTSTATUS Code = STATUS_SUCCESS, Status = STATUS_SUCCESS;
    HANDLE Event;

    if (EnableAtRuntime)
    {
        Value = 3;
        if (!SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &Value, sizeof(Value)))
            SbxChildFail(&Failures, CHILD_STRICT_SET_FAILED, "SetProcessMitigationPolicy(StrictHandleCheck)", GetLastError());
    }

    Value = 0;
    if (!GetProcessMitigationPolicy(GetCurrentProcess(), ProcessStrictHandleCheckPolicy, &Value, sizeof(Value)) || Value != 3)
        SbxChildFail(&Failures, CHILD_STRICT_QUERY_MISMATCH, "strict handle policy query", Value);

    _SEH2_TRY
    {
        Status = NtClose((HANDLE)(ULONG_PTR)0x7FF4);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Code = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (Code != STATUS_SUCCESS || Status != STATUS_INVALID_HANDLE)
        SbxChildFail(&Failures, CHILD_STRICT_NO_EXCEPTION,
                     "NtClose invalid-handle behavior", Code != STATUS_SUCCESS ? Code : Status);

    Code = STATUS_SUCCESS;
    _SEH2_TRY
    {
        Event = CreateEventW(NULL, TRUE, FALSE, NULL);
        if (Event) CloseHandle(Event);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Code = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    if (Code != STATUS_SUCCESS)
        SbxChildFail(&Failures, CHILD_STRICT_VALID_HANDLE_RAISED, "valid handle close raised", Code);

    Value = 0;
    if (SetProcessMitigationPolicy(ProcessStrictHandleCheckPolicy, &Value, sizeof(Value)) || GetLastError() != ERROR_ACCESS_DENIED)
        SbxChildFail(&Failures, CHILD_STRICT_DISABLE_ALLOWED, "strict handle checks disabled after enabling", GetLastError());

    return Failures;
}

static DWORD
RunCreationChild(void)
{
    DWORD Failures = 0;
    WCHAR SystemFont[MAX_PATH], TempFont[MAX_PATH];
    PVOID Memory;

    if ((GetPolicy(ProcessASLRPolicy) & 5) != 5) SbxChildFail(&Failures, CHILD_ASLR_MISMATCH, "ASLR policy", GetPolicy(ProcessASLRPolicy));
    if (GetPolicy(ProcessExtensionPointDisablePolicy) != 1) SbxChildFail(&Failures, CHILD_EP_MISMATCH, "extension point policy", GetPolicy(ProcessExtensionPointDisablePolicy));
    if (GetPolicy(ProcessFontDisablePolicy) != 1) SbxChildFail(&Failures, CHILD_FONT_MISMATCH, "font policy", GetPolicy(ProcessFontDisablePolicy));
    if ((GetPolicy(ProcessImageLoadPolicy) & 3) != 3) SbxChildFail(&Failures, CHILD_IMAGELOAD_MISMATCH, "image load policy", GetPolicy(ProcessImageLoadPolicy));
    if (!(GetPolicy(ProcessDynamicCodePolicy) & 1)) SbxChildFail(&Failures, CHILD_DYNCODE_MISMATCH, "dynamic code policy", GetPolicy(ProcessDynamicCodePolicy));
    Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (Memory) { SbxChildFail(&Failures, CHILD_DYNCODE_MISMATCH, "dynamic code allowed", 0); VirtualFree(Memory, 0, MEM_RELEASE); }

    if (GetModuleHandleW(APPINIT_DLL))
        SbxChildFail(&Failures, CHILD_APPINIT_LOADED, "AppInit DLL loaded despite extension point policy", 0);

    if (!CopyFontToTemp(SystemFont, ARRAYSIZE(SystemFont), TempFont, ARRAYSIZE(TempFont)))
        SbxChildFail(&Failures, CHILD_NO_FONT, "font copy", GetLastError());
    else
    {
        if (AddFontResourceW(TempFont))
        {
            SbxChildFail(&Failures, CHILD_FONT_NOT_ENFORCED, "non-system font accepted", 0);
            RemoveFontResourceW(TempFont);
        }
        if (!AddFontResourceW(SystemFont))
            SbxChildFail(&Failures, CHILD_SYSTEM_FONT_BLOCKED, "system font rejected", GetLastError());
        else
            RemoveFontResourceW(SystemFont);
        DeleteFileW(TempFont);
    }
    return Failures;
}

static DWORD
RunAppInitControlChild(void)
{
    DWORD Failures = 0;
    if (!GetModuleHandleW(APPINIT_DLL))
        SbxChildFail(&Failures, CHILD_APPINIT_NOT_LOADED, "AppInit DLL not loaded without policy", 0);
    return Failures;
}

static DWORD
RunPreferChild(BOOL ExpectSystem32)
{
    DWORD Failures = 0;
    WCHAR System32[MAX_PATH], Loaded[MAX_PATH];
    HMODULE Module = LoadLibraryW(APPINIT_DLL);
    BOOL FromSystem32;

    if (!Module)
    {
        SbxChildFail(&Failures, CHILD_PREFER_MISMATCH, "LoadLibrary", GetLastError());
        return Failures;
    }
    GetSystemDirectoryW(System32, ARRAYSIZE(System32));
    GetModuleFileNameW(Module, Loaded, ARRAYSIZE(Loaded));
    FromSystem32 = _wcsnicmp(Loaded, System32, wcslen(System32)) == 0;
    if (FromSystem32 != ExpectSystem32)
        SbxChildFail(&Failures, CHILD_PREFER_MISMATCH, ExpectSystem32 ? "not loaded from system32" : "loaded from system32", 0);
    FreeLibrary(Module);
    return Failures;
}

static BOOL
SetAppInit(PCWSTR Dll, DWORD Load)
{
    HKEY Key;
    LONG Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, APPINIT_KEY, 0, KEY_SET_VALUE, &Key);
    if (Error != ERROR_SUCCESS) return FALSE;
    RegSetValueExW(Key, L"AppInit_DLLs", 0, REG_SZ, (const BYTE*)Dll, (DWORD)((wcslen(Dll) + 1) * sizeof(WCHAR)));
    RegSetValueExW(Key, L"LoadAppInit_DLLs", 0, REG_DWORD, (const BYTE*)&Load, sizeof(Load));
    RegCloseKey(Key);
    return TRUE;
}

static BOOL
SpawnWithMitigations(PCWSTR Application, PCSTR Mode, ULONGLONG Options, DWORD *ExitCode)
{
    WCHAR Self[MAX_PATH], CommandLine[MAX_PATH * 2];
    STARTUPINFOEXW Startup;
    PROCESS_INFORMATION Info;
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes = NULL;
    SIZE_T Size = 0;
    BOOL Result;

    if (!Application)
    {
        GetModuleFileNameW(NULL, Self, ARRAYSIZE(Self));
        Application = Self;
    }
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" KernelBase child %S", Application, Mode);
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.StartupInfo.cb = sizeof(Startup);
    if (Options)
    {
        InitializeProcThreadAttributeList(NULL, 1, 0, &Size);
        Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
        if (!Attributes || !InitializeProcThreadAttributeList(Attributes, 1, 0, &Size)) return FALSE;
        if (!UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &Options, sizeof(Options), NULL, NULL)) return FALSE;
        Startup.lpAttributeList = Attributes;
    }
    Result = CreateProcessW(Application, CommandLine, NULL, NULL, FALSE,
                            CREATE_UNICODE_ENVIRONMENT | (Attributes ? EXTENDED_STARTUPINFO_PRESENT : 0),
                            NULL, NULL, &Startup.StartupInfo, &Info);
    if (Attributes)
    {
        DeleteProcThreadAttributeList(Attributes);
        HeapFree(GetProcessHeap(), 0, Attributes);
    }
    if (!Result) return FALSE;
    *ExitCode = SbxWaitChild(&Info);
    return TRUE;
}

static void
TestExports(void)
{
    HMODULE KernelBase = LoadLibraryW(L"kernelbase.dll");
    static const PCSTR Names[] =
    {
        "GetProcessMitigationPolicy", "SetProcessMitigationPolicy", "IsProcessInJob",
        "CheckTokenMembershipEx", "CheckTokenCapability", "GetAppContainerNamedObjectPath",
        "DeriveCapabilitySidsFromName", "GetThreadInformation", "SetThreadInformation",
        "AppContainerRegisterSid", "AppContainerUnregisterSid",
        "AppContainerLookupMoniker", "AppContainerFreeMemory",
        "CheckTokenMembership", "DeleteProcThreadAttributeList", "UpdateProcThreadAttribute"
    };
    ULONG Index;

    ok(KernelBase != NULL, "kernelbase.dll not loadable %lu\n", GetLastError());
    if (!KernelBase) return;
    for (Index = 0; Index < ARRAYSIZE(Names); Index++)
        ok(GetProcAddress(KernelBase, Names[Index]) != NULL, "kernelbase!%s missing\n", Names[Index]);
    ok(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "GetAppContainerNamedObjectPath") != NULL, "kernel32!GetAppContainerNamedObjectPath missing\n");
}

static void
FreeDerivedCapabilitySids(PSID *Sids, DWORD Count)
{
    DWORD Index;

    if (!Sids) return;
    for (Index = 0; Index < Count; Index++)
        if (Sids[Index]) LocalFree(Sids[Index]);
    LocalFree(Sids);
}

static void
TestNamedCapabilityDerivation(void)
{
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    PFN_DeriveCapabilitySidsFromName pDerive =
        (PFN_DeriveCapabilitySidsFromName)GetProcAddress(KernelBase, "DeriveCapabilitySidsFromName");
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY PackageAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
    PSID *Groups = NULL, *Capabilities = NULL, *UpperGroups = NULL, *UpperCapabilities = NULL;
    DWORD GroupCount = 0, CapabilityCount = 0, UpperGroupCount = 0, UpperCapabilityCount = 0;

    ok(pDerive != NULL, "DeriveCapabilitySidsFromName missing\n");
    if (!pDerive) return;

    ok(pDerive(L"lpacChromeInstallFiles", &Groups, &GroupCount,
               &Capabilities, &CapabilityCount),
       "named capability derivation failed %lu\n", GetLastError());
    ok(GroupCount == 1 && Groups != NULL, "group SID count %lu\n", GroupCount);
    ok(CapabilityCount == 1 && Capabilities != NULL,
       "capability SID count %lu\n", CapabilityCount);
    if (GroupCount == 1 && Groups && Groups[0])
    {
        ok(IsValidSid(Groups[0]), "derived group SID is invalid\n");
        ok(!memcmp(GetSidIdentifierAuthority(Groups[0]), &NtAuthority, sizeof(NtAuthority)),
           "derived group SID authority mismatch\n");
        ok(*GetSidSubAuthorityCount(Groups[0]) == 9,
           "derived group SID subauthority count %u\n", *GetSidSubAuthorityCount(Groups[0]));
        ok(*GetSidSubAuthority(Groups[0], 0) == SECURITY_BUILTIN_DOMAIN_RID,
           "derived group SID prefix %lu\n", *GetSidSubAuthority(Groups[0], 0));
    }
    if (CapabilityCount == 1 && Capabilities && Capabilities[0])
    {
        ok(IsValidSid(Capabilities[0]), "derived capability SID is invalid\n");
        ok(!memcmp(GetSidIdentifierAuthority(Capabilities[0]), &PackageAuthority, sizeof(PackageAuthority)),
           "derived capability SID authority mismatch\n");
        ok(*GetSidSubAuthorityCount(Capabilities[0]) == 10,
           "derived capability SID subauthority count %u\n", *GetSidSubAuthorityCount(Capabilities[0]));
        ok(*GetSidSubAuthority(Capabilities[0], 0) == SECURITY_CAPABILITY_BASE_RID,
           "derived capability SID prefix %lu\n", *GetSidSubAuthority(Capabilities[0], 0));
        ok(*GetSidSubAuthority(Capabilities[0], 1) == SECURITY_CAPABILITY_APP_RID,
           "derived capability SID type %lu\n", *GetSidSubAuthority(Capabilities[0], 1));
    }

    ok(pDerive(L"LPACCHROMEINSTALLFILES", &UpperGroups, &UpperGroupCount,
               &UpperCapabilities, &UpperCapabilityCount),
       "uppercase named capability derivation failed %lu\n", GetLastError());
    if (GroupCount == 1 && UpperGroupCount == 1 && Groups && UpperGroups)
        ok(EqualSid(Groups[0], UpperGroups[0]), "group capability derivation is case-sensitive\n");
    if (CapabilityCount == 1 && UpperCapabilityCount == 1 && Capabilities && UpperCapabilities)
        ok(EqualSid(Capabilities[0], UpperCapabilities[0]), "capability derivation is case-sensitive\n");

    FreeDerivedCapabilitySids(UpperCapabilities, UpperCapabilityCount);
    FreeDerivedCapabilitySids(UpperGroups, UpperGroupCount);
    FreeDerivedCapabilitySids(Capabilities, CapabilityCount);
    FreeDerivedCapabilitySids(Groups, GroupCount);
}

static void
TestBrowserProfileRegistration(void)
{
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    PFN_AppContainerRegisterSid pRegister =
        (PFN_AppContainerRegisterSid)GetProcAddress(KernelBase, "AppContainerRegisterSid");
    PFN_AppContainerUnregisterSid pUnregister =
        (PFN_AppContainerUnregisterSid)GetProcAddress(KernelBase, "AppContainerUnregisterSid");
    PFN_AppContainerLookupMoniker pLookup =
        (PFN_AppContainerLookupMoniker)GetProcAddress(KernelBase, "AppContainerLookupMoniker");
    PFN_AppContainerFreeMemory pFree =
        (PFN_AppContainerFreeMemory)GetProcAddress(KernelBase, "AppContainerFreeMemory");
    WCHAR Moniker[64];
    PSID Package = NULL;
    LPWSTR Returned = NULL;
    HRESULT hr;
    BOOL Registered = FALSE;

    ok(pRegister != NULL, "AppContainerRegisterSid missing\n");
    ok(pUnregister != NULL, "AppContainerUnregisterSid missing\n");
    ok(pLookup != NULL, "AppContainerLookupMoniker missing\n");
    ok(pFree != NULL, "AppContainerFreeMemory missing\n");
    if (!pRegister || !pUnregister || !pLookup || !pFree) return;

    StringCchPrintfW(Moniker, ARRAYSIZE(Moniker), L"cr.sb.net.sbx%08lx%08lx",
                     GetCurrentProcessId(), GetTickCount());
    hr = DeriveAppContainerSidFromAppContainerName(Moniker, &Package);
    ok(hr == S_OK && Package != NULL, "profile SID derivation failed 0x%lx\n", hr);
    if (!Package) return;

    pUnregister(Package);
    Returned = (LPWSTR)(ULONG_PTR)0xdeadbeef;
    hr = pLookup(Package, &Returned);
    ok(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) &&
       Returned == (LPWSTR)(ULONG_PTR)0xdeadbeef,
       "unregistered SID lookup returned 0x%lx, %p\n", hr, Returned);
    if (SUCCEEDED(hr) && Returned) pFree(Returned);

    hr = pRegister(Package, Moniker, L"ReactOS browser sandbox parity test");
    ok(hr == S_OK, "AppContainerRegisterSid failed 0x%lx\n", hr);
    Registered = SUCCEEDED(hr);
    if (!Registered) goto Cleanup;

    Returned = NULL;
    hr = pLookup(Package, &Returned);
    ok(hr == S_OK && Returned != NULL, "AppContainerLookupMoniker failed 0x%lx\n", hr);
    if (Returned)
    {
        ok(!wcscmp(Returned, Moniker), "lookup moniker %S != %S\n", Returned, Moniker);
        pFree(Returned);
        Returned = NULL;
    }

    hr = pRegister(Package, Moniker, L"ReactOS browser sandbox parity test");
    ok(hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS),
       "duplicate AppContainerRegisterSid returned 0x%lx\n", hr);

    hr = pUnregister(Package);
    ok(hr == S_OK, "AppContainerUnregisterSid failed 0x%lx\n", hr);
    Registered = FALSE;
    Returned = (LPWSTR)(ULONG_PTR)0xdeadbeef;
    hr = pLookup(Package, &Returned);
    ok(hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) &&
       Returned == (LPWSTR)(ULONG_PTR)0xdeadbeef,
       "lookup after unregister returned 0x%lx, %p\n", hr, Returned);
    if (SUCCEEDED(hr) && Returned) pFree(Returned);

Cleanup:
    if (Registered) pUnregister(Package);
    FreeSid(Package);
}

static void
TestTokenHelpers(void)
{
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    PFN_CheckTokenMembershipEx pCheckTokenMembershipEx = (PFN_CheckTokenMembershipEx)GetProcAddress(KernelBase, "CheckTokenMembershipEx");
    PFN_CheckTokenCapability pCheckTokenCapability = (PFN_CheckTokenCapability)GetProcAddress(KernelBase, "CheckTokenCapability");
    PFN_GetAppContainerNamedObjectPath pGetPath = (PFN_GetAppContainerNamedObjectPath)GetProcAddress(KernelBase, "GetAppContainerNamedObjectPath");
    PFN_NtCreateLowBoxToken pNtCreateLowBoxToken = (PFN_NtCreateLowBoxToken)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateLowBoxToken");
    HANDLE Process = NULL, Impersonation = NULL, LowBox = NULL, LowImpersonation = NULL;
    PSID Package = NULL, Capability = NULL, OtherCapability = NULL, Random = NULL, AllPackages = NULL;
    SID_IDENTIFIER_AUTHORITY PackageAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    SID_AND_ATTRIBUTES CapabilityAttr;
    UCHAR UserBuffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD Length;
    BOOL Member = FALSE, Has = FALSE;
    WCHAR Path[256], Expected[256];
    LPWSTR PackageString = NULL;
    ULONG Returned = 0;
    NTSTATUS Status;

    if (!pCheckTokenMembershipEx || !pCheckTokenCapability || !pGetPath || !pNtCreateLowBoxToken)
    {
        skip("token helpers missing\n");
        return;
    }

    Process = SbxOpenToken(TOKEN_QUERY | TOKEN_DUPLICATE);
    ok(Process != NULL, "OpenProcessToken failed %lu\n", GetLastError());
    if (!Process) return;
    ok(DuplicateTokenEx(Process, TOKEN_QUERY, NULL, SecurityImpersonation, TokenImpersonation, &Impersonation), "DuplicateTokenEx failed %lu\n", GetLastError());
    ok(GetTokenInformation(Process, TokenUser, UserBuffer, sizeof(UserBuffer), &Length), "TokenUser failed %lu\n", GetLastError());

    ok(pCheckTokenMembershipEx(Impersonation, ((PTOKEN_USER)UserBuffer)->User.Sid, 0, &Member) && Member, "user SID not a member (%lu)\n", GetLastError());
    AllocateAndInitializeSid(&NtAuthority, 4, 21, 1, 2, 3, 0, 0, 0, 0, &Random);
    Member = TRUE;
    ok(pCheckTokenMembershipEx(Impersonation, Random, 0, &Member) && !Member, "random SID reported as member\n");
    ok(!pCheckTokenMembershipEx(Impersonation, Random, 0x10, &Member) && GetLastError() == ERROR_INVALID_PARAMETER, "invalid flags accepted (%lu)\n", GetLastError());
    ok(pCheckTokenMembershipEx(NULL, ((PTOKEN_USER)UserBuffer)->User.Sid, 0, &Member) && Member, "NULL token membership failed %lu\n", GetLastError());

    ok(DeriveAppContainerSidFromAppContainerName(L"SbxKernelBaseTest", &Package) == S_OK, "Derive failed\n");
    AllocateAndInitializeSid(&PackageAuthority, 2, SECURITY_CAPABILITY_BASE_RID, SECURITY_CAPABILITY_INTERNET_CLIENT, 0, 0, 0, 0, 0, 0, &Capability);
    AllocateAndInitializeSid(&PackageAuthority, 2, SECURITY_CAPABILITY_BASE_RID, SECURITY_CAPABILITY_PRIVATE_NETWORK_CLIENT_SERVER, 0, 0, 0, 0, 0, 0, &OtherCapability);
    AllocateAndInitializeSid(&PackageAuthority, 2, SECURITY_APP_PACKAGE_BASE_RID, SECURITY_BUILTIN_PACKAGE_ANY_PACKAGE, 0, 0, 0, 0, 0, 0, &AllPackages);
    CapabilityAttr.Sid = Capability;
    CapabilityAttr.Attributes = SE_GROUP_ENABLED;
    Status = pNtCreateLowBoxToken(&LowBox, Process, TOKEN_ALL_ACCESS, NULL, Package, 1, &CapabilityAttr, 0, NULL);
    ok(NT_SUCCESS(Status), "NtCreateLowBoxToken failed 0x%lx\n", Status);
    if (LowBox)
    {
        ok(DuplicateTokenEx(LowBox, TOKEN_QUERY, NULL, SecurityImpersonation, TokenImpersonation, &LowImpersonation), "lowbox DuplicateTokenEx failed %lu\n", GetLastError());
        Member = TRUE;
        ok(pCheckTokenMembershipEx(LowImpersonation, Package, 0, &Member) && !Member, "package SID member without CTMF_INCLUDE_APPCONTAINER\n");
        ok(pCheckTokenMembershipEx(LowImpersonation, Package, CTMF_INCLUDE_APPCONTAINER, &Member) && !Member,
           "raw lowbox package SID membership with flag: %d (%lu)\n", Member, GetLastError());
        ok(pCheckTokenMembershipEx(LowImpersonation, Capability, CTMF_INCLUDE_APPCONTAINER, &Member) && !Member,
           "raw lowbox capability membership with flag: %d\n", Member);
        ok(pCheckTokenMembershipEx(LowImpersonation, AllPackages, CTMF_INCLUDE_APPCONTAINER, &Member) && !Member,
           "raw lowbox ALL APPLICATION PACKAGES membership with flag: %d\n", Member);
        {
            UCHAR CapBuffer[sizeof(TOKEN_GROUPS) + 4 * (sizeof(SID_AND_ATTRIBUTES) + SECURITY_MAX_SID_SIZE)];
            DWORD CapIndex;
            LPWSTR CapString = NULL, OtherString = NULL;
            ConvertSidToStringSidW(OtherCapability, &OtherString);
            if (GetTokenInformation(LowImpersonation, TokenCapabilities, CapBuffer, sizeof(CapBuffer), &Length))
            {
                for (CapIndex = 0; CapIndex < ((PTOKEN_GROUPS)CapBuffer)->GroupCount; CapIndex++)
                {
                    ConvertSidToStringSidW(((PTOKEN_GROUPS)CapBuffer)->Groups[CapIndex].Sid, &CapString);
                    trace("capability[%lu] = %S attr 0x%lx (other = %S)\n", CapIndex, CapString, ((PTOKEN_GROUPS)CapBuffer)->Groups[CapIndex].Attributes, OtherString);
                    LocalFree(CapString);
                }
            }
            if (OtherString) LocalFree(OtherString);
        }
        Member = TRUE;
        ok(pCheckTokenMembershipEx(LowImpersonation, OtherCapability, CTMF_INCLUDE_APPCONTAINER, &Member) && !Member, "missing capability reported as member\n");
        Member = TRUE;
        ok(pCheckTokenMembershipEx(LowImpersonation, OtherCapability, 0, &Member) && !Member, "missing capability reported as group member\n");
        Member = TRUE;
        ok(pCheckTokenMembershipEx(LowImpersonation, Random, 0, &Member) && !Member, "random SID member of lowbox token\n");
        {
            PTOKEN_GROUPS LowGroups;
            DWORD GroupIndex, GroupLength = 0;
            LPWSTR GroupString;
            GetTokenInformation(LowImpersonation, TokenGroups, NULL, 0, &GroupLength);
            LowGroups = HeapAlloc(GetProcessHeap(), 0, GroupLength);
            if (LowGroups && GetTokenInformation(LowImpersonation, TokenGroups, LowGroups, GroupLength, &GroupLength))
            {
                for (GroupIndex = 0; GroupIndex < LowGroups->GroupCount; GroupIndex++)
                {
                    ConvertSidToStringSidW(LowGroups->Groups[GroupIndex].Sid, &GroupString);
                    trace("lowbox group[%lu] = %S attr 0x%lx equal-other %d\n", GroupIndex, GroupString, LowGroups->Groups[GroupIndex].Attributes,
                          EqualSid(LowGroups->Groups[GroupIndex].Sid, OtherCapability));
                    LocalFree(GroupString);
                }
            }
            if (LowGroups) HeapFree(GetProcessHeap(), 0, LowGroups);
        }

        ok(pCheckTokenCapability(LowImpersonation, Capability, &Has) && Has, "CheckTokenCapability(held) failed (%lu)\n", GetLastError());
        Has = TRUE;
        ok(pCheckTokenCapability(LowImpersonation, OtherCapability, &Has) && !Has, "CheckTokenCapability(missing) wrong\n");
        Has = TRUE;
        ok(pCheckTokenCapability(Impersonation, Capability, &Has) && Has,
           "CheckTokenCapability(non-AppContainer) wrong\n");

        ConvertSidToStringSidW(Package, &PackageString);
        StringCchPrintfW(Expected, ARRAYSIZE(Expected), L"AppContainerNamedObjects\\%s", PackageString);
        ok(pGetPath(NULL, Package, ARRAYSIZE(Path), Path, &Returned), "GetAppContainerNamedObjectPath(sid) failed %lu\n", GetLastError());
        ok(_wcsicmp(Path, Expected) == 0, "path %S != %S\n", Path, Expected);
        ok(Returned == (ULONG)wcslen(Expected) + 1, "returned length %lu\n", Returned);
        Path[0] = 0;
        ok(pGetPath(LowBox, NULL, ARRAYSIZE(Path), Path, &Returned), "GetAppContainerNamedObjectPath(token) failed %lu\n", GetLastError());
        ok(_wcsicmp(Path, Expected) == 0, "token path %S != %S\n", Path, Expected);
        Returned = 0;
        ok(!pGetPath(NULL, Package, 4, Path, &Returned) && GetLastError() == ERROR_INSUFFICIENT_BUFFER && Returned == (ULONG)wcslen(Expected) + 1,
           "small buffer handling wrong (%lu, %lu)\n", GetLastError(), Returned);
        ok(!pGetPath(Process, NULL, ARRAYSIZE(Path), Path, &Returned), "non-AppContainer token produced a path\n");
        if (PackageString) LocalFree(PackageString);
    }

    if (LowImpersonation) CloseHandle(LowImpersonation);
    if (LowBox) CloseHandle(LowBox);
    if (Impersonation) CloseHandle(Impersonation);
    CloseHandle(Process);
    if (Package) FreeSid(Package);
    if (Capability) FreeSid(Capability);
    if (OtherCapability) FreeSid(OtherCapability);
    if (Random) FreeSid(Random);
    if (AllPackages) FreeSid(AllPackages);
}

static void
TestRuntimePolicies(void)
{
    static const struct
    {
        DWORD Bit;
        PCSTR Name;
    } Cases[] =
    {
        { POLICY_ASLR_SET_FAILED, "ASLR set" },
        { POLICY_ASLR_GET_FAILED, "ASLR query" },
        { POLICY_EP_SET_FAILED, "extension-point set" },
        { POLICY_EP_GET_FAILED, "extension-point query" },
        { POLICY_EP_CLEAR_ALLOWED, "extension-point clear" },
        { POLICY_FONT_SET_FAILED, "font set" },
        { POLICY_FONT_GET_FAILED, "font query" },
        { POLICY_FONT_TOGGLE_FAILED, "font clear" },
        { POLICY_IMAGE_SET_FAILED, "image-load set" },
        { POLICY_IMAGE_GET_FAILED, "image-load query" },
        { POLICY_IMAGE_INVALID_ACCEPTED, "invalid image-load flags" },
        { POLICY_PAYLOAD_SET_FAILED, "payload set" },
        { POLICY_PAYLOAD_GET_FAILED, "payload query" },
        { POLICY_SYSCALL_SET_FAILED, "system-call-filter set" },
        { POLICY_SYSCALL_GET_FAILED, "system-call-filter query" },
        { POLICY_CFG_SET_ACCEPTED, "runtime CFG set" },
        { POLICY_DYNAMIC_SET_FAILED, "dynamic-code set" },
        { POLICY_DYNAMIC_CODE_ALLOWED, "dynamic-code enforcement" },
        { POLICY_THREAD_OPTOUT_FAILED, "dynamic-code thread opt-out" },
    };
    DWORD ExitCode = 1;
    ULONG Index;

    ok(SpawnWithMitigations(NULL, "policy", 0, &ExitCode), "policy child spawn failed %lu\n", GetLastError());
    for (Index = 0; Index < ARRAYSIZE(Cases); Index++)
        ok(!(ExitCode & Cases[Index].Bit), "runtime %s failed (mask 0x%lx)\n", Cases[Index].Name, ExitCode);
}

static void
TestCreationPolicies(void)
{
    DWORD ExitCode = 1;
    WCHAR SystemFont[MAX_PATH], TempFont[MAX_PATH];
    ULONGLONG Options = PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON |
                        PROCESS_CREATION_MITIGATION_POLICY_HIGH_ENTROPY_ASLR_ALWAYS_ON |
                        PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON |
                        PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON |
                        PROCESS_CREATION_MITIGATION_POLICY_FONT_DISABLE_ALWAYS_ON |
                        PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_REMOTE_ALWAYS_ON |
                        PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON;
    BOOL AppInitSet;

    if (CopyFontToTemp(SystemFont, ARRAYSIZE(SystemFont), TempFont, ARRAYSIZE(TempFont)))
    {
        ok(AddFontResourceW(TempFont) != 0, "unrestricted process cannot add a temp font (%lu)\n", GetLastError());
        RemoveFontResourceW(TempFont);
        DeleteFileW(TempFont);
    }
    else
    {
        skip("no system font available\n");
    }

    AppInitSet = SetAppInit(APPINIT_DLL, 1);
    ok(AppInitSet, "AppInit registry setup failed %lu\n", GetLastError());
    ok(SpawnWithMitigations(NULL, "creation", Options, &ExitCode), "creation child spawn failed %lu\n", GetLastError());
    ok(ExitCode == 0, "creation policy child failed with 0x%lx\n", ExitCode);
    if (AppInitSet)
    {
        ok(SpawnWithMitigations(NULL, "appinit-control", 0, &ExitCode), "control child spawn failed %lu\n", GetLastError());
        ok(ExitCode == 0, "AppInit control child failed with 0x%lx\n", ExitCode);
        SetAppInit(L"", 0);
    }
}

static DWORD WINAPI
AffinityThreadProc(LPVOID Parameter)
{
    UNREFERENCED_PARAMETER(Parameter);
    return 0;
}

static DWORD
RunAffinityChild(void)
{
    GROUP_AFFINITY Affinity;
    DWORD_PTR ProcessMask = 0, SystemMask = 0;
    DWORD Failures = 0;
    BOOL Result;
    char Text[128];

    ZeroMemory(&Affinity, sizeof(Affinity));
    Result = GetThreadGroupAffinity(GetCurrentThread(), &Affinity);
    GetProcessAffinityMask(GetCurrentProcess(), &ProcessMask, &SystemMask);
    StringCchPrintfA(Text, ARRAYSIZE(Text), "sandbox child: affinity result %d mask %Ix group %u process %Ix system %Ix\n",
                     Result, Affinity.Mask, Affinity.Group, ProcessMask, SystemMask);
    OutputDebugStringA(Text);
    if (!Result || Affinity.Mask != 1 || Affinity.Group != 0)
        SbxChildFail(&Failures, 1, "initial thread group affinity", (DWORD)Affinity.Mask);
    return Failures;
}

static LPPROC_THREAD_ATTRIBUTE_LIST
AllocateAttributeList(ULONG Count)
{
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    SIZE_T Size = 0;

    InitializeProcThreadAttributeList(NULL, Count, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Attributes)
        return NULL;
    if (!InitializeProcThreadAttributeList(Attributes, Count, 0, &Size))
    {
        HeapFree(GetProcessHeap(), 0, Attributes);
        return NULL;
    }
    return Attributes;
}

static void
FreeAttributeList(LPPROC_THREAD_ATTRIBUTE_LIST Attributes)
{
    if (!Attributes)
        return;
    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);
}

static void
TestThreadAttributes(void)
{
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes = NULL;
    GROUP_AFFINITY Affinity, Original, Queried;
    PROCESSOR_NUMBER Ideal, QueriedIdeal;
    USHORT Node = 0;
    HANDLE Thread;
    DWORD ExitCode = 1;
    PROCESS_INFORMATION Info;
    DWORD Error;
    BOOL Result;

    ZeroMemory(&Original, sizeof(Original));
    ok(GetThreadGroupAffinity(GetCurrentThread(), &Original), "GetThreadGroupAffinity failed %lu\n", GetLastError());
    ZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Mask = 1;
    ok(SetThreadGroupAffinity(GetCurrentThread(), &Affinity, NULL), "SetThreadGroupAffinity failed %lu\n", GetLastError());
    ok(GetThreadGroupAffinity(GetCurrentThread(), &Queried) && Queried.Mask == 1, "group affinity not applied: %Ix\n", Queried.Mask);
    Affinity.Group = 1;
    SetLastError(0xdeadbeef);
    ok(!SetThreadGroupAffinity(GetCurrentThread(), &Affinity, NULL) && GetLastError() == ERROR_INVALID_PARAMETER,
       "nonexistent group accepted: %lu\n", GetLastError());
    Affinity.Group = 0;
    Affinity.Reserved[1] = 1;
    ok(!SetThreadGroupAffinity(GetCurrentThread(), &Affinity, NULL), "reserved bits accepted\n");
    ok(SetThreadGroupAffinity(GetCurrentThread(), &Original, NULL), "restoring the affinity failed %lu\n", GetLastError());

    Attributes = AllocateAttributeList(1);
    ok(Attributes != NULL, "group-affinity attribute list failed %lu\n", GetLastError());
    if (!Attributes) return;
    ZeroMemory(&Affinity, sizeof(Affinity));
    Affinity.Mask = 1;
    SetLastError(0xdeadbeef);
    ok(!UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_GROUP_AFFINITY, &Affinity, sizeof(Affinity) - 1, NULL, NULL) &&
       GetLastError() == ERROR_BAD_LENGTH, "GROUP_AFFINITY size not validated: %lu\n", GetLastError());
    ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_GROUP_AFFINITY, &Affinity, sizeof(Affinity), NULL, NULL),
       "GROUP_AFFINITY failed %lu\n", GetLastError());

    Thread = CreateRemoteThreadEx(GetCurrentProcess(), NULL, 0, AffinityThreadProc, NULL, CREATE_SUSPENDED, Attributes, NULL);
    ok(Thread != NULL, "CreateRemoteThreadEx(group affinity) failed %lu\n", GetLastError());
    if (Thread)
    {
        ok(GetThreadGroupAffinity(Thread, &Queried) && Queried.Mask == 1, "thread attribute affinity not applied: %Ix\n", Queried.Mask);
        ok(ResumeThread(Thread) == 1, "suspended thread was running\n");
        ok(WaitForSingleObject(Thread, 5000) == WAIT_OBJECT_0, "attribute thread did not run\n");
        CloseHandle(Thread);
    }

    SetLastError(0xdeadbeef);
    Result = SbxSpawnChild("KernelBase", "affinity", NULL, NULL, Attributes,
                           CREATE_SUSPENDED, &Info);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER,
       "CreateProcess(group affinity) returned %d, %lu\n", Result, Error);
    if (Result)
    {
        ZeroMemory(&Queried, sizeof(Queried));
        ok(GetThreadGroupAffinity(Info.hThread, &Queried) && Queried.Mask == 1,
           "suspended child initial thread affinity %Ix (%lu)\n", Queried.Mask, GetLastError());
        ok(ResumeThread(Info.hThread) == 1, "child initial thread was not suspended\n");
        ExitCode = SbxWaitChild(&Info);
        ok(ExitCode == 0, "affinity child failed with 0x%lx\n", ExitCode);
    }
    FreeAttributeList(Attributes);

    Attributes = AllocateAttributeList(1);
    ok(Attributes != NULL, "ideal-processor attribute list failed %lu\n", GetLastError());
    if (!Attributes) return;
    ZeroMemory(&Ideal, sizeof(Ideal));
    ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_IDEAL_PROCESSOR,
                                 &Ideal, sizeof(Ideal), NULL, NULL),
       "IDEAL_PROCESSOR failed %lu\n", GetLastError());
    Thread = CreateRemoteThreadEx(GetCurrentProcess(), NULL, 0, AffinityThreadProc, NULL,
                                  CREATE_SUSPENDED, Attributes, NULL);
    ok(Thread != NULL, "CreateRemoteThreadEx(ideal processor) failed %lu\n", GetLastError());
    if (Thread)
    {
        ZeroMemory(&QueriedIdeal, sizeof(QueriedIdeal));
        ok(GetThreadIdealProcessorEx(Thread, &QueriedIdeal) &&
           QueriedIdeal.Group == 0 && QueriedIdeal.Number == 0,
           "ideal processor not applied: group %u number %u (%lu)\n",
           QueriedIdeal.Group, QueriedIdeal.Number, GetLastError());
        ResumeThread(Thread);
        WaitForSingleObject(Thread, 5000);
        CloseHandle(Thread);
    }
    SetLastError(0xdeadbeef);
    Result = SbxSpawnChild("KernelBase", "noop", NULL, NULL, Attributes,
                           CREATE_SUSPENDED, &Info);
    Error = GetLastError();
    ok(!Result && Error == ERROR_INVALID_PARAMETER,
       "CreateProcess(ideal processor) returned %d, %lu\n", Result, Error);
    if (Result)
    {
        ZeroMemory(&QueriedIdeal, sizeof(QueriedIdeal));
        ok(GetThreadIdealProcessorEx(Info.hThread, &QueriedIdeal) &&
           QueriedIdeal.Group == 0 && QueriedIdeal.Number == 0,
           "child ideal processor group %u number %u (%lu)\n",
           QueriedIdeal.Group, QueriedIdeal.Number, GetLastError());
        ResumeThread(Info.hThread);
        ExitCode = SbxWaitChild(&Info);
        ok(ExitCode == 0, "ideal-processor child failed with 0x%lx\n", ExitCode);
    }
    FreeAttributeList(Attributes);

    Attributes = AllocateAttributeList(1);
    ok(Attributes != NULL, "preferred-node attribute list failed %lu\n", GetLastError());
    if (!Attributes) return;
    ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_PREFERRED_NODE,
                                 &Node, sizeof(Node), NULL, NULL),
       "PREFERRED_NODE failed %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    Thread = CreateRemoteThreadEx(GetCurrentProcess(), NULL, 0, AffinityThreadProc, NULL,
                                  CREATE_SUSPENDED, Attributes, NULL);
    Error = GetLastError();
    ok(Thread == NULL && Error == ERROR_INVALID_PARAMETER,
       "CreateRemoteThreadEx accepted process-only preferred node: %p %lu\n", Thread, Error);
    if (Thread)
    {
        TerminateThread(Thread, 0);
        CloseHandle(Thread);
    }
    ok(SbxSpawnChild("KernelBase", "noop", NULL, NULL, Attributes, CREATE_SUSPENDED, &Info),
       "CreateProcess(preferred node) failed %lu\n", GetLastError());
    if (Info.hProcess)
    {
        ResumeThread(Info.hThread);
        ExitCode = SbxWaitChild(&Info);
        ok(ExitCode == 0, "preferred-node child failed with 0x%lx\n", ExitCode);
    }
    FreeAttributeList(Attributes);
}

static void
TestStrictHandleChecks(void)
{
    DWORD ExitCode = 1;

    ok(SpawnWithMitigations(NULL, "strict-runtime", 0, &ExitCode), "strict runtime child spawn failed %lu\n", GetLastError());
    ok(ExitCode == 0, "strict handle runtime child failed with 0x%lx\n", ExitCode);
    ExitCode = 1;
    ok(SpawnWithMitigations(NULL, "strict-creation", PROCESS_CREATION_MITIGATION_POLICY_STRICT_HANDLE_CHECKS_ALWAYS_ON, &ExitCode),
       "strict creation child spawn failed %lu\n", GetLastError());
    ok(ExitCode == 0, "strict handle creation child failed with 0x%lx\n", ExitCode);
}

static void
TestPreferSystem32(void)
{
    WCHAR Temp[MAX_PATH], Dir[MAX_PATH], Exe[MAX_PATH], Dll[MAX_PATH], Self[MAX_PATH], System32[MAX_PATH];
    DWORD ExitCode = 1;

    GetTempPathW(ARRAYSIZE(Temp), Temp);
    StringCchPrintfW(Dir, ARRAYSIZE(Dir), L"%ssbx_prefer", Temp);
    CreateDirectoryW(Dir, NULL);
    StringCchPrintfW(Exe, ARRAYSIZE(Exe), L"%s\\sandbox_apitest.exe", Dir);
    StringCchPrintfW(Dll, ARRAYSIZE(Dll), L"%s\\%s", Dir, APPINIT_DLL);
    GetModuleFileNameW(NULL, Self, ARRAYSIZE(Self));
    GetSystemDirectoryW(System32, ARRAYSIZE(System32));
    StringCchCatW(System32, ARRAYSIZE(System32), L"\\");
    StringCchCatW(System32, ARRAYSIZE(System32), APPINIT_DLL);
    if (!CopyFileW(Self, Exe, FALSE) || !CopyFileW(System32, Dll, FALSE))
    {
        skip("cannot stage prefer-system32 copies (%lu)\n", GetLastError());
        return;
    }

    ok(SpawnWithMitigations(Exe, "prefer-appdir", 0, &ExitCode), "prefer control spawn failed %lu\n", GetLastError());
    ok(ExitCode == 0, "app-dir copy not preferred without policy (0x%lx)\n", ExitCode);
    ok(SpawnWithMitigations(Exe, "prefer-system32", PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_PREFER_SYSTEM32_ALWAYS_ON, &ExitCode), "prefer spawn failed %lu\n", GetLastError());
    ok(ExitCode == 0, "system32 not preferred under policy (0x%lx)\n", ExitCode);

    DeleteFileW(Dll);
    DeleteFileW(Exe);
    RemoveDirectoryW(Dir);
}

START_TEST(KernelBase)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);

    if (SbxIsChild(Arguments, Count, "policy")) ExitProcess(RunPolicyChild());
    if (SbxIsChild(Arguments, Count, "creation")) ExitProcess(RunCreationChild());
    if (SbxIsChild(Arguments, Count, "appinit-control")) ExitProcess(RunAppInitControlChild());
    if (SbxIsChild(Arguments, Count, "prefer-appdir")) ExitProcess(RunPreferChild(FALSE));
    if (SbxIsChild(Arguments, Count, "prefer-system32")) ExitProcess(RunPreferChild(TRUE));
    if (SbxIsChild(Arguments, Count, "strict-runtime")) ExitProcess(RunStrictHandleChild(TRUE));
    if (SbxIsChild(Arguments, Count, "affinity")) ExitProcess(RunAffinityChild());
    if (SbxIsChild(Arguments, Count, "strict-creation")) ExitProcess(RunStrictHandleChild(FALSE));
    if (SbxIsChild(Arguments, Count, "noop")) ExitProcess(0);

    TestExports();
    TestNamedCapabilityDerivation();
    TestBrowserProfileRegistration();
    TestTokenHelpers();
    TestRuntimePolicies();
    TestCreationPolicies();
    TestPreferSystem32();
    TestStrictHandleChecks();
    TestThreadAttributes();
}
