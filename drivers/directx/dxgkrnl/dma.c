/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU command buffer / DMA submission
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * DxgkRender — translates a context command buffer through DxgkDdiRender
 *              and submits it as tracked GPU work.
 * DxgkPresent — present surface to display via the present queue.
 *
 * The present path validates parameters, builds a DXGKRNL_PRESENT_ENTRY
 * descriptor, and hands it off to the present queue (present.c).  The
 * queue handles VSync synchronisation and dispatches to either the DOD
 * (DxgkDdiPresentDisplayOnly) or full WDDM (DxgkDdiPresent) miniport
 * callback.
 */

#include "dxgkrnl_private.h"
#include "present.h"
#include "vidmm.h"
#include "vidpn.h"
#include "vidsch.h"

/* ========================================================================
 * Render ring
 *
 * The WDDM render contract gives the user-mode driver a command buffer, an
 * allocation list, and a patch-location list that dxgkrnl owns.  One backing
 * block holds all three and is mapped into the creating process exactly once;
 * D3DKMTRender translates its contents into a kernel DMA buffer through
 * DxgkDdiRender and then republishes the same mapping for the next frame.
 * ====================================================================== */

#define DXGKP_RENDER_MIN_COMMAND_BYTES  PAGE_SIZE
#define DXGKP_RENDER_MAX_PRIVATE_DATA   (64 * 1024)
#define DXGKP_RENDER_MAX_COMMAND_BYTES  (1024 * 1024)

VOID
DxgkContextRenderTeardown(
    _Inout_ PDXGKRNL_CONTEXT Context)
{
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;

    PAGED_CODE();
    if (Context == NULL || Context->RenderRingKernel == NULL)
        return;

    if (Context->RenderRingUser != NULL && Context->RenderRingMdl != NULL)
    {
        if (Context->RenderRingProcess != NULL && Context->RenderRingProcess != PsGetCurrentProcess())
        {
            KeStackAttachProcess(Context->RenderRingProcess, &ApcState);
            Attached = TRUE;
        }
        MmUnmapLockedPages(Context->RenderRingUser, Context->RenderRingMdl);
        if (Attached)
            KeUnstackDetachProcess(&ApcState);
        Context->RenderRingUser = NULL;
    }
    if (Context->RenderRingMdl != NULL)
    {
        IoFreeMdl(Context->RenderRingMdl);
        Context->RenderRingMdl = NULL;
    }
    if (Context->RenderRingProcess != NULL)
    {
        ObDereferenceObject(Context->RenderRingProcess);
        Context->RenderRingProcess = NULL;
    }
    ExFreePoolWithTag(Context->RenderRingKernel, TAG_DXGK_CONTEXT);
    Context->RenderRingKernel = NULL;
    Context->RenderRingBytes = 0;
    Context->RenderCommandBufferSize = 0;
    Context->RenderAllocationListSize = 0;
    Context->RenderPatchLocationListSize = 0;
}

/*
 * DxgkContextRenderInitialize
 *
 * Builds and publishes the render ring for a freshly created context.  The
 * geometry follows the miniport's DXGK_CONTEXTINFO, clamped to what this
 * scheduler's inline packet capacity can actually submit, so a request that
 * is accepted here can always be submitted later.
 *
 * IRQL: PASSIVE_LEVEL, in the creating process context.
 */
NTSTATUS
DxgkContextRenderInitialize(
    _Inout_ PDXGKRNL_CONTEXT Context)
{
    SIZE_T TotalBytes;
    ULONG CommandBytes;
    ULONG AllocationCount;
    ULONG PatchCount;
    PMDL Mdl;
    PVOID Kernel;
    PVOID User = NULL;

    PAGED_CODE();
    if (Context == NULL)
        return STATUS_INVALID_PARAMETER;

    CommandBytes = Context->ContextInfo.DmaBufferSize;
    if (CommandBytes < DXGKP_RENDER_MIN_COMMAND_BYTES)
        CommandBytes = DXGKP_RENDER_MIN_COMMAND_BYTES;
    if (CommandBytes > DXGKP_RENDER_MAX_COMMAND_BYTES)
        CommandBytes = DXGKP_RENDER_MAX_COMMAND_BYTES;
    CommandBytes = (ULONG)ROUND_TO_PAGES(CommandBytes);

    AllocationCount = Context->ContextInfo.AllocationListSize;
    if (AllocationCount == 0 || AllocationCount > VIDSCH_INLINE_ALLOCATIONS)
        AllocationCount = VIDSCH_INLINE_ALLOCATIONS;
    PatchCount = Context->ContextInfo.PatchLocationListSize;
    if (PatchCount == 0 || PatchCount > VIDSCH_INLINE_PATCHES)
        PatchCount = VIDSCH_INLINE_PATCHES;

    Context->RenderAllocationListOffset = CommandBytes;
    Context->RenderPatchLocationListOffset = CommandBytes + AllocationCount * (ULONG)sizeof(D3DDDI_ALLOCATIONLIST);
    TotalBytes = (SIZE_T)Context->RenderPatchLocationListOffset + (SIZE_T)PatchCount * sizeof(D3DDDI_PATCHLOCATIONLIST);
    TotalBytes = ROUND_TO_PAGES(TotalBytes);

    Kernel = ExAllocatePoolWithTag(NonPagedPool, TotalBytes, TAG_DXGK_CONTEXT);
    if (Kernel == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Kernel, TotalBytes);

    Mdl = IoAllocateMdl(Kernel, (ULONG)TotalBytes, FALSE, FALSE, NULL);
    if (Mdl == NULL)
    {
        ExFreePoolWithTag(Kernel, TAG_DXGK_CONTEXT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    MmBuildMdlForNonPagedPool(Mdl);

    _SEH2_TRY
    {
        User = MmMapLockedPagesSpecifyCache(Mdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        User = NULL;
    }
    _SEH2_END;
    if (User == NULL)
    {
        IoFreeMdl(Mdl);
        ExFreePoolWithTag(Kernel, TAG_DXGK_CONTEXT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Context->RenderRingKernel = Kernel;
    Context->RenderRingUser = User;
    Context->RenderRingMdl = Mdl;
    Context->RenderRingBytes = TotalBytes;
    Context->RenderCommandBufferSize = CommandBytes;
    Context->RenderAllocationListSize = AllocationCount;
    Context->RenderPatchLocationListSize = PatchCount;
    Context->RenderRingProcess = PsGetCurrentProcess();
    ObReferenceObject(Context->RenderRingProcess);
    return STATUS_SUCCESS;
}

static VOID
DxgkpRenderPublishRing(
    _In_ PDXGKRNL_CONTEXT Context,
    _Inout_ D3DKMT_RENDER *pRender)
{
    pRender->pNewCommandBuffer = Context->RenderRingUser;
    pRender->NewCommandBufferSize = Context->RenderCommandBufferSize;
    pRender->pNewAllocationList = (D3DDDI_ALLOCATIONLIST *)((PUCHAR)Context->RenderRingUser + Context->RenderAllocationListOffset);
    pRender->NewAllocationListSize = Context->RenderAllocationListSize;
    pRender->pNewPatchLocationList = (D3DDDI_PATCHLOCATIONLIST *)((PUCHAR)Context->RenderRingUser + Context->RenderPatchLocationListOffset);
    pRender->NewPatchLocationListSize = Context->RenderPatchLocationListSize;
    pRender->NewCommandBuffer = 0;
}

/* ========================================================================
 * DxgkRender
 *
 * D3DKMTRender -- translates the context's command buffer into a DMA buffer
 * through DxgkDdiRender and submits it as tracked GPU work.
 *
 * Every allocation the request names is referenced and residency-pinned
 * before translation, so the placement DxgkDdiPatch writes at kick time is
 * the one that was validated here and cannot be evicted underneath the
 * packet.  The pins are owned by the submission and released exactly once at
 * its terminal edge.  A zero-length render carries no GPU work and is
 * acknowledged after validation.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
NTAPI
DxgkRender(
    _Inout_ D3DKMT_RENDER *pRender)
{
    PDXGKRNL_ADAPTER Adapter = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKRNL_CONTEXT Context;
    PDXGKRNL_DMA_BUFFER DmaBuffer = NULL;
    DXGKARG_RENDER RenderArgs;
    DXGKRNL_TRACK_DMA_ARGS TrackArgs;
    D3DDDI_ALLOCATIONLIST *UserAllocationList;
    D3DDDI_PATCHLOCATIONLIST *UserPatchList;
    D3DDDI_ALLOCATIONLIST CapturedAllocations[VIDSCH_INLINE_ALLOCATIONS];
    D3DDDI_PATCHLOCATIONLIST CapturedPatches[VIDSCH_INLINE_PATCHES];
    D3DDDI_PATCHLOCATIONLIST PatchOut[VIDSCH_INLINE_PATCHES];
    DXGK_ALLOCATIONLIST KernelAllocations[VIDSCH_INLINE_ALLOCATIONS];
    PDXGKVMM_ALLOCATION AllocationReferences[VIDSCH_INLINE_ALLOCATIONS];
    PDXGKVMM_ALLOCATION OpenBindings[VIDSCH_INLINE_ALLOCATIONS];
    PVOID DmaBufferPrivateData = NULL;
    ULONG DmaBufferPrivateDataSize = 0;
    UINT ReferencedCount = 0;
    UINT PatchOutCount = 0;
    UINT DmaBytesUsed = 0;
    UINT Index;
    ULONG VidSchFence = 0;
    NTSTATUS Status;
    BOOLEAN RingLocked = FALSE;
    BOOLEAN KmdTransaction = FALSE;

    PAGED_CODE();

    if (pRender == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pRender->hContext == 0)
        return STATUS_INVALID_HANDLE;

    if (pRender->Flags.RenderKm || pRender->Flags.RenderKmReadback)
        return STATUS_NOT_SUPPORTED;

    Context = DxgkLookupContextByHandle(pRender->hContext, &Adapter, &Device);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    if (Adapter == NULL || Device == NULL)
    {
        DxgkDereferenceContext(Context);
        return STATUS_INVALID_HANDLE;
    }
    if (Adapter->MiniportContext == NULL || Adapter->MiniportContext->UseDodLayout || Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        DxgkDereferenceContext(Context);
        return STATUS_NOT_SUPPORTED;
    }
    if (pRender->hDevice != 0 && pRender->hDevice != Device->Handle)
    {
        DxgkDereferenceContext(Context);
        return STATUS_INVALID_HANDLE;
    }
    if (Context->VirtualAddressing)
    {
        /* Virtual contexts submit through SubmitCommandVirtual, not here. */
        DxgkDereferenceContext(Context);
        return STATUS_INVALID_PARAMETER;
    }
    if (pRender->BroadcastContextCount != 0)
    {
        /* Broadcasting one command buffer to additional contexts is not
         * implemented; refuse rather than silently rendering to one. */
        DxgkDereferenceContext(Context);
        return STATUS_NOT_SUPPORTED;
    }

    DXGKRNL_TRACE("DxgkRender: hDevice=0x%X hContext=0x%X CmdBufSize=%u allocs=%u patches=%u\n",
                  pRender->hDevice,
                  pRender->hContext,
                  pRender->CommandLength,
                  pRender->AllocationCount,
                  pRender->PatchLocationCount);

    (VOID)KeWaitForSingleObject(&Context->RenderLock, Executive, KernelMode, FALSE, NULL);
    RingLocked = TRUE;

    if (Context->RenderRingKernel == NULL || Context->RenderRingUser == NULL)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }
    if (Context->RenderRingProcess != PsGetCurrentProcess())
    {
        Status = STATUS_ACCESS_DENIED;
        goto Cleanup;
    }
    if (pRender->AllocationCount > Context->RenderAllocationListSize ||
        pRender->PatchLocationCount > Context->RenderPatchLocationListSize)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    if (pRender->CommandLength > Context->RenderCommandBufferSize ||
        pRender->CommandOffset > Context->RenderCommandBufferSize ||
        pRender->CommandOffset + pRender->CommandLength > Context->RenderCommandBufferSize)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    if (pRender->CommandLength == 0)
    {
        DxgkpRenderPublishRing(Context, pRender);
        pRender->QueuedBufferCount = 0;
        Status = STATUS_SUCCESS;
        goto Cleanup;
    }

    if (DXGK_CB_FULL(Adapter, DxgkDdiRender) == NULL || DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand) == NULL)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    /* Snapshot the user-writable lists before validating them so no entry can
     * change between validation and use. */
    RtlZeroMemory(CapturedAllocations, sizeof(CapturedAllocations));
    RtlZeroMemory(CapturedPatches, sizeof(CapturedPatches));
    RtlZeroMemory(KernelAllocations, sizeof(KernelAllocations));
    RtlZeroMemory(AllocationReferences, sizeof(AllocationReferences));
    RtlZeroMemory(OpenBindings, sizeof(OpenBindings));
    UserAllocationList = (D3DDDI_ALLOCATIONLIST *)((PUCHAR)Context->RenderRingKernel + Context->RenderAllocationListOffset);
    UserPatchList = (D3DDDI_PATCHLOCATIONLIST *)((PUCHAR)Context->RenderRingKernel + Context->RenderPatchLocationListOffset);
    if (pRender->AllocationCount != 0)
        RtlCopyMemory(CapturedAllocations, UserAllocationList, pRender->AllocationCount * sizeof(*CapturedAllocations));
    if (pRender->PatchLocationCount != 0)
        RtlCopyMemory(CapturedPatches, UserPatchList, pRender->PatchLocationCount * sizeof(*CapturedPatches));

    for (Index = 0; Index < pRender->PatchLocationCount; ++Index)
    {
        if (CapturedPatches[Index].AllocationIndex >= pRender->AllocationCount)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
    }

    if (!DxgkBeginKmdTransaction(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    KmdTransaction = TRUE;
    if (InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE ||
        InterlockedCompareExchange(&Context->Destroying, 0, 0) != 0)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }

    for (Index = 0; Index < pRender->AllocationCount; ++Index)
    {
        PDXGKVMM_ALLOCATION Reference = NULL;
        PDXGKVMM_ALLOCATION Binding = NULL;

        Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)CapturedAllocations[Index].hAllocation, Adapter, Device, &Reference);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_HANDLE;
            goto Cleanup;
        }
        Status = DxgkVidMmAcquireSubmissionResidencyPin(Reference, Adapter, &KernelAllocations[Index]);
        if (!NT_SUCCESS(Status))
        {
            DxgkVidMmDereferenceAllocation(Reference);
            goto Cleanup;
        }
        KernelAllocations[Index].WriteOperation = CapturedAllocations[Index].WriteOperation;
        Status = DxgkVidMmReferenceOpenBinding((HANDLE)(ULONG_PTR)CapturedAllocations[Index].hAllocation, Adapter, Device, &KernelAllocations[Index].hDeviceSpecificAllocation, &Binding);
        if (!NT_SUCCESS(Status))
        {
            DxgkVidMmReleaseSubmissionResidencyPin(Reference);
            DxgkVidMmDereferenceAllocation(Reference);
            goto Cleanup;
        }
        AllocationReferences[Index] = Reference;
        OpenBindings[Index] = Binding;
        ReferencedCount++;
    }

    Status = DxgkAllocateDmaBuffer(Adapter, max(Context->ContextInfo.DmaBufferSize, pRender->CommandLength), &DmaBuffer);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* The miniport declares how much DMA-buffer private data it writes per
     * submission; give it exactly that, not a pointer-sized stack slot. */
    DmaBufferPrivateDataSize = Context->ContextInfo.DmaBufferPrivateDataSize;
    if (DmaBufferPrivateDataSize != 0)
    {
        if (DmaBufferPrivateDataSize > DXGKP_RENDER_MAX_PRIVATE_DATA)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        DmaBufferPrivateData = ExAllocatePoolWithTag(NonPagedPool, DmaBufferPrivateDataSize, TAG_DXGK_SUBMITDMA);
        if (DmaBufferPrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        RtlZeroMemory(DmaBufferPrivateData, DmaBufferPrivateDataSize);
    }

    RtlZeroMemory(&RenderArgs, sizeof(RenderArgs));
    RenderArgs.pCommand = (PUCHAR)Context->RenderRingKernel + pRender->CommandOffset;
    RenderArgs.CommandLength = pRender->CommandLength;
    RenderArgs.pDmaBuffer = DmaBuffer->VirtualAddress;
    RenderArgs.DmaSize = DmaBuffer->Capacity;
    RenderArgs.pDmaBufferPrivateData = DmaBufferPrivateData;
    RenderArgs.DmaBufferPrivateDataSize = DmaBufferPrivateDataSize;
    RenderArgs.pAllocationList = KernelAllocations;
    RenderArgs.AllocationListSize = pRender->AllocationCount;
    RenderArgs.pPatchLocationListIn = CapturedPatches;
    RenderArgs.PatchLocationListInSize = pRender->PatchLocationCount;
    RenderArgs.pPatchLocationListOut = PatchOut;
    RenderArgs.PatchLocationListOutSize = RTL_NUMBER_OF(PatchOut);
    RenderArgs.DmaBufferSegmentId = DmaBuffer->SegmentId;
    RenderArgs.DmaBufferPhysicalAddress = DmaBuffer->SegmentAddress;
    RtlZeroMemory(PatchOut, sizeof(PatchOut));

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiRender)(Context->hMiniportContext != NULL ? Context->hMiniportContext : Device->hMiniportDevice, &RenderArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (RenderArgs.pDmaBuffer == NULL ||
        (PUCHAR)RenderArgs.pDmaBuffer < (PUCHAR)DmaBuffer->VirtualAddress ||
        (PUCHAR)RenderArgs.pDmaBuffer > (PUCHAR)DmaBuffer->VirtualAddress + DmaBuffer->Capacity)
    {
        Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        goto Cleanup;
    }
    DmaBytesUsed = (UINT)((PUCHAR)RenderArgs.pDmaBuffer - (PUCHAR)DmaBuffer->VirtualAddress);
    if (RenderArgs.pPatchLocationListOut < PatchOut ||
        (SIZE_T)(RenderArgs.pPatchLocationListOut - PatchOut) > RTL_NUMBER_OF(PatchOut))
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    PatchOutCount = (UINT)(RenderArgs.pPatchLocationListOut - PatchOut);
    for (Index = 0; Index < PatchOutCount; ++Index)
    {
        UINT AllocationIndex = PatchOut[Index].AllocationIndex;

        if (AllocationIndex >= pRender->AllocationCount)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        /* DXGK_ALLOCATIONLIST carries no size, so the miniport cannot bound
         * the offset it will patch.  dxgkrnl owns the allocation and must
         * refuse an offset that would resolve outside it, otherwise a
         * user-mode driver could name a neighbouring allocation's memory. */
        if (AllocationReferences[AllocationIndex] == NULL ||
            PatchOut[Index].AllocationOffset >= AllocationReferences[AllocationIndex]->Size)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
    }
    if (DmaBytesUsed == 0)
    {
        /* The miniport translated the stream to no hardware work. */
        DxgkpRenderPublishRing(Context, pRender);
        pRender->QueuedBufferCount = 0;
        Status = STATUS_SUCCESS;
        goto Cleanup;
    }
    DmaBuffer->SubmissionStartOffset = 0;
    DmaBuffer->SubmissionEndOffset = DmaBytesUsed;

    RtlZeroMemory(&TrackArgs, sizeof(TrackArgs));
    TrackArgs.Device = Device;
    TrackArgs.Context = Context;
    TrackArgs.EnforceSubmissionQuota = TRUE;
    TrackArgs.AllocationReferences = AllocationReferences;
    TrackArgs.AllocationReferenceCount = ReferencedCount;
    TrackArgs.OpenBindingReferences = OpenBindings;
    TrackArgs.OpenBindingReferenceCount = ReferencedCount;

    Status = VidSchSubmitCommandTracked(Adapter,
                                        Context->NodeOrdinal,
                                        0,
                                        DmaBuffer,
                                        DmaBufferPrivateData,
                                        DmaBufferPrivateDataSize,
                                        KernelAllocations,
                                        pRender->AllocationCount,
                                        PatchOut,
                                        PatchOutCount,
                                        Adapter->SchedulingCaps.MultiEngineAware ? NULL : Device->hMiniportDevice,
                                        Adapter->SchedulingCaps.MultiEngineAware ? Context->hMiniportContext : NULL,
                                        Context->SchedulingPriority,
                                        &TrackArgs,
                                        0,
                                        0,
                                        &VidSchFence);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* The DMA buffer is now owned by the tracked submission, which took its
     * own allocation references and residency pins; the ones this call holds
     * are released below either way. */
    DmaBuffer = NULL;

    DxgkpRenderPublishRing(Context, pRender);
    pRender->QueuedBufferCount = 1;
    Status = STATUS_SUCCESS;

Cleanup:
    for (Index = 0; Index < ReferencedCount; ++Index)
    {
        if (OpenBindings[Index] != NULL)
            DxgkVidMmDereferenceLogicalAllocation(OpenBindings[Index]);
        if (AllocationReferences[Index] != NULL)
        {
            DxgkVidMmReleaseSubmissionResidencyPin(AllocationReferences[Index]);
            DxgkVidMmDereferenceAllocation(AllocationReferences[Index]);
        }
    }
    if (DmaBufferPrivateData != NULL)
        ExFreePoolWithTag(DmaBufferPrivateData, TAG_DXGK_SUBMITDMA);
    if (DmaBuffer != NULL)
        DxgkFreeDmaBuffer(DmaBuffer);
    if (KmdTransaction)
        DxgkEndKmdTransaction(Adapter);
    if (RingLocked)
        KeReleaseMutex(&Context->RenderLock, FALSE);
    DxgkDereferenceContext(Context);
    return Status;
}

/* ========================================================================
 * DxgkPresent
 *
 * D3DKMTPresent -- presents a rendered surface to the display.
 *
 * Validates the present parameters, determines the present type
 * (blt / flip / colour fill), and routes the operation through the
 * present queue in present.c.
 *
 * For display-only adapters with immediate flip interval, the present
 * is executed synchronously (the shadow FB is pushed to the GPU before
 * this function returns).  For full WDDM adapters or VSync-synchronized
 * presents, the operation is queued and processed at VSync time.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
NTAPI
DxgkPresent(
    _Inout_ D3DKMT_PRESENT *pPresent)
{
    PDXGKRNL_ADAPTER         Adapter = NULL;
    PDXGKRNL_DEVICE          Device = NULL;
    PDXGKRNL_CONTEXT         Context = NULL;
    DXGKRNL_PRESENT_ENTRY    Entry;
    ULONG64                  PresentId;
    NTSTATUS                 Status;
    BOOLEAN                  DestinationIsInternal = FALSE;

    PAGED_CODE();

    if (pPresent == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pPresent->hDevice == 0)
        return STATUS_INVALID_HANDLE;

    if (pPresent->Flags.Value == 0 && pPresent->hSource == 0)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&Entry, sizeof(Entry));

    Context = DxgkLookupContextByHandle(pPresent->hContext, &Adapter, &Device);
    if (Context != NULL)
    {
        if (!DxgkReferenceDevice(Device))
        {
            DxgkDereferenceContext(Context);
            return STATUS_DELETE_PENDING;
        }
    }
    else
    {
        Status = DxgkReferenceOwnedDeviceByHandle(pPresent->hDevice, PsGetCurrentProcess(), &Adapter, &Device);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Adapter->SchedulingCaps.MultiEngineAware)
        {
            DxgkDereferenceDevice(Device);
            return STATUS_INVALID_HANDLE;
        }
    }

    Entry.Context = Context;
    Entry.Device = Device;
    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0)
    {
        DxgkpReleasePresentEntry(&Entry);
        return STATUS_DELETE_PENDING;
    }

    Status = DxgkpAcquireSharedSurfaceSnapshot(Adapter, &Entry.SharedSurface);
    if (!NT_SUCCESS(Status))
    {
        DxgkpReleasePresentEntry(&Entry);
        return Status;
    }

    /* --- Build the present entry ---------------------------------------- */

    Entry.hSource        = pPresent->hSource;
    Entry.hDestination   = pPresent->hDestination;
    Entry.Color          = pPresent->Color;
    Entry.FlipInterval   = pPresent->FlipInterval;

    /*
     * Determine the VidPn source.  If RestrictVidPnSource is set, use the
     * caller-specified VidPnSourceId.  Otherwise default to source 0
     * (the primary display).
     */
    if (pPresent->Flags.RestrictVidPnSource)
        Entry.VidPnSourceId = pPresent->VidPnSourceId;
    else
        Entry.VidPnSourceId = 0;

    /* Source rectangle. */
    if (pPresent->Flags.SrcRectValid)
    {
        Entry.SrcRect = pPresent->SrcRect;
    }
    else
    {
        /* Default to the full committed display resolution. */
        Entry.SrcRect.left   = 0;
        Entry.SrcRect.top    = 0;
        Entry.SrcRect.right  = (LONG)Entry.SharedSurface.CommittedWidth;
        Entry.SrcRect.bottom = (LONG)Entry.SharedSurface.CommittedHeight;
    }

    /* Destination rectangle. */
    if (pPresent->Flags.DstRectValid)
    {
        Entry.DstRect = pPresent->DstRect;
    }
    else
    {
        Entry.DstRect.left   = 0;
        Entry.DstRect.top    = 0;
        Entry.DstRect.right  = (LONG)Entry.SharedSurface.CommittedWidth;
        Entry.DstRect.bottom = (LONG)Entry.SharedSurface.CommittedHeight;
    }

    /*
     * Determine the present type from the D3DKMT_PRESENTFLAGS.
     *
     * Priority order (matching Windows behaviour):
     *   1. Flip — hardware page flip (if supported).
     *   2. ColorFill — solid colour fill.
     *   3. Blt — default: blit from source to destination.
     */
    if (pPresent->Flags.Flip)
    {
        Entry.Type = DxgkPresentTypeFlip;
    }
    else if (pPresent->Flags.ColorFill)
    {
        Entry.Type = DxgkPresentTypeColorFill;
    }
    else
    {
        /* Default to blit present (covers Blt flag and unset flags). */
        Entry.Type = DxgkPresentTypeBlt;
    }

    /*
     * User-mode present callers may omit hDestination to mean "present to the
     * current primary". Route those blits to the shared-primary allocation
     * when dxgkrnl already exposed one.
     *
     * Phase-1 callers like dwm.exe can also still issue placeholder presents
     * before they have a real source allocation. Treat those as no-ops rather
     * than faulting the miniport with null allocation handles.
     */
    if (Entry.Type == DxgkPresentTypeBlt)
    {
        if (Entry.hSource == 0 && Entry.hDestination == 0)
        {
            DXGKRNL_WARN("DxgkPresent: rejecting source-less blt present "
                          "hDevice=0x%X hWindow=%p VidPn=%u rect=(%ld,%ld)-(%ld,%ld)\n",
                          pPresent->hDevice,
                          pPresent->hWindow,
                          Entry.VidPnSourceId,
                          Entry.DstRect.left,
                          Entry.DstRect.top,
                          Entry.DstRect.right,
                          Entry.DstRect.bottom);
            DxgkpReleasePresentEntry(&Entry);
            return STATUS_INVALID_PARAMETER;
        }

        if (Entry.hSource != 0 &&
            Entry.hDestination == 0 &&
            Entry.SharedSurface.PrimaryHandle != NULL &&
            DxgkpDeviceOwnsVidPnSource(Device->Handle, Entry.VidPnSourceId))
        {
            /*
             * Only the VidPn source owner may present straight to the primary.
             * A present with no destination from a non-owner is a windowed
             * present: it must succeed but must NOT scan its surface out to the
             * primary (which would paint over the live desktop).  Leaving
             * hDestination == 0 makes the present a no-op-to-screen below.
             * This mirrors the Windows model where DWM owns the primary and
             * composites app presents into their window.
             */
            Entry.hDestination = (D3DKMT_HANDLE)(ULONG_PTR)Entry.SharedSurface.PrimaryHandle;
            DestinationIsInternal = TRUE;
        }
    }

    if (Entry.hSource != 0)
    {
        Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)Entry.hSource, Adapter, Device, &Entry.SourceAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkpReleasePresentEntry(&Entry);
            return Status;
        }
        Status = DxgkVidMmReferenceOpenBinding((HANDLE)(ULONG_PTR)Entry.hSource, Adapter, Device, &Entry.SourceOpenBindingHandle, &Entry.SourceOpenBindingReference);
        if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND)
        {
            DxgkpReleasePresentEntry(&Entry);
            return Status;
        }
    }

    if (Entry.hDestination != 0)
    {
        Status = DxgkVidMmReferenceAllocation((HANDLE)(ULONG_PTR)Entry.hDestination, Adapter, DestinationIsInternal ? NULL : Device, &Entry.DestinationAllocation);
        if (!NT_SUCCESS(Status))
        {
            DxgkpReleasePresentEntry(&Entry);
            return Status;
        }
        if (!DestinationIsInternal)
        {
            Status = DxgkVidMmReferenceOpenBinding((HANDLE)(ULONG_PTR)Entry.hDestination, Adapter, Device, &Entry.DestinationOpenBindingHandle, &Entry.DestinationOpenBindingReference);
            if (!NT_SUCCESS(Status) && Status != STATUS_NOT_FOUND)
            {
                DxgkpReleasePresentEntry(&Entry);
                return Status;
            }
        }
    }

    Entry.SourceIsSharedPrimary = Entry.SourceAllocation != NULL && Entry.SourceAllocation == Entry.SharedSurface.PrimaryAllocation;
    Entry.SourceIsSharedShadow = Entry.SourceAllocation != NULL && Entry.SourceAllocation == Entry.SharedSurface.ShadowAllocation;
    Entry.DestinationIsSharedPrimary = Entry.DestinationAllocation != NULL && Entry.DestinationAllocation == Entry.SharedSurface.PrimaryAllocation;
    Entry.DestinationIsSharedShadow = Entry.DestinationAllocation != NULL && Entry.DestinationAllocation == Entry.SharedSurface.ShadowAllocation;
    if (!Adapter->MiniportContext->IsDisplayOnlyDriver && Entry.SharedSurface.ShadowFb == NULL && !Entry.SourceIsSharedPrimary && !Entry.SourceIsSharedShadow && !Entry.DestinationIsSharedPrimary && !Entry.DestinationIsSharedShadow)
        DxgkpReleaseSharedSurfaceSnapshot(&Entry.SharedSurface);

    /* --- Submit to the present queue ----------------------------------- */

    Status = DxgkpQueuePresent(Adapter, &Entry, &PresentId);

    if (Status == STATUS_DEVICE_BUSY)
    {
        DXGKRNL_WARN("DxgkPresent: queue full — present dropped "
                     "(PresentId=%llu)\n", PresentId);
    }
    else if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkPresent: DxgkpQueuePresent returned 0x%08lX\n",
                     Status);
    }

    return Status;
}
