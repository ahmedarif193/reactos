/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite extended EX_TIMER / KTIMER coverage
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static KEVENT TimerEvent;
static volatile LONG DpcCount;

static
VOID
NTAPI
TimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Arg1,
    _In_opt_ PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    if (InterlockedIncrement(&DpcCount) >= 3)
        KeSetEvent(&TimerEvent, IO_NO_INCREMENT, FALSE);
}

static
VOID
TestOneShotDpc(VOID)
{
    KTIMER Timer;
    KDPC Dpc;
    LARGE_INTEGER DueTime;
    NTSTATUS Status;

    KeInitializeTimer(&Timer);
    KeInitializeDpc(&Dpc, TimerDpc, NULL);
    KeInitializeEvent(&TimerEvent, NotificationEvent, FALSE);
    DpcCount = 0;

    DueTime.QuadPart = -10 * 1000 * 50;
    ok_bool_false(KeSetTimer(&Timer, DueTime, &Dpc), "fresh timer was set");

    DueTime.QuadPart = -10 * 1000 * 1000 * 5;
    Status = KeWaitForSingleObject(&TimerEvent, Executive, KernelMode, FALSE, &DueTime);
    ok(DpcCount >= 1, "one-shot dpc count %ld\n", DpcCount);
    ok_bool_false(KeCancelTimer(&Timer), "cancel after fire");
}

static
VOID
TestPeriodicDpc(VOID)
{
    KTIMER Timer;
    KDPC Dpc;
    LARGE_INTEGER DueTime;
    NTSTATUS Status;

    KeInitializeTimerEx(&Timer, SynchronizationTimer);
    KeInitializeDpc(&Dpc, TimerDpc, NULL);
    KeInitializeEvent(&TimerEvent, NotificationEvent, FALSE);
    DpcCount = 0;

    DueTime.QuadPart = -10 * 1000 * 30;
    KeSetTimerEx(&Timer, DueTime, 30, &Dpc);

    DueTime.QuadPart = -10 * 1000 * 1000 * 5;
    Status = KeWaitForSingleObject(&TimerEvent, Executive, KernelMode, FALSE, &DueTime);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(DpcCount >= 3, "periodic dpc count %ld\n", DpcCount);
    ok_bool_true(KeCancelTimer(&Timer), "cancel armed periodic");
}

static
VOID
TestCancelBeforeFire(VOID)
{
    KTIMER Timer;
    KDPC Dpc;
    LARGE_INTEGER DueTime;

    KeInitializeTimer(&Timer);
    KeInitializeDpc(&Dpc, TimerDpc, NULL);
    DpcCount = 0;

    DueTime.QuadPart = -10 * 1000 * 1000 * 30;
    KeSetTimer(&Timer, DueTime, &Dpc);
    ok_bool_true(KeCancelTimer(&Timer), "cancel before fire");
    ok_bool_false(KeReadStateTimer(&Timer), "timer not signaled after cancel");
    ok_eq_long(DpcCount, 0L);
}

START_TEST(ExTimerExtra)
{
    TestOneShotDpc();
    TestPeriodicDpc();
    TestCancelBeforeFire();
}
