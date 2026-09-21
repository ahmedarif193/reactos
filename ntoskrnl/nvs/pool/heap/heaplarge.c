/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/heap/heaplarge.c
 * PURPOSE:     Large pool heap allocations
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/poolenv.h"
#include "../include/poolheap.h"

PVOID
PoolLargeAllocate(
    _Inout_ PPOOL_HEAP Heap,
    _In_ SIZE_T Bytes,
    _In_ ULONG Tag)
{
    SIZE_T CommitBytes = (Bytes + PAGE_SIZE - 1) & ~((SIZE_T)PAGE_SIZE - 1);
    SIZE_T Units;
    ULONG i;

    if (CommitBytes < Bytes || (CommitBytes >> PAGE_SHIFT) > 0xFFFFFFFFULL)
        return NULL;

    Units = (CommitBytes + POOL_UNIT_MASK) >> POOL_UNIT_SHIFT;

    for (i = 0; i < Heap->RegionCount; i++)
    {
        PPOOL_VA_REGION Region = Heap->Region[i];
        PPOOL_UNIT Unit;
        PVOID Base;

        Base = PoolVaReserve(Region, Units, PoolUnitLarge);
        if (Base == NULL)
            continue;

        if (!NT_SUCCESS(PoolVaCommit(Region, Base, CommitBytes)))
        {
            PoolVaRelease(Region, Base, Units);
            continue;
        }

        Unit = PoolVaUnit(Region, Base);
        Unit->Tag = Tag;
        Unit->Flags = 0;
        Unit->PageCount = (ULONG)(CommitBytes >> PAGE_SHIFT);
        POOL_ATOMIC_ADD32(&Heap->LargeCount, 1);
        return Base;
    }

    return NULL;
}

BOOLEAN
PoolLargeFree(
    _Inout_ PPOOL_HEAP Heap,
    _In_ PPOOL_VA_REGION Region,
    _In_ PVOID Block,
    _Out_ PPOOL_BLOCK_INFO Info)
{
    PPOOL_UNIT Unit = PoolVaUnit(Region, Block);
    SIZE_T Units;
    SIZE_T Bytes;

    if (((ULONG_PTR)Block & POOL_UNIT_MASK) != 0 || Unit->State != PoolUnitLarge ||
        Unit->PageCount == 0)
    {
        return FALSE;
    }

    Units = Unit->UnitCount;
    Bytes = (SIZE_T)Unit->PageCount << PAGE_SHIFT;

    Info->Size = Bytes;
    Info->Tag = Unit->Tag;
    Info->Flags = Unit->Flags;
    Info->Kind = PoolBlockLarge;
    PoolCaptureTrailer(Block, Info);

    Unit->PageCount = 0;
    PoolVaDecommit(Region, Block, Bytes);
    PoolVaRelease(Region, Block, Units);
    POOL_ATOMIC_ADD32(&Heap->LargeCount, -1);
    return TRUE;
}
