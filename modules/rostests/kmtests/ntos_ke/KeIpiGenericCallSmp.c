/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite SMP IPI rendezvous test
 * COPYRIGHT:   Copyright 2026
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define KESMP_IPI_MAX_CPUS     (sizeof(KAFFINITY) * 8)
#define KESMP_IPI_ROUNDS       128
#define KESMP_IPI_SPIN_LIMIT   100000

typedef struct _KMT_IPI_CONTEXT
{
    KAFFINITY ActiveMask;
    ULONG ActiveCount;
    volatile LONG Entered;
    volatile LONG Released;
    volatile LONG DuplicateEntries;
    volatile LONG BadCpuEntries;
    volatile LONG EntryTimeouts;
    volatile LONG PerCpu[KESMP_IPI_MAX_CPUS];
} KMT_IPI_CONTEXT, *PKMT_IPI_CONTEXT;

static
ULONG
CountSetBits(
    KAFFINITY Mask)
{
    ULONG Count;

    Count = 0;
    while (Mask != 0)
    {
        Count += (ULONG)(Mask & 1);
        Mask >>= 1;
    }

    return Count;
}

static
KAFFINITY
NthAffinityBit(
    KAFFINITY ActiveMask,
    ULONG Index)
{
    ULONG Bit;

    for (Bit = 0; Bit < KESMP_IPI_MAX_CPUS; Bit++)
    {
        KAFFINITY BitMask;

        BitMask = ((KAFFINITY)1) << Bit;
        if (!(ActiveMask & BitMask))
            continue;

        if (Index == 0)
            return BitMask;

        Index--;
    }

    return 0;
}

static
ULONG_PTR
NTAPI
IpiGenericCallWorker(
    ULONG_PTR Context)
{
    PKMT_IPI_CONTEXT IpiContext;
    ULONG CurrentCpu;
    ULONG Spin;

    IpiContext = (PKMT_IPI_CONTEXT)Context;
    CurrentCpu = KeGetCurrentProcessorNumber();

    if (CurrentCpu >= KESMP_IPI_MAX_CPUS ||
        !(IpiContext->ActiveMask & (((KAFFINITY)1) << CurrentCpu)))
    {
        InterlockedIncrement(&IpiContext->BadCpuEntries);
        return (ULONG_PTR)CurrentCpu;
    }

    if (InterlockedIncrement(&IpiContext->PerCpu[CurrentCpu]) != 1)
        InterlockedIncrement(&IpiContext->DuplicateEntries);

    KeMemoryBarrier();
    InterlockedIncrement(&IpiContext->Entered);

    for (Spin = 0; Spin < KESMP_IPI_SPIN_LIMIT; Spin++)
    {
        if ((ULONG)IpiContext->Entered >= IpiContext->ActiveCount)
            break;

        YieldProcessor();
    }

    if ((ULONG)IpiContext->Entered < IpiContext->ActiveCount)
        InterlockedIncrement(&IpiContext->EntryTimeouts);

    KeMemoryBarrier();
    InterlockedIncrement(&IpiContext->Released);
    return (ULONG_PTR)CurrentCpu;
}

START_TEST(KeIpiGenericCallSmp)
{
    KMT_IPI_CONTEXT Context;
    KAFFINITY ActiveMask;
    ULONG ActiveCount;
    ULONG Round;

    if (!KmtIsMultiProcessorBuild)
    {
        skip(TRUE, "This test requires an MP kernel build\n");
        return;
    }

    ActiveMask = KeQueryActiveProcessors();
    ActiveCount = CountSetBits(ActiveMask);
    if (ActiveCount < 2)
    {
        skip(TRUE, "Need at least 2 active CPUs, got %lu\n", ActiveCount);
        return;
    }

    trace("Running KeIpiGenericCall SMP test on mask %p (%lu CPUs)\n",
          (PVOID)(ULONG_PTR)ActiveMask,
          ActiveCount);

    for (Round = 0; Round < KESMP_IPI_ROUNDS; Round++)
    {
        KAFFINITY Affinity;
        ULONG CallerCpu;
        ULONG Bit;
        ULONG_PTR ReturnedCpu;

        Affinity = NthAffinityBit(ActiveMask, Round % ActiveCount);
        ok(Affinity != 0, "Round %lu has no affinity target\n", Round);
        if (Affinity == 0)
            break;

        RtlZeroMemory(&Context, sizeof(Context));
        Context.ActiveMask = ActiveMask;
        Context.ActiveCount = ActiveCount;

        KeSetSystemAffinityThread(Affinity);
        CallerCpu = KeGetCurrentProcessorNumber();
        ok((((KAFFINITY)1) << CallerCpu) == Affinity,
           "Round %lu running on CPU %lu for affinity %p\n",
           Round,
           CallerCpu,
           (PVOID)(ULONG_PTR)Affinity);

        ReturnedCpu = KeIpiGenericCall(IpiGenericCallWorker, (ULONG_PTR)&Context);
        KeRevertToUserAffinityThread();

        ok_eq_ulongptr(ReturnedCpu, CallerCpu);
        ok_eq_long(Context.DuplicateEntries, 0);
        ok_eq_long(Context.BadCpuEntries, 0);
        ok_eq_long(Context.EntryTimeouts, 0);
        ok_eq_long(Context.Entered, ActiveCount);
        ok_eq_long(Context.Released, ActiveCount);

        for (Bit = 0; Bit < KESMP_IPI_MAX_CPUS; Bit++)
        {
            LONG Expected;

            Expected = (ActiveMask & (((KAFFINITY)1) << Bit)) ? 1 : 0;
            ok_eq_long(Context.PerCpu[Bit], Expected);
        }
    }
}
