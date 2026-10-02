/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new entry point
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2024 Carl J. Bialorucki <carl.bialorucki@reactos.org>
 */

#include "ntfspch.h"

/* GLOBALS *****************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(INIT, DriverEntry)
#pragma alloc_text(PAGE, NtfsUnload)
#pragma alloc_text(PAGE, NtfsFsdCleanup)
#pragma alloc_text(PAGE, NtfsFsdLockControl)
#pragma alloc_text(PAGE, NtfsFsdDeviceControl)
#pragma alloc_text(PAGE, NtfsFsdShutdown)
#endif

PDEVICE_OBJECT NtfsDiskFileSystemDeviceObject;

#define TAG_IRP_CTXT 'iftN'
#define TAG_ATT_CTXT 'aftN'
#define TAG_FILE_REC 'rftN'
#define TAG_FCB 'FftN'

CACHE_MANAGER_CALLBACKS CacheMgrCallbacks;
FAST_IO_DISPATCH FastIoDispatch;
NPAGED_LOOKASIDE_LIST IrpContextLookasideList;
NPAGED_LOOKASIDE_LIST FcbLookasideList;
NPAGED_LOOKASIDE_LIST AttrCtxtLookasideList;
PDRIVER_OBJECT NtfsDriverObject;
/* FUNCTIONS ****************************************************************/
NTSTATUS
NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
            _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;
    UNICODE_STRING UnicodeString;
    NtfsDriverObject = DriverObject;
    UNREFERENCED_PARAMETER(RegistryPath);
    RtlInitUnicodeString(&UnicodeString, L"\\Ntfs");
    Status = IoCreateDevice(DriverObject,
                            0,
                            &UnicodeString,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &NtfsDiskFileSystemDeviceObject);
    if (!NT_SUCCESS( Status )) {
        DPRINT("NtfsDriverEntry: Failed with Status %X\n", Status);
        return Status;
    }
    DriverObject->DriverUnload = NtfsUnload;

    DriverObject->MajorFunction[IRP_MJ_CREATE]                   = NtfsFsdCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE]                    = NtfsFsdClose;
    DriverObject->MajorFunction[IRP_MJ_READ]                     = NtfsFsdRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE]                    = NtfsFsdWrite;
    DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION]        = NtfsFsdQueryInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION]          = NtfsFsdSetInformation;
    DriverObject->MajorFunction[IRP_MJ_QUERY_EA]                 = NtfsFsdQueryEa;
    DriverObject->MajorFunction[IRP_MJ_SET_EA]                   = NtfsFsdSetEa;
    DriverObject->MajorFunction[IRP_MJ_QUERY_SECURITY]           = NtfsFsdQuerySecurity;
    DriverObject->MajorFunction[IRP_MJ_SET_SECURITY]             = NtfsFsdSetSecurity;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS]            = NtfsFsdFlushBuffers;
    DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = NtfsFsdQueryVolumeInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION]   = NtfsFsdSetVolumeInformation;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP]                  = NtfsFsdCleanup;
    DriverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL]        = NtfsFsdDirectoryControl;
    DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL]      = NtfsFsdFileSystemControl;
    DriverObject->MajorFunction[IRP_MJ_LOCK_CONTROL]             = NtfsFsdLockControl;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL]           = NtfsFsdDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN]                 = NtfsFsdShutdown;
    //DriverObject->MajorFunction[IRP_MJ_PNP]                      = NtfsFsdPnp;

    // Do not set DO_DIRECT_IO/DO_BUFFERED_IO flags on the FS control device
    // The I/O manager and Cache Manager will decide buffering for file I/O

    // Initialize FastIo dispatch table
    RtlZeroMemory(&FastIoDispatch, sizeof(FAST_IO_DISPATCH));
    FastIoDispatch.SizeOfFastIoDispatch = sizeof(FAST_IO_DISPATCH);
    FastIoDispatch.FastIoCheckIfPossible = NtfsFastIoCheckIfPossible;
    // Defer to CopyRead/CopyWrite if we later support it, for now return FALSE
    FastIoDispatch.FastIoRead = NtfsFastIoRead;
    FastIoDispatch.FastIoWrite = NtfsFastIoWrite;
    FastIoDispatch.FastIoQueryBasicInfo = NtfsFastIoQueryBasicInfo;
    FastIoDispatch.FastIoQueryStandardInfo = NtfsFastIoQueryStandardInfo;
    FastIoDispatch.FastIoQueryNetworkOpenInfo = NtfsFastIoQueryNetworkOpenInfo;
    FastIoDispatch.AcquireFileForNtCreateSection = (PFAST_IO_ACQUIRE_FILE)NtfsFastIoAcquireFileForNtCreateSection;
    FastIoDispatch.ReleaseFileForNtCreateSection = (PFAST_IO_RELEASE_FILE)NtfsFastIoReleaseFileForNtCreateSection;
    FastIoDispatch.FastIoDetachDevice = (PFAST_IO_DETACH_DEVICE)NtfsFastIoDetachDevice;
    FastIoDispatch.FastIoQueryOpen = NtfsFastIoQueryOpen;

    // Register Fast I/O dispatch at driver level so all created FS device objects inherit it
    DriverObject->FastIoDispatch = &FastIoDispatch;

    // Get global driver settings from registry
    GetGlobalSettingsFromRegistry();

    // Register file system
    IoRegisterFileSystem(NtfsDiskFileSystemDeviceObject);
    ObReferenceObject(NtfsDiskFileSystemDeviceObject);

    return STATUS_SUCCESS;
}

_Function_class_(IRP_MJ_LOCK_CONTROL)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdLockControl(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                   _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Handles lock and unlock requests.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-lock-control
     */
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFileContextBlock FileCB = NtfsGetFileContext(IrpSp->FileObject);

    if (!FileCB || !FileCB->StreamCB)
    {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
        return STATUS_INVALID_PARAMETER;
    }

    /* The lock package owns the request from here, including completing it. */
    return FsRtlProcessFileLock(&FileCB->StreamCB->FileLock, Irp, NULL);
}

_Function_class_(IRP_MJ_DEVICE_CONTROL)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdDeviceControl(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                     _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Determine if volume is open.
     * If it is, pass the IRP to the appropriate storage driver.
     * If not, fail the IRP.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-device-control
     */

    // Shamelessly ripped from the old driver.

    PVolumeContextBlock DeviceExt;
    PFILE_OBJECT FileObject;
    PFileContextBlock FileCB;

    FileObject = IoGetCurrentIrpStackLocation(Irp)->FileObject;
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject && FileObject)
    {
        FileCB = NtfsGetFileContext(FileObject);
        if (!FileCB || !FileCB->IsVolumeOpen)
        {
            Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
            Irp->IoStatus.Information = 0;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_INVALID_PARAMETER;
        }
    }

    DeviceExt = (PVolumeContextBlock)(VolumeDeviceObject->DeviceExtension);
    IoSkipCurrentIrpStackLocation(Irp);

    /* Lower driver will complete - we don't have to */
    // IrpContext->Flags &= ~IRPCONTEXT_COMPLETE;

    return IoCallDriver(DeviceExt->StorageDevice, Irp);
}

_Function_class_(IRP_MJ_SHUTDOWN)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdShutdown (_In_ PDEVICE_OBJECT VolumeDeviceObject,
                 _Inout_ PIRP Irp)
{
    NTSTATUS Status = NtfsDiskFlushKm();

    /* Overview:
     * Occurs when the system is being shutdown.
     * Do any cleanup needed and return STATUS_SUCCESS.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-shutdown
     */
    UNREFERENCED_PARAMETER(VolumeDeviceObject);
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

_Function_class_(DRIVER_UNLOAD)
VOID
NTAPI
NtfsUnload(_In_ _Unreferenced_parameter_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    ObDereferenceObject(NtfsDiskFileSystemDeviceObject);
}

static
NTSTATUS
NtfsPrepareFileDeletion(_In_ PVolumeContextBlock VolCB,
                         _In_ PStreamContextBlock FileStream,
                         _In_ BOOLEAN WholeFile,
                         _Out_ PStreamContextBlock** DeleteStreams,
                         _Out_ PSIZE_T DeleteStreamCount)
{
    PLIST_ENTRY Entry;
    PStreamContextBlock* Streams;
    SIZE_T Count = 0;
    SIZE_T Index = 0;
    BOOLEAN OpenHandles = FALSE;

    *DeleteStreams = NULL;
    *DeleteStreamCount = 0;
    ExAcquireFastMutex(&VolCB->StreamListMutex);
    for (Entry = VolCB->StreamList.Flink; Entry != &VolCB->StreamList; Entry = Entry->Flink)
    {
        PStreamContextBlock Stream = CONTAINING_RECORD(Entry, StreamContextBlock, ListEntry);

        if (Stream == FileStream ||
            (WholeFile && Stream->FileReference == FileStream->FileReference))
        {
            Count++;
            OpenHandles |= Stream->UncleanCount != 0;
        }
    }
    if (OpenHandles)
    {
        for (Entry = VolCB->StreamList.Flink; Entry != &VolCB->StreamList; Entry = Entry->Flink)
        {
            PStreamContextBlock Stream = CONTAINING_RECORD(Entry, StreamContextBlock, ListEntry);

            if (Stream == FileStream ||
                (WholeFile && Stream->FileReference == FileStream->FileReference))
                Stream->DeletePending = TRUE;
        }
        ExReleaseFastMutex(&VolCB->StreamListMutex);
        return STATUS_PENDING;
    }
    if (Count > MAXULONG_PTR / sizeof(*Streams))
    {
        ExReleaseFastMutex(&VolCB->StreamListMutex);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Streams = ExAllocatePoolWithTag(PagedPool, Count * sizeof(*Streams), TAG_NTFS);
    if (!Streams)
    {
        ExReleaseFastMutex(&VolCB->StreamListMutex);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    for (Entry = VolCB->StreamList.Flink; Entry != &VolCB->StreamList; Entry = Entry->Flink)
    {
        PStreamContextBlock Stream = CONTAINING_RECORD(Entry, StreamContextBlock, ListEntry);

        if (Stream == FileStream ||
            (WholeFile && Stream->FileReference == FileStream->FileReference))
        {
            Stream->DeletePending = TRUE;
            Stream->ReferenceCount++;
            Streams[Index++] = Stream;
        }
    }
    ExReleaseFastMutex(&VolCB->StreamListMutex);
    *DeleteStreams = Streams;
    *DeleteStreamCount = Count;
    return STATUS_SUCCESS;
}

static
VOID
NtfsFinalizePendingDelete(_In_ PVolumeContextBlock VolCB,
                          _In_ PFileContextBlock FileCB,
                          _In_ PFILE_OBJECT FileObject,
                          _In_ BOOLEAN DeleteLink)
{
    BOOLEAN IsDirectory =
        !!(NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY);
    NTSTATUS DeleteStatus;
    PWCHAR DeletePath;
    PWCHAR ResolvedPath = NULL;
    ULONG DeletePathLength;
    BOOLEAN LastLink = TRUE;
    BOOLEAN MetadataAcquired = FALSE;
    BOOLEAN Deleted = FALSE;
    BOOLEAN RecordDeleted = FALSE;
    BOOLEAN StreamDeleted = FALSE;
    PStreamContextBlock* DeleteStreams = NULL;
    SIZE_T DeleteStreamCount = 0;
    SIZE_T StreamIndex;
    PStreamContextBlock BaseStream;
    BOOLEAN FileDeletion;
    BOOLEAN StreamOnly;

    KeEnterCriticalRegion();
    NtfsAcquireMetadata(VolCB);
    BaseStream = NtfsReferenceStreamContext(VolCB, FileCB->FileRec,
                                             IsDirectory ? TypeIndexAllocation : TypeData,
                                             IsDirectory ? L"$I30" : NULL);
    NtfsReleaseMetadata(VolCB);
    if (!BaseStream)
    {
        KeLeaveCriticalRegion();
        return;
    }
    ExAcquireResourceExclusiveLite(&BaseStream->MainResource, TRUE);
    if (BaseStream->Deleted || (FileCB->StreamCB && FileCB->StreamCB->Deleted))
    {
        ExReleaseResourceLite(&BaseStream->MainResource);
        NtfsDereferenceStreamContext(VolCB, BaseStream);
        KeLeaveCriticalRegion();
        return;
    }
    FileDeletion = BaseStream->DeletePending;
    StreamOnly = !DeleteLink && !FileDeletion && FileCB->RequestedType == TypeData &&
                 FileCB->RequestedStream && FileCB->RequestedStream[0];
    if (FileCB->StreamCB != BaseStream)
        ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
    DeleteStatus = DeleteLink ? NtfsPersistPendingSize(VolCB, FileCB) : STATUS_SUCCESS;
    if (FileCB->StreamCB != BaseStream)
        ExReleaseResourceLite(NtfsGetMainResource(FileCB));
    if (!NT_SUCCESS(DeleteStatus))
        goto DeleteDone;
    NtfsAcquireMetadata(VolCB);
    MetadataAcquired = TRUE;
    DeletePath = FileCB->FileName.Buffer;
    DeletePathLength = FileCB->FileName.Length / sizeof(WCHAR);
    if (DeleteLink)
    {
        ULONG Index;

        for (Index = 0; Index < DeletePathLength; Index++)
        {
            if (DeletePath[Index] == L':')
            {
                DeletePathLength = Index;
                break;
            }
        }
    }

    if (FileCB->StreamCB && DeleteLink)
    {
        PNtfsFileRecord NamedRecord = NULL;
        ULONG RemainingNameLength = 0;
        BOOLEAN SameFile;

        DeleteStatus = NtfsMasterFileTableGetFileRecordFromQueryEx(
            NtfsVolumeGetMft(VolCB->DiskVolume),
            DeletePath, DeletePathLength, TRUE,
            &RemainingNameLength, &NamedRecord);
        SameFile = NT_SUCCESS(DeleteStatus) &&
                   RemainingNameLength == 0 && NamedRecord &&
                   ((((ULONGLONG)NtfsFileRecordGetHeader(NamedRecord)->SequenceNumber << 48) |
                      NtfsFileRecordGetHeader(NamedRecord)->MFTRecordNumber) ==
                     FileCB->StreamCB->FileReference);
        if (SameFile)
            LastLink = NtfsFileRecordGetLinkCount(NamedRecord) <= 1;
        if (NamedRecord)
            NtfsFileRecordDestroy(NamedRecord);
        if (!SameFile)
        {
            if (NT_SUCCESS(DeleteStatus))
                DeleteStatus = STATUS_OBJECT_NAME_NOT_FOUND;
            goto DeleteDone;
        }
    }
    if (LastLink)
    {
        if (FileCB->StreamCB)
        {
            FileDeletion |= !StreamOnly;
            DeleteStatus = NtfsPrepareFileDeletion(VolCB,
                                                   StreamOnly ? FileCB->StreamCB : BaseStream,
                                                   !StreamOnly,
                                                   &DeleteStreams, &DeleteStreamCount);
            if (DeleteStatus == STATUS_PENDING)
            {
                NtfsReleaseMetadata(VolCB);
                ExReleaseResourceLite(&BaseStream->MainResource);
                NtfsDereferenceStreamContext(VolCB, BaseStream);
                KeLeaveCriticalRegion();
                return;
            }
            if (!NT_SUCCESS(DeleteStatus))
                goto DeleteDone;
        }
        NtfsReleaseMetadata(VolCB);
        MetadataAcquired = FALSE;

        for (StreamIndex = 0; StreamIndex < DeleteStreamCount; StreamIndex++)
        {
            PStreamContextBlock Stream = DeleteStreams[StreamIndex];

            ExAcquireResourceExclusiveLite(&Stream->PagingIoResource, TRUE);
            Stream->Deleted = TRUE;
            Stream->SizePending = FALSE;
            ExReleaseResourceLite(&Stream->PagingIoResource);
        }
        if (FileObject->SectionObjectPointer)
        {
            if (FileObject->PrivateCacheMap)
            {
                LARGE_INTEGER Empty = { { 0, 0 } };

                CcUninitializeCacheMap(FileObject, &Empty, NULL);
            }
        }
        for (StreamIndex = 0; StreamIndex < DeleteStreamCount; StreamIndex++)
        {
            PSECTION_OBJECT_POINTERS Sections = &DeleteStreams[StreamIndex]->SectionObjectPointers;

            Sections->ImageSectionObject = NULL;
            CcPurgeCacheSection(Sections, NULL, 0, TRUE);
        }

        NtfsAcquireMetadata(VolCB);
        MetadataAcquired = TRUE;
    }
    DeleteStatus = STATUS_SUCCESS;
    if (FileCB->StreamCB && LastLink)
    {
        PNtfsFileRecord NamedRecord = NULL;
        ULONG RemainingNameLength = 0;
        BOOLEAN SameFile;

        SameFile = NT_SUCCESS(NtfsMasterFileTableGetFileRecordFromQueryEx(
                       NtfsVolumeGetMft(VolCB->DiskVolume),
                       DeletePath,
                       DeletePathLength,
                       TRUE,
                       &RemainingNameLength,
                       &NamedRecord)) &&
                   RemainingNameLength == 0 &&
                   NamedRecord &&
                   NtfsFileRecordGetHeader(NamedRecord)->MFTRecordNumber ==
                       NtfsFileRecordGetHeader(FileCB->FileRec)->MFTRecordNumber &&
                   NtfsFileRecordGetHeader(NamedRecord)->SequenceNumber ==
                       NtfsFileRecordGetHeader(FileCB->FileRec)->SequenceNumber;
        if (NamedRecord)
            NtfsFileRecordDestroy(NamedRecord);
        if (!SameFile)
        {
            ULONG ResolvedLength = 0;
            ULONG SuffixOffset = 0;
            ULONG SuffixLength;
            ULONG Capacity = MAXUSHORT / sizeof(WCHAR) - 1;

            if (DeleteLink)
            {
                DeleteStatus = STATUS_OBJECT_NAME_NOT_FOUND;
                goto DeleteDone;
            }

            while (SuffixOffset < DeletePathLength && DeletePath[SuffixOffset] != L':')
                SuffixOffset++;
            SuffixLength = DeletePathLength - SuffixOffset;

            ResolvedPath = ExAllocatePoolWithTag(PagedPool, MAXUSHORT, TAG_NTFS);
            if (!ResolvedPath)
                DeleteStatus = STATUS_INSUFFICIENT_RESOURCES;
            else
            {
                DeleteStatus = NtfsMasterFileTableGetPathFromFileReference(
                    NtfsVolumeGetMft(VolCB->DiskVolume),
                    FileCB->StreamCB->FileReference,
                    ResolvedPath,
                    Capacity,
                    &ResolvedLength);
            }
            if (NT_SUCCESS(DeleteStatus) &&
                (ResolvedLength > Capacity || SuffixLength > Capacity - ResolvedLength))
            {
                DeleteStatus = STATUS_NAME_TOO_LONG;
            }
            if (NT_SUCCESS(DeleteStatus))
            {
                RtlCopyMemory(ResolvedPath + ResolvedLength,
                              DeletePath + SuffixOffset,
                              SuffixLength * sizeof(WCHAR));
                ResolvedLength += SuffixLength;
                ResolvedPath[ResolvedLength] = UNICODE_NULL;
                DeletePath = ResolvedPath;
                DeletePathLength = ResolvedLength;
            }
        }
    }
    if (NT_SUCCESS(DeleteStatus) && StreamOnly)
    {
        DeleteStatus = NtfsRefreshDirectoryRecord(VolCB, FileCB);
        if (NT_SUCCESS(DeleteStatus))
        {
            DeleteStatus = NtfsFileRecordDeleteNamedDataStream(FileCB->FileRec,
                                                               FileCB->RequestedStream);
        }
        Deleted = NT_SUCCESS(DeleteStatus);
        StreamDeleted = Deleted;
    }
    else if (NT_SUCCESS(DeleteStatus))
    {
        DeleteStatus = NtfsMasterFileTableDeleteFileEx(
            NtfsVolumeGetMft(VolCB->DiskVolume),
            DeletePath,
            DeletePathLength,
            IsDirectory,
            FileCB->FileRec,
            LastLink,
            &RecordDeleted);
        Deleted = NT_SUCCESS(DeleteStatus);
    }
    InterlockedIncrement(&VolCB->DirGeneration);
    if (!StreamOnly)
    {
        NtfsEvictCachedRecord(VolCB,
                              DeletePath,
                              (USHORT)DeletePathLength,
                              RecordDeleted);
    }
    if (Deleted)
        NtfsRecordNameMissing(VolCB, DeletePath, (USHORT)DeletePathLength);
    if (Deleted && !StreamOnly)
    {
        NtfsReportFileChange(VolCB,
                             FileCB,
                             IsDirectory ? FILE_NOTIFY_CHANGE_DIR_NAME : FILE_NOTIFY_CHANGE_FILE_NAME,
                             FILE_ACTION_REMOVED);
    }

DeleteDone:
    if (!MetadataAcquired)
    {
        NtfsAcquireMetadata(VolCB);
        MetadataAcquired = TRUE;
    }
    if (!RecordDeleted && !StreamDeleted && FileCB->StreamCB)
    {
        PLIST_ENTRY Entry;

        ExAcquireFastMutex(&VolCB->StreamListMutex);
        for (Entry = VolCB->StreamList.Flink; Entry != &VolCB->StreamList; Entry = Entry->Flink)
        {
            PStreamContextBlock Stream = CONTAINING_RECORD(Entry, StreamContextBlock, ListEntry);

            if (Stream == FileCB->StreamCB ||
                (FileDeletion && Stream->FileReference == FileCB->StreamCB->FileReference))
            {
                PLIST_ENTRY CcbEntry;

                Stream->DeletePending = FALSE;
                if (FileDeletion)
                {
                    for (CcbEntry = Stream->NativeScb.CcbList.Flink;
                         CcbEntry != &Stream->NativeScb.CcbList;
                         CcbEntry = CcbEntry->Flink)
                    {
                        PNTFS_NATIVE_CCB Ccb = CONTAINING_RECORD(CcbEntry, NTFS_NATIVE_CCB, StreamEntry);
                        PFileContextBlock Other = CONTAINING_RECORD(Ccb, FileContextBlock, NativeCcb);

                        Other->DeletePending = FALSE;
                        if (Ccb->FileObject)
                            Ccb->FileObject->DeletePending = FALSE;
                    }
                }
            }
        }
        ExReleaseFastMutex(&VolCB->StreamListMutex);
    }
    if (DeleteLink)
        NtfsSetLinkDeletePending(FileCB, FALSE);
    NtfsReleaseMetadata(VolCB);
    if (ResolvedPath)
        ExFreePoolWithTag(ResolvedPath, TAG_NTFS);
    for (StreamIndex = 0; StreamIndex < DeleteStreamCount; StreamIndex++)
    {
        PStreamContextBlock Stream = DeleteStreams[StreamIndex];

        if (!RecordDeleted && !StreamDeleted)
        {
            ExAcquireResourceExclusiveLite(&Stream->PagingIoResource, TRUE);
            Stream->Deleted = FALSE;
            ExReleaseResourceLite(&Stream->PagingIoResource);
        }
        NtfsDereferenceStreamContext(VolCB, Stream);
    }
    if (DeleteStreams)
        ExFreePoolWithTag(DeleteStreams, TAG_NTFS);
    ExReleaseResourceLite(&BaseStream->MainResource);
    NtfsDereferenceStreamContext(VolCB, BaseStream);
    KeLeaveCriticalRegion();

    if (!NT_SUCCESS(DeleteStatus))
        DPRINT1("NtfsFsdCleanup: delete failed 0x%08lx\n", DeleteStatus);
}

VOID
NtfsCleanupFailedCreate(_In_ PVolumeContextBlock VolCB,
                        _In_ PFileContextBlock FileCB,
                        _In_ PFILE_OBJECT FileObject)
{
    BOOLEAN DeleteLink = FALSE;
    BOOLEAN DeleteRequested;

    if (!FileCB->StreamCB || !FileCB->NativeCcb.StreamEntry.Flink)
        return;
    KeEnterCriticalRegion();
    NtfsAcquireMetadata(VolCB);
    if (FileCB->CleanupComplete)
    {
        NtfsReleaseMetadata(VolCB);
        KeLeaveCriticalRegion();
        return;
    }
    FileCB->CleanupComplete = TRUE;
    if (FileCB->ShareAccessSet)
    {
        ExAcquireFastMutex(&VolCB->StreamListMutex);
        IoRemoveShareAccess(FileObject, &FileCB->StreamCB->ShareAccess);
        FileCB->ShareAccessSet = FALSE;
        InterlockedDecrement(&FileCB->StreamCB->UncleanCount);
        ExReleaseFastMutex(&VolCB->StreamListMutex);
    }
    if (FileCB->NativeCcb.Lcb)
    {
        ASSERT(FileCB->NativeCcb.Lcb->CleanupCount != 0);
        FileCB->NativeCcb.Lcb->CleanupCount--;
        DeleteLink = FileCB->DeletePending && FileCB->NativeCcb.Lcb->CleanupCount == 0;
    }
    DeleteRequested = DeleteLink ||
        (FileCB->StreamCB->DeletePending && FileCB->StreamCB->UncleanCount == 0);
    NtfsReleaseMetadata(VolCB);
    KeLeaveCriticalRegion();
    if (DeleteRequested && VolCB->DiskVolume && FileCB->FileRec &&
        !NtfsVolumeIsReadOnly(VolCB->DiskVolume) && FileCB->FileName.Length != 0)
    {
        NtfsFinalizePendingDelete(VolCB, FileCB, FileObject, DeleteLink);
    }
}

_Function_class_(IRP_MJ_CLEANUP)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdCleanup(_In_ PDEVICE_OBJECT VolumeDeviceObject,
               _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * If the device object is the control device, complete the IRP.
     * Otherwise, perform any cleanup as needed.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-cleanup
     */
    PIO_STACK_LOCATION IrpSp;
    PFileContextBlock FileCB;
    BOOLEAN FirstCleanup = FALSE;

    if (VolumeDeviceObject == NtfsDiskFileSystemDeviceObject)
    {
        // DeviceObject represents FileSystem
        Irp->IoStatus.Information = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileCB = NtfsGetFileContext(IrpSp->FileObject);

    if (FileCB && !FileCB->CleanupComplete)
    {
        PVolumeContextBlock VolCB =
            (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
        BOOLEAN NotifyUnlock = FALSE;

        ExAcquireFastMutex(&VolCB->VolumeStateMutex);
        if (FileCB->IsVolumeOpen)
        {
            KIRQL OldIrql;

            if (VolCB->VolumeHandleCount > 0)
            {
                IoRemoveShareAccess(IrpSp->FileObject,
                                    &VolCB->VolumeShareAccess);
                VolCB->VolumeHandleCount--;
            }

            if (VolCB->VolumeLockOwner == IrpSp->FileObject)
            {
                VolCB->VolumeLockOwner = NULL;
                IoAcquireVpbSpinLock(&OldIrql);
                VolumeDeviceObject->Vpb->Flags &= ~VPB_LOCKED;
                if (!VolCB->Dismounted)
                {
                    VolumeDeviceObject->Vpb->Flags &=
                        ~VPB_DIRECT_WRITES_ALLOWED;
                }
                IoReleaseVpbSpinLock(OldIrql);
                NotifyUnlock = TRUE;
            }
        }
        if (VolCB->OpenHandleCount > 0)
            VolCB->OpenHandleCount--;
        FileCB->CleanupComplete = TRUE;
        FirstCleanup = TRUE;
        ExReleaseFastMutex(&VolCB->VolumeStateMutex);

        if (NotifyUnlock)
        {
            FsRtlNotifyVolumeEvent(IrpSp->FileObject,
                                   FSRTL_VOLUME_UNLOCK);
        }
        IrpSp->FileObject->Flags |= FO_CLEANUP_COMPLETE;
    }

    // Do not free the FCB/stream structures here. Cleanup is called when the
    // last handle is closed, but the file object may still be referenced by
    // the cache/section. The actual deallocation is done on IRP_MJ_CLOSE.
    if (FileCB)
    {
        PVolumeContextBlock VolCB =
            (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
        BOOLEAN LastHandle;
        BOOLEAN DeleteLink = FALSE;

        if (FileCB->FileDir && VolCB->NotifySync)
        {
            KeEnterCriticalRegion();
            FsRtlNotifyCleanup(VolCB->NotifySync,
                               &VolCB->NotifyList,
                               FileCB);
            KeLeaveCriticalRegion();
        }
        LastHandle = TRUE;
        if (FileCB->StreamCB)
        {
            // Byte-range locks belong to the handle, so they end with it.
            FsRtlFastUnlockAll(&FileCB->StreamCB->FileLock,
                               IrpSp->FileObject,
                               IoGetRequestorProcess(Irp),
                               NULL);
            if (FileCB->ShareAccessSet)
            {
                ExAcquireFastMutex(&VolCB->StreamListMutex);
                IoRemoveShareAccess(IrpSp->FileObject,
                                    &FileCB->StreamCB->ShareAccess);
                ExReleaseFastMutex(&VolCB->StreamListMutex);
                FileCB->ShareAccessSet = FALSE;
                LastHandle = InterlockedDecrement(&FileCB->StreamCB->UncleanCount) == 0;
            }
            else
            {
                LastHandle = FileCB->StreamCB->UncleanCount == 0;
            }
        }

        if (FirstCleanup)
        {
            KeEnterCriticalRegion();
            NtfsAcquireMetadata(VolCB);
            if (FileCB->CreateOptions & FILE_DELETE_ON_CLOSE)
            {
                if (FileCB->NativeCcb.Lcb &&
                    !(FileCB->RequestedType == TypeData &&
                      FileCB->RequestedStream && FileCB->RequestedStream[0]))
                {
                    NtfsSetLinkDeletePending(FileCB, TRUE);
                }
                else if (FileCB->StreamCB)
                {
                    FileCB->StreamCB->DeletePending = TRUE;
                }
                if (FileCB->FileDir && VolCB->NotifySync)
                {
                    FsRtlNotifyFullChangeDirectory(VolCB->NotifySync,
                                                   &VolCB->NotifyList,
                                                   IrpSp->FileObject->FsContext,
                                                   NULL,
                                                   FALSE,
                                                   FALSE,
                                                   0,
                                                   NULL,
                                                   NULL,
                                                   NULL);
                }
            }
            if (FileCB->NativeCcb.Lcb)
            {
                ASSERT(FileCB->NativeCcb.Lcb->CleanupCount != 0);
                FileCB->NativeCcb.Lcb->CleanupCount--;
                DeleteLink = FileCB->DeletePending &&
                             FileCB->NativeCcb.Lcb->CleanupCount == 0;
            }
            NtfsReleaseMetadata(VolCB);
            KeLeaveCriticalRegion();
        }

        if (FileCB->StreamCB && FileCB->StreamCB->SizePending &&
            !FileCB->DeletePending && !(FileCB->CreateOptions & FILE_DELETE_ON_CLOSE))
        {
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
            NtfsPersistPendingSize(VolCB, FileCB);
            ExReleaseResourceLite(NtfsGetMainResource(FileCB));
            KeLeaveCriticalRegion();
        }

        /* The handle is going away, so a requested delete happens now. */
        if (FirstCleanup &&
            (DeleteLink || (FileCB->StreamCB
                 ? (FileCB->StreamCB->DeletePending && LastHandle)
                 : (FileCB->DeletePending || (FileCB->CreateOptions & FILE_DELETE_ON_CLOSE)))) &&
            VolCB->DiskVolume &&
            !NtfsVolumeIsReadOnly(VolCB->DiskVolume) &&
            FileCB->FileName.Length != 0)
        {
            /*
             * Cached pages of a file that is about to stop existing must go
             * before it does, or the cache manager keeps trying to write them
             * back to a record that has been freed.
             */
                /* TRUE also tears down the shared map, which outlives the
                 * private one and is what keeps retrying the write-back. */
            NtfsFinalizePendingDelete(VolCB, FileCB, IrpSp->FileObject, DeleteLink);
        }

        if (FirstCleanup && FileCB->NotifyFilter)
        {
            ULONG NotifyFilter = FileCB->NotifyFilter;

            FileCB->NotifyFilter = 0;
            if (FileCB->RequestedStream && FileCB->RequestedStream[0])
                NotifyFilter &= ~FILE_NOTIFY_CHANGE_SIZE;
            if (NotifyFilter && !(FileCB->StreamCB && FileCB->StreamCB->Deleted))
                NtfsReportFileChange(VolCB, FileCB, NotifyFilter, FILE_ACTION_MODIFIED);
        }

        /* The cache holds a file-object reference, so waiting for CLOSE to
         * release the private map prevents normal cached files from closing. */
        if (IrpSp->FileObject->PrivateCacheMap)
            CcUninitializeCacheMap(IrpSp->FileObject, NULL, NULL);
    }

    // TODO: How do we determine when the volume needs to get cleaned up?

    Irp->IoStatus.Information = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}
