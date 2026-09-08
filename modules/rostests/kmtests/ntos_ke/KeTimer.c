/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPLv2+ - See COPYING in the top level directory
 * PURPOSE:         Kernel-Mode Test Suite Timer test
 * PROGRAMMER:      Rafal Harabien <rafalh@reactos.org>
 */

#include <kmt_test.h>

#define CheckTimer(Timer, ExpectedType, State, ExpectedWaitNext,                \
                            Irql, ThreadList, ThreadCount) do                   \
{                                                                               \
    INT TheIndex;                                                               \
    PLIST_ENTRY TheEntry;                                                       \
    PKTHREAD TheThread;                                                         \
    ok_eq_uint((Timer)->Header.Type, ExpectedType);                             \
    /* Win7+ KeInitializeTimerEx zeros Header.Hand and the masked Lock bits;   \
     * NT 5.x leaves Hand at sizeof(KTIMER)/sizeof(ULONG) (10 on i386) and     \
     * preserves the 0x5500 in bits 8-15 of Lock. */                            \
    if (GetNTVersion() >= _WIN32_WINNT_WIN7)                                    \
    {                                                                           \
        ok_eq_uint((Timer)->Header.Hand, 0);                                    \
        ok_eq_hex((Timer)->Header.Lock & 0xFF00FF00L, 0x00000000L);             \
    }                                                                           \
    else                                                                        \
    {                                                                           \
        ok_eq_uint((Timer)->Header.Hand,                                        \
                   sizeof *(Timer) / sizeof(ULONG));                            \
        ok_eq_hex((Timer)->Header.Lock & 0xFF00FF00L, 0x00005500L);             \
    }                                                                           \
    ok_eq_long((Timer)->Header.SignalState, State);                             \
    TheEntry = (Timer)->Header.WaitListHead.Flink;                              \
    for (TheIndex = 0; TheIndex < (ThreadCount); ++TheIndex)                    \
    {                                                                           \
        TheThread = CONTAINING_RECORD(TheEntry, KTHREAD,                        \
                                        WaitBlock[0].WaitListEntry);            \
        ok_eq_pointer(TheThread, (ThreadList)[TheIndex]);                       \
        ok_eq_pointer(TheEntry->Flink->Blink, TheEntry);                        \
        TheEntry = TheEntry->Flink;                                             \
    }                                                                           \
    ok_eq_pointer(TheEntry, &(Timer)->Header.WaitListHead);                     \
    ok_eq_pointer(TheEntry->Flink->Blink, TheEntry);                            \
    ok_eq_long(KeReadStateTimer(Timer), State);                                 \
    ok_eq_bool(Thread->WaitNext, ExpectedWaitNext);                             \
    ok_irql(Irql);                                                              \
} while (0)

static
VOID
TestTimerFunctional(
    IN PKTIMER Timer,
    IN TIMER_TYPE Type,
    IN KIRQL OriginalIrql)
{
    PKTHREAD Thread = KeGetCurrentThread();

    memset(Timer, 0x55, sizeof *Timer);
    KeInitializeTimerEx(Timer, Type);
    CheckTimer(Timer, TimerNotificationObject + Type, 0L, FALSE, OriginalIrql, (PVOID *)NULL, 0);
}

typedef struct _TIMER_WAIT_APC_CONTEXT
{
    KAPC Apc;
    KEVENT ApcDone;
    volatile LONG Delivered;
    BOOLEAN Inserted;
} TIMER_WAIT_APC_CONTEXT;

static VOID NTAPI
TimerWaitApc(
    PKAPC Apc,
    PKNORMAL_ROUTINE *NormalRoutine,
    PVOID *NormalContext,
    PVOID *SystemArgument1,
    PVOID *SystemArgument2)
{
    TIMER_WAIT_APC_CONTEXT *Context;

    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    Context = CONTAINING_RECORD(Apc, TIMER_WAIT_APC_CONTEXT, Apc);
    InterlockedIncrement(&Context->Delivered);
    KeSetEvent(&Context->ApcDone, IO_NO_INCREMENT, FALSE);
}

static VOID NTAPI
TimerWaitDpc(PKDPC Dpc, PVOID DeferredContext,
             PVOID SystemArgument1, PVOID SystemArgument2)
{
    TIMER_WAIT_APC_CONTEXT *Context = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    Context->Inserted = KeInsertQueueApc(&Context->Apc, NULL, NULL, 0);
}

static VOID
TestInterruptedTimerWait(VOID)
{
    TIMER_WAIT_APC_CONTEXT Context;
    KTIMER ApcTimer;
    KDPC Dpc;
    KEVENT Events[4];
    PVOID Objects[4];
    KWAIT_BLOCK WaitBlocks[4];
    LARGE_INTEGER DueTime, Timeout;
    NTSTATUS Status;
    ULONG Test, Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Events); ++Index)
    {
        KeInitializeEvent(&Events[Index], NotificationEvent, FALSE);
        Objects[Index] = &Events[Index];
    }

    /* Interrupt an outstanding timeout with a special kernel APC. The wait
     * must unlink its old blocks, deliver the APC, and resume until timeout.
     * In particular, the delay-only block must not interpret the overlaid
     * KTHREAD.WaitTime field as a dispatcher-object pointer on x86. */
    for (Test = 0; Test < 4; ++Test)
    {
        RtlZeroMemory(&Context, sizeof(Context));
        KeInitializeEvent(&Context.ApcDone, NotificationEvent, FALSE);
        KeInitializeApc(&Context.Apc, KeGetCurrentThread(),
                        OriginalApcEnvironment, TimerWaitApc, NULL,
                        NULL, KernelMode, NULL);
        KeInitializeTimer(&ApcTimer);
        KeInitializeDpc(&Dpc, TimerWaitDpc, &Context);
        DueTime.QuadPart = -50 * 10000;
        Timeout.QuadPart = -250 * 10000;
        KeSetTimer(&ApcTimer, DueTime, &Dpc);

        if (Test == 0)
            Status = KeDelayExecutionThread(KernelMode, FALSE, &Timeout);
        else if (Test == 1)
            Status = KeWaitForSingleObject(Objects[0], Executive,
                                           KernelMode, FALSE, &Timeout);
        else
            Status = KeWaitForMultipleObjects(Test == 2 ? 3 : 4, Objects,
                                               WaitAny, Executive, KernelMode,
                                               FALSE, &Timeout,
                                               Test == 2 ? NULL : WaitBlocks);

        ok_eq_hex(Status, Test == 0 ? STATUS_SUCCESS : STATUS_TIMEOUT);
        ok_eq_long(Context.Delivered, 1);

        /* Drain the DPC before its stack storage or the APC can go away,
         * including on a failing implementation that returned too early. */
        KeCancelTimer(&ApcTimer);
        KeFlushQueuedDpcs();
        if (Context.Inserted)
            KeWaitForSingleObject(&Context.ApcDone, Executive,
                                  KernelMode, FALSE, NULL);
        ok(Context.Inserted, "APC was not queued for timer wait %lu\n", Test);
        ok_irql(PASSIVE_LEVEL);
    }
}

START_TEST(KeTimer)
{
    KTIMER Timer;
    KIRQL Irql;
    KIRQL Irqls[] = { PASSIVE_LEVEL, APC_LEVEL, DISPATCH_LEVEL, HIGH_LEVEL };
    INT i;

    for (i = 0; i < sizeof Irqls / sizeof Irqls[0]; ++i)
    {
        /* DRIVER_IRQL_NOT_LESS_OR_EQUAL (TODO: on MP only?) */
        if (Irqls[i] > DISPATCH_LEVEL && KmtIsCheckedBuild)
            return;
        KeRaiseIrql(Irqls[i], &Irql);
        TestTimerFunctional(&Timer, NotificationTimer, Irqls[i]);
        TestTimerFunctional(&Timer, SynchronizationTimer, Irqls[i]);
        KeLowerIrql(Irql);
    }

    ok_irql(PASSIVE_LEVEL);
    KmtSetIrql(PASSIVE_LEVEL);
    TestInterruptedTimerWait();
}
