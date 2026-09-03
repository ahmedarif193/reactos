/*
 * PROJECT:     ReactOS storage expansion tool
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Grow the last MBR partition and its NTFS volume to the end of the disk
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include "expandcore.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#define ExpandVsnprintf _vsnprintf
#else
#define ExpandVsnprintf vsnprintf
#endif

#define ALIGN_UP8(x) (((x) + 7) & ~(uint64_t)7)

#define MBR_TABLE_OFFSET      0x1BE
#define MBR_SIGNATURE_OFFSET  0x1FE
#define MBR_SIGNATURE         0xAA55
#define MBR_ENTRY_COUNT       4
#define MBR_TYPE_EMPTY        0x00
#define MBR_TYPE_EXTENDED     0x05
#define MBR_TYPE_EXTENDED_LBA 0x0F
#define MBR_TYPE_GPT          0xEE

#define NTFS_FILE_MAGIC       0x454C4946
#define NTFS_RECORD_BITMAP    6
#define NTFS_RECORD_BADCLUS   8

#define ATTR_TYPE_DATA        0x80
#define ATTR_TYPE_END         0xFFFFFFFF
#define ATTR_COMPRESSION_MASK 0x00FF
#define ATTR_ENCRYPTED        0x4000
#define ATTR_SPARSE           0x8000
#define ATTR_NONRESIDENT_HEADER_SIZE 0x40

#define MAX_RUNS              512
#define MAX_RECORD_SIZE       16384
#define MAX_MAPPING_PAIRS     4096
#define MAX_SECTOR_SIZE       4096
#define BITMAP_CHUNK          65536

#pragma pack(push, 1)

typedef struct _MBR_ENTRY
{
    uint8_t  Status;
    uint8_t  StartChs[3];
    uint8_t  Type;
    uint8_t  EndChs[3];
    uint32_t StartLba;
    uint32_t SectorCount;
} MBR_ENTRY;

typedef struct _NTFS_BOOT_SECTOR
{
    uint8_t  JumpInstruction[3];
    uint8_t  OemId[8];
    uint16_t BytesPerSector;
    uint8_t  SectorsPerCluster;
    uint8_t  Reserved0[7];
    uint8_t  MediaDescriptor;
    uint8_t  Reserved1[2];
    uint16_t SectorsPerTrack;
    uint16_t NumberOfHeads;
    uint32_t HiddenSectors;
    uint8_t  Reserved3[4];
    uint32_t Unknown;
    uint64_t SectorsInVolume;
    uint64_t MftLcn;
    uint64_t MftMirrLcn;
    int8_t   ClustersPerFileRecord;
    uint8_t  Reserved4[3];
    int8_t   ClustersPerIndexRecord;
    uint8_t  Reserved5[3];
    uint64_t SerialNumber;
    uint32_t Checksum;
} NTFS_BOOT_SECTOR;

typedef struct _NTFS_RECORD_HEADER
{
    uint32_t Magic;
    uint16_t UpdateSequenceOffset;
    uint16_t SizeOfUpdateSequence;
    uint64_t LogFileSequenceNumber;
} NTFS_RECORD_HEADER;

typedef struct _NTFS_FILE_RECORD_HEADER
{
    NTFS_RECORD_HEADER Header;
    uint16_t SequenceNumber;
    uint16_t HardLinkCount;
    uint16_t AttributeOffset;
    uint16_t Flags;
    uint32_t ActualSize;
    uint32_t AllocatedSize;
    uint64_t BaseFileRecord;
    uint16_t NextAttributeId;
    uint16_t Padding;
    uint32_t MftRecordNumber;
} NTFS_FILE_RECORD_HEADER;

typedef struct _NTFS_ATTRIBUTE
{
    uint32_t Type;
    uint32_t Length;
    uint8_t  IsNonResident;
    uint8_t  NameLength;
    uint16_t NameOffset;
    uint16_t Flags;
    uint16_t AttributeId;
    uint64_t FirstVcn;
    uint64_t LastVcn;
    uint16_t DataRunsOffset;
    uint16_t CompressionUnitSize;
    uint32_t Reserved;
    uint64_t AllocatedSize;
    uint64_t DataSize;
    uint64_t InitializedSize;
} NTFS_ATTRIBUTE;

#pragma pack(pop)

typedef struct _NTFS_RUN
{
    uint64_t Lcn;
    uint64_t Length;
    int Sparse;
} NTFS_RUN;

typedef struct _NTFS_CONTEXT
{
    EXPAND_DEVICE* Device;
    uint64_t PartitionStart;
    uint64_t MftLcn;
    uint64_t OldClusters;
    uint64_t NewClusters;
    uint32_t BytesPerSector;
    uint32_t ClusterSize;
    uint32_t RecordSize;
} NTFS_CONTEXT;

const char*
ExpandStatusText(int Status)
{
    switch (Status)
    {
        case EXPAND_OK: return "success";
        case EXPAND_IO_ERROR: return "disk I/O failed";
        case EXPAND_NO_MBR: return "no MBR partition table found";
        case EXPAND_GPT_UNSUPPORTED: return "GPT disks are not supported";
        case EXPAND_NOT_LAST: return "the target partition is not the last one on the disk";
        case EXPAND_NOTHING_TO_DO: return "nothing to expand";
        case EXPAND_NOT_NTFS: return "the target partition does not hold an NTFS volume";
        case EXPAND_CORRUPT: return "unexpected on-disk metadata";
        case EXPAND_NO_ROOM: return "no room for the new run list";
        case EXPAND_BAD_PARAMETER: return "invalid parameter";
        case EXPAND_TOO_LARGE: return "the requested size does not fit an MBR partition entry";
        default: return "unknown error";
    }
}

static void
Report(EXPAND_DEVICE* Device, const char* Format, ...)
{
    char Line[256];
    va_list Args;

    if (!Device->Log)
        return;

    va_start(Args, Format);
    ExpandVsnprintf(Line, sizeof(Line) - 1, Format, Args);
    va_end(Args);
    Line[sizeof(Line) - 1] = 0;
    Device->Log(Device->Context, Line);
}

static int
IsPowerOfTwo(uint64_t Value)
{
    return Value != 0 && (Value & (Value - 1)) == 0;
}

static int
ValidateFixup(const NTFS_RECORD_HEADER* Header, uint32_t RecordSize, uint32_t BytesPerSector)
{
    uint32_t UsaSize = (uint32_t)Header->SizeOfUpdateSequence * 2;

    return !(RecordSize < BytesPerSector ||
             (RecordSize % BytesPerSector) != 0 ||
             Header->SizeOfUpdateSequence != RecordSize / BytesPerSector + 1 ||
             Header->UpdateSequenceOffset > RecordSize ||
             UsaSize > RecordSize - Header->UpdateSequenceOffset);
}

static int
ApplyFixup(uint8_t* Record, uint32_t RecordSize, uint32_t BytesPerSector)
{
    NTFS_RECORD_HEADER* Header = (NTFS_RECORD_HEADER*)Record;
    uint16_t* Usa;
    uint8_t* SectorTail;
    uint32_t Index;

    if (!ValidateFixup(Header, RecordSize, BytesPerSector))
        return 0;

    Usa = (uint16_t*)(Record + Header->UpdateSequenceOffset);
    SectorTail = Record + BytesPerSector - 2;

    for (Index = 1; Index < Header->SizeOfUpdateSequence; Index++)
    {
        uint16_t Stamped;

        memcpy(&Stamped, SectorTail, 2);
        if (Stamped != Usa[0])
            return 0;
        memcpy(SectorTail, &Usa[Index], 2);
        SectorTail += BytesPerSector;
    }

    return 1;
}

static int
CommitFixup(uint8_t* Record, uint32_t RecordSize, uint32_t BytesPerSector)
{
    NTFS_RECORD_HEADER* Header = (NTFS_RECORD_HEADER*)Record;
    uint16_t* Usa;
    uint8_t* SectorTail;
    uint32_t Index;

    if (!ValidateFixup(Header, RecordSize, BytesPerSector))
        return 0;

    Usa = (uint16_t*)(Record + Header->UpdateSequenceOffset);
    SectorTail = Record + BytesPerSector - 2;

    Usa[0]++;
    if (Usa[0] == 0)
        Usa[0]++;

    for (Index = 1; Index < Header->SizeOfUpdateSequence; Index++)
    {
        memcpy(&Usa[Index], SectorTail, 2);
        memcpy(SectorTail, &Usa[0], 2);
        SectorTail += BytesPerSector;
    }

    return 1;
}

static uint32_t
SignedMappingBytes(int64_t Value)
{
    uint32_t Bytes;

    for (Bytes = 1; Bytes < 8; Bytes++)
    {
        int64_t Minimum = -((int64_t)1 << (Bytes * 8 - 1));
        int64_t Maximum = ((int64_t)1 << (Bytes * 8 - 1)) - 1;

        if (Value >= Minimum && Value <= Maximum)
            return Bytes;
    }

    return 8;
}

static int
DecodeRuns(const uint8_t* Buffer,
           const uint8_t* End,
           NTFS_RUN* Runs,
           uint32_t Maximum,
           uint32_t* Count)
{
    const uint8_t* Current = Buffer;
    uint64_t Lcn = 0;
    uint32_t Total = 0;

    if (Buffer > End)
        return 0;

    while (Current < End && *Current != 0)
    {
        uint8_t Header = *Current++;
        uint32_t LengthBytes = Header & 0x0F;
        uint32_t OffsetBytes = Header >> 4;
        uint64_t Length = 0;
        int64_t Delta = 0;
        uint32_t Index;

        if (LengthBytes == 0 || LengthBytes > 8 || OffsetBytes > 8)
            return 0;
        if ((uint64_t)(End - Current) < (uint64_t)LengthBytes + OffsetBytes)
            return 0;
        if (Total >= Maximum)
            return 0;

        for (Index = 0; Index < LengthBytes; Index++)
            Length |= (uint64_t)Current[Index] << (Index * 8);
        Current += LengthBytes;

        if (Length == 0)
            return 0;

        if (OffsetBytes != 0)
        {
            for (Index = 0; Index < OffsetBytes; Index++)
                Delta |= (int64_t)((uint64_t)Current[Index] << (Index * 8));
            if (OffsetBytes < 8 && (Current[OffsetBytes - 1] & 0x80) != 0)
                Delta |= -((int64_t)1 << (OffsetBytes * 8));
            Current += OffsetBytes;

            Lcn = (uint64_t)((int64_t)Lcn + Delta);
            Runs[Total].Lcn = Lcn;
            Runs[Total].Sparse = 0;
        }
        else
        {
            Runs[Total].Lcn = 0;
            Runs[Total].Sparse = 1;
        }

        Runs[Total].Length = Length;
        Total++;
    }

    *Count = Total;
    return 1;
}

static uint32_t
EncodeRuns(const NTFS_RUN* Runs, uint32_t Count, uint8_t* Buffer, uint32_t Capacity)
{
    uint64_t Previous = 0;
    uint32_t Offset = 0;
    uint32_t Index;

    for (Index = 0; Index < Count; Index++)
    {
        uint32_t LengthBytes;
        uint32_t OffsetBytes = 0;
        int64_t Delta = 0;
        uint32_t Byte;

        if (Runs[Index].Length == 0)
            return 0;

        LengthBytes = SignedMappingBytes((int64_t)Runs[Index].Length);

        if (!Runs[Index].Sparse)
        {
            Delta = (int64_t)(Runs[Index].Lcn - Previous);
            OffsetBytes = SignedMappingBytes(Delta);
        }

        if (Offset + 1 + LengthBytes + OffsetBytes + 1 > Capacity)
            return 0;

        Buffer[Offset++] = (uint8_t)((OffsetBytes << 4) | LengthBytes);
        for (Byte = 0; Byte < LengthBytes; Byte++)
            Buffer[Offset++] = (uint8_t)(Runs[Index].Length >> (Byte * 8));
        for (Byte = 0; Byte < OffsetBytes; Byte++)
            Buffer[Offset++] = (uint8_t)((uint64_t)Delta >> (Byte * 8));

        if (!Runs[Index].Sparse)
            Previous = Runs[Index].Lcn;
    }

    if (Offset + 1 > Capacity)
        return 0;
    Buffer[Offset++] = 0;

    return Offset;
}

static NTFS_ATTRIBUTE*
FindAttribute(uint8_t* Record,
              uint32_t RecordSize,
              uint32_t Type,
              const uint16_t* Name,
              uint32_t NameLength,
              int* IsLast)
{
    NTFS_FILE_RECORD_HEADER* Header = (NTFS_FILE_RECORD_HEADER*)Record;
    uint32_t Offset = Header->AttributeOffset;

    *IsLast = 0;

    if (Offset < sizeof(NTFS_FILE_RECORD_HEADER))
        return NULL;

    while (Offset + 8 <= RecordSize)
    {
        NTFS_ATTRIBUTE* Attribute = (NTFS_ATTRIBUTE*)(Record + Offset);
        uint32_t Next;

        if (Attribute->Type == ATTR_TYPE_END)
            return NULL;

        if (Attribute->Length < 0x18 ||
            Attribute->Length > RecordSize - Offset ||
            (Attribute->Length & 7) != 0)
        {
            return NULL;
        }

        Next = Offset + Attribute->Length;

        if (Attribute->Type == Type &&
            Attribute->NameLength == NameLength &&
            (NameLength == 0 ||
             ((uint32_t)Attribute->NameOffset + NameLength * 2 <= Attribute->Length &&
              memcmp(Record + Offset + Attribute->NameOffset, Name, NameLength * 2) == 0)))
        {
            if (Next + 4 <= RecordSize &&
                *(uint32_t*)(Record + Next) == ATTR_TYPE_END)
            {
                *IsLast = 1;
            }
            return Attribute;
        }

        Offset = Next;
    }

    return NULL;
}

static int
ReadRecord(NTFS_CONTEXT* Context, uint32_t Number, uint8_t* Record)
{
    uint64_t Offset = Context->PartitionStart +
                      Context->MftLcn * Context->ClusterSize +
                      (uint64_t)Number * Context->RecordSize;

    if (!Context->Device->Read(Context->Device->Context, Offset, Context->RecordSize, Record))
        return EXPAND_IO_ERROR;

    if (((NTFS_FILE_RECORD_HEADER*)Record)->Header.Magic != NTFS_FILE_MAGIC)
        return EXPAND_CORRUPT;

    if (!ApplyFixup(Record, Context->RecordSize, Context->BytesPerSector))
        return EXPAND_CORRUPT;

    return EXPAND_OK;
}

static int
WriteRecord(NTFS_CONTEXT* Context, uint32_t Number, uint8_t* Record)
{
    uint64_t Offset = Context->PartitionStart +
                      Context->MftLcn * Context->ClusterSize +
                      (uint64_t)Number * Context->RecordSize;

    if (!CommitFixup(Record, Context->RecordSize, Context->BytesPerSector))
        return EXPAND_CORRUPT;

    if (!Context->Device->Write(Context->Device->Context, Offset, Context->RecordSize, Record))
        return EXPAND_IO_ERROR;

    return EXPAND_OK;
}

static int
ReplaceRuns(NTFS_CONTEXT* Context,
            uint8_t* Record,
            NTFS_ATTRIBUTE* Attribute,
            const NTFS_RUN* Runs,
            uint32_t RunCount,
            uint64_t LastVcn,
            uint64_t AllocatedSize,
            uint64_t DataSize,
            uint64_t InitializedSize)
{
    uint8_t Encoded[MAX_MAPPING_PAIRS];
    NTFS_FILE_RECORD_HEADER* Header = (NTFS_FILE_RECORD_HEADER*)Record;
    uint32_t AttributeOffset = (uint32_t)((uint8_t*)Attribute - Record);
    uint32_t RunsOffset = Attribute->DataRunsOffset;
    uint32_t EncodedLength;
    uint32_t NewLength;

    if (RunsOffset < ATTR_NONRESIDENT_HEADER_SIZE || RunsOffset >= Context->RecordSize)
        return EXPAND_CORRUPT;

    EncodedLength = EncodeRuns(Runs, RunCount, Encoded, sizeof(Encoded));
    if (EncodedLength == 0)
        return EXPAND_CORRUPT;

    NewLength = (uint32_t)ALIGN_UP8((uint64_t)RunsOffset + EncodedLength);
    if (AttributeOffset + NewLength + 8 > Context->RecordSize)
        return EXPAND_NO_ROOM;

    memset((uint8_t*)Attribute + RunsOffset, 0, NewLength - RunsOffset);
    memcpy((uint8_t*)Attribute + RunsOffset, Encoded, EncodedLength);

    Attribute->Length = NewLength;
    Attribute->LastVcn = LastVcn;
    Attribute->AllocatedSize = AllocatedSize;
    Attribute->DataSize = DataSize;
    Attribute->InitializedSize = InitializedSize;

    *(uint32_t*)(Record + AttributeOffset + NewLength) = ATTR_TYPE_END;
    *(uint32_t*)(Record + AttributeOffset + NewLength + 4) = 0;
    Header->ActualSize = AttributeOffset + NewLength + 8;

    return EXPAND_OK;
}

static int
RunsTransfer(NTFS_CONTEXT* Context,
             const NTFS_RUN* Runs,
             uint32_t RunCount,
             uint64_t FileOffset,
             uint32_t Length,
             uint8_t* Buffer,
             int Write)
{
    uint32_t ClusterSize = Context->ClusterSize;
    uint32_t Done = 0;

    while (Done < Length)
    {
        uint64_t Position = FileOffset + Done;
        uint64_t Vcn = Position / ClusterSize;
        uint64_t InCluster = Position % ClusterSize;
        uint64_t Base = 0;
        uint32_t Index;
        int Found = 0;

        for (Index = 0; Index < RunCount; Index++)
        {
            uint64_t RunVcn;
            uint64_t Disk;
            uint64_t Available;
            uint32_t Chunk;

            if (Vcn >= Base + Runs[Index].Length)
            {
                Base += Runs[Index].Length;
                continue;
            }

            if (Runs[Index].Sparse)
                return EXPAND_CORRUPT;

            RunVcn = Vcn - Base;
            Disk = Context->PartitionStart +
                   (Runs[Index].Lcn + RunVcn) * ClusterSize + InCluster;
            Available = (Runs[Index].Length - RunVcn) * ClusterSize - InCluster;
            Chunk = (uint32_t)(Available < Length - Done ? Available : Length - Done);

            if (Write)
            {
                if (!Context->Device->Write(Context->Device->Context, Disk, Chunk, Buffer + Done))
                    return EXPAND_IO_ERROR;
            }
            else
            {
                if (!Context->Device->Read(Context->Device->Context, Disk, Chunk, Buffer + Done))
                    return EXPAND_IO_ERROR;
            }

            Done += Chunk;
            Found = 1;
            break;
        }

        if (!Found)
            return EXPAND_CORRUPT;
    }

    return EXPAND_OK;
}

static int
ReadMbr(EXPAND_DEVICE* Device, uint8_t* Sector)
{
    if (!Device->Read(Device->Context, 0, Device->SectorSize, Sector))
        return EXPAND_IO_ERROR;

    if (*(uint16_t*)(Sector + MBR_SIGNATURE_OFFSET) != MBR_SIGNATURE)
        return EXPAND_NO_MBR;

    return EXPAND_OK;
}

int
ExpandBuildPlan(EXPAND_DEVICE* Device,
                uint32_t PartitionNumber,
                uint64_t PadBytes,
                uint64_t MinimumGain,
                EXPAND_PLAN* Plan)
{
    uint8_t Sector[MAX_SECTOR_SIZE];
    uint8_t Boot[MAX_SECTOR_SIZE];
    MBR_ENTRY* Table;
    NTFS_BOOT_SECTOR* BootSector;
    uint64_t DiskSectors;
    uint64_t PadSectors;
    uint64_t EndSector;
    uint64_t TargetEnd;
    uint64_t NewSectors;
    uint64_t FinalSectors;
    uint64_t VolumeSectors;
    uint32_t SectorSize = Device->SectorSize;
    uint32_t Slot;
    uint32_t Ordinal = 0;
    int Target = -1;
    int Status;

    if (SectorSize < 512 || SectorSize > MAX_SECTOR_SIZE || !IsPowerOfTwo(SectorSize))
        return EXPAND_BAD_PARAMETER;

    memset(Plan, 0, sizeof(*Plan));

    Status = ReadMbr(Device, Sector);
    if (Status != EXPAND_OK)
        return Status;

    Table = (MBR_ENTRY*)(Sector + MBR_TABLE_OFFSET);

    for (Slot = 0; Slot < MBR_ENTRY_COUNT; Slot++)
    {
        if (Table[Slot].Type == MBR_TYPE_GPT)
            return EXPAND_GPT_UNSUPPORTED;
    }

    for (Slot = 0; Slot < MBR_ENTRY_COUNT; Slot++)
    {
        if (Table[Slot].Type == MBR_TYPE_EMPTY || Table[Slot].SectorCount == 0)
            continue;

        Ordinal++;

        if (PartitionNumber != 0)
        {
            if (Ordinal == PartitionNumber)
            {
                Target = (int)Slot;
                Plan->PartitionNumber = Ordinal;
            }
        }
        else if (Target < 0 || Table[Slot].StartLba > Table[Target].StartLba)
        {
            Target = (int)Slot;
            Plan->PartitionNumber = Ordinal;
        }
    }

    if (Target < 0)
        return EXPAND_BAD_PARAMETER;

    if (Table[Target].Type == MBR_TYPE_EXTENDED ||
        Table[Target].Type == MBR_TYPE_EXTENDED_LBA)
    {
        return EXPAND_BAD_PARAMETER;
    }

    TargetEnd = (uint64_t)Table[Target].StartLba + Table[Target].SectorCount;

    for (Slot = 0; Slot < MBR_ENTRY_COUNT; Slot++)
    {
        if ((int)Slot == Target ||
            Table[Slot].Type == MBR_TYPE_EMPTY ||
            Table[Slot].SectorCount == 0)
        {
            continue;
        }

        if ((uint64_t)Table[Slot].StartLba + Table[Slot].SectorCount > TargetEnd)
            return EXPAND_NOT_LAST;
    }

    DiskSectors = Device->DiskSize / SectorSize;
    PadSectors = (PadBytes + SectorSize - 1) / SectorSize;

    if (DiskSectors <= PadSectors)
        return EXPAND_BAD_PARAMETER;

    EndSector = DiskSectors - PadSectors;
    if (EndSector > 0xFFFFFFFFULL)
        EndSector = 0xFFFFFFFFULL;
    if (EndSector <= Table[Target].StartLba)
        return EXPAND_BAD_PARAMETER;

    NewSectors = EndSector - Table[Target].StartLba;
    FinalSectors = NewSectors > Table[Target].SectorCount
                       ? NewSectors
                       : Table[Target].SectorCount;

    Plan->PartitionSlot = (uint32_t)Target;
    Plan->PartitionType = Table[Target].Type;
    Plan->PartitionStart = (uint64_t)Table[Target].StartLba * SectorSize;
    Plan->OldPartitionBytes = (uint64_t)Table[Target].SectorCount * SectorSize;
    Plan->NewPartitionBytes = FinalSectors * SectorSize;
    Plan->PartitionGain = Plan->NewPartitionBytes - Plan->OldPartitionBytes;
    Plan->RewritePartitionTable = FinalSectors != Table[Target].SectorCount;

    if (!Device->Read(Device->Context, Plan->PartitionStart, SectorSize, Boot))
        return EXPAND_IO_ERROR;

    BootSector = (NTFS_BOOT_SECTOR*)Boot;

    if (memcmp(BootSector->OemId, "NTFS    ", 8) != 0)
        return EXPAND_NOT_NTFS;

    if (!IsPowerOfTwo(BootSector->BytesPerSector) ||
        BootSector->BytesPerSector < 512 ||
        BootSector->BytesPerSector > MAX_SECTOR_SIZE ||
        !IsPowerOfTwo(BootSector->SectorsPerCluster) ||
        BootSector->SectorsPerCluster > 128 ||
        BootSector->SectorsInVolume == 0)
    {
        return EXPAND_CORRUPT;
    }

    Plan->BytesPerSector = BootSector->BytesPerSector;
    Plan->ClusterSize = (uint32_t)BootSector->BytesPerSector * BootSector->SectorsPerCluster;
    Plan->OldVolumeSectors = BootSector->SectorsInVolume;
    Plan->OldClusters = BootSector->SectorsInVolume / BootSector->SectorsPerCluster;

    VolumeSectors = Plan->NewPartitionBytes / BootSector->BytesPerSector;
    if (VolumeSectors < 2)
        return EXPAND_CORRUPT;

    Plan->NewVolumeSectors = VolumeSectors - 1;
    if (Plan->NewVolumeSectors < Plan->OldVolumeSectors)
        Plan->NewVolumeSectors = Plan->OldVolumeSectors;

    Plan->NewClusters = Plan->NewVolumeSectors / BootSector->SectorsPerCluster;
    Plan->VolumeGain = (Plan->NewClusters - Plan->OldClusters) * Plan->ClusterSize;
    Plan->RewriteVolume = Plan->NewClusters > Plan->OldClusters;

    if (!Plan->RewritePartitionTable && !Plan->RewriteVolume)
        return EXPAND_NOTHING_TO_DO;

    if (Plan->PartitionGain < MinimumGain && Plan->VolumeGain < MinimumGain)
        return EXPAND_NOTHING_TO_DO;

    return EXPAND_OK;
}

static int
GrowBitmap(NTFS_CONTEXT* Context, uint32_t* ExtraClusters)
{
    uint8_t Record[MAX_RECORD_SIZE];
    uint8_t Chunk[BITMAP_CHUNK];
    NTFS_RUN Runs[MAX_RUNS];
    NTFS_ATTRIBUTE* Attribute;
    uint32_t RunCount = 0;
    uint32_t ClusterSize = Context->ClusterSize;
    uint64_t OldBitmapClusters;
    uint64_t NewBitmapDataSize;
    uint64_t NewBitmapClusters;
    uint64_t Extra;
    uint64_t NewLcn = Context->OldClusters;
    uint64_t FirstByte;
    uint64_t EndByte;
    uint64_t Position;
    uint8_t Preserved = 0;
    int IsLast;
    int Status;

    Status = ReadRecord(Context, NTFS_RECORD_BITMAP, Record);
    if (Status != EXPAND_OK)
        return Status;

    Attribute = FindAttribute(Record, Context->RecordSize, ATTR_TYPE_DATA, NULL, 0, &IsLast);
    if (!Attribute || !Attribute->IsNonResident || !IsLast)
        return EXPAND_CORRUPT;

    if (Attribute->FirstVcn != 0 ||
        (Attribute->Flags & (ATTR_COMPRESSION_MASK | ATTR_ENCRYPTED | ATTR_SPARSE)) != 0)
    {
        return EXPAND_CORRUPT;
    }

    if (!DecodeRuns((uint8_t*)Attribute + Attribute->DataRunsOffset,
                    (uint8_t*)Attribute + Attribute->Length,
                    Runs,
                    MAX_RUNS,
                    &RunCount) ||
        RunCount == 0)
    {
        return EXPAND_CORRUPT;
    }

    OldBitmapClusters = Attribute->AllocatedSize / ClusterSize;
    if (OldBitmapClusters == 0 || Attribute->LastVcn + 1 != OldBitmapClusters)
        return EXPAND_CORRUPT;

    NewBitmapDataSize = ALIGN_UP8((Context->NewClusters + 7) / 8);
    NewBitmapClusters = (NewBitmapDataSize + ClusterSize - 1) / ClusterSize;
    if (NewBitmapClusters < OldBitmapClusters)
        NewBitmapClusters = OldBitmapClusters;
    Extra = NewBitmapClusters - OldBitmapClusters;

    if (Extra != 0)
    {
        if (NewLcn + Extra > Context->NewClusters)
            return EXPAND_NO_ROOM;

        if (!Runs[RunCount - 1].Sparse &&
            Runs[RunCount - 1].Lcn + Runs[RunCount - 1].Length == NewLcn)
        {
            Runs[RunCount - 1].Length += Extra;
        }
        else
        {
            if (RunCount >= MAX_RUNS)
                return EXPAND_NO_ROOM;
            Runs[RunCount].Lcn = NewLcn;
            Runs[RunCount].Length = Extra;
            Runs[RunCount].Sparse = 0;
            RunCount++;
        }
    }

    FirstByte = Context->OldClusters / 8;
    EndByte = NewBitmapClusters * ClusterSize;

    if ((Context->OldClusters & 7) != 0)
    {
        Status = RunsTransfer(Context, Runs, RunCount, FirstByte, 1, &Preserved, 0);
        if (Status != EXPAND_OK)
            return Status;
        Preserved &= (uint8_t)((1u << (Context->OldClusters & 7)) - 1);
    }

    for (Position = FirstByte; Position < EndByte; )
    {
        uint64_t Remaining = EndByte - Position;
        uint32_t Length = (uint32_t)(Remaining < BITMAP_CHUNK ? Remaining : BITMAP_CHUNK);
        uint32_t Byte;

        for (Byte = 0; Byte < Length; Byte++)
        {
            uint64_t Base = (Position + Byte) * 8;
            uint8_t Value = 0;
            uint32_t Bit;

            for (Bit = 0; Bit < 8; Bit++)
            {
                uint64_t Cluster = Base + Bit;

                if (Cluster < Context->OldClusters)
                    continue;
                if (Cluster >= Context->NewClusters ||
                    (Extra != 0 && Cluster >= NewLcn && Cluster < NewLcn + Extra))
                {
                    Value |= (uint8_t)(1u << Bit);
                }
            }

            if (Position + Byte == FirstByte)
                Value |= Preserved;

            Chunk[Byte] = Value;
        }

        Status = RunsTransfer(Context, Runs, RunCount, Position, Length, Chunk, 1);
        if (Status != EXPAND_OK)
            return Status;

        Position += Length;
    }

    Status = ReplaceRuns(Context,
                         Record,
                         Attribute,
                         Runs,
                         RunCount,
                         NewBitmapClusters - 1,
                         NewBitmapClusters * ClusterSize,
                         NewBitmapDataSize,
                         NewBitmapDataSize);
    if (Status != EXPAND_OK)
        return Status;

    Status = WriteRecord(Context, NTFS_RECORD_BITMAP, Record);
    if (Status != EXPAND_OK)
        return Status;

    *ExtraClusters = (uint32_t)Extra;
    return EXPAND_OK;
}

static int
GrowBadClusters(NTFS_CONTEXT* Context)
{
    static const uint16_t BadName[4] = { '$', 'B', 'a', 'd' };
    uint8_t Record[MAX_RECORD_SIZE];
    NTFS_RUN Runs[MAX_RUNS];
    NTFS_ATTRIBUTE* Attribute;
    uint32_t RunCount = 0;
    uint64_t Mapped = 0;
    uint64_t Missing;
    uint64_t Initialized;
    uint32_t Index;
    int IsLast;
    int Status;

    Status = ReadRecord(Context, NTFS_RECORD_BADCLUS, Record);
    if (Status != EXPAND_OK)
        return Status;

    Attribute = FindAttribute(Record, Context->RecordSize, ATTR_TYPE_DATA, BadName, 4, &IsLast);
    if (!Attribute || !Attribute->IsNonResident || !IsLast)
        return EXPAND_CORRUPT;

    if (!DecodeRuns((uint8_t*)Attribute + Attribute->DataRunsOffset,
                    (uint8_t*)Attribute + Attribute->Length,
                    Runs,
                    MAX_RUNS,
                    &RunCount) ||
        RunCount == 0)
    {
        return EXPAND_CORRUPT;
    }

    for (Index = 0; Index < RunCount; Index++)
        Mapped += Runs[Index].Length;

    if (Mapped >= Context->NewClusters)
        return EXPAND_OK;

    Missing = Context->NewClusters - Mapped;
    Initialized = Attribute->InitializedSize;

    if (Runs[RunCount - 1].Sparse)
    {
        Runs[RunCount - 1].Length += Missing;
    }
    else
    {
        if (RunCount >= MAX_RUNS)
            return EXPAND_NO_ROOM;
        Runs[RunCount].Lcn = 0;
        Runs[RunCount].Length = Missing;
        Runs[RunCount].Sparse = 1;
        RunCount++;
    }

    Status = ReplaceRuns(Context,
                         Record,
                         Attribute,
                         Runs,
                         RunCount,
                         Context->NewClusters - 1,
                         Context->NewClusters * Context->ClusterSize,
                         Context->NewClusters * Context->ClusterSize,
                         Initialized);
    if (Status != EXPAND_OK)
        return Status;

    return WriteRecord(Context, NTFS_RECORD_BADCLUS, Record);
}

int
ExpandApplyPlan(EXPAND_DEVICE* Device, const EXPAND_PLAN* Plan)
{
    NTFS_CONTEXT Context;
    NTFS_BOOT_SECTOR* BootSector;
    uint8_t Sector[MAX_SECTOR_SIZE];
    uint8_t Boot[MAX_SECTOR_SIZE];
    MBR_ENTRY* Table;
    uint32_t Extra = 0;
    int Status;

    memset(&Context, 0, sizeof(Context));
    Context.Device = Device;
    Context.PartitionStart = Plan->PartitionStart;
    Context.BytesPerSector = Plan->BytesPerSector;
    Context.ClusterSize = Plan->ClusterSize;
    Context.OldClusters = Plan->OldClusters;
    Context.NewClusters = Plan->NewClusters;

    if (!Device->Read(Device->Context, Plan->PartitionStart, Plan->BytesPerSector, Boot))
        return EXPAND_IO_ERROR;

    BootSector = (NTFS_BOOT_SECTOR*)Boot;

    if (BootSector->ClustersPerFileRecord >= 0)
    {
        Context.RecordSize = (uint32_t)BootSector->ClustersPerFileRecord * Context.ClusterSize;
    }
    else
    {
        int Shift = -BootSector->ClustersPerFileRecord;

        if (Shift >= 31)
            return EXPAND_CORRUPT;
        Context.RecordSize = 1u << Shift;
    }

    if (Context.RecordSize < Context.BytesPerSector ||
        Context.RecordSize > MAX_RECORD_SIZE ||
        (Context.RecordSize % Context.BytesPerSector) != 0)
    {
        return EXPAND_CORRUPT;
    }

    Context.MftLcn = BootSector->MftLcn;

    if (Plan->RewriteVolume)
    {
        Status = GrowBitmap(&Context, &Extra);
        if (Status != EXPAND_OK)
            return Status;

        Report(Device,
               "$Bitmap now covers %llu clusters (+%lu cluster(s) of backing store)\n",
               (unsigned long long)Plan->NewClusters,
               (unsigned long)Extra);

        Status = GrowBadClusters(&Context);
        if (Status != EXPAND_OK)
            return Status;

        Report(Device, "$BadClus:$Bad now spans the whole cluster space\n");
    }

    if (Plan->RewritePartitionTable)
    {
        Status = ReadMbr(Device, Sector);
        if (Status != EXPAND_OK)
            return Status;

        Table = (MBR_ENTRY*)(Sector + MBR_TABLE_OFFSET);
        Table[Plan->PartitionSlot].SectorCount =
            (uint32_t)(Plan->NewPartitionBytes / Device->SectorSize);

        if (!Device->Write(Device->Context, 0, Device->SectorSize, Sector))
            return EXPAND_IO_ERROR;

        Report(Device,
               "partition %lu now spans %llu sector(s)\n",
               (unsigned long)Plan->PartitionNumber,
               (unsigned long long)(Plan->NewPartitionBytes / Device->SectorSize));
    }

    if (Plan->RewriteVolume)
    {
        BootSector->SectorsInVolume = Plan->NewVolumeSectors;

        if (!Device->Write(Device->Context,
                           Plan->PartitionStart + Plan->NewVolumeSectors * Plan->BytesPerSector,
                           Plan->BytesPerSector,
                           Boot))
        {
            return EXPAND_IO_ERROR;
        }

        if (!Device->Write(Device->Context, Plan->PartitionStart, Plan->BytesPerSector, Boot))
            return EXPAND_IO_ERROR;

        Report(Device,
               "$Boot now declares %llu sector(s)\n",
               (unsigned long long)Plan->NewVolumeSectors);
    }

    return EXPAND_OK;
}
