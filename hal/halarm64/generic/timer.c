/*
 * PROJECT:     ReactOS ARM64 HAL
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Timer Support for ARM64
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <hal.h>

/* GLOBALS *******************************************************************/

LARGE_INTEGER HalpClockFrequency;
ULONG HalpCurrentTimeIncrement;

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
HalpInitializeTimer(VOID)
{
    ULONG64 Frequency;
    
    /* Read counter frequency from system register */
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r" (Frequency));
    
    HalpClockFrequency.QuadPart = Frequency;
    
    /* Calculate time increment for 10ms (100Hz) */
    HalpCurrentTimeIncrement = 100000; /* 100us units */
    
    /* Program the physical timer for periodic interrupts */
    /* Set compare value for next interrupt */
    ULONG64 CurrentCount, CompareValue;
    __asm__ __volatile__("mrs %0, cntpct_el0" : "=r" (CurrentCount));
    
    CompareValue = CurrentCount + (Frequency / 100); /* 10ms */
    __asm__ __volatile__("msr cntp_cval_el0, %0" :: "r" (CompareValue));
    
    /* Enable timer */
    __asm__ __volatile__("msr cntp_ctl_el0, %0" :: "r" (1ULL));
}

VOID
NTAPI
HalCalibratePerformanceCounter(
    IN volatile PLONG Count,
    IN ULONGLONG NewCount)
{
    /* Set performance counter calibration */
    /* TODO: Implement calibration */
}

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL)
{
    LARGE_INTEGER Result;
    
    /* Read system counter */
    __asm__ __volatile__("mrs %0, cntpct_el0" : "=r" (Result.QuadPart));
    
    if (PerformanceFrequency)
    {
        *PerformanceFrequency = HalpClockFrequency;
    }
    
    return Result;
}

VOID
NTAPI
KeStallExecutionProcessor(
    IN ULONG MicroSeconds)
{
    ULONG64 StartCount, CurrentCount, EndCount;
    ULONG64 Frequency;
    
    /* Get counter frequency */
    __asm__ __volatile__("mrs %0, cntfrq_el0" : "=r" (Frequency));
    
    /* Calculate number of ticks to wait */
    ULONG64 TicksToWait = (Frequency * MicroSeconds) / 1000000;
    
    /* Get start count */
    __asm__ __volatile__("mrs %0, cntpct_el0" : "=r" (StartCount));
    EndCount = StartCount + TicksToWait;
    
    /* Spin until we reach the target count */
    do
    {
        __asm__ __volatile__("mrs %0, cntpct_el0" : "=r" (CurrentCount));
    } while (CurrentCount < EndCount);
}

ULONG
NTAPI
HalSetTimeIncrement(
    IN ULONG DesiredIncrement)
{
    /* Set new time increment */
    HalpCurrentTimeIncrement = DesiredIncrement;
    return DesiredIncrement;
}

/* EOF */