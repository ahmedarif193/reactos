/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/ex/expool.h
 * PURPOSE:     Executive pool integration definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "../include/poolenv.h"
#include "../include/poolheap.h"
#include "../include/pooltrack.h"
#include "../include/poolback.h"

#if defined(POOL_HOST_TEST)
#include <mmcc/pool/host/ntpoolshim.h>
#endif

#define EX_POOL_HEAP_NONPAGED       0
#define EX_POOL_HEAP_PAGED          1
#define EX_POOL_HEAP_EXECUTABLE     2
#define EX_POOL_HEAP_COUNT          3
#define EX_POOL_BASE_TYPE_MASK      1
#define EX_POOL_BOOT_SLOTS          16
#define EX_POOL_BLOCK_QUOTA         0x01
#define EX_POOL_FLAG_SPECIAL_POOL   0x20

typedef struct _EX_POOL_STATE
{
    PPOOL_HEAP Heap[EX_POOL_HEAP_COUNT];
    POOL_VA_REGION Region[EX_POOL_HEAP_COUNT][2];
    POOL_TAG_TRACKER Tracker;
    volatile LONG TrackerExpanding;
    volatile LONG ExpansionPending;
    volatile LONG ExpansionAttaching;
} EX_POOL_STATE, *PEX_POOL_STATE;

extern EX_POOL_STATE ExpPoolState;
extern ULONG ExpPoolFlags;

FORCEINLINE
ULONG
ExpPoolClass(_In_ ULONG HeapIndex)
{
    return HeapIndex == EX_POOL_HEAP_PAGED ? EX_POOL_HEAP_PAGED : EX_POOL_HEAP_NONPAGED;
}

FORCEINLINE
ULONG
ExpPoolHeapFromType(_In_ POOL_TYPE PoolType)
{
    if (PoolType & EX_POOL_BASE_TYPE_MASK)
        return EX_POOL_HEAP_PAGED;
    return (PoolType & POOL_NX_ALLOCATION) ? EX_POOL_HEAP_NONPAGED : EX_POOL_HEAP_EXECUTABLE;
}

FORCEINLINE
POOL_TYPE
ExpPoolTypeFromHeap(_In_ ULONG HeapIndex)
{
    if (HeapIndex == EX_POOL_HEAP_PAGED)
        return PagedPool;
    return HeapIndex == EX_POOL_HEAP_EXECUTABLE ? NonPagedPoolExecute : NonPagedPoolNx;
}

PVOID ExpPoolAllocate(_In_ ULONG HeapIndex, _In_ SIZE_T NumberOfBytes, _In_ ULONG Tag, _In_ ULONG HeapFlags);
BOOLEAN ExpPoolAttachExpansion(VOID);
PPOOL_HEAP ExpPoolHeapFromAddress(_In_ PVOID Block, _Out_ PULONG HeapIndex);
VOID MiDumpPoolConsumers(_In_ BOOLEAN CalledFromDbg, _In_ ULONG Tag, _In_ ULONG Mask, _In_ ULONG Flags);
