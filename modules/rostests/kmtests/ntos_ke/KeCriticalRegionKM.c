/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite critical/guarded region API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(KeCriticalRegionKM)
{
    BOOLEAN BaseKernel = KeAreApcsDisabled();
    BOOLEAN BaseAll = KeAreAllApcsDisabled();

    ok_eq_bool(KeAreApcsDisabled(), BaseKernel);
    ok_eq_bool(KeAreAllApcsDisabled(), BaseAll);

    KeEnterCriticalRegion();
    ok_bool_true(KeAreApcsDisabled(), "APCs disabled in critical region");
    ok_eq_bool(KeAreAllApcsDisabled(), BaseAll);

    KeEnterCriticalRegion();
    ok_bool_true(KeAreApcsDisabled(), "APCs disabled in nested critical region");
    KeLeaveCriticalRegion();
    ok_bool_true(KeAreApcsDisabled(), "APCs disabled after nested leave");

    KeLeaveCriticalRegion();
    ok_eq_bool(KeAreApcsDisabled(), BaseKernel);

    KeEnterGuardedRegion();
    ok_bool_true(KeAreApcsDisabled(), "APCs disabled in guarded region");
    ok_bool_true(KeAreAllApcsDisabled(), "special APCs disabled in guarded region");
    KeLeaveGuardedRegion();

    ok_eq_bool(KeAreApcsDisabled(), BaseKernel);
    ok_eq_bool(KeAreAllApcsDisabled(), BaseAll);
}
