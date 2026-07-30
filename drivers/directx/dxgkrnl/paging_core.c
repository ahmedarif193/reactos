/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Paging operation validation and the multipass build state machine
 */

#include "paging_core.h"

NTSTATUS
DxgkPagingCoreValidate(
    _In_ const DXGK_PAGING_CORE_REQUEST *Request,
    _In_ ULONG PageTableLevelCount)
{
    if (Request->Operation == DxgkPagingCoreOpNone || Request->Operation >= DxgkPagingCoreOpMax)
        return STATUS_INVALID_PARAMETER;

    switch (Request->Operation)
    {
        case DxgkPagingCoreOpTransfer:
            if (Request->TransferSize == 0)
                return STATUS_INVALID_PARAMETER;
            /* A transfer between the same segment at the same place is not a
             * copy; and one side must be real memory the CPU can reach. */
            if (Request->SourceSegmentId == Request->DestinationSegmentId)
                return STATUS_INVALID_PARAMETER;
            if (Request->SourceSegmentId == 0 && Request->DestinationSegmentId == 0)
                return STATUS_INVALID_PARAMETER;
            /* Segment id 0 means system memory, which must actually exist. */
            if ((Request->SourceSegmentId == 0 || Request->DestinationSegmentId == 0) &&
                !Request->SystemMemoryPresent)
                return STATUS_INVALID_PARAMETER;
            return STATUS_SUCCESS;

        case DxgkPagingCoreOpFill:
            if (Request->TransferSize == 0)
                return STATUS_INVALID_PARAMETER;
            if (Request->DestinationSegmentId == 0)
                return STATUS_INVALID_PARAMETER;
            return STATUS_SUCCESS;

        case DxgkPagingCoreOpDiscardContent:
        case DxgkPagingCoreOpNotifyResidency:
            return STATUS_SUCCESS;

        case DxgkPagingCoreOpMapAperture:
        case DxgkPagingCoreOpUnmapAperture:
            /* Aperture segments are 1-based; segment 0 is system memory and
             * has no aperture to map into. */
            if (Request->DestinationSegmentId == 0)
                return STATUS_INVALID_PARAMETER;
            if (Request->Operation == DxgkPagingCoreOpMapAperture && !Request->SystemMemoryPresent)
                return STATUS_INVALID_PARAMETER;
            return STATUS_SUCCESS;

        case DxgkPagingCoreOpUpdatePageTable:
            if (Request->PageTableLevel >= PageTableLevelCount)
                return STATUS_INVALID_PARAMETER;
            if (Request->SizeInBytes == 0)
                return STATUS_INVALID_PARAMETER;
            if (Request->GpuVirtualAddress > MAXULONGLONG - Request->SizeInBytes)
                return STATUS_INTEGER_OVERFLOW;
            return STATUS_SUCCESS;

        case DxgkPagingCoreOpFlushTlb:
            /* A zero-length flush would leave stale translations behind while
             * reporting success. */
            if (Request->SizeInBytes == 0)
                return STATUS_INVALID_PARAMETER;
            if (Request->GpuVirtualAddress > MAXULONGLONG - Request->SizeInBytes)
                return STATUS_INTEGER_OVERFLOW;
            return STATUS_SUCCESS;

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

NTSTATUS
DxgkPagingCoreMultipassBegin(
    _Out_ PDXGK_PAGING_CORE_MULTIPASS Pass,
    _In_ ULONGLONG TotalBytes)
{
    RtlZeroMemory(Pass, sizeof(*Pass));
    if (TotalBytes == 0)
        return STATUS_INVALID_PARAMETER;
    Pass->TotalBytes = TotalBytes;
    Pass->Started = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkPagingCoreMultipassIsFirst(
    _In_ const DXGK_PAGING_CORE_MULTIPASS Pass[1])
{
    return Pass->Started && Pass->PassCount == 0;
}

NTSTATUS
DxgkPagingCoreMultipassAdvance(
    _Inout_ PDXGK_PAGING_CORE_MULTIPASS Pass,
    _In_ NTSTATUS MiniportStatus,
    _In_ ULONGLONG BytesProcessed)
{
    if (!Pass->Started || Pass->Complete)
        return STATUS_INVALID_DEVICE_STATE;
    if (Pass->PassCount >= DXGK_PAGING_CORE_MAX_PASSES)
        return STATUS_DEVICE_BUSY;
    if (BytesProcessed > Pass->TotalBytes - Pass->MultipassOffset)
        return STATUS_INVALID_PARAMETER;

    Pass->PassCount++;
    Pass->MultipassOffset += BytesProcessed;

    if (MiniportStatus == STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER)
    {
        /*
         * The miniport wants another pass.  It must have consumed something,
         * or the loop would ask for the same bytes forever.
         */
        if (BytesProcessed == 0)
            return STATUS_DEVICE_PROTOCOL_ERROR;
        if (Pass->MultipassOffset >= Pass->TotalBytes)
        {
            /* Nothing left, so there is nothing for another pass to do. */
            Pass->Complete = TRUE;
            return STATUS_SUCCESS;
        }
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    if (!NT_SUCCESS(MiniportStatus))
        return MiniportStatus;

    /* A success that left bytes behind is a protocol violation: the miniport
     * must either finish or ask for another pass. */
    if (Pass->MultipassOffset != Pass->TotalBytes)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    Pass->Complete = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkPagingCoreMultipassMayEmitFence(
    _In_ const DXGK_PAGING_CORE_MULTIPASS Pass[1])
{
    return Pass->Complete && !Pass->FenceEmitted;
}

NTSTATUS
DxgkPagingCoreMultipassEmitFence(
    _Inout_ PDXGK_PAGING_CORE_MULTIPASS Pass)
{
    /*
     * Signalling before the last pass would tell the waiter the copy finished
     * while the tail of it is still outstanding.
     */
    if (!Pass->Complete)
        return STATUS_INVALID_DEVICE_STATE;
    if (Pass->FenceEmitted)
        return STATUS_INVALID_DEVICE_STATE;
    Pass->FenceEmitted = TRUE;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkPagingCoreShouldAppendMonitoredSignal(
    _In_ ULONG ConfiguredWddmLevel,
    _In_ ULONG RuntimeWddmLevel,
    _In_ BOOLEAN SignalRequested,
    _In_ BOOLEAN GpuAddressReferenceAvailable)
{
    return ConfiguredWddmLevel >= 2200 &&
           RuntimeWddmLevel >= 2200 &&
           SignalRequested &&
           GpuAddressReferenceAvailable;
}

NTSTATUS
DxgkPagingCoreBeginMonitoredSignal(
    _In_ BOOLEAN OperationsBuilt)
{
    /*
     * Operation 16 is the last command in the same paging DMA stream.  It
     * cannot be built while any transfer/fill/page-table pass is outstanding.
     */
    return OperationsBuilt
               ? STATUS_SUCCESS
               : STATUS_INVALID_DEVICE_STATE;
}

NTSTATUS
DxgkPagingCoreFinishMonitoredSignal(
    _In_ NTSTATUS BuildStatus,
    _In_ ULONG BytesEmitted,
    _Out_ PBOOLEAN SignalWrittenByGpu)
{
    if (SignalWrittenByGpu == NULL)
        return STATUS_INVALID_PARAMETER;
    *SignalWrittenByGpu = FALSE;
    if (!NT_SUCCESS(BuildStatus))
        return BuildStatus;

    /*
     * A miniport may return success without advancing pDmaBuffer.  That did
     * not encode a GPU write, so the tracked retire path must retain its CPU
     * publication fallback.
     */
    *SignalWrittenByGpu = BytesEmitted != 0;
    return STATUS_SUCCESS;
}

DXGK_PAGING_NO_WORK_SIGNAL_ROUTE
DxgkPagingCoreNoWorkSignalRoute(
    _In_ BOOLEAN SignalRequested,
    _In_ BOOLEAN RetainedReferenceAvailable)
{
    if (!SignalRequested)
        return DxgkPagingNoWorkSignalNone;
    if (RetainedReferenceAvailable)
        return DxgkPagingNoWorkSignalRetainedReference;
    return DxgkPagingNoWorkSignalHandle;
}

VOID
DxgkPagingFenceQueueCoreInitialize(
    _Out_ PDXGK_PAGING_FENCE_QUEUE_CORE Queue,
    _In_ ULONGLONG InitialFenceValue)
{
    RtlZeroMemory(Queue, sizeof(*Queue));
    KeInitializeMutex(&Queue->Lock, 0);
    Queue->CommittedFenceValue = InitialFenceValue;
    KeMemoryBarrier();
    Queue->Initialized = TRUE;
}

NTSTATUS
DxgkPagingFenceQueueCoreBegin(
    _Inout_ PDXGK_PAGING_FENCE_QUEUE_CORE Queue,
    _Out_ PDXGK_PAGING_FENCE_TRANSACTION Transaction)
{
    NTSTATUS Status;

    PAGED_CODE();
    if (Queue == NULL || Transaction == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Transaction, sizeof(*Transaction));
    if (!Queue->Initialized)
        return STATUS_INVALID_DEVICE_STATE;

    Status = KeWaitForSingleObject(&Queue->Lock,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Queue->Initialized || Queue->ShuttingDown)
    {
        KeReleaseMutex(&Queue->Lock, FALSE);
        return STATUS_DELETE_PENDING;
    }
    if (Queue->CommittedFenceValue == MAXULONGLONG)
    {
        KeReleaseMutex(&Queue->Lock, FALSE);
        return STATUS_INTEGER_OVERFLOW;
    }

    Transaction->Queue = Queue;
    Transaction->CandidateCounter =
        (LONG64)Queue->CommittedFenceValue;
    Transaction->StartingFenceValue =
        Queue->CommittedFenceValue;
    Transaction->Active = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkPagingFenceQueueCoreComplete(
    _Inout_ PDXGK_PAGING_FENCE_TRANSACTION Transaction,
    _In_ ULONGLONG PublishedFenceValue)
{
    PDXGK_PAGING_FENCE_QUEUE_CORE Queue;
    ULONGLONG Candidate;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (Transaction == NULL ||
        !Transaction->Active ||
        Transaction->Queue == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    Queue = Transaction->Queue;
    Candidate = (ULONGLONG)InterlockedCompareExchange64(
                                &Transaction->CandidateCounter,
                                0,
                                0);

    if (PublishedFenceValue == 0)
    {
        /* A request that produced no paging packet consumes no value. */
        if (Candidate != Transaction->StartingFenceValue)
        {
            Queue->CommittedFenceValue = Candidate;
            Status = STATUS_DEVICE_PROTOCOL_ERROR;
        }
    }
    else if (Candidate != PublishedFenceValue ||
             PublishedFenceValue !=
                 Transaction->StartingFenceValue + 1)
    {
        /*
         * Never reuse a value that the producer may already have exposed.
         * This is a provider contract failure, but consuming the largest
         * observed candidate is safer than allowing a later transaction to
         * alias an in-flight fence.
         */
        Queue->CommittedFenceValue =
            max(Candidate, PublishedFenceValue);
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
    }
    else
    {
        Queue->CommittedFenceValue = PublishedFenceValue;
    }

    KeMemoryBarrier();
    Transaction->Active = FALSE;
    Transaction->Queue = NULL;
    KeReleaseMutex(&Queue->Lock, FALSE);
    return Status;
}

VOID
DxgkPagingFenceQueueCoreAbort(
    _Inout_ PDXGK_PAGING_FENCE_TRANSACTION Transaction)
{
    PDXGK_PAGING_FENCE_QUEUE_CORE Queue;

    PAGED_CODE();
    if (Transaction == NULL ||
        !Transaction->Active ||
        Transaction->Queue == NULL)
    {
        return;
    }

    /*
     * Scheduler admission is the commit point.  The caller invokes Abort only
     * when no packet was admitted, so even a locally incremented candidate is
     * deliberately discarded and can be reused by the next transaction.
     */
    Queue = Transaction->Queue;
    Transaction->Active = FALSE;
    Transaction->Queue = NULL;
    KeReleaseMutex(&Queue->Lock, FALSE);
}

VOID
DxgkPagingFenceQueueCoreShutDown(
    _Inout_ PDXGK_PAGING_FENCE_QUEUE_CORE Queue)
{
    NTSTATUS Status;

    PAGED_CODE();
    if (Queue == NULL || !Queue->Initialized)
        return;

    /*
     * Waiting for the mutex drains the one admitted/building transaction.
     * Once ShuttingDown is visible, callers that retained the queue before its
     * public handle was detached are also refused at Begin.
     */
    Status = KeWaitForSingleObject(&Queue->Lock,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return;
    Queue->ShuttingDown = TRUE;
    KeMemoryBarrier();
    KeReleaseMutex(&Queue->Lock, FALSE);
}

ULONGLONG
DxgkPagingFenceQueueCoreQueryCommitted(
    _Inout_ PDXGK_PAGING_FENCE_QUEUE_CORE Queue)
{
    ULONGLONG Value;
    NTSTATUS Status;

    PAGED_CODE();
    if (Queue == NULL || !Queue->Initialized)
        return 0;

    Status = KeWaitForSingleObject(&Queue->Lock,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   NULL);
    if (!NT_SUCCESS(Status))
        return 0;
    Value = Queue->CommittedFenceValue;
    KeReleaseMutex(&Queue->Lock, FALSE);
    return Value;
}

/* EOF */
