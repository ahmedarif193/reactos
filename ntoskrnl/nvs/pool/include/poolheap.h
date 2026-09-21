/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/include/poolheap.h
 * PURPOSE:     Pool heap structures and interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "poolva.h"

typedef enum _POOL_BLOCK_KIND
{
    PoolBlockNone = 0,
    PoolBlockLfh,
    PoolBlockVs,
    PoolBlockRange,
    PoolBlockLarge
} POOL_BLOCK_KIND;

typedef struct _POOL_BLOCK_INFO
{
    SIZE_T Size;
    ULONG Tag;
    UCHAR Flags;
    UCHAR Kind;
    ULONG_PTR Trailer;
} POOL_BLOCK_INFO, *PPOOL_BLOCK_INFO;

FORCEINLINE
VOID
PoolCaptureTrailer(_In_ PVOID Block, _Inout_ PPOOL_BLOCK_INFO Info)
{
    Info->Trailer = 0;
    if (Info->Flags != 0)
        Info->Trailer = *(ULONG_PTR *)((ULONG_PTR)Block + Info->Size - sizeof(ULONG_PTR));
}

#include "poolseg.h"
#include "poollfh.h"
#include "poolvs.h"

typedef struct _POOL_HEAP_CPU
{
    volatile LONG64 Allocations;
    volatile LONG64 Frees;
    volatile LONG64 BytesInUse;
    volatile LONG64 Failures;
} POOL_CACHE_ALIGNED POOL_HEAP_CPU, *PPOOL_HEAP_CPU;

typedef struct _POOL_HEAP_STATISTICS
{
    ULONG64 Allocations;
    ULONG64 Frees;
    LONG64 BytesInUse;
    ULONG64 Failures;
    LONG64 CommittedPages;
    LONG64 PeakCommittedPages;
    SIZE_T ReservedUnits;
    ULONG SegmentCount;
    ULONG LfhSubsegmentCount;
    ULONG VsSubsegmentCount;
    ULONG LargeCount;
} POOL_HEAP_STATISTICS, *PPOOL_HEAP_STATISTICS;

typedef struct _POOL_HEAP
{
    ULONG Signature;
    ULONG HeapId;
    ULONG Options;
    BOOLEAN Paged;
    ULONG SlotCount;
    ULONG ShardCount;
    ULONG RegionCount;
    ULONG64 EncodeKey;
    PPOOL_VA_REGION Region[POOL_MAX_REGIONS];
    PPOOL_SEGMENT_SHARD Shard;
    PPOOL_VS_CONTEXT Vs;
    PPOOL_LFH_SLOT LfhSlot;
    PPOOL_HEAP_CPU Cpu;
    POOL_LFH_CLASS LfhClass[POOL_LFH_CLASS_COUNT];
    volatile LONG LargeCount;
} POOL_HEAP, *PPOOL_HEAP;

NTSTATUS PoolHeapCreate(_Out_ PPOOL_HEAP *Heap, _In_ PPOOL_VA_REGION Region, _In_ ULONG HeapId,
                        _In_ ULONG ProcessorCount, _In_ ULONG64 EncodeKey, _In_ ULONG Options);
NTSTATUS PoolHeapAddRegion(_Inout_ PPOOL_HEAP Heap, _In_ PPOOL_VA_REGION Region);
PVOID PoolHeapAllocateEx(_Inout_ PPOOL_HEAP Heap, _In_ SIZE_T Bytes, _In_ ULONG Tag, _In_ ULONG Flags,
                         _Out_opt_ PSIZE_T ActualBytes);
BOOLEAN PoolHeapFree(_Inout_ PPOOL_HEAP Heap, _In_ PVOID Block, _Out_opt_ PPOOL_BLOCK_INFO Info);
BOOLEAN PoolHeapQuery(_Inout_ PPOOL_HEAP Heap, _In_ PVOID Block, _Out_ PPOOL_BLOCK_INFO Info);
BOOLEAN PoolHeapSetFlags(_Inout_ PPOOL_HEAP Heap, _In_ PVOID Block, _In_ UCHAR Flags);
BOOLEAN PoolHeapContains(_In_ PPOOL_HEAP Heap, _In_ PVOID Block);
VOID PoolHeapTrim(_Inout_ PPOOL_HEAP Heap);
VOID PoolHeapQueryStatistics(_In_ PPOOL_HEAP Heap, _Out_ PPOOL_HEAP_STATISTICS Statistics);

typedef BOOLEAN (*POOL_ENUMERATE_ROUTINE)(_In_opt_ PVOID Context, _In_ PVOID Block, _In_ PPOOL_BLOCK_INFO Info);

BOOLEAN PoolHeapDescribe(_In_ PPOOL_HEAP Heap, _In_ PVOID Address, _Out_ PVOID *Block,
                         _Out_ PPOOL_BLOCK_INFO Info, _Out_ PBOOLEAN Allocated);
VOID PoolHeapEnumerate(_In_ PPOOL_HEAP Heap, _In_ POOL_ENUMERATE_ROUTINE Routine, _In_opt_ PVOID Context);

PVOID PoolLargeAllocate(_Inout_ PPOOL_HEAP Heap, _In_ SIZE_T Bytes, _In_ ULONG Tag);
BOOLEAN PoolLargeFree(_Inout_ PPOOL_HEAP Heap, _In_ PPOOL_VA_REGION Region, _In_ PVOID Block,
                      _Out_ PPOOL_BLOCK_INFO Info);

FORCEINLINE
PVOID
PoolHeapAllocate(_Inout_ PPOOL_HEAP Heap, _In_ SIZE_T Bytes, _In_ ULONG Tag, _In_ ULONG Flags)
{
    return PoolHeapAllocateEx(Heap, Bytes, Tag, Flags, NULL);
}

FORCEINLINE
PPOOL_VA_REGION
PoolHeapRegionFromAddress(_In_ PPOOL_HEAP Heap, _In_ PVOID Address)
{
    ULONG i;

    for (i = 0; i < Heap->RegionCount; i++)
    {
        if (PoolVaContains(Heap->Region[i], Address))
            return Heap->Region[i];
    }

    return NULL;
}

FORCEINLINE
PPOOL_LFH_SLOT
PoolHeapLfhSlot(_In_ PPOOL_HEAP Heap, _In_ ULONG Class, _In_ ULONG Slot)
{
    return &Heap->LfhSlot[Class * Heap->SlotCount + Slot];
}
