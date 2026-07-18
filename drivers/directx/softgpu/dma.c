/*
 * PROJECT:     ReactOS WDDM Null/Software GPU Miniport
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     DMA command scheduling DDIs for softgpu.sys.
 *              Implements SubmitCommand, PreemptCommand, BuildPagingBuffer,
 *              QueryCurrentFence, Patch, and the fence-completion DPC.
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Architecture notes (amd64/x86)
 * ================================
 * SubmitCommand is called at DISPATCH_LEVEL.  It stores the fence ID under
 * FenceLock (KSPIN_LOCK) and queues the per-device DPC.
 *
 * SoftGpuDpcRoutine fires at DISPATCH_LEVEL.  It:
 *   1. Acquires FenceLock.
 *   2. Copies CurrentFence -> CompletedFence.
 *   3. Releases FenceLock.
 *   4. Calls DxgkCbNotifyInterrupt(DXGK_INTERRUPT_TYPE_DMA_COMPLETED).
 *   5. Calls DxgkCbNotifyDpc.
 *
 * Steps 4 and 5 must be called in this order per the WDDM contract.
 * DxgkCbNotifyInterrupt is called inside the conceptual "interrupt context"
 * (no real interrupt on softgpu) and DxgkCbNotifyDpc signals dxgkrnl to
 * wake up the scheduling thread.
 *
 * On x86 TSO the store to CompletedFence inside FenceLock is visible to
 * QueryCurrentFence (also inside FenceLock) without an explicit fence
 * instruction, but we keep the spinlock for correct IRQL management.
 */

/* INCLUDES ******************************************************************/

#include "softgpu.h"

#define SOFTGPU_TRACE_LOG_LIMIT  32
#define SOFTGPU_TRACE_SLOW_US    1000ULL

static volatile LONG g_SoftGpuSubmitTraceCount = 0;
static volatile LONG g_SoftGpuDpcTraceCount = 0;
static volatile LONG g_SoftGpuPointerTraceCount = 0;

FORCEINLINE ULONGLONG
SoftGpuTraceNow100ns(VOID)
{
    return KeQueryInterruptTime();
}

FORCEINLINE ULONGLONG
SoftGpuTraceElapsedUs(
    _In_ ULONGLONG Start100ns)
{
    ULONGLONG End100ns = KeQueryInterruptTime();

    if (End100ns <= Start100ns)
        return 0;

    return (End100ns - Start100ns) / 10ULL;
}

/* =========================================================================
 * SoftGpuDpcRoutine  — KDPC callback
 * =========================================================================
 */

/*
 * SoftGpuDpcRoutine
 *
 * Fires at DISPATCH_LEVEL after SubmitCommand queues the DPC.
 * DeferredContext is the PSOFTGPU_DEVICE pointer.
 *
 * Notifies dxgkrnl that the last submitted fence has been "completed" by
 * our simulated GPU.
 */
VOID
NTAPI
SoftGpuDpcRoutine(
    _In_     PKDPC   Dpc,
    _In_opt_ PVOID   DeferredContext,
    _In_opt_ PVOID   SystemArgument1,
    _In_opt_ PVOID   SystemArgument2)
{
    PSOFTGPU_DEVICE              Device;
    KIRQL                        OldIrql;
    ULONG                        CompletedFence;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData;
    ULONGLONG                    Start100ns;
    ULONGLONG                    ElapsedUs;
    LONG                         TraceSeq;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Device = (PSOFTGPU_DEVICE)DeferredContext;
    if (Device == NULL)
        return;

    ASSERT(Device->Magic == SOFTGPU_DEVICE_MAGIC);
    Start100ns = SoftGpuTraceNow100ns();

    /* Step 1-3: promote CurrentFence to CompletedFence under FenceLock. */
    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return;
    }
    Device->CompletedFence = Device->CurrentFence;
    CompletedFence         = Device->CompletedFence;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    /* Step 4: notify dxgkrnl of DMA completion (in "interrupt context"). */
    RtlZeroMemory(&NotifyData, sizeof(NotifyData));
    NotifyData.InterruptType                = DXGK_INTERRUPT_TYPE_DMA_COMPLETED;
    NotifyData.DmaCompleted.SubmissionFenceId = CompletedFence;
    NotifyData.DmaCompleted.NodeOrdinal     = 0;
    NotifyData.DmaCompleted.EngineOrdinal   = 0;

    if (Device->DxgkInterface.DxgkCbNotifyInterrupt != NULL)
    {
        Device->DxgkInterface.DxgkCbNotifyInterrupt(
            Device->DxgkInterface.DeviceHandle,
            &NotifyData);
    }

    /* Step 5: signal dxgkrnl to schedule the next packet. */
    if (Device->DxgkInterface.DxgkCbNotifyDpc != NULL)
    {
        Device->DxgkInterface.DxgkCbNotifyDpc(
            Device->DxgkInterface.DeviceHandle);
    }

    ElapsedUs = SoftGpuTraceElapsedUs(Start100ns);
    TraceSeq = InterlockedIncrement(&g_SoftGpuDpcTraceCount);
    if (TraceSeq <= SOFTGPU_TRACE_LOG_LIMIT ||
        ElapsedUs >= SOFTGPU_TRACE_SLOW_US)
    {
        DPRINT("SOFTGPU: DpcRoutine seq=%ld completed fence=%lu dur=%I64u us last=%lu\n",
               TraceSeq,
               CompletedFence,
               ElapsedUs,
               CompletedFence);
    }
}


/* =========================================================================
 * DxgkDdiDpcRoutine  — miniport DPC forwarding
 * =========================================================================
 */

/*
 * SoftGpuDdiDpcRoutine
 *
 * Called by dxgkrnl from its own DPC when the miniport's KDPC fires.
 * On softgpu we already complete all work in SoftGpuDpcRoutine (the raw
 * KDPC); this entry point is a no-op.
 */
VOID
APIENTRY
SoftGpuDdiDpcRoutine(
    _In_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}


/* =========================================================================
 * DxgkDdiInterruptRoutine
 * =========================================================================
 */

/*
 * SoftGpuDdiInterruptRoutine
 *
 * softgpu does not use a real interrupt line.  This routine will never be
 * invoked by the kernel; it is registered as a placeholder in case dxgkrnl
 * ever calls into it.
 *
 * Returns FALSE (interrupt not claimed) unconditionally.
 */
BOOLEAN
APIENTRY
SoftGpuDdiInterruptRoutine(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(MessageNumber);
    return FALSE;
}


/* =========================================================================
 * DxgkDdiSubmitCommand
 * =========================================================================
 */

/*
 * SoftGpuDdiSubmitCommand
 *
 * Records the submission fence ID and queues a DPC to "complete" it.
 * This simulates a GPU that executes all commands instantaneously.
 *
 * IRQL: DISPATCH_LEVEL (called from dxgkrnl scheduler)
 */
NTSTATUS
APIENTRY
SoftGpuDdiSubmitCommand(
    _In_ PVOID                        MiniportDeviceContext,
    _In_ CONST DXGKARG_SUBMITCOMMAND *SubmitCommand)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    KIRQL           OldIrql;
    ULONGLONG       Start100ns;
    ULONGLONG       ElapsedUs;
    LONG            TraceSeq;
    BOOLEAN         Queued;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        SubmitCommand == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (SubmitCommand->NodeOrdinal != 0 || SubmitCommand->EngineOrdinal != 0)
        return STATUS_INVALID_PARAMETER;

    Start100ns = SoftGpuTraceNow100ns();

    /* The same lock serializes StopDevice's gate with DPC insertion. */
    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_DELETE_PENDING;
    }
    Device->CurrentFence = SubmitCommand->SubmissionFenceId;
    Queued = KeInsertQueueDpc(&Device->DpcObject, NULL, NULL);
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    /*
     * Queue the DPC so completion is notified asynchronously.
     * KeInsertQueueDpc returns FALSE if the DPC is already queued; that is
     * acceptable since the DPC will fire and pick up CurrentFence.
     */
    ElapsedUs = SoftGpuTraceElapsedUs(Start100ns);
    TraceSeq = InterlockedIncrement(&g_SoftGpuSubmitTraceCount);

    if (TraceSeq <= SOFTGPU_TRACE_LOG_LIMIT ||
        ElapsedUs >= SOFTGPU_TRACE_SLOW_US)
    {
        DPRINT("SOFTGPU: SubmitCommand seq=%ld fence=%u node=%u queued=%u dur=%I64u us completed=%lu\n",
               TraceSeq,
               SubmitCommand->SubmissionFenceId,
               SubmitCommand->NodeOrdinal,
               Queued,
               ElapsedUs,
               Device->CompletedFence);
    }

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiPreemptCommand
 * =========================================================================
 */

/*
 * SoftGpuDdiPreemptCommand
 *
 * The software queue has no atomic cancellation primitive.  Refuse preemption
 * instead of reporting a fence that no engine actually preempted.
 *
 * IRQL: DISPATCH_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiPreemptCommand(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *PreemptCommand)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        PreemptCommand == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PreemptCommand->NodeOrdinal != 0 ||
        PreemptCommand->EngineOrdinal != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_NOT_SUPPORTED;
}


/* =========================================================================
 * DxgkDdiRender
 * =========================================================================
 */

/*
 * SoftGpuDdiRender
 *
 * Translates a user command buffer into a DMA buffer.  softgpu executes no
 * command stream, so translation is a bounded copy; patch locations pass
 * through unchanged.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiRender(
    _In_    PVOID           hContext,
    _Inout_ DXGKARG_RENDER *pRender)
{
    UINT CopyLength;
    UINT i;

    UNREFERENCED_PARAMETER(hContext);

    if (pRender == NULL || pRender->pCommand == NULL || pRender->pDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pRender->CommandLength > pRender->DmaSize)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    if (pRender->PatchLocationListInSize > pRender->PatchLocationListOutSize)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    CopyLength = pRender->CommandLength;
    RtlCopyMemory(pRender->pDmaBuffer, pRender->pCommand, CopyLength);
    pRender->pDmaBuffer = (PUCHAR)pRender->pDmaBuffer + CopyLength;

    for (i = 0; i < pRender->PatchLocationListInSize; i++)
    {
        pRender->pPatchLocationListOut[i] = pRender->pPatchLocationListIn[i];
        pRender->pPatchLocationListOut[i].PatchOffset = pRender->pPatchLocationListIn[i].PatchOffset;
    }
    pRender->pPatchLocationListOut += pRender->PatchLocationListInSize;

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiBuildPagingBuffer
 * =========================================================================
 */

/*
 * SoftGpuDdiBuildPagingBuffer
 *
 * softgpu has no real paging engine.  All allocations live in the aperture
 * segment backed by the contiguous framebuffer slab; dxgkrnl never actually
 * moves them.  We accept all operations as no-ops.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiBuildPagingBuffer(
    _In_    PVOID                      MiniportDeviceContext,
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (BuildPagingBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (BuildPagingBuffer->Operation)
    {
    case DXGK_OPERATION_TRANSFER:
    case DXGK_OPERATION_SPECIAL_LOCK_TRANSFER:
    case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
    case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
        /* softgpu allocations are already CPU-visible system memory. */
        return STATUS_SUCCESS;

    default:
        return STATUS_NOT_SUPPORTED;
    }
}


/* =========================================================================
 * DxgkDdiQueryCurrentFence
 * =========================================================================
 */

/*
 * SoftGpuDdiQueryCurrentFence
 *
 * Reports the most recently completed GPU fence to dxgkrnl.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiQueryCurrentFence(
    _In_    PVOID                       MiniportDeviceContext,
    _Inout_ PDXGKARG_QUERYCURRENTFENCE  pCurrentFence)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    KIRQL           OldIrql;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC ||
        pCurrentFence == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    pCurrentFence->CurrentFence = Device->CompletedFence;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiPatch
 * =========================================================================
 */

/*
 * SoftGpuDdiPatch
 *
 * softgpu never reads DMA buffer contents, so patching is a no-op.
 *
 * IRQL: PASSIVE_LEVEL or DISPATCH_LEVEL (called from dxgkrnl scheduler)
 */
NTSTATUS
APIENTRY
SoftGpuDdiPatch(
    _In_ PVOID                   MiniportDeviceContext,
    _In_ CONST DXGKARG_PATCH    *Patch)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (Patch == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}


/* =========================================================================
 * Cursor / palette stubs
 * =========================================================================
 */

NTSTATUS
APIENTRY
SoftGpuDdiSetPointerPosition(
    _In_ PVOID                            MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERPOSITION *SetPointerPosition)
{
    LONG TraceSeq = InterlockedIncrement(&g_SoftGpuPointerTraceCount);

    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (SetPointerPosition == NULL)
        return STATUS_INVALID_PARAMETER;

    if (TraceSeq <= SOFTGPU_TRACE_LOG_LIMIT)
    {
        DPRINT("SOFTGPU: SetPointerPosition seq=%ld src=%u visible=%u procedural=%u x=%d y=%d\n",
               TraceSeq,
               SetPointerPosition->VidPnSourceId,
               SetPointerPosition->Flags.Visible,
               SetPointerPosition->Flags.Procedural,
               SetPointerPosition->X,
               SetPointerPosition->Y);
    }

    /* No hardware cursor; dxgkrnl should use a software cursor path. */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
APIENTRY
SoftGpuDdiSetPointerShape(
    _In_ PVOID                         MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPOINTERSHAPE *SetPointerShape)
{
    LONG TraceSeq = InterlockedIncrement(&g_SoftGpuPointerTraceCount);

    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (SetPointerShape == NULL)
        return STATUS_INVALID_PARAMETER;

    if (TraceSeq <= SOFTGPU_TRACE_LOG_LIMIT)
    {
        DPRINT("SOFTGPU: SetPointerShape seq=%ld src=%u width=%u height=%u pitch=%u flags=0x%lx\n",
               TraceSeq,
               SetPointerShape->VidPnSourceId,
               SetPointerShape->Width,
               SetPointerShape->Height,
               SetPointerShape->Pitch,
               SetPointerShape->Flags.Value);
    }

    /* No hardware cursor; dxgkrnl should use a software cursor path. */
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
APIENTRY
SoftGpuDdiSetPalette(
    _In_ PVOID                   MiniportDeviceContext,
    _In_ CONST DXGKARG_SETPALETTE *SetPalette)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(SetPalette);

    return STATUS_NOT_SUPPORTED;
}


/* =========================================================================
 * DxgkDdiGetScanLine
 * =========================================================================
 */

/*
 * SoftGpuDdiGetScanLine
 *
 * Simulates a display that is always in vertical blank.  Returns ScanLine=0
 * and InVerticalBlank=TRUE so that any vblank wait completes immediately.
 */
NTSTATUS
APIENTRY
SoftGpuDdiGetScanLine(
    _In_    PVOID                  MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSCANLINE   GetScanLine)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (GetScanLine == NULL || GetScanLine->VidPnSourceId != 0)
        return STATUS_INVALID_PARAMETER;

    GetScanLine->ScanLine       = 0;
    GetScanLine->InVerticalBlank= TRUE;
    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiCreateContext / DxgkDdiDestroyContext
 * =========================================================================
 */

/*
 * SoftGpuDdiCreateContext
 *
 * Allocates a SOFTGPU_CONTEXT and fills in the DMA buffer geometry that
 * dxgkrnl propagates to the user-mode driver.
 *
 * DmaBufferSize: 64 KB — sufficient for a Vista-era render command batch.
 * AllocationListSize: 256 entries.
 * PatchLocationListSize: 256 entries.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiCreateContext(
    _In_    PVOID                   MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATECONTEXT  CreateContext)
{
    PSOFTGPU_CONTEXT Ctx;

    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (CreateContext == NULL)
        return STATUS_INVALID_PARAMETER;

    if (CreateContext->NodeOrdinal != 0)
        return STATUS_INVALID_PARAMETER;

    DPRINT("SOFTGPU: CreateContext hContext=%p NodeOrdinal=%u\n",
           CreateContext->hContext, CreateContext->NodeOrdinal);

    Ctx = (PSOFTGPU_CONTEXT)ExAllocatePoolWithTag(NonPagedPool,
                                                   sizeof(SOFTGPU_CONTEXT),
                                                   SOFTGPU_POOL_TAG);
    if (Ctx == NULL)
    {
        DPRINT1("SOFTGPU: CreateContext: pool alloc failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Ctx, sizeof(*Ctx));
    Ctx->Magic          = SOFTGPU_CONTEXT_MAGIC;
    Ctx->NodeOrdinal    = CreateContext->NodeOrdinal;
    Ctx->EngineAffinity = CreateContext->EngineAffinity;

    /*
     * Fill in DMA buffer geometry for the UMD via DXGK_CONTEXTINFO.
     * DmaBufferSize:            64 KB — sufficient for a Vista-era command batch.
     * AllocationListSize:       256 entries.
     * PatchLocationListSize:    256 entries.
     * DmaBufferSegmentSet:      0 requests physically contiguous system memory.
     * DmaBufferPrivateDataSize: 0 (softgpu has no per-DMA private state).
     */
    CreateContext->ContextInfo.DmaBufferSize            = 64 * 1024;
    CreateContext->ContextInfo.DmaBufferSegmentSet      = 0;
    CreateContext->ContextInfo.DmaBufferPrivateDataSize = 0;
    CreateContext->ContextInfo.AllocationListSize       = 256;
    CreateContext->ContextInfo.PatchLocationListSize    = 256;
    CreateContext->ContextInfo.Caps.Value               = 0;

    CreateContext->hContext = (HANDLE)Ctx;

    DPRINT("SOFTGPU: CreateContext: Ctx=%p\n", Ctx);
    return STATUS_SUCCESS;
}

/*
 * SoftGpuDdiDestroyContext
 *
 * Frees the SOFTGPU_CONTEXT.  The argument is the hContext written back
 * by CreateContext, not the MiniportDeviceContext.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiDestroyContext(
    _In_ PVOID hContext)
{
    PSOFTGPU_CONTEXT Ctx = (PSOFTGPU_CONTEXT)hContext;

    DPRINT("SOFTGPU: DestroyContext Ctx=%p\n", Ctx);

    if (Ctx != NULL)
    {
        if (Ctx->Magic != SOFTGPU_CONTEXT_MAGIC)
            return STATUS_INVALID_PARAMETER;

        Ctx->Magic = 0xDEADC047UL;
        ExFreePoolWithTag(Ctx, SOFTGPU_POOL_TAG);
    }

    return STATUS_SUCCESS;
}

/* EOF */
