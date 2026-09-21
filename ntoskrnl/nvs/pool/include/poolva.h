/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/include/poolva.h
 * PURPOSE:     Pool virtual address management definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

typedef NTSTATUS (*POOL_COMMIT_ROUTINE)(_In_opt_ PVOID Context, _In_ PVOID Base, _In_ SIZE_T Bytes);
typedef VOID (*POOL_DECOMMIT_ROUTINE)(_In_opt_ PVOID Context, _In_ PVOID Base, _In_ SIZE_T Bytes);

typedef struct _POOL_BACKING
{
    POOL_COMMIT_ROUTINE Commit;
    POOL_DECOMMIT_ROUTINE Decommit;
    PVOID Context;
} POOL_BACKING, *PPOOL_BACKING;

typedef enum _POOL_UNIT_STATE
{
    PoolUnitFree = 0,
    PoolUnitMetadata,
    PoolUnitSegment,
    PoolUnitLarge,
    PoolUnitTail
} POOL_UNIT_STATE;

typedef struct _POOL_UNIT
{
    volatile UCHAR State;
    UCHAR Flags;
    USHORT Spare;
    ULONG Tag;
    ULONG UnitCount;
    ULONG PageCount;
} POOL_UNIT, *PPOOL_UNIT;

C_ASSERT(sizeof(POOL_UNIT) == 16);

typedef struct _POOL_VA_REGION
{
    ULONG_PTR Base;
    ULONG_PTR End;
    SIZE_T UnitCount;
    PPOOL_UNIT Directory;
    PULONG64 BusyMap;
    SIZE_T MapWords;
    POOL_BACKING Backing;
    BOOLEAN Paged;

    POOL_LOCK Lock;
    SIZE_T Hint;
    SIZE_T UnitsInUse;
    ULONG_PTR MetadataNext;
    ULONG_PTR MetadataCommitted;
    ULONG_PTR MetadataEnd;

    volatile LONG64 CommittedPages;
    volatile LONG64 PeakCommittedPages;
} POOL_VA_REGION, *PPOOL_VA_REGION;

NTSTATUS PoolVaRegionInitialize(_Out_ PPOOL_VA_REGION Region, _In_ PVOID Base, _In_ SIZE_T Bytes,
                                _In_ BOOLEAN Paged, _In_ PPOOL_BACKING Backing);
PVOID PoolVaReserve(_Inout_ PPOOL_VA_REGION Region, _In_ SIZE_T UnitCount, _In_ UCHAR State);
VOID PoolVaRelease(_Inout_ PPOOL_VA_REGION Region, _In_ PVOID Base, _In_ SIZE_T UnitCount);
PVOID PoolVaAllocateMetadata(_Inout_ PPOOL_VA_REGION Region, _In_ SIZE_T Bytes);
NTSTATUS PoolVaCommit(_Inout_ PPOOL_VA_REGION Region, _In_ PVOID Base, _In_ SIZE_T Bytes);
VOID PoolVaDecommit(_Inout_ PPOOL_VA_REGION Region, _In_ PVOID Base, _In_ SIZE_T Bytes);

FORCEINLINE
BOOLEAN
PoolVaContains(_In_ PPOOL_VA_REGION Region, _In_ PVOID Address)
{
    return ((ULONG_PTR)Address >= Region->Base && (ULONG_PTR)Address < Region->End);
}

FORCEINLINE
PPOOL_UNIT
PoolVaUnit(_In_ PPOOL_VA_REGION Region, _In_ PVOID Address)
{
    return &Region->Directory[((ULONG_PTR)Address - Region->Base) >> POOL_UNIT_SHIFT];
}

FORCEINLINE
PVOID
PoolVaUnitBase(_In_ PVOID Address)
{
    return (PVOID)((ULONG_PTR)Address & ~(ULONG_PTR)POOL_UNIT_MASK);
}
