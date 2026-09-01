/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     SoftGPU VSync phase and notification policy tests
 */

#include <kmt_test.h>
#include "vsync_policy_core.h"

static VOID
CheckPolicy(
    _In_ ULONG State,
    _In_ BOOLEAN Valid,
    _In_ BOOLEAN PhaseEnabled,
    _In_ BOOLEAN NotificationEnabled,
    _In_ BOOLEAN CancelTimer)
{
    SOFTGPU_VSYNC_POLICY Policy;

    RtlFillMemory(&Policy, sizeof(Policy), 0x55);
    ok_eq_bool(SoftGpuVsyncEvaluatePolicy(State, &Policy), Valid);
    ok_eq_bool(Policy.PhaseEnabled, PhaseEnabled);
    ok_eq_bool(Policy.NotificationEnabled, NotificationEnabled);
    ok_eq_bool(Policy.CancelTimer, CancelTimer);
}

START_TEST(SoftGpuVsyncPolicy)
{
    CheckPolicy(
        SoftGpuVsyncEnable,
        TRUE,
        TRUE,
        TRUE,
        FALSE);
    CheckPolicy(
        SoftGpuVsyncDisableKeepPhase,
        TRUE,
        TRUE,
        FALSE,
        FALSE);
    CheckPolicy(
        SoftGpuVsyncDisableNoPhase,
        TRUE,
        FALSE,
        FALSE,
        TRUE);
    CheckPolicy(
        MAXULONG,
        FALSE,
        FALSE,
        FALSE,
        FALSE);

    ok_eq_bool(SoftGpuVsyncCanDeliverNotification(TRUE, TRUE, TRUE, TRUE), TRUE);
    ok_eq_bool(SoftGpuVsyncCanDeliverNotification(FALSE, TRUE, TRUE, TRUE), FALSE);
    ok_eq_bool(SoftGpuVsyncCanDeliverNotification(TRUE, FALSE, TRUE, TRUE), FALSE);
    ok_eq_bool(SoftGpuVsyncCanDeliverNotification(TRUE, TRUE, FALSE, TRUE), FALSE);
    ok_eq_bool(SoftGpuVsyncCanDeliverNotification(TRUE, TRUE, TRUE, FALSE), FALSE);
}

/* EOF */
