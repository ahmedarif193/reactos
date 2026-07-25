/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file read handling
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

typedef struct _NTFS_READ_WORK_ITEM
{
    PIO_WORKITEM WorkItem;
    PIRP Irp;
} NTFS_READ_WORK_ITEM, *PNTFS_READ_WORK_ITEM;

static
NTSTATUS
NtfsReadFile(_In_ PDEVICE_OBJECT DeviceObject,
             _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File = FileObject->FsContext;
    PHandleContextBlock Handle = FileObject->FsContext2;
    LARGE_INTEGER ByteOffset = IrpSp->Parameters.Read.ByteOffset;
    LARGE_INTEGER LockLength;
    ULONG Length = IrpSp->Parameters.Read.Length;
    BOOLEAN PagingIo = BooleanFlagOn(Irp->Flags, IRP_PAGING_IO);
    BOOLEAN NoCache;
    PERESOURCE Resource;
    PVOID Buffer;
    size_t BytesRead = 0;
    NTSTATUS Status;
    int Result;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (!File)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (!PagingIo && (!Handle || Handle->CleanupComplete))
        return NtfsCompleteRequest(Irp, STATUS_FILE_CLOSED, 0);
    if (!PagingIo &&
        !(Handle->DesiredAccess & (FILE_READ_DATA | FILE_EXECUTE)))
        return NtfsCompleteRequest(Irp, STATUS_ACCESS_DENIED, 0);
    if (File->IsVolume) {
        if (PagingIo ||
            (IrpSp->MinorFunction &
             (IRP_MN_MDL | IRP_MN_COMPLETE)))
            return NtfsCompleteRequest(
                Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoGetNextIrpStackLocation(Irp)->FileObject = NULL;
        IoGetNextIrpStackLocation(Irp)->Flags |=
            SL_OVERRIDE_VERIFY_VOLUME;
        return IoCallDriver(
            File->Volume->StorageDevice, Irp);
    }
    if (File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY)
        return NtfsCompleteRequest(Irp, STATUS_FILE_IS_A_DIRECTORY, 0);
    if (IrpSp->MinorFunction & (IRP_MN_MDL | IRP_MN_COMPLETE))
        return NtfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (ByteOffset.HighPart == -1 &&
        ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION)
        ByteOffset = FileObject->CurrentByteOffset;
    if (ByteOffset.QuadPart < 0)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if ((ULONGLONG)ByteOffset.QuadPart >=
        (ULONGLONG)File->CommonFCBHeader.FileSize.QuadPart)
        return NtfsCompleteRequest(Irp,
                                   PagingIo ? STATUS_SUCCESS :
                                              STATUS_END_OF_FILE,
                                   0);
    if (Length >
        (ULONGLONG)File->CommonFCBHeader.FileSize.QuadPart -
        ByteOffset.QuadPart)
        Length = (ULONG)(File->CommonFCBHeader.FileSize.QuadPart -
                         ByteOffset.QuadPart);

    Buffer = GetBuffer(Irp);
    if (Length && !Buffer)
        return NtfsCompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    if (!Length)
        return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);

    if (!PagingIo) {
        LockLength.QuadPart = Length;
        if (!FsRtlFastCheckLockForRead(
                &File->FileLock,
                &ByteOffset,
                &LockLength,
                IrpSp->Parameters.Read.Key,
                FileObject,
                IoGetRequestorProcess(Irp)))
            return NtfsCompleteRequest(
                Irp, STATUS_FILE_LOCK_CONFLICT, 0);
    }

    NoCache = PagingIo ||
              BooleanFlagOn(Irp->Flags, IRP_NOCACHE) ||
              BooleanFlagOn(FileObject->Flags,
                            FO_NO_INTERMEDIATE_BUFFERING);
    if (!NoCache) {
        IO_STATUS_BLOCK CacheStatus;

        KeEnterCriticalRegion();
        ExAcquireResourceSharedLite(&File->MainResource, TRUE);
        CacheStatus.Status = STATUS_SUCCESS;
        CacheStatus.Information = 0;
        _SEH2_TRY {
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
            if (!CcCopyRead(FileObject,
                            &ByteOffset,
                            Length,
                            TRUE,
                            Buffer,
                            &CacheStatus)) {
                Status = STATUS_CANT_WAIT;
            } else {
                Status = CacheStatus.Status;
                BytesRead = CacheStatus.Information;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            Status = _SEH2_GetExceptionCode();
            BytesRead = 0;
        }
        _SEH2_END;
        if (!PagingIo &&
            NT_SUCCESS(Status) &&
            (FileObject->Flags & FO_SYNCHRONOUS_IO))
            FileObject->CurrentByteOffset.QuadPart =
                ByteOffset.QuadPart + BytesRead;
        ExReleaseResourceLite(&File->MainResource);
        KeLeaveCriticalRegion();
        return NtfsCompleteRequest(Irp, Status, BytesRead);
    }

    Resource = PagingIo ?
        &File->PagingIoResource : &File->MainResource;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(Resource, TRUE);
    if (!PagingIo && File->SectionObjectPointers.SharedCacheMap) {
        IO_STATUS_BLOCK CacheStatus;

        CcFlushCache(&File->SectionObjectPointers,
                     &ByteOffset,
                     Length,
                     &CacheStatus);
        if (!NT_SUCCESS(CacheStatus.Status)) {
            ExReleaseResourceLite(Resource);
            KeLeaveCriticalRegion();
            return NtfsCompleteRequest(Irp, CacheStatus.Status, 0);
        }
    }
    Result = Ntfs3gRosReadFileAt(File->File,
                                 (uint64_t)ByteOffset.QuadPart,
                                 Buffer,
                                 Length,
                                 &BytesRead);
    if (Result < 0) {
        Status = Ntfs3gRosStatusFromError(-Result);
        BytesRead = 0;
    } else {
        Status = STATUS_SUCCESS;
    }
    if (!PagingIo &&
        NT_SUCCESS(Status) &&
        (FileObject->Flags & FO_SYNCHRONOUS_IO))
        FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + BytesRead;
    ExReleaseResourceLite(Resource);
    KeLeaveCriticalRegion();
    return NtfsCompleteRequest(Irp, Status, BytesRead);
}

static
VOID
NTAPI
NtfsReadWorker(_In_ PDEVICE_OBJECT DeviceObject,
               _In_opt_ PVOID Context)
{
    PNTFS_READ_WORK_ITEM ReadWorkItem = Context;

    NtfsReadFile(DeviceObject, ReadWorkItem->Irp);
    IoFreeWorkItem(ReadWorkItem->WorkItem);
    ExFreePoolWithTag(ReadWorkItem, TAG_NTFS);
}

NTSTATUS
NTAPI
NtfsFsdRead(_In_ PDEVICE_OBJECT DeviceObject,
            _Inout_ PIRP Irp)
{
    PNTFS_READ_WORK_ITEM ReadWorkItem;

    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        return NtfsReadFile(DeviceObject, Irp);

    ReadWorkItem = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(*ReadWorkItem),
                                         TAG_NTFS);
    if (!ReadWorkItem)
        return NtfsCompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);

    ReadWorkItem->WorkItem = IoAllocateWorkItem(DeviceObject);
    if (!ReadWorkItem->WorkItem) {
        ExFreePoolWithTag(ReadWorkItem, TAG_NTFS);
        return NtfsCompleteRequest(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
    }

    ReadWorkItem->Irp = Irp;
    IoMarkIrpPending(Irp);
    IoQueueWorkItem(ReadWorkItem->WorkItem,
                    NtfsReadWorker,
                    DelayedWorkQueue,
                    ReadWorkItem);
    return STATUS_PENDING;
}
