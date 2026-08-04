/*
 * PROJECT:     ReactOS ACPI Platform Device Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     PnP ownership for ACPI devices controlled by acpi.sys
 */

#include <ntddk.h>

#define NDEBUG
#include <debug.h>

#define ACPIPLAT_TAG 'tlPA'

typedef struct _ACPIPLAT_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
} ACPIPLAT_DEVICE_EXTENSION, *PACPIPLAT_DEVICE_EXTENSION;

static
NTSTATUS
NTAPI
AcpiPlatCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
AcpiPlatForwardSynchronously(
    _In_ PACPIPLAT_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, AcpiPlatCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
VOID
AcpiPlatReportDevice(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PWSTR HardwareIds;
    ULONG Length = 0;
    NTSTATUS Status;

    Status = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyHardwareID, 0, NULL, &Length);
    if (Status != STATUS_BUFFER_TOO_SMALL || Length < sizeof(WCHAR))
        return;
    HardwareIds = ExAllocatePoolWithTag(PagedPool, Length, ACPIPLAT_TAG);
    if (!HardwareIds)
        return;
    Status = IoGetDeviceProperty(PhysicalDeviceObject, DevicePropertyHardwareID, Length, HardwareIds, &Length);
    if (NT_SUCCESS(Status))
        DPRINT1("ACPIPLAT: firmware-owned platform device %ls is active\n", HardwareIds);
    ExFreePoolWithTag(HardwareIds, ACPIPLAT_TAG);
}

static
NTSTATUS
NTAPI
AcpiPlatPassThroughCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PACPIPLAT_DEVICE_EXTENSION DeviceExtension = Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}

static
NTSTATUS
NTAPI
AcpiPlatPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPLAT_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, AcpiPlatPassThroughCompletion, DeviceExtension, TRUE, TRUE, TRUE);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AcpiPlatPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPLAT_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = AcpiPlatForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                AcpiPlatReportDevice(DeviceExtension->PhysicalDevice);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_REMOVE_DEVICE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = AcpiPlatForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(DeviceExtension->LowerDevice, Irp);
    }
}

static
NTSTATUS
NTAPI
AcpiPlatPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPLAT_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AcpiPlatAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PACPIPLAT_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*DeviceExtension), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, ACPIPLAT_TAG, 0, 0);
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
AcpiPlatUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->MajorFunction[IRP_MJ_CREATE] = AcpiPlatPassThrough;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AcpiPlatPassThrough;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = AcpiPlatPassThrough;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AcpiPlatPassThrough;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = AcpiPlatPassThrough;
    DriverObject->MajorFunction[IRP_MJ_PNP] = AcpiPlatPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AcpiPlatPower;
    DriverObject->DriverExtension->AddDevice = AcpiPlatAddDevice;
    DriverObject->DriverUnload = AcpiPlatUnload;
    return STATUS_SUCCESS;
}
