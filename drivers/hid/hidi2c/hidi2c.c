/*
 * PROJECT:     ReactOS HID over I2C minidriver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     hidclass minidriver dispatch and PnP/power integration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "hidi2c.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;

static
NTSTATUS
NTAPI
Hidi2cSignalCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    KeSetEvent(Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
Hidi2cForwardAndWait(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ BOOLEAN PowerIrp)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           Hidi2cSignalCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);
    Status = PowerIrp ? PoCallDriver(HidExtension->NextDeviceObject, Irp) :
                        IoCallDriver(HidExtension->NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    return Irp->IoStatus.Status;
}

static
NTSTATUS
Hidi2cCompleteIrp(
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
NTAPI
Hidi2cCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return Hidi2cCompleteIrp(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NTAPI
Hidi2cInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidExtension->MiniDeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    PHID_DEVICE_ATTRIBUTES Attributes;
    ULONG Length;

    switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
            if (IrpStack->Parameters.DeviceIoControl.OutputBufferLength <
                sizeof(HID_DEVICE_ATTRIBUTES) || Irp->UserBuffer == NULL)
            {
                return Hidi2cCompleteIrp(Irp,
                                         STATUS_INVALID_BUFFER_SIZE,
                                         0);
            }
            Attributes = Irp->UserBuffer;
            RtlZeroMemory(Attributes, sizeof(*Attributes));
            Attributes->Size = sizeof(*Attributes);
            Attributes->VendorID = DeviceExtension->I2cDescriptor.VendorId;
            Attributes->ProductID = DeviceExtension->I2cDescriptor.ProductId;
            Attributes->VersionNumber = DeviceExtension->I2cDescriptor.VersionId;
            return Hidi2cCompleteIrp(Irp,
                                     STATUS_SUCCESS,
                                     sizeof(*Attributes));

        case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
            if (Irp->UserBuffer == NULL)
                return Hidi2cCompleteIrp(Irp, STATUS_INVALID_USER_BUFFER, 0);
            Length = min(IrpStack->Parameters.DeviceIoControl.OutputBufferLength,
                         (ULONG)sizeof(HID_DESCRIPTOR));
            RtlCopyMemory(Irp->UserBuffer,
                          &DeviceExtension->HidDescriptor,
                          Length);
            return Hidi2cCompleteIrp(Irp, STATUS_SUCCESS, Length);

        case IOCTL_HID_GET_REPORT_DESCRIPTOR:
            if (Irp->UserBuffer == NULL ||
                DeviceExtension->ReportDescriptor == NULL)
            {
                return Hidi2cCompleteIrp(Irp, STATUS_INVALID_USER_BUFFER, 0);
            }
            Length = min(IrpStack->Parameters.DeviceIoControl.OutputBufferLength,
                         (ULONG)DeviceExtension->I2cDescriptor.ReportDescriptorLength);
            RtlCopyMemory(Irp->UserBuffer,
                          DeviceExtension->ReportDescriptor,
                          Length);
            return Hidi2cCompleteIrp(Irp, STATUS_SUCCESS, Length);

        case IOCTL_HID_READ_REPORT:
            return Hidi2cQueueReadReport(DeviceObject, Irp);

        case IOCTL_HID_WRITE_REPORT:
        case IOCTL_HID_GET_FEATURE:
        case IOCTL_HID_SET_FEATURE:
        case IOCTL_HID_SET_OUTPUT_REPORT:
        case IOCTL_HID_GET_INPUT_REPORT:
            return Hidi2cReportRequest(
                       DeviceObject,
                       Irp,
                       IrpStack->Parameters.DeviceIoControl.IoControlCode);

        case IOCTL_HID_ACTIVATE_DEVICE:
        case IOCTL_HID_DEACTIVATE_DEVICE:
            return Hidi2cCompleteIrp(Irp, STATUS_SUCCESS, 0);

        default:
            return Hidi2cCompleteIrp(Irp,
                                     STATUS_NOT_SUPPORTED,
                                     0);
    }
}

static
NTSTATUS
NTAPI
Hidi2cPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (IrpStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = Hidi2cForwardAndWait(DeviceObject, Irp, FALSE);
            if (NT_SUCCESS(Status))
            {
                Status = Hidi2cStartDevice(
                             DeviceObject,
                             IrpStack->Parameters.StartDevice.AllocatedResourcesTranslated);
            }
            return Hidi2cCompleteIrp(Irp, Status, 0);

        case IRP_MN_STOP_DEVICE:
            Hidi2cStopDevice(DeviceObject);
            Status = Hidi2cForwardAndWait(DeviceObject, Irp, FALSE);
            return Hidi2cCompleteIrp(Irp, Status, 0);

        case IRP_MN_SURPRISE_REMOVAL:
        case IRP_MN_REMOVE_DEVICE:
            Hidi2cStopDevice(DeviceObject);
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(HidExtension->NextDeviceObject, Irp);

        case IRP_MN_QUERY_CAPABILITIES:
            Status = Hidi2cForwardAndWait(DeviceObject, Irp, FALSE);
            if (NT_SUCCESS(Status) &&
                IrpStack->Parameters.DeviceCapabilities.Capabilities != NULL)
            {
                IrpStack->Parameters.DeviceCapabilities.Capabilities->SurpriseRemovalOK = TRUE;
            }
            return Hidi2cCompleteIrp(Irp, Status, Irp->IoStatus.Information);

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(HidExtension->NextDeviceObject, Irp);
    }
}

static
NTSTATUS
NTAPI
Hidi2cPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    DEVICE_POWER_STATE TargetState;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);
    if (IrpStack->MinorFunction != IRP_MN_SET_POWER ||
        IrpStack->Parameters.Power.Type != DevicePowerState)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(HidExtension->NextDeviceObject, Irp);
    }

    TargetState = IrpStack->Parameters.Power.State.DeviceState;
    if (TargetState == PowerDeviceD0)
    {
        Status = Hidi2cForwardAndWait(DeviceObject, Irp, TRUE);
        if (NT_SUCCESS(Status))
            Status = Hidi2cSetDevicePower(DeviceObject, TRUE);
    }
    else
    {
        Status = Hidi2cSetDevicePower(DeviceObject, FALSE);
        if (!NT_SUCCESS(Status))
            DPRINT1("hidi2c: SET_POWER(SLEEP) failed %08lx\n", Status);
        Status = Hidi2cForwardAndWait(DeviceObject, Irp, TRUE);
    }
    return Hidi2cCompleteIrp(Irp, Status, 0);
}

static
NTSTATUS
NTAPI
Hidi2cSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(HidExtension->NextDeviceObject, Irp);
}

static
NTSTATUS
NTAPI
Hidi2cAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidExtension->MiniDeviceExtension;

    UNREFERENCED_PARAMETER(DriverObject);

    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    return Hidi2cInitializeTransport(DeviceExtension);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    HID_MINIDRIVER_REGISTRATION Registration;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = Hidi2cCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Hidi2cCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] =
        Hidi2cInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = Hidi2cPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = Hidi2cPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = Hidi2cSystemControl;
    DriverObject->DriverExtension->AddDevice = Hidi2cAddDevice;

    RtlZeroMemory(&Registration, sizeof(Registration));
    Registration.Revision = HID_REVISION;
    Registration.DriverObject = DriverObject;
    Registration.RegistryPath = RegistryPath;
    Registration.DeviceExtensionSize = sizeof(HIDI2C_DEVICE_EXTENSION);
    Registration.DevicesArePolled = FALSE;
    return HidRegisterMinidriver(&Registration);
}
