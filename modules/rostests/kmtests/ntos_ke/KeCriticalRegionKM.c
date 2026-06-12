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
    ok_bool_false(KeAreApcsDisabled(), "APCs disabled at entry");
    ok_bool_false(KeAreAllApcsDisabled(), "all APCs disabled at entry");

    KeEnterCriticalRegion();
    ok_bool_true(KeAreApcsDisabled(), "APCs enabled in critical region");
    ok_bool_false(KeAreAllApcsDisabled(), "special APCs disabled in critical region");

    KeEnterCriticalRegion();
    ok_bool_true(KeAreApcsDisabled(), "APCs enabled in nested critical region");
    KeLeaveCriticalRegion();
    ok_bool_true(KeAreApcsDisabled(), "APCs enabled after nested leave");

    KeLeaveCriticalRegion();
    ok_bool_false(KeAreApcsDisabled(), "APCs disabled after leave");

    KeEnterGuardedRegion();
    ok_bool_true(KeAreApcsDisabled(), "APCs enabled in guarded region");
    ok_bool_true(KeAreAllApcsDisabled(), "special APCs enabled in guarded region");
    KeLeaveGuardedRegion();

    ok_bool_false(KeAreApcsDisabled(), "APCs disabled after guarded leave");
    ok_bool_false(KeAreAllApcsDisabled(), "all APCs disabled after guarded leave");
}
