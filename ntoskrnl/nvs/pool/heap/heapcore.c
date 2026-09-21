/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/heap/heapcore.c
 * PURPOSE:     Pool heap core allocation and management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/poolenv.h"
#include "../include/poolheap.h"

typedef struct _POOL_LOCATION
{
    UCHAR Kind;
    PPOOL_VA_REGION Region;
    PPOOL_SEGMENT Segment;
    PPOOL_PAGE_RANGE Range;
    PVOID Owner;
} POOL_LOCATION, *PPOOL_LOCATION;

NTSTATUS
PoolHeapCreate(
    _Out_ PPOOL_HEAP *OutHeap,
    _In_ PPOOL_VA_REGION Region,
    _In_ ULONG HeapId,
    _In_ ULONG ProcessorCount,
    _In_ ULONG64 EncodeKey,
    _In_ ULONG Options)
{
    PPOOL_HEAP Heap;
    ULONG SlotCount;
    ULONG ShardCount;
    ULONG i;

    *OutHeap = NULL;

    if (ProcessorCount == 0)
        ProcessorCount = 1;

    SlotCount = (ProcessorCount > POOL_MAX_SLOTS) ? POOL_MAX_SLOTS : ProcessorCount;
    ShardCount = (ProcessorCount > POOL_MAX_SHARDS) ? POOL_MAX_SHARDS : ProcessorCount;

    PoolLfhInitialize();

    Heap = PoolVaAllocateMetadata(Region, sizeof(POOL_HEAP));
    if (Heap == NULL)
        return STATUS_NO_MEMORY;

    Heap->HeapId = HeapId;
    Heap->Options = Options;
    Heap->Paged = Region->Paged;
    Heap->SlotCount = SlotCount;
    Heap->ShardCount = ShardCount;
    Heap->EncodeKey = EncodeKey ^ (ULONG64)(ULONG_PTR)Heap;
    Heap->Region[0] = Region;
    Heap->RegionCount = 1;

    Heap->Shard = PoolVaAllocateMetadata(Region, sizeof(POOL_SEGMENT_SHARD) * ShardCount);
    Heap->Vs = PoolVaAllocateMetadata(Region, sizeof(POOL_VS_CONTEXT) * ShardCount);
    Heap->LfhSlot = PoolVaAllocateMetadata(Region, sizeof(POOL_LFH_SLOT) * SlotCount * POOL_LFH_CLASS_COUNT);
    Heap->Cpu = PoolVaAllocateMetadata(Region, sizeof(POOL_HEAP_CPU) * SlotCount);

    if (Heap->Shard == NULL || Heap->Vs == NULL || Heap->LfhSlot == NULL || Heap->Cpu == NULL)
        return STATUS_NO_MEMORY;

    for (i = 0; i < ShardCount; i++)
    {
        ULONG List;

        POOL_LOCK_INIT(&Heap->Shard[i].Lock, Heap->Paged);
        InitializeListHead(&Heap->Shard[i].Available);

        POOL_LOCK_INIT(&Heap->Vs[i].Lock, Heap->Paged);
        InitializeListHead(&Heap->Vs[i].Subsegments);
        for (List = 0; List < POOL_VS_FREE_LISTS; List++)
            InitializeListHead(&Heap->Vs[i].FreeList[List]);
    }

    for (i = 0; i < SlotCount * POOL_LFH_CLASS_COUNT; i++)
    {
        POOL_LOCK_INIT(&Heap->LfhSlot[i].Lock, Heap->Paged);
        InitializeListHead(&Heap->LfhSlot[i].Partial);
        InitializeListHead(&Heap->LfhSlot[i].Full);
    }

    for (i = 0; i < POOL_LFH_CLASS_COUNT; i++)
        Heap->LfhClass[i].BlockSize = PoolLfhClassSize(i);

    Heap->Signature = POOL_HEAP_SIGNATURE;
    *OutHeap = Heap;
    return STATUS_SUCCESS;
}

NTSTATUS
PoolHeapAddRegion(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PPOOL_VA_REGION Region)
{
    if (Heap->RegionCount >= POOL_MAX_REGIONS || Region->Paged != Heap->Paged)
        return STATUS_INVALID_PARAMETER;

    Heap->Region[Heap->RegionCount] = Region;
    Heap->RegionCount++;
    return STATUS_SUCCESS;
}

BOOLEAN
PoolHeapContains(
    _In_ PPOOL_HEAP Heap,
    _In_ PVOID Block)
{
    return (PoolHeapRegionFromAddress(Heap, Block) != NULL);
}

FORCEINLINE
PPOOL_HEAP_CPU
PoolHeapCpu(
    _In_ PPOOL_HEAP Heap)
{
    return &Heap->Cpu[POOL_CURRENT_PROCESSOR() % Heap->SlotCount];
}

PVOID
PoolHeapAllocateEx(
    _Inout_ PPOOL_HEAP Heap,
    _In_ SIZE_T Bytes,
    _In_ ULONG Tag,
    _In_ ULONG Flags,
    _Out_opt_ PSIZE_T ActualBytes)
{
    PPOOL_HEAP_CPU Cpu = PoolHeapCpu(Heap);
    PVOID Block = NULL;
    SIZE_T Charged = 0;

    if (Bytes == 0)
        Bytes = 1;

    if (Flags & POOL_ALLOC_CACHE_ALIGNED)
    {
        if (Bytes > (SIZE_T)-1 - POOL_CACHE_ALIGNMENT)
            return NULL;

        Bytes = (Bytes + POOL_CACHE_ALIGNMENT - 1) & ~((SIZE_T)POOL_CACHE_ALIGNMENT - 1);
    }

    if (Bytes < PAGE_SIZE)
    {
        ULONG Class = PoolLfhClassFromSize(Bytes);

        if (Class < POOL_LFH_CLASS_COUNT)
        {
            PPOOL_LFH_CLASS LfhClass = &Heap->LfhClass[Class];

            if ((Flags & POOL_ALLOC_CACHE_ALIGNED) || POOL_ATOMIC_READ32(&LfhClass->Activity) >= POOL_LFH_ACTIVATION)
            {
                Block = PoolLfhAllocate(Heap, Class, Tag);
                Charged = LfhClass->BlockSize;
            }
            else
            {
                POOL_ATOMIC_ADD32(&LfhClass->Activity, 1);
            }
        }

        if (Block == NULL && !(Flags & POOL_ALLOC_CACHE_ALIGNED))
        {
            Block = PoolVsAllocate(Heap, Bytes, Tag, &Charged);
        }
    }

    if (Block == NULL && Bytes <= POOL_RANGE_MAX_BYTES)
    {
        ULONG Pages = (ULONG)((Bytes + PAGE_SIZE - 1) >> PAGE_SHIFT);

        Block = PoolSegAllocatePages(Heap, Pages, PoolRangeBlock, 0, Tag);
        Charged = (SIZE_T)Pages << PAGE_SHIFT;
    }
    else if (Block == NULL)
    {
        Block = PoolLargeAllocate(Heap, Bytes, Tag);
        Charged = (Bytes + PAGE_SIZE - 1) & ~((SIZE_T)PAGE_SIZE - 1);
    }

    if (Block == NULL)
    {
        POOL_ATOMIC_ADD64(&Cpu->Failures, 1);
        return NULL;
    }

    if (Flags & POOL_ALLOC_ZERO)
        RtlZeroMemory(Block, Bytes);

    if (ActualBytes != NULL)
        *ActualBytes = Charged;

    POOL_ATOMIC_ADD64(&Cpu->Allocations, 1);
    POOL_ATOMIC_ADD64(&Cpu->BytesInUse, (LONG64)Charged);
    return Block;
}

static
BOOLEAN
PoolHeapLocate(
    _In_ PPOOL_HEAP Heap,
    _In_ PVOID Block,
    _Out_ PPOOL_LOCATION Location)
{
    PPOOL_VA_REGION Region = PoolHeapRegionFromAddress(Heap, Block);
    PPOOL_SEGMENT Segment;
    PPOOL_PAGE_RANGE Range;
    UCHAR UnitState;

    Location->Kind = PoolBlockNone;
    if (Region == NULL)
        return FALSE;

    Location->Region = Region;
    UnitState = PoolVaUnit(Region, Block)->State;

    if (UnitState == PoolUnitLarge)
    {
        Location->Kind = PoolBlockLarge;
        return TRUE;
    }

    if (UnitState != PoolUnitSegment)
        return FALSE;

    Segment = PoolSegFromAddress(Block);
    if (Segment->Signature != POOL_SEGMENT_SIGNATURE || Segment->Heap != Heap)
        return FALSE;

    Range = &Segment->Range[PoolSegPageIndex(Block)];
    Location->Segment = Segment;
    Location->Range = &Segment->Range[Range->First];
    Location->Owner = PoolSegPageAddress(Segment, Range->First);

    switch (Range->State)
    {
        case PoolRangeLfh:
            Location->Kind = PoolBlockLfh;
            return TRUE;

        case PoolRangeVs:
            Location->Kind = PoolBlockVs;
            return TRUE;

        case PoolRangeBlock:
            if (Location->Owner != Block)
                return FALSE;

            Location->Kind = PoolBlockRange;
            return TRUE;

        default:
            return FALSE;
    }
}

BOOLEAN
PoolHeapQuery(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PVOID Block,
    _Out_ PPOOL_BLOCK_INFO Info)
{
    POOL_LOCATION Location;
    PPOOL_UNIT Unit;

    if (!PoolHeapLocate(Heap, Block, &Location))
        return FALSE;

    switch (Location.Kind)
    {
        case PoolBlockLfh:
            return PoolLfhQuery(Location.Owner, Block, Info);

        case PoolBlockVs:
            return PoolVsQuery(Heap, Location.Owner, Block, Info);

        case PoolBlockRange:
            Info->Size = (SIZE_T)Location.Range->Pages << PAGE_SHIFT;
            Info->Tag = Location.Range->Tag;
            Info->Flags = Location.Range->Flags;
            Info->Kind = PoolBlockRange;
            PoolCaptureTrailer(Block, Info);
            return TRUE;

        case PoolBlockLarge:
            Unit = PoolVaUnit(Location.Region, Block);
            if (((ULONG_PTR)Block & POOL_UNIT_MASK) != 0 || Unit->PageCount == 0)
                return FALSE;

            Info->Size = (SIZE_T)Unit->PageCount << PAGE_SHIFT;
            Info->Tag = Unit->Tag;
            Info->Flags = Unit->Flags;
            Info->Kind = PoolBlockLarge;
            PoolCaptureTrailer(Block, Info);
            return TRUE;

        default:
            return FALSE;
    }
}

BOOLEAN
PoolHeapSetFlags(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PVOID Block,
    _In_ UCHAR Flags)
{
    POOL_LOCATION Location;

    if (!PoolHeapLocate(Heap, Block, &Location))
        return FALSE;

    switch (Location.Kind)
    {
        case PoolBlockLfh:
            return PoolLfhSetFlags(Location.Owner, Block, Flags);

        case PoolBlockVs:
            return PoolVsSetFlags(Heap, Location.Owner, Block, Flags);

        case PoolBlockRange:
            Location.Range->Flags = Flags;
            return TRUE;

        case PoolBlockLarge:
            if (((ULONG_PTR)Block & POOL_UNIT_MASK) != 0)
                return FALSE;

            PoolVaUnit(Location.Region, Block)->Flags = Flags;
            return TRUE;

        default:
            return FALSE;
    }
}

BOOLEAN
PoolHeapFree(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PVOID Block,
    _Out_opt_ PPOOL_BLOCK_INFO Info)
{
    PPOOL_HEAP_CPU Cpu = PoolHeapCpu(Heap);
    POOL_LOCATION Location;
    POOL_BLOCK_INFO Local;
    BOOLEAN Freed = FALSE;

    if (Info == NULL)
        Info = &Local;

    if (!PoolHeapLocate(Heap, Block, &Location))
        return FALSE;

    switch (Location.Kind)
    {
        case PoolBlockLfh:
            Freed = PoolLfhFree(Heap, Location.Owner, Block, Info);
            break;

        case PoolBlockVs:
            Freed = PoolVsFree(Heap, Location.Owner, Block, Info);
            break;

        case PoolBlockRange:
            Info->Size = (SIZE_T)Location.Range->Pages << PAGE_SHIFT;
            Info->Tag = Location.Range->Tag;
            Info->Flags = Location.Range->Flags;
            Info->Kind = PoolBlockRange;
            PoolCaptureTrailer(Block, Info);

            if (Heap->Options & POOL_HEAP_POISON_ON_FREE)
                RtlFillMemory(Block, Info->Size, POOL_FREE_POISON);

            PoolSegFreePages(Heap, Block);
            Freed = TRUE;
            break;

        case PoolBlockLarge:
            Freed = PoolLargeFree(Heap, Location.Region, Block, Info);
            break;

        default:
            break;
    }

    if (Freed)
    {
        POOL_ATOMIC_ADD64(&Cpu->Frees, 1);
        POOL_ATOMIC_ADD64(&Cpu->BytesInUse, -(LONG64)Info->Size);
    }

    return Freed;
}

VOID
PoolHeapTrim(
    _Inout_ PPOOL_HEAP Heap)
{
    ULONG i;

    PoolLfhTrim(Heap);
    PoolVsTrim(Heap);

    for (i = 0; i < Heap->ShardCount; i++)
        PoolSegTrimShard(Heap, i);
}

VOID
PoolHeapQueryStatistics(
    _In_ PPOOL_HEAP Heap,
    _Out_ PPOOL_HEAP_STATISTICS Statistics)
{
    ULONG i;

    RtlZeroMemory(Statistics, sizeof(*Statistics));

    for (i = 0; i < Heap->SlotCount; i++)
    {
        Statistics->Allocations += (ULONG64)Heap->Cpu[i].Allocations;
        Statistics->Frees += (ULONG64)Heap->Cpu[i].Frees;
        Statistics->BytesInUse += Heap->Cpu[i].BytesInUse;
        Statistics->Failures += (ULONG64)Heap->Cpu[i].Failures;
    }

    for (i = 0; i < Heap->RegionCount; i++)
    {
        Statistics->CommittedPages += POOL_ATOMIC_READ64(&Heap->Region[i]->CommittedPages);
        Statistics->PeakCommittedPages += POOL_ATOMIC_READ64(&Heap->Region[i]->PeakCommittedPages);
        Statistics->ReservedUnits += Heap->Region[i]->UnitsInUse;
    }

    for (i = 0; i < Heap->ShardCount; i++)
    {
        Statistics->SegmentCount += Heap->Shard[i].SegmentCount;
        Statistics->VsSubsegmentCount += Heap->Vs[i].SubsegmentCount;
    }

    for (i = 0; i < Heap->SlotCount * POOL_LFH_CLASS_COUNT; i++)
        Statistics->LfhSubsegmentCount += Heap->LfhSlot[i].SubsegmentCount;

    Statistics->LargeCount = (ULONG)Heap->LargeCount;
}
