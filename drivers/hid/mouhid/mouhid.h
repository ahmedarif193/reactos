#pragma once

#define _HIDPI_NO_FUNCTION_MACROS_
#include <ntddk.h>
#include <hidclass.h>
#include <hidpddi.h>
#include <hidpi.h>
#define NDEBUG
#include <debug.h>
#include <ntddmou.h>
#include <kbdmou.h>
#include <debug.h>

#include "ptpframe.h"

typedef struct
{
    //
    // lower device object
    //
    PDEVICE_OBJECT NextDeviceObject;

    //
    // irp which is used for reading input reports
    //
    PIRP Irp;

    //
    // event
    //
    KEVENT ReadCompletionEvent;

    //
    // device object for class callback
    //
    PDEVICE_OBJECT ClassDeviceObject;

    //
    // class callback
    //
    PVOID ClassService;

    //
    // mouse type
    //
    USHORT MouseIdentifier;

    //
    // wheel usage page
    //
    USHORT WheelUsagePage;

    //
    // buffer for the four usage lists below
    //
    PVOID UsageListBuffer;

    //
    // usage list length
    //
    USHORT UsageListLength;

    //
    // current usage list length
    //
    PUSAGE CurrentUsageList;

    //
    // previous usage list
    //
    PUSAGE PreviousUsageList;

    //
    // removed usage item list
    //
    PUSAGE BreakUsageList;

    //
    // new item usage list
    //
    PUSAGE MakeUsageList;

    //
    // preparsed data
    //
    PVOID PreparsedData;

    //
    // mdl for reading input report
    //
    PMDL ReportMDL;

    //
    // input report buffer
    //
    PCHAR Report;

    //
    // input report length
    //
    ULONG ReportLength;

    //
    // file object the device is reading reports from
    //
    PFILE_OBJECT FileObject;

    //
    // report read is active
    //
    UCHAR ReadReportActive;

    //
    // stop reading flag
    //
    UCHAR StopReadReport;

    //
    // mouse absolute
    //
    UCHAR MouseAbsolute;

    //
    // value caps x
    //
    HIDP_VALUE_CAPS ValueCapsX;

    //
    // value caps y button
    //
    HIDP_VALUE_CAPS ValueCapsY;

    /* Precision Touchpad state. */
    BOOLEAN Touchpad;
    BOOLEAN Digitizer;
    BOOLEAN DigitizerTipDown;
    BOOLEAN TouchContactActive;
    BOOLEAN ScrollActive;
    UCHAR TouchpadButtons;
    UCHAR Reserved[2];
    USHORT FingerLinkCount;
    USHORT DigitizerLink;
    USHORT FingerLinks[MOUHID_PTP_MAX_CONTACTS];
    ULONG PrimaryContactId;
    LONG LastTouchX;
    LONG LastTouchY;
    LONG TouchRemainderX;
    LONG TouchRemainderY;
    LONG LastScrollX;
    LONG LastScrollY;
    LONG ScrollRemainderX;
    LONG ScrollRemainderY;
    MOUHID_PTP_FRAME_ASSEMBLER TouchpadFrameAssembler;

} MOUHID_DEVICE_EXTENSION, *PMOUHID_DEVICE_EXTENSION;

#define WHEEL_DELTA 120
#define VIRTUAL_SCREEN_SIZE_X (65536)
#define VIRTUAL_SCREEN_SIZE_Y (65536)
#define MOUHID_TOUCHPAD_MOTION_DIVISOR 2
#define MOUHID_TOUCHPAD_SCROLL_STEP 40

NTSTATUS
MouHid_InitiateRead(
    IN PMOUHID_DEVICE_EXTENSION DeviceExtension);

#define MOUHID_TAG 'diHM'
