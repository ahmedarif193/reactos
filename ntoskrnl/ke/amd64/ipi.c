/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     IPI code for x64
 * COPYRIGHT:   Copyright 2023 Timo Kreuzer <timo.kreuzer@reactos.org>
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

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

        if (Requests & IPI_DPC)
            DpcRequest = TRUE;

        ASSERT((Requests & ~IPI_DPC) == 0);
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
    __debugbreak();
    return 0;
}
