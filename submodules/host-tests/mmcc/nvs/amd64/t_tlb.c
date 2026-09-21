/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/amd64/t_tlb.c
 * PURPOSE:     AMD64 translation lookaside buffer regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "tlbshim.h"
#include <nvs/arch/amd64/tlb.h>
#include <nvs/arch/amd64/hardware.h>
#include <pthread.h>

typedef struct _TEST_CPU
{
    ULONG64 Cr3, Cr4;
    ULONG64 PageFlushes, FullFlushes, Cr3Writes, Cr4Writes, Invpcids, CpuidCalls;
    ULONG64 Pages[8];
    ULONG64 Pending;
    BOOLEAN Enabled;
    KIRQL Irql;
} TEST_CPU;

static TEST_CPU Cpus[MAXIMUM_PROCESSORS];
static _Thread_local ULONG CurrentCpu;
static ULONG64 ActiveCpus;
static ULONG Checks, Failures, Finished;
static ULONG64 Ipis;

#define CHECK(e) do { __atomic_fetch_add(&Checks, 1, __ATOMIC_RELAXED); if (!(e)) { if (__atomic_fetch_add(&Failures, 1, __ATOMIC_RELAXED) < 20) fprintf(stderr, "cpu%u %u: %s\n", CurrentCpu, __LINE__, #e); } } while (0)

ULONG MiAmd64TlbCpu(VOID) { return CurrentCpu; }
ULONG64 MiAmd64TlbActiveCpus(VOID) { return ActiveCpus; }
ULONG64 MiAmd64TlbReadCr3(VOID) { return Cpus[CurrentCpu].Cr3; }
ULONG64 MiAmd64TlbReadCr4(VOID) { return Cpus[CurrentCpu].Cr4; }

static VOID FlushAll(VOID)
{
    TEST_CPU *Cpu = &Cpus[CurrentCpu];
    ULONG Index;
    Cpu->FullFlushes++;
    for (Index = 0; Index < RTL_NUMBER_OF(Cpu->Pages); Index++)
        Cpu->Pages[Index] = 0;
}

VOID MiAmd64TlbWriteCr3(ULONG64 Value)
{
    TEST_CPU *Cpu = &Cpus[CurrentCpu];
    CHECK(!Cpu->Enabled);
    CHECK(!(Value & (1ULL << 63)));
    CHECK((Cpu->Cr4 & ((1ULL << 7) | (1ULL << 17))) == 0);
    Cpu->Cr3 = Value;
    Cpu->Cr3Writes++;
    FlushAll();
}

VOID MiAmd64TlbWriteCr4(ULONG64 Value)
{
    TEST_CPU *Cpu = &Cpus[CurrentCpu];
    CHECK(!Cpu->Enabled);
    CHECK((Value ^ Cpu->Cr4) == (1ULL << 7));
    Cpu->Cr4 = Value;
    Cpu->Cr4Writes++;
    FlushAll();
}

VOID MiAmd64TlbPage(ULONG64 Address)
{
    TEST_CPU *Cpu = &Cpus[CurrentCpu];
    ULONG Index;
    CHECK(!Cpu->Enabled);
    CHECK((Address & (PAGE_SIZE - 1)) == 0);
    CHECK(MiAmd64CanonicalAddress(Address));
    for (Index = 0; Index < RTL_NUMBER_OF(Cpu->Pages); Index++)
        if (Cpu->Pages[Index] == Address)
            Cpu->Pages[Index] = 0;
    Cpu->PageFlushes++;
}

VOID MiAmd64TlbCpuid(int Registers[4], int Leaf)
{
    TEST_CPU *Cpu = &Cpus[CurrentCpu];
    CHECK(!Cpu->Enabled);
    Cpu->CpuidCalls++;
    memset(Registers, 0, 4 * sizeof(int));
    if (Leaf == 0)
        Registers[0] = CurrentCpu % 4 == 1 ? 1 : 7;
    else
    {
        CHECK(Leaf == 7 && CurrentCpu % 4 != 1);
        Registers[1] = CurrentCpu % 4 == 0 ? (1 << 10) : 0;
    }
}

VOID MiAmd64TlbInvpcid(VOID)
{
    CHECK(!Cpus[CurrentCpu].Enabled);
    CHECK(CurrentCpu % 4 == 0);
    Cpus[CurrentCpu].Invpcids++;
    FlushAll();
}

BOOLEAN MiAmd64TlbDisableInterrupts(VOID)
{
    BOOLEAN Previous = Cpus[CurrentCpu].Enabled;
    Cpus[CurrentCpu].Enabled = FALSE;
    return Previous;
}

VOID MiAmd64TlbRestoreInterrupts(BOOLEAN Enabled)
{
    CHECK(!Cpus[CurrentCpu].Enabled);
    Cpus[CurrentCpu].Enabled = Enabled;
}

KIRQL MiAmd64TlbRaiseIrql(VOID)
{
    KIRQL Previous = Cpus[CurrentCpu].Irql;
    if (Previous < 12)
        Cpus[CurrentCpu].Irql = 12;
    return Previous;
}

VOID MiAmd64TlbLowerIrql(KIRQL Previous)
{
    CHECK(Previous <= Cpus[CurrentCpu].Irql);
    Cpus[CurrentCpu].Irql = Previous;
}

VOID MiAmd64TlbPause(VOID) { sched_yield(); }

VOID KiSendMemoryIpi(ULONG64 Targets)
{
    ULONG Target;
    CHECK((Targets & (1ULL << CurrentCpu)) == 0);
    CHECK((Targets & ~ActiveCpus) == 0);
    __atomic_fetch_add(&Ipis, 1, __ATOMIC_RELAXED);
    while (Targets)
    {
        Target = (ULONG)__builtin_ctzll(Targets);
        __atomic_fetch_or(&Cpus[Target].Pending, 1ULL << CurrentCpu, __ATOMIC_RELEASE);
        Targets &= Targets - 1;
    }
}

VOID MiAmd64TlbPoll(VOID)
{
    ULONG64 Sources = __atomic_exchange_n(&Cpus[CurrentCpu].Pending, 0, __ATOMIC_ACQUIRE);
    while (Sources)
    {
        MiAmd64ProcessTlbRequest((ULONG)__builtin_ctzll(Sources));
        Sources &= Sources - 1;
    }
}

static VOID SeedCpu(ULONG Cpu)
{
    Cpus[Cpu].Cr3 = 0x12345000;
    Cpus[Cpu].Cr4 = (1ULL << 20) | (1ULL << 21) | (1ULL << 5);
    if (Cpu % 4 == 0 || Cpu % 4 == 2)
        Cpus[Cpu].Cr4 |= 1ULL << 7;
    if (Cpu % 4 == 3)
    {
        Cpus[Cpu].Cr4 |= 1ULL << 17;
        Cpus[Cpu].Cr3 |= 42;
    }
    Cpus[Cpu].Enabled = TRUE;
    Cpus[Cpu].Irql = 0;
}

static VOID LocalCases(VOID)
{
    ULONG CpuIndex, Index;
    for (CpuIndex = 0; CpuIndex < 4; CpuIndex++)
    {
        TEST_CPU *Cpu = &Cpus[CpuIndex];
        ULONG64 Cr4, Cr3, Full, Pages, Cpuid;
        CurrentCpu = CpuIndex;
        ActiveCpus = 1ULL << CpuIndex;
        SeedCpu(CpuIndex);
        Cr4 = Cpu->Cr4;
        Cr3 = Cpu->Cr3;
        for (Index = 0; Index < RTL_NUMBER_OF(Cpu->Pages); Index++)
            Cpu->Pages[Index] = 0xffff800000000000ULL + (Index + 1) * PAGE_SIZE;
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)0xffff800000001001ULL, PAGE_SIZE, MiTlbLocal);
        CHECK(Cpu->PageFlushes == 2 && Cpu->FullFlushes == 0);
        CHECK(Cpu->Pages[0] == 0 && Cpu->Pages[1] == 0 && Cpu->Pages[2] != 0);
        CHECK(Cpu->Enabled && Cpu->Irql == 0);
        MiArchInvalidateTlbRange(NULL, 0, MiTlbAllProcessors);
        CHECK(Cpu->PageFlushes == 2 && Cpu->FullFlushes == 0);
        KeFlushEntireTb(FALSE, FALSE);
        for (Index = 0; Index < RTL_NUMBER_OF(Cpu->Pages); Index++)
            CHECK(Cpu->Pages[Index] == 0);
        CHECK(Cpu->Cr4 == Cr4 && Cpu->Cr3 == Cr3);
        CHECK(Cpu->Enabled && Cpu->Irql == 0);
        CHECK(Cpu->Invpcids == (CpuIndex == 0 ? 1u : 0u));
        CHECK(Cpu->Cr3Writes == (CpuIndex == 1 ? 1u : 0u));
        CHECK(Cpu->Cr4Writes == (CpuIndex >= 2 ? 2u : 0u));
        Cpuid = Cpu->CpuidCalls;
        Full = Cpu->FullFlushes;
        Cpu->Enabled = FALSE;
        Cpu->Irql = 15;
        KeFlushCurrentTb();
        CHECK(!Cpu->Enabled && Cpu->Irql == 15);
        CHECK(Cpu->FullFlushes > Full);
        CHECK(Cpu->CpuidCalls == Cpuid);
        CHECK(Cpu->Cr4 == Cr4 && Cpu->Cr3 == Cr3);
        Cpu->Enabled = TRUE;
        Cpu->Irql = 0;
        Pages = Cpu->PageFlushes;
        Full = Cpu->FullFlushes;
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)0xffffffffffffffffULL, 2, MiTlbLocal);
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)0x7fffffffffffULL, 2, MiTlbLocal);
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)0x800000000000ULL, 1, MiTlbLocal);
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)0x1000, (SIZE_T)-1, MiTlbLocal);
        CHECK(Cpu->PageFlushes == Pages && Cpu->FullFlushes >= Full + 4);
        Full = Cpu->FullFlushes;
        MiAmd64FlushTargets(~ActiveCpus, 0, 0);
        CHECK(Cpu->FullFlushes == Full);
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)0x1000, 64 * PAGE_SIZE, MiTlbLocal);
        CHECK(Cpu->PageFlushes == Pages + 64 && Cpu->FullFlushes == Full);
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)0x1000, 65 * PAGE_SIZE, MiTlbLocal);
        CHECK(Cpu->FullFlushes > Full);
    }
    CHECK(KiTbFlushTimeStamp == 4);
    CHECK(Ipis == 0);
}

static VOID *Worker(PVOID Argument)
{
    ULONG Round, CpuCount = (ULONG)__builtin_popcountll(ActiveCpus);
    TEST_CPU *Cpu;
    ULONG64 Cr3, Cr4;
    CurrentCpu = (ULONG)(ULONG_PTR)Argument;
    Cpu = &Cpus[CurrentCpu];
    Cr3 = Cpu->Cr3;
    Cr4 = Cpu->Cr4;
    for (Round = 0; Round < 500; Round++)
    {
        Cpu->Enabled = (Round & 1) != 0;
        Cpu->Irql = Round % 3;
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)(0xffff800000000000ULL + Round * PAGE_SIZE),
                                (Round % 7 + 1) * PAGE_SIZE, MiTlbAllProcessors);
        CHECK(Cpu->Enabled == ((Round & 1) != 0) && Cpu->Irql == Round % 3);
        if (Round % 13 == 0)
            KeFlushEntireTb(TRUE, TRUE);
        if (Round % 17 == 0)
            MiAmd64FlushTargets(1ULL << ((CurrentCpu + 1) % CpuCount), 0x12345000, 1);
        MiAmd64TlbPoll();
    }
    __atomic_fetch_add(&Finished, 1, __ATOMIC_RELEASE);
    while (__atomic_load_n(&Finished, __ATOMIC_ACQUIRE) != CpuCount)
    {
        MiAmd64TlbPoll();
        sched_yield();
    }
    MiAmd64TlbPoll();
    CHECK(Cpu->Cr3 == Cr3 && Cpu->Cr4 == Cr4);
    return NULL;
}

static VOID SmpCase(ULONG Count)
{
    pthread_t Threads[16];
    ULONG64 BeforePages[16], BeforeFull[16];
    ULONG Cpu, Before = KiTbFlushTimeStamp;
    ActiveCpus = (1ULL << Count) - 1;
    Finished = 0;
    for (Cpu = 0; Cpu < Count; Cpu++)
    {
        SeedCpu(Cpu);
        BeforePages[Cpu] = Cpus[Cpu].PageFlushes;
        BeforeFull[Cpu] = Cpus[Cpu].FullFlushes;
    }
    for (Cpu = 0; Cpu < Count; Cpu++)
        CHECK(pthread_create(&Threads[Cpu], NULL, Worker, (PVOID)(ULONG_PTR)Cpu) == 0);
    for (Cpu = 0; Cpu < Count; Cpu++)
        CHECK(pthread_join(Threads[Cpu], NULL) == 0);
    CHECK((ULONG)KiTbFlushTimeStamp == Before + Count * 39);
    for (Cpu = 0; Cpu < Count; Cpu++)
    {
        ULONG Source;
        ULONG64 Pages = Cpus[Cpu].PageFlushes, Full = Cpus[Cpu].FullFlushes;
        CurrentCpu = Cpu;
        CHECK(Pages == BeforePages[Cpu] + Count * 1994 + 30);
        CHECK(Full == BeforeFull[Cpu] + Count * 39 * (Cpu % 4 >= 2 ? 2 : 1));
        CHECK(Cpus[Cpu].Pending == 0);
        for (Source = 0; Source < Count; Source++)
            MiAmd64ProcessTlbRequest(Source);
        CHECK(Cpus[Cpu].PageFlushes == Pages && Cpus[Cpu].FullFlushes == Full);
    }
}

int main(void)
{
    CHECK(MiAmd64SelfMapSlotAddress(0, 0) == 0xfffff68000000000ULL);
    CHECK(MiAmd64SelfMapSlotAddress(0, 1) == 0xfffff6fb40000000ULL);
    CHECK(MiAmd64SelfMapSlotAddress(0, 2) == 0xfffff6fb7da00000ULL);
    CHECK(MiAmd64SelfMapSlotAddress(0, 3) == 0xfffff6fb7dbed000ULL);
    LocalCases();
    SmpCase(2);
    SmpCase(4);
    SmpCase(8);
    printf("AMD64 TLB: %u checks, %u failures, %llu IPI batches\n", Checks, Failures, Ipis);
    return Failures != 0;
}
