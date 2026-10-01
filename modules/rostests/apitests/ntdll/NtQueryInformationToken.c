/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Tests for the NtQueryInformationToken API
 * COPYRIGHT:       Copyright 2022 George Bișoc <george.bisoc@reactos.org>
 */

#include "precomp.h"

static
PVOID
QueryWin32TokenInformation(HANDLE Token, TOKEN_INFORMATION_CLASS Class, PULONG Length)
{
    PVOID Buffer;
    DWORD Required = 0;
    BOOL Success;

    SetLastError(ERROR_SUCCESS);
    Success = GetTokenInformation(Token, Class, NULL, 0, &Required);
    ok(!Success, "GetTokenInformation(%u) unexpectedly succeeded\n", Class);
    ok(GetLastError() == ERROR_INSUFFICIENT_BUFFER,
       "GetTokenInformation(%u) error %lu\n", Class, GetLastError());
    if (Success || GetLastError() != ERROR_INSUFFICIENT_BUFFER || !Required)
        return NULL;
    Buffer = RtlAllocateHeap(RtlGetProcessHeap(), 0, Required);
    ok(Buffer != NULL, "Failed to allocate %lu bytes for class %u\n", Required, Class);
    if (!Buffer)
        return NULL;
    Success = GetTokenInformation(Token, Class, Buffer, Required, &Required);
    ok(Success, "GetTokenInformation(%u) error %lu\n", Class, GetLastError());
    if (!Success)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Buffer);
        return NULL;
    }
    *Length = Required;
    return Buffer;
}

static
BOOL
TokenSidInBuffer(PSID Sid, PVOID Buffer, ULONG Length)
{
    ULONG_PTR Offset;

    if (!Sid || (ULONG_PTR)Sid < (ULONG_PTR)Buffer)
        return FALSE;
    Offset = (ULONG_PTR)Sid - (ULONG_PTR)Buffer;
    if (Offset > Length || Length - Offset < FIELD_OFFSET(SID, SubAuthority))
        return FALSE;
    if (GetSidLengthRequired(((SID*)Sid)->SubAuthorityCount) > Length - Offset)
        return FALSE;
    return IsValidSid(Sid);
}

static
HANDLE
OpenCurrentToken(VOID)
{
    BOOL Success;
    HANDLE Token;

    Success = OpenProcessToken(GetCurrentProcess(),
                               TOKEN_READ | TOKEN_QUERY_SOURCE | TOKEN_DUPLICATE,
                               &Token);
    if (!Success)
    {
        ok(FALSE, "OpenProcessToken() has failed to get the process' token (error code: %lu)!\n", GetLastError());
        return NULL;
    }

    return Token;
}

static
VOID
QueryTokenUserTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    PTOKEN_USER UserToken;
    ULONG BufferLength;
    UNICODE_STRING SidString;

    /*
     * Query the exact buffer length to hold
     * our stuff, STATUS_BUFFER_TOO_SMALL must
     * be expected here.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenUser,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    /* Allocate the buffer based on the size we got */
    UserToken = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!UserToken)
    {
        ok(FALSE, "Failed to allocate from heap for token user (required buffer length %lu)!\n", BufferLength);
        return;
    }

    /* Now do the actual query */
    Status = NtQueryInformationToken(Token,
                                     TokenUser,
                                     UserToken,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);

    RtlConvertSidToUnicodeString(&SidString, UserToken->User.Sid, TRUE);
    trace("=============== TokenUser ===============\n");
    trace("The SID of current token user is: %s\n", wine_dbgstr_w(SidString.Buffer));
    trace("=========================================\n\n");
    RtlFreeUnicodeString(&SidString);

    RtlFreeHeap(RtlGetProcessHeap(), 0, UserToken);
}

static
VOID
QueryTokenGroupsTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    PTOKEN_GROUPS Groups;
    PTOKEN_GROUPS Win32Groups;
    TOKEN_STATISTICS Statistics;
    HANDLE MembershipToken = NULL;
    BOOL Success, Member, ExpectedMember;
    ULONG Index, Other, Win32Length, StatisticsLength;
    ULONG BufferLength;

    /*
     * Query the exact buffer length to hold
     * our stuff, STATUS_BUFFER_TOO_SMALL must
     * be expected here.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenGroups,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    /* Allocate the buffer based on the size we got */
    Groups = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!Groups)
    {
        ok(FALSE, "Failed to allocate from heap for token groups (required buffer length %lu)!\n", BufferLength);
        return;
    }

    /*
     * Now do the actual query and validate the
     * number of groups.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenGroups,
                                     Groups,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Done;
    ok(BufferLength >= FIELD_OFFSET(TOKEN_GROUPS, Groups), "Groups length %lu\n", BufferLength);
    if (BufferLength < FIELD_OFFSET(TOKEN_GROUPS, Groups))
        goto Done;
    ok(Groups->GroupCount <= (BufferLength - FIELD_OFFSET(TOKEN_GROUPS, Groups)) / sizeof(SID_AND_ATTRIBUTES),
       "Group count %lu exceeds buffer %lu\n", Groups->GroupCount, BufferLength);
    if (Groups->GroupCount > (BufferLength - FIELD_OFFSET(TOKEN_GROUPS, Groups)) / sizeof(SID_AND_ATTRIBUTES))
        goto Done;
    Win32Groups = QueryWin32TokenInformation(Token, TokenGroups, &Win32Length);
    if (!Win32Groups)
        goto Done;
    Success = Win32Length >= FIELD_OFFSET(TOKEN_GROUPS, Groups) &&
              Win32Groups->GroupCount <= (Win32Length - FIELD_OFFSET(TOKEN_GROUPS, Groups)) / sizeof(SID_AND_ATTRIBUTES);
    ok(Success, "Win32 groups exceed buffer %lu\n", Win32Length);
    if (!Success)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Win32Groups);
        goto Done;
    }
    ok(Win32Length == BufferLength, "Group lengths Nt %lu Win32 %lu\n", BufferLength, Win32Length);
    ok(Win32Groups->GroupCount == Groups->GroupCount,
       "Group counts Nt %lu Win32 %lu\n", Groups->GroupCount, Win32Groups->GroupCount);
    Status = NtQueryInformationToken(Token, TokenStatistics, &Statistics, sizeof(Statistics), &StatisticsLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(Statistics.GroupCount == Groups->GroupCount,
           "Statistics groups %lu, queried %lu\n", Statistics.GroupCount, Groups->GroupCount);
    Success = DuplicateTokenEx(Token, TOKEN_QUERY, NULL, SecurityImpersonation,
                              TokenImpersonation, &MembershipToken);
    ok(Success, "DuplicateTokenEx membership error %lu\n", GetLastError());
    for (Index = 0; Index < Groups->GroupCount; Index++)
    {
        BOOL Valid = TokenSidInBuffer(Groups->Groups[Index].Sid, Groups, BufferLength);

        ok(Valid, "Group %lu has an invalid SID\n", Index);
        if (!Valid)
            continue;
        if (Index < Win32Groups->GroupCount)
        {
            BOOL Win32Valid = TokenSidInBuffer(Win32Groups->Groups[Index].Sid, Win32Groups, Win32Length);

            ok(Win32Valid, "Win32 group %lu has an invalid SID\n", Index);
            if (Win32Valid)
            {
                ok(EqualSid(Groups->Groups[Index].Sid, Win32Groups->Groups[Index].Sid),
                   "Group %lu SID differs between APIs\n", Index);
                ok(Groups->Groups[Index].Attributes == Win32Groups->Groups[Index].Attributes,
                   "Group %lu attributes Nt %#lx Win32 %#lx\n", Index,
                   Groups->Groups[Index].Attributes, Win32Groups->Groups[Index].Attributes);
            }
        }
        for (Other = 0; Other < Index; Other++)
        {
            if (TokenSidInBuffer(Groups->Groups[Other].Sid, Groups, BufferLength))
                ok(!EqualSid(Groups->Groups[Index].Sid, Groups->Groups[Other].Sid),
                   "Groups %lu and %lu duplicate a SID\n", Index, Other);
        }
        if (MembershipToken)
        {
            Success = CheckTokenMembership(MembershipToken, Groups->Groups[Index].Sid, &Member);
            ok(Success, "CheckTokenMembership group %lu error %lu\n", Index, GetLastError());
            ExpectedMember = !!(Groups->Groups[Index].Attributes & SE_GROUP_ENABLED) &&
                             !(Groups->Groups[Index].Attributes & SE_GROUP_USE_FOR_DENY_ONLY);
            if (Success)
                ok(Member == ExpectedMember, "Group %lu membership %d expected %d\n",
                   Index, Member, ExpectedMember);
        }
    }
    if (MembershipToken)
        CloseHandle(MembershipToken);
    RtlFreeHeap(RtlGetProcessHeap(), 0, Win32Groups);

Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, Groups);
}

static
VOID
QueryTokenPrivilegesTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    PTOKEN_PRIVILEGES Privileges;
    PTOKEN_PRIVILEGES Win32Privileges;
    TOKEN_STATISTICS Statistics;
    ULONG Win32Length, StatisticsLength, Index, Other;
    ULONG BufferLength;

    /*
     * Query the exact buffer length to hold
     * our stuff, STATUS_BUFFER_TOO_SMALL must
     * be expected here.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenPrivileges,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    /* Allocate the buffer based on the size we got */
    Privileges = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!Privileges)
    {
        ok(FALSE, "Failed to allocate from heap for token privileges (required buffer length %lu)!\n", BufferLength);
        return;
    }

    /*
     * Now do the actual query and validate the
     * number of privileges.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenPrivileges,
                                     Privileges,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Done;
    ok(BufferLength >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges), "Privileges length %lu\n", BufferLength);
    if (BufferLength < FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges))
        goto Done;
    ok(Privileges->PrivilegeCount <= (BufferLength - FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES),
       "Privilege count %lu exceeds buffer %lu\n", Privileges->PrivilegeCount, BufferLength);
    if (Privileges->PrivilegeCount > (BufferLength - FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES))
        goto Done;
    Win32Privileges = QueryWin32TokenInformation(Token, TokenPrivileges, &Win32Length);
    if (!Win32Privileges)
        goto Done;
    if (Win32Length < FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) ||
        Win32Privileges->PrivilegeCount > (Win32Length - FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES))
    {
        ok(FALSE, "Win32 privileges exceed buffer %lu\n", Win32Length);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Win32Privileges);
        goto Done;
    }
    ok(Win32Length == BufferLength, "Privilege lengths Nt %lu Win32 %lu\n", BufferLength, Win32Length);
    ok(Win32Privileges->PrivilegeCount == Privileges->PrivilegeCount,
       "Privilege counts Nt %lu Win32 %lu\n", Privileges->PrivilegeCount, Win32Privileges->PrivilegeCount);
    Status = NtQueryInformationToken(Token, TokenStatistics, &Statistics, sizeof(Statistics), &StatisticsLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
        ok(Statistics.PrivilegeCount == Privileges->PrivilegeCount,
           "Statistics privileges %lu, queried %lu\n", Statistics.PrivilegeCount, Privileges->PrivilegeCount);
    for (Index = 0; Index < Privileges->PrivilegeCount; Index++)
    {
        WCHAR Name[128];
        DWORD NameLength = RTL_NUMBER_OF(Name);
        BOOL Success;

        if (Index < Win32Privileges->PrivilegeCount)
        {
            ok(RtlEqualLuid(&Privileges->Privileges[Index].Luid, &Win32Privileges->Privileges[Index].Luid),
               "Privilege %lu LUID differs between APIs\n", Index);
            ok(Privileges->Privileges[Index].Attributes == Win32Privileges->Privileges[Index].Attributes,
               "Privilege %lu attributes Nt %#lx Win32 %#lx\n", Index,
               Privileges->Privileges[Index].Attributes, Win32Privileges->Privileges[Index].Attributes);
        }
        for (Other = 0; Other < Index; Other++)
            ok(!RtlEqualLuid(&Privileges->Privileges[Index].Luid, &Privileges->Privileges[Other].Luid),
               "Privileges %lu and %lu duplicate a LUID\n", Index, Other);
        Success = LookupPrivilegeNameW(NULL, &Privileges->Privileges[Index].Luid, Name, &NameLength);
        ok(Success, "LookupPrivilegeNameW privilege %lu error %lu\n", Index, GetLastError());
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Win32Privileges);

Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, Privileges);
}

static
VOID
QueryTokenOwnerTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    PTOKEN_OWNER Owner;
    ULONG BufferLength;
    UNICODE_STRING SidString;

    /*
     * Query the exact buffer length to hold
     * our stuff, STATUS_BUFFER_TOO_SMALL must
     * be expected here.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenOwner,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    /* Allocate the buffer based on the size we got */
    Owner = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!Owner)
    {
        ok(FALSE, "Failed to allocate from heap for token owner (required buffer length %lu)!\n", BufferLength);
        return;
    }

    /*
     * Now do the actual query and validate the
     * token owner (must be the local admin).
     */
    Status = NtQueryInformationToken(Token,
                                     TokenOwner,
                                     Owner,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);

    RtlConvertSidToUnicodeString(&SidString, Owner->Owner, TRUE);
    ok_wstr(SidString.Buffer, L"S-1-5-32-544");
    RtlFreeUnicodeString(&SidString);

    RtlFreeHeap(RtlGetProcessHeap(), 0, Owner);
}

static
VOID
QueryTokenPrimaryGroupTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    PTOKEN_PRIMARY_GROUP PrimaryGroup;
    ULONG BufferLength;
    UNICODE_STRING SidString;

    /*
     * Query the exact buffer length to hold
     * our stuff, STATUS_BUFFER_TOO_SMALL must
     * be expected here.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenPrimaryGroup,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    /* Allocate the buffer based on the size we got */
    PrimaryGroup = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!PrimaryGroup)
    {
        ok(FALSE, "Failed to allocate from heap for token primary group (required buffer length %lu)!\n", BufferLength);
        return;
    }

    /* Now do the actual query */
    Status = NtQueryInformationToken(Token,
                                     TokenPrimaryGroup,
                                     PrimaryGroup,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);

    RtlConvertSidToUnicodeString(&SidString, PrimaryGroup->PrimaryGroup, TRUE);
    trace("=============== TokenPrimaryGroup ===============\n");
    trace("The primary group SID of current token is: %s\n", wine_dbgstr_w(SidString.Buffer));
    trace("=========================================\n\n");
    RtlFreeUnicodeString(&SidString);

    RtlFreeHeap(RtlGetProcessHeap(), 0, PrimaryGroup);
}

static
VOID
QueryTokenDefaultDaclTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    PTOKEN_DEFAULT_DACL Dacl;
    ULONG BufferLength;

    /*
     * Query the exact buffer length to hold
     * our stuff, STATUS_BUFFER_TOO_SMALL must
     * be expected here.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenDefaultDacl,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    /* Allocate the buffer based on the size we got */
    Dacl = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!Dacl)
    {
        ok(FALSE, "Failed to allocate from heap for token default DACL (required buffer length %lu)!\n", BufferLength);
        return;
    }

    /*
     * Now do the actual query and validate the
     * ACL revision and number count of ACEs.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenDefaultDacl,
                                     Dacl,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Dacl->DefaultDacl->AclRevision == 2, "The ACL revision of token default DACL must be 2 (current revision %u)!\n", Dacl->DefaultDacl->AclRevision);
    ok(Dacl->DefaultDacl->AceCount == 2, "The ACL's ACE count must be 2 (current ACE count %u)!\n", Dacl->DefaultDacl->AceCount);

    RtlFreeHeap(RtlGetProcessHeap(), 0, Dacl);
}

static
VOID
QueryTokenSourceTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    PTOKEN_SOURCE Source;
    TOKEN_SOURCE Win32Source, DuplicateSource;
    HANDLE Duplicate = NULL, Limited = NULL;
    BOOL Success;
    ULONG BufferLength;
    DWORD Win32Length;

    /*
     * Query the exact buffer length to hold
     * our stuff, STATUS_BUFFER_TOO_SMALL must
     * be expected here.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenSource,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);
    ok(BufferLength == sizeof(TOKEN_SOURCE), "Required source length %lu\n", BufferLength);
    if (Status != STATUS_BUFFER_TOO_SMALL || BufferLength != sizeof(TOKEN_SOURCE)) return;

    /* Allocate the buffer based on the size we got */
    Source = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!Source)
    {
        ok(FALSE, "Failed to allocate from heap for token source (required buffer length %lu)!\n", BufferLength);
        return;
    }

    /* Now do the actual query */
    Status = NtQueryInformationToken(Token,
                                     TokenSource,
                                     Source,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);

    if (!NT_SUCCESS(Status))
        goto Done;
    ok(BufferLength == sizeof(TOKEN_SOURCE), "Source length %lu\n", BufferLength);
    if (BufferLength != sizeof(TOKEN_SOURCE)) goto Done;
    Success = GetTokenInformation(Token, TokenSource, &Win32Source, sizeof(Win32Source), &Win32Length);
    ok(Success, "GetTokenInformation source error %lu\n", GetLastError());
    if (Success)
    {
        ok(Win32Length == sizeof(Win32Source), "Win32 source length %lu\n", Win32Length);
        ok(!memcmp(Source->SourceName, Win32Source.SourceName, TOKEN_SOURCE_LENGTH),
           "All eight source-name bytes must match between APIs\n");
        ok(RtlEqualLuid(&Source->SourceIdentifier, &Win32Source.SourceIdentifier),
           "Source identifier differs between APIs\n");
    }
    trace("Token source %.*s identifier %lx:%lx\n", TOKEN_SOURCE_LENGTH, Source->SourceName,
          Source->SourceIdentifier.HighPart, Source->SourceIdentifier.LowPart);
    Success = DuplicateTokenEx(Token, TOKEN_QUERY | TOKEN_QUERY_SOURCE, NULL,
                              SecurityImpersonation, TokenPrimary, &Duplicate);
    ok(Success, "DuplicateTokenEx source error %lu\n", GetLastError());
    if (Success)
    {
        Status = NtQueryInformationToken(Duplicate, TokenSource, &DuplicateSource, sizeof(DuplicateSource), &BufferLength);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            ok(!memcmp(Source->SourceName, DuplicateSource.SourceName, TOKEN_SOURCE_LENGTH),
               "Token duplication changed source-name bytes\n");
            ok(RtlEqualLuid(&Source->SourceIdentifier, &DuplicateSource.SourceIdentifier),
               "Token duplication changed source identifier\n");
        }
        CloseHandle(Duplicate);
    }
    Success = DuplicateHandle(GetCurrentProcess(), Token, GetCurrentProcess(), &Limited, TOKEN_QUERY, FALSE, 0);
    ok(Success, "DuplicateHandle query-only token error %lu\n", GetLastError());
    if (Success)
    {
        Status = NtQueryInformationToken(Limited, TokenSource, &DuplicateSource, sizeof(DuplicateSource), &BufferLength);
        ok_ntstatus(Status, STATUS_ACCESS_DENIED);
        CloseHandle(Limited);
    }

Done:
    RtlFreeHeap(RtlGetProcessHeap(), 0, Source);
}

static
VOID
QueryTokenTypeTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    TOKEN_TYPE Type;
    ULONG BufferLength;

    /*
     * Query the token type. The token of the
     * current calling process must be primary
     * since we aren't impersonating the security
     * context of a client.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenType,
                                     &Type,
                                     sizeof(TOKEN_TYPE),
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Type == TokenPrimary, "The current token is not primary!\n");
}

static
VOID
QueryTokenImpersonationTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    SECURITY_IMPERSONATION_LEVEL Level;
    ULONG BufferLength;
    HANDLE DupToken;
    OBJECT_ATTRIBUTES ObjectAttributes;

    /*
     * Windows throws STATUS_INVALID_INFO_CLASS here
     * because one cannot simply query the impersonation
     * level of a primary token.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenImpersonationLevel,
                                     &Level,
                                     sizeof(SECURITY_IMPERSONATION_LEVEL),
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_INVALID_INFO_CLASS);

    /*
     * Initialize the object attribute and duplicate
     * the token into an actual impersonation one.
     */
    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               0,
                               NULL,
                               NULL);

    Status = NtDuplicateToken(Token,
                              TOKEN_QUERY,
                              &ObjectAttributes,
                              FALSE,
                              TokenImpersonation,
                              &DupToken);
    if (!NT_SUCCESS(Status))
    {
        ok(FALSE, "Failed to duplicate token (Status code %lx)!\n", Status);
        return;
    }

    /* Now do the actual query */
    Status = NtQueryInformationToken(DupToken,
                                     TokenImpersonationLevel,
                                     &Level,
                                     sizeof(SECURITY_IMPERSONATION_LEVEL),
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(Level == SecurityAnonymous, "The current token impersonation level is not anonymous!\n");
    NtClose(DupToken);
}

static
VOID
QueryTokenStatisticsTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    PTOKEN_STATISTICS Statistics;
    ULONG BufferLength;

    /*
     * Query the exact buffer length to hold
     * our stuff, STATUS_BUFFER_TOO_SMALL must
     * be expected here.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenStatistics,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    /* Allocate the buffer based on the size we got */
    Statistics = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!Statistics)
    {
        ok(FALSE, "Failed to allocate from heap for token statistics (required buffer length %lu)!\n", BufferLength);
        return;
    }

    /* Do the actual query */
    Status = NtQueryInformationToken(Token,
                                     TokenStatistics,
                                     Statistics,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);

    trace("=============== TokenStatistics ===============\n");
    trace("Token ID: %lu %lu\n", Statistics->TokenId.LowPart, Statistics->TokenId.HighPart);
    trace("Authentication ID: %lu %lu\n", Statistics->AuthenticationId.LowPart, Statistics->AuthenticationId.HighPart);
    trace("Dynamic Charged: %lu\n", Statistics->DynamicCharged);
    trace("Dynamic Available: %lu\n", Statistics->DynamicAvailable);
    trace("Modified ID: %lu %lu\n", Statistics->ModifiedId.LowPart, Statistics->ModifiedId.HighPart);
    trace("=========================================\n\n");

    RtlFreeHeap(RtlGetProcessHeap(), 0, Statistics);
}

static
VOID
QueryTokenPrivilegesAndGroupsTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    PTOKEN_GROUPS_AND_PRIVILEGES PrivsAndGroups;
    TOKEN_GROUPS SidToRestrict;
    HANDLE FilteredToken;
    PSID WorldSid;
    ULONG BufferLength;
    static SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};

    /*
     * Create a World SID and filter the token
     * by adding a restricted SID.
     */
    Status = RtlAllocateAndInitializeSid(&WorldAuthority,
                                         1,
                                         SECURITY_WORLD_RID,
                                         0, 0, 0, 0, 0, 0, 0,
                                         &WorldSid);
    if (!NT_SUCCESS(Status))
    {
        ok(FALSE, "Failed to allocate World SID (Status code %lx)!\n", Status);
        return;
    }

    SidToRestrict.GroupCount = 1;
    SidToRestrict.Groups[0].Attributes = 0;
    SidToRestrict.Groups[0].Sid = WorldSid;

    Status = NtFilterToken(Token,
                           0,
                           NULL,
                           NULL,
                           &SidToRestrict,
                           &FilteredToken);
    if (!NT_SUCCESS(Status))
    {
        ok(FALSE, "Failed to filter the current token (Status code %lx)!\n", Status);
        RtlFreeHeap(RtlGetProcessHeap(), 0, WorldSid);
        return;
    }

    /*
     * Query the exact buffer length to hold
     * our stuff, STATUS_BUFFER_TOO_SMALL must
     * be expected here.
     */
    Status = NtQueryInformationToken(FilteredToken,
                                     TokenGroupsAndPrivileges,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    /* Allocate the buffer based on the size we got */
    PrivsAndGroups = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!PrivsAndGroups)
    {
        ok(FALSE, "Failed to allocate from heap for token privileges and groups (required buffer length %lu)!\n", BufferLength);
        RtlFreeHeap(RtlGetProcessHeap(), 0, WorldSid);
        NtClose(FilteredToken);
        return;
    }

    /* Do the actual query */
    Status = NtQueryInformationToken(FilteredToken,
                                     TokenGroupsAndPrivileges,
                                     PrivsAndGroups,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);

    trace("=============== TokenGroupsAndPrivileges ===============\n");
    trace("SID count: %lu\n", PrivsAndGroups->SidCount);
    trace("SID length: %lu\n", PrivsAndGroups->SidLength);
    trace("Restricted SID count: %lu\n", PrivsAndGroups->RestrictedSidCount);
    trace("Restricted SID length: %lu\n", PrivsAndGroups->RestrictedSidLength);
    trace("Privilege count: %lu\n", PrivsAndGroups->PrivilegeCount);
    trace("Privilege length: %lu\n", PrivsAndGroups->PrivilegeLength);
    trace("Authentication ID: %lu %lu\n", PrivsAndGroups->AuthenticationId.LowPart, PrivsAndGroups->AuthenticationId.HighPart);
    trace("=========================================\n\n");

    RtlFreeHeap(RtlGetProcessHeap(), 0, PrivsAndGroups);
    RtlFreeHeap(RtlGetProcessHeap(), 0, WorldSid);
    NtClose(FilteredToken);
}

static
VOID
QueryTokenRestrictedSidsTest(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    PTOKEN_GROUPS RestrictedGroups;
    TOKEN_GROUPS SidToRestrict;
    ULONG BufferLength;
    HANDLE FilteredToken;
    PSID WorldSid;
    static SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};

    /*
     * Query the exact buffer length to hold
     * our stuff, STATUS_BUFFER_TOO_SMALL must
     * be expected here.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenRestrictedSids,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    /* Allocate the buffer based on the size we got */
    RestrictedGroups = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!RestrictedGroups)
    {
        ok(FALSE, "Failed to allocate from heap for restricted SIDs (required buffer length %lu)!\n", BufferLength);
        return;
    }

    /*
     * Query the number of restricted SIDs. Originally the token
     * doesn't have any restricted SIDs inserted.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenRestrictedSids,
                                     RestrictedGroups,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(RestrictedGroups->GroupCount == 0, "There mustn't be any restricted SIDs before filtering (number of restricted SIDs %lu)!\n", RestrictedGroups->GroupCount);

    RtlFreeHeap(RtlGetProcessHeap(), 0, RestrictedGroups);
    RestrictedGroups = NULL;

    Status = RtlAllocateAndInitializeSid(&WorldAuthority,
                                         1,
                                         SECURITY_WORLD_RID,
                                         0, 0, 0, 0, 0, 0, 0,
                                         &WorldSid);
    if (!NT_SUCCESS(Status))
    {
        ok(FALSE, "Failed to allocate World SID (Status code %lx)!\n", Status);
        return;
    }

    SidToRestrict.GroupCount = 1;
    SidToRestrict.Groups[0].Attributes = 0;
    SidToRestrict.Groups[0].Sid = WorldSid;

    Status = NtFilterToken(Token,
                           0,
                           NULL,
                           NULL,
                           &SidToRestrict,
                           &FilteredToken);
    if (!NT_SUCCESS(Status))
    {
        ok(FALSE, "Failed to filter the current token (Status code %lx)!\n", Status);
        RtlFreeHeap(RtlGetProcessHeap(), 0, WorldSid);
        return;
    }

    Status = NtQueryInformationToken(FilteredToken,
                                     TokenRestrictedSids,
                                     NULL,
                                     0,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_BUFFER_TOO_SMALL);

    RestrictedGroups = RtlAllocateHeap(RtlGetProcessHeap(), 0, BufferLength);
    if (!RestrictedGroups)
    {
        ok(FALSE, "Failed to allocate from heap for restricted SIDs (required buffer length %lu)!\n", BufferLength);
        RtlFreeHeap(RtlGetProcessHeap(), 0, WorldSid);
        return;
    }

    /*
     * Do a query again, this time we must have a
     * restricted SID inserted into the token.
     */
    Status = NtQueryInformationToken(FilteredToken,
                                     TokenRestrictedSids,
                                     RestrictedGroups,
                                     BufferLength,
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(RestrictedGroups->GroupCount == 1, "There must be only one restricted SID added in token (number of restricted SIDs %lu)!\n", RestrictedGroups->GroupCount);

    RtlFreeHeap(RtlGetProcessHeap(), 0, RestrictedGroups);
    RtlFreeHeap(RtlGetProcessHeap(), 0, WorldSid);
    NtClose(FilteredToken);
}

static
VOID
QueryTokenSessionIdTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    ULONG SessionId;
    ULONG BufferLength;

    /*
     * Query the session ID. Generally the current
     * process token is not under any terminal service
     * so the ID must be 0.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenSessionId,
                                     &SessionId,
                                     sizeof(ULONG),
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(SessionId == 0, "The session ID of current token must be 0 (current session %lu)!\n", SessionId);
}

static
VOID
QueryTokenIsSandboxInert(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    ULONG IsTokenInert;
    ULONG BufferLength;
    HANDLE FilteredToken;

    /*
     * Query the sandbox inert token information,
     * it must not be inert.
     */
    Status = NtQueryInformationToken(Token,
                                     TokenSandBoxInert,
                                     &IsTokenInert,
                                     sizeof(ULONG),
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(IsTokenInert == FALSE, "The token must not be a sandbox inert one!\n");

    /*
     * Try to turn the token into an inert
     * one by filtering it.
     */
    Status = NtFilterToken(Token,
                           SANDBOX_INERT,
                           NULL,
                           NULL,
                           NULL,
                           &FilteredToken);
    if (!NT_SUCCESS(Status))
    {
        ok(FALSE, "Failed to filter the current token (Status code %lx)!\n", Status);
        return;
    }

    /*
     * Now do a query again, this time
     * the token should be inert.
     */
    Status = NtQueryInformationToken(FilteredToken,
                                     TokenSandBoxInert,
                                     &IsTokenInert,
                                     sizeof(ULONG),
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    ok(IsTokenInert == TRUE, "The token must be a sandbox inert one after filtering!\n");

    NtClose(FilteredToken);
}

static
VOID
QueryTokenOriginTests(
    _In_ HANDLE Token)
{
    NTSTATUS Status;
    TOKEN_ORIGIN Origin;
    TOKEN_ORIGIN Win32Origin, DuplicateOrigin;
    HANDLE Duplicate;
    DWORD Win32Length;
    BOOL Success;
    ULONG BufferLength;

    /* Query the token origin */
    Status = NtQueryInformationToken(Token,
                                     TokenOrigin,
                                     &Origin,
                                     sizeof(TOKEN_ORIGIN),
                                     &BufferLength);
    ok_ntstatus(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok(BufferLength == sizeof(Origin), "Origin length %lu\n", BufferLength);
    Success = GetTokenInformation(Token, TokenOrigin, &Win32Origin, sizeof(Win32Origin), &Win32Length);
    ok(Success, "GetTokenInformation origin error %lu\n", GetLastError());
    if (Success)
        ok(RtlEqualLuid(&Origin.OriginatingLogonSession, &Win32Origin.OriginatingLogonSession),
           "Origin differs between APIs\n");
    Success = DuplicateTokenEx(Token, TOKEN_QUERY, NULL, SecurityImpersonation, TokenPrimary, &Duplicate);
    ok(Success, "DuplicateTokenEx origin error %lu\n", GetLastError());
    if (Success)
    {
        Status = NtQueryInformationToken(Duplicate, TokenOrigin, &DuplicateOrigin, sizeof(DuplicateOrigin), &BufferLength);
        ok_ntstatus(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
            ok(RtlEqualLuid(&Origin.OriginatingLogonSession, &DuplicateOrigin.OriginatingLogonSession),
               "Token duplication changed originating logon session\n");
        CloseHandle(Duplicate);
    }
}

START_TEST(NtQueryInformationToken)
{
    NTSTATUS Status;
    HANDLE Token;
    PVOID Dummy;
    ULONG DummyReturnLength;

    /* ReturnLength is NULL */
    Status = NtQueryInformationToken(NULL,
                                     TokenUser,
                                     NULL,
                                     0,
                                     NULL);
    ok_ntstatus(Status, STATUS_ACCESS_VIOLATION);

    /* We don't give any token here */
    Status = NtQueryInformationToken(NULL,
                                     TokenUser,
                                     &Dummy,
                                     0,
                                     &DummyReturnLength);
    ok_ntstatus(Status, STATUS_INVALID_HANDLE);

    Token = OpenCurrentToken();
    if (!Token)
        return;

    /* Class 0 is unused on Windows */
    Status = NtQueryInformationToken(Token,
                                     0,
                                     &Dummy,
                                     0,
                                     &DummyReturnLength);
    ok_ntstatus(Status, STATUS_INVALID_INFO_CLASS);

    /* We give a bogus info class */
    Status = NtQueryInformationToken(Token,
                                     0xa0a,
                                     &Dummy,
                                     0,
                                     &DummyReturnLength);
    ok_ntstatus(Status, STATUS_INVALID_INFO_CLASS);

    /* Now perform tests for each class */
    QueryTokenUserTests(Token);
    QueryTokenGroupsTests(Token);
    QueryTokenPrivilegesTests(Token);
    QueryTokenOwnerTests(Token);
    QueryTokenPrimaryGroupTests(Token);
    QueryTokenDefaultDaclTests(Token);
    QueryTokenSourceTests(Token);
    QueryTokenTypeTests(Token);
    QueryTokenImpersonationTests(Token);
    QueryTokenStatisticsTests(Token);
    QueryTokenPrivilegesAndGroupsTests(Token);
    QueryTokenRestrictedSidsTest(Token);
    QueryTokenSessionIdTests(Token);
    QueryTokenIsSandboxInert(Token);
    QueryTokenOriginTests(Token);

    NtClose(Token);
}
