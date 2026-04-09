/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     IRP dispatch for staged NTFS port
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
NtfslxCompleteRequest(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NtfslxCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;

    if (NtfslxIsVolumeDevice(DeviceExtension) &&
        Stack->FileObject != NULL &&
        Stack->FileObject->FileName.Length != 0)
    {
        return NtfslxCompleteRequest(Irp, STATUS_NOT_IMPLEMENTED, 0);
    }

    return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, FILE_OPENED);
}

static
NTSTATUS
NtfslxClose(
    _Inout_ PIRP Irp)
{
    return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
NTAPI
NtfslxFsdDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;

    FsRtlEnterFileSystem();

    Stack = IoGetCurrentIrpStackLocation(Irp);
    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            Status = NtfslxCreate(DeviceObject, Irp);
            break;

        case IRP_MJ_CLOSE:
        case IRP_MJ_CLEANUP:
            Status = NtfslxClose(Irp);
            break;

        case IRP_MJ_FILE_SYSTEM_CONTROL:
            Status = NtfslxFileSystemControl(DeviceObject, Irp);
            break;

        default:
            Status = NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
            break;
    }

    FsRtlExitFileSystem();
    return Status;
}
