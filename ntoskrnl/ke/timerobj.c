/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/timerobj.c
 * PURPOSE:         Handle Kernel Timers (Kernel-part of Executive Timers)
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

KTIMER_TABLE_ENTRY KiTimerTableListHead[TIMER_TABLE_SIZE];
LARGE_INTEGER KiTimeIncrementReciprocal;
UCHAR KiTimeIncrementShiftCount;
BOOLEAN KiEnableTimerWatchdog = FALSE;

/* PRIVATE FUNCTIONS *********************************************************/

BOOLEAN
FASTCALL
KiInsertTreeTimer(IN PKTIMER Timer,
                  IN LARGE_INTEGER Interval)
{
    BOOLEAN Inserted = FALSE;
    ULONG Hand = 0;
    PKSPIN_LOCK_QUEUE LockQueue;
    DPRINT("KiInsertTreeTimer(): Timer %p, Interval: %I64d\n", Timer, Interval.QuadPart);

    /* Setup the timer's due time */
    if (KiComputeDueTime(Timer, Interval, &Hand))
    {
        /* Acquire the lock */
        LockQueue = KiAcquireTimerLock(Hand);

        if (!Timer->Header.Inserted)
        {
            KiReleaseTimerLock(LockQueue);
            return FALSE;
        }

        /* Insert the timer */
        if (KiInsertTimerTable(Timer, Hand))
        {
            /* It was already there, remove it */
            KiRemoveEntryTimer(Timer);
            Timer->Header.Inserted = FALSE;
        }
        else
        {
            /* Otherwise, we're now inserted */
            Inserted = TRUE;
        }

        /* Release the lock */
        KiReleaseTimerLock(LockQueue);
    }

    /* Release the lock and return insert status */
    return Inserted;
}

BOOLEAN
FASTCALL
KiInsertTimerTable(IN PKTIMER Timer,
                   IN ULONG Hand)
{
    ULONGLONG InterruptTime;
    ULONGLONG DueTime = Timer->DueTime.QuadPart;
    BOOLEAN Expired = FALSE;
    PLIST_ENTRY ListHead, NextEntry;
    PKTIMER CurrentTimer;
    DPRINT("KiInsertTimerTable(): Timer %p, Hand: %lu\n", Timer, Hand);

    /* Check if the period is zero */
    if (!Timer->Period) Timer->Header.SignalState = FALSE;

    /* Sanity check */
    ASSERT(Hand == KiComputeTimerTableIndex(DueTime));

    /* Loop the timer list backwards */
    ListHead = &KiTimerTableListHead[Hand].Entry;
    NextEntry = ListHead->Blink;
    while (NextEntry != ListHead)
    {
        /* Get the timer */
        CurrentTimer = CONTAINING_RECORD(NextEntry, KTIMER, TimerListEntry);

        /* Now check if we can fit it before */
        if ((ULONGLONG)DueTime >= CurrentTimer->DueTime.QuadPart) break;

        /* Keep looping */
        NextEntry = NextEntry->Blink;
    }

    /* Looped all the list, insert it here and get the interrupt time again */
    InsertHeadList(NextEntry, &Timer->TimerListEntry);

    /* Check if we didn't find it in the list */
    if (NextEntry == ListHead)
    {
        /* Set the time */
        KiTimerTableListHead[Hand].Time.QuadPart = DueTime;

        /* Make sure it hasn't expired already */
        InterruptTime = KeQueryInterruptTime();
        if (DueTime <= InterruptTime) Expired = TRUE;
    }

    /* Return expired state */
    return Expired;
}

BOOLEAN
FASTCALL
KiSignalTimer(IN PKTIMER Timer)
{
    BOOLEAN RequestInterrupt = FALSE;
    PKDPC Dpc = Timer->Dpc;
    ULONG Period = Timer->Period;
    LARGE_INTEGER Interval, SystemTime;
    DPRINT("KiSignalTimer(): Timer %p\n", Timer);

    /* Set default values */
    Timer->Header.Inserted = FALSE;
    Timer->Header.SignalState = TRUE;

    /* Check if the timer has waiters */
    if (!IsListEmpty(&Timer->Header.WaitListHead))
    {
        KiWaitTest(Timer, IO_NO_INCREMENT);
    }

    /* Check if we have a period */
    if (Period)
    {
        /* Calculate the interval and insert the timer */
        Interval.QuadPart = Int32x32To64(Period, -10000);
        while (!KiInsertTreeTimer(Timer, Interval));
    }

    /* Check if we have a DPC */
    if (Dpc)
    {
        /* Insert it in the queue */
        KeQuerySystemTime(&SystemTime);
        KeInsertQueueDpc(Dpc,
                         ULongToPtr(SystemTime.LowPart),
                         ULongToPtr(SystemTime.HighPart));
        RequestInterrupt = TRUE;
    }

    /* Return whether we need to request a DPC interrupt or not */
    return RequestInterrupt;
}

#if defined(__GNUC__) && !defined(__clang__) && (__GNUC__ >= 12)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdangling-pointer"
#endif
VOID
FASTCALL
KiCompleteTimer(IN PKTIMER Timer,
                IN PKSPIN_LOCK_QUEUE LockQueue)
{
    LIST_ENTRY ListHead;
    BOOLEAN RequestInterrupt = FALSE;
    DPRINT("KiCompleteTimer(): Timer %p, LockQueue: %p\n", Timer, LockQueue);

    /* Remove it from the timer list */
    KiRemoveEntryTimer(Timer);

    /* Link the timer list to our stack */
    InitializeListHead(&ListHead);
    InsertHeadList(&ListHead, &Timer->TimerListEntry);

    /* Release the timer lock */
    KiReleaseTimerLock(LockQueue);

    /* Acquire the timer object lock */
    KiAcquireDispatcherObject(&Timer->Header);

    /* Signal the timer if it's still on our list */
    if (!IsListEmpty(&ListHead)) RequestInterrupt = KiSignalTimer(Timer);

    /*
     * KiSignalTimer may leave a one-shot timer detached from the timer table
     * with the temporary stack list still referenced from TimerListEntry.
     * Clear that transient state before returning.
     */
    if ((Timer->TimerListEntry.Flink == &ListHead) &&
        (Timer->TimerListEntry.Blink == &ListHead))
    {
        Timer->TimerListEntry.Flink = NULL;
        Timer->TimerListEntry.Blink = NULL;
    }

    /* Release the timer object lock */
    KiReleaseDispatcherObject(&Timer->Header);

    /* Request a DPC if needed */
    if (RequestInterrupt) HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
}
#if defined(__GNUC__) && !defined(__clang__) && (__GNUC__ >= 12)
#pragma GCC diagnostic pop
#endif

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeCancelTimer(IN OUT PKTIMER Timer)
{
    KIRQL OldIrql;
    BOOLEAN Inserted;
    ASSERT_TIMER(Timer);
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    DPRINT("KeCancelTimer(): Timer %p\n", Timer);

    /* Raise IRQL and acquire the timer object lock */
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Timer->Header);

    /* Check if it's inserted, and remove it if it is */
    Inserted = Timer->Header.Inserted;
    if (Inserted) KxRemoveTreeTimer(Timer);

    /* Release the timer object lock */
    KiReleaseDispatcherObject(&Timer->Header);
    KeLowerIrql(OldIrql);

    /* Return the old state */
    return Inserted;
}

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeTimer(OUT PKTIMER Timer)
{
    /* Call the New Function */
    KeInitializeTimerEx(Timer, NotificationTimer);
}

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeTimerEx(OUT PKTIMER Timer,
                    IN TIMER_TYPE Type)
{
    DPRINT("KeInitializeTimerEx(): Timer %p, Type %s\n",
            Timer, (Type == NotificationTimer) ?
           "NotificationTimer" : "SynchronizationTimer");

    /* Initialize the Dispatch Header */
    ASSERT((Type == NotificationTimer) || (Type == SynchronizationTimer));
    Timer->Header.Type = TimerNotificationObject + Type;
    Timer->Header.TimerControlFlags = 0;
    Timer->Header.Hand = 0;
#if (NTDDI_VERSION >= NTDDI_WIN7) || defined(_M_ARM64)
    Timer->Header.TimerMiscFlags = 0;
#else
    Timer->Header.Inserted = 0;
#endif
    Timer->Header.SignalState = 0;
    InitializeListHead(&(Timer->Header.WaitListHead));

    /* Initialize the Other data */
    Timer->DueTime.QuadPart = 0;
    Timer->Period = 0;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeReadStateTimer(IN PKTIMER Timer)
{
    /* Return the Signal State */
    ASSERT_TIMER(Timer);
    return (BOOLEAN)Timer->Header.SignalState;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeSetTimer(IN OUT PKTIMER Timer,
           IN LARGE_INTEGER DueTime,
           IN PKDPC Dpc OPTIONAL)
{
    /* Call the newer function and supply a period of 0 */
    return KeSetTimerEx(Timer, DueTime, 0, Dpc);
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeSetTimerEx(IN OUT PKTIMER Timer,
             IN LARGE_INTEGER DueTime,
             IN LONG Period,
             IN PKDPC Dpc OPTIONAL)
{
    KIRQL OldIrql;
    BOOLEAN Inserted;
    ULONG Hand = 0;
    BOOLEAN RequestInterrupt = FALSE;
    ASSERT_TIMER(Timer);
    ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);
    DPRINT("KeSetTimerEx(): Timer %p, DueTime %I64d, Period %d, Dpc %p\n",
           Timer, DueTime.QuadPart, Period, Dpc);

    /* Raise IRQL and acquire the timer object lock */
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Timer->Header);

    /* Check if it's inserted, and remove it if it is */
    Inserted = Timer->Header.Inserted;
    if (Inserted) KxRemoveTreeTimer(Timer);

    /* Set Default Timer Data */
    Timer->Dpc = Dpc;
    Timer->Period = Period;
    if (!KiComputeDueTime(Timer, DueTime, &Hand))
    {
        /* Signal the timer */
        RequestInterrupt = KiSignalTimer(Timer);

        /* Release the timer object lock */
        KiReleaseDispatcherObject(&Timer->Header);

        /* Check if we need to do an interrupt */
        if (RequestInterrupt) HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }
    else
    {
        /* Insert the timer */
        Timer->Header.SignalState = FALSE;
        KiReleaseDispatcherObject(&Timer->Header);
        KxInsertTimerNoRelease(Timer, Hand);
    }

    /* Exit the dispatcher */
    KiExitDispatcher(OldIrql);

    /* Return old state */
    return Inserted;
}
