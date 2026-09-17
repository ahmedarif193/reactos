/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Restricted SID enforcement in the access check
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "precomp.h"

#define TORTURE_ITERATIONS 300

/* These token-claim definitions are missing from the older public SDK surface
 * used by the ReactOS build, but are the ABI consumed by current Chromium. */
#ifndef TOKEN_SECURITY_ATTRIBUTE_TYPE_UINT64
#define TOKEN_SECURITY_ATTRIBUTE_TYPE_INVALID 0x00
#define TOKEN_SECURITY_ATTRIBUTE_TYPE_INT64   0x01
#define TOKEN_SECURITY_ATTRIBUTE_TYPE_UINT64  0x02
#define TOKEN_SECURITY_ATTRIBUTE_TYPE_STRING  0x03
#define TOKEN_SECURITY_ATTRIBUTES_INFORMATION_VERSION_V1 1

typedef struct _SBX_TOKEN_SECURITY_ATTRIBUTE_V1
{
    UNICODE_STRING Name;
    USHORT ValueType;
    USHORT Reserved;
    ULONG Flags;
    ULONG ValueCount;
    union
    {
        PLONG64 pInt64;
        PULONG64 pUint64;
        PUNICODE_STRING pString;
        PVOID pFqbn;
        PVOID pOctetString;
    } Values;
} SBX_TOKEN_SECURITY_ATTRIBUTE_V1, *PSBX_TOKEN_SECURITY_ATTRIBUTE_V1;

typedef struct _SBX_TOKEN_SECURITY_ATTRIBUTES_INFORMATION
{
    USHORT Version;
    USHORT Reserved;
    ULONG AttributeCount;
    union
    {
        PSBX_TOKEN_SECURITY_ATTRIBUTE_V1 pAttributeV1;
    } Attribute;
} SBX_TOKEN_SECURITY_ATTRIBUTES_INFORMATION, *PSBX_TOKEN_SECURITY_ATTRIBUTES_INFORMATION;
#endif

typedef struct _SBX_SECURITY_ATTRIBUTE_VALUE
{
    USHORT Type;
    ULONG Flags;
    ULONG Count;
    ULONGLONG Uint64[8];
    WCHAR String[8][128];
} SBX_SECURITY_ATTRIBUTE_VALUE, *PSBX_SECURITY_ATTRIBUTE_VALUE;

static BOOL
QuerySecurityAttribute(
    _In_ HANDLE Token,
    _In_ PCWSTR Name,
    _Out_opt_ PSBX_SECURITY_ATTRIBUTE_VALUE Value)
{
    PSBX_TOKEN_SECURITY_ATTRIBUTES_INFORMATION Information;
    PSBX_TOKEN_SECURITY_ATTRIBUTE_V1 Attribute;
    PBYTE Buffer;
    DWORD Length = 0, Index;
    BOOL Found = FALSE;

    SetLastError(ERROR_SUCCESS);
    GetTokenInformation(Token, TokenSecurityAttributes, NULL, 0, &Length);
    if (!Length || GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return FALSE;

    Buffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Length);
    if (!Buffer)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    if (!GetTokenInformation(Token, TokenSecurityAttributes, Buffer, Length, &Length))
        goto Cleanup;

    Information = (PSBX_TOKEN_SECURITY_ATTRIBUTES_INFORMATION)Buffer;
    if (Information->Version != TOKEN_SECURITY_ATTRIBUTES_INFORMATION_VERSION_V1)
    {
        SetLastError(ERROR_INVALID_DATA);
        goto Cleanup;
    }

    for (Index = 0; Index < Information->AttributeCount; Index++)
    {
        Attribute = &Information->Attribute.pAttributeV1[Index];
        if (Attribute->Name.Buffer &&
            Attribute->Name.Length == wcslen(Name) * sizeof(WCHAR) &&
            !_wcsnicmp(Attribute->Name.Buffer, Name,
                       Attribute->Name.Length / sizeof(WCHAR)))
        {
            if (!Attribute->ValueCount)
            {
                SetLastError(ERROR_INVALID_DATA);
                goto Cleanup;
            }
            if (Value)
            {
                ULONG ValueIndex;

                ZeroMemory(Value, sizeof(*Value));
                Value->Type = Attribute->ValueType;
                Value->Flags = Attribute->Flags;
                Value->Count = Attribute->ValueCount;
                if (Value->Count > ARRAYSIZE(Value->Uint64))
                {
                    SetLastError(ERROR_BUFFER_OVERFLOW);
                    goto Cleanup;
                }
                for (ValueIndex = 0; ValueIndex < Value->Count; ValueIndex++)
                {
                    if (Attribute->ValueType == TOKEN_SECURITY_ATTRIBUTE_TYPE_UINT64)
                        Value->Uint64[ValueIndex] = Attribute->Values.pUint64[ValueIndex];
                    else if (Attribute->ValueType == TOKEN_SECURITY_ATTRIBUTE_TYPE_INT64)
                        Value->Uint64[ValueIndex] = Attribute->Values.pInt64[ValueIndex];
                    else if (Attribute->ValueType == TOKEN_SECURITY_ATTRIBUTE_TYPE_STRING &&
                             Attribute->Values.pString[ValueIndex].Buffer)
                    {
                        ULONG Characters = min(Attribute->Values.pString[ValueIndex].Length / sizeof(WCHAR),
                                               ARRAYSIZE(Value->String[ValueIndex]) - 1);
                        CopyMemory(Value->String[ValueIndex],
                                   Attribute->Values.pString[ValueIndex].Buffer,
                                   Characters * sizeof(WCHAR));
                        Value->String[ValueIndex][Characters] = UNICODE_NULL;
                    }
                }
            }
            Found = TRUE;
            SetLastError(ERROR_SUCCESS);
            break;
        }
    }

Cleanup:
    HeapFree(GetProcessHeap(), 0, Buffer);
    return Found;
}

static BOOL
SetIsolationDefaultDacl(
    _In_ HANDLE Token,
    _In_ PSID UserSid,
    _In_ const SBX_SECURITY_ATTRIBUTE_VALUE *Value,
    _Out_opt_ PDWORD AceSize)
{
    UCHAR BaseBuffer[sizeof(ACL) + 3 * (sizeof(ACCESS_ALLOWED_ACE) + SECURITY_MAX_SID_SIZE)];
    PACL BaseAcl = (PACL)BaseBuffer, NewAcl = NULL;
    UCHAR SidBuffer[SECURITY_MAX_SID_SIZE];
    PSID OwnerRights = (PSID)SidBuffer;
    DWORD SidSize = sizeof(SidBuffer), Required = 0;
    ACL_SIZE_INFORMATION Size;
    TOKEN_DEFAULT_DACL DefaultDacl;
    WCHAR Condition[1024], Part[160];
    ULONG Index;
    BOOL Result = FALSE;

    if (!CreateWellKnownSid(WinCreatorOwnerRightsSid, NULL, OwnerRights, &SidSize) ||
        !InitializeAcl(BaseAcl, sizeof(BaseBuffer), ACL_REVISION) ||
        !AddAccessAllowedAceEx(BaseAcl, ACL_REVISION, 0, READ_CONTROL, OwnerRights) ||
        !AddAccessAllowedAceEx(BaseAcl, ACL_REVISION, 0, GENERIC_EXECUTE, UserSid))
        return FALSE;

    if (FAILED(StringCchCopyW(Condition, ARRAYSIZE(Condition),
                              L"(TSA://ProcUnique == {")))
        return FALSE;
    for (Index = 0; Index < Value->Count; Index++)
    {
        if (Value->Type == TOKEN_SECURITY_ATTRIBUTE_TYPE_UINT64 ||
            Value->Type == TOKEN_SECURITY_ATTRIBUTE_TYPE_INT64)
        {
            if (FAILED(StringCchPrintfW(Part, ARRAYSIZE(Part), L"%s%I64u",
                                        Index ? L"," : L"", Value->Uint64[Index])))
                return FALSE;
        }
        else if (Value->Type == TOKEN_SECURITY_ATTRIBUTE_TYPE_STRING)
        {
            if (wcschr(Value->String[Index], L'\"') ||
                FAILED(StringCchPrintfW(Part, ARRAYSIZE(Part), L"%s\"%s\"",
                                        Index ? L"," : L"", Value->String[Index])))
                return FALSE;
        }
        else
        {
            SetLastError(ERROR_NOT_SUPPORTED);
            return FALSE;
        }
        if (FAILED(StringCchCatW(Condition, ARRAYSIZE(Condition), Part)))
            return FALSE;
    }
    if (FAILED(StringCchCatW(Condition, ARRAYSIZE(Condition), L"})")))
        return FALSE;

    if (!GetAclInformation(BaseAcl, &Size, sizeof(Size), AclSizeInformation))
        return FALSE;
    BaseAcl->AclSize = (WORD)Size.AclBytesInUse;

    SetLastError(0xdeadbeef);
    if (AddConditionalAce(BaseAcl, ACL_REVISION, 0,
                          ACCESS_ALLOWED_CALLBACK_ACE_TYPE, GENERIC_ALL,
                          UserSid, Condition, &Required) ||
        GetLastError() != ERROR_INSUFFICIENT_BUFFER || Required <= BaseAcl->AclSize)
    {
        SetLastError(ERROR_INVALID_DATA);
        return FALSE;
    }

    NewAcl = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Required);
    if (!NewAcl)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    CopyMemory(NewAcl, BaseAcl, BaseAcl->AclSize);
    NewAcl->AclSize = (WORD)Required;
    if (!AddConditionalAce(NewAcl, ACL_REVISION, 0,
                           ACCESS_ALLOWED_CALLBACK_ACE_TYPE, GENERIC_ALL,
                           UserSid, Condition, &Required))
        goto Cleanup;

    DefaultDacl.DefaultDacl = NewAcl;
    Result = SetTokenInformation(Token, TokenDefaultDacl,
                                 &DefaultDacl, sizeof(DefaultDacl));
    if (Result && AceSize)
    {
        PACE_HEADER Ace = NULL;
        if (GetAce(NewAcl, NewAcl->AceCount - 1, (PVOID *)&Ace))
            *AceSize = Ace->AceSize;
    }

Cleanup:
    HeapFree(GetProcessHeap(), 0, NewAcl);
    return Result;
}

static BOOL
CanOpenProcessAs(
    _In_ HANDLE Token,
    _In_ DWORD ProcessId,
    _In_ DWORD Access,
    _Out_opt_ PDWORD Error)
{
    HANDLE Impersonation = NULL, Process = NULL;
    TOKEN_PRIVILEGES Privilege;
    BOOL Result = FALSE;

    if (!DuplicateTokenEx(Token, TOKEN_QUERY | TOKEN_IMPERSONATE | TOKEN_ADJUST_PRIVILEGES,
                          NULL, SecurityImpersonation, TokenImpersonation,
                          &Impersonation))
        goto Cleanup;

    if (LookupPrivilegeValueW(NULL, L"SeDebugPrivilege", &Privilege.Privileges[0].Luid))
    {
        Privilege.PrivilegeCount = 1;
        Privilege.Privileges[0].Attributes = 0;
        AdjustTokenPrivileges(Impersonation, FALSE, &Privilege, 0, NULL, NULL);
    }
    if (!ImpersonateLoggedOnUser(Impersonation))
        goto Cleanup;
    SetLastError(ERROR_SUCCESS);
    Process = OpenProcess(Access, FALSE, ProcessId);
    if (Error) *Error = GetLastError();
    RevertToSelf();
    Result = Process != NULL;

Cleanup:
    if (Process) CloseHandle(Process);
    if (Impersonation) CloseHandle(Impersonation);
    return Result;
}

static PSID
GetUserSidOfToken(HANDLE Token)
{
    UCHAR Buffer[sizeof(TOKEN_USER) + SECURITY_MAX_SID_SIZE];
    DWORD Length;
    PSID Copy;
    DWORD SidLength;

    if (!GetTokenInformation(Token, TokenUser, Buffer, sizeof(Buffer), &Length))
        return NULL;
    SidLength = GetLengthSid(((PTOKEN_USER)Buffer)->User.Sid);
    Copy = HeapAlloc(GetProcessHeap(), 0, SidLength);
    if (Copy) CopySid(SidLength, Copy, ((PTOKEN_USER)Buffer)->User.Sid);
    return Copy;
}

static HANDLE
CreateRestricted(HANDLE Base, PSID RestrictingSid, DWORD Flags)
{
    SID_AND_ATTRIBUTES Restrict = { RestrictingSid, 0 };
    HANDLE Token = NULL;

    if (!CreateRestrictedToken(Base, Flags, 0, NULL, 0, NULL, 1, &Restrict, &Token))
        return NULL;
    return Token;
}

static HANDLE
CreateEventWithDacl(PCWSTR Name, PCWSTR Sddl)
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

static ACCESS_MASK
MaximumAllowedAs(HANDLE Token, PCWSTR Name)
{
    HANDLE Handle = NULL;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ObjectName;
    WCHAR Path[256];
    OBJECT_BASIC_INFORMATION Basic;
    NTSTATUS Status;
    ACCESS_MASK Granted = 0;

    StringCchPrintfW(Path, ARRAYSIZE(Path), L"\\Sessions\\%lu\\BaseNamedObjects\\%s",
                     NtCurrentPeb()->SessionId, Name);
    RtlInitUnicodeString(&ObjectName, Path);
    InitializeObjectAttributes(&Attributes, &ObjectName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    ok(ImpersonateLoggedOnUser(Token), "impersonation failed %lu\n", GetLastError());
    Status = NtOpenEvent(&Handle, MAXIMUM_ALLOWED, &Attributes);
    RevertToSelf();
    if (NT_SUCCESS(Status))
    {
        Status = NtQueryObject(Handle, ObjectBasicInformation, &Basic, sizeof(Basic), NULL);
        Granted = NT_SUCCESS(Status) ? Basic.GrantedAccess : 0;
        CloseHandle(Handle);
    }
    return Granted;
}

static VOID
TestProcUniqueIsolation(HANDLE Base)
{
    SBX_SECURITY_ATTRIBUTE_VALUE ParentValue, DuplicateValue, ChildValue;
    HANDLE IsolationToken = NULL, ChildToken = NULL;
    PROCESS_INFORMATION Info;
    STARTUPINFOW Startup;
    WCHAR Application[MAX_PATH], CommandLine[MAX_PATH * 2];
    PSID UserSid = NULL;
    DWORD Error, AceSize = 0;
    BOOL Result;

    ZeroMemory(&Info, sizeof(Info));
    Result = QuerySecurityAttribute(Base, L"TSA://ProcUnique", &ParentValue);
    ok(Result, "current token has no TSA://ProcUnique attribute, error %lu\n", GetLastError());
    if (!Result) return;

    ok((ParentValue.Type == TOKEN_SECURITY_ATTRIBUTE_TYPE_UINT64 ||
        ParentValue.Type == TOKEN_SECURITY_ATTRIBUTE_TYPE_INT64 ||
        ParentValue.Type == TOKEN_SECURITY_ATTRIBUTE_TYPE_STRING),
       "TSA://ProcUnique type %u\n", ParentValue.Type);
    ok(ParentValue.Count == 2, "TSA://ProcUnique value count %lu\n", ParentValue.Count);
    trace("TSA://ProcUnique type %u flags 0x%lx count %lu values %I64u/%I64u\n",
          ParentValue.Type, ParentValue.Flags, ParentValue.Count,
          ParentValue.Uint64[0], ParentValue.Uint64[1]);

    ok(DuplicateTokenEx(Base, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation,
                        TokenPrimary, &IsolationToken),
       "DuplicateTokenEx for isolation failed %lu\n", GetLastError());
    if (!IsolationToken) return;

    Result = QuerySecurityAttribute(IsolationToken, L"TSA://ProcUnique", &DuplicateValue);
    ok(Result, "duplicated token lost TSA://ProcUnique, error %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    ok(DuplicateValue.Type == ParentValue.Type &&
       DuplicateValue.Count == ParentValue.Count &&
       DuplicateValue.Flags == ParentValue.Flags,
       "duplicated attribute metadata differs: type %u/%u, count %lu/%lu, flags 0x%lx/0x%lx\n",
       DuplicateValue.Type, ParentValue.Type, DuplicateValue.Count,
       ParentValue.Count, DuplicateValue.Flags, ParentValue.Flags);
    if (ParentValue.Type == TOKEN_SECURITY_ATTRIBUTE_TYPE_STRING)
        ok(!wcscmp(DuplicateValue.String[0], ParentValue.String[0]) &&
           !wcscmp(DuplicateValue.String[1], ParentValue.String[1]),
           "duplicated attribute strings differ\n");
    else
        ok(DuplicateValue.Uint64[0] == ParentValue.Uint64[0] &&
           DuplicateValue.Uint64[1] == ParentValue.Uint64[1],
           "duplicated attribute values differ: %I64u/%I64u vs %I64u/%I64u\n",
           DuplicateValue.Uint64[0], DuplicateValue.Uint64[1],
           ParentValue.Uint64[0], ParentValue.Uint64[1]);

    UserSid = GetUserSidOfToken(IsolationToken);
    ok(UserSid != NULL, "isolation token has no user SID\n");
    if (!UserSid) goto Cleanup;
    Result = SetIsolationDefaultDacl(IsolationToken, UserSid,
                                     &ParentValue, &AceSize);
    ok(Result, "conditional isolation DACL failed %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    ok(AceSize > FIELD_OFFSET(ACCESS_ALLOWED_CALLBACK_ACE, SidStart) + GetLengthSid(UserSid),
       "conditional ACE does not contain an expression: %lu bytes\n", AceSize);

    ok(GetModuleFileNameW(NULL, Application, ARRAYSIZE(Application)) != 0,
       "GetModuleFileNameW failed %lu\n", GetLastError());
    if (!Application[0]) goto Cleanup;
    StringCchPrintfW(CommandLine, ARRAYSIZE(CommandLine),
                     L"\"%s\" RestrictedToken child suspended", Application);
    ZeroMemory(&Startup, sizeof(Startup));
    Startup.cb = sizeof(Startup);
    Result = CreateProcessAsUserW(IsolationToken, Application, CommandLine,
                                  NULL, NULL, FALSE,
                                  CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
                                  NULL, NULL, &Startup, &Info);
    ok(Result, "CreateProcessAsUserW with isolation token failed %lu\n", GetLastError());
    if (!Result) goto Cleanup;

    ok(OpenProcessToken(Info.hProcess, TOKEN_QUERY | TOKEN_DUPLICATE, &ChildToken),
       "OpenProcessToken(child) failed %lu\n", GetLastError());
    if (!ChildToken) goto Cleanup;
    Result = QuerySecurityAttribute(ChildToken, L"TSA://ProcUnique", &ChildValue);
    ok(Result, "child token has no TSA://ProcUnique, error %lu\n", GetLastError());
    if (!Result) goto Cleanup;
    ok(ChildValue.Type == ParentValue.Type && ChildValue.Count == ParentValue.Count,
       "child attribute metadata differs: type %u/%u, count %lu/%lu\n",
       ChildValue.Type, ParentValue.Type, ChildValue.Count, ParentValue.Count);
    if (ParentValue.Type == TOKEN_SECURITY_ATTRIBUTE_TYPE_STRING)
        ok(wcscmp(ChildValue.String[0], ParentValue.String[0]) != 0 ||
           wcscmp(ChildValue.String[1], ParentValue.String[1]) != 0,
           "child inherited the parent's ProcUnique strings\n");
    else
        ok(ChildValue.Uint64[0] != ParentValue.Uint64[0] ||
           ChildValue.Uint64[1] != ParentValue.Uint64[1],
           "child inherited the parent's ProcUnique values %I64u/%I64u\n",
           ChildValue.Uint64[0], ChildValue.Uint64[1]);

    ok(TerminateProcess(Info.hProcess, 0), "TerminateProcess failed %lu\n", GetLastError());
    WaitForSingleObject(Info.hProcess, SBX_CHILD_TIMEOUT_MS);

    Result = CanOpenProcessAs(IsolationToken, Info.dwProcessId, READ_CONTROL, &Error);
    ok(Result, "source token cannot read child process control, error %lu\n", Error);
    Result = CanOpenProcessAs(IsolationToken, Info.dwProcessId, WRITE_DAC, &Error);
    ok(Result, "source token did not match conditional ACE for WRITE_DAC, error %lu\n", Error);
    Result = CanOpenProcessAs(IsolationToken, Info.dwProcessId, PROCESS_ALL_ACCESS, &Error);
    ok(Result, "source token did not match conditional ACE for full access, error %lu\n", Error);

    Result = CanOpenProcessAs(ChildToken, Info.dwProcessId, READ_CONTROL, &Error);
    ok(Result, "child token cannot read its process control, error %lu\n", Error);
    Result = CanOpenProcessAs(ChildToken, Info.dwProcessId, WRITE_DAC, &Error);
    ok(!Result && Error == ERROR_ACCESS_DENIED,
       "child token matched stale ProcUnique conditional ACE: %u, error %lu\n",
       Result, Error);
    Result = CanOpenProcessAs(ChildToken, Info.dwProcessId, PROCESS_ALL_ACCESS, &Error);
    ok(!Result && Error == ERROR_ACCESS_DENIED,
       "child token received full access through stale ProcUnique: %u, error %lu\n",
       Result, Error);

Cleanup:
    if (Info.hThread) CloseHandle(Info.hThread);
    if (Info.hProcess) CloseHandle(Info.hProcess);
    if (ChildToken) CloseHandle(ChildToken);
    if (UserSid) HeapFree(GetProcessHeap(), 0, UserSid);
    if (IsolationToken) CloseHandle(IsolationToken);
}

START_TEST(RestrictedToken)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    HANDLE Base, Restricted, Impersonation;
    PSID RestrictingSid = NULL, UserSid;
    LPWSTR RestrictingString = NULL, UserString = NULL;
    WCHAR Sddl[512];
    HANDLE Event, Opened;
    DWORD Error, Value, Length, Iteration;
    ULONG Failures = 0;

    Base = SbxOpenToken(TOKEN_ALL_ACCESS);
    ok(Base != NULL, "OpenProcessToken failed %lu\n", GetLastError());
    if (!Base) return;

    TestProcUniqueIsolation(Base);

    ok(AllocateAndInitializeSid(&NtAuthority, 3, 111, 0x5bc0, 0x1234, 0, 0, 0, 0, 0, &RestrictingSid),
       "AllocateAndInitializeSid failed %lu\n", GetLastError());
    UserSid = GetUserSidOfToken(Base);
    ok(UserSid != NULL, "no user SID\n");
    if (!RestrictingSid || !UserSid) goto Cleanup;
    ConvertSidToStringSidW(RestrictingSid, &RestrictingString);
    ConvertSidToStringSidW(UserSid, &UserString);

    Restricted = CreateRestricted(Base, RestrictingSid, 0);
    ok(Restricted != NULL, "CreateRestrictedToken failed %lu\n", GetLastError());
    if (!Restricted) goto Cleanup;

    ok(GetTokenInformation(Restricted, TokenIsRestricted, &Value, sizeof(Value), &Length) && Value == 1,
       "TokenIsRestricted not reported\n");
    ok(GetTokenInformation(Restricted, TokenHasRestrictions, &Value, sizeof(Value), &Length) && Value == 1,
       "TokenHasRestrictions not reported\n");
    ok(!IsTokenRestricted(Base), "base token reports restricted\n");
    ok(IsTokenRestricted(Restricted), "restricted token not reported restricted\n");

    ok(DuplicateTokenEx(Restricted, TOKEN_ALL_ACCESS, NULL, SecurityImpersonation, TokenImpersonation, &Impersonation),
       "DuplicateTokenEx failed %lu\n", GetLastError());
    if (!Impersonation) goto Cleanup;

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)", UserString);
    Event = CreateEventWithDacl(L"sbx_restricted_useronly", Sddl);
    ok(Event != NULL, "CreateEvent failed %lu\n", GetLastError());
    if (Event)
    {
        Opened = OpenAs(Impersonation, L"sbx_restricted_useronly", EVENT_ALL_ACCESS, &Error);
        ok(Opened == NULL && Error == ERROR_ACCESS_DENIED, "restricted open of user-only DACL: %p %lu\n", Opened, Error);
        if (Opened) CloseHandle(Opened);
        Opened = OpenAs(Impersonation, L"sbx_restricted_useronly", SYNCHRONIZE, &Error);
        ok(Opened == NULL && Error == ERROR_ACCESS_DENIED, "restricted sync open of user-only DACL: %p %lu\n", Opened, Error);
        if (Opened) CloseHandle(Opened);
        ok(MaximumAllowedAs(Impersonation, L"sbx_restricted_useronly") == 0, "restricted MAXIMUM_ALLOWED granted rights on user-only DACL\n");
        CloseHandle(Event);
    }

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;GA;;;%s)", UserString, RestrictingString);
    Event = CreateEventWithDacl(L"sbx_restricted_both", Sddl);
    ok(Event != NULL, "CreateEvent failed %lu\n", GetLastError());
    if (Event)
    {
        Opened = OpenAs(Impersonation, L"sbx_restricted_both", EVENT_ALL_ACCESS, &Error);
        ok(Opened != NULL, "restricted open of user+restricting DACL failed %lu\n", Error);
        if (Opened) CloseHandle(Opened);
        ok(MaximumAllowedAs(Impersonation, L"sbx_restricted_both") & EVENT_MODIFY_STATE, "restricted MAXIMUM_ALLOWED lost modify\n");
        CloseHandle(Event);
    }

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;0x00100000;;;%s)", UserString, RestrictingString);
    Event = CreateEventWithDacl(L"sbx_restricted_partial", Sddl);
    ok(Event != NULL, "CreateEvent failed %lu\n", GetLastError());
    if (Event)
    {
        ACCESS_MASK Granted = MaximumAllowedAs(Impersonation, L"sbx_restricted_partial");
        ok(Granted == SYNCHRONIZE, "restricted MAXIMUM_ALLOWED intersection: 0x%lx\n", Granted);
        Opened = OpenAs(Impersonation, L"sbx_restricted_partial", SYNCHRONIZE, &Error);
        ok(Opened != NULL, "restricted sync open failed %lu\n", Error);
        if (Opened) CloseHandle(Opened);
        Opened = OpenAs(Impersonation, L"sbx_restricted_partial", EVENT_MODIFY_STATE, &Error);
        ok(Opened == NULL && Error == ERROR_ACCESS_DENIED, "restricted modify open: %p %lu\n", Opened, Error);
        if (Opened) CloseHandle(Opened);
        CloseHandle(Event);
    }

    StringCchPrintfW(Sddl, ARRAYSIZE(Sddl), L"D:(A;;GA;;;%s)(A;;GA;;;%s)", UserString, RestrictingString);
    Event = CreateEventWithDacl(L"sbx_restricted_torture", Sddl);
    if (Event)
    {
        for (Iteration = 0; Iteration < TORTURE_ITERATIONS; Iteration++)
        {
            Opened = OpenAs(Impersonation, L"sbx_restricted_torture", EVENT_ALL_ACCESS, &Error);
            if (!Opened) Failures++; else CloseHandle(Opened);
            Opened = OpenAs(Impersonation, L"sbx_restricted_useronly", EVENT_ALL_ACCESS, &Error);
            if (Opened) { Failures++; CloseHandle(Opened); }
        }
        ok(Failures == 0, "restricted torture failures: %lu\n", Failures);
        CloseHandle(Event);
    }

    CloseHandle(Impersonation);
    CloseHandle(Restricted);

Cleanup:
    if (RestrictingString) LocalFree(RestrictingString);
    if (UserString) LocalFree(UserString);
    if (UserSid) HeapFree(GetProcessHeap(), 0, UserSid);
    if (RestrictingSid) FreeSid(RestrictingSid);
    CloseHandle(Base);
}
