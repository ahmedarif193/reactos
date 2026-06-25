/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/eventobj.c
 * PURPOSE:         Implements the Event Dispatcher Object
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
VOID
NTAPI
KeClearEvent(IN PKEVENT Event)
{
    ASSERT_EVENT(Event);

    /* Reset Signal State */
    Event->Header.SignalState = FALSE;
}

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeEvent(OUT PKEVENT Event,
                  IN EVENT_TYPE Type,
                  IN BOOLEAN State)
{
    /* Initialize the Dispatcher Header */
    ASSERT((Type == NotificationEvent) || (Type == SynchronizationEvent));
    Event->Header.Type = EventNotificationObject + Type;
    Event->Header.Signalling = FALSE;
    Event->Header.Size = sizeof(KEVENT) / sizeof(ULONG);
    Event->Header.SignalState = State;
    InitializeListHead(&(Event->Header.WaitListHead));
}

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeEventPair(IN PKEVENT_PAIR EventPair)
{
    /* Initialize the Event Pair Type and Size */
    EventPair->Type = EventPairObject;
    EventPair->Size = sizeof(KEVENT_PAIR);

    /* Initialize the two Events */
    KeInitializeEvent(&EventPair->LowEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&EventPair->HighEvent, SynchronizationEvent, FALSE);
}

/*
 * @implemented
 */
LONG
NTAPI
KePulseEvent(IN PKEVENT Event,
             IN KPRIORITY Increment,
             IN BOOLEAN Wait)
{
    KIRQL OldIrql;
    LONG PreviousState;
    PKTHREAD Thread;
    ASSERT_EVENT(Event);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Event->Header);

    PreviousState = Event->Header.SignalState;

    if (!PreviousState && !IsListEmpty(&Event->Header.WaitListHead))
    {
        Event->Header.SignalState = 1;
        KiWaitTest(Event, Increment);
    }

    Event->Header.SignalState = 0;

    KiReleaseDispatcherObject(&Event->Header);

    if (Wait == FALSE)
    {
        KiExitDispatcher(OldIrql);
    }
    else
    {
        Thread = KeGetCurrentThread();
        Thread->WaitNext = TRUE;
        Thread->WaitIrql = OldIrql;
    }

    return PreviousState;
}

/*
 * @implemented
 */
LONG
NTAPI
KeReadStateEvent(IN PKEVENT Event)
{
    ASSERT_EVENT(Event);

    /* Return the Signal State */
    return Event->Header.SignalState;
}

/*
 * @implemented
 */
LONG
NTAPI
KeResetEvent(IN PKEVENT Event)
{
    KIRQL OldIrql;
    LONG PreviousState;

    ASSERT_EVENT(Event);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Event->Header);

    PreviousState = Event->Header.SignalState;
    Event->Header.SignalState = 0;

    KiReleaseDispatcherObject(&Event->Header);
    KeLowerIrql(OldIrql);

    return PreviousState;
}

/*
 * @implemented
 */
LONG
NTAPI
KeSetEvent(IN PKEVENT Event,
           IN KPRIORITY Increment,
           IN BOOLEAN Wait)
{
    KIRQL OldIrql;
    LONG PreviousState;
    PKTHREAD Thread;
    ASSERT_EVENT(Event);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /*
     * Check if this is an signaled notification event without an upcoming wait.
     * In this case, we can immediately return TRUE, without locking.
     */
    if ((Event->Header.Type == EventNotificationObject) &&
        (Event->Header.SignalState == 1) &&
        !(Wait))
    {
        /* Return the signal state (TRUE/Signalled) */
        return TRUE;
    }

    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Event->Header);

    PreviousState = Event->Header.SignalState;
    Event->Header.SignalState = 1;

    if (!(PreviousState) && !(IsListEmpty(&Event->Header.WaitListHead)))
    {
        KiWaitTest(Event, Increment);
    }

    KiReleaseDispatcherObject(&Event->Header);

    if (!Wait)
    {
        KiExitDispatcher(OldIrql);
    }
    else
    {
        Thread = KeGetCurrentThread();
        Thread->WaitNext = TRUE;
        Thread->WaitIrql = OldIrql;
    }

    return PreviousState;
}

/*
 * @implemented
 */
VOID
NTAPI
KeSetEventBoostPriority(IN PKEVENT Event,
                        IN PKTHREAD *WaitingThread OPTIONAL)
{
    KIRQL OldIrql;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD Thread = KeGetCurrentThread(), WaitThread;
    ASSERT((Event->Header.Type & KOBJECT_TYPE_MASK) == EventSynchronizationObject);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Event->Header);

    if (IsListEmpty(&Event->Header.WaitListHead))
    {
        Event->Header.SignalState = 1;
        KiReleaseDispatcherObject(&Event->Header);
        KiExitDispatcher(OldIrql);
        return;
    }

    WaitBlock = CONTAINING_RECORD(Event->Header.WaitListHead.Flink,
                                  KWAIT_BLOCK,
                                  WaitListEntry);

    if (WaitBlock->WaitType == WaitAll)
    {
        Event->Header.SignalState = 1;
        KiWaitTest(Event, EVENT_INCREMENT);
    }
    else
    {
        WaitThread = WaitBlock->Thread;
        if (WaitingThread) *WaitingThread = WaitThread;

        RemoveEntryList(&WaitBlock->WaitListEntry);
        WaitBlock->WaitListEntry.Flink = NULL;

        KiAcquireThreadLock(WaitThread);
        if (WaitThread->State == Waiting)
        {
            Thread->Priority = KiComputeNewPriority(Thread, 0);
            WaitBlock->BlockState = WaitBlockInactive;
            KiUnlinkThread(WaitThread, STATUS_SUCCESS);
            WaitThread->AdjustIncrement = Thread->Priority;
            WaitThread->AdjustReason = AdjustBoost;
            KiReadyThread(WaitThread);
        }
        else
        {
            WaitBlock->BlockState = WaitBlockInactive;
        }
        KiReleaseThreadLock(WaitThread);
    }

    KiReleaseDispatcherObject(&Event->Header);
    KiExitDispatcher(OldIrql);
}

/* EOF */
