/*
 * PROJECT:     FreeLoader
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Implements routines to support booting from a RAM Disk.
 * COPYRIGHT:   Copyright 2008 ReactOS Portable Systems Group
 *              Copyright 2009 Hervé Poussineau
 *              Copyright 2019 Hermes Belusca-Maito
 */

/* INCLUDES *******************************************************************/

#include <freeldr.h>
#include <debug.h>
#include <ctype.h>
#include <limits.h>
#include <stddef.h>
#include <string.h>
#include <ntstrsafe.h>
#include <fs/iso.h>
#include <fs/fat.h>
#include <disk.h>
#include <arch/archwsup.h>

#ifdef UEFIBOOT
extern BOOLEAN UefiVideoDisplayBootLogo(VOID);
extern BOOLEAN UefiVideoIsBootLogoDrawn(VOID);
#endif

#if defined(__GNUC__)
extern VOID
AddReactOSArcDiskInfo(
    IN PSTR ArcName,
    IN ULONG Signature,
    IN ULONG Checksum,
    IN BOOLEAN ValidPartitionTable) __attribute__((weak));
#else
VOID
AddReactOSArcDiskInfo(
    IN PSTR ArcName,
    IN ULONG Signature,
    IN ULONG Checksum,
    IN BOOLEAN ValidPartitionTable);
#endif

#if defined(__GNUC__)
VOID FatFlushCache(VOID) __attribute__((weak));
#else
VOID FatFlushCache(VOID);
#if defined(_MSC_VER)
static VOID FatFlushCacheStub(VOID)
{
    /* Optional legacy FAT cache flush is unavailable; nothing to do. */
}
#pragma comment(linker, "/alternatename:FatFlushCache=FatFlushCacheStub")
#endif
#endif
#include "lib/fatfs/ff.h"
#include <ramdisk_signature.h>
#include "../ntldr/ntldropts.h"

#if defined(__GNUC__)
extern ULONG ArcGetRelativeTime(VOID) __attribute__((weak));
#endif

#if defined(__GNUC__)
__attribute__((weak)) SIZE_T DiskReadBufferSize = 0;
#else
extern SIZE_T DiskReadBufferSize;
#endif

ULONGLONG DbgQueryMicrosecondsSinceBoot(VOID);

DBG_DEFAULT_CHANNEL(DISK);

#if DBG
static
VOID
RamDiskTraceSample(IN PCSTR Label,
                   IN const VOID *Address)
{
    const UCHAR *Bytes;

    if (!Label || !Address)
    {
        return;
    }

    Bytes = (const UCHAR *)Address;
    TRACE("%s [%p]: %02X %02X %02X %02X %02X %02X %02X %02X\n",
          Label,
          Address,
          Bytes[0], Bytes[1], Bytes[2], Bytes[3],
          Bytes[4], Bytes[5], Bytes[6], Bytes[7]);
}

static
PFN_NUMBER
RamDiskPointerToPfn(IN const VOID *Address)
{
    return Address ? (PFN_NUMBER)(((ULONG_PTR)Address) >> MM_PAGE_SHIFT) : 0;
}
#endif

#define RAMDISK_ALLOCATION_ALIGNMENT 0x1000ULL
#define ALIGN_UP_BY_ULL(Value, Alignment) \
    (((Value) + ((Alignment) - 1ULL)) & ~((Alignment) - 1ULL))

/* GLOBALS ********************************************************************/

PVOID gInitRamDiskBase = NULL;
ULONG gInitRamDiskSize = 0;

static BOOLEAN   RamDiskDeviceRegistered = FALSE;
static PVOID     RamDiskBase;
static ULONGLONG RamDiskFileSize;    // FIXME: RAM disks currently limited to 4GB.
static ULONGLONG RamDiskImageLength; // Total bytes populated in the backing store from the start of the allocation
static ULONG     RamDiskImageOffset; // Starting offset from the Ramdisk base.
static ULONGLONG RamDiskVolumeOffset; // Offset where the FAT volume starts (typically after the MBR)
static ULONGLONG RamDiskVolumeLength; // Length of the exposed FAT volume
static ULONGLONG RamDiskOffset;      // Current position in the Ramdisk.
static ULONGLONG RamDiskRequestedSize = 0;
static PVOID     RamDiskWritableBase = NULL;
static ULONGLONG RamDiskWritableSize = 0;
static BOOLEAN   RamDiskErrorShown = FALSE;

#if defined(_M_AMD64) || defined(__x86_64__) || defined(_M_ARM64) || defined(__aarch64__)
#define RAMDISK_MAX_LOW_BYTES     ((ULONGLONG)MM_MAX_PAGE_LOADER_MAPPED << MM_PAGE_SHIFT)
#else
#define RAMDISK_MAX_LOW_BYTES     (0x40000000ULL) /* 1 GiB on 32-bit */
#endif

#define RAMDISK_LOW_ALLOC_MAX     RAMDISK_MAX_LOW_BYTES
#define RAMDISK_SAFETY_SLACK      (64ULL * 1024ULL * 1024ULL)

static
BOOLEAN
RamDiskComputeMbrMetadata(IN PVOID BaseAddress,
                          IN ULONGLONG DiskSize,
                          OUT PULONG Signature,
                          OUT PULONG Checksum,
                          OUT PBOOLEAN ValidPartition)
{
    PMASTER_BOOT_RECORD MasterBootRecord;
    ULONG Sum = 0;
    ULONG WordCount;
    PULONG Words;

    if (!BaseAddress || DiskSize < sizeof(MASTER_BOOT_RECORD))
        return FALSE;

    MasterBootRecord = (PMASTER_BOOT_RECORD)BaseAddress;
    if (MasterBootRecord->MasterBootRecordMagic != 0xAA55)
        return FALSE;

    WordCount = (ULONG)(sizeof(MASTER_BOOT_RECORD) / sizeof(ULONG));
    Words = (PULONG)MasterBootRecord;
    for (ULONG Index = 0; Index < WordCount; ++Index)
    {
        Sum += Words[Index];
    }

    if (MasterBootRecord->Signature == 0 || MasterBootRecord->Signature == 0xFFFFFFFFu)
    {
        MasterBootRecord->Signature = RamDiskDeriveDiskSignature(BaseAddress, DiskSize);

        /* Recalculate the checksum after updating the signature. */
        Sum = 0;
        for (ULONG Index = 0; Index < WordCount; ++Index)
        {
            Sum += Words[Index];
        }
    }

    if (Signature)
        *Signature = MasterBootRecord->Signature;

    if (Checksum)
        *Checksum = (~Sum) + 1;

    if (ValidPartition)
    {
        BOOLEAN Found = FALSE;

        for (ULONG EntryIndex = 0; EntryIndex < RTL_NUMBER_OF(MasterBootRecord->PartitionTable); ++EntryIndex)
        {
            const PARTITION_TABLE_ENTRY *Entry = &MasterBootRecord->PartitionTable[EntryIndex];

            if (Entry->SystemIndicator != PARTITION_ENTRY_UNUSED &&
                Entry->PartitionSectorCount != 0)
            {
                Found = TRUE;
                break;
            }
        }

        *ValidPartition = Found;
    }

    return TRUE;
}

static
VOID
RamDiskRegisterArcDevice(VOID)
{
    static BOOLEAN ArcRegistered = FALSE;
    ULONG Signature;
    ULONG Checksum;
    BOOLEAN ValidPartition;

    if (ArcRegistered)
        return;

    if (!RamDiskBase || RamDiskFileSize < sizeof(MASTER_BOOT_RECORD))
        return;

    if (!RamDiskComputeMbrMetadata(RamDiskBase,
                                   RamDiskFileSize,
                                   &Signature,
                                   &Checksum,
                                   &ValidPartition))
    {
        return;
    }

    if (AddReactOSArcDiskInfo)
    {
        AddReactOSArcDiskInfo("ramdisk(0)", Signature, Checksum, ValidPartition);
    }
    ArcRegistered = TRUE;
}

static BOOLEAN RamDiskReserveWritableBuffer(ULONGLONG RequestedSize, BOOLEAN OptionalRamDisk);

static VOID
RamDiskSetVisibleRegion(IN ULONGLONG Offset,
                        IN ULONGLONG Length)
{
    RamDiskVolumeOffset = Offset;
    RamDiskVolumeLength = Length;
    /* Note: Caller must call RamDiskInvalidateFatCache() after changing visible LBA window,
       as the FAT mount state is invalidated when the underlying disk region changes */
}

static VOID
RamDiskResetVisibleRegion(VOID)
{
    ULONGLONG VisibleLength = 0;

    if (RamDiskImageLength > RamDiskImageOffset)
        VisibleLength = RamDiskImageLength - RamDiskImageOffset;

    RamDiskSetVisibleRegion(RamDiskImageOffset, VisibleLength);
}

static VOID
RamDiskInvalidateFatCache(VOID)
{
#if defined(__GNUC__)
    if (FatFlushCache)
        FatFlushCache();
#else
    FatFlushCache();
#endif
}

static
ULONGLONG
RamDiskWritableAllocationLimit(VOID)
{
    if (RAMDISK_LOW_ALLOC_MAX > RAMDISK_SAFETY_SLACK)
        return RAMDISK_LOW_ALLOC_MAX - RAMDISK_SAFETY_SLACK;

    return RAMDISK_LOW_ALLOC_MAX;
}

static VOID
RamDiskReleaseMemory(PVOID Base,
                     ULONGLONG Size)
{
    if (!Base)
        return;

    (void)Size;

    MmFreeMemory(Base);
}

#if defined(__GNUC__)
__attribute__((used))
#endif
DWORD
get_fattime(VOID)
{
    /* Return a fixed timestamp: 2025-01-01 00:00:00 */
    return (DWORD)(((2025 - 1980) << 25) | (1 << 21) | (1 << 16));
}

#define ISO_SECTOR_SIZE 2048

typedef struct _ISO_SOURCE
{
    const UCHAR *MemoryBase;
    ULONGLONG Size;
    ULONG ArcFileId;
    ULONGLONG ArcOffset;
    ULONGLONG ArcPosition;
} ISO_SOURCE, *PISO_SOURCE;

static
ULONG
RamDiskGetRelativeTime(VOID)
{
    if (ArcGetRelativeTime)
        return ArcGetRelativeTime();

    return 0;
}

static
ULONGLONG
RamDiskQueryMicroseconds(VOID)
{
    ULONGLONG Micros = DbgQueryMicrosecondsSinceBoot();

    if (Micros != 0)
        return Micros;

    return (ULONGLONG)RamDiskGetRelativeTime() * 1000000ULL;
}

static
ARC_STATUS
RamDiskOpenIsoSource(
    _In_ PCSTR FileName,
    _In_opt_ PCSTR DefaultPath,
    _In_ ULONGLONG ImageOffset,
    _In_ ULONGLONG ImageLength,
    _Out_ PISO_SOURCE Source)
{
    ARC_STATUS Status;
    ULONG FileId;
    FILEINFORMATION Information;
    ULONGLONG FileSize;
    ULONGLONG EffectiveLength;
    UCHAR Descriptor[ISO_SECTOR_SIZE];
    LARGE_INTEGER Position;
    ULONG BytesRead;
    BOOLEAN OpenedRawDevice = FALSE;

    if (!Source)
        return EINVAL;

    RtlZeroMemory(Source, sizeof(*Source));
    Source->ArcFileId = INVALID_FILE_ID;

    Status = FsOpenFile((PCHAR)FileName, DefaultPath, OpenReadOnly, &FileId);
    if (Status != ESUCCESS)
    {
        /* Fall back to opening the ARC device directly (e.g. CD/DVD handle) */
        if (FileName && strchr(FileName, ')'))
        {
            Status = ArcOpen((PCHAR)FileName, OpenReadOnly, &FileId);
            if (Status == ESUCCESS)
            {
                OpenedRawDevice = TRUE;
            }
        }

        if (Status != ESUCCESS)
            return Status;
    }

    Status = ArcGetFileInformation(FileId, &Information);
    if (Status != ESUCCESS)
    {
        ArcClose(FileId);
        return Status;
    }

    FileSize = Information.EndingAddress.QuadPart;

    /*
     * Some firmware/device paths report the raw device capacity here,
     * which for 2KiB-block optical media can be 4x the ISO size when
     * combined with 512-byte sector-based partition metadata. Read the
     * ISO9660 Primary Volume Descriptor to obtain an authoritative size
     * and prefer it when available.
     */
    {
        ULONGLONG PvdOffset = ImageOffset + (ULONGLONG)16 * ISO_SECTOR_SIZE;

        Position.QuadPart = PvdOffset;
        if (ArcSeek(FileId, &Position, SeekAbsolute) == ESUCCESS)
        {
            if (ArcRead(FileId, Descriptor, ISO_SECTOR_SIZE, &BytesRead) == ESUCCESS &&
                BytesRead == ISO_SECTOR_SIZE)
            {
                PPVD Pvd = (PPVD)Descriptor;
                if (Pvd->VdType == 1 &&
                    RtlEqualMemory(Pvd->StandardId, "CD001", 5) &&
                    Pvd->VdVersion == 1)
                {
                    USHORT LogicalBlockSize = Pvd->LogicalBlockSizeL;
                    if (LogicalBlockSize == 0)
                        LogicalBlockSize = ISO_SECTOR_SIZE; /* Fallback to default */

                    /* Compute the ISO byte length from the PVD */
                    ULONGLONG IsoLength = (ULONGLONG)Pvd->VolumeSpaceSizeL * LogicalBlockSize;

                    /* Prefer the PVD-declared size when sensible (never larger than reported file size if nonzero). */
                    if (IsoLength != 0 && (FileSize == 0 || IsoLength <= FileSize))
                        FileSize = IsoLength;
                }
            }
        }

        /* Restore the caller's requested starting position */
        Position.QuadPart = ImageOffset;
        ArcSeek(FileId, &Position, SeekAbsolute);
    }

    if (FileSize == 0 && ImageLength != 0)
    {
        /* Some firmware return 0 for raw devices; use the supplied length as a hint */
        FileSize = ImageOffset + ImageLength;
    }

    if (FileSize != 0 && ImageOffset >= FileSize)
    {
        ArcClose(FileId);
        return EINVAL;
    }

    EffectiveLength = (FileSize != 0) ? (FileSize - ImageOffset) : ImageLength;
    if (FileSize != 0 && ImageLength != 0 && ImageLength < EffectiveLength)
        EffectiveLength = ImageLength;
    if (EffectiveLength == 0)
    {
        ArcClose(FileId);
        return EINVAL;
    }

    Source->MemoryBase = NULL;
    Source->ArcFileId = FileId;
    Source->ArcOffset = ImageOffset;
    Source->Size = EffectiveLength;
    Source->ArcPosition = OpenedRawDevice ? ImageOffset : 0;

    return ESUCCESS;
}

static
VOID
RamDiskCloseIsoSource(
    _Inout_ PISO_SOURCE Source)
{
    if (!Source)
        return;

    if (Source->ArcFileId != INVALID_FILE_ID)
    {
        ArcClose(Source->ArcFileId);
        Source->ArcFileId = INVALID_FILE_ID;
    }
}

BOOLEAN
RamDiskBuildWritableImage(
    IN PISO_SOURCE Source,
    IN ULONGLONG RequestedSize,
    OUT PVOID *NewBase,
    OUT PULONGLONG NewSize,
    IN BOOLEAN OptionalRamDisk)
{
    PVOID WritableBase = NULL;
    ULONGLONG WritableSize = 0;
    ULONGLONG RequiredSize;
    ULONGLONG SourceSize;
    ULONGLONG ResidentBytes = 0;
    ULONGLONG BytesToCopy;

    if (!Source || !NewBase || !NewSize || Source->Size == 0)
        return FALSE;

    SourceSize = Source->Size;

    /* Buffer = source data + extra writable headroom (min 64 MiB) */
    RequiredSize = RequestedSize;
    if (RequiredSize < SourceSize + (64ULL * 1024ULL * 1024ULL))
        RequiredSize = SourceSize + (64ULL * 1024ULL * 1024ULL);

    if (RequiredSize + RAMDISK_SAFETY_SLACK > RAMDISK_LOW_ALLOC_MAX)
    {
        WARN("RamDiskBuildWritableImage: requested size %llu exceeds low-memory limit %llu\n",
             RequiredSize,
             RAMDISK_LOW_ALLOC_MAX);
        if (!RamDiskErrorShown && !OptionalRamDisk)
        {
            UiMessageBox("Requested writable RAM disk size exceeds available low memory.");
            RamDiskErrorShown = TRUE;
        }
        return FALSE;
    }

    if (Source->MemoryBase)
        ResidentBytes = ALIGN_UP_BY_ULL(SourceSize, RAMDISK_ALLOCATION_ALIGNMENT);

    if ((RequiredSize + ResidentBytes + RAMDISK_SAFETY_SLACK) > RAMDISK_LOW_ALLOC_MAX)
    {
        WARN("RamDiskBuildWritableImage: %llu-byte source plus %llu-byte writable buffer exceed low-memory budget %llu\n",
             SourceSize,
             RequiredSize,
             RAMDISK_LOW_ALLOC_MAX);
        if (!RamDiskErrorShown && !OptionalRamDisk)
        {
            UiMessageBox("Writable RAM disk request uses too much low memory.");
            RamDiskErrorShown = TRUE;
        }
        return FALSE;
    }

    RequiredSize = ALIGN_UP_BY_ULL(RequiredSize, RAMDISK_ALLOCATION_ALIGNMENT);
    if (RequiredSize == 0 || RequiredSize > MAXULONG)
        return FALSE;

    TRACE("RamDiskBuildWritableImage: source=%llu requested=%llu align=%llu\n",
          SourceSize, RequestedSize, RequiredSize);

    if (!RamDiskReserveWritableBuffer(RequiredSize, OptionalRamDisk))
        return FALSE;

    if (!RamDiskGetReservedBuffer(RequiredSize, &WritableBase, &WritableSize))
        return FALSE;

    if (WritableSize > MAXULONG)
        WritableSize = MAXULONG;

    /*
     * Raw-copy the source medium (ISO or FAT) into the writable buffer.
     * The source's own filesystem structure is preserved as-is; FreeLDR's
     * filesystem detection (IsoMount / FatMount) handles both transparently.
     */
    BytesToCopy = SourceSize;
    if (BytesToCopy > WritableSize)
        BytesToCopy = WritableSize;

    TRACE("RamDiskBuildWritableImage: copying %llu bytes from source into %llu-byte buffer\n",
          BytesToCopy, WritableSize);

    UiDrawProgressBarCenter("Copying source to writable RAM disk...");

    {
        ULONGLONG TotalDone = 0;
        ULONG CopyChunk = 8 * 1024 * 1024;
        ULONG LastPercent = 0;
        ULONGLONG StartMicros = RamDiskQueryMicroseconds();
        ULONGLONG LastUpdateMicros = StartMicros;
        ULONG TotalMB = (ULONG)((BytesToCopy + (1024ULL * 1024 - 1)) / (1024ULL * 1024));
        CHAR ProgressMsg[128];
        BOOLEAN IsArcRead = FALSE;

        if (Source->MemoryBase)
        {
            /* Memory-to-memory copy path */
        }
        else if (Source->ArcFileId != INVALID_FILE_ID)
        {
            LARGE_INTEGER SeekPos;
            SeekPos.QuadPart = Source->ArcOffset;
            if (ArcSeek(Source->ArcFileId, &SeekPos, SeekAbsolute) != ESUCCESS)
            {
                RamDiskReleaseMemory(WritableBase, WritableSize);
                return FALSE;
            }
            IsArcRead = TRUE;
        }
        else
        {
            RamDiskReleaseMemory(WritableBase, WritableSize);
            return FALSE;
        }

        for (TotalDone = 0; TotalDone < BytesToCopy; )
        {
            ULONG CurrentChunk = CopyChunk;
            ULONG Percent;
            ULONGLONG NowMicros;

            if ((BytesToCopy - TotalDone) < CurrentChunk)
                CurrentChunk = (ULONG)(BytesToCopy - TotalDone);

            if (IsArcRead)
            {
                ULONG Count;
                if (ArcRead(Source->ArcFileId,
                            (PVOID)((ULONG_PTR)WritableBase + (ULONG_PTR)TotalDone),
                            CurrentChunk, &Count) != ESUCCESS ||
                    Count != CurrentChunk)
                {
                    RamDiskReleaseMemory(WritableBase, WritableSize);
                    return FALSE;
                }
            }
            else
            {
                RtlCopyMemory((PVOID)((ULONG_PTR)WritableBase + (ULONG_PTR)TotalDone),
                              (PVOID)((ULONG_PTR)Source->MemoryBase + (ULONG_PTR)TotalDone),
                              CurrentChunk);
            }

            TotalDone += CurrentChunk;
            Percent = (ULONG)((TotalDone * 100) / BytesToCopy);
            NowMicros = RamDiskQueryMicroseconds();

            /* Update TUI every ~500ms or on percent change */
            if (Percent != LastPercent ||
                (NowMicros - LastUpdateMicros) >= 500000ULL)
            {
                ULONG DoneMB = (ULONG)(TotalDone / (1024ULL * 1024));
                ULONGLONG ElapsedMicros = NowMicros - StartMicros;
                ULONG SpeedMBs = 0;
                ULONG SecsLeft = 0;

                if (ElapsedMicros > 0)
                {
                    /* Speed in MB/s: (bytes / elapsed_us) * 1000000 / 1048576 */
                    SpeedMBs = (ULONG)((TotalDone * 1000000ULL) /
                                       (ElapsedMicros * 1048576ULL));
                    if (SpeedMBs == 0)
                        SpeedMBs = 1;
                    if (TotalDone < BytesToCopy)
                    {
                        ULONGLONG Remaining = BytesToCopy - TotalDone;
                        /* seconds = remaining_bytes / (total_bytes / elapsed_s) */
                        SecsLeft = (ULONG)((Remaining * ElapsedMicros) /
                                           (TotalDone * 1000000ULL));
                    }
                }

                RtlStringCbPrintfA(ProgressMsg, sizeof(ProgressMsg),
                                   "Ramdisk %u%% (%u/%u MB, %u MB/s, %us left)",
                                   Percent, DoneMB, TotalMB,
                                   SpeedMBs, SecsLeft);
                UiUpdateProgressBar(Percent, ProgressMsg);
                TRACE("RamDiskBuildWritableImage: %s\n", ProgressMsg);
                LastPercent = Percent;
                LastUpdateMicros = NowMicros;
            }
        }

        /* Final update */
        {
            ULONGLONG ElapsedMicros = RamDiskQueryMicroseconds() - StartMicros;
            ULONG ElapsedSecs = (ULONG)(ElapsedMicros / 1000000ULL);
            ULONG AvgMBs = 0;
            if (ElapsedSecs > 0)
                AvgMBs = (ULONG)(TotalMB / ElapsedSecs);

            RtlStringCbPrintfA(ProgressMsg, sizeof(ProgressMsg),
                               "Ramdisk copy complete (%u MB in %us, %u MB/s)",
                               TotalMB, ElapsedSecs, AvgMBs);
            UiUpdateProgressBar(100, ProgressMsg);
            TRACE("RamDiskBuildWritableImage: %s\n", ProgressMsg);
        }
    }

    /* Zero space beyond the source data */
    if (BytesToCopy < WritableSize)
    {
        RtlZeroMemory((PVOID)((ULONG_PTR)WritableBase + (ULONG_PTR)BytesToCopy),
                      (SIZE_T)(WritableSize - BytesToCopy));
    }

    /* Expose the full buffer; filesystem detection handles the format */
    RamDiskSetVisibleRegion(0, WritableSize);

#if DBG
    {
        PFN_NUMBER BasePfn = RamDiskPointerToPfn(WritableBase);
        PFN_NUMBER EndPfn = RamDiskPointerToPfn((const VOID *)((ULONG_PTR)WritableBase + (ULONG_PTR)(WritableSize - 1)));
        TRACE("RamDiskBuildWritableImage: PFN span %lx-%lx source=%llu buffer=%llu\n",
              BasePfn, EndPfn, BytesToCopy, WritableSize);
        RamDiskTraceSample("  writable[0]", WritableBase);
        if (Source->MemoryBase)
        {
            PFN_NUMBER SourceBasePfn = RamDiskPointerToPfn(Source->MemoryBase);
            TRACE("  source: base=%p size=%llu basePFN=%lx\n",
                  Source->MemoryBase, Source->Size, SourceBasePfn);
        }
    }
#endif

    *NewBase = WritableBase;
    *NewSize = WritableSize;
    TRACE("RamDiskBuildWritableImage: writable ramdisk ready at %p (%llu bytes)\n",
          WritableBase,
          WritableSize);
    return TRUE;
}

static
ULONGLONG
RamDiskParseSizeString(
    PCSTR ValueString,
    ULONG ValueLength)
{
    ULONGLONG Value = 0;
    ULONGLONG Multiplier = 1;
    BOOLEAN SawDigit = FALSE;
    ULONG Index = 0;

    if (!ValueString || ValueLength == 0)
        return 0;

    /* Skip leading whitespace */
    while (Index < ValueLength && isspace((unsigned char)ValueString[Index]))
        ++Index;

    /* Parse the numeric component */
    while (Index < ValueLength && isdigit((unsigned char)ValueString[Index]))
    {
        int Digit = ValueString[Index] - '0';

        if (Value > (ULLONG_MAX - Digit) / 10ULL)
            return 0;

        Value = Value * 10ULL + (ULONGLONG)Digit;
        SawDigit = TRUE;
        ++Index;
    }

    if (!SawDigit)
        return 0;

    /* Skip any whitespace between the number and the optional suffix */
    while (Index < ValueLength && isspace((unsigned char)ValueString[Index]))
        ++Index;

    if (Index < ValueLength)
    {
        char Suffix = (char)toupper((unsigned char)ValueString[Index]);

        switch (Suffix)
        {
            case 'B':
                Multiplier = 1ULL;
                ++Index;
                break;

            case 'K':
                Multiplier = 1024ULL;
                ++Index;
                break;

            case 'M':
                Multiplier = 1024ULL * 1024ULL;
                ++Index;
                break;

            case 'G':
                Multiplier = 1024ULL * 1024ULL * 1024ULL;
                ++Index;
                break;

            case 'T':
                Multiplier = 1024ULL * 1024ULL * 1024ULL * 1024ULL;
                ++Index;
                break;

            default:
                return 0;
        }

        /* Optional trailing 'B' (e.g. "MB", "GiB") */
        if (Index < ValueLength)
        {
            char SecondSuffix = (char)toupper((unsigned char)ValueString[Index]);

            if (SecondSuffix == 'I')
            {
                /* Accept IEC-style suffixes like MiB/GiB */
                ++Index;
                if (Index < ValueLength)
                {
                    SecondSuffix = (char)toupper((unsigned char)ValueString[Index]);
                }
                else
                {
                    SecondSuffix = '\0';
                }
            }

            if (SecondSuffix == 'B')
            {
                ++Index;
            }
        }

        while (Index < ValueLength && isspace((unsigned char)ValueString[Index]))
            ++Index;

        /* Reject unknown trailing characters */
        if (Index < ValueLength)
            return 0;

        if (Multiplier != 1ULL && Value > ULLONG_MAX / Multiplier)
            return 0;

        Value *= Multiplier;
    }

    return Value;
}

ULONGLONG
RamDiskGetRequestedSize(VOID)
{
    return RamDiskRequestedSize;
}

ULONGLONG
RamDiskGetImageLength(VOID)
{
    return RamDiskImageLength;
}

ULONG
RamDiskGetImageOffset(VOID)
{
    return RamDiskImageOffset;
}

ULONGLONG
RamDiskGetVolumeOffset(VOID)
{
    return RamDiskVolumeOffset;
}

#if defined(__GNUC__)
__attribute__((unused))
#endif
static
BOOLEAN
RamDiskReserveWritableBuffer(ULONGLONG RequestedSize, BOOLEAN OptionalRamDisk)
{
    ULONGLONG AllocationSize;
    PVOID Base;
    ULONGLONG AllocationLimit;

    if (RequestedSize == 0)
        return FALSE;

    AllocationSize = ALIGN_UP_BY_ULL(RequestedSize, RAMDISK_ALLOCATION_ALIGNMENT);
    if (AllocationSize == 0 || AllocationSize < RequestedSize)
        return FALSE;

    AllocationLimit = RamDiskWritableAllocationLimit();

    if (AllocationSize > AllocationLimit)
    {
        WARN("Requested ramdisk buffer %llu bytes exceeds low-memory limit %llu bytes\n",
             AllocationSize,
             AllocationLimit);
        if (!RamDiskErrorShown && !OptionalRamDisk)
        {
            UiMessageBox("Requested writable RAM disk size exceeds available low memory.");
            RamDiskErrorShown = TRUE;
        }
        return FALSE;
    }

    if (RamDiskWritableBase &&
        RamDiskWritableSize >= AllocationSize &&
        ((ULONGLONG)(ULONG_PTR)RamDiskWritableBase + AllocationSize) <= AllocationLimit)
        return TRUE;

    if (RamDiskWritableBase)
    {
        RamDiskReleaseMemory(RamDiskWritableBase, RamDiskWritableSize);
        RamDiskWritableBase = NULL;
        RamDiskWritableSize = 0;
    }

    if ((ULONGLONG)(SIZE_T)AllocationSize != AllocationSize)
    {
        WARN("Requested ramdisk size (%llu) exceeds allocator limits\n", AllocationSize);
        return FALSE;
    }

    /*
     * Allocate ramdisk memory as LoaderXIPRom.
     *
     * Using LoaderXIPRom ensures the ramdisk descriptor is uniquely identifiable
     * by the kernel's IopStartRamdisk function. Other allocations use LoaderMemoryData,
     * which can cause descriptor coalescing and misidentification when adjacent
     * LoaderMemoryData regions exist.
     *
     * The kernel's memory manager will mark LoaderXIPRom pages as ROM, preventing
     * reclamation.
     */
    if (OptionalRamDisk)
    {
        Base = MmAllocateHighestMemoryBelowAddressOptional((SIZE_T)AllocationSize,
                                                           (PVOID)(ULONG_PTR)AllocationLimit,
                                                           LoaderXIPRom);
    }
    else
    {
        Base = MmAllocateHighestMemoryBelowAddress((SIZE_T)AllocationSize,
                                                   (PVOID)(ULONG_PTR)AllocationLimit,
                                                   LoaderXIPRom);
    }
    if (!Base)
    {
        WARN("Failed to reserve writable ramdisk buffer (%llu bytes)\n", AllocationSize);
        if (!RamDiskErrorShown && !OptionalRamDisk)
        {
            UiMessageBox("Unable to allocate low-memory buffer for writable RAM disk.");
            RamDiskErrorShown = TRUE;
        }
        return FALSE;
    }

    if (((ULONGLONG)(ULONG_PTR)Base + AllocationSize) > AllocationLimit)
    {
        WARN("Writable ramdisk buffer %p-%p exceeds limit %p\n",
             Base,
             (PVOID)(ULONG_PTR)((ULONG_PTR)Base + (ULONG_PTR)AllocationSize),
             (PVOID)(ULONG_PTR)AllocationLimit);
        RamDiskReleaseMemory(Base, AllocationSize);
        if (!RamDiskErrorShown && !OptionalRamDisk)
        {
            UiMessageBox("Unable to allocate low-memory buffer for writable RAM disk.");
            RamDiskErrorShown = TRUE;
        }
        return FALSE;
    }

    RamDiskWritableBase = Base;
    RamDiskWritableSize = AllocationSize;
    TRACE("Reserved writable ramdisk buffer at %p (%llu bytes)\n", Base, AllocationSize);
    return TRUE;
}

BOOLEAN
RamDiskGetReservedBuffer(
    IN ULONGLONG MinimumSize,
    OUT PVOID *BaseAddress,
    OUT PULONGLONG ActualSize)
{
    if (!BaseAddress || !ActualSize)
        return FALSE;

    if (RamDiskWritableBase && RamDiskWritableSize >= MinimumSize)
    {
        *BaseAddress = RamDiskWritableBase;
        *ActualSize = RamDiskWritableSize;

        RamDiskWritableBase = NULL;
        RamDiskWritableSize = 0;
        return TRUE;
    }

    return FALSE;
}

/*
 * RamDiskGetBackingStore - Get ramdisk backing store information for kernel.
 *
 * Returns the base page and page count of the ramdisk backing store.
 * This information is used by the kernel's memory manager to mark these
 * pages as non-reclaimable (ROM) in the PFN database.
 */
BOOLEAN
RamDiskGetBackingStore(
    OUT PFN_NUMBER *BasePage,
    OUT PFN_NUMBER *PageCount)
{
    ULONGLONG TotalSize;
    PFN_NUMBER Pages;

    if (!BasePage || !PageCount)
        return FALSE;

    /* Check if ramdisk is active */
    if (!RamDiskBase || RamDiskFileSize == 0)
    {
        *BasePage = 0;
        *PageCount = 0;
        return FALSE;
    }

    /* Calculate the base page and page count */
    *BasePage = (PFN_NUMBER)((ULONG_PTR)RamDiskBase >> PAGE_SHIFT);

    /* Calculate total size including image offset for proper page coverage */
    TotalSize = RamDiskFileSize;
    Pages = (PFN_NUMBER)((TotalSize + PAGE_SIZE - 1) >> PAGE_SHIFT);
    *PageCount = Pages;

    TRACE("RamDiskGetBackingStore: Base=%p BasePage=%lu PageCount=%lu Size=%llu\n",
          RamDiskBase, (ULONG)*BasePage, (ULONG)*PageCount, TotalSize);

    return TRUE;
}

/* FUNCTIONS ******************************************************************/

static ULONGLONG RamDiskGetVisibleLength(VOID)
{
    return (RamDiskVolumeLength != 0)
           ? RamDiskVolumeLength
           : (RamDiskImageLength > RamDiskVolumeOffset)
               ? RamDiskImageLength - RamDiskVolumeOffset
               : 0;
}

static ARC_STATUS RamDiskClose(ULONG FileId)
{
    /* Nothing to do */
    return ESUCCESS;
}

static ARC_STATUS RamDiskGetFileInformation(ULONG FileId, FILEINFORMATION* Information)
{
    ULONGLONG VisibleLength;

    RtlZeroMemory(Information, sizeof(*Information));
    VisibleLength = RamDiskGetVisibleLength();

    Information->EndingAddress.QuadPart = VisibleLength;
    Information->CurrentAddress.QuadPart = RamDiskOffset;

    return ESUCCESS;
}

static ARC_STATUS RamDiskOpen(CHAR* Path, OPENMODE OpenMode, ULONG* FileId)
{
    /* Always return success, as contents are already in memory */
    return ESUCCESS;
}

static ARC_STATUS RamDiskRead(ULONG FileId, VOID* Buffer, ULONG N, ULONG* Count)
{
    PVOID StartAddress;
    ULONGLONG VisibleLength;

    /* Don't allow reads past our image */
    VisibleLength = RamDiskGetVisibleLength();

    if ((RamDiskOffset >= VisibleLength) || (RamDiskOffset + N > VisibleLength))
    {
        *Count = 0;
        return EIO;
    }

    /* Get actual pointer without truncating offsets on 32-bit builds. */
    {
        ULONGLONG TotalOffset = RamDiskVolumeOffset + RamDiskOffset;
        ULONG_PTR BaseAddress = (ULONG_PTR)RamDiskBase;
        ULONGLONG MaxOffset = ((ULONGLONG)~(ULONG_PTR)0);

        if (TotalOffset > (MaxOffset - (ULONGLONG)BaseAddress))
        {
            WARN("RamDiskRead: offset overflow (total=%I64u base=%p)\n", TotalOffset, RamDiskBase);
            *Count = 0;
            return EIO;
        }

        StartAddress = (PVOID)(BaseAddress + (ULONG_PTR)TotalOffset);
    }

    /* Do the read */
    RtlCopyMemory(Buffer, StartAddress, N);
    RamDiskOffset += N;
    *Count = N;

    return ESUCCESS;
}

static ARC_STATUS RamDiskSeek(ULONG FileId, LARGE_INTEGER* Position, SEEKMODE SeekMode)
{
    LARGE_INTEGER NewPosition = *Position;
    ULONGLONG VisibleLength;

    switch (SeekMode)
    {
        case SeekAbsolute:
            break;
        case SeekRelative:
            NewPosition.QuadPart += RamDiskOffset;
            break;
        default:
            ASSERT(FALSE);
            return EINVAL;
    }

    VisibleLength = RamDiskGetVisibleLength();

    if (NewPosition.QuadPart > VisibleLength)
        return EINVAL;

    RamDiskOffset = NewPosition.QuadPart;
    return ESUCCESS;
}

static const DEVVTBL RamDiskVtbl =
{
    RamDiskClose,
    RamDiskGetFileInformation,
    RamDiskOpen,
    RamDiskRead,
    RamDiskSeek,
};

static ARC_STATUS
RamDiskLoadVirtualFile(
    IN PCSTR FileName,
    IN PCSTR DefaultPath OPTIONAL,
    IN BOOLEAN OptionalRamDisk)
{
    ARC_STATUS Status;
    ULONG RamFileId;
    ULONG ChunkSize, Count;
    ULONGLONG TotalRead;
    ULONG LastPercent;
    FILEINFORMATION Information;
    LARGE_INTEGER Position;

    /* Display progress */
#ifdef UEFIBOOT
    if (!UefiVideoIsBootLogoDrawn())
    {
        UiDrawProgressBarCenter("Loading RamDisk...");
    }
#else
    UiDrawProgressBarCenter("Loading RamDisk...");
#endif

    /*
     * If the firmware or a previous boot stage already provided the ramdisk
     * image in memory, skip the expensive readback and reuse the cached data.
     */
    if (gInitRamDiskBase && gInitRamDiskSize != 0)
    {
        BOOLEAN UseResidentImage = FALSE;
        ULONGLONG ResidentSize = (ULONGLONG)gInitRamDiskSize;

        if ((ULONGLONG)RamDiskImageOffset < ResidentSize)
        {
            ULONGLONG Available = ResidentSize - (ULONGLONG)RamDiskImageOffset;
            ULONGLONG Required = (RamDiskImageLength != 0)
                                  ? RamDiskImageLength
                                  : Available;

            if (Required <= Available)
                UseResidentImage = TRUE;
            else
                WARN("RamDiskLoadVirtualFile: resident image too small (offset=%lu required=%llu available=%llu)\n",
                     (ULONG)RamDiskImageOffset,
                     Required,
                     Available);
        }

        if (UseResidentImage)
        {
            RamDiskBase = gInitRamDiskBase;
            RamDiskFileSize = ResidentSize;
            RamDiskImageOffset = 0;
            RamDiskImageLength = RamDiskFileSize;
            RamDiskResetVisibleRegion();
            UiUpdateProgressBar(100, NULL);
            TRACE("RamDiskLoadVirtualFile: using resident ramdisk image (%llu bytes)\n",
                  ResidentSize);
            return ESUCCESS;
        }
    }

    /* Try opening the Ramdisk file */
    Status = FsOpenFile(FileName, DefaultPath, OpenReadOnly, &RamFileId);
    if (Status != ESUCCESS)
        return Status;

    /* Get the file size */
    Status = ArcGetFileInformation(RamFileId, &Information);
    if (Status != ESUCCESS)
    {
        ArcClose(RamFileId);
        return Status;
    }

    /* Enforce the legacy 4GB limit on 32-bit builds */
#if !defined(_M_AMD64) && !defined(__x86_64__)
    if (Information.EndingAddress.HighPart != 0)
    {
        ArcClose(RamFileId);
        if (!OptionalRamDisk)
            UiMessageBox("RAM disk too big.");
        return ENOMEM;
    }
#endif

    RamDiskFileSize = Information.EndingAddress.QuadPart;
#if !defined(_M_AMD64) && !defined(__x86_64__)
    ASSERT(RamDiskFileSize < 0x100000000); // Legacy limit on 32-bit builds.
#endif

    /* Allocate memory for it */
    ChunkSize = 8 * 1024 * 1024;
    if (DiskReadBufferSize != 0 && DiskReadBufferSize <= ULONG_MAX)
    {
        ULONG PreferredChunk = (ULONG)DiskReadBufferSize;
        if (PreferredChunk > ChunkSize)
            ChunkSize = PreferredChunk;
    }

    if (RamDiskFileSize < ChunkSize && RamDiskFileSize <= ULONG_MAX)
        ChunkSize = (ULONG)RamDiskFileSize;

    ChunkSize &= ~(ISO_SECTOR_SIZE - 1);
    if (ChunkSize == 0)
        ChunkSize = ISO_SECTOR_SIZE;

#if defined(_M_AMD64) || defined(__x86_64__)
    /* Use LoaderXIPRom for unique identification by IopStartRamdisk */
    if (OptionalRamDisk)
        RamDiskBase = MmAllocateMemoryWithTypeOptional(RamDiskFileSize, LoaderXIPRom);
    else
        RamDiskBase = MmAllocateMemoryWithType(RamDiskFileSize, LoaderXIPRom);
    if (!RamDiskBase)
    {
        RamDiskFileSize = 0;
        ArcClose(RamFileId);
        if (!OptionalRamDisk)
            UiMessageBox("Failed to allocate memory for RAM disk.");
        return ENOMEM;
    }
#else
    {
        ULONGLONG AllocationLimit = RamDiskWritableAllocationLimit();

        if (RamDiskFileSize > AllocationLimit)
        {
            RamDiskFileSize = 0;
            ArcClose(RamFileId);
            if (!OptionalRamDisk)
                UiMessageBox("RAM disk image is larger than available low memory.");
            return ENOMEM;
        }

        /* Use LoaderXIPRom for unique identification by IopStartRamdisk */
        if (OptionalRamDisk)
        {
            RamDiskBase = MmAllocateHighestMemoryBelowAddressOptional((SIZE_T)RamDiskFileSize,
                                                                      (PVOID)(ULONG_PTR)AllocationLimit,
                                                                      LoaderXIPRom);
        }
        else
        {
            RamDiskBase = MmAllocateHighestMemoryBelowAddress((SIZE_T)RamDiskFileSize,
                                                              (PVOID)(ULONG_PTR)AllocationLimit,
                                                              LoaderXIPRom);
        }
        if (!RamDiskBase)
        {
            RamDiskFileSize = 0;
            ArcClose(RamFileId);
            if (!OptionalRamDisk)
                UiMessageBox("Failed to allocate low memory for RAM disk.");
            return ENOMEM;
        }
    }
#endif

    Position.QuadPart = 0;
    Status = ArcSeek(RamFileId, &Position, SeekAbsolute);
    if (Status != ESUCCESS)
    {
        RamDiskReleaseMemory(RamDiskBase, RamDiskFileSize);
        RamDiskBase = NULL;
        RamDiskFileSize = 0;
        ArcClose(RamFileId);
        if (!OptionalRamDisk)
            UiMessageBox("Failed to read RAM disk.");
        return Status;
    }

    /*
     * Read it in chunks
     */
    LastPercent = 0;
    for (TotalRead = 0; TotalRead < RamDiskFileSize; )
    {
        ULONG CurrentChunk = ChunkSize;

        /* Check if we're at the last chunk */
        if ((RamDiskFileSize - TotalRead) < CurrentChunk)
        {
            /* Only need the actual data required */
            CurrentChunk = (ULONG)(RamDiskFileSize - TotalRead);
        }

        if (CurrentChunk == 0)
            break;

        /* Update progress no more than once per percent change */
        if (RamDiskFileSize != 0)
        {
            ULONGLONG Completed = TotalRead + CurrentChunk;
            ULONG NewPercent = (ULONG)((Completed * 100ULL) / RamDiskFileSize);
            if (NewPercent > 100)
                NewPercent = 100;

            if ((NewPercent >= LastPercent + 1) ||
                (NewPercent == 100 && NewPercent != LastPercent))
            {
                UiUpdateProgressBar(NewPercent, NULL);
                LastPercent = NewPercent;
            }
        }

        /* Copy the contents */
        Status = ArcRead(RamFileId,
                         (PVOID)((ULONG_PTR)RamDiskBase + (ULONG_PTR)TotalRead),
                         CurrentChunk,
                         &Count);

        /* Check for success */
        if ((Status != ESUCCESS) || (Count != CurrentChunk))
        {
            RamDiskReleaseMemory(RamDiskBase, RamDiskFileSize);
            RamDiskBase = NULL;
            RamDiskFileSize = 0;
            ArcClose(RamFileId);
            if (!OptionalRamDisk)
                UiMessageBox("Failed to read RAM disk.");
            return ((Status != ESUCCESS) ? Status : EIO);
        }

        TotalRead += CurrentChunk;
    }

    if (LastPercent < 100)
        UiUpdateProgressBar(100, NULL);

    ArcClose(RamFileId);

    return ESUCCESS;
}

ARC_STATUS
RamDiskInitialize(
    IN BOOLEAN InitRamDisk,
    IN PCSTR LoadOptions OPTIONAL,
    IN PCSTR DefaultPath OPTIONAL)
{
    RamDiskErrorShown = FALSE;

    TRACE("RamDiskInitialize: Begin (Init=%s)\n", InitRamDisk ? "true" : "false");

#ifdef UEFIBOOT
    /* Display BGRT splash once right at the start of ramdisk init. */
    static BOOLEAN BgrtLogoShown = FALSE;
    TRACE("RamDiskInitialize: BGRT logo shown=%s\n", BgrtLogoShown ? "yes" : "no");
    if (!BgrtLogoShown)
    {
        BOOLEAN LogoResult = UefiVideoDisplayBootLogo();
        TRACE("RamDiskInitialize: BGRT logo request result=%s\n",
              LogoResult ? "success" : "failure");
        if (LogoResult)
            BgrtLogoShown = TRUE;
    }
#endif

    /* Reset the RAMDISK device */
    if (RamDiskBase && RamDiskBase != gInitRamDiskBase)
    {
        /* This is not the initial Ramdisk, so we can free the allocated memory */
        RamDiskReleaseMemory(RamDiskBase, RamDiskFileSize);
    }
    RamDiskBase = NULL;
    RamDiskFileSize = 0;
    RamDiskImageLength = 0;
    RamDiskImageOffset = 0;
    RamDiskOffset = 0;
    RamDiskRequestedSize = 0;
    RamDiskVolumeOffset = 0;
    RamDiskVolumeLength = 0;

    if (InitRamDisk)
    {
        /* We initialize the initial Ramdisk: it should be present in memory */
        if (!gInitRamDiskBase || gInitRamDiskSize == 0)
            return ENODEV;

        // TODO: Handle SDI image.

        RamDiskBase = gInitRamDiskBase;
        RamDiskFileSize = gInitRamDiskSize;
        ASSERT(RamDiskFileSize < 0x100000000); // See FIXME about 4GB support in RamDiskLoadVirtualFile().

        if ((ULONGLONG)RamDiskImageOffset >= RamDiskFileSize)
            RamDiskImageOffset = 0;

        if (RamDiskImageLength == 0 ||
            RamDiskImageLength > RamDiskFileSize - RamDiskImageOffset)
        {
            RamDiskImageLength = RamDiskFileSize - RamDiskImageOffset;
        }

        RamDiskResetVisibleRegion();
    }
    else
    {
        /* We initialize the Ramdisk from the load options */
        ARC_STATUS Status;
        CHAR FileName[MAX_PATH] = "";
        PVOID OriginalBase;

        /* If we don't have any load options, initialize an empty Ramdisk */
        if (LoadOptions)
        {
            PCSTR Option;
            ULONG FileNameLength;
            ULONG OptionLength;

            Option = NtLdrGetOptionEx(LoadOptions, "RDRAMSIZE=", &OptionLength);
            if (Option && OptionLength > (sizeof("RDRAMSIZE=") - 1))
            {
                ULONGLONG ParsedSize;

                ParsedSize = RamDiskParseSizeString(
                                Option + (sizeof("RDRAMSIZE=") - 1),
                                OptionLength - (sizeof("RDRAMSIZE=") - 1));
                if (ParsedSize != 0)
                {
                    RamDiskRequestedSize = ParsedSize;
                    TRACE("Requested writable ramdisk size: %llu bytes\n",
                          RamDiskRequestedSize);
                }
                else
                {
                    WARN("Ignoring invalid RDRAMSIZE option value\n");
                }
            }

            /* Ramdisk image file name */
            Option = NtLdrGetOptionEx(LoadOptions, "RDPATH=", &FileNameLength);
            if (Option && (FileNameLength > 7))
            {
                /* Copy the file name */
                Option += 7; FileNameLength -= 7;
                RtlStringCbCopyNA(FileName, sizeof(FileName),
                                  Option, FileNameLength * sizeof(CHAR));
            }

            /* Ramdisk image length */
            Option = NtLdrGetOption(LoadOptions, "RDIMAGELENGTH=");
            if (Option)
            {
                RamDiskImageLength = _atoi64(Option + 14);
            }

            /* Ramdisk image offset */
            Option = NtLdrGetOption(LoadOptions, "RDIMAGEOFFSET=");
            if (Option)
            {
                RamDiskImageOffset = atol(Option + 14);
            }
        }

        BOOLEAN StreamingSucceeded = FALSE;
        ULONGLONG StreamIsoSize = 0;
        BOOLEAN OptionalRamDisk = (RamDiskRequestedSize != 0 && !*FileName);

        if (RamDiskRequestedSize != 0)
        {
            ISO_SOURCE StreamSource;
            PVOID WritableBase;
            ULONGLONG WritableSize;
            ARC_STATUS StreamStatus;
            PCSTR StreamFileName = NULL;
            PCSTR StreamDefaultPath = NULL;

            if (*FileName)
            {
                StreamFileName = FileName;
                StreamDefaultPath = DefaultPath;
            }
            else if (DefaultPath)
            {
                StreamFileName = DefaultPath;
                StreamDefaultPath = NULL;
            }

            if (StreamFileName)
            {
                StreamStatus = RamDiskOpenIsoSource(StreamFileName,
                                                    StreamDefaultPath,
                                                    RamDiskImageOffset,
                                                    RamDiskImageLength,
                                                    &StreamSource);
                if (StreamStatus != ESUCCESS)
                {
                    /* BIOS/legacy fallback: if opening by file/path failed (e.g. DefaultPath
                     * is 'ramdisk(0)' or not a real ISO path), try raw firmware CD devices. */
                    static const PCSTR CdCandidates[] = { "cdrom(0)", "cdrom(1)", NULL };
                    for (int i = 0; CdCandidates[i]; ++i)
                    {
                        StreamStatus = RamDiskOpenIsoSource(CdCandidates[i],
                                                            NULL,
                                                            0, /* ISO starts at LBA 0 */
                                                            0,
                                                            &StreamSource);
                        if (StreamStatus == ESUCCESS)
                            break;
                    }
                }

                if (StreamStatus == ESUCCESS)
                {
                    StreamIsoSize = StreamSource.Size;
                    {
                        ULONGLONG ExtraBytes = RamDiskRequestedSize;
                        if (ExtraBytes < (64ULL * 1024ULL * 1024ULL))
                            ExtraBytes = (64ULL * 1024ULL * 1024ULL);
                        ULONGLONG TotalTarget = StreamIsoSize + ExtraBytes;

                        if (RamDiskBuildWritableImage(&StreamSource,
                                                       TotalTarget,
                                                       &WritableBase,
                                                       &WritableSize,
                                                       OptionalRamDisk))
                        {
                            RamDiskCloseIsoSource(&StreamSource);
                            RamDiskBase = WritableBase;
                            RamDiskFileSize = WritableSize;
                            RamDiskImageOffset = 0;
                            RamDiskImageLength = WritableSize;
                            StreamingSucceeded = TRUE;
                            TRACE("RamDiskInitialize: writable ramdisk ready from streaming (%llu bytes, ISO=%llu extra=%llu)\n",
                                  RamDiskFileSize, StreamIsoSize, ExtraBytes);
                        }
                        else
                        {
                            /* Try to adaptively shrink the extra overlay to fit the low-memory budget. */
                            ULONGLONG AllocationLimit = RamDiskWritableAllocationLimit();
                            ULONGLONG MaxExtra = (AllocationLimit > StreamIsoSize)
                                                   ? (AllocationLimit - StreamIsoSize)
                                                   : 0;
                            if (MaxExtra >= (64ULL * 1024ULL * 1024ULL) && MaxExtra < ExtraBytes)
                            {
                                ULONGLONG ShrunkTotal = StreamIsoSize + MaxExtra;
                                TRACE("RamDiskInitialize: streaming expansion failed; retrying with shrunk extra %llu (total %llu)\n",
                                      MaxExtra, ShrunkTotal);

                                if (RamDiskBuildWritableImage(&StreamSource,
                                                               ShrunkTotal,
                                                               &WritableBase,
                                                               &WritableSize,
                                                               OptionalRamDisk))
                                {
                                    RamDiskCloseIsoSource(&StreamSource);
                                    RamDiskBase = WritableBase;
                                    RamDiskFileSize = WritableSize;
                                    RamDiskImageOffset = 0;
                                    RamDiskImageLength = WritableSize;
                                    /* Record the effective requested size so boot path gets retargeted. */
                                    RamDiskRequestedSize = MaxExtra;
                                    StreamingSucceeded = TRUE;
                                    TRACE("RamDiskInitialize: writable ramdisk ready from streaming after shrink (%llu bytes, ISO=%llu extra=%llu)\n",
                                          RamDiskFileSize, StreamIsoSize, MaxExtra);
                                }
                                else
                                {
                                    RamDiskCloseIsoSource(&StreamSource);
                                    TRACE("RamDiskInitialize: streaming expansion failed even after shrink; falling back to in-memory copy\n");
                                }
                            }
                            else
                            {
                                RamDiskCloseIsoSource(&StreamSource);
                                TRACE("RamDiskInitialize: streaming writable expansion failed, falling back to in-memory copy\n");
                            }
                        }
                    }
                }
            }

            if (StreamingSucceeded)
                goto WritableReady;

            if (RamDiskRequestedSize != 0 && StreamIsoSize != 0)
            {
                ULONGLONG IsoSize = StreamIsoSize;
                ULONGLONG ResidentIsoBytes;
                ULONGLONG AllocationLimit = RamDiskWritableAllocationLimit();
                ULONGLONG ExtraBytes = RamDiskRequestedSize;
                ULONGLONG RequiredSize;

                /* New semantics: RDRAMSIZE denotes extra writable bytes
                   beyond the ISO contents. Enforce a minimum of 64 MiB
                   headroom if the request is smaller. */
                if (ExtraBytes < (64ULL * 1024ULL * 1024ULL))
                    ExtraBytes = (64ULL * 1024ULL * 1024ULL);

                RequiredSize = IsoSize + ExtraBytes;

                if (RequiredSize > AllocationLimit)
                {
                    WARN("RamDiskInitialize: writable overlay request (%llu) exceeds low-memory limit before staging ISO (%llu)\n",
                         RequiredSize,
                         (ULONGLONG)RAMDISK_LOW_ALLOC_MAX);
                    if (!RamDiskErrorShown && !OptionalRamDisk)
                    {
                        UiMessageBox("Writable RAM disk request exceeds available low memory. Continuing with read-only media.");
                        RamDiskErrorShown = TRUE;
                    }
                    RamDiskRequestedSize = 0;
                }
                else
                {
                    ResidentIsoBytes = ALIGN_UP_BY_ULL(IsoSize, RAMDISK_ALLOCATION_ALIGNMENT);

                    if (ResidentIsoBytes > AllocationLimit ||
                        RequiredSize > AllocationLimit - ResidentIsoBytes)
                    {
                        WARN("RamDiskInitialize: ISO (%llu) + writable extra (%llu) would exceed low-memory budget %llu\n",
                             IsoSize,
                             ExtraBytes,
                             (ULONGLONG)RAMDISK_LOW_ALLOC_MAX);
                        if (!RamDiskErrorShown && !OptionalRamDisk)
                        {
                            UiMessageBox("Writable RAM disk request leaves insufficient low memory once the ISO is cached. Continuing with read-only media.");
                            RamDiskErrorShown = TRUE;
                        }
                        RamDiskRequestedSize = 0;
                    }
                }
            }
        }

        if (*FileName)
            Status = RamDiskLoadVirtualFile(FileName, DefaultPath, OptionalRamDisk);
        else
            Status = RamDiskLoadVirtualFile(DefaultPath, NULL, OptionalRamDisk);
        if (Status != ESUCCESS)
            return Status;

        OriginalBase = RamDiskBase;
            if (RamDiskRequestedSize != 0)
            {
            TRACE("RamDiskInitialize: expanding to writable RAMFS (extra %llu bytes requested)\n",
                  RamDiskRequestedSize);
                PVOID WritableBase;
                ULONGLONG WritableSize;
                PVOID IsoImageBase;
                ULONGLONG IsoImageLength;
                ISO_SOURCE MemorySource;

            IsoImageBase = (PVOID)((ULONG_PTR)OriginalBase + RamDiskImageOffset);
            IsoImageLength = RamDiskFileSize - RamDiskImageOffset;

            MemorySource.MemoryBase = IsoImageBase;
            MemorySource.Size = (RamDiskImageLength != 0 &&
                                 RamDiskImageLength <= IsoImageLength)
                                ? RamDiskImageLength
                                : IsoImageLength;
            MemorySource.ArcFileId = INVALID_FILE_ID;
            MemorySource.ArcOffset = 0;
            MemorySource.ArcPosition = 0;

            /* New semantics: total target = ISO length + extra (>=64MiB) */
            {
                ULONGLONG ExtraBytes = RamDiskRequestedSize;
                if (ExtraBytes < (64ULL * 1024ULL * 1024ULL))
                    ExtraBytes = (64ULL * 1024ULL * 1024ULL);
                ULONGLONG TotalTarget = MemorySource.Size + ExtraBytes;

                if (!RamDiskBuildWritableImage(&MemorySource,
                                               TotalTarget,
                                               &WritableBase,
                                               &WritableSize,
                                               OptionalRamDisk))
                {
                    if (!RamDiskErrorShown && !OptionalRamDisk)
                    {
                        UiMessageBox("Failed to expand LiveCD into writable RAM.");
                        RamDiskErrorShown = TRUE;
                    }
                    RamDiskRequestedSize = 0;
                    TRACE("RamDiskInitialize: continuing with read-only copy because writable buffer allocation failed\n");
                    RamDiskBase = OriginalBase;
                    /* Ensure RamDiskImageLength is set so the visible region covers the loaded data */
                    if (!RamDiskImageLength)
                        RamDiskImageLength = RamDiskFileSize - RamDiskImageOffset;
                    RamDiskVolumeOffset = 0;
                    RamDiskVolumeLength = 0;
                    goto WritableFallback;
                }
            }

            if ((OriginalBase != gInitRamDiskBase) &&
                (OriginalBase != WritableBase))
            {
                RamDiskReleaseMemory(OriginalBase, RamDiskFileSize);
            }

            RamDiskBase = WritableBase;
            RamDiskFileSize = WritableSize;
            RamDiskImageOffset = 0;
            RamDiskImageLength = WritableSize;
            TRACE("RamDiskInitialize: writable ramdisk ready (%llu bytes)\n",
                  RamDiskFileSize);
        }
    }

WritableReady:
    /* Adjust the Ramdisk image length if needed */
    if (!RamDiskImageLength || (RamDiskImageLength > RamDiskFileSize - RamDiskImageOffset))
        RamDiskImageLength = RamDiskFileSize - RamDiskImageOffset;

WritableFallback:

    /* Ensure a fresh filesystem mount the next time ramdisk(0) is accessed. */
    if (RamDiskVolumeLength == 0)
    {
        RamDiskResetVisibleRegion();
    }
    /* Changing the exposed LBA window invalidates any cached FAT mount state. */
    RamDiskInvalidateFatCache();

    RamDiskRegisterArcDevice();

    /* Register the RAMDISK device */
    if (!RamDiskDeviceRegistered)
    {
        FsRegisterDevice("ramdisk(0)", &RamDiskVtbl);
        RamDiskDeviceRegistered = TRUE;
    }

#if DBG
    if (RamDiskBase && RamDiskFileSize != 0)
    {
        PFN_NUMBER BasePfn = RamDiskPointerToPfn(RamDiskBase);
        PFN_NUMBER EndPfn = RamDiskPointerToPfn((const VOID *)((ULONG_PTR)RamDiskBase + (ULONG_PTR)(RamDiskFileSize - 1)));
        TRACE("RamDiskInitialize: base=%p size=%llu basePFN=%lx endPFN=%lx imageOffset=%lu imageLength=%llu volumeOffset=%llu volumeLength=%llu requested=%llu\n",
              RamDiskBase,
              RamDiskFileSize,
              BasePfn,
              EndPfn,
              RamDiskImageOffset,
              RamDiskImageLength,
              RamDiskVolumeOffset,
              RamDiskVolumeLength,
              RamDiskRequestedSize);
        RamDiskTraceSample("  disk[0]", RamDiskBase);
        if (RamDiskImageOffset < RamDiskFileSize)
        {
            RamDiskTraceSample("  disk[image]", (PUCHAR)RamDiskBase + RamDiskImageOffset);
        }
        if (RamDiskVolumeOffset < RamDiskFileSize)
        {
            RamDiskTraceSample("  disk[volume]", (PUCHAR)RamDiskBase + RamDiskVolumeOffset);
        }
    }
#endif

    return ESUCCESS;
}
