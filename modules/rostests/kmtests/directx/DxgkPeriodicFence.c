/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Periodic monitored fences driven by display vsync
 *
 * A periodic fence is a clock bound to one display target.  If another
 * target's vsync advances it, a composition loop paces against the wrong
 * monitor and tears.
 */

#include <kmt_test.h>
#include "fence_core.h"

static VOID TestBinding(VOID)
{
    DXGK_PERIODIC_FENCE Fence;

    { NTSTATUS Observed = DxgkPeriodicFenceCoreBind(&Fence, 0, 1, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(Fence.Bound, "bound");
    ok_eq_ulong(Fence.PeriodInVSyncs, 1UL);

    { NTSTATUS Observed = DxgkPeriodicFenceCoreBind(&Fence, 0, 1, 77); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 77ULL);

    /* A zero period would advance the fence on nothing at all. */
    { NTSTATUS Observed = DxgkPeriodicFenceCoreBind(&Fence, 0, 0, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_bool_false(Fence.Bound, "a rejected bind leaves nothing bound");
}

static VOID TestEveryVSync(VOID)
{
    DXGK_PERIODIC_FENCE Fence;
    ULONGLONG Value;

    { NTSTATUS Observed = DxgkPeriodicFenceCoreBind(&Fence, 1, 1, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 1), "advances on its target");
    ok_eq_ulonglong(Fence.CurrentValue, 1ULL);
    ok_bool_true(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 1), "advances again");
    ok_eq_ulonglong(Fence.CurrentValue, 2ULL);

    /* Another display's vsync is not this fence's clock. */
    Value = Fence.CurrentValue;
    ok_bool_false(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 0), "other target ignored");
    ok_eq_ulonglong(Fence.CurrentValue, Value);
    ok_bool_false(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 99), "unknown target ignored");
    ok_eq_ulonglong(Fence.CurrentValue, Value);
}

static VOID TestMultiVSyncPeriod(VOID)
{
    DXGK_PERIODIC_FENCE Fence;

    { NTSTATUS Observed = DxgkPeriodicFenceCoreBind(&Fence, 2, 3, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 2), "first of three");
    ok_eq_ulonglong(Fence.CurrentValue, 0ULL);
    ok_bool_false(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 2), "second of three");
    ok_eq_ulonglong(Fence.CurrentValue, 0ULL);
    ok_bool_true(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 2), "third completes the period");
    ok_eq_ulonglong(Fence.CurrentValue, 1ULL);

    /* The counter restarts, so the period stays exactly three. */
    ok_bool_false(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 2), "counter restarted");
    ok_bool_false(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 2), "still counting");
    ok_bool_true(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 2), "second period");
    ok_eq_ulonglong(Fence.CurrentValue, 2ULL);

    /* An ignored vsync for another target must not consume a tick of the
     * period either, or the fence drifts against its own display. */
    ok_bool_false(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 2), "first tick");
    ok_bool_false(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 5), "foreign vsync");
    ok_bool_false(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 2), "second tick");
    ok_bool_true(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 2), "third tick still completes");
    ok_eq_ulonglong(Fence.CurrentValue, 3ULL);
}

static VOID TestUnbound(VOID)
{
    DXGK_PERIODIC_FENCE Fence;

    RtlZeroMemory(&Fence, sizeof(Fence));
    ok_bool_false(DxgkPeriodicFenceCoreNotifyVSync(&Fence, 0), "unbound never advances");
    ok_eq_ulonglong(Fence.CurrentValue, 0ULL);
}

START_TEST(DxgkPeriodicFence)
{
    TestBinding();
    TestEveryVSync();
    TestMultiVSyncPeriod();
    TestUnbound();
}

/* EOF */
