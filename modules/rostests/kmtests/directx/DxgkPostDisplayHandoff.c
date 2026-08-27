/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     POST display ownership handoff rollback state tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include "postdisplay_core.h"

static
VOID
DxgkPostDisplayTestRestartableFailure(VOID)
{
    DXGK_POST_DISPLAY_HANDOFF_CORE Core = {0};

    ok_bool_true(DxgkPostDisplayCoreArm(&Core), "the stopped fallback arms one handoff transaction");
    ok_bool_false(DxgkPostDisplayCoreArm(&Core), "a claimant cannot replace its pending fallback");
    ok_eq_long(DxgkPostDisplayCoreComplete(&Core, STATUS_DEVICE_HARDWARE_ERROR, TRUE), DxgkPostDisplayCompletionRollback);
    ok_eq_long(DxgkPostDisplayCoreComplete(&Core, STATUS_DEVICE_HARDWARE_ERROR, TRUE), DxgkPostDisplayCompletionNone);
}

static
VOID
DxgkPostDisplayTestSuccessfulClaimant(VOID)
{
    DXGK_POST_DISPLAY_HANDOFF_CORE Core = {0};

    ok_bool_true(DxgkPostDisplayCoreArm(&Core), "a fallback can be retained for a new claimant");
    ok_eq_long(DxgkPostDisplayCoreComplete(&Core, STATUS_SUCCESS, TRUE), DxgkPostDisplayCompletionCommit);
    ok_bool_true(DxgkPostDisplayCoreArm(&Core), "a committed transaction leaves the state reusable");
}

static
VOID
DxgkPostDisplayTestNonRestartableFailure(VOID)
{
    DXGK_POST_DISPLAY_HANDOFF_CORE Core = {0};

    ok_bool_true(DxgkPostDisplayCoreArm(&Core), "the fallback is retained until claimant completion");
    ok_eq_long(DxgkPostDisplayCoreComplete(&Core, STATUS_DEVICE_HARDWARE_ERROR, FALSE), DxgkPostDisplayCompletionCommit);
    ok_eq_long(DxgkPostDisplayCoreComplete(&Core, STATUS_SUCCESS, TRUE), DxgkPostDisplayCompletionNone);
}

START_TEST(DxgkPostDisplayHandoff)
{
    DxgkPostDisplayTestRestartableFailure();
    DxgkPostDisplayTestSuccessfulClaimant();
    DxgkPostDisplayTestNonRestartableFailure();
}
