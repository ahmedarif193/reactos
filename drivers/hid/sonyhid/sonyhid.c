/*
 * PROJECT:     ReactOS HID Stack
 * LICENSE:     GPL-2.0-or-later
 * FILE:        drivers/hid/sonyhid/sonyhid.c
 * PURPOSE:     Sony DualShock 3 / DualShock 4 / DualSense HID support
 *
 * Cleanroom feature plan derived from public Linux hid-sony /
 * hid-playstation architecture (multi-evdev, LED class, battery,
 * motion sensors). All code is original ReactOS implementation.
 *
 * DEVICES RECOGNIZED:
 *   DualShock 3     USB / Bluetooth IDs
 *   DualShock 4     USB / Bluetooth IDs (V1, V2, Dongle)
 *   DualSense       USB / Bluetooth IDs (Standard, Edge)
 *
 * INITIAL FEATURE SCOPE:
 *   Touchpad -> mouse pointer
 *   Gamepad input preservation through HID pass-through
 *   Limited output report initialization
 *   Private bring-up IOCTLs for DS4 / DualSense output experiments
 *   Proper PnP / power management lifecycle
 *
 * TODO / FUTURE WORK:
 *   [ ] DS3 specific Bluetooth pairing support
 *   [ ] Audio/headset output reports (DS4/DS)
 *   [ ] Standard HID output mapping for user-mode rumble APIs
 *   [ ] Public, tested user-mode ABI for LED / rumble / trigger control
 *   [ ] Motion sensor and battery exposure to user mode
 *   [ ] Multi-touch reporting beyond mouclass pointer synthesis
 *   [ ] Additional DualSense vendor output reports
 *   [ ] Full DualSense Edge profile support
 *   [ ] HidP_SetUsages / HidP_SetUsageValue for output
 *         report construction
 */

#include "sonyhid.h"

#define SONYHID_BIT(x) (1U << (x))

/* -- DS3 report IDs (USB & BT share the same) --------------------- */
#define DS3_INPUT_REPORT_USB           0x01
#define DS3_INPUT_REPORT_SIZE          49
#define DS3_OUTPUT_REPORT_USB          0x01
#define DS3_OUTPUT_REPORT_SIZE         48

/* -- DS4 report IDs ------------------------------------------------ */
#define DS4_INPUT_REPORT_USB           0x01
#define DS4_INPUT_REPORT_USB_SIZE      64
#define DS4_INPUT_REPORT_BT            0x11
#define DS4_INPUT_REPORT_BT_SIZE       78
#define DS4_OUTPUT_REPORT_USB          0x05
#define DS4_OUTPUT_REPORT_USB_SIZE     32
#define DS4_OUTPUT_REPORT_BT           0x11
#define DS4_OUTPUT_REPORT_BT_SIZE      78

/* -- DualSense report IDs ------------------------------------------ */
#define DS_INPUT_REPORT_USB            0x01
#define DS_INPUT_REPORT_USB_SIZE       64
#define DS_INPUT_REPORT_BT             0x31
#define DS_INPUT_REPORT_BT_SIZE        78
#define DS_OUTPUT_REPORT_USB           0x02
#define DS_OUTPUT_REPORT_USB_SIZE      63
#define DS_OUTPUT_REPORT_BT            0x31
#define DS_OUTPUT_REPORT_BT_SIZE       78
#define DS_OUTPUT_TAG                  0x10

/* -- Misc constants ------------------------------------------------ */
#define SONYHID_MAX_OUTPUT_REPORT_SIZE 78
#define SONYHID_DISPATCH_MIN_INTERVAL  -40000LL /* ~4 ms in 100ns */
#define SONYHID_OUTPUT_CRC32_SEED      0xA2

/* -- DS4 button / touchpad bit offsets ---------------------------- */
#define DS4_BUTTONS1_SQUARE             SONYHID_BIT(4)
#define DS4_BUTTONS1_CROSS              SONYHID_BIT(5)
#define DS4_BUTTONS1_CIRCLE             SONYHID_BIT(6)
#define DS4_BUTTONS1_TRIANGLE           SONYHID_BIT(7)
#define DS4_BUTTONS2_L1                 SONYHID_BIT(0)
#define DS4_BUTTONS2_R1                 SONYHID_BIT(1)
#define DS4_BUTTONS2_L2                 SONYHID_BIT(2)
#define DS4_BUTTONS2_R2                 SONYHID_BIT(3)
#define DS4_BUTTONS2_SHARE              SONYHID_BIT(4)
#define DS4_BUTTONS2_OPTIONS            SONYHID_BIT(5)
#define DS4_BUTTONS2_L3                 SONYHID_BIT(6)
#define DS4_BUTTONS2_R3                 SONYHID_BIT(7)
#define DS4_BUTTONS3_PS                 SONYHID_BIT(0)
#define DS4_BUTTONS3_TOUCHPAD           SONYHID_BIT(1)

/* -- DS button bit offsets ----------------------------------------- */
#define DS_BUTTONS1_SQUARE              SONYHID_BIT(4)
#define DS_BUTTONS1_CROSS               SONYHID_BIT(5)
#define DS_BUTTONS1_CIRCLE              SONYHID_BIT(6)
#define DS_BUTTONS1_TRIANGLE            SONYHID_BIT(7)
#define DS_BUTTONS2_L1                  SONYHID_BIT(0)
#define DS_BUTTONS2_R1                  SONYHID_BIT(1)
#define DS_BUTTONS2_L2                  SONYHID_BIT(2)
#define DS_BUTTONS2_R2                  SONYHID_BIT(3)
#define DS_BUTTONS2_CREATE              SONYHID_BIT(4)
#define DS_BUTTONS2_OPTIONS             SONYHID_BIT(5)
#define DS_BUTTONS2_L3                  SONYHID_BIT(6)
#define DS_BUTTONS2_R3                  SONYHID_BIT(7)
#define DS_BUTTONS3_PS                  SONYHID_BIT(0)
#define DS_BUTTONS3_TOUCHPAD            SONYHID_BIT(1)
#define DS_BUTTONS3_MIC                 SONYHID_BIT(2)

/* -- Touch constants ----------------------------------------------- */
#define DS_TOUCH_POINT_INACTIVE         SONYHID_BIT(7)

/* -- Output report fields ------------------------------------------ */
#define DS4_OUTPUT_HWCTL_CRC32          SONYHID_BIT(6)
#define DS4_OUTPUT_HWCTL_HID            SONYHID_BIT(7)
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

#define DS_OUTPUT_VALID_FLAG0_COMPATIBLE_VIBRATION SONYHID_BIT(0)
#define DS_OUTPUT_VALID_FLAG0_HAPTICS_SELECT       SONYHID_BIT(1)
#define DS_OUTPUT_VALID_FLAG0_RIGHT_TRIGGER        SONYHID_BIT(2)
#define DS_OUTPUT_VALID_FLAG0_LEFT_TRIGGER         SONYHID_BIT(3)
#define DS_OUTPUT_VALID_FLAG1_LIGHTBAR_CONTROL     SONYHID_BIT(2)
#define DS_OUTPUT_VALID_FLAG1_PLAYER_LEDS          SONYHID_BIT(4)
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

#define DS4_OUTPUT_RUMBLE_MIN_SIZE     (DS4_OUTPUT_USB_COMMON_OFFSET + DS4_OUTPUT_COMMON_MOTOR_LEFT + 1)
#define DS4_OUTPUT_LIGHTBAR_MIN_SIZE   (DS4_OUTPUT_USB_COMMON_OFFSET + DS4_OUTPUT_COMMON_LIGHTBAR_BLUE + 1)
#define DS_OUTPUT_RUMBLE_MIN_SIZE      (DS_OUTPUT_USB_COMMON_OFFSET + DS_OUTPUT_COMMON_MOTOR_LEFT + 1)
#define DS_OUTPUT_TRIGGERS_MIN_SIZE    (DS_OUTPUT_USB_COMMON_OFFSET + DS_OUTPUT_COMMON_LEFT_TRIGGER_EFFECT + SONYHID_TRIGGER_EFFECT_SIZE)
#define DS_OUTPUT_PLAYER_LEDS_MIN_SIZE (DS_OUTPUT_USB_COMMON_OFFSET + DS_OUTPUT_COMMON_PLAYER_LEDS + 1)
#define DS_OUTPUT_LIGHTBAR_MIN_SIZE    (DS_OUTPUT_USB_COMMON_OFFSET + DS_OUTPUT_COMMON_LIGHTBAR_BLUE + 1)

/* -- DS3 button byte offsets -------------------------------------- */
#define DS3_BYTE_BUTTONS1              2
#define DS3_BYTE_BUTTONS2              3
#define DS3_BYTE_BUTTONS3              4
#define DS3_BYTE_DPAD                  13
#define DS3_BYTE_LSTICK_X               6
#define DS3_BYTE_LSTICK_Y               7
#define DS3_BYTE_RSTICK_X               8
#define DS3_BYTE_RSTICK_Y               9
#define DS3_BYTE_ACC_X                 41
#define DS3_BYTE_BATTERY              30

/* -- D-pad value -> angle constants (DS3) -------------------------- */
#define DS3_DPAD_NEUTRAL              0x0F
#define DS3_DPAD_N                    0x00
#define DS3_DPAD_NE                   0x01
#define DS3_DPAD_E                    0x02
#define DS3_DPAD_SE                   0x03
#define DS3_DPAD_S                    0x04
#define DS3_DPAD_SW                   0x05
#define DS3_DPAD_W                    0x06
#define DS3_DPAD_NW                   0x07

/* -- DS3 buttons in ButtonByte1 ------------------------------------ */
#define DS3_BTN_SELECT                 SONYHID_BIT(0)
#define DS3_BTN_L3                     SONYHID_BIT(1)
#define DS3_BTN_R3                     SONYHID_BIT(2)
#define DS3_BTN_START                  SONYHID_BIT(3)
#define DS3_BTN_UP                     SONYHID_BIT(4)
#define DS3_BTN_RIGHT                  SONYHID_BIT(5)
#define DS3_BTN_DOWN                   SONYHID_BIT(6)
#define DS3_BTN_LEFT                   SONYHID_BIT(7)

/* -- DS3 buttons in ButtonByte2 ------------------------------------ */
#define DS3_BTN_L2                     SONYHID_BIT(0)
#define DS3_BTN_R2                     SONYHID_BIT(1)
#define DS3_BTN_L1                     SONYHID_BIT(2)
#define DS3_BTN_R1                     SONYHID_BIT(3)
#define DS3_BTN_TRIANGLE               SONYHID_BIT(4)
#define DS3_BTN_CIRCLE                 SONYHID_BIT(5)
#define DS3_BTN_CROSS                  SONYHID_BIT(6)
#define DS3_BTN_SQUARE                 SONYHID_BIT(7)

/* -- DS3 buttons in ButtonByte3 ------------------------------------ */
#define DS3_BTN_PS                     SONYHID_BIT(0)
#define DS3_BTN_TOUCHPAD               SONYHID_BIT(1)

/* ------------------------------------------------------------------
 *  Device identification
 * ------------------------------------------------------------------ */

static SONYHID_DEVICE_TYPE
SonyHid_GetDeviceType(
    _In_ USHORT VendorId,
    _In_ USHORT ProductId)
{
    if (VendorId != SONY_VENDOR_ID)
        return SonyHidDeviceUnknown;

    switch (ProductId)
    {
        case SONY_PRODUCT_DUALSHOCK3:
        case SONY_PRODUCT_DUALSHOCK3_ALT:
            return SonyHidDeviceDualShock3;

        case SONY_PRODUCT_DUALSHOCK4_V1:
        case SONY_PRODUCT_DUALSHOCK4_V2:
        case SONY_PRODUCT_DUALSHOCK4_DONGLE:
            return SonyHidDeviceDualShock4;

        case SONY_PRODUCT_DUALSENSE:
        case SONY_PRODUCT_DUALSENSE_EDGE:
            return SonyHidDeviceDualSense;

        default:
            return SonyHidDeviceUnknown;
    }
}

static SONYHID_CONNECTION_TYPE
SonyHid_DetectConnection(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    if (!DeviceExtension || !DeviceExtension->NextDeviceObject ||
        !DeviceExtension->Report || DeviceExtension->ReportLength < 1)
        return SonyHidConnectionUnknown;

    if (DeviceExtension->DeviceType == SonyHidDeviceDualShock3)
    {
        /* DS3: USB sends report ID 0x01, BT sends 0x01 too but with
         * different 0xF1/0xF2 output report IDs on the HID interface */
        if (DeviceExtension->ReportLength >= DS3_INPUT_REPORT_SIZE &&
            DeviceExtension->Report[0] == DS3_INPUT_REPORT_USB)
        {
            return SonyHidConnectionUsb;
        }
        return SonyHidConnectionUnknown;
    }

    if (DeviceExtension->DeviceType == SonyHidDeviceDualShock4)
    {
        if (DeviceExtension->ReportLength >= DS4_INPUT_REPORT_BT_SIZE &&
            DeviceExtension->Report[0] == DS4_INPUT_REPORT_BT)
        {
            return SonyHidConnectionBluetooth;
        }
        if (DeviceExtension->ReportLength >= DS4_INPUT_REPORT_USB_SIZE &&
            DeviceExtension->Report[0] == DS4_INPUT_REPORT_USB)
        {
            return SonyHidConnectionUsb;
        }
        return SonyHidConnectionUnknown;
    }

    if (DeviceExtension->DeviceType == SonyHidDeviceDualSense)
    {
        if (DeviceExtension->ReportLength >= DS_INPUT_REPORT_BT_SIZE &&
            DeviceExtension->Report[0] == DS_INPUT_REPORT_BT)
        {
            return SonyHidConnectionBluetooth;
        }
        if (DeviceExtension->ReportLength >= DS_INPUT_REPORT_USB_SIZE &&
            DeviceExtension->Report[0] == DS_INPUT_REPORT_USB)
        {
            return SonyHidConnectionUsb;
        }
        return SonyHidConnectionUnknown;
    }

    return SonyHidConnectionUnknown;
}

/* ------------------------------------------------------------------
 *  IRP submission helpers
 * ------------------------------------------------------------------ */

static NTSTATUS
NTAPI
SonyHid_ForwardCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
SonyHid_SubmitRequest(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_ ULONG InputBufferSize,
    _In_reads_bytes_opt_(InputBufferSize) PVOID InputBuffer,
    _In_ ULONG OutputBufferSize,
    _Out_writes_bytes_opt_(OutputBufferSize) PVOID OutputBuffer)
{
    KEVENT Event;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;

    DeviceExtension = DeviceObject->DeviceExtension;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        DeviceExtension->NextDeviceObject,
                                        InputBuffer,
                                        InputBufferSize,
                                        OutputBuffer,
                                        OutputBufferSize,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

/* ------------------------------------------------------------------
 *  Output report sending
 * ------------------------------------------------------------------ */

static NTSTATUS
SonyHid_SendOutputReport(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_reads_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength)
{
    KEVENT Event;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;

    if (!Report || !ReportLength)
        return STATUS_INVALID_PARAMETER;

    DeviceExtension = DeviceObject->DeviceExtension;
    if (!DeviceExtension->NextDeviceObject)
        return STATUS_DEVICE_NOT_CONNECTED;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoAllocateIrp(DeviceExtension->NextDeviceObject->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->UserBuffer = Report;
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    IoStack = IoGetNextIrpStackLocation(Irp);
    RtlZeroMemory(IoStack, sizeof(*IoStack));
    IoStack->MajorFunction = IRP_MJ_WRITE;
    IoStack->Parameters.Write.Length = ReportLength;
    IoStack->DeviceObject = DeviceExtension->NextDeviceObject;

    IoSetCompletionRoutine(Irp, SonyHid_ForwardCompletion, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
    }

    Status = Irp->IoStatus.Status;
    IoFreeIrp(Irp);
    return Status;
}

static VOID
SonyHid_WriteLe32(
    _Out_writes_bytes_(4) PUCHAR Buffer,
    _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
    Buffer[2] = (UCHAR)(Value >> 16);
    Buffer[3] = (UCHAR)(Value >> 24);
}

static ULONG
SonyHid_ComputeCrc32(
    _In_ ULONG InitialCrc,
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length)
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
                Crc = (Crc >> 1) ^ 0xEDB88320;
            else
                Crc >>= 1;
        }
    }

    return ~Crc;
}

static VOID
SonyHid_SignBluetoothOutputReport(
    _Inout_updates_bytes_(ReportLength) PUCHAR Report,
    _In_ ULONG ReportLength)
{
    UCHAR Seed = SONYHID_OUTPUT_CRC32_SEED;
    ULONG Crc;

    if (ReportLength < sizeof(ULONG))
        return;

    Crc = SonyHid_ComputeCrc32(0, &Seed, sizeof(Seed));
    Crc = SonyHid_ComputeCrc32(Crc, Report, ReportLength - sizeof(ULONG));
    SonyHid_WriteLe32(&Report[ReportLength - sizeof(ULONG)], Crc);
}

static VOID
SonyHid_ResetOutputState(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    DeviceExtension->StrongMotorMagnitude = 0;
    DeviceExtension->WeakMotorMagnitude = 0;
    DeviceExtension->LightbarRed = 0;
    DeviceExtension->LightbarGreen = 0;
    DeviceExtension->LightbarBlue = 128;
    DeviceExtension->PlayerLedMask = SONYHID_BIT(2);
    RtlZeroMemory(DeviceExtension->RightTriggerEffect,
                  sizeof(DeviceExtension->RightTriggerEffect));
    RtlZeroMemory(DeviceExtension->LeftTriggerEffect,
                  sizeof(DeviceExtension->LeftTriggerEffect));
    DeviceExtension->OutputSequence = 0;
}

static VOID
SonyHid_QueryOutputReportId(
    _In_ PHIDP_PREPARSED_DATA PreparsedData,
    _In_ PHIDP_CAPS Capabilities,
    _Inout_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    HIDP_VALUE_CAPS ValueCaps;
    HIDP_BUTTON_CAPS ButtonCaps;
    USHORT CapsLength;
    NTSTATUS Status;

    DeviceExtension->HasOutputReportId = FALSE;
    DeviceExtension->OutputReportId = 0;

    if (!Capabilities->OutputReportByteLength)
        return;

    if (Capabilities->NumberOutputValueCaps)
    {
        CapsLength = 1;
        RtlZeroMemory(&ValueCaps, sizeof(ValueCaps));
        Status = HidP_GetSpecificValueCaps(HidP_Output,
                                           HID_USAGE_PAGE_UNDEFINED,
                                           HIDP_LINK_COLLECTION_UNSPECIFIED,
                                           0,
                                           &ValueCaps,
                                           &CapsLength,
                                           PreparsedData);
        if (Status == HIDP_STATUS_SUCCESS && CapsLength)
        {
            DeviceExtension->OutputReportId = ValueCaps.ReportID;
            DeviceExtension->HasOutputReportId = TRUE;
            return;
        }
    }

    if (Capabilities->NumberOutputButtonCaps)
    {
        CapsLength = 1;
        RtlZeroMemory(&ButtonCaps, sizeof(ButtonCaps));
        Status = HidP_GetSpecificButtonCaps(HidP_Output,
                                            HID_USAGE_PAGE_UNDEFINED,
                                            HIDP_LINK_COLLECTION_UNSPECIFIED,
                                            0,
                                            &ButtonCaps,
                                            &CapsLength,
                                            PreparsedData);
        if (Status == HIDP_STATUS_SUCCESS && CapsLength)
        {
            DeviceExtension->OutputReportId = ButtonCaps.ReportID;
            DeviceExtension->HasOutputReportId = TRUE;
        }
    }
}

static ULONG
SonyHid_GetProtocolOutputReportId(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    switch (DeviceExtension->DeviceType)
    {
        case SonyHidDeviceDualShock4:
            return (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth) ?
                DS4_OUTPUT_REPORT_BT : DS4_OUTPUT_REPORT_USB;

        case SonyHidDeviceDualSense:
            return (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth) ?
                DS_OUTPUT_REPORT_BT : DS_OUTPUT_REPORT_USB;

        default:
            return 0;
    }
}

static BOOLEAN
SonyHid_IsOutputReportIdSupported(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    ULONG ReportId;

    ReportId = SonyHid_GetProtocolOutputReportId(DeviceExtension);
    if (!ReportId)
        return FALSE;

    return !DeviceExtension->HasOutputReportId ||
           DeviceExtension->OutputReportId == (UCHAR)ReportId;
}

static ULONG
SonyHid_GetOutputReportLength(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    if (!SonyHid_IsOutputReportIdSupported(DeviceExtension) ||
        !DeviceExtension->OutputReportLength ||
        DeviceExtension->OutputReportLength > SONYHID_MAX_OUTPUT_REPORT_SIZE)
    {
        return 0;
    }

    return DeviceExtension->OutputReportLength;
}

static ULONG
SonyHid_GetRequiredDualShock4OutputLength(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG UpdateFlags)
{
    ULONG Length = 0;

    if (UpdateFlags & SONYHID_OUTPUT_FLAG_PLAYER_LEDS)
        return 0;

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
        return DS4_OUTPUT_REPORT_BT_SIZE;

    if (UpdateFlags & SONYHID_OUTPUT_FLAG_RUMBLE)
        Length = DS4_OUTPUT_RUMBLE_MIN_SIZE;

    if ((UpdateFlags & SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB) &&
        Length < DS4_OUTPUT_LIGHTBAR_MIN_SIZE)
    {
        Length = DS4_OUTPUT_LIGHTBAR_MIN_SIZE;
    }

    return Length;
}

static ULONG
SonyHid_GetRequiredDualSenseOutputLength(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG UpdateFlags)
{
    ULONG Length = 0;

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
        return DS_OUTPUT_REPORT_BT_SIZE;

    if (UpdateFlags & SONYHID_OUTPUT_FLAG_RUMBLE)
        Length = DS_OUTPUT_RUMBLE_MIN_SIZE;

    if ((UpdateFlags & SONYHID_OUTPUT_FLAG_ADAPTIVE_TRIGGERS) &&
        Length < DS_OUTPUT_TRIGGERS_MIN_SIZE)
    {
        Length = DS_OUTPUT_TRIGGERS_MIN_SIZE;
    }

    if ((UpdateFlags & SONYHID_OUTPUT_FLAG_PLAYER_LEDS) &&
        Length < DS_OUTPUT_PLAYER_LEDS_MIN_SIZE)
    {
        Length = DS_OUTPUT_PLAYER_LEDS_MIN_SIZE;
    }

    if ((UpdateFlags & SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB) &&
        Length < DS_OUTPUT_LIGHTBAR_MIN_SIZE)
    {
        Length = DS_OUTPUT_LIGHTBAR_MIN_SIZE;
    }

    return Length;
}

static ULONG
SonyHid_GetRequiredOutputReportLength(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG UpdateFlags)
{
    switch (DeviceExtension->DeviceType)
    {
        case SonyHidDeviceDualShock4:
            return SonyHid_GetRequiredDualShock4OutputLength(DeviceExtension,
                                                            UpdateFlags);

        case SonyHidDeviceDualSense:
            return SonyHid_GetRequiredDualSenseOutputLength(DeviceExtension,
                                                           UpdateFlags);

        default:
            return 0;
    }
}

static ULONG
SonyHid_GetOutputCapabilities(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Flags = 0;
    ULONG ReportLength;

    ReportLength = SonyHid_GetOutputReportLength(DeviceExtension);
    if (!ReportLength)
        return 0;

    switch (DeviceExtension->DeviceType)
    {
        case SonyHidDeviceDualShock4:
            if (ReportLength >= SonyHid_GetRequiredOutputReportLength(DeviceExtension,
                                                                       SONYHID_OUTPUT_FLAG_RUMBLE))
                Flags |= SONYHID_OUTPUT_CAP_RUMBLE;

            if (ReportLength >= SonyHid_GetRequiredOutputReportLength(DeviceExtension,
                                                                       SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB))
                Flags |= SONYHID_OUTPUT_CAP_LIGHTBAR_RGB;

            return Flags;

        case SonyHidDeviceDualSense:
            if (ReportLength >= SonyHid_GetRequiredOutputReportLength(DeviceExtension,
                                                                       SONYHID_OUTPUT_FLAG_RUMBLE))
                Flags |= SONYHID_OUTPUT_CAP_RUMBLE;

            if (ReportLength >= SonyHid_GetRequiredOutputReportLength(DeviceExtension,
                                                                       SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB))
                Flags |= SONYHID_OUTPUT_CAP_LIGHTBAR_RGB;

            if (ReportLength >= SonyHid_GetRequiredOutputReportLength(DeviceExtension,
                                                                       SONYHID_OUTPUT_FLAG_PLAYER_LEDS))
                Flags |= SONYHID_OUTPUT_CAP_PLAYER_LEDS;

            if (ReportLength >= SonyHid_GetRequiredOutputReportLength(DeviceExtension,
                                                                       SONYHID_OUTPUT_FLAG_ADAPTIVE_TRIGGERS))
                Flags |= SONYHID_OUTPUT_CAP_ADAPTIVE_TRIGGERS;

            return Flags;

        default:
            return 0;
    }
}

static VOID
SonyHid_SetCommonOutputState(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension,
    _In_ PSONYHID_OUTPUT_STATE State)
{
    if (State->Flags & SONYHID_OUTPUT_FLAG_RUMBLE)
    {
        DeviceExtension->StrongMotorMagnitude = State->StrongMotorMagnitude;
        DeviceExtension->WeakMotorMagnitude = State->WeakMotorMagnitude;
    }

    if (State->Flags & SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB)
    {
        DeviceExtension->LightbarRed = State->LightbarRed;
        DeviceExtension->LightbarGreen = State->LightbarGreen;
        DeviceExtension->LightbarBlue = State->LightbarBlue;
    }

    if (State->Flags & SONYHID_OUTPUT_FLAG_PLAYER_LEDS)
    {
        DeviceExtension->PlayerLedMask = State->PlayerLedMask & 0x1F;
    }

    if (State->Flags & SONYHID_OUTPUT_FLAG_ADAPTIVE_TRIGGERS)
    {
        RtlCopyMemory(DeviceExtension->RightTriggerEffect,
                      State->RightTriggerEffect,
                      SONYHID_TRIGGER_EFFECT_SIZE);
        RtlCopyMemory(DeviceExtension->LeftTriggerEffect,
                      State->LeftTriggerEffect,
                      SONYHID_TRIGGER_EFFECT_SIZE);
    }
}

static NTSTATUS
SonyHid_BuildDualShock4OutputReport(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG UpdateFlags,
    _Out_writes_bytes_(SONYHID_MAX_OUTPUT_REPORT_SIZE) PUCHAR Report,
    _Out_ PULONG ReportLength)
{
    ULONG CommonOffset;

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
    {
        *ReportLength = SonyHid_GetOutputReportLength(DeviceExtension);
        Report[0] = DS4_OUTPUT_REPORT_BT;
        Report[1] = DS4_OUTPUT_HWCTL_HID | DS4_OUTPUT_HWCTL_CRC32;
        CommonOffset = DS4_OUTPUT_BT_COMMON_OFFSET;
    }
    else
    {
        *ReportLength = SonyHid_GetOutputReportLength(DeviceExtension);
        Report[0] = DS4_OUTPUT_REPORT_USB;
        CommonOffset = DS4_OUTPUT_USB_COMMON_OFFSET;
    }

    if (UpdateFlags & SONYHID_OUTPUT_FLAG_RUMBLE)
    {
        Report[CommonOffset + DS4_OUTPUT_COMMON_VALID0] |= DS4_OUTPUT_VALID_MOTOR;
        Report[CommonOffset + DS4_OUTPUT_COMMON_MOTOR_LEFT] =
            DeviceExtension->StrongMotorMagnitude;
        Report[CommonOffset + DS4_OUTPUT_COMMON_MOTOR_RIGHT] =
            DeviceExtension->WeakMotorMagnitude;
    }

    if (UpdateFlags & SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB)
    {
        Report[CommonOffset + DS4_OUTPUT_COMMON_VALID0] |= DS4_OUTPUT_VALID_LED;
        Report[CommonOffset + DS4_OUTPUT_COMMON_LIGHTBAR_RED] =
            DeviceExtension->LightbarRed;
        Report[CommonOffset + DS4_OUTPUT_COMMON_LIGHTBAR_GREEN] =
            DeviceExtension->LightbarGreen;
        Report[CommonOffset + DS4_OUTPUT_COMMON_LIGHTBAR_BLUE] =
            DeviceExtension->LightbarBlue;
    }

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
        SonyHid_SignBluetoothOutputReport(Report, *ReportLength);

    return STATUS_SUCCESS;
}

static NTSTATUS
SonyHid_BuildDualSenseOutputReport(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG UpdateFlags,
    _Out_writes_bytes_(SONYHID_MAX_OUTPUT_REPORT_SIZE) PUCHAR Report,
    _Out_ PULONG ReportLength)
{
    ULONG CommonOffset;

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
    {
        *ReportLength = SonyHid_GetOutputReportLength(DeviceExtension);
        Report[0] = DS_OUTPUT_REPORT_BT;
        Report[1] = (UCHAR)(DeviceExtension->OutputSequence << 4);
        Report[2] = DS_OUTPUT_TAG;
        DeviceExtension->OutputSequence = (DeviceExtension->OutputSequence + 1) & 0x0F;
        CommonOffset = DS_OUTPUT_BT_COMMON_OFFSET;
    }
    else
    {
        *ReportLength = SonyHid_GetOutputReportLength(DeviceExtension);
        Report[0] = DS_OUTPUT_REPORT_USB;
        CommonOffset = DS_OUTPUT_USB_COMMON_OFFSET;
    }

    if (UpdateFlags & SONYHID_OUTPUT_FLAG_RUMBLE)
    {
        Report[CommonOffset + DS_OUTPUT_COMMON_VALID0] |=
            DS_OUTPUT_VALID_FLAG0_COMPATIBLE_VIBRATION |
            DS_OUTPUT_VALID_FLAG0_HAPTICS_SELECT;
        Report[CommonOffset + DS_OUTPUT_COMMON_MOTOR_LEFT] =
            DeviceExtension->StrongMotorMagnitude;
        Report[CommonOffset + DS_OUTPUT_COMMON_MOTOR_RIGHT] =
            DeviceExtension->WeakMotorMagnitude;
    }

    if (UpdateFlags & SONYHID_OUTPUT_FLAG_ADAPTIVE_TRIGGERS)
    {
        Report[CommonOffset + DS_OUTPUT_COMMON_VALID0] |=
            DS_OUTPUT_VALID_FLAG0_RIGHT_TRIGGER |
            DS_OUTPUT_VALID_FLAG0_LEFT_TRIGGER;
        RtlCopyMemory(&Report[CommonOffset + DS_OUTPUT_COMMON_RIGHT_TRIGGER_EFFECT],
                      DeviceExtension->RightTriggerEffect,
                      SONYHID_TRIGGER_EFFECT_SIZE);
        RtlCopyMemory(&Report[CommonOffset + DS_OUTPUT_COMMON_LEFT_TRIGGER_EFFECT],
                      DeviceExtension->LeftTriggerEffect,
                      SONYHID_TRIGGER_EFFECT_SIZE);
    }

    if (UpdateFlags & SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB)
    {
        Report[CommonOffset + DS_OUTPUT_COMMON_VALID1] |=
            DS_OUTPUT_VALID_FLAG1_LIGHTBAR_CONTROL;
        Report[CommonOffset + DS_OUTPUT_COMMON_LIGHTBAR_RED] =
            DeviceExtension->LightbarRed;
        Report[CommonOffset + DS_OUTPUT_COMMON_LIGHTBAR_GREEN] =
            DeviceExtension->LightbarGreen;
        Report[CommonOffset + DS_OUTPUT_COMMON_LIGHTBAR_BLUE] =
            DeviceExtension->LightbarBlue;
    }

    if (UpdateFlags & SONYHID_OUTPUT_FLAG_PLAYER_LEDS)
    {
        Report[CommonOffset + DS_OUTPUT_COMMON_VALID1] |=
            DS_OUTPUT_VALID_FLAG1_PLAYER_LEDS;
        Report[CommonOffset + DS_OUTPUT_COMMON_PLAYER_LEDS] =
            DeviceExtension->PlayerLedMask;
    }

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
        SonyHid_SignBluetoothOutputReport(Report, *ReportLength);

    return STATUS_SUCCESS;
}

static NTSTATUS
SonyHid_BuildRuntimeOutputReport(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG UpdateFlags,
    _Out_writes_bytes_(SONYHID_MAX_OUTPUT_REPORT_SIZE) PUCHAR Report,
    _Out_ PULONG ReportLength)
{
    ULONG RequiredLength;

    RtlZeroMemory(Report, SONYHID_MAX_OUTPUT_REPORT_SIZE);
    *ReportLength = SonyHid_GetOutputReportLength(DeviceExtension);

    RequiredLength = SonyHid_GetRequiredOutputReportLength(DeviceExtension,
                                                           UpdateFlags);
    if (!RequiredLength ||
        !*ReportLength ||
        *ReportLength < RequiredLength)
    {
        *ReportLength = 0;
        return STATUS_NOT_SUPPORTED;
    }

    switch (DeviceExtension->DeviceType)
    {
        case SonyHidDeviceDualShock4:
            if (UpdateFlags & SONYHID_OUTPUT_FLAG_PLAYER_LEDS)
                return STATUS_NOT_SUPPORTED;

            return SonyHid_BuildDualShock4OutputReport(DeviceExtension,
                                                       UpdateFlags,
                                                       Report,
                                                       ReportLength);

        case SonyHidDeviceDualSense:
            return SonyHid_BuildDualSenseOutputReport(DeviceExtension,
                                                      UpdateFlags,
                                                      Report,
                                                      ReportLength);

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static NTSTATUS
SonyHid_ApplyOutputState(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PSONYHID_OUTPUT_STATE State)
{
    NTSTATUS Status;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;
    UCHAR Report[SONYHID_MAX_OUTPUT_REPORT_SIZE];
    ULONG ReportLength;
    ULONG Caps;
    ULONG ExpectedReportLength;

    DeviceExtension = DeviceObject->DeviceExtension;

    if (!State || State->Size < SONYHID_OUTPUT_STATE_V1_SIZE ||
        !(State->Flags & SONYHID_OUTPUT_VALID_FLAGS) ||
        (State->Flags & ~SONYHID_OUTPUT_VALID_FLAGS))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((State->Flags & SONYHID_OUTPUT_FLAG_ADAPTIVE_TRIGGERS) &&
        State->Size < sizeof(*State))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Caps = SonyHid_GetOutputCapabilities(DeviceExtension);
    if ((State->Flags & SONYHID_OUTPUT_FLAG_RUMBLE) &&
        !(Caps & SONYHID_OUTPUT_CAP_RUMBLE))
        return STATUS_NOT_SUPPORTED;

    if ((State->Flags & SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB) &&
        !(Caps & SONYHID_OUTPUT_CAP_LIGHTBAR_RGB))
        return STATUS_NOT_SUPPORTED;

    if ((State->Flags & SONYHID_OUTPUT_FLAG_PLAYER_LEDS) &&
        !(Caps & SONYHID_OUTPUT_CAP_PLAYER_LEDS))
        return STATUS_NOT_SUPPORTED;

    if ((State->Flags & SONYHID_OUTPUT_FLAG_ADAPTIVE_TRIGGERS) &&
        !(Caps & SONYHID_OUTPUT_CAP_ADAPTIVE_TRIGGERS))
        return STATUS_NOT_SUPPORTED;

    ExpectedReportLength = SonyHid_GetOutputReportLength(DeviceExtension);
    if (!ExpectedReportLength)
        return STATUS_NOT_SUPPORTED;

    ExAcquireFastMutex(&DeviceExtension->OutputLock);
    SonyHid_SetCommonOutputState(DeviceExtension, State);
    Status = SonyHid_BuildRuntimeOutputReport(DeviceExtension,
                                              State->Flags,
                                              Report,
                                              &ReportLength);
    if (NT_SUCCESS(Status))
    {
        if (ReportLength != ExpectedReportLength)
        {
            Status = STATUS_NOT_SUPPORTED;
        }
        else
        {
            Status = SonyHid_SendOutputReport(DeviceObject, Report, ReportLength);
        }
    }
    ExReleaseFastMutex(&DeviceExtension->OutputLock);

    return Status;
}

/* ------------------------------------------------------------------
 *  Default output report (LED + motor init)
 * ------------------------------------------------------------------ */

static VOID
SonyHid_SendDefaultOutput(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS Status;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;
    UCHAR Report[SONYHID_MAX_OUTPUT_REPORT_SIZE];
    ULONG ReportLength = 0;
    ULONG UpdateFlags;
    ULONG Caps;

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(Report, sizeof(Report));
    UpdateFlags = SONYHID_OUTPUT_FLAG_RUMBLE |
                  SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB;

    if (DeviceExtension->DeviceType == SonyHidDeviceDualShock3)
    {
        /* DS3: enable LED 1, zero motors */
        Report[0] = DS3_OUTPUT_REPORT_USB;
        Report[1] = 0x00;
        Report[2] = 0x00;
        Report[3] = 0x00;
        Report[4] = 0x00;
        Report[9] = 0x02; /* LED 1 */
        ReportLength = DS3_OUTPUT_REPORT_SIZE;
    }
    else
    {
        if (DeviceExtension->DeviceType == SonyHidDeviceDualSense)
            UpdateFlags |= SONYHID_OUTPUT_FLAG_PLAYER_LEDS;

        Caps = SonyHid_GetOutputCapabilities(DeviceExtension);
        UpdateFlags &= Caps;
        if (!UpdateFlags)
            return;

        ExAcquireFastMutex(&DeviceExtension->OutputLock);
        Status = SonyHid_BuildRuntimeOutputReport(DeviceExtension,
                                                  UpdateFlags,
                                                  Report,
                                                  &ReportLength);
        ExReleaseFastMutex(&DeviceExtension->OutputLock);

        if (!NT_SUCCESS(Status))
            return;
    }

    if (!ReportLength)
        return;

    if (((DeviceExtension->DeviceType == SonyHidDeviceDualShock4 ||
          DeviceExtension->DeviceType == SonyHidDeviceDualSense) &&
         !DeviceExtension->OutputReportLength) ||
        (DeviceExtension->OutputReportLength &&
         ReportLength > DeviceExtension->OutputReportLength))
    {
        DPRINT1("[SONYHID] default output report unsupported by HID caps: report=%02x built=%lu caps=%lu type=%d\n",
                Report[0],
                ReportLength,
                DeviceExtension->OutputReportLength,
                DeviceExtension->DeviceType);
        return;
    }

    Status = SonyHid_SendOutputReport(DeviceObject, Report, ReportLength);
    if (!NT_SUCCESS(Status))
        DPRINT1("[SONYHID] default output report failed with %x\n", Status);
}

/* ------------------------------------------------------------------
 *  Input dispatch (mouse class)
 * ------------------------------------------------------------------ */

static VOID
SonyHid_DispatchInputData(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension,
    _In_ PMOUSE_INPUT_DATA InputData)
{
    KIRQL OldIrql;
    ULONG InputDataConsumed;

    if (!DeviceExtension->ClassService)
        return;

    ASSERT(DeviceExtension->ClassDeviceObject);

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    (*(PSERVICE_CALLBACK_ROUTINE)DeviceExtension->ClassService)(
        DeviceExtension->ClassDeviceObject,
        InputData,
        InputData + 1,
        &InputDataConsumed);
    KeLowerIrql(OldIrql);

}

static SHORT
SonyHid_ScaleDelta(
    _In_ LONG Delta)
{
    if (Delta > 1024 || Delta < -1024)
        return 0;

    Delta /= 2;
    if (Delta > 127)
        Delta = 127;
    else if (Delta < -127)
        Delta = -127;

    return (SHORT)Delta;
}

static VOID
SonyHid_HandleTouch(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN Active,
    _In_ USHORT X,
    _In_ USHORT Y,
    _In_ BOOLEAN LeftButtonDown)
{
    MOUSE_INPUT_DATA InputData;
    BOOLEAN SendReport = FALSE;
    SHORT DeltaX = 0;
    SHORT DeltaY = 0;

    RtlZeroMemory(&InputData, sizeof(InputData));

    if (LeftButtonDown != DeviceExtension->LeftButtonDown)
    {
        InputData.ButtonFlags |= LeftButtonDown ? MOUSE_LEFT_BUTTON_DOWN : MOUSE_LEFT_BUTTON_UP;
        DeviceExtension->LeftButtonDown = LeftButtonDown;
        SendReport = TRUE;
    }

    if (Active)
    {
        if (DeviceExtension->TouchActive)
        {
            DeltaX = SonyHid_ScaleDelta((LONG)X - DeviceExtension->LastTouchX);
            DeltaY = SonyHid_ScaleDelta((LONG)Y - DeviceExtension->LastTouchY);

            if (DeltaX || DeltaY)
            {
                InputData.LastX = DeltaX;
                InputData.LastY = DeltaY;
                SendReport = TRUE;
            }
        }

        DeviceExtension->LastTouchX = X;
        DeviceExtension->LastTouchY = Y;
        DeviceExtension->TouchActive = TRUE;
    }
    else
    {
        DeviceExtension->TouchActive = FALSE;
    }

    if (SendReport)
        SonyHid_DispatchInputData(DeviceExtension, &InputData);
}

static VOID
SonyHid_DecodeTouchPoint(
    _In_reads_bytes_(4) PUCHAR Point,
    _Out_ PBOOLEAN Active,
    _Out_ PUSHORT X,
    _Out_ PUSHORT Y)
{
    *Active = !(Point[0] & DS_TOUCH_POINT_INACTIVE);
    *X = (USHORT)(Point[1] | ((Point[2] & 0x0f) << 8));
    *Y = (USHORT)(((Point[2] >> 4) & 0x0f) | (Point[3] << 4));
}

/* ------------------------------------------------------------------
 *  Battery parsing
 * ------------------------------------------------------------------ */

static VOID
SonyHid_ParseBatteryDS3(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR BatteryByte;

    if (DeviceExtension->ReportLength < 31)
        return;

    BatteryByte = DeviceExtension->Report[DS3_BYTE_BATTERY];

    if (BatteryByte >= 0xEE)
    {
        DeviceExtension->BatteryStatus = (BatteryByte & 0x01) ?
            SonyHidBatteryFull : SonyHidBatteryCharging;
        DeviceExtension->BatteryLevel = 100;
    }
    else
    {
        DeviceExtension->BatteryStatus = SonyHidBatteryDischarging;
        if (BatteryByte <= 5)
            DeviceExtension->BatteryLevel = (UCHAR)(BatteryByte * 20);
        else
            DeviceExtension->BatteryLevel = 100;
    }
    DeviceExtension->HasBattery = TRUE;
}

static VOID
SonyHid_ParseBatteryDS4(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR BatteryByte;
    ULONG Offset;

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
    {
        if (DeviceExtension->ReportLength < 32)
            return;
        Offset = 30;
    }
    else
    {
        if (DeviceExtension->ReportLength < 13)
            return;
        Offset = 12;
    }

    BatteryByte = DeviceExtension->Report[Offset];

    if (BatteryByte & 0x10)
    {
        DeviceExtension->BatteryStatus = SonyHidBatteryFull;
        DeviceExtension->BatteryLevel = 100;
    }
    else if (BatteryByte & 0x08)
    {
        DeviceExtension->BatteryStatus = SonyHidBatteryCharging;
        DeviceExtension->BatteryLevel = (UCHAR)((BatteryByte & 0x07) * 20 + 10);
    }
    else
    {
        DeviceExtension->BatteryStatus = SonyHidBatteryDischarging;
        DeviceExtension->BatteryLevel = (UCHAR)((BatteryByte & 0x07) * 20 + 5);
        if (DeviceExtension->BatteryLevel > 100)
            DeviceExtension->BatteryLevel = 100;
    }
    DeviceExtension->HasBattery = TRUE;
}

static VOID
SonyHid_ParseBatteryDS(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR BatteryByte;
    UCHAR ChargingByte;

    if (DeviceExtension->ReportLength < 54)
        return;

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
    {
        if (DeviceExtension->ReportLength < 56)
            return;

        BatteryByte = DeviceExtension->Report[54];
        ChargingByte = DeviceExtension->Report[55] & 0x0F;
    }
    else
    {
        BatteryByte = DeviceExtension->Report[53];
        ChargingByte = (DeviceExtension->Report[52] >> 4) & 0x0F;
    }

    DeviceExtension->BatteryLevel = (UCHAR)((BatteryByte & 0x0F) * 10 + 5);
    if (DeviceExtension->BatteryLevel > 100)
        DeviceExtension->BatteryLevel = 100;

    switch (ChargingByte)
    {
        case 0x0:
            DeviceExtension->BatteryStatus = SonyHidBatteryDischarging;
            break;
        case 0x1:
            DeviceExtension->BatteryStatus = SonyHidBatteryCharging;
            break;
        case 0x2:
            DeviceExtension->BatteryStatus = SonyHidBatteryFull;
            DeviceExtension->BatteryLevel = 100;
            break;
        default:
            DeviceExtension->BatteryStatus = SonyHidBatteryUnknown;
            break;
    }
    DeviceExtension->HasBattery = TRUE;
}

/* ------------------------------------------------------------------
 *  Motion sensor parsing
 * ------------------------------------------------------------------ */

static VOID
SonyHid_ParseMotionDS3(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    LONG RawX, RawY, RawZ;

    if (DeviceExtension->ReportLength < 49)
        return;

    /* DS3 accelerometer: 3 x 16-bit big-endian at offset 41 */
    RawX = ((LONG)(DeviceExtension->Report[42]) << 8) | DeviceExtension->Report[41];
    RawY = ((LONG)(DeviceExtension->Report[44]) << 8) | DeviceExtension->Report[43];
    RawZ = ((LONG)(DeviceExtension->Report[46]) << 8) | DeviceExtension->Report[45];

    DeviceExtension->AccelX = RawX - 511;
    DeviceExtension->AccelY = 511 - RawY;
    DeviceExtension->AccelZ = 511 - RawZ;
    DeviceExtension->HasMotionSensors = TRUE;
}

static VOID
SonyHid_ParseMotionDS4(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    SHORT RawGyroX, RawGyroY, RawGyroZ;
    SHORT RawAccelX, RawAccelY, RawAccelZ;

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
    {
        if (DeviceExtension->ReportLength < 78)
            return;

        RawGyroX = (SHORT)((DeviceExtension->Report[13] << 8) | DeviceExtension->Report[14]);
        RawGyroY = (SHORT)((DeviceExtension->Report[15] << 8) | DeviceExtension->Report[16]);
        RawGyroZ = (SHORT)((DeviceExtension->Report[17] << 8) | DeviceExtension->Report[18]);
        RawAccelX = (SHORT)((DeviceExtension->Report[19] << 8) | DeviceExtension->Report[20]);
        RawAccelY = (SHORT)((DeviceExtension->Report[21] << 8) | DeviceExtension->Report[22]);
        RawAccelZ = (SHORT)((DeviceExtension->Report[23] << 8) | DeviceExtension->Report[24]);
    }
    else
    {
        if (DeviceExtension->ReportLength < 64)
            return;

        RawGyroX = (SHORT)((DeviceExtension->Report[13] << 8) | DeviceExtension->Report[14]);
        RawGyroY = (SHORT)((DeviceExtension->Report[15] << 8) | DeviceExtension->Report[16]);
        RawGyroZ = (SHORT)((DeviceExtension->Report[17] << 8) | DeviceExtension->Report[18]);
        RawAccelX = (SHORT)((DeviceExtension->Report[19] << 8) | DeviceExtension->Report[20]);
        RawAccelY = (SHORT)((DeviceExtension->Report[21] << 8) | DeviceExtension->Report[22]);
        RawAccelZ = (SHORT)((DeviceExtension->Report[23] << 8) | DeviceExtension->Report[24]);
    }

    DeviceExtension->GyroX = RawGyroX;
    DeviceExtension->GyroY = RawGyroY;
    DeviceExtension->GyroZ = RawGyroZ;
    DeviceExtension->AccelX = RawAccelX;
    DeviceExtension->AccelY = RawAccelY;
    DeviceExtension->AccelZ = RawAccelZ;
    DeviceExtension->HasMotionSensors = TRUE;
}

static VOID
SonyHid_ParseMotionDS(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    SHORT RawGyroX, RawGyroY, RawGyroZ;
    SHORT RawAccelX, RawAccelY, RawAccelZ;

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
    {
        if (DeviceExtension->ReportLength < 78)
            return;

        RawGyroX = (SHORT)((DeviceExtension->Report[17] << 8) | DeviceExtension->Report[16]);
        RawGyroY = (SHORT)((DeviceExtension->Report[19] << 8) | DeviceExtension->Report[18]);
        RawGyroZ = (SHORT)((DeviceExtension->Report[21] << 8) | DeviceExtension->Report[20]);
        RawAccelX = (SHORT)((DeviceExtension->Report[23] << 8) | DeviceExtension->Report[22]);
        RawAccelY = (SHORT)((DeviceExtension->Report[25] << 8) | DeviceExtension->Report[24]);
        RawAccelZ = (SHORT)((DeviceExtension->Report[27] << 8) | DeviceExtension->Report[26]);
    }
    else
    {
        if (DeviceExtension->ReportLength < 64)
            return;

        RawGyroX = (SHORT)((DeviceExtension->Report[17] << 8) | DeviceExtension->Report[16]);
        RawGyroY = (SHORT)((DeviceExtension->Report[19] << 8) | DeviceExtension->Report[18]);
        RawGyroZ = (SHORT)((DeviceExtension->Report[21] << 8) | DeviceExtension->Report[20]);
        RawAccelX = (SHORT)((DeviceExtension->Report[23] << 8) | DeviceExtension->Report[22]);
        RawAccelY = (SHORT)((DeviceExtension->Report[25] << 8) | DeviceExtension->Report[24]);
        RawAccelZ = (SHORT)((DeviceExtension->Report[27] << 8) | DeviceExtension->Report[26]);
    }

    DeviceExtension->GyroX = RawGyroX;
    DeviceExtension->GyroY = RawGyroY;
    DeviceExtension->GyroZ = RawGyroZ;
    DeviceExtension->AccelX = RawAccelX;
    DeviceExtension->AccelY = RawAccelY;
    DeviceExtension->AccelZ = RawAccelZ;
    DeviceExtension->HasMotionSensors = TRUE;
}

/* ------------------------------------------------------------------
 *  DS3 report processing
 * ------------------------------------------------------------------ */

static VOID
SonyHid_ProcessDualShock3Report(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    BOOLEAN Active;
    BOOLEAN TouchActive;
    USHORT TouchX, TouchY;

    if (DeviceExtension->ReportLength < DS3_INPUT_REPORT_SIZE ||
        DeviceExtension->Report[0] != DS3_INPUT_REPORT_USB)
    {
        return;
    }

    /* DS3 does not have a real touchpad - use the right analog stick
     * as cursor when the PS button is held. A future version can add
     * gyro-based pointing. For now, no mouse output from DS3. */

    SonyHid_ParseBatteryDS3(DeviceExtension);
    SonyHid_ParseMotionDS3(DeviceExtension);

    /* The DS3 reports an analog value at offset 13 for the "touchpad"
     * button (0x00 = pressed) - we treat analog button as pressed when
     * value drops below 0x30 */
    TouchActive = !!(DeviceExtension->Report[DS3_BYTE_BUTTONS3] & DS3_BTN_TOUCHPAD);
    TouchX = DeviceExtension->Report[DS3_BYTE_RSTICK_X];
    TouchY = DeviceExtension->Report[DS3_BYTE_RSTICK_Y];
    Active = TouchActive;

    SonyHid_HandleTouch(DeviceExtension, Active, TouchX, TouchY, TouchActive);
}

/* ------------------------------------------------------------------
 *  DS4 report processing (USB + BT)
 * ------------------------------------------------------------------ */

static VOID
SonyHid_ProcessDualShock4Report(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    BOOLEAN Active[2];
    BOOLEAN LeftButtonDown;
    UCHAR TouchReports;
    UCHAR Index;
    USHORT X[2], Y[2];
    PUCHAR TouchReport;
    PUCHAR Point;
    BOOLEAN UseBT;
    ULONG TouchBase;
    ULONG TouchStride;

    UseBT = (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth);

    if (UseBT)
    {
        if (DeviceExtension->ReportLength < DS4_INPUT_REPORT_BT_SIZE ||
            DeviceExtension->Report[0] != DS4_INPUT_REPORT_BT)
            return;

        LeftButtonDown = !!(DeviceExtension->Report[8] & DS4_BUTTONS3_TOUCHPAD);
        TouchReports = DeviceExtension->Report[35];
        if (!TouchReports)
        {
            SonyHid_HandleTouch(DeviceExtension, FALSE, 0, 0, LeftButtonDown);
            goto ParseMotionAndBattery;
        }
        if (TouchReports > 2)
            TouchReports = 2;
        TouchBase = 36;
        TouchStride = 9;
    }
    else
    {
        if (DeviceExtension->ReportLength < DS4_INPUT_REPORT_USB_SIZE ||
            DeviceExtension->Report[0] != DS4_INPUT_REPORT_USB)
            return;

        LeftButtonDown = !!(DeviceExtension->Report[7] & DS4_BUTTONS3_TOUCHPAD);
        TouchReports = DeviceExtension->Report[33];
        if (!TouchReports)
        {
            SonyHid_HandleTouch(DeviceExtension, FALSE, 0, 0, LeftButtonDown);
            goto ParseMotionAndBattery;
        }
        if (TouchReports > 3)
            TouchReports = 3;
        TouchBase = 34;
        TouchStride = 9;
    }

    /* Process touch reports - up to 2 points tracked simultaneously */
    for (Index = 0; Index < TouchReports && Index < 2; Index++)
    {
        TouchReport = &DeviceExtension->Report[TouchBase + (Index * TouchStride)];
        Point = &TouchReport[1];
        SonyHid_DecodeTouchPoint(Point, &Active[Index], &X[Index], &Y[Index]);

        if (!Active[Index])
        {
            Point = &TouchReport[5];
            SonyHid_DecodeTouchPoint(Point, &Active[Index], &X[Index], &Y[Index]);
        }
    }

    /* Dispatch first active touch point to mouse */
    for (Index = 0; Index < TouchReports && Index < 2; Index++)
    {
        if (Active[Index])
        {
            SonyHid_HandleTouch(DeviceExtension, TRUE, X[Index], Y[Index], LeftButtonDown);
            break;
        }
    }

    if (Index >= TouchReports || Index >= 2 || !Active[0])
        SonyHid_HandleTouch(DeviceExtension, FALSE, 0, 0, LeftButtonDown);

ParseMotionAndBattery:
    SonyHid_ParseBatteryDS4(DeviceExtension);
    SonyHid_ParseMotionDS4(DeviceExtension);
}

/* ------------------------------------------------------------------
 *  DualSense report processing (USB + BT)
 * ------------------------------------------------------------------ */

static VOID
SonyHid_ProcessDualSenseReport(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    BOOLEAN Active[2];
    BOOLEAN LeftButtonDown;
    USHORT X[2], Y[2];
    PUCHAR Point;

    if (DeviceExtension->ConnectionType == SonyHidConnectionBluetooth)
    {
        if (DeviceExtension->ReportLength < DS_INPUT_REPORT_BT_SIZE ||
            DeviceExtension->Report[0] != DS_INPUT_REPORT_BT)
            return;

        LeftButtonDown = !!(DeviceExtension->Report[11] & DS_BUTTONS3_TOUCHPAD);

        Point = &DeviceExtension->Report[35];
        SonyHid_DecodeTouchPoint(Point, &Active[0], &X[0], &Y[0]);
        if (!Active[0])
        {
            Point = &DeviceExtension->Report[39];
            SonyHid_DecodeTouchPoint(Point, &Active[0], &X[0], &Y[0]);
        }

        Point = &DeviceExtension->Report[43];
        SonyHid_DecodeTouchPoint(Point, &Active[1], &X[1], &Y[1]);
        if (!Active[1])
        {
            Point = &DeviceExtension->Report[47];
            SonyHid_DecodeTouchPoint(Point, &Active[1], &X[1], &Y[1]);
        }
    }
    else
    {
        if (DeviceExtension->ReportLength < DS_INPUT_REPORT_USB_SIZE ||
            DeviceExtension->Report[0] != DS_INPUT_REPORT_USB)
            return;

        LeftButtonDown = !!(DeviceExtension->Report[10] & DS_BUTTONS3_TOUCHPAD);

        Point = &DeviceExtension->Report[33];
        SonyHid_DecodeTouchPoint(Point, &Active[0], &X[0], &Y[0]);
        if (!Active[0])
        {
            Point = &DeviceExtension->Report[37];
            SonyHid_DecodeTouchPoint(Point, &Active[0], &X[0], &Y[0]);
        }

        /* DualSense USB only reports one touch point */
        Active[1] = FALSE;
    }

    if (Active[0])
        SonyHid_HandleTouch(DeviceExtension, TRUE, X[0], Y[0], LeftButtonDown);
    else
        SonyHid_HandleTouch(DeviceExtension, FALSE, 0, 0, LeftButtonDown);

    SonyHid_ParseBatteryDS(DeviceExtension);
    SonyHid_ParseMotionDS(DeviceExtension);
}

/* ------------------------------------------------------------------
 *  Report dispatcher
 * ------------------------------------------------------------------ */

static VOID
SonyHid_ProcessReport(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    /* Auto-detect connection type on first report */
    if (DeviceExtension->ConnectionType == SonyHidConnectionUnknown)
        DeviceExtension->ConnectionType = SonyHid_DetectConnection(DeviceExtension);

    if (!DeviceExtension->ClassService)
        return;

    switch (DeviceExtension->DeviceType)
    {
        case SonyHidDeviceDualShock3:
            SonyHid_ProcessDualShock3Report(DeviceExtension);
            break;

        case SonyHidDeviceDualShock4:
            SonyHid_ProcessDualShock4Report(DeviceExtension);
            break;

        case SonyHidDeviceDualSense:
            SonyHid_ProcessDualSenseReport(DeviceExtension);
            break;

        default:
            break;
    }
}

/* ------------------------------------------------------------------
 *  Read completion + initiate
 * ------------------------------------------------------------------ */

NTSTATUS
NTAPI
SonyHid_ReadCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PSONYHID_DEVICE_EXTENSION DeviceExtension = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->IoStatus.Status == STATUS_PRIVILEGE_NOT_HELD ||
        Irp->IoStatus.Status == STATUS_DEVICE_NOT_CONNECTED ||
        Irp->IoStatus.Status == STATUS_CANCELLED ||
        DeviceExtension->StopReadReport)
    {
        DeviceExtension->ReadReportActive = FALSE;
        DeviceExtension->StopReadReport = FALSE;
        KeSetEvent(&DeviceExtension->ReadCompletionEvent, IO_NO_INCREMENT, FALSE);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    if (NT_SUCCESS(Irp->IoStatus.Status))
    {
        SonyHid_ProcessReport(DeviceExtension);
    }
    else
    {
        DeviceExtension->ReadReportActive = FALSE;
        KeSetEvent(&DeviceExtension->ReadCompletionEvent, IO_NO_INCREMENT, FALSE);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    SonyHid_InitiateRead(DeviceExtension);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
NTAPI
SonyHid_ReadMirrorCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PSONYHID_DEVICE_EXTENSION DeviceExtension = Context;
    PUCHAR Buffer;
    SIZE_T Length;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (NT_SUCCESS(Irp->IoStatus.Status) &&
        Irp->IoStatus.Information &&
        DeviceExtension->Report)
    {
        Buffer = NULL;
        if (Irp->MdlAddress)
            Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
        if (!Buffer)
            Buffer = Irp->AssociatedIrp.SystemBuffer;

        if (Buffer)
        {
            Length = Irp->IoStatus.Information;
            if (Length > DeviceExtension->ReportLength)
                Length = DeviceExtension->ReportLength;

            RtlCopyMemory(DeviceExtension->Report, Buffer, Length);
            SonyHid_ProcessReport(DeviceExtension);
        }
    }

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);

    return STATUS_CONTINUE_COMPLETION;
}

NTSTATUS
SonyHid_InitiateRead(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    if (!DeviceExtension->Irp || !DeviceExtension->ReportMdl || !DeviceExtension->FileObject)
        return STATUS_DEVICE_NOT_READY;

    KeClearEvent(&DeviceExtension->ReadCompletionEvent);

    IoReuseIrp(DeviceExtension->Irp, STATUS_SUCCESS);
    DeviceExtension->Irp->MdlAddress = DeviceExtension->ReportMdl;
    DeviceExtension->Irp->UserBuffer = DeviceExtension->Report;
    DeviceExtension->Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    DeviceExtension->Irp->IoStatus.Information = 0;

    IoStack = IoGetNextIrpStackLocation(DeviceExtension->Irp);
    RtlZeroMemory(IoStack, sizeof(IO_STACK_LOCATION));
    IoStack->MajorFunction = IRP_MJ_READ;
    IoStack->Parameters.Read.Length = DeviceExtension->ReportLength;
    IoStack->FileObject = DeviceExtension->FileObject;

    IoSetCompletionRoutine(DeviceExtension->Irp,
                           SonyHid_ReadCompletion,
                           DeviceExtension,
                           TRUE,
                           TRUE,
                           TRUE);

    DeviceExtension->ReadReportActive = TRUE;
    Status = IoCallDriver(DeviceExtension->NextDeviceObject, DeviceExtension->Irp);

    if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
    {
        DeviceExtension->ReadReportActive = FALSE;
        KeSetEvent(&DeviceExtension->ReadCompletionEvent, IO_NO_INCREMENT, FALSE);
    }

    return Status;
}

/* ------------------------------------------------------------------
 *  Read stop
 * ------------------------------------------------------------------ */

static VOID
SonyHid_StopRead(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension)
{
    if (!DeviceExtension->ReadReportActive)
        return;

    DeviceExtension->StopReadReport = TRUE;
    IoCancelIrp(DeviceExtension->Irp);
    KeWaitForSingleObject(&DeviceExtension->ReadCompletionEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
}

/* ------------------------------------------------------------------
 *  IRP dispatch: Create / Close
 * ------------------------------------------------------------------ */

NTSTATUS
NTAPI
SonyHid_Create(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;

    DeviceExtension = DeviceObject->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, SonyHid_ForwardCompletion, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    if (NT_SUCCESS(Status) &&
        IoStack->FileObject &&
        DeviceExtension->FileObject == NULL &&
        IoStack->Parameters.Create.SecurityContext &&
        IoStack->Parameters.Create.SecurityContext->DesiredAccess)
    {
        DeviceExtension->FileObject = IoStack->FileObject;
        Status = SonyHid_InitiateRead(DeviceExtension);
        if (Status == STATUS_PENDING)
            Status = STATUS_SUCCESS;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
SonyHid_Close(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;

    DeviceExtension = DeviceObject->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    if (DeviceExtension->FileObject == IoStack->FileObject)
    {
        SonyHid_StopRead(DeviceExtension);
        DeviceExtension->FileObject = NULL;
        DeviceExtension->TouchActive = FALSE;
        DeviceExtension->LeftButtonDown = FALSE;
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
}

/* ------------------------------------------------------------------
 *  IRP dispatch: InternalDeviceControl (mouse IOCTLs)
 * ------------------------------------------------------------------ */

NTSTATUS
NTAPI
SonyHid_InternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;
    PMOUSE_ATTRIBUTES Attributes;
    PCONNECT_DATA ConnectData;
    NTSTATUS Status;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_MOUSE_QUERY_ATTRIBUTES:
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(MOUSE_ATTRIBUTES))
            {
                Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_BUFFER_TOO_SMALL;
            }

            Attributes = Irp->AssociatedIrp.SystemBuffer;
            Attributes->MouseIdentifier = DeviceExtension->MouseIdentifier;
            Attributes->NumberOfButtons = 1;
            Attributes->SampleRate = 0;
            Attributes->InputDataQueueLength = 2;

            Irp->IoStatus.Information = sizeof(MOUSE_ATTRIBUTES);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;

        case IOCTL_INTERNAL_MOUSE_CONNECT:
            if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(CONNECT_DATA))
            {
                Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_INVALID_PARAMETER;
            }

            if (DeviceExtension->ClassService)
            {
                Irp->IoStatus.Status = STATUS_SHARING_VIOLATION;
                IoCompleteRequest(Irp, IO_NO_INCREMENT);
                return STATUS_SHARING_VIOLATION;
            }

            ConnectData = IoStack->Parameters.DeviceIoControl.Type3InputBuffer;
            DeviceExtension->ClassDeviceObject = ConnectData->ClassDeviceObject;
            DeviceExtension->ClassService = ConnectData->ClassService;

            Status = STATUS_SUCCESS;
            if (DeviceExtension->FileObject && !DeviceExtension->ReadReportActive)
            {
                Status = SonyHid_InitiateRead(DeviceExtension);
                if (Status == STATUS_PENDING)
                    Status = STATUS_SUCCESS;
            }

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IOCTL_INTERNAL_MOUSE_DISCONNECT:
            SonyHid_StopRead(DeviceExtension);
            DeviceExtension->ClassDeviceObject = NULL;
            DeviceExtension->ClassService = NULL;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;

        case IOCTL_INTERNAL_MOUSE_ENABLE:
        case IOCTL_INTERNAL_MOUSE_DISABLE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_SUCCESS;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
    }
}

/* ------------------------------------------------------------------
 *  IRP dispatch: DeviceControl
 *
 *  TODO: IOCTL_SONYHID_* is private bring-up plumbing. Before exposing
 *  this as an upstream-supported ABI, move the definitions to a public
 *  header, add a real user-mode consumer/test, and verify the output
 *  contract against native behavior where possible.
 * ------------------------------------------------------------------ */

NTSTATUS
NTAPI
SonyHid_DeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;
    PSONYHID_OUTPUT_CAPABILITIES Caps;
    PSONYHID_OUTPUT_STATE State;
    NTSTATUS Status;
    ULONG Information;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    Information = 0;

    switch (IoStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_SONYHID_GET_OUTPUT_CAPABILITIES:
            if (IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Caps))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Caps = Irp->AssociatedIrp.SystemBuffer;
            if (!Caps)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            RtlZeroMemory(Caps, sizeof(*Caps));
            Caps->Size = sizeof(*Caps);
            Caps->Flags = SonyHid_GetOutputCapabilities(DeviceExtension);
            Caps->OutputReportLength = Caps->Flags ?
                SonyHid_GetOutputReportLength(DeviceExtension) : 0;

            Information = sizeof(*Caps);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_SONYHID_SET_OUTPUT_STATE:
            if (IoStack->Parameters.DeviceIoControl.InputBufferLength < SONYHID_OUTPUT_STATE_V1_SIZE)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            State = Irp->AssociatedIrp.SystemBuffer;
            if (!State)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if ((State->Flags & SONYHID_OUTPUT_FLAG_ADAPTIVE_TRIGGERS) &&
                IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*State))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Status = SonyHid_ApplyOutputState(DeviceObject, State);
            break;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/* ------------------------------------------------------------------
 *  IRP dispatch: PassThrough (read / write / device control / etc.)
 * ------------------------------------------------------------------ */

NTSTATUS
NTAPI
SonyHid_PassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PSONYHID_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IoStack;

    DeviceExtension = DeviceObject->DeviceExtension;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    if (IoStack->MajorFunction == IRP_MJ_READ && DeviceExtension->Report)
    {
        IoCopyCurrentIrpStackLocationToNext(Irp);
        IoSetCompletionRoutine(Irp,
                               SonyHid_ReadMirrorCompletion,
                               DeviceExtension,
                               TRUE,
                               TRUE,
                               TRUE);
        return IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
    }

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
}

/* ------------------------------------------------------------------
 *  IRP dispatch: Power
 * ------------------------------------------------------------------ */

NTSTATUS
NTAPI
SonyHid_Power(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PSONYHID_DEVICE_EXTENSION DeviceExtension;

    DeviceExtension = DeviceObject->DeviceExtension;
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->NextDeviceObject, Irp);
}

/* ------------------------------------------------------------------
 *  Start device - query HID capabilities and init
 * ------------------------------------------------------------------ */

static NTSTATUS
SonyHid_StartDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    NTSTATUS Status;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;
    HID_COLLECTION_INFORMATION Information;
    HIDP_CAPS Capabilities;
    PVOID PreparsedData;

    DeviceExtension = DeviceObject->DeviceExtension;

    Status = SonyHid_SubmitRequest(DeviceObject,
                                   IOCTL_HID_GET_COLLECTION_INFORMATION,
                                   0,
                                   NULL,
                                   sizeof(Information),
                                   &Information);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("[SONYHID] failed to query collection information %x\n", Status);
        return Status;
    }

    DeviceExtension->CollectionInformation = Information;
    DeviceExtension->DeviceType = SonyHid_GetDeviceType(Information.VendorID, Information.ProductID);
    if (DeviceExtension->DeviceType == SonyHidDeviceUnknown)
    {
        DPRINT1("[SONYHID] unsupported device vid=%04x pid=%04x\n",
                Information.VendorID, Information.ProductID);
        return STATUS_NOT_SUPPORTED;
    }

    PreparsedData = ExAllocatePoolWithTag(NonPagedPool, Information.DescriptorSize, SONYHID_TAG);
    if (!PreparsedData)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = SonyHid_SubmitRequest(DeviceObject,
                                   IOCTL_HID_GET_COLLECTION_DESCRIPTOR,
                                   0,
                                   NULL,
                                   Information.DescriptorSize,
                                   PreparsedData);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(PreparsedData, SONYHID_TAG);
        return Status;
    }

    Status = HidP_GetCaps(PreparsedData, &Capabilities);

    if (Status != HIDP_STATUS_SUCCESS)
    {
        ExFreePoolWithTag(PreparsedData, SONYHID_TAG);
        DPRINT1("[SONYHID] HidP_GetCaps failed %x\n", Status);
        return Status;
    }

    if (Capabilities.UsagePage != HID_USAGE_PAGE_GENERIC ||
        (Capabilities.Usage != HID_USAGE_GENERIC_GAMEPAD &&
         Capabilities.Usage != HID_USAGE_GENERIC_JOYSTICK))
    {
        DPRINT1("[SONYHID] not a gamepad/joystick (usage=%04x/%04x)\n",
                Capabilities.UsagePage, Capabilities.Usage);
        ExFreePoolWithTag(PreparsedData, SONYHID_TAG);
        return STATUS_NOT_SUPPORTED;
    }

    DeviceExtension->ReportLength = Capabilities.InputReportByteLength;
    DeviceExtension->OutputReportLength = Capabilities.OutputReportByteLength;
    DeviceExtension->FeatureReportLength = Capabilities.FeatureReportByteLength;
    SonyHid_QueryOutputReportId(PreparsedData,
                                &Capabilities,
                                DeviceExtension);
    ExFreePoolWithTag(PreparsedData, SONYHID_TAG);
    if ((DeviceExtension->DeviceType == SonyHidDeviceDualShock4 &&
         (DeviceExtension->ReportLength >= DS4_INPUT_REPORT_BT_SIZE ||
          DeviceExtension->OutputReportLength >= DS4_OUTPUT_REPORT_BT_SIZE)) ||
        (DeviceExtension->DeviceType == SonyHidDeviceDualSense &&
         (DeviceExtension->ReportLength >= DS_INPUT_REPORT_BT_SIZE ||
          DeviceExtension->OutputReportLength >= DS_OUTPUT_REPORT_BT_SIZE)))
    {
        DeviceExtension->ConnectionType = SonyHidConnectionBluetooth;
    }
    else
    {
        DeviceExtension->ConnectionType = SonyHidConnectionUsb;
    }

    /* Minimum usable report size; DS3 BT sends 49, DS4 USB 64, DS BT 78 */
    if (DeviceExtension->ReportLength < DS3_INPUT_REPORT_SIZE)
        return STATUS_NOT_SUPPORTED;

    DeviceExtension->Report = ExAllocatePoolWithTag(NonPagedPool,
                                                     DeviceExtension->ReportLength,
                                                     SONYHID_TAG);
    if (!DeviceExtension->Report)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(DeviceExtension->Report, DeviceExtension->ReportLength);

    DeviceExtension->ReportMdl = IoAllocateMdl(DeviceExtension->Report,
                                                DeviceExtension->ReportLength,
                                                FALSE,
                                                FALSE,
                                                NULL);
    if (!DeviceExtension->ReportMdl)
    {
        ExFreePoolWithTag(DeviceExtension->Report, SONYHID_TAG);
        DeviceExtension->Report = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    MmBuildMdlForNonPagedPool(DeviceExtension->ReportMdl);

    SonyHid_SendDefaultOutput(DeviceObject);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------
 *  Resource cleanup
 * ------------------------------------------------------------------ */

static VOID
SonyHid_FreeResources(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PSONYHID_DEVICE_EXTENSION DeviceExtension;

    DeviceExtension = DeviceObject->DeviceExtension;

    if (DeviceExtension->ReportMdl)
    {
        IoFreeMdl(DeviceExtension->ReportMdl);
        DeviceExtension->ReportMdl = NULL;
    }

    if (DeviceExtension->Report)
    {
        ExFreePoolWithTag(DeviceExtension->Report, SONYHID_TAG);
        DeviceExtension->Report = NULL;
    }
}

/* ------------------------------------------------------------------
 *  PnP dispatch
 * ------------------------------------------------------------------ */

NTSTATUS
NTAPI
SonyHid_Pnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;
    KEVENT Event;
    NTSTATUS Status;

    DeviceExtension = DeviceObject->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);

    switch (IoStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            KeInitializeEvent(&Event, NotificationEvent, FALSE);
            IoCopyCurrentIrpStackLocationToNext(Irp);
            IoSetCompletionRoutine(Irp, SonyHid_ForwardCompletion, &Event, TRUE, TRUE, TRUE);

            Status = IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
                Status = Irp->IoStatus.Status;
            }

            if (NT_SUCCESS(Status))
                Status = SonyHid_StartDevice(DeviceObject);

            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            SonyHid_StopRead(DeviceExtension);
            SonyHid_FreeResources(DeviceObject);
            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(DeviceExtension->NextDeviceObject, Irp);

        case IRP_MN_REMOVE_DEVICE:
            SonyHid_StopRead(DeviceExtension);
            SonyHid_FreeResources(DeviceObject);

            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->NextDeviceObject, Irp);

            if (DeviceExtension->Irp)
            {
                IoFreeIrp(DeviceExtension->Irp);
                DeviceExtension->Irp = NULL;
            }

            IoDetachDevice(DeviceExtension->NextDeviceObject);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            IoSkipCurrentIrpStackLocation(Irp);
            return IoCallDriver(DeviceExtension->NextDeviceObject, Irp);
    }
}

/* ------------------------------------------------------------------
 *  AddDevice
 * ------------------------------------------------------------------ */

NTSTATUS
NTAPI
SonyHid_AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT NextDeviceObject;
    PSONYHID_DEVICE_EXTENSION DeviceExtension;
    POWER_STATE State;

    Status = IoCreateDevice(DriverObject,
                            sizeof(SONYHID_DEVICE_EXTENSION),
                            NULL,
                            FILE_DEVICE_MOUSE,
                            0,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    NextDeviceObject = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (!NextDeviceObject)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));

    DeviceExtension->NextDeviceObject = NextDeviceObject;
    DeviceExtension->MouseIdentifier = MOUSE_HID_HARDWARE;
    DeviceExtension->ConnectionType = SonyHidConnectionUnknown;
    ExInitializeFastMutex(&DeviceExtension->OutputLock);
    KeInitializeEvent(&DeviceExtension->ReadCompletionEvent, NotificationEvent, TRUE);
    SonyHid_ResetOutputState(DeviceExtension);

    DeviceExtension->Irp = IoAllocateIrp(NextDeviceObject->StackSize, FALSE);
    if (!DeviceExtension->Irp)
    {
        IoDetachDevice(NextDeviceObject);
        IoDeleteDevice(DeviceObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    State.DeviceState = PowerDeviceD0;
    PoSetPowerState(DeviceObject, DevicePowerState, State);

    DeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------
 *  Driver unload - full cleanup
 * ------------------------------------------------------------------ */

VOID
NTAPI
SonyHid_Unload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    /*
     * All device objects are detached and deleted in IRP_MN_REMOVE_DEVICE.
     * By the time the driver unloads, no devices should remain.
     * If any remain, it indicates a PnP ordering bug.
     */
    DPRINT("[SONYHID] DriverUnload\n");
}

/* ------------------------------------------------------------------
 *  DriverEntry
 * ------------------------------------------------------------------ */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_CREATE] = SonyHid_Create;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = SonyHid_Close;
    DriverObject->MajorFunction[IRP_MJ_READ] = SonyHid_PassThrough;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = SonyHid_PassThrough;
    DriverObject->MajorFunction[IRP_MJ_FLUSH_BUFFERS] = SonyHid_PassThrough;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = SonyHid_DeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = SonyHid_InternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_POWER] = SonyHid_Power;
    DriverObject->MajorFunction[IRP_MJ_PNP] = SonyHid_Pnp;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = SonyHid_PassThrough;
    DriverObject->DriverUnload = SonyHid_Unload;
    DriverObject->DriverExtension->AddDevice = SonyHid_AddDevice;
    return STATUS_SUCCESS;
}
