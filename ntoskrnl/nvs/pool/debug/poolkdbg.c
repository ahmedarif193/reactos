/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/debug/poolkdbg.c
 * PURPOSE:     Pool allocator debugger interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../ex/expool.h"

#define NDEBUG
#include <debug.h>

#if DBG && defined(KDBG)

#include <kdbg/kdb.h>

typedef struct _POOL_KDBG_FIND
{
    ULONG Tag;
    ULONG Mask;
    ULONG Found;
} POOL_KDBG_FIND, *PPOOL_KDBG_FIND;

static const PCSTR PoolKdbgKindName[] = { "none", "lfh", "vs", "range", "large" };

static
VOID
PoolKdbgParseTag(
    _In_ PCHAR Text,
    _Out_ PULONG Tag,
    _Out_ PULONG Mask)
{
    CHAR Characters[4] = { ' ', ' ', ' ', ' ' };
    SIZE_T Length = strlen(Text);
    ULONG i;

    *Mask = 0;
    if (Length > 4)
        Length = 4;

    for (i = 0; i < Length; i++)
    {
        Characters[i] = Text[i];
        if (Text[i] != '?')
            *Mask |= 0xFFUL << (i * 8);
    }

    RtlCopyMemory(Tag, Characters, sizeof(ULONG));
}

static
VOID
PoolKdbgSummarize(
    _In_ PCSTR Name,
    _In_opt_ PPOOL_HEAP Heap)
{
    POOL_HEAP_STATISTICS Statistics;

    if (Heap == NULL)
    {
        KdbpPrint("%s heap: not initialized\n", Name);
        return;
    }

    PoolHeapQueryStatistics(Heap, &Statistics);
    KdbpPrint("%s heap %p: %I64u allocs, %I64u frees, %I64d bytes in use, %I64u failures\n"
              "  committed %I64d pages (peak %I64d), %Iu units reserved\n"
              "  %lu segments, %lu lfh subsegments, %lu vs subsegments, %lu large blocks\n",
              Name, Heap, Statistics.Allocations, Statistics.Frees, Statistics.BytesInUse,
              Statistics.Failures, Statistics.CommittedPages, Statistics.PeakCommittedPages,
              Statistics.ReservedUnits, Statistics.SegmentCount, Statistics.LfhSubsegmentCount,
              Statistics.VsSubsegmentCount, Statistics.LargeCount);
}

BOOLEAN
ExpKdbgExtPool(
    ULONG Argc,
    PCHAR Argv[])
{
    POOL_BLOCK_INFO Info;
    ULONG_PTR Address;
    BOOLEAN Allocated;
    PPOOL_HEAP Heap;
    ULONG HeapIndex;
    PVOID Block;

    if (Argc < 2)
    {
        PoolKdbgSummarize("nonpaged", ExpPoolState.Heap[EX_POOL_HEAP_NONPAGED]);
        PoolKdbgSummarize("executable nonpaged", ExpPoolState.Heap[EX_POOL_HEAP_EXECUTABLE]);
        PoolKdbgSummarize("paged", ExpPoolState.Heap[EX_POOL_HEAP_PAGED]);
        return TRUE;
    }

    if (!KdbpGetHexNumber(Argv[1], &Address))
    {
        KdbpPrint("Invalid parameter: %s\n", Argv[1]);
        return TRUE;
    }

    Heap = ExpPoolHeapFromAddress((PVOID)Address, &HeapIndex);
    if (Heap == NULL)
    {
        KdbpPrint("%p is not inside a pool region\n", (PVOID)Address);
        return TRUE;
    }

    if (!PoolHeapDescribe(Heap, (PVOID)Address, &Block, &Info, &Allocated))
    {
        KdbpPrint("%p is in the %s heap but not inside a block\n", (PVOID)Address,
                  HeapIndex == EX_POOL_HEAP_PAGED ? "paged" : "nonpaged");
        return TRUE;
    }

    KdbpPrint("%p: %s heap, %s block %p, %Iu bytes, tag %.4s, flags %02x, %s\n",
              (PVOID)Address, HeapIndex == EX_POOL_HEAP_PAGED ? "paged" : "nonpaged",
              PoolKdbgKindName[Info.Kind], Block, Info.Size, (PCHAR)&Info.Tag, Info.Flags,
              Allocated ? "allocated" : "free");
    return TRUE;
}

BOOLEAN
ExpKdbgExtPoolUsed(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG Tag = 0;
    ULONG Mask = 0;

    if (Argc > 1)
        PoolKdbgParseTag(Argv[Argc - 1], &Tag, &Mask);

    MiDumpPoolConsumers(TRUE, Tag, Mask, 0);
    return TRUE;
}

static
BOOLEAN
PoolKdbgFindRoutine(
    _In_opt_ PVOID Context,
    _In_ PVOID Block,
    _In_ PPOOL_BLOCK_INFO Info)
{
    PPOOL_KDBG_FIND Find = Context;

    if ((Info->Tag & Find->Mask) != (Find->Tag & Find->Mask))
        return TRUE;

    Find->Found++;
    KdbpPrint("%p %-5s %8Iu %.4s\n", Block, PoolKdbgKindName[Info->Kind], Info->Size, (PCHAR)&Info->Tag);
    return TRUE;
}

BOOLEAN
ExpKdbgExtPoolFind(
    ULONG Argc,
    PCHAR Argv[])
{
    ULONG HeapIndex = EX_POOL_HEAP_NONPAGED;
    POOL_KDBG_FIND Find;

    if (Argc < 2)
    {
        KdbpPrint("Specify a tag\n");
        return TRUE;
    }

    RtlZeroMemory(&Find, sizeof(Find));
    if (strcmp(Argv[1], "*") != 0)
        PoolKdbgParseTag(Argv[1], &Find.Tag, &Find.Mask);

    if (Argc > 2 && strtoul(Argv[2], NULL, 0) == 1)
        HeapIndex = EX_POOL_HEAP_PAGED;

    if (ExpPoolState.Heap[HeapIndex] == NULL)
    {
        KdbpPrint("Heap not initialized\n");
        return TRUE;
    }

    PoolHeapEnumerate(ExpPoolState.Heap[HeapIndex], PoolKdbgFindRoutine, &Find);
    if (HeapIndex == EX_POOL_HEAP_NONPAGED && ExpPoolState.Heap[EX_POOL_HEAP_EXECUTABLE] != NULL)
        PoolHeapEnumerate(ExpPoolState.Heap[EX_POOL_HEAP_EXECUTABLE], PoolKdbgFindRoutine, &Find);
    KdbpPrint("%lu blocks\n", Find.Found);
    return TRUE;
}

#endif
