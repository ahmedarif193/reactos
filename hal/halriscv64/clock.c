/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
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

    KeUpdateSystemTime(TrapFrame, Increment, TrapFrame->PreviousIrql);
}
