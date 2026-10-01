/*
 * ReactOS MARTA provider
 * Copyright (C) 2005 - 2006 ReactOS Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 */
/*
 * PROJECT:         ReactOS MARTA provider
 * FILE:            lib/ntmarta/ntmarta.c
 * PURPOSE:         ReactOS MARTA provider
 * PROGRAMMER:      Thomas Weidenmueller <w3seek@reactos.com>
 *
 * UPDATE HISTORY:
 *      07/26/2005  Created
 */

#include "ntmarta.h"

#define NDEBUG
#include <debug.h>

NTSYSAPI NTSTATUS NTAPI RtlNewSecurityObjectEx(PSECURITY_DESCRIPTOR, PSECURITY_DESCRIPTOR,
    PSECURITY_DESCRIPTOR *, GUID *, BOOLEAN, ULONG, HANDLE, PGENERIC_MAPPING);

HINSTANCE hDllInstance;

/* FIXME: Vista+ API */
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

/* FIXME: Vista+ API */
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

static ACCESS_MODE
AccpGetAceAccessMode(IN PACE_HEADER AceHeader)
{
    ACCESS_MODE Mode = NOT_USED_ACCESS;

    switch (AceHeader->AceType)
    {
        case ACCESS_ALLOWED_ACE_TYPE:
        case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
        case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
        case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
            Mode = GRANT_ACCESS;
            break;

        case ACCESS_DENIED_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_OBJECT_ACE_TYPE:
            Mode = DENY_ACCESS;
            break;

        case SYSTEM_AUDIT_ACE_TYPE:
        case SYSTEM_AUDIT_CALLBACK_ACE_TYPE:
        case SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE:
        case SYSTEM_AUDIT_OBJECT_ACE_TYPE:
            if (AceHeader->AceFlags & FAILED_ACCESS_ACE_FLAG)
                Mode = SET_AUDIT_FAILURE;
            else if (AceHeader->AceFlags & SUCCESSFUL_ACCESS_ACE_FLAG)
                Mode = SET_AUDIT_SUCCESS;
            break;
    }

    return Mode;
}

static UINT
AccpGetAceStructureSize(IN PACE_HEADER AceHeader)
{
    UINT Size = 0;

    switch (AceHeader->AceType)
    {
        case ACCESS_ALLOWED_ACE_TYPE:
        case ACCESS_DENIED_ACE_TYPE:
            Size = FIELD_OFFSET(ACCESS_ALLOWED_ACE,
                                SidStart);
            break;
        case ACCESS_ALLOWED_CALLBACK_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_ACE_TYPE:
            Size = FIELD_OFFSET(ACCESS_ALLOWED_CALLBACK_ACE,
                                SidStart);
            break;
        case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
        {
            PACCESS_ALLOWED_CALLBACK_OBJECT_ACE Ace = (PACCESS_ALLOWED_CALLBACK_OBJECT_ACE)AceHeader;
            Size = FIELD_OFFSET(ACCESS_ALLOWED_CALLBACK_OBJECT_ACE,
                                ObjectType);
            if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                Size += sizeof(Ace->ObjectType);
            if (Ace->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
                Size += sizeof(Ace->InheritedObjectType);
            break;
        }
        case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_OBJECT_ACE_TYPE:
        {
            PACCESS_ALLOWED_OBJECT_ACE Ace = (PACCESS_ALLOWED_OBJECT_ACE)AceHeader;
            Size = FIELD_OFFSET(ACCESS_ALLOWED_OBJECT_ACE,
                                ObjectType);
            if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                Size += sizeof(Ace->ObjectType);
            if (Ace->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
                Size += sizeof(Ace->InheritedObjectType);
            break;
        }

        case SYSTEM_AUDIT_ACE_TYPE:
            Size = FIELD_OFFSET(SYSTEM_AUDIT_ACE,
                                SidStart);
            break;
        case SYSTEM_AUDIT_CALLBACK_ACE_TYPE:
            Size = FIELD_OFFSET(SYSTEM_AUDIT_CALLBACK_ACE,
                                SidStart);
            break;
        case SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE:
        {
            PSYSTEM_AUDIT_CALLBACK_OBJECT_ACE Ace = (PSYSTEM_AUDIT_CALLBACK_OBJECT_ACE)AceHeader;
            Size = FIELD_OFFSET(SYSTEM_AUDIT_CALLBACK_OBJECT_ACE,
                                ObjectType);
            if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                Size += sizeof(Ace->ObjectType);
            if (Ace->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
                Size += sizeof(Ace->InheritedObjectType);
            break;
        }
        case SYSTEM_AUDIT_OBJECT_ACE_TYPE:
        {
            PSYSTEM_AUDIT_OBJECT_ACE Ace = (PSYSTEM_AUDIT_OBJECT_ACE)AceHeader;
            Size = FIELD_OFFSET(SYSTEM_AUDIT_OBJECT_ACE,
                                ObjectType);
            if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                Size += sizeof(Ace->ObjectType);
            if (Ace->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
                Size += sizeof(Ace->InheritedObjectType);
            break;
        }

        case SYSTEM_MANDATORY_LABEL_ACE_TYPE:
            Size = FIELD_OFFSET(SYSTEM_MANDATORY_LABEL_ACE,
                                SidStart);
            break;
    }

    return Size;
}

static PSID
AccpGetAceSid(IN PACE_HEADER AceHeader)
{
    return (PSID)((ULONG_PTR)AceHeader + AccpGetAceStructureSize(AceHeader));
}

static ACCESS_MASK
AccpGetAceAccessMask(IN PACE_HEADER AceHeader)
{
    return *((PACCESS_MASK)(AceHeader + 1));
}

static BOOL
AccpIsObjectAce(IN PACE_HEADER AceHeader)
{
    BOOL Ret;

    switch (AceHeader->AceType)
    {
        case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
        case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_OBJECT_ACE_TYPE:
        case SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE:
        case SYSTEM_AUDIT_OBJECT_ACE_TYPE:
            Ret = TRUE;
            break;

        default:
            Ret = FALSE;
            break;
    }

    return Ret;
}

static DWORD
AccpGetTrusteeObjects(IN PTRUSTEE_W Trustee,
                      OUT GUID *pObjectTypeGuid  OPTIONAL,
                      OUT GUID *pInheritedObjectTypeGuid  OPTIONAL)
{
    DWORD Ret;

    switch (Trustee->TrusteeForm)
    {
        case TRUSTEE_IS_OBJECTS_AND_NAME:
        {
            POBJECTS_AND_NAME_W pOan = (POBJECTS_AND_NAME_W)Trustee->ptstrName;

            /* pOan->ObjectsPresent should always be 0 here because a previous
               call to AccpGetTrusteeSid should have rejected these trustees
               already. */
            ASSERT(pOan->ObjectsPresent == 0);

            Ret = pOan->ObjectsPresent;
            break;
        }

        case TRUSTEE_IS_OBJECTS_AND_SID:
        {
            POBJECTS_AND_SID pOas = (POBJECTS_AND_SID)Trustee->ptstrName;

            if (pObjectTypeGuid != NULL && pOas->ObjectsPresent & ACE_OBJECT_TYPE_PRESENT)
                *pObjectTypeGuid = pOas->ObjectTypeGuid;

            if (pInheritedObjectTypeGuid != NULL && pOas->ObjectsPresent & ACE_INHERITED_OBJECT_TYPE_PRESENT)
                *pInheritedObjectTypeGuid = pOas->InheritedObjectTypeGuid;

            Ret = pOas->ObjectsPresent;
            break;
        }

        default:
            /* Any other trustee forms have no objects attached... */
            Ret = 0;
            break;
    }

    return Ret;
}

static DWORD
AccpCalcNeededAceSize(IN PSID Sid,
                      IN DWORD ObjectsPresent)
{
    DWORD Ret;

    Ret = sizeof(ACE) + GetLengthSid(Sid);

    /* This routine calculates the generic size of the ACE needed.
       If no objects are present it is assumed that only a standard
       ACE is to be created. */

    if (ObjectsPresent & ACE_OBJECT_TYPE_PRESENT)
        Ret += sizeof(GUID);
    if (ObjectsPresent & ACE_INHERITED_OBJECT_TYPE_PRESENT)
        Ret += sizeof(GUID);

    if (ObjectsPresent != 0)
        Ret += sizeof(DWORD); /* Include the Flags member to make it an object ACE */

    return Ret;
}

static GUID*
AccpGetObjectAceObjectType(IN PACE_HEADER AceHeader)
{
    GUID *ObjectType = NULL;

    switch (AceHeader->AceType)
    {
        case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
        {
            PACCESS_ALLOWED_CALLBACK_OBJECT_ACE Ace = (PACCESS_ALLOWED_CALLBACK_OBJECT_ACE)AceHeader;
            if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                ObjectType = &Ace->ObjectType;
            break;
        }
        case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_OBJECT_ACE_TYPE:
        {
            PACCESS_ALLOWED_OBJECT_ACE Ace = (PACCESS_ALLOWED_OBJECT_ACE)AceHeader;
            if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                ObjectType = &Ace->ObjectType;
            break;
        }

        case SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE:
        {
            PSYSTEM_AUDIT_CALLBACK_OBJECT_ACE Ace = (PSYSTEM_AUDIT_CALLBACK_OBJECT_ACE)AceHeader;
            if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                ObjectType = &Ace->ObjectType;
            break;
        }
        case SYSTEM_AUDIT_OBJECT_ACE_TYPE:
        {
            PSYSTEM_AUDIT_OBJECT_ACE Ace = (PSYSTEM_AUDIT_OBJECT_ACE)AceHeader;
            if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                ObjectType = &Ace->ObjectType;
            break;
        }
    }

    return ObjectType;
}

static GUID*
AccpGetObjectAceInheritedObjectType(IN PACE_HEADER AceHeader)
{
    GUID *ObjectType = NULL;

    switch (AceHeader->AceType)
    {
        case ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE:
        {
            PACCESS_ALLOWED_CALLBACK_OBJECT_ACE Ace = (PACCESS_ALLOWED_CALLBACK_OBJECT_ACE)AceHeader;
            if (Ace->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
            {
                if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                    ObjectType = &Ace->InheritedObjectType;
                else
                    ObjectType = &Ace->ObjectType;
            }
            break;
        }
        case ACCESS_ALLOWED_OBJECT_ACE_TYPE:
        case ACCESS_DENIED_OBJECT_ACE_TYPE:
        {
            PACCESS_ALLOWED_OBJECT_ACE Ace = (PACCESS_ALLOWED_OBJECT_ACE)AceHeader;
            if (Ace->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
            {
                if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                    ObjectType = &Ace->InheritedObjectType;
                else
                    ObjectType = &Ace->ObjectType;
            }
            break;
        }

        case SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE:
        {
            PSYSTEM_AUDIT_CALLBACK_OBJECT_ACE Ace = (PSYSTEM_AUDIT_CALLBACK_OBJECT_ACE)AceHeader;
            if (Ace->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
            {
                if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                    ObjectType = &Ace->InheritedObjectType;
                else
                    ObjectType = &Ace->ObjectType;
            }
            break;
        }
        case SYSTEM_AUDIT_OBJECT_ACE_TYPE:
        {
            PSYSTEM_AUDIT_OBJECT_ACE Ace = (PSYSTEM_AUDIT_OBJECT_ACE)AceHeader;
            if (Ace->Flags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
            {
                if (Ace->Flags & ACE_OBJECT_TYPE_PRESENT)
                    ObjectType = &Ace->InheritedObjectType;
                else
                    ObjectType = &Ace->ObjectType;
            }
            break;
        }
    }

    return ObjectType;
}

static DWORD
AccpOpenLSAPolicyHandle(IN LPWSTR SystemName,
                        IN ACCESS_MASK DesiredAccess,
                        OUT PLSA_HANDLE pPolicyHandle)
{
    LSA_OBJECT_ATTRIBUTES LsaObjectAttributes = {0};
    LSA_UNICODE_STRING LsaSystemName, *psn;
    SIZE_T SystemNameLength;
    NTSTATUS Status;

    if (SystemName != NULL && SystemName[0] != L'\0')
    {
        SystemNameLength = wcslen(SystemName);
        if (SystemNameLength > UNICODE_STRING_MAX_CHARS)
        {
            return ERROR_INVALID_PARAMETER;
        }

        LsaSystemName.Buffer = SystemName;
        LsaSystemName.Length = (USHORT)SystemNameLength * sizeof(WCHAR);
        LsaSystemName.MaximumLength = LsaSystemName.Length + sizeof(WCHAR);
        psn = &LsaSystemName;
    }
    else
    {
        psn = NULL;
    }

    Status = LsaOpenPolicy(psn,
                           &LsaObjectAttributes,
                           DesiredAccess,
                           pPolicyHandle);
    if (!NT_SUCCESS(Status))
        return LsaNtStatusToWinError(Status);

    return ERROR_SUCCESS;
}

static LPWSTR
AccpGetTrusteeName(IN PTRUSTEE_W Trustee)
{
    switch (Trustee->TrusteeForm)
    {
        case TRUSTEE_IS_NAME:
            return Trustee->ptstrName;

        case TRUSTEE_IS_OBJECTS_AND_NAME:
            return ((POBJECTS_AND_NAME_W)Trustee->ptstrName)->ptstrName;

        default:
            return NULL;
    }
}

static DWORD
AccpLookupCurrentUser(OUT PSID *ppSid)
{
    DWORD Ret;
    CHAR Buffer[sizeof(TOKEN_USER) + sizeof(SID) + sizeof(DWORD)*SID_MAX_SUB_AUTHORITIES];
    DWORD Length;
    HANDLE Token;
    PSID pSid;

    *ppSid = NULL;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_READ, TRUE, &Token))
    {
        Ret = GetLastError();
        if (Ret != ERROR_NO_TOKEN)
        {
            return Ret;
        }

        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_READ, &Token))
        {
            return GetLastError();
        }
    }

    Length = sizeof(Buffer);
    if (!GetTokenInformation(Token, TokenUser, Buffer, Length, &Length))
    {
        Ret = GetLastError();
        CloseHandle(Token);
        return Ret;
    }
    CloseHandle(Token);

    pSid = ((PTOKEN_USER)Buffer)->User.Sid;
    Length = GetLengthSid(pSid);
    *ppSid = LocalAlloc(LMEM_FIXED, Length);
    if (!*ppSid)
    {
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    CopyMemory(*ppSid, pSid, Length);

    return ERROR_SUCCESS;
}

static DWORD
AccpLookupSidByName(IN LSA_HANDLE PolicyHandle,
                    IN LPWSTR Name,
                    OUT PSID *pSid)
{
    NTSTATUS Status;
    LSA_UNICODE_STRING LsaNames[1];
    PLSA_REFERENCED_DOMAIN_LIST ReferencedDomains = NULL;
    PLSA_TRANSLATED_SID2 TranslatedSid = NULL;
    DWORD SidLen;
    SIZE_T NameLength;
    DWORD Ret = ERROR_SUCCESS;

    NameLength = wcslen(Name);
    if (NameLength > UNICODE_STRING_MAX_CHARS)
    {
        return ERROR_INVALID_PARAMETER;
    }

    LsaNames[0].Buffer = Name;
    LsaNames[0].Length = (USHORT)NameLength * sizeof(WCHAR);
    LsaNames[0].MaximumLength = LsaNames[0].Length + sizeof(WCHAR);

    Status = LsaLookupNames2(PolicyHandle,
                             0,
                             sizeof(LsaNames) / sizeof(LsaNames[0]),
                             LsaNames,
                             &ReferencedDomains,
                             &TranslatedSid);

    if (!NT_SUCCESS(Status))
        return LsaNtStatusToWinError(Status);

    if (TranslatedSid->Use == SidTypeUnknown || TranslatedSid->Use == SidTypeInvalid)
    {
        Ret = LsaNtStatusToWinError(STATUS_NONE_MAPPED); /* FIXME- what error code? */
        goto Cleanup;
    }

    SidLen = GetLengthSid(TranslatedSid->Sid);
    ASSERT(SidLen != 0);

    *pSid = LocalAlloc(LMEM_FIXED, (SIZE_T)SidLen);
    if (*pSid != NULL)
    {
        if (!CopySid(SidLen,
                     *pSid,
                     TranslatedSid->Sid))
        {
            Ret = GetLastError();

            LocalFree((HLOCAL)*pSid);
            *pSid = NULL;
        }
    }
    else
        Ret = ERROR_NOT_ENOUGH_MEMORY;

Cleanup:
    LsaFreeMemory(ReferencedDomains);
    LsaFreeMemory(TranslatedSid);

    return Ret;
}


static DWORD
AccpGetTrusteeSid(IN PTRUSTEE_W Trustee,
                  IN OUT PLSA_HANDLE pPolicyHandle,
                  OUT PSID *ppSid,
                  OUT BOOL *Allocated)
{
    DWORD Ret = ERROR_SUCCESS;
    LPWSTR TrusteeName;

    *ppSid = NULL;
    *Allocated = FALSE;

    if (Trustee->MultipleTrusteeOperation == TRUSTEE_IS_IMPERSONATE)
        return ERROR_INVALID_PARAMETER;

    /* Windows ignores this */
#if 0
    if (Trustee->pMultipleTrustee || Trustee->MultipleTrusteeOperation != NO_MULTIPLE_TRUSTEE)
    {
        /* This is currently not supported */
        return ERROR_INVALID_PARAMETER;
    }
#endif

    switch (Trustee->TrusteeForm)
    {
        case TRUSTEE_IS_OBJECTS_AND_NAME:
            if (((POBJECTS_AND_NAME_W)Trustee->ptstrName)->ObjectsPresent != 0)
            {
                /* This is not supported as there is no way to interpret the
                   strings provided, and we need GUIDs for the ACEs... */
                Ret = ERROR_INVALID_PARAMETER;
                break;
            }
            /* fall through */

        case TRUSTEE_IS_NAME:
            TrusteeName = AccpGetTrusteeName(Trustee);
            if (!wcscmp(TrusteeName, L"CURRENT_USER"))
            {
                Ret = AccpLookupCurrentUser(ppSid);
                if (Ret == ERROR_SUCCESS)
                {
                    ASSERT(*ppSid != NULL);
                    *Allocated = TRUE;
                }
                break;
            }

            if (*pPolicyHandle == NULL)
            {
                Ret = AccpOpenLSAPolicyHandle(NULL, /* FIXME - always local? */
                                              POLICY_LOOKUP_NAMES,
                                              pPolicyHandle);
                if (Ret != ERROR_SUCCESS)
                    return Ret;

                ASSERT(*pPolicyHandle != NULL);
            }

            Ret = AccpLookupSidByName(*pPolicyHandle,
                                      TrusteeName,
                                      ppSid);
            if (Ret == ERROR_SUCCESS)
            {
                ASSERT(*ppSid != NULL);
                *Allocated = TRUE;
            }
            break;

        case TRUSTEE_IS_OBJECTS_AND_SID:
            *ppSid = ((POBJECTS_AND_SID)Trustee->ptstrName)->pSid;
            break;

        case TRUSTEE_IS_SID:
            *ppSid = (PSID)Trustee->ptstrName;
            break;

        default:
            Ret = ERROR_INVALID_PARAMETER;
            break;
    }

    return Ret;
}


static DWORD AccpProjectFileSecurity(HANDLE Handle, PSECURITY_DESCRIPTOR *Descriptor);

/**********************************************************************
 * AccRewriteGetHandleRights				EXPORTED
 *
 * @unimplemented
 */
DWORD WINAPI
AccRewriteGetHandleRights(HANDLE handle,
                          SE_OBJECT_TYPE ObjectType,
                          SECURITY_INFORMATION SecurityInfo,
                          PSID* ppsidOwner,
                          PSID* ppsidGroup,
                          PACL* ppDacl,
                          PACL* ppSacl,
                          PSECURITY_DESCRIPTOR* ppSecurityDescriptor)
{
    PSECURITY_DESCRIPTOR pSD = NULL;
    HKEY QueryKey = NULL;
    ULONG SDSize = 0;
    SECURITY_INFORMATION QueryInformation = SecurityInfo;
    NTSTATUS Status;
    DWORD LastErr;
    DWORD Ret;

    /* save the last error code */
    LastErr = GetLastError();

    if (ObjectType == SE_FILE_OBJECT && (SecurityInfo & DACL_SECURITY_INFORMATION))
        QueryInformation |= OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION;

    if (ObjectType == SE_REGISTRY_KEY)
    {
        REGSAM Access = 0;
        HKEY OpenedKey;

        if (SecurityInfo & (OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                            DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION |
                            LABEL_SECURITY_INFORMATION | ATTRIBUTE_SECURITY_INFORMATION |
                            SCOPE_SECURITY_INFORMATION | PROCESS_TRUST_LABEL_SECURITY_INFORMATION |
                            ACCESS_FILTER_SECURITY_INFORMATION))
            Access |= READ_CONTROL;
        if (SecurityInfo & SACL_SECURITY_INFORMATION)
            Access |= ACCESS_SYSTEM_SECURITY;

        Ret = RegOpenKeyExW((HKEY)handle, NULL, 0,
                            Access | KEY_QUERY_VALUE | KEY_ENUMERATE_SUB_KEYS,
                            &OpenedKey);
        if (Ret != ERROR_SUCCESS && Access != 0)
            Ret = RegOpenKeyExW((HKEY)handle, NULL, 0, Access, &OpenedKey);
        if (Ret != ERROR_SUCCESS && Access != 0)
            goto Cleanup;
        if (Ret == ERROR_SUCCESS)
            QueryKey = OpenedKey;
    }

    do
    {
        Ret = ERROR_SUCCESS;

        /* allocate a buffer large enough to hold the
           security descriptor we need to return */
        if (SDSize > MAXDWORD - 0x100)
        {
            Ret = ERROR_NOT_ENOUGH_MEMORY;
            goto Cleanup;
        }
        SDSize += 0x100;
        if (pSD == NULL)
        {
            pSD = LocalAlloc(LMEM_FIXED,
                             (SIZE_T)SDSize);
        }
        else
        {
            PSECURITY_DESCRIPTOR newSD;

            newSD = LocalReAlloc((HLOCAL)pSD,
                                 (SIZE_T)SDSize,
                                 LMEM_MOVEABLE);
            if (newSD != NULL)
                pSD = newSD;
            else
            {
                Ret = GetLastError();
                goto Cleanup;
            }
        }

        if (pSD == NULL)
        {
            Ret = GetLastError();
            break;
        }

        /* perform the actual query depending on the object type */
        switch (ObjectType)
        {
            case SE_REGISTRY_KEY:
            {
                Ret = (DWORD)RegGetKeySecurity(QueryKey ? QueryKey : (HKEY)handle,
                                               SecurityInfo & ~(PROTECTED_DACL_SECURITY_INFORMATION |
                                                                PROTECTED_SACL_SECURITY_INFORMATION |
                                                                UNPROTECTED_DACL_SECURITY_INFORMATION |
                                                                UNPROTECTED_SACL_SECURITY_INFORMATION),
                                               pSD,
                                               &SDSize);
                break;
            }

            case SE_FILE_OBJECT:
                /* FIXME - handle console handles? */
            case SE_KERNEL_OBJECT:
            case SE_WMIGUID_OBJECT:
            {
                Status = NtQuerySecurityObject(handle,
                                               QueryInformation,
                                               pSD,
                                               SDSize,
                                               &SDSize);
                if (!NT_SUCCESS(Status))
                {
                    Ret = RtlNtStatusToDosError(Status);
                }
                break;
            }

            case SE_SERVICE:
            {
                if (!QueryServiceObjectSecurity((SC_HANDLE)handle,
                                                SecurityInfo,
                                                pSD,
                                                SDSize,
                                                &SDSize))
                {
                    Ret = GetLastError();
                }
                break;
            }

            case SE_WINDOW_OBJECT:
            {
                if (!GetUserObjectSecurity(handle,
                                           &SecurityInfo,
                                           pSD,
                                           SDSize,
                                           &SDSize))
                {
                    Ret = GetLastError();
                }
                break;
            }

            case SE_PRINTER:
            case SE_LMSHARE:
                Ret = ERROR_CALL_NOT_IMPLEMENTED;
                break;

            default:
                Ret = ERROR_INVALID_PARAMETER;
                break;
        }

    } while (Ret == ERROR_INSUFFICIENT_BUFFER);

    if (Ret == ERROR_SUCCESS && ObjectType == SE_FILE_OBJECT &&
        (SecurityInfo & DACL_SECURITY_INFORMATION))
        Ret = AccpProjectFileSecurity(handle, &pSD);

    if (Ret == ERROR_SUCCESS)
    {
        BOOL Present, Defaulted;

        if (SecurityInfo & OWNER_SECURITY_INFORMATION && ppsidOwner != NULL)
        {
            *ppsidOwner = NULL;
            if (!GetSecurityDescriptorOwner(pSD,
                                            ppsidOwner,
                                            &Defaulted))
            {
                Ret = GetLastError();
                goto Cleanup;
            }
        }

        if (SecurityInfo & GROUP_SECURITY_INFORMATION && ppsidGroup != NULL)
        {
            *ppsidGroup = NULL;
            if (!GetSecurityDescriptorGroup(pSD,
                                            ppsidGroup,
                                            &Defaulted))
            {
                Ret = GetLastError();
                goto Cleanup;
            }
        }

        if (SecurityInfo & DACL_SECURITY_INFORMATION && ppDacl != NULL)
        {
            *ppDacl = NULL;
            if (!GetSecurityDescriptorDacl(pSD,
                                           &Present,
                                           ppDacl,
                                           &Defaulted))
            {
                Ret = GetLastError();
                goto Cleanup;
            }
        }

        if ((SecurityInfo & (SACL_SECURITY_INFORMATION | LABEL_SECURITY_INFORMATION)) && ppSacl != NULL)
        {
            *ppSacl = NULL;
            if (!GetSecurityDescriptorSacl(pSD,
                                           &Present,
                                           ppSacl,
                                           &Defaulted))
            {
                Ret = GetLastError();
                goto Cleanup;
            }
        }

        if (ppSecurityDescriptor)
            *ppSecurityDescriptor = pSD;
    }
    else
    {
Cleanup:
        if (pSD != NULL)
        {
            LocalFree((HLOCAL)pSD);
        }
    }

    if (QueryKey != NULL)
        RegCloseKey(QueryKey);

    /* restore the last error code */
    SetLastError(LastErr);

    return Ret;
}


static NTSTATUS
AccpQueryFileSecurity(HANDLE Handle,
                     SECURITY_INFORMATION Information,
                     PSECURITY_DESCRIPTOR *Descriptor)
{
    ULONG Length = 0;
    ULONG Attempt;
    NTSTATUS Status;

    *Descriptor = NULL;
    Status = NtQuerySecurityObject(Handle, Information, NULL, 0, &Length);
    for (Attempt = 0; Status == STATUS_BUFFER_TOO_SMALL && Attempt < 3; ++Attempt)
    {
        ULONG Capacity = Length;
        *Descriptor = HeapAlloc(GetProcessHeap(), 0, Capacity);
        if (!*Descriptor) return STATUS_INSUFFICIENT_RESOURCES;
        Status = NtQuerySecurityObject(Handle, Information, *Descriptor, Capacity, &Length);
        if (NT_SUCCESS(Status)) return Status;
        HeapFree(GetProcessHeap(), 0, *Descriptor);
        *Descriptor = NULL;
    }
    return Status;
}

static DWORD
AccpProjectFileSecurity(HANDLE Handle, PSECURITY_DESCRIPTOR *Descriptor)
{
    GENERIC_MAPPING Mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE,
                               FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    PSECURITY_DESCRIPTOR Parent = NULL, Converted = NULL, Published = NULL;
    POBJECT_NAME_INFORMATION Name = NULL;
    SECURITY_DESCRIPTOR Input, Result;
    SECURITY_DESCRIPTOR_CONTROL Control, ConvertedControl;
    FILE_BASIC_INFORMATION Basic;
    struct
    {
        ULONG Length;
        WCHAR Name[2];
    } FileName;
    IO_STATUS_BLOCK IoStatus;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ParentName;
    HANDLE ParentHandle = NULL;
    PACL Dacl;
    BOOLEAN Present, Defaulted;
    ULONG Length, Capacity = 0, Revision, Attempt, Index;
    NTSTATUS Status;
    DWORD Ret = ERROR_SUCCESS;

    Status = RtlGetControlSecurityDescriptor(*Descriptor, &Control, &Revision);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    if (Control & (SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED))
        return ERROR_SUCCESS;
    Status = RtlGetDaclSecurityDescriptor(*Descriptor, &Present, &Dacl, &Defaulted);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    if (!Present || !Dacl) return ERROR_SUCCESS;

    Status = NtQueryInformationFile(Handle, &IoStatus, &Basic,
                                    sizeof(Basic), FileBasicInformation);
    if (Status == STATUS_ACCESS_DENIED)
        return RtlNtStatusToDosError(RtlSetControlSecurityDescriptor(*Descriptor,
                                      SE_DACL_PROTECTED, SE_DACL_PROTECTED));
    if (!NT_SUCCESS(Status)) return ERROR_SUCCESS;
    Status = NtQueryInformationFile(Handle, &IoStatus, &FileName,
                                    sizeof(FileName), FileNameInformation);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
        return ERROR_SUCCESS;
    if (!FileName.Length || (FileName.Length % sizeof(WCHAR)) ||
        FileName.Name[0] != L'\\' || FileName.Length <= sizeof(WCHAR) ||
        FileName.Name[1] == L':')
        return ERROR_SUCCESS;

    Length = 0;
    Status = NtQueryObject(Handle, ObjectNameInformation, NULL, 0, &Length);
    for (Attempt = 0; Attempt < 3 &&
         (Status == STATUS_INFO_LENGTH_MISMATCH || Status == STATUS_BUFFER_TOO_SMALL ||
          Status == STATUS_BUFFER_OVERFLOW); ++Attempt)
    {
        HeapFree(GetProcessHeap(), 0, Name);
        Name = NULL;
        if (Length < sizeof(*Name)) goto Cleanup;
        Capacity = Length;
        Name = HeapAlloc(GetProcessHeap(), 0, Capacity);
        if (!Name)
        {
            Ret = ERROR_NOT_ENOUGH_MEMORY;
            goto Cleanup;
        }
        Status = NtQueryObject(Handle, ObjectNameInformation, Name, Capacity, &Length);
    }
    if (!NT_SUCCESS(Status) || !Name ||
        !Name->Name.Length || (Name->Name.Length % sizeof(WCHAR)) ||
        (ULONG_PTR)Name->Name.Buffer < (ULONG_PTR)Name + sizeof(*Name) ||
        (ULONG_PTR)Name->Name.Buffer - (ULONG_PTR)Name > Capacity ||
        Name->Name.Length > Capacity - ((ULONG_PTR)Name->Name.Buffer - (ULONG_PTR)Name))
        goto Cleanup;
    ParentName = Name->Name;
    if (ParentName.Buffer[0] != L'\\') goto Cleanup;
    for (Index = 0; Index < ParentName.Length / sizeof(WCHAR); ++Index)
        if (ParentName.Buffer[Index] == L':') goto Cleanup;
    while (ParentName.Length && ParentName.Buffer[ParentName.Length / sizeof(WCHAR) - 1] == L'\\')
        ParentName.Length -= sizeof(WCHAR);
    while (ParentName.Length && ParentName.Buffer[ParentName.Length / sizeof(WCHAR) - 1] != L'\\')
        ParentName.Length -= sizeof(WCHAR);
    if (!ParentName.Length) goto Cleanup;

    InitializeObjectAttributes(&Attributes, &ParentName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&ParentHandle, READ_CONTROL, &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = AccpQueryFileSecurity(ParentHandle, DACL_SECURITY_INFORMATION, &Parent);
    if (!NT_SUCCESS(Status)) goto Cleanup;

    RtlCreateSecurityDescriptor(&Result, SECURITY_DESCRIPTOR_REVISION);
    Result.Control = Control & ~SE_SELF_RELATIVE;
    Result.Sbz1 = ((PISECURITY_DESCRIPTOR)*Descriptor)->Sbz1;
    Status = RtlGetOwnerSecurityDescriptor(*Descriptor, &Result.Owner, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = RtlGetGroupSecurityDescriptor(*Descriptor, &Result.Group, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = RtlGetSaclSecurityDescriptor(*Descriptor, &Present, &Result.Sacl, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Failed;
    Result.Dacl = Dacl;
    Input = Result;
    Input.Sacl = NULL;
    Input.Control &= ~(SE_SACL_PRESENT | SE_SACL_DEFAULTED | SE_SACL_AUTO_INHERIT_REQ |
                       SE_SACL_AUTO_INHERITED);
    Input.Control |= SE_SACL_PROTECTED;
    if (!ConvertToAutoInheritPrivateObjectSecurity(Parent, &Input, &Converted, NULL,
            !!(Basic.FileAttributes & FILE_ATTRIBUTE_DIRECTORY), &Mapping))
    {
        Ret = GetLastError();
        if (Ret == ERROR_CALL_NOT_IMPLEMENTED || Ret == ERROR_NOT_SUPPORTED)
            Ret = ERROR_SUCCESS;
        goto Cleanup;
    }
    Status = RtlGetControlSecurityDescriptor(Converted, &ConvertedControl, &Revision);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = RtlGetDaclSecurityDescriptor(Converted, &Present, &Result.Dacl, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Failed;
    Result.Control = (Result.Control & ~SE_DACL_PROTECTED) |
                     (ConvertedControl & SE_DACL_PROTECTED);
    Length = 0;
    Status = RtlAbsoluteToSelfRelativeSD(&Result, NULL, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL) goto Failed;
    Published = LocalAlloc(LMEM_FIXED, Length);
    if (!Published)
    {
        Ret = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }
    Status = RtlAbsoluteToSelfRelativeSD(&Result, Published, &Length);
    if (!NT_SUCCESS(Status)) goto Failed;
    LocalFree(*Descriptor);
    *Descriptor = Published;
    Published = NULL;
    goto Cleanup;

Failed:
    Ret = RtlNtStatusToDosError(Status);
Cleanup:
    if (Published) LocalFree(Published);
    if (Converted) DestroyPrivateObjectSecurity(&Converted);
    if (ParentHandle) NtClose(ParentHandle);
    HeapFree(GetProcessHeap(), 0, Name);
    HeapFree(GetProcessHeap(), 0, Parent);
    return Ret;
}

static NTSTATUS
AccpSetFileSecurity(HANDLE Handle,
                   SECURITY_INFORMATION Information,
                   PSECURITY_DESCRIPTOR Descriptor)
{
    GENERIC_MAPPING Mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE,
                               FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    PSECURITY_DESCRIPTOR Current = NULL, Parent = NULL, Inherited = NULL;
    POBJECT_NAME_INFORMATION Name = NULL;
    SECURITY_DESCRIPTOR Creator, Result, Published;
    SECURITY_DESCRIPTOR_CONTROL Control, InheritedControl;
    const SECURITY_DESCRIPTOR_CONTROL DaclControl = SE_DACL_PRESENT | SE_DACL_DEFAULTED |
        SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED;
    FILE_STANDARD_INFORMATION Standard;
    struct
    {
        ULONG Length;
        WCHAR Name[2];
    } FileName;
    IO_STATUS_BLOCK IoStatus;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING ParentName;
    HANDLE ParentHandle = NULL;
    PACL Dacl;
    BOOLEAN Present, Defaulted;
    ULONG Length, Revision, Attempt;
    NTSTATUS Status;

    if (Information & SACL_SECURITY_INFORMATION)
    {
        Status = AccpQueryFileSecurity(Handle, OWNER_SECURITY_INFORMATION |
                                      GROUP_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION |
                                      (Information & DACL_SECURITY_INFORMATION),
                                      &Current);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Status = RtlCreateSecurityDescriptor(&Published, SECURITY_DESCRIPTOR_REVISION);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Status = RtlGetControlSecurityDescriptor(Descriptor, &Control, &Revision);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Published.Control = (Control & ~(SE_SELF_RELATIVE | SE_SACL_AUTO_INHERIT_REQ)) |
                            SE_SACL_AUTO_INHERITED;
        Published.Sbz1 = ((PISECURITY_DESCRIPTOR)Descriptor)->Sbz1;
        Status = RtlGetOwnerSecurityDescriptor((Information & OWNER_SECURITY_INFORMATION) ?
                                               Descriptor : Current, &Published.Owner, &Defaulted);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Status = RtlGetGroupSecurityDescriptor((Information & GROUP_SECURITY_INFORMATION) ?
                                               Descriptor : Current, &Published.Group, &Defaulted);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Status = RtlGetDaclSecurityDescriptor(Descriptor, &Present, &Published.Dacl, &Defaulted);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Status = RtlGetSaclSecurityDescriptor(Descriptor, &Present, &Published.Sacl, &Defaulted);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Descriptor = &Published;
    }

    if (!(Information & DACL_SECURITY_INFORMATION) ||
        (Information & PROTECTED_DACL_SECURITY_INFORMATION))
    {
        Status = NtSetSecurityObject(Handle, Information, Descriptor);
        goto Cleanup;
    }

    Status = RtlGetDaclSecurityDescriptor(Descriptor, &Present, &Dacl, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    if (!Present || !Dacl)
    {
        Status = NtSetSecurityObject(Handle, Information, Descriptor);
        goto Cleanup;
    }

    if (!Current)
    {
        Status = AccpQueryFileSecurity(Handle, OWNER_SECURITY_INFORMATION |
                                      GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                      &Current);
        if (!NT_SUCCESS(Status)) goto Cleanup;
    }
    Status = RtlGetControlSecurityDescriptor(Current, &Control, &Revision);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    if ((Control & SE_DACL_PROTECTED) &&
        !(Information & UNPROTECTED_DACL_SECURITY_INFORMATION)) goto Direct;

    Status = NtQueryInformationFile(Handle, &IoStatus, &Standard,
                                    sizeof(Standard), FileStandardInformation);
    if (!NT_SUCCESS(Status)) goto Direct;
    Status = NtQueryInformationFile(Handle, &IoStatus, &FileName,
                                    sizeof(FileName), FileNameInformation);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) goto Direct;
    if (!FileName.Length ||
        (FileName.Name[0] == L'\\' &&
         (FileName.Length == sizeof(WCHAR) || FileName.Name[1] == L':'))) goto Direct;

    Length = 0;
    Status = NtQueryObject(Handle, ObjectNameInformation, NULL, 0, &Length);
    for (Attempt = 0; Attempt < 3 &&
         (Status == STATUS_INFO_LENGTH_MISMATCH || Status == STATUS_BUFFER_TOO_SMALL ||
          Status == STATUS_BUFFER_OVERFLOW); ++Attempt)
    {
        HeapFree(GetProcessHeap(), 0, Name);
        Name = HeapAlloc(GetProcessHeap(), 0, Length);
        if (!Name)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        Status = NtQueryObject(Handle, ObjectNameInformation, Name, Length, &Length);
    }
    if (!NT_SUCCESS(Status)) goto Cleanup;
    ParentName = Name->Name;
    while (ParentName.Length && ParentName.Buffer[ParentName.Length / sizeof(WCHAR) - 1] == L'\\')
        ParentName.Length -= sizeof(WCHAR);
    while (ParentName.Length && ParentName.Buffer[ParentName.Length / sizeof(WCHAR) - 1] != L'\\')
        ParentName.Length -= sizeof(WCHAR);
    if (!ParentName.Length) goto Direct;

    InitializeObjectAttributes(&Attributes, &ParentName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&ParentHandle, READ_CONTROL | SYNCHRONIZE, &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_FOR_BACKUP_INTENT);
    if (!NT_SUCCESS(Status)) goto Direct;
    Status = AccpQueryFileSecurity(ParentHandle, DACL_SECURITY_INFORMATION, &Parent);
    if (!NT_SUCCESS(Status)) goto Direct;

    RtlCreateSecurityDescriptor(&Creator, SECURITY_DESCRIPTOR_REVISION);
    Status = RtlGetOwnerSecurityDescriptor((Information & OWNER_SECURITY_INFORMATION) ?
                                           Descriptor : Current, &Creator.Owner, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = RtlGetGroupSecurityDescriptor((Information & GROUP_SECURITY_INFORMATION) ?
                                           Descriptor : Current, &Creator.Group, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Creator.Dacl = Dacl;
    Creator.Control |= SE_DACL_PRESENT | SE_SACL_PRESENT | SE_SACL_PROTECTED;
    Status = RtlNewSecurityObjectEx(Parent, &Creator, &Inherited, NULL, Standard.Directory,
                                    SEF_DACL_AUTO_INHERIT | SEF_AVOID_OWNER_CHECK |
                                    SEF_AVOID_PRIVILEGE_CHECK | SEF_AVOID_OWNER_RESTRICTION,
                                    NULL, &Mapping);
    if (!NT_SUCCESS(Status)) goto Cleanup;

    RtlCreateSecurityDescriptor(&Result, SECURITY_DESCRIPTOR_REVISION);
    Status = RtlGetControlSecurityDescriptor(Descriptor, &Control, &Revision);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = RtlGetControlSecurityDescriptor(Inherited, &InheritedControl, &Revision);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Result.Control = (Control & ~(SE_SELF_RELATIVE | DaclControl)) |
                     (InheritedControl & DaclControl) | SE_DACL_AUTO_INHERIT_REQ;
    Result.Sbz1 = ((PISECURITY_DESCRIPTOR)Descriptor)->Sbz1;
    Status = RtlGetOwnerSecurityDescriptor(Descriptor, &Result.Owner, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = RtlGetGroupSecurityDescriptor(Descriptor, &Result.Group, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = RtlGetDaclSecurityDescriptor(Inherited, &Present, &Result.Dacl, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = RtlGetSaclSecurityDescriptor(Descriptor, &Present, &Result.Sacl, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = NtSetSecurityObject(Handle, Information, &Result);
    goto Cleanup;

Direct:
    if (Status == STATUS_INSUFFICIENT_RESOURCES || Status == STATUS_NO_MEMORY ||
        Status == STATUS_BUFFER_TOO_SMALL || Status == STATUS_INFO_LENGTH_MISMATCH)
        goto Cleanup;
    Status = NtSetSecurityObject(Handle, Information, Descriptor);
Cleanup:
    if (Inherited) RtlDeleteSecurityObject(&Inherited);
    if (ParentHandle) NtClose(ParentHandle);
    HeapFree(GetProcessHeap(), 0, Name);
    HeapFree(GetProcessHeap(), 0, Parent);
    HeapFree(GetProcessHeap(), 0, Current);
    return Status;
}

static DWORD
AccpInheritFileDacl(HANDLE Handle,
                   BOOLEAN Directory,
                   PSECURITY_DESCRIPTOR OldParent,
                   PSECURITY_DESCRIPTOR NewParent,
                   PSECURITY_DESCRIPTOR Current,
                   PBOOLEAN Propagate)
{
    GENERIC_MAPPING Mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE,
                               FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    const SECURITY_DESCRIPTOR_CONTROL DaclControl = SE_DACL_PRESENT | SE_DACL_DEFAULTED |
        SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED;
    SECURITY_DESCRIPTOR Creator;
    SECURITY_DESCRIPTOR_CONTROL Control;
    PSECURITY_DESCRIPTOR Converted = NULL, Inherited = NULL;
    PACL Dacl;
    BOOLEAN Present, Defaulted;
    ULONG Revision;
    NTSTATUS Status;
    DWORD Ret;

    *Propagate = FALSE;
    Status = RtlGetControlSecurityDescriptor(Current, &Control, &Revision);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    if (Control & SE_DACL_PROTECTED) return ERROR_SUCCESS;
    Status = RtlGetDaclSecurityDescriptor(Current, &Present, &Dacl, &Defaulted);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    Status = RtlCreateSecurityDescriptor(&Creator, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    Status = RtlGetOwnerSecurityDescriptor(Current, &Creator.Owner, &Defaulted);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    Status = RtlGetGroupSecurityDescriptor(Current, &Creator.Group, &Defaulted);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    Creator.Dacl = Dacl;
    Creator.Control = (Control & ~(SE_SELF_RELATIVE | SE_SACL_PRESENT | SE_SACL_DEFAULTED |
                                   SE_SACL_AUTO_INHERIT_REQ | SE_SACL_AUTO_INHERITED)) |
                      SE_SACL_PROTECTED;
    Creator.Sbz1 = ((PISECURITY_DESCRIPTOR)Current)->Sbz1;
    if (!(Control & SE_DACL_AUTO_INHERITED) && Present && Dacl && Dacl->AceCount)
    {
        if (!ConvertToAutoInheritPrivateObjectSecurity(OldParent, &Creator, &Converted,
                                                       NULL, Directory, &Mapping))
            return GetLastError();
        Status = RtlGetControlSecurityDescriptor(Converted, &Control, &Revision);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Status = RtlGetDaclSecurityDescriptor(Converted, &Present, &Creator.Dacl, &Defaulted);
        if (!NT_SUCCESS(Status)) goto Cleanup;
        Creator.Control = (Creator.Control & ~DaclControl) | (Control & DaclControl);
    }
    Status = RtlNewSecurityObjectEx(NewParent, &Creator, &Inherited, NULL, Directory,
                                    SEF_DACL_AUTO_INHERIT | SEF_AVOID_OWNER_CHECK |
                                    SEF_AVOID_PRIVILEGE_CHECK | SEF_AVOID_OWNER_RESTRICTION,
                                    NULL, &Mapping);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = RtlSetControlSecurityDescriptor(Inherited, SE_DACL_AUTO_INHERIT_REQ,
                                            SE_DACL_AUTO_INHERIT_REQ);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = NtSetSecurityObject(Handle, DACL_SECURITY_INFORMATION, Inherited);
    if (!NT_SUCCESS(Status)) goto Cleanup;
    Status = RtlGetControlSecurityDescriptor(Inherited, &Control, &Revision);
    if (NT_SUCCESS(Status)) *Propagate = !(Control & SE_DACL_PROTECTED);
Cleanup:
    Ret = RtlNtStatusToDosError(Status);
    if (Inherited) RtlDeleteSecurityObject(&Inherited);
    if (Converted) DestroyPrivateObjectSecurity(&Converted);
    return Ret;
}

typedef struct _ACCP_SECURITY_DIRECTORY
{
    struct _ACCP_SECURITY_DIRECTORY *Previous;
    HANDLE Handle;
    PSECURITY_DESCRIPTOR OldDescriptor;
    PSECURITY_DESCRIPTOR NewDescriptor;
    BOOLEAN RestartScan;
} ACCP_SECURITY_DIRECTORY, *PACCP_SECURITY_DIRECTORY;

static DWORD
AccpPropagateFileDacl(HANDLE DirectoryHandle,
                     PSECURITY_DESCRIPTOR OldDescriptor,
                     PSECURITY_DESCRIPTOR NewDescriptor)
{
    ACCP_SECURITY_DIRECTORY Root = {NULL, DirectoryHandle, OldDescriptor, NewDescriptor, TRUE};
    PACCP_SECURITY_DIRECTORY Directory = &Root, Next;
    PFILE_DIRECTORY_INFORMATION Entry;
    PSECURITY_DESCRIPTOR OldChild = NULL, NewChild = NULL;
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Standard;
    SECURITY_DESCRIPTOR_CONTROL Control;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Name, Empty = {0};
    IO_STATUS_BLOCK IoStatus;
    HANDLE ChildHandle = NULL, EnumerationHandle = NULL;
    ULONG Index, Revision;
    const ULONG Capacity = 65536;
    BOOLEAN Propagate;
    NTSTATUS Status;
    DWORD Ret = ERROR_SUCCESS;

    Entry = HeapAlloc(GetProcessHeap(), 0, Capacity);
    if (!Entry) return ERROR_NOT_ENOUGH_MEMORY;
    while (Directory)
    {
        Status = NtQueryDirectoryFile(Directory->Handle, NULL, NULL, NULL, &IoStatus,
                                       Entry, Capacity, FileDirectoryInformation, TRUE,
                                       NULL, Directory->RestartScan);
        Directory->RestartScan = FALSE;
        if (Status == STATUS_NO_MORE_FILES)
        {
            if (Directory == &Root) break;
            Next = Directory->Previous;
            NtClose(Directory->Handle);
            HeapFree(GetProcessHeap(), 0, Directory->OldDescriptor);
            HeapFree(GetProcessHeap(), 0, Directory->NewDescriptor);
            HeapFree(GetProcessHeap(), 0, Directory);
            Directory = Next;
            continue;
        }
        if (!NT_SUCCESS(Status))
        {
            Ret = RtlNtStatusToDosError(Status);
            break;
        }
        if (IoStatus.Information < FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) ||
            IoStatus.Information > Capacity || Entry->NextEntryOffset ||
            !Entry->FileNameLength || Entry->FileNameLength % sizeof(WCHAR) ||
            Entry->FileNameLength > MAXUSHORT ||
            Entry->FileNameLength > IoStatus.Information -
                                    FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName))
        {
            Ret = ERROR_INVALID_DATA;
            break;
        }
        if ((Entry->FileNameLength == sizeof(WCHAR) && Entry->FileName[0] == L'.') ||
            (Entry->FileNameLength == 2 * sizeof(WCHAR) &&
             Entry->FileName[0] == L'.' && Entry->FileName[1] == L'.'))
            continue;
        for (Index = 0; Index < Entry->FileNameLength / sizeof(WCHAR); ++Index)
            if (!Entry->FileName[Index] || Entry->FileName[Index] == L'\\' ||
                Entry->FileName[Index] == L'/' || Entry->FileName[Index] == L':')
                break;
        if (Index != Entry->FileNameLength / sizeof(WCHAR))
        {
            Ret = ERROR_INVALID_DATA;
            break;
        }
        Name.Buffer = Entry->FileName;
        Name.Length = Name.MaximumLength = (USHORT)Entry->FileNameLength;
        InitializeObjectAttributes(&Attributes, &Name, OBJ_CASE_INSENSITIVE,
                                   Directory->Handle, NULL);
        Status = NtOpenFile(&ChildHandle, READ_CONTROL | WRITE_DAC | FILE_READ_ATTRIBUTES |
                                         SYNCHRONIZE, &Attributes, &IoStatus,
                            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                            FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT);
        if (Status == STATUS_ACCESS_DENIED || Status == STATUS_SHARING_VIOLATION)
            continue;
        if (!NT_SUCCESS(Status)) goto Failed;
        Status = NtQueryInformationFile(ChildHandle, &IoStatus, &Basic,
                                        sizeof(Basic), FileBasicInformation);
        if (!NT_SUCCESS(Status)) goto Failed;
        if (Basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            Ret = ERROR_NOT_SUPPORTED;
            break;
        }
        Status = NtQueryInformationFile(ChildHandle, &IoStatus, &Standard,
                                        sizeof(Standard), FileStandardInformation);
        if (!NT_SUCCESS(Status)) goto Failed;
        Status = AccpQueryFileSecurity(ChildHandle, OWNER_SECURITY_INFORMATION |
                                      GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                      &OldChild);
        if (!NT_SUCCESS(Status)) goto Failed;
        Status = RtlGetControlSecurityDescriptor(OldChild, &Control, &Revision);
        if (!NT_SUCCESS(Status)) goto Failed;
        if (Standard.Directory && !(Control & SE_DACL_PROTECTED))
        {
            InitializeObjectAttributes(&Attributes, &Empty, OBJ_CASE_INSENSITIVE,
                                       ChildHandle, NULL);
            Status = NtOpenFile(&EnumerationHandle, FILE_LIST_DIRECTORY | SYNCHRONIZE,
                                &Attributes, &IoStatus,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT |
                                FILE_SYNCHRONOUS_IO_NONALERT);
            if (!NT_SUCCESS(Status) && Status != STATUS_SHARING_VIOLATION) goto Failed;
        }
        Ret = AccpInheritFileDacl(ChildHandle, Standard.Directory, Directory->OldDescriptor,
                                 Directory->NewDescriptor, OldChild, &Propagate);
        if (Ret != ERROR_SUCCESS) break;
        if (Propagate && EnumerationHandle)
        {
            Status = AccpQueryFileSecurity(ChildHandle, OWNER_SECURITY_INFORMATION |
                                          GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                          &NewChild);
            if (!NT_SUCCESS(Status)) goto Failed;
            Next = HeapAlloc(GetProcessHeap(), 0, sizeof(*Next));
            if (!Next)
            {
                Ret = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            Next->Previous = Directory;
            Next->Handle = EnumerationHandle;
            Next->OldDescriptor = OldChild;
            Next->NewDescriptor = NewChild;
            Next->RestartScan = TRUE;
            Directory = Next;
            EnumerationHandle = NULL;
            OldChild = NewChild = NULL;
        }
        NtClose(ChildHandle);
        ChildHandle = NULL;
        if (EnumerationHandle) NtClose(EnumerationHandle);
        EnumerationHandle = NULL;
        HeapFree(GetProcessHeap(), 0, OldChild);
        OldChild = NULL;
        continue;

Failed:
        Ret = RtlNtStatusToDosError(Status);
        break;
    }
    if (ChildHandle) NtClose(ChildHandle);
    if (EnumerationHandle) NtClose(EnumerationHandle);
    HeapFree(GetProcessHeap(), 0, OldChild);
    HeapFree(GetProcessHeap(), 0, NewChild);
    while (Directory && Directory != &Root)
    {
        Next = Directory->Previous;
        NtClose(Directory->Handle);
        HeapFree(GetProcessHeap(), 0, Directory->OldDescriptor);
        HeapFree(GetProcessHeap(), 0, Directory->NewDescriptor);
        HeapFree(GetProcessHeap(), 0, Directory);
        Directory = Next;
    }
    HeapFree(GetProcessHeap(), 0, Entry);
    return Ret;
}

static DWORD
AccpSetAndPropagateFileSecurity(HANDLE Handle,
                               SECURITY_INFORMATION Information,
                               PSECURITY_DESCRIPTOR Descriptor)
{
    PSECURITY_DESCRIPTOR OldDescriptor = NULL, NewDescriptor = NULL;
    FILE_ACCESS_INFORMATION Access;
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Standard;
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Empty = {0};
    IO_STATUS_BLOCK IoStatus;
    HANDLE DirectoryHandle = NULL;
    NTSTATUS Status;
    DWORD Ret;

    if (!(Information & DACL_SECURITY_INFORMATION))
        return RtlNtStatusToDosError(AccpSetFileSecurity(Handle, Information, Descriptor));
    Status = NtQueryInformationFile(Handle, &IoStatus, &Standard,
                                    sizeof(Standard), FileStandardInformation);
    if (!NT_SUCCESS(Status) || !Standard.Directory)
        return RtlNtStatusToDosError(AccpSetFileSecurity(Handle, Information, Descriptor));
    Status = NtQueryInformationFile(Handle, &IoStatus, &Access,
                                    sizeof(Access), FileAccessInformation);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    if (Access.AccessFlags == FILE_ALL_ACCESS)
        return RtlNtStatusToDosError(AccpSetFileSecurity(Handle, Information, Descriptor));
    Status = AccpQueryFileSecurity(Handle, OWNER_SECURITY_INFORMATION |
                                  GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  &OldDescriptor);
    if (!NT_SUCCESS(Status)) goto Failed;
    InitializeObjectAttributes(&Attributes, &Empty, OBJ_CASE_INSENSITIVE, Handle, NULL);
    Status = NtOpenFile(&DirectoryHandle, FILE_LIST_DIRECTORY | FILE_READ_ATTRIBUTES |
                        SYNCHRONIZE, &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT |
                        FILE_SYNCHRONOUS_IO_NONALERT);
    if (Status == STATUS_SHARING_VIOLATION)
    {
        Status = AccpSetFileSecurity(Handle, Information, Descriptor);
        goto Failed;
    }
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = NtQueryInformationFile(DirectoryHandle, &IoStatus, &Basic,
                                    sizeof(Basic), FileBasicInformation);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = AccpSetFileSecurity(Handle, Information, Descriptor);
    if (!NT_SUCCESS(Status)) goto Failed;
    if (Basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
    {
        Ret = ERROR_NOT_SUPPORTED;
        goto Cleanup;
    }
    Status = AccpQueryFileSecurity(Handle, OWNER_SECURITY_INFORMATION |
                                  GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  &NewDescriptor);
    if (!NT_SUCCESS(Status)) goto Failed;
    Ret = AccpPropagateFileDacl(DirectoryHandle, OldDescriptor, NewDescriptor);
    goto Cleanup;

Failed:
    Ret = RtlNtStatusToDosError(Status);
Cleanup:
    if (DirectoryHandle) NtClose(DirectoryHandle);
    HeapFree(GetProcessHeap(), 0, OldDescriptor);
    HeapFree(GetProcessHeap(), 0, NewDescriptor);
    return Ret;
}

static DWORD
AccpQueryRegistrySecurity(HKEY Key,
                         SECURITY_INFORMATION Information,
                         PSECURITY_DESCRIPTOR *Descriptor)
{
    DWORD Length = 0, Attempt, Ret;

    *Descriptor = NULL;
    Information |= OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION;
    Ret = RegGetKeySecurity(Key, Information, NULL, &Length);
    for (Attempt = 0; Ret == ERROR_INSUFFICIENT_BUFFER && Attempt < 3; ++Attempt)
    {
        DWORD Capacity = Length;
        if (!Capacity) return ERROR_INVALID_DATA;
        *Descriptor = HeapAlloc(GetProcessHeap(), 0, Capacity);
        if (!*Descriptor) return ERROR_NOT_ENOUGH_MEMORY;
        Ret = RegGetKeySecurity(Key, Information, *Descriptor, &Length);
        if (Ret == ERROR_SUCCESS) return Ret;
        HeapFree(GetProcessHeap(), 0, *Descriptor);
        *Descriptor = NULL;
    }
    return Ret;
}

static DWORD
AccpInheritRegistrySecurity(HKEY Key,
                       PSECURITY_DESCRIPTOR OldParent,
                       PSECURITY_DESCRIPTOR NewParent,
                       PSECURITY_DESCRIPTOR Current,
                       SECURITY_INFORMATION Information,
                       PSECURITY_INFORMATION Propagate)
{
    GENERIC_MAPPING Mapping = {KEY_READ, KEY_WRITE, KEY_EXECUTE, KEY_ALL_ACCESS};
    const SECURITY_DESCRIPTOR_CONTROL DaclControl = SE_DACL_PRESENT | SE_DACL_DEFAULTED |
        SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED | SE_DACL_PROTECTED;
    const SECURITY_DESCRIPTOR_CONTROL SaclControl = SE_SACL_PRESENT | SE_SACL_DEFAULTED |
        SE_SACL_AUTO_INHERIT_REQ | SE_SACL_AUTO_INHERITED | SE_SACL_PROTECTED;
    SECURITY_DESCRIPTOR Creator, Published;
    SECURITY_DESCRIPTOR_CONTROL Control;
    PSECURITY_DESCRIPTOR Converted = NULL, Inherited = NULL;
    BOOLEAN Present, Defaulted;
    ULONG Revision, Flags = 0;
    NTSTATUS Status;
    DWORD Ret;

    *Propagate = 0;
    Status = RtlGetControlSecurityDescriptor(Current, &Control, &Revision);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    if (Control & SE_DACL_PROTECTED) Information &= ~DACL_SECURITY_INFORMATION;
    if (Control & SE_SACL_PROTECTED) Information &= ~SACL_SECURITY_INFORMATION;
    if (!Information) return ERROR_SUCCESS;
    Status = RtlCreateSecurityDescriptor(&Creator, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    Status = RtlGetOwnerSecurityDescriptor(Current, &Creator.Owner, &Defaulted);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    Status = RtlGetGroupSecurityDescriptor(Current, &Creator.Group, &Defaulted);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    Creator.Control = Control & ~SE_SELF_RELATIVE;
    if (Information & DACL_SECURITY_INFORMATION)
    {
        Status = RtlGetDaclSecurityDescriptor(Current, &Present, &Creator.Dacl, &Defaulted);
        if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
        Flags |= SEF_DACL_AUTO_INHERIT;
    }
    else Creator.Control = (Creator.Control & ~DaclControl) | SE_DACL_PROTECTED;
    if (Information & SACL_SECURITY_INFORMATION)
    {
        Status = RtlGetSaclSecurityDescriptor(Current, &Present, &Creator.Sacl, &Defaulted);
        if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
        Flags |= SEF_SACL_AUTO_INHERIT;
    }
    else Creator.Control = (Creator.Control & ~SaclControl) | SE_SACL_PROTECTED;
    Creator.Sbz1 = ((PISECURITY_DESCRIPTOR)Current)->Sbz1;
    if ((Information & DACL_SECURITY_INFORMATION) && !(Control & SE_DACL_AUTO_INHERITED) &&
        (Control & SE_DACL_PRESENT) && Creator.Dacl && Creator.Dacl->AceCount)
    {
        if (!ConvertToAutoInheritPrivateObjectSecurity(OldParent, &Creator, &Converted,
                                                       NULL, TRUE, &Mapping))
            return GetLastError();
        Status = RtlGetControlSecurityDescriptor(Converted, &Control, &Revision);
        if (!NT_SUCCESS(Status)) goto Failed;
        Status = RtlGetDaclSecurityDescriptor(Converted, &Present, &Creator.Dacl, &Defaulted);
        if (!NT_SUCCESS(Status)) goto Failed;
        Creator.Control = (Creator.Control & ~DaclControl) | (Control & DaclControl);
    }
    Status = RtlNewSecurityObjectEx(NewParent, &Creator, &Inherited, NULL, TRUE,
                                    Flags | SEF_AVOID_OWNER_CHECK |
                                    SEF_AVOID_PRIVILEGE_CHECK | SEF_AVOID_OWNER_RESTRICTION,
                                    NULL, &Mapping);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = RtlCreateSecurityDescriptor(&Published, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = RtlGetControlSecurityDescriptor(Inherited, &Control, &Revision);
    if (!NT_SUCCESS(Status)) goto Failed;
    Published.Control = Control & ~SE_SELF_RELATIVE;
    if (Information & DACL_SECURITY_INFORMATION)
        Published.Control |= SE_DACL_AUTO_INHERIT_REQ;
    Published.Sbz1 = ((PISECURITY_DESCRIPTOR)Inherited)->Sbz1;
    if (Information & DACL_SECURITY_INFORMATION)
    {
        Status = RtlGetDaclSecurityDescriptor(Inherited, &Present, &Published.Dacl, &Defaulted);
        if (!NT_SUCCESS(Status)) goto Failed;
    }
    if (Information & SACL_SECURITY_INFORMATION)
    {
        Status = RtlGetSaclSecurityDescriptor(Inherited, &Present, &Published.Sacl, &Defaulted);
        if (!NT_SUCCESS(Status)) goto Failed;
        if (Published.Sacl && !Published.Sacl->AceCount) Published.Sacl = NULL;
    }
    Ret = RegSetKeySecurity(Key, Information, &Published);
    if (Ret != ERROR_SUCCESS) goto Cleanup;
    if (Control & SE_DACL_PROTECTED) Information &= ~DACL_SECURITY_INFORMATION;
    if (Control & SE_SACL_PROTECTED) Information &= ~SACL_SECURITY_INFORMATION;
    *Propagate = Information;
    goto Cleanup;

Failed:
    Ret = RtlNtStatusToDosError(Status);
Cleanup:
    if (Inherited) RtlDeleteSecurityObject(&Inherited);
    if (Converted) DestroyPrivateObjectSecurity(&Converted);
    return Ret;
}

static DWORD
AccpOpenRegistryEnumeration(HKEY Key,
                            PHKEY Enumeration)
{
    *Enumeration = NULL;
    return RegOpenKeyExW(Key, L"", REG_OPTION_OPEN_LINK,
                         READ_CONTROL | KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, Enumeration);
}

typedef struct _ACCP_SECURITY_KEY
{
    struct _ACCP_SECURITY_KEY *Previous;
    HKEY Handle;
    PSECURITY_DESCRIPTOR OldDescriptor;
    PSECURITY_DESCRIPTOR NewDescriptor;
    DWORD Index;
    SECURITY_INFORMATION Information;
} ACCP_SECURITY_KEY, *PACCP_SECURITY_KEY;

static DWORD
AccpPropagateRegistrySecurity(HKEY KeyHandle,
                         PSECURITY_DESCRIPTOR OldDescriptor,
                         PSECURITY_DESCRIPTOR NewDescriptor,
                         SECURITY_INFORMATION Information)
{
    ACCP_SECURITY_KEY Root = {NULL, KeyHandle, OldDescriptor, NewDescriptor, 0, Information};
    PACCP_SECURITY_KEY Key = &Root, Next;
    PSECURITY_DESCRIPTOR OldChild = NULL, NewChild = NULL;
    HKEY ChildHandle = NULL, EnumerationHandle = NULL;
    SECURITY_DESCRIPTOR_CONTROL Control;
    WCHAR *Name, *ExpandedName;
    DWORD Capacity = 256, Length, Index, Revision, Access, Ret = ERROR_SUCCESS;
    SECURITY_INFORMATION Propagate;
    NTSTATUS Status;

    Name = HeapAlloc(GetProcessHeap(), 0, Capacity * sizeof(WCHAR));
    if (!Name) return ERROR_NOT_ENOUGH_MEMORY;
    while (Key)
    {
        Length = Capacity;
        Ret = RegEnumKeyExW(Key->Handle, Key->Index, Name, &Length, NULL, NULL, NULL, NULL);
        if (Ret == ERROR_NO_MORE_ITEMS)
        {
            Ret = ERROR_SUCCESS;
            if (Key == &Root) break;
            Next = Key->Previous;
            RegCloseKey(Key->Handle);
            HeapFree(GetProcessHeap(), 0, Key->OldDescriptor);
            HeapFree(GetProcessHeap(), 0, Key->NewDescriptor);
            HeapFree(GetProcessHeap(), 0, Key);
            Key = Next;
            continue;
        }
        if (Ret == ERROR_MORE_DATA)
        {
            if (Capacity > MAXDWORD / 2 ||
                (SIZE_T)Capacity * 2 > (SIZE_T)-1 / sizeof(WCHAR))
            {
                Ret = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            ExpandedName = HeapReAlloc(GetProcessHeap(), 0, Name, (SIZE_T)Capacity * 2 * sizeof(WCHAR));
            if (!ExpandedName)
            {
                Ret = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            Name = ExpandedName;
            Capacity *= 2;
            continue;
        }
        if (Ret != ERROR_SUCCESS) break;
        if (!Length || Length >= Capacity || Name[Length] || Key->Index == MAXDWORD)
        {
            Ret = ERROR_INVALID_DATA;
            break;
        }
        for (Index = 0; Index < Length; ++Index)
            if (!Name[Index] || Name[Index] == L'\\') break;
        if (Index != Length)
        {
            Ret = ERROR_INVALID_DATA;
            break;
        }
        ++Key->Index;
        SetSecurityAccessMask(Key->Information, &Access);
        Ret = RegOpenKeyExW(Key->Handle, Name, REG_OPTION_OPEN_LINK, READ_CONTROL | Access,
                            &ChildHandle);
        if (Ret == ERROR_ACCESS_DENIED) continue;
        if (Ret != ERROR_SUCCESS) break;
        Ret = AccpQueryRegistrySecurity(ChildHandle, Key->Information, &OldChild);
        if (Ret != ERROR_SUCCESS) break;
        Status = RtlGetControlSecurityDescriptor(OldChild, &Control, &Revision);
        if (!NT_SUCCESS(Status))
        {
            Ret = RtlNtStatusToDosError(Status);
            break;
        }
        if (((Key->Information & DACL_SECURITY_INFORMATION) && !(Control & SE_DACL_PROTECTED)) ||
            ((Key->Information & SACL_SECURITY_INFORMATION) && !(Control & SE_SACL_PROTECTED)))
        {
            Ret = AccpOpenRegistryEnumeration(ChildHandle, &EnumerationHandle);
            if (Ret != ERROR_SUCCESS && Ret != ERROR_ACCESS_DENIED) break;
        }
        Ret = AccpInheritRegistrySecurity(ChildHandle, Key->OldDescriptor, Key->NewDescriptor,
                                         OldChild, Key->Information, &Propagate);
        if (Ret != ERROR_SUCCESS) break;
        if (Propagate && EnumerationHandle)
        {
            Ret = AccpQueryRegistrySecurity(ChildHandle, Propagate, &NewChild);
            if (Ret != ERROR_SUCCESS) break;
            Next = HeapAlloc(GetProcessHeap(), 0, sizeof(*Next));
            if (!Next)
            {
                Ret = ERROR_NOT_ENOUGH_MEMORY;
                break;
            }
            Next->Previous = Key;
            Next->Handle = EnumerationHandle;
            Next->OldDescriptor = OldChild;
            Next->NewDescriptor = NewChild;
            Next->Index = 0;
            Next->Information = Propagate;
            Key = Next;
            EnumerationHandle = NULL;
            OldChild = NewChild = NULL;
        }
        RegCloseKey(ChildHandle);
        ChildHandle = NULL;
        if (EnumerationHandle) RegCloseKey(EnumerationHandle);
        EnumerationHandle = NULL;
        HeapFree(GetProcessHeap(), 0, OldChild);
        OldChild = NULL;
    }
    if (ChildHandle) RegCloseKey(ChildHandle);
    if (EnumerationHandle) RegCloseKey(EnumerationHandle);
    HeapFree(GetProcessHeap(), 0, OldChild);
    HeapFree(GetProcessHeap(), 0, NewChild);
    while (Key && Key != &Root)
    {
        Next = Key->Previous;
        RegCloseKey(Key->Handle);
        HeapFree(GetProcessHeap(), 0, Key->OldDescriptor);
        HeapFree(GetProcessHeap(), 0, Key->NewDescriptor);
        HeapFree(GetProcessHeap(), 0, Key);
        Key = Next;
    }
    HeapFree(GetProcessHeap(), 0, Name);
    return Ret;
}

static DWORD
AccpSetAndPropagateRegistrySecurity(HKEY Key,
                                   SECURITY_INFORMATION Information,
                                   PSECURITY_DESCRIPTOR Descriptor)
{
    PSECURITY_DESCRIPTOR OldDescriptor = NULL, NewDescriptor = NULL;
    HKEY SecurityHandle = NULL, QueryHandle = NULL, EnumerationHandle = NULL;
    SECURITY_DESCRIPTOR Published;
    SECURITY_DESCRIPTOR_CONTROL Control;
    BOOLEAN Present, Defaulted;
    ULONG Revision;
    NTSTATUS Status;
    DWORD Ret, Access;
    SECURITY_INFORMATION AclInformation = Information & (DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION);

    if (!AclInformation)
        return RegSetKeySecurity(Key, Information, Descriptor);
    SetSecurityAccessMask(Information, &Access);
    Ret = RegOpenKeyExW(Key, L"", REG_OPTION_OPEN_LINK, Access, &SecurityHandle);
    if (Ret != ERROR_SUCCESS) return Ret;
    Access = READ_CONTROL;
    if (AclInformation & SACL_SECURITY_INFORMATION) Access |= ACCESS_SYSTEM_SECURITY;
    Ret = RegOpenKeyExW(Key, L"", REG_OPTION_OPEN_LINK, Access, &QueryHandle);
    if (Ret == ERROR_ACCESS_DENIED)
    {
        Ret = RegSetKeySecurity(SecurityHandle, Information, Descriptor);
        goto Cleanup;
    }
    if (Ret != ERROR_SUCCESS) goto Cleanup;
    Ret = AccpQueryRegistrySecurity(QueryHandle, AclInformation, &OldDescriptor);
    if (Ret != ERROR_SUCCESS) goto Cleanup;
    Ret = AccpOpenRegistryEnumeration(Key, &EnumerationHandle);
    if (Ret != ERROR_SUCCESS && Ret != ERROR_ACCESS_DENIED) goto Cleanup;
    Status = RtlCreateSecurityDescriptor(&Published, SECURITY_DESCRIPTOR_REVISION);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = RtlGetControlSecurityDescriptor(Descriptor, &Control, &Revision);
    if (!NT_SUCCESS(Status)) goto Failed;
    Published.Control = Control & ~SE_SELF_RELATIVE;
    if (AclInformation & DACL_SECURITY_INFORMATION)
        Published.Control |= SE_DACL_AUTO_INHERIT_REQ | SE_DACL_AUTO_INHERITED;
    if (AclInformation & SACL_SECURITY_INFORMATION)
        Published.Control = (Published.Control & ~SE_SACL_AUTO_INHERIT_REQ) | SE_SACL_AUTO_INHERITED;
    Published.Sbz1 = ((PISECURITY_DESCRIPTOR)Descriptor)->Sbz1;
    Status = RtlGetOwnerSecurityDescriptor(Descriptor, &Published.Owner, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = RtlGetGroupSecurityDescriptor(Descriptor, &Published.Group, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = RtlGetDaclSecurityDescriptor(Descriptor, &Present, &Published.Dacl, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = RtlGetSaclSecurityDescriptor(Descriptor, &Present, &Published.Sacl, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Failed;
    Ret = RegSetKeySecurity(SecurityHandle, Information, &Published);
    if (Ret != ERROR_SUCCESS) goto Cleanup;
    if (!EnumerationHandle)
    {
        Ret = AccpOpenRegistryEnumeration(Key, &EnumerationHandle);
        if (Ret == ERROR_ACCESS_DENIED)
        {
            Ret = ERROR_SUCCESS;
            goto Cleanup;
        }
        if (Ret != ERROR_SUCCESS) goto Cleanup;
    }
    Ret = AccpQueryRegistrySecurity(QueryHandle, AclInformation, &NewDescriptor);
    if (Ret != ERROR_SUCCESS) goto Cleanup;
    Ret = AccpPropagateRegistrySecurity(EnumerationHandle, OldDescriptor, NewDescriptor, AclInformation);
    goto Cleanup;

Failed:
    Ret = RtlNtStatusToDosError(Status);
Cleanup:
    if (EnumerationHandle) RegCloseKey(EnumerationHandle);
    if (QueryHandle) RegCloseKey(QueryHandle);
    if (SecurityHandle) RegCloseKey(SecurityHandle);
    HeapFree(GetProcessHeap(), 0, OldDescriptor);
    HeapFree(GetProcessHeap(), 0, NewDescriptor);
    return Ret;
}

/**********************************************************************
 * AccRewriteSetHandleRights				EXPORTED
 *
 * @unimplemented
 */
DWORD WINAPI
AccRewriteSetHandleRights(HANDLE handle,
                          SE_OBJECT_TYPE ObjectType,
                          SECURITY_INFORMATION SecurityInfo,
                          PSECURITY_DESCRIPTOR pSecurityDescriptor)
{
    NTSTATUS Status;
    DWORD LastErr;
    DWORD Ret = ERROR_SUCCESS;

    /* save the last error code */
    LastErr = GetLastError();

    /* set the security according to the object type */
    switch (ObjectType)
    {
        case SE_REGISTRY_KEY:
        {
            Ret = AccpSetAndPropagateRegistrySecurity((HKEY)handle,
                                                      SecurityInfo,
                                                      pSecurityDescriptor);
            break;
        }

        case SE_FILE_OBJECT:
            /* FIXME - handle console handles? */
            Ret = AccpSetAndPropagateFileSecurity(handle, SecurityInfo, pSecurityDescriptor);
            break;
        case SE_KERNEL_OBJECT:
        case SE_WMIGUID_OBJECT:
        {
            Status = NtSetSecurityObject(handle,
                                         SecurityInfo,
                                         pSecurityDescriptor);
            if (!NT_SUCCESS(Status))
            {
                Ret = RtlNtStatusToDosError(Status);
            }
            break;
        }

        case SE_SERVICE:
        {
            if (!SetServiceObjectSecurity((SC_HANDLE)handle,
                                          SecurityInfo,
                                          pSecurityDescriptor))
            {
                Ret = GetLastError();
            }
            break;
        }

        case SE_WINDOW_OBJECT:
        {
            if (!SetUserObjectSecurity(handle,
                                       &SecurityInfo,
                                       pSecurityDescriptor))
            {
                Ret = GetLastError();
            }
            break;
        }

        case SE_PRINTER:
        case SE_LMSHARE:
            Ret = ERROR_CALL_NOT_IMPLEMENTED;
            break;

        default:
            Ret = ERROR_INVALID_PARAMETER;
            break;
    }


    /* restore the last error code */
    SetLastError(LastErr);

    return Ret;
}


static DWORD
AccpOpenNamedObject(LPWSTR pObjectName,
                    SE_OBJECT_TYPE ObjectType,
                    SECURITY_INFORMATION SecurityInfo,
                    PHANDLE Handle,
                    PHANDLE Handle2,
                    BOOL Write)
{
    LPWSTR lpPath;
    NTSTATUS Status;
    ACCESS_MASK DesiredAccess = (ACCESS_MASK)0;
    DWORD Ret = ERROR_SUCCESS;

    /* determine the required access rights */
    switch (ObjectType)
    {
        case SE_REGISTRY_KEY:
        case SE_FILE_OBJECT:
        case SE_KERNEL_OBJECT:
        case SE_SERVICE:
        case SE_WINDOW_OBJECT:
            if (Write)
            {
                SetSecurityAccessMask(SecurityInfo,
                                      (PDWORD)&DesiredAccess);
                if (ObjectType == SE_FILE_OBJECT &&
                    (SecurityInfo & (DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION)))
                    DesiredAccess |= READ_CONTROL | FILE_READ_ATTRIBUTES;
            }
            else
            {
                QuerySecurityAccessMask(SecurityInfo,
                                        (PDWORD)&DesiredAccess);
            }
            break;

        default:
            break;
    }

    /* make a copy of the path if we're modifying the string */
    switch (ObjectType)
    {
        case SE_REGISTRY_KEY:
        case SE_SERVICE:
            lpPath = (LPWSTR)LocalAlloc(LMEM_FIXED,
                                        (wcslen(pObjectName) + 1) * sizeof(WCHAR));
            if (lpPath == NULL)
            {
                Ret = GetLastError();
                goto Cleanup;
            }

            wcscpy(lpPath,
                   pObjectName);
            break;

        default:
            lpPath = pObjectName;
            break;
    }

    /* open a handle to the path depending on the object type */
    switch (ObjectType)
    {
        case SE_FILE_OBJECT:
        {
            IO_STATUS_BLOCK IoStatusBlock;
            OBJECT_ATTRIBUTES ObjectAttributes;
            UNICODE_STRING FileName;
            ACCESS_MASK OpenAccess = DesiredAccess;

            if (!RtlDosPathNameToNtPathName_U(pObjectName,
                                              &FileName,
                                              NULL,
                                              NULL))
            {
                Ret = ERROR_INVALID_NAME;
                goto Cleanup;
            }

            InitializeObjectAttributes(&ObjectAttributes,
                                       &FileName,
                                       OBJ_CASE_INSENSITIVE,
                                       NULL,
                                       NULL);

            if (!Write && (SecurityInfo & DACL_SECURITY_INFORMATION))
                OpenAccess |= FILE_READ_ATTRIBUTES;
            Status = NtOpenFile(Handle,
                                OpenAccess,
                                &ObjectAttributes,
                                &IoStatusBlock,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                0);

            if (Status == STATUS_ACCESS_DENIED && OpenAccess != DesiredAccess)
                Status = NtOpenFile(Handle, DesiredAccess, &ObjectAttributes, &IoStatusBlock,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, 0);

            RtlFreeHeap(RtlGetProcessHeap(),
                        0,
                        FileName.Buffer);

            if (!NT_SUCCESS(Status))
            {
                Ret = RtlNtStatusToDosError(Status);
            }
            break;
        }

        case SE_REGISTRY_KEY:
        {
            static const struct
            {
                HKEY hRootKey;
                LPCWSTR szRootKey;
            } AccRegRootKeys[] =
            {
                {HKEY_CLASSES_ROOT, L"CLASSES_ROOT"},
                {HKEY_CURRENT_USER, L"CURRENT_USER"},
                {HKEY_LOCAL_MACHINE, L"MACHINE"},
                {HKEY_USERS, L"USERS"},
                {HKEY_CURRENT_CONFIG, L"CONFIG"},
            };
            LPWSTR lpMachineName, lpRootKeyName, lpKeyName;
            HKEY hRootKey = NULL;
            UINT i;

            /* parse the registry path */
            if (lpPath[0] == L'\\' && lpPath[1] == L'\\')
            {
                lpMachineName = lpPath;

                lpRootKeyName = wcschr(lpPath + 2,
                                       L'\\');
                if (lpRootKeyName == NULL)
                    goto ParseRegErr;
                else
                    *(lpRootKeyName++) = L'\0';
            }
            else
            {
                lpMachineName = NULL;
                lpRootKeyName = lpPath;
            }

            lpKeyName = wcschr(lpRootKeyName,
                               L'\\');
            if (lpKeyName != NULL)
            {
                *(lpKeyName++) = L'\0';
            }

            for (i = 0;
                 i != sizeof(AccRegRootKeys) / sizeof(AccRegRootKeys[0]);
                 i++)
            {
                if (!_wcsicmp(lpRootKeyName,
                             AccRegRootKeys[i].szRootKey))
                {
                    hRootKey = AccRegRootKeys[i].hRootKey;
                    break;
                }
            }

            if (hRootKey == NULL)
            {
ParseRegErr:
                /* FIXME - right error code? */
                Ret = ERROR_INVALID_PARAMETER;
                goto Cleanup;
            }

            /* open the registry key */
            if (lpMachineName != NULL)
            {
                Ret = RegConnectRegistry(lpMachineName,
                                         hRootKey,
                                         (PHKEY)Handle2);

                if (Ret != ERROR_SUCCESS)
                    goto Cleanup;

                hRootKey = (HKEY)(*Handle2);
            }

            Ret = RegOpenKeyEx(hRootKey,
                               lpKeyName,
                               0,
                               (REGSAM)DesiredAccess,
                               (PHKEY)Handle);
            if (Ret != ERROR_SUCCESS)
            {
                if (*Handle2 != NULL)
                {
                    RegCloseKey((HKEY)(*Handle2));
                }

                goto Cleanup;
            }
            break;
        }

        case SE_SERVICE:
        {
            LPWSTR lpServiceName, lpMachineName;

            /* parse the service path */
            if (lpPath[0] == L'\\' && lpPath[1] == L'\\')
            {
                DesiredAccess |= SC_MANAGER_CONNECT;

                lpMachineName = lpPath;

                lpServiceName = wcschr(lpPath + 2,
                                       L'\\');
                if (lpServiceName == NULL)
                {
                    /* FIXME - right error code? */
                    Ret = ERROR_INVALID_PARAMETER;
                    goto Cleanup;
                }
                else
                    *(lpServiceName++) = L'\0';
            }
            else
            {
                lpMachineName = NULL;
                lpServiceName = lpPath;
            }

            /* open the service */
            *Handle2 = (HANDLE)OpenSCManager(lpMachineName,
                                             NULL,
                                             (DWORD)DesiredAccess);
            if (*Handle2 == NULL)
            {
                Ret = GetLastError();
                ASSERT(Ret != ERROR_SUCCESS);
                goto Cleanup;
            }

            DesiredAccess &= ~SC_MANAGER_CONNECT;
            *Handle = (HANDLE)OpenService((SC_HANDLE)(*Handle2),
                                          lpServiceName,
                                          (DWORD)DesiredAccess);
            if (*Handle == NULL)
            {
                Ret = GetLastError();
                ASSERT(Ret != ERROR_SUCCESS);
                ASSERT(*Handle2 != NULL);
                CloseServiceHandle((SC_HANDLE)(*Handle2));

                goto Cleanup;
            }
            break;
        }

        default:
        {
            UNIMPLEMENTED;
            Ret = ERROR_CALL_NOT_IMPLEMENTED;
            break;
        }
    }

Cleanup:
    if (lpPath != NULL && lpPath != pObjectName)
    {
        LocalFree((HLOCAL)lpPath);
    }

    return Ret;
}


static VOID
AccpCloseObjectHandle(SE_OBJECT_TYPE ObjectType,
                      HANDLE Handle,
                      HANDLE Handle2)
{
    ASSERT(Handle != NULL);

    /* close allocated handles depending on the object type */
    switch (ObjectType)
    {
        case SE_REGISTRY_KEY:
            RegCloseKey((HKEY)Handle);
            if (Handle2 != NULL)
                RegCloseKey((HKEY)Handle2);
            break;

        case SE_FILE_OBJECT:
            NtClose(Handle);
            break;

        case SE_KERNEL_OBJECT:
        case SE_WINDOW_OBJECT:
            CloseHandle(Handle);
            break;

        case SE_SERVICE:
            CloseServiceHandle((SC_HANDLE)Handle);
            ASSERT(Handle2 != NULL);
            CloseServiceHandle((SC_HANDLE)Handle2);
            break;

        default:
            break;
    }
}


/**********************************************************************
 * AccRewriteGetNamedRights				EXPORTED
 *
 * @unimplemented
 */
DWORD WINAPI
AccRewriteGetNamedRights(LPWSTR pObjectName,
                         SE_OBJECT_TYPE ObjectType,
                         SECURITY_INFORMATION SecurityInfo,
                         PSID* ppsidOwner,
                         PSID* ppsidGroup,
                         PACL* ppDacl,
                         PACL* ppSacl,
                         PSECURITY_DESCRIPTOR* ppSecurityDescriptor)
{
    HANDLE Handle = NULL;
    HANDLE Handle2 = NULL;
    DWORD LastErr;
    DWORD Ret;

    /* save the last error code */
    LastErr = GetLastError();

    /* create the handle */
    Ret = AccpOpenNamedObject(pObjectName,
                              ObjectType,
                              SecurityInfo,
                              &Handle,
                              &Handle2,
                              FALSE);

    if (Ret == ERROR_SUCCESS)
    {
        ASSERT(Handle != NULL);

        /* perform the operation */
        Ret = AccRewriteGetHandleRights(Handle,
                                        ObjectType,
                                        SecurityInfo,
                                        ppsidOwner,
                                        ppsidGroup,
                                        ppDacl,
                                        ppSacl,
                                        ppSecurityDescriptor);

        /* close opened handles */
        AccpCloseObjectHandle(ObjectType,
                              Handle,
                              Handle2);
    }

    /* restore the last error code */
    SetLastError(LastErr);

    return Ret;
}


/**********************************************************************
 * AccRewriteSetNamedRights				EXPORTED
 *
 * @unimplemented
 */
DWORD WINAPI
AccRewriteSetNamedRights(LPWSTR pObjectName,
                         SE_OBJECT_TYPE ObjectType,
                         SECURITY_INFORMATION SecurityInfo,
                         PSECURITY_DESCRIPTOR pSecurityDescriptor)
{
    HANDLE Handle = NULL;
    HANDLE Handle2 = NULL;
    DWORD LastErr;
    DWORD Ret;

    /* save the last error code */
    LastErr = GetLastError();

    /* create the handle */
    Ret = AccpOpenNamedObject(pObjectName,
                              ObjectType,
                              SecurityInfo,
                              &Handle,
                              &Handle2,
                              TRUE);

    if (Ret == ERROR_SUCCESS)
    {
        ASSERT(Handle != NULL);

        /* perform the operation */
        Ret = AccRewriteSetHandleRights(Handle,
                                        ObjectType,
                                        SecurityInfo,
                                        pSecurityDescriptor);

        /* close opened handles */
        AccpCloseObjectHandle(ObjectType,
                              Handle,
                              Handle2);
    }

    /* restore the last error code */
    SetLastError(LastErr);

    return Ret;
}


static BOOL
AccpAppendKeptAces(PACL OldAcl,
                   PACL NewAcl,
                   const BOOLEAN *pKeepAce,
                   DWORD AceCount,
                   PDWORD Index,
                   BOOLEAN StopAtAllowed)
{
    DWORD i;
    PACE_HEADER pAce;

    if (!OldAcl)
        return TRUE;

    for (i = *Index; i < AceCount; *Index = ++i)
    {
        if (!pKeepAce[i])
            continue;
        if (!GetAce(OldAcl, i, (PVOID*)&pAce))
            return FALSE;
        if (StopAtAllowed && AccpGetAceAccessMode(pAce) == GRANT_ACCESS)
            break;
        if (!AddAce(NewAcl, NewAcl->AclRevision, MAXDWORD, pAce, pAce->AceSize))
            return FALSE;
    }
    return TRUE;
}


static DWORD
AccpAppendExplicitAce(PACL Acl, PEXPLICIT_ACCESS_W Entry, PLSA_HANDLE PolicyHandle)
{
    GUID ObjectType, InheritedObjectType;
    GUID *Object = NULL, *InheritedObject = NULL;
    DWORD ObjectsPresent, Ret;
    PSID Sid;
    BOOL Allocated, Success;
    ACCESS_MODE Mode = Entry->grfAccessMode;

    Ret = AccpGetTrusteeSid(&Entry->Trustee, PolicyHandle, &Sid, &Allocated);
    if (Ret != ERROR_SUCCESS) return Ret;
    ObjectsPresent = AccpGetTrusteeObjects(&Entry->Trustee, &ObjectType, &InheritedObjectType);
    if (ObjectsPresent & ACE_OBJECT_TYPE_PRESENT) Object = &ObjectType;
    if (ObjectsPresent & ACE_INHERITED_OBJECT_TYPE_PRESENT) InheritedObject = &InheritedObjectType;
    if (Mode == DENY_ACCESS)
    {
        if (ObjectsPresent)
            Success = AddAccessDeniedObjectAce(Acl, ACL_REVISION_DS, Entry->grfInheritance,
                                                Entry->grfAccessPermissions, Object, InheritedObject, Sid);
        else
            Success = AddAccessDeniedAceEx(Acl, Acl->AclRevision, Entry->grfInheritance,
                                            Entry->grfAccessPermissions, Sid);
    }
    else if (Mode == GRANT_ACCESS || Mode == SET_ACCESS)
    {
        if (ObjectsPresent)
            Success = AddAccessAllowedObjectAce(Acl, ACL_REVISION_DS, Entry->grfInheritance,
                                                 Entry->grfAccessPermissions, Object, InheritedObject, Sid);
        else
            Success = AddAccessAllowedAceEx(Acl, Acl->AclRevision, Entry->grfInheritance,
                                             Entry->grfAccessPermissions, Sid);
    }
    else
    {
        if (ObjectsPresent)
            Success = AddAuditAccessObjectAce(Acl, ACL_REVISION_DS, Entry->grfInheritance,
                                               Entry->grfAccessPermissions, Object, InheritedObject, Sid,
                                               Mode != SET_AUDIT_FAILURE, Mode != SET_AUDIT_SUCCESS);
        else
            Success = AddAuditAccessAceEx(Acl, Acl->AclRevision, Entry->grfInheritance,
                                           Entry->grfAccessPermissions, Sid,
                                           Mode != SET_AUDIT_FAILURE, Mode != SET_AUDIT_SUCCESS);
    }
    Ret = Success ? ERROR_SUCCESS : GetLastError();
    if (Allocated) LocalFree(Sid);
    return Ret;
}


/**********************************************************************
 * AccRewriteSetEntriesInAcl				EXPORTED
 *
 * @implemented
 */
DWORD WINAPI
AccRewriteSetEntriesInAcl(ULONG cCountOfExplicitEntries,
                          PEXPLICIT_ACCESS_W pListOfExplicitEntries,
                          PACL OldAcl,
                          PACL* NewAcl)
{
    PACL pNew = NULL;
    ACL_SIZE_INFORMATION SizeInformation;
    ULONGLONG NewAclSize;
    PACE_HEADER pAce;
    BOOLEAN KeepAceBuf[8];
    BOOLEAN *pKeepAce = NULL;
    DWORD ObjectsPresent, Needed, Offset, Revision = ACL_REVISION;
    BOOL needToClean = FALSE;
    PSID pSid1 = NULL, pSid2;
    ULONG i, j;
    LSA_HANDLE PolicyHandle = NULL;
    DWORD LastErr;
    DWORD Ret = ERROR_SUCCESS;
    DWORD KeptAceIndex = 0;

    /* save the last error code */
    LastErr = GetLastError();

    if (!NewAcl) return ERROR_INVALID_PARAMETER;
    *NewAcl = NULL;
    if (cCountOfExplicitEntries && !pListOfExplicitEntries) return ERROR_INVALID_PARAMETER;

    if (!cCountOfExplicitEntries && !OldAcl)
        goto Cleanup;

    /* Get information about previous ACL */
    if (OldAcl)
    {
        if (!IsValidAcl(OldAcl))
        {
            Ret = ERROR_INVALID_ACL;
            goto Cleanup;
        }
        Revision = max(Revision, OldAcl->AclRevision);
        if (!GetAclInformation(OldAcl, &SizeInformation, sizeof(ACL_SIZE_INFORMATION), AclSizeInformation))
        {
            Ret = GetLastError();
            goto Cleanup;
        }

        if (SizeInformation.AceCount > sizeof(KeepAceBuf) / sizeof(KeepAceBuf[0]))
        {
            pKeepAce = (BOOLEAN *)LocalAlloc(LMEM_FIXED, SizeInformation.AceCount * sizeof(*pKeepAce));
            if (!pKeepAce)
            {
                Ret = ERROR_NOT_ENOUGH_MEMORY;
                goto Cleanup;
            }
        }
        else
            pKeepAce = KeepAceBuf;

        memset(pKeepAce, TRUE, SizeInformation.AceCount * sizeof(*pKeepAce));
    }
    else
    {
        ZeroMemory(&SizeInformation, sizeof(ACL_SIZE_INFORMATION));
        SizeInformation.AclBytesInUse = sizeof(ACL);
    }

    NewAclSize = SizeInformation.AclBytesInUse;

    /* Get size required for new entries */
    for (i = 0; i < cCountOfExplicitEntries; i++)
    {
        Ret = AccpGetTrusteeSid(&pListOfExplicitEntries[i].Trustee,
                                &PolicyHandle,
                                &pSid1,
                                &needToClean);
        if (Ret != ERROR_SUCCESS)
            goto Cleanup;

        ObjectsPresent = AccpGetTrusteeObjects(&pListOfExplicitEntries[i].Trustee,
                                               NULL,
                                               NULL);
        if (!IsValidSid(pSid1))
        {
            Ret = ERROR_INVALID_SID;
            goto Cleanup;
        }
        if (ObjectsPresent & ~(ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT))
        {
            Ret = ERROR_INVALID_PARAMETER;
            goto Cleanup;
        }
        if (ObjectsPresent) Revision = ACL_REVISION_DS;
        Needed = 0;

        switch (pListOfExplicitEntries[i].grfAccessMode)
        {
            case REVOKE_ACCESS:
            case SET_ACCESS:
                /* Discard all accesses for the trustee... */
                for (j = 0; j < SizeInformation.AceCount; j++)
                {
                    if (!pKeepAce[j])
                        continue;
                    if (!GetAce(OldAcl, j, (PVOID*)&pAce))
                    {
                        Ret = GetLastError();
                        goto Cleanup;
                    }

                    if (AccpGetAceAccessMode(pAce) == NOT_USED_ACCESS) continue;
                    if (AccpIsObjectAce(pAce) &&
                        pAce->AceSize < FIELD_OFFSET(ACCESS_ALLOWED_OBJECT_ACE, ObjectType))
                    {
                        Ret = ERROR_INVALID_ACL;
                        goto Cleanup;
                    }
                    Offset = AccpGetAceStructureSize(pAce);
                    if (!Offset || Offset > pAce->AceSize ||
                        pAce->AceSize - Offset < FIELD_OFFSET(SID, SubAuthority))
                    {
                        Ret = ERROR_INVALID_ACL;
                        goto Cleanup;
                    }
                    pSid2 = (PSID)((PBYTE)pAce + Offset);
                    if (GetSidLengthRequired(((SID *)pSid2)->SubAuthorityCount) > pAce->AceSize - Offset ||
                        !IsValidSid(pSid2))
                    {
                        Ret = ERROR_INVALID_ACL;
                        goto Cleanup;
                    }
                    if (pListOfExplicitEntries[i].grfAccessMode == REVOKE_ACCESS &&
                        AccpGetAceAccessMode(pAce) != GRANT_ACCESS &&
                        AccpGetAceAccessMode(pAce) != SET_AUDIT_SUCCESS &&
                        AccpGetAceAccessMode(pAce) != SET_AUDIT_FAILURE)
                        continue;
                    if (RtlEqualSid(pSid1, pSid2))
                    {
                        pKeepAce[j] = FALSE;
                        NewAclSize -= pAce->AceSize;
                    }
                }
                if (pListOfExplicitEntries[i].grfAccessMode == REVOKE_ACCESS)
                    break;
                /* ...and replace by the current access */
            case GRANT_ACCESS:
            case DENY_ACCESS:
                /* Add to ACL */
                Needed = AccpCalcNeededAceSize(pSid1, ObjectsPresent);
                break;
            case SET_AUDIT_SUCCESS:
            case SET_AUDIT_FAILURE:
            case SET_AUDIT_SUCCESS | SET_AUDIT_FAILURE:
                Needed = AccpCalcNeededAceSize(pSid1, ObjectsPresent);
                break;
            default:
                Ret = ERROR_INVALID_PARAMETER;
                goto Cleanup;
        }

        NewAclSize += Needed;
        if (needToClean)
            LocalFree((HLOCAL)pSid1);
        pSid1 = NULL;
        needToClean = FALSE;
    }

    /* Succeed, if no ACL needs to be allocated */
    if (NewAclSize == 0)
        goto Cleanup;

    if (NewAclSize > (MAXUSHORT & ~(sizeof(ULONG) - 1)))
    {
        Ret = ERROR_ALLOTTED_SPACE_EXCEEDED;
        goto Cleanup;
    }
    SizeInformation.AclBytesInUse = (DWORD)((NewAclSize + sizeof(ULONG) - 1) &
                                           ~((ULONGLONG)sizeof(ULONG) - 1));

    /* OK, now create the new ACL */
    DPRINT("Allocating %u bytes for the new ACL\n", SizeInformation.AclBytesInUse);
    pNew = (PACL)LocalAlloc(LMEM_FIXED, SizeInformation.AclBytesInUse);
    if (!pNew)
    {
        Ret = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }
    if (!InitializeAcl(pNew, SizeInformation.AclBytesInUse, Revision))
    {
        Ret = GetLastError();
        goto Cleanup;
    }

    /* Fill it */
    /* 1a) New audit entries (SET_AUDIT_SUCCESS, SET_AUDIT_FAILURE) */
    for (i = 0; i < cCountOfExplicitEntries; i++)
    {
        ACCESS_MODE Mode = pListOfExplicitEntries[i].grfAccessMode;
        if (Mode == SET_AUDIT_SUCCESS || Mode == SET_AUDIT_FAILURE ||
            Mode == (SET_AUDIT_SUCCESS | SET_AUDIT_FAILURE))
        {
            Ret = AccpAppendExplicitAce(pNew, &pListOfExplicitEntries[i], &PolicyHandle);
            if (Ret != ERROR_SUCCESS) goto Cleanup;
        }
    }

    /* 1b) Existing audit entries */

    /* 2a) New denied entries (DENY_ACCESS) */
    for (i = 0; i < cCountOfExplicitEntries; i++)
    {
        if (pListOfExplicitEntries[i].grfAccessMode == DENY_ACCESS)
        {
            Ret = AccpAppendExplicitAce(pNew, &pListOfExplicitEntries[i], &PolicyHandle);
            if (Ret != ERROR_SUCCESS) goto Cleanup;
        }
    }

    /* 2b) Existing denied entries */
    if (!AccpAppendKeptAces(OldAcl, pNew, pKeepAce, SizeInformation.AceCount, &KeptAceIndex, TRUE))
    {
        Ret = GetLastError();
        goto Cleanup;
    }

    /* 3a) New allow entries (GRANT_ACCESS, SET_ACCESS) */
    for (i = 0; i < cCountOfExplicitEntries; i++)
    {
        if (pListOfExplicitEntries[i].grfAccessMode == SET_ACCESS ||
            pListOfExplicitEntries[i].grfAccessMode == GRANT_ACCESS)
        {
            Ret = AccpAppendExplicitAce(pNew, &pListOfExplicitEntries[i], &PolicyHandle);
            if (Ret != ERROR_SUCCESS) goto Cleanup;
        }
    }

    /* 3b) Existing allow entries */
    if (!AccpAppendKeptAces(OldAcl, pNew, pKeepAce, SizeInformation.AceCount, &KeptAceIndex, FALSE))
    {
        Ret = GetLastError();
        goto Cleanup;
    }

    *NewAcl = pNew;

Cleanup:
    if (needToClean) LocalFree((HLOCAL)pSid1);
    if (pKeepAce && pKeepAce != KeepAceBuf)
        LocalFree((HLOCAL)pKeepAce);

    if (pNew && Ret != ERROR_SUCCESS)
        LocalFree((HLOCAL)pNew);

    if (PolicyHandle)
        LsaClose(PolicyHandle);

    /* restore the last error code */
    SetLastError(LastErr);

    return Ret;
}


/**********************************************************************
 * AccGetInheritanceSource				EXPORTED
 *
 * @unimplemented
 */
DWORD WINAPI
AccGetInheritanceSource(LPWSTR pObjectName,
                        SE_OBJECT_TYPE ObjectType,
                        SECURITY_INFORMATION SecurityInfo,
                        BOOL Container,
                        GUID** pObjectClassGuids,
                        DWORD GuidCount,
                        PACL pAcl,
                        PFN_OBJECT_MGR_FUNCTS pfnArray,
                        PGENERIC_MAPPING pGenericMapping,
                        PINHERITED_FROMW pInheritArray)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}


/**********************************************************************
 * AccFreeIndexArray					EXPORTED
 *
 * @implemented
 */
DWORD WINAPI
AccFreeIndexArray(PINHERITED_FROMW pInheritArray,
                  USHORT AceCnt,
                  PFN_OBJECT_MGR_FUNCTS pfnArray  OPTIONAL)
{
    PINHERITED_FROMW pLast;

    UNREFERENCED_PARAMETER(pfnArray);

    pLast = pInheritArray + AceCnt;
    while (pInheritArray != pLast)
    {
        if (pInheritArray->AncestorName != NULL)
        {
            LocalFree((HLOCAL)pInheritArray->AncestorName);
            pInheritArray->AncestorName = NULL;
        }

        pInheritArray++;
    }

    return ERROR_SUCCESS;
}


/**********************************************************************
 * AccRewriteGetExplicitEntriesFromAcl			EXPORTED
 *
 * @implemented
 */
DWORD WINAPI
AccRewriteGetExplicitEntriesFromAcl(PACL pacl,
                                    PULONG pcCountOfExplicitEntries,
                                    PEXPLICIT_ACCESS_W* pListOfExplicitEntries)
{
    PACE_HEADER AceHeader;
    PSID Sid, SidTarget;
    ULONG ObjectAceCount = 0, EntryCount = 0, EntryIndex = 0, ObjectIndex = 0;
    POBJECTS_AND_SID ObjSid;
    SIZE_T Size, SidBytes, SidBytesUsed = 0, EntryBytes, ObjectBytes;
    PEXPLICIT_ACCESS_W peaw;
    DWORD LastErr, SidLen, Offset, Copies, AceCount, AclBytes;
    DWORD AceIndex = 0;
    DWORD ErrorCode = ERROR_SUCCESS;
    BOOL ObjectAce;

    /* save the last error code */
    LastErr = GetLastError();

    if (pacl != NULL)
    {
        AceCount = pacl->AceCount;
        AclBytes = pacl->AclSize;
        if (AceCount != 0)
        {
            Size = 0;

            /* calculate the space needed */
            while (AceIndex < AceCount)
            {
                if (!GetAce(pacl, AceIndex, (LPVOID*)&AceHeader))
                {
                    ErrorCode = GetLastError();
                    goto Cleanup;
                }
                if ((ULONG_PTR)AceHeader < (ULONG_PTR)pacl ||
                    (ULONG_PTR)AceHeader - (ULONG_PTR)pacl > AclBytes ||
                    AclBytes - ((ULONG_PTR)AceHeader - (ULONG_PTR)pacl) < sizeof(ACE_HEADER) ||
                    AceHeader->AceSize < sizeof(ACE_HEADER) ||
                    AceHeader->AceSize > AclBytes - ((ULONG_PTR)AceHeader - (ULONG_PTR)pacl))
                {
                    ErrorCode = ERROR_INVALID_ACL;
                    goto Cleanup;
                }
                ObjectAce = AccpIsObjectAce(AceHeader);
                if (ObjectAce && AceHeader->AceSize < FIELD_OFFSET(ACCESS_ALLOWED_OBJECT_ACE, ObjectType))
                {
                    ErrorCode = ERROR_INVALID_ACL;
                    goto Cleanup;
                }
                Offset = AccpGetAceStructureSize(AceHeader);
                if (!Offset || Offset > AceHeader->AceSize ||
                    AceHeader->AceSize - Offset < FIELD_OFFSET(SID, SubAuthority))
                {
                    ErrorCode = ERROR_INVALID_ACL;
                    goto Cleanup;
                }
                Sid = (PSID)((PBYTE)AceHeader + Offset);
                SidLen = GetSidLengthRequired(((SID *)Sid)->SubAuthorityCount);
                if (SidLen > AceHeader->AceSize - Offset || !IsValidSid(Sid))
                {
                    ErrorCode = ERROR_INVALID_ACL;
                    goto Cleanup;
                }
                Copies = (AceHeader->AceType == SYSTEM_AUDIT_ACE_TYPE ||
                          AceHeader->AceType == SYSTEM_AUDIT_OBJECT_ACE_TYPE) &&
                         (AceHeader->AceFlags & (SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG)) ==
                         (SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG) ? 2 : 1;
                if (EntryCount > MAXDWORD - Copies ||
                    (ObjectAce && ObjectAceCount > MAXDWORD - Copies) ||
                    SidLen > ((SIZE_T)-1 - Size) / Copies)
                {
                    ErrorCode = ERROR_NOT_ENOUGH_MEMORY;
                    goto Cleanup;
                }
                EntryCount += Copies;
                Size += (SIZE_T)SidLen * Copies;

                if (ObjectAce)
                    ObjectAceCount += Copies;

                AceIndex++;
            }

            SidBytes = Size;
            if ((EntryCount && sizeof(EXPLICIT_ACCESS_W) > (SIZE_T)-1 / EntryCount) ||
                (ObjectAceCount && sizeof(OBJECTS_AND_SID) > (SIZE_T)-1 / ObjectAceCount))
            {
                ErrorCode = ERROR_NOT_ENOUGH_MEMORY;
                goto Cleanup;
            }
            EntryBytes = (SIZE_T)EntryCount * sizeof(EXPLICIT_ACCESS_W);
            ObjectBytes = (SIZE_T)ObjectAceCount * sizeof(OBJECTS_AND_SID);
            if (ObjectBytes > (SIZE_T)-1 - Size ||
                EntryBytes > (SIZE_T)-1 - Size - ObjectBytes)
            {
                ErrorCode = ERROR_NOT_ENOUGH_MEMORY;
                goto Cleanup;
            }
            Size += ObjectBytes + EntryBytes;

            ASSERT(AceCount == AceIndex);

            /* allocate the array */
            peaw = (PEXPLICIT_ACCESS_W)LocalAlloc(LMEM_FIXED,
                                                  Size);
            if (peaw != NULL)
            {
                AceIndex = 0;
                ObjSid = (POBJECTS_AND_SID)(peaw + EntryCount);
                SidTarget = (PSID)(ObjSid + ObjectAceCount);

                /* initialize the array */
                while (AceIndex < AceCount)
                {
                    if (!GetAce(pacl, AceIndex, (LPVOID*)&AceHeader))
                    {
                        ErrorCode = GetLastError();
                        goto CompleteEntries;
                    }
                    if ((ULONG_PTR)AceHeader < (ULONG_PTR)pacl ||
                        (ULONG_PTR)AceHeader - (ULONG_PTR)pacl > AclBytes ||
                        AclBytes - ((ULONG_PTR)AceHeader - (ULONG_PTR)pacl) < sizeof(ACE_HEADER) ||
                        AceHeader->AceSize < sizeof(ACE_HEADER) ||
                        AceHeader->AceSize > AclBytes - ((ULONG_PTR)AceHeader - (ULONG_PTR)pacl))
                    {
                        ErrorCode = ERROR_INVALID_ACL;
                        goto CompleteEntries;
                    }
                    ObjectAce = AccpIsObjectAce(AceHeader);
                    if (ObjectAce && AceHeader->AceSize < FIELD_OFFSET(ACCESS_ALLOWED_OBJECT_ACE, ObjectType))
                    {
                        ErrorCode = ERROR_INVALID_ACL;
                        goto CompleteEntries;
                    }
                    Offset = AccpGetAceStructureSize(AceHeader);
                    if (!Offset || Offset > AceHeader->AceSize ||
                        AceHeader->AceSize - Offset < FIELD_OFFSET(SID, SubAuthority))
                    {
                        ErrorCode = ERROR_INVALID_ACL;
                        goto CompleteEntries;
                    }
                    Sid = (PSID)((PBYTE)AceHeader + Offset);
                    SidLen = GetSidLengthRequired(((SID *)Sid)->SubAuthorityCount);
                    if (SidLen > AceHeader->AceSize - Offset || !IsValidSid(Sid))
                    {
                        ErrorCode = ERROR_INVALID_ACL;
                        goto CompleteEntries;
                    }
                    Copies = (AceHeader->AceType == SYSTEM_AUDIT_ACE_TYPE ||
                              AceHeader->AceType == SYSTEM_AUDIT_OBJECT_ACE_TYPE) &&
                             (AceHeader->AceFlags & (SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG)) ==
                             (SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG) ? 2 : 1;
                    if (EntryIndex > EntryCount || Copies > EntryCount - EntryIndex ||
                        (ObjectAce && (ObjectIndex > ObjectAceCount || Copies > ObjectAceCount - ObjectIndex)) ||
                        SidBytesUsed > SidBytes || SidLen > (SidBytes - SidBytesUsed) / Copies)
                    {
                        ErrorCode = ERROR_INVALID_ACL;
                        goto CompleteEntries;
                    }

                    peaw[EntryIndex].grfAccessPermissions = AccpGetAceAccessMask(AceHeader);
                    peaw[EntryIndex].grfAccessMode = Copies == 2 ? SET_AUDIT_SUCCESS : AccpGetAceAccessMode(AceHeader);
                    peaw[EntryIndex].grfInheritance = AceHeader->AceFlags & VALID_INHERIT_FLAGS;

                    if (CopySid(SidLen,
                                SidTarget,
                                Sid))
                    {
                        if (!IsValidSid(SidTarget) || GetLengthSid(SidTarget) != SidLen)
                        {
                            ErrorCode = ERROR_INVALID_ACL;
                            goto CompleteEntries;
                        }
                        if (ObjectAce)
                        {
                            BuildTrusteeWithObjectsAndSid(&peaw[EntryIndex].Trustee,
                                                          ObjSid++,
                                                          AccpGetObjectAceObjectType(AceHeader),
                                                          AccpGetObjectAceInheritedObjectType(AceHeader),
                                                          SidTarget);
                            ObjectIndex++;
                        }
                        else
                        {
                            BuildTrusteeWithSid(&peaw[EntryIndex].Trustee,
                                                SidTarget);
                        }

                        SidTarget = (PSID)((ULONG_PTR)SidTarget + SidLen);
                        SidBytesUsed += SidLen;
                    }
                    else
                    {
                        /* copying the SID failed, treat it as an fatal error... */
                        ErrorCode = GetLastError();

                        /* free allocated resources */
                        LocalFree(peaw);
                        peaw = NULL;
                        EntryIndex = 0;
                        goto CompleteEntries;
                    }

                    EntryIndex++;
                    if (Copies == 2)
                    {
                        peaw[EntryIndex] = peaw[EntryIndex - 1];
                        peaw[EntryIndex].grfAccessMode = SET_AUDIT_FAILURE;
                        if (!CopySid(SidLen, SidTarget, (PSID)((PBYTE)SidTarget - SidLen)))
                        {
                            ErrorCode = GetLastError();
                            goto CompleteEntries;
                        }
                        if (ObjectAce)
                        {
                            *ObjSid = *(ObjSid - 1);
                            ObjSid->pSid = SidTarget;
                            peaw[EntryIndex].Trustee.ptstrName = (LPWSTR)ObjSid++;
                            ObjectIndex++;
                        }
                        else
                        {
                            BuildTrusteeWithSid(&peaw[EntryIndex].Trustee, SidTarget);
                        }
                        SidTarget = (PSID)((ULONG_PTR)SidTarget + SidLen);
                        SidBytesUsed += SidLen;
                        EntryIndex++;
                    }
                    AceIndex++;
                }
                if (EntryIndex != EntryCount || ObjectIndex != ObjectAceCount || SidBytesUsed != SidBytes)
                    ErrorCode = ERROR_INVALID_ACL;

CompleteEntries:
                if (ErrorCode != ERROR_SUCCESS)
                {
                    if (peaw) LocalFree(peaw);
                    peaw = NULL;
                    EntryIndex = 0;
                }
                *pcCountOfExplicitEntries = EntryIndex;
                *pListOfExplicitEntries = peaw;
            }
            else
                ErrorCode = ERROR_NOT_ENOUGH_MEMORY;
        }
        else
        {
            goto EmptyACL;
        }
    }
    else
    {
EmptyACL:
        *pcCountOfExplicitEntries = 0;
        *pListOfExplicitEntries = NULL;
    }

Cleanup:
    /* restore the last error code */
    SetLastError(LastErr);

    return ErrorCode;
}


typedef struct _ACCP_TREE_FRAME
{
    struct _ACCP_TREE_FRAME *Previous;
    HANDLE Handle;
    HANDLE Enumeration;
    PSECURITY_DESCRIPTOR Descriptor;
    WCHAR *Name;
    DWORD Index;
    BOOLEAN Container;
    BOOLEAN Restart;
    BOOLEAN FirstChild;
    BOOLEAN NamesOnly;
} ACCP_TREE_FRAME, *PACCP_TREE_FRAME;

typedef struct _ACCP_TREE_CONTEXT
{
    SE_OBJECT_TYPE Type;
    SECURITY_INFORMATION Information;
    DWORD Action;
    FN_PROGRESSW Progress;
    PROG_INVOKE_SETTING Setting;
    PVOID Args;
    PSID Owner;
    PSID Group;
    PACL Dacl;
    PACL Sacl;
} ACCP_TREE_CONTEXT;

static VOID
AccpTreeNotify(ACCP_TREE_CONTEXT *Context, WCHAR *Name, DWORD Status,
               BOOLEAN SecuritySet)
{
    if (Context->Progress &&
        (Context->Setting == ProgressInvokeEveryObject ||
         Context->Setting == ProgressInvokePrePostError ||
         (Context->Setting == ProgressInvokeOnError && Status != ERROR_SUCCESS)))
        Context->Progress(Name, Status, &Context->Setting, Context->Args, SecuritySet);
}

static DWORD
AccpTreeName(const WCHAR *Parent, const WCHAR *Child, SIZE_T Length, WCHAR **Result)
{
    SIZE_T ParentLength = wcslen(Parent), Separator = ParentLength && Parent[ParentLength - 1] != L'\\';
    SIZE_T Capacity = (SIZE_T)-1 / sizeof(WCHAR);

    *Result = NULL;
    if (ParentLength >= Capacity || Length >= Capacity - ParentLength ||
        Separator >= Capacity - ParentLength - Length)
        return ERROR_NOT_ENOUGH_MEMORY;
    *Result = HeapAlloc(GetProcessHeap(), 0, (ParentLength + Separator + Length + 1) * sizeof(WCHAR));
    if (!*Result) return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(*Result, Parent, ParentLength * sizeof(WCHAR));
    if (Separator) (*Result)[ParentLength++] = L'\\';
    memcpy(*Result + ParentLength, Child, Length * sizeof(WCHAR));
    (*Result)[ParentLength + Length] = 0;
    return ERROR_SUCCESS;
}

static VOID
AccpTreeClose(ACCP_TREE_CONTEXT *Context, HANDLE Handle)
{
    if (!Handle) return;
    if (Context->Type == SE_REGISTRY_KEY) RegCloseKey(Handle);
    else NtClose(Handle);
}

static VOID
AccpTreeFreeFrame(ACCP_TREE_CONTEXT *Context, PACCP_TREE_FRAME Frame)
{
    AccpTreeClose(Context, Frame->Enumeration);
    AccpTreeClose(Context, Frame->Handle);
    if (Frame->Descriptor) RtlDeleteSecurityObject(&Frame->Descriptor);
    HeapFree(GetProcessHeap(), 0, Frame->Name);
    HeapFree(GetProcessHeap(), 0, Frame);
}

static DWORD
AccpTreeQuery(ACCP_TREE_CONTEXT *Context, HANDLE Handle, PSECURITY_DESCRIPTOR *Descriptor)
{
    SECURITY_INFORMATION Information = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
        (Context->Information & (DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION));
    if (Context->Type == SE_REGISTRY_KEY)
        return AccpQueryRegistrySecurity(Handle, Information, Descriptor);
    return RtlNtStatusToDosError(AccpQueryFileSecurity(Handle, Information, Descriptor));
}

static DWORD
AccpTreeMergeDacl(PACL Acl)
{
    PACE_HEADER Ace, Previous;
    ULONG Index, Prior, Offset, Kept, End;
    BOOLEAN Duplicate;

    if (!Acl) return ERROR_SUCCESS;
    if (!RtlValidAcl(Acl)) return ERROR_INVALID_ACL;
    Offset = End = sizeof(ACL);
    Kept = 0;
    for (Index = 0; Index < Acl->AceCount; ++Index)
    {
        Ace = (PACE_HEADER)((BYTE *)Acl + Offset);
        Offset += Ace->AceSize;
        Duplicate = FALSE;
        if (Ace->AceFlags & INHERITED_ACE)
        {
            Previous = (PACE_HEADER)(Acl + 1);
            for (Prior = 0; Prior < Kept; ++Prior)
            {
                if (Previous->AceSize == Ace->AceSize &&
                    (!memcmp(Previous, Ace, Ace->AceSize) ||
                     (Ace->AceType == ACCESS_ALLOWED_ACE_TYPE &&
                      Ace->AceSize >= sizeof(ACE_HEADER) + sizeof(ACCESS_MASK) &&
                      !memcmp(Previous, Ace, sizeof(ACE_HEADER)) &&
                      !(AccpGetAceAccessMask(Ace) & ~AccpGetAceAccessMask(Previous)) &&
                      !memcmp((BYTE *)Previous + sizeof(ACE_HEADER) + sizeof(ACCESS_MASK),
                              (BYTE *)Ace + sizeof(ACE_HEADER) + sizeof(ACCESS_MASK),
                              Ace->AceSize - sizeof(ACE_HEADER) - sizeof(ACCESS_MASK)))))
                {
                    Duplicate = TRUE;
                    break;
                }
                Previous = (PACE_HEADER)((BYTE *)Previous + Previous->AceSize);
            }
        }
        if (!Duplicate)
        {
            ULONG Size = Ace->AceSize;
            memmove((BYTE *)Acl + End, Ace, Size);
            End += Size;
            ++Kept;
        }
    }
    Acl->AceCount = (USHORT)Kept;
    Acl->AclSize = (USHORT)End;
    return ERROR_SUCCESS;
}

static DWORD
AccpTreeApply(ACCP_TREE_CONTEXT *Context, PACCP_TREE_FRAME Frame,
              PSECURITY_DESCRIPTOR Parent, BOOLEAN Root)
{
    GENERIC_MAPPING FileMapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    GENERIC_MAPPING KeyMapping = {KEY_READ, KEY_WRITE, KEY_EXECUTE, KEY_ALL_ACCESS};
    SECURITY_DESCRIPTOR Creator;
    PSECURITY_DESCRIPTOR Current = NULL, NewDescriptor = NULL;
    SECURITY_INFORMATION Information = Context->Information;
    BOOLEAN Present, Defaulted;
    ACL Empty;
    PACL Dacl;
    DWORD Ret;
    ULONG Flags = SEF_AVOID_OWNER_CHECK | SEF_AVOID_PRIVILEGE_CHECK | SEF_AVOID_OWNER_RESTRICTION;
    NTSTATUS Status;

    Ret = AccpTreeQuery(Context, Frame->Handle, &Current);
    if (Ret != ERROR_SUCCESS) return Ret;
    RtlCreateSecurityDescriptor(&Creator, SECURITY_DESCRIPTOR_REVISION);
    RtlCreateAcl(&Empty, sizeof(Empty), ACL_REVISION);
    Status = RtlGetOwnerSecurityDescriptor(Current, &Creator.Owner, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Failed;
    Status = RtlGetGroupSecurityDescriptor(Current, &Creator.Group, &Defaulted);
    if (!NT_SUCCESS(Status)) goto Failed;
    if (Information & OWNER_SECURITY_INFORMATION) Creator.Owner = Context->Owner;
    if (Information & GROUP_SECURITY_INFORMATION) Creator.Group = Context->Group;
    Creator.Control |= SE_DACL_PRESENT | SE_SACL_PRESENT;
    if (Information & DACL_SECURITY_INFORMATION)
    {
        Flags |= SEF_DACL_AUTO_INHERIT;
        if (Root) Creator.Dacl = Context->Dacl;
        else if (Context->Action == TREE_SEC_INFO_RESET) Creator.Dacl = &Empty;
        else
        {
            Status = RtlGetDaclSecurityDescriptor(Current, &Present, &Creator.Dacl, &Defaulted);
            if (!NT_SUCCESS(Status)) goto Failed;
        }
        if (Root && (Information & PROTECTED_DACL_SECURITY_INFORMATION))
            Creator.Control |= SE_DACL_PROTECTED;
        else
        {
            Information &= ~PROTECTED_DACL_SECURITY_INFORMATION;
            Information |= UNPROTECTED_DACL_SECURITY_INFORMATION;
        }
    }
    else Creator.Control |= SE_DACL_PROTECTED;
    if (Information & SACL_SECURITY_INFORMATION)
    {
        Flags |= SEF_SACL_AUTO_INHERIT;
        if (Root) Creator.Sacl = Context->Sacl;
        else if (Context->Action == TREE_SEC_INFO_RESET) Creator.Sacl = &Empty;
        else
        {
            Status = RtlGetSaclSecurityDescriptor(Current, &Present, &Creator.Sacl, &Defaulted);
            if (!NT_SUCCESS(Status)) goto Failed;
        }
        if (Root && (Information & PROTECTED_SACL_SECURITY_INFORMATION))
            Creator.Control |= SE_SACL_PROTECTED;
        else
        {
            Information &= ~PROTECTED_SACL_SECURITY_INFORMATION;
            Information |= UNPROTECTED_SACL_SECURITY_INFORMATION;
        }
    }
    else Creator.Control |= SE_SACL_PROTECTED;
    Status = RtlNewSecurityObjectEx(Parent, &Creator, &NewDescriptor, NULL, Frame->Container,
                                    Flags, NULL, Context->Type == SE_REGISTRY_KEY ? &KeyMapping : &FileMapping);
    if (!NT_SUCCESS(Status)) goto Failed;
    if (Information & DACL_SECURITY_INFORMATION)
    {
        Status = RtlGetDaclSecurityDescriptor(NewDescriptor, &Present, &Dacl, &Defaulted);
        if (!NT_SUCCESS(Status)) goto Failed;
        Ret = AccpTreeMergeDacl(Dacl);
        if (Ret != ERROR_SUCCESS) goto Cleanup;
        Status = RtlSetControlSecurityDescriptor(NewDescriptor, SE_DACL_AUTO_INHERIT_REQ,
                                                SE_DACL_AUTO_INHERIT_REQ);
        if (!NT_SUCCESS(Status)) goto Failed;
    }
    if (Context->Type == SE_REGISTRY_KEY)
        Ret = RegSetKeySecurity(Frame->Handle, Information, NewDescriptor);
    else
        Ret = RtlNtStatusToDosError(NtSetSecurityObject(Frame->Handle, Information, NewDescriptor));
    if (Information & DACL_SECURITY_INFORMATION)
        RtlSetControlSecurityDescriptor(NewDescriptor, SE_DACL_AUTO_INHERIT_REQ, 0);
    if (Ret == ERROR_SUCCESS)
    {
        if (Frame->Descriptor) RtlDeleteSecurityObject(&Frame->Descriptor);
        Frame->Descriptor = NewDescriptor;
        NewDescriptor = NULL;
    }
    goto Cleanup;
Failed:
    Ret = RtlNtStatusToDosError(Status);
Cleanup:
    if (NewDescriptor) RtlDeleteSecurityObject(&NewDescriptor);
    HeapFree(GetProcessHeap(), 0, Current);
    return Ret;
}

static DWORD
AccpTreeFileType(PACCP_TREE_FRAME Frame)
{
    FILE_BASIC_INFORMATION Basic;
    FILE_STANDARD_INFORMATION Standard;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    Status = NtQueryInformationFile(Frame->Handle, &IoStatus, &Basic, sizeof(Basic), FileBasicInformation);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    if (Basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) return ERROR_NOT_SUPPORTED;
    Status = NtQueryInformationFile(Frame->Handle, &IoStatus, &Standard, sizeof(Standard), FileStandardInformation);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    Frame->Container = Standard.Directory;
    return ERROR_SUCCESS;
}

static DWORD
AccpTreeOpenFile(ACCP_TREE_CONTEXT *Context, HANDLE Parent, UNICODE_STRING *Name,
                 PLARGE_INTEGER FileId, PACCP_TREE_FRAME Frame)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    FILE_INTERNAL_INFORMATION Internal;
    UNICODE_STRING Identifier;
    DWORD Access;
    ULONG Options = FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT;
    NTSTATUS Status;

    SetSecurityAccessMask(Context->Information, &Access);
    if (FileId)
    {
        Identifier.Buffer = (WCHAR *)FileId;
        Identifier.Length = Identifier.MaximumLength = sizeof(*FileId);
        Name = &Identifier;
        Options |= FILE_OPEN_BY_FILE_ID;
    }
    InitializeObjectAttributes(&Attributes, Name, OBJ_CASE_INSENSITIVE, Parent, NULL);
    Status = NtOpenFile(&Frame->Handle, Access | READ_CONTROL | FILE_READ_ATTRIBUTES | SYNCHRONIZE,
                        &Attributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        Options);
    if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
    if (FileId)
    {
        Status = NtQueryInformationFile(Frame->Handle, &IoStatus, &Internal, sizeof(Internal),
                                        FileInternalInformation);
        if (!NT_SUCCESS(Status)) return RtlNtStatusToDosError(Status);
        if (IoStatus.Information != sizeof(Internal) || Internal.IndexNumber.QuadPart != FileId->QuadPart)
            return ERROR_INVALID_DATA;
    }
    return AccpTreeFileType(Frame);
}

static DWORD
AccpTreeFileParent(ACCP_TREE_CONTEXT *Context, HANDLE Handle,
                   PSECURITY_DESCRIPTOR *Descriptor)
{
    FILE_NAME_INFORMATION FileName;
    POBJECT_NAME_INFORMATION ObjectName = NULL;
    UNICODE_STRING ParentName;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    HANDLE Parent = NULL;
    ULONG Length = 0, Capacity = 0, Attempt;
    DWORD Access = READ_CONTROL | SYNCHRONIZE, Ret;
    NTSTATUS Status;

    *Descriptor = NULL;
    Status = NtQueryInformationFile(Handle, &IoStatus, &FileName, sizeof(FileName), FileNameInformation);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) return RtlNtStatusToDosError(Status);
    if (IoStatus.Information < FIELD_OFFSET(FILE_NAME_INFORMATION, FileName) + sizeof(WCHAR) ||
        IoStatus.Information > sizeof(FileName) || !FileName.FileNameLength ||
        FileName.FileNameLength % sizeof(WCHAR)) return ERROR_INVALID_DATA;
    if (FileName.FileNameLength == sizeof(WCHAR) && FileName.FileName[0] == L'\\') return ERROR_SUCCESS;
    Status = NtQueryObject(Handle, ObjectNameInformation, NULL, 0, &Length);
    for (Attempt = 0; Attempt < 3 &&
         (Status == STATUS_INFO_LENGTH_MISMATCH || Status == STATUS_BUFFER_TOO_SMALL ||
          Status == STATUS_BUFFER_OVERFLOW); ++Attempt)
    {
        HeapFree(GetProcessHeap(), 0, ObjectName);
        Capacity = Length;
        ObjectName = HeapAlloc(GetProcessHeap(), 0, Capacity);
        if (!ObjectName) return ERROR_NOT_ENOUGH_MEMORY;
        Status = NtQueryObject(Handle, ObjectNameInformation, ObjectName, Capacity, &Length);
    }
    if (!NT_SUCCESS(Status)) goto Failed;
    if (!ObjectName || Capacity < sizeof(*ObjectName) || !ObjectName->Name.Length ||
        ObjectName->Name.Length > ObjectName->Name.MaximumLength ||
        ObjectName->Name.Length % sizeof(WCHAR) ||
        (ULONG_PTR)ObjectName->Name.Buffer < (ULONG_PTR)ObjectName ||
        (ULONG_PTR)ObjectName->Name.Buffer - (ULONG_PTR)ObjectName > Capacity ||
        ObjectName->Name.Length > Capacity - ((ULONG_PTR)ObjectName->Name.Buffer - (ULONG_PTR)ObjectName))
    {
        Ret = ERROR_INVALID_DATA;
        goto Cleanup;
    }
    ParentName = ObjectName->Name;
    while (ParentName.Length && ParentName.Buffer[ParentName.Length / sizeof(WCHAR) - 1] == L'\\')
        ParentName.Length -= sizeof(WCHAR);
    while (ParentName.Length && ParentName.Buffer[ParentName.Length / sizeof(WCHAR) - 1] != L'\\')
        ParentName.Length -= sizeof(WCHAR);
    if (!ParentName.Length)
    {
        Ret = ERROR_INVALID_NAME;
        goto Cleanup;
    }
    ParentName.MaximumLength = ParentName.Length;
    if (Context->Information & SACL_SECURITY_INFORMATION) Access |= ACCESS_SYSTEM_SECURITY;
    InitializeObjectAttributes(&Attributes, &ParentName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenFile(&Parent, Access, &Attributes, &IoStatus,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                        FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT);
    if (!NT_SUCCESS(Status)) goto Failed;
    Ret = AccpTreeQuery(Context, Parent, Descriptor);
    goto Cleanup;
Failed:
    Ret = RtlNtStatusToDosError(Status);
Cleanup:
    if (Parent) NtClose(Parent);
    HeapFree(GetProcessHeap(), 0, ObjectName);
    return Ret;
}

static DWORD
AccpTreeNextFile(PACCP_TREE_FRAME Frame, WCHAR **Name, PLARGE_INTEGER FileId, PBOOLEAN ById)
{
    const ULONG Capacity = 65536;
    PVOID Buffer;
    PFILE_DIRECTORY_INFORMATION Entry;
    PFILE_ID_BOTH_DIR_INFO IdEntry;
    WCHAR *FileName;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    ULONG Index, Offset, Length, NextOffset;
    DWORD Ret;

    *Name = NULL;
    *ById = FALSE;
    Buffer = HeapAlloc(GetProcessHeap(), 0, Capacity);
    if (!Buffer) return ERROR_NOT_ENOUGH_MEMORY;
    for (;;)
    {
        Status = NtQueryDirectoryFile(Frame->Enumeration, NULL, NULL, NULL, &IoStatus,
                                       Buffer, Capacity, Frame->NamesOnly ? FileDirectoryInformation :
                                       FileIdBothDirectoryInformation, TRUE, NULL, Frame->Restart);
        if (!Frame->NamesOnly && Frame->Restart &&
            (Status == STATUS_INVALID_INFO_CLASS || Status == STATUS_NOT_SUPPORTED ||
             Status == STATUS_NOT_IMPLEMENTED))
        {
            Frame->NamesOnly = TRUE;
            continue;
        }
        Frame->Restart = FALSE;
        if (Status == STATUS_NO_MORE_FILES)
        {
            Ret = ERROR_NO_MORE_ITEMS;
            break;
        }
        if (!NT_SUCCESS(Status))
        {
            Ret = RtlNtStatusToDosError(Status);
            break;
        }
        Offset = Frame->NamesOnly ? FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) :
                                   FIELD_OFFSET(FILE_ID_BOTH_DIR_INFO, FileName);
        if (IoStatus.Information < Offset || IoStatus.Information > Capacity)
        {
            Ret = ERROR_INVALID_DATA;
            break;
        }
        if (Frame->NamesOnly)
        {
            Entry = Buffer;
            FileName = Entry->FileName;
            Length = Entry->FileNameLength;
            NextOffset = Entry->NextEntryOffset;
        }
        else
        {
            IdEntry = Buffer;
            FileName = IdEntry->FileName;
            Length = IdEntry->FileNameLength;
            NextOffset = IdEntry->NextEntryOffset;
            *FileId = IdEntry->FileId;
        }
        if (NextOffset || !Length || Length % sizeof(WCHAR) || Length > MAXUSHORT ||
            Length > IoStatus.Information - Offset)
        {
            Ret = ERROR_INVALID_DATA;
            break;
        }
        if ((Length == sizeof(WCHAR) && FileName[0] == L'.') ||
            (Length == 2 * sizeof(WCHAR) && FileName[0] == L'.' && FileName[1] == L'.')) continue;
        Ret = ERROR_SUCCESS;
        for (Index = 0; Index < Length / sizeof(WCHAR); ++Index)
            if (!FileName[Index] || FileName[Index] == L'\\' || FileName[Index] == L'/' || FileName[Index] == L':')
            {
                Ret = ERROR_INVALID_DATA;
                break;
            }
        if (Ret != ERROR_SUCCESS) break;
        *Name = HeapAlloc(GetProcessHeap(), 0, Length + sizeof(WCHAR));
        if (!*Name)
        {
            Ret = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        memcpy(*Name, FileName, Length);
        (*Name)[Length / sizeof(WCHAR)] = 0;
        *ById = !Frame->NamesOnly;
        break;
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
    return Ret;
}

static DWORD
AccpTreeOpenEnumeration(ACCP_TREE_CONTEXT *Context, PACCP_TREE_FRAME Frame)
{
    OBJECT_ATTRIBUTES Attributes;
    UNICODE_STRING Empty = {0};
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    if (!Frame->Container) return ERROR_SUCCESS;
    if (Context->Type == SE_REGISTRY_KEY)
        return AccpOpenRegistryEnumeration(Frame->Handle, (PHKEY)&Frame->Enumeration);
    InitializeObjectAttributes(&Attributes, &Empty, OBJ_CASE_INSENSITIVE, Frame->Handle, NULL);
    Status = NtOpenFile(&Frame->Enumeration, FILE_LIST_DIRECTORY | SYNCHRONIZE,
                        &Attributes, &IoStatus, FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_DIRECTORY_FILE | FILE_OPEN_REPARSE_POINT | FILE_SYNCHRONOUS_IO_NONALERT);
    if (Status == STATUS_SHARING_VIOLATION) return ERROR_ACCESS_DENIED;
    return RtlNtStatusToDosError(Status);
}

static DWORD
AccpTreeNextRegistry(PACCP_TREE_FRAME Frame, WCHAR **Name)
{
    DWORD Capacity = 256, Length, Ret, Index;
    WCHAR *Buffer, *Expanded;

    *Name = NULL;
    Buffer = HeapAlloc(GetProcessHeap(), 0, Capacity * sizeof(WCHAR));
    if (!Buffer) return ERROR_NOT_ENOUGH_MEMORY;
    for (;;)
    {
        Length = Capacity;
        Ret = RegEnumKeyExW(Frame->Enumeration, Frame->Index, Buffer, &Length, NULL, NULL, NULL, NULL);
        if (Ret != ERROR_MORE_DATA) break;
        if (Capacity > MAXDWORD / 2 || (SIZE_T)Capacity * 2 > (SIZE_T)-1 / sizeof(WCHAR))
        {
            Ret = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        Expanded = HeapReAlloc(GetProcessHeap(), 0, Buffer, (SIZE_T)Capacity * 2 * sizeof(WCHAR));
        if (!Expanded)
        {
            Ret = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        Buffer = Expanded;
        Capacity *= 2;
    }
    if (Ret == ERROR_SUCCESS)
    {
        if (!Length || Length >= Capacity || Buffer[Length] || Frame->Index == MAXDWORD)
            Ret = ERROR_INVALID_DATA;
        for (Index = 0; Ret == ERROR_SUCCESS && Index < Length; ++Index)
            if (!Buffer[Index] || Buffer[Index] == L'\\') Ret = ERROR_INVALID_DATA;
        if (Ret == ERROR_SUCCESS)
        {
            ++Frame->Index;
            *Name = Buffer;
            Buffer = NULL;
        }
    }
    HeapFree(GetProcessHeap(), 0, Buffer);
    return Ret;
}

static DWORD
AccpTreeWalk(ACCP_TREE_CONTEXT *Context, PACCP_TREE_FRAME Root)
{
    PACCP_TREE_FRAME Frame = Root, Child = NULL, Previous;
    WCHAR *Name = NULL;
    LARGE_INTEGER FileId;
    BOOLEAN ById;
    DWORD Ret, Access, EnumerationError;

    if (Context->Type == SE_REGISTRY_KEY)
    {
        Ret = AccpTreeOpenEnumeration(Context, Root);
        if (Ret != ERROR_SUCCESS) return Ret;
    }
    while (Frame)
    {
        if (Context->Setting == ProgressCancelOperation)
        {
            Ret = ERROR_ACCESS_DENIED;
            break;
        }
        Ret = Frame->Enumeration ? (Context->Type == SE_REGISTRY_KEY ?
              AccpTreeNextRegistry(Frame, &Name) : AccpTreeNextFile(Frame, &Name, &FileId, &ById)) : ERROR_NO_MORE_ITEMS;
        if (Ret == ERROR_NO_MORE_ITEMS)
        {
            AccpTreeNotify(Context, Frame->Name, ERROR_SUCCESS, TRUE);
            Previous = Frame->Previous;
            if (Frame != Root) AccpTreeFreeFrame(Context, Frame);
            Frame = Previous;
            Ret = ERROR_SUCCESS;
            continue;
        }
        if (Ret != ERROR_SUCCESS) break;
        Child = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Child));
        if (!Child)
        {
            Ret = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }
        Ret = AccpTreeName(Frame->Name, Name, wcslen(Name), &Child->Name);
        if (Ret != ERROR_SUCCESS) break;
        Child->Container = TRUE;
        Child->Restart = TRUE;
        Child->FirstChild = TRUE;
        Child->Previous = Frame;
        SetSecurityAccessMask(Context->Information, &Access);
        if (Context->Type == SE_REGISTRY_KEY)
            Ret = RegOpenKeyExW(Frame->Enumeration, Name, REG_OPTION_OPEN_LINK,
                                Access | READ_CONTROL, (PHKEY)&Child->Handle);
        else
        {
            UNICODE_STRING ChildName;
            ChildName.Buffer = Name;
            ChildName.Length = ChildName.MaximumLength = (USHORT)(wcslen(Name) * sizeof(WCHAR));
            Ret = AccpTreeOpenFile(Context, Frame->Enumeration, &ChildName, ById ? &FileId : NULL, Child);
        }
        HeapFree(GetProcessHeap(), 0, Name);
        Name = NULL;
        if (Ret == ERROR_SUCCESS)
        {
            if (!Frame->FirstChild) AccpTreeNotify(Context, Child->Name, ERROR_SUCCESS, FALSE);
            Frame->FirstChild = FALSE;
            if (Context->Setting == ProgressCancelOperation)
            {
                Ret = ERROR_ACCESS_DENIED;
                break;
            }
            EnumerationError = Context->Type == SE_FILE_OBJECT ?
                               AccpTreeOpenEnumeration(Context, Child) : ERROR_SUCCESS;
            Ret = AccpTreeApply(Context, Child, Frame->Descriptor, FALSE);
            if (Ret == ERROR_SUCCESS && EnumerationError != ERROR_SUCCESS) Ret = EnumerationError;
        }
        if (Ret == ERROR_SUCCESS && Context->Type == SE_REGISTRY_KEY)
            Ret = AccpTreeOpenEnumeration(Context, Child);
        if (Ret != ERROR_SUCCESS)
        {
            AccpTreeNotify(Context, Child->Name, Ret, Child->Descriptor != NULL);
            if (Ret != ERROR_ACCESS_DENIED && Ret != ERROR_SHARING_VIOLATION) break;
            AccpTreeFreeFrame(Context, Child);
            Child = NULL;
            continue;
        }
        Frame = Child;
        Child = NULL;
    }
    HeapFree(GetProcessHeap(), 0, Name);
    if (Child) AccpTreeFreeFrame(Context, Child);
    while (Frame && Frame != Root)
    {
        Previous = Frame->Previous;
        AccpTreeFreeFrame(Context, Frame);
        Frame = Previous;
    }
    return Ret;
}

DWORD WINAPI
TreeSetNamedSecurityInfoW(LPWSTR Name, SE_OBJECT_TYPE ObjectType,
                          SECURITY_INFORMATION Information, PSID Owner, PSID Group,
                          PACL Dacl, PACL Sacl, DWORD Action, FN_PROGRESSW Progress,
                          PROG_INVOKE_SETTING Setting, PVOID Args)
{
    ACCP_TREE_CONTEXT Context = {ObjectType, Information, Action, Progress, Setting,
                                  Args, Owner, Group, Dacl, Sacl};
    ACCP_TREE_CONTEXT OriginalContext = Context;
    PACCP_TREE_FRAME Root = NULL;
    HANDLE QueryHandle = NULL, Machine = NULL, ParentHandle = NULL, ParentMachine = NULL;
    PSECURITY_DESCRIPTOR Parent = NULL;
    WCHAR *ParentName = NULL, *End;
    DWORD Ret, Access, EnumerationError = ERROR_SUCCESS, LastError = GetLastError();
    BOOLEAN RootEnumerationFailure = FALSE;
    SECURITY_INFORMATION Supported = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
        DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION |
        UNPROTECTED_DACL_SECURITY_INFORMATION | PROTECTED_SACL_SECURITY_INFORMATION |
        UNPROTECTED_SACL_SECURITY_INFORMATION;

    if (!Name || !*Name || Action < TREE_SEC_INFO_SET || Action > TREE_SEC_INFO_RESET_KEEP_EXPLICIT ||
        Setting < ProgressInvokeNever || Setting > ProgressInvokePrePostError)
        return ERROR_INVALID_PARAMETER;
    if (ObjectType != SE_REGISTRY_KEY && ObjectType != SE_FILE_OBJECT) return ERROR_NOT_SUPPORTED;
    if (Information & ~Supported) return ERROR_NOT_SUPPORTED;
    if (((Information & OWNER_SECURITY_INFORMATION) && !Owner) ||
        ((Information & GROUP_SECURITY_INFORMATION) && !Group) ||
        ((Information & DACL_SECURITY_INFORMATION) && !Dacl) ||
        ((Information & SACL_SECURITY_INFORMATION) && !Sacl)) return ERROR_INVALID_PARAMETER;
    Root = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Root));
    if (!Root) return ERROR_NOT_ENOUGH_MEMORY;
    Root->Container = TRUE;
    Root->Restart = TRUE;
    Root->FirstChild = TRUE;
    Root->Name = HeapAlloc(GetProcessHeap(), 0, (wcslen(Name) + 1) * sizeof(WCHAR));
    if (!Root->Name) { Ret = ERROR_NOT_ENOUGH_MEMORY; goto Cleanup; }
    wcscpy(Root->Name, Name);
    Ret = AccpOpenNamedObject(Name, ObjectType, OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                              (Information & (DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION)),
                              &QueryHandle, &Machine, FALSE);
    if (Ret != ERROR_SUCCESS) { Machine = NULL; goto Cleanup; }
    SetSecurityAccessMask(Information, &Access);
    if (ObjectType == SE_FILE_OBJECT)
    {
        UNICODE_STRING Empty = {0};
        Ret = AccpTreeOpenFile(&Context, QueryHandle, &Empty, NULL, Root);
        if (Ret != ERROR_SUCCESS) goto Cleanup;
        Ret = AccpTreeFileParent(&Context, Root->Handle, &Parent);
        if (Ret != ERROR_SUCCESS) goto Cleanup;
    }
    else
    {
        Ret = RegOpenKeyExW(QueryHandle, L"", REG_OPTION_OPEN_LINK, Access | READ_CONTROL, (PHKEY)&Root->Handle);
        if (Ret != ERROR_SUCCESS) goto Cleanup;
    }
    ParentName = HeapAlloc(GetProcessHeap(), 0, (wcslen(Name) + 1) * sizeof(WCHAR));
    if (!ParentName) { Ret = ERROR_NOT_ENOUGH_MEMORY; goto Cleanup; }
    wcscpy(ParentName, Name);
    End = wcsrchr(ParentName, L'\\');
    if (End && ObjectType == SE_REGISTRY_KEY)
    {
        *End = 0;
        Ret = AccpOpenNamedObject(ParentName, ObjectType,
                                  OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION |
                                  (Information & (DACL_SECURITY_INFORMATION | SACL_SECURITY_INFORMATION)),
                                  &ParentHandle, &ParentMachine, FALSE);
        if (Ret != ERROR_SUCCESS) { ParentMachine = NULL; goto Cleanup; }
        Ret = AccpTreeQuery(&Context, ParentHandle, &Parent);
        if (Ret != ERROR_SUCCESS) goto Cleanup;
    }
    if (ObjectType == SE_FILE_OBJECT)
        EnumerationError = AccpTreeOpenEnumeration(&Context, Root);
    Ret = AccpTreeApply(&Context, Root, Parent, TRUE);
    if (Ret == ERROR_SUCCESS)
    {
        if (EnumerationError != ERROR_SUCCESS)
        {
            RootEnumerationFailure = TRUE;
            Ret = EnumerationError;
            if (Context.Setting == ProgressInvokeEveryObject || Context.Setting == ProgressInvokePrePostError)
                AccpTreeNotify(&Context, Root->Name, Ret, TRUE);
        }
        else Ret = AccpTreeWalk(&Context, Root);
    }
Cleanup:
    if (Root && Root->Name && Ret != ERROR_SUCCESS)
        AccpTreeNotify(RootEnumerationFailure ? &OriginalContext : &Context,
                        Root->Name, Ret, Root->Descriptor != NULL);
    if (ParentHandle) AccpCloseObjectHandle(ObjectType, ParentHandle, ParentMachine);
    if (QueryHandle) AccpCloseObjectHandle(ObjectType, QueryHandle, Machine);
    if (Root) AccpTreeFreeFrame(&Context, Root);
    HeapFree(GetProcessHeap(), 0, ParentName);
    HeapFree(GetProcessHeap(), 0, Parent);
    SetLastError(LastError);
    return Ret;
}

/**********************************************************************
 * AccTreeResetNamedSecurityInfo			EXPORTED
 *
 * @unimplemented
 */
DWORD WINAPI
AccTreeResetNamedSecurityInfo(LPWSTR pObjectName,
                              SE_OBJECT_TYPE ObjectType,
                              SECURITY_INFORMATION SecurityInfo,
                              PSID pOwner,
                              PSID pGroup,
                              PACL pDacl,
                              PACL pSacl,
                              BOOL KeepExplicit,
                              FN_PROGRESSW fnProgress,
                              PROG_INVOKE_SETTING ProgressInvokeSetting,
                              PVOID Args)
{
    return TreeSetNamedSecurityInfoW(pObjectName, ObjectType, SecurityInfo, pOwner, pGroup,
                                      pDacl, pSacl, KeepExplicit ? TREE_SEC_INFO_RESET_KEEP_EXPLICIT :
                                      TREE_SEC_INFO_RESET, fnProgress, ProgressInvokeSetting, Args);
}


BOOL WINAPI
DllMain(IN HINSTANCE hinstDLL,
        IN DWORD dwReason,
        IN LPVOID lpvReserved)
{
    switch (dwReason)
    {
        case DLL_PROCESS_ATTACH:
            hDllInstance = hinstDLL;
            DisableThreadLibraryCalls(hinstDLL);
            break;

        case DLL_PROCESS_DETACH:
            break;
    }
    return TRUE;
}
