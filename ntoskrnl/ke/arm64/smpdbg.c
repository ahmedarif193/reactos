/*
 * PROJECT:     ReactOS Kernel (ARM64)
 * PURPOSE:     SMP boot diagnostics state. Recorded from ntoskrnl and the HAL,
 *              dumped from one place. Gated at runtime by /SMPDIAG (SmpDbgEnabled).
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64)

BOOLEAN SmpDbgEnabled = FALSE;

#define SMPDBG_TIMER_PPI(_i) (((_i) == 27) || ((_i) == 30))

typedef struct _SMPDBG_CPU
{
    volatile ULONG Begin;   /* HAL accepted the timer PPI */
    volatile ULONG Eoi;     /* HAL EOI'd the timer PPI    */
    volatile ULONG Reject;  /* HAL rejected at high IRQL  */
    volatile ULONG Tick;    /* timer ISR ran              */
    volatile ULONG Ipi;     /* KiIpiServiceRoutine ran    */
    volatile ULONG Park;    /* idle loop entered WFI      */
    volatile ULONG Wake;    /* idle loop returned from WFI*/
    volatile ULONG Ctl;     /* last timer CTL             */
    volatile LONG  Tval;    /* last timer TVAL            */
    volatile ULONG Pmr;     /* last GIC PMR               */
    volatile ULONG GicPrio; /* timer PPI GIC priority     */
    volatile ULONG GicEn;   /* timer PPI enabled          */
    volatile ULONG GicPend; /* timer PPI pending          */
    volatile ULONG GicAct;  /* timer PPI active           */
    volatile ULONG IdleStreak; /* consecutive all-idle ticks seen by this CPU */
    volatile ULONG RemoteDpc;
    volatile ULONG SchedulerIpi;
    volatile ULONG QueuedDpcIpi;
} SMPDBG_CPU;

static SMPDBG_CPU SmpDbgCpu[SMPDBG_MAXCPU];

/* One-shot latch: dump the wedge state exactly once per all-idle episode, then stay silent until the system recovers (some CPU goes non-idle). Avoids thousands of repeated SMPWEDGE lines. */
static volatile LONG SmpDbgWedgeDumped;

VOID NTAPI SmpDbgTimerBegin(ULONG Cpu, ULONG IntId)
{
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU) && SMPDBG_TIMER_PPI(IntId))
        InterlockedIncrement((PLONG)&SmpDbgCpu[Cpu].Begin);
}

VOID NTAPI SmpDbgTimerEoi(ULONG Cpu, ULONG IntId)
{
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU) && SMPDBG_TIMER_PPI(IntId))
        InterlockedIncrement((PLONG)&SmpDbgCpu[Cpu].Eoi);
}

VOID NTAPI SmpDbgTimerReject(ULONG Cpu, ULONG IntId)
{
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU) && SMPDBG_TIMER_PPI(IntId))
        InterlockedIncrement((PLONG)&SmpDbgCpu[Cpu].Reject);
}

VOID NTAPI SmpDbgTimerTick(ULONG Cpu)
{
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU))
        SmpDbgCpu[Cpu].Tick++;
}

VOID NTAPI SmpDbgIpi(ULONG Cpu)
{
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU))
        SmpDbgCpu[Cpu].Ipi++;
}

VOID NTAPI SmpDbgRemoteDpc(ULONG Cpu, ULONG SourceCpu)
{
    UNREFERENCED_PARAMETER(SourceCpu);

    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU))
        InterlockedIncrement((PLONG)&SmpDbgCpu[Cpu].RemoteDpc);
}

VOID NTAPI SmpDbgSchedulerIpi(ULONG Cpu, PVOID Thread, ULONG Cause)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(Cause);

    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU))
        InterlockedIncrement((PLONG)&SmpDbgCpu[Cpu].SchedulerIpi);
}

VOID NTAPI SmpDbgQueuedDpcIpi(ULONG Cpu)
{
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU))
        InterlockedIncrement((PLONG)&SmpDbgCpu[Cpu].QueuedDpcIpi);
}

VOID NTAPI SmpDbgPark(ULONG Cpu)
{
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU))
        SmpDbgCpu[Cpu].Park++;
}

VOID NTAPI SmpDbgWake(ULONG Cpu)
{
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU))
        SmpDbgCpu[Cpu].Wake++;
}

/*
 * Per-CPU heartbeat from the timer ISR. Each CPU prints its own line every 64
 * of its own ticks. If a CPU's heartbeat stops while the boot is stalled, that
 * CPU's timer is dead (no ticks); if heartbeats keep coming on all CPUs while
 * the boot is stalled, timers are alive and it is a lost wakeup / scheduler bug.
 * next=NextThread shows a runnable thread the idle CPU is failing to pick up.
 */
extern PKPRCB KiProcessorBlock[];

VOID NTAPI SmpDbgHeartbeat(ULONG Cpu)
{
    /*
     * Wedge detector, called from the timer ISR (immune to the scheduler-dispatch
     * deadlock it diagnoses, because the architected timer keeps firing). Silent
     * during normal boot: it only prints when every CPU except this one is idle
     * (KiIdleSummary), i.e. an all-CPUs-idle wedge, and then dumps each PRCB's
     * dispatch state so a thread that is runnable-but-never-dispatched (NextThread
     * set, or ReadySummary != 0, or a non-empty DeferredReadyList while all idle)
     * is visible straight off the serial log.
     */
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU))
    {
        KAFFINITY Active = (KAFFINITY)KeActiveProcessors;
        KAFFINITY IdleOrMe = (KAFFINITY)KiIdleSummary | ((KAFFINITY)1 << Cpu);
        BOOLEAN AllIdle = (Active != 0) && ((IdleOrMe & Active) == Active);

        /* Require a sustained all-idle streak before crying wedge: brief all-idle windows during boot (waiting on I/O between phases) are normal and must not trigger. A real wedge is permanent, so the streak climbs without bound. */
        if (!AllIdle)
        {
            SmpDbgCpu[Cpu].IdleStreak = 0;
        }
        else if (SmpDbgCpu[Cpu].IdleStreak < 0xFFFFFFFF)
        {
            SmpDbgCpu[Cpu].IdleStreak++;
        }

        if (AllIdle && (SmpDbgCpu[Cpu].IdleStreak >= 512) &&
            (InterlockedCompareExchange(&SmpDbgWedgeDumped, 1, 0) == 0))
        {
            ULONG i;
            DbgPrint("SMPWEDGE cpu=%lu tick=%lu streak=%lu idle=0x%Ix active=0x%Ix\n",
                     Cpu, SmpDbgCpu[Cpu].Tick, SmpDbgCpu[Cpu].IdleStreak,
                     (ULONG_PTR)KiIdleSummary, (ULONG_PTR)KeActiveProcessors);
            for (i = 0; i < (ULONG)KeNumberProcessors && i < SMPDBG_MAXCPU; i++)
            {
                PKPRCB P = KiProcessorBlock[i];
                if (P != NULL)
                    DbgPrint("SMPWEDGE   cpu%lu next=%p ready=0x%lx defer=%p cur=%p sleep=%u tick=%lu treq=%p dpcq=%lu dpcact=%u dpcint=%u rdpc=%lu schedipi=%lu dpcipi=%lu\n",
                             i, P->NextThread, (ULONG)P->ReadySummary,
                             P->DeferredReadyListHead.Next, P->CurrentThread,
                             (ULONG)P->Sleeping, SmpDbgCpu[i].Tick,
                             (PVOID)P->TimerRequest, (ULONG)P->DpcData[0].DpcQueueDepth,
                             (ULONG)P->DpcRoutineActive, (ULONG)P->DpcInterruptRequested,
                             SmpDbgCpu[i].RemoteDpc, SmpDbgCpu[i].SchedulerIpi,
                             SmpDbgCpu[i].QueuedDpcIpi);
            }
        }
    }
}

VOID NTAPI SmpDbgCntv(ULONG Cpu, ULONG Ctl, LONG Tval, ULONG Pmr)
{
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU))
    {
        SmpDbgCpu[Cpu].Ctl = Ctl;
        SmpDbgCpu[Cpu].Tval = Tval;
        SmpDbgCpu[Cpu].Pmr = Pmr;
    }
}

VOID NTAPI SmpDbgGic(ULONG Cpu, ULONG Prio, ULONG En, ULONG Pend, ULONG Act)
{
    if (SmpDbgEnabled && (Cpu < SMPDBG_MAXCPU))
    {
        SmpDbgCpu[Cpu].GicPrio = Prio;
        SmpDbgCpu[Cpu].GicEn = En;
        SmpDbgCpu[Cpu].GicPend = Pend;
        SmpDbgCpu[Cpu].GicAct = Act;
    }
}

ULONG NTAPI SmpDbgGetTick(ULONG Cpu)
{
    return (Cpu < SMPDBG_MAXCPU) ? SmpDbgCpu[Cpu].Tick : 0;
}

VOID NTAPI SmpDbgDumpTimers(ULONG StrandMask)
{
    if (!SmpDbgEnabled)
        return;

    /* "ticks=[...]" kept verbatim so scripts/analyze_cpu_state.py keeps parsing it. */
    DbgPrint("SMP4DBG timer-state strand=0x%lx ticks=[%lu,%lu,%lu,%lu] "
             "begin=[%lu,%lu,%lu,%lu] eoi=[%lu,%lu,%lu,%lu] rej=[%lu,%lu,%lu,%lu] "
             "ctl=[0x%lx,0x%lx,0x%lx,0x%lx] pmr=[0x%lx,0x%lx,0x%lx,0x%lx]\n",
             StrandMask,
             SmpDbgCpu[0].Tick, SmpDbgCpu[1].Tick, SmpDbgCpu[2].Tick, SmpDbgCpu[3].Tick,
             SmpDbgCpu[0].Begin, SmpDbgCpu[1].Begin, SmpDbgCpu[2].Begin, SmpDbgCpu[3].Begin,
             SmpDbgCpu[0].Eoi, SmpDbgCpu[1].Eoi, SmpDbgCpu[2].Eoi, SmpDbgCpu[3].Eoi,
             SmpDbgCpu[0].Reject, SmpDbgCpu[1].Reject, SmpDbgCpu[2].Reject, SmpDbgCpu[3].Reject,
             SmpDbgCpu[0].Ctl, SmpDbgCpu[1].Ctl, SmpDbgCpu[2].Ctl, SmpDbgCpu[3].Ctl,
             SmpDbgCpu[0].Pmr, SmpDbgCpu[1].Pmr, SmpDbgCpu[2].Pmr, SmpDbgCpu[3].Pmr);
    DbgPrint("SMP4DBG timer-gic gicprio=[0x%lx,0x%lx,0x%lx,0x%lx] "
             "gicen=[%lu,%lu,%lu,%lu] gicpend=[%lu,%lu,%lu,%lu] gicact=[%lu,%lu,%lu,%lu]\n",
             SmpDbgCpu[0].GicPrio, SmpDbgCpu[1].GicPrio, SmpDbgCpu[2].GicPrio, SmpDbgCpu[3].GicPrio,
             SmpDbgCpu[0].GicEn, SmpDbgCpu[1].GicEn, SmpDbgCpu[2].GicEn, SmpDbgCpu[3].GicEn,
             SmpDbgCpu[0].GicPend, SmpDbgCpu[1].GicPend, SmpDbgCpu[2].GicPend, SmpDbgCpu[3].GicPend,
             SmpDbgCpu[0].GicAct, SmpDbgCpu[1].GicAct, SmpDbgCpu[2].GicAct, SmpDbgCpu[3].GicAct);
}

/*
 * ============================================================================
 * All-idle deadlock watchdog
 * ============================================================================
 *
 * When the boot wedges with every CPU idle (timers alive, but no thread
 * runnable), a heartbeat can't help - it only shows "all idle". This watchdog
 * is a system thread that sleeps on a timed delay (which still fires, because
 * the timers are alive) and watches the machine-wide context-switch count.
 * During a real deadlock nothing runs but the idle threads (and this watchdog),
 * so context switches flatline. When that persists, it walks every thread and
 * prints its scheduler state + wait reason + waited-on object, so the circular
 * wait can be read straight off the serial log.
 */

extern PKPRCB KiProcessorBlock[];

static LONG SmpDbgWatchdogStarted = 0;

static const char *SmpDbgStateName(UCHAR State)
{
    static const char *Names[] =
    {
        "Init", "Ready", "Running", "Standby", "Terminated",
        "Waiting", "Transition", "DeferredReady", "GateWait"
    };
    return (State < (sizeof(Names) / sizeof(Names[0]))) ? Names[State] : "?";
}

static ULONGLONG SmpDbgSumContextSwitches(VOID)
{
    ULONGLONG Sum = 0;
    ULONG i;

    for (i = 0; i < (ULONG)KeNumberProcessors && i < MAXIMUM_PROCESSORS; i++)
    {
        PKPRCB Prcb = KiProcessorBlock[i];
        if (Prcb != NULL)
            Sum += Prcb->KeContextSwitches;
    }
    return Sum;
}

static VOID SmpDbgThreadStateDump(VOID)
{
    PEPROCESS Process;
    ULONG ProcCount = 0;

    DbgPrint("SMPWD ===== all-CPU-idle deadlock: thread state dump =====\n");
    DbgPrint("SMPWD idle=0x%Ix active=0x%Ix\n",
             (ULONG_PTR)KiIdleSummary, (ULONG_PTR)KeActiveProcessors);

    Process = PsGetNextProcess(NULL);
    while (Process != NULL && ProcCount++ < 128)
    {
        PETHREAD Thread;
        ULONG ThreadCount = 0;

        DbgPrint("SMPWD proc=%p '%s'\n", Process, (PCSTR)PsGetProcessImageFileName(Process));

        Thread = PsGetNextProcessThread(Process, NULL);
        while (Thread != NULL && ThreadCount++ < 256)
        {
            PKTHREAD Tcb = &Thread->Tcb;
            PVOID Object = NULL;

            if (Tcb->State == 5 /* Waiting */ && Tcb->WaitBlockList != NULL)
                Object = Tcb->WaitBlockList->Object;

            DbgPrint("SMPWD   thr=%p state=%s reason=%u waitirql=%u prio=%d obj=%p start=%p\n",
                     Thread, SmpDbgStateName(Tcb->State), (ULONG)Tcb->WaitReason,
                     (ULONG)Tcb->WaitIrql, (int)Tcb->Priority, Object, Thread->StartAddress);

            Thread = PsGetNextProcessThread(Process, Thread);
        }

        Process = PsGetNextProcess(Process);
    }
    DbgPrint("SMPWD ===== end thread state dump =====\n");
}

static VOID NTAPI SmpDbgWatchdogThread(PVOID Context)
{
    LARGE_INTEGER Delay;
    ULONGLONG Prev;
    LONG Stalled = 0;
    BOOLEAN Dumped = FALSE;

    UNREFERENCED_PARAMETER(Context);

    Delay.QuadPart = -2LL * 10 * 1000 * 1000; /* 2 seconds, relative */
    Prev = SmpDbgSumContextSwitches();

    for (;;)
    {
        ULONGLONG Cur;

        KeDelayExecutionThread(KernelMode, FALSE, &Delay);

        Cur = SmpDbgSumContextSwitches();

        /*
         * Deadlock signal: every CPU except the one this watchdog woke on is
         * advertised idle (KiIdleSummary), AND machine-wide context switches are
         * essentially flat. The all-idle test is robust against user-mode retry
         * storms (e.g. the EventLog/SCM RegisterEventSource loop), which keep the
         * context-switch count moving but still leave the kernel scheduler with
         * no runnable thread when the real wedge hits.
         */
        {
            ULONG MyCpu = KeGetCurrentProcessorNumber();
            KAFFINITY Active = (KAFFINITY)KeActiveProcessors;
            KAFFINITY IdleOrMe = (KAFFINITY)KiIdleSummary | ((KAFFINITY)1 << MyCpu);
            BOOLEAN AllIdle = (Active != 0) && ((IdleOrMe & Active) == Active);

            if (AllIdle && (Cur - Prev < 256))
            {
                Stalled++;
            }
            else
            {
                Stalled = 0;
                Dumped = FALSE;
            }
        }
        Prev = Cur;

        if (Stalled >= 2 && !Dumped)
        {
            Dumped = TRUE;
            DbgPrint("SMPWD all CPUs idle ~%lds (idle=0x%Ix active=0x%Ix ctxsw=%I64u) - dumping thread state\n",
                     (long)Stalled * 2, (ULONG_PTR)KiIdleSummary,
                     (ULONG_PTR)KeActiveProcessors, Cur);
            SmpDbgThreadStateDump();
        }
    }
}

VOID NTAPI SmpDbgStartWatchdog(VOID)
{
    HANDLE Handle;

    if (!SmpDbgEnabled)
        return;
    if (InterlockedExchange(&SmpDbgWatchdogStarted, 1) != 0)
        return;

    if (PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, NULL, NULL, NULL,
                             SmpDbgWatchdogThread, NULL) == STATUS_SUCCESS)
    {
        ZwClose(Handle);
        DbgPrint("SMPWD watchdog thread started\n");
    }
}

#endif /* _M_ARM64 */
