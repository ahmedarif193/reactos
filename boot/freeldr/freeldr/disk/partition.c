/*
 *  FreeLoader
 *  Copyright (C) 1998-2003  Brian Palmer  <brianp@sginet.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef _M_ARM
#include <freeldr.h>
#include <fs/fat.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);

#define MaxDriveNumber 0xFF
static PARTITION_STYLE DiskPartitionType[MaxDriveNumber + 1];

static BOOLEAN
DiskDetectRawLayout(
    IN UCHAR DriveNumber,
    OUT ULONGLONG *TotalSectors,
    OUT UCHAR *SystemIndicator)
{
    GEOMETRY Geometry;
    ULONGLONG SectorCount = 0;
    UCHAR Indicator = PARTITION_IFS;

    if (SystemIndicator)
        *SystemIndicator = Indicator;

    if (TotalSectors)
        *TotalSectors = 0;

    if (MachDiskGetDriveGeometry(DriveNumber, &Geometry))
    {
        if (Geometry.Sectors != 0)
        {
            SectorCount = Geometry.Sectors;
        }
        else if (Geometry.Cylinders != 0 &&
                 Geometry.Heads != 0 &&
                 Geometry.SectorsPerTrack != 0)
        {
            SectorCount = (ULONGLONG)Geometry.Cylinders * Geometry.Heads * Geometry.SectorsPerTrack;
        }
    }

    if (MachDiskReadLogicalSectors(DriveNumber, 0, 1, DiskReadBuffer))
    {
        const FAT_BOOTSECTOR *FatBoot = (const FAT_BOOTSECTOR *)DiskReadBuffer;
        const FAT32_BOOTSECTOR *Fat32Boot = (const FAT32_BOOTSECTOR *)DiskReadBuffer;
        const PUCHAR Raw = (const PUCHAR)DiskReadBuffer;
        USHORT Signature = *(const USHORT *)(Raw + 510);

        if (Signature == 0xAA55)
        {
            if (RtlEqualMemory(Fat32Boot->FileSystemType, "FAT32   ", 8))
            {
                Indicator = PARTITION_FAT32;
            }
            else if (RtlEqualMemory(FatBoot->FileSystemType, "FAT16   ", 8))
            {
                Indicator = PARTITION_FAT_16;
            }
            else if (RtlEqualMemory(FatBoot->FileSystemType, "FAT12   ", 8))
            {
                Indicator = PARTITION_FAT_12;
            }
            else if (RtlEqualMemory(FatBoot->OemName, "NTFS    ", 8) ||
                     RtlEqualMemory(FatBoot->OemName, "EXFAT   ", 8))
            {
                Indicator = PARTITION_IFS;
            }

            if (SectorCount == 0)
            {
                if (FatBoot->TotalSectors != 0)
                {
                    SectorCount = FatBoot->TotalSectors;
                }
                else if (FatBoot->TotalSectorsBig != 0)
                {
                    SectorCount = FatBoot->TotalSectorsBig;
                }
                else if (Fat32Boot->TotalSectorsBig != 0)
                {
                    SectorCount = Fat32Boot->TotalSectorsBig;
                }
            }
        }
        else if (RtlEqualMemory(Raw + 3, "NTFS    ", 8))
        {
            Indicator = PARTITION_IFS;
        }
    }

    if (SectorCount == 0)
    {
        return FALSE;
    }

    if (SystemIndicator)
        *SystemIndicator = Indicator;

    if (TotalSectors)
        *TotalSectors = SectorCount;

    return TRUE;
}

static BOOLEAN
DiskBuildRawPartitionEntry(
    IN UCHAR DriveNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    ULONGLONG SectorCount;
    UCHAR Indicator;

    if (!DiskDetectRawLayout(DriveNumber, &SectorCount, &Indicator))
    {
        TRACE("DiskBuildRawPartitionEntry: failed to detect layout for drive 0x%x\n", DriveNumber);
        return FALSE;
    }

    if (SectorCount > MAXULONG)
        SectorCount = MAXULONG;

    RtlZeroMemory(PartitionTableEntry, sizeof(*PartitionTableEntry));
    PartitionTableEntry->SystemIndicator = Indicator;
    PartitionTableEntry->SectorCountBeforePartition = 0;
    PartitionTableEntry->PartitionSectorCount = (ULONG)SectorCount;
    return TRUE;
}

static
BOOLEAN
DiskIsValidMbrPartitionEntry(
    IN const PARTITION_TABLE_ENTRY *Entry)
{
    UCHAR Boot = Entry->BootIndicator;

    if (Boot != 0x00 && Boot != 0x80)
        return FALSE;

    if (Entry->SystemIndicator == PARTITION_ENTRY_UNUSED)
        return FALSE;

    if (Entry->PartitionSectorCount == 0)
        return FALSE;

    /* Accept both CHS and LBA-only partitions.
     * Modern partitioning tools often leave CHS fields as 0 for LBA-only partitions.
     * Rejecting CHS=0 causes misdetection of valid MBR disks as RAW/CDROM. */
    return TRUE;
}

#include <pshpack1.h>
typedef struct _EFI_PARTITION_HEADER
{
    CHAR     Signature[8];
    ULONG    Revision;
    ULONG    HeaderSize;
    ULONG    HeaderCRC32;
    ULONG    Reserved;
    ULONGLONG MyLBA;
    ULONGLONG AlternateLBA;
    ULONGLONG FirstUsableLBA;
    ULONGLONG LastUsableLBA;
    GUID     DiskGuid;
    ULONGLONG PartitionEntryLBA;
    ULONG    NumberOfPartitionEntries;
    ULONG    SizeOfPartitionEntry;
    ULONG    PartitionEntryArrayCRC32;
    ULONG    Reserved2;
} EFI_PARTITION_HEADER, *PEFI_PARTITION_HEADER;

typedef struct _EFI_PARTITION_ENTRY
{
    GUID     PartitionTypeGuid;
    GUID     UniquePartitionGuid;
    ULONGLONG StartingLBA;
    ULONGLONG EndingLBA;
    ULONGLONG Attributes;
    WCHAR    Name[36];
} EFI_PARTITION_ENTRY, *PEFI_PARTITION_ENTRY;
#include <poppack.h>

static const CHAR GptSignature[8] = { 'E','F','I',' ','P','A','R','T' };

static const GUID GptEspTypeGuid =
    {0xC12A7328, 0xF81F, 0x11D2, {0xBA, 0x4B, 0x00, 0xA0, 0xC9, 0x3E, 0xC9, 0x3B}};
static const GUID GptMsftBasicDataGuid =
    {0xEBD0A0A2, 0xB9E5, 0x4433, {0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7}};
static const GUID GptMsftReservedGuid =
    {0xE3C9E316, 0x0B5C, 0x4DB8, {0x81, 0x7D, 0xF9, 0x2D, 0xF0, 0x02, 0x15, 0xAE}};

static
BOOLEAN
IsEqualGuid(
    IN const GUID *Guid1,
    IN const GUID *Guid2)
{
    return (RtlCompareMemory(Guid1, Guid2, sizeof(GUID)) == sizeof(GUID));
}

static
BOOLEAN
DiskReadGptHeader(
    IN UCHAR DriveNumber,
    OUT PGEOMETRY Geometry,
    OUT PEFI_PARTITION_HEADER Header)
{
    SIZE_T CopySize;

    if (!MachDiskGetDriveGeometry(DriveNumber, Geometry) ||
        Geometry->BytesPerSector == 0)
    {
        RtlZeroMemory(Geometry, sizeof(*Geometry));
        Geometry->BytesPerSector = 512;
    }

    if (!MachDiskReadLogicalSectors(DriveNumber, 1ULL, 1, DiskReadBuffer))
    {
        return FALSE;
    }

    CopySize = min(sizeof(*Header), DiskReadBufferSize);
    RtlZeroMemory(Header, sizeof(*Header));
    RtlCopyMemory(Header, DiskReadBuffer, CopySize);

    if (RtlCompareMemory(Header->Signature, GptSignature, sizeof(GptSignature)) != sizeof(GptSignature))
    {
        TRACE("DiskReadGptHeader: invalid signature\n");
        return FALSE;
    }

    if (Header->HeaderSize < sizeof(EFI_PARTITION_HEADER) - sizeof(Header->Reserved2))
    {
        TRACE("DiskReadGptHeader: unexpected header size %lu\n", Header->HeaderSize);
        return FALSE;
    }

    if (Header->SizeOfPartitionEntry == 0 || Header->NumberOfPartitionEntries == 0)
    {
        TRACE("DiskReadGptHeader: empty partition array\n");
        return FALSE;
    }

    return TRUE;
}

static
BOOLEAN
DiskReadGptPartitionEntry(
    IN UCHAR DriveNumber,
    IN PEFI_PARTITION_HEADER Header,
    IN const GEOMETRY *Geometry,
    IN ULONG EntryIndex,
    OUT PEFI_PARTITION_ENTRY Entry)
{
    ULONGLONG EntryOffset;
    ULONGLONG SectorNumber;
    ULONG SectorSize;
    ULONG OffsetInSector;
    ULONG BytesNeeded;
    ULONG SectorCount;
    SIZE_T CopySize;

    SectorSize = (Geometry && Geometry->BytesPerSector) ? Geometry->BytesPerSector : 512;

    EntryOffset = (ULONGLONG)EntryIndex * Header->SizeOfPartitionEntry;
    SectorNumber = Header->PartitionEntryLBA + (EntryOffset / SectorSize);
    OffsetInSector = (ULONG)(EntryOffset % SectorSize);
    BytesNeeded = OffsetInSector + Header->SizeOfPartitionEntry;
    SectorCount = (BytesNeeded + SectorSize - 1) / SectorSize;

    if ((ULONGLONG)SectorCount * SectorSize > DiskReadBufferSize)
    {
        TRACE("DiskReadGptPartitionEntry: buffer too small (%lu bytes needed)\n", BytesNeeded);
        return FALSE;
    }

    if (!MachDiskReadLogicalSectors(DriveNumber, SectorNumber, SectorCount, DiskReadBuffer))
    {
        TRACE("DiskReadGptPartitionEntry: read failed (LBA=%llu, Count=%lu)\n",
              SectorNumber,
              SectorCount);
        return FALSE;
    }

    CopySize = min(sizeof(*Entry), Header->SizeOfPartitionEntry);
    RtlZeroMemory(Entry, sizeof(*Entry));
    RtlCopyMemory(Entry, (PUCHAR)DiskReadBuffer + OffsetInSector, CopySize);
    return TRUE;
}

static
UCHAR
DiskGptTypeToSystemIndicator(
    IN const GUID *TypeGuid)
{
    static const GUID GuidNull = {0};

    if (IsEqualGuid(TypeGuid, &GuidNull))
        return PARTITION_ENTRY_UNUSED;

    if (IsEqualGuid(TypeGuid, &GptMsftReservedGuid))
        return PARTITION_ENTRY_UNUSED;

    if (IsEqualGuid(TypeGuid, &GptEspTypeGuid))
        return PARTITION_FAT32;

    if (IsEqualGuid(TypeGuid, &GptMsftBasicDataGuid))
        return PARTITION_IFS;

    return PARTITION_IFS;
}

static
VOID
DiskConvertGptEntry(
    IN PEFI_PARTITION_ENTRY GptEntry,
    IN UCHAR SystemIndicator,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    ULONGLONG SectorCount;

    RtlZeroMemory(PartitionTableEntry, sizeof(*PartitionTableEntry));
    PartitionTableEntry->SystemIndicator = SystemIndicator;

    if (GptEntry->StartingLBA > MAXULONG)
        PartitionTableEntry->SectorCountBeforePartition = MAXULONG;
    else
        PartitionTableEntry->SectorCountBeforePartition = (ULONG)GptEntry->StartingLBA;

    if (GptEntry->EndingLBA >= GptEntry->StartingLBA)
        SectorCount = (GptEntry->EndingLBA - GptEntry->StartingLBA) + 1;
    else
        SectorCount = 0;

    if (SectorCount > MAXULONG)
        PartitionTableEntry->PartitionSectorCount = MAXULONG;
    else
        PartitionTableEntry->PartitionSectorCount = (ULONG)SectorCount;
}

/* BRFR signature at disk offset 0x600 */
#define XBOX_SIGNATURE_SECTOR 3
#define XBOX_SIGNATURE        ('B' | ('R' << 8) | ('F' << 16) | ('R' << 24))

/* Default hardcoded partition number to boot from Xbox disk */
#define FATX_DATA_PARTITION 1

static struct
{
    ULONG SectorCountBeforePartition;
    ULONG PartitionSectorCount;
    UCHAR SystemIndicator;
} XboxPartitions[] =
{
    /* This is in the \Device\Harddisk0\Partition.. order used by the Xbox kernel */
    { 0x0055F400, 0x0098F800, PARTITION_FAT32  }, /* Store , E: */
    { 0x00465400, 0x000FA000, PARTITION_FAT_16 }, /* System, C: */
    { 0x00000400, 0x00177000, PARTITION_FAT_16 }, /* Cache1, X: */
    { 0x00177400, 0x00177000, PARTITION_FAT_16 }, /* Cache2, Y: */
    { 0x002EE400, 0x00177000, PARTITION_FAT_16 }  /* Cache3, Z: */
};

static BOOLEAN
DiskReadBootRecord(
    IN UCHAR DriveNumber,
    IN ULONGLONG LogicalSectorNumber,
    OUT PMASTER_BOOT_RECORD BootRecord)
{
    ULONG Index;

    /* Read master boot record */
    if (!MachDiskReadLogicalSectors(DriveNumber, LogicalSectorNumber, 1, DiskReadBuffer))
    {
        return FALSE;
    }
    RtlCopyMemory(BootRecord, DiskReadBuffer, sizeof(MASTER_BOOT_RECORD));

    TRACE("Dumping partition table for drive 0x%x:\n", DriveNumber);
    TRACE("Boot record logical start sector = %d\n", LogicalSectorNumber);
    TRACE("sizeof(MASTER_BOOT_RECORD) = 0x%x.\n", sizeof(MASTER_BOOT_RECORD));

    for (Index = 0; Index < 4; Index++)
    {
        TRACE("-------------------------------------------\n");
        TRACE("Partition %d\n", (Index + 1));
        TRACE("BootIndicator: 0x%x\n", BootRecord->PartitionTable[Index].BootIndicator);
        TRACE("StartHead: 0x%x\n", BootRecord->PartitionTable[Index].StartHead);
        TRACE("StartSector (Plus 2 cylinder bits): 0x%x\n", BootRecord->PartitionTable[Index].StartSector);
        TRACE("StartCylinder: 0x%x\n", BootRecord->PartitionTable[Index].StartCylinder);
        TRACE("SystemIndicator: 0x%x\n", BootRecord->PartitionTable[Index].SystemIndicator);
        TRACE("EndHead: 0x%x\n", BootRecord->PartitionTable[Index].EndHead);
        TRACE("EndSector (Plus 2 cylinder bits): 0x%x\n", BootRecord->PartitionTable[Index].EndSector);
        TRACE("EndCylinder: 0x%x\n", BootRecord->PartitionTable[Index].EndCylinder);
        TRACE("SectorCountBeforePartition: 0x%x\n", BootRecord->PartitionTable[Index].SectorCountBeforePartition);
        TRACE("PartitionSectorCount: 0x%x\n", BootRecord->PartitionTable[Index].PartitionSectorCount);
    }

    /* Check the partition table magic value */
    return (BootRecord->MasterBootRecordMagic == 0xaa55);
}

static BOOLEAN
DiskGetFirstPartitionEntry(
    IN PMASTER_BOOT_RECORD MasterBootRecord,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    ULONG Index;

    for (Index = 0; Index < 4; Index++)
    {
        /* Check the system indicator. If it's not an extended or unused partition then we're done. */
        if ((MasterBootRecord->PartitionTable[Index].SystemIndicator != PARTITION_ENTRY_UNUSED) &&
            (MasterBootRecord->PartitionTable[Index].SystemIndicator != PARTITION_EXTENDED) &&
            (MasterBootRecord->PartitionTable[Index].SystemIndicator != PARTITION_XINT13_EXTENDED))
        {
            RtlCopyMemory(PartitionTableEntry, &MasterBootRecord->PartitionTable[Index], sizeof(PARTITION_TABLE_ENTRY));
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
DiskGetFirstExtendedPartitionEntry(
    IN PMASTER_BOOT_RECORD MasterBootRecord,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    ULONG Index;

    for (Index = 0; Index < 4; Index++)
    {
        /* Check the system indicator. If it an extended partition then we're done. */
        if ((MasterBootRecord->PartitionTable[Index].SystemIndicator == PARTITION_EXTENDED) ||
            (MasterBootRecord->PartitionTable[Index].SystemIndicator == PARTITION_XINT13_EXTENDED))
        {
            RtlCopyMemory(PartitionTableEntry, &MasterBootRecord->PartitionTable[Index], sizeof(PARTITION_TABLE_ENTRY));
            return TRUE;
        }
    }

    return FALSE;
}

static BOOLEAN
DiskGetActivePartitionEntry(
    IN UCHAR DriveNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry,
    OUT PULONG ActivePartition)
{
    ULONG BootablePartitionCount = 0;
    ULONG CurrentPartitionNumber;
    ULONG Index;
    MASTER_BOOT_RECORD MasterBootRecord;
    PPARTITION_TABLE_ENTRY ThisPartitionTableEntry;

    *ActivePartition = 0;

    /* Read master boot record */
    if (!DiskReadBootRecord(DriveNumber, 0, &MasterBootRecord))
    {
        return FALSE;
    }

    CurrentPartitionNumber = 0;
    for (Index = 0; Index < 4; Index++)
    {
        ThisPartitionTableEntry = &MasterBootRecord.PartitionTable[Index];

        if (ThisPartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED &&
            ThisPartitionTableEntry->SystemIndicator != PARTITION_EXTENDED &&
            ThisPartitionTableEntry->SystemIndicator != PARTITION_XINT13_EXTENDED)
        {
            CurrentPartitionNumber++;

            /* Test if this is the bootable partition */
            if (ThisPartitionTableEntry->BootIndicator == 0x80)
            {
                BootablePartitionCount++;
                *ActivePartition = CurrentPartitionNumber;

                /* Copy the partition table entry */
                RtlCopyMemory(PartitionTableEntry,
                              ThisPartitionTableEntry,
                              sizeof(PARTITION_TABLE_ENTRY));
            }
        }
    }

    /* Make sure there was only one bootable partition */
    if (BootablePartitionCount == 0)
    {
        ERR("No bootable (active) partitions found.\n");
        return FALSE;
    }
    else if (BootablePartitionCount != 1)
    {
        ERR("Too many bootable (active) partitions found.\n");
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
DiskGetMbrPartitionEntry(
    IN UCHAR DriveNumber,
    IN ULONG PartitionNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    MASTER_BOOT_RECORD MasterBootRecord;
    PARTITION_TABLE_ENTRY ExtendedPartitionTableEntry;
    ULONG ExtendedPartitionNumber;
    ULONG ExtendedPartitionOffset;
    ULONG Index;
    ULONG CurrentPartitionNumber;
    PPARTITION_TABLE_ENTRY ThisPartitionTableEntry;

    /* Read master boot record */
    if (!DiskReadBootRecord(DriveNumber, 0, &MasterBootRecord))
    {
        return FALSE;
    }

    CurrentPartitionNumber = 0;
    for (Index = 0; Index < 4; Index++)
    {
        ThisPartitionTableEntry = &MasterBootRecord.PartitionTable[Index];

        if (ThisPartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED &&
            ThisPartitionTableEntry->SystemIndicator != PARTITION_EXTENDED &&
            ThisPartitionTableEntry->SystemIndicator != PARTITION_XINT13_EXTENDED)
        {
            CurrentPartitionNumber++;
        }

        if (PartitionNumber == CurrentPartitionNumber)
        {
            RtlCopyMemory(PartitionTableEntry, ThisPartitionTableEntry, sizeof(PARTITION_TABLE_ENTRY));
            return TRUE;
        }
    }

    /*
     * They want an extended partition entry so we will need
     * to loop through all the extended partitions on the disk
     * and return the one they want.
     */
    ExtendedPartitionNumber = PartitionNumber - CurrentPartitionNumber - 1;

    /*
     * Set the initial relative starting sector to 0.
     * This is because extended partition starting
     * sectors a numbered relative to their parent.
     */
    ExtendedPartitionOffset = 0;

    for (Index = 0; Index <= ExtendedPartitionNumber; Index++)
    {
        /* Get the extended partition table entry */
        if (!DiskGetFirstExtendedPartitionEntry(&MasterBootRecord, &ExtendedPartitionTableEntry))
        {
            return FALSE;
        }

        /* Adjust the relative starting sector of the partition */
        ExtendedPartitionTableEntry.SectorCountBeforePartition += ExtendedPartitionOffset;
        if (ExtendedPartitionOffset == 0)
        {
            /* Set the start of the parrent extended partition */
            ExtendedPartitionOffset = ExtendedPartitionTableEntry.SectorCountBeforePartition;
        }
        /* Read the partition boot record */
        if (!DiskReadBootRecord(DriveNumber, ExtendedPartitionTableEntry.SectorCountBeforePartition, &MasterBootRecord))
        {
            return FALSE;
        }

        /* Get the first real partition table entry */
        if (!DiskGetFirstPartitionEntry(&MasterBootRecord, PartitionTableEntry))
        {
            return FALSE;
        }

        /* Now correct the start sector of the partition */
        PartitionTableEntry->SectorCountBeforePartition += ExtendedPartitionTableEntry.SectorCountBeforePartition;
    }

    /*
     * When we get here we should have the correct entry already
     * stored in PartitionTableEntry, so just return TRUE.
     */
    return TRUE;
}

static BOOLEAN
DiskGetBrfrPartitionEntry(
    IN UCHAR DriveNumber,
    IN ULONG PartitionNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    /*
     * Get partition entry of an Xbox-standard BRFR partitioned disk.
     */
    if (PartitionNumber >= 1 && PartitionNumber <= sizeof(XboxPartitions) / sizeof(XboxPartitions[0]) &&
        MachDiskReadLogicalSectors(DriveNumber, XBOX_SIGNATURE_SECTOR, 1, DiskReadBuffer))
    {
        if (*((PULONG)DiskReadBuffer) != XBOX_SIGNATURE)
        {
            /* No magic Xbox partitions */
            return FALSE;
        }

        RtlZeroMemory(PartitionTableEntry, sizeof(PARTITION_TABLE_ENTRY));
        PartitionTableEntry->SystemIndicator = XboxPartitions[PartitionNumber - 1].SystemIndicator;
        PartitionTableEntry->SectorCountBeforePartition = XboxPartitions[PartitionNumber - 1].SectorCountBeforePartition;
        PartitionTableEntry->PartitionSectorCount = XboxPartitions[PartitionNumber - 1].PartitionSectorCount;
        return TRUE;
    }

    /* Partition does not exist */
    return FALSE;
}

VOID
DiskDetectPartitionType(
    IN UCHAR DriveNumber)
{
    MASTER_BOOT_RECORD MasterBootRecord;
    ULONG Index;
    ULONG PartitionCount = 0;
    PPARTITION_TABLE_ENTRY ThisPartitionTableEntry;
    PARTITION_TABLE_ENTRY PartitionTableEntry;
    BOOLEAN GPTProtect = FALSE;
    BOOLEAN HaveValidEntry = FALSE;

    /* Probe for Master Boot Record */
    if (DiskReadBootRecord(DriveNumber, 0, &MasterBootRecord))
    {
        DiskPartitionType[DriveNumber] = PARTITION_STYLE_MBR;

        /* Check for GUID Partition Table */
        for (Index = 0; Index < 4; Index++)
        {
            ThisPartitionTableEntry = &MasterBootRecord.PartitionTable[Index];

            if (ThisPartitionTableEntry->SystemIndicator != PARTITION_ENTRY_UNUSED)
            {
                PartitionCount++;

                if (Index == 0 && ThisPartitionTableEntry->SystemIndicator == PARTITION_GPT)
                {
                    GPTProtect = TRUE;
                }
            }

            if (DiskIsValidMbrPartitionEntry(ThisPartitionTableEntry))
            {
                HaveValidEntry = TRUE;
            }
        }

        if (PartitionCount == 0)
        {
            DiskPartitionType[DriveNumber] = PARTITION_STYLE_RAW;
            TRACE("Drive 0x%X partition type unknown\n", DriveNumber);
            return;
        }

        if (!GPTProtect && !HaveValidEntry)
        {
            DiskPartitionType[DriveNumber] = PARTITION_STYLE_RAW;
            TRACE("Drive 0x%X partition type unknown\n", DriveNumber);
            return;
        }

        if (PartitionCount == 1 && GPTProtect)
        {
            DiskPartitionType[DriveNumber] = PARTITION_STYLE_GPT;
        }
        TRACE("Drive 0x%X partition type %s\n", DriveNumber, DiskPartitionType[DriveNumber] == PARTITION_STYLE_MBR ? "MBR" : "GPT");
        return;
    }

    /* Probe for Xbox-BRFR partitioning */
    if (DiskGetBrfrPartitionEntry(DriveNumber, FATX_DATA_PARTITION, &PartitionTableEntry))
    {
        DiskPartitionType[DriveNumber] = PARTITION_STYLE_BRFR;
        TRACE("Drive 0x%X partition type Xbox-BRFR\n", DriveNumber);
        return;
    }

    /* Failed to detect partitions, assume partitionless disk */
    DiskPartitionType[DriveNumber] = PARTITION_STYLE_RAW;
    TRACE("Drive 0x%X partition type unknown\n", DriveNumber);
}

BOOLEAN
DiskGetBootPartitionEntry(
    IN UCHAR DriveNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry,
    OUT PULONG BootPartition)
{
    switch (DiskPartitionType[DriveNumber])
    {
        case PARTITION_STYLE_MBR:
        {
            return DiskGetActivePartitionEntry(DriveNumber, PartitionTableEntry, BootPartition);
        }
        case PARTITION_STYLE_GPT:
        {
            EFI_PARTITION_HEADER Header;
            EFI_PARTITION_ENTRY GptEntry;
            GEOMETRY Geometry;
            ULONG Index;
            ULONG LogicalPartition = 0;
            BOOLEAN HaveCandidate = FALSE;
            PARTITION_TABLE_ENTRY CandidateEntry;
            ULONG CandidateNumber = 0;
            UCHAR Indicator;

            if (!DiskReadGptHeader(DriveNumber, &Geometry, &Header))
                return FALSE;

            RtlZeroMemory(&CandidateEntry, sizeof(CandidateEntry));

            for (Index = 0; Index < Header.NumberOfPartitionEntries; ++Index)
            {
                if (!DiskReadGptPartitionEntry(DriveNumber, &Header, &Geometry, Index, &GptEntry))
                    return FALSE;

                Indicator = DiskGptTypeToSystemIndicator(&GptEntry.PartitionTypeGuid);
                if (Indicator == PARTITION_ENTRY_UNUSED)
                    continue;

                if (GptEntry.StartingLBA == 0 || GptEntry.EndingLBA < GptEntry.StartingLBA)
                    continue;

                LogicalPartition++;

                if (!HaveCandidate)
                {
                    DiskConvertGptEntry(&GptEntry, Indicator, &CandidateEntry);
                    CandidateNumber = LogicalPartition;
                    HaveCandidate = TRUE;
                }

                if (IsEqualGuid(&GptEntry.PartitionTypeGuid, &GptEspTypeGuid))
                {
                    DiskConvertGptEntry(&GptEntry, Indicator, PartitionTableEntry);
                    *BootPartition = LogicalPartition;
                    return TRUE;
                }
            }

            if (!HaveCandidate)
                return FALSE;

            RtlCopyMemory(PartitionTableEntry, &CandidateEntry, sizeof(*PartitionTableEntry));
            *BootPartition = CandidateNumber;
            return TRUE;
        }
        case PARTITION_STYLE_RAW:
        {
            if (!DiskBuildRawPartitionEntry(DriveNumber, PartitionTableEntry))
                return FALSE;

            *BootPartition = 1;
            return TRUE;
        }
        case PARTITION_STYLE_BRFR:
        {
            if (DiskGetBrfrPartitionEntry(DriveNumber, FATX_DATA_PARTITION, PartitionTableEntry))
            {
                *BootPartition = FATX_DATA_PARTITION;
                return TRUE;
            }
            return FALSE;
        }
        default:
        {
            ERR("Drive 0x%X partition type = %d, should not happen!\n", DriveNumber, DiskPartitionType[DriveNumber]);
            ASSERT(FALSE);
        }
    }
    return FALSE;
}

BOOLEAN
DiskGetPartitionEntry(
    IN UCHAR DriveNumber,
    IN ULONG PartitionNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry)
{
    switch (DiskPartitionType[DriveNumber])
    {
        case PARTITION_STYLE_MBR:
        {
            return DiskGetMbrPartitionEntry(DriveNumber, PartitionNumber, PartitionTableEntry);
        }
        case PARTITION_STYLE_GPT:
        {
            EFI_PARTITION_HEADER Header;
            EFI_PARTITION_ENTRY GptEntry;
            GEOMETRY Geometry;
            ULONG Index;
            ULONG LogicalPartition = 0;
            UCHAR Indicator;

            if (!DiskReadGptHeader(DriveNumber, &Geometry, &Header))
                return FALSE;

            for (Index = 0; Index < Header.NumberOfPartitionEntries; ++Index)
            {
                if (!DiskReadGptPartitionEntry(DriveNumber, &Header, &Geometry, Index, &GptEntry))
                    return FALSE;

                Indicator = DiskGptTypeToSystemIndicator(&GptEntry.PartitionTypeGuid);
                if (Indicator == PARTITION_ENTRY_UNUSED)
                    continue;

                if (GptEntry.StartingLBA == 0 || GptEntry.EndingLBA < GptEntry.StartingLBA)
                    continue;

                LogicalPartition++;
                if (LogicalPartition == PartitionNumber)
                {
                    DiskConvertGptEntry(&GptEntry, Indicator, PartitionTableEntry);
                    return TRUE;
                }
            }

            return FALSE;
        }
        case PARTITION_STYLE_RAW:
        {
            if (PartitionNumber != 1)
                return FALSE;

            return DiskBuildRawPartitionEntry(DriveNumber, PartitionTableEntry);
        }
        case PARTITION_STYLE_BRFR:
        {
            return DiskGetBrfrPartitionEntry(DriveNumber, PartitionNumber, PartitionTableEntry);
        }
        default:
        {
            ERR("Drive 0x%X partition type = %d, should not happen!\n", DriveNumber, DiskPartitionType[DriveNumber]);
            ASSERT(FALSE);
        }
    }
    return FALSE;
}

#endif
