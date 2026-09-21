/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Remote DPC dispatch requests and importance ordering
 */

#include <kmt_test.h>

VOID Test_KeArm64DpcImportance(VOID);

typedef struct _IMPORTANCE_CONTEXT
{
    KDPC Dpcs[4];
    PKPRCB Prcb;
    ULONG Cpu;
    ULONG InitialCount;
    ULONG MaximumDepth;
    ULONG Calls;
    ULONG CpuErrors;
    ULONG IrqlErrors;
    ULONG Order[4];
    LONGLONG TimeoutTicks;
    volatile LONG Ready;
    volatile LONG Release;
    BOOLEAN Clean;
    BOOLEAN TimedOut;
} IMPORTANCE_CONTEXT;

static VOID NTAPI
ImportanceDpc(PKDPC Dpc, PVOID Parameter, PVOID Argument1, PVOID Argument2)
{
    IMPORTANCE_CONTEXT *Context = Parameter;
    ULONG Index = Context->Calls++;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument2);
    if (Index < RTL_NUMBER_OF(Context->Order))
        Context->Order[Index] = (ULONG)(ULONG_PTR)Argument1;
    if (KeGetCurrentProcessorNumber() != Context->Cpu) Context->CpuErrors++;
    if (KeGetCurrentIrql() != DISPATCH_LEVEL) Context->IrqlErrors++;
}

static VOID NTAPI
HoldTarget(PVOID Parameter)
{
    IMPORTANCE_CONTEXT *Context = Parameter;
    KAFFINITY PreviousAffinity;
    PKDPC_DATA Data;
    KIRQL OldIrql;
    LONGLONG Deadline;

    PreviousAffinity = KeSetSystemAffinityThreadEx((KAFFINITY)1 << Context->Cpu);
    KeFlushQueuedDpcs();
    /* An unrelated IPI must not set the dispatch-request flag during the check. */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    Context->Prcb = KeGetCurrentPrcb();
    Data = &Context->Prcb->DpcData[0];
    KeAcquireSpinLockAtDpcLevel(&Data->DpcLock);
    Context->Clean = !Data->DpcQueueDepth &&
                     !Context->Prcb->DpcRoutineActive &&
                     !Context->Prcb->DpcNormalProcessingRequested &&
                     !Context->Prcb->DpcNormalDpcPresent;
    Context->InitialCount = Data->DpcCount;
    Context->MaximumDepth = Context->Prcb->MaximumDpcQueueDepth;
    KeReleaseSpinLockFromDpcLevel(&Data->DpcLock);
    Deadline = KeQueryPerformanceCounter(NULL).QuadPart + Context->TimeoutTicks;
    InterlockedExchange(&Context->Ready, 1);
    while (Context->Clean && !Context->Release)
    {
        if (KeQueryPerformanceCounter(NULL).QuadPart >= Deadline)
        {
            Context->TimedOut = TRUE;
            break;
        }
        YieldProcessor();
    }
    KeLowerIrql(OldIrql);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}

static VOID
CheckImportance(ULONG Cpu, KDPC_IMPORTANCE Importance, BOOLEAN CheckOrder)
{
    IMPORTANCE_CONTEXT Context;
    LARGE_INTEGER Frequency;
    PKTHREAD Thread;
    PKDPC_DATA Data;
    KIRQL OldIrql;
    LONGLONG Deadline;
    ULONG Attempt, Index, Queued = 0, Count = CheckOrder ? 4 : 1;
    ULONG DpcSummary = 0, IpiSummary = 0;
    BOOLEAN Accepted = FALSE, Requested = FALSE, Present = FALSE, IpiRequested = FALSE, Expected;
    static const KDPC_IMPORTANCE OrderImportance[] = {
        LowImportance, MediumHighImportance, MediumImportance, HighImportance
    };
    static const ULONG ExpectedOrder[] = {3, 0, 1, 2};

    KeQueryPerformanceCounter(&Frequency);
    for (Attempt = 0; Attempt < 32 && !Accepted; Attempt++)
    {
        RtlZeroMemory(&Context, sizeof(Context));
        Context.Cpu = Cpu;
        Context.TimeoutTicks = Frequency.QuadPart;
        for (Index = 0; Index < Count; Index++)
        {
            KeInitializeDpc(&Context.Dpcs[Index], ImportanceDpc, &Context);
            KeSetTargetProcessorDpc(&Context.Dpcs[Index], (CCHAR)Cpu);
            KeSetImportanceDpc(&Context.Dpcs[Index], CheckOrder ? OrderImportance[Index] : Importance);
        }
        Thread = KmtStartThread(HoldTarget, &Context);
        if (!Thread) return;
        Deadline = KeQueryPerformanceCounter(NULL).QuadPart + 2 * Frequency.QuadPart;
        while (!Context.Ready && KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
            YieldProcessor();
        KeMemoryBarrier();
        Queued = 0;
        if (Context.Ready && Context.Clean && (CheckOrder || Context.MaximumDepth > Count))
        {
            KeRaiseIrql(HIGH_LEVEL, &OldIrql);
            for (Index = 0; Index < Count; Index++)
            {
                if (KeInsertQueueDpc(&Context.Dpcs[Index], (PVOID)(ULONG_PTR)Index, NULL))
                    Queued++;
            }
            Data = &Context.Prcb->DpcData[0];
            KeAcquireSpinLockAtDpcLevel(&Data->DpcLock);
            Requested = (BOOLEAN)Context.Prcb->DpcNormalProcessingRequested;
            Present = (BOOLEAN)Context.Prcb->DpcNormalDpcPresent;
            DpcSummary = (ULONG)Context.Prcb->DpcRequestSummary;
            IpiSummary = (ULONG)Context.Prcb->RequestSummary;
            IpiRequested = (IpiSummary & IPI_DPC) != 0;
            Accepted = Data->DpcCount == Context.InitialCount + Count &&
                       Data->DpcQueueDepth == (LONG)Count;
            KeReleaseSpinLockFromDpcLevel(&Data->DpcLock);
            InterlockedExchange(&Context.Release, 1);
            KeLowerIrql(OldIrql);
        }
        else
        {
            InterlockedExchange(&Context.Release, 1);
        }
        KmtFinishThread(Thread, NULL);
        KeFlushQueuedDpcs();
        ok(!Context.TimedOut, "CPU %lu target hold timed out\n", Cpu);
    }

    ok(Accepted, "CPU %lu importance %u order %u: no isolated queue after %lu attempts\n",
       Cpu, Importance, CheckOrder, Attempt);
    if (!Accepted) return;
    ok_eq_ulong(Queued, Count);
    ok_eq_ulong(Context.Calls, Count);
    ok_eq_ulong(Context.CpuErrors, 0);
    ok_eq_ulong(Context.IrqlErrors, 0);
    if (CheckOrder)
    {
        for (Index = 0; Index < Count; Index++)
            ok_eq_ulong(Context.Order[Index], ExpectedOrder[Index]);
        ok_eq_bool(Requested, TRUE);
        ok_eq_bool(Present, TRUE);
        ok_eq_bool(IpiRequested, TRUE);
    }
    else
    {
        Expected = (Importance == HighImportance || Importance == MediumHighImportance);
        ok(Requested == Expected && Present == !Expected && IpiRequested == Expected,
           "CPU %lu importance %u: requested = %u, present = %u, ipi = %u, queue depth = %lu, threshold = %lu\n",
           Cpu, Importance, Requested, Present, IpiRequested, Count, Context.MaximumDepth);
    }
    ok_eq_ulong(Context.MaximumDepth, 4UL);
    trace("DPC_IMPORTANCE cpu=%lu importance=%u order=%u requested=%u dpcsummary=0x%lx ipisummary=0x%lx calls=%lu threshold=%lu attempts=%lu\n",
          Cpu, Importance, CheckOrder, Requested, DpcSummary, IpiSummary, Context.Calls, Context.MaximumDepth, Attempt);
}

START_TEST(KeArm64DpcImportance)
{
    KAFFINITY PreviousAffinity;
    ULONG Cpu, Index;
    static const KDPC_IMPORTANCE Importance[] = {
        LowImportance, MediumImportance, MediumHighImportance, HighImportance
    };

    if (skip(KeNumberProcessors >= 2, "Two processors required\n")) return;
    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    for (Cpu = 1; Cpu < (ULONG)KeNumberProcessors; Cpu++)
    {
        for (Index = 0; Index < RTL_NUMBER_OF(Importance); Index++)
            CheckImportance(Cpu, Importance[Index], FALSE);
        CheckImportance(Cpu, MediumHighImportance, TRUE);
    }
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}
