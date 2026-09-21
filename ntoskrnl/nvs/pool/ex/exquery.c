/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/ex/exquery.c
 * PURPOSE:     Executive pool query interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "expool.h"

#define NDEBUG
#include <debug.h>

#ifdef KDBG
#include <kdbg/kdb.h>
#endif

SIZE_T
NTAPI
ExQueryPoolBlockSize(
    IN PVOID PoolBlock,
    OUT PBOOLEAN QuotaCharged)
{
    POOL_BLOCK_INFO Info;
    PPOOL_HEAP Heap;
    ULONG HeapIndex;

    *QuotaCharged = FALSE;

    Heap = ExpPoolHeapFromAddress(PoolBlock, &HeapIndex);
    if (Heap == NULL || !PoolHeapQuery(Heap, PoolBlock, &Info))
        return PAGE_SIZE;

    if (Info.Flags & EX_POOL_BLOCK_QUOTA)
    {
        *QuotaCharged = TRUE;
        return Info.Size - sizeof(PEPROCESS);
    }

    return Info.Size;
}

VOID
NTAPI
ExpCheckPoolAllocation(
    PVOID P,
    POOL_TYPE PoolType,
    ULONG Tag)
{
    POOL_BLOCK_INFO Info;
    PPOOL_HEAP Heap;
    ULONG HeapIndex;

    UNREFERENCED_PARAMETER(PoolType);

    Heap = ExpPoolHeapFromAddress(P, &HeapIndex);
    if (Heap == NULL || !PoolHeapQuery(Heap, P, &Info))
        return;

    if ((Info.Tag & ~PROTECTED_POOL) != (Tag & ~PROTECTED_POOL))
        KeBugCheckEx(BAD_POOL_CALLER, 0x0A, (ULONG_PTR)P, Info.Tag, Tag);
}

VOID
NTAPI
ExQueryPoolUsage(
    OUT PULONG PagedPoolPages,
    OUT PULONG NonPagedPoolPages,
    OUT PULONG PagedPoolAllocs,
    OUT PULONG PagedPoolFrees,
    OUT PULONG PagedPoolLookasideHits,
    OUT PULONG NonPagedPoolAllocs,
    OUT PULONG NonPagedPoolFrees,
    OUT PULONG NonPagedPoolLookasideHits)
{
    POOL_HEAP_STATISTICS Statistics;

    RtlZeroMemory(&Statistics, sizeof(Statistics));
    if (ExpPoolState.Heap[EX_POOL_HEAP_NONPAGED] != NULL)
        PoolHeapQueryStatistics(ExpPoolState.Heap[EX_POOL_HEAP_NONPAGED], &Statistics);

    *NonPagedPoolPages = (ULONG)Statistics.CommittedPages;
    *NonPagedPoolAllocs = (ULONG)Statistics.Allocations;
    *NonPagedPoolFrees = (ULONG)Statistics.Frees;
    *NonPagedPoolLookasideHits = 0;

    if (ExpPoolState.Heap[EX_POOL_HEAP_EXECUTABLE] != NULL)
    {
        PoolHeapQueryStatistics(ExpPoolState.Heap[EX_POOL_HEAP_EXECUTABLE], &Statistics);
        *NonPagedPoolPages += (ULONG)Statistics.CommittedPages;
        *NonPagedPoolAllocs += (ULONG)Statistics.Allocations;
        *NonPagedPoolFrees += (ULONG)Statistics.Frees;
    }

    RtlZeroMemory(&Statistics, sizeof(Statistics));
    if (ExpPoolState.Heap[EX_POOL_HEAP_PAGED] != NULL)
        PoolHeapQueryStatistics(ExpPoolState.Heap[EX_POOL_HEAP_PAGED], &Statistics);

    *PagedPoolPages = (ULONG)Statistics.CommittedPages;
    *PagedPoolAllocs = (ULONG)Statistics.Allocations;
    *PagedPoolFrees = (ULONG)Statistics.Frees;
    *PagedPoolLookasideHits = 0;
}

NTSTATUS
NTAPI
ExGetPoolTagInfo(
    IN PSYSTEM_POOLTAG_INFORMATION SystemInformation,
    IN ULONG SystemInformationLength,
    IN OUT PULONG ReturnLength OPTIONAL)
{
    ULONG Needed = FIELD_OFFSET(SYSTEM_POOLTAG_INFORMATION, TagInfo);
    NTSTATUS Status = STATUS_SUCCESS;
    POOL_TAG_USAGE Usage;
    ULONG64 Cursor = 0;
    ULONG Count = 0;

    while (PoolTagEnumerate(&ExpPoolState.Tracker, &Cursor, &Usage))
    {
        PSYSTEM_POOLTAG TagEntry;

        Needed += sizeof(SYSTEM_POOLTAG);
        if (Needed > SystemInformationLength)
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
            continue;
        }

        TagEntry = &SystemInformation->TagInfo[Count++];
        TagEntry->TagUlong = Usage.Tag;
        TagEntry->PagedAllocs = (ULONG)Usage.Allocations[EX_POOL_HEAP_PAGED];
        TagEntry->PagedFrees = (ULONG)Usage.Frees[EX_POOL_HEAP_PAGED];
        TagEntry->PagedUsed = (SIZE_T)Usage.Bytes[EX_POOL_HEAP_PAGED];
        TagEntry->NonPagedAllocs = (ULONG)Usage.Allocations[EX_POOL_HEAP_NONPAGED];
        TagEntry->NonPagedFrees = (ULONG)Usage.Frees[EX_POOL_HEAP_NONPAGED];
        TagEntry->NonPagedUsed = (SIZE_T)Usage.Bytes[EX_POOL_HEAP_NONPAGED];
    }

    if (SystemInformationLength >= FIELD_OFFSET(SYSTEM_POOLTAG_INFORMATION, TagInfo))
        SystemInformation->Count = Count;

    if (ReturnLength != NULL)
        *ReturnLength = Needed;

    return Status;
}

VOID
MiDumpPoolConsumers(
    BOOLEAN CalledFromDbg,
    ULONG Tag,
    ULONG Mask,
    ULONG Flags)
{
    POOL_TAG_USAGE Usage;
    ULONG64 Cursor = 0;

    UNREFERENCED_PARAMETER(Flags);

    while (PoolTagEnumerate(&ExpPoolState.Tracker, &Cursor, &Usage))
    {
        if (Mask != 0 && (Usage.Tag & Mask) != (Tag & Mask))
            continue;

        if (Usage.Bytes[EX_POOL_HEAP_NONPAGED] == 0 && Usage.Bytes[EX_POOL_HEAP_PAGED] == 0)
            continue;

#ifdef KDBG
        if (CalledFromDbg)
        {
            KdbpPrint("%.4s  np: %I64u/%I64u %I64d  pg: %I64u/%I64u %I64d\n", (PCHAR)&Usage.Tag,
                      Usage.Allocations[EX_POOL_HEAP_NONPAGED], Usage.Frees[EX_POOL_HEAP_NONPAGED],
                      Usage.Bytes[EX_POOL_HEAP_NONPAGED],
                      Usage.Allocations[EX_POOL_HEAP_PAGED], Usage.Frees[EX_POOL_HEAP_PAGED],
                      Usage.Bytes[EX_POOL_HEAP_PAGED]);
            continue;
        }
#else
        UNREFERENCED_PARAMETER(CalledFromDbg);
#endif

        DbgPrint("%.4s  np: %I64u/%I64u %I64d  pg: %I64u/%I64u %I64d\n", (PCHAR)&Usage.Tag,
                 Usage.Allocations[EX_POOL_HEAP_NONPAGED], Usage.Frees[EX_POOL_HEAP_NONPAGED],
                 Usage.Bytes[EX_POOL_HEAP_NONPAGED],
                 Usage.Allocations[EX_POOL_HEAP_PAGED], Usage.Frees[EX_POOL_HEAP_PAGED],
                 Usage.Bytes[EX_POOL_HEAP_PAGED]);
    }
}
