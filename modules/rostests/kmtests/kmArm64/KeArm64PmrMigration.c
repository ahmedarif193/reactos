/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     IRQL and GIC priority-mask reconciliation during remote migration
 */

#include <kmt_test.h>

VOID Test_KeArm64PmrMigration(VOID);

typedef ULONG (FASTCALL *PGET_PMR)(VOID);
typedef VOID (FASTCALL *PREQUEST_SOFTWARE_INTERRUPT)(KIRQL Level);

#define MIGRATION_ROUNDS 2048
#define MIGRATION_PRIORITY_ROUNDS 256

typedef struct _MIGRATION_TIMES
{
    LONGLONG Ticks[3];
} MIGRATION_TIMES;

typedef struct _PMR_MIGRATION
{
    KEVENT Ready;
    KEVENT Go;
    KEVENT Ack;
    PGET_PMR GetPmr;
    PREQUEST_SOFTWARE_INTERRUPT RequestInterrupt;
    ULONG Baseline[MAXIMUM_PROCESSORS];
    volatile LONG Stop;
    volatile LONG LastCpu;
    volatile LONG Request;
    volatile LONG Acknowledged;
    ULONG RequestedCpu;
    BOOLEAN SignalAck;
    LONGLONG Received;
    KPRIORITY ReceivedPriority;
    ULONG Transitions;
    ULONG Migrations;
    ULONG BadIrql;
    ULONG BadMask;
    ULONG BadDaif;
    KAFFINITY SeenCpus;
} PMR_MIGRATION;

static VOID NTAPI
MigrationThread(PVOID Parameter)
{
    PMR_MIGRATION *Context = Parameter;
    KAFFINITY PreviousAffinity;
    KIRQL OldIrql, ObservedIrql;
    ULONG Cpu, Pmr, PreviousCpu = MAXULONG;
    ULONG64 Daif;
    LONG Request, Acknowledged = 0;
    LONGLONG Received;
    KPRIORITY Priority;
    static const KIRQL Levels[] = {0, 1, 2, 3, 12, 13, 14, 15};

    PreviousAffinity = KeSetAffinityThread(KeGetCurrentThread(), 2);
    KeSetEvent(&Context->Ready, IO_NO_INCREMENT, FALSE);
    KeWaitForSingleObject(&Context->Go, Executive, KernelMode, FALSE, NULL);
    while (!Context->Stop)
    {
        KeRaiseIrql(Levels[Context->Transitions % RTL_NUMBER_OF(Levels)], &OldIrql);
        if ((Context->Transitions & 127) == 127)
            Context->RequestInterrupt(DISPATCH_LEVEL);
        KeLowerIrql(OldIrql);

        __asm__ __volatile__("mrs %0, daif\n\tmsr daifset, #3" : "=r"(Daif) :: "memory");
        Cpu = KeGetCurrentProcessorNumber();
        ObservedIrql = KeGetCurrentIrql();
        Pmr = Context->GetPmr();
        Request = Context->Request;
        KeMemoryBarrier();
        Received = 0;
        Priority = 0;
        if (Request != Acknowledged && Cpu == Context->RequestedCpu)
        {
            Received = KeQueryPerformanceCounter(NULL).QuadPart;
            Priority = KeQueryPriorityThread(KeGetCurrentThread());
        }
        __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
        if (OldIrql != PASSIVE_LEVEL || ObservedIrql != PASSIVE_LEVEL) Context->BadIrql++;
        if (Cpu >= (ULONG)KeNumberProcessors || Pmr != Context->Baseline[Cpu]) Context->BadMask++;
        if (Daif & 0x80) Context->BadDaif++;
        Context->SeenCpus |= (KAFFINITY)1 << Cpu;
        if (Cpu != PreviousCpu)
        {
            Context->Migrations++;
            PreviousCpu = Cpu;
        }
        Context->Transitions++;
        InterlockedExchange(&Context->LastCpu, (LONG)Cpu);
        if (Received != 0)
        {
            Context->Received = Received;
            Context->ReceivedPriority = Priority;
            Acknowledged = Request;
            InterlockedExchange(&Context->Acknowledged, Request);
            if (Context->SignalAck)
                KeSetEvent(&Context->Ack, IO_NO_INCREMENT, FALSE);
        }
    }
    KeSetAffinityThread(KeGetCurrentThread(), PreviousAffinity);
}

static VOID
ReportMigrationTimes(ULONG Mode, MIGRATION_TIMES *Times, ULONG Count, LONGLONG Frequency)
{
    LONGLONG Sum[3] = {0}, Value;
    ULONG Column, Round, Index;
    static const PCSTR Names[] = {"call", "arrival", "ack"};

    if (!Count) return;
    for (Column = 0; Column < RTL_NUMBER_OF(Names); Column++)
    {
        for (Round = 0; Round < Count; Round++)
            Sum[Column] += Times[Round].Ticks[Column];
        for (Round = 1; Round < Count; Round++)
        {
            Value = Times[Round].Ticks[Column];
            Index = Round;
            while (Index && Times[Index - 1].Ticks[Column] > Value)
            {
                Times[Index].Ticks[Column] = Times[Index - 1].Ticks[Column];
                Index--;
            }
            Times[Index].Ticks[Column] = Value;
        }
        trace("MIGRATION_TIME mode=%lu phase=%s samples=%lu sum_us=%I64u p50_us=%I64u p95_us=%I64u p99_us=%I64u max_us=%I64u\n",
              Mode, Names[Column], Count, Sum[Column] * 1000000 / Frequency,
              Times[(Count - 1) * 50 / 100].Ticks[Column] * 1000000 / Frequency,
              Times[(Count - 1) * 95 / 100].Ticks[Column] * 1000000 / Frequency,
              Times[(Count - 1) * 99 / 100].Ticks[Column] * 1000000 / Frequency,
              Times[Count - 1].Ticks[Column] * 1000000 / Frequency);
    }
}

static VOID
CheckMigration(ULONG Mode, ULONG Rounds, PMR_MIGRATION *Baseline)
{
    PMR_MIGRATION Context = {0};
    PKTHREAD Thread;
    PKTHREAD Controller = KeGetCurrentThread();
    KAFFINITY ExpectedCpus;
    LARGE_INTEGER Frequency, Timeout;
    LONGLONG Deadline, Start, Returned, End;
    ULONG Cpu, Round, Completed = 0, TimingErrors = 0, PriorityErrors = 0;
    ULONG BeforeSwitches, CallSwitches = 0, WaitSwitches = 0;
    ULONG BeforeWait, LateArrivals = 0, LateAcks = 0;
    KPRIORITY PreviousPriority, PriorityMin = HIGH_PRIORITY, PriorityMax = 0;
    MIGRATION_TIMES *Times;
    NTSTATUS Status;

    Context.GetPmr = Baseline->GetPmr;
    Context.RequestInterrupt = Baseline->RequestInterrupt;
    RtlCopyMemory(Context.Baseline, Baseline->Baseline, sizeof(Context.Baseline));
    Context.SignalAck = (Mode != 0);
    Times = ExAllocatePoolWithTag(NonPagedPool, Rounds * sizeof(*Times), 'tMmK');
    ok(Times != NULL, "Mode %lu timing allocation failed\n", Mode);
    if (!Times) return;

    PreviousPriority = KeQueryPriorityThread(Controller);
    if (Mode != 0) KeSetPriorityThread(Controller, 16);
    KeInitializeEvent(&Context.Ready, NotificationEvent, FALSE);
    KeInitializeEvent(&Context.Go, NotificationEvent, FALSE);
    KeInitializeEvent(&Context.Ack, SynchronizationEvent, FALSE);
    Context.LastCpu = -1;
    KeQueryPerformanceCounter(&Frequency);
    Timeout.QuadPart = -10000000;
    Thread = KmtStartThread(MigrationThread, &Context);
    if (!Thread) goto Cleanup;
    Status = KeWaitForSingleObject(&Context.Ready, Executive, KernelMode, FALSE, &Timeout);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (Status != STATUS_SUCCESS) goto Stop;
    if (Mode != 0) KeSetPriorityThread(Thread, Mode == 1 ? 8 : 17);
    KeSetEvent(&Context.Go, IO_NO_INCREMENT, FALSE);
    for (Round = 0; Round < Rounds; Round++)
    {
        Cpu = 1 + ((Round + 1) % ((ULONG)KeNumberProcessors - 1));
        Context.RequestedCpu = Cpu;
        KeClearEvent(&Context.Ack);
        Start = KeQueryPerformanceCounter(NULL).QuadPart;
        BeforeSwitches = Controller->ContextSwitches;
        InterlockedExchange(&Context.Request, (LONG)Round + 1);
        KeSetAffinityThread(Thread, (KAFFINITY)1 << Cpu);
        Returned = KeQueryPerformanceCounter(NULL).QuadPart;
        CallSwitches += Controller->ContextSwitches - BeforeSwitches;
        BeforeWait = Controller->ContextSwitches;
        Deadline = Returned + Frequency.QuadPart;
        while (Context.Acknowledged != (LONG)Round + 1 && KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
        {
            if (Mode == 0)
                KeStallExecutionProcessor(1);
            else
                KeWaitForSingleObject(&Context.Ack, Executive, KernelMode, FALSE, &Timeout);
        }
        End = KeQueryPerformanceCounter(NULL).QuadPart;
        WaitSwitches += Controller->ContextSwitches - BeforeWait;
        if (Context.Acknowledged != (LONG)Round + 1) break;
        KeMemoryBarrier();
        if (Context.Received < Start || End < Context.Received || Returned < Start)
            TimingErrors++;
        Times[Completed].Ticks[0] = Returned - Start;
        Times[Completed].Ticks[1] = Context.Received - Start;
        Times[Completed].Ticks[2] = End - Context.Received;
        if (Context.Received - Returned > Frequency.QuadPart / 1000) LateArrivals++;
        if (End - Context.Received > Frequency.QuadPart / 1000) LateAcks++;
        PriorityMin = min(PriorityMin, Context.ReceivedPriority);
        PriorityMax = max(PriorityMax, Context.ReceivedPriority);
        if (Mode != 0 &&
            (KeQueryPriorityThread(Controller) != 16 ||
             (Mode == 1 && (Context.ReceivedPriority < 8 || Context.ReceivedPriority >= 16)) ||
             (Mode == 2 && Context.ReceivedPriority != 17)))
            PriorityErrors++;
        Completed++;
    }

Stop:
    InterlockedExchange(&Context.Stop, 1);
    KmtFinishThread(Thread, &Context.Go);
    ExpectedCpus = KeQueryActiveProcessors() & ~(KAFFINITY)1;
    ok_eq_ulong(Completed, Rounds);
    ok(Context.Transitions >= Rounds, "Only %lu IRQL transitions\n", Context.Transitions);
    ok(Context.Migrations >= Rounds - 1, "Only %lu migrations\n", Context.Migrations);
    ok_eq_ulonglong(Context.SeenCpus, ExpectedCpus);
    ok_eq_ulong(Context.BadIrql, 0);
    ok_eq_ulong(Context.BadMask, 0);
    ok_eq_ulong(Context.BadDaif, 0);
    ok_eq_ulong(TimingErrors, 0);
    ok_eq_ulong(PriorityErrors, 0);
    trace("PMR_MIGRATION mode=%lu requests=%lu transitions=%lu migrations=%lu cpus=0x%Ix irql_errors=%lu mask_errors=%lu daif_errors=%lu\n",
          Mode, Completed, Context.Transitions, Context.Migrations, Context.SeenCpus,
          Context.BadIrql, Context.BadMask, Context.BadDaif);
    trace("MIGRATION_SCHED mode=%lu samples=%lu controller=%ld worker_min=%ld worker_max=%ld call_switches=%lu wait_switches=%lu late_arrivals=%lu late_acks=%lu\n",
          Mode, Completed, KeQueryPriorityThread(Controller), PriorityMin, PriorityMax,
          CallSwitches, WaitSwitches, LateArrivals, LateAcks);
    ReportMigrationTimes(Mode, Times, Completed, Frequency.QuadPart);

Cleanup:
    if (Mode != 0) KeSetPriorityThread(Controller, PreviousPriority);
    ExFreePoolWithTag(Times, 'tMmK');
}

START_TEST(KeArm64PmrMigration)
{
    PMR_MIGRATION Baseline = {0};
    KAFFINITY PreviousAffinity;
    ULONG Cpu;
    ULONG64 Daif;

    if (skip(KeNumberProcessors >= 3, "Three processors required\n")) return;
    Baseline.GetPmr = (PGET_PMR)KmtGetSystemRoutineAddress(L"HalGetGicPriorityMask");
    Baseline.RequestInterrupt = (PREQUEST_SOFTWARE_INTERRUPT)KmtGetSystemRoutineAddress(L"HalRequestSoftwareInterrupt");
    if (skip(Baseline.GetPmr && Baseline.RequestInterrupt, "GIC helpers unavailable\n")) return;

    PreviousAffinity = KeSetSystemAffinityThreadEx(1);
    for (Cpu = 0; Cpu < (ULONG)KeNumberProcessors; Cpu++)
    {
        KeSetSystemAffinityThread((KAFFINITY)1 << Cpu);
        KeLowerIrql(PASSIVE_LEVEL);
        __asm__ __volatile__("mrs %0, daif\n\tmsr daifset, #3" : "=r"(Daif) :: "memory");
        Baseline.Baseline[Cpu] = Baseline.GetPmr();
        __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
        ok(Baseline.Baseline[Cpu] >= 0xE0, "CPU %lu baseline PMR = 0x%lx\n", Cpu, Baseline.Baseline[Cpu]);
    }
    KeSetSystemAffinityThread(1);
    /* Retain the original busy-poll case, then control priority and wait for acknowledgments. */
    CheckMigration(0, MIGRATION_ROUNDS, &Baseline);
    CheckMigration(1, MIGRATION_PRIORITY_ROUNDS, &Baseline);
    CheckMigration(2, MIGRATION_PRIORITY_ROUNDS, &Baseline);
    KeRevertToUserAffinityThreadEx(PreviousAffinity);
}
