/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/dpc.c
 * PURPOSE:         Deferred Procedure Call (DPC) Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  Philip Susi (phreak@iag.net)
 *                  Eric Kohl
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* DebugDpcTime removed from KPRCB at Vista+ */
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
#define KiResetDebugDpcTime(Prcb) ((void)(Prcb))
#else
#define KiResetDebugDpcTime(Prcb) ((Prcb)->DebugDpcTime = 0)
#endif

/* GLOBALS *******************************************************************/

ULONG KiMaximumDpcQueueDepth = 4;
ULONG KiMinimumDpcRate = 3;
ULONG KiAdjustDpcThreshold = 20;
ULONG KiIdealDpcRate = 20;
BOOLEAN KeThreadDpcEnable;
FAST_MUTEX KiGenericCallDpcMutex;
KDPC KiTimerExpireDpc;
ULONG KiTimeLimitIsrMicroseconds;
ULONG KiDPCTimeout = 110;

/* PRIVATE FUNCTIONS *********************************************************/

VOID
NTAPI
KiCheckTimerTable(IN ULARGE_INTEGER CurrentTime)
{
#if DBG
    ULONG i = 0;
    PLIST_ENTRY ListHead, NextEntry;
    KIRQL OldIrql;
    PKTIMER Timer;

    /* Raise IRQL to high and loop timers */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    do
    {
        /* Loop the current list */
        ListHead = &KiTimerTableListHead[i].Entry;
        NextEntry = ListHead->Flink;
        while (NextEntry != ListHead)
        {
            /* Get the timer and move to the next one */
            Timer = CONTAINING_RECORD(NextEntry, KTIMER, TimerListEntry);
            NextEntry = NextEntry->Flink;

            /* Check if it expired */
            if (Timer->DueTime.QuadPart <= CurrentTime.QuadPart)
            {
                /* Check if the DPC was queued, but didn't run */
                if (!(KeGetCurrentPrcb()->TimerRequest) &&
                    !(*((volatile PULONG*)(&KiTimerExpireDpc.DpcData))))
                {
                    /* This is bad, breakpoint! */
                    DPRINT1("Invalid timer state!\n");
                    DbgBreakPoint();
                }
            }
        }

        /* Move to the next timer */
        i++;
    } while(i < TIMER_TABLE_SIZE);

    /* Lower IRQL and return */
    KeLowerIrql(OldIrql);
#endif
}

VOID
NTAPI
KiTimerExpiration(IN PKDPC Dpc,
                  IN PVOID DeferredContext,
                  IN PVOID SystemArgument1,
                  IN PVOID SystemArgument2)
{
    ULARGE_INTEGER SystemTime, InterruptTime;
    LARGE_INTEGER Interval;
    LONG Limit, Index, i;
    ULONG Timers, ActiveTimers, DpcCalls;
    PLIST_ENTRY ListHead, NextEntry;
    KIRQL OldIrql;
    PKTIMER Timer;
    PKDPC TimerDpc;
    ULONG Period;
    DPC_QUEUE_ENTRY DpcEntry[MAX_TIMER_DPCS];
    PKSPIN_LOCK_QUEUE LockQueue;
    PKPRCB Prcb = KeGetCurrentPrcb();

    /* Disable interrupts */
    _disable();

    /* Query system and interrupt time */
    KeQuerySystemTime((PLARGE_INTEGER)&SystemTime);
    InterruptTime.QuadPart = KeQueryInterruptTime();
    Limit = KeTickCount.LowPart;

    /* Bring interrupts back */
    _enable();

    /* Get the index of the timer and normalize it */
    Index = PtrToLong(SystemArgument1);
    if ((Limit - Index) >= TIMER_TABLE_SIZE)
    {
        /* Normalize it */
        Limit = Index + TIMER_TABLE_SIZE - 1;
    }

    /* Setup index and actual limit */
    Index--;
    Limit &= (TIMER_TABLE_SIZE - 1);

    /* Setup accounting data */
    DpcCalls = 0;
    Timers = 24;
    ActiveTimers = 4;

    /* Raise IRQL to synchronization level */
    OldIrql = KeRaiseIrqlToSynchLevel();

    /* Start expiration loop */
    do
    {
        /* Get the current index */
        Index = (Index + 1) & (TIMER_TABLE_SIZE - 1);

        /* Get list pointers and loop the list */
        ListHead = &KiTimerTableListHead[Index].Entry;
        while (ListHead != ListHead->Flink)
        {
            /* Lock the timer and go to the next entry */
            LockQueue = KiAcquireTimerLock(Index);
            NextEntry = ListHead->Flink;

            /* Get the current timer and check its due time */
            Timers--;
            Timer = CONTAINING_RECORD(NextEntry, KTIMER, TimerListEntry);
            if ((NextEntry != ListHead) &&
                (Timer->DueTime.QuadPart <= InterruptTime.QuadPart))
            {
                /* It's expired, remove it */
                ActiveTimers--;
                KiRemoveEntryTimer(Timer);

                /* Make it non-inserted and unlock it */
                Timer->Header.Inserted = FALSE;
                KiReleaseTimerLock(LockQueue);

                /* Get the DPC and period */
                TimerDpc = Timer->Dpc;
                Period = Timer->Period;

                /* Signal it and wake waiters under the timer object lock */
                KiAcquireDispatcherObject(&Timer->Header);
                Timer->Header.SignalState = 1;
                if (!IsListEmpty(&Timer->Header.WaitListHead))
                {
                    KiWaitTest(Timer, IO_NO_INCREMENT);
                }
                KiReleaseDispatcherObject(&Timer->Header);

                /* Check if we have a period */
                if (Period)
                {
                    /* Calculate the interval and insert the timer */
                    Interval.QuadPart = Int32x32To64(Period, -10000);
                    while (!KiInsertTreeTimer(Timer, Interval));
                }

                /* Check if we have a DPC */
                if (TimerDpc)
                {
#ifdef CONFIG_SMP
                    /*
                     * If the DPC is targeted to another processor,
                     * then insert it into that processor's DPC queue
                     * instead of delivering it now.
                     * If the DPC is a threaded DPC, and the current CPU
                     * has threaded DPCs enabled (KiExecuteDpc is actively parsing DPCs),
                     * then also insert it into the DPC queue for threaded delivery,
                     * instead of doing it here.
                     */
                    if (((TimerDpc->Number >= MAXIMUM_PROCESSORS) &&
                        ((TimerDpc->Number - MAXIMUM_PROCESSORS) != Prcb->Number)) ||
                        ((TimerDpc->Type == ThreadedDpcObject) && (Prcb->ThreadDpcEnable)))
                    {
                        /* Queue it */
                        KeInsertQueueDpc(TimerDpc,
                                         UlongToPtr(SystemTime.LowPart),
                                         UlongToPtr(SystemTime.HighPart));
                    }
                    else
#endif
                    {
                        /* Setup the DPC Entry */
                        DpcEntry[DpcCalls].Dpc = TimerDpc;
                        DpcEntry[DpcCalls].Routine = TimerDpc->DeferredRoutine;
                        DpcEntry[DpcCalls].Context = TimerDpc->DeferredContext;
                        DpcCalls++;
                        ASSERT(DpcCalls < MAX_TIMER_DPCS);
                    }
                }

                /* Check if we're done processing */
                if (!(ActiveTimers) || !(Timers))
                {
                    /* Exit the dispatcher while doing DPCs */
                    KiExitDispatcher(DISPATCH_LEVEL);

                    /* Start looping all DPC Entries */
                    for (i = 0; DpcCalls; DpcCalls--, i++)
                    {
#if DBG
                        /* Clear DPC Time */
                        KiResetDebugDpcTime(Prcb);
#endif

                        /* Call the DPC */
                        DpcEntry[i].Routine(DpcEntry[i].Dpc,
                                            DpcEntry[i].Context,
                                            UlongToPtr(SystemTime.LowPart),
                                            UlongToPtr(SystemTime.HighPart));
                    }

                    /* Reset accounting */
                    Timers = 24;
                    ActiveTimers = 4;

                    /* Raise back to synchronization level */
                    KeRaiseIrqlToSynchLevel();
                }
            }
            else
            {
                /* Check if the timer list is empty */
                if (NextEntry != ListHead)
                {
                    /* Sanity check */
                    ASSERT(KiTimerTableListHead[Index].Time.QuadPart <=
                           Timer->DueTime.QuadPart);

                    /* Update the time */
                    _disable();
                    KiTimerTableListHead[Index].Time.QuadPart =
                        Timer->DueTime.QuadPart;
                    _enable();
                }

                /* Release the lock */
                KiReleaseTimerLock(LockQueue);

                /* Check if we've scanned all the timers we could */
                if (!Timers)
                {
                    /* Exit the dispatcher while doing DPCs */
                    KiExitDispatcher(DISPATCH_LEVEL);

                    /* Start looping all DPC Entries */
                    for (i = 0; DpcCalls; DpcCalls--, i++)
                    {
#if DBG
                        /* Clear DPC Time */
                        KiResetDebugDpcTime(Prcb);
#endif

                        /* Call the DPC */
                        DpcEntry[i].Routine(DpcEntry[i].Dpc,
                                            DpcEntry[i].Context,
                                            UlongToPtr(SystemTime.LowPart),
                                            UlongToPtr(SystemTime.HighPart));
                    }

                    /* Reset accounting */
                    Timers = 24;
                    ActiveTimers = 4;

                    /* Raise back to synchronization level */
                    KeRaiseIrqlToSynchLevel();
                }

                /* Done looping */
                break;
            }
        }
    } while (Index != Limit);

    /* Verify the timer table, on debug builds */
    if (KeNumberProcessors == 1) KiCheckTimerTable(InterruptTime);

    /* Check if we still have DPC entries */
    if (DpcCalls)
    {
        /* Exit the dispatcher while doing DPCs */
        KiExitDispatcher(DISPATCH_LEVEL);

        /* Start looping all DPC Entries */
        for (i = 0; DpcCalls; DpcCalls--, i++)
        {
#if DBG
            /* Clear DPC Time */
            KiResetDebugDpcTime(Prcb);
#endif

            /* Call the DPC */
            DpcEntry[i].Routine(DpcEntry[i].Dpc,
                                DpcEntry[i].Context,
                                UlongToPtr(SystemTime.LowPart),
                                UlongToPtr(SystemTime.HighPart));
        }

        /* Lower IRQL if we need to */
        if (OldIrql != DISPATCH_LEVEL) KeLowerIrql(OldIrql);
    }
    else
    {
        /* Exit the dispatcher */
        KiExitDispatcher(OldIrql);
    }
}

VOID
FASTCALL
KiTimerListExpire(IN PLIST_ENTRY ExpiredListHead,
                  IN KIRQL OldIrql)
{
    ULARGE_INTEGER SystemTime;
    LARGE_INTEGER Interval;
    LONG i;
    ULONG DpcCalls = 0;
    PKTIMER Timer;
    PKDPC TimerDpc;
    ULONG Period;
    DPC_QUEUE_ENTRY DpcEntry[MAX_TIMER_DPCS];
    PKPRCB Prcb = KeGetCurrentPrcb();

    /* Query system */
    KeQuerySystemTime((PLARGE_INTEGER)&SystemTime);

    /* Loop expired list */
    while (ExpiredListHead->Flink != ExpiredListHead)
    {
        /* Get the current timer */
        Timer = CONTAINING_RECORD(ExpiredListHead->Flink, KTIMER, TimerListEntry);

        /* Remove it */
        RemoveEntryList(&Timer->TimerListEntry);

        /* Not inserted */
        Timer->Header.Inserted = FALSE;

        /* Get the DPC and period */
        TimerDpc = Timer->Dpc;
        Period = Timer->Period;

        /* Signal it and wake waiters under the timer object lock */
        KiAcquireDispatcherObject(&Timer->Header);
        Timer->Header.SignalState = 1;
        if (!IsListEmpty(&Timer->Header.WaitListHead))
        {
            KiWaitTest(Timer, IO_NO_INCREMENT);
        }
        KiReleaseDispatcherObject(&Timer->Header);

        /* Check if we have a period */
        if (Period)
        {
            /* Calculate the interval and insert the timer */
            Interval.QuadPart = Int32x32To64(Period, -10000);
            while (!KiInsertTreeTimer(Timer, Interval));
        }

        /* Check if we have a DPC */
        if (TimerDpc)
        {
#ifdef CONFIG_SMP
            /*
             * If the DPC is targeted to another processor,
             * then insert it into that processor's DPC queue
             * instead of delivering it now.
             * If the DPC is a threaded DPC, and the current CPU
             * has threaded DPCs enabled (KiExecuteDpc is actively parsing DPCs),
             * then also insert it into the DPC queue for threaded delivery,
             * instead of doing it here.
             */
            if (((TimerDpc->Number >= MAXIMUM_PROCESSORS) &&
                ((TimerDpc->Number - MAXIMUM_PROCESSORS) != Prcb->Number)) ||
                ((TimerDpc->Type == ThreadedDpcObject) && (Prcb->ThreadDpcEnable)))
            {
                /* Queue it */
                KeInsertQueueDpc(TimerDpc,
                                 UlongToPtr(SystemTime.LowPart),
                                 UlongToPtr(SystemTime.HighPart));
            }
            else
#endif
            {
                /* Setup the DPC Entry */
                DpcEntry[DpcCalls].Dpc = TimerDpc;
                DpcEntry[DpcCalls].Routine = TimerDpc->DeferredRoutine;
                DpcEntry[DpcCalls].Context = TimerDpc->DeferredContext;
                DpcCalls++;
                ASSERT(DpcCalls < MAX_TIMER_DPCS);
            }
        }
    }

    /* Check if we still have DPC entries */
    if (DpcCalls)
    {
        /* Exit the dispatcher while doing DPCs */
        KiExitDispatcher(DISPATCH_LEVEL);

        /* Start looping all DPC Entries */
        for (i = 0; DpcCalls; DpcCalls--, i++)
        {
#if DBG
            /* Clear DPC Time */
            KiResetDebugDpcTime(Prcb);
#endif

            /* Call the DPC */
            DpcEntry[i].Routine(DpcEntry[i].Dpc,
                                DpcEntry[i].Context,
                                UlongToPtr(SystemTime.LowPart),
                                UlongToPtr(SystemTime.HighPart));
        }

        /* Lower IRQL */
        KeLowerIrql(OldIrql);
    }
    else
    {
        /* Exit the dispatcher */
        KiExitDispatcher(OldIrql);
    }
}

_Requires_lock_not_held_(Prcb->PrcbLock)
VOID
NTAPI
KiQuantumEnd(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD NextThread, Thread = Prcb->CurrentThread;

    /* Check if a DPC Event was requested to be signaled */
    if (InterlockedExchange(&Prcb->DpcSetEventRequest, 0))
    {
        /* Signal it */
        KeSetEvent(&Prcb->DpcEvent, 0, 0);
    }

    /* Raise to synchronization level and lock the PRCB and thread */
    KeRaiseIrqlToSynchLevel();
    KiAcquireThreadLock(Thread);
    KiAcquirePrcbLock(Prcb);

    /* Check if Quantum expired */
    if (KiIsThreadQuantumExpired(Thread))
    {
        /* Check if we're real-time and with quantums disabled */
        if ((Thread->Priority >= LOW_REALTIME_PRIORITY) &&
            (Thread->ApcState.Process->DisableQuantum))
        {
            /* Otherwise, set maximum quantum */
            KiSetThreadQuantum(Thread, MAX_QUANTUM);
        }
        else
        {
            /* Reset the new Quantum */
            KiSetThreadQuantum(Thread, Thread->QuantumReset);

            /* Calculate new priority */
            Thread->Priority = KiComputeNewPriority(Thread, 1);

            /* Check if a new thread is scheduled */
            if (!Prcb->NextThread)
            {
                /* Get a new ready thread */
                NextThread = KiSelectReadyThread(Thread->Priority, Prcb);
                if (NextThread)
                {
                    /* Found one, set it on standby */
                    NextThread->State = Standby;
                    Prcb->NextThread = NextThread;
                }
            }
            else
            {
                /* Otherwise, make sure that this thread doesn't get preempted */
                Thread->Preempted = FALSE;
            }
        }
    }

    /* Release the thread lock */
    KiReleaseThreadLock(Thread);

    /* A satisfied wait can leave this running thread selected on itself. */
    if (KiConsumeSelfNextThread(Prcb, Thread))
    {
        KiReleasePrcbLock(Prcb);
        KeLowerIrql(DISPATCH_LEVEL);
        return;
    }

    /* Check if there's no thread scheduled */
    if (!Prcb->NextThread)
    {
        /* Just leave now */
        KiReleasePrcbLock(Prcb);
        KeLowerIrql(DISPATCH_LEVEL);
        return;
    }

    /* Get the next thread now */
    NextThread = Prcb->NextThread;

    /* Set current thread's swap busy to true */
    KiSetThreadSwapBusy(Thread);

#if defined(_M_ARM64)
    _disable();
#endif

    /* Switch threads in PRCB */
    Prcb->NextThread = NULL;
    Prcb->CurrentThread = NextThread;

    /* Set thread to running and the switch reason to Quantum End */
    NextThread->State = Running;
    Thread->WaitReason = WrQuantumEnd;

    /* Queue it on the ready lists */
    KxQueueReadyThread(Thread, Prcb);

    /* Set wait IRQL to APC_LEVEL */
    Thread->WaitIrql = APC_LEVEL;

    /* Swap threads */
    KiSwapContext(APC_LEVEL, Thread);

    /* Lower IRQL back to DISPATCH_LEVEL */
    KeLowerIrql(DISPATCH_LEVEL);
}

VOID
FASTCALL
KiRetireDpcList(IN PKPRCB Prcb)
{
    PKDPC_DATA DpcData;
#if (NTDDI_VERSION < NTDDI_LONGHORN)
    PLIST_ENTRY ListHead;
#endif
    PLIST_ENTRY DpcEntry;
    PKDPC Dpc;
    PKDEFERRED_ROUTINE DeferredRoutine;
    PVOID DeferredContext, SystemArgument1, SystemArgument2;
    ULONG_PTR TimerHand;
#ifdef CONFIG_SMP
    KIRQL OldIrql;
#endif

    /* Get data and list variables before starting anything else */
    DpcData = &Prcb->DpcData[DPC_NORMAL];
#if (NTDDI_VERSION < NTDDI_LONGHORN)
    ListHead = &DpcData->DpcListHead;
#endif

    /* Main outer loop */
    do
    {
        /* Set us as active */
        Prcb->DpcRoutineActive = TRUE;

        /* Check if this is a timer expiration request */
        if (Prcb->TimerRequest)
        {
            /* It is, get the timer hand and disable timer request */
            TimerHand = Prcb->TimerHand;
            Prcb->TimerRequest = 0;

            /* Expire timers with interrupts enabled */
            _enable();
            KiTimerExpiration(NULL, NULL, (PVOID)TimerHand, NULL);
            _disable();
        }

        /* Loop while we have entries in the queue */
        while (DpcData->DpcQueueDepth != 0)
        {
            /* Lock the DPC data and get the DPC entry*/
            KeAcquireSpinLockAtDpcLevel(&DpcData->DpcLock);

#if (NTDDI_VERSION >= NTDDI_LONGHORN)
            /* Vista+: KDPC_LIST uses SINGLE_LIST_ENTRY */
            {
                PSINGLE_LIST_ENTRY SListEntry = PopEntryList(&DpcData->DpcList.ListHead);
                if (SListEntry)
                {
                    DpcEntry = (PLIST_ENTRY)SListEntry;
                    Dpc = CONTAINING_RECORD(DpcEntry, KDPC, DpcListEntry);
                    /* Clear LastEntry if list is now empty */
                    if (!DpcData->DpcList.ListHead.Next)
                        DpcData->DpcList.LastEntry = NULL;
                }
                else
                {
                    DpcEntry = NULL;
                    Dpc = NULL;
                }
            }

            /* Make sure we have an entry */
            if (Dpc)
            {
#else
            DpcEntry = ListHead->Flink;

            /* Make sure we have an entry */
            if (DpcEntry != ListHead)
            {
                /* Remove the DPC from the list */
                RemoveEntryList(DpcEntry);
                Dpc = CONTAINING_RECORD(DpcEntry, KDPC, DpcListEntry);
#endif

                /* Clear its DPC data and save its parameters */
                Dpc->DpcData = NULL;
                DeferredRoutine = Dpc->DeferredRoutine;
                DeferredContext = Dpc->DeferredContext;
                SystemArgument1 = Dpc->SystemArgument1;
                SystemArgument2 = Dpc->SystemArgument2;

                /* Decrease the queue depth */
                DpcData->DpcQueueDepth--;

#if DBG
                /* Clear DPC Time */
                KiResetDebugDpcTime(Prcb);
#endif

                /* Release the lock */
                KeReleaseSpinLockFromDpcLevel(&DpcData->DpcLock);

                /* Re-enable interrupts */
                _enable();

                /* Call the DPC */
                DeferredRoutine(Dpc,
                                DeferredContext,
                                SystemArgument1,
                                SystemArgument2);
                ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

                /* Disable interrupts and keep looping */
                _disable();
            }
            else
            {
                /* The queue should be flushed now */
                ASSERT(DpcData->DpcQueueDepth == 0);

                /* Release DPC Lock */
                KeReleaseSpinLockFromDpcLevel(&DpcData->DpcLock);
            }
        }

        /* Clear DPC Flags */
        Prcb->DpcRoutineActive = FALSE;
        Prcb->DpcInterruptRequested = FALSE;

#ifdef CONFIG_SMP
        /* Check if we have deferred threads */
        if (Prcb->DeferredReadyListHead.Next)
        {

            /* Re-enable interrupts and raise to synch */
            _enable();
            OldIrql = KeRaiseIrqlToSynchLevel();

            /* Process deferred threads */
            KiProcessDeferredReadyList(Prcb);

            /* Lower IRQL back and disable interrupts */
            KeLowerIrql(OldIrql);
            _disable();
        }
#endif
    } while (DpcData->DpcQueueDepth != 0);
}

VOID
NTAPI
KiInitializeDpc(IN PKDPC Dpc,
                IN PKDEFERRED_ROUTINE DeferredRoutine,
                IN PVOID DeferredContext,
                IN KOBJECTS Type)
{
    /* Setup the DPC Object */
    Dpc->Type = Type;
    Dpc->Number = 0;
    Dpc->Importance= MediumImportance;
    Dpc->DeferredRoutine = DeferredRoutine;
    Dpc->DeferredContext = DeferredContext;
    Dpc->DpcData = NULL;
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeThreadedDpc(IN PKDPC Dpc,
                        IN PKDEFERRED_ROUTINE DeferredRoutine,
                        IN PVOID DeferredContext)
{
    /* Call the internal routine */
    KiInitializeDpc(Dpc, DeferredRoutine, DeferredContext, ThreadedDpcObject);
}

/*
 * @implemented
 */
VOID
NTAPI
KeInitializeDpc(IN PKDPC Dpc,
                IN PKDEFERRED_ROUTINE DeferredRoutine,
                IN PVOID DeferredContext)
{
    /* Call the internal routine */
    KiInitializeDpc(Dpc, DeferredRoutine, DeferredContext, DpcObject);
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeInsertQueueDpc(IN PKDPC Dpc,
                 IN PVOID SystemArgument1,
                 IN PVOID SystemArgument2)
{
    KIRQL OldIrql;
    PKPRCB Prcb, CurrentPrcb;
    ULONG Cpu;
    PKDPC_DATA DpcData;
    BOOLEAN DpcConfigured = FALSE, DpcInserted = FALSE;
    ASSERT_DPC(Dpc);

    /* Check IRQL and Raise it to HIGH_LEVEL */
    KeRaiseIrql(HIGH_LEVEL, &OldIrql);
    CurrentPrcb = KeGetCurrentPrcb();

    /* Check if the DPC has more then the maximum number of CPUs */
    if (Dpc->Number >= MAXIMUM_PROCESSORS)
    {
        /* Then substract the maximum and get that PRCB. */
        Cpu = Dpc->Number - MAXIMUM_PROCESSORS;
        Prcb = KiProcessorBlock[Cpu];
    }
    else
    {
        /* Use the current one */
        Prcb = CurrentPrcb;
        Cpu = Prcb->Number;
    }

    /* Check if this is a threaded DPC and threaded DPCs are enabled */
    if ((Dpc->Type == ThreadedDpcObject) && (Prcb->ThreadDpcEnable))
    {
        /* Then use the threaded data */
        DpcData = &Prcb->DpcData[DPC_THREADED];
    }
    else
    {
        /* Otherwise, use the regular data */
        DpcData = &Prcb->DpcData[DPC_NORMAL];
    }

    /* Acquire the DPC lock */
    KiAcquireSpinLock(&DpcData->DpcLock);

    /* Get the DPC Data */
    if (!InterlockedCompareExchangePointer(&Dpc->DpcData, DpcData, NULL))
    {
        /* Now we can play with the DPC safely */
        Dpc->SystemArgument1 = SystemArgument1;
        Dpc->SystemArgument2 = SystemArgument2;
        DpcData->DpcQueueDepth++;
        DpcData->DpcCount++;
        DpcConfigured = TRUE;

        /* Check if this is a high importance DPC */
        if (Dpc->Importance == HighImportance)
        {
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
            /* Vista+: push to head of singly-linked list */
            PushEntryList(&DpcData->DpcList.ListHead, (PSINGLE_LIST_ENTRY)&Dpc->DpcListEntry);
            if (!DpcData->DpcList.LastEntry)
                DpcData->DpcList.LastEntry = (PSINGLE_LIST_ENTRY)&Dpc->DpcListEntry;
#else
            InsertHeadList(&DpcData->DpcListHead, &Dpc->DpcListEntry);
#endif
        }
        else
        {
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
            /* Vista+: append to tail of singly-linked list */
            PSINGLE_LIST_ENTRY Entry = (PSINGLE_LIST_ENTRY)&Dpc->DpcListEntry;
            Entry->Next = NULL;
            if (DpcData->DpcList.LastEntry)
                DpcData->DpcList.LastEntry->Next = Entry;
            else
                DpcData->DpcList.ListHead.Next = Entry;
            DpcData->DpcList.LastEntry = Entry;
#else
            InsertTailList(&DpcData->DpcListHead, &Dpc->DpcListEntry);
#endif
        }

        /* Check if this is the DPC on the threaded list */
        if (&Prcb->DpcData[DPC_THREADED] == DpcData)
        {
            /* Make sure a threaded DPC isn't already active */
            if (!(Prcb->DpcThreadActive) && !(Prcb->DpcThreadRequested))
            {
                /* FIXME: Setup Threaded DPC */
                UNIMPLEMENTED_FATAL("Threaded DPC not supported\n");
            }
        }
        else
        {
            KeMemoryBarrier();

            /* Make sure a DPC isn't executing already */
            if (!(Prcb->DpcRoutineActive) && !(Prcb->DpcInterruptRequested))
            {
                /* Check if this is the same CPU */
                if (Prcb != CurrentPrcb)
                {
                    /*
                     * Check if the DPC is of high importance or above the
                     * maximum depth. If it is, then make sure that the CPU
                     * isn't idle, or that it's sleeping.
                     */
                    if (((Dpc->Importance == HighImportance) ||
                        (DpcData->DpcQueueDepth >=
                         Prcb->MaximumDpcQueueDepth)) &&
                        (!(AFFINITY_MASK(Cpu) & KiIdleSummary) ||
                         (Prcb->Sleeping)))
                    {
                        /* Set interrupt requested */
                        Prcb->DpcInterruptRequested = TRUE;

                        /* Set DPC inserted */
                        DpcInserted = TRUE;
                    }
                }
                else
                {
                    /* Check if the DPC is of anything but low importance */
                    if ((Dpc->Importance != LowImportance) ||
                        (DpcData->DpcQueueDepth >=
                         Prcb->MaximumDpcQueueDepth) ||
                        (Prcb->DpcRequestRate < Prcb->MinimumDpcRate))
                    {
                        /* Set interrupt requested */
                        Prcb->DpcInterruptRequested = TRUE;

                        /* Set DPC inserted */
                        DpcInserted = TRUE;
                    }
                }
            }
        }
    }

    /* Release the lock */
    KiReleaseSpinLock(&DpcData->DpcLock);

    /* Check if the DPC was inserted */
    if (DpcInserted)
    {
        /* Check if this was SMP */
        if (Prcb != CurrentPrcb)
        {
            /* It was, request and IPI */
            KiIpiSend(AFFINITY_MASK(Cpu), IPI_DPC);
        }
        else
        {
            /* It wasn't, request an interrupt from HAL */
            HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
        }
    }

    /* Lower IRQL */
    KeLowerIrql(OldIrql);
    return DpcConfigured;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeRemoveQueueDpc(IN PKDPC Dpc)
{
    PKDPC_DATA DpcData;
    BOOLEAN Enable;
    ASSERT_DPC(Dpc);

    /* Disable interrupts */
    Enable = KeDisableInterrupts();

    /* Get DPC data */
    DpcData = Dpc->DpcData;
    if (DpcData)
    {
        /* Acquire the DPC lock */
        KiAcquireSpinLock(&DpcData->DpcLock);

        /* Make sure that the data didn't change */
        if (DpcData == Dpc->DpcData)
        {
            /* Remove the DPC */
            DpcData->DpcQueueDepth--;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
            {
                /* Vista+: singly-linked list removal — find previous entry */
                PSINGLE_LIST_ENTRY Prev = &DpcData->DpcList.ListHead;
                PSINGLE_LIST_ENTRY Cur = (PSINGLE_LIST_ENTRY)&Dpc->DpcListEntry;
                while (Prev->Next && Prev->Next != Cur) Prev = Prev->Next;
                if (Prev->Next == Cur)
                {
                    Prev->Next = Cur->Next;
                    if (DpcData->DpcList.LastEntry == Cur)
                        DpcData->DpcList.LastEntry = (Prev == &DpcData->DpcList.ListHead && !Prev->Next) ? NULL : Prev;
                }
            }
#else
            RemoveEntryList(&Dpc->DpcListEntry);
#endif
            Dpc->DpcData = NULL;
        }

        /* Release the lock */
        KiReleaseSpinLock(&DpcData->DpcLock);
    }

    /* Re-enable interrupts */
    KeRestoreInterrupts(Enable);

    /* Return if the DPC was in the queue or not */
    return DpcData ? TRUE : FALSE;
}

/*
 * @implemented
 */
_IRQL_requires_max_(APC_LEVEL)
VOID
NTAPI
KeFlushQueuedDpcs(VOID)
{
    ULONG ProcessorIndex;
    PKPRCB TargetPrcb;

    PAGED_CODE();
    ASSERT(KeGetCurrentThread()->SystemAffinityActive == FALSE);

    /* Loop all processors */
    for (ProcessorIndex = 0; ProcessorIndex < KeNumberProcessors; ProcessorIndex++)
    {
        /* Get the target processor's PRCB */
        TargetPrcb = KiProcessorBlock[ProcessorIndex];

        /* Check if there are DPCs on either queues */
        if ((TargetPrcb->DpcData[DPC_NORMAL].DpcQueueDepth > 0) ||
            (TargetPrcb->DpcData[DPC_THREADED].DpcQueueDepth > 0))
        {
            /* Check if this is the current processor */
            if (TargetPrcb == KeGetCurrentPrcb())
            {
                /* Request a DPC interrupt */
                HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
            }
            else
            {
                /* Attach to the target processor. This will cause a DPC
                   interrupt on the target processor and flush all DPCs. */
                KeSetSystemAffinityThread(TargetPrcb->SetMember);
            }
        }
    }

    /* Revert back to user affinity */
    if (KeGetCurrentThread()->SystemAffinityActive)
    {
        KeRevertToUserAffinityThread();
    }
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeIsExecutingDpc(VOID)
{
    /* Return if the Dpc Routine is active */
#if defined(_M_ARM64)
    return _KeIsExecutingDpc();
#else
    return KeGetCurrentPrcb()->DpcRoutineActive;
#endif
}

/*
 * @implemented
 */
VOID
NTAPI
KeSetImportanceDpc (IN PKDPC Dpc,
                    IN KDPC_IMPORTANCE Importance)
{
    /* Set the DPC Importance */
    ASSERT_DPC(Dpc);
    Dpc->Importance = Importance;
}

/*
 * @implemented
 */
VOID
NTAPI
KeSetTargetProcessorDpc(IN PKDPC Dpc,
                        IN CCHAR Number)
{
    /* Set a target CPU */
    ASSERT_DPC(Dpc);
    Dpc->Number = Number + MAXIMUM_PROCESSORS;
}

typedef struct _KI_GENERIC_DPC_BARRIER
{
    DEFERRED_REVERSE_BARRIER Reverse;
    LONG PassedCount;
    PLONG Flags;
} KI_GENERIC_DPC_BARRIER, *PKI_GENERIC_DPC_BARRIER;

/*
 * @implemented
 */
VOID
NTAPI
KeGenericCallDpc(IN PKDEFERRED_ROUTINE Routine,
                 IN PVOID Context)
{
    DECLSPEC_CACHEALIGN volatile ULONG Barrier;
    KIRQL OldIrql;
    KI_GENERIC_DPC_BARRIER Sync;
    PKPRCB CurrentPrcb;
    PLONG Flags;
    CCHAR Number;
    ULONG Count, Index;
    ASSERT(KeGetCurrentIrql() < DISPATCH_LEVEL);

    Count = KeNumberProcessors;

    Flags = ExAllocatePoolWithTag(NonPagedPool, Count * sizeof(LONG), 'cpDK');
    if (Flags == NULL)
    {
        Count = 1;
    }
    else
    {
        for (Index = 0; Index < Count; Index++)
            Flags[Index] = 0;
    }

    Barrier = Count;
    Sync.Reverse.Barrier = Count;
    Sync.Reverse.TotalProcessors = Count;
    Sync.PassedCount = 0;
    Sync.Flags = Flags;

    ExAcquireFastMutex(&KiGenericCallDpcMutex);

    KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    CurrentPrcb = KeGetCurrentPrcb();

    if (Flags != NULL)
    {
        for (Number = 0; Number < (CCHAR)Count; Number++)
        {
            PKPRCB Prcb = KiProcessorBlock[(UCHAR)Number];

            if (Prcb == CurrentPrcb) continue;

            KeInitializeDpc(&Prcb->CallDpc, Routine, Context);
            KeSetTargetProcessorDpc(&Prcb->CallDpc, Number);
            KeSetImportanceDpc(&Prcb->CallDpc, HighImportance);
            KeInsertQueueDpc(&Prcb->CallDpc, (PVOID)&Barrier, (PVOID)&Sync);
        }
    }

    Routine(&CurrentPrcb->CallDpc, Context, (PVOID)&Barrier, (PVOID)&Sync);

    while (Barrier != 0)
        YieldProcessor();

    KeLowerIrql(OldIrql);

    ExReleaseFastMutex(&KiGenericCallDpcMutex);

    if (Flags != NULL)
        ExFreePoolWithTag(Flags, 'cpDK');
}

/*
 * @implemented
 */
VOID
NTAPI
KeSignalCallDpcDone(IN PVOID SystemArgument1)
{
    //
    // Decrement the barrier, which is actually the processor count
    //
    InterlockedDecrement((PLONG)SystemArgument1);
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
KeSignalCallDpcSynchronize(IN PVOID SystemArgument2)
{
    PKI_GENERIC_DPC_BARRIER Sync = SystemArgument2;
    ULONG Total, Index;
    LONG Flag, Comp, Done;
    BOOLEAN First;

    Total = Sync->Reverse.TotalProcessors;
    if (Total <= 1)
        return TRUE;

    Index = KeGetCurrentProcessorNumber();

    Sync->Flags[Index] ^= (LONG)0x80000000;
    Flag = Sync->Flags[Index];

    First = (Index == 0);
    Comp = Flag + (LONG)Index;
    Done = Flag + (LONG)Total;

    if (First)
        InterlockedExchange((PLONG)&Sync->Reverse.Barrier, Comp);

    while (InterlockedCompareExchange((PLONG)&Sync->Reverse.Barrier, Comp + 1, Comp) != Done)
        YieldProcessor();

    InterlockedIncrement(&Sync->PassedCount);

    while (First && InterlockedCompareExchange(&Sync->PassedCount, 0, (LONG)Total))
        YieldProcessor();

    return First;
}

/* EOF */
