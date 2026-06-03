/*
 * PROJECT:     ReactOS SMBus Controller Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Intel ICH/PCH (i801-compatible) SMBus host controller driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * The register interface implemented here is the System Management Bus host
 * controller found in Intel I/O Controller Hub (ICH) and Platform Controller
 * Hub (PCH) parts.  The register layout, command codes and status semantics
 * are public (Intel PCH/ICH datasheets, SMBus 2.0/3.0 specifications) and
 * have been stable across the supported parts.
 */

#pragma once

#include <ntddk.h>

#define SMBUS_TAG 'ubmS'

/* Host controller register offsets (relative to the I/O BAR) */
#define SMBHSTSTS       0x00    /* Host status                       */
#define SMBHSTCNT       0x02    /* Host control                      */
#define SMBHSTCMD       0x03    /* Host command                      */
#define SMBHSTADD       0x04    /* Transmit slave address            */
#define SMBHSTDAT0      0x05    /* Host data 0 / block byte count    */
#define SMBHSTDAT1      0x06    /* Host data 1                       */
#define SMBBLKDAT       0x07    /* Block data byte (auto-increment)  */
#define SMBPEC          0x08    /* Packet error check (ICH3+)        */
#define SMBAUXSTS       0x0C    /* Auxiliary status (ICH4+)          */
#define SMBAUXCTL       0x0D    /* Auxiliary control (ICH4+)         */

/* Host status register bits (SMBHSTSTS) */
#define SMBHSTSTS_BUSY      0x01
#define SMBHSTSTS_INTR      0x02
#define SMBHSTSTS_DEV_ERR   0x04
#define SMBHSTSTS_BUS_ERR   0x08
#define SMBHSTSTS_FAILED    0x10
#define SMBHSTSTS_ALERT     0x20
#define SMBHSTSTS_INUSE     0x40    /* Hardware semaphore shared with BIOS/ACPI */
#define SMBHSTSTS_BYTE_DONE 0x80
#define SMBHSTSTS_ERROR     (SMBHSTSTS_DEV_ERR | SMBHSTSTS_BUS_ERR | \
                             SMBHSTSTS_FAILED)
#define SMBHSTSTS_CLEAR     (SMBHSTSTS_INTR | SMBHSTSTS_DEV_ERR | \
                             SMBHSTSTS_BUS_ERR | SMBHSTSTS_FAILED | \
                             SMBHSTSTS_BYTE_DONE)

/* Host control register bits (SMBHSTCNT) */
#define SMBHSTCNT_INTREN        0x01
#define SMBHSTCNT_KILL          0x02
#define SMBHSTCNT_LAST_BYTE     0x20
#define SMBHSTCNT_START         0x40
#define SMBHSTCNT_PEC_EN        0x80

/* SMBus protocol command codes programmed into SMBHSTCNT[4:2] */
#define SMBHSTCNT_QUICK         0x00
#define SMBHSTCNT_BYTE          0x04
#define SMBHSTCNT_BYTE_DATA     0x08
#define SMBHSTCNT_WORD_DATA     0x0C
#define SMBHSTCNT_PROC_CALL     0x10
#define SMBHSTCNT_BLOCK_DATA    0x14
#define SMBHSTCNT_I2C_BLOCK     0x18

/* Auxiliary status/control register bits (ICH4+) */
#define SMBAUXSTS_CRCE      0x01    /* CRC (PEC) error */
#define SMBAUXCTL_CRC       0x01    /* Enable hardware PEC */
#define SMBAUXCTL_E32B      0x02    /* Enable 32-byte block buffer */

/* PCI configuration-space host configuration register (SMBHSTCFG) */
#define SMBUS_PCI_HSTCFG    0x40
#define SMBHSTCFG_HST_EN    0x01    /* SMBus host controller enable */
#define SMBHSTCFG_SMI_EN    0x02    /* Route completion to SMI# instead of IRQ */
#define SMBHSTCFG_I2C_EN    0x04    /* I2C (vs SMBus) timing */
#define SMBHSTCFG_SPD_WD    0x10    /* SPD write disable */

#define SMBUS_MIN_IO_LENGTH 0x10
#define SMBUS_MAX_POLL_COUNT 10000
#define SMBUS_BLOCK_MAX 32

/* Controller capability flags reported through IOCTL_SMBUS_GET_CONTROLLER_INFO */
#define SMBUS_CAP_BLOCK_BUFFER  0x00000001
#define SMBUS_CAP_PEC           0x00000002

/* Per-transaction flags */
#define SMBUS_TRANSACTION_FLAG_PEC 0x00000001

#define IOCTL_SMBUS_GET_CONTROLLER_INFO \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x801, METHOD_BUFFERED, FILE_READ_DATA)
#define IOCTL_SMBUS_EXECUTE \
    CTL_CODE(FILE_DEVICE_UNKNOWN, 0x802, METHOD_BUFFERED, FILE_READ_DATA | FILE_WRITE_DATA)

typedef enum _SMBUS_TRANSFER_PROTOCOL
{
    SmbusTransferQuick = 0,
    SmbusTransferByte,
    SmbusTransferByteData,
    SmbusTransferWordData,
    SmbusTransferBlockData,
    SmbusTransferProcessCall,
    SmbusTransferBlockProcessCall,
    SmbusTransferMaximum
} SMBUS_TRANSFER_PROTOCOL;

typedef struct _SMBUS_CONTROLLER_INFO
{
    ULONG Version;
    ULONGLONG IoPort;
    ULONG IoLength;
    ULONGLONG MmioPhysical;
    ULONG MmioLength;
    ULONG Capabilities;
    UCHAR HostStatus;
    BOOLEAN Started;
} SMBUS_CONTROLLER_INFO, *PSMBUS_CONTROLLER_INFO;

typedef struct _SMBUS_TRANSACTION
{
    ULONG Version;
    ULONG Flags;
    UCHAR Address;          /* 7-bit slave address (1..0x7F)         */
    UCHAR Command;          /* Command / register byte               */
    UCHAR Protocol;         /* SMBUS_TRANSFER_PROTOCOL               */
    BOOLEAN Read;           /* TRUE for read transactions            */
    USHORT Data;            /* Byte/word payload (in for write, out) */
    UCHAR BlockLength;      /* Block byte count (in for write, out)  */
    UCHAR Reserved[3];
    UCHAR Block[SMBUS_BLOCK_MAX];
} SMBUS_TRANSACTION, *PSMBUS_TRANSACTION;

typedef struct _SMBUS_DEVICE_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT LowerDevice;
    PDEVICE_OBJECT PhysicalDevice;
    IO_REMOVE_LOCK RemoveLock;
    FAST_MUTEX TransactionLock;
    BUS_INTERFACE_STANDARD BusInterface;
    BOOLEAN BusInterfaceAcquired;
    UNICODE_STRING InterfaceName;
    BOOLEAN InterfaceRegistered;
    BOOLEAN InterfaceEnabled;
    PUCHAR IoBase;
    ULONG IoLength;
    PHYSICAL_ADDRESS IoStart;
    PHYSICAL_ADDRESS MmioStart;
    ULONG MmioLength;
    ULONG Capabilities;
    UCHAR OriginalConfig;
    BOOLEAN ConfigSaved;
    BOOLEAN Started;
} SMBUS_DEVICE_EXTENSION, *PSMBUS_DEVICE_EXTENSION;

DRIVER_INITIALIZE DriverEntry;
DRIVER_ADD_DEVICE SmbusAddDevice;
DRIVER_UNLOAD SmbusUnload;

_Dispatch_type_(IRP_MJ_CREATE)
_Dispatch_type_(IRP_MJ_CLOSE)
DRIVER_DISPATCH SmbusCreateClose;

_Dispatch_type_(IRP_MJ_DEVICE_CONTROL)
DRIVER_DISPATCH SmbusDeviceControl;

_Dispatch_type_(IRP_MJ_PNP)
DRIVER_DISPATCH SmbusPnp;

_Dispatch_type_(IRP_MJ_POWER)
DRIVER_DISPATCH SmbusPower;

DRIVER_DISPATCH SmbusPassThrough;
