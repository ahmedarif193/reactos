/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Mutate the NTFS $I30 directory index
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define NTFS_INDEX_HEADER_LARGE 1

/*
 * Multi-record updates below are sequenced so every intermediate on-disk
 * state keeps the tree readable: new index buffers become reachable only
 * after their content and bitmap bits are durable, and the root switch is
 * always the final single-record commit. Crash atomicity across the whole
 * sequence still requires the future LFS transaction work.
 */

/*
 * Locates where Name belongs in one index node. Insertion callers leave
 * FoundExact NULL and receive STATUS_OBJECT_NAME_COLLISION for an exact
 * match; removal callers receive the matched entry's offset instead.
 */
static NTSTATUS
FindIndexInsertionPoint(
    _In_ PVolume DiskVolume,
    _In_ PIndexNodeHeader Header,
    _In_ ULONG HeaderBytes,
    _In_ PUNICODE_STRING Name,
    _Out_ PULONG InsertionOffset,
    _Out_ PULONGLONG ChildVcn,
    _Out_ PBOOLEAN Descend,
    _Out_opt_ PBOOLEAN FoundExact = NULL)
{
    PIndexEntry Entry;
    ULONG_PTR End;
    LONG Comparison;
    BOOLEAN Large;
    BOOLEAN FoundEnd = FALSE;
    NTSTATUS Status;

    if (!DiskVolume || !Header || !Name ||
        !InsertionOffset || !ChildVcn ||
        !Descend)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *InsertionOffset = 0;
    *ChildVcn = 0;
    *Descend = FALSE;
    if (FoundExact)
        *FoundExact = FALSE;

    if (HeaderBytes < sizeof(*Header) ||
        Header->IndexOffset < sizeof(*Header) ||
        Header->IndexOffset >
            Header->TotalIndexSize ||
        Header->TotalIndexSize >
            Header->AllocatedSize ||
        Header->AllocatedSize >
            HeaderBytes ||
        (Header->Flags &
         ~NTFS_INDEX_HEADER_LARGE) != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    Large =
        !!(Header->Flags &
           NTFS_INDEX_HEADER_LARGE);

    Entry =
        reinterpret_cast<PIndexEntry>(
            reinterpret_cast<PUCHAR>(Header) +
            Header->IndexOffset);
    End =
        reinterpret_cast<ULONG_PTR>(Header) +
        Header->TotalIndexSize;
    while (reinterpret_cast<ULONG_PTR>(Entry) <
           End)
    {
        PFileNameEx IndexedName;
        UNICODE_STRING IndexedString;
        BOOLEAN HasChild;

        if (!NtfsIsDirectoryIndexEntryValid(
                Entry,
                (ULONG)(End -
                    reinterpret_cast<ULONG_PTR>(
                        Entry))))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        HasChild =
            !!(Entry->Flags & INDEX_ENTRY_NODE);
        if (HasChild != Large)
            return STATUS_FILE_CORRUPT_ERROR;

        if (Entry->Flags & INDEX_ENTRY_END)
        {
            if (reinterpret_cast<ULONG_PTR>(Entry) +
                    Entry->EntryLength != End)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            FoundEnd = TRUE;
            if (HasChild)
            {
                *ChildVcn =
                    *GetSubnodeVCN(Entry);
                *Descend = TRUE;
            }
            else
            {
                *InsertionOffset =
                    (ULONG)(
                        reinterpret_cast<PUCHAR>(
                            Entry) -
                        reinterpret_cast<PUCHAR>(
                            Header));
            }
            break;
        }

        IndexedName =
            reinterpret_cast<PFileNameEx>(
                Entry->IndexStream);
        IndexedString =
            NtfsMakeCountedUnicodeString(
                IndexedName->Name,
                IndexedName->NameLength *
                    sizeof(WCHAR));
        Status = DiskVolume->CompareFileNames(
            Name,
            &IndexedString,
            &Comparison);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Comparison == 0)
        {
            if (!FoundExact)
                return STATUS_OBJECT_NAME_COLLISION;
            *FoundExact = TRUE;
            *InsertionOffset =
                (ULONG)(
                    reinterpret_cast<PUCHAR>(
                        Entry) -
                    reinterpret_cast<PUCHAR>(
                        Header));
            return STATUS_SUCCESS;
        }
        if (Comparison < 0)
        {
            if (HasChild)
            {
                *ChildVcn =
                    *GetSubnodeVCN(Entry);
                *Descend = TRUE;
            }
            else
            {
                *InsertionOffset =
                    (ULONG)(
                        reinterpret_cast<PUCHAR>(
                            Entry) -
                        reinterpret_cast<PUCHAR>(
                            Header));
            }
            return STATUS_SUCCESS;
        }

        Entry =
            reinterpret_cast<PIndexEntry>(
                reinterpret_cast<PUCHAR>(Entry) +
                Entry->EntryLength);
    }

    return FoundEnd
        ? STATUS_SUCCESS
        : STATUS_FILE_CORRUPT_ERROR;
}

static NTSTATUS
FindChildLinkOffset(
    _In_ PIndexNodeHeader Header,
    _In_ ULONGLONG ChildVcn,
    _Out_ PULONG LinkOffset)
{
    PIndexEntry Entry;
    ULONG_PTR End;

    *LinkOffset = 0;
    if ((Header->Flags &
         NTFS_INDEX_HEADER_LARGE) == 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Entry =
        reinterpret_cast<PIndexEntry>(
            reinterpret_cast<PUCHAR>(Header) +
            Header->IndexOffset);
    End =
        reinterpret_cast<ULONG_PTR>(Header) +
        Header->TotalIndexSize;
    while (reinterpret_cast<ULONG_PTR>(Entry) <
           End)
    {
        if (!NtfsIsDirectoryIndexEntryValid(
                Entry,
                (ULONG)(End -
                    reinterpret_cast<ULONG_PTR>(
                        Entry))) ||
            !(Entry->Flags & INDEX_ENTRY_NODE))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        if (*GetSubnodeVCN(Entry) == ChildVcn)
        {
            *LinkOffset =
                (ULONG)(
                    reinterpret_cast<PUCHAR>(
                        Entry) -
                    reinterpret_cast<PUCHAR>(
                        Header));
            return STATUS_SUCCESS;
        }
        if (Entry->Flags & INDEX_ENTRY_END)
            break;
        Entry =
            reinterpret_cast<PIndexEntry>(
                reinterpret_cast<PUCHAR>(Entry) +
                Entry->EntryLength);
    }

    return STATUS_FILE_CORRUPT_ERROR;
}

static NTSTATUS
BuildIndexEntry(
    _In_ ULONGLONG FileReference,
    _In_ PFileNameEx FileName,
    _Outptr_result_bytebuffer_(*EntryLength)
        PIndexEntry* NewEntry,
    _Out_ PULONG EntryLength)
{
    ULONG FileNameLength;
    ULONG Length;

    if (!FileName || !NewEntry ||
        !EntryLength ||
        FileName->NameLength == 0 ||
        FileName->NameType >
            NAME_TYPE_WIN32_AND_DOS ||
        GetSequenceFromFileRef(
            FileReference) == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *NewEntry = NULL;
    *EntryLength = 0;

    FileNameLength =
        FIELD_OFFSET(FileNameEx, Name) +
        FileName->NameLength * sizeof(WCHAR);
    if (FileNameLength > MAXUSHORT ||
        FileNameLength >
            MAXULONG -
            FIELD_OFFSET(IndexEntry,
                         IndexStream))
    {
        return STATUS_NAME_TOO_LONG;
    }
    Length = ALIGN_UP_BY(
        FIELD_OFFSET(IndexEntry,
                     IndexStream) +
            FileNameLength,
        sizeof(ULONGLONG));
    if (Length > MAXUSHORT)
        return STATUS_NAME_TOO_LONG;

    *NewEntry =
        reinterpret_cast<PIndexEntry>(
            NtfsAllocatePoolWithTag(
                PagedPool,
                Length,
                TAG_BTREE));
    if (!*NewEntry)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(*NewEntry,
                  Length);
    (*NewEntry)->Data.Directory.IndexedFile =
        FileReference;
    (*NewEntry)->EntryLength =
        (USHORT)Length;
    (*NewEntry)->StreamLength =
        (USHORT)FileNameLength;
    RtlCopyMemory((*NewEntry)->IndexStream,
                  FileName,
                  FileNameLength);
    *EntryLength = Length;
    return STATUS_SUCCESS;
}

/*
 * Copies one node's complete entry list (terminating END entry included)
 * while splicing PendingEntry in front of the entry at InsertionOffset.
 * With Retarget set, the displaced entry additionally receives a new
 * child VCN; this is how a split publishes its right node in the parent.
 */
static NTSTATUS
ComposeEntryList(
    _In_ PIndexNodeHeader Header,
    _In_ ULONG InsertionOffset,
    _In_ PIndexEntry PendingEntry,
    _In_ ULONG PendingLength,
    _In_ BOOLEAN Retarget,
    _In_ ULONGLONG RetargetVcn,
    _Out_writes_bytes_(*ListBytes) PUCHAR List,
    _Out_ PULONG ListBytes)
{
    PIndexEntry Displaced;
    ULONG ListStart = Header->IndexOffset;
    ULONG ListEnd = Header->TotalIndexSize;
    ULONG Prefix;

    if (InsertionOffset < ListStart ||
        InsertionOffset >= ListEnd)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    Prefix = InsertionOffset - ListStart;

    RtlCopyMemory(
        List,
        reinterpret_cast<PUCHAR>(Header) +
            ListStart,
        Prefix);
    RtlCopyMemory(List + Prefix,
                  PendingEntry,
                  PendingLength);
    RtlCopyMemory(
        List + Prefix + PendingLength,
        reinterpret_cast<PUCHAR>(Header) +
            InsertionOffset,
        ListEnd - InsertionOffset);
    *ListBytes =
        ListEnd - ListStart + PendingLength;

    if (Retarget)
    {
        Displaced =
            reinterpret_cast<PIndexEntry>(
                List + Prefix + PendingLength);
        if (!(Displaced->Flags &
              INDEX_ENTRY_NODE))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        *GetSubnodeVCN(Displaced) = RetargetVcn;
    }
    return STATUS_SUCCESS;
}

/*
 * Selects the promoted median of an entry list that no longer fits its
 * node: the first entry carrying at least half of the accumulated entry
 * bytes. Both resulting halves may be empty of ordinary entries; the
 * terminating END entry always stays with the right half.
 */
static NTSTATUS
SplitEntryList(
    _In_reads_bytes_(ListBytes) PUCHAR List,
    _In_ ULONG ListBytes,
    _Out_ PULONG LeftBytes,
    _Out_ PULONG MedianOffset,
    _Out_ PULONG MedianLength)
{
    PIndexEntry Entry;
    ULONG Offset = 0;
    ULONG PayloadBytes = 0;
    ULONG Accumulated = 0;

    *LeftBytes = 0;
    *MedianOffset = 0;
    *MedianLength = 0;

    while (Offset < ListBytes)
    {
        Entry =
            reinterpret_cast<PIndexEntry>(
                List + Offset);
        if (!NtfsIsDirectoryIndexEntryValid(
                Entry,
                ListBytes - Offset))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        if (Entry->Flags & INDEX_ENTRY_END)
        {
            if (Offset + Entry->EntryLength !=
                ListBytes)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            break;
        }
        PayloadBytes += Entry->EntryLength;
        Offset += Entry->EntryLength;
    }
    if (Offset >= ListBytes ||
        PayloadBytes == 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Offset = 0;
    for (;;)
    {
        Entry =
            reinterpret_cast<PIndexEntry>(
                List + Offset);
        if (Entry->Flags & INDEX_ENTRY_END)
            return STATUS_FILE_CORRUPT_ERROR;
        Accumulated += Entry->EntryLength;
        if (Accumulated * 2 >= PayloadBytes)
        {
            *LeftBytes = Offset;
            *MedianOffset = Offset;
            *MedianLength = Entry->EntryLength;
            return STATUS_SUCCESS;
        }
        Offset += Entry->EntryLength;
    }
}

/*
 * Builds a complete INDX record image around one entry list. The list
 * either already carries its END entry, or a fresh END is appended for
 * the left half of a split, pointing at the median's former subtree.
 */
static NTSTATUS
BuildIndexBufferImage(
    _Out_writes_bytes_(RecordSize) PUCHAR Image,
    _In_ ULONG RecordSize,
    _In_ ULONG BytesPerSector,
    _In_ ULONGLONG Vcn,
    _In_ BOOLEAN Internal,
    _In_reads_bytes_(EntriesBytes)
        const UCHAR* Entries,
    _In_ ULONG EntriesBytes,
    _In_ BOOLEAN BuildEnd,
    _In_ ULONGLONG EndChildVcn)
{
    PIndexBuffer Buffer =
        reinterpret_cast<PIndexBuffer>(Image);
    PIndexEntry EndEntry;
    PUSHORT UpdateSequence;
    ULONG UpdateSequenceCount;
    ULONG EntriesOffset;
    ULONG EndLength;
    ULONG Total;

    if (RecordSize < BytesPerSector ||
        RecordSize % BytesPerSector != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    UpdateSequenceCount =
        RecordSize / BytesPerSector + 1;

    RtlZeroMemory(Image, RecordSize);
    RtlCopyMemory(Buffer->RecordHeader.TypeID,
                  "INDX",
                  4);
    Buffer->RecordHeader.UpdateSequenceOffset =
        (USHORT)(
            FIELD_OFFSET(IndexBuffer,
                         IndexHeader) +
            sizeof(IndexNodeHeader));
    Buffer->RecordHeader.SizeOfUpdateSequence =
        (USHORT)UpdateSequenceCount;
    Buffer->VCN = Vcn;
    UpdateSequence =
        reinterpret_cast<PUSHORT>(
            Image +
            Buffer->
                RecordHeader.
                    UpdateSequenceOffset);
    UpdateSequence[0] = 1;

    EntriesOffset = ALIGN_UP_BY(
        sizeof(IndexNodeHeader) +
            UpdateSequenceCount *
                sizeof(USHORT),
        sizeof(ULONGLONG));
    EndLength = BuildEnd
        ? FIELD_OFFSET(IndexEntry,
                       IndexStream) +
            (Internal
                ? sizeof(ULONGLONG)
                : 0)
        : 0;
    Total = EntriesOffset + EntriesBytes +
        EndLength;
    if (RecordSize <
            FIELD_OFFSET(IndexBuffer,
                         IndexHeader) ||
        Total >
            RecordSize -
            FIELD_OFFSET(IndexBuffer,
                         IndexHeader))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Buffer->IndexHeader.IndexOffset =
        EntriesOffset;
    Buffer->IndexHeader.TotalIndexSize = Total;
    Buffer->IndexHeader.AllocatedSize =
        RecordSize -
        FIELD_OFFSET(IndexBuffer, IndexHeader);
    Buffer->IndexHeader.Flags =
        Internal ? NTFS_INDEX_HEADER_LARGE : 0;

    RtlCopyMemory(
        reinterpret_cast<PUCHAR>(
            &Buffer->IndexHeader) +
            EntriesOffset,
        Entries,
        EntriesBytes);
    if (BuildEnd)
    {
        EndEntry =
            reinterpret_cast<PIndexEntry>(
                reinterpret_cast<PUCHAR>(
                    &Buffer->IndexHeader) +
                EntriesOffset + EntriesBytes);
        EndEntry->EntryLength =
            (USHORT)EndLength;
        EndEntry->Flags = INDEX_ENTRY_END |
            (Internal ? INDEX_ENTRY_NODE : 0);
        if (Internal)
        {
            *GetSubnodeVCN(EndEntry) =
                EndChildVcn;
        }
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
WriteIndexNode(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord DirectoryFile,
    _In_ ULONGLONG Vcn,
    _In_ ULONGLONG AllocationUnit,
    _In_ ULONG RecordSize,
    _Inout_updates_bytes_(RecordSize)
        PUCHAR Image)
{
    LARGE_INTEGER WriteOffset;
    ULONGLONG AllocationOffset;
    ULONG WriteLength = RecordSize;
    NTSTATUS Status;

    if (Vcn > ~(ULONGLONG)0 / AllocationUnit)
        return STATUS_FILE_CORRUPT_ERROR;
    AllocationOffset = Vcn * AllocationUnit;
    if (AllocationOffset >
        0x7fffffffffffffffULL - RecordSize)
    {
        return STATUS_FILE_TOO_LARGE;
    }

    Status = NtfsCommitFixup(
        reinterpret_cast<PNTFSRecordHeader>(
            Image),
        RecordSize,
        DiskVolume->BytesPerSector);
    if (!NT_SUCCESS(Status))
        return Status;

    WriteOffset.QuadPart =
        (LONGLONG)AllocationOffset;
    Status = DirectoryFile->WriteFileData(
        TypeIndexAllocation,
        const_cast<PWSTR>(NtfsI30Name),
        Image,
        &WriteLength,
        &WriteOffset);
    if (NT_SUCCESS(Status) &&
        WriteLength != RecordSize)
    {
        Status = STATUS_END_OF_FILE;
    }
    return Status;
}

static NTSTATUS
SetIndexRecordBitmapBit(
    _In_ PFileRecord DirectoryFile,
    _In_ ULONGLONG RecordOrdinal,
    _In_ BOOLEAN Value)
{
    PAttribute BitmapAttribute;
    LARGE_INTEGER WriteOffset;
    ULONGLONG BitmapLength;
    ULONGLONG ByteOffset;
    ULONG BytesRemaining;
    ULONG WriteLength;
    UCHAR GrowthChunk[8] = {};
    UCHAR BitmapByte;
    UCHAR BitmapMask;
    NTSTATUS Status;

    ByteOffset = RecordOrdinal >> 3;
    BitmapMask =
        (UCHAR)(1u << (RecordOrdinal & 7));

    BitmapAttribute =
        DirectoryFile->GetAttribute(
            TypeBitmap,
            const_cast<PWSTR>(NtfsI30Name));
    if (!BitmapAttribute)
        return STATUS_FILE_CORRUPT_ERROR;
    BitmapLength =
        GetAttributeDataSize(BitmapAttribute);

    if (ByteOffset >= BitmapLength)
    {
        if (!Value)
            return STATUS_SUCCESS;
        if (ByteOffset - BitmapLength >=
            sizeof(GrowthChunk))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        WriteLength = sizeof(GrowthChunk);
        WriteOffset.QuadPart =
            (LONGLONG)BitmapLength;
        Status = DirectoryFile->WriteFileData(
            TypeBitmap,
            const_cast<PWSTR>(NtfsI30Name),
            GrowthChunk,
            &WriteLength,
            &WriteOffset);
        if (!NT_SUCCESS(Status))
            return Status;
        if (WriteLength != sizeof(GrowthChunk))
            return STATUS_END_OF_FILE;
        BitmapAttribute =
            DirectoryFile->GetAttribute(
                TypeBitmap,
                const_cast<PWSTR>(NtfsI30Name));
        if (!BitmapAttribute)
            return STATUS_FILE_CORRUPT_ERROR;
    }

    BytesRemaining = 1;
    Status = DirectoryFile->CopyData(
        BitmapAttribute,
        &BitmapByte,
        &BytesRemaining,
        ByteOffset);
    if (!NT_SUCCESS(Status))
        return Status;
    if (BytesRemaining != 0)
        return STATUS_END_OF_FILE;

    if (Value)
    {
        if (BitmapByte & BitmapMask)
            return STATUS_FILE_CORRUPT_ERROR;
        BitmapByte |= BitmapMask;
    }
    else
    {
        BitmapByte &= (UCHAR)~BitmapMask;
    }

    WriteLength = 1;
    WriteOffset.QuadPart = (LONGLONG)ByteOffset;
    Status = DirectoryFile->WriteFileData(
        TypeBitmap,
        const_cast<PWSTR>(NtfsI30Name),
        &BitmapByte,
        &WriteLength,
        &WriteOffset);
    if (NT_SUCCESS(Status) && WriteLength != 1)
        Status = STATUS_END_OF_FILE;
    return Status;
}

/*
 * Chooses the target position for a new index record: the first free
 * bitmap slot inside the current allocation, or the append position at
 * its end. Nothing is made durable here; the record becomes owned once
 * its image is written and its bitmap bit is set.
 */
static NTSTATUS
SelectIndexRecordSlot(
    _In_ PFileRecord DirectoryFile,
    _In_ ULONG RecordSize,
    _In_ ULONGLONG AllocationUnit,
    _Out_ PULONGLONG Vcn,
    _Out_ PULONGLONG RecordOrdinal)
{
    PAttribute IndexAllocationAttribute;
    PAttribute BitmapAttribute;
    UCHAR Chunk[64];
    ULONGLONG AllocationLength;
    ULONGLONG BitmapLength;
    ULONGLONG RecordCount;
    ULONGLONG Ordinal = 0;
    ULONGLONG ByteOffset = 0;
    NTSTATUS Status;

    *Vcn = 0;
    *RecordOrdinal = 0;

    IndexAllocationAttribute =
        DirectoryFile->GetAttribute(
            TypeIndexAllocation,
            const_cast<PWSTR>(NtfsI30Name));
    BitmapAttribute =
        DirectoryFile->GetAttribute(
            TypeBitmap,
            const_cast<PWSTR>(NtfsI30Name));
    if (!IndexAllocationAttribute ||
        !IndexAllocationAttribute->
            IsNonResident ||
        !BitmapAttribute)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    AllocationLength =
        GetAttributeDataSize(
            IndexAllocationAttribute);
    BitmapLength =
        GetAttributeDataSize(BitmapAttribute);
    if (AllocationLength % RecordSize != 0)
        return STATUS_FILE_CORRUPT_ERROR;
    RecordCount =
        AllocationLength / RecordSize;

    while (Ordinal < RecordCount &&
           ByteOffset < BitmapLength)
    {
        ULONG BytesRemaining =
            (ULONG)(
                BitmapLength - ByteOffset <
                    sizeof(Chunk)
                ? BitmapLength - ByteOffset
                : sizeof(Chunk));
        ULONG Read = BytesRemaining;

        Status = DirectoryFile->CopyData(
            BitmapAttribute,
            Chunk,
            &BytesRemaining,
            ByteOffset);
        if (!NT_SUCCESS(Status))
            return Status;
        if (BytesRemaining != 0)
            return STATUS_END_OF_FILE;

        for (ULONG Index = 0;
             Index < Read &&
                Ordinal < RecordCount;
             Index++)
        {
            for (ULONG Bit = 0;
                 Bit < 8 &&
                    Ordinal < RecordCount;
                 Bit++, Ordinal++)
            {
                if (!(Chunk[Index] &
                      (1u << Bit)))
                {
                    *RecordOrdinal = Ordinal;
                    *Vcn = Ordinal *
                        (RecordSize /
                         AllocationUnit);
                    return STATUS_SUCCESS;
                }
            }
        }
        ByteOffset += Read;
    }

    *RecordOrdinal = RecordCount;
    *Vcn = RecordCount *
        (RecordSize / AllocationUnit);
    return STATUS_SUCCESS;
}

/*
 * Replaces the complete $INDEX_ROOT value with backup/restore so a
 * failed record commit leaves the previous root intact.
 */
NTSTATUS
Directory::ReplaceIndexRootValue(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord DirectoryFile,
    _In_reads_bytes_(ValueLength) PUCHAR Value,
    _In_ ULONG ValueLength)
{
    PAttribute IndexRootAttribute;
    PFileRecord IndexRootOwner;
    PUCHAR RecordBackup;
    NTSTATUS Status;

    IndexRootAttribute =
        DirectoryFile->GetAttribute(
            TypeIndexRoot,
            const_cast<PWSTR>(NtfsI30Name));
    if (!IndexRootAttribute ||
        IndexRootAttribute->IsNonResident)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    IndexRootOwner =
        DirectoryFile->GetAttributeOwner(
            IndexRootAttribute);
    if (!IndexRootOwner)
        return STATUS_FILE_CORRUPT_ERROR;

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[IndexRootOwner->
                RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup,
                  IndexRootOwner->Data,
                  IndexRootOwner->
                      RecordBufferSize);

    Status = IndexRootOwner->
        ReplaceResidentData(
            IndexRootAttribute,
            Value,
            ValueLength);
    if (NT_SUCCESS(Status))
    {
        Status = DiskVolume->MFT->
            WriteFileRecordToMFT(
                IndexRootOwner);
    }
    if (!NT_SUCCESS(Status))
    {
        RtlCopyMemory(
            IndexRootOwner->Data,
            RecordBackup,
            IndexRootOwner->RecordBufferSize);
        IndexRootOwner->Header =
            reinterpret_cast<PFileRecordHeader>(
                IndexRootOwner->Data);
        IndexRootOwner->ClearDataRunCache();
    }
    delete[] RecordBackup;
    return Status;
}

static NTSTATUS
BuildRootValueFromList(
    _In_ const IndexRootEx* OldRoot,
    _In_reads_bytes_(ListBytes)
        const UCHAR* List,
    _In_ ULONG ListBytes,
    _In_ BOOLEAN Large,
    _Outptr_result_bytebuffer_(*ValueLength)
        PUCHAR* Value,
    _Out_ PULONG ValueLength)
{
    const ULONG RootPrefix =
        FIELD_OFFSET(IndexRootEx, Header);
    PIndexRootEx NewRoot;
    ULONG Total;

    *Value = NULL;
    *ValueLength = 0;
    if (ListBytes >
        MAXULONG - RootPrefix -
            sizeof(IndexNodeHeader))
    {
        return STATUS_FILE_TOO_LARGE;
    }
    Total = RootPrefix +
        sizeof(IndexNodeHeader) + ListBytes;

    *Value =
        new(PagedPool, TAG_BTREE) UCHAR[Total];
    if (!*Value)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(*Value, Total);
    NewRoot =
        reinterpret_cast<PIndexRootEx>(*Value);
    RtlCopyMemory(NewRoot,
                  OldRoot,
                  RootPrefix);
    NewRoot->Header.IndexOffset =
        sizeof(IndexNodeHeader);
    NewRoot->Header.TotalIndexSize =
        sizeof(IndexNodeHeader) + ListBytes;
    NewRoot->Header.AllocatedSize =
        NewRoot->Header.TotalIndexSize;
    NewRoot->Header.Flags =
        Large ? NTFS_INDEX_HEADER_LARGE : 0;
    RtlCopyMemory(
        reinterpret_cast<PUCHAR>(
            &NewRoot->Header) +
            sizeof(IndexNodeHeader),
        List,
        ListBytes);
    *ValueLength = Total;
    return STATUS_SUCCESS;
}

/*
 * Moves one complete root entry list into an index buffer and leaves the
 * root as a single END entry pointing at it. This serves both the first
 * promotion of a resident-only directory and later root overflows of an
 * already-large index. The buffer content becomes durable before the
 * root switch commits the new shape.
 */
NTSTATUS
Directory::PushDownRoot(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord DirectoryFile,
    _In_ const IndexRootEx* OldRoot,
    _In_reads_bytes_(ListBytes) PUCHAR List,
    _In_ ULONG ListBytes,
    _In_ BOOLEAN ListInternal,
    _In_ ULONG RecordSize,
    _In_ ULONGLONG AllocationUnit)
{
    PAttribute IndexAllocationAttribute;
    PAttribute BitmapAttribute;
    PIndexEntry RootEnd;
    PUCHAR Image = NULL;
    PUCHAR RootValue = NULL;
    UCHAR RootEndList[
        FIELD_OFFSET(IndexEntry, IndexStream) +
        sizeof(ULONGLONG)] = {};
    IndexRootEx RootPrefix;
    ULONGLONG Vcn = 0;
    ULONGLONG RecordOrdinal = 0;
    ULONG RootValueLength;
    NTSTATUS Status;

    /*
     * The record mutations below may relocate attribute values; keep the
     * fixed $INDEX_ROOT prefix in a local copy.
     */
    RtlCopyMemory(&RootPrefix,
                  OldRoot,
                  sizeof(RootPrefix));

    Image =
        new(PagedPool, TAG_BTREE)
            UCHAR[RecordSize];
    if (!Image)
        return STATUS_INSUFFICIENT_RESOURCES;

    IndexAllocationAttribute =
        DirectoryFile->GetAttribute(
            TypeIndexAllocation,
            const_cast<PWSTR>(NtfsI30Name));
    BitmapAttribute =
        DirectoryFile->GetAttribute(
            TypeBitmap,
            const_cast<PWSTR>(NtfsI30Name));
    if (!IndexAllocationAttribute !=
        !BitmapAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    if (!IndexAllocationAttribute)
    {
        /*
         * First promotion. The full record has no room for the two new
         * attribute headers while the old root list is still resident,
         * so every edit stays in memory in this order: shrink the root
         * to its END-only form, add $BITMAP with the first bit set, add
         * the $INDEX_ALLOCATION stub, and let the stub's promotion write
         * the already-durable buffer image and commit the record once.
         */
        PAttribute IndexRootAttribute;
        PAttribute NewAttribute;
        PFileRecord RootOwner;
        PUCHAR BaseBackup = NULL;
        PUCHAR OwnerBackup = NULL;
        UCHAR InitialBitmap[8] = { 1 };

        Status = BuildIndexBufferImage(
            Image,
            RecordSize,
            DiskVolume->BytesPerSector,
            0,
            ListInternal,
            List,
            ListBytes,
            FALSE,
            0);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = NtfsCommitFixup(
            reinterpret_cast<PNTFSRecordHeader>(
                Image),
            RecordSize,
            DiskVolume->BytesPerSector);
        if (!NT_SUCCESS(Status))
            goto Done;

        RootEnd =
            reinterpret_cast<PIndexEntry>(
                RootEndList);
        RootEnd->EntryLength =
            (USHORT)sizeof(RootEndList);
        RootEnd->Flags =
            INDEX_ENTRY_END | INDEX_ENTRY_NODE;
        *GetSubnodeVCN(RootEnd) = 0;
        Status = BuildRootValueFromList(
            &RootPrefix,
            RootEndList,
            sizeof(RootEndList),
            TRUE,
            &RootValue,
            &RootValueLength);
        if (!NT_SUCCESS(Status))
            goto Done;

        IndexRootAttribute =
            DirectoryFile->GetAttribute(
                TypeIndexRoot,
                const_cast<PWSTR>(NtfsI30Name));
        if (!IndexRootAttribute ||
            IndexRootAttribute->IsNonResident)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        RootOwner =
            DirectoryFile->GetAttributeOwner(
                IndexRootAttribute);
        if (!RootOwner)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        BaseBackup =
            new(PagedPool, TAG_FILE_RECORD)
                UCHAR[DirectoryFile->
                    RecordBufferSize];
        if (!BaseBackup)
        {
            Status =
                STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        RtlCopyMemory(
            BaseBackup,
            DirectoryFile->Data,
            DirectoryFile->RecordBufferSize);
        if (RootOwner != DirectoryFile)
        {
            OwnerBackup =
                new(PagedPool, TAG_FILE_RECORD)
                    UCHAR[RootOwner->
                        RecordBufferSize];
            if (!OwnerBackup)
            {
                delete[] BaseBackup;
                Status =
                    STATUS_INSUFFICIENT_RESOURCES;
                goto Done;
            }
            RtlCopyMemory(
                OwnerBackup,
                RootOwner->Data,
                RootOwner->RecordBufferSize);
        }

        Status = RootOwner->ReplaceResidentData(
            IndexRootAttribute,
            RootValue,
            RootValueLength);
        if (NT_SUCCESS(Status))
        {
            Status =
                DirectoryFile->
                    InsertResidentAttribute(
                        TypeBitmap,
                        const_cast<PWSTR>(
                            NtfsI30Name),
                        &NewAttribute);
        }
        if (NT_SUCCESS(Status))
        {
            Status =
                DirectoryFile->
                    ReplaceResidentData(
                        NewAttribute,
                        InitialBitmap,
                        sizeof(InitialBitmap));
        }
        if (NT_SUCCESS(Status))
        {
            Status =
                DirectoryFile->
                    InsertResidentAttribute(
                        TypeIndexAllocation,
                        const_cast<PWSTR>(
                            NtfsI30Name),
                        &NewAttribute);
        }
        if (NT_SUCCESS(Status))
        {
            Status =
                DirectoryFile->
                    PromoteResidentData(
                        NewAttribute,
                        Image,
                        RecordSize,
                        0,
                        RecordSize,
                        RecordSize);
        }
        if (NT_SUCCESS(Status) &&
            RootOwner != DirectoryFile)
        {
            Status = DiskVolume->MFT->
                WriteFileRecordToMFT(RootOwner);
        }
        if (!NT_SUCCESS(Status))
        {
            RtlCopyMemory(
                DirectoryFile->Data,
                BaseBackup,
                DirectoryFile->
                    RecordBufferSize);
            DirectoryFile->Header =
                reinterpret_cast<
                    PFileRecordHeader>(
                        DirectoryFile->Data);
            DirectoryFile->ClearDataRunCache();
            if (OwnerBackup)
            {
                RtlCopyMemory(
                    RootOwner->Data,
                    OwnerBackup,
                    RootOwner->RecordBufferSize);
                RootOwner->Header =
                    reinterpret_cast<
                        PFileRecordHeader>(
                            RootOwner->Data);
                RootOwner->ClearDataRunCache();
            }
        }
        delete[] OwnerBackup;
        delete[] BaseBackup;
        goto Done;
    }
    else
    {
        Status = SelectIndexRecordSlot(
            DirectoryFile,
            RecordSize,
            AllocationUnit,
            &Vcn,
            &RecordOrdinal);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = BuildIndexBufferImage(
            Image,
            RecordSize,
            DiskVolume->BytesPerSector,
            Vcn,
            ListInternal,
            List,
            ListBytes,
            FALSE,
            0);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = WriteIndexNode(
            DiskVolume,
            DirectoryFile,
            Vcn,
            AllocationUnit,
            RecordSize,
            Image);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = SetIndexRecordBitmapBit(
            DirectoryFile,
            RecordOrdinal,
            TRUE);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    RootEnd =
        reinterpret_cast<PIndexEntry>(
            RootEndList);
    RootEnd->EntryLength =
        (USHORT)sizeof(RootEndList);
    RootEnd->Flags =
        INDEX_ENTRY_END | INDEX_ENTRY_NODE;
    *GetSubnodeVCN(RootEnd) = Vcn;

    Status = BuildRootValueFromList(
        &RootPrefix,
        RootEndList,
        sizeof(RootEndList),
        TRUE,
        &RootValue,
        &RootValueLength);
    if (!NT_SUCCESS(Status))
        goto Done;
    Status = ReplaceIndexRootValue(
        DiskVolume,
        DirectoryFile,
        RootValue,
        RootValueLength);
    if (!NT_SUCCESS(Status))
    {
        NTSTATUS CleanupStatus =
            SetIndexRecordBitmapBit(
                DirectoryFile,
                RecordOrdinal,
                FALSE);
        if (!NT_SUCCESS(CleanupStatus))
        {
            DPRINT1(
                "Index bitmap bit %llu was not "
                "released after a failed root "
                "switch: 0x%lx.\n",
                RecordOrdinal,
                CleanupStatus);
        }
    }

Done:
    delete[] RootValue;
    delete[] Image;
    return Status;
}

/*
 * Turns the median of a split into the entry promoted to the parent:
 * the median's key travels upward while its child slot points at the
 * left node that now holds every key collating before it.
 */
static NTSTATUS
BuildPromotedEntry(
    _In_reads_bytes_(MedianLength)
        const UCHAR* Median,
    _In_ ULONG MedianLength,
    _In_ BOOLEAN Internal,
    _In_ ULONGLONG LeftVcn,
    _Outptr_result_bytebuffer_(*PromotedLength)
        PIndexEntry* Promoted,
    _Out_ PULONG PromotedLength)
{
    PIndexEntry Entry;
    ULONG Length;

    *Promoted = NULL;
    *PromotedLength = 0;

    Length = Internal
        ? MedianLength
        : MedianLength + sizeof(ULONGLONG);
    if (Length > MAXUSHORT)
        return STATUS_NAME_TOO_LONG;
    Entry =
        reinterpret_cast<PIndexEntry>(
            NtfsAllocatePoolWithTag(
                PagedPool,
                Length,
                TAG_BTREE));
    if (!Entry)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Entry, Length);
    RtlCopyMemory(Entry, Median, MedianLength);
    Entry->EntryLength = (USHORT)Length;
    Entry->Flags |= INDEX_ENTRY_NODE;
    *GetSubnodeVCN(Entry) = LeftVcn;
    *Promoted = Entry;
    *PromotedLength = Length;
    return STATUS_SUCCESS;
}

static NTSTATUS
ReadIndexNode(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord DirectoryFile,
    _In_ ULONG RecordSize,
    _In_ ULONGLONG AllocationUnit,
    _In_ ULONGLONG Vcn,
    _Out_writes_bytes_(RecordSize) PUCHAR Image)
{
    PAttribute IndexAllocationAttribute;
    PAttribute BitmapAttribute;
    PIndexBuffer NodeBuffer;
    ULONGLONG AllocationLength;
    ULONGLONG BitmapLength;
    ULONGLONG AllocationOffset;
    ULONGLONG RecordNumber;
    ULONGLONG BitmapByte;
    ULONG BytesRemaining;
    UCHAR BitmapValue;
    UCHAR BitmapMask;
    NTSTATUS Status;

    IndexAllocationAttribute =
        DirectoryFile->GetAttribute(
            TypeIndexAllocation,
            const_cast<PWSTR>(NtfsI30Name));
    BitmapAttribute =
        DirectoryFile->GetAttribute(
            TypeBitmap,
            const_cast<PWSTR>(NtfsI30Name));
    if (!IndexAllocationAttribute ||
        !IndexAllocationAttribute->
            IsNonResident ||
        !BitmapAttribute)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    AllocationLength =
        GetAttributeDataSize(
            IndexAllocationAttribute);
    BitmapLength =
        GetAttributeDataSize(BitmapAttribute);

    if (Vcn > ~(ULONGLONG)0 / AllocationUnit)
        return STATUS_FILE_CORRUPT_ERROR;
    AllocationOffset = Vcn * AllocationUnit;
    if (AllocationOffset % RecordSize != 0 ||
        AllocationOffset > AllocationLength ||
        RecordSize >
            AllocationLength -
            AllocationOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    RecordNumber =
        AllocationOffset / RecordSize;
    BitmapByte = RecordNumber >> 3;
    if (BitmapByte >= BitmapLength)
        return STATUS_FILE_CORRUPT_ERROR;
    BytesRemaining = 1;
    Status = DirectoryFile->CopyData(
        BitmapAttribute,
        &BitmapValue,
        &BytesRemaining,
        BitmapByte);
    BitmapMask =
        (UCHAR)(1u << (RecordNumber & 7));
    if (!NT_SUCCESS(Status))
        return Status;
    if (BytesRemaining != 0 ||
        !(BitmapValue & BitmapMask))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    BytesRemaining = RecordSize;
    Status = DirectoryFile->CopyData(
        IndexAllocationAttribute,
        Image,
        &BytesRemaining,
        AllocationOffset);
    if (!NT_SUCCESS(Status))
        return Status;
    if (BytesRemaining != 0)
        return STATUS_END_OF_FILE;
    NodeBuffer =
        reinterpret_cast<PIndexBuffer>(Image);
    Status = NtfsApplyFixup(
        &NodeBuffer->RecordHeader,
        RecordSize,
        DiskVolume->BytesPerSector);
    if (!NT_SUCCESS(Status))
        return Status;
    if (RtlCompareMemory(
            NodeBuffer->RecordHeader.TypeID,
            "INDX",
            4) != 4 ||
        NodeBuffer->VCN != Vcn)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    return STATUS_SUCCESS;
}

/*
 * Ascends the recorded descent path after a node overflow. Each level
 * splits around its median, publishes the right node, and retries the
 * promoted entry one level up until it fits, reaching the root at worst.
 */
NTSTATUS
Directory::SplitAndPromote(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord DirectoryFile,
    _In_ ULONG RecordSize,
    _In_ ULONGLONG AllocationUnit,
    _In_reads_(PathDepth)
        const ULONGLONG* PathVcns,
    _In_ ULONG PathDepth,
    _Inout_updates_bytes_(RecordSize)
        PUCHAR NodeImage,
    _In_ ULONG LeafInsertionOffset,
    _In_ PIndexEntry FirstPending,
    _In_ ULONG FirstPendingLength)
{
    PIndexEntry Pending = FirstPending;
    PIndexEntry Promoted = NULL;
    PUCHAR Scratch = NULL;
    PUCHAR SideImage = NULL;
    ULONG PendingLength = FirstPendingLength;
    ULONG InsertionOffset =
        LeafInsertionOffset;
    ULONG ScratchCapacity;
    ULONG Level = PathDepth;
    BOOLEAN Retarget = FALSE;
    ULONGLONG RetargetVcn = 0;
    NTSTATUS Status;

    /* One node's entry list (<= RecordSize) plus one pending entry. */
    ScratchCapacity =
        RecordSize + (MAXUSHORT + 1);
    Scratch =
        new(PagedPool, TAG_BTREE)
            UCHAR[ScratchCapacity];
    SideImage =
        new(PagedPool, TAG_BTREE)
            UCHAR[RecordSize];
    if (!Scratch || !SideImage)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    while (Level != 0)
    {
        PIndexBuffer NodeBuffer =
            reinterpret_cast<PIndexBuffer>(
                NodeImage);
        PIndexNodeHeader Header =
            &NodeBuffer->IndexHeader;
        ULONGLONG NodeVcn =
            PathVcns[Level - 1];
        ULONGLONG NewVcn;
        ULONGLONG NewOrdinal;
        ULONG ListBytes;
        ULONG LeftBytes;
        ULONG MedianOffset;
        ULONG MedianLength;
        BOOLEAN Internal;

        if (Header->TotalIndexSize -
                Header->IndexOffset >
            ScratchCapacity - PendingLength)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        Status = ComposeEntryList(
            Header,
            InsertionOffset,
            Pending,
            PendingLength,
            Retarget,
            RetargetVcn,
            Scratch,
            &ListBytes);
        if (!NT_SUCCESS(Status))
            goto Done;
        Internal =
            !!(Header->Flags &
               NTFS_INDEX_HEADER_LARGE);

        if (Header->IndexOffset + ListBytes <=
            Header->AllocatedSize)
        {
            RtlCopyMemory(
                reinterpret_cast<PUCHAR>(
                    Header) +
                    Header->IndexOffset,
                Scratch,
                ListBytes);
            Header->TotalIndexSize =
                Header->IndexOffset +
                ListBytes;
            Status = WriteIndexNode(
                DiskVolume,
                DirectoryFile,
                NodeVcn,
                AllocationUnit,
                RecordSize,
                NodeImage);
            goto Done;
        }

        Status = SplitEntryList(
            Scratch,
            ListBytes,
            &LeftBytes,
            &MedianOffset,
            &MedianLength);
        if (!NT_SUCCESS(Status))
            goto Done;

        Status = SelectIndexRecordSlot(
            DirectoryFile,
            RecordSize,
            AllocationUnit,
            &NewVcn,
            &NewOrdinal);
        if (!NT_SUCCESS(Status))
            goto Done;

        /*
         * Publish the right node first: until the parent link switches,
         * it stays unreachable and a failure leaves the previous tree.
         */
        Status = BuildIndexBufferImage(
            SideImage,
            RecordSize,
            DiskVolume->BytesPerSector,
            NewVcn,
            Internal,
            Scratch + MedianOffset +
                MedianLength,
            ListBytes - MedianOffset -
                MedianLength,
            FALSE,
            0);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = WriteIndexNode(
            DiskVolume,
            DirectoryFile,
            NewVcn,
            AllocationUnit,
            RecordSize,
            SideImage);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = SetIndexRecordBitmapBit(
            DirectoryFile,
            NewOrdinal,
            TRUE);
        if (!NT_SUCCESS(Status))
            goto Done;

        Status = BuildPromotedEntry(
            Scratch + MedianOffset,
            MedianLength,
            Internal,
            NodeVcn,
            &Promoted,
            &PendingLength);
        if (!NT_SUCCESS(Status))
            goto Done;

        Status = BuildIndexBufferImage(
            SideImage,
            RecordSize,
            DiskVolume->BytesPerSector,
            NodeVcn,
            Internal,
            Scratch,
            LeftBytes,
            TRUE,
            Internal
                ? *GetSubnodeVCN(
                    reinterpret_cast<
                        PIndexEntry>(
                            Scratch +
                            MedianOffset))
                : 0);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = WriteIndexNode(
            DiskVolume,
            DirectoryFile,
            NodeVcn,
            AllocationUnit,
            RecordSize,
            SideImage);
        if (!NT_SUCCESS(Status))
            goto Done;

        if (Pending != FirstPending)
            NtfsFreePool(Pending);
        Pending = Promoted;
        Promoted = NULL;
        Retarget = TRUE;
        RetargetVcn = NewVcn;
        Level--;

        if (Level != 0)
        {
            PIndexNodeHeader ParentHeader;

            Status = ReadIndexNode(
                DiskVolume,
                DirectoryFile,
                RecordSize,
                AllocationUnit,
                PathVcns[Level - 1],
                NodeImage);
            if (!NT_SUCCESS(Status))
                goto Done;
            ParentHeader =
                &reinterpret_cast<PIndexBuffer>(
                    NodeImage)->IndexHeader;
            Status = FindChildLinkOffset(
                ParentHeader,
                PathVcns[Level],
                &InsertionOffset);
            if (!NT_SUCCESS(Status))
                goto Done;
        }
    }

    /* The promoted entry now belongs in the resident root. */
    {
        PAttribute IndexRootAttribute;
        PIndexRootEx IndexRoot;
        PUCHAR RootValue = NULL;
        ULONG RootValueLength;
        ULONG ListBytes;

        IndexRootAttribute =
            DirectoryFile->GetAttribute(
                TypeIndexRoot,
                const_cast<PWSTR>(NtfsI30Name));
        if (!IndexRootAttribute ||
            IndexRootAttribute->IsNonResident ||
            IndexRootAttribute->
                Resident.DataLength <
                    FIELD_OFFSET(IndexRootEx,
                                 Header) +
                    sizeof(IndexNodeHeader))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        IndexRoot =
            reinterpret_cast<PIndexRootEx>(
                GetResidentDataPointer(
                    IndexRootAttribute));
        Status = FindChildLinkOffset(
            &IndexRoot->Header,
            PathVcns[0],
            &InsertionOffset);
        if (!NT_SUCCESS(Status))
            goto Done;
        if (IndexRoot->Header.TotalIndexSize -
                IndexRoot->Header.IndexOffset >
            ScratchCapacity - PendingLength)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        Status = ComposeEntryList(
            &IndexRoot->Header,
            InsertionOffset,
            Pending,
            PendingLength,
            Retarget,
            RetargetVcn,
            Scratch,
            &ListBytes);
        if (!NT_SUCCESS(Status))
            goto Done;

        Status = BuildRootValueFromList(
            IndexRoot,
            Scratch,
            ListBytes,
            TRUE,
            &RootValue,
            &RootValueLength);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = ReplaceIndexRootValue(
            DiskVolume,
            DirectoryFile,
            RootValue,
            RootValueLength);
        delete[] RootValue;
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            /*
             * The root record itself is full: push the composed root
             * list one level down and re-point the root at it.
             */
            Status = PushDownRoot(
                DiskVolume,
                DirectoryFile,
                IndexRoot,
                Scratch,
                ListBytes,
                TRUE,
                RecordSize,
                AllocationUnit);
        }
    }

Done:
    if (Pending && Pending != FirstPending)
        NtfsFreePool(Pending);
    if (Promoted)
        NtfsFreePool(Promoted);
    delete[] SideImage;
    delete[] Scratch;
    return Status;
}

/*
 * Inserts one entry into a node's entry list at InsertionOffset (the mirror
 * of CutEntryInNodeHeader). The caller has already ensured the list has room;
 * AllocatedSize, if it changes, is the caller's concern.
 */
static void
SpliceEntryIntoHeader(
    _Inout_ PIndexNodeHeader Header,
    _In_ ULONG InsertionOffset,
    _In_ PIndexEntry Entry,
    _In_ ULONG EntryLength)
{
    RtlMoveMemory(
        reinterpret_cast<PUCHAR>(Header) +
            InsertionOffset + EntryLength,
        reinterpret_cast<PUCHAR>(Header) +
            InsertionOffset,
        Header->TotalIndexSize - InsertionOffset);
    RtlCopyMemory(
        reinterpret_cast<PUCHAR>(Header) +
            InsertionOffset,
        Entry,
        EntryLength);
    Header->TotalIndexSize += EntryLength;
}

NTSTATUS
Directory::AddFileToDirectory(
    _In_ PFileRecord DirectoryFile,
    _In_ ULONGLONG FileReference,
    _In_ PFileNameEx FileToAdd)
{
    PAttribute IndexRootAttribute;
    PAttribute IndexAllocationAttribute;
    PAttribute BitmapAttribute;
    PIndexRootEx IndexRoot;
    PIndexEntry NewEntry = NULL;
    PUCHAR NewRootData = NULL;
    PUCHAR IndexBufferData = NULL;
    ULONG EntryLength;
    ULONG InsertionOffset;
    ULONG NewRootDataLength;
    ULONG IndexRecordSize;
    ULONG VisitedCount = 0;
    ULONGLONG ChildVcn;
    ULONGLONG VisitedVcns[64];
    ULONGLONG ParentReference;
    ULONGLONG AllocationUnit;
    UNICODE_STRING Name;
    BOOLEAN Descend;
    BOOLEAN EntryCommitted = FALSE;
    NTSTATUS TimestampStatus;
    NTSTATUS Status;

    if (!DiskVolume || !DirectoryFile ||
        !DirectoryFile->Header ||
        !DirectoryFile->Data ||
        !FileToAdd ||
        !(DirectoryFile->Header->Flags &
          FR_IS_DIRECTORY) ||
        DirectoryFile->Header->SequenceNumber == 0 ||
        DiskVolume->IsReadOnly)
    {
        return STATUS_INVALID_PARAMETER;
    }
    ParentReference =
        MakeFileReference(DirectoryFile->Header);
    if (FileToAdd->ParentFileReference !=
            ParentReference)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = BuildIndexEntry(
        FileReference,
        FileToAdd,
        &NewEntry,
        &EntryLength);
    if (!NT_SUCCESS(Status))
        goto Done;
    Name = NtfsMakeCountedUnicodeString(
        FileToAdd->Name,
        FileToAdd->NameLength * sizeof(WCHAR));

    IndexRootAttribute =
        DirectoryFile->GetAttribute(
            TypeIndexRoot,
            const_cast<PWSTR>(NtfsI30Name));
    if (!IndexRootAttribute ||
        IndexRootAttribute->IsNonResident ||
        IndexRootAttribute->
            Resident.DataOffset < 0x18 ||
        IndexRootAttribute->
            Resident.DataLength <
                FIELD_OFFSET(IndexRootEx,
                             Header) +
                sizeof(IndexNodeHeader) ||
        IndexRootAttribute->
            Resident.DataOffset >
                IndexRootAttribute->Length ||
        IndexRootAttribute->
            Resident.DataLength >
                IndexRootAttribute->Length -
                IndexRootAttribute->
                    Resident.DataOffset)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    IndexRoot =
        reinterpret_cast<PIndexRootEx>(
            GetResidentDataPointer(
                IndexRootAttribute));
    IndexRecordSize =
        BytesPerIndexRecord(DiskVolume);
    if (IndexRecordSize == 0 ||
        IndexRoot->AttributeType !=
            TypeFileName ||
        IndexRoot->CollationRule !=
            ATTRDEF_COLLATION_FILENAME ||
        IndexRoot->BytesPerIndexRec !=
            IndexRecordSize ||
        IndexRoot->ClusPerIndexRec !=
            (UCHAR)
                DiskVolume->
                    ClustersPerIndexRecord)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    AllocationUnit =
        IndexRecordSize <
            BytesPerCluster(DiskVolume)
            ? DiskVolume->BytesPerSector
            : BytesPerCluster(DiskVolume);
    if (AllocationUnit == 0 ||
        IndexRecordSize % AllocationUnit != 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    Status = FindIndexInsertionPoint(
        DiskVolume,
        &IndexRoot->Header,
        IndexRootAttribute->
            Resident.DataLength -
            FIELD_OFFSET(IndexRootEx,
                         Header),
        &Name,
        &InsertionOffset,
        &ChildVcn,
        &Descend);
    if (!NT_SUCCESS(Status))
        goto Done;

    if (!Descend)
    {
        PIndexNodeHeader NewHeader;
        ULONG RootPrefix =
            FIELD_OFFSET(IndexRootEx, Header);
        ULONG NewTotal;

        if (IndexRoot->Header.TotalIndexSize >
                MAXULONG - EntryLength)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }
        NewTotal =
            IndexRoot->Header.TotalIndexSize +
            EntryLength;
        NewRootDataLength =
            RootPrefix + NewTotal;
        NewRootData =
            new(PagedPool, TAG_BTREE)
                UCHAR[NewRootDataLength];
        if (!NewRootData)
        {
            Status =
                STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        RtlZeroMemory(NewRootData,
                      NewRootDataLength);
        RtlCopyMemory(NewRootData,
                      IndexRoot,
                      RootPrefix +
                      IndexRoot->
                          Header.TotalIndexSize);
        NewHeader =
            &reinterpret_cast<PIndexRootEx>(
                NewRootData)->Header;
        SpliceEntryIntoHeader(
            NewHeader,
            InsertionOffset,
            NewEntry,
            EntryLength);
        NewHeader->AllocatedSize = NewTotal;

        Status = ReplaceIndexRootValue(
            DiskVolume,
            DirectoryFile,
            NewRootData,
            NewRootDataLength);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            /*
             * The base record cannot hold the grown root: promote the
             * composed entry list into the first index buffer.
             */
            Status = PushDownRoot(
                DiskVolume,
                DirectoryFile,
                IndexRoot,
                reinterpret_cast<PUCHAR>(
                    NewHeader) +
                    NewHeader->IndexOffset,
                NewHeader->TotalIndexSize -
                    NewHeader->IndexOffset,
                FALSE,
                IndexRecordSize,
                AllocationUnit);
        }
        if (!NT_SUCCESS(Status))
            goto Done;
        EntryCommitted = TRUE;
        goto TouchDirectory;
    }

    IndexAllocationAttribute =
        DirectoryFile->GetAttribute(
            TypeIndexAllocation,
            const_cast<PWSTR>(NtfsI30Name));
    BitmapAttribute =
        DirectoryFile->GetAttribute(
            TypeBitmap,
            const_cast<PWSTR>(NtfsI30Name));
    if (!IndexAllocationAttribute ||
        !IndexAllocationAttribute->
            IsNonResident ||
        !BitmapAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    IndexBufferData =
        new(PagedPool, TAG_BTREE)
            UCHAR[IndexRecordSize];
    if (!IndexBufferData)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    while (Descend)
    {
        PIndexBuffer NodeBuffer;

        if (VisitedCount ==
            RTL_NUMBER_OF(VisitedVcns))
        {
            Status =
                STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        for (ULONG Index = 0;
             Index < VisitedCount;
             Index++)
        {
            if (VisitedVcns[Index] ==
                ChildVcn)
            {
                Status =
                    STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
        }
        VisitedVcns[VisitedCount++] =
            ChildVcn;

        Status = ReadIndexNode(
            DiskVolume,
            DirectoryFile,
            IndexRecordSize,
            AllocationUnit,
            ChildVcn,
            IndexBufferData);
        if (!NT_SUCCESS(Status))
            goto Done;
        NodeBuffer =
            reinterpret_cast<PIndexBuffer>(
                IndexBufferData);

        Descend = FALSE;
        Status = FindIndexInsertionPoint(
            DiskVolume,
            &NodeBuffer->IndexHeader,
            IndexRecordSize -
                FIELD_OFFSET(IndexBuffer,
                             IndexHeader),
            &Name,
            &InsertionOffset,
            &ChildVcn,
            &Descend);
        if (!NT_SUCCESS(Status))
            goto Done;
        if (Descend)
            continue;

        if (NodeBuffer->
                IndexHeader.TotalIndexSize >
                    MAXULONG - EntryLength)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }
        if (NodeBuffer->
                IndexHeader.TotalIndexSize +
                    EntryLength >
            NodeBuffer->
                IndexHeader.AllocatedSize)
        {
            /*
             * The target leaf is full: split along the recorded path
             * and let the median climb toward the root.
             */
            Status = SplitAndPromote(
                DiskVolume,
                DirectoryFile,
                IndexRecordSize,
                AllocationUnit,
                VisitedVcns,
                VisitedCount,
                IndexBufferData,
                InsertionOffset,
                NewEntry,
                EntryLength);
            if (!NT_SUCCESS(Status))
                goto Done;
            EntryCommitted = TRUE;
            break;
        }
        SpliceEntryIntoHeader(
            &NodeBuffer->IndexHeader,
            InsertionOffset,
            NewEntry,
            EntryLength);

        Status = WriteIndexNode(
            DiskVolume,
            DirectoryFile,
            VisitedVcns[VisitedCount - 1],
            AllocationUnit,
            IndexRecordSize,
            IndexBufferData);
        if (!NT_SUCCESS(Status))
            goto Done;
        EntryCommitted = TRUE;
        break;
    }

TouchDirectory:
    if (EntryCommitted)
    {
        TimestampStatus =
            DirectoryFile->TouchDirectory();
        if (!NT_SUCCESS(TimestampStatus))
        {
            DPRINT1(
                "Directory entry was committed but "
                "timestamp update failed: 0x%lx.\n",
                TimestampStatus);
        }
        Status = STATUS_SUCCESS;
    }

Done:
    delete[] IndexBufferData;
    delete[] NewRootData;
    if (NewEntry)
        NtfsFreePool(NewEntry);
    return Status;
}

static void
CutEntryInNodeHeader(
    _Inout_ PIndexNodeHeader Header,
    _In_ ULONG EntryOffset,
    _In_ ULONG EntryLength)
{
    RtlMoveMemory(
        reinterpret_cast<PUCHAR>(Header) +
            EntryOffset,
        reinterpret_cast<PUCHAR>(Header) +
            EntryOffset + EntryLength,
        Header->TotalIndexSize -
            EntryOffset - EntryLength);
    Header->TotalIndexSize -= EntryLength;
}

/*
 * Copies only the key of an index entry: file reference, stream, and no
 * child slot. The caller reattaches a child through BuildPromotedEntry
 * when the key replaces an internal entry.
 */
static NTSTATUS
BuildKeyOnlyEntry(
    _In_ PIndexEntry Source,
    _Outptr_result_bytebuffer_(*KeyLength)
        PIndexEntry* KeyEntry,
    _Out_ PULONG KeyLength)
{
    PIndexEntry Entry;
    ULONG Length;

    *KeyEntry = NULL;
    *KeyLength = 0;
    if (Source->StreamLength == 0)
        return STATUS_FILE_CORRUPT_ERROR;
    Length = ALIGN_UP_BY(
        FIELD_OFFSET(IndexEntry, IndexStream) +
            Source->StreamLength,
        sizeof(ULONGLONG));
    Entry =
        reinterpret_cast<PIndexEntry>(
            NtfsAllocatePoolWithTag(
                PagedPool,
                Length,
                TAG_BTREE));
    if (!Entry)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Entry, Length);
    Entry->Data.Directory.IndexedFile =
        Source->Data.Directory.IndexedFile;
    Entry->EntryLength = (USHORT)Length;
    Entry->StreamLength = Source->StreamLength;
    RtlCopyMemory(Entry->IndexStream,
                  Source->IndexStream,
                  Source->StreamLength);
    *KeyEntry = Entry;
    *KeyLength = Length;
    return STATUS_SUCCESS;
}

/*
 * Frees the $I30 bitmap bits of a subtree that holds no keys at all: a
 * straight chain of END-only nodes left behind by removals.
 */
static NTSTATUS
ReleaseEndOnlyChain(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord DirectoryFile,
    _In_ ULONG RecordSize,
    _In_ ULONGLONG AllocationUnit,
    _In_ ULONGLONG ChainVcn)
{
    PUCHAR Image;
    ULONG Depth = 0;
    NTSTATUS Status = STATUS_SUCCESS;

    Image =
        new(PagedPool, TAG_BTREE)
            UCHAR[RecordSize];
    if (!Image)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (;;)
    {
        PIndexBuffer NodeBuffer;
        PIndexEntry Entry;
        BOOLEAN Internal;

        if (Depth++ > 64)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }
        Status = ReadIndexNode(
            DiskVolume,
            DirectoryFile,
            RecordSize,
            AllocationUnit,
            ChainVcn,
            Image);
        if (!NT_SUCCESS(Status))
            break;
        NodeBuffer =
            reinterpret_cast<PIndexBuffer>(Image);
        Entry =
            reinterpret_cast<PIndexEntry>(
                reinterpret_cast<PUCHAR>(
                    &NodeBuffer->IndexHeader) +
                NodeBuffer->
                    IndexHeader.IndexOffset);
        if (!NtfsIsDirectoryIndexEntryValid(
                Entry,
                NodeBuffer->
                    IndexHeader.TotalIndexSize -
                NodeBuffer->
                    IndexHeader.IndexOffset) ||
            !(Entry->Flags & INDEX_ENTRY_END))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }
        Internal =
            !!(Entry->Flags & INDEX_ENTRY_NODE);

        Status = SetIndexRecordBitmapBit(
            DirectoryFile,
            (ChainVcn * AllocationUnit) /
                RecordSize,
            FALSE);
        if (!NT_SUCCESS(Status))
            break;
        if (!Internal)
            break;
        ChainVcn = *GetSubnodeVCN(Entry);
    }

    delete[] Image;
    return Status;
}

/*
 * Removes and returns the collation maximum of one subtree as a key-only
 * entry. When the subtree carries no keys at all, Found stays FALSE and
 * nothing is modified; the caller releases the chain. An internal node
 * whose END subtree runs dry donates its own last entry and adopts that
 * entry's child as the new rightmost subtree.
 */
static NTSTATUS
RemoveMaxFromSubtree(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord DirectoryFile,
    _In_ ULONG RecordSize,
    _In_ ULONGLONG AllocationUnit,
    _In_ ULONGLONG Vcn,
    _In_ ULONG Depth,
    _Outptr_result_maybenull_
        PIndexEntry* KeyEntry,
    _Out_ PULONG KeyLength,
    _Out_ PBOOLEAN Found)
{
    PIndexBuffer NodeBuffer;
    PIndexEntry Entry;
    PIndexEntry LastReal = NULL;
    PIndexEntry EndEntry = NULL;
    PUCHAR Image = NULL;
    ULONG_PTR End;
    ULONG LastRealOffset = 0;
    BOOLEAN Internal;
    NTSTATUS Status;

    *KeyEntry = NULL;
    *KeyLength = 0;
    *Found = FALSE;
    if (Depth > 64)
        return STATUS_FILE_CORRUPT_ERROR;

    Image =
        new(PagedPool, TAG_BTREE)
            UCHAR[RecordSize];
    if (!Image)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = ReadIndexNode(
        DiskVolume,
        DirectoryFile,
        RecordSize,
        AllocationUnit,
        Vcn,
        Image);
    if (!NT_SUCCESS(Status))
        goto Done;
    NodeBuffer =
        reinterpret_cast<PIndexBuffer>(Image);

    Entry =
        reinterpret_cast<PIndexEntry>(
            reinterpret_cast<PUCHAR>(
                &NodeBuffer->IndexHeader) +
            NodeBuffer->IndexHeader.IndexOffset);
    End =
        reinterpret_cast<ULONG_PTR>(
            &NodeBuffer->IndexHeader) +
        NodeBuffer->IndexHeader.TotalIndexSize;
    while (reinterpret_cast<ULONG_PTR>(Entry) <
           End)
    {
        if (!NtfsIsDirectoryIndexEntryValid(
                Entry,
                (ULONG)(End -
                    reinterpret_cast<ULONG_PTR>(
                        Entry))))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        if (Entry->Flags & INDEX_ENTRY_END)
        {
            EndEntry = Entry;
            break;
        }
        LastReal = Entry;
        LastRealOffset =
            (ULONG)(
                reinterpret_cast<PUCHAR>(Entry) -
                reinterpret_cast<PUCHAR>(
                    &NodeBuffer->IndexHeader));
        Entry =
            reinterpret_cast<PIndexEntry>(
                reinterpret_cast<PUCHAR>(Entry) +
                Entry->EntryLength);
    }
    if (!EndEntry)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    Internal =
        !!(NodeBuffer->IndexHeader.Flags &
           NTFS_INDEX_HEADER_LARGE);

    if (Internal)
    {
        ULONGLONG RightVcn =
            *GetSubnodeVCN(EndEntry);

        Status = RemoveMaxFromSubtree(
            DiskVolume,
            DirectoryFile,
            RecordSize,
            AllocationUnit,
            RightVcn,
            Depth + 1,
            KeyEntry,
            KeyLength,
            Found);
        if (!NT_SUCCESS(Status) || *Found)
            goto Done;

        if (!LastReal)
        {
            /* This level holds no key either. */
            goto Done;
        }

        /*
         * The rightmost subtree is a bare END chain: free it, donate the
         * last entry's key upward, and let its child become the new
         * rightmost subtree.
         */
        Status = ReleaseEndOnlyChain(
            DiskVolume,
            DirectoryFile,
            RecordSize,
            AllocationUnit,
            RightVcn);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = BuildKeyOnlyEntry(
            LastReal,
            KeyEntry,
            KeyLength);
        if (!NT_SUCCESS(Status))
            goto Done;
        {
            ULONGLONG DonatedChild =
                *GetSubnodeVCN(LastReal);
            ULONG CutLength =
                LastReal->EntryLength;

            CutEntryInNodeHeader(
                &NodeBuffer->IndexHeader,
                LastRealOffset,
                CutLength);
            EndEntry =
                reinterpret_cast<PIndexEntry>(
                    reinterpret_cast<PUCHAR>(
                        &NodeBuffer->
                            IndexHeader) +
                    NodeBuffer->
                        IndexHeader.
                            TotalIndexSize -
                    (FIELD_OFFSET(IndexEntry,
                                  IndexStream) +
                     sizeof(ULONGLONG)));
            *GetSubnodeVCN(EndEntry) =
                DonatedChild;
        }
        Status = WriteIndexNode(
            DiskVolume,
            DirectoryFile,
            Vcn,
            AllocationUnit,
            RecordSize,
            Image);
        if (NT_SUCCESS(Status))
            *Found = TRUE;
        goto Done;
    }

    if (!LastReal)
        goto Done;
    Status = BuildKeyOnlyEntry(
        LastReal,
        KeyEntry,
        KeyLength);
    if (!NT_SUCCESS(Status))
        goto Done;
    CutEntryInNodeHeader(
        &NodeBuffer->IndexHeader,
        LastRealOffset,
        LastReal->EntryLength);
    Status = WriteIndexNode(
        DiskVolume,
        DirectoryFile,
        Vcn,
        AllocationUnit,
        RecordSize,
        Image);
    if (NT_SUCCESS(Status))
        *Found = TRUE;

Done:
    if (!NT_SUCCESS(Status) || !*Found)
    {
        if (*KeyEntry)
        {
            NtfsFreePool(*KeyEntry);
            *KeyEntry = NULL;
            *KeyLength = 0;
        }
    }
    delete[] Image;
    return Status;
}

NTSTATUS
Directory::RemoveFileFromDirectory(
    _In_ PFileRecord DirectoryFile,
    _In_ ULONGLONG FileReference,
    _In_ PUNICODE_STRING Name)
{
    PAttribute IndexRootAttribute;
    PIndexRootEx IndexRoot;
    PIndexEntry Matched;
    PIndexEntry Replacement = NULL;
    PIndexEntry MaxKey = NULL;
    PUCHAR NodeImage = NULL;
    PUCHAR RootValue = NULL;
    PUCHAR Scratch = NULL;
    ULONG MatchOffset;
    ULONG MaxKeyLength = 0;
    ULONG ReplacementLength = 0;
    ULONG IndexRecordSize;
    ULONG VisitedCount = 0;
    ULONGLONG ChildVcn;
    ULONGLONG RemovedChild;
    ULONGLONG VisitedVcns[64];
    ULONGLONG AllocationUnit;
    BOOLEAN Descend;
    BOOLEAN FoundExact = FALSE;
    BOOLEAN MatchedInRoot;
    BOOLEAN MatchedInternal;
    BOOLEAN MaxFound = FALSE;
    BOOLEAN EntryCommitted = FALSE;
    NTSTATUS TimestampStatus;
    NTSTATUS Status;

    if (!DiskVolume || !DirectoryFile ||
        !DirectoryFile->Header ||
        !DirectoryFile->Data ||
        !Name || Name->Length == 0 ||
        !(DirectoryFile->Header->Flags &
          FR_IS_DIRECTORY) ||
        DiskVolume->IsReadOnly)
    {
        return STATUS_INVALID_PARAMETER;
    }

    IndexRootAttribute =
        DirectoryFile->GetAttribute(
            TypeIndexRoot,
            const_cast<PWSTR>(NtfsI30Name));
    if (!IndexRootAttribute ||
        IndexRootAttribute->IsNonResident ||
        IndexRootAttribute->
            Resident.DataLength <
                FIELD_OFFSET(IndexRootEx,
                             Header) +
                sizeof(IndexNodeHeader))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    IndexRoot =
        reinterpret_cast<PIndexRootEx>(
            GetResidentDataPointer(
                IndexRootAttribute));
    IndexRecordSize =
        BytesPerIndexRecord(DiskVolume);
    if (IndexRecordSize == 0 ||
        IndexRoot->AttributeType !=
            TypeFileName ||
        IndexRoot->CollationRule !=
            ATTRDEF_COLLATION_FILENAME)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    AllocationUnit =
        IndexRecordSize <
            BytesPerCluster(DiskVolume)
            ? DiskVolume->BytesPerSector
            : BytesPerCluster(DiskVolume);
    if (AllocationUnit == 0 ||
        IndexRecordSize % AllocationUnit != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = FindIndexInsertionPoint(
        DiskVolume,
        &IndexRoot->Header,
        IndexRootAttribute->
            Resident.DataLength -
            FIELD_OFFSET(IndexRootEx, Header),
        Name,
        &MatchOffset,
        &ChildVcn,
        &Descend,
        &FoundExact);
    if (!NT_SUCCESS(Status))
        return Status;
    MatchedInRoot = FoundExact;

    NodeImage =
        new(PagedPool, TAG_BTREE)
            UCHAR[IndexRecordSize];
    if (!NodeImage)
        return STATUS_INSUFFICIENT_RESOURCES;

    while (!FoundExact)
    {
        PIndexBuffer NodeBuffer;

        if (!Descend)
        {
            Status = STATUS_NOT_FOUND;
            goto Done;
        }
        if (VisitedCount ==
            RTL_NUMBER_OF(VisitedVcns))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        for (ULONG Index = 0;
             Index < VisitedCount;
             Index++)
        {
            if (VisitedVcns[Index] == ChildVcn)
            {
                Status =
                    STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
        }
        VisitedVcns[VisitedCount++] = ChildVcn;

        Status = ReadIndexNode(
            DiskVolume,
            DirectoryFile,
            IndexRecordSize,
            AllocationUnit,
            ChildVcn,
            NodeImage);
        if (!NT_SUCCESS(Status))
            goto Done;
        NodeBuffer =
            reinterpret_cast<PIndexBuffer>(
                NodeImage);
        Descend = FALSE;
        Status = FindIndexInsertionPoint(
            DiskVolume,
            &NodeBuffer->IndexHeader,
            IndexRecordSize -
                FIELD_OFFSET(IndexBuffer,
                             IndexHeader),
            Name,
            &MatchOffset,
            &ChildVcn,
            &Descend,
            &FoundExact);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    /* MatchOffset addresses the entry inside its owning node header. */
    Matched = MatchedInRoot
        ? reinterpret_cast<PIndexEntry>(
              reinterpret_cast<PUCHAR>(
                  &IndexRoot->Header) +
              MatchOffset)
        : reinterpret_cast<PIndexEntry>(
              reinterpret_cast<PUCHAR>(
                  &reinterpret_cast<PIndexBuffer>(
                      NodeImage)->IndexHeader) +
              MatchOffset);
    if (Matched->Data.Directory.IndexedFile !=
        FileReference)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }
    MatchedInternal =
        !!(Matched->Flags & INDEX_ENTRY_NODE);
    RemovedChild = MatchedInternal
        ? *GetSubnodeVCN(Matched)
        : 0;

    if (!MatchedInternal)
    {
        /* Leaf removal: cut the entry where it stands. */
        if (MatchedInRoot)
        {
            ULONG RootPrefix =
                FIELD_OFFSET(IndexRootEx,
                             Header);
            ULONG ValueLength =
                IndexRootAttribute->
                    Resident.DataLength;

            RootValue =
                new(PagedPool, TAG_BTREE)
                    UCHAR[ValueLength];
            if (!RootValue)
            {
                Status =
                    STATUS_INSUFFICIENT_RESOURCES;
                goto Done;
            }
            RtlCopyMemory(RootValue,
                          IndexRoot,
                          ValueLength);
            CutEntryInNodeHeader(
                &reinterpret_cast<PIndexRootEx>(
                    RootValue)->Header,
                MatchOffset,
                Matched->EntryLength);
            reinterpret_cast<PIndexRootEx>(
                RootValue)->
                    Header.AllocatedSize =
                reinterpret_cast<PIndexRootEx>(
                    RootValue)->
                        Header.TotalIndexSize;
            Status = ReplaceIndexRootValue(
                DiskVolume,
                DirectoryFile,
                RootValue,
                RootPrefix +
                    reinterpret_cast<
                        PIndexRootEx>(
                            RootValue)->
                        Header.TotalIndexSize);
        }
        else
        {
            PIndexBuffer NodeBuffer =
                reinterpret_cast<PIndexBuffer>(
                    NodeImage);

            CutEntryInNodeHeader(
                &NodeBuffer->IndexHeader,
                MatchOffset,
                Matched->EntryLength);
            Status = WriteIndexNode(
                DiskVolume,
                DirectoryFile,
                VisitedVcns[VisitedCount - 1],
                AllocationUnit,
                IndexRecordSize,
                NodeImage);
        }
        if (!NT_SUCCESS(Status))
            goto Done;
        EntryCommitted = TRUE;
        goto TouchDirectory;
    }

    /*
     * Internal removal: pull the collation maximum out of the entry's
     * subtree as the replacement key. A keyless subtree is released
     * whole and the entry simply disappears.
     */
    Status = RemoveMaxFromSubtree(
        DiskVolume,
        DirectoryFile,
        IndexRecordSize,
        AllocationUnit,
        RemovedChild,
        0,
        &MaxKey,
        &MaxKeyLength,
        &MaxFound);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (MaxFound)
    {
        Status = BuildPromotedEntry(
            reinterpret_cast<PUCHAR>(MaxKey),
            MaxKeyLength,
            FALSE,
            RemovedChild,
            &Replacement,
            &ReplacementLength);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    else
    {
        Status = ReleaseEndOnlyChain(
            DiskVolume,
            DirectoryFile,
            IndexRecordSize,
            AllocationUnit,
            RemovedChild);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    if (MatchedInRoot)
    {
        ULONG ListBytes;
        ULONG ScratchCapacity =
            IndexRecordSize + 4096;

        /*
         * The subtree writes above may have moved record attributes;
         * refresh the root and relocate the matched entry.
         */
        IndexRootAttribute =
            DirectoryFile->GetAttribute(
                TypeIndexRoot,
                const_cast<PWSTR>(NtfsI30Name));
        if (!IndexRootAttribute ||
            IndexRootAttribute->IsNonResident)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        IndexRoot =
            reinterpret_cast<PIndexRootEx>(
                GetResidentDataPointer(
                    IndexRootAttribute));
        Status = FindChildLinkOffset(
            &IndexRoot->Header,
            RemovedChild,
            &MatchOffset);
        if (!NT_SUCCESS(Status))
            goto Done;
        Matched =
            reinterpret_cast<PIndexEntry>(
                reinterpret_cast<PUCHAR>(
                    &IndexRoot->Header) +
                MatchOffset);

        Scratch =
            new(PagedPool, TAG_BTREE)
                UCHAR[ScratchCapacity];
        if (!Scratch)
        {
            Status =
                STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        {
            ULONG ListStart =
                IndexRoot->Header.IndexOffset;
            ULONG ListEnd =
                IndexRoot->Header.TotalIndexSize;
            ULONG Prefix =
                MatchOffset - ListStart;
            ULONG Tail =
                ListEnd - MatchOffset -
                Matched->EntryLength;

            if (ListEnd - ListStart -
                    Matched->EntryLength +
                    ReplacementLength >
                ScratchCapacity)
            {
                Status =
                    STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            RtlCopyMemory(
                Scratch,
                reinterpret_cast<PUCHAR>(
                    &IndexRoot->Header) +
                    ListStart,
                Prefix);
            if (Replacement)
            {
                RtlCopyMemory(
                    Scratch + Prefix,
                    Replacement,
                    ReplacementLength);
            }
            RtlCopyMemory(
                Scratch + Prefix +
                    ReplacementLength,
                reinterpret_cast<PUCHAR>(
                    &IndexRoot->Header) +
                    MatchOffset +
                    Matched->EntryLength,
                Tail);
            ListBytes =
                Prefix + ReplacementLength +
                Tail;
        }

        Status = BuildRootValueFromList(
            IndexRoot,
            Scratch,
            ListBytes,
            TRUE,
            &RootValue,
            &ReplacementLength /* reused */);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = ReplaceIndexRootValue(
            DiskVolume,
            DirectoryFile,
            RootValue,
            ReplacementLength);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            Status = PushDownRoot(
                DiskVolume,
                DirectoryFile,
                IndexRoot,
                Scratch,
                ListBytes,
                TRUE,
                IndexRecordSize,
                AllocationUnit);
        }
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    else
    {
        PIndexBuffer NodeBuffer =
            reinterpret_cast<PIndexBuffer>(
                NodeImage);

        CutEntryInNodeHeader(
            &NodeBuffer->IndexHeader,
            MatchOffset,
            Matched->EntryLength);
        if (Replacement)
        {
            Status = SplitAndPromote(
                DiskVolume,
                DirectoryFile,
                IndexRecordSize,
                AllocationUnit,
                VisitedVcns,
                VisitedCount,
                NodeImage,
                MatchOffset,
                Replacement,
                ReplacementLength);
        }
        else
        {
            Status = WriteIndexNode(
                DiskVolume,
                DirectoryFile,
                VisitedVcns[VisitedCount - 1],
                AllocationUnit,
                IndexRecordSize,
                NodeImage);
        }
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    EntryCommitted = TRUE;

TouchDirectory:
    if (EntryCommitted)
    {
        TimestampStatus =
            DirectoryFile->TouchDirectory();
        if (!NT_SUCCESS(TimestampStatus))
        {
            DPRINT1(
                "Directory entry was removed but "
                "timestamp update failed: 0x%lx.\n",
                TimestampStatus);
        }
        Status = STATUS_SUCCESS;
    }

Done:
    if (MaxKey)
        NtfsFreePool(MaxKey);
    if (Replacement)
        NtfsFreePool(Replacement);
    delete[] Scratch;
    delete[] RootValue;
    delete[] NodeImage;
    return Status;
}
