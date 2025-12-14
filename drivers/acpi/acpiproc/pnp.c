/*
 * PROJECT:     ReactOS ACPI Processor Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     PnP dispatching for ACPI processor devices
 * COPYRIGHT:   2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "acpiproc.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
AcpiprocHandleStartDevice(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _Inout_ PIRP Irp)
{
    NTSTATUS Status;

    Status = AcpiprocForwardIrpAndWait(DeviceExtension, Irp);
    if (NT_SUCCESS(Status))
    {
        Status = AcpiprocStartDevice(DeviceExtension);
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
AcpiprocDispatchPnP(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPROC_DEVICE DeviceExtension = (PACPIPROC_DEVICE)DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = AcpiprocHandleStartDevice(DeviceExtension, Irp);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            return Status;

        case IRP_MN_STOP_DEVICE:
            AcpiprocStopDevice(DeviceExtension);
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            AcpiprocStopDevice(DeviceExtension);
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            break;

        case IRP_MN_REMOVE_DEVICE:
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            AcpiprocStopDevice(DeviceExtension);
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);

            if (DeviceExtension->LowerDevice)
            {
                IoDetachDevice(DeviceExtension->LowerDevice);
                DeviceExtension->LowerDevice = NULL;
            }

            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            break;
    }

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return Status;
}
