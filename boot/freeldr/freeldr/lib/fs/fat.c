/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     FAT filesystem driver for FreeLoader
 * COPYRIGHT:   Copyright 1998-2003 Brian Palmer (brianp@sginet.com)
 *              Copyright 2009 Hervé Poussineau
 *              Copyright 2019 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

#include <freeldr.h>

/*
 * Leave verbose FAT lookup tracing off by default; opt in via the build
 * environment when deep diagnostics are needed.
 */
#ifndef FREELDR_VERBOSE_FAT_LOOKUP
#define FREELDR_VERBOSE_FAT_LOOKUP 0
#endif

#include <debug.h>
DBG_DEFAULT_CHANNEL(FILESYSTEM);

/* Fine-grained debug flag for verbose FAT directory lookup traces */
#if FREELDR_VERBOSE_FAT_LOOKUP
#define FAT_LOOKUP_TRACE(...) FAT_LOOKUP_TRACE(__VA_ARGS__)
#else
#define FAT_LOOKUP_TRACE(...) ((void)0)
#endif

ULONG    FatDetermineFatType(PFAT_BOOTSECTOR FatBootSector, ULONGLONG PartitionSectorCount);
PVOID    FatBufferDirectory(PFAT_VOLUME_INFO Volume, ULONG DirectoryStartCluster, ULONG* DirectorySize, BOOLEAN RootDirectory);
BOOLEAN    FatSearchDirectoryBufferForFile(PFAT_VOLUME_INFO Volume, PVOID DirectoryBuffer, ULONG DirectorySize, PCHAR FileName, BOOLEAN RequireDirectory, PFAT_FILE_INFO FatFileInfoPointer);
ARC_STATUS FatLookupFile(PFAT_VOLUME_INFO Volume, PCSTR FileName, PFAT_FILE_INFO FatFileInfoPointer);
void    FatParseShortFileName(PCHAR Buffer, PDIRENTRY DirEntry);
static BOOLEAN FatGetFatEntry(PFAT_VOLUME_INFO Volume, UINT32 Cluster, PUINT32 ClusterPointer);
static ULONG FatCountClustersInChain(PFAT_VOLUME_INFO Volume, UINT32 StartCluster);
static BOOLEAN FatReadClusterChain(PFAT_VOLUME_INFO Volume, UINT32 StartClusterNumber, UINT32 NumberOfClusters, PVOID Buffer, PUINT32 LastClusterNumber);
BOOLEAN    FatReadPartialCluster(PFAT_VOLUME_INFO Volume, ULONG ClusterNumber, ULONG StartingOffset, ULONG Length, PVOID Buffer);
BOOLEAN    FatReadVolumeSectors(PFAT_VOLUME_INFO Volume, ULONG SectorNumber, ULONG SectorCount, PVOID Buffer);

#define FAT_IS_END_CLUSTER(clnumber)  \
    (((Volume->FatType == FAT12) && (clnumber >= 0xff8)) || \
    ((Volume->FatType == FAT16 || Volume->FatType == FATX16) && (clnumber >= 0xfff8)) || \
    ((Volume->FatType == FAT32 || Volume->FatType == FATX32) && (clnumber >= 0x0ffffff8)))

#define TAG_FAT_CHAIN 'CtaT'
#define TAG_FAT_FILE 'FtaF'
#define TAG_FAT_VOLUME 'VtaF'
#define TAG_FAT_BUFFER 'BtaF'
#define TAG_FAT_CACHE 'HtaF'

#define FAT_MAX_CACHE_SIZE (256 * 1024) // 256 KiB, note: it should fit maximum FAT12 FAT size (6144 bytes)

typedef struct _FAT_VOLUME_INFO
{
    PUCHAR FatCache; /* A part of 1st FAT cached in memory */
    PULONG FatCacheIndex; /* Cached sector's indexes */
    ULONG FatCacheSize; /* Size of the cache in sectors */
    ULONG FatSectorStart; /* Starting sector of 1st FAT table */
    ULONG ActiveFatSectorStart; /* Starting sector of active FAT table */
    ULONG SectorsPerFat; /* Sectors per FAT table */
    ULONG RootDirSectorStart; /* Starting sector of the root directory (non-fat32) */
    ULONG RootDirSectors; /* Number of sectors of the root directory (non-fat32) */
    ULONG RootDirStartCluster; /* Starting cluster number of the root directory (fat32 only) */
    ULONG DataSectorStart; /* Starting sector of the data area */
    ULONG DeviceId;
    UINT16 BytesPerSector; /* Number of bytes per sector */
    UINT8 FatType; /* FAT12, FAT16, FAT32, FATX16 or FATX32 */
    UINT8 NumberOfFats; /* Number of FAT tables */
    UINT8 SectorsPerCluster; /* Number of sectors per cluster */
} FAT_VOLUME_INFO;

PFAT_VOLUME_INFO FatVolumes[MAX_FDS];

VOID FatSwapFatBootSector(PFAT_BOOTSECTOR Obj)
{
    SW(Obj, BytesPerSector);
    SW(Obj, ReservedSectors);
    SW(Obj, RootDirEntries);
    SW(Obj, TotalSectors);
    SW(Obj, SectorsPerFat);
    SW(Obj, SectorsPerTrack);
    SW(Obj, NumberOfHeads);
    SD(Obj, HiddenSectors);
    SD(Obj, TotalSectorsBig);
    SD(Obj, VolumeSerialNumber);
    SW(Obj, BootSectorMagic);
}

VOID FatSwapFat32BootSector(PFAT32_BOOTSECTOR Obj)
{
    SW(Obj, BytesPerSector);
    SW(Obj, ReservedSectors);
    SW(Obj, RootDirEntries);
    SW(Obj, TotalSectors);
    SW(Obj, SectorsPerFat);
    SW(Obj, NumberOfHeads);
    SD(Obj, HiddenSectors);
    SD(Obj, TotalSectorsBig);
    SD(Obj, SectorsPerFatBig);
    SW(Obj, ExtendedFlags);
    SW(Obj, FileSystemVersion);
    SD(Obj, RootDirStartCluster);
    SW(Obj, FsInfo);
    SW(Obj, BackupBootSector);
    SD(Obj, VolumeSerialNumber);
    SW(Obj, BootSectorMagic);
}

VOID FatSwapFatXBootSector(PFATX_BOOTSECTOR Obj)
{
    SD(Obj, VolumeSerialNumber);
    SD(Obj, SectorsPerCluster);
    SW(Obj, NumberOfFats);
}

VOID FatSwapDirEntry(PDIRENTRY Obj)
{
    SW(Obj, CreateTime);
    SW(Obj, CreateDate);
    SW(Obj, LastAccessDate);
    SW(Obj, ClusterHigh);
    SW(Obj, Time);
    SW(Obj, Date);
    SW(Obj, ClusterLow);
    SD(Obj, Size);
}

VOID FatSwapLFNDirEntry(PLFN_DIRENTRY Obj)
{
    int i;
    SW(Obj, StartCluster);
    for(i = 0; i < 5; i++)
        Obj->Name0_4[i] = SWAPW(Obj->Name0_4[i]);
    for(i = 0; i < 6; i++)
        Obj->Name5_10[i] = SWAPW(Obj->Name5_10[i]);
    for(i = 0; i < 2; i++)
        Obj->Name11_12[i] = SWAPW(Obj->Name11_12[i]);
}

VOID FatSwapFatXDirEntry(PFATX_DIRENTRY Obj)
{
    SD(Obj, StartCluster);
    SD(Obj, Size);
    SW(Obj, Time);
    SW(Obj, Date);
    SW(Obj, CreateTime);
    SW(Obj, CreateDate);
    SW(Obj, LastAccessTime);
    SW(Obj, LastAccessDate);
}

BOOLEAN FatOpenVolume(PFAT_VOLUME_INFO Volume, PFAT_BOOTSECTOR BootSector, ULONGLONG PartitionSectorCount)
{
    char ErrMsg[80];
    ULONG FatSize, i;
    PFAT_BOOTSECTOR    FatVolumeBootSector;
    PFAT32_BOOTSECTOR Fat32VolumeBootSector;
    PFATX_BOOTSECTOR FatXVolumeBootSector;

    FAT_LOOKUP_TRACE("FatOpenVolume() DeviceId = %d\n", Volume->DeviceId);

    //
    // Allocate the memory to hold the boot sector
    //
    FatVolumeBootSector = (PFAT_BOOTSECTOR)BootSector;
    Fat32VolumeBootSector = (PFAT32_BOOTSECTOR)BootSector;
    FatXVolumeBootSector = (PFATX_BOOTSECTOR)BootSector;

    // Get the FAT type
    Volume->FatType = FatDetermineFatType(FatVolumeBootSector, PartitionSectorCount);

    // Dump boot sector (and swap it for big endian systems)
    FAT_LOOKUP_TRACE("Dumping boot sector:\n");
    if (ISFATX(Volume->FatType))
    {
        FatSwapFatXBootSector(FatXVolumeBootSector);
        FAT_LOOKUP_TRACE("sizeof(FATX_BOOTSECTOR) = 0x%x.\n", sizeof(FATX_BOOTSECTOR));

        FAT_LOOKUP_TRACE("FileSystemType: %c%c%c%c.\n", FatXVolumeBootSector->FileSystemType[0], FatXVolumeBootSector->FileSystemType[1], FatXVolumeBootSector->FileSystemType[2], FatXVolumeBootSector->FileSystemType[3]);
        FAT_LOOKUP_TRACE("VolumeSerialNumber: 0x%x\n", FatXVolumeBootSector->VolumeSerialNumber);
        FAT_LOOKUP_TRACE("SectorsPerCluster: %d\n", FatXVolumeBootSector->SectorsPerCluster);
        FAT_LOOKUP_TRACE("NumberOfFats: %d\n", FatXVolumeBootSector->NumberOfFats);
        FAT_LOOKUP_TRACE("Unknown: 0x%x\n", FatXVolumeBootSector->Unknown);

        FAT_LOOKUP_TRACE("FatType %s\n", Volume->FatType == FATX16 ? "FATX16" : "FATX32");

    }
    else if (Volume->FatType == FAT32)
    {
        FatSwapFat32BootSector(Fat32VolumeBootSector);
        FAT_LOOKUP_TRACE("sizeof(FAT32_BOOTSECTOR) = 0x%x.\n", sizeof(FAT32_BOOTSECTOR));

        FAT_LOOKUP_TRACE("JumpBoot: 0x%x 0x%x 0x%x\n", Fat32VolumeBootSector->JumpBoot[0], Fat32VolumeBootSector->JumpBoot[1], Fat32VolumeBootSector->JumpBoot[2]);
        FAT_LOOKUP_TRACE("OemName: %c%c%c%c%c%c%c%c\n", Fat32VolumeBootSector->OemName[0], Fat32VolumeBootSector->OemName[1], Fat32VolumeBootSector->OemName[2], Fat32VolumeBootSector->OemName[3], Fat32VolumeBootSector->OemName[4], Fat32VolumeBootSector->OemName[5], Fat32VolumeBootSector->OemName[6], Fat32VolumeBootSector->OemName[7]);
        FAT_LOOKUP_TRACE("BytesPerSector: %d\n", Fat32VolumeBootSector->BytesPerSector);
        FAT_LOOKUP_TRACE("SectorsPerCluster: %d\n", Fat32VolumeBootSector->SectorsPerCluster);
        FAT_LOOKUP_TRACE("ReservedSectors: %d\n", Fat32VolumeBootSector->ReservedSectors);
        FAT_LOOKUP_TRACE("NumberOfFats: %d\n", Fat32VolumeBootSector->NumberOfFats);
        FAT_LOOKUP_TRACE("RootDirEntries: %d\n", Fat32VolumeBootSector->RootDirEntries);
        FAT_LOOKUP_TRACE("TotalSectors: %d\n", Fat32VolumeBootSector->TotalSectors);
        FAT_LOOKUP_TRACE("MediaDescriptor: 0x%x\n", Fat32VolumeBootSector->MediaDescriptor);
        FAT_LOOKUP_TRACE("SectorsPerFat: %d\n", Fat32VolumeBootSector->SectorsPerFat);
        FAT_LOOKUP_TRACE("SectorsPerTrack: %d\n", Fat32VolumeBootSector->SectorsPerTrack);
        FAT_LOOKUP_TRACE("NumberOfHeads: %d\n", Fat32VolumeBootSector->NumberOfHeads);
        FAT_LOOKUP_TRACE("HiddenSectors: %d\n", Fat32VolumeBootSector->HiddenSectors);
        FAT_LOOKUP_TRACE("TotalSectorsBig: %d\n", Fat32VolumeBootSector->TotalSectorsBig);
        FAT_LOOKUP_TRACE("SectorsPerFatBig: %d\n", Fat32VolumeBootSector->SectorsPerFatBig);
        FAT_LOOKUP_TRACE("ExtendedFlags: 0x%x\n", Fat32VolumeBootSector->ExtendedFlags);
        FAT_LOOKUP_TRACE("FileSystemVersion: 0x%x\n", Fat32VolumeBootSector->FileSystemVersion);
        FAT_LOOKUP_TRACE("RootDirStartCluster: %d\n", Fat32VolumeBootSector->RootDirStartCluster);
        FAT_LOOKUP_TRACE("FsInfo: %d\n", Fat32VolumeBootSector->FsInfo);
        FAT_LOOKUP_TRACE("BackupBootSector: %d\n", Fat32VolumeBootSector->BackupBootSector);
        FAT_LOOKUP_TRACE("Reserved: 0x%x\n", Fat32VolumeBootSector->Reserved);
        FAT_LOOKUP_TRACE("DriveNumber: 0x%x\n", Fat32VolumeBootSector->DriveNumber);
        FAT_LOOKUP_TRACE("Reserved1: 0x%x\n", Fat32VolumeBootSector->Reserved1);
        FAT_LOOKUP_TRACE("BootSignature: 0x%x\n", Fat32VolumeBootSector->BootSignature);
        FAT_LOOKUP_TRACE("VolumeSerialNumber: 0x%x\n", Fat32VolumeBootSector->VolumeSerialNumber);
        FAT_LOOKUP_TRACE("VolumeLabel: %c%c%c%c%c%c%c%c%c%c%c\n", Fat32VolumeBootSector->VolumeLabel[0], Fat32VolumeBootSector->VolumeLabel[1], Fat32VolumeBootSector->VolumeLabel[2], Fat32VolumeBootSector->VolumeLabel[3], Fat32VolumeBootSector->VolumeLabel[4], Fat32VolumeBootSector->VolumeLabel[5], Fat32VolumeBootSector->VolumeLabel[6], Fat32VolumeBootSector->VolumeLabel[7], Fat32VolumeBootSector->VolumeLabel[8], Fat32VolumeBootSector->VolumeLabel[9], Fat32VolumeBootSector->VolumeLabel[10]);
        FAT_LOOKUP_TRACE("FileSystemType: %c%c%c%c%c%c%c%c\n", Fat32VolumeBootSector->FileSystemType[0], Fat32VolumeBootSector->FileSystemType[1], Fat32VolumeBootSector->FileSystemType[2], Fat32VolumeBootSector->FileSystemType[3], Fat32VolumeBootSector->FileSystemType[4], Fat32VolumeBootSector->FileSystemType[5], Fat32VolumeBootSector->FileSystemType[6], Fat32VolumeBootSector->FileSystemType[7]);
        FAT_LOOKUP_TRACE("BootSectorMagic: 0x%x\n", Fat32VolumeBootSector->BootSectorMagic);
    }
    else
    {
        FatSwapFatBootSector(FatVolumeBootSector);
        FAT_LOOKUP_TRACE("sizeof(FAT_BOOTSECTOR) = 0x%x.\n", sizeof(FAT_BOOTSECTOR));

        FAT_LOOKUP_TRACE("JumpBoot: 0x%x 0x%x 0x%x\n", FatVolumeBootSector->JumpBoot[0], FatVolumeBootSector->JumpBoot[1], FatVolumeBootSector->JumpBoot[2]);
        FAT_LOOKUP_TRACE("OemName: %c%c%c%c%c%c%c%c\n", FatVolumeBootSector->OemName[0], FatVolumeBootSector->OemName[1], FatVolumeBootSector->OemName[2], FatVolumeBootSector->OemName[3], FatVolumeBootSector->OemName[4], FatVolumeBootSector->OemName[5], FatVolumeBootSector->OemName[6], FatVolumeBootSector->OemName[7]);
        FAT_LOOKUP_TRACE("BytesPerSector: %d\n", FatVolumeBootSector->BytesPerSector);
        FAT_LOOKUP_TRACE("SectorsPerCluster: %d\n", FatVolumeBootSector->SectorsPerCluster);
        FAT_LOOKUP_TRACE("ReservedSectors: %d\n", FatVolumeBootSector->ReservedSectors);
        FAT_LOOKUP_TRACE("NumberOfFats: %d\n", FatVolumeBootSector->NumberOfFats);
        FAT_LOOKUP_TRACE("RootDirEntries: %d\n", FatVolumeBootSector->RootDirEntries);
        FAT_LOOKUP_TRACE("TotalSectors: %d\n", FatVolumeBootSector->TotalSectors);
        FAT_LOOKUP_TRACE("MediaDescriptor: 0x%x\n", FatVolumeBootSector->MediaDescriptor);
        FAT_LOOKUP_TRACE("SectorsPerFat: %d\n", FatVolumeBootSector->SectorsPerFat);
        FAT_LOOKUP_TRACE("SectorsPerTrack: %d\n", FatVolumeBootSector->SectorsPerTrack);
        FAT_LOOKUP_TRACE("NumberOfHeads: %d\n", FatVolumeBootSector->NumberOfHeads);
        FAT_LOOKUP_TRACE("HiddenSectors: %d\n", FatVolumeBootSector->HiddenSectors);
        FAT_LOOKUP_TRACE("TotalSectorsBig: %d\n", FatVolumeBootSector->TotalSectorsBig);
        FAT_LOOKUP_TRACE("DriveNumber: 0x%x\n", FatVolumeBootSector->DriveNumber);
        FAT_LOOKUP_TRACE("Reserved1: 0x%x\n", FatVolumeBootSector->Reserved1);
        FAT_LOOKUP_TRACE("BootSignature: 0x%x\n", FatVolumeBootSector->BootSignature);
        FAT_LOOKUP_TRACE("VolumeSerialNumber: 0x%x\n", FatVolumeBootSector->VolumeSerialNumber);
        FAT_LOOKUP_TRACE("VolumeLabel: %c%c%c%c%c%c%c%c%c%c%c\n", FatVolumeBootSector->VolumeLabel[0], FatVolumeBootSector->VolumeLabel[1], FatVolumeBootSector->VolumeLabel[2], FatVolumeBootSector->VolumeLabel[3], FatVolumeBootSector->VolumeLabel[4], FatVolumeBootSector->VolumeLabel[5], FatVolumeBootSector->VolumeLabel[6], FatVolumeBootSector->VolumeLabel[7], FatVolumeBootSector->VolumeLabel[8], FatVolumeBootSector->VolumeLabel[9], FatVolumeBootSector->VolumeLabel[10]);
        FAT_LOOKUP_TRACE("FileSystemType: %c%c%c%c%c%c%c%c\n", FatVolumeBootSector->FileSystemType[0], FatVolumeBootSector->FileSystemType[1], FatVolumeBootSector->FileSystemType[2], FatVolumeBootSector->FileSystemType[3], FatVolumeBootSector->FileSystemType[4], FatVolumeBootSector->FileSystemType[5], FatVolumeBootSector->FileSystemType[6], FatVolumeBootSector->FileSystemType[7]);
        FAT_LOOKUP_TRACE("BootSectorMagic: 0x%x\n", FatVolumeBootSector->BootSectorMagic);
    }

    //
    // Check the boot sector magic
    //
    if (! ISFATX(Volume->FatType) && FatVolumeBootSector->BootSectorMagic != 0xaa55)
    {
        sprintf(ErrMsg, "Invalid boot sector magic (expected 0xaa55 found 0x%x)",
                FatVolumeBootSector->BootSectorMagic);
        FileSystemError(ErrMsg);
        return FALSE;
    }

    //
    // Check the FAT cluster size
    // We do not support clusters bigger than 64k
    //
    if ((ISFATX(Volume->FatType) && 64 * 1024 < FatXVolumeBootSector->SectorsPerCluster * 512) ||
       (! ISFATX(Volume->FatType) && 64 * 1024 < FatVolumeBootSector->SectorsPerCluster * FatVolumeBootSector->BytesPerSector))
    {
        FileSystemError("This file system has cluster sizes bigger than 64k.\nFreeLoader does not support this.");
        return FALSE;
    }

    //
    // Get the sectors per FAT,
    // root directory starting sector,
    // and data sector start
    //
    if (ISFATX(Volume->FatType))
    {
        Volume->BytesPerSector = 512;
        Volume->SectorsPerCluster = SWAPD(FatXVolumeBootSector->SectorsPerCluster);
        Volume->FatSectorStart = (0x1000 / Volume->BytesPerSector);
        Volume->ActiveFatSectorStart = Volume->FatSectorStart;
        Volume->NumberOfFats = 1;
        FatSize = (ULONG)(PartitionSectorCount / Volume->SectorsPerCluster *
                  (Volume->FatType == FATX16 ? 2 : 4));
        Volume->SectorsPerFat = ROUND_UP(FatSize, 0x1000) / Volume->BytesPerSector;

        Volume->RootDirSectorStart = Volume->FatSectorStart + Volume->NumberOfFats * Volume->SectorsPerFat;
        Volume->RootDirSectors = FatXVolumeBootSector->SectorsPerCluster;

        Volume->DataSectorStart = Volume->RootDirSectorStart + Volume->RootDirSectors;
    }
    else if (Volume->FatType != FAT32)
    {
        Volume->BytesPerSector = FatVolumeBootSector->BytesPerSector;
        Volume->SectorsPerCluster = FatVolumeBootSector->SectorsPerCluster;
        Volume->FatSectorStart = FatVolumeBootSector->ReservedSectors;
        Volume->ActiveFatSectorStart = Volume->FatSectorStart;
        Volume->NumberOfFats = FatVolumeBootSector->NumberOfFats;
        Volume->SectorsPerFat = FatVolumeBootSector->SectorsPerFat;

        Volume->RootDirSectorStart = Volume->FatSectorStart + Volume->NumberOfFats * Volume->SectorsPerFat;
        Volume->RootDirSectors = ((FatVolumeBootSector->RootDirEntries * 32) + (Volume->BytesPerSector - 1)) / Volume->BytesPerSector;

        Volume->DataSectorStart = Volume->RootDirSectorStart + Volume->RootDirSectors;
    }
    else
    {
        Volume->BytesPerSector = Fat32VolumeBootSector->BytesPerSector;
        Volume->SectorsPerCluster = Fat32VolumeBootSector->SectorsPerCluster;
        Volume->FatSectorStart = Fat32VolumeBootSector->ReservedSectors;
        Volume->ActiveFatSectorStart = Volume->FatSectorStart +
                                       ((Fat32VolumeBootSector->ExtendedFlags & 0x80) ? ((Fat32VolumeBootSector->ExtendedFlags & 0x0f) * Fat32VolumeBootSector->SectorsPerFatBig) : 0);
        Volume->NumberOfFats = Fat32VolumeBootSector->NumberOfFats;
        Volume->SectorsPerFat = Fat32VolumeBootSector->SectorsPerFatBig;

        Volume->RootDirStartCluster = Fat32VolumeBootSector->RootDirStartCluster;
        Volume->DataSectorStart = Volume->FatSectorStart + Volume->NumberOfFats * Volume->SectorsPerFat;

        //
        // Check version
        // we only work with version 0
        //
        if (Fat32VolumeBootSector->FileSystemVersion != 0)
        {
            FileSystemError("FreeLoader is too old to work with this FAT32 filesystem.\nPlease update FreeLoader.");
            return FALSE;
        }
    }

    Volume->FatCacheSize = min(Volume->SectorsPerFat, FAT_MAX_CACHE_SIZE / Volume->BytesPerSector);
    FAT_LOOKUP_TRACE("FAT cache is %d sectors, %d bytes\n", Volume->FatCacheSize, Volume->FatCacheSize * Volume->BytesPerSector);

    Volume->FatCache = FrLdrTempAlloc(Volume->FatCacheSize * Volume->BytesPerSector, TAG_FAT_CACHE);
    if (!Volume->FatCache)
    {
        FileSystemError("Cannot allocate memory for FAT cache");
        return FALSE;
    }

    Volume->FatCacheIndex = FrLdrTempAlloc(Volume->FatCacheSize * sizeof(*Volume->FatCacheIndex), TAG_FAT_VOLUME);
    if (!Volume->FatCacheIndex)
    {
        FileSystemError("Cannot allocate memory for FAT cache index");
        FrLdrTempFree(Volume->FatCache, TAG_FAT_CACHE);
        return FALSE;
    }

    // read the beginning of the FAT (or the whole one) to cache
    if (!FatReadVolumeSectors(Volume, Volume->ActiveFatSectorStart, Volume->FatCacheSize, Volume->FatCache))
    {
        FileSystemError("Error when reading FAT cache");
        FrLdrTempFree(Volume->FatCache, TAG_FAT_CACHE);
        FrLdrTempFree(Volume->FatCacheIndex, TAG_FAT_VOLUME);
        return FALSE;
    }

    // fill the index with sector numbers
    for (i = 0; i < Volume->FatCacheSize; i++)
    {
        Volume->FatCacheIndex[i] = Volume->ActiveFatSectorStart + i;
    }

    return TRUE;
}

ULONG FatDetermineFatType(PFAT_BOOTSECTOR FatBootSector, ULONGLONG PartitionSectorCount)
{
    ULONG            RootDirSectors;
    ULONG            DataSectorCount;
    ULONG            SectorsPerFat;
    ULONG            TotalSectors;
    ULONG            CountOfClusters;
    PFAT32_BOOTSECTOR    Fat32BootSector = (PFAT32_BOOTSECTOR)FatBootSector;
    PFATX_BOOTSECTOR    FatXBootSector = (PFATX_BOOTSECTOR)FatBootSector;

    if (0 == strncmp(FatXBootSector->FileSystemType, "FATX", 4))
    {
        CountOfClusters = (ULONG)(PartitionSectorCount / FatXBootSector->SectorsPerCluster);
        if (CountOfClusters < 65525)
        {
            /* Volume is FATX16 */
            return FATX16;
        }
        else
        {
            /* Volume is FAT32 */
            return FATX32;
        }
    }
    else
    {
        RootDirSectors = ((SWAPW(FatBootSector->RootDirEntries) * 32) + (SWAPW(FatBootSector->BytesPerSector) - 1)) / SWAPW(FatBootSector->BytesPerSector);
        SectorsPerFat = SWAPW(FatBootSector->SectorsPerFat) ? SWAPW(FatBootSector->SectorsPerFat) : SWAPD(Fat32BootSector->SectorsPerFatBig);
        TotalSectors = SWAPW(FatBootSector->TotalSectors) ? SWAPW(FatBootSector->TotalSectors) : SWAPD(FatBootSector->TotalSectorsBig);
        DataSectorCount = TotalSectors - (SWAPW(FatBootSector->ReservedSectors) + (FatBootSector->NumberOfFats * SectorsPerFat) + RootDirSectors);

//mjl
        if (FatBootSector->SectorsPerCluster == 0)
            CountOfClusters = 0;
        else
            CountOfClusters = DataSectorCount / FatBootSector->SectorsPerCluster;

        if (CountOfClusters < 4085)
        {
            /* Volume is FAT12 */
            return FAT12;
        }
        else if (CountOfClusters < 65525)
        {
            /* Volume is FAT16 */
            return FAT16;
        }
        else
        {
            /* Volume is FAT32 */
            return FAT32;
        }
    }
}

typedef struct _DIRECTORY_BUFFER
{
    LIST_ENTRY Link;
    PVOID Volume;
    ULONG DirectoryStartCluster;
    ULONG DirectorySize;
    ULONG Magic;
    UCHAR Data[];
} DIRECTORY_BUFFER, *PDIRECTORY_BUFFER;

LIST_ENTRY DirectoryBufferListHead = {&DirectoryBufferListHead, &DirectoryBufferListHead};

#define DIRECTORY_BUFFER_MAGIC   0x46424452u /* 'RDBF' */
#define DIRECTORY_BUFFER_TAIL    0x46C0DEF5u

static __inline PULONG
FatDirectoryBufferTail(PDIRECTORY_BUFFER Buffer)
{
    return (PULONG)(Buffer->Data + Buffer->DirectorySize);
}

static VOID
FatStampDirectoryBufferTail(PDIRECTORY_BUFFER Buffer)
{
    *FatDirectoryBufferTail(Buffer) = DIRECTORY_BUFFER_TAIL;
}

static BOOLEAN
FatValidateDirectoryBuffer(PDIRECTORY_BUFFER Buffer)
{
    if (!Buffer)
        return FALSE;

    if (Buffer->Magic != DIRECTORY_BUFFER_MAGIC)
        return FALSE;

    if (Buffer->DirectorySize == 0)
        return FALSE;

    return (*FatDirectoryBufferTail(Buffer) == DIRECTORY_BUFFER_TAIL);
}

PVOID FatBufferDirectory(PFAT_VOLUME_INFO Volume, ULONG DirectoryStartCluster, ULONG *DirectorySize, BOOLEAN RootDirectory)
{
    PDIRECTORY_BUFFER DirectoryBuffer;
    PLIST_ENTRY Entry;

    FAT_LOOKUP_TRACE("FatBufferDirectory() DirectoryStartCluster = %d RootDirectory = %s\n", DirectoryStartCluster, (RootDirectory ? "TRUE" : "FALSE"));

    /*
     * For FAT32, the root directory is nothing special. We can treat it the same
     * as a subdirectory.
     */
    if (RootDirectory && Volume->FatType == FAT32)
    {
        DirectoryStartCluster = Volume->RootDirStartCluster;
        RootDirectory = FALSE;
    }

    /* Search the list for a match */
    for (Entry = DirectoryBufferListHead.Flink;
         Entry != &DirectoryBufferListHead;)
    {
        DirectoryBuffer = CONTAINING_RECORD(Entry, DIRECTORY_BUFFER, Link);

        /* Check if it matches */
        if ((DirectoryBuffer->Volume == Volume) &&
            (DirectoryBuffer->DirectoryStartCluster == DirectoryStartCluster))
        {
            if (!FatValidateDirectoryBuffer(DirectoryBuffer))
            {
                PLIST_ENTRY Next = Entry->Flink;

                FAT_LOOKUP_TRACE("FatBufferDirectory: cached buffer for cluster %lu invalidated\n",
                      DirectoryStartCluster);

                RemoveEntryList(&DirectoryBuffer->Link);
                FrLdrTempFree(DirectoryBuffer, TAG_FAT_BUFFER);
                Entry = Next;
                continue;
            }

            FAT_LOOKUP_TRACE("Found cached buffer\n");
            *DirectorySize = DirectoryBuffer->DirectorySize;
            return DirectoryBuffer->Data;
        }

        Entry = Entry->Flink;
    }

    //
    // Calculate the size of the directory
    //
    if (RootDirectory)
    {
        *DirectorySize = Volume->RootDirSectors * Volume->BytesPerSector;
    }
    else
    {
        *DirectorySize = FatCountClustersInChain(Volume, DirectoryStartCluster) * Volume->SectorsPerCluster * Volume->BytesPerSector;
    }

    //
    // Attempt to allocate memory for directory buffer
    //
    FAT_LOOKUP_TRACE("Trying to allocate (DirectorySize) %d bytes.\n", *DirectorySize);
    DirectoryBuffer = FrLdrTempAlloc(sizeof(DIRECTORY_BUFFER) + *DirectorySize + sizeof(ULONG),
                                     TAG_FAT_BUFFER);

    if (DirectoryBuffer == NULL)
    {
        return NULL;
    }

    //
    // Now read directory contents into DirectoryBuffer
    //
    if (RootDirectory)
    {
        if (!FatReadVolumeSectors(Volume, Volume->RootDirSectorStart, Volume->RootDirSectors, DirectoryBuffer->Data))
        {
            FrLdrTempFree(DirectoryBuffer, TAG_FAT_BUFFER);
            return NULL;
        }
    }
    else
    {
        if (!FatReadClusterChain(Volume, DirectoryStartCluster, 0xFFFFFFFF, DirectoryBuffer->Data, NULL))
        {
            FrLdrTempFree(DirectoryBuffer, TAG_FAT_BUFFER);
            return NULL;
        }
    }

    /* Enqueue it in the list */
    DirectoryBuffer->Volume = Volume;
    DirectoryBuffer->DirectoryStartCluster = DirectoryStartCluster;
    DirectoryBuffer->DirectorySize = *DirectorySize;
    DirectoryBuffer->Magic = DIRECTORY_BUFFER_MAGIC;
    FatStampDirectoryBufferTail(DirectoryBuffer);
    InsertTailList(&DirectoryBufferListHead, &DirectoryBuffer->Link);

    return DirectoryBuffer->Data;
}

VOID
FatFlushCache(VOID)
{
    PLIST_ENTRY Entry;

    /* Release any cached directory buffers */
    while (!IsListEmpty(&DirectoryBufferListHead))
    {
        Entry = RemoveHeadList(&DirectoryBufferListHead);
        FrLdrTempFree(CONTAINING_RECORD(Entry, DIRECTORY_BUFFER, Link), TAG_FAT_BUFFER);
    }

    /* Tear down mounted volume state so the next access remounts cleanly */
    for (ULONG i = 0; i < sizeof(FatVolumes) / sizeof(FatVolumes[0]); ++i)
    {
        PFAT_VOLUME_INFO Volume = FatVolumes[i];
        if (!Volume)
            continue;

        if (Volume->FatCache)
        {
            FrLdrTempFree(Volume->FatCache, TAG_FAT_CACHE);
            Volume->FatCache = NULL;
        }

        if (Volume->FatCacheIndex)
        {
            FrLdrTempFree(Volume->FatCacheIndex, TAG_FAT_VOLUME);
            Volume->FatCacheIndex = NULL;
        }

        FrLdrTempFree(Volume, TAG_FAT_VOLUME);
        FatVolumes[i] = NULL;
    }
}

BOOLEAN FatSearchDirectoryBufferForFile(PFAT_VOLUME_INFO Volume, PVOID DirectoryBuffer, ULONG DirectorySize, PCHAR FileName, BOOLEAN RequireDirectory, PFAT_FILE_INFO FatFileInfoPointer)
{
    ULONG        EntryCount;
    ULONG        CurrentEntry;
    CHAR        LfnNameBuffer[265];
    CHAR        ShortNameBuffer[20];
    ULONG        StartCluster;
    DIRENTRY        OurDirEntry;
    LFN_DIRENTRY    OurLfnDirEntry;
    PDIRENTRY    DirEntry = &OurDirEntry;
    PLFN_DIRENTRY    LfnDirEntry = &OurLfnDirEntry;

    EntryCount = DirectorySize / sizeof(DIRENTRY);

    FAT_LOOKUP_TRACE("FatSearchDirectoryBufferForFile() DirectoryBuffer = 0x%x EntryCount = %d FileName = %s\n",
                     DirectoryBuffer,
                     EntryCount,
                     FileName);

    RtlZeroMemory(ShortNameBuffer, 13 * sizeof(CHAR));
    RtlZeroMemory(LfnNameBuffer, 261 * sizeof(CHAR));

    for (CurrentEntry=0; CurrentEntry<EntryCount; CurrentEntry++, DirectoryBuffer = ((PDIRENTRY)DirectoryBuffer)+1)
    {
        OurLfnDirEntry = *((PLFN_DIRENTRY) DirectoryBuffer);
        FatSwapLFNDirEntry(LfnDirEntry);
        OurDirEntry = *((PDIRENTRY) DirectoryBuffer);
        FatSwapDirEntry(DirEntry);

        FAT_LOOKUP_TRACE("Entry %lu raw first byte=0x%02X attr=0x%02X reservedNT=0x%02X cluster=(hi:0x%04X lo:0x%04X)\n",
              CurrentEntry,
              (unsigned char)DirEntry->FileName[0],
              DirEntry->Attr,
              DirEntry->ReservedNT,
              DirEntry->ClusterHigh,
              DirEntry->ClusterLow);
        FAT_LOOKUP_TRACE("Entry %lu raw short bytes = %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
              CurrentEntry,
              (unsigned char)DirEntry->FileName[0],
              (unsigned char)DirEntry->FileName[1],
              (unsigned char)DirEntry->FileName[2],
              (unsigned char)DirEntry->FileName[3],
              (unsigned char)DirEntry->FileName[4],
              (unsigned char)DirEntry->FileName[5],
              (unsigned char)DirEntry->FileName[6],
              (unsigned char)DirEntry->FileName[7],
              (unsigned char)DirEntry->FileName[8],
              (unsigned char)DirEntry->FileName[9],
              (unsigned char)DirEntry->FileName[10]);

        //FAT_LOOKUP_TRACE("Dumping directory entry %d:\n", CurrentEntry);
        //DbgDumpBuffer(DPRINT_FILESYSTEM, DirEntry, sizeof(DIRENTRY));

        //
        // Check if this is the last file in the directory
        // If DirEntry[0] == 0x00 then that means all the
        // entries after this one are unused. If this is the
        // last entry then we didn't find the file in this directory.
        //
        if (DirEntry->FileName[0] == '\0')
        {
            FAT_LOOKUP_TRACE("Entry %lu indicates end-of-directory (0x00 marker)\n", CurrentEntry);
            return FALSE;
        }

        //
        // Check if this is a deleted entry or not
        //
        if (DirEntry->FileName[0] == '\xE5')
        {
            FAT_LOOKUP_TRACE("Entry %lu marked deleted (0xE5), skipping\n", CurrentEntry);
            RtlZeroMemory(ShortNameBuffer, 13 * sizeof(CHAR));
            RtlZeroMemory(LfnNameBuffer, 261 * sizeof(CHAR));
            continue;
        }

        //
        // Check if this is a LFN entry
        // If so it needs special handling
        //
        if (DirEntry->Attr == ATTR_LONG_NAME)
        {
            FAT_LOOKUP_TRACE("Entry %lu is LFN fragment (seq raw 0x%02X)\n", CurrentEntry, LfnDirEntry->SequenceNumber);
            //
            // Check to see if this is a deleted LFN entry, if so continue
            //
            if (LfnDirEntry->SequenceNumber & 0x80)
            {
                FAT_LOOKUP_TRACE("Entry %lu LFN fragment has delete bit set, skipping\n", CurrentEntry);
                continue;
            }

            //
            // Mask off high two bits of sequence number
            // and make the sequence number zero-based
            //
            LfnDirEntry->SequenceNumber &= 0x3F;
            LfnDirEntry->SequenceNumber--;
            FAT_LOOKUP_TRACE("Entry %lu normalized LFN sequence %u\n", CurrentEntry, LfnDirEntry->SequenceNumber);

            //
            // Get all 13 LFN entry characters
            //
            if (LfnDirEntry->Name0_4[0] != 0xFFFF)
            {
                LfnNameBuffer[0 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name0_4[0];
            }
            if (LfnDirEntry->Name0_4[1] != 0xFFFF)
            {
                LfnNameBuffer[1 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name0_4[1];
            }
            if (LfnDirEntry->Name0_4[2] != 0xFFFF)
            {
                LfnNameBuffer[2 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name0_4[2];
            }
            if (LfnDirEntry->Name0_4[3] != 0xFFFF)
            {
                LfnNameBuffer[3 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name0_4[3];
            }
            if (LfnDirEntry->Name0_4[4] != 0xFFFF)
            {
                LfnNameBuffer[4 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name0_4[4];
            }
            if (LfnDirEntry->Name5_10[0] != 0xFFFF)
            {
                LfnNameBuffer[5 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name5_10[0];
            }
            if (LfnDirEntry->Name5_10[1] != 0xFFFF)
            {
                LfnNameBuffer[6 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name5_10[1];
            }
            if (LfnDirEntry->Name5_10[2] != 0xFFFF)
            {
                LfnNameBuffer[7 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name5_10[2];
            }
            if (LfnDirEntry->Name5_10[3] != 0xFFFF)
            {
                LfnNameBuffer[8 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name5_10[3];
            }
            if (LfnDirEntry->Name5_10[4] != 0xFFFF)
            {
                LfnNameBuffer[9 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name5_10[4];
            }
            if (LfnDirEntry->Name5_10[5] != 0xFFFF)
            {
                LfnNameBuffer[10 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name5_10[5];
            }
            if (LfnDirEntry->Name11_12[0] != 0xFFFF)
            {
                LfnNameBuffer[11 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name11_12[0];
            }
            if (LfnDirEntry->Name11_12[1] != 0xFFFF)
            {
                LfnNameBuffer[12 + (LfnDirEntry->SequenceNumber * 13)] = (UCHAR)LfnDirEntry->Name11_12[1];
            }

            FAT_LOOKUP_TRACE("Entry %lu updated LFN buffer snapshot length=%zu contents='%s'\n",
                  CurrentEntry,
                  strlen(LfnNameBuffer),
                  LfnNameBuffer);

            //FAT_LOOKUP_TRACE("Dumping long name buffer:\n");
            //DbgDumpBuffer(DPRINT_FILESYSTEM, LfnNameBuffer, 260);

            continue;
        }

        //
        // Check for the volume label attribute
        // and skip over this entry if found
        //
        if (DirEntry->Attr & ATTR_VOLUMENAME)
        {
            FAT_LOOKUP_TRACE("Entry %lu is volume label, clearing buffers and skipping\n", CurrentEntry);
            RtlZeroMemory(ShortNameBuffer, 13 * sizeof(UCHAR));
            RtlZeroMemory(LfnNameBuffer, 261 * sizeof(UCHAR));
            continue;
        }

        //
        // If we get here then we've found a short file name
        // entry and LfnNameBuffer contains the long file
        // name or zeroes. All we have to do now is see if the
        // file name matches either the short or long file name
        // and fill in the FAT_FILE_INFO structure if it does
        // or zero our buffers and continue looking.
        //

        //
        // Get short file name
        //
        FatParseShortFileName(ShortNameBuffer, DirEntry);
        FAT_LOOKUP_TRACE("Entry %lu short name parsed='%s' (len=%zu)\n",
              CurrentEntry,
              ShortNameBuffer,
              strlen(ShortNameBuffer));
        FAT_LOOKUP_TRACE("Entry %lu current LFN buffer len=%zu contents='%s'\n",
              CurrentEntry,
              strlen(LfnNameBuffer),
              LfnNameBuffer);

        //FAT_LOOKUP_TRACE("Entry: %d LFN = %s\n", CurrentEntry, LfnNameBuffer);
        //FAT_LOOKUP_TRACE("Entry: %d DOS name = %s\n", CurrentEntry, ShortNameBuffer);

        if ((ShortNameBuffer[0] == 'H') &&
            (ShortNameBuffer[1] == 'A') &&
            (ShortNameBuffer[2] == 'L'))
        {
            FAT_LOOKUP_TRACE("HAL candidate raw bytes = %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  (unsigned char)DirEntry->FileName[0],
                  (unsigned char)DirEntry->FileName[1],
                  (unsigned char)DirEntry->FileName[2],
                  (unsigned char)DirEntry->FileName[3],
                  (unsigned char)DirEntry->FileName[4],
                  (unsigned char)DirEntry->FileName[5],
                  (unsigned char)DirEntry->FileName[6],
                  (unsigned char)DirEntry->FileName[7],
                  (unsigned char)DirEntry->FileName[8],
                  (unsigned char)DirEntry->FileName[9],
                  (unsigned char)DirEntry->FileName[10]);
        }
        if ((ShortNameBuffer[0] == 'K') &&
            (ShortNameBuffer[1] == 'S'))
        {
            FAT_LOOKUP_TRACE("KS candidate raw bytes = %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  (unsigned char)DirEntry->FileName[0],
                  (unsigned char)DirEntry->FileName[1],
                  (unsigned char)DirEntry->FileName[2],
                  (unsigned char)DirEntry->FileName[3],
                  (unsigned char)DirEntry->FileName[4],
                  (unsigned char)DirEntry->FileName[5],
                  (unsigned char)DirEntry->FileName[6],
                  (unsigned char)DirEntry->FileName[7],
                  (unsigned char)DirEntry->FileName[8],
                  (unsigned char)DirEntry->FileName[9],
                  (unsigned char)DirEntry->FileName[10]);
        }

        //
        // See if the file name matches either the short or long name
        //
        FAT_LOOKUP_TRACE("Comparing '%s' with LFN '%s' (len %zu) and short '%s' (len %zu)\n",
              FileName,
              LfnNameBuffer,
              strlen(LfnNameBuffer),
              ShortNameBuffer,
              strlen(ShortNameBuffer));

        if ((LfnNameBuffer[0] != '\0' && (_stricmp(FileName, LfnNameBuffer) == 0)) ||
            (ShortNameBuffer[0] != '\0' && (_stricmp(FileName, ShortNameBuffer) == 0)))
        {
            //
            // We found the entry, now fill in the FAT_FILE_INFO struct
            //
            FatFileInfoPointer->Attributes = DirEntry->Attr;
            FatFileInfoPointer->FileSize = DirEntry->Size;
            FatFileInfoPointer->FilePointer = 0;
            StartCluster = ((ULONG)DirEntry->ClusterHigh << 16) + DirEntry->ClusterLow;
            FatFileInfoPointer->CurrentCluster = StartCluster;
            FatFileInfoPointer->StartCluster = StartCluster;

            FAT_LOOKUP_TRACE("MSDOS Directory Entry:\n");
            FAT_LOOKUP_TRACE("FileName[11] = %c%c%c%c%c%c%c%c%c%c%c\n", DirEntry->FileName[0], DirEntry->FileName[1], DirEntry->FileName[2], DirEntry->FileName[3], DirEntry->FileName[4], DirEntry->FileName[5], DirEntry->FileName[6], DirEntry->FileName[7], DirEntry->FileName[8], DirEntry->FileName[9], DirEntry->FileName[10]);
            FAT_LOOKUP_TRACE("FileName bytes = %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X\n",
                  (unsigned char)DirEntry->FileName[0],
                  (unsigned char)DirEntry->FileName[1],
                  (unsigned char)DirEntry->FileName[2],
                  (unsigned char)DirEntry->FileName[3],
                  (unsigned char)DirEntry->FileName[4],
                  (unsigned char)DirEntry->FileName[5],
                  (unsigned char)DirEntry->FileName[6],
                  (unsigned char)DirEntry->FileName[7],
                  (unsigned char)DirEntry->FileName[8],
                  (unsigned char)DirEntry->FileName[9],
                  (unsigned char)DirEntry->FileName[10]);
            FAT_LOOKUP_TRACE("Attr = 0x%x\n", DirEntry->Attr);
            FAT_LOOKUP_TRACE("ReservedNT = 0x%x\n", DirEntry->ReservedNT);
            FAT_LOOKUP_TRACE("TimeInTenths = %d\n", DirEntry->TimeInTenths);
            FAT_LOOKUP_TRACE("CreateTime = %d\n", DirEntry->CreateTime);
            FAT_LOOKUP_TRACE("CreateDate = %d\n", DirEntry->CreateDate);
            FAT_LOOKUP_TRACE("LastAccessDate = %d\n", DirEntry->LastAccessDate);
            FAT_LOOKUP_TRACE("ClusterHigh = 0x%x\n", DirEntry->ClusterHigh);
            FAT_LOOKUP_TRACE("Time = %d\n", DirEntry->Time);
            FAT_LOOKUP_TRACE("Date = %d\n", DirEntry->Date);
            FAT_LOOKUP_TRACE("ClusterLow = 0x%x\n", DirEntry->ClusterLow);
            FAT_LOOKUP_TRACE("Size = %d\n", DirEntry->Size);
            FAT_LOOKUP_TRACE("StartCluster = 0x%x\n", StartCluster);

            if (RequireDirectory && !(DirEntry->Attr & ATTR_DIRECTORY))
            {
                FAT_LOOKUP_TRACE("Entry matched name but is not a directory; continuing search\n");
                RtlZeroMemory(ShortNameBuffer, 13 * sizeof(UCHAR));
                RtlZeroMemory(LfnNameBuffer, 261 * sizeof(UCHAR));
                continue;
            }

            return TRUE;
        }

        //
        // Nope, no match - zero buffers and continue looking
        //
        FAT_LOOKUP_TRACE("Entry %lu no match, clearing buffers and continuing\n", CurrentEntry);
        RtlZeroMemory(ShortNameBuffer, 13 * sizeof(UCHAR));
        RtlZeroMemory(LfnNameBuffer, 261 * sizeof(UCHAR));
    }

    FAT_LOOKUP_TRACE("FatSearchDirectoryBufferForFile() finished scanning %lu entries without match for '%s'\n",
          (unsigned long)EntryCount,
          FileName);
    return FALSE;
}

static BOOLEAN FatXSearchDirectoryBufferForFile(PFAT_VOLUME_INFO Volume, PVOID DirectoryBuffer, ULONG DirectorySize, PCHAR FileName, PFAT_FILE_INFO FatFileInfoPointer)
{
    ULONG        EntryCount;
    ULONG        CurrentEntry;
    SIZE_T        FileNameLen;
    FATX_DIRENTRY    OurDirEntry;
    PFATX_DIRENTRY    DirEntry = &OurDirEntry;

    EntryCount = DirectorySize / sizeof(FATX_DIRENTRY);

    FAT_LOOKUP_TRACE("FatXSearchDirectoryBufferForFile() DirectoryBuffer = 0x%x EntryCount = %d FileName = %s\n", DirectoryBuffer, EntryCount, FileName);

    FileNameLen = strlen(FileName);

    for (CurrentEntry = 0; CurrentEntry < EntryCount; CurrentEntry++, DirectoryBuffer = ((PFATX_DIRENTRY)DirectoryBuffer)+1)
    {
        OurDirEntry = *(PFATX_DIRENTRY) DirectoryBuffer;
        FatSwapFatXDirEntry(&OurDirEntry);
        if (0xff == DirEntry->FileNameSize)
        {
            break;
        }
        if (0xe5 == DirEntry->FileNameSize)
        {
            continue;
        }
        if (FileNameLen == DirEntry->FileNameSize &&
            0 == _strnicmp(FileName, DirEntry->FileName, FileNameLen))
        {
            /*
             * We found the entry, now fill in the FAT_FILE_INFO struct
             */
            FatFileInfoPointer->Attributes = DirEntry->Attr;
            FatFileInfoPointer->FileSize = DirEntry->Size;
            FatFileInfoPointer->FilePointer = 0;
            FatFileInfoPointer->CurrentCluster = DirEntry->StartCluster;
            FatFileInfoPointer->StartCluster = DirEntry->StartCluster;

            FAT_LOOKUP_TRACE("FATX Directory Entry:\n");
            FAT_LOOKUP_TRACE("FileNameSize = %d\n", DirEntry->FileNameSize);
            FAT_LOOKUP_TRACE("Attr = 0x%x\n", DirEntry->Attr);
            FAT_LOOKUP_TRACE("StartCluster = 0x%x\n", DirEntry->StartCluster);
            FAT_LOOKUP_TRACE("Size = %d\n", DirEntry->Size);
            FAT_LOOKUP_TRACE("Time = %d\n", DirEntry->Time);
            FAT_LOOKUP_TRACE("Date = %d\n", DirEntry->Date);
            FAT_LOOKUP_TRACE("CreateTime = %d\n", DirEntry->CreateTime);
            FAT_LOOKUP_TRACE("CreateDate = %d\n", DirEntry->CreateDate);
            FAT_LOOKUP_TRACE("LastAccessTime = %d\n", DirEntry->LastAccessTime);
            FAT_LOOKUP_TRACE("LastAccessDate = %d\n", DirEntry->LastAccessDate);

            return TRUE;
        }
    }

    return FALSE;
}

/*
 * FatLookupFile()
 * This function searches the file system for the
 * specified filename and fills in an FAT_FILE_INFO structure
 * with info describing the file, etc. returns ARC error code
 */
ARC_STATUS FatLookupFile(PFAT_VOLUME_INFO Volume, PCSTR FileName, PFAT_FILE_INFO FatFileInfoPointer)
{
    UINT32        i;
    ULONG        NumberOfPathParts;
    CHAR        PathPart[261];
    PVOID        DirectoryBuffer;
    ULONG        DirectoryStartCluster = 0;
    ULONG        DirectorySize;
    FAT_FILE_INFO    FatFileInfo;
    BOOLEAN        RequireDirectory;

    FAT_LOOKUP_TRACE("FatLookupFile() FileName = %s\n", FileName);

    RtlZeroMemory(FatFileInfoPointer, sizeof(FAT_FILE_INFO));

    /* Skip leading path separator, if any */
    while (*FileName == '\\' || *FileName == '/')
        ++FileName;
    //
    // Figure out how many sub-directories we are nested in
    //
    NumberOfPathParts = FsGetNumPathParts(FileName);

    //
    // Loop once for each part
    //
    for (i=0; i<NumberOfPathParts; i++)
    {
        //
        // Get first path part
        //
        FsGetFirstNameFromPath(PathPart, FileName);

        //
        // Advance to the next part of the path
        //
        for (; (*FileName != '\\') && (*FileName != '/') && (*FileName != '\0'); FileName++)
        {
        }

        while (*FileName == '\\' || *FileName == '/')
            FileName++;

        //
        // Buffer the directory contents
        //
        DirectoryBuffer = FatBufferDirectory(Volume, DirectoryStartCluster, &DirectorySize, (i == 0) );
        if (DirectoryBuffer == NULL)
        {
            return ENOMEM;
        }

        //
        // Search for file name in directory
        //
        RequireDirectory = ((i + 1) < NumberOfPathParts);

        if (ISFATX(Volume->FatType))
        {
            if (!FatXSearchDirectoryBufferForFile(Volume, DirectoryBuffer, DirectorySize, PathPart, &FatFileInfo))
            {
                return ENOENT;
            }

            if (RequireDirectory && !(FatFileInfo.Attributes & ATTR_DIRECTORY))
            {
                return ENOTDIR;
            }
        }
        else
        {
            if (!FatSearchDirectoryBufferForFile(Volume, DirectoryBuffer, DirectorySize, PathPart, RequireDirectory, &FatFileInfo))
            {
                return ENOENT;
            }
        }

        //
        // If we have another sub-directory to go then
        // grab the start cluster and free the fat chain array
        //
        if (RequireDirectory)
        {
            //
            // Check if current entry is a directory
            //
            if (!(FatFileInfo.Attributes & ATTR_DIRECTORY))
            {
                return ENOTDIR;
            }
            DirectoryStartCluster = FatFileInfo.StartCluster;
        }
    }

    RtlCopyMemory(FatFileInfoPointer, &FatFileInfo, sizeof(FAT_FILE_INFO));

    return ESUCCESS;
}

/*
 * FatParseFileName()
 * This function parses a directory entry name which
 * is in the form of "FILE   EXT" and puts it in Buffer
 * in the form of "file.ext"
 */
void FatParseShortFileName(PCHAR Buffer, PDIRENTRY DirEntry)
{
    size_t Index = 0;

    RtlZeroMemory(Buffer, 13);

    /* Special-case 0x05 which encodes 0xE5. */
    if (DirEntry->FileName[0] == 0x05)
        DirEntry->FileName[0] = (CHAR)0xE5;

    /* Copy main part (up to 8 characters). */
    for (size_t j = 0; j < 8 && Index < 12; ++j)
    {
        unsigned char Character = (unsigned char)DirEntry->FileName[j];

        if (Character <= ' ')
            break;

        Buffer[Index++] = (CHAR)Character;
    }

    /* Copy extension (up to 3 characters), only if there is any. */
    for (size_t j = 0; j < 3 && Index < 12; ++j)
    {
        unsigned char Character = (unsigned char)DirEntry->FileName[8 + j];

        if (Character <= ' ')
            break;

        if (j == 0)
        {
            Buffer[Index++] = '.';
            if (Index >= 12)
                break;
        }

        Buffer[Index++] = (CHAR)Character;
    }

    Buffer[Index] = '\0';

    //FAT_LOOKUP_TRACE("FatParseShortFileName() ShortName = %s\n", Buffer);
}

/**
 * @brief Reads 1-4 sectors from FAT using the cache
 */
static
PUCHAR FatGetFatSector(PFAT_VOLUME_INFO Volume, UINT32 FatSectorNumber)
{
    UINT32 SectorNumAbsolute = Volume->ActiveFatSectorStart + FatSectorNumber;
    UINT32 CacheIndex = FatSectorNumber % Volume->FatCacheSize;

    ASSERT(FatSectorNumber < Volume->SectorsPerFat);

    // cache miss
    if (Volume->FatCacheIndex[CacheIndex] != SectorNumAbsolute)
    {
        UINT32 SectorsToRead = min(Volume->FatCacheSize - CacheIndex, min(Volume->SectorsPerFat - SectorNumAbsolute, 4));
        UINT8 i;

        if (!FatReadVolumeSectors(Volume, SectorNumAbsolute, SectorsToRead, &Volume->FatCache[CacheIndex * Volume->BytesPerSector]))
        {
            return NULL;
        }

        for (i = 0; i < SectorsToRead; i++)
        {
            Volume->FatCacheIndex[CacheIndex + i] = SectorNumAbsolute + i;
        }

        FAT_LOOKUP_TRACE("FAT cache miss: read sector 0x%x from disk\n", SectorNumAbsolute);
    }
    else
    {
        FAT_LOOKUP_TRACE("FAT cache hit: sector 0x%x present\n", SectorNumAbsolute);
    }

    return &Volume->FatCache[CacheIndex * Volume->BytesPerSector];
}

/*
 * FatGetFatEntry()
 * returns the Fat entry for a given cluster number
 */
static
BOOLEAN FatGetFatEntry(PFAT_VOLUME_INFO Volume, UINT32 Cluster, PUINT32 ClusterPointer)
{
    UINT32 FatOffset, ThisFatSecNum, ThisFatEntOffset, fat;
    PUCHAR ReadBuffer;

    FAT_LOOKUP_TRACE("FatGetFatEntry() Retrieving FAT entry for cluster %d.\n", Cluster);

    switch(Volume->FatType)
    {
    case FAT12:

        FatOffset = Cluster + (Cluster / 2);
        ThisFatSecNum = FatOffset / Volume->BytesPerSector;
        ThisFatEntOffset = (FatOffset % Volume->BytesPerSector);

        FAT_LOOKUP_TRACE("FatOffset: %d\n", FatOffset);
        FAT_LOOKUP_TRACE("ThisFatSecNum: %d\n", ThisFatSecNum);
        FAT_LOOKUP_TRACE("ThisFatEntOffset: %d\n", ThisFatEntOffset);

        // The cluster pointer can span within two sectors, but the FatGetFatSector function
        // reads 4 sectors most times, except when we are at the edge of FAT cache
        // and/or FAT region on the disk. For FAT12 the whole FAT would be cached so
        // there will be no situation when the first sector is at the end of the cache
        // and the next one is in the beginning

        ReadBuffer = FatGetFatSector(Volume, ThisFatSecNum);
        if (!ReadBuffer)
        {
            return FALSE;
        }

        fat = *((USHORT *) (ReadBuffer + ThisFatEntOffset));
        fat = SWAPW(fat);
        if (Cluster & 0x0001)
            fat = fat >> 4;    /* Cluster number is ODD */
        else
            fat = fat & 0x0FFF;    /* Cluster number is EVEN */

        break;

    case FAT16:
    case FATX16:

        FatOffset = (Cluster * 2);
        ThisFatSecNum = FatOffset / Volume->BytesPerSector;
        ThisFatEntOffset = (FatOffset % Volume->BytesPerSector);

        ReadBuffer = FatGetFatSector(Volume, ThisFatSecNum);
        if (!ReadBuffer)
        {
            return FALSE;
        }

        fat = *((USHORT *) (ReadBuffer + ThisFatEntOffset));
        fat = SWAPW(fat);

        break;

    case FAT32:
    case FATX32:

        FatOffset = (Cluster * 4);
        ThisFatSecNum = FatOffset / Volume->BytesPerSector;
        ThisFatEntOffset = (FatOffset % Volume->BytesPerSector);

        ReadBuffer = FatGetFatSector(Volume, ThisFatSecNum);
        if (!ReadBuffer)
        {
            return FALSE;
        }

        // Get the fat entry
        fat = (*((ULONG *) (ReadBuffer + ThisFatEntOffset))) & 0x0FFFFFFF;
        fat = SWAPD(fat);

        break;

    default:
        ERR("Unknown FAT type %d\n", Volume->FatType);
        return FALSE;
    }

    FAT_LOOKUP_TRACE("FAT entry is 0x%x.\n", fat);

    *ClusterPointer = fat;

    return TRUE;
}

static
ULONG FatCountClustersInChain(PFAT_VOLUME_INFO Volume, UINT32 StartCluster)
{
    ULONG    ClusterCount = 0;

    FAT_LOOKUP_TRACE("FatCountClustersInChain() StartCluster = %d\n", StartCluster);

    while (1)
    {
        //
        // If end of chain then break out of our cluster counting loop
        //
        if (FAT_IS_END_CLUSTER(StartCluster))
        {
            break;
        }

        //
        // Increment count
        //
        ClusterCount++;

        //
        // Get next cluster
        //
        if (!FatGetFatEntry(Volume, StartCluster, &StartCluster))
        {
            return 0;
        }
    }

    FAT_LOOKUP_TRACE("FatCountClustersInChain() ClusterCount = %d\n", ClusterCount);

    return ClusterCount;
}

static
BOOLEAN FatReadAdjacentClusters(
    PFAT_VOLUME_INFO Volume,
    UINT32 StartClusterNumber,
    UINT32 MaxClusters,
    PVOID Buffer,
    PUINT32 ClustersRead,
    PUINT32 LastClusterNumber)
{
    UINT32 NextClusterNumber;
    UINT32 ClustersToRead = 1;
    UINT32 PrevClusterNumber = StartClusterNumber;
    UINT32 ClusterStartSector = ((PrevClusterNumber - 2) * Volume->SectorsPerCluster) + Volume->DataSectorStart;

    *ClustersRead = 0;
    *LastClusterNumber = 0;

    if (!FatGetFatEntry(Volume, StartClusterNumber, &NextClusterNumber))
    {
        return FALSE;
    }

    // getting the number of adjacent clusters
    while (!FAT_IS_END_CLUSTER(NextClusterNumber) && ClustersToRead < MaxClusters && (NextClusterNumber == PrevClusterNumber + 1))
    {
        ClustersToRead++;
        PrevClusterNumber = NextClusterNumber;
        if (!FatGetFatEntry(Volume, PrevClusterNumber, &NextClusterNumber))
        {
            return FALSE;
        }
    }

    if (!FatReadVolumeSectors(Volume, ClusterStartSector, ClustersToRead * Volume->SectorsPerCluster, Buffer))
    {
        return FALSE;
    }

    *ClustersRead = ClustersToRead;
    *LastClusterNumber = NextClusterNumber;

    return !FAT_IS_END_CLUSTER(NextClusterNumber) && ClustersToRead < MaxClusters;
}

/*
 * FatReadClusterChain()
 * Reads the specified clusters into memory
 */
static
BOOLEAN FatReadClusterChain(PFAT_VOLUME_INFO Volume, UINT32 StartClusterNumber, UINT32 NumberOfClusters, PVOID Buffer, PUINT32 LastClusterNumber)
{
    UINT32 ClustersRead, NextClusterNumber, ClustersLeft = NumberOfClusters;

    FAT_LOOKUP_TRACE("FatReadClusterChain() StartClusterNumber = %d NumberOfClusters = %d Buffer = 0x%x\n", StartClusterNumber, NumberOfClusters, Buffer);

    ASSERT(NumberOfClusters > 0);

    while (FatReadAdjacentClusters(Volume, StartClusterNumber, ClustersLeft, Buffer, &ClustersRead, &NextClusterNumber))
    {
        ClustersLeft -= ClustersRead;
        Buffer = (PVOID)((ULONG_PTR)Buffer + (ClustersRead * Volume->SectorsPerCluster * Volume->BytesPerSector));
        StartClusterNumber = NextClusterNumber;
    }

    if (LastClusterNumber)
    {
        *LastClusterNumber = NextClusterNumber;
    }

    return (ClustersRead > 0);
}

/*
 * FatReadPartialCluster()
 * Reads part of a cluster into memory
 */
BOOLEAN FatReadPartialCluster(PFAT_VOLUME_INFO Volume, ULONG ClusterNumber, ULONG StartingOffset, ULONG Length, PVOID Buffer)
{
    ULONG        ClusterStartSector;
    ULONG SectorOffset, ReadSize, SectorCount;
    PUCHAR ReadBuffer;
    BOOLEAN Success = FALSE;

    //FAT_LOOKUP_TRACE("FatReadPartialCluster() ClusterNumber = %d StartingOffset = %d Length = %d Buffer = 0x%x\n", ClusterNumber, StartingOffset, Length, Buffer);

    ClusterStartSector = ((ClusterNumber - 2) * Volume->SectorsPerCluster) + Volume->DataSectorStart;

    // This is the offset of the data in sectors
    SectorOffset = (StartingOffset / Volume->BytesPerSector);
    StartingOffset %= Volume->BytesPerSector;

    // Calculate how many sectors we need to read
    SectorCount = (StartingOffset + Length + Volume->BytesPerSector - 1) / Volume->BytesPerSector;

    // Calculate rounded up read size
    ReadSize = SectorCount * Volume->BytesPerSector;

    ReadBuffer = FrLdrTempAlloc(ReadSize, TAG_FAT_BUFFER);
    if (!ReadBuffer)
    {
        return FALSE;
    }

    if (FatReadVolumeSectors(Volume, ClusterStartSector + SectorOffset, SectorCount, ReadBuffer))
    {
        RtlCopyMemory(Buffer, ReadBuffer + StartingOffset, Length);
        Success = TRUE;
    }

    FrLdrTempFree(ReadBuffer, TAG_FAT_BUFFER);

    return Success;
}

/*
 * FatReadFile()
 * Reads BytesToRead from open file and
 * returns the number of bytes read in BytesRead
 */
static
BOOLEAN FatReadFile(PFAT_FILE_INFO FatFileInfo, ULONG BytesToRead, ULONG* BytesRead, PVOID Buffer)
{
    PFAT_VOLUME_INFO Volume = FatFileInfo->Volume;
    UINT32 NextClusterNumber, BytesPerCluster;

    FAT_LOOKUP_TRACE("FatReadFile() BytesToRead = %d Buffer = 0x%x\n", BytesToRead, Buffer);

    if (BytesRead != NULL)
    {
        *BytesRead = 0;
    }

    //
    // If the user is trying to read past the end of
    // the file then return success with BytesRead == 0.
    //
    if (FatFileInfo->FilePointer >= FatFileInfo->FileSize)
    {
        return TRUE;
    }

    //
    // If the user is trying to read more than there is to read
    // then adjust the amount to read.
    //
    if ((FatFileInfo->FilePointer + BytesToRead) > FatFileInfo->FileSize)
    {
        BytesToRead = (FatFileInfo->FileSize - FatFileInfo->FilePointer);
    }

    //
    // Ok, now we have to perform at most 3 calculations
    // I'll draw you a picture (using nifty ASCII art):
    //
    // CurrentFilePointer -+
    //                     |
    //    +----------------+
    //    |
    // +-----------+-----------+-----------+-----------+
    // | Cluster 1 | Cluster 2 | Cluster 3 | Cluster 4 |
    // +-----------+-----------+-----------+-----------+
    //    |                                    |
    //    +---------------+--------------------+
    //                    |
    // BytesToRead -------+
    //
    // 1 - The first calculation (and read) will align
    //     the file pointer with the next cluster.
    //     boundary (if we are supposed to read that much)
    // 2 - The next calculation (and read) will read
    //     in all the full clusters that the requested
    //     amount of data would cover (in this case
    //     clusters 2 & 3).
    // 3 - The last calculation (and read) would read
    //     in the remainder of the data requested out of
    //     the last cluster.
    //

    BytesPerCluster = Volume->SectorsPerCluster * Volume->BytesPerSector;

    //
    // Only do the first read if we
    // aren't aligned on a cluster boundary
    //
    if (FatFileInfo->FilePointer % BytesPerCluster)
    {
        //
        // Do the math for our first read
        //
        UINT32 OffsetInCluster = FatFileInfo->FilePointer % BytesPerCluster;
        UINT32 LengthInCluster = min(BytesToRead, BytesPerCluster - OffsetInCluster);

        ASSERT(LengthInCluster <= BytesPerCluster && LengthInCluster > 0);

        //
        // Now do the read and update BytesRead, BytesToRead, FilePointer, & Buffer
        //
        if (!FatReadPartialCluster(Volume, FatFileInfo->CurrentCluster, OffsetInCluster, LengthInCluster, Buffer))
        {
            return FALSE;
        }
        if (BytesRead != NULL)
        {
            *BytesRead += LengthInCluster;
        }
        BytesToRead -= LengthInCluster;
        FatFileInfo->FilePointer += LengthInCluster;
        Buffer = (PVOID)((ULONG_PTR)Buffer + LengthInCluster);

        // get the next cluster if needed
        if ((LengthInCluster + OffsetInCluster) == BytesPerCluster)
        {
            if (!FatGetFatEntry(Volume, FatFileInfo->CurrentCluster, &NextClusterNumber))
            {
                return FALSE;
            }

            FatFileInfo->CurrentCluster = NextClusterNumber;
            FAT_LOOKUP_TRACE("FatReadFile() FatFileInfo->CurrentCluster = 0x%x\n", FatFileInfo->CurrentCluster);
        }
    }

    //
    // Do the math for our second read (if any data left)
    //
    if (BytesToRead > 0)
    {
        //
        // Determine how many full clusters we need to read
        //
        UINT32 NumberOfClusters = BytesToRead / BytesPerCluster;

        FAT_LOOKUP_TRACE("Going to read: %u clusters\n", NumberOfClusters);

        if (NumberOfClusters > 0)
        {
            UINT32 BytesReadHere = NumberOfClusters * BytesPerCluster;

            ASSERT(!FAT_IS_END_CLUSTER(FatFileInfo->CurrentCluster));

            if (!FatReadClusterChain(Volume, FatFileInfo->CurrentCluster, NumberOfClusters, Buffer, &NextClusterNumber))
            {
                return FALSE;
            }

            if (BytesRead != NULL)
            {
                *BytesRead += BytesReadHere;
            }
            BytesToRead -= BytesReadHere;
            Buffer = (PVOID)((ULONG_PTR)Buffer + BytesReadHere);

            ASSERT(!FAT_IS_END_CLUSTER(NextClusterNumber) || BytesToRead == 0);

            FatFileInfo->FilePointer += BytesReadHere;
            FatFileInfo->CurrentCluster = NextClusterNumber;
            FAT_LOOKUP_TRACE("FatReadFile() FatFileInfo->CurrentCluster = 0x%x\n", FatFileInfo->CurrentCluster);
        }
    }

    //
    // Do the math for our third read (if any data left)
    //
    if (BytesToRead > 0)
    {
        ASSERT(!FAT_IS_END_CLUSTER(FatFileInfo->CurrentCluster));

        //
        // Now do the read and update BytesRead & FilePointer
        //
        if (!FatReadPartialCluster(Volume, FatFileInfo->CurrentCluster, 0, BytesToRead, Buffer))
        {
            return FALSE;
        }
        if (BytesRead != NULL)
        {
            *BytesRead += BytesToRead;
        }
        FatFileInfo->FilePointer += BytesToRead;
    }

    return TRUE;
}

BOOLEAN FatReadVolumeSectors(PFAT_VOLUME_INFO Volume, ULONG SectorNumber, ULONG SectorCount, PVOID Buffer)
{
    LARGE_INTEGER Position;
    ULONG Count;
    ARC_STATUS Status;

    //FAT_LOOKUP_TRACE("FatReadVolumeSectors(): SectorNumber %d, SectorCount %d, Buffer %p\n",
    //    SectorNumber, SectorCount, Buffer);

    //
    // Seek to right position
    //
    Position.QuadPart = (ULONGLONG)SectorNumber * Volume->BytesPerSector;
    Status = ArcSeek(Volume->DeviceId, &Position, SeekAbsolute);
    if (Status != ESUCCESS)
    {
        FAT_LOOKUP_TRACE("FatReadVolumeSectors() Failed to seek\n");
        return FALSE;
    }

    //
    // Read data
    //
    Status = ArcRead(Volume->DeviceId, Buffer, SectorCount * Volume->BytesPerSector, &Count);
    if (Status != ESUCCESS || Count != SectorCount * Volume->BytesPerSector)
    {
        FAT_LOOKUP_TRACE("FatReadVolumeSectors() Failed to read\n");
        return FALSE;
    }

    // Return success
    return TRUE;
}

ARC_STATUS FatClose(ULONG FileId)
{
    PFAT_FILE_INFO FileHandle = FsGetDeviceSpecific(FileId);

    FrLdrTempFree(FileHandle, TAG_FAT_FILE);

    return ESUCCESS;
}

ARC_STATUS FatGetFileInformation(ULONG FileId, FILEINFORMATION* Information)
{
    PFAT_FILE_INFO FileHandle = FsGetDeviceSpecific(FileId);

    RtlZeroMemory(Information, sizeof(*Information));
    Information->EndingAddress.LowPart = FileHandle->FileSize;
    Information->CurrentAddress.LowPart = FileHandle->FilePointer;

    FAT_LOOKUP_TRACE("FatGetFileInformation(%lu) -> FileSize = %lu, FilePointer = 0x%lx\n",
          FileId, Information->EndingAddress.LowPart, Information->CurrentAddress.LowPart);

    return ESUCCESS;
}

ARC_STATUS FatOpen(CHAR* Path, OPENMODE OpenMode, ULONG* FileId)
{
    PFAT_VOLUME_INFO FatVolume;
    FAT_FILE_INFO TempFileInfo;
    PFAT_FILE_INFO FileHandle;
    ULONG DeviceId;
    BOOLEAN IsDirectory;
    ARC_STATUS Status;

    if (OpenMode != OpenReadOnly && OpenMode != OpenDirectory)
        return EACCES;

    DeviceId = FsGetDeviceId(*FileId);
    FatVolume = FatVolumes[DeviceId];

    FAT_LOOKUP_TRACE("FatOpen() FileName = %s\n", Path);

    RtlZeroMemory(&TempFileInfo, sizeof(TempFileInfo));
    Status = FatLookupFile(FatVolume, Path, &TempFileInfo);
    if (Status != ESUCCESS)
        return ENOENT;

    //
    // Check if caller opened what he expected (dir vs file)
    //
    IsDirectory = (TempFileInfo.Attributes & ATTR_DIRECTORY) != 0;
    if (IsDirectory && OpenMode != OpenDirectory)
        return EISDIR;
    else if (!IsDirectory && OpenMode != OpenReadOnly)
        return ENOTDIR;

    FileHandle = FrLdrTempAlloc(sizeof(FAT_FILE_INFO), TAG_FAT_FILE);
    if (!FileHandle)
        return ENOMEM;

    RtlCopyMemory(FileHandle, &TempFileInfo, sizeof(FAT_FILE_INFO));
    FileHandle->Volume = FatVolume;

    FsSetDeviceSpecific(*FileId, FileHandle);
    return ESUCCESS;
}

ARC_STATUS FatRead(ULONG FileId, VOID* Buffer, ULONG N, ULONG* Count)
{
    PFAT_FILE_INFO FileHandle = FsGetDeviceSpecific(FileId);
    BOOLEAN Success;

    //
    // Call old read method
    //
    Success = FatReadFile(FileHandle, N, Count, Buffer);

    //
    // Check for success
    //
    if (Success)
        return ESUCCESS;
    else
        return EIO;
}

ARC_STATUS FatSeek(ULONG FileId, LARGE_INTEGER* Position, SEEKMODE SeekMode)
{
    PFAT_FILE_INFO FileHandle = FsGetDeviceSpecific(FileId);
    PFAT_VOLUME_INFO Volume = FileHandle->Volume;
    LARGE_INTEGER NewPosition = *Position;

    switch (SeekMode)
    {
        case SeekAbsolute:
            break;
        case SeekRelative:
            NewPosition.QuadPart += (ULONGLONG)FileHandle->FilePointer;
            break;
        default:
            ASSERT(FALSE);
            return EINVAL;
    }

    if (NewPosition.HighPart != 0)
        return EINVAL;
    if (NewPosition.LowPart >= FileHandle->FileSize)
        return EINVAL;

    FAT_LOOKUP_TRACE("FatSeek() NewPosition = %u, OldPointer = %u, SeekMode = %d\n", NewPosition.LowPart, FileHandle->FilePointer, SeekMode);

    {
        UINT32 OldClusterIdx = FileHandle->FilePointer / (Volume->SectorsPerCluster * Volume->BytesPerSector);
        UINT32 NewClusterIdx = NewPosition.LowPart / (Volume->SectorsPerCluster * Volume->BytesPerSector);

        FAT_LOOKUP_TRACE("FatSeek() OldClusterIdx: %u, NewClusterIdx: %u\n", OldClusterIdx, NewClusterIdx);

        if (NewClusterIdx != OldClusterIdx)
        {
            UINT32 CurrentClusterIdx, ClusterNumber;

            if (NewClusterIdx > OldClusterIdx)
            {
                CurrentClusterIdx = OldClusterIdx;
                ClusterNumber = FileHandle->CurrentCluster;
            }
            else
            {
                CurrentClusterIdx = 0;
                ClusterNumber = FileHandle->StartCluster;
            }

            for (; CurrentClusterIdx < NewClusterIdx; CurrentClusterIdx++)
            {
                if (!FatGetFatEntry(Volume, ClusterNumber, &ClusterNumber))
                {
                    return EIO;
                }
            }
            FileHandle->CurrentCluster = ClusterNumber;
        }
    }

    FileHandle->FilePointer = NewPosition.LowPart;

    return ESUCCESS;
}

const DEVVTBL FatFuncTable =
{
    FatClose,
    FatGetFileInformation,
    FatOpen,
    FatRead,
    FatSeek,
    L"fastfat",
};

const DEVVTBL FatXFuncTable =
{
    FatClose,
    FatGetFileInformation,
    FatOpen,
    FatRead,
    FatSeek,
    L"vfatfs",
};

const DEVVTBL* FatMount(ULONG DeviceId)
{
    PFAT_VOLUME_INFO Volume;
    UCHAR Buffer[512];
    PFAT_BOOTSECTOR BootSector = (PFAT_BOOTSECTOR)Buffer;
    PFAT32_BOOTSECTOR BootSector32 = (PFAT32_BOOTSECTOR)Buffer;
    PFATX_BOOTSECTOR BootSectorX = (PFATX_BOOTSECTOR)Buffer;
    FILEINFORMATION FileInformation;
    LARGE_INTEGER Position;
    ULONG Count;
    ULARGE_INTEGER SectorCount;
    ARC_STATUS Status;

    FAT_LOOKUP_TRACE("Enter FatMount(%lu)\n", DeviceId);

    //
    // Allocate data for volume information
    //
    Volume = FrLdrTempAlloc(sizeof(FAT_VOLUME_INFO), TAG_FAT_VOLUME);
    if (!Volume)
        return NULL;
    RtlZeroMemory(Volume, sizeof(FAT_VOLUME_INFO));

    //
    // Read the BootSector
    //
    Position.QuadPart = 0;
    Status = ArcSeek(DeviceId, &Position, SeekAbsolute);
    if (Status != ESUCCESS)
    {
        FrLdrTempFree(Volume, TAG_FAT_VOLUME);
        return NULL;
    }
    Status = ArcRead(DeviceId, Buffer, sizeof(Buffer), &Count);
    if (Status != ESUCCESS || Count != sizeof(Buffer))
    {
        FrLdrTempFree(Volume, TAG_FAT_VOLUME);
        return NULL;
    }

    //
    // Check if BootSector is valid. If no, return early
    //
    if (!RtlEqualMemory(BootSector->FileSystemType, "FAT12   ", 8) &&
        !RtlEqualMemory(BootSector->FileSystemType, "FAT16   ", 8) &&
        !RtlEqualMemory(BootSector32->FileSystemType, "FAT32   ", 8) &&
        !RtlEqualMemory(BootSectorX->FileSystemType, "FATX", 4))
    {
        FrLdrTempFree(Volume, TAG_FAT_VOLUME);
        return NULL;
    }

    //
    // Determine sector count
    //
    Status = ArcGetFileInformation(DeviceId, &FileInformation);
    if (Status != ESUCCESS)
    {
        FrLdrTempFree(Volume, TAG_FAT_VOLUME);
        return NULL;
    }
    SectorCount.QuadPart = (FileInformation.EndingAddress.QuadPart - FileInformation.StartingAddress.QuadPart);
    SectorCount.QuadPart /= SECTOR_SIZE;

    //
    // Keep device id
    //
    Volume->DeviceId = DeviceId;

    //
    // Really open the volume
    //
    if (!FatOpenVolume(Volume, BootSector, SectorCount.QuadPart))
    {
        FrLdrTempFree(Volume, TAG_FAT_VOLUME);
        return NULL;
    }

    //
    // Remember FAT volume information
    //
    FatVolumes[DeviceId] = Volume;

    //
    // Return success
    //
    FAT_LOOKUP_TRACE("FatMount(%lu) success\n", DeviceId);
    return (ISFATX(Volume->FatType) ? &FatXFuncTable : &FatFuncTable);
}
