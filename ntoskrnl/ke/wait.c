/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/wait.c
 * PURPOSE:         Manages waiting for Dispatcher Objects
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Gunnar Dalsnes
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS *********************************************************/

VOID
FASTCALL
KiWaitTest(IN PVOID ObjectPointer,
           IN KPRIORITY Increment)
{
    PLIST_ENTRY WaitEntry, WaitList;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD WaitThread;
    PKMUTANT FirstObject = ObjectPointer;
    NTSTATUS WaitStatus;
    BOOLEAN IsQueue;

    IsQueue = ((FirstObject->Header.Type & KOBJECT_TYPE_MASK) == QueueObject);
    WaitList = &FirstObject->Header.WaitListHead;
    WaitEntry = WaitList->Flink;
    while ((FirstObject->Header.SignalState > 0) && (WaitEntry != WaitList))
    {
        WaitBlock = CONTAINING_RECORD(WaitEntry, KWAIT_BLOCK, WaitListEntry);
        WaitThread = WaitBlock->Thread;
        WaitEntry = WaitEntry->Flink;

        /* Dequeue waits consume an entry in KiInsertQueue and must not be
         * completed as ordinary dispatcher-object waits. */
        if (IsQueue && (WaitBlock->WaitType == WaitDequeue))
            continue;

        RemoveEntryList(&WaitBlock->WaitListEntry);
        WaitBlock->WaitListEntry.Flink = NULL;

        KiAcquireThreadLock(WaitThread);

        if (WaitThread->State == Waiting)
        {
            WaitStatus = STATUS_KERNEL_APC;
            if (WaitBlock->WaitType == WaitAny)
            {
                WaitStatus = (NTSTATUS)WaitBlock->WaitKey;
                KiSatisfyObjectWait(FirstObject, WaitThread);
            }
            WaitBlock->BlockState = WaitBlockInactive;
            KiUnwaitThread(WaitThread, WaitStatus, Increment);
        }
        else
        {
            WaitBlock->BlockState = WaitBlockInactive;
        }

        KiReleaseThreadLock(WaitThread);
    }
}

VOID
FASTCALL
KiUnlinkThread(IN PKTHREAD Thread,
               IN LONG_PTR WaitStatus)
{
    PKWAIT_BLOCK WaitBlock;
    PKTIMER Timer;

    /* Update wait status */
    Thread->WaitStatus |= WaitStatus;

#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
    {
        if (Thread->TimerActive)
        {
            if (KxRemoveTreeTimer(&Thread->Timer))
            {
                InitializeListHead(&Thread->Timer.Header.WaitListHead);
                Thread->WaitBlock[TIMER_WAIT_BLOCK].WaitListEntry.Flink = NULL;
                Thread->WaitBlock[TIMER_WAIT_BLOCK].WaitListEntry.Blink = NULL;
                Thread->WaitBlock[TIMER_WAIT_BLOCK].BlockState = WaitBlockInactive;
            }
            Thread->TimerActive = FALSE;
        }
        WaitBlock = Thread->WaitBlockList;
        DBG_UNREFERENCED_LOCAL_VARIABLE(WaitBlock);
    }
#else
    WaitBlock = Thread->WaitBlockList;
    do
    {
        WaitBlock->BlockState = WaitBlockInactive;
        WaitBlock = WaitBlock->NextWaitBlock;
    } while (WaitBlock != Thread->WaitBlockList);
#endif

    /* Remove the thread from the PRCB wait list under its PRCB wait lock.
     * Membership is tracked by WaitPrcb (published/cleared under that lock),
     * NOT by Flink: WaitListEntry is reused as the ready-queue link, so a
     * Ready thread's Flink must never be mistaken for wait-list membership. */
    {
        PKPRCB WaitPrcb = Thread->WaitPrcb;
        if (WaitPrcb)
        {
            KiAcquireWaitLock(WaitPrcb);
            if (Thread->WaitPrcb == WaitPrcb)
            {
                RemoveEntryList(&Thread->WaitListEntry);
                Thread->WaitListEntry.Flink = NULL;
                Thread->WaitPrcb = NULL;
            }
            KiReleaseWaitLock(WaitPrcb);
        }
    }

    /* Check if there's a Thread Timer */
    Timer = &Thread->Timer;
    if (Timer->Header.Inserted) KxRemoveTreeTimer(Timer);

    /* Increment the Queue's active threads */
    if (Thread->Queue) KiIncrementQueueCurrentCount(Thread->Queue);
}

VOID
FASTCALL
KiUnlinkWaitBlocks(IN PKTHREAD Thread)
{
#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
    PKWAIT_BLOCK WaitBlockArray = Thread->WaitBlockList;
    ULONG Count = Thread->WaitBlockCount;
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        PKWAIT_BLOCK WaitBlock = &WaitBlockArray[Index];
        PDISPATCHER_HEADER Object = (PDISPATCHER_HEADER)WaitBlock->Object;

        if (!Object) continue;
        if (WaitBlock->BlockState != WaitBlockActive) continue;

        KiAcquireDispatcherObject(Object);
        if (WaitBlock->BlockState == WaitBlockActive)
        {
            if (WaitBlock->WaitListEntry.Flink)
                RemoveEntryList(&WaitBlock->WaitListEntry);
            WaitBlock->WaitListEntry.Flink = NULL;
            WaitBlock->BlockState = WaitBlockInactive;
        }
        KiReleaseDispatcherObject(Object);
    }
#else
    PKWAIT_BLOCK FirstWaitBlock, WaitBlock, NextWaitBlock;
    PDISPATCHER_HEADER Object;

    FirstWaitBlock = Thread->WaitBlockList;
    WaitBlock = FirstWaitBlock;
    do
    {
        NextWaitBlock = WaitBlock->NextWaitBlock;
        Object = (PDISPATCHER_HEADER)WaitBlock->Object;

        if ((Object != NULL) && (WaitBlock->WaitListEntry.Flink != NULL))
        {
            KiAcquireDispatcherObject(Object);
            if (WaitBlock->WaitListEntry.Flink != NULL)
            {
                RemoveEntryList(&WaitBlock->WaitListEntry);
                WaitBlock->WaitListEntry.Flink = NULL;
                WaitBlock->BlockState = WaitBlockInactive;
            }
            KiReleaseDispatcherObject(Object);
        }

        WaitBlock = NextWaitBlock;
    } while (WaitBlock != FirstWaitBlock);
#endif
}

/* Must be called with the dispatcher lock held */
VOID
FASTCALL
KiUnwaitThread(IN PKTHREAD Thread,
               IN LONG_PTR WaitStatus,
               IN KPRIORITY Increment)
{
    /* Unlink the thread */
    KiUnlinkThread(Thread, WaitStatus);

    /* Tell the scheduler do to the increment when it readies the thread */
    ASSERT(Increment >= 0);
    Thread->AdjustIncrement = (SCHAR)Increment;
    Thread->AdjustReason = AdjustUnwait;

    /* Reschedule the Thread */
    KiReadyThread(Thread);
}

static
VOID
FASTCALL
KiSortObjectLocks(IN PVOID Object[], IN ULONG Count, OUT PVOID Sorted[])
{
    ULONG i, j;
    PVOID Tmp;

    for (i = 0; i < Count; i++) Sorted[i] = Object[i];
    for (i = 1; i < Count; i++)
    {
        Tmp = Sorted[i];
        j = i;
        while ((j > 0) && ((ULONG_PTR)Sorted[j - 1] > (ULONG_PTR)Tmp))
        {
            Sorted[j] = Sorted[j - 1];
            j--;
        }
        Sorted[j] = Tmp;
    }
}

VOID
FASTCALL
KiAcquireObjectLocks(IN PVOID Object[], IN ULONG Count)
{
    PVOID Sorted[MAXIMUM_WAIT_OBJECTS];
    ULONG i;

    KiSortObjectLocks(Object, Count, Sorted);
    for (i = 0; i < Count; i++)
    {
        if ((i == 0) || (Sorted[i] != Sorted[i - 1]))
            KiAcquireDispatcherObject((DISPATCHER_HEADER*)Sorted[i]);
    }
}

VOID
FASTCALL
KiReleaseObjectLocks(IN PVOID Object[], IN ULONG Count)
{
    PVOID Sorted[MAXIMUM_WAIT_OBJECTS];
    ULONG i;

    KiSortObjectLocks(Object, Count, Sorted);
    for (i = 0; i < Count; i++)
    {
        if ((i == 0) || (Sorted[i] != Sorted[i - 1]))
            KiReleaseDispatcherObject((DISPATCHER_HEADER*)Sorted[i]);
    }
}

VOID
FASTCALL
KiAcquireFastMutex(IN PFAST_MUTEX FastMutex)
{
#if (NTDDI_VERSION >= NTDDI_VISTA)
    ULONG BitsToRemove, BitsToAdd;
    LONG OldValue, NewValue;

    C_ASSERT((FM_LOCK_WAITER_WOKEN * 2) == FM_LOCK_WAITER_INC);

    /* Increase contention count */
    FastMutex->Contention++;

    BitsToRemove = FM_LOCK_BIT;
    BitsToAdd = FM_LOCK_WAITER_INC;

    for (;;)
    {
        OldValue = FastMutex->Count;
        for (;;)
        {
            if (OldValue & FM_LOCK_BIT)
            {
                ASSERT((BitsToRemove == FM_LOCK_BIT) ||
                       ((OldValue & FM_LOCK_WAITER_WOKEN) != 0));
                NewValue = OldValue ^ BitsToRemove;
                NewValue = InterlockedCompareExchange(&FastMutex->Count, NewValue, OldValue);
                if (NewValue == OldValue)
                {
                    return;
                }
            }
            else
            {
                NewValue = OldValue + BitsToAdd;
                NewValue = InterlockedCompareExchange(&FastMutex->Count, NewValue, OldValue);
                if (NewValue == OldValue)
                {
                    break;
                }
            }

            OldValue = NewValue;
        }

        KeWaitForSingleObject(&FastMutex->Event, WrMutex, KernelMode, FALSE, NULL);
        ASSERT((FastMutex->Count & FM_LOCK_WAITER_WOKEN) != 0);
        BitsToRemove = FM_LOCK_BIT | FM_LOCK_WAITER_WOKEN;
        BitsToAdd = FM_LOCK_WAITER_WOKEN;
    }
#else
    /* Increase contention count */
    FastMutex->Contention++;

    /* Wait for the event */
    KeWaitForSingleObject(&FastMutex->Event, WrMutex, KernelMode, FALSE, NULL);
#endif
}

VOID
FASTCALL
KiAcquireGuardedMutex(IN OUT PKGUARDED_MUTEX GuardedMutex)
{
    ULONG BitsToRemove, BitsToAdd;
    LONG OldValue, NewValue;

    /* We depend on these bits being just right */
    C_ASSERT((GM_LOCK_WAITER_WOKEN * 2) == GM_LOCK_WAITER_INC);

    /* Increase the contention count */
    GuardedMutex->Contention++;

    /* Start by unlocking the Guarded Mutex */
    BitsToRemove = GM_LOCK_BIT;
    BitsToAdd = GM_LOCK_WAITER_INC;

    /* Start change loop */
    for (;;)
    {
        /* Loop sanity checks */
        ASSERT((BitsToRemove == GM_LOCK_BIT) ||
               (BitsToRemove == (GM_LOCK_BIT | GM_LOCK_WAITER_WOKEN)));
        ASSERT((BitsToAdd == GM_LOCK_WAITER_INC) ||
               (BitsToAdd == GM_LOCK_WAITER_WOKEN));

        /* Get the Count Bits */
        OldValue = GuardedMutex->Count;

        /* Start internal bit change loop */
        for (;;)
        {
            /* Check if the Guarded Mutex is locked */
            if (OldValue & GM_LOCK_BIT)
            {
                /* Sanity check */
                ASSERT((BitsToRemove == GM_LOCK_BIT) ||
                       ((OldValue & GM_LOCK_WAITER_WOKEN) != 0));

                /* Unlock it by removing the Lock Bit */
                NewValue = OldValue ^ BitsToRemove;
                NewValue = InterlockedCompareExchange(&GuardedMutex->Count,
                                                      NewValue,
                                                      OldValue);
                if (NewValue == OldValue) return;
            }
            else
            {
                /* The Guarded Mutex isn't locked, so simply set the bits */
                NewValue = OldValue + BitsToAdd;
                NewValue = InterlockedCompareExchange(&GuardedMutex->Count,
                                                      NewValue,
                                                      OldValue);
                if (NewValue == OldValue) break;
            }

            /* Old value changed, loop again */
            OldValue = NewValue;
        }

        /* Now we have to wait for it */
        KeWaitForGate(&GuardedMutex->Gate, WrGuardedMutex, KernelMode);
        ASSERT((GuardedMutex->Count & GM_LOCK_WAITER_WOKEN) != 0);

        /* Ok, the wait is done, so set the new bits */
        BitsToRemove = GM_LOCK_BIT | GM_LOCK_WAITER_WOKEN;
        BitsToAdd = GM_LOCK_WAITER_WOKEN;
    }
}

//
// This routine exits the dispatcher after a compatible operation and
// swaps the context to the next scheduled thread on the current CPU if
// one is available.
//
// It does NOT attempt to scan for a new thread to schedule.
//
VOID
FASTCALL
KiExitDispatcher(IN KIRQL OldIrql)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD Thread, NextThread;
    BOOLEAN PendingApc;

    /* Make sure we're at synchronization level */
    ASSERT(KeGetCurrentIrql() == SYNCH_LEVEL);

    /* Check if we have deferred threads */
    KiCheckDeferredReadyList(Prcb);

    /* Check if we were called at dispatcher level or higher */
    if (OldIrql >= DISPATCH_LEVEL)
    {
        /* Check if we have a thread to schedule, and that no DPC is active */
        if ((Prcb->NextThread) && !(Prcb->DpcRoutineActive))
        {
            /* Request DPC interrupt */
            HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }

        /* Lower IRQL and exit */
        goto Quickie;
    }

    /* Make sure there's a new thread scheduled */
    if (!Prcb->NextThread) goto Quickie;

    /* Lock the PRCB */
    KiAcquirePrcbLock(Prcb);

    /* Drop a stale self-placement and keep the current thread running. */
    Thread = Prcb->CurrentThread;
    if ((Prcb->NextThread == NULL) ||
        KiConsumeSelfNextThread(Prcb, Thread))
    {
        KiReleasePrcbLock(Prcb);
        goto Quickie;
    }

    /* Get the next thread now */
    NextThread = Prcb->NextThread;

#if defined(_M_ARM64)
    if ((Thread == Prcb->IdleThread) && (Thread->State == Initialized))
    {
        Thread->State = Running;
    }
#endif

    /* Set current thread's swap busy to true */
    KiSetThreadSwapBusy(Thread);

#if defined(_M_ARM64)
    _disable();
#endif

    /* Switch threads in PRCB */
    Prcb->NextThread = NULL;
    Prcb->CurrentThread = NextThread;

    /* Set thread to running */
    NextThread->State = Running;

    /* Queue it on the ready lists */
    KxQueueReadyThread(Thread, Prcb);

    /* Set wait IRQL */
    Thread->WaitIrql = OldIrql;

    /* Swap threads and check if APCs were pending */
    PendingApc = KiSwapContext(OldIrql, Thread);
    if (PendingApc)
    {
        /* Lower only to APC */
        KeLowerIrql(APC_LEVEL);

        /* Deliver APCs */
        KiDeliverApc(KernelMode, NULL, NULL);
        ASSERT(OldIrql == PASSIVE_LEVEL);
    }

    /* Lower IRQl back */
Quickie:
    KeLowerIrql(OldIrql);
}

/* PUBLIC FUNCTIONS **********************************************************/

BOOLEAN
NTAPI
KeIsWaitListEmpty(IN PVOID Object)
{
    UNIMPLEMENTED;
    return FALSE;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeDelayExecutionThread(IN KPROCESSOR_MODE WaitMode,
                       IN BOOLEAN Alertable,
                       IN PLARGE_INTEGER Interval OPTIONAL)
{
    PKTIMER Timer;
    PKWAIT_BLOCK TimerBlock;
    PKTHREAD Thread = KeGetCurrentThread();
    NTSTATUS WaitStatus;
    BOOLEAN Swappable;
    PLARGE_INTEGER OriginalDueTime;
    LARGE_INTEGER DueTime, NewDueTime, InterruptTime;
    ULONG Hand = 0;

    if (Thread->WaitNext)
        ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    else
        ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    /* If this is a user-mode wait of 0 seconds, yield execution */
    if (!(Interval->QuadPart) && (WaitMode != KernelMode))
    {
        /* Make sure the wait isn't alertable or interrupting an APC */
        if (!(Alertable) && !(Thread->ApcState.UserApcPending))
        {
            /* Yield execution */
            return NtYieldExecution();
        }
    }

    /* Setup the original time and timer/wait blocks */
    OriginalDueTime = Interval;
    Timer = &Thread->Timer;
    TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];

    /* Check if the lock is already held */
    if (!Thread->WaitNext) goto WaitStart;

    /*  Otherwise, we already have the lock, so initialize the wait */
    Thread->WaitNext = FALSE;
    KxDelayThreadWait();
    KiAcquireDispatcherObject(&Timer->Header);

    /* Start wait loop */
    for (;;)
    {
        /* Disable pre-emption */
        Thread->Preempted = FALSE;

        /* Check if a kernel APC is pending and we're below APC_LEVEL */
        if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
        {
            /* Unlock the dispatcher */
            KiReleaseDispatcherObject(&Timer->Header);
            KiExitDispatcher(Thread->WaitIrql);
        }
        else
        {
            /* Check if we have to bail out due to an alerted state */
            WaitStatus = KiCheckAlertability(Thread, Alertable, WaitMode);
            if (WaitStatus != STATUS_WAIT_0) break;

            /* Check if the timer expired */
            InterruptTime.QuadPart = KeQueryInterruptTime();
            if ((ULONGLONG)InterruptTime.QuadPart >= Timer->DueTime.QuadPart)
            {
                /* It did, so we don't need to wait */
                goto NoWait;
            }

            /* It didn't, so prepare it */
            Timer->TimerListEntry.Flink = NULL;
            Timer->TimerListEntry.Blink = NULL;

            /* Handle Kernel Queues */
            if (Thread->Queue)
            {
                /* Wake a waiter on the thread's own queue UNDER THE QUEUE LOCK.
                 * The caller holds a different object's lock, and
                 * KiActivateWaiterQueue mutates the queue's wait list, so it must
                 * hold Queue->Header (else it races KeInsertQueue and corrupts
                 * the queue wait list). A thread never KeWaitForSingleObject's on
                 * its own KQUEUE, so this is never the already-held object. */
                KiAcquireDispatcherObject(&Thread->Queue->Header);
                KiActivateWaiterQueue(Thread->Queue);
                KiReleaseDispatcherObject(&Thread->Queue->Header);
            }

            /* Serialize APC insertion with the final wait publication. */
            if (!KxTryBeginThreadWait(Thread))
            {
                KiReleaseDispatcherObject(&Timer->Header);
                KiUndoActivateWaiterQueue(Thread);
                KiExitDispatcher(Thread->WaitIrql);
                goto WaitStart;
            }

            /* Setup the wait information */
            Timer->Header.Inserted = TRUE;
            TimerBlock->BlockState = WaitBlockActive;

            /* Publish the wait and release the thread lock */
            KxCommitThreadWait(Thread, Swappable);

            /* Insert the timer and swap the thread */
            KiReleaseDispatcherObject(&Timer->Header);
            KxInsertTimerNoRelease(Timer, Hand);
            WaitStatus = (NTSTATUS)KiSwapThread(Thread, KeGetCurrentPrcb(), TRUE);

            /* Check if were swapped ok */
            if (WaitStatus != STATUS_KERNEL_APC)
            {
                /* This is a good thing */
                if (WaitStatus == STATUS_TIMEOUT) WaitStatus = STATUS_SUCCESS;

                /* Return Status */
                return WaitStatus;
            }

            /* Recalculate due times */
            Interval = KiRecalculateDueTime(OriginalDueTime,
                                            &DueTime,
                                            &NewDueTime);
        }

WaitStart:
        /* Setup a new wait */
        Thread->WaitIrql = KeRaiseIrqlToSynchLevel();
        KxDelayThreadWait();
        KiAcquireDispatcherObject(&Timer->Header);
    }

    /* We're done! */
    KiReleaseDispatcherObject(&Timer->Header);
    KiExitDispatcher(Thread->WaitIrql);
    return WaitStatus;

NoWait:
    /* There was nothing to wait for. Did we have a wait interval? */
    if (!Interval->QuadPart)
    {
        /* Unlock the dispatcher and do a yield */
        KiReleaseDispatcherObject(&Timer->Header);
        KiExitDispatcher(Thread->WaitIrql);
        return NtYieldExecution();
    }

    /* Unlock the dispatcher and adjust the quantum for a no-wait */
    KiReleaseDispatcherObject(&Timer->Header);
    KiAdjustQuantumThread(Thread);
    return STATUS_SUCCESS;
}

/*
 * @implemented
 *
 * Blocks the current thread until it is alerted by NtAlertThreadByThreadId
 * (targeting this thread's id) or until the optional timeout expires. This is
 * the kernel half of the Win8+ thread-alert-by-id primitive that backs
 * ntdll's RtlWaitOnAddress / WaitOnAddress.
 *
 * The wait is committed on the thread's own timer with no dispatcher object:
 * the only things that end it are the paired alert (delivered by
 * KeAlertThreadByThreadId via KiUnwaitThread with STATUS_ALERTED), the timer
 * (STATUS_TIMEOUT) or a kernel APC (which is delivered and the wait re-armed).
 * It is deliberately NOT an alertable wait, so classic user-mode alerts and
 * user APCs do not disturb it - matching Windows, where this wait uses the
 * distinct KTHREAD.AlertedByThreadId flag and the WrAlertByThreadId reason
 * rather than the classic Alerted[] array.
 *
 * The pending-alert flag (Thread->AlertedByThreadId) is read-and-cleared under
 * the thread lock at the exact point the thread transitions to Waiting, so it
 * serializes against KeAlertThreadByThreadId (which takes the same thread lock
 * to either set the flag or unwait a committed waiter). There is therefore no
 * lost-wakeup window between the pre-block flag check and the block itself.
 */
NTSTATUS
NTAPI
KeWaitForAlertByThreadId(IN PVOID Address,
                         IN PLARGE_INTEGER Timeout OPTIONAL)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKTIMER Timer = &Thread->Timer;
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    NTSTATUS WaitStatus;
    BOOLEAN AlertedByThreadId;
    BOOLEAN Swappable;
    PLARGE_INTEGER OriginalDueTime = Timeout;
    LARGE_INTEGER DueTime = {{0}}, NewDueTime, InterruptTime;
    ULONG Hand = 0;

    /* The Address argument is a user-mode cookie that Windows records only for
     * ETW tracing; it does NOT participate in the wake decision (a waiter is
     * paired with an alerter purely by thread id), so we simply ignore it. */
    UNREFERENCED_PARAMETER(Address);

    /* We are always entered fresh, without a wait lock already held */
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    ASSERT(!Thread->WaitNext);

WaitStart:
    /* Raise to synch level to begin the wait */
    Thread->WaitIrql = KeRaiseIrqlToSynchLevel();

    /* Set up the wait: no object, reason WrAlertByThreadId, non-alertable */
    Thread->WaitBlockList = TimerBlock;
    Thread->WaitStatus = STATUS_SUCCESS;
    Thread->Alertable = FALSE;
    Thread->WaitReason = WrAlertByThreadId;
    Thread->WaitMode = KernelMode;
    KxChainTimerOnly();
    Thread->WaitListEntry.Flink = NULL;
    Swappable = KiCheckThreadStackSwap(Thread, KernelMode);
    Thread->WaitTime = KeTickCount.LowPart;

    /* Arm the timeout timer, if one was supplied */
    if (Timeout)
    {
        KxSetTimerForThreadWait(Timer, *Timeout, &Hand);
        DueTime.QuadPart = Timer->DueTime.QuadPart;

        /* Point the (single) timer wait block at the timer */
        Timer->Header.WaitListHead.Flink = &TimerBlock->WaitListEntry;
        Timer->Header.WaitListHead.Blink = &TimerBlock->WaitListEntry;
        TimerBlock->WaitListEntry.Flink = &Timer->Header.WaitListHead;
        TimerBlock->WaitListEntry.Blink = &Timer->Header.WaitListHead;
    }

    /* Take the thread timer's dispatcher lock as the wait's commit anchor */
    KiAcquireDispatcherObject(&Timer->Header);

    /* Start the wait loop */
    for (;;)
    {
        /* Disable pre-emption */
        Thread->Preempted = FALSE;

        /* Deliver a pending, deliverable kernel APC before blocking */
        if ((Thread->ApcState.KernelApcPending) && !(Thread->SpecialApcDisable) &&
            (Thread->WaitIrql < APC_LEVEL))
        {
            /* Unlock the dispatcher and let the APC run */
            KiReleaseDispatcherObject(&Timer->Header);
            KiExitDispatcher(Thread->WaitIrql);
        }
        else
        {
            /* A pending thread-id alert has priority over an already-expired
             * timeout. Take the thread lock to make this check a linearization
             * point against KeAlertThreadByThreadId. KxTryBeginThreadWait also
             * closes the late kernel-APC window before inspecting the flag. */
            if (!KxTryBeginThreadWait(Thread))
            {
                KiReleaseDispatcherObject(&Timer->Header);
                KiExitDispatcher(Thread->WaitIrql);
                goto WaitStart;
            }
            AlertedByThreadId = InterlockedBitTestAndReset(&Thread->ThreadFlags, KTHREAD_ALERTED_BY_THREAD_ID_BIT);
            KiReleaseThreadLock(Thread);
            if (AlertedByThreadId)
            {
                WaitStatus = STATUS_ALERTED;
                goto NoWait;
            }

            /* If a timeout was given, check whether it already expired */
            if (Timeout)
            {
                InterruptTime.QuadPart = KeQueryInterruptTime();
                if ((ULONGLONG)InterruptTime.QuadPart >= Timer->DueTime.QuadPart)
                {
                    /* Timed out with nothing to wait for */
                    WaitStatus = STATUS_TIMEOUT;
                    goto NoWait;
                }

                /* It didn't, so activate the timer */
                Timer->TimerListEntry.Flink = NULL;
                Timer->TimerListEntry.Blink = NULL;
            }

            /* Handle a waiter on this thread's own queue, under the queue lock */
            if (Thread->Queue)
            {
                KiAcquireDispatcherObject(&Thread->Queue->Header);
                KiActivateWaiterQueue(Thread->Queue);
                KiReleaseDispatcherObject(&Thread->Queue->Header);
            }

            /* Commit the wait under the thread lock. Recheck both late kernel
             * APC insertion and AlertedByThreadId after queue activation, at
             * the exact point where the thread transitions to Waiting. */
            if (!KxTryBeginThreadWait(Thread))
            {
                KiReleaseDispatcherObject(&Timer->Header);
                KiUndoActivateWaiterQueue(Thread);
                KiExitDispatcher(Thread->WaitIrql);
                goto WaitStart;
            }
            AlertedByThreadId = InterlockedBitTestAndReset(&Thread->ThreadFlags, KTHREAD_ALERTED_BY_THREAD_ID_BIT);
            if (AlertedByThreadId)
            {
                /* An alert is already pending: consume it and don't block */
                KiReleaseThreadLock(Thread);
                KiUndoActivateWaiterQueue(Thread);
                WaitStatus = STATUS_ALERTED;
                goto NoWait;
            }

            if (Timeout)
            {
                Timer->Header.Inserted = TRUE;
                TimerBlock->BlockState = WaitBlockActive;
            }

            /* Publish the wait and release the thread lock */
            KxCommitThreadWait(Thread, Swappable);

            /* Release the timer lock and insert the timer if we armed one */
            KiReleaseDispatcherObject(&Timer->Header);
            if (Timeout) KxInsertTimerNoRelease(Timer, Hand);

            /* Swap the thread out */
            WaitStatus = (NTSTATUS)KiSwapThread(Thread, KeGetCurrentPrcb(), TRUE);

            /* Check if this wasn't a kernel APC interruption */
            if (WaitStatus != STATUS_KERNEL_APC)
            {
                /* STATUS_ALERTED (paired alert) or STATUS_TIMEOUT (timer) */
                return WaitStatus;
            }

            /* A kernel APC ran; recompute the remaining timeout and retry */
            if (Timeout)
            {
                Timeout = KiRecalculateDueTime(OriginalDueTime,
                                               &DueTime,
                                               &NewDueTime);
                if (!Timeout)
                {
                    /* The timeout fully elapsed while the APC was running */
                    return STATUS_TIMEOUT;
                }
            }
        }

        /* Set up a new wait */
        goto WaitStart;
    }

NoWait:
    /* We aren't going to block: release the timer lock, exit the dispatcher via
     * the quantum adjustment, and return the completed wait status. */
    KiReleaseDispatcherObject(&Timer->Header);
    KiAdjustQuantumThread(Thread);
    return WaitStatus;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeWaitForSingleObject(IN PVOID Object,
                      IN KWAIT_REASON WaitReason,
                      IN KPROCESSOR_MODE WaitMode,
                      IN BOOLEAN Alertable,
                      IN PLARGE_INTEGER Timeout OPTIONAL)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PKMUTANT CurrentObject = (PKMUTANT)Object;
    PKWAIT_BLOCK WaitBlock = &Thread->WaitBlock[0];
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    PKTIMER Timer = &Thread->Timer;
    NTSTATUS WaitStatus;
    BOOLEAN Swappable;
    LARGE_INTEGER DueTime = {{0}}, NewDueTime, InterruptTime;
    PLARGE_INTEGER OriginalDueTime = Timeout;
    ULONG Hand = 0;


    if (Thread->WaitNext)
        ASSERT(KeGetCurrentIrql() == SYNCH_LEVEL);
    else
        ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL ||
               (KeGetCurrentIrql() == DISPATCH_LEVEL &&
                Timeout && Timeout->QuadPart == 0));

    /* Check if the lock is already held */
    if (!Thread->WaitNext) goto WaitStart;

    /*  Otherwise, we already have the lock, so initialize the wait */
    Thread->WaitNext = FALSE;
    KxSingleThreadWait();
    KiAcquireDispatcherObject(&CurrentObject->Header);

    /* Start wait loop */
    for (;;)
    {
        /* Disable pre-emption */
        Thread->Preempted = FALSE;

        /* Check if a kernel APC is pending and we're below APC_LEVEL */
        if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
        {
            /* Unlock the dispatcher */
            KiReleaseDispatcherObject(&CurrentObject->Header);
            KiExitDispatcher(Thread->WaitIrql);
        }
        else
        {
            /* Check if it's a mutant */
            if ((CurrentObject->Header.Type & KOBJECT_TYPE_MASK) == MutantObject)
            {
                /* Check its signal state or if we own it */
                if ((CurrentObject->Header.SignalState > 0) ||
                    (Thread == CurrentObject->OwnerThread))
                {
                    /* Just unwait this guy and exit */
                    if (CurrentObject->Header.SignalState != MINLONG)
                    {
                        /* It has a normal signal state. Unwait and return */
                        KiSatisfyMutantWait(CurrentObject, Thread);
                        WaitStatus = (NTSTATUS)Thread->WaitStatus;
                        goto DontWait;
                    }
                    else
                    {
                        /* Raise an exception */
                        KiReleaseDispatcherObject(&CurrentObject->Header);
                        KiExitDispatcher(Thread->WaitIrql);
                        ExRaiseStatus(STATUS_MUTANT_LIMIT_EXCEEDED);
                   }
                }
            }
            else if (CurrentObject->Header.SignalState > 0)
            {
                /* Another satisfied object */
                KiSatisfyNonMutantWait(CurrentObject);
                WaitStatus = STATUS_WAIT_0;
                goto DontWait;
            }

            /* Make sure we can satisfy the Alertable request */
            WaitStatus = KiCheckAlertability(Thread, Alertable, WaitMode);
            if (WaitStatus != STATUS_WAIT_0) break;

            /* Enable the Timeout Timer if there was any specified */
            if (Timeout)
            {
                /* Check if the timer expired */
                InterruptTime.QuadPart = KeQueryInterruptTime();
                if ((ULONGLONG)InterruptTime.QuadPart >=
                    Timer->DueTime.QuadPart)
                {
                    /* It did, so we don't need to wait */
                    WaitStatus = STATUS_TIMEOUT;
                    goto DontWait;
                }

                /* It didn't, so prepare it */
                Timer->TimerListEntry.Flink = NULL;
                Timer->TimerListEntry.Blink = NULL;
            }

            /* Handle Kernel Queues */
            if (Thread->Queue)
            {
                /* A completion-port thread may wait on its own queue handle. */
                if (Thread->Queue == (PKQUEUE)CurrentObject)
                {
                    KiActivateWaiterQueue(Thread->Queue);
                }
                else
                {
                    KiAcquireDispatcherObject(&Thread->Queue->Header);
                    KiActivateWaiterQueue(Thread->Queue);
                    KiReleaseDispatcherObject(&Thread->Queue->Header);
                }
            }

            /* Serialize APC insertion with the final wait publication. */
            if (!KxTryBeginThreadWait(Thread))
            {
                KiReleaseDispatcherObject(&CurrentObject->Header);
                KiUndoActivateWaiterQueue(Thread);
                KiExitDispatcher(Thread->WaitIrql);
                goto WaitStart;
            }

            /* Arm the timer and link the Object to this Wait Block */
            WaitBlock->BlockState = WaitBlockActive;
            if (Timeout)
            {
                Timer->Header.Inserted = TRUE;
                TimerBlock->BlockState = WaitBlockActive;
            }
            InsertTailList(&CurrentObject->Header.WaitListHead,
                           &WaitBlock->WaitListEntry);

            /* Publish the wait and release the thread lock */
            KxCommitThreadWait(Thread, Swappable);

            /* Release the object lock */
            KiReleaseDispatcherObject(&CurrentObject->Header);

            /* Check if we have a timer */
            if (Timeout)
            {
                /* Insert it */
                KxInsertTimerNoRelease(Timer, Hand);
            }

            /* Do the actual swap */
            WaitStatus = (NTSTATUS)KiSwapThread(Thread, KeGetCurrentPrcb(), TRUE);

            /* Check if we were executing an APC */
            if (WaitStatus != STATUS_KERNEL_APC)
            {
                if (WaitBlock->WaitListEntry.Flink)
                {
                    KIRQL CleanupIrql = KeRaiseIrqlToSynchLevel();
                    KiAcquireDispatcherObject(&CurrentObject->Header);
                    if (WaitBlock->WaitListEntry.Flink)
                    {
                        RemoveEntryList(&WaitBlock->WaitListEntry);
                        WaitBlock->WaitListEntry.Flink = NULL;
                    }
                    KiReleaseDispatcherObject(&CurrentObject->Header);
                    KeLowerIrql(CleanupIrql);
                }
                return WaitStatus;
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
        /* Setup a new wait */
        Thread->WaitIrql = KeRaiseIrqlToSynchLevel();
        KxSingleThreadWait();
        KiAcquireDispatcherObject(&CurrentObject->Header);
    }

    /* Wait complete */
    KiReleaseDispatcherObject(&CurrentObject->Header);
    KiExitDispatcher(Thread->WaitIrql);
    return WaitStatus;

DontWait:
    /* Release the object lock but maintain high IRQL */
    KiReleaseDispatcherObject(&CurrentObject->Header);

    /* Adjust the Quantum and return the wait status */
    KiAdjustQuantumThread(Thread);
    return WaitStatus;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeWaitForMultipleObjects(IN ULONG Count,
                         IN PVOID Object[],
                         IN WAIT_TYPE WaitType,
                         IN KWAIT_REASON WaitReason,
                         IN KPROCESSOR_MODE WaitMode,
                         IN BOOLEAN Alertable,
                         IN PLARGE_INTEGER Timeout OPTIONAL,
                         OUT PKWAIT_BLOCK WaitBlockArray OPTIONAL)
{
    PKMUTANT CurrentObject;
    PKWAIT_BLOCK WaitBlock;
    PKTHREAD Thread = KeGetCurrentThread();
    PKWAIT_BLOCK TimerBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    PKTIMER Timer = &Thread->Timer;
    NTSTATUS WaitStatus = STATUS_SUCCESS;
    BOOLEAN Swappable;
    PLARGE_INTEGER OriginalDueTime = Timeout;
    LARGE_INTEGER DueTime = {{0}}, NewDueTime, InterruptTime;
    ULONG Index, Hand = 0;


    if (Thread->WaitNext)
        ASSERT(KeGetCurrentIrql() == SYNCH_LEVEL);
    else if (!Timeout || (Timeout->QuadPart != 0))
    {
        ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
    }
    else
        ASSERT(KeGetCurrentIrql() <= DISPATCH_LEVEL);

    /* Make sure the Wait Count is valid */
    if (!WaitBlockArray)
    {
        /* Check in regards to the Thread Object Limit */
        if (Count > THREAD_WAIT_OBJECTS)
        {
            /* Bugcheck */
            KeBugCheck(MAXIMUM_WAIT_OBJECTS_EXCEEDED);
        }

        /* Use the Thread's Wait Block */
        WaitBlockArray = &Thread->WaitBlock[0];
    }
    else
    {
        /* Using our own Block Array, so check with the System Object Limit */
        if (Count > MAXIMUM_WAIT_OBJECTS)
        {
            /* Bugcheck */
            KeBugCheck(MAXIMUM_WAIT_OBJECTS_EXCEEDED);
        }
    }

    /* Sanity check */
    ASSERT(Count != 0);

    /* Check if the lock is already held */
    if (!Thread->WaitNext) goto WaitStart;

    /*  Otherwise, we already have the lock, so initialize the wait */
    Thread->WaitNext = FALSE;
    /*  Note that KxMultiThreadWait is a macro, defined in ke_x.h, that  */
    /*  uses  (and modifies some of) the following local                 */
    /*  variables:                                                       */
    /*  Thread, Index, WaitBlock, Timer, Timeout, Hand and Swappable.    */
    /*  If it looks like this code doesn't actually wait for any objects */
    /*  at all, it's because the setup is done by that macro.            */
    KxMultiThreadWait();
    KiAcquireObjectLocks(Object, Count);

    /* Start wait loop */
    for (;;)
    {
        /* Disable pre-emption */
        Thread->Preempted = FALSE;

        /* Check if a kernel APC is pending and we're below APC_LEVEL */
        if (KiIsKernelApcDeliverable(Thread, Thread->WaitIrql))
        {
            /* Unlock the dispatcher */
            KiReleaseObjectLocks(Object, Count);
            KiExitDispatcher(Thread->WaitIrql);
        }
        else
        {
            /* Check what kind of wait this is */
            Index = 0;
            if (WaitType == WaitAny)
            {
                /* Loop blocks */
                do
                {
                    /* Get the Current Object */
                    CurrentObject = (PKMUTANT)Object[Index];

                    /* Check if the Object is a mutant */
                    if ((CurrentObject->Header.Type & KOBJECT_TYPE_MASK) == MutantObject)
                    {
                        /* Check if it's signaled */
                        if ((CurrentObject->Header.SignalState > 0) ||
                            (Thread == CurrentObject->OwnerThread))
                        {
                            /* This is a Wait Any, so unwait this and exit */
                            if (CurrentObject->Header.SignalState !=
                                (LONG)MINLONG)
                            {
                                /* Normal signal state, unwait it and return */
                                KiSatisfyMutantWait(CurrentObject, Thread);
                                WaitStatus = (NTSTATUS)Thread->WaitStatus | Index;
                                goto DontWait;
                            }
                            else
                            {
                                /* Raise an exception (see wasm.ru) */
                                KiReleaseObjectLocks(Object, Count);
                                KiExitDispatcher(Thread->WaitIrql);
                                ExRaiseStatus(STATUS_MUTANT_LIMIT_EXCEEDED);
                            }
                        }
                    }
                    else if (CurrentObject->Header.SignalState > 0)
                    {
                        /* Another signaled object, unwait and return */
                        KiSatisfyNonMutantWait(CurrentObject);
                        WaitStatus = Index;
                        goto DontWait;
                    }

                    /* Go to the next block */
                    Index++;
                } while (Index < Count);
            }
            else
            {
                /* Loop blocks */
                do
                {
                    /* Get the Current Object */
                    CurrentObject = (PKMUTANT)Object[Index];

                    /* Check if we're dealing with a mutant again */
                    if ((CurrentObject->Header.Type & KOBJECT_TYPE_MASK) == MutantObject)
                    {
                        /* Check if it has an invalid count */
                        if ((Thread == CurrentObject->OwnerThread) &&
                            (CurrentObject->Header.SignalState == (LONG)MINLONG))
                        {
                            /* Raise an exception */
                            KiReleaseObjectLocks(Object, Count);
                            KiExitDispatcher(Thread->WaitIrql);
                            ExRaiseStatus(STATUS_MUTANT_LIMIT_EXCEEDED);
                        }
                        else if ((CurrentObject->Header.SignalState <= 0) &&
                                 (Thread != CurrentObject->OwnerThread))
                        {
                            /* We don't own it, can't satisfy the wait */
                            break;
                        }
                    }
                    else if (CurrentObject->Header.SignalState <= 0)
                    {
                        /* Not signaled, can't satisfy */
                        break;
                    }

                    /* Go to the next block */
                    Index++;
                } while (Index < Count);

                /* Check if we've went through all the objects */
                if (Index == Count)
                {
                    /* Loop wait blocks */
#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
                    for (Index = 0; Index < Count; Index++)
                    {
                        CurrentObject = (PKMUTANT)WaitBlockArray[Index].Object;
                        KiSatisfyObjectWait(CurrentObject, Thread);
                    }
#else
                    WaitBlock = WaitBlockArray;
                    do
                    {
                        /* Get the object and satisfy it */
                        CurrentObject = (PKMUTANT)WaitBlock->Object;
                        KiSatisfyObjectWait(CurrentObject, Thread);

                        /* Go to the next block */
                        WaitBlock = WaitBlock->NextWaitBlock;
                    } while(WaitBlock != WaitBlockArray);
#endif

                    /* Set the wait status and get out */
                    WaitStatus = (NTSTATUS)Thread->WaitStatus;
                    goto DontWait;
                }
            }

            /* Make sure we can satisfy the Alertable request */
            WaitStatus = KiCheckAlertability(Thread, Alertable, WaitMode);
            if (WaitStatus != STATUS_WAIT_0) break;

            /* Enable the Timeout Timer if there was any specified */
            if (Timeout)
            {
                /* Check if the timer expired */
                InterruptTime.QuadPart = KeQueryInterruptTime();
                if ((ULONGLONG)InterruptTime.QuadPart >=
                    Timer->DueTime.QuadPart)
                {
                    /* It did, so we don't need to wait */
                    WaitStatus = STATUS_TIMEOUT;
                    goto DontWait;
                }

                /* It didn't, so prepare it */
                Timer->TimerListEntry.Flink = NULL;
                Timer->TimerListEntry.Blink = NULL;

                /* Link the wait blocks */
#if !((NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64))
                WaitBlock->NextWaitBlock = TimerBlock;
#endif
            }

            /* Handle Kernel Queues */
            if (Thread->Queue)
            {
                BOOLEAN QueueLockHeld = FALSE;

                for (Index = 0; Index < Count; Index++)
                {
                    if (Object[Index] == Thread->Queue)
                    {
                        QueueLockHeld = TRUE;
                        break;
                    }
                }

                if (QueueLockHeld)
                {
                    KiActivateWaiterQueue(Thread->Queue);
                }
                else
                {
                    KiAcquireDispatcherObject(&Thread->Queue->Header);
                    KiActivateWaiterQueue(Thread->Queue);
                    KiReleaseDispatcherObject(&Thread->Queue->Header);
                }
            }

            /* Timer expiration holds the timer object while acquiring the
             * waiter thread lock. Preserve that order during publication. */
            if (Timeout) KiAcquireDispatcherObject(&Timer->Header);

            /* Serialize APC insertion with the final wait publication. */
            if (!KxTryBeginThreadWait(Thread))
            {
                if (Timeout) KiReleaseDispatcherObject(&Timer->Header);
                KiReleaseObjectLocks(Object, Count);
                KiUndoActivateWaiterQueue(Thread);
                KiExitDispatcher(Thread->WaitIrql);
                goto WaitStart;
            }

            /* Arm the timer for both linkage schemes below; the deferred
             * KxInsertTimerNoRelease keys on Inserted */
            if (Timeout) Timer->Header.Inserted = TRUE;

            /* Insert into Object's Wait List*/
#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
            for (Index = 0; Index < Count; Index++)
            {
                CurrentObject = WaitBlockArray[Index].Object;
                WaitBlockArray[Index].BlockState = WaitBlockActive;
                InsertTailList(&CurrentObject->Header.WaitListHead,
                               &WaitBlockArray[Index].WaitListEntry);
            }
            if (Timeout)
            {
                TimerBlock->BlockState = WaitBlockActive;
                InsertTailList(&Timer->Header.WaitListHead,
                               &TimerBlock->WaitListEntry);
            }
#else
            WaitBlock = WaitBlockArray;
            do
            {
                /* Get the Current Object */
                CurrentObject = WaitBlock->Object;

                /* Link the Object to this Wait Block */
                InsertTailList(&CurrentObject->Header.WaitListHead,
                               &WaitBlock->WaitListEntry);

                /* Move to the next Wait Block */
                WaitBlock = WaitBlock->NextWaitBlock;
            } while (WaitBlock != WaitBlockArray);
#endif

            /* Publish the wait and release the thread lock */
            KxCommitThreadWait(Thread, Swappable);
            if (Timeout) KiReleaseDispatcherObject(&Timer->Header);

            /* Release the object locks */
            KiReleaseObjectLocks(Object, Count);

            /* Check if we have a timer */
            if (Timeout)
            {
                KxInsertTimerNoRelease(Timer, Hand);
            }

            /* Swap the thread */
            WaitStatus = (NTSTATUS)KiSwapThread(Thread, KeGetCurrentPrcb(), TRUE);

            /* Check if we were executing an APC */
            if (WaitStatus != STATUS_KERNEL_APC)
            {
                KIRQL CleanupIrql = KeRaiseIrqlToSynchLevel();
                KiAcquireObjectLocks(Object, Count);
                for (Index = 0; Index < Count; Index++)
                {
                    if (WaitBlockArray[Index].WaitListEntry.Flink)
                    {
                        RemoveEntryList(&WaitBlockArray[Index].WaitListEntry);
                        WaitBlockArray[Index].WaitListEntry.Flink = NULL;
                    }
                }
                KiReleaseObjectLocks(Object, Count);
                KeLowerIrql(CleanupIrql);
                return WaitStatus;
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
        /* Setup a new wait */
        Thread->WaitIrql = KeRaiseIrqlToSynchLevel();
        KxMultiThreadWait();
        KiAcquireObjectLocks(Object, Count);
    }

    /* We are done */
    KiReleaseObjectLocks(Object, Count);
    KiExitDispatcher(Thread->WaitIrql);
    return WaitStatus;

DontWait:
    /* Release the object locks but maintain high IRQL */
    KiReleaseObjectLocks(Object, Count);

    /* Adjust the Quantum and return the wait status */
    KiAdjustQuantumThread(Thread);
    return WaitStatus;
}

NTSTATUS
NTAPI
NtDelayExecution(IN BOOLEAN Alertable,
                 IN PLARGE_INTEGER DelayInterval)
{
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    LARGE_INTEGER SafeInterval;
    NTSTATUS Status;

    /* Check the previous mode */
    if (PreviousMode != KernelMode)
    {
        /* Enter SEH for probing */
        _SEH2_TRY
        {
            /* Probe and capture the time out */
            SafeInterval = ProbeForReadLargeInteger(DelayInterval);
            DelayInterval = &SafeInterval;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            /* Return the exception code */
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
   }

   /* Call the Kernel Function */
   Status = KeDelayExecutionThread(PreviousMode,
                                   Alertable,
                                   DelayInterval);

   /* Return Status */
   return Status;
}

/* EOF */
