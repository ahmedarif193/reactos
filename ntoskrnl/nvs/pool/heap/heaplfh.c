/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/heap/heaplfh.c
 * PURPOSE:     Low-fragmentation pool heap allocation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/poolenv.h"
#include "../include/poolheap.h"

static const USHORT PoolLfhSizes[POOL_LFH_CLASS_COUNT] =
{
      16,   32,   48,   64,   80,   96,  112,  128,
     160,  192,  224,  256,
     320,  384,  448,  512,
     640,  768,  896, 1024,
    1280, 1536, 1792, 2048,
    2560, 3072, 3584, 4096
};

static UCHAR PoolLfhClassMap[(POOL_LFH_MAX_BYTES / POOL_ALIGNMENT) + 1];

VOID
PoolLfhInitialize(VOID)
{
    ULONG Index;
    ULONG Class = 0;

    for (Index = 0; Index < RTL_NUMBER_OF(PoolLfhClassMap); Index++)
    {
        while (PoolLfhSizes[Class] < Index * POOL_ALIGNMENT)
            Class++;

        PoolLfhClassMap[Index] = (UCHAR)Class;
    }
}

ULONG
PoolLfhClassFromSize(
    _In_ SIZE_T Bytes)
{
    if (Bytes > POOL_LFH_MAX_BYTES)
        return POOL_LFH_CLASS_COUNT;

    return PoolLfhClassMap[(Bytes + POOL_ALIGNMENT - 1) / POOL_ALIGNMENT];
}

USHORT
PoolLfhClassSize(
    _In_ ULONG Class)
{
    return (Class < POOL_LFH_CLASS_COUNT) ? PoolLfhSizes[Class] : 0;
}

static
PPOOL_LFH_SUBSEGMENT
PoolLfhCreateSubsegment(
    _Inout_ PPOOL_HEAP Heap,
    _In_ ULONG Class,
    _In_ PPOOL_LFH_SLOT Slot,
    _In_ ULONG64 SlotAllocations)
{
    PPOOL_LFH_SUBSEGMENT Subsegment;
    SIZE_T TargetBytes;
    SIZE_T HeaderBytes;
    ULONG BlockSize = PoolLfhSizes[Class];
    ULONG BlockCount;
    ULONG FirstCount;
    ULONG MapWords;
    ULONG NeededPages;
    ULONG PerPage;
    ULONG Pages;
    ULONG Lead;
    ULONG i;

    Lead = (BlockSize % POOL_CACHE_ALIGNMENT == 0) ? POOL_CACHE_ALIGNMENT : POOL_ALIGNMENT;
    if (BlockSize + Lead > PAGE_SIZE)
        return NULL;

    PerPage = (ULONG)((PAGE_SIZE - Lead) / BlockSize);

    if (SlotAllocations < 64)
        TargetBytes = PAGE_SIZE;
    else if (SlotAllocations < 1024)
        TargetBytes = 4 * PAGE_SIZE;
    else
        TargetBytes = 16 * PAGE_SIZE;

    if (TargetBytes < 4 * (SIZE_T)BlockSize)
        TargetBytes = 4 * (SIZE_T)BlockSize;

    Pages = (ULONG)((TargetBytes + PAGE_SIZE - 1) >> PAGE_SHIFT);
    BlockCount = Pages * PerPage;
    if (BlockCount > POOL_LFH_MAX_BLOCKS)
        BlockCount = POOL_LFH_MAX_BLOCKS;

    for (;;)
    {
        SIZE_T HeaderPages;

        MapWords = (BlockCount + 63) / 64;
        HeaderBytes = FIELD_OFFSET(POOL_LFH_SUBSEGMENT, Bitmap) + MapWords * sizeof(ULONG64) +
                      BlockCount * sizeof(ULONG) + BlockCount;
        HeaderBytes = (HeaderBytes + POOL_CACHE_ALIGNMENT - 1) & ~(SIZE_T)(POOL_CACHE_ALIGNMENT - 1);

        if ((HeaderBytes & (PAGE_SIZE - 1)) == 0)
            HeaderBytes += Lead;

        HeaderPages = (HeaderBytes >> PAGE_SHIFT) + 1;
        FirstCount = (ULONG)((PAGE_SIZE - (HeaderBytes & (PAGE_SIZE - 1))) / BlockSize);
        NeededPages = (ULONG)HeaderPages;

        if (BlockCount > FirstCount)
            NeededPages += (BlockCount - FirstCount + PerPage - 1) / PerPage;

        if (NeededPages <= Pages || BlockCount == 1)
            break;

        BlockCount--;
    }

    Pages = NeededPages;
    if (FirstCount > BlockCount)
        FirstCount = BlockCount;

    Subsegment = PoolSegAllocatePages(Heap, Pages, PoolRangeLfh, (USHORT)Class, 0);
    if (Subsegment == NULL)
        return NULL;

    RtlZeroMemory(Subsegment, HeaderBytes);
    Subsegment->Slot = Slot;
    Subsegment->BlockBase = (PUCHAR)Subsegment + HeaderBytes;
    Subsegment->PagedBase = (PUCHAR)Subsegment + (((HeaderBytes >> PAGE_SHIFT) + 1) << PAGE_SHIFT);
    Subsegment->FirstCount = (USHORT)FirstCount;
    Subsegment->PerPage = (USHORT)PerPage;
    Subsegment->Lead = (USHORT)Lead;
    Subsegment->Tags = (PULONG)&Subsegment->Bitmap[MapWords];
    Subsegment->Flags = (PUCHAR)&Subsegment->Tags[BlockCount];
    Subsegment->BlockSize = (USHORT)BlockSize;
    Subsegment->BlockCount = (USHORT)BlockCount;
    Subsegment->FreeCount = (USHORT)BlockCount;
    Subsegment->MapWords = (USHORT)MapWords;
    Subsegment->Pages = (USHORT)Pages;
    Subsegment->Class = (UCHAR)Class;

    for (i = BlockCount; i < MapWords * 64; i++)
        Subsegment->Bitmap[i >> 6] |= ((ULONG64)1 << (i & 63));

    Subsegment->Signature = POOL_LFH_SIGNATURE;
    return Subsegment;
}

static
PVOID
PoolLfhTake(
    _Inout_ PPOOL_LFH_SUBSEGMENT Subsegment,
    _In_ ULONG Tag)
{
    ULONG Word = Subsegment->HintWord;
    ULONG Scanned;

    for (Scanned = 0; Scanned < Subsegment->MapWords; Scanned++)
    {
        ULONG64 Clear = ~Subsegment->Bitmap[Word];

        if (Clear != 0)
        {
            ULONG Bit = Word * 64 + POOL_CTZ64(Clear);

            Subsegment->Bitmap[Word] |= ((ULONG64)1 << (Bit & 63));
            Subsegment->Tags[Bit] = Tag;
            Subsegment->Flags[Bit] = 0;
            Subsegment->FreeCount--;
            Subsegment->HintWord = (USHORT)Word;
            return PoolLfhBlockAddress(Subsegment, Bit);
        }

        Word++;
        if (Word == Subsegment->MapWords)
            Word = 0;
    }

    return NULL;
}

PVOID
PoolLfhAllocate(
    _Inout_ PPOOL_HEAP Heap,
    _In_ ULONG Class,
    _In_ ULONG Tag)
{
    PPOOL_LFH_SLOT Slot = PoolHeapLfhSlot(Heap, Class, POOL_CURRENT_PROCESSOR() % Heap->SlotCount);
    PPOOL_LFH_SUBSEGMENT Subsegment;
    ULONG64 SlotAllocations;
    PVOID Block = NULL;
    KIRQL OldIrql;

    POOL_LOCK_ACQUIRE(&Slot->Lock, &OldIrql);

    if (!IsListEmpty(&Slot->Partial))
    {
        Subsegment = CONTAINING_RECORD(Slot->Partial.Flink, POOL_LFH_SUBSEGMENT, Link);
        Block = PoolLfhTake(Subsegment, Tag);
        POOL_ASSERT(Block != NULL);

        if (Subsegment->FreeCount == 0)
        {
            RemoveEntryList(&Subsegment->Link);
            InsertHeadList(&Slot->Full, &Subsegment->Link);
            Subsegment->OnFullList = TRUE;
        }

        Slot->Allocations++;
        POOL_LOCK_RELEASE(&Slot->Lock, OldIrql);
        return Block;
    }

    SlotAllocations = Slot->Allocations;
    POOL_LOCK_RELEASE(&Slot->Lock, OldIrql);

    Subsegment = PoolLfhCreateSubsegment(Heap, Class, Slot, SlotAllocations);
    if (Subsegment == NULL)
        return NULL;

    Block = PoolLfhTake(Subsegment, Tag);

    POOL_LOCK_ACQUIRE(&Slot->Lock, &OldIrql);
    InsertHeadList(&Slot->Partial, &Subsegment->Link);
    Slot->SubsegmentCount++;
    Slot->Allocations++;
    POOL_LOCK_RELEASE(&Slot->Lock, OldIrql);
    return Block;
}

FORCEINLINE
BOOLEAN
PoolLfhIndex(
    _In_ PPOOL_LFH_SUBSEGMENT Subsegment,
    _In_ PVOID Block,
    _Out_ PULONG Bit)
{
    if (Subsegment->Signature != POOL_LFH_SIGNATURE)
        return FALSE;

    return PoolLfhBlockIndex(Subsegment, Block, TRUE, Bit);
}

BOOLEAN
PoolLfhQuery(
    _In_ PPOOL_LFH_SUBSEGMENT Subsegment,
    _In_ PVOID Block,
    _Out_ PPOOL_BLOCK_INFO Info)
{
    ULONG Bit;

    if (!PoolLfhIndex(Subsegment, Block, &Bit))
        return FALSE;

    if (((Subsegment->Bitmap[Bit >> 6] >> (Bit & 63)) & 1) == 0)
        return FALSE;

    Info->Size = Subsegment->BlockSize;
    Info->Tag = Subsegment->Tags[Bit];
    Info->Flags = Subsegment->Flags[Bit];
    Info->Kind = PoolBlockLfh;
    PoolCaptureTrailer(Block, Info);
    return TRUE;
}

BOOLEAN
PoolLfhSetFlags(
    _In_ PPOOL_LFH_SUBSEGMENT Subsegment,
    _In_ PVOID Block,
    _In_ UCHAR Flags)
{
    ULONG Bit;

    if (!PoolLfhIndex(Subsegment, Block, &Bit))
        return FALSE;

    if (((Subsegment->Bitmap[Bit >> 6] >> (Bit & 63)) & 1) == 0)
        return FALSE;

    Subsegment->Flags[Bit] = Flags;
    return TRUE;
}

BOOLEAN
PoolLfhFree(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PPOOL_LFH_SUBSEGMENT Subsegment,
    _In_ PVOID Block,
    _Out_ PPOOL_BLOCK_INFO Info)
{
    PPOOL_LFH_SLOT Slot;
    BOOLEAN Retire = FALSE;
    KIRQL OldIrql;
    ULONG Bit;

    if (!PoolLfhIndex(Subsegment, Block, &Bit))
        return FALSE;

    Slot = Subsegment->Slot;
    POOL_LOCK_ACQUIRE(&Slot->Lock, &OldIrql);

    if (((Subsegment->Bitmap[Bit >> 6] >> (Bit & 63)) & 1) == 0)
    {
        POOL_LOCK_RELEASE(&Slot->Lock, OldIrql);
        return FALSE;
    }

    Info->Size = Subsegment->BlockSize;
    Info->Tag = Subsegment->Tags[Bit];
    Info->Flags = Subsegment->Flags[Bit];
    Info->Kind = PoolBlockLfh;
    PoolCaptureTrailer(Block, Info);

    if (Heap->Options & POOL_HEAP_POISON_ON_FREE)
        RtlFillMemory(Block, Subsegment->BlockSize, POOL_FREE_POISON);

    Subsegment->Bitmap[Bit >> 6] &= ~((ULONG64)1 << (Bit & 63));
    Subsegment->FreeCount++;
    Subsegment->HintWord = (USHORT)(Bit >> 6);
    Slot->Frees++;

    if (Subsegment->OnFullList)
    {
        RemoveEntryList(&Subsegment->Link);
        InsertTailList(&Slot->Partial, &Subsegment->Link);
        Subsegment->OnFullList = FALSE;
    }

    if (Subsegment->FreeCount == Subsegment->BlockCount &&
        Slot->Partial.Flink != &Subsegment->Link)
    {
        RemoveEntryList(&Subsegment->Link);
        Slot->SubsegmentCount--;
        Subsegment->Signature = 0;
        Retire = TRUE;
    }

    POOL_LOCK_RELEASE(&Slot->Lock, OldIrql);

    if (Retire)
        PoolSegFreePages(Heap, Subsegment);

    return TRUE;
}

VOID
PoolLfhTrim(
    _Inout_ PPOOL_HEAP Heap)
{
    ULONG Index;

    for (Index = 0; Index < Heap->SlotCount * POOL_LFH_CLASS_COUNT; Index++)
    {
        PPOOL_LFH_SLOT Slot = &Heap->LfhSlot[Index];
        LIST_ENTRY Retired;
        PLIST_ENTRY Entry;
        KIRQL OldIrql;

        InitializeListHead(&Retired);
        POOL_LOCK_ACQUIRE(&Slot->Lock, &OldIrql);

        Entry = Slot->Partial.Flink;
        while (Entry != &Slot->Partial)
        {
            PPOOL_LFH_SUBSEGMENT Subsegment = CONTAINING_RECORD(Entry, POOL_LFH_SUBSEGMENT, Link);

            Entry = Entry->Flink;
            if (Subsegment->FreeCount != Subsegment->BlockCount)
                continue;

            RemoveEntryList(&Subsegment->Link);
            InsertTailList(&Retired, &Subsegment->Link);
            Slot->SubsegmentCount--;
            Subsegment->Signature = 0;
        }

        POOL_LOCK_RELEASE(&Slot->Lock, OldIrql);

        while (!IsListEmpty(&Retired))
        {
            Entry = Retired.Flink;
            RemoveEntryList(Entry);
            PoolSegFreePages(Heap, CONTAINING_RECORD(Entry, POOL_LFH_SUBSEGMENT, Link));
        }
    }
}
