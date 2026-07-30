/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidMm paging-operation state machine
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * One owned state machine for every WDDM paging packet kind: transfer, fill,
 * discard, map/unmap aperture, page-table update, TLB flush, and residency
 * notification.  Each public operation runs the documented multipass
 * DxgkDdiBuildPagingBuffer protocol, submits every produced pass as a tracked
 * paging packet, and reaches exactly one terminal edge — the caller's paging
 * fence is signaled once, either by the final packet's retirement or, when the
 * miniport produced no hardware work at all, synchronously.
 *
 * Placement is not published by the build: an allocation whose content is in
 * flight keeps its committed placement until the packet that moves it retires
 * (DxgkPagingSyncPlacement).  Scheduler admission consumes the same fence, so
 * no render packet can execute against a placement its paging work has not
 * finished establishing.
 */

#include "dxgkrnl_private.h"
#include "paging_core.h"
#include "vidmm.h"
#include "vidsch.h"

#define DXGKP_PAGING_MAX_PASSES         256
#define DXGKP_PAGING_MIN_BUFFER_BYTES   PAGE_SIZE
#define DXGKP_PAGING_MAX_BUFFER_BYTES   (1024 * 1024)
#define DXGKP_PAGING_SYNC_TIMEOUT_MS    2000

static ULONG
DxgkpPagingBufferBytes(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_SEGMENT Segments;
    ULONG Bytes = DXGKP_PAGING_MIN_BUFFER_BYTES;

    Segments = (PDXGKRNL_SEGMENT)Adapter->Segments;
    if (Segments != NULL && Adapter->SegmentCount != 0 && Segments[0].PagingBufferSize != 0)
        Bytes = Segments[0].PagingBufferSize;
    if (Bytes < DXGKP_PAGING_MIN_BUFFER_BYTES)
        Bytes = DXGKP_PAGING_MIN_BUFFER_BYTES;
    if (Bytes > DXGKP_PAGING_MAX_BUFFER_BYTES)
        Bytes = DXGKP_PAGING_MAX_BUFFER_BYTES;
    return Bytes;
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
static BOOLEAN
DxgkpPagingMonitoredFenceSignalSupported(
    _In_opt_ PDXGKRNL_ADAPTER Adapter)
{
    return Adapter != NULL &&
           Adapter->MiniportContext != NULL &&
           !Adapter->MiniportContext->UseDodLayout &&
           DxgkPagingCoreShouldAppendMonitoredSignal(
               REACTOS_WDDM_TARGET_LEVEL,
               DxgkCapsCoreInterfaceVersionToLevel(
                   Adapter->MiniportContext->InitData.s.Version),
               TRUE,
               TRUE);
}
#endif

static VOID
DxgkpPagingFillBuildArgs(
    _In_ CONST DXGKRNL_PAGING_OP *Op,
    _In_ BOOLEAN FirstPass,
    _Inout_ DXGKARG_BUILDPAGINGBUFFER *BuildArgs)
{
    switch (Op->Type)
    {
        case DxgkPagingOpTransfer:
            BuildArgs->Operation = DXGK_OPERATION_TRANSFER;
            /* The first pass of a multipass transfer is its start boundary;
             * the miniport reports the end by completing the build. */
            BuildArgs->Transfer.Flags.TransferStart = FirstPass ? 1 : 0;
            BuildArgs->Transfer.Flags.AllocationIsIdle = Op->AllocationIsIdle ? 1 : 0;
            BuildArgs->Transfer.hAllocation = Op->hMiniportAllocation;
            BuildArgs->Transfer.TransferOffset = Op->TransferOffset;
            BuildArgs->Transfer.TransferSize = Op->TransferSize;
            BuildArgs->Transfer.Source.SegmentId = Op->SourceSegmentId;
            if (Op->SourceSegmentId == 0)
                BuildArgs->Transfer.Source.pMdl = Op->SourceMdl;
            else
                BuildArgs->Transfer.Source.SegmentAddress = Op->SourceSegmentAddress;
            BuildArgs->Transfer.Destination.SegmentId = Op->DestinationSegmentId;
            if (Op->DestinationSegmentId == 0)
                BuildArgs->Transfer.Destination.pMdl = Op->DestinationMdl;
            else
                BuildArgs->Transfer.Destination.SegmentAddress = Op->DestinationSegmentAddress;
            BuildArgs->Transfer.MdlOffset = Op->MdlOffset;
            break;

        case DxgkPagingOpFill:
            BuildArgs->Operation = DXGK_OPERATION_FILL;
            BuildArgs->Fill.hAllocation = Op->hMiniportAllocation;
            BuildArgs->Fill.FillSize = Op->FillSize;
            BuildArgs->Fill.FillPattern = Op->FillPattern;
            BuildArgs->Fill.Destination.SegmentId = Op->DestinationSegmentId;
            BuildArgs->Fill.Destination.SegmentAddress = Op->DestinationSegmentAddress;
            break;

        case DxgkPagingOpDiscardContent:
            BuildArgs->Operation = DXGK_OPERATION_DISCARD_CONTENT;
            BuildArgs->DiscardContent.hAllocation = Op->hMiniportAllocation;
            BuildArgs->DiscardContent.Flags.AllocationIsIdle = Op->AllocationIsIdle ? 1 : 0;
            BuildArgs->DiscardContent.SegmentId = Op->DestinationSegmentId;
            BuildArgs->DiscardContent.SegmentAddress.QuadPart = Op->DestinationSegmentAddress.QuadPart;
            break;

        case DxgkPagingOpMapAperture:
            BuildArgs->Operation = DXGK_OPERATION_MAP_APERTURE_SEGMENT;
            BuildArgs->MapApertureSegment.hDevice = Op->hMiniportDevice;
            BuildArgs->MapApertureSegment.hAllocation = Op->hMiniportAllocation;
            BuildArgs->MapApertureSegment.SegmentId = Op->DestinationSegmentId;
            BuildArgs->MapApertureSegment.OffsetInPages = Op->OffsetInPages;
            BuildArgs->MapApertureSegment.NumberOfPages = Op->NumberOfPages;
            BuildArgs->MapApertureSegment.pMdl = Op->SourceMdl;
            BuildArgs->MapApertureSegment.MdlOffset = Op->MdlOffset;
            break;

        case DxgkPagingOpUnmapAperture:
            BuildArgs->Operation = DXGK_OPERATION_UNMAP_APERTURE_SEGMENT;
            BuildArgs->UnmapApertureSegment.hDevice = Op->hMiniportDevice;
            BuildArgs->UnmapApertureSegment.hAllocation = Op->hMiniportAllocation;
            BuildArgs->UnmapApertureSegment.SegmentId = Op->DestinationSegmentId;
            BuildArgs->UnmapApertureSegment.OffsetInPages = Op->OffsetInPages;
            BuildArgs->UnmapApertureSegment.NumberOfPages = Op->NumberOfPages;
            BuildArgs->UnmapApertureSegment.DummyPage = Op->DummyPage;
            break;

#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
        case DxgkPagingOpUpdatePageTable:
            BuildArgs->Operation = DXGK_OPERATION_UPDATE_PAGE_TABLE;
            BuildArgs->UpdatePageTable.PageTableLevel = Op->PageTableLevel;
            BuildArgs->UpdatePageTable.hAllocation = Op->hMiniportAllocation;
            BuildArgs->UpdatePageTable.PageTableAddress = Op->PageTableAddress;
            BuildArgs->UpdatePageTable.pPageTableEntries = Op->PageTableEntries;
            BuildArgs->UpdatePageTable.StartIndex = Op->StartIndex;
            BuildArgs->UpdatePageTable.NumPageTableEntries = Op->NumPageTableEntries;
            BuildArgs->UpdatePageTable.Flags.InitialUpdate = Op->InitialUpdate ? 1 : 0;
            BuildArgs->UpdatePageTable.AllocationOffsetInBytes = Op->AllocationOffsetInBytes;
            BuildArgs->UpdatePageTable.hProcess = Op->hMiniportProcess;
            BuildArgs->UpdatePageTable.UpdateMode = Op->UpdateMode;
            BuildArgs->UpdatePageTable.FirstPteVirtualAddress = Op->StartVirtualAddress;
            break;

        case DxgkPagingOpFlushTlb:
            BuildArgs->Operation = DXGK_OPERATION_FLUSH_TLB;
            BuildArgs->FlushTlb.RootPageTableAddress = Op->RootPageTableAddress;
            BuildArgs->FlushTlb.hProcess = Op->hMiniportProcess;
            BuildArgs->FlushTlb.StartVirtualAddress = Op->StartVirtualAddress;
            BuildArgs->FlushTlb.EndVirtualAddress = Op->EndVirtualAddress;
            break;

        case DxgkPagingOpNotifyResidency:
            BuildArgs->Operation = DXGK_OPERATION_NOTIFY_RESIDENCY;
            BuildArgs->NotifyResidency.hAllocation = Op->hMiniportAllocation;
            BuildArgs->NotifyResidency.PhysicalAddress = Op->NotifyPhysicalAddress;
            BuildArgs->NotifyResidency.Resident = Op->NotifyResident ? 1 : 0;
            break;
#endif

        default:
            break;
    }
}

BOOLEAN
DxgkPagingOperationSupported(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGKRNL_PAGING_OP_TYPE Type)
{
    if (Adapter == NULL || DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer) == NULL)
        return FALSE;
    switch (Type)
    {
        case DxgkPagingOpTransfer:
        case DxgkPagingOpFill:
        case DxgkPagingOpDiscardContent:
        case DxgkPagingOpMapAperture:
        case DxgkPagingOpUnmapAperture:
            return TRUE;
        case DxgkPagingOpUpdatePageTable:
        case DxgkPagingOpFlushTlb:
        case DxgkPagingOpNotifyResidency:
            return Adapter->MiniportContext != NULL &&
                   DxgkCapsCoreInterfaceVersionAtLeast(
                       Adapter->MiniportContext->InitData.s.Version,
                       DXGK_CAPS_CORE_LEVEL_WDDM_2_0);
        default:
            return FALSE;
    }
}

/*
 * DxgkPagingExecuteBatch
 *
 * Build an entire set of paging operations before admitting any packet to
 * the scheduler.  Every operation and every multipass fragment is packed into
 * one DMA buffer.  A build failure therefore frees an unsubmitted buffer; it
 * can never leave the caller trying to cancel an earlier packet from the same
 * residency transaction.
 *
 * All operations must target the same paging engine and miniport device,
 * because the scheduler metadata is attached once to the combined buffer.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkPagingExecuteBatch(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_reads_(OperationCount) CONST DXGKRNL_PAGING_OP *Operations,
    _In_ ULONG OperationCount,
    _In_reads_opt_(LifetimeAllocationReferenceCount)
        PDXGKVMM_ALLOCATION const *LifetimeAllocationReferences,
    _In_ UINT LifetimeAllocationReferenceCount,
    _In_ D3DKMT_HANDLE hSignalSyncObject,
    _In_ ULONG64 SignalFenceValue,
    _Out_opt_ PULONG OutPagingFenceId,
    _Out_opt_ PBOOLEAN OutQueued)
{
    PDXGKRNL_DMA_BUFFER DmaBuffer = NULL;
    DXGKRNL_TRACK_DMA_ARGS TrackArgs;
    PUCHAR FinalCursor = NULL;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    PVOID SignalSyncObjectReference = NULL;
    D3DGPU_VIRTUAL_ADDRESS SignalFenceGpuVa = 0;
    BOOLEAN BuildGpuSignal = FALSE;
    BOOLEAN SignalWrittenByGpu = FALSE;
#endif
    ULONG BufferBytes;
    ULONG LastFenceId = 0;
    ULONG OperationIndex;
    ULONG BytesUsed;
    NTSTATUS Status;

    PAGED_CODE();

    if (OutPagingFenceId != NULL)
        *OutPagingFenceId = 0;
    if (OutQueued != NULL)
        *OutQueued = FALSE;
    if (Adapter == NULL ||
        Operations == NULL ||
        OperationCount == 0 ||
        (LifetimeAllocationReferenceCount != 0 &&
         LifetimeAllocationReferences == NULL) ||
        (SIZE_T)LifetimeAllocationReferenceCount >
            MAXULONG_PTR / sizeof(*LifetimeAllocationReferences))
        return STATUS_INVALID_PARAMETER;
    if (Adapter->MiniportDeviceContext == NULL)
        return STATUS_DEVICE_NOT_READY;

#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    if (hSignalSyncObject != 0 &&
        DxgkpPagingMonitoredFenceSignalSupported(Adapter))
    {
        Status =
            DxgkSyncObjectReferenceTrackedSignal(
                hSignalSyncObject,
                Device,
                &SignalSyncObjectReference);
        if (!NT_SUCCESS(Status))
            return Status;

        Status =
            DxgkSyncObjectQueryTrackedSignalAddress(
                SignalSyncObjectReference,
                Device,
                &SignalFenceGpuVa);
        if (NT_SUCCESS(Status))
        {
            BuildGpuSignal = TRUE;
        }
        else if (Status != STATUS_NOT_SUPPORTED)
        {
            goto Cleanup;
        }
    }
#endif

    for (OperationIndex = 0; OperationIndex < OperationCount; ++OperationIndex)
    {
        if (!DxgkPagingOperationSupported(Adapter,
                                          Operations[OperationIndex].Type))
        {
            Status = STATUS_NOT_SUPPORTED;
            goto Cleanup;
        }
        if (Operations[OperationIndex].NodeOrdinal != Operations[0].NodeOrdinal ||
            Operations[OperationIndex].EngineOrdinal != Operations[0].EngineOrdinal ||
            Operations[OperationIndex].hMiniportDevice != Operations[0].hMiniportDevice)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
    }

    BufferBytes = DxgkpPagingBufferBytes(Adapter);
    if (OperationCount <=
        DXGKP_PAGING_MAX_BUFFER_BYTES / PAGE_SIZE
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
            - (BuildGpuSignal ? 1 : 0)
#endif
        )
    {
        ULONG Estimate =
            OperationCount
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
            + (BuildGpuSignal ? 1 : 0)
#endif
            ;

        Estimate *= PAGE_SIZE;

        if (Estimate > BufferBytes)
            BufferBytes = Estimate;
    }
    else
    {
        BufferBytes = DXGKP_PAGING_MAX_BUFFER_BYTES;
    }

    for (;;)
    {
        PUCHAR Cursor;
        PUCHAR End;
        BOOLEAN RetryLarger = FALSE;

        Status = DxgkAllocateDmaBuffer(Adapter, BufferBytes, &DmaBuffer);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Cursor = (PUCHAR)DmaBuffer->VirtualAddress;
        End = Cursor + DmaBuffer->Capacity;

        for (OperationIndex = 0;
             OperationIndex < OperationCount && !RetryLarger;
             ++OperationIndex)
        {
            ULONG MultipassOffset = 0;
            ULONG Pass;
            BOOLEAN Complete = FALSE;

            for (Pass = 0; Pass < DXGKP_PAGING_MAX_PASSES; ++Pass)
            {
                DXGKARG_BUILDPAGINGBUFFER BuildArgs;
                ULONG PreviousMultipassOffset = MultipassOffset;
                PUCHAR PreviousCursor = Cursor;
                NTSTATUS BuildStatus;

                RtlZeroMemory(&BuildArgs, sizeof(BuildArgs));
                BuildArgs.pDmaBuffer = Cursor;
                BuildArgs.DmaSize = (UINT)(End - Cursor);
                BuildArgs.MultipassOffset = MultipassOffset;
                DxgkpPagingFillBuildArgs(&Operations[OperationIndex],
                                         Pass == 0,
                                         &BuildArgs);

                if (!DxgkAcquireKmdCall(Adapter))
                {
                    Status = STATUS_DELETE_PENDING;
                    goto Cleanup;
                }
                _SEH2_TRY
                {
                    BuildStatus =
                        DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer)(
                            Adapter->MiniportDeviceContext,
                            &BuildArgs);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    BuildStatus = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                DxgkReleaseKmdCall(Adapter);

                if (!NT_SUCCESS(BuildStatus) &&
                    BuildStatus != STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER)
                {
                    Status = BuildStatus;
                    goto Cleanup;
                }
                if (BuildArgs.pDmaBuffer == NULL ||
                    (PUCHAR)BuildArgs.pDmaBuffer < PreviousCursor ||
                    (PUCHAR)BuildArgs.pDmaBuffer > End)
                {
                    Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
                    goto Cleanup;
                }

                Cursor = (PUCHAR)BuildArgs.pDmaBuffer;
                MultipassOffset = BuildArgs.MultipassOffset;
                Complete = NT_SUCCESS(BuildStatus);
                if (Complete)
                    break;

                if (Cursor == PreviousCursor &&
                    MultipassOffset == PreviousMultipassOffset)
                {
                    RetryLarger = TRUE;
                    break;
                }
            }

            if (!RetryLarger && !Complete)
            {
                Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
                goto Cleanup;
            }
        }

#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
        if (BuildGpuSignal && !RetryLarger)
        {
            ULONG MultipassOffset = 0;
            ULONG Pass;
            BOOLEAN Complete = FALSE;
            PUCHAR SignalStartCursor = Cursor;

            Status =
                DxgkPagingCoreBeginMonitoredSignal(
                    OperationIndex == OperationCount);
            if (!NT_SUCCESS(Status))
                goto Cleanup;

            for (Pass = 0; Pass < DXGKP_PAGING_MAX_PASSES; ++Pass)
            {
                DXGKARG_BUILDPAGINGBUFFER BuildArgs;
                ULONG PreviousMultipassOffset = MultipassOffset;
                PUCHAR PreviousCursor = Cursor;
                NTSTATUS BuildStatus;

                RtlZeroMemory(&BuildArgs, sizeof(BuildArgs));
                BuildArgs.pDmaBuffer = Cursor;
                BuildArgs.DmaSize = (UINT)(End - Cursor);
                BuildArgs.MultipassOffset = MultipassOffset;
                BuildArgs.Operation =
                    DXGK_OPERATION_SIGNAL_MONITORED_FENCE;
                BuildArgs.SignalMonitoredFence.MonitoredFenceGpuVa =
                    SignalFenceGpuVa;
                BuildArgs.SignalMonitoredFence.MonitoredFenceValue =
                    SignalFenceValue;

                if (!DxgkAcquireKmdCall(Adapter))
                {
                    Status = STATUS_DELETE_PENDING;
                    goto Cleanup;
                }
                _SEH2_TRY
                {
                    BuildStatus =
                        DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer)(
                            Adapter->MiniportDeviceContext,
                            &BuildArgs);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    BuildStatus = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                DxgkReleaseKmdCall(Adapter);

                if (!NT_SUCCESS(BuildStatus) &&
                    BuildStatus !=
                        STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER)
                {
                    Status = BuildStatus;
                    goto Cleanup;
                }
                if (BuildArgs.pDmaBuffer == NULL ||
                    (PUCHAR)BuildArgs.pDmaBuffer < PreviousCursor ||
                    (PUCHAR)BuildArgs.pDmaBuffer > End)
                {
                    Status =
                        STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
                    goto Cleanup;
                }

                Cursor = (PUCHAR)BuildArgs.pDmaBuffer;
                MultipassOffset = BuildArgs.MultipassOffset;
                Complete = NT_SUCCESS(BuildStatus);
                if (Complete)
                    break;

                if (Cursor == PreviousCursor &&
                    MultipassOffset == PreviousMultipassOffset)
                {
                    RetryLarger = TRUE;
                    break;
                }
            }

            if (!RetryLarger && !Complete)
            {
                Status =
                    STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
                goto Cleanup;
            }

            /*
             * A successful build that emitted no bytes did not put a GPU
             * write in the stream.  Keep the ordinary retire-time CPU
             * publication enabled instead of claiming a GPU-written fence.
             */
            if (!RetryLarger)
            {
                Status =
                    DxgkPagingCoreFinishMonitoredSignal(
                        STATUS_SUCCESS,
                        (ULONG)(Cursor - SignalStartCursor),
                        &SignalWrittenByGpu);
                if (!NT_SUCCESS(Status))
                    goto Cleanup;
            }
        }
#endif

        if (!RetryLarger)
        {
            FinalCursor = Cursor;
            break;
        }
        DxgkFreeDmaBuffer(DmaBuffer);
        DmaBuffer = NULL;
        if (BufferBytes >= DXGKP_PAGING_MAX_BUFFER_BYTES)
        {
            Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            goto Cleanup;
        }
        BufferBytes = min(BufferBytes * 2,
                          (ULONG)DXGKP_PAGING_MAX_BUFFER_BYTES);
    }

    ASSERT(FinalCursor != NULL);
    BytesUsed = (ULONG)(FinalCursor - (PUCHAR)DmaBuffer->VirtualAddress);
    if (BytesUsed == 0)
    {
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
        if (DxgkPagingCoreNoWorkSignalRoute(
                hSignalSyncObject != 0,
                SignalSyncObjectReference != NULL) ==
            DxgkPagingNoWorkSignalRetainedReference)
        {
            DxgkSyncObjectPublishTrackedSignal(
                SignalSyncObjectReference,
                SignalFenceValue);
            Status = STATUS_SUCCESS;
        }
        else
#endif
        {
            Status = hSignalSyncObject != 0
                       ? DxgkSyncObjectGpuRetireSignal(
                             hSignalSyncObject,
                             SignalFenceValue)
                       : STATUS_SUCCESS;
        }
        goto Cleanup;
    }

    DmaBuffer->SubmissionStartOffset = 0;
    DmaBuffer->SubmissionEndOffset = BytesUsed;
    RtlZeroMemory(&TrackArgs, sizeof(TrackArgs));
    TrackArgs.Device = Device;
    TrackArgs.hSignalSyncObject = hSignalSyncObject;
    TrackArgs.SignalFenceValue = SignalFenceValue;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    TrackArgs.SignalSyncObjectReference =
        SignalSyncObjectReference;
    TrackArgs.SignalWrittenByGpu = SignalWrittenByGpu;
#endif
    TrackArgs.LifetimeAllocationReferences =
        LifetimeAllocationReferences;
    TrackArgs.LifetimeAllocationReferenceCount =
        LifetimeAllocationReferenceCount;
    Status = VidSchSubmitCommandTracked(Adapter,
                                        Operations[0].NodeOrdinal,
                                        Operations[0].EngineOrdinal,
                                        DmaBuffer,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        Operations[0].hMiniportDevice,
                                        NULL,
                                        0,
                                        &TrackArgs,
                                        VIDSCH_SUBMITFLAG_PAGING,
                                        0,
                                        &LastFenceId);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    DmaBuffer = NULL;
    if (OutPagingFenceId != NULL)
        *OutPagingFenceId = LastFenceId;
    if (OutQueued != NULL)
        *OutQueued = TRUE;
    Status = STATUS_SUCCESS;

Cleanup:
    if (DmaBuffer != NULL)
        DxgkFreeDmaBuffer(DmaBuffer);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    if (SignalSyncObjectReference != NULL)
    {
        DxgkSyncObjectReleaseTrackedSignal(
            SignalSyncObjectReference,
            FALSE,
            SignalFenceValue);
    }
#endif
    return Status;
}

/*
 * DxgkPagingExecute
 *
 * Runs one paging operation to a single terminal edge.
 *
 * The multipass protocol is the documented one: DxgkDdiBuildPagingBuffer is
 * called with the running MultipassOffset until it reports the operation is
 * fully described.  Every pass that produced bytes is submitted as a tracked
 * paging packet in adapter fence order, and only the last submitted packet
 * carries the caller's paging fence, so the fence retires exactly once and
 * only after all passes have executed.
 *
 * On success *OutPagingFenceId receives the adapter-wide fence the caller's
 * work must wait for, or 0 when the operation completed with no GPU work.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkPagingExecute(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_ CONST DXGKRNL_PAGING_OP *Op,
    _In_ D3DKMT_HANDLE hSignalSyncObject,
    _In_ ULONG64 SignalFenceValue,
    _Out_opt_ PULONG OutPagingFenceId)
{
    PDXGKRNL_DMA_BUFFER DmaBuffer = NULL;
    PDXGKRNL_DMA_BUFFER PendingBuffer = NULL;
    DXGKARG_BUILDPAGINGBUFFER BuildArgs;
    DXGKRNL_TRACK_DMA_ARGS TrackArgs;
    ULONG BufferBytes;
    ULONG MultipassOffset = 0;
    ULONG PendingBytes = 0;
    ULONG LastFenceId = 0;
    ULONG Pass;
    NTSTATUS Status;
    NTSTATUS BuildStatus;
    BOOLEAN Complete = FALSE;

    PAGED_CODE();

    if (OutPagingFenceId != NULL)
        *OutPagingFenceId = 0;
    if (Adapter == NULL || Op == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!DxgkPagingOperationSupported(Adapter, Op->Type))
        return STATUS_NOT_SUPPORTED;
    if (Adapter->MiniportDeviceContext == NULL)
        return STATUS_DEVICE_NOT_READY;

#if (REACTOS_WDDM_TARGET_LEVEL >= 2200)
    if (hSignalSyncObject != 0 &&
        DxgkpPagingMonitoredFenceSignalSupported(Adapter))
    {
        BOOLEAN Queued;

        return DxgkPagingExecuteBatch(
                   Adapter,
                   Device,
                   Op,
                   1,
                   NULL,
                   0,
                   hSignalSyncObject,
                   SignalFenceValue,
                   OutPagingFenceId,
                   &Queued);
    }
#endif

    BufferBytes = DxgkpPagingBufferBytes(Adapter);

    for (Pass = 0; Pass < DXGKP_PAGING_MAX_PASSES && !Complete; Pass++)
    {
        ULONG PreviousMultipassOffset = MultipassOffset;
        ULONG BytesUsed;

        Status = DxgkAllocateDmaBuffer(Adapter, BufferBytes, &DmaBuffer);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        RtlZeroMemory(&BuildArgs, sizeof(BuildArgs));
        BuildArgs.pDmaBuffer = DmaBuffer->VirtualAddress;
        BuildArgs.DmaSize = DmaBuffer->Capacity;
        BuildArgs.MultipassOffset = MultipassOffset;
        DxgkpPagingFillBuildArgs(Op, Pass == 0, &BuildArgs);

        if (!DxgkAcquireKmdCall(Adapter))
        {
            Status = STATUS_DELETE_PENDING;
            goto Cleanup;
        }
        _SEH2_TRY
        {
            BuildStatus = DXGK_CB_FULL(Adapter, DxgkDdiBuildPagingBuffer)(Adapter->MiniportDeviceContext, &BuildArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            BuildStatus = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseKmdCall(Adapter);

        if (!NT_SUCCESS(BuildStatus) && BuildStatus != STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER)
        {
            Status = BuildStatus;
            goto Cleanup;
        }

        if (BuildArgs.pDmaBuffer == NULL ||
            (PUCHAR)BuildArgs.pDmaBuffer < (PUCHAR)DmaBuffer->VirtualAddress ||
            (PUCHAR)BuildArgs.pDmaBuffer > (PUCHAR)DmaBuffer->VirtualAddress + DmaBuffer->Capacity)
        {
            Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            goto Cleanup;
        }
        BytesUsed = (ULONG)((PUCHAR)BuildArgs.pDmaBuffer - (PUCHAR)DmaBuffer->VirtualAddress);
        MultipassOffset = BuildArgs.MultipassOffset;
        Complete = NT_SUCCESS(BuildStatus);

        if (BytesUsed == 0)
        {
            DxgkFreeDmaBuffer(DmaBuffer);
            DmaBuffer = NULL;
            if (Complete)
                break;
            /* No progress and no bytes: the pass cannot be described in this
             * buffer, so grow once and retry rather than spin forever. */
            if (MultipassOffset == PreviousMultipassOffset)
            {
                if (BufferBytes >= DXGKP_PAGING_MAX_BUFFER_BYTES)
                {
                    Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
                    goto Cleanup;
                }
                BufferBytes = min(BufferBytes * 2, (ULONG)DXGKP_PAGING_MAX_BUFFER_BYTES);
            }
            continue;
        }

        if (BytesUsed > DmaBuffer->Capacity)
        {
            Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            goto Cleanup;
        }
        DmaBuffer->SubmissionStartOffset = 0;
        DmaBuffer->SubmissionEndOffset = BytesUsed;

        /* Hold this pass back one iteration: only the final pass may carry
         * the caller's paging fence, and the final pass is not known until
         * the build reports completion. */
        if (PendingBuffer != NULL)
        {
            RtlZeroMemory(&TrackArgs, sizeof(TrackArgs));
            TrackArgs.Device = Device;
            Status = VidSchSubmitCommandTracked(Adapter, Op->NodeOrdinal, Op->EngineOrdinal, PendingBuffer, NULL, 0, NULL, 0, NULL, 0, Op->hMiniportDevice, NULL, 0, &TrackArgs, VIDSCH_SUBMITFLAG_PAGING, 0, &LastFenceId);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
            PendingBuffer = NULL;
        }
        PendingBuffer = DmaBuffer;
        PendingBytes = BytesUsed;
        DmaBuffer = NULL;
    }

    if (!Complete)
    {
        Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        goto Cleanup;
    }

    if (PendingBuffer == NULL)
    {
        /* The miniport described no hardware work, so the operation is
         * already finished; publish the caller's fence synchronously. */
        Status = hSignalSyncObject != 0 ? DxgkSyncObjectGpuRetireSignal(hSignalSyncObject, SignalFenceValue) : STATUS_SUCCESS;
        if (OutPagingFenceId != NULL)
            *OutPagingFenceId = LastFenceId;
        return Status;
    }

    UNREFERENCED_PARAMETER(PendingBytes);
    RtlZeroMemory(&TrackArgs, sizeof(TrackArgs));
    TrackArgs.Device = Device;
    TrackArgs.hSignalSyncObject = hSignalSyncObject;
    TrackArgs.SignalFenceValue = SignalFenceValue;
    Status = VidSchSubmitCommandTracked(Adapter, Op->NodeOrdinal, Op->EngineOrdinal, PendingBuffer, NULL, 0, NULL, 0, NULL, 0, Op->hMiniportDevice, NULL, 0, &TrackArgs, VIDSCH_SUBMITFLAG_PAGING, 0, &LastFenceId);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    PendingBuffer = NULL;
    if (OutPagingFenceId != NULL)
        *OutPagingFenceId = LastFenceId;
    return STATUS_SUCCESS;

Cleanup:
    if (DmaBuffer != NULL)
        DxgkFreeDmaBuffer(DmaBuffer);
    if (PendingBuffer != NULL)
        DxgkFreeDmaBuffer(PendingBuffer);
    return Status;
}

/*
 * DxgkPagingWaitForFence
 *
 * Bounded wait for a paging packet to retire.  Retirement is normally driven
 * by the completion DPC; this also pumps the retirement list so a caller that
 * blocks here cannot deadlock behind a DPC that already ran.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkPagingWaitForFence(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG PagingFenceId,
    _In_ ULONG TimeoutMs)
{
    LARGE_INTEGER Interval;
    ULONG Waited;

    PAGED_CODE();
    if (PagingFenceId == 0)
        return STATUS_SUCCESS;
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    Interval.QuadPart = -1000LL;
    for (Waited = 0; Waited <= TimeoutMs * 10; Waited++)
    {
        DxgkRetireCompletedDmaBuffers(Adapter);
        if (DxgkPagingFenceCompleted(Adapter, PagingFenceId))
            return STATUS_SUCCESS;
        if (InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0)
            return STATUS_DEVICE_REMOVED;
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }
    return STATUS_TIMEOUT;
}

/*
 * DxgkPagingExecuteSynchronous
 *
 * Runs a paging operation and waits for it, for the internal residency paths
 * whose callers hold placement state and must observe the finished move.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkPagingExecuteSynchronous(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_ CONST DXGKRNL_PAGING_OP *Op)
{
    ULONG FenceId = 0;
    NTSTATUS Status;

    PAGED_CODE();
    Status = DxgkPagingExecute(Adapter, Device, Op, 0, 0, &FenceId);
    if (!NT_SUCCESS(Status))
        return Status;
    return DxgkPagingWaitForFence(Adapter, FenceId, DXGKP_PAGING_SYNC_TIMEOUT_MS);
}

/* ========================================================================
 * Placement publication
 *
 * A paging packet that moves an allocation does not take effect until it
 * retires.  The allocation therefore records the pending placement plus the
 * adapter fence that establishes it, and only publishes it once the adapter's
 * completed-fence watermark has passed.
 * ====================================================================== */

BOOLEAN
DxgkPagingFenceCompleted(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG PagingFenceId)
{
    ULONG Completed;

    if (PagingFenceId == 0)
        return TRUE;
    if (Adapter == NULL)
        return FALSE;
    Completed = (ULONG)InterlockedCompareExchange((volatile LONG *)&Adapter->LastCompletedSubmissionFenceId, 0, 0);
    return (LONG)(Completed - PagingFenceId) >= 0;
}

VOID
DxgkPagingBeginPlacement(
    _Inout_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONG SegmentId,
    _In_ ULONGLONG SegmentOffset,
    _In_ ULONG PagingFenceId)
{
    if (Allocation == NULL)
        return;
    Allocation->PendingSegmentId = SegmentId;
    Allocation->PendingSegmentOffset = SegmentOffset;
    Allocation->PagingFenceId = PagingFenceId;
    Allocation->PendingPlacement = PagingFenceId != 0;
}

BOOLEAN
DxgkPagingSyncPlacement(
    _Inout_ PDXGKVMM_ALLOCATION Allocation)
{
    if (Allocation == NULL)
        return TRUE;
    if (!Allocation->PendingPlacement)
        return TRUE;
    if (!DxgkPagingFenceCompleted(Allocation->Adapter, Allocation->PagingFenceId))
        return FALSE;
    Allocation->PendingPlacement = FALSE;
    Allocation->PagingFenceId = 0;
    return TRUE;
}

VOID
DxgkPagingAbandonPlacement(
    _Inout_ PDXGKVMM_ALLOCATION Allocation)
{
    if (Allocation == NULL)
        return;
    Allocation->PendingPlacement = FALSE;
    Allocation->PendingSegmentId = 0;
    Allocation->PendingSegmentOffset = 0;
    Allocation->PagingFenceId = 0;
}

/*
 * DxgkPagingAllocationReadyForSubmission
 *
 * Scheduler admission predicate: an allocation whose paging packet has not
 * retired cannot back GPU work yet.
 */
BOOLEAN
DxgkPagingAllocationReadyForSubmission(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    if (Allocation == NULL)
        return TRUE;
    if (!Allocation->PendingPlacement)
        return TRUE;
    return DxgkPagingFenceCompleted(Allocation->Adapter, Allocation->PagingFenceId);
}

/* EOF */
