/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Read-only exFAT filesystem support using FatFs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <freeldr.h>
#include <ff.h>
#include <diskio.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(FILESYSTEM);

#define TAG_EXFAT_VOLUME 'VfxE'
#define TAG_EXFAT_FILE   'FfxE'
#define TAG_EXFAT_PATH   'PfxE'
#define TAG_EXFAT_FATFS  'LfxE'

#define EXFAT_NAME_OFFSET              3
#define EXFAT_NAME_LENGTH              8
#define EXFAT_SECTOR_SHIFT_OFFSET      108
#define EXFAT_BOOT_SIGNATURE_OFFSET    510
#define EXFAT_MIN_SECTOR_SHIFT         9
#define EXFAT_MAX_SECTOR_SHIFT         12

typedef struct _EXFAT_VOLUME
{
    ULONG DeviceId;
    ULONG BytesPerSector;
    ULONGLONG SectorCount;
    BYTE DriveNumber;
    FATFS FileSystem;
} EXFAT_VOLUME, *PEXFat_VOLUME;

typedef struct _EXFAT_FILE
{
    BOOLEAN IsDirectory;
    UCHAR Attributes;
    ULONG FileNameLength;
    CHAR FileName[32];
    FSIZE_t FileSize;
    union
    {
        FIL File;
        DIR Directory;
    } Handle;
} EXFAT_FILE, *PEXFat_FILE;

static PEXFat_VOLUME ExFatVolumes[FF_VOLUMES];

PARTITION VolToPart[FF_VOLUMES] = { { 0, 0 } };

static ARC_STATUS
ExFatMapStatus(
    FRESULT Result)
{
    switch (Result)
    {
        case FR_OK:
            return ESUCCESS;
        case FR_NO_FILE:
        case FR_NO_PATH:
            return ENOENT;
        case FR_INVALID_NAME:
        case FR_INVALID_PARAMETER:
            return EINVAL;
        case FR_DENIED:
        case FR_WRITE_PROTECTED:
        case FR_LOCKED:
            return EACCES;
        case FR_EXIST:
            return EEXIST;
        case FR_NOT_READY:
        case FR_INVALID_DRIVE:
        case FR_NOT_ENABLED:
            return ENODEV;
        case FR_NOT_ENOUGH_CORE:
            return ENOMEM;
        case FR_TOO_MANY_OPEN_FILES:
            return EMFILE;
        default:
            return EIO;
    }
}

static PEXFat_VOLUME
ExFatGetVolumeByDevice(
    ULONG DeviceId)
{
    ULONG Index;

    for (Index = 0; Index < FF_VOLUMES; ++Index)
    {
        if (ExFatVolumes[Index] && ExFatVolumes[Index]->DeviceId == DeviceId)
            return ExFatVolumes[Index];
    }

    return NULL;
}

static PCHAR
ExFatBuildPath(
    PEXFat_VOLUME Volume,
    PCSTR Path)
{
    SIZE_T Length;
    PCHAR FatPath;
    PCHAR Destination;

    while (*Path == '\\' || *Path == '/')
        ++Path;

    Length = strlen(Path);
    FatPath = FrLdrTempAlloc(Length + 4, TAG_EXFAT_PATH);
    if (!FatPath)
        return NULL;

    FatPath[0] = '0' + Volume->DriveNumber;
    FatPath[1] = ':';
    FatPath[2] = '/';
    Destination = &FatPath[3];

    while (*Path)
    {
        *Destination++ = (*Path == '\\') ? '/' : *Path;
        ++Path;
    }
    *Destination = ANSI_NULL;

    return FatPath;
}

static VOID
ExFatSetFileName(
    PEXFat_FILE File,
    PCSTR Path)
{
    PCSTR Name;
    PCSTR Current;
    SIZE_T Length;

    Name = Path;
    for (Current = Path; *Current; ++Current)
    {
        if (*Current == '\\' || *Current == '/')
            Name = Current + 1;
    }

    Length = min(strlen(Name), sizeof(File->FileName) - 1);
    RtlCopyMemory(File->FileName, Name, Length);
    File->FileName[Length] = ANSI_NULL;
    File->FileNameLength = (ULONG)Length;
}

static UCHAR
ExFatMapAttributes(
    BYTE Attributes)
{
    UCHAR ArcAttributes = 0;

    if (Attributes & AM_RDO)
        ArcAttributes |= ReadOnlyFile;
    if (Attributes & AM_HID)
        ArcAttributes |= HiddenFile;
    if (Attributes & AM_SYS)
        ArcAttributes |= SystemFile;
    if (Attributes & AM_ARC)
        ArcAttributes |= ArchiveFile;
    if (Attributes & AM_DIR)
        ArcAttributes |= DirectoryFile;

    return ArcAttributes;
}

static ARC_STATUS
ExFatClose(
    ULONG FileId)
{
    PEXFat_FILE File = FsGetDeviceSpecific(FileId);
    FRESULT Result;

    if (File->IsDirectory)
        Result = f_closedir(&File->Handle.Directory);
    else
        Result = f_close(&File->Handle.File);

    FrLdrTempFree(File, TAG_EXFAT_FILE);
    return ExFatMapStatus(Result);
}

static ARC_STATUS
ExFatGetFileInformation(
    ULONG FileId,
    FILEINFORMATION* Information)
{
    PEXFat_FILE File = FsGetDeviceSpecific(FileId);

    RtlZeroMemory(Information, sizeof(*Information));
    Information->EndingAddress.QuadPart = File->FileSize;
    if (!File->IsDirectory)
        Information->CurrentAddress.QuadPart = f_tell(&File->Handle.File);
    Information->Attributes = File->Attributes;
    Information->FileNameLength = File->FileNameLength;
    RtlCopyMemory(Information->FileName, File->FileName, File->FileNameLength + 1);

    return ESUCCESS;
}

static ARC_STATUS
ExFatOpen(
    CHAR* Path,
    OPENMODE OpenMode,
    ULONG* FileId)
{
    ULONG DeviceId;
    PEXFat_VOLUME Volume;
    PEXFat_FILE File;
    PCHAR FatPath;
    FILINFO Information;
    FRESULT Result;
    BOOLEAN IsRoot;
    BOOLEAN IsDirectory;

    if (OpenMode != OpenReadOnly && OpenMode != OpenDirectory)
        return EACCES;

    DeviceId = FsGetDeviceId(*FileId);
    Volume = ExFatGetVolumeByDevice(DeviceId);
    if (!Volume)
        return ENODEV;

    FatPath = ExFatBuildPath(Volume, Path);
    if (!FatPath)
        return ENOMEM;

    IsRoot = (FatPath[3] == ANSI_NULL);
    RtlZeroMemory(&Information, sizeof(Information));
    if (IsRoot)
    {
        Information.fattrib = AM_DIR;
    }
    else
    {
        Result = f_stat(FatPath, &Information);
        if (Result != FR_OK)
        {
            FrLdrTempFree(FatPath, TAG_EXFAT_PATH);
            return ExFatMapStatus(Result);
        }
    }

    IsDirectory = !!(Information.fattrib & AM_DIR);
    if (IsDirectory && OpenMode != OpenDirectory)
    {
        FrLdrTempFree(FatPath, TAG_EXFAT_PATH);
        return EISDIR;
    }
    if (!IsDirectory && OpenMode != OpenReadOnly)
    {
        FrLdrTempFree(FatPath, TAG_EXFAT_PATH);
        return ENOTDIR;
    }

    File = FrLdrTempAlloc(sizeof(*File), TAG_EXFAT_FILE);
    if (!File)
    {
        FrLdrTempFree(FatPath, TAG_EXFAT_PATH);
        return ENOMEM;
    }
    RtlZeroMemory(File, sizeof(*File));

    if (IsDirectory)
        Result = f_opendir(&File->Handle.Directory, FatPath);
    else
        Result = f_open(&File->Handle.File, FatPath, FA_READ | FA_OPEN_EXISTING);

    FrLdrTempFree(FatPath, TAG_EXFAT_PATH);
    if (Result != FR_OK)
    {
        FrLdrTempFree(File, TAG_EXFAT_FILE);
        return ExFatMapStatus(Result);
    }

    File->IsDirectory = IsDirectory;
    File->Attributes = ExFatMapAttributes(Information.fattrib);
    File->FileSize = IsDirectory ? 0 : Information.fsize;
    ExFatSetFileName(File, Path);
    FsSetDeviceSpecific(*FileId, File);

    return ESUCCESS;
}

static ARC_STATUS
ExFatRead(
    ULONG FileId,
    VOID* Buffer,
    ULONG Size,
    ULONG* BytesRead)
{
    PEXFat_FILE File = FsGetDeviceSpecific(FileId);
    UINT Read;
    FRESULT Result;

    *BytesRead = 0;
    if (File->IsDirectory)
        return EISDIR;

    Result = f_read(&File->Handle.File, Buffer, Size, &Read);
    *BytesRead = Read;
    return ExFatMapStatus(Result);
}

static ARC_STATUS
ExFatSeek(
    ULONG FileId,
    LARGE_INTEGER* Position,
    SEEKMODE SeekMode)
{
    PEXFat_FILE File = FsGetDeviceSpecific(FileId);
    LONGLONG NewPosition;

    if (File->IsDirectory)
        return EISDIR;

    if (SeekMode == SeekAbsolute)
        NewPosition = Position->QuadPart;
    else if (SeekMode == SeekRelative)
        NewPosition = (LONGLONG)f_tell(&File->Handle.File) + Position->QuadPart;
    else
        return EINVAL;

    if (NewPosition < 0 || (ULONGLONG)NewPosition > File->FileSize)
        return EINVAL;

    return ExFatMapStatus(f_lseek(&File->Handle.File, (FSIZE_t)NewPosition));
}

const DEVVTBL ExFatFuncTable =
{
    ExFatClose,
    ExFatGetFileInformation,
    ExFatOpen,
    ExFatRead,
    ExFatSeek,
    L"exfat",
};

const DEVVTBL*
ExFatMount(
    ULONG DeviceId)
{
    UCHAR BootSector[SECTOR_SIZE];
    FILEINFORMATION Information;
    LARGE_INTEGER Position;
    PEXFat_VOLUME Volume;
    PEXFat_VOLUME ExistingVolume;
    ULONGLONG VolumeSize;
    ULONG Count;
    ULONG DriveNumber;
    CHAR DrivePath[3];
    BYTE SectorShift;
    FRESULT Result;

    ExistingVolume = ExFatGetVolumeByDevice(DeviceId);
    if (ExistingVolume)
        return &ExFatFuncTable;

    Position.QuadPart = 0;
    if (ArcSeek(DeviceId, &Position, SeekAbsolute) != ESUCCESS ||
        ArcRead(DeviceId, BootSector, sizeof(BootSector), &Count) != ESUCCESS ||
        Count != sizeof(BootSector))
    {
        return NULL;
    }

    if (!RtlEqualMemory(&BootSector[EXFAT_NAME_OFFSET], "EXFAT   ", EXFAT_NAME_LENGTH) ||
        BootSector[EXFAT_BOOT_SIGNATURE_OFFSET] != 0x55 ||
        BootSector[EXFAT_BOOT_SIGNATURE_OFFSET + 1] != 0xAA)
    {
        return NULL;
    }

    SectorShift = BootSector[EXFAT_SECTOR_SHIFT_OFFSET];
    if (SectorShift < EXFAT_MIN_SECTOR_SHIFT || SectorShift > EXFAT_MAX_SECTOR_SHIFT)
        return NULL;

    if (ArcGetFileInformation(DeviceId, &Information) != ESUCCESS ||
        Information.EndingAddress.QuadPart <= Information.StartingAddress.QuadPart)
    {
        return NULL;
    }

    VolumeSize = Information.EndingAddress.QuadPart - Information.StartingAddress.QuadPart;
    Volume = FrLdrTempAlloc(sizeof(*Volume), TAG_EXFAT_VOLUME);
    if (!Volume)
        return NULL;
    RtlZeroMemory(Volume, sizeof(*Volume));

    for (DriveNumber = 0; DriveNumber < FF_VOLUMES; ++DriveNumber)
    {
        if (!ExFatVolumes[DriveNumber])
            break;
    }
    if (DriveNumber == FF_VOLUMES)
    {
        FrLdrTempFree(Volume, TAG_EXFAT_VOLUME);
        return NULL;
    }

    Volume->DeviceId = DeviceId;
    Volume->BytesPerSector = 1UL << SectorShift;
    Volume->SectorCount = VolumeSize / Volume->BytesPerSector;
    Volume->DriveNumber = (BYTE)DriveNumber;
    if (!Volume->SectorCount || VolumeSize % Volume->BytesPerSector)
    {
        FrLdrTempFree(Volume, TAG_EXFAT_VOLUME);
        return NULL;
    }

    ExFatVolumes[DriveNumber] = Volume;
    VolToPart[DriveNumber].pd = (BYTE)DriveNumber;
    VolToPart[DriveNumber].pt = 0;
    DrivePath[0] = '0' + (CHAR)DriveNumber;
    DrivePath[1] = ':';
    DrivePath[2] = ANSI_NULL;

    Result = f_mount(&Volume->FileSystem, DrivePath, 1);
    if (Result != FR_OK || Volume->FileSystem.fs_type != FS_EXFAT)
    {
        f_mount(NULL, DrivePath, 0);
        ExFatVolumes[DriveNumber] = NULL;
        FrLdrTempFree(Volume, TAG_EXFAT_VOLUME);
        return NULL;
    }

    TRACE("ExFatMount(%lu) mounted as FatFs drive %lu\n", DeviceId, DriveNumber);
    return &ExFatFuncTable;
}

DSTATUS
disk_initialize(
    BYTE PhysicalDrive)
{
    if (PhysicalDrive >= FF_VOLUMES || !ExFatVolumes[PhysicalDrive])
        return STA_NOINIT;

    return STA_PROTECT;
}

DSTATUS
disk_status(
    BYTE PhysicalDrive)
{
    return disk_initialize(PhysicalDrive);
}

DRESULT
disk_read(
    BYTE PhysicalDrive,
    BYTE* Buffer,
    LBA_t Sector,
    UINT Count)
{
    PEXFat_VOLUME Volume;
    LARGE_INTEGER Position;
    ULONG BytesToRead;
    ULONG BytesRead;

    if (PhysicalDrive >= FF_VOLUMES || !Buffer || !Count)
        return RES_PARERR;

    Volume = ExFatVolumes[PhysicalDrive];
    if (!Volume)
        return RES_NOTRDY;
    if (Sector >= Volume->SectorCount || Count > Volume->SectorCount - Sector)
        return RES_PARERR;
    if (Count > MAXULONG / Volume->BytesPerSector)
        return RES_PARERR;

    Position.QuadPart = Sector * Volume->BytesPerSector;
    BytesToRead = Count * Volume->BytesPerSector;
    if (ArcSeek(Volume->DeviceId, &Position, SeekAbsolute) != ESUCCESS ||
        ArcRead(Volume->DeviceId, Buffer, BytesToRead, &BytesRead) != ESUCCESS ||
        BytesRead != BytesToRead)
    {
        return RES_ERROR;
    }

    return RES_OK;
}

DRESULT
disk_write(
    BYTE PhysicalDrive,
    const BYTE* Buffer,
    LBA_t Sector,
    UINT Count)
{
    UNREFERENCED_PARAMETER(PhysicalDrive);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Sector);
    UNREFERENCED_PARAMETER(Count);
    return RES_WRPRT;
}

DRESULT
disk_ioctl(
    BYTE PhysicalDrive,
    BYTE Command,
    void* Buffer)
{
    PEXFat_VOLUME Volume;

    if (PhysicalDrive >= FF_VOLUMES)
        return RES_PARERR;

    Volume = ExFatVolumes[PhysicalDrive];
    if (!Volume)
        return RES_NOTRDY;

    switch (Command)
    {
        case CTRL_SYNC:
            return RES_OK;
        case GET_SECTOR_COUNT:
            if (!Buffer)
                return RES_PARERR;
            *(LBA_t*)Buffer = Volume->SectorCount;
            return RES_OK;
        case GET_SECTOR_SIZE:
            if (!Buffer)
                return RES_PARERR;
            *(WORD*)Buffer = (WORD)Volume->BytesPerSector;
            return RES_OK;
        case GET_BLOCK_SIZE:
            if (!Buffer)
                return RES_PARERR;
            *(DWORD*)Buffer = 1;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

DWORD
get_fattime(VOID)
{
    return (1U << 21) | (1U << 16);
}

void*
ff_memalloc(
    UINT Size)
{
    return FrLdrTempAlloc(Size, TAG_EXFAT_FATFS);
}

void
ff_memfree(
    void* Allocation)
{
    FrLdrTempFree(Allocation, TAG_EXFAT_FATFS);
}
