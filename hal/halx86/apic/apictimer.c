/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/apic/apictimer.c
 * PURPOSE:         System Profiling
 * PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include "apicp.h"
#define NDEBUG
#include <debug.h>

extern LARGE_INTEGER HalpCpuClockFrequency;

/* HAL profiling variables */
/* Read by every starting processor while another processor may be inside a
 * start/stop/interval transition: the flag is published with interlocked
 * stores and the 64-bit interval is read through an interlocked compare so
 * 32-bit builds cannot tear it. */
volatile LONG HalIsProfiling = FALSE;
volatile LONG64 HalCurProfileInterval = 10000000;

static
ULONGLONG
HalpReadProfileInterval(VOID)
{
    return (ULONGLONG)InterlockedCompareExchange64((volatile LONG64 *)&HalCurProfileInterval, 0, 0);
}
ULONGLONG HalMinProfileInterval = 1000;
ULONGLONG HalMaxProfileInterval = 10000000;

static
ULONG
HalpCalibrateProfileTimer(VOID)
{
    LVT_REGISTER LvtEntry;
    ULONGLONG TscFrequency, StartTsc, ElapsedTsc, WaitTicks;
    ULONGLONG TimerFrequency;
    ULONG ElapsedTicks;

    TscFrequency = HalpCpuClockFrequency.QuadPart;
    if (!TscFrequency) return 0;

    /* Measure one millisecond with the calibrated TSC.  The profile timer is
     * masked while its local-APIC countdown rate is established. */
    LvtEntry.Long = 0;
    LvtEntry.TimerMode = 1;
    LvtEntry.Vector = APIC_PROFILE_VECTOR;
    LvtEntry.Mask = 1;
    ApicWrite(APIC_TMRLVTR, LvtEntry.Long);
    ApicWrite(APIC_TDCR, TIMER_DV_DivideBy1);
    ApicWrite(APIC_TICR, MAXULONG);

    WaitTicks = max(TscFrequency / 1000, 1);
    StartTsc = __rdtsc();
    do
    {
        ElapsedTsc = __rdtsc() - StartTsc;
        YieldProcessor();
    } while (ElapsedTsc < WaitTicks);

    ElapsedTicks = MAXULONG - ApicRead(APIC_TCCR);
    ApicWrite(APIC_TICR, 0);
    if (!ElapsedTicks || !ElapsedTsc) return 0;

    TimerFrequency = ((ULONGLONG)ElapsedTicks * TscFrequency) / ElapsedTsc;
    if (!TimerFrequency) return 0;
    if (TimerFrequency > MAXULONG) return MAXULONG;
    return (ULONG)TimerFrequency;
}

static
ULONG
HalpProfileIntervalToTicks(
    IN ULONGLONG Interval100ns)
{
    ULONGLONG Frequency, Ticks;

    Frequency = KeGetPcr()->HalReserved[HAL_PROFILING_FREQUENCY];
    if (!Frequency)
    {
        Frequency = HalpCalibrateProfileTimer();
        KeGetPcr()->HalReserved[HAL_PROFILING_FREQUENCY] = (ULONG)Frequency;
    }
    if (!Frequency) return 1;

    /* Split the quotient and remainder so the multiplication cannot wrap. */
    Ticks = (Frequency / 10000000) * Interval100ns;
    Ticks += ((Frequency % 10000000) * Interval100ns) / 10000000;
    if (Ticks == 0) return 1;
    if (Ticks > MAXULONG) return MAXULONG;
    return (ULONG)Ticks;
}

static
ULONG_PTR
NTAPI
HalpSetLocalProfileState(
    IN ULONG_PTR Enable)
{
    LVT_REGISTER LvtEntry;
    ULONG TimerInterval;

    LvtEntry.Long = 0;
    LvtEntry.TimerMode = 1;
    LvtEntry.Vector = APIC_PROFILE_VECTOR;
    LvtEntry.Mask = Enable ? 0 : 1;
    if (Enable)
    {
        TimerInterval = KeGetPcr()->HalReserved[HAL_PROFILING_INTERVAL];
        if (!TimerInterval)
        {
            TimerInterval = HalpProfileIntervalToTicks(HalpReadProfileInterval());
            KeGetPcr()->HalReserved[HAL_PROFILING_INTERVAL] = TimerInterval;
        }
        ApicWrite(APIC_TICR, TimerInterval);
    }
    ApicWrite(APIC_TMRLVTR, LvtEntry.Long);
    return 0;
}

static
ULONG_PTR
NTAPI
HalpSetLocalProfileInterval(
    IN ULONG_PTR Interval)
{
    ULONG TimerInterval;

    TimerInterval = HalpProfileIntervalToTicks((ULONGLONG)Interval);
    KeGetPcr()->HalReserved[HAL_PROFILING_INTERVAL] = TimerInterval;
    ApicWrite(APIC_TICR, TimerInterval);
    return 0;
}

static
VOID
HalpBroadcastProfileOperation(
    IN PKIPI_BROADCAST_WORKER Worker,
    IN ULONG_PTR Argument)
{
    /* The rendezvous raises to IPI_LEVEL, so it is illegal at or above it
       (boot phase 0 calls in here at HIGH_LEVEL to clear a stale timer,
       running on the sole active processor); program the local APIC only */
    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
    {
        Worker(Argument);
        return;
    }
    KeIpiGenericCall(Worker, Argument);
}

/* TIMER FUNCTIONS ************************************************************/

VOID
NTAPI
ApicSetTimerInterval(ULONG MicroSeconds)
{
    LVT_REGISTER LvtEntry;
    ULONG TimerInterval;

    /* Calculate the Timer interval */
    TimerInterval = HalpProfileIntervalToTicks((ULONGLONG)MicroSeconds * 10);

    /* Set the count interval */
    ApicWrite(APIC_TICR, TimerInterval);

    /* Set to periodic / masked */
    LvtEntry.Long = 0;
    LvtEntry.TimerMode = 1;
    LvtEntry.Vector = APIC_PROFILE_VECTOR;
    LvtEntry.Mask = 1;
    ApicWrite(APIC_TMRLVTR, LvtEntry.Long);

}

VOID
NTAPI
ApicInitializeTimer(ULONG Cpu)
{

    /* Initialize the TSC */
    //HalpInitializeTsc();

    /* Set clock multiplier to 1 */
    ApicWrite(APIC_TDCR, TIMER_DV_DivideBy1);

    ApicSetTimerInterval(1000);

// KeSetTimeIncrement
}

VOID
FASTCALL
HalpProfileInterruptHandler(_In_ PKTRAP_FRAME TrapFrame)
{
    KeProfileInterruptWithSource(TrapFrame, ProfileTime);
}


/* PUBLIC FUNCTIONS ***********************************************************/

VOID
NTAPI
HalInitializeProfiling(VOID)
{
    KeGetPcr()->HalReserved[HAL_PROFILING_INTERVAL] = 0;
    KeGetPcr()->HalReserved[HAL_PROFILING_FREQUENCY] = 0;
    if (InterlockedCompareExchange((volatile LONG *)&HalIsProfiling, 0, 0)) HalpSetLocalProfileState(TRUE);
}

VOID
NTAPI
HalStartProfileInterrupt(IN KPROFILE_SOURCE ProfileSource)
{
    /* Only handle ProfileTime */
    if (ProfileSource == ProfileTime)
    {
        /* Publish the armed state before the broadcast: a processor starting
         * up after the broadcast snapshot arms itself from the flag. */
        InterlockedExchange((volatile LONG *)&HalIsProfiling, TRUE);
        HalpBroadcastProfileOperation(HalpSetLocalProfileState, TRUE);
    }
}

VOID
NTAPI
HalStopProfileInterrupt(IN KPROFILE_SOURCE ProfileSource)
{
    /* Only handle ProfileTime */
    if (ProfileSource == ProfileTime)
    {
        /* Publish the stopped state before the broadcast: a processor that
         * starts between the two must not arm itself from a stale flag and
         * then be missed by the broadcast snapshot. */
        InterlockedExchange((volatile LONG *)&HalIsProfiling, FALSE);
        HalpBroadcastProfileOperation(HalpSetLocalProfileState, FALSE);
    }
}

ULONG_PTR
NTAPI
HalSetProfileInterval(IN ULONG_PTR Interval)
{
    ULONGLONG FixedInterval;

    FixedInterval = (ULONGLONG)Interval;

    /* Check bounds */
    if (FixedInterval < HalMinProfileInterval)
    {
        FixedInterval = HalMinProfileInterval;
    }
    else if (FixedInterval > HalMaxProfileInterval)
    {
        FixedInterval = HalMaxProfileInterval;
    }

    /* Remember interval */
    InterlockedExchange64((volatile LONG64 *)&HalCurProfileInterval, (LONG64)FixedInterval);

    /* Recalculate and publish the interval on every local APIC. */
    HalpBroadcastProfileOperation(HalpSetLocalProfileInterval,
                                  (ULONG_PTR)FixedInterval);

    return (ULONG_PTR)FixedInterval;
}
