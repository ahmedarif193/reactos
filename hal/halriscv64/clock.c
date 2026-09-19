/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Supervisor timer clock tick
 *
 * One periodic deadline on the boot hart, expressed in timebase ticks.
 * With Sstc the deadline is written to `stimecmp`; STIP is then defined as
 * (time >= stimecmp), so programming the next deadline is the acknowledge.
 * Without Sstc the SBI TIME extension programs it, and the SBI clears the
 * pending bit when the new deadline is set.
 */

#include <ntifs.h>
#include "halp.h"

ULONG HalpRiscvFeatureFlags;
BOOLEAN HalpRiscvClockUsesSstc;
ULONG64 HalpRiscvClockDeadline;
ULONG64 HalpRiscvClockPeriod;
ULONG HalpRiscvClockIncrement;
static BOOLEAN HalpProfileEnabled;
static ULONG HalpProfileInterval = 100000; /* 10 ms, in 100 ns units. */
static ULONG64 HalpProfileElapsed;

VOID NTAPI KeProfileInterruptWithSource(PKTRAP_FRAME TrapFrame, KPROFILE_SOURCE Source);

/* ProfileTime samples the interrupted PC on the normal supervisor timer.
 * PMU event sources require a separate implementation and are not advertised. */
VOID NTAPI HalStartProfileInterrupt(KPROFILE_SOURCE Source)
{
    if (Source == ProfileTime)
    {
        HalpProfileElapsed = 0;
        HalpProfileEnabled = TRUE;
    }
}

VOID NTAPI HalStopProfileInterrupt(KPROFILE_SOURCE Source)
{
    if (Source == ProfileTime) HalpProfileEnabled = FALSE;
}

ULONG_PTR NTAPI HalSetProfileInterval(ULONG_PTR Interval)
{
    KIRQL OldIrql;
    /* The periodic clock cannot provide samples more often than one tick. */
    Interval = max(Interval, 100000);
    Interval = min(Interval, MAXULONG - 99999);
    Interval = ((Interval + 99999) / 100000) * 100000;
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    HalpProfileInterval = (ULONG)Interval;
    HalpProfileElapsed = 0;
    KeLowerIrql(OldIrql);
    return Interval;
}

/* 100 ns units to timebase ticks, rounded up so a tick is never short. */
static
ULONG64
HalpRiscvIncrementToTicks(
    _In_ ULONG Increment)
{
    return ((ULONG64)Increment * HalpRiscvTimebaseFrequency + 9999999ULL) / 10000000ULL;
}

static
VOID
HalpRiscvWriteClockDeadline(
    _In_ ULONG64 Deadline)
{
    RISCV_SBI_RETURN Result;

    if (HalpRiscvClockUsesSstc)
    {
        __asm__ __volatile__("csrw stimecmp, %0" :: "r"(Deadline) : "memory");
        return;
    }

    Result = HalpRiscvSetTimer(Deadline);
    if (Result.Error != 0)
    {
        KeBugCheckEx(HAL_INITIALIZATION_FAILED,
                     (ULONG_PTR)Result.Error,
                     (ULONG_PTR)Deadline,
                     (ULONG_PTR)HalpRiscvClockPeriod,
                     1);
    }
}

VOID
HalpRiscvStartClock(VOID)
{
    HalpRiscvClockUsesSstc = (HalpRiscvFeatureFlags & RISCV_HAL_FEATURE_SSTC) != 0;
    HalpRiscvClockIncrement = HalpRiscvCurrentTimeIncrement;
    HalpRiscvClockPeriod = HalpRiscvIncrementToTicks(HalpRiscvClockIncrement);
    HalpRiscvClockDeadline = HalpRiscvReadTime() + HalpRiscvClockPeriod;
    HalpRiscvWriteClockDeadline(HalpRiscvClockDeadline);
}

/* Called by the kernel at CLOCK_LEVEL with interrupts masked. */
VOID
NTAPI
HalpRiscvClockInterrupt(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG Increment = HalpRiscvClockIncrement;
    ULONG64 Now;

    /* The elapsed period is credited; a HalSetTimeIncrement change applies
     * to the period that starts now. */
    if (HalpRiscvCurrentTimeIncrement != Increment)
    {
        HalpRiscvClockIncrement = HalpRiscvCurrentTimeIncrement;
        HalpRiscvClockPeriod = HalpRiscvIncrementToTicks(HalpRiscvClockIncrement);
    }

    /* Absolute periodic deadline. When the hart fell behind (debugger stall,
     * long masked section), rebase on the current time instead of taking a
     * burst of back-to-back ticks; KeQueryPerformanceCounter stays exact. */
    HalpRiscvClockDeadline += HalpRiscvClockPeriod;
    Now = HalpRiscvReadTime();
    if ((LONG64)(HalpRiscvClockDeadline - Now) <= 0)
        HalpRiscvClockDeadline = Now + HalpRiscvClockPeriod;
    HalpRiscvWriteClockDeadline(HalpRiscvClockDeadline);

    if (HalpProfileEnabled)
    {
        HalpProfileElapsed += Increment;
        if (HalpProfileElapsed >= HalpProfileInterval)
        {
            KIRQL OldIrql;
            HalpProfileElapsed %= HalpProfileInterval;
            KeRaiseIrql(PROFILE_LEVEL, &OldIrql);
            KeProfileInterruptWithSource(TrapFrame, ProfileTime);
            KeLowerIrql(OldIrql);
        }
    }
    KeUpdateSystemTime(TrapFrame, Increment, TrapFrame->PreviousIrql);
}
