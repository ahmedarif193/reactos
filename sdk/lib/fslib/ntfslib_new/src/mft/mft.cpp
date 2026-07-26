/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define MFT_ALLOCATION_BITMAP_CHUNK_SIZE 0x10000
#define MFT_FIRST_ORDINARY_FILE_RECORD 24

/*
 * Reserve $MFT data in coarse chunks so its records stay in long runs
 * instead of interleaving one mapping pair per newly touched cluster.
 */
#define MFT_ALLOCATION_GROWTH_CLUSTERS 16
#define NTFS_MAX_SIGNED_OFFSET \
    ((ULONGLONG)0x7fffffffffffffffULL)

static NTSTATUS
InitializeFileRecordBuffer(
    _In_ PVolume DiskVolume,
    _In_ ULONG FileRecordSize,
    _In_ ULONG FileRecordNumber,
    _In_ USHORT SequenceNumber,
    _In_ ULONGLONG BaseFileReference,
    _In_ BOOLEAN InUse,
    _Inout_ PFileRecord File)
{
    ULONG AttributeOffset;
    ULONG UsaBytes;
    ULONG UsaCount;

    if (!DiskVolume || !File || !File->Data ||
        FileRecordSize < sizeof(FileRecordHeader) ||
        DiskVolume->BytesPerSector == 0 ||
        FileRecordSize % DiskVolume->BytesPerSector != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    UsaCount =
        FileRecordSize / DiskVolume->BytesPerSector + 1;
    if (UsaCount > MAXUSHORT ||
        UsaCount >
            (MAXULONG -
             FIELD_OFFSET(FileRecordHeader,
                          MFTRecordNumber) -
             sizeof(ULONG)) /
                sizeof(USHORT))
    {
        return STATUS_FILE_TOO_LARGE;
    }
    UsaBytes = UsaCount * sizeof(USHORT);
    AttributeOffset = ALIGN_UP_BY(
        FIELD_OFFSET(FileRecordHeader,
                     MFTRecordNumber) +
            sizeof(ULONG) + UsaBytes,
        sizeof(ULONGLONG));
    if (AttributeOffset > MAXUSHORT ||
        AttributeOffset >
            FileRecordSize - 2 * sizeof(ULONG))
    {
        return STATUS_FILE_TOO_LARGE;
    }

    RtlZeroMemory(File->Data, FileRecordSize);
    File->Header =
        reinterpret_cast<PFileRecordHeader>(
            File->Data);
    RtlCopyMemory(File->Header->Header.TypeID,
                  "FILE",
                  sizeof(File->Header->Header.TypeID));
    File->Header->Header.UpdateSequenceOffset =
        (USHORT)(FIELD_OFFSET(FileRecordHeader,
                             MFTRecordNumber) +
                 sizeof(ULONG));
    File->Header->Header.SizeOfUpdateSequence =
        (USHORT)UsaCount;
    File->Header->SequenceNumber =
        SequenceNumber != 0 ? SequenceNumber : 1;
    File->Header->HardLinkCount = 0;
    File->Header->AttributeOffset =
        (USHORT)AttributeOffset;
    File->Header->Flags =
        InUse ? FR_IN_USE : 0;
    File->Header->ActualSize =
        AttributeOffset + 2 * sizeof(ULONG);
    File->Header->AllocatedSize =
        FileRecordSize;
    File->Header->BaseFileRecord =
        InUse ? BaseFileReference : 0;
    File->Header->NextAttributeID = 0;
    File->Header->MFTRecordNumber =
        FileRecordNumber;

    *reinterpret_cast<PULONG>(
        File->Data + AttributeOffset) =
            TypeAttributeEndMarker;
    return STATUS_SUCCESS;
}

static NTSTATUS
ReadMftBitmapByte(
    _In_ PFileRecord MftFile,
    _In_ PAttribute BitmapAttribute,
    _In_ ULONGLONG ByteOffset,
    _Out_ PUCHAR Value)
{
    ULONG Length = 1;
    NTSTATUS Status;

    if (!MftFile || !BitmapAttribute || !Value)
        return STATUS_INVALID_PARAMETER;

    Status = MftFile->CopyData(BitmapAttribute,
                               Value,
                               &Length,
                               ByteOffset);
    if (!NT_SUCCESS(Status))
        return Status;
    return Length == 0
        ? STATUS_SUCCESS
        : STATUS_END_OF_FILE;
}

static NTSTATUS
WriteMftBitmapByte(
    _In_ PFileRecord MftFile,
    _In_ ULONGLONG ByteOffset,
    _In_ UCHAR Value)
{
    LARGE_INTEGER Offset;
    ULONG Length = 1;

    if (!MftFile ||
        ByteOffset > NTFS_MAX_SIGNED_OFFSET)
        return STATUS_INVALID_PARAMETER;
    Offset.QuadPart = (LONGLONG)ByteOffset;
    return MftFile->WriteFileData(TypeBitmap,
                                  NULL,
                                  &Value,
                                  &Length,
                                  &Offset);
}

MasterFileTable::MasterFileTable(_In_ PVolume TargetVolume,
                                 _In_ UINT64 MFTLCN,
                                 _In_ UINT64 MFTMirrLCN,
                                 _In_ INT8   ClustersPerFileRecord)
{
    NTSTATUS Status;

    DiskVolume = TargetVolume;
    this->MFTLCN = MFTLCN;
    this->MFTMirrLCN = MFTMirrLCN;

    /* Set the file record size, in bytes.
     * If clusters per file record is less than 0, the file record size is 2^(-ClustersPerFileRecord).
     * Otherwise, the file record size is ClustersPerFileRecord * SectorsPerCluster * BytesPerSector.
     */
    FileRecordSize = ClustersPerFileRecord < 0 ?
                     1 << (-(ClustersPerFileRecord))
                     : ClustersPerFileRecord * BytesPerCluster(DiskVolume);

    // Initialize $MFT
    Status = GetFileRecord(_MFT, &MFTFile);
    if (!NT_SUCCESS(Status))
        DPRINT1("Failed to get $MFT!\n");

    /*
     * Bootstrap the main runlist before loading record 1. $MFTMirr starts
     * with a copy of MFT record 0; it is not the file record describing
     * $MFTMirr. That descriptor is MFT record 1 in the main $MFT stream.
     */
    if (MFTFile)
        MFTDataAttr = MFTFile->GetAttribute(TypeData, NULL);

    // Initialize $MFTMirr
    Status = GetFileRecord(_MFTMirr, &MFTMirrFile);
    if (!NT_SUCCESS(Status))
        DPRINT1("Failed to get $MFTMirr!\n");

    /* Cache the $DATA attributes so GetFileRecord() doesn't re-walk the
     * attribute list on every call. The persistent records memoize their
     * own decoded run lists.
     */
    if (MFTMirrFile)
        MFTMirrDataAttr = MFTMirrFile->GetAttribute(TypeData, NULL);
}

MasterFileTable::~MasterFileTable()
{
    delete MFTFile;
    delete MFTMirrFile;
}

NTSTATUS
MasterFileTable::QueryVolumeInformation(
    _Out_ PNtfsVolumeInformation Information)
{
    if (!Information)
        return STATUS_INVALID_PARAMETER;
    if (!MFTFile || !MFTDataAttr ||
        !MFTDataAttr->IsNonResident ||
        FileRecordSize == 0)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Information->BytesPerFileRecordSegment =
        FileRecordSize;
    Information->MftValidDataLength =
        MFTDataAttr->NonResident.DataSize;
    Information->MftStartLcn = MFTLCN;
    Information->Mft2StartLcn = MFTMirrLCN;

    /*
     * TotalReserved and the MFT-zone bounds deliberately remain zero.
     * This allocator does not reserve or enforce an MFT zone, so reporting
     * guessed bounds would promise space that is not actually protected.
     */
    return STATUS_SUCCESS;
}

NTSTATUS
MasterFileTable::WriteFileRecordToMFT(_In_ PFileRecord File)
{
    PUCHAR CommittedImage = NULL;
    PUCHAR SourceImage;
    NTSTATUS Status, WriteStatus;
    LARGE_INTEGER FileRecordOffset;
    BOOLEAN SelfReferential;

    // TODO: Add logging here

    if (!File || !File->Data || !File->Header ||
        FileRecordSize == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = File->CommitFixup();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Unable to commit fixup! (Status: 0x%X)\n", Status);
        return Status;
    }

    /*
     * The write routes through the $MFT/$MFTMirr attribute walkers. When
     * the record being persisted IS one of those metadata records, the
     * walk would read its own buffer with update-sequence bytes spliced
     * over live attribute bytes, so that one case serializes into a private
     * snapshot and restores the live record before writing. Every ordinary
     * record shares no buffer with the walker and writes its committed
     * buffer directly, reverting the fixup afterward.
     */
    SelfReferential =
        (File == MFTFile || File == MFTMirrFile);
    if (SelfReferential)
    {
        CommittedImage =
            new(PagedPool, TAG_MFT) UCHAR[FileRecordSize];
        if (!CommittedImage)
        {
            (void)File->ApplyFixup();
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(CommittedImage,
                      File->Data,
                      FileRecordSize);
        Status = File->ApplyFixup();
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Unable to revert fixup! (Status: 0x%X)\n", Status);
            delete[] CommittedImage;
            return Status;
        }
        SourceImage = CommittedImage;
    }
    else
    {
        SourceImage = File->Data;
    }

    ULONG FRSize = FileRecordSize;
    FileRecordOffset.QuadPart =
        (LONGLONG)((ULONGLONG)
            File->Header->MFTRecordNumber *
            FileRecordSize);

    // Write file record to $MFT.
    WriteStatus = MFTFile->WriteFileData(TypeData,
                                         NULL,
                                         SourceImage,
                                         &FRSize,
                                         &FileRecordOffset);

    if (!NT_SUCCESS(WriteStatus))
    {
        DPRINT1("Unable to write file record %lu! (Status: 0x%X)\n",
                File->Header->MFTRecordNumber,
                WriteStatus);
    }

    if (NT_SUCCESS(WriteStatus) &&
        IsFileRecordInMFTMirr(File->Header->MFTRecordNumber))
    {
        FRSize = FileRecordSize;

        // Write file record to $MFTMirr.
        WriteStatus = MFTMirrFile->WriteFileData(TypeData,
                                                 NULL,
                                                 SourceImage,
                                                 &FRSize,
                                                 &FileRecordOffset);

        if (!NT_SUCCESS(WriteStatus))
        {
            DPRINT1("Unable to write to MFT Mirror! (Status: 0x%X)\n",
                    WriteStatus);
        }
    }

    if (SelfReferential)
    {
        delete[] CommittedImage;
    }
    else
    {
        Status = File->ApplyFixup();
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("Unable to revert fixup! (Status: 0x%X)\n", Status);
            return Status;
        }
    }

    /*
     * User-visible mutation paths prepare their applicable timestamps in the
     * same record commit. This low-level writer also persists NTFS metadata
     * records and therefore must not invent semantic timestamp updates.
     */

    return WriteStatus;
}

NTSTATUS
MasterFileTable::IsFileRecordNumberInUse(_In_  ULONG FileRecordNumber,
                                         _Out_ PBOOLEAN InUse)
{
    NTSTATUS Status;
    UCHAR Bitmask;
    UCHAR BitmapSection;
    ULONG Size;

    if (!MFTFile)
        return STATUS_INVALID_DEVICE_STATE;

    Size = 1;
    Status = MFTFile->CopyData(TypeBitmap,
                               NULL,
                               &BitmapSection,
                               &Size,
                               FileRecordNumber >> 3);

    Bitmask = 1 << (FileRecordNumber % 8);

    if (!NT_SUCCESS(Status) || Size != 0)
    {
        DPRINT1("Failed to get bitmap!\n");
        return NT_SUCCESS(Status) ? STATUS_END_OF_FILE : Status;
    }

    *InUse = !!(BitmapSection & Bitmask);
    return STATUS_SUCCESS;
}

NTSTATUS
MasterFileTable::AllocateExtensionFileRecord(
    _In_ ULONGLONG BaseFileReference,
    _Out_ PFileRecord* File)
{
    if (!File)
        return STATUS_INVALID_PARAMETER;
    *File = NULL;
    if (GetSequenceFromFileRef(
            BaseFileReference) == 0 ||
        GetFRNFromFileRef(
            BaseFileReference) <=
            NTFS_LAST_RESERVED_FILE_RECORD ||
        GetFRNFromFileRef(
            BaseFileReference) > MAXULONG)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return AllocateFileRecord(
        BaseFileReference,
        FALSE,
        File);
}

NTSTATUS
MasterFileTable::AllocateBaseFileRecord(
    _In_ BOOLEAN IsDirectory,
    _Out_ PFileRecord* File)
{
    if (!File)
        return STATUS_INVALID_PARAMETER;
    *File = NULL;
    return AllocateFileRecord(
        0,
        IsDirectory,
        File);
}

NTSTATUS
MasterFileTable::AllocateFileRecord(
    _In_ ULONGLONG BaseFileReference,
    _In_ BOOLEAN IsDirectory,
    _Out_ PFileRecord* File)
{
    PAttribute BitmapAttribute;
    PFileRecord NewFile = NULL;
    PUCHAR BitmapBuffer = NULL;
    ULONGLONG BitmapDataLength;
    ULONGLONG BitmapByteCount;
    ULONGLONG ByteOffset;
    ULONGLONG Candidate = 0;
    ULONGLONG RecordCount;
    ULONGLONG RequiredBitmapLength;
    ULONG ChunkLength;
    ULONG ReadLength;
    USHORT SequenceNumber = 1;
    BOOLEAN Found = FALSE;
    BOOLEAN RecordWritten = FALSE;
    UCHAR BitmapByte;
    UCHAR BitMask;
    NTSTATUS Status;

    if (!File)
        return STATUS_INVALID_PARAMETER;
    *File = NULL;
    if (!DiskVolume || DiskVolume->IsReadOnly ||
        !MFTFile || !MFTDataAttr ||
        !MFTDataAttr->IsNonResident ||
        FileRecordSize == 0)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }
    if (BaseFileReference != 0 &&
        (GetSequenceFromFileRef(
             BaseFileReference) == 0 ||
         GetFRNFromFileRef(
             BaseFileReference) <=
             NTFS_LAST_RESERVED_FILE_RECORD ||
         GetFRNFromFileRef(
             BaseFileReference) > MAXULONG))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (MFTDataAttr->NonResident.DataSize <
            FileRecordSize ||
        MFTDataAttr->NonResident.DataSize %
            FileRecordSize != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    RecordCount =
        MFTDataAttr->NonResident.DataSize /
        FileRecordSize;
    if (RecordCount <
        MFT_FIRST_ORDINARY_FILE_RECORD)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    BitmapAttribute =
        MFTFile->GetAttribute(TypeBitmap, NULL);
    if (!BitmapAttribute)
        return STATUS_FILE_CORRUPT_ERROR;
    BitmapDataLength =
        GetAttributeDataSize(BitmapAttribute);
    BitmapByteCount =
        (RecordCount + 7) / 8;
    if (BitmapDataLength < BitmapByteCount)
        return STATUS_FILE_CORRUPT_ERROR;
    BitmapBuffer =
        new(PagedPool, TAG_MFT)
            UCHAR[MFT_ALLOCATION_BITMAP_CHUNK_SIZE];
    if (!BitmapBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    ByteOffset =
        MFT_FIRST_ORDINARY_FILE_RECORD / 8;
    while (ByteOffset < BitmapByteCount)
    {
        ChunkLength = (ULONG)min(
            BitmapByteCount - ByteOffset,
            (ULONGLONG)
                MFT_ALLOCATION_BITMAP_CHUNK_SIZE);
        ReadLength = ChunkLength;
        Status = MFTFile->CopyData(
            BitmapAttribute,
            BitmapBuffer,
            &ReadLength,
            ByteOffset);
        if (!NT_SUCCESS(Status) || ReadLength != 0)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_END_OF_FILE;
            goto Done;
        }

        for (ULONG Index = 0;
             Index < ChunkLength;
             Index++)
        {
            UCHAR ValidMask = 0xff;
            ULONGLONG FirstRecord =
                (ByteOffset + Index) * 8;

            if (FirstRecord + 8 > RecordCount)
            {
                ULONG ValidBits =
                    (ULONG)(RecordCount -
                            FirstRecord);
                ValidMask = (UCHAR)(
                    (1u << ValidBits) - 1u);
            }
            if ((BitmapBuffer[Index] &
                 ValidMask) != ValidMask)
            {
                for (ULONG Bit = 0;
                     Bit < 8;
                     Bit++)
                {
                    if ((ValidMask & (1u << Bit)) &&
                        !(BitmapBuffer[Index] &
                          (1u << Bit)))
                    {
                        Candidate =
                            FirstRecord + Bit;
                        Found = TRUE;
                        break;
                    }
                }
            }
            if (Found)
                break;
        }
        if (Found)
            break;
        ByteOffset += ChunkLength;
    }

    if (!Found)
    {
        Candidate = RecordCount;
        if (Candidate > MAXULONG ||
            Candidate >
                NTFS_MAX_SIGNED_OFFSET /
                    FileRecordSize)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }
    }

    RequiredBitmapLength =
        ALIGN_UP_BY((Candidate + 8) / 8,
                    sizeof(ULONGLONG));
    if (RequiredBitmapLength > BitmapDataLength)
    {
        static const UCHAR Zeroes[
            MFT_ALLOCATION_BITMAP_CHUNK_SIZE] = {};
        ULONGLONG Position = BitmapDataLength;

        while (Position < RequiredBitmapLength)
        {
            LARGE_INTEGER Offset;
            ULONG Length = (ULONG)min(
                RequiredBitmapLength - Position,
                (ULONGLONG)sizeof(Zeroes));

            if (Position >
                NTFS_MAX_SIGNED_OFFSET)
            {
                Status = STATUS_FILE_TOO_LARGE;
                goto Done;
            }
            Offset.QuadPart = (LONGLONG)Position;
            Status = MFTFile->WriteFileData(
                TypeBitmap,
                NULL,
                const_cast<PUCHAR>(Zeroes),
                &Length,
                &Offset);
            if (!NT_SUCCESS(Status))
                goto Done;
            Position += Length;
        }
        BitmapAttribute =
            MFTFile->GetAttribute(TypeBitmap,
                                  NULL);
        if (!BitmapAttribute ||
            GetAttributeDataSize(BitmapAttribute) <
                RequiredBitmapLength)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
    }

    if (Candidate < RecordCount)
    {
        PUCHAR RawRecord =
            new(PagedPool, TAG_MFT)
                UCHAR[FileRecordSize];
        if (!RawRecord)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        ReadLength = FileRecordSize;
        Status = MFTFile->CopyData(
            MFTDataAttr,
            RawRecord,
            &ReadLength,
            Candidate * FileRecordSize);
        if (NT_SUCCESS(Status) &&
            ReadLength == 0 &&
            RtlCompareMemory(RawRecord,
                             "FILE",
                             4) == 4)
        {
            PFileRecordHeader OldHeader =
                reinterpret_cast<PFileRecordHeader>(
                    RawRecord);
            if (OldHeader->MFTRecordNumber ==
                    Candidate &&
                OldHeader->SequenceNumber != 0)
            {
                SequenceNumber =
                    OldHeader->SequenceNumber;
            }
        }
        delete[] RawRecord;
        if (!NT_SUCCESS(Status) ||
            ReadLength != 0)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_END_OF_FILE;
            goto Done;
        }
    }

    NewFile = new(PagedPool, TAG_MFT)
        FileRecord(DiskVolume, FileRecordSize);
    if (!NewFile)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    Status = InitializeFileRecordBuffer(
        DiskVolume,
        FileRecordSize,
        (ULONG)Candidate,
        SequenceNumber,
        BaseFileReference,
        TRUE,
        NewFile);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (IsDirectory)
        NewFile->Header->Flags |= FR_IS_DIRECTORY;

    /*
     * Growing $MFT one record at a time interleaves its clusters with
     * ordinary allocations and floods record 0 with mapping pairs.
     * Reserve allocation in coarse chunks so the stream stays in long
     * runs; the record write below then never extends on its own.
     */
    {
        const ULONGLONG GrowthChunk =
            (ULONGLONG)MFT_ALLOCATION_GROWTH_CLUSTERS *
            BytesPerCluster(DiskVolume);
        ULONGLONG RecordEnd =
            ((ULONGLONG)Candidate + 1) *
            FileRecordSize;
        PAttribute CurrentDataAttr =
            MFTFile->GetAttribute(TypeData,
                                  NULL);

        if (CurrentDataAttr &&
            CurrentDataAttr->IsNonResident &&
            RecordEnd >
                CurrentDataAttr->
                    NonResident.AllocatedSize &&
            RecordEnd <=
                ~(ULONGLONG)0 - GrowthChunk)
        {
            NTSTATUS GrowthStatus =
                MFTFile->SetFileAllocationSize(
                    TypeData,
                    NULL,
                    ALIGN_UP_BY(RecordEnd,
                                GrowthChunk));
            if (!NT_SUCCESS(GrowthStatus))
            {
                DPRINT1(
                    "Chunked $MFT preallocation "
                    "failed: 0x%lx.\n",
                    GrowthStatus);
            }
        }
    }

    Status = WriteFileRecordToMFT(NewFile);
    if (!NT_SUCCESS(Status))
        goto Done;
    RecordWritten = TRUE;

    BitmapAttribute =
        MFTFile->GetAttribute(TypeBitmap, NULL);
    if (!BitmapAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    ByteOffset = Candidate >> 3;
    Status = ReadMftBitmapByte(
        MFTFile,
        BitmapAttribute,
        ByteOffset,
        &BitmapByte);
    if (!NT_SUCCESS(Status))
        goto Done;
    BitMask = (UCHAR)(1u << (Candidate & 7));
    if (BitmapByte & BitMask)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    Status = WriteMftBitmapByte(
        MFTFile,
        ByteOffset,
        (UCHAR)(BitmapByte | BitMask));
    if (!NT_SUCCESS(Status))
        goto Done;

    *File = NewFile;
    NewFile = NULL;
    Status = STATUS_SUCCESS;

Done:
    if (!NT_SUCCESS(Status) &&
        RecordWritten && NewFile)
    {
        (void)InitializeFileRecordBuffer(
            DiskVolume,
            FileRecordSize,
            NewFile->Header->MFTRecordNumber,
            NewFile->Header->SequenceNumber,
            0,
            FALSE,
            NewFile);
        (void)WriteFileRecordToMFT(NewFile);
    }
    delete NewFile;
    delete[] BitmapBuffer;
    return Status;
}

NTSTATUS
MasterFileTable::DeallocateExtensionFileRecord(
    _Inout_ PFileRecord File)
{
    if (!File || !File->Header ||
        File->Header->BaseFileRecord == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return DeallocateFileRecord(File);
}

NTSTATUS
MasterFileTable::DeallocateBaseFileRecord(
    _Inout_ PFileRecord File)
{
    if (!File || !File->Header ||
        File->Header->BaseFileRecord != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return DeallocateFileRecord(File);
}

NTSTATUS
MasterFileTable::DeallocateFileRecord(
    _Inout_ PFileRecord File)
{
    PAttribute BitmapAttribute;
    ULONG FileRecordNumber;
    USHORT SequenceNumber;
    ULONGLONG ByteOffset;
    UCHAR BitmapByte;
    UCHAR BitMask;
    NTSTATUS Status;

    if (!DiskVolume || DiskVolume->IsReadOnly ||
        !MFTFile || !File || !File->Header ||
        !(File->Header->Flags & FR_IN_USE) ||
        File->Header->MFTRecordNumber <
            MFT_FIRST_ORDINARY_FILE_RECORD)
    {
        return STATUS_INVALID_PARAMETER;
    }

    FileRecordNumber =
        File->Header->MFTRecordNumber;
    BitmapAttribute =
        MFTFile->GetAttribute(TypeBitmap, NULL);
    if (!BitmapAttribute)
        return STATUS_FILE_CORRUPT_ERROR;
    ByteOffset = FileRecordNumber >> 3;
    Status = ReadMftBitmapByte(
        MFTFile,
        BitmapAttribute,
        ByteOffset,
        &BitmapByte);
    if (!NT_SUCCESS(Status))
        return Status;

    BitMask =
        (UCHAR)(1u << (FileRecordNumber & 7));
    if (!(BitmapByte & BitMask))
        return STATUS_NOT_FOUND;

    SequenceNumber =
        File->Header->SequenceNumber + 1;
    if (SequenceNumber == 0)
        SequenceNumber = 1;
    Status = InitializeFileRecordBuffer(
        DiskVolume,
        FileRecordSize,
        FileRecordNumber,
        SequenceNumber,
        0,
        FALSE,
        File);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = WriteFileRecordToMFT(File);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * Publish the record as free only after its in-use contents are gone.
     * A bitmap write failure therefore leaks a blank record rather than
     * allowing a later allocation to reuse a still-live extension record.
     */
    return WriteMftBitmapByte(
        MFTFile,
        ByteOffset,
        (UCHAR)(BitmapByte & ~BitMask));
}
