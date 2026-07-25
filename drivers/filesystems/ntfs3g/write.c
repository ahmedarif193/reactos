/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file write handling
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

/*
 * Growing the attribute ahead of an extending write pays for itself once a file
 * is being streamed, but a small file would only be paying the correction that
 * cleanup has to make, so it starts once the file passes this size.
 */
#define NTFS3G_EXTEND_AHEAD_MIN (64 * 1024)
#define NTFS3G_EXTEND_AHEAD_MAX (1024 * 1024)

typedef struct _NTFS_WRITE_WORK_ITEM
{
    PIO_WORKITEM WorkItem;
    PIRP Irp;
} NTFS_WRITE_WORK_ITEM, *PNTFS_WRITE_WORK_ITEM;

static
NTSTATUS
NtfsWriteFile(_In_ PDEVICE_OBJECT DeviceObject,
              _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File = FileObject->FsContext;
    PHandleContextBlock Handle = FileObject->FsContext2;
    PVolumeContextBlock Volume = DeviceObject->DeviceExtension;
    LARGE_INTEGER ByteOffset = IrpSp->Parameters.Write.ByteOffset;
    LARGE_INTEGER EndOffset;
    LARGE_INTEGER LockLength;
    LARGE_INTEGER OldValidDataLength;
    ULONG Length = IrpSp->Parameters.Write.Length;
    BOOLEAN PagingIo = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    BOOLEAN NoCache;
    PERESOURCE Resource;
    PVOID Buffer;
    size_t BytesWritten = 0;
    NTSTATUS Status = STATUS_SUCCESS;
    IO_STATUS_BLOCK CacheStatus;
    int Result;

    if (!File)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (!PagingIo && Volume->ShutdownStarted)
        return NtfsCompleteRequest(Irp, STATUS_SYSTEM_SHUTDOWN, 0);
    if (!PagingIo && (!Handle || Handle->CleanupComplete))
        return NtfsCompleteRequest(Irp, STATUS_FILE_CLOSED, 0);
    if (!PagingIo &&
        !(Handle->DesiredAccess & (FILE_WRITE_DATA | FILE_APPEND_DATA)))
        return NtfsCompleteRequest(Irp, STATUS_ACCESS_DENIED, 0);
    if (File->IsVolume) {
        if (PagingIo ||
            (IrpSp->MinorFunction &
             (IRP_MN_MDL | IRP_MN_COMPLETE)))
            return NtfsCompleteRequest(
                Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
        if (Volume->LockFileObject != FileObject)
            return NtfsCompleteRequest(
                Irp, STATUS_ACCESS_DENIED, 0);
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoGetNextIrpStackLocation(Irp)->FileObject = NULL;
        IoGetNextIrpStackLocation(Irp)->Flags |=
            SL_OVERRIDE_VERIFY_VOLUME;
        return IoCallDriver(Volume->StorageDevice, Irp);
    }
    if (File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY)
        return NtfsCompleteRequest(Irp, STATUS_FILE_IS_A_DIRECTORY, 0);
    if (IrpSp->MinorFunction & (IRP_MN_MDL | IRP_MN_COMPLETE))
        return NtfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    if (ByteOffset.HighPart == -1) {
        if (ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION)
            ByteOffset = FileObject->CurrentByteOffset;
        else if (ByteOffset.LowPart == FILE_WRITE_TO_END_OF_FILE)
            ByteOffset = File->CommonFCBHeader.FileSize;
    }
    if (ByteOffset.QuadPart < 0 ||
        Length > MAXLONGLONG - ByteOffset.QuadPart)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (!PagingIo &&
        !(Handle->DesiredAccess & FILE_WRITE_DATA) &&
        ByteOffset.QuadPart != File->CommonFCBHeader.FileSize.QuadPart)
        return NtfsCompleteRequest(Irp, STATUS_ACCESS_DENIED, 0);
    if (!PagingIo) {
        LockLength.QuadPart = Length;
        if (!FsRtlFastCheckLockForWrite(
                &File->FileLock,
                &ByteOffset,
                &LockLength,
                IrpSp->Parameters.Write.Key,
                FileObject,
                IoGetRequestorProcess(Irp)))
            return NtfsCompleteRequest(
                Irp, STATUS_FILE_LOCK_CONFLICT, 0);
    }

    if (PagingIo) {
        if ((ULONGLONG)ByteOffset.QuadPart >=
            (ULONGLONG)File->CommonFCBHeader.FileSize.QuadPart)
            return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
        if (Length >
            (ULONGLONG)File->CommonFCBHeader.FileSize.QuadPart -
            ByteOffset.QuadPart)
            Length = (ULONG)(File->CommonFCBHeader.FileSize.QuadPart -
                             ByteOffset.QuadPart);
    }
    if (!Length)
        return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

    Buffer = GetBuffer(Irp);
    if (!Buffer)
        return NtfsCompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);

    EndOffset.QuadPart = ByteOffset.QuadPart + Length;
    NoCache = PagingIo ||
              BooleanFlagOn(Irp->Flags, IRP_NOCACHE) ||
              BooleanFlagOn(FileObject->Flags,
                            FO_NO_INTERMEDIATE_BUFFERING);
    if (!NoCache) {
        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
        if (Volume->ShutdownStarted) {
            ExReleaseResourceLite(&File->MainResource);
            KeLeaveCriticalRegion();
            return NtfsCompleteRequest(
                Irp, STATUS_SYSTEM_SHUTDOWN, 0);
        }
        OldValidDataLength = File->CommonFCBHeader.ValidDataLength;
        _SEH2_TRY {
            if (EndOffset.QuadPart >
                File->CommonFCBHeader.FileSize.QuadPart) {
                /*
                 * Growing the attribute is the expensive part of an extending
                 * write, so grow it ahead of the request — doubling up to a
                 * megabyte — and let the writes that fit inside the allocation
                 * just move the cached size. Cleanup records the exact size.
                 */
                if (EndOffset.QuadPart >
                    File->CommonFCBHeader.AllocationSize.QuadPart) {
                    LONGLONG Ahead = EndOffset.QuadPart;

                    if (Ahead < NTFS3G_EXTEND_AHEAD_MIN)
                        Ahead = 0;
                    else if (Ahead > NTFS3G_EXTEND_AHEAD_MAX)
                        Ahead = NTFS3G_EXTEND_AHEAD_MAX;
                    Result = Ntfs3gRosSetFileSize(
                        File->File, (uint64_t)(EndOffset.QuadPart + Ahead));
                    if (Result < 0) {
                        Status = Ntfs3gRosStatusFromError(-Result);
                        _SEH2_LEAVE;
                    }
                    Result = Ntfs3gRosGetFileInformation(
                        File->File, &File->Information);
                    if (Result < 0) {
                        Status = Ntfs3gRosStatusFromError(-Result);
                        _SEH2_LEAVE;
                    }
                    File->CommonFCBHeader.AllocationSize.QuadPart =
                        File->Information.AllocationSize;
                    if (Ahead)
                        File->SizeGrownAhead = TRUE;
                }
                File->CommonFCBHeader.FileSize.QuadPart = EndOffset.QuadPart;
                if (File->SectionObjectPointers.SharedCacheMap)
                    CcSetFileSizes(
                        FileObject,
                        (PCC_FILE_SIZES)&File->CommonFCBHeader.AllocationSize);
            }
            if (!FileObject->PrivateCacheMap) {
                CcInitializeCacheMap(
                    FileObject,
                    (PCC_FILE_SIZES)&File->CommonFCBHeader.AllocationSize,
                    FALSE,
                    &CacheManagerCallbacks,
                    File);
                CcSetReadAheadGranularity(
                    FileObject, NTFS3G_READ_AHEAD_GRANULARITY);
            }
            if (ByteOffset.QuadPart > OldValidDataLength.QuadPart &&
                !CcZeroData(FileObject,
                            &OldValidDataLength,
                            &ByteOffset,
                            TRUE)) {
                Status = STATUS_CANT_WAIT;
                _SEH2_LEAVE;
            }
            if (!CcCopyWrite(FileObject,
                             &ByteOffset,
                             Length,
                             TRUE,
                             Buffer)) {
                Status = STATUS_CANT_WAIT;
                _SEH2_LEAVE;
            }
            BytesWritten = Length;
            if (EndOffset.QuadPart >
                File->CommonFCBHeader.ValidDataLength.QuadPart) {
                File->CommonFCBHeader.ValidDataLength = EndOffset;
                CcSetFileSizes(
                    FileObject,
                    (PCC_FILE_SIZES)&File->CommonFCBHeader.AllocationSize);
            }
            FileObject->Flags |= FO_FILE_MODIFIED;
            if ((IrpSp->Flags & SL_WRITE_THROUGH) ||
                (FileObject->Flags & FO_WRITE_THROUGH)) {
                CcFlushCache(&File->SectionObjectPointers,
                             ByteOffset.QuadPart >
                                 OldValidDataLength.QuadPart ?
                                 NULL : &ByteOffset,
                             ByteOffset.QuadPart >
                                 OldValidDataLength.QuadPart ?
                             0 : Length,
                             &CacheStatus);
                Status = CacheStatus.Status;
                if (NT_SUCCESS(Status)) {
                    Result = Ntfs3gRosFlushFile(File->File);
                    if (Result < 0)
                        Status = Ntfs3gRosStatusFromError(-Result);
                }
                if (NT_SUCCESS(Status)) {
                    Result = Ntfs3gRosFlushVolume(Volume->Volume);
                    if (Result < 0)
                        Status = Ntfs3gRosStatusFromError(-Result);
                }
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            Status = _SEH2_GetExceptionCode();
            BytesWritten = 0;
        }
        _SEH2_END;
        if (NT_SUCCESS(Status) &&
            (FileObject->Flags & FO_SYNCHRONOUS_IO))
            FileObject->CurrentByteOffset = EndOffset;
        ExReleaseResourceLite(&File->MainResource);
        KeLeaveCriticalRegion();
        return NtfsCompleteRequest(Irp, Status, BytesWritten);
    }

    Resource = PagingIo ?
        &File->PagingIoResource : &File->MainResource;
    KeEnterCriticalRegion();
    if (PagingIo)
        ExAcquireResourceSharedLite(Resource, TRUE);
    else
        ExAcquireResourceExclusiveLite(Resource, TRUE);
    if (!PagingIo && Volume->ShutdownStarted) {
        ExReleaseResourceLite(Resource);
        KeLeaveCriticalRegion();
        return NtfsCompleteRequest(
            Irp, STATUS_SYSTEM_SHUTDOWN, 0);
    }

    if (!PagingIo && File->SectionObjectPointers.SharedCacheMap) {
        CcFlushCache(&File->SectionObjectPointers,
                     &ByteOffset,
                     Length,
                     &CacheStatus);
        if (!NT_SUCCESS(CacheStatus.Status) ||
            !CcPurgeCacheSection(&File->SectionObjectPointers,
                                 &ByteOffset,
                                 Length,
                                 FALSE)) {
            Status = NT_SUCCESS(CacheStatus.Status) ?
                STATUS_USER_MAPPED_FILE : CacheStatus.Status;
            goto Complete;
        }
    }

    Result = Ntfs3gRosWriteFileAt(File->File,
                                  (uint64_t)ByteOffset.QuadPart,
                                  Buffer,
                                  Length,
                                  &BytesWritten);
    if (Result < 0) {
        Status = Ntfs3gRosStatusFromError(-Result);
        BytesWritten = 0;
        goto Complete;
    }
    Result = Ntfs3gRosGetFileInformation(File->File, &File->Information);
    if (Result < 0) {
        Status = Ntfs3gRosStatusFromError(-Result);
        goto Complete;
    }

    File->CommonFCBHeader.AllocationSize.QuadPart =
        File->Information.AllocationSize;
    File->CommonFCBHeader.FileSize.QuadPart = File->Information.FileSize;
    if (!PagingIo)
        File->CommonFCBHeader.ValidDataLength.QuadPart =
            File->Information.FileSize;
    if (!PagingIo && File->SectionObjectPointers.SharedCacheMap)
        CcSetFileSizes(
            FileObject,
            (PCC_FILE_SIZES)&File->CommonFCBHeader.AllocationSize);
    if (!PagingIo && (FileObject->Flags & FO_SYNCHRONOUS_IO))
        FileObject->CurrentByteOffset.QuadPart =
            ByteOffset.QuadPart + BytesWritten;
    FileObject->Flags |= FO_FILE_MODIFIED;
    if ((IrpSp->Flags & SL_WRITE_THROUGH) ||
        (FileObject->Flags & FO_WRITE_THROUGH)) {
        Result = Ntfs3gRosFlushFile(File->File);
        if (Result < 0) {
            Status = Ntfs3gRosStatusFromError(-Result);
            goto Complete;
        }
        Result = Ntfs3gRosFlushVolume(Volume->Volume);
        if (Result < 0) {
            Status = Ntfs3gRosStatusFromError(-Result);
            goto Complete;
        }
    }

Complete:
    ExReleaseResourceLite(Resource);
    KeLeaveCriticalRegion();
    return NtfsCompleteRequest(Irp, Status, BytesWritten);
}

static
VOID
NTAPI
NtfsWriteWorker(_In_ PDEVICE_OBJECT DeviceObject,
                _In_opt_ PVOID Context)
{
    PNTFS_WRITE_WORK_ITEM WriteWorkItem = Context;

    NtfsWriteFile(DeviceObject, WriteWorkItem->Irp);
    IoFreeWorkItem(WriteWorkItem->WorkItem);
    ExFreePoolWithTag(WriteWorkItem, TAG_NTFS);
}

NTSTATUS
NTAPI
NtfsFsdWrite(_In_ PDEVICE_OBJECT DeviceObject,
             _Inout_ PIRP Irp)
{
    PNTFS_WRITE_WORK_ITEM WriteWorkItem;

    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        return NtfsWriteFile(DeviceObject, Irp);

    WriteWorkItem = ExAllocatePoolWithTag(NonPagedPool,
                                          sizeof(*WriteWorkItem),
                                          TAG_NTFS);
    if (!WriteWorkItem)
        return NtfsCompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);

    WriteWorkItem->WorkItem = IoAllocateWorkItem(DeviceObject);
    if (!WriteWorkItem->WorkItem) {
        ExFreePoolWithTag(WriteWorkItem, TAG_NTFS);
        return NtfsCompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }

    WriteWorkItem->Irp = Irp;
    IoMarkIrpPending(Irp);
    IoQueueWorkItem(WriteWorkItem->WorkItem,
                    NtfsWriteWorker,
                    DelayedWorkQueue,
                    WriteWorkItem);
    return STATUS_PENDING;
}
