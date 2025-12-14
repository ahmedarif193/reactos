/*
 * PROJECT:     ReactOS ACPI Processor Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver entry points and global bookkeeping
 * COPYRIGHT:   2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <initguid.h>
#include "acpiproc.h"

#define NDEBUG
#include <debug.h>

LONG AcpiprocNextProcessorIndex = 0;
LIST_ENTRY AcpiprocDeviceList;
FAST_MUTEX AcpiprocDeviceListLock;

static NTSTATUS AcpiprocAcquireAcpiInterface(_Inout_ PACPIPROC_DEVICE DeviceExtension);
static VOID AcpiprocReleaseAcpiInterface(_Inout_ PACPIPROC_DEVICE DeviceExtension);
static NTSTATUS AcpiprocSendQueryInterfaceIrp(_Inout_ PACPIPROC_DEVICE DeviceExtension);
static VOID NTAPI AcpiprocNotificationCallback(_In_ PVOID Context, _In_ ULONG NotifyValue);
static NTSTATUS NTAPI AcpiprocQueryInterfaceCompletion(_In_ PDEVICE_OBJECT DeviceObject,
                                                       _In_ PIRP Irp,
                                                       _In_ PVOID Context);

static
VOID
NTAPI
AcpiprocUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

static
VOID
AcpiprocInitializeDispatchTable(
    _Inout_ PDRIVER_OBJECT DriverObject)
{
    ULONG Index;

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; ++Index)
    {
        DriverObject->MajorFunction[Index] = AcpiprocDispatchDefault;
    }

    DriverObject->MajorFunction[IRP_MJ_PNP] = AcpiprocDispatchPnP;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AcpiprocDispatchPower;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AcpiprocDispatchDeviceControl;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DPRINT("acpiproc: DriverEntry\n");

    InitializeListHead(&AcpiprocDeviceList);
    ExInitializeFastMutex(&AcpiprocDeviceListLock);

    AcpiprocInitializeDispatchTable(DriverObject);

    DriverObject->DriverUnload = AcpiprocUnload;
    DriverObject->DriverExtension->AddDevice = AcpiprocAddDevice;

    return STATUS_SUCCESS;
}

static
VOID
AcpiprocInsertDeviceLocked(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    if (DeviceExtension->InGlobalList)
        return;

    InsertTailList(&AcpiprocDeviceList, &DeviceExtension->ListEntry);
    DeviceExtension->InGlobalList = TRUE;
}

static
VOID
AcpiprocRemoveDeviceLocked(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    if (!DeviceExtension->InGlobalList)
        return;

    RemoveEntryList(&DeviceExtension->ListEntry);
    InitializeListHead(&DeviceExtension->ListEntry);
    DeviceExtension->InGlobalList = FALSE;
}

VOID
AcpiprocStopDevice(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    ExAcquireFastMutex(&AcpiprocDeviceListLock);
    AcpiprocRemoveDeviceLocked(DeviceExtension);
    ExReleaseFastMutex(&AcpiprocDeviceListLock);

    DeviceExtension->Started = FALSE;
    DeviceExtension->DeviceStatus = 0;

    AcpiprocReleaseAcpiInterface(DeviceExtension);
    AcpiprocCleanupUid(DeviceExtension);
    AcpiprocCleanupPerfStates(DeviceExtension);
    AcpiprocCleanupIdleStates(DeviceExtension);
    AcpiprocCleanupThrottleStates(DeviceExtension);
    AcpiprocCleanupThermalInfo(DeviceExtension);
}

NTSTATUS
NTAPI
AcpiprocAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_OBJECT DeviceObject = NULL;
    PACPIPROC_DEVICE DeviceExtension;
    NTSTATUS Status;

    DPRINT("acpiproc: AddDevice %p\n", PhysicalDeviceObject);

    Status = IoCreateDevice(DriverObject,
                            sizeof(ACPIPROC_DEVICE),
                            NULL,
                            FILE_DEVICE_ACPI,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: IoCreateDevice failed 0x%lx\n", Status);
        return Status;
    }

    DeviceExtension = (PACPIPROC_DEVICE)DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->Self = DeviceObject;
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, ACPIPROC_TAG, 0, 0);
    InitializeListHead(&DeviceExtension->ListEntry);

    DeviceObject->Flags |= DO_POWER_PAGABLE;

    DeviceExtension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject,
                                                               PhysicalDeviceObject);
    if (!DeviceExtension->LowerDevice)
    {
        DPRINT1("acpiproc: IoAttachDeviceToDeviceStack failed\n");
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }

    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
AcpiprocDispatchDefault(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPROC_DEVICE DeviceExtension = (PACPIPROC_DEVICE)DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return Status;
}

NTSTATUS
NTAPI
AcpiprocDispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPROC_DEVICE DeviceExtension = (PACPIPROC_DEVICE)DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        PoStartNextPowerIrp(Irp);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    Status = PoCallDriver(DeviceExtension->LowerDevice, Irp);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return Status;
}

NTSTATUS
NTAPI
AcpiprocDispatchDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPROC_DEVICE DeviceExtension = (PACPIPROC_DEVICE)DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    BOOLEAN Handled = FALSE;
    ULONG_PTR Information = 0;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_ACPIPROC_QUERY_THROTTLE:
        {
            PACPIPROC_THROTTLE_INFO Info;
            SIZE_T RequiredSize;
            SIZE_T BaseSize;
            ULONG StateCount;

            Handled = TRUE;

            if (!DeviceExtension->Throttle.States ||
                DeviceExtension->Throttle.StateCount == 0)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }

            StateCount = DeviceExtension->Throttle.StateCount;
            BaseSize = FIELD_OFFSET(ACPIPROC_THROTTLE_INFO, States);
            RequiredSize = BaseSize + ((SIZE_T)StateCount * sizeof(ACPIPROC_THROTTLE_STATE_INFO));

            if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < RequiredSize)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Information = RequiredSize;
                break;
            }

            Info = (PACPIPROC_THROTTLE_INFO)Irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(Info, RequiredSize);

            Info->Version = ACPIPROC_THROTTLE_INFO_VERSION;
            Info->StateCount = StateCount;
            Info->ActiveState = DeviceExtension->Throttle.CurrentStateValid ?
                DeviceExtension->Throttle.CurrentStateIndex : MAXULONG;

            if (DeviceExtension->Throttle.TpcValid)
            {
                ULONG Limit = DeviceExtension->Throttle.TpcLimit;
                if (Limit >= StateCount)
                    Limit = StateCount - 1;
                Info->Flags |= ACPIPROC_THROTTLE_INFO_FLAG_TPC_VALID;
                Info->LimitIndex = Limit;
            }
            else
            {
                Info->LimitIndex = MAXULONG;
            }

            for (ULONG Index = 0; Index < StateCount; ++Index)
            {
                Info->States[Index] = *(PACPIPROC_THROTTLE_STATE_INFO)&DeviceExtension->Throttle.States[Index];
            }

            Information = RequiredSize;
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_ACPIPROC_SET_THROTTLE:
        {
            PACPIPROC_THROTTLE_REQUEST Request;

            Handled = TRUE;

            if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Request))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Information = sizeof(*Request);
                break;
            }

            Request = (PACPIPROC_THROTTLE_REQUEST)Irp->AssociatedIrp.SystemBuffer;
            Status = AcpiprocSetThrottleIndex(DeviceExtension, Request->StateIndex);
            break;
        }

        case IOCTL_ACPIPROC_QUERY_THERMAL:
        {
            PACPIPROC_THERMAL_INFO Info;

            Handled = TRUE;

            if (IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Info))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Information = sizeof(*Info);
                break;
            }

            Info = (PACPIPROC_THERMAL_INFO)Irp->AssociatedIrp.SystemBuffer;
            RtlZeroMemory(Info, sizeof(*Info));

            Info->Version = ACPIPROC_THERMAL_INFO_VERSION;

            if (DeviceExtension->Thermal.HotTripValid)
            {
                Info->Flags |= ACPIPROC_THERMAL_INFO_FLAG_HOT_VALID;
                Info->HotTripPoint = DeviceExtension->Thermal.HotTripPoint;
            }

            if (DeviceExtension->Thermal.CriticalTripValid)
            {
                Info->Flags |= ACPIPROC_THERMAL_INFO_FLAG_CRITICAL_VALID;
                Info->CriticalTripPoint = DeviceExtension->Thermal.CriticalTripPoint;
            }

            if (DeviceExtension->Thermal.HotEventPending)
                Info->Flags |= ACPIPROC_THERMAL_INFO_FLAG_HOT_PENDING;

            if (DeviceExtension->Thermal.CriticalEventPending)
                Info->Flags |= ACPIPROC_THERMAL_INFO_FLAG_CRITICAL_PENDING;

            Information = sizeof(*Info);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_ACPIPROC_CLEAR_THERMAL:
        {
            PACPIPROC_THERMAL_CLEAR Request;

            Handled = TRUE;

            if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Request))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Information = sizeof(*Request);
                break;
            }

            Request = (PACPIPROC_THERMAL_CLEAR)Irp->AssociatedIrp.SystemBuffer;

            if (Request->Flags & ACPIPROC_THERMAL_CLEAR_HOT)
                DeviceExtension->Thermal.HotEventPending = FALSE;

            if (Request->Flags & ACPIPROC_THERMAL_CLEAR_CRITICAL)
                DeviceExtension->Thermal.CriticalEventPending = FALSE;

            Status = STATUS_SUCCESS;
            break;
        }

        default:
            break;
    }

    if (!Handled)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
        IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
        return Status;
    }

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    Irp->IoStatus.Information = Information;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
AcpiprocStartDevice(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    ULONG ProcessorSta = 0;
    NTSTATUS Status;

    if (DeviceExtension->Started)
        return STATUS_SUCCESS;

    Status = AcpiprocEvaluateIntegerMethod(DeviceExtension, "_STA", &ProcessorSta);
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->DeviceStatus = ProcessorSta;
    }
    else
    {
        DPRINT1("acpiproc: Failed to query _STA 0x%lx\n", Status);
    }

    Status = AcpiprocQueryUid(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: _UID unavailable (status 0x%lx)\n", Status);
    }

    DeviceExtension->ProcessorIndex =
        (ULONG)InterlockedIncrement(&AcpiprocNextProcessorIndex) - 1;

    Status = AcpiprocInitializePerfStates(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: Failed to initialize performance states (status 0x%lx)\n",
                Status);
    }

    Status = AcpiprocInitializeIdleStates(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: Failed to initialize idle states (status 0x%lx)\n", Status);
    }

    Status = AcpiprocInitializeThrottleStates(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: Failed to initialize throttle states (status 0x%lx)\n",
                Status);
    }

    Status = AcpiprocInitializeThermalInfo(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: Failed to initialize thermal info (status 0x%lx)\n",
                Status);
    }

    Status = AcpiprocAcquireAcpiInterface(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: Failed to acquire ACPI interface (status 0x%lx)\n", Status);
    }

    DeviceExtension->Started = TRUE;

    ExAcquireFastMutex(&AcpiprocDeviceListLock);
    AcpiprocInsertDeviceLocked(DeviceExtension);
    ExReleaseFastMutex(&AcpiprocDeviceListLock);

    DPRINT("acpiproc: Processor #%lu started (STA 0x%lx, UID %s)\n",
           DeviceExtension->ProcessorIndex,
           DeviceExtension->DeviceStatus,
           DeviceExtension->Uid.Valid ? "present" : "<none>");

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AcpiprocQueryInterfaceCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
AcpiprocSendQueryInterfaceIrp(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    PIRP Irp;
    PIO_STACK_LOCATION IrpSp;
    KEVENT Event;
    NTSTATUS Status;

    if (!DeviceExtension || !DeviceExtension->LowerDevice)
        return STATUS_INVALID_PARAMETER;

    Irp = IoAllocateIrp(DeviceExtension->LowerDevice->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    IrpSp = IoGetNextIrpStackLocation(Irp);
    IrpSp->MajorFunction = IRP_MJ_PNP;
    IrpSp->MinorFunction = IRP_MN_QUERY_INTERFACE;
    IrpSp->Parameters.QueryInterface.InterfaceType = (LPGUID)&GUID_ACPI_INTERFACE_STANDARD;
    IrpSp->Parameters.QueryInterface.Size = sizeof(ACPI_INTERFACE_STANDARD);
    IrpSp->Parameters.QueryInterface.Version = 1;
    IrpSp->Parameters.QueryInterface.Interface = (PINTERFACE)&DeviceExtension->AcpiInterface;
    IrpSp->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    IoSetCompletionRoutine(Irp,
                           AcpiprocQueryInterfaceCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = Irp->IoStatus.Status;
    }

    IoFreeIrp(Irp);
    return Status;
}

static
NTSTATUS
AcpiprocAcquireAcpiInterface(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    NTSTATUS Status;

    if (!DeviceExtension)
        return STATUS_INVALID_PARAMETER;

    if (DeviceExtension->InterfaceAcquired)
        return STATUS_SUCCESS;

    Status = AcpiprocSendQueryInterfaceIrp(DeviceExtension);
    if (!NT_SUCCESS(Status))
        return Status;

    if (DeviceExtension->AcpiInterface.InterfaceReference)
    {
        DeviceExtension->AcpiInterface.InterfaceReference(DeviceExtension->AcpiInterface.Context);
    }

    DeviceExtension->InterfaceAcquired = TRUE;

    if (DeviceExtension->AcpiInterface.RegisterForDeviceNotifications)
    {
        Status = DeviceExtension->AcpiInterface.RegisterForDeviceNotifications(
            DeviceExtension->AcpiInterface.Context,
            AcpiprocNotificationCallback,
            DeviceExtension);
        if (NT_SUCCESS(Status))
        {
            DeviceExtension->NotificationsRegistered = TRUE;
        }
        else
        {
            DPRINT1("acpiproc: RegisterForDeviceNotifications failed 0x%lx\n", Status);
        }
    }

    return STATUS_SUCCESS;
}

static
VOID
AcpiprocReleaseAcpiInterface(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    if (!DeviceExtension)
        return;

    if (DeviceExtension->NotificationsRegistered &&
        DeviceExtension->AcpiInterface.UnregisterForDeviceNotifications)
    {
        DeviceExtension->AcpiInterface.UnregisterForDeviceNotifications(
            DeviceExtension->AcpiInterface.Context,
            AcpiprocNotificationCallback);
        DeviceExtension->NotificationsRegistered = FALSE;
    }

    if (DeviceExtension->InterfaceAcquired &&
        DeviceExtension->AcpiInterface.InterfaceDereference)
    {
        DeviceExtension->AcpiInterface.InterfaceDereference(
            DeviceExtension->AcpiInterface.Context);
    }

    DeviceExtension->InterfaceAcquired = FALSE;
    RtlZeroMemory(&DeviceExtension->AcpiInterface, sizeof(DeviceExtension->AcpiInterface));
}

static
VOID
NTAPI
AcpiprocNotificationCallback(
    _In_ PVOID Context,
    _In_ ULONG NotifyValue)
{
    PACPIPROC_DEVICE DeviceExtension = (PACPIPROC_DEVICE)Context;

    if (!DeviceExtension)
        return;

    if (!NT_SUCCESS(IoAcquireRemoveLock(&DeviceExtension->RemoveLock,
                                        (PVOID)(ULONG_PTR)NotifyValue)))
    {
        return;
    }

    switch (NotifyValue)
    {
        case 0x80:
            AcpiprocHandlePpcNotification(DeviceExtension);
            break;
        case 0x82:
            AcpiprocHandleTpcNotification(DeviceExtension);
            break;
        case 0x90:
            AcpiprocHandleThermalNotification(DeviceExtension, AcpiprocThermalEventHot);
            break;
        case 0x91:
            AcpiprocHandleThermalNotification(DeviceExtension, AcpiprocThermalEventCritical);
            break;

        default:
            break;
    }

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock,
                        (PVOID)(ULONG_PTR)NotifyValue);
}
