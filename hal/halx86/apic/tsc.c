/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            hal/halx86/apic/tsc.c
 * PURPOSE:         HAL Routines for TSC handling
 * PROGRAMMERS:     Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <hal.h>
#include "tsc.h"
#include "apicp.h"
/* Enable debugging to track timing issues */
#undef NDEBUG
#include <debug.h>

LARGE_INTEGER HalpCpuClockFrequency = {{INITIAL_STALL_COUNT * 1000000}};

UCHAR TscCalibrationPhase;
ULONG64 TscCalibrationArray[NUM_SAMPLES];

#define RTC_MODE 6 /* Mode 6 is 1024 Hz */
#define SAMPLE_FREQUENCY ((32768 << 1) >> RTC_MODE)

/* PRIVATE FUNCTIONS *********************************************************/

static
ULONG64
DoLinearRegression(
    ULONG XMax,
    ULONG64 *ArrayY)
{
    ULONG X, SumXX;
    ULONG64 SumXY;

    /* Calculate the sum of the squares of X */
    SumXX = (XMax * (XMax + 1) * (2*XMax + 1)) / 6;

    /* Calculate the sum of the differences to the first value
       weighted by x */
    for (SumXY = 0, X = 1; X <= XMax; X++)
    {
         SumXY += X * (ArrayY[X] - ArrayY[0]);
    }

    /* Account for sample frequency */
    SumXY *= SAMPLE_FREQUENCY;

    /* Return the quotient of the sums */
    return (SumXY + (SumXX/2)) / SumXX;
}

VOID
NTAPI
HalpInitializeTsc(VOID)
{
    ULONG_PTR Flags;
    PVOID PreviousHandler;
    UCHAR RegisterA, RegisterB;

    /* Check if the CPU supports RDTSC */
    if (!(KeGetCurrentPrcb()->FeatureBits & KF_RDTSC))
    {
        KeBugCheck(HAL_INITIALIZATION_FAILED);
    }

     /* Save flags and disable interrupts */
    Flags = __readeflags();
    _disable();

    /* Enable the periodic interrupt in the CMOS */
    RegisterB = HalpReadCmos(RTC_REGISTER_B);
    HalpWriteCmos(RTC_REGISTER_B, RegisterB | RTC_REG_B_PI);

    /* Modify register A to RTC_MODE to get SAMPLE_FREQUENCY */
    RegisterA = HalpReadCmos(RTC_REGISTER_A);
    RegisterA = (RegisterA & 0xF0) | RTC_MODE;
    HalpWriteCmos(RTC_REGISTER_A, RegisterA);

    /* Save old IDT entry */
    PreviousHandler = KeQueryInterruptHandler(APIC_CLOCK_VECTOR);

    /* Set the calibration ISR */
    KeRegisterInterruptHandler(APIC_CLOCK_VECTOR, TscCalibrationISR);

    /* Reset TSC value to 0 */
    __writemsr(MSR_RDTSC, 0);

    /* Enable the timer interrupt */
    HalEnableSystemInterrupt(APIC_CLOCK_VECTOR, CLOCK_LEVEL, Latched);

    /* Read register C, so that the next interrupt can happen */
    HalpReadCmos(RTC_REGISTER_C);

    /* Wait for completion */
    _enable();
    while (TscCalibrationPhase < NUM_SAMPLES) _ReadWriteBarrier();
    _disable();

    /* Disable the periodic interrupt in the CMOS */
    HalpWriteCmos(RTC_REGISTER_B, RegisterB & ~RTC_REG_B_PI);

    /* Disable the timer interrupt */
    HalDisableSystemInterrupt(APIC_CLOCK_VECTOR, CLOCK_LEVEL);

    /* Restore the previous handler */
    KeRegisterInterruptHandler(APIC_CLOCK_VECTOR, PreviousHandler);

    /* Calculate an average, using simplified linear regression */
    HalpCpuClockFrequency.QuadPart = DoLinearRegression(NUM_SAMPLES - 1,
                                                        TscCalibrationArray);
    
    /* Debug: Log the calibration samples */
    DPRINT1("HalpInitializeTsc: Calibration samples (TSC values at 1024Hz intervals):\n");
    for (ULONG i = 0; i < NUM_SAMPLES; i++)
    {
        DPRINT1("  Sample[%lu] = %llu cycles\n", i, TscCalibrationArray[i]);
        if (i > 0)
        {
            ULONG64 Delta = TscCalibrationArray[i] - TscCalibrationArray[i-1];
            DPRINT1("    Delta from previous = %llu cycles (~%llu Hz)\n", 
                    Delta, Delta * SAMPLE_FREQUENCY);
        }
    }
    DPRINT1("HalpInitializeTsc: Calculated CPU frequency = %lld Hz\n", 
            HalpCpuClockFrequency.QuadPart);

    /* Restore flags */
    __writeeflags(Flags);

}

VOID
NTAPI
HalpCalibrateStallExecution(VOID)
{
    ULONG StallFactor;
    
    // Timer interrupt is now active
    DPRINT1("HalpCalibrateStallExecution: Starting TSC calibration...\n");

    HalpInitializeTsc();

    StallFactor = (ULONG)(HalpCpuClockFrequency.QuadPart / 1000000);
    KeGetPcr()->StallScaleFactor = StallFactor;
    
    /* Log the calibration results */
    DPRINT1("HalpCalibrateStallExecution: CPU Frequency = %lld Hz\n", HalpCpuClockFrequency.QuadPart);
    DPRINT1("HalpCalibrateStallExecution: StallScaleFactor = %lu (cycles per microsecond)\n", StallFactor);
    
    /* Sanity check - typical modern CPUs run at 1-4 GHz */
    if (StallFactor < 100 || StallFactor > 10000)
    {
        DPRINT1("HalpCalibrateStallExecution: WARNING - Suspicious StallScaleFactor %lu!\n", StallFactor);
        DPRINT1("HalpCalibrateStallExecution: Expected range is 100-10000 for 0.1-10 GHz CPUs\n");
        
        /* If calibration failed badly, use a reasonable default (2 GHz) */
        if (StallFactor < 10 || StallFactor > 100000)
        {
            DPRINT1("HalpCalibrateStallExecution: Using default 2 GHz calibration\n");
            HalpCpuClockFrequency.QuadPart = 2000000000LL;
            StallFactor = 2000;
            KeGetPcr()->StallScaleFactor = StallFactor;
        }
    }
    
    /* Test the calibration */
    {
        ULONG64 StartTest = __rdtsc();
        KeStallExecutionProcessor(1000); /* 1ms test */
        ULONG64 EndTest = __rdtsc();
        ULONG64 Cycles = EndTest - StartTest;
        DPRINT1("HalpCalibrateStallExecution: 1ms test used %llu cycles (expected ~%lu)\n", 
                Cycles, StallFactor * 1000);
    }
}

/* PUBLIC FUNCTIONS ***********************************************************/

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL)
{
    LARGE_INTEGER Result;

    /* Make sure it's calibrated */
    ASSERT(HalpCpuClockFrequency.QuadPart != 0);

    /* Does the caller want the frequency? */
    if (PerformanceFrequency)
    {
        /* Return tsc frequency */
        *PerformanceFrequency = HalpCpuClockFrequency;
    }

    /* Return the current value */
    Result.QuadPart = __rdtsc();
    return Result;
}

VOID
NTAPI
KeStallExecutionProcessor(ULONG MicroSeconds)
{
    ULONG64 StartTime, EndTime, ActualEndTime;
    ULONG StallFactor;
    static BOOLEAN FirstCall = TRUE;
    static ULONG BigDelayCount = 0;

    /* Get stall factor */
    StallFactor = KeGetPcr()->StallScaleFactor;
    
    /* Log first call and big delays */
    if (FirstCall)
    {
        DPRINT1("KeStallExecutionProcessor: First call, StallFactor=%lu\n", StallFactor);
        FirstCall = FALSE;
    }
    
    if (MicroSeconds >= 100000 && BigDelayCount < 5) /* Log first 5 delays >= 100ms */
    {
        BigDelayCount++;
        DPRINT1("KeStallExecutionProcessor: Large delay requested: %lu us\n", MicroSeconds);
    }

    /* Get the initial time */
    StartTime = __rdtsc();

    /* Calculate the ending time - ensure 64-bit math */
    EndTime = StartTime + ((ULONG64)StallFactor * (ULONG64)MicroSeconds);

    /* Loop until time is elapsed */
    while ((ActualEndTime = __rdtsc()) < EndTime);
    
    /* Check for timing problems on large delays */
    if (MicroSeconds >= 100000)
    {
        ULONG64 ActualCycles = ActualEndTime - StartTime;
        ULONG64 ExpectedCycles = (ULONG64)StallFactor * (ULONG64)MicroSeconds;
        ULONG64 ActualMicroseconds = ActualCycles / StallFactor;
        
        if (ActualMicroseconds > MicroSeconds * 2)
        {
            DPRINT1("KeStallExecutionProcessor: ERROR - Delay was too long!\n");
            DPRINT1("  Requested: %lu us, Actual: ~%llu us (%.1fx longer)\n",
                    MicroSeconds, ActualMicroseconds,
                    (double)ActualMicroseconds / (double)MicroSeconds);
            DPRINT1("  StallFactor=%lu, Expected cycles=%llu, Actual cycles=%llu\n",
                    StallFactor, ExpectedCycles, ActualCycles);
        }
    }
}

VOID
NTAPI
HalCalibratePerformanceCounter(
    IN volatile PLONG Count,
    IN ULONGLONG NewCount)
{
    UNIMPLEMENTED;
    ASSERT(FALSE);
}

