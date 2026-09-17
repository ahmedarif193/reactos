/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Chromium/Firefox alternate window-station and desktop isolation
 */

#include "precomp.h"

#define CHILD_WINSTATION_MISMATCH 0x01
#define CHILD_DESKTOP_MISMATCH 0x02
#define CHILD_WINDOW_FAILED 0x04
#define CHILD_SD_QUERY_FAILED 0x08

#define DESKTOP_TORTURE_CHILDREN 4

static PSECURITY_DESCRIPTOR
QueryUserObjectSecurityDescriptor(HANDLE Object)
{
    SECURITY_INFORMATION Information = DACL_SECURITY_INFORMATION;
    PSECURITY_DESCRIPTOR Descriptor;
    DWORD Length = 0;

    SetLastError(0xdeadbeef);
    GetUserObjectSecurity(Object, &Information, NULL, 0, &Length);
    if (!Length || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return NULL;
    Descriptor = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Descriptor)
        return NULL;
    if (!GetUserObjectSecurity(Object, &Information, Descriptor, Length, &Length))
    {
        HeapFree(GetProcessHeap(), 0, Descriptor);
        return NULL;
    }
    return Descriptor;
}

static BOOL
GetUserObjectName(HANDLE Object, PWSTR Name, DWORD NameCount)
{
    DWORD Length;

    return GetUserObjectInformationW(Object, UOI_NAME, Name,
                                     NameCount * sizeof(WCHAR), &Length);
}

static BOOL
AddRestrictedDesktopDenyAce(HDESK Desktop)
{
    static const ACCESS_MASK DenyMask =
        WRITE_DAC | WRITE_OWNER | DELETE | DESKTOP_CREATEMENU |
        DESKTOP_CREATEWINDOW | DESKTOP_HOOKCONTROL | DESKTOP_JOURNALPLAYBACK |
        DESKTOP_JOURNALRECORD | DESKTOP_SWITCHDESKTOP;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PSID Restricted = NULL;
    PACL OldDacl = NULL, NewDacl = NULL;
    EXPLICIT_ACCESSW Entry;
    DWORD Error;
    BOOL Result = FALSE;

    Error = GetSecurityInfo(Desktop, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION,
                            NULL, NULL, &OldDacl, NULL, &Descriptor);
    if (Error != ERROR_SUCCESS)
        return FALSE;
    if (!AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_RESTRICTED_CODE_RID,
                                  0, 0, 0, 0, 0, 0, 0, &Restricted))
        goto Cleanup;
    ZeroMemory(&Entry, sizeof(Entry));
    Entry.grfAccessPermissions = DenyMask;
    Entry.grfAccessMode = DENY_ACCESS;
    Entry.grfInheritance = NO_INHERITANCE;
    Entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    Entry.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    Entry.Trustee.ptstrName = (LPWSTR)Restricted;
    Error = SetEntriesInAclW(1, &Entry, OldDacl, &NewDacl);
    if (Error != ERROR_SUCCESS)
        goto Cleanup;
    Error = SetSecurityInfo(Desktop, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION,
                            NULL, NULL, NewDacl, NULL);
    Result = Error == ERROR_SUCCESS;

Cleanup:
    if (NewDacl) LocalFree(NewDacl);
    if (Restricted) FreeSid(Restricted);
    if (Descriptor) LocalFree(Descriptor);
    return Result;
}

static BOOL
HasRestrictedDesktopDenyAce(HDESK Desktop)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PSID Restricted = NULL;
    PACL Dacl = NULL;
    DWORD Error, Index;
    BOOL Found = FALSE;

    Error = GetSecurityInfo(Desktop, SE_WINDOW_OBJECT, DACL_SECURITY_INFORMATION,
                            NULL, NULL, &Dacl, NULL, &Descriptor);
    if (Error != ERROR_SUCCESS)
        return FALSE;
    if (!AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_RESTRICTED_CODE_RID,
                                  0, 0, 0, 0, 0, 0, 0, &Restricted))
        goto Cleanup;
    for (Index = 0; Dacl && Index < Dacl->AceCount; Index++)
    {
        PACE_HEADER Header;
        PACCESS_DENIED_ACE Ace;

        if (!GetAce(Dacl, Index, (PVOID *)&Header))
            break;
        if (Header->AceType != ACCESS_DENIED_ACE_TYPE)
            continue;
        Ace = (PACCESS_DENIED_ACE)Header;
        if (EqualSid(&Ace->SidStart, Restricted) &&
            (Ace->Mask & DESKTOP_SWITCHDESKTOP))
        {
            Found = TRUE;
            break;
        }
    }

Cleanup:
    if (Restricted) FreeSid(Restricted);
    if (Descriptor) LocalFree(Descriptor);
    return Found;
}

static DWORD
RunDesktopChild(PCSTR ExpectedStationA, PCSTR ExpectedDesktopA)
{
    DWORD Failures = 0;
    HWINSTA Station;
    HDESK Desktop;
    HWND Window;
    WCHAR Name[128];
    WCHAR ExpectedStation[128], ExpectedDesktop[128];
    SECURITY_INFORMATION Information = DACL_SECURITY_INFORMATION;
    DWORD Length = 0;

    if (!MultiByteToWideChar(CP_ACP, 0, ExpectedStationA, -1,
                             ExpectedStation, ARRAYSIZE(ExpectedStation)) ||
        !MultiByteToWideChar(CP_ACP, 0, ExpectedDesktopA, -1,
                             ExpectedDesktop, ARRAYSIZE(ExpectedDesktop)))
        return CHILD_WINSTATION_MISMATCH | CHILD_DESKTOP_MISMATCH;

    Station = GetProcessWindowStation();
    if (!Station || !GetUserObjectName(Station, Name, ARRAYSIZE(Name)) ||
        _wcsicmp(Name, ExpectedStation))
    {
        SbxChildFail(&Failures, CHILD_WINSTATION_MISMATCH,
                     "process window station mismatch", GetLastError());
    }

    Desktop = GetThreadDesktop(GetCurrentThreadId());
    if (!Desktop || !GetUserObjectName(Desktop, Name, ARRAYSIZE(Name)) ||
        _wcsicmp(Name, ExpectedDesktop))
    {
        SbxChildFail(&Failures, CHILD_DESKTOP_MISMATCH,
                     "thread desktop mismatch", GetLastError());
    }

    SetLastError(0xdeadbeef);
    GetUserObjectSecurity(Desktop, &Information, NULL, 0, &Length);
    if (!Length || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
    {
        SbxChildFail(&Failures, CHILD_SD_QUERY_FAILED,
                     "desktop security descriptor size query", GetLastError());
    }

    Window = CreateWindowExW(0, L"STATIC", L"sandbox alternate desktop",
                             WS_OVERLAPPED, 0, 0, 20, 20, NULL, NULL,
                             GetModuleHandleW(NULL), NULL);
    if (!Window)
        SbxChildFail(&Failures, CHILD_WINDOW_FAILED,
                     "window creation on alternate desktop", GetLastError());
    else
        DestroyWindow(Window);
    return Failures;
}

static BOOL
SpawnDesktopChild(PCWSTR FullDesktopName, PCWSTR StationName,
                  PCWSTR DesktopName, PPROCESS_INFORMATION Info)
{
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH * 2];
    STARTUPINFOW Startup;

    if (!GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)))
        return FALSE;
    if (FAILED(StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine),
                                L"\"%s\" Desktop child target %s %s",
                                Application, StationName, DesktopName)))
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        return FALSE;
    }
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    Startup.lpDesktop = (PWSTR)FullDesktopName;
    ZeroMemory(Info, sizeof(*Info));
    return CreateProcessW(Application, CommandLine, NULL, NULL, FALSE,
                          CREATE_UNICODE_ENVIRONMENT, NULL, NULL, &Startup, Info);
}

static void
TestAlternateWindowStation(void)
{
    HWINSTA OriginalStation, Station = NULL;
    HDESK OriginalDesktop, Desktop = NULL;
    PSECURITY_DESCRIPTOR StationSd = NULL, DesktopSd = NULL;
    SECURITY_ATTRIBUTES StationAttributes, DesktopAttributes;
    WCHAR RequestedStation[64], RequestedDesktop[64];
    WCHAR StationName[128], DesktopName[128], FullDesktopName[260];
    PROCESS_INFORMATION Children[DESKTOP_TORTURE_CHILDREN];
    DWORD ExitCode, LabelPolicy;
    ULONG Index;

    OriginalStation = GetProcessWindowStation();
    OriginalDesktop = GetThreadDesktop(GetCurrentThreadId());
    ok(OriginalStation != NULL && OriginalDesktop != NULL,
       "default station/desktop unavailable %lu\n", GetLastError());
    if (!OriginalStation || !OriginalDesktop) return;

    StationSd = QueryUserObjectSecurityDescriptor(OriginalStation);
    DesktopSd = QueryUserObjectSecurityDescriptor(OriginalDesktop);
    ok(StationSd != NULL, "window-station DACL query failed %lu\n", GetLastError());
    ok(DesktopSd != NULL, "desktop DACL query failed %lu\n", GetLastError());
    if (!StationSd || !DesktopSd) goto Cleanup;

    StringCchPrintfW(RequestedStation, ARRAYSIZE(RequestedStation),
                     L"SbxStation_%08lx", GetCurrentProcessId());
    StringCchPrintfW(RequestedDesktop, ARRAYSIZE(RequestedDesktop),
                     L"SbxDesktop_%08lx", GetCurrentProcessId());
    StationAttributes.nLength = sizeof(StationAttributes);
    StationAttributes.lpSecurityDescriptor = StationSd;
    StationAttributes.bInheritHandle = FALSE;
    Station = CreateWindowStationW(RequestedStation, 0,
                                   GENERIC_READ | WINSTA_CREATEDESKTOP |
                                   READ_CONTROL | WRITE_DAC,
                                   &StationAttributes);
    ok(Station != NULL, "CreateWindowStation failed %lu\n", GetLastError());
    if (!Station) goto Cleanup;
    ok(GetUserObjectName(Station, StationName, ARRAYSIZE(StationName)),
       "window-station name query failed %lu\n", GetLastError());

    ok(SetProcessWindowStation(Station),
       "SetProcessWindowStation(alternate) failed %lu\n", GetLastError());
    if (GetProcessWindowStation() != Station) goto Cleanup;
    DesktopAttributes.nLength = sizeof(DesktopAttributes);
    DesktopAttributes.lpSecurityDescriptor = DesktopSd;
    DesktopAttributes.bInheritHandle = FALSE;
    Desktop = CreateDesktopW(RequestedDesktop, NULL, NULL, 0,
                             DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS |
                             READ_CONTROL | WRITE_DAC | WRITE_OWNER,
                             &DesktopAttributes);
    ok(Desktop != NULL, "CreateDesktop on alternate station failed %lu\n", GetLastError());
    ok(SetProcessWindowStation(OriginalStation),
       "restoring the process window station failed %lu\n", GetLastError());
    if (!Desktop) goto Cleanup;

    ok(GetUserObjectName(Desktop, DesktopName, ARRAYSIZE(DesktopName)),
       "desktop name query failed %lu\n", GetLastError());
    ok(!_wcsicmp(StationName, RequestedStation), "station name %S != %S\n",
       StationName, RequestedStation);
    ok(!_wcsicmp(DesktopName, RequestedDesktop), "desktop name %S != %S\n",
       DesktopName, RequestedDesktop);
    StringCchPrintfW(FullDesktopName, ARRAYSIZE(FullDesktopName), L"%s\\%s",
                     StationName, DesktopName);

    ok(AddRestrictedDesktopDenyAce(Desktop),
       "failed to add the browser restricted-SID desktop deny ACE: %lu\n",
       GetLastError());
    ok(HasRestrictedDesktopDenyAce(Desktop),
       "browser restricted-SID deny ACE is missing from the alternate desktop\n");
    ok(SbxSetLabel(Desktop, SE_WINDOW_OBJECT, SECURITY_MANDATORY_LOW_RID,
                   SYSTEM_MANDATORY_LABEL_NO_WRITE_UP) == ERROR_SUCCESS,
       "alternate desktop label failed %lu\n", GetLastError());
    ok(SbxQueryLabelRid(Desktop, SE_WINDOW_OBJECT, &LabelPolicy) ==
           SECURITY_MANDATORY_LOW_RID,
       "alternate desktop did not retain its low label\n");
    ok(LabelPolicy == SYSTEM_MANDATORY_LABEL_NO_WRITE_UP,
       "alternate desktop label policy 0x%lx\n", LabelPolicy);

    ZeroMemory(Children, sizeof(Children));
    for (Index = 0; Index < DESKTOP_TORTURE_CHILDREN; Index++)
    {
        ok(SpawnDesktopChild(FullDesktopName, StationName, DesktopName,
                             &Children[Index]),
           "alternate desktop child %lu launch failed %lu\n", Index,
           GetLastError());
    }
    for (Index = 0; Index < DESKTOP_TORTURE_CHILDREN; Index++)
    {
        if (!Children[Index].hProcess) continue;
        ExitCode = SbxWaitChild(&Children[Index]);
        ok(ExitCode == 0, "alternate desktop child %lu failed with 0x%lx\n",
           Index, ExitCode);
    }

Cleanup:
    if (GetProcessWindowStation() != OriginalStation)
        SetProcessWindowStation(OriginalStation);
    if (Desktop) CloseDesktop(Desktop);
    if (Station) CloseWindowStation(Station);
    if (DesktopSd) HeapFree(GetProcessHeap(), 0, DesktopSd);
    if (StationSd) HeapFree(GetProcessHeap(), 0, StationSd);
}

START_TEST(Desktop)
{
    char **Arguments;
    int Count = winetest_get_mainargs(&Arguments);

    if (SbxIsChild(Arguments, Count, "target") && Count >= 6)
        ExitProcess(RunDesktopChild(Arguments[4], Arguments[5]));

    TestAlternateWindowStation();
}
