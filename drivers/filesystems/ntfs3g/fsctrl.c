/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file-system controls
 * COPYRIGHT:   Copyright 2024 Carl J. Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

static NTSTATUS
NtfsVerifyVolume(_In_ PDEVICE_OBJECT DeviceObject)
{
    PVolumeContextBlock Volume;
    uint64_t SerialNumber;
    NTSTATUS Status;
    int Result;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject)
        return STATUS_INVALID_DEVICE_REQUEST;
    Volume = DeviceObject->DeviceExtension;
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(
        &Volume->FcbListResource, TRUE);
    if (Volume->Dismounted || !Volume->Volume) {
        Status = STATUS_VOLUME_DISMOUNTED;
    } else {
        Result = Ntfs3gRosReadVolumeSerialNumber(
            Volume->Volume, &SerialNumber);
        if (Result < 0) {
            Status = Ntfs3gRosStatusFromError(-Result);
        } else if ((ULONG)SerialNumber !=
                       DeviceObject->Vpb->SerialNumber ||
                   SerialNumber !=
                       Ntfs3gRosGetVolumeSerialNumber(
                           Volume->Volume)) {
            Status = STATUS_WRONG_VOLUME;
        } else {
            Volume->StorageDevice->Flags &=
                ~DO_VERIFY_VOLUME;
            Status = STATUS_SUCCESS;
        }
    }
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();
    return Status;
}

static NTSTATUS
NtfsPurgeFcbForVolumeLock(_Inout_ PFileContextBlock File)
{
    IO_STATUS_BLOCK CacheStatus;

    if (!MmFlushImageSection(&File->SectionObjectPointers,
                             MmFlushForDelete))
        return STATUS_SHARING_VIOLATION;

    CacheStatus.Status = STATUS_SUCCESS;
    CacheStatus.Information = 0;
    CcFlushCache(&File->SectionObjectPointers,
                 NULL,
                 0,
                 &CacheStatus);
    if (!NT_SUCCESS(CacheStatus.Status))
        return CacheStatus.Status;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &File->PagingIoResource, TRUE);
    ExReleaseResourceLite(&File->PagingIoResource);
    KeLeaveCriticalRegion();
    if (!CcPurgeCacheSection(&File->SectionObjectPointers,
                             NULL,
                             0,
                             FALSE))
        return STATUS_SHARING_VIOLATION;
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfsUserFsRequest(_In_ PDEVICE_OBJECT DeviceObject,
                  _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp =
        IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File =
        FileObject ? FileObject->FsContext : NULL;
    PHandleContextBlock Handle =
        FileObject ? FileObject->FsContext2 : NULL;
    PVolumeContextBlock Volume;
    PFileContextBlock Candidate;
    NTFS3G_ROS_FILE *CoreFile;
    PLIST_ENTRY Entry;
    ULONG OpenHandleCount = 0;
    ULONG ControlCode =
        IrpSp->Parameters.FileSystemControl.FsControlCode;
    BOOLEAN NotifyLock = FALSE;
    BOOLEAN NotifyLockFailed = FALSE;
    BOOLEAN NotifyUnlock = FALSE;
    BOOLEAN NotifyDismount = FALSE;
    KIRQL VpbIrql;
    NTSTATUS Status;
    int Result;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject)
        return STATUS_INVALID_DEVICE_REQUEST;
    switch (ControlCode) {
        case FSCTL_LOCK_VOLUME:
        case FSCTL_UNLOCK_VOLUME:
        case FSCTL_DISMOUNT_VOLUME:
        case FSCTL_IS_VOLUME_MOUNTED:
        case FSCTL_IS_VOLUME_DIRTY:
        case FSCTL_ALLOW_EXTENDED_DASD_IO:
            break;
        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
    if (!File || !File->IsVolume || !Handle ||
        Handle->CleanupComplete)
        return STATUS_ACCESS_DENIED;
    Volume = DeviceObject->DeviceExtension;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &Volume->FcbListResource, TRUE);
    switch (ControlCode) {
        case FSCTL_LOCK_VOLUME:
LockVolumeScan:
            if (Volume->Dismounted || !Volume->Volume) {
                Status = STATUS_VOLUME_DISMOUNTED;
                break;
            }
            if (Volume->LockFileObject == FileObject) {
                Status = STATUS_SUCCESS;
                break;
            }
            if (Volume->LockFileObject) {
                Status = STATUS_ACCESS_DENIED;
                NotifyLockFailed = TRUE;
                break;
            }

            Candidate = NULL;
            OpenHandleCount = 0;
            for (Entry = Volume->FcbListHead.Flink;
                 Entry != &Volume->FcbListHead;
                 Entry = Entry->Flink) {
                PFileContextBlock Current = CONTAINING_RECORD(
                    Entry, FileContextBlock, ListEntry);

                if (Current->IsVolume) {
                    OpenHandleCount += Current->OpenHandleCount;
                    continue;
                }
                if (Current->OpenHandleCount) {
                    Status = STATUS_ACCESS_DENIED;
                    NotifyLockFailed = TRUE;
                    goto LockVolumeComplete;
                }
                if (!Candidate &&
                    (Current->File ||
                     Current->SectionObjectPointers.SharedCacheMap ||
                     Current->SectionObjectPointers.DataSectionObject ||
                     Current->SectionObjectPointers.ImageSectionObject)) {
                    NtfsReferenceFcb(Current);
                    Candidate = Current;
                }
            }

            if (Candidate) {
                ExReleaseResourceLite(&Volume->FcbListResource);
                KeLeaveCriticalRegion();
                Status = NtfsPurgeFcbForVolumeLock(Candidate);
                KeEnterCriticalRegion();
                ExAcquireResourceExclusiveLite(
                    &Volume->FcbListResource, TRUE);
                if (!NT_SUCCESS(Status) ||
                    Candidate->OpenHandleCount ||
                    Candidate->SectionObjectPointers.SharedCacheMap ||
                    Candidate->SectionObjectPointers.DataSectionObject ||
                    Candidate->SectionObjectPointers.ImageSectionObject) {
                    NtfsDereferenceFcb(Candidate);
                    NotifyLockFailed = TRUE;
                    Status = NT_SUCCESS(Status) ?
                        STATUS_ACCESS_DENIED : Status;
                    break;
                }

                ExAcquireResourceExclusiveLite(
                    &Candidate->MainResource, TRUE);
                CoreFile = Candidate->File;
                Candidate->File = NULL;
                ExReleaseResourceLite(&Candidate->MainResource);
                NtfsDereferenceFcb(Candidate);

                ExReleaseResourceLite(&Volume->FcbListResource);
                KeLeaveCriticalRegion();
                if (CoreFile)
                    Ntfs3gRosCloseFile(CoreFile);
                KeEnterCriticalRegion();
                ExAcquireResourceExclusiveLite(
                    &Volume->FcbListResource, TRUE);
                goto LockVolumeScan;
            }
            if (OpenHandleCount != 1) {
                Status = STATUS_ACCESS_DENIED;
                NotifyLockFailed = TRUE;
                break;
            }

            Result = Ntfs3gRosFlushVolume(Volume->Volume);
            if (Result < 0) {
                Status = Ntfs3gRosStatusFromError(-Result);
                NotifyLockFailed = TRUE;
                break;
            }
            Volume->LockFileObject = FileObject;
            IoAcquireVpbSpinLock(&VpbIrql);
            DeviceObject->Vpb->Flags |= VPB_LOCKED;
            IoReleaseVpbSpinLock(VpbIrql);
            NotifyLock = TRUE;
            Status = STATUS_SUCCESS;
            break;

LockVolumeComplete:
            break;

        case FSCTL_UNLOCK_VOLUME:
            if (Volume->LockFileObject != FileObject) {
                Status = STATUS_ACCESS_DENIED;
                break;
            }
            Volume->LockFileObject = NULL;
            IoAcquireVpbSpinLock(&VpbIrql);
            DeviceObject->Vpb->Flags &= ~VPB_LOCKED;
            if (!Volume->Dismounted)
                DeviceObject->Vpb->Flags &=
                    ~VPB_DIRECT_WRITES_ALLOWED;
            IoReleaseVpbSpinLock(VpbIrql);
            NotifyUnlock = !Volume->Dismounted;
            Status = STATUS_SUCCESS;
            break;

        case FSCTL_DISMOUNT_VOLUME:
            if (Volume->LockFileObject != FileObject) {
                Status = STATUS_ACCESS_DENIED;
                break;
            }
            if (Volume->Dismounted || !Volume->Volume) {
                Status = STATUS_VOLUME_DISMOUNTED;
                break;
            }
            Result = Ntfs3gRosFlushVolume(Volume->Volume);
            if (Result < 0) {
                Status = Ntfs3gRosStatusFromError(-Result);
                break;
            }
            Status = Ntfs3gRosUnmountDevice(Volume->Volume);
            if (!NT_SUCCESS(Status))
                break;
            Volume->Volume = NULL;
            Volume->Dismounted = TRUE;
            IoAcquireVpbSpinLock(&VpbIrql);
            DeviceObject->Vpb->Flags &= ~VPB_MOUNTED;
            DeviceObject->Vpb->Flags |= VPB_DIRECT_WRITES_ALLOWED;
            IoReleaseVpbSpinLock(VpbIrql);
            if (Volume->ShutdownRegistered) {
                IoUnregisterShutdownNotification(DeviceObject);
                Volume->ShutdownRegistered = FALSE;
            }
            NotifyDismount = TRUE;
            Status = STATUS_SUCCESS;
            break;

        case FSCTL_IS_VOLUME_MOUNTED:
            Status = Volume->Dismounted || !Volume->Volume ?
                STATUS_VOLUME_DISMOUNTED : STATUS_SUCCESS;
            break;

        case FSCTL_IS_VOLUME_DIRTY:
            if (!Irp->AssociatedIrp.SystemBuffer ||
                IrpSp->Parameters.FileSystemControl.OutputBufferLength <
                    sizeof(ULONG)) {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            *(PULONG)Irp->AssociatedIrp.SystemBuffer = 0;
            Irp->IoStatus.Information = sizeof(ULONG);
            Status = STATUS_SUCCESS;
            break;

        case FSCTL_ALLOW_EXTENDED_DASD_IO:
            Handle->ExtendedDasdIo = TRUE;
            Status = STATUS_SUCCESS;
            break;

        default:
            ASSERT(FALSE);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    ExReleaseResourceLite(&Volume->FcbListResource);
    KeLeaveCriticalRegion();

    if (NotifyLock)
        FsRtlNotifyVolumeEvent(FileObject, FSRTL_VOLUME_LOCK);
    else if (NotifyLockFailed)
        FsRtlNotifyVolumeEvent(
            FileObject, FSRTL_VOLUME_LOCK_FAILED);
    if (NotifyUnlock)
        FsRtlNotifyVolumeEvent(FileObject, FSRTL_VOLUME_UNLOCK);
    if (NotifyDismount) {
        FsRtlNotifyVolumeEvent(
            FileObject, FSRTL_VOLUME_DISMOUNT);
        FsRtlDismountComplete(Volume->StorageDevice, Status);
    }
    return Status;
}

NTSTATUS
NTAPI
NtfsFsdFileSystemControl(_In_ PDEVICE_OBJECT DeviceObject,
                         _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    BOOLEAN TopLevel = IoGetTopLevelIrp() == NULL;
    NTSTATUS Status;

    PAGED_CODE();
    Irp->IoStatus.Information = 0;
    FsRtlEnterFileSystem();
    if (TopLevel)
        IoSetTopLevelIrp(Irp);

    switch (IrpSp->MinorFunction) {
        case IRP_MN_MOUNT_VOLUME:
            Status = NtfsMountVolume(IrpSp->Parameters.MountVolume.DeviceObject,
                                     IrpSp->Parameters.MountVolume.Vpb);
            break;
        case IRP_MN_VERIFY_VOLUME:
            Status = NtfsVerifyVolume(DeviceObject);
            break;
        case IRP_MN_USER_FS_REQUEST:
            Status = NtfsUserFsRequest(DeviceObject, Irp);
            break;
        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    if (TopLevel)
        IoSetTopLevelIrp(NULL);
    FsRtlExitFileSystem();
    return NtfsCompleteRequest(
        Irp, Status, Irp->IoStatus.Information);
}

NTSTATUS
NTAPI
NtfsFsdFlushBuffers(_In_ PDEVICE_OBJECT DeviceObject,
                    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PVolumeContextBlock Volume;
    PFileContextBlock File;
    IO_STATUS_BLOCK CacheStatus;
    int Result;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    Volume = DeviceObject->DeviceExtension;
    File = IrpSp->FileObject ? IrpSp->FileObject->FsContext : NULL;
    if (File && File->IsVolume) {
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoGetNextIrpStackLocation(Irp)->FileObject = NULL;
        IoGetNextIrpStackLocation(Irp)->Flags |=
            SL_OVERRIDE_VERIFY_VOLUME;
        return IoCallDriver(Volume->StorageDevice, Irp);
    }
    if (Volume->Dismounted || !Volume->Volume)
        return NtfsCompleteRequest(
            Irp, STATUS_VOLUME_DISMOUNTED, 0);
    if (File && File->SectionObjectPointers.SharedCacheMap) {
        CcFlushCache(&File->SectionObjectPointers, NULL, 0, &CacheStatus);
        if (!NT_SUCCESS(CacheStatus.Status))
            return NtfsCompleteRequest(Irp, CacheStatus.Status, 0);
    }
    if (File && File->File) {
        KeEnterCriticalRegion();
        ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
        ExAcquireResourceExclusiveLite(
            &File->PagingIoResource, TRUE);
        Result = Ntfs3gRosFlushFile(File->File);
        ExReleaseResourceLite(&File->PagingIoResource);
        ExReleaseResourceLite(&File->MainResource);
        KeLeaveCriticalRegion();
        if (Result < 0)
            return NtfsCompleteRequest(
                Irp, Ntfs3gRosStatusFromError(-Result), 0);
    }
    Result = Ntfs3gRosFlushVolume(Volume->Volume);
    return NtfsCompleteRequest(
        Irp,
        Result < 0 ? Ntfs3gRosStatusFromError(-Result) : STATUS_SUCCESS,
        0);
}
