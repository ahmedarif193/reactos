/*
 * PROJECT:         ReactOS API tests
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Tests for the NtAccessCheckByTypeResultList API
 * COPYRIGHT:       Copyright 2023 George Bișoc <george.bisoc@reactos.org>
 */

#include "precomp.h"

static GENERIC_MAPPING RegMapping = {KEY_READ, KEY_WRITE, KEY_EXECUTE, KEY_ALL_ACCESS};
static GUID ObjectType = {0x12345678, 0x1234, 0x5678, {0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88}};
static GUID ChildObjectType = {0x23456789, 0x2345, 0x6786, {0x2, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99}};
static SID_IDENTIFIER_AUTHORITY WorldAuthority = {SECURITY_WORLD_SID_AUTHORITY};
static SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};

static
HANDLE
GetTokenProcess(
    _In_ BOOLEAN WantImpersonateLevel,
    _In_ BOOLEAN WantImpersonateType)
{
    NTSTATUS Status;
    HANDLE Token;
    HANDLE DuplicatedToken;
    OBJECT_ATTRIBUTES ObjectAttributes;
    SECURITY_QUALITY_OF_SERVICE Sqos;

    Status = NtOpenProcessToken(NtCurrentProcess(),
                                TOKEN_QUERY | TOKEN_DUPLICATE,
                                &Token);
    if (!NT_SUCCESS(Status))
    {
        trace("Failed to get current process token (Status 0x%08lx)\n", Status);
        return NULL;
    }

    Sqos.Length = sizeof(SECURITY_QUALITY_OF_SERVICE);
    Sqos.ImpersonationLevel = WantImpersonateLevel ? SecurityImpersonation : SecurityAnonymous;
    Sqos.ContextTrackingMode = 0;
    Sqos.EffectiveOnly = FALSE;

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               0,
                               NULL,
                               NULL);
    ObjectAttributes.SecurityQualityOfService = &Sqos;

    Status = NtDuplicateToken(Token,
                              TOKEN_QUERY | TOKEN_DUPLICATE,
                              &ObjectAttributes,
                              FALSE,
                              WantImpersonateType ? TokenImpersonation : TokenPrimary,
                              &DuplicatedToken);
    if (!NT_SUCCESS(Status))
    {
        trace("Failed to duplicate token (Status 0x%08lx)\n", Status);
        NtClose(Token);
        return NULL;
    }

    return DuplicatedToken;
}

static
VOID
ParamValidationNoObjsList(VOID)
{
    NTSTATUS Status;
    NTSTATUS AccessStatus;
    ACCESS_MASK GrantedAccess;
    PPRIVILEGE_SET PrivilegeSet = NULL;
    ULONG PrivilegeSetLength;
    HANDLE Token = NULL;
    SECURITY_DESCRIPTOR Sd;

    PrivilegeSetLength = FIELD_OFFSET(PRIVILEGE_SET, Privilege[16]);
    PrivilegeSet = RtlAllocateHeap(RtlGetProcessHeap(), 0, PrivilegeSetLength);
    if (PrivilegeSet == NULL)
    {
        skip("Failed to allocate PrivilegeSet, skipping tests\n");
        goto Quit;
    }

    Token = GetTokenProcess(TRUE, TRUE);
    if (Token == NULL)
    {
        skip("Failed to get token, skipping tests\n");
        goto Quit;
    }

    Status = RtlCreateSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create a security descriptor, skipping tests\n");
        goto Quit;
    }

    RtlSetGroupSecurityDescriptor(&Sd, NULL, FALSE);
    RtlSetOwnerSecurityDescriptor(&Sd, NULL, FALSE);
    RtlSetDaclSecurityDescriptor(&Sd, FALSE, NULL, FALSE);

    /* The function expects an object type list */
    Status = NtAccessCheckByTypeResultList(&Sd,
                                           NULL,
                                           Token,
                                           MAXIMUM_ALLOWED,
                                           NULL,
                                           0,
                                           &RegMapping,
                                           PrivilegeSet,
                                           &PrivilegeSetLength,
                                           &GrantedAccess,
                                           &AccessStatus);
    ok_hex(Status, STATUS_INVALID_PARAMETER);

Quit:
    if (Token)
    {
        NtClose(Token);
    }

    if (PrivilegeSet)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, PrivilegeSet);
    }
}

static
VOID
PrintAccessStatusAndGrantedAccess(
    _In_ PNTSTATUS AccessStatus,
    _In_ PACCESS_MASK GrantedAccess,
    _In_ ULONG ObjectTypeListLength)
{
    ULONG i;

    trace("===== OBJECT ACCESS & STATUS LIST =====\n");
    for (i = 0; i < ObjectTypeListLength; i++)
    {
        trace("OBJ #%lu, access status 0x%08lx, granted access 0x%08lx\n", i, AccessStatus[i], GrantedAccess[i]);
    }
    trace("\n");
}

static
VOID
GrantedAccessTests(VOID)
{
    NTSTATUS Status;
    NTSTATUS AccessStatus[6];
    ACCESS_MASK GrantedAccess[6];
    PPRIVILEGE_SET PrivilegeSet = NULL;
    ULONG PrivilegeSetLength;
    HANDLE Token = NULL;
    PACL Dacl = NULL;
    ULONG DaclSize;
    ULONG i;
    SECURITY_DESCRIPTOR Sd;
    OBJECT_TYPE_LIST ObjTypeList[6];
    PSID EveryoneSid = NULL, AdminSid = NULL, UsersSid = NULL;
    GUID ChildObjectType2 = {0x34578901, 0x3456, 0x7896, {0x3, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00}};
    GUID ChildObjectType3 = {0x45678901, 0x4567, 0x1122, {0x4, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x01}};
    GUID ChildObjectType4 = {0x56788901, 0x1111, 0x2222, {0x5, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x02}};
    GUID ChildObjectType5 = {0x67901234, 0x2222, 0x3333, {0x4, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x03}};

    /* Allocate all the stuff we need */
    PrivilegeSetLength = FIELD_OFFSET(PRIVILEGE_SET, Privilege[16]);
    PrivilegeSet = RtlAllocateHeap(RtlGetProcessHeap(), 0, PrivilegeSetLength);
    if (PrivilegeSet == NULL)
    {
        skip("Failed to allocate PrivilegeSet, skipping tests\n");
        return;
    }

    Status = RtlAllocateAndInitializeSid(&WorldAuthority,
                                         1,
                                         SECURITY_WORLD_RID,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         &EveryoneSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create Everyone SID, skipping tests\n");
        goto Quit;
    }

    Status = RtlAllocateAndInitializeSid(&NtAuthority,
                                         2,
                                         SECURITY_BUILTIN_DOMAIN_RID,
                                         DOMAIN_ALIAS_RID_ADMINS,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         &AdminSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create Admins SID, skipping tests\n");
        goto Quit;
    }

    Status = RtlAllocateAndInitializeSid(&NtAuthority,
                                         2,
                                         SECURITY_BUILTIN_DOMAIN_RID,
                                         DOMAIN_ALIAS_RID_USERS,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         &UsersSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create User SID, skipping tests\n");
        goto Quit;
    }

    Token = GetTokenProcess(TRUE, TRUE);
    if (Token == NULL)
    {
        skip("Failed to get token, skipping tests\n");
        goto Quit;
    }

    Status = RtlCreateSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create a security descriptor, skipping tests\n");
        goto Quit;
    }

    DaclSize = sizeof(ACL) +
               sizeof(ACCESS_ALLOWED_OBJECT_ACE) + RtlLengthSid(AdminSid) +
               sizeof(ACCESS_ALLOWED_OBJECT_ACE) + RtlLengthSid(EveryoneSid) +
               sizeof(ACCESS_ALLOWED_OBJECT_ACE) + RtlLengthSid(EveryoneSid);
    Dacl = RtlAllocateHeap(RtlGetProcessHeap(),
                           HEAP_ZERO_MEMORY,
                           DaclSize);
    if (Dacl == NULL)
    {
        skip("Failed to allocate memory for DACL, skipping tests\n");
        goto Quit;
    }

    Status = RtlCreateAcl(Dacl,
                          DaclSize,
                          ACL_REVISION4);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create DACL, skipping tests\n");
        goto Quit;
    }

    /*
     * Admins have full access over the key object, everyone else can only read from
     * it and can only query the value from the child sub-object.
     */
    Status = RtlAddAccessAllowedObjectAce(Dacl,
                                          ACL_REVISION4,
                                          0,
                                          KEY_ALL_ACCESS,
                                          &ObjectType,
                                          NULL,
                                          AdminSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to add allowed object ACE for Admins SID, skipping tests\n");
        goto Quit;
    }

    Status = RtlAddAccessAllowedObjectAce(Dacl,
                                          ACL_REVISION4,
                                          0,
                                          KEY_READ,
                                          &ObjectType,
                                          NULL,
                                          EveryoneSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to add allowed object ACE for Everyone SID, skipping tests\n");
        goto Quit;
    }

    Status = RtlAddAccessAllowedObjectAce(Dacl,
                                          ACL_REVISION4,
                                          0,
                                          KEY_QUERY_VALUE,
                                          &ChildObjectType,
                                          NULL,
                                          EveryoneSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to add allowed object ACE for Everyone SID, skipping tests\n");
        goto Quit;
    }

    /* Setup the descriptor */
    RtlSetGroupSecurityDescriptor(&Sd, UsersSid, FALSE);
    RtlSetOwnerSecurityDescriptor(&Sd, AdminSid, FALSE);
    RtlSetDaclSecurityDescriptor(&Sd, TRUE, Dacl, FALSE);

    /* Setup the object type list */
    ObjTypeList[0].Level = ACCESS_OBJECT_GUID;
    ObjTypeList[0].Sbz = 0;
    ObjTypeList[0].ObjectType = &ObjectType;

    ObjTypeList[1].Level = ACCESS_PROPERTY_SET_GUID;
    ObjTypeList[1].Sbz = 0;
    ObjTypeList[1].ObjectType = &ChildObjectType;

    ObjTypeList[2].Level = ACCESS_PROPERTY_GUID;
    ObjTypeList[2].Sbz = 0;
    ObjTypeList[2].ObjectType = &ChildObjectType2;

    ObjTypeList[3].Level = ACCESS_PROPERTY_GUID;
    ObjTypeList[3].Sbz = 0;
    ObjTypeList[3].ObjectType = &ChildObjectType3;

    ObjTypeList[4].Level = ACCESS_PROPERTY_SET_GUID;
    ObjTypeList[4].Sbz = 0;
    ObjTypeList[4].ObjectType = &ChildObjectType4;

    ObjTypeList[5].Level = ACCESS_PROPERTY_GUID;
    ObjTypeList[5].Sbz = 0;
    ObjTypeList[5].ObjectType = &ChildObjectType5;

    /* Admins should be granted every access */
    Status = NtAccessCheckByTypeResultList(&Sd,
                                           NULL,
                                           Token,
                                           MAXIMUM_ALLOWED,
                                           ObjTypeList,
                                           RTL_NUMBER_OF(ObjTypeList),
                                           &RegMapping,
                                           PrivilegeSet,
                                           &PrivilegeSetLength,
                                           GrantedAccess,
                                           AccessStatus);
    ok_hex(Status, STATUS_SUCCESS);

    PrintAccessStatusAndGrantedAccess(AccessStatus, GrantedAccess, RTL_NUMBER_OF(ObjTypeList));
    for (i = 0; i < RTL_NUMBER_OF(ObjTypeList); i++)
    {
        ok(AccessStatus[i] == STATUS_SUCCESS, "Expected STATUS_SUCCESS but got 0x%08lx\n", AccessStatus[i]);
        ok(GrantedAccess[i] == KEY_ALL_ACCESS, "Expected KEY_ALL_ACCESS but got 0x%08lx\n", GrantedAccess[i]);
    }

    /* Everyone else can only read */
    Status = NtAccessCheckByTypeResultList(&Sd,
                                           NULL,
                                           Token,
                                           KEY_READ,
                                           ObjTypeList,
                                           RTL_NUMBER_OF(ObjTypeList),
                                           &RegMapping,
                                           PrivilegeSet,
                                           &PrivilegeSetLength,
                                           GrantedAccess,
                                           AccessStatus);
    ok_hex(Status, STATUS_SUCCESS);

    PrintAccessStatusAndGrantedAccess(AccessStatus, GrantedAccess, RTL_NUMBER_OF(ObjTypeList));
    for (i = 0; i < RTL_NUMBER_OF(ObjTypeList); i++)
    {
        ok(AccessStatus[i] == STATUS_SUCCESS, "Expected STATUS_SUCCESS but got 0x%08lx\n", AccessStatus[i]);
        ok(GrantedAccess[i] == KEY_READ, "Expected KEY_READ but got 0x%08lx\n", GrantedAccess[i]);
    }

    /* Everyone else can only query a registry value from the child object */
    Status = NtAccessCheckByTypeResultList(&Sd,
                                           NULL,
                                           Token,
                                           KEY_QUERY_VALUE,
                                           ObjTypeList,
                                           RTL_NUMBER_OF(ObjTypeList),
                                           &RegMapping,
                                           PrivilegeSet,
                                           &PrivilegeSetLength,
                                           GrantedAccess,
                                           AccessStatus);
    ok_hex(Status, STATUS_SUCCESS);

    PrintAccessStatusAndGrantedAccess(AccessStatus, GrantedAccess, RTL_NUMBER_OF(ObjTypeList));
    for (i = 0; i < RTL_NUMBER_OF(ObjTypeList); i++)
    {
        ok(AccessStatus[i] == STATUS_SUCCESS, "Expected STATUS_SUCCESS but got 0x%08lx\n", AccessStatus[i]);
        ok(GrantedAccess[i] == KEY_QUERY_VALUE, "Expected KEY_QUERY_VALUE but got 0x%08lx\n", GrantedAccess[i]);
    }

Quit:
    if (Dacl)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Dacl);
    }

    if (Token)
    {
        NtClose(Token);
    }

    if (UsersSid)
    {
        RtlFreeSid(UsersSid);
    }

    if (AdminSid)
    {
        RtlFreeSid(AdminSid);
    }

    if (EveryoneSid)
    {
        RtlFreeSid(EveryoneSid);
    }

    if (PrivilegeSet)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, PrivilegeSet);
    }
}

static
VOID
DenyAccessTests(VOID)
{
    NTSTATUS Status;
    NTSTATUS AccessStatus[6];
    ACCESS_MASK GrantedAccess[6];
    PPRIVILEGE_SET PrivilegeSet = NULL;
    ULONG PrivilegeSetLength;
    HANDLE Token = NULL;
    PACL Dacl = NULL;
    ULONG DaclSize;
    ULONG i;
    SECURITY_DESCRIPTOR Sd;
    OBJECT_TYPE_LIST ObjTypeList[6];
    PSID EveryoneSid = NULL, AdminSid = NULL, UsersSid = NULL;
    GUID ChildObjectType2 = {0x34578901, 0x3456, 0x7896, {0x3, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0x00}};
    GUID ChildObjectType3 = {0x45678901, 0x4567, 0x1122, {0x4, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17, 0x01}};
    GUID ChildObjectType4 = {0x56788901, 0x1111, 0x2222, {0x5, 0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x02}};
    GUID ChildObjectType5 = {0x67901234, 0x2222, 0x3333, {0x4, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x03}};

    /* Allocate all the stuff we need */
    PrivilegeSetLength = FIELD_OFFSET(PRIVILEGE_SET, Privilege[16]);
    PrivilegeSet = RtlAllocateHeap(RtlGetProcessHeap(), 0, PrivilegeSetLength);
    if (PrivilegeSet == NULL)
    {
        skip("Failed to allocate PrivilegeSet, skipping tests\n");
        return;
    }

    Status = RtlAllocateAndInitializeSid(&WorldAuthority,
                                         1,
                                         SECURITY_WORLD_RID,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         &EveryoneSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create Everyone SID, skipping tests\n");
        goto Quit;
    }

    Status = RtlAllocateAndInitializeSid(&NtAuthority,
                                         2,
                                         SECURITY_BUILTIN_DOMAIN_RID,
                                         DOMAIN_ALIAS_RID_ADMINS,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         &AdminSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create Admins SID, skipping tests\n");
        goto Quit;
    }

    Status = RtlAllocateAndInitializeSid(&NtAuthority,
                                         2,
                                         SECURITY_BUILTIN_DOMAIN_RID,
                                         DOMAIN_ALIAS_RID_USERS,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         0,
                                         &UsersSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create User SID, skipping tests\n");
        goto Quit;
    }

    Token = GetTokenProcess(TRUE, TRUE);
    if (Token == NULL)
    {
        skip("Failed to get token, skipping tests\n");
        goto Quit;
    }

    Status = RtlCreateSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create a security descriptor, skipping tests\n");
        goto Quit;
    }

    DaclSize = sizeof(ACL) +
               sizeof(ACCESS_DENIED_OBJECT_ACE) + RtlLengthSid(AdminSid) +
               sizeof(ACCESS_DENIED_OBJECT_ACE) + RtlLengthSid(EveryoneSid) +
               sizeof(ACCESS_ALLOWED_OBJECT_ACE) + RtlLengthSid(EveryoneSid);
    Dacl = RtlAllocateHeap(RtlGetProcessHeap(),
                           HEAP_ZERO_MEMORY,
                           DaclSize);
    if (Dacl == NULL)
    {
        skip("Failed to allocate memory for DACL, skipping tests\n");
        goto Quit;
    }

    Status = RtlCreateAcl(Dacl,
                          DaclSize,
                          ACL_REVISION4);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to create DACL, skipping tests\n");
        goto Quit;
    }

    /*
     * Admins can't read the main object, whereas everyone else can't write
     * into the child object.
     */
    Status = RtlAddAccessDeniedObjectAce(Dacl,
                                         ACL_REVISION4,
                                         0,
                                         KEY_READ,
                                         &ObjectType,
                                         NULL,
                                         AdminSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to add deny object ACE for Admins SID, skipping tests\n");
        goto Quit;
    }

    Status = RtlAddAccessDeniedObjectAce(Dacl,
                                         ACL_REVISION4,
                                         0,
                                         KEY_WRITE,
                                         &ChildObjectType,
                                         NULL,
                                         EveryoneSid);
    if (!NT_SUCCESS(Status))
    {
        skip("Failed to add deny object ACE for Everyone SID, skipping tests\n");
        goto Quit;
    }

    /* Setup the descriptor */
    RtlSetGroupSecurityDescriptor(&Sd, UsersSid, FALSE);
    RtlSetOwnerSecurityDescriptor(&Sd, AdminSid, FALSE);
    RtlSetDaclSecurityDescriptor(&Sd, TRUE, Dacl, FALSE);

    /* Setup the object type list */
    ObjTypeList[0].Level = ACCESS_OBJECT_GUID;
    ObjTypeList[0].Sbz = 0;
    ObjTypeList[0].ObjectType = &ObjectType;

    ObjTypeList[1].Level = ACCESS_PROPERTY_SET_GUID;
    ObjTypeList[1].Sbz = 0;
    ObjTypeList[1].ObjectType = &ChildObjectType;

    ObjTypeList[2].Level = ACCESS_PROPERTY_GUID;
    ObjTypeList[2].Sbz = 0;
    ObjTypeList[2].ObjectType = &ChildObjectType2;

    ObjTypeList[3].Level = ACCESS_PROPERTY_GUID;
    ObjTypeList[3].Sbz = 0;
    ObjTypeList[3].ObjectType = &ChildObjectType3;

    ObjTypeList[4].Level = ACCESS_PROPERTY_SET_GUID;
    ObjTypeList[4].Sbz = 0;
    ObjTypeList[4].ObjectType = &ChildObjectType4;

    ObjTypeList[5].Level = ACCESS_PROPERTY_GUID;
    ObjTypeList[5].Sbz = 0;
    ObjTypeList[5].ObjectType = &ChildObjectType5;

    /*
     * Admins shouldn't be able to read from the main object.
     * NtAccessCheckByTypeResultList will return partial rights
     * that have been granted to the caller even if the caller
     * did not get all the rights he wanted.
     */
    Status = NtAccessCheckByTypeResultList(&Sd,
                                           NULL,
                                           Token,
                                           KEY_READ,
                                           ObjTypeList,
                                           RTL_NUMBER_OF(ObjTypeList),
                                           &RegMapping,
                                           PrivilegeSet,
                                           &PrivilegeSetLength,
                                           GrantedAccess,
                                           AccessStatus);
    ok_hex(Status, STATUS_SUCCESS);

    PrintAccessStatusAndGrantedAccess(AccessStatus, GrantedAccess, RTL_NUMBER_OF(ObjTypeList));
    for (i = 0; i < RTL_NUMBER_OF(ObjTypeList); i++)
    {
        ok(AccessStatus[i] == STATUS_ACCESS_DENIED, "Expected STATUS_ACCESS_DENIED but got 0x%08lx\n", AccessStatus[i]);
        ok(GrantedAccess[i] == READ_CONTROL, "Expected READ_CONTROL as given partial right but got 0x%08lx\n", GrantedAccess[i]);
    }

    /* Everyone else can't write into the child object */
    Status = NtAccessCheckByTypeResultList(&Sd,
                                           NULL,
                                           Token,
                                           KEY_WRITE,
                                           ObjTypeList,
                                           RTL_NUMBER_OF(ObjTypeList),
                                           &RegMapping,
                                           PrivilegeSet,
                                           &PrivilegeSetLength,
                                           GrantedAccess,
                                           AccessStatus);
    ok_hex(Status, STATUS_SUCCESS);

    PrintAccessStatusAndGrantedAccess(AccessStatus, GrantedAccess, RTL_NUMBER_OF(ObjTypeList));
    for (i = 0; i < RTL_NUMBER_OF(ObjTypeList); i++)
    {
        ok(AccessStatus[i] == STATUS_ACCESS_DENIED, "Expected STATUS_ACCESS_DENIED but got 0x%08lx\n", AccessStatus[i]);
        ok(GrantedAccess[i] == READ_CONTROL, "Expected READ_CONTROL as given partial right but got 0x%08lx\n", GrantedAccess[i]);
    }

Quit:
    if (Dacl)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, Dacl);
    }

    if (Token)
    {
        NtClose(Token);
    }

    if (UsersSid)
    {
        RtlFreeSid(UsersSid);
    }

    if (AdminSid)
    {
        RtlFreeSid(AdminSid);
    }

    if (EveryoneSid)
    {
        RtlFreeSid(EveryoneSid);
    }

    if (PrivilegeSet)
    {
        RtlFreeHeap(RtlGetProcessHeap(), 0, PrivilegeSet);
    }
}

static
VOID
DaclResultListOutputs(VOID)
{
    GUID Object = {0xbf967a86, 0x0de6, 0x11d0, {0xa2,0x85,0x00,0xaa,0x00,0x30,0x49,0xe2}};
    GUID PropertySet = {0x77b5b886, 0x944a, 0x11d1, {0xae,0xbd,0x00,0x00,0xf8,0x03,0x67,0xc1}};
    OBJECT_TYPE_LIST Objects[] = {{ACCESS_OBJECT_GUID, 0, &Object}, {ACCESS_PROPERTY_SET_GUID, 0, &PropertySet}};
    GENERIC_MAPPING Mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    SID Everyone = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    SECURITY_DESCRIPTOR Sd;
    SECURITY_QUALITY_OF_SERVICE Sqos;
    OBJECT_ATTRIBUTES Attributes;
    ACL Empty;
    PRIVILEGE_SET Privileges;
    ACCESS_MASK Grants[RTL_NUMBER_OF(Objects)];
    NTSTATUS Statuses[RTL_NUMBER_OF(Objects)], Status;
    ULONG Size, Capacity, i, j;
    PTOKEN_PRIVILEGES PrivilegeInformation = NULL;
    HANDLE Source = NULL, Token = NULL;

    Status = NtOpenProcessToken(NtCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &Source);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Done;
    Sqos.Length = sizeof(Sqos);
    Sqos.ImpersonationLevel = SecurityImpersonation;
    Sqos.ContextTrackingMode = SECURITY_STATIC_TRACKING;
    Sqos.EffectiveOnly = FALSE;
    InitializeObjectAttributes(&Attributes, NULL, 0, NULL, NULL);
    Attributes.SecurityQualityOfService = &Sqos;
    Status = NtDuplicateToken(Source, TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &Attributes,
                               FALSE, TokenImpersonation, &Token);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = NtAdjustPrivilegesToken(Token, TRUE, NULL, 0, NULL, NULL);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Done;
    Size = 0;
    Status = NtQueryInformationToken(Token, TokenPrivileges, NULL, 0, &Size);
    ok_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok(Size >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges), "Invalid privilege sizing %lu.\n", Size);
    if (Status != STATUS_BUFFER_TOO_SMALL || Size < FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) goto Done;
    Capacity = Size;
    PrivilegeInformation = RtlAllocateHeap(RtlGetProcessHeap(), 0, Capacity);
    ok(!!PrivilegeInformation, "Privilege buffer allocation failed.\n");
    if (!PrivilegeInformation) goto Done;
    Status = NtQueryInformationToken(Token, TokenPrivileges, PrivilegeInformation, Capacity, &Size);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Done;
    ok(Size >= FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) && Size <= Capacity,
       "Invalid privilege query size %lu, capacity %lu.\n", Size, Capacity);
    if (Size < FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges) || Size > Capacity) goto Done;
    ok(PrivilegeInformation->PrivilegeCount <= (Size - FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES),
       "Privilege count exceeds returned size.\n");
    if (PrivilegeInformation->PrivilegeCount > (Size - FIELD_OFFSET(TOKEN_PRIVILEGES, Privileges)) / sizeof(LUID_AND_ATTRIBUTES)) goto Done;
    for (i = 0; i < PrivilegeInformation->PrivilegeCount; ++i)
    {
        ok(!(PrivilegeInformation->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED), "Private token privilege %lu remains enabled.\n", i);
        if (PrivilegeInformation->Privileges[i].Attributes & SE_PRIVILEGE_ENABLED) goto Done;
    }
    Status = RtlCreateSecurityDescriptor(&Sd, SECURITY_DESCRIPTOR_REVISION);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = RtlSetOwnerSecurityDescriptor(&Sd, &Everyone, FALSE);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = RtlSetGroupSecurityDescriptor(&Sd, &Everyone, FALSE);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = RtlCreateAcl(&Empty, sizeof(Empty), ACL_REVISION);
    ok_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) goto Done;
    for (i = 0; i < 3; ++i)
    {
        winetest_push_context("result-list %s DACL", i == 0 ? "NULL" : i == 1 ? "absent" : "empty");
        Status = RtlSetDaclSecurityDescriptor(&Sd, i != 1, i == 2 ? &Empty : NULL, FALSE);
        ok_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status)) { winetest_pop_context(); goto Done; }
        for (j = 0; j < RTL_NUMBER_OF(Objects); ++j)
        {
            Grants[j] = 0xdeadbeef;
            Statuses[j] = 0xdeadbeef;
        }
        memset(&Privileges, 0xcc, sizeof(Privileges));
        Size = sizeof(Privileges);
        Status = NtAccessCheckByTypeResultList(&Sd, NULL, Token, FILE_READ_DATA, Objects, RTL_NUMBER_OF(Objects),
                                              &Mapping, &Privileges, &Size, Grants, Statuses);
        ok_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            for (j = 0; j < RTL_NUMBER_OF(Objects); ++j)
            {
                ok(Grants[j] == (i == 2 ? 0 : FILE_READ_DATA), "Entry %lu grant is %#lx.\n", j, Grants[j]);
                ok(Statuses[j] == (i == 2 ? STATUS_ACCESS_DENIED : STATUS_SUCCESS),
                   "Entry %lu status is %#lx.\n", j, Statuses[j]);
            }
            ok(!Privileges.PrivilegeCount, "Data access used %lu privileges.\n", Privileges.PrivilegeCount);
        }
        winetest_pop_context();
    }

Done:
    if (PrivilegeInformation) RtlFreeHeap(RtlGetProcessHeap(), 0, PrivilegeInformation);
    if (Token) NtClose(Token);
    if (Source) NtClose(Source);
}

START_TEST(NtAccessCheckByTypeResultList)
{
    ParamValidationNoObjsList();
    DaclResultListOutputs();
    GrantedAccessTests();
    DenyAccessTests();
}
