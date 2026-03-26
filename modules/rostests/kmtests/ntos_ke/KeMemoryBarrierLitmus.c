/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite SMP memory barrier litmus test
 * COPYRIGHT:   Copyright 2026
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define KESMP_MB_SPIN_LIMIT          200000
#define KESMP_MB_RELAXED_ROUNDS      50000
#define KESMP_MB_BARRIER_ROUNDS      50000

typedef struct _KMT_MB_SHARED
{
    volatile LONG Generation;
    volatile LONG Armed;
    volatile LONG Finished;
    volatile LONG Stop;
    volatile LONG Timeouts;
    volatile LONG R1;
    volatile LONG R2;
    volatile LONG X;
    LONG Padding0[15];
    volatile LONG Y;
    LONG Padding1[15];
    BOOLEAN UseBarrier;
} KMT_MB_SHARED, *PKMT_MB_SHARED;

typedef struct _KMT_MB_WORKER
{
    PKMT_MB_SHARED Shared;
    KEVENT ReadyEvent;
    PKTHREAD Thread;
    KAFFINITY Affinity;
    ULONG WorkerIndex;
    volatile LONG AffinityFailures;
    volatile LONG IterationsRun;
} KMT_MB_WORKER, *PKMT_MB_WORKER;

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

    for (Bit = 0; Bit < sizeof(KAFFINITY) * 8; Bit++)
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
VOID
NTAPI
MemoryBarrierWorker(
    PVOID Parameter)
{
    PKMT_MB_WORKER Worker;
    LONG LocalGeneration;

    Worker = Parameter;
    LocalGeneration = 0;

    KeSetSystemAffinityThread(Worker->Affinity);
    if (!((((KAFFINITY)1) << KeGetCurrentProcessorNumber()) & Worker->Affinity))
        InterlockedIncrement(&Worker->AffinityFailures);

    ok_bool_false(KeSetEvent(&Worker->ReadyEvent, IO_NO_INCREMENT, FALSE),
                  "KeSetEvent returned\n");

    while (InterlockedCompareExchange(&Worker->Shared->Stop, 0, 0) == 0)
    {
        LONG Generation;
        ULONG Spin;

        while ((Generation = InterlockedCompareExchange(&Worker->Shared->Generation, 0, 0)) == LocalGeneration)
        {
            if (InterlockedCompareExchange(&Worker->Shared->Stop, 0, 0) != 0)
                goto Exit;

            YieldProcessor();
        }

        if (InterlockedCompareExchange(&Worker->Shared->Stop, 0, 0) != 0)
            break;

        LocalGeneration = Generation;
        InterlockedIncrement(&Worker->Shared->Armed);

        for (Spin = 0; Spin < KESMP_MB_SPIN_LIMIT; Spin++)
        {
            if (InterlockedCompareExchange(&Worker->Shared->Armed, 0, 0) >= 2)
                break;

            YieldProcessor();
        }

        if (InterlockedCompareExchange(&Worker->Shared->Armed, 0, 0) < 2)
            InterlockedIncrement(&Worker->Shared->Timeouts);

        if (Worker->WorkerIndex == 0)
        {
            Worker->Shared->X = 1;
            if (Worker->Shared->UseBarrier)
                KeMemoryBarrier();
            else
                KeMemoryBarrierWithoutFence();
            Worker->Shared->R1 = Worker->Shared->Y;
        }
        else
        {
            Worker->Shared->Y = 1;
            if (Worker->Shared->UseBarrier)
                KeMemoryBarrier();
            else
                KeMemoryBarrierWithoutFence();
            Worker->Shared->R2 = Worker->Shared->X;
        }

        InterlockedIncrement(&Worker->IterationsRun);
        InterlockedIncrement(&Worker->Shared->Finished);
    }

Exit:
    KeRevertToUserAffinityThread();
}

static
VOID
RunLitmusRounds(
    PKMT_MB_SHARED Shared,
    PKMT_MB_WORKER Workers,
    ULONG Rounds,
    BOOLEAN UseBarrier,
    PLONG ZeroZeroCount,
    PLONG TimeoutCount)
{
    ULONG Iteration;

    *ZeroZeroCount = 0;
    *TimeoutCount = 0;

    Shared->UseBarrier = UseBarrier;
    for (Iteration = 0; Iteration < Rounds; Iteration++)
    {
        ULONG Spin;

        Shared->X = 0;
        Shared->Y = 0;
        Shared->R1 = -1;
        Shared->R2 = -1;
        Shared->Armed = 0;
        Shared->Finished = 0;
        KeMemoryBarrierWithoutFence();
        InterlockedIncrement(&Shared->Generation);

        for (Spin = 0; Spin < KESMP_MB_SPIN_LIMIT; Spin++)
        {
            if (InterlockedCompareExchange(&Shared->Finished, 0, 0) >= 2)
                break;

            YieldProcessor();
        }

        if (InterlockedCompareExchange(&Shared->Finished, 0, 0) < 2)
        {
            InterlockedIncrement(&Shared->Timeouts);
            (*TimeoutCount)++;
            continue;
        }

        if (Shared->R1 == 0 && Shared->R2 == 0)
            (*ZeroZeroCount)++;
    }

    ok_eq_long(Workers[0].AffinityFailures, 0);
    ok_eq_long(Workers[1].AffinityFailures, 0);
}

START_TEST(KeMemoryBarrierLitmus)
{
    KMT_MB_SHARED Shared;
    KMT_MB_WORKER Workers[2];
    KAFFINITY ActiveMask;
    ULONG ActiveCount;
    LONG RelaxedZeroZero;
    LONG RelaxedTimeouts;
    LONG BarrierZeroZero;
    LONG BarrierTimeouts;
    ULONG i;

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

    RtlZeroMemory(&Shared, sizeof(Shared));
    RtlZeroMemory(Workers, sizeof(Workers));

    for (i = 0; i < RTL_NUMBER_OF(Workers); i++)
    {
        Workers[i].Shared = &Shared;
        Workers[i].WorkerIndex = i;
        Workers[i].Affinity = NthAffinityBit(ActiveMask, i);
        ok(Workers[i].Affinity != 0, "Worker %lu has no affinity\n", i);
        KeInitializeEvent(&Workers[i].ReadyEvent, SynchronizationEvent, FALSE);
        Workers[i].Thread = KmtStartThread(MemoryBarrierWorker, &Workers[i]);
    }

    for (i = 0; i < RTL_NUMBER_OF(Workers); i++)
    {
        ok_eq_hex(KeWaitForSingleObject(&Workers[i].ReadyEvent,
                                        Executive,
                                        KernelMode,
                                        FALSE,
                                        NULL),
                  STATUS_SUCCESS);
    }

    trace("Running relaxed store-buffering litmus on affinities %p and %p\n",
          (PVOID)(ULONG_PTR)Workers[0].Affinity,
          (PVOID)(ULONG_PTR)Workers[1].Affinity);

    RunLitmusRounds(&Shared,
                    Workers,
                    KESMP_MB_RELAXED_ROUNDS,
                    FALSE,
                    &RelaxedZeroZero,
                    &RelaxedTimeouts);
    RunLitmusRounds(&Shared,
                    Workers,
                    KESMP_MB_BARRIER_ROUNDS,
                    TRUE,
                    &BarrierZeroZero,
                    &BarrierTimeouts);

    Shared.Stop = 1;
    KeMemoryBarrierWithoutFence();
    InterlockedIncrement(&Shared.Generation);
    for (i = 0; i < RTL_NUMBER_OF(Workers); i++)
        KmtFinishThread(Workers[i].Thread, NULL);

    trace("Relaxed zero/zero=%ld timeouts=%ld, barrier zero/zero=%ld timeouts=%ld\n",
          RelaxedZeroZero,
          RelaxedTimeouts,
          BarrierZeroZero,
          BarrierTimeouts);

    ok_eq_long(RelaxedTimeouts, 0);
    ok_eq_long(BarrierTimeouts, 0);
    ok_eq_long(Shared.Timeouts, 0);
    ok_eq_long(BarrierZeroZero, 0);
    ok_eq_long(Workers[0].IterationsRun, KESMP_MB_RELAXED_ROUNDS + KESMP_MB_BARRIER_ROUNDS);
    ok_eq_long(Workers[1].IterationsRun, KESMP_MB_RELAXED_ROUNDS + KESMP_MB_BARRIER_ROUNDS);

    if (RelaxedZeroZero == 0)
        trace("No relaxed zero/zero outcome observed; this can happen on some VMs or timings\n");
}
