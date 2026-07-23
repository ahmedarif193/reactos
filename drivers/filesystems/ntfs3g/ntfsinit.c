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

    RtlZeroMemory(&FastIoDispatch, sizeof(FastIoDispatch));
    FastIoDispatch.SizeOfFastIoDispatch = sizeof(FastIoDispatch);
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
    PFileContextBlock File = IrpSp->FileObject ? IrpSp->FileObject->FsContext : NULL;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (File)
        FsRtlFastUnlockAll(&File->FileLock, IrpSp->FileObject,
                           IoGetRequestorProcess(Irp), NULL);
    return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
NTAPI
NtfsFsdLockControl(_In_ PDEVICE_OBJECT DeviceObject,
                   _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return NtfsCompleteRequest(Irp, STATUS_NOT_SUPPORTED, 0);
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
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(Volume->StorageDevice, Irp);
}

NTSTATUS
NTAPI
NtfsFsdShutdown(_In_ PDEVICE_OBJECT DeviceObject,
                _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return NtfsCompleteRequest(Irp, STATUS_SUCCESS, 0);
}
