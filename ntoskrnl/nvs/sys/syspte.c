/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/sys/syspte.c
 * PURPOSE:     System page-table entry allocation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/misys.h>

static
ULONG
MiSysPteClass(
    _In_ ULONG PageCount)
{
    ULONG Class = 0;

    while ((1u << Class) < PageCount)
        Class++;

    return Class;
}

/* Track allocated extents instead of allocating one bit for every possible
 * kernel VA page. Small runs retain the existing per-CPU caches. */
static
ULONG64
MiSysPteRangeReserve(
    _Inout_ PMI_SYSTEM_PTES Ptes,
    _In_ ULONG64 Count,
    _In_ ULONG64 Alignment)
{
    PMI_VAD_NODE Node = MI_ALLOCATE(sizeof(*Node));
    ULONG64 Index = ~0ULL;
    KIRQL OldIrql;

    if (Node == NULL)
        return Index;
    RtlZeroMemory(Node, sizeof(*Node));

    MI_SPIN_ACQUIRE(&Ptes->Lock, &OldIrql);
    if (MiVadFindEmptyRange(&Ptes->Allocations, Count, Alignment, &Index))
    {
        Node->StartingVpn = Index;
        Node->EndingVpn = Index + Count - 1;
        if (!MiVadInsert(&Ptes->Allocations, Node))
            Index = ~0ULL;
    }
    else
    {
        Index = ~0ULL;
    }
    MI_SPIN_RELEASE(&Ptes->Lock, OldIrql);

    if (Index == ~0ULL)
        MI_FREE(Node);
    return Index;
}

static
VOID
MiSysPteRangeRelease(
    _Inout_ PMI_SYSTEM_PTES Ptes,
    _In_ ULONG64 Start,
    _In_ ULONG64 Count)
{
    PMI_VAD_NODE Node;
    KIRQL OldIrql;

    MI_SPIN_ACQUIRE(&Ptes->Lock, &OldIrql);
    Node = MiVadFind(&Ptes->Allocations, Start);
    MI_ASSERT(Node != NULL && Node->StartingVpn == Start && Node->EndingVpn == Start + Count - 1);
    MiVadRemove(&Ptes->Allocations, Node);
    MI_SPIN_RELEASE(&Ptes->Lock, OldIrql);
    MI_FREE(Node);
}

NTSTATUS
MiSystemPtesInitialize(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 PageCount,
    _In_ ULONG CpuCount)
{
    PMI_SYSTEM_PTES Ptes;
    NTSTATUS Status;
    ULONG i;

    if (PageCount == 0 || PageCount > (~0ULL >> PAGE_SHIFT))
        return STATUS_INVALID_PARAMETER;

    Ptes = MI_ALLOCATE(sizeof(*Ptes));
    if (Ptes == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Ptes, sizeof(*Ptes));
    Ptes->System = System;
    Ptes->PageCount = PageCount;
    Ptes->FreePages = (LONG64)PageCount;
    Ptes->CacheCount = (CpuCount == 0) ? 1 : ((CpuCount > MI_SYSPTE_CPU_CACHES) ? MI_SYSPTE_CPU_CACHES : CpuCount);
    Ptes->DefaultStackPages = 6;
    MI_SPIN_INIT(&Ptes->Lock);
    MI_SPIN_INIT(&Ptes->PopulateLock);
    MiVadRootInitialize(&Ptes->Allocations, 0, PageCount - 1);

    for (i = 0; i < MI_SYSPTE_CPU_CACHES; i++)
        MI_SPIN_INIT(&Ptes->Cache[i].Lock);

    Status = MiSystemRegionCreate(System, PageCount, FALSE, &Ptes->Vad, &Ptes->Base);
    if (!NT_SUCCESS(Status))
    {
        MI_FREE(Ptes);
        return Status;
    }

    /* Process roots copy the kernel's top-level entries on AMD64. Seed one
     * leaf path per top-level span before processes can be created, so later
     * table population remains visible through those shared entries. This
     * needs only a few table pages, not tables for every page of the arena. */
    {
        ULONG64 Span = 1ULL << System->Arch->Level[System->Arch->PagingLevels - 1].Shift;
        ULONG64 End = Ptes->Base + (PageCount << PAGE_SHIFT);
        ULONG64 Va = Ptes->Base;

        while (Va < End)
        {
            Status = MiPtPinSystemRange(&System->SystemSpace, Va, PAGE_SIZE);
            if (!NT_SUCCESS(Status))
            {
                MiSystemRegionDelete(System, Ptes->Vad, TRUE);
                MI_FREE(Ptes);
                return Status;
            }
            Va = (Va & ~(Span - 1)) + Span;
        }
    }

    System->SystemPtes = Ptes;
    return STATUS_SUCCESS;
}

VOID
MiSystemPtesUninitialize(
    _Inout_ PMI_SYSTEM System)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG Cpu, Class;

    if (Ptes == NULL)
        return;

    for (Cpu = 0; Cpu < MI_SYSPTE_CPU_CACHES; Cpu++)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[Cpu];

        while (Cache->StackDepth != 0)
        {
            ULONG64 Top = Cache->Stack[--Cache->StackDepth];
            ULONG64 Base = Top - (ULONG64)Ptes->DefaultStackPages * PAGE_SIZE;

            MiSystemUnmap(System, Base, Ptes->DefaultStackPages, TRUE);
            MiReturnCommit(&System->SystemSpace, Ptes->DefaultStackPages);
            MiReleaseSystemPtes(System, Base - PAGE_SIZE, Ptes->DefaultStackPages + 1);
        }
    }

    for (Cpu = 0; Cpu < MI_SYSPTE_CPU_CACHES; Cpu++)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[Cpu];

        for (Class = 0; Class < MI_SYSPTE_CLASSES; Class++)
        {
            while (Cache->Depth[Class] != 0)
            {
                ULONG64 Va = Cache->Entry[Class][--Cache->Depth[Class]];

                MiSysPteRangeRelease(Ptes, (Va - Ptes->Base) >> PAGE_SHIFT, 1ULL << Class);
                MI_ATOMIC_ADD64(&Ptes->FreePages, (LONG64)(1ULL << Class));
            }
        }
    }

    MI_ASSERT(MI_ATOMIC_READ64(&Ptes->FreePages) == (LONG64)Ptes->PageCount);
    MI_ASSERT(Ptes->Allocations.NodeCount == 0);

    System->SystemPtes = NULL;
    MiSystemRegionDelete(System, Ptes->Vad, TRUE);
    MI_FREE(Ptes);
}

ULONG64
MiSystemPteCacheHits(
    _In_ PMI_SYSTEM System)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG64 Total = 0;
    ULONG i;

    for (i = 0; i < Ptes->CacheCount; i++)
    {
        KIRQL OldIrql;

        MI_SPIN_ACQUIRE(&Ptes->Cache[i].Lock, &OldIrql);
        Total += Ptes->Cache[i].CacheHits;
        MI_SPIN_RELEASE(&Ptes->Cache[i].Lock, OldIrql);
    }

    return Total;
}

ULONG64
MiReserveSystemPtes(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG PageCount)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG64 Pages = PageCount;
    ULONG64 Index;

    if (Ptes == NULL || PageCount == 0)
        return 0;

    if (PageCount <= MI_SYSPTE_CLASS_MAX_PAGES)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[MI_CURRENT_CPU() % Ptes->CacheCount];
        ULONG Class = MiSysPteClass(PageCount);
        ULONG64 Va = 0;
        KIRQL OldIrql;

        Pages = 1ULL << Class;

        MI_SPIN_ACQUIRE(&Cache->Lock, &OldIrql);
        Cache->Reservations++;
        if (Cache->Depth[Class] != 0)
        {
            Va = Cache->Entry[Class][--Cache->Depth[Class]];
            Cache->CacheHits++;
        }
        MI_SPIN_RELEASE(&Cache->Lock, OldIrql);

        if (Va != 0)
            return Va;
    }

    Index = MiSysPteRangeReserve(Ptes, Pages, (Pages <= MI_SYSPTE_CLASS_MAX_PAGES) ? Pages : 1);
    if (Index == ~0ULL)
        return 0;

    {
        KIRQL OldIrql;
        NTSTATUS Status;

        /* Prepare only this reservation. Keep touched tables resident across
         * releases so another CPU's mapping in the same table stays valid and
         * cache hits never need a pageable lock or a table allocation. */
        MI_SPIN_ACQUIRE(&Ptes->PopulateLock, &OldIrql);
        Status = MiPtPinSystemRange(&System->SystemSpace,
                                    Ptes->Base + (Index << PAGE_SHIFT),
                                    Pages << PAGE_SHIFT);
        MI_SPIN_RELEASE(&Ptes->PopulateLock, OldIrql);
        if (!NT_SUCCESS(Status))
        {
            MiSysPteRangeRelease(Ptes, Index, Pages);
            return 0;
        }
    }
    MI_ATOMIC_ADD64(&Ptes->FreePages, -(LONG64)Pages);
    return Ptes->Base + (Index << PAGE_SHIFT);
}

VOID
MiReleaseSystemPtes(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG PageCount)
{
    PMI_SYSTEM_PTES Ptes = System->SystemPtes;
    ULONG64 Pages = PageCount;

    MI_ASSERT((VirtualAddress & (PAGE_SIZE - 1)) == 0);
    MI_ASSERT(VirtualAddress >= Ptes->Base && VirtualAddress + ((ULONG64)PageCount << PAGE_SHIFT) <=
                                                  Ptes->Base + (Ptes->PageCount << PAGE_SHIFT));

    if (PageCount <= MI_SYSPTE_CLASS_MAX_PAGES)
    {
        PMI_SYSPTE_CPU_CACHE Cache = &Ptes->Cache[MI_CURRENT_CPU() % Ptes->CacheCount];
        ULONG Class = MiSysPteClass(PageCount);
        BOOLEAN Cached = FALSE;
        KIRQL OldIrql;

        Pages = 1ULL << Class;

        MI_SPIN_ACQUIRE(&Cache->Lock, &OldIrql);
        if (Cache->Depth[Class] < MI_SYSPTE_CACHE_DEPTH)
        {
            Cache->Entry[Class][Cache->Depth[Class]++] = VirtualAddress;
            Cached = TRUE;
        }
        MI_SPIN_RELEASE(&Cache->Lock, OldIrql);

        if (Cached)
            return;
    }

    MiSysPteRangeRelease(Ptes, (VirtualAddress - Ptes->Base) >> PAGE_SHIFT, Pages);
    MI_ATOMIC_ADD64(&Ptes->FreePages, (LONG64)Pages);
}
