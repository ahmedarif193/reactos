/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the restricted token, job and process launch sequence used by browser sandboxes
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <sddl.h>
#include <aclapi.h>
#include <strsafe.h>

#ifndef PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE
#define PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE 0x01ULL
#define PROCESS_CREATION_MITIGATION_POLICY_DEP_ATL_THUNK_ENABLE 0x02ULL
#define PROCESS_CREATION_MITIGATION_POLICY_SEHOP_ENABLE 0x04ULL
#define PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON (0x01ULL << 12)
#define PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON (0x01ULL << 16)
#define PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON (0x01ULL << 32)
#define PROCESS_CREATION_MITIGATION_POLICY_FONT_DISABLE_ALWAYS_ON (0x01ULL << 48)
#define PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_REMOTE_ALWAYS_ON (0x01ULL << 52)
#define PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON (0x01ULL << 56)
#endif

#define CHILD_TIMEOUT_MS 20000
#define CHILD_OK 0
#define CHILD_NO_THREAD_TOKEN 10
#define CHILD_THREAD_TOKEN_NOT_LOW 11
#define CHILD_REVERT_FAILED 12
#define CHILD_NO_PROCESS_TOKEN 13
#define CHILD_PROCESS_TOKEN_NOT_UNTRUSTED 14
#define CHILD_PROCESS_TOKEN_NOT_LOW 15
#define CHILD_LOWER_FAILED 16

#define PROCESS_MITIGATION_OPTIONS_MASK_POLICY 5
#define MITIGATION_GUARD_BYTES 8
#define MITIGATION_MAX_QUERY_BYTES 24

typedef BOOL
(WINAPI *PGET_PROCESS_MITIGATION_POLICY)(
    _In_ HANDLE Process,
    _In_ DWORD Policy,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ SIZE_T Length);

typedef struct _MITIGATION_QUERY_CASE
{
    SIZE_T Length;
    SIZE_T Offset;
} MITIGATION_QUERY_CASE;

static BOOL
AllBytesEqual(
    _In_reads_bytes_(Length) const BYTE *Buffer,
    _In_ SIZE_T Length,
    _In_ BYTE Value)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; ++Index)
    {
        if (Buffer[Index] != Value)
            return FALSE;
    }
    return TRUE;
}

static BOOL
MitigationMaskIsWellFormed(
    _In_reads_bytes_(Length) const BYTE *Buffer,
    _In_ SIZE_T Length)
{
    SIZE_T Index;

    /* Each mitigation occupies a nibble whose low two bits are the support mask. */
    for (Index = 0; Index < Length; ++Index)
    {
        if (Buffer[Index] & 0xCC)
            return FALSE;
    }
    return TRUE;
}

static void
TestMitigationOptionsMask(void)
{
    static const SIZE_T InvalidLengths[] = { 0, 1, 7 };
    static const MITIGATION_QUERY_CASE ValidCases[] =
    {
        { 8, MITIGATION_GUARD_BYTES },
        { 9, MITIGATION_GUARD_BYTES },
        { 15, MITIGATION_GUARD_BYTES },
        { 16, MITIGATION_GUARD_BYTES },
        { 17, MITIGATION_GUARD_BYTES },
        { 24, MITIGATION_GUARD_BYTES },
        { 16, MITIGATION_GUARD_BYTES + 1 },
    };
    PGET_PROCESS_MITIGATION_POLICY GetPolicy;
    BYTE First[MITIGATION_GUARD_BYTES + 1 + MITIGATION_MAX_QUERY_BYTES + MITIGATION_GUARD_BYTES];
    BYTE Second[sizeof(First)];
    BYTE Mask8[8] = { 0 }, Mask16[16] = { 0 };
    BYTE *FirstOutput, *SecondOutput;
    HANDLE Self;
    HMODULE Kernel32;
    DWORD Error;
    SIZE_T Index, Length, Offset;
    BOOL FirstSuccess, SecondSuccess;

    Kernel32 = GetModuleHandleW(L"kernel32.dll");
    GetPolicy = (PGET_PROCESS_MITIGATION_POLICY)GetProcAddress(Kernel32,
                                                               "GetProcessMitigationPolicy");
    ok(GetPolicy != NULL, "GetProcessMitigationPolicy is unavailable, error %lu\n", GetLastError());
    if (!GetPolicy) return;

    for (Index = 0; Index < ARRAYSIZE(InvalidLengths); ++Index)
    {
        memset(First, 0xCC, sizeof(First));
        SetLastError(0xDEADBEEF);
        FirstSuccess = GetPolicy(GetCurrentProcess(),
                                 PROCESS_MITIGATION_OPTIONS_MASK_POLICY,
                                 First + MITIGATION_GUARD_BYTES,
                                 InvalidLengths[Index]);
        Error = GetLastError();
        ok(!FirstSuccess, "length %Iu unexpectedly succeeded\n", InvalidLengths[Index]);
        ok(Error == ERROR_INVALID_PARAMETER,
           "length %Iu returned error %lu\n", InvalidLengths[Index], Error);
        ok(AllBytesEqual(First, sizeof(First), 0xCC),
           "length %Iu modified the guarded buffer\n", InvalidLengths[Index]);
    }

    for (Index = 0; Index < ARRAYSIZE(ValidCases); ++Index)
    {
        Length = ValidCases[Index].Length;
        Offset = ValidCases[Index].Offset;
        FirstOutput = First + Offset;
        SecondOutput = Second + Offset;
        memset(First, 0xCC, sizeof(First));
        memset(Second, 0x55, sizeof(Second));

        FirstSuccess = GetPolicy(GetCurrentProcess(),
                                 PROCESS_MITIGATION_OPTIONS_MASK_POLICY,
                                 FirstOutput,
                                 Length);
        SecondSuccess = GetPolicy(GetCurrentProcess(),
                                  PROCESS_MITIGATION_OPTIONS_MASK_POLICY,
                                  SecondOutput,
                                  Length);
        ok(FirstSuccess && SecondSuccess,
           "length %Iu offset %Iu failed with %lu\n", Length, Offset, GetLastError());
        if (!FirstSuccess || !SecondSuccess)
            continue;

        ok(AllBytesEqual(First, Offset, 0xCC) &&
           AllBytesEqual(Second, Offset, 0x55),
           "length %Iu offset %Iu wrote before the output\n", Length, Offset);
        ok(AllBytesEqual(FirstOutput + Length, sizeof(First) - Offset - Length, 0xCC) &&
           AllBytesEqual(SecondOutput + Length, sizeof(Second) - Offset - Length, 0x55),
           "length %Iu offset %Iu wrote beyond the output\n", Length, Offset);
        ok(!memcmp(FirstOutput, SecondOutput, Length),
           "length %Iu offset %Iu left input-dependent output\n", Length, Offset);
        ok(MitigationMaskIsWellFormed(FirstOutput, Length),
           "length %Iu offset %Iu returned reserved mask bits\n", Length, Offset);

        if (Length == sizeof(Mask8) && Offset == MITIGATION_GUARD_BYTES)
            memcpy(Mask8, FirstOutput, sizeof(Mask8));
        if (Length == sizeof(Mask16) && Offset == MITIGATION_GUARD_BYTES)
            memcpy(Mask16, FirstOutput, sizeof(Mask16));
    }

    ok(!memcmp(Mask8, Mask16, sizeof(Mask8)),
       "eight- and sixteen-byte queries disagree on the first mask\n");

    Self = OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, GetCurrentProcessId());
    ok(Self != NULL, "OpenProcess(self) failed with %lu\n", GetLastError());
    if (Self)
    {
        memset(First, 0xCC, sizeof(First));
        FirstOutput = First + MITIGATION_GUARD_BYTES;
        FirstSuccess = GetPolicy(Self,
                                 PROCESS_MITIGATION_OPTIONS_MASK_POLICY,
                                 FirstOutput,
                                 sizeof(Mask16));
        ok(FirstSuccess, "real-handle query failed with %lu\n", GetLastError());
        ok(FirstSuccess && !memcmp(FirstOutput, Mask16, sizeof(Mask16)),
           "real-handle query disagrees with the pseudo-handle query\n");
        ok(AllBytesEqual(First, MITIGATION_GUARD_BYTES, 0xCC) &&
           AllBytesEqual(FirstOutput + sizeof(Mask16),
                         sizeof(First) - MITIGATION_GUARD_BYTES - sizeof(Mask16),
                         0xCC),
           "real-handle query wrote outside its output\n");
        CloseHandle(Self);
    }

    memset(First, 0xCC, sizeof(First));
    SetLastError(0xDEADBEEF);
    FirstSuccess = GetPolicy(GetCurrentProcess(),
                             0xFFFFFFFF,
                             First + MITIGATION_GUARD_BYTES,
                             sizeof(Mask16));
    Error = GetLastError();
    ok(!FirstSuccess, "invalid mitigation policy unexpectedly succeeded\n");
    ok(Error == ERROR_INVALID_PARAMETER,
       "invalid mitigation policy returned error %lu\n", Error);
    ok(AllBytesEqual(First, sizeof(First), 0xCC),
       "invalid mitigation policy modified the guarded buffer\n");

    memset(First, 0xCC, sizeof(First));
    SetLastError(0xDEADBEEF);
    FirstSuccess = GetPolicy(INVALID_HANDLE_VALUE,
                             PROCESS_MITIGATION_OPTIONS_MASK_POLICY,
                             First + MITIGATION_GUARD_BYTES,
                             sizeof(Mask16));
    ok(FirstSuccess, "invalid-handle query failed with %lu\n", GetLastError());
    ok(FirstSuccess &&
       !memcmp(First + MITIGATION_GUARD_BYTES, Mask16, sizeof(Mask16)),
       "invalid-handle query disagrees with the current-process query\n");
    ok(AllBytesEqual(First, MITIGATION_GUARD_BYTES, 0xCC) &&
       AllBytesEqual(First + MITIGATION_GUARD_BYTES + sizeof(Mask16),
                     sizeof(First) - MITIGATION_GUARD_BYTES - sizeof(Mask16),
                     0xCC),
       "invalid-handle query wrote outside its output\n");
}

static DWORD
GetIntegrityRid(
    _In_ HANDLE Token)
{
    BYTE Buffer[SECURITY_MAX_SID_SIZE + sizeof(TOKEN_MANDATORY_LABEL)];
    PTOKEN_MANDATORY_LABEL Label = (PTOKEN_MANDATORY_LABEL)Buffer;
    DWORD Length;

    if (!GetTokenInformation(Token, TokenIntegrityLevel, Buffer, sizeof(Buffer), &Length))
        return 0xFFFFFFFF;

    return *GetSidSubAuthority(Label->Label.Sid, *GetSidSubAuthorityCount(Label->Label.Sid) - 1);
}

static BOOL
SetIntegrity(
    _In_ HANDLE Token,
    _In_ DWORD Rid,
    _In_ PCSTR Tag)
{
    SID_IDENTIFIER_AUTHORITY Authority = SECURITY_MANDATORY_LABEL_AUTHORITY;
    TOKEN_MANDATORY_LABEL Label;
    PSID Sid;
    BOOL Success;

    if (!AllocateAndInitializeSid(&Authority, 1, Rid, 0, 0, 0, 0, 0, 0, 0, &Sid))
    {
        if (Tag) ok(FALSE, "%s: AllocateAndInitializeSid(integrity) failed with %lu\n", Tag, GetLastError());
        return FALSE;
    }

    Label.Label.Attributes = SE_GROUP_INTEGRITY;
    Label.Label.Sid = Sid;
    Success = SetTokenInformation(Token, TokenIntegrityLevel, &Label, sizeof(Label) + GetLengthSid(Sid));
    if (Tag) ok(Success, "%s: SetTokenInformation(TokenIntegrityLevel %lu) failed with %lu\n", Tag, Rid, GetLastError());
    FreeSid(Sid);
    return Success;
}

static void
ChildReport(
    _In_ PCSTR Format,
    ...)
{
    CHAR Line[1024];
    DWORD Written;
    va_list Args;

    va_start(Args, Format);
    StringCchVPrintfA(Line, sizeof(Line), Format, Args);
    va_end(Args);
    WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), Line, (DWORD)strlen(Line), &Written, NULL);
}

static void
ChildReportTokenSecurity(
    _In_ HANDLE Token,
    _In_ PCSTR Tag)
{
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    LPWSTR Sddl = NULL;
    DWORD Error;

    Error = GetSecurityInfo(Token, SE_KERNEL_OBJECT, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, NULL, &Descriptor);
    if (Error != ERROR_SUCCESS)
    {
        ChildReport("CHILD %s: GetSecurityInfo failed %lu\n", Tag, Error);
        return;
    }
    if (ConvertSecurityDescriptorToStringSecurityDescriptorW(Descriptor, SDDL_REVISION_1, OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION, &Sddl, NULL))
    {
        ChildReport("CHILD %s: %S\n", Tag, Sddl);
        LocalFree(Sddl);
    }
    LocalFree(Descriptor);
}

static int
RunChild(void)
{
    HANDLE Token;
    DWORD Rid;

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, FALSE, &Token))
    {
        ChildReport("CHILD OpenThreadToken failed %lu\n", GetLastError());
        return CHILD_NO_THREAD_TOKEN;
    }
    Rid = GetIntegrityRid(Token);
    CloseHandle(Token);
    if (Rid != SECURITY_MANDATORY_LOW_RID)
        return CHILD_THREAD_TOKEN_NOT_LOW;

    if (OpenProcessToken(GetCurrentProcess(), READ_CONTROL, &Token))
    {
        ChildReportTokenSecurity(Token, "process token (impersonating)");
        CloseHandle(Token);
    }
    else
    {
        ChildReport("CHILD OpenProcessToken(READ_CONTROL) while impersonating failed %lu\n", GetLastError());
    }

    if (!RevertToSelf())
        return CHILD_REVERT_FAILED;

    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_DEFAULT, &Token))
    {
        ChildReport("CHILD OpenProcessToken(QUERY|ADJUST_DEFAULT) after revert failed %lu\n", GetLastError());
        return CHILD_NO_PROCESS_TOKEN;
    }
    Rid = GetIntegrityRid(Token);
    if (Rid != SECURITY_MANDATORY_LOW_RID)
    {
        CloseHandle(Token);
        return CHILD_PROCESS_TOKEN_NOT_LOW;
    }
    if (!SetIntegrity(Token, SECURITY_MANDATORY_UNTRUSTED_RID, NULL))
    {
        CloseHandle(Token);
        return CHILD_LOWER_FAILED;
    }
    Rid = GetIntegrityRid(Token);
    CloseHandle(Token);
    if (Rid != SECURITY_MANDATORY_UNTRUSTED_RID)
        return CHILD_PROCESS_TOKEN_NOT_UNTRUSTED;

    return CHILD_OK;
}

static BOOL
RemoveAllPrivileges(
    _In_ HANDLE Token)
{
    PTOKEN_PRIVILEGES Privileges;
    DWORD Length = 0, Index;
    BOOL Success;

    GetTokenInformation(Token, TokenPrivileges, NULL, 0, &Length);
    if (!Length)
        return TRUE;
    Privileges = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Privileges)
        return FALSE;
    if (!GetTokenInformation(Token, TokenPrivileges, Privileges, Length, &Length))
    {
        HeapFree(GetProcessHeap(), 0, Privileges);
        return FALSE;
    }
    for (Index = 0; Index < Privileges->PrivilegeCount; ++Index)
        Privileges->Privileges[Index].Attributes = SE_PRIVILEGE_REMOVED;
    Success = Privileges->PrivilegeCount == 0 ||
              AdjustTokenPrivileges(Token, FALSE, Privileges, 0, NULL, NULL);
    HeapFree(GetProcessHeap(), 0, Privileges);
    return Success;
}

static HANDLE
CreateLockdownToken(
    _In_ HANDLE BaseToken,
    _In_ PSID RandomSid)
{
    SID_IDENTIFIER_AUTHORITY NullAuthority = SECURITY_NULL_SID_AUTHORITY;
    SID_IDENTIFIER_AUTHORITY CreatorAuthority = SECURITY_CREATOR_SID_AUTHORITY;
    SID_AND_ATTRIBUTES Restricting[2];
    PSID NullSid = NULL, OwnerRightsSid = NULL;
    HANDLE Token = NULL;
    PTOKEN_DEFAULT_DACL DefaultDacl;
    EXPLICIT_ACCESSW Access[2];
    PACL NewAcl = NULL;
    TOKEN_DEFAULT_DACL NewDefault;
    DWORD Length = 0, Error;
    BOOL Success;

    ok(AllocateAndInitializeSid(&NullAuthority, 1, SECURITY_NULL_RID, 0, 0, 0, 0, 0, 0, 0, &NullSid),
       "lockdown: null SID allocation failed with %lu\n", GetLastError());
    if (!NullSid) return NULL;

    Restricting[0].Sid = NullSid;
    Restricting[0].Attributes = 0;
    Restricting[1].Sid = RandomSid;
    Restricting[1].Attributes = 0;

    Success = CreateRestrictedToken(BaseToken, DISABLE_MAX_PRIVILEGE, 0, NULL, 0, NULL, 2, Restricting, &Token);
    ok(Success, "lockdown: CreateRestrictedToken failed with %lu\n", GetLastError());
    FreeSid(NullSid);
    if (!Success) return NULL;

    ok(RemoveAllPrivileges(Token), "lockdown: removing privileges failed with %lu\n", GetLastError());

    GetTokenInformation(Token, TokenDefaultDacl, NULL, 0, &Length);
    DefaultDacl = HeapAlloc(GetProcessHeap(), 0, Length ? Length : sizeof(*DefaultDacl));
    Success = DefaultDacl && GetTokenInformation(Token, TokenDefaultDacl, DefaultDacl, Length, &Length);
    ok(Success, "lockdown: GetTokenInformation(TokenDefaultDacl) failed with %lu\n", GetLastError());

    ZeroMemory(Access, sizeof(Access));
    Access[0].grfAccessPermissions = GENERIC_ALL;
    Access[0].grfAccessMode = GRANT_ACCESS;
    Access[0].grfInheritance = NO_INHERITANCE;
    Access[0].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    Access[0].Trustee.TrusteeType = TRUSTEE_IS_GROUP;
    Access[0].Trustee.ptstrName = (LPWSTR)RandomSid;
    Access[1].grfAccessPermissions = READ_CONTROL;
    Access[1].grfAccessMode = GRANT_ACCESS;
    Access[1].grfInheritance = NO_INHERITANCE;
    ok(AllocateAndInitializeSid(&CreatorAuthority, 1, SECURITY_CREATOR_OWNER_RIGHTS_RID, 0, 0, 0, 0, 0, 0, 0, &OwnerRightsSid),
       "lockdown: owner rights SID allocation failed with %lu\n", GetLastError());
    Access[1].Trustee.TrusteeForm = TRUSTEE_IS_SID;
    Access[1].Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    Access[1].Trustee.ptstrName = (LPWSTR)OwnerRightsSid;

    Error = SetEntriesInAclW(OwnerRightsSid ? 2 : 1, Access, Success ? DefaultDacl->DefaultDacl : NULL, &NewAcl);
    ok(Error == ERROR_SUCCESS, "lockdown: SetEntriesInAclW failed with %lu\n", Error);
    if (Error == ERROR_SUCCESS)
    {
        NewDefault.DefaultDacl = NewAcl;
        Success = SetTokenInformation(Token, TokenDefaultDacl, &NewDefault, sizeof(NewDefault));
        ok(Success, "lockdown: SetTokenInformation(TokenDefaultDacl) failed with %lu\n", GetLastError());
        LocalFree(NewAcl);
    }
    if (DefaultDacl) HeapFree(GetProcessHeap(), 0, DefaultDacl);
    if (OwnerRightsSid) FreeSid(OwnerRightsSid);

    SetIntegrity(Token, SECURITY_MANDATORY_LOW_RID, "lockdown");
    return Token;
}

static HANDLE
CreateInitialToken(
    _In_ HANDLE BaseToken)
{
    PTOKEN_GROUPS Groups = NULL;
    PTOKEN_USER User = NULL;
    PSID_AND_ATTRIBUTES Restricting = NULL;
    HANDLE Restricted = NULL, Impersonation = NULL;
    DWORD Length = 0, Count = 0, Index;
    BOOL Success;

    GetTokenInformation(BaseToken, TokenGroups, NULL, 0, &Length);
    Groups = HeapAlloc(GetProcessHeap(), 0, Length);
    Success = Groups && GetTokenInformation(BaseToken, TokenGroups, Groups, Length, &Length);
    ok(Success, "initial: GetTokenInformation(TokenGroups) failed with %lu\n", GetLastError());
    if (!Success) goto Cleanup;

    GetTokenInformation(BaseToken, TokenUser, NULL, 0, &Length);
    User = HeapAlloc(GetProcessHeap(), 0, Length);
    Success = User && GetTokenInformation(BaseToken, TokenUser, User, Length, &Length);
    ok(Success, "initial: GetTokenInformation(TokenUser) failed with %lu\n", GetLastError());
    if (!Success) goto Cleanup;

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

    Success = CreateRestrictedToken(BaseToken, 0, 0, NULL, 0, NULL, Count, Restricting, &Restricted);
    ok(Success, "initial: CreateRestrictedToken(%lu restricting SIDs) failed with %lu\n", Count, GetLastError());
    if (!Success) goto Cleanup;

    SetIntegrity(Restricted, SECURITY_MANDATORY_LOW_RID, "initial");

    Success = DuplicateTokenEx(Restricted, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &Impersonation);
    ok(Success, "initial: DuplicateTokenEx(impersonation) failed with %lu\n", GetLastError());

Cleanup:
    if (Restricted) CloseHandle(Restricted);
    if (Restricting) HeapFree(GetProcessHeap(), 0, Restricting);
    if (User) HeapFree(GetProcessHeap(), 0, User);
    if (Groups) HeapFree(GetProcessHeap(), 0, Groups);
    return Impersonation;
}

static HANDLE
CreateLockdownJob(void)
{
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION Extended;
    JOBOBJECT_BASIC_UI_RESTRICTIONS Ui;
    HANDLE Job;
    BOOL Success;

    Job = CreateJobObjectW(NULL, NULL);
    ok(Job != NULL, "CreateJobObjectW failed with %lu\n", GetLastError());
    if (!Job) return NULL;

    ZeroMemory(&Extended, sizeof(Extended));
    Extended.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
                                                JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION |
                                                JOB_OBJECT_LIMIT_ACTIVE_PROCESS;
    Extended.BasicLimitInformation.ActiveProcessLimit = 1;
    Success = SetInformationJobObject(Job, JobObjectExtendedLimitInformation, &Extended, sizeof(Extended));
    ok(Success, "SetInformationJobObject(ExtendedLimit) failed with %lu\n", GetLastError());

    Ui.UIRestrictionsClass = JOB_OBJECT_UILIMIT_HANDLES | JOB_OBJECT_UILIMIT_READCLIPBOARD |
                             JOB_OBJECT_UILIMIT_WRITECLIPBOARD | JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS |
                             JOB_OBJECT_UILIMIT_DISPLAYSETTINGS | JOB_OBJECT_UILIMIT_GLOBALATOMS |
                             JOB_OBJECT_UILIMIT_DESKTOP | JOB_OBJECT_UILIMIT_EXITWINDOWS;
    Success = SetInformationJobObject(Job, JobObjectBasicUIRestrictions, &Ui, sizeof(Ui));
    ok(Success, "SetInformationJobObject(BasicUIRestrictions) failed with %lu\n", GetLastError());

    return Job;
}

static void
TestAlternateDesktop(void)
{
    HWINSTA Old, Station;
    HDESK Desktop = NULL;
    PSECURITY_DESCRIPTOR Descriptor = NULL;
    PACL Sacl = NULL;
    BOOL Present = FALSE, Defaulted = FALSE;
    BOOL Success;
    DWORD Error;

    Old = GetProcessWindowStation();
    Station = CreateWindowStationW(NULL, 0, WINSTA_ALL_ACCESS, NULL);
    ok(Station != NULL, "CreateWindowStationW failed with %lu\n", GetLastError());
    if (!Station) return;

    Success = SetProcessWindowStation(Station);
    ok(Success, "SetProcessWindowStation(alternate) failed with %lu\n", GetLastError());
    if (Success)
    {
        Desktop = CreateDesktopW(L"sbx_test_desktop", NULL, NULL, 0, DESKTOP_CREATEWINDOW | DESKTOP_READOBJECTS | DESKTOP_WRITEOBJECTS | READ_CONTROL | WRITE_DAC | WRITE_OWNER, NULL);
        ok(Desktop != NULL, "CreateDesktopW(alternate) failed with %lu\n", GetLastError());
        SetProcessWindowStation(Old);
    }

    Success = ConvertStringSecurityDescriptorToSecurityDescriptorW(L"S:(ML;;NW;;;LW)", SDDL_REVISION_1, &Descriptor, NULL);
    ok(Success, "ConvertStringSecurityDescriptorToSecurityDescriptorW(label) failed with %lu\n", GetLastError());
    if (Success)
    {
        Success = GetSecurityDescriptorSacl(Descriptor, &Present, &Sacl, &Defaulted);
        ok(Success && Present && Sacl, "GetSecurityDescriptorSacl returned %d present %d\n", Success, Present);
    }

    if (Desktop && Sacl)
    {
        Error = SetSecurityInfo(Desktop, SE_WINDOW_OBJECT, LABEL_SECURITY_INFORMATION, NULL, NULL, NULL, Sacl);
        ok(Error == ERROR_SUCCESS, "SetSecurityInfo(desktop label) failed with %lu\n", Error);
    }

    if (Descriptor) LocalFree(Descriptor);
    if (Desktop) CloseDesktop(Desktop);
    CloseWindowStation(Station);
}

START_TEST(SandboxLaunch)
{
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH + 64];
    STARTUPINFOEXW Startup;
    PROCESS_INFORMATION ProcessInfo;
    LPPROC_THREAD_ATTRIBUTE_LIST Attributes = NULL;
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    HANDLE BaseToken = NULL, Lockdown = NULL, Initial = NULL, Job = NULL;
    HANDLE StdOut = GetStdHandle(STD_OUTPUT_HANDLE);
    PSID RandomSid = NULL;
    DWORD64 Mitigations;
    DWORD ChildPolicy = PROCESS_CREATION_CHILD_PROCESS_RESTRICTED;
    DWORD ExitCode = 0xFFFFFFFF;
    SIZE_T Size = 0;
    char **Arguments;
    int ArgumentCount;
    BOOL Success;

    ArgumentCount = winetest_get_mainargs(&Arguments);
    if (ArgumentCount >= 3 && !strcmp(Arguments[2], "child"))
        TerminateProcess(GetCurrentProcess(), RunChild());

    TestMitigationOptionsMask();

    ZeroMemory(&Startup, sizeof(Startup));

    ZeroMemory(&ProcessInfo, sizeof(ProcessInfo));

    Success = OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &BaseToken);
    ok(Success, "OpenProcessToken failed with %lu\n", GetLastError());
    if (!Success) return;

    Success = AllocateAndInitializeSid(&NtAuthority, 4, 111, 0x12345678, 0x23456789, 0x3456789A, 0, 0, 0, 0, &RandomSid);
    ok(Success, "random SID allocation failed with %lu\n", GetLastError());
    if (!Success) goto Cleanup;

    Lockdown = CreateLockdownToken(BaseToken, RandomSid);
    Initial = CreateInitialToken(BaseToken);
    Job = CreateLockdownJob();
    TestAlternateDesktop();

    if (!Lockdown || !Initial)
    {
        skip("token creation failed, not launching the child\n");
        goto Cleanup;
    }

    Success = GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)) != 0;
    ok(Success, "GetModuleFileNameW failed with %lu\n", GetLastError());
    if (!Success) goto Cleanup;
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine), L"\"%s\" SandboxLaunch child", Application);

    InitializeProcThreadAttributeList(NULL, 4, 0, &Size);
    Attributes = HeapAlloc(GetProcessHeap(), 0, Size);
    Success = Attributes && InitializeProcThreadAttributeList(Attributes, 4, 0, &Size);
    ok(Success, "InitializeProcThreadAttributeList failed with %lu\n", GetLastError());
    if (!Success) goto Cleanup;

    Mitigations = PROCESS_CREATION_MITIGATION_POLICY_DEP_ENABLE |
                  PROCESS_CREATION_MITIGATION_POLICY_DEP_ATL_THUNK_ENABLE |
                  PROCESS_CREATION_MITIGATION_POLICY_SEHOP_ENABLE |
                  PROCESS_CREATION_MITIGATION_POLICY_HEAP_TERMINATE_ALWAYS_ON |
                  PROCESS_CREATION_MITIGATION_POLICY_BOTTOM_UP_ASLR_ALWAYS_ON |
                  PROCESS_CREATION_MITIGATION_POLICY_EXTENSION_POINT_DISABLE_ALWAYS_ON |
                  PROCESS_CREATION_MITIGATION_POLICY_FONT_DISABLE_ALWAYS_ON |
                  PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_REMOTE_ALWAYS_ON |
                  PROCESS_CREATION_MITIGATION_POLICY_IMAGE_LOAD_NO_LOW_LABEL_ALWAYS_ON;
    Success = UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_MITIGATION_POLICY, &Mitigations, sizeof(Mitigations), NULL, NULL);
    ok(Success, "UpdateProcThreadAttribute(MITIGATION_POLICY) failed with %lu\n", GetLastError());

    Success = UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_CHILD_PROCESS_POLICY, &ChildPolicy, sizeof(ChildPolicy), NULL, NULL);
    ok(Success, "UpdateProcThreadAttribute(CHILD_PROCESS_POLICY) failed with %lu\n", GetLastError());

    Success = UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, &StdOut, sizeof(StdOut), NULL, NULL);
    ok(Success, "UpdateProcThreadAttribute(HANDLE_LIST) failed with %lu\n", GetLastError());

    if (Job)
    {
        Success = UpdateProcThreadAttribute(Attributes, 0, PROC_THREAD_ATTRIBUTE_JOB_LIST, &Job, sizeof(Job), NULL, NULL);
        ok(Success, "UpdateProcThreadAttribute(JOB_LIST) failed with %lu\n", GetLastError());
    }

    ZeroMemory(&Startup, sizeof(Startup));
    Startup.StartupInfo.cb = sizeof(Startup);
    Startup.StartupInfo.dwFlags = STARTF_FORCEOFFFEEDBACK | STARTF_USESTDHANDLES;
    Startup.StartupInfo.hStdOutput = StdOut;
    Startup.StartupInfo.hStdError = StdOut;
    Startup.lpAttributeList = Attributes;

    Success = CreateProcessAsUserW(Lockdown,
                                   Application,
                                   CommandLine,
                                   NULL,
                                   NULL,
                                   TRUE,
                                   CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT | EXTENDED_STARTUPINFO_PRESENT | DETACHED_PROCESS,
                                   NULL,
                                   NULL,
                                   &Startup.StartupInfo,
                                   &ProcessInfo);
    ok(Success, "CreateProcessAsUserW(lockdown token) failed with %lu\n", GetLastError());
    if (!Success) goto Cleanup;

    Success = SetThreadToken(&ProcessInfo.hThread, Initial);
    ok(Success, "SetThreadToken(initial token) failed with %lu\n", GetLastError());

    ok(ResumeThread(ProcessInfo.hThread) == 1, "ResumeThread failed with %lu\n", GetLastError());

    ok(WaitForSingleObject(ProcessInfo.hProcess, CHILD_TIMEOUT_MS) == WAIT_OBJECT_0, "child did not exit in time\n");
    GetExitCodeProcess(ProcessInfo.hProcess, &ExitCode);
    ok(ExitCode == CHILD_OK, "child exited with %lu\n", ExitCode);
    if (ExitCode == STILL_ACTIVE)
        TerminateProcess(ProcessInfo.hProcess, 1);

Cleanup:
    if (ProcessInfo.hThread) CloseHandle(ProcessInfo.hThread);
    if (ProcessInfo.hProcess) CloseHandle(ProcessInfo.hProcess);
    if (Attributes)
    {
        DeleteProcThreadAttributeList(Attributes);
        HeapFree(GetProcessHeap(), 0, Attributes);
    }
    if (Job) CloseHandle(Job);
    if (Initial) CloseHandle(Initial);
    if (Lockdown) CloseHandle(Lockdown);
    if (RandomSid) FreeSid(RandomSid);
    if (BaseToken) CloseHandle(BaseToken);
}
