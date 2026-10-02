/*
 * PROJECT:     LiberNT PowerPC HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Decrementer clock, time base counter and stall
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

static ULONG HalpPpcCurrentTimeIncrement = PPC_HAL_MAXIMUM_INCREMENT;

/* Decrementer ticks for a time increment in 100 ns units. */
static
ULONG
HalpPpcIncrementToTicks(_In_ ULONG Increment)
{
    return (ULONG)(((ULONGLONG)HalpPpcTimebaseFrequency * Increment) / 10000000ULL);
}

ULONGLONG
HalpPpcReadTimebase(VOID)
{
    ULONG Upper, Lower, Check;

    do
    {
        __asm__ __volatile__("mftbu %0\n\tmftb %1\n\tmftbu %2" : "=r"(Upper), "=r"(Lower), "=r"(Check));
    } while (Upper != Check);
    return ((ULONGLONG)Upper << 32) | Lower;
}

/*
 * Measure the time base against 8254 channel 2 (1.193182 MHz, gated through
 * port 0x61): firmware frequency reports are not trustworthy on every PReP
 * board or emulator. Returns 0 when the counter does not run.
 */
ULONG
HalpPpcCalibrateTimebase(VOID)
{
    const ULONG Count = 11932;  /* 10 ms */
    ULONGLONG Start, Elapsed;
    UCHAR Control;
    ULONG Spin;

    Control = READ_PORT_UCHAR((PUCHAR)0x61);
    WRITE_PORT_UCHAR((PUCHAR)0x61, (Control & ~0x02) | 0x01);
    WRITE_PORT_UCHAR((PUCHAR)0x43, 0xB0);
    WRITE_PORT_UCHAR((PUCHAR)0x42, (UCHAR)Count);
    WRITE_PORT_UCHAR((PUCHAR)0x42, (UCHAR)(Count >> 8));

    /* Restart the gate so the count starts now, then wait for OUT2. */
    WRITE_PORT_UCHAR((PUCHAR)0x61, (Control & ~0x03));
    WRITE_PORT_UCHAR((PUCHAR)0x61, (Control & ~0x02) | 0x01);
    Start = HalpPpcReadTimebase();
    for (Spin = 0; Spin < 100000000; Spin++)
    {
        if (READ_PORT_UCHAR((PUCHAR)0x61) & 0x20)
            break;
    }
    Elapsed = HalpPpcReadTimebase() - Start;
    WRITE_PORT_UCHAR((PUCHAR)0x61, Control);

    if ((Spin == 100000000) || (Elapsed == 0))
        return 0;
    return (ULONG)(Elapsed * 100);
}

VOID
HalpPpcStartClock(VOID)
{
    __asm__ __volatile__("mtdec %0" :: "r"(HalpPpcIncrementToTicks(HalpPpcCurrentTimeIncrement)) : "memory");
}

VOID
NTAPI
HalpPpcParkClock(VOID)
{
    /* Push the next decrementer interrupt out; the kernel replays the tick. */
    __asm__ __volatile__("mtdec %0" :: "r"(0x7FFFFFFFUL) : "memory");
}

VOID
NTAPI
HalpPpcClockInterrupt(_In_ PKTRAP_FRAME TrapFrame)
{
    ULONG Increment = HalpPpcCurrentTimeIncrement;

    HalpPpcStartClock();
    KeUpdateSystemTime(TrapFrame, Increment, TrapFrame->PreviousIrql);
}

ULONG
NTAPI
HalSetTimeIncrement(_In_ ULONG Increment)
{
    if (Increment > PPC_HAL_MAXIMUM_INCREMENT)
        Increment = PPC_HAL_MAXIMUM_INCREMENT;
    if (Increment < PPC_HAL_MINIMUM_INCREMENT)
        Increment = PPC_HAL_MINIMUM_INCREMENT;
    HalpPpcCurrentTimeIncrement = Increment;
    return Increment;
}

LARGE_INTEGER
NTAPI
KeQueryPerformanceCounter(_Out_opt_ PLARGE_INTEGER PerformanceFrequency)
{
    LARGE_INTEGER Counter;

    if (PerformanceFrequency)
        PerformanceFrequency->QuadPart = HalpPpcTimebaseFrequency;
    Counter.QuadPart = (LONGLONG)HalpPpcReadTimebase();
    return Counter;
}

VOID
NTAPI
KeStallExecutionProcessor(_In_ ULONG MicroSeconds)
{
    ULONGLONG Start = HalpPpcReadTimebase();
    ULONGLONG Ticks = ((ULONGLONG)HalpPpcTimebaseFrequency * MicroSeconds) / 1000000ULL;

    while (HalpPpcReadTimebase() - Start < Ticks)
        YieldProcessor();
}

/* No profile source is available. */
VOID NTAPI HalStartProfileInterrupt(KPROFILE_SOURCE Source) { UNREFERENCED_PARAMETER(Source); }
VOID NTAPI HalStopProfileInterrupt(KPROFILE_SOURCE Source) { UNREFERENCED_PARAMETER(Source); }
ULONG_PTR NTAPI HalSetProfileInterval(ULONG_PTR Interval) { return Interval; }
