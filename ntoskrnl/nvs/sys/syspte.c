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

static
BOOLEAN
MiSysPteRunFree(
    _In_ PMI_SYSTEM_PTES Ptes,
    _In_ ULONG64 Start,
    _In_ ULONG64 Count,
    _Out_ PULONG64 Blocker)
{
    ULONG64 i;

    for (i = Count; i != 0; i--)
    {
        ULONG64 Bit = Start + i - 1;

        if ((Ptes->Bitmap[Bit >> 6] >> (Bit & 63)) & 1)
        {
            *Blocker = Bit;
            return FALSE;
        }
    }

    return TRUE;
}

static
ULONG64
MiSysPteBitmapReserve(
    _Inout_ PMI_SYSTEM_PTES Ptes,
    _In_ ULONG64 Count,
    _In_ ULONG64 Alignment)
{
    ULONG64 Result = ~0ULL;
    ULONG Pass;
    KIRQL OldIrql;

    MI_SPIN_ACQUIRE(&Ptes->Lock, &OldIrql);

    for (Pass = 0; Pass < 2 && Result == ~0ULL; Pass++)
    {
        ULONG64 Start = (Pass == 0) ? Ptes->Hint : 0;
        ULONG64 Limit = (Pass == 0) ? Ptes->PageCount : Ptes->Hint + Count;

        if (Limit > Ptes->PageCount)
            Limit = Ptes->PageCount;

        Start = (Start + Alignment - 1) & ~(Alignment - 1);

        while (Start + Count <= Limit)
        {
            ULONG64 Blocker;

            if ((Start & 63) == 0 && Count <= 64 && Ptes->Bitmap[Start >> 6] == ~0ULL)
            {
                Start += 64;
                continue;
            }

            if (MiSysPteRunFree(Ptes, Start, Count, &Blocker))
            {
                ULONG64 i;

                for (i = Start; i < Start + Count; i++)
                    Ptes->Bitmap[i >> 6] |= 1ULL << (i & 63);

                Ptes->Hint = Start + Count;
                Result = Start;
                break;
            }

            Start = (Blocker + 1 + Alignment - 1) & ~(Alignment - 1);
        }
    }

    MI_SPIN_RELEASE(&Ptes->Lock, OldIrql);
    return Result;
}

static
VOID
MiSysPteBitmapRelease(
    _Inout_ PMI_SYSTEM_PTES Ptes,
    _In_ ULONG64 Start,
    _In_ ULONG64 Count)
{
    KIRQL OldIrql;
    ULONG64 i;

    MI_SPIN_ACQUIRE(&Ptes->Lock, &OldIrql);

    for (i = Start; i < Start + Count; i++)
    {
        MI_ASSERT((Ptes->Bitmap[i >> 6] >> (i & 63)) & 1);
        Ptes->Bitmap[i >> 6] &= ~(1ULL << (i & 63));
    }

    if (Start < Ptes->Hint)
        Ptes->Hint = Start;

    MI_SPIN_RELEASE(&Ptes->Lock, OldIrql);
}

NTSTATUS
MiSystemPtesInitialize(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 PageCount,
    _In_ ULONG CpuCount)
{
    PMI_SYSTEM_PTES Ptes;
    NTSTATUS Status;
    ULONG64 Words = (PageCount + 63) >> 6;
    ULONG i;

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

    for (i = 0; i < MI_SYSPTE_CPU_CACHES; i++)
        MI_SPIN_INIT(&Ptes->Cache[i].Lock);

    Ptes->Bitmap = MI_ALLOCATE(Words * sizeof(ULONG64));
    if (Ptes->Bitmap == NULL)
    {
        MI_FREE(Ptes);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Ptes->Bitmap, Words * sizeof(ULONG64));
    if (PageCount & 63)
        Ptes->Bitmap[Words - 1] = ~0ULL << (PageCount & 63);

    Status = MiSystemRegionCreate(System, PageCount, TRUE, &Ptes->Vad, &Ptes->Base);
    if (!NT_SUCCESS(Status))
    {
        MI_FREE(Ptes->Bitmap);
        MI_FREE(Ptes);
        return Status;
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

                MiSysPteBitmapRelease(Ptes, (Va - Ptes->Base) >> PAGE_SHIFT, 1ULL << Class);
                MI_ATOMIC_ADD64(&Ptes->FreePages, (LONG64)(1ULL << Class));
            }
        }
    }

    MI_ASSERT(MI_ATOMIC_READ64(&Ptes->FreePages) == (LONG64)Ptes->PageCount);

    System->SystemPtes = NULL;
    MiSystemRegionDelete(System, Ptes->Vad, TRUE);
    MI_FREE(Ptes->Bitmap);
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

    Index = MiSysPteBitmapReserve(Ptes, Pages, (Pages <= MI_SYSPTE_CLASS_MAX_PAGES) ? Pages : 1);
    if (Index == ~0ULL)
        return 0;

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

    MiSysPteBitmapRelease(Ptes, (VirtualAddress - Ptes->Base) >> PAGE_SHIFT, Pages);
    MI_ATOMIC_ADD64(&Ptes->FreePages, (LONG64)Pages);
}
