/*
 * PROJECT:     ReactOS HID over I2C minidriver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     HID-I2C protocol and hidclass integration definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#define _HIDPI_
#define _HIDPI_NO_FUNCTION_MACROS_
#define RESHUB_USE_HELPER_ROUTINES

#include <ntddk.h>
#include <hidport.h>
#include <acpiioct.h>
#include <reshub.h>
#include <spb.h>

#define HIDI2C_TAG '2IdH'
#define HIDI2C_MAX_DESCRIPTOR_SIZE 4096
#define HIDI2C_COMMAND_OVERHEAD 16

#include <pshpack1.h>
typedef struct _HIDI2C_DEVICE_DESCRIPTOR
{
    USHORT HidDescriptorLength;
    USHORT BcdVersion;
    USHORT ReportDescriptorLength;
    USHORT ReportDescriptorRegister;
    USHORT InputRegister;
    USHORT MaxInputLength;
    USHORT OutputRegister;
    USHORT MaxOutputLength;
    USHORT CommandRegister;
    USHORT DataRegister;
    USHORT VendorId;
    USHORT ProductId;
    USHORT VersionId;
    ULONG Reserved;
} HIDI2C_DEVICE_DESCRIPTOR, *PHIDI2C_DEVICE_DESCRIPTOR;
#include <poppack.h>

typedef struct _HIDI2C_DEVICE_EXTENSION
{
    HANDLE SpbHandle;
    HANDLE InputThreadHandle;
    PKINTERRUPT InterruptObject;
    LARGE_INTEGER ConnectionId;
    HIDI2C_DEVICE_DESCRIPTOR I2cDescriptor;
    HID_DESCRIPTOR HidDescriptor;
    PUCHAR ReportDescriptor;
    PUCHAR InputBuffer;
    PUCHAR CommandBuffer;
    ULONG TransferBufferSize;
    ULONG InterruptVector;
    KIRQL InterruptIrql;
    KAFFINITY InterruptAffinity;
    KINTERRUPT_MODE InterruptMode;
    BOOLEAN UsesReportIds;
    FAST_MUTEX IoMutex;
    IO_CSQ ReadCsq;
    KSPIN_LOCK ReadQueueLock;
    LIST_ENTRY ReadQueue;
    KEVENT ReadQueueEvent;
    KEVENT ReadsDrainedEvent;
    KEVENT InputReadyEvent;
    volatile LONG OutstandingReads;
    volatile LONG InterruptPending;
    volatile LONG InterruptMasked;
    volatile LONG StopThread;
    volatile LONG Started;
    volatile LONG Powered;
} HIDI2C_DEVICE_EXTENSION, *PHIDI2C_DEVICE_EXTENSION;

NTSTATUS
Hidi2cInitializeTransport(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
Hidi2cStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PCM_RESOURCE_LIST Resources);

VOID
Hidi2cStopDevice(
    _In_ PDEVICE_OBJECT DeviceObject);

NTSTATUS
Hidi2cSetDevicePower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN PowerOn);

NTSTATUS
Hidi2cQueueReadReport(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

NTSTATUS
Hidi2cReportRequest(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ ULONG IoControlCode);
