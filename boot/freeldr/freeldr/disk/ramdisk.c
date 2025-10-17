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
#include "../ntldr/ntldropts.h"

DBG_DEFAULT_CHANNEL(DISK);

#define RAMDISK_ALLOCATION_ALIGNMENT 0x1000ULL
#define ALIGN_UP_BY_ULL(Value, Alignment) \
    (((Value) + ((Alignment) - 1ULL)) & ~((Alignment) - 1ULL))

/* GLOBALS ********************************************************************/

PVOID gInitRamDiskBase = NULL;
ULONG gInitRamDiskSize = 0;

static BOOLEAN   RamDiskDeviceRegistered = FALSE;
static PVOID     RamDiskBase;
static ULONGLONG RamDiskFileSize;    // FIXME: RAM disks currently limited to 4GB.
static ULONGLONG RamDiskImageLength; // Size of valid data in the Ramdisk (usually == RamDiskFileSize - RamDiskImageOffset)
static ULONG     RamDiskImageOffset; // Starting offset from the Ramdisk base.
static ULONGLONG RamDiskOffset;      // Current position in the Ramdisk.
static ULONGLONG RamDiskRequestedSize = 0;
static PVOID     RamDiskWritableBase = NULL;
static ULONGLONG RamDiskWritableSize = 0;

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

#if defined(__GNUC__)
__attribute__((unused))
#endif
static
BOOLEAN
RamDiskReserveWritableBuffer(ULONGLONG RequestedSize)
{
    ULONGLONG AllocationSize;
    PVOID Base;

    if (RequestedSize == 0)
        return FALSE;

    AllocationSize = ALIGN_UP_BY_ULL(RequestedSize, RAMDISK_ALLOCATION_ALIGNMENT);
    if (AllocationSize == 0 || AllocationSize < RequestedSize)
        return FALSE;

    if (RamDiskWritableBase && RamDiskWritableSize >= AllocationSize)
        return TRUE;

    if (RamDiskWritableBase)
    {
        MmFreeMemory(RamDiskWritableBase);
        RamDiskWritableBase = NULL;
        RamDiskWritableSize = 0;
    }

    if ((ULONGLONG)(SIZE_T)AllocationSize != AllocationSize)
    {
        WARN("Requested ramdisk size (%llu) exceeds allocator limits\n", AllocationSize);
        return FALSE;
    }

    Base = MmAllocateMemoryWithType((SIZE_T)AllocationSize, LoaderXIPRom);
    if (!Base)
    {
        WARN("Failed to reserve writable ramdisk buffer (%llu bytes)\n", AllocationSize);
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

/* FUNCTIONS ******************************************************************/

static ARC_STATUS RamDiskClose(ULONG FileId)
{
    /* Nothing to do */
    return ESUCCESS;
}

static ARC_STATUS RamDiskGetFileInformation(ULONG FileId, FILEINFORMATION* Information)
{
    RtlZeroMemory(Information, sizeof(*Information));
    Information->EndingAddress.QuadPart = RamDiskImageLength;
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

    /* Don't allow reads past our image */
    if (RamDiskOffset >= RamDiskImageLength || RamDiskOffset + N > RamDiskImageLength)
    {
        *Count = 0;
        return EIO;
    }
    // N = min(N, RamdiskImageLength - RamDiskOffset);

    /* Get actual pointer */
    StartAddress = (PVOID)((ULONG_PTR)RamDiskBase + RamDiskImageOffset + (ULONG_PTR)RamDiskOffset);

    /* Do the read */
    RtlCopyMemory(Buffer, StartAddress, N);
    RamDiskOffset += N;
    *Count = N;

    return ESUCCESS;
}

static ARC_STATUS RamDiskSeek(ULONG FileId, LARGE_INTEGER* Position, SEEKMODE SeekMode)
{
    LARGE_INTEGER NewPosition = *Position;

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

    if (NewPosition.QuadPart >= RamDiskImageLength)
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
    IN PCSTR DefaultPath OPTIONAL)
{
    ARC_STATUS Status;
    ULONG RamFileId;
    ULONG ChunkSize, Count;
    ULONGLONG TotalRead;
    ULONG PercentPerChunk, Percent;
    FILEINFORMATION Information;
    LARGE_INTEGER Position;

    /* Display progress */
    UiDrawProgressBarCenter("Loading RamDisk...");

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

    /* FIXME: For now, limit RAM disks to 4GB */
    if (Information.EndingAddress.HighPart != 0)
    {
        ArcClose(RamFileId);
        UiMessageBox("RAM disk too big.");
        return ENOMEM;
    }
    RamDiskFileSize = Information.EndingAddress.QuadPart;
    ASSERT(RamDiskFileSize < 0x100000000); // See FIXME above.

    /* Allocate memory for it */
    ChunkSize = 8 * 1024 * 1024;
    if (RamDiskFileSize < ChunkSize)
        PercentPerChunk = 0;
    else
        PercentPerChunk = 100 * ChunkSize / RamDiskFileSize;
    RamDiskBase = MmAllocateMemoryWithType(RamDiskFileSize, LoaderXIPRom);
    if (!RamDiskBase)
    {
        RamDiskFileSize = 0;
        ArcClose(RamFileId);
        UiMessageBox("Failed to allocate memory for RAM disk.");
        return ENOMEM;
    }

    /*
     * Read it in chunks
     */
    Percent = 0;
    for (TotalRead = 0; TotalRead < RamDiskFileSize; TotalRead += ChunkSize)
    {
        /* Check if we're at the last chunk */
        if ((RamDiskFileSize - TotalRead) < ChunkSize)
        {
            /* Only need the actual data required */
            ChunkSize = (ULONG)(RamDiskFileSize - TotalRead);
        }

        /* Update progress */
        UiUpdateProgressBar(Percent, NULL);
        Percent += PercentPerChunk;

        /* Copy the contents */
        Position.QuadPart = TotalRead;
        Status = ArcSeek(RamFileId, &Position, SeekAbsolute);
        if (Status == ESUCCESS)
        {
            Status = ArcRead(RamFileId,
                             (PVOID)((ULONG_PTR)RamDiskBase + (ULONG_PTR)TotalRead),
                             ChunkSize,
                             &Count);
        }

        /* Check for success */
        if ((Status != ESUCCESS) || (Count != ChunkSize))
        {
            MmFreeMemory(RamDiskBase);
            RamDiskBase = NULL;
            RamDiskFileSize = 0;
            ArcClose(RamFileId);
            UiMessageBox("Failed to read RAM disk.");
            return ((Status != ESUCCESS) ? Status : EIO);
        }
    }
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
    /* Reset the RAMDISK device */
    if ((RamDiskBase != gInitRamDiskBase) &&
        (RamDiskFileSize != gInitRamDiskSize) &&
        (gInitRamDiskSize != 0))
    {
        /* This is not the initial Ramdisk, so we can free the allocated memory */
        MmFreeMemory(RamDiskBase);
    }
    RamDiskBase = NULL;
    RamDiskFileSize = 0;
    RamDiskImageLength = 0;
    RamDiskImageOffset = 0;
    RamDiskOffset = 0;
    RamDiskRequestedSize = 0;
    RamDiskRequestedSize = 0;

    if (InitRamDisk)
    {
        /* We initialize the initial Ramdisk: it should be present in memory */
        if (!gInitRamDiskBase || gInitRamDiskSize == 0)
            return ENODEV;

        // TODO: Handle SDI image.

        RamDiskBase = gInitRamDiskBase;
        RamDiskFileSize = gInitRamDiskSize;
        ASSERT(RamDiskFileSize < 0x100000000); // See FIXME about 4GB support in RamDiskLoadVirtualFile().
    }
    else
    {
        /* We initialize the Ramdisk from the load options */
        ARC_STATUS Status;
        CHAR FileName[MAX_PATH] = "";

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

        if (*FileName)
            Status = RamDiskLoadVirtualFile(FileName, DefaultPath);
        else
            Status = RamDiskLoadVirtualFile(DefaultPath, NULL);
        if (Status != ESUCCESS)
            return Status;
    }

    /* Adjust the Ramdisk image length if needed */
    if (!RamDiskImageLength || (RamDiskImageLength > RamDiskFileSize - RamDiskImageOffset))
        RamDiskImageLength = RamDiskFileSize - RamDiskImageOffset;

    /* Register the RAMDISK device */
    if (!RamDiskDeviceRegistered)
    {
        FsRegisterDevice("ramdisk(0)", &RamDiskVtbl);
        RamDiskDeviceRegistered = TRUE;
    }

    return ESUCCESS;
}
