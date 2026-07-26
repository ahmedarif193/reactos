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

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IrpSp->FileObject;
    VolCB = VolumeDeviceObject &&
            VolumeDeviceObject->DeviceExtension
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    DiskVolume = VolCB ? VolCB->DiskVolume : NULL;
    Buffer = (PUCHAR)(GetBuffer(Irp));
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
    if (RequestedLength != 0 && !Buffer)
    {
        Status = STATUS_INVALID_USER_BUFFER;
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
