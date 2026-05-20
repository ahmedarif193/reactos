#pragma once

#define _HIDPI_NO_FUNCTION_MACROS_
#include <ntddk.h>
#include <hidclass.h>
#include <hidpddi.h>
#include <hidpi.h>
#include <hidusage.h>
#include <ntddmou.h>
#include <kbdmou.h>

#define NDEBUG
#include <debug.h>

#define SONYHID_TAG 'HySP'

#define IOCTL_SONYHID_GET_OUTPUT_CAPABILITIES \
    CTL_CODE(FILE_DEVICE_MOUSE, 0x900, METHOD_BUFFERED, FILE_READ_ACCESS)
#define IOCTL_SONYHID_SET_OUTPUT_STATE \
    CTL_CODE(FILE_DEVICE_MOUSE, 0x901, METHOD_BUFFERED, FILE_WRITE_ACCESS)

#define SONYHID_TRIGGER_EFFECT_SIZE             11

#define SONYHID_OUTPUT_CAP_RUMBLE               0x00000001
#define SONYHID_OUTPUT_CAP_LIGHTBAR_RGB         0x00000002
#define SONYHID_OUTPUT_CAP_PLAYER_LEDS          0x00000004
#define SONYHID_OUTPUT_CAP_ADAPTIVE_TRIGGERS    0x00000008

#define SONYHID_OUTPUT_FLAG_RUMBLE              0x00000001
#define SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB        0x00000002
#define SONYHID_OUTPUT_FLAG_PLAYER_LEDS         0x00000004
#define SONYHID_OUTPUT_FLAG_ADAPTIVE_TRIGGERS   0x00000008
#define SONYHID_OUTPUT_VALID_FLAGS              (SONYHID_OUTPUT_FLAG_RUMBLE | \
                                                 SONYHID_OUTPUT_FLAG_LIGHTBAR_RGB | \
                                                 SONYHID_OUTPUT_FLAG_PLAYER_LEDS | \
                                                 SONYHID_OUTPUT_FLAG_ADAPTIVE_TRIGGERS)

typedef struct _SONYHID_OUTPUT_CAPABILITIES
{
    ULONG Size;
    ULONG Flags;
    ULONG OutputReportLength;
} SONYHID_OUTPUT_CAPABILITIES, *PSONYHID_OUTPUT_CAPABILITIES;

typedef struct _SONYHID_OUTPUT_STATE
{
    ULONG Size;
    ULONG Flags;
    UCHAR StrongMotorMagnitude;
    UCHAR WeakMotorMagnitude;
    UCHAR LightbarRed;
    UCHAR LightbarGreen;
    UCHAR LightbarBlue;
    UCHAR PlayerLedMask;
    UCHAR Reserved[2];
    UCHAR RightTriggerEffect[SONYHID_TRIGGER_EFFECT_SIZE];
    UCHAR LeftTriggerEffect[SONYHID_TRIGGER_EFFECT_SIZE];
} SONYHID_OUTPUT_STATE, *PSONYHID_OUTPUT_STATE;

#define SONYHID_OUTPUT_STATE_V1_SIZE \
    (sizeof(ULONG) + sizeof(ULONG) + (8 * sizeof(UCHAR)))

/* -- Vendor ID ---------------------------------------------------- */
#define SONY_VENDOR_ID 0x054c

/* -- DualShock 3 Product IDs -------------------------------------- */
#define SONY_PRODUCT_DUALSHOCK3         0x0268
#define SONY_PRODUCT_DUALSHOCK3_ALT     0x042f

/* -- DualShock 4 Product IDs -------------------------------------- */
#define SONY_PRODUCT_DUALSHOCK4_V1      0x05c4
#define SONY_PRODUCT_DUALSHOCK4_V2      0x09cc
#define SONY_PRODUCT_DUALSHOCK4_DONGLE  0x0ba0

/* -- DualSense Product IDs ---------------------------------------- */
#define SONY_PRODUCT_DUALSENSE          0x0ce6
#define SONY_PRODUCT_DUALSENSE_EDGE     0x0df2

/* -- Device type --------------------------------------------------- */
typedef enum _SONYHID_DEVICE_TYPE
{
    SonyHidDeviceUnknown,
    SonyHidDeviceDualShock3,
    SonyHidDeviceDualShock4,
    SonyHidDeviceDualSense
} SONYHID_DEVICE_TYPE;

/* -- Connection type ---------------------------------------------- */
typedef enum _SONYHID_CONNECTION_TYPE
{
    SonyHidConnectionUnknown,
    SonyHidConnectionUsb,
    SonyHidConnectionBluetooth
} SONYHID_CONNECTION_TYPE;

/* -- Battery status ------------------------------------------------ */
typedef enum _SONYHID_BATTERY_STATUS
{
    SonyHidBatteryUnknown,
    SonyHidBatteryDischarging,
    SonyHidBatteryCharging,
    SonyHidBatteryFull
} SONYHID_BATTERY_STATUS;

/* -- Device extension ---------------------------------------------- */
typedef struct _SONYHID_DEVICE_EXTENSION
{
    PDEVICE_OBJECT NextDeviceObject;
    PIRP Irp;
    KEVENT ReadCompletionEvent;

    PDEVICE_OBJECT ClassDeviceObject;
    PVOID ClassService;

    USHORT MouseIdentifier;

    PMDL ReportMdl;
    PUCHAR Report;
    ULONG ReportLength;
    ULONG OutputReportLength;
    ULONG FeatureReportLength;
    UCHAR OutputReportId;
    BOOLEAN HasOutputReportId;
    ULONG ReadTraceCount;
    ULONG NoClassTraceCount;
    ULONG TouchTraceCount;
    ULONG DispatchTraceCount;
    PFILE_OBJECT FileObject;
    FAST_MUTEX OutputLock;

    BOOLEAN ReadReportActive;
    BOOLEAN StopReadReport;
    BOOLEAN TouchActive;
    BOOLEAN LeftButtonDown;

    USHORT LastTouchX;
    USHORT LastTouchY;

    HID_COLLECTION_INFORMATION CollectionInformation;
    SONYHID_DEVICE_TYPE DeviceType;
    SONYHID_CONNECTION_TYPE ConnectionType;

    /* -- Motion sensors --------------------------------------- */
    BOOLEAN HasMotionSensors;
    LONG AccelX, AccelY, AccelZ;
    LONG GyroX, GyroY, GyroZ;

    /* -- Battery ----------------------------------------------- */
    BOOLEAN HasBattery;
    UCHAR BatteryLevel;
    SONYHID_BATTERY_STATUS BatteryStatus;

    /* -- Multi-touch tracking ---------------------------------- */
    BOOLEAN TouchTracked[2];
    USHORT TouchX[2];
    USHORT TouchY[2];

    /* -- Input dispatch throttling ----------------------------- */
    LARGE_INTEGER LastDispatchTime;

    /* -- Runtime output state ---------------------------------- */
    UCHAR StrongMotorMagnitude;
    UCHAR WeakMotorMagnitude;
    UCHAR LightbarRed;
    UCHAR LightbarGreen;
    UCHAR LightbarBlue;
    UCHAR PlayerLedMask;
    UCHAR RightTriggerEffect[SONYHID_TRIGGER_EFFECT_SIZE];
    UCHAR LeftTriggerEffect[SONYHID_TRIGGER_EFFECT_SIZE];
    UCHAR OutputSequence;
} SONYHID_DEVICE_EXTENSION, *PSONYHID_DEVICE_EXTENSION;

NTSTATUS
SonyHid_InitiateRead(
    _In_ PSONYHID_DEVICE_EXTENSION DeviceExtension);
