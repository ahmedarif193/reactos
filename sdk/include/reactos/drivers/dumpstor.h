/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Private crash-dump storage-provider interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/*
 * A dump provider polls its own miniport with interrupts disabled, one
 * microsecond per iteration. This bounds a single request at ~5 seconds.
 */
#define ROS_DUMP_POLL_ITERATIONS 5000000

#define ROS_STORAGE_DUMP_INTERFACE_VERSION 1
#define IOCTL_REACTOS_STORAGE_GET_DUMP_INTERFACE CTL_CODE(FILE_DEVICE_MASS_STORAGE, 0x900, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define ROS_USB_DUMP_INTERFACE_VERSION 2
#define IOCTL_INTERNAL_REACTOS_USB_GET_DUMP_INTERFACE CTL_CODE(FILE_DEVICE_UNKNOWN, 0x900, METHOD_NEITHER, FILE_ANY_ACCESS)

typedef NTSTATUS(NTAPI *PROS_STORAGE_DUMP_PREPARE)(_In_ PVOID Context);

typedef NTSTATUS(NTAPI *PROS_STORAGE_DUMP_WRITE)(_In_ PVOID Context, _In_ ULONG64 DiskByteOffset, _In_ PHYSICAL_ADDRESS Buffer, _In_ ULONG Length);

typedef NTSTATUS(NTAPI *PROS_STORAGE_DUMP_FLUSH)(_In_ PVOID Context);

typedef struct _ROS_STORAGE_DUMP_INTERFACE
{
    ULONG Version;
    ULONG Size;
    PVOID Context;
    /* In/out: caller supplies and provider confirms the logical sector size. */
    ULONG BytesPerSector;
    ULONG MaximumTransferLength;
    PROS_STORAGE_DUMP_PREPARE Prepare;
    PROS_STORAGE_DUMP_WRITE WriteRoutine;
    PROS_STORAGE_DUMP_FLUSH Flush;
} ROS_STORAGE_DUMP_INTERFACE, *PROS_STORAGE_DUMP_INTERFACE;

typedef NTSTATUS(NTAPI *PROS_USB_DUMP_PREPARE)(_In_ PVOID Context);

typedef NTSTATUS(NTAPI *PROS_USB_DUMP_TRANSFER)(_In_ PVOID Context, _In_ BOOLEAN DirectionIn, _In_reads_bytes_(Length) PVOID Buffer, _In_ ULONG Length);

typedef NTSTATUS(NTAPI *PROS_USB_DUMP_TRANSFER_PHYSICAL)(_In_ PVOID Context, _In_ PHYSICAL_ADDRESS Buffer, _In_ ULONG Length);

/*
 * This interface is private to USBSTOR, USBHUB, and USBPORT. USBHUB supplies
 * DeviceHandle while forwarding the request to the root host-controller
 * stack. USBPORT replaces the request fields with the crash-safe callbacks.
 */
typedef struct _ROS_USB_DUMP_INTERFACE
{
    ULONG Version;
    ULONG Size;
    PVOID DeviceHandle;
    PVOID BulkInPipe;
    PVOID BulkOutPipe;
    UCHAR InterfaceNumber;
    UCHAR BulkInEndpointAddress;
    UCHAR BulkOutEndpointAddress;
    UCHAR Reserved;
    PVOID Context;
    PROS_USB_DUMP_PREPARE Prepare;
    PROS_USB_DUMP_TRANSFER Transfer;
    PROS_USB_DUMP_TRANSFER_PHYSICAL TransferPhysical;
} ROS_USB_DUMP_INTERFACE, *PROS_USB_DUMP_INTERFACE;
