/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new entry point
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfspch.h"

/* GLOBALS *****************************************************************/

/*
 * Serving reads from the cache requires every file object for a stream to
 * share one SECTION_OBJECT_POINTERS. Each open currently builds its own
 * context block, so a second open of a file that still has a data section
 * aliases it, and Mm faults on the mismatched page state. Until context
 * blocks are shared per stream, reads go straight to the volume.
 */
const BOOLEAN NtfsCachedReadsEnabled = TRUE;

/* The cache manager retains this table for the lifetime of every cache map. */
static CACHE_MANAGER_CALLBACKS NtfsCacheManagerCallbacks =
{
    NtfsAcqLazyWrite,
    NtfsRelLazyWrite,
    NtfsAcqReadAhead,
    NtfsRelReadAhead
};

/*
 * NTFS proper refreshes a file's last-access time at most once per hour; a
 * driver that rewrites the record on every read pays an exclusive lock and a
 * metadata update per I/O that Windows does not.
 */
BOOLEAN
NtfsShouldStampLastAccess(_In_ PFileContextBlock FileCB)
{
    NtfsFileBasicInformation Basic;
    LARGE_INTEGER Now;

    if (!NT_SUCCESS(NtfsFileRecordGetBasicInformation(FileCB->FileRec, &Basic)))
        return TRUE;

    KeQuerySystemTime(&Now);
    return Now.QuadPart - (LONGLONG)Basic.LastAccessTime >= 36000000000LL;
}

typedef struct _NTFS_SYNC_READ_CONTEXT
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
} NTFS_SYNC_READ_CONTEXT, *PNTFS_SYNC_READ_CONTEXT;

static
NTSTATUS
NTAPI
NtfsDirectReadCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PNTFS_SYNC_READ_CONTEXT ReadContext = (PNTFS_SYNC_READ_CONTEXT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    ReadContext->IoStatus = Irp->IoStatus;
    KeSetEvent(&ReadContext->Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NtfsSubmitDirectRead(
    _In_ PVolumeContextBlock VolCB,
    _Inout_ PIRP Irp,
    _In_ ULONGLONG DiskOffset,
    _In_ ULONG Length)
{
    NTFS_SYNC_READ_CONTEXT ReadContext;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    KeInitializeEvent(&ReadContext.Event, NotificationEvent, FALSE);
    ReadContext.IoStatus.Status = STATUS_PENDING;
    ReadContext.IoStatus.Information = 0;

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_READ;
    IoStack->MinorFunction = 0;
    IoStack->Flags = SL_OVERRIDE_VERIFY_VOLUME;
    IoStack->Control = 0;
    IoStack->FileObject = NULL;
    IoStack->Parameters.Read.Length = Length;
    IoStack->Parameters.Read.ByteOffset.QuadPart = DiskOffset;
    IoSetCompletionRoutine(Irp,
                           NtfsDirectReadCompletion,
                           &ReadContext,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(VolCB->StorageDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&ReadContext.Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    NT_ASSERT(ReadContext.IoStatus.Status != STATUS_PENDING);
    Irp->IoStatus = ReadContext.IoStatus;
    return ReadContext.IoStatus.Status;
}

static
NTSTATUS
NtfsLockReadBuffer(
    _Inout_ PIRP Irp,
    _In_ ULONG Length)
{
    PMDL Mdl;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Irp->MdlAddress)
        return STATUS_SUCCESS;
    if (!Irp->UserBuffer)
        return STATUS_INVALID_USER_BUFFER;

    Mdl = IoAllocateMdl(Irp->UserBuffer, Length, FALSE, FALSE, Irp);
    if (!Mdl)
        return STATUS_INSUFFICIENT_RESOURCES;

    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, Irp->RequestorMode, IoWriteAccess);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        Irp->MdlAddress = NULL;
        if (!FsRtlIsNtstatusExpected(Status))
            Status = STATUS_INVALID_USER_BUFFER;
    }
    return Status;
}

static
NTSTATUS
NtfsTryDirectRead(
    _In_ PVolumeContextBlock VolCB,
    _In_ PFileContextBlock FileCB,
    _Inout_ PIRP Irp,
    _In_ ULONGLONG FileOffset,
    _In_ ULONG Length,
    _Out_ PBOOLEAN Handled)
{
    PAttribute Attribute;
    NtfsRetrievalExtent Extent;
    ULONGLONG ReturnedStartingVcn;
    ULONGLONG StartingVcn;
    ULONGLONG ClusterSize;
    ULONGLONG InClusterOffset;
    ULONGLONG Available;
    ULONGLONG PhysicalCluster;
    ULONGLONG DiskOffset;
    ULONG ExtentCount;
    NTSTATUS Status;

    *Handled = FALSE;
    if (!Irp->MdlAddress &&
        !Irp->AssociatedIrp.SystemBuffer)
    {
        Status = NtfsLockReadBuffer(Irp, Length);
        if (!NT_SUCCESS(Status))
        {
            *Handled = TRUE;
            return Status;
        }
    }
    if (!VolCB->StorageDevice ||
        !VolCB->BytesPerSector ||
        !Irp->MdlAddress ||
        Irp->MdlAddress->Next ||
        MmGetMdlByteCount(Irp->MdlAddress) < Length ||
        ((ULONG_PTR)MmGetMdlVirtualAddress(Irp->MdlAddress) &
         VolCB->StorageDevice->AlignmentRequirement) != 0 ||
        Length % VolCB->BytesPerSector != 0 ||
        FileOffset % VolCB->BytesPerSector != 0)
    {
        return STATUS_SUCCESS;
    }

    Attribute = NtfsFileRecordGetAttribute(FileCB->FileRec,
                                           FileCB->RequestedType,
                                           FileCB->RequestedStream);
    if (!Attribute ||
        !Attribute->IsNonResident ||
        (Attribute->Flags & (ATTR_COMPRESSION_MASK | ATTR_ENCRYPTED)) ||
        FileOffset > ~(ULONGLONG)0 - Length ||
        FileOffset + Length > Attribute->NonResident.InitalizedDataSize)
    {
        return STATUS_SUCCESS;
    }

    ClusterSize = (ULONGLONG)VolCB->BytesPerSector *
                  NtfsVolumeGetSectorsPerCluster(VolCB->DiskVolume);
    if (!ClusterSize)
        return STATUS_SUCCESS;

    StartingVcn = FileOffset / ClusterSize;
    InClusterOffset = FileOffset % ClusterSize;
    ExtentCount = 1;
    Status = NtfsFileRecordQueryRetrievalPointers(FileCB->FileRec,
                                                  FileCB->RequestedType,
                                                  FileCB->RequestedStream,
                                                  StartingVcn,
                                                  &ReturnedStartingVcn,
                                                  &Extent,
                                                  &ExtentCount);
    if ((!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW) ||
        ExtentCount != 1 ||
        ReturnedStartingVcn > StartingVcn ||
        Extent.NextVcn <= StartingVcn ||
        Extent.Lcn < 0)
    {
        return STATUS_SUCCESS;
    }

    Available = Extent.NextVcn - StartingVcn;
    if (Available > ~(ULONGLONG)0 / ClusterSize)
        return STATUS_SUCCESS;
    Available *= ClusterSize;
    if (Available < InClusterOffset ||
        Available - InClusterOffset < Length)
    {
        return STATUS_SUCCESS;
    }

    PhysicalCluster = StartingVcn - ReturnedStartingVcn;
    if ((ULONGLONG)Extent.Lcn >
        ~(ULONGLONG)0 - PhysicalCluster)
    {
        return STATUS_SUCCESS;
    }
    PhysicalCluster += (ULONGLONG)Extent.Lcn;
    if (PhysicalCluster > ~(ULONGLONG)0 / ClusterSize)
        return STATUS_SUCCESS;
    DiskOffset = PhysicalCluster * ClusterSize;
    if (DiskOffset > ~(ULONGLONG)0 - InClusterOffset)
        return STATUS_SUCCESS;
    DiskOffset += InClusterOffset;

    *Handled = TRUE;
    return NtfsSubmitDirectRead(VolCB, Irp, DiskOffset, Length);
}

/* FUNCTIONS ****************************************************************/
_Function_class_(IRP_MJ_READ)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdRead(_In_ PDEVICE_OBJECT VolumeDeviceObject,
            _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Handles read requests.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-read
     */
    PIO_STACK_LOCATION IrpSp;
    PFILE_OBJECT FileObject;
    PVolumeContextBlock VolCB;
    PNtfsVolume DiskVolume;
    NTSTATUS Status;
    NTSTATUS TimestampStatus;
    PUCHAR Buffer;
    LARGE_INTEGER ReadOffset;
    ULONG OriginalLength;
    ULONG RequestedLength;
    ULONG BytesRead;
    PFileContextBlock FileCB;
    BOOLEAN ResourceAcquired = FALSE;
    BOOLEAN PagingIo = FALSE;
    BOOLEAN DirectRead;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IrpSp->FileObject;
    VolCB = VolumeDeviceObject &&
            VolumeDeviceObject->DeviceExtension
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    DiskVolume = VolCB ? VolCB->DiskVolume : NULL;
    Buffer = NULL;
    ReadOffset = IrpSp->Parameters.Read.ByteOffset;
    RequestedLength = IrpSp->Parameters.Read.Length;
    OriginalLength = RequestedLength;
    FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;

    if (!FileCB || !FileCB->FileRec || !DiskVolume)
    {
        DPRINT1("NtfsFsdRead(): invalid file or volume context!\n");
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    if (ReadOffset.QuadPart < 0)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    if (!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        FileCB->StreamCB &&
        !FsRtlCheckLockForReadAccess(&FileCB->StreamCB->FileLock, Irp))
    {
        Status = STATUS_FILE_LOCK_CONFLICT;
        goto Complete;
    }

    /* Paging I/O is serviced while a caller may already hold MainResource,
     * so it synchronizes on PagingIoResource instead. */
    PagingIo = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(
        PagingIo ? &FileCB->PagingIoResource : &FileCB->MainResource,
        TRUE);
    ResourceAcquired = TRUE;

    if (IrpSp->MinorFunction == IRP_MN_COMPLETE)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Complete;
    }

    if (RequestedLength &&
        FileCB->RequestedType == TypeData &&
        (PagingIo ||
         BooleanFlagOn(Irp->Flags, IRP_NOCACHE) ||
         BooleanFlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING)))
    {
        Status = NtfsTryDirectRead(VolCB,
                                   FileCB,
                                   Irp,
                                   (ULONGLONG)ReadOffset.QuadPart,
                                   RequestedLength,
                                   &DirectRead);
        if (DirectRead)
        {
            if (NT_SUCCESS(Status))
            {
                if (Irp->IoStatus.Information <= OriginalLength)
                {
                    RequestedLength = OriginalLength -
                                      (ULONG)Irp->IoStatus.Information;
                }
                else
                {
                    Status = STATUS_DATA_ERROR;
                }
            }
            goto ReadDone;
        }
    }

    Buffer = (PUCHAR)GetBuffer(Irp);
    if (RequestedLength != 0 && !Buffer)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto Complete;
    }

    /* Open-only and one-page streams should not pay for cache-map setup. */
    if (NtfsCachedReadsEnabled &&
        RequestedLength &&
        !PagingIo &&
        !BooleanFlagOn(Irp->Flags, IRP_NOCACHE) &&
        !BooleanFlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING) &&
        FileCB->RequestedType == TypeData &&
        FileCB->CommonFCBHeader.FileSize.QuadPart > PAGE_SIZE &&
        FileObject->PrivateCacheMap == NULL)
    {
        PCC_FILE_SIZES FileSizes =
            (PCC_FILE_SIZES)&FileCB->CommonFCBHeader.AllocationSize;

        CcInitializeCacheMap(FileObject,
                             FileSizes,
                             FALSE,
                             &NtfsCacheManagerCallbacks,
                             FileCB);
        CcSetFileSizes(FileObject, FileSizes);
        CcSetReadAheadGranularity(FileObject, 0x40000);
        FileObject->Flags |= FO_CACHE_SUPPORTED;
    }

    /*
     * Ordinary reads go through the cache manager so repeated and sequential
     * access is served from memory. Paging and no-buffering requests must
     * reach the volume directly.
     */
    if (NtfsCachedReadsEnabled &&
        RequestedLength &&
        !BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        !BooleanFlagOn(Irp->Flags, IRP_NOCACHE) &&
        !BooleanFlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING) &&
        FileObject->PrivateCacheMap != NULL &&
        FileCB->RequestedType == TypeData)
    {
        if (ReadOffset.QuadPart >= FileCB->CommonFCBHeader.FileSize.QuadPart)
        {
            RequestedLength = 0;
            Status = STATUS_END_OF_FILE;
        }
        else
        {
            if (ReadOffset.QuadPart + RequestedLength >
                FileCB->CommonFCBHeader.FileSize.QuadPart)
            {
                RequestedLength =
                    (ULONG)(FileCB->CommonFCBHeader.FileSize.QuadPart -
                            ReadOffset.QuadPart);
                OriginalLength = RequestedLength;
            }
            if (!CcCopyRead(FileObject, &ReadOffset, RequestedLength,
                            TRUE, Buffer, &Irp->IoStatus))
            {
                Status = STATUS_CANT_WAIT;
            }
            else
            {
                Status = Irp->IoStatus.Status;
                if (NT_SUCCESS(Status))
                {
                    RequestedLength = OriginalLength -
                                      (ULONG)Irp->IoStatus.Information;
                }
            }
        }
    }
    else if (RequestedLength)
    {
        /*
         * A paging read targets the section's own pages, which Mm has already
         * locked for this transfer. Filling them by way of the storage stack
         * would probe and lock them a second time, so land the data in pool
         * memory and copy it across.
         */
        PVOID Bounce = PagingIo
            ? ExAllocatePoolUninitialized(NonPagedPool, RequestedLength, TAG_NTFS)
            : NULL;

        // Copy data from $DATA into file buffer.
        Status = NtfsFileRecordCopyData(FileCB->FileRec,
                                        FileCB->RequestedType,
                                        FileCB->RequestedStream,
                                        Bounce ? (PUCHAR)Bounce : Buffer,
                                        &RequestedLength,
                                        ReadOffset.QuadPart);
        if (Bounce)
        {
            if (NT_SUCCESS(Status) && OriginalLength > RequestedLength)
                RtlCopyMemory(Buffer, Bounce, OriginalLength - RequestedLength);
            ExFreePoolWithTag(Bounce, TAG_NTFS);
        }
    }

    else
    {
        // If we aren't reading anything, don't read anything.
        Status = STATUS_SUCCESS;
    }

ReadDone:
    ExReleaseResourceLite(PagingIo ? &FileCB->PagingIoResource
                                   : &FileCB->MainResource);
    KeLeaveCriticalRegion();
    ResourceAcquired = FALSE;

    if (NT_SUCCESS(Status))
    {
        BytesRead = OriginalLength - RequestedLength;
        if (BytesRead != 0 &&
            !PagingIo &&
            FileCB->RequestedType == TypeData &&
            !NtfsVolumeIsReadOnly(DiskVolume) &&
            FileCB->LastAccessStampPending)
        {
            FileCB->LastAccessStampPending = FALSE;
            /*
             * Last-access is metadata, so serialize its MFT update after the
             * shared data read. A timestamp failure must not discard bytes
             * already delivered successfully to the caller.
             *
             * Paging reads are excluded: they are the cache manager faulting
             * pages in on behalf of a request that already stamped the file,
             * and they run on a thread that still holds MainResource shared,
             * which no exclusive acquire here could ever be granted over.
             */
            KeEnterCriticalRegion();
            ExAcquireResourceExclusiveLite(
                &FileCB->MainResource,
                TRUE);
            ExAcquireResourceExclusiveLite(
                &VolCB->MetadataResource,
                TRUE);
            TimestampStatus =
                NtfsFileRecordUpdateAutomaticTimestamps(
                    FileCB->FileRec,
                    NTFS_BASIC_INFO_LAST_ACCESS_TIME);
            ExReleaseResourceLite(
                &VolCB->MetadataResource);
            ExReleaseResourceLite(
                &FileCB->MainResource);
            KeLeaveCriticalRegion();
            if (!NT_SUCCESS(TimestampStatus))
            {
                DPRINT1(
                    "NtfsFsdRead(): failed to update last-access time: 0x%08lx\n",
                    TimestampStatus);
            }
        }

        if (FileObject->Flags & FO_SYNCHRONOUS_IO)
        {
            FileObject->CurrentByteOffset.QuadPart =
                ReadOffset.QuadPart + BytesRead;
        }

        Irp->IoStatus.Information = BytesRead;
    }

    else
    {
        Irp->IoStatus.Information = 0;
    }

Complete:
    if (ResourceAcquired)
    {
        ExReleaseResourceLite(PagingIo ? &FileCB->PagingIoResource
                                       : &FileCB->MainResource);
        KeLeaveCriticalRegion();
    }
    if (!NT_SUCCESS(Status))
        Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}
