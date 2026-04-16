/*
 * PROJECT:     ReactOS NTFS driver (ntfslx)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Directory B-tree index insert/remove for $I30
 *
 * Inserts into the resident $INDEX_ROOT, and spills to $INDEX_ALLOCATION
 * when the MFT record runs out of room.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

#define NTFSLX_ATTRIBUTE_BITMAP             0x000000B0UL
#define NTFSLX_INDEX_ROOT_FLAG_LARGE        0x00000001UL
#define NTFSLX_INDEX_MP_ENCODE_MAX          512
#define NTFSLX_INDEXMOD_MAX_ENTRIES_PER_BLOCK 512

#ifndef NTFS_BLOCK_SIZE
#define NTFS_BLOCK_SIZE 512
#endif

/*
 * On-disk index entry for the $I30 filename index.
 * The FILE_NAME attribute key follows immediately after this header.
 */
#include <pshpack1.h>
typedef struct _NTFSLX_I30_INDEX_ENTRY
{
    ULONGLONG IndexedFile;   /* MFT reference */
    USHORT Length;            /* total entry length */
    USHORT KeyLength;         /* FILE_NAME attribute key length */
    USHORT Flags;             /* NTFSLX_INDEX_ENTRY_NODE | NTFSLX_INDEX_ENTRY_END */
    USHORT Reserved;
    /* NTFSLX_FILE_NAME_ATTRIBUTE + WCHAR[] name follows at offset 16 */
} NTFSLX_I30_INDEX_ENTRY, *PNTFSLX_I30_INDEX_ENTRY;

typedef struct _NTFSLX_INDX_BLOCK_HEADER
{
    ULONG     Magic;       /* 'INDX' = 0x58444E49 */
    USHORT    UsaOffset;
    USHORT    UsaCount;
    ULONGLONG Lsn;
    ULONGLONG IndexBlockVcn;
    /* NTFSLX_INDEX_HEADER Index follows at offset 24 (canonical NTFS layout
     * expected by NtfslxValidateIndexBufferHeader / NtfslxWalkIndexEntryRange). */
} NTFSLX_INDX_BLOCK_HEADER, *PNTFSLX_INDX_BLOCK_HEADER;
#include <poppack.h>

/* The INDEX_HEADER always lives immediately after the INDX block header
 * (Magic + UsaOffset + UsaCount + Lsn + Vcn = 24 bytes). This matches the
 * layout that NtfslxValidateIndexBufferHeader expects. */
#define NTFSLX_INDX_BLOCK_INDEX_HEADER_OFFSET  24

static VOID
NtfslxInitIndexBlock(
    _Out_writes_bytes_(BlockSize) PVOID BlockBuffer,
    _In_ ULONG BlockSize,
    _In_ ULONGLONG Vcn);

static PNTFSLX_INDEX_HEADER
NtfslxIndxBlockHeader(
    _In_ PVOID BlockBuffer);

static NTSTATUS
NtfslxReadDirIndexBlock(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG Vcn,
    _In_ ULONG BlockSize,
    _Out_writes_bytes_(BlockSize) PVOID BlockBuffer);

static NTSTATUS
NtfslxWriteDirIndexBlock(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG Vcn,
    _In_ ULONG BlockSize,
    _In_reads_bytes_(BlockSize) PVOID BlockBuffer);

static NTSTATUS
NtfslxInsertEntryIntoIndexBody(
    _Inout_ PNTFSLX_INDEX_HEADER IndexHeader,
    _In_ ULONG AllocatedBytes,
    _In_reads_bytes_(NewEntryLength) const VOID *NewEntry,
    _In_ ULONG NewEntryLength,
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt);

/*
 * Build a mapping-pairs byte stream for a single run { Lcn, Length }.
 * Caller passes a buffer big enough (16 bytes is plenty for a single run).
 * Returns the number of bytes written (including the 0 terminator).
 */
static ULONG
NtfslxBuildSingleRunMappingPairs(
    _In_ LONGLONG Lcn,
    _In_ LONGLONG Length,
    _Out_writes_bytes_(BufferSize) PUCHAR Buffer,
    _In_ ULONG BufferSize)
{
    LONG LengthBytes = 1;
    LONG OffsetBytes = 1;
    LONG J;
    ULONG Idx = 0;

    {
        ULONGLONG V = (ULONGLONG)Length;
        while (V >= 0x80) { V >>= 8; LengthBytes++; }
    }
    {
        LONGLONG V = Lcn < 0 ? ~Lcn : Lcn;
        while (V >= 0x80) { V >>= 8; OffsetBytes++; }
    }

    if ((ULONG)(1 + LengthBytes + OffsetBytes + 1) > BufferSize)
    {
        return 0;
    }

    RtlZeroMemory(Buffer, BufferSize);
    Buffer[Idx++] = (UCHAR)((OffsetBytes << 4) | LengthBytes);
    for (J = 0; J < LengthBytes; J++)
        Buffer[Idx++] = (UCHAR)((Length >> (J * 8)) & 0xFF);
    for (J = 0; J < OffsetBytes; J++)
        Buffer[Idx++] = (UCHAR)((Lcn >> (J * 8)) & 0xFF);
    Idx++; /* terminator zero (already) */
    return Idx;
}

static NTSTATUS
NtfslxEncodeIndexMappingPairs(
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _Out_writes_bytes_to_(MaxSize, *OutSize) PUCHAR OutBuffer,
    _In_ ULONG MaxSize,
    _Out_ PULONG OutSize)
{
    ULONG Offset;
    LONGLONG PrevLcn;
    ULONG I;

    *OutSize = 0;
    Offset = 0;
    PrevLcn = 0;

    for (I = 0; Runlist[I].Length != 0; I++)
    {
        LONGLONG Length = Runlist[I].Length;
        LONGLONG Lcn = Runlist[I].Lcn;
        LONGLONG LcnDelta = 0;
        UCHAR LenBytes;
        UCHAR OfsBytes;
        ULONG J;
        UCHAR Header;

        if (Length <= 0)
        {
            return STATUS_INVALID_PARAMETER;
        }

        LenBytes = 1;
        {
            ULONGLONG V = (ULONGLONG)Length;
            while (V > 0x7F)
            {
                V >>= 8;
                LenBytes++;
            }
        }

        if (Lcn == NTFSLX_LCN_HOLE)
        {
            OfsBytes = 0;
        }
        else if (Lcn >= 0)
        {
            ULONGLONG AbsV;

            LcnDelta = Lcn - PrevLcn;
            OfsBytes = 1;
            AbsV = (ULONGLONG)((LcnDelta < 0) ? ~LcnDelta : LcnDelta);
            while (AbsV > 0x7F)
            {
                AbsV >>= 8;
                OfsBytes++;
            }
        }
        else
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (Offset + 1 + LenBytes + OfsBytes + 1 > MaxSize)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        Header = (UCHAR)((OfsBytes << 4) | LenBytes);
        OutBuffer[Offset++] = Header;

        for (J = 0; J < LenBytes; J++)
        {
            OutBuffer[Offset++] = (UCHAR)(((ULONGLONG)Length >> (J * 8)) & 0xFF);
        }

        for (J = 0; J < OfsBytes; J++)
        {
            OutBuffer[Offset++] = (UCHAR)(((ULONGLONG)LcnDelta >> (J * 8)) & 0xFF);
        }

        if (Lcn != NTFSLX_LCN_HOLE)
        {
            PrevLcn = Lcn;
        }
    }

    if (Offset >= MaxSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    OutBuffer[Offset++] = 0;
    *OutSize = Offset;
    return STATUS_SUCCESS;
}

static LONG
NtfslxCompareIndexEntryNames(
    _In_reads_bytes_(LeftLength) const VOID *LeftEntry,
    _In_ ULONG LeftLength,
    _In_reads_bytes_(RightLength) const VOID *RightEntry,
    _In_ ULONG RightLength,
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    const NTFSLX_I30_INDEX_ENTRY *Left;
    const NTFSLX_I30_INDEX_ENTRY *Right;
    const NTFSLX_FILE_NAME_ATTRIBUTE *LeftFn;
    const NTFSLX_FILE_NAME_ATTRIBUTE *RightFn;
    const WCHAR *LeftName;
    const WCHAR *RightName;

    UNREFERENCED_PARAMETER(LeftLength);
    UNREFERENCED_PARAMETER(RightLength);

    Left = (const NTFSLX_I30_INDEX_ENTRY *)LeftEntry;
    Right = (const NTFSLX_I30_INDEX_ENTRY *)RightEntry;
    LeftFn = (const NTFSLX_FILE_NAME_ATTRIBUTE *)
        ((const UCHAR *)Left + sizeof(*Left));
    RightFn = (const NTFSLX_FILE_NAME_ATTRIBUTE *)
        ((const UCHAR *)Right + sizeof(*Right));
    LeftName = (const WCHAR *)((const UCHAR *)LeftFn + sizeof(*LeftFn));
    RightName = (const WCHAR *)((const UCHAR *)RightFn + sizeof(*RightFn));

    return NtfslxCollateNames((const USHORT *)LeftName,
                              LeftFn->FileNameLength,
                              (const USHORT *)RightName,
                              RightFn->FileNameLength,
                              1,
                              TRUE,
                              DevExt->UpcaseTable,
                              DevExt->UpcaseTableLength);
}

static NTSTATUS
NtfslxAppendRightLeafIndexBlock(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _Inout_ PNTFSLX_MFT_RECORD DirRecord,
    _In_reads_bytes_(NewEntryLength) const VOID *NewEntry,
    _In_ ULONG NewEntryLength)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_ATTR_RECORD IndexAllocAttr;
    PNTFSLX_ATTR_RECORD IndexRootAttr;
    PNTFSLX_ATTR_RECORD BitmapAttr;
    PNTFSLX_INDEX_ROOT IndexRoot;
    PNTFSLX_RUNLIST_ELEMENT OldRunlist = NULL;
    PNTFSLX_RUNLIST_ELEMENT NewRunlist = NULL;
    PNTFSLX_RUNLIST_ELEMENT MergedRunlist = NULL;
    ULONG OldRunlistCount = 0;
    ULONG NewRunlistCount = 0;
    ULONG MergedRunlistCount = 0;
    ULONG MergedCapacity = 0;
    PUCHAR RightBlock = NULL;
    PUCHAR NewBlock = NULL;
    PNTFSLX_INDEX_HEADER RightHeader;
    PNTFSLX_INDEX_HEADER NewHeader;
    PUCHAR RightBase;
    PUCHAR NewBase;
    ULONG EntryOffsets[NTFSLX_INDEXMOD_MAX_ENTRIES_PER_BLOCK];
    ULONG EntryLengths[NTFSLX_INDEXMOD_MAX_ENTRIES_PER_BLOCK];
    ULONG EntryCount = 0;
    ULONG EntryOffset;
    ULONG EndOffset = 0;
    ULONG InsertOffset;
    ULONG MoveStart;
    ULONG MovedBytes;
    ULONG FreeBytes;
    ULONG NeedExtraBytes;
    ULONG MoveIndex;
    ULONG InsertIndex;
    ULONG BlockSize;
    ULONG ClustersPerBlock;
    ULONG ClusterSize;
    ULONG OldAttrLength;
    ULONG NewAttrLength;
    ULONG MpOffset;
    ULONG MpSize;
    ULONG NameBytes;
    USHORT NameOffset;
    ULONG NeededBitmapBytes;
    ULONG BitmapValueLength;
    ULONG OldTotalClusters;
    ULONG NewTotalClusters;
    ULONG NewBlockIndex;
    ULONG I;
    ULONGLONG NumBlocks;
    ULONGLONG RightmostVcn;
    ULONGLONG NewBlockVcn;
    UCHAR MpBuffer[NTFSLX_INDEX_MP_ENCODE_MAX];
    NTSTATUS Status;
    const CHAR *Stage = "start";

    Stage = "find-root";
    Status = NtfslxFindAttribute(DirRecord,
                                 NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 IndexName,
                                 RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexRootAttr);
    if (!NT_SUCCESS(Status) || IndexRootAttr->NonResident != 0)
    {
        return NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
    }

    IndexRoot = (PNTFSLX_INDEX_ROOT)((PUCHAR)IndexRootAttr +
                                      IndexRootAttr->Data.Resident.ValueOffset);
    BlockSize = IndexRoot->IndexBlockSize;
    if (BlockSize == 0)
    {
        BlockSize = DevExt->VolumeInfo.BytesPerIndexRecord;
    }
    ClusterSize = DevExt->VolumeInfo.BytesPerCluster;
    NameBytes = (ULONG)(RTL_NUMBER_OF(IndexName) - 1) * sizeof(WCHAR);
    ClustersPerBlock = BlockSize / ClusterSize;
    if (ClustersPerBlock == 0)
    {
        ClustersPerBlock = 1;
    }

    Stage = "find-indexalloc";
    Status = NtfslxFindAttribute(DirRecord,
                                 NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                 IndexName,
                                 RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexAllocAttr);
    if (!NT_SUCCESS(Status) || IndexAllocAttr->NonResident == 0)
    {
        return NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
    }

    Stage = "build-runlist";
    Status = NtfslxBuildIndexAllocationRunlist(IndexAllocAttr,
                                               &OldRunlist,
                                               &OldRunlistCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    OldTotalClusters = (ULONG)(IndexAllocAttr->Data.NonResident.AllocatedSize / ClusterSize);
    NumBlocks = OldTotalClusters / ClustersPerBlock;
    if (NumBlocks == 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    RightmostVcn = (NumBlocks - 1) * ClustersPerBlock;
    RightBlock = ExAllocatePoolWithTag(NonPagedPool, BlockSize, NTFSLX_TAG);
    NewBlock = ExAllocatePoolWithTag(NonPagedPool, BlockSize, NTFSLX_TAG);
    if (RightBlock == NULL || NewBlock == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Stage = "read-right-block";
    Status = NtfslxReadDirIndexBlock(DevExt, OldRunlist, RightmostVcn, BlockSize, RightBlock);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    RightHeader = NtfslxIndxBlockHeader(RightBlock);
    RightBase = (PUCHAR)RightHeader;
    EntryOffset = RightHeader->EntriesOffset;
    InsertOffset = 0;
    InsertIndex = 0;
    while (EntryOffset + sizeof(NTFSLX_I30_INDEX_ENTRY) <= RightHeader->IndexLength)
    {
        PNTFSLX_I30_INDEX_ENTRY Entry = (PNTFSLX_I30_INDEX_ENTRY)(RightBase + EntryOffset);

        if (Entry->Length == 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }
        if (Entry->Flags & NTFSLX_INDEX_ENTRY_END)
        {
            EndOffset = EntryOffset;
            break;
        }
        if (EntryCount >= RTL_NUMBER_OF(EntryOffsets))
        {
            Status = STATUS_BUFFER_OVERFLOW;
            goto Cleanup;
        }

        EntryOffsets[EntryCount] = EntryOffset;
        EntryLengths[EntryCount] = Entry->Length;
        EntryCount++;
        EntryOffset += Entry->Length;
    }

    if (EntryCount == 0 || EndOffset == 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    InsertOffset = EndOffset;
    InsertIndex = EntryCount;
    for (I = 0; I < EntryCount; I++)
    {
        LONG CompareEntry = NtfslxCompareIndexEntryNames(NewEntry,
                                                         NewEntryLength,
                                                         RightBase + EntryOffsets[I],
                                                         EntryLengths[I],
                                                         DevExt);
        if (CompareEntry == 0)
        {
            Status = STATUS_OBJECT_NAME_COLLISION;
            goto Cleanup;
        }
        if (CompareEntry < 0)
        {
            InsertOffset = EntryOffsets[I];
            InsertIndex = I;
            break;
        }
    }

    FreeBytes = RightHeader->AllocatedSize - RightHeader->IndexLength;
    NeedExtraBytes = (FreeBytes >= sizeof(ULONGLONG))
        ? 0
        : (ULONG)(sizeof(ULONGLONG) - FreeBytes);
    MoveStart = InsertOffset;
    MovedBytes = EndOffset - MoveStart;
    MoveIndex = InsertIndex;
    NTFSDBG("ntfslx: append-right rightmostVcn=%I64u free=%lu needExtra=%lu entries=%lu insertIndex=%lu moved=%lu\n",
             RightmostVcn,
             FreeBytes,
             NeedExtraBytes,
             EntryCount,
             InsertIndex,
             MovedBytes);

    while (MovedBytes < NeedExtraBytes)
    {
        if (MoveIndex == 0)
        {
            Status = STATUS_DISK_FULL;
            goto Cleanup;
        }

        MoveIndex--;
        MoveStart = EntryOffsets[MoveIndex];
        MovedBytes = EndOffset - MoveStart;
    }

    Stage = "allocate-clusters";
    Status = NtfslxAllocateClusters(DevExt->StorageDevice,
                                    &DevExt->VolumeInfo,
                                    DevExt->MftRunlist,
                                    ClustersPerBlock,
                                    0,
                                    &NewRunlist,
                                    &NewRunlistCount);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    NewBlockVcn = OldTotalClusters;
    NewTotalClusters = OldTotalClusters + ClustersPerBlock;
    MergedCapacity = OldRunlistCount + NewRunlistCount + 2;
    MergedRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                          MergedCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                          NTFSLX_TAG);
    if (MergedRunlist == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlZeroMemory(MergedRunlist, MergedCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT));

    for (I = 0; OldRunlist[I].Length != 0; I++)
    {
        MergedRunlist[MergedRunlistCount++] = OldRunlist[I];
    }

    for (I = 0; NewRunlist[I].Length != 0; I++)
    {
        NTFSLX_RUNLIST_ELEMENT Shifted;

        Shifted = NewRunlist[I];
        Shifted.Vcn += (LONGLONG)OldTotalClusters;
        if (MergedRunlistCount != 0 &&
            MergedRunlist[MergedRunlistCount - 1].Lcn >= 0 &&
            Shifted.Lcn >= 0 &&
            MergedRunlist[MergedRunlistCount - 1].Vcn +
                MergedRunlist[MergedRunlistCount - 1].Length == Shifted.Vcn &&
            MergedRunlist[MergedRunlistCount - 1].Lcn +
                MergedRunlist[MergedRunlistCount - 1].Length == Shifted.Lcn)
        {
            MergedRunlist[MergedRunlistCount - 1].Length += Shifted.Length;
        }
        else
        {
            MergedRunlist[MergedRunlistCount++] = Shifted;
        }
    }

    MergedRunlist[MergedRunlistCount].Vcn = (LONGLONG)NewTotalClusters;
    MergedRunlist[MergedRunlistCount].Lcn = NTFSLX_LCN_ENOENT;
    MergedRunlist[MergedRunlistCount].Length = 0;
    MergedRunlistCount++;

    Stage = "encode-mapping-pairs";
    Status = NtfslxEncodeIndexMappingPairs(MergedRunlist,
                                           MpBuffer,
                                           sizeof(MpBuffer),
                                           &MpSize);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    OldAttrLength = IndexAllocAttr->Length;
    NameOffset = IndexAllocAttr->NameOffset;
    if (NameOffset < sizeof(NTFSLX_ATTR_RECORD))
    {
        NameOffset = sizeof(NTFSLX_ATTR_RECORD);
    }
    MpOffset = IndexAllocAttr->Data.NonResident.MappingPairsOffset;
    if (MpOffset < ROUND_UP(NameOffset + NameBytes, 8))
    {
        MpOffset = ROUND_UP(NameOffset + NameBytes, 8);
    }
    NewAttrLength = ROUND_UP(MpOffset + MpSize, 8);
    if (NewAttrLength != OldAttrLength)
    {
        Stage = "resize-indexalloc";
        Status = NtfslxResizeAttributeRecord(DirRecord, IndexAllocAttr, NewAttrLength);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
    }

    IndexAllocAttr->NameLength = (UCHAR)(RTL_NUMBER_OF(IndexName) - 1);
    IndexAllocAttr->NameOffset = NameOffset;
    RtlCopyMemory((PUCHAR)IndexAllocAttr + NameOffset, IndexName, NameBytes);
    RtlZeroMemory((PUCHAR)IndexAllocAttr + MpOffset, NewAttrLength - MpOffset);
    RtlCopyMemory((PUCHAR)IndexAllocAttr + MpOffset, MpBuffer, MpSize);
    IndexAllocAttr->Data.NonResident.LowestVcn = 0;
    IndexAllocAttr->Data.NonResident.HighestVcn = NewTotalClusters - 1;
    IndexAllocAttr->Data.NonResident.MappingPairsOffset = (USHORT)MpOffset;
    IndexAllocAttr->Data.NonResident.AllocatedSize = (ULONGLONG)NewTotalClusters * ClusterSize;
    IndexAllocAttr->Data.NonResident.DataSize = (ULONGLONG)NewTotalClusters * ClusterSize;
    IndexAllocAttr->Data.NonResident.InitializedSize = (ULONGLONG)NewTotalClusters * ClusterSize;
    IndexAllocAttr->Data.NonResident.CompressedSize = 0;

    Stage = "find-bitmap";
    Status = NtfslxFindAttribute(DirRecord,
                                 NTFSLX_ATTRIBUTE_BITMAP,
                                 IndexName,
                                 RTL_NUMBER_OF(IndexName) - 1,
                                 &BitmapAttr);
    if (!NT_SUCCESS(Status) || BitmapAttr->NonResident != 0)
    {
        Status = NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
        goto Cleanup;
    }

    NewBlockIndex = (ULONG)(NewBlockVcn / ClustersPerBlock);
    NeededBitmapBytes = (NewBlockIndex / 8) + 1;
    BitmapValueLength = BitmapAttr->Data.Resident.ValueLength;
    if (BitmapValueLength < NeededBitmapBytes)
    {
        ULONG NewBitmapAttrLength;

        NewBitmapAttrLength = ROUND_UP(BitmapAttr->Data.Resident.ValueOffset + NeededBitmapBytes, 8);
        Stage = "resize-bitmap";
        Status = NtfslxResizeAttributeRecord(DirRecord, BitmapAttr, NewBitmapAttrLength);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        Status = NtfslxFindAttribute(DirRecord,
                                     NTFSLX_ATTRIBUTE_BITMAP,
                                     IndexName,
                                     RTL_NUMBER_OF(IndexName) - 1,
                                     &BitmapAttr);
        if (!NT_SUCCESS(Status) || BitmapAttr->NonResident != 0)
        {
            Status = NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
            goto Cleanup;
        }

        RtlZeroMemory((PUCHAR)BitmapAttr + BitmapAttr->Data.Resident.ValueOffset + BitmapValueLength,
                      NeededBitmapBytes - BitmapValueLength);
        BitmapAttr->Data.Resident.ValueLength = NeededBitmapBytes;
        BitmapValueLength = NeededBitmapBytes;
    }

    ((PUCHAR)BitmapAttr + BitmapAttr->Data.Resident.ValueOffset)[NewBlockIndex / 8] |=
        (UCHAR)(1u << (NewBlockIndex & 7));

    NtfslxInitIndexBlock(NewBlock, BlockSize, NewBlockVcn);
    NewHeader = NtfslxIndxBlockHeader(NewBlock);
    NewBase = (PUCHAR)NewHeader;
    if (MovedBytes != 0)
    {
        RtlCopyMemory(NewBase + NewHeader->EntriesOffset,
                      RightBase + MoveStart,
                      MovedBytes);
        NewHeader->IndexLength = NewHeader->EntriesOffset + MovedBytes;
    }

    {
        PNTFSLX_I30_INDEX_ENTRY NewEnd =
            (PNTFSLX_I30_INDEX_ENTRY)(NewBase + NewHeader->IndexLength);
        NewEnd->IndexedFile = 0;
        NewEnd->Length = sizeof(NTFSLX_I30_INDEX_ENTRY);
        NewEnd->KeyLength = 0;
        NewEnd->Flags = NTFSLX_INDEX_ENTRY_END;
        NewEnd->Reserved = 0;
        NewHeader->IndexLength += sizeof(NTFSLX_I30_INDEX_ENTRY);
    }

    Stage = "insert-new-block";
    Status = NtfslxInsertEntryIntoIndexBody(NewHeader,
                                            NewHeader->AllocatedSize,
                                            NewEntry,
                                            NewEntryLength,
                                            DevExt);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    {
        PNTFSLX_I30_INDEX_ENTRY RightEnd =
            (PNTFSLX_I30_INDEX_ENTRY)(RightBase + MoveStart);
        RightEnd->IndexedFile = 0;
        RightEnd->Length = (USHORT)(sizeof(NTFSLX_I30_INDEX_ENTRY) + sizeof(ULONGLONG));
        RightEnd->KeyLength = 0;
        RightEnd->Flags = NTFSLX_INDEX_ENTRY_END | NTFSLX_INDEX_ENTRY_NODE;
        RightEnd->Reserved = 0;
        *(PULONGLONG)((PUCHAR)RightEnd + sizeof(NTFSLX_I30_INDEX_ENTRY)) = NewBlockVcn;
        RightHeader->Flags = NTFSLX_INDEX_ROOT_FLAG_LARGE;
        RightHeader->IndexLength = MoveStart + RightEnd->Length;
    }

    Stage = "write-new-block";
    Status = NtfslxWriteDirIndexBlock(DevExt, MergedRunlist, NewBlockVcn, BlockSize, NewBlock);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    Stage = "write-old-block";
    Status = NtfslxWriteDirIndexBlock(DevExt, MergedRunlist, RightmostVcn, BlockSize, RightBlock);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    NTFSDBG("ntfslx: AllocationInsert: extended index dir block=%I64u newBlock=%I64u moved=%lu\n",
             RightmostVcn,
             NewBlockVcn,
             MovedBytes);
    Status = STATUS_SUCCESS;

Cleanup:
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: append-right failed stage=%s status=0x%08lx rightmostVcn=%I64u moved=%lu needExtra=%lu\n",
                 Stage,
                 Status,
                 RightmostVcn,
                 MovedBytes,
                 NeedExtraBytes);
    }

    if (!NT_SUCCESS(Status) && NewRunlist != NULL)
    {
        NtfslxFreeClusters(DevExt->StorageDevice,
                           &DevExt->VolumeInfo,
                           DevExt->MftRunlist,
                           NewRunlist,
                           NewRunlistCount);
    }
    if (RightBlock != NULL)
    {
        ExFreePoolWithTag(RightBlock, NTFSLX_TAG);
    }
    if (NewBlock != NULL)
    {
        ExFreePoolWithTag(NewBlock, NTFSLX_TAG);
    }
    if (MergedRunlist != NULL)
    {
        ExFreePoolWithTag(MergedRunlist, NTFSLX_TAG);
    }
    if (NewRunlist != NULL)
    {
        ExFreePoolWithTag(NewRunlist, NTFSLX_TAG);
    }
    if (OldRunlist != NULL)
    {
        ExFreePoolWithTag(OldRunlist, 'aIxN');
    }

    return Status;
}

/*
 * Initialize a fresh INDX block using the canonical NTFS layout:
 *
 *   0  ..  7   NTFSLX_RECORD_HEADER  (Magic / UsaOffset / UsaCount)
 *   8  .. 15   Lsn
 *  16  .. 23   IndexBlockVcn
 *  24  .. 39   NTFSLX_INDEX_HEADER   (Entries/Index/Allocated/Flags)
 *  40  .. 57   Update sequence array (9 USHORTs for a 4 KB block)
 *  58+         entries, aligned
 *
 * FirstEntryOffset in the INDEX_HEADER is relative to the HEADER's start
 * (offset 24 in the block) and must be >= sizeof(NTFSLX_INDEX_HEADER) and
 * skip over any bytes used by the USA that live inside the index area.
 * AllocatedSize must equal BlockSize - 24 to satisfy
 * NtfslxValidateIndexBufferHeader.
 */
static VOID
NtfslxInitIndexBlock(
    _Out_writes_bytes_(BlockSize) PVOID BlockBuffer,
    _In_ ULONG BlockSize,
    _In_ ULONGLONG Vcn)
{
    PNTFSLX_INDX_BLOCK_HEADER Header;
    PNTFSLX_INDEX_HEADER IndexHeader;
    PNTFSLX_I30_INDEX_ENTRY EndEntry;
    USHORT UsaCount;
    USHORT UsaOffset;
    ULONG EntriesOffset;

    RtlZeroMemory(BlockBuffer, BlockSize);

    Header = (PNTFSLX_INDX_BLOCK_HEADER)BlockBuffer;
    Header->Magic = NTFSLX_RECORD_MAGIC_INDX;
    UsaCount = (USHORT)((BlockSize / NTFS_BLOCK_SIZE) + 1);

    /*
     * Place the USA right after the NTFSLX_INDEX_HEADER (offset 40) so it
     * does not collide with the index-header fields that live at offsets
     * 24..39. Then EntriesOffset (relative to the NTFSLX_INDEX_HEADER start
     * at block offset 24) must point past the USA, i.e. past the first
     * (16 + UsaCount * 2) bytes of the header area.
     */
    UsaOffset = (USHORT)(NTFSLX_INDX_BLOCK_INDEX_HEADER_OFFSET +
                         sizeof(NTFSLX_INDEX_HEADER));
    Header->UsaOffset = UsaOffset;
    Header->UsaCount = UsaCount;
    Header->Lsn = 0;
    Header->IndexBlockVcn = Vcn;

    IndexHeader = (PNTFSLX_INDEX_HEADER)(
        (PUCHAR)BlockBuffer + NTFSLX_INDX_BLOCK_INDEX_HEADER_OFFSET);

    EntriesOffset = ROUND_UP(sizeof(NTFSLX_INDEX_HEADER) +
                             (ULONG)UsaCount * sizeof(USHORT), 8);

    IndexHeader->EntriesOffset = EntriesOffset;
    IndexHeader->AllocatedSize = BlockSize - NTFSLX_INDX_BLOCK_INDEX_HEADER_OFFSET;
    IndexHeader->Flags = 0;

    /* Terminal END entry immediately at EntriesOffset. */
    EndEntry = (PNTFSLX_I30_INDEX_ENTRY)((PUCHAR)IndexHeader + EntriesOffset);
    EndEntry->IndexedFile = 0;
    EndEntry->Length = sizeof(NTFSLX_I30_INDEX_ENTRY);
    EndEntry->KeyLength = 0;
    EndEntry->Flags = NTFSLX_INDEX_ENTRY_END;
    EndEntry->Reserved = 0;

    IndexHeader->IndexLength = EntriesOffset + sizeof(NTFSLX_I30_INDEX_ENTRY);
}

/*
 * Return a pointer to the NTFSLX_INDEX_HEADER inside an INDX block. The
 * header is always at the canonical offset 24 regardless of UsaCount.
 */
static PNTFSLX_INDEX_HEADER
NtfslxIndxBlockHeader(
    _In_ PVOID BlockBuffer)
{
    return (PNTFSLX_INDEX_HEADER)(
        (PUCHAR)BlockBuffer + NTFSLX_INDX_BLOCK_INDEX_HEADER_OFFSET);
}

/*
 * Read a single INDX block into caller-provided buffer (sized to BlockSize).
 * Validates the header and applies the MST fixup.
 */
static NTSTATUS
NtfslxReadDirIndexBlock(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG Vcn,
    _In_ ULONG BlockSize,
    _Out_writes_bytes_(BlockSize) PVOID BlockBuffer)
{
    NTSTATUS Status;

    Status = NtfslxReadIndexAllocationBlock(DevExt->StorageDevice,
                                            &DevExt->VolumeInfo,
                                            Runlist,
                                            Vcn,
                                            BlockBuffer);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: ReadDirIndexBlock vcn=%I64u failed 0x%08lx\n", Vcn, Status);
        return Status;
    }

    Status = NtfslxValidateIndexAllocationBlock(BlockBuffer,
                                                BlockSize,
                                                DevExt->VolumeInfo.BytesPerSector,
                                                Vcn);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: ValidateIndexAllocationBlock vcn=%I64u failed 0x%08lx\n",
                 Vcn, Status);
    }
    return Status;
}

/*
 * Write a single INDX block. A copy is made, MST fixup applied, the copy is
 * written to disk, and the caller's buffer is left untouched.
 */
static NTSTATUS
NtfslxWriteDirIndexBlock(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG Vcn,
    _In_ ULONG BlockSize,
    _In_reads_bytes_(BlockSize) PVOID BlockBuffer)
{
    PNTFSLX_INDX_BLOCK_HEADER Copy;
    LONGLONG Lcn;
    NTSTATUS Status;
    ULONG ClusterSize;
    ULONG BlockClusters;
    ULONG I;
    PUCHAR Src;

    ClusterSize = DevExt->VolumeInfo.BytesPerCluster;
    BlockClusters = BlockSize / ClusterSize;
    if (BlockClusters == 0)
        BlockClusters = 1;

    Copy = ExAllocatePoolWithTag(NonPagedPool, BlockSize, NTFSLX_TAG);
    if (Copy == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlCopyMemory(Copy, BlockBuffer, BlockSize);

    Status = NtfslxPreWriteMstFixup((PNTFSLX_RECORD_HEADER)Copy, BlockSize);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: WriteDirIndexBlock: MST fixup failed 0x%08lx\n", Status);
        ExFreePoolWithTag(Copy, NTFSLX_TAG);
        return Status;
    }

    /* Walk clusters — typically a single cluster (index block = 1 cluster on
     * small volumes), but we handle multi-cluster blocks anyway. */
    Src = (PUCHAR)Copy;
    for (I = 0; I < BlockClusters; I++)
    {
        LONGLONG BlockVcn = (LONGLONG)(Vcn + I);
        PNTFSLX_RUNLIST_ELEMENT Run;

        Lcn = -1;
        for (Run = Runlist; Run->Length != 0; Run++)
        {
            if (BlockVcn >= Run->Vcn && BlockVcn < Run->Vcn + Run->Length)
            {
                if (Run->Lcn < 0)
                    break;
                Lcn = Run->Lcn + (BlockVcn - Run->Vcn);
                break;
            }
        }
        if (Lcn < 0)
        {
            NTFSDBG("ntfslx: WriteDirIndexBlock: no mapping for vcn=%I64d\n",
                     BlockVcn);
            ExFreePoolWithTag(Copy, NTFSLX_TAG);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Status = NtfslxWriteDisk(DevExt->StorageDevice,
                                 Lcn * ClusterSize,
                                 ClusterSize,
                                 DevExt->VolumeInfo.BytesPerSector,
                                 Src,
                                 TRUE);
        if (!NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: WriteDirIndexBlock: WriteDisk vcn=%I64d lcn=%I64d failed 0x%08lx\n",
                     BlockVcn, Lcn, Status);
            ExFreePoolWithTag(Copy, NTFSLX_TAG);
            return Status;
        }
        Src += ClusterSize;
    }

    ExFreePoolWithTag(Copy, NTFSLX_TAG);
    return STATUS_SUCCESS;
}

/*
 * Insert a new index entry into an in-memory INDX block (or INDEX_ROOT body).
 * `IndexHeader` points at the NTFSLX_INDEX_HEADER; `AllocatedBytes` is the
 * upper bound on IndexLength (i.e. the block's usable index area).
 *
 * Returns STATUS_DISK_FULL if the block lacks room, or
 * STATUS_OBJECT_NAME_COLLISION if an entry with the same name already exists.
 */
static NTSTATUS
NtfslxInsertEntryIntoIndexBody(
    _Inout_ PNTFSLX_INDEX_HEADER IndexHeader,
    _In_ ULONG AllocatedBytes,
    _In_reads_bytes_(NewEntryLength) const VOID *NewEntry,
    _In_ ULONG NewEntryLength,
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    PUCHAR IndexBase = (PUCHAR)IndexHeader;
    ULONG CurrentOffset = IndexHeader->EntriesOffset;
    ULONG IndexEntriesEnd = IndexHeader->IndexLength;
    ULONG InsertOffset = CurrentOffset;
    ULONG TailLength;
    const NTFSLX_FILE_NAME_ATTRIBUTE *NewFnAttr;
    ULONG NewNameLen;
    const WCHAR *NewName;

    NewFnAttr = (const NTFSLX_FILE_NAME_ATTRIBUTE *)
        ((const UCHAR *)NewEntry + sizeof(NTFSLX_I30_INDEX_ENTRY));
    NewNameLen = NewFnAttr->FileNameLength;
    NewName = (const WCHAR *)((const UCHAR *)NewFnAttr +
                              sizeof(NTFSLX_FILE_NAME_ATTRIBUTE));

    /* Find insertion point by collation. */
    while (CurrentOffset + sizeof(NTFSLX_I30_INDEX_ENTRY) <= IndexEntriesEnd)
    {
        PNTFSLX_I30_INDEX_ENTRY Cur = (PNTFSLX_I30_INDEX_ENTRY)(IndexBase + CurrentOffset);
        PNTFSLX_FILE_NAME_ATTRIBUTE CurFn;
        LONG Cmp;

        if (Cur->Length == 0)
            return STATUS_FILE_CORRUPT_ERROR;

        if (Cur->Flags & NTFSLX_INDEX_ENTRY_END)
        {
            InsertOffset = CurrentOffset;
            break;
        }

        if (Cur->KeyLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
        {
            CurFn = (PNTFSLX_FILE_NAME_ATTRIBUTE)((PUCHAR)Cur +
                     sizeof(NTFSLX_I30_INDEX_ENTRY));
            Cmp = NtfslxCollateNames(
                (const USHORT *)NewName, NewNameLen,
                (const USHORT *)((PUCHAR)CurFn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)),
                CurFn->FileNameLength,
                1, TRUE,
                DevExt->UpcaseTable,
                DevExt->UpcaseTableLength);
            if (Cmp < 0)
            {
                InsertOffset = CurrentOffset;
                break;
            }
            if (Cmp == 0)
            {
                return STATUS_OBJECT_NAME_COLLISION;
            }
        }

        CurrentOffset += Cur->Length;
        InsertOffset = CurrentOffset;
    }

    /* Capacity check. */
    if (IndexEntriesEnd + NewEntryLength > AllocatedBytes)
    {
        return STATUS_DISK_FULL;
    }

    /* Shift tail to make room. */
    TailLength = IndexEntriesEnd - InsertOffset;
    if (TailLength > 0)
    {
        RtlMoveMemory(IndexBase + InsertOffset + NewEntryLength,
                      IndexBase + InsertOffset,
                      TailLength);
    }

    /* Copy new entry. */
    RtlCopyMemory(IndexBase + InsertOffset, NewEntry, NewEntryLength);

    IndexHeader->IndexLength += NewEntryLength;
    return STATUS_SUCCESS;
}

/*
 * Locate and remove a $FILE_NAME entry from an in-memory INDX block body or
 * INDEX_ROOT body. The index header's IndexLength is decremented, but the
 * caller is responsible for flushing the block / MFT record.
 */
static NTSTATUS
NtfslxRemoveEntryFromIndexBody(
    _Inout_ PNTFSLX_INDEX_HEADER IndexHeader,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength,
    _Out_ PULONG OutRemovedLength,
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    PUCHAR IndexBase = (PUCHAR)IndexHeader;
    ULONG CurrentOffset = IndexHeader->EntriesOffset;
    ULONG IndexEntriesEnd = IndexHeader->IndexLength;
    ULONG EntryLength;
    ULONG TailLength;

    *OutRemovedLength = 0;

    while (CurrentOffset + sizeof(NTFSLX_I30_INDEX_ENTRY) <= IndexEntriesEnd)
    {
        PNTFSLX_I30_INDEX_ENTRY Cur = (PNTFSLX_I30_INDEX_ENTRY)(IndexBase + CurrentOffset);
        PNTFSLX_FILE_NAME_ATTRIBUTE CurFn;

        if (Cur->Length == 0)
            return STATUS_FILE_CORRUPT_ERROR;
        if (Cur->Flags & NTFSLX_INDEX_ENTRY_END)
            break;

        if (Cur->KeyLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
        {
            CurFn = (PNTFSLX_FILE_NAME_ATTRIBUTE)((PUCHAR)Cur +
                     sizeof(NTFSLX_I30_INDEX_ENTRY));
            if (CurFn->FileNameType != NTFSLX_FILE_NAME_DOS &&
                NtfslxAreNamesEqual(
                    (const USHORT *)FileName, FileNameLength,
                    (const USHORT *)((PUCHAR)CurFn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)),
                    CurFn->FileNameLength,
                    TRUE,
                    DevExt->UpcaseTable,
                    DevExt->UpcaseTableLength))
            {
                EntryLength = Cur->Length;
                TailLength = IndexEntriesEnd - (CurrentOffset + EntryLength);

                if (TailLength > 0)
                {
                    RtlMoveMemory(IndexBase + CurrentOffset,
                                  IndexBase + CurrentOffset + EntryLength,
                                  TailLength);
                }
                RtlZeroMemory(IndexBase + IndexEntriesEnd - EntryLength,
                              EntryLength);
                IndexHeader->IndexLength -= EntryLength;
                *OutRemovedLength = EntryLength;
                return STATUS_SUCCESS;
            }
        }

        CurrentOffset += Cur->Length;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/*
 * Scan $INDEX_ALLOCATION blocks looking for a matching name and remove it.
 * Returns STATUS_OBJECT_NAME_NOT_FOUND if no block holds the name.
 */
static NTSTATUS
NtfslxIndexAllocationRemove(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PNTFSLX_MFT_RECORD DirRecord,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_ATTR_RECORD IndexAllocAttr;
    PNTFSLX_ATTR_RECORD IndexRootAttr;
    PNTFSLX_INDEX_ROOT IndexRoot;
    PNTFSLX_RUNLIST_ELEMENT Runlist = NULL;
    ULONG RunlistCount = 0;
    PVOID BlockBuf = NULL;
    PNTFSLX_INDEX_HEADER BlockIndexHeader;
    ULONG BlockSize;
    ULONG ClustersPerBlock;
    ULONG RemovedLength;
    ULONGLONG TotalClusters;
    ULONGLONG NumBlocks;
    ULONGLONG Vcn;
    ULONGLONG BlockIdx;
    NTSTATUS Status;

    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexRootAttr);
    if (!NT_SUCCESS(Status))
        return STATUS_OBJECT_NAME_NOT_FOUND;

    IndexRoot = (PNTFSLX_INDEX_ROOT)((PUCHAR)IndexRootAttr +
                                      IndexRootAttr->Data.Resident.ValueOffset);
    BlockSize = IndexRoot->IndexBlockSize;
    if (BlockSize == 0)
        BlockSize = DevExt->VolumeInfo.BytesPerIndexRecord;
    ClustersPerBlock = BlockSize / DevExt->VolumeInfo.BytesPerCluster;
    if (ClustersPerBlock == 0)
        ClustersPerBlock = 1;

    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexAllocAttr);
    if (!NT_SUCCESS(Status) || IndexAllocAttr->NonResident == 0)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    Status = NtfslxBuildIndexAllocationRunlist(IndexAllocAttr, &Runlist,
                                               &RunlistCount);
    if (!NT_SUCCESS(Status))
        return STATUS_OBJECT_NAME_NOT_FOUND;

    TotalClusters = IndexAllocAttr->Data.NonResident.AllocatedSize /
                    DevExt->VolumeInfo.BytesPerCluster;
    NumBlocks = TotalClusters / ClustersPerBlock;

    BlockBuf = ExAllocatePoolWithTag(NonPagedPool, BlockSize, NTFSLX_TAG);
    if (BlockBuf == NULL)
    {
        ExFreePoolWithTag(Runlist, 'aIxN');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (BlockIdx = 0; BlockIdx < NumBlocks; BlockIdx++)
    {
        Vcn = BlockIdx * ClustersPerBlock;

        Status = NtfslxReadDirIndexBlock(DevExt, Runlist, Vcn, BlockSize, BlockBuf);
        if (!NT_SUCCESS(Status))
            continue;

        BlockIndexHeader = NtfslxIndxBlockHeader(BlockBuf);

        Status = NtfslxRemoveEntryFromIndexBody(BlockIndexHeader,
                                                 FileName, FileNameLength,
                                                 &RemovedLength,
                                                 DevExt);
        if (Status == STATUS_SUCCESS)
        {
            Status = NtfslxWriteDirIndexBlock(DevExt, Runlist, Vcn,
                                              BlockSize, BlockBuf);
            ExFreePoolWithTag(BlockBuf, NTFSLX_TAG);
            ExFreePoolWithTag(Runlist, 'aIxN');
            NTFSDBG("ntfslx: AllocationRemove: removed '%.*S' from vcn=%I64u (%lu bytes)\n",
                     FileNameLength, FileName, Vcn, RemovedLength);
            return Status;
        }
    }

    ExFreePoolWithTag(BlockBuf, NTFSLX_TAG);
    ExFreePoolWithTag(Runlist, 'aIxN');
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/*
 * Locate and in-place update a $FILE_NAME entry in an in-memory INDX block
 * body or INDEX_ROOT body. Returns STATUS_OBJECT_NAME_NOT_FOUND if the name
 * isn't present in this body.
 */
static NTSTATUS
NtfslxUpdateEntryInIndexBody(
    _Inout_ PNTFSLX_INDEX_HEADER IndexHeader,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength,
    _In_ ULONGLONG NewDataSize,
    _In_ ULONGLONG NewAllocatedSize,
    _In_ PLARGE_INTEGER LastDataChangeTime,
    _In_ PLARGE_INTEGER LastMftChangeTime,
    _In_ PLARGE_INTEGER LastAccessTime,
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    PUCHAR IndexBase = (PUCHAR)IndexHeader;
    ULONG CurrentOffset = IndexHeader->EntriesOffset;
    ULONG IndexEntriesEnd = IndexHeader->IndexLength;

    while (CurrentOffset + sizeof(NTFSLX_I30_INDEX_ENTRY) <= IndexEntriesEnd)
    {
        PNTFSLX_I30_INDEX_ENTRY Cur = (PNTFSLX_I30_INDEX_ENTRY)(IndexBase + CurrentOffset);
        PNTFSLX_FILE_NAME_ATTRIBUTE CurFn;

        if (Cur->Length == 0)
            return STATUS_FILE_CORRUPT_ERROR;
        if (Cur->Flags & NTFSLX_INDEX_ENTRY_END)
            break;

        if (Cur->KeyLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
        {
            CurFn = (PNTFSLX_FILE_NAME_ATTRIBUTE)((PUCHAR)Cur +
                     sizeof(NTFSLX_I30_INDEX_ENTRY));
            if (CurFn->FileNameType != NTFSLX_FILE_NAME_DOS &&
                NtfslxAreNamesEqual(
                    (const USHORT *)FileName, FileNameLength,
                    (const USHORT *)((PUCHAR)CurFn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)),
                    CurFn->FileNameLength,
                    TRUE,
                    DevExt->UpcaseTable,
                    DevExt->UpcaseTableLength))
            {
                CurFn->DataSize = NewDataSize;
                CurFn->AllocatedSize = NewAllocatedSize;
                CurFn->LastDataChangeTime = (ULONGLONG)LastDataChangeTime->QuadPart;
                CurFn->LastMftChangeTime = (ULONGLONG)LastMftChangeTime->QuadPart;
                CurFn->LastAccessTime = (ULONGLONG)LastAccessTime->QuadPart;
                return STATUS_SUCCESS;
            }
        }

        CurrentOffset += Cur->Length;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/*
 * Migrate all existing entries out of a resident $INDEX_ROOT into a freshly
 * allocated INDX block (and its $INDEX_ALLOCATION + $BITMAP attributes).
 * Leaves the $INDEX_ROOT with just a terminal end entry that has the NODE
 * flag + child VCN = 0. The caller's DirRecord is mutated in place.
 */
static NTSTATUS
NtfslxSpillIndexRootToAllocation(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _Inout_ PNTFSLX_MFT_RECORD DirRecord,
    _In_ ULONGLONG DirectoryMftIndex)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_ATTR_RECORD IndexRootAttr;
    PNTFSLX_ATTR_RECORD IndexAllocAttr;
    PNTFSLX_ATTR_RECORD BitmapAttr;
    PNTFSLX_INDEX_ROOT IndexRoot;
    PNTFSLX_INDEX_HEADER SrcHeader;
    PNTFSLX_RUNLIST_ELEMENT Runlist = NULL;
    ULONG RunlistCount = 0;
    PUCHAR BlockBuf = NULL;
    PNTFSLX_INDEX_HEADER DstHeader;
    PUCHAR SrcBase;
    PUCHAR DstBase;
    ULONG SrcEntriesOffset;
    ULONG SrcIndexLength;
    ULONG MigrateBytes;
    ULONG BlockSize;
    ULONG ClustersPerBlock;
    ULONG NewRootBody;
    ULONG NewRootAttrSize;
    PNTFSLX_I30_INDEX_ENTRY EndEntry;
    UCHAR MpBuf[16];
    ULONG MpLen;
    ULONG MpOffset;
    ULONG NewIndexAllocLen;
    PNTFSLX_ATTR_RECORD NewIndexAllocAttr = NULL;
    UCHAR InitialBitmap = 0x01;  /* first block is allocated */
    NTSTATUS Status;

    /* Locate resident $INDEX_ROOT. */
    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexRootAttr);
    if (!NT_SUCCESS(Status) || IndexRootAttr->NonResident != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = (PNTFSLX_INDEX_ROOT)((PUCHAR)IndexRootAttr +
                                      IndexRootAttr->Data.Resident.ValueOffset);
    SrcHeader = &IndexRoot->Index;
    SrcBase = (PUCHAR)SrcHeader;
    SrcEntriesOffset = SrcHeader->EntriesOffset;
    SrcIndexLength = SrcHeader->IndexLength;

    if (SrcIndexLength < SrcEntriesOffset ||
        SrcEntriesOffset < sizeof(NTFSLX_INDEX_HEADER))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /* If already spilled (LARGE flag set) we shouldn't be here. */
    if (SrcHeader->Flags & NTFSLX_INDEX_ROOT_FLAG_LARGE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    BlockSize = IndexRoot->IndexBlockSize;
    if (BlockSize == 0)
        BlockSize = DevExt->VolumeInfo.BytesPerIndexRecord;
    if (BlockSize == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    ClustersPerBlock = BlockSize / DevExt->VolumeInfo.BytesPerCluster;
    if (ClustersPerBlock == 0)
        ClustersPerBlock = 1;

    /* Allocate a single cluster run for the first INDX block. */
    Status = NtfslxAllocateClusters(DevExt->StorageDevice,
                                    &DevExt->VolumeInfo,
                                    DevExt->MftRunlist,
                                    ClustersPerBlock,
                                    0,
                                    &Runlist,
                                    &RunlistCount);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: SpillIndexRoot: AllocateClusters failed 0x%08lx\n", Status);
        return Status;
    }
    if (RunlistCount < 2 || Runlist[0].Length < (LONGLONG)ClustersPerBlock ||
        Runlist[0].Lcn < 0)
    {
        NTFSDBG("ntfslx: SpillIndexRoot: unexpected runlist shape count=%lu\n",
                 RunlistCount);
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, Runlist, RunlistCount);
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
        return STATUS_UNSUCCESSFUL;
    }

    /* Build the first INDX block and copy existing entries into it. */
    BlockBuf = ExAllocatePoolWithTag(NonPagedPool, BlockSize, NTFSLX_TAG);
    if (BlockBuf == NULL)
    {
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, Runlist, RunlistCount);
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NtfslxInitIndexBlock(BlockBuf, BlockSize, 0);

    DstHeader = NtfslxIndxBlockHeader(BlockBuf);
    DstBase = (PUCHAR)DstHeader;

    MigrateBytes = SrcIndexLength - SrcEntriesOffset;
    /* Don't migrate the terminal end entry (it's fine to leave in src temporarily,
     * we'll overwrite the dst end entry below). */
    /* Find terminal end entry length in src and exclude it from bulk copy. */
    {
        ULONG Walk = SrcEntriesOffset;
        ULONG BulkEnd = SrcIndexLength;
        while (Walk + sizeof(NTFSLX_I30_INDEX_ENTRY) <= SrcIndexLength)
        {
            PNTFSLX_I30_INDEX_ENTRY E = (PNTFSLX_I30_INDEX_ENTRY)(SrcBase + Walk);
            if (E->Length == 0) break;
            if (E->Flags & NTFSLX_INDEX_ENTRY_END)
            {
                BulkEnd = Walk;
                break;
            }
            Walk += E->Length;
        }
        MigrateBytes = BulkEnd - SrcEntriesOffset;
    }

    if (MigrateBytes > 0)
    {
        if (DstHeader->EntriesOffset + MigrateBytes +
            sizeof(NTFSLX_I30_INDEX_ENTRY) > DstHeader->AllocatedSize)
        {
            NTFSDBG("ntfslx: SpillIndexRoot: migrate %lu bytes won't fit block %lu\n",
                     MigrateBytes, BlockSize);
            ExFreePoolWithTag(BlockBuf, NTFSLX_TAG);
            NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                               DevExt->MftRunlist, Runlist, RunlistCount);
            ExFreePoolWithTag(Runlist, NTFSLX_TAG);
            return STATUS_DISK_FULL;
        }

        RtlCopyMemory(DstBase + DstHeader->EntriesOffset,
                      SrcBase + SrcEntriesOffset,
                      MigrateBytes);

        /* Rewrite the dst terminal end entry after the migrated entries. */
        EndEntry = (PNTFSLX_I30_INDEX_ENTRY)(DstBase + DstHeader->EntriesOffset +
                                              MigrateBytes);
        EndEntry->IndexedFile = 0;
        EndEntry->Length = sizeof(NTFSLX_I30_INDEX_ENTRY);
        EndEntry->KeyLength = 0;
        EndEntry->Flags = NTFSLX_INDEX_ENTRY_END;
        EndEntry->Reserved = 0;

        DstHeader->IndexLength = DstHeader->EntriesOffset + MigrateBytes +
                                  sizeof(NTFSLX_I30_INDEX_ENTRY);
    }

    /* Write the INDX block to its cluster. */
    Status = NtfslxWriteDirIndexBlock(DevExt, Runlist, 0, BlockSize, BlockBuf);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: SpillIndexRoot: WriteDirIndexBlock failed 0x%08lx\n", Status);
        ExFreePoolWithTag(BlockBuf, NTFSLX_TAG);
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, Runlist, RunlistCount);
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
        return Status;
    }

    ExFreePoolWithTag(BlockBuf, NTFSLX_TAG);

    /*
     * Shrink $INDEX_ROOT to: INDEX_ROOT header + terminal NODE end entry
     * (16 + 8 = 24 bytes of entries body), with LARGE flag set.
     *
     * New value length = INDEX_ROOT fixed header + INDEX_HEADER + entries.
     * INDEX_ROOT fixed header = 16 bytes (Type..ClustersPerIndexBlock).
     * INDEX_HEADER             = 16 bytes.
     * Terminal end entry       = 16 (header) + 8 (child VCN) = 24 bytes.
     * Total value length       = 16 + 16 + 24 = 56 bytes.
     */
    NewRootBody = (ULONG)(FIELD_OFFSET(NTFSLX_INDEX_ROOT, Index) +
                          sizeof(NTFSLX_INDEX_HEADER) +
                          sizeof(NTFSLX_I30_INDEX_ENTRY) + sizeof(ULONGLONG));
    NewRootAttrSize = ROUND_UP(IndexRootAttr->Data.Resident.ValueOffset + NewRootBody, 8);

    /* Resize (shrink) the root attribute in place. */
    Status = NtfslxResizeAttributeRecord(DirRecord, IndexRootAttr, NewRootAttrSize);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: SpillIndexRoot: shrink root failed 0x%08lx\n", Status);
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, Runlist, RunlistCount);
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
        return Status;
    }

    /* Refresh pointers after resize. */
    IndexRoot = (PNTFSLX_INDEX_ROOT)((PUCHAR)IndexRootAttr +
                                      IndexRootAttr->Data.Resident.ValueOffset);
    SrcHeader = &IndexRoot->Index;
    SrcBase = (PUCHAR)SrcHeader;
    IndexRootAttr->Data.Resident.ValueLength = NewRootBody;

    SrcHeader->EntriesOffset = sizeof(NTFSLX_INDEX_HEADER);
    SrcHeader->AllocatedSize = NewRootBody -
        (ULONG)FIELD_OFFSET(NTFSLX_INDEX_ROOT, Index);
    SrcHeader->Flags = NTFSLX_INDEX_ROOT_FLAG_LARGE;

    /* Build the terminal end entry with NODE flag and child VCN = 0. */
    EndEntry = (PNTFSLX_I30_INDEX_ENTRY)(SrcBase + sizeof(NTFSLX_INDEX_HEADER));
    EndEntry->IndexedFile = 0;
    EndEntry->Length = (USHORT)(sizeof(NTFSLX_I30_INDEX_ENTRY) + sizeof(ULONGLONG));
    EndEntry->KeyLength = 0;
    EndEntry->Flags = NTFSLX_INDEX_ENTRY_END | NTFSLX_INDEX_ENTRY_NODE;
    EndEntry->Reserved = 0;
    /* Child VCN in the 8 bytes following the entry header. */
    *(PULONGLONG)((PUCHAR)EndEntry + sizeof(NTFSLX_I30_INDEX_ENTRY)) = 0;

    SrcHeader->IndexLength = sizeof(NTFSLX_INDEX_HEADER) +
                             (ULONG)EndEntry->Length;

    /* Create or update $INDEX_ALLOCATION attribute. */
    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexAllocAttr);
    if (NT_SUCCESS(Status))
    {
        /* An allocation already exists — caller should have taken the
         * append path, not the spill path. */
        NTFSDBG("ntfslx: SpillIndexRoot: stale $INDEX_ALLOCATION present\n");
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, Runlist, RunlistCount);
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /* Build a single-run mapping pair. */
    MpLen = NtfslxBuildSingleRunMappingPairs(Runlist[0].Lcn, ClustersPerBlock,
                                             MpBuf, sizeof(MpBuf));
    if (MpLen == 0)
    {
        NTFSDBG("ntfslx: SpillIndexRoot: mapping pairs too long\n");
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, Runlist, RunlistCount);
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Manually construct the non-resident $INDEX_ALLOCATION attribute:
     *   0..15   generic header (Type, Length, NonResident, NameLen, NameOff, ...)
     *   16..71  non-resident block (LowestVcn...CompressedSize = 56 bytes)
     *   72..79  name "$I30" (4 WCHARs)
     *   80..    mapping pairs + padding
     *
     * NameOffset = 72, MappingPairsOffset = 80.
     *
     * Since NtfslxInsertAttributeRecord's resident layout would put the name
     * at +24 (inside the NonResident union area), we allocate space via an
     * empty-resident placeholder, resize to hold everything, then overwrite
     * the entire attribute body.
     */
    {
        ULONG NameBytes = (ULONG)(RTL_NUMBER_OF(IndexName) - 1) * sizeof(WCHAR);
        ULONG NameOff = sizeof(NTFSLX_ATTR_RECORD);
        MpOffset = ROUND_UP(NameOff + NameBytes, 8);
        NewIndexAllocLen = ROUND_UP(MpOffset + MpLen, 8);
    }

    /* Start with an empty-resident placeholder we can then rewrite in place. */
    Status = NtfslxInsertAttributeRecord(DirRecord,
                                         DevExt->VolumeInfo.BytesPerFileRecord,
                                         NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                         NULL, 0,
                                         NULL, 0,
                                         &NewIndexAllocAttr);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: SpillIndexRoot: insert placeholder $INDEX_ALLOCATION failed 0x%08lx\n",
                 Status);
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, Runlist, RunlistCount);
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxResizeAttributeRecord(DirRecord, NewIndexAllocAttr,
                                         NewIndexAllocLen);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: SpillIndexRoot: resize $INDEX_ALLOCATION to %lu failed 0x%08lx\n",
                 NewIndexAllocLen, Status);
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, Runlist, RunlistCount);
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
        return Status;
    }

    /* Wipe the body and set up the non-resident header + name + mapping pairs. */
    RtlZeroMemory((PUCHAR)NewIndexAllocAttr + 16, NewIndexAllocLen - 16);
    NewIndexAllocAttr->NonResident = 1;
    NewIndexAllocAttr->NameLength = (UCHAR)(RTL_NUMBER_OF(IndexName) - 1);
    NewIndexAllocAttr->NameOffset = (USHORT)sizeof(NTFSLX_ATTR_RECORD);
    NewIndexAllocAttr->Flags = 0;
    NewIndexAllocAttr->Data.NonResident.LowestVcn = 0;
    NewIndexAllocAttr->Data.NonResident.HighestVcn = ClustersPerBlock - 1;
    NewIndexAllocAttr->Data.NonResident.MappingPairsOffset = (USHORT)MpOffset;
    NewIndexAllocAttr->Data.NonResident.CompressionUnit = 0;
    NewIndexAllocAttr->Data.NonResident.AllocatedSize =
        (ULONGLONG)ClustersPerBlock * DevExt->VolumeInfo.BytesPerCluster;
    NewIndexAllocAttr->Data.NonResident.DataSize =
        NewIndexAllocAttr->Data.NonResident.AllocatedSize;
    NewIndexAllocAttr->Data.NonResident.InitializedSize =
        NewIndexAllocAttr->Data.NonResident.AllocatedSize;
    NewIndexAllocAttr->Data.NonResident.CompressedSize = 0;

    /* Name at offset 72. */
    RtlCopyMemory((PUCHAR)NewIndexAllocAttr + NewIndexAllocAttr->NameOffset,
                  IndexName,
                  (ULONG)NewIndexAllocAttr->NameLength * sizeof(WCHAR));

    /* Mapping pairs at MappingPairsOffset. */
    RtlCopyMemory((PUCHAR)NewIndexAllocAttr + MpOffset, MpBuf, MpLen);

    /* Create $BITMAP (resident, 8 bytes) with bit 0 set. */
    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_BITMAP,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &BitmapAttr);
    if (Status == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        UCHAR BitmapValue[8] = { 0 };
        BitmapValue[0] = InitialBitmap;
        Status = NtfslxInsertAttributeRecord(DirRecord,
                                             DevExt->VolumeInfo.BytesPerFileRecord,
                                             NTFSLX_ATTRIBUTE_BITMAP,
                                             IndexName,
                                             RTL_NUMBER_OF(IndexName) - 1,
                                             BitmapValue, sizeof(BitmapValue),
                                             NULL);
    }
    else if (NT_SUCCESS(Status) && BitmapAttr->NonResident == 0 &&
             BitmapAttr->Data.Resident.ValueLength >= 1)
    {
        PUCHAR BitmapData = (PUCHAR)BitmapAttr + BitmapAttr->Data.Resident.ValueOffset;
        BitmapData[0] |= InitialBitmap;
        Status = STATUS_SUCCESS;
    }
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: SpillIndexRoot: $BITMAP insert/update failed 0x%08lx\n",
                 Status);
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, Runlist, RunlistCount);
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
        return Status;
    }

    /* Free our temporary runlist; the on-disk attribute holds the canonical copy. */
    ExFreePoolWithTag(Runlist, NTFSLX_TAG);

    NTFSDBG("ntfslx: SpillIndexRoot dir=%I64u migrated=%lu bytes, block at lcn=%I64d\n",
             DirectoryMftIndex, MigrateBytes,
             (LONGLONG)NewIndexAllocAttr->Data.NonResident.DataSize / DevExt->VolumeInfo.BytesPerCluster);

    return STATUS_SUCCESS;
}

/*
 * Insert a new entry into one of the directory's existing INDX blocks.
 * Scans every block until it finds one with room; if every block is full,
 * append a new right-sibling leaf block when the new name collates after
 * the current rightmost leaf range. The caller persists any in-memory
 * $INDEX_ALLOCATION / $BITMAP changes to the directory MFT record.
 */
static NTSTATUS
NtfslxIndexAllocationInsert(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PNTFSLX_MFT_RECORD DirRecord,
    _In_reads_bytes_(NewEntryLength) const VOID *NewEntry,
    _In_ ULONG NewEntryLength)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_ATTR_RECORD IndexAllocAttr;
    PNTFSLX_ATTR_RECORD IndexRootAttr;
    PNTFSLX_INDEX_ROOT IndexRoot;
    PNTFSLX_RUNLIST_ELEMENT Runlist = NULL;
    ULONG RunlistCount = 0;
    PVOID BlockBuf = NULL;
    PNTFSLX_INDEX_HEADER BlockIndexHeader;
    ULONG BlockSize;
    ULONG ClustersPerBlock;
    ULONGLONG TotalClusters;
    ULONGLONG NumBlocks;
    ULONGLONG Vcn;
    ULONGLONG BlockIdx;
    NTSTATUS Status;
    NTSTATUS LastFailure = STATUS_DISK_FULL;

    /* We need the root's ClustersPerIndexBlock to size blocks. */
    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexRootAttr);
    if (!NT_SUCCESS(Status))
        return Status;

    IndexRoot = (PNTFSLX_INDEX_ROOT)((PUCHAR)IndexRootAttr +
                                      IndexRootAttr->Data.Resident.ValueOffset);
    BlockSize = IndexRoot->IndexBlockSize;
    if (BlockSize == 0)
        BlockSize = DevExt->VolumeInfo.BytesPerIndexRecord;

    ClustersPerBlock = BlockSize / DevExt->VolumeInfo.BytesPerCluster;
    if (ClustersPerBlock == 0)
        ClustersPerBlock = 1;

    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexAllocAttr);
    if (!NT_SUCCESS(Status) || IndexAllocAttr->NonResident == 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = NtfslxBuildIndexAllocationRunlist(IndexAllocAttr,
                                               &Runlist,
                                               &RunlistCount);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocationInsert: BuildRunlist failed 0x%08lx\n", Status);
        return Status;
    }

    TotalClusters = IndexAllocAttr->Data.NonResident.AllocatedSize /
                    DevExt->VolumeInfo.BytesPerCluster;
    NumBlocks = TotalClusters / ClustersPerBlock;

    BlockBuf = ExAllocatePoolWithTag(NonPagedPool, BlockSize, NTFSLX_TAG);
    if (BlockBuf == NULL)
    {
        ExFreePoolWithTag(Runlist, 'aIxN');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NTFSDBG("ntfslx: AllocationInsert: scanning %I64u blocks\n", NumBlocks);

    for (BlockIdx = 0; BlockIdx < NumBlocks; BlockIdx++)
    {
        Vcn = BlockIdx * ClustersPerBlock;

        Status = NtfslxReadDirIndexBlock(DevExt, Runlist, Vcn, BlockSize, BlockBuf);
        if (!NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: AllocationInsert: read block vcn=%I64u failed 0x%08lx\n",
                     Vcn, Status);
            LastFailure = Status;
            continue;
        }

        BlockIndexHeader = NtfslxIndxBlockHeader(BlockBuf);

        Status = NtfslxInsertEntryIntoIndexBody(
            BlockIndexHeader,
            BlockIndexHeader->AllocatedSize,
            NewEntry, NewEntryLength,
            DevExt);

        NTFSDBG("ntfslx: AllocationInsert: block vcn=%I64u insert result=0x%08lx\n",
                 Vcn, Status);

        if (Status == STATUS_DISK_FULL)
        {
            LastFailure = Status;
            continue;
        }
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(BlockBuf, NTFSLX_TAG);
            ExFreePoolWithTag(Runlist, 'aIxN');
            return Status;
        }

        /* Success — write the updated block back. */
        Status = NtfslxWriteDirIndexBlock(DevExt, Runlist, Vcn, BlockSize, BlockBuf);
        ExFreePoolWithTag(BlockBuf, NTFSLX_TAG);
        ExFreePoolWithTag(Runlist, 'aIxN');
        if (NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: AllocationInsert: wrote to block vcn=%I64u\n", Vcn);
        }
        return Status;
    }

    ExFreePoolWithTag(BlockBuf, NTFSLX_TAG);
    ExFreePoolWithTag(Runlist, 'aIxN');

    NTFSDBG("ntfslx: AllocationInsert: no block has room (blocks=%I64u)\n",
             NumBlocks);
    if (LastFailure == STATUS_DISK_FULL)
    {
        NTSTATUS ExtendStatus;

        ExtendStatus = NtfslxAppendRightLeafIndexBlock(DevExt,
                                                       DirRecord,
                                                       NewEntry,
                                                       NewEntryLength);
        if (NT_SUCCESS(ExtendStatus))
        {
            return STATUS_SUCCESS;
        }

        NTFSDBG("ntfslx: AllocationInsert: append-right failed 0x%08lx\n",
                 ExtendStatus);
        LastFailure = ExtendStatus;
    }

    return LastFailure;
}

/*
 * Search every $INDEX_ALLOCATION block for a matching $FILE_NAME entry and
 * update its sizes/timestamps in place. Returns STATUS_OBJECT_NAME_NOT_FOUND
 * if no block holds the name.
 */
static NTSTATUS
NtfslxIndexAllocationUpdate(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PNTFSLX_MFT_RECORD DirRecord,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength,
    _In_ ULONGLONG NewDataSize,
    _In_ ULONGLONG NewAllocatedSize,
    _In_ PLARGE_INTEGER LastDataChangeTime,
    _In_ PLARGE_INTEGER LastMftChangeTime,
    _In_ PLARGE_INTEGER LastAccessTime)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_ATTR_RECORD IndexAllocAttr;
    PNTFSLX_ATTR_RECORD IndexRootAttr;
    PNTFSLX_INDEX_ROOT IndexRoot;
    PNTFSLX_RUNLIST_ELEMENT Runlist = NULL;
    ULONG RunlistCount = 0;
    PVOID BlockBuf = NULL;
    PNTFSLX_INDEX_HEADER BlockIndexHeader;
    ULONG BlockSize;
    ULONG ClustersPerBlock;
    ULONGLONG TotalClusters;
    ULONGLONG NumBlocks;
    ULONGLONG Vcn;
    ULONGLONG BlockIdx;
    NTSTATUS Status;

    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexRootAttr);
    if (!NT_SUCCESS(Status))
        return STATUS_OBJECT_NAME_NOT_FOUND;

    IndexRoot = (PNTFSLX_INDEX_ROOT)((PUCHAR)IndexRootAttr +
                                      IndexRootAttr->Data.Resident.ValueOffset);
    BlockSize = IndexRoot->IndexBlockSize;
    if (BlockSize == 0)
        BlockSize = DevExt->VolumeInfo.BytesPerIndexRecord;
    ClustersPerBlock = BlockSize / DevExt->VolumeInfo.BytesPerCluster;
    if (ClustersPerBlock == 0)
        ClustersPerBlock = 1;

    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexAllocAttr);
    if (!NT_SUCCESS(Status) || IndexAllocAttr->NonResident == 0)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    Status = NtfslxBuildIndexAllocationRunlist(IndexAllocAttr, &Runlist,
                                               &RunlistCount);
    if (!NT_SUCCESS(Status))
        return STATUS_OBJECT_NAME_NOT_FOUND;

    TotalClusters = IndexAllocAttr->Data.NonResident.AllocatedSize /
                    DevExt->VolumeInfo.BytesPerCluster;
    NumBlocks = TotalClusters / ClustersPerBlock;

    BlockBuf = ExAllocatePoolWithTag(NonPagedPool, BlockSize, NTFSLX_TAG);
    if (BlockBuf == NULL)
    {
        ExFreePoolWithTag(Runlist, 'aIxN');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (BlockIdx = 0; BlockIdx < NumBlocks; BlockIdx++)
    {
        Vcn = BlockIdx * ClustersPerBlock;

        Status = NtfslxReadDirIndexBlock(DevExt, Runlist, Vcn, BlockSize, BlockBuf);
        if (!NT_SUCCESS(Status))
            continue;

        BlockIndexHeader = NtfslxIndxBlockHeader(BlockBuf);

        Status = NtfslxUpdateEntryInIndexBody(
            BlockIndexHeader,
            FileName, FileNameLength,
            NewDataSize, NewAllocatedSize,
            LastDataChangeTime, LastMftChangeTime, LastAccessTime,
            DevExt);

        if (Status == STATUS_SUCCESS)
        {
            Status = NtfslxWriteDirIndexBlock(DevExt, Runlist, Vcn,
                                              BlockSize, BlockBuf);
            ExFreePoolWithTag(BlockBuf, NTFSLX_TAG);
            ExFreePoolWithTag(Runlist, 'aIxN');
            return Status;
        }
        /* STATUS_OBJECT_NAME_NOT_FOUND or other -- keep scanning. */
    }

    ExFreePoolWithTag(BlockBuf, NTFSLX_TAG);
    ExFreePoolWithTag(Runlist, 'aIxN');
    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/*
 * Build a complete $I30 index entry from file creation parameters.
 * Caller must free the returned buffer.
 */
static
NTSTATUS
NtfslxBuildI30IndexEntry(
    _In_ ULONGLONG FileMftReference,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength,
    _In_ UCHAR FileNameType,
    _In_ ULONG FileAttributes,
    _In_ ULONGLONG FileDataSize,
    _In_ ULONGLONG FileAllocSize,
    _In_ PLARGE_INTEGER CreationTime,
    _In_ PLARGE_INTEGER LastDataChangeTime,
    _In_ PLARGE_INTEGER LastMftChangeTime,
    _In_ PLARGE_INTEGER LastAccessTime,
    _In_ ULONGLONG ParentMftReference,
    _Outptr_ PUCHAR *EntryBuffer,
    _Out_ PULONG EntryLength)
{
    PNTFSLX_I30_INDEX_ENTRY Entry;
    PNTFSLX_FILE_NAME_ATTRIBUTE FnAttr;
    ULONG FnAttrSize;
    ULONG NameBytes;
    ULONG TotalLength;
    PUCHAR Buffer;

    *EntryBuffer = NULL;
    *EntryLength = 0;

    NameBytes = FileNameLength * sizeof(WCHAR);
    FnAttrSize = sizeof(NTFSLX_FILE_NAME_ATTRIBUTE) + NameBytes;
    TotalLength = ROUND_UP(sizeof(NTFSLX_I30_INDEX_ENTRY) + FnAttrSize, 8);

    Buffer = ExAllocatePoolWithTag(NonPagedPool, TotalLength, NTFSLX_TAG);
    if (Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Buffer, TotalLength);

    Entry = (PNTFSLX_I30_INDEX_ENTRY)Buffer;
    Entry->IndexedFile = FileMftReference;
    Entry->Length = (USHORT)TotalLength;
    Entry->KeyLength = (USHORT)FnAttrSize;
    Entry->Flags = 0;
    Entry->Reserved = 0;

    FnAttr = (PNTFSLX_FILE_NAME_ATTRIBUTE)(Buffer + sizeof(NTFSLX_I30_INDEX_ENTRY));
    FnAttr->ParentDirectory = ParentMftReference;
    FnAttr->CreationTime = CreationTime->QuadPart;
    FnAttr->LastDataChangeTime = LastDataChangeTime->QuadPart;
    FnAttr->LastMftChangeTime = LastMftChangeTime->QuadPart;
    FnAttr->LastAccessTime = LastAccessTime->QuadPart;
    FnAttr->AllocatedSize = FileAllocSize;
    FnAttr->DataSize = FileDataSize;
    FnAttr->FileAttributes = FileAttributes;
    FnAttr->Type.Ea.PackedEaSize = 0;
    FnAttr->Type.Ea.Reserved = 0;
    FnAttr->FileNameLength = (UCHAR)FileNameLength;
    FnAttr->FileNameType = FileNameType;

    RtlCopyMemory((PUCHAR)FnAttr + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE),
                  FileName, NameBytes);

    *EntryBuffer = Buffer;
    *EntryLength = TotalLength;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxIndexInsertFileName(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG DirectoryMftIndex,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength,
    _In_ UCHAR FileNameType,
    _In_ ULONGLONG FileMftReference,
    _In_ ULONG FileAttributes,
    _In_ ULONGLONG FileDataSize,
    _In_ ULONGLONG FileAllocSize,
    _In_ PLARGE_INTEGER CreationTime,
    _In_ PLARGE_INTEGER LastDataChangeTime,
    _In_ PLARGE_INTEGER LastMftChangeTime,
    _In_ PLARGE_INTEGER LastAccessTime)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_MFT_RECORD DirRecord;
    PNTFSLX_ATTR_RECORD IndexRootAttr;
    PNTFSLX_ATTR_RECORD AllocAttr;
    PNTFSLX_INDEX_ROOT IndexRoot;
    PNTFSLX_INDEX_HEADER IndexHeader;
    PNTFSLX_I30_INDEX_ENTRY CurrentEntry;
    PNTFSLX_FILE_NAME_ATTRIBUTE CurrentFn;
    PUCHAR NewEntry;
    PUCHAR IndexBase;
    ULONG NewEntryLength;
    ULONG RecordSize;
    ULONG InsertOffset;
    ULONG IndexEntriesEnd;
    ULONG TailLength;
    ULONG NewIndexLength;
    ULONG NewAttrValueLength;
    ULONG CurrentOffset;
    ULONGLONG ParentMftRef;
    LONG CompareResult;
    NTSTATUS FindAlloc;
    BOOLEAN UseResidentRootInsert;
    NTSTATUS Status;

    RecordSize = DevExt->VolumeInfo.BytesPerFileRecord;

    /* Read the directory MFT record so we can use its live sequence number. */
    DirRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (DirRecord == NULL)
    {
        ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                 DevExt->MftRunlist, DirectoryMftIndex, DirRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        return Status;
    }

    ParentMftRef = MK_MREF(DirectoryMftIndex, DirRecord->SequenceNumber);
    Status = NtfslxBuildI30IndexEntry(FileMftReference,
                                      FileName, FileNameLength,
                                      FileNameType, FileAttributes,
                                      FileDataSize, FileAllocSize,
                                      CreationTime, LastDataChangeTime,
                                      LastMftChangeTime, LastAccessTime,
                                      ParentMftRef,
                                      &NewEntry, &NewEntryLength);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        return Status;
    }

    /* Find INDEX_ROOT ($I30) */
    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexRootAttr);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
        return Status;
    }

    if (IndexRootAttr->NonResident != 0)
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = (PNTFSLX_INDEX_ROOT)((PUCHAR)IndexRootAttr +
                                      IndexRootAttr->Data.Resident.ValueOffset);
    IndexHeader = &IndexRoot->Index;

    /*
     * Once a directory has spilled into $INDEX_ALLOCATION, the resident root
     * is no longer a plain append-only entry array. It becomes the B-tree
     * root and must retain only separator entries / child pointers. Inserting
     * new names directly into the root after that point breaks lookup for
     * names that belong to the child block (for example $Secure in the root
     * directory), so large-directory inserts must go straight to allocation.
     */
    FindAlloc = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                    IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                    &AllocAttr);
    if (FindAlloc != STATUS_OBJECT_NAME_NOT_FOUND && !NT_SUCCESS(FindAlloc))
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
        return FindAlloc;
    }
    UseResidentRootInsert =
        (FindAlloc == STATUS_OBJECT_NAME_NOT_FOUND) &&
        ((IndexHeader->Flags & NTFSLX_INDEX_ROOT_FLAG_LARGE) == 0);

    /* Collision + insertion-point scan on the resident root. We also need this
     * so we can detect duplicates even when the final insert will land in an
     * allocation block. */
    IndexBase = (PUCHAR)IndexHeader;
    InsertOffset = IndexHeader->EntriesOffset;
    IndexEntriesEnd = IndexHeader->IndexLength;
    CurrentOffset = InsertOffset;
    while (CurrentOffset + sizeof(NTFSLX_I30_INDEX_ENTRY) <= IndexEntriesEnd)
    {
        CurrentEntry = (PNTFSLX_I30_INDEX_ENTRY)(IndexBase + CurrentOffset);

        if (CurrentEntry->Length == 0)
            break;

        if (CurrentEntry->Flags & NTFSLX_INDEX_ENTRY_END)
        {
            InsertOffset = CurrentOffset;
            break;
        }

        if (CurrentEntry->KeyLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
        {
            CurrentFn = (PNTFSLX_FILE_NAME_ATTRIBUTE)((PUCHAR)CurrentEntry +
                         sizeof(NTFSLX_I30_INDEX_ENTRY));
            CompareResult = NtfslxCollateNames(
                (const USHORT *)FileName, FileNameLength,
                (const USHORT *)((PUCHAR)CurrentFn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)),
                CurrentFn->FileNameLength,
                1,
                TRUE,
                DevExt->UpcaseTable,
                DevExt->UpcaseTableLength);

            if (CompareResult < 0)
            {
                InsertOffset = CurrentOffset;
                break;
            }
            else if (CompareResult == 0)
            {
                ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
                ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
                return STATUS_OBJECT_NAME_COLLISION;
            }
        }

        CurrentOffset += CurrentEntry->Length;
        InsertOffset = CurrentOffset;
    }

    if (UseResidentRootInsert)
    {
        /* Try the resident-root path first. */
        NewIndexLength = IndexHeader->IndexLength + NewEntryLength;
        NewAttrValueLength = IndexRootAttr->Data.Resident.ValueLength + NewEntryLength;

        {
            ULONG NewAttrLength = ROUND_UP(
                IndexRootAttr->Data.Resident.ValueOffset + NewAttrValueLength, 8);
            ULONG InsertRelativeOffset = InsertOffset;
            NTSTATUS ResizeStatus;

            ResizeStatus = NtfslxResizeAttributeRecord(DirRecord, IndexRootAttr,
                                                       NewAttrLength);
            if (NT_SUCCESS(ResizeStatus))
            {
                /* Refresh pointers since the attribute may have been zero-filled in its tail */
                IndexRoot = (PNTFSLX_INDEX_ROOT)((PUCHAR)IndexRootAttr +
                                                  IndexRootAttr->Data.Resident.ValueOffset);
                IndexHeader = &IndexRoot->Index;
                IndexBase = (PUCHAR)IndexHeader;

                TailLength = IndexEntriesEnd - InsertRelativeOffset;
                if (TailLength > 0)
                {
                    RtlMoveMemory(IndexBase + InsertRelativeOffset + NewEntryLength,
                                  IndexBase + InsertRelativeOffset,
                                  TailLength);
                }

                RtlCopyMemory(IndexBase + InsertRelativeOffset, NewEntry, NewEntryLength);

                IndexHeader->IndexLength = NewIndexLength;
                IndexHeader->AllocatedSize = NewIndexLength;
                IndexRootAttr->Data.Resident.ValueLength = NewAttrValueLength;

                Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                              DevExt->MftRunlist, DirectoryMftIndex, DirRecord);
                ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
                ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
                return Status;
            }

            if (ResizeStatus != STATUS_DISK_FULL)
            {
                ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
                ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
                return ResizeStatus;
            }
            NTFSDBG("ntfslx: IndexInsert dir=%I64u resident root full, trying allocation\n",
                     DirectoryMftIndex);
        }
    }
    else
    {
        NTFSDBG("ntfslx: IndexInsert dir=%I64u bypassing resident root for large index\n",
                 DirectoryMftIndex);
    }

    /*
     * Resident root is full. Fallback: insert into $INDEX_ALLOCATION.
     *
     * Step 1: if no $INDEX_ALLOCATION exists yet, migrate resident entries out
     *         of $INDEX_ROOT into a fresh allocation block and mark the root
     *         with the LARGE flag. The root's MFT record must be written
     *         after this step so the on-disk state matches the in-memory one.
     *
     * Step 2: locate an existing allocation block with room and insert there.
     *         If no block has room, allocate another cluster (extend the
     *         allocation) — not implemented in this pass, fall back to
     *         STATUS_DISK_FULL.
     */
    {
        if (FindAlloc == STATUS_OBJECT_NAME_NOT_FOUND)
        {
            Status = NtfslxSpillIndexRootToAllocation(DevExt, DirRecord,
                                                      DirectoryMftIndex);
            if (!NT_SUCCESS(Status))
            {
                NTFSDBG("ntfslx: IndexInsert: spill failed 0x%08lx\n", Status);
                ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
                ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
                return Status;
            }
            /* Persist the mutated MFT record so the allocation-insert below
             * reads a consistent directory state. */
            Status = NtfslxWriteMftRecord(DevExt->StorageDevice,
                                          &DevExt->VolumeInfo,
                                          DevExt->MftRunlist,
                                          DirectoryMftIndex,
                                          DirRecord);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
                ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
                return Status;
            }
        }
        else if (!NT_SUCCESS(FindAlloc))
        {
            ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
            ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
            return FindAlloc;
        }
    }

    Status = NtfslxIndexAllocationInsert(DevExt, DirRecord, NewEntry, NewEntryLength);
    if (NT_SUCCESS(Status))
    {
        Status = NtfslxWriteMftRecord(DevExt->StorageDevice,
                                      &DevExt->VolumeInfo,
                                      DevExt->MftRunlist,
                                      DirectoryMftIndex,
                                      DirRecord);
    }
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: IndexInsert: allocation insert failed 0x%08lx\n", Status);
    }

    ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
    ExFreePoolWithTag(NewEntry, NTFSLX_TAG);
    return Status;
}

/*
 * Patch an existing $I30 entry in place: DataSize / AllocatedSize / times.
 * Entry byte size is unchanged, so no shifts or header length updates.
 */
NTSTATUS
NtfslxIndexUpdateFileName(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG DirectoryMftIndex,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength,
    _In_ ULONGLONG NewDataSize,
    _In_ ULONGLONG NewAllocatedSize,
    _In_ PLARGE_INTEGER CreationTime,
    _In_ PLARGE_INTEGER LastDataChangeTime,
    _In_ PLARGE_INTEGER LastMftChangeTime,
    _In_ PLARGE_INTEGER LastAccessTime)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_MFT_RECORD DirRecord;
    PNTFSLX_ATTR_RECORD IndexRootAttr;
    PNTFSLX_INDEX_ROOT IndexRoot;
    PNTFSLX_INDEX_HEADER IndexHeader;
    PNTFSLX_I30_INDEX_ENTRY CurrentEntry;
    PNTFSLX_FILE_NAME_ATTRIBUTE CurrentFn;
    PUCHAR IndexBase;
    ULONG RecordSize;
    ULONG CurrentOffset;
    ULONG IndexEntriesEnd;
    BOOLEAN Found;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(CreationTime);

    RecordSize = DevExt->VolumeInfo.BytesPerFileRecord;

    DirRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (DirRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                 DevExt->MftRunlist, DirectoryMftIndex, DirRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexRootAttr);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        return Status;
    }

    if (IndexRootAttr->NonResident != 0)
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = (PNTFSLX_INDEX_ROOT)((PUCHAR)IndexRootAttr +
                                      IndexRootAttr->Data.Resident.ValueOffset);
    IndexHeader = &IndexRoot->Index;
    IndexBase = (PUCHAR)IndexHeader;

    Found = FALSE;
    CurrentOffset = IndexHeader->EntriesOffset;
    IndexEntriesEnd = IndexHeader->IndexLength;

    while (CurrentOffset + sizeof(NTFSLX_I30_INDEX_ENTRY) <= IndexEntriesEnd)
    {
        CurrentEntry = (PNTFSLX_I30_INDEX_ENTRY)(IndexBase + CurrentOffset);

        if (CurrentEntry->Length == 0)
        {
            break;
        }

        if (CurrentEntry->Flags & NTFSLX_INDEX_ENTRY_END)
        {
            break;
        }

        if (CurrentEntry->KeyLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
        {
            CurrentFn = (PNTFSLX_FILE_NAME_ATTRIBUTE)((PUCHAR)CurrentEntry +
                         sizeof(NTFSLX_I30_INDEX_ENTRY));

            if (CurrentFn->FileNameType != NTFSLX_FILE_NAME_DOS &&
                NtfslxAreNamesEqual(
                    (const USHORT *)FileName, FileNameLength,
                    (const USHORT *)((PUCHAR)CurrentFn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)),
                    CurrentFn->FileNameLength,
                    TRUE,
                    DevExt->UpcaseTable,
                    DevExt->UpcaseTableLength))
            {
                CurrentFn->DataSize = NewDataSize;
                CurrentFn->AllocatedSize = NewAllocatedSize;
                CurrentFn->LastDataChangeTime = (ULONGLONG)LastDataChangeTime->QuadPart;
                CurrentFn->LastMftChangeTime = (ULONGLONG)LastMftChangeTime->QuadPart;
                CurrentFn->LastAccessTime = (ULONGLONG)LastAccessTime->QuadPart;
                Found = TRUE;
                break;
            }
        }

        CurrentOffset += CurrentEntry->Length;
    }

    if (Found)
    {
        Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                      DevExt->MftRunlist, DirectoryMftIndex, DirRecord);
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        return Status;
    }

    /* Not found in the resident root — check $INDEX_ALLOCATION blocks. */
    Status = NtfslxIndexAllocationUpdate(DevExt, DirRecord,
                                          FileName, FileNameLength,
                                          NewDataSize, NewAllocatedSize,
                                          LastDataChangeTime,
                                          LastMftChangeTime,
                                          LastAccessTime);
    ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
    return Status;
}

NTSTATUS
NtfslxIndexRemoveFileName(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG DirectoryMftIndex,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_MFT_RECORD DirRecord;
    PNTFSLX_ATTR_RECORD IndexRootAttr;
    PNTFSLX_INDEX_ROOT IndexRoot;
    PNTFSLX_INDEX_HEADER IndexHeader;
    PNTFSLX_I30_INDEX_ENTRY CurrentEntry;
    PNTFSLX_FILE_NAME_ATTRIBUTE CurrentFn;
    PUCHAR IndexBase;
    ULONG RecordSize;
    ULONG CurrentOffset;
    ULONG IndexEntriesEnd;
    ULONG EntryLength;
    ULONG TailLength;
    BOOLEAN Found;
    NTSTATUS Status;

    RecordSize = DevExt->VolumeInfo.BytesPerFileRecord;

    /* Read the directory MFT record */
    DirRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (DirRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                 DevExt->MftRunlist, DirectoryMftIndex, DirRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        return Status;
    }

    /* Find INDEX_ROOT ($I30) */
    Status = NtfslxFindAttribute(DirRecord, NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 IndexName, RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexRootAttr);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        return Status;
    }

    if (IndexRootAttr->NonResident != 0)
    {
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = (PNTFSLX_INDEX_ROOT)((PUCHAR)IndexRootAttr +
                                      IndexRootAttr->Data.Resident.ValueOffset);
    IndexHeader = &IndexRoot->Index;
    IndexBase = (PUCHAR)IndexHeader;

    /* Search for the entry to remove in the resident root first. */
    Found = FALSE;
    CurrentOffset = IndexHeader->EntriesOffset;
    IndexEntriesEnd = IndexHeader->IndexLength;

    while (CurrentOffset + sizeof(NTFSLX_I30_INDEX_ENTRY) <= IndexEntriesEnd)
    {
        CurrentEntry = (PNTFSLX_I30_INDEX_ENTRY)(IndexBase + CurrentOffset);

        if (CurrentEntry->Length == 0)
        {
            break;
        }

        if (CurrentEntry->Flags & NTFSLX_INDEX_ENTRY_END)
        {
            break;
        }

        /* Compare names */
        if (CurrentEntry->KeyLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
        {
            CurrentFn = (PNTFSLX_FILE_NAME_ATTRIBUTE)((PUCHAR)CurrentEntry +
                         sizeof(NTFSLX_I30_INDEX_ENTRY));

            if (CurrentFn->FileNameType != NTFSLX_FILE_NAME_DOS &&
                NtfslxAreNamesEqual(
                    (const USHORT *)FileName, FileNameLength,
                    (const USHORT *)((PUCHAR)CurrentFn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE)),
                    CurrentFn->FileNameLength,
                    TRUE,
                    DevExt->UpcaseTable,
                    DevExt->UpcaseTableLength))
            {
                Found = TRUE;
                break;
            }
        }

        CurrentOffset += CurrentEntry->Length;
    }

    if (Found)
    {
        ULONG NewAttrLength;
        ULONG NewValueLength;
        NTSTATUS ResizeStatus;

        /* Remove the entry: shift tail within the attribute value. */
        EntryLength = CurrentEntry->Length;
        TailLength = IndexEntriesEnd - (CurrentOffset + EntryLength);

        if (TailLength > 0)
        {
            RtlMoveMemory(IndexBase + CurrentOffset,
                          IndexBase + CurrentOffset + EntryLength,
                          TailLength);
        }

        /* Update the index header in place -- value is now shorter. */
        IndexHeader->IndexLength -= EntryLength;
        IndexHeader->AllocatedSize -= EntryLength;

        /* Compute the new attribute value length. */
        NewValueLength = IndexRootAttr->Data.Resident.ValueLength - EntryLength;
        NewAttrLength = ROUND_UP(
            IndexRootAttr->Data.Resident.ValueOffset + NewValueLength, 8);

        /* Use the generic resize helper so following attributes move up
         * correctly (otherwise $INDEX_ALLOCATION / $BITMAP that sit after
         * $INDEX_ROOT end up at the wrong offset). */
        ResizeStatus = NtfslxResizeAttributeRecord(DirRecord, IndexRootAttr,
                                                   NewAttrLength);
        if (!NT_SUCCESS(ResizeStatus))
        {
            NTFSDBG("ntfslx: IndexRemove: ResizeAttributeRecord to %lu failed 0x%08lx\n",
                     NewAttrLength, ResizeStatus);
            ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
            return ResizeStatus;
        }

        /* After resize the pointer is still valid, but ValueLength needs to
         * be updated to reflect the shrink. */
        IndexRootAttr->Data.Resident.ValueLength = NewValueLength;

        NTFSDBG("ntfslx: IndexRemove dir=%I64u name='%.*S' removed from resident root (%lu bytes), new attr size=%lu\n",
                 DirectoryMftIndex, FileNameLength, FileName, EntryLength, NewAttrLength);

        Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                      DevExt->MftRunlist, DirectoryMftIndex, DirRecord);
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
        return Status;
    }

    /* Not in resident root — fall through to $INDEX_ALLOCATION scan. */
    NTFSDBG("ntfslx: IndexRemove dir=%I64u name='%.*S' not in resident root, trying allocation\n",
             DirectoryMftIndex, FileNameLength, FileName);
    Status = NtfslxIndexAllocationRemove(DevExt, DirRecord, FileName, FileNameLength);
    NTFSDBG("ntfslx: IndexRemove dir=%I64u name='%.*S' allocation result=0x%08lx\n",
             DirectoryMftIndex, FileNameLength, FileName, Status);
    ExFreePoolWithTag(DirRecord, NTFSLX_TAG);
    return Status;
}
