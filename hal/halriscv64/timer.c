/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V time counter and NT increment policy
 */

#include <ntifs.h>
#include "halp.h"

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    _Out_opt_ PLARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER Counter;

    if ((HalpRiscvInitializationPhase == 0) ||
        (HalpRiscvTimebaseFrequency == 0))
    {
        Counter.QuadPart = 0;
        if (PerformanceFrequency != NULL)
            PerformanceFrequency->QuadPart = 0;
        return Counter;
    }

    Counter.QuadPart = (LONGLONG)HalpRiscvReadTime();
    if (PerformanceFrequency != NULL)
    {
        PerformanceFrequency->QuadPart =
            (LONGLONG)HalpRiscvTimebaseFrequency;
    }
    return Counter;
}

/* The clock ISR credits the period that just elapsed and programs the next
 * deadline with the value stored here, so a change takes effect one tick
 * later without disturbing the running deadline. */
ULONG
NTAPI
HalSetTimeIncrement(
    _In_ ULONG Increment)
{
    if (Increment < RISCV_HAL_MINIMUM_INCREMENT)
        Increment = RISCV_HAL_MINIMUM_INCREMENT;
    if (Increment > RISCV_HAL_MAXIMUM_INCREMENT)
        Increment = RISCV_HAL_MAXIMUM_INCREMENT;
    HalpRiscvCurrentTimeIncrement = Increment;
    return Increment;
}


VOID
NTAPI
KeStallExecutionProcessor(
    _In_ ULONG MicroSeconds)
{
    ULONG64 Frequency = HalpRiscvTimebaseFrequency;
    ULONG64 Start, Ticks;

    /* No elapsed-time guarantee is possible before discovery of the FDT
     * timebase. An invented frequency can violate device timing contracts. */
    if (Frequency == 0)
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, 0x54494d45, 0, 0, 0);
    Ticks = ((ULONG64)MicroSeconds * Frequency + 999999) / 1000000;
    Start = HalpRiscvReadTime();
    while (HalpRiscvReadTime() - Start < Ticks)
        __asm__ __volatile__("nop");
}
