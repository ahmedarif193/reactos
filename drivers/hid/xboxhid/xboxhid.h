/*
 * PROJECT:     ReactOS HID Stack
 * SPDX-License-Identifier: MIT
 * PURPOSE:     Xbox USB gamepad HID minidriver definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#define _HIDPI_
#define _HIDPI_NO_FUNCTION_MACROS_
#include <ntddk.h>
#include <hidport.h>
#include <hidusage.h>
#include <usbioctl.h>
#include <usb.h>
#include <usbdlib.h>
#include <gip.h>

#define NDEBUG
#include <debug.h>

#define XBOXHID_TAG 'HbxX'

#define XBOXHID_TYPE_XUSB 1
#define XBOXHID_TYPE_GIP  2

#define XBOXHID_IG_REPORT_ID  1
#define XBOXHID_XI_REPORT_ID  2
#define XBOXHID_IG_REPORT_LEN 15
#define XBOXHID_XI_REPORT_LEN 17
#define XBOXHID_FEATURE_LEN   17
#define XBOXHID_OUTPUT_LEN    5

#define XBOXHID_BTN_DPAD_UP    0x0001
#define XBOXHID_BTN_DPAD_DOWN  0x0002
#define XBOXHID_BTN_DPAD_LEFT  0x0004
#define XBOXHID_BTN_DPAD_RIGHT 0x0008
#define XBOXHID_BTN_START      0x0010
#define XBOXHID_BTN_BACK       0x0020
#define XBOXHID_BTN_LTHUMB     0x0040
#define XBOXHID_BTN_RTHUMB     0x0080
#define XBOXHID_BTN_LSHOULDER  0x0100
#define XBOXHID_BTN_RSHOULDER  0x0200
#define XBOXHID_BTN_GUIDE      0x0400
#define XBOXHID_BTN_A          0x1000
#define XBOXHID_BTN_B          0x2000
#define XBOXHID_BTN_X          0x4000
#define XBOXHID_BTN_Y          0x8000

typedef struct _XBOXHID_STATE
{
    SHORT ThumbLX;
    SHORT ThumbLY;
    SHORT ThumbRX;
    SHORT ThumbRY;
    USHORT LeftTrigger;
    USHORT RightTrigger;
    USHORT Buttons;
} XBOXHID_STATE, *PXBOXHID_STATE;

typedef struct _XBOXHID_EXTENSION
{
    PDEVICE_OBJECT NextDeviceObject;
    PUSB_DEVICE_DESCRIPTOR DeviceDescriptor;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    PUSBD_INTERFACE_INFORMATION InterfaceInfo;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;
    USBD_PIPE_HANDLE InPipe;
    USBD_PIPE_HANDLE OutPipe;
    ULONG InMaxPacket;
    ULONG Type;

    KSPIN_LOCK Lock;
    LIST_ENTRY PendingReads;
    XBOXHID_STATE State;
    ULONG StateSequence;
    ULONG IgSequence;
    ULONG XiSequence;
    USHORT RumbleIntensity;
    USHORT BuzzIntensity;
    ULONG RumbleCutoff;
    ULONG BuzzCutoff;

    LONG Running;
    LONG Removed;
    PIRP ReadIrp;
    PURB ReadUrb;
    PIO_WORKITEM ResetWorkItem;
    KEVENT ReadIdle;
    KEVENT WriteSubmitIdle;
    KEVENT WriteIdle;
    LONG WritesSubmitting;
    LONG PendingWrites;
    KSPIN_LOCK GipLock;
    GIP_CONTEXT Gip;
    BOOLEAN GipReady;
    UCHAR ReadBuffer[64];
} XBOXHID_EXTENSION, *PXBOXHID_EXTENSION;
