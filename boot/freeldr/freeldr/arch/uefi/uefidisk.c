/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Disk Access Functions
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <uefildr.h>
#include <uefi/uefiarcname.h>
#include <arch/uefi/machuefi.h>
#include <DevicePath.h>  /* EFI_DEVICE_PATH_PROTOCOL, helpers */
#include <disk.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(WARNING);

#define TAG_HW_RESOURCE_LIST    'lRwH'
#define TAG_HW_DISK_CONTEXT     'cDwH'
#define FIRST_PARTITION 1
#define MAX_PARTITION_SEARCH 128

typedef struct tagDISKCONTEXT
{
    UCHAR DriveNumber;
    ULONG SectorSize;
    ULONGLONG SectorOffset;
    ULONGLONG SectorCount;
    ULONGLONG SectorNumber;
    /* UEFI plumbed info */
    EFI_HANDLE BlockHandle;
    CHAR ArcDevicePath[128];
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
static EFI_GUID LoadedImageProtocolGuid = EFI_LOADED_IMAGE_PROTOCOL_GUID;

#ifndef DevicePathNodeLength
#define DevicePathNodeLength(Node) ((Node)->Length[0] | ((Node)->Length[1] << 8))
#endif

static
BOOLEAN
UefiDevicePathMatchesParentDisk(
    _In_ EFI_DEVICE_PATH_PROTOCOL* CandidateDiskPath,
    _In_ EFI_DEVICE_PATH_PROTOCOL* PartitionPath)
{
    EFI_DEVICE_PATH_PROTOCOL* DiskNode = CandidateDiskPath;
    EFI_DEVICE_PATH_PROTOCOL* PartNode = PartitionPath;

    if (!CandidateDiskPath || !PartitionPath)
        return FALSE;

    while (!IsDevicePathEnd(PartNode))
    {
        if (PartNode->Type == MEDIA_DEVICE_PATH && PartNode->SubType == MEDIA_HARDDRIVE_DP)
        {
            /* The disk path should end before the hard-drive node */
            return IsDevicePathEnd(DiskNode);
        }

        if (IsDevicePathEnd(DiskNode))
            return FALSE;

        UINTN LenPart = DevicePathNodeLength(PartNode);
        UINTN LenDisk = DevicePathNodeLength(DiskNode);
        if (LenPart != LenDisk)
            return FALSE;

        if (memcmp(PartNode, DiskNode, LenPart) != 0)
            return FALSE;

        PartNode = NextDevicePathNode(PartNode);
        DiskNode = NextDevicePathNode(DiskNode);
    }

    return FALSE;
}

static
BOOLEAN
UefiDetectIsoVolume(_In_ EFI_BLOCK_IO* BlockIo)
{
    EFI_STATUS Status;
    VOID* Buffer = NULL;
    UINT32 BlockSize;
    UINT64 IsoOffsetBytes;
    UINT64 ReadLba;
    UINTN  ReadSize;
    UINTN  DescriptorOffset;
    UINTN  BlockCount;

    if (!BlockIo || !BlockIo->Media || BlockIo->Media->BlockSize == 0)
        return FALSE;

    if (BlockIo->Media->LogicalPartition)
        return FALSE;

    if (!BlockIo->Media->MediaPresent)
        return FALSE;

    BlockSize = BlockIo->Media->BlockSize;
    IsoOffsetBytes = 16ull * 2048ull; /* ISO9660 PVD offset */

    ReadLba = IsoOffsetBytes / BlockSize;
    DescriptorOffset = (UINTN)(IsoOffsetBytes % BlockSize);
    ReadSize = 2048 + DescriptorOffset;
    if (ReadSize % BlockSize)
    {
        UINTN BlocksNeeded = (ReadSize + BlockSize - 1) / BlockSize;
        ReadSize = BlocksNeeded * BlockSize;
    }

    BlockCount = ReadSize / BlockSize;

    if (BlockIo->Media->LastBlock < ReadLba + BlockCount - 1)
        return FALSE;

    Status = GlobalSystemTable->BootServices->AllocatePool(EfiLoaderData, ReadSize, &Buffer);
    if (EFI_ERROR(Status) || !Buffer)
        return FALSE;

    Status = BlockIo->ReadBlocks(BlockIo,
                                 BlockIo->Media->MediaId,
                                 ReadLba,
                                 ReadSize,
                                 Buffer);
    if (EFI_ERROR(Status))
    {
        GlobalSystemTable->BootServices->FreePool(Buffer);
        return FALSE;
    }

    UINT8* Descriptor = (UINT8*)Buffer + DescriptorOffset;
    BOOLEAN IsIso = FALSE;
    if (Descriptor[0] == 0x01 &&
        Descriptor[1] == 'C' && Descriptor[2] == 'D' &&
        Descriptor[3] == '0' && Descriptor[4] == '0' && Descriptor[5] == '1')
    {
        IsIso = TRUE;
    }

    GlobalSystemTable->BootServices->FreePool(Buffer);
    return IsIso;
}

BOOLEAN
UefiIsCdRomHandle(IN EFI_HANDLE Handle)
{
    EFI_DEVICE_PATH_PROTOCOL* DevicePath = NULL;

    if (!EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
            Handle, &DevicePathProtocolGuid, (VOID**)&DevicePath)) &&
        DevicePath)
    {
        EFI_DEVICE_PATH_PROTOCOL* Node = DevicePath;
        while (!IsDevicePathEnd(Node))
        {
            if (Node->Type == MEDIA_DEVICE_PATH && Node->SubType == MEDIA_CDROM_DP)
                return TRUE;
            Node = NextDevicePathNode(Node);
        }
    }

    /*
     * Some firmware (notably SATA/USB bridges) expose optical media via a
     * hard-drive style device path. Fall back to Block I/O heuristics so these
     * devices are still classified as CDs.
     */
    EFI_BLOCK_IO* BlockIo = NULL;
    if (!EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
            Handle, &bioGuid, (VOID**)&BlockIo)) &&
        BlockIo && BlockIo->Media && !BlockIo->Media->LogicalPartition)
    {
        if (!BlockIo->Media->MediaPresent)
            return FALSE;

        if (BlockIo->Media->ReadOnly)
        {
            if (BlockIo->Media->BlockSize == 2048 ||
                UefiDetectIsoVolume(BlockIo))
            {
                TRACE("UefiIsCdRomHandle: heuristic matched read-only media (BlockSize=%u)\n",
                      BlockIo->Media->BlockSize);
                return TRUE;
            }

            TRACE("UefiIsCdRomHandle: read-only media without ISO signature, assuming CD-ROM\n");
            return TRUE;
        }

        if (UefiDetectIsoVolume(BlockIo))
        {
            TRACE("UefiIsCdRomHandle: detected ISO9660 volume on writable/removable media\n");
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
UefiIsUsbHandle(IN EFI_HANDLE Handle)
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
        if (Node->Type == MESSAGING_DEVICE_PATH)
        {
#ifdef MSG_USB_DP
            if (Node->SubType == MSG_USB_DP)
                return TRUE;
#endif
#ifdef MSG_USB_CLASS_DP
            if (Node->SubType == MSG_USB_CLASS_DP)
                return TRUE;
#endif
#ifdef MSG_USB_WWID_DP
            if (Node->SubType == MSG_USB_WWID_DP)
                return TRUE;
#endif
#ifdef MSG_USB_HOST_DP
            if (Node->SubType == MSG_USB_HOST_DP)
                return TRUE;
#endif
        }
        Node = NextDevicePathNode(Node);
    }

    /*
     * Fall back to Block I/O heuristics: many USB mass-storage bridges expose
     * themselves as hard disks but still flag the media as removable.
     */
    EFI_BLOCK_IO* BlockIo = NULL;
    if (!EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
            Handle, &bioGuid, (VOID**)&BlockIo)) &&
        BlockIo && BlockIo->Media && BlockIo->Media->RemovableMedia)
    {
        TRACE("UefiIsUsbHandle: Block I/O reports removable media; treating as USB\n");
        return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
UefiHandleGetPartitionInfo(
    IN EFI_HANDLE Handle,
    OUT PULONG PartitionNumber OPTIONAL,
    OUT PUINT64 PartitionStart OPTIONAL)
{
    EFI_DEVICE_PATH_PROTOCOL* DevicePath = NULL;

    if (PartitionNumber)
        *PartitionNumber = 0;
    if (PartitionStart)
        *PartitionStart = 0;

    if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
            Handle, &DevicePathProtocolGuid, (VOID**)&DevicePath)) ||
        !DevicePath)
    {
        return FALSE;
    }

    EFI_DEVICE_PATH_PROTOCOL* Node = DevicePath;
    while (!IsDevicePathEnd(Node))
    {
        if ((Node->Type == MEDIA_DEVICE_PATH) && (Node->SubType == MEDIA_HARDDRIVE_DP))
        {
            HARDDRIVE_DEVICE_PATH* Hd = (HARDDRIVE_DEVICE_PATH*)Node;
            if (PartitionNumber)
                *PartitionNumber = Hd->PartitionNumber;
            if (PartitionStart)
                *PartitionStart = Hd->PartitionStart;
            TRACE("UefiHandleGetPartitionInfo: PartitionNumber=%lu StartLBA=%I64u\n",
                  Hd->PartitionNumber, Hd->PartitionStart);
            return TRUE;
        }

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
    BOOLEAN HasEntry = FALSE;
    PARTITION_TABLE_ENTRY LocalEntry;
    PPARTITION_TABLE_ENTRY TargetEntry;
    ULONG DevicePathPartition = 0;
    UINT64 DevicePathStartLba = 0;
    BOOLEAN HaveDevicePathInfo = FALSE;

    TRACE("UefiGetBootPartitionEntry: DriveNumber: %d\n", DriveNumber - FIRST_BIOS_DISK);
    /* UefiBootRoot is the offset into the array of handles where the raw disk of the boot drive is.
     * Partitions start with 1 in ARC, but UEFI root drive identitfier is also first partition. */
    PartitionNum = (OffsetToBoot - UefiBootRootIdentifier);

    if (PublicBootHandle != NULL)
    {
        HaveDevicePathInfo = UefiHandleGetPartitionInfo(PublicBootHandle,
                                                        &DevicePathPartition,
                                                        &DevicePathStartLba);
        if (HaveDevicePathInfo && DevicePathPartition != 0)
        {
            TRACE("UefiGetBootPartitionEntry: DevicePath reports partition %lu (start LBA %I64u)\n",
                  DevicePathPartition, DevicePathStartLba);
            PartitionNum = DevicePathPartition;
        }
        else if (HaveDevicePathInfo)
        {
            TRACE("UefiGetBootPartitionEntry: DevicePath reports no partition number (start LBA %I64u)\n",
                  DevicePathStartLba);
        }
    }

    if (PartitionNum == 0 || PartitionNum == 0xFF)
    {
        TRACE("Boot PartitionNumber fallback (PartitionNum=%lu)\n", PartitionNum);
        PartitionNum = FIRST_PARTITION;
    }

    TargetEntry = PartitionTableEntry ? PartitionTableEntry : &LocalEntry;
    RtlZeroMemory(TargetEntry, sizeof(*TargetEntry));

    DiskDetectPartitionType(DriveNumber);

    if (PartitionNum >= FIRST_PARTITION && PartitionNum != 0xFF)
    {
        HasEntry = DiskGetPartitionEntry(DriveNumber, PartitionNum, TargetEntry);
    }

    if (!HasEntry && HaveDevicePathInfo && DevicePathStartLba != 0)
    {
        TRACE("UefiGetBootPartitionEntry: searching by start LBA %I64u\n", DevicePathStartLba);

        /* Obtain device block size for proper LBA unit conversion */
        EFI_BLOCK_IO* QueryBlockIo = NULL;
        ULONG QueryBlockSize = 512;
        do {
            ULONG RootIndex = InternalUefiDisk[(DriveNumber >= FIRST_BIOS_DISK) ? (DriveNumber - FIRST_BIOS_DISK) : 0].UefiRootNumber;
            EFI_HANDLE H = handles[RootIndex];
            if (!EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(H, &bioGuid, (void**)&QueryBlockIo)) &&
                QueryBlockIo && QueryBlockIo->Media && QueryBlockIo->Media->BlockSize)
            {
                QueryBlockSize = QueryBlockIo->Media->BlockSize;
            }
        } while (0);
        for (ULONG idx = FIRST_PARTITION; idx < FIRST_PARTITION + MAX_PARTITION_SEARCH; ++idx)
        {
            PARTITION_TABLE_ENTRY SearchEntry;
            RtlZeroMemory(&SearchEntry, sizeof(SearchEntry));

            if (!DiskGetPartitionEntry(DriveNumber, idx, &SearchEntry))
                continue;

            if (SearchEntry.PartitionSectorCount == 0 &&
                SearchEntry.SystemIndicator == PARTITION_ENTRY_UNUSED)
            {
                continue;
            }

            /* Convert 512-byte based MBR/GPT LBAs to device logical blocks */
            ULONGLONG SearchStartLbaDev = ((ULONGLONG)SearchEntry.SectorCountBeforePartition * 512ULL) / (ULONGLONG)max(1u, QueryBlockSize);
            if (SearchStartLbaDev == DevicePathStartLba)
            {
                RtlCopyMemory(TargetEntry, &SearchEntry, sizeof(*TargetEntry));
                PartitionNum = idx;
                HasEntry = TRUE;
                TRACE("UefiGetBootPartitionEntry: matched partition index %lu via start LBA\n", idx);
                break;
            }
        }
    }

    if (!PartitionTableEntry)
    {
        RtlZeroMemory(&LocalEntry, sizeof(LocalEntry));
    }

    *BootPartition = PartitionNum;
    TRACE("UefiGetBootPartitionEntry: Boot Partition is: %d (entry %s)\n",
          PartitionNum,
          HasEntry ? "ok" : "missing");

    return HasEntry;
}

BOOLEAN
UefiDiskIsUsb(IN UCHAR DriveNumber)
{
    ULONG UefiDriveNumber;
    BOOLEAN Result;

    TRACE("UefiDiskIsUsb: DriveNumber=0x%02x\n", DriveNumber);

    /* Check if this is a BIOS disk */
    if (DriveNumber < FIRST_BIOS_DISK)
    {
        TRACE("UefiDiskIsUsb: Not a BIOS disk (< 0x80)\n");
        return FALSE;
    }

    /* Convert to UEFI drive number */
    UefiDriveNumber = DriveNumber - FIRST_BIOS_DISK;
    TRACE("UefiDiskIsUsb: UefiDriveNumber=%lu, PcBiosDiskCount=%u\n", UefiDriveNumber, PcBiosDiskCount);

    /* Check if the drive number is valid */
    if (UefiDriveNumber >= PcBiosDiskCount)
    {
        TRACE("UefiDiskIsUsb: Drive number out of range\n");
        return FALSE;
    }

    /* Check if we have the internal disk info */
    if (InternalUefiDisk == NULL || handles == NULL)
    {
        TRACE("UefiDiskIsUsb: No internal disk info available\n");
        return FALSE;
    }

    /* Get the UEFI handle index for this disk */
    ULONG UefiHandleIndex = InternalUefiDisk[UefiDriveNumber].UefiRootNumber;
    TRACE("UefiDiskIsUsb: UefiHandleIndex=%lu\n", UefiHandleIndex);

    /* Check if this handle represents a USB device */
    Result = UefiIsUsbHandle(handles[UefiHandleIndex]);
    TRACE("UefiDiskIsUsb: UefiIsUsbHandle returned %s\n", Result ? "TRUE" : "FALSE");

    return Result;
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
    BOOLEAN IsCdPath;
    EFI_HANDLE DeviceHandle = NULL;
    EFI_BLOCK_IO* BlockIo = NULL;

    if (DiskReadBufferSize == 0)
    {
        ERR("DiskOpen(): DiskReadBufferSize is 0, something is wrong.\n");
        ASSERT(FALSE);
        return ENOMEM;
    }

    if (!DissectArcPath(Path, NULL, &DriveNumber, &DrivePartition))
        return EINVAL;

    UefiDriveNumber = DriveNumber - FIRST_BIOS_DISK;
    IsCdPath = (DrivePartition == 0xff);

    if (IsCdPath)
    {
        if (UefiDriveNumber >= UefiGetCdromCount())
            return EINVAL;

        DeviceHandle = UefiGetCdromHandle(UefiDriveNumber);
        if (!DeviceHandle)
            return EINVAL;

        if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(DeviceHandle, &bioGuid, (void**)&BlockIo)) ||
            !BlockIo || !BlockIo->Media)
        {
            return EIO;
        }

        bio = BlockIo;
        SectorSize = bio->Media->BlockSize ? bio->Media->BlockSize : 2048;
        SectorOffset = 0;
        SectorCount = bio->Media->LastBlock + 1;

        /* Remap CD-ROM drive numbers to avoid collisions with HDD 0x80 */
        DriveNumber = (UCHAR)(0xE0 + UefiDriveNumber);
    }
    else
    {
        ULONG RootIndex;

        if (UefiDriveNumber >= PcBiosDiskCount)
            return EINVAL;

        RootIndex = InternalUefiDisk[UefiDriveNumber].UefiRootNumber;
        DeviceHandle = handles[RootIndex];

        if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(DeviceHandle, &bioGuid, (void**)&BlockIo)) ||
            !BlockIo || !BlockIo->Media)
        {
            return EIO;
        }

        bio = BlockIo;
        SectorSize = bio->Media->BlockSize;

        if (DrivePartition != 0)
        {
            if (!DiskGetPartitionEntry(DriveNumber, DrivePartition, &PartitionTableEntry))
                return EINVAL;

            /*
             * MBR/GPT partition LBAs are expressed in 512-byte sectors.
             * Convert them to the current device's Block I/O sector size
             * so that subsequent reads using BlockIo are correctly aligned.
             */
            SectorOffset = PartitionTableEntry.SectorCountBeforePartition;
            SectorCount = PartitionTableEntry.PartitionSectorCount;

            if (SectorSize != 0 && SectorSize != 512)
            {
                ULONGLONG offsetBytes = (ULONGLONG)SectorOffset * 512ULL;
                ULONGLONG countBytes  = (ULONGLONG)SectorCount * 512ULL;

                SectorOffset = (ULONGLONG)(offsetBytes / SectorSize);
                SectorCount  = (ULONGLONG)(countBytes  / SectorSize);
            }
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
    }

    Context = FrLdrTempAlloc(sizeof(DISKCONTEXT), TAG_HW_DISK_CONTEXT);
    if (!Context)
        return ENOMEM;

    Context->DriveNumber = DriveNumber;
    Context->SectorSize = SectorSize;
    Context->SectorOffset = SectorOffset;
    Context->SectorCount = SectorCount;
    Context->SectorNumber = 0;
    Context->BlockHandle = DeviceHandle;
    if (Path)
        RtlStringCbCopyA(Context->ArcDevicePath, sizeof(Context->ArcDevicePath), Path);
    else
        Context->ArcDevicePath[0] = '\0';
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

/*
 * Expose helpers to retrieve the UEFI block handle and ARC device path
 * associated with a given FileId opened via UefiDiskOpen.
 */
EFI_HANDLE
UefiGetBlockHandleForFileId(ULONG FileId)
{
    DISKCONTEXT* Context = FsGetDeviceSpecific(FileId);
    return Context ? Context->BlockHandle : NULL;
}

PCCHAR
UefiGetArcPathForFileId(ULONG FileId)
{
    DISKCONTEXT* Context;
    ULONG DeviceId;

    /* Get the underlying disk device from the file */
    DeviceId = FsGetDeviceId(FileId);
    if (DeviceId == INVALID_FILE_ID)
        return NULL;

    /* Get the disk context from the device */
    Context = FsGetDeviceSpecific(DeviceId);
    return (Context && Context->ArcDevicePath[0]) ? Context->ArcDevicePath : NULL;
}

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

                for (j = 0; j < PcBiosDiskCount; ++j)
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
            else
            {
                EFI_DEVICE_PATH_PROTOCOL* BootPartitionPath = NULL;
                if (!EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
                        handles[i], &DevicePathProtocolGuid, (VOID**)&BootPartitionPath)) &&
                    BootPartitionPath)
                {
                    for (ULONG root = 0; root < SystemHandleCount; ++root)
                    {
                        EFI_BLOCK_IO* RootBio;
                        EFI_DEVICE_PATH_PROTOCOL* RootPath = NULL;

                        if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
                                handles[root], &bioGuid, (VOID**)&RootBio)) ||
                            !RootBio || RootBio->Media->LogicalPartition)
                        {
                            continue;
                        }

                        if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
                                handles[root], &DevicePathProtocolGuid, (VOID**)&RootPath)) ||
                            !RootPath)
                        {
                            continue;
                        }

                        if (UefiDevicePathMatchesParentDisk(RootPath, BootPartitionPath))
                        {
                            ULONG j;

                            TRACE("Boot partition maps to root index %lu\n", root);
                            UefiBootRootIdentifier = root;
                            for (j = 0; j < PcBiosDiskCount; ++j)
                            {
                                if (InternalUefiDisk[j].UefiRootNumber == UefiBootRootIdentifier)
                                {
                                    InternalUefiDisk[j].IsThisTheBootDrive = TRUE;
                                    PublicBootArcDisk = j;
                                    TRACE("Found Boot drive via partition mapping\n");
                                    break;
                                }
                            }
                            break;
                        }
                    }
                }
            }
        }
    }

ULONG
UefiGetPhysicalDiskCount(VOID)
{
    return PcBiosDiskCount;
}

EFI_HANDLE
UefiGetPhysicalDiskHandle(ULONG ArcIndex)
{
    if (ArcIndex >= PcBiosDiskCount)
        return NULL;
    return handles[InternalUefiDisk[ArcIndex].UefiRootNumber];
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

   EFI_LOADED_IMAGE_PROTOCOL* LoadedImage = NULL;
   EFI_HANDLE BootHandle = NULL;
   EFI_BLOCK_IO* BootBlockIo = NULL;
   EFI_BLOCK_IO* BootMediaIo = bio;
   EFI_BLOCK_IO* RootDiskIo = NULL;
   EFI_BLOCK_IO* BootClassifyIo = bio;
   BOOLEAN BootHandleIsPartition = FALSE;

   if (!EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
           GlobalImageHandle,
           &LoadedImageProtocolGuid,
           (VOID**)&LoadedImage)) &&
       LoadedImage && LoadedImage->DeviceHandle)
   {
        BootHandle = LoadedImage->DeviceHandle;
        if (!EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
                BootHandle,
                &bioGuid,
                (VOID**)&BootBlockIo)) &&
            BootBlockIo)
        {
            BootMediaIo = BootBlockIo;
            if (BootBlockIo->Media)
            {
                TRACE("Boot handle media: Removable=%d BlockSize=%u LogicalPartition=%d ReadOnly=%d\n",
                      BootBlockIo->Media->RemovableMedia,
                      BootBlockIo->Media->BlockSize,
                      BootBlockIo->Media->LogicalPartition,
                      BootBlockIo->Media->ReadOnly);
                BootHandleIsPartition = BootBlockIo->Media->LogicalPartition ? TRUE : FALSE;
                BootClassifyIo = BootBlockIo;
                if (BootHandleIsPartition)
                {
                    TRACE("Boot handle is a logical partition; using partition media for heuristics\n");
                }
            }
        }
   }

   if (!EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
           handles[UefiBootRootIdentifier],
           &bioGuid,
           (VOID**)&RootDiskIo)) &&
       RootDiskIo && RootDiskIo->Media && RootDiskIo->Media->MediaPresent)
   {
        TRACE("Root disk media: Removable=%d BlockSize=%u LogicalPartition=%d ReadOnly=%d\n",
              RootDiskIo->Media->RemovableMedia,
              RootDiskIo->Media->BlockSize,
              RootDiskIo->Media->LogicalPartition,
              RootDiskIo->Media->ReadOnly);
   }
   else
   {
        RootDiskIo = NULL;
   }

   if (BootMediaIo && BootMediaIo->Media)
   {
        TRACE("Boot media: Removable=%d BlockSize=%u LogicalPartition=%d ReadOnly=%d\n",
              BootMediaIo->Media->RemovableMedia,
              BootMediaIo->Media->BlockSize,
              BootMediaIo->Media->LogicalPartition,
              BootMediaIo->Media->ReadOnly);
   }

   BOOLEAN BootHandleIsCd = FALSE;
   if (BootHandle && (!BootBlockIo || !BootBlockIo->Media || !BootHandleIsPartition))
   {
        BootHandleIsCd = UefiIsCdRomHandle(BootHandle);
        if (BootHandleIsCd)
        {
            TRACE("Boot device handle classified as CD-ROM; forcing ISO/CD semantics\n");
        }
   }
   else if (BootHandle && BootHandleIsPartition)
   {
        TRACE("Boot device handle represents a logical partition; skipping CD-ROM override\n");
   }

   ULONG BootPartition = 0;
   PARTITION_TABLE_ENTRY PartitionEntry;
   BOOLEAN HasPartitionInfo = FALSE;
   BOOLEAN TreatAsCd = UefiIsCdRomHandle(handles[UefiBootRootIdentifier]);
   BOOLEAN IsUsbBoot = UefiIsUsbHandle(handles[UefiBootRootIdentifier]);
   ULONG DevPathPartition = 0;
   UINT64 DevPathStart = 0;
   EFI_HANDLE PartitionInfoHandle = handles[UefiBootRootIdentifier];

   if (BootHandle && BootHandleIsPartition)
   {
        PartitionInfoHandle = BootHandle;
   }

   if (BootHandleIsCd)
   {
        TreatAsCd = TRUE;
        HasPartitionInfo = FALSE;
        BootPartition = 0;
        PartitionInfoHandle = BootHandle;
   }

   /*
    * Some firmwares report a logical partition even when booting from a
    * raw ISO-on-disk image. Probe the parent disk for an ISO9660 PVD before
    * trusting any partition metadata.
    */
   if (!TreatAsCd && RootDiskIo &&
       !RootDiskIo->Media->LogicalPartition && RootDiskIo->Media->MediaPresent)
   {
        if (UefiDetectIsoVolume(RootDiskIo))
        {
            TRACE("Root disk contains ISO9660 data; switching to CD semantics\n");
            TreatAsCd = TRUE;
            HasPartitionInfo = FALSE;
            BootPartition = 0;
            PartitionInfoHandle = handles[UefiBootRootIdentifier];
            BootHandleIsPartition = FALSE;
            BootClassifyIo = RootDiskIo;
        }
   }

   if (!EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(
           handles[UefiBootRootIdentifier],
           &bioGuid,
           (VOID**)&RootDiskIo)) &&
       RootDiskIo && RootDiskIo->Media && !RootDiskIo->Media->MediaPresent)
   {
        RootDiskIo = NULL;
   }

   BOOLEAN DevPathHasPartition =
       UefiHandleGetPartitionInfo(PartitionInfoHandle,
                                  &DevPathPartition,
                                  &DevPathStart);

   RtlZeroMemory(&PartitionEntry, sizeof(PartitionEntry));

   if (!TreatAsCd &&
       UefiGetBootPartitionEntry(FrldrBootDrive, &PartitionEntry, &BootPartition) &&
       BootPartition != 0)
   {
        BOOLEAN PartitionLooksValid = (PartitionEntry.PartitionSectorCount != 0 &&
                                       PartitionEntry.SystemIndicator != PARTITION_ENTRY_UNUSED);

        TRACE("Boot partition entry: type=0x%02x offset=0x%I64x length=0x%I64x (valid=%d)\n",
              PartitionEntry.SystemIndicator,
              PartitionEntry.SectorCountBeforePartition,
              PartitionEntry.PartitionSectorCount,
              PartitionLooksValid);

        HasPartitionInfo = PartitionLooksValid;
   }
   else if (BootHandleIsPartition && !HasPartitionInfo)
   {
        TRACE("Boot handle reports a partition but no valid entry was found; reusing root disk for heuristics\n");
        BootHandleIsPartition = FALSE;
        PartitionInfoHandle = handles[UefiBootRootIdentifier];
        if (RootDiskIo)
        {
            BootClassifyIo = RootDiskIo;
            TRACE("Using root disk Block I/O (%p) for media classification\n", RootDiskIo);
        }
   }

   TRACE("Device path partition info: has=%d partition=%lu start=%I64u\n",
         DevPathHasPartition,
         DevPathPartition,
         DevPathStart);

   if (TreatAsCd && HasPartitionInfo)
   {
        if (DevPathHasPartition && DevPathPartition != 0)
        {
            TRACE("UEFI device path targets a partition; CD->HDD\n");
            TreatAsCd = FALSE;
        }
        else
        {
            TRACE("Partitions exist, but boot path is not partition-specific; stay as CD\n");
        }
   }

   EFI_BLOCK_IO* IsoProbeIo = BootClassifyIo;
   if (BootHandleIsPartition && RootDiskIo)
   {
        IsoProbeIo = RootDiskIo;
   }

   if (!TreatAsCd && IsoProbeIo && IsoProbeIo->Media &&
       !IsoProbeIo->Media->LogicalPartition && IsoProbeIo->Media->MediaPresent)
   {
        if (UefiDetectIsoVolume(IsoProbeIo))
        {
            TRACE("Detected ISO9660 signature on boot media; forcing ISO/CD semantics\n");
            TreatAsCd = TRUE;
            HasPartitionInfo = FALSE;
            BootPartition = 0;
        }
   }

   /*
    * Treat removable 2048-byte block media without partition info as ISO/CD.
    * USB devices only follow this path when firmware reports them read-only,
    * so writable USB mass storage stays in HDD mode while true USB CD-ROMs
    * still keep optical semantics.
    */
   if (!TreatAsCd && BootClassifyIo && BootClassifyIo->Media &&
       BootClassifyIo->Media->RemovableMedia == TRUE &&
       BootClassifyIo->Media->BlockSize == 2048 && !HasPartitionInfo &&
       (!IsUsbBoot || BootClassifyIo->Media->ReadOnly))
   {
        TRACE("Removable 2048-byte media without partitions, treating as ISO/CD\n");
        TreatAsCd = TRUE;
   }

   if (!TreatAsCd && BootClassifyIo && BootClassifyIo->Media &&
       BootClassifyIo->Media->ReadOnly && !HasPartitionInfo)
   {
        TRACE("Read-only media without partition info, treating as ISO/CD\n");
        TreatAsCd = TRUE;
   }

   /* For USB boot media, prefer HDD mode only when firmware targeted a partition */
   if (IsUsbBoot)
   {
        TRACE("USB boot media detected by UefiIsUsbHandle\n");
        if (HasPartitionInfo)
        {
            if (DevPathHasPartition && DevPathPartition != 0)
            {
                TRACE("USB device path names partition %lu; forcing HDD mode\n",
                      DevPathPartition);
                TreatAsCd = FALSE;
            }
            else
            {
                TRACE("USB partitions present but boot path is not partition-specific; keeping CD semantics\n");
            }
        }
        else
        {
            /* USB without partitions - typically ISO (USB CD-ROM) or raw FAT */
            TRACE("USB boot media without partition info detected\n");
            /* If removable 2K-block media, TreatAsCd was already set above. */
        }
   }
   else
   {
        TRACE("Not USB boot media (IsUsbBoot=%d)\n", IsUsbBoot);
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
        EFI_HANDLE CdHandle = BootHandleIsCd ? BootHandle : handles[UefiBootRootIdentifier];
        ULONG CdIndex = MapToCdromIndex(CdHandle);
        UefiBootHasDiskArc = FALSE;
        UefiBootDiskArcNumber = 0;
        UefiBootDiskArcPartition = 0;
        FrldrBootPartition = 0xFF;
        RtlStringCbPrintfA(FrLdrBootPath, sizeof(FrLdrBootPath),
                           "multi(0)disk(0)cdrom(%lu)", CdIndex);
        TRACE("UefiSetBootpath: provisional path = '%s' (will be validated by INI open)\n", FrLdrBootPath);
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
        TRACE("UefiSetBootpath: provisional path = '%s' (will be validated by INI open)\n", FrLdrBootPath);
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

    /* Register all CD-ROM devices so fallback logic can find them */
    {
        ULONG CdCount = UefiGetCdromCount();
        for (i = 0; i < CdCount; i++)
        {
            CHAR CdArcName[64];
            RtlStringCbPrintfA(CdArcName, sizeof(CdArcName), "multi(0)disk(0)cdrom(%lu)", i);
            FsRegisterDevice(CdArcName, &UefiDiskVtbl);
        }
    }

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
                WARN("Failed to allocate %lu bytes for CD checksum; skipping checksum\n", NewSize);
                ReadBuffer = NULL;
            }
            else
            {
                TempBufferAllocated = TRUE;
            }
        }
        else
        {
            ReadBuffer = DiskReadBuffer;
        }

        if (ReadBuffer)
        {
            /* Read the ISO primary volume descriptor (at logical block 16) */
            Status = BootBlockIo->ReadBlocks(BootBlockIo,
                                             BootBlockIo->Media->MediaId,
                                             16ULL,
                                             BlocksToRead * BlockSize,
                                             ReadBuffer);
            if (EFI_ERROR(Status))
            {
                WARN("ReadBlocks for CD checksum failed (Status=%lx); skipping checksum\n",
                     (ULONG_PTR)Status);
                if (TempBufferAllocated)
                    FrLdrTempFree(ReadBuffer, TAG_HW_DISK_CONTEXT);
                ReadBuffer = NULL;
            }
        }

        if (ReadBuffer)
        {
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
        }
        else
        {
            /* Continue boot even when checksum can't be computed */
            AddReactOSArcDiskInfo(FrLdrBootPath, 0, 0, FALSE);
        }

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

    if (DriveNumber >= 0xE0)
    {
        ULONG CdIndex = DriveNumber - 0xE0;
        EFI_BLOCK_IO* BlockIo;
        EFI_HANDLE DeviceHandle = UefiGetCdromHandle(CdIndex);
        if (!DeviceHandle)
            return FALSE;

        if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(DeviceHandle, &bioGuid, (void**)&BlockIo)) ||
            !BlockIo || !BlockIo->Media)
        {
            return FALSE;
        }

        BlockIo->ReadBlocks(BlockIo,
                             BlockIo->Media->MediaId,
                             SectorNumber,
                             SectorCount * BlockIo->Media->BlockSize,
                             Buffer);
        return TRUE;
    }

    if (DriveNumber < FIRST_BIOS_DISK)
        return FALSE;

    UefiDriveNumber = DriveNumber - FIRST_BIOS_DISK;
    if (UefiDriveNumber >= PcBiosDiskCount)
        return FALSE;

    UefiDriveNumber = InternalUefiDisk[UefiDriveNumber].UefiRootNumber;
    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);

    /* Devices setup */
    bio->ReadBlocks(bio, bio->Media->MediaId, SectorNumber, SectorCount * bio->Media->BlockSize, Buffer);
    return TRUE;
}

BOOLEAN
UefiDiskGetDriveGeometry(UCHAR DriveNumber, PGEOMETRY Geometry)
{
    ULONG UefiDriveNumber;
    EFI_HANDLE DeviceHandle;

    if (DriveNumber >= 0xE0)
    {
        ULONG CdIndex = DriveNumber - 0xE0;
        EFI_BLOCK_IO* BlockIo;

        DeviceHandle = UefiGetCdromHandle(CdIndex);
        if (!DeviceHandle)
            return FALSE;

        if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(DeviceHandle, &bioGuid, (void**)&BlockIo)) ||
            !BlockIo || !BlockIo->Media)
        {
            return FALSE;
        }

        Geometry->Cylinders = 1;
        Geometry->Heads = 1;
        Geometry->SectorsPerTrack = BlockIo->Media->LastBlock + 1;
        Geometry->BytesPerSector = BlockIo->Media->BlockSize;
        Geometry->Sectors = BlockIo->Media->LastBlock + 1;
        return TRUE;
    }

    if (DriveNumber < FIRST_BIOS_DISK)
        return FALSE;

    UefiDriveNumber = DriveNumber - FIRST_BIOS_DISK;
    if (UefiDriveNumber >= PcBiosDiskCount)
        return FALSE;

    UefiDriveNumber = InternalUefiDisk[UefiDriveNumber].UefiRootNumber;
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
    ULONG UefiDriveNumber;
    EFI_HANDLE DeviceHandle;

    if (DriveNumber >= 0xE0)
    {
        ULONG CdIndex = DriveNumber - 0xE0;
        EFI_BLOCK_IO* BlockIo;

        DeviceHandle = UefiGetCdromHandle(CdIndex);
        if (!DeviceHandle)
            return 0;

        if (EFI_ERROR(GlobalSystemTable->BootServices->HandleProtocol(DeviceHandle, &bioGuid, (void**)&BlockIo)) ||
            !BlockIo || !BlockIo->Media)
        {
            return 0;
        }

        return BlockIo->Media->LastBlock + 1;
    }

    if (DriveNumber < FIRST_BIOS_DISK)
        return 0;

    UefiDriveNumber = DriveNumber - FIRST_BIOS_DISK;
    if (UefiDriveNumber >= PcBiosDiskCount)
        return 0;

    UefiDriveNumber = InternalUefiDisk[UefiDriveNumber].UefiRootNumber;
    TRACE("UefiDiskGetCacheableBlockCount: DriveNumber: %d\n", UefiDriveNumber);

    GlobalSystemTable->BootServices->HandleProtocol(handles[UefiDriveNumber], &bioGuid, (void**)&bio);
    return (bio->Media->LastBlock + 1);
}


BOOLEAN
UefiClassifyMediaFromHandle(
    _In_ EFI_HANDLE Handle,
    _Out_ PUEFI_MEDIA_INFO Info)
{
    EFI_BLOCK_IO* BlockIo = NULL;
    EFI_STATUS Status;

    if (!Info)
        return FALSE;
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Kind = UefiMediaUnknown;
    if (!Handle)
        return FALSE;

    Status = GlobalSystemTable->BootServices->HandleProtocol(Handle, &bioGuid, (VOID**)&BlockIo);
    if (EFI_ERROR(Status) || !BlockIo || !BlockIo->Media)
        return FALSE;

    Info->Removable = BlockIo->Media->RemovableMedia;
    Info->ReadOnly  = BlockIo->Media->ReadOnly;
    Info->BlockSize = BlockIo->Media->BlockSize;
    Info->HasPartitionInfo = BlockIo->Media->LogicalPartition ? TRUE : FALSE;

    if (UefiIsCdRomHandle(Handle) || (!Info->HasPartitionInfo && Info->BlockSize == 2048))
        Info->Kind = UefiMediaCdrom;
    else
        Info->Kind = UefiMediaDisk;

    return TRUE;
}
