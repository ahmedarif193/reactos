/*
 * PROJECT:     ReactOS Raspberry Pi 5 framebuffer miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Bounded ACPI platform-information query for user-mode providers
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5vc4.h"

#include <initguid.h>
#include <poclass.h>
#include <acpiioct.h>

NTSYSAPI
NTSTATUS
NTAPI
ZwWaitForSingleObject(
    _In_ HANDLE Handle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);

#define NDEBUG
#include <debug.h>

#define RPI5VC4_ACPI_METHOD(a, b, c, d) \
    ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | \
     ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))

static
NTSTATUS
Rpi5Vc4OpenInterface(
    _In_ PCWSTR InterfaceName,
    _In_ ACCESS_MASK Access,
    _Out_ PHANDLE Handle)
{
    UNICODE_STRING Name;
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;

    RtlInitUnicodeString(&Name, InterfaceName);
    InitializeObjectAttributes(&Attributes,
                               &Name,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    return ZwCreateFile(Handle,
                        Access | SYNCHRONIZE,
                        &Attributes,
                        &IoStatus,
                        NULL,
                        FILE_ATTRIBUTE_NORMAL,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_OPEN,
                        FILE_NON_DIRECTORY_FILE |
                            FILE_SYNCHRONOUS_IO_NONALERT,
                        NULL,
                        0);
}

static
NTSTATUS
Rpi5Vc4DeviceIoControl(
    _In_ HANDLE Handle,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID OutputBuffer,
    _In_ ULONG OutputLength,
    _Out_opt_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    Status = ZwDeviceIoControlFile(Handle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatus,
                                   IoControlCode,
                                   InputBuffer,
                                   InputLength,
                                   OutputBuffer,
                                   OutputLength);
    if (Status == STATUS_PENDING)
    {
        Status = ZwWaitForSingleObject(Handle, FALSE, NULL);
        if (NT_SUCCESS(Status))
            Status = IoStatus.Status;
    }
    if (Information != NULL)
        *Information = NT_SUCCESS(Status) ? IoStatus.Information : 0;
    return Status;
}

static
VOID
Rpi5Vc4CollectThermalZones(
    _Inout_ PRPI5VC4_PLATFORM_INFO PlatformInfo)
{
    PTHERMAL_INFORMATION Thermal;
    PRPI5VC4_THERMAL_ZONE_INFO Zone;
    PWSTR InterfaceList;
    PWSTR InterfaceName;
    ULONG_PTR Information;
    NTSTATUS Status;
    HANDLE Handle;
    ULONG Stamp;

    Status = IoGetDeviceInterfaces(&GUID_DEVICE_THERMAL_ZONE,
                                   NULL,
                                   0,
                                   &InterfaceList);
    if (!NT_SUCCESS(Status))
        return;

    for (InterfaceName = InterfaceList;
         *InterfaceName != UNICODE_NULL &&
             PlatformInfo->ThermalZoneCount <
                 RPI5VC4_PLATFORM_MAX_THERMAL_ZONES;
         InterfaceName += wcslen(InterfaceName) + 1)
    {
        Status = Rpi5Vc4OpenInterface(InterfaceName,
                                     FILE_READ_DATA,
                                     &Handle);
        if (!NT_SUCCESS(Status))
            continue;

        Zone = &PlatformInfo->ThermalZones[
            PlatformInfo->ThermalZoneCount];
        Thermal = ExAllocatePoolWithTag(PagedPool,
                                        sizeof(*Thermal),
                                        'tT5R');
        if (Thermal == NULL)
        {
            ZwClose(Handle);
            break;
        }

        RtlZeroMemory(Thermal, sizeof(*Thermal));
        Stamp = MAXULONG;
        Status = Rpi5Vc4DeviceIoControl(
                     Handle,
                     IOCTL_THERMAL_QUERY_INFORMATION,
                     &Stamp,
                     sizeof(Stamp),
                     Thermal,
                     sizeof(*Thermal),
                     &Information);
        ZwClose(Handle);
        if (NT_SUCCESS(Status) &&
            Information >= FIELD_OFFSET(THERMAL_INFORMATION,
                                        ActiveTripPoint) &&
            Thermal->CurrentTemperature != MAXULONG)
        {
            Zone->Flags = RPI5VC4_THERMAL_FLAG_PRESENT;
            Zone->CurrentTemperature = Thermal->CurrentTemperature;
            Zone->PassiveTripPoint = Thermal->PassiveTripPoint;
            Zone->CriticalTripPoint = Thermal->CriticalTripPoint;
            if (Thermal->PassiveTripPoint != MAXULONG)
            {
                Zone->Flags |= RPI5VC4_THERMAL_FLAG_PASSIVE_TRIP;
                if (Thermal->CurrentTemperature >= Thermal->PassiveTripPoint)
                    Zone->Flags |= RPI5VC4_THERMAL_FLAG_THERMAL_THROTTLE;
            }
            if (Thermal->CriticalTripPoint != MAXULONG)
                Zone->Flags |= RPI5VC4_THERMAL_FLAG_CRITICAL_TRIP;
            PlatformInfo->ThermalZoneCount++;
        }
        ExFreePoolWithTag(Thermal, 'tT5R');
    }

    ExFreePool(InterfaceList);
}

static
BOOLEAN
Rpi5Vc4ReadAcpiInteger(
    _In_reads_bytes_(OutputLength) PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputLength,
    _In_ ULONG Index,
    _Out_ PULONG Value)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    SIZE_T Length;
    ULONG Current;

    if (OutputLength < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ||
        Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ||
        Output->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ||
        Output->Length > OutputLength)
    {
        return FALSE;
    }

    Argument = &Output->Argument[0];
    End = (PUCHAR)Output + Output->Length;
    if (Output->Count == 1)
    {
        if ((PUCHAR)Argument > End ||
            (SIZE_T)(End - (PUCHAR)Argument) <
                FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) ||
            Argument->Type != ACPI_METHOD_ARGUMENT_PACKAGE ||
            Argument->DataLength >
                (SIZE_T)(End - Argument->Data))
        {
            return FALSE;
        }
        End = Argument->Data + Argument->DataLength;
        Argument = (PACPI_METHOD_ARGUMENT)Argument->Data;
    }
    else
    {
        if (Index >= Output->Count)
            return FALSE;
    }

    for (Current = 0; Current <= Index; Current++)
    {
        if ((PUCHAR)Argument > End ||
            (SIZE_T)(End - (PUCHAR)Argument) <
                FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data))
        {
            return FALSE;
        }
        Length = ACPI_METHOD_ARGUMENT_LENGTH(Argument->DataLength);
        if (Length > (SIZE_T)(End - (PUCHAR)Argument))
            return FALSE;
        if (Current == Index)
        {
            if (Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER ||
                Argument->DataLength < sizeof(ULONG))
            {
                return FALSE;
            }
            *Value = Argument->Argument;
            return TRUE;
        }
        Argument = (PACPI_METHOD_ARGUMENT)((PUCHAR)Argument + Length);
    }
    return FALSE;
}

static
VOID
Rpi5Vc4CollectFans(
    _Inout_ PRPI5VC4_PLATFORM_INFO PlatformInfo)
{
    UCHAR OutputStorage[256];
    PACPI_EVAL_OUTPUT_BUFFER Output = (PVOID)OutputStorage;
    ACPI_EVAL_INPUT_BUFFER Input;
    PRPI5VC4_FAN_INFO Fan;
    PWSTR InterfaceList;
    PWSTR InterfaceName;
    ULONG_PTR Information;
    NTSTATUS Status;
    HANDLE Handle;
    ULONG Value;

    Status = IoGetDeviceInterfaces(&GUID_DEVICE_FAN,
                                   NULL,
                                   0,
                                   &InterfaceList);
    if (!NT_SUCCESS(Status))
        return;

    for (InterfaceName = InterfaceList;
         *InterfaceName != UNICODE_NULL &&
             PlatformInfo->FanCount < RPI5VC4_PLATFORM_MAX_FANS;
         InterfaceName += wcslen(InterfaceName) + 1)
    {
        Status = Rpi5Vc4OpenInterface(InterfaceName,
                                     FILE_READ_DATA | FILE_WRITE_DATA,
                                     &Handle);
        if (!NT_SUCCESS(Status))
            continue;

        Fan = &PlatformInfo->Fans[PlatformInfo->FanCount++];
        Fan->Flags = RPI5VC4_FAN_FLAG_PRESENT;
        RtlZeroMemory(&Input, sizeof(Input));
        Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
        Input.MethodNameAsUlong = RPI5VC4_ACPI_METHOD('_', 'F', 'S', 'T');
        RtlZeroMemory(OutputStorage, sizeof(OutputStorage));
        Status = Rpi5Vc4DeviceIoControl(Handle,
                                       IOCTL_ACPI_EVAL_METHOD,
                                       &Input,
                                       sizeof(Input),
                                       Output,
                                       sizeof(OutputStorage),
                                       &Information);
        ZwClose(Handle);
        if (!NT_SUCCESS(Status))
            continue;

        if (Rpi5Vc4ReadAcpiInteger(Output,
                                  (ULONG)Information,
                                  1,
                                  &Value) &&
            Value != MAXULONG)
        {
            Fan->Control = Value;
            Fan->Flags |= RPI5VC4_FAN_FLAG_CONTROL_VALID;
        }
        if (Rpi5Vc4ReadAcpiInteger(Output,
                                  (ULONG)Information,
                                  2,
                                  &Value) &&
            Value != MAXULONG)
        {
            Fan->Speed = Value;
            Fan->Flags |= RPI5VC4_FAN_FLAG_SPEED_VALID;
        }
    }

    ExFreePool(InterfaceList);
}

VP_STATUS
Rpi5Vc4QueryPlatformInfo(
    _Out_ PRPI5VC4_PLATFORM_INFO PlatformInfo)
{
    VideoPortZeroMemory(PlatformInfo, sizeof(*PlatformInfo));
    PlatformInfo->Size = sizeof(*PlatformInfo);
    PlatformInfo->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return ERROR_INVALID_FUNCTION;
    Rpi5Vc4CollectThermalZones(PlatformInfo);
    Rpi5Vc4CollectFans(PlatformInfo);
    return NO_ERROR;
}
