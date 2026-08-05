/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new volume management
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfspch.h"

extern NPAGED_LOOKASIDE_LIST FileCBLookasideList;
//TODO:

static
NTSTATUS
NtfsGetVolumeInformation(PDEVICE_OBJECT DeviceObject,
                         PFILE_FS_VOLUME_INFORMATION Buffer,
                         PULONG Length)
{
    size_t VolumeInfoSize = sizeof(FILE_FS_VOLUME_INFORMATION);

    if (*Length < VolumeInfoSize + DeviceObject->Vpb->VolumeLabelLength)
        return STATUS_BUFFER_TOO_SMALL;

    Buffer->VolumeSerialNumber = DeviceObject->Vpb->SerialNumber;
    Buffer->VolumeLabelLength = DeviceObject->Vpb->VolumeLabelLength;
    RtlCopyMemory(Buffer->VolumeLabel,
                  DeviceObject->Vpb->VolumeLabel,
                  DeviceObject->Vpb->VolumeLabelLength);

    // TODO: Fix this
    Buffer->VolumeCreationTime.QuadPart = 0;
    Buffer->SupportsObjects = FALSE;

    // TODO: Investigate. Should we be returning the bytes written instead?
    *Length -= VolumeInfoSize + DeviceObject->Vpb->VolumeLabelLength;

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsGetSizeInfo(PDEVICE_OBJECT DeviceObject,
                PFILE_FS_SIZE_INFORMATION Buffer,
                PULONG Length)
{
    PNtfsVolume DiskVolume;

    if (*Length < sizeof(FILE_FS_SIZE_INFORMATION))
        return STATUS_BUFFER_OVERFLOW;

    DiskVolume = ((PVolumeContextBlock)(DeviceObject->DeviceExtension))->DiskVolume;

    if (!DiskVolume)
        return STATUS_INSUFFICIENT_RESOURCES;

    NtfsVolumeGetFreeClusters(DiskVolume, &Buffer->AvailableAllocationUnits);
    Buffer->TotalAllocationUnits.QuadPart = NtfsVolumeGetClustersInVolume(DiskVolume);
    Buffer->SectorsPerAllocationUnit = NtfsVolumeGetSectorsPerCluster(DiskVolume);
    Buffer->BytesPerSector = NtfsVolumeGetBytesPerSector(DiskVolume);

    *Length -= sizeof(FILE_FS_SIZE_INFORMATION);

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsGetAttributeInfo(PNtfsVolume DiskVolume,
                     PFILE_FS_ATTRIBUTE_INFORMATION Buffer,
                     PULONG Length)
{
    NTSTATUS Status;
    size_t BytesToWrite;
    LPCWSTR NTFSVerFormat;
    UNICODE_STRING NTFSVer;
    PNtfsLogFileService LFS;

    if (gShowVersionInfo)
    {
        // Report "NTFS x.x, Client x.x"
        BytesToWrite = sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 38;
        if (*Length < BytesToWrite)
            goto fallback;
        LFS = NtfsVolumeGetLFS(DiskVolume);
        Buffer->FileSystemNameLength = 40;
        NTFSVerFormat = L"NTFS %1ld.%1ld, Client %1ld.%1ld";
        RtlInitEmptyUnicodeString(&NTFSVer,
                                  Buffer->FileSystemName,
                                  40);
        Status = RtlUnicodeStringPrintf(&NTFSVer,
                                        NTFSVerFormat,
                                        NtfsVolumeGetMajorVersion(DiskVolume),
                                        NtfsVolumeGetMinorVersion(DiskVolume),
                                        NtfsLogFileServiceGetClientMajorVersion(LFS),
                                        NtfsLogFileServiceGetClientMinorVersion(LFS));
        if (!NT_SUCCESS(Status))
            goto fallback;
    }

    else
    {
fallback:
        // Report "NTFS"
        BytesToWrite = sizeof(FILE_FS_ATTRIBUTE_INFORMATION) + 6;
        if (*Length < BytesToWrite)
            return STATUS_BUFFER_TOO_SMALL;
        Buffer->FileSystemNameLength = 8;
        RtlCopyMemory(Buffer->FileSystemName, L"NTFS", 8);
        *Length -= BytesToWrite;
    }

    /* For more information on FileSystemAttributes:
     * https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-smb/3065351b-0b78-4976-9a5a-11657d8857c7
     *
     * TODO: Add attributes as needed.
     */
    Buffer->FileSystemAttributes = FILE_CASE_PRESERVED_NAMES
                                   | FILE_UNICODE_ON_DISK
                                   | FILE_NAMED_STREAMS
                                   | FILE_SUPPORTS_REPARSE_POINTS
                                   | FILE_SUPPORTS_EXTENDED_ATTRIBUTES;

    if (NtfsVolumeIsReadOnly(DiskVolume))
        Buffer->FileSystemAttributes |= FILE_READ_ONLY_VOLUME;

    Buffer->MaximumComponentNameLength = 255;
    *Length -= BytesToWrite;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsSetVolumeLabel(_In_ PDEVICE_OBJECT DeviceObject,
                   _In_ PFILE_FS_LABEL_INFORMATION NewLabel,
                   _In_ PULONG Length)
{
    NTSTATUS Status;
    PNtfsVolume DiskVolume;

    DiskVolume = ((PVolumeContextBlock)(DeviceObject->DeviceExtension))->DiskVolume;

    if (!DiskVolume || !NewLabel)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfsVolumeSetVolumeLabel(DiskVolume,
                                      NewLabel->VolumeLabel,
                                      NewLabel->VolumeLabelLength);

    if (!NT_SUCCESS(Status))
        return Status;

    // Re-read volume label.
    Status = NtfsVolumeGetVolumeLabel(DiskVolume,
                                      DeviceObject->Vpb->VolumeLabel,
                                      &DeviceObject->Vpb->VolumeLabelLength);
    return Status;
}

/* GLOBALS *****************************************************************/
#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtfsFsdQueryVolumeInformation)
#pragma alloc_text(PAGE, NtfsFsdSetVolumeInformation)
#pragma alloc_text(PAGE, NtfsMountVolume)
#endif

/* FUNCTIONS ****************************************************************/
_Function_class_(IRP_MJ_QUERY_VOLUME_INFORMATION)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdQueryVolumeInformation(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                              _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Returns file system information.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-query-volume-information
     */
    PIO_STACK_LOCATION IoStack;
    FS_INFORMATION_CLASS FSInfoRequest;
    PVolumeContextBlock VolCB;
    NTSTATUS Status;
    PVOID SystemBuffer;
    ULONG BufferLength;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    FSInfoRequest = IoStack->Parameters.QueryVolume.FsInformationClass;
    VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    SystemBuffer = Irp->AssociatedIrp.SystemBuffer;
    BufferLength = IoStack->Parameters.QueryFile.Length;

    switch (FSInfoRequest)
    {
        case FileFsVolumeInformation:
            Status = NtfsGetVolumeInformation(VolumeDeviceObject,
                                              (PFILE_FS_VOLUME_INFORMATION)SystemBuffer,
                                              &BufferLength);
            break;
        case FileFsSizeInformation:
            Status = NtfsGetSizeInfo(VolumeDeviceObject,
                                     (PFILE_FS_SIZE_INFORMATION)SystemBuffer,
                                     &BufferLength);
            break;
        case FileFsAttributeInformation:
            Status = NtfsGetAttributeInfo(VolCB->DiskVolume,
                                          (PFILE_FS_ATTRIBUTE_INFORMATION)SystemBuffer,
                                          &BufferLength);
            break;
        case FileFsControlInformation:
        case FileFsDeviceInformation:
        case FileFsDriverPathInformation:
        case FileFsFullSizeInformation:
            Status = STATUS_NOT_IMPLEMENTED;
            break;
        case FileFsObjectIdInformation:
        /* Used in Windows 7+
         * case FileFsSectorSizeInformation:
         */
        default:
            Status = STATUS_NOT_IMPLEMENTED;
            break;
    }

    if (NT_SUCCESS(Status))
        Irp->IoStatus.Information =
            IoStack->Parameters.QueryFile.Length - BufferLength;
    else
        Irp->IoStatus.Information = 0;

    return Status;
}

_Function_class_(IRP_MJ_SET_VOLUME_INFORMATION)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdSetVolumeInformation(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                            _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Handles requests to change file system information.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-set-volume-information
     */

    PIO_STACK_LOCATION IoStack;
    FS_INFORMATION_CLASS FSInfoRequest;
    NTSTATUS Status;
    PVOID SystemBuffer;
    ULONG BufferLength;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    FSInfoRequest = IoStack->Parameters.QueryVolume.FsInformationClass;
    SystemBuffer = Irp->AssociatedIrp.SystemBuffer;
    BufferLength = IoStack->Parameters.QueryFile.Length;

    switch (FSInfoRequest)
    {
        case FileFsLabelInformation:
            Status = NtfsSetVolumeLabel(VolumeDeviceObject,
                                        (PFILE_FS_LABEL_INFORMATION)SystemBuffer,
                                        &BufferLength);
            break;
        case FileFsControlInformation:
        case FileFsObjectIdInformation:
        default:
            Status = STATUS_NOT_IMPLEMENTED;
            break;
    }

    if (NT_SUCCESS(Status))
        Irp->IoStatus.Information =
            IoStack->Parameters.QueryFile.Length - BufferLength;
    else
        Irp->IoStatus.Information = 0;

    return Status;
}

_Requires_lock_held_(_Global_critical_region_)
NTSTATUS
NtfsMountVolume(IN PDEVICE_OBJECT TargetDeviceObject,
                IN PVPB Vpb,
                IN PDEVICE_OBJECT FsDeviceObject)
{
    PDEVICE_OBJECT FSDeviceObject;
    PNtfsVolume DiskVolume;
    NTSTATUS Status;
    PVolumeContextBlock VolCB;
    DISK_GEOMETRY DiskGeometry;
    ULONG Index;
    ULONG Size;

    // Get disk geometry.
    Size = sizeof(DISK_GEOMETRY);
    Status = DeviceIoControl(TargetDeviceObject,
                             IOCTL_DISK_GET_DRIVE_GEOMETRY,
                             NULL,
                             0,
                             &DiskGeometry,
                             &Size,
                             TRUE);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    // Set up NTFS library to use disk routines.
    Status = NtfsDiskPrepareMountKm(TargetDeviceObject,
                                    DiskGeometry.BytesPerSector);

    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* Check if we're really NTFS. It's OK if we're not.
     * We're a boot driver, NT will try every possible filesystem.
     */
    Status = NtfsProbePartitionAndOpenVolume(DiskGeometry.BytesPerSector,
                                             &DiskVolume);

    if (!NT_SUCCESS(Status))
        goto Cleanup;

    // Create file system device object.
    Status = IoCreateDevice(NtfsDriverObject,
                            sizeof(VolumeContextBlock),
                            NULL,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &FSDeviceObject);

    if (!NT_SUCCESS(Status))
        goto Cleanup;

    // Do not force buffered or direct I/O at FS level; leave to I/O manager/CC

    // Set up FastIo dispatch table for this volume
    FSDeviceObject->DriverObject->FastIoDispatch = &FastIoDispatch;

    // Initialize Volume Context Block VolCB.
    VolCB = (PVolumeContextBlock)FSDeviceObject->DeviceExtension;
    RtlZeroMemory(VolCB, sizeof(VolumeContextBlock));
    Status = ExInitializeResourceLite(&VolCB->MetadataResource);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(FSDeviceObject);
        goto Cleanup;
    }
    ExInitializeFastMutex(&VolCB->VolumeStateMutex);
    ExInitializeFastMutex(&VolCB->StreamListMutex);
    ExInitializeFastMutex(&VolCB->MissingNameMutex);
    InitializeListHead(&VolCB->MissingNameList);
    for (Index = 0; Index < NTFS_MISSING_NAME_BUCKETS; Index++)
        InitializeListHead(&VolCB->MissingNameHash[Index]);
    VolCB->MissingNameCount = 0;
    ExInitializeFastMutex(&VolCB->RecordCacheMutex);
    InitializeListHead(&VolCB->RecordCacheList);
    for (Index = 0; Index < NTFS_RECORD_CACHE_BUCKETS; Index++)
        InitializeListHead(&VolCB->RecordCacheHash[Index]);
    VolCB->RecordCacheCount = 0;
    ExInitializeFastMutex(&VolCB->DirCacheMutex);
    VolCB->CachedDir = NULL;
    VolCB->CachedDirBusy = FALSE;
    VolCB->CachedDirPathLength = 0;
    VolCB->CachedDirGeneration = 0;
    VolCB->DirGeneration = 0;
    ExInitializeFastMutex(&VolCB->IdleFcbMutex);
    InitializeListHead(&VolCB->IdleFcbList);
    VolCB->IdleFcbCount = 0;
    InitializeListHead(&VolCB->StreamList);
    InitializeListHead(&VolCB->NotifyList);
    FsRtlNotifyInitializeSync(&VolCB->NotifySync);
    FSDeviceObject->Vpb = TargetDeviceObject->Vpb;

    // Give VolCB access to Ntfs Partition object
    VolCB->DiskVolume = DiskVolume;
    VolCB->BytesPerSector = DiskGeometry.BytesPerSector;

    // Set up storage device in VolCB.
    VolCB->StorageDevice = TargetDeviceObject;
    VolCB->StorageDevice->Vpb->DeviceObject = FSDeviceObject;
    VolCB->StorageDevice->Vpb->RealDevice = VolCB->StorageDevice;
    VolCB->StorageDevice->Vpb->Flags |= VPB_MOUNTED;
    FSDeviceObject->StackSize = VolCB->StorageDevice->StackSize + 1;

    // Tell IO manager we are done initializing.
    FSDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    // Create file stream object.
    VolCB->StreamFileObject = IoCreateStreamFileObject(NULL,
                                                       VolCB->StorageDevice);

    // Get serial number.
    FSDeviceObject->Vpb->SerialNumber = NtfsVolumeGetSerialNumber(DiskVolume);

    // Get volume label.
    Status = NtfsVolumeGetVolumeLabel(DiskVolume,
                                      FSDeviceObject->Vpb->VolumeLabel,
                                      &FSDeviceObject->Vpb->VolumeLabelLength);

    // Mount volume.
    FsRtlNotifyVolumeEvent(VolCB->StreamFileObject, FSRTL_VOLUME_MOUNT);

    Status = STATUS_SUCCESS;

Cleanup:
    return Status;
}


/* MISSING-NAME CACHE *******************************************************/

static
BOOLEAN
NtfsMissingNameMatches(_In_ PNtfsMissingName Entry,
                       _In_ ULONG Hash,
                       _In_reads_(Length) PCWSTR Name,
                       _In_ USHORT Length)
{
    USHORT Index;

    if (Entry->Hash != Hash || Entry->Length != Length)
        return FALSE;

    for (Index = 0; Index < Length; Index++)
    {
        if (RtlUpcaseUnicodeChar(Entry->Name[Index]) !=
            RtlUpcaseUnicodeChar(Name[Index]))
        {
            return FALSE;
        }
    }
    return TRUE;
}

/*
 * Repeated lookups of a name that is not there would otherwise walk the
 * directory index every time, so remember the misses.
 */
BOOLEAN
NtfsIsNameKnownMissing(_In_ PVolumeContextBlock VolCB,
                       _In_reads_(Length) PCWSTR Name,
                       _In_ USHORT Length)
{
    ULONG Hash;
    PLIST_ENTRY Entry;
    BOOLEAN Found = FALSE;

    if (!VolCB || !Name || !Length)
        return FALSE;

    Hash = NtfsHashName(Name, Length);
    ExAcquireFastMutex(&VolCB->MissingNameMutex);
    for (Entry = VolCB->MissingNameHash[
             Hash & (NTFS_MISSING_NAME_BUCKETS - 1)].Flink;
         Entry != &VolCB->MissingNameHash[
             Hash & (NTFS_MISSING_NAME_BUCKETS - 1)];
         Entry = Entry->Flink)
    {
        PNtfsMissingName Missing =
            CONTAINING_RECORD(Entry, NtfsMissingName, HashLink);

        if (NtfsMissingNameMatches(Missing, Hash, Name, Length))
        {
            /* Keep the hot names at the front so eviction takes the cold ones. */
            RemoveEntryList(&Missing->Link);
            InsertHeadList(&VolCB->MissingNameList, &Missing->Link);
            Found = TRUE;
            break;
        }
    }
    ExReleaseFastMutex(&VolCB->MissingNameMutex);
    return Found;
}

VOID
NtfsRecordNameMissing(_In_ PVolumeContextBlock VolCB,
                      _In_reads_(Length) PCWSTR Name,
                      _In_ USHORT Length)
{
    ULONG Hash;
    PLIST_ENTRY Entry;
    PNtfsMissingName Missing;

    if (!VolCB || !Name || !Length)
        return;

    Missing = (PNtfsMissingName)ExAllocatePoolUninitialized(
        NonPagedPool,
        FIELD_OFFSET(NtfsMissingName, Name) + Length * sizeof(WCHAR),
        TAG_NTFS);
    if (!Missing)
        return;

    Hash = NtfsHashName(Name, Length);
    Missing->Hash = Hash;
    Missing->Length = Length;
    RtlCopyMemory(Missing->Name, Name, Length * sizeof(WCHAR));

    ExAcquireFastMutex(&VolCB->MissingNameMutex);
    for (Entry = VolCB->MissingNameHash[
             Hash & (NTFS_MISSING_NAME_BUCKETS - 1)].Flink;
         Entry != &VolCB->MissingNameHash[
             Hash & (NTFS_MISSING_NAME_BUCKETS - 1)];
         Entry = Entry->Flink)
    {
        if (NtfsMissingNameMatches(
                CONTAINING_RECORD(Entry, NtfsMissingName, HashLink),
                                   Hash, Name, Length))
        {
            ExReleaseFastMutex(&VolCB->MissingNameMutex);
            ExFreePoolWithTag(Missing, TAG_NTFS);
            return;
        }
    }

    InsertHeadList(&VolCB->MissingNameList, &Missing->Link);
    InsertHeadList(
        &VolCB->MissingNameHash[
            Hash & (NTFS_MISSING_NAME_BUCKETS - 1)],
        &Missing->HashLink);
    VolCB->MissingNameCount++;
    while (VolCB->MissingNameCount > NTFS_MAX_MISSING_NAMES)
    {
        PLIST_ENTRY Oldest = RemoveTailList(&VolCB->MissingNameList);
        PNtfsMissingName Evicted =
            CONTAINING_RECORD(Oldest, NtfsMissingName, Link);

        RemoveEntryList(&Evicted->HashLink);
        VolCB->MissingNameCount--;
        ExFreePoolWithTag(Evicted, TAG_NTFS);
    }
    ExReleaseFastMutex(&VolCB->MissingNameMutex);
}

VOID
NtfsForgetMissingName(_In_ PVolumeContextBlock VolCB,
                      _In_reads_(Length) PCWSTR Name,
                      _In_ USHORT Length)
{
    ULONG Hash;
    PLIST_ENTRY Entry;

    if (!VolCB || !Name || !Length)
        return;

    Hash = NtfsHashName(Name, Length);
    ExAcquireFastMutex(&VolCB->MissingNameMutex);
    for (Entry = VolCB->MissingNameHash[
             Hash & (NTFS_MISSING_NAME_BUCKETS - 1)].Flink;
         Entry != &VolCB->MissingNameHash[
             Hash & (NTFS_MISSING_NAME_BUCKETS - 1)];
         Entry = Entry->Flink)
    {
        PNtfsMissingName Missing =
            CONTAINING_RECORD(Entry, NtfsMissingName, HashLink);

        if (NtfsMissingNameMatches(Missing, Hash, Name, Length))
        {
            RemoveEntryList(&Missing->Link);
            RemoveEntryList(&Missing->HashLink);
            VolCB->MissingNameCount--;
            ExReleaseFastMutex(&VolCB->MissingNameMutex);
            ExFreePoolWithTag(Missing, TAG_NTFS);
            return;
        }
    }
    ExReleaseFastMutex(&VolCB->MissingNameMutex);
}

VOID
NtfsForgetAllMissingNames(_In_ PVolumeContextBlock VolCB)
{
    LIST_ENTRY Stale;
    ULONG Index;

    if (!VolCB)
        return;

    InitializeListHead(&Stale);
    ExAcquireFastMutex(&VolCB->MissingNameMutex);
    if (!IsListEmpty(&VolCB->MissingNameList))
    {
        Stale = VolCB->MissingNameList;
        Stale.Flink->Blink = &Stale;
        Stale.Blink->Flink = &Stale;
        InitializeListHead(&VolCB->MissingNameList);
    }
    for (Index = 0; Index < NTFS_MISSING_NAME_BUCKETS; Index++)
        InitializeListHead(&VolCB->MissingNameHash[Index]);
    VolCB->MissingNameCount = 0;
    ExReleaseFastMutex(&VolCB->MissingNameMutex);

    while (!IsListEmpty(&Stale))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&Stale);

        ExFreePoolWithTag(CONTAINING_RECORD(Entry, NtfsMissingName, Link), TAG_NTFS);
    }
}

/* PARSED RECORD CACHE ******************************************************/

/*
 * Resolving a path parses the directory index and the file record from disk.
 * Handles come and go far faster than the files behind them change, so a
 * record is kept after its last handle closes and handed to the next open of
 * the same name. Entries in use are never freed; eviction only takes idle ones.
 */

static
BOOLEAN
NtfsCachedRecordMatches(_In_ PNtfsCachedRecord Entry,
                        _In_ ULONG Hash,
                        _In_reads_(Length) PCWSTR Name,
                        _In_ USHORT Length)
{
    USHORT Index;

    if (Entry->Hash != Hash || Entry->Length != Length)
        return FALSE;

    for (Index = 0; Index < Length; Index++)
    {
        if (RtlUpcaseUnicodeChar(Entry->Name[Index]) !=
            RtlUpcaseUnicodeChar(Name[Index]))
        {
            return FALSE;
        }
    }
    return TRUE;
}

/* Caller must hold RecordCacheMutex. */
static
PNtfsCachedRecord
NtfsLookupCachedRecordLocked(_In_ PVolumeContextBlock VolCB,
                             _In_ ULONG Hash,
                             _In_reads_(Length) PCWSTR Name,
                             _In_ USHORT Length)
{
    PLIST_ENTRY Bucket = &VolCB->RecordCacheHash[Hash & (NTFS_RECORD_CACHE_BUCKETS - 1)];
    PLIST_ENTRY Entry;

    for (Entry = Bucket->Flink; Entry != Bucket; Entry = Entry->Flink)
    {
        PNtfsCachedRecord Candidate = CONTAINING_RECORD(Entry, NtfsCachedRecord, HashLink);

        if (NtfsCachedRecordMatches(Candidate, Hash, Name, Length))
            return Candidate;
    }
    return NULL;
}

/* Caller must hold RecordCacheMutex. */
static
VOID
NtfsTrimRecordCache(_In_ PVolumeContextBlock VolCB)
{
    PLIST_ENTRY Entry = VolCB->RecordCacheList.Blink;

    while (VolCB->RecordCacheCount > NTFS_MAX_CACHED_RECORDS &&
           Entry != &VolCB->RecordCacheList)
    {
        PNtfsCachedRecord Candidate = CONTAINING_RECORD(Entry, NtfsCachedRecord, Link);
        PLIST_ENTRY Previous = Entry->Blink;

        if (Candidate->InUse == 0)
        {
            RemoveEntryList(&Candidate->Link);
            RemoveEntryList(&Candidate->HashLink);
            VolCB->RecordCacheCount--;
            NtfsFileRecordDestroy(Candidate->Record);
            ExFreePoolWithTag(Candidate, TAG_NTFS);
        }
        Entry = Previous;
    }
}

PNtfsCachedRecord
NtfsAcquireCachedRecord(_In_ PVolumeContextBlock VolCB,
                        _In_reads_(Length) PCWSTR Name,
                        _In_ USHORT Length)
{
    ULONG Hash;
    PNtfsCachedRecord Found;

    if (!VolCB || !Name || !Length)
        return NULL;

    Hash = NtfsHashName(Name, Length);
    ExAcquireFastMutex(&VolCB->RecordCacheMutex);
    Found = NtfsLookupCachedRecordLocked(VolCB, Hash, Name, Length);
    if (Found)
    {
        Found->InUse++;
        RemoveEntryList(&Found->Link);
        InsertHeadList(&VolCB->RecordCacheList, &Found->Link);
    }
    ExReleaseFastMutex(&VolCB->RecordCacheMutex);
    return Found;
}

PNtfsCachedRecord
NtfsCacheRecord(_In_ PVolumeContextBlock VolCB,
                _In_reads_(Length) PCWSTR Name,
                _In_ USHORT Length,
                _In_ PNtfsFileRecord Record)
{
    ULONG Hash;
    PNtfsCachedRecord New;

    if (!VolCB || !Name || !Length || !Record)
        return NULL;

    /*
     * Directory records go stale: index edits rewrite them through the
     * library's own instances, which this cache cannot see. Serving a stale
     * parse of INDEX_ROOT corrupts every later directory load, so only
     * ordinary file records are kept.
     */
    if (NtfsFileRecordGetHeader(Record)->Flags & FR_IS_DIRECTORY)
        return NULL;

    New = (PNtfsCachedRecord)ExAllocatePoolUninitialized(
        NonPagedPool,
        FIELD_OFFSET(NtfsCachedRecord, Name) + Length * sizeof(WCHAR),
        TAG_NTFS);
    if (!New)
        return NULL;

    Hash = NtfsHashName(Name, Length);
    New->Hash = Hash;
    New->Length = Length;
    New->InUse = 1;
    New->Evicted = FALSE;
    New->Record = Record;
    RtlCopyMemory(New->Name, Name, Length * sizeof(WCHAR));

    ExAcquireFastMutex(&VolCB->RecordCacheMutex);
    /*
     * One entry per name. A second entry for a name already cached would
     * outlive the eviction that a delete or a rename performs on the first,
     * and would go on serving a name that no longer resolves.
     */
    if (NtfsLookupCachedRecordLocked(VolCB, Hash, Name, Length))
    {
        ExReleaseFastMutex(&VolCB->RecordCacheMutex);
        ExFreePoolWithTag(New, TAG_NTFS);
        return NULL;
    }
    InsertHeadList(&VolCB->RecordCacheList, &New->Link);
    InsertHeadList(
        &VolCB->RecordCacheHash[
            Hash & (NTFS_RECORD_CACHE_BUCKETS - 1)],
        &New->HashLink);
    VolCB->RecordCacheCount++;
    NtfsTrimRecordCache(VolCB);
    ExReleaseFastMutex(&VolCB->RecordCacheMutex);
    return New;
}

VOID
NtfsReleaseCachedRecord(_In_ PVolumeContextBlock VolCB,
                        _In_ PNtfsCachedRecord Entry)
{
    BOOLEAN Destroy = FALSE;

    if (!VolCB || !Entry)
        return;

    ExAcquireFastMutex(&VolCB->RecordCacheMutex);
    Entry->InUse--;
    /* An evicted entry is off the list already and only waited on its users. */
    if (Entry->Evicted && Entry->InUse == 0)
        Destroy = TRUE;
    ExReleaseFastMutex(&VolCB->RecordCacheMutex);

    if (Destroy)
    {
        if (Entry->Record)
            NtfsFileRecordDestroy(Entry->Record);
        ExFreePoolWithTag(Entry, TAG_NTFS);
    }
}

VOID
NtfsEvictCachedRecord(_In_ PVolumeContextBlock VolCB,
                      _In_reads_(Length) PCWSTR Name,
                      _In_ USHORT Length,
                      _In_ BOOLEAN RecordAlreadyFreed)
{
    ULONG Hash;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;
    LIST_ENTRY DoomedList;

    if (!VolCB || !Name || !Length)
        return;

    InitializeListHead(&DoomedList);
    Hash = NtfsHashName(Name, Length);
    ExAcquireFastMutex(&VolCB->RecordCacheMutex);
    /*
     * Every entry under this name goes, not just the first one found. The
     * name is about to stop resolving, and one entry left behind would keep
     * answering opens for a file that no longer exists.
     */
    for (Entry = VolCB->RecordCacheHash[
             Hash & (NTFS_RECORD_CACHE_BUCKETS - 1)].Flink;
         Entry != &VolCB->RecordCacheHash[
             Hash & (NTFS_RECORD_CACHE_BUCKETS - 1)];
         Entry = Next)
    {
        PNtfsCachedRecord Candidate =
            CONTAINING_RECORD(Entry, NtfsCachedRecord, HashLink);

        Next = Entry->Flink;
        if (NtfsCachedRecordMatches(Candidate, Hash, Name, Length))
        {
            RemoveEntryList(&Candidate->Link);
            RemoveEntryList(&Candidate->HashLink);
            VolCB->RecordCacheCount--;
            Candidate->Evicted = TRUE;
            /* Deleting the file frees the record inside the library, so the
             * pointer must not be used or destroyed again. */
            if (RecordAlreadyFreed)
                Candidate->Record = NULL;
            if (Candidate->InUse == 0)
                InsertTailList(&DoomedList, &Candidate->Link);
        }
    }
    ExReleaseFastMutex(&VolCB->RecordCacheMutex);

    while (!IsListEmpty(&DoomedList))
    {
        PNtfsCachedRecord Doomed = CONTAINING_RECORD(RemoveHeadList(&DoomedList), NtfsCachedRecord, Link);

        if (Doomed->Record)
            NtfsFileRecordDestroy(Doomed->Record);
        ExFreePoolWithTag(Doomed, TAG_NTFS);
    }
}
