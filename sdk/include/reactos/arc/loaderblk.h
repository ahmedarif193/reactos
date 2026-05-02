/*
 * PROJECT:     ReactOS Kernel/Bootloader shared headers
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        sdk/include/reactos/arc/loaderblk.h
 * PURPOSE:     Data structures shared between bootloader and kernel
 */

#pragma once

#define SMBIOS_TABLE_GUID \
    { 0xeb9d2d31, 0x2d88, 0x11d3, { 0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d } }

#define SMBIOS3_TABLE_GUID \
    { 0xf2fd1544, 0x9794, 0x4a2c, { 0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94 } }

#include <pshpack1.h>

typedef struct _SMBIOS_ENTRY_POINT
{
    CHAR Anchor[4];
    UCHAR Checksum;
    UCHAR Length;
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    USHORT MaxStructureSize;
    UCHAR Revision;
    UCHAR FormattedArea[5];
    CHAR IntermediateAnchor[5];
    UCHAR IntermediateChecksum;
    USHORT TableLength;
    ULONG TableAddress;
    USHORT NumberOfStructures;
    UCHAR BcdRevision;
} SMBIOS_ENTRY_POINT, *PSMBIOS_ENTRY_POINT;

typedef struct _SMBIOS3_ENTRY_POINT
{
    CHAR Anchor[5];
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

typedef struct _SMBIOS_HEADER
{
    UCHAR Type;
    UCHAR Length;
    USHORT Handle;
} SMBIOS_HEADER, *PSMBIOS_HEADER;

typedef struct _SMBIOS_SYSTEM_INFO
{
    SMBIOS_HEADER Header;
    UCHAR Manufacturer;
    UCHAR ProductName;
    UCHAR Version;
    UCHAR SerialNumber;
    UCHAR UUID[16];
    UCHAR WakeUpType;
    UCHAR SKUNumber;
    UCHAR Family;
} SMBIOS_SYSTEM_INFO, *PSMBIOS_SYSTEM_INFO;

#include <poppack.h>

typedef struct _SMBIOS_BIOS_DATA
{
    PHYSICAL_ADDRESS EntryPointAddress;
    PHYSICAL_ADDRESS TableAddress;
    ULONG TableSize;
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    UCHAR DmiRevision;
    UCHAR Reserved;
} SMBIOS_BIOS_DATA, *PSMBIOS_BIOS_DATA;
