/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/ex/exinit.c
 * PURPOSE:     Executive pool initialization
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "expool.h"

#define NDEBUG
#if !defined(POOL_HOST_TEST)
#include <debug.h>
#endif

EX_POOL_STATE ExpPoolState;
ULONG ExpPoolFlags;

static POOL_TAG_TABLE ExpBootTagTable;

static
ULONG64
ExpPoolEncodeKey(VOID)
{
    ULONG64 Key = 0x9E3779B97F4A7C15ULL;

    Key ^= (ULONG64)KeQueryInterruptTime();
    Key ^= (ULONG64)(ULONG_PTR)&Key << 17;
    Key ^= (ULONG64)(ULONG_PTR)&ExpPoolState >> 3;
    return Key;
}

static
ULONG
ExpPoolHeapOptions(VOID)
{
#if DBG
    return POOL_HEAP_POISON_ON_FREE;
#else
    return 0;
#endif
}

BOOLEAN
ExpPoolAttachExpansion(VOID)
{
    PPOOL_VA_REGION Region = &ExpPoolState.Region[EX_POOL_HEAP_NONPAGED][1];
    POOL_KM_LAYOUT Layout;
    POOL_BACKING Backing;
    BOOLEAN Attached = FALSE;

    if (!ExpPoolState.ExpansionPending)
        return FALSE;

    if (InterlockedCompareExchange(&ExpPoolState.ExpansionAttaching, 1, 0) != 0)
        return FALSE;

    if (ExpPoolState.ExpansionPending)
    {
        PoolBackQueryLayout(&Layout);
        PoolBackDemandBacking(&Backing, FALSE);

        if (NT_SUCCESS(PoolVaRegionInitialize(Region, Layout.NonPagedExpansionBase,
                                              Layout.NonPagedExpansionBytes, FALSE, &Backing)) &&
            NT_SUCCESS(PoolHeapAddRegion(ExpPoolState.Heap[EX_POOL_HEAP_NONPAGED], Region)))
        {
            InterlockedExchange(&ExpPoolState.ExpansionPending, 0);
            Attached = TRUE;
        }
    }

    InterlockedExchange(&ExpPoolState.ExpansionAttaching, 0);
    return Attached;
}

CODE_SEG("INIT")
VOID
NTAPI
InitializePool(
    IN POOL_TYPE PoolType,
    IN ULONG Threshold)
{
    POOL_KM_LAYOUT Layout;
    POOL_BACKING Backing;
    PPOOL_VA_REGION First;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Threshold);
    PoolBackQueryLayout(&Layout);

    if (PoolType == NonPagedPool)
    {
        First = &ExpPoolState.Region[EX_POOL_HEAP_NONPAGED][0];

        PoolTagTrackerInitialize(&ExpPoolState.Tracker);
        PoolTagTrackerAddTable(&ExpPoolState.Tracker, &ExpBootTagTable);

        PoolBackResidentBacking(&Backing);
        Status = PoolVaRegionInitialize(First, Layout.NonPagedResidentBase, Layout.NonPagedResidentBytes,
                                        FALSE, &Backing);
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 0, (ULONG_PTR)Status, 0, 0);

        Status = PoolHeapCreate(&ExpPoolState.Heap[EX_POOL_HEAP_NONPAGED], First, EX_POOL_HEAP_NONPAGED,
                                EX_POOL_BOOT_SLOTS, ExpPoolEncodeKey(), ExpPoolHeapOptions());
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 1, (ULONG_PTR)Status, 0, 0);

        ExpPoolState.ExpansionPending = 1;
        ExpPoolAttachExpansion();

        First = &ExpPoolState.Region[EX_POOL_HEAP_EXECUTABLE][0];
        PoolBackDemandBacking(&Backing, TRUE);
        Status = PoolVaRegionInitialize(First, Layout.ExecutableBase, Layout.ExecutableBytes, FALSE, &Backing);
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 4, (ULONG_PTR)Status, 0, 0);

        Status = PoolHeapCreate(&ExpPoolState.Heap[EX_POOL_HEAP_EXECUTABLE], First, EX_POOL_HEAP_EXECUTABLE,
                                EX_POOL_BOOT_SLOTS, ExpPoolEncodeKey(), ExpPoolHeapOptions());
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 5, (ULONG_PTR)Status, 0, 0);
        return;
    }

    ExpPoolAttachExpansion();

    First = &ExpPoolState.Region[EX_POOL_HEAP_PAGED][0];
    PoolBackDemandBacking(&Backing, FALSE);

    Status = PoolVaRegionInitialize(First, Layout.PagedBase, Layout.PagedBytes, TRUE, &Backing);
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 2, (ULONG_PTR)Status, 0, 0);

    Status = PoolHeapCreate(&ExpPoolState.Heap[EX_POOL_HEAP_PAGED], First, EX_POOL_HEAP_PAGED,
                            EX_POOL_BOOT_SLOTS, ExpPoolEncodeKey(), ExpPoolHeapOptions());
    if (!NT_SUCCESS(Status))
        KeBugCheckEx(MUST_SUCCEED_POOL_EMPTY, 3, (ULONG_PTR)Status, 0, 0);
}

VOID
NTAPI
ExInitializePoolDescriptor(
    IN PVOID PoolDescriptor,
    IN POOL_TYPE PoolType,
    IN ULONG PoolIndex,
    IN ULONG Threshold,
    IN PVOID PoolLock)
{
    UNREFERENCED_PARAMETER(PoolDescriptor);
    UNREFERENCED_PARAMETER(PoolType);
    UNREFERENCED_PARAMETER(PoolIndex);
    UNREFERENCED_PARAMETER(Threshold);
    UNREFERENCED_PARAMETER(PoolLock);
}
