/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite kernel time API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestTimeIncrement(VOID)
{
    ULONG Increment = KeQueryTimeIncrement();

    ok(Increment >= 1000 && Increment <= 1000000, "KeQueryTimeIncrement=%lu out of range\n", Increment);
}

static
VOID
TestInterruptTime(VOID)
{
    ULONGLONG T1, T2;
    LARGE_INTEGER Interval;

    T1 = KeQueryInterruptTime();
    ok(T1 != 0, "InterruptTime is zero\n");

    Interval.QuadPart = -10 * 1000 * 20;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);

    T2 = KeQueryInterruptTime();
    ok(T2 > T1, "InterruptTime did not advance: %I64u -> %I64u\n", T1, T2);
}

static
VOID
TestSystemTime(VOID)
{
    LARGE_INTEGER S1, S2;
    LARGE_INTEGER Interval;

    KeQuerySystemTime(&S1);
    ok(S1.QuadPart > 0x01C0000000000000LL, "SystemTime implausible: %I64x\n", S1.QuadPart);

    Interval.QuadPart = -10 * 1000 * 20;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);

    KeQuerySystemTime(&S2);
    ok(S2.QuadPart > S1.QuadPart, "SystemTime did not advance: %I64d -> %I64d\n", S1.QuadPart, S2.QuadPart);
}

static
VOID
TestTickCount(VOID)
{
    LARGE_INTEGER C1, C2;
    LARGE_INTEGER Interval;

    KeQueryTickCount(&C1);
    ok(C1.QuadPart > 0, "TickCount is zero\n");

    Interval.QuadPart = -10 * 1000 * 40;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);

    KeQueryTickCount(&C2);
    ok(C2.QuadPart > C1.QuadPart, "TickCount did not advance: %I64d -> %I64d\n", C1.QuadPart, C2.QuadPart);
}

static
VOID
TestPerformanceCounter(VOID)
{
    LARGE_INTEGER Freq, P1, P2;
    ULONG i;

    P1 = KeQueryPerformanceCounter(&Freq);
    ok(Freq.QuadPart > 0, "QPC frequency not positive: %I64d\n", Freq.QuadPart);
    ok(P1.QuadPart > 0, "QPC not positive: %I64d\n", P1.QuadPart);

    for (i = 0; i < 1000; i++)
        YieldProcessor();

    P2 = KeQueryPerformanceCounter(NULL);
    ok(P2.QuadPart >= P1.QuadPart, "QPC went backwards: %I64d -> %I64d\n", P1.QuadPart, P2.QuadPart);
}

static
VOID
TestPreciseTime(VOID)
{
    ULONG64 Qpc1 = 0, Qpc2 = 0;
    ULONGLONG I1, I2, Plain;
    LARGE_INTEGER S1, S2;

    I1 = KeQueryInterruptTimePrecise(&Qpc1);
    I2 = KeQueryInterruptTimePrecise(&Qpc2);
    ok(I2 >= I1, "precise InterruptTime went backwards: %I64u -> %I64u\n", I1, I2);
    ok(Qpc2 >= Qpc1, "precise qpc stamp went backwards: %I64u -> %I64u\n", Qpc1, Qpc2);
    Plain = KeQueryInterruptTime();
    ok(Plain + 10 * 1000 * 1000 >= I1, "precise far ahead of plain: %I64u vs %I64u\n", I1, Plain);

    KeQuerySystemTimePrecise(&S1);
    KeQuerySystemTimePrecise(&S2);
    ok(S2.QuadPart >= S1.QuadPart, "precise SystemTime went backwards: %I64d -> %I64d\n", S1.QuadPart, S2.QuadPart);
}

START_TEST(KeTime)
{
    TestTimeIncrement();
    TestInterruptTime();
    TestSystemTime();
    TestTickCount();
    TestPerformanceCounter();
    TestPreciseTime();
}
