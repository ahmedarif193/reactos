/*
 * PROJECT:     ReactOS ACPI Time and Alarm Driver
 * LICENSE:     GPL-2.0-or-later
 */

#pragma once

#include <ntddk.h>
#include <wdmguid.h>
#include <acpiioct.h>

#define ACPITIME_TAG 'mTcA'
#define ACPITIME_MAX_OUTPUT (64 * 1024)
#define ACPITIME_METHOD(a, b, c, d) ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))

#define ACPITIME_CAP_AC_WAKE 0x00000001
#define ACPITIME_CAP_DC_WAKE 0x00000002
#define ACPITIME_CAP_REAL_TIME 0x00000004
#define ACPITIME_CAP_MASK 0x000001FF

typedef struct _ACPITIME_GRT_INFO
{
    USHORT Year;
    UCHAR Month;
    UCHAR Day;
    UCHAR Hour;
    UCHAR Minute;
    UCHAR Second;
    UCHAR Valid;
    USHORT Milliseconds;
    SHORT Timezone;
    UCHAR Daylight;
    UCHAR Reserved[3];
} ACPITIME_GRT_INFO, *PACPITIME_GRT_INFO;

typedef struct _ACPITIME_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    ACPI_INTERFACE_STANDARD AcpiInterface;
    BOOLEAN InterfaceAcquired;
    BOOLEAN NotificationsRegistered;
    BOOLEAN Started;
    BOOLEAN Removing;
    volatile LONG WorkCount;
    KEVENT WorkIdleEvent;
    ULONG Capabilities;
} ACPITIME_DEVICE_EXTENSION, *PACPITIME_DEVICE_EXTENSION;
