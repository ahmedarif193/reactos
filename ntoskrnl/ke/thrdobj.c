/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/thrdobj.c
 * PURPOSE:         Implements routines to manage the Kernel Thread Object
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern EX_WORK_QUEUE ExWorkerQueue[MaximumWorkQueue];
extern LIST_ENTRY PspReaperListHead;

/* Internal exports not declared by the DDK. */
NTKERNELAPI KPRIORITY NTAPI KeQueryEffectivePriorityThread(_In_ PKTHREAD Thread);
NTKERNELAPI KPRIORITY NTAPI KeSetActualBasePriorityThread(_Inout_ PKTHREAD Thread, _In_ KPRIORITY Priority);

/* FUNCTIONS *****************************************************************/

UCHAR
NTAPI
KeFindNextRightSetAffinity(IN UCHAR Number,
                           IN KAFFINITY Set)
{
    KAFFINITY Bit;
    ULONG Result;
    ASSERT(Set != 0);

    /* Calculate the mask */
    Bit = (AFFINITY_MASK(Number) - 1) & Set;

    /* If it's 0, use the one we got */
    if (!Bit) Bit = Set;

    /* Now find the right set and return it */
    BitScanReverseAffinity(&Result, Bit);
    return (UCHAR)Result;
}

BOOLEAN
NTAPI
KeReadStateThread(IN PKTHREAD Thread)
{
    ASSERT_THREAD(Thread);

    /* Return signal state */
    return (BOOLEAN)Thread->Header.SignalState;
}

KPRIORITY
NTAPI
KeQueryBasePriorityThread(IN PKTHREAD Thread)
{
    LONG BaseIncrement;
    KIRQL OldIrql;
    PKPROCESS Process;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Raise IRQL to synch level */
    OldIrql = KeRaiseIrqlToSynchLevel();

    /* Lock the thread */
    KiAcquireThreadLock(Thread);

    /* Get the Process */
    Process = Thread->ApcStatePointer[0]->Process;

    /* Calculate the base increment */
    BaseIncrement = Thread->BasePriority - Process->BasePriority;

    /* If saturation occured, return the saturation increment instead */
    if (Thread->Saturation) BaseIncrement = (HIGH_PRIORITY + 1) / 2 *
                                            Thread->Saturation;

    /* Release thread lock */
    KiReleaseThreadLock(Thread);

    /* Lower IRQl and return Increment */
    KeLowerIrql(OldIrql);
    return BaseIncrement;
}

BOOLEAN
NTAPI
KeSetDisableBoostThread(IN OUT PKTHREAD Thread,
                        IN BOOLEAN Disable)
{
    ASSERT_THREAD(Thread);

    /* Check if we're enabling or disabling */
    if (Disable)
    {
        /* Set the bit */
        return InterlockedBitTestAndSet(&Thread->ThreadFlags, 1);
    }
    else
    {
        /* Remove the bit */
        return InterlockedBitTestAndReset(&Thread->ThreadFlags, 1);
    }
}

VOID
NTAPI
KeReadyThread(IN PKTHREAD Thread)
{
    KIRQL OldIrql;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the Dispatcher Database */
    OldIrql = KeRaiseIrqlToSynchLevel();

    /* Make the thread ready */
    KiAcquireThreadLock(Thread);
    KiReadyThread(Thread);
    KiReleaseThreadLock(Thread);

    /* Unlock dispatcher database */
    KiExitDispatcher(OldIrql);
}

ULONG
NTAPI
KeAlertResumeThread(IN PKTHREAD Thread)
{
    ULONG PreviousCount;
    KLOCK_QUEUE_HANDLE ApcLock;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the APC Queue */
    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    /* Return if Thread is already alerted. */
    KiAcquireThreadLock(Thread);
    if (!Thread->Alerted[KernelMode])
    {
        /* If it's Blocked, unblock if it we should */
        if ((Thread->State == Waiting) && (Thread->Alertable))
        {
            /* Abort the wait */
            KiUnwaitThread(Thread, STATUS_ALERTED, THREAD_ALERT_INCREMENT);
        }
        else
        {
            /* If not, simply Alert it */
            Thread->Alerted[KernelMode] = TRUE;
        }
    }
    KiReleaseThreadLock(Thread);

    /* Save the old Suspend Count */
    PreviousCount = Thread->SuspendCount;

    /* If the thread is suspended, decrease one of the suspend counts */
    if (PreviousCount)
    {
        /* Decrease count. If we are now zero, unwait it completely */
        Thread->SuspendCount--;
        if (!(Thread->SuspendCount) && !(Thread->FreezeCount))
        {
            /* Signal and satisfy */
            KiAcquireDispatcherObject(&Thread->SuspendSemaphore.Header);
            Thread->SuspendSemaphore.Header.SignalState++;
            KiWaitTest(&Thread->SuspendSemaphore.Header, IO_NO_INCREMENT);
            KiReleaseDispatcherObject(&Thread->SuspendSemaphore.Header);
        }
    }

    /* Release Locks and return the Old State */
    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);
    return PreviousCount;
}

BOOLEAN
NTAPI
KeAlertThread(IN PKTHREAD Thread,
              IN KPROCESSOR_MODE AlertMode)
{
    BOOLEAN PreviousState;
    KLOCK_QUEUE_HANDLE ApcLock;
    UCHAR AlertModeIndex;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    ASSERT((AlertMode == KernelMode) || (AlertMode == UserMode));
    AlertModeIndex = (UCHAR)AlertMode;

    /* Lock the APC Queue and the thread */
    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);
    KiAcquireThreadLock(Thread);

    /* Save the Previous State */
    PreviousState = Thread->Alerted[AlertModeIndex];

    /* Check if it's already alerted */
    if (!PreviousState)
    {
        /* Check if the thread is alertable, and blocked in the given mode */
        if ((Thread->State == Waiting) &&
            (Thread->Alertable) &&
            (AlertMode <= Thread->WaitMode))
        {
            /* Abort the wait to alert the thread */
            KiUnwaitThread(Thread, STATUS_ALERTED, THREAD_ALERT_INCREMENT);
        }
        else
        {
            /* Otherwise, merely set the alerted state */
            Thread->Alerted[AlertModeIndex] = TRUE;
        }
    }

    /* Release the thread lock */
    KiReleaseThreadLock(Thread);
    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);

    /* Return the old state */
    return PreviousState;
}

BOOLEAN
NTAPI
KeAlertThreadByThreadId(IN PKTHREAD Thread)
{
    BOOLEAN PreviousState;
    KLOCK_QUEUE_HANDLE ApcLock;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /*
     * Deliver a Win8+ alert-by-thread-id to the target thread. This is the
     * counterpart of KeWaitForAlertByThreadId. Unlike KeAlertThread it uses the
     * dedicated KTHREAD.AlertedByThreadId flag and the WrAlertByThreadId wait
     * reason, so it neither disturbs nor is disturbed by the classic alert
     * (Alerted[]) mechanism.
     */

    /* Lock the APC queue and the thread */
    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);
    KiAcquireThreadLock(Thread);

    /* Read the current pending-alert state. Bit 4 of ThreadFlags is only ever
     * set here and cleared by KeWaitForAlertByThreadId, both under this thread
     * lock, so it is stable for the duration of this critical section. */
    PreviousState = (BOOLEAN)((Thread->ThreadFlags >>
                              KTHREAD_ALERTED_BY_THREAD_ID_BIT) & 1);

    /* Act only if an alert wasn't already pending */
    if (!PreviousState)
    {
        /* Check if the thread is committed in an alert-by-id wait. Both this
         * decision and the waiter's commit run under the thread lock, so we
         * either observe State == Waiting and wake it, or the waiter later
         * observes (and consumes) the pending flag we set below. No lost wakeup. */
        if ((Thread->State == Waiting) &&
            (Thread->WaitReason == WrAlertByThreadId))
        {
            /* It is blocked in the alert wait: wake it with STATUS_ALERTED
             * without leaving a pending alert behind. */
            KiUnwaitThread(Thread, STATUS_ALERTED, THREAD_ALERT_INCREMENT);
        }
        else
        {
            /* Not (yet) in an alert wait: remember the alert so its next
             * KeWaitForAlertByThreadId returns STATUS_ALERTED immediately. */
            InterlockedBitTestAndSet(&Thread->ThreadFlags,
                                     KTHREAD_ALERTED_BY_THREAD_ID_BIT);
        }
    }

    /* Release the locks */
    KiReleaseThreadLock(Thread);
    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);

    /* Return the old pending state */
    return PreviousState;
}

VOID
NTAPI
KeBoostPriorityThread(IN PKTHREAD Thread,
                      IN KPRIORITY Increment)
{
    KIRQL OldIrql;
    KPRIORITY Priority;
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the Dispatcher Database */
    OldIrql = KeRaiseIrqlToSynchLevel();

    /* Only threads in the dynamic range get boosts */
    if (Thread->Priority < LOW_REALTIME_PRIORITY)
    {
        /* Lock the thread */
        KiAcquireThreadLock(Thread);

        /* Check again, and make sure there's not already a boost */
        if ((Thread->Priority < LOW_REALTIME_PRIORITY) &&
            !(Thread->PriorityDecrement))
        {
            /* Compute the new priority and see if it's higher */
            Priority = Thread->BasePriority + Increment;
            if (Priority > Thread->Priority)
            {
                if (Priority >= LOW_REALTIME_PRIORITY)
                {
                    Priority = LOW_REALTIME_PRIORITY - 1;
                }

                /* Reset the quantum */
                KiSetThreadQuantum(Thread, Thread->QuantumReset);

                /* Set the new Priority */
                KiSetPriorityThread(Thread, Priority);
            }
        }

        /* Release thread lock */
        KiReleaseThreadLock(Thread);
    }

    /* Release the dispatcher lokc */
    KiExitDispatcher(OldIrql);
}

ULONG
NTAPI
KeForceResumeThread(IN PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    ULONG PreviousCount;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the APC Queue */
    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    /* Save the old Suspend Count */
    PreviousCount = Thread->SuspendCount + Thread->FreezeCount;

    /* If the thread is suspended, wake it up!!! */
    if (PreviousCount)
    {
        /* Unwait it completely */
        Thread->SuspendCount = 0;
        Thread->FreezeCount = 0;

        /* Lock the suspend semaphore */
        KiAcquireDispatcherObject(&Thread->SuspendSemaphore.Header);

        /* Signal and satisfy */
        Thread->SuspendSemaphore.Header.SignalState++;
        KiWaitTest(&Thread->SuspendSemaphore.Header, IO_NO_INCREMENT);

        /* Release the suspend semaphore */
        KiReleaseDispatcherObject(&Thread->SuspendSemaphore.Header);
    }

    /* Release Lock and return the Old State */
    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);
    return PreviousCount;
}

VOID
NTAPI
KeFreezeAllThreads(VOID)
{
    KLOCK_QUEUE_HANDLE LockHandle, ApcLock;
    PKTHREAD Current, CurrentThread = KeGetCurrentThread();
    PKPROCESS Process = CurrentThread->ApcState.Process;
    PLIST_ENTRY ListHead, NextEntry;
    LONG OldCount;
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the process */
    KiAcquireProcessLockRaiseToSynch(Process, &LockHandle);

    /* If someone is already trying to free us, try again */
    while (CurrentThread->FreezeCount)
    {
        /* Release and re-acquire the process lock so the APC will go through */
        KiReleaseProcessLock(&LockHandle);
        KiAcquireProcessLockRaiseToSynch(Process, &LockHandle);
    }

    /* Enter a critical region */
    KeEnterCriticalRegion();

    /* Loop the Process's Threads */
    ListHead = &Process->ThreadListHead;
    NextEntry = ListHead->Flink;
    do
    {
        /* Get the current thread */
        Current = CONTAINING_RECORD(NextEntry, KTHREAD, ThreadListEntry);

        /* Lock it */
        KiAcquireApcLockAtSynchLevel(Current, &ApcLock);

        /* Make sure it's not ours, and check if APCs are enabled */
        if ((Current != CurrentThread) && (Current->ApcQueueable))
        {
            /* Sanity check */
            OldCount = Current->SuspendCount;
            ASSERT(OldCount != MAXIMUM_SUSPEND_COUNT);

            /* Increase the freeze count */
            Current->FreezeCount++;

            /* Make sure it wasn't already suspended */
            if (!(OldCount) && !(Current->SuspendCount))
            {
                /* Did we already insert it? */
                if (!Current->SuspendApc.Inserted)
                {
                    /* Insert the APC */
                    Current->SuspendApc.Inserted = TRUE;
                    KiInsertQueueApc(&Current->SuspendApc, IO_NO_INCREMENT);
                }
                else
                {
                    /* Lock the suspend semaphore */
                    KiAcquireDispatcherObject(&Current->SuspendSemaphore.Header);

                    /* Unsignal the semaphore, the APC was already inserted */
                    Current->SuspendSemaphore.Header.SignalState--;

                    /* Release the suspend semaphore */
                    KiReleaseDispatcherObject(&Current->SuspendSemaphore.Header);
                }
            }
        }

        /* Release the APC lock */
        KiReleaseApcLockFromSynchLevel(&ApcLock);

        /* Move to the next thread */
        NextEntry = NextEntry->Flink;
    } while (NextEntry != ListHead);

    /* Release the process lock and exit the dispatcher */
    KiReleaseProcessLockFromSynchLevel(&LockHandle);
    KiExitDispatcher(LockHandle.OldIrql);
}

ULONG
NTAPI
KeResumeThread(IN PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    ULONG PreviousCount;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the APC Queue */
    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    /* Save the Old Count */
    PreviousCount = Thread->SuspendCount;

    /* Check if it existed */
    if (PreviousCount)
    {
        /* Decrease the suspend count */
        Thread->SuspendCount--;

        /* Check if the thrad is still suspended or not */
        if ((!Thread->SuspendCount) && (!Thread->FreezeCount))
        {
            /* Acquire the suspend semaphore lock */
            KiAcquireDispatcherObject(&Thread->SuspendSemaphore.Header);

            /* Signal the Suspend Semaphore */
            Thread->SuspendSemaphore.Header.SignalState++;
            KiWaitTest(&Thread->SuspendSemaphore.Header, IO_NO_INCREMENT);

            /* Release the suspend semaphore lock */
            KiReleaseDispatcherObject(&Thread->SuspendSemaphore.Header);
        }
    }

    /* Release APC Queue lock and return the Old State */
    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);
    return PreviousCount;
}

VOID
NTAPI
KeRundownThread(VOID)
{
    KIRQL OldIrql;
    PKTHREAD Thread = KeGetCurrentThread();
    PLIST_ENTRY NextEntry, ListHead;
    PKMUTANT Mutant;
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Optimized path if nothing is on the list at the moment */
    if (IsListEmpty(&Thread->MutantListHead)) return;

    /* Lock the Dispatcher Database */
    OldIrql = KeRaiseIrqlToSynchLevel();

    /* Get the List Pointers */
    ListHead = &Thread->MutantListHead;
    NextEntry = ListHead->Flink;
    while (NextEntry != ListHead)
    {
        /* Get the Mutant */
        Mutant = CONTAINING_RECORD(NextEntry, KMUTANT, MutantListEntry);
        ASSERT_MUTANT(Mutant);

        /* Make sure it's not terminating with APCs off */
        if (Mutant->ApcDisable)
        {
            /* Bugcheck the system */
            KeBugCheckEx(THREAD_TERMINATE_HELD_MUTEX,
                         (ULONG_PTR)Thread,
                         (ULONG_PTR)Mutant,
                         0,
                         0);
        }

        /* Now we can remove it */
        RemoveEntryList(&Mutant->MutantListEntry);

        /* Unconditionally abandon it under the mutant lock */
        KiAcquireDispatcherObject(&Mutant->Header);
        Mutant->Header.SignalState = 1;
        Mutant->Abandoned = TRUE;
        Mutant->OwnerThread = NULL;

        /* Check if the Wait List isn't empty */
        if (!IsListEmpty(&Mutant->Header.WaitListHead))
        {
            /* Wake the Mutant */
            KiWaitTest(&Mutant->Header, MUTANT_INCREMENT);
        }
        KiReleaseDispatcherObject(&Mutant->Header);

        /* Move on */
        NextEntry = Thread->MutantListHead.Flink;
    }

    /* Release the Lock */
    KiExitDispatcher(OldIrql);
}

VOID
NTAPI
KeStartThread(IN OUT PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE LockHandle;
#ifdef CONFIG_SMP
    PKNODE Node;
    PKPRCB NodePrcb;
    KAFFINITY Set, Mask;
#endif
    UCHAR IdealProcessor = 0;
    PKPROCESS Process = Thread->ApcState.Process;

    /* Setup static fields from parent */
    Thread->DisableBoost = Process->DisableBoost;
#if defined(_M_IX86)
    Thread->Iopl = Process->Iopl;
#endif
    Thread->QuantumReset = Process->QuantumReset;
    KiSetThreadQuantum(Thread, Thread->QuantumReset);
    Thread->SystemAffinityActive = FALSE;

    /* Lock the process */
    KiAcquireProcessLockRaiseToSynch(Process, &LockHandle);

    /* Setup volatile data */
    Thread->Priority = Process->BasePriority;
    Thread->BasePriority = Process->BasePriority;
    KiThreadAffinityMask(Thread) = Process->Affinity;
    KiThreadUserAffinityMask(Thread) = Process->Affinity;

#ifdef CONFIG_SMP
    /* Get the KNODE and its PRCB */
    Node = KeNodeBlock[Process->IdealNode];
    NodePrcb = KiProcessorBlock[Process->ThreadSeed];

    /* Calculate affinity mask */
#ifdef _M_ARM
    DbgBreakPoint();
    Set = 0;
#else
    Set = ~NodePrcb->MultiThreadProcessorSet;
#endif
    Mask = Node->ProcessorMask & Process->Affinity;
    Set &= Mask;
    if (Set) Mask = Set;

    /* Get the new thread seed */
    IdealProcessor = KeFindNextRightSetAffinity(Process->ThreadSeed, Mask);
    Process->ThreadSeed = IdealProcessor;

    /* Sanity check */
    ASSERT((KiThreadUserAffinityMask(Thread) & AFFINITY_MASK(IdealProcessor)));
#endif

    /* Set the Ideal Processor */
    Thread->IdealProcessor = IdealProcessor;
    Thread->UserIdealProcessor = IdealProcessor;

    /* Insert the thread into the process list under the process lock */
    InsertTailList(&Process->ThreadListHead, &Thread->ThreadListEntry);

    /* Increase the stack count */
    ASSERT(Process->StackCount != MAXULONG);
    Process->StackCount++;

    /* Release locks and return */
    KiReleaseProcessLock(&LockHandle);
}

VOID
NTAPI
KiSuspendRundown(IN PKAPC Apc)
{
    /* Does nothing */
    UNREFERENCED_PARAMETER(Apc);
}

VOID
NTAPI
KiSuspendNop(IN PKAPC Apc,
             IN PKNORMAL_ROUTINE *NormalRoutine,
             IN PVOID *NormalContext,
             IN PVOID *SystemArgument1,
             IN PVOID *SystemArgument2)
{
    /* Does nothing */
    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
}

VOID
NTAPI
KiSuspendThread(IN PVOID NormalContext,
                IN PVOID SystemArgument1,
                IN PVOID SystemArgument2)
{
    /* Non-alertable kernel-mode suspended wait */
    KeWaitForSingleObject(&KeGetCurrentThread()->SuspendSemaphore,
                          Suspended,
                          KernelMode,
                          FALSE,
                          NULL);
}

ULONG
NTAPI
KeSuspendThread(PKTHREAD Thread)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    ULONG PreviousCount;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the APC Queue */
    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    /* Save the Old Count */
    PreviousCount = Thread->SuspendCount;

    /* Handle the maximum */
    if (PreviousCount == MAXIMUM_SUSPEND_COUNT)
    {
        /* Raise an exception */
        KiReleaseApcLock(&ApcLock);
        RtlRaiseStatus(STATUS_SUSPEND_COUNT_EXCEEDED);
    }

    /* Should we bother to queue at all? */
    if (Thread->ApcQueueable)
    {
        /* Increment the suspend count */
        Thread->SuspendCount++;

        /* Check if we should suspend it */
        if (!(PreviousCount) && !(Thread->FreezeCount))
        {
            /* Is the APC already inserted? */
            if (!Thread->SuspendApc.Inserted)
            {
                /* Not inserted, insert it */
                Thread->SuspendApc.Inserted = TRUE;
                KiInsertQueueApc(&Thread->SuspendApc, IO_NO_INCREMENT);
            }
            else
            {
                /* Lock the suspend semaphore */
                KiAcquireDispatcherObject(&Thread->SuspendSemaphore.Header);

                /* Unsignal the semaphore, the APC was already inserted */
                Thread->SuspendSemaphore.Header.SignalState--;

                /* Release the suspend semaphore */
                KiReleaseDispatcherObject(&Thread->SuspendSemaphore.Header);
            }
        }
    }

    /* Release Lock and return the Old State */
    KiReleaseApcLockFromSynchLevel(&ApcLock);
    KiExitDispatcher(ApcLock.OldIrql);
    return PreviousCount;
}

VOID
NTAPI
KeThawAllThreads(VOID)
{
    KLOCK_QUEUE_HANDLE LockHandle, ApcLock;
    PKTHREAD Current, CurrentThread = KeGetCurrentThread();
    PKPROCESS Process = CurrentThread->ApcState.Process;
    PLIST_ENTRY ListHead, NextEntry;
    LONG OldCount;
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the process */
    KiAcquireProcessLockRaiseToSynch(Process, &LockHandle);

    /* Loop the Process's Threads */
    ListHead = &Process->ThreadListHead;
    NextEntry = ListHead->Flink;
    do
    {
        /* Get the current thread */
        Current = CONTAINING_RECORD(NextEntry, KTHREAD, ThreadListEntry);

        /* Lock it */
        KiAcquireApcLockAtSynchLevel(Current, &ApcLock);

        /* Make sure we are frozen */
        OldCount = Current->FreezeCount;
        if (OldCount)
        {
            /* Decrease the freeze count */
            Current->FreezeCount--;

            /* Check if both counts are zero now */
            if (!(Current->SuspendCount) && (!Current->FreezeCount))
            {
                /* Lock the suspend semaphore */
                KiAcquireDispatcherObject(&Current->SuspendSemaphore.Header);

                /* Signal the suspend semaphore and wake it */
                Current->SuspendSemaphore.Header.SignalState++;
                KiWaitTest(&Current->SuspendSemaphore, 0);

                /* Unlock the suspend semaphore */
                KiReleaseDispatcherObject(&Current->SuspendSemaphore.Header);
            }
        }

        /* Release the APC lock */
        KiReleaseApcLockFromSynchLevel(&ApcLock);

        /* Go to the next one */
        NextEntry = NextEntry->Flink;
    } while (NextEntry != ListHead);

    /* Release the process lock and exit the dispatcher */
    KiReleaseProcessLockFromSynchLevel(&LockHandle);
    KiExitDispatcher(LockHandle.OldIrql);

    /* Leave the critical region */
    KeLeaveCriticalRegion();
}

BOOLEAN
NTAPI
KeTestAlertThread(IN KPROCESSOR_MODE AlertMode)
{
    PKTHREAD Thread = KeGetCurrentThread();
    BOOLEAN OldState;
    KLOCK_QUEUE_HANDLE ApcLock;
    UCHAR AlertModeIndex;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    ASSERT((AlertMode == KernelMode) || (AlertMode == UserMode));
    AlertModeIndex = (UCHAR)AlertMode;

    /* Lock the Dispatcher Database and the APC Queue */
    KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

    /* Save the old State */
    OldState = Thread->Alerted[AlertModeIndex];

    /* Check the Thread is alerted */
    if (OldState)
    {
        /* Disable alert for this mode */
        Thread->Alerted[AlertModeIndex] = FALSE;
    }
    else if ((AlertMode != KernelMode) &&
             (!IsListEmpty(&Thread->ApcState.ApcListHead[UserMode])))
    {
        /* If the mode is User and the Queue isn't empty, set Pending */
        Thread->ApcState.UserApcPending = TRUE;
    }

    /* Release Locks and return the Old State */
    KiReleaseApcLock(&ApcLock);
    return OldState;
}

NTSTATUS
NTAPI
KeInitThread(IN OUT PKTHREAD Thread,
             IN PVOID KernelStack,
             IN PKSYSTEM_ROUTINE SystemRoutine,
             IN PKSTART_ROUTINE StartRoutine,
             IN PVOID StartContext,
             IN PCONTEXT Context,
             IN PVOID Teb,
             IN PKPROCESS Process)
{
    BOOLEAN AllocatedStack = FALSE;
    ULONG i;
    PKWAIT_BLOCK TimerWaitBlock;
    PKTIMER Timer;
    NTSTATUS Status;

    /* Initialize the Dispatcher Header */
    Thread->Header.Type = ThreadObject;
    Thread->Header.ThreadControlFlags = 0;
    Thread->Header.DebugActive = FALSE;
    Thread->Header.SignalState = 0;
    InitializeListHead(&(Thread->Header.WaitListHead));

    /* Initialize the Mutant List */
    InitializeListHead(&Thread->MutantListHead);

    /* Initialize the wait blocks */
    for (i = 0; i< (THREAD_WAIT_OBJECTS + 1); i++)
    {
        /* Put our pointer */
        Thread->WaitBlock[i].Thread = Thread;
    }

    /* Set swap settings */
    Thread->EnableStackSwap = TRUE;
    Thread->IdealProcessor = 1;
#if (NTDDI_VERSION < NTDDI_WIN7)
    Thread->SwapBusy = FALSE;
#endif
    Thread->KernelStackResident = TRUE;
    Thread->AdjustReason = AdjustNone;
#if defined(_M_AMD64) && (NTDDI_VERSION >= NTDDI_WIN7)
    Thread->Running = FALSE;
    Thread->ReadyTransition = FALSE;
#endif

    /* Initialize the lock */
    KeInitializeSpinLock(&Thread->ThreadLock);

    /* Setup the Service Descriptor Table for Native Calls */
#if defined(_WIN64) && (NTDDI_VERSION >= NTDDI_LONGHORN)
    /* TODO(NT6.1): ServiceTable was removed from KTHREAD on amd64 starting at Vista
     * (per ndk/ketypes.h KTHREAD definition). Real NT routes the syscall table via
     * KeServiceDescriptorTable[Shadow] + Thread->GuiThread flag instead. We currently
     * just skip the per-thread assignment here so the build compiles for NT6.x x64,
     * but the actual GuiThread plumbing for the descriptor lookup needs to be wired
     * up properly. See traphandler.c for the matching read-side TODOs. */
#else
    Thread->ServiceTable = KeServiceDescriptorTable;
#endif

    /* Setup APC Fields */
    InitializeListHead(&Thread->ApcState.ApcListHead[KernelMode]);
    InitializeListHead(&Thread->ApcState.ApcListHead[UserMode]);
    Thread->ApcState.Process = Process;
    Thread->ApcStatePointer[OriginalApcEnvironment] = &Thread->ApcState;
    Thread->ApcStatePointer[AttachedApcEnvironment] = &Thread->SavedApcState;
    Thread->ApcStateIndex = OriginalApcEnvironment;
    Thread->ApcQueueable = TRUE;
    KeInitializeSpinLock(&Thread->ApcQueueLock);

    /* Initialize the Suspend APC */
    KeInitializeApc(&Thread->SuspendApc,
                    Thread,
                    OriginalApcEnvironment,
                    KiSuspendNop,
                    KiSuspendRundown,
                    KiSuspendThread,
                    KernelMode,
                    NULL);

    /* Initialize the Suspend Semaphore */
#if (NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64)
    KeInitializeEvent(&Thread->SuspendEvent, SynchronizationEvent, FALSE);
#else
    KeInitializeSemaphore(&Thread->SuspendSemaphore, 0, 2);
#endif

    /* Setup the timer */
    Timer = &Thread->Timer;
    KeInitializeTimer(Timer);
    TimerWaitBlock = &Thread->WaitBlock[TIMER_WAIT_BLOCK];
    TimerWaitBlock->Object = Timer;
    TimerWaitBlock->WaitKey = STATUS_TIMEOUT;
    TimerWaitBlock->WaitType = WaitAny;
#if !((NTDDI_VERSION >= NTDDI_WIN8) || defined(_M_ARM64))
    TimerWaitBlock->NextWaitBlock = NULL;
#endif

    /* Link the two wait lists together */
    TimerWaitBlock->WaitListEntry.Flink = &Timer->Header.WaitListHead;
    TimerWaitBlock->WaitListEntry.Blink = &Timer->Header.WaitListHead;

    /* Set the TEB and process */
    Thread->Teb = Teb;
    Thread->Process = Process;

    /* Check if we have a kernel stack */
    if (!KernelStack)
    {
        /* We don't, allocate one */
#if defined(_M_ARM64)
        DPRINT("[arm64][ke] KeInitThread: MmCreateKernelStack\n");
#endif
        KernelStack = MmCreateKernelStack(FALSE, 0);
#if defined(_M_ARM64)
        DPRINT("[arm64][ke] KeInitThread: MmCreateKernelStack returned %p\n",
                KernelStack);
#endif
        if (!KernelStack) return STATUS_INSUFFICIENT_RESOURCES;

        /* Remember for later */
        AllocatedStack = TRUE;
    }

    /* Set the Thread Stacks */
    Thread->InitialStack = KernelStack;
    Thread->StackBase = KernelStack;
    Thread->StackLimit = (ULONG_PTR)KernelStack - KERNEL_STACK_SIZE;
    Thread->KernelStackResident = TRUE;

    /* Enter SEH to avoid crashes due to user mode */
    Status = STATUS_SUCCESS;
    _SEH2_TRY
    {
        /* Initialize the Thread Context */
#if defined(_M_ARM64)
        DPRINT("[arm64][ke] KeInitThread: KiInitializeContextThread stack=%p limit=%p\n",
                Thread->InitialStack,
                (PVOID)Thread->StackLimit);
#endif
        KiInitializeContextThread(Thread,
                                  SystemRoutine,
                                  StartRoutine,
                                  StartContext,
                                  Context);
#if defined(_M_ARM64)
        DPRINT("[arm64][ke] KeInitThread: KiInitializeContextThread done kernelStack=%p\n",
                Thread->KernelStack);
#endif
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Set failure status */
        Status = STATUS_UNSUCCESSFUL;

        /* Check if a stack was allocated */
        if (AllocatedStack)
        {
            /* Delete the stack */
            MmDeleteKernelStack((PVOID)Thread->StackBase, FALSE);
            Thread->InitialStack = NULL;
        }
    }
    _SEH2_END;

    /* Set the Thread to initialized */
    Thread->State = Initialized;
    return Status;
}

VOID
NTAPI
KeInitializeThread(IN PKPROCESS Process,
                   IN OUT PKTHREAD Thread,
                   IN PKSYSTEM_ROUTINE SystemRoutine,
                   IN PKSTART_ROUTINE StartRoutine,
                   IN PVOID StartContext,
                   IN PCONTEXT Context,
                   IN PVOID Teb,
                   IN PVOID KernelStack)
{
    /* Initialize and start the thread on success */
    if (NT_SUCCESS(KeInitThread(Thread,
                                KernelStack,
                                SystemRoutine,
                                StartRoutine,
                                StartContext,
                                Context,
                                Teb,
                                Process)))
    {
        /* Start it */
        KeStartThread(Thread);
    }
}

VOID
NTAPI
KeUninitThread(IN PKTHREAD Thread)
{
    /* Delete the stack */
    MmDeleteKernelStack((PVOID)Thread->StackBase, FALSE);
    Thread->InitialStack = NULL;
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @unimplemented
 */
VOID
NTAPI
KeCapturePersistentThreadState(IN PVOID CurrentThread,
                               IN ULONG Setting1,
                               IN ULONG Setting2,
                               IN ULONG Setting3,
                               IN ULONG Setting4,
                               IN ULONG Setting5,
                               IN PVOID ThreadState)
{
    UNIMPLEMENTED;
}

/*
 * @implemented
 */
#undef KeGetCurrentThread
PKTHREAD
NTAPI
KeGetCurrentThread(VOID)
{
    /* Return the current thread on this PCR */
    return _KeGetCurrentThread();
}

/*
 * @implemented
 */
#undef KeGetPreviousMode
UCHAR
NTAPI
KeGetPreviousMode(VOID)
{
    /* Return the previous mode of this thread */
    return _KeGetPreviousMode();
}

/*
 * @implemented
 */
ULONG
NTAPI
KeQueryRuntimeThread(IN PKTHREAD Thread,
                     OUT PULONG UserTime)
{
    ASSERT_THREAD(Thread);

    /* Return the User Time */
    *UserTime = Thread->UserTime;

    /* Return the Kernel Time */
    return Thread->KernelTime;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeSetKernelStackSwapEnable(IN BOOLEAN Enable)
{
    BOOLEAN PreviousState;
    PKTHREAD Thread = KeGetCurrentThread();

    /* Save Old State */
    PreviousState = Thread->EnableStackSwap;

    /* Set New State */
    Thread->EnableStackSwap = Enable;

    /* Return Old State */
    return PreviousState;
}

/*
 * @implemented
 */
KPRIORITY
NTAPI
KeQueryPriorityThread(IN PKTHREAD Thread)
{
    ASSERT_THREAD(Thread);

    /* Return the current priority */
    return Thread->Priority;
}

/*
 * @implemented
 */
KPRIORITY
NTAPI
KeQueryEffectivePriorityThread(
    _In_ PKTHREAD Thread)
{
    ASSERT_THREAD(Thread);

    /* Without AutoBoost priority donation, the effective priority is the
     * scheduler's current one */
    return Thread->Priority;
}

/*
 * @implemented
 */
VOID
NTAPI
KeRevertToUserAffinityThread(VOID)
{
    KIRQL OldIrql;
    PKPRCB Prcb;
    PKTHREAD NextThread, CurrentThread = KeGetCurrentThread();
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);
    ASSERT(CurrentThread->SystemAffinityActive != FALSE);

    /* Lock the Dispatcher Database */
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireThreadLock(CurrentThread);

    /* Set the user affinity and processor and disable system affinity */
    CurrentThread->Affinity = CurrentThread->UserAffinity;
    CurrentThread->IdealProcessor = CurrentThread->UserIdealProcessor;
    CurrentThread->SystemAffinityActive = FALSE;

    /* Get the current PRCB and check if it doesn't match this affinity */
    Prcb = KeGetCurrentPrcb();
    if (!(Prcb->SetMember & KiThreadAffinityMask(CurrentThread)))
    {
        /* Lock the PRCB */
        KiAcquirePrcbLock(Prcb);

        /* Check if there's no next thread scheduled */
        if (!Prcb->NextThread)
        {
            /* Select a new thread and set it on standby */
            NextThread = KiSelectNextThread(Prcb);
            NextThread->State = Standby;
            Prcb->NextThread = NextThread;
        }

        /* Release the PRCB lock */
        KiReleasePrcbLock(Prcb);
    }

    /* Unlock dispatcher database */
    KiReleaseThreadLock(CurrentThread);
    KiExitDispatcher(OldIrql);
}

/*
 * @implemented
 */
UCHAR
NTAPI
KeSetIdealProcessorThread(IN PKTHREAD Thread,
                          IN UCHAR Processor)
{
    CCHAR OldIdealProcessor;
    KIRQL OldIrql;
    ASSERT(Processor <= MAXIMUM_PROCESSORS);

    /* Lock the Dispatcher Database */
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireThreadLock(Thread);

    /* Save Old Ideal Processor */
    OldIdealProcessor = Thread->UserIdealProcessor;

    /* Make sure a valid CPU was given */
    if (Processor < KeNumberProcessors)
    {
        /* Check if the user ideal CPU is in the affinity */
        if (KiThreadAffinityMask(Thread) & AFFINITY_MASK(Processor))
        {
            /* Set the ideal processor */
            Thread->IdealProcessor = Processor;

            /* Check if system affinity is used */
            if (!Thread->SystemAffinityActive)
            {
                /* It's not, so update the user CPU too */
                Thread->UserIdealProcessor = Processor;
            }
        }
    }

    /* Release dispatcher lock and return the old ideal CPU */
    KiReleaseThreadLock(Thread);
    KiExitDispatcher(OldIrql);
    return OldIdealProcessor;
}

/*
 * @implemented
 */
VOID
NTAPI
KeSetSystemAffinityThread(IN KAFFINITY Affinity)
{
    KIRQL OldIrql;
    PKPRCB Prcb;
    PKTHREAD NextThread, CurrentThread = KeGetCurrentThread();
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);
    ASSERT((Affinity & KeActiveProcessors) != 0);

    /* Lock the Dispatcher Database */
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireThreadLock(CurrentThread);

    /* Restore the affinity and enable system affinity */
    KiThreadAffinityMask(CurrentThread) = Affinity;
    CurrentThread->SystemAffinityActive = TRUE;

#ifdef CONFIG_SMP
    /* Calculate the ideal processor from the affinity set */
    CurrentThread->IdealProcessor =
        KiFindIdealProcessor(Affinity, CurrentThread->IdealProcessor);
#endif

    /* Get the current PRCB and check if it doesn't match this affinity */
    Prcb = KeGetCurrentPrcb();
    if (!(Prcb->SetMember & KiThreadAffinityMask(CurrentThread)))
    {
        /* Lock the PRCB */
        KiAcquirePrcbLock(Prcb);

        /* Check if there's no next thread scheduled */
        if (!Prcb->NextThread)
        {
            /* Select a new thread and set it on standby */
            NextThread = KiSelectNextThread(Prcb);
            NextThread->State = Standby;
            Prcb->NextThread = NextThread;
        }

        /* Release the PRCB lock */
        KiReleasePrcbLock(Prcb);
    }

    /* Unlock dispatcher database */
    KiReleaseThreadLock(CurrentThread);
    KiExitDispatcher(OldIrql);
}

/*
 * @implemented
 */
KAFFINITY
NTAPI
KeSetSystemAffinityThreadEx(IN KAFFINITY Affinity)
{
    PKTHREAD CurrentThread = KeGetCurrentThread();
    KAFFINITY OldAffinity;

    OldAffinity = CurrentThread->SystemAffinityActive ? KiThreadAffinityMask(CurrentThread) : 0;
    KeSetSystemAffinityThread(Affinity);
    return OldAffinity;
}

/*
 * @implemented
 */
VOID
NTAPI
KeRevertToUserAffinityThreadEx(IN KAFFINITY Affinity)
{
    if (Affinity != 0)
        KeSetSystemAffinityThread(Affinity);
    else
        KeRevertToUserAffinityThread();
}

/*
 * @implemented
 */
LONG
NTAPI
KeSetBasePriorityThread(IN PKTHREAD Thread,
                        IN LONG Increment)
{
    KIRQL OldIrql;
    KPRIORITY OldBasePriority, Priority, BasePriority;
    LONG OldIncrement;
    PKPROCESS Process;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Get the process */
    Process = Thread->ApcState.Process;

    /* Lock the Dispatcher Database */
    OldIrql = KeRaiseIrqlToSynchLevel();

    /* Lock the thread */
    KiAcquireThreadLock(Thread);

    /* Save the old base priority and increment */
    OldBasePriority = Thread->BasePriority;
    OldIncrement = OldBasePriority - Process->BasePriority;

    /* If priority saturation happened, use the saturated increment */
    if (Thread->Saturation) OldIncrement = (HIGH_PRIORITY + 1) / 2 *
                                            Thread->Saturation;

    /* Reset the saturation value */
    Thread->Saturation = 0;

    /* Now check if saturation is being used for the new value */
    if (abs(Increment) >= ((HIGH_PRIORITY + 1) / 2))
    {
        /* Check if we need positive or negative saturation */
        Thread->Saturation = (Increment > 0) ? 1 : -1;
    }

    /* Normalize the Base Priority */
    BasePriority = Process->BasePriority + Increment;
    if (Process->BasePriority >= LOW_REALTIME_PRIORITY)
    {
        /* Check if it's too low */
        if (BasePriority < LOW_REALTIME_PRIORITY)
        {
            /* Set it to the lowest real time level */
            BasePriority = LOW_REALTIME_PRIORITY;
        }

        /* Check if it's too high */
        if (BasePriority > HIGH_PRIORITY) BasePriority = HIGH_PRIORITY;

        /* We are at real time, so use the raw base priority */
        Priority = BasePriority;
    }
    else
    {
        /* Check if it's entering the real time range */
        if (BasePriority >= LOW_REALTIME_PRIORITY)
        {
            /* Set it to the highest dynamic level */
            BasePriority = LOW_REALTIME_PRIORITY - 1;
        }

        /* Check if it's too low and normalize it */
        if (BasePriority <= LOW_PRIORITY) BasePriority = 1;

        /* Check if Saturation is used */
        if (Thread->Saturation)
        {
            /* Use the raw base priority */
            Priority = BasePriority;
        }
        else
        {
            /* Otherwise, calculate the new priority */
            Priority = KiComputeNewPriority(Thread, 0);
            Priority += (BasePriority - OldBasePriority);

            /* Check if it entered the real-time range */
            if (Priority >= LOW_REALTIME_PRIORITY)
            {
                /* Normalize it down to the highest dynamic priority */
                Priority = LOW_REALTIME_PRIORITY - 1;
            }
            else if (Priority <= LOW_PRIORITY)
            {
                /* It went too low, normalize it */
                Priority = 1;
            }
        }
    }

    /* Finally set the new base priority */
    Thread->BasePriority = (SCHAR)BasePriority;

    /* Reset the decrements */
    Thread->PriorityDecrement = 0;

    /* Check if we're changing priority after all */
    if (Priority != Thread->Priority)
    {
        /* Reset the quantum and do the actual priority modification */
        KiSetThreadQuantum(Thread, Thread->QuantumReset);
        KiSetPriorityThread(Thread, Priority);
    }

    /* Release thread lock */
    KiReleaseThreadLock(Thread);

    /* Release the dispatcher database and return old increment */
    KiExitDispatcher(OldIrql);
    return OldIncrement;
}

/*
 * @implemented
 */
KPRIORITY
NTAPI
KeSetActualBasePriorityThread(
    _Inout_ PKTHREAD Thread,
    _In_ KPRIORITY Priority)
{
    KIRQL OldIrql;
    KPRIORITY OldBasePriority, NewPriority, BasePriority;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Normalize the requested base priority into the legal range */
    BasePriority = Priority;
    if (BasePriority > HIGH_PRIORITY) BasePriority = HIGH_PRIORITY;
    if (BasePriority <= LOW_PRIORITY) BasePriority = 1;

    /* Lock the Dispatcher Database */
    OldIrql = KeRaiseIrqlToSynchLevel();

    /* Lock the thread */
    KiAcquireThreadLock(Thread);

    /* Save the old base priority */
    OldBasePriority = Thread->BasePriority;

    /* Unlike KeSetBasePriorityThread, the exact base priority is applied,
     * so any prior saturation increment state is discarded */
    Thread->Saturation = 0;

    if (BasePriority >= LOW_REALTIME_PRIORITY)
    {
        /* Real-time threads run exactly at their base priority */
        NewPriority = BasePriority;
    }
    else
    {
        /* Rebase the dynamic priority by the base priority change */
        NewPriority = KiComputeNewPriority(Thread, 0);
        NewPriority += (BasePriority - OldBasePriority);

        /* Check if it entered the real-time range */
        if (NewPriority >= LOW_REALTIME_PRIORITY)
        {
            /* Normalize it down to the highest dynamic priority */
            NewPriority = LOW_REALTIME_PRIORITY - 1;
        }
        else if (NewPriority <= LOW_PRIORITY)
        {
            /* It went too low, normalize it */
            NewPriority = 1;
        }
    }

    /* Set the new base priority and reset the decrements */
    Thread->BasePriority = (SCHAR)BasePriority;
    Thread->PriorityDecrement = 0;

    /* Check if we're changing priority after all */
    if (NewPriority != Thread->Priority)
    {
        /* Reset the quantum and do the actual priority modification */
        KiSetThreadQuantum(Thread, Thread->QuantumReset);
        KiSetPriorityThread(Thread, NewPriority);
    }

    /* Release thread lock */
    KiReleaseThreadLock(Thread);

    /* Release the dispatcher database and return the old base priority */
    KiExitDispatcher(OldIrql);
    return OldBasePriority;
}

/*
 * @implemented
 */
KAFFINITY
NTAPI
KeSetAffinityThread(IN PKTHREAD Thread,
                    IN KAFFINITY Affinity)
{
    KIRQL OldIrql;
    KAFFINITY OldAffinity;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Raise IRQL */
    OldIrql = KeRaiseIrqlToSynchLevel();

    /* Call the internal function (it acquires the thread lock itself) */
    OldAffinity = KiSetAffinityThread(Thread, Affinity);

    /* Return old affinity */
    KiExitDispatcher(OldIrql);
    return OldAffinity;
}

/*
 * @implemented
 */
KPRIORITY
NTAPI
KeSetPriorityThread(IN PKTHREAD Thread,
                    IN KPRIORITY Priority)
{
    KIRQL OldIrql;
    KPRIORITY OldPriority;
    ASSERT_THREAD(Thread);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);
    ASSERT((Priority <= HIGH_PRIORITY) && (Priority >= LOW_PRIORITY));
    ASSERT(KeIsExecutingDpc() == FALSE);

    /* Lock the Dispatcher Database */
    OldIrql = KeRaiseIrqlToSynchLevel();

    /* Lock the thread */
    KiAcquireThreadLock(Thread);

    /* Save the old Priority and reset decrement */
    OldPriority = Thread->Priority;
    Thread->PriorityDecrement = 0;

    /* Make sure that an actual change is being done */
    if (Priority != Thread->Priority)
    {
        /* Reset the quantum */
        KiSetThreadQuantum(Thread, Thread->QuantumReset);

        /* Check if priority is being set too low and normalize if so */
        if ((Thread->BasePriority != 0) && !(Priority)) Priority = 1;

        /* Set the new Priority */
        KiSetPriorityThread(Thread, Priority);
    }

    /* Release thread lock */
    KiReleaseThreadLock(Thread);

    /* Release the dispatcher database */
    KiExitDispatcher(OldIrql);

    /* Return Old Priority */
    return OldPriority;
}

VOID
FASTCALL
KiQueueThreadForReaping(IN PKTHREAD Thread)
{
    PLIST_ENTRY *ListHead;
    PETHREAD Entry, SavedEntry;
    PETHREAD *ThreadAddr;

    Entry = (PETHREAD)PspReaperListHead.Flink;
    ThreadAddr = &((PETHREAD)Thread)->ReaperLink;

    do
    {
        ListHead = &PspReaperListHead.Flink;
        *ThreadAddr = Entry;
        SavedEntry = Entry;
        Entry = InterlockedCompareExchangePointer((PVOID*)ListHead,
                                                  ThreadAddr,
                                                  Entry);
    } while (Entry != SavedEntry);

    if (!Entry)
    {
        KiAcquireDispatcherObject(&ExWorkerQueue[HyperCriticalWorkQueue].WorkerQueue.Header);
        KiInsertQueue(&ExWorkerQueue[HyperCriticalWorkQueue].WorkerQueue,
                      &PspReaperWorkItem.List,
                      FALSE);
        KiReleaseDispatcherObject(&ExWorkerQueue[HyperCriticalWorkQueue].WorkerQueue.Header);
    }
}

/*
 * @implemented
 */
VOID
NTAPI
KeTerminateThread(IN KPRIORITY Increment)
{
    KLOCK_QUEUE_HANDLE LockHandle;
    PKTHREAD Thread = KeGetCurrentThread();
    PKPROCESS Process = Thread->ApcState.Process;
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Lock the process */
    KiAcquireProcessLockRaiseToSynch(Process, &LockHandle);

    /* Make sure we won't get Swapped */
    KiSetThreadSwapBusy(Thread);

    /* Save the Kernel and User Times */
    Process->KernelTime += Thread->KernelTime;
    Process->UserTime += Thread->UserTime;

#if !defined(_M_AMD64) || (NTDDI_VERSION < NTDDI_WIN7)
    KiQueueThreadForReaping(Thread);
#endif

    /* Check the thread has an associated queue */
    if (Thread->Queue)
    {
        /* Remove it from the list, and handle the queue */
        KiAcquireDispatcherObject(&Thread->Queue->Header);
        RemoveEntryList(&Thread->QueueListEntry);
        KiActivateWaiterQueue(Thread->Queue);
        KiReleaseDispatcherObject(&Thread->Queue->Header);
    }

    /* Signal the thread */
    KiAcquireDispatcherObject(&Thread->Header);
    Thread->Header.SignalState = TRUE;
    if (!IsListEmpty(&Thread->Header.WaitListHead))
    {
        /* Unwait the threads */
        KiWaitTest(Thread, Increment);
    }
    KiReleaseDispatcherObject(&Thread->Header);

    /* Remove the thread from the list */
    RemoveEntryList(&Thread->ThreadListEntry);

    /* Release the process lock */
    KiReleaseProcessLockFromSynchLevel(&LockHandle);

    /* Set us as terminated, decrease the Process's stack count */
    KiAcquireThreadLock(Thread);
    Thread->State = Terminated;

    /* Decrease stack count */
    ASSERT(Process->StackCount != 0);
    ASSERT(Process->State == ProcessInMemory);
    Process->StackCount--;
    if (!(Process->StackCount) && !(IsListEmpty(&Process->ThreadListHead)))
    {
        /* FIXME: Swap stacks */
    }

    /* Rundown arch-specific parts */
    KiRundownThread(Thread);

    /* Swap to a new thread */
    KiReleaseThreadLock(Thread);
    KiSwapThread(Thread, KeGetCurrentPrcb(), FALSE);
}
