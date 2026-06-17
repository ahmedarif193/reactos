/*
 * PROJECT:     ReactOS Kernel (ARM64)
 * PURPOSE:     SMP boot diagnostics state. Recorded from ntoskrnl and the HAL,
 *              dumped from one place. Gated at runtime by /SMPDIAG (SmpDbgEnabled).
 */

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64)

BOOLEAN SmpDbgEnabled = FALSE; /* set TRUE by the /SMPDIAG boot option */

#define SMPDBG_TIMER_PPI(_i) (((_i) == 27) || ((_i) == 30))

typedef struct _SMPDBG_CPU
{
    volatile ULONG Begin;   /* HAL accepted the timer PPI */
    volatile ULONG Eoi;     /* HAL EOI'd the timer PPI    */
    volatile ULONG Reject;  /* HAL rejected at high IRQL  */
    volatile ULONG Tick;    /* timer ISR ran              */
    volatile ULONG Ctl;     /* last timer CTL             */
    volatile LONG  Tval;    /* last timer TVAL            */
    volatile ULONG Pmr;     /* last GIC PMR               */
    volatile ULONG GicPrio; /* timer PPI GIC priority     */
    volatile ULONG GicEn;   /* timer PPI enabled          */
    volatile ULONG GicPend; /* timer PPI pending          */
    volatile ULONG GicAct;  /* timer PPI active           */
} SMPDBG_CPU;

static SMPDBG_CPU SmpDbgCpu[SMPDBG_MAXCPU];

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

#endif /* _M_ARM64 */
