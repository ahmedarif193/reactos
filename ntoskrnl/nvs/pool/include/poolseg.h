/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/include/poolseg.h
 * PURPOSE:     Pool segment structures and interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

typedef enum _POOL_RANGE_STATE
{
    PoolRangeFree = 0,
    PoolRangeHeader,
    PoolRangeLfh,
    PoolRangeVs,
    PoolRangeBlock,
    PoolRangeDraining
} POOL_RANGE_STATE;

typedef struct _POOL_PAGE_RANGE
{
    UCHAR State;
    UCHAR Flags;
    USHORT First;
    USHORT Pages;
    USHORT Owner;
    ULONG Tag;
} POOL_PAGE_RANGE, *PPOOL_PAGE_RANGE;

C_ASSERT(sizeof(POOL_PAGE_RANGE) == 12);

struct _POOL_HEAP;

typedef struct _POOL_SEGMENT
{
    ULONG Signature;
    USHORT Shard;
    USHORT FreePages;
    USHORT HeaderPages;
    BOOLEAN OnAvailable;
    UCHAR RegionIndex;
    LIST_ENTRY AvailableLink;
    struct _POOL_HEAP *Heap;
    ULONG64 BusyMap[POOL_SEGMENT_MAP_WORDS];
    ULONG64 CommitMap[POOL_SEGMENT_MAP_WORDS];
    POOL_PAGE_RANGE Range[POOL_SEGMENT_PAGES];
} POOL_SEGMENT, *PPOOL_SEGMENT;

typedef struct _POOL_SEGMENT_SHARD
{
    POOL_LOCK Lock;
    LIST_ENTRY Available;
    ULONG SegmentCount;
    ULONG FreeCommittedPages;
    ULONG64 RangeAllocations;
    ULONG64 RangeFrees;
} POOL_CACHE_ALIGNED POOL_SEGMENT_SHARD, *PPOOL_SEGMENT_SHARD;

PVOID PoolSegAllocatePages(_Inout_ struct _POOL_HEAP *Heap, _In_ ULONG PageCount, _In_ UCHAR State,
                           _In_ USHORT Owner, _In_ ULONG Tag);
VOID PoolSegFreePages(_Inout_ struct _POOL_HEAP *Heap, _In_ PVOID Base);
VOID PoolSegTrimShard(_Inout_ struct _POOL_HEAP *Heap, _In_ ULONG ShardIndex);

FORCEINLINE
PPOOL_SEGMENT
PoolSegFromAddress(_In_ PVOID Address)
{
    return (PPOOL_SEGMENT)((ULONG_PTR)Address & ~(ULONG_PTR)POOL_UNIT_MASK);
}

FORCEINLINE
ULONG
PoolSegPageIndex(_In_ PVOID Address)
{
    return (ULONG)(((ULONG_PTR)Address & POOL_UNIT_MASK) >> PAGE_SHIFT);
}

FORCEINLINE
PVOID
PoolSegPageAddress(_In_ PPOOL_SEGMENT Segment, _In_ ULONG Index)
{
    return (PVOID)((ULONG_PTR)Segment + ((ULONG_PTR)Index << PAGE_SHIFT));
}
