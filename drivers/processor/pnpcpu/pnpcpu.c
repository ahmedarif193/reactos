/*
 * PROJECT:     ReactOS ACPI Processor Module Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ACPI0007 topology and processor power capability discovery
 */

#include "pnpcpu.h"

#define NDEBUG
#include <debug.h>

typedef struct _PNPCPU_WORK_CONTEXT
{
    WORK_QUEUE_ITEM WorkItem;
    PPNPCPU_DEVICE_EXTENSION DeviceExtension;
} PNPCPU_WORK_CONTEXT, *PPNPCPU_WORK_CONTEXT;

static
NTSTATUS
NTAPI
PnpcpuCompletion(
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
PnpcpuForwardSynchronously(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, PnpcpuCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
PnpcpuSendAcpiRequest(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ PACPI_EVAL_INPUT_BUFFER InputBuffer,
    _Out_writes_bytes_(OutputLength) PACPI_EVAL_OUTPUT_BUFFER OutputBuffer,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    *Information = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD, DeviceExtension->LowerDevice, InputBuffer, sizeof(*InputBuffer), OutputBuffer, OutputLength, FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    *Information = IoStatus.Information;
    return Status;
}

static
NTSTATUS
PnpcpuEvaluateMethod(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _Outptr_result_bytebuffer_(*OutputLength) PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    ACPI_EVAL_INPUT_BUFFER InputBuffer;
    PACPI_EVAL_OUTPUT_BUFFER Buffer;
    ULONG BufferLength = PAGE_SIZE;
    ULONG_PTR Information;
    NTSTATUS Status;

    *OutputBuffer = NULL;
    *OutputLength = 0;
    RtlZeroMemory(&InputBuffer, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    InputBuffer.MethodNameAsUlong = MethodName;

    for (;;)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, BufferLength, PNPCPU_TAG);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(Buffer, BufferLength);

        Status = PnpcpuSendAcpiRequest(DeviceExtension, &InputBuffer, Buffer, BufferLength, &Information);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;

        ExFreePoolWithTag(Buffer, PNPCPU_TAG);
        if (Information <= BufferLength || Information > PNPCPU_MAX_ACPI_OUTPUT)
            return STATUS_ACPI_INVALID_DATA;
        BufferLength = (ULONG)Information;
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, PNPCPU_TAG);
        return Status;
    }
    if (Buffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Buffer->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) || Buffer->Length > BufferLength)
    {
        ExFreePoolWithTag(Buffer, PNPCPU_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    *OutputBuffer = Buffer;
    *OutputLength = BufferLength;
    return STATUS_SUCCESS;
}

static
BOOLEAN
PnpcpuFirstArgumentValid(
    _In_ PACPI_EVAL_OUTPUT_BUFFER OutputBuffer,
    _In_ ULONG OutputLength)
{
    ULONG HeaderLength = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument);
    ULONG ArgumentLength;

    if (OutputBuffer->Count == 0 || OutputBuffer->Length < HeaderLength || OutputBuffer->Length > OutputLength)
        return FALSE;
    ArgumentLength = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(&OutputBuffer->Argument[0]);
    return ArgumentLength <= OutputBuffer->Length - HeaderLength;
}

static
BOOLEAN
PnpcpuParseUnsignedAscii(
    _In_reads_bytes_(Length) const UCHAR *String,
    _In_ ULONG Length,
    _Out_ PULONG Value)
{
    ULONG Result = 0;
    ULONG Index = 0;
    ULONG Base = 10;
    BOOLEAN FoundDigit = FALSE;

    if (Length >= 2 && String[0] == '0' && (String[1] == 'x' || String[1] == 'X'))
    {
        Base = 16;
        Index = 2;
    }
    for (; Index < Length && String[Index] != ANSI_NULL; Index++)
    {
        ULONG Digit;

        if (String[Index] >= '0' && String[Index] <= '9')
            Digit = String[Index] - '0';
        else if (Base == 16 && String[Index] >= 'a' && String[Index] <= 'f')
            Digit = String[Index] - 'a' + 10;
        else if (Base == 16 && String[Index] >= 'A' && String[Index] <= 'F')
            Digit = String[Index] - 'A' + 10;
        else
            return FALSE;
        if (Result > (MAXULONG - Digit) / Base)
            return FALSE;
        Result = Result * Base + Digit;
        FoundDigit = TRUE;
    }
    if (!FoundDigit)
        return FALSE;
    *Value = Result;
    return TRUE;
}

static
BOOLEAN
PnpcpuQueryInteger(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _Out_ PULONG Value)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    NTSTATUS Status;
    BOOLEAN Valid = FALSE;

    Status = PnpcpuEvaluateMethod(DeviceExtension, MethodName, &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return FALSE;
    if (PnpcpuFirstArgumentValid(OutputBuffer, OutputLength) && OutputBuffer->Argument[0].Type == ACPI_METHOD_ARGUMENT_INTEGER)
    {
        *Value = OutputBuffer->Argument[0].Argument;
        Valid = TRUE;
    }
    else if (PnpcpuFirstArgumentValid(OutputBuffer, OutputLength) && OutputBuffer->Argument[0].Type == ACPI_METHOD_ARGUMENT_STRING)
    {
        Valid = PnpcpuParseUnsignedAscii(OutputBuffer->Argument[0].Data, OutputBuffer->Argument[0].DataLength, Value);
    }
    ExFreePoolWithTag(OutputBuffer, PNPCPU_TAG);
    return Valid;
}

static
VOID
PnpcpuQueryMat(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG OutputLength;
    ULONG Flags;
    ULONG MatUid;
    PUCHAR Data;
    NTSTATUS Status;

    DeviceExtension->ApicIdValid = FALSE;
    Status = PnpcpuEvaluateMethod(DeviceExtension, PNPCPU_METHOD('_', 'M', 'A', 'T'), &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return;
    if (!PnpcpuFirstArgumentValid(OutputBuffer, OutputLength))
        goto Exit;

    Argument = &OutputBuffer->Argument[0];
    if (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER || Argument->DataLength < 2)
        goto Exit;
    Data = Argument->Data;
    if (Data[0] == 0 && Data[1] >= 8 && Argument->DataLength >= 8)
    {
        RtlCopyMemory(&Flags, Data + 4, sizeof(Flags));
        if (Flags & 3)
        {
            DeviceExtension->ApicId = Data[3];
            DeviceExtension->ApicIdValid = TRUE;
            if (!DeviceExtension->UidValid)
            {
                DeviceExtension->Uid = Data[2];
                DeviceExtension->UidValid = TRUE;
            }
        }
    }
    else if (Data[0] == 9 && Data[1] >= 16 && Argument->DataLength >= 16)
    {
        RtlCopyMemory(&Flags, Data + 8, sizeof(Flags));
        if (Flags & 3)
        {
            RtlCopyMemory(&DeviceExtension->ApicId, Data + 4, sizeof(DeviceExtension->ApicId));
            RtlCopyMemory(&MatUid, Data + 12, sizeof(MatUid));
            DeviceExtension->ApicIdValid = TRUE;
            if (!DeviceExtension->UidValid)
            {
                DeviceExtension->Uid = MatUid;
                DeviceExtension->UidValid = TRUE;
            }
        }
    }

Exit:
    ExFreePoolWithTag(OutputBuffer, PNPCPU_TAG);
}

static
BOOLEAN
PnpcpuProbeMethod(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _Out_ PULONG Count)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    NTSTATUS Status;

    *Count = 0;
    Status = PnpcpuEvaluateMethod(DeviceExtension, MethodName, &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return FALSE;
    *Count = OutputBuffer->Count;
    ExFreePoolWithTag(OutputBuffer, PNPCPU_TAG);
    return TRUE;
}

static
VOID
PnpcpuWriteRegistryDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _In_ ULONG Value)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    ZwSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
}

static
VOID
PnpcpuPublishProperties(
    _In_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    HANDLE KeyHandle;
    NTSTATUS Status;

    Status = IoOpenDeviceRegistryKey(DeviceExtension->Pdo, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &KeyHandle);
    if (!NT_SUCCESS(Status))
        return;
    if (DeviceExtension->UidValid)
        PnpcpuWriteRegistryDword(KeyHandle, L"AcpiUid", DeviceExtension->Uid);
    if (DeviceExtension->ApicIdValid)
        PnpcpuWriteRegistryDword(KeyHandle, L"ApicId", DeviceExtension->ApicId);
    if (DeviceExtension->ProximityValid)
        PnpcpuWriteRegistryDword(KeyHandle, L"ProximityDomain", DeviceExtension->ProximityDomain);
    PnpcpuWriteRegistryDword(KeyHandle, L"PowerCapabilities", DeviceExtension->CapabilityMask);
    ZwClose(KeyHandle);
}

static
VOID
PnpcpuRefreshCapabilities(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    static const ULONG Methods[8] =
    {
        PNPCPU_METHOD('_', 'C', 'P', 'C'),
        PNPCPU_METHOD('_', 'C', 'S', 'T'),
        PNPCPU_METHOD('_', 'P', 'S', 'S'),
        PNPCPU_METHOD('_', 'P', 'C', 'T'),
        PNPCPU_METHOD('_', 'P', 'S', 'D'),
        PNPCPU_METHOD('_', 'P', 'P', 'C'),
        PNPCPU_METHOD('_', 'T', 'S', 'S'),
        PNPCPU_METHOD('_', 'T', 'S', 'D')
    };
    ULONG CapabilityMask = 0;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Methods); Index++)
    {
        if (PnpcpuProbeMethod(DeviceExtension, Methods[Index], &DeviceExtension->CapabilityCounts[Index]))
            CapabilityMask |= 1u << Index;
    }
    DeviceExtension->CapabilityMask = CapabilityMask;
    PnpcpuPublishProperties(DeviceExtension);

    DPRINT1("PNPCPU: uid=%s%lu apic=%s%lu pxm=%s%lu active=%lu caps=0x%02lx CPC=%lu CST=%lu PSS=%lu PCT=%lu PSD=%lu PPC=%lu TSS=%lu TSD=%lu\n",
            DeviceExtension->UidValid ? "" : "?", DeviceExtension->Uid,
            DeviceExtension->ApicIdValid ? "" : "?", DeviceExtension->ApicId,
            DeviceExtension->ProximityValid ? "" : "?", DeviceExtension->ProximityDomain,
            KeQueryActiveProcessorCountEx(ALL_PROCESSOR_GROUPS), DeviceExtension->CapabilityMask,
            DeviceExtension->CapabilityCounts[0], DeviceExtension->CapabilityCounts[1],
            DeviceExtension->CapabilityCounts[2], DeviceExtension->CapabilityCounts[3],
            DeviceExtension->CapabilityCounts[4], DeviceExtension->CapabilityCounts[5],
            DeviceExtension->CapabilityCounts[6], DeviceExtension->CapabilityCounts[7]);
}

static
VOID
PnpcpuWorker(
    _In_ PVOID Context)
{
    PPNPCPU_WORK_CONTEXT WorkContext = Context;
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = WorkContext->DeviceExtension;

    if (DeviceExtension->Started && !DeviceExtension->Removing)
        PnpcpuRefreshCapabilities(DeviceExtension);
    if (InterlockedDecrement(&DeviceExtension->WorkCount) == 0)
        KeSetEvent(&DeviceExtension->WorkIdleEvent, IO_NO_INCREMENT, FALSE);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, DeviceExtension);
    ExFreePoolWithTag(WorkContext, PNPCPU_TAG);
}

static
VOID
NTAPI
PnpcpuNotification(
    _In_ PVOID Context,
    _In_ ULONG NotifyCode)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = Context;
    PPNPCPU_WORK_CONTEXT WorkContext;

    if (NotifyCode != 0x80 && NotifyCode != 0x81 && NotifyCode != 0x82)
        return;
    if (!NT_SUCCESS(IoAcquireRemoveLock(&DeviceExtension->RemoveLock, DeviceExtension)))
        return;
    WorkContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*WorkContext), PNPCPU_TAG);
    if (!WorkContext)
    {
        IoReleaseRemoveLock(&DeviceExtension->RemoveLock, DeviceExtension);
        return;
    }
    WorkContext->DeviceExtension = DeviceExtension;
    ExInitializeWorkItem(&WorkContext->WorkItem, PnpcpuWorker, WorkContext);
    if (InterlockedIncrement(&DeviceExtension->WorkCount) == 1)
        KeClearEvent(&DeviceExtension->WorkIdleEvent);
    DPRINT1("PNPCPU: ACPI notification 0x%02lx; refreshing processor capabilities\n", NotifyCode);
    ExQueueWorkItem(&WorkContext->WorkItem, DelayedWorkQueue);
}

static
NTSTATUS
PnpcpuQueryAcpiInterface(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(&DeviceExtension->AcpiInterface, sizeof(DeviceExtension->AcpiInterface));
    DeviceExtension->AcpiInterface.Size = sizeof(DeviceExtension->AcpiInterface);
    DeviceExtension->AcpiInterface.Version = 1;
    Irp = IoAllocateIrp(DeviceExtension->LowerDevice->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_ACPI_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size = sizeof(DeviceExtension->AcpiInterface);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)&DeviceExtension->AcpiInterface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    IoSetCompletionRoutine(Irp, PnpcpuCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    IoFreeIrp(Irp);
    if (NT_SUCCESS(Status))
        DeviceExtension->InterfaceAcquired = TRUE;
    return Status;
}

static
VOID
PnpcpuReleaseAcpiInterface(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->NotificationsRegistered)
    {
        DeviceExtension->AcpiInterface.UnregisterForDeviceNotifications(DeviceExtension->AcpiInterface.Context, PnpcpuNotification);
        DeviceExtension->NotificationsRegistered = FALSE;
    }
    if (DeviceExtension->InterfaceAcquired)
    {
        DeviceExtension->AcpiInterface.InterfaceDereference(DeviceExtension->AcpiInterface.Context);
        DeviceExtension->InterfaceAcquired = FALSE;
    }
}

static
NTSTATUS
PnpcpuStartDevice(
    _Inout_ PPNPCPU_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    DeviceExtension->UidValid = PnpcpuQueryInteger(DeviceExtension, PNPCPU_METHOD('_', 'U', 'I', 'D'), &DeviceExtension->Uid);
    DeviceExtension->ProximityValid = PnpcpuQueryInteger(DeviceExtension, PNPCPU_METHOD('_', 'P', 'X', 'M'), &DeviceExtension->ProximityDomain);
    PnpcpuQueryMat(DeviceExtension);
    PnpcpuRefreshCapabilities(DeviceExtension);

    Status = PnpcpuQueryAcpiInterface(DeviceExtension);
    if (NT_SUCCESS(Status))
    {
        Status = DeviceExtension->AcpiInterface.RegisterForDeviceNotifications(DeviceExtension->AcpiInterface.Context, PnpcpuNotification, DeviceExtension);
        if (NT_SUCCESS(Status))
            DeviceExtension->NotificationsRegistered = TRUE;
        else
            DPRINT1("PNPCPU: notification registration failed, status 0x%08lx\n", Status);
    }
    else
    {
        DPRINT1("PNPCPU: ACPI interface query failed, status 0x%08lx\n", Status);
    }

    DeviceExtension->Started = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
PnpcpuPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = PnpcpuForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = PnpcpuStartDevice(DeviceExtension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
            DeviceExtension->Started = FALSE;
            PnpcpuReleaseAcpiInterface(DeviceExtension);
            KeWaitForSingleObject(&DeviceExtension->WorkIdleEvent, Executive, KernelMode, FALSE, NULL);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            DeviceExtension->Started = FALSE;
            DeviceExtension->Removing = TRUE;
            PnpcpuReleaseAcpiInterface(DeviceExtension);
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
            DeviceExtension->Removing = TRUE;
            PnpcpuReleaseAcpiInterface(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = PnpcpuForwardSynchronously(DeviceExtension, Irp);
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
PnpcpuPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
PnpcpuAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PPNPCPU_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*DeviceExtension), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->Self = DeviceObject;
    DeviceExtension->Pdo = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, PNPCPU_TAG, 0, 0);
    KeInitializeEvent(&DeviceExtension->WorkIdleEvent, NotificationEvent, TRUE);
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
PnpcpuUnload(
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
    DriverObject->MajorFunction[IRP_MJ_PNP] = PnpcpuPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = PnpcpuPower;
    DriverObject->DriverExtension->AddDevice = PnpcpuAddDevice;
    DriverObject->DriverUnload = PnpcpuUnload;
    return STATUS_SUCCESS;
}
