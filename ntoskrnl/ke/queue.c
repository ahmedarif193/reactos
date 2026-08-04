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

/*
 * Called when a thread which has a queue entry is entering a wait state
 */
VOID
FASTCALL
KiActivateWaiterQueue(IN PKQUEUE Queue)
{
    PLIST_ENTRY QueueEntry;
    PLIST_ENTRY WaitEntry;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD Thread;
    ASSERT_QUEUE(Queue);

    /* Decrement the number of active threads */
    Queue->CurrentCount--;

    /* Make sure the counts are OK */
    if (Queue->CurrentCount < Queue->MaximumCount)
    {
        /* Get the Queue Entry */
        QueueEntry = Queue->EntryListHead.Flink;

        /* Get the Wait Entry */
        WaitEntry = Queue->Header.WaitListHead.Blink;

        /* Make sure that the Queue entries are not part of empty lists */
        if ((WaitEntry != &Queue->Header.WaitListHead) &&
            (QueueEntry != &Queue->EntryListHead))
        {
            /* Get the Wait Block and the waiting Thread */
            WaitBlock = CONTAINING_RECORD(WaitEntry,
                                          KWAIT_BLOCK,
                                          WaitListEntry);
            Thread = WaitBlock->Thread;

            /* Unwait it under its thread lock, and only if still waiting: the
             * wake and the PRCB wait-list teardown (KiUnlinkThread) must be
             * serialized by the thread lock against the wait commit. */
            KiAcquireThreadLock(Thread);
            if (Thread->State == Waiting)
            {
                /* Remove this entry */
                RemoveEntryList(QueueEntry);
                QueueEntry->Flink = NULL;

                /* Decrease the Signal State */
                Queue->Header.SignalState--;

                /* Eagerly unlink the wait block from the queue's wait list.
                 * The thread's embedded WaitBlock is reused by its next wait, so
                 * leaving it linked here makes one LIST_ENTRY reachable from two
                 * dispatcher lists -> a later KiWaitTest walks a stale block and
                 * dereferences a garbage WaitBlock->Thread. (Caller holds the
                 * queue object lock; Win11 unlinks wait blocks eagerly.) */
                RemoveEntryList(WaitEntry);
                WaitEntry->Flink = NULL;
                WaitBlock->BlockState = WaitBlockInactive;
                KiUnwaitThread(Thread, (LONG_PTR)QueueEntry, IO_NO_INCREMENT);
            }
            KiReleaseThreadLock(Thread);
        }
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
    PKWAIT_BLOCK WaitBlock;
    PLIST_ENTRY WaitEntry;
    ASSERT_QUEUE(Queue);

    /* Save the old state */
    InitialState = Queue->Header.SignalState;

    /* Get the Entry */
    WaitEntry = Queue->Header.WaitListHead.Blink;

    /*
     * Why the KeGetCurrentThread()->Queue != Queue?
     * KiInsertQueue might be called from an APC for the current thread.
     * -Gunnar
     */
    if ((Queue->CurrentCount < Queue->MaximumCount) &&
        (WaitEntry != &Queue->Header.WaitListHead) &&
        ((Thread->Queue != Queue) ||
         (Thread->WaitReason != WrQueue)))
    {
        /* Get the Wait Block and Thread */
        WaitBlock = CONTAINING_RECORD(WaitEntry, KWAIT_BLOCK, WaitListEntry);
        Thread = WaitBlock->Thread;

        KiAcquireThreadLock(Thread);
        if (Thread->State == Waiting)
        {
            RemoveEntryList(WaitEntry);
            WaitEntry->Flink = NULL;
            WaitBlock->BlockState = WaitBlockInactive;
            KiUnwaitThread(Thread, (LONG_PTR)Entry, IO_NO_INCREMENT);
            KiReleaseThreadLock(Thread);
            return InitialState;
        }
        KiReleaseThreadLock(Thread);
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
PLIST_ENTRY
NTAPI
KiRemoveQueue(IN PKQUEUE Queue,
              IN KPROCESSOR_MODE WaitMode,
              IN BOOLEAN Alertable,
              IN PLARGE_INTEGER Timeout OPTIONAL)
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
        Queue->CurrentCount--;
    }

    /* Loop until the queue is processed */
    while (TRUE)
    {
        /* Check if the counts are valid and if there is still a queued entry */
        QueueEntry = Queue->EntryListHead.Flink;
        if ((Queue->CurrentCount < Queue->MaximumCount) &&
            (QueueEntry != &Queue->EntryListHead))
        {
            /* Decrease the number of entries */
            Queue->Header.SignalState--;

            /* Increase numbef of running threads */
            Queue->CurrentCount++;

            /* Check if the entry is valid. If not, bugcheck */
            if (!(QueueEntry->Flink) || !(QueueEntry->Blink))
            {
                /* Invalid item */
                KeBugCheckEx(INVALID_WORK_QUEUE_ITEM,
                             (ULONG_PTR)QueueEntry,
                             (ULONG_PTR)Queue,
                             (ULONG_PTR)NULL,
                             (ULONG_PTR)((PWORK_QUEUE_ITEM)QueueEntry)->
                                         WorkerRoutine);
            }

            /* Remove the Entry */
            RemoveEntryList(QueueEntry);
            QueueEntry->Flink = NULL;

            /* Nothing to wait on */
            break;
        }
        else
        {
            /* Check if a kernel APC is pending and we're below APC_LEVEL */
            if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
            {
                /* Increment the count and unlock the queue */
                Queue->CurrentCount++;
                KiReleaseDispatcherObject(&Queue->Header);
                KiExitDispatcher(Thread->WaitIrql);
            }
            else
            {
                Status = KiCheckAlertability(Thread, Alertable, WaitMode);
                if ((NTSTATUS)Status != STATUS_WAIT_0)
                {
                    QueueEntry = (PLIST_ENTRY)Status;
                    Queue->CurrentCount++;
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
                        QueueEntry = (PLIST_ENTRY)STATUS_TIMEOUT;
                        Queue->CurrentCount++;
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
                    Queue->CurrentCount++;
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
                    return (PLIST_ENTRY)Status;
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
            Queue->CurrentCount--;
        }
    }

    /* Unlock the queue and return */
    KiReleaseDispatcherObject(&Queue->Header);
    KiExitDispatcher(Thread->WaitIrql);
    return QueueEntry;
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
    ASSERT(IsListEmpty(&Queue->Header.WaitListHead));

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
    }

    /* Loop the list */
    while (!IsListEmpty(&Queue->ThreadListHead))
    {
        /* Get the next entry */
        NextEntry = Queue->ThreadListHead.Flink;

        /* Get the associated thread */
        Thread = CONTAINING_RECORD(NextEntry, KTHREAD, QueueListEntry);

        /* Clear its queue */
        Thread->Queue = NULL;

        /* Remove this entry */
        RemoveEntryList(NextEntry);
    }

    /* Release the queue object */
    KiReleaseDispatcherObject(&Queue->Header);

    /* Exit the dispatcher and return the first entry (if any) */
    KiExitDispatcher(OldIrql);
    return FirstEntry;
}

/* EOF */
