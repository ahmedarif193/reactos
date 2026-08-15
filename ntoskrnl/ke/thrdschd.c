/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ke/thrdschd.c
 * PURPOSE:         Kernel Thread Scheduler (Affinity, Priority, Scheduling)
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

KAFFINITY KiIdleSummary;
KAFFINITY KiIdleSMTSummary;

#ifdef CONFIG_SMP
#define KI_BALANCE_QUEUE_LIMIT 64

C_ASSERT(KiBalanceWakePlacement == SMPDBG_BALANCE_WAKE_PLACEMENT);
C_ASSERT(KiBalanceIdle == SMPDBG_BALANCE_IDLE);
C_ASSERT(KiBalanceQuantum == SMPDBG_BALANCE_QUANTUM);

typedef struct DECLSPEC_CACHEALIGN _KI_BALANCE_STATE
{
    volatile LONG SourceCursor;
    volatile LONG IdleTick;
} KI_BALANCE_STATE;

static KI_BALANCE_STATE KiBalanceState[MAXIMUM_PROCESSORS];
#endif

/* FUNCTIONS *****************************************************************/

_Requires_lock_held_(Prcb->PrcbLock)
PKTHREAD
FASTCALL
KiIdleSchedule(IN PKPRCB Prcb)
{
    PKTHREAD Thread;

    Thread = KiSelectReadyThread(0, Prcb);
    if (Thread != NULL)
        return Thread;

    if (KiIsProcessorParked(Prcb))
        return Prcb->IdleThread;

#ifdef CONFIG_SMP
    KiReleasePrcbLock(Prcb);
    KiBalanceReadyQueues(Prcb, KiBalanceIdle);
    KiAcquirePrcbLock(Prcb);

    if (Prcb->NextThread != NULL)
        return Prcb->IdleThread;

    Thread = KiSelectReadyThread(0, Prcb);
    if (Thread != NULL)
        return Thread;
#endif

    return Prcb->IdleThread;
}

VOID
FASTCALL
KiProcessDeferredReadyList(IN PKPRCB Prcb)
{
    PSINGLE_LIST_ENTRY ListEntry;
    PKTHREAD Thread;

    /* Make sure there is something on the ready list */
    ASSERT(Prcb->DeferredReadyListHead.Next != NULL);

    /* Get the first entry and clear the list */
    ListEntry = Prcb->DeferredReadyListHead.Next;
    Prcb->DeferredReadyListHead.Next = NULL;

    /* Start processing loop */
    do
    {
        /* Get the thread and advance to the next entry */
        Thread = CONTAINING_RECORD(ListEntry, KTHREAD, SwapListEntry);
        ListEntry = ListEntry->Next;

        /* Make the thread ready */
        KiDeferredReadyThread(Thread);
    } while (ListEntry != NULL);

    /* Make sure the ready list is still empty */
    ASSERT(Prcb->DeferredReadyListHead.Next == NULL);
}

VOID
FASTCALL
KiQueueReadyThread(IN PKTHREAD Thread,
                   IN PKPRCB Prcb)
{
    /* Call the macro. We keep the API for compatibility with ASM code */
    KxQueueReadyThread(Thread, Prcb);
}

#ifdef CONFIG_SMP
ULONG
NTAPI
KiFindIdealProcessor(
    _In_ KAFFINITY ProcessorSet,
    _In_ UCHAR OriginalIdealProcessor)
{
    PKPRCB OriginalIdealPrcb;
    KAFFINITY NodeMask, NonParkedSet;
    ULONG Processor;

    NonParkedSet = KiGetNonParkedProcessorSet();
    if (ProcessorSet & NonParkedSet)
        ProcessorSet &= NonParkedSet;

    /* Check if we can use the original ideal processor */
    if (ProcessorSet & AFFINITY_MASK(OriginalIdealProcessor))
    {
        /* We can, so use it */
        return OriginalIdealProcessor;
    }

    /* Only use active processors */
    ProcessorSet &= KeActiveProcessors;

    /* Get the original ideal PRCB */
    OriginalIdealPrcb = KiProcessorBlock[OriginalIdealProcessor];

    /* Check if we can use the original node */
    NodeMask = OriginalIdealPrcb->ParentNode->ProcessorMask & ProcessorSet;
    if (NodeMask)
    {
        /* Use the node set instead */
        ProcessorSet = NodeMask;
    }

    /* Calculate the ideal CPU from the affinity set */
    BitScanReverseAffinity(&Processor, ProcessorSet);
    return Processor;
}

static
BOOLEAN
KiTryClaimIdleProcessor(
    _In_ ULONG Processor)
{
    return InterlockedBitTestAndResetAffinity(&KiIdleSummary, Processor);
}

#define KI_BUSY_LOAD_LIMIT 8

static
ULONG
KiCountReadyThreadsLocked(
    _In_ PKPRCB Prcb,
    _In_ ULONG Limit)
{
    PLIST_ENTRY ListHead, Entry;
    ULONG Priority, Count = 0;

    for (Priority = 0; (Priority <= HIGH_PRIORITY) && (Count < Limit); Priority++)
    {
        if (!(Prcb->ReadySummary & PRIORITY_MASK(Priority)))
            continue;

        ListHead = &Prcb->DispatcherReadyListHead[Priority];
        for (Entry = ListHead->Flink; (Entry != ListHead) && (Count < Limit); Entry = Entry->Flink)
            Count++;
    }

    return Count;
}

static
VOID
KiQueryProcessorDispatchState(
    _In_ PKPRCB Prcb,
    _Out_ KPRIORITY *DispatchPriority,
    _Out_ PULONG RunnableLoad)
{
    PKTHREAD CurrentThread, NextThread;
    ULONG ReadySummary, ReadyPriority;
    KPRIORITY Priority;
    ULONG Load;

    KiAcquirePrcbLock(Prcb);

    CurrentThread = Prcb->CurrentThread;
    NextThread = Prcb->NextThread;
    ReadySummary = Prcb->ReadySummary;
    Priority = 0;
    Load = 0;

    if ((CurrentThread != NULL) && (CurrentThread != Prcb->IdleThread))
    {
        Priority = CurrentThread->Priority;
        Load++;
    }

    if (NextThread != NULL)
    {
        if (NextThread->Priority > Priority)
            Priority = NextThread->Priority;
        Load++;
    }

    if (ReadySummary != 0)
    {
        BitScanReverse(&ReadyPriority, ReadySummary);
        if ((KPRIORITY)ReadyPriority > Priority)
            Priority = (KPRIORITY)ReadyPriority;

        Load += KiCountReadyThreadsLocked(Prcb, KI_BUSY_LOAD_LIMIT - Load);
    }

    KiReleasePrcbLock(Prcb);

    *DispatchPriority = Priority;
    *RunnableLoad = Load;
}

static
PKTHREAD
KiFindStealableThreadLocked(
    _In_ PKPRCB Source,
    _In_ ULONG TargetCpu,
    _In_ KPRIORITY Priority,
    _Inout_ PULONG Scanned)
{
    PLIST_ENTRY ListHead, Entry;
    PKTHREAD Thread;

    ASSERT(Source->ReadySummary & PRIORITY_MASK(Priority));
    ListHead = &Source->DispatcherReadyListHead[Priority];
    for (Entry = ListHead->Flink;
         (Entry != ListHead) && (*Scanned < KI_BALANCE_QUEUE_LIMIT);
         Entry = Entry->Flink)
    {
        (*Scanned)++;
        Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);
        if (!(KiThreadAffinityMask(Thread) & AFFINITY_MASK(TargetCpu)))
            continue;
        if (KiTryThreadLock(Thread))
            continue;
        if ((Thread->State == Ready) &&
            !Thread->ProcessReadyQueue &&
            (Thread->NextProcessor == Source->Number) &&
            (Thread->Priority == Priority) &&
            (KiThreadAffinityMask(Thread) & AFFINITY_MASK(TargetCpu)))
        {
            return Thread;
        }
        KiReleaseThreadLock(Thread);
    }

    return NULL;
}

static
ULONG
KiFindBestStandbySource(
    _In_ ULONG TargetCpu,
    _In_ LONG MinimumPriority,
    _In_ ULONG Start,
    _In_ ULONG ProcessorCount,
    _Out_ PLONG StandbyPriority)
{
    PKPRCB Source;
    PKTHREAD CurrentThread, Thread;
    ULONG BestCpu, Offset, SourceCpu;
    LONG BestPriority;

    BestCpu = MAXULONG;
    BestPriority = MinimumPriority - 1;

    for (Offset = 0; Offset < ProcessorCount; Offset++)
    {
        SourceCpu = (Start + Offset) % ProcessorCount;
        if ((SourceCpu == TargetCpu) ||
            !(KeActiveProcessors & AFFINITY_MASK(SourceCpu)))
        {
            continue;
        }

        Source = KiProcessorBlock[SourceCpu];
        if (Source == NULL)
            continue;

        KiAcquirePrcbLock(Source);
        CurrentThread = Source->CurrentThread;
        Thread = Source->NextThread;
        if ((CurrentThread != NULL) &&
            (CurrentThread != Source->IdleThread) &&
            (Thread != NULL) &&
            (Thread != Source->IdleThread) &&
            (Thread->State == Standby) &&
            (Thread->NextProcessor == SourceCpu) &&
            (Thread->Priority >= MinimumPriority) &&
            (KiThreadAffinityMask(Thread) & AFFINITY_MASK(TargetCpu)) &&
            (Thread->Priority > BestPriority))
        {
            BestCpu = SourceCpu;
            BestPriority = Thread->Priority;
        }
        KiReleasePrcbLock(Source);
    }

    *StandbyPriority = BestPriority;
    return BestCpu;
}

static
PKTHREAD
KiTryStealStandbyThreadLocked(
    _In_ PKPRCB Source,
    _In_ ULONG TargetCpu,
    _In_ KPRIORITY ExpectedPriority,
    _Out_ PBOOLEAN Preempted,
    _Out_ PKTHREAD *ReplacementThread)
{
    PKTHREAD CurrentThread, Replacement, Thread;

    *ReplacementThread = NULL;
    CurrentThread = Source->CurrentThread;
    Thread = Source->NextThread;
    if ((CurrentThread == NULL) ||
        (CurrentThread == Source->IdleThread) ||
        (Thread == NULL) ||
        (Thread == Source->IdleThread) ||
        (Thread->State != Standby) ||
        (Thread->NextProcessor != Source->Number) ||
        (Thread->Priority != ExpectedPriority) ||
        !(KiThreadAffinityMask(Thread) & AFFINITY_MASK(TargetCpu)) ||
        KiTryThreadLock(Thread))
    {
        return NULL;
    }

    if ((Source->NextThread != Thread) ||
        (Thread->State != Standby) ||
        (Thread->NextProcessor != Source->Number) ||
        (Thread->Priority != ExpectedPriority) ||
        !(KiThreadAffinityMask(Thread) & AFFINITY_MASK(TargetCpu)))
    {
        KiReleaseThreadLock(Thread);
        return NULL;
    }

    *Preempted = Thread->Preempted;
    Thread->Preempted = FALSE;
    Source->NextThread = NULL;
    Thread->State = DeferredReady;
    Thread->NextProcessor = TargetCpu;

    Replacement = NULL;
    if (CurrentThread->Priority < HIGH_PRIORITY)
        Replacement = KiSelectReadyThread(CurrentThread->Priority + 1, Source);

    if (Replacement != NULL)
    {
        Replacement->State = Standby;
        Source->NextThread = Replacement;
        *ReplacementThread = Replacement;
        if (CurrentThread->State == Running)
            CurrentThread->Preempted = TRUE;
    }
    else
    {
        CurrentThread->Preempted = FALSE;
    }

    return Thread;
}

_Requires_lock_not_held_(Target->PrcbLock)
BOOLEAN
FASTCALL
KiBalanceReadyQueues(
    _In_ PKPRCB Target,
    _In_ KI_BALANCE_REASON Reason)
{
    PKPRCB Source;
    PKTHREAD CurrentThread, DisplacedThread, SourceReplacement, Thread;
    ULONG IpiCause, LocalPriority, ProcessorCount, Scanned;
    ULONG StandbyCpu, Start, Offset, SourceCpu, TargetCpu, TargetTick;
    LONG MinimumPriority, Priority, StandbyPriority;
    BOOLEAN Preempted, RequestIpi, RequestSourceIpi, StandbySteal, TargetIdle;

    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);
    ASSERT((Reason >= KiBalanceIdle) && (Reason <= KiBalanceQuantum));

    TargetCpu = Target->Number;
    if ((TargetCpu >= (ULONG)KeNumberProcessors) ||
        !(KeActiveProcessors & Target->SetMember) ||
        KiIsProcessorParked(Target))
    {
        return FALSE;
    }

    if (Reason == KiBalanceIdle)
    {
        LONG PreviousTick;

        TargetTick = KeTickCount.LowPart;
        PreviousTick = InterlockedExchange(&KiBalanceState[TargetCpu].IdleTick, TargetTick);
        if ((PreviousTick != 0) && ((ULONG)PreviousTick == TargetTick))
            return FALSE;
    }

    KiAcquirePrcbLock(Target);
    CurrentThread = Target->CurrentThread;
    TargetIdle = (CurrentThread == Target->IdleThread);
    if ((Target->NextThread != NULL) ||
        ((Reason == KiBalanceIdle) &&
         (!TargetIdle || (Target->ReadySummary != 0))))
    {
        KiReleasePrcbLock(Target);
        return FALSE;
    }

    MinimumPriority = (CurrentThread != NULL) ? CurrentThread->Priority : 0;
    if ((Reason == KiBalanceQuantum) && (Target->ReadySummary != 0))
    {
        BitScanReverse(&LocalPriority, Target->ReadySummary);
        if ((LONG)LocalPriority >= MinimumPriority)
            MinimumPriority = (LONG)LocalPriority + 1;
    }
    KiReleasePrcbLock(Target);

    ProcessorCount = (ULONG)KeNumberProcessors;
    Start = (ULONG)InterlockedIncrement(&KiBalanceState[TargetCpu].SourceCursor) % ProcessorCount;
    Scanned = 0;
    Thread = NULL;
    Source = NULL;
    SourceReplacement = NULL;
    RequestSourceIpi = FALSE;
    StandbySteal = FALSE;
    StandbyCpu = KiFindBestStandbySource(TargetCpu, MinimumPriority, Start, ProcessorCount, &StandbyPriority);

    for (Priority = HIGH_PRIORITY;
         (Priority >= MinimumPriority) && (Scanned < KI_BALANCE_QUEUE_LIMIT);
         Priority--)
    {
        if ((StandbyCpu != MAXULONG) && (Priority == StandbyPriority))
        {
            SourceCpu = StandbyCpu;
            Source = KiProcessorBlock[SourceCpu];
            KiAcquirePrcbLock(Source);
            Thread = KiTryStealStandbyThreadLocked(Source, TargetCpu, (KPRIORITY)Priority, &Preempted, &SourceReplacement);
            if (Thread != NULL)
            {
                StandbySteal = TRUE;
                RequestSourceIpi = (SourceReplacement != NULL) && (Source != KeGetCurrentPrcb());
                KiReleasePrcbLock(Source);
                goto ThreadFound;
            }
            KiReleasePrcbLock(Source);
            StandbyCpu = MAXULONG;
        }

        for (Offset = 0; Offset < ProcessorCount; Offset++)
        {
            SourceCpu = (Start + Offset) % ProcessorCount;
            if (SourceCpu == TargetCpu)
                continue;
            if (!(KeActiveProcessors & AFFINITY_MASK(SourceCpu)))
                continue;

            Source = KiProcessorBlock[SourceCpu];
            if ((Source == NULL) ||
                !(Source->ReadySummary & PRIORITY_MASK(Priority)))
            {
                continue;
            }

            KiAcquirePrcbLock(Source);
            if (!(Source->ReadySummary & PRIORITY_MASK(Priority)))
            {
                KiReleasePrcbLock(Source);
                continue;
            }

            Thread = KiFindStealableThreadLocked(Source, TargetCpu, (KPRIORITY)Priority, &Scanned);
            if (Thread == NULL)
            {
                KiReleasePrcbLock(Source);
                continue;
            }

            ASSERT(Source->ReadySummary & PRIORITY_MASK(Thread->Priority));
            if (RemoveEntryList(&Thread->WaitListEntry))
                Source->ReadySummary &= ~PRIORITY_MASK(Thread->Priority);

            Preempted = Thread->Preempted;
            Thread->Preempted = FALSE;
            Thread->State = DeferredReady;
            Thread->NextProcessor = TargetCpu;
            KiReleasePrcbLock(Source);
            goto ThreadFound;
        }
    }

    return FALSE;

ThreadFound:
    DisplacedThread = NULL;
    IpiCause = SMPDBG_SCHED_IDLE_REQUEST;
    RequestIpi = FALSE;
    KiAcquirePrcbLock(Target);
    CurrentThread = Target->CurrentThread;
    if ((Target->NextThread != NULL) &&
        (Thread->Priority > Target->NextThread->Priority))
    {
        DisplacedThread = Target->NextThread;
        DisplacedThread->Preempted = TRUE;
        DisplacedThread->State = DeferredReady;
        Thread->State = Standby;
        Target->NextThread = Thread;
        IpiCause = SMPDBG_SCHED_REPLACE_STANDBY;
        RequestIpi = (Target != KeGetCurrentPrcb());
    }
    else if ((Target->NextThread == NULL) &&
             (CurrentThread != NULL) &&
             (Thread->Priority > CurrentThread->Priority))
    {
        if (CurrentThread->State == Running)
            CurrentThread->Preempted = TRUE;
        Thread->State = Standby;
        Target->NextThread = Thread;
        IpiCause = SMPDBG_SCHED_PREEMPT_CURRENT;
        RequestIpi = (Target != KeGetCurrentPrcb());
    }
    else
    {
        Thread->State = Ready;
        if (Preempted)
            InsertHeadList(&Target->DispatcherReadyListHead[Thread->Priority], &Thread->WaitListEntry);
        else
            InsertTailList(&Target->DispatcherReadyListHead[Thread->Priority], &Thread->WaitListEntry);
        Target->ReadySummary |= PRIORITY_MASK(Thread->Priority);
    }

    if (CurrentThread == Target->IdleThread)
    {
        InterlockedBitTestAndResetAffinity(&KiIdleSummary, TargetCpu);
        IpiCause = SMPDBG_SCHED_IDLE_REQUEST;
        RequestIpi = (Target != KeGetCurrentPrcb());
    }
    KiReleasePrcbLock(Target);

#if defined(_M_AMD64) || defined(_M_ARM64)
    if (SmpDbgEnabled)
    {
        SmpDbgBalanceEvent(TargetCpu, SourceCpu, Reason);
        if (StandbySteal)
            SmpDbgStandbySteal(TargetCpu);
    }
#endif
    KiReleaseThreadLock(Thread);

    if (RequestSourceIpi)
    {
#if defined(_M_AMD64) || defined(_M_ARM64)
        if (SmpDbgEnabled)
            SmpDbgSchedulerIpi(SourceCpu, SourceReplacement, SMPDBG_SCHED_STANDBY_REPAIR);
#endif
        KiIpiSend(Source->SetMember, IPI_DPC);
    }

    if (RequestIpi)
    {
#if defined(_M_AMD64) || defined(_M_ARM64)
        if (SmpDbgEnabled)
            SmpDbgSchedulerIpi(TargetCpu, Thread, IpiCause);
#endif
        KiIpiSend(Target->SetMember, IPI_DPC);
    }

    if (DisplacedThread != NULL)
        KiDeferredReadyThread(DisplacedThread);

#if !defined(_M_AMD64) && !defined(_M_ARM64)
    DBG_UNREFERENCED_LOCAL_VARIABLE(IpiCause);
    DBG_UNREFERENCED_LOCAL_VARIABLE(StandbySteal);
#endif

    return TRUE;
}

static
ULONG
KiSelectNextProcessor(
    _In_ PKTHREAD Thread,
    _Out_ PKAFFINITY IdleRequest)
{
    KAFFINITY PreferredSet, IdleSet, NonParkedSet;
    ULONG Processor, ProcessorCount, StartProcessor, Offset;
    ULONG BestProcessor, RunnableLoad, BestLoad;
    KPRIORITY DispatchPriority, BestPriority;
    PKPRCB Prcb;

    /* Start with the affinity */
    PreferredSet = KiThreadAffinityMask(Thread) & KeActiveProcessors;
    ASSERT(PreferredSet != 0);
    NonParkedSet = KiGetNonParkedProcessorSet();
    if (PreferredSet & NonParkedSet)
        PreferredSet &= NonParkedSet;
    *IdleRequest = 0;

#ifdef _M_ARM64
    /* Favor an unloaded local processor without self-placing a running waiter. */
    Processor = KeGetCurrentProcessorNumber();
    Prcb = KeGetCurrentPrcb();
    if ((Thread != Prcb->CurrentThread) &&
        (PreferredSet & AFFINITY_MASK(Processor)) &&
        (Prcb->NextThread == NULL) &&
        (Prcb->ReadySummary == 0) &&
        (Prcb->CurrentThread != NULL) &&
        (Thread->BasePriority >= Prcb->CurrentThread->BasePriority))
    {
        return Processor;
    }
#endif

    /* Claim an allowed idle processor atomically. */
    for (;;)
    {
        IdleSet = PreferredSet & KiIdleSummary;
        if (IdleSet == 0)
            break;

#ifdef _M_ARM64
        if (Thread->IdealProcessor < (ULONG)KeNumberProcessors &&
            KiProcessorBlock[Thread->IdealProcessor] != NULL)
        {
            PKSCHEDULER_SUBNODE SubNode =
                (PKSCHEDULER_SUBNODE)KiProcessorBlock[Thread->IdealProcessor]->SchedulerSubNode;
            if (SubNode != NULL)
            {
                KAFFINITY SubIdle = IdleSet & SubNode->IdleCpuSet;
                if (SubIdle != 0)
                {
                    IdleSet = SubIdle;
                }
                {
                    KAFFINITY SmtIdle = IdleSet & SubNode->IdleCpuSet & SubNode->IdleSmtSet;
                    if (SmtIdle != 0)
                    {
                        IdleSet = SmtIdle;
                    }
                }
            }
        }
#endif

        if ((Thread->IdealProcessor < (ULONG)KeNumberProcessors) &&
            (IdleSet & AFFINITY_MASK(Thread->IdealProcessor)))
        {
            Processor = Thread->IdealProcessor;
        }
        else
        {
            NT_VERIFY(BitScanForwardAffinity(&Processor, IdleSet) != FALSE);
        }

        if (KiTryClaimIdleProcessor(Processor))
        {
            *IdleRequest = AFFINITY_MASK(Processor);
            return Processor;
        }
    }

    BestProcessor = MAXULONG;
    BestPriority = HIGH_PRIORITY + 1;
    BestLoad = MAXULONG;
    ProcessorCount = (ULONG)KeNumberProcessors;
    if ((Thread->IdealProcessor < ProcessorCount) &&
        (PreferredSet & AFFINITY_MASK(Thread->IdealProcessor)))
    {
        StartProcessor = Thread->IdealProcessor;
    }
    else
    {
        NT_VERIFY(BitScanForwardAffinity(&StartProcessor, PreferredSet) != FALSE);
    }

    for (Offset = 0; Offset < ProcessorCount; Offset++)
    {
        Processor = (StartProcessor + Offset) % ProcessorCount;
        if (!(PreferredSet & AFFINITY_MASK(Processor)))
            continue;

        Prcb = KiProcessorBlock[Processor];
        if (Prcb == NULL)
            continue;

        KiQueryProcessorDispatchState(Prcb, &DispatchPriority, &RunnableLoad);
        if ((DispatchPriority < BestPriority) ||
            ((DispatchPriority == BestPriority) && (RunnableLoad < BestLoad)) ||
            ((DispatchPriority == BestPriority) && (RunnableLoad == BestLoad) &&
             (Processor == Thread->IdealProcessor)))
        {
            BestProcessor = Processor;
            BestPriority = DispatchPriority;
            BestLoad = RunnableLoad;
        }
    }

    ASSERT(BestProcessor < (ULONG)KeNumberProcessors);
#if defined(_M_AMD64) || defined(_M_ARM64)
    if (SmpDbgEnabled)
        SmpDbgBalanceEvent(BestProcessor, Thread->NextProcessor, KiBalanceWakePlacement);
#endif
    return BestProcessor;
}
#else
#define KiSelectNextProcessor(Thread, IdleRequest) (*(IdleRequest) = 0, 0)
#endif

VOID
FASTCALL
KiDeferredReadyThread(IN PKTHREAD Thread)
{
    PKPRCB Prcb;
    BOOLEAN Preempted;
    KAFFINITY IdleRequest;
    ULONG Processor;
    KPRIORITY OldPriority;
    PKTHREAD NextThread;

    /* Sanity checks */
    ASSERT(Thread->State == DeferredReady);
    ASSERT((Thread->Priority >= 0) && (Thread->Priority <= HIGH_PRIORITY));

#if defined(CONFIG_SMP) && defined(_M_AMD64) && \
    (NTDDI_VERSION >= NTDDI_WIN7)
    /* Publish a migrating thread only after its old stack is detached. */
    KiAcquireThreadLock(Thread);
    if (Thread->Running)
    {
        Thread->ReadyTransition = TRUE;
        KiReleaseThreadLock(Thread);
        return;
    }
    KiReleaseThreadLock(Thread);
#endif

    /* Check if we have any adjusts to do */
    if (Thread->AdjustReason == AdjustBoost)
    {
        /* Lock the thread */
        KiAcquireThreadLock(Thread);

        /* Check if the priority is low enough to qualify for boosting */
        if ((Thread->Priority <= Thread->AdjustIncrement) &&
            (Thread->Priority < (LOW_REALTIME_PRIORITY - 3)) &&
            !(Thread->DisableBoost))
        {
            /* Calculate the new priority based on the adjust increment */
            OldPriority = min(Thread->AdjustIncrement + 1,
                              LOW_REALTIME_PRIORITY - 3);

            /* Make sure we're not decreasing outside of the priority range */
            ASSERT((Thread->PriorityDecrement >= 0) &&
                   (Thread->PriorityDecrement <= Thread->Priority));

            /* Calculate the new priority decrement based on the boost */
            Thread->PriorityDecrement += ((SCHAR)OldPriority - Thread->Priority);

            /* Again verify that this decrement is valid */
            ASSERT((Thread->PriorityDecrement >= 0) &&
                   (Thread->PriorityDecrement <= OldPriority));

            /* Set the new priority */
            Thread->Priority = (SCHAR)OldPriority;
        }

        /* We need 4 quanta, make sure we have them, then decrease by one */
        if (KiGetThreadQuantum(Thread) < 4) KiSetThreadQuantum(Thread, 4);
        KiDecrementThreadQuantum(Thread, 1);

        /* Make sure the priority is still valid */
        ASSERT((Thread->Priority >= 0) && (Thread->Priority <= HIGH_PRIORITY));

        /* Release the lock and clear the adjust reason */
        KiReleaseThreadLock(Thread);
        Thread->AdjustReason = AdjustNone;
    }
    else if (Thread->AdjustReason == AdjustUnwait)
    {
        /* Acquire the thread lock and check if this is a real-time thread */
        KiAcquireThreadLock(Thread);
        if (Thread->Priority < LOW_REALTIME_PRIORITY)
        {
            /* It's not real time, but is it time critical? */
            if (Thread->BasePriority >= (LOW_REALTIME_PRIORITY - 2))
            {
                /* It is, so simply reset its quantum */
                KiSetThreadQuantum(Thread, Thread->QuantumReset);
            }
            else
            {
                /* Has the priority been adjusted previously? */
                if (!(Thread->PriorityDecrement) && (Thread->AdjustIncrement))
                {
                    /* Yes, reset its quantum */
                    KiSetThreadQuantum(Thread, Thread->QuantumReset);
                }

                /* Wait code already handles quantum adjustment during APCs */
                if (Thread->WaitStatus != STATUS_KERNEL_APC)
                {
                    /* Decrease the quantum by one and check if we're out */
                    if (KiDecrementThreadQuantum(Thread, 1))
                    {
                        /* We are, reset the quantum and get a new priority */
                        KiSetThreadQuantum(Thread, Thread->QuantumReset);
                        Thread->Priority = KiComputeNewPriority(Thread, 1);
                    }
                }
            }

            /* Now check if we have no decrement and boosts are enabled */
            if (!(Thread->PriorityDecrement) && !(Thread->DisableBoost))
            {
                /* Make sure we have an increment */
                ASSERT(Thread->AdjustIncrement >= 0);

                /* Calculate the new priority after the increment */
                OldPriority = Thread->BasePriority + Thread->AdjustIncrement;

                /* Check if this is a foreground process */
                if (CONTAINING_RECORD(Thread->ApcState.Process, EPROCESS, Pcb)->
                    Vm.Flags.MemoryPriority == MEMORY_PRIORITY_FOREGROUND)
                {
                    /* Apply the foreground boost */
                    OldPriority += PsPrioritySeparation;
                }

                /* Check if this new priority is higher */
                if (OldPriority > Thread->Priority)
                {
                    /* Make sure we don't go into the real time range */
                    if (OldPriority >= LOW_REALTIME_PRIORITY)
                    {
                        /* Normalize it back down one notch */
                        OldPriority = LOW_REALTIME_PRIORITY - 1;
                    }

                    /* Check if the priority is higher then the boosted base */
                    if (OldPriority > (Thread->BasePriority +
                                       Thread->AdjustIncrement))
                    {
                        /* Setup a priority decrement to nullify the boost  */
                        Thread->PriorityDecrement = ((SCHAR)OldPriority -
                                                    Thread->BasePriority -
                                                    Thread->AdjustIncrement);
                    }

                    /* Make sure that the priority decrement is valid */
                    ASSERT((Thread->PriorityDecrement >= 0) &&
                           (Thread->PriorityDecrement <= OldPriority));

                    /* Set this new priority */
                    Thread->Priority = (SCHAR)OldPriority;
                }
            }
        }
        else
        {
            /* It's a real-time thread, so just reset its quantum */
            KiSetThreadQuantum(Thread, Thread->QuantumReset);
        }

        /* Make sure the priority makes sense */
        ASSERT((Thread->Priority >= 0) && (Thread->Priority <= HIGH_PRIORITY));

        /* Release the thread lock and reset the adjust reason */
        KiReleaseThreadLock(Thread);
        Thread->AdjustReason = AdjustNone;
    }

    /* Serialize the final priority and affinity snapshot through publication. */
    KiAcquireThreadLock(Thread);

    /* Clear thread preemption status and save current values */
    Preempted = Thread->Preempted;
    OldPriority = Thread->Priority;
    Thread->Preempted = FALSE;

    /* Select a processor to run on */
    Processor = KiSelectNextProcessor(Thread, &IdleRequest);
    Thread->NextProcessor = Processor;

    /* Get the PRCB and lock it */
    Prcb = KiProcessorBlock[Processor];
    KiAcquirePrcbLock(Prcb);

#ifndef CONFIG_SMP
    /* Check if we have an idle summary */
    if (KiIdleSummary)
    {
        /* Clear it and set this thread as the next one */
        KiIdleSummary = 0;
        Thread->State = Standby;
        Prcb->NextThread = Thread;

        /* Unlock the PRCB and return */
        KiReleasePrcbLock(Prcb);
        KiReleaseThreadLock(Thread);
        return;
    }
#endif // !CONFIG_SMP

    /* Get the next scheduled thread */
    NextThread = Prcb->NextThread;
    if (NextThread)
    {
        /* Sanity check */
        ASSERT(NextThread->State == Standby);

        /* Check if priority changed */
        if (OldPriority > NextThread->Priority)
        {
            /* Preempt the thread */
            NextThread->Preempted = TRUE;

            /* Put this one as the next one */
            Thread->State = Standby;
            Prcb->NextThread = Thread;

            /* Set it in deferred ready mode */
            NextThread->State = DeferredReady;
            KiReleasePrcbLock(Prcb);
            KiReleaseThreadLock(Thread);

#ifdef CONFIG_SMP
            if (Prcb != KeGetCurrentPrcb())
            {
#if defined(_M_AMD64) || defined(_M_ARM64)
                if (SmpDbgEnabled)
                    SmpDbgSchedulerIpi(Prcb->Number, Thread, SMPDBG_SCHED_REPLACE_STANDBY);
#endif
                KiIpiSend(Prcb->SetMember, IPI_DPC);
            }
#endif

            KiDeferredReadyThread(NextThread);
            return;
        }
    }
    else
    {
        /* Set the next thread as the current thread */
        NextThread = Prcb->CurrentThread;
        if (OldPriority > NextThread->Priority)
        {
            /* Preempt it if it's already running */
            if (NextThread->State == Running) NextThread->Preempted = TRUE;

            /* Set the thread on standby and as the next thread */
            Thread->State = Standby;
            Prcb->NextThread = Thread;

            /* Release the lock */
            KiReleasePrcbLock(Prcb);
            KiReleaseThreadLock(Thread);

            /* Check if we're running on another CPU */
            if (KeGetCurrentProcessorNumber() != Thread->NextProcessor)
            {
                /* We are, send an IPI */
#if defined(_M_AMD64) || defined(_M_ARM64)
                if (SmpDbgEnabled)
                    SmpDbgSchedulerIpi(Thread->NextProcessor, Thread, SMPDBG_SCHED_PREEMPT_CURRENT);
#endif
                KiIpiSend(AFFINITY_MASK(Thread->NextProcessor), IPI_DPC);
            }
            return;
        }
    }

    /* Sanity check */
    ASSERT((OldPriority >= 0) && (OldPriority <= HIGH_PRIORITY));

    /* Set this thread as ready */
    Thread->State = Ready;
    Thread->WaitTime = KeTickCount.LowPart;

    /* Insert this thread in the appropriate order */
    Preempted ? InsertHeadList(&Prcb->DispatcherReadyListHead[OldPriority],
                               &Thread->WaitListEntry) :
                InsertTailList(&Prcb->DispatcherReadyListHead[OldPriority],
                               &Thread->WaitListEntry);

    /* Update the ready summary */
    Prcb->ReadySummary |= PRIORITY_MASK(OldPriority);

    /* Sanity check */
    ASSERT(OldPriority == Thread->Priority);

    /* Release the lock */
    KiReleasePrcbLock(Prcb);
    KiReleaseThreadLock(Thread);

#ifdef CONFIG_SMP
    if (IdleRequest != 0)
    {
#if defined(_M_AMD64) || defined(_M_ARM64)
        if (SmpDbgEnabled)
            SmpDbgSchedulerIpi(Processor, Thread, SMPDBG_SCHED_IDLE_REQUEST);
#endif
        KiIpiSend(IdleRequest, IPI_DPC);
    }
#endif
}

PKTHREAD
FASTCALL
KiSelectNextThread(IN PKPRCB Prcb)
{
    PKTHREAD Thread;

    /* Select a ready thread */
    Thread = KiSelectReadyThread(0, Prcb);
    if (!Thread)
    {
        /* Didn't find any, get the current idle thread */
        Thread = Prcb->IdleThread;

        /* Enable idle scheduling */
        InterlockedBitTestAndSetAffinity(&KiIdleSummary, Prcb->Number);
        Prcb->IdleSchedule = TRUE;

        /* FIXME: SMT support */
        //ASSERTMSG("SMP: Not yet implemented\n", FALSE);
    }

    /* Sanity checks and return the thread */
    ASSERT(Thread != NULL);
    //ASSERT((Thread->BasePriority == 0) || (Thread->Priority != 0));
    return Thread;
}

LONG_PTR
FASTCALL
KiSwapThread(IN PKTHREAD CurrentThread,
             IN PKPRCB Prcb,
             IN BOOLEAN NormalWait)
{
    BOOLEAN ApcState = FALSE;
    BOOLEAN SelfPlaced = FALSE;
    KIRQL WaitIrql;
    LONG_PTR WaitStatus;
    PKTHREAD NextThread;
    ASSERT(KeGetCurrentIrql() >= DISPATCH_LEVEL);

    /* Acquire the PRCB lock */
    KiAcquirePrcbLock(Prcb);

#if defined(_M_ARM64)
    _disable();
#endif

    /* A waker can re-ready this thread before its wait switches it out. */
    NextThread = Prcb->NextThread;
    if (NextThread)
    {
        Prcb->NextThread = NULL;
        if (NextThread == CurrentThread)
        {
            SelfPlaced = TRUE;
            NextThread = NULL;
        }
    }

    if (!NextThread)
    {
        NextThread = KiSelectReadyThread(0, Prcb);
        if (NextThread == CurrentThread)
        {
            SelfPlaced = TRUE;
            NextThread = KiSelectReadyThread(0, Prcb);
        }
    }

    ASSERT(CurrentThread != Prcb->IdleThread);

    if (SelfPlaced && (CurrentThread->State != Terminated))
    {
        if (NextThread)
        {
            NextThread->State = Standby;
            Prcb->NextThread = NextThread;
        }

        CurrentThread->State = Running;
        ASSERT(Prcb->CurrentThread == CurrentThread);
        KiClearThreadSwapBusy(CurrentThread);
        KiReleasePrcbLock(Prcb);

#if defined(_M_ARM64)
        _enable();
#endif

        WaitIrql = CurrentThread->WaitIrql;
        ApcState = (CurrentThread->ApcState.KernelApcPending) &&
                   !(CurrentThread->SpecialApcDisable) &&
                   (WaitIrql == PASSIVE_LEVEL);
    }
    else
    {
        if (NextThread)
        {
            Prcb->CurrentThread = NextThread;
            NextThread->State = Running;
        }
        else
        {
            /* Set the idle summary */
            InterlockedBitTestAndSetAffinity(&KiIdleSummary, Prcb->Number);

            /* Schedule the idle thread */
            NextThread = Prcb->IdleThread;
            Prcb->CurrentThread = NextThread;
            NextThread->State = Running;
        }

        KiReleasePrcbLock(Prcb);

        WaitIrql = CurrentThread->WaitIrql;

        ApcState = KiSwapContext(WaitIrql, CurrentThread);
    }

    /* Get the wait status */
    WaitStatus = CurrentThread->WaitStatus;

    if (NormalWait &&
        (CurrentThread->WaitBlockList != NULL) &&
        (CurrentThread->WaitBlockList[0].Thread == CurrentThread))
    {
        KiUnlinkWaitBlocks(CurrentThread);
    }

    /* Check if we need to deliver APCs */
    if (ApcState)
    {
        /* Lower to APC_LEVEL */
        KeLowerIrql(APC_LEVEL);

        /* Deliver APCs */
        KiDeliverApc(KernelMode, NULL, NULL);
        ASSERT(WaitIrql == 0);
    }

    /* Lower IRQL back to what it was and return the wait status */
    KeLowerIrql(WaitIrql);
    return WaitStatus;
}

VOID
NTAPI
KiReadyThread(IN PKTHREAD Thread)
{
    IN PKPROCESS Process = Thread->ApcState.Process;

    /* Check if the process is paged out */
    if (Process->State != ProcessInMemory)
    {
        /* We don't page out processes in ROS */
        ASSERT(FALSE);
    }
    else if (!Thread->KernelStackResident)
    {
        /* Increase the stack count */
        ASSERT(Process->StackCount != MAXULONG);
        Process->StackCount++;

        /* Set the thread to transition */
        ASSERT(Thread->State != Transition);
        Thread->State = Transition;

        /* The stack is always resident in ROS */
        ASSERT(FALSE);
    }
    else
    {
        /* Insert the thread on the deferred ready list */
        KiInsertDeferredReadyList(Thread);
    }
}

VOID
NTAPI
KiAdjustQuantumThread(IN PKTHREAD Thread)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    PKTHREAD NextThread;

    /* Acquire thread and PRCB lock */
    KiAcquireThreadLock(Thread);
    KiAcquirePrcbLock(Prcb);

    /* Don't adjust for RT threads */
    if ((Thread->Priority < LOW_REALTIME_PRIORITY) &&
        (Thread->BasePriority < (LOW_REALTIME_PRIORITY - 2)))
    {
        /* Decrease Quantum by one and see if we've ran out */
        if (KiDecrementThreadQuantum(Thread, 1))
        {
            /* Return quantum */
            KiSetThreadQuantum(Thread, Thread->QuantumReset);

            /* Calculate new Priority */
            Thread->Priority = KiComputeNewPriority(Thread, 1);

            /* Check if there's no next thread scheduled */
            if (!Prcb->NextThread)
            {
                /* Select a ready thread and check if we found one */
                NextThread = KiSelectReadyThread(Thread->Priority, Prcb);
                if (NextThread)
                {
                    /* Set it on standby and switch to it */
                    NextThread->State = Standby;
                    Prcb->NextThread = NextThread;
                }
            }
            else
            {
                /* This thread can be preempted again */
                Thread->Preempted = FALSE;
            }
        }
    }

    /* Release locks */
    KiReleasePrcbLock(Prcb);
    KiReleaseThreadLock(Thread);
    KiExitDispatcher(Thread->WaitIrql);
}

VOID
FASTCALL
KiSetPriorityThread(IN PKTHREAD Thread,
                    IN KPRIORITY Priority)
{
    PKPRCB Prcb;
    ULONG Processor;
    BOOLEAN RequestInterrupt = FALSE;
    KPRIORITY OldPriority;
    PKTHREAD NewThread;
    ASSERT((Priority >= 0) && (Priority <= HIGH_PRIORITY));

    /* Check if priority changed */
    if (Thread->Priority != Priority)
    {
        /* Loop priority setting in case we need to start over */
        for (;;)
        {
            /* Choose action based on thread's state */
            if (Thread->State == Ready)
            {
                /* Make sure we're not on the ready queue */
                if (!Thread->ProcessReadyQueue)
                {
                    /* Get the PRCB for the thread and lock it */
                    Processor = Thread->NextProcessor;
                    Prcb = KiProcessorBlock[Processor];
                    KiAcquirePrcbLock(Prcb);

                    /* Make sure the thread is still ready and on this CPU */
                    if ((Thread->State == Ready) &&
                        (Thread->NextProcessor == Prcb->Number))
                    {
                        /* Sanity check */
                        ASSERT((Prcb->ReadySummary &
                                PRIORITY_MASK(Thread->Priority)));

                        /* Remove it from the current queue */
                        if (RemoveEntryList(&Thread->WaitListEntry))
                        {
                            /* Update the ready summary */
                            Prcb->ReadySummary ^= PRIORITY_MASK(Thread->
                                                                Priority);
                        }

                        /* Update priority */
                        Thread->Priority = (SCHAR)Priority;

                        /* Re-insert it at its current priority */
                        KiInsertDeferredReadyList(Thread);

                        /* Release the PRCB Lock */
                        KiReleasePrcbLock(Prcb);
                    }
                    else
                    {
                        /* Release the lock and loop again */
                        KiReleasePrcbLock(Prcb);
                        continue;
                    }
                }
                else
                {
                    /* It's already on the ready queue, just update priority */
                    Thread->Priority = (SCHAR)Priority;
                }
            }
            else if (Thread->State == Standby)
            {
                /* Get the PRCB for the thread and lock it */
                Processor = Thread->NextProcessor;
                Prcb = KiProcessorBlock[Processor];
                KiAcquirePrcbLock(Prcb);

                /* Check if we're still the next thread to run */
                if (Thread == Prcb->NextThread)
                {
                    /* Get the old priority and update ours */
                    OldPriority = Thread->Priority;
                    Thread->Priority = (SCHAR)Priority;

                    /* Check if there was a change */
                    if (Priority < OldPriority)
                    {
                        /* Find a new thread */
                        NewThread = KiSelectReadyThread(Priority + 1, Prcb);
                        if (NewThread)
                        {
                            /* Found a new one, set it on standby */
                            NewThread->State = Standby;
                            Prcb->NextThread = NewThread;

                            /* Dispatch our thread */
                            KiInsertDeferredReadyList(Thread);
                        }
                    }

                    /* Release the PRCB lock */
                    KiReleasePrcbLock(Prcb);
                }
                else
                {
                    /* Release the lock and try again */
                    KiReleasePrcbLock(Prcb);
                    continue;
                }
            }
            else if (Thread->State == Running)
            {
                /* Get the PRCB for the thread and lock it */
                Processor = Thread->NextProcessor;
                Prcb = KiProcessorBlock[Processor];
                KiAcquirePrcbLock(Prcb);

                /* Check if we're still the current thread running */
                if (Thread == Prcb->CurrentThread)
                {
                    /* Get the old priority and update ours */
                    OldPriority = Thread->Priority;
                    Thread->Priority = (SCHAR)Priority;

                    /* Check if there was a change and there's no new thread */
                    if ((Priority < OldPriority) && !(Prcb->NextThread))
                    {
                        /* Find a new thread */
                        NewThread = KiSelectReadyThread(Priority + 1, Prcb);
                        if (NewThread)
                        {
                            /* Found a new one, set it on standby */
                            NewThread->State = Standby;
                            Prcb->NextThread = NewThread;

                            /* Request an interrupt */
                            RequestInterrupt = TRUE;
                        }
                    }

                    /* Release the lock and check if we need an interrupt */
                    KiReleasePrcbLock(Prcb);
                    if (RequestInterrupt)
                    {
                        /* Check if we're running on another CPU */
                        if (KeGetCurrentProcessorNumber() != Processor)
                        {
                            /* We are, send an IPI */
#if defined(_M_AMD64) || defined(_M_ARM64)
                            if (SmpDbgEnabled)
                                SmpDbgSchedulerIpi(Processor, NewThread, SMPDBG_SCHED_PRIORITY);
#endif
                            KiIpiSend(AFFINITY_MASK(Processor), IPI_DPC);
                        }
                    }
                }
                else
                {
                    /* Thread changed, release lock and restart */
                    KiReleasePrcbLock(Prcb);
                    continue;
                }
            }
            else if (Thread->State == DeferredReady)
            {
                Thread->Priority = (SCHAR)Priority;
            }
            else
            {
                /* Any other state, just change priority */
                Thread->Priority = (SCHAR)Priority;
            }

            /* If we got here, then thread state was consistent, so bail out */
            break;
        }
    }
}

#ifdef CONFIG_SMP
static
VOID
KiUpdateEffectiveAffinityThread(
    _In_ PKTHREAD Thread)
{
    PKPRCB Prcb;

    /* Acquire the thread lock */
    KiAcquireThreadLock(Thread);

    /* Get the PRCB that the thread is to be run on and lock it */
    Prcb = KiProcessorBlock[Thread->NextProcessor];
    KiAcquirePrcbLock(Prcb);

    /* Set the thread's affinity and ideal processor */
    Thread->Affinity = Thread->UserAffinity;
    Thread->IdealProcessor = Thread->UserIdealProcessor;

    /* Check if the affinity doesn't match with the current processor */
    if ((Prcb->SetMember & KiThreadAffinityMask(Thread)) == 0)
    {
        if (Thread->State == Running)
        {
            /* Check if there is the next thread is selected already */
            if (Prcb->NextThread == NULL)
            {
                /* It is not, select a new thread and set it on standby */
                Prcb->NextThread = KiSelectNextThread(Prcb);
                Prcb->NextThread->State = Standby;
            }

            /* Check if the thread is running on a different processor */
            if (Prcb != KeGetCurrentPrcb())
            {
                /* It is, send an IPI */
#if defined(_M_AMD64) || defined(_M_ARM64)
                if (SmpDbgEnabled)
                    SmpDbgSchedulerIpi(Thread->NextProcessor, Prcb->NextThread, SMPDBG_SCHED_AFFINITY);
#endif
                KiIpiSend(AFFINITY_MASK(Thread->NextProcessor), IPI_DPC);
            }
        }
        else if (Thread->State == Standby)
        {
            /* Select a new thread and set it on standby */
            Prcb->NextThread = KiSelectNextThread(Prcb);
            Prcb->NextThread->State = Standby;

            /* Insert the thread back into the ready list */
            KiInsertDeferredReadyList(Thread);
        }
        else if (Thread->State == Ready)
        {
            /* Remove it from the list */
            if (RemoveEntryList(&Thread->WaitListEntry))
            {
                /* The list is empty now, reset the ready summary */
                Prcb->ReadySummary &= ~PRIORITY_MASK(Thread->Priority);
            }

            /* Insert the thread back into the ready list */
            KiInsertDeferredReadyList(Thread);
        }
    }

    KiReleasePrcbLock(Prcb);
    KiReleaseThreadLock(Thread);
}
#endif // CONFIG_SMP

KAFFINITY
FASTCALL
KiSetAffinityThread(IN PKTHREAD Thread,
                    IN KAFFINITY Affinity)
{
    KAFFINITY OldAffinity;

    /* Get the current affinity */
    OldAffinity = KiThreadUserAffinityMask(Thread);

    /* Make sure that the affinity is valid */
    if (((Affinity & Thread->ApcState.Process->Affinity) != (Affinity)) ||
        (!Affinity))
    {
        /* Bugcheck the system */
        KeBugCheck(INVALID_AFFINITY_SET);
    }

    /* Update the new affinity */
    KiThreadUserAffinityMask(Thread) = Affinity;

#ifdef CONFIG_SMP
    /* Check if system affinity is not active */
    if (!Thread->SystemAffinityActive)
    {
        /* Calculate the new ideal processor from the affinity set */
        Thread->UserIdealProcessor =
            KiFindIdealProcessor(Affinity, Thread->UserIdealProcessor);

        /* Update the effective affinity */
        KiUpdateEffectiveAffinityThread(Thread);
    }
#endif

    /* Return the old affinity */
    return OldAffinity;
}

//
// This macro exists because NtYieldExecution locklessly attempts to read from
// the KPRCB's ready summary, and the usual way of going through KeGetCurrentPrcb
// would require getting fs:1C first (or gs), and then doing another dereference.
// In an attempt to minimize the amount of instructions and potential race/tear
// that could happen, Windows seems to define this as a macro that directly acceses
// the ready summary through a single fs: read by going through the KPCR's PrcbData.
//
// See http://research.microsoft.com/en-us/collaboration/global/asia-pacific/programs/trk_case4_process-thread_management.pdf (DEAD_LINK)
//
// We need this per-arch because sometimes it's Prcb and sometimes PrcbData, and
// because on x86 it's FS, and on x64 it's GS (not sure what it is on ARM/PPC).
//
#ifdef _M_IX86
#define KiGetCurrentReadySummary() __readfsdword(FIELD_OFFSET(KIPCR, PrcbData.ReadySummary))
#elif _M_AMD64
#define KiGetCurrentReadySummary() __readgsdword(FIELD_OFFSET(KIPCR, Prcb.ReadySummary))
#else
#define KiGetCurrentReadySummary() KeGetCurrentPrcb()->ReadySummary
#endif

/*
 * @implemented
 */
NTSTATUS
NTAPI
NtYieldExecution(VOID)
{
    NTSTATUS Status;
    KIRQL OldIrql;
    PKPRCB Prcb;
    PKTHREAD Thread, NextThread;

    /* NB: No instructions (other than entry code) should preceed this line */

    /* Fail if there's no ready summary */
    if (!KiGetCurrentReadySummary()) return STATUS_NO_YIELD_PERFORMED;

    /* Now get the current thread, set the status... */
    Status = STATUS_NO_YIELD_PERFORMED;
    Thread = KeGetCurrentThread();

    /* Raise IRQL to synch and get the KPRCB now */
    OldIrql = KeRaiseIrqlToSynchLevel();
    Prcb = KeGetCurrentPrcb();

    /* Now check if there's still a ready summary */
    if (Prcb->ReadySummary)
    {
        /* Acquire thread and PRCB lock */
        KiAcquireThreadLock(Thread);
        KiAcquirePrcbLock(Prcb);

        /* Find a new thread to run if none was selected */
        if (!Prcb->NextThread)
        {
            NextThread = KiSelectReadyThread(1, Prcb);
            if (NextThread)
            {
                NextThread->State = Standby;
                Prcb->NextThread = NextThread;
            }
        }

        /* Consuming a stale self-placement means no yield occurred. */
        if (KiConsumeSelfNextThread(Prcb, Thread))
        {
            NextThread = NULL;
        }
        else
        {
            NextThread = Prcb->NextThread;
        }

        if (NextThread)
        {
            /* Reset quantum and recalculate priority */
            KiSetThreadQuantum(Thread, Thread->QuantumReset);
            Thread->Priority = KiComputeNewPriority(Thread, 1);

            /* Release the thread lock */
            KiReleaseThreadLock(Thread);

            /* Set context swap busy */
            KiSetThreadSwapBusy(Thread);

#if defined(_M_ARM64)
            _disable();
#endif

            /* Set the new thread as running */
            Prcb->NextThread = NULL;
            Prcb->CurrentThread = NextThread;
            NextThread->State = Running;

            /* Setup a yield wait and queue the thread */
            Thread->WaitReason = WrYieldExecution;
            KxQueueReadyThread(Thread, Prcb);

            /* Make it wait at APC_LEVEL */
            Thread->WaitIrql = APC_LEVEL;

            /* Sanity check */
            ASSERT(OldIrql <= DISPATCH_LEVEL);

            /* Swap to new thread */
            KiSwapContext(APC_LEVEL, Thread);
            Status = STATUS_SUCCESS;
        }
        else
        {
            /* Release the PRCB and thread lock */
            KiReleasePrcbLock(Prcb);
            KiReleaseThreadLock(Thread);
        }
    }

    /* Lower IRQL and return */
    KeLowerIrql(OldIrql);
    return Status;
}
