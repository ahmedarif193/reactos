/*
 * PROJECT:     ReactOS Intel LPSS / DesignWare I2C driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDM/PnP, power, and Resource Hub controller interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "i2cdesignware.h"

#define NDEBUG
#include <debug.h>

DRIVER_INITIALIZE DriverEntry;

static
NTSTATUS
DwCompleteIrp(
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
DwSignalCompletion(
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
DwForwardAndWait(
    _In_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ BOOLEAN PowerIrp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           DwSignalCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);
    Status = PowerIrp ?
             PoCallDriver(DeviceExtension->LowerDeviceObject, Irp) :
             IoCallDriver(DeviceExtension->LowerDeviceObject, Irp);
    if (Status == STATUS_PENDING)
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    return Irp->IoStatus.Status;
}

static
NTSTATUS
DwQueryLocation(
    _Inout_ PDW_I2C_DEVICE_EXTENSION DeviceExtension)
{
    ULONG ResultLength;
    NTSTATUS Status;

    Status = IoGetDeviceProperty(DeviceExtension->PhysicalDeviceObject,
                                 DevicePropertyBusNumber,
                                 sizeof(DeviceExtension->BusNumber),
                                 &DeviceExtension->BusNumber,
                                 &ResultLength);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IoGetDeviceProperty(DeviceExtension->PhysicalDeviceObject,
                                 DevicePropertyAddress,
                                 sizeof(DeviceExtension->Address),
                                 &DeviceExtension->Address,
                                 &ResultLength);
    if (!NT_SUCCESS(Status))
        return Status;

    DeviceExtension->Segment = 0;
    return STATUS_SUCCESS;
}

static
NTSTATUS
DwStartDevice(
    _Inout_ PDW_I2C_DEVICE_EXTENSION DeviceExtension,
    _In_opt_ PCM_RESOURCE_LIST Resources)
{
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG FullIndex, PartialIndex;
    NTSTATUS Status = STATUS_DEVICE_CONFIGURATION_ERROR;

    if (Resources == NULL)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    for (FullIndex = 0; FullIndex < Resources->Count; FullIndex++)
    {
        FullDescriptor = &Resources->List[FullIndex];
        for (PartialIndex = 0;
             PartialIndex < FullDescriptor->PartialResourceList.Count;
             PartialIndex++)
        {
            Descriptor = &FullDescriptor->PartialResourceList.PartialDescriptors[PartialIndex];
            if (Descriptor->Type == CmResourceTypeMemory &&
                Descriptor->u.Memory.Length >= 0x300)
            {
                DeviceExtension->PhysicalBase = Descriptor->u.Memory.Start;
                DeviceExtension->RegistersLength = Descriptor->u.Memory.Length;
                DeviceExtension->Registers = MmMapIoSpace(
                                                 Descriptor->u.Memory.Start,
                                                 Descriptor->u.Memory.Length,
                                                 MmNonCached);
                if (DeviceExtension->Registers == NULL)
                    return STATUS_INSUFFICIENT_RESOURCES;
                Status = STATUS_SUCCESS;
                break;
            }
        }
        if (NT_SUCCESS(Status))
            break;
    }

    if (!NT_SUCCESS(Status))
        return Status;

    Status = DwQueryLocation(DeviceExtension);
    if (NT_SUCCESS(Status))
        Status = DwInitializeHardware(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        MmUnmapIoSpace(DeviceExtension->Registers,
                       DeviceExtension->RegistersLength);
        DeviceExtension->Registers = NULL;
        DeviceExtension->RegistersLength = 0;
        return Status;
    }

    Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        DwQuiesceHardware(DeviceExtension);
        MmUnmapIoSpace(DeviceExtension->Registers,
                       DeviceExtension->RegistersLength);
        DeviceExtension->Registers = NULL;
        DeviceExtension->RegistersLength = 0;
        return Status;
    }

    DeviceExtension->InterfaceEnabled = TRUE;
    DeviceExtension->Started = TRUE;
    DeviceExtension->DevicePowerState = PowerDeviceD0;
    return STATUS_SUCCESS;
}

static
VOID
DwStopDevice(
    _Inout_ PDW_I2C_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->InterfaceEnabled)
    {
        IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
        DeviceExtension->InterfaceEnabled = FALSE;
    }

    KeWaitForSingleObject(&DeviceExtension->TransferSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
    DeviceExtension->Started = FALSE;
    DeviceExtension->ControllerExternallyLocked = FALSE;
    if (DeviceExtension->Registers != NULL)
    {
        DwQuiesceHardware(DeviceExtension);
        MmUnmapIoSpace(DeviceExtension->Registers,
                       DeviceExtension->RegistersLength);
        DeviceExtension->Registers = NULL;
        DeviceExtension->RegistersLength = 0;
    }
    KeReleaseSemaphore(&DeviceExtension->TransferSemaphore,
                       IO_NO_INCREMENT,
                       1,
                       FALSE);
}

static
NTSTATUS
NTAPI
DwCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    return DwCompleteIrp(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NTAPI
DwDefaultDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PDW_I2C_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDeviceObject, Irp);
}

static
NTSTATUS
NTAPI
DwInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PDW_I2C_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    PREACTOS_SPB_CONTROLLER_INFO ControllerInfo;
    PREACTOS_SPB_TRANSFER_LIST TransferList;
    ULONG_PTR Information = 0;
    BOOLEAN SemaphoreAcquired = FALSE;
    NTSTATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return DwCompleteIrp(Irp, STATUS_INVALID_DEVICE_STATE, 0);

    switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_INTERNAL_SPB_QUERY_CONTROLLER:
            if (IrpStack->Parameters.DeviceIoControl.OutputBufferLength <
                sizeof(*ControllerInfo))
            {
                return DwCompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            }
            if (!DeviceExtension->Started)
                return DwCompleteIrp(Irp, STATUS_DEVICE_NOT_READY, 0);

            ControllerInfo = Irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(ControllerInfo, sizeof(*ControllerInfo));
            ControllerInfo->Version = REACTOS_SPB_INTERFACE_VERSION;
            ControllerInfo->Segment = DeviceExtension->Segment;
            ControllerInfo->BusNumber = DeviceExtension->BusNumber;
            ControllerInfo->Address = DeviceExtension->Address;
            return DwCompleteIrp(Irp, STATUS_SUCCESS, sizeof(*ControllerInfo));

        case IOCTL_INTERNAL_SPB_LOCK_CONTROLLER:
            Status = KeWaitForSingleObject(&DeviceExtension->TransferSemaphore,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           NULL);
            if (!NT_SUCCESS(Status))
                return DwCompleteIrp(Irp, Status, 0);
            if (!DeviceExtension->Started ||
                DeviceExtension->DevicePowerState != PowerDeviceD0)
            {
                KeReleaseSemaphore(&DeviceExtension->TransferSemaphore,
                                   IO_NO_INCREMENT,
                                   1,
                                   FALSE);
                return DwCompleteIrp(Irp, STATUS_DEVICE_NOT_READY, 0);
            }
            DeviceExtension->ControllerExternallyLocked = TRUE;
            return DwCompleteIrp(Irp, STATUS_SUCCESS, 0);

        case IOCTL_INTERNAL_SPB_UNLOCK_CONTROLLER:
            /* Atomic claim: with a plain check-then-clear two racing
               unlockers could both release the limit-1 semaphore, which
               raises an exception. Only the caller that flips TRUE->FALSE
               performs the release. */
            if (InterlockedExchange(&DeviceExtension->ControllerExternallyLocked, FALSE) == FALSE)
                return DwCompleteIrp(Irp, STATUS_INVALID_DEVICE_STATE, 0);
            KeReleaseSemaphore(&DeviceExtension->TransferSemaphore,
                               IO_NO_INCREMENT,
                               1,
                               FALSE);
            return DwCompleteIrp(Irp, STATUS_SUCCESS, 0);

        case IOCTL_INTERNAL_SPB_EXECUTE_TRANSFER_LIST:
            if (IrpStack->Parameters.DeviceIoControl.InputBufferLength <
                sizeof(*TransferList))
            {
                return DwCompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            }

            TransferList = Irp->AssociatedIrp.SystemBuffer;
            if (!(TransferList->Flags &
                  REACTOS_SPB_TRANSFER_FLAG_CONTROLLER_LOCKED))
            {
                Status = KeWaitForSingleObject(&DeviceExtension->TransferSemaphore,
                                               Executive,
                                               KernelMode,
                                               FALSE,
                                               NULL);
                if (!NT_SUCCESS(Status))
                    return DwCompleteIrp(Irp, Status, 0);
                SemaphoreAcquired = TRUE;
            }
            else if (!DeviceExtension->ControllerExternallyLocked)
            {
                return DwCompleteIrp(Irp, STATUS_INVALID_DEVICE_STATE, 0);
            }

            if (!DeviceExtension->Started ||
                DeviceExtension->DevicePowerState != PowerDeviceD0)
            {
                Status = STATUS_DEVICE_NOT_READY;
            }
            else
            {
                Status = DwExecuteTransferList(DeviceExtension,
                                               TransferList,
                                               &Information);
            }

            if (SemaphoreAcquired)
            {
                KeReleaseSemaphore(&DeviceExtension->TransferSemaphore,
                                   IO_NO_INCREMENT,
                                   1,
                                   FALSE);
            }
            return DwCompleteIrp(Irp, Status, Information);

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(DeviceExtension->LowerDeviceObject, Irp);
    }
}

static
NTSTATUS
NTAPI
DwPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PDW_I2C_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (IrpStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = DwForwardAndWait(DeviceExtension, Irp, FALSE);
            if (NT_SUCCESS(Status))
            {
                Status = DwStartDevice(
                             DeviceExtension,
                             IrpStack->Parameters.StartDevice.AllocatedResourcesTranslated);
            }
            return DwCompleteIrp(Irp, Status, 0);

        case IRP_MN_STOP_DEVICE:
            DwStopDevice(DeviceExtension);
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(DeviceExtension->LowerDeviceObject, Irp);

        case IRP_MN_SURPRISE_REMOVAL:
            DwStopDevice(DeviceExtension);
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(DeviceExtension->LowerDeviceObject, Irp);

        case IRP_MN_REMOVE_DEVICE:
            DwStopDevice(DeviceExtension);
            if (DeviceExtension->InterfaceRegistered)
            {
                RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
                DeviceExtension->InterfaceRegistered = FALSE;
            }
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDeviceObject, Irp);
            IoDetachDevice(DeviceExtension->LowerDeviceObject);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(DeviceExtension->LowerDeviceObject, Irp);
    }
}

static
NTSTATUS
NTAPI
DwPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PDW_I2C_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    DEVICE_POWER_STATE TargetState;
    NTSTATUS Status;

    PoStartNextPowerIrp(Irp);
    if (IrpStack->MinorFunction != IRP_MN_SET_POWER ||
        IrpStack->Parameters.Power.Type != DevicePowerState)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(DeviceExtension->LowerDeviceObject, Irp);
    }

    TargetState = IrpStack->Parameters.Power.State.DeviceState;
    if (TargetState == PowerDeviceD0)
    {
        Status = DwForwardAndWait(DeviceExtension, Irp, TRUE);
        if (NT_SUCCESS(Status) && DeviceExtension->Started &&
            DeviceExtension->Registers != NULL)
        {
            KeWaitForSingleObject(&DeviceExtension->TransferSemaphore,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            Status = DwInitializeHardware(DeviceExtension);
            if (NT_SUCCESS(Status))
                DeviceExtension->DevicePowerState = PowerDeviceD0;
            KeReleaseSemaphore(&DeviceExtension->TransferSemaphore,
                               IO_NO_INCREMENT,
                               1,
                               FALSE);
        }
        return DwCompleteIrp(Irp, Status, 0);
    }

    KeWaitForSingleObject(&DeviceExtension->TransferSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
    if (DeviceExtension->Registers != NULL)
        DwQuiesceHardware(DeviceExtension);
    DeviceExtension->DevicePowerState = TargetState;
    KeReleaseSemaphore(&DeviceExtension->TransferSemaphore,
                       IO_NO_INCREMENT,
                       1,
                       FALSE);
    Status = DwForwardAndWait(DeviceExtension, Irp, TRUE);
    return DwCompleteIrp(Irp, Status, 0);
}

static
NTSTATUS
NTAPI
DwAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDW_I2C_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(*DeviceExtension),
                            NULL,
                            FILE_DEVICE_CONTROLLER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->Self = DeviceObject;
    DeviceExtension->PhysicalDeviceObject = PhysicalDeviceObject;
    DeviceExtension->DevicePowerState = PowerDeviceD3;
    KeInitializeSemaphore(&DeviceExtension->TransferSemaphore, 1, 1);

    DeviceExtension->LowerDeviceObject =
        IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (DeviceExtension->LowerDeviceObject == NULL)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    Status = IoRegisterDeviceInterface(PhysicalDeviceObject,
                                       &GUID_REACTOS_SPB_CONTROLLER,
                                       NULL,
                                       &DeviceExtension->InterfaceName);
    if (!NT_SUCCESS(Status))
    {
        IoDetachDevice(DeviceExtension->LowerDeviceObject);
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    DeviceExtension->InterfaceRegistered = TRUE;
    DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = DwDefaultDispatch;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = DwCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = DwCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] =
        DwInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = DwPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = DwPower;
    DriverObject->DriverExtension->AddDevice = DwAddDevice;
    return STATUS_SUCCESS;
}
