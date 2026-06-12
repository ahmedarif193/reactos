/*
 * PROJECT:         ReactOS Runtime Library
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Dynamic hash table implementation
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

#define RTLP_HASH_BASE_SIZE 128
#define RTLP_HASH_MAX_BLOCKS 1024
#define RTLP_HASH_TAG 'bHtR'

typedef struct _RTLP_HASH_DIRECTORY
{
    LIST_ENTRY Enumerators;
    PLIST_ENTRY Blocks[RTLP_HASH_MAX_BLOCKS];
} RTLP_HASH_DIRECTORY, *PRTLP_HASH_DIRECTORY;

typedef struct _RTLP_HASH_ENUM_LINK
{
    LIST_ENTRY Linkage;
    PRTL_DYNAMIC_HASH_TABLE_ENUMERATOR Enumerator;
} RTLP_HASH_ENUM_LINK, *PRTLP_HASH_ENUM_LINK;

/* PRIVATE FUNCTIONS ********************************************************/

static
PLIST_ENTRY
RtlpHashTableBucket(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _In_ ULONG Index)
{
    PRTLP_HASH_DIRECTORY Directory = HashTable->Directory;

    return &Directory->Blocks[Index / RTLP_HASH_BASE_SIZE][Index % RTLP_HASH_BASE_SIZE];
}

static
ULONG
RtlpHashTableIndex(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _In_ ULONG_PTR Signature)
{
    ULONG Index = (ULONG)(Signature & HashTable->DivisorMask);

    if (Index < HashTable->Pivot)
        Index = (ULONG)(Signature & (HashTable->DivisorMask * 2 + 1));
    return Index;
}

static
BOOLEAN
RtlpHashTableEntryIsMarker(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _In_ PLIST_ENTRY Entry)
{
    PRTLP_HASH_DIRECTORY Directory = HashTable->Directory;
    PLIST_ENTRY Link;

    if (HashTable->NumEnumerators == 0)
        return FALSE;
    for (Link = Directory->Enumerators.Flink; Link != &Directory->Enumerators; Link = Link->Flink)
    {
        PRTLP_HASH_ENUM_LINK EnumLink = CONTAINING_RECORD(Link, RTLP_HASH_ENUM_LINK, Linkage);

        if (&EnumLink->Enumerator->HashEntry.Linkage == Entry)
            return TRUE;
    }
    return FALSE;
}

static
PRTL_DYNAMIC_HASH_TABLE_ENTRY
RtlpHashTableFindNext(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _In_ PLIST_ENTRY ChainHead,
    _In_ PLIST_ENTRY First,
    _In_ ULONG_PTR Signature)
{
    PLIST_ENTRY Link;

    for (Link = First; Link != ChainHead; Link = Link->Flink)
    {
        PRTL_DYNAMIC_HASH_TABLE_ENTRY Entry;

        if (RtlpHashTableEntryIsMarker(HashTable, Link))
            continue;
        Entry = CONTAINING_RECORD(Link, RTL_DYNAMIC_HASH_TABLE_ENTRY, Linkage);
        if (Entry->Signature == Signature)
            return Entry;
    }
    return NULL;
}

/* PUBLIC FUNCTIONS *********************************************************/

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlCreateHashTable(
    _Inout_ PRTL_DYNAMIC_HASH_TABLE *HashTable,
    _In_ ULONG Shift,
    _In_ ULONG Flags)
{
    PRTL_DYNAMIC_HASH_TABLE Table;
    PRTLP_HASH_DIRECTORY Directory;
    PLIST_ENTRY Block;
    ULONG i;

    if (HashTable == NULL || Shift != 0 || Flags != 0)
        return FALSE;

    if (*HashTable != NULL)
    {
        Table = *HashTable;
        Table->Flags = 0;
    }
    else
    {
        Table = RtlpAllocateMemory(sizeof(*Table), RTLP_HASH_TAG);
        if (Table == NULL)
            return FALSE;
        Table->Flags = RTL_HASH_ALLOCATED_HEADER;
    }

    Directory = RtlpAllocateMemory(sizeof(*Directory), RTLP_HASH_TAG);
    if (Directory == NULL)
    {
        if (Table->Flags & RTL_HASH_ALLOCATED_HEADER)
            RtlpFreeMemory(Table, RTLP_HASH_TAG);
        return FALSE;
    }
    RtlZeroMemory(Directory, sizeof(*Directory));
    InitializeListHead(&Directory->Enumerators);

    Block = RtlpAllocateMemory(RTLP_HASH_BASE_SIZE * sizeof(LIST_ENTRY), RTLP_HASH_TAG);
    if (Block == NULL)
    {
        RtlpFreeMemory(Directory, RTLP_HASH_TAG);
        if (Table->Flags & RTL_HASH_ALLOCATED_HEADER)
            RtlpFreeMemory(Table, RTLP_HASH_TAG);
        return FALSE;
    }
    for (i = 0; i < RTLP_HASH_BASE_SIZE; i++)
        InitializeListHead(&Block[i]);
    Directory->Blocks[0] = Block;

    Table->Shift = 0;
    Table->TableSize = RTLP_HASH_BASE_SIZE;
    Table->Pivot = 0;
    Table->DivisorMask = RTLP_HASH_BASE_SIZE - 1;
    Table->NumEntries = 0;
    Table->NonEmptyBuckets = 0;
    Table->NumEnumerators = 0;
    Table->Directory = Directory;

    *HashTable = Table;
    return TRUE;
}

/*
 * @implemented
 */
VOID
NTAPI
RtlDeleteHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable)
{
    PRTLP_HASH_DIRECTORY Directory = HashTable->Directory;
    ULONG i;

    for (i = 0; i < RTLP_HASH_MAX_BLOCKS; i++)
    {
        if (Directory->Blocks[i] != NULL)
            RtlpFreeMemory(Directory->Blocks[i], RTLP_HASH_TAG);
    }
    RtlpFreeMemory(Directory, RTLP_HASH_TAG);
    if (HashTable->Flags & RTL_HASH_ALLOCATED_HEADER)
        RtlpFreeMemory(HashTable, RTLP_HASH_TAG);
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlInsertEntryHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _In_ PRTL_DYNAMIC_HASH_TABLE_ENTRY Entry,
    _In_ ULONG_PTR Signature,
    _Inout_opt_ PRTL_DYNAMIC_HASH_TABLE_CONTEXT Context)
{
    PLIST_ENTRY ChainHead;
    PLIST_ENTRY Prev;
    PLIST_ENTRY Link;

    Entry->Signature = Signature;
    ChainHead = RtlpHashTableBucket(HashTable, RtlpHashTableIndex(HashTable, Signature));

    Prev = ChainHead;
    for (Link = ChainHead->Flink; Link != ChainHead; Link = Link->Flink)
    {
        PRTL_DYNAMIC_HASH_TABLE_ENTRY Current;

        if (RtlpHashTableEntryIsMarker(HashTable, Link))
        {
            Prev = Link;
            continue;
        }
        Current = CONTAINING_RECORD(Link, RTL_DYNAMIC_HASH_TABLE_ENTRY, Linkage);
        if (Current->Signature >= Signature)
            break;
        Prev = Link;
    }

    if (IsListEmpty(ChainHead))
        HashTable->NonEmptyBuckets++;
    InsertHeadList(Prev, &Entry->Linkage);
    HashTable->NumEntries++;

    if (Context != NULL)
    {
        Context->ChainHead = ChainHead;
        Context->PrevLinkage = &Entry->Linkage;
        Context->Signature = Signature;
    }
    return TRUE;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlRemoveEntryHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _In_ PRTL_DYNAMIC_HASH_TABLE_ENTRY Entry,
    _Inout_opt_ PRTL_DYNAMIC_HASH_TABLE_CONTEXT Context)
{
    PLIST_ENTRY ChainHead;

    if (Entry->Linkage.Flink == NULL || Entry->Linkage.Blink == NULL)
        return FALSE;

    if (Context != NULL && Context->PrevLinkage == &Entry->Linkage)
        Context->PrevLinkage = Entry->Linkage.Blink;

    RemoveEntryList(&Entry->Linkage);
    Entry->Linkage.Flink = NULL;
    Entry->Linkage.Blink = NULL;
    HashTable->NumEntries--;

    ChainHead = RtlpHashTableBucket(HashTable, RtlpHashTableIndex(HashTable, Entry->Signature));
    if (IsListEmpty(ChainHead))
        HashTable->NonEmptyBuckets--;
    return TRUE;
}

/*
 * @implemented
 */
PRTL_DYNAMIC_HASH_TABLE_ENTRY
NTAPI
RtlLookupEntryHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _In_ ULONG_PTR Signature,
    _Out_opt_ PRTL_DYNAMIC_HASH_TABLE_CONTEXT Context)
{
    PLIST_ENTRY ChainHead;
    PRTL_DYNAMIC_HASH_TABLE_ENTRY Entry;

    ChainHead = RtlpHashTableBucket(HashTable, RtlpHashTableIndex(HashTable, Signature));
    Entry = RtlpHashTableFindNext(HashTable, ChainHead, ChainHead->Flink, Signature);

    if (Context != NULL)
    {
        Context->ChainHead = ChainHead;
        Context->PrevLinkage = Entry != NULL ? &Entry->Linkage : ChainHead;
        Context->Signature = Signature;
    }
    return Entry;
}

/*
 * @implemented
 */
PRTL_DYNAMIC_HASH_TABLE_ENTRY
NTAPI
RtlGetNextEntryHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _In_ PRTL_DYNAMIC_HASH_TABLE_CONTEXT Context)
{
    PRTL_DYNAMIC_HASH_TABLE_ENTRY Entry;
    PLIST_ENTRY First;

    if (Context == NULL || Context->ChainHead == NULL)
        return NULL;

    First = Context->PrevLinkage == Context->ChainHead ? Context->ChainHead : Context->PrevLinkage->Flink;
    Entry = RtlpHashTableFindNext(HashTable, Context->ChainHead, First, Context->Signature);
    if (Entry != NULL)
        Context->PrevLinkage = &Entry->Linkage;
    return Entry;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlInitEnumerationHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _Out_ PRTL_DYNAMIC_HASH_TABLE_ENUMERATOR Enumerator)
{
    PRTLP_HASH_DIRECTORY Directory = HashTable->Directory;
    PRTLP_HASH_ENUM_LINK EnumLink;
    PLIST_ENTRY ChainHead;

    EnumLink = RtlpAllocateMemory(sizeof(*EnumLink), RTLP_HASH_TAG);
    if (EnumLink == NULL)
        return FALSE;

    EnumLink->Enumerator = Enumerator;
    InsertTailList(&Directory->Enumerators, &EnumLink->Linkage);
    HashTable->NumEnumerators++;

    ChainHead = RtlpHashTableBucket(HashTable, 0);
    Enumerator->BucketIndex = 0;
    Enumerator->ChainHead = ChainHead;
    Enumerator->HashEntry.Signature = 0;
    InsertHeadList(ChainHead, &Enumerator->HashEntry.Linkage);
    return TRUE;
}

/*
 * @implemented
 */
PRTL_DYNAMIC_HASH_TABLE_ENTRY
NTAPI
RtlEnumerateEntryHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _Inout_ PRTL_DYNAMIC_HASH_TABLE_ENUMERATOR Enumerator)
{
    PLIST_ENTRY Link;
    ULONG Index;

    RemoveEntryList(&Enumerator->HashEntry.Linkage);

    Link = Enumerator->HashEntry.Linkage.Flink;
    Index = Enumerator->BucketIndex;
    for (;;)
    {
        PLIST_ENTRY ChainHead = RtlpHashTableBucket(HashTable, Index);

        while (Link != ChainHead)
        {
            if (!RtlpHashTableEntryIsMarker(HashTable, Link))
            {
                PRTL_DYNAMIC_HASH_TABLE_ENTRY Entry = CONTAINING_RECORD(Link, RTL_DYNAMIC_HASH_TABLE_ENTRY, Linkage);

                Enumerator->BucketIndex = Index;
                Enumerator->ChainHead = ChainHead;
                InsertHeadList(Link, &Enumerator->HashEntry.Linkage);
                return Entry;
            }
            Link = Link->Flink;
        }

        Index++;
        if (Index >= HashTable->TableSize)
        {
            Enumerator->BucketIndex = HashTable->TableSize - 1;
            Enumerator->ChainHead = ChainHead;
            InsertTailList(ChainHead, &Enumerator->HashEntry.Linkage);
            return NULL;
        }
        Link = RtlpHashTableBucket(HashTable, Index)->Flink;
    }
}

/*
 * @implemented
 */
VOID
NTAPI
RtlEndEnumerationHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _Inout_ PRTL_DYNAMIC_HASH_TABLE_ENUMERATOR Enumerator)
{
    PRTLP_HASH_DIRECTORY Directory = HashTable->Directory;
    PLIST_ENTRY Link;

    RemoveEntryList(&Enumerator->HashEntry.Linkage);

    for (Link = Directory->Enumerators.Flink; Link != &Directory->Enumerators; Link = Link->Flink)
    {
        PRTLP_HASH_ENUM_LINK EnumLink = CONTAINING_RECORD(Link, RTLP_HASH_ENUM_LINK, Linkage);

        if (EnumLink->Enumerator == Enumerator)
        {
            RemoveEntryList(&EnumLink->Linkage);
            RtlpFreeMemory(EnumLink, RTLP_HASH_TAG);
            break;
        }
    }
    HashTable->NumEnumerators--;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlInitWeakEnumerationHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _Out_ PRTL_DYNAMIC_HASH_TABLE_ENUMERATOR Enumerator)
{
    UNREFERENCED_PARAMETER(HashTable);

    Enumerator->BucketIndex = 0;
    Enumerator->ChainHead = NULL;
    Enumerator->HashEntry.Linkage.Flink = NULL;
    Enumerator->HashEntry.Linkage.Blink = NULL;
    Enumerator->HashEntry.Signature = 0;
    return TRUE;
}

/*
 * @implemented
 */
PRTL_DYNAMIC_HASH_TABLE_ENTRY
NTAPI
RtlWeaklyEnumerateEntryHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _Inout_ PRTL_DYNAMIC_HASH_TABLE_ENUMERATOR Enumerator)
{
    ULONG Index = Enumerator->BucketIndex;
    PLIST_ENTRY Link;

    if (Enumerator->ChainHead == NULL)
    {
        Index = 0;
        Enumerator->ChainHead = RtlpHashTableBucket(HashTable, 0);
        Link = Enumerator->ChainHead->Flink;
    }
    else if (Enumerator->HashEntry.Linkage.Flink != NULL)
    {
        Link = Enumerator->HashEntry.Linkage.Flink;
    }
    else
    {
        Link = Enumerator->ChainHead->Flink;
    }

    for (;;)
    {
        PLIST_ENTRY ChainHead = RtlpHashTableBucket(HashTable, Index);

        while (Link != ChainHead)
        {
            if (!RtlpHashTableEntryIsMarker(HashTable, Link))
            {
                PRTL_DYNAMIC_HASH_TABLE_ENTRY Entry = CONTAINING_RECORD(Link, RTL_DYNAMIC_HASH_TABLE_ENTRY, Linkage);

                Enumerator->BucketIndex = Index;
                Enumerator->ChainHead = ChainHead;
                Enumerator->HashEntry.Linkage.Flink = Link;
                return Entry;
            }
            Link = Link->Flink;
        }

        Index++;
        if (Index >= HashTable->TableSize)
            return NULL;
        Link = RtlpHashTableBucket(HashTable, Index)->Flink;
    }
}

/*
 * @implemented
 */
VOID
NTAPI
RtlEndWeakEnumerationHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable,
    _Inout_ PRTL_DYNAMIC_HASH_TABLE_ENUMERATOR Enumerator)
{
    UNREFERENCED_PARAMETER(HashTable);
    Enumerator->ChainHead = NULL;
    Enumerator->HashEntry.Linkage.Flink = NULL;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlExpandHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable)
{
    PRTLP_HASH_DIRECTORY Directory = HashTable->Directory;
    ULONG NewIndex = HashTable->TableSize;
    ULONG BlockIndex = NewIndex / RTLP_HASH_BASE_SIZE;
    PLIST_ENTRY NewBucket;
    PLIST_ENTRY OldBucket;
    PLIST_ENTRY Link;
    ULONG i;

    if (HashTable->NumEnumerators != 0)
        return FALSE;
    if (BlockIndex >= RTLP_HASH_MAX_BLOCKS)
        return FALSE;

    if (Directory->Blocks[BlockIndex] == NULL)
    {
        PLIST_ENTRY Block = RtlpAllocateMemory(RTLP_HASH_BASE_SIZE * sizeof(LIST_ENTRY), RTLP_HASH_TAG);

        if (Block == NULL)
            return FALSE;
        for (i = 0; i < RTLP_HASH_BASE_SIZE; i++)
            InitializeListHead(&Block[i]);
        Directory->Blocks[BlockIndex] = Block;
    }

    OldBucket = RtlpHashTableBucket(HashTable, HashTable->Pivot);
    HashTable->TableSize++;
    NewBucket = RtlpHashTableBucket(HashTable, NewIndex);

    if (!IsListEmpty(OldBucket) && IsListEmpty(NewBucket))
        HashTable->NonEmptyBuckets++;

    Link = OldBucket->Flink;
    while (Link != OldBucket)
    {
        PRTL_DYNAMIC_HASH_TABLE_ENTRY Entry = CONTAINING_RECORD(Link, RTL_DYNAMIC_HASH_TABLE_ENTRY, Linkage);
        PLIST_ENTRY Next = Link->Flink;

        if ((Entry->Signature & (HashTable->DivisorMask * 2 + 1)) == NewIndex)
        {
            RemoveEntryList(Link);
            InsertTailList(NewBucket, Link);
        }
        Link = Next;
    }

    if (IsListEmpty(OldBucket))
        HashTable->NonEmptyBuckets--;
    if (IsListEmpty(NewBucket))
        HashTable->NonEmptyBuckets--;

    HashTable->Pivot++;
    if (HashTable->Pivot == HashTable->DivisorMask + 1)
    {
        HashTable->DivisorMask = HashTable->DivisorMask * 2 + 1;
        HashTable->Pivot = 0;
        HashTable->Shift++;
    }
    return TRUE;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlContractHashTable(
    _In_ PRTL_DYNAMIC_HASH_TABLE HashTable)
{
    PLIST_ENTRY OldBucket;
    PLIST_ENTRY NewBucket;
    PLIST_ENTRY Link;

    if (HashTable->NumEnumerators != 0)
        return FALSE;
    if (HashTable->TableSize <= RTLP_HASH_BASE_SIZE)
        return FALSE;

    if (HashTable->Pivot == 0)
    {
        HashTable->DivisorMask = HashTable->DivisorMask / 2;
        HashTable->Pivot = HashTable->DivisorMask + 1;
        HashTable->Shift--;
    }
    HashTable->Pivot--;

    OldBucket = RtlpHashTableBucket(HashTable, HashTable->TableSize - 1);
    NewBucket = RtlpHashTableBucket(HashTable, HashTable->Pivot);

    if (!IsListEmpty(OldBucket))
    {
        if (IsListEmpty(NewBucket))
            HashTable->NonEmptyBuckets++;
        HashTable->NonEmptyBuckets--;
        while (!IsListEmpty(OldBucket))
        {
            Link = RemoveHeadList(OldBucket);
            InsertTailList(NewBucket, Link);
        }
    }

    HashTable->TableSize--;
    return TRUE;
}
