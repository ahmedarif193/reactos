/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IPI code for x64
 * COPYRIGHT:   Copyright 2023 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#include <reactos/smpdbg.h>
#define NDEBUG
#include <debug.h>

#define KI_IPI_MEMORY       0x20
#define KI_IPI_GENERIC_CALL 0x40

typedef struct DECLSPEC_CACHEALIGN _KI_GENERIC_CALL_PACKET
{
    PKIPI_BROADCAST_WORKER Function;
    ULONG_PTR Argument;
    volatile LONG TargetsRemaining;
    volatile LONG Release;
    volatile KAFFINITY OutstandingDone;
} KI_GENERIC_CALL_PACKET;

static KI_GENERIC_CALL_PACKET KiGenericCallPacket;
static volatile LONG KiGenericCallOwner = -1;

/* FUNCTIONS *****************************************************************/

static
KAFFINITY
KiIpiQueueRequest(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG IpiRequest)
{
    KAFFINITY QueuedTargets;
    PKPRCB SourcePrcb = KeGetCurrentPrcb();
    ULONG Processor;

    ASSERT(SourcePrcb->Number < MAXIMUM_PROCESSORS);
    TargetSet &= (KAFFINITY)KeActiveProcessors;
    QueuedTargets = TargetSet;

    while (BitScanForwardAffinity(&Processor, TargetSet))
    {
        PKPRCB TargetPrcb = KiProcessorBlock[Processor];

        ASSERT(TargetPrcb != NULL);
        InterlockedOr64(
            (PLONG64)&TargetPrcb->RequestMailbox[SourcePrcb->Number].RequestSummary,
            IpiRequest);

        InterlockedBitTestAndSetAffinity(
            (volatile KAFFINITY *)&TargetPrcb->SenderSummary,
            SourcePrcb->Number);

        TargetSet &= ~AFFINITY_MASK(Processor);
    }

    return QueuedTargets;
}

VOID
FASTCALL
KiIpiSend(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG IpiRequest)
{
    /* Check if we can send the IPI directly */
    if (IpiRequest == IPI_APC)
    {
        HalSendSoftwareInterrupt(TargetSet, APC_LEVEL);
    }
    else if (IpiRequest == IPI_DPC)
    {
        TargetSet = KiIpiQueueRequest(TargetSet, IPI_DPC);

        if (TargetSet != 0)
            HalRequestIpi(TargetSet);
    }
    else if (IpiRequest == IPI_FREEZE)
    {
        /* On x64 the freeze IPI is an NMI */
        HalSendNMI(TargetSet);
    }
    else
    {
        ASSERT(FALSE);
    }
}

VOID
KiSendMemoryIpi(ULONG64 Targets)
{
    KAFFINITY Queued = KiIpiQueueRequest((KAFFINITY)Targets, KI_IPI_MEMORY);
    if (Queued != 0)
        HalRequestIpi(Queued);
}

static
VOID
KiIpiParticipateInGenericCall(
    _In_ PKPRCB Prcb)
{
    PKIPI_BROADCAST_WORKER Function = KiGenericCallPacket.Function;
    ULONG_PTR Argument = KiGenericCallPacket.Argument;
    KIRQL OldIrql;

    ASSERT(KiGenericCallPacket.OutstandingDone & Prcb->SetMember);

    InterlockedDecrement(&KiGenericCallPacket.TargetsRemaining);
    while (!KiGenericCallPacket.Release)
    {
        YieldProcessor();
    }

    OldIrql = KfRaiseIrql(IPI_LEVEL);
    Function(Argument);
    KeLowerIrql(OldIrql);

    InterlockedBitTestAndResetAffinity(&KiGenericCallPacket.OutstandingDone,
                                       Prcb->Number);
}

VOID
NTAPI
KiIpiProcessRequests(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    KAFFINITY Sources;
    BOOLEAN DpcRequest = FALSE;
    ULONG Source;

    ASSERT(Prcb->Number < MAXIMUM_PROCESSORS);
    Sources = (KAFFINITY)InterlockedExchange64(
        (PLONG64)&Prcb->SenderSummary,
        0);

    while (BitScanForwardAffinity(&Source, Sources))
    {
        ULONG64 Requests;

        Requests = (ULONG64)InterlockedExchange64(
            (PLONG64)&Prcb->RequestMailbox[Source].RequestSummary,
            0);

        if (Requests & KI_IPI_MEMORY)
        {
            if (SmpDbgEnabled)
                SmpDbgTbFlushIpi(Prcb->Number);
            MiAmd64ProcessTlbRequest(Source);
        }

        if (Requests & KI_IPI_GENERIC_CALL)
        {
            if (SmpDbgEnabled)
                SmpDbgGenericCallIpi(Prcb->Number);
            KiIpiParticipateInGenericCall(Prcb);
        }

        if (Requests & IPI_DPC)
        {
            if (SmpDbgEnabled)
                SmpDbgRemoteDpc(Prcb->Number, Source);
            DpcRequest = TRUE;
        }

        ASSERT((Requests & ~(KI_IPI_MEMORY |
                             KI_IPI_GENERIC_CALL |
                             IPI_DPC)) == 0);
        Sources &= ~AFFINITY_MASK(Source);
    }

    if (DpcRequest)
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
}

ULONG_PTR
NTAPI
KeIpiGenericCall(
    _In_ PKIPI_BROADCAST_WORKER Function,
    _In_ ULONG_PTR Argument)
{
    PKPRCB Prcb;
    KAFFINITY Targets, IpiTargets, RemainingSet;
    BOOLEAN InterruptsEnabled;
    KIRQL OldIrql, ExecuteIrql;
    ULONG_PTR Status;
    ULONG Processor;
    LONG TargetCount;

    InterruptsEnabled = (__readeflags() & EFLAGS_INTERRUPT_MASK) != 0;

    OldIrql = KeGetCurrentIrql();
    if (OldIrql < SYNCH_LEVEL)
        KeRaiseIrql(SYNCH_LEVEL, &OldIrql);

    Prcb = KeGetCurrentPrcb();

    while (InterlockedCompareExchange(&KiGenericCallOwner,
                                      (LONG)Prcb->Number,
                                      -1) != -1)
    {
        KiIpiProcessRequests();
        YieldProcessor();
    }

    Targets = (KAFFINITY)KeActiveProcessors & ~Prcb->SetMember;

    if (Targets != 0)
    {
        TargetCount = 0;
        RemainingSet = Targets;
        while (BitScanForwardAffinity(&Processor, RemainingSet))
        {
            TargetCount++;
            RemainingSet &= ~AFFINITY_MASK(Processor);
        }

        KiGenericCallPacket.Function = Function;
        KiGenericCallPacket.Argument = Argument;
        KiGenericCallPacket.Release = 0;
        KiGenericCallPacket.OutstandingDone = Targets;
        KiGenericCallPacket.TargetsRemaining = TargetCount;
        KeMemoryBarrier();

        IpiTargets = KiIpiQueueRequest(Targets, KI_IPI_GENERIC_CALL);
        if (IpiTargets != 0)
            HalRequestIpi(IpiTargets);

        while (KiGenericCallPacket.TargetsRemaining != 0)
        {
            if (!InterruptsEnabled)
                KiIpiProcessRequests();

            YieldProcessor();
        }
    }

    KeRaiseIrql(IPI_LEVEL, &ExecuteIrql);
    if (Targets != 0)
        KiGenericCallPacket.Release = 1;

    Status = Function(Argument);

    while (KiGenericCallPacket.OutstandingDone != 0)
    {
        if (!InterruptsEnabled)
            KiIpiProcessRequests();

        YieldProcessor();
    }

    KeLowerIrql(ExecuteIrql);
    InterlockedExchange(&KiGenericCallOwner, -1);
    KeLowerIrql(OldIrql);
    return Status;
}
