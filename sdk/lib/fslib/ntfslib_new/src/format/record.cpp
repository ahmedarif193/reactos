/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS volume formatter: MFT record construction
 */

#include "formatint.h"

/*
 * On-disk sizes that differ from sizeof() because ntfsattribdef.h is compiled
 * with natural alignment: the compiler pads these structures, NTFS does not.
 */
#define ATTRIBUTE_RESIDENT_HEADER_SIZE     0x18
#define ATTRIBUTE_NONRESIDENT_HEADER_SIZE  0x40
#define ATTRIBUTE_SPARSE_HEADER_SIZE       0x48
#define FILE_RECORD_HEADER_SIZE            0x30
#define VOLUME_INFORMATION_SIZE            12
#define INDEX_ENTRY_HEADER_SIZE            0x10
#define FILE_NAME_HEADER_SIZE              0x42

static ULONG
FormatStringLength(_In_opt_ PCWSTR String)
{
    ULONG Length = 0;

    if (!String)
        return 0;

    while (String[Length] != L'\0')
        Length++;

    return Length;
}

/*
 * NTFS encodes a record/index size either as a positive count of clusters or,
 * when it is smaller than a cluster, as the negated base-2 logarithm.
 */
static INT8
FormatEncodeSizeInClusters(_In_ ULONG Size,
                           _In_ ULONG ClusterSize)
{
    INT8 Log;

    if (Size >= ClusterSize)
        return (INT8)(Size / ClusterSize);

    for (Log = 0; Log < 32; Log++)
    {
        if ((1UL << Log) == Size)
            return (INT8)(-Log);
    }

    return 0;
}

void
FormatBeginRecord(_In_ PFormatContext Ctx,
                  _In_ ULONG RecordNumber,
                  _In_ USHORT SequenceNumber,
                  _In_ USHORT Flags)
{
    PFileRecordHeader Header = (PFileRecordHeader)Ctx->RecordBuffer;
    USHORT UsaCount = (USHORT)(Ctx->MftRecordSize / Ctx->BytesPerSector + 1);

    RtlZeroMemory(Ctx->RecordBuffer, Ctx->MftRecordSize);

    Header->Header.TypeID[0] = 'F';
    Header->Header.TypeID[1] = 'I';
    Header->Header.TypeID[2] = 'L';
    Header->Header.TypeID[3] = 'E';
    Header->Header.UpdateSequenceOffset = FILE_RECORD_HEADER_SIZE;
    Header->Header.SizeOfUpdateSequence = UsaCount;
    Header->Header.LogFileSequenceNumber = 0;

    Header->SequenceNumber = SequenceNumber;
    Header->HardLinkCount = 1;
    Header->AttributeOffset = (USHORT)ALIGN_UP_BY(
        FILE_RECORD_HEADER_SIZE + UsaCount * sizeof(USHORT), 8);
    Header->Flags = Flags;
    Header->AllocatedSize = Ctx->MftRecordSize;
    Header->BaseFileRecord = 0;
    Header->MFTRecordNumber = RecordNumber;

    Ctx->RecordOffset = Header->AttributeOffset;
    Ctx->NextAttributeId = 0;
}

/*
 * Reserves and initialises an attribute header. Returns NULL when the record
 * has no room left, which the callers treat as a layout bug.
 */
static PAttribute
FormatBeginAttribute(_In_ PFormatContext Ctx,
                     _In_ ULONG Type,
                     _In_opt_ PCWSTR Name,
                     _In_ ULONG HeaderSize,
                     _In_ ULONG ValueLength,
                     _Out_ PULONG ValueOffset)
{
    ULONG NameLength = FormatStringLength(Name);
    ULONG NameOffset = HeaderSize;
    ULONG Offset;
    ULONG Length;
    PAttribute Attr;

    Offset = ALIGN_UP_BY(NameOffset + NameLength * sizeof(WCHAR), 8);
    Length = ALIGN_UP_BY(Offset + ValueLength, 8);

    /* Leave room for the 8 byte end-of-attributes marker. */
    if (Ctx->RecordOffset + Length + 8 > Ctx->MftRecordSize)
        return NULL;

    Attr = (PAttribute)(Ctx->RecordBuffer + Ctx->RecordOffset);
    Attr->AttributeType = Type;
    Attr->Length = Length;
    Attr->NameLength = (UINT8)NameLength;
    /* Windows records the offset just past the fixed header even when the
     * attribute is unnamed, and readers rely on it to bound the value. */
    Attr->NameOffset = (UINT16)NameOffset;
    Attr->Flags = 0;
    Attr->AttributeID = Ctx->NextAttributeId++;

    if (NameLength)
    {
        RtlCopyMemory((PUCHAR)Attr + NameOffset,
                      Name,
                      NameLength * sizeof(WCHAR));
    }

    *ValueOffset = Offset;

    return Attr;
}

PAttribute
FormatAddResident(_In_ PFormatContext Ctx,
                  _In_ ULONG Type,
                  _In_opt_ PCWSTR Name,
                  _In_ const void* Value,
                  _In_ ULONG ValueLength,
                  _In_ UCHAR IndexedFlag)
{
    ULONG ValueOffset;
    PAttribute Attr = FormatBeginAttribute(Ctx,
                                           Type,
                                           Name,
                                           ATTRIBUTE_RESIDENT_HEADER_SIZE,
                                           ValueLength,
                                           &ValueOffset);
    if (!Attr)
        return NULL;

    Attr->IsNonResident = 0;
    Attr->Resident.DataLength = ValueLength;
    Attr->Resident.DataOffset = (UINT16)ValueOffset;
    Attr->Resident.IndexedFlag = IndexedFlag;
    Attr->Resident.Padding = 0;

    if (Value && ValueLength)
        RtlCopyMemory((PUCHAR)Attr + ValueOffset, Value, ValueLength);

    Ctx->RecordOffset += Attr->Length;

    return Attr;
}

PAttribute
FormatAddNonResident(_In_ PFormatContext Ctx,
                     _In_ ULONG Type,
                     _In_opt_ PCWSTR Name,
                     _In_ ULONGLONG StartLcn,
                     _In_ ULONGLONG ClusterCount,
                     _In_ ULONGLONG DataSize,
                     _In_ ULONGLONG ValidDataSize,
                     _In_ USHORT AttributeFlags,
                     _In_ BOOLEAN Sparse)
{
    UCHAR Runs[32];
    ULONG RunsLength;
    ULONG ValueOffset;
    /*
     * The larger header carrying CompressedDataSize is only present when the
     * attribute is flagged compressed or sparse. A plain hole runlist, as used
     * by $BadClus:$Bad, keeps the ordinary header.
     */
    ULONG HeaderSize = (AttributeFlags & (ATTR_COMPRESSED | ATTR_SPARSE))
                           ? ATTRIBUTE_SPARSE_HEADER_SIZE
                           : ATTRIBUTE_NONRESIDENT_HEADER_SIZE;
    PAttribute Attr;

    RunsLength = FormatEncodeDataRuns(Runs,
                                      sizeof(Runs),
                                      StartLcn,
                                      ClusterCount,
                                      Sparse);
    if (RunsLength == 0)
        return NULL;

    Attr = FormatBeginAttribute(Ctx,
                                Type,
                                Name,
                                HeaderSize,
                                RunsLength,
                                &ValueOffset);
    if (!Attr)
        return NULL;

    Attr->IsNonResident = 1;
    Attr->Flags = AttributeFlags;
    Attr->NonResident.FirstVCN = 0;
    Attr->NonResident.LastVCN = ClusterCount ? ClusterCount - 1 : 0;
    Attr->NonResident.DataRunsOffset = (UINT16)ValueOffset;
    Attr->NonResident.CompressionUnitSize = 0;
    Attr->NonResident.Reserved = 0;
    Attr->NonResident.AllocatedSize = ClusterCount * Ctx->ClusterSize;
    Attr->NonResident.DataSize = DataSize;
    Attr->NonResident.InitalizedDataSize = ValidDataSize;

    /*
     * AllocatedSize covers the mapped VCN range even when the runs are holes;
     * CompressedDataSize is what reports the bytes actually on disk, and it
     * only exists in the larger header.
     */
    if (AttributeFlags & (ATTR_COMPRESSED | ATTR_SPARSE))
        Attr->NonResident.CompressedDataSize = Sparse ? 0 : Attr->NonResident.AllocatedSize;

    RtlCopyMemory((PUCHAR)Attr + ValueOffset, Runs, RunsLength);

    Ctx->RecordOffset += Attr->Length;

    return Attr;
}

/*
 * Stamps the update sequence number over the last two bytes of every sector,
 * stashing the displaced values in the update sequence array. Equivalent to
 * ntfslib's NtfsCommitFixup, reimplemented here so that the formatter needs
 * nothing from the environment-bound part of the library.
 */
void
FormatCommitFixupBuffer(_In_ PFormatContext Ctx,
                        _Inout_ PUCHAR Buffer,
                        _In_ ULONG Size)
{
    PNTFSRecordHeader Header = (PNTFSRecordHeader)Buffer;
    PUSHORT Usa = (PUSHORT)(Buffer + Header->UpdateSequenceOffset);
    PUSHORT SectorTail;
    ULONG Index;

    UNREFERENCED_PARAMETER(Size);

    /* A fresh record starts at update sequence number 1. */
    Usa[0] = 1;

    SectorTail = (PUSHORT)(Buffer + Ctx->BytesPerSector - sizeof(USHORT));

    for (Index = 1; Index < Header->SizeOfUpdateSequence; Index++)
    {
        Usa[Index] = *SectorTail;
        *SectorTail = Usa[0];
        SectorTail = (PUSHORT)((PUCHAR)SectorTail + Ctx->BytesPerSector);
    }
}

NTSTATUS
FormatEndRecord(_In_ PFormatContext Ctx)
{
    PFileRecordHeader Header = (PFileRecordHeader)Ctx->RecordBuffer;
    PULONG Marker;

    if (Ctx->RecordOffset + 8 > Ctx->MftRecordSize)
        return STATUS_INSUFFICIENT_RESOURCES;

    Marker = (PULONG)(Ctx->RecordBuffer + Ctx->RecordOffset);
    Marker[0] = ATTR_END;
    Marker[1] = 0;
    Ctx->RecordOffset += 8;

    Header->ActualSize = Ctx->RecordOffset;
    Header->NextAttributeID = Ctx->NextAttributeId;

    FormatCommitFixupBuffer(Ctx, Ctx->RecordBuffer, Ctx->MftRecordSize);

    return STATUS_SUCCESS;
}

void
FormatFillStandardInformation(_In_ PFormatContext Ctx,
                              _Out_ PStandardInformationEx Information,
                              _In_ ULONG FilePermissions)
{
    ULONGLONG Now = Ctx->CurrentTime;

    RtlZeroMemory(Information, sizeof(*Information));
    Information->CreationTime = Now;
    Information->LastWriteTime = Now;
    Information->ChangeTime = Now;
    Information->LastAccessTime = Now;
    Information->FilePermissions = FilePermissions;
}

ULONG
FormatFillFileName(_In_ PFormatContext Ctx,
                   _Out_ PUCHAR Buffer,
                   _In_ ULONGLONG ParentReference,
                   _In_ PCWSTR Name,
                   _In_ ULONG Flags,
                   _In_ UCHAR NameType,
                   _In_ ULONGLONG AllocatedSize,
                   _In_ ULONGLONG DataSize)
{
    PFileNameEx FileName = (PFileNameEx)Buffer;
    ULONG NameLength = FormatStringLength(Name);
    ULONG Length;

    if (NameLength == 0 || NameLength > NTFS_MAX_FILE_NAME_LENGTH)
        return 0;

    Length = FILE_NAME_HEADER_SIZE + NameLength * sizeof(WCHAR);
    RtlZeroMemory(Buffer, Length);

    FileName->ParentFileReference = ParentReference;
    FileName->CreationTime = Ctx->CurrentTime;
    FileName->LastWriteTime = Ctx->CurrentTime;
    FileName->ChangeTime = Ctx->CurrentTime;
    FileName->LastAccessTime = Ctx->CurrentTime;
    FileName->AllocatedSize = AllocatedSize;
    FileName->DataSize = DataSize;
    FileName->Flags = Flags;
    FileName->NameLength = (UINT8)NameLength;
    FileName->NameType = NameType;
    RtlCopyMemory(Buffer + FILE_NAME_HEADER_SIZE,
                  Name,
                  NameLength * sizeof(WCHAR));

    return Length;
}

NTSTATUS
FormatAddFileName(_In_ PFormatContext Ctx,
                  _In_ ULONGLONG ParentReference,
                  _In_ PCWSTR Name,
                  _In_ ULONG Flags,
                  _In_ UCHAR NameType,
                  _In_ ULONGLONG AllocatedSize,
                  _In_ ULONGLONG DataSize)
{
    UCHAR Buffer[FILE_NAME_HEADER_SIZE + 2 * NTFS_MAX_FILE_NAME_LENGTH];
    ULONG Length = FormatFillFileName(Ctx,
                                      Buffer,
                                      ParentReference,
                                      Name,
                                      Flags,
                                      NameType,
                                      AllocatedSize,
                                      DataSize);
    if (Length == 0)
        return STATUS_INVALID_PARAMETER;

    /* $FILE_NAME is always indexed and always resident. */
    if (!FormatAddResident(Ctx, TypeFileName, NULL, Buffer, Length, 1))
        return STATUS_INSUFFICIENT_RESOURCES;

    return STATUS_SUCCESS;
}

/*
 * Leaf index entry: the file reference, then the $FILE_NAME value that the
 * index is collated on.
 */
ULONG
FormatBuildFileNameIndexEntry(_In_ PFormatContext Ctx,
                              _Out_ PUCHAR Buffer,
                              _In_ ULONGLONG FileReference,
                              _In_ PCWSTR Name,
                              _In_ ULONG Flags,
                              _In_ ULONGLONG AllocatedSize,
                              _In_ ULONGLONG DataSize)
{
    PIndexEntry Entry = (PIndexEntry)Buffer;
    ULONG StreamLength;
    ULONG EntryLength;

    StreamLength = FormatFillFileName(Ctx,
                                      Buffer + INDEX_ENTRY_HEADER_SIZE,
                                      NTFS_ROOT_FILE_REFERENCE,
                                      Name,
                                      Flags,
                                      NAME_TYPE_WIN32_AND_DOS,
                                      AllocatedSize,
                                      DataSize);
    if (StreamLength == 0)
        return 0;

    EntryLength = ALIGN_UP_BY(INDEX_ENTRY_HEADER_SIZE + StreamLength, 8);

    /* Zero the alignment tail left between the stream and the next entry. */
    RtlZeroMemory(Buffer + INDEX_ENTRY_HEADER_SIZE + StreamLength,
                  EntryLength - INDEX_ENTRY_HEADER_SIZE - StreamLength);

    Entry->Data.Directory.IndexedFile = FileReference;
    Entry->EntryLength = (UINT16)EntryLength;
    Entry->StreamLength = (UINT16)StreamLength;
    Entry->Flags = 0;

    return EntryLength;
}

/*
 * Adds an $INDEX_ROOT holding nothing but the end-of-node entry. Because the
 * index fits in the record there is no $INDEX_ALLOCATION and no $BITMAP.
 */
NTSTATUS
FormatAddEmptyDirectoryIndex(_In_ PFormatContext Ctx,
                             _In_ ULONG IndexedAttributeType,
                             _In_ ULONG CollationRule,
                             _In_opt_ PCWSTR IndexName)
{
    UCHAR Buffer[sizeof(IndexRootEx) + INDEX_ENTRY_HEADER_SIZE];
    PIndexRootEx Root = (PIndexRootEx)Buffer;
    PIndexEntry End;
    ULONG Length = 0x20 + INDEX_ENTRY_HEADER_SIZE;

    RtlZeroMemory(Buffer, sizeof(Buffer));

    Root->AttributeType = IndexedAttributeType;
    Root->CollationRule = CollationRule;
    Root->BytesPerIndexRec = Ctx->IndexRecordSize;
    Root->ClusPerIndexRec = (UCHAR)FormatEncodeSizeInClusters(Ctx->IndexRecordSize,
                                                              Ctx->ClusterSize);

    /* Offsets in the node header are relative to the node header itself. */
    Root->Header.IndexOffset = sizeof(IndexNodeHeader);
    Root->Header.TotalIndexSize = sizeof(IndexNodeHeader) + INDEX_ENTRY_HEADER_SIZE;
    Root->Header.AllocatedSize = Root->Header.TotalIndexSize;
    Root->Header.Flags = 0;

    End = (PIndexEntry)(Buffer + 0x20);
    End->EntryLength = INDEX_ENTRY_HEADER_SIZE;
    End->StreamLength = 0;
    End->Flags = INDEX_ENTRY_END;

    if (!FormatAddResident(Ctx,
                           TypeIndexRoot,
                           IndexName,
                           Buffer,
                           Length,
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

/*
 * $INDEX_ROOT for a directory too large to keep its entries resident. The root
 * holds one end entry flagged as a node, whose trailing VCN points at the
 * index block in $INDEX_ALLOCATION.
 */
NTSTATUS
FormatAddIndexRootNode(_In_ PFormatContext Ctx,
                       _In_ ULONG IndexedAttributeType,
                       _In_ ULONG CollationRule,
                       _In_ PCWSTR IndexName)
{
    /* Node entries carry an 8 byte child VCN after the entry header. */
    const ULONG NodeEntryLength = INDEX_ENTRY_HEADER_SIZE + sizeof(ULONGLONG);

    UCHAR Buffer[0x20 + INDEX_ENTRY_HEADER_SIZE + sizeof(ULONGLONG)];
    PIndexRootEx Root = (PIndexRootEx)Buffer;
    PIndexEntry End;
    PULONGLONG ChildVcn;

    RtlZeroMemory(Buffer, sizeof(Buffer));

    Root->AttributeType = IndexedAttributeType;
    Root->CollationRule = CollationRule;
    Root->BytesPerIndexRec = Ctx->IndexRecordSize;
    Root->ClusPerIndexRec = (UCHAR)FormatEncodeSizeInClusters(Ctx->IndexRecordSize,
                                                              Ctx->ClusterSize);

    Root->Header.IndexOffset = sizeof(IndexNodeHeader);
    Root->Header.TotalIndexSize = sizeof(IndexNodeHeader) + NodeEntryLength;
    Root->Header.AllocatedSize = Root->Header.TotalIndexSize;
    /* Tells readers that $INDEX_ALLOCATION is present. */
    Root->Header.Flags = 1;

    End = (PIndexEntry)(Buffer + 0x20);
    End->EntryLength = (UINT16)NodeEntryLength;
    End->StreamLength = 0;
    End->Flags = INDEX_ENTRY_NODE | INDEX_ENTRY_END;

    ChildVcn = (PULONGLONG)(Buffer + 0x20 + INDEX_ENTRY_HEADER_SIZE);
    *ChildVcn = 0;

    if (!FormatAddResident(Ctx,
                           TypeIndexRoot,
                           IndexName,
                           Buffer,
                           0x20 + NodeEntryLength,
                           0))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}
