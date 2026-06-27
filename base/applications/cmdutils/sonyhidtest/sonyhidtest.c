/*
 * PROJECT:     ReactOS Sony HID diagnostics
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Exercise Sony DualShock/DualSense vendor output reports
 */

#include <windef.h>
#include <winbase.h>
#include <winuser.h>
#include <winreg.h>
#include <setupapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <stdarg.h>

#include "ddk/hidsdi.h"

#define SONY_VENDOR_ID                  0x054c

#define SONY_PRODUCT_DUALSHOCK4_V1      0x05c4
#define SONY_PRODUCT_DUALSHOCK4_V2      0x09cc
#define SONY_PRODUCT_DUALSHOCK4_DONGLE  0x0ba0
#define SONY_PRODUCT_DUALSENSE          0x0ce6
#define SONY_PRODUCT_DUALSENSE_EDGE     0x0df2

#define DS4_OUTPUT_REPORT_USB           0x05
#define DS4_OUTPUT_REPORT_BT            0x11
#define DS4_OUTPUT_HWCTL_CRC32          0x40
#define DS4_OUTPUT_HWCTL_HID            0x80
#define DS4_OUTPUT_VALID_MOTOR          0x01
#define DS4_OUTPUT_VALID_LED            0x02
#define DS4_OUTPUT_COMMON_VALID0        0
#define DS4_OUTPUT_COMMON_MOTOR_RIGHT   3
#define DS4_OUTPUT_COMMON_MOTOR_LEFT    4
#define DS4_OUTPUT_COMMON_LIGHTBAR_RED  5
#define DS4_OUTPUT_COMMON_LIGHTBAR_GREEN 6
#define DS4_OUTPUT_COMMON_LIGHTBAR_BLUE 7
#define DS4_OUTPUT_USB_COMMON_OFFSET    1
#define DS4_OUTPUT_BT_COMMON_OFFSET     3

#define DS_OUTPUT_REPORT_USB            0x02
#define DS_OUTPUT_REPORT_BT             0x31
#define DS_OUTPUT_TAG                   0x10
#define DS_OUTPUT_VALID_FLAG0_COMPATIBLE_VIBRATION 0x01
#define DS_OUTPUT_VALID_FLAG0_HAPTICS_SELECT       0x02
#define DS_OUTPUT_VALID_FLAG0_RIGHT_TRIGGER        0x04
#define DS_OUTPUT_VALID_FLAG0_LEFT_TRIGGER         0x08
#define DS_OUTPUT_VALID_FLAG1_LIGHTBAR_CONTROL     0x04
#define DS_OUTPUT_VALID_FLAG1_PLAYER_LEDS          0x10
#define DS_OUTPUT_COMMON_VALID0        0
#define DS_OUTPUT_COMMON_VALID1        1
#define DS_OUTPUT_COMMON_MOTOR_RIGHT   2
#define DS_OUTPUT_COMMON_MOTOR_LEFT    3
#define DS_OUTPUT_COMMON_RIGHT_TRIGGER_EFFECT 10
#define DS_OUTPUT_COMMON_LEFT_TRIGGER_EFFECT  21
#define DS_OUTPUT_COMMON_PLAYER_LEDS   43
#define DS_OUTPUT_COMMON_LIGHTBAR_RED  44
#define DS_OUTPUT_COMMON_LIGHTBAR_GREEN 45
#define DS_OUTPUT_COMMON_LIGHTBAR_BLUE 46
#define DS_OUTPUT_USB_COMMON_OFFSET    1
#define DS_OUTPUT_BT_COMMON_OFFSET     3

#define SONY_OUTPUT_CRC32_SEED          0xa2
#define TRIGGER_EFFECT_SIZE             11

#define OUTPUT_FLAG_RUMBLE              0x00000001
#define OUTPUT_FLAG_LIGHTBAR            0x00000002
#define OUTPUT_FLAG_PLAYER_LEDS         0x00000004
#define OUTPUT_FLAG_TRIGGERS            0x00000008

typedef enum _SONY_TEST_DEVICE_TYPE
{
    SonyTestDeviceUnknown,
    SonyTestDeviceDualShock4,
    SonyTestDeviceDualSense
} SONY_TEST_DEVICE_TYPE;

typedef struct _SONY_TEST_DEVICE
{
    HANDLE Handle;
    HIDD_ATTRIBUTES Attributes;
    HIDP_CAPS Caps;
    SONY_TEST_DEVICE_TYPE Type;
    BOOL Bluetooth;
    WCHAR Product[128];
} SONY_TEST_DEVICE, *PSONY_TEST_DEVICE;

typedef struct _OUTPUT_STATE
{
    ULONG Flags;
    UCHAR StrongMotor;
    UCHAR WeakMotor;
    UCHAR Red;
    UCHAR Green;
    UCHAR Blue;
    UCHAR PlayerLeds;
    UCHAR RightTrigger[TRIGGER_EFFECT_SIZE];
    UCHAR LeftTrigger[TRIGGER_EFFECT_SIZE];
} OUTPUT_STATE, *POUTPUT_STATE;

static void
DebugPrintf(
    const WCHAR *Format,
    ...)
{
    WCHAR Buffer[512];
    va_list Args;

    va_start(Args, Format);
    _vsnwprintf(Buffer,
                sizeof(Buffer) / sizeof(Buffer[0]) - 2,
                Format,
                Args);
    va_end(Args);

    Buffer[(sizeof(Buffer) / sizeof(Buffer[0])) - 2] = UNICODE_NULL;
    wcscat(Buffer, L"\n");
    OutputDebugStringW(Buffer);
    wprintf(L"%s", Buffer);
}

static void
Usage(void)
{
    wprintf(L"Usage:\n");
    wprintf(L"  sonyhidtest list\n");
    wprintf(L"  sonyhidtest test [index]\n");
    wprintf(L"  sonyhidtest off [index]\n");
    wprintf(L"  sonyhidtest rumble <strong> <weak> [ms] [index]\n");
    wprintf(L"  sonyhidtest led <red> <green> <blue> [index]\n");
    wprintf(L"  sonyhidtest player <mask> [index]\n");
    wprintf(L"  sonyhidtest trigger off [index]\n");
    wprintf(L"  sonyhidtest trigger constant <force> [index]\n");
    wprintf(L"  sonyhidtest trigger raw <11 right bytes> [11 left bytes] [index]\n");
    wprintf(L"  sonyhidtest autorun [period_ms] [count]\n");
}

static BOOL
ParseUlong(
    const WCHAR *Text,
    ULONG MinValue,
    ULONG MaxValue,
    PULONG Value)
{
    WCHAR *End;
    ULONG Parsed;

    if (!Text || !*Text)
        return FALSE;

    Parsed = wcstoul(Text, &End, 0);
    if (*End || Parsed < MinValue || Parsed > MaxValue)
        return FALSE;

    *Value = Parsed;
    return TRUE;
}

static SONY_TEST_DEVICE_TYPE
GetSonyDeviceType(USHORT ProductId)
{
    switch (ProductId)
    {
        case SONY_PRODUCT_DUALSHOCK4_V1:
        case SONY_PRODUCT_DUALSHOCK4_V2:
        case SONY_PRODUCT_DUALSHOCK4_DONGLE:
            return SonyTestDeviceDualShock4;

        case SONY_PRODUCT_DUALSENSE:
        case SONY_PRODUCT_DUALSENSE_EDGE:
            return SonyTestDeviceDualSense;

        default:
            return SonyTestDeviceUnknown;
    }
}

static const WCHAR *
GetDeviceTypeName(SONY_TEST_DEVICE_TYPE Type)
{
    switch (Type)
    {
        case SonyTestDeviceDualShock4:
            return L"DualShock 4";
        case SonyTestDeviceDualSense:
            return L"DualSense";
        default:
            return L"Unknown";
    }
}

static ULONG
ComputeCrc32(
    ULONG InitialCrc,
    const UCHAR *Buffer,
    ULONG Length)
{
    ULONG Crc;
    ULONG Bit;

    Crc = ~InitialCrc;
    while (Length--)
    {
        Crc ^= *Buffer++;
        for (Bit = 0; Bit < 8; Bit++)
        {
            if (Crc & 1)
                Crc = (Crc >> 1) ^ 0xedb88320;
            else
                Crc >>= 1;
        }
    }

    return ~Crc;
}

static void
WriteLe32(
    UCHAR *Buffer,
    ULONG Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
    Buffer[2] = (UCHAR)(Value >> 16);
    Buffer[3] = (UCHAR)(Value >> 24);
}

static void
SignBluetoothReport(
    UCHAR *Report,
    ULONG ReportLength)
{
    UCHAR Seed;
    ULONG Crc;

    if (ReportLength < sizeof(ULONG))
        return;

    Seed = SONY_OUTPUT_CRC32_SEED;
    Crc = ComputeCrc32(0, &Seed, sizeof(Seed));
    Crc = ComputeCrc32(Crc, Report, ReportLength - sizeof(ULONG));
    WriteLe32(&Report[ReportLength - sizeof(ULONG)], Crc);
}

static BOOL
GetCaps(
    HANDLE Device,
    PHIDP_CAPS Caps)
{
    PHIDP_PREPARSED_DATA PreparsedData;
    NTSTATUS Status;

    if (!HidD_GetPreparsedData(Device, &PreparsedData))
        return FALSE;

    Status = HidP_GetCaps(PreparsedData, Caps);
    HidD_FreePreparsedData(PreparsedData);
    return Status == HIDP_STATUS_SUCCESS;
}

static BOOL
OpenAndProbeDevice(
    const WCHAR *Path,
    DWORD Access,
    PSONY_TEST_DEVICE Device)
{
    HANDLE Handle;
    HIDD_ATTRIBUTES Attributes;
    HIDP_CAPS Caps;
    SONY_TEST_DEVICE_TYPE Type;
    WCHAR Product[128];

    Handle = CreateFileW(Path,
                         Access,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         0,
                         NULL);
    if (Handle == INVALID_HANDLE_VALUE)
        return FALSE;

    ZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Size = sizeof(Attributes);
    if (!HidD_GetAttributes(Handle, &Attributes))
    {
        CloseHandle(Handle);
        return FALSE;
    }

    Type = GetSonyDeviceType(Attributes.ProductID);
    if (Attributes.VendorID != SONY_VENDOR_ID || Type == SonyTestDeviceUnknown)
    {
        CloseHandle(Handle);
        return FALSE;
    }

    if (!GetCaps(Handle, &Caps) ||
        Caps.UsagePage != HID_USAGE_PAGE_GENERIC ||
        (Caps.Usage != HID_USAGE_GENERIC_GAMEPAD &&
         Caps.Usage != HID_USAGE_GENERIC_JOYSTICK))
    {
        CloseHandle(Handle);
        return FALSE;
    }

    ZeroMemory(Product, sizeof(Product));
    if (!HidD_GetProductString(Handle, Product, sizeof(Product)))
        swprintf(Product, sizeof(Product) / sizeof(Product[0]), L"%s %04x:%04x",
                 GetDeviceTypeName(Type),
                 Attributes.VendorID,
                 Attributes.ProductID);

    Device->Handle = Handle;
    Device->Attributes = Attributes;
    Device->Caps = Caps;
    Device->Type = Type;
    Device->Bluetooth = (Type == SonyTestDeviceDualSense) ?
        (Caps.OutputReportByteLength >= 78) :
        (Caps.OutputReportByteLength >= 78);
    wcscpy(Device->Product, Product);
    return TRUE;
}

static BOOL
EnumerateSonyDevice(
    ULONG TargetIndex,
    DWORD Access,
    PSONY_TEST_DEVICE Device)
{
    GUID HidGuid;
    HDEVINFO DevInfo;
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    DWORD InterfaceIndex;
    ULONG SonyIndex;
    BOOL Found;

    HidD_GetHidGuid(&HidGuid);
    DevInfo = SetupDiGetClassDevsW(&HidGuid,
                                   NULL,
                                   NULL,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DevInfo == INVALID_HANDLE_VALUE)
        return FALSE;

    ZeroMemory(&InterfaceData, sizeof(InterfaceData));
    InterfaceData.cbSize = sizeof(InterfaceData);
    InterfaceIndex = 0;
    SonyIndex = 0;
    Found = FALSE;

    while (SetupDiEnumDeviceInterfaces(DevInfo,
                                       NULL,
                                       &HidGuid,
                                       InterfaceIndex++,
                                       &InterfaceData))
    {
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
        DWORD RequiredSize;

        RequiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(DevInfo,
                                         &InterfaceData,
                                         NULL,
                                         0,
                                         &RequiredSize,
                                         NULL);
        if (!RequiredSize)
            continue;

        Detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequiredSize);
        if (!Detail)
            continue;

        Detail->cbSize = sizeof(*Detail);
        if (SetupDiGetDeviceInterfaceDetailW(DevInfo,
                                             &InterfaceData,
                                             Detail,
                                             RequiredSize,
                                             NULL,
                                             NULL))
        {
            SONY_TEST_DEVICE Candidate;

            ZeroMemory(&Candidate, sizeof(Candidate));
            if (OpenAndProbeDevice(Detail->DevicePath, Access, &Candidate))
            {
                if (SonyIndex == TargetIndex)
                {
                    *Device = Candidate;
                    Found = TRUE;
                    HeapFree(GetProcessHeap(), 0, Detail);
                    break;
                }

                SonyIndex++;
                CloseHandle(Candidate.Handle);
            }
        }

        HeapFree(GetProcessHeap(), 0, Detail);
    }

    SetupDiDestroyDeviceInfoList(DevInfo);
    return Found;
}

static int
ListSonyDevices(void)
{
    GUID HidGuid;
    HDEVINFO DevInfo;
    SP_DEVICE_INTERFACE_DATA InterfaceData;
    DWORD InterfaceIndex;
    ULONG SonyIndex;

    HidD_GetHidGuid(&HidGuid);
    DevInfo = SetupDiGetClassDevsW(&HidGuid,
                                   NULL,
                                   NULL,
                                   DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (DevInfo == INVALID_HANDLE_VALUE)
    {
        wprintf(L"SetupDiGetClassDevsW failed: %lu\n", GetLastError());
        return 1;
    }

    ZeroMemory(&InterfaceData, sizeof(InterfaceData));
    InterfaceData.cbSize = sizeof(InterfaceData);
    InterfaceIndex = 0;
    SonyIndex = 0;

    while (SetupDiEnumDeviceInterfaces(DevInfo,
                                       NULL,
                                       &HidGuid,
                                       InterfaceIndex++,
                                       &InterfaceData))
    {
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W Detail;
        DWORD RequiredSize;

        RequiredSize = 0;
        SetupDiGetDeviceInterfaceDetailW(DevInfo,
                                         &InterfaceData,
                                         NULL,
                                         0,
                                         &RequiredSize,
                                         NULL);
        if (!RequiredSize)
            continue;

        Detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RequiredSize);
        if (!Detail)
            continue;

        Detail->cbSize = sizeof(*Detail);
        if (SetupDiGetDeviceInterfaceDetailW(DevInfo,
                                             &InterfaceData,
                                             Detail,
                                             RequiredSize,
                                             NULL,
                                             NULL))
        {
            SONY_TEST_DEVICE Device;

            ZeroMemory(&Device, sizeof(Device));
            if (OpenAndProbeDevice(Detail->DevicePath,
                                   GENERIC_READ | GENERIC_WRITE,
                                   &Device))
            {
                wprintf(L"[%lu] %s %04x:%04x reports in/out/feature=%u/%u/%u %s\n",
                        SonyIndex,
                        Device.Product,
                        Device.Attributes.VendorID,
                        Device.Attributes.ProductID,
                        Device.Caps.InputReportByteLength,
                        Device.Caps.OutputReportByteLength,
                        Device.Caps.FeatureReportByteLength,
                        Device.Bluetooth ? L"bt" : L"usb");
                SonyIndex++;
                CloseHandle(Device.Handle);
            }
        }

        HeapFree(GetProcessHeap(), 0, Detail);
    }

    SetupDiDestroyDeviceInfoList(DevInfo);
    if (!SonyIndex)
        wprintf(L"No supported Sony HID gamepad was found.\n");
    return 0;
}

static BOOL
BuildDualShock4Report(
    const SONY_TEST_DEVICE *Device,
    const OUTPUT_STATE *State,
    UCHAR *Report,
    ULONG ReportLength)
{
    ULONG CommonOffset;

    if (Device->Bluetooth)
    {
        if (ReportLength < 78)
            return FALSE;

        Report[0] = DS4_OUTPUT_REPORT_BT;
        Report[1] = DS4_OUTPUT_HWCTL_HID | DS4_OUTPUT_HWCTL_CRC32;
        CommonOffset = DS4_OUTPUT_BT_COMMON_OFFSET;
    }
    else
    {
        if (ReportLength < DS4_OUTPUT_USB_COMMON_OFFSET + DS4_OUTPUT_COMMON_LIGHTBAR_BLUE + 1)
            return FALSE;

        Report[0] = DS4_OUTPUT_REPORT_USB;
        CommonOffset = DS4_OUTPUT_USB_COMMON_OFFSET;
    }

    if (State->Flags & OUTPUT_FLAG_RUMBLE)
    {
        Report[CommonOffset + DS4_OUTPUT_COMMON_VALID0] |= DS4_OUTPUT_VALID_MOTOR;
        Report[CommonOffset + DS4_OUTPUT_COMMON_MOTOR_LEFT] = State->StrongMotor;
        Report[CommonOffset + DS4_OUTPUT_COMMON_MOTOR_RIGHT] = State->WeakMotor;
    }

    if (State->Flags & OUTPUT_FLAG_LIGHTBAR)
    {
        Report[CommonOffset + DS4_OUTPUT_COMMON_VALID0] |= DS4_OUTPUT_VALID_LED;
        Report[CommonOffset + DS4_OUTPUT_COMMON_LIGHTBAR_RED] = State->Red;
        Report[CommonOffset + DS4_OUTPUT_COMMON_LIGHTBAR_GREEN] = State->Green;
        Report[CommonOffset + DS4_OUTPUT_COMMON_LIGHTBAR_BLUE] = State->Blue;
    }

    if (Device->Bluetooth)
        SignBluetoothReport(Report, ReportLength);

    return TRUE;
}

static BOOL
BuildDualSenseReport(
    const SONY_TEST_DEVICE *Device,
    const OUTPUT_STATE *State,
    UCHAR *Report,
    ULONG ReportLength)
{
    ULONG CommonOffset;

    if (Device->Bluetooth)
    {
        if (ReportLength < 78)
            return FALSE;

        Report[0] = DS_OUTPUT_REPORT_BT;
        Report[1] = 0;
        Report[2] = DS_OUTPUT_TAG;
        CommonOffset = DS_OUTPUT_BT_COMMON_OFFSET;
    }
    else
    {
        if (ReportLength < DS_OUTPUT_USB_COMMON_OFFSET + DS_OUTPUT_COMMON_LIGHTBAR_BLUE + 1)
            return FALSE;

        Report[0] = DS_OUTPUT_REPORT_USB;
        CommonOffset = DS_OUTPUT_USB_COMMON_OFFSET;
    }

    if (State->Flags & OUTPUT_FLAG_RUMBLE)
    {
        Report[CommonOffset + DS_OUTPUT_COMMON_VALID0] |=
            DS_OUTPUT_VALID_FLAG0_COMPATIBLE_VIBRATION |
            DS_OUTPUT_VALID_FLAG0_HAPTICS_SELECT;
        Report[CommonOffset + DS_OUTPUT_COMMON_MOTOR_LEFT] = State->StrongMotor;
        Report[CommonOffset + DS_OUTPUT_COMMON_MOTOR_RIGHT] = State->WeakMotor;
    }

    if (State->Flags & OUTPUT_FLAG_TRIGGERS)
    {
        if (ReportLength < CommonOffset + DS_OUTPUT_COMMON_LEFT_TRIGGER_EFFECT + TRIGGER_EFFECT_SIZE)
            return FALSE;

        Report[CommonOffset + DS_OUTPUT_COMMON_VALID0] |=
            DS_OUTPUT_VALID_FLAG0_RIGHT_TRIGGER |
            DS_OUTPUT_VALID_FLAG0_LEFT_TRIGGER;
        CopyMemory(&Report[CommonOffset + DS_OUTPUT_COMMON_RIGHT_TRIGGER_EFFECT],
                   State->RightTrigger,
                   TRIGGER_EFFECT_SIZE);
        CopyMemory(&Report[CommonOffset + DS_OUTPUT_COMMON_LEFT_TRIGGER_EFFECT],
                   State->LeftTrigger,
                   TRIGGER_EFFECT_SIZE);
    }

    if (State->Flags & OUTPUT_FLAG_LIGHTBAR)
    {
        Report[CommonOffset + DS_OUTPUT_COMMON_VALID1] |=
            DS_OUTPUT_VALID_FLAG1_LIGHTBAR_CONTROL;
        Report[CommonOffset + DS_OUTPUT_COMMON_LIGHTBAR_RED] = State->Red;
        Report[CommonOffset + DS_OUTPUT_COMMON_LIGHTBAR_GREEN] = State->Green;
        Report[CommonOffset + DS_OUTPUT_COMMON_LIGHTBAR_BLUE] = State->Blue;
    }

    if (State->Flags & OUTPUT_FLAG_PLAYER_LEDS)
    {
        Report[CommonOffset + DS_OUTPUT_COMMON_VALID1] |=
            DS_OUTPUT_VALID_FLAG1_PLAYER_LEDS;
        Report[CommonOffset + DS_OUTPUT_COMMON_PLAYER_LEDS] = State->PlayerLeds & 0x1f;
    }

    if (Device->Bluetooth)
        SignBluetoothReport(Report, ReportLength);

    return TRUE;
}

static BOOL
BuildOutputReport(
    const SONY_TEST_DEVICE *Device,
    const OUTPUT_STATE *State,
    UCHAR *Report,
    ULONG ReportLength)
{
    ZeroMemory(Report, ReportLength);

    switch (Device->Type)
    {
        case SonyTestDeviceDualShock4:
            if (State->Flags & (OUTPUT_FLAG_PLAYER_LEDS | OUTPUT_FLAG_TRIGGERS))
                return FALSE;
            return BuildDualShock4Report(Device, State, Report, ReportLength);

        case SonyTestDeviceDualSense:
            return BuildDualSenseReport(Device, State, Report, ReportLength);

        default:
            return FALSE;
    }
}

static BOOL
SendOutputState(
    PSONY_TEST_DEVICE Device,
    const OUTPUT_STATE *State)
{
    UCHAR *Report;
    ULONG ReportLength;
    DWORD Written;
    BOOL Result;

    ReportLength = Device->Caps.OutputReportByteLength;
    if (!ReportLength)
    {
        wprintf(L"Device has no HID output report.\n");
        return FALSE;
    }

    Report = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ReportLength);
    if (!Report)
        return FALSE;

    if (!BuildOutputReport(Device, State, Report, ReportLength))
    {
        HeapFree(GetProcessHeap(), 0, Report);
        wprintf(L"Requested output state is not supported by this report length.\n");
        return FALSE;
    }

    Written = 0;
    Result = WriteFile(Device->Handle, Report, ReportLength, &Written, NULL);
    if (!Result || Written != ReportLength)
    {
        Result = HidD_SetOutputReport(Device->Handle, Report, ReportLength);
    }

    if (!Result)
        wprintf(L"Output report failed: %lu\n", GetLastError());

    HeapFree(GetProcessHeap(), 0, Report);
    return Result;
}

static int
OpenSelectedDevice(
    ULONG Index,
    PSONY_TEST_DEVICE Device)
{
    ZeroMemory(Device, sizeof(*Device));
    if (!EnumerateSonyDevice(Index, GENERIC_READ | GENERIC_WRITE, Device))
    {
        wprintf(L"Could not open Sony HID gamepad index %lu for output.\n", Index);
        return 1;
    }

    wprintf(L"Using [%lu] %s %04x:%04x reports in/out/feature=%u/%u/%u %s\n",
            Index,
            Device->Product,
            Device->Attributes.VendorID,
            Device->Attributes.ProductID,
            Device->Caps.InputReportByteLength,
            Device->Caps.OutputReportByteLength,
            Device->Caps.FeatureReportByteLength,
            Device->Bluetooth ? L"bt" : L"usb");
    return 0;
}

static int
RunRumble(
    int argc,
    WCHAR **argv)
{
    SONY_TEST_DEVICE Device;
    OUTPUT_STATE State;
    ULONG Strong;
    ULONG Weak;
    ULONG Delay;
    ULONG Index;
    int Result;

    if (argc < 4 ||
        !ParseUlong(argv[2], 0, 255, &Strong) ||
        !ParseUlong(argv[3], 0, 255, &Weak))
    {
        Usage();
        return 1;
    }

    Delay = 0;
    Index = 0;
    if (argc > 4 && !ParseUlong(argv[4], 0, 60000, &Delay))
    {
        Usage();
        return 1;
    }
    if (argc > 5 && !ParseUlong(argv[5], 0, 32, &Index))
    {
        Usage();
        return 1;
    }

    Result = OpenSelectedDevice(Index, &Device);
    if (Result)
        return Result;

    ZeroMemory(&State, sizeof(State));
    State.Flags = OUTPUT_FLAG_RUMBLE;
    State.StrongMotor = (UCHAR)Strong;
    State.WeakMotor = (UCHAR)Weak;
    Result = SendOutputState(&Device, &State) ? 0 : 1;

    if (!Result && Delay)
    {
        Sleep(Delay);
        ZeroMemory(&State, sizeof(State));
        State.Flags = OUTPUT_FLAG_RUMBLE;
        SendOutputState(&Device, &State);
    }

    CloseHandle(Device.Handle);
    return Result;
}

static int
RunLed(
    int argc,
    WCHAR **argv)
{
    SONY_TEST_DEVICE Device;
    OUTPUT_STATE State;
    ULONG Red;
    ULONG Green;
    ULONG Blue;
    ULONG Index;
    int Result;

    if (argc < 5 ||
        !ParseUlong(argv[2], 0, 255, &Red) ||
        !ParseUlong(argv[3], 0, 255, &Green) ||
        !ParseUlong(argv[4], 0, 255, &Blue))
    {
        Usage();
        return 1;
    }

    Index = 0;
    if (argc > 5 && !ParseUlong(argv[5], 0, 32, &Index))
    {
        Usage();
        return 1;
    }

    Result = OpenSelectedDevice(Index, &Device);
    if (Result)
        return Result;

    ZeroMemory(&State, sizeof(State));
    State.Flags = OUTPUT_FLAG_LIGHTBAR;
    State.Red = (UCHAR)Red;
    State.Green = (UCHAR)Green;
    State.Blue = (UCHAR)Blue;
    Result = SendOutputState(&Device, &State) ? 0 : 1;

    CloseHandle(Device.Handle);
    return Result;
}

static int
RunPlayer(
    int argc,
    WCHAR **argv)
{
    SONY_TEST_DEVICE Device;
    OUTPUT_STATE State;
    ULONG Mask;
    ULONG Index;
    int Result;

    if (argc < 3 || !ParseUlong(argv[2], 0, 31, &Mask))
    {
        Usage();
        return 1;
    }

    Index = 0;
    if (argc > 3 && !ParseUlong(argv[3], 0, 32, &Index))
    {
        Usage();
        return 1;
    }

    Result = OpenSelectedDevice(Index, &Device);
    if (Result)
        return Result;

    ZeroMemory(&State, sizeof(State));
    State.Flags = OUTPUT_FLAG_PLAYER_LEDS;
    State.PlayerLeds = (UCHAR)Mask;
    Result = SendOutputState(&Device, &State) ? 0 : 1;

    CloseHandle(Device.Handle);
    return Result;
}

static BOOL
ParseTriggerBytes(
    int argc,
    WCHAR **argv,
    int Start,
    UCHAR *RightTrigger,
    UCHAR *LeftTrigger,
    int *Consumed)
{
    ULONG Value;
    int Index;

    if (argc < Start + TRIGGER_EFFECT_SIZE)
        return FALSE;

    for (Index = 0; Index < TRIGGER_EFFECT_SIZE; Index++)
    {
        if (!ParseUlong(argv[Start + Index], 0, 255, &Value))
            return FALSE;
        RightTrigger[Index] = (UCHAR)Value;
    }

    *Consumed = TRIGGER_EFFECT_SIZE;
    if (argc >= Start + (TRIGGER_EFFECT_SIZE * 2))
    {
        for (Index = 0; Index < TRIGGER_EFFECT_SIZE; Index++)
        {
            if (!ParseUlong(argv[Start + TRIGGER_EFFECT_SIZE + Index], 0, 255, &Value))
                return FALSE;
            LeftTrigger[Index] = (UCHAR)Value;
        }
        *Consumed = TRIGGER_EFFECT_SIZE * 2;
    }
    else
    {
        CopyMemory(LeftTrigger, RightTrigger, TRIGGER_EFFECT_SIZE);
    }

    return TRUE;
}

static int
RunTrigger(
    int argc,
    WCHAR **argv)
{
    SONY_TEST_DEVICE Device;
    OUTPUT_STATE State;
    ULONG Force;
    ULONG Index;
    int Consumed;
    int Result;

    if (argc < 3)
    {
        Usage();
        return 1;
    }

    ZeroMemory(&State, sizeof(State));
    State.Flags = OUTPUT_FLAG_TRIGGERS;

    Consumed = 1;
    if (!_wcsicmp(argv[2], L"off"))
    {
        State.RightTrigger[0] = 0x05;
        State.LeftTrigger[0] = 0x05;
    }
    else if (!_wcsicmp(argv[2], L"constant"))
    {
        if (argc < 4 || !ParseUlong(argv[3], 0, 255, &Force))
        {
            Usage();
            return 1;
        }

        State.RightTrigger[0] = 0x01;
        State.RightTrigger[1] = 0x00;
        State.RightTrigger[2] = (UCHAR)Force;
        CopyMemory(State.LeftTrigger, State.RightTrigger, TRIGGER_EFFECT_SIZE);
        Consumed = 2;
    }
    else if (!_wcsicmp(argv[2], L"raw"))
    {
        if (!ParseTriggerBytes(argc,
                               argv,
                               3,
                               State.RightTrigger,
                               State.LeftTrigger,
                               &Consumed))
        {
            Usage();
            return 1;
        }
        Consumed++;
    }
    else
    {
        Usage();
        return 1;
    }

    Index = 0;
    if (argc > 2 + Consumed &&
        !ParseUlong(argv[2 + Consumed], 0, 32, &Index))
    {
        Usage();
        return 1;
    }

    Result = OpenSelectedDevice(Index, &Device);
    if (Result)
        return Result;

    Result = SendOutputState(&Device, &State) ? 0 : 1;
    CloseHandle(Device.Handle);
    return Result;
}

static int
RunOff(
    int argc,
    WCHAR **argv)
{
    SONY_TEST_DEVICE Device;
    OUTPUT_STATE State;
    ULONG Index;
    int Result;

    Index = 0;
    if (argc > 2 && !ParseUlong(argv[2], 0, 32, &Index))
    {
        Usage();
        return 1;
    }

    Result = OpenSelectedDevice(Index, &Device);
    if (Result)
        return Result;

    ZeroMemory(&State, sizeof(State));
    State.Flags = OUTPUT_FLAG_RUMBLE;
    if (Device.Type == SonyTestDeviceDualSense)
    {
        State.Flags |= OUTPUT_FLAG_TRIGGERS;
        State.RightTrigger[0] = 0x05;
        State.LeftTrigger[0] = 0x05;
    }

    Result = SendOutputState(&Device, &State) ? 0 : 1;
    CloseHandle(Device.Handle);
    return Result;
}

static int
RunTest(
    int argc,
    WCHAR **argv)
{
    SONY_TEST_DEVICE Device;
    OUTPUT_STATE State;
    ULONG Index;
    int Result;

    Index = 0;
    if (argc > 2 && !ParseUlong(argv[2], 0, 32, &Index))
    {
        Usage();
        return 1;
    }

    Result = OpenSelectedDevice(Index, &Device);
    if (Result)
        return Result;

    ZeroMemory(&State, sizeof(State));
    State.Flags = OUTPUT_FLAG_LIGHTBAR;
    State.Red = 0;
    State.Green = 0;
    State.Blue = 128;
    if (Device.Type == SonyTestDeviceDualSense)
    {
        State.Flags |= OUTPUT_FLAG_PLAYER_LEDS;
        State.PlayerLeds = 0x04;
    }
    if (!SendOutputState(&Device, &State))
        Result = 1;

    Sleep(250);

    ZeroMemory(&State, sizeof(State));
    State.Flags = OUTPUT_FLAG_RUMBLE;
    State.StrongMotor = 160;
    State.WeakMotor = 80;
    if (!SendOutputState(&Device, &State))
        Result = 1;

    Sleep(350);

    if (Device.Type == SonyTestDeviceDualSense)
    {
        ZeroMemory(&State, sizeof(State));
        State.Flags = OUTPUT_FLAG_TRIGGERS;
        State.RightTrigger[0] = 0x01;
        State.RightTrigger[1] = 0x00;
        State.RightTrigger[2] = 110;
        CopyMemory(State.LeftTrigger, State.RightTrigger, TRIGGER_EFFECT_SIZE);
        if (!SendOutputState(&Device, &State))
            Result = 1;

        Sleep(800);
    }

    ZeroMemory(&State, sizeof(State));
    State.Flags = OUTPUT_FLAG_RUMBLE;
    if (Device.Type == SonyTestDeviceDualSense)
    {
        State.Flags |= OUTPUT_FLAG_TRIGGERS;
        State.RightTrigger[0] = 0x05;
        State.LeftTrigger[0] = 0x05;
    }
    if (!SendOutputState(&Device, &State))
        Result = 1;

    CloseHandle(Device.Handle);
    return Result;
}

static void
BuildAutorunState(
    const SONY_TEST_DEVICE *Device,
    ULONG Tick,
    POUTPUT_STATE State)
{
    static const UCHAR Colors[][3] =
    {
        { 128, 0, 0 },
        { 0, 128, 0 },
        { 0, 0, 128 },
        { 128, 64, 0 },
        { 64, 0, 128 },
    };
    ULONG Color;

    ZeroMemory(State, sizeof(*State));

    Color = Tick % (sizeof(Colors) / sizeof(Colors[0]));
    State->Flags = OUTPUT_FLAG_RUMBLE | OUTPUT_FLAG_LIGHTBAR;
    State->StrongMotor = (Tick & 1) ? 160 : 64;
    State->WeakMotor = (Tick & 1) ? 64 : 160;
    State->Red = Colors[Color][0];
    State->Green = Colors[Color][1];
    State->Blue = Colors[Color][2];

    if (Device->Type == SonyTestDeviceDualSense)
    {
        State->Flags |= OUTPUT_FLAG_PLAYER_LEDS | OUTPUT_FLAG_TRIGGERS;
        State->PlayerLeds = (UCHAR)(1 << (Tick % 5));
        State->RightTrigger[0] = 0x01;
        State->RightTrigger[1] = 0x00;
        State->RightTrigger[2] = (Tick & 1) ? 120 : 60;
        CopyMemory(State->LeftTrigger, State->RightTrigger, TRIGGER_EFFECT_SIZE);
    }
}

static int
RunAutorun(
    int argc,
    WCHAR **argv)
{
    ULONG PeriodMs;
    ULONG Count;
    ULONG Tick;
    ULONG Index;

    PeriodMs = 1000;
    Count = 0;
    if (argc > 2 && !ParseUlong(argv[2], 100, 60000, &PeriodMs))
    {
        Usage();
        return 1;
    }
    if (argc > 3 && !ParseUlong(argv[3], 0, 0xffffffff, &Count))
    {
        Usage();
        return 1;
    }

    DebugPrintf(L"SONYHIDTEST_AUTORUN_BEGIN period=%lu count=%lu",
                PeriodMs,
                Count);

    Tick = 0;
    while (!Count || Tick < Count)
    {
        BOOL AnyDevice;

        AnyDevice = FALSE;
        for (Index = 0; Index < 32; Index++)
        {
            SONY_TEST_DEVICE Device;
            OUTPUT_STATE State;

            ZeroMemory(&Device, sizeof(Device));
            if (!EnumerateSonyDevice(Index, GENERIC_READ | GENERIC_WRITE, &Device))
                break;

            AnyDevice = TRUE;
            BuildAutorunState(&Device, Tick, &State);
            DebugPrintf(L"SONYHIDTEST_AUTORUN_TICK tick=%lu index=%lu type=%s out=%u",
                        Tick,
                        Index,
                        GetDeviceTypeName(Device.Type),
                        Device.Caps.OutputReportByteLength);
            if (!SendOutputState(&Device, &State))
            {
                DebugPrintf(L"SONYHIDTEST_AUTORUN_FAIL tick=%lu index=%lu error=%lu",
                            Tick,
                            Index,
                            GetLastError());
            }

            CloseHandle(Device.Handle);
        }

        if (!AnyDevice)
            DebugPrintf(L"SONYHIDTEST_AUTORUN_NO_DEVICE tick=%lu", Tick);

        Tick++;
        Sleep(PeriodMs);
    }

    DebugPrintf(L"SONYHIDTEST_AUTORUN_END ticks=%lu", Tick);
    return 0;
}

int
wmain(
    int argc,
    WCHAR **argv)
{
    if (argc < 2)
    {
        Usage();
        return 1;
    }

    if (!_wcsicmp(argv[1], L"list"))
        return ListSonyDevices();

    if (!_wcsicmp(argv[1], L"test"))
        return RunTest(argc, argv);

    if (!_wcsicmp(argv[1], L"off"))
        return RunOff(argc, argv);

    if (!_wcsicmp(argv[1], L"rumble"))
        return RunRumble(argc, argv);

    if (!_wcsicmp(argv[1], L"led"))
        return RunLed(argc, argv);

    if (!_wcsicmp(argv[1], L"player"))
        return RunPlayer(argc, argv);

    if (!_wcsicmp(argv[1], L"trigger"))
        return RunTrigger(argc, argv);

    if (!_wcsicmp(argv[1], L"autorun"))
        return RunAutorun(argc, argv);

    Usage();
    return 1;
}
