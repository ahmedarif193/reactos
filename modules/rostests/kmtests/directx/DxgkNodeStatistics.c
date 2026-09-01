/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     GPU node execution accounting
 *
 * A GPU node's busy clock is what every per-engine utilization figure is
 * computed from, so the ways it can go wrong all show up as a number someone
 * reads off a screen: a charge left open pins an engine at 100%, a charge
 * closed twice makes a busy engine look idle, and a counter that can go
 * backwards produces a negative utilization.
 */

#include <kmt_test.h>
#include "node_stats_core.h"

/* Ticks per second used throughout: the value keeps the arithmetic legible
 * while still exercising the split conversion. */
#define TEST_FREQUENCY 10000000LL

static VOID TestSingleCharge(VOID)
{
    DXGK_NODE_STATS Stats;

    RtlZeroMemory(&Stats, sizeof(Stats));

    /* An idle node has run for no time at all, however late it is asked. */
    ok_eq_longlong(DxgkNodeStatsCoreRunningTicks(&Stats, 5000), 0LL);

    DxgkNodeStatsCoreOpen(&Stats, 0, 1000);
    ok_eq_long(Stats.ActiveCount, 1L);
    ok_eq_longlong(Stats.BusySince, 1000LL);
    /* The charge is open, so the elapsed part of it counts already -- an
     * engine that never retires anything must not look idle. */
    ok_eq_longlong(DxgkNodeStatsCoreRunningTicks(&Stats, 1600), 600LL);
    /* ...without being committed to the counter itself. */
    ok_eq_longlong(Stats.RunningTicks, 0LL);

    DxgkNodeStatsCoreClose(&Stats, 0, 1600);
    ok_eq_long(Stats.ActiveCount, 0L);
    ok_eq_longlong(Stats.RunningTicks, 600LL);
    ok_eq_longlong(Stats.BusySince, 0LL);
    /* A closed charge does not keep accruing. */
    ok_eq_longlong(DxgkNodeStatsCoreRunningTicks(&Stats, 9999), 600LL);
}

static VOID TestOverlappingCharges(VOID)
{
    DXGK_NODE_STATS Stats;

    RtlZeroMemory(&Stats, sizeof(Stats));

    /*
     * Two packets in flight on one node is one busy node, not two.  Charging
     * the interval once per packet would let a node report more busy time
     * than wall time has passed.
     */
    DxgkNodeStatsCoreOpen(&Stats, 0, 100);
    DxgkNodeStatsCoreOpen(&Stats, 0, 150);
    ok_eq_long(Stats.ActiveCount, 2L);
    ok_eq_longlong(Stats.BusySince, 100LL);

    DxgkNodeStatsCoreClose(&Stats, 0, 200);
    /* The node is still busy: the first packet's departure ends nothing. */
    ok_eq_long(Stats.ActiveCount, 1L);
    ok_eq_longlong(Stats.RunningTicks, 0LL);
    ok_eq_longlong(Stats.BusySince, 100LL);

    DxgkNodeStatsCoreClose(&Stats, 0, 300);
    ok_eq_long(Stats.ActiveCount, 0L);
    ok_eq_longlong(Stats.RunningTicks, 200LL);   /* 300 - 100, charged once */
}

static VOID TestAccumulation(VOID)
{
    DXGK_NODE_STATS Stats;

    RtlZeroMemory(&Stats, sizeof(Stats));

    DxgkNodeStatsCoreOpen(&Stats, 0, 10);
    DxgkNodeStatsCoreClose(&Stats, 0, 30);
    DxgkNodeStatsCoreOpen(&Stats, 0, 100);
    DxgkNodeStatsCoreClose(&Stats, 0, 105);
    /* Separate busy periods add; the gap between them does not. */
    ok_eq_longlong(Stats.RunningTicks, 25LL);
}

static VOID TestClockGoingBackwards(VOID)
{
    DXGK_NODE_STATS Stats;

    RtlZeroMemory(&Stats, sizeof(Stats));

    /*
     * A retirement timestamped before its dispatch cannot be turned into a
     * duration.  Dropping that one interval keeps the total monotonic, which
     * is what makes a delta between two samples meaningful at all.
     */
    DxgkNodeStatsCoreOpen(&Stats, 0, 5000);
    DxgkNodeStatsCoreClose(&Stats, 0, 4000);
    ok_eq_longlong(Stats.RunningTicks, 0LL);
    ok_eq_long(Stats.ActiveCount, 0L);

    /* A snapshot taken before the charge opened is not negative either. */
    DxgkNodeStatsCoreOpen(&Stats, 0, 5000);
    ok_eq_longlong(DxgkNodeStatsCoreRunningTicks(&Stats, 4000), 0LL);
}

static VOID TestPacketTypes(VOID)
{
    DXGK_NODE_STATS Stats;
    ULONG Index;

    RtlZeroMemory(&Stats, sizeof(Stats));

    /* Each packet type is counted on its own so paging traffic can be told
     * apart from a client's own work. */
    DxgkNodeStatsCoreOpen(&Stats, 0, 10);
    DxgkNodeStatsCoreClose(&Stats, 0, 20);
    DxgkNodeStatsCoreOpen(&Stats, 1, 30);
    DxgkNodeStatsCoreClose(&Stats, 1, 40);
    DxgkNodeStatsCoreOpen(&Stats, 1, 50);
    DxgkNodeStatsCoreClose(&Stats, 1, 60);

    ok_eq_longlong(Stats.PacketsDispatched[0], 1LL);
    ok_eq_longlong(Stats.PacketsRetired[0], 1LL);
    ok_eq_longlong(Stats.PacketsDispatched[1], 2LL);
    ok_eq_longlong(Stats.PacketsRetired[1], 2LL);
    for (Index = 2; Index < DXGK_NODE_STATS_PACKET_TYPES; Index++)
    {
        ok_eq_longlong(Stats.PacketsDispatched[Index], 0LL);
        ok_eq_longlong(Stats.PacketsRetired[Index], 0LL);
    }
    /* Context switches count every dispatch, whatever its type. */
    ok_eq_longlong(Stats.ContextSwitches, 3LL);
    ok_eq_longlong(Stats.RunningTicks, 30LL);

    /* A type the adapter cannot produce indexes past the arrays; it is
     * refused rather than written through. */
    DxgkNodeStatsCoreOpen(&Stats, DXGK_NODE_STATS_PACKET_TYPES, 70);
    ok_eq_long(Stats.ActiveCount, 0L);
    ok_eq_longlong(Stats.ContextSwitches, 3LL);
    DxgkNodeStatsCoreClose(&Stats, DXGK_NODE_STATS_PACKET_TYPES, 80);
    ok_eq_long(Stats.ActiveCount, 0L);
    ok_eq_longlong(Stats.RunningTicks, 30LL);
}

static VOID TestPreemption(VOID)
{
    DXGK_NODE_STATS Stats;

    RtlZeroMemory(&Stats, sizeof(Stats));

    DxgkNodeStatsCoreRequestPreemption(&Stats);
    ok_eq_longlong(Stats.PreemptionsRequested, 1LL);
    ok_eq_longlong(Stats.PreemptionsCompleted, 0LL);

    /* A preemption that caught a packet took that packet back. */
    DxgkNodeStatsCoreCompletePreemption(&Stats, 0, TRUE);
    ok_eq_longlong(Stats.PreemptionsCompleted, 1LL);
    ok_eq_longlong(Stats.PacketsPreempted[0], 1LL);

    /* One that caught nothing interrupted nothing. */
    DxgkNodeStatsCoreRequestPreemption(&Stats);
    DxgkNodeStatsCoreCompletePreemption(&Stats, 0, FALSE);
    ok_eq_longlong(Stats.PreemptionsRequested, 2LL);
    ok_eq_longlong(Stats.PreemptionsCompleted, 2LL);
    ok_eq_longlong(Stats.PacketsPreempted[0], 1LL);
}

static VOID TestConversion(VOID)
{
    /* One second of ticks is one second of 100ns units. */
    ok_eq_longlong(DxgkNodeStatsCoreTicksTo100ns(TEST_FREQUENCY, TEST_FREQUENCY), 10000000LL);
    ok_eq_longlong(DxgkNodeStatsCoreTicksTo100ns(TEST_FREQUENCY / 2, TEST_FREQUENCY), 5000000LL);

    /* A counter far faster than the unit must not truncate the whole
     * measurement away: a single tick of a 1 GHz counter is 10 units. */
    ok_eq_longlong(DxgkNodeStatsCoreTicksTo100ns(1, 1000000000LL), 0LL);
    ok_eq_longlong(DxgkNodeStatsCoreTicksTo100ns(1000, 1000000000LL), 10LL);

    /* Nothing measured is nothing reported, and a frequency that was never
     * captured cannot be divided by. */
    ok_eq_longlong(DxgkNodeStatsCoreTicksTo100ns(0, TEST_FREQUENCY), 0LL);
    ok_eq_longlong(DxgkNodeStatsCoreTicksTo100ns(-5, TEST_FREQUENCY), 0LL);
    ok_eq_longlong(DxgkNodeStatsCoreTicksTo100ns(TEST_FREQUENCY, 0), 0LL);
    ok_eq_longlong(DxgkNodeStatsCoreTicksTo100ns(TEST_FREQUENCY, -1), 0LL);

    /*
     * Three weeks of busy time on a 24 MHz counter.  Scaling before dividing
     * would overflow here, which is why the conversion splits the quotient
     * from the remainder.
     */
    {
        LONG64 Frequency = 24000000LL;
        LONG64 Seconds = 21LL * 24LL * 60LL * 60LL;
        LONG64 Observed = DxgkNodeStatsCoreTicksTo100ns(Seconds * Frequency, Frequency);

        ok_eq_longlong(Observed, Seconds * 10000000LL);
    }
}

static VOID TestNullSafety(VOID)
{
    /* The accounting sits on the submission and completion paths; a missing
     * record there must not take the scheduler down with it. */
    DxgkNodeStatsCoreOpen(NULL, 0, 100);
    DxgkNodeStatsCoreClose(NULL, 0, 200);
    DxgkNodeStatsCoreRequestPreemption(NULL);
    DxgkNodeStatsCoreCompletePreemption(NULL, 0, TRUE);
    ok_eq_longlong(DxgkNodeStatsCoreRunningTicks(NULL, 100), 0LL);
}

START_TEST(DxgkNodeStatistics)
{
    TestSingleCharge();
    TestOverlappingCharges();
    TestAccumulation();
    TestClockGoingBackwards();
    TestPacketTypes();
    TestPreemption();
    TestConversion();
    TestNullSafety();
}
