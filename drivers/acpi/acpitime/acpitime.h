/*
 * PROJECT:     ReactOS ACPI Time and Alarm Driver
 * LICENSE:     GPL-2.0-or-later
 */

#pragma once

#include <ntddk.h>
#include <wdmguid.h>
#include <acpiioct.h>
#include <reactos/drivers/acpitime.h>

#define ACPITIME_TAG 'mTcA'
#define ACPITIME_MAX_OUTPUT (64 * 1024)
#define ACPITIME_METHOD(a, b, c, d) ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))

typedef struct _ACPITIME_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    KMUTEX MethodMutex;
    ACPI_INTERFACE_STANDARD AcpiInterface;
    UNICODE_STRING InterfaceName;
    BOOLEAN InterfaceAcquired;
    BOOLEAN NotificationsRegistered;
    BOOLEAN InterfaceRegistered;
    BOOLEAN InterfaceEnabled;
    volatile LONG Started;
    volatile LONG Removing;
    volatile LONG WorkCount;
    KEVENT WorkIdleEvent;
    ULONG Capabilities;
} ACPITIME_DEVICE_EXTENSION, *PACPITIME_DEVICE_EXTENSION;
