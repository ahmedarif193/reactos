/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Disk access through Open Firmware block devices
 */

#include <freeldr.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(DISK);

#define FIRST_BIOS_DISK   0x80
#define OFW_MAX_DISKS     8
#define OFW_PATH_LENGTH   128
#define OFW_MAX_TREE_WALK 256

typedef struct _OFW_DISK
{
    OFW_HANDLE Ihandle;
    ULONG BlockSize;
    ULONGLONG BlockCount;
    BOOLEAN IsCdrom;
    CHAR Path[OFW_PATH_LENGTH];
} OFW_DISK, *POFW_DISK;

typedef struct tagDISKCONTEXT
{
    UCHAR DriveNumber;
    ULONG SectorSize;
    ULONGLONG SectorOffset;
    ULONGLONG SectorCount;
    ULONGLONG SectorNumber;
} DISKCONTEXT;

static OFW_DISK OfwDisks[OFW_MAX_DISKS];
static ULONG OfwDiskCount;

UCHAR FrldrBootDrive;
ULONG FrldrBootPartition;
PVOID DiskReadBuffer;
SIZE_T DiskReadBufferSize;

static
POFW_DISK
OfwGetDisk(_In_ UCHAR DriveNumber)
{
    if (DriveNumber < FIRST_BIOS_DISK || (ULONG)(DriveNumber - FIRST_BIOS_DISK) >= OfwDiskCount)
        return NULL;
    return &OfwDisks[DriveNumber - FIRST_BIOS_DISK];
}

BOOLEAN
OfwDiskReadLogicalSectors(UCHAR DriveNumber, ULONGLONG SectorNumber, ULONG SectorCount, PVOID Buffer)
{
    POFW_DISK Disk = OfwGetDisk(DriveNumber);
    ULONG Length;

    if (!Disk)
        return FALSE;

    Length = SectorCount * Disk->BlockSize;
    if (OfwSeek(Disk->Ihandle, SectorNumber * Disk->BlockSize) < 0)
    {
        TRACE("OFW seek failed on drive 0x%x sector %I64u\n", DriveNumber, SectorNumber);
        return FALSE;
    }
    if (OfwRead(Disk->Ihandle, Buffer, Length) != (LONG)Length)
    {
        TRACE("OFW read failed on drive 0x%x sector %I64u count %lu\n", DriveNumber, SectorNumber, SectorCount);
        return FALSE;
    }
    return TRUE;
}

BOOLEAN
OfwDiskGetDriveGeometry(UCHAR DriveNumber, PGEOMETRY Geometry)
{
    POFW_DISK Disk = OfwGetDisk(DriveNumber);

    if (!Disk)
        return FALSE;

    Geometry->Cylinders = (ULONG)(Disk->BlockCount / (255 * 63));
    Geometry->Heads = 255;
    Geometry->SectorsPerTrack = 63;
    Geometry->BytesPerSector = Disk->BlockSize;
    Geometry->Sectors = Disk->BlockCount;
    return TRUE;
}

ULONG
OfwDiskGetCacheableBlockCount(UCHAR DriveNumber)
{
    UNREFERENCED_PARAMETER(DriveNumber);
    return 64;
}

UCHAR
OfwGetFloppyCount(VOID)
{
    return 0;
}

/* ARC device vtable ********************************************************/

static ARC_STATUS
OfwDiskClose(ULONG FileId)
{
    DISKCONTEXT *Context = FsGetDeviceSpecific(FileId);
    FrLdrTempFree(Context, TAG_HW_DISK_CONTEXT);
    return ESUCCESS;
}

static ARC_STATUS
OfwDiskGetFileInformation(ULONG FileId, FILEINFORMATION *Information)
{
    DISKCONTEXT *Context = FsGetDeviceSpecific(FileId);
    POFW_DISK Disk = OfwGetDisk(Context->DriveNumber);

    RtlZeroMemory(Information, sizeof(*Information));
    Information->StartingAddress.QuadPart = Context->SectorOffset * Context->SectorSize;
    Information->EndingAddress.QuadPart = (Context->SectorOffset + Context->SectorCount) * Context->SectorSize;
    Information->CurrentAddress.QuadPart = Context->SectorNumber * Context->SectorSize;
    Information->Type = (Disk && Disk->IsCdrom) ? CdromController : DiskPeripheral;
    return ESUCCESS;
}

static ARC_STATUS
OfwDiskOpen(CHAR *Path, OPENMODE OpenMode, ULONG *FileId)
{
    DISKCONTEXT *Context;
    POFW_DISK Disk;
    UCHAR DriveNumber;
    ULONG DrivePartition;
    ULONGLONG SectorOffset, SectorCount;

    UNREFERENCED_PARAMETER(OpenMode);

    if (DiskReadBufferSize == 0)
        return ENOMEM;
    if (!DissectArcPath(Path, NULL, &DriveNumber, &DrivePartition))
        return EINVAL;

    Disk = OfwGetDisk(DriveNumber);
    if (!Disk)
        return EINVAL;

    if (DrivePartition != 0xFF && DrivePartition != 0)
    {
        PARTITION_INFORMATION PartitionEntry;

        if (!DiskGetPartitionEntry(DriveNumber, Disk->BlockSize, DrivePartition, &PartitionEntry))
            return EIO;
        SectorOffset = PartitionEntry.StartingOffset.QuadPart / Disk->BlockSize;
        SectorCount = PartitionEntry.PartitionLength.QuadPart / Disk->BlockSize;
    }
    else
    {
        SectorOffset = 0;
        SectorCount = Disk->BlockCount;
    }

    Context = FrLdrTempAlloc(sizeof(DISKCONTEXT), TAG_HW_DISK_CONTEXT);
    if (!Context)
        return ENOMEM;

    Context->DriveNumber = DriveNumber;
    Context->SectorSize = Disk->BlockSize;
    Context->SectorOffset = SectorOffset;
    Context->SectorCount = SectorCount;
    Context->SectorNumber = 0;
    FsSetDeviceSpecific(*FileId, Context);
    return ESUCCESS;
}

static ARC_STATUS
OfwDiskRead(ULONG FileId, VOID *Buffer, ULONG N, ULONG *Count)
{
    DISKCONTEXT *Context = FsGetDeviceSpecific(FileId);
    PUCHAR Ptr = Buffer;
    ULONG Length, TotalSectors, MaxSectors, ReadSectors;
    ULONGLONG SectorOffset;
    BOOLEAN Success = TRUE;

    TotalSectors = (N + Context->SectorSize - 1) / Context->SectorSize;
    MaxSectors = (ULONG)(DiskReadBufferSize / Context->SectorSize);
    SectorOffset = Context->SectorOffset + Context->SectorNumber;

    while (TotalSectors)
    {
        ReadSectors = min(TotalSectors, MaxSectors);
        Success = OfwDiskReadLogicalSectors(Context->DriveNumber, SectorOffset, ReadSectors, DiskReadBuffer);
        if (!Success)
            break;

        Length = min(ReadSectors * Context->SectorSize, N);
        RtlCopyMemory(Ptr, DiskReadBuffer, Length);
        Ptr += Length;
        N -= Length;
        SectorOffset += ReadSectors;
        TotalSectors -= ReadSectors;
    }

    *Count = (ULONG)(Ptr - (PUCHAR)Buffer);
    Context->SectorNumber = SectorOffset - Context->SectorOffset;
    return Success ? ESUCCESS : EIO;
}

static ARC_STATUS
OfwDiskSeek(ULONG FileId, LARGE_INTEGER *Position, SEEKMODE SeekMode)
{
    DISKCONTEXT *Context = FsGetDeviceSpecific(FileId);
    LARGE_INTEGER NewPosition = *Position;

    switch (SeekMode)
    {
        case SeekAbsolute:
            break;
        case SeekRelative:
            NewPosition.QuadPart += Context->SectorNumber * Context->SectorSize;
            break;
        default:
            return EINVAL;
    }

    if (NewPosition.QuadPart & (Context->SectorSize - 1))
        return EINVAL;

    NewPosition.QuadPart /= Context->SectorSize;
    if (Context->SectorCount != 0 && (ULONGLONG)NewPosition.QuadPart >= Context->SectorCount)
        return EINVAL;

    Context->SectorNumber = NewPosition.QuadPart;
    return ESUCCESS;
}

static const DEVVTBL OfwDiskVtbl =
{
    OfwDiskClose,
    OfwDiskGetFileInformation,
    OfwDiskOpen,
    OfwDiskRead,
    OfwDiskSeek,
};

/* Enumeration **************************************************************/

static
BOOLEAN
OfwNodeIsBlockDevice(_In_ OFW_HANDLE Node)
{
    CHAR Type[16];
    LONG Length = OfwGetProp(Node, "device_type", Type, sizeof(Type) - 1);

    if (Length <= 0)
        return FALSE;
    Type[Length] = ANSI_NULL;
    return strcmp(Type, "block") == 0;
}

static
VOID
OfwProbeDisk(_In_ OFW_HANDLE Node)
{
    POFW_DISK Disk;
    ULONG Value;
    UCHAR Signature[8];

    if (OfwDiskCount >= OFW_MAX_DISKS)
        return;

    Disk = &OfwDisks[OfwDiskCount];
    RtlZeroMemory(Disk, sizeof(*Disk));
    if (OfwPackageToPath(Node, Disk->Path, sizeof(Disk->Path)) <= 0)
        return;

    /* Opening fails when the drive has no medium. */
    Disk->Ihandle = OfwOpen(Disk->Path);
    if (Disk->Ihandle == OFW_INVALID)
    {
        TRACE("OFW block device %s cannot be opened\n", Disk->Path);
        return;
    }

    Disk->BlockSize = 512;
    if (OfwCallMethod1("block-size", Disk->Ihandle, &Value) == 0 && Value != 0 && (Value & (Value - 1)) == 0)
        Disk->BlockSize = Value;
    Disk->BlockCount = 0;
    if (OfwCallMethod1("#blocks", Disk->Ihandle, &Value) == 0)
        Disk->BlockCount = Value;

    /* An ISO 9660 primary volume descriptor marks a CD-ROM. */
    if (OfwSeek(Disk->Ihandle, 16 * 2048) >= 0 && OfwRead(Disk->Ihandle, Signature, sizeof(Signature)) == sizeof(Signature) && memcmp(&Signature[1], "CD001", 5) == 0)
    {
        Disk->IsCdrom = TRUE;
        if (Disk->BlockCount != 0 && Disk->BlockSize != 2048)
            Disk->BlockCount = Disk->BlockCount * Disk->BlockSize / 2048;
        Disk->BlockSize = 2048;
    }

    TRACE("OFW disk %lu: %s block %lu count %I64u%s\n", OfwDiskCount, Disk->Path, Disk->BlockSize, Disk->BlockCount, Disk->IsCdrom ? " (CD-ROM)" : "");
    OfwDiskCount++;
}

static
VOID
OfwWalkTree(_In_ OFW_HANDLE Node, _Inout_ PULONG Budget)
{
    OFW_HANDLE Child;

    for (; Node != OFW_INVALID && *Budget != 0; Node = OfwPeer(Node))
    {
        (*Budget)--;
        if (OfwNodeIsBlockDevice(Node))
            OfwProbeDisk(Node);

        Child = OfwChild(Node);
        if (Child != OFW_INVALID)
            OfwWalkTree(Child, Budget);
    }
}

BOOLEAN
OfwInitializeBootDevices(VOID)
{
    ULONG Budget = OFW_MAX_TREE_WALK;
    ULONG i, Checksum, Signature;
    BOOLEAN ValidPartitionTable, BootFound = FALSE;
    CHAR ArcName[64];

    /* A bounce buffer of whole CD-ROM sectors in loader memory. */
    DiskReadBufferSize = 64 * 2048;
    DiskReadBuffer = MmAllocateMemoryWithType(DiskReadBufferSize, LoaderFirmwareTemporary);
    if (!DiskReadBuffer)
        return FALSE;

    OfwDiskCount = 0;
    OfwWalkTree(OfwChild(OfwPeer(OFW_INVALID)), &Budget);

    for (i = 0; i < OfwDiskCount; i++)
    {
        ARC_STATUS Status;
        UCHAR DriveNumber = (UCHAR)(FIRST_BIOS_DISK + i);

        if (OfwDisks[i].IsCdrom)
            RtlStringCbPrintfA(ArcName, sizeof(ArcName), "multi(0)disk(0)cdrom(%lu)", i);
        else
            RtlStringCbPrintfA(ArcName, sizeof(ArcName), "multi(0)disk(0)rdisk(%lu)", i);

        DiskReportError(FALSE);
        Status = DiskInitialize(DriveNumber, ArcName, OfwDisks[i].IsCdrom ? CdromController : DiskPeripheral, &OfwDiskVtbl, &Checksum, &Signature, &ValidPartitionTable);
        DiskReportError(TRUE);
        if (Status != ESUCCESS)
        {
            WARN("OFW disk %s failed to initialize\n", ArcName);
            continue;
        }

        /* Boot from the first CD-ROM, otherwise from the first disk. */
        if (!BootFound && OfwDisks[i].IsCdrom)
        {
            FrldrBootDrive = DriveNumber;
            FrldrBootPartition = 0xFF;
            RtlStringCbCopyA(FrLdrBootPath, sizeof(FrLdrBootPath), ArcName);
            BootFound = TRUE;
        }
    }

    if (!BootFound)
    {
        for (i = 0; i < OfwDiskCount; i++)
        {
            ULONG BootPartition;
            UCHAR DriveNumber = (UCHAR)(FIRST_BIOS_DISK + i);

            if (OfwDisks[i].IsCdrom || !DiskGetBootPartitionEntry(DriveNumber, NULL, &BootPartition))
                continue;
            FrldrBootDrive = DriveNumber;
            FrldrBootPartition = BootPartition;
            RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath), "multi(0)disk(0)rdisk(%lu)partition(%lu)", i, BootPartition);
            BootFound = TRUE;
            break;
        }
    }

    TRACE("OFW boot path: %s\n", BootFound ? FrLdrBootPath : "(none)");
    return BootFound;
}

/* For disk.c!DiskError() */
PCSTR
DiskGetErrorCodeString(_In_ ULONG ErrorCode)
{
    UNREFERENCED_PARAMETER(ErrorCode);
    return NULL;
}

UCHAR
DriveMapGetBiosDriveNumber(PCSTR DeviceName)
{
    UNREFERENCED_PARAMETER(DeviceName);
    return 0;
}
