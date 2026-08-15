/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Portable processor related routines
 * COPYRIGHT:   Copyright 2025 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

KAFFINITY KeActiveProcessors = 0;

KAFFINITY
FASTCALL
KiGetNonParkedProcessorSet(VOID)
{
#ifdef _M_ARM64
    KAFFINITY NonParkedSet = *(volatile KAFFINITY *)&KiNode0SubNode.NonParkedSet;

    return NonParkedSet ? (NonParkedSet & KeActiveProcessors) : KeActiveProcessors;
#else
    return KeActiveProcessors;
#endif
}

BOOLEAN
FASTCALL
KiIsProcessorParked(
    _In_ PKPRCB Prcb)
{
    return (KiGetNonParkedProcessorSet() & Prcb->SetMember) == 0;
}

#ifdef _M_ARM64
static
ULONG
KiCountProcessorSet(
    _In_ KAFFINITY ProcessorSet)
{
    ULONG Count = 0;

    while (ProcessorSet)
    {
        ProcessorSet &= ProcessorSet - 1;
        Count++;
    }
    return Count;
}

static
PKTHREAD
KiRemoveParkedReadyThread(
    _In_ PKPRCB Prcb,
    _In_ KAFFINITY NonParkedSet)
{
    PLIST_ENTRY ListHead;
    PLIST_ENTRY Entry;
    PKTHREAD Thread;
    LONG Priority;

    KiAcquirePrcbLock(Prcb);
    for (Priority = HIGH_PRIORITY; Priority >= 0; Priority--)
    {
        if (!(Prcb->ReadySummary & PRIORITY_MASK(Priority)))
            continue;
        ListHead = &Prcb->DispatcherReadyListHead[Priority];
        for (Entry = ListHead->Flink; Entry != ListHead; Entry = Entry->Flink)
        {
            Thread = CONTAINING_RECORD(Entry, KTHREAD, WaitListEntry);
            if (!(KiThreadAffinityMask(Thread) & NonParkedSet) || KiTryThreadLock(Thread))
                continue;
            if (Thread->State != Ready || Thread->NextProcessor != Prcb->Number || Thread->Priority != Priority)
            {
                KiReleaseThreadLock(Thread);
                continue;
            }
            if (RemoveEntryList(&Thread->WaitListEntry))
                Prcb->ReadySummary &= ~PRIORITY_MASK(Priority);
            Thread->State = DeferredReady;
            KiReleasePrcbLock(Prcb);
            KiReleaseThreadLock(Thread);
            return Thread;
        }
    }
    KiReleasePrcbLock(Prcb);
    return NULL;
}

static
PKTHREAD
KiRemoveParkedStandbyThread(
    _In_ PKPRCB Prcb,
    _In_ KAFFINITY NonParkedSet)
{
    PKTHREAD Thread;

    KiAcquirePrcbLock(Prcb);
    Thread = Prcb->NextThread;
    if (!Thread || !(KiThreadAffinityMask(Thread) & NonParkedSet) || KiTryThreadLock(Thread))
    {
        KiReleasePrcbLock(Prcb);
        return NULL;
    }
    if (Prcb->NextThread != Thread || Thread->State != Standby || Thread->NextProcessor != Prcb->Number)
    {
        KiReleaseThreadLock(Thread);
        KiReleasePrcbLock(Prcb);
        return NULL;
    }
    Prcb->NextThread = NULL;
    Thread->State = DeferredReady;
    KiReleasePrcbLock(Prcb);
    KiReleaseThreadLock(Thread);
    return Thread;
}

static
VOID
KiMigrateParkedProcessorWork(
    _In_ KAFFINITY NewlyParkedSet,
    _In_ KAFFINITY NonParkedSet,
    _Out_ PKAFFINITY InterruptSet)
{
    PKTHREAD Thread;
    PKPRCB Prcb;
    KIRQL OldIrql;
    ULONG Processor;

    *InterruptSet = 0;
    OldIrql = KeRaiseIrqlToDpcLevel();
    while (NewlyParkedSet)
    {
        BitScanForwardAffinity(&Processor, NewlyParkedSet);
        NewlyParkedSet &= ~AFFINITY_MASK(Processor);
        Prcb = KiProcessorBlock[Processor];
        if (!Prcb)
            continue;
        while ((Thread = KiRemoveParkedReadyThread(Prcb, NonParkedSet)) != NULL)
            KiDeferredReadyThread(Thread);
        Thread = KiRemoveParkedStandbyThread(Prcb, NonParkedSet);
        if (Thread)
            KiDeferredReadyThread(Thread);
        for (;;)
        {
            Thread = Prcb->CurrentThread;
            if (!Thread || Thread == Prcb->IdleThread)
                break;
            KiAcquireThreadLock(Thread);
            KiAcquirePrcbLock(Prcb);
            if (Prcb->CurrentThread != Thread)
            {
                KiReleasePrcbLock(Prcb);
                KiReleaseThreadLock(Thread);
                continue;
            }
            if (KiThreadAffinityMask(Thread) & NonParkedSet)
            {
                KiSetThreadQuantum(Thread, 0);
                Prcb->QuantumEnd = TRUE;
                *InterruptSet |= Prcb->SetMember;
            }
            KiReleasePrcbLock(Prcb);
            KiReleaseThreadLock(Thread);
            break;
        }
    }
    if (*InterruptSet)
        KiIpiSend(*InterruptSet, IPI_DPC);
    KeLowerIrql(OldIrql);
}
#endif

NTSTATUS
NTAPI
PoSetProcessorAggregatorParking(
    _In_ ULONG RequestedParkedProcessors,
    _Out_ PULONG ParkedProcessors)
{
#ifdef _M_ARM64
    KAFFINITY ActiveSet;
    KAFFINITY NonParkedSet;
    KAFFINITY OldNonParkedSet;
    KAFFINITY NewlyParkedSet;
    KAFFINITY InterruptSet;
    KAFFINITY CoreSet;
    KAFFINITY ProcessedSet = 0;
    PKPRCB Prcb;
    ULONG ActiveCount;
    ULONG CoreCount;
    ULONG Remaining;
    LONG Processor;

    if (!ParkedProcessors || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return STATUS_INVALID_PARAMETER;
    ActiveSet = KeActiveProcessors;
    ActiveCount = KiCountProcessorSet(ActiveSet);
    if (!ActiveCount)
        return STATUS_DEVICE_NOT_READY;
    Remaining = min(RequestedParkedProcessors, ActiveCount - 1);
    NonParkedSet = ActiveSet;
    for (Processor = (LONG)KeNumberProcessors - 1; Processor >= 0 && Remaining; Processor--)
    {
        if (!(ActiveSet & AFFINITY_MASK(Processor)) || (ProcessedSet & AFFINITY_MASK(Processor)))
            continue;
        Prcb = KiProcessorBlock[Processor];
        CoreSet = Prcb ? (Prcb->MultiThreadProcessorSet & ActiveSet) : AFFINITY_MASK(Processor);
        if (!CoreSet)
            CoreSet = AFFINITY_MASK(Processor);
        ProcessedSet |= CoreSet;
        if (CoreSet & AFFINITY_MASK(0))
            continue;
        CoreCount = KiCountProcessorSet(CoreSet);
        if (CoreCount <= Remaining && KiCountProcessorSet(NonParkedSet) > CoreCount)
        {
            NonParkedSet &= ~CoreSet;
            Remaining -= CoreCount;
        }
    }
    for (Processor = (LONG)KeNumberProcessors - 1; Processor >= 1 && Remaining; Processor--)
    {
        if (NonParkedSet & AFFINITY_MASK(Processor))
        {
            NonParkedSet &= ~AFFINITY_MASK(Processor);
            Remaining--;
        }
    }
    OldNonParkedSet = (KAFFINITY)InterlockedExchange64((PLONG64)&KiNode0SubNode.NonParkedSet, (LONG64)NonParkedSet);
    *ParkedProcessors = KiCountProcessorSet(ActiveSet & ~NonParkedSet);
    MmWriteableSharedUserData->UnparkedProcessorCount = (USHORT)(ActiveCount - *ParkedProcessors);
    NewlyParkedSet = OldNonParkedSet & ~NonParkedSet;
    if (NewlyParkedSet)
        KiMigrateParkedProcessorWork(NewlyParkedSet, NonParkedSet, &InterruptSet);
    DPRINT1("PO: processor aggregator requested %lu parked, applied %lu (nonparked=0x%Ix)\n",
            RequestedParkedProcessors, *ParkedProcessors, NonParkedSet);
    return STATUS_SUCCESS;
#else
    UNREFERENCED_PARAMETER(RequestedParkedProcessors);
    if (ParkedProcessors)
        *ParkedProcessors = 0;
    return STATUS_NOT_SUPPORTED;
#endif
}

/* Number of processors */
#if (NTDDI_VERSION >= NTDDI_VISTA)
volatile CCHAR KeNumberProcessors = 0;
#else
CCHAR KeNumberProcessors = 0;
#endif

#ifdef CONFIG_SMP

/* Theoretical maximum number of processors that can be handled.
 * Set once at run-time. Returned by KeQueryMaximumProcessorCount(). */
ULONG KeMaximumProcessors = MAXIMUM_PROCESSORS;

/* Maximum number of logical processors that can be started
 * (including dynamically) at run-time. If 0: do not perform checks. */
ULONG KeNumprocSpecified = 0;

/* Maximum number of logical processors that can be started
 * at boot-time. If 0: do not perform checks. */
ULONG KeBootprocSpecified = 0;

#endif // CONFIG_SMP

/* FUNCTIONS *****************************************************************/

KAFFINITY
NTAPI
KeQueryActiveProcessors(VOID)
{
    return KeActiveProcessors;
}

ULONG
NTAPI
KeQueryActiveProcessorCountEx(IN USHORT GroupNumber)
{
    if (GroupNumber != 0 && GroupNumber != ALL_PROCESSOR_GROUPS)
        return 0;
    return (ULONG)KeNumberProcessors;
}

ULONG
NTAPI
KeQueryMaximumProcessorCount(VOID)
{
#ifdef CONFIG_SMP
    return KeMaximumProcessors;
#else
    return (ULONG)KeNumberProcessors;
#endif
}

ULONG
NTAPI
KeQueryMaximumProcessorCountEx(
    _In_ USHORT GroupNumber)
{
    /* Only a single processor group (group 0) is supported. */
    if ((GroupNumber != ALL_PROCESSOR_GROUPS) && (GroupNumber != 0))
        return 0;

    return KeQueryMaximumProcessorCount();
}

ULONG
NTAPI
KeGetCurrentProcessorNumberEx(
    _Out_opt_ PPROCESSOR_NUMBER ProcNumber)
{
    /* Only a single processor group (group 0) is supported, so the processor
     * number within the group is simply the current processor number. */
    ULONG Number = KeGetCurrentProcessorNumber();

    if (ProcNumber != NULL)
    {
        ProcNumber->Group = 0;
        ProcNumber->Number = (UCHAR)Number;
        ProcNumber->Reserved = 0;
    }

    return Number;
}

/**
 * Retrieves the number of the current processor.
 *
 * \param ProcessorNumber Pointer to a PROCESSOR_NUMBER structure that receives the processor number.
 *
 * \return NTSTATUS The status of the operation.
 */
NTSTATUS
NTAPI
NtGetCurrentProcessorNumberEx(
    _Out_ PPROCESSOR_NUMBER ProcessorNumber)
{
    _SEH2_TRY
    {
        ProbeForWrite(ProcessorNumber, sizeof(PROCESSOR_NUMBER), __alignof(PROCESSOR_NUMBER));
        ProcessorNumber->Group = 0; // TODO: Support processor groups
        ProcessorNumber->Number = (UCHAR)KeGetCurrentProcessorNumber();
        ProcessorNumber->Reserved = 0;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        return _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return STATUS_SUCCESS;
}
