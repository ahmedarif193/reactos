/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file-system driver entry points
 * COPYRIGHT:   Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2024 Carl J. Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

PDEVICE_OBJECT NtfsDiskFileSystemDeviceObject;
FAST_IO_DISPATCH FastIoDispatch;
CACHE_MANAGER_CALLBACKS CacheManagerCallbacks;

NTSTATUS
NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject,
            _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(L"\\Ntfs");
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    Status = Ntfs3gRosInitializeKernelLibrary();
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IoCreateDevice(DriverObject,
                            0,
                            &DeviceName,
                            FILE_DEVICE_DISK_FILE_SYSTEM,
                            0,
                            FALSE,
                            &NtfsDiskFileSystemDeviceObject);
    if (!NT_SUCCESS(Status)) {
        Ntfs3gRosUninitializeKernelLibrary();
        return Status;
    }

    DriverObject->DriverUnload = NtfsUnload;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = NtfsFsdCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = NtfsFsdClose;
    DriverObject->MajorFunction[IRP_MJ_READ] = NtfsFsdRead;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = NtfsFsdWrite;
    DriverObject->MajorFunction[IRP_MJ_QUERY_INFORMATION] = NtfsFsdQueryInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_INFORMATION] = NtfsFsdSetInformation;
    DriverObject->MajorFunction[IRP_MJ_QUERY_EA] = NtfsFsdQueryEa;
    DriverObject->MajorFunction[IRP_MJ_SET_EA] = NtfsFsdSetEa;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = NtfsFsdFlushBuffers;
    DriverObject->MajorFunction[IRP_MJ_QUERY_VOLUME_INFORMATION] = NtfsFsdQueryVolumeInformation;
    DriverObject->MajorFunction[IRP_MJ_SET_VOLUME_INFORMATION] = NtfsFsdSetVolumeInformation;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = NtfsFsdCleanup;
    DriverObject->MajorFunction[IRP_MJ_DIRECTORY_CONTROL] = NtfsFsdDirectoryControl;
    DriverObject->MajorFunction[IRP_MJ_FILE_SYSTEM_CONTROL] = NtfsFsdFileSystemControl;
    DriverObject->MajorFunction[IRP_MJ_LOCK_CONTROL] = NtfsFsdLockControl;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = NtfsFsdDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = NtfsFsdShutdown;

    RtlZeroMemory(&CacheManagerCallbacks, sizeof(CacheManagerCallbacks));
    CacheManagerCallbacks.AcquireForLazyWrite = NtfsAcquireForLazyWrite;
    CacheManagerCallbacks.ReleaseFromLazyWrite = NtfsReleaseFromLazyWrite;
    CacheManagerCallbacks.AcquireForReadAhead = NtfsAcquireForReadAhead;
    CacheManagerCallbacks.ReleaseFromReadAhead = NtfsReleaseFromReadAhead;

    RtlZeroMemory(&FastIoDispatch, sizeof(FastIoDispatch));
    FastIoDispatch.SizeOfFastIoDispatch = sizeof(FastIoDispatch);
    FastIoDispatch.FastIoCheckIfPossible = NtfsFastIoCheckIfPossible;
    FastIoDispatch.FastIoRead = FsRtlCopyRead;
    FastIoDispatch.FastIoWrite = NtfsFastIoWrite;
    FastIoDispatch.AcquireFileForNtCreateSection = NtfsFastIoAcquireFileForNtCreateSection;
    FastIoDispatch.ReleaseFileForNtCreateSection = NtfsFastIoReleaseFileForNtCreateSection;
    DriverObject->FastIoDispatch = &FastIoDispatch;

    NtfsDiskFileSystemDeviceObject->Flags |= DO_DIRECT_IO;
    NtfsDiskFileSystemDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    IoRegisterFileSystem(NtfsDiskFileSystemDeviceObject);
    return STATUS_SUCCESS;
}

VOID
NTAPI
NtfsUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    IoUnregisterFileSystem(NtfsDiskFileSystemDeviceObject);
    IoDeleteDevice(NtfsDiskFileSystemDeviceObject);
    Ntfs3gRosUninitializeKernelLibrary();
}

NTSTATUS
NTAPI
NtfsFsdCleanup(_In_ PDEVICE_OBJECT DeviceObject,
               _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);

    UNREFERENCED_PARAMETER(DeviceObject);
    if (IrpSp->FileObject)
        NtfsCleanupFileObject(IrpSp->FileObject,
                              IoGetRequestorProcess(Irp));
    return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
NTAPI
NtfsFsdLockControl(_In_ PDEVICE_OBJECT DeviceObject,
                   _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File;
    PHandleContextBlock Handle;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject || !FileObject)
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    File = FileObject->FsContext;
    Handle = FileObject->FsContext2;
    if (!File)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
    if (!Handle || Handle->CleanupComplete)
        return NtfsCompleteRequest(Irp, STATUS_FILE_CLOSED, 0);
    if (File->IsVolume ||
        (File->Information.Attributes & NTFS3G_ROS_FILE_DIRECTORY))
        return NtfsCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);

    /*
     * FsRtlProcessFileLock owns completion (and may pend the IRP), so the
     * request must not pass through NtfsCompleteRequest after this point.
     */
    return FsRtlProcessFileLock(&File->FileLock, Irp, NULL);
}

NTSTATUS
NTAPI
NtfsFsdDeviceControl(_In_ PDEVICE_OBJECT DeviceObject,
                     _Inout_ PIRP Irp)
{
    PVolumeContextBlock Volume;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject)
        return NtfsCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    Volume = DeviceObject->DeviceExtension;
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoGetNextIrpStackLocation(Irp)->FileObject = NULL;
    IoGetNextIrpStackLocation(Irp)->Flags |=
        SL_OVERRIDE_VERIFY_VOLUME;
    return IoCallDriver(Volume->StorageDevice, Irp);
}

static NTSTATUS
NtfsShutdownStorage(_In_ PDEVICE_OBJECT StorageDevice)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(&IoStatus, sizeof(IoStatus));
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_SHUTDOWN,
                                       StorageDevice,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    IoGetNextIrpStackLocation(Irp)->Flags |= SL_OVERRIDE_VERIFY_VOLUME;
    Status = IoCallDriver(StorageDevice, Irp);
    if (Status == STATUS_PENDING) {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

NTSTATUS
NTAPI
NtfsFsdShutdown(_In_ PDEVICE_OBJECT DeviceObject,
                _Inout_ PIRP Irp)
{
    PVolumeContextBlock Volume;
    PFileContextBlock File;
    PLIST_ENTRY Entry;
    IO_STATUS_BLOCK CacheStatus;
    BOOLEAN TopLevel;
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS CurrentStatus;
    int Result;

    if (DeviceObject == NtfsDiskFileSystemDeviceObject)
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    Volume = DeviceObject->DeviceExtension;
    FsRtlEnterFileSystem();
    TopLevel = IoGetTopLevelIrp() == NULL;
    if (TopLevel)
        IoSetTopLevelIrp(Irp);

    ExAcquireResourceExclusiveLite(
        &Volume->FcbListResource, TRUE);
    if (Volume->ShutdownStarted) {
        ExReleaseResourceLite(&Volume->FcbListResource);
        if (TopLevel)
            IoSetTopLevelIrp(NULL);
        FsRtlExitFileSystem();
        return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
    }
    Volume->ShutdownStarted = TRUE;

    for (Entry = Volume->FcbListHead.Flink;
         Entry != &Volume->FcbListHead;
         Entry = Entry->Flink) {
        File = CONTAINING_RECORD(
            Entry, FileContextBlock, ListEntry);
        if (File->SectionObjectPointers.SharedCacheMap) {
            CacheStatus.Status = STATUS_SUCCESS;
            CacheStatus.Information = 0;
            CcFlushCache(&File->SectionObjectPointers,
                         NULL,
                         0,
                         &CacheStatus);
            if (!NT_SUCCESS(CacheStatus.Status) &&
                NT_SUCCESS(Status))
                Status = CacheStatus.Status;
        }

        ExAcquireResourceExclusiveLite(
            &File->MainResource, TRUE);
        ExAcquireResourceExclusiveLite(
            &File->PagingIoResource, TRUE);
        if (File->File) {
            Result = Ntfs3gRosFlushFile(File->File);
            if (Result < 0 && NT_SUCCESS(Status))
                Status = Ntfs3gRosStatusFromError(-Result);
        }
        ExReleaseResourceLite(&File->PagingIoResource);
        ExReleaseResourceLite(&File->MainResource);
    }

    Result = Ntfs3gRosFlushVolume(Volume->Volume);
    if (Result < 0 && NT_SUCCESS(Status))
        Status = Ntfs3gRosStatusFromError(-Result);
    ExReleaseResourceLite(&Volume->FcbListResource);

    CurrentStatus = NtfsShutdownStorage(Volume->StorageDevice);
    if (!NT_SUCCESS(CurrentStatus) && NT_SUCCESS(Status))
        Status = CurrentStatus;
    if (!NT_SUCCESS(Status)) {
        DbgPrintEx(DPFLTR_NTFS_ID,
                   DPFLTR_ERROR_LEVEL,
                   "NTFS3G: shutdown flush failed: %08lx\n",
                   Status);
    }

    if (TopLevel)
        IoSetTopLevelIrp(NULL);
    FsRtlExitFileSystem();
    return NtfsCompleteRequest(Irp, Status, 0);
}
