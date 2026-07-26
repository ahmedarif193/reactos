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

static
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

    if (!VolCB || !File)
        return NULL;

    Header = NtfsFileRecordGetHeader(File);
    if (!Header || Header->SequenceNumber == 0)
        return NULL;

    FileReference =
        ((ULONGLONG)Header->SequenceNumber << 48) |
        Header->MFTRecordNumber;
    RtlInitUnicodeString(&StreamName, RequestedStream);

    ExAcquireFastMutex(&VolCB->StreamListMutex);
    for (Entry = VolCB->StreamList.Flink;
         Entry != &VolCB->StreamList;
         Entry = Entry->Flink)
    {
        StreamCB = CONTAINING_RECORD(Entry, StreamContextBlock, ListEntry);
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
    StreamCB->RequestedType = RequestedType;
    StreamCB->ReferenceCount = 1;
    FsRtlInitializeFileLock(&StreamCB->FileLock, NULL, NULL);
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
        FsRtlUninitializeFileLock(&StreamCB->FileLock);
        if (StreamCB->RequestedStream.Buffer)
            ExFreePool(StreamCB->RequestedStream.Buffer);
        ExFreePool(StreamCB);
    }
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
    BOOLEAN PerformAccessChecks;
    PNtfsFileRecord CurrentFile = NULL;
    PNtfsCachedRecord CachedRecord = NULL;
    UINT8 Disposition;
    PVolumeContextBlock VolCB;
    PNtfsVolume DiskVolume;
    PNtfsMasterFileTable Mft;
    ULONG RemainingNameLength = 0;
    ULONG FileAttributes;
    USHORT FileNameLength;
    BOOLEAN ExternalBackingDeleted = FALSE;

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
    FileAttributes =
        IrpSp->Parameters.Create.FileAttributes &
        FILE_ATTRIBUTE_VALID_FLAGS;
    if (FileAttributes & ~FILE_ATTRIBUTE_NORMAL)
        FileAttributes &= ~FILE_ATTRIBUTE_NORMAL;
    VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    DiskVolume = VolCB->DiskVolume;
    Mft = NtfsVolumeGetMft(DiskVolume);

    // Determine if we should check access rights
    PerformAccessChecks = (Irp->RequestorMode == UserMode) ||
                          (IrpSp->Flags & SL_FORCE_ACCESS_CHECK);

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
    /*
     * An open that only ever fails still costs a full index walk, so answer
     * from the missing-name cache when the path is already known absent.
     */
    if (!(IrpSp->Parameters.Create.Options & FILE_OPEN_REPARSE_POINT) &&
        Disposition == FILE_OPEN &&
        NtfsIsNameKnownMissing(VolCB,
                               FileObject->FileName.Buffer,
                               FileObject->FileName.Length / sizeof(WCHAR)))
    {
        Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
        Irp->IoStatus.Status = STATUS_OBJECT_NAME_NOT_FOUND;
        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&VolCB->MetadataResource, TRUE);

    /*
     * Parsing the path and the file record is by far the most expensive part
     * of an open, so reuse the record left behind by an earlier open of the
     * same name when there is one.
     */
    if (Disposition == FILE_OPEN || Disposition == FILE_OPEN_IF)
    {
        CachedRecord = NtfsAcquireCachedRecord(
            VolCB,
            FileObject->FileName.Buffer,
            (USHORT)(FileObject->FileName.Length / sizeof(WCHAR)));
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
        BOOLEAN ParentLookup =
            Disposition == FILE_OPEN &&
            !(IrpSp->Parameters.Create.Options &
              FILE_OPEN_REPARSE_POINT) &&
            NtfsLookupInCachedParent(
                VolCB,
                Mft,
                &FileObject->FileName,
                FALSE,
                &Status,
                &CurrentFile);

        if (!ParentLookup)
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
        ExReleaseResourceLite(&VolCB->MetadataResource);
        KeLeaveCriticalRegion();
        NtfsFileRecordDestroy(CurrentFile);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp,
                          IO_DISK_INCREMENT);
        return Status;
    }

    if ((Status == STATUS_NOT_FOUND ||
         Status == STATUS_OBJECT_NAME_NOT_FOUND ||
         Status == STATUS_OBJECT_PATH_NOT_FOUND) &&
        !(IrpSp->Parameters.Create.Options & FILE_OPEN_REPARSE_POINT))
    {
        if (Disposition == FILE_OPEN)
        {
            NtfsRememberLookupParent(
                VolCB,
                Mft,
                &FileObject->FileName);
        }
        NtfsRecordNameMissing(VolCB,
                              FileObject->FileName.Buffer,
                              FileObject->FileName.Length / sizeof(WCHAR));
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
        // The file was found.

        // In this case, return an error.
        if (Disposition == FILE_CREATE)
        {
            ExReleaseResourceLite(&VolCB->MetadataResource);
            KeLeaveCriticalRegion();
            Irp->IoStatus.Information = FILE_EXISTS;
            return STATUS_INVALID_PARAMETER;
        }

        // In every other case, we should continue to open the file.
    }

    else
    {
        // The file was not found.

        switch (Disposition)
        {
            case FILE_SUPERSEDE:
            case FILE_CREATE:
            case FILE_OPEN_IF:
            case FILE_OVERWRITE_IF:
                /* In these cases, create the file and open it.
                 * Algorithm will probably be something like:
                 *     - Call MFT to allocate a new file record.
                 *     - Add $FILE_NAME attribute to parent directory tree.
                 *     - Set new file record to CurrentFile to open it.
                 * MFT will handle finding a free RecordID and calling LFS.
                 */
                Status = NtfsMasterFileTableCreateFile(
                    Mft,
                    FileObject->FileName.Buffer,
                    FileObject->FileName.Length / sizeof(WCHAR),
                    !!(IrpSp->Parameters.Create.Options & FILE_DIRECTORY_FILE),
                    FileAttributes,
                    &CurrentFile);
                /* The name exists now, so any cached miss for it is wrong. */
                if (NT_SUCCESS(Status))
                {
                    InterlockedIncrement(&VolCB->DirGeneration);
                    NtfsForgetMissingName(VolCB,
                                          FileObject->FileName.Buffer,
                                          FileObject->FileName.Length / sizeof(WCHAR));
                }
                if (!NT_SUCCESS(Status))
                {
                    ExReleaseResourceLite(&VolCB->MetadataResource);
                    KeLeaveCriticalRegion();
                    Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
                    Irp->IoStatus.Status = Status;
                    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
                    return Status;
                }
                break;
            case FILE_OPEN:
            case FILE_OVERWRITE:
            default:
                // In these cases, return an error.
                ExReleaseResourceLite(&VolCB->MetadataResource);
                KeLeaveCriticalRegion();
                Irp->IoStatus.Information = FILE_DOES_NOT_EXIST;
                Irp->IoStatus.Status = STATUS_OBJECT_NAME_NOT_FOUND;
                IoCompleteRequest(Irp, IO_DISK_INCREMENT);
                return STATUS_OBJECT_NAME_NOT_FOUND;
                break;
        }

    }

    ExReleaseResourceLite(&VolCB->MetadataResource);
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

    if (!CachedRecord && CurrentFile)
    {
        CachedRecord = NtfsCacheRecord(
            VolCB,
            FileObject->FileName.Buffer,
            (USHORT)(FileObject->FileName.Length / sizeof(WCHAR)),
            CurrentFile);
    }

    FileCB->CachedRecord = CachedRecord;
    FileCB->FileRec = CurrentFile;
    FileCB->LastAccessStampPending = NtfsShouldStampLastAccess(FileCB);
    FileCB->CreateOptions = IrpSp->Parameters.Create.Options;
    FileCB->DesiredAccess = IrpSp->Parameters.Create.SecurityContext->DesiredAccess;
    FileCB->AutomaticTimestampMask =
        NtfsFileRecordGetAutomaticTimestampMask(
            CurrentFile);

    /*
     * WOF FILE-provider files become ordinary files as soon as their unnamed
     * data stream is opened for content writes.  Do this before cache sizes
     * are initialized so Cache Manager observes the materialized allocation.
     */
    if (FileCB->RequestedType == TypeData &&
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
            if (FileCB->RequestedStream)
                ExFreePool(FileCB->RequestedStream);
            if (FileCB->FileName.Buffer &&
                FileCB->FileName.Buffer != FileCB->InlineFileName)
                ExFreePool(FileCB->FileName.Buffer);
            NtfsFileRecordDestroy(CurrentFile);
            ExDeleteResourceLite(
                &FileCB->MainResource);
            ExDeleteResourceLite(
                &FileCB->PagingIoResource);
            ExFreePool(FileCB);
            Irp->IoStatus.Information = 0;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(
                Irp,
                IO_DISK_INCREMENT);
            return Status;
        }
    }

    /* Data streams share section and lock state across their open handles.
     * For more details see:
     * https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_section_object_pointers
     */
    if (!(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY))
    {
        FileCB->StreamCB = NtfsReferenceStreamContext(VolCB,
                                                      CurrentFile,
                                                      FileCB->RequestedType,
                                                      FileCB->RequestedStream);
        if (!FileCB->StreamCB)
        {
            return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                            Irp,
                                            FileCB,
                                            CurrentFile,
                                            CachedRecord,
                                            STATUS_INSUFFICIENT_RESOURCES);
        }
    }

    if (!!(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY))
    {
        /* Reuse the previous tree when nothing on the volume changed. */
        USHORT PathChars =
            (USHORT)(FileObject->FileName.Length / sizeof(WCHAR));

        ExAcquireFastMutex(&VolCB->DirCacheMutex);
        if (VolCB->CachedDir &&
            !VolCB->CachedDirBusy &&
            VolCB->CachedDirGeneration == VolCB->DirGeneration &&
            VolCB->CachedDirPathLength == PathChars &&
            PathChars != 0 &&
            RtlCompareMemory(VolCB->CachedDirPath,
                             FileObject->FileName.Buffer,
                             PathChars * sizeof(WCHAR)) ==
                PathChars * sizeof(WCHAR))
        {
            FileCB->FileDir = VolCB->CachedDir;
            FileCB->FileDirBorrowed = TRUE;
            VolCB->CachedDirBusy = TRUE;
        }
        ExReleaseFastMutex(&VolCB->DirCacheMutex);
    }

    if (!FileCB->FileDir &&
        !!(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY))
    {
        // Set up btree for this file
        FileCB->FileDir = NtfsDirectoryCreate(DiskVolume);
        if (!FileCB->FileDir)
        {
            return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                            Irp,
                                            FileCB,
                                            CurrentFile,
                                            CachedRecord,
                                            STATUS_INSUFFICIENT_RESOURCES);
        }

        Status = NtfsDirectoryLoadForEnumeration(
            FileCB->FileDir,
            FileCB->FileRec);
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
        if (!NT_SUCCESS(Status))
        {
            return NtfsCompleteFailedCreate(VolumeDeviceObject,
                                            Irp,
                                            FileCB,
                                            CurrentFile,
                                            CachedRecord,
                                            Status);
        }
    }

    // Initialize file sizes and section state.
    {
        // Initialize the common header sizes from attributes
        PAttribute DataAttr = NtfsFileRecordGetAttribute(
            CurrentFile,
            FileCB->RequestedType,
            FileCB->RequestedStream);
        if (DataAttr)
        {
            if (DataAttr->IsNonResident)
            {
                FileCB->CommonFCBHeader.AllocationSize.QuadPart =
                    NtfsAttributeGetPhysicalAllocationSize(
                        DataAttr);
                FileCB->CommonFCBHeader.FileSize.QuadPart       = DataAttr->NonResident.DataSize;
                FileCB->CommonFCBHeader.ValidDataLength.QuadPart= DataAttr->NonResident.InitalizedDataSize;
            }
            else
            {
                /* Cc requires AllocationSize >= FileSize; resident data is
                 * wholly contained in the record, so they are equal. */
                FileCB->CommonFCBHeader.AllocationSize.QuadPart = DataAttr->Resident.DataLength;
                FileCB->CommonFCBHeader.FileSize.QuadPart       = DataAttr->Resident.DataLength;
                FileCB->CommonFCBHeader.ValidDataLength.QuadPart= DataAttr->Resident.DataLength;
            }
        }
        else
        {
            FileCB->CommonFCBHeader.AllocationSize.QuadPart = 0;
            FileCB->CommonFCBHeader.FileSize.QuadPart       = 0;
            FileCB->CommonFCBHeader.ValidDataLength.QuadPart= 0;
        }

        if (FileCB->StreamCB)
        {
            /* Mm requires SectionObjectPointer for image and data sections. */
            FileObject->SectionObjectPointer =
                &FileCB->StreamCB->SectionObjectPointers;
        }
        FileObject->FsContext = FileCB;
    }

    // Open file.
    if (ExternalBackingDeleted)
    {
        FileObject->Flags |=
            FO_FILE_MODIFIED |
            FO_FILE_SIZE_CHANGED;
    }
    Irp->IoStatus.Information = FILE_OPENED;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return STATUS_SUCCESS;
}
