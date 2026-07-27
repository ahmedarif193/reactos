/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Gated AMD64 SMP runtime progress diagnostics
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>

#define NDEBUG
#include <debug.h>

typedef struct DECLSPEC_CACHEALIGN _SMPDBG_CPU
{
    volatile ULONG64 RuntimeTicks;
    volatile ULONG64 QuantumRequests;
    volatile ULONG64 DispatchInterrupts;
    volatile ULONG64 QuantumEnds;
    volatile ULONG64 Ipis;
    volatile ULONG64 RemoteDpcs;
    volatile ULONG64 SchedulerIpis;
    volatile ULONG64 QueuedDpcIpis;
    volatile ULONG64 TbFlushIpis;
    volatile ULONG64 GenericCallIpis;
    volatile ULONG64 BalanceWake;
    volatile ULONG64 BalanceIdle;
    volatile ULONG64 BalanceQuantum;
    volatile ULONG64 BalancePeriodic;
    volatile ULONG64 StandbySteals;
    volatile ULONG64 CycleCharges;
    volatile ULONG64 ChargedCycles;
    volatile ULONG64 CycleChargeResets;
    volatile ULONG64 RemoteDpcSources[SMPDBG_MAXCPU];
    volatile ULONG64 BalanceSources[SMPDBG_MAXCPU];
    volatile ULONG_PTR LastThread;
    volatile ULONG_PTR LastProcessId;
    volatile ULONG_PTR LastThreadId;
    volatile ULONG64 LastImage0;
    volatile ULONG64 LastImage1;
    volatile ULONG LastSourceCpu;
    volatile ULONG LastPriority;
    volatile ULONG LastIdealProcessor;
    volatile ULONG LastCause;
} SMPDBG_CPU, *PSMPDBG_CPU;

typedef struct _SMPDBG_SAMPLE
{
    ULONG64 RuntimeTicks;
    ULONG64 QuantumRequests;
    ULONG64 DispatchInterrupts;
    ULONG64 QuantumEnds;
    ULONG64 Ipis;
    ULONG64 RemoteDpcs;
    ULONG64 SchedulerIpis;
    ULONG64 QueuedDpcIpis;
    ULONG64 TbFlushIpis;
    ULONG64 GenericCallIpis;
    ULONG64 BalanceWake;
    ULONG64 BalanceIdle;
    ULONG64 BalanceQuantum;
    ULONG64 BalancePeriodic;
    ULONG64 StandbySteals;
    ULONG64 CycleCharges;
    ULONG64 ChargedCycles;
    ULONG64 CycleChargeResets;
    ULONG64 RemoteDpcSources[SMPDBG_MAXCPU];
    ULONG64 BalanceSources[SMPDBG_MAXCPU];
    ULONG ContextSwitches;
} SMPDBG_SAMPLE, *PSMPDBG_SAMPLE;

typedef struct _SMPDBG_THREAD_SNAPSHOT
{
    PKTHREAD Thread;
    ULONG_PTR ProcessId;
    ULONG_PTR ThreadId;
    KAFFINITY Affinity;
    ULONG Priority;
    ULONG State;
    ULONG IdealProcessor;
    ULONG NextProcessor;
    ULONG WaitAge;
    CHAR Image[17];
} SMPDBG_THREAD_SNAPSHOT, *PSMPDBG_THREAD_SNAPSHOT;

typedef struct _SMPDBG_DISPATCH_SNAPSHOT
{
    SMPDBG_THREAD_SNAPSHOT Current;
    SMPDBG_THREAD_SNAPSHOT Next;
    SMPDBG_THREAD_SNAPSHOT Ready;
    ULONG ReadySummary;
    ULONG ReadyCount;
    ULONG CurrentPriority;
    ULONG CurrentBasePriority;
    ULONG CurrentState;
    ULONG QuantumReset;
    LONG QuantumLeft;
    ULONG64 CycleTime;
    ULONG64 QuantumTarget;
    ULONG64 StartCycles;
    ULONG QuantumEnd;
    ULONG DpcQueueDepth;
    ULONG DpcRoutineActive;
    ULONG DpcInterruptRequested;
} SMPDBG_DISPATCH_SNAPSHOT, *PSMPDBG_DISPATCH_SNAPSHOT;

BOOLEAN SmpDbgEnabled = FALSE;

static SMPDBG_CPU SmpDbgCpu[MAXIMUM_PROCESSORS];
static SMPDBG_SAMPLE SmpDbgPrevious[MAXIMUM_PROCESSORS];
static volatile LONG SmpDbgReporterStarted;

static
PSMPDBG_CPU
SmpDbgGetCpu(
    _In_ ULONG Cpu)
{
    if (!SmpDbgEnabled || (Cpu >= RTL_NUMBER_OF(SmpDbgCpu)))
        return NULL;

    return &SmpDbgCpu[Cpu];
}

VOID
NTAPI
SmpDbgRuntimeTick(
    _In_ ULONG Cpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter != NULL)
        Counter->RuntimeTicks++;
}

VOID
NTAPI
SmpDbgQuantumRequest(
    _In_ ULONG Cpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter != NULL)
        Counter->QuantumRequests++;
}

VOID
NTAPI
SmpDbgDispatchInterrupt(
    _In_ ULONG Cpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter != NULL)
        Counter->DispatchInterrupts++;
}

VOID
NTAPI
SmpDbgQuantumEnd(
    _In_ ULONG Cpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter != NULL)
        Counter->QuantumEnds++;
}

VOID
NTAPI
SmpDbgIpi(
    _In_ ULONG Cpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter != NULL)
        Counter->Ipis++;
}

VOID
NTAPI
SmpDbgRemoteDpc(
    _In_ ULONG Cpu,
    _In_ ULONG SourceCpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter != NULL)
    {
        InterlockedIncrement64((PLONG64)&Counter->RemoteDpcs);
        if (SourceCpu < RTL_NUMBER_OF(Counter->RemoteDpcSources))
        {
            InterlockedIncrement64((PLONG64)&Counter->RemoteDpcSources[SourceCpu]);
        }
    }
}

VOID
NTAPI
SmpDbgSchedulerIpi(
    _In_ ULONG Cpu,
    _In_opt_ PVOID ThreadPointer,
    _In_ ULONG Cause)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);
    PKTHREAD Thread = ThreadPointer;
    ULONG SourceCpu;

    if (Counter == NULL)
        return;

    SourceCpu = KeGetCurrentProcessorNumber();
    InterlockedIncrement64((PLONG64)&Counter->SchedulerIpis);
    Counter->LastSourceCpu = SourceCpu;
    Counter->LastCause = Cause;

    if (Thread != NULL)
    {
        PETHREAD Ethread = CONTAINING_RECORD(Thread, ETHREAD, Tcb);
        PEPROCESS Process = CONTAINING_RECORD(Thread->ApcState.Process, EPROCESS, Pcb);
        ULONG64 Image[2] = {0, 0};

        RtlCopyMemory(Image, PsGetProcessImageFileName(Process), sizeof(Process->ImageFileName));
        Counter->LastThread = (ULONG_PTR)Thread;
        Counter->LastProcessId = (ULONG_PTR)Ethread->Cid.UniqueProcess;
        Counter->LastThreadId = (ULONG_PTR)Ethread->Cid.UniqueThread;
        Counter->LastImage0 = Image[0];
        Counter->LastImage1 = Image[1];
        Counter->LastPriority = Thread->Priority;
        Counter->LastIdealProcessor = Thread->IdealProcessor;
    }
}

VOID
NTAPI
SmpDbgQueuedDpcIpi(
    _In_ ULONG Cpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter != NULL)
        InterlockedIncrement64((PLONG64)&Counter->QueuedDpcIpis);
}

VOID
NTAPI
SmpDbgTbFlushIpi(
    _In_ ULONG Cpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter != NULL)
        InterlockedIncrement64((PLONG64)&Counter->TbFlushIpis);
}

VOID
NTAPI
SmpDbgGenericCallIpi(
    _In_ ULONG Cpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter != NULL)
        InterlockedIncrement64((PLONG64)&Counter->GenericCallIpis);
}

VOID
NTAPI
SmpDbgBalanceEvent(
    _In_ ULONG TargetCpu,
    _In_ ULONG SourceCpu,
    _In_ ULONG Reason)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(TargetCpu);

    if (Counter == NULL)
        return;

    switch (Reason)
    {
        case SMPDBG_BALANCE_WAKE_PLACEMENT:
            InterlockedIncrement64((PLONG64)&Counter->BalanceWake);
            break;
        case SMPDBG_BALANCE_IDLE:
            InterlockedIncrement64((PLONG64)&Counter->BalanceIdle);
            break;
        case SMPDBG_BALANCE_QUANTUM:
            InterlockedIncrement64((PLONG64)&Counter->BalanceQuantum);
            break;
        case SMPDBG_BALANCE_PERIODIC:
            InterlockedIncrement64((PLONG64)&Counter->BalancePeriodic);
            break;
        default:
            return;
    }

    if (SourceCpu < RTL_NUMBER_OF(Counter->BalanceSources))
        InterlockedIncrement64((PLONG64)&Counter->BalanceSources[SourceCpu]);
}

VOID
NTAPI
SmpDbgStandbySteal(
    _In_ ULONG TargetCpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(TargetCpu);

    if (Counter != NULL)
        InterlockedIncrement64((PLONG64)&Counter->StandbySteals);
}

VOID
NTAPI
SmpDbgCycleCharge(
    _In_ ULONG Cpu,
    _In_ ULONG64 Cycles,
    _In_ BOOLEAN Reset)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter == NULL)
        return;

    if (Reset)
    {
        InterlockedIncrement64((PLONG64)&Counter->CycleChargeResets);
        return;
    }

    InterlockedIncrement64((PLONG64)&Counter->CycleCharges);
    InterlockedExchangeAdd64((PLONG64)&Counter->ChargedCycles, (LONG64)Cycles);
}

static
VOID
SmpDbgSnapshotThread(
    _In_opt_ PKTHREAD Thread,
    _Out_ PSMPDBG_THREAD_SNAPSHOT Snapshot)
{
    PETHREAD Ethread;
    PEPROCESS Process;

    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    Snapshot->Image[0] = '-';
    if (Thread == NULL)
        return;

    Snapshot->Thread = Thread;
    Snapshot->Affinity = KiThreadAffinityMask(Thread);
    Snapshot->Priority = Thread->Priority;
    Snapshot->State = Thread->State;
    Snapshot->IdealProcessor = Thread->IdealProcessor;
    Snapshot->NextProcessor = Thread->NextProcessor;
    Snapshot->WaitAge = KeTickCount.LowPart - Thread->WaitTime;

    Ethread = CONTAINING_RECORD(Thread, ETHREAD, Tcb);
    Snapshot->ProcessId = (ULONG_PTR)Ethread->Cid.UniqueProcess;
    Snapshot->ThreadId = (ULONG_PTR)Ethread->Cid.UniqueThread;
    if (Thread->ApcState.Process == NULL)
        return;

    Process = CONTAINING_RECORD(Thread->ApcState.Process, EPROCESS, Pcb);
    RtlCopyMemory(Snapshot->Image, PsGetProcessImageFileName(Process), sizeof(Process->ImageFileName));
    Snapshot->Image[16] = ANSI_NULL;
}

static
VOID
SmpDbgSnapshotDispatchState(
    _In_ PKPRCB Prcb,
    _Out_ PSMPDBG_DISPATCH_SNAPSHOT Snapshot)
{
    PLIST_ENTRY ListHead, Entry;
    PKTHREAD CurrentThread, ReadyThread;
    ULONG Priority, ReadyPriority;
    KIRQL OldIrql;

    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquirePrcbLock(Prcb);

    CurrentThread = Prcb->CurrentThread;
    SmpDbgSnapshotThread(CurrentThread, &Snapshot->Current);
    SmpDbgSnapshotThread(Prcb->NextThread, &Snapshot->Next);
    Snapshot->ReadySummary = Prcb->ReadySummary;
    Snapshot->StartCycles = Prcb->StartCycles;
    Snapshot->QuantumEnd = Prcb->QuantumEnd;
    Snapshot->DpcQueueDepth = Prcb->DpcData[0].DpcQueueDepth;
    Snapshot->DpcRoutineActive = Prcb->DpcRoutineActive;
    Snapshot->DpcInterruptRequested = Prcb->DpcInterruptRequested;

    if (CurrentThread != NULL)
    {
        Snapshot->CurrentPriority = CurrentThread->Priority;
        Snapshot->CurrentBasePriority = CurrentThread->BasePriority;
        Snapshot->CurrentState = CurrentThread->State;
        Snapshot->QuantumReset = CurrentThread->QuantumReset;
        Snapshot->QuantumLeft = KiGetThreadQuantum(CurrentThread);
        Snapshot->CycleTime = CurrentThread->CycleTime;
        Snapshot->QuantumTarget = CurrentThread->QuantumTarget;
    }

    if (Snapshot->ReadySummary != 0)
    {
        BitScanReverse(&ReadyPriority, Snapshot->ReadySummary);
        ListHead = &Prcb->DispatcherReadyListHead[ReadyPriority];
        if (!IsListEmpty(ListHead))
        {
            ReadyThread = CONTAINING_RECORD(ListHead->Flink, KTHREAD, WaitListEntry);
            SmpDbgSnapshotThread(ReadyThread, &Snapshot->Ready);
        }
    }

    for (Priority = 0;
         (Priority <= HIGH_PRIORITY) && (Snapshot->ReadyCount < 1024);
         Priority++)
    {
        ListHead = &Prcb->DispatcherReadyListHead[Priority];
        for (Entry = ListHead->Flink;
             (Entry != ListHead) && (Snapshot->ReadyCount < 1024);
             Entry = Entry->Flink)
        {
            Snapshot->ReadyCount++;
        }
    }

    KiReleasePrcbLock(Prcb);
    KeLowerIrql(OldIrql);
}

static
VOID
NTAPI
SmpDbgReporterThread(
    _In_opt_ PVOID Context)
{
    LARGE_INTEGER Delay;
    ULONG64 LastTime, Sample = 0;
    ULONG Cpu;

    UNREFERENCED_PARAMETER(Context);

    Delay.QuadPart = -10LL * 1000 * 1000;
    LastTime = KeQueryInterruptTime();

    for (;;)
    {
        ULONG64 Now;

        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        Now = KeQueryInterruptTime();
        Sample++;

        DbgPrint("SMPSTAT sample=%I64u elapsed_ms=%I64u active=0x%Ix idle=0x%Ix reportcpu=%lu thraff=0x%Ix sysaff=0x%Ix\n",
                 Sample,
                 (Now - LastTime) / 10000,
                 (ULONG_PTR)KeActiveProcessors,
                 (ULONG_PTR)KiIdleSummary,
                 KeGetCurrentProcessorNumber(),
                 (ULONG_PTR)KiThreadAffinityMask(KeGetCurrentThread()),
                 PsInitialSystemProcess ? (ULONG_PTR)PsInitialSystemProcess->Pcb.Affinity : 0);
        LastTime = Now;

        for (Cpu = 0;
             (Cpu < (ULONG)KeNumberProcessors) &&
             (Cpu < RTL_NUMBER_OF(SmpDbgCpu));
             Cpu++)
        {
            PKPRCB Prcb = KiProcessorBlock[Cpu];
            PKIPCR Pcr;
            SMPDBG_DISPATCH_SNAPSHOT Dispatch;
            SMPDBG_SAMPLE Current;
            PSMPDBG_SAMPLE Previous;
            union
            {
                ULONG64 Parts[2];
                CHAR Text[17];
            } LastImage;
            ULONG Source;

            if (Prcb == NULL)
                continue;

            Pcr = CONTAINING_RECORD(Prcb, KIPCR, Prcb);
            Current.RuntimeTicks = SmpDbgCpu[Cpu].RuntimeTicks;
            Current.QuantumRequests = SmpDbgCpu[Cpu].QuantumRequests;
            Current.DispatchInterrupts = SmpDbgCpu[Cpu].DispatchInterrupts;
            Current.QuantumEnds = SmpDbgCpu[Cpu].QuantumEnds;
            Current.Ipis = SmpDbgCpu[Cpu].Ipis;
            Current.RemoteDpcs = SmpDbgCpu[Cpu].RemoteDpcs;
            Current.SchedulerIpis = SmpDbgCpu[Cpu].SchedulerIpis;
            Current.QueuedDpcIpis = SmpDbgCpu[Cpu].QueuedDpcIpis;
            Current.TbFlushIpis = SmpDbgCpu[Cpu].TbFlushIpis;
            Current.GenericCallIpis = SmpDbgCpu[Cpu].GenericCallIpis;
            Current.BalanceWake = SmpDbgCpu[Cpu].BalanceWake;
            Current.BalanceIdle = SmpDbgCpu[Cpu].BalanceIdle;
            Current.BalanceQuantum = SmpDbgCpu[Cpu].BalanceQuantum;
            Current.BalancePeriodic = SmpDbgCpu[Cpu].BalancePeriodic;
            Current.StandbySteals = SmpDbgCpu[Cpu].StandbySteals;
            Current.CycleCharges = SmpDbgCpu[Cpu].CycleCharges;
            Current.ChargedCycles = SmpDbgCpu[Cpu].ChargedCycles;
            Current.CycleChargeResets = SmpDbgCpu[Cpu].CycleChargeResets;
            for (Source = 0; Source < SMPDBG_MAXCPU; Source++)
            {
                Current.RemoteDpcSources[Source] =
                    SmpDbgCpu[Cpu].RemoteDpcSources[Source];
                Current.BalanceSources[Source] =
                    SmpDbgCpu[Cpu].BalanceSources[Source];
            }
            Current.ContextSwitches = Pcr->ContextSwitches;
            Previous = &SmpDbgPrevious[Cpu];
            LastImage.Parts[0] = SmpDbgCpu[Cpu].LastImage0;
            LastImage.Parts[1] = SmpDbgCpu[Cpu].LastImage1;
            LastImage.Text[16] = ANSI_NULL;
            SmpDbgSnapshotDispatchState(Prcb, &Dispatch);

            DbgPrint("\nSMPSTAT cpu=%lu dtick=%I64u dqreq=%I64u ddisp=%I64u dqend=%I64u dipi=%I64u drdpc=%I64u dsched=%I64u ddpc=%I64u dtb=%I64u dgen=%I64u dplace=%I64u didle=%I64u dquant=%I64u dperiod=%I64u dstandby=%I64u dcharge=%I64u dcycles=%I64u dreset=%I64u dsrc0=%I64u dsrc1=%I64u dsrc2=%I64u dsrc3=%I64u dbsrc0=%I64u dbsrc1=%I64u dbsrc2=%I64u dbsrc3=%I64u dcsw=%lu ready=0x%lx rdycnt=%lu next=%p nextimg=%.16s nextpid=%Ix nexttid=%Ix nextpri=%lu nextstate=%lu nextaff=0x%Ix nextideal=%lu nextcpu=%lu cur=%p curimg=%.16s curpid=%Ix curtid=%Ix curpri=%lu curbase=%lu curstate=%lu curaff=0x%Ix curideal=%lu curcpu=%lu rdythr=%p rdyimg=%.16s rdypid=%Ix rdytid=%Ix rdypri=%lu rdystate=%lu rdyage=%lu rdyaff=0x%Ix rdyideal=%lu rdycpu=%lu qreset=%lu qleft=%ld qunit=%lu cycle=%I64u qtarget=%I64u startcyc=%I64u qflag=%u dpcq=%lu dpcact=%u dpcpend=%u lastthr=%p lastpid=%Ix lasttid=%Ix lastsrc=%lu lastpri=%lu lastideal=%lu lastcause=%lu lastimg=%.16s\n",
                     Cpu,
                     Current.RuntimeTicks - Previous->RuntimeTicks,
                     Current.QuantumRequests - Previous->QuantumRequests,
                     Current.DispatchInterrupts - Previous->DispatchInterrupts,
                     Current.QuantumEnds - Previous->QuantumEnds,
                     Current.Ipis - Previous->Ipis,
                     Current.RemoteDpcs - Previous->RemoteDpcs,
                     Current.SchedulerIpis - Previous->SchedulerIpis,
                     Current.QueuedDpcIpis - Previous->QueuedDpcIpis,
                     Current.TbFlushIpis - Previous->TbFlushIpis,
                     Current.GenericCallIpis - Previous->GenericCallIpis,
                     Current.BalanceWake - Previous->BalanceWake,
                     Current.BalanceIdle - Previous->BalanceIdle,
                     Current.BalanceQuantum - Previous->BalanceQuantum,
                     Current.BalancePeriodic - Previous->BalancePeriodic,
                     Current.StandbySteals - Previous->StandbySteals,
                     Current.CycleCharges - Previous->CycleCharges,
                     Current.ChargedCycles - Previous->ChargedCycles,
                     Current.CycleChargeResets - Previous->CycleChargeResets,
                     Current.RemoteDpcSources[0] - Previous->RemoteDpcSources[0],
                     Current.RemoteDpcSources[1] - Previous->RemoteDpcSources[1],
                     Current.RemoteDpcSources[2] - Previous->RemoteDpcSources[2],
                     Current.RemoteDpcSources[3] - Previous->RemoteDpcSources[3],
                     Current.BalanceSources[0] - Previous->BalanceSources[0],
                     Current.BalanceSources[1] - Previous->BalanceSources[1],
                     Current.BalanceSources[2] - Previous->BalanceSources[2],
                     Current.BalanceSources[3] - Previous->BalanceSources[3],
                     Current.ContextSwitches - Previous->ContextSwitches,
                     Dispatch.ReadySummary,
                     Dispatch.ReadyCount,
                     Dispatch.Next.Thread,
                     Dispatch.Next.Image,
                     Dispatch.Next.ProcessId,
                     Dispatch.Next.ThreadId,
                     Dispatch.Next.Priority,
                     Dispatch.Next.State,
                     (ULONG_PTR)Dispatch.Next.Affinity,
                     Dispatch.Next.IdealProcessor,
                     Dispatch.Next.NextProcessor,
                     Dispatch.Current.Thread,
                     Dispatch.Current.Image,
                     Dispatch.Current.ProcessId,
                     Dispatch.Current.ThreadId,
                     Dispatch.CurrentPriority,
                     Dispatch.CurrentBasePriority,
                     Dispatch.CurrentState,
                     (ULONG_PTR)Dispatch.Current.Affinity,
                     Dispatch.Current.IdealProcessor,
                     Dispatch.Current.NextProcessor,
                     Dispatch.Ready.Thread,
                     Dispatch.Ready.Image,
                     Dispatch.Ready.ProcessId,
                     Dispatch.Ready.ThreadId,
                     Dispatch.Ready.Priority,
                     Dispatch.Ready.State,
                     Dispatch.Ready.WaitAge,
                     (ULONG_PTR)Dispatch.Ready.Affinity,
                     Dispatch.Ready.IdealProcessor,
                     Dispatch.Ready.NextProcessor,
                     Dispatch.QuantumReset,
                     Dispatch.QuantumLeft,
                     KiCyclesPerClockQuantum,
                     Dispatch.CycleTime,
                     Dispatch.QuantumTarget,
                     Dispatch.StartCycles,
                     Dispatch.QuantumEnd,
                     Dispatch.DpcQueueDepth,
                     Dispatch.DpcRoutineActive,
                     Dispatch.DpcInterruptRequested,
                     (PVOID)SmpDbgCpu[Cpu].LastThread,
                     SmpDbgCpu[Cpu].LastProcessId,
                     SmpDbgCpu[Cpu].LastThreadId,
                     SmpDbgCpu[Cpu].LastSourceCpu,
                     SmpDbgCpu[Cpu].LastPriority,
                     SmpDbgCpu[Cpu].LastIdealProcessor,
                     SmpDbgCpu[Cpu].LastCause,
                     LastImage.Text);

            *Previous = Current;
        }
    }
}

VOID
NTAPI
SmpDbgStartWatchdog(VOID)
{
    HANDLE Handle;
    NTSTATUS Status;

    if (!SmpDbgEnabled)
        return;

    if (InterlockedExchange(&SmpDbgReporterStarted, 1) != 0)
        return;

    Status = PsCreateSystemThread(&Handle,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  SmpDbgReporterThread,
                                  NULL);
    if (NT_SUCCESS(Status))
    {
        ZwClose(Handle);
        DbgPrint("SMPSTAT reporter started kernel=%p\n", (PVOID)PsNtosImageBase);
    }
    else
    {
        DbgPrint("SMPSTAT reporter start failed status=0x%08lx\n", Status);
    }
}
