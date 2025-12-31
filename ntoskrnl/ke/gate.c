/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/gate.c
 * PURPOSE:         Implements the Gate Dispatcher Object
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

VOID
FASTCALL
KeInitializeGate(IN PKGATE Gate)
{
    /* Initialize the Dispatcher Header */
    Gate->Header.Type = GateObject;
    Gate->Header.Signalling = FALSE;
    Gate->Header.Size = sizeof(KGATE) / sizeof(ULONG);
    Gate->Header.SignalState = 0;
    InitializeListHead(&(Gate->Header.WaitListHead));
}

VOID
FASTCALL
KeWaitForGate(IN PKGATE Gate,
              IN KWAIT_REASON WaitReason,
              IN KPROCESSOR_MODE WaitMode)
{
    KLOCK_QUEUE_HANDLE ApcLock;
    PKTHREAD Thread = KeGetCurrentThread();
    PKWAIT_BLOCK GateWaitBlock;
    LONG_PTR Status;
    PKQUEUE Queue;

    /* ARM64 DEBUG: Log gate details before assertion */
    DPRINT1("KeWaitForGate: Gate=%p, Type=0x%x, SignalState=%ld, WaitReason=%d\n",
            Gate,
            Gate ? Gate->Header.Type : 0xFF,
            Gate ? Gate->Header.SignalState : -1,
            WaitReason);

    /* If gate pointer looks suspicious, dump memory */
    if (Gate && ((ULONG_PTR)Gate & 0xF) != 0)
    {
        DPRINT1("KeWaitForGate: WARNING - Gate pointer %p is not 16-byte aligned!\n", Gate);
    }

    /* Check if gate pointer points to valid kernel memory */
    if (Gate)
    {
        ULONG_PTR GateAddr = (ULONG_PTR)Gate;
        if (GateAddr < 0xFFFF000000000000ULL)
        {
            DPRINT1("KeWaitForGate: ERROR - Gate pointer %p is not in kernel space!\n", Gate);
            DPRINT1("              First 8 bytes at gate: %016llX\n", *(PULONGLONG)Gate);
        }
        else
        {
            /* Dump the gate structure contents */
            DPRINT1("KeWaitForGate: Gate dump: Type=%02X Size=%02X SignalState=%08lX\n",
                    Gate->Header.Type,
                    Gate->Header.Size,
                    Gate->Header.SignalState);
            DPRINT1("               WaitListHead: Flink=%p Blink=%p\n",
                    Gate->Header.WaitListHead.Flink,
                    Gate->Header.WaitListHead.Blink);
        }
    }

    ASSERT_GATE(Gate);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Start wait loop */
    do
    {
        /* Acquire the APC lock */
        KiAcquireApcLockRaiseToSynch(Thread, &ApcLock);

        /* Check if a kernel APC is pending and we're below APC_LEVEL */
        if ((Thread->ApcState.KernelApcPending) &&
            !(Thread->SpecialApcDisable) &&
            (ApcLock.OldIrql < APC_LEVEL))
        {
            /* Release the lock, this will fire the APC */
            KiReleaseApcLock(&ApcLock);
        }
        else
        {
            /* Check if we have a queue and lock the dispatcher if so */
            Queue = Thread->Queue;
            if (Queue) KiAcquireDispatcherLockAtSynchLevel();

            /* Lock the thread */
            KiAcquireThreadLock(Thread);

            /* Lock the gate */
            KiAcquireDispatcherObject(&Gate->Header);

            /* Check if it's already signaled */
            if (Gate->Header.SignalState)
            {
                /* Unsignal it */
                Gate->Header.SignalState = 0;

                /* Release the gate and thread locks */
                KiReleaseDispatcherObject(&Gate->Header);
                KiReleaseThreadLock(Thread);

                /* Release the gate lock */
                if (Queue) KiReleaseDispatcherLockFromSynchLevel();

                /* Release the APC lock and return */
                KiReleaseApcLock(&ApcLock);
                break;
            }

            /* Setup a Wait Block */
            GateWaitBlock = &Thread->WaitBlock[0];
            GateWaitBlock->Object = (PVOID)Gate;
            GateWaitBlock->Thread = Thread;

            /* Set the Thread Wait Data */
            Thread->WaitMode = WaitMode;
            Thread->WaitReason = WaitReason;
            Thread->WaitIrql = ApcLock.OldIrql;
            Thread->State = GateWait;
            Thread->GateObject = Gate;

            /* Insert into the Wait List */
            InsertTailList(&Gate->Header.WaitListHead,
                           &GateWaitBlock->WaitListEntry);

            /* Release the gate lock */
            KiReleaseDispatcherObject(&Gate->Header);

            /* Set swap busy */
            KiSetThreadSwapBusy(Thread);

            /* Release the thread lock */
            KiReleaseThreadLock(Thread);

            /* Check if we had a queue */
            if (Queue)
            {
                /* Wake it up */
                KiActivateWaiterQueue(Queue);

                /* Release the dispatcher lock */
                KiReleaseDispatcherLockFromSynchLevel();
            }

            /* Release the APC lock but stay at DPC level */
            KiReleaseApcLockFromSynchLevel(&ApcLock);

            /* Find a new thread to run */
            Status = KiSwapThread(Thread, KeGetCurrentPrcb());

            /* Make sure we weren't executing an APC */
            if (Status == STATUS_SUCCESS) return;
        }
    } while (TRUE);
}

VOID
FASTCALL
KeSignalGateBoostPriority(IN PKGATE Gate)
{
    PKTHREAD WaitThread;
    PKWAIT_BLOCK WaitBlock;
    KIRQL OldIrql;
    ASSERT_GATE(Gate);
    ASSERT_IRQL_LESS_OR_EQUAL(DISPATCH_LEVEL);

    /* Start entry loop */
    for (;;)
    {
        /* Raise to synch level */
        OldIrql = KeRaiseIrqlToSynchLevel();

        /* Lock the gate */
        KiAcquireDispatcherObject(&Gate->Header);

        /* Make sure we're not already signaled or that the list is empty */
        if (Gate->Header.SignalState) break;

        /* Check if our wait list is empty */
        if (IsListEmpty(&Gate->Header.WaitListHead))
        {
            /* It is, so signal the event */
            Gate->Header.SignalState = 1;
            break;
        }
        else
        {
            /* Get WaitBlock */
            WaitBlock = CONTAINING_RECORD(Gate->Header.WaitListHead.Flink,
                                          KWAIT_BLOCK,
                                          WaitListEntry);

            /* Get the Associated thread */
            WaitThread = WaitBlock->Thread;

            /* Check to see if the waiting thread is locked */
            if (KiTryThreadLock(WaitThread))
            {
                /* Unlock the gate */
                KiReleaseDispatcherObject(&Gate->Header);

                /* Lower IRQL and loop again */
                KeLowerIrql(OldIrql);
                continue;
            }

            /* Remove it */
            RemoveEntryList(&WaitBlock->WaitListEntry);

            /* Clear wait status */
            WaitThread->WaitStatus = STATUS_SUCCESS;

            /* Set state and CPU */
            WaitThread->State = DeferredReady;
            WaitThread->DeferredProcessor = KeGetCurrentPrcb()->Number;

            /* Release the gate lock */
            KiReleaseDispatcherObject(&Gate->Header);

            /* Release the thread lock */
            KiReleaseThreadLock(WaitThread);

            /* FIXME: Boosting */

            /* Check if we have a queue */
            if (WaitThread->Queue)
            {
                /* Acquire the dispatcher lock */
                KiAcquireDispatcherLockAtSynchLevel();

                /* Check if we still have one */
                if (WaitThread->Queue)
                {
                    /* Increment active threads */
                    WaitThread->Queue->CurrentCount++;
                }

                /* Release lock */
                KiReleaseDispatcherLockFromSynchLevel();
            }

            /* Make the thread ready */
            KiReadyThread(WaitThread);

            /* Exit the dispatcher */
            KiExitDispatcher(OldIrql);
            return;
        }
    }

    /* If we got here, then there's no rescheduling. */
    KiReleaseDispatcherObject(&Gate->Header);
    KeLowerIrql(OldIrql);
}

/* EOF */
