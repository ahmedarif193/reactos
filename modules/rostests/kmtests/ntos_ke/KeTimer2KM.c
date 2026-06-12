/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite extended timer API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestPeriodicTimer(VOID)
{
    KTIMER Timer;
    LARGE_INTEGER DueTime;
    NTSTATUS Status;
    LARGE_INTEGER Timeout;
    ULONG i;

    KeInitializeTimerEx(&Timer, SynchronizationTimer);
    ok_bool_false(KeReadStateTimer(&Timer), "fresh timer signaled");

    DueTime.QuadPart = -10 * 1000 * 20;
    KeSetTimerEx(&Timer, DueTime, 20, NULL);

    for (i = 0; i < 3; i++)
    {
        Timeout.QuadPart = -10 * 1000 * 1000 * 5;
        Status = KeWaitForSingleObject(&Timer, Executive, KernelMode, FALSE, &Timeout);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    ok_bool_true(KeCancelTimer(&Timer), "cancel armed periodic timer");
    ok_bool_false(KeCancelTimer(&Timer), "cancel disarmed timer");
}

static
VOID
TestCoalescableTimer(VOID)
{
    KTIMER Timer;
    KDPC Dpc;
    LARGE_INTEGER DueTime;
    BOOLEAN WasQueued;

    KeInitializeTimerEx(&Timer, NotificationTimer);
    KeInitializeDpc(&Dpc, NULL, NULL);

    DueTime.QuadPart = -10 * 1000 * 1000 * 60;
    WasQueued = KeSetCoalescableTimer(&Timer, DueTime, 1000, 100, NULL);
    ok_bool_false(WasQueued, "fresh coalescable timer was queued");

    WasQueued = KeSetCoalescableTimer(&Timer, DueTime, 1000, 100, NULL);
    ok_bool_true(WasQueued, "rearm reports queued");

    ok_bool_true(KeCancelTimer(&Timer), "cancel coalescable");
}

START_TEST(KeTimer2KM)
{
    TestPeriodicTimer();
    TestCoalescableTimer();
}
