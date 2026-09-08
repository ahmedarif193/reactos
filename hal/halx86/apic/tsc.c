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
#define NDEBUG
#include <debug.h>

LARGE_INTEGER HalpCpuClockFrequency = {{INITIAL_STALL_COUNT * 1000000}};

UCHAR TscCalibrationPhase;
ULONG64 TscCalibrationArray[NUM_SAMPLES];

#define HALP_PERFORMANCE_FREQUENCY 10000000ULL
#define RTC_MODE 6 /* Mode 6 is 1024 Hz */
#define SAMPLE_FREQUENCY ((32768 << 1) >> RTC_MODE)

#define PM_TIMER_FREQUENCY 3579545UL
#define PM_CALIBRATION_TICKS (PM_TIMER_FREQUENCY / 20) /* 50 ms */
#define PM_CALIBRATION_MIN_TICKS (PM_TIMER_FREQUENCY / 100) /* 10 ms */
#define PM_CALIBRATION_MAX_POLLS 1000000UL

/* PRIVATE FUNCTIONS *********************************************************/

static
ULONG64
HalpReadOrderedTsc(VOID)
{
    int CpuInfo[4];

    /* CPUID also works on pre-SSE2 processors supported by the x86 HAL. */
#if defined(__GNUC__) || defined(__clang__)
    __asm__ __volatile__("cpuid"
                         : "=a" (CpuInfo[0]), "=b" (CpuInfo[1]),
                           "=c" (CpuInfo[2]), "=d" (CpuInfo[3])
                         : "a" (0)
                         : "memory");
#else
    {
        volatile int SerializingValue;
        __cpuid(CpuInfo, 0);
        SerializingValue = CpuInfo[0];
        _ReadWriteBarrier();
    }
#endif
    return __rdtsc();
}

static
BOOLEAN
HalpCalibrateTscWithPmTimer(OUT PULONG64 Frequency)
{
    ULONG Port, Mask, Start, Elapsed, Poll, Sample, Attempt;
    ULONG64 StartTsc, DeltaTsc, Samples[3], Swap;

    if (!HalpGetPmTimer(&Port, &Mask))
        return FALSE;

    Sample = 0;
    for (Attempt = 0; Attempt < 9 && Sample < RTL_NUMBER_OF(Samples); ++Attempt)
    {
        Start = READ_PORT_ULONG((PULONG)(ULONG_PTR)Port) & Mask;
        StartTsc = HalpReadOrderedTsc();

        /* Read the counter itself, not the number of delivered interrupts.
         * A bounded poll also handles absent or stopped firmware timers. */
        for (Poll = 0; Poll < PM_CALIBRATION_MAX_POLLS; ++Poll)
        {
            Elapsed = (READ_PORT_ULONG((PULONG)(ULONG_PTR)Port) - Start) & Mask;
            if (Elapsed >= PM_CALIBRATION_TICKS)
                break;
        }
        DeltaTsc = HalpReadOrderedTsc() - StartTsc;

        /* Reject backwards/glitching timers and heavily interrupted samples.
         * The mask above handles either 24-bit or 32-bit counter wraparound. */
        /* Fast port emulation can exhaust the polling budget before 50 ms.
         * A shorter, sufficiently long interval is still valid: the divisor
         * is the measured PM delta, not the requested duration or poll count. */
        if (Elapsed < PM_CALIBRATION_MIN_TICKS)
            return FALSE;

        if (Elapsed > PM_TIMER_FREQUENCY / 5 ||
            !DeltaTsc || DeltaTsc > MAXULONGLONG / PM_TIMER_FREQUENCY)
        {
            continue;
        }

        Samples[Sample] = DeltaTsc * PM_TIMER_FREQUENCY / Elapsed;
        if (Samples[Sample])
            ++Sample;
    }

    if (Sample != RTL_NUMBER_OF(Samples))
        return FALSE;

    /* Use the median so a single SMI or VM descheduling at an endpoint does
     * not determine the frequency used by every subsequent QPC call. */
    for (Sample = 0; Sample < 2; ++Sample)
    {
        for (Poll = Sample + 1; Poll < 3; ++Poll)
        {
            if (Samples[Poll] < Samples[Sample])
            {
                Swap = Samples[Sample];
                Samples[Sample] = Samples[Poll];
                Samples[Poll] = Swap;
            }
        }
    }
    *Frequency = Samples[1];
    return TRUE;
}

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
    ULONG64 Frequency;

    /* Check if the CPU supports RDTSC */
    if (!(KeGetCurrentPrcb()->FeatureBits & KF_RDTSC))
    {
        KeBugCheck(HAL_INITIALIZATION_FAILED);
    }

     /* Save flags and disable interrupts */
    Flags = __readeflags();
    _disable();

    if (HalpCalibrateTscWithPmTimer(&Frequency))
    {
        HalpCpuClockFrequency.QuadPart = Frequency;
        __writeeflags(Flags);
        return;
    }

    TscCalibrationPhase = 0;

    /* Enable the periodic interrupt in the CMOS */
    RegisterB = HalpReadCmos(RTC_REGISTER_B);
    HalpWriteCmos(RTC_REGISTER_B, RegisterB | RTC_REG_B_PI);

    /* Modify register A to RTC_MODE to get SAMPLE_FREQUENCY */
    RegisterA = HalpReadCmos(RTC_REGISTER_A);
    HalpWriteCmos(RTC_REGISTER_A, (RegisterA & 0xF0) | RTC_MODE);

    /* Save old IDT entry */
    PreviousHandler = KeQueryInterruptHandler(APIC_CLOCK_VECTOR);

    /* Set the calibration ISR */
    KeRegisterInterruptHandler(APIC_CLOCK_VECTOR, TscCalibrationISR);

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
    HalpWriteCmos(RTC_REGISTER_A, RegisterA);

    /* Disable the timer interrupt */
    HalDisableSystemInterrupt(APIC_CLOCK_VECTOR, CLOCK_LEVEL);

    /* Restore the previous handler */
    KeRegisterInterruptHandler(APIC_CLOCK_VECTOR, PreviousHandler);

    /* Calculate an average, using simplified linear regression */
    HalpCpuClockFrequency.QuadPart = DoLinearRegression(NUM_SAMPLES - 1,
                                                        TscCalibrationArray);

    /* Restore flags */
    __writeeflags(Flags);

}

CODE_SEG("INIT")
VOID
NTAPI
HalpCalibrateStallExecution(VOID)
{
    // Timer interrupt is now active

    HalpInitializeTsc();

    KeGetPcr()->StallScaleFactor = (ULONG)(HalpCpuClockFrequency.QuadPart / 1000000);
}

/* PUBLIC FUNCTIONS ***********************************************************/

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(
    OUT PLARGE_INTEGER PerformanceFrequency OPTIONAL)
{
    LARGE_INTEGER Result;
    ULONG64 Tsc, Seconds, Remainder, Frequency;

    /* Make sure it's calibrated */
    ASSERT(HalpCpuClockFrequency.QuadPart != 0);

    /* Does the caller want the frequency? */
    if (PerformanceFrequency)
    {
        /* Return tsc frequency */
        PerformanceFrequency->QuadPart = HALP_PERFORMANCE_FREQUENCY;
    }

    /* Return the current value */
    Frequency = HalpCpuClockFrequency.QuadPart;
    Tsc = __rdtsc();
    Seconds = Tsc / Frequency;
    Remainder = Tsc % Frequency;
    Result.QuadPart = Seconds * HALP_PERFORMANCE_FREQUENCY + (Remainder * HALP_PERFORMANCE_FREQUENCY) / Frequency;
    return Result;
}

VOID
NTAPI
KeStallExecutionProcessor(ULONG MicroSeconds)
{
    ULONG64 StartTime, EndTime;

    /* Get the initial time */
    StartTime = __rdtsc();

    /* Calculate the ending time */
    EndTime = StartTime + KeGetPcr()->StallScaleFactor * MicroSeconds;

    /* Loop until time is elapsed */
    while (__rdtsc() < EndTime);
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

