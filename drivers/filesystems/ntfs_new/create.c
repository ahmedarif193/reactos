/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new file creation APIs
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfspch.h"

/* GLOBALS *****************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtfsFsdCreate)
#endif

/* FUNCTIONS ****************************************************************/

static
NTSTATUS
NtfsReturnCreateReparse(
    _Inout_ PIRP Irp,
    _In_ PNtfsFileRecord File,
    _In_ ULONG RemainingNameLength)
{
    PReparsePointEx ReparseData;
    ULONG BufferLength = 0;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;
    if (!File ||
        RemainingNameLength > MAXUSHORT)
    {
        return STATUS_NAME_TOO_LONG;
    }

    Status = NtfsFileRecordReadReparsePoint(
        File,
        NULL,
        &BufferLength);
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return Status;
    if (BufferLength < sizeof(*ReparseData) ||
        BufferLength >
            NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return STATUS_IO_REPARSE_DATA_INVALID;
    }

    ReparseData =
        (PReparsePointEx)ExAllocatePoolWithTag(
            NonPagedPool,
            BufferLength,
            TAG_NTFS);
    if (!ReparseData)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfsFileRecordReadReparsePoint(
        File,
        (PUCHAR)ReparseData,
        &BufferLength);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(ReparseData, TAG_NTFS);
        return Status;
    }

    /*
     * The common header is layout-compatible with REPARSE_DATA_BUFFER.
     * The I/O manager consumes Reserved as the byte count of the unparsed
     * suffix and owns AuxiliaryBuffer after STATUS_REPARSE is returned.
     */
    ReparseData->Padding =
        (USHORT)RemainingNameLength;
    Irp->Tail.Overlay.AuxiliaryBuffer =
        (PCHAR)ReparseData;
    Irp->IoStatus.Information =
        ReparseData->ReparseType;
    return STATUS_REPARSE;
}

BOOLEAN
NtfsSplitParentName(
    _In_ PUNICODE_STRING Name,
    _Out_ PUSHORT ParentLength,
    _Out_ PWCHAR* LeafName,
    _Out_ PUSHORT LeafLength)
{
    ULONG CharacterCount;
    ULONG Index;
    ULONG Separator = 0;
    BOOLEAN FoundSeparator = FALSE;

    if (!Name || !Name->Buffer || !Name->Length)
        return FALSE;

    CharacterCount = Name->Length / sizeof(WCHAR);
    for (Index = CharacterCount; Index != 0; Index--)
    {
        if (Name->Buffer[Index - 1] == L'\\')
        {
            Separator = Index - 1;
            FoundSeparator = TRUE;
            break;
        }
    }
    if (!FoundSeparator || Separator + 1 >= CharacterCount)
        return FALSE;

    *ParentLength = (USHORT)(Separator == 0 ? 1 : Separator);
    *LeafName = &Name->Buffer[Separator + 1];
    *LeafLength = (USHORT)(CharacterCount - Separator - 1);
    return TRUE;
}

static
NTSTATUS
NtfsCompleteCreate(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Information = Information;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

static
NTSTATUS
NtfsNormalizeRelatedName(
    _Inout_ PFILE_OBJECT FileObject)
{
    PFILE_OBJECT RelatedFileObject;
    PFileContextBlock ParentFileCB;
    PWCHAR NewBuffer;
    USHORT ParentLength;
    USHORT ChildLength;
    USHORT SeparatorLength;
    ULONG NewLength;

    RelatedFileObject = FileObject->RelatedFileObject;
    if (!RelatedFileObject)
        return STATUS_SUCCESS;

    ParentFileCB = NtfsGetFileContext(RelatedFileObject);
    if (!ParentFileCB)
        return STATUS_INVALID_PARAMETER;

    ChildLength = FileObject->FileName.Length;
    if (ParentFileCB->IsVolumeOpen)
        return ChildLength == 0 ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;

    if (ChildLength != 0 && FileObject->FileName.Buffer[0] == L'\\')
        return STATUS_INVALID_PARAMETER;

    if (ChildLength != 0 &&
        (!ParentFileCB->FileRec ||
         !(NtfsFileRecordGetHeader(ParentFileCB->FileRec)->Flags & FR_IS_DIRECTORY)))
    {
        return STATUS_OBJECT_PATH_NOT_FOUND;
    }

    ParentLength = ParentFileCB->FileName.Length;
    SeparatorLength = ParentLength != 0 &&
                      ChildLength != 0 &&
                      ParentFileCB->FileName.Buffer[ParentLength / sizeof(WCHAR) - 1] != L'\\'
                          ? sizeof(WCHAR)
                          : 0;
    NewLength = (ULONG)ParentLength + SeparatorLength + ChildLength;
    if (NewLength > MAXUSHORT - sizeof(WCHAR))
        return STATUS_OBJECT_NAME_INVALID;

    NewBuffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool, NewLength + sizeof(WCHAR), TAG_NTFS);
    if (!NewBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (ParentLength != 0)
        RtlCopyMemory(NewBuffer, ParentFileCB->FileName.Buffer, ParentLength);
    if (SeparatorLength != 0)
        NewBuffer[ParentLength / sizeof(WCHAR)] = L'\\';
    if (ChildLength != 0)
        RtlCopyMemory((PUCHAR)NewBuffer + ParentLength + SeparatorLength, FileObject->FileName.Buffer, ChildLength);
    NewBuffer[NewLength / sizeof(WCHAR)] = UNICODE_NULL;

    if (FileObject->FileName.Buffer)
        ExFreePoolWithTag(FileObject->FileName.Buffer, 0);
    FileObject->FileName.Buffer = NewBuffer;
    FileObject->FileName.Length = (USHORT)NewLength;
    FileObject->FileName.MaximumLength = (USHORT)(NewLength + sizeof(WCHAR));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsResolveFileIdName(
    _In_ PVolumeContextBlock VolCB,
    _Inout_ PFILE_OBJECT FileObject)
{
    ULONGLONG Reference;
    PWCHAR Scratch;
    PWCHAR NewBuffer;
    ULONG ScratchLength = (MAXUSHORT - sizeof(WCHAR)) / sizeof(WCHAR);
    ULONG PathLength = 0;
    NTSTATUS Status;

    if (FileObject->FileName.Length != sizeof(ULONGLONG) || !FileObject->FileName.Buffer)
        return STATUS_INVALID_PARAMETER;
    if (!VolCB->DiskVolume)
        return STATUS_VOLUME_DISMOUNTED;
    RtlCopyMemory(&Reference, FileObject->FileName.Buffer, sizeof(Reference));

    Scratch = ExAllocatePoolWithTag(PagedPool, ScratchLength * sizeof(WCHAR), TAG_NTFS);
    if (!Scratch)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeEnterCriticalRegion();
    NtfsAcquireMetadata(VolCB);
    Status = NtfsMasterFileTableGetPathFromFileReference(NtfsVolumeGetMft(VolCB->DiskVolume),
                                                         Reference,
                                                         Scratch,
                                                         ScratchLength,
                                                         &PathLength);
    NtfsReleaseMetadata(VolCB);
    KeLeaveCriticalRegion();
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Scratch, TAG_NTFS);
        return Status == STATUS_BUFFER_TOO_SMALL ? STATUS_NAME_TOO_LONG : Status;
    }

    NewBuffer = ExAllocatePoolWithTag(PagedPool, (PathLength + 1) * sizeof(WCHAR), TAG_NTFS);
    if (!NewBuffer)
    {
        ExFreePoolWithTag(Scratch, TAG_NTFS);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(NewBuffer, Scratch, PathLength * sizeof(WCHAR));
    NewBuffer[PathLength] = UNICODE_NULL;
    ExFreePoolWithTag(Scratch, TAG_NTFS);

    ExFreePoolWithTag(FileObject->FileName.Buffer, 0);
    FileObject->FileName.Buffer = NewBuffer;
    FileObject->FileName.Length = (USHORT)(PathLength * sizeof(WCHAR));
    FileObject->FileName.MaximumLength = (USHORT)((PathLength + 1) * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsValidateCreateName(
    _In_ PUNICODE_STRING Name)
{
    ULONG CharacterCount;
    ULONG LeadingSeparators = 0;
    ULONG ComponentStart;
    ULONG Index;
    BOOLEAN InStream;

    if (!Name->Length)
        return STATUS_SUCCESS;
    if (!Name->Buffer || (Name->Length & (sizeof(WCHAR) - 1)) != 0)
        return STATUS_OBJECT_NAME_INVALID;

    CharacterCount = Name->Length / sizeof(WCHAR);
    while (LeadingSeparators < CharacterCount && Name->Buffer[LeadingSeparators] == L'\\')
        LeadingSeparators++;
    if (LeadingSeparators > 2)
        return STATUS_OBJECT_NAME_INVALID;

    ComponentStart = LeadingSeparators;
    InStream = FALSE;
    for (Index = LeadingSeparators; Index < CharacterCount; Index++)
    {
        ULONG ComponentLength;
        WCHAR Character = Name->Buffer[Index];

        if (Character != L'\\')
        {
            if (Character == L':')
                InStream = TRUE;
            else if (!InStream &&
                     (Character < 0x20 || Character == L'/' || Character == L'|' ||
                      FsRtlIsUnicodeCharacterWild(Character)))
                return STATUS_OBJECT_NAME_INVALID;
            continue;
        }
        if (InStream)
            return STATUS_OBJECT_NAME_INVALID;
        if (Index == ComponentStart)
            return STATUS_OBJECT_NAME_INVALID;

        ComponentLength = Index - ComponentStart;
        if (ComponentLength == 1 && Name->Buffer[ComponentStart] == L'.')
            return Index + 1 < CharacterCount ? STATUS_OBJECT_PATH_NOT_FOUND : STATUS_OBJECT_NAME_INVALID;
        if (ComponentLength == 2 && Name->Buffer[ComponentStart] == L'.' && Name->Buffer[ComponentStart + 1] == L'.')
            return STATUS_OBJECT_NAME_INVALID;
        ComponentStart = Index + 1;
    }

    if (ComponentStart < CharacterCount)
    {
        ULONG ComponentLength = CharacterCount - ComponentStart;

        if (ComponentLength == 1 && Name->Buffer[ComponentStart] == L'.')
            return STATUS_OBJECT_NAME_INVALID;
        if (ComponentLength == 2 && Name->Buffer[ComponentStart] == L'.' && Name->Buffer[ComponentStart + 1] == L'.')
            return STATUS_OBJECT_NAME_INVALID;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsTranslateNotFoundStatus(
    _In_ PNtfsMasterFileTable Mft,
    _In_ PUNICODE_STRING Name)
{
    UNICODE_STRING ParentName;
    PNtfsFileRecord ParentFile = NULL;
    PWCHAR LeafName;
    USHORT ParentLength;
    USHORT LeafLength;
    ULONG RemainingNameLength = 0;
    BOOLEAN InvalidLeaf = FALSE;
    USHORT Index;
    NTSTATUS Status;

    if (!NtfsSplitParentName(Name, &ParentLength, &LeafName, &LeafLength))
        return STATUS_OBJECT_NAME_NOT_FOUND;

    for (Index = 0; Index < LeafLength; Index++)
    {
        if (LeafName[Index] == L'?' || LeafName[Index] == L'*')
        {
            InvalidLeaf = TRUE;
            break;
        }
    }
    ParentName.Buffer = Name->Buffer;
    ParentName.Length = ParentLength * sizeof(WCHAR);
    ParentName.MaximumLength = ParentName.Length;
    Status = NtfsMasterFileTableGetFileRecordFromQueryEx(Mft, ParentName.Buffer, ParentName.Length / sizeof(WCHAR), TRUE, &RemainingNameLength, &ParentFile);
    if (NT_SUCCESS(Status) && RemainingNameLength == 0 && ParentFile && (NtfsFileRecordGetHeader(ParentFile)->Flags & FR_IS_DIRECTORY))
        Status = InvalidLeaf ? STATUS_OBJECT_NAME_INVALID : STATUS_OBJECT_NAME_NOT_FOUND;
    else
        Status = STATUS_OBJECT_PATH_NOT_FOUND;
    if (ParentFile)
        NtfsFileRecordDestroy(ParentFile);
    return Status;
}

static
NTSTATUS
NtfsCheckFileAccess(
    _In_ PNtfsFileRecord File,
    _In_ PACCESS_STATE AccessState,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK PreviouslyGrantedAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PACCESS_MASK GrantedAccess)
{
    PPRIVILEGE_SET Privileges = NULL;
    NTSTATUS Status;

    Status = NtfsCheckRecordAccess(File,
                                   &AccessState->SubjectSecurityContext,
                                   DesiredAccess,
                                   PreviouslyGrantedAccess,
                                   AccessMode,
                                   GrantedAccess,
                                   &Privileges);
    if (NT_SUCCESS(Status) && Privileges)
        Status = SeAppendPrivileges(AccessState, Privileges);
    if (Privileges)
        SeFreePrivileges(Privileges);
    return Status;
}

static
NTSTATUS
NtfsCheckTraverseAccess(
    _In_ PNtfsMasterFileTable Mft,
    _In_ PUNICODE_STRING Name,
    _In_ ULONGLONG RelatedReference,
    _In_ USHORT NameOffset,
    _In_ BOOLEAN IncludeFinalDirectory,
    _In_opt_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PNtfsFileRecord Directory = NULL;
    PNtfsFileRecord NextDirectory = NULL;
    ACCESS_MASK GrantedAccess;
    ULONG RemainingNameLength = 0;
    ULONG CharacterCount = Name->Length / sizeof(WCHAR);
    ULONG Index = min(NameOffset, CharacterCount);
    ULONG End;
    ULONG Next;
    NTSTATUS Status = STATUS_SUCCESS;

    if (RelatedReference)
    {
        ULONG ParentLength = Index;

        while (ParentLength > 1 && Name->Buffer[ParentLength - 1] == L'\\')
            --ParentLength;
        Status = NtfsMasterFileTableGetFileRecordFromQueryEx(
            Mft, Name->Buffer, ParentLength, TRUE, &RemainingNameLength, &Directory);
        if (NT_SUCCESS(Status) &&
            (!Directory || RemainingNameLength != 0 ||
             (((ULONGLONG)NtfsFileRecordGetHeader(Directory)->SequenceNumber << 48) |
              NtfsFileRecordGetHeader(Directory)->MFTRecordNumber) != RelatedReference))
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        }
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    if (!AccessState || AccessMode == KernelMode ||
        (AccessState->Flags & TOKEN_HAS_TRAVERSE_PRIVILEGE))
    {
        goto Done;
    }
    while (Index < CharacterCount && Name->Buffer[Index] == L'\\')
        ++Index;
    if (Index == CharacterCount && !IncludeFinalDirectory)
        goto Done;

    if (!Directory)
        Status = NtfsMasterFileTableGetFileRecordFromQueryEx(Mft, L"\\", 1, TRUE,
                                                             &RemainingNameLength, &Directory);
    if (!NT_SUCCESS(Status))
        goto Done;

    for (;;)
    {
        if (!(NtfsFileRecordGetHeader(Directory)->Flags & FR_IS_DIRECTORY))
        {
            Status = STATUS_OBJECT_PATH_NOT_FOUND;
            break;
        }
        if (NtfsFileRecordGetAttribute(Directory, TypeReparsePoint, NULL))
        {
            Status = STATUS_SUCCESS;
            break;
        }
        Status = NtfsCheckFileAccess(Directory, AccessState, FILE_TRAVERSE,
                                     0, AccessMode, &GrantedAccess);
        if (!NT_SUCCESS(Status) || Index == CharacterCount)
            break;

        End = Index;
        while (End < CharacterCount && Name->Buffer[End] != L'\\')
            ++End;
        Next = End;
        while (Next < CharacterCount && Name->Buffer[Next] == L'\\')
            ++Next;
        if (Next == CharacterCount && !IncludeFinalDirectory)
            break;

        Status = NtfsMasterFileTableGetFileRecordInDirectory(Mft, Directory,
                                                             &Name->Buffer[Index], End - Index,
                                                             &NextDirectory);
        if (!NT_SUCCESS(Status))
        {
            if (Status == STATUS_NOT_FOUND || Status == STATUS_OBJECT_NAME_NOT_FOUND)
                Status = STATUS_OBJECT_PATH_NOT_FOUND;
            break;
        }
        NtfsFileRecordDestroy(Directory);
        Directory = NextDirectory;
        NextDirectory = NULL;
        Index = Next;
    }

Done:
    if (NextDirectory)
        NtfsFileRecordDestroy(NextDirectory);
    if (Directory)
        NtfsFileRecordDestroy(Directory);
    return Status;
}

static
NTSTATUS
NtfsCheckOpenAccess(
    _In_ PNtfsMasterFileTable Mft,
    _In_ PNtfsFileRecord File,
    _In_ PUNICODE_STRING Name,
    _Inout_ PIO_SECURITY_CONTEXT SecurityContext,
    _In_ ULONG Disposition,
    _In_ BOOLEAN CreateStream,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PACCESS_STATE AccessState = SecurityContext->AccessState;
    PNtfsFileRecord Parent = NULL;
    ACCESS_MASK DesiredAccess = SecurityContext->DesiredAccess;
    ACCESS_MASK PreviouslyGrantedAccess;
    ACCESS_MASK GrantedAccess;
    ACCESS_MASK ExtraAccess = CreateStream ? FILE_WRITE_DATA : 0;
    ACCESS_MASK ParentAccess;
    ULONG RemainingNameLength = 0;
    PWCHAR LeafName;
    USHORT ParentLength;
    USHORT LeafLength;
    NTSTATUS Status;

    if (!AccessState)
        return STATUS_SUCCESS;
    PreviouslyGrantedAccess = AccessState->PreviouslyGrantedAccess;
    if (Disposition == FILE_SUPERSEDE)
        ExtraAccess |= DELETE;
    else if (Disposition == FILE_OVERWRITE || Disposition == FILE_OVERWRITE_IF)
        ExtraAccess |= FILE_WRITE_DATA;

    ParentAccess = (DesiredAccess | ExtraAccess) & (DELETE | FILE_READ_ATTRIBUTES);
    if (DesiredAccess & MAXIMUM_ALLOWED)
        ParentAccess |= DELETE | FILE_READ_ATTRIBUTES;
    ParentAccess &= ~PreviouslyGrantedAccess;
    if (ParentAccess &&
        NtfsSplitParentName(Name, &ParentLength, &LeafName, &LeafLength))
    {
        Status = NtfsMasterFileTableGetFileRecordFromQueryEx(Mft,
                                                            Name->Buffer,
                                                            ParentLength,
                                                            TRUE,
                                                            &RemainingNameLength,
                                                            &Parent);
        if (NT_SUCCESS(Status) && RemainingNameLength == 0 && Parent)
        {
            if (ParentAccess & DELETE)
            {
                Status = NtfsCheckFileAccess(Parent, AccessState, FILE_DELETE_CHILD,
                                             0, AccessMode, &GrantedAccess);
                if (NT_SUCCESS(Status))
                    PreviouslyGrantedAccess |= DELETE;
            }
            if (ParentAccess & FILE_READ_ATTRIBUTES)
            {
                Status = NtfsCheckFileAccess(Parent, AccessState, FILE_LIST_DIRECTORY,
                                             0, AccessMode, &GrantedAccess);
                if (NT_SUCCESS(Status))
                    PreviouslyGrantedAccess |= FILE_READ_ATTRIBUTES;
            }
        }
        if (Parent)
            NtfsFileRecordDestroy(Parent);
    }

    Status = NtfsCheckFileAccess(File, AccessState,
                                 (DesiredAccess | ExtraAccess) & ~PreviouslyGrantedAccess,
                                 PreviouslyGrantedAccess,
                                 AccessMode, &GrantedAccess);
    if (NT_SUCCESS(Status))
    {
        if (!(DesiredAccess & MAXIMUM_ALLOWED))
            GrantedAccess &= DesiredAccess | AccessState->PreviouslyGrantedAccess;
        SecurityContext->DesiredAccess = GrantedAccess;
        AccessState->PreviouslyGrantedAccess |= GrantedAccess;
        AccessState->RemainingDesiredAccess &= ~(GrantedAccess | MAXIMUM_ALLOWED);
    }
    return Status;
}

static
NTSTATUS
NtfsCheckNamedStreamDisposition(
    _In_ PNtfsFileRecord File,
    _Inout_ PWSTR StreamName,
    _In_ ULONG Disposition,
    _Out_ PBOOLEAN StreamExisted)
{
    PNtfsDataStreamInformation Streams = NULL;
    UNICODE_STRING RequestedName;
    UNICODE_STRING ExistingName;
    ULONG Capacity = 4;
    ULONG Count;
    ULONG Index;
    NTSTATUS Status;

    *StreamExisted = NtfsFileRecordGetAttribute(File, TypeData, StreamName) != NULL;
    if (!*StreamExisted)
    {
        for (;;)
        {
            if (Capacity > MAXULONG / sizeof(*Streams))
                return STATUS_FILE_TOO_LARGE;
            Streams = ExAllocatePoolWithTag(PagedPool,
                                            Capacity * sizeof(*Streams),
                                            TAG_NTFS);
            if (!Streams)
                return STATUS_INSUFFICIENT_RESOURCES;
            Count = Capacity;
            Status = NtfsFileRecordQueryDataStreams(File, Streams, &Count);
            if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_BUFFER_OVERFLOW)
                break;
            ExFreePoolWithTag(Streams, TAG_NTFS);
            Streams = NULL;
            if (Capacity > MAXULONG / 2)
                return STATUS_FILE_TOO_LARGE;
            Capacity *= 2;
        }
        if (NT_SUCCESS(Status))
        {
            RtlInitUnicodeString(&RequestedName, StreamName);
            for (Index = 0; Index < Count; Index++)
            {
                RtlInitUnicodeString(&ExistingName, Streams[Index].Name);
                if (RtlEqualUnicodeString(&RequestedName, &ExistingName, TRUE))
                {
                    RtlCopyMemory(StreamName, ExistingName.Buffer, ExistingName.Length);
                    *StreamExisted = TRUE;
                    break;
                }
            }
        }
        ExFreePoolWithTag(Streams, TAG_NTFS);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    if (*StreamExisted && Disposition == FILE_CREATE)
        return STATUS_OBJECT_NAME_COLLISION;
    if (!*StreamExisted &&
        (Disposition == FILE_OPEN || Disposition == FILE_OVERWRITE))
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsOpenNamedDataStream(
    _In_ PVolumeContextBlock VolCB,
    _In_ PFileContextBlock FileCB,
    _In_ PFILE_OBJECT FileObject,
    _Inout_ PIO_SECURITY_CONTEXT SecurityContext,
    _In_ ULONG Disposition,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PBOOLEAN Created,
    _Out_ PBOOLEAN Overwritten)
{
    PACCESS_STATE AccessState = SecurityContext->AccessState;
    PNtfsFileRecord IdentityRecord = NULL;
    NtfsFileBasicInformation BasicInformation;
    IO_STATUS_BLOCK FlushStatus;
    LARGE_INTEGER ZeroSize = {{0, 0}};
    ACCESS_MASK GrantedAccess;
    BOOLEAN StreamExisted;
    BOOLEAN PagingAcquired = FALSE;
    BOOLEAN MetadataAcquired = FALSE;
    BOOLEAN Overwrite = Disposition == FILE_SUPERSEDE ||
                        Disposition == FILE_OVERWRITE ||
                        Disposition == FILE_OVERWRITE_IF;
    NTSTATUS Status;

    *Created = FALSE;
    *Overwritten = FALSE;
    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
    if (FileCB->StreamCB->DeletePending)
    {
        Status = STATUS_DELETE_PENDING;
        goto Done;
    }
    if (Overwrite)
    {
        if (!MmCanFileBeTruncated(&FileCB->StreamCB->SectionObjectPointers, &ZeroSize))
        {
            Status = STATUS_USER_MAPPED_FILE;
            goto Done;
        }
        if (FileObject->SectionObjectPointer->SharedCacheMap)
        {
            CcFlushCache(FileObject->SectionObjectPointer, NULL, 0, &FlushStatus);
            Status = FlushStatus.Status;
            if (!NT_SUCCESS(Status))
                goto Done;
        }
    }
    ExAcquireResourceExclusiveLite(NtfsGetPagingIoResource(FileCB), TRUE);
    PagingAcquired = TRUE;
    NtfsAcquireMetadata(VolCB);
    MetadataAcquired = TRUE;
    if (FileCB->DeletePending)
    {
        Status = STATUS_DELETE_PENDING;
        goto Done;
    }
    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    if (VolCB->Dismounting || VolCB->Dismounted)
        Status = STATUS_VOLUME_DISMOUNTED;
    else if (VolCB->VolumeLockOwner)
        Status = STATUS_ACCESS_DENIED;
    else
        Status = STATUS_SUCCESS;
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (!(NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY))
    {
        Status = NtfsMasterFileTableGetFileRecordByReference(
            NtfsVolumeGetMft(VolCB->DiskVolume),
            FileCB->StreamCB->FileReference,
            &IdentityRecord);
        if (NT_SUCCESS(Status) &&
            (NtfsFileRecordGetHeader(IdentityRecord)->Flags & FR_IS_DIRECTORY))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
        }
        if (IdentityRecord)
            NtfsFileRecordDestroy(IdentityRecord);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    Status = NtfsCheckNamedStreamDisposition(FileCB->FileRec,
                                             FileCB->RequestedStream,
                                             Disposition,
                                             &StreamExisted);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = NtfsCheckOpenAccess(NtfsVolumeGetMft(VolCB->DiskVolume),
                                 FileCB->FileRec,
                                 &FileCB->FileName,
                                 SecurityContext,
                                 StreamExisted ? Disposition : FILE_CREATE,
                                 !StreamExisted,
                                 AccessMode);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (!StreamExisted || Overwrite)
    {
        if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
        {
            Status = STATUS_MEDIA_WRITE_PROTECTED;
            goto Done;
        }
        if (!(NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY))
        {
            Status = NtfsFileRecordGetBasicInformation(FileCB->FileRec, &BasicInformation);
            if (NT_SUCCESS(Status) && (BasicInformation.FileAttributes & FILE_ATTRIBUTE_READONLY))
                Status = STATUS_ACCESS_DENIED;
            if (!NT_SUCCESS(Status))
                goto Done;
        }
        if (!StreamExisted)
        {
            if (AccessState && !(AccessState->Flags & TOKEN_HAS_RESTORE_PRIVILEGE))
            {
                Status = NtfsCheckFileAccess(FileCB->FileRec, AccessState,
                                             FILE_WRITE_DATA, 0, AccessMode, &GrantedAccess);
                if (!NT_SUCCESS(Status))
                    goto Done;
            }
            Status = NtfsFileRecordCreateNamedDataStream(FileCB->FileRec,
                                                        FileCB->RequestedStream);
            *Created = NT_SUCCESS(Status);
        }
        else
        {
            Status = NtfsFileRecordSetFileDataSize(FileCB->FileRec, TypeData,
                                                   FileCB->RequestedStream, 0);
            *Overwritten = NT_SUCCESS(Status);
        }
        if (NT_SUCCESS(Status))
        {
            FileCB->StreamCB->SizePending = FALSE;
            InterlockedIncrement(&VolCB->DirGeneration);
            if (*Created)
            {
                NtfsForgetMissingName(VolCB,
                                      FileCB->FileName.Buffer,
                                      FileCB->FileName.Length / sizeof(WCHAR));
            }
        }
    }

Done:
    if (MetadataAcquired)
        NtfsReleaseMetadata(VolCB);
    if (PagingAcquired)
        ExReleaseResourceLite(NtfsGetPagingIoResource(FileCB));
    if (NT_SUCCESS(Status) && (*Created || *Overwritten))
    {
        NtfsRefreshFileSizes(VolCB, FileCB, FileObject);
        if (*Overwritten)
            NtfsPurgeStreamCache(FileCB, FileObject, NULL, 0);
        FileObject->Flags |= FO_FILE_MODIFIED | FO_FILE_SIZE_CHANGED;
    }
    ExReleaseResourceLite(NtfsGetMainResource(FileCB));
    KeLeaveCriticalRegion();
    return Status;
}

static
NTSTATUS
NtfsAssignCreateSecurity(
    _In_ PNtfsMasterFileTable Mft,
    _In_opt_ PNtfsFileRecord Parent,
    _In_ PUNICODE_STRING Name,
    _In_ BOOLEAN IsDirectory,
    _In_opt_ PACCESS_STATE AccessState,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PSECURITY_DESCRIPTOR* NewDescriptor)
{
    PNtfsFileRecord ParentFile = NULL;
    PUCHAR ParentDescriptor = NULL;
    ULONG DescriptorLength = 0;
    ULONG RemainingNameLength = 0;
    ULONG AutoInheritFlags = 0;
    SECURITY_DESCRIPTOR_CONTROL Control = 0;
    SECURITY_DESCRIPTOR_CONTROL ParentControl = 0;
    PWCHAR LeafName;
    USHORT ParentLength;
    USHORT LeafLength;
    ACCESS_MASK GrantedAccess;
    NTSTATUS Status;

    *NewDescriptor = NULL;
    if (!AccessState)
        return STATUS_SUCCESS;

    if (!Parent)
    {
        if (!NtfsSplitParentName(Name, &ParentLength, &LeafName, &LeafLength))
            return STATUS_OBJECT_PATH_INVALID;
        Status = NtfsMasterFileTableGetFileRecordFromQueryEx(Mft, Name->Buffer,
                                                             ParentLength, TRUE,
                                                             &RemainingNameLength, &ParentFile);
        if (!NT_SUCCESS(Status) || RemainingNameLength != 0 || !ParentFile)
        {
            if (ParentFile)
                NtfsFileRecordDestroy(ParentFile);
            return NT_SUCCESS(Status) ? STATUS_OBJECT_PATH_NOT_FOUND : Status;
        }
        Parent = ParentFile;
    }

    Status = NtfsFileRecordReadSecurityDescriptor(Parent, NULL, &DescriptorLength);
    if (Status != STATUS_BUFFER_TOO_SMALL && Status != STATUS_NOT_FOUND)
    {
        if (ParentFile)
            NtfsFileRecordDestroy(ParentFile);
        return Status;
    }
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        ParentDescriptor = (PUCHAR)ExAllocatePoolWithTag(PagedPool, DescriptorLength, TAG_NTFS);
        if (!ParentDescriptor)
        {
            if (ParentFile)
                NtfsFileRecordDestroy(ParentFile);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        Status = NtfsFileRecordReadSecurityDescriptor(Parent, ParentDescriptor, &DescriptorLength);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(ParentDescriptor, TAG_NTFS);
            if (ParentFile)
                NtfsFileRecordDestroy(ParentFile);
            return Status;
        }
    }
    if (ParentFile)
        NtfsFileRecordDestroy(ParentFile);

    if (ParentDescriptor &&
        !(AccessState->Flags & TOKEN_HAS_RESTORE_PRIVILEGE) &&
        !SeAccessCheck(ParentDescriptor,
                       &AccessState->SubjectSecurityContext,
                       FALSE,
                       IsDirectory ? FILE_ADD_SUBDIRECTORY : FILE_ADD_FILE,
                       0,
                       NULL,
                       IoGetFileObjectGenericMapping(),
                       AccessMode,
                       &GrantedAccess,
                       &Status))
    {
        ExFreePoolWithTag(ParentDescriptor, TAG_NTFS);
        return Status;
    }

    if (ParentDescriptor)
        ParentControl = ((PISECURITY_DESCRIPTOR_RELATIVE)ParentDescriptor)->Control;
    if (AccessState->SecurityDescriptor)
        Control = ((PISECURITY_DESCRIPTOR_RELATIVE)AccessState->SecurityDescriptor)->Control;
    if (!(Control & SE_DACL_PRESENT) && (ParentControl & SE_DACL_AUTO_INHERITED))
        AutoInheritFlags |= SEF_DACL_AUTO_INHERIT;
    if (!(Control & SE_SACL_PRESENT) && (ParentControl & SE_SACL_AUTO_INHERITED))
        AutoInheritFlags |= SEF_SACL_AUTO_INHERIT;

    Status = SeAssignSecurityEx(ParentDescriptor,
                                AccessState->SecurityDescriptor,
                                NewDescriptor,
                                NULL,
                                IsDirectory,
                                AutoInheritFlags,
                                &AccessState->SubjectSecurityContext,
                                IoGetFileObjectGenericMapping(),
                                PagedPool);

    if (ParentDescriptor)
        ExFreePoolWithTag(ParentDescriptor, TAG_NTFS);
    return Status;
}

static
BOOLEAN
NtfsTakeCachedLookupParent(
    _In_ PVolumeContextBlock VolCB,
    _In_ PUNICODE_STRING Name,
    _Out_ PNtfsFileRecord* File)
{
    USHORT CachedLength;
    USHORT NameLength;

    *File = NULL;
    if (VolCB->CachedLookupParent &&
        VolCB->CachedLookupParentGeneration != VolCB->DirGeneration)
    {
        NtfsFileRecordDestroy(VolCB->CachedLookupParent);
        VolCB->CachedLookupParent = NULL;
        VolCB->CachedLookupParentPathLength = 0;
    }
    if (!VolCB->CachedLookupParent ||
        !Name ||
        !Name->Buffer)
    {
        return FALSE;
    }

    /* Directory opens may retain one trailing separator in the object name. */
    NameLength = Name->Length / sizeof(WCHAR);
    CachedLength = VolCB->CachedLookupParentPathLength;
    if (!((NameLength == CachedLength) ||
          (NameLength == CachedLength + 1 &&
           Name->Buffer[NameLength - 1] == L'\\')) ||
        RtlCompareMemory(VolCB->CachedLookupParentPath,
                         Name->Buffer,
                         CachedLength * sizeof(WCHAR)) !=
            CachedLength * sizeof(WCHAR))
    {
        return FALSE;
    }

    *File = VolCB->CachedLookupParent;
    VolCB->CachedLookupParent = NULL;
    VolCB->CachedLookupParentPathLength = 0;
    return TRUE;
}

/*
 * A cold miss in one directory should not resolve that directory from the
 * root again. Directory edits advance DirGeneration, making this parsed
 * parent unusable before a later lookup can observe it.
 */
static
BOOLEAN
NtfsLookupInCachedParent(
    _In_ PVolumeContextBlock VolCB,
    _In_ PNtfsMasterFileTable Mft,
    _In_ PUNICODE_STRING Name,
    _In_ BOOLEAN OpenFinalReparsePoint,
    _Out_ PNTSTATUS LookupStatus,
    _Out_ PNtfsFileRecord* File)
{
    PWCHAR LeafName;
    USHORT LeafLength;
    USHORT ParentLength;
    NTSTATUS Status;

    *File = NULL;
    if (!NtfsSplitParentName(Name,
                             &ParentLength,
                             &LeafName,
                             &LeafLength))
    {
        return FALSE;
    }

    if (VolCB->CachedLookupParent &&
        VolCB->CachedLookupParentGeneration != VolCB->DirGeneration)
    {
        NtfsFileRecordDestroy(VolCB->CachedLookupParent);
        VolCB->CachedLookupParent = NULL;
        VolCB->CachedLookupParentPathLength = 0;
    }
    if (!VolCB->CachedLookupParent ||
        VolCB->CachedLookupParentPathLength != ParentLength ||
        RtlCompareMemory(VolCB->CachedLookupParentPath,
                         Name->Buffer,
                         ParentLength * sizeof(WCHAR)) !=
            ParentLength * sizeof(WCHAR))
    {
        return FALSE;
    }

    Status = NtfsMasterFileTableGetFileRecordInDirectory(
        Mft,
        VolCB->CachedLookupParent,
        LeafName,
        LeafLength,
        File);
    if (NT_SUCCESS(Status) &&
        !OpenFinalReparsePoint &&
        NtfsFileRecordGetAttribute(
            *File,
            TypeReparsePoint,
            NULL))
    {
        Status = STATUS_REPARSE;
    }
    *LookupStatus = Status;
    return TRUE;
}

static
VOID
NtfsRememberLookupParent(
    _In_ PVolumeContextBlock VolCB,
    _In_ PNtfsMasterFileTable Mft,
    _In_ PUNICODE_STRING Name)
{
    PNtfsFileRecord Parent = NULL;
    PWCHAR LeafName;
    USHORT LeafLength;
    USHORT ParentLength;
    ULONG RemainingNameLength;
    NTSTATUS Status;

    if (!NtfsSplitParentName(Name,
                             &ParentLength,
                             &LeafName,
                             &LeafLength) ||
        ParentLength > RTL_NUMBER_OF(VolCB->CachedLookupParentPath))
    {
        return;
    }
    UNREFERENCED_PARAMETER(LeafName);
    UNREFERENCED_PARAMETER(LeafLength);

    if (VolCB->CachedLookupParent &&
        VolCB->CachedLookupParentGeneration == VolCB->DirGeneration &&
        VolCB->CachedLookupParentPathLength == ParentLength &&
        RtlCompareMemory(VolCB->CachedLookupParentPath,
                         Name->Buffer,
                         ParentLength * sizeof(WCHAR)) ==
            ParentLength * sizeof(WCHAR))
    {
        return;
    }

    Status = NtfsMasterFileTableGetFileRecordFromQueryEx(
        Mft,
        Name->Buffer,
        ParentLength,
        FALSE,
        &RemainingNameLength,
        &Parent);
    if (!NT_SUCCESS(Status) ||
        !Parent ||
        !(NtfsFileRecordGetHeader(Parent)->Flags & FR_IS_DIRECTORY))
    {
        if (Parent)
            NtfsFileRecordDestroy(Parent);
        return;
    }

    if (VolCB->CachedLookupParent)
        NtfsFileRecordDestroy(VolCB->CachedLookupParent);
    VolCB->CachedLookupParent = Parent;
    VolCB->CachedLookupParentGeneration = VolCB->DirGeneration;
    VolCB->CachedLookupParentPathLength = ParentLength;
    RtlCopyMemory(VolCB->CachedLookupParentPath,
                  Name->Buffer,
                  ParentLength * sizeof(WCHAR));
}

PStreamContextBlock
NtfsReferenceStreamContext(
    _In_ PVolumeContextBlock VolCB,
    _In_ PNtfsFileRecord File,
    _In_ AttributeType RequestedType,
    _In_opt_ PWSTR RequestedStream)
{
    PFileRecordHeader Header;
    PStreamContextBlock StreamCB;
    PLIST_ENTRY Entry;
    ULONGLONG FileReference;
    UNICODE_STRING StreamName;
    UNICODE_STRING BaseStreamName;
    PAttribute DataAttribute;
    BOOLEAN FileDeletePending = FALSE;
    BOOLEAN FileDeleted = FALSE;
    AttributeType BaseType;

    if (!VolCB || !File)
        return NULL;

    Header = NtfsFileRecordGetHeader(File);
    if (!Header || Header->SequenceNumber == 0)
        return NULL;

    FileReference =
        ((ULONGLONG)Header->SequenceNumber << 48) |
        Header->MFTRecordNumber;
    RtlInitUnicodeString(&StreamName, RequestedStream);
    BaseType = Header->Flags & FR_IS_DIRECTORY ? TypeIndexAllocation : TypeData;
    RtlInitUnicodeString(&BaseStreamName, Header->Flags & FR_IS_DIRECTORY ? L"$I30" : NULL);

    ExAcquireFastMutex(&VolCB->StreamListMutex);
    for (Entry = VolCB->StreamList.Flink;
         Entry != &VolCB->StreamList;
         Entry = Entry->Flink)
    {
        StreamCB = CONTAINING_RECORD(Entry, StreamContextBlock, ListEntry);
        if (StreamCB->FileReference == FileReference)
        {
            if (StreamCB->RequestedType == BaseType &&
                RtlEqualUnicodeString(&StreamCB->RequestedStream, &BaseStreamName, TRUE))
            {
                FileDeletePending |= StreamCB->DeletePending;
                FileDeleted |= StreamCB->Deleted;
            }
        }
        if (StreamCB->FileReference == FileReference &&
            StreamCB->RequestedType == RequestedType &&
            RtlEqualUnicodeString(&StreamCB->RequestedStream,
                                  &StreamName,
                                  TRUE))
        {
            StreamCB->ReferenceCount++;
            ExReleaseFastMutex(&VolCB->StreamListMutex);
            return StreamCB;
        }
    }

    StreamCB = (PStreamContextBlock)ExAllocatePoolZero(NonPagedPool,
                                                        sizeof(*StreamCB),
                                                        TAG_NTFS);
    if (!StreamCB)
    {
        ExReleaseFastMutex(&VolCB->StreamListMutex);
        return NULL;
    }

    if (StreamName.Length != 0)
    {
        StreamCB->RequestedStream.MaximumLength =
            StreamName.Length + sizeof(WCHAR);
        StreamCB->RequestedStream.Buffer =
            (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                         StreamCB->RequestedStream.MaximumLength,
                                         TAG_NTFS);
        if (!StreamCB->RequestedStream.Buffer)
        {
            ExFreePool(StreamCB);
            ExReleaseFastMutex(&VolCB->StreamListMutex);
            return NULL;
        }
        RtlCopyMemory(StreamCB->RequestedStream.Buffer,
                      StreamName.Buffer,
                      StreamName.Length);
        StreamCB->RequestedStream.Buffer[
            StreamName.Length / sizeof(WCHAR)] = UNICODE_NULL;
        StreamCB->RequestedStream.Length = StreamName.Length;
    }

    StreamCB->FileReference = FileReference;
    StreamCB->DeletePending = FileDeletePending;
    StreamCB->Deleted = FileDeleted;
    StreamCB->RequestedType = RequestedType;
    StreamCB->ReferenceCount = 1;
    InitializeListHead(&StreamCB->NativeScb.CcbList);
    InitializeListHead(&StreamCB->NativeScb.ChildLcbList);
    if (!NT_SUCCESS(ExInitializeResourceLite(&StreamCB->MainResource)))
    {
        if (StreamCB->RequestedStream.Buffer)
            ExFreePool(StreamCB->RequestedStream.Buffer);
        ExFreePool(StreamCB);
        ExReleaseFastMutex(&VolCB->StreamListMutex);
        return NULL;
    }
    if (!NT_SUCCESS(ExInitializeResourceLite(&StreamCB->PagingIoResource)))
    {
        ExDeleteResourceLite(&StreamCB->MainResource);
        if (StreamCB->RequestedStream.Buffer)
            ExFreePool(StreamCB->RequestedStream.Buffer);
        ExFreePool(StreamCB);
        ExReleaseFastMutex(&VolCB->StreamListMutex);
        return NULL;
    }
    ExInitializeFastMutex(&StreamCB->HeaderMutex);
    FsRtlSetupAdvancedHeader(&StreamCB->CommonFCBHeader, &StreamCB->HeaderMutex);
    StreamCB->CommonFCBHeader.Resource = &StreamCB->MainResource;
    StreamCB->CommonFCBHeader.PagingIoResource = &StreamCB->PagingIoResource;
    StreamCB->CommonFCBHeader.IsFastIoPossible = FastIoIsPossible;
    FsRtlInitializeFileLock(&StreamCB->FileLock, NULL, NULL);
    DataAttribute = NtfsFileRecordGetAttribute(File, RequestedType, RequestedStream);
    if (DataAttribute)
    {
        if (DataAttribute->IsNonResident)
        {
            StreamCB->CommonFCBHeader.AllocationSize.QuadPart = NtfsAttributeGetPhysicalAllocationSize(DataAttribute);
            StreamCB->CommonFCBHeader.FileSize.QuadPart = DataAttribute->NonResident.DataSize;
            StreamCB->CommonFCBHeader.ValidDataLength.QuadPart = DataAttribute->NonResident.InitalizedDataSize;
        }
        else
        {
            StreamCB->CommonFCBHeader.AllocationSize.QuadPart = DataAttribute->Resident.DataLength;
            StreamCB->CommonFCBHeader.FileSize.QuadPart = DataAttribute->Resident.DataLength;
            StreamCB->CommonFCBHeader.ValidDataLength.QuadPart = DataAttribute->Resident.DataLength;
        }
    }
    InsertTailList(&VolCB->StreamList, &StreamCB->ListEntry);
    ExReleaseFastMutex(&VolCB->StreamListMutex);
    return StreamCB;
}

VOID
NtfsDereferenceStreamContext(
    _In_ PVolumeContextBlock VolCB,
    _In_ PStreamContextBlock StreamCB)
{
    BOOLEAN FreeContext = FALSE;

    ExAcquireFastMutex(&VolCB->StreamListMutex);
    ASSERT(StreamCB->ReferenceCount > 0);
    StreamCB->ReferenceCount--;
    if (StreamCB->ReferenceCount == 0)
    {
        RemoveEntryList(&StreamCB->ListEntry);
        FreeContext = TRUE;
    }
    ExReleaseFastMutex(&VolCB->StreamListMutex);

    if (FreeContext)
    {
        ASSERT(IsListEmpty(&StreamCB->NativeScb.CcbList));
        ASSERT(IsListEmpty(&StreamCB->NativeScb.ChildLcbList));
        FsRtlUninitializeFileLock(&StreamCB->FileLock);
        ExDeleteResourceLite(&StreamCB->MainResource);
        ExDeleteResourceLite(&StreamCB->PagingIoResource);
        if (StreamCB->RequestedStream.Buffer)
            ExFreePool(StreamCB->RequestedStream.Buffer);
        ExFreePool(StreamCB);
    }
}

NTSTATUS
NtfsReferenceNameParent(
    _In_ PVolumeContextBlock VolCB,
    _In_ PUNICODE_STRING Name,
    _Out_ PStreamContextBlock* ParentStream,
    _Out_ PUNICODE_STRING LeafName)
{
    PNtfsMasterFileTable Mft = NtfsVolumeGetMft(VolCB->DiskVolume);
    PNtfsFileRecord Parent = NULL;
    USHORT ParentLength;
    USHORT LeafLength;
    USHORT Index;
    ULONG RemainingLength = 0;
    BOOLEAN Borrowed = FALSE;
    NTSTATUS Status;

    *ParentStream = NULL;
    RtlZeroMemory(LeafName, sizeof(*LeafName));
    if (!NtfsSplitParentName(Name, &ParentLength, &LeafName->Buffer, &LeafLength))
        return STATUS_OBJECT_PATH_INVALID;
    for (Index = 0; Index < LeafLength; Index++)
    {
        if (LeafName->Buffer[Index] == L':')
            break;
    }
    if (Index == 0)
        return STATUS_OBJECT_NAME_INVALID;
    LeafName->Length = LeafName->MaximumLength = Index * sizeof(WCHAR);
    NtfsRememberLookupParent(VolCB, Mft, Name);
    if (VolCB->CachedLookupParent &&
        VolCB->CachedLookupParentGeneration == VolCB->DirGeneration &&
        VolCB->CachedLookupParentPathLength == ParentLength &&
        RtlCompareMemory(VolCB->CachedLookupParentPath, Name->Buffer,
                         ParentLength * sizeof(WCHAR)) == ParentLength * sizeof(WCHAR))
    {
        Parent = VolCB->CachedLookupParent;
        Borrowed = TRUE;
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = NtfsMasterFileTableGetFileRecordFromQueryEx(
            Mft, Name->Buffer, ParentLength, FALSE, &RemainingLength, &Parent);
    }
    if (NT_SUCCESS(Status) && (!Parent || RemainingLength != 0))
        Status = STATUS_OBJECT_PATH_NOT_FOUND;
    if (NT_SUCCESS(Status) && !(NtfsFileRecordGetHeader(Parent)->Flags & FR_IS_DIRECTORY))
        Status = STATUS_NOT_A_DIRECTORY;
    if (NT_SUCCESS(Status))
    {
        *ParentStream = NtfsReferenceStreamContext(VolCB, Parent, TypeIndexAllocation, L"$I30");
        if (!*ParentStream)
            Status = STATUS_INSUFFICIENT_RESOURCES;
    }
    if (Parent && !Borrowed)
        NtfsFileRecordDestroy(Parent);
    return Status;
}

NTSTATUS
NtfsFindOpenLink(_In_ PVolumeContextBlock VolCB,
                 _In_ PFileContextBlock FileCB,
                 _Out_ PNTFS_NATIVE_LCB* Link)
{
    PStreamContextBlock ParentStream;
    UNICODE_STRING LeafName;
    UNICODE_STRING LinkName;
    PLIST_ENTRY Entry;
    NTSTATUS Status;

    *Link = FileCB->NativeCcb.Lcb;
    if (*Link)
        return STATUS_SUCCESS;
    Status = NtfsReferenceNameParent(VolCB, &FileCB->FileName, &ParentStream, &LeafName);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = NtfsMasterFileTableGetLinkName(NtfsVolumeGetMft(VolCB->DiskVolume),
                                           FileCB->FileRec, ParentStream->FileReference,
                                           &LeafName, &LinkName);
    if (NT_SUCCESS(Status))
    {
        for (Entry = ParentStream->NativeScb.ChildLcbList.Flink;
             Entry != &ParentStream->NativeScb.ChildLcbList;
             Entry = Entry->Flink)
        {
            PNTFS_NATIVE_LCB Lcb = CONTAINING_RECORD(Entry, NTFS_NATIVE_LCB, ParentEntry);
            PNTFS_NATIVE_CCB Ccb;
            PFileContextBlock Other;

            ASSERT(!IsListEmpty(&Lcb->CcbList));
            Ccb = CONTAINING_RECORD(Lcb->CcbList.Flink, NTFS_NATIVE_CCB, LcbEntry);
            Other = CONTAINING_RECORD(Ccb, FileContextBlock, NativeCcb);
            if (Other->StreamCB->FileReference == FileCB->StreamCB->FileReference &&
                RtlEqualUnicodeString(&Lcb->FileName, &LinkName, FALSE))
            {
                *Link = Lcb;
                break;
            }
        }
    }
    NtfsDereferenceStreamContext(VolCB, ParentStream);
    return Status;
}

static
NTSTATUS
NtfsRegisterOpenLink(
    _In_ PVolumeContextBlock VolCB,
    _In_ PFileContextBlock FileCB,
    _In_ PFILE_OBJECT FileObject)
{
    PStreamContextBlock ParentStream = NULL;
    PNTFS_NATIVE_LCB Lcb = NULL;
    PNTFS_NATIVE_CCB Ccb = &FileCB->NativeCcb;
    PLIST_ENTRY Entry;
    UNICODE_STRING LeafName;
    UNICODE_STRING LinkName;
    NTSTATUS Status;

    if (!(FileCB->CreateOptions & FILE_OPEN_BY_FILE_ID) &&
        FileCB->FileName.Length > sizeof(WCHAR))
    {
        Status = NtfsReferenceNameParent(VolCB, &FileCB->FileName, &ParentStream, &LeafName);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = NtfsMasterFileTableGetLinkName(NtfsVolumeGetMft(VolCB->DiskVolume),
                                               FileCB->FileRec, ParentStream->FileReference,
                                               &LeafName, &LinkName);
        if (!NT_SUCCESS(Status))
            goto Failure;
        for (Entry = ParentStream->NativeScb.ChildLcbList.Flink;
             Entry != &ParentStream->NativeScb.ChildLcbList;
             Entry = Entry->Flink)
        {
            PNTFS_NATIVE_LCB Candidate = CONTAINING_RECORD(Entry, NTFS_NATIVE_LCB, ParentEntry);
            PNTFS_NATIVE_CCB ExistingCcb;
            PFileContextBlock ExistingFile;

            ASSERT(!IsListEmpty(&Candidate->CcbList));
            ExistingCcb = CONTAINING_RECORD(Candidate->CcbList.Flink, NTFS_NATIVE_CCB, LcbEntry);
            ExistingFile = CONTAINING_RECORD(ExistingCcb, FileContextBlock, NativeCcb);
            if (ExistingFile->StreamCB->FileReference == FileCB->StreamCB->FileReference &&
                RtlEqualUnicodeString(&Candidate->FileName, &LinkName, FALSE))
            {
                if (ExistingFile->DeletePending)
                {
                    Status = STATUS_DELETE_PENDING;
                    goto Failure;
                }
                Lcb = Candidate;
                break;
            }
        }
        if (Lcb)
        {
            NtfsDereferenceStreamContext(VolCB, ParentStream);
            ParentStream = NULL;
            if (Lcb->CleanupCount == MAXULONG)
                return STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            Lcb = ExAllocatePoolZero(NonPagedPool, sizeof(*Lcb), TAG_NTFS);
            if (!Lcb)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Failure;
            }
            Lcb->FileName.Buffer = ExAllocatePoolWithTag(PagedPool, LinkName.Length, TAG_NTFS);
            if (!Lcb->FileName.Buffer)
            {
                ExFreePoolWithTag(Lcb, TAG_NTFS);
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Failure;
            }
            RtlCopyMemory(Lcb->FileName.Buffer, LinkName.Buffer, LinkName.Length);
            Lcb->FileName.Length = Lcb->FileName.MaximumLength = LinkName.Length;
            Lcb->ParentScb = &ParentStream->NativeScb;
            InitializeListHead(&Lcb->CcbList);
            InsertTailList(&ParentStream->NativeScb.ChildLcbList, &Lcb->ParentEntry);
        }
        InsertTailList(&Lcb->CcbList, &Ccb->LcbEntry);
        Lcb->CleanupCount++;
    }
    Ccb->Lcb = Lcb;
    Ccb->FileName = FileCB->FileName;
    Ccb->FileObject = FileObject;
    InsertTailList(&FileCB->StreamCB->NativeScb.CcbList, &Ccb->StreamEntry);
    return STATUS_SUCCESS;

Failure:
    if (ParentStream)
        NtfsDereferenceStreamContext(VolCB, ParentStream);
    return Status;
}

VOID
NtfsSetLinkDeletePending(_In_ PFileContextBlock FileCB,
                         _In_ BOOLEAN DeletePending)
{
    PNTFS_NATIVE_LCB Lcb = FileCB->NativeCcb.Lcb;
    PLIST_ENTRY Entry;

    FileCB->DeletePending = DeletePending;
    if (!Lcb)
        return;
    for (Entry = Lcb->CcbList.Flink; Entry != &Lcb->CcbList; Entry = Entry->Flink)
    {
        PNTFS_NATIVE_CCB Ccb = CONTAINING_RECORD(Entry, NTFS_NATIVE_CCB, LcbEntry);
        PFileContextBlock Other = CONTAINING_RECORD(Ccb, FileContextBlock, NativeCcb);

        Other->DeletePending = DeletePending;
        if (Ccb->FileObject)
            Ccb->FileObject->DeletePending = DeletePending ||
                (Other->StreamCB && Other->StreamCB->DeletePending);
    }
}

VOID
NtfsRemoveOpenLink(_In_ PVolumeContextBlock VolCB,
                   _In_ PFileContextBlock FileCB)
{
    PNTFS_NATIVE_CCB Ccb = &FileCB->NativeCcb;
    PNTFS_NATIVE_LCB Lcb;

    KeEnterCriticalRegion();
    NtfsAcquireMetadata(VolCB);
    if (Ccb->StreamEntry.Flink)
    {
        RemoveEntryList(&Ccb->StreamEntry);
        Ccb->StreamEntry.Flink = Ccb->StreamEntry.Blink = NULL;
        Lcb = Ccb->Lcb;
        if (Lcb)
        {
            if (!FileCB->CleanupComplete)
            {
                ASSERT(Lcb->CleanupCount != 0);
                Lcb->CleanupCount--;
            }
            RemoveEntryList(&Ccb->LcbEntry);
            if (IsListEmpty(&Lcb->CcbList))
            {
                PStreamContextBlock ParentStream =
                    CONTAINING_RECORD(Lcb->ParentScb, StreamContextBlock, NativeScb);

                ASSERT(Lcb->CleanupCount == 0);
                RemoveEntryList(&Lcb->ParentEntry);
                ExFreePoolWithTag(Lcb->FileName.Buffer, TAG_NTFS);
                ExFreePoolWithTag(Lcb, TAG_NTFS);
                NtfsDereferenceStreamContext(VolCB, ParentStream);
            }
        }
        Ccb->Lcb = NULL;
        Ccb->FileObject = NULL;
    }
    NtfsReleaseMetadata(VolCB);
    KeLeaveCriticalRegion();
}

NTSTATUS
NtfsCheckDirectoryOpenChildren(_In_ PVolumeContextBlock VolCB,
                               _In_ ULONGLONG DirectoryReference)
{
    PLIST_ENTRY Entry;
    ULONGLONG* Parents;
    SIZE_T Count = 0;
    SIZE_T Index = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    ExAcquireFastMutex(&VolCB->StreamListMutex);
    for (Entry = VolCB->StreamList.Flink; Entry != &VolCB->StreamList; Entry = Entry->Flink)
        Count++;
    ExReleaseFastMutex(&VolCB->StreamListMutex);
    if (Count == 0)
        return STATUS_SUCCESS;
    if (Count > MAXULONG_PTR / sizeof(*Parents))
        return STATUS_INSUFFICIENT_RESOURCES;
    Parents = ExAllocatePoolWithTag(PagedPool, Count * sizeof(*Parents), TAG_NTFS);
    if (!Parents)
        return STATUS_INSUFFICIENT_RESOURCES;
    ExAcquireFastMutex(&VolCB->StreamListMutex);
    for (Entry = VolCB->StreamList.Flink; Entry != &VolCB->StreamList; Entry = Entry->Flink)
    {
        PStreamContextBlock Stream = CONTAINING_RECORD(Entry, StreamContextBlock, ListEntry);
        PLIST_ENTRY Child;

        for (Child = Stream->NativeScb.ChildLcbList.Flink;
             Child != &Stream->NativeScb.ChildLcbList;
             Child = Child->Flink)
        {
            PNTFS_NATIVE_LCB Lcb = CONTAINING_RECORD(Child, NTFS_NATIVE_LCB, ParentEntry);

            if (Lcb->CleanupCount != 0)
            {
                ASSERT(Index < Count);
                Parents[Index++] = Stream->FileReference;
                break;
            }
        }
    }
    ExReleaseFastMutex(&VolCB->StreamListMutex);
    Count = Index;
    for (Index = 0; Index < Count; Index++)
    {
        BOOLEAN Descendant;

        Status = NtfsMasterFileTableIsDescendantDirectory(NtfsVolumeGetMft(VolCB->DiskVolume),
                                                        Parents[Index], DirectoryReference,
                                                        &Descendant);
        if (!NT_SUCCESS(Status))
            break;
        if (Descendant)
        {
            Status = STATUS_ACCESS_DENIED;
            break;
        }
    }
    ExFreePoolWithTag(Parents, TAG_NTFS);
    return Status;
}

static
NTSTATUS
NtfsCompleteFailedCreate(
    _In_ PDEVICE_OBJECT VolumeDeviceObject,
    _Inout_ PIRP Irp,
    _In_opt_ PFileContextBlock FileCB,
    _In_opt_ PNtfsFileRecord FileRecord,
    _In_opt_ PNtfsCachedRecord CachedRecord,
    _In_ NTSTATUS Status)
{
    if (FileCB)
    {
        NtfsCleanupFailedCreate((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension,
                                 FileCB, IoGetCurrentIrpStackLocation(Irp)->FileObject);
    }

    /* A record on loan from the cache is returned, never destroyed here. */
    if (CachedRecord)
    {
        if (FileCB && FileCB->FileRec == CachedRecord->Record)
            FileCB->FileRec = NULL;
        if (FileRecord == CachedRecord->Record)
            FileRecord = NULL;
        NtfsReleaseCachedRecord(
            (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension,
            CachedRecord);
    }

    if (FileCB)
    {
        NtfsRemoveOpenLink((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension, FileCB);
        if (FileCB->FileDir)
        {
            if (FileCB->FileDirBorrowed)
            {
                PVolumeContextBlock Vol =
                    (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;

                ExAcquireFastMutex(&Vol->DirCacheMutex);
                Vol->CachedDirBusy = FALSE;
                ExReleaseFastMutex(&Vol->DirCacheMutex);
            }
            else
            {
                NtfsDirectoryDestroy(FileCB->FileDir);
            }
        }
        if (FileCB->StreamCB)
        {
            if (FileCB->ShareAccessSet)
            {
                PVolumeContextBlock Vol =
                    (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;

                ExAcquireFastMutex(&Vol->StreamListMutex);
                IoRemoveShareAccess(IoGetCurrentIrpStackLocation(Irp)->FileObject,
                                    &FileCB->StreamCB->ShareAccess);
                ExReleaseFastMutex(&Vol->StreamListMutex);
                FileCB->ShareAccessSet = FALSE;
                InterlockedDecrement(&FileCB->StreamCB->UncleanCount);
            }
            NtfsDereferenceStreamContext(
                (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension,
                FileCB->StreamCB);
        }
        if (FileCB->RequestedStream)
            ExFreePool(FileCB->RequestedStream);
        if (FileCB->FileName.Buffer &&
            FileCB->FileName.Buffer != FileCB->InlineFileName)
            ExFreePool(FileCB->FileName.Buffer);
        if (FileCB->FileRec)
        {
            if (FileCB->FileRec == FileRecord)
                FileRecord = NULL;
            NtfsFileRecordDestroy(FileCB->FileRec);
        }
        ExDeleteResourceLite(&FileCB->MainResource);
        ExDeleteResourceLite(&FileCB->PagingIoResource);
        ExFreePool(FileCB);
    }
    if (FileRecord)
        NtfsFileRecordDestroy(FileRecord);

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

static
NTSTATUS
NtfsOpenVolume(
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp,
    _In_ PVolumeContextBlock VolCB)
{
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PIO_SECURITY_CONTEXT SecurityContext =
        IrpSp->Parameters.Create.SecurityContext;
    PFileContextBlock FileCB;
    ULONG CreateOptions;
    UCHAR Disposition;
    NTSTATUS Status;

    CreateOptions = GetCreateOptions(IrpSp->Parameters.Create.Options);
    Disposition = GetDisposition(IrpSp->Parameters.Create.Options);
    if (Disposition != FILE_OPEN && Disposition != FILE_OPEN_IF)
        Status = STATUS_ACCESS_DENIED;
    else if (CreateOptions & FILE_DIRECTORY_FILE)
        Status = STATUS_NOT_A_DIRECTORY;
    else if (CreateOptions & FILE_DELETE_ON_CLOSE)
        Status = STATUS_CANNOT_DELETE;
    else if (IrpSp->Flags & SL_OPEN_TARGET_DIRECTORY)
        Status = STATUS_INVALID_PARAMETER;
    else
        Status = STATUS_SUCCESS;

    if (!NT_SUCCESS(Status))
        goto Complete;

    FileCB = (PFileContextBlock)ExAllocatePoolZero(NonPagedPool,
                                                   sizeof(*FileCB),
                                                   TAG_NTFS);
    if (!FileCB)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Complete;
    }

    Status = ExInitializeResourceLite(&FileCB->MainResource);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(FileCB, TAG_NTFS);
        goto Complete;
    }
    Status = ExInitializeResourceLite(&FileCB->PagingIoResource);
    if (!NT_SUCCESS(Status))
    {
        ExDeleteResourceLite(&FileCB->MainResource);
        ExFreePoolWithTag(FileCB, TAG_NTFS);
        goto Complete;
    }
    ExInitializeFastMutex(&FileCB->HeaderMutex);
    FsRtlSetupAdvancedHeader(&FileCB->CommonFCBHeader,
                             &FileCB->HeaderMutex);
    FileCB->CommonFCBHeader.Resource = &FileCB->MainResource;
    FileCB->CommonFCBHeader.PagingIoResource =
        &FileCB->PagingIoResource;
    FileCB->CommonFCBHeader.IsFastIoPossible =
        FastIoIsNotPossible;
    FileCB->IsVolumeOpen = TRUE;
    FileCB->CreateOptions = IrpSp->Parameters.Create.Options;
    FileCB->DesiredAccess = SecurityContext->DesiredAccess;
    if (SecurityContext->AccessState)
    {
        FileCB->DesiredAccess |=
            SecurityContext->AccessState->PreviouslyGrantedAccess;
    }

    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    if (VolCB->Dismounting || VolCB->Dismounted)
    {
        Status = STATUS_VOLUME_DISMOUNTED;
    }
    else if (VolCB->VolumeLockOwner)
    {
        Status = STATUS_ACCESS_DENIED;
    }
    else if (VolCB->VolumeHandleCount == 0)
    {
        IoSetShareAccess(FileCB->DesiredAccess,
                         IrpSp->Parameters.Create.ShareAccess,
                         FileObject,
                         &VolCB->VolumeShareAccess);
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = IoCheckShareAccess(
            FileCB->DesiredAccess,
            IrpSp->Parameters.Create.ShareAccess,
            FileObject,
            &VolCB->VolumeShareAccess,
            TRUE);
    }

    if (NT_SUCCESS(Status))
    {
        VolCB->VolumeHandleCount++;
        VolCB->OpenHandleCount++;
        FileObject->FsContext = FileCB;
    }
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);

    if (!NT_SUCCESS(Status))
    {
        ExDeleteResourceLite(&FileCB->MainResource);
        ExDeleteResourceLite(&FileCB->PagingIoResource);
        ExFreePoolWithTag(FileCB, TAG_NTFS);
        goto Complete;
    }

    Irp->IoStatus.Information = FILE_OPENED;

Complete:
    if (!NT_SUCCESS(Status))
        Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
NtfsFsdCreate(_In_ PDEVICE_OBJECT VolumeDeviceObject,
              _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Handle creation or opening of a file, device, directory, or volume.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-create
     */

    PIO_STACK_LOCATION IrpSp;
    PFileContextBlock FileCB;
    NTSTATUS Status;
    PFILE_OBJECT FileObject;
    PNtfsFileRecord CurrentFile = NULL;
    PNtfsCachedRecord CachedRecord = NULL;
    UINT8 Disposition;
    PVolumeContextBlock VolCB;
    PNtfsVolume DiskVolume;
    PNtfsMasterFileTable Mft;
    ULONG CreateOptions;
    ULONG RemainingNameLength = 0;
    LONG ResolvedGeneration;
    ULONG FileAttributes;
    USHORT FileNameLength;
    USHORT TraverseNameOffset = 0;
    USHORT TargetLeafLength = 0;
    USHORT TargetParentLength = 0;
    ULONGLONG TraverseRelatedReference = 0;
    BOOLEAN ExternalBackingDeleted = FALSE;
    BOOLEAN DirectoryMetadataAcquired = FALSE;
    BOOLEAN CachedParentLookup = FALSE;
    BOOLEAN OpenTargetDirectory;
    BOOLEAN FileExisted = TRUE;
    BOOLEAN Overwritten = FALSE;
    BOOLEAN NamedDataStream = FALSE;
    BOOLEAN StreamExisted = TRUE;
    BOOLEAN StreamCreated = FALSE;

    if (VolumeDeviceObject == NtfsDiskFileSystemDeviceObject)
    {
        /* DeviceObject represents FileSystem instead of logical volume */
        Irp->IoStatus.Information = FILE_OPENED;
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
        return STATUS_SUCCESS;
    }

    // Investigate file request
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IrpSp->FileObject;
    Disposition = GetDisposition(IrpSp->Parameters.Create.Options);
    CreateOptions = GetCreateOptions(IrpSp->Parameters.Create.Options);
    OpenTargetDirectory = BooleanFlagOn(IrpSp->Flags, SL_OPEN_TARGET_DIRECTORY);
    FileAttributes =
        IrpSp->Parameters.Create.FileAttributes &
        FILE_ATTRIBUTE_VALID_FLAGS;
    if (FileAttributes & ~FILE_ATTRIBUTE_NORMAL)
        FileAttributes &= ~FILE_ATTRIBUTE_NORMAL;
    if (!(CreateOptions & FILE_DIRECTORY_FILE))
        FileAttributes = (FileAttributes & ~FILE_ATTRIBUTE_NORMAL) | FILE_ATTRIBUTE_ARCHIVE;
    VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;

    if ((CreateOptions & (FILE_DIRECTORY_FILE | FILE_NON_DIRECTORY_FILE)) ==
        (FILE_DIRECTORY_FILE | FILE_NON_DIRECTORY_FILE))
    {
        return NtfsCompleteCreate(Irp, STATUS_INVALID_PARAMETER, 0);
    }

    if (IrpSp->Parameters.Create.Options & FILE_OPEN_BY_FILE_ID)
        Status = NtfsResolveFileIdName(VolCB, FileObject);
    else
    {
        USHORT RelativeNameLength = FileObject->FileName.Length;

        KeEnterCriticalRegion();
        NtfsAcquireMetadata(VolCB);
        Status = NtfsNormalizeRelatedName(FileObject);
        if (NT_SUCCESS(Status) && FileObject->RelatedFileObject)
        {
            PFileContextBlock RelatedFileCB = NtfsGetFileContext(FileObject->RelatedFileObject);

            if (!RelatedFileCB->IsVolumeOpen && RelatedFileCB->FileRec)
            {
                PFileRecordHeader RelatedHeader = NtfsFileRecordGetHeader(RelatedFileCB->FileRec);

                TraverseRelatedReference = ((ULONGLONG)RelatedHeader->SequenceNumber << 48) |
                                             RelatedHeader->MFTRecordNumber;
                TraverseNameOffset = (FileObject->FileName.Length - RelativeNameLength) / sizeof(WCHAR);
            }
        }
        NtfsReleaseMetadata(VolCB);
        KeLeaveCriticalRegion();
    }
    if (!NT_SUCCESS(Status))
        return NtfsCompleteCreate(Irp, Status, 0);

    if (FileObject->FileName.Length == 0 &&
        (FileObject->RelatedFileObject == NULL ||
         NtfsGetFileContext(FileObject->RelatedFileObject)->IsVolumeOpen))
    {
        return NtfsOpenVolume(Irp,
                              IrpSp,
                              VolCB);
    }

    Status = NtfsValidateCreateName(&FileObject->FileName);
    if (!NT_SUCCESS(Status))
        return NtfsCompleteCreate(Irp, Status, 0);

    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    if (VolCB->Dismounting || VolCB->Dismounted)
        Status = STATUS_VOLUME_DISMOUNTED;
    else if (VolCB->VolumeLockOwner)
        Status = STATUS_ACCESS_DENIED;
    else
        Status = STATUS_SUCCESS;
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
        return Status;
    }

    DiskVolume = VolCB->DiskVolume;
    Mft = NtfsVolumeGetMft(DiskVolume);

    // TODO: Check if we have rights to access file.

    // Try to find the requested file record.
    if ((FileObject->FileName.Length &
         (sizeof(WCHAR) - 1)) != 0)
    {
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status =
            STATUS_OBJECT_NAME_INVALID;
        IoCompleteRequest(Irp,
                          IO_DISK_INCREMENT);
        return STATUS_OBJECT_NAME_INVALID;
    }
    if (OpenTargetDirectory)
    {
        PWCHAR TargetLeafName;

        if (!NtfsSplitParentName(&FileObject->FileName, &TargetParentLength, &TargetLeafName, &TargetLeafLength))
            return NtfsCompleteCreate(Irp, STATUS_OBJECT_NAME_INVALID, 0);
    }
    KeEnterCriticalRegion();
    NtfsAcquireMetadata(VolCB);
    if (!(CreateOptions & FILE_OPEN_BY_FILE_ID))
    {
        Status = NtfsCheckTraverseAccess(Mft, &FileObject->FileName,
                                         TraverseRelatedReference, TraverseNameOffset,
                                         FALSE, IrpSp->Parameters.Create.SecurityContext->AccessState,
                                         (IrpSp->Flags & SL_FORCE_ACCESS_CHECK) ? UserMode : Irp->RequestorMode);
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteCreate(Irp, Status, 0);
        }
    }
    if (!OpenTargetDirectory)
    {
        AttributeType RequestedType;
        PWSTR RequestedStream = NULL;

        Status = NtfsVolumeGetADSPreference(DiskVolume, &FileObject->FileName,
                                            &RequestedType, &RequestedStream);
        if (NT_SUCCESS(Status) &&
            RequestedType != TypeData && RequestedType != TypeIndexAllocation)
        {
            Status = STATUS_ACCESS_DENIED;
        }
        if (NT_SUCCESS(Status) && RequestedType == TypeIndexAllocation && RequestedStream)
        {
            UNICODE_STRING StreamName;
            UNICODE_STRING IndexName = RTL_CONSTANT_STRING(L"$I30");

            RtlInitUnicodeString(&StreamName, RequestedStream);
            if (!RtlEqualUnicodeString(&StreamName, &IndexName, TRUE))
                Status = STATUS_INVALID_PARAMETER;
        }
        if (RequestedStream)
            ExFreePool(RequestedStream);
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteCreate(Irp, Status, 0);
        }
    }
    /*
     * An open that only ever fails still costs a full index walk, so answer
     * from the missing-name cache when the path is already known absent.
     */
    if (!OpenTargetDirectory &&
        !(IrpSp->Parameters.Create.Options & FILE_OPEN_REPARSE_POINT) &&
        Disposition == FILE_OPEN &&
        NtfsIsNameKnownMissing(VolCB,
                               FileObject->FileName.Buffer,
                               FileObject->FileName.Length / sizeof(WCHAR)))
    {
        Status = NtfsTranslateNotFoundStatus(Mft, &FileObject->FileName);
        NtfsReleaseMetadata(VolCB);
        KeLeaveCriticalRegion();
        Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
        return Status;
    }

    if (OpenTargetDirectory)
    {
        PNtfsFileRecord TargetFile = NULL;
        ULONG TargetRemainingNameLength = 0;

        Status = NtfsMasterFileTableGetFileRecordFromQueryEx(Mft, FileObject->FileName.Buffer, FileObject->FileName.Length / sizeof(WCHAR), TRUE, &TargetRemainingNameLength, &TargetFile);
        if (NT_SUCCESS(Status) && TargetRemainingNameLength == 0)
        {
            NtfsFileRecordDestroy(TargetFile);
        }
        else if (Status == STATUS_NOT_FOUND || Status == STATUS_OBJECT_NAME_NOT_FOUND || Status == STATUS_OBJECT_PATH_NOT_FOUND)
        {
            if (TargetFile)
                NtfsFileRecordDestroy(TargetFile);
            TargetFile = NULL;
            Status = NtfsMasterFileTableGetFileRecordFromQueryEx(Mft, FileObject->FileName.Buffer, TargetParentLength, TRUE, &TargetRemainingNameLength, &TargetFile);
            if (!NT_SUCCESS(Status) || TargetRemainingNameLength != 0 || !TargetFile || !(NtfsFileRecordGetHeader(TargetFile)->Flags & FR_IS_DIRECTORY))
            {
                if (TargetFile)
                    NtfsFileRecordDestroy(TargetFile);
                NtfsReleaseMetadata(VolCB);
                KeLeaveCriticalRegion();
                return NtfsCompleteCreate(Irp, STATUS_OBJECT_PATH_NOT_FOUND, 0);
            }
            NtfsFileRecordDestroy(TargetFile);
            Status = STATUS_SUCCESS;
        }
        else
        {
            if (TargetFile)
                NtfsFileRecordDestroy(TargetFile);
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteCreate(Irp, Status, 0);
        }
        FileObject->FileName.Length = TargetParentLength * sizeof(WCHAR);
    }

    /*
     * Parsing the path and the file record is by far the most expensive part
     * of an open, so reuse the record left behind by an earlier open of the
     * same name when there is one. Every disposition looks: the cached record
     * is the one parsed instance of that name, so an open that goes on to
     * modify the file has to be holding it rather than a private copy that
     * later opens would never see. A hit on FILE_CREATE simply means the name
     * exists, which the disposition handling below already rejects.
     */
    CachedRecord = NtfsAcquireCachedRecord(VolCB, FileObject->FileName.Buffer, (USHORT)(FileObject->FileName.Length / sizeof(WCHAR)));
    if (CachedRecord &&
        !(IrpSp->Parameters.Create.Options & FILE_OPEN_REPARSE_POINT) &&
        NtfsFileRecordGetAttribute(CachedRecord->Record, TypeReparsePoint, NULL))
    {
        NtfsReleaseCachedRecord(VolCB, CachedRecord);
        CachedRecord = NULL;
    }

    if (CachedRecord)
    {
        CurrentFile = CachedRecord->Record;
        Status = STATUS_SUCCESS;
    }
    else if (Disposition == FILE_OPEN &&
             NtfsTakeCachedLookupParent(
                 VolCB,
                 &FileObject->FileName,
                 &CurrentFile))
    {
        Status = STATUS_SUCCESS;
    }
    else
    {
        CachedParentLookup =
            !(IrpSp->Parameters.Create.Options &
              FILE_OPEN_REPARSE_POINT) &&
            NtfsLookupInCachedParent(
                VolCB,
                Mft,
                &FileObject->FileName,
                FALSE,
                &Status,
                &CurrentFile);

        if (!CachedParentLookup)
        {
            Status =
                NtfsMasterFileTableGetFileRecordFromQueryEx(
                    Mft,
                    FileObject->FileName.Buffer,
                    FileObject->FileName.Length /
                        sizeof(WCHAR),
                    !!(IrpSp->Parameters.Create.Options &
                       FILE_OPEN_REPARSE_POINT),
                    &RemainingNameLength,
                    &CurrentFile);
        }
    }

    if (Status == STATUS_REPARSE)
    {
        Status = NtfsReturnCreateReparse(
            Irp,
            CurrentFile,
            RemainingNameLength);
        NtfsReleaseMetadata(VolCB);
        KeLeaveCriticalRegion();
        NtfsFileRecordDestroy(CurrentFile);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp,
                          IO_DISK_INCREMENT);
        return Status;
    }

    if (Status == STATUS_NOT_FOUND ||
        Status == STATUS_OBJECT_NAME_NOT_FOUND ||
        Status == STATUS_OBJECT_PATH_NOT_FOUND)
    {
        Status = NtfsTranslateNotFoundStatus(Mft, &FileObject->FileName);
        if (!(IrpSp->Parameters.Create.Options & FILE_OPEN_REPARSE_POINT))
        {
            if (Disposition == FILE_OPEN)
            {
                NtfsRememberLookupParent(
                    VolCB,
                    Mft,
                    &FileObject->FileName);
            }
            NtfsRecordNameMissing(
                VolCB,
                FileObject->FileName.Buffer,
                FileObject->FileName.Length / sizeof(WCHAR));
        }
    }

    if (NT_SUCCESS(Status) &&
        Disposition == FILE_OPEN &&
        !(IrpSp->Parameters.Create.SecurityContext->DesiredAccess & DELETE) &&
        !(IrpSp->Parameters.Create.Options & FILE_DELETE_ON_CLOSE) &&
        !(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY) &&
        (!VolCB->CachedLookupParent ||
         VolCB->CachedLookupParentGeneration != VolCB->DirGeneration))
    {
        NtfsRememberLookupParent(
            VolCB,
            Mft,
            &FileObject->FileName);
    }

    /* What we do here depends on the CreateDisposition value.
     * See https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-ntcreatefile
     */

    if (NT_SUCCESS(Status))
    {
        AttributeType RequestedType;
        PWSTR RequestedStream = NULL;
        BOOLEAN IsDirectory;

        Status = NtfsVolumeGetADSPreference(DiskVolume,
                                            &FileObject->FileName,
                                            &RequestedType,
                                            &RequestedStream);
        if (NT_SUCCESS(Status))
        {
            BOOLEAN BaseDirectory =
                !!(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY);
            ULONG StreamOffset = 0;

            while (StreamOffset < FileObject->FileName.Length / sizeof(WCHAR) &&
                   FileObject->FileName.Buffer[StreamOffset] != L':')
            {
                ++StreamOffset;
            }
            if (RequestedType == TypeIndexAllocation)
            {
                if (!BaseDirectory)
                {
                    Status = STATUS_ACCESS_DENIED;
                }
                else
                {
                    FileObject->FileName.Length = StreamOffset * sizeof(WCHAR);
                    RequestedType = TypeData;
                    if (RequestedStream)
                        ExFreePool(RequestedStream);
                    RequestedStream = NULL;
                }
            }
            else if (BaseDirectory && !RequestedStream &&
                     StreamOffset < FileObject->FileName.Length / sizeof(WCHAR))
            {
                Status = STATUS_ACCESS_DENIED;
            }
        }
        if (NT_SUCCESS(Status))
        {
            NamedDataStream = !OpenTargetDirectory && RequestedType == TypeData &&
                              RequestedStream && RequestedStream[0];
            if (NamedDataStream)
            {
                Status = NtfsCheckNamedStreamDisposition(CurrentFile,
                                                         RequestedStream,
                                                         Disposition,
                                                         &StreamExisted);
            }
        }
        if (RequestedStream)
            ExFreePool(RequestedStream);
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, NULL,
                                            CurrentFile, CachedRecord, Status);
        }
        IsDirectory = !NamedDataStream &&
                      !!(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY);

        // The file was found.

        if (!IsDirectory &&
            FileObject->FileName.Length != 0 &&
            FileObject->FileName.Buffer[FileObject->FileName.Length / sizeof(WCHAR) - 1] == L'\\')
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, NULL, CurrentFile, CachedRecord, STATUS_NOT_A_DIRECTORY);
        }
        if ((CreateOptions & FILE_DIRECTORY_FILE) &&
            !IsDirectory)
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, NULL, CurrentFile, CachedRecord, STATUS_NOT_A_DIRECTORY);
        }
        if ((CreateOptions & FILE_NON_DIRECTORY_FILE) &&
            IsDirectory)
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, NULL, CurrentFile, CachedRecord, STATUS_FILE_IS_A_DIRECTORY);
        }
        if (IsDirectory)
        {
            while (FileObject->FileName.Length > sizeof(WCHAR) && FileObject->FileName.Buffer[FileObject->FileName.Length / sizeof(WCHAR) - 1] == L'\\')
                FileObject->FileName.Length -= sizeof(WCHAR);
        }

        // In this case, return an error.
        if (Disposition == FILE_CREATE && !NamedDataStream)
        {
            ULONG NameIndex;
            BOOLEAN RootName = TRUE;

            for (NameIndex = 0; NameIndex < FileObject->FileName.Length / sizeof(WCHAR); NameIndex++)
            {
                if (FileObject->FileName.Buffer[NameIndex] != L'\\')
                {
                    RootName = FALSE;
                    break;
                }
            }
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            Status = IsDirectory && RootName ? STATUS_ACCESS_DENIED : STATUS_OBJECT_NAME_COLLISION;
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, NULL, CurrentFile, CachedRecord, Status);
        }
        if (IsDirectory &&
            (Disposition == FILE_SUPERSEDE ||
             Disposition == FILE_OVERWRITE ||
             Disposition == FILE_OVERWRITE_IF))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, NULL, CurrentFile, CachedRecord, STATUS_OBJECT_NAME_COLLISION);
        }

        // In every other case, we should continue to open the file.
    }

    else
    {
        // The file was not found.

        FileExisted = FALSE;
        if (Status == STATUS_OBJECT_PATH_NOT_FOUND)
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, NULL, CurrentFile, CachedRecord, Status);
        }
        switch (Disposition)
        {
            case FILE_SUPERSEDE:
            case FILE_CREATE:
            case FILE_OPEN_IF:
            case FILE_OVERWRITE_IF:
            {
                BOOLEAN CreatedWithCachedParent = FALSE;
                PSECURITY_DESCRIPTOR NewDescriptor = NULL;
                PWCHAR LeafName;
                USHORT LeafLength;
                USHORT ParentLength;
                USHORT FullNameLength = FileObject->FileName.Length;

                if (!OpenTargetDirectory)
                {
                    AttributeType NewType;
                    PWSTR NewStream = NULL;
                    USHORT StreamOffset = 0;
                    BOOLEAN NamedStream;

                    while (StreamOffset < FullNameLength / sizeof(WCHAR) &&
                           FileObject->FileName.Buffer[StreamOffset] != L':')
                    {
                        ++StreamOffset;
                    }
                    if (StreamOffset < FullNameLength / sizeof(WCHAR) &&
                        NT_SUCCESS(NtfsVolumeGetADSPreference(DiskVolume,
                                                              &FileObject->FileName,
                                                              &NewType,
                                                              &NewStream)) &&
                        NewType == TypeData)
                    {
                        NamedStream = NewStream && NewStream[0];
                        if (NewStream)
                            ExFreePool(NewStream);
                        if (NamedStream && (CreateOptions & FILE_DIRECTORY_FILE))
                        {
                            NtfsReleaseMetadata(VolCB);
                            KeLeaveCriticalRegion();
                            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, NULL, CurrentFile,
                                                            CachedRecord, STATUS_NOT_A_DIRECTORY);
                        }
                        if (!(CreateOptions & FILE_DIRECTORY_FILE))
                        {
                            FileObject->FileName.Length = StreamOffset * sizeof(WCHAR);
                            if (NamedStream)
                            {
                                NamedDataStream = TRUE;
                                StreamExisted = FALSE;
                            }
                        }
                    }
                    else if (NewStream)
                    {
                        ExFreePool(NewStream);
                    }
                }

                if (!(CreateOptions & FILE_DIRECTORY_FILE) &&
                    FileObject->FileName.Length != 0 &&
                    FileObject->FileName.Buffer[FileObject->FileName.Length / sizeof(WCHAR) - 1] == L'\\')
                {
                    Status = (CreateOptions & FILE_NON_DIRECTORY_FILE) ?
                             STATUS_OBJECT_NAME_INVALID : STATUS_NOT_A_DIRECTORY;
                    NtfsReleaseMetadata(VolCB);
                    KeLeaveCriticalRegion();
                    return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, NULL, CurrentFile, CachedRecord, Status);
                }
                while (FileObject->FileName.Length > sizeof(WCHAR) &&
                       FileObject->FileName.Buffer[FileObject->FileName.Length / sizeof(WCHAR) - 1] == L'\\')
                    FileObject->FileName.Length -= sizeof(WCHAR);

                /* In these cases, create the file and open it.
                 * Algorithm will probably be something like:
                 *     - Call MFT to allocate a new file record.
                 *     - Add $FILE_NAME attribute to parent directory tree.
                 *     - Set new file record to CurrentFile to open it.
                 * MFT will handle finding a free RecordID and calling LFS.
                 */
                if (NtfsSplitParentName(
                        &FileObject->FileName,
                        &ParentLength,
                        &LeafName,
                        &LeafLength))
                {
                    NtfsRememberLookupParent(
                        VolCB,
                        Mft,
                        &FileObject->FileName);
                    if (VolCB->CachedLookupParent &&
                        VolCB->CachedLookupParentGeneration ==
                            VolCB->DirGeneration &&
                        VolCB->CachedLookupParentPathLength ==
                            ParentLength &&
                        RtlCompareMemory(
                            VolCB->CachedLookupParentPath,
                            FileObject->FileName.Buffer,
                            ParentLength * sizeof(WCHAR)) ==
                            ParentLength * sizeof(WCHAR))
                    {
                        CreatedWithCachedParent = TRUE;
                        Status = NtfsAssignCreateSecurity(
                            Mft,
                            VolCB->CachedLookupParent,
                            &FileObject->FileName,
                            !!(IrpSp->Parameters.Create.Options &
                               FILE_DIRECTORY_FILE),
                            IrpSp->Parameters.Create.SecurityContext->AccessState,
                            (IrpSp->Flags & SL_FORCE_ACCESS_CHECK) ? UserMode : Irp->RequestorMode,
                            &NewDescriptor);
                        if (NT_SUCCESS(Status))
                        {
                            Status =
                                NtfsMasterFileTableCreateFileInDirectory(
                                    Mft,
                                    VolCB->CachedLookupParent,
                                    LeafName,
                                    LeafLength,
                                    !!(IrpSp->Parameters.Create.Options &
                                       FILE_DIRECTORY_FILE),
                                    FileAttributes,
                                    TRUE,
                                    &CurrentFile);
                        }
                    }
                }
                if (!CreatedWithCachedParent)
                {
                    Status = NtfsAssignCreateSecurity(
                        Mft,
                        NULL,
                        &FileObject->FileName,
                        !!(IrpSp->Parameters.Create.Options &
                           FILE_DIRECTORY_FILE),
                        IrpSp->Parameters.Create.SecurityContext->AccessState,
                        (IrpSp->Flags & SL_FORCE_ACCESS_CHECK) ? UserMode : Irp->RequestorMode,
                        &NewDescriptor);
                    if (NT_SUCCESS(Status))
                    {
                        Status = NtfsMasterFileTableCreateFile(
                            Mft,
                            FileObject->FileName.Buffer,
                            FileObject->FileName.Length / sizeof(WCHAR),
                            !!(IrpSp->Parameters.Create.Options &
                               FILE_DIRECTORY_FILE),
                            FileAttributes,
                            &CurrentFile);
                    }
                }
                if (NT_SUCCESS(Status) && NewDescriptor)
                {
                    Status = NtfsFileRecordSetSecurityDescriptor(
                        CurrentFile,
                        (const UCHAR*)NewDescriptor,
                        RtlLengthSecurityDescriptor(NewDescriptor));
                    if (!NT_SUCCESS(Status))
                    {
                        NtfsMasterFileTableDeleteFile(
                            Mft,
                            FileObject->FileName.Buffer,
                            FileObject->FileName.Length / sizeof(WCHAR),
                            !!(IrpSp->Parameters.Create.Options &
                               FILE_DIRECTORY_FILE));
                        NtfsFileRecordDestroy(CurrentFile);
                        CurrentFile = NULL;
                    }
                }
                if (NewDescriptor)
                    SeDeassignSecurity(&NewDescriptor);
                /* The name exists now, so any cached miss for it is wrong. */
                if (NT_SUCCESS(Status))
                {
                    LONG Generation =
                        InterlockedIncrement(
                            &VolCB->DirGeneration);

                    if (CreatedWithCachedParent)
                    {
                        VolCB->CachedLookupParentGeneration =
                            Generation;
                    }
                    NtfsForgetMissingName(VolCB,
                                          FileObject->FileName.Buffer,
                                          FileObject->FileName.Length / sizeof(WCHAR));
                    FileObject->FileName.Length = FullNameLength;
                }
                else if (CreatedWithCachedParent)
                {
                    NtfsFileRecordDestroy(
                        VolCB->CachedLookupParent);
                    VolCB->CachedLookupParent = NULL;
                    VolCB->CachedLookupParentPathLength = 0;
                }
                if (!NT_SUCCESS(Status))
                {
                    NtfsReleaseMetadata(VolCB);
                    KeLeaveCriticalRegion();
                    Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
                    Irp->IoStatus.Status = Status;
                    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
                    return Status;
                }
                break;
            }
            case FILE_OPEN:
            case FILE_OVERWRITE:
            default:
                // In these cases, return an error.
                NtfsReleaseMetadata(VolCB);
                KeLeaveCriticalRegion();
                Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_DISK_INCREMENT);
                return Status;
                break;
        }

    }

    if (!CachedRecord && CurrentFile &&
        !(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY))
    {
        CachedRecord = NtfsCacheRecord(VolCB,
                                       FileObject->FileName.Buffer,
                                       (USHORT)(FileObject->FileName.Length / sizeof(WCHAR)),
                                       CurrentFile);
        if (!CachedRecord)
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, NULL, CurrentFile, NULL,
                                            STATUS_INSUFFICIENT_RESOURCES);
        }
        if (CurrentFile != CachedRecord->Record)
        {
            NtfsFileRecordDestroy(CurrentFile);
            CurrentFile = CachedRecord->Record;
        }
    }
    ResolvedGeneration = VolCB->DirGeneration;
    NtfsReleaseMetadata(VolCB);
    KeLeaveCriticalRegion();

    // Create file context block.
    /*
     * Initializing two ERESOURCEs costs more than the rest of an open put
     * together, so a block whose handles have all closed is kept with its
     * header, resources and mutex intact and only its per-open half reset.
     */
    FileCB = NULL;
    ExAcquireFastMutex(&VolCB->IdleFcbMutex);
    if (!IsListEmpty(&VolCB->IdleFcbList))
    {
        PLIST_ENTRY Idle = RemoveHeadList(&VolCB->IdleFcbList);

        VolCB->IdleFcbCount--;
        FileCB = CONTAINING_RECORD(Idle, FileContextBlock, IdleLink);
    }
    ExReleaseFastMutex(&VolCB->IdleFcbMutex);

    if (FileCB)
    {
        RtlZeroMemory((PUCHAR)FileCB + NTFS_FCB_PER_OPEN_OFFSET,
                      sizeof(FileContextBlock) - NTFS_FCB_PER_OPEN_OFFSET);
    }
    else
    {
        FileCB = (PFileContextBlock)ExAllocatePoolZero(NonPagedPool,
                                                       sizeof(FileContextBlock),
                                                       TAG_NTFS);
        if (!FileCB)
        {
            return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                            Irp,
                                            NULL,
                                            CurrentFile,
                                            CachedRecord,
                                            STATUS_INSUFFICIENT_RESOURCES);
        }

        // Initialize the NT required FCB header and resources
        ExInitializeResourceLite(&FileCB->MainResource);
        ExInitializeResourceLite(&FileCB->PagingIoResource);
        ExInitializeFastMutex(&FileCB->HeaderMutex);
        FsRtlSetupAdvancedHeader(&FileCB->CommonFCBHeader, &FileCB->HeaderMutex);
    }
    FileCB->CommonFCBHeader.Resource = &FileCB->MainResource;
    FileCB->CommonFCBHeader.PagingIoResource = &FileCB->PagingIoResource;
    FileCB->CommonFCBHeader.IsFastIoPossible = FastIoIsPossible;

    // Set file name
    FileNameLength = IrpSp->FileObject->FileName.Length;

    PWCHAR FileNameBuffer;

    if (FileNameLength <= sizeof(FileCB->InlineFileName))
    {
        FileNameBuffer = FileCB->InlineFileName;
    }
    else
    {
        FileNameBuffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                                       FileNameLength,
                                                       TAG_NTFS);
    }
    if (!FileNameBuffer)
    {
        return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                        Irp,
                                        FileCB,
                                        CurrentFile,
                                        CachedRecord,
                                        STATUS_INSUFFICIENT_RESOURCES);
    }
    RtlCopyMemory(FileNameBuffer,
                  IrpSp->FileObject->FileName.Buffer,
                  FileNameLength);
    FileCB->FileName.Buffer = FileNameBuffer;
    FileCB->FileName.Length = FileNameLength;
    FileCB->FileName.MaximumLength = FileNameLength;
    // Non-inline name storage is freed when the FileCB is cleaned up.

    // Get ADS Preferences for the file.
    Status = NtfsVolumeGetADSPreference(DiskVolume,
                                        &FileObject->FileName,
                                        &FileCB->RequestedType,
                                        &FileCB->RequestedStream);

    if (!NT_SUCCESS(Status))
    {
        return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                        Irp,
                                        FileCB,
                                        CurrentFile,
                                        CachedRecord,
                                        Status);
    }

    KeEnterCriticalRegion();
    NtfsAcquireMetadata(VolCB);
    FileCB->CachedRecord = CachedRecord;
    FileCB->FileRec = CurrentFile;
    FileCB->AutomaticTimestampMask = NTFS_AUTOMATIC_TIMESTAMP_FIELDS;
    if (ResolvedGeneration != VolCB->DirGeneration &&
        !(IrpSp->Parameters.Create.Options & FILE_OPEN_BY_FILE_ID))
    {
        PNtfsFileRecord ResolvedFile = NULL;
        ULONG UnresolvedLength = 0;

        Status = NtfsCheckTraverseAccess(Mft, &FileCB->FileName,
                                         TraverseRelatedReference, TraverseNameOffset,
                                         OpenTargetDirectory,
                                         IrpSp->Parameters.Create.SecurityContext->AccessState,
                                         (IrpSp->Flags & SL_FORCE_ACCESS_CHECK) ? UserMode : Irp->RequestorMode);
        if (NT_SUCCESS(Status))
            Status = NtfsMasterFileTableGetFileRecordFromQueryEx(
                Mft, FileCB->FileName.Buffer, FileCB->FileName.Length / sizeof(WCHAR),
                TRUE, &UnresolvedLength, &ResolvedFile);
        if (NT_SUCCESS(Status) &&
            (!ResolvedFile || UnresolvedLength != 0 ||
             NtfsFileRecordGetHeader(ResolvedFile)->MFTRecordNumber !=
                 NtfsFileRecordGetHeader(CurrentFile)->MFTRecordNumber ||
             NtfsFileRecordGetHeader(ResolvedFile)->SequenceNumber !=
                 NtfsFileRecordGetHeader(CurrentFile)->SequenceNumber))
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        }
        if (ResolvedFile)
            NtfsFileRecordDestroy(ResolvedFile);
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, FileCB,
                                            CurrentFile, CachedRecord, Status);
        }
    }
    if (ResolvedGeneration != VolCB->DirGeneration)
    {
        Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (NT_SUCCESS(Status) &&
            !(NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY))
        {
            PNtfsFileRecord IdentityRecord = NULL;
            PFileRecordHeader Header = NtfsFileRecordGetHeader(FileCB->FileRec);
            ULONGLONG FileReference = ((ULONGLONG)Header->SequenceNumber << 48) |
                                       Header->MFTRecordNumber;

            Status = NtfsMasterFileTableGetFileRecordByReference(Mft, FileReference,
                                                                 &IdentityRecord);
            if (NT_SUCCESS(Status) &&
                (NtfsFileRecordGetHeader(IdentityRecord)->Flags & FR_IS_DIRECTORY))
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
            }
            if (IdentityRecord)
                NtfsFileRecordDestroy(IdentityRecord);
        }
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, FileCB,
                                            CurrentFile, CachedRecord, Status);
        }
        ResolvedGeneration = VolCB->DirGeneration;
    }
    FileCB->LastAccessStampPending = NtfsShouldStampLastAccess(FileCB);
    FileCB->CreateOptions = IrpSp->Parameters.Create.Options;
    FileCB->ManageVolumeAccess = SeSinglePrivilegeCheck(SeExports->SeManageVolumePrivilege, Irp->RequestorMode);
    if (NamedDataStream)
    {
        Status = NtfsCheckNamedStreamDisposition(CurrentFile,
                                                 FileCB->RequestedStream,
                                                 Disposition,
                                                 &StreamExisted);
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, FileCB,
                                            CurrentFile, CachedRecord, Status);
        }
    }
    if (FileExisted)
    {
        Status = NtfsCheckOpenAccess(Mft, CurrentFile, &FileObject->FileName,
                                     IrpSp->Parameters.Create.SecurityContext,
                                     OpenTargetDirectory ? FILE_OPEN :
                                         (NamedDataStream && !StreamExisted ? FILE_CREATE : Disposition),
                                     NamedDataStream && !StreamExisted,
                                     (IrpSp->Flags & SL_FORCE_ACCESS_CHECK) ? UserMode : Irp->RequestorMode);
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, FileCB, CurrentFile, CachedRecord, Status);
        }
    }
    FileCB->DesiredAccess = IrpSp->Parameters.Create.SecurityContext->DesiredAccess;
    if (IrpSp->Parameters.Create.SecurityContext->AccessState)
    {
        FileCB->DesiredAccess |=
            IrpSp->Parameters.Create.SecurityContext->AccessState->PreviouslyGrantedAccess;
    }
    /* A read-only file may still be opened to change its attributes, but it
     * cannot be opened for data writes or an overwriting disposition. */
    if (FileExisted &&
        !OpenTargetDirectory &&
        !(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY) &&
        ((FileCB->DesiredAccess & (FILE_WRITE_DATA | FILE_APPEND_DATA)) ||
         (NamedDataStream && !StreamExisted) ||
         Disposition == FILE_OVERWRITE ||
         Disposition == FILE_OVERWRITE_IF))
    {
        NtfsFileBasicInformation BasicInformation;

        Status = NtfsFileRecordGetBasicInformation(CurrentFile, &BasicInformation);
        if (NT_SUCCESS(Status) && (BasicInformation.FileAttributes & FILE_ATTRIBUTE_READONLY))
            Status = STATUS_ACCESS_DENIED;
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, FileCB, CurrentFile, CachedRecord, Status);
        }
    }

    /*
     * WOF FILE-provider files become ordinary files as soon as their unnamed
     * data stream is opened for content writes.  Do this before cache sizes
     * are initialized so Cache Manager observes the materialized allocation.
     */
    if (!(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY) &&
        FileCB->RequestedType == TypeData &&
        !FileCB->RequestedStream &&
        (FileCB->DesiredAccess &
         (FILE_WRITE_DATA | FILE_APPEND_DATA)))
    {
        Status = NtfsVolumeIsReadOnly(DiskVolume)
            ? STATUS_MEDIA_WRITE_PROTECTED
            : NtfsFileRecordDeleteExternalBacking(
                CurrentFile);
        if (Status ==
            STATUS_OBJECT_NOT_EXTERNALLY_BACKED)
        {
            Status = STATUS_SUCCESS;
        }
        else if (NT_SUCCESS(Status))
        {
            ExternalBackingDeleted = TRUE;
        }

        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, FileCB, CurrentFile,
                                            CachedRecord, Status);
        }
    }

    /* Data streams share section and lock state across their open handles.
     * For more details see:
     * https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_section_object_pointers
     */
    FileCB->StreamCB = NtfsReferenceStreamContext(VolCB,
                                                  CurrentFile,
                                                  FileCB->RequestedType,
                                                  FileCB->RequestedStream);
    Status = FileCB->StreamCB
        ? NtfsRegisterOpenLink(VolCB, FileCB, FileObject)
        : STATUS_INSUFFICIENT_RESOURCES;
    NtfsReleaseMetadata(VolCB);
    KeLeaveCriticalRegion();
    if (!NT_SUCCESS(Status))
    {
        return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                        Irp,
                                        FileCB,
                                        CurrentFile,
                                        CachedRecord,
                                        Status);
    }
    if (FileCB->StreamCB->DeletePending)
    {
        return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                        Irp,
                                        FileCB,
                                        CurrentFile,
                                        CachedRecord,
                                        STATUS_DELETE_PENDING);
    }

    if ((NamedDataStream || !(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY)) &&
        ((FileCB->DesiredAccess & FILE_WRITE_DATA) ||
         (NamedDataStream &&
          (Disposition == FILE_SUPERSEDE ||
           Disposition == FILE_OVERWRITE || Disposition == FILE_OVERWRITE_IF)) ||
         (IrpSp->Parameters.Create.Options & FILE_DELETE_ON_CLOSE)) &&
        !MmFlushImageSection(&FileCB->StreamCB->SectionObjectPointers, MmFlushForWrite))
    {
        return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                        Irp,
                                        FileCB,
                                        CurrentFile,
                                        CachedRecord,
                                        (IrpSp->Parameters.Create.Options & FILE_DELETE_ON_CLOSE)
                                            ? STATUS_CANNOT_DELETE
                                            : STATUS_SHARING_VIOLATION);
    }

    if (FileExisted && !NamedDataStream &&
        !OpenTargetDirectory &&
        !(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY) &&
        (Disposition == FILE_SUPERSEDE ||
         Disposition == FILE_OVERWRITE ||
         Disposition == FILE_OVERWRITE_IF))
    {
        LARGE_INTEGER ZeroSize;

        ExAcquireFastMutex(&VolCB->StreamListMutex);
        Status = IoCheckShareAccess(FileCB->DesiredAccess,
                                    IrpSp->Parameters.Create.ShareAccess,
                                    FileObject,
                                    &FileCB->StreamCB->ShareAccess,
                                    FALSE);
        ExReleaseFastMutex(&VolCB->StreamListMutex);
        if (!NT_SUCCESS(Status))
        {
            return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                            Irp,
                                            FileCB,
                                            CurrentFile,
                                            CachedRecord,
                                            Status);
        }

        ZeroSize.QuadPart = 0;
        if (!MmCanFileBeTruncated(&FileCB->StreamCB->SectionObjectPointers, &ZeroSize))
        {
            return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                            Irp,
                                            FileCB,
                                            CurrentFile,
                                            CachedRecord,
                                            STATUS_USER_MAPPED_FILE);
        }
    }

    KeEnterCriticalRegion();
    NtfsAcquireMetadata(VolCB);
    if (FileCB->DeletePending || FileCB->StreamCB->DeletePending ||
        FileCB->StreamCB->Deleted)
    {
        Status = STATUS_DELETE_PENDING;
    }
    else
    {
        ExAcquireFastMutex(&VolCB->StreamListMutex);
        Status = IoCheckShareAccess(FileCB->DesiredAccess,
                                    IrpSp->Parameters.Create.ShareAccess,
                                    FileObject,
                                    &FileCB->StreamCB->ShareAccess,
                                    TRUE);
        if (NT_SUCCESS(Status))
        {
            FileCB->ShareAccessSet = TRUE;
            InterlockedIncrement(&FileCB->StreamCB->UncleanCount);
        }
        ExReleaseFastMutex(&VolCB->StreamListMutex);
    }
    NtfsReleaseMetadata(VolCB);
    KeLeaveCriticalRegion();
    if (!NT_SUCCESS(Status))
    {
        return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                        Irp,
                                        FileCB,
                                        CurrentFile,
                                        CachedRecord,
                                        Status);
    }
    if (!NamedDataStream &&
        !!(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY))
    {
        /* Reuse the previous tree when nothing on the volume changed. */
        USHORT PathChars;

        KeEnterCriticalRegion();
        NtfsAcquireMetadata(VolCB);
        DirectoryMetadataAcquired = TRUE;
        PathChars = (USHORT)(FileCB->FileName.Length / sizeof(WCHAR));
        ExAcquireFastMutex(&VolCB->DirCacheMutex);
        if (VolCB->CachedDir &&
            !(FileCB->CreateOptions & FILE_OPEN_BY_FILE_ID) &&
            !VolCB->CachedDirBusy &&
            VolCB->CachedDirGeneration == VolCB->DirGeneration &&
            VolCB->CachedDirPathLength == PathChars &&
            PathChars != 0 &&
            RtlCompareMemory(VolCB->CachedDirPath,
                             FileCB->FileName.Buffer,
                             PathChars * sizeof(WCHAR)) ==
                PathChars * sizeof(WCHAR))
        {
            FileCB->FileDir = VolCB->CachedDir;
            FileCB->FileDirBorrowed = TRUE;
            VolCB->CachedDirBusy = TRUE;
        }
        ExReleaseFastMutex(&VolCB->DirCacheMutex);
    }

    if (!NamedDataStream && !FileCB->FileDir &&
        !!(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY))
    {
        // Set up btree for this file
        FileCB->FileDir = NtfsDirectoryCreate(DiskVolume);
        if (!FileCB->FileDir)
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                            Irp,
                                            FileCB,
                                            CurrentFile,
                                            CachedRecord,
                                            STATUS_INSUFFICIENT_RESOURCES);
        }

        Status = STATUS_SUCCESS;
        if (ResolvedGeneration != VolCB->DirGeneration)
        {
            PNtfsFileRecord FreshRecord = NULL;

            Status = NtfsMasterFileTableGetFileRecordByReference(
                Mft, FileCB->StreamCB->FileReference, &FreshRecord);
            if (NT_SUCCESS(Status) &&
                !(NtfsFileRecordGetHeader(FreshRecord)->Flags & FR_IS_DIRECTORY))
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
            }
            if (NT_SUCCESS(Status))
                Status = NtfsFileRecordRefresh(FileCB->FileRec, FreshRecord);
            if (FreshRecord)
                NtfsFileRecordDestroy(FreshRecord);
        }
        if (NT_SUCCESS(Status))
        {
            Status = NtfsDirectoryLoadForEnumeration(FileCB->FileDir, FileCB->FileRec);
            if (Status == STATUS_NOT_IMPLEMENTED)
            {
                Status = NtfsDirectoryLoadDirectory(
                    FileCB->FileDir,
                    FileCB->FileRec);
            }
            else if (NT_SUCCESS(Status))
            {
                FileCB->FileDirDirect = TRUE;
            }
        }
        if (!NT_SUCCESS(Status))
        {
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
            return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                            Irp,
                                            FileCB,
                                            CurrentFile,
                                            CachedRecord,
                                            Status);
        }
        if (!FileCB->FileDirDirect && !(FileCB->CreateOptions & FILE_OPEN_BY_FILE_ID) &&
            FileCB->FileName.Length != 0 &&
            FileCB->FileName.Length <= sizeof(VolCB->CachedDirPath))
        {
            PNtfsDirectory Evicted = NULL;

            ExAcquireFastMutex(&VolCB->DirCacheMutex);
            if (!VolCB->CachedDir ||
                (!VolCB->CachedDirBusy && VolCB->CachedDirGeneration != VolCB->DirGeneration))
            {
                Evicted = VolCB->CachedDir;
                VolCB->CachedDir = FileCB->FileDir;
                VolCB->CachedDirGeneration = VolCB->DirGeneration;
                VolCB->CachedDirPathLength = FileCB->FileName.Length / sizeof(WCHAR);
                RtlCopyMemory(VolCB->CachedDirPath, FileCB->FileName.Buffer, FileCB->FileName.Length);
                VolCB->CachedDirBusy = TRUE;
                FileCB->FileDirBorrowed = TRUE;
            }
            ExReleaseFastMutex(&VolCB->DirCacheMutex);
            if (Evicted)
                NtfsDirectoryDestroy(Evicted);
        }
    }
    if (DirectoryMetadataAcquired)
    {
        NtfsReleaseMetadata(VolCB);
        KeLeaveCriticalRegion();
    }

    /*
     * Publish the handle under the same lock used by volume locking and
     * dismount. If either operation won the race while the name was being
     * resolved, do not expose a new handle after that boundary.
     */
    ExAcquireFastMutex(&VolCB->VolumeStateMutex);
    if (VolCB->Dismounting || VolCB->Dismounted)
        Status = STATUS_VOLUME_DISMOUNTED;
    else if (VolCB->VolumeLockOwner)
        Status = STATUS_ACCESS_DENIED;
    else
    {
        VolCB->OpenHandleCount++;
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&VolCB->VolumeStateMutex);
    if (!NT_SUCCESS(Status))
    {
        return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                        Irp,
                                        FileCB,
                                        CurrentFile,
                                        CachedRecord,
                                        Status);
    }

    // Initialize file sizes and section state.
    {
        if (FileCB->StreamCB &&
            (NamedDataStream || !(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY)))
        {
            /* Mm requires SectionObjectPointer for image and data sections. */
            FileObject->SectionObjectPointer =
                &FileCB->StreamCB->SectionObjectPointers;
        }
        FileObject->FsContext = FileCB->StreamCB;
        FileObject->FsContext2 = FileCB;
    }

    /* Asking to delete a read-only file on close is refused at the open, the
     * same as asking for it through a disposition later. */
    if (FileExisted &&
        !OpenTargetDirectory &&
        (IrpSp->Parameters.Create.Options & FILE_DELETE_ON_CLOSE))
    {
        NtfsFileBasicInformation DeleteOnCloseBasic;

        KeEnterCriticalRegion();
        ExAcquireResourceSharedLite(NtfsGetMainResource(FileCB), TRUE);
        NtfsAcquireMetadata(VolCB);
        Status = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (NT_SUCCESS(Status))
            Status = NtfsFileRecordGetBasicInformation(CurrentFile, &DeleteOnCloseBasic);
        if (NT_SUCCESS(Status) && (DeleteOnCloseBasic.FileAttributes & FILE_ATTRIBUTE_READONLY))
            Status = STATUS_CANNOT_DELETE;
        NtfsReleaseMetadata(VolCB);
        ExReleaseResourceLite(NtfsGetMainResource(FileCB));
        KeLeaveCriticalRegion();
        if (!NT_SUCCESS(Status))
        {
            ExAcquireFastMutex(&VolCB->VolumeStateMutex);
            if (VolCB->OpenHandleCount > 0)
                VolCB->OpenHandleCount--;
            ExReleaseFastMutex(&VolCB->VolumeStateMutex);
            FileObject->FsContext = NULL;
            FileObject->FsContext2 = NULL;
            FileObject->SectionObjectPointer = NULL;
            return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                            Irp,
                                            FileCB,
                                            CurrentFile,
                                            CachedRecord,
                                            Status);
        }
    }

    /*
     * An overwriting disposition opens the existing file and then empties it:
     * without this, writing something shorter over a file leaves the old tail
     * behind, which is what "echo x > existing" does all day.
     */
    if (NamedDataStream)
    {
        Status = NtfsOpenNamedDataStream(VolCB, FileCB, FileObject,
                                         IrpSp->Parameters.Create.SecurityContext,
                                         Disposition,
                                         (IrpSp->Flags & SL_FORCE_ACCESS_CHECK) ? UserMode : Irp->RequestorMode,
                                         &StreamCreated,
                                         &Overwritten);
        if (!NT_SUCCESS(Status))
        {
            ExAcquireFastMutex(&VolCB->VolumeStateMutex);
            if (VolCB->OpenHandleCount > 0)
                VolCB->OpenHandleCount--;
            ExReleaseFastMutex(&VolCB->VolumeStateMutex);
            FileObject->FsContext = NULL;
            FileObject->FsContext2 = NULL;
            FileObject->SectionObjectPointer = NULL;
            return NtfsCompleteFailedCreate(VolumeDeviceObject, Irp, FileCB,
                                            CurrentFile, CachedRecord, Status);
        }
    }
    if (FileExisted && !NamedDataStream &&
        !OpenTargetDirectory &&
        FileCB->RequestedType == TypeData &&
        !(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY) &&
        (Disposition == FILE_SUPERSEDE ||
         Disposition == FILE_OVERWRITE ||
         Disposition == FILE_OVERWRITE_IF))
    {
        KeEnterCriticalRegion();
        NtfsAcquireMetadata(VolCB);
        Status = NtfsFileRecordSetFileDataSize(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream, 0);
        if (NT_SUCCESS(Status) && !FileCB->RequestedStream)
        {
            NtfsFileBasicInformation OverwriteBasic;

            RtlZeroMemory(&OverwriteBasic, sizeof(OverwriteBasic));
            OverwriteBasic.Fields = NTFS_BASIC_INFO_FILE_ATTRIBUTES;
            OverwriteBasic.FileAttributes = FileAttributes;
            Status = NtfsFileRecordSetBasicInformation(FileCB->FileRec, &OverwriteBasic);
        }
        NtfsReleaseMetadata(VolCB);
        KeLeaveCriticalRegion();
        if (!NT_SUCCESS(Status))
        {
            ExAcquireFastMutex(&VolCB->VolumeStateMutex);
            if (VolCB->OpenHandleCount > 0)
                VolCB->OpenHandleCount--;
            ExReleaseFastMutex(&VolCB->VolumeStateMutex);
            FileObject->FsContext = NULL;
            FileObject->FsContext2 = NULL;
            FileObject->SectionObjectPointer = NULL;
            return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                            Irp,
                                            FileCB,
                                            CurrentFile,
                                            CachedRecord,
                                            Status);
        }

        if (FileCB->StreamCB)
            FileCB->StreamCB->SizePending = FALSE;
        NtfsRefreshFileSizes(VolCB, FileCB, FileObject);
        NtfsPurgeStreamCache(FileCB, FileObject, NULL, 0);
        FileObject->Flags |= FO_FILE_MODIFIED | FO_FILE_SIZE_CHANGED;
        Overwritten = TRUE;
    }

    // Open file.
    if (ExternalBackingDeleted)
    {
        FileObject->Flags |=
            FO_FILE_MODIFIED |
            FO_FILE_SIZE_CHANGED;
    }
    if (OpenTargetDirectory)
    {
        Irp->IoStatus.Information = FILE_OPENED;
    }
    else if (!FileExisted || StreamCreated)
    {
        Irp->IoStatus.Information = FILE_CREATED;
    }
    else if (Overwritten)
    {
        Irp->IoStatus.Information = Disposition == FILE_SUPERSEDE ? FILE_SUPERSEDED : FILE_OVERWRITTEN;
    }
    else
    {
        Irp->IoStatus.Information = FILE_OPENED;
    }
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return STATUS_SUCCESS;
}
