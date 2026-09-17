/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            lib/advapi32/sec/sec.c
 * PURPOSE:         Security descriptor functions
 * PROGRAMMER:      Ariadne ( ariadne@xs4all.nl)
 *                  Steven Edwards ( Steven_Ed4153@yahoo.com )
 *                  Andrew Greenwood ( silverblade_uk@hotmail.com )
 * UPDATE HISTORY:
 *                  Created 01/11/98
 */

#include <advapi32.h>
WINE_DEFAULT_DEBUG_CHANNEL(advapi);

/*
 * @implemented
 */
BOOL
WINAPI
GetSecurityDescriptorControl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                             PSECURITY_DESCRIPTOR_CONTROL pControl,
                             LPDWORD lpdwRevision)
{
    NTSTATUS Status;

    Status = RtlGetControlSecurityDescriptor(pSecurityDescriptor,
                                             pControl,
                                             (PULONG)lpdwRevision);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                          LPBOOL lpbDaclPresent,
                          PACL *pDacl,
                          LPBOOL lpbDaclDefaulted)
{
    BOOLEAN DaclPresent;
    BOOLEAN DaclDefaulted;
    NTSTATUS Status;

    Status = RtlGetDaclSecurityDescriptor(pSecurityDescriptor,
                                          &DaclPresent,
                                          pDacl,
                                          &DaclDefaulted);
    *lpbDaclPresent = (BOOL)DaclPresent;
    *lpbDaclDefaulted = (BOOL)DaclDefaulted;

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                           PSID *pGroup,
                           LPBOOL lpbGroupDefaulted)
{
    BOOLEAN GroupDefaulted;
    NTSTATUS Status;

    Status = RtlGetGroupSecurityDescriptor(pSecurityDescriptor,
                                           pGroup,
                                           &GroupDefaulted);
    *lpbGroupDefaulted = (BOOL)GroupDefaulted;

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                           PSID *pOwner,
                           LPBOOL lpbOwnerDefaulted)
{
    BOOLEAN OwnerDefaulted;
    NTSTATUS Status;

    Status = RtlGetOwnerSecurityDescriptor(pSecurityDescriptor,
                                           pOwner,
                                           &OwnerDefaulted);
    *lpbOwnerDefaulted = (BOOL)OwnerDefaulted;

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
DWORD
WINAPI
GetSecurityDescriptorRMControl(PSECURITY_DESCRIPTOR SecurityDescriptor,
                               PUCHAR RMControl)
{
    if (!RtlGetSecurityDescriptorRMControl(SecurityDescriptor,
                                           RMControl))
        return ERROR_INVALID_DATA;

    return ERROR_SUCCESS;
}


/*
 * @implemented
 */
BOOL
WINAPI
GetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                          LPBOOL lpbSaclPresent,
                          PACL *pSacl,
                          LPBOOL lpbSaclDefaulted)
{
    BOOLEAN SaclPresent;
    BOOLEAN SaclDefaulted;
    NTSTATUS Status;

    Status = RtlGetSaclSecurityDescriptor(pSecurityDescriptor,
                                          &SaclPresent,
                                          pSacl,
                                          &SaclDefaulted);
    *lpbSaclPresent = (BOOL)SaclPresent;
    *lpbSaclDefaulted = (BOOL)SaclDefaulted;

    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}

/*
 * @implemented
 */
BOOL
WINAPI
IsValidSecurityDescriptor(PSECURITY_DESCRIPTOR pSecurityDescriptor)
{
    BOOLEAN Result;

    Result = RtlValidSecurityDescriptor (pSecurityDescriptor);
    if (Result == FALSE)
        SetLastError(RtlNtStatusToDosError(STATUS_INVALID_SECURITY_DESCR));

    return (BOOL)Result;
}

/*
 * @implemented
 */
BOOL
WINAPI
MakeAbsoluteSD2(IN OUT PSECURITY_DESCRIPTOR pSelfRelativeSecurityDescriptor,
                OUT LPDWORD lpdwBufferSize)
{
    NTSTATUS Status;

    Status = RtlSelfRelativeToAbsoluteSD2(pSelfRelativeSecurityDescriptor,
                                          lpdwBufferSize);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
MakeSelfRelativeSD(PSECURITY_DESCRIPTOR pAbsoluteSecurityDescriptor,
                   PSECURITY_DESCRIPTOR pSelfRelativeSecurityDescriptor,
                   LPDWORD lpdwBufferLength)
{
    NTSTATUS Status;

    Status = RtlAbsoluteToSelfRelativeSD(pAbsoluteSecurityDescriptor,
                                         pSelfRelativeSecurityDescriptor,
                                         (PULONG)lpdwBufferLength);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetSecurityDescriptorControl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                             SECURITY_DESCRIPTOR_CONTROL ControlBitsOfInterest,
                             SECURITY_DESCRIPTOR_CONTROL ControlBitsToSet)
{
    NTSTATUS Status;

    Status = RtlSetControlSecurityDescriptor(pSecurityDescriptor,
                                             ControlBitsOfInterest,
                                             ControlBitsToSet);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetSecurityDescriptorDacl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                          BOOL bDaclPresent,
                          PACL pDacl,
                          BOOL bDaclDefaulted)
{
    NTSTATUS Status;

    Status = RtlSetDaclSecurityDescriptor(pSecurityDescriptor,
                                          bDaclPresent,
                                          pDacl,
                                          bDaclDefaulted);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetSecurityDescriptorGroup(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                           PSID pGroup,
                           BOOL bGroupDefaulted)
{
    NTSTATUS Status;

    Status = RtlSetGroupSecurityDescriptor(pSecurityDescriptor,
                                           pGroup,
                                           bGroupDefaulted);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetSecurityDescriptorOwner(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                           PSID pOwner,
                           BOOL bOwnerDefaulted)
{
    NTSTATUS Status;

    Status = RtlSetOwnerSecurityDescriptor(pSecurityDescriptor,
                                           pOwner,
                                           bOwnerDefaulted);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
DWORD
WINAPI
SetSecurityDescriptorRMControl(PSECURITY_DESCRIPTOR SecurityDescriptor,
                               PUCHAR RMControl)
{
    RtlSetSecurityDescriptorRMControl(SecurityDescriptor,
                                      RMControl);

    return ERROR_SUCCESS;
}


/*
 * @implemented
 */
BOOL
WINAPI
SetSecurityDescriptorSacl(PSECURITY_DESCRIPTOR pSecurityDescriptor,
                          BOOL bSaclPresent,
                          PACL pSacl,
                          BOOL bSaclDefaulted)
{
    NTSTATUS Status;

    Status = RtlSetSaclSecurityDescriptor(pSecurityDescriptor,
                                          bSaclPresent,
                                          pSacl,
                                          bSaclDefaulted);
    if (!NT_SUCCESS(Status))
    {
        SetLastError(RtlNtStatusToDosError(Status));
        return FALSE;
    }

    return TRUE;
}


/*
 * @implemented
 */
VOID
WINAPI
QuerySecurityAccessMask(IN SECURITY_INFORMATION SecurityInformation,
                        OUT LPDWORD DesiredAccess)
{
    *DesiredAccess = 0;

    if (SecurityInformation & (OWNER_SECURITY_INFORMATION |
                               GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION |
                               LABEL_SECURITY_INFORMATION))
    {
        *DesiredAccess |= READ_CONTROL;
    }

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
        *DesiredAccess |= ACCESS_SYSTEM_SECURITY;
}


/*
 * @implemented
 */
VOID
WINAPI
SetSecurityAccessMask(IN SECURITY_INFORMATION SecurityInformation,
                      OUT LPDWORD DesiredAccess)
{
    *DesiredAccess = 0;

    if (SecurityInformation & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION))
        *DesiredAccess |= WRITE_OWNER;

    if (SecurityInformation & DACL_SECURITY_INFORMATION)
        *DesiredAccess |= WRITE_DAC;

    if (SecurityInformation & SACL_SECURITY_INFORMATION)
        *DesiredAccess |= ACCESS_SYSTEM_SECURITY;

    if (SecurityInformation & LABEL_SECURITY_INFORMATION)
        *DesiredAccess |= WRITE_OWNER;
}


/*
 * @unimplemented
 */
BOOL
WINAPI
ConvertToAutoInheritPrivateObjectSecurity(IN PSECURITY_DESCRIPTOR ParentDescriptor,
                                          IN PSECURITY_DESCRIPTOR CurrentSecurityDescriptor,
                                          OUT PSECURITY_DESCRIPTOR* NewSecurityDescriptor,
                                          IN GUID* ObjectType,
                                          IN BOOLEAN IsDirectoryObject,
                                          IN PGENERIC_MAPPING GenericMapping)
{
    UNIMPLEMENTED;
    return FALSE;
}


static VOID
BuildpFreeAbsoluteSecurityDescriptor(IN PISECURITY_DESCRIPTOR SecurityDescriptor)
{
    LocalFree(SecurityDescriptor->Owner);
    LocalFree(SecurityDescriptor->Group);
    LocalFree(SecurityDescriptor->Sacl);
    LocalFree(SecurityDescriptor->Dacl);
}


static DWORD
BuildpCopySid(IN PSID SourceSid,
              OUT PSID *DestinationSid)
{
    PSID Sid;
    ULONG SidLength;
    NTSTATUS Status;

    if (SourceSid == NULL || !RtlValidSid(SourceSid))
        return ERROR_INVALID_PARAMETER;

    SidLength = RtlLengthSid(SourceSid);
    Sid = LocalAlloc(LMEM_FIXED, SidLength);
    if (Sid == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    Status = RtlCopySid(SidLength, Sid, SourceSid);
    if (!NT_SUCCESS(Status))
    {
        LocalFree(Sid);
        return RtlNtStatusToDosError(Status);
    }

    *DestinationSid = Sid;
    return ERROR_SUCCESS;
}


static DWORD
BuildpGetCurrentUserSid(OUT PSID *Sid)
{
    PTOKEN_USER UserInformation;
    HANDLE Token;
    DWORD Error;
    DWORD Size;

    if (!OpenThreadToken(GetCurrentThread(), TOKEN_QUERY, TRUE, &Token))
    {
        Error = GetLastError();
        if (Error != ERROR_NO_TOKEN)
            return Error;

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &Token))
            return GetLastError();
    }

    Size = 0;
    if (GetTokenInformation(Token, TokenUser, NULL, 0, &Size))
    {
        CloseHandle(Token);
        return ERROR_INVALID_DATA;
    }

    Error = GetLastError();
    if (Error != ERROR_INSUFFICIENT_BUFFER)
    {
        CloseHandle(Token);
        return Error;
    }

    UserInformation = LocalAlloc(LMEM_FIXED, Size);
    if (UserInformation == NULL)
    {
        CloseHandle(Token);
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    if (!GetTokenInformation(Token,
                             TokenUser,
                             UserInformation,
                             Size,
                             &Size))
    {
        Error = GetLastError();
        LocalFree(UserInformation);
        CloseHandle(Token);
        return Error;
    }

    Error = BuildpCopySid(UserInformation->User.Sid, Sid);
    LocalFree(UserInformation);
    CloseHandle(Token);
    return Error;
}


static DWORD
BuildpLookupAccountSid(IN LPCWSTR AccountName,
                       OUT PSID *Sid)
{
    LPWSTR DomainName = NULL;
    PSID AccountSid = NULL;
    SID_NAME_USE Use;
    DWORD DomainSize = 0;
    DWORD SidSize = 0;
    DWORD Error;

    if (AccountName == NULL)
        return ERROR_INVALID_PARAMETER;

    if (wcscmp(AccountName, L"CURRENT_USER") == 0)
        return BuildpGetCurrentUserSid(Sid);

    if (LookupAccountNameW(NULL,
                           AccountName,
                           NULL,
                           &SidSize,
                           NULL,
                           &DomainSize,
                           &Use))
    {
        return ERROR_INVALID_DATA;
    }
    Error = GetLastError();
    if (Error != ERROR_INSUFFICIENT_BUFFER || SidSize == 0)
        return Error;

    AccountSid = LocalAlloc(LMEM_FIXED, SidSize);
    if (AccountSid == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    if (DomainSize != 0)
    {
        DomainName = LocalAlloc(LMEM_FIXED, DomainSize * sizeof(WCHAR));
        if (DomainName == NULL)
        {
            LocalFree(AccountSid);
            return ERROR_NOT_ENOUGH_MEMORY;
        }
    }

    if (!LookupAccountNameW(NULL,
                            AccountName,
                            AccountSid,
                            &SidSize,
                            DomainName,
                            &DomainSize,
                            &Use))
    {
        Error = GetLastError();
        LocalFree(DomainName);
        LocalFree(AccountSid);
        return Error;
    }

    LocalFree(DomainName);
    *Sid = AccountSid;
    return ERROR_SUCCESS;
}


static DWORD
BuildpTrusteeToSid(IN PTRUSTEE_W Trustee,
                   OUT PSID *Sid)
{
    POBJECTS_AND_NAME_W ObjectsAndName;
    POBJECTS_AND_SID ObjectsAndSid;

    if (Trustee == NULL ||
        Trustee->MultipleTrusteeOperation != NO_MULTIPLE_TRUSTEE)
    {
        return ERROR_INVALID_PARAMETER;
    }

    switch (Trustee->TrusteeForm)
    {
        case TRUSTEE_IS_SID:
            return BuildpCopySid((PSID)Trustee->ptstrName, Sid);

        case TRUSTEE_IS_NAME:
            return BuildpLookupAccountSid(Trustee->ptstrName, Sid);

        case TRUSTEE_IS_OBJECTS_AND_SID:
            ObjectsAndSid = (POBJECTS_AND_SID)Trustee->ptstrName;
            if (ObjectsAndSid == NULL)
                return ERROR_INVALID_PARAMETER;
            return BuildpCopySid(ObjectsAndSid->pSid, Sid);

        case TRUSTEE_IS_OBJECTS_AND_NAME:
            ObjectsAndName = (POBJECTS_AND_NAME_W)Trustee->ptstrName;
            if (ObjectsAndName == NULL || ObjectsAndName->ObjectsPresent != 0)
                return ERROR_INVALID_PARAMETER;
            return BuildpLookupAccountSid(ObjectsAndName->ptstrName, Sid);

        default:
            return ERROR_INVALID_PARAMETER;
    }
}


static DWORD
BuildpMakeAbsoluteSecurityDescriptor(IN PSECURITY_DESCRIPTOR RelativeSd,
                                     OUT PISECURITY_DESCRIPTOR AbsoluteSd)
{
    ULONG AbsoluteSize = sizeof(SECURITY_DESCRIPTOR);
    ULONG DaclSize = 0, SaclSize = 0;
    ULONG OwnerSize = 0, GroupSize = 0;
    PACL Dacl = NULL, Sacl = NULL;
    PSID Owner = NULL, Group = NULL;
    NTSTATUS Status;

    Status = RtlSelfRelativeToAbsoluteSD(RelativeSd,
                                         AbsoluteSd,
                                         &AbsoluteSize,
                                         Dacl,
                                         &DaclSize,
                                         Sacl,
                                         &SaclSize,
                                         Owner,
                                         &OwnerSize,
                                         Group,
                                         &GroupSize);
    if (Status == STATUS_SUCCESS)
        return ERROR_SUCCESS;

    if (Status != STATUS_BUFFER_TOO_SMALL)
        return RtlNtStatusToDosError(Status);

    if (DaclSize != 0 && (Dacl = LocalAlloc(LMEM_FIXED, DaclSize)) == NULL)
        goto NoMemory;
    if (SaclSize != 0 && (Sacl = LocalAlloc(LMEM_FIXED, SaclSize)) == NULL)
        goto NoMemory;
    if (OwnerSize != 0 && (Owner = LocalAlloc(LMEM_FIXED, OwnerSize)) == NULL)
        goto NoMemory;
    if (GroupSize != 0 && (Group = LocalAlloc(LMEM_FIXED, GroupSize)) == NULL)
        goto NoMemory;

    AbsoluteSize = sizeof(SECURITY_DESCRIPTOR);
    Status = RtlSelfRelativeToAbsoluteSD(RelativeSd,
                                         AbsoluteSd,
                                         &AbsoluteSize,
                                         Dacl,
                                         &DaclSize,
                                         Sacl,
                                         &SaclSize,
                                         Owner,
                                         &OwnerSize,
                                         Group,
                                         &GroupSize);
    if (!NT_SUCCESS(Status))
    {
        LocalFree(Dacl);
        LocalFree(Sacl);
        LocalFree(Owner);
        LocalFree(Group);
        return RtlNtStatusToDosError(Status);
    }

    return ERROR_SUCCESS;

NoMemory:
    LocalFree(Dacl);
    LocalFree(Sacl);
    LocalFree(Owner);
    LocalFree(Group);
    return ERROR_NOT_ENOUGH_MEMORY;
}


/*
 * @implemented
 */
DWORD
WINAPI
BuildSecurityDescriptorW(IN PTRUSTEE_W pOwner  OPTIONAL,
                         IN PTRUSTEE_W pGroup  OPTIONAL,
                         IN ULONG cCountOfAccessEntries,
                         IN PEXPLICIT_ACCESS_W pListOfAccessEntries  OPTIONAL,
                         IN ULONG cCountOfAuditEntries,
                         IN PEXPLICIT_ACCESS_W pListOfAuditEntries  OPTIONAL,
                         IN PSECURITY_DESCRIPTOR pOldSD  OPTIONAL,
                         OUT PULONG pSizeNewSD,
                         OUT PSECURITY_DESCRIPTOR* pNewSD)
{
    SECURITY_DESCRIPTOR Descriptor;
    SECURITY_DESCRIPTOR_CONTROL Control;
    PSECURITY_DESCRIPTOR NewDescriptor = NULL;
    PSID NewSid;
    PACL NewAcl;
    ULONG Revision;
    ULONG NewSize;
    DWORD Error;
    NTSTATUS Status;

    TRACE("(%p,%p,%lu,%p,%lu,%p,%p,%p,%p)\n",
          pOwner, pGroup, cCountOfAccessEntries, pListOfAccessEntries,
          cCountOfAuditEntries, pListOfAuditEntries, pOldSD,
          pSizeNewSD, pNewSD);

    if (pSizeNewSD == NULL || pNewSD == NULL)
        return ERROR_INVALID_PARAMETER;

    if (pOldSD != NULL)
    {
        Status = RtlGetControlSecurityDescriptor(pOldSD, &Control, &Revision);
        if (!NT_SUCCESS(Status))
            return RtlNtStatusToDosError(Status);

        if (!(Control & SE_SELF_RELATIVE))
            return ERROR_INVALID_SECURITY_DESCR;

        Error = BuildpMakeAbsoluteSecurityDescriptor(pOldSD, &Descriptor);
        if (Error != ERROR_SUCCESS)
            return Error;
    }
    else
    {
        Status = RtlCreateSecurityDescriptor(&Descriptor,
                                             SECURITY_DESCRIPTOR_REVISION);
        if (!NT_SUCCESS(Status))
            return RtlNtStatusToDosError(Status);
    }

    if (pOwner != NULL)
    {
        PSID OldSid;

        Error = BuildpTrusteeToSid(pOwner, &NewSid);
        if (Error != ERROR_SUCCESS)
            goto Cleanup;

        OldSid = Descriptor.Owner;
        Status = RtlSetOwnerSecurityDescriptor(&Descriptor, NewSid, FALSE);
        if (!NT_SUCCESS(Status))
        {
            LocalFree(NewSid);
            Error = RtlNtStatusToDosError(Status);
            goto Cleanup;
        }
        LocalFree(OldSid);
    }

    if (pGroup != NULL)
    {
        PSID OldSid;

        Error = BuildpTrusteeToSid(pGroup, &NewSid);
        if (Error != ERROR_SUCCESS)
            goto Cleanup;

        OldSid = Descriptor.Group;
        Status = RtlSetGroupSecurityDescriptor(&Descriptor, NewSid, FALSE);
        if (!NT_SUCCESS(Status))
        {
            LocalFree(NewSid);
            Error = RtlNtStatusToDosError(Status);
            goto Cleanup;
        }
        LocalFree(OldSid);
    }

    if (pListOfAccessEntries != NULL)
    {
        PACL OldAcl;

        Error = SetEntriesInAclW(cCountOfAccessEntries,
                                 pListOfAccessEntries,
                                 Descriptor.Dacl,
                                 &NewAcl);
        if (Error != ERROR_SUCCESS)
            goto Cleanup;

        OldAcl = Descriptor.Dacl;
        Status = RtlSetDaclSecurityDescriptor(&Descriptor, TRUE, NewAcl, FALSE);
        if (!NT_SUCCESS(Status))
        {
            LocalFree(NewAcl);
            Error = RtlNtStatusToDosError(Status);
            goto Cleanup;
        }
        LocalFree(OldAcl);
    }

    if (pListOfAuditEntries != NULL)
    {
        PACL OldAcl;

        Error = SetEntriesInAclW(cCountOfAuditEntries,
                                 pListOfAuditEntries,
                                 Descriptor.Sacl,
                                 &NewAcl);
        if (Error != ERROR_SUCCESS)
            goto Cleanup;

        OldAcl = Descriptor.Sacl;
        Status = RtlSetSaclSecurityDescriptor(&Descriptor, TRUE, NewAcl, FALSE);
        if (!NT_SUCCESS(Status))
        {
            LocalFree(NewAcl);
            Error = RtlNtStatusToDosError(Status);
            goto Cleanup;
        }
        LocalFree(OldAcl);
    }

    NewSize = 0;
    Status = RtlMakeSelfRelativeSD(&Descriptor, NULL, &NewSize);
    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        Error = RtlNtStatusToDosError(Status);
        goto Cleanup;
    }

    NewDescriptor = LocalAlloc(LMEM_FIXED, NewSize);
    if (NewDescriptor == NULL)
    {
        Error = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }

    Status = RtlMakeSelfRelativeSD(&Descriptor, NewDescriptor, &NewSize);
    if (!NT_SUCCESS(Status))
    {
        Error = RtlNtStatusToDosError(Status);
        LocalFree(NewDescriptor);
        goto Cleanup;
    }

    *pSizeNewSD = NewSize;
    *pNewSD = NewDescriptor;
    Error = ERROR_SUCCESS;

Cleanup:
    BuildpFreeAbsoluteSecurityDescriptor(&Descriptor);
    return Error;
}


/*
 * @implemented
 */
DWORD
WINAPI
BuildSecurityDescriptorA(IN PTRUSTEE_A pOwner  OPTIONAL,
                         IN PTRUSTEE_A pGroup  OPTIONAL,
                         IN ULONG cCountOfAccessEntries,
                         IN PEXPLICIT_ACCESS_A pListOfAccessEntries  OPTIONAL,
                         IN ULONG cCountOfAuditEntries,
                         IN PEXPLICIT_ACCESS_A pListOfAuditEntries  OPTIONAL,
                         IN PSECURITY_DESCRIPTOR pOldSD  OPTIONAL,
                         OUT PULONG pSizeNewSD,
                         OUT PSECURITY_DESCRIPTOR* pNewSD)
{
    PTRUSTEE_W OwnerW = NULL, GroupW = NULL;
    PEXPLICIT_ACCESS_W AccessEntriesW = NULL, AuditEntriesW = NULL;
    DWORD Error;

    TRACE("(%p,%p,%lu,%p,%lu,%p,%p,%p,%p)\n",
          pOwner, pGroup, cCountOfAccessEntries, pListOfAccessEntries,
          cCountOfAuditEntries, pListOfAuditEntries, pOldSD,
          pSizeNewSD, pNewSD);

    if (pOwner != NULL)
    {
        Error = InternalTrusteeAToW(pOwner, &OwnerW);
        if (Error != ERROR_SUCCESS)
            goto Cleanup;
    }

    if (pGroup != NULL)
    {
        Error = InternalTrusteeAToW(pGroup, &GroupW);
        if (Error != ERROR_SUCCESS)
            goto Cleanup;
    }

    if (pListOfAccessEntries != NULL)
    {
        if (cCountOfAccessEntries != 0)
        {
            Error = InternalExplicitAccessAToW(cCountOfAccessEntries,
                                               pListOfAccessEntries,
                                               &AccessEntriesW);
            if (Error != ERROR_SUCCESS)
                goto Cleanup;
        }
        else
        {
            AccessEntriesW = (PEXPLICIT_ACCESS_W)pListOfAccessEntries;
        }
    }

    if (pListOfAuditEntries != NULL)
    {
        if (cCountOfAuditEntries != 0)
        {
            Error = InternalExplicitAccessAToW(cCountOfAuditEntries,
                                               pListOfAuditEntries,
                                               &AuditEntriesW);
            if (Error != ERROR_SUCCESS)
                goto Cleanup;
        }
        else
        {
            AuditEntriesW = (PEXPLICIT_ACCESS_W)pListOfAuditEntries;
        }
    }

    Error = BuildSecurityDescriptorW(OwnerW,
                                     GroupW,
                                     cCountOfAccessEntries,
                                     AccessEntriesW,
                                     cCountOfAuditEntries,
                                     AuditEntriesW,
                                     pOldSD,
                                     pSizeNewSD,
                                     pNewSD);

Cleanup:
    if (pListOfAuditEntries != NULL && cCountOfAuditEntries != 0)
        RtlFreeHeap(RtlGetProcessHeap(), 0, AuditEntriesW);
    if (pListOfAccessEntries != NULL && cCountOfAccessEntries != 0)
        RtlFreeHeap(RtlGetProcessHeap(), 0, AccessEntriesW);
    if (pGroup != NULL && GroupW != NULL)
        InternalFreeConvertedTrustee(GroupW, pGroup);
    if (pOwner != NULL && OwnerW != NULL)
        InternalFreeConvertedTrustee(OwnerW, pOwner);
    return Error;
}

/* EOF */
