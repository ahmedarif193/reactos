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
    NTSTATUS Status;
    DWORD LastErr;
    DWORD Ret;

    /* save the last error code */
    LastErr = GetLastError();

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
                                               SecurityInfo,
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

static NTSTATUS
AccpSetFileSecurity(HANDLE Handle,
                   SECURITY_INFORMATION Information,
                   PSECURITY_DESCRIPTOR Descriptor)
{
    GENERIC_MAPPING Mapping = {FILE_GENERIC_READ, FILE_GENERIC_WRITE,
                               FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS};
    PSECURITY_DESCRIPTOR Current = NULL, Parent = NULL, Inherited = NULL;
    POBJECT_NAME_INFORMATION Name = NULL;
    SECURITY_DESCRIPTOR Creator, Result;
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

    if (!(Information & DACL_SECURITY_INFORMATION) ||
        (Information & PROTECTED_DACL_SECURITY_INFORMATION))
        return NtSetSecurityObject(Handle, Information, Descriptor);

    Status = RtlGetDaclSecurityDescriptor(Descriptor, &Present, &Dacl, &Defaulted);
    if (!NT_SUCCESS(Status)) return Status;
    if (!Present || !Dacl)
        return NtSetSecurityObject(Handle, Information, Descriptor);

    Status = AccpQueryFileSecurity(Handle, OWNER_SECURITY_INFORMATION |
                                  GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION,
                                  &Current);
    if (!NT_SUCCESS(Status)) goto Cleanup;
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
                     (InheritedControl & DaclControl);
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
            Ret = (DWORD)RegSetKeySecurity((HKEY)handle,
                                           SecurityInfo,
                                           pSecurityDescriptor);
            break;
        }

        case SE_FILE_OBJECT:
            /* FIXME - handle console handles? */
            Status = AccpSetFileSecurity(handle, SecurityInfo, pSecurityDescriptor);
            if (!NT_SUCCESS(Status)) Ret = RtlNtStatusToDosError(Status);
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
                    (SecurityInfo & DACL_SECURITY_INFORMATION))
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

            Status = NtOpenFile(Handle,
                                DesiredAccess,
                                &ObjectAttributes,
                                &IoStatusBlock,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                0);

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
    PACE_HEADER pAce;
    BOOLEAN KeepAceBuf[8];
    BOOLEAN *pKeepAce = NULL;
    GUID ObjectTypeGuid, InheritedObjectTypeGuid;
    DWORD ObjectsPresent;
    BOOL needToClean;
    PSID pSid1, pSid2;
    ULONG i, j;
    LSA_HANDLE PolicyHandle = NULL;
    BOOL bRet;
    DWORD LastErr;
    DWORD Ret = ERROR_SUCCESS;
    DWORD KeptAceIndex = 0;

    /* save the last error code */
    LastErr = GetLastError();

    *NewAcl = NULL;

    if (!cCountOfExplicitEntries && !OldAcl)
        goto Cleanup;

    /* Get information about previous ACL */
    if (OldAcl)
    {
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

                    pSid2 = AccpGetAceSid(pAce);
                    if (pListOfExplicitEntries[i].grfAccessMode == REVOKE_ACCESS &&
                        AccpGetAceAccessMode(pAce) != GRANT_ACCESS &&
                        AccpGetAceAccessMode(pAce) != SET_AUDIT_SUCCESS &&
                        AccpGetAceAccessMode(pAce) != SET_AUDIT_FAILURE)
                        continue;
                    if (RtlEqualSid(pSid1, pSid2))
                    {
                        pKeepAce[j] = FALSE;
                        SizeInformation.AclBytesInUse -= pAce->AceSize;
                    }
                }
                if (pListOfExplicitEntries[i].grfAccessMode == REVOKE_ACCESS)
                    break;
                /* ...and replace by the current access */
            case GRANT_ACCESS:
            case DENY_ACCESS:
                /* Add to ACL */
                SizeInformation.AclBytesInUse += AccpCalcNeededAceSize(pSid1, ObjectsPresent);
                break;
            case SET_AUDIT_SUCCESS:
            case SET_AUDIT_FAILURE:
                /* FIXME */
                DPRINT1("Case not implemented!\n");
                break;
            default:
                DPRINT1("Unknown access mode 0x%x. Ignoring it\n", pListOfExplicitEntries[i].grfAccessMode);
                break;
        }

        if (needToClean)
            LocalFree((HLOCAL)pSid1);
    }

    /* Succeed, if no ACL needs to be allocated */
    if (SizeInformation.AclBytesInUse == 0)
        goto Cleanup;

    /* OK, now create the new ACL */
    DPRINT("Allocating %u bytes for the new ACL\n", SizeInformation.AclBytesInUse);
    pNew = (PACL)LocalAlloc(LMEM_FIXED, SizeInformation.AclBytesInUse);
    if (!pNew)
    {
        Ret = ERROR_NOT_ENOUGH_MEMORY;
        goto Cleanup;
    }
    if (!InitializeAcl(pNew, SizeInformation.AclBytesInUse,
                       (OldAcl && OldAcl->AclRevision > ACL_REVISION) ? OldAcl->AclRevision : ACL_REVISION))
    {
        Ret = GetLastError();
        goto Cleanup;
    }

    /* Fill it */
    /* 1a) New audit entries (SET_AUDIT_SUCCESS, SET_AUDIT_FAILURE) */
    /* FIXME */

    /* 1b) Existing audit entries */

    /* 2a) New denied entries (DENY_ACCESS) */
    for (i = 0; i < cCountOfExplicitEntries; i++)
    {
        if (pListOfExplicitEntries[i].grfAccessMode == DENY_ACCESS)
        {
            /* FIXME: take care of pListOfExplicitEntries[i].grfInheritance */
            Ret = AccpGetTrusteeSid(&pListOfExplicitEntries[i].Trustee,
                                    &PolicyHandle,
                                    &pSid1,
                                    &needToClean);
            if (Ret != ERROR_SUCCESS)
                goto Cleanup;

            ObjectsPresent = AccpGetTrusteeObjects(&pListOfExplicitEntries[i].Trustee,
                                                   &ObjectTypeGuid,
                                                   &InheritedObjectTypeGuid);

            if (ObjectsPresent == 0)
            {
                /* FIXME: Call AddAccessDeniedAceEx instead! */
                bRet = AddAccessDeniedAce(pNew, ACL_REVISION, pListOfExplicitEntries[i].grfAccessPermissions, pSid1);
            }
            else
            {
                /* FIXME: Call AddAccessDeniedObjectAce */
                DPRINT1("Object ACEs not yet supported!\n");
                SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
                bRet = FALSE;
            }

            if (needToClean) LocalFree((HLOCAL)pSid1);
            if (!bRet)
            {
                Ret = GetLastError();
                goto Cleanup;
            }
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
            /* FIXME: take care of pListOfExplicitEntries[i].grfInheritance */
            Ret = AccpGetTrusteeSid(&pListOfExplicitEntries[i].Trustee,
                                    &PolicyHandle,
                                    &pSid1,
                                    &needToClean);
            if (Ret != ERROR_SUCCESS)
                goto Cleanup;

            ObjectsPresent = AccpGetTrusteeObjects(&pListOfExplicitEntries[i].Trustee,
                                                   &ObjectTypeGuid,
                                                   &InheritedObjectTypeGuid);

            if (ObjectsPresent == 0)
            {
                /* FIXME: Call AddAccessAllowedAceEx instead! */
                bRet = AddAccessAllowedAce(pNew, ACL_REVISION, pListOfExplicitEntries[i].grfAccessPermissions, pSid1);
            }
            else
            {
                /* FIXME: Call AddAccessAllowedObjectAce */
                DPRINT1("Object ACEs not yet supported!\n");
                SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
                bRet = FALSE;
            }

            if (needToClean) LocalFree((HLOCAL)pSid1);
            if (!bRet)
            {
                Ret = GetLastError();
                goto Cleanup;
            }
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
    ULONG ObjectAceCount = 0;
    POBJECTS_AND_SID ObjSid;
    SIZE_T Size;
    PEXPLICIT_ACCESS_W peaw;
    DWORD LastErr, SidLen;
    DWORD AceIndex = 0;
    DWORD ErrorCode = ERROR_SUCCESS;

    /* save the last error code */
    LastErr = GetLastError();

    if (pacl != NULL)
    {
        if (pacl->AceCount != 0)
        {
            Size = (SIZE_T)pacl->AceCount * sizeof(EXPLICIT_ACCESS_W);

            /* calculate the space needed */
            while (GetAce(pacl,
                          AceIndex,
                          (LPVOID*)&AceHeader))
            {
                Sid = AccpGetAceSid(AceHeader);
                Size += GetLengthSid(Sid);

                if (AccpIsObjectAce(AceHeader))
                    ObjectAceCount++;

                AceIndex++;
            }

            Size += ObjectAceCount * sizeof(OBJECTS_AND_SID);

            ASSERT(pacl->AceCount == AceIndex);

            /* allocate the array */
            peaw = (PEXPLICIT_ACCESS_W)LocalAlloc(LMEM_FIXED,
                                                  Size);
            if (peaw != NULL)
            {
                AceIndex = 0;
                ObjSid = (POBJECTS_AND_SID)(peaw + pacl->AceCount);
                SidTarget = (PSID)(ObjSid + ObjectAceCount);

                /* initialize the array */
                while (GetAce(pacl,
                              AceIndex,
                              (LPVOID*)&AceHeader))
                {
                    Sid = AccpGetAceSid(AceHeader);
                    SidLen = GetLengthSid(Sid);

                    peaw[AceIndex].grfAccessPermissions = AccpGetAceAccessMask(AceHeader);
                    peaw[AceIndex].grfAccessMode = AccpGetAceAccessMode(AceHeader);
                    peaw[AceIndex].grfInheritance = AceHeader->AceFlags & VALID_INHERIT_FLAGS;

                    if (CopySid(SidLen,
                                SidTarget,
                                Sid))
                    {
                        if (AccpIsObjectAce(AceHeader))
                        {
                            BuildTrusteeWithObjectsAndSid(&peaw[AceIndex].Trustee,
                                                          ObjSid++,
                                                          AccpGetObjectAceObjectType(AceHeader),
                                                          AccpGetObjectAceInheritedObjectType(AceHeader),
                                                          SidTarget);
                        }
                        else
                        {
                            BuildTrusteeWithSid(&peaw[AceIndex].Trustee,
                                                SidTarget);
                        }

                        SidTarget = (PSID)((ULONG_PTR)SidTarget + SidLen);
                    }
                    else
                    {
                        /* copying the SID failed, treat it as an fatal error... */
                        ErrorCode = GetLastError();

                        /* free allocated resources */
                        LocalFree(peaw);
                        peaw = NULL;
                        AceIndex = 0;
                        break;
                    }

                    AceIndex++;
                }

                *pcCountOfExplicitEntries = AceIndex;
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

    /* restore the last error code */
    SetLastError(LastErr);

    return ErrorCode;
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
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
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
