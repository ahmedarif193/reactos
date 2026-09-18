/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Remote ready, standby and running thread transitions
 */

#include <kmt_test.h>

VOID Test_KeArm64Dispatcher(VOID);

#ifdef _M_ARM64

#define SCHEDULER_ROUNDS 32
#define SCHEDULER_MODES 7

typedef struct _SCHEDULER_HOLD
{
    KDPC Dpc;
    ULONG Cpu;
    LONGLONG Frequency;
    volatile LONG Entered;
    volatile LONG Release;
    volatile LONG Active;
    volatile LONG Expired;
} SCHEDULER_HOLD;

typedef struct _SCHEDULER_WORKER
{
    KEVENT Ready;
    KEVENT Go;
    KEVENT Done;
    SCHEDULER_HOLD *Hold;
    volatile LONG *Sequence;
    volatile LONG Stop;
    volatile LONG Started;
    volatile LONG SpinRelease;
    volatile LONG LastCpu;
    BOOLEAN Spin;
    ULONG Order;
    ULONG Cpu;
    ULONG StateErrors;
    KPRIORITY Priority;
    PKPRCB Prcb;
    LONGLONG Received;
    LONGLONG Migrated;
    ULONGLONG RuntimeTicks;
} SCHEDULER_WORKER;

static BOOLEAN
WaitSchedulerFlag(volatile LONG *Flag, LONG Value, LONGLONG Frequency)
{
    LONGLONG Deadline = KeQueryPerformanceCounter(NULL).QuadPart + Frequency;

    while (*Flag != Value && KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
        YieldProcessor();
    KeMemoryBarrier();
    return *Flag == Value;
}

static BOOLEAN
WaitSchedulerEvent(PKEVENT Event)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10000000;
    return KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS;
}

static VOID NTAPI
HoldSchedulerCpu(PKDPC Dpc, PVOID Parameter, PVOID Argument1, PVOID Argument2)
{
    SCHEDULER_HOLD *Hold = Parameter;
    LONGLONG Deadline;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    Deadline = KeQueryPerformanceCounter(NULL).QuadPart + Hold->Frequency;
    InterlockedExchange(&Hold->Active, 1);
    InterlockedExchange(&Hold->Entered, 1);
    while (!Hold->Release && KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
        YieldProcessor();
    if (!Hold->Release) InterlockedIncrement(&Hold->Expired);
    InterlockedExchange(&Hold->Active, 0);
}

static VOID NTAPI
SchedulerWorker(PVOID Parameter)
{
    SCHEDULER_WORKER *Worker = Parameter;
    KIRQL OldIrql;
    ULONG Cpu;

    for (;;)
    {
        KeSetEvent(&Worker->Ready, IO_NO_INCREMENT, TRUE);
        KeWaitForSingleObject(&Worker->Go, Executive, KernelMode, FALSE, NULL);
        if (Worker->Stop) break;
        Worker->Received = KeQueryPerformanceCounter(NULL).QuadPart;
        OldIrql = KeRaiseIrqlToDpcLevel();
        Worker->Cpu = KeGetCurrentProcessorNumber();
        Worker->Prcb = KeGetCurrentPrcb();
        Worker->RuntimeTicks = (ULONGLONG)Worker->Prcb->KernelTime + Worker->Prcb->UserTime;
        Worker->Priority = KeQueryPriorityThread(KeGetCurrentThread());
        if (OldIrql != PASSIVE_LEVEL ||
            (Worker->Hold->Active && Worker->Cpu == Worker->Hold->Cpu))
            Worker->StateErrors++;
        Worker->Order = InterlockedIncrement(Worker->Sequence);
        KeLowerIrql(OldIrql);
        InterlockedExchange(&Worker->Started, 1);
        while (Worker->Spin && !Worker->SpinRelease && !Worker->Stop)
        {
            Cpu = KeGetCurrentProcessorNumber();
            if (Cpu != Worker->Cpu && Worker->Migrated == 0)
                Worker->Migrated = KeQueryPerformanceCounter(NULL).QuadPart;
            InterlockedExchange(&Worker->LastCpu, (LONG)Cpu);
            YieldProcessor();
        }
        KeSetEvent(&Worker->Done, IO_NO_INCREMENT, FALSE);
    }
}

static VOID
SortSchedulerTimes(LONGLONG *Times, ULONG Count)
{
    ULONG Index, Position;
    LONGLONG Value;

    for (Index = 1; Index < Count; Index++)
    {
        Value = Times[Index];
        Position = Index;
        while (Position && Times[Position - 1] > Value)
        {
            Times[Position] = Times[Position - 1];
            Position--;
        }
        Times[Position] = Value;
    }
}

static VOID
CheckSchedulerCpu(ULONG Controller, ULONG Cpu, ULONG OtherCpu, LONGLONG Frequency)
{
    static const KPRIORITY ExpectedPriority[SCHEDULER_MODES][2] = {
        {18, 17}, {20, 19}, {18, 17}, {18, 19}, {18, 17}, {18, 19}, {18, 17}
    };
    SCHEDULER_HOLD Hold = {0};
    SCHEDULER_WORKER Workers[2] = {0};
    PKTHREAD Threads[2] = {0};
    volatile LONG Sequence = 0;
    ULONG Index, Mode, Round, Completed, StateErrors, OrderErrors;
    ULONG AffinityErrors, PriorityErrors, Timeouts;
    ULONG NoRuntimeTick, LatencyErrors;
    LONGLONG Start = 0, DispatchTimes[SCHEDULER_ROUNDS], MigrationTimes[SCHEDULER_ROUNDS];
    ULONGLONG RuntimeBefore = 0;
    BOOLEAN Held, Success, Migrating;

    Hold.Cpu = Cpu;
    Hold.Frequency = Frequency;
    KeInitializeDpc(&Hold.Dpc, HoldSchedulerCpu, &Hold);
    KeSetTargetProcessorDpc(&Hold.Dpc, (CCHAR)Cpu);
    KeSetImportanceDpc(&Hold.Dpc, HighImportance);
    for (Index = 0; Index < 2; Index++)
    {
        Workers[Index].Hold = &Hold;
        Workers[Index].Sequence = &Sequence;
        Workers[Index].LastCpu = -1;
        KeInitializeEvent(&Workers[Index].Ready, SynchronizationEvent, FALSE);
        KeInitializeEvent(&Workers[Index].Go, SynchronizationEvent, FALSE);
        KeInitializeEvent(&Workers[Index].Done, SynchronizationEvent, FALSE);
        Threads[Index] = KmtStartThread(SchedulerWorker, &Workers[Index]);
        if (!Threads[Index]) goto Cleanup;
        Success = WaitSchedulerEvent(&Workers[Index].Ready);
        ok(Success, "CPU %lu worker %lu did not initialize\n", Cpu, Index);
        if (!Success) goto Cleanup;
    }

    for (Mode = 0; Mode < SCHEDULER_MODES; Mode++)
    {
        Completed = StateErrors = OrderErrors = AffinityErrors = PriorityErrors = Timeouts = 0;
        NoRuntimeTick = LatencyErrors = 0;
        for (Round = 0; Round < SCHEDULER_ROUNDS; Round++)
        {
            Sequence = 0;
            Hold.Entered = Hold.Release = Hold.Expired = 0;
            for (Index = 0; Index < 2; Index++)
            {
                Workers[Index].Started = Workers[Index].SpinRelease = 0;
                Workers[Index].LastCpu = -1;
                Workers[Index].Received = Workers[Index].Migrated = 0;
                Workers[Index].StateErrors = 0;
                Workers[Index].Spin = (Mode >= 4 && Index == 0);
                KeSetAffinityThread(Threads[Index], (KAFFINITY)1 << Cpu);
                KeSetPriorityThread(Threads[Index], Index == 0 ? 18 : (Mode >= 4 ? 17 : 19));
            }
            Held = Mode < 4;
            Migrating = Mode == 2 || Mode == 3;
            Success = TRUE;
            if (Held)
            {
                Success = KeInsertQueueDpc(&Hold.Dpc, NULL, NULL);
                if (Success) Success = WaitSchedulerFlag(&Hold.Entered, 1, Frequency);
            }
            if (Success)
            {
                KeSetEvent(&Workers[0].Go, IO_NO_INCREMENT, FALSE);
                if (!Held) Success = WaitSchedulerFlag(&Workers[0].Started, 1, Frequency);
            }
            if (Success)
            {
                KeSetEvent(&Workers[1].Go, IO_NO_INCREMENT, FALSE);
                if (Held)
                {
                    if (Threads[0]->State != Ready || Threads[1]->State != Standby)
                        StateErrors++;
                }
                else if (Threads[0]->State != Running || Threads[1]->State != Ready)
                {
                    StateErrors++;
                }
                if (!Held)
                {
                    RuntimeBefore = (ULONGLONG)Workers[0].Prcb->KernelTime + Workers[0].Prcb->UserTime;
                    Start = KeQueryPerformanceCounter(NULL).QuadPart;
                }
                switch (Mode)
                {
                    case 0: /* Lower the standby thread below the ready thread. */
                        KeSetPriorityThread(Threads[1], 17);
                        break;
                    case 1: /* Raise the ready thread above the standby thread. */
                        KeSetPriorityThread(Threads[0], 20);
                        break;
                    case 2: /* Move the ready thread to an unblocked processor. */
                        KeSetPriorityThread(Threads[1], 17);
                        KeSetAffinityThread(Threads[1], (KAFFINITY)1 << OtherCpu);
                        break;
                    case 3: /* Move the standby thread to an unblocked processor. */
                        KeSetAffinityThread(Threads[1], (KAFFINITY)1 << OtherCpu);
                        break;
                    case 4: /* Lower a running thread so its ready peer preempts it. */
                        KeSetPriorityThread(Threads[0], 16);
                        break;
                    case 5: /* Raise a ready thread so it preempts its running peer. */
                        KeSetPriorityThread(Threads[1], 19);
                        break;
                    case 6: /* Migrate a running thread, allowing its ready peer to run. */
                        KeSetAffinityThread(Threads[0], (KAFFINITY)1 << OtherCpu);
                        Success = WaitSchedulerFlag(&Workers[0].LastCpu, (LONG)OtherCpu, Frequency);
                        break;
                }
                if (Migrating || !Held)
                {
                    if (!WaitSchedulerEvent(&Workers[1].Done)) Success = FALSE;
                    if (Migrating && !Hold.Active) StateErrors++;
                }
            }
            InterlockedExchange(&Hold.Release, 1);
            InterlockedExchange(&Workers[0].SpinRelease, 1);
            if (Held) KeFlushQueuedDpcs();
            if (Success && !WaitSchedulerEvent(&Workers[0].Done)) Success = FALSE;
            if (Success && Held && !Migrating && !WaitSchedulerEvent(&Workers[1].Done)) Success = FALSE;
            if (Success && !WaitSchedulerEvent(&Workers[0].Ready)) Success = FALSE;
            if (Success && !WaitSchedulerEvent(&Workers[1].Ready)) Success = FALSE;
            if (!Success)
            {
                Timeouts++;
                break;
            }
            StateErrors += Workers[0].StateErrors + Workers[1].StateErrors + Hold.Expired;
            if (Workers[0].Order != (Migrating ? 2 : 1) ||
                Workers[1].Order != (Migrating ? 1 : 2)) OrderErrors++;
            if (Workers[0].Cpu != Cpu || Workers[1].Cpu != (Migrating ? OtherCpu : Cpu))
                AffinityErrors++;
            if (Workers[0].Priority != ExpectedPriority[Mode][0] ||
                Workers[1].Priority != ExpectedPriority[Mode][1]) PriorityErrors++;
            if (!Held)
            {
                DispatchTimes[Completed] = Workers[1].Received - Start;
                MigrationTimes[Completed] = Mode == 6 ? Workers[0].Migrated - Start : 0;
                if (DispatchTimes[Completed] < 0 || MigrationTimes[Completed] < 0)
                    LatencyErrors++;
                if (Workers[1].RuntimeTicks == RuntimeBefore) NoRuntimeTick++;
            }
            Completed++;
        }
        ok_eq_ulong(Completed, SCHEDULER_ROUNDS);
        ok_eq_ulong(StateErrors, 0);
        ok_eq_ulong(OrderErrors, 0);
        ok_eq_ulong(AffinityErrors, 0);
        ok_eq_ulong(PriorityErrors, 0);
        ok_eq_ulong(Timeouts, 0);
        trace("SCHED_TRANSITION controller=%lu cpu=%lu other=%lu mode=%lu rounds=%lu state_errors=%lu order_errors=%lu affinity_errors=%lu priority_errors=%lu timeouts=%lu\n",
              Controller, Cpu, OtherCpu, Mode, Completed, StateErrors, OrderErrors,
              AffinityErrors, PriorityErrors, Timeouts);
        if (Mode >= 4 && Completed)
        {
            ok_eq_ulong(LatencyErrors, 0);
            ok(NoRuntimeTick != 0, "CPU %lu mode %lu never dispatched before the next runtime tick\n", Cpu, Mode);
            SortSchedulerTimes(DispatchTimes, Completed);
            SortSchedulerTimes(MigrationTimes, Completed);
            trace("SCHED_LATENCY controller=%lu cpu=%lu mode=%lu samples=%lu no_runtime_tick=%lu p50_us=%I64d p95_us=%I64d max_us=%I64d migrate_p50_us=%I64d migrate_max_us=%I64d\n",
                  Controller, Cpu, Mode, Completed, NoRuntimeTick,
                  DispatchTimes[(Completed - 1) / 2] * 1000000 / Frequency,
                  DispatchTimes[(Completed - 1) * 95 / 100] * 1000000 / Frequency,
                  DispatchTimes[Completed - 1] * 1000000 / Frequency,
                  MigrationTimes[(Completed - 1) / 2] * 1000000 / Frequency,
                  MigrationTimes[Completed - 1] * 1000000 / Frequency);
        }
        if (Timeouts) break;
    }

Cleanup:
    InterlockedExchange(&Hold.Release, 1);
    for (Index = 0; Index < 2; Index++)
    {
        InterlockedExchange(&Workers[Index].Stop, 1);
        InterlockedExchange(&Workers[Index].SpinRelease, 1);
        if (Threads[Index]) KmtFinishThread(Threads[Index], &Workers[Index].Go);
    }
    KeFlushQueuedDpcs();
}

#endif

START_TEST(KeArm64Dispatcher)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64Dispatcher is ARM64-only\n");
#else
    KAFFINITY PreviousAffinity;
    KPRIORITY PreviousPriority;
    LARGE_INTEGER Frequency;
    ULONG Controller, Cpu, OtherCpu;

    if (skip(KeNumberProcessors >= 3, "Three processors required\n")) return;
    KeQueryPerformanceCounter(&Frequency);
    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    PreviousPriority = KeSetPriorityThread(KeGetCurrentThread(), 22);
    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
    {
        Controller = (Cpu + 1) % (ULONG)KeNumberProcessors;
        OtherCpu = (Cpu + 2) % (ULONG)KeNumberProcessors;
        KeSetSystemAffinityThread((KAFFINITY)1 << Controller);
        CheckSchedulerCpu(Controller, Cpu, OtherCpu, Frequency.QuadPart);
    }
    KeSetPriorityThread(KeGetCurrentThread(), PreviousPriority);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
#endif
}
