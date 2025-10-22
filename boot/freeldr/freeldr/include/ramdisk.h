/*
 * PROJECT:     FreeLoader
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Header file for ramdisk support.
 * COPYRIGHT:   Copyright 2008 ReactOS Portable Systems Group
 *              Copyright 2009 Hervé Poussineau
 *              Copyright 2019 Hermes Belusca-Maito
 */

#pragma once

typedef struct _RAMDISK_FAT32_LAYOUT
{
    ULONG BytesPerSector;
    ULONG SectorsPerCluster;
    ULONG ReservedSectors;
    ULONG NumberOfFats;
    ULONG FatSizeSectors;
    ULONG FirstDataSector;
    ULONG RootDirFirstCluster;
    ULONG TotalSectors;
    ULONG HiddenSectors;
    ULONGLONG VolumeSizeBytes;
} RAMDISK_FAT32_LAYOUT, *PRAMDISK_FAT32_LAYOUT;

ARC_STATUS
RamDiskInitialize(
    IN BOOLEAN InitRamDisk,
    IN PCSTR LoadOptions OPTIONAL,
    IN PCSTR DefaultPath OPTIONAL);

ULONGLONG
RamDiskGetRequestedSize(VOID);

BOOLEAN
RamDiskGetReservedBuffer(IN ULONGLONG MinimumSize,
                         OUT PVOID *BaseAddress,
                         OUT PULONGLONG ActualSize);

ULONGLONG
RamDiskGetImageLength(VOID);

ULONG
RamDiskGetImageOffset(VOID);

BOOLEAN
RamDiskFormatFat32(IN PVOID BaseAddress,
                   IN ULONGLONG DiskSize,
                   OUT PRAMDISK_FAT32_LAYOUT Layout OPTIONAL);

extern PVOID gInitRamDiskBase;
extern ULONG gInitRamDiskSize;
