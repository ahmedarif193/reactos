/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/include/poolvs.h
 * PURPOSE:     Variable-size pool heap definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

typedef struct _POOL_VS_CHUNK
{
    ULONG64 Encoded;
    ULONG Tag;
    UCHAR Flags;
    UCHAR Spare[3];
} POOL_VS_CHUNK, *PPOOL_VS_CHUNK;

C_ASSERT(sizeof(POOL_VS_CHUNK) == POOL_VS_UNIT);

typedef struct _POOL_VS_SUBSEGMENT
{
    LIST_ENTRY Link;
    ULONG Signature;
    ULONG TotalUnits;
    USHORT Pages;
    USHORT Shard;
} POOL_VS_SUBSEGMENT, *PPOOL_VS_SUBSEGMENT;

#define POOL_VS_SUBSEGMENT_BYTES     32

C_ASSERT(sizeof(POOL_VS_SUBSEGMENT) <= POOL_VS_SUBSEGMENT_BYTES);
C_ASSERT((POOL_VS_SUBSEGMENT_BYTES % POOL_VS_UNIT) == 0);

typedef struct _POOL_VS_CONTEXT
{
    POOL_LOCK Lock;
    LIST_ENTRY FreeList[POOL_VS_FREE_LISTS];
    LIST_ENTRY Subsegments;
    ULONG SubsegmentCount;
    SIZE_T UnitsFree;
    ULONG64 Allocations;
    ULONG64 Frees;
} POOL_CACHE_ALIGNED POOL_VS_CONTEXT, *PPOOL_VS_CONTEXT;

SIZE_T PoolVsMaxBytes(VOID);
PVOID PoolVsAllocate(_Inout_ struct _POOL_HEAP *Heap, _In_ SIZE_T Bytes, _In_ ULONG Tag,
                     _Out_ PSIZE_T ActualBytes);
BOOLEAN PoolVsFree(_Inout_ struct _POOL_HEAP *Heap, _In_ PPOOL_VS_SUBSEGMENT Subsegment,
                   _In_ PVOID Block, _Out_ PPOOL_BLOCK_INFO Info);
BOOLEAN PoolVsQuery(_Inout_ struct _POOL_HEAP *Heap, _In_ PPOOL_VS_SUBSEGMENT Subsegment,
                    _In_ PVOID Block, _Out_ PPOOL_BLOCK_INFO Info);
BOOLEAN PoolVsSetFlags(_Inout_ struct _POOL_HEAP *Heap, _In_ PPOOL_VS_SUBSEGMENT Subsegment,
                       _In_ PVOID Block, _In_ UCHAR Flags);
VOID PoolVsTrim(_Inout_ struct _POOL_HEAP *Heap);
BOOLEAN PoolVsWalk(_In_ struct _POOL_HEAP *Heap, _In_ PPOOL_VS_SUBSEGMENT Subsegment, _Inout_ PVOID *Cursor,
                   _Out_ PVOID *Block, _Out_ PPOOL_BLOCK_INFO Info, _Out_ PBOOLEAN Allocated);
