/*
 * PROJECT:     ReactOS SD/MMC Storage Class Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main header for sffdisk.sys
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ntdddisk.h>
#include <ntddstor.h>
#include <ntddsd.h>
#include <reactos/drivers/sd/sddef.h>
#include <stdio.h>

/** @brief Pool tag: 'SFfd' */
#define TAG_SFFDISK 'dfFS'

/** @brief Default sector size for SD/eMMC (512 bytes). */
#define SD_DEFAULT_SECTOR_SIZE 512

/** @brief Maximum transfer size per request. */
#define SFFDISK_MAX_TRANSFER_SIZE (256 * 1024)

/**
 * @brief Device extension for each FDO created by sffdisk.
 *
 * Contains all per-device state including the SD bus interface,
 * card properties, disk geometry, and device naming information.
 */
typedef struct _SFFDISK_DEVICE_EXTENSION
{
    PDEVICE_OBJECT Self;                /**< Our own device object */
    PDEVICE_OBJECT LowerDevice;         /**< Next lower device in the stack */
    PDEVICE_OBJECT PhysicalDevice;      /**< The physical device object (PDO) */
    IO_REMOVE_LOCK RemoveLock;          /**< Remove lock for safe device removal */
    SDBUS_INTERFACE_STANDARD BusInterface; /**< SD bus interface from the bus driver */
    BOOLEAN InterfaceOpen;              /**< TRUE if the bus interface has been opened */
    SD_CARD_TYPE CardType;              /**< Card type (SD, SDHC, eMMC, etc.) */
    ULONGLONG TotalSectors;             /**< Total number of 512-byte sectors on the card */
    ULONG BytesPerSector;               /**< Bytes per sector (always 512 for SD/eMMC) */
    BOOLEAN WriteProtected;             /**< TRUE if the write-protect switch is engaged */
    BOOLEAN MediaPresent;               /**< TRUE if media is present */
    ULONG MediaChangeCount;             /**< Media change counter */
    BOOLEAN Removable;                  /**< TRUE for removable SD cards, FALSE for eMMC */
    BOOLEAN HighCapacity;               /**< TRUE if the card uses block addressing (SDHC/SDXC/eMMC) */
    BOOLEAN CidValid;
    SD_CID CardId;                      /**< Card identification data */
    DISK_GEOMETRY DiskGeometry;         /**< Disk geometry reported to the storage stack */
    DEVICE_POWER_STATE DevicePowerState; /**< Current device power state */
    ULONG DiskNumber;                   /**< Disk number from IoGetConfigurationInformation()->DiskCount */
    HANDLE HarddiskDirectory;           /**< Handle to \\Device\\HarddiskN directory object */
    UNICODE_STRING DeviceName;          /**< FDO device name (e.g. \\Device\\Harddisk0\\DR0) */
    UNICODE_STRING DiskInterfaceName;
    BOOLEAN DiskInterfaceRegistered;
    BOOLEAN DiskInterfaceEnabled;
    BOOLEAN SurpriseRemoved;
    BOOLEAN Present;

} SFFDISK_DEVICE_EXTENSION, *PSFFDISK_DEVICE_EXTENSION;

/* sffdisk.c */
DRIVER_INITIALIZE DriverEntry;
DRIVER_ADD_DEVICE SffdiskAddDevice;

_Dispatch_type_(IRP_MJ_PNP)
DRIVER_DISPATCH SffdiskPnp;

_Dispatch_type_(IRP_MJ_POWER)
DRIVER_DISPATCH SffdiskPower;

_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
DRIVER_DISPATCH SffdiskCreateClose;

/**
 * @brief Initialize the SD card and populate the device extension.
 *
 * Called during IRP_MN_START_DEVICE to open the bus interface,
 * query card properties, and set up disk geometry.
 *
 * @param[in,out] DeviceExtension  Pointer to the sffdisk device extension.
 *
 * @return STATUS_SUCCESS or an appropriate NTSTATUS error code.
 */
NTSTATUS
SffdiskStartDevice(
    _In_ PSFFDISK_DEVICE_EXTENSION DeviceExtension);

_Dispatch_type_(IRP_MJ_FLUSH_BUFFERS)
DRIVER_DISPATCH SffdiskFlushBuffers;

/* ioctl.c */
_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH SffdiskDeviceControl;

/* readwrite.c */
_Dispatch_type_(IRP_MJ_READ)
_Dispatch_type_(IRP_MJ_WRITE)
DRIVER_DISPATCH SffdiskReadWrite;
