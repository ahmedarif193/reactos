/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Remote kernel APC delivery during execution, migration and waits
 */

#include <kmt_test.h>

VOID Test_KeArm64Apc(VOID);
NTSYSAPI BOOLEAN NTAPI KeRemoveQueueApc(PKAPC Apc);

#define APC_ROUNDS 32
#define APC_MODES 8

typedef struct _APC_TEST_CONTEXT APC_TEST_CONTEXT;

typedef struct _APC_TEST_SLOT
{
    KAPC Apc;
    APC_TEST_CONTEXT *Context;
    ULONG Kind;
    ULONG Round;
    volatile LONG KernelCalls;
    volatile LONG NormalCalls;
    LONG Order;
} APC_TEST_SLOT;

typedef struct _APC_CANCEL
{
    KDPC Dpc;
    KEVENT Done;
    PKAPC Apc;
    LONGLONG Frequency;
    volatile LONG Ready;
    volatile LONG Go;
    BOOLEAN Removed;
    ULONG Calls;
    ULONG Cpu;
    ULONG CpuErrors;
    ULONG Timeouts;
    ULONG IrqlErrors;
} APC_CANCEL;

struct _APC_TEST_CONTEXT
{
    KEVENT Go;
    KEVENT Ready;
    KEVENT Observed;
    KEVENT RoundDone;
    KEVENT Wake;
    KEVENT SpecialDone;
    KEVENT NormalDone;
    APC_TEST_SLOT Slots[2];
    APC_CANCEL Cancel;
    PKTHREAD Thread;
    ULONG Cpu;
    ULONG Mode;
    KAFFINITY AllowedCpus;
    volatile LONG64 SeenCpus;
    LONGLONG Frequency;
    volatile LONG Stop;
    volatile LONG Release;
    volatile LONG Inspect;
    volatile LONG LastCpu;
    volatile LONG Guard;
    volatile LONG Sequence;
    volatile LONG KernelCalls;
    volatile LONG NormalCalls;
    volatile LONG Rundowns;
    volatile LONG OwnerErrors;
    volatile LONG CpuErrors;
    volatile LONG IrqlErrors;
    volatile LONG ArgumentErrors;
    volatile LONG BlockedErrors;
    volatile LONG OrderErrors;
    volatile LONG Timeouts;
    volatile LONG WaitErrors;
};

static BOOLEAN
WaitApcEvent(PKEVENT Event)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10000000;
    return KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout) == STATUS_SUCCESS;
}

static VOID NTAPI
CancelApcDpc(PKDPC Dpc, PVOID Parameter, PVOID Argument1, PVOID Argument2)
{
    APC_CANCEL *Cancel = Parameter;
    LONGLONG Deadline = KeQueryPerformanceCounter(NULL).QuadPart + Cancel->Frequency;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    if (KeGetCurrentIrql() != DISPATCH_LEVEL) Cancel->IrqlErrors++;
    if (KeGetCurrentProcessorNumber() != Cancel->Cpu) Cancel->CpuErrors++;
    InterlockedExchange(&Cancel->Ready, 1);
    while (!Cancel->Go && KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
        YieldProcessor();
    if (Cancel->Go)
    {
        Cancel->Removed = KeRemoveQueueApc(Cancel->Apc);
        Cancel->Calls++;
    }
    else
    {
        Cancel->Timeouts++;
    }
    KeSetEvent(&Cancel->Done, IO_NO_INCREMENT, FALSE);
}

static BOOLEAN
WaitApcCancelReady(APC_CANCEL *Cancel)
{
    LONGLONG Deadline = KeQueryPerformanceCounter(NULL).QuadPart + Cancel->Frequency;

    while (!Cancel->Ready && KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
        YieldProcessor();
    return Cancel->Ready != 0;
}

static VOID
CheckApcCallback(APC_TEST_SLOT *Slot, KIRQL Irql, PVOID Argument1, PVOID Argument2)
{
    APC_TEST_CONTEXT *Context = Slot->Context;
    ULONG Cpu = KeGetCurrentProcessorNumber();

    if (KeGetCurrentThread() != Context->Thread)
        InterlockedIncrement(&Context->OwnerErrors);
    if (!(Context->AllowedCpus & ((KAFFINITY)1 << Cpu)))
        InterlockedIncrement(&Context->CpuErrors);
    InterlockedOr64(&Context->SeenCpus, (LONG64)1 << Cpu);
    if (KeGetCurrentIrql() != Irql || !KmtAreInterruptsEnabled())
        InterlockedIncrement(&Context->IrqlErrors);
    if (Argument1 != (PVOID)(ULONG_PTR)(Slot->Round + 1) ||
        Argument2 != (PVOID)(ULONG_PTR)(0xa900 + Slot->Kind))
        InterlockedIncrement(&Context->ArgumentErrors);
    if (Context->Guard > 1 || (Context->Guard && Slot->Kind != 0))
        InterlockedIncrement(&Context->BlockedErrors);
}

static VOID NTAPI
ApcNormalRoutine(PVOID Parameter, PVOID Argument1, PVOID Argument2)
{
    APC_TEST_SLOT *Slot = Parameter;
    APC_TEST_CONTEXT *Context = Slot->Context;

    CheckApcCallback(Slot, PASSIVE_LEVEL, Argument1, Argument2);
    if (Slot->KernelCalls != 1 || KeAreApcsDisabled())
        InterlockedIncrement(&Context->OrderErrors);
    InterlockedIncrement(&Slot->NormalCalls);
    InterlockedIncrement(&Context->NormalCalls);
    KeSetEvent(&Context->NormalDone, IO_NO_INCREMENT, FALSE);
}

static VOID NTAPI
ApcKernelRoutine(PKAPC Apc, PKNORMAL_ROUTINE *NormalRoutine, PVOID *NormalContext,
                 PVOID *Argument1, PVOID *Argument2)
{
    APC_TEST_SLOT *Slot = CONTAINING_RECORD(Apc, APC_TEST_SLOT, Apc);
    APC_TEST_CONTEXT *Context = Slot->Context;

    CheckApcCallback(Slot, APC_LEVEL, *Argument1, *Argument2);
    if (*NormalRoutine != (Slot->Kind ? ApcNormalRoutine : NULL) ||
        (Slot->Kind && *NormalContext != Slot))
        InterlockedIncrement(&Context->ArgumentErrors);
    Slot->Order = InterlockedIncrement(&Context->Sequence);
    InterlockedIncrement(&Slot->KernelCalls);
    InterlockedIncrement(&Context->KernelCalls);
    if (Slot->Kind == 0)
        KeSetEvent(&Context->SpecialDone, IO_NO_INCREMENT, FALSE);
}

static VOID NTAPI
ApcRundownRoutine(PKAPC Apc)
{
    APC_TEST_SLOT *Slot = CONTAINING_RECORD(Apc, APC_TEST_SLOT, Apc);

    InterlockedIncrement(&Slot->Context->Rundowns);
}

static VOID NTAPI
ApcWorker(PVOID Parameter)
{
    APC_TEST_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    KIRQL OldIrql = PASSIVE_LEVEL;
    LARGE_INTEGER Timeout;
    LONGLONG Deadline;
    BOOLEAN Observed;
    NTSTATUS Status;

    Context->Thread = KeGetCurrentThread();
    PreviousAffinity = KeSetAffinityThread(Context->Thread, (KAFFINITY)1 << Context->Cpu);
    KeSetPriorityThread(Context->Thread, 17);
    /* System-thread startup may supply the initial critical region. */
    if (KeAreApcsDisabled() && !KeAreAllApcsDisabled())
        KeLeaveCriticalRegion();
    if (KeAreApcsDisabled())
    {
        InterlockedIncrement(&Context->BlockedErrors);
        KeSetEvent(&Context->Ready, IO_NO_INCREMENT, FALSE);
        KeSetAffinityThread(Context->Thread, PreviousAffinity);
        return;
    }

    for (;;)
    {
        if (!WaitApcEvent(&Context->Go))
        {
            InterlockedIncrement(&Context->Timeouts);
            break;
        }
        if (Context->Stop) break;
        if (Context->Mode == 2)
        {
            Context->Guard = 1;
            KeEnterCriticalRegion();
        }
        else if (Context->Mode == 3 || Context->Mode >= 6)
        {
            Context->Guard = 2;
            KeEnterGuardedRegion();
        }
        else if (Context->Mode == 4)
        {
            Context->Guard = 2;
            KeRaiseIrql(APC_LEVEL, &OldIrql);
        }
        Context->LastCpu = KeGetCurrentProcessorNumber();
        KeSetEvent(&Context->Ready, IO_NO_INCREMENT, FALSE);
        if (Context->Mode == 5)
        {
            Timeout.QuadPart = -10000000;
            Status = KeWaitForSingleObject(&Context->Wake, Executive, KernelMode, FALSE, &Timeout);
            if (Status != STATUS_SUCCESS)
                InterlockedIncrement(&Context->WaitErrors);
        }
        else
        {
            Observed = FALSE;
            Deadline = KeQueryPerformanceCounter(NULL).QuadPart + Context->Frequency;
            while (!Context->Release && !Context->Stop &&
                   KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
            {
                Context->LastCpu = KeGetCurrentProcessorNumber();
                if (Context->Inspect && !Observed)
                {
                    if (Context->Slots[1].KernelCalls != 0 ||
                        Context->Slots[1].NormalCalls != 0 ||
                        (Context->Mode != 2 && Context->Slots[0].KernelCalls != 0))
                        InterlockedIncrement(&Context->BlockedErrors);
                    Observed = TRUE;
                    KeSetEvent(&Context->Observed, IO_NO_INCREMENT, FALSE);
                }
                YieldProcessor();
            }
            if (!Context->Release && !Context->Stop)
                InterlockedIncrement(&Context->Timeouts);
        }
        Context->Guard = 0;
        if (Context->Mode == 2) KeLeaveCriticalRegion();
        else if (Context->Mode == 3 || Context->Mode >= 6) KeLeaveGuardedRegion();
        else if (Context->Mode == 4) KeLowerIrql(OldIrql);
        KeSetEvent(&Context->RoundDone, IO_NO_INCREMENT, FALSE);
        if (Context->Stop) break;
    }
    KeSetAffinityThread(Context->Thread, PreviousAffinity);
}

static BOOLEAN
WaitApcState(APC_TEST_CONTEXT *Context, PKTHREAD Thread, ULONG Cpu, BOOLEAN WaitForBlock)
{
    LONGLONG Deadline = KeQueryPerformanceCounter(NULL).QuadPart + Context->Frequency;

    do
    {
        if (WaitForBlock ? Thread->State == Waiting : Context->LastCpu == (LONG)Cpu)
            return TRUE;
        YieldProcessor();
    } while (KeQueryPerformanceCounter(NULL).QuadPart < Deadline);
    InterlockedIncrement(&Context->Timeouts);
    return FALSE;
}

static VOID
CheckRemoteApc(ULONG Cpu, ULONG Mode)
{
    APC_TEST_CONTEXT Context = {0};
    PKTHREAD Thread;
    LARGE_INTEGER Frequency;
    ULONG Round, Index, Completed = 0, InsertErrors = 0, CountErrors = 0;
    ULONG CancelErrors = 0, Cancelled = 0, Delivered = 0, CancelIndex;
    ULONG ControllerWins = 0, DpcWins = 0;
    LONG ExpectedKernel = 0, ExpectedNormal = 0;
    ULONG CurrentCpu = Cpu, NextCpu = Cpu;
    ULONG Controller = (Cpu + 1) % (ULONG)KeNumberProcessors;
    BOOLEAN Initialized = FALSE;
    BOOLEAN Removed[2], ControllerRemoved;

    KeSetSystemAffinityThread((KAFFINITY)1 << Controller);
    Context.Cpu = Cpu;
    Context.Mode = Mode;
    KeQueryPerformanceCounter(&Frequency);
    Context.Frequency = Frequency.QuadPart;
    KeInitializeEvent(&Context.Go, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.Ready, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.Observed, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.RoundDone, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.Wake, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.SpecialDone, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.NormalDone, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Context.Cancel.Done, SynchronizationEvent, FALSE);
    Context.Cancel.Frequency = Frequency.QuadPart;
    Context.Cancel.Cpu = (Cpu + 2) % (ULONG)KeNumberProcessors;
    KeInitializeDpc(&Context.Cancel.Dpc, CancelApcDpc, &Context.Cancel);
    KeSetTargetProcessorDpc(&Context.Cancel.Dpc, (CCHAR)Context.Cancel.Cpu);
    KeSetImportanceDpc(&Context.Cancel.Dpc, HighImportance);
    Thread = KmtStartThread(ApcWorker, &Context);
    if (!Thread) return;

    for (Round = 0; Round < APC_ROUNDS; Round++)
    {
        Context.Release = 0;
        Context.Inspect = 0;
        Context.Sequence = 0;
        Removed[0] = Removed[1] = FALSE;
        KeClearEvent(&Context.SpecialDone);
        KeClearEvent(&Context.NormalDone);
        Context.AllowedCpus = (KAFFINITY)1 << CurrentCpu;
        if (Mode == 1)
        {
            NextCpu = (CurrentCpu + 1) % (ULONG)KeNumberProcessors;
            if (NextCpu == Controller) NextCpu = (NextCpu + 1) % (ULONG)KeNumberProcessors;
            Context.AllowedCpus |= (KAFFINITY)1 << NextCpu;
        }
        for (Index = 0; Index < 2; Index++)
        {
            APC_TEST_SLOT *Slot = &Context.Slots[Index];
            Slot->Context = &Context;
            Slot->Kind = Index;
            Slot->Round = Round;
            Slot->KernelCalls = Slot->NormalCalls = 0;
            Slot->Order = 0;
            KeInitializeApc(&Slot->Apc, Thread, OriginalApcEnvironment, ApcKernelRoutine,
                            ApcRundownRoutine, Index ? ApcNormalRoutine : NULL, KernelMode, Slot);
        }
        Initialized = TRUE;
        KeSetEvent(&Context.Go, IO_NO_INCREMENT, FALSE);
        if (!WaitApcEvent(&Context.Ready)) goto TimedOut;
        if (Mode == 5 && !WaitApcState(&Context, Thread, 0, TRUE)) break;
        if (Mode == 1 && !(Round & 1))
            KeSetAffinityThread(Thread, (KAFFINITY)1 << NextCpu);
        /* Queue normal first so blocked cases also check special-APC ordering. */
        for (Index = 2; Index-- != 0;)
        {
            if (!KeInsertQueueApc(&Context.Slots[Index].Apc,
                                 (PVOID)(ULONG_PTR)(Round + 1),
                                 (PVOID)(ULONG_PTR)(0xa900 + Index), IO_NO_INCREMENT))
                InsertErrors++;
        }
        if (Mode >= 6)
        {
            CancelIndex = Round & 1;
            Context.Cancel.Apc = &Context.Slots[CancelIndex].Apc;
            Context.Cancel.Ready = Context.Cancel.Go = 0;
            Context.Cancel.Removed = FALSE;
            if (!KeInsertQueueDpc(&Context.Cancel.Dpc, NULL, NULL))
            {
                CancelErrors++;
                break;
            }
            if (!WaitApcCancelReady(&Context.Cancel)) goto TimedOut;
            if (Mode == 7 && !(Round & 2))
            {
                InterlockedExchange(&Context.Release, 1);
                KeStallExecutionProcessor(5);
            }
            InterlockedExchange(&Context.Cancel.Go, 1);
            if (Mode == 7) InterlockedExchange(&Context.Release, 1);
            ControllerRemoved = KeRemoveQueueApc(Context.Cancel.Apc);
            if (!WaitApcEvent(&Context.Cancel.Done)) goto TimedOut;
            if (ControllerRemoved && Context.Cancel.Removed) CancelErrors++;
            Removed[CancelIndex] = ControllerRemoved || Context.Cancel.Removed;
            if (ControllerRemoved) ControllerWins++;
            if (Context.Cancel.Removed) DpcWins++;
            if (Removed[CancelIndex]) Cancelled++;
            else Delivered++;
            if (Mode == 6 && !Removed[CancelIndex]) CancelErrors++;
            if (KeRemoveQueueApc(Context.Cancel.Apc)) CancelErrors++;
        }
        if (Mode == 1)
        {
            if (Round & 1) KeSetAffinityThread(Thread, (KAFFINITY)1 << NextCpu);
            if (!WaitApcState(&Context, Thread, NextCpu, FALSE)) break;
            CurrentCpu = NextCpu;
        }
        if (Mode == 2 && !WaitApcEvent(&Context.SpecialDone)) goto TimedOut;
        if (Mode >= 2 && Mode <= 4)
        {
            InterlockedExchange(&Context.Inspect, 1);
            if (!WaitApcEvent(&Context.Observed)) goto TimedOut;
        }
        if (Mode == 0 || Mode == 1 || Mode == 5)
        {
            if (!WaitApcEvent(&Context.SpecialDone) || !WaitApcEvent(&Context.NormalDone)) goto TimedOut;
            if (Mode == 5 && KeReadStateEvent(&Context.RoundDone))
                InterlockedIncrement(&Context.WaitErrors);
        }
        InterlockedExchange(&Context.Release, 1);
        if (Mode == 5) KeSetEvent(&Context.Wake, IO_NO_INCREMENT, FALSE);
        if (Mode == 2 && !WaitApcEvent(&Context.NormalDone)) goto TimedOut;
        if (Mode == 3 || Mode == 4)
        {
            if (!WaitApcEvent(&Context.SpecialDone) || !WaitApcEvent(&Context.NormalDone)) goto TimedOut;
        }
        if (Mode >= 6)
        {
            if (!Removed[0] && !WaitApcEvent(&Context.SpecialDone)) goto TimedOut;
            if (!Removed[1] && !WaitApcEvent(&Context.NormalDone)) goto TimedOut;
        }
        if (!WaitApcEvent(&Context.RoundDone)) goto TimedOut;
        if (Context.Slots[0].KernelCalls != !Removed[0] || Context.Slots[0].NormalCalls != 0 ||
            Context.Slots[1].KernelCalls != !Removed[1] || Context.Slots[1].NormalCalls != !Removed[1])
            CountErrors++;
        ExpectedKernel += !Removed[0] + !Removed[1];
        ExpectedNormal += !Removed[1];
        if (Mode >= 2 && Mode <= 4 && Context.Slots[0].Order >= Context.Slots[1].Order)
            InterlockedIncrement(&Context.OrderErrors);
        Completed++;
        continue;
TimedOut:
        InterlockedIncrement(&Context.Timeouts);
        break;
    }

    InterlockedExchange(&Context.Stop, 1);
    InterlockedExchange(&Context.Release, 1);
    InterlockedExchange(&Context.Cancel.Go, 1);
    KeSetEvent(&Context.Wake, IO_NO_INCREMENT, FALSE);
    if (Initialized)
        for (Index = 0; Index < 2; Index++) KeRemoveQueueApc(&Context.Slots[Index].Apc);
    if (Mode >= 6) KeFlushQueuedDpcs();
    KmtFinishThread(Thread, &Context.Go);
    ok_eq_ulong(Completed, APC_ROUNDS);
    ok_eq_ulong(InsertErrors, 0);
    ok_eq_ulong(CountErrors, 0);
    ok_eq_long(Context.KernelCalls, ExpectedKernel);
    ok_eq_long(Context.NormalCalls, ExpectedNormal);
    ok_eq_long(Context.Rundowns, 0);
    ok_eq_long(Context.OwnerErrors, 0);
    ok_eq_long(Context.CpuErrors, 0);
    ok_eq_long(Context.IrqlErrors, 0);
    ok_eq_long(Context.ArgumentErrors, 0);
    ok_eq_long(Context.BlockedErrors, 0);
    ok_eq_long(Context.OrderErrors, 0);
    ok_eq_long(Context.Timeouts, 0);
    ok_eq_long(Context.WaitErrors, 0);
    if (Mode >= 6)
    {
        ok_eq_ulong(CancelErrors, 0);
        ok_eq_ulong(Context.Cancel.Calls, APC_ROUNDS);
        ok_eq_ulong(Context.Cancel.Timeouts, 0);
        ok_eq_ulong(Context.Cancel.IrqlErrors, 0);
        ok_eq_ulong(Context.Cancel.CpuErrors, 0);
        ok_eq_ulong(ControllerWins + DpcWins, Cancelled);
        ok_eq_ulong(Cancelled + Delivered, APC_ROUNDS);
        trace("APC_CANCEL cpu=%lu mode=%lu rounds=%lu cancelled=%lu delivered=%lu controller_wins=%lu dpc_wins=%lu calls=%lu errors=%lu timeouts=%lu irql_errors=%lu cpu_errors=%lu\n",
              Cpu, Mode, Completed, Cancelled, Delivered, ControllerWins, DpcWins,
              Context.Cancel.Calls, CancelErrors, Context.Cancel.Timeouts, Context.Cancel.IrqlErrors,
              Context.Cancel.CpuErrors);
    }
    trace("APC_DELIVERY controller=%lu cpu=%lu mode=%lu rounds=%lu kernel=%ld normal=%ld cpus=0x%I64x insert_errors=%lu count_errors=%lu\n",
          Controller, Cpu, Mode, Completed, Context.KernelCalls, Context.NormalCalls,
          Context.SeenCpus, InsertErrors, CountErrors);
    trace("APC_STATE cpu=%lu mode=%lu owner_errors=%ld cpu_errors=%ld irql_errors=%ld argument_errors=%ld blocked_errors=%ld order_errors=%ld timeouts=%ld wait_errors=%ld rundown=%ld\n",
          Cpu, Mode, Context.OwnerErrors, Context.CpuErrors, Context.IrqlErrors,
          Context.ArgumentErrors, Context.BlockedErrors, Context.OrderErrors,
          Context.Timeouts, Context.WaitErrors, Context.Rundowns);
}

START_TEST(KeArm64Apc)
{
    KAFFINITY PreviousAffinity;
    KPRIORITY PreviousPriority;
    ULONG Cpu, Mode;

    if (skip(KeNumberProcessors >= 3, "Three processors required\n")) return;
    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    PreviousPriority = KeSetPriorityThread(KeGetCurrentThread(), 18);
    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
        for (Mode = 0; Mode < APC_MODES; Mode++) CheckRemoteApc(Cpu, Mode);
    KeSetPriorityThread(KeGetCurrentThread(), PreviousPriority);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}
