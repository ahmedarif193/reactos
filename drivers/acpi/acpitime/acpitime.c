/*
 * PROJECT:     ReactOS ACPI Time and Alarm Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ACPI000E real-time and persistent wake-alarm support
 */

#include "acpitime.h"

#define NDEBUG
#include <debug.h>

typedef struct _ACPITIME_WORK_CONTEXT
{
    WORK_QUEUE_ITEM WorkItem;
    PACPITIME_DEVICE_EXTENSION DeviceExtension;
} ACPITIME_WORK_CONTEXT, *PACPITIME_WORK_CONTEXT;

static
NTSTATUS
NTAPI
AcpiTimeCompletion(
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
AcpiTimeForwardSynchronously(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, AcpiTimeCompletion, &Event, TRUE, TRUE, TRUE);
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
AcpiTimeSendRequest(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
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
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD, DeviceExtension->LowerDevice, InputBuffer, InputLength, OutputBuffer, OutputLength, FALSE, &Event, &IoStatus);
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
AcpiTimeEvaluate(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Outptr_result_bytebuffer_(*OutputLength) PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    PACPI_EVAL_OUTPUT_BUFFER Buffer;
    ULONG BufferLength = PAGE_SIZE;
    ULONG_PTR Information;
    NTSTATUS Status;

    *OutputBuffer = NULL;
    *OutputLength = 0;
    for (;;)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, BufferLength, ACPITIME_TAG);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(Buffer, BufferLength);
        Status = AcpiTimeSendRequest(DeviceExtension, InputBuffer, InputLength, Buffer, BufferLength, &Information);
        if (Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_BUFFER_TOO_SMALL)
            break;
        ExFreePoolWithTag(Buffer, ACPITIME_TAG);
        if (Information <= BufferLength || Information > ACPITIME_MAX_OUTPUT)
            return STATUS_ACPI_INVALID_DATA;
        BufferLength = (ULONG)Information;
    }
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, ACPITIME_TAG);
        return Status;
    }
    if (Buffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Buffer->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) || Buffer->Length > BufferLength)
    {
        ExFreePoolWithTag(Buffer, ACPITIME_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }
    *OutputBuffer = Buffer;
    *OutputLength = BufferLength;
    return STATUS_SUCCESS;
}

static
BOOLEAN
AcpiTimeFirstArgumentValid(
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
NTSTATUS
AcpiTimeEvaluateNoArguments(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _Outptr_result_bytebuffer_(*OutputLength) PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    ACPI_EVAL_INPUT_BUFFER InputBuffer;

    RtlZeroMemory(&InputBuffer, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    InputBuffer.MethodNameAsUlong = MethodName;
    return AcpiTimeEvaluate(DeviceExtension, &InputBuffer, sizeof(InputBuffer), OutputBuffer, OutputLength);
}

static
NTSTATUS
AcpiTimeEvaluateIntegerArgument(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _In_ ULONG Argument,
    _Outptr_result_bytebuffer_(*OutputLength) PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    ACPI_EVAL_INPUT_BUFFER_SIMPLE_INTEGER InputBuffer;

    RtlZeroMemory(&InputBuffer, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIMPLE_INTEGER_SIGNATURE;
    InputBuffer.MethodNameAsUlong = MethodName;
    InputBuffer.IntegerArgument = Argument;
    return AcpiTimeEvaluate(DeviceExtension, &InputBuffer, sizeof(InputBuffer), OutputBuffer, OutputLength);
}

static
BOOLEAN
AcpiTimeQueryInteger(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _Out_ PULONG Value)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    BOOLEAN Valid = FALSE;
    NTSTATUS Status;

    Status = AcpiTimeEvaluateNoArguments(DeviceExtension, MethodName, &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return FALSE;
    if (AcpiTimeFirstArgumentValid(OutputBuffer, OutputLength) && OutputBuffer->Argument[0].Type == ACPI_METHOD_ARGUMENT_INTEGER)
    {
        *Value = OutputBuffer->Argument[0].Argument;
        Valid = TRUE;
    }
    ExFreePoolWithTag(OutputBuffer, ACPITIME_TAG);
    return Valid;
}

static
BOOLEAN
AcpiTimeQueryIntegerArgument(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _In_ ULONG Argument,
    _Out_ PULONG Value)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    BOOLEAN Valid = FALSE;
    NTSTATUS Status;

    Status = AcpiTimeEvaluateIntegerArgument(DeviceExtension, MethodName, Argument, &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return FALSE;
    if (AcpiTimeFirstArgumentValid(OutputBuffer, OutputLength) && OutputBuffer->Argument[0].Type == ACPI_METHOD_ARGUMENT_INTEGER)
    {
        *Value = OutputBuffer->Argument[0].Argument;
        Valid = TRUE;
    }
    ExFreePoolWithTag(OutputBuffer, ACPITIME_TAG);
    return Valid;
}

static
BOOLEAN
AcpiTimeClearWakeStatus(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Timer)
{
    ULONG Result;

    return AcpiTimeQueryIntegerArgument(DeviceExtension, ACPITIME_METHOD('_', 'C', 'W', 'S'), Timer, &Result) && Result == 0;
}

static
BOOLEAN
AcpiTimeReadClock(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _Out_ PACPITIME_GRT_INFO Time)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG OutputLength;
    BOOLEAN Valid = FALSE;
    NTSTATUS Status;

    Status = AcpiTimeEvaluateNoArguments(DeviceExtension, ACPITIME_METHOD('_', 'G', 'R', 'T'), &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return FALSE;
    if (!AcpiTimeFirstArgumentValid(OutputBuffer, OutputLength))
        goto Exit;
    Argument = &OutputBuffer->Argument[0];
    if (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER || Argument->DataLength < sizeof(*Time))
        goto Exit;
    RtlCopyMemory(Time, Argument->Data, sizeof(*Time));
    Valid = Time->Valid == 1 && Time->Year >= 1900 && Time->Year <= 9999 && Time->Month >= 1 && Time->Month <= 12 && Time->Day >= 1 && Time->Day <= 31 && Time->Hour <= 23 && Time->Minute <= 59 && Time->Second <= 59 && Time->Milliseconds <= 1000 && (Time->Timezone == 2047 || (Time->Timezone >= -1440 && Time->Timezone <= 1440));
Exit:
    ExFreePoolWithTag(OutputBuffer, ACPITIME_TAG);
    return Valid;
}

static
VOID
AcpiTimeWriteDword(
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
AcpiTimeRefresh(
    _Inout_ PACPITIME_DEVICE_EXTENSION DeviceExtension)
{
    ACPITIME_GRT_INFO Time;
    BOOLEAN TimeValid = FALSE;
    ULONG WakeStatus[2] = {0, 0};
    ULONG TimerValue[2] = {MAXULONG, MAXULONG};
    ULONG TimerPolicy[2] = {MAXULONG, MAXULONG};
    HANDLE KeyHandle;
    ULONG TimerCount;
    ULONG Timer;
    NTSTATUS Status;

    if (!AcpiTimeQueryInteger(DeviceExtension, ACPITIME_METHOD('_', 'G', 'C', 'P'), &DeviceExtension->Capabilities))
    {
        DPRINT1("ACPITIME: required _GCP method failed\n");
        return;
    }
    DeviceExtension->Capabilities &= ACPITIME_CAP_MASK;
    if (DeviceExtension->Capabilities & ACPITIME_CAP_REAL_TIME)
        TimeValid = AcpiTimeReadClock(DeviceExtension, &Time);
    TimerCount = (DeviceExtension->Capabilities & ACPITIME_CAP_DC_WAKE) ? 2 : ((DeviceExtension->Capabilities & ACPITIME_CAP_AC_WAKE) ? 1 : 0);
    for (Timer = 0; Timer < TimerCount; Timer++)
    {
        AcpiTimeQueryIntegerArgument(DeviceExtension, ACPITIME_METHOD('_', 'G', 'W', 'S'), Timer, &WakeStatus[Timer]);
        AcpiTimeQueryIntegerArgument(DeviceExtension, ACPITIME_METHOD('_', 'T', 'I', 'V'), Timer, &TimerValue[Timer]);
        AcpiTimeQueryIntegerArgument(DeviceExtension, ACPITIME_METHOD('_', 'T', 'I', 'P'), Timer, &TimerPolicy[Timer]);
        if ((WakeStatus[Timer] & 1) && !AcpiTimeClearWakeStatus(DeviceExtension, Timer))
            DPRINT1("ACPITIME: failed to clear persistent wake status for timer %lu\n", Timer);
    }
    Status = IoOpenDeviceRegistryKey(DeviceExtension->PhysicalDevice, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &KeyHandle);
    if (NT_SUCCESS(Status))
    {
        AcpiTimeWriteDword(KeyHandle, L"Capabilities", DeviceExtension->Capabilities);
        AcpiTimeWriteDword(KeyHandle, L"TimeValid", TimeValid);
        AcpiTimeWriteDword(KeyHandle, L"AcWakeStatus", WakeStatus[0]);
        AcpiTimeWriteDword(KeyHandle, L"AcTimerValue", TimerValue[0]);
        AcpiTimeWriteDword(KeyHandle, L"AcTimerPolicy", TimerPolicy[0]);
        if (TimerCount == 2)
        {
            AcpiTimeWriteDword(KeyHandle, L"DcWakeStatus", WakeStatus[1]);
            AcpiTimeWriteDword(KeyHandle, L"DcTimerValue", TimerValue[1]);
            AcpiTimeWriteDword(KeyHandle, L"DcTimerPolicy", TimerPolicy[1]);
        }
        if (TimeValid)
        {
            AcpiTimeWriteDword(KeyHandle, L"Year", Time.Year);
            AcpiTimeWriteDword(KeyHandle, L"Month", Time.Month);
            AcpiTimeWriteDword(KeyHandle, L"Day", Time.Day);
            AcpiTimeWriteDword(KeyHandle, L"Hour", Time.Hour);
            AcpiTimeWriteDword(KeyHandle, L"Minute", Time.Minute);
            AcpiTimeWriteDword(KeyHandle, L"Second", Time.Second);
        }
        ZwClose(KeyHandle);
    }
    DPRINT1("ACPITIME: caps=0x%03lx time=%s AC(status=0x%lx value=%lu policy=%lu) DC(status=0x%lx value=%lu policy=%lu)\n",
            DeviceExtension->Capabilities, TimeValid ? "valid" : "unavailable",
            WakeStatus[0], TimerValue[0], TimerPolicy[0], WakeStatus[1], TimerValue[1], TimerPolicy[1]);
}

static
VOID
AcpiTimeWorker(
    _In_ PVOID Context)
{
    PACPITIME_WORK_CONTEXT WorkContext = Context;
    PACPITIME_DEVICE_EXTENSION DeviceExtension = WorkContext->DeviceExtension;

    if (DeviceExtension->Started && !DeviceExtension->Removing)
        AcpiTimeRefresh(DeviceExtension);
    if (InterlockedDecrement(&DeviceExtension->WorkCount) == 0)
        KeSetEvent(&DeviceExtension->WorkIdleEvent, IO_NO_INCREMENT, FALSE);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, DeviceExtension);
    ExFreePoolWithTag(WorkContext, ACPITIME_TAG);
}

static
VOID
NTAPI
AcpiTimeNotification(
    _In_ PVOID Context,
    _In_ ULONG NotifyCode)
{
    PACPITIME_DEVICE_EXTENSION DeviceExtension = Context;
    PACPITIME_WORK_CONTEXT WorkContext;

    if (NotifyCode != 0x02 && NotifyCode != 0x80)
        return;
    if (!NT_SUCCESS(IoAcquireRemoveLock(&DeviceExtension->RemoveLock, DeviceExtension)))
        return;
    WorkContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*WorkContext), ACPITIME_TAG);
    if (!WorkContext)
    {
        IoReleaseRemoveLock(&DeviceExtension->RemoveLock, DeviceExtension);
        return;
    }
    WorkContext->DeviceExtension = DeviceExtension;
    ExInitializeWorkItem(&WorkContext->WorkItem, AcpiTimeWorker, WorkContext);
    if (InterlockedIncrement(&DeviceExtension->WorkCount) == 1)
        KeClearEvent(&DeviceExtension->WorkIdleEvent);
    DPRINT1("ACPITIME: ACPI notification 0x%02lx; refreshing alarm state\n", NotifyCode);
    ExQueueWorkItem(&WorkContext->WorkItem, DelayedWorkQueue);
}

static
NTSTATUS
AcpiTimeQueryInterface(
    _Inout_ PACPITIME_DEVICE_EXTENSION DeviceExtension)
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
    IoSetCompletionRoutine(Irp, AcpiTimeCompletion, &Event, TRUE, TRUE, TRUE);
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
AcpiTimeReleaseInterface(
    _Inout_ PACPITIME_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->NotificationsRegistered)
    {
        DeviceExtension->AcpiInterface.UnregisterForDeviceNotifications(DeviceExtension->AcpiInterface.Context, AcpiTimeNotification);
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
AcpiTimeStart(
    _Inout_ PACPITIME_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    AcpiTimeRefresh(DeviceExtension);
    Status = AcpiTimeQueryInterface(DeviceExtension);
    if (NT_SUCCESS(Status))
    {
        Status = DeviceExtension->AcpiInterface.RegisterForDeviceNotifications(DeviceExtension->AcpiInterface.Context, AcpiTimeNotification, DeviceExtension);
        if (NT_SUCCESS(Status))
            DeviceExtension->NotificationsRegistered = TRUE;
        else
            DPRINT1("ACPITIME: notification registration failed, status 0x%08lx\n", Status);
    }
    else
    {
        DPRINT1("ACPITIME: ACPI interface query failed, status 0x%08lx\n", Status);
    }
    DeviceExtension->Started = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
AcpiTimePnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPITIME_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = AcpiTimeForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = AcpiTimeStart(DeviceExtension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
            DeviceExtension->Started = FALSE;
            AcpiTimeReleaseInterface(DeviceExtension);
            KeWaitForSingleObject(&DeviceExtension->WorkIdleEvent, Executive, KernelMode, FALSE, NULL);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            DeviceExtension->Started = FALSE;
            DeviceExtension->Removing = TRUE;
            AcpiTimeReleaseInterface(DeviceExtension);
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
            AcpiTimeReleaseInterface(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = AcpiTimeForwardSynchronously(DeviceExtension, Irp);
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
AcpiTimePower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPITIME_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AcpiTimeAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PACPITIME_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*DeviceExtension), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, ACPITIME_TAG, 0, 0);
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
AcpiTimeUnload(
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
    DriverObject->MajorFunction[IRP_MJ_PNP] = AcpiTimePnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AcpiTimePower;
    DriverObject->DriverExtension->AddDevice = AcpiTimeAddDevice;
    DriverObject->DriverUnload = AcpiTimeUnload;
    return STATUS_SUCCESS;
}
