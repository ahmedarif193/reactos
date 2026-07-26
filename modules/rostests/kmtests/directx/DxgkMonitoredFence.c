/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Monitored fence value semantics
 *
 * A monitored fence is read directly by the GPU and by waiters without a
 * kernel transition, so its value can only ever move forward.
 */

#include <kmt_test.h>
#include "fence_core.h"

static VOID TestInitialization(VOID)
{
    DXGK_MONITORED_FENCE Fence;

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 0, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 0ULL);
    ok_bool_true(Fence.Initialized, "initialized");

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 42, DXGK_MONITORED_FENCE_FLAG_SHARED); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 42ULL);

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 0, 0x8000UL); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* Neither signalable nor waitable carries no information at all. */
    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 0,
                  DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL | DXGK_MONITORED_FENCE_FLAG_NO_WAIT); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestMonotonicSignal(VOID)
{
    DXGK_MONITORED_FENCE Fence;

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 10, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 11); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 11ULL);
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 11); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 11ULL);
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 1000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 1000ULL);

    /*
     * A regressing signal is refused and leaves the published value alone.
     * Applying it would un-satisfy waits that already resolved, and a waiter
     * that has been released cannot be recalled.
     */
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 999); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulonglong(Fence.CurrentValue, 1000ULL);
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulonglong(Fence.CurrentValue, 1000ULL);

    /* 64-bit values must not be truncated to 32. */
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 0x1FFFFFFFFULL); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 0x1FFFFFFFFULL);
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 0xFFFFFFFFULL); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestWaitResolution(VOID)
{
    DXGK_MONITORED_FENCE Fence;

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 5, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkMonitoredFenceCoreIsSatisfied(&Fence, 5), "at the value");
    ok_bool_true(DxgkMonitoredFenceCoreIsSatisfied(&Fence, 4), "below the value");
    ok_bool_false(DxgkMonitoredFenceCoreIsSatisfied(&Fence, 6), "above the value");
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 6); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkMonitoredFenceCoreIsSatisfied(&Fence, 6), "satisfied after signal");
}

static VOID TestCapabilityGating(VOID)
{
    DXGK_MONITORED_FENCE NoSignal;
    DXGK_MONITORED_FENCE NoWait;
    DXGK_MONITORED_FENCE Uninitialized;

    RtlZeroMemory(&Uninitialized, sizeof(Uninitialized));
    { NTSTATUS Observed = DxgkMonitoredFenceCoreCanWait(&Uninitialized); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Uninitialized, 1); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }
    ok_bool_false(DxgkMonitoredFenceCoreIsSatisfied(&Uninitialized, 0), "uninitialized satisfies nothing");

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&NoSignal, 0, DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreCanSignal(&NoSignal); ok_eq_hex(Observed, STATUS_NOT_SUPPORTED); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&NoSignal, 1); ok_eq_hex(Observed, STATUS_NOT_SUPPORTED); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreCanWait(&NoSignal); ok_eq_hex(Observed, STATUS_SUCCESS); }

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&NoWait, 0, DXGK_MONITORED_FENCE_FLAG_NO_WAIT); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreCanWait(&NoWait); ok_eq_hex(Observed, STATUS_NOT_SUPPORTED); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreCanSignal(&NoWait); ok_eq_hex(Observed, STATUS_SUCCESS); }
}

START_TEST(DxgkMonitoredFence)
{
    TestInitialization();
    TestMonotonicSignal();
    TestWaitResolution();
    TestCapabilityGating();
}

/* EOF */
