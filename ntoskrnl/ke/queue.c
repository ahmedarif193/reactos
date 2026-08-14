/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/queue.c
 * PURPOSE:         Implements kernel queues
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Gunnar Dalsnes
 *                  Eric Kohl
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS *********************************************************/

static BOOLEAN
KiWakeQueueDequeueWaiter(
    _Inout_ PKQUEUE Queue,
    _Inout_ PLIST_ENTRY QueueEntry,
    _In_ BOOLEAN EntryIsQueued)
{
    PLIST_ENTRY PreviousEntry;
    PLIST_ENTRY WaitEntry;
    PLIST_ENTRY WaitList;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD Thread;

    WaitList = &Queue->Header.WaitListHead;
    WaitEntry = WaitList->Blink;
    while (WaitEntry != WaitList)
    {
        PreviousEntry = WaitEntry->Blink;
        WaitBlock = CONTAINING_RECORD(WaitEntry, KWAIT_BLOCK, WaitListEntry);
        if (WaitBlock->WaitType != WaitDequeue)
        {
            WaitEntry = PreviousEntry;
            continue;
        }

        Thread = WaitBlock->Thread;
        KiAcquireThreadLock(Thread);
        if ((Thread->State == Waiting) && (WaitBlock->BlockState == WaitBlockActive))
        {
            if (EntryIsQueued)
            {
                RemoveEntryList(QueueEntry);
                QueueEntry->Flink = NULL;
                Queue->Header.SignalState--;
            }

            RemoveEntryList(WaitEntry);
            WaitEntry->Flink = NULL;
            WaitBlock->BlockState = WaitBlockInactive;
            KiUnwaitThread(Thread, (LONG_PTR)QueueEntry, IO_NO_INCREMENT);
            KiReleaseThreadLock(Thread);
            return TRUE;
        }
        KiReleaseThreadLock(Thread);
        WaitEntry = PreviousEntry;
    }

    return FALSE;
}

static VOID
KiRundownQueueWaitersLocked(
    _Inout_ PKQUEUE Queue)
{
    PLIST_ENTRY WaitEntry;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD Thread;
    LONG_PTR WaitStatus;

    while (!IsListEmpty(&Queue->Header.WaitListHead))
    {
        WaitEntry = Queue->Header.WaitListHead.Flink;
        WaitBlock = CONTAINING_RECORD(WaitEntry, KWAIT_BLOCK, WaitListEntry);
        Thread = WaitBlock->Thread;

        KiAcquireThreadLock(Thread);
        RemoveEntryList(WaitEntry);
        WaitEntry->Flink = NULL;
        if ((Thread->State == Waiting) && (WaitBlock->BlockState == WaitBlockActive))
        {
            WaitBlock->BlockState = WaitBlockInactive;
            if (WaitBlock->WaitType == WaitDequeue)
            {
                KiIncrementQueueCurrentCount(Queue);
                WaitStatus = STATUS_ABANDONED;
            }
            else if (WaitBlock->WaitType == WaitAny)
                WaitStatus = WaitBlock->WaitKey;
            else
                WaitStatus = STATUS_KERNEL_APC;
            KiUnwaitThread(Thread, WaitStatus, IO_NO_INCREMENT);
        }
        else
        {
            WaitBlock->BlockState = WaitBlockInactive;
        }
        KiReleaseThreadLock(Thread);
    }
}

/*
 * Called when a thread which has a queue entry is entering a wait state
 */
VOID
FASTCALL
KiActivateWaiterQueue(IN PKQUEUE Queue)
{
    PLIST_ENTRY QueueEntry;
    ASSERT_QUEUE(Queue);

    /* Decrement the number of active threads */
    KiDecrementQueueCurrentCount(Queue);

    /* Make sure the counts are OK */
    if (Queue->CurrentCount < Queue->MaximumCount)
    {
        /* Get the Queue Entry */
        QueueEntry = Queue->EntryListHead.Flink;

        if (QueueEntry != &Queue->EntryListHead)
            KiWakeQueueDequeueWaiter(Queue, QueueEntry, TRUE);
    }
}

/*
 * Returns the previous number of entries in the queue
 */
LONG
NTAPI
KiInsertQueue(IN PKQUEUE Queue,
              IN PLIST_ENTRY Entry,
              IN BOOLEAN Head)
{
    ULONG InitialState;
    PKTHREAD Thread = KeGetCurrentThread();
    ASSERT_QUEUE(Queue);

    /* Save the old state */
    InitialState = Queue->Header.SignalState;

    if ((Queue->CurrentCount < Queue->MaximumCount) &&
        ((Thread->Queue != Queue) || (Thread->WaitReason != WrQueue)) &&
        KiWakeQueueDequeueWaiter(Queue, Entry, FALSE))
    {
        return InitialState;
    }

    /* Increase the Entries */
    Queue->Header.SignalState++;

    /* Check which mode we're using */
    if (Head)
    {
        /* Insert in the head */
        InsertHeadList(&Queue->EntryListHead, Entry);
    }
    else
    {
        /* Insert at the end */
        InsertTailList(&Queue->EntryListHead, Entry);
    }

    /* Ordinary object waiters observe the signaled queue without consuming
     * its entry. A single transition wakes every such waiter. */
    if ((InitialState == 0) && !IsListEmpty(&Queue->Header.WaitListHead))
        KiWaitTest(Queue, IO_NO_INCREMENT);

    /* Return the previous state */
    return InitialState;
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeQueue(IN PKQUEUE Queue,
                  IN ULONG Count OPTIONAL)
{
    /* Initialize the Header */
    Queue->Header.Type = QueueObject;
    Queue->Header.Abandoned = 0;
    Queue->Header.Size = sizeof(KQUEUE) / sizeof(ULONG);
    Queue->Header.SignalState = 0;
    InitializeListHead(&(Queue->Header.WaitListHead));

    /* Initialize the Lists */
    InitializeListHead(&Queue->EntryListHead);
    InitializeListHead(&Queue->ThreadListHead);

    /* Set the Current and Maximum Count */
    Queue->CurrentCount = 0;
    Queue->MaximumCount = (Count == 0) ? (ULONG) KeNumberProcessors : Count;
}

/*
 * @implemented
 */
LONG
NTAPI
KeInsertHeadQueue(IN PKQUEUE Queue,
                  IN PLIST_ENTRY Entry)
{
    LONG PreviousState;
    KIRQL OldIrql;
    ASSERT_QUEUE(Queue);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the Queue object */
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Queue->Header);

    /* Insert the Queue */
    PreviousState = KiInsertQueue(Queue, Entry, TRUE);

    /* Release the Queue object */
    KiReleaseDispatcherObject(&Queue->Header);
    KiExitDispatcher(OldIrql);

    /* Return previous State */
    return PreviousState;
}

/*
 * @implemented
 */
LONG
NTAPI
KeInsertQueue(IN PKQUEUE Queue,
              IN PLIST_ENTRY Entry)
{
    LONG PreviousState;
    KIRQL OldIrql;
    ASSERT_QUEUE(Queue);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the Queue object */
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Queue->Header);

    /* Insert the Queue */
    PreviousState = KiInsertQueue(Queue, Entry, FALSE);

    /* Release the Queue object */
    KiReleaseDispatcherObject(&Queue->Header);
    KiExitDispatcher(OldIrql);

    /* Return previous State */
    return PreviousState;
}

/*
 * @implemented
 *
 * Returns number of entries in the queue
 */
LONG
NTAPI
KeReadStateQueue(IN PKQUEUE Queue)
{
    /* Returns the Signal State */
    ASSERT_QUEUE(Queue);
    return Queue->Header.SignalState;
}

/*
 * @implemented
 */
static ULONG
KiDrainQueueEntries(
    _Inout_ PKQUEUE Queue,
    _Out_writes_to_(Count, return) PLIST_ENTRY *EntryArray,
    _In_ ULONG EntriesRemoved,
    _In_ ULONG Count)
{
    PLIST_ENTRY QueueEntry;

    do
    {
        QueueEntry = Queue->EntryListHead.Flink;
        Queue->Header.SignalState--;

        if (!(QueueEntry->Flink) || !(QueueEntry->Blink))
            KeBugCheckEx(INVALID_WORK_QUEUE_ITEM, (ULONG_PTR)QueueEntry, (ULONG_PTR)Queue, (ULONG_PTR)NULL, (ULONG_PTR)((PWORK_QUEUE_ITEM)QueueEntry)->WorkerRoutine);

        RemoveEntryList(QueueEntry);
        QueueEntry->Flink = NULL;
        EntryArray[EntriesRemoved++] = QueueEntry;
    } while ((EntriesRemoved < Count) && !IsListEmpty(&Queue->EntryListHead));

    return EntriesRemoved;
}

static BOOLEAN
KiIsQueueWaitStatus(_In_ LONG_PTR Status)
{
    return ((Status == STATUS_ABANDONED) ||
            (Status == STATUS_USER_APC) ||
            (Status == STATUS_ALERTED) ||
            (Status == STATUS_TIMEOUT));
}

static ULONG
NTAPI
KiRemoveQueueExInternal(
    _Inout_ PKQUEUE Queue,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_writes_to_(Count, return) PLIST_ENTRY *EntryArray,
    _In_ ULONG Count)
{
    PLIST_ENTRY QueueEntry;
    LONG_PTR Status;
    PKTHREAD Thread = KeGetCurrentThread();
    PKQUEUE PreviousQueue;
    PKWAIT_BLOCK WaitBlock = &Thread->WaitBlock[0];
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    PKTIMER Timer = &Thread->Timer;
    BOOLEAN Swappable;
    PLARGE_INTEGER OriginalDueTime = Timeout;
    LARGE_INTEGER DueTime = {{0}}, NewDueTime, InterruptTime;
    ULONG Hand = 0;
    ULONG EntriesRemoved = 0;
    ASSERT_QUEUE(Queue);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);


    /* Check if the Lock is already held */
    if (Thread->WaitNext)
    {
        /* It is, so next time don't do expect this */
        Thread->WaitNext = FALSE;
        KxQueueThreadWait();
    }
    else
    {
        /* Raise IRQL to synch and prepare the wait */
        Thread->WaitIrql = KeRaiseIrqlToSynchLevel();
        KxQueueThreadWait();
    }
    Thread->Alertable = Alertable;

    PreviousQueue = Thread->Queue;
    QueueEntry = &Thread->QueueListEntry;

    if ((Queue != PreviousQueue) && (PreviousQueue != NULL))
    {
        KiAcquireDispatcherObject(&PreviousQueue->Header);
        RemoveEntryList(QueueEntry);
        KiActivateWaiterQueue(PreviousQueue);
        KiReleaseDispatcherObject(&PreviousQueue->Header);
    }

    Thread->Queue = Queue;
    KiAcquireDispatcherObject(&Queue->Header);

    /* Check if this is a different queue */
    if (Queue != PreviousQueue)
    {
        /* Insert in this new Queue */
        InsertTailList(&Queue->ThreadListHead, QueueEntry);
    }
    else
    {
        /* Same queue, decrement waiting threads */
        KiDecrementQueueCurrentCount(Queue);
    }

    /* Loop until the queue is processed */
    while (TRUE)
    {
        if (Queue->Header.Abandoned)
        {
            EntryArray[0] = (PLIST_ENTRY)(ULONG_PTR)STATUS_ABANDONED;
            EntriesRemoved = 1;
            KiIncrementQueueCurrentCount(Queue);
            break;
        }

        /* Check if the counts are valid and if there is still a queued entry */
        QueueEntry = Queue->EntryListHead.Flink;
        if ((Queue->CurrentCount < Queue->MaximumCount) &&
            (QueueEntry != &Queue->EntryListHead))
        {
            /* Increase number of running threads */
            KiIncrementQueueCurrentCount(Queue);
            EntriesRemoved = KiDrainQueueEntries(Queue, EntryArray, EntriesRemoved, Count);

            /* Nothing to wait on */
            break;
        }
        else
        {
            /* Check if a kernel APC is pending and we're below APC_LEVEL */
            if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
            {
                /* Increment the count and unlock the queue */
                KiIncrementQueueCurrentCount(Queue);
                KiReleaseDispatcherObject(&Queue->Header);
                KiExitDispatcher(Thread->WaitIrql);
            }
            else
            {
                Status = KiCheckAlertability(Thread, Alertable, WaitMode);
                if ((NTSTATUS)Status != STATUS_WAIT_0)
                {
                    EntryArray[0] = (PLIST_ENTRY)Status;
                    EntriesRemoved = 1;
                    KiIncrementQueueCurrentCount(Queue);
                    break;
                }

                /* Enable the Timeout Timer if there was any specified */
                if (Timeout)
                {
                    /* Check if the timer expired */
                    InterruptTime.QuadPart = KeQueryInterruptTime();
                    if ((ULONG64)InterruptTime.QuadPart >= Timer->DueTime.QuadPart)
                    {
                        /* It did, so we don't need to wait */
                        EntryArray[0] = (PLIST_ENTRY)(ULONG_PTR)STATUS_TIMEOUT;
                        EntriesRemoved = 1;
                        KiIncrementQueueCurrentCount(Queue);
                        break;
                    }

                    /* It didn't, so prepare it */
                    Timer->TimerListEntry.Flink = NULL;
                    Timer->TimerListEntry.Blink = NULL;
                }

                /* Serialize APC insertion with the final wait publication. */
                if (!KxTryBeginThreadWait(Thread))
                {
                    /* Not blocking after all: restore the active count and
                     * retry, exactly like the pre-wait APC check above. */
                    KiIncrementQueueCurrentCount(Queue);
                    KiReleaseDispatcherObject(&Queue->Header);
                    KiExitDispatcher(Thread->WaitIrql);
                    goto WaitStart;
                }

                /* Insert the wait block in the list */
                WaitBlock->BlockState = WaitBlockActive;
                if (Timeout)
                {
                    Timer->Header.Inserted = TRUE;
                    TimerBlock->BlockState = WaitBlockActive;
                }
                InsertTailList(&Queue->Header.WaitListHead,
                               &WaitBlock->WaitListEntry);

                /* Publish the wait and release the thread lock */
                KxCommitThreadWait(Thread, Swappable);

                /* Release the queue object */
                KiReleaseDispatcherObject(&Queue->Header);

                /* Check if we have a timer */
                if (Timeout)
                {
                    /* Insert it */
                    KxInsertTimerNoRelease(Timer, Hand);
                }

                /* Do the actual swap */
                Status = KiSwapThread(Thread, KeGetCurrentPrcb(), TRUE);

                /* Reset the wait reason */
                Thread->WaitReason = 0;

                /* Check if we were executing an APC */
                if (Status != STATUS_KERNEL_APC)
                {
                    if (WaitBlock->WaitListEntry.Flink)
                    {
                        KIRQL CleanupIrql = KeRaiseIrqlToSynchLevel();
                        KiAcquireDispatcherObject(&Queue->Header);
                        if (WaitBlock->WaitListEntry.Flink)
                        {
                            RemoveEntryList(&WaitBlock->WaitListEntry);
                            WaitBlock->WaitListEntry.Flink = NULL;
                        }
                        KiReleaseDispatcherObject(&Queue->Header);
                        KeLowerIrql(CleanupIrql);
                    }

                    EntryArray[0] = (PLIST_ENTRY)Status;
                    EntriesRemoved = 1;
                    if ((Count > 1) && !KiIsQueueWaitStatus(Status))
                    {
                        KIRQL DrainIrql = KeRaiseIrqlToSynchLevel();
                        KiAcquireDispatcherObject(&Queue->Header);
                        if (!IsListEmpty(&Queue->EntryListHead))
                            EntriesRemoved = KiDrainQueueEntries(Queue, EntryArray, EntriesRemoved, Count);
                        KiReleaseDispatcherObject(&Queue->Header);
                        KeLowerIrql(DrainIrql);
                    }
                    return EntriesRemoved;
                }

                /* Check if we had a timeout */
                if (Timeout)
                {
                    /* Recalculate due times */
                    Timeout = KiRecalculateDueTime(OriginalDueTime,
                                                   &DueTime,
                                                   &NewDueTime);
                }
            }

WaitStart:
            /* Start another wait */
            Thread->WaitIrql = KeRaiseIrqlToSynchLevel();
            KxQueueThreadWait();
            Thread->Alertable = Alertable;
            KiAcquireDispatcherObject(&Queue->Header);
            KiDecrementQueueCurrentCount(Queue);
        }
    }

    /* Unlock the queue and return */
    KiReleaseDispatcherObject(&Queue->Header);
    KiExitDispatcher(Thread->WaitIrql);
    return EntriesRemoved;
}

PLIST_ENTRY
NTAPI
KiRemoveQueue(
    _Inout_ PKQUEUE Queue,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PLIST_ENTRY Entry;

    KiRemoveQueueExInternal(Queue, WaitMode, Alertable, Timeout, &Entry, 1);
    return Entry;
}

PLIST_ENTRY
NTAPI
KeRemoveQueue(IN PKQUEUE Queue,
              IN KPROCESSOR_MODE WaitMode,
              IN PLARGE_INTEGER Timeout OPTIONAL)
{
    return KiRemoveQueue(Queue, WaitMode, FALSE, Timeout);
}

/*
 * @implemented
 */
ULONG
NTAPI
KeRemoveQueueEx(
    _Inout_ PKQUEUE Queue,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_writes_to_(Count, return) PLIST_ENTRY *EntryArray,
    _In_ ULONG Count)
{
    return KiRemoveQueueExInternal(Queue, WaitMode, Alertable, Timeout, EntryArray, Count);
}

/*
 * @implemented
 */
PLIST_ENTRY
NTAPI
KeRundownQueue(IN PKQUEUE Queue)
{
    PLIST_ENTRY FirstEntry, NextEntry;
    PKTHREAD Thread;
    KIRQL OldIrql;
#if defined(_M_ARM64)
    DPRINT1("[arm64][queue] KeRundownQueue: queue=%p irql=%u\n",
            Queue,
            KeGetCurrentIrql());
#endif
    ASSERT_QUEUE(Queue);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the Queue object */
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Queue->Header);

    /* Check if the list is empty */
    FirstEntry = Queue->EntryListHead.Flink;
    if (FirstEntry == &Queue->EntryListHead)
    {
        /* We won't return anything */
        FirstEntry = NULL;
    }
    else
    {
        /* Remove this entry */
        RemoveEntryList(&Queue->EntryListHead);
        InitializeListHead(&Queue->EntryListHead);
        Queue->Header.SignalState = 0;
    }

    Queue->Header.Abandoned = TRUE;

    /* Loop the list */
    while (!IsListEmpty(&Queue->ThreadListHead))
    {
        /* Get the next entry */
        NextEntry = Queue->ThreadListHead.Flink;

        /* Get the associated thread */
        Thread = CONTAINING_RECORD(NextEntry, KTHREAD, QueueListEntry);

        /* Clear its queue */
        KiAcquireThreadLock(Thread);
        Thread->Queue = NULL;

        /* Remove this entry */
        RemoveEntryList(NextEntry);
        KiReleaseThreadLock(Thread);
    }

    /* Dequeue waiters become active as rundown wakes them. */
    Queue->CurrentCount = 0;
    KiRundownQueueWaitersLocked(Queue);

    /* Release the queue object */
    KiReleaseDispatcherObject(&Queue->Header);

    /* Exit the dispatcher and return the first entry (if any) */
    KiExitDispatcher(OldIrql);
    return FirstEntry;
}

/* EOF */
