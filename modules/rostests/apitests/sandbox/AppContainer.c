/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     AppContainer profiles, lowbox tokens and capability access checks
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

#define PACKAGE_NAME L"sandbox_apitest_pkg"
#define CHILD_NOT_APPCONTAINER 0x01
#define CHILD_BAD_PACKAGE_SID 0x02
#define CHILD_NOT_LOW 0x04
#define CHILD_USERONLY_OPENED 0x08
#define CHILD_PACKAGE_DENIED 0x10
#define CHILD_ALLPKG_MISMATCH 0x20
#define CHILD_NAMED_OBJECT_FAILED 0x40
#define CHILD_CAPABILITY_MISSING 0x80
#define CHILD_DESKTOP_MISMATCH 0x100
#define CHILD_WINDOW_FAILED 0x200
#define CHILD_JOB_MISSING 0x400
#define CHILD_DYNAMIC_CODE_ALLOWED 0x800

#define APPCONTAINER_TORTURE_CHILDREN 6

static PFN_NtCreateLowBoxToken pNtCreateLowBoxToken;

static PSID
CapabilitySid(DWORD Rid)
{
    SID_IDENTIFIER_AUTHORITY Authority = SECURITY_APP_PACKAGE_AUTHORITY;
    PSID Sid = NULL;
    AllocateAndInitializeSid(&Authority, 2, SECURITY_CAPABILITY_BASE_RID, Rid, 0, 0, 0, 0, 0, 0, &Sid);
    return Sid;
}

static HANDLE
CreateLowBox(HANDLE Base, PSID Package, PSID Capability)
{
    SID_AND_ATTRIBUTES Cap = { Capability, SE_GROUP_ENABLED };
    HANDLE Token = NULL;
    NTSTATUS Status;

    Status = pNtCreateLowBoxToken(&Token, Base, TOKEN_ALL_ACCESS, NULL, Package, Capability ? 1 : 0, Capability ? &Cap : NULL, 0, NULL);
    ok(NT_SUCCESS(Status), "NtCreateLowBoxToken failed 0x%lx\n", Status);
    return NT_SUCCESS(Status) ? Token : NULL;
}

static HANDLE
CreateEventWithSddl(PCWSTR Name, PCWSTR Sddl)
{
    SECURITY_ATTRIBUTES Sa = { sizeof(Sa), NULL, FALSE };
    HANDLE Event;

    Sa.lpSecurityDescriptor = SbxSdFromSddl(Sddl);
    if (!Sa.lpSecurityDescriptor)
    {
        ok(0, "SDDL '%S' failed %lu\n", Sddl, GetLastError());
        return NULL;
    }
    Event = CreateEventW(&Sa, TRUE, FALSE, Name);
    LocalFree(Sa.lpSecurityDescriptor);
    return Event;
}

static HANDLE
OpenAs(HANDLE Token, PCWSTR Name, DWORD Access, PDWORD Error)
{
    HANDLE Handle;

    if (!ImpersonateLoggedOnUser(Token))
    {
        *Error = GetLastError();
        return NULL;
    }
    Handle = OpenEventW(Access, FALSE, Name);
    *Error = GetLastError();
    RevertToSelf();
    return Handle;
}

static LPWSTR
UserSidString(void)
{
    HANDLE Token = SbxOpenToken(TOKEN_QUERY);
    UCHAR Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD Length;
    LPWSTR String = NULL;

    if (!Token) return NULL;
    if (GetTokenInformation(Token, TokenUser, Buffer, sizeof(Buffer), &Length))
        ConvertSidToStringSidW(((PTOKEN_USER)Buffer)->User.Sid, &String);
    CloseHandle(Token);
    return String;
}

static void
TestDerive(void)
{
    PSID First = NULL, Second = NULL, Other = NULL;
    HRESULT hr;

    hr = DeriveAppContainerSidFromAppContainerName(PACKAGE_NAME, &First);
    ok(hr == S_OK, "Derive failed 0x%lx\n", hr);
    hr = DeriveAppContainerSidFromAppContainerName(L"SANDBOX_APITEST_PKG", &Second);
    ok(hr == S_OK, "Derive(upper) failed 0x%lx\n", hr);
    hr = DeriveAppContainerSidFromAppContainerName(L"sandbox_apitest_other", &Other);
    ok(hr == S_OK, "Derive(other) failed 0x%lx\n", hr);
    if (First && Second && Other)
    {
        ok(*GetSidSubAuthorityCount(First) == SECURITY_APP_PACKAGE_RID_COUNT, "subauthority count %u\n", *GetSidSubAuthorityCount(First));
        ok(*GetSidSubAuthority(First, 0) == SECURITY_APP_PACKAGE_BASE_RID, "base rid %lu\n", *GetSidSubAuthority(First, 0));
        ok(EqualSid(First, Second), "derivation is case sensitive\n");
        ok(!EqualSid(First, Other), "different names collide\n");
    }
    if (First) LocalFree(First);
    if (Second) LocalFree(Second);
    if (Other) LocalFree(Other);
}

static void
TestProfile(void)
{
    PSID Sid = NULL;
    PSID DuplicateSid = NULL;
    HRESULT hr;
    HKEY Key;

    DeleteAppContainerProfile(PACKAGE_NAME);
    hr = CreateAppContainerProfile(PACKAGE_NAME, L"Sandbox Test", L"apitest", NULL, 0, &Sid);
    ok(hr == S_OK, "CreateAppContainerProfile failed 0x%lx\n", hr);
    ok(Sid != NULL, "no SID returned\n");
    if (Sid) LocalFree(Sid);
    hr = CreateAppContainerProfile(PACKAGE_NAME, L"Sandbox Test", L"apitest", NULL, 0, &DuplicateSid);
    ok(hr == HRESULT_FROM_WIN32(ERROR_ALREADY_EXISTS), "second create returned 0x%lx\n", hr);
    if (DuplicateSid) LocalFree(DuplicateSid);
    hr = DeleteAppContainerProfile(PACKAGE_NAME);
    ok(hr == S_OK, "DeleteAppContainerProfile failed 0x%lx\n", hr);
    hr = GetAppContainerRegistryLocation(KEY_READ, &Key);
    ok(FAILED(hr), "registry location outside an AppContainer succeeded\n");
    if (SUCCEEDED(hr)) RegCloseKey(Key);
}

static void
TestLowBoxToken(void)
{
    HANDLE Base, Token, Impersonation = NULL;
    PSID Package = NULL, Capability;
    UCHAR Buffer[sizeof(TOKEN_APPCONTAINER_INFORMATION) + SECURITY_MAX_SID_SIZE];
    UCHAR GroupsBuffer[sizeof(TOKEN_GROUPS) + 4 * (sizeof(SID_AND_ATTRIBUTES) + SECURITY_MAX_SID_SIZE)];
    DWORD Value, Length, Error;
    DWORD LabelPolicy, LabelRid;
    TOKEN_MANDATORY_POLICY MandatoryPolicy;
    LPWSTR UserString, CapabilityString = NULL;
    WCHAR Sddl[512];
    HANDLE Event, Opened;

    ok(DeriveAppContainerSidFromAppContainerName(PACKAGE_NAME, &Package) == S_OK, "Derive failed\n");
    Capability = CapabilitySid(SECURITY_CAPABILITY_INTERNET_CLIENT);
    ok(Capability != NULL, "capability SID allocation failed\n");
    Base = SbxOpenToken(TOKEN_ALL_ACCESS);
    if (!Package || !Capability || !Base) goto Cleanup;

    Token = CreateLowBox(Base, Package, Capability);
    if (!Token) goto Cleanup;

    ok(GetTokenInformation(Token, TokenIsAppContainer, &Value, sizeof(Value), &Length) && Value == 1, "TokenIsAppContainer not set\n");
    ok(GetTokenInformation(Token, TokenAppContainerNumber, &Value, sizeof(Value), &Length) && Value != 0, "TokenAppContainerNumber is zero\n");
    ok(GetTokenInformation(Token, TokenAppContainerSid, Buffer, sizeof(Buffer), &Length), "TokenAppContainerSid failed %lu\n", GetLastError());
    ok(((PTOKEN_APPCONTAINER_INFORMATION)Buffer)->TokenAppContainer &&
       EqualSid(((PTOKEN_APPCONTAINER_INFORMATION)Buffer)->TokenAppContainer, Package), "package SID mismatch\n");
    ok(GetTokenInformation(Token, TokenCapabilities, GroupsBuffer, sizeof(GroupsBuffer), &Length), "TokenCapabilities failed %lu\n", GetLastError());
    ok(((PTOKEN_GROUPS)GroupsBuffer)->GroupCount == 1, "capability count %lu\n", ((PTOKEN_GROUPS)GroupsBuffer)->GroupCount);
    if (((PTOKEN_GROUPS)GroupsBuffer)->GroupCount == 1)
        ok(EqualSid(((PTOKEN_GROUPS)GroupsBuffer)->Groups[0].Sid, Capability), "capability SID mismatch\n");
    ok(SbxGetTokenIntegrity(Token) == SECURITY_MANDATORY_LOW_RID, "lowbox integrity 0x%lx\n", SbxGetTokenIntegrity(Token));

    ok(GetTokenInformation(Base, TokenIsAppContainer, &Value, sizeof(Value), &Length) && Value == 0, "base token reports AppContainer\n");

    ok(DuplicateTokenEx(Token, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &Impersonation),
       "DuplicateTokenEx failed %lu\n", GetLastError());
    ok(GetTokenInformation(Impersonation, TokenIsAppContainer, &Value, sizeof(Value), &Length) && Value == 1, "duplicate lost AppContainer\n");
    ok(SbxGetTokenIntegrity(Impersonation) == SECURITY_MANDATORY_LOW_RID,
       "duplicate integrity 0x%lx\n", SbxGetTokenIntegrity(Impersonation));
    ZeroMemory(&MandatoryPolicy, sizeof(MandatoryPolicy));
    ok(GetTokenInformation(Impersonation, TokenMandatoryPolicy, &MandatoryPolicy,
                           sizeof(MandatoryPolicy), &Length),
       "TokenMandatoryPolicy failed %lu\n", GetLastError());
    trace("lowbox impersonation mandatory policy 0x%lx\n", MandatoryPolicy.Policy);

    UserString = UserSidString();
    ConvertSidToStringSidW(Capability, &CapabilityString);
    if (!UserString || !CapabilityString || !Impersonation) goto CleanupToken;

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)S:(ML;;NW;;;LW)", UserString);
    Event = CreateEventWithSddl(L"sbx_ac_useronly", Sddl);
    if (Event)
    {
        Opened = OpenAs(Impersonation, L"sbx_ac_useronly", EVENT_ALL_ACCESS, &Error);
        ok(Opened == NULL && Error == ERROR_ACCESS_DENIED, "lowbox open of user-only DACL: %p %lu\n", Opened, Error);
        if (Opened) CloseHandle(Opened);
        CloseHandle(Event);
    }

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;GA;;;S-1-15-2-1)S:(ML;;NW;;;LW)", UserString);
    Event = CreateEventWithSddl(L"sbx_ac_allpkg", Sddl);
    if (Event)
    {
        Opened = OpenAs(Impersonation, L"sbx_ac_allpkg", EVENT_ALL_ACCESS, &Error);
        ok(Opened != NULL, "lowbox open with ALL APPLICATION PACKAGES ACE failed %lu\n", Error);
        if (Opened) CloseHandle(Opened);
        CloseHandle(Event);
    }

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;S-1-15-2-1)S:(ML;;NW;;;LW)");
    Event = CreateEventWithSddl(L"sbx_ac_allpkgonly", Sddl);
    if (Event)
    {
        Opened = OpenAs(Impersonation, L"sbx_ac_allpkgonly", EVENT_ALL_ACCESS, &Error);
        ok(Opened == NULL && Error == ERROR_ACCESS_DENIED,
           "lowbox open with only ALL APPLICATION PACKAGES ACE: %p %lu\n", Opened, Error);
        if (Opened) CloseHandle(Opened);
        CloseHandle(Event);
    }

    {
        LPWSTR PackageString = NULL;
        ConvertSidToStringSidW(Package, &PackageString);
        StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;GA;;;%s)S:(ML;;NW;;;LW)", UserString, PackageString);
        LocalFree(PackageString);
    }
    Event = CreateEventWithSddl(L"sbx_ac_package", Sddl);
    if (Event)
    {
        Opened = OpenAs(Impersonation, L"sbx_ac_package", EVENT_ALL_ACCESS, &Error);
        ok(Opened != NULL, "lowbox open with package SID ACE failed %lu\n", Error);
        if (Opened) CloseHandle(Opened);
        CloseHandle(Event);
    }

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;GA;;;%s)S:(ML;;NW;;;LW)", UserString, CapabilityString);
    Event = CreateEventWithSddl(L"sbx_ac_capability", Sddl);
    if (Event)
    {
        Opened = OpenAs(Impersonation, L"sbx_ac_capability", EVENT_ALL_ACCESS, &Error);
        ok(Opened != NULL, "lowbox open with capability ACE failed %lu\n", Error);
        if (Opened) CloseHandle(Opened);
        CloseHandle(Event);
    }

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;GA;;;S-1-15-3-2)S:(ML;;NW;;;LW)", UserString);
    Event = CreateEventWithSddl(L"sbx_ac_othercap", Sddl);
    if (Event)
    {
        Opened = OpenAs(Impersonation, L"sbx_ac_othercap", EVENT_ALL_ACCESS, &Error);
        ok(Opened == NULL && Error == ERROR_ACCESS_DENIED, "lowbox open with foreign capability ACE: %p %lu\n", Opened, Error);
        if (Opened) CloseHandle(Opened);
        CloseHandle(Event);
    }

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;GA;;;S-1-15-2-1)S:(ML;;NW;;;ME)", UserString);
    Event = CreateEventWithSddl(L"sbx_ac_medium", Sddl);
    if (Event)
    {
        LabelRid = SbxQueryLabelRid(Event, SE_KERNEL_OBJECT, &LabelPolicy);
        ok(LabelRid == SECURITY_MANDATORY_MEDIUM_RID,
           "explicit medium object label 0x%lx\n", LabelRid);
        ok(LabelPolicy == SYSTEM_MANDATORY_LABEL_NO_WRITE_UP,
           "explicit medium object policy 0x%lx\n", LabelPolicy);
        Opened = OpenAs(Impersonation, L"sbx_ac_medium", EVENT_MODIFY_STATE, &Error);
        ok(Opened != NULL,
           "lowbox impersonation token could not modify the medium object: %lu\n", Error);
        if (Opened) CloseHandle(Opened);
        CloseHandle(Event);
    }

CleanupToken:
    if (CapabilityString) LocalFree(CapabilityString);
    if (UserString) LocalFree(UserString);
    if (Impersonation) CloseHandle(Impersonation);
    CloseHandle(Token);
Cleanup:
    if (Base) CloseHandle(Base);
    if (Capability) FreeSid(Capability);
    if (Package) LocalFree(Package);
}

static DWORD
RunChild(BOOL Lpac, PCSTR ExpectedDesktop)
{
    DWORD Failures = 0;
    HANDLE Token = SbxOpenToken(TOKEN_QUERY);
    DWORD Value = 0, Length;
    UCHAR Buffer[sizeof(TOKEN_APPCONTAINER_INFORMATION) + SECURITY_MAX_SID_SIZE];
    UCHAR GroupsBuffer[sizeof(TOKEN_GROUPS) + 4 * (sizeof(SID_AND_ATTRIBUTES) + SECURITY_MAX_SID_SIZE)];
    PSID Package = NULL;
    HANDLE Event;

    if (ExpectedDesktop)
    {
        HDESK Desktop;
        WCHAR ActualDesktop[128], WantedDesktop[128];
        DWORD NameLength;
        BOOL InJob = FALSE;
        HWND Window;
        PVOID Memory;

        MultiByteToWideChar(CP_ACP, 0, ExpectedDesktop, -1,
                            WantedDesktop, ARRAYSIZE(WantedDesktop));
        Desktop = GetThreadDesktop(GetCurrentThreadId());
        if (!Desktop ||
            !GetUserObjectInformationW(Desktop, UOI_NAME, ActualDesktop,
                                       sizeof(ActualDesktop), &NameLength) ||
            _wcsicmp(ActualDesktop, WantedDesktop))
        {
            SbxChildFail(&Failures, CHILD_DESKTOP_MISMATCH,
                         "alternate AppContainer desktop", GetLastError());
        }
        Window = CreateWindowExW(0, L"STATIC", L"AppContainer torture",
                                 WS_OVERLAPPED, 0, 0, 16, 16, NULL, NULL,
                                 GetModuleHandleW(NULL), NULL);
        if (!Window)
            SbxChildFail(&Failures, CHILD_WINDOW_FAILED,
                         "window creation on AppContainer desktop", GetLastError());
        else
            DestroyWindow(Window);
        if (!IsProcessInJob(GetCurrentProcess(), NULL, &InJob) || !InJob)
            SbxChildFail(&Failures, CHILD_JOB_MISSING,
                         "AppContainer process job assignment", GetLastError());
        Memory = VirtualAlloc(NULL, 4096, MEM_COMMIT | MEM_RESERVE,
                              PAGE_EXECUTE_READWRITE);
        if (Memory)
        {
            SbxChildFail(&Failures, CHILD_DYNAMIC_CODE_ALLOWED,
                         "dynamic code allowed in AppContainer torture child", 0);
            VirtualFree(Memory, 0, MEM_RELEASE);
        }
    }

    if (!Token) return 0xFF;
    if (!GetTokenInformation(Token, TokenIsAppContainer, &Value, sizeof(Value), &Length) || Value != 1)
        SbxChildFail(&Failures, CHILD_NOT_APPCONTAINER, "TokenIsAppContainer", GetLastError());
    DeriveAppContainerSidFromAppContainerName(PACKAGE_NAME, &Package);
    if (!GetTokenInformation(Token, TokenAppContainerSid, Buffer, sizeof(Buffer), &Length) ||
        !((PTOKEN_APPCONTAINER_INFORMATION)Buffer)->TokenAppContainer || !Package ||
        !EqualSid(((PTOKEN_APPCONTAINER_INFORMATION)Buffer)->TokenAppContainer, Package))
        SbxChildFail(&Failures, CHILD_BAD_PACKAGE_SID, "TokenAppContainerSid", GetLastError());
    if (SbxGetTokenIntegrity(Token) != SECURITY_MANDATORY_LOW_RID)
        SbxChildFail(&Failures, CHILD_NOT_LOW, "integrity", SbxGetTokenIntegrity(Token));
    if (!GetTokenInformation(Token, TokenCapabilities, GroupsBuffer, sizeof(GroupsBuffer), &Length) ||
        ((PTOKEN_GROUPS)GroupsBuffer)->GroupCount != 1)
        SbxChildFail(&Failures, CHILD_CAPABILITY_MISSING, "TokenCapabilities", GetLastError());
    CloseHandle(Token);
    if (Package) LocalFree(Package);

    Event = OpenEventW(EVENT_ALL_ACCESS, FALSE, L"Global\\sbx_ac_child_useronly");
    if (Event) { SbxChildFail(&Failures, CHILD_USERONLY_OPENED, "user-only event opened", 0); CloseHandle(Event); }
    Event = OpenEventW(EVENT_ALL_ACCESS, FALSE, L"Global\\sbx_ac_child_package");
    if (!Event) SbxChildFail(&Failures, CHILD_PACKAGE_DENIED, "package event denied", GetLastError()); else CloseHandle(Event);
    Event = OpenEventW(EVENT_ALL_ACCESS, FALSE, L"Global\\sbx_ac_child_allpkg");
    if (Lpac ? (Event != NULL) : (Event == NULL))
        SbxChildFail(&Failures, CHILD_ALLPKG_MISMATCH, "ALL APPLICATION PACKAGES event", GetLastError());
    if (Event) CloseHandle(Event);
    Event = OpenEventW(EVENT_ALL_ACCESS, FALSE, L"Global\\sbx_ac_child_restrictedpkg");
    if (!Event)
        SbxChildFail(&Failures, CHILD_ALLPKG_MISMATCH, "ALL RESTRICTED APPLICATION PACKAGES event", GetLastError());
    if (Event) CloseHandle(Event);

    Event = CreateEventW(NULL, TRUE, FALSE, L"sbx_ac_child_named");
    if (!Event) SbxChildFail(&Failures, CHILD_NAMED_OBJECT_FAILED, "CreateEvent in named object directory", GetLastError());
    else
    {
        UCHAR NameBuffer[1024];
        ULONG NameLength;
        if (NT_SUCCESS(NtQueryObject(Event, ObjectNameInformation, NameBuffer, sizeof(NameBuffer), &NameLength)))
        {
            OutputDebugStringW(L"sandbox child: event path ");
            OutputDebugStringW(((POBJECT_NAME_INFORMATION)NameBuffer)->Name.Buffer);
            OutputDebugStringW(L"\n");
        }
        SetEvent(Event);
        Sleep(1500);
        CloseHandle(Event);
    }
    return Failures;
}

static HANDLE
CreateChildDirectory(PSID Package)
{
    WCHAR Path[256], Sddl[256];
    LPWSTR PackageString = NULL;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Parent = NULL, Directory = NULL;
    PSECURITY_DESCRIPTOR Sd;
    NTSTATUS Status;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Sessions\\%lu\\AppContainerNamedObjects", NtCurrentPeb()->SessionId);
    RtlInitUnicodeString(&Name, Path);
    ConvertSidToStringSidW(Package, &PackageString);
    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;OICI;GA;;;WD)(A;OICI;GA;;;%s)S:(ML;;NW;;;LW)", PackageString);
    Sd = SbxSdFromSddl(Sddl);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, NULL, Sd);
    Status = NtCreateDirectoryObject(&Parent, DIRECTORY_ALL_ACCESS, &Attributes);
    if (!NT_SUCCESS(Status))
    {
        skip("cannot create AppContainerNamedObjects (0x%lx)\n", Status);
        LocalFree(Sd);
        LocalFree(PackageString);
        return NULL;
    }
    StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s", PackageString);
    LocalFree(PackageString);
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE | OBJ_OPENIF, Parent, Sd);
    Status = NtCreateDirectoryObject(&Directory, DIRECTORY_ALL_ACCESS, &Attributes);
    ok(NT_SUCCESS(Status), "package directory creation failed 0x%lx\n", Status);
    NtClose(Parent);
    LocalFree(Sd);
    return NT_SUCCESS(Status) ? Directory : NULL;
}

static NTSTATUS
CreatePackageDirectoryByPath(PSID Package, PHANDLE Directory)
{
    WCHAR Path[512];
    LPWSTR PackageString = NULL;
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    NTSTATUS Status;

    *Directory = NULL;
    if (!ConvertSidToStringSidW(Package, &PackageString))
        return STATUS_NO_MEMORY;
    StringCchPrintfW(Path, ARRAYSIZE(Path),
                     L"\\Sessions\\%lu\\AppContainerNamedObjects\\%s",
                     NtCurrentPeb()->SessionId, PackageString);
    LocalFree(PackageString);
    RtlInitUnicodeString(&Name, Path);
    InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtCreateDirectoryObject(Directory, DIRECTORY_ALL_ACCESS, &Attributes);
    return Status;
}

static void
TestSavedHandleLifetime(void)
{
    WCHAR PackageName[96];
    HANDLE Base = NULL, Directory = NULL, Probe = NULL, Token = NULL;
    PSID Package = NULL;
    NTSTATUS Status;

    StringCchPrintfW(PackageName, ARRAYSIZE(PackageName),
                     L"sandbox_apitest_saved_%08lx_%08lx",
                     GetCurrentProcessId(), GetTickCount());
    ok(DeriveAppContainerSidFromAppContainerName(PackageName, &Package) == S_OK,
       "saved-handle package SID derivation failed\n");
    if (!Package) return;

    Directory = CreateChildDirectory(Package);
    ok(Directory != NULL, "saved-handle package directory creation failed\n");
    Base = SbxOpenToken(TOKEN_ALL_ACCESS);
    ok(Base != NULL, "OpenProcessToken failed %lu\n", GetLastError());
    if (!Directory || !Base) goto Cleanup;

    Status = pNtCreateLowBoxToken(&Token, Base, TOKEN_ALL_ACCESS, NULL, Package,
                                  0, NULL, 1, &Directory);
    ok(NT_SUCCESS(Status) && Token != NULL,
       "NtCreateLowBoxToken(saved handle) failed 0x%lx\n", Status);
    if (!Token) goto Cleanup;

    NtClose(Directory);
    Directory = NULL;
    Status = CreatePackageDirectoryByPath(Package, &Probe);
    ok(Status == STATUS_OBJECT_NAME_COLLISION,
       "lowbox token did not retain its saved directory handle: 0x%lx\n", Status);
    if (Probe)
    {
        NtClose(Probe);
        Probe = NULL;
    }

    NtClose(Token);
    Token = NULL;
    Status = CreatePackageDirectoryByPath(Package, &Probe);
    ok(NT_SUCCESS(Status),
       "saved directory survived after the lowbox token was closed: 0x%lx\n", Status);

Cleanup:
    if (Probe) NtClose(Probe);
    if (Token) NtClose(Token);
    if (Directory) NtClose(Directory);
    if (Base) CloseHandle(Base);
    LocalFree(Package);
}

static void
TestRegistryLocationAsLowBox(PSID Package)
{
    HANDLE Base = NULL, LowBox = NULL, Impersonation = NULL;
    HKEY Key = NULL;
    DWORD SubKeys = 0, Values = 0;
    HRESULT hr;

    Base = SbxOpenToken(TOKEN_QUERY | TOKEN_DUPLICATE);
    ok(Base != NULL, "OpenProcessToken failed %lu\n", GetLastError());
    if (!Base) return;
    LowBox = CreateLowBox(Base, Package, NULL);
    if (!LowBox) goto Cleanup;
    ok(DuplicateTokenEx(LowBox, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation,
                        TokenImpersonation, &Impersonation),
       "lowbox impersonation token creation failed %lu\n", GetLastError());
    if (!Impersonation) goto Cleanup;

    if (!ImpersonateLoggedOnUser(Impersonation))
    {
        ok(0, "lowbox impersonation failed %lu\n", GetLastError());
        goto Cleanup;
    }
    hr = GetAppContainerRegistryLocation(KEY_READ, &Key);
    ok(RevertToSelf(), "RevertToSelf failed %lu\n", GetLastError());
    ok(hr == S_OK && Key != NULL,
       "GetAppContainerRegistryLocation under lowbox impersonation failed 0x%lx\n",
       hr);
    if (Key)
    {
        ok(RegQueryInfoKeyW(Key, NULL, NULL, NULL, &SubKeys, NULL, NULL,
                            &Values, NULL, NULL, NULL, NULL) == ERROR_SUCCESS,
           "returned AppContainer registry key is unusable\n");
    }

Cleanup:
    if (Key) RegCloseKey(Key);
    if (Impersonation) CloseHandle(Impersonation);
    if (LowBox) CloseHandle(LowBox);
    if (Base) CloseHandle(Base);
}

static BOOL
StageAppContainerImage(PSID Package, PCWSTR UserString, PWSTR Application, SIZE_T ApplicationCount,
                       PWSTR Directory, SIZE_T DirectoryCount)
{
    WCHAR ProgramData[MAX_PATH], Source[MAX_PATH], Sddl[512];
    LPWSTR PackageString = NULL;
    PSECURITY_DESCRIPTOR Sd = NULL;
    DWORD Length;
    BOOL Result = FALSE;

    Length = GetEnvironmentVariableW(L"ProgramData", ProgramData, ARRAYSIZE(ProgramData));
    if (!Length || Length >= ARRAYSIZE(ProgramData))
    {
        Length = GetTempPathW(ARRAYSIZE(ProgramData), ProgramData);
        if (!Length || Length >= ARRAYSIZE(ProgramData))
            return FALSE;
    }
    if (FAILED(StringCchPrintfW(Directory, DirectoryCount, L"%s\\sandbox_apitest_%lu",
                                ProgramData, GetCurrentProcessId())))
        return FALSE;
    if (!CreateDirectoryW(Directory, NULL) && GetLastError() != ERROR_ALREADY_EXISTS)
        return FALSE;

    if (!ConvertSidToStringSidW(Package, &PackageString))
        goto Cleanup;
    if (FAILED(StringCchPrintfW(Sddl, ARRAYSIZE(Sddl),
                                L"D:P(A;OICI;FA;;;%s)(A;OICI;GRGX;;;%s)",
                                UserString, PackageString)))
        goto Cleanup;
    Sd = SbxSdFromSddl(Sddl);
    if (!Sd || !SetFileSecurityW(Directory, DACL_SECURITY_INFORMATION, Sd))
        goto Cleanup;

    if (!GetModuleFileNameW(NULL, Source, ARRAYSIZE(Source)))
        goto Cleanup;
    if (FAILED(StringCchPrintfW(Application, ApplicationCount, L"%s\\sandbox_apitest.exe", Directory)))
        goto Cleanup;
    if (!CopyFileW(Source, Application, FALSE))
        goto Cleanup;
    if (!SetFileSecurityW(Application, DACL_SECURITY_INFORMATION, Sd))
    {
        DeleteFileW(Application);
        goto Cleanup;
    }
    Result = TRUE;

Cleanup:
    if (Sd) LocalFree(Sd);
    if (PackageString) LocalFree(PackageString);
    if (!Result) RemoveDirectoryW(Directory);
    return Result;
}

static void
TestChild(BOOL Lpac)
{
    PSID Package = NULL, Capability = NULL;
    SECURITY_CAPABILITIES Capabilities;
    SID_AND_ATTRIBUTES CapabilityAttr;
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes = NULL;
    SIZE_T Size = 0;
    DWORD OptOut = PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT;
    PROCESS_INFORMATION Info;
    LPWSTR UserString = UserSidString(), PackageString = NULL;
    WCHAR Sddl[512];
    WCHAR Application[MAX_PATH] = L"", StageDirectory[MAX_PATH] = L"";
    HANDLE UserOnly = NULL, PackageEvent = NULL, AllPkg = NULL, RestrictedPkg = NULL, Directory = NULL, Named = NULL;
    DWORD ExitCode;
    BOOL Staged;

    ok(DeriveAppContainerSidFromAppContainerName(PACKAGE_NAME, &Package) == S_OK, "Derive failed\n");
    Capability = CapabilitySid(SECURITY_CAPABILITY_INTERNET_CLIENT);
    if (!Package || !Capability || !UserString) goto Cleanup;
    ConvertSidToStringSidW(Package, &PackageString);
    Staged = StageAppContainerImage(Package, UserString, Application, ARRAYSIZE(Application),
                                    StageDirectory, ARRAYSIZE(StageDirectory));
    ok(Staged,
       "failed to stage an AppContainer-readable image: %lu\n", GetLastError());
    if (!Staged) goto Cleanup;

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)S:(ML;;NW;;;LW)", UserString);
    UserOnly = CreateEventWithSddl(L"Global\\sbx_ac_child_useronly", Sddl);
    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;GA;;;%s)S:(ML;;NW;;;LW)", UserString, PackageString);
    PackageEvent = CreateEventWithSddl(L"Global\\sbx_ac_child_package", Sddl);
    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;GA;;;S-1-15-2-1)S:(ML;;NW;;;LW)", UserString);
    AllPkg = CreateEventWithSddl(L"Global\\sbx_ac_child_allpkg", Sddl);
    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;GA;;;S-1-15-2-2)S:(ML;;NW;;;LW)", UserString);
    RestrictedPkg = CreateEventWithSddl(L"Global\\sbx_ac_child_restrictedpkg", Sddl);
    ok(UserOnly && PackageEvent && AllPkg && RestrictedPkg, "event creation failed %lu\n", GetLastError());

    Directory = NULL;

    CapabilityAttr.Sid = Capability;
    CapabilityAttr.Attributes = SE_GROUP_ENABLED;
    Capabilities.AppContainerSid = Package;
    Capabilities.Capabilities = &CapabilityAttr;
    Capabilities.CapabilityCount = 1;
    Capabilities.Reserved = 0;

    InitializeProcThreadAttributeList(NULL, 2, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    ok(Attributes && InitializeProcThreadAttributeList(Attributes, 2, 0, &Size), "InitializeProcThreadAttributeList failed %lu\n", GetLastError());
    if (!Attributes) goto Cleanup;
    ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES, &Capabilities, sizeof(Capabilities), NULL, NULL),
       "UpdateProcThreadAttribute(SECURITY_CAPABILITIES) failed %lu\n", GetLastError());
    if (Lpac)
        ok(UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY, &OptOut, sizeof(OptOut), NULL, NULL),
           "UpdateProcThreadAttribute(ALL_APPLICATION_PACKAGES_POLICY) failed %lu\n", GetLastError());

    ok(SbxSpawnChildExecutable(Application, StageDirectory, "AppContainer", Lpac ? "lpac" : "ac",
                               NULL, NULL, Attributes, 0, &Info),
       "CreateProcess(AppContainer child) failed %lu\n", GetLastError());
    if (Info.hProcess)
    {
        HANDLE ChildToken = NULL;
        DWORD Value = 0, Length;

        if (OpenProcessToken(Info.hProcess, TOKEN_QUERY, &ChildToken))
        {
            ok(GetTokenInformation(ChildToken, TokenIsAppContainer, &Value, sizeof(Value), &Length) && Value == 1,
               "child process token is not an AppContainer\n");
            CloseHandle(ChildToken);
        }
        {
            UNICODE_STRING Name;
            OBJECT_ATTRIBUTES ObjectAttributes;
            NTSTATUS Status = STATUS_OBJECT_NAME_NOT_FOUND;
            ULONG Attempt;
            WCHAR Path[512];
            UNICODE_STRING DirectoryPath;

            StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Sessions\\%lu\\AppContainerNamedObjects\\%s\\sbx_ac_child_named", NtCurrentPeb()->SessionId, PackageString);
            RtlInitUnicodeString(&Name, Path);
            InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, NULL, NULL);
            for (Attempt = 0; Attempt < 40 && !NT_SUCCESS(Status); Attempt++)
            {
                Status = NtOpenEvent(&Named, EVENT_ALL_ACCESS, &ObjectAttributes);
                if (!NT_SUCCESS(Status)) Sleep(100);
            }
            StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Sessions\\%lu\\AppContainerNamedObjects\\%s", NtCurrentPeb()->SessionId, PackageString);
            RtlInitUnicodeString(&DirectoryPath, Path);
            InitializeObjectAttributes(&ObjectAttributes, &DirectoryPath, OBJ_CASE_INSENSITIVE, NULL, NULL);
            ok(NT_SUCCESS(NtOpenDirectoryObject(&Directory, DIRECTORY_QUERY, &ObjectAttributes)), "package directory missing after launch\n");
            if (Named)
            {
                NtClose(Named);
                Named = NULL;
            }
            if (Directory)
            {
                RtlInitUnicodeString(&Name, L"Global");
                InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, Directory, NULL);
                Status = NtOpenSymbolicLinkObject(&Named, SYMBOLIC_LINK_QUERY, &ObjectAttributes);
                if (Status == STATUS_OBJECT_TYPE_MISMATCH)
                    Status = NtOpenDirectoryObject(&Named, DIRECTORY_QUERY, &ObjectAttributes);
                ok(NT_SUCCESS(Status), "Global object missing in the package directory (0x%lx)\n", Status);
                if (NT_SUCCESS(Status)) { CloseHandle(Named); Named = NULL; }
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
                Named = NULL;
                RtlInitUnicodeString(&Name, L"sbx_ac_child_named");
                InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_CASE_INSENSITIVE, Directory, NULL);
                Status = NtOpenEvent(&Named, EVENT_ALL_ACCESS, &ObjectAttributes);
            }
            ok(NT_SUCCESS(Status), "child named object not redirected into the AppContainer directory (0x%lx)\n", Status);
            if (Named) CloseHandle(Named);
        }
        ExitCode = SbxWaitChild(&Info);
        ok(ExitCode == 0, "%s child failed with 0x%lx\n", Lpac ? "LPAC" : "AppContainer", ExitCode);
    }

Cleanup:
    if (Attributes) { DeleteProcThreadAttributeList(Attributes); HeapFree(GetProcessHeap(), 0, Attributes); }
    if (Directory) NtClose(Directory);
    if (UserOnly) CloseHandle(UserOnly);
    if (PackageEvent) CloseHandle(PackageEvent);
    if (AllPkg) CloseHandle(AllPkg);
    if (RestrictedPkg) CloseHandle(RestrictedPkg);
    if (PackageString) LocalFree(PackageString);
    if (UserString) LocalFree(UserString);
    if (Capability) FreeSid(Capability);
    if (Package) LocalFree(Package);
    if (Application[0]) DeleteFileW(Application);
    if (StageDirectory[0]) RemoveDirectoryW(StageDirectory);
}

static LPPROC_THREAD_ATTRIBUTE_LIST
CreateTortureAttributes(
    _In_ PSECURITY_CAPABILITIES Capabilities,
    _In_ BOOL Lpac,
    _In_ PDWORD OptOut,
    _In_ PHANDLE Job,
    _In_ PULONGLONG Mitigations)
{
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes;
    SIZE_T Size = 0;
    DWORD Count = Lpac ? 4 : 3;

    InitializeProcThreadAttributeList(NULL, Count, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    if (!Attributes ||
        !InitializeProcThreadAttributeList(Attributes, Count, 0, &Size))
    {
        if (Attributes) HeapFree(GetProcessHeap(), 0, Attributes);
        return NULL;
    }
    if (!UpdateProcThreadAttribute(Attributes, 0,
                                   PROC_THREAD_ATTRIBUTE_SECURITY_CAPABILITIES,
                                   Capabilities, sizeof(*Capabilities), NULL, NULL) ||
        !UpdateProcThreadAttribute(Attributes, 0,
                                   PROC_THREAD_ATTRIBUTE_JOB_LIST,
                                   Job, sizeof(*Job), NULL, NULL) ||
        !UpdateProcThreadAttribute(Attributes, 0,
                                   PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY,
                                   Mitigations, sizeof(*Mitigations), NULL, NULL) ||
        (Lpac &&
         !UpdateProcThreadAttribute(Attributes, 0,
                                    PROC_THREAD_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY,
                                    OptOut, sizeof(*OptOut), NULL, NULL)))
    {
        DeleteProcThreadAttributeList(Attributes);
        HeapFree(GetProcessHeap(), 0, Attributes);
        return NULL;
    }
    return Attributes;
}

static VOID
FreeTortureAttributes(_In_opt_ LPPROC_THREAD_ATTRIBUTE_LIST Attributes)
{
    if (!Attributes) return;
    DeleteProcThreadAttributeList(Attributes);
    HeapFree(GetProcessHeap(), 0, Attributes);
}

static VOID
TestConcurrentCompositeChildren(PSID Package)
{
    PSID Capability = NULL;
    SID_AND_ATTRIBUTES CapabilityAttr;
    SECURITY_CAPABILITIES Capabilities;
    LPPROC_THREAD_ATTRIBUTE_LIST NormalAttributes = NULL, LpacAttributes = NULL;
    PROCESS_INFORMATION Children[APPCONTAINER_TORTURE_CHILDREN];
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION JobLimits;
    SECURITY_ATTRIBUTES DesktopAttributes;
    PSECURITY_DESCRIPTOR DesktopSd = NULL;
    HANDLE Job = NULL, UserOnly = NULL, PackageEvent = NULL;
    HANDLE AllPkg = NULL, RestrictedPkg = NULL;
    HDESK Desktop = NULL;
    HWINSTA Station;
    LPWSTR UserString = UserSidString(), PackageString = NULL;
    WCHAR Application[MAX_PATH] = L"", StageDirectory[MAX_PATH] = L"";
    WCHAR DesktopName[64], StationName[128], FullDesktopName[260];
    WCHAR Sddl[512], CommandLine[MAX_PATH * 2];
    DWORD OptOut = PROCESS_CREATION_ALL_APPLICATION_PACKAGES_OPT_OUT;
    DWORD NameLength, ExitCode;
    ULONGLONG Mitigations =
        PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE |
        PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON |
        PROCESS_CREATION_MITIGATION_POLICY_PROHIBIT_DYNAMIC_CODE_ALWAYS_ON;
    ULONG Index;

    ZeroMemory(Children, sizeof(Children));
    Capability = CapabilitySid(SECURITY_CAPABILITY_INTERNET_CLIENT);
    if (!Package || !Capability || !UserString) goto Cleanup;
    if (!ConvertSidToStringSidW(Package, &PackageString)) goto Cleanup;
    ok(StageAppContainerImage(Package, UserString, Application,
                              ARRAYSIZE(Application), StageDirectory,
                              ARRAYSIZE(StageDirectory)),
       "failed to stage the composite AppContainer image: %lu\n", GetLastError());
    if (!Application[0]) goto Cleanup;

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl),
                     L"D:(A;;GA;;;%s)S:(ML;;NW;;;LW)", UserString);
    UserOnly = CreateEventWithSddl(L"Global\\sbx_ac_child_useronly", Sddl);
    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl),
                     L"D:(A;;GA;;;%s)(A;;GA;;;%s)S:(ML;;NW;;;LW)",
                     UserString, PackageString);
    PackageEvent = CreateEventWithSddl(L"Global\\sbx_ac_child_package", Sddl);
    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl),
                     L"D:(A;;GA;;;%s)(A;;GA;;;S-1-15-2-1)S:(ML;;NW;;;LW)",
                     UserString);
    AllPkg = CreateEventWithSddl(L"Global\\sbx_ac_child_allpkg", Sddl);
    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl),
                     L"D:(A;;GA;;;%s)(A;;GA;;;S-1-15-2-2)S:(ML;;NW;;;LW)",
                     UserString);
    RestrictedPkg = CreateEventWithSddl(L"Global\\sbx_ac_child_restrictedpkg", Sddl);
    ok(UserOnly && PackageEvent && AllPkg && RestrictedPkg,
       "composite AppContainer event setup failed %lu\n", GetLastError());
    if (!UserOnly || !PackageEvent || !AllPkg || !RestrictedPkg) goto Cleanup;

    StringCchPrintfW(DesktopName, ARRAYSIZE(DesktopName),
                     L"SbxAcDesktop_%08lx", GetCurrentProcessId());
    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl),
                     L"D:P(A;;GA;;;%s)(A;;GA;;;%s)(A;;GA;;;S-1-15-2-1)"
                     L"S:(ML;;NW;;;LW)", UserString, PackageString);
    DesktopSd = SbxSdFromSddl(Sddl);
    DesktopAttributes.nLength = sizeof(DesktopAttributes);
    DesktopAttributes.lpSecurityDescriptor = DesktopSd;
    DesktopAttributes.bInheritHandle = FALSE;
    Desktop = CreateDesktopW(DesktopName, NULL, NULL, 0, GENERIC_ALL,
                             &DesktopAttributes);
    ok(Desktop != NULL, "composite alternate desktop creation failed %lu\n",
       GetLastError());
    if (!Desktop) goto Cleanup;
    Station = GetProcessWindowStation();
    ok(Station && GetUserObjectInformationW(Station, UOI_NAME, StationName,
                                             sizeof(StationName), &NameLength),
       "window station name query failed %lu\n", GetLastError());
    if (!Station || !StationName[0]) goto Cleanup;
    StringCchPrintfW(FullDesktopName, ARRAYSIZE(FullDesktopName), L"%s\\%s",
                     StationName, DesktopName);

    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "composite AppContainer job creation failed %lu\n",
       GetLastError());
    if (!Job) goto Cleanup;
    ZeroMemory(&JobLimits, sizeof(JobLimits));
    JobLimits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE | JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    JobLimits.BasicLimitInformation.ActiveProcessLimit =
        APPCONTAINER_TORTURE_CHILDREN + 1;
    ok(SetInformationJobObject(Job, JobObjectExtendedLimitInformation,
                               &JobLimits, sizeof(JobLimits)),
       "composite AppContainer job limits failed %lu\n", GetLastError());

    CapabilityAttr.Sid = Capability;
    CapabilityAttr.Attributes = SE_GROUP_ENABLED;
    Capabilities.AppContainerSid = Package;
    Capabilities.Capabilities = &CapabilityAttr;
    Capabilities.CapabilityCount = 1;
    Capabilities.Reserved = 0;
    NormalAttributes = CreateTortureAttributes(&Capabilities, FALSE, &OptOut,
                                                &Job, &Mitigations);
    LpacAttributes = CreateTortureAttributes(&Capabilities, TRUE, &OptOut,
                                              &Job, &Mitigations);
    ok(NormalAttributes && LpacAttributes,
       "composite AppContainer attribute construction failed %lu\n",
       GetLastError());
    if (!NormalAttributes || !LpacAttributes) goto Cleanup;

    for (Index = 0; Index < APPCONTAINER_TORTURE_CHILDREN; Index++)
    {
        STARTUPINFOEXW Startup;
        BOOL Lpac = (Index & 1) != 0;

        ZeroMemory(&Startup, sizeof(Startup));
        Startup.StartupInfo.cb = sizeof(Startup);
        Startup.StartupInfo.lpDesktop = FullDesktopName;
        Startup.lpAttributeList = Lpac ? LpacAttributes : NormalAttributes;
        StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine),
                         L"\"%s\" AppContainer child %s %s", Application,
                         Lpac ? L"lpacdesk" : L"acdesk", DesktopName);
        ok(CreateProcessW(Application, CommandLine, NULL, NULL, FALSE,
                          EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
                          NULL, StageDirectory, &Startup.StartupInfo,
                          &Children[Index]),
           "composite AppContainer child %lu launch failed %lu\n", Index,
           GetLastError());
    }
    for (Index = 0; Index < APPCONTAINER_TORTURE_CHILDREN; Index++)
    {
        if (!Children[Index].hProcess) continue;
        ExitCode = SbxWaitChild(&Children[Index]);
        ok(ExitCode == 0,
           "composite %s child %lu failed with 0x%lx\n",
           (Index & 1) ? "LPAC" : "AppContainer", Index, ExitCode);
    }

Cleanup:
    FreeTortureAttributes(LpacAttributes);
    FreeTortureAttributes(NormalAttributes);
    if (Job) CloseHandle(Job);
    if (Desktop) CloseDesktop(Desktop);
    if (DesktopSd) LocalFree(DesktopSd);
    if (UserOnly) CloseHandle(UserOnly);
    if (PackageEvent) CloseHandle(PackageEvent);
    if (AllPkg) CloseHandle(AllPkg);
    if (RestrictedPkg) CloseHandle(RestrictedPkg);
    if (PackageString) LocalFree(PackageString);
    if (UserString) LocalFree(UserString);
    if (Capability) FreeSid(Capability);
    if (Application[0]) DeleteFileW(Application);
    if (StageDirectory[0]) RemoveDirectoryW(StageDirectory);
}

START_TEST(AppContainer)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);
    PSID ProfileSid = NULL;
    HRESULT hr;

    pNtCreateLowBoxToken = (PFN_NtCreateLowBoxToken)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateLowBoxToken");

    if (SbxIsChild(Arguments, Count, "ac"))
        ExitProcess(RunChild(FALSE, NULL));
    if (SbxIsChild(Arguments, Count, "lpac"))
        ExitProcess(RunChild(TRUE, NULL));
    if (SbxIsChild(Arguments, Count, "acdesk") && Count >= 5)
        ExitProcess(RunChild(FALSE, Arguments[4]));
    if (SbxIsChild(Arguments, Count, "lpacdesk") && Count >= 5)
        ExitProcess(RunChild(TRUE, Arguments[4]));

    ok(pNtCreateLowBoxToken != NULL, "NtCreateLowBoxToken not exported\n");
    TestDerive();
    TestProfile();
    if (pNtCreateLowBoxToken)
    {
        TestLowBoxToken();
        TestSavedHandleLifetime();
        DeleteAppContainerProfile(PACKAGE_NAME);
        hr = CreateAppContainerProfile(PACKAGE_NAME, L"Sandbox Test", L"apitest", NULL, 0, &ProfileSid);
        ok(hr == S_OK && ProfileSid != NULL, "child AppContainer profile creation failed 0x%lx\n", hr);
        if (ProfileSid)
        {
            TestRegistryLocationAsLowBox(ProfileSid);
            TestChild(FALSE);
            TestChild(TRUE);
            TestConcurrentCompositeChildren(ProfileSid);
            LocalFree(ProfileSid);
        }
        hr = DeleteAppContainerProfile(PACKAGE_NAME);
        ok(hr == S_OK, "child AppContainer profile deletion failed 0x%lx\n", hr);
    }
}
