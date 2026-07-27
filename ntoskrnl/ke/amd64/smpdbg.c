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
    volatile ULONG64 RemoteDpcSources[SMPDBG_MAXCPU];
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
    ULONG64 RemoteDpcSources[SMPDBG_MAXCPU];
    ULONG ContextSwitches;
} SMPDBG_SAMPLE, *PSMPDBG_SAMPLE;

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
            InterlockedIncrement64(
                (PLONG64)&Counter->RemoteDpcSources[SourceCpu]);
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
        PEPROCESS Process =
            CONTAINING_RECORD(Thread->ApcState.Process, EPROCESS, Pcb);
        ULONG64 Image[2] = {0, 0};

        RtlCopyMemory(Image,
                      PsGetProcessImageFileName(Process),
                      sizeof(Process->ImageFileName));
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
                 PsInitialSystemProcess ?
                     (ULONG_PTR)PsInitialSystemProcess->Pcb.Affinity : 0);
        LastTime = Now;

        for (Cpu = 0;
             (Cpu < (ULONG)KeNumberProcessors) &&
             (Cpu < RTL_NUMBER_OF(SmpDbgCpu));
             Cpu++)
        {
            PKPRCB Prcb = KiProcessorBlock[Cpu];
            PKIPCR Pcr;
            PKTHREAD CurrentThread;
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
            CurrentThread = Prcb->CurrentThread;
            Current.RuntimeTicks = SmpDbgCpu[Cpu].RuntimeTicks;
            Current.QuantumRequests = SmpDbgCpu[Cpu].QuantumRequests;
            Current.DispatchInterrupts = SmpDbgCpu[Cpu].DispatchInterrupts;
            Current.QuantumEnds = SmpDbgCpu[Cpu].QuantumEnds;
            Current.Ipis = SmpDbgCpu[Cpu].Ipis;
            Current.RemoteDpcs = SmpDbgCpu[Cpu].RemoteDpcs;
            Current.SchedulerIpis = SmpDbgCpu[Cpu].SchedulerIpis;
            Current.QueuedDpcIpis = SmpDbgCpu[Cpu].QueuedDpcIpis;
            for (Source = 0; Source < SMPDBG_MAXCPU; Source++)
            {
                Current.RemoteDpcSources[Source] =
                    SmpDbgCpu[Cpu].RemoteDpcSources[Source];
            }
            Current.ContextSwitches = Pcr->ContextSwitches;
            Previous = &SmpDbgPrevious[Cpu];
            LastImage.Parts[0] = SmpDbgCpu[Cpu].LastImage0;
            LastImage.Parts[1] = SmpDbgCpu[Cpu].LastImage1;
            LastImage.Text[16] = ANSI_NULL;

            DbgPrint("SMPSTAT cpu=%lu dtick=%I64u dqreq=%I64u ddisp=%I64u dqend=%I64u dipi=%I64u drdpc=%I64u dsched=%I64u ddpc=%I64u dsrc0=%I64u dsrc1=%I64u dsrc2=%I64u dsrc3=%I64u dcsw=%lu ready=0x%lx next=%p cur=%p qflag=%u dpcq=%lu dpcact=%u dpcpend=%u lastthr=%p lastpid=%Ix lasttid=%Ix lastsrc=%lu lastpri=%lu lastideal=%lu lastcause=%lu lastimg=%.16s\n",
                     Cpu,
                     Current.RuntimeTicks - Previous->RuntimeTicks,
                     Current.QuantumRequests - Previous->QuantumRequests,
                     Current.DispatchInterrupts - Previous->DispatchInterrupts,
                     Current.QuantumEnds - Previous->QuantumEnds,
                     Current.Ipis - Previous->Ipis,
                     Current.RemoteDpcs - Previous->RemoteDpcs,
                     Current.SchedulerIpis - Previous->SchedulerIpis,
                     Current.QueuedDpcIpis - Previous->QueuedDpcIpis,
                     Current.RemoteDpcSources[0] - Previous->RemoteDpcSources[0],
                     Current.RemoteDpcSources[1] - Previous->RemoteDpcSources[1],
                     Current.RemoteDpcSources[2] - Previous->RemoteDpcSources[2],
                     Current.RemoteDpcSources[3] - Previous->RemoteDpcSources[3],
                     Current.ContextSwitches - Previous->ContextSwitches,
                     Prcb->ReadySummary,
                     Prcb->NextThread,
                     CurrentThread,
                     (ULONG)Prcb->QuantumEnd,
                     (ULONG)Prcb->DpcData[0].DpcQueueDepth,
                     (ULONG)Prcb->DpcRoutineActive,
                     (ULONG)Prcb->DpcInterruptRequested,
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
