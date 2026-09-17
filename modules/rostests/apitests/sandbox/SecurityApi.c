/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AppContainer SID helpers and the conditional/resource ACE surface
 */

#include "precomp.h"

typedef BOOLEAN (NTAPI *PFN_RtlIsCapabilitySid)(PSID);
typedef NTSTATUS (NTAPI *PFN_RtlGetAppContainerSidType)(PSID, PULONG);
typedef NTSTATUS (NTAPI *PFN_RtlGetAppContainerParent)(PSID, PSID *);
typedef BOOLEAN (NTAPI *PFN_RtlIsParentOfChildAppContainer)(PSID, PSID);
typedef NTSTATUS (NTAPI *PFN_RtlCheckTokenMembershipEx)(HANDLE, PSID, ULONG, PBOOLEAN);
typedef BOOL (WINAPI *PFN_GetAppContainerAce)(PACL, DWORD, PVOID *, DWORD *);
typedef BOOL (WINAPI *PFN_AddScopedPolicyIDAce)(PACL, DWORD, DWORD, DWORD, PSID);
typedef BOOL (WINAPI *PFN_AddResourceAttributeAce)(PACL, DWORD, DWORD, DWORD, PSID, PCLAIM_SECURITY_ATTRIBUTES_INFORMATION, PDWORD);

static PFN_GetAppContainerAce pGetAppContainerAce;
static PFN_AddScopedPolicyIDAce pAddScopedPolicyIDAce;
static PFN_AddResourceAttributeAce pAddResourceAttributeAce;

static PSID
MakePackageSid(ULONG Count, ULONG Seed)
{
    SID_IDENTIFIER_AUTHORITY PackageAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
    PSID Sid = LocalAlloc(LPTR, SECURITY_MAX_SID_SIZE);
    ULONG Index;

    if (!Sid) return NULL;
    InitializeSid(Sid, &PackageAuthority, (BYTE)Count);
    *GetSidSubAuthority(Sid, 0) = SECURITY_APP_PACKAGE_BASE_RID;
    for (Index = 1; Index < Count; Index++)
        *GetSidSubAuthority(Sid, Index) = Seed + Index;
    return Sid;
}

static PSID
MakeCapabilitySid(ULONG Rid)
{
    SID_IDENTIFIER_AUTHORITY PackageAuthority = {SECURITY_APP_PACKAGE_AUTHORITY};
    PSID Sid = NULL;

    AllocateAndInitializeSid(&PackageAuthority, 2, SECURITY_CAPABILITY_BASE_RID, Rid, 0, 0, 0, 0, 0, 0, &Sid);
    return Sid;
}

static void
TestSidClassification(void)
{
    HMODULE Ntdll = GetModuleHandleW(L"ntdll.dll");
    PFN_RtlIsCapabilitySid pRtlIsCapabilitySid = (PFN_RtlIsCapabilitySid)GetProcAddress(Ntdll, "RtlIsCapabilitySid");
    PFN_RtlGetAppContainerSidType pRtlGetAppContainerSidType = (PFN_RtlGetAppContainerSidType)GetProcAddress(Ntdll, "RtlGetAppContainerSidType");
    PFN_RtlGetAppContainerParent pRtlGetAppContainerParent = (PFN_RtlGetAppContainerParent)GetProcAddress(Ntdll, "RtlGetAppContainerParent");
    PFN_RtlIsParentOfChildAppContainer pRtlIsParentOfChildAppContainer = (PFN_RtlIsParentOfChildAppContainer)GetProcAddress(Ntdll, "RtlIsParentOfChildAppContainer");
    SID_IDENTIFIER_AUTHORITY NtAuthority = SECURITY_NT_AUTHORITY;
    PSID Parent = NULL, Child = NULL, Other = NULL, Capability = NULL, Ordinary = NULL, Derived = NULL;
    ULONG Type;
    NTSTATUS Status;

    if (!pRtlIsCapabilitySid || !pRtlGetAppContainerSidType || !pRtlGetAppContainerParent || !pRtlIsParentOfChildAppContainer)
    {
        skip("AppContainer SID helpers missing from ntdll\n");
        return;
    }

    Parent = MakePackageSid(SECURITY_APP_PACKAGE_RID_COUNT, 0x1000);
    Child = MakePackageSid(SECURITY_CHILD_PACKAGE_RID_COUNT, 0x1000);
    Other = MakePackageSid(SECURITY_CHILD_PACKAGE_RID_COUNT, 0x2000);
    Capability = MakeCapabilitySid(SECURITY_CAPABILITY_INTERNET_CLIENT);
    AllocateAndInitializeSid(&NtAuthority, 1, SECURITY_LOCAL_SYSTEM_RID, 0, 0, 0, 0, 0, 0, 0, &Ordinary);
    ok(Parent && Child && Other && Capability && Ordinary, "SID setup failed %lu\n", GetLastError());
    if (!Parent || !Child || !Other || !Capability || !Ordinary) goto Cleanup;

    ok(pRtlIsCapabilitySid(Capability), "capability SID not recognized\n");
    ok(!pRtlIsCapabilitySid(Parent), "package SID reported as a capability\n");
    ok(!pRtlIsCapabilitySid(Ordinary), "NT SID reported as a capability\n");

    Type = 0xFF;
    Status = pRtlGetAppContainerSidType(Parent, &Type);
    ok(NT_SUCCESS(Status) && Type == ParentAppContainerSidType, "parent SID type 0x%lx %lu\n", Status, Type);
    Type = 0xFF;
    Status = pRtlGetAppContainerSidType(Child, &Type);
    ok(NT_SUCCESS(Status) && Type == ChildAppContainerSidType, "child SID type 0x%lx %lu\n", Status, Type);
    Type = 0xFF;
    Status = pRtlGetAppContainerSidType(Ordinary, &Type);
    ok(Status == STATUS_NOT_APPCONTAINER && Type == NotAppContainerSidType,
       "NT SID type 0x%lx %lu\n", Status, Type);
    Type = 0xFF;
    Status = pRtlGetAppContainerSidType(Capability, &Type);
    ok(Status == STATUS_NOT_APPCONTAINER && Type == NotAppContainerSidType,
       "capability SID type 0x%lx %lu\n", Status, Type);

    Status = pRtlGetAppContainerParent(Child, &Derived);
    ok(NT_SUCCESS(Status) && Derived != NULL, "RtlGetAppContainerParent failed 0x%lx\n", Status);
    if (Derived)
    {
        ok(EqualSid(Derived, Parent), "derived parent does not match the parent SID\n");
        RtlFreeSid(Derived);
        Derived = NULL;
    }
    ok(!NT_SUCCESS(pRtlGetAppContainerParent(Parent, &Derived)), "parent SID accepted as a child\n");
    if (Derived) { RtlFreeSid(Derived); Derived = NULL; }

    ok(pRtlIsParentOfChildAppContainer(Parent, Child), "parent/child relation not recognized\n");
    ok(!pRtlIsParentOfChildAppContainer(Parent, Other), "unrelated child accepted\n");
    ok(!pRtlIsParentOfChildAppContainer(Child, Parent), "reversed relation accepted\n");

Cleanup:
    if (Parent) LocalFree(Parent);
    if (Child) LocalFree(Child);
    if (Other) LocalFree(Other);
    if (Capability) FreeSid(Capability);
    if (Ordinary) FreeSid(Ordinary);
}

static void
TestAppContainerAce(void)
{
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    UCHAR AclBuffer[1024];
    PACL Acl = (PACL)AclBuffer;
    PSID World = NULL, Package = NULL, Capability = NULL;
    PACCESS_ALLOWED_ACE Found = NULL;
    DWORD Index = 0xFFFF;

    ok(InitializeAcl(Acl, sizeof(AclBuffer), ACL_REVISION), "InitializeAcl failed %lu\n", GetLastError());
    ok(AllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &World),
       "world SID failed %lu\n", GetLastError());
    Package = MakePackageSid(SECURITY_APP_PACKAGE_RID_COUNT, 0x3000);
    Capability = MakeCapabilitySid(SECURITY_CAPABILITY_INTERNET_CLIENT);
    if (!World || !Package || !Capability) goto Cleanup;

    ok(AddAccessAllowedAce(Acl, ACL_REVISION, GENERIC_READ, World), "world ACE failed %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(!pGetAppContainerAce(Acl, 0, (PVOID *)&Found, &Index) && GetLastError() == ERROR_NOT_FOUND,
       "GetAppContainerAce on a package-free ACL: %lu\n", GetLastError());

    ok(AddAccessAllowedAce(Acl, ACL_REVISION, GENERIC_EXECUTE, Package), "package ACE failed %lu\n", GetLastError());
    ok(AddAccessAllowedAce(Acl, ACL_REVISION, GENERIC_READ, Capability), "capability ACE failed %lu\n", GetLastError());

    Found = NULL;
    Index = 0xFFFF;
    ok(pGetAppContainerAce(Acl, 0, (PVOID *)&Found, &Index), "GetAppContainerAce failed %lu\n", GetLastError());
    ok(Index == 1, "AppContainer ACE index %lu\n", Index);
    ok(Found != NULL && EqualSid((PSID)&Found->SidStart, Package), "wrong ACE returned\n");

    Found = NULL;
    Index = 0xFFFF;
    ok(pGetAppContainerAce(Acl, 2, (PVOID *)&Found, &Index),
       "capability ACE not reported as an AppContainer ACE: %lu\n", GetLastError());
    ok(Index == 2, "capability ACE index %lu\n", Index);
    ok(Found != NULL && EqualSid((PSID)&Found->SidStart, Capability), "wrong capability ACE returned\n");

    ok(pGetAppContainerAce(Acl, 1, (PVOID *)&Found, NULL), "GetAppContainerAce without an index failed %lu\n", GetLastError());

    SetLastError(0xdeadbeef);
    ok(!pGetAppContainerAce(Acl, 3, (PVOID *)&Found, &Index) && GetLastError() == ERROR_INVALID_PARAMETER,
       "GetAppContainerAce past the last ACE: %lu\n", GetLastError());
    SetLastError(0xdeadbeef);
    ok(!pGetAppContainerAce(NULL, 0, (PVOID *)&Found, &Index) && GetLastError() == ERROR_INVALID_PARAMETER,
       "GetAppContainerAce(NULL acl): %lu\n", GetLastError());

Cleanup:
    if (World) FreeSid(World);
    if (Package) LocalFree(Package);
    if (Capability) FreeSid(Capability);
}

static void
TestScopedPolicyAce(void)
{
    SID_IDENTIFIER_AUTHORITY PolicyAuthority = SECURITY_SCOPED_POLICY_ID_AUTHORITY;
    UCHAR AclBuffer[512];
    PACL Acl = (PACL)AclBuffer;
    PSID Policy = NULL;
    PSYSTEM_SCOPED_POLICY_ID_ACE Ace = NULL;
    ACL_SIZE_INFORMATION Size;
    BOOL Result;
    DWORD Error;

    ok(InitializeAcl(Acl, sizeof(AclBuffer), ACL_REVISION), "InitializeAcl failed %lu\n", GetLastError());
    ok(AllocateAndInitializeSid(&PolicyAuthority, 1, 0x1234, 0, 0, 0, 0, 0, 0, 0, &Policy),
       "policy SID failed %lu\n", GetLastError());
    if (!Policy) return;

    SetLastError(0xdeadbeef);
    Result = pAddScopedPolicyIDAce(Acl, ACL_REVISION_DS + 1, 0, 0, Policy);
    Error = GetLastError();
    ok(!Result && Error == ERROR_REVISION_MISMATCH,
       "AddScopedPolicyIDAce revision-5 behavior: %u, %lu\n", Result, Error);

    ok(InitializeAcl(Acl, sizeof(AclBuffer), ACL_REVISION_DS),
       "InitializeAcl(ACL_REVISION_DS) failed %lu\n", GetLastError());
    ok(pAddScopedPolicyIDAce(Acl, ACL_REVISION_DS, CONTAINER_INHERIT_ACE, 0, Policy),
       "AddScopedPolicyIDAce failed %lu\n", GetLastError());
    ok(GetAclInformation(Acl, &Size, sizeof(Size), AclSizeInformation) && Size.AceCount == 1,
       "ACE count %lu\n", Size.AceCount);
    ok(GetAce(Acl, 0, (PVOID *)&Ace), "GetAce failed %lu\n", GetLastError());
    if (Ace)
    {
        ok(Ace->Header.AceType == SYSTEM_SCOPED_POLICY_ID_ACE_TYPE, "ACE type 0x%x\n", Ace->Header.AceType);
        ok(Ace->Header.AceFlags == CONTAINER_INHERIT_ACE, "ACE flags 0x%x\n", Ace->Header.AceFlags);
        ok(Ace->Header.AceSize == FIELD_OFFSET(SYSTEM_SCOPED_POLICY_ID_ACE, SidStart) + GetLengthSid(Policy),
           "ACE size %u\n", Ace->Header.AceSize);
        ok(EqualSid((PSID)&Ace->SidStart, Policy), "ACE SID mismatch\n");
    }
    ok(IsValidAcl(Acl), "ACL invalid after the scoped policy ACE\n");
    FreeSid(Policy);
}

static void
TestResourceAttributeAce(void)
{
    SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
    UCHAR AclBuffer[1024];
    PACL Acl = (PACL)AclBuffer;
    PSID World = NULL;
    CLAIM_SECURITY_ATTRIBUTE_V1 Attribute;
    CLAIM_SECURITY_ATTRIBUTES_INFORMATION Info;
    PSYSTEM_RESOURCE_ATTRIBUTE_ACE Ace = NULL;
    PCLAIM_SECURITY_ATTRIBUTE_RELATIVE_V1 Relative;
    LONG64 Values[2] = {0x1122334455667788ll, 7};
    PWSTR Strings[2] = {L"alpha", L"beta"};
    DWORD Length = 0, SidLength;

    ok(InitializeAcl(Acl, sizeof(AclBuffer), ACL_REVISION), "InitializeAcl failed %lu\n", GetLastError());
    ok(AllocateAndInitializeSid(&WorldAuthority, 1, SECURITY_WORLD_RID, 0, 0, 0, 0, 0, 0, 0, &World),
       "world SID failed %lu\n", GetLastError());
    if (!World) return;
    SidLength = GetLengthSid(World);

    ZeroMemory(&Attribute, sizeof(Attribute));
    Attribute.Name = L"SbxInt";
    Attribute.ValueType = CLAIM_SECURITY_ATTRIBUTE_TYPE_INT64;
    Attribute.Flags = CLAIM_SECURITY_ATTRIBUTE_MANDATORY;
    Attribute.ValueCount = 2;
    Attribute.Values.pInt64 = Values;
    Info.Version = CLAIM_SECURITY_ATTRIBUTES_INFORMATION_VERSION_V1;
    Info.Reserved = 0;
    Info.AttributeCount = 1;
    Info.Attribute.pAttributeV1 = &Attribute;

    SetLastError(0xdeadbeef);
    Info.AttributeCount = 2;
    ok(!pAddResourceAttributeAce(Acl, ACL_REVISION_DS, 0, 0, World, &Info, &Length) && GetLastError() == ERROR_INVALID_PARAMETER,
       "AddResourceAttributeAce accepted two attributes: %lu\n", GetLastError());
    Info.AttributeCount = 1;

    ok(pAddResourceAttributeAce(Acl, ACL_REVISION_DS, 0, 0, World, &Info, &Length),
       "AddResourceAttributeAce(int64) failed %lu\n", GetLastError());
    ok(Length != 0, "returned length is zero\n");
    ok(GetAce(Acl, 0, (PVOID *)&Ace), "GetAce failed %lu\n", GetLastError());
    if (Ace)
    {
        ok(Ace->Header.AceType == SYSTEM_RESOURCE_ATTRIBUTE_ACE_TYPE, "ACE type 0x%x\n", Ace->Header.AceType);
        ok(Length == sizeof(ACL) + Ace->Header.AceSize,
           "required ACL length %lu, ACE size %u\n", Length, Ace->Header.AceSize);
        ok(EqualSid((PSID)&Ace->SidStart, World), "ACE SID mismatch\n");

        Relative = (PCLAIM_SECURITY_ATTRIBUTE_RELATIVE_V1)((PUCHAR)Ace +
                   ((FIELD_OFFSET(SYSTEM_RESOURCE_ATTRIBUTE_ACE, SidStart) + SidLength + 3) & ~3));
        ok(Relative->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_INT64, "relative value type %u\n", Relative->ValueType);
        ok(Relative->ValueCount == 2, "relative value count %lu\n", Relative->ValueCount);
        ok(Relative->Flags == CLAIM_SECURITY_ATTRIBUTE_MANDATORY, "relative flags 0x%lx\n", Relative->Flags);
        ok(Relative->Name != 0 && wcscmp((PWSTR)((PUCHAR)Relative + Relative->Name), L"SbxInt") == 0,
           "relative name offset %lu\n", Relative->Name);
        ok(Relative->Values.pInt64[0] != 0 && Relative->Values.pInt64[1] != 0,
           "relative int64 offsets are zero\n");
        ok(*(PLONG64)((PUCHAR)Relative + Relative->Values.pInt64[0]) == Values[0] &&
           *(PLONG64)((PUCHAR)Relative + Relative->Values.pInt64[1]) == Values[1],
           "relative int64 values wrong (offsets %lu, %lu)\n",
           Relative->Values.pInt64[0], Relative->Values.pInt64[1]);
    }
    ok(IsValidAcl(Acl), "ACL invalid after the int64 attribute ACE\n");

    Attribute.Name = L"SbxStr";
    Attribute.ValueType = CLAIM_SECURITY_ATTRIBUTE_TYPE_STRING;
    Attribute.Flags = 0;
    Attribute.ValueCount = 2;
    Attribute.Values.ppString = Strings;
    Length = 0;
    ok(pAddResourceAttributeAce(Acl, ACL_REVISION_DS, 0, 0, World, &Info, &Length),
       "AddResourceAttributeAce(string) failed %lu\n", GetLastError());
    ok(GetAce(Acl, 1, (PVOID *)&Ace), "GetAce(1) failed %lu\n", GetLastError());
    if (Ace)
    {
        Relative = (PCLAIM_SECURITY_ATTRIBUTE_RELATIVE_V1)((PUCHAR)Ace +
                   ((FIELD_OFFSET(SYSTEM_RESOURCE_ATTRIBUTE_ACE, SidStart) + SidLength + 3) & ~3));
        ok(Relative->ValueType == CLAIM_SECURITY_ATTRIBUTE_TYPE_STRING, "string value type %u\n", Relative->ValueType);
        ok(wcscmp((PWSTR)((PUCHAR)Relative + Relative->Name), L"SbxStr") == 0, "string attribute name wrong\n");
        ok(wcscmp((PWSTR)((PUCHAR)Relative + Relative->Values.ppString[0]), L"alpha") == 0, "string value 0 wrong\n");
        ok(wcscmp((PWSTR)((PUCHAR)Relative + Relative->Values.ppString[1]), L"beta") == 0, "string value 1 wrong\n");
    }
    ok(IsValidAcl(Acl), "ACL invalid after the string attribute ACE\n");
    FreeSid(World);
}

static void
TestSandboxedTokenClasses(void)
{
    HANDLE Base = SbxOpenToken(TOKEN_QUERY | TOKEN_DUPLICATE);
    HANDLE Restricted = NULL;
    SID_IDENTIFIER_AUTHORITY NullAuthority = SECURITY_NULL_SID_AUTHORITY;
    SID_AND_ATTRIBUTES Restrict;
    PSID NullSid = NULL;
    DWORD Value, Length;

    ok(Base != NULL, "OpenProcessToken failed %lu\n", GetLastError());
    if (!Base) return;

    Value = 0xFF;
    ok(GetTokenInformation(Base, TokenIsSandboxed, &Value, sizeof(Value), &Length) && Value == 0,
       "TokenIsSandboxed on the base token: %lu (%lu)\n", Value, GetLastError());
    Value = 0xFF;
    SetLastError(0xdeadbeef);
    ok(!GetTokenInformation(Base, TokenIsLessPrivilegedAppContainer, &Value, sizeof(Value), &Length) &&
       GetLastError() == ERROR_INVALID_PARAMETER,
       "TokenIsLessPrivilegedAppContainer on the base token: %lu (%lu)\n", Value, GetLastError());

    ok(AllocateAndInitializeSid(&NullAuthority, 1, SECURITY_NULL_RID, 0, 0, 0, 0, 0, 0, 0, &NullSid),
       "null SID failed %lu\n", GetLastError());
    if (NullSid)
    {
        Restrict.Sid = NullSid;
        Restrict.Attributes = 0;
        ok(CreateRestrictedToken(Base, 0, 0, NULL, 0, NULL, 1, &Restrict, &Restricted),
           "CreateRestrictedToken failed %lu\n", GetLastError());
        if (Restricted)
        {
            Value = 0xFF;
            ok(GetTokenInformation(Restricted, TokenIsSandboxed, &Value, sizeof(Value), &Length) && Value == 0,
               "TokenIsSandboxed on a restricted token: %lu (%lu)\n", Value, GetLastError());
            CloseHandle(Restricted);
        }
        FreeSid(NullSid);
    }
    CloseHandle(Base);
}

typedef LONG (WINAPI *PFN_GetPackageFromToken)(HANDLE, UINT32 *, PWSTR);
typedef HRESULT (WINAPI *PFN_GetAppContainerFolderPath)(PCWSTR, PWSTR *);

#define PACKAGE_MONIKER L"SbxSecurityApiPackage"

static void
TestPackageIdentity(void)
{
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");
    PFN_GetPackageFromToken pFamily = (PFN_GetPackageFromToken)GetProcAddress(KernelBase, "GetPackageFamilyNameFromToken");
    PFN_GetPackageFromToken pFull = (PFN_GetPackageFromToken)GetProcAddress(KernelBase, "GetPackageFullNameFromToken");
    PFN_GetPackageFromToken pAumid = (PFN_GetPackageFromToken)GetProcAddress(KernelBase, "GetApplicationUserModelIdFromToken");
    PFN_GetAppContainerFolderPath pFolder = (PFN_GetAppContainerFolderPath)GetProcAddress(LoadLibraryW(L"userenv.dll"), "GetAppContainerFolderPath");
    PFN_NtCreateLowBoxToken pNtCreateLowBoxToken = (PFN_NtCreateLowBoxToken)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtCreateLowBoxToken");
    HANDLE Base = NULL, LowBox = NULL;
    PSID Package = NULL;
    LPWSTR PackageString = NULL, Folder = NULL;
    WCHAR Name[128];
    UINT32 Length;
    LONG Error;
    HRESULT hr;
    NTSTATUS Status;

    if (!pFamily || !pFull || !pAumid || !pFolder || !pNtCreateLowBoxToken)
    {
        skip("package identity APIs missing\n");
        return;
    }

    DeleteAppContainerProfile(PACKAGE_MONIKER);
    hr = CreateAppContainerProfile(PACKAGE_MONIKER, L"Sbx display", L"Sbx description", NULL, 0, &Package);
    ok(hr == S_OK && Package != NULL, "CreateAppContainerProfile failed 0x%lx\n", hr);
    if (!Package) return;

    Base = SbxOpenToken(TOKEN_QUERY | TOKEN_DUPLICATE);
    ok(Base != NULL, "OpenProcessToken failed %lu\n", GetLastError());
    if (!Base) goto Cleanup;

    Length = ARRAYSIZE(Name);
    Error = pFamily(Base, &Length, Name);
    ok(Error == APPMODEL_ERROR_NO_PACKAGE, "family name of a non-AppContainer token: %ld\n", Error);

    Status = pNtCreateLowBoxToken(&LowBox, Base, TOKEN_ALL_ACCESS, NULL, Package, 0, NULL, 0, NULL);
    ok(NT_SUCCESS(Status), "NtCreateLowBoxToken failed 0x%lx\n", Status);
    if (!LowBox) goto Cleanup;

    Length = 0;
    Error = pFamily(LowBox, &Length, NULL);
    ok(Error == APPMODEL_ERROR_NO_PACKAGE && Length == 0,
       "family name size query: %ld %u\n", Error, Length);

    Length = ARRAYSIZE(Name);
    Error = pFamily(LowBox, &Length, Name);
    ok(Error == APPMODEL_ERROR_NO_PACKAGE, "family name: %ld\n", Error);

    Length = ARRAYSIZE(Name);
    Error = pFull(LowBox, &Length, Name);
    ok(Error == APPMODEL_ERROR_NO_PACKAGE, "full name: %ld\n", Error);

    Length = ARRAYSIZE(Name);
    Error = pAumid(LowBox, &Length, Name);
    ok(Error == APPMODEL_ERROR_NO_APPLICATION, "AUMID of an application-less package: %ld\n", Error);

    ok(ConvertSidToStringSidW(Package, &PackageString), "ConvertSidToStringSid failed %lu\n", GetLastError());
    if (PackageString)
    {
        hr = pFolder(PackageString, &Folder);
        ok(hr == S_OK && Folder != NULL, "GetAppContainerFolderPath failed 0x%lx\n", hr);
        if (Folder)
        {
            ok(wcsstr(Folder, L"\\Packages\\sbxsecurityapipackage\\AC") != NULL,
               "folder path %S\n", Folder);
            CoTaskMemFree(Folder);
        }
        hr = pFolder(NULL, &Folder);
        ok(hr == E_INVALIDARG, "GetAppContainerFolderPath(NULL) 0x%lx\n", hr);
        LocalFree(PackageString);
    }

Cleanup:
    if (LowBox) CloseHandle(LowBox);
    if (Base) CloseHandle(Base);
    if (Package) FreeSid(Package);
    DeleteAppContainerProfile(PACKAGE_MONIKER);
}

static void
TestWellKnownCapabilitySids(void)
{
    static const struct
    {
        WELL_KNOWN_SID_TYPE Type;
        PCWSTR String;
    } Expected[] =
    {
        { WinBuiltinAnyPackageSid, L"S-1-15-2-1" },
        { WinCapabilityInternetClientSid, L"S-1-15-3-1" },
        { WinCapabilityInternetClientServerSid, L"S-1-15-3-2" },
        { WinCapabilityPrivateNetworkClientServerSid, L"S-1-15-3-3" },
        { WinCapabilityPicturesLibrarySid, L"S-1-15-3-4" },
        { WinCapabilityVideosLibrarySid, L"S-1-15-3-5" },
        { WinCapabilityMusicLibrarySid, L"S-1-15-3-6" },
        { WinCapabilityDocumentsLibrarySid, L"S-1-15-3-7" },
        { WinCapabilityEnterpriseAuthenticationSid, L"S-1-15-3-8" },
        { WinCapabilitySharedUserCertificatesSid, L"S-1-15-3-9" },
        { WinCapabilityRemovableStorageSid, L"S-1-15-3-10" },
        { WinCapabilityAppointmentsSid, L"S-1-15-3-11" },
        { WinCapabilityContactsSid, L"S-1-15-3-12" },
    };
    UCHAR Buffer[SECURITY_MAX_SID_SIZE];
    DWORD Size, Index;
    LPWSTR String;

    Size = sizeof(Buffer);
    SetLastError(0xdeadbeef);
    ok(!CreateWellKnownSid(WinApplicationPackageAuthoritySid, NULL, (PSID)Buffer, &Size) &&
       GetLastError() == ERROR_INVALID_PARAMETER,
       "CreateWellKnownSid(WinApplicationPackageAuthoritySid): %lu\n", GetLastError());

    for (Index = 0; Index < ARRAYSIZE(Expected); Index++)
    {
        Size = sizeof(Buffer);
        if (!CreateWellKnownSid(Expected[Index].Type, NULL, (PSID)Buffer, &Size))
        {
            ok(0, "CreateWellKnownSid(%u) failed %lu\n", Expected[Index].Type, GetLastError());
            continue;
        }
        if (!ConvertSidToStringSidW((PSID)Buffer, &String))
            continue;
        ok(wcscmp(String, Expected[Index].String) == 0, "type %u: %S != %S\n", Expected[Index].Type, String, Expected[Index].String);
        LocalFree(String);
        ok(IsWellKnownSid((PSID)Buffer, Expected[Index].Type), "IsWellKnownSid(%u) failed\n", Expected[Index].Type);
        ok(Size == GetLengthSid((PSID)Buffer), "type %u: returned size %lu\n", Expected[Index].Type, Size);
    }
}

START_TEST(SecurityApi)
{
    HMODULE KernelBase = GetModuleHandleW(L"kernelbase.dll");

    pGetAppContainerAce = (PFN_GetAppContainerAce)GetProcAddress(KernelBase, "GetAppContainerAce");
    pAddScopedPolicyIDAce = (PFN_AddScopedPolicyIDAce)GetProcAddress(KernelBase, "AddScopedPolicyIDAce");
    pAddResourceAttributeAce = (PFN_AddResourceAttributeAce)GetProcAddress(KernelBase, "AddResourceAttributeAce");
    ok(pGetAppContainerAce != NULL, "kernelbase!GetAppContainerAce missing\n");
    ok(pAddScopedPolicyIDAce != NULL, "kernelbase!AddScopedPolicyIDAce missing\n");
    ok(pAddResourceAttributeAce != NULL, "kernelbase!AddResourceAttributeAce missing\n");

    TestWellKnownCapabilitySids();
    TestSidClassification();
    if (pGetAppContainerAce) TestAppContainerAce();
    if (pAddScopedPolicyIDAce) TestScopedPolicyAce();
    if (pAddResourceAttributeAce) TestResourceAttributeAce();
    TestSandboxedTokenClasses();
    TestPackageIdentity();
}
