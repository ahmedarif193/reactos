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

/* Whole-dispatch accounting: where does an open or create actually go. */
static LONG NtfsProfCreates = 0;
static LONG64 NtfsProfQuery = 0;     /* path resolution */
static LONG64 NtfsProfDisp = 0;      /* disposition incl. library create */
static LONG64 NtfsProfFcb = 0;       /* context-block build */
static LONG64 NtfsProfTail = 0;      /* dir load, sizes, cache map, open */
LONG64 NtfsProfCleanup = 0;
LONG64 NtfsProfClose = 0;
LONG NtfsProfCleanupCount = 0;
LONG NtfsProfCloseCount = 0;
extern LONG NtfsIoReadCount;
extern LONG NtfsIoWriteCount;
extern LONG64 NtfsIoReadTicks;
extern LONG64 NtfsIoWriteTicks;

_Function_class_(IRP_MJ_CREATE)
_Function_class_(DRIVER_DISPATCH)
/* The cache manager retains this table for the lifetime of every cache map,
 * so it must not live on the stack of the create that installs it. */
static const CACHE_MANAGER_CALLBACKS NtfsCacheManagerCallbacks =
{
    NtfsAcqLazyWrite,
    NtfsRelLazyWrite,
    NtfsAcqReadAhead,
    NtfsRelReadAhead
};

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
            NtfsDirectoryDestroy(FileCB->FileDir);
        if (FileCB->StreamCB)
        {
            NtfsDereferenceStreamContext(
                (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension,
                FileCB->StreamCB);
        }
        if (FileCB->RequestedStream)
            ExFreePool(FileCB->RequestedStream);
        if (FileCB->FileName.Buffer)
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
    LARGE_INTEGER ProfT0, ProfT1, ProfT2, ProfT3, ProfT4;
    ProfT0.QuadPart = 0; ProfT1.QuadPart = 0; ProfT2.QuadPart = 0; ProfT3.QuadPart = 0; ProfT4.QuadPart = 0;
    ProfT0 = KeQueryPerformanceCounter(NULL);
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
    else
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

    ProfT1 = KeQueryPerformanceCounter(NULL);
    if ((Status == STATUS_NOT_FOUND ||
         Status == STATUS_OBJECT_NAME_NOT_FOUND ||
         Status == STATUS_OBJECT_PATH_NOT_FOUND) &&
        !(IrpSp->Parameters.Create.Options & FILE_OPEN_REPARSE_POINT))
    {
        NtfsRecordNameMissing(VolCB,
                              FileObject->FileName.Buffer,
                              FileObject->FileName.Length / sizeof(WCHAR));
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

    ProfT2 = KeQueryPerformanceCounter(NULL);
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

    PWCHAR FileNameBuffer = (PWCHAR)ExAllocatePoolWithTag(PagedPool,
                                                          FileNameLength * sizeof(WCHAR),
                                                          TAG_NTFS);
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
    // NOTE: FileNameBuffer gets freed when the FileCB is cleaned up.

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

    ProfT3 = KeQueryPerformanceCounter(NULL);
    FileCB->CachedRecord = CachedRecord;
    FileCB->FileRec = CurrentFile;
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
            if (FileCB->FileName.Buffer)
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

    /* Assume that this is the first file stream request.
     * For more details see:
     * https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/ns-wdm-_section_object_pointers
     *
     * TODO: Handle multiple opened files pointing to the same stream properly.
     */
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

    if (!!(NtfsFileRecordGetHeader(CurrentFile)->Flags & FR_IS_DIRECTORY))
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

        Status = NtfsDirectoryLoadDirectory(FileCB->FileDir, FileCB->FileRec);
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

    // Initialize cache map on first open when we have valid sizes
    {
        // Use the CommonFCBHeader fields as the canonical CC_FILE_SIZES storage
        PCC_FILE_SIZES FileSizes = (PCC_FILE_SIZES)&FileCB->CommonFCBHeader.AllocationSize;
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

        /*
         * Mm requires SectionObjectPointer for image and data sections even
         * when ordinary cached reads are disabled. The cache-read switch only
         * controls whether this file object gets a private Cache Manager map.
        */
        FileObject->SectionObjectPointer = &FileCB->StreamCB->SectionObjectPointers;
        FileObject->FsContext = FileCB;

        if (NtfsCachedReadsEnabled &&
            FileObject->PrivateCacheMap == NULL)
        {
            CcInitializeCacheMap(FileObject,
                                 FileSizes,
                                 FALSE,
                                 &NtfsCacheManagerCallbacks,
                                 FileCB);
            CcSetFileSizes(FileObject, FileSizes);
            CcSetReadAheadGranularity(FileObject, 0x10000);
            FileObject->Flags |= FO_CACHE_SUPPORTED;
        }
    }

    // Open file.
    if (ExternalBackingDeleted)
    {
        FileObject->Flags |=
            FO_FILE_MODIFIED |
            FO_FILE_SIZE_CHANGED;
    }
    ProfT4 = KeQueryPerformanceCounter(NULL);
    if (ProfT1.QuadPart && ProfT3.QuadPart)
    {
        InterlockedAdd64(&NtfsProfQuery, ProfT1.QuadPart - ProfT0.QuadPart);
        InterlockedAdd64(&NtfsProfDisp, ProfT2.QuadPart - ProfT1.QuadPart);
        InterlockedAdd64(&NtfsProfFcb, ProfT3.QuadPart - ProfT2.QuadPart);
        InterlockedAdd64(&NtfsProfTail, ProfT4.QuadPart - ProfT3.QuadPart);
        if ((InterlockedIncrement(&NtfsProfCreates) & 0x3F) == 0)
        {
            DPRINT1("CRACCT n=%ld query=%I64d disp=%I64d fcb=%I64d tail=%I64d clean(%ld)=%I64d close(%ld)=%I64d\n",
                    NtfsProfCreates, NtfsProfQuery, NtfsProfDisp,
                    NtfsProfFcb, NtfsProfTail,
                    NtfsProfCleanupCount, NtfsProfCleanup,
                    NtfsProfCloseCount, NtfsProfClose);
            DPRINT1("IOACCT rd(%ld)=%I64d wr(%ld)=%I64d\n",
                    NtfsIoReadCount, NtfsIoReadTicks,
                    NtfsIoWriteCount, NtfsIoWriteTicks);
        }
    }

    Irp->IoStatus.Information = FILE_OPENED;
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return STATUS_SUCCESS;
}
