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
} SMPDBG_CPU, *PSMPDBG_CPU;

typedef struct _SMPDBG_SAMPLE
{
    ULONG64 RuntimeTicks;
    ULONG64 QuantumRequests;
    ULONG64 DispatchInterrupts;
    ULONG64 QuantumEnds;
    ULONG64 Ipis;
    ULONG64 RemoteDpcs;
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
    _In_ ULONG Cpu)
{
    PSMPDBG_CPU Counter = SmpDbgGetCpu(Cpu);

    if (Counter != NULL)
        Counter->RemoteDpcs++;
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

        DbgPrint("SMPSTAT sample=%I64u elapsed_ms=%I64u active=0x%Ix idle=0x%Ix\n",
                 Sample,
                 (Now - LastTime) / 10000,
                 (ULONG_PTR)KeActiveProcessors,
                 (ULONG_PTR)KiIdleSummary);
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
            Current.ContextSwitches = Pcr->ContextSwitches;
            Previous = &SmpDbgPrevious[Cpu];

            DbgPrint("SMPSTAT cpu=%lu dtick=%I64u dqreq=%I64u ddisp=%I64u dqend=%I64u dipi=%I64u drdpc=%I64u dcsw=%lu ready=0x%lx next=%p cur=%p qflag=%u dpcq=%lu dpcact=%u dpcpend=%u\n",
                     Cpu,
                     Current.RuntimeTicks - Previous->RuntimeTicks,
                     Current.QuantumRequests - Previous->QuantumRequests,
                     Current.DispatchInterrupts - Previous->DispatchInterrupts,
                     Current.QuantumEnds - Previous->QuantumEnds,
                     Current.Ipis - Previous->Ipis,
                     Current.RemoteDpcs - Previous->RemoteDpcs,
                     Current.ContextSwitches - Previous->ContextSwitches,
                     Prcb->ReadySummary,
                     Prcb->NextThread,
                     CurrentThread,
                     (ULONG)Prcb->QuantumEnd,
                     (ULONG)Prcb->DpcData[0].DpcQueueDepth,
                     (ULONG)Prcb->DpcRoutineActive,
                     (ULONG)Prcb->DpcInterruptRequested);

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
