/*
 * PROJECT:     ReactOS fan control utility
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Control ACPI thermal zones through the Windows thermal ABI
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#define WIN32_NO_STATUS
#include <windows.h>
#include <setupapi.h>
#include <initguid.h>
#include <poclass.h>
#include <acpiioct.h>
#include <reactos/drivers/acpi/acpithermal.h>
#include <stdio.h>
#include <stdlib.h>

typedef BOOL (*FANCTL_VISITOR)(HANDLE Device, ULONG Index, PVOID Context);

typedef struct _FANCTL_IOCTL_CONTEXT
{
    DWORD IoControlCode;
    PVOID Input;
    DWORD InputLength;
    ULONG Succeeded;
    DWORD LastError;
} FANCTL_IOCTL_CONTEXT, *PFANCTL_IOCTL_CONTEXT;

typedef struct _FANCTL_STATUS_CONTEXT
{
    ULONG Succeeded;
    DWORD LastError;
} FANCTL_STATUS_CONTEXT, *PFANCTL_STATUS_CONTEXT;

#define FANCTL_FIF_ARGUMENT_COUNT 4
#define FANCTL_FPS_ARGUMENT_COUNT 5
#define FANCTL_FPS_CONTROL 0
#define FANCTL_FPS_TRIP_POINT 1
#define FANCTL_FPS_SPEED 2
#define FANCTL_MAX_PERCENT 100

static BOOL FanctlGetIntegerArgument(PACPI_METHOD_ARGUMENT Argument, PUCHAR End, PULONG Value);

static VOID FanctlUsage(VOID)
{
    wprintf(L"ReactOS ACPI fan control (Windows thermal ABI)\n\n");
    wprintf(L"Usage:\n");
    wprintf(L"  fanctl status\n");
    wprintf(L"  fanctl max\n");
    wprintf(L"  fanctl level <0-10>    0=max, 10=active cooling off\n");
    wprintf(L"  fanctl policy <active|passive>\n");
    wprintf(L"  fanctl passive <0-100>\n");
}

static BOOL FanctlParseUchar(PCWSTR Text, ULONG Maximum, PUCHAR Value)
{
    PWSTR End;
    ULONG Parsed;

    if (!Text || !*Text)
        return FALSE;
    Parsed = wcstoul(Text, &End, 10);
    if (*End || Parsed > Maximum)
        return FALSE;
    *Value = (UCHAR)Parsed;
    return TRUE;
}

static VOID FanctlPrintTemperature(PCWSTR Label, ULONG Temperature)
{
    LONGLONG CelsiusHundredths;
    LONGLONG Absolute;

    if (Temperature == (ULONG)-1)
    {
        wprintf(L"  %-12ls disabled\n", Label);
        return;
    }

    CelsiusHundredths = (LONGLONG)Temperature * 10 - 27315;
    Absolute = CelsiusHundredths < 0 ? -CelsiusHundredths : CelsiusHundredths;
    wprintf(L"  %-12ls %ls%I64u.%02I64u C (%lu x 0.1 K)\n", Label, CelsiusHundredths < 0 ? L"-" : L"", Absolute / 100, Absolute % 100, Temperature);
}

static BOOL FanctlVisitInterfaces(const GUID *InterfaceGuid, FANCTL_VISITOR Visitor, PVOID Context, PULONG Found)
{
    SP_DEVICE_INTERFACE_DETAIL_DATA_W *Detail;
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    HDEVINFO DeviceInfo;
    DWORD Required;
    ULONG Index;
    HANDLE Device;
    BOOL Result = TRUE;

    *Found = 0;
    DeviceInfo = SetupDiGetClassDevsW(InterfaceGuid, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DeviceInfo == INVALID_HANDLE_VALUE)
        return FALSE;

    for (Index = 0;; Index++)
    {
        ZeroMemory(&InterfaceData, sizeof(InterfaceData));
        InterfaceData.cbSize = sizeof(InterfaceData);
        if (!SetupDiEnumDeviceInterfaces(DeviceInfo, NULL, InterfaceGuid, Index, &InterfaceData))
        {
            if (GetLastError() != ERROR_NO_MORE_ITEMS)
                Result = FALSE;
            break;
        }

        Required = 0;
        SetupDiGetDeviceInterfaceDetailW(DeviceInfo, &InterfaceData, NULL, 0, &Required, NULL);
        if (!Required)
        {
            Result = FALSE;
            continue;
        }

        Detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, Required);
        if (!Detail)
        {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
            Result = FALSE;
            break;
        }

        Detail->cbSize = sizeof(*Detail);
        if (!SetupDiGetDeviceInterfaceDetailW(DeviceInfo, &InterfaceData, Detail, Required, NULL, NULL))
        {
            HeapFree(GetProcessHeap(), 0, Detail);
            Result = FALSE;
            continue;
        }

        (*Found)++;
        Device = CreateFileW(Detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (Device == INVALID_HANDLE_VALUE)
        {
            fwprintf(stderr, L"Unable to open device %lu (error %lu).\n", Index, GetLastError());
            Result = FALSE;
        }
        else
        {
            if (!Visitor(Device, Index, Context))
                Result = FALSE;
            CloseHandle(Device);
        }
        HeapFree(GetProcessHeap(), 0, Detail);
    }

    SetupDiDestroyDeviceInfoList(DeviceInfo);
    return Result;
}

static BOOL FanctlIoctlVisitor(HANDLE Device, ULONG Index, PVOID Context)
{
    PFANCTL_IOCTL_CONTEXT Ioctl = Context;
    DWORD Returned;

    if (!DeviceIoControl(Device, Ioctl->IoControlCode, Ioctl->Input, Ioctl->InputLength, NULL, 0, &Returned, NULL))
    {
        Ioctl->LastError = GetLastError();
        fwprintf(stderr, L"Thermal zone %lu rejected the request (error %lu).\n", Index, Ioctl->LastError);
        return FALSE;
    }

    Ioctl->Succeeded++;
    return TRUE;
}

static BOOL FanctlStatusVisitor(HANDLE Device, ULONG Index, PVOID Context)
{
    PFANCTL_STATUS_CONTEXT StatusContext = Context;
    ACPI_THERMAL_INFORMATION_EX Information;
    DWORD Returned;
    ULONG Stamp = (ULONG)-1;
    ULONG Trip;

    ZeroMemory(&Information, sizeof(Information));
    if (!DeviceIoControl(Device, IOCTL_THERMAL_QUERY_INFORMATION, &Stamp, sizeof(Stamp), &Information, sizeof(Information), &Returned, NULL))
    {
        StatusContext->LastError = GetLastError();
        fwprintf(stderr, L"Thermal zone %lu query failed (error %lu).\n", Index, StatusContext->LastError);
        return FALSE;
    }
    if (Returned < sizeof(Information))
    {
        StatusContext->LastError = ERROR_INVALID_DATA;
        fwprintf(stderr, L"Thermal zone %lu returned %lu bytes; expected %Iu.\n", Index, Returned, sizeof(Information));
        return FALSE;
    }

    wprintf(L"Thermal zone %lu (stamp %lu):\n", Index, Information.ThermalStamp);
    FanctlPrintTemperature(L"current", Information.CurrentTemperature);
    FanctlPrintTemperature(L"passive", Information.PassiveTripPoint);
    FanctlPrintTemperature(L"critical", Information.CriticalTripPoint);
    FanctlPrintTemperature(L"S4/hot", Information.S4TransitionTripPoint);
    wprintf(L"  active trips %u; passive devices %ls; poll %lu ms\n", Information.ActiveTripPointCount, Information.PassiveCoolingDevicesPresent ? L"yes" : L"no", Information.PollingPeriod);
    for (Trip = 0; Trip < Information.ActiveTripPointCount; Trip++)
    {
        WCHAR Label[16];
        _snwprintf(Label, ARRAYSIZE(Label), L"active %lu", Trip);
        Label[ARRAYSIZE(Label) - 1] = UNICODE_NULL;
        FanctlPrintTemperature(Label, Information.ActiveTripPoint[Trip]);
    }
    StatusContext->Succeeded++;
    return TRUE;
}

static BOOL FanctlFanStatusVisitor(HANDLE Device, ULONG Index, PVOID Context)
{
    PFANCTL_STATUS_CONTEXT StatusContext = Context;
    ACPI_EVAL_INPUT_BUFFER Input;
    BYTE OutputBytes[256];
    PACPI_EVAL_OUTPUT_BUFFER Output = (PACPI_EVAL_OUTPUT_BUFFER)OutputBytes;
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    DWORD Returned;
    ULONG Revision;
    ULONG Control;
    ULONG Speed;

    ZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    CopyMemory(Input.MethodName, "_FST", sizeof(Input.MethodName));
    if (!DeviceIoControl(Device, IOCTL_ACPI_EVAL_METHOD, &Input, sizeof(Input), Output, sizeof(OutputBytes), &Returned, NULL))
    {
        StatusContext->LastError = GetLastError();
        return FALSE;
    }
    if (Returned < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) || Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Output->Count < 3)
    {
        StatusContext->LastError = ERROR_INVALID_DATA;
        return FALSE;
    }

    End = OutputBytes + Returned;
    Argument = Output->Argument;
    if (!FanctlGetIntegerArgument(Argument, End, &Revision))
        return FALSE;
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    if (!FanctlGetIntegerArgument(Argument, End, &Control))
        return FALSE;
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    if (!FanctlGetIntegerArgument(Argument, End, &Speed))
        return FALSE;
    wprintf(L"Fan %lu: revision %lu, control %lu, speed %ls", Index, Revision, Control, Speed == (ULONG)-1 ? L"unknown" : L"");
    if (Speed != (ULONG)-1)
        wprintf(L"%lu RPM", Speed);
    wprintf(L"\n");
    StatusContext->Succeeded++;
    return TRUE;
}

static BOOL FanctlEvaluateMethod(HANDLE Device, PCSTR MethodName, PBYTE OutputBytes, DWORD OutputSize, PDWORD Returned)
{
    ACPI_EVAL_INPUT_BUFFER Input;

    ZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    CopyMemory(Input.MethodName, MethodName, sizeof(Input.MethodName));
    return DeviceIoControl(Device, IOCTL_ACPI_EVAL_METHOD, &Input, sizeof(Input), OutputBytes, OutputSize, Returned, NULL);
}

static BOOL FanctlGetIntegerArgument(PACPI_METHOD_ARGUMENT Argument, PUCHAR End, PULONG Value)
{
    if ((PUCHAR)Argument > End || (SIZE_T)(End - (PUCHAR)Argument) < FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) || Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER || Argument->DataLength < sizeof(ULONG))
        return FALSE;
    *Value = Argument->Argument;
    return TRUE;
}

static BOOL FanctlGetMaximumFanControl(HANDLE Device, PULONG Control, PDWORD LastError)
{
    BYTE OutputBytes[4096];
    PACPI_EVAL_OUTPUT_BUFFER Output = (PACPI_EVAL_OUTPUT_BUFFER)OutputBytes;
    PACPI_METHOD_ARGUMENT Argument;
    PACPI_METHOD_ARGUMENT Element;
    PUCHAR End;
    PUCHAR PackageEnd;
    DWORD Returned;
    ULONG Values[FANCTL_FPS_ARGUMENT_COUNT];
    ULONG MaximumSpeed = 0;
    ULONG MaximumSpeedControl = 0;
    ULONG Ac0Control = 0;
    ULONG Value;
    ULONG Index;
    ULONG ElementIndex;
    BOOL HaveMaximumSpeed = FALSE;
    BOOL HaveAc0 = FALSE;

    if (!FanctlEvaluateMethod(Device, "_FIF", OutputBytes, sizeof(OutputBytes), &Returned))
        goto failed;
    End = OutputBytes + Returned;
    if (Returned < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) || Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Output->Count < FANCTL_FIF_ARGUMENT_COUNT)
        goto invalid;
    Argument = Output->Argument;
    for (Index = 0; Index < FANCTL_FIF_ARGUMENT_COUNT; Index++)
    {
        if (!FanctlGetIntegerArgument(Argument, End, &Value))
            goto invalid;
        if (Index == 0 && Value != 0)
            goto invalid;
        if (Index == 1 && Value != 0)
        {
            *Control = FANCTL_MAX_PERCENT;
            return TRUE;
        }
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }

    if (!FanctlEvaluateMethod(Device, "_FPS", OutputBytes, sizeof(OutputBytes), &Returned))
        goto failed;
    End = OutputBytes + Returned;
    if (Returned < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) || Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Output->Count < 2)
        goto invalid;
    Argument = Output->Argument;
    if (!FanctlGetIntegerArgument(Argument, End, &Value) || Value != 0)
        goto invalid;
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    for (Index = 1; Index < Output->Count; Index++)
    {
        if ((PUCHAR)Argument > End || (SIZE_T)(End - (PUCHAR)Argument) < FIELD_OFFSET(ACPI_METHOD_ARGUMENT, Data) || Argument->Type != ACPI_METHOD_ARGUMENT_PACKAGE || Argument->DataLength > (USHORT)(End - Argument->Data))
            goto invalid;
        Element = (PACPI_METHOD_ARGUMENT)Argument->Data;
        PackageEnd = Argument->Data + Argument->DataLength;
        for (ElementIndex = 0; ElementIndex < FANCTL_FPS_ARGUMENT_COUNT; ElementIndex++)
        {
            if (!FanctlGetIntegerArgument(Element, PackageEnd, &Values[ElementIndex]))
                goto invalid;
            Element = ACPI_METHOD_NEXT_ARGUMENT(Element);
        }
        if (Values[FANCTL_FPS_TRIP_POINT] == 0 && !HaveAc0)
        {
            Ac0Control = Values[FANCTL_FPS_CONTROL];
            HaveAc0 = TRUE;
        }
        if (Values[FANCTL_FPS_SPEED] != (ULONG)-1 && (!HaveMaximumSpeed || Values[FANCTL_FPS_SPEED] > MaximumSpeed))
        {
            MaximumSpeed = Values[FANCTL_FPS_SPEED];
            MaximumSpeedControl = Values[FANCTL_FPS_CONTROL];
            HaveMaximumSpeed = TRUE;
        }
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }

    if (HaveMaximumSpeed)
        *Control = MaximumSpeedControl;
    else if (HaveAc0)
        *Control = Ac0Control;
    else
        goto invalid;
    return TRUE;

invalid:
    *LastError = ERROR_INVALID_DATA;
    return FALSE;

failed:
    *LastError = GetLastError();
    return FALSE;
}

static BOOL FanctlForceFanVisitor(HANDLE Device, ULONG Index, PVOID Context)
{
    PFANCTL_STATUS_CONTEXT StatusContext = Context;
    ACPI_EVAL_INPUT_BUFFER_SIMPLE_INTEGER Input;
    DWORD Returned;
    ULONG Control;

    if (!FanctlGetMaximumFanControl(Device, &Control, &StatusContext->LastError))
    {
        fwprintf(stderr, L"Fan %lu cannot determine a safe maximum control (error %lu).\n", Index, StatusContext->LastError);
        return FALSE;
    }

    ZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIMPLE_INTEGER_SIGNATURE;
    CopyMemory(Input.MethodName, "_FSL", sizeof(Input.MethodName));
    Input.IntegerArgument = Control;
    if (!DeviceIoControl(Device, IOCTL_ACPI_EVAL_METHOD, &Input, sizeof(Input), NULL, 0, &Returned, NULL))
    {
        StatusContext->LastError = GetLastError();
        fwprintf(stderr, L"Fan %lu _FSL(%lu) fallback failed (error %lu).\n", Index, Control, StatusContext->LastError);
        return FALSE;
    }
    wprintf(L"Fan %lu: maximum control %lu applied.\n", Index, Control);
    StatusContext->Succeeded++;
    return TRUE;
}

static int FanctlStatus(VOID)
{
    FANCTL_STATUS_CONTEXT Thermal = {0};
    FANCTL_STATUS_CONTEXT Fans = {0};
    ULONG ThermalFound;
    ULONG FansFound;

    FanctlVisitInterfaces(&GUID_DEVICE_THERMAL_ZONE, FanctlStatusVisitor, &Thermal, &ThermalFound);
    FanctlVisitInterfaces(&GUID_DEVICE_FAN, FanctlFanStatusVisitor, &Fans, &FansFound);
    if (!ThermalFound)
        fwprintf(stderr, L"No ACPI thermal-zone interface was found.\n");
    if (FansFound && !Fans.Succeeded)
        fwprintf(stderr, L"Fan telemetry is unavailable (_FST is optional).\n");
    return Thermal.Succeeded ? 0 : 1;
}

static int FanctlSendThermalIoctl(DWORD IoControlCode, PUCHAR Value, BOOL AllowMaximumFallback)
{
    FANCTL_IOCTL_CONTEXT Ioctl;
    FANCTL_STATUS_CONTEXT Fallback = {0};
    ULONG ThermalFound;
    ULONG FanFound = 0;

    ZeroMemory(&Ioctl, sizeof(Ioctl));
    Ioctl.IoControlCode = IoControlCode;
    Ioctl.Input = Value;
    Ioctl.InputLength = sizeof(*Value);
    FanctlVisitInterfaces(&GUID_DEVICE_THERMAL_ZONE, FanctlIoctlVisitor, &Ioctl, &ThermalFound);
    if (Ioctl.Succeeded)
    {
        wprintf(L"Applied to %lu thermal zone(s).\n", Ioctl.Succeeded);
        return 0;
    }

    if (AllowMaximumFallback)
    {
        FanctlVisitInterfaces(&GUID_DEVICE_FAN, FanctlForceFanVisitor, &Fallback, &FanFound);
        if (Fallback.Succeeded)
        {
            wprintf(L"Maximum fan control applied through validated _FIF/_FPS data to %lu fan(s).\n", Fallback.Succeeded);
            return 0;
        }
    }

    if (!ThermalFound)
        fwprintf(stderr, L"No ACPI thermal-zone interface was found.\n");
    if (AllowMaximumFallback && !FanFound)
        fwprintf(stderr, L"No ACPI fan interface was found for the safe fallback.\n");
    return 1;
}

int wmain(int argc, WCHAR *argv[])
{
    UCHAR Value;

    if (argc == 2 && !_wcsicmp(argv[1], L"status"))
        return FanctlStatus();
    if (argc == 2 && !_wcsicmp(argv[1], L"max"))
    {
        Value = 0;
        return FanctlSendThermalIoctl(IOCTL_RUN_ACTIVE_COOLING_METHOD, &Value, TRUE);
    }
    if (argc == 3 && !_wcsicmp(argv[1], L"level"))
    {
        if (!FanctlParseUchar(argv[2], MAX_ACTIVE_COOLING_LEVELS, &Value))
        {
            fwprintf(stderr, L"Level must be 0 through 10.\n");
            return 2;
        }
        return FanctlSendThermalIoctl(IOCTL_RUN_ACTIVE_COOLING_METHOD, &Value, Value == 0);
    }
    if (argc == 3 && !_wcsicmp(argv[1], L"passive"))
    {
        if (!FanctlParseUchar(argv[2], 100, &Value))
        {
            fwprintf(stderr, L"Passive limit must be 0 through 100.\n");
            return 2;
        }
        return FanctlSendThermalIoctl(IOCTL_THERMAL_SET_PASSIVE_LIMIT, &Value, FALSE);
    }
    if (argc == 3 && !_wcsicmp(argv[1], L"policy"))
    {
        if (!_wcsicmp(argv[2], L"active"))
            Value = ACTIVE_COOLING;
        else if (!_wcsicmp(argv[2], L"passive"))
            Value = PASSIVE_COOLING;
        else
        {
            fwprintf(stderr, L"Policy must be active or passive.\n");
            return 2;
        }
        return FanctlSendThermalIoctl(IOCTL_THERMAL_SET_COOLING_POLICY, &Value, FALSE);
    }

    FanctlUsage();
    return 2;
}
