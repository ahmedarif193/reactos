/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new write APIs
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfspch.h"

/* GLOBALS *****************************************************************/

#define NTFS_CACHED_GROWTH_LIMIT     (64 * 1024 * 1024)
#define NTFS_RESIDENT_WRITE_LIMIT    512

/* FUNCTIONS ****************************************************************/

static
BOOLEAN
NtfsGrowForCachedWrite(_In_ PVolumeContextBlock VolCB,
                       _In_ PFileContextBlock FileCB,
                       _In_ PFILE_OBJECT FileObj,
                       _In_ LONGLONG EndOffset)
{
    PFSRTL_ADVANCED_FCB_HEADER Header = NtfsGetCommonFcbHeader(FileCB);
    LONGLONG Allocation = Header->AllocationSize.QuadPart;
    PAttribute DataAttribute;
    LONGLONG Target;
    NTSTATUS Status;
    BOOLEAN SizePersisted = FALSE;

    if (EndOffset > Allocation)
    {
        Target = Allocation + ((Allocation < NTFS_CACHED_GROWTH_LIMIT) ? Allocation : NTFS_CACHED_GROWTH_LIMIT);
        if (Target < EndOffset)
            Target = EndOffset;

        NtfsAcquireMetadata(VolCB);
        if (Allocation == 0 && EndOffset <= PAGE_SIZE)
        {
            Status = NtfsFileRecordSetFileDataSize(FileCB->FileRec, FileCB->RequestedType,
                                                   FileCB->RequestedStream, EndOffset);
            SizePersisted = NT_SUCCESS(Status);
        }
        else
        {
            Status = NtfsFileRecordSetFileAllocationSize(FileCB->FileRec, FileCB->RequestedType,
                                                         FileCB->RequestedStream, Target);
        }
        if (NT_SUCCESS(Status))
        {
            DataAttribute = NtfsFileRecordGetAttribute(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream);
            if (DataAttribute && DataAttribute->IsNonResident)
                Allocation = (LONGLONG)NtfsAttributeGetPhysicalAllocationSize(DataAttribute);
            InterlockedIncrement(&VolCB->DirGeneration);
        }
        NtfsReleaseMetadata(VolCB);
        if (Allocation < EndOffset)
            return FALSE;
        if (SizePersisted)
            FileCB->WriteTimesStamped = TRUE;
    }

    ExAcquireResourceExclusiveLite(NtfsGetPagingIoResource(FileCB), TRUE);
    Header->AllocationSize.QuadPart = Allocation;
    if (Header->FileSize.QuadPart < EndOffset)
        Header->FileSize.QuadPart = EndOffset;
    FileCB->StreamCB->SizePending = !SizePersisted;
    ExReleaseResourceLite(NtfsGetPagingIoResource(FileCB));

    if (FileObj->PrivateCacheMap != NULL)
        CcSetFileSizes(FileObj, (PCC_FILE_SIZES)&Header->AllocationSize);
    FileObj->Flags |= FO_FILE_SIZE_CHANGED;
    return TRUE;
}

NTSTATUS
NtfsPersistPendingSize(_In_ PVolumeContextBlock VolCB,
                       _In_ PFileContextBlock FileCB)
{
    PFSRTL_ADVANCED_FCB_HEADER Header;
    PAttribute DataAttribute;
    NTSTATUS Status = STATUS_SUCCESS;
    LONGLONG Allocation = -1;
    LONGLONG FileSize;

    if (!FileCB->StreamCB || !FileCB->StreamCB->SizePending || !FileCB->FileRec)
        return STATUS_SUCCESS;

    Header = NtfsGetCommonFcbHeader(FileCB);
    FileSize = Header->FileSize.QuadPart;

    ExAcquireResourceExclusiveLite(NtfsGetPagingIoResource(FileCB), TRUE);
    NtfsAcquireMetadata(VolCB);
    DataAttribute = NtfsFileRecordGetAttribute(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream);
    if (DataAttribute && DataAttribute->IsNonResident)
    {
        if (DataAttribute->NonResident.DataSize != (ULONGLONG)FileSize)
        {
            Status = NtfsFileRecordSetFileDataSize(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream,
                                                   (ULONGLONG)FileSize);
        }
        else if (NtfsAttributeGetPhysicalAllocationSize(DataAttribute) > (ULONGLONG)FileSize)
        {
            Status = NtfsFileRecordSetFileAllocationSize(FileCB->FileRec, FileCB->RequestedType,
                                                         FileCB->RequestedStream, (ULONGLONG)FileSize);
        }
        DataAttribute = NtfsFileRecordGetAttribute(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream);
        if (NT_SUCCESS(Status) && DataAttribute && DataAttribute->IsNonResident)
            Allocation = (LONGLONG)NtfsAttributeGetPhysicalAllocationSize(DataAttribute);
        if (NT_SUCCESS(Status))
            InterlockedIncrement(&VolCB->DirGeneration);
    }
    else
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
    }
    NtfsReleaseMetadata(VolCB);

    if (NT_SUCCESS(Status))
    {
        if (Allocation >= FileSize)
            Header->AllocationSize.QuadPart = Allocation;
        FileCB->StreamCB->SizePending = FALSE;
    }
    ExReleaseResourceLite(NtfsGetPagingIoResource(FileCB));
    return Status;
}

static
BOOLEAN
NtfsCachedWrite(_In_ PVolumeContextBlock VolCB,
                _In_ PFileContextBlock FileCB,
                _In_ PFILE_OBJECT FileObj,
                _In_ PIRP Irp,
                _In_ PVOID Buffer,
                _In_ ULONG Length,
                _In_ PLARGE_INTEGER ByteOffset,
                _Out_ PNTSTATUS Status)
{
    PFSRTL_ADVANCED_FCB_HEADER Header = NtfsGetCommonFcbHeader(FileCB);
    LONGLONG EndOffset = ByteOffset->QuadPart + Length;
    BOOLEAN Handled = FALSE;
    BOOLEAN Extending;

    if (ByteOffset->QuadPart < 0 || !CcCanIWrite(FileObj, Length, TRUE, FALSE))
        return FALSE;

    if (Length >= 1024 * 1024 && EndOffset > Header->ValidDataLength.QuadPart &&
        FileObj->SectionObjectPointer->SharedCacheMap == NULL)
    {
        return FALSE;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(NtfsGetMainResource(FileCB), TRUE);
    Extending = EndOffset > Header->ValidDataLength.QuadPart;
    if (Extending)
    {
        ExReleaseResourceLite(NtfsGetMainResource(FileCB));
        ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
        if (!FileCB->StreamCB ||
            FileCB->StreamCB->Deleted ||
            EndOffset <= NTFS_RESIDENT_WRITE_LIMIT ||
            ByteOffset->QuadPart > Header->ValidDataLength.QuadPart ||
            !NtfsGrowForCachedWrite(VolCB, FileCB, FileObj, EndOffset))
        {
            ExReleaseResourceLite(NtfsGetMainResource(FileCB));
            KeLeaveCriticalRegion();
            return FALSE;
        }
    }

    if (EndOffset <= Header->ValidDataLength.QuadPart || Extending)
    {
        if (FileObj->PrivateCacheMap == NULL)
            NtfsInitializeStreamCache(FileCB, FileObj);

        Handled = TRUE;
        _SEH2_TRY
        {
            *Status = CcCopyWrite(FileObj, ByteOffset, Length, TRUE, Buffer) ? STATUS_SUCCESS
                                                                              : STATUS_CANT_WAIT;
        }
        _SEH2_EXCEPT(FsRtlIsNtstatusExpected(_SEH2_GetExceptionCode()) ?
                     EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            *Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (NT_SUCCESS(*Status))
        {
            if (EndOffset > Header->ValidDataLength.QuadPart)
                Header->ValidDataLength.QuadPart = EndOffset;
            FileObj->Flags |= FO_FILE_MODIFIED;
            if (FileObj->Flags & FO_SYNCHRONOUS_IO)
                FileObj->CurrentByteOffset.QuadPart = ByteOffset->QuadPart + Length;
            Irp->IoStatus.Information = Length;
        }
    }

    ExReleaseResourceLite(NtfsGetMainResource(FileCB));

    if (Handled && NT_SUCCESS(*Status) && !FileCB->WriteTimesStamped)
    {
        FileCB->WriteTimesStamped = TRUE;
        ExAcquireResourceExclusiveLite(NtfsGetMainResource(FileCB), TRUE);
        NtfsAcquireMetadata(VolCB);
        NtfsFileRecordUpdateAutomaticTimestamps(FileCB->FileRec,
                                                NTFS_BASIC_INFO_LAST_WRITE_TIME |
                                                NTFS_BASIC_INFO_CHANGE_TIME);
        NtfsReleaseMetadata(VolCB);
        ExReleaseResourceLite(NtfsGetMainResource(FileCB));
        InterlockedIncrement(&VolCB->DirGeneration);
    }

    KeLeaveCriticalRegion();
    return Handled;
}

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
    PMDL LockMdl = NULL;
    BOOLEAN LockFailed = FALSE;

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

    if (!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        !(FileCB->DesiredAccess &
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

    if (!BooleanFlagOn(Irp->Flags, IRP_PAGING_IO | IRP_NOCACHE) &&
        !BooleanFlagOn(FileObj->Flags, FO_NO_INTERMEDIATE_BUFFERING | FO_WRITE_THROUGH) &&
        Length != 0 &&
        RequestedType == TypeData &&
        FileObj->SectionObjectPointer != NULL &&
        NtfsCachedWrite(VolCB, FileCB, FileObj, Irp, Buffer, Length, &ByteOffset, &Status))
    {
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

    if (PagingIo)
    {
        LONGLONG FileSize = NtfsGetCommonFcbHeader(FileCB)->FileSize.QuadPart;

        if (FileCB->StreamCB && FileCB->StreamCB->Deleted)
        {
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = Length;
            goto Complete;
        }
        if (ByteOffset.QuadPart >= FileSize)
            Length = 0;
        else if (ByteOffset.QuadPart + Length > FileSize)
            Length = (ULONG)(FileSize - ByteOffset.QuadPart);
    }
    else if (Length != 0 &&
             FileObj->SectionObjectPointer != NULL &&
             FileObj->SectionObjectPointer->SharedCacheMap != NULL)
    {
        IO_STATUS_BLOCK FlushStatus;

        CcFlushCache(FileObj->SectionObjectPointer, NULL, 0, &FlushStatus);
        if (!NT_SUCCESS(FlushStatus.Status))
        {
            Status = FlushStatus.Status;
            goto Complete;
        }
    }

    NtfsAcquireMetadata(VolCB);
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

    if (!PagingIo && Length != 0 && Buffer != NULL &&
        Irp->RequestorMode == UserMode &&
        !Irp->MdlAddress && !Irp->AssociatedIrp.SystemBuffer)
    {
        LockMdl = IoAllocateMdl(Buffer, Length, FALSE, FALSE, NULL);
        if (LockMdl)
        {
            _SEH2_TRY
            {
                MmProbeAndLockPages(LockMdl, UserMode, IoReadAccess);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                IoFreeMdl(LockMdl);
                LockMdl = NULL;
                LockFailed = TRUE;
            }
            _SEH2_END;
            if (LockMdl)
            {
                PVOID SystemBuffer = MmGetSystemAddressForMdlSafe(LockMdl, NormalPagePriority);
                if (SystemBuffer)
                {
                    Buffer = SystemBuffer;
                }
                else
                {
                    MmUnlockPages(LockMdl);
                    IoFreeMdl(LockMdl);
                    LockMdl = NULL;
                    LockFailed = TRUE;
                }
            }
        }
        else
        {
            LockFailed = TRUE;
        }
    }

    if (LockFailed)
    {
        Status = STATUS_INVALID_USER_BUFFER;
    }
    else
    {
        Status = NtfsFileRecordWriteFileData(FileRec,
                                             RequestedType,
                                             RequestedStream,
                                             Buffer,
                                             &Length,
                                             &ByteOffset);

        /* The library updates duplicated $FILE_NAME information through a
         * separate parent record. Invalidate parsed directory snapshots
         * before releasing MetadataResource or a later open can reuse the
         * pre-write sizes from CachedLookupParent. */
        if (NT_SUCCESS(Status) && Length != 0 && RequestedType == TypeData && !RequestedStream)
            InterlockedIncrement(&VolCB->DirGeneration);
    }

    if (LockMdl)
    {
        MmUnlockPages(LockMdl);
        IoFreeMdl(LockMdl);
        LockMdl = NULL;
    }
    if (BounceBuffer)
    {
        ExFreePoolWithTag(BounceBuffer, TAG_NTFS);
        BounceBuffer = NULL;
    }
    NtfsReleaseMetadata(VolCB);

    if (NT_SUCCESS(Status))
    {
        if (!PagingIo)
            NtfsRefreshFileSizes(FileCB, FileObj);
        if (!PagingIo && Length != 0 && RequestedType == TypeData)
            NtfsPurgeStreamCache(FileCB, FileObj, &ByteOffset, Length);
        FileObj->Flags |=
            FO_FILE_MODIFIED |
            FO_FILE_SIZE_CHANGED;

        /* A cache flush must not change the application's file pointer. */
        if (!PagingIo && (IrpSp->FileObject->Flags & FO_SYNCHRONOUS_IO))
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
