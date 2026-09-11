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
static volatile KIRQL ExtendedTimerIrql;

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
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    if (InterlockedIncrement(&DpcCount) >= (LONG)(ULONG_PTR)Context)
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
    KeInitializeDpc(&Dpc, TimerDpc, (PVOID)1);
    KeInitializeEvent(&TimerEvent, NotificationEvent, FALSE);
    DpcCount = 0;

    DueTime.QuadPart = -10 * 1000 * 50;
    ok_bool_false(KeSetTimer(&Timer, DueTime, &Dpc), "fresh timer was set");

    DueTime.QuadPart = -10 * 1000 * 1000 * 5;
    Status = KeWaitForSingleObject(&TimerEvent, Executive, KernelMode, FALSE, &DueTime);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_false(KeCancelTimer(&Timer), "cancel after fire");
    KeFlushQueuedDpcs();
    ok_eq_long(DpcCount, 1);
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
    KeInitializeDpc(&Dpc, TimerDpc, (PVOID)3);
    KeInitializeEvent(&TimerEvent, NotificationEvent, FALSE);
    DpcCount = 0;

    DueTime.QuadPart = -10 * 1000 * 30;
    KeSetTimerEx(&Timer, DueTime, 30, &Dpc);

    DueTime.QuadPart = -10 * 1000 * 1000 * 5;
    Status = KeWaitForSingleObject(&TimerEvent, Executive, KernelMode, FALSE, &DueTime);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(KeCancelTimer(&Timer), "cancel armed periodic");
    KeFlushQueuedDpcs();
    ok(DpcCount >= 3, "periodic dpc count %ld\n", DpcCount);
}

static
VOID
TestCancelBeforeFire(VOID)
{
    KTIMER Timer;
    KDPC Dpc;
    LARGE_INTEGER DueTime;

    KeInitializeTimer(&Timer);
    KeInitializeDpc(&Dpc, TimerDpc, (PVOID)1);
    DpcCount = 0;

    DueTime.QuadPart = -10 * 1000 * 1000 * 30;
    KeSetTimer(&Timer, DueTime, &Dpc);
    ok_bool_true(KeCancelTimer(&Timer), "cancel before fire");
    KeFlushQueuedDpcs();
    ok_bool_false(KeReadStateTimer(&Timer), "timer not signaled after cancel");
    ok_eq_long(DpcCount, 0L);
}

static
VOID
NTAPI
ExtendedTimerCallback(
    _In_ PEX_TIMER Timer,
    _In_opt_ PVOID Context)
{
    PKEVENT Event = Context;

    UNREFERENCED_PARAMETER(Timer);

    ExtendedTimerIrql = KeGetCurrentIrql();
    InterlockedIncrement(&DpcCount);
    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
}

static
VOID
TestExtendedTimer(VOID)
{
    static const ULONG Attributes[] = {
        0, EX_TIMER_NOTIFICATION, EX_TIMER_HIGH_RESOLUTION,
        EX_TIMER_HIGH_RESOLUTION | EX_TIMER_NOTIFICATION,
        EX_TIMER_NO_WAKE, EX_TIMER_NO_WAKE | EX_TIMER_NOTIFICATION
    };
    LARGE_INTEGER Timeout;
    PEX_TIMER Timer;
    BOOLEAN Result;
    NTSTATUS Status;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); ++Index)
    {
        Timer = ExAllocateTimer(NULL, NULL, Attributes[Index]);
        ok(Timer != NULL, "Allocation failed for attributes %lx\n", Attributes[Index]);
        if (Timer)
        {
            Result = ExDeleteTimer(Timer, TRUE, TRUE, NULL);
            ok_eq_bool(Result, FALSE);
        }
    }

    KeInitializeEvent(&TimerEvent, NotificationEvent, FALSE);
    ExtendedTimerIrql = PASSIVE_LEVEL;
    DpcCount = 0;
    Timer = ExAllocateTimer(ExtendedTimerCallback, &TimerEvent, EX_TIMER_NOTIFICATION);
    trace("ExAllocateTimer(notification) returned %p\n", Timer);
    ok(Timer != NULL, "ExAllocateTimer failed\n");
    if (Timer == NULL)
        return;

    Result = ExSetTimer(Timer, -10LL * 1000 * 20, 0, NULL);
    trace("ExSetTimer returned %u\n", Result);
    ok_eq_bool(Result, FALSE);
    Timeout.QuadPart = -10LL * 1000 * 1000 * 5;
    Status = KeWaitForSingleObject(&TimerEvent, Executive, KernelMode, FALSE, &Timeout);
    trace("extended timer wait returned 0x%08lx, callbacks %ld, IRQL %u\n", Status, DpcCount, ExtendedTimerIrql);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(DpcCount, 1);
    ok_eq_ulong(ExtendedTimerIrql, DISPATCH_LEVEL);

    Result = ExCancelTimer(Timer, NULL);
    trace("ExCancelTimer after expiry returned %u\n", Result);
    ok_eq_bool(Result, FALSE);
    Result = ExDeleteTimer(Timer, TRUE, TRUE, NULL);
    trace("ExDeleteTimer returned %u\n", Result);
    ok_eq_bool(Result, FALSE);

    Timer = ExAllocateTimer(NULL, NULL, 0);
    ok(Timer != NULL, "ExAllocateTimer failed\n");
    if (!Timer)
        return;
    Result = ExSetTimer(Timer, -30LL * 1000 * 1000 * 10, 0, NULL);
    ok_eq_bool(Result, FALSE);
    Result = ExDeleteTimer(Timer, TRUE, TRUE, NULL);
    ok_eq_bool(Result, TRUE);
}

/* Fatal contract check, run individually in a disposable guest.
 * Windows 11 stops with TIMER_OR_DPC_INVALID (9, 0, Attributes, 0). */
START_TEST(ExTimerInvalidAttributes)
{
    PEX_TIMER Timer = ExAllocateTimer(NULL, NULL, MAXULONG);

    ok(FALSE, "Invalid timer attributes returned %p instead of bugchecking\n", Timer);
    if (Timer)
        ExDeleteTimer(Timer, TRUE, TRUE, NULL);
}

/* Windows 11 accepts this combination despite the documented restriction.
 * Keep it explicit: older kernels may bugcheck for these attributes. */
START_TEST(ExTimerCombinedAttributes)
{
    static const ULONG Attributes[] = {
        EX_TIMER_HIGH_RESOLUTION | EX_TIMER_NO_WAKE,
        EX_TIMER_HIGH_RESOLUTION | EX_TIMER_NO_WAKE | EX_TIMER_NOTIFICATION
    };
    PEX_TIMER Timer;
    BOOLEAN Result;
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Attributes); ++Index)
    {
        Timer = ExAllocateTimer(NULL, NULL, Attributes[Index]);
        ok(Timer != NULL, "Allocation failed for attributes %lx\n", Attributes[Index]);
        if (Timer)
        {
            Result = ExDeleteTimer(Timer, TRUE, TRUE, NULL);
            ok_eq_bool(Result, FALSE);
        }
    }
}

START_TEST(ExTimerExtra)
{
    TestOneShotDpc();
    TestPeriodicDpc();
    TestCancelBeforeFire();
    TestExtendedTimer();
}
