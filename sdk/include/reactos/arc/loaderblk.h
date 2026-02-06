/*
 * PROJECT:     ReactOS Kernel/Bootloader shared headers
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        sdk/include/reactos/arc/loaderblk.h
 * PURPOSE:     Data structures shared between bootloader and kernel
 *              for passing hardware information via loader block
 */

#ifndef _LOADERBLK_H_
#define _LOADERBLK_H_

#pragma once

/*
 * SMBIOS table GUIDs for EFI configuration table lookup.
 */
#define SMBIOS_TABLE_GUID \
    { 0xeb9d2d31, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define SMBIOS3_TABLE_GUID \
    { 0xf2fd1544, 0x9794, 0x4a2c, { 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 } }

#include <pshpack1.h>

/*
 * SMBIOS 2.x Entry Point structure (32-bit addressing).
 * Found by scanning 0xF0000-0xFFFFF on BIOS systems, or via
 * SMBIOS_TABLE_GUID in EFI configuration tables on UEFI systems.
 */
typedef struct _SMBIOS_ENTRY_POINT
{
    CHAR Anchor[4];             /* "_SM_" */
    UCHAR Checksum;
    UCHAR Length;
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    USHORT MaxStructureSize;
    UCHAR Revision;
    UCHAR FormattedArea[5];
    CHAR IntermediateAnchor[5]; /* "_DMI_" */
    UCHAR IntermediateChecksum;
    USHORT TableLength;
    ULONG TableAddress;
    USHORT NumberOfStructures;
    UCHAR BcdRevision;
} SMBIOS_ENTRY_POINT, *PSMBIOS_ENTRY_POINT;

/*
 * SMBIOS 3.0+ Entry Point structure (64-bit addressing).
 * Found via SMBIOS3_TABLE_GUID in EFI configuration tables.
 */
typedef struct _SMBIOS3_ENTRY_POINT
{
    CHAR Anchor[5];             /* "_SM3_" */
    UCHAR Checksum;
    UCHAR Length;
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    UCHAR DocRevision;
    UCHAR Revision;
    UCHAR Reserved;
    ULONG MaxStructureSize;
    ULONGLONG TableAddress;
} SMBIOS3_ENTRY_POINT, *PSMBIOS3_ENTRY_POINT;

/*
 * SMBIOS structure header (common to all structure types).
 */
typedef struct _SMBIOS_HEADER
{
    UCHAR Type;
    UCHAR Length;
    USHORT Handle;
} SMBIOS_HEADER, *PSMBIOS_HEADER;

/*
 * SMBIOS Type 1: System Information.
 * Contains manufacturer, product name, serial number, UUID, etc.
 */
typedef struct _SMBIOS_SYSTEM_INFO
{
    SMBIOS_HEADER Header;       /* Type = 1 */
    UCHAR Manufacturer;         /* String index */
    UCHAR ProductName;          /* String index */
    UCHAR Version;              /* String index */
    UCHAR SerialNumber;         /* String index */
    UCHAR UUID[16];
    UCHAR WakeUpType;
    UCHAR SKUNumber;            /* String index (2.4+) */
    UCHAR Family;               /* String index (2.4+) */
} SMBIOS_SYSTEM_INFO, *PSMBIOS_SYSTEM_INFO;

#include <poppack.h>

/*
 * SMBIOS data passed from bootloader to kernel.
 * Used to provide SMBIOS table location on UEFI systems where
 * the legacy 0xF0000-0xFFFFF scan doesn't work.
 *
 * This structure is stored as Configuration Data for the "SMBIOS"
 * MultiFunctionAdapter component in the ARC configuration tree.
 */
typedef struct _SMBIOS_BIOS_DATA
{
    PHYSICAL_ADDRESS EntryPointAddress; /* SMBIOS entry point (_SM_ or _SM3_) */
    PHYSICAL_ADDRESS TableAddress;      /* SMBIOS structure table address */
    ULONG TableSize;                    /* Size of the SMBIOS structure table */
    UCHAR MajorVersion;                 /* SMBIOS major version */
    UCHAR MinorVersion;                 /* SMBIOS minor version */
    UCHAR DmiRevision;                  /* 2 for SMBIOS 2.x, 3 for SMBIOS 3.0+ */
    UCHAR Reserved;
} SMBIOS_BIOS_DATA, *PSMBIOS_BIOS_DATA;

#endif /* _LOADERBLK_H_ */
