/*
 * PROJECT:     ReactOS KernelBase
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Token membership, capability and AppContainer object path helpers
 */

#include <windows.h>
#include <sddl.h>

LONG NTAPI RtlCheckTokenMembershipEx(HANDLE TokenHandle, PSID SidToCheck, ULONG Flags, PBOOLEAN IsMember);
ULONG NTAPI RtlNtStatusToDosError(LONG Status);

#define CTMF_INCLUDE_APPCONTAINER 0x00000001UL
#define CTMF_INCLUDE_LPAC 0x00000002UL
#define CTMF_VALID_FLAGS (CTMF_INCLUDE_APPCONTAINER | CTMF_INCLUDE_LPAC)
#define BASEP_ROUND_UP(x, a) (((x) + ((a) - 1)) & ~((a) - 1))

static
BOOL
BasepOpenEffectiveToken(
    _In_opt_ HANDLE TokenHandle,
    _In_ DWORD Access,
    _Out_ PHANDLE Token,
    _Out_ PBOOL Opened)
{
    *Opened = FALSE;
    *Token = TokenHandle;
    if (TokenHandle)
        return TRUE;

    if (OpenThreadToken(GetCurrentThread(), Access, TRUE, Token) ||
        OpenProcessToken(GetCurrentProcess(), Access, Token))
    {
        *Opened = TRUE;
        return TRUE;
    }
    return FALSE;
}

static
PVOID
BasepQueryTokenInformation(
    _In_ HANDLE Token,
    _In_ TOKEN_INFORMATION_CLASS Class)
{
    DWORD Length = 0;
    PVOID Buffer;

    GetTokenInformation(Token, Class, NULL, 0, &Length);
    if (!Length)
        return NULL;
    Buffer = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!Buffer)
        return NULL;
    if (!GetTokenInformation(Token, Class, Buffer, Length, &Length))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return NULL;
    }
    return Buffer;
}

static
BOOL
BasepSidInGroups(
    _In_ PTOKEN_GROUPS Groups,
    _In_ PSID Sid,
    _In_ BOOL RequireEnabled)
{
    DWORD Index;

    for (Index = 0; Index < Groups->GroupCount; Index++)
    {
        if (RequireEnabled &&
            (!(Groups->Groups[Index].Attributes & SE_GROUP_ENABLED) ||
             (Groups->Groups[Index].Attributes & SE_GROUP_USE_FOR_DENY_ONLY)))
        {
            continue;
        }
        if (EqualSid(Groups->Groups[Index].Sid, Sid))
            return TRUE;
    }
    return FALSE;
}

BOOL
WINAPI
CheckTokenMembershipEx(
    _In_opt_ HANDLE TokenHandle,
    _In_ PSID SidToCheck,
    _In_ DWORD Flags,
    _Out_ PBOOL IsMember)
{
    BOOLEAN Member = FALSE;
    LONG Status;

    if (!IsMember)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    Status = RtlCheckTokenMembershipEx(TokenHandle, SidToCheck, Flags, &Member);
    *IsMember = Member ? TRUE : FALSE;
    if (Status < 0)
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }
    return TRUE;
}

BOOL
WINAPI
CheckTokenCapability(
    _In_opt_ HANDLE TokenHandle,
    _In_ PSID CapabilitySidToCheck,
    _Out_ PBOOL HasCapability)
{
    HANDLE Token;
    BOOL Opened, Result = FALSE;
    DWORD IsAppContainer = 0, Length;
    PTOKEN_GROUPS Capabilities;

    if (!HasCapability || !CapabilitySidToCheck)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *HasCapability = FALSE;

    if (!BasepOpenEffectiveToken(TokenHandle, TOKEN_QUERY, &Token, &Opened))
        return FALSE;

    if (!GetTokenInformation(Token, TokenIsAppContainer, &IsAppContainer, sizeof(IsAppContainer), &Length))
        goto Cleanup;
    if (!IsAppContainer)
    {
        *HasCapability = TRUE;
        Result = TRUE;
        goto Cleanup;
    }

    Capabilities = BasepQueryTokenInformation(Token, TokenCapabilities);
    if (!Capabilities)
        goto Cleanup;
    *HasCapability = BasepSidInGroups(Capabilities, CapabilitySidToCheck, FALSE);
    HeapFree(GetProcessHeap(), 0, Capabilities);
    Result = TRUE;

Cleanup:
    if (Opened) CloseHandle(Token);
    return Result;
}

BOOL
WINAPI
GetAppContainerNamedObjectPath(
    _In_opt_ HANDLE Token,
    _In_opt_ PSID AppContainerSid,
    _In_ ULONG ObjectPathLength,
    _Out_writes_opt_(ObjectPathLength) LPWSTR ObjectPath,
    _Out_ PULONG ReturnLength)
{
    static const WCHAR Prefix[] = L"AppContainerNamedObjects\\";
    HANDLE Effective;
    BOOL Opened = FALSE, Result = FALSE;
    PTOKEN_APPCONTAINER_INFORMATION Package = NULL;
    LPWSTR SidString = NULL;
    ULONG Needed;

    if (!ReturnLength)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *ReturnLength = 0;

    if (!AppContainerSid)
    {
        if (!BasepOpenEffectiveToken(Token, TOKEN_QUERY, &Effective, &Opened))
            return FALSE;
        Package = BasepQueryTokenInformation(Effective, TokenAppContainerSid);
        if (Opened) CloseHandle(Effective);
        if (!Package || !Package->TokenAppContainer)
        {
            if (Package) HeapFree(GetProcessHeap(), 0, Package);
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
        AppContainerSid = Package->TokenAppContainer;
    }

    if (!ConvertSidToStringSidW(AppContainerSid, &SidString))
        goto Cleanup;

    Needed = (ULONG)(ARRAYSIZE(Prefix) - 1 + lstrlenW(SidString) + 1);
    *ReturnLength = Needed;
    if (!ObjectPath || ObjectPathLength < Needed)
    {
        SetLastError(ERROR_INSUFFICIENT_BUFFER);
        goto Cleanup;
    }

    lstrcpyW(ObjectPath, Prefix);
    lstrcatW(ObjectPath, SidString);
    Result = TRUE;

Cleanup:
    if (SidString) LocalFree(SidString);
    if (Package) HeapFree(GetProcessHeap(), 0, Package);
    return Result;
}

static
BOOLEAN
BasepIsAppContainerSid(
    _In_ PSID Sid)
{
    static const SID_IDENTIFIER_AUTHORITY PackageAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
    PUCHAR Count;

    if (!Sid || !IsValidSid(Sid))
        return FALSE;
    if (memcmp(GetSidIdentifierAuthority(Sid), &PackageAuthority, sizeof(PackageAuthority)) != 0)
        return FALSE;
    Count = GetSidSubAuthorityCount(Sid);
    return *Count >= 2 &&
           (*GetSidSubAuthority(Sid, 0) == SECURITY_APP_PACKAGE_BASE_RID ||
            *GetSidSubAuthority(Sid, 0) == SECURITY_CAPABILITY_BASE_RID);
}

BOOL
APIENTRY
GetAppContainerAce(
    _In_ PACL Acl,
    _In_ DWORD StartingAceIndex,
    _Outptr_ PVOID *AppContainerAce,
    _Out_opt_ DWORD *AppContainerAceIndex)
{
    ACL_SIZE_INFORMATION Size;
    PACE_HEADER Ace;
    DWORD Index;

    if (!Acl || !AppContainerAce || !IsValidAcl(Acl))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!GetAclInformation(Acl, &Size, sizeof(Size), AclSizeInformation))
        return FALSE;
    if (StartingAceIndex >= Size.AceCount)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    for (Index = StartingAceIndex; Index < Size.AceCount; Index++)
    {
        if (!GetAce(Acl, Index, (PVOID *)&Ace))
            return FALSE;
        if (Ace->AceType != ACCESS_ALLOWED_ACE_TYPE ||
            !BasepIsAppContainerSid(&((PACCESS_ALLOWED_ACE)Ace)->SidStart))
            continue;
        *AppContainerAce = Ace;
        if (AppContainerAceIndex)
            *AppContainerAceIndex = Index;
        return TRUE;
    }

    SetLastError(ERROR_NOT_FOUND);
    return FALSE;
}

BOOL
WINAPI
AddScopedPolicyIDAce(
    _Inout_ PACL Acl,
    _In_ DWORD AceRevision,
    _In_ DWORD AceFlags,
    _In_ DWORD AccessMask,
    _In_ PSID Sid)
{
    static const SID_IDENTIFIER_AUTHORITY PolicyAuthority = SECURITY_SCOPED_POLICY_ID_AUTHORITY;
    ACL_SIZE_INFORMATION Size;
    PSYSTEM_SCOPED_POLICY_ID_ACE Ace;
    DWORD Length;

    if ((Acl && Acl->AclRevision > ACL_REVISION_DS) || AceRevision > ACL_REVISION_DS)
    {
        SetLastError(ERROR_REVISION_MISMATCH);
        return FALSE;
    }

    if (!Acl || !Sid || !IsValidSid(Sid) || !IsValidAcl(Acl) ||
        memcmp(GetSidIdentifierAuthority(Sid), &PolicyAuthority, sizeof(PolicyAuthority)) != 0 ||
        (AceFlags & ~VALID_INHERIT_FLAGS) || AccessMask != 0)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    if (!GetAclInformation(Acl, &Size, sizeof(Size), AclSizeInformation))
        return FALSE;

    Length = (DWORD)(FIELD_OFFSET(SYSTEM_SCOPED_POLICY_ID_ACE, SidStart) + GetLengthSid(Sid));
    if (Size.AclBytesFree < Length)
    {
        SetLastError(ERROR_ALLOTTED_SPACE_EXCEEDED);
        return FALSE;
    }

    Ace = (PSYSTEM_SCOPED_POLICY_ID_ACE)((PUCHAR)Acl + Acl->AclSize - Size.AclBytesFree);
    Ace->Header.AceType = SYSTEM_SCOPED_POLICY_ID_ACE_TYPE;
    Ace->Header.AceFlags = (BYTE)AceFlags;
    Ace->Header.AceSize = (WORD)Length;
    Ace->Mask = AccessMask;
    CopySid(GetLengthSid(Sid), (PSID)&Ace->SidStart, Sid);

    Acl->AceCount++;
    if (Acl->AclRevision < AceRevision)
        Acl->AclRevision = (BYTE)AceRevision;
    return TRUE;
}

static
DWORD
BasepClaimValueSize(
    _In_ PCLAIM_SECURITY_ATTRIBUTE_V1 Attribute,
    _In_ DWORD Index)
{
    switch (Attribute->ValueType)
    {
        case CLAIM_SECURITY_ATTRIBUTE_TYPE_INT64:
        case CLAIM_SECURITY_ATTRIBUTE_TYPE_UINT64:
        case CLAIM_SECURITY_ATTRIBUTE_TYPE_BOOLEAN:
            return sizeof(ULONG64);
        case CLAIM_SECURITY_ATTRIBUTE_TYPE_STRING:
            return (DWORD)((lstrlenW(Attribute->Values.ppString[Index]) + 1) * sizeof(WCHAR));
        case CLAIM_SECURITY_ATTRIBUTE_TYPE_FQBN:
            return (DWORD)(sizeof(ULONG64) + sizeof(ULONG) +
                           (lstrlenW(Attribute->Values.pFqbn[Index].Name) + 1) * sizeof(WCHAR));
        case CLAIM_SECURITY_ATTRIBUTE_TYPE_SID:
        case CLAIM_SECURITY_ATTRIBUTE_TYPE_OCTET_STRING:
            return sizeof(ULONG) + Attribute->Values.pOctetString[Index].ValueLength;
        default:
            return 0;
    }
}

static
BOOL
BasepIsWorldSid(
    _In_ PSID Sid)
{
    static const SID_IDENTIFIER_AUTHORITY WorldAuthority = SECURITY_WORLD_SID_AUTHORITY;

    return IsValidSid(Sid) &&
           *GetSidSubAuthorityCount(Sid) == 1 &&
           memcmp(GetSidIdentifierAuthority(Sid), &WorldAuthority, sizeof(WorldAuthority)) == 0 &&
           *GetSidSubAuthority(Sid, 0) == SECURITY_WORLD_RID;
}

BOOL
WINAPI
AddResourceAttributeAce(
    _Inout_ PACL Acl,
    _In_ DWORD AceRevision,
    _In_ DWORD AceFlags,
    _In_ DWORD AccessMask,
    _In_ PSID Sid,
    _In_ PCLAIM_SECURITY_ATTRIBUTES_INFORMATION AttributeInfo,
    _Out_ PDWORD ReturnLength)
{
    ACL_SIZE_INFORMATION Size;
    PCLAIM_SECURITY_ATTRIBUTE_V1 Attribute;
    PCLAIM_SECURITY_ATTRIBUTE_RELATIVE_V1 Relative;
    PSYSTEM_RESOURCE_ATTRIBUTE_ACE Ace;
    PUCHAR Values, Data;
    DWORD Index, NameLength, AttributeLength, SidLength, AceLength, DataOffset, RequiredLength;

    if (!ReturnLength)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }
    *ReturnLength = 0;

    if (!Acl || !Sid || !AttributeInfo || !BasepIsWorldSid(Sid) || !IsValidAcl(Acl) ||
        Acl->AclRevision > ACL_REVISION_DS || AceRevision > ACL_REVISION_DS ||
        (AceFlags & ~VALID_INHERIT_FLAGS) || AccessMask != 0 ||
        AttributeInfo->Version != CLAIM_SECURITY_ATTRIBUTES_INFORMATION_VERSION_V1 ||
        AttributeInfo->AttributeCount != 1)
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    Attribute = AttributeInfo->Attribute.pAttributeV1;
    if (!Attribute || !Attribute->Name || Attribute->Reserved != 0 ||
        (Attribute->Flags & ~(CLAIM_SECURITY_ATTRIBUTE_VALID_FLAGS | CLAIM_SECURITY_ATTRIBUTE_CUSTOM_FLAGS)))
    {
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    NameLength = (DWORD)((lstrlenW(Attribute->Name) + 1) * sizeof(WCHAR));
    for (Index = 0; Index < Attribute->ValueCount; Index++)
    {
        DWORD ValueLength = BasepClaimValueSize(Attribute, Index);
        if (!ValueLength)
        {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
        }
    }

    AttributeLength = FIELD_OFFSET(CLAIM_SECURITY_ATTRIBUTE_RELATIVE_V1, Values) +
                      max(Attribute->ValueCount, 1) * sizeof(ULONG);
    DataOffset = BASEP_ROUND_UP(AttributeLength + NameLength, sizeof(ULONG));
    AttributeLength = DataOffset;
    for (Index = 0; Index < Attribute->ValueCount; Index++)
    {
        DWORD Alignment = (Attribute->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_INT64 ||
                           Attribute->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_UINT64 ||
                           Attribute->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_BOOLEAN ||
                           Attribute->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_FQBN)
                          ? sizeof(ULONG64) : sizeof(ULONG);
        AttributeLength = BASEP_ROUND_UP(AttributeLength, Alignment);
        AttributeLength += BASEP_ROUND_UP(BasepClaimValueSize(Attribute, Index), sizeof(ULONG));
    }

    SidLength = GetLengthSid(Sid);
    AceLength = BASEP_ROUND_UP(FIELD_OFFSET(SYSTEM_RESOURCE_ATTRIBUTE_ACE, SidStart) + SidLength, sizeof(ULONG)) + AttributeLength;

    if (!GetAclInformation(Acl, &Size, sizeof(Size), AclSizeInformation))
        return FALSE;
    RequiredLength = BASEP_ROUND_UP(Size.AclBytesInUse + AceLength, sizeof(ULONG));
    *ReturnLength = RequiredLength;
    if (Size.AclBytesFree < AceLength)
    {
        SetLastError(ERROR_ALLOTTED_SPACE_EXCEEDED);
        return FALSE;
    }

    Ace = (PSYSTEM_RESOURCE_ATTRIBUTE_ACE)((PUCHAR)Acl + Acl->AclSize - Size.AclBytesFree);
    ZeroMemory(Ace, AceLength);
    Ace->Header.AceType = SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE;
    Ace->Header.AceFlags = (BYTE)AceFlags;
    Ace->Header.AceSize = (WORD)AceLength;
    Ace->Mask = AccessMask;
    CopySid(SidLength, (PSID)&Ace->SidStart, Sid);

    Relative = (PCLAIM_SECURITY_ATTRIBUTE_RELATIVE_V1)((PUCHAR)Ace +
               BASEP_ROUND_UP(FIELD_OFFSET(SYSTEM_RESOURCE_ATTRIBUTE_ACE, SidStart) + SidLength, sizeof(ULONG)));
    Relative->ValueType = Attribute->ValueType;
    Relative->Flags = Attribute->Flags;
    Relative->ValueCount = Attribute->ValueCount;
    Values = (PUCHAR)&Relative->Values;
    Relative->Name = FIELD_OFFSET(CLAIM_SECURITY_ATTRIBUTE_RELATIVE_V1, Values) +
                     max(Attribute->ValueCount, 1) * sizeof(ULONG);
    CopyMemory((PUCHAR)Relative + Relative->Name, Attribute->Name, NameLength);
    Data = (PUCHAR)Relative + DataOffset;

    for (Index = 0; Index < Attribute->ValueCount; Index++)
    {
        DWORD ValueLength = BasepClaimValueSize(Attribute, Index);
        DWORD Alignment = (Attribute->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_INT64 ||
                           Attribute->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_UINT64 ||
                           Attribute->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_BOOLEAN ||
                           Attribute->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_FQBN)
                          ? sizeof(ULONG64) : sizeof(ULONG);

        Data = (PUCHAR)Relative + BASEP_ROUND_UP((ULONG)(Data - (PUCHAR)Relative), Alignment);
        ((PULONG)Values)[Index] = (ULONG)(Data - (PUCHAR)Relative);

        switch (Attribute->ValueType)
        {
            case CLAIM_SECURITY_ATTRIBUTE_TYPE_INT64:
            case CLAIM_SECURITY_ATTRIBUTE_TYPE_UINT64:
            case CLAIM_SECURITY_ATTRIBUTE_TYPE_BOOLEAN:
                CopyMemory(Data, &Attribute->Values.pInt64[Index], sizeof(ULONG64));
                break;
            case CLAIM_SECURITY_ATTRIBUTE_TYPE_STRING:
                CopyMemory(Data, Attribute->Values.ppString[Index], ValueLength);
                break;
            case CLAIM_SECURITY_ATTRIBUTE_TYPE_FQBN:
            {
                DWORD NamePart = ValueLength - sizeof(ULONG64) - sizeof(ULONG);
                CopyMemory(Data, &Attribute->Values.pFqbn[Index].Version, sizeof(ULONG64));
                *(PULONG)(Data + sizeof(ULONG64)) = sizeof(ULONG64) + sizeof(ULONG);
                CopyMemory(Data + sizeof(ULONG64) + sizeof(ULONG), Attribute->Values.pFqbn[Index].Name, NamePart);
                break;
            }
            default:
                *(PULONG)Data = Attribute->Values.pOctetString[Index].ValueLength;
                CopyMemory(Data + sizeof(ULONG),
                           Attribute->Values.pOctetString[Index].pValue,
                           Attribute->Values.pOctetString[Index].ValueLength);
                break;
        }
        Data += BASEP_ROUND_UP(ValueLength, sizeof(ULONG));
    }

    Acl->AceCount++;
    if (Acl->AclRevision < AceRevision)
        Acl->AclRevision = (BYTE)AceRevision;
    return TRUE;
}
