/*
* PROJECT:         ReactOS Kernel
* LICENSE:         GPL - See COPYING in the top level directory
* FILE:            ntoskrnl/ke/balmgr.c
* PURPOSE:         Balance Set Manager
* PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
*/

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

#define READY_SCAN_PRIORITY_MIN 8
#define READY_SCAN_PRIORITY_MAX 9
#define THREAD_BOOST_PRIORITY 11
#define READY_SCAN_PROCESSORS 8
#define READY_SCAN_CANDIDATES 16
#define READY_SCAN_BOOSTS 1
#define READY_SCAN_WAIT_TICKS 6
ULONG KiReadyScanLast;

/* PRIVATE FUNCTIONS *********************************************************/

static
VOID
KiScanReadyQueuesOnPrcb(IN PKPRCB Prcb,
                        IN ULONG TickNow,
                        IN ULONG CandidateQuota,
                        IN ULONG BoostQuota)
{
    ULONG Index;
    ULONG Summary;
    ULONG CandidatesScanned = 0;
    ULONG ThreadsBoosted = 0;
    PLIST_ENTRY ListHead, NextEntry;
    PKTHREAD Thread;

    KiAcquirePrcbLock(Prcb);
    Index = Prcb->QueueIndex;

    /* Check if there's any thread that need help */
    Summary = Prcb->ReadySummary & (PRIORITY_MASK(READY_SCAN_PRIORITY_MIN) | PRIORITY_MASK(READY_SCAN_PRIORITY_MAX));
    while (Summary &&
           (CandidatesScanned < CandidateQuota) &&
           (ThreadsBoosted < BoostQuota))
    {
        /* Normalize the index */
        if ((Index < READY_SCAN_PRIORITY_MIN) || (Index > READY_SCAN_PRIORITY_MAX)) Index = READY_SCAN_PRIORITY_MIN;

        /* Loop for ready threads */
        if (Summary & PRIORITY_MASK(Index))
        {
            ASSERT(!IsListEmpty(&Prcb->DispatcherReadyListHead[Index]));
            ListHead = &Prcb->DispatcherReadyListHead[Index];
            NextEntry = ListHead->Flink;

            while ((NextEntry != ListHead) &&
                   (CandidatesScanned < CandidateQuota) &&
                   (ThreadsBoosted < BoostQuota))
            {
                Thread = CONTAINING_RECORD(NextEntry,
                                           KTHREAD,
                                           WaitListEntry);
                NextEntry = NextEntry->Flink;
                CandidatesScanned++;

                /* Never wait on ThreadLock while holding the PRCB lock. */
                if (KiTryThreadLock(Thread))
                {
                    continue;
                }

                if ((Thread->State != Ready) ||
                    (Thread->NextProcessor != Prcb->Number) ||
                    (Thread->Priority != Index))
                {
                    KiReleaseThreadLock(Thread);
                    continue;
                }

                if ((ULONG)(TickNow - Thread->WaitTime) >=
                    READY_SCAN_WAIT_TICKS)
                {
                    ASSERT(Prcb->ReadySummary & PRIORITY_MASK(Index));
                    if (RemoveEntryList(&Thread->WaitListEntry))
                    {
                        Prcb->ReadySummary ^= PRIORITY_MASK(Index);
                    }

                    ASSERT((Thread->PriorityDecrement >= 0) &&
                           (Thread->PriorityDecrement <= Thread->Priority));
                    Thread->PriorityDecrement +=
                        (THREAD_BOOST_PRIORITY - Thread->Priority);
                    ASSERT((Thread->PriorityDecrement >= 0) &&
                           (Thread->PriorityDecrement <=
                            THREAD_BOOST_PRIORITY));

                    Thread->Priority = THREAD_BOOST_PRIORITY;
                    KiSetThreadQuantum(Thread, WAIT_QUANTUM_DECREMENT * 4);
                    KiInsertDeferredReadyList(Thread);
                    ThreadsBoosted++;
                }

                KiReleaseThreadLock(Thread);
            }

            if (NextEntry != ListHead)
            {
                Index++;
                break;
            }

            Summary &= ~PRIORITY_MASK(Index);
        }

        Index++;
    }

    Prcb->QueueIndex = Summary ? Index : READY_SCAN_PRIORITY_MIN;
    KiReleasePrcbLock(Prcb);

#if defined(_M_AMD64) || defined(_M_ARM64)
    if (SmpDbgEnabled && ThreadsBoosted)
        SmpDbgBalanceEvent(Prcb->Number, Prcb->Number, SMPDBG_BALANCE_PERIODIC);
#endif
}

VOID
NTAPI
KiScanReadyQueues(IN PKDPC Dpc,
                  IN PVOID DeferredContext,
                  IN PVOID SystemArgument1,
                  IN PVOID SystemArgument2)
{
    PULONG ScanLast = DeferredContext;
    ULONG ScanIndex = *ScanLast;
    ULONG ProcessorCount;
    ULONG CandidateQuota, BoostQuota;
    ULONG Processor;
    ULONG TickNow = KeTickCount.LowPart;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ProcessorCount = min((ULONG)KeNumberProcessors,
                         (ULONG)READY_SCAN_PROCESSORS);
    ASSERT(ProcessorCount != 0);
    CandidateQuota = READY_SCAN_CANDIDATES;
    BoostQuota = READY_SCAN_BOOSTS;
    OldIrql = KeRaiseIrqlToSynchLevel();

    for (Processor = 0; Processor < ProcessorCount; Processor++)
    {
        KiScanReadyQueuesOnPrcb(KiProcessorBlock[ScanIndex],
                                TickNow,
                                CandidateQuota,
                                BoostQuota);

        ScanIndex++;
        if (ScanIndex == KeNumberProcessors) ScanIndex = 0;
    }

    *ScanLast = ScanIndex;
    KiExitDispatcher(OldIrql);
}

VOID
NTAPI
KeBalanceSetManager(IN PVOID Context)
{
    KDPC ScanDpc;
    KTIMER PeriodTimer;
    LARGE_INTEGER DueTime;
    KWAIT_BLOCK WaitBlockArray[1] = {0};
    PVOID WaitObjects[1];
    NTSTATUS Status;

    /* Set us at a low real-time priority level */
    KeSetPriorityThread(KeGetCurrentThread(), LOW_REALTIME_PRIORITY);

    /* Setup the timer and scanner DPC */
    KeInitializeTimerEx(&PeriodTimer, SynchronizationTimer);
    KeInitializeDpc(&ScanDpc, KiScanReadyQueues, &KiReadyScanLast);

    /* Setup the periodic timer */
    DueTime.QuadPart = -1 * 10 * 1000 * 1000;
    KeSetTimerEx(&PeriodTimer, DueTime, 1000, &ScanDpc);

    /* Setup the wait objects */
    WaitObjects[0] = &PeriodTimer;
    //WaitObjects[1] = MmWorkingSetManagerEvent; // NO WS Management Yet!

    /* Start wait loop */
    do
    {
        /* Wait on our objects */
        Status = KeWaitForMultipleObjects(1,
                                          WaitObjects,
                                          WaitAny,
                                          Executive,
                                          KernelMode,
                                          FALSE,
                                          NULL,
                                          WaitBlockArray);
        switch (Status)
        {
            /* Check if our timer expired */
            case STATUS_WAIT_0:

                /* Adjust lookaside lists */
                //ExAdjustLookasideDepth();

                /* Call the working set manager */
                //MmWorkingSetManager();

                /* FIXME: Outswap stacks */

                /* Done */
                break;

            /* Check if the working set manager notified us */
            case STATUS_WAIT_1:

                /* Call the working set manager */
                //MmWorkingSetManager();
                break;

            /* Anything else */
            default:
                DPRINT1("BALMGR: Illegal wait status, %lx =\n", Status);
                break;
        }
    } while (TRUE);
}
