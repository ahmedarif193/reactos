/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     DPC flush completion across active callbacks and scheduler batches
 */

#include <kmt_test.h>

VOID Test_KeArm64DpcFlush(VOID);

#define FLUSH_BACKLOG 128
#define FLUSH_STRESS_ROUNDS 64

typedef struct _FLUSH_CONTEXT
{
    KEVENT Ready;
    KEVENT Go;
    KDPC Blocker;
    volatile LONG Started;
    volatile LONG Release;
    volatile LONG Finished;
    volatile LONG TimedOut;
    volatile LONG Drained;
    volatile LONG FlushEntered;
    volatile LONG FlushReturned;
    LONG FinishedAtReturn;
    LONG DrainedAtReturn;
    ULONG FlushCpu;
    ULONG ReturnCpu;
    KIRQL ReturnIrql;
    BOOLEAN ReturnSystemAffinity;
    BOOLEAN SystemAffinity;
    LONGLONG TimeoutTicks;
} FLUSH_CONTEXT;

static BOOLEAN
WaitForFlag(volatile LONG *Flag)
{
    LARGE_INTEGER Delay;
    ULONG Attempt;

    Delay.QuadPart = -10000;
    for (Attempt = 0; Attempt < 2000; Attempt++)
    {
        if (InterlockedCompareExchange(Flag, 0, 0)) return TRUE;
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
    return FALSE;
}

static VOID NTAPI
BlockingDpc(PKDPC Dpc, PVOID Parameter, PVOID Argument1, PVOID Argument2)
{
    FLUSH_CONTEXT *Context = Parameter;
    LONGLONG Deadline = KeQueryPerformanceCounter(NULL).QuadPart + Context->TimeoutTicks;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    InterlockedExchange(&Context->Started, 1);
    while (!InterlockedCompareExchange(&Context->Release, 0, 0))
    {
        if (KeQueryPerformanceCounter(NULL).QuadPart >= Deadline)
        {
            InterlockedExchange(&Context->TimedOut, 1);
            break;
        }
        YieldProcessor();
    }
    InterlockedExchange(&Context->Finished, 1);
}

static VOID NTAPI
BacklogDpc(PKDPC Dpc, PVOID Parameter, PVOID Argument1, PVOID Argument2)
{
    FLUSH_CONTEXT *Context = Parameter;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    InterlockedIncrement(&Context->Drained);
}

static VOID NTAPI
FlushThread(PVOID Parameter)
{
    FLUSH_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;

    PreviousAffinity = KeSetAffinityThread(KeGetCurrentThread(), (KAFFINITY)1 << Context->FlushCpu);
    if (Context->SystemAffinity)
        KeSetSystemAffinityThread((KAFFINITY)1 << Context->FlushCpu);
    KeSetPriorityThread(KeGetCurrentThread(), 15);
    KeSetEvent(&Context->Ready, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&Context->Go, Executive, KernelMode, FALSE, NULL);
    InterlockedExchange(&Context->FlushEntered, 1);
    KeFlushQueuedDpcs();
    Context->FinishedAtReturn = Context->Finished;
    Context->DrainedAtReturn = Context->Drained;
    Context->ReturnCpu = KeGetCurrentProcessorNumber();
    Context->ReturnIrql = KeGetCurrentIrql();
    Context->ReturnSystemAffinity = KeGetCurrentThread()->SystemAffinityActive;
    InterlockedExchange(&Context->FlushReturned, 1);
    if (Context->SystemAffinity) KeRevertToUserAffinityThread();
    KeSetAffinityThread(KeGetCurrentThread(), PreviousAffinity);
}

static VOID
TestFlush(ULONG TargetCpu, ULONG BacklogCount, BOOLEAN SystemAffinity)
{
    FLUSH_CONTEXT Context = {0};
    PKDPC Backlog = NULL;
    PKTHREAD Thread;
    LARGE_INTEGER Frequency, Delay;
    ULONG Index, Queued = 0;
    BOOLEAN Inserted, Started, Entered;
    LONG EarlyReturn;

    if (BacklogCount)
    {
        Backlog = ExAllocatePoolWithTag(NonPagedPool, BacklogCount * sizeof(*Backlog), 'tfDK');
        ok(Backlog != NULL, "DPC backlog allocation failed\n");
        if (!Backlog) return;
    }
    Context.FlushCpu = TargetCpu == 1 ? 2 : 1;
    Context.SystemAffinity = SystemAffinity;
    KeQueryPerformanceCounter(&Frequency);
    Context.TimeoutTicks = 2 * Frequency.QuadPart;
    KeInitializeEvent(&Context.Ready, NotificationEvent, FALSE);
    KeInitializeEvent(&Context.Go, NotificationEvent, FALSE);
    KeInitializeDpc(&Context.Blocker, BlockingDpc, &Context);
    KeSetTargetProcessorDpc(&Context.Blocker, (CCHAR)TargetCpu);
    KeSetImportanceDpc(&Context.Blocker, HighImportance);
    Thread = KmtStartThread(FlushThread, &Context);
    if (!Thread)
    {
        if (Backlog) ExFreePoolWithTag(Backlog, 'tfDK');
        return;
    }
    KeWaitForSingleObject(&Context.Ready, Executive, KernelMode, FALSE, NULL);
    Inserted = KeInsertQueueDpc(&Context.Blocker, NULL, NULL);
    Started = Inserted && WaitForFlag(&Context.Started);
    if (Started)
    {
        for (Index = 0; Index < BacklogCount; Index++)
        {
            if (Index & 1)
                KeInitializeThreadedDpc(&Backlog[Index], BacklogDpc, &Context);
            else
                KeInitializeDpc(&Backlog[Index], BacklogDpc, &Context);
            KeSetTargetProcessorDpc(&Backlog[Index], (CCHAR)TargetCpu);
            KeSetImportanceDpc(&Backlog[Index], (KDPC_IMPORTANCE)(Index % 3));
            if (KeInsertQueueDpc(&Backlog[Index], NULL, NULL)) Queued++;
        }
    }
    KeSetEvent(&Context.Go, IO_NO_INCREMENT, FALSE);
    Entered = WaitForFlag(&Context.FlushEntered);
    Delay.QuadPart = -20 * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    EarlyReturn = Context.FlushReturned;
    InterlockedExchange(&Context.Release, 1);
    KmtFinishThread(Thread, NULL);

    /* Cancel leftovers on a broken implementation, then wait out any callback. */
    KeRemoveQueueDpc(&Context.Blocker);
    for (Index = 0; Index < (Started ? BacklogCount : 0); Index++)
        KeRemoveQueueDpc(&Backlog[Index]);
    KeSetSystemAffinityThread((KAFFINITY)1 << TargetCpu);
    KeRevertToUserAffinityThread();

    ok_bool_true(Inserted, "blocker queued");
    ok_bool_true(Started, "blocker started");
    ok_bool_true(Entered, "flush entered");
    ok_eq_ulong(Queued, BacklogCount);
    ok_eq_long(Context.TimedOut, 0);
    ok_eq_long(EarlyReturn, 0);
    ok_eq_long(Context.FinishedAtReturn, 1);
    ok_eq_long(Context.DrainedAtReturn, (LONG)BacklogCount);
    ok_eq_uint(Context.ReturnIrql, PASSIVE_LEVEL);
    ok_eq_ulong(Context.ReturnCpu, Context.FlushCpu);
    ok_eq_uint(Context.ReturnSystemAffinity, SystemAffinity);
    trace("DPC_FLUSH cpu=%lu backlog=%lu system_affinity=%u early=%ld finished=%ld drained=%ld timed_out=%ld\n",
          TargetCpu, BacklogCount, SystemAffinity, EarlyReturn, Context.FinishedAtReturn,
          Context.DrainedAtReturn, Context.TimedOut);
    if (Backlog) ExFreePoolWithTag(Backlog, 'tfDK');
}

typedef struct _FLUSH_STRESS
{
    PKEVENT Go;
    ULONG Cpu;
    volatile LONG Calls;
    volatile LONG BadCpu;
    volatile LONG BadIrql;
    ULONG Rounds;
    ULONG Errors;
} FLUSH_STRESS;

static VOID NTAPI
StressDpc(PKDPC Dpc, PVOID Parameter, PVOID Argument1, PVOID Argument2)
{
    FLUSH_STRESS *Context = Parameter;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument2);
    if (KeGetCurrentProcessorNumber() != (ULONG)(ULONG_PTR)Argument1)
        InterlockedIncrement(&Context->BadCpu);
    if (KeGetCurrentIrql() != DISPATCH_LEVEL && KeGetCurrentIrql() != PASSIVE_LEVEL)
        InterlockedIncrement(&Context->BadIrql);
    InterlockedIncrement(&Context->Calls);
}

static VOID NTAPI
StressThread(PVOID Parameter)
{
    FLUSH_STRESS *Context = Parameter;
    PKDPC Dpcs;
    ULONG Cpu, Round, Count = KeNumberProcessors;
    KAFFINITY PreviousAffinity;
    KIRQL OldIrql;
    BOOLEAN SystemAffinity;

    Dpcs = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(*Dpcs), 'sfDK');
    if (!Dpcs)
    {
        Context->Errors++;
        return;
    }
    PreviousAffinity = KeSetAffinityThread(KeGetCurrentThread(), (KAFFINITY)1 << Context->Cpu);
    KeWaitForSingleObject(Context->Go, Executive, KernelMode, FALSE, NULL);
    for (Round = 0; Round < FLUSH_STRESS_ROUNDS; Round++)
    {
        Context->Calls = 0;
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        for (Cpu = 0; Cpu < Count; Cpu++)
        {
            if (Round & 1)
                KeInitializeThreadedDpc(&Dpcs[Cpu], StressDpc, Context);
            else
                KeInitializeDpc(&Dpcs[Cpu], StressDpc, Context);
            KeSetTargetProcessorDpc(&Dpcs[Cpu], (CCHAR)Cpu);
            KeSetImportanceDpc(&Dpcs[Cpu], (KDPC_IMPORTANCE)(Round % 3));
            if (!KeInsertQueueDpc(&Dpcs[Cpu], (PVOID)(ULONG_PTR)Cpu, NULL))
                Context->Errors++;
        }
        KeLowerIrql(OldIrql);
        SystemAffinity = (Round & 2) != 0;
        if (SystemAffinity) KeSetSystemAffinityThread((KAFFINITY)1 << Context->Cpu);
        KeFlushQueuedDpcs();
        if (Context->Calls != (LONG)Count || KeGetCurrentIrql() != PASSIVE_LEVEL ||
            KeGetCurrentProcessorNumber() != Context->Cpu ||
            KeGetCurrentThread()->SystemAffinityActive != SystemAffinity)
        {
            Context->Errors++;
        }
        if (SystemAffinity) KeRevertToUserAffinityThread();
        Context->Rounds++;
        if (Context->Errors) break;
    }
    for (Cpu = 0; Cpu < Count; Cpu++) KeRemoveQueueDpc(&Dpcs[Cpu]);
    for (Cpu = 0; Cpu < Count; Cpu++) KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
    KeRevertToUserAffinityThread();
    KeSetAffinityThread(KeGetCurrentThread(), PreviousAffinity);
    ExFreePoolWithTag(Dpcs, 'sfDK');
}

static VOID
TestConcurrentFlush(VOID)
{
    FLUSH_STRESS Workers[MAXIMUM_PROCESSORS] = {0};
    PKTHREAD Threads[MAXIMUM_PROCESSORS];
    KEVENT Go;
    ULONG Cpu, Created = 0;

    KeInitializeEvent(&Go, NotificationEvent, FALSE);
    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
    {
        Workers[Cpu].Go = &Go;
        Workers[Cpu].Cpu = Cpu;
        Threads[Cpu] = KmtStartThread(StressThread, &Workers[Cpu]);
        if (!Threads[Cpu]) break;
        Created++;
    }
    KeSetEvent(&Go, IO_NO_INCREMENT, FALSE);
    for (Cpu = 0; Cpu < Created; Cpu++)
    {
        KmtFinishThread(Threads[Cpu], NULL);
        ok_eq_ulong(Workers[Cpu].Rounds, FLUSH_STRESS_ROUNDS);
        ok_eq_ulong(Workers[Cpu].Errors, 0);
        ok_eq_long(Workers[Cpu].BadCpu, 0);
        ok_eq_long(Workers[Cpu].BadIrql, 0);
        trace("DPC_FLUSH_STRESS cpu=%lu rounds=%lu errors=%lu\n",
              Cpu, Workers[Cpu].Rounds, Workers[Cpu].Errors);
    }
}

START_TEST(KeArm64DpcFlush)
{
    KAFFINITY PreviousAffinity;
    ULONG Cpu;

    if (skip(KeNumberProcessors >= 3, "Three processors required\n")) return;
    PreviousAffinity = KeSetAffinityThread(KeGetCurrentThread(), 1);
    for (Cpu = 1; Cpu < (ULONG)KeNumberProcessors; Cpu++)
    {
        TestFlush(Cpu, 0, FALSE);
        TestFlush(Cpu, FLUSH_BACKLOG, FALSE);
        TestFlush(Cpu, 0, TRUE);
        TestFlush(Cpu, FLUSH_BACKLOG, TRUE);
    }
    KeSetAffinityThread(KeGetCurrentThread(), PreviousAffinity);
    TestConcurrentFlush();
}
