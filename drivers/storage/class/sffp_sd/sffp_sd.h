/*
 * PROJECT:     ReactOS SD Function Protocol Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Header for sffp_sd.sys - SD memory function protocol driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntddk.h>
#include <ntdddisk.h>
#include <ntddstor.h>
#include <ntddsd.h>
#include <sffdisk.h>
#include <reactos/drivers/sd/sddef.h>

/** @brief Pool tag: 'SFps' */
#define TAG_SFFP_SD 'spFS'

/** @brief Default sector size for SD cards (512 bytes). */
#define SD_SECTOR_SIZE 512

/** @brief Maximum sectors per single multi-block transfer (128 sectors = 64KB). */
#define SFFP_SD_MAX_SECTORS_PER_TRANSFER 128

/**
 * @brief Device extension for the SD function protocol driver.
 *
 * This driver sits between the class driver (sffdisk.sys) and the bus driver
 * (sdbus.sys). It translates block-level I/O requests into SD card commands
 * using the SDBUS interface.
 */
typedef struct _SFFP_SD_EXTENSION
{
    PDEVICE_OBJECT Self;                /**< Our own device object */
    PDEVICE_OBJECT LowerDevice;         /**< Next lower device in the stack */
    PDEVICE_OBJECT PhysicalDevice;      /**< The physical device object (PDO) from the bus driver */
    IO_REMOVE_LOCK RemoveLock;          /**< Remove lock for safe device removal */
    KSPIN_LOCK UsageLock;               /**< Protects special-file usage state */
    ULONG PagingPathCount;
    ULONG HibernationPathCount;
    ULONG DumpPathCount;
    ULONG BootPathCount;
    ULONG PostDisplayPathCount;
    ULONG GuestAssignedPathCount;
    BOOLEAN PowerPagable;               /**< Base pageability inherited from below */
    SDBUS_INTERFACE_STANDARD BusInterface; /**< SD bus interface from the bus driver */
    BOOLEAN InterfaceOpen;              /**< TRUE if the bus interface has been opened */
    KSPIN_LOCK BusRequestLock;          /**< Serializes interface admission and rundown */
    ULONG OutstandingBusRequests;       /**< Commands currently using BusInterface */
    KEVENT BusRequestsDrained;          /**< Signaled when OutstandingBusRequests is zero */
    BOOLEAN BusRequestsBlocked;         /**< TRUE while PnP is closing the bus interface */
    BOOLEAN HighCapacity;               /**< TRUE if the card uses block (sector) addressing (SDHC/SDXC/eMMC) */
    SDBUS_FUNCTION_TYPE FunctionType;   /**< Function type reported by the bus driver */
    SD_CARD_TYPE CardType;              /**< Card type reported by the bus driver */
    DEVICE_POWER_STATE DevicePowerState; /**< Current device power state */

} SFFP_SD_EXTENSION, *PSFFP_SD_EXTENSION;

/**
 * @brief Build an SD read command descriptor and argument.
 *
 * Uses CMD17 (READ_SINGLE_BLOCK) for single-sector reads and
 * CMD18 (READ_MULTIPLE_BLOCK) for multi-sector reads.
 *
 * @param[out] CmdDesc        Receives the filled-in command descriptor.
 * @param[out] Argument       Receives the command argument (address).
 * @param[in]  SectorAddress  Starting sector number.
 * @param[in]  SectorCount    Number of sectors to read.
 * @param[in]  HighCapacity   TRUE for block addressing, FALSE for byte addressing.
 */
VOID
SffpSdBuildReadCommand(
    _Out_ PSDCMD_DESCRIPTOR CmdDesc,
    _Out_ PULONG Argument,
    _In_ ULONGLONG SectorAddress,
    _In_ ULONG SectorCount,
    _In_ BOOLEAN HighCapacity);

/**
 * @brief Build an SD write command descriptor and argument.
 *
 * Uses CMD24 (WRITE_BLOCK) for single-sector writes and
 * CMD25 (WRITE_MULTIPLE_BLOCK) for multi-sector writes.
 *
 * @param[out] CmdDesc        Receives the filled-in command descriptor.
 * @param[out] Argument       Receives the command argument (address).
 * @param[in]  SectorAddress  Starting sector number.
 * @param[in]  SectorCount    Number of sectors to write.
 * @param[in]  HighCapacity   TRUE for block addressing, FALSE for byte addressing.
 */
VOID
SffpSdBuildWriteCommand(
    _Out_ PSDCMD_DESCRIPTOR CmdDesc,
    _Out_ PULONG Argument,
    _In_ ULONGLONG SectorAddress,
    _In_ ULONG SectorCount,
    _In_ BOOLEAN HighCapacity);

/* Driver dispatch prototypes */
DRIVER_INITIALIZE DriverEntry;
DRIVER_ADD_DEVICE SffpSdAddDevice;

_Dispatch_type_(IRP_MJ_PNP)
DRIVER_DISPATCH SffpSdPnp;

_Dispatch_type_(IRP_MJ_POWER)
DRIVER_DISPATCH SffpSdPower;

_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
DRIVER_DISPATCH SffpSdCreateClose;

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH SffpSdDeviceControl;

_Dispatch_type_(IRP_MJ_INTERNAL_DEVICE_CONTROL)
DRIVER_DISPATCH SffpSdInternalDeviceControl;
