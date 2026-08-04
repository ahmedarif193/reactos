/*
 * PROJECT:     ReactOS ACPI Time and Alarm Control Utility
 * LICENSE:     GPL-2.0-or-later
 */

#include <windows.h>
#include <setupapi.h>
#include <stdio.h>
#include <wchar.h>
#include <initguid.h>
#include <reactos/drivers/acpitime.h>

static
HANDLE
AcpiTimeCtlOpenDevice(VOID)
{
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    PSP_DEVICE_INTERFACE_DETAIL_DATA_W DetailData = NULL;
    HDEVINFO DeviceInfo;
    DWORD RequiredSize = 0;
    HANDLE Device = INVALID_HANDLE_VALUE;

    DeviceInfo = SetupDiGetClassDevsW(&GUID_DEVINTERFACE_REACTOS_ACPITIME, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DeviceInfo == INVALID_HANDLE_VALUE)
        return INVALID_HANDLE_VALUE;
    ZeroMemory(&InterfaceData, sizeof(InterfaceData));
    InterfaceData.cbSize = sizeof(InterfaceData);
    if (!SetupDiEnumDeviceInterfaces(DeviceInfo, NULL, &GUID_DEVINTERFACE_REACTOS_ACPITIME, 0, &InterfaceData))
        goto Cleanup;
    SetupDiGetDeviceInterfaceDetailW(DeviceInfo, &InterfaceData, NULL, 0, &RequiredSize, NULL);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || RequiredSize < sizeof(*DetailData))
        goto Cleanup;
    DetailData = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequiredSize);
    if (!DetailData)
        goto Cleanup;
    DetailData->cbSize = sizeof(*DetailData);
    if (!SetupDiGetDeviceInterfaceDetailW(DeviceInfo, &InterfaceData, DetailData, RequiredSize, NULL, NULL))
        goto Cleanup;
    Device = CreateFileW(DetailData->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

Cleanup:
    if (DetailData)
        HeapFree(GetProcessHeap(), 0, DetailData);
    SetupDiDestroyDeviceInfoList(DeviceInfo);
    return Device;
}

static
BOOL
AcpiTimeCtlQueryInformation(
    _In_ HANDLE Device,
    _Out_ PACPITIME_DEVICE_INFORMATION Information)
{
    DWORD BytesReturned;

    ZeroMemory(Information, sizeof(*Information));
    if (!DeviceIoControl(Device, IOCTL_ACPITIME_QUERY_INFORMATION, NULL, 0, Information, sizeof(*Information), &BytesReturned, NULL))
        return FALSE;
    return BytesReturned == sizeof(*Information) && Information->Version == ACPITIME_DRIVER_INTERFACE_VERSION;
}

static
BOOL
AcpiTimeCtlGetTime(
    _In_ HANDLE Device,
    _Out_ PACPITIME_TIME_INFORMATION Time)
{
    DWORD BytesReturned;

    ZeroMemory(Time, sizeof(*Time));
    return DeviceIoControl(Device, IOCTL_ACPITIME_GET_TIME, NULL, 0, Time, sizeof(*Time), &BytesReturned, NULL) && BytesReturned == sizeof(*Time);
}

static
BOOL
AcpiTimeCtlGetTimer(
    _In_ HANDLE Device,
    _In_ ULONG Timer,
    _Out_ PACPITIME_TIMER_INFORMATION Information)
{
    DWORD BytesReturned;

    ZeroMemory(Information, sizeof(*Information));
    Information->Timer = Timer;
    return DeviceIoControl(Device, IOCTL_ACPITIME_GET_TIMER, &Timer, sizeof(Timer), Information, sizeof(*Information), &BytesReturned, NULL) && BytesReturned == sizeof(*Information);
}

static
VOID
AcpiTimeCtlPrintTime(
    _In_ PACPITIME_TIME_INFORMATION Time)
{
    if (Time->Timezone == 2047)
        wprintf(L"Time:         %04u-%02u-%02u %02u:%02u:%02u.%03u (timezone unspecified, daylight=%u)\n", Time->Year, Time->Month, Time->Day, Time->Hour, Time->Minute, Time->Second, Time->Milliseconds, Time->Daylight);
    else
        wprintf(L"Time:         %04u-%02u-%02u %02u:%02u:%02u.%03u (UTC-local=%d minutes, daylight=%u)\n", Time->Year, Time->Month, Time->Day, Time->Hour, Time->Minute, Time->Second, Time->Milliseconds, Time->Timezone, Time->Daylight);
}

static
VOID
AcpiTimeCtlPrintTimer(
    _In_ PACPITIME_TIMER_INFORMATION Information)
{
    PCWSTR Name = Information->Timer == ACPITIME_TIMER_AC ? L"AC" : L"DC";

    wprintf(L"%ls timer:     status=0x%08lx (%ls, %ls) value=", Name, Information->Status, (Information->Status & 1) ? L"expired" : L"pending", (Information->Status & 2) ? L"caused wake" : L"no wake");
    if (Information->Value == ACPITIME_TIMER_DISABLED)
        wprintf(L"disabled");
    else
        wprintf(L"%lu seconds", Information->Value);
    if (Information->Policy == ACPITIME_TIMER_DISABLED)
        wprintf(L" policy=never\n");
    else
        wprintf(L" policy=%lu seconds\n", Information->Policy);
}

static
BOOL
AcpiTimeCtlStatus(
    _In_ HANDLE Device)
{
    ACPITIME_DEVICE_INFORMATION DeviceInformation;
    ACPITIME_TIMER_INFORMATION TimerInformation;
    ACPITIME_TIME_INFORMATION Time;
    BOOL Result = TRUE;

    if (!AcpiTimeCtlQueryInformation(Device, &DeviceInformation))
    {
        wprintf(L"Capability query failed: %lu\n", GetLastError());
        return FALSE;
    }
    wprintf(L"Capabilities:  0x%03lx  AC=%ls DC=%ls clock=%ls millisecond=%ls S4/S5-status=%ls\n", DeviceInformation.Capabilities, (DeviceInformation.Capabilities & ACPITIME_CAP_AC_WAKE) ? L"yes" : L"no", (DeviceInformation.Capabilities & ACPITIME_CAP_DC_WAKE) ? L"yes" : L"no", (DeviceInformation.Capabilities & ACPITIME_CAP_REAL_TIME) ? L"yes" : L"no", (DeviceInformation.Capabilities & ACPITIME_CAP_MILLISECOND_TIME) ? L"yes" : L"no", (DeviceInformation.Capabilities & ACPITIME_CAP_S4_S5_WAKE_STATUS) ? L"yes" : L"no");
    if (DeviceInformation.Capabilities & ACPITIME_CAP_REAL_TIME)
    {
        if (AcpiTimeCtlGetTime(Device, &Time))
            AcpiTimeCtlPrintTime(&Time);
        else
        {
            wprintf(L"Time query failed: %lu\n", GetLastError());
            Result = FALSE;
        }
    }
    if (DeviceInformation.Capabilities & ACPITIME_CAP_AC_WAKE)
    {
        if (AcpiTimeCtlGetTimer(Device, ACPITIME_TIMER_AC, &TimerInformation))
            AcpiTimeCtlPrintTimer(&TimerInformation);
        else
        {
            wprintf(L"AC timer query failed: %lu\n", GetLastError());
            Result = FALSE;
        }
    }
    if (DeviceInformation.Capabilities & ACPITIME_CAP_DC_WAKE)
    {
        if (AcpiTimeCtlGetTimer(Device, ACPITIME_TIMER_DC, &TimerInformation))
            AcpiTimeCtlPrintTimer(&TimerInformation);
        else
        {
            wprintf(L"DC timer query failed: %lu\n", GetLastError());
            Result = FALSE;
        }
    }
    return Result;
}

static
BOOL
AcpiTimeCtlParseTimer(
    _In_ PCWSTR Text,
    _Out_ PULONG Timer)
{
    if (_wcsicmp(Text, L"ac") == 0)
        *Timer = ACPITIME_TIMER_AC;
    else if (_wcsicmp(Text, L"dc") == 0)
        *Timer = ACPITIME_TIMER_DC;
    else
        return FALSE;
    return TRUE;
}

static
BOOL
AcpiTimeCtlParseUlong(
    _In_ PCWSTR Text,
    _Out_ PULONG Value)
{
    PWSTR End;
    ULONG Parsed;

    if (!Text[0] || Text[0] == L'-')
        return FALSE;
    Parsed = wcstoul(Text, &End, 0);
    if (*End != UNICODE_NULL)
        return FALSE;
    *Value = Parsed;
    return TRUE;
}

static
BOOL
AcpiTimeCtlSetTimer(
    _In_ HANDLE Device,
    _In_ INT argc,
    _In_reads_(argc) WCHAR **argv)
{
    ACPITIME_TIMER_SET TimerSet;
    DWORD BytesReturned;

    if (argc < 4 || argc > 5 || !AcpiTimeCtlParseTimer(argv[2], &TimerSet.Timer))
        return FALSE;
    if (_wcsicmp(argv[3], L"off") == 0 || _wcsicmp(argv[3], L"disabled") == 0)
    {
        TimerSet.Value = ACPITIME_TIMER_DISABLED;
        TimerSet.Policy = ACPITIME_TIMER_DISABLED;
    }
    else if (!AcpiTimeCtlParseUlong(argv[3], &TimerSet.Value))
        return FALSE;
    else
        TimerSet.Policy = 0;
    if (argc == 5 && !AcpiTimeCtlParseUlong(argv[4], &TimerSet.Policy))
        return FALSE;
    if (!DeviceIoControl(Device, IOCTL_ACPITIME_SET_TIMER, &TimerSet, sizeof(TimerSet), NULL, 0, &BytesReturned, NULL))
    {
        wprintf(L"Timer programming failed: %lu\n", GetLastError());
        return FALSE;
    }
    wprintf(L"%ls timer programmed.\n", TimerSet.Timer == ACPITIME_TIMER_AC ? L"AC" : L"DC");
    return TRUE;
}

static
BOOL
AcpiTimeCtlClearTimer(
    _In_ HANDLE Device,
    _In_ PCWSTR TimerText)
{
    DWORD BytesReturned;
    ULONG Timer;

    if (!AcpiTimeCtlParseTimer(TimerText, &Timer))
        return FALSE;
    if (!DeviceIoControl(Device, IOCTL_ACPITIME_CLEAR_STATUS, &Timer, sizeof(Timer), NULL, 0, &BytesReturned, NULL))
    {
        wprintf(L"Wake-status clear failed: %lu\n", GetLastError());
        return FALSE;
    }
    wprintf(L"%ls wake status cleared.\n", Timer == ACPITIME_TIMER_AC ? L"AC" : L"DC");
    return TRUE;
}

static
BOOL
AcpiTimeCtlSetTime(
    _In_ HANDLE Device,
    _In_ INT argc,
    _In_reads_(argc) WCHAR **argv)
{
    ACPITIME_TIME_INFORMATION Time;
    DWORD BytesReturned;
    ULONG Year, Month, Day, Hour, Minute, Second;
    LONG Timezone = 2047;
    ULONG Daylight = 0;
    WCHAR Extra;
    PWSTR End;

    if (argc < 4 || argc > 6 || swscanf(argv[2], L"%lu-%lu-%lu%c", &Year, &Month, &Day, &Extra) != 3 || swscanf(argv[3], L"%lu:%lu:%lu%c", &Hour, &Minute, &Second, &Extra) != 3)
        return FALSE;
    if (argc >= 5)
    {
        if (!argv[4][0])
            return FALSE;
        Timezone = wcstol(argv[4], &End, 0);
        if (*End != UNICODE_NULL || Timezone < -1440 || (Timezone > 1440 && Timezone != 2047))
            return FALSE;
    }
    if (argc == 6 && (!AcpiTimeCtlParseUlong(argv[5], &Daylight) || Daylight > 3))
        return FALSE;
    if (Year > 0xFFFF || Month > 0xFF || Day > 0xFF || Hour > 0xFF || Minute > 0xFF || Second > 0xFF)
        return FALSE;
    ZeroMemory(&Time, sizeof(Time));
    Time.Year = (USHORT)Year;
    Time.Month = (UCHAR)Month;
    Time.Day = (UCHAR)Day;
    Time.Hour = (UCHAR)Hour;
    Time.Minute = (UCHAR)Minute;
    Time.Second = (UCHAR)Second;
    Time.Valid = 1;
    Time.Timezone = (SHORT)Timezone;
    Time.Daylight = (UCHAR)Daylight;
    if (!DeviceIoControl(Device, IOCTL_ACPITIME_SET_TIME, &Time, sizeof(Time), NULL, 0, &BytesReturned, NULL))
    {
        wprintf(L"Clock programming failed: %lu\n", GetLastError());
        return FALSE;
    }
    wprintf(L"Firmware clock programmed.\n");
    return TRUE;
}

static
VOID
AcpiTimeCtlUsage(VOID)
{
    wprintf(L"Usage:\n");
    wprintf(L"  acpitimectl status\n");
    wprintf(L"  acpitimectl set-timer <ac|dc> <seconds|off> [policy-seconds]\n");
    wprintf(L"  acpitimectl clear <ac|dc>\n");
    wprintf(L"  acpitimectl set-time <YYYY-MM-DD> <HH:MM:SS> [UTC-local-minutes] [daylight-mask]\n");
}

INT
wmain(
    _In_ INT argc,
    _In_reads_(argc) WCHAR **argv)
{
    HANDLE Device;
    BOOL Result = FALSE;

    Device = AcpiTimeCtlOpenDevice();
    if (Device == INVALID_HANDLE_VALUE)
    {
        wprintf(L"ACPI time and alarm device is unavailable (error %lu).\n", GetLastError());
        return 1;
    }
    if (argc == 1 || (argc == 2 && _wcsicmp(argv[1], L"status") == 0))
        Result = AcpiTimeCtlStatus(Device);
    else if (_wcsicmp(argv[1], L"set-timer") == 0)
        Result = AcpiTimeCtlSetTimer(Device, argc, argv);
    else if (_wcsicmp(argv[1], L"clear") == 0 && argc == 3)
        Result = AcpiTimeCtlClearTimer(Device, argv[2]);
    else if (_wcsicmp(argv[1], L"set-time") == 0)
        Result = AcpiTimeCtlSetTime(Device, argc, argv);
    else
        AcpiTimeCtlUsage();
    CloseHandle(Device);
    return Result ? 0 : 1;
}
