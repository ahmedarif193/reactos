/*
 * PROJECT:     ReactOS ACPI Processor Module Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ACPI0007 topology and processor power capability discovery
 */

#pragma once

#include <ntddk.h>
#include <wdmguid.h>
#include <acpiioct.h>

#define PNPCPU_TAG 'uCpP'
#define PNPCPU_MAX_ACPI_OUTPUT (64 * 1024)

#define PNPCPU_CAP_CPC 0x00000001
#define PNPCPU_CAP_CST 0x00000002
#define PNPCPU_CAP_PSS 0x00000004
#define PNPCPU_CAP_PCT 0x00000008
#define PNPCPU_CAP_PSD 0x00000010
#define PNPCPU_CAP_PPC 0x00000020
#define PNPCPU_CAP_TSS 0x00000040
#define PNPCPU_CAP_TSD 0x00000080

#define PNPCPU_METHOD(a, b, c, d) ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))

typedef struct _PNPCPU_DEVICE_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT Pdo;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    ACPI_INTERFACE_STANDARD AcpiInterface;
    BOOLEAN InterfaceAcquired;
    BOOLEAN NotificationsRegistered;
    BOOLEAN Started;
    BOOLEAN Removing;
    volatile LONG WorkCount;
    KEVENT WorkIdleEvent;
    BOOLEAN UidValid;
    ULONG Uid;
    BOOLEAN ApicIdValid;
    ULONG ApicId;
    BOOLEAN ProximityValid;
    ULONG ProximityDomain;
    ULONG CapabilityMask;
    ULONG CapabilityCounts[8];
} PNPCPU_DEVICE_EXTENSION, *PPNPCPU_DEVICE_EXTENSION;
