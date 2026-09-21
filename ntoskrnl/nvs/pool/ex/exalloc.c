/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/ex/exalloc.c
 * PURPOSE:     Executive pool allocation interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "expool.h"

#define NDEBUG
#if !defined(POOL_HOST_TEST)
#include <debug.h>
#endif

PPOOL_HEAP
ExpPoolHeapFromAddress(
    _In_ PVOID Block,
    _Out_ PULONG HeapIndex)
{
    ULONG i;

    for (i = 0; i < EX_POOL_HEAP_COUNT; i++)
    {
        PPOOL_HEAP Heap = ExpPoolState.Heap[i];

        if (Heap != NULL && PoolHeapContains(Heap, Block))
        {
            *HeapIndex = i;
            return Heap;
        }
    }

    *HeapIndex = EX_POOL_HEAP_COUNT;
    return NULL;
}

static
VOID
ExpPoolExpandTracker(VOID)
{
    PPOOL_TAG_TRACKER Tracker = &ExpPoolState.Tracker;
    ULONG Wanted = (ULONG)KeNumberProcessors;
    PPOOL_TAG_TABLE Table;

    if (Wanted > EX_POOL_BOOT_SLOTS)
        Wanted = EX_POOL_BOOT_SLOTS;

    if (Tracker->TableCount >= Wanted)
        return;

    if (InterlockedCompareExchange(&ExpPoolState.TrackerExpanding, 1, 0) != 0)
        return;

    while (Tracker->TableCount < Wanted)
    {
        Table = PoolHeapAllocate(ExpPoolState.Heap[EX_POOL_HEAP_NONPAGED], sizeof(POOL_TAG_TABLE),
                                 'TlpX', 0);
        if (Table == NULL || !PoolTagTrackerAddTable(Tracker, Table))
            break;
    }

    InterlockedExchange(&ExpPoolState.TrackerExpanding, 0);
}

PVOID
ExpPoolAllocate(
    _In_ ULONG HeapIndex,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Tag,
    _In_ ULONG HeapFlags)
{
    PPOOL_HEAP Heap;
    SIZE_T Actual;
    PVOID Block;

    if (HeapIndex == EX_POOL_HEAP_PAGED && ExpPoolState.Heap[EX_POOL_HEAP_PAGED] == NULL)
        HeapIndex = EX_POOL_HEAP_NONPAGED;

    Heap = ExpPoolState.Heap[HeapIndex];
    if (Heap == NULL)
        return NULL;

    if (ExpPoolState.Tracker.TableCount < (ULONG)KeNumberProcessors)
        ExpPoolExpandTracker();

    Block = PoolHeapAllocateEx(Heap, NumberOfBytes, Tag, HeapFlags, &Actual);
    if (Block == NULL && HeapIndex == EX_POOL_HEAP_NONPAGED && ExpPoolAttachExpansion())
        Block = PoolHeapAllocateEx(Heap, NumberOfBytes, Tag, HeapFlags, &Actual);

    if (Block != NULL)
    {
        PoolTagCharge(&ExpPoolState.Tracker, Tag, ExpPoolClass(HeapIndex), Actual);
        return Block;
    }

    DPRINT1("Pool allocation failed: heap %lu, %Iu bytes, tag %.4s\n", HeapIndex, NumberOfBytes, (PCHAR)&Tag);
    return NULL;
}

PVOID
NTAPI
ExAllocatePoolWithTag(
    IN POOL_TYPE PoolType,
    IN SIZE_T NumberOfBytes,
    IN ULONG Tag)
{
    ULONG HeapIndex = ExpPoolHeapFromType(PoolType);
    ULONG HeapFlags = (PoolType & CACHE_ALIGNED_POOL_MASK) ? POOL_ALLOC_CACHE_ALIGNED : 0;
    PVOID Block;

    if (PoolType & POOL_ZERO_ALLOCATION)
        HeapFlags |= POOL_ALLOC_ZERO;

    if ((ExpPoolFlags & EX_POOL_FLAG_SPECIAL_POOL) && MmUseSpecialPool(NumberOfBytes, Tag))
    {
        Block = MmAllocateSpecialPool(NumberOfBytes, Tag, PoolType, 2);
        if (Block != NULL)
        {
            if (HeapFlags & POOL_ALLOC_ZERO)
                RtlZeroMemory(Block, NumberOfBytes);

            return Block;
        }
    }

    Block = ExpPoolAllocate(HeapIndex, NumberOfBytes, Tag, HeapFlags);
    if (Block != NULL)
        return Block;

    if (PoolType & MUST_SUCCEED_POOL_MASK)
        KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, NumberOfBytes, HeapIndex, Tag, 0);

    if (PoolType & POOL_RAISE_IF_ALLOCATION_FAILURE)
        ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

    return NULL;
}

PVOID
NTAPI
ExAllocatePool(
    POOL_TYPE PoolType,
    SIZE_T NumberOfBytes)
{
    return ExAllocatePoolWithTag(PoolType, NumberOfBytes, TAG_NONE);
}

PVOID
NTAPI
ExAllocatePoolWithTagPriority(
    IN POOL_TYPE PoolType,
    IN SIZE_T NumberOfBytes,
    IN ULONG Tag,
    IN EX_POOL_PRIORITY Priority)
{
    UNREFERENCED_PARAMETER(Priority);
    return ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);
}

PVOID
NTAPI
ExAllocatePool2(
    IN POOL_FLAGS Flags,
    IN SIZE_T NumberOfBytes,
    IN ULONG Tag)
{
    ULONG HeapIndex = (Flags & POOL_FLAG_PAGED) ? EX_POOL_HEAP_PAGED :
                      (Flags & POOL_FLAG_NON_PAGED_EXECUTE) ? EX_POOL_HEAP_EXECUTABLE : EX_POOL_HEAP_NONPAGED;
    ULONG HeapFlags = 0;
    PVOID Block;

    if (Tag == 0 || NumberOfBytes == 0)
    {
        if (Flags & POOL_FLAG_RAISE_ON_FAILURE)
            ExRaiseStatus(STATUS_INVALID_PARAMETER);

        return NULL;
    }

    if (Flags & POOL_FLAG_USE_QUOTA)
    {
        POOL_TYPE PoolType = ExpPoolTypeFromHeap(HeapIndex);

        if (Flags & POOL_FLAG_CACHE_ALIGNED)
            PoolType |= CACHE_ALIGNED_POOL_MASK;
        if (!(Flags & POOL_FLAG_RAISE_ON_FAILURE))
            PoolType |= POOL_QUOTA_FAIL_INSTEAD_OF_RAISE;

        Block = ExAllocatePoolWithQuotaTag(PoolType, NumberOfBytes, Tag);
        if (Block != NULL && !(Flags & POOL_FLAG_UNINITIALIZED))
            RtlZeroMemory(Block, NumberOfBytes);

        return Block;
    }

    if (!(Flags & POOL_FLAG_UNINITIALIZED))
        HeapFlags |= POOL_ALLOC_ZERO;
    if (Flags & POOL_FLAG_CACHE_ALIGNED)
        HeapFlags |= POOL_ALLOC_CACHE_ALIGNED;

    if ((ExpPoolFlags & EX_POOL_FLAG_SPECIAL_POOL) && MmUseSpecialPool(NumberOfBytes, Tag))
    {
        Block = MmAllocateSpecialPool(NumberOfBytes, Tag,
                                      ExpPoolTypeFromHeap(HeapIndex), 2);
        if (Block != NULL)
        {
            if (HeapFlags & POOL_ALLOC_ZERO)
                RtlZeroMemory(Block, NumberOfBytes);

            return Block;
        }
    }

    Block = ExpPoolAllocate(HeapIndex, NumberOfBytes, Tag, HeapFlags);
    if (Block == NULL && (Flags & POOL_FLAG_RAISE_ON_FAILURE))
        ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

    return Block;
}

PVOID
NTAPI
ExAllocatePool3(
    IN POOL_FLAGS Flags,
    IN SIZE_T NumberOfBytes,
    IN ULONG Tag,
    IN PCPOOL_EXTENDED_PARAMETER ExtendedParameters,
    IN ULONG ExtendedParametersCount)
{
    UNREFERENCED_PARAMETER(ExtendedParameters);
    UNREFERENCED_PARAMETER(ExtendedParametersCount);
    return ExAllocatePool2(Flags, NumberOfBytes, Tag);
}
