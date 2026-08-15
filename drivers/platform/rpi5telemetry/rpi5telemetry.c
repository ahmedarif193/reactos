/*
 * PROJECT:     ReactOS Raspberry Pi 5 Firmware Telemetry Driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Sensor API provider for RPI0005 firmware telemetry
 */

#include <ntddk.h>
#include <acpiioct.h>
#include <initguid.h>
#include <reactos/drivers/sensorprovider.h>

#define NDEBUG
#include <debug.h>

#define RPI5TEL_TAG '5TlR'
#define RPI5TEL_METHOD(a, b, c, d) ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))
#define RPI5TEL_DSM_REVISION 1
#define RPI5TEL_DSM_QUERY 0
#define RPI5TEL_DSM_CAPABILITIES 1
#define RPI5TEL_DSM_LEGACY_VOLTAGE 3
#define RPI5TEL_DSM_RTC 4
#define RPI5TEL_DSM_PMIC_VOLTAGE 5
#define RPI5TEL_DSM_PMIC_CURRENT 6
#define RPI5TEL_DSM_PMIC_POWER 7
#define RPI5TEL_DSM_POWER_SOURCE 8
#define RPI5TEL_REQUIRED_DSM_FUNCTIONS 0x00EA
#define RPI5TEL_MAX_OUTPUT 4096
#define RPI5TEL_READ_ATTEMPTS 5
#define RPI5TEL_READ_RETRY_DELAY_100NS (-10 * 1000)

typedef struct _RPI5TEL_CHANNEL
{
    UCHAR Function;
    UCHAR Id;
    UCHAR Type;
    UCHAR Unit;
    PCWSTR Name;
} RPI5TEL_CHANNEL, *PRPI5TEL_CHANNEL;

typedef struct _RPI5TEL_PMIC_CHANNEL
{
    UCHAR Id;
    BOOLEAN Voltage;
    PCWSTR Name;
} RPI5TEL_PMIC_CHANNEL, *PRPI5TEL_PMIC_CHANNEL;

typedef struct _RPI5TEL_POWER_PAIR
{
    UCHAR CurrentId;
    UCHAR VoltageId;
    PCWSTR Name;
} RPI5TEL_POWER_PAIR, *PRPI5TEL_POWER_PAIR;

typedef struct _RPI5TEL_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    KMUTEX MethodMutex;
    UNICODE_STRING InterfaceName;
    BOOLEAN InterfaceRegistered;
    BOOLEAN InterfaceEnabled;
    volatile LONG Started;
    volatile LONG Removing;
    ULONG TemperatureMask;
    ULONG LegacyVoltageMask;
    ULONG RtcMask;
    ULONG PmicVoltageMask;
    ULONG PmicCurrentMask;
    ULONG ChannelCount;
    RPI5TEL_CHANNEL Channels[REACTOS_SENSOR_PROVIDER_MAX_CHANNELS];
} RPI5TEL_DEVICE_EXTENSION, *PRPI5TEL_DEVICE_EXTENSION;

/* {4376C7BC-6ED1-4D3F-9DD7-74791A2E6B04} */
static const GUID Rpi5TelemetryProviderId =
    {0x4376C7BC, 0x6ED1, 0x4D3F, {0x9D, 0xD7, 0x74, 0x79, 0x1A, 0x2E, 0x6B, 0x04}};

static const UCHAR Rpi5TelemetryDsmUuid[16] =
    {0xD5, 0xD5, 0xFD, 0x31, 0x36, 0x6D, 0xB7, 0x47, 0xBC, 0xE8, 0x74, 0x69, 0x0D, 0x66, 0x1C, 0x0C};

static const RPI5TEL_PMIC_CHANNEL Rpi5TelemetryPmicChannels[] =
{
    {0, FALSE, L"3V7_WL_SW current"}, {1, FALSE, L"3V3_SYS current"},
    {2, FALSE, L"1V8_SYS current"}, {3, FALSE, L"DDR_VDD2 current"},
    {4, FALSE, L"DDR_VDDQ current"}, {5, FALSE, L"1V1_SYS current"},
    {6, FALSE, L"0V8_SW current"}, {7, FALSE, L"VDD_CORE current"},
    {8, TRUE, L"3V7_WL_SW voltage"}, {9, TRUE, L"3V3_SYS voltage"},
    {10, TRUE, L"1V8_SYS voltage"}, {11, TRUE, L"DDR_VDD2 voltage"},
    {12, TRUE, L"DDR_VDDQ voltage"}, {13, TRUE, L"1V1_SYS voltage"},
    {14, TRUE, L"0V8_SW voltage"}, {15, TRUE, L"VDD_CORE voltage"},
    {16, FALSE, L"0V8_AON current"}, {17, FALSE, L"3V3_DAC current"},
    {18, FALSE, L"3V3_ADC current"}, {19, TRUE, L"0V8_AON voltage"},
    {20, TRUE, L"3V3_DAC voltage"}, {21, TRUE, L"3V3_ADC voltage"},
    {22, FALSE, L"HDMI current"}, {23, TRUE, L"HDMI voltage"},
    {24, TRUE, L"EXT5V voltage"}, {25, TRUE, L"RTC BATT voltage"},
};

static const RPI5TEL_POWER_PAIR Rpi5TelemetryPowerPairs[] =
{
    {0, 8, L"3V7_WL_SW power"}, {1, 9, L"3V3_SYS power"},
    {2, 10, L"1V8_SYS power"}, {3, 11, L"DDR_VDD2 power"},
    {4, 12, L"DDR_VDDQ power"}, {5, 13, L"1V1_SYS power"},
    {6, 14, L"0V8_SW power"}, {7, 15, L"VDD_CORE power"},
    {16, 19, L"0V8_AON power"}, {17, 20, L"3V3_DAC power"},
    {18, 21, L"3V3_ADC power"}, {22, 23, L"HDMI power"},
};

static
VOID
Rpi5TelemetryCopyString(
    _Out_writes_(DestinationCount) PWCHAR Destination,
    _In_ SIZE_T DestinationCount,
    _In_ PCWSTR Source)
{
    SIZE_T Index;

    for (Index = 0; Index + 1 < DestinationCount && Source[Index] != UNICODE_NULL; Index++)
        Destination[Index] = Source[Index];
    Destination[Index] = UNICODE_NULL;
}

static
NTSTATUS
NTAPI
Rpi5TelemetryCompletion(
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
Rpi5TelemetryForwardSynchronously(
    _In_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, Rpi5TelemetryCompletion, &Event, TRUE, TRUE, TRUE);
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
Rpi5TelemetrySendRequest(
    _In_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension,
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
Rpi5TelemetryEvaluateDsm(
    _In_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Function,
    _In_ BOOLEAN HasId,
    _In_ ULONG Id,
    _Outptr_ PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer)
{
    ULONG InputStorage[32];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Input = (PVOID)InputStorage;
    PACPI_METHOD_ARGUMENT Argument;
    PACPI_METHOD_ARGUMENT PackageArgument;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    ULONG InputLength;
    ULONG_PTR Information;
    NTSTATUS Status;

    *OutputBuffer = NULL;
    RtlZeroMemory(InputStorage, sizeof(InputStorage));
    Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input->MethodNameAsUlong = RPI5TEL_METHOD('_', 'D', 'S', 'M');
    Input->ArgumentCount = 4;
    Argument = Input->Argument;
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument, Rpi5TelemetryDsmUuid, sizeof(Rpi5TelemetryDsmUuid));
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, RPI5TEL_DSM_REVISION);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, Function);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    Argument->Type = ACPI_METHOD_ARGUMENT_PACKAGE;
    Argument->Argument = 0;
    Argument->DataLength = HasId ? ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) : 0;
    if (HasId)
    {
        PackageArgument = (PACPI_METHOD_ARGUMENT)Argument->Data;
        ACPI_METHOD_SET_ARGUMENT_INTEGER(PackageArgument, Id);
    }
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    InputLength = (ULONG)((PUCHAR)Argument - (PUCHAR)Input);
    Input->Size = InputLength - FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    Output = ExAllocatePoolWithTag(PagedPool, RPI5TEL_MAX_OUTPUT, RPI5TEL_TAG);
    if (!Output)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Output, RPI5TEL_MAX_OUTPUT);
    Status = Rpi5TelemetrySendRequest(DeviceExtension, Input, InputLength, Output, RPI5TEL_MAX_OUTPUT, &Information);
    if (!NT_SUCCESS(Status))
        goto Failure;
    if (Information < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + ACPI_METHOD_ARGUMENT_LENGTH(0) || Information > RPI5TEL_MAX_OUTPUT || Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Output->Length > Information || Output->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(&Output->Argument[0]) || Output->Count == 0)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Failure;
    }
    *OutputBuffer = Output;
    return STATUS_SUCCESS;

Failure:
    ExFreePoolWithTag(Output, RPI5TEL_TAG);
    return Status;
}

static
PACPI_METHOD_ARGUMENT
Rpi5TelemetryOutputArgument(
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG Index)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Current;
    ULONG ArgumentLength;

    if (!Output ||
        Output->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ||
        Index >= Output->Count)
    {
        return NULL;
    }
    Argument = &Output->Argument[0];
    End = (PUCHAR)Output + Output->Length;
    for (Current = 0; Current <= Index; Current++)
    {
        if ((PUCHAR)Argument > End ||
            (ULONG)(End - (PUCHAR)Argument) < ACPI_METHOD_ARGUMENT_LENGTH(0))
        {
            return NULL;
        }
        ArgumentLength = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Argument);
        if (ArgumentLength > (ULONG)(End - (PUCHAR)Argument))
            return NULL;
        if (Current == Index)
            return Argument;
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
    return NULL;
}

static
PACPI_METHOD_ARGUMENT
Rpi5TelemetryPackageArgument(
    _In_ PACPI_METHOD_ARGUMENT Package,
    _In_ ULONG Index)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Current;
    ULONG ArgumentLength;

    if (!Package || Package->Type != ACPI_METHOD_ARGUMENT_PACKAGE || Package->DataLength < ACPI_METHOD_ARGUMENT_LENGTH(0))
        return NULL;
    Argument = (PACPI_METHOD_ARGUMENT)Package->Data;
    End = Package->Data + Package->DataLength;
    for (Current = 0; Current <= Index; Current++)
    {
        if ((PUCHAR)Argument > End || (ULONG)(End - (PUCHAR)Argument) < ACPI_METHOD_ARGUMENT_LENGTH(0))
            return NULL;
        ArgumentLength = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Argument);
        if (ArgumentLength > (ULONG)(End - (PUCHAR)Argument))
            return NULL;
        if (Current == Index)
            return Argument;
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
    return NULL;
}

static
PACPI_METHOD_ARGUMENT
Rpi5TelemetryResultArgument(
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG Index)
{
    if (Output->Count == 1 &&
        Output->Argument[0].Type == ACPI_METHOD_ARGUMENT_PACKAGE)
    {
        return Rpi5TelemetryPackageArgument(&Output->Argument[0], Index);
    }
    return Rpi5TelemetryOutputArgument(Output, Index);
}

static
NTSTATUS
Rpi5TelemetryReadInteger(
    _In_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Function,
    _In_ ULONG Id,
    _Out_ PULONG Value)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    LARGE_INTEGER RetryDelay;
    ULONG Attempt;
    NTSTATUS Status;

    RetryDelay.QuadPart = RPI5TEL_READ_RETRY_DELAY_100NS;
    for (Attempt = 0; Attempt < RPI5TEL_READ_ATTEMPTS; Attempt++)
    {
        Status = Rpi5TelemetryEvaluateDsm(DeviceExtension, Function, TRUE, Id, &Output);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Output->Count != 1 || Output->Argument[0].Type != ACPI_METHOD_ARGUMENT_INTEGER || Output->Argument[0].Argument == MAXULONG)
            Status = STATUS_DEVICE_DATA_ERROR;
        else
        {
            *Value = Output->Argument[0].Argument;
            Status = STATUS_SUCCESS;
        }
        ExFreePoolWithTag(Output, RPI5TEL_TAG);
        if (Status != STATUS_DEVICE_DATA_ERROR || Attempt + 1 == RPI5TEL_READ_ATTEMPTS)
            break;
        KeDelayExecutionThread(KernelMode, FALSE, &RetryDelay);
    }
    if (NT_SUCCESS(Status) && Attempt != 0)
        DPRINT1("RPI5TEL: recovered transient firmware read function=%lu id=%lu attempts=%lu\n", Function, Id, Attempt + 1);
    return Status;
}

static
VOID
Rpi5TelemetryAddChannel(
    _Inout_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Function,
    _In_ UCHAR Id,
    _In_ UCHAR Type,
    _In_ UCHAR Unit,
    _In_ PCWSTR Name)
{
    PRPI5TEL_CHANNEL Channel;

    if (DeviceExtension->ChannelCount >= RTL_NUMBER_OF(DeviceExtension->Channels))
        return;
    Channel = &DeviceExtension->Channels[DeviceExtension->ChannelCount++];
    Channel->Function = Function;
    Channel->Id = Id;
    Channel->Type = Type;
    Channel->Unit = Unit;
    Channel->Name = Name;
}

static
NTSTATUS
Rpi5TelemetryQueryCapabilities(
    _Inout_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG Values[6];
    ULONG Supported = 0;
    ULONG Index;
    NTSTATUS Status;

    Status = Rpi5TelemetryEvaluateDsm(DeviceExtension, RPI5TEL_DSM_QUERY, FALSE, 0, &Output);
    if (!NT_SUCCESS(Status))
        return Status;
    Argument = Rpi5TelemetryOutputArgument(Output, 0);
    if (Output->Count == 1 && Argument && Argument->Type == ACPI_METHOD_ARGUMENT_BUFFER && Argument->DataLength >= sizeof(USHORT))
        Supported = Argument->Data[0] | ((ULONG)Argument->Data[1] << 8);
    ExFreePoolWithTag(Output, RPI5TEL_TAG);
    if ((Supported & RPI5TEL_REQUIRED_DSM_FUNCTIONS) != RPI5TEL_REQUIRED_DSM_FUNCTIONS)
        return STATUS_NOT_SUPPORTED;
    Status = Rpi5TelemetryEvaluateDsm(DeviceExtension, RPI5TEL_DSM_CAPABILITIES, FALSE, 0, &Output);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Index = 0; Index < RTL_NUMBER_OF(Values); Index++)
    {
        PACPI_METHOD_ARGUMENT Item = Rpi5TelemetryResultArgument(Output, Index);

        if (!Item || Item->Type != ACPI_METHOD_ARGUMENT_INTEGER)
        {
            Status = STATUS_ACPI_INVALID_DATA;
            goto Done;
        }
        Values[Index] = Item->Argument;
    }
    if (Values[0] != RPI5TEL_DSM_REVISION)
    {
        Status = STATUS_REVISION_MISMATCH;
        goto Done;
    }
    DeviceExtension->TemperatureMask = Values[1];
    DeviceExtension->LegacyVoltageMask = Values[2];
    DeviceExtension->RtcMask = Values[3];
    DeviceExtension->PmicVoltageMask = Values[4];
    DeviceExtension->PmicCurrentMask = Values[5];
    Status = STATUS_SUCCESS;

Done:
    ExFreePoolWithTag(Output, RPI5TEL_TAG);
    return Status;
}

static
NTSTATUS
Rpi5TelemetryBuildChannels(
    _Inout_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Index;

    DeviceExtension->ChannelCount = 0;
    RtlZeroMemory(DeviceExtension->Channels, sizeof(DeviceExtension->Channels));
    for (Index = 0; Index < RTL_NUMBER_OF(Rpi5TelemetryPmicChannels); Index++)
    {
        const RPI5TEL_PMIC_CHANNEL *Channel = &Rpi5TelemetryPmicChannels[Index];
        ULONG Mask = Channel->Voltage ? DeviceExtension->PmicVoltageMask : DeviceExtension->PmicCurrentMask;

        if (!(Mask & (1UL << Channel->Id)))
            continue;
        Rpi5TelemetryAddChannel(DeviceExtension,
                                Channel->Voltage ? RPI5TEL_DSM_PMIC_VOLTAGE : RPI5TEL_DSM_PMIC_CURRENT,
                                Channel->Id,
                                Channel->Voltage ? REACTOS_SENSOR_TYPE_VOLTAGE : REACTOS_SENSOR_TYPE_CURRENT,
                                Channel->Voltage ? REACTOS_SENSOR_UNIT_MICROVOLTS : REACTOS_SENSOR_UNIT_MICROAMPS,
                                Channel->Name);
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Rpi5TelemetryPowerPairs); Index++)
    {
        const RPI5TEL_POWER_PAIR *Pair = &Rpi5TelemetryPowerPairs[Index];

        if ((DeviceExtension->PmicCurrentMask & (1UL << Pair->CurrentId)) && (DeviceExtension->PmicVoltageMask & (1UL << Pair->VoltageId)))
            Rpi5TelemetryAddChannel(DeviceExtension, RPI5TEL_DSM_PMIC_POWER, Pair->CurrentId, REACTOS_SENSOR_TYPE_POWER, REACTOS_SENSOR_UNIT_MILLIWATTS, Pair->Name);
    }
    return DeviceExtension->ChannelCount ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
VOID
Rpi5TelemetryWriteDword(
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
Rpi5TelemetryPublishMetadata(
    _In_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension)
{
    static const PCWSTR LegacyVoltageNames[] = {NULL, L"LegacyVoltage1Microvolts", L"LegacyVoltage2Microvolts", L"LegacyVoltage3Microvolts", L"LegacyVoltage4Microvolts"};
    static const PCWSTR RtcNames[] = {L"RtcRegister0", L"RtcRegister1", L"RtcRegister2", L"RtcRegister3"};
    static const PCWSTR SourceNames[] = {L"SourceMaxCurrentMilliAmps", L"SourceMaxPowerMilliWatts", L"PowerResetReason", L"UsbHighCurrent", L"OverCurrent"};
    PACPI_EVAL_OUTPUT_BUFFER Output;
    HANDLE KeyHandle;
    ULONG Index;
    ULONG Value;

    if (!NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceExtension->PhysicalDevice, PLUGPLAY_REGKEY_DEVICE, KEY_SET_VALUE, &KeyHandle)))
        return;
    Rpi5TelemetryWriteDword(KeyHandle, L"TelemetryRevision", RPI5TEL_DSM_REVISION);
    Rpi5TelemetryWriteDword(KeyHandle, L"TemperatureMask", DeviceExtension->TemperatureMask);
    Rpi5TelemetryWriteDword(KeyHandle, L"LegacyVoltageMask", DeviceExtension->LegacyVoltageMask);
    Rpi5TelemetryWriteDword(KeyHandle, L"RtcMask", DeviceExtension->RtcMask);
    Rpi5TelemetryWriteDword(KeyHandle, L"PmicVoltageMask", DeviceExtension->PmicVoltageMask);
    Rpi5TelemetryWriteDword(KeyHandle, L"PmicCurrentMask", DeviceExtension->PmicCurrentMask);
    for (Index = 1; Index < RTL_NUMBER_OF(LegacyVoltageNames); Index++)
    {
        if ((DeviceExtension->LegacyVoltageMask & (1UL << Index)) && NT_SUCCESS(Rpi5TelemetryReadInteger(DeviceExtension, RPI5TEL_DSM_LEGACY_VOLTAGE, Index, &Value)))
            Rpi5TelemetryWriteDword(KeyHandle, LegacyVoltageNames[Index], Value);
    }
    for (Index = 0; Index < RTL_NUMBER_OF(RtcNames); Index++)
    {
        if ((DeviceExtension->RtcMask & (1UL << Index)) && NT_SUCCESS(Rpi5TelemetryReadInteger(DeviceExtension, RPI5TEL_DSM_RTC, Index, &Value)))
            Rpi5TelemetryWriteDword(KeyHandle, RtcNames[Index], Value);
    }
    if (NT_SUCCESS(Rpi5TelemetryEvaluateDsm(DeviceExtension, RPI5TEL_DSM_POWER_SOURCE, FALSE, 0, &Output)))
    {
        for (Index = 0; Index < RTL_NUMBER_OF(SourceNames); Index++)
        {
            PACPI_METHOD_ARGUMENT Item = Rpi5TelemetryResultArgument(Output, Index);

            if (Item && Item->Type == ACPI_METHOD_ARGUMENT_INTEGER)
                Rpi5TelemetryWriteDword(KeyHandle, SourceNames[Index], Item->Argument);
        }
        ExFreePoolWithTag(Output, RPI5TEL_TAG);
    }
    ZwClose(KeyHandle);
}

static
NTSTATUS
Rpi5TelemetryStart(
    _Inout_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
    Status = Rpi5TelemetryQueryCapabilities(DeviceExtension);
    if (NT_SUCCESS(Status))
        Status = Rpi5TelemetryBuildChannels(DeviceExtension);
    if (NT_SUCCESS(Status))
        Rpi5TelemetryPublishMetadata(DeviceExtension);
    KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RPI5TEL: capability discovery failed, status 0x%08lx\n", Status);
        return Status;
    }
    InterlockedExchange(&DeviceExtension->Started, TRUE);
    Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, TRUE);
    if (!NT_SUCCESS(Status))
        InterlockedExchange(&DeviceExtension->Started, FALSE);
    else
        DeviceExtension->InterfaceEnabled = TRUE;
    DPRINT1("RPI5TEL: channels=%lu legacyV=0x%08lx pmicV=0x%08lx pmicA=0x%08lx rtc=0x%08lx status=0x%08lx\n",
            DeviceExtension->ChannelCount, DeviceExtension->LegacyVoltageMask, DeviceExtension->PmicVoltageMask,
            DeviceExtension->PmicCurrentMask, DeviceExtension->RtcMask, Status);
    return Status;
}

static
VOID
Rpi5TelemetryStop(
    _Inout_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension)
{
    InterlockedExchange(&DeviceExtension->Started, FALSE);
    if (DeviceExtension->InterfaceEnabled)
    {
        IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
        DeviceExtension->InterfaceEnabled = FALSE;
    }
}

static
VOID
Rpi5TelemetryChannelGuid(
    _In_ PRPI5TEL_CHANNEL Channel,
    _Out_ PGUID SensorId)
{
    *SensorId = Rpi5TelemetryProviderId;
    SensorId->Data1 ^= ((ULONG)Channel->Function << 16) | ((ULONG)Channel->Type << 8) | Channel->Id;
}

static
NTSTATUS
Rpi5TelemetryQueryProvider(
    _In_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension,
    _Out_ PREACTOS_SENSOR_PROVIDER_INFORMATION Information)
{
    ULONG Index;

    if (!DeviceExtension->Started)
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(Information, sizeof(*Information));
    Information->Version = REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION;
    Information->Size = sizeof(*Information);
    Information->ProviderId = Rpi5TelemetryProviderId;
    Information->Flags = REACTOS_SENSOR_PROVIDER_FLAG_READ_ONLY;
    Information->ChannelCount = DeviceExtension->ChannelCount;
    Rpi5TelemetryCopyString(Information->Manufacturer, RTL_NUMBER_OF(Information->Manufacturer), L"Raspberry Pi");
    Rpi5TelemetryCopyString(Information->Model, RTL_NUMBER_OF(Information->Model), L"Raspberry Pi 5 firmware telemetry");
    for (Index = 0; Index < DeviceExtension->ChannelCount; Index++)
    {
        PRPI5TEL_CHANNEL Source = &DeviceExtension->Channels[Index];
        PREACTOS_SENSOR_CHANNEL_INFORMATION Channel = &Information->Channels[Index];

        Rpi5TelemetryChannelGuid(Source, &Channel->SensorId);
        Channel->Type = Source->Type;
        Channel->Unit = Source->Unit;
        Channel->Flags = REACTOS_SENSOR_CHANNEL_FLAG_READ_ONLY | REACTOS_SENSOR_CHANNEL_FLAG_INTERNAL;
        Channel->Resolution = 1;
        Rpi5TelemetryCopyString(Channel->Name, RTL_NUMBER_OF(Channel->Name), Source->Name);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
Rpi5TelemetryReadChannel(
    _In_ PRPI5TEL_DEVICE_EXTENSION DeviceExtension,
    _In_ PREACTOS_SENSOR_READ_INPUT Input,
    _Out_ PREACTOS_SENSOR_READING Reading)
{
    PRPI5TEL_CHANNEL Channel;
    LARGE_INTEGER Timestamp;
    ULONG Value;
    NTSTATUS Status;

    if (Input->Version != REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION || Input->Size < sizeof(*Input) || Input->Reserved != 0)
        return STATUS_INVALID_PARAMETER;
    if (!DeviceExtension->Started)
        return STATUS_DEVICE_NOT_READY;
    if (Input->ChannelIndex >= DeviceExtension->ChannelCount)
        return STATUS_INVALID_PARAMETER;
    Channel = &DeviceExtension->Channels[Input->ChannelIndex];
    KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
    Status = Rpi5TelemetryReadInteger(DeviceExtension, Channel->Function, Channel->Id, &Value);
    KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;
    KeQuerySystemTime(&Timestamp);
    RtlZeroMemory(Reading, sizeof(*Reading));
    Reading->Version = REACTOS_SENSOR_PROVIDER_INTERFACE_VERSION;
    Reading->Size = sizeof(*Reading);
    Rpi5TelemetryChannelGuid(Channel, &Reading->SensorId);
    Reading->Type = Channel->Type;
    Reading->Unit = Channel->Unit;
    Reading->Flags = REACTOS_SENSOR_CHANNEL_FLAG_READ_ONLY | REACTOS_SENSOR_CHANNEL_FLAG_INTERNAL;
    Reading->RawValue = Value;
    Reading->Value = Value;
    Reading->Timestamp = Timestamp.QuadPart;
    return STATUS_SUCCESS;
}

static
NTSTATUS
Rpi5TelemetryComplete(
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
Rpi5TelemetryCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRPI5TEL_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    if (Stack->MajorFunction == IRP_MJ_CREATE && (!DeviceExtension->Started || DeviceExtension->Removing))
        Status = STATUS_DEVICE_NOT_READY;
    return Rpi5TelemetryComplete(Irp, Status, 0);
}

static
NTSTATUS
NTAPI
Rpi5TelemetryDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRPI5TEL_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    REACTOS_SENSOR_READ_INPUT Input;
    NTSTATUS Status;
    ULONG_PTR Information = 0;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return Rpi5TelemetryComplete(Irp, Status, 0);
    if (!DeviceExtension->Started || DeviceExtension->Removing)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Done;
    }
    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_REACTOS_SENSOR_QUERY_PROVIDER:
            if (Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(REACTOS_SENSOR_PROVIDER_INFORMATION))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                Status = Rpi5TelemetryQueryProvider(DeviceExtension, Irp->AssociatedIrp.SystemBuffer);
                if (NT_SUCCESS(Status))
                    Information = sizeof(REACTOS_SENSOR_PROVIDER_INFORMATION);
            }
            break;

        case IOCTL_REACTOS_SENSOR_READ_CHANNEL:
            if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(Input) || Stack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(REACTOS_SENSOR_READING))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                Input = *(PREACTOS_SENSOR_READ_INPUT)Irp->AssociatedIrp.SystemBuffer;
                Status = Rpi5TelemetryReadChannel(DeviceExtension, &Input, Irp->AssociatedIrp.SystemBuffer);
                if (NT_SUCCESS(Status))
                    Information = sizeof(REACTOS_SENSOR_READING);
            }
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

Done:
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return Rpi5TelemetryComplete(Irp, Status, Information);
}

static
NTSTATUS
NTAPI
Rpi5TelemetryPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRPI5TEL_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = Rpi5TelemetryForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = Rpi5TelemetryStart(DeviceExtension);
            return Rpi5TelemetryComplete(Irp, Status, 0);

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Rpi5TelemetryStop(DeviceExtension);
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (!NT_SUCCESS(Status))
                return Rpi5TelemetryComplete(Irp, Status, 0);
            InterlockedExchange(&DeviceExtension->Removing, TRUE);
            Rpi5TelemetryStop(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = Rpi5TelemetryForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            if (DeviceExtension->InterfaceRegistered)
                RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
            Rpi5TelemetryComplete(Irp, Status, 0);
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
Rpi5TelemetryPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRPI5TEL_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
Rpi5TelemetryPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRPI5TEL_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
Rpi5TelemetryAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PRPI5TEL_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*DeviceExtension), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, RPI5TEL_TAG, 0, 0);
    KeInitializeMutex(&DeviceExtension->MethodMutex, 0);
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_DEVINTERFACE_REACTOS_SENSOR_PROVIDER, NULL, &DeviceExtension->InterfaceName);
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
Rpi5TelemetryUnload(
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
    DriverObject->MajorFunction[IRP_MJ_CREATE] = Rpi5TelemetryCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Rpi5TelemetryCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = Rpi5TelemetryCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Rpi5TelemetryDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = Rpi5TelemetryPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = Rpi5TelemetryPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = Rpi5TelemetryPassThrough;
    DriverObject->DriverExtension->AddDevice = Rpi5TelemetryAddDevice;
    DriverObject->DriverUnload = Rpi5TelemetryUnload;
    return STATUS_SUCCESS;
}
