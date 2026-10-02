/*
 * PROJECT:         ReactOS Kernel - Vista+ APIs
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * FILE:            lib/drivers/ntoskrnl_vista/fsrtl.c
 * PURPOSE:         FsRtl functions of Vista+
 * PROGRAMMERS:     Pierre Schweitzer <pierre@reactos.org>
 */

#include <ntdef.h>
#include <ntifs.h>
#include <pseh/pseh2.h>

typedef struct _ECP_LIST
{
    ULONG Signature;
    ULONG Flags;
    LIST_ENTRY EcpList;
} ECP_LIST, *PECP_LIST;

typedef ULONG ECP_HEADER_FLAGS;

typedef struct _ECP_HEADER
{
    ULONG Signature;
    ULONG Spare;
    LIST_ENTRY ListEntry;
    GUID EcpType;
    PFSRTL_EXTRA_CREATE_PARAMETER_CLEANUP_CALLBACK CleanupCallback;
    ECP_HEADER_FLAGS Flags;
    ULONG Size;
    PVOID ListAllocatedFrom;
    PVOID Filter;
} ECP_HEADER, *PECP_HEADER;

#define ECP_HEADER_SIZE (sizeof(ECP_HEADER))

#define ECP_LIST_SIGNATURE   'LpcE'
#define ECP_HEADER_SIGNATURE 'HpcE'
#define ECP_LIST_TAG         'LpcE'

#define ECP_HEADER_FLAG_ACKNOWLEDGED     0x00000001
#define ECP_HEADER_FLAG_FROM_USER_MODE   0x00000002
#define ECP_HEADER_FLAG_NONPAGED_LIST    0x00000004
#define ECP_HEADER_FLAG_CALLER_OWNED     0x00000008

#define ECP_HEADER_TO_CONTEXT(H) ((PVOID)((ULONG_PTR)H + ECP_HEADER_SIZE))
#define ECP_CONTEXT_TO_HEADER(C) ((PECP_HEADER)((ULONG_PTR)C - ECP_HEADER_SIZE))

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlRemoveDotsFromPath(IN PWSTR OriginalString,
                        IN USHORT PathLength,
                        OUT USHORT *NewLength)
{
    USHORT Length, ReadPos, WritePos;

    Length = PathLength / sizeof(WCHAR);

    if (Length == 3 && OriginalString[0] == '\\' && OriginalString[1] == '.' && OriginalString[2] == '.')
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (Length == 2 && OriginalString[0] == '.' && OriginalString[1] == '.')
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    if (Length > 2 && OriginalString[0] == '.' && OriginalString[1] == '.' && OriginalString[2] == '\\')
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    for (ReadPos = 0, WritePos = 0; ReadPos < Length; ++WritePos)
    {
        for (; ReadPos > 0 && ReadPos < Length; ++ReadPos)
        {
            if (ReadPos < Length - 1 && OriginalString[ReadPos] == '\\' && OriginalString[ReadPos + 1] == '\\')
            {
                continue;
            }

            if (OriginalString[ReadPos] != '.')
            {
                break;
            }

            if (ReadPos == Length - 1)
            {
                if (OriginalString[ReadPos - 1] == '\\')
                {
                    if (WritePos > 1)
                    {
                        --WritePos;
                    }

                    continue;
                }

                OriginalString[WritePos] = '.';
                ++WritePos;
                continue;
            }

            if (OriginalString[ReadPos + 1] == '\\')
            {
                if (OriginalString[ReadPos - 1] != '\\')
                {
                    OriginalString[WritePos] = '.';
                    ++WritePos;
                    continue;
                }
            }
            else
            {
                if (OriginalString[ReadPos + 1] != '.' || OriginalString[ReadPos - 1] != '\\' ||
                    ((ReadPos != Length - 2) && OriginalString[ReadPos + 2] != '\\'))
                {
                    OriginalString[WritePos] = '.';
                    ++WritePos;
                    continue;
                }

                for (WritePos -= 2; (SHORT)WritePos > 0 && OriginalString[WritePos] != '\\'; --WritePos);

                if ((SHORT)WritePos < 0 || OriginalString[WritePos] != '\\')
                {
                    return STATUS_IO_REPARSE_DATA_INVALID;
                }

                if (WritePos == 0 && ReadPos == Length - 2)
                {
                    WritePos = 1;
                }
            }

            ++ReadPos;
        }

        if (ReadPos >= Length)
        {
            break;
        }

        OriginalString[WritePos] = OriginalString[ReadPos];
        ++ReadPos;
    }

    *NewLength = WritePos * sizeof(WCHAR);

    while (WritePos < Length)
    {
        OriginalString[WritePos++] = UNICODE_NULL;
    }

    return STATUS_SUCCESS;
}

FORCEINLINE
BOOLEAN
IsNullGuid(IN PGUID Guid)
{
    if (Guid->Data1 == 0 && Guid->Data2 == 0 && Guid->Data3 == 0 &&
        ((ULONG *)Guid->Data4)[0] == 0 && ((ULONG *)Guid->Data4)[1] == 0)
    {
        return TRUE;
    }

    return FALSE;
}

FORCEINLINE
BOOLEAN
IsEven(IN USHORT Digit)
{
    return ((Digit & 1) != 1);
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlValidateReparsePointBuffer(IN ULONG BufferLength,
                                IN PREPARSE_DATA_BUFFER ReparseBuffer)
{
    USHORT DataLength;
    ULONG ReparseTag;
    PREPARSE_GUID_DATA_BUFFER GuidBuffer;

    /* Validate data size range */
    if (BufferLength < REPARSE_DATA_BUFFER_HEADER_SIZE || BufferLength > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    GuidBuffer = (PREPARSE_GUID_DATA_BUFFER)ReparseBuffer;
    DataLength = ReparseBuffer->ReparseDataLength;
    ReparseTag = ReparseBuffer->ReparseTag;

    /* Validate size consistency */
    if (DataLength + REPARSE_DATA_BUFFER_HEADER_SIZE != BufferLength && DataLength + REPARSE_GUID_DATA_BUFFER_HEADER_SIZE != BufferLength)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    /* REPARSE_DATA_BUFFER is reserved for MS tags */
    if (DataLength + REPARSE_DATA_BUFFER_HEADER_SIZE == BufferLength && !IsReparseTagMicrosoft(ReparseTag))
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    /* If that a GUID data buffer, its GUID cannot be null, and it cannot contain a MS tag */
    if (DataLength + REPARSE_GUID_DATA_BUFFER_HEADER_SIZE == BufferLength && ((!IsReparseTagMicrosoft(ReparseTag)
        && IsNullGuid(&GuidBuffer->ReparseGuid)) || (ReparseTag == IO_REPARSE_TAG_MOUNT_POINT || ReparseTag == IO_REPARSE_TAG_SYMLINK)))
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    /* Check the data for MS non reserved tags */
    if (!(ReparseTag & 0xFFF0000) && ReparseTag != IO_REPARSE_TAG_RESERVED_ZERO && ReparseTag != IO_REPARSE_TAG_RESERVED_ONE)
    {
        /* If that's a mount point, validate the MountPointReparseBuffer branch */
        if (ReparseTag == IO_REPARSE_TAG_MOUNT_POINT)
        {
            /* We need information */
            if (DataLength >= REPARSE_DATA_BUFFER_HEADER_SIZE)
            {
                /* Substitue must be the first in row */
                if (!ReparseBuffer->MountPointReparseBuffer.SubstituteNameOffset)
                {
                    /* Substitude must be null-terminated */
                    if (ReparseBuffer->MountPointReparseBuffer.PrintNameOffset == ReparseBuffer->MountPointReparseBuffer.SubstituteNameLength + sizeof(UNICODE_NULL))
                    {
                        /* There must just be the Offset/Length fields + buffer + 2 null chars */
                        if (DataLength == ReparseBuffer->MountPointReparseBuffer.PrintNameLength + ReparseBuffer->MountPointReparseBuffer.SubstituteNameLength + (FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.PathBuffer) - FIELD_OFFSET(REPARSE_DATA_BUFFER, MountPointReparseBuffer.SubstituteNameOffset)) + 2 * sizeof(UNICODE_NULL))
                        {
                            return STATUS_SUCCESS;
                        }
                    }
                }
            }
        }
        else
        {
#define FIELDS_SIZE (FIELD_OFFSET(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.PathBuffer) - FIELD_OFFSET(REPARSE_DATA_BUFFER, SymbolicLinkReparseBuffer.SubstituteNameOffset))

            /* If that's not a symlink, accept the MS tag as it */
            if (ReparseTag != IO_REPARSE_TAG_SYMLINK)
            {
                return STATUS_SUCCESS;
            }

            /* We need information */
            if (DataLength >= FIELDS_SIZE)
            {
                /* Validate lengths */
                if (ReparseBuffer->SymbolicLinkReparseBuffer.SubstituteNameLength && ReparseBuffer->SymbolicLinkReparseBuffer.PrintNameLength)
                {
                    /* Validate unicode strings */
                    if (IsEven(ReparseBuffer->SymbolicLinkReparseBuffer.SubstituteNameLength) && IsEven(ReparseBuffer->SymbolicLinkReparseBuffer.PrintNameLength) &&
                        IsEven(ReparseBuffer->SymbolicLinkReparseBuffer.SubstituteNameOffset) && IsEven(ReparseBuffer->SymbolicLinkReparseBuffer.PrintNameOffset))
                    {
                        if ((DataLength + REPARSE_DATA_BUFFER_HEADER_SIZE >= ReparseBuffer->SymbolicLinkReparseBuffer.SubstituteNameOffset + ReparseBuffer->SymbolicLinkReparseBuffer.SubstituteNameLength + FIELDS_SIZE + REPARSE_DATA_BUFFER_HEADER_SIZE)
                            && (DataLength + REPARSE_DATA_BUFFER_HEADER_SIZE >= ReparseBuffer->SymbolicLinkReparseBuffer.PrintNameLength + ReparseBuffer->SymbolicLinkReparseBuffer.PrintNameOffset + FIELDS_SIZE + REPARSE_DATA_BUFFER_HEADER_SIZE))
                        {
                            return STATUS_SUCCESS;
                        }
                    }
                }
            }
#undef FIELDS_SIZE
        }

        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    return STATUS_IO_REPARSE_TAG_INVALID;
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlGetEcpListFromIrp(IN PIRP Irp,
                       OUT PECP_LIST *EcpList)
{
    /* Call Io */
    return IoGetIrpExtraCreateParameter(Irp, EcpList);
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlSetEcpListIntoIrp(IN OUT PIRP Irp,
                       IN PECP_LIST EcpList)
{
    return IoSetIrpExtraCreateParameter(Irp, EcpList);
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlInitializeExtraCreateParameterList(IN OUT PECP_LIST EcpList)
{
    EcpList->Signature = ECP_LIST_SIGNATURE;
    EcpList->Flags = 0;
    InitializeListHead(&EcpList->EcpList);
    return STATUS_SUCCESS;
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlAllocateExtraCreateParameterList(IN FSRTL_ALLOCATE_ECPLIST_FLAGS Flags,
                                      OUT PECP_LIST *EcpList)
{
    PECP_LIST List = NULL;

    if (Flags & FSRTL_ALLOCATE_ECPLIST_FLAG_CHARGE_QUOTA)
    {
        _SEH2_TRY
        {
            List = ExAllocatePoolWithQuotaTag(PagedPool, sizeof(ECP_LIST), ECP_LIST_TAG);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            List = NULL;
        }
        _SEH2_END;
    }
    else
    {
        List = ExAllocatePoolWithTag(PagedPool, sizeof(ECP_LIST), ECP_LIST_TAG);
    }

    *EcpList = List;
    if (List == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FsRtlInitializeExtraCreateParameterList(List);
}

NTKERNELAPI
VOID
NTAPI
FsRtlInitializeExtraCreateParameter(OUT PECP_HEADER Ecp,
                                    IN ULONG EcpFlags,
                                    IN PFSRTL_EXTRA_CREATE_PARAMETER_CLEANUP_CALLBACK CleanupCallback OPTIONAL,
                                    IN ULONG TotalSize,
                                    IN LPCGUID EcpType,
                                    IN PVOID ListAllocatedFrom OPTIONAL)
{
    Ecp->Signature = ECP_HEADER_SIGNATURE;
    Ecp->Spare = 0;
    InitializeListHead(&Ecp->ListEntry);
    Ecp->EcpType = *EcpType;
    Ecp->CleanupCallback = CleanupCallback;
    Ecp->Flags = EcpFlags;
    Ecp->Size = TotalSize;
    Ecp->ListAllocatedFrom = ListAllocatedFrom;
    Ecp->Filter = NULL;
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlAllocateExtraCreateParameter(IN LPCGUID EcpType,
                                  IN ULONG SizeOfContext,
                                  IN FSRTL_ALLOCATE_ECP_FLAGS Flags,
                                  IN PFSRTL_EXTRA_CREATE_PARAMETER_CLEANUP_CALLBACK CleanupCallback OPTIONAL,
                                  IN ULONG PoolTag,
                                  OUT PVOID *EcpContext)
{
    POOL_TYPE PoolType = (Flags & FSRTL_ALLOCATE_ECP_FLAG_NONPAGED_POOL) ? NonPagedPool : PagedPool;
    ULONG TotalSize = ECP_HEADER_SIZE + SizeOfContext;
    PECP_HEADER Header = NULL;

    *EcpContext = NULL;

    if (TotalSize < SizeOfContext)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Flags & FSRTL_ALLOCATE_ECP_FLAG_CHARGE_QUOTA)
    {
        _SEH2_TRY
        {
            Header = ExAllocatePoolWithQuotaTag(PoolType, TotalSize, PoolTag);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Header = NULL;
        }
        _SEH2_END;
    }
    else
    {
        Header = ExAllocatePoolWithTag(PoolType, TotalSize, PoolTag);
    }

    if (Header == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Header, TotalSize);
    FsRtlInitializeExtraCreateParameter(Header, 0, CleanupCallback, TotalSize, EcpType, NULL);
    *EcpContext = ECP_HEADER_TO_CONTEXT(Header);
    return STATUS_SUCCESS;
}

NTKERNELAPI
VOID
NTAPI
FsRtlInitExtraCreateParameterLookasideList(IN OUT PVOID Lookaside,
                                           IN FSRTL_ECP_LOOKASIDE_FLAGS Flags,
                                           IN SIZE_T Size,
                                           IN ULONG Tag)
{
    if (Flags & FSRTL_ECP_LOOKASIDE_FLAG_NONPAGED_POOL)
    {
        ExInitializeNPagedLookasideList(Lookaside, NULL, NULL, 0, ECP_HEADER_SIZE + Size, Tag, 0);
    }
    else
    {
        ExInitializePagedLookasideList(Lookaside, NULL, NULL, 0, ECP_HEADER_SIZE + Size, Tag, 0);
    }
}

NTKERNELAPI
VOID
NTAPI
FsRtlDeleteExtraCreateParameterLookasideList(IN OUT PVOID Lookaside,
                                             IN FSRTL_ECP_LOOKASIDE_FLAGS Flags)
{
    if (Flags & FSRTL_ECP_LOOKASIDE_FLAG_NONPAGED_POOL)
    {
        ExDeleteNPagedLookasideList(Lookaside);
    }
    else
    {
        ExDeletePagedLookasideList(Lookaside);
    }
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlAllocateExtraCreateParameterFromLookasideList(IN LPCGUID EcpType,
                                                   IN ULONG SizeOfContext,
                                                   IN FSRTL_ALLOCATE_ECP_FLAGS Flags,
                                                   IN PFSRTL_EXTRA_CREATE_PARAMETER_CLEANUP_CALLBACK CleanupCallback OPTIONAL,
                                                   IN OUT PVOID LookasideList,
                                                   OUT PVOID *EcpContext)
{
    PGENERAL_LOOKASIDE General = LookasideList;
    ULONG TotalSize = ECP_HEADER_SIZE + SizeOfContext;
    PECP_HEADER Header;

    *EcpContext = NULL;

    if (TotalSize < SizeOfContext)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (TotalSize > General->Size)
    {
        return FsRtlAllocateExtraCreateParameter(EcpType,
                                                 SizeOfContext,
                                                 Flags,
                                                 CleanupCallback,
                                                 General->Tag,
                                                 EcpContext);
    }

    if (Flags & FSRTL_ALLOCATE_ECP_FLAG_NONPAGED_POOL)
    {
        Header = ExAllocateFromNPagedLookasideList(LookasideList);
    }
    else
    {
        Header = ExAllocateFromPagedLookasideList(LookasideList);
    }

    if (Header == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Header, TotalSize);
    FsRtlInitializeExtraCreateParameter(Header,
                                        (Flags & FSRTL_ALLOCATE_ECP_FLAG_NONPAGED_POOL) ? ECP_HEADER_FLAG_NONPAGED_LIST : 0,
                                        CleanupCallback,
                                        TotalSize,
                                        EcpType,
                                        LookasideList);
    *EcpContext = ECP_HEADER_TO_CONTEXT(Header);
    return STATUS_SUCCESS;
}

NTKERNELAPI
VOID
NTAPI
FsRtlFreeExtraCreateParameter(IN PVOID EcpContext)
{
    PECP_HEADER Header = ECP_CONTEXT_TO_HEADER(EcpContext);

    if (Header->CleanupCallback != NULL)
    {
        Header->CleanupCallback(EcpContext, &Header->EcpType);
    }

    Header->Signature = 0;
    if (Header->ListAllocatedFrom == NULL)
    {
        ExFreePool(Header);
    }
    else if (Header->Flags & ECP_HEADER_FLAG_NONPAGED_LIST)
    {
        ExFreeToNPagedLookasideList(Header->ListAllocatedFrom, Header);
    }
    else
    {
        ExFreeToPagedLookasideList(Header->ListAllocatedFrom, Header);
    }
}

NTKERNELAPI
VOID
NTAPI
FsRtlFreeExtraCreateParameterList(IN PECP_LIST EcpList)
{
    PECP_HEADER Header;

    while (!IsListEmpty(&EcpList->EcpList))
    {
        Header = CONTAINING_RECORD(RemoveHeadList(&EcpList->EcpList), ECP_HEADER, ListEntry);
        InitializeListHead(&Header->ListEntry);
        FsRtlFreeExtraCreateParameter(ECP_HEADER_TO_CONTEXT(Header));
    }

    EcpList->Signature = 0;
    ExFreePoolWithTag(EcpList, ECP_LIST_TAG);
}

static
PECP_HEADER
FsRtlpFindEcp(IN PECP_LIST EcpList,
              IN LPCGUID EcpType)
{
    PLIST_ENTRY Entry;
    PECP_HEADER Header;

    for (Entry = EcpList->EcpList.Flink; Entry != &EcpList->EcpList; Entry = Entry->Flink)
    {
        Header = CONTAINING_RECORD(Entry, ECP_HEADER, ListEntry);
        if (RtlCompareMemory(&Header->EcpType, EcpType, sizeof(GUID)) == sizeof(GUID))
        {
            return Header;
        }
    }

    return NULL;
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlInsertExtraCreateParameter(IN OUT PECP_LIST EcpList,
                                IN OUT PVOID EcpContext)
{
    PECP_HEADER Header = ECP_CONTEXT_TO_HEADER(EcpContext);

    if (FsRtlpFindEcp(EcpList, &Header->EcpType) != NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    InsertTailList(&EcpList->EcpList, &Header->ListEntry);
    return STATUS_SUCCESS;
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlFindExtraCreateParameter(IN PECP_LIST EcpList,
                              IN LPCGUID EcpType,
                              OUT PVOID *EcpContext OPTIONAL,
                              OUT ULONG *EcpContextSize OPTIONAL)
{
    PECP_HEADER Header = FsRtlpFindEcp(EcpList, EcpType);

    if (EcpContext != NULL)
    {
        *EcpContext = (Header != NULL) ? ECP_HEADER_TO_CONTEXT(Header) : NULL;
    }
    if (EcpContextSize != NULL)
    {
        *EcpContextSize = (Header != NULL) ? Header->Size - ECP_HEADER_SIZE : 0;
    }

    return (Header != NULL) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlRemoveExtraCreateParameter(IN OUT PECP_LIST EcpList,
                                IN LPCGUID EcpType,
                                OUT PVOID *EcpContext,
                                OUT ULONG *EcpContextSize OPTIONAL)
{
    PECP_HEADER Header = FsRtlpFindEcp(EcpList, EcpType);

    if (Header == NULL)
    {
        *EcpContext = NULL;
        return STATUS_NOT_FOUND;
    }

    RemoveEntryList(&Header->ListEntry);
    InitializeListHead(&Header->ListEntry);
    Header->Flags &= ~ECP_HEADER_FLAG_CALLER_OWNED;

    *EcpContext = ECP_HEADER_TO_CONTEXT(Header);
    if (EcpContextSize != NULL)
    {
        *EcpContextSize = Header->Size - ECP_HEADER_SIZE;
    }
    return STATUS_SUCCESS;
}

NTKERNELAPI
VOID
NTAPI
FsRtlAcknowledgeEcp(IN PVOID EcpContext)
{
    ECP_CONTEXT_TO_HEADER(EcpContext)->Flags |= ECP_HEADER_FLAG_ACKNOWLEDGED;
}

NTKERNELAPI
VOID
NTAPI
FsRtlPrepareToReuseEcp(IN PVOID EcpContext)
{
    ECP_CONTEXT_TO_HEADER(EcpContext)->Flags &= ~ECP_HEADER_FLAG_ACKNOWLEDGED;
}

NTKERNELAPI
BOOLEAN
NTAPI
FsRtlIsEcpAcknowledged(IN PVOID EcpContext)
{
    return (ECP_CONTEXT_TO_HEADER(EcpContext)->Flags & ECP_HEADER_FLAG_ACKNOWLEDGED) != 0;
}

NTKERNELAPI
BOOLEAN
NTAPI
FsRtlIsEcpFromUserMode(IN PVOID EcpContext)
{
    return (ECP_CONTEXT_TO_HEADER(EcpContext)->Flags & ECP_HEADER_FLAG_FROM_USER_MODE) != 0;
}

VOID
NTAPI
FsRtlpMarkCallerEcps(IN PECP_LIST EcpList)
{
    PLIST_ENTRY Entry;

    for (Entry = EcpList->EcpList.Flink; Entry != &EcpList->EcpList; Entry = Entry->Flink)
    {
        CONTAINING_RECORD(Entry, ECP_HEADER, ListEntry)->Flags |= ECP_HEADER_FLAG_CALLER_OWNED;
    }
}

VOID
NTAPI
FsRtlpFreeAddedEcps(IN PECP_LIST EcpList)
{
    PLIST_ENTRY Entry, Next;
    PECP_HEADER Header;

    for (Entry = EcpList->EcpList.Flink; Entry != &EcpList->EcpList; Entry = Next)
    {
        Next = Entry->Flink;
        Header = CONTAINING_RECORD(Entry, ECP_HEADER, ListEntry);
        if (Header->Flags & ECP_HEADER_FLAG_CALLER_OWNED)
        {
            Header->Flags &= ~ECP_HEADER_FLAG_CALLER_OWNED;
            continue;
        }

        RemoveEntryList(&Header->ListEntry);
        InitializeListHead(&Header->ListEntry);
        FsRtlFreeExtraCreateParameter(ECP_HEADER_TO_CONTEXT(Header));
    }
}

NTKERNELAPI
NTSTATUS
NTAPI
FsRtlGetNextExtraCreateParameter(IN PECP_LIST EcpList,
                                 IN PVOID CurrentEcpContext,
                                 OUT LPGUID NextEcpType OPTIONAL,
                                 OUT PVOID *NextEcpContext,
                                 OUT PULONG NextEcpContextSize OPTIONAL)
{
    PECP_HEADER CurrentEntry;

    if (EcpList == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* If we have no context ... */
    if (CurrentEcpContext == NULL)
    {
        if (IsListEmpty(&EcpList->EcpList))
        {
            goto FailEmpty;
        }

        /* Simply consider first entry */
        CurrentEntry = CONTAINING_RECORD(EcpList->EcpList.Flink, ECP_HEADER, ListEntry);
    }
    else
    {
        /* Otherwise, consider the entry matching the given context */
        CurrentEntry = ECP_CONTEXT_TO_HEADER(CurrentEcpContext);

        /* Make sure we didn't reach the end */
        if (CurrentEntry->ListEntry.Flink == &EcpList->EcpList)
        {
            goto FailEmpty;
        }

        CurrentEntry = CONTAINING_RECORD(CurrentEntry->ListEntry.Flink, ECP_HEADER, ListEntry);
    }

    /* We must have an entry */
    if (CurrentEntry == NULL)
    {
        goto FailEmpty;
    }

    /* If caller wants a context, give it */
    if (NextEcpContext != NULL)
    {
        *NextEcpContext = ECP_HEADER_TO_CONTEXT(CurrentEntry);
    }

    /* Same for its size (which the size minus the header overhead) */
    if (NextEcpContextSize != NULL)
    {
         *NextEcpContextSize = CurrentEntry->Size - sizeof(ECP_HEADER);
    }

    /* And copy the type if asked to */
    if (NextEcpType != NULL)
    {
        RtlCopyMemory(NextEcpType, &CurrentEntry->EcpType, sizeof(GUID));
    }

    /* Job done */
    return STATUS_SUCCESS;

    /* Failure case: just zero everything */
FailEmpty:
    if (NextEcpContext != NULL)
    {
        *NextEcpContext = NULL;
    }

    if (NextEcpContextSize != NULL)
    {
        *NextEcpContextSize = 0;
    }

    if (NextEcpType != NULL)
    {
        RtlZeroMemory(NextEcpType, sizeof(GUID));
    }

    /* And return failure */
    return STATUS_NOT_FOUND;
}

NTKRNLVISTAAPI
BOOLEAN
NTAPI
FsRtlAreVolumeStartupApplicationsComplete(VOID)
{
    return TRUE;
}

NTKRNLVISTAAPI
NTSTATUS
NTAPI
FsRtlCheckOplockEx(
    _In_ POPLOCK Oplock,
    _In_ PIRP Irp,
    _In_ ULONG Flags,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine)
{
    return FsRtlCheckOplock(Oplock, Irp, Context, CompletionRoutine, PostIrpRoutine);
}

NTKRNLVISTAAPI
NTSTATUS
NTAPI
FsRtlOplockBreakH(
    _In_ POPLOCK Oplock,
    _In_ PIRP Irp,
    _In_ ULONG Flags,
    _In_opt_ PVOID Context,
    _In_opt_ POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine,
    _In_opt_ POPLOCK_FS_PREPOST_IRP PostIrpRoutine)
{
    return FsRtlCheckOplock(Oplock, Irp, Context, CompletionRoutine, PostIrpRoutine);
}

NTKRNLVISTAAPI
BOOLEAN
NTAPI
FsRtlOplockIsSharedRequest(
    _In_ PIRP Irp)
{
    return FALSE;
}

NTKRNLVISTAAPI
BOOLEAN
NTAPI
FsRtlCurrentOplockH(
    _In_ POPLOCK Oplock)
{
    return FALSE;
}

NTKRNLVISTAAPI
BOOLEAN
NTAPI
FsRtlAreThereCurrentOrInProgressFileLocks(
    _In_ PFILE_LOCK FileLock)
{
    return FsRtlAreThereCurrentFileLocks(FileLock);
}

NTKRNLVISTAAPI
VOID
NTAPI
CcCoherencyFlushAndPurgeCache(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer,
    _In_opt_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _Out_ PIO_STATUS_BLOCK IoStatus,
    _In_opt_ ULONG Flags)
{
    CcFlushCache(SectionObjectPointer, FileOffset, Length, IoStatus);
}

NTKRNLVISTAAPI
BOOLEAN
NTAPI
CcCopyWriteWontFlush(
    _In_ PFILE_OBJECT FileObject,
    _In_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length)
{
    return CcCanIWrite(FileObject, Length, FALSE, FALSE);
}

