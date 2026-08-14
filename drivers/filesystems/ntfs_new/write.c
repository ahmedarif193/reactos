/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new write APIs
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfspch.h"

/* GLOBALS *****************************************************************/

/* FUNCTIONS ****************************************************************/

_Function_class_(IRP_MJ_WRITE)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdWrite(_In_ PDEVICE_OBJECT VolumeDeviceObject,
             _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Handles write requests.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-write
     */
    NTSTATUS Status;
    PIO_STACK_LOCATION IrpSp;
    PUCHAR Buffer;
    LARGE_INTEGER ByteOffset;
    ULONG Length;
    PFileContextBlock FileCB;
    PFILE_OBJECT FileObj;
    PVolumeContextBlock VolCB;
    PNtfsVolume DiskVolume;
    PNtfsFileRecord FileRec;
    AttributeType RequestedType;
    PWSTR RequestedStream;
    BOOLEAN ResourceAcquired = FALSE;
    BOOLEAN PagingIo = FALSE;
    PVOID BounceBuffer = NULL;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    FileObj = IrpSp->FileObject;
    if (!FileObj ||
        !VolumeDeviceObject->DeviceExtension ||
        !FileObj->FsContext)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    FileCB = NtfsGetFileContext(FileObj);
    VolCB = (PVolumeContextBlock)VolumeDeviceObject->DeviceExtension;
    if (FileCB->IsVolumeOpen)
        return NtfsForwardVolumeIo(VolCB, FileCB, Irp, TRUE);

    FileRec = FileCB->FileRec;
    if (!FileRec)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }

    DiskVolume = VolCB->DiskVolume;
    if (!DiskVolume)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Complete;
    }

    if (NtfsVolumeIsReadOnly(DiskVolume))
    {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto Complete;
    }

    if (!(FileCB->DesiredAccess &
          (FILE_WRITE_DATA | FILE_APPEND_DATA)))
    {
        Status = STATUS_ACCESS_DENIED;
        goto Complete;
    }

    Buffer = (PUCHAR)GetBuffer(Irp);
    Length = IrpSp->Parameters.Write.Length;
    if (Length != 0 && !Buffer)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto Complete;
    }

    ByteOffset = IrpSp->Parameters.Write.ByteOffset;
    RequestedType = FileCB->RequestedType;
    RequestedStream = FileCB->RequestedStream;

    /* Only a handle granted append access *without* plain write access is
     * append-only; FILE_GENERIC_WRITE includes FILE_APPEND_DATA, so testing
     * that bit alone would redirect every ordinary write to end of file. */
    if ((FileCB->DesiredAccess & FILE_APPEND_DATA) &&
        !(FileCB->DesiredAccess & FILE_WRITE_DATA))
    {
        ByteOffset.HighPart = -1;
        ByteOffset.LowPart = FILE_WRITE_TO_END_OF_FILE;
    }
    if (!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        FileCB->StreamCB &&
        !FsRtlCheckLockForWriteAccess(&FileCB->StreamCB->FileLock, Irp))
    {
        Status = STATUS_FILE_LOCK_CONFLICT;
        goto Complete;
    }

    /* Paging I/O is serviced while a caller may already hold MainResource,
     * so it synchronizes on PagingIoResource instead. */
    PagingIo = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    KeEnterCriticalRegion();
    if (PagingIo)
        ExAcquireResourceSharedLite(NtfsGetPagingIoResource(FileCB), TRUE);
    else
        ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
    ResourceAcquired = TRUE;

    ExAcquireResourceExclusiveLite(&VolCB->MetadataResource, TRUE);
    /*
     * A paging write hands us the section's own pages. Passing them straight
     * through means the storage stack probes and locks pages that Mm has
     * already locked for this transfer, so copy through pool memory instead.
     */
    if (PagingIo && Length != 0)
    {
        BounceBuffer = ExAllocatePoolUninitialized(NonPagedPool, Length, TAG_NTFS);
        if (BounceBuffer)
        {
            RtlCopyMemory(BounceBuffer, Buffer, Length);
            Buffer = BounceBuffer;
        }
    }

    Status = NtfsFileRecordWriteFileData(FileRec,
                                         RequestedType,
                                         RequestedStream,
                                         Buffer,
                                         &Length,
                                         &ByteOffset);

    if (BounceBuffer)
    {
        ExFreePoolWithTag(BounceBuffer, TAG_NTFS);
        BounceBuffer = NULL;
    }
    ExReleaseResourceLite(&VolCB->MetadataResource);

    if (NT_SUCCESS(Status))
    {
        NtfsRefreshFileSizes(FileCB,
                             FileObj);
        if (!PagingIo && Length != 0 && RequestedType == TypeData)
            NtfsPurgeStreamCache(FileCB, FileObj, &ByteOffset, Length);
        FileObj->Flags |=
            FO_FILE_MODIFIED |
            FO_FILE_SIZE_CHANGED;

        if (IrpSp->FileObject->Flags & FO_SYNCHRONOUS_IO)
        {
            // Advance file pointer
            IrpSp->FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + Length;
        }

        Irp->IoStatus.Information = Length;
    }
    else
    {
        Irp->IoStatus.Information = 0;
    }

    ExReleaseResourceLite(PagingIo ? NtfsGetPagingIoResource(FileCB)
                                   : NtfsGetMainResource(FileCB));
    KeLeaveCriticalRegion();
    ResourceAcquired = FALSE;

Complete:
    if (ResourceAcquired)
    {
        ExReleaseResourceLite(PagingIo ? NtfsGetPagingIoResource(FileCB)
                                       : NtfsGetMainResource(FileCB));
        KeLeaveCriticalRegion();
    }

    if (!NT_SUCCESS(Status))
        Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}
