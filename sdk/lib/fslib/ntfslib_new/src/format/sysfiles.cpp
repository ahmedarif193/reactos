/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS volume formatter: system files and metadata
 */

#include "formatint.h"

#define VOLUME_INFORMATION_SIZE 12
#define NTFS_MAJOR_VERSION 3
#define NTFS_MINOR_VERSION 1

/* On-disk sizes; see the note in record.cpp about structure padding. */
#define INDEX_ENTRY_HEADER_SIZE 0x10

/* NTFSRecordHeader + VCN, i.e. what precedes an INDX node header. */
#define INDEX_BUFFER_HEADER_SIZE 0x18

/* $MFT records 0-23 are the metadata region; see mkntfs for the same rule. */
#define NTFS_LAST_SEQUENCED_RECORD 23

#define ROOT_REFERENCE NTFS_ROOT_FILE_REFERENCE

#define SYSTEM_FILE_PERMISSIONS (FILE_PERM_HIDDEN | FILE_PERM_SYSTEM)
#define SYSTEM_FILE_NAME_FLAGS  (FN_HIDDEN | FN_SYSTEM)

typedef struct FormatExtent
{
    ULONGLONG Lcn;
    ULONGLONG Count;
} FormatExtent;

static USHORT
FormatSequenceFor(_In_ ULONG RecordNumber)
{
    if (RecordNumber == 0 || RecordNumber > NTFS_LAST_SEQUENCED_RECORD)
        return 1;

    return (USHORT)RecordNumber;
}

/* Writes the record currently in Ctx->RecordBuffer to its slot in $MFT, and
 * additionally to $MFTMirr for the four records the mirror covers. */
static NTSTATUS
FormatFlushRecord(_In_ PFormatContext Ctx,
                  _In_ ULONG RecordNumber)
{
    ULONGLONG Offset;
    NTSTATUS Status;

    Status = FormatEndRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Offset = Ctx->MftLcn * Ctx->ClusterSize +
             (ULONGLONG)RecordNumber * Ctx->MftRecordSize;

    Status = FormatWriteAt(Ctx, Offset, Ctx->MftRecordSize, Ctx->RecordBuffer);
    if (!NT_SUCCESS(Status))
        return Status;

    if (RecordNumber < 4)
    {
        Offset = Ctx->MftMirrLcn * Ctx->ClusterSize +
                 (ULONGLONG)RecordNumber * Ctx->MftRecordSize;

        Status = FormatWriteAt(Ctx,
                               Offset,
                               Ctx->MftRecordSize,
                               Ctx->RecordBuffer);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

/*
 * Starts a system file record: $STANDARD_INFORMATION followed by the
 * $FILE_NAME that links it into the root directory.
 */
static NTSTATUS
FormatBeginSystemFile(_In_ PFormatContext Ctx,
                      _In_ ULONG RecordNumber,
                      _In_ PCWSTR Name,
                      _In_ USHORT RecordFlags,
                      _In_ ULONG NameFlags,
                      _In_ ULONGLONG AllocatedSize,
                      _In_ ULONGLONG DataSize)
{
    StandardInformationEx Information;

    FormatBeginRecord(Ctx,
                      RecordNumber,
                      FormatSequenceFor(RecordNumber),
                      RecordFlags);

    FormatFillStandardInformation(Ctx, &Information, SYSTEM_FILE_PERMISSIONS);
    if (!FormatAddResident(Ctx,
                           TypeStandardInformation,
                           NULL,
                           &Information,
                           sizeof(Information),
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FormatAddFileName(Ctx,
                             ROOT_REFERENCE,
                             Name,
                             SYSTEM_FILE_NAME_FLAGS | NameFlags,
                             NAME_TYPE_WIN32_AND_DOS,
                             AllocatedSize,
                             DataSize);
}

/* A plain metadata file whose single unnamed $DATA covers one extent. */
static NTSTATUS
FormatWriteSimpleDataFile(_In_ PFormatContext Ctx,
                          _In_ ULONG RecordNumber,
                          _In_ PCWSTR Name,
                          _In_ ULONGLONG Lcn,
                          _In_ ULONGLONG ClusterCount,
                          _In_ ULONGLONG DataSize)
{
    NTSTATUS Status;

    Status = FormatBeginSystemFile(Ctx,
                                   RecordNumber,
                                   Name,
                                   FR_IN_USE,
                                   0,
                                   ClusterCount * Ctx->ClusterSize,
                                   DataSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!FormatAddNonResident(Ctx,
                              TypeData,
                              NULL,
                              Lcn,
                              ClusterCount,
                              DataSize,
                              DataSize,
                              0,
                              FALSE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FormatFlushRecord(Ctx, RecordNumber);
}

static NTSTATUS
FormatWriteMftRecord(_In_ PFormatContext Ctx)
{
    NTSTATUS Status;

    Status = FormatBeginSystemFile(Ctx,
                                   _MFT,
                                   L"$MFT",
                                   FR_IN_USE,
                                   0,
                                   Ctx->MftClusters * Ctx->ClusterSize,
                                   Ctx->MftDataSize);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!FormatAddNonResident(Ctx,
                              TypeData,
                              NULL,
                              Ctx->MftLcn,
                              Ctx->MftClusters,
                              Ctx->MftDataSize,
                              Ctx->MftDataSize,
                              0,
                              FALSE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Which MFT records are in use. */
    if (!FormatAddNonResident(Ctx,
                              TypeBitmap,
                              NULL,
                              Ctx->MftBitmapLcn,
                              Ctx->MftBitmapClusters,
                              Ctx->MftBitmapDataSize,
                              Ctx->MftBitmapDataSize,
                              0,
                              FALSE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FormatFlushRecord(Ctx, _MFT);
}

static NTSTATUS
FormatWriteVolumeRecord(_In_ PFormatContext Ctx)
{
    UCHAR Information[VOLUME_INFORMATION_SIZE];
    PVolumeInformationEx VolumeInformation = (PVolumeInformationEx)Information;
    ULONG LabelLength = 0;
    NTSTATUS Status;

    Status = FormatBeginSystemFile(Ctx, _Volume, L"$Volume", FR_IN_USE, 0, 0, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Ctx->Params->VolumeLabel)
    {
        while (Ctx->Params->VolumeLabel[LabelLength] != L'\0' &&
               LabelLength < NTFS_FORMAT_MAX_LABEL_CHARS)
        {
            LabelLength++;
        }
    }

    if (LabelLength != 0)
    {
        if (!FormatAddResident(Ctx,
                               TypeVolumeName,
                               NULL,
                               Ctx->Params->VolumeLabel,
                               LabelLength * sizeof(WCHAR),
                               0))
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    RtlZeroMemory(Information, sizeof(Information));
    VolumeInformation->MajorVersion = NTFS_MAJOR_VERSION;
    VolumeInformation->MinorVersion = NTFS_MINOR_VERSION;
    VolumeInformation->Flags = 0;

    if (!FormatAddResident(Ctx,
                           TypeVolumeInformation,
                           NULL,
                           Information,
                           VOLUME_INFORMATION_SIZE,
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FormatFlushRecord(Ctx, _Volume);
}

/*
 * The system files live in the root directory, so the root index has to list
 * them. Order matters: NTFS index entries are sorted by the collation rule,
 * which for $I30 is the upcased file name.
 */
typedef struct FormatRootEntry
{
    ULONG RecordNumber;
    PCWSTR Name;
    ULONG NameFlags;
} FormatRootEntry;

static const FormatRootEntry FormatRootEntries[] =
{
    { _AttrDef,  L"$AttrDef",  0 },
    { _BadClus,  L"$BadClus",  0 },
    { _Bitmap,   L"$Bitmap",   0 },
    { _Boot,     L"$Boot",     0 },
    { _Extend,   L"$Extend",   FN_DIRECTORY },
    { _LogFile,  L"$LogFile",  0 },
    { _MFT,      L"$MFT",      0 },
    { _MFTMirr,  L"$MFTMirr",  0 },
    { _Secure,   L"$Secure",   FN_INDEX_VIEW },
    { _UpCase,   L"$UpCase",   0 },
    { _Volume,   L"$Volume",   0 },
};

/*
 * Index entries cache the file's sizes alongside its name, so report the same
 * values the file record carries.
 */
static void
FormatSystemFileSizes(_In_ PFormatContext Ctx,
                      _In_ ULONG RecordNumber,
                      _Out_ PULONGLONG AllocatedSize,
                      _Out_ PULONGLONG DataSize)
{
    switch (RecordNumber)
    {
    case _MFT:
        *AllocatedSize = Ctx->MftClusters * Ctx->ClusterSize;
        *DataSize = Ctx->MftDataSize;
        return;
    case _MFTMirr:
        *AllocatedSize = Ctx->MftMirrClusters * Ctx->ClusterSize;
        *DataSize = 4ULL * Ctx->MftRecordSize;
        return;
    case _LogFile:
        *AllocatedSize = Ctx->LogFileClusters * Ctx->ClusterSize;
        *DataSize = Ctx->LogFileSize;
        return;
    case _AttrDef:
        *AllocatedSize = Ctx->AttrDefClusters * Ctx->ClusterSize;
        *DataSize = NTFS_ATTRDEF_SIZE;
        return;
    case _Bitmap:
        *AllocatedSize = Ctx->BitmapClusters * Ctx->ClusterSize;
        *DataSize = Ctx->BitmapDataSize;
        return;
    case _Boot:
        *AllocatedSize = Ctx->BootClusters * Ctx->ClusterSize;
        *DataSize = NTFS_BOOT_AREA_SIZE;
        return;
    case _UpCase:
        *AllocatedSize = Ctx->UpCaseClusters * Ctx->ClusterSize;
        *DataSize = NTFS_UPCASE_SIZE;
        return;
    default:
        /* $Volume, $BadClus, $Secure and $Extend have no unnamed data. */
        *AllocatedSize = 0;
        *DataSize = 0;
        return;
    }
}

/*
 * Writes the single index block backing the root directory. Entries are
 * emitted in the order of the table above, which is already collation order
 * for these names.
 */
static NTSTATUS
FormatWriteRootIndexBlock(_In_ PFormatContext Ctx)
{
    PUCHAR Block = Ctx->TransferBuffer;
    ULONG BlockSize = Ctx->IndexRecordSize;
    ULONG AllocatedBytes = (ULONG)(Ctx->RootIndexClusters * Ctx->ClusterSize);
    PIndexBuffer Index = (PIndexBuffer)Block;
    USHORT UsaCount = (USHORT)(BlockSize / Ctx->BytesPerSector + 1);
    ULONG EntriesOffset;
    ULONG Offset;
    ULONG Number;
    PIndexEntry End;

    if (AllocatedBytes > Ctx->TransferSize)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Block, AllocatedBytes);

    Index->RecordHeader.TypeID[0] = 'I';
    Index->RecordHeader.TypeID[1] = 'N';
    Index->RecordHeader.TypeID[2] = 'D';
    Index->RecordHeader.TypeID[3] = 'X';
    Index->RecordHeader.UpdateSequenceOffset = 0x28;
    Index->RecordHeader.SizeOfUpdateSequence = UsaCount;
    Index->RecordHeader.LogFileSequenceNumber = 0;
    Index->VCN = 0;

    /* Entries start after the update sequence array, 8 byte aligned. */
    EntriesOffset = ALIGN_UP_BY(0x28 + UsaCount * sizeof(USHORT), 8);

    /* Offsets in the node header are relative to the node header itself. */
    Index->IndexHeader.IndexOffset = EntriesOffset - INDEX_BUFFER_HEADER_SIZE;
    Index->IndexHeader.AllocatedSize = BlockSize - INDEX_BUFFER_HEADER_SIZE;
    Index->IndexHeader.Flags = 0;

    Offset = EntriesOffset;
    for (Number = 0; Number < RTL_NUMBER_OF(FormatRootEntries); Number++)
    {
        const FormatRootEntry* Entry = &FormatRootEntries[Number];
        ULONGLONG Reference;
        ULONGLONG AllocatedSize, DataSize;
        ULONG EntryLength;

        Reference = NTFS_MK_FILE_REFERENCE(Entry->RecordNumber,
                                           FormatSequenceFor(Entry->RecordNumber));

        FormatSystemFileSizes(Ctx,
                              Entry->RecordNumber,
                              &AllocatedSize,
                              &DataSize);

        EntryLength = FormatBuildFileNameIndexEntry(Ctx,
                                                    Block + Offset,
                                                    Reference,
                                                    Entry->Name,
                                                    SYSTEM_FILE_NAME_FLAGS |
                                                        Entry->NameFlags,
                                                    AllocatedSize,
                                                    DataSize);
        if (EntryLength == 0)
            return STATUS_INVALID_PARAMETER;

        Offset += EntryLength;

        /* Keep room for the terminating end entry. */
        if (Offset + INDEX_ENTRY_HEADER_SIZE > BlockSize)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    End = (PIndexEntry)(Block + Offset);
    End->EntryLength = INDEX_ENTRY_HEADER_SIZE;
    End->StreamLength = 0;
    End->Flags = INDEX_ENTRY_END;
    Offset += INDEX_ENTRY_HEADER_SIZE;

    Index->IndexHeader.TotalIndexSize = Offset - INDEX_BUFFER_HEADER_SIZE;

    FormatCommitFixupBuffer(Ctx, Block, BlockSize);

    return FormatWriteCluster(Ctx, Ctx->RootIndexLcn, AllocatedBytes, Block);
}

static NTSTATUS
FormatWriteRootRecord(_In_ PFormatContext Ctx)
{
    UCHAR SecurityDescriptor[192];
    StandardInformationEx Information;
    ULONG SecurityLength;
    UCHAR IndexBitmap[8];
    NTSTATUS Status;

    FormatBeginRecord(Ctx,
                      _Root,
                      FormatSequenceFor(_Root),
                      FR_IN_USE | FR_IS_DIRECTORY);

    FormatFillStandardInformation(Ctx, &Information, SYSTEM_FILE_PERMISSIONS);
    if (!FormatAddResident(Ctx,
                           TypeStandardInformation,
                           NULL,
                           &Information,
                           sizeof(Information),
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* The root directory is its own parent. */
    Status = FormatAddFileName(Ctx,
                               ROOT_REFERENCE,
                               L".",
                               SYSTEM_FILE_NAME_FLAGS | FN_DIRECTORY,
                               NAME_TYPE_WIN32_AND_DOS,
                               0,
                               0);
    if (!NT_SUCCESS(Status))
        return Status;

    SecurityLength = FormatBuildDefaultSecurityDescriptor(SecurityDescriptor,
                                                          sizeof(SecurityDescriptor));
    if (SecurityLength != 0)
    {
        if (!FormatAddResident(Ctx,
                               TypeSecurityDescriptor,
                               NULL,
                               SecurityDescriptor,
                               SecurityLength,
                               0))
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    /*
     * The system-file entries do not fit in the record, so the root uses an
     * $INDEX_ROOT node plus an $INDEX_ALLOCATION block and its bitmap.
     */
    Status = FormatAddIndexRootNode(Ctx,
                                    TypeFileName,
                                    ATTRDEF_COLLATION_FILENAME,
                                    L"$I30");
    if (!NT_SUCCESS(Status))
        return Status;

    if (!FormatAddNonResident(Ctx,
                              TypeIndexAllocation,
                              L"$I30",
                              Ctx->RootIndexLcn,
                              Ctx->RootIndexClusters,
                              Ctx->RootIndexClusters * Ctx->ClusterSize,
                              Ctx->RootIndexClusters * Ctx->ClusterSize,
                              0,
                              FALSE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* One bit per index block; the single block we wrote is in use. */
    RtlZeroMemory(IndexBitmap, sizeof(IndexBitmap));
    IndexBitmap[0] = 0x01;

    if (!FormatAddResident(Ctx,
                           TypeBitmap,
                           L"$I30",
                           IndexBitmap,
                           sizeof(IndexBitmap),
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = FormatFlushRecord(Ctx, _Root);
    if (!NT_SUCCESS(Status))
        return Status;

    return FormatWriteRootIndexBlock(Ctx);
}

static NTSTATUS
FormatWriteBadClusRecord(_In_ PFormatContext Ctx)
{
    NTSTATUS Status;

    Status = FormatBeginSystemFile(Ctx, _BadClus, L"$BadClus", FR_IN_USE, 0, 0, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    /* The unnamed stream is empty; the bad cluster list lives in $Bad. */
    if (!FormatAddResident(Ctx, TypeData, NULL, NULL, 0, 0))
        return STATUS_INSUFFICIENT_RESOURCES;

    /*
     * $Bad is a sparse stream spanning the whole volume: every cluster it
     * reports as a hole is a good cluster. A fresh volume has no bad ones.
     */
    if (!FormatAddNonResident(Ctx,
                              TypeData,
                              L"$Bad",
                              0,
                              Ctx->TotalClusters,
                              Ctx->TotalClusters * Ctx->ClusterSize,
                              0,
                              0,
                              TRUE))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return FormatFlushRecord(Ctx, _BadClus);
}

static NTSTATUS
FormatWriteSecureRecord(_In_ PFormatContext Ctx)
{
    NTSTATUS Status;

    Status = FormatBeginSystemFile(Ctx, _Secure, L"$Secure", FR_IN_USE, 0, 0, 0);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Empty security descriptor stream plus its two empty view indexes. */
    if (!FormatAddResident(Ctx, TypeData, L"$SDS", NULL, 0, 0))
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = FormatAddEmptyDirectoryIndex(Ctx,
                                          0,
                                          ATTRDEF_COLLATION_SEC_HASH,
                                          L"$SDH");
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatAddEmptyDirectoryIndex(Ctx,
                                          0,
                                          ATTRDEF_COLLATION_ULONG,
                                          L"$SII");
    if (!NT_SUCCESS(Status))
        return Status;

    return FormatFlushRecord(Ctx, _Secure);
}

static NTSTATUS
FormatWriteExtendRecord(_In_ PFormatContext Ctx)
{
    StandardInformationEx Information;
    NTSTATUS Status;

    FormatBeginRecord(Ctx,
                      _Extend,
                      FormatSequenceFor(_Extend),
                      FR_IN_USE | FR_IS_DIRECTORY);

    FormatFillStandardInformation(Ctx, &Information, SYSTEM_FILE_PERMISSIONS);
    if (!FormatAddResident(Ctx,
                           TypeStandardInformation,
                           NULL,
                           &Information,
                           sizeof(Information),
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = FormatAddFileName(Ctx,
                               ROOT_REFERENCE,
                               L"$Extend",
                               SYSTEM_FILE_NAME_FLAGS | FN_DIRECTORY,
                               NAME_TYPE_WIN32_AND_DOS,
                               0,
                               0);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatAddEmptyDirectoryIndex(Ctx,
                                          TypeFileName,
                                          ATTRDEF_COLLATION_FILENAME,
                                          L"$I30");
    if (!NT_SUCCESS(Status))
        return Status;

    return FormatFlushRecord(Ctx, _Extend);
}

/*
 * Records past $Extend up to NTFS_LAST_RESERVED_FILE_RECORD are reserved by
 * NTFS. They are marked in use in the $MFT bitmap, so they must be readable
 * records; like the reserved records Windows creates they carry only
 * $STANDARD_INFORMATION and an empty $DATA and are linked into no directory.
 *
 * Reserving the whole range matters: readers hide records at or below
 * NTFS_LAST_RESERVED_FILE_RECORD from directory enumeration, so a user file
 * allocated there would be invisible.
 *
 * Everything above that gets a valid but free FILE header, which keeps the
 * MFT uniformly parseable.
 */
static NTSTATUS
FormatWriteRemainingRecords(_In_ PFormatContext Ctx)
{
    ULONG RecordNumber;
    NTSTATUS Status;

    for (RecordNumber = _Extend + 1;
         RecordNumber < Ctx->MftRecordCount;
         RecordNumber++)
    {
        BOOLEAN Reserved = (RecordNumber < NTFS_FORMAT_RESERVED_RECORDS);

        FormatBeginRecord(Ctx,
                          RecordNumber,
                          FormatSequenceFor(RecordNumber),
                          Reserved ? FR_IN_USE : 0);

        if (Reserved)
        {
            StandardInformationEx Information;

            FormatFillStandardInformation(Ctx,
                                          &Information,
                                          SYSTEM_FILE_PERMISSIONS);
            if (!FormatAddResident(Ctx,
                                   TypeStandardInformation,
                                   NULL,
                                   &Information,
                                   sizeof(Information),
                                   0))
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            if (!FormatAddResident(Ctx, TypeData, NULL, NULL, 0, 0))
                return STATUS_INSUFFICIENT_RESOURCES;
        }

        Status = FormatFlushRecord(Ctx, RecordNumber);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
FormatWriteMftBitmap(_In_ PFormatContext Ctx)
{
    ULONG Length = (ULONG)(Ctx->MftBitmapClusters * Ctx->ClusterSize);
    ULONG Bit;

    if (Length > Ctx->TransferSize)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Ctx->TransferBuffer, Length);

    for (Bit = 0; Bit < NTFS_FORMAT_RESERVED_RECORDS; Bit++)
        Ctx->TransferBuffer[Bit / 8] |= (UCHAR)(1u << (Bit % 8));

    return FormatWriteCluster(Ctx, Ctx->MftBitmapLcn, Length, Ctx->TransferBuffer);
}

/*
 * Writes the volume bitmap in chunks so that a large volume never needs the
 * whole bitmap resident at once.
 */
static NTSTATUS
FormatWriteVolumeBitmap(_In_ PFormatContext Ctx)
{
    FormatExtent Extents[9];
    ULONG ExtentCount = 0;
    ULONGLONG TotalBytes = Ctx->BitmapClusters * Ctx->ClusterSize;
    ULONGLONG Written = 0;
    NTSTATUS Status;

    Extents[ExtentCount].Lcn = Ctx->BootLcn;
    Extents[ExtentCount++].Count = Ctx->BootClusters;
    Extents[ExtentCount].Lcn = Ctx->MftLcn;
    Extents[ExtentCount++].Count = Ctx->MftClusters;
    Extents[ExtentCount].Lcn = Ctx->MftBitmapLcn;
    Extents[ExtentCount++].Count = Ctx->MftBitmapClusters;
    Extents[ExtentCount].Lcn = Ctx->LogFileLcn;
    Extents[ExtentCount++].Count = Ctx->LogFileClusters;
    Extents[ExtentCount].Lcn = Ctx->BitmapLcn;
    Extents[ExtentCount++].Count = Ctx->BitmapClusters;
    Extents[ExtentCount].Lcn = Ctx->UpCaseLcn;
    Extents[ExtentCount++].Count = Ctx->UpCaseClusters;
    Extents[ExtentCount].Lcn = Ctx->AttrDefLcn;
    Extents[ExtentCount++].Count = Ctx->AttrDefClusters;
    Extents[ExtentCount].Lcn = Ctx->RootIndexLcn;
    Extents[ExtentCount++].Count = Ctx->RootIndexClusters;
    Extents[ExtentCount].Lcn = Ctx->MftMirrLcn;
    Extents[ExtentCount++].Count = Ctx->MftMirrClusters;

    while (Written < TotalBytes)
    {
        ULONG Chunk = (TotalBytes - Written) > Ctx->TransferSize
                          ? Ctx->TransferSize
                          : (ULONG)(TotalBytes - Written);
        ULONGLONG FirstBit = Written * 8;
        ULONGLONG LastBit = FirstBit + (ULONGLONG)Chunk * 8;
        ULONG Index;

        RtlZeroMemory(Ctx->TransferBuffer, Chunk);

        for (Index = 0; Index < ExtentCount; Index++)
        {
            ULONGLONG Start = Extents[Index].Lcn;
            ULONGLONG End = Start + Extents[Index].Count;
            ULONGLONG Bit;

            if (End <= FirstBit || Start >= LastBit)
                continue;

            if (Start < FirstBit)
                Start = FirstBit;
            if (End > LastBit)
                End = LastBit;

            for (Bit = Start; Bit < End; Bit++)
            {
                ULONGLONG Local = Bit - FirstBit;
                Ctx->TransferBuffer[Local / 8] |= (UCHAR)(1u << (Local % 8));
            }
        }

        /* Clusters that do not exist must never look available. */
        if (LastBit > Ctx->TotalClusters)
        {
            ULONGLONG Bit = Ctx->TotalClusters > FirstBit ? Ctx->TotalClusters
                                                          : FirstBit;

            for (; Bit < LastBit; Bit++)
            {
                ULONGLONG Local = Bit - FirstBit;
                Ctx->TransferBuffer[Local / 8] |= (UCHAR)(1u << (Local % 8));
            }
        }

        Status = FormatWriteAt(Ctx,
                               Ctx->BitmapLcn * Ctx->ClusterSize + Written,
                               Chunk,
                               Ctx->TransferBuffer);
        if (!NT_SUCCESS(Status))
            return Status;

        Written += Chunk;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
FormatWriteUpCase(_In_ PFormatContext Ctx)
{
    PWCHAR Table;
    ULONGLONG Offset = Ctx->UpCaseLcn * Ctx->ClusterSize;
    ULONGLONG Written = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    Table = (PWCHAR)Ctx->Params->Allocate(Ctx->Params->IoContext,
                                          NTFS_UPCASE_SIZE);
    if (!Table)
        return STATUS_INSUFFICIENT_RESOURCES;

    FormatBuildUpCaseTable(Table);

    while (Written < NTFS_UPCASE_SIZE)
    {
        ULONG Chunk = (NTFS_UPCASE_SIZE - Written) > Ctx->TransferSize
                          ? Ctx->TransferSize
                          : (ULONG)(NTFS_UPCASE_SIZE - Written);

        Status = FormatWriteAt(Ctx,
                               Offset + Written,
                               Chunk,
                               (PUCHAR)Table + Written);
        if (!NT_SUCCESS(Status))
            break;

        Written += Chunk;
    }

    Ctx->Params->Free(Ctx->Params->IoContext, Table);

    if (!NT_SUCCESS(Status))
        return Status;

    /* Pad the rest of the final cluster. */
    if (Ctx->UpCaseClusters * Ctx->ClusterSize > NTFS_UPCASE_SIZE)
    {
        ULONG Tail = (ULONG)(Ctx->UpCaseClusters * Ctx->ClusterSize -
                             NTFS_UPCASE_SIZE);

        RtlZeroMemory(Ctx->TransferBuffer, Tail);
        Status = FormatWriteAt(Ctx,
                               Offset + NTFS_UPCASE_SIZE,
                               Tail,
                               Ctx->TransferBuffer);
    }

    return Status;
}

static NTSTATUS
FormatWriteAttrDef(_In_ PFormatContext Ctx)
{
    ULONG Length = (ULONG)(Ctx->AttrDefClusters * Ctx->ClusterSize);

    if (Length > Ctx->TransferSize)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Ctx->TransferBuffer, Length);
    FormatBuildAttrDefTable((PAttrDefEntry)Ctx->TransferBuffer);

    return FormatWriteCluster(Ctx, Ctx->AttrDefLcn, Length, Ctx->TransferBuffer);
}

NTSTATUS
FormatWriteMetadata(_In_ PFormatContext Ctx)
{
    NTSTATUS Status;

    /*
     * An all-0xFF $LogFile is the canonical empty journal; ntfslib's LFS
     * recognises it and skips restart-page recovery entirely.
     */
    Status = FormatFillClusters(Ctx, Ctx->LogFileLcn, Ctx->LogFileClusters, 0xFF);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteUpCase(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteAttrDef(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteVolumeBitmap(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteMftBitmap(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteMftRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _MFTMirr,
                                       L"$MFTMirr",
                                       Ctx->MftMirrLcn,
                                       Ctx->MftMirrClusters,
                                       4ULL * Ctx->MftRecordSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _LogFile,
                                       L"$LogFile",
                                       Ctx->LogFileLcn,
                                       Ctx->LogFileClusters,
                                       Ctx->LogFileSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteVolumeRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _AttrDef,
                                       L"$AttrDef",
                                       Ctx->AttrDefLcn,
                                       Ctx->AttrDefClusters,
                                       NTFS_ATTRDEF_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteRootRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _Bitmap,
                                       L"$Bitmap",
                                       Ctx->BitmapLcn,
                                       Ctx->BitmapClusters,
                                       Ctx->BitmapDataSize);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _Boot,
                                       L"$Boot",
                                       Ctx->BootLcn,
                                       Ctx->BootClusters,
                                       NTFS_BOOT_AREA_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteBadClusRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSecureRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteSimpleDataFile(Ctx,
                                       _UpCase,
                                       L"$UpCase",
                                       Ctx->UpCaseLcn,
                                       Ctx->UpCaseClusters,
                                       NTFS_UPCASE_SIZE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = FormatWriteExtendRecord(Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    return FormatWriteRemainingRecords(Ctx);
}
