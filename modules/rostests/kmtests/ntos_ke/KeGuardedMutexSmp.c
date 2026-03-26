/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite SMP guarded mutex stress test
 * COPYRIGHT:   Copyright 2026
 *
 * This is intended as the first torture-style SMP kmtest for ntoskrnl
 * synchronization. Follow-up tests should cover:
 *  - cross-CPU APC/DPC delivery and ordering
 *  - pushlock / ERESOURCE contention
 *  - address-space lock and page-out races
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define KESMP_MAX_WORKERS        4
#define KESMP_ITERATIONS         2000
#define KESMP_SWITCH_INTERVAL    8
#define KESMP_HOLD_US            25

typedef struct _KMT_GM_SMP_SHARED
{
    KGUARDED_MUTEX Mutex;
    KEVENT StartEvent;
    volatile LONG ActiveOwners;
    volatile LONG AcquisitionCount;
} KMT_GM_SMP_SHARED, *PKMT_GM_SMP_SHARED;

typedef struct _KMT_GM_SMP_WORKER
{
    PKMT_GM_SMP_SHARED Shared;
    KEVENT ReadyEvent;
    PKTHREAD Thread;
    ULONG WorkerIndex;
    ULONG Iterations;
    KAFFINITY PrimaryAffinity;
    KAFFINITY SecondaryAffinity;
    volatile LONG Acquisitions;
    volatile LONG OverlapFailures;
    volatile LONG OwnerFailures;
    volatile LONG AffinityFailures;
} KMT_GM_SMP_WORKER, *PKMT_GM_SMP_WORKER;

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
GuardedMutexSmpWorker(
    PVOID Parameter)
{
    PKMT_GM_SMP_WORKER Worker;
    ULONG Iteration;

    Worker = Parameter;

    ok(Worker != NULL, "Worker context missing\n");
    if (Worker == NULL)
        PsTerminateSystemThread(STATUS_INVALID_PARAMETER);

    ok_bool_false(KeSetEvent(&Worker->ReadyEvent, IO_NO_INCREMENT, FALSE),
                  "KeSetEvent returned\n");
    ok_eq_hex(KeWaitForSingleObject(&Worker->Shared->StartEvent,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL),
              STATUS_SUCCESS);

    for (Iteration = 0; Iteration < Worker->Iterations; Iteration++)
    {
        KAFFINITY Affinity;
        ULONG CurrentCpu;

        if (((Iteration / KESMP_SWITCH_INTERVAL) & 1) != 0)
            Affinity = Worker->SecondaryAffinity;
        else
            Affinity = Worker->PrimaryAffinity;

        KeSetSystemAffinityThread(Affinity);

        CurrentCpu = KeGetCurrentProcessorNumber();
        if ((((KAFFINITY)1) << CurrentCpu) & Affinity)
        {
            /* expected */
        }
        else
        {
            InterlockedIncrement(&Worker->AffinityFailures);
        }

        KeAcquireGuardedMutex(&Worker->Shared->Mutex);

        if (Worker->Shared->Mutex.Owner != KeGetCurrentThread())
            InterlockedIncrement(&Worker->OwnerFailures);

        if (InterlockedIncrement(&Worker->Shared->ActiveOwners) != 1)
            InterlockedIncrement(&Worker->OverlapFailures);

        InterlockedIncrement(&Worker->Shared->AcquisitionCount);
        InterlockedIncrement(&Worker->Acquisitions);

        KeStallExecutionProcessor(KESMP_HOLD_US);

        if (InterlockedDecrement(&Worker->Shared->ActiveOwners) != 0)
            InterlockedIncrement(&Worker->OverlapFailures);

        KeReleaseGuardedMutex(&Worker->Shared->Mutex);
        KeRevertToUserAffinityThread();

        if ((Iteration & 0x1f) == 0)
            KeStallExecutionProcessor(1);
    }
}

START_TEST(KeGuardedMutexSmp)
{
    KMT_GM_SMP_SHARED Shared;
    KMT_GM_SMP_WORKER Workers[KESMP_MAX_WORKERS];
    KAFFINITY ActiveMask;
    ULONG ActiveCount;
    ULONG WorkerCount;
    ULONG i;
    LONG TotalAcquisitions;

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

    WorkerCount = ActiveCount;
    if (WorkerCount > KESMP_MAX_WORKERS)
        WorkerCount = KESMP_MAX_WORKERS;

    trace("Running guarded mutex SMP stress with %lu workers on mask %p\n",
          WorkerCount,
          (PVOID)(ULONG_PTR)ActiveMask);

    RtlZeroMemory(&Shared, sizeof(Shared));
    KeInitializeGuardedMutex(&Shared.Mutex);
    KeInitializeEvent(&Shared.StartEvent, NotificationEvent, FALSE);

    RtlZeroMemory(Workers, sizeof(Workers));
    for (i = 0; i < WorkerCount; i++)
    {
        Workers[i].Shared = &Shared;
        Workers[i].WorkerIndex = i;
        Workers[i].Iterations = KESMP_ITERATIONS;
        Workers[i].PrimaryAffinity = NthAffinityBit(ActiveMask, i);
        Workers[i].SecondaryAffinity = NthAffinityBit(ActiveMask,
                                                      (i + 1) % ActiveCount);
        if (Workers[i].SecondaryAffinity == 0)
            Workers[i].SecondaryAffinity = Workers[i].PrimaryAffinity;

        ok(Workers[i].PrimaryAffinity != 0, "Worker %lu has no primary CPU\n", i);
        ok(Workers[i].SecondaryAffinity != 0, "Worker %lu has no secondary CPU\n", i);

        KeInitializeEvent(&Workers[i].ReadyEvent, SynchronizationEvent, FALSE);
        Workers[i].Thread = KmtStartThread(GuardedMutexSmpWorker, &Workers[i]);
    }

    for (i = 0; i < WorkerCount; i++)
    {
        ok_eq_hex(KeWaitForSingleObject(&Workers[i].ReadyEvent,
                                        Executive,
                                        KernelMode,
                                        FALSE,
                                        NULL),
                  STATUS_SUCCESS);
    }

    ok_bool_false(KeSetEvent(&Shared.StartEvent, IO_NO_INCREMENT, FALSE),
                  "KeSetEvent returned\n");

    for (i = 0; i < WorkerCount; i++)
        KmtFinishThread(Workers[i].Thread, NULL);

    TotalAcquisitions = 0;
    for (i = 0; i < WorkerCount; i++)
    {
        ok_eq_long(Workers[i].OverlapFailures, 0);
        ok_eq_long(Workers[i].OwnerFailures, 0);
        ok_eq_long(Workers[i].AffinityFailures, 0);
        ok_eq_long(Workers[i].Acquisitions, KESMP_ITERATIONS);
        TotalAcquisitions += Workers[i].Acquisitions;
    }

    ok_eq_long(Shared.ActiveOwners, 0);
    ok_eq_long(Shared.AcquisitionCount, WorkerCount * KESMP_ITERATIONS);
    ok_eq_long(TotalAcquisitions, WorkerCount * KESMP_ITERATIONS);
    ok_eq_pointer(Shared.Mutex.Owner, NULL);
    ok_eq_long(Shared.Mutex.Count, 1);
}
