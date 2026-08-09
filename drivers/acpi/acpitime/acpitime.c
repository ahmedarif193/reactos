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

C_ASSERT(sizeof(ACPITIME_TIME_INFORMATION) == 16);

static BOOLEAN AcpiTimeValidateClock(_In_ PACPITIME_TIME_INFORMATION Time, _In_ BOOLEAN RequireValid);

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

    if (OutputBuffer->Count == 0 || OutputBuffer->Length < HeaderLength + ACPI_METHOD_ARGUMENT_LENGTH(0) || OutputBuffer->Length > OutputLength)
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
NTSTATUS
AcpiTimeEvaluateTwoIntegerArguments(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _In_ ULONG FirstValue,
    _In_ ULONG SecondValue,
    _Outptr_result_bytebuffer_(*OutputLength) PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    ULONG InputStorage[(FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) + 2 * ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) + sizeof(ULONG) - 1) / sizeof(ULONG)];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX InputBuffer = (PVOID)InputStorage;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG InputLength = sizeof(InputStorage);

    RtlZeroMemory(InputStorage, sizeof(InputStorage));
    InputBuffer->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    InputBuffer->MethodNameAsUlong = MethodName;
    InputBuffer->Size = InputLength - FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    InputBuffer->ArgumentCount = 2;
    Argument = InputBuffer->Argument;
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, FirstValue);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, SecondValue);
    return AcpiTimeEvaluate(DeviceExtension, InputBuffer, InputLength, OutputBuffer, OutputLength);
}

static
NTSTATUS
AcpiTimeEvaluateBufferArgument(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _In_reads_bytes_(BufferLength) PVOID Buffer,
    _In_ USHORT BufferLength,
    _Outptr_result_bytebuffer_(*OutputLength) PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    ULONG InputStorage[(FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) + ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ACPITIME_TIME_INFORMATION)) + sizeof(ULONG) - 1) / sizeof(ULONG)];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX InputBuffer = (PVOID)InputStorage;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG InputLength;

    if (BufferLength > sizeof(ACPITIME_TIME_INFORMATION))
        return STATUS_INVALID_BUFFER_SIZE;
    InputLength = FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) + ACPI_METHOD_ARGUMENT_LENGTH(BufferLength);
    RtlZeroMemory(InputStorage, sizeof(InputStorage));
    InputBuffer->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    InputBuffer->MethodNameAsUlong = MethodName;
    InputBuffer->Size = InputLength - FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    InputBuffer->ArgumentCount = 1;
    Argument = InputBuffer->Argument;
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument, Buffer, BufferLength);
    return AcpiTimeEvaluate(DeviceExtension, InputBuffer, InputLength, OutputBuffer, OutputLength);
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
AcpiTimeQueryTwoIntegerArguments(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _In_ ULONG FirstValue,
    _In_ ULONG SecondValue,
    _Out_ PULONG Value)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    BOOLEAN Valid = FALSE;
    NTSTATUS Status;

    Status = AcpiTimeEvaluateTwoIntegerArguments(DeviceExtension, MethodName, FirstValue, SecondValue, &OutputBuffer, &OutputLength);
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
AcpiTimeQueryBufferArgument(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _In_reads_bytes_(BufferLength) PVOID Buffer,
    _In_ USHORT BufferLength,
    _Out_ PULONG Value)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    BOOLEAN Valid = FALSE;
    NTSTATUS Status;

    Status = AcpiTimeEvaluateBufferArgument(DeviceExtension, MethodName, Buffer, BufferLength, &OutputBuffer, &OutputLength);
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
AcpiTimeTimerSupported(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Timer)
{
    if (Timer == ACPITIME_TIMER_AC)
        return (DeviceExtension->Capabilities & ACPITIME_CAP_AC_WAKE) != 0;
    if (Timer == ACPITIME_TIMER_DC)
        return (DeviceExtension->Capabilities & ACPITIME_CAP_DC_WAKE) != 0;
    return FALSE;
}

static
BOOLEAN
AcpiTimeReadTimer(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PACPITIME_TIMER_INFORMATION TimerInformation)
{
    ULONG Timer = TimerInformation->Timer;

    if (!AcpiTimeTimerSupported(DeviceExtension, Timer))
        return FALSE;
    if (!AcpiTimeQueryIntegerArgument(DeviceExtension, ACPITIME_METHOD('_', 'G', 'W', 'S'), Timer, &TimerInformation->Status))
        return FALSE;
    if (!AcpiTimeQueryIntegerArgument(DeviceExtension, ACPITIME_METHOD('_', 'T', 'I', 'V'), Timer, &TimerInformation->Value))
        return FALSE;
    return AcpiTimeQueryIntegerArgument(DeviceExtension, ACPITIME_METHOD('_', 'T', 'I', 'P'), Timer, &TimerInformation->Policy);
}

static
BOOLEAN
AcpiTimeWriteTimer(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ PACPITIME_TIMER_SET TimerSet)
{
    ACPITIME_TIMER_INFORMATION Previous;
    ULONG Result;

    Previous.Timer = TimerSet->Timer;
    if (!AcpiTimeTimerSupported(DeviceExtension, TimerSet->Timer) || !AcpiTimeReadTimer(DeviceExtension, &Previous))
        return FALSE;
    if (!AcpiTimeQueryTwoIntegerArguments(DeviceExtension, ACPITIME_METHOD('_', 'S', 'T', 'P'), TimerSet->Timer, TimerSet->Policy, &Result) || Result != 0)
        return FALSE;
    if (AcpiTimeQueryTwoIntegerArguments(DeviceExtension, ACPITIME_METHOD('_', 'S', 'T', 'V'), TimerSet->Timer, TimerSet->Value, &Result) && Result == 0)
    {
        DPRINT1("ACPITIME: programmed %s timer value=%lu policy=%lu\n",
                TimerSet->Timer == ACPITIME_TIMER_AC ? "AC" : "DC", TimerSet->Value, TimerSet->Policy);
        return TRUE;
    }
    AcpiTimeQueryTwoIntegerArguments(DeviceExtension, ACPITIME_METHOD('_', 'S', 'T', 'P'), Previous.Timer, Previous.Policy, &Result);
    AcpiTimeQueryTwoIntegerArguments(DeviceExtension, ACPITIME_METHOD('_', 'S', 'T', 'V'), Previous.Timer, Previous.Value, &Result);
    return FALSE;
}

static BOOLEAN AcpiTimeReadClock(_In_ PACPITIME_DEVICE_EXTENSION DeviceExtension, _Out_ PACPITIME_TIME_INFORMATION Time, _Out_ PNTSTATUS MethodStatus)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG OutputLength;
    BOOLEAN Valid = FALSE;
    NTSTATUS Status;

    Status = AcpiTimeEvaluateNoArguments(DeviceExtension, ACPITIME_METHOD('_', 'G', 'R', 'T'), &OutputBuffer, &OutputLength);
    *MethodStatus = Status;
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPITIME: _GRT evaluation failed, status 0x%08lx\n", Status);
        return FALSE;
    }
    if (!AcpiTimeFirstArgumentValid(OutputBuffer, OutputLength))
    {
        DPRINT1("ACPITIME: _GRT returned invalid envelope count=%lu length=%lu allocation=%lu\n", OutputBuffer->Count, OutputBuffer->Length, OutputLength);
        *MethodStatus = STATUS_ACPI_INVALID_DATA;
        goto Exit;
    }
    Argument = &OutputBuffer->Argument[0];
    if (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER || Argument->DataLength < sizeof(*Time))
    {
        DPRINT1("ACPITIME: _GRT returned type=%u length=%u, expected a %lu-byte buffer\n", Argument->Type, Argument->DataLength, (ULONG)sizeof(*Time));
        *MethodStatus = STATUS_ACPI_INVALID_DATA;
        goto Exit;
    }
    RtlCopyMemory(Time, Argument->Data, sizeof(*Time));
    Valid = AcpiTimeValidateClock(Time, TRUE);
    *MethodStatus = Valid ? STATUS_SUCCESS : STATUS_DEVICE_DATA_ERROR;
    DPRINT1("ACPITIME: _GRT raw %04u-%02u-%02u %02u:%02u:%02u.%03u valid=%u timezone=%d daylight=%u reserved=%u/%u/%u accepted=%u\n", Time->Year, Time->Month, Time->Day, Time->Hour, Time->Minute, Time->Second, Time->Milliseconds, Time->Valid, Time->Timezone, Time->Daylight, Time->Reserved[0], Time->Reserved[1], Time->Reserved[2], Valid);
Exit:
    ExFreePoolWithTag(OutputBuffer, ACPITIME_TAG);
    return Valid;
}

static
BOOLEAN
AcpiTimeValidateClock(
    _In_ PACPITIME_TIME_INFORMATION Time,
    _In_ BOOLEAN RequireValid)
{
    static const UCHAR DaysPerMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    ULONG MaximumDay;
    BOOLEAN LeapYear;

    if ((RequireValid && Time->Valid != 1) || Time->Year < 1900 || Time->Year > 9999 || Time->Month < 1 || Time->Month > 12)
        return FALSE;
    LeapYear = ((Time->Year % 4) == 0 && (Time->Year % 100) != 0) || (Time->Year % 400) == 0;
    MaximumDay = DaysPerMonth[Time->Month - 1] + (Time->Month == 2 && LeapYear);
    if (Time->Day < 1 || Time->Day > MaximumDay || Time->Hour > 23 || Time->Minute > 59 || Time->Second > 59 || Time->Milliseconds > 1000)
        return FALSE;
    if (Time->Timezone != 2047 && (Time->Timezone < -1440 || Time->Timezone > 1440))
        return FALSE;
    return (Time->Daylight & ~3) == 0 && Time->Reserved[0] == 0 && Time->Reserved[1] == 0 && Time->Reserved[2] == 0;
}

static
BOOLEAN
AcpiTimeWriteClock(
    _In_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ PACPITIME_TIME_INFORMATION Time)
{
    ACPITIME_TIME_INFORMATION FirmwareTime;
    ULONG Result;

    if (!(DeviceExtension->Capabilities & ACPITIME_CAP_REAL_TIME) || !AcpiTimeValidateClock(Time, TRUE))
        return FALSE;
    FirmwareTime = *Time;
    FirmwareTime.Valid = 0;
    RtlZeroMemory(FirmwareTime.Reserved, sizeof(FirmwareTime.Reserved));
    if (!AcpiTimeQueryBufferArgument(DeviceExtension, ACPITIME_METHOD('_', 'S', 'R', 'T'), &FirmwareTime, sizeof(FirmwareTime), &Result) || Result != 0)
        return FALSE;
    DPRINT1("ACPITIME: firmware real time updated to %04u-%02u-%02u %02u:%02u:%02u\n",
            Time->Year, Time->Month, Time->Day, Time->Hour, Time->Minute, Time->Second);
    return TRUE;
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
BOOLEAN
AcpiTimeRefresh(
    _Inout_ PACPITIME_DEVICE_EXTENSION DeviceExtension)
{
    ACPITIME_TIME_INFORMATION Time;
    BOOLEAN TimeValid = FALSE;
    ULONG WakeStatus[2] = {0, 0};
    ULONG TimerValue[2] = {MAXULONG, MAXULONG};
    ULONG TimerPolicy[2] = {MAXULONG, MAXULONG};
    HANDLE KeyHandle;
    ULONG TimerCount;
    ULONG Timer;
    NTSTATUS Status;
    NTSTATUS TimeStatus = STATUS_NOT_SUPPORTED;

    if (!AcpiTimeQueryInteger(DeviceExtension, ACPITIME_METHOD('_', 'G', 'C', 'P'), &DeviceExtension->Capabilities))
    {
        DPRINT1("ACPITIME: required _GCP method failed\n");
        return FALSE;
    }
    DeviceExtension->Capabilities &= ACPITIME_CAP_VALID_MASK;
    if (DeviceExtension->Capabilities & ACPITIME_CAP_REAL_TIME)
    {
        TimeValid = AcpiTimeReadClock(DeviceExtension, &Time, &TimeStatus);
        if (TimeStatus == STATUS_OBJECT_NAME_NOT_FOUND || TimeStatus == STATUS_OBJECT_PATH_NOT_FOUND)
        {
            DeviceExtension->Capabilities &= ~(ACPITIME_CAP_REAL_TIME | ACPITIME_CAP_MILLISECOND_TIME);
            DPRINT1("ACPITIME: firmware advertised an unusable real-time clock; masking clock capabilities\n");
        }
    }
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
    return TRUE;
}

static
VOID
NTAPI
AcpiTimeWorker(
    _In_ PVOID Context)
{
    PACPITIME_WORK_CONTEXT WorkContext = Context;
    PACPITIME_DEVICE_EXTENSION DeviceExtension = WorkContext->DeviceExtension;

    KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
    if (DeviceExtension->Started && !DeviceExtension->Removing)
        AcpiTimeRefresh(DeviceExtension);
    KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
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
AcpiTimeSetDeviceInterface(
    _Inout_ PACPITIME_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN Enable)
{
    NTSTATUS Status;

    if (!DeviceExtension->InterfaceRegistered || DeviceExtension->InterfaceEnabled == Enable)
        return STATUS_SUCCESS;
    Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, Enable);
    if (NT_SUCCESS(Status))
        DeviceExtension->InterfaceEnabled = Enable;
    return Status;
}

static
NTSTATUS
AcpiTimeStart(
    _Inout_ PACPITIME_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
    if (!AcpiTimeRefresh(DeviceExtension))
    {
        KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
    Status = AcpiTimeQueryInterface(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPITIME: ACPI interface query failed, status 0x%08lx\n", Status);
        return Status;
    }
    Status = DeviceExtension->AcpiInterface.RegisterForDeviceNotifications(DeviceExtension->AcpiInterface.Context, AcpiTimeNotification, DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPITIME: notification registration failed, status 0x%08lx\n", Status);
        AcpiTimeReleaseInterface(DeviceExtension);
        return Status;
    }
    DeviceExtension->NotificationsRegistered = TRUE;
    InterlockedExchange(&DeviceExtension->Started, TRUE);
    Status = AcpiTimeSetDeviceInterface(DeviceExtension, TRUE);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&DeviceExtension->Started, FALSE);
        AcpiTimeReleaseInterface(DeviceExtension);
    }
    return Status;
}

static
NTSTATUS
AcpiTimeCompleteRequest(
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
AcpiTimeCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPITIME_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    if (Stack->MajorFunction == IRP_MJ_CREATE && (!DeviceExtension->Started || DeviceExtension->Removing))
        Status = STATUS_DEVICE_NOT_READY;
    return AcpiTimeCompleteRequest(Irp, Status, 0);
}

static
NTSTATUS
NTAPI
AcpiTimeDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPITIME_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG ControlCode = Stack->Parameters.DeviceIoControl.IoControlCode;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return AcpiTimeCompleteRequest(Irp, Status, 0);
    if (!DeviceExtension->Started || DeviceExtension->Removing)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Complete;
    }
    KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
    switch (ControlCode)
    {
        case IOCTL_ACPITIME_QUERY_INFORMATION:
        {
            PACPITIME_DEVICE_INFORMATION DeviceInformation = Buffer;

            if (OutputLength < sizeof(*DeviceInformation))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            RtlZeroMemory(DeviceInformation, sizeof(*DeviceInformation));
            DeviceInformation->Version = ACPITIME_DRIVER_INTERFACE_VERSION;
            DeviceInformation->Capabilities = DeviceExtension->Capabilities;
            Information = sizeof(*DeviceInformation);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_ACPITIME_GET_TIME:
        {
            PACPITIME_TIME_INFORMATION Time = Buffer;
            NTSTATUS MethodStatus;

            if (OutputLength < sizeof(*Time))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            if (!(DeviceExtension->Capabilities & ACPITIME_CAP_REAL_TIME))
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }
            RtlZeroMemory(Time, sizeof(*Time));
            Status = AcpiTimeReadClock(DeviceExtension, Time, &MethodStatus) ? STATUS_SUCCESS : MethodStatus;
            if (NT_SUCCESS(Status))
                Information = sizeof(*Time);
            break;
        }

        case IOCTL_ACPITIME_SET_TIME:
            if (InputLength < sizeof(ACPITIME_TIME_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else if (!(DeviceExtension->Capabilities & ACPITIME_CAP_REAL_TIME))
                Status = STATUS_NOT_SUPPORTED;
            else if (!AcpiTimeValidateClock(Buffer, TRUE))
                Status = STATUS_INVALID_PARAMETER;
            else
                Status = AcpiTimeWriteClock(DeviceExtension, Buffer) ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
            break;

        case IOCTL_ACPITIME_GET_TIMER:
        {
            PACPITIME_TIMER_INFORMATION TimerInformation = Buffer;
            ULONG Timer;

            if (InputLength < sizeof(Timer) || OutputLength < sizeof(*TimerInformation))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            Timer = TimerInformation->Timer;
            if (!AcpiTimeTimerSupported(DeviceExtension, Timer))
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }
            RtlZeroMemory(TimerInformation, sizeof(*TimerInformation));
            TimerInformation->Timer = Timer;
            Status = AcpiTimeReadTimer(DeviceExtension, TimerInformation) ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
            if (NT_SUCCESS(Status))
                Information = sizeof(*TimerInformation);
            break;
        }

        case IOCTL_ACPITIME_SET_TIMER:
            if (InputLength < sizeof(ACPITIME_TIMER_SET))
                Status = STATUS_BUFFER_TOO_SMALL;
            else if (!AcpiTimeTimerSupported(DeviceExtension, ((PACPITIME_TIMER_SET)Buffer)->Timer))
                Status = STATUS_NOT_SUPPORTED;
            else
                Status = AcpiTimeWriteTimer(DeviceExtension, Buffer) ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
            break;

        case IOCTL_ACPITIME_CLEAR_STATUS:
            if (InputLength < sizeof(ULONG))
                Status = STATUS_BUFFER_TOO_SMALL;
            else if (!AcpiTimeTimerSupported(DeviceExtension, *(PULONG)Buffer))
                Status = STATUS_NOT_SUPPORTED;
            else
                Status = AcpiTimeClearWakeStatus(DeviceExtension, *(PULONG)Buffer) ? STATUS_SUCCESS : STATUS_IO_DEVICE_ERROR;
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);

Complete:
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return AcpiTimeCompleteRequest(Irp, Status, Information);
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
            InterlockedExchange(&DeviceExtension->Started, FALSE);
            AcpiTimeSetDeviceInterface(DeviceExtension, FALSE);
            AcpiTimeReleaseInterface(DeviceExtension);
            KeWaitForSingleObject(&DeviceExtension->WorkIdleEvent, Executive, KernelMode, FALSE, NULL);
            KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
            KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            InterlockedExchange(&DeviceExtension->Started, FALSE);
            InterlockedExchange(&DeviceExtension->Removing, TRUE);
            AcpiTimeSetDeviceInterface(DeviceExtension, FALSE);
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
            InterlockedExchange(&DeviceExtension->Started, FALSE);
            InterlockedExchange(&DeviceExtension->Removing, TRUE);
            AcpiTimeSetDeviceInterface(DeviceExtension, FALSE);
            AcpiTimeReleaseInterface(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = AcpiTimeForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            if (DeviceExtension->InterfaceRegistered)
                RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
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
    KeInitializeMutex(&DeviceExtension->MethodMutex, 0);
    KeInitializeEvent(&DeviceExtension->WorkIdleEvent, NotificationEvent, TRUE);
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_DEVINTERFACE_REACTOS_ACPITIME, NULL, &DeviceExtension->InterfaceName);
    if (!NT_SUCCESS(Status))
    {
        IoDetachDevice(DeviceExtension->LowerDevice);
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceExtension->InterfaceRegistered = TRUE;
    DeviceObject->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;
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
    DriverObject->MajorFunction[IRP_MJ_CREATE] = AcpiTimeCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AcpiTimeCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = AcpiTimeCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AcpiTimeDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = AcpiTimePnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AcpiTimePower;
    DriverObject->DriverExtension->AddDevice = AcpiTimeAddDevice;
    DriverObject->DriverUnload = AcpiTimeUnload;
    return STATUS_SUCCESS;
}
