/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Supervisor timer clock tick
 *
 * One periodic deadline per hart, expressed in timebase ticks.
 * SBI TIME programs the deadline and clears the pending interrupt. Firmware
 * selects the available timer mechanism independently for each hart.
 */

#include <ntifs.h>
#include "halp.h"

typedef struct _HAL_RISCV_CLOCK
{
    ULONG64 Deadline;
    ULONG64 Period;
    ULONG Increment;
    ULONG64 ProfileElapsed;
} HAL_RISCV_CLOCK;
static HAL_RISCV_CLOCK HalpRiscvClocks[MAXIMUM_PROCESSORS];
static BOOLEAN HalpProfileEnabled;
static ULONG HalpProfileInterval = 100000; /* 10 ms, in 100 ns units. */


VOID NTAPI KeUpdateRunTime(PKTRAP_FRAME TrapFrame, KIRQL Irql);

VOID NTAPI KeProfileInterruptWithSource(PKTRAP_FRAME TrapFrame, KPROFILE_SOURCE Source);

/* ProfileTime samples the interrupted PC on the normal supervisor timer.
 * PMU event sources require a separate implementation and are not advertised. */
VOID NTAPI HalStartProfileInterrupt(KPROFILE_SOURCE Source)
{
    if (Source == ProfileTime)
    {
        HalpRiscvClocks[KeGetCurrentProcessorNumber()].ProfileElapsed = 0;
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
    HalpRiscvClocks[KeGetCurrentProcessorNumber()].ProfileElapsed = 0;
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

    Result = HalpRiscvSetTimer(Deadline);
    if (Result.Error != 0)
    {
        KeBugCheckEx(HAL_INITIALIZATION_FAILED,
                     (ULONG_PTR)Result.Error,
                     (ULONG_PTR)Deadline,
                     (ULONG_PTR)HalpRiscvClocks[KeGetCurrentProcessorNumber()].Period,
                     1);
    }
}

VOID
HalpRiscvStartClock(VOID)
{
    HAL_RISCV_CLOCK *Clock = &HalpRiscvClocks[KeGetCurrentProcessorNumber()];
    Clock->Increment = HalpRiscvCurrentTimeIncrement;
    Clock->Period = HalpRiscvIncrementToTicks(Clock->Increment);
    Clock->Deadline = HalpRiscvReadTime() + Clock->Period;
    HalpRiscvWriteClockDeadline(Clock->Deadline);
}

/* Called by the kernel at CLOCK_LEVEL with interrupts masked. */
VOID
NTAPI
HalpRiscvClockInterrupt(
    _In_ PKTRAP_FRAME TrapFrame)
{
    ULONG Number = KeGetCurrentProcessorNumber();
    HAL_RISCV_CLOCK *Clock = &HalpRiscvClocks[Number];
    ULONG Increment = Clock->Increment;
    ULONG64 Now;

    /* The elapsed period is credited; a HalSetTimeIncrement change applies
     * to the period that starts now. */
    if (HalpRiscvCurrentTimeIncrement != Increment)
    {
        Clock->Increment = HalpRiscvCurrentTimeIncrement;
        Clock->Period = HalpRiscvIncrementToTicks(Clock->Increment);
    }

    /* Absolute periodic deadline. When the hart fell behind (debugger stall,
     * long masked section), rebase on the current time instead of taking a
     * burst of back-to-back ticks; KeQueryPerformanceCounter stays exact. */
    Clock->Deadline += Clock->Period;
    Now = HalpRiscvReadTime();
    if ((LONG64)(Clock->Deadline - Now) <= 0)
        Clock->Deadline = Now + Clock->Period;
    HalpRiscvWriteClockDeadline(Clock->Deadline);

    if (HalpProfileEnabled)
    {
        Clock->ProfileElapsed += Increment;
        if (Clock->ProfileElapsed >= HalpProfileInterval)
        {
            KIRQL OldIrql;
            Clock->ProfileElapsed %= HalpProfileInterval;
            KeRaiseIrql(PROFILE_LEVEL, &OldIrql);
            KeProfileInterruptWithSource(TrapFrame, ProfileTime);
            KeLowerIrql(OldIrql);
        }
    }
    if (Number == 0)
        KeUpdateSystemTime(TrapFrame, Increment, TrapFrame->PreviousIrql);
    else
        KeUpdateRunTime(TrapFrame, TrapFrame->PreviousIrql);
}
