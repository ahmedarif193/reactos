/*
 * PROJECT:     ReactOS Intel Overclocking Watchdog Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Safe ownership of the Alder Lake INTC1099 watchdog
 */

#include <ntddk.h>

#define NDEBUG
#include <debug.h>

#define INTELWDT_TAG 'dWtI'
#define INTELWDT_TOV 0x000003FF
#define INTELWDT_CTL_LCK 0x00001000
#define INTELWDT_EN 0x00004000
#define INTELWDT_NO_ICCSURV_STS 0x01000000
#define INTELWDT_ICCSURV_STS 0x02000000
#define INTELWDT_RLD 0x80000000

typedef struct _INTELWDT_DEVICE_EXTENSION
{
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    PULONG ControlPort;
    ULONG TimeoutSeconds;
    BOOLEAN Started;
    BOOLEAN Locked;
    BOOLEAN HardwareRunning;
    BOOLEAN TimerArmed;
    KTIMER KeepaliveTimer;
    KDPC KeepaliveDpc;
} INTELWDT_DEVICE_EXTENSION, *PINTELWDT_DEVICE_EXTENSION;

static
NTSTATUS
NTAPI
IntelWdtCompletion(
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
IntelWdtForwardSynchronously(
    _In_ PINTELWDT_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, IntelWdtCompletion, &Event, TRUE, TRUE, TRUE);
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
IntelWdtPing(
    _In_ PINTELWDT_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Control;

    Control = READ_PORT_ULONG(DeviceExtension->ControlPort);
    WRITE_PORT_ULONG(DeviceExtension->ControlPort, Control | INTELWDT_RLD);
}

static
VOID
NTAPI
IntelWdtKeepaliveDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PINTELWDT_DEVICE_EXTENSION DeviceExtension = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    if (DeviceExtension->TimerArmed && DeviceExtension->HardwareRunning)
        IntelWdtPing(DeviceExtension);
}

static
VOID
IntelWdtStopKeepalive(
    _Inout_ PINTELWDT_DEVICE_EXTENSION DeviceExtension)
{
    DeviceExtension->TimerArmed = FALSE;
    KeCancelTimer(&DeviceExtension->KeepaliveTimer);
    KeRemoveQueueDpc(&DeviceExtension->KeepaliveDpc);
    KeFlushQueuedDpcs();
}

static
NTSTATUS
IntelWdtStartHardware(
    _Inout_ PINTELWDT_DEVICE_EXTENSION DeviceExtension,
    _In_ PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    LARGE_INTEGER DueTime;
    ULONG Control;
    ULONG Index;
    LONG Period;

    if (!Resources || Resources->Count == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    PartialList = &Resources->List[0].PartialResourceList;
    for (Index = 0; Index < PartialList->Count; Index++)
    {
        Descriptor = &PartialList->PartialDescriptors[Index];
        if (Descriptor->Type == CmResourceTypePort && Descriptor->u.Port.Length >= sizeof(ULONG))
        {
            DeviceExtension->ControlPort = (PULONG)(ULONG_PTR)Descriptor->u.Port.Start.QuadPart;
            break;
        }
    }
    if (!DeviceExtension->ControlPort)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Control = READ_PORT_ULONG(DeviceExtension->ControlPort);
    DeviceExtension->TimeoutSeconds = (Control & INTELWDT_TOV) + 1;
    DeviceExtension->Locked = (Control & INTELWDT_CTL_LCK) != 0;
    DeviceExtension->HardwareRunning = (Control & INTELWDT_EN) != 0;
    if (Control & (INTELWDT_NO_ICCSURV_STS | INTELWDT_ICCSURV_STS))
        DPRINT1("INTELOCWDT: watchdog reset status is set, control=0x%08lx\n", Control);

    if (DeviceExtension->HardwareRunning && !DeviceExtension->Locked)
    {
        WRITE_PORT_ULONG(DeviceExtension->ControlPort, Control & ~INTELWDT_EN);
        Control = READ_PORT_ULONG(DeviceExtension->ControlPort);
        DeviceExtension->HardwareRunning = (Control & INTELWDT_EN) != 0;
    }
    if (DeviceExtension->HardwareRunning)
    {
        IntelWdtPing(DeviceExtension);
        Period = max(1000L, (LONG)DeviceExtension->TimeoutSeconds * 500L);
        DueTime.QuadPart = -(LONGLONG)Period * 10000;
        DeviceExtension->TimerArmed = TRUE;
        KeSetTimerEx(&DeviceExtension->KeepaliveTimer, DueTime, Period, &DeviceExtension->KeepaliveDpc);
    }
    DeviceExtension->Started = TRUE;
    DPRINT1("INTELOCWDT: port=%p running=%u locked=%u timeout=%lu seconds\n", DeviceExtension->ControlPort, DeviceExtension->HardwareRunning, DeviceExtension->Locked, DeviceExtension->TimeoutSeconds);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IntelWdtPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELWDT_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = IntelWdtForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = IntelWdtStartHardware(DeviceExtension, Stack->Parameters.StartDevice.AllocatedResourcesTranslated);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
            DeviceExtension->Started = FALSE;
            IntelWdtStopKeepalive(DeviceExtension);
            DeviceExtension->ControlPort = NULL;
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            DeviceExtension->Started = FALSE;
            IntelWdtStopKeepalive(DeviceExtension);
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (!NT_SUCCESS(Status))
            {
                Irp->IoStatus.Status = Status;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return Status;
            }
            DeviceExtension->Started = FALSE;
            IntelWdtStopKeepalive(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = IntelWdtForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
IntelWdtPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PINTELWDT_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
IntelWdtAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PINTELWDT_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*DeviceExtension), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, INTELWDT_TAG, 0, 0);
    KeInitializeTimerEx(&DeviceExtension->KeepaliveTimer, NotificationTimer);
    KeInitializeDpc(&DeviceExtension->KeepaliveDpc, IntelWdtKeepaliveDpc, DeviceExtension);
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
IntelWdtUnload(
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
    DriverObject->MajorFunction[IRP_MJ_PNP] = IntelWdtPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = IntelWdtPower;
    DriverObject->DriverExtension->AddDevice = IntelWdtAddDevice;
    DriverObject->DriverUnload = IntelWdtUnload;
    return STATUS_SUCCESS;
}
