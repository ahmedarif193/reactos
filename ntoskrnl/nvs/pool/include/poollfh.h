/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/include/poollfh.h
 * PURPOSE:     Low-fragmentation pool heap definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

struct _POOL_LFH_SLOT;

typedef struct _POOL_LFH_SUBSEGMENT
{
    LIST_ENTRY Link;
    struct _POOL_LFH_SLOT *Slot;
    PUCHAR BlockBase;
    PULONG Tags;
    PUCHAR Flags;
    ULONG Signature;
    PUCHAR PagedBase;
    USHORT BlockSize;
    USHORT BlockCount;
    USHORT FreeCount;
    USHORT HintWord;
    USHORT MapWords;
    USHORT Pages;
    USHORT FirstCount;
    USHORT PerPage;
    USHORT Lead;
    UCHAR Class;
    BOOLEAN OnFullList;
    ULONG64 Bitmap[ANYSIZE_ARRAY];
} POOL_LFH_SUBSEGMENT, *PPOOL_LFH_SUBSEGMENT;

FORCEINLINE
PUCHAR
PoolLfhBlockAddress(
    _In_ PPOOL_LFH_SUBSEGMENT Subsegment,
    _In_ ULONG Bit)
{
    if (Bit < Subsegment->FirstCount)
        return Subsegment->BlockBase + (SIZE_T)Bit * Subsegment->BlockSize;

    Bit -= Subsegment->FirstCount;
    return Subsegment->PagedBase + ((SIZE_T)(Bit / Subsegment->PerPage) << PAGE_SHIFT) + Subsegment->Lead +
           (SIZE_T)(Bit % Subsegment->PerPage) * Subsegment->BlockSize;
}

FORCEINLINE
BOOLEAN
PoolLfhBlockIndex(
    _In_ PPOOL_LFH_SUBSEGMENT Subsegment,
    _In_ PVOID Address,
    _In_ BOOLEAN Exact,
    _Out_ PULONG Bit)
{
    PUCHAR Pointer = Address;
    SIZE_T Offset;
    SIZE_T Index;

    if (Pointer < Subsegment->BlockBase)
        return FALSE;

    if (Pointer < Subsegment->PagedBase)
    {
        Offset = (SIZE_T)(Pointer - Subsegment->BlockBase);
        Index = Offset / Subsegment->BlockSize;

        if (Index >= Subsegment->FirstCount || (Exact && Offset % Subsegment->BlockSize != 0))
            return FALSE;

        *Bit = (ULONG)Index;
        return TRUE;
    }

    if (Subsegment->PerPage == 0)
        return FALSE;

    Offset = (SIZE_T)(Pointer - Subsegment->PagedBase);
    Index = (Offset >> PAGE_SHIFT) * Subsegment->PerPage;
    Offset &= PAGE_SIZE - 1;

    if (Offset < Subsegment->Lead)
        return FALSE;

    Offset -= Subsegment->Lead;
    if (Offset / Subsegment->BlockSize >= Subsegment->PerPage || (Exact && Offset % Subsegment->BlockSize != 0))
        return FALSE;

    Index += Subsegment->FirstCount + Offset / Subsegment->BlockSize;
    if (Index >= Subsegment->BlockCount)
        return FALSE;

    *Bit = (ULONG)Index;
    return TRUE;
}

typedef struct _POOL_LFH_SLOT
{
    POOL_LOCK Lock;
    LIST_ENTRY Partial;
    LIST_ENTRY Full;
    ULONG64 Allocations;
    ULONG64 Frees;
    ULONG SubsegmentCount;
} POOL_CACHE_ALIGNED POOL_LFH_SLOT, *PPOOL_LFH_SLOT;

typedef struct _POOL_LFH_CLASS
{
    USHORT BlockSize;
    volatile LONG Activity;
} POOL_LFH_CLASS, *PPOOL_LFH_CLASS;

VOID PoolLfhInitialize(VOID);
ULONG PoolLfhClassFromSize(_In_ SIZE_T Bytes);
USHORT PoolLfhClassSize(_In_ ULONG Class);
PVOID PoolLfhAllocate(_Inout_ struct _POOL_HEAP *Heap, _In_ ULONG Class, _In_ ULONG Tag);
BOOLEAN PoolLfhFree(_Inout_ struct _POOL_HEAP *Heap, _In_ PPOOL_LFH_SUBSEGMENT Subsegment,
                    _In_ PVOID Block, _Out_ PPOOL_BLOCK_INFO Info);
BOOLEAN PoolLfhQuery(_In_ PPOOL_LFH_SUBSEGMENT Subsegment, _In_ PVOID Block, _Out_ PPOOL_BLOCK_INFO Info);
BOOLEAN PoolLfhSetFlags(_In_ PPOOL_LFH_SUBSEGMENT Subsegment, _In_ PVOID Block, _In_ UCHAR Flags);
VOID PoolLfhTrim(_Inout_ struct _POOL_HEAP *Heap);
