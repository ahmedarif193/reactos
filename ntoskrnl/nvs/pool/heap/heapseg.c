/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/heap/heapseg.c
 * PURPOSE:     Pool heap segment management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/poolenv.h"
#include "../include/poolheap.h"

#define POOL_SEG_BIT(i)  ((ULONG64)1 << ((i) & 63))

FORCEINLINE
BOOLEAN
PoolSegTest(PULONG64 Map, ULONG Bit)
{
    return (BOOLEAN)((Map[Bit >> 6] >> (Bit & 63)) & 1);
}

static
ULONG
PoolSegFindRun(
    _In_ PPOOL_SEGMENT Segment,
    _In_ ULONG PageCount)
{
    ULONG Bit = 0;
    ULONG Run = 0;

    if (PageCount == 1)
    {
        ULONG Word;

        for (Word = 0; Word < POOL_SEGMENT_MAP_WORDS; Word++)
        {
            ULONG64 Clear = ~Segment->BusyMap[Word];

            if (Clear != 0)
                return Word * 64 + POOL_CTZ64(Clear);
        }

        return POOL_SEGMENT_PAGES;
    }

    while (Bit < POOL_SEGMENT_PAGES)
    {
        if ((Bit & 63) == 0 && Run == 0 && Segment->BusyMap[Bit >> 6] == ~(ULONG64)0)
        {
            Bit += 64;
            continue;
        }

        if (PoolSegTest(Segment->BusyMap, Bit))
        {
            Run = 0;
        }
        else
        {
            Run++;
            if (Run == PageCount)
                return Bit + 1 - PageCount;
        }

        Bit++;
    }

    return POOL_SEGMENT_PAGES;
}

static
ULONG
PoolSegClaim(
    _Inout_ PPOOL_SEGMENT Segment,
    _In_ ULONG First,
    _In_ ULONG PageCount,
    _In_ UCHAR State,
    _In_ USHORT Owner,
    _In_ ULONG Tag,
    _Out_ PULONG64 NeedCommit)
{
    ULONG AlreadyCommitted = 0;
    ULONG i;

    for (i = 0; i < POOL_SEGMENT_MAP_WORDS; i++)
        NeedCommit[i] = 0;

    for (i = First; i < First + PageCount; i++)
    {
        Segment->BusyMap[i >> 6] |= POOL_SEG_BIT(i);

        if (PoolSegTest(Segment->CommitMap, i))
        {
            AlreadyCommitted++;
        }
        else
        {
            NeedCommit[i >> 6] |= POOL_SEG_BIT(i);
            Segment->CommitMap[i >> 6] |= POOL_SEG_BIT(i);
        }

        Segment->Range[i].First = (USHORT)First;
        Segment->Range[i].Owner = Owner;
        Segment->Range[i].Pages = 0;
        Segment->Range[i].Flags = 0;
        Segment->Range[i].Tag = 0;
        Segment->Range[i].State = State;
    }

    Segment->Range[First].Pages = (USHORT)PageCount;
    Segment->Range[First].Tag = Tag;
    Segment->FreePages = (USHORT)(Segment->FreePages - PageCount);
    return AlreadyCommitted;
}

static
VOID
PoolSegDecommitRuns(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PPOOL_SEGMENT Segment,
    _In_ PULONG64 Map,
    _In_ ULONG Limit)
{
    PPOOL_VA_REGION Region = Heap->Region[Segment->RegionIndex];
    ULONG Bit = 0;

    while (Bit < Limit)
    {
        ULONG Start;

        if (!PoolSegTest(Map, Bit))
        {
            Bit++;
            continue;
        }

        Start = Bit;
        while (Bit < Limit && PoolSegTest(Map, Bit))
            Bit++;

        PoolVaDecommit(Region, PoolSegPageAddress(Segment, Start),
                       (SIZE_T)(Bit - Start) << PAGE_SHIFT);
    }
}

static
NTSTATUS
PoolSegCommitRuns(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PPOOL_SEGMENT Segment,
    _In_ PULONG64 Map)
{
    PPOOL_VA_REGION Region = Heap->Region[Segment->RegionIndex];
    ULONG Bit = 0;

    while (Bit < POOL_SEGMENT_PAGES)
    {
        NTSTATUS Status;
        ULONG Start;

        if (!PoolSegTest(Map, Bit))
        {
            Bit++;
            continue;
        }

        Start = Bit;
        while (Bit < POOL_SEGMENT_PAGES && PoolSegTest(Map, Bit))
            Bit++;

        Status = PoolVaCommit(Region, PoolSegPageAddress(Segment, Start),
                              (SIZE_T)(Bit - Start) << PAGE_SHIFT);
        if (!NT_SUCCESS(Status))
        {
            PoolSegDecommitRuns(Heap, Segment, Map, Start);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

static
VOID
PoolSegUnclaimLocked(
    _Inout_ PPOOL_SEGMENT_SHARD Shard,
    _Inout_ PPOOL_SEGMENT Segment,
    _In_ ULONG First,
    _In_ ULONG PageCount)
{
    ULONG i;

    for (i = First; i < First + PageCount; i++)
    {
        Segment->Range[i].State = PoolRangeFree;
        Segment->Range[i].Pages = 0;
        Segment->BusyMap[i >> 6] &= ~POOL_SEG_BIT(i);
    }

    Segment->FreePages = (USHORT)(Segment->FreePages + PageCount);

    if (!Segment->OnAvailable)
    {
        InsertHeadList(&Shard->Available, &Segment->AvailableLink);
        Segment->OnAvailable = TRUE;
    }
}

static
PPOOL_SEGMENT
PoolSegCreate(
    _Inout_ PPOOL_HEAP Heap,
    _In_ USHORT ShardIndex)
{
    PPOOL_SEGMENT Segment = NULL;
    ULONG HeaderPages;
    ULONG RegionIndex;
    ULONG i;

    HeaderPages = (ULONG)((sizeof(POOL_SEGMENT) + PAGE_SIZE - 1) >> PAGE_SHIFT);

    for (RegionIndex = 0; RegionIndex < Heap->RegionCount; RegionIndex++)
    {
        PPOOL_VA_REGION Region = Heap->Region[RegionIndex];

        Segment = PoolVaReserve(Region, 1, PoolUnitSegment);
        if (Segment == NULL)
            continue;

        if (NT_SUCCESS(PoolVaCommit(Region, Segment, (SIZE_T)HeaderPages << PAGE_SHIFT)))
            break;

        PoolVaRelease(Region, Segment, 1);
        Segment = NULL;
    }

    if (Segment == NULL)
        return NULL;

    RtlZeroMemory(Segment, (SIZE_T)HeaderPages << PAGE_SHIFT);
    Segment->Shard = ShardIndex;
    Segment->HeaderPages = (USHORT)HeaderPages;
    Segment->FreePages = (USHORT)(POOL_SEGMENT_PAGES - HeaderPages);
    Segment->RegionIndex = (UCHAR)RegionIndex;
    Segment->Heap = Heap;

    for (i = 0; i < HeaderPages; i++)
    {
        Segment->BusyMap[i >> 6] |= POOL_SEG_BIT(i);
        Segment->CommitMap[i >> 6] |= POOL_SEG_BIT(i);
        Segment->Range[i].State = PoolRangeHeader;
    }

    for (i = POOL_SEGMENT_PAGES; i < POOL_SEGMENT_MAP_WORDS * 64; i++)
        Segment->BusyMap[i >> 6] |= POOL_SEG_BIT(i);

    Segment->Signature = POOL_SEGMENT_SIGNATURE;
    return Segment;
}

static
VOID
PoolSegDestroy(
    _Inout_ PPOOL_HEAP Heap,
    _Inout_ PPOOL_SEGMENT Segment)
{
    PPOOL_VA_REGION Region = Heap->Region[Segment->RegionIndex];
    ULONG64 Map[POOL_SEGMENT_MAP_WORDS];
    ULONG HeaderPages = Segment->HeaderPages;
    ULONG i;

    for (i = 0; i < POOL_SEGMENT_MAP_WORDS; i++)
        Map[i] = Segment->CommitMap[i];

    for (i = 0; i < HeaderPages; i++)
        Map[i >> 6] &= ~POOL_SEG_BIT(i);

    Segment->Signature = 0;
    PoolSegDecommitRuns(Heap, Segment, Map, POOL_SEGMENT_PAGES);
    PoolVaDecommit(Region, Segment, (SIZE_T)HeaderPages << PAGE_SHIFT);
    PoolVaRelease(Region, Segment, 1);
}

static
ULONG
PoolSegCommittedFreePages(
    _In_ PPOOL_SEGMENT Segment)
{
    ULONG Count = 0;
    ULONG i;

    for (i = 0; i < POOL_SEGMENT_MAP_WORDS; i++)
    {
        ULONG64 Word = Segment->CommitMap[i] & ~Segment->BusyMap[i];

        while (Word != 0)
        {
            Word &= Word - 1;
            Count++;
        }
    }

    return Count;
}

static
BOOLEAN
PoolSegRetireLocked(
    _Inout_ PPOOL_SEGMENT_SHARD Shard,
    _Inout_ PPOOL_SEGMENT Segment)
{
    if (Segment->FreePages != POOL_SEGMENT_PAGES - Segment->HeaderPages || Shard->SegmentCount <= 1)
        return FALSE;

    RemoveEntryList(&Segment->AvailableLink);
    Segment->OnAvailable = FALSE;
    Shard->SegmentCount--;
    Shard->FreeCommittedPages -= PoolSegCommittedFreePages(Segment);
    return TRUE;
}

PVOID
PoolSegAllocatePages(
    _Inout_ PPOOL_HEAP Heap,
    _In_ ULONG PageCount,
    _In_ UCHAR State,
    _In_ USHORT Owner,
    _In_ ULONG Tag)
{
    USHORT ShardIndex = (USHORT)(POOL_CURRENT_PROCESSOR() % Heap->ShardCount);
    PPOOL_SEGMENT_SHARD Shard = &Heap->Shard[ShardIndex];
    ULONG64 NeedCommit[POOL_SEGMENT_MAP_WORDS];
    PPOOL_SEGMENT Segment = NULL;
    PLIST_ENTRY Entry;
    ULONG First = POOL_SEGMENT_PAGES;
    ULONG AlreadyCommitted = 0;
    ULONG Probes = 0;
    BOOLEAN Created = FALSE;
    KIRQL OldIrql;
    ULONG i;

    if (PageCount == 0 || PageCount > POOL_SEGMENT_PAGES / 2)
        return NULL;

    POOL_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);

    for (Entry = Shard->Available.Flink;
         Entry != &Shard->Available && Probes < POOL_SEGMENT_PROBE_LIMIT;
         Entry = Entry->Flink, Probes++)
    {
        PPOOL_SEGMENT Candidate = CONTAINING_RECORD(Entry, POOL_SEGMENT, AvailableLink);

        if (Candidate->FreePages < PageCount)
            continue;

        First = PoolSegFindRun(Candidate, PageCount);
        if (First < POOL_SEGMENT_PAGES)
        {
            Segment = Candidate;
            break;
        }
    }

    if (Segment != NULL)
    {
        AlreadyCommitted = PoolSegClaim(Segment, First, PageCount, State, Owner, Tag, NeedCommit);
        Shard->FreeCommittedPages -= AlreadyCommitted;

        if (Segment->FreePages == 0)
        {
            RemoveEntryList(&Segment->AvailableLink);
            Segment->OnAvailable = FALSE;
        }
        else if (Shard->Available.Flink != &Segment->AvailableLink)
        {
            RemoveEntryList(&Segment->AvailableLink);
            InsertHeadList(&Shard->Available, &Segment->AvailableLink);
        }
    }

    Shard->RangeAllocations++;
    POOL_LOCK_RELEASE(&Shard->Lock, OldIrql);

    if (Segment == NULL)
    {
        Segment = PoolSegCreate(Heap, ShardIndex);
        if (Segment == NULL)
            return NULL;

        Created = TRUE;
        First = Segment->HeaderPages;
        PoolSegClaim(Segment, First, PageCount, State, Owner, Tag, NeedCommit);
    }

    if (!NT_SUCCESS(PoolSegCommitRuns(Heap, Segment, NeedCommit)))
    {
        for (i = 0; i < POOL_SEGMENT_MAP_WORDS; i++)
            Segment->CommitMap[i] &= ~NeedCommit[i];

        if (Created)
        {
            PoolSegDestroy(Heap, Segment);
            return NULL;
        }

        POOL_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);
        Shard->FreeCommittedPages += AlreadyCommitted;
        PoolSegUnclaimLocked(Shard, Segment, First, PageCount);
        POOL_LOCK_RELEASE(&Shard->Lock, OldIrql);
        return NULL;
    }

    if (Created)
    {
        POOL_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);
        InsertHeadList(&Shard->Available, &Segment->AvailableLink);
        Segment->OnAvailable = TRUE;
        Shard->SegmentCount++;
        POOL_LOCK_RELEASE(&Shard->Lock, OldIrql);
    }

    return PoolSegPageAddress(Segment, First);
}

VOID
PoolSegFreePages(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PVOID Base)
{
    PPOOL_SEGMENT Segment = PoolSegFromAddress(Base);
    ULONG First = PoolSegPageIndex(Base);
    PPOOL_SEGMENT_SHARD Shard;
    ULONG64 Drain[POOL_SEGMENT_MAP_WORDS];
    BOOLEAN Retire;
    ULONG PageCount;
    KIRQL OldIrql;
    ULONG i;

    POOL_ASSERT(Segment->Signature == POOL_SEGMENT_SIGNATURE);
    POOL_ASSERT(Segment->Range[First].First == First);
    POOL_ASSERT(Segment->Range[First].Pages != 0);

    Shard = &Heap->Shard[Segment->Shard];
    PageCount = Segment->Range[First].Pages;

    POOL_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);
    Shard->RangeFrees++;

    if (Shard->FreeCommittedPages + PageCount > POOL_SHARD_COMMIT_CACHE)
    {
        for (i = 0; i < POOL_SEGMENT_MAP_WORDS; i++)
            Drain[i] = 0;

        for (i = First; i < First + PageCount; i++)
        {
            Segment->Range[i].State = PoolRangeDraining;
            Segment->CommitMap[i >> 6] &= ~POOL_SEG_BIT(i);
            Drain[i >> 6] |= POOL_SEG_BIT(i);
        }

        POOL_LOCK_RELEASE(&Shard->Lock, OldIrql);
        PoolSegDecommitRuns(Heap, Segment, Drain, POOL_SEGMENT_PAGES);
        POOL_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);
    }
    else
    {
        Shard->FreeCommittedPages += PageCount;
    }

    PoolSegUnclaimLocked(Shard, Segment, First, PageCount);
    Retire = PoolSegRetireLocked(Shard, Segment);
    POOL_LOCK_RELEASE(&Shard->Lock, OldIrql);

    if (Retire)
        PoolSegDestroy(Heap, Segment);
}

VOID
PoolSegTrimShard(
    _Inout_ PPOOL_HEAP Heap,
    _In_ ULONG ShardIndex)
{
    PPOOL_SEGMENT_SHARD Shard = &Heap->Shard[ShardIndex];
    ULONG64 Drain[POOL_SEGMENT_MAP_WORDS];
    ULONG Budget;
    KIRQL OldIrql;

    POOL_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);
    Budget = Shard->FreeCommittedPages;

    while (Budget != 0 && Shard->FreeCommittedPages != 0)
    {
        PPOOL_SEGMENT Segment = NULL;
        PLIST_ENTRY Entry;
        ULONG First = 0;
        ULONG Count = 0;
        BOOLEAN Retire;
        ULONG i;

        for (Entry = Shard->Available.Flink; Entry != &Shard->Available; Entry = Entry->Flink)
        {
            PPOOL_SEGMENT Candidate = CONTAINING_RECORD(Entry, POOL_SEGMENT, AvailableLink);

            for (i = 0; i < POOL_SEGMENT_MAP_WORDS; i++)
            {
                ULONG64 Word = Candidate->CommitMap[i] & ~Candidate->BusyMap[i];

                if (Word != 0)
                {
                    First = i * 64 + POOL_CTZ64(Word);
                    Segment = Candidate;
                    break;
                }
            }

            if (Segment != NULL)
                break;
        }

        if (Segment == NULL)
            break;

        for (i = 0; i < POOL_SEGMENT_MAP_WORDS; i++)
            Drain[i] = 0;

        for (i = First;
             i < POOL_SEGMENT_PAGES && PoolSegTest(Segment->CommitMap, i) &&
             !PoolSegTest(Segment->BusyMap, i);
             i++)
        {
            Segment->BusyMap[i >> 6] |= POOL_SEG_BIT(i);
            Segment->CommitMap[i >> 6] &= ~POOL_SEG_BIT(i);
            Segment->Range[i].State = PoolRangeDraining;
            Drain[i >> 6] |= POOL_SEG_BIT(i);
            Count++;
        }

        Segment->FreePages = (USHORT)(Segment->FreePages - Count);
        Shard->FreeCommittedPages -= Count;
        Budget = (Budget > Count) ? Budget - Count : 0;

        if (Segment->FreePages == 0)
        {
            RemoveEntryList(&Segment->AvailableLink);
            Segment->OnAvailable = FALSE;
        }

        POOL_LOCK_RELEASE(&Shard->Lock, OldIrql);
        PoolSegDecommitRuns(Heap, Segment, Drain, POOL_SEGMENT_PAGES);
        POOL_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);

        PoolSegUnclaimLocked(Shard, Segment, First, Count);
        Retire = PoolSegRetireLocked(Shard, Segment);

        if (Retire)
        {
            POOL_LOCK_RELEASE(&Shard->Lock, OldIrql);
            PoolSegDestroy(Heap, Segment);
            POOL_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);
        }
    }

    POOL_LOCK_RELEASE(&Shard->Lock, OldIrql);
}
