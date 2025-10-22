/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Disk Access Functions
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <uefildr.h>
#include <uefi/uefiarcname.h>
#include <DevicePath.h>  /* EFI_DEVICE_PATH_PROTOCOL, helpers */
#include <disk.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

#define TAG_HW_RESOURCE_LIST    'lRwH'
#define TAG_HW_DISK_CONTEXT     'cDwH'
#define FIRST_PARTITION 1

typedef struct tagDISKCONTEXT
{
    UCHAR DriveNumber;
    ULONG SectorSize;
    ULONGLONG SectorOffset;
    ULONGLONG SectorCount;
    ULONGLONG SectorNumber;
} DISKCONTEXT;

typedef struct _INTERNAL_UEFI_DISK
{
    UCHAR ArcDriveNumber;
    UCHAR NumOfPartitions;
    UCHAR UefiRootNumber;
    BOOLEAN IsThisTheBootDrive;
} INTERNAL_UEFI_DISK, *PINTERNAL_UEFI_DISK;

/* GLOBALS *******************************************************************/

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern EFI_HANDLE PublicBootHandle; /* Freeldr itself */

/* Made to match BIOS */
PVOID DiskReadBuffer;
UCHAR PcBiosDiskCount;

UCHAR FrldrBootDrive;
ULONG FrldrBootPartition;
SIZE_T DiskReadBufferSize;
PVOID Buffer;

static const CHAR Hex[] = "0123456789abcdef";
static CHAR PcDiskIdentifier[32][20];

/* UEFI-specific */
static ULONG UefiBootRootIdentifier;
static ULONG OffsetToBoot;
static ULONG PublicBootArcDisk;
static INTERNAL_UEFI_DISK* InternalUefiDisk = NULL;
static EFI_GUID bioGuid = BLOCK_IO_PROTOCOL;
static EFI_BLOCK_IO* bio;
static EFI_HANDLE* handles = NULL;

BOOLEAN UefiBootHasDiskArc = FALSE;
ULONG UefiBootDiskArcNumber = 0;
ULONG UefiBootDiskArcPartition = 0;

#ifndef EFI_DEVICE_PATH_PROTOCOL_GUID
#define EFI_DEVICE_PATH_PROTOCOL_GUID \
  { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } }
#endif

static EFI_GUID DevicePathProtocolGuid = EFI_DEVICE_PATH_PROTOCOL_GUID;

static
BOOLEAN
UefiIsCdRomHandle(IN EFI_HANDLE Handle)
{
    EFI_DEVICE_PATH_PROTOCOL* DevicePath = NULL;

    if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
            Handle, &DevicePathProtocolGuid, (VOID**)&DevicePath)) ||
        !DevicePath)
    {
        return FALSE;
    }

    EFI_DEVICE_PATH_PROTOCOL* Node = DevicePath;
    while (!IsDevicePathEnd(Node))
    {
        if (Node->Type == MEDIA_DEVICE_PATH && Node->SubType == MEDIA_CDROM_DP)
            return TRUE;
        Node = NextDevicePathNode(Node);
    }

    return FALSE;
}

/* FUNCTIONS *****************************************************************/

PCHAR
GetHarddiskIdentifier(UCHAR DriveNumber)
{
    TRACE("GetHarddiskIdentifier: DriveNumber: %d\n", DriveNumber);
    return PcDiskIdentifier[DriveNumber - FIRST_BIOS_DISK];
}

static LONG lReportError = 0; // >= 0: display errors; < 0: hide errors.

LONG
DiskReportError(BOOLEAN bShowError)
{
    /* Set the reference count */
    if (bShowError) ++lReportError;
    else            --lReportError;
    return lReportError;
}

BOOLEAN
UefiGetBootPartitionEntry(
    IN UCHAR DriveNumber,
    OUT PPARTITION_TABLE_ENTRY PartitionTableEntry,
    OUT PULONG BootPartition)
{
    ULONG PartitionNum;

    TRACE("UefiGetBootPartitionEntry: DriveNumber: %d\n", DriveNumber - FIRST_BIOS_DISK);
    /* UefiBootRoot is the offset into the array of handles where the raw disk of the boot drive is.
     * Partitions start with 1 in ARC, but UEFI root drive identitfier is also first partition. */
    PartitionNum = (OffsetToBoot - UefiBootRootIdentifier);
    if (PartitionNum == 0)
    {
        TRACE("Boot PartitionNumber is 0\n");
        /* The OffsetToBoot is equal to the RootIdentifier */
        PartitionNum = FIRST_PARTITION;
    }

    *BootPartition = PartitionNum;
    TRACE("UefiGetBootPartitionEntry: Boot Partition is: %d\n", PartitionNum);
    return TRUE;
}

static
ARC_STATUS
UefiDiskClose(ULONG FileId)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    FrLdrTempFree(Context, TAG_HW_DISK_CONTEXT);
    return ESUCCESS;
}

static
ARC_STATUS
UefiDiskGetFileInformation(ULONG FileId, FILEINFORMATION *Information)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    RtlZeroMemory(Information, sizeof(*Information));

    /*
     * The ARC specification mentions that for partitions, StartingAddress and
     * EndingAddress are the start and end positions of the partition in terms
     * of byte offsets from the start of the disk.
     * CurrentAddress is the current offset into (i.e. relative to) the partition.
     */
    Information->StartingAddress.QuadPart = Context->SectorOffset * Context->SectorSize;
    Information->EndingAddress.QuadPart   = (Context->SectorOffset + Context->SectorCount) * Context->SectorSize;
    Information->CurrentAddress.QuadPart  = Context->SectorNumber * Context->SectorSize;

    return ESUCCESS;
}

static
ARC_STATUS
UefiDiskOpen(CHAR *Path, OPENMODE OpenMode, ULONG *FileId)
{
    DISKCONTEXT* Context;
    UCHAR DriveNumber;
    ULONG DrivePartition, SectorSize;
    ULONGLONG SectorOffset = 0;
    ULONGLONG SectorCount = 0;
    ULONG UefiDriveNumber = 0;
    PARTITION_TABLE_ENTRY PartitionTableEntry;

    TRACE("UefiDiskOpen: File ID: %d, Path: %s\n", FileId, Path);

    if (DiskReadBufferSize == 0)
    {
        ERR("DiskOpen(): DiskReadBufferSize is 0, something is wrong.\n");
        ASSERT(FALSE);
        return ENOMEM;
    }

    if (!DissectArcPath(Path, NULL, &DriveNumber, &DrivePartition))
        return EINVAL;

    TRACE("Opening disk: DriveNumber: %d, DrivePartition: %d\n", DriveNumber, DrivePartition);
    UefiDriveNumber = DriveNumber - FIRST_BIOS_DISK;
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    SectorSize = bio->Media->BlockSize;

    if (DrivePartition != 0xff && DrivePartition != 0)
    {
        if (!DiskGetPartitionEntry(DriveNumber, DrivePartition, &PartitionTableEntry))
            return EINVAL;

        SectorOffset = PartitionTableEntry.SectorCountBeforePartition;
        SectorCount = PartitionTableEntry.PartitionSectorCount;
    }
    else
    {
        GEOMETRY Geometry;
        if (!MachDiskGetDriveGeometry(DriveNumber, &Geometry))
            return EINVAL;

        if (SectorSize != Geometry.BytesPerSector)
        {
            ERR("SectorSize (%lu) != Geometry.BytesPerSector (%lu), expect problems!\n",
                SectorSize, Geometry.BytesPerSector);
        }

        SectorOffset = 0;
        SectorCount = Geometry.Sectors;
    }

    Context = FrLdrTempAlloc(sizeof(DISKCONTEXT), TAG_HW_DISK_CONTEXT);
    if (!Context)
        return ENOMEM;

    Context->DriveNumber = DriveNumber;
    Context->SectorSize = SectorSize;
    Context->SectorOffset = SectorOffset;
    Context->SectorCount = SectorCount;
    Context->SectorNumber = 0;
    FsSetDeviceSpecific(*FileId, Context);
    return ESUCCESS;
}

static
ARC_STATUS
UefiDiskRead(ULONG FileId, VOID *Buffer, ULONG N, ULONG *Count)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    UCHAR* Ptr = (UCHAR*)Buffer;
    ULONG Length, TotalSectors, MaxSectors, ReadSectors;
    ULONGLONG SectorOffset;
    BOOLEAN ret;

    ASSERT(DiskReadBufferSize > 0);

    TotalSectors = (N + Context->SectorSize - 1) / Context->SectorSize;
    MaxSectors   = DiskReadBufferSize / Context->SectorSize;
    SectorOffset = Context->SectorOffset + Context->SectorNumber;

    // If MaxSectors is 0, this will lead to infinite loop.
    // In release builds assertions are disabled, however we also have sanity checks in DiskOpen()
    ASSERT(MaxSectors > 0);

    ret = TRUE;

    while (TotalSectors)
    {
        ReadSectors = min(TotalSectors, MaxSectors);

        ret = MachDiskReadLogicalSectors(Context->DriveNumber,
                                         SectorOffset,
                                         ReadSectors,
                                         DiskReadBuffer);
        if (!ret)
            break;

        Length = ReadSectors * Context->SectorSize;
        Length = min(Length, N);

        RtlCopyMemory(Ptr, DiskReadBuffer, Length);

        Ptr += Length;
        N -= Length;
        SectorOffset += ReadSectors;
        TotalSectors -= ReadSectors;
    }

    *Count = (ULONG)((ULONG_PTR)Ptr - (ULONG_PTR)Buffer);
    Context->SectorNumber = SectorOffset - Context->SectorOffset;

    return (ret ? ESUCCESS : EIO);
}

static
ARC_STATUS
UefiDiskSeek(ULONG FileId, LARGE_INTEGER *Position, SEEKMODE SeekMode)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    LARGE_INTEGER NewPosition = *Position;

    switch (SeekMode)
    {
        case SeekAbsolute:
            break;
        case SeekRelative:
            NewPosition.QuadPart += (Context->SectorNumber * Context->SectorSize);
            break;
        default:
            ASSERT(FALSE);
            return EINVAL;
    }

    if (NewPosition.QuadPart & (Context->SectorSize - 1))
        return EINVAL;

    /* Convert in number of sectors */
    NewPosition.QuadPart /= Context->SectorSize;

    /* HACK: CDROMs may have a SectorCount of 0 */
    if (Context->SectorCount != 0 && NewPosition.QuadPart >= Context->SectorCount)
        return EINVAL;

    Context->SectorNumber = NewPosition.QuadPart;
    return ESUCCESS;
}

static const DEVVTBL UefiDiskVtbl =
{
    UefiDiskClose,
    UefiDiskGetFileInformation,
    UefiDiskOpen,
    UefiDiskRead,
    UefiDiskSeek,
};

static
VOID
GetHarddiskInformation(UCHAR DriveNumber)
{
    PMASTER_BOOT_RECORD Mbr;
    PULONG Buffer;
    ULONG i;
    ULONG Checksum;
    ULONG Signature;
    BOOLEAN ValidPartitionTable;
    CHAR ArcName[MAX_PATH];
    PARTITION_TABLE_ENTRY PartitionTableEntry;
    PCHAR Identifier = PcDiskIdentifier[DriveNumber - FIRST_BIOS_DISK];

    /* Detect disk partition type */
    DiskDetectPartitionType(DriveNumber);

    /* Read the MBR */
    if (!MachDiskReadLogicalSectors(DriveNumber, 0ULL, 1, DiskReadBuffer))
    {
        ERR("Reading MBR failed\n");
        /* We failed, use a default identifier */
        sprintf(Identifier, "BIOSDISK%d", DriveNumber - FIRST_BIOS_DISK);
        return;
    }

    Buffer = (ULONG*)DiskReadBuffer;
    Mbr = (PMASTER_BOOT_RECORD)DiskReadBuffer;

    Signature = Mbr->Signature;
    TRACE("Signature: %x\n", Signature);

    /* Calculate the MBR checksum */
    Checksum = 0;
    for (i = 0; i < 512 / sizeof(ULONG); i++)
    {
        Checksum += Buffer[i];
    }
    Checksum = ~Checksum + 1;
    TRACE("Checksum: %x\n", Checksum);

    ValidPartitionTable = (Mbr->MasterBootRecordMagic == 0xAA55);

    /* Fill out the ARC disk block */
    sprintf(ArcName, "multi(0)disk(0)rdisk(%u)", DriveNumber - FIRST_BIOS_DISK);
    AddReactOSArcDiskInfo(ArcName, Signature, Checksum, ValidPartitionTable);

    sprintf(ArcName, "multi(0)disk(0)rdisk(%u)partition(0)", DriveNumber - FIRST_BIOS_DISK);
    FsRegisterDevice(ArcName, &UefiDiskVtbl);

    /* Add partitions */
    i = FIRST_PARTITION;
    DiskReportError(FALSE);
    while (DiskGetPartitionEntry(DriveNumber, i, &PartitionTableEntry))
    {
        if (PartitionTableEntry.SystemIndicator != PARTITION_ENTRY_UNUSED)
        {
            sprintf(ArcName, "multi(0)disk(0)rdisk(%u)partition(%lu)", DriveNumber - FIRST_BIOS_DISK, i);
            FsRegisterDevice(ArcName, &UefiDiskVtbl);
        }
        i++;
    }
    DiskReportError(TRUE);

    InternalUefiDisk[DriveNumber].NumOfPartitions = i;
    /* Convert checksum and signature to identifier string */
    Identifier[0] = Hex[(Checksum >> 28) & 0x0F];
    Identifier[1] = Hex[(Checksum >> 24) & 0x0F];
    Identifier[2] = Hex[(Checksum >> 20) & 0x0F];
    Identifier[3] = Hex[(Checksum >> 16) & 0x0F];
    Identifier[4] = Hex[(Checksum >> 12) & 0x0F];
    Identifier[5] = Hex[(Checksum >> 8) & 0x0F];
    Identifier[6] = Hex[(Checksum >> 4) & 0x0F];
    Identifier[7] = Hex[Checksum & 0x0F];
    Identifier[8] = '-';
    Identifier[9] = Hex[(Signature >> 28) & 0x0F];
    Identifier[10] = Hex[(Signature >> 24) & 0x0F];
    Identifier[11] = Hex[(Signature >> 20) & 0x0F];
    Identifier[12] = Hex[(Signature >> 16) & 0x0F];
    Identifier[13] = Hex[(Signature >> 12) & 0x0F];
    Identifier[14] = Hex[(Signature >> 8) & 0x0F];
    Identifier[15] = Hex[(Signature >> 4) & 0x0F];
    Identifier[16] = Hex[Signature & 0x0F];
    Identifier[17] = '-';
    Identifier[18] = (ValidPartitionTable ? 'A' : 'X');
    Identifier[19] = 0;
    TRACE("Identifier: %s\n", Identifier);
}

static
VOID
UefiSetupBlockDevices(VOID)
{
    ULONG BlockDeviceIndex;
    ULONG SystemHandleCount;
    EFI_STATUS Status;
    ULONG i;

    UINTN handle_size = 0;
    PcBiosDiskCount = 0;
    UefiBootRootIdentifier = 0;

    /* 1) Setup a list of boot handles by using the LocateHandle protocol */
    Status = GlobalSystemTable->BootServices->LocateHandle(ByProtocol, &bioGuid, NULL, &handle_size, handles);
    handles = MmAllocateMemoryWithType(handle_size, LoaderFirmwareTemporary);
    Status = GlobalSystemTable->BootServices->LocateHandle(ByProtocol, &bioGuid, NULL, &handle_size, handles);
    SystemHandleCount = handle_size / sizeof(EFI_HANDLE);
    InternalUefiDisk = MmAllocateMemoryWithType(sizeof(INTERNAL_UEFI_DISK) * SystemHandleCount, LoaderFirmwareTemporary);

    BlockDeviceIndex = 0;
    /* 2) Parse the handle list */
    for (i = 0; i < SystemHandleCount; ++i)
    {
        Status = GlobalSystemTable->BootServices->HandleProtocol(handles[i], &bioGuid, (void**)&bio);
        if (handles[i] == PublicBootHandle)
        {
            OffsetToBoot = i; /* Drive offset in the handles list */
        }

        if (EFI_ERROR(Status) || 
            bio == NULL ||
            bio->Media->BlockSize == 0 ||
            bio->Media->BlockSize > 4096)
        {
            TRACE("UefiSetupBlockDevices: UEFI has found a block device that failed, skipping\n");
            continue;
        }
        if (bio->Media->LogicalPartition == FALSE)
        {
            TRACE("Found root of a HDD\n");
            PcBiosDiskCount++;
            InternalUefiDisk[BlockDeviceIndex].ArcDriveNumber = BlockDeviceIndex;
            InternalUefiDisk[BlockDeviceIndex].UefiRootNumber = i;
            GetHarddiskInformation(BlockDeviceIndex + FIRST_BIOS_DISK);
            BlockDeviceIndex++;
        }
        else if (handles[i] == PublicBootHandle)
        {
            GlobalSystemTable->BootServices->HandleProtocol(handles[i], &bioGuid, (void**)&bio);
            if (bio->Media->LogicalPartition == FALSE)
            {
                ULONG j;

                TRACE("Found root at index %u\n", i);
                UefiBootRootIdentifier = i;

                for (j = 0; j <= PcBiosDiskCount; ++j)
                {
                    /* Now only of the root drive number is equal to this drive we found above */
                    if (InternalUefiDisk[j].UefiRootNumber == UefiBootRootIdentifier)
                    {
                        InternalUefiDisk[j].IsThisTheBootDrive = TRUE;
                        PublicBootArcDisk = j;
                        TRACE("Found Boot drive\n");
                    }
                }
            }
        }
    }
}

static
BOOLEAN
UefiSetBootpath(VOID)
{
   TRACE("UefiSetBootpath: Setting up boot path\n");
   GlobalSystemTable->BootServices->HandleProtocol(handles[UefiBootRootIdentifier], &bioGuid, (void**)&bio);
   FrldrBootDrive = (FIRST_BIOS_DISK + PublicBootArcDisk);
   UefiBootHasDiskArc = FALSE;
   UefiBootDiskArcNumber = 0;
   UefiBootDiskArcPartition = 0;

   TRACE("Boot media: Removable=%d BlockSize=%u LogicalPartition=%d\n",
         bio->Media->RemovableMedia,
         bio->Media->BlockSize,
         bio->Media->LogicalPartition);

   ULONG BootPartition = 0;
   PARTITION_TABLE_ENTRY PartitionEntry;
   BOOLEAN HasPartitionInfo = FALSE;
   BOOLEAN TreatAsCd = UefiIsCdRomHandle(handles[UefiBootRootIdentifier]);

   RtlZeroMemory(&PartitionEntry, sizeof(PartitionEntry));

   if (UefiGetBootPartitionEntry(FrldrBootDrive, &PartitionEntry, &BootPartition) &&
       BootPartition != 0)
   {
        HasPartitionInfo = (PartitionEntry.PartitionSectorCount != 0 ||
                            PartitionEntry.SystemIndicator != PARTITION_ENTRY_UNUSED);
   }

   if (TreatAsCd && HasPartitionInfo)
   {
        TRACE("Boot handle advertises CD but partitioned media detected, treating as HDD\n");
        TreatAsCd = FALSE;
   }

   if (!TreatAsCd && bio->Media->RemovableMedia == TRUE &&
       bio->Media->BlockSize == 2048 && !HasPartitionInfo)
   {
        TRACE("Removable 2048-byte media without partitions, treating as ISO/CD\n");
        TreatAsCd = TRUE;
   }

   if (!HasPartitionInfo)
   {
        BootPartition = 1;
   }

   if (BootPartition == 0)
   {
        BootPartition = 1;
   }

   if (TreatAsCd && BootPartition > 1)
   {
        TRACE("Normalizing boot partition %lu to 1 for ISO/CD boot\n", BootPartition);
        BootPartition = 1;
   }

   if (TreatAsCd)
   {
        ULONG CdIndex = MapToCdromIndex(handles[UefiBootRootIdentifier]);
        UefiBootHasDiskArc = FALSE;
        UefiBootDiskArcNumber = 0;
        UefiBootDiskArcPartition = 0;
        FrldrBootPartition = 0xFF;
        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)cdrom(%lu)", CdIndex);
   }
   else
   {
        UefiBootHasDiskArc = TRUE;
        UefiBootDiskArcNumber = PublicBootArcDisk;
        UefiBootDiskArcPartition = BootPartition;
        FrldrBootPartition = BootPartition;
        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)rdisk(%u)partition(%lu)",
                           PublicBootArcDisk, BootPartition);
   }

   TRACE("UEFI boot media classification: TreatAsCd=%d HasPartitionInfo=%d BootPartition=%lu\n",
         TreatAsCd, HasPartitionInfo, BootPartition);

    return TRUE;
}

BOOLEAN
UefiInitializeBootDevices(VOID)
{
    ULONG i = 0;
    BOOLEAN IsCdBoot;

    /* Use a larger bouncing buffer than a single EFI page to speed up ISO reads */
    {
        SIZE_T PreferredBufferSize = FrLdrGetRecommendedDiskBufferSize(0);

        if (PreferredBufferSize < (SIZE_T)(1024 * 1024))
            PreferredBufferSize = (SIZE_T)(1024 * 1024);

        PreferredBufferSize = (PreferredBufferSize + EFI_PAGE_SIZE - 1) & ~((SIZE_T)EFI_PAGE_SIZE - 1);

        DiskReadBufferSize = PreferredBufferSize;
        DiskReadBuffer = MmAllocateMemoryWithType(DiskReadBufferSize, LoaderFirmwareTemporary);
    }
    if (!DiskReadBuffer)
    {
        /* Fall back to a single page if the large buffer cannot be allocated */
        DiskReadBufferSize = EFI_PAGE_SIZE;
        DiskReadBuffer = MmAllocateMemoryWithType(DiskReadBufferSize, LoaderFirmwareTemporary);
    }
    ASSERT(DiskReadBuffer != NULL);
    UefiSetupBlockDevices();
    UefiSetBootpath();

    /* Populate the ARC disk list so the Windows boot loader sees every disk. */
    UefiEnumerateArcDisks();

    /* Add it, if it's a cdrom */
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiBootRootIdentifier], &bioGuid, (void**)&bio);
    IsCdBoot = (FrldrBootPartition == 0xFF);

    if (IsCdBoot ||
        (bio->Media->RemovableMedia == TRUE && bio->Media->BlockSize == 2048))
    {
        PMASTER_BOOT_RECORD Mbr;
        PULONG Buffer;
        ULONG Checksum = 0;
        ULONG Signature;
        ULONG BlockSize;
        ULONG BlocksToRead;
        ULONG ChecksumBytes;
        EFI_STATUS Status;
        EFI_BLOCK_IO* BootBlockIo;

        BlockSize = bio->Media->BlockSize;
        if (BlockSize == 0)
        {
            /* Fallback to the ISO9660 logical block size */
            BlockSize = 2048;
        }

        /* Ensure we read enough data to cover the ISO primary descriptor */
        BlocksToRead = (2048 + BlockSize - 1) / BlockSize;
        if (BlocksToRead == 0)
        {
            BlocksToRead = 1;
        }

        /* Obtain the block protocol for the boot handle */
        Status = GlobalSystemTable->BootServices->HandleProtocol(
            handles[UefiBootRootIdentifier], &bioGuid, (VOID**)&BootBlockIo);
        if (EFI_ERROR(Status) || BootBlockIo == NULL)
        {
            ERR("Failed to query block protocol for boot device (Status=%lx)\n", (ULONG_PTR)Status);
            return FALSE;
        }

        if (BootBlockIo->Media)
        {
            SIZE_T BlockSize = BootBlockIo->Media->BlockSize ? BootBlockIo->Media->BlockSize : 2048;
            SIZE_T Granularity = BootBlockIo->Media->OptimalTransferLengthGranularity;
            SIZE_T DesiredSize;

            if (Granularity != 0)
            {
                DesiredSize = BlockSize * Granularity;
            }
            else
            {
                DesiredSize = BlockSize * 512;
            }

            if (DesiredSize < (SIZE_T)(1024 * 1024))
                DesiredSize = (SIZE_T)(1024 * 1024);

            if (DesiredSize > (SIZE_T)(8 * 1024 * 1024))
                DesiredSize = (SIZE_T)(8 * 1024 * 1024);

            DesiredSize = (DesiredSize + EFI_PAGE_SIZE - 1) & ~((SIZE_T)EFI_PAGE_SIZE - 1);

            if (DesiredSize > DiskReadBufferSize)
            {
                PVOID NewBuffer = MmAllocateMemoryWithType(DesiredSize, LoaderFirmwareTemporary);
                if (NewBuffer)
                {
                    if (DiskReadBuffer)
                        MmFreeMemory(DiskReadBuffer);

                    DiskReadBuffer = NewBuffer;
                    DiskReadBufferSize = DesiredSize;
                }
            }
        }

        /* Sanity-check read buffer size */
        PVOID ReadBuffer;
        BOOLEAN TempBufferAllocated = FALSE;

        if (BlocksToRead * BlockSize > DiskReadBufferSize)
        {
            ULONG NewSize = BlocksToRead * BlockSize;
            ReadBuffer = FrLdrTempAlloc(NewSize, TAG_HW_DISK_CONTEXT);
            if (!ReadBuffer)
            {
                ERR("Failed to allocate %lu bytes for CD checksum\n", NewSize);
                return FALSE;
            }
            TempBufferAllocated = TRUE;
        }
        else
        {
            ReadBuffer = DiskReadBuffer;
        }

        /* Read the ISO primary volume descriptor (at logical block 16) */
        Status = BootBlockIo->ReadBlocks(BootBlockIo,
                                         BootBlockIo->Media->MediaId,
                                         16ULL,
                                         BlocksToRead * BlockSize,
                                         ReadBuffer);
        if (EFI_ERROR(Status))
        {
            ERR("ReadBlocks for CD checksum failed (Status=%lx)\n", (ULONG_PTR)Status);
            if (TempBufferAllocated)
                FrLdrTempFree(ReadBuffer, TAG_HW_DISK_CONTEXT);
            return FALSE;
        }

        Buffer = (ULONG*)ReadBuffer;
        Mbr = (PMASTER_BOOT_RECORD)ReadBuffer;

        Signature = Mbr->Signature;
        TRACE("Signature: %x\n", Signature);

        /* Calculate the MBR checksum */
        ChecksumBytes = min(BlocksToRead * BlockSize, (ULONG)2048);
        for (i = 0; i < ChecksumBytes / sizeof(ULONG); i++)
        {
            Checksum += Buffer[i];
        }
        Checksum = ~Checksum + 1;
        TRACE("Checksum: %x\n", Checksum);

        /* Fill out the ARC disk block */
        AddReactOSArcDiskInfo(FrLdrBootPath, Signature, Checksum, TRUE);

        if (TempBufferAllocated)
            FrLdrTempFree(ReadBuffer, TAG_HW_DISK_CONTEXT);

        FsRegisterDevice(FrLdrBootPath, &UefiDiskVtbl);
        PcBiosDiskCount++; // This is not accounted for in the number of pre-enumerated BIOS drives!
        TRACE("Additional boot drive detected: 0x%02X\n", (int)FrldrBootDrive);
    }
    return TRUE;
}

UCHAR
UefiGetFloppyCount(VOID)
{
    /* No floppy for you for now... */
    return 0;
}

BOOLEAN
UefiDiskReadLogicalSectors(
    IN UCHAR DriveNumber,
    IN ULONGLONG SectorNumber,
    IN ULONG SectorCount,
    OUT PVOID Buffer)
{
    ULONG UefiDriveNumber;

    UefiDriveNumber = InternalUefiDisk[DriveNumber - FIRST_BIOS_DISK].UefiRootNumber;
    //TODO keep this TRACE("UefiDiskReadLogicalSectors: DriveNumber: %d\n", UefiDriveNumber);
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);

    /* Devices setup */
    bio->ReadBlocks(bio, bio->Media->MediaId, SectorNumber, SectorCount * bio->Media->BlockSize, Buffer);
    return TRUE;
}

BOOLEAN
UefiDiskGetDriveGeometry(UCHAR DriveNumber, PGEOMETRY Geometry)
{
    ULONG UefiDriveNumber;

    UefiDriveNumber = InternalUefiDisk[DriveNumber - FIRST_BIOS_DISK].UefiRootNumber;
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    Geometry->Cylinders = 1; // Not relevant for the UEFI BIO protocol
    Geometry->Heads = 1;     // Not relevant for the UEFI BIO protocol
    Geometry->SectorsPerTrack = (bio->Media->LastBlock + 1);
    Geometry->BytesPerSector = bio->Media->BlockSize;
    Geometry->Sectors = (bio->Media->LastBlock + 1);

    return TRUE;
}

ULONG
UefiDiskGetCacheableBlockCount(UCHAR DriveNumber)
{
    ULONG UefiDriveNumber = InternalUefiDisk[DriveNumber - FIRST_BIOS_DISK].UefiRootNumber;
    TRACE("UefiDiskGetCacheableBlockCount: DriveNumber: %d\n", UefiDriveNumber);

    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    return (bio->Media->LastBlock + 1);
}
