/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS system libraries
 * FILE:            lib/advapi32/sec/ac.c
 * PURPOSE:         ACL/ACE functions
 */

#include <advapi32.h>
#include <lm.h>
WINE_DEFAULT_DEBUG_CHANNEL(advapi);

/* --- ACE --- */

/*
 * @implemented
 */
BOOL
WINAPI
AddAccessAllowedObjectAce(PACL pAcl,
                          DWORD dwAceRevision,
                          DWORD AceFlags,
                          DWORD AccessMask,
                          GUID *ObjectTypeGuid,
                          GUID *InheritedObjectTypeGuid,
                          PSID pSid)
{
    NTSTATUS Status;

    Status = RtlAddAccessAllowedObjectAce(pAcl,
                                          dwAceRevision,
                                          AceFlags,
                                          AccessMask,
                                          ObjectTypeGuid,
                                          InheritedObjectTypeGuid,
                                          pSid);
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
AddAccessDeniedObjectAce(PACL pAcl,
                         DWORD dwAceRevision,
                         DWORD AceFlags,
                         DWORD AccessMask,
                         GUID* ObjectTypeGuid,
                         GUID* InheritedObjectTypeGuid,
                         PSID pSid)
{
    NTSTATUS Status;

    Status = RtlAddAccessDeniedObjectAce(pAcl,
                                         dwAceRevision,
                                         AceFlags,
                                         AccessMask,
                                         ObjectTypeGuid,
                                         InheritedObjectTypeGuid,
                                         pSid);
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
AddAuditAccessObjectAce(PACL pAcl,
                        DWORD dwAceRevision,
                        DWORD AceFlags,
                        DWORD AccessMask,
                        GUID *ObjectTypeGuid,
                        GUID *InheritedObjectTypeGuid,
                        PSID pSid,
                        BOOL bAuditSuccess,
                        BOOL bAuditFailure)
{
    NTSTATUS Status;

    Status = RtlAddAuditAccessObjectAce(pAcl,
                                        dwAceRevision,
                                        AceFlags,
                                        AccessMask,
                                        ObjectTypeGuid,
                                        InheritedObjectTypeGuid,
                                        pSid,
                                        bAuditSuccess,
                                        bAuditFailure);
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
GetInheritanceSourceW(LPWSTR pObjectName,
                      SE_OBJECT_TYPE ObjectType,
                      SECURITY_INFORMATION SecurityInfo,
                      BOOL Container,
                      GUID **pObjectClassGuids  OPTIONAL,
                      DWORD GuidCount,
                      PACL pAcl,
                      PFN_OBJECT_MGR_FUNCTS pfnArray  OPTIONAL,
                      PGENERIC_MAPPING pGenericMapping,
                      PINHERITED_FROMW pInheritArray)
{
    DWORD ErrorCode;

    ErrorCode = CheckNtMartaPresent();
    if (ErrorCode == ERROR_SUCCESS)
    {
        /* call the MARTA provider */
        ErrorCode = AccGetInheritanceSource(pObjectName,
                                            ObjectType,
                                            SecurityInfo,
                                            Container,
                                            pObjectClassGuids,
                                            GuidCount,
                                            pAcl,
                                            pfnArray,
                                            pGenericMapping,
                                            pInheritArray);
    }

    return ErrorCode;
}


/*
 * @unimplemented
 */
DWORD
WINAPI
GetInheritanceSourceA(LPSTR pObjectName,
                      SE_OBJECT_TYPE ObjectType,
                      SECURITY_INFORMATION SecurityInfo,
                      BOOL Container,
                      GUID **pObjectClassGuids  OPTIONAL,
                      DWORD GuidCount,
                      PACL pAcl,
                      PFN_OBJECT_MGR_FUNCTS pfnArray  OPTIONAL,
                      PGENERIC_MAPPING pGenericMapping,
                      PINHERITED_FROMA pInheritArray)
{
    /* That's all this function does, at least up to w2k3... Even MS was too
       lazy to implement it... */
    return ERROR_CALL_NOT_IMPLEMENTED;
}


/*
 * @implemented
 */
DWORD
WINAPI
FreeInheritedFromArray(PINHERITED_FROMW pInheritArray,
                       USHORT AceCnt,
                       PFN_OBJECT_MGR_FUNCTS pfnArray  OPTIONAL)
{
    DWORD ErrorCode;

    ErrorCode = CheckNtMartaPresent();
    if (ErrorCode == ERROR_SUCCESS)
    {
        /* call the MARTA provider */
        ErrorCode = AccFreeIndexArray(pInheritArray,
                                      AceCnt,
                                      pfnArray);
    }

    return ErrorCode;
}


/*
 * @implemented
 */
DWORD
WINAPI
SetEntriesInAclW(ULONG cCountOfExplicitEntries,
                 PEXPLICIT_ACCESS_W pListOfExplicitEntries,
                 PACL OldAcl,
                 PACL *NewAcl)
{
    DWORD ErrorCode;

    if (!NewAcl)
    {
        return ERROR_INVALID_PARAMETER;
    }

    ErrorCode = CheckNtMartaPresent();
    if (ErrorCode == ERROR_SUCCESS)
    {
        /* call the MARTA provider */
        ErrorCode = AccRewriteSetEntriesInAcl(cCountOfExplicitEntries,
                                              pListOfExplicitEntries,
                                              OldAcl,
                                              NewAcl);
    }

    return ErrorCode;
}


DWORD
InternalTrusteeAToW(IN PTRUSTEE_A pTrusteeA,
                    OUT PTRUSTEE_W *pTrusteeW)
{
    TRUSTEE_FORM TrusteeForm;
    INT BufferSize = 0;
    PSTR lpStr;
    DWORD ErrorCode = ERROR_SUCCESS;

    //ASSERT(sizeof(TRUSTEE_W) == sizeof(TRUSTEE_A));

    *pTrusteeW = NULL;

    TrusteeForm = GetTrusteeFormA(pTrusteeA);
    switch (TrusteeForm)
    {
        case TRUSTEE_IS_NAME:
        {
            /* directly copy the array, this works as the size of the EXPLICIT_ACCESS_A
               structure matches the size of the EXPLICIT_ACCESS_W version */
            lpStr = GetTrusteeNameA(pTrusteeA);
            if (lpStr != NULL)
                BufferSize = strlen(lpStr) + 1;

            *pTrusteeW = RtlAllocateHeap(RtlGetProcessHeap(),
                                         0,
                                         sizeof(TRUSTEE_W) + (BufferSize * sizeof(WCHAR)));
            if (*pTrusteeW != NULL)
            {
                RtlCopyMemory(*pTrusteeW,
                              pTrusteeA,
                              FIELD_OFFSET(TRUSTEE_A,
                                           ptstrName));

                if (lpStr != NULL)
                {
                    (*pTrusteeW)->ptstrName = (PWSTR)((*pTrusteeW) + 1);

                    /* convert the trustee's name */
                    if (MultiByteToWideChar(CP_ACP,
                                            0,
                                            lpStr,
                                            -1,
                                            (*pTrusteeW)->ptstrName,
                                            BufferSize) == 0)
                    {
                        goto ConvertErr;
                    }
                }
                else
                {
                    RtlFreeHeap(RtlGetProcessHeap(),
                                0,
                                *pTrusteeW);
                    goto NothingToConvert;
                }
            }
            else
                ErrorCode = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }

        case TRUSTEE_IS_OBJECTS_AND_NAME:
        {
            POBJECTS_AND_NAME_A oanA = (POBJECTS_AND_NAME_A)GetTrusteeNameA(pTrusteeA);
            POBJECTS_AND_NAME_W oan;
            PWSTR StrBuf;

            /* calculate the size needed */
            if ((oanA->ObjectsPresent & ACE_OBJECT_TYPE_PRESENT) &&
                oanA->ObjectTypeName != NULL)
            {
                BufferSize = strlen(oanA->ObjectTypeName) + 1;
            }
            if ((oanA->ObjectsPresent & ACE_INHERITED_OBJECT_TYPE_PRESENT) &&
                oanA->InheritedObjectTypeName != NULL)
            {
                BufferSize += strlen(oanA->InheritedObjectTypeName) + 1;
            }
            if (oanA->ptstrName != NULL)
            {
                BufferSize += strlen(oanA->ptstrName) + 1;
            }

            *pTrusteeW = RtlAllocateHeap(RtlGetProcessHeap(),
                                         0,
                                         sizeof(TRUSTEE_W) + sizeof(OBJECTS_AND_NAME_W) +
                                             (BufferSize * sizeof(WCHAR)));

            if (*pTrusteeW != NULL)
            {
                oan = (POBJECTS_AND_NAME_W)((*pTrusteeW) + 1);
                StrBuf = (PWSTR)(oan + 1);

                /* copy over the parts of the TRUSTEE structure that don't need
                   to be touched */
                RtlCopyMemory(*pTrusteeW,
                              pTrusteeA,
                              FIELD_OFFSET(TRUSTEE_A,
                                           ptstrName));

                (*pTrusteeW)->ptstrName = (LPWSTR)oan;

                /* convert the OBJECTS_AND_NAME_A structure */
                oan->ObjectsPresent = oanA->ObjectsPresent;
                oan->ObjectType = oanA->ObjectType;

                if ((oanA->ObjectsPresent & ACE_OBJECT_TYPE_PRESENT) &&
                    oanA->ObjectTypeName != NULL)
                {
                    BufferSize = strlen(oanA->ObjectTypeName) + 1;

                    if (MultiByteToWideChar(CP_ACP,
                                            0,
                                            oanA->ObjectTypeName,
                                            -1,
                                            StrBuf,
                                            BufferSize) == 0)
                    {
                        goto ConvertErr;
                    }
                    oan->ObjectTypeName = StrBuf;

                    StrBuf += BufferSize;
                }
                else
                    oan->ObjectTypeName = NULL;

                if ((oanA->ObjectsPresent & ACE_INHERITED_OBJECT_TYPE_PRESENT) &&
                    oanA->InheritedObjectTypeName != NULL)
                {
                    /* convert inherited object type name */
                    BufferSize = strlen(oanA->InheritedObjectTypeName) + 1;

                    if (MultiByteToWideChar(CP_ACP,
                                            0,
                                            oanA->InheritedObjectTypeName,
                                            -1,
                                            StrBuf,
                                            BufferSize) == 0)
                    {
                        goto ConvertErr;
                    }
                    oan->InheritedObjectTypeName = StrBuf;

                    StrBuf += BufferSize;
                }
                else
                    oan->InheritedObjectTypeName = NULL;

                if (oanA->ptstrName != NULL)
                {
                    /* convert the trustee name */
                    BufferSize = strlen(oanA->ptstrName) + 1;

                    if (MultiByteToWideChar(CP_ACP,
                                            0,
                                            oanA->ptstrName,
                                            -1,
                                            StrBuf,
                                            BufferSize) == 0)
                    {
                        goto ConvertErr;
                    }
                    oan->ptstrName = StrBuf;
                }
                else
                    oan->ptstrName = NULL;
            }
            else
                ErrorCode = ERROR_NOT_ENOUGH_MEMORY;
            break;
        }

        default:
        {
NothingToConvert:
            /* no need to convert anything to unicode */
            *pTrusteeW = (PTRUSTEE_W)pTrusteeA;
            break;
        }
    }

    return ErrorCode;

ConvertErr:
    ErrorCode = GetLastError();

    /* cleanup */
    RtlFreeHeap(RtlGetProcessHeap(),
                0,
                *pTrusteeW);

    return ErrorCode;
}


VOID
InternalFreeConvertedTrustee(IN PTRUSTEE_W pTrusteeW,
                             IN PTRUSTEE_A pTrusteeA)
{
    if ((PVOID)pTrusteeW != (PVOID)pTrusteeA)
    {
        RtlFreeHeap(RtlGetProcessHeap(),
                    0,
                    pTrusteeW);
    }
}


DWORD
InternalExplicitAccessAToW(IN ULONG cCountOfExplicitEntries,
                           IN PEXPLICIT_ACCESS_A pListOfExplicitEntriesA,
                           OUT PEXPLICIT_ACCESS_W *pListOfExplicitEntriesW)
{
    TRUSTEE_FORM TrusteeForm;
    SIZE_T Size;
    ULONG i;
    ULONG ObjectsAndNameCount = 0;
    PEXPLICIT_ACCESS_W peaw = NULL;
    DWORD ErrorCode = ERROR_SUCCESS;
    LPSTR lpStr;

    /* NOTE: This code assumes that the size of the TRUSTEE_A and TRUSTEE_W structure matches! */
    //ASSERT(sizeof(TRUSTEE_A) == sizeof(TRUSTEE_W));

    *pListOfExplicitEntriesW = NULL;

    if (cCountOfExplicitEntries != 0)
    {
        /* calculate the size needed */
        Size = cCountOfExplicitEntries * sizeof(EXPLICIT_ACCESS_W);
        for (i = 0; i != cCountOfExplicitEntries; i++)
        {
            TrusteeForm = GetTrusteeFormA(&pListOfExplicitEntriesA[i].Trustee);

            switch (TrusteeForm)
            {
                case TRUSTEE_IS_NAME:
                {
                    lpStr = GetTrusteeNameA(&pListOfExplicitEntriesA[i].Trustee);
                    if (lpStr != NULL)
                        Size += (strlen(lpStr) + 1) * sizeof(WCHAR);
                    break;
                }

                case TRUSTEE_IS_OBJECTS_AND_NAME:
                {
                    POBJECTS_AND_NAME_A oan = (POBJECTS_AND_NAME_A)GetTrusteeNameA(&pListOfExplicitEntriesA[i].Trustee);

                    if ((oan->ObjectsPresent & ACE_OBJECT_TYPE_PRESENT) &&
                        oan->ObjectTypeName != NULL)
                    {
                        Size += (strlen(oan->ObjectTypeName) + 1) * sizeof(WCHAR);
                    }

                    if ((oan->ObjectsPresent & ACE_INHERITED_OBJECT_TYPE_PRESENT) &&
                        oan->InheritedObjectTypeName != NULL)
                    {
                        Size += (strlen(oan->InheritedObjectTypeName) + 1) * sizeof(WCHAR);
                    }

                    if (oan->ptstrName != NULL)
                        Size += (strlen(oan->ptstrName) + 1) * sizeof(WCHAR);

                    ObjectsAndNameCount++;
                    break;
                }

                default:
                    break;
            }
        }

        /* allocate the array */
        peaw = RtlAllocateHeap(RtlGetProcessHeap(),
                               0,
                               Size);
        if (peaw != NULL)
        {
            INT BufferSize;
            POBJECTS_AND_NAME_W oan = (POBJECTS_AND_NAME_W)(peaw + cCountOfExplicitEntries);
            LPWSTR StrBuf = (LPWSTR)(oan + ObjectsAndNameCount);

            /* convert the array to unicode */
            for (i = 0; i != cCountOfExplicitEntries; i++)
            {
                peaw[i].grfAccessPermissions = pListOfExplicitEntriesA[i].grfAccessPermissions;
                peaw[i].grfAccessMode = pListOfExplicitEntriesA[i].grfAccessMode;
                peaw[i].grfInheritance = pListOfExplicitEntriesA[i].grfInheritance;

                /* convert or copy the TRUSTEE structure */
                TrusteeForm = GetTrusteeFormA(&pListOfExplicitEntriesA[i].Trustee);
                switch (TrusteeForm)
                {
                    case TRUSTEE_IS_NAME:
                    {
                        lpStr = GetTrusteeNameA(&pListOfExplicitEntriesA[i].Trustee);
                        if (lpStr != NULL)
                        {
                            RtlCopyMemory(&peaw[i].Trustee,
                                          &pListOfExplicitEntriesA[i].Trustee,
                                          FIELD_OFFSET(TRUSTEE_A,
                                                       ptstrName));

                            /* convert the trustee name */
                            BufferSize = strlen(lpStr) + 1;

                            if (MultiByteToWideChar(CP_ACP,
                                                    0,
                                                    lpStr,
                                                    -1,
                                                    StrBuf,
                                                    BufferSize) == 0)
                            {
                                goto ConvertErr;
                            }
                            peaw[i].Trustee.ptstrName = StrBuf;

                            StrBuf += BufferSize;
                        }
                        else
                            goto RawTrusteeCopy;

                        break;
                    }

                    case TRUSTEE_IS_OBJECTS_AND_NAME:
                    {
                        POBJECTS_AND_NAME_A oanA = (POBJECTS_AND_NAME_A)GetTrusteeNameA(&pListOfExplicitEntriesA[i].Trustee);

                        /* copy over the parts of the TRUSTEE structure that don't need
                           to be touched */
                        RtlCopyMemory(&peaw[i].Trustee,
                                      &pListOfExplicitEntriesA[i].Trustee,
                                      FIELD_OFFSET(TRUSTEE_A,
                                                   ptstrName));

                        peaw[i].Trustee.ptstrName = (LPWSTR)oan;

                        /* convert the OBJECTS_AND_NAME_A structure */
                        oan->ObjectsPresent = oanA->ObjectsPresent;
                        oan->ObjectType = oanA->ObjectType;

                        if ((oanA->ObjectsPresent & ACE_OBJECT_TYPE_PRESENT) &&
                            oanA->ObjectTypeName != NULL)
                        {
                            BufferSize = strlen(oanA->ObjectTypeName) + 1;

                            if (MultiByteToWideChar(CP_ACP,
                                                    0,
                                                    oanA->ObjectTypeName,
                                                    -1,
                                                    StrBuf,
                                                    BufferSize) == 0)
                            {
                                goto ConvertErr;
                            }
                            oan->ObjectTypeName = StrBuf;

                            StrBuf += BufferSize;
                        }
                        else
                            oan->ObjectTypeName = NULL;

                        if ((oanA->ObjectsPresent & ACE_INHERITED_OBJECT_TYPE_PRESENT) &&
                            oanA->InheritedObjectTypeName != NULL)
                        {
                            /* convert inherited object type name */
                            BufferSize = strlen(oanA->InheritedObjectTypeName) + 1;

                            if (MultiByteToWideChar(CP_ACP,
                                                    0,
                                                    oanA->InheritedObjectTypeName,
                                                    -1,
                                                    StrBuf,
                                                    BufferSize) == 0)
                            {
                                goto ConvertErr;
                            }
                            oan->InheritedObjectTypeName = StrBuf;

                            StrBuf += BufferSize;
                        }
                        else
                            oan->InheritedObjectTypeName = NULL;

                        if (oanA->ptstrName != NULL)
                        {
                            /* convert the trustee name */
                            BufferSize = strlen(oanA->ptstrName) + 1;

                            if (MultiByteToWideChar(CP_ACP,
                                                    0,
                                                    oanA->ptstrName,
                                                    -1,
                                                    StrBuf,
                                                    BufferSize) == 0)
                            {
                                goto ConvertErr;
                            }
                            oan->ptstrName = StrBuf;

                            StrBuf += BufferSize;
                        }
                        else
                            oan->ptstrName = NULL;

                        /* move on to the next OBJECTS_AND_NAME_A structure */
                        oan++;
                        break;
                    }

                    default:
                    {
RawTrusteeCopy:
                        /* just copy over the TRUSTEE structure, they don't contain any
                           ansi/unicode specific data */
                        RtlCopyMemory(&peaw[i].Trustee,
                                      &pListOfExplicitEntriesA[i].Trustee,
                                      sizeof(TRUSTEE_A));
                        break;
                    }
                }
            }

            ASSERT(ErrorCode == ERROR_SUCCESS);
            *pListOfExplicitEntriesW = peaw;
        }
        else
            ErrorCode = ERROR_NOT_ENOUGH_MEMORY;
    }

    return ErrorCode;

ConvertErr:
    ErrorCode = GetLastError();

    /* cleanup */
    RtlFreeHeap(RtlGetProcessHeap(),
                0,
                peaw);

    return ErrorCode;
}


/*
 * @implemented
 */
DWORD
WINAPI
SetEntriesInAclA(ULONG cCountOfExplicitEntries,
                 PEXPLICIT_ACCESS_A pListOfExplicitEntries,
                 PACL OldAcl,
                 PACL *NewAcl)
{
    PEXPLICIT_ACCESS_W ListOfExplicitEntriesW = NULL;
    DWORD ErrorCode;

    ErrorCode = InternalExplicitAccessAToW(cCountOfExplicitEntries,
                                           pListOfExplicitEntries,
                                           &ListOfExplicitEntriesW);
    if (ErrorCode == ERROR_SUCCESS)
    {
        ErrorCode = SetEntriesInAclW(cCountOfExplicitEntries,
                                     ListOfExplicitEntriesW,
                                     OldAcl,
                                     NewAcl);

        /* free the allocated array */
        RtlFreeHeap(RtlGetProcessHeap(),
                    0,
                    ListOfExplicitEntriesW);
    }

    return ErrorCode;
}


/*
 * @implemented
 */
DWORD
WINAPI
GetExplicitEntriesFromAclW(PACL pacl,
                           PULONG pcCountOfExplicitEntries,
                           PEXPLICIT_ACCESS_W *pListOfExplicitEntries)
{
    DWORD ErrorCode;

    ErrorCode = CheckNtMartaPresent();
    if (ErrorCode == ERROR_SUCCESS)
    {
        /* call the MARTA provider */
        ErrorCode = AccRewriteGetExplicitEntriesFromAcl(pacl,
                                                        pcCountOfExplicitEntries,
                                                        pListOfExplicitEntries);
    }

    return ErrorCode;
}


typedef struct _ACL_RIGHTS_CONTEXT
{
    LSA_HANDLE Policy;
    PSID TrusteeSid;
} ACL_RIGHTS_CONTEXT;

typedef struct _ACL_RIGHTS_SID
{
    struct _ACL_RIGHTS_SID *Next;
    BYTE Sid[ANYSIZE_ARRAY];
} ACL_RIGHTS_SID;

static DWORD
InternalAclCopySid(PSID Sid, PSID *Copy)
{
    DWORD Length;

    *Copy = NULL;
    if (!Sid || !IsValidSid(Sid))
        return ERROR_INVALID_SID;
    Length = GetLengthSid(Sid);
    *Copy = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!*Copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!CopySid(Length, *Copy, Sid) || !IsValidSid(*Copy))
    {
        HeapFree(GetProcessHeap(), 0, *Copy);
        *Copy = NULL;
        return ERROR_INVALID_SID;
    }
    return ERROR_SUCCESS;
}

static DWORD
InternalAclOpenPolicy(ACL_RIGHTS_CONTEXT *Context)
{
    LSA_OBJECT_ATTRIBUTES Attributes = {0};
    NTSTATUS Status;

    if (Context->Policy)
        return ERROR_SUCCESS;
    Status = LsaOpenPolicy(NULL, &Attributes, POLICY_LOOKUP_NAMES, &Context->Policy);
    return LsaNtStatusToWinError(Status);
}

static DWORD
InternalAclResolveName(ACL_RIGHTS_CONTEXT *Context, LPCWSTR Name, PSID *Sid)
{
    PLSA_REFERENCED_DOMAIN_LIST Domains = NULL;
    PLSA_TRANSLATED_SID2 Translated = NULL;
    LSA_UNICODE_STRING String;
    SIZE_T Length;
    PWSTR Copy;
    NTSTATUS Status;
    DWORD Error;

    *Sid = NULL;
    if (!Name)
        return ERROR_INVALID_PARAMETER;
    Length = wcslen(Name);
    if (Length > (USHRT_MAX - sizeof(WCHAR)) / sizeof(WCHAR))
        return ERROR_INVALID_PARAMETER;
    Copy = HeapAlloc(GetProcessHeap(), 0, (Length + 1) * sizeof(WCHAR));
    if (!Copy)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(Copy, Name, Length * sizeof(WCHAR));
    Copy[Length] = UNICODE_NULL;
    Error = InternalAclOpenPolicy(Context);
    if (Error == ERROR_SUCCESS)
    {
        RtlInitUnicodeString(&String, Copy);
        Status = LsaLookupNames2(Context->Policy, 0, 1, &String, &Domains, &Translated);
        Error = LsaNtStatusToWinError(Status);
        if (Status == STATUS_SUCCESS)
            Error = InternalAclCopySid(Translated->Sid, Sid);
    }
    if (Translated)
        LsaFreeMemory(Translated);
    if (Domains)
        LsaFreeMemory(Domains);
    HeapFree(GetProcessHeap(), 0, Copy);
    return Error;
}

static DWORD
InternalAclQueueSid(ACL_RIGHTS_SID **Head, ACL_RIGHTS_SID **Tail, PSID Sid)
{
    ACL_RIGHTS_SID *Entry;
    DWORD Length;

    if (!Sid || !IsValidSid(Sid))
        return ERROR_INVALID_SID;
    for (Entry = *Head; Entry; Entry = Entry->Next)
        if (EqualSid(Entry->Sid, Sid))
            return ERROR_SUCCESS;
    Length = GetLengthSid(Sid);
    Entry = HeapAlloc(GetProcessHeap(), 0, FIELD_OFFSET(ACL_RIGHTS_SID, Sid) + Length);
    if (!Entry)
        return ERROR_NOT_ENOUGH_MEMORY;
    if (!CopySid(Length, Entry->Sid, Sid))
    {
        HeapFree(GetProcessHeap(), 0, Entry);
        return ERROR_INVALID_SID;
    }
    Entry->Next = NULL;
    if (*Tail)
        (*Tail)->Next = Entry;
    else
        *Head = Entry;
    *Tail = Entry;
    return ERROR_SUCCESS;
}

static DWORD
InternalAclGroupMembers(ACL_RIGHTS_CONTEXT *Context,
                        PSID GroupSid,
                        PLSA_TRANSLATED_NAME Group,
                        PLSA_REFERENCED_DOMAIN_LIST Domains,
                        ACL_RIGHTS_SID **Head,
                        ACL_RIGHTS_SID **Tail)
{
    PWSTR Name = NULL, Qualified = NULL;
    PLSA_UNICODE_STRING Domain;
    PSID Resolved = NULL;
    LPBYTE Buffer = NULL;
    DWORD_PTR Resume = 0;
    DWORD Read, Total, Index, Error, Status;
    SIZE_T DomainLength, MemberLength;

    Name = HeapAlloc(GetProcessHeap(), 0, Group->Name.Length + sizeof(WCHAR));
    if (!Name)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(Name, Group->Name.Buffer, Group->Name.Length);
    Name[Group->Name.Length / sizeof(WCHAR)] = UNICODE_NULL;
    Error = InternalAclResolveName(Context, Name, &Resolved);
    if (Error != ERROR_SUCCESS)
        goto Cleanup;
    if (!EqualSid(Resolved, GroupSid))
    {
        Error = ERROR_NONE_MAPPED;
        goto Cleanup;
    }
    HeapFree(GetProcessHeap(), 0, Resolved);
    Resolved = NULL;

    do
    {
        Buffer = NULL;
        Read = Total = 0;
        if (Group->Use == SidTypeAlias)
            Status = NetLocalGroupGetMembers(NULL, Name, 0, &Buffer,
                                            MAX_PREFERRED_LENGTH, &Read, &Total, &Resume);
        else
            Status = NetGroupGetUsers(NULL, Name, 0, &Buffer,
                                     MAX_PREFERRED_LENGTH, &Read, &Total, &Resume);
        if (Status != ERROR_SUCCESS && Status != ERROR_MORE_DATA)
        {
            if (Error == ERROR_SUCCESS)
                Error = Status;
        }
        else if (Error == ERROR_SUCCESS)
        {
            for (Index = 0; Index < Read; ++Index)
            {
                if (Group->Use == SidTypeAlias)
                {
                    Error = InternalAclQueueSid(Head, Tail,
                                                ((PLOCALGROUP_MEMBERS_INFO_0)Buffer)[Index].lgrmi0_sid);
                }
                else
                {
                    if (!Domains || Group->DomainIndex < 0 ||
                        (ULONG)Group->DomainIndex >= Domains->Entries)
                    {
                        Error = ERROR_NONE_MAPPED;
                        break;
                    }
                    Domain = &Domains->Domains[Group->DomainIndex].Name;
                    DomainLength = Domain->Length / sizeof(WCHAR);
                    MemberLength = wcslen(((PGROUP_USERS_INFO_0)Buffer)[Index].grui0_name);
                    if (DomainLength > (USHRT_MAX / sizeof(WCHAR)) - 2 ||
                        MemberLength > (USHRT_MAX / sizeof(WCHAR)) - DomainLength - 2)
                    {
                        Error = ERROR_INVALID_PARAMETER;
                        break;
                    }
                    Qualified = HeapAlloc(GetProcessHeap(), 0,
                                          (DomainLength + MemberLength + 2) * sizeof(WCHAR));
                    if (!Qualified)
                    {
                        Error = ERROR_NOT_ENOUGH_MEMORY;
                        break;
                    }
                    memcpy(Qualified, Domain->Buffer, Domain->Length);
                    Qualified[DomainLength] = L'\\';
                    memcpy(Qualified + DomainLength + 1,
                           ((PGROUP_USERS_INFO_0)Buffer)[Index].grui0_name,
                           (MemberLength + 1) * sizeof(WCHAR));
                    Error = InternalAclResolveName(Context, Qualified, &Resolved);
                    HeapFree(GetProcessHeap(), 0, Qualified);
                    Qualified = NULL;
                    if (Error == ERROR_SUCCESS)
                        Error = InternalAclQueueSid(Head, Tail, Resolved);
                    HeapFree(GetProcessHeap(), 0, Resolved);
                    Resolved = NULL;
                }
                if (Error != ERROR_SUCCESS)
                    break;
            }
        }
        if (Buffer)
            NetApiBufferFree(Buffer);
        Buffer = NULL;
    } while (Status == ERROR_MORE_DATA);

Cleanup:
    HeapFree(GetProcessHeap(), 0, Resolved);
    HeapFree(GetProcessHeap(), 0, Qualified);
    HeapFree(GetProcessHeap(), 0, Name);
    return Error;
}

static DWORD
InternalAclSidMatches(ACL_RIGHTS_CONTEXT *Context, PSID Sid, BOOL *Matches)
{
    SID World = {SID_REVISION, 1, {SECURITY_WORLD_SID_AUTHORITY}, {SECURITY_WORLD_RID}};
    ACL_RIGHTS_SID *Head = NULL, *Tail = NULL, *Entry, *Next;
    PLSA_REFERENCED_DOMAIN_LIST Domains = NULL;
    PLSA_TRANSLATED_NAME Names = NULL;
    PSID Current;
    NTSTATUS Status;
    DWORD Error;

    *Matches = FALSE;
    Error = InternalAclQueueSid(&Head, &Tail, Sid);
    if (Error != ERROR_SUCCESS)
        return Error;
    for (Entry = Head; Entry; Entry = Entry->Next)
    {
        if (EqualSid(Entry->Sid, Context->TrusteeSid) || EqualSid(Entry->Sid, &World))
        {
            *Matches = TRUE;
            break;
        }
        Error = InternalAclOpenPolicy(Context);
        if (Error != ERROR_SUCCESS)
            break;
        Current = Entry->Sid;
        Status = LsaLookupSids(Context->Policy, 1, &Current, &Domains, &Names);
        Error = LsaNtStatusToWinError(Status);
        if (Status == STATUS_SUCCESS &&
            (Names->Use == SidTypeAlias || Names->Use == SidTypeGroup))
            Error = InternalAclGroupMembers(Context, Current, Names, Domains, &Head, &Tail);
        if (Names)
            LsaFreeMemory(Names);
        if (Domains)
            LsaFreeMemory(Domains);
        Names = NULL;
        Domains = NULL;
        if (Error != ERROR_SUCCESS)
            break;
    }
    for (Entry = Head; Entry; Entry = Next)
    {
        Next = Entry->Next;
        HeapFree(GetProcessHeap(), 0, Entry);
    }
    return Error;
}

static DWORD
InternalAclQueryRights(PACL Acl,
                       PTRUSTEE_W Trustee,
                       BOOL Audit,
                       BOOL Ansi,
                       PACCESS_MASK First,
                       PACCESS_MASK Second)
{
    ACL_RIGHTS_CONTEXT Context = {0};
    TRUSTEE_W CapturedTrustee;
    OBJECTS_AND_SID CapturedObjects;
    PACL CapturedAcl = NULL;
    PACE_HEADER Ace;
    PACCESS_ALLOWED_ACE AccessAce;
    PSID Sid;
    PBYTE Cursor, End;
    ACCESS_MASK Allowed = 0, Denied = 0, Successful = 0, Failed = 0, Mask;
    DWORD Length, Index, SidOffset, SidLength, ObjectFlags, Error;
    BYTE AuditFlags;
    BOOL AllowSeen = FALSE, Matches, ObjectAce, ObjectTrustee = FALSE;

    if (!Acl || !Trustee || !First || (Audit && !Second))
        return ERROR_INVALID_PARAMETER;
    CapturedTrustee = *Trustee;
    if (CapturedTrustee.MultipleTrusteeOperation != NO_MULTIPLE_TRUSTEE)
        return ERROR_INVALID_PARAMETER;
    Length = Acl->AclSize;
    if (Length < sizeof(ACL))
        return ERROR_INVALID_ACL;
    CapturedAcl = HeapAlloc(GetProcessHeap(), 0, Length);
    if (!CapturedAcl)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(CapturedAcl, Acl, Length);
    Error = ERROR_INVALID_ACL;
    if (CapturedAcl->AclSize != Length || !IsValidAcl(CapturedAcl))
        goto Cleanup;
    if (CapturedTrustee.TrusteeForm == TRUSTEE_IS_SID)
        Error = InternalAclCopySid((PSID)CapturedTrustee.ptstrName, &Context.TrusteeSid);
    else if (CapturedTrustee.TrusteeForm == TRUSTEE_IS_NAME)
        Error = InternalAclResolveName(&Context, CapturedTrustee.ptstrName, &Context.TrusteeSid);
    else if (CapturedTrustee.TrusteeForm == TRUSTEE_IS_OBJECTS_AND_SID)
    {
        Error = ERROR_INVALID_PARAMETER;
        if (!CapturedTrustee.ptstrName)
            goto Cleanup;
        CapturedObjects = *(POBJECTS_AND_SID)CapturedTrustee.ptstrName;
        if (CapturedObjects.ObjectsPresent & ~(ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT))
            goto Cleanup;
        if (Ansi && !CapturedObjects.ObjectsPresent)
        {
            Error = ERROR_INVALID_SID;
            goto Cleanup;
        }
        ObjectTrustee = CapturedObjects.ObjectsPresent != 0;
        Error = InternalAclCopySid(CapturedObjects.pSid, &Context.TrusteeSid);
    }
    else
        Error = ERROR_INVALID_PARAMETER;
    if (Error != ERROR_SUCCESS)
        goto Cleanup;

    Cursor = (PBYTE)(CapturedAcl + 1);
    End = (PBYTE)CapturedAcl + Length;
    for (Index = 0; Index < CapturedAcl->AceCount; ++Index)
    {
        Error = ERROR_INVALID_ACL;
        if ((SIZE_T)(End - Cursor) < sizeof(ACE_HEADER))
            goto Cleanup;
        Ace = (PACE_HEADER)Cursor;
        if (Ace->AceSize < sizeof(ACE_HEADER) || Ace->AceSize > (SIZE_T)(End - Cursor))
            goto Cleanup;
        ObjectAce = Ace->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE ||
                    Ace->AceType == ACCESS_DENIED_OBJECT_ACE_TYPE ||
                    Ace->AceType == SYSTEM_AUDIT_OBJECT_ACE_TYPE;
        ObjectFlags = 0;
        SidOffset = FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart);
        if (ObjectAce)
        {
            SidOffset = FIELD_OFFSET(ACCESS_ALLOWED_OBJECT_ACE, ObjectType);
            if (Ace->AceSize < SidOffset)
                goto Cleanup;
            ObjectFlags = ((PACCESS_ALLOWED_OBJECT_ACE)Ace)->Flags;
            if (ObjectFlags & ~(ACE_OBJECT_TYPE_PRESENT | ACE_INHERITED_OBJECT_TYPE_PRESENT))
                goto Cleanup;
            if (ObjectFlags & ACE_OBJECT_TYPE_PRESENT)
                SidOffset += sizeof(GUID);
            if (ObjectFlags & ACE_INHERITED_OBJECT_TYPE_PRESENT)
                SidOffset += sizeof(GUID);
        }
        if (Ace->AceSize < SidOffset + FIELD_OFFSET(SID, SubAuthority))
            goto Cleanup;
        if (Audit)
        {
            if (Ace->AceType != SYSTEM_AUDIT_ACE_TYPE && Ace->AceType != SYSTEM_AUDIT_OBJECT_ACE_TYPE)
                goto Cleanup;
        }
        else if (Ace->AceType == ACCESS_ALLOWED_ACE_TYPE || Ace->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE)
            AllowSeen = TRUE;
        else if ((Ace->AceType != ACCESS_DENIED_ACE_TYPE && Ace->AceType != ACCESS_DENIED_OBJECT_ACE_TYPE) || AllowSeen)
            goto Cleanup;
        AccessAce = (PACCESS_ALLOWED_ACE)Ace;
        Sid = Cursor + SidOffset;
        SidLength = GetSidLengthRequired(((PISID)Sid)->SubAuthorityCount);
        if (SidLength > Ace->AceSize - SidOffset || !IsValidSid(Sid))
            goto Cleanup;
        if (ObjectFlags & ACE_OBJECT_TYPE_PRESENT)
        {
            if (Audit || Ace->AceType == ACCESS_ALLOWED_OBJECT_ACE_TYPE)
            {
                Error = Audit ? ERROR_INVALID_PARAMETER : ERROR_UNKNOWN_PROPERTY;
                goto Cleanup;
            }
            Cursor += Ace->AceSize;
            Error = ERROR_SUCCESS;
            continue;
        }
        if (!Audit && (Ace->AceFlags & INHERIT_ONLY_ACE))
        {
            Cursor += Ace->AceSize;
            Error = ERROR_SUCCESS;
            continue;
        }
        if (ObjectTrustee)
        {
            Error = ERROR_NONE_MAPPED;
            goto Cleanup;
        }
        Error = InternalAclSidMatches(&Context, Sid, &Matches);
        if (Error != ERROR_SUCCESS)
            goto Cleanup;
        if (Matches)
        {
            Mask = AccessAce->Mask;
            if (Audit)
            {
                AuditFlags = Ace->AceFlags & (SUCCESSFUL_ACCESS_ACE_FLAG | FAILED_ACCESS_ACE_FLAG);
                if (AuditFlags == SUCCESSFUL_ACCESS_ACE_FLAG)
                    Successful |= Mask;
                else if (AuditFlags == FAILED_ACCESS_ACE_FLAG)
                    Failed |= Mask;
            }
            else
            {
                Mask &= ~(GENERIC_READ | GENERIC_WRITE | GENERIC_EXECUTE | GENERIC_ALL);
                if (Ace->AceType == ACCESS_DENIED_ACE_TYPE || Ace->AceType == ACCESS_DENIED_OBJECT_ACE_TYPE)
                    Denied |= Mask;
                else
                    Allowed |= Mask;
            }
        }
        Cursor += Ace->AceSize;
    }
    if (Audit)
    {
        *First = Successful;
        *Second = Failed;
    }
    else
        *First = Allowed & ~Denied;
    Error = ERROR_SUCCESS;

Cleanup:
    if (Context.Policy)
        LsaClose(Context.Policy);
    HeapFree(GetProcessHeap(), 0, Context.TrusteeSid);
    HeapFree(GetProcessHeap(), 0, CapturedAcl);
    return Error;
}


/*
 * @unimplemented
 */
DWORD
WINAPI
GetEffectiveRightsFromAclW(IN PACL pacl,
                           IN PTRUSTEE_W pTrustee,
                           OUT PACCESS_MASK pAccessRights)
{
    return InternalAclQueryRights(pacl, pTrustee, FALSE, FALSE, pAccessRights, NULL);
}


/*
 * @implemented
 */
DWORD
WINAPI
GetEffectiveRightsFromAclA(IN PACL pacl,
                           IN PTRUSTEE_A pTrustee,
                           OUT PACCESS_MASK pAccessRights)
{
    PTRUSTEE_W pTrusteeW = NULL;
    DWORD ErrorCode;

    ErrorCode = InternalTrusteeAToW(pTrustee,
                                    &pTrusteeW);
    if (ErrorCode == ERROR_SUCCESS)
    {
        ErrorCode = InternalAclQueryRights(pacl, pTrusteeW, FALSE, TRUE, pAccessRights, NULL);

        InternalFreeConvertedTrustee(pTrusteeW,
                                     pTrustee);
    }
    else
        ErrorCode = ERROR_NOT_ENOUGH_MEMORY;

    return ErrorCode;
}


/*
 * @unimplemented
 */
DWORD
WINAPI
GetAuditedPermissionsFromAclW(IN PACL pacl,
                              IN PTRUSTEE_W pTrustee,
                              OUT PACCESS_MASK pSuccessfulAuditedRights,
                              OUT PACCESS_MASK pFailedAuditRights)
{
    return InternalAclQueryRights(pacl, pTrustee, TRUE, FALSE,
                                  pSuccessfulAuditedRights, pFailedAuditRights);
}


/*
 * @implemented
 */
DWORD
WINAPI
GetAuditedPermissionsFromAclA(IN PACL pacl,
                              IN PTRUSTEE_A pTrustee,
                              OUT PACCESS_MASK pSuccessfulAuditedRights,
                              OUT PACCESS_MASK pFailedAuditRights)
{
    PTRUSTEE_W pTrusteeW = NULL;
    DWORD ErrorCode;

    ErrorCode = InternalTrusteeAToW(pTrustee,
                                    &pTrusteeW);
    if (ErrorCode == ERROR_SUCCESS)
    {
        ErrorCode = InternalAclQueryRights(pacl, pTrusteeW, TRUE, TRUE,
                                          pSuccessfulAuditedRights, pFailedAuditRights);

        InternalFreeConvertedTrustee(pTrusteeW,
                                     pTrustee);
    }
    else
        ErrorCode = ERROR_NOT_ENOUGH_MEMORY;

    return ErrorCode;
}

/* EOF */
