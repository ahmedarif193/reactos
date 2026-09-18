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
CheckMigration(ULONG Mode, ULONG Rounds, PMR_MIGRATION *Baseline)
{
    PMR_MIGRATION Context = {0};
    PKTHREAD Thread;
    PKTHREAD Controller = KeGetCurrentThread();
    KAFFINITY ExpectedCpus;
    LARGE_INTEGER Frequency, Timeout;
    LONGLONG Deadline, Start, Returned, End;
    ULONG Cpu, Round, Completed = 0, TimingErrors = 0, PriorityErrors = 0;
    KPRIORITY PreviousPriority;
    NTSTATUS Status;

    Context.GetPmr = Baseline->GetPmr;
    Context.RequestInterrupt = Baseline->RequestInterrupt;
    RtlCopyMemory(Context.Baseline, Baseline->Baseline, sizeof(Context.Baseline));
    Context.SignalAck = (Mode != 0);

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
        InterlockedExchange(&Context.Request, (LONG)Round + 1);
        KeSetAffinityThread(Thread, (KAFFINITY)1 << Cpu);
        Returned = KeQueryPerformanceCounter(NULL).QuadPart;
        Deadline = Returned + Frequency.QuadPart;
        while (Context.Acknowledged != (LONG)Round + 1 && KeQueryPerformanceCounter(NULL).QuadPart < Deadline)
        {
            if (Mode == 0)
                KeStallExecutionProcessor(1);
            else
                KeWaitForSingleObject(&Context.Ack, Executive, KernelMode, FALSE, &Timeout);
        }
        End = KeQueryPerformanceCounter(NULL).QuadPart;
        if (Context.Acknowledged != (LONG)Round + 1) break;
        KeMemoryBarrier();
        if (Context.Received < Start || End < Context.Received || Returned < Start)
            TimingErrors++;
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

Cleanup:
    if (Mode != 0) KeSetPriorityThread(Controller, PreviousPriority);
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
