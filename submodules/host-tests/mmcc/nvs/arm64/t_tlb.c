/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/arm64/t_tlb.c
 * PURPOSE:     ARM64 translation lookaside buffer regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/mienv.h>
#include <nvs/arch/arm64/tlbops.h>
#include <pthread.h>

typedef struct _TEST_TRACE
{
    ULONG Cpu, Phase, PageCount, FullCount, Batches;
    ULONG64 Page[65];
    MI_TLB_SCOPE Scope;
} TEST_TRACE;

static _Thread_local TEST_TRACE Trace;
static ULONG CpuCount = 1;
static ULONG Checks, Failures;
static ULONG64 TargetPages[8], TargetFull[8];

#define CHECK(e) do { __atomic_fetch_add(&Checks, 1, __ATOMIC_RELAXED); if (!(e)) { if (__atomic_fetch_add(&Failures, 1, __ATOMIC_RELAXED) < 20) fprintf(stderr, "cpu%u %u: %s\n", Trace.Cpu, __LINE__, #e); } } while (0)

VOID MiArm64TlbStoreBarrier(VOID)
{
    CHECK(Trace.Phase == 0);
    Trace.Phase = 1;
    Trace.PageCount = Trace.FullCount = 0;
}

VOID MiArm64TlbCompleteBarrier(VOID)
{
    CHECK(Trace.Phase == 1);
    CHECK((Trace.PageCount != 0) != (Trace.FullCount != 0));
    Trace.Phase = 2;
}

VOID MiArm64TlbInstructionBarrier(VOID)
{
    CHECK(Trace.Phase == 2);
    Trace.Phase = 0;
    Trace.Batches++;
}

static VOID CountTargets(ULONG64 *Counters, MI_TLB_SCOPE Scope)
{
    ULONG Cpu;
    CHECK(Scope == Trace.Scope);
    for (Cpu = 0; Cpu < CpuCount; Cpu++)
        if (Cpu == Trace.Cpu || Scope == MiTlbAllProcessors)
            __atomic_fetch_add(&Counters[Cpu], 1, __ATOMIC_RELAXED);
}

VOID MiArm64TlbPage(ULONG64 Page, MI_TLB_SCOPE Scope)
{
    CHECK(Trace.Phase == 1 && Trace.FullCount == 0);
    CHECK(Trace.PageCount < RTL_NUMBER_OF(Trace.Page));
    if (Trace.PageCount < RTL_NUMBER_OF(Trace.Page))
        Trace.Page[Trace.PageCount] = Page;
    Trace.PageCount++;
    CountTargets(TargetPages, Scope);
}

VOID MiArm64TlbAll(MI_TLB_SCOPE Scope)
{
    CHECK(Trace.Phase == 1 && Trace.PageCount == 0 && Trace.FullCount == 0);
    Trace.FullCount++;
    CountTargets(TargetFull, Scope);
}

static VOID ExpectPages(ULONG64 Address, ULONG Count, ULONG Batches)
{
    ULONG Index;
    CHECK(Trace.Phase == 0 && Trace.FullCount == 0);
    CHECK(Trace.Batches == Batches + 1 && Trace.PageCount == Count);
    for (Index = 0; Index < Count && Index < RTL_NUMBER_OF(Trace.Page); Index++)
        CHECK(Trace.Page[Index] == (Address >> PAGE_SHIFT) + Index);
}

static VOID ExpectFull(ULONG Batches)
{
    CHECK(Trace.Phase == 0 && Trace.PageCount == 0);
    CHECK(Trace.Batches == Batches + 1 && Trace.FullCount == 1);
}

static VOID RangeCases(VOID)
{
    ULONG Scope, Index, Batches;
    ULONG64 Addresses[] = { 0, 1, 4095, 4096, 0xffff800000001001ULL,
                            0xfffffffffffff000ULL, 0xfffffffffffffffeULL };
    SIZE_T Lengths[] = { 1, 2, 4095, 4096, 4097, 63 * PAGE_SIZE, 64 * PAGE_SIZE - 1,
                        64 * PAGE_SIZE, 65 * PAGE_SIZE, (SIZE_T)-1 };

    for (Scope = MiTlbLocal; Scope <= MiTlbAllProcessors; Scope++)
    {
        Trace.Scope = (MI_TLB_SCOPE)Scope;
        Batches = Trace.Batches;
        MiArchInvalidateTlbRange(NULL, 0, Trace.Scope);
        MiArchTlbInvalidate(~0ULL, 0, Scope == MiTlbAllProcessors);
        CHECK(Trace.Batches == Batches);
        MiArchInvalidateTlbSingle((PVOID)(ULONG_PTR)0xffff800000001123ULL, Trace.Scope);
        ExpectPages(0xffff800000001123ULL, 1, Batches);

        for (Index = 0; Index < RTL_NUMBER_OF(Addresses); Index++)
        {
            ULONG SizeIndex;
            ULONG64 Address = Addresses[Index];
            for (SizeIndex = 0; SizeIndex < RTL_NUMBER_OF(Lengths); SizeIndex++)
            {
                SIZE_T Length = Lengths[SizeIndex];
                Batches = Trace.Batches;
                MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)Address, Length, Trace.Scope);
                if (Length >= 64 * PAGE_SIZE || Length > ~Address)
                    ExpectFull(Batches);
                else
                    ExpectPages(Address, (ULONG)((Length + (Address & (PAGE_SIZE - 1)) + PAGE_SIZE - 1) >> PAGE_SHIFT), Batches);
            }
        }

        Batches = Trace.Batches;
        MiArchTlbInvalidate(0x1000, 3, Scope == MiTlbAllProcessors);
        ExpectPages(0x1000, 3, Batches);
        Batches = Trace.Batches;
        MiArchTlbInvalidate(0x1000, ((SIZE_T)-1 >> PAGE_SHIFT) + 1, Scope == MiTlbAllProcessors);
        ExpectFull(Batches);
        Batches = Trace.Batches;
        MiArchTlbInvalidateAll(Scope == MiTlbAllProcessors);
        ExpectFull(Batches);
    }
}

static VOID *Worker(PVOID Argument)
{
    ULONG Round;
    RtlZeroMemory(&Trace, sizeof(Trace));
    Trace.Cpu = (ULONG)(ULONG_PTR)Argument;
    for (Round = 0; Round < 500; Round++)
    {
        ULONG Batches = Trace.Batches;
        ULONG Pages = Round % 7 + 1;
        ULONG64 Address = 0xffff800000000000ULL + (ULONG64)Round * PAGE_SIZE;
        Trace.Scope = (Round & 1) ? MiTlbLocal : MiTlbAllProcessors;
        MiArchTlbInvalidate(Address, Pages, Trace.Scope == MiTlbAllProcessors);
        ExpectPages(Address, Pages, Batches);
        if (Round % 11 == 0)
        {
            Batches = Trace.Batches;
            MiArchTlbInvalidateAll(Trace.Scope == MiTlbAllProcessors);
            ExpectFull(Batches);
        }
    }
    return NULL;
}

static VOID SmpCases(ULONG Count)
{
    pthread_t Threads[8];
    ULONG Cpu, Round;
    ULONG64 Pages = 0, Full = 0;
    CpuCount = Count;
    RtlZeroMemory(TargetPages, sizeof(TargetPages));
    RtlZeroMemory(TargetFull, sizeof(TargetFull));
    for (Cpu = 0; Cpu < Count; Cpu++)
        if (pthread_create(&Threads[Cpu], NULL, Worker, (PVOID)(ULONG_PTR)Cpu) != 0)
            abort();
    for (Cpu = 0; Cpu < Count; Cpu++)
        CHECK(pthread_join(Threads[Cpu], NULL) == 0);
    for (Round = 0; Round < 500; Round++)
    {
        ULONG Sources = (Round & 1) ? 1 : Count;
        Pages += Sources * (Round % 7 + 1);
        if (Round % 11 == 0)
            Full += Sources;
    }
    for (Cpu = 0; Cpu < Count; Cpu++)
    {
        CHECK(TargetPages[Cpu] == Pages);
        CHECK(TargetFull[Cpu] == Full);
    }
}

int main(VOID)
{
    RangeCases();
    SmpCases(1);
    SmpCases(2);
    SmpCases(4);
    SmpCases(8);
    printf("ARM64 TLB: %u checks, %u failures\n", Checks, Failures);
    return Failures != 0;
}
