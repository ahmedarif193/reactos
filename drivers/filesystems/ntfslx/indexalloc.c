/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Large-directory $INDEX_ALLOCATION / INDX traversal helpers
 *
 * Integration note:
 * - This module is intentionally self-contained and is not wired into CMake
 *   or ntfslx.h yet.
 * - Future directory-query code can build a runlist from the nonresident
 *   $INDEX_ALLOCATION attribute, read and validate INDX blocks, and recurse
 *   through child subnodes using the helpers below.
 */

#include "ntfslx.h"

#include <debug.h>

#define NTFSLX_INDEX_ALLOCATION_TAG 'aIxN'

#define NTFSLX_ATTRIBUTE_INDEX_ALLOCATION 0x000000A0UL

#define NTFSLX_INDEX_ENTRY_NODE 0x0001
#define NTFSLX_INDEX_ENTRY_END 0x0002

#include <pshpack1.h>
typedef struct _NTFSLX_INDEX_HEADER_ATTRIBUTE
{
    ULONG EntriesOffset;
    ULONG IndexLength;
    ULONG AllocatedSize;
    UCHAR Flags;
    UCHAR Reserved[3];
} NTFSLX_INDEX_HEADER_ATTRIBUTE, *PNTFSLX_INDEX_HEADER_ATTRIBUTE;

typedef struct _NTFSLX_INDEX_ROOT_ATTRIBUTE
{
    ULONG Type;
    ULONG CollationRule;
    ULONG IndexBlockSize;
    UCHAR ClustersPerIndexBlock;
    UCHAR Reserved[3];
    NTFSLX_INDEX_HEADER_ATTRIBUTE Header;
} NTFSLX_INDEX_ROOT_ATTRIBUTE, *PNTFSLX_INDEX_ROOT_ATTRIBUTE;

typedef struct _NTFSLX_INDEX_BUFFER
{
    NTFSLX_RECORD_HEADER Ntfs;
    ULONGLONG Lsn;
    ULONGLONG Vcn;
    NTFSLX_INDEX_HEADER_ATTRIBUTE Header;
} NTFSLX_INDEX_BUFFER, *PNTFSLX_INDEX_BUFFER;

typedef struct _NTFSLX_INDEX_ENTRY_ATTRIBUTE
{
    ULONGLONG FileReference;
    USHORT Length;
    USHORT KeyLength;
    USHORT Flags;
    USHORT Reserved;
} NTFSLX_INDEX_ENTRY_ATTRIBUTE, *PNTFSLX_INDEX_ENTRY_ATTRIBUTE;

typedef struct _NTFSLX_FILENAME_ATTRIBUTE
{
    ULONGLONG ParentDirectory;
    ULONGLONG CreationTime;
    ULONGLONG LastDataChangeTime;
    ULONGLONG LastMftChangeTime;
    ULONGLONG LastAccessTime;
    ULONGLONG AllocatedSize;
    ULONGLONG DataSize;
    ULONG FileAttributes;
    union
    {
        struct
        {
            USHORT PackedEaSize;
            USHORT Reserved;
        } EaInfo;
        ULONG ReparseTag;
    } Extended;
    UCHAR FileNameLength;
    UCHAR FileNameType;
    WCHAR FileName[1];
} NTFSLX_FILENAME_ATTRIBUTE, *PNTFSLX_FILENAME_ATTRIBUTE;
#include <poppack.h>

C_ASSERT(sizeof(NTFSLX_INDEX_HEADER_ATTRIBUTE) == 16);
C_ASSERT(sizeof(NTFSLX_INDEX_ROOT_ATTRIBUTE) == 32);
C_ASSERT(sizeof(NTFSLX_INDEX_BUFFER) == 40);
C_ASSERT(sizeof(NTFSLX_INDEX_ENTRY_ATTRIBUTE) == 16);

typedef struct _NTFSLX_INDEX_TRAVERSAL_CONTEXT
{
    PDEVICE_OBJECT StorageDevice;
    PNTFSLX_VOLUME_INFO VolumeInfo;
    PNTFSLX_RUNLIST_ELEMENT Runlist;
    PNTFSLX_DIR_ENUM_CALLBACK Callback;
    PVOID CallbackContext;
    ULONG IndexBlockSize;
    PULONGLONG VisitedVcns;
    ULONG VisitedCount;
    ULONG VisitedCapacity;
} NTFSLX_INDEX_TRAVERSAL_CONTEXT, *PNTFSLX_INDEX_TRAVERSAL_CONTEXT;

static NTSTATUS NtfslxEnumerateIndexAllocationBlock(
    _Inout_ PNTFSLX_INDEX_TRAVERSAL_CONTEXT Context,
    _In_ ULONGLONG Vcn);

static NTSTATUS
NtfslxEnsureRunlistCapacity(
    _Inout_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Inout_ PULONG Capacity,
    _In_ ULONG RequiredCount)
{
    PNTFSLX_RUNLIST_ELEMENT NewRunlist;
    ULONG NewCapacity;

    ASSERT(Runlist != NULL);
    ASSERT(Capacity != NULL);

    if (RequiredCount <= *Capacity)
    {
        return STATUS_SUCCESS;
    }

    NewCapacity = (*Capacity != 0) ? *Capacity : 16;
    while (NewCapacity < RequiredCount)
    {
        if (NewCapacity > (MAXULONG / 2))
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        NewCapacity *= 2;
    }

    NewRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                       NewCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                       NTFSLX_INDEX_ALLOCATION_TAG);
    if (NewRunlist == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(NewRunlist, NewCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT));
    if (*Runlist != NULL && *Capacity != 0)
    {
        RtlCopyMemory(NewRunlist,
                      *Runlist,
                      *Capacity * sizeof(NTFSLX_RUNLIST_ELEMENT));
        ExFreePoolWithTag(*Runlist, NTFSLX_INDEX_ALLOCATION_TAG);
    }

    *Runlist = NewRunlist;
    *Capacity = NewCapacity;
    ASSERT(*Capacity >= RequiredCount);
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfslxApplyIndexMstFixup(
    _Inout_ PNTFSLX_RECORD_HEADER Record,
    _In_ ULONG RecordSize,
    _In_ ULONG SectorSize)
{
    USHORT UsaOffset;
    USHORT UsaCount;
    ULONG UsaSize;
    PUSHORT UsaEntry;
    PUSHORT DataEntry;
    USHORT UpdateSequenceNumber;
    ULONG Index;
    ULONG SectorCount;

    ASSERT(Record != NULL);
    ASSERT(SectorSize != 0);
    ASSERT(RecordSize != 0);

    if (Record == NULL || SectorSize == 0 || RecordSize == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((RecordSize % SectorSize) != 0)
    {
        NTFSDBG("ntfslx: INDX MST invalid sizes RecordSize=%lu SectorSize=%lu\n",
                RecordSize,
                SectorSize);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    SectorCount = RecordSize / SectorSize;
    if (SectorCount == 0)
    {
        NTFSDBG("ntfslx: INDX MST zero sector count RecordSize=%lu SectorSize=%lu\n",
                RecordSize,
                SectorSize);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    UsaOffset = Record->UsaOffset;
    UsaCount = Record->UsaCount;
    if (UsaCount == 0)
    {
        NTFSDBG("ntfslx: INDX MST zero USA count UsaOffset=%hu RecordSize=%lu\n",
                UsaOffset,
                RecordSize);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    UsaSize = (ULONG)UsaCount * sizeof(USHORT);
    if ((UsaOffset & 1) != 0 ||
        UsaOffset > RecordSize ||
        UsaSize > RecordSize - UsaOffset ||
        UsaCount != (USHORT)(SectorCount + 1))
    {
        NTFSDBG("ntfslx: INDX MST invalid USA layout UsaOffset=%hu UsaCount=%hu UsaSize=%lu SectorCount=%lu RecordSize=%lu\n",
                UsaOffset,
                UsaCount,
                UsaSize,
                SectorCount,
                RecordSize);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    UsaEntry = (PUSHORT)((PUCHAR)Record + UsaOffset);
    UpdateSequenceNumber = *UsaEntry;
    DataEntry = (PUSHORT)((PUCHAR)Record + SectorSize - sizeof(USHORT));

    for (Index = 0; Index < SectorCount; ++Index)
    {
        if (*DataEntry != UpdateSequenceNumber)
        {
            NTFSDBG("ntfslx: INDX MST sequence mismatch sector=%lu expected=0x%04x actual=0x%04x\n",
                    Index,
                    UpdateSequenceNumber,
                    *DataEntry);
            Record->Magic = NTFSLX_RECORD_MAGIC_BAAD;
            return STATUS_FILE_CORRUPT_ERROR;
        }

        DataEntry = (PUSHORT)((PUCHAR)DataEntry + SectorSize);
    }

    DataEntry = (PUSHORT)((PUCHAR)Record + SectorSize - sizeof(USHORT));
    for (Index = 0; Index < SectorCount; ++Index)
    {
        *DataEntry = *(++UsaEntry);
        DataEntry = (PUSHORT)((PUCHAR)DataEntry + SectorSize);
    }

    ASSERT(Record->Magic == NTFSLX_RECORD_MAGIC_INDX ||
           Record->Magic == NTFSLX_RECORD_MAGIC_BAAD);
    return STATUS_SUCCESS;
}

static BOOLEAN
NtfslxIndexEntryHasChildNode(
    _In_ const NTFSLX_INDEX_ENTRY_ATTRIBUTE *Entry)
{
    return BooleanFlagOn(Entry->Flags, NTFSLX_INDEX_ENTRY_NODE);
}

static BOOLEAN
NtfslxIndexEntryIsEnd(
    _In_ const NTFSLX_INDEX_ENTRY_ATTRIBUTE *Entry)
{
    return BooleanFlagOn(Entry->Flags, NTFSLX_INDEX_ENTRY_END);
}

static BOOLEAN
NtfslxIndexEntryLooksValid(
    _In_reads_bytes_(RemainingLength) const NTFSLX_INDEX_ENTRY_ATTRIBUTE *Entry,
    _In_ ULONG RemainingLength,
    _Out_ PULONG KeyLength)
{
    ULONG MinimumLength;
    ULONG HeaderLength;

    ASSERT(Entry != NULL);
    ASSERT(KeyLength != NULL);

    HeaderLength = sizeof(*Entry);
    if (RemainingLength < HeaderLength ||
        Entry->Length < HeaderLength ||
        Entry->Length > RemainingLength ||
        (Entry->Length & 7) != 0)
    {
        return FALSE;
    }

    if (NtfslxIndexEntryIsEnd(Entry))
    {
        MinimumLength = HeaderLength;
        if (NtfslxIndexEntryHasChildNode(Entry))
        {
            MinimumLength += sizeof(ULONGLONG);
        }

        if (Entry->Length != MinimumLength ||
            Entry->KeyLength != 0)
        {
            return FALSE;
        }

        *KeyLength = 0;
        return TRUE;
    }

    if (Entry->KeyLength < FIELD_OFFSET(NTFSLX_FILENAME_ATTRIBUTE, FileName) ||
        Entry->KeyLength > Entry->Length - HeaderLength)
    {
        return FALSE;
    }

    if (((Entry->KeyLength - FIELD_OFFSET(NTFSLX_FILENAME_ATTRIBUTE, FileName)) & 1) != 0)
    {
        return FALSE;
    }

    MinimumLength = HeaderLength + Entry->KeyLength;
    if (NtfslxIndexEntryHasChildNode(Entry))
    {
        MinimumLength += sizeof(ULONGLONG);
    }

    if (Entry->Length < MinimumLength)
    {
        return FALSE;
    }

    *KeyLength = Entry->KeyLength;
    return TRUE;
}

static NTSTATUS
NtfslxIndexEntryToDirEntry(
    _In_reads_bytes_(RemainingLength) const NTFSLX_INDEX_ENTRY_ATTRIBUTE *Entry,
    _In_ ULONG RemainingLength,
    _Out_ PNTFSLX_DIR_ENTRY DirEntry)
{
    const NTFSLX_FILENAME_ATTRIBUTE *FileNameAttr;
    ULONG NameLength;
    NTSTATUS Status;

    if (NtfslxIndexEntryIsEnd(Entry))
    {
        return STATUS_NO_MORE_FILES;
    }

    if (!NtfslxIndexEntryLooksValid(Entry, RemainingLength, &NameLength))
    {
        NTFSDBG("ntfslx: invalid index entry layout Remaining=%lu EntryLength=%hu KeyLength=%hu Flags=0x%04x\n",
                RemainingLength,
                Entry->Length,
                Entry->KeyLength,
                Entry->Flags);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    FileNameAttr = (const NTFSLX_FILENAME_ATTRIBUTE *)
        ((const UCHAR *)Entry + sizeof(*Entry));
    if (NameLength < FIELD_OFFSET(NTFSLX_FILENAME_ATTRIBUTE, FileName))
    {
        NTFSDBG("ntfslx: invalid index filename attribute length NameLength=%lu Minimum=%lu\n",
                NameLength,
                (ULONG)FIELD_OFFSET(NTFSLX_FILENAME_ATTRIBUTE, FileName));
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (FileNameAttr->FileNameLength > RTL_NUMBER_OF(DirEntry->FileName))
    {
        NTFSDBG("ntfslx: invalid filename length in index entry NameChars=%u MaxChars=%lu\n",
                FileNameAttr->FileNameLength,
                (ULONG)RTL_NUMBER_OF(DirEntry->FileName));
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (FIELD_OFFSET(NTFSLX_FILENAME_ATTRIBUTE, FileName) +
        (FileNameAttr->FileNameLength * sizeof(WCHAR)) != NameLength)
    {
        NTFSDBG("ntfslx: mismatched filename payload length NameChars=%u NameLength=%lu\n",
                FileNameAttr->FileNameLength,
                NameLength);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    RtlZeroMemory(DirEntry, sizeof(*DirEntry));
    DirEntry->FileReference = Entry->FileReference;
    DirEntry->ParentReference = FileNameAttr->ParentDirectory;
    DirEntry->CreationTime.QuadPart = (LONGLONG)FileNameAttr->CreationTime;
    DirEntry->LastDataChangeTime.QuadPart = (LONGLONG)FileNameAttr->LastDataChangeTime;
    DirEntry->LastMftChangeTime.QuadPart = (LONGLONG)FileNameAttr->LastMftChangeTime;
    DirEntry->LastAccessTime.QuadPart = (LONGLONG)FileNameAttr->LastAccessTime;
    DirEntry->AllocatedSize = FileNameAttr->AllocatedSize;
    DirEntry->DataSize = FileNameAttr->DataSize;
    DirEntry->FileAttributes = FileNameAttr->FileAttributes;
    DirEntry->FileNameType = FileNameAttr->FileNameType;
    DirEntry->FileNameLength = FileNameAttr->FileNameLength;
    DirEntry->IndexEntryFlags = Entry->Flags;
    DirEntry->HasChildNode = NtfslxIndexEntryHasChildNode(Entry);
    DirEntry->IndexEntryOffset = 0;
    DirEntry->ChildVcn = 0;

    if (DirEntry->FileNameLength != 0)
    {
        RtlCopyMemory(DirEntry->FileName,
                      FileNameAttr->FileName,
                      DirEntry->FileNameLength * sizeof(WCHAR));
    }

    if (DirEntry->HasChildNode)
    {
        DirEntry->ChildVcn = *(const ULONGLONG *)
            ((const UCHAR *)Entry + Entry->Length - sizeof(ULONGLONG));
    }

    ASSERT(DirEntry->FileNameLength <= RTL_NUMBER_OF(DirEntry->FileName));
    ASSERT(!DirEntry->HasChildNode || Entry->Length >= sizeof(*Entry) + sizeof(ULONGLONG));
    Status = STATUS_SUCCESS;
    return Status;
}

static NTSTATUS
NtfslxValidateIndexEntryRange(
    _In_reads_bytes_(NodeLength) const UCHAR *NodeBase,
    _In_ ULONG NodeLength,
    _In_ ULONG EntriesOffset,
    _In_ ULONG IndexLength)
{
    ULONG Offset;
    ULONG KeyLength;
    const NTFSLX_INDEX_ENTRY_ATTRIBUTE *IndexEntry;

    ASSERT(NodeBase != NULL);
    ASSERT((EntriesOffset & 7) == 0);
    ASSERT((IndexLength & 7) == 0);

    if (NodeBase == NULL ||
        (EntriesOffset & 7) != 0 ||
        (IndexLength & 7) != 0 ||
        EntriesOffset > IndexLength ||
        IndexLength > NodeLength)
    {
        NTFSDBG("ntfslx: invalid index entry range NodeLength=%lu EntriesOffset=%lu IndexLength=%lu\n",
                NodeLength,
                EntriesOffset,
                IndexLength);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Offset = EntriesOffset;
    while (Offset < IndexLength)
    {
        IndexEntry = (const NTFSLX_INDEX_ENTRY_ATTRIBUTE *)(NodeBase + Offset);
        if (!NtfslxIndexEntryLooksValid(IndexEntry, IndexLength - Offset, &KeyLength))
        {
            NTFSDBG("ntfslx: malformed index entry at offset=%lu remaining=%lu length=%hu key=%hu flags=0x%04x\n",
                    Offset,
                    IndexLength - Offset,
                    IndexEntry->Length,
                    IndexEntry->KeyLength,
                    IndexEntry->Flags);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (NtfslxIndexEntryIsEnd(IndexEntry))
        {
            if ((Offset + IndexEntry->Length) != IndexLength)
            {
                NTFSDBG("ntfslx: index end entry not terminal offset=%lu entryLength=%hu indexLength=%lu\n",
                        Offset,
                        IndexEntry->Length,
                        IndexLength);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            return STATUS_SUCCESS;
        }

        if (IndexEntry->Length == 0)
        {
            NTFSDBG("ntfslx: zero-length index entry at offset=%lu indexLength=%lu\n",
                    Offset,
                    IndexLength);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Offset += IndexEntry->Length;
    }

    return STATUS_FILE_CORRUPT_ERROR;
}

/* NtfslxRunlistVcnToLcn and NtfslxReadMappedAttributeData are now provided
 * by metadata.c and declared in ntfslx.h */

static NTSTATUS
NtfslxIndexDecodeMappingPairs(
    _In_ PNTFSLX_ATTR_RECORD Attribute,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Out_ PULONG RunlistCount)
{
    PNTFSLX_RUNLIST_ELEMENT OutputRunlist;
    ULONG Capacity;
    ULONG Count;
    PUCHAR Buffer;
    PUCHAR AttributeEnd;
    LONGLONG Vcn;
    LONGLONG Lcn;
    LONGLONG Delta;
    ULONGLONG HighestVcn;
    UCHAR Header;
    UCHAR LengthBytes;
    UCHAR LcnBytes;
    ULONG Index;
    NTSTATUS Status;

    ASSERT(Runlist != NULL);
    ASSERT(RunlistCount != NULL);

    if (Runlist == NULL || RunlistCount == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *Runlist = NULL;
    *RunlistCount = 0;

    if (Attribute == NULL || Attribute->NonResident == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Attribute->Length < sizeof(*Attribute) ||
        Attribute->Data.NonResident.MappingPairsOffset < sizeof(*Attribute) ||
        Attribute->Data.NonResident.MappingPairsOffset >= Attribute->Length ||
        Attribute->Data.NonResident.HighestVcn < Attribute->Data.NonResident.LowestVcn)
    {
        NTFSDBG("ntfslx: invalid $INDEX_ALLOCATION attribute layout AttrLen=%lu MappingPairsOffset=%hu LowestVcn=%I64u HighestVcn=%I64u\n",
                Attribute->Length,
                Attribute->Data.NonResident.MappingPairsOffset,
                Attribute->Data.NonResident.LowestVcn,
                Attribute->Data.NonResident.HighestVcn);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Capacity = 16;
    Count = 0;
    OutputRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                          Capacity * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                          NTFSLX_INDEX_ALLOCATION_TAG);
    if (OutputRunlist == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(OutputRunlist, Capacity * sizeof(NTFSLX_RUNLIST_ELEMENT));

    Vcn = (LONGLONG)Attribute->Data.NonResident.LowestVcn;
    Lcn = 0;

    if (Vcn > 0)
    {
        OutputRunlist[Count].Vcn = 0;
        OutputRunlist[Count].Lcn = NTFSLX_LCN_RL_NOT_MAPPED;
        OutputRunlist[Count].Length = Vcn;
        Count++;
    }

    Buffer = (PUCHAR)Attribute + Attribute->Data.NonResident.MappingPairsOffset;
    AttributeEnd = (PUCHAR)Attribute + Attribute->Length;
    if (Buffer < (PUCHAR)Attribute || Buffer >= AttributeEnd)
    {
        NTFSDBG("ntfslx: invalid mapping-pairs pointer Attr=%p Buffer=%p End=%p\n",
                Attribute,
                Buffer,
                AttributeEnd);
        ExFreePoolWithTag(OutputRunlist, NTFSLX_INDEX_ALLOCATION_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    while (Buffer < AttributeEnd && *Buffer != 0)
    {
        Status = NtfslxEnsureRunlistCapacity(&OutputRunlist, &Capacity, Count + 2);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(OutputRunlist, NTFSLX_INDEX_ALLOCATION_TAG);
            return Status;
        }

        Header = *Buffer++;
        LengthBytes = Header & 0x0F;
        LcnBytes = (Header >> 4) & 0x0F;
        if (LengthBytes == 0 ||
            LengthBytes > sizeof(LONGLONG) ||
            LcnBytes > sizeof(LONGLONG) ||
            Buffer > AttributeEnd ||
            (ULONG)(AttributeEnd - Buffer) < (ULONG)(LengthBytes + LcnBytes))
        {
            NTFSDBG("ntfslx: invalid mapping-pairs header Header=0x%02x LengthBytes=%u LcnBytes=%u Remaining=%lu\n",
                    Header,
                    LengthBytes,
                    LcnBytes,
                    (ULONG)(AttributeEnd - Buffer));
            ExFreePoolWithTag(OutputRunlist, NTFSLX_INDEX_ALLOCATION_TAG);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Delta = (CHAR)Buffer[LengthBytes - 1];
        for (Index = LengthBytes - 1; Index != 0; --Index)
        {
            Delta = (Delta << 8) + Buffer[Index - 1];
        }

        if (Delta <= 0)
        {
            NTFSDBG("ntfslx: invalid mapping-pairs run length Delta=%I64d\n",
                    Delta);
            ExFreePoolWithTag(OutputRunlist, NTFSLX_INDEX_ALLOCATION_TAG);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Vcn > (LONGLONG)MAXLONGLONG - Delta)
        {
            NTFSDBG("ntfslx: mapping-pairs VCN overflow Vcn=%I64d Delta=%I64d\n",
                    Vcn,
                    Delta);
            ExFreePoolWithTag(OutputRunlist, NTFSLX_INDEX_ALLOCATION_TAG);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        OutputRunlist[Count].Vcn = Vcn;
        OutputRunlist[Count].Length = Delta;
        Vcn += Delta;

        if (LcnBytes == 0)
        {
            OutputRunlist[Count].Lcn = NTFSLX_LCN_HOLE;
        }
        else
        {
            Delta = (CHAR)Buffer[LengthBytes + LcnBytes - 1];
            for (Index = LengthBytes + LcnBytes - 1; Index > LengthBytes; --Index)
            {
                Delta = (Delta << 8) + Buffer[Index - 1];
            }

            Lcn += Delta;
            if (Lcn < -1)
            {
                NTFSDBG("ntfslx: invalid mapping-pairs LCN result Lcn=%I64d Delta=%I64d\n",
                        Lcn,
                        Delta);
                ExFreePoolWithTag(OutputRunlist, NTFSLX_INDEX_ALLOCATION_TAG);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            OutputRunlist[Count].Lcn = Lcn;
        }

        Buffer += LengthBytes + LcnBytes;
        Count++;
    }

    if (Buffer >= AttributeEnd)
    {
        NTFSDBG("ntfslx: mapping-pairs stream not terminated cleanly Buffer=%p End=%p\n",
                Buffer,
                AttributeEnd);
        ExFreePoolWithTag(OutputRunlist, NTFSLX_INDEX_ALLOCATION_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    HighestVcn = Attribute->Data.NonResident.HighestVcn;
    if (Count == 0 || Vcn == 0 || Vcn - 1 != (LONGLONG)HighestVcn)
    {
        NTFSDBG("ntfslx: mapping-pairs VCN coverage mismatch Count=%lu FinalVcn=%I64d HighestVcn=%I64u\n",
                Count,
                Vcn,
                HighestVcn);
        ExFreePoolWithTag(OutputRunlist, NTFSLX_INDEX_ALLOCATION_TAG);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    OutputRunlist[Count].Vcn = Vcn;
    OutputRunlist[Count].Lcn = NTFSLX_LCN_ENOENT;
    OutputRunlist[Count].Length = 0;
    Count++;

    *Runlist = OutputRunlist;
    *RunlistCount = Count;
    return STATUS_SUCCESS;
}

static BOOLEAN
NtfslxIndexVisitedVcn(
    _In_ PNTFSLX_INDEX_TRAVERSAL_CONTEXT Context,
    _In_ ULONGLONG Vcn)
{
    ULONG Index;

    ASSERT(Context != NULL);
    ASSERT(Context->VisitedCount <= Context->VisitedCapacity);

    for (Index = 0; Index < Context->VisitedCount; ++Index)
    {
        if (Context->VisitedVcns[Index] == Vcn)
        {
            return TRUE;
        }
    }

    return FALSE;
}

static NTSTATUS
NtfslxIndexRememberVcn(
    _Inout_ PNTFSLX_INDEX_TRAVERSAL_CONTEXT Context,
    _In_ ULONGLONG Vcn)
{
    PULONGLONG NewVisited;
    ULONG NewCapacity;

    ASSERT(Context != NULL);
    ASSERT(Context->VisitedCount <= Context->VisitedCapacity);

    if (NtfslxIndexVisitedVcn(Context, Vcn))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Context->VisitedCount < Context->VisitedCapacity)
    {
        Context->VisitedVcns[Context->VisitedCount++] = Vcn;
        return STATUS_SUCCESS;
    }

    NewCapacity = (Context->VisitedCapacity != 0) ? (Context->VisitedCapacity * 2) : 16;
    if (NewCapacity > (MAXULONG / sizeof(ULONGLONG)))
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NewVisited = ExAllocatePoolWithTag(NonPagedPool,
                                       NewCapacity * sizeof(ULONGLONG),
                                       NTFSLX_INDEX_ALLOCATION_TAG);
    if (NewVisited == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(NewVisited, NewCapacity * sizeof(ULONGLONG));
    if (Context->VisitedVcns != NULL && Context->VisitedCount != 0)
    {
        RtlCopyMemory(NewVisited,
                      Context->VisitedVcns,
                      Context->VisitedCount * sizeof(ULONGLONG));
        ExFreePoolWithTag(Context->VisitedVcns, NTFSLX_INDEX_ALLOCATION_TAG);
    }

    Context->VisitedVcns = NewVisited;
    Context->VisitedCapacity = NewCapacity;
    Context->VisitedVcns[Context->VisitedCount++] = Vcn;
    ASSERT(Context->VisitedCount <= Context->VisitedCapacity);
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfslxValidateResidentIndexRoot(
    _In_reads_bytes_(RootLength) const NTFSLX_INDEX_ROOT_ATTRIBUTE *IndexRoot,
    _In_ ULONG RootLength,
    _Out_ PULONG EntriesOffset,
    _Out_ PULONG IndexLength)
{
    ULONG FirstEntryOffset;
    ULONG UsedLength;
    ULONG AllocatedSize;
    ULONG HeaderOffset;

    ASSERT(IndexRoot != NULL);
    ASSERT(EntriesOffset != NULL);
    ASSERT(IndexLength != NULL);

    if (IndexRoot == NULL || EntriesOffset == NULL || IndexLength == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (RootLength < sizeof(*IndexRoot) || IndexRoot->Type != NTFSLX_ATTRIBUTE_FILE_NAME)
    {
        NTFSDBG("ntfslx: invalid resident $INDEX_ROOT RootLength=%lu Type=0x%08lx ExpectedType=0x%08lx\n",
                RootLength,
                IndexRoot != NULL ? IndexRoot->Type : 0UL,
                NTFSLX_ATTRIBUTE_FILE_NAME);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    HeaderOffset = FIELD_OFFSET(NTFSLX_INDEX_ROOT_ATTRIBUTE, Header);
    FirstEntryOffset = IndexRoot->Header.EntriesOffset;
    UsedLength = IndexRoot->Header.IndexLength;
    AllocatedSize = IndexRoot->Header.AllocatedSize;

    if (FirstEntryOffset < sizeof(NTFSLX_INDEX_HEADER_ATTRIBUTE) ||
        (FirstEntryOffset & 7) != 0 ||
        FirstEntryOffset > UsedLength ||
        (UsedLength & 7) != 0 ||
        UsedLength > RootLength - HeaderOffset ||
        AllocatedSize > RootLength - HeaderOffset ||
        UsedLength > AllocatedSize)
    {
        NTFSDBG("ntfslx: invalid resident $INDEX_ROOT header FirstEntry=%lu Used=%lu Allocated=%lu RootLength=%lu HeaderOffset=%lu Flags=0x%02x\n",
                FirstEntryOffset,
                UsedLength,
                AllocatedSize,
                RootLength,
                HeaderOffset,
                IndexRoot->Header.Flags);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    *EntriesOffset = HeaderOffset + FirstEntryOffset;
    *IndexLength = HeaderOffset + UsedLength;
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfslxValidateIndexBufferHeader(
    _In_reads_bytes_(BufferLength) const NTFSLX_INDEX_BUFFER *IndexBuffer,
    _In_ ULONG BufferLength,
    _In_ ULONG SectorSize,
    _In_ ULONGLONG ExpectedVcn,
    _Out_ PULONG EntriesOffset,
    _Out_ PULONG IndexLength)
{
    ULONG FirstEntryOffset;
    ULONG UsedLength;
    ULONG AllocatedSize;
    ULONG HeaderOffset;

    ASSERT(IndexBuffer != NULL);
    ASSERT(EntriesOffset != NULL);
    ASSERT(IndexLength != NULL);

    if (IndexBuffer == NULL || EntriesOffset == NULL || IndexLength == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (BufferLength < sizeof(*IndexBuffer) ||
        IndexBuffer->Ntfs.Magic != NTFSLX_RECORD_MAGIC_INDX)
    {
        NTFSDBG("ntfslx: invalid INDX header BufferLength=%lu Magic=0x%08lx\n",
                BufferLength,
                IndexBuffer != NULL ? IndexBuffer->Ntfs.Magic : 0UL);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (IndexBuffer->Vcn != ExpectedVcn)
    {
        NTFSDBG("ntfslx: INDX VCN mismatch Expected=%I64u Actual=%I64u\n",
                ExpectedVcn,
                IndexBuffer->Vcn);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    HeaderOffset = FIELD_OFFSET(NTFSLX_INDEX_BUFFER, Header);
    FirstEntryOffset = IndexBuffer->Header.EntriesOffset;
    UsedLength = IndexBuffer->Header.IndexLength;
    AllocatedSize = IndexBuffer->Header.AllocatedSize;

    if (FirstEntryOffset < sizeof(NTFSLX_INDEX_HEADER_ATTRIBUTE) ||
        (FirstEntryOffset & 7) != 0 ||
        FirstEntryOffset > UsedLength ||
        (UsedLength & 7) != 0 ||
        UsedLength > BufferLength - HeaderOffset ||
        AllocatedSize != BufferLength - HeaderOffset ||
        UsedLength > AllocatedSize)
    {
        NTFSDBG("ntfslx: invalid INDX entry header FirstEntry=%lu Used=%lu Allocated=%lu BufferLength=%lu HeaderOffset=%lu Flags=0x%02x\n",
                FirstEntryOffset,
                UsedLength,
                AllocatedSize,
                BufferLength,
                HeaderOffset,
                IndexBuffer->Header.Flags);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (BufferLength % SectorSize != 0)
    {
        NTFSDBG("ntfslx: INDX buffer size is not sector aligned BufferLength=%lu SectorSize=%lu\n",
                BufferLength,
                SectorSize);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    *EntriesOffset = HeaderOffset + FirstEntryOffset;
    *IndexLength = HeaderOffset + UsedLength;
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfslxWalkIndexEntryRange(
    _Inout_ PNTFSLX_INDEX_TRAVERSAL_CONTEXT Context,
    _In_reads_bytes_(NodeLength) const UCHAR *NodeBase,
    _In_ ULONG NodeLength,
    _In_ ULONG EntriesOffset,
    _In_ ULONG IndexLength)
{
    ULONG Offset;
    ULONG KeyLength;
    ULONG Remaining;
    NTFSLX_DIR_ENTRY Entry;
    NTSTATUS Status;
    const NTFSLX_INDEX_ENTRY_ATTRIBUTE *IndexEntry;

    ASSERT(Context != NULL);
    ASSERT(Context->Callback != NULL);

    Offset = EntriesOffset;
    if ((Offset & 7) != 0 ||
        (IndexLength & 7) != 0 ||
        Offset > IndexLength ||
        IndexLength > NodeLength)
    {
        NTFSDBG("ntfslx: invalid walk range NodeLength=%lu Offset=%lu IndexLength=%lu\n",
                NodeLength,
                Offset,
                IndexLength);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    while (Offset < IndexLength)
    {
        IndexEntry = (const NTFSLX_INDEX_ENTRY_ATTRIBUTE *)(NodeBase + Offset);
        Remaining = IndexLength - Offset;
        if (!NtfslxIndexEntryLooksValid(IndexEntry, Remaining, &KeyLength))
        {
            NTFSDBG("ntfslx: walk saw malformed entry Offset=%lu Remaining=%lu Length=%hu Key=%hu Flags=0x%04x\n",
                    Offset,
                    Remaining,
                    IndexEntry->Length,
                    IndexEntry->KeyLength,
                    IndexEntry->Flags);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (NtfslxIndexEntryIsEnd(IndexEntry))
        {
            if ((Offset + IndexEntry->Length) != IndexLength)
            {
                NTFSDBG("ntfslx: walk end entry not terminal Offset=%lu EntryLength=%hu IndexLength=%lu\n",
                        Offset,
                        IndexEntry->Length,
                        IndexLength);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            if (NtfslxIndexEntryHasChildNode(IndexEntry))
            {
                Entry.HasChildNode = TRUE;
                Entry.ChildVcn = *(const ULONGLONG *)
                    ((const UCHAR *)IndexEntry + IndexEntry->Length - sizeof(ULONGLONG));

                if (Context->Runlist == NULL)
                {
                    NTFSDBG("ntfslx: terminal child VCN without runlist ChildVcn=%I64u\n",
                            Entry.ChildVcn);
                    return STATUS_INVALID_PARAMETER;
                }

                if (NtfslxIndexVisitedVcn(Context, Entry.ChildVcn))
                {
                    NTFSDBG("ntfslx: duplicate terminal child VCN encountered ChildVcn=%I64u\n",
                            Entry.ChildVcn);
                    return STATUS_FILE_CORRUPT_ERROR;
                }

                Status = NtfslxIndexRememberVcn(Context, Entry.ChildVcn);
                if (!NT_SUCCESS(Status))
                {
                    return Status;
                }

                Status = NtfslxEnumerateIndexAllocationBlock(Context, Entry.ChildVcn);
                if (!NT_SUCCESS(Status))
                {
                    NTFSDBG("ntfslx: failed to enumerate terminal child INDX block ChildVcn=%I64u status=0x%08lx\n",
                            Entry.ChildVcn,
                            Status);
                    return Status;
                }
            }

            return STATUS_SUCCESS;
        }

        Status = NtfslxIndexEntryToDirEntry(IndexEntry, Remaining, &Entry);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        Entry.IndexEntryOffset = Offset;
        Status = Context->Callback(&Entry, Context->CallbackContext);
        if (Status == STATUS_NO_MORE_FILES)
        {
            return STATUS_SUCCESS;
        }

        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (Entry.HasChildNode)
        {
            if (Context->Runlist == NULL)
            {
                NTFSDBG("ntfslx: child VCN without runlist ChildVcn=%I64u\n",
                        Entry.ChildVcn);
                return STATUS_INVALID_PARAMETER;
            }

            if (NtfslxIndexVisitedVcn(Context, Entry.ChildVcn))
            {
                NTFSDBG("ntfslx: duplicate child VCN encountered ChildVcn=%I64u\n",
                        Entry.ChildVcn);
                return STATUS_FILE_CORRUPT_ERROR;
            }

            Status = NtfslxIndexRememberVcn(Context, Entry.ChildVcn);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }

            Status = NtfslxEnumerateIndexAllocationBlock(Context, Entry.ChildVcn);
            if (!NT_SUCCESS(Status))
            {
                NTFSDBG("ntfslx: failed to enumerate child INDX block ChildVcn=%I64u status=0x%08lx\n",
                        Entry.ChildVcn,
                        Status);
                return Status;
            }
        }

        if (IndexEntry->Length == 0)
        {
            NTFSDBG("ntfslx: walk saw zero-length entry Offset=%lu IndexLength=%lu\n",
                    Offset,
                    IndexLength);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Offset += IndexEntry->Length;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
NtfslxEnumerateIndexAllocationBlock(
    _Inout_ PNTFSLX_INDEX_TRAVERSAL_CONTEXT Context,
    _In_ ULONGLONG Vcn)
{
    PNTFSLX_INDEX_BUFFER IndexBuffer;
    NTSTATUS Status;
    ULONG EntriesOffset;
    ULONG IndexLength;

    ASSERT(Context != NULL);
    ASSERT(Context->StorageDevice != NULL);
    ASSERT(Context->VolumeInfo != NULL);
    ASSERT(Context->Callback != NULL);

    if (Context == NULL || Context->StorageDevice == NULL || Context->VolumeInfo == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Context->IndexBlockSize != Context->VolumeInfo->BytesPerIndexRecord)
    {
        NTFSDBG("ntfslx: INDX block size mismatch ContextSize=%lu VolumeSize=%lu\n",
                Context->IndexBlockSize,
                Context->VolumeInfo->BytesPerIndexRecord);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Vcn > (ULONGLONG)(MAXLONGLONG / Context->VolumeInfo->BytesPerCluster))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    IndexBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                        Context->IndexBlockSize,
                                        NTFSLX_INDEX_ALLOCATION_TAG);
    if (IndexBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMappedAttributeData(Context->StorageDevice,
                                           Context->VolumeInfo,
                                           Context->Runlist,
                                           Vcn * Context->VolumeInfo->BytesPerCluster,
                                           Context->IndexBlockSize,
                                           (PUCHAR)IndexBuffer);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: failed reading INDX block Vcn=%I64u status=0x%08lx\n",
                Vcn,
                Status);
        ExFreePoolWithTag(IndexBuffer, NTFSLX_INDEX_ALLOCATION_TAG);
        return Status;
    }

    Status = NtfslxValidateIndexBufferHeader(IndexBuffer,
                                             Context->IndexBlockSize,
                                             Context->VolumeInfo->BytesPerSector,
                                             Vcn,
                                             &EntriesOffset,
                                             &IndexLength);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: invalid INDX block header Vcn=%I64u status=0x%08lx\n",
                Vcn,
                Status);
        ExFreePoolWithTag(IndexBuffer, NTFSLX_INDEX_ALLOCATION_TAG);
        return Status;
    }

    Status = NtfslxApplyIndexMstFixup(&IndexBuffer->Ntfs,
                                      Context->IndexBlockSize,
                                      Context->VolumeInfo->BytesPerSector);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: INDX MST fixup failed Vcn=%I64u status=0x%08lx\n",
                Vcn,
                Status);
        ExFreePoolWithTag(IndexBuffer, NTFSLX_INDEX_ALLOCATION_TAG);
        return Status;
    }

    Status = NtfslxWalkIndexEntryRange(Context,
                                       (const UCHAR *)IndexBuffer,
                                       Context->IndexBlockSize,
                                       EntriesOffset,
                                       IndexLength);

    ExFreePoolWithTag(IndexBuffer, NTFSLX_INDEX_ALLOCATION_TAG);
    return Status;
}

NTSTATUS
NtfslxBuildIndexAllocationRunlist(
    _In_ PNTFSLX_ATTR_RECORD IndexAllocationAttribute,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Out_ PULONG RunlistCount)
{
    if (IndexAllocationAttribute == NULL ||
        IndexAllocationAttribute->Type != NTFSLX_ATTRIBUTE_INDEX_ALLOCATION)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ASSERT(Runlist != NULL);
    ASSERT(RunlistCount != NULL);
    return NtfslxIndexDecodeMappingPairs(IndexAllocationAttribute,
                                         Runlist,
                                         RunlistCount);
}

NTSTATUS
NtfslxReadIndexAllocationBlock(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT IndexAllocationRunlist,
    _In_ ULONGLONG Vcn,
    _Out_writes_bytes_(VolumeInfo->BytesPerIndexRecord) PVOID Buffer)
{
    NTSTATUS Status;

    ASSERT(StorageDevice != NULL);
    ASSERT(VolumeInfo != NULL);
    ASSERT(IndexAllocationRunlist != NULL);
    ASSERT(Buffer != NULL);

    if (StorageDevice == NULL || VolumeInfo == NULL || IndexAllocationRunlist == NULL || Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (VolumeInfo->BytesPerCluster == 0 ||
        VolumeInfo->BytesPerIndexRecord == 0 ||
        (VolumeInfo->BytesPerIndexRecord % VolumeInfo->BytesPerCluster) != 0)
    {
        NTFSDBG("ntfslx: invalid index-allocation read geometry Cluster=%lu IndexRecord=%lu\n",
                VolumeInfo->BytesPerCluster,
                VolumeInfo->BytesPerIndexRecord);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (Vcn > (ULONGLONG)(MAXLONGLONG / VolumeInfo->BytesPerCluster))
    {
        return STATUS_UNRECOGNIZED_VOLUME;
    }

    Status = NtfslxReadMappedAttributeData(StorageDevice,
                                          VolumeInfo,
                                          IndexAllocationRunlist,
                                          Vcn * VolumeInfo->BytesPerCluster,
                                          VolumeInfo->BytesPerIndexRecord,
                                          (PUCHAR)Buffer);
    return Status;
}

NTSTATUS
NtfslxValidateIndexAllocationBlock(
    _Inout_updates_bytes_(IndexBlockSize) PVOID Buffer,
    _In_ ULONG IndexBlockSize,
    _In_ ULONG SectorSize,
    _In_ ULONGLONG ExpectedVcn)
{
    PNTFSLX_INDEX_BUFFER IndexBuffer;
    NTSTATUS Status;
    ULONG EntriesOffset;
    ULONG IndexLength;

    ASSERT(Buffer != NULL);
    ASSERT(IndexBlockSize != 0);
    ASSERT(SectorSize != 0);

    if (Buffer == NULL || IndexBlockSize == 0 || SectorSize == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((IndexBlockSize % SectorSize) != 0)
    {
        NTFSDBG("ntfslx: invalid index-allocation block geometry IndexBlockSize=%lu SectorSize=%lu\n",
                IndexBlockSize,
                SectorSize);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexBuffer = (PNTFSLX_INDEX_BUFFER)Buffer;
    Status = NtfslxValidateIndexBufferHeader(IndexBuffer,
                                             IndexBlockSize,
                                             SectorSize,
                                             ExpectedVcn,
                                             &EntriesOffset,
                                             &IndexLength);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: NtfslxValidateIndexBufferHeader failed in NtfslxValidateIndexAllocationBlock status=0x%08lx ExpectedVcn=%I64u\n",
                Status,
                ExpectedVcn);
        return Status;
    }

    Status = NtfslxApplyIndexMstFixup(&IndexBuffer->Ntfs,
                                      IndexBlockSize,
                                      SectorSize);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: NtfslxApplyIndexMstFixup failed in NtfslxValidateIndexAllocationBlock status=0x%08lx ExpectedVcn=%I64u\n",
                Status,
                ExpectedVcn);
        return Status;
    }

    return NtfslxValidateIndexEntryRange((const UCHAR *)IndexBuffer,
                                         IndexBlockSize,
                                         EntriesOffset,
                                         IndexLength);
}

static NTSTATUS
NtfslxValidateIndexRoot(
    _In_reads_bytes_(RootLength) const NTFSLX_INDEX_ROOT_ATTRIBUTE *IndexRoot,
    _In_ ULONG RootLength,
    _In_ ULONG ExpectedIndexBlockSize,
    _Out_ PULONG EntriesOffset,
    _Out_ PULONG IndexLength)
{
    NTSTATUS Status;

    Status = NtfslxValidateResidentIndexRoot(IndexRoot,
                                             RootLength,
                                             EntriesOffset,
                                             IndexLength);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: invalid $INDEX_ROOT status=0x%08lx RootLength=%lu ExpectedIndexBlock=%lu\n",
                Status,
                RootLength,
                ExpectedIndexBlockSize);
        return Status;
    }

    if (IndexRoot->IndexBlockSize != ExpectedIndexBlockSize)
    {
        NTFSDBG("ntfslx: unexpected $INDEX_ROOT block size RootBlock=%lu Expected=%lu ClustersPerBlock=%u Flags=0x%02x\n",
                IndexRoot->IndexBlockSize,
                ExpectedIndexBlockSize,
                IndexRoot->ClustersPerIndexBlock,
                IndexRoot->Header.Flags);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
NtfslxEnumerateAllocationSubtree(
    _Inout_ PNTFSLX_INDEX_TRAVERSAL_CONTEXT Context,
    _In_reads_bytes_(RootLength) const NTFSLX_INDEX_ROOT_ATTRIBUTE *IndexRoot,
    _In_ ULONG RootLength,
    _In_ ULONG EntriesOffset,
    _In_ ULONG IndexLength)
{
    NTSTATUS Status;

    ASSERT(Context != NULL);
    ASSERT(IndexRoot != NULL);

    Status = NtfslxWalkIndexEntryRange(Context,
                                       (const UCHAR *)IndexRoot,
                                       RootLength,
                                       EntriesOffset,
                                       IndexLength);
    return Status;
}

NTSTATUS
NtfslxEnumerateIndexAllocationTree(
    _In_reads_bytes_(RootLength) const VOID *IndexRootBuffer,
    _In_ ULONG RootLength,
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT IndexAllocationRunlist,
    _In_ ULONG IndexAllocationRunlistCount,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    const NTFSLX_INDEX_ROOT_ATTRIBUTE *IndexRoot;
    NTFSLX_INDEX_TRAVERSAL_CONTEXT TraversalContext;
    ULONG EntriesOffset;
    ULONG IndexLength;
    NTSTATUS Status;

    if (IndexRootBuffer == NULL ||
        StorageDevice == NULL ||
        VolumeInfo == NULL ||
        Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((IndexAllocationRunlist == NULL && IndexAllocationRunlistCount != 0) ||
        (IndexAllocationRunlist != NULL && IndexAllocationRunlistCount == 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    IndexRoot = (const NTFSLX_INDEX_ROOT_ATTRIBUTE *)IndexRootBuffer;
    Status = NtfslxValidateIndexRoot(IndexRoot,
                                     RootLength,
                                     VolumeInfo->BytesPerIndexRecord,
                                     &EntriesOffset,
                                     &IndexLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(&TraversalContext, sizeof(TraversalContext));
    TraversalContext.StorageDevice = StorageDevice;
    TraversalContext.VolumeInfo = VolumeInfo;
    TraversalContext.Runlist = IndexAllocationRunlist;
    TraversalContext.Callback = Callback;
    TraversalContext.CallbackContext = Context;
    TraversalContext.IndexBlockSize = VolumeInfo->BytesPerIndexRecord;

    if (IndexAllocationRunlist != NULL)
    {
        ASSERT(IndexAllocationRunlistCount != 0);
        if (IndexAllocationRunlist[IndexAllocationRunlistCount - 1].Length != 0)
        {
            NTFSDBG("ntfslx: unterminated $INDEX_ALLOCATION runlist Count=%lu LastVcn=%I64d LastLcn=%I64d LastLen=%I64d\n",
                    IndexAllocationRunlistCount,
                    IndexAllocationRunlist[IndexAllocationRunlistCount - 1].Vcn,
                    IndexAllocationRunlist[IndexAllocationRunlistCount - 1].Lcn,
                    IndexAllocationRunlist[IndexAllocationRunlistCount - 1].Length);
            return STATUS_FILE_CORRUPT_ERROR;
        }
    }

    if (IndexRoot->Header.Flags != 0 &&
        (IndexAllocationRunlist == NULL || IndexAllocationRunlistCount == 0))
    {
        NTFSDBG("ntfslx: large index root seen without an index-allocation runlist\n");
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxEnumerateAllocationSubtree(&TraversalContext,
                                              IndexRoot,
                                              RootLength,
                                              EntriesOffset,
                                              IndexLength);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: failed walking index tree status=0x%08lx RootLength=%lu EntriesOffset=%lu IndexLength=%lu RunCount=%lu RootFlags=0x%02x\n",
                Status,
                RootLength,
                EntriesOffset,
                IndexLength,
                IndexAllocationRunlistCount,
                IndexRoot->Header.Flags);
        if (TraversalContext.VisitedVcns != NULL)
        {
            ExFreePoolWithTag(TraversalContext.VisitedVcns, NTFSLX_INDEX_ALLOCATION_TAG);
        }

        return Status;
    }

    if (TraversalContext.VisitedVcns != NULL)
    {
        ExFreePoolWithTag(TraversalContext.VisitedVcns, NTFSLX_INDEX_ALLOCATION_TAG);
    }

    return STATUS_SUCCESS;
}
