/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

typedef struct _SHORT_NAME_PAIR
{
    ULONGLONG FileReference;
    PBTreeKey LongNameKey;
    PBTreeKey ShortNameKey;
    BOOLEAN Occupied;
} SHORT_NAME_PAIR, *PSHORT_NAME_PAIR;

static ULONG
HashFileReference(_In_ ULONGLONG FileReference)
{
    return (ULONG)(FileReference ^ (FileReference >> 32)) * 2654435761UL;
}

BOOLEAN
NtfsIsDirectoryIndexEntryValid(_In_ PIndexEntry Entry,
                               _In_ ULONG Remaining)
{
    ULONG HeaderSize = FIELD_OFFSET(IndexEntry, IndexStream);
    ULONG PayloadSize;
    ULONG NameBytes;
    PFileNameEx FileName;

    if (Remaining < HeaderSize ||
        Entry->EntryLength < HeaderSize ||
        (Entry->EntryLength & 7) != 0 ||
        Entry->EntryLength > Remaining ||
        (Entry->Flags & ~(INDEX_ENTRY_NODE | INDEX_ENTRY_END)) != 0)
    {
        return FALSE;
    }

    PayloadSize = Entry->EntryLength - HeaderSize;
    if (Entry->Flags & INDEX_ENTRY_NODE)
    {
        if (PayloadSize < sizeof(ULONGLONG))
            return FALSE;
        PayloadSize -= sizeof(ULONGLONG);
    }

    if (Entry->StreamLength > PayloadSize)
        return FALSE;

    if (Entry->Flags & INDEX_ENTRY_END)
        return Entry->StreamLength == 0;

    if (Entry->StreamLength < FIELD_OFFSET(FileNameEx, Name))
        return FALSE;

    FileName = (PFileNameEx)Entry->IndexStream;
    NameBytes = (ULONG)FileName->NameLength * sizeof(WCHAR);
    return NameBytes <= Entry->StreamLength -
                        (ULONG)FIELD_OFFSET(FileNameEx, Name);
}

PBTreeKey
Directory::AllocateKey()
{
    PDIRECTORY_KEY_BLOCK Block = KeyBlocks;
    PBTreeKey Key;

    if (!Block || Block->Used == DIRECTORY_KEY_BLOCK_CAPACITY)
    {
        Block = new(PagedPool, TAG_BTREE) DIRECTORY_KEY_BLOCK();
        if (!Block)
            return NULL;

        Block->Next = KeyBlocks;
        KeyBlocks = Block;
    }

    Key = &Block->Keys[Block->Used++];
    RtlZeroMemory(Key, sizeof(*Key));
    Key->Flags = BTREE_KEY_ARENA_OBJECT;
    return Key;
}

void
Directory::FreeKeyBlocks()
{
    while (KeyBlocks)
    {
        PDIRECTORY_KEY_BLOCK Block = KeyBlocks;
        KeyBlocks = Block->Next;
        delete Block;
    }
}

NTSTATUS
Directory::CreateNode(
    _Inout_ PUCHAR BitmapData,
    _In_ ULONG BitmapLength,
    _Inout_ PBTreeKey ParentNodeKey)
{
    NTSTATUS Status;
    PBTreeNode NewNode;
    PBTreeKey CurrentKey, NextKey;
    PIndexBuffer NodeBuffer;
    PIndexEntry CurrentEntry;
    PULONGLONG VCN;
    ULONGLONG AllocationOffset;
    ULONGLONG IndexRecordNumber;
    ULONG BitmapByte;
    ULONG IndexRecordSize;
    ULONG_PTR EndOfIndexBuffer;
    UCHAR BitmapMask;
    BOOLEAN FoundEndEntry;

    // Get VCN from the end of the node entry
    VCN = GetSubnodeVCN(ParentNodeKey->Entry);
    IndexRecordSize = BytesPerIndexRecord(DiskVolume);
    AllocationOffset = GetAllocationOffsetFromVCN(*VCN);

    /* A child reference is valid only when its index buffer is allocated in
     * the named index's $BITMAP. Clear the bit in our private bitmap copy to
     * reject duplicate references and recursive cycles.
     */
    if (!BitmapData ||
        IndexRecordSize == 0 ||
        AllocationOffset % IndexRecordSize != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRecordNumber = AllocationOffset / IndexRecordSize;
    if ((IndexRecordNumber >> 3) >= BitmapLength)
        return STATUS_FILE_CORRUPT_ERROR;

    BitmapByte = (ULONG)(IndexRecordNumber >> 3);
    BitmapMask = (UCHAR)(1 << (IndexRecordNumber & 7));
    if (!(BitmapData[BitmapByte] & BitmapMask))
        return STATUS_FILE_CORRUPT_ERROR;
    BitmapData[BitmapByte] &= ~BitmapMask;

    if (AllocationOffset > IndexAllocationLength ||
        IndexRecordSize > IndexAllocationLength - AllocationOffset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    NodeBuffer = (PIndexBuffer)(IndexAllocationData + AllocationOffset);
    Status = NtfsApplyFixup(&NodeBuffer->RecordHeader,
                            IndexRecordSize,
                            DiskVolume->BytesPerSector);
    if (!NT_SUCCESS(Status) ||
        RtlCompareMemory(NodeBuffer->RecordHeader.TypeID, "INDX", 4) != 4 ||
        NodeBuffer->VCN != *VCN ||
        NodeBuffer->IndexHeader.IndexOffset >
            NodeBuffer->IndexHeader.TotalIndexSize ||
        (ULONG)FIELD_OFFSET(IndexBuffer, IndexHeader) +
            NodeBuffer->IndexHeader.TotalIndexSize > IndexRecordSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    // Create the new node and first key.
    NewNode = new(PagedPool, TAG_BTREE) BTreeNode();
    if (!NewNode)
        return STATUS_INSUFFICIENT_RESOURCES;

    CurrentKey = AllocateKey();
    if (!CurrentKey)
    {
        delete NewNode;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    NewNode->FirstKey = CurrentKey;
    ParentNodeKey->ChildNode = NewNode;

    // Walk through the index and create keys for all the entries
    CurrentEntry = (PIndexEntry)((ULONG_PTR)(&NodeBuffer->IndexHeader)
                                 + NodeBuffer->IndexHeader.IndexOffset);
    EndOfIndexBuffer = (ULONG_PTR)(&NodeBuffer->IndexHeader) + NodeBuffer->IndexHeader.TotalIndexSize;
    FoundEndEntry = FALSE;

    while ((ULONG_PTR)CurrentEntry < EndOfIndexBuffer)
    {
        if (!NtfsIsDirectoryIndexEntryValid(
                CurrentEntry,
                (ULONG)(EndOfIndexBuffer - (ULONG_PTR)CurrentEntry)))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        /*
         * IndexAllocationData remains owned by this Directory, so keys can
         * point directly into it instead of allocating and copying every
         * entry independently.
         */
        CurrentKey->Entry = CurrentEntry;
        CurrentKey->Flags |= BTREE_KEY_BORROWED_ENTRY;
        CurrentKey->ParentNodeKey = ParentNodeKey;

        if (CurrentKey->Entry->Flags & INDEX_ENTRY_NODE)
        {
            Status = CreateNode(BitmapData,
                                BitmapLength,
                                CurrentKey);

            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Failed to create subnode!\n");
                return Status;
            }
        }

        if (!(CurrentEntry->Flags & INDEX_ENTRY_END))
        {
            // Create the next key
            NextKey = AllocateKey();
            if (!NextKey)
            {
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            CurrentKey->NextKey = NextKey;

            // Advance to next entry
            CurrentEntry = (PIndexEntry)((ULONG_PTR)CurrentEntry
                                         + CurrentEntry->EntryLength);
            CurrentKey = NextKey;
        }
        else
        {
            CurrentKey->NextKey = NULL;
            FoundEndEntry = TRUE;
            break;
        }
    }

    if (!FoundEndEntry)
        return STATUS_FILE_CORRUPT_ERROR;

    NewNode->VCN = *VCN;
    return STATUS_SUCCESS;
}

NTSTATUS
Directory::CreateRootNode(_In_  PFileRecord File,
                          _Out_ PBTreeNode *NewRootNode)
{
    NTSTATUS Status;
    PAttribute IndexRootAttribute, IndexAllocationAttribute, BitmapAttribute;
    PIndexRootEx IndexRootData;
    PBTreeNode RootNode;
    PBTreeKey CurrentKey, NextKey;
    PIndexEntry CurrentEntry;
    ULONG_PTR EndOfIndexRootData;
    BOOLEAN FoundEndEntry;
    PUCHAR BitmapData = NULL;
    ULONG BitmapLength = 0;

    *NewRootNode = NULL;

    // Get $INDEX_ROOT attribute.
    IndexRootAttribute = File->GetAttribute(
        TypeIndexRoot,
        const_cast<PWSTR>(L"$I30"));

    // If it's a directory, it must have an index root.
    if (!IndexRootAttribute || IndexRootAttribute->IsNonResident)
        return STATUS_FILE_CORRUPT_ERROR;

    if (IndexRootAttribute->Resident.DataLength <
        FIELD_OFFSET(IndexRootEx, Header) + sizeof(IndexNodeHeader))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /* Set up pointers.
     * Note: IndexAllocationAttribute may be null. That is okay!
     */
    IndexRootData = (PIndexRootEx)GetResidentDataPointer(IndexRootAttribute);
    IndexAllocationAttribute = File->GetAttribute(
        TypeIndexAllocation,
        const_cast<PWSTR>(L"$I30"));
    BitmapAttribute = File->GetAttribute(
        TypeBitmap,
        const_cast<PWSTR>(L"$I30"));
    EndOfIndexRootData = ((ULONG_PTR)IndexRootData) +
                         (FIELD_OFFSET(IndexRootEx, Header)) +
                         (IndexRootData->Header.TotalIndexSize);

    // Make sure we won't try reading past the attribute-end
    if (((ULONG)FIELD_OFFSET(IndexRootEx, Header) +
         IndexRootData->Header.TotalIndexSize) >
        IndexRootAttribute->Resident.DataLength ||
        IndexRootData->Header.IndexOffset >
            IndexRootData->Header.TotalIndexSize)
    {
        DPRINT1("Filesystem corruption detected!\n");
        return STATUS_FILE_CORRUPT_ERROR;
    }

    // Initialize variables
    RootNode = new(PagedPool, TAG_BTREE) BTreeNode();
    if (!RootNode)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failed;
    }

    CurrentKey = AllocateKey();
    if (!CurrentKey)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failed;
    }

    RootNode->FirstKey = CurrentKey;

    CurrentEntry = (PIndexEntry)(((ULONG_PTR)IndexRootData) +
                                 (FIELD_OFFSET(IndexRootEx, Header)) +
                                 (IndexRootData->Header.IndexOffset));
    FoundEndEntry = FALSE;

    while ((ULONG_PTR)CurrentEntry < EndOfIndexRootData)
    {
        if (!NtfsIsDirectoryIndexEntryValid(
                CurrentEntry,
                (ULONG)(EndOfIndexRootData - (ULONG_PTR)CurrentEntry)))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Failed;
        }

        // Create current entry
        CurrentKey->Entry = (PIndexEntry)NtfsAllocatePoolWithTag(NonPagedPool,
                                                                 CurrentEntry->EntryLength,
                                                                 TAG_NTFS);
        if (!CurrentKey->Entry)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Failed;
        }

        // Copy the current entry to its key
        RtlCopyMemory(CurrentKey->Entry,
                      CurrentEntry,
                      CurrentEntry->EntryLength);

        if (CurrentEntry->Flags & INDEX_ENTRY_NODE)
        {
            if (!IndexAllocationAttribute ||
                !IndexAllocationAttribute->IsNonResident ||
                !BitmapAttribute)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Failed;
            }

            if (!IndexAllocationData)
            {
                Status = File->ReadAttributeAlloc(
                    IndexAllocationAttribute,
                    &IndexAllocationData,
                    &IndexAllocationLength);
                if (!NT_SUCCESS(Status) ||
                    !IndexAllocationData ||
                    IndexAllocationLength == 0)
                {
                    if (NT_SUCCESS(Status))
                        Status = STATUS_FILE_CORRUPT_ERROR;
                    goto Failed;
                }

                Status = File->ReadAttributeAlloc(BitmapAttribute,
                                                  &BitmapData,
                                                  &BitmapLength);
                if (!NT_SUCCESS(Status) ||
                    !BitmapData ||
                    BitmapLength == 0)
                {
                    if (NT_SUCCESS(Status))
                        Status = STATUS_FILE_CORRUPT_ERROR;
                    goto Failed;
                }
            }

            // Create child node
            Status = CreateNode(BitmapData,
                                BitmapLength,
                                CurrentKey);

            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Failed to create node!\n");
                goto Failed;
            }
        }

        if (!(CurrentEntry->Flags & INDEX_ENTRY_END))
        {
            // Create next key
            NextKey = AllocateKey();
            if (!NextKey)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Failed;
            }
            CurrentKey->NextKey = NextKey;

            // Advance to the next entry
            CurrentEntry = (PIndexEntry)((ULONG_PTR)CurrentEntry +
                                         CurrentEntry->EntryLength);
            CurrentKey = NextKey;
        }

        else
        {
            // We've copied the last entry.
            FoundEndEntry = TRUE;
            break;
        }
    }

    if (!FoundEndEntry)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failed;
    }

    delete[] BitmapData;
    *NewRootNode = RootNode;
    return STATUS_SUCCESS;
Failed:
    delete[] BitmapData;
    delete[] IndexAllocationData;
    IndexAllocationData = NULL;
    IndexAllocationLength = 0;
    NtfsDestroyBTreeNode(RootNode);
    return Status;
}

NTSTATUS
Directory::LoadDirectory(_In_ PFileRecord File)
{
    NTSTATUS Status;
    PBTreeKey SearchKey;
    PSHORT_NAME_PAIR PairTable;
    ULONG EntryCount, PairIndex, PairTableSize, ShortNameCount;
    ULONGLONG FileReference;
    // This only works on files that are directories.
    ASSERT(File->Header->Flags & FR_IS_DIRECTORY);

    // Get the root node for the directory
    Status = CreateRootNode(File, &RootNode);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Failed to create root node!\n");
        return Status;
    }

    CurrentKey = GetFirstKey(RootNode);
    EntryCount = 0;
    ShortNameCount = 0;

    /* The namespace byte is the on-disk authority for DOS aliases. Count and
     * mark entries in one pass; no filename-shape heuristics are needed.
     */
    for (SearchKey = CurrentKey;
         SearchKey;
         SearchKey = GetNextKey(SearchKey))
    {
        if (!(SearchKey->Entry->Flags & INDEX_ENTRY_END))
        {
            EntryCount++;
            if (GetFileName(SearchKey)->NameType == NAME_TYPE_DOS)
            {
                SearchKey->Flags |= DIR_KEY_8DOT3;
                ShortNameCount++;
            }
        }
    }

    if (ShortNameCount == 0)
        return STATUS_SUCCESS;

    /* Build an FRN-keyed table so each Win32 entry retains its DOS alias.
     * Enumeration can then obtain the alias in O(1).
     */
    PairTableSize = 8;
    while (EntryCount <= MAXULONG / 2 &&
           PairTableSize < EntryCount * 2 &&
           PairTableSize <= MAXULONG / 2)
    {
        PairTableSize *= 2;
    }

    PairTable = new(PagedPool, TAG_BTREE) SHORT_NAME_PAIR[PairTableSize];
    if (!PairTable)
    {
        DPRINT1("Unable to cache directory short-name pairs.\n");
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(PairTable, PairTableSize * sizeof(*PairTable));
    for (SearchKey = CurrentKey;
         SearchKey;
         SearchKey = GetNextKey(SearchKey))
    {
        if (SearchKey->Entry->Flags & INDEX_ENTRY_END)
            continue;

        FileReference = FileRef(SearchKey);
        PairIndex = HashFileReference(FileReference) & (PairTableSize - 1);
        while (PairTable[PairIndex].Occupied &&
               PairTable[PairIndex].FileReference != FileReference)
        {
            PairIndex = (PairIndex + 1) & (PairTableSize - 1);
        }

        PairTable[PairIndex].Occupied = TRUE;
        PairTable[PairIndex].FileReference = FileReference;
        if (GetFileName(SearchKey)->NameType == NAME_TYPE_DOS)
            PairTable[PairIndex].ShortNameKey = SearchKey;
        else if (GetFileName(SearchKey)->NameType == NAME_TYPE_WIN32)
            PairTable[PairIndex].LongNameKey = SearchKey;
    }

    for (PairIndex = 0; PairIndex < PairTableSize; PairIndex++)
    {
        if (PairTable[PairIndex].LongNameKey)
        {
            PairTable[PairIndex].LongNameKey->ShortNameKey =
                PairTable[PairIndex].ShortNameKey;
        }
    }

    delete[] PairTable;
    return Status;
}

BOOLEAN
Directory::DoesFileNameMatch(PUNICODE_STRING NameFilter,
                             PBTreeKey Key,
                             BOOLEAN IgnoreCase)
{
    UNICODE_STRING FileNameString;
    PFileNameEx FileNameData;

    if (Key->Entry->Flags & INDEX_ENTRY_END)
    {
        // This is a dummy key, it will not match with any file.
        return FALSE;
    }

    FileNameData = GetFileName(Key);
    FileNameString = NtfsMakeCountedUnicodeString(
        FileNameData->Name,
        FileNameData->NameLength * sizeof(WCHAR));

    /* The search entry points uppercase the expression once with this
     * volume's $UpCase table. Use the same table for the candidate name.
     */
    return NtfsIsNameInExpression(NameFilter,
                                  &FileNameString,
                                  IgnoreCase,
                                  DiskVolume->GetUpcaseTable());
}

PBTreeKey
Directory::GetShortNameKey(_In_ PBTreeKey Key)
{
    if (!Key || Key->Entry->Flags & INDEX_ENTRY_END)
        return NULL;

    return Key->ShortNameKey;
}

Directory::Directory(_In_ PVolume DiskVolume)
{
    this->DiskVolume = DiskVolume;
    RootNode = NULL;
    CurrentKey = NULL;
}

Directory::~Directory()
{
    /*
     * Borrowed index entries point into IndexAllocationData, so destroy the
     * key tree before releasing the shared allocation buffer. Null RootNode
     * keeps the BTree base destructor from visiting it a second time.
     */
    NtfsDestroyBTreeNode(RootNode);
    RootNode = NULL;
    CurrentKey = NULL;
    FreeKeyBlocks();
    delete[] IndexAllocationData;
    IndexAllocationData = NULL;
    IndexAllocationLength = 0;
}
