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
static VOID
SoftGpuExecuteBlt(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_CMD *Cmd)
{
    LONG Width = Cmd->DstRect.right - Cmd->DstRect.left;
    LONG Height = Cmd->DstRect.bottom - Cmd->DstRect.top;
    ULONGLONG SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    ULONGLONG RowBytes;
    ULONGLONG SrcSpan;
    ULONGLONG DstSpan;
    PUCHAR SrcVa;
    PUCHAR DstVa;
    LONG Row;

    if (Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384)
        return;
    RowBytes = (ULONGLONG)Width * 4;
    if (Cmd->SrcPitch < RowBytes || Cmd->DstPitch < RowBytes ||
        Cmd->SrcPitch > 0x100000 || Cmd->DstPitch > 0x100000)
        return;
    SrcSpan = (ULONGLONG)Cmd->SrcPitch * (Height - 1) + RowBytes;
    DstSpan = (ULONGLONG)Cmd->DstPitch * (Height - 1) + RowBytes;
    if (Cmd->SrcAddress < SlabBase || Cmd->DstAddress < SlabBase ||
        Cmd->SrcAddress - SlabBase + SrcSpan > Device->FrameBufferSize ||
        Cmd->DstAddress - SlabBase + DstSpan > Device->FrameBufferSize)
        return;

    SrcVa = (PUCHAR)Device->FrameBuffer + (Cmd->SrcAddress - SlabBase);
    DstVa = (PUCHAR)Device->FrameBuffer + (Cmd->DstAddress - SlabBase);
    if (Cmd->SrcPitch == Cmd->DstPitch && Cmd->SrcPitch == RowBytes)
    {
        RtlMoveMemory(DstVa, SrcVa, (SIZE_T)RowBytes * Height);
        return;
    }
    for (Row = 0; Row < Height; Row++)
    {
        RtlMoveMemory(DstVa + (SIZE_T)Cmd->DstPitch * Row,
                      SrcVa + (SIZE_T)Cmd->SrcPitch * Row,
                      (SIZE_T)RowBytes);
    }
}

static VOID
SoftGpuExecuteFill(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_CMD *Cmd)
{
    LONG Width = Cmd->DstRect.right - Cmd->DstRect.left;
    LONG Height = Cmd->DstRect.bottom - Cmd->DstRect.top;
    ULONGLONG SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    ULONGLONG RowBytes;
    ULONGLONG DstSpan;
    PUCHAR DstVa;
    LONG Row;
    LONG Col;

    if (Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384)
        return;
    RowBytes = (ULONGLONG)Width * 4;
    if (Cmd->DstPitch < RowBytes || Cmd->DstPitch > 0x100000)
        return;
    DstSpan = (ULONGLONG)Cmd->DstPitch * (Height - 1) + RowBytes;
    if (Cmd->DstAddress < SlabBase ||
        Cmd->DstAddress - SlabBase + DstSpan > Device->FrameBufferSize)
        return;

    DstVa = (PUCHAR)Device->FrameBuffer + (Cmd->DstAddress - SlabBase);
    for (Row = 0; Row < Height; Row++)
    {
        PULONG RowPtr = (PULONG)(DstVa + (SIZE_T)Cmd->DstPitch * Row);

        for (Col = 0; Col < Width; Col++)
            RowPtr[Col] = Cmd->Color;
    }
}

static VOID
SoftGpuExecutePage(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_CMD *Cmd)
{
    ULONGLONG SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    PUCHAR SlabVa;

    if (Cmd->ByteCount == 0 || Cmd->ByteCount > Device->FrameBufferSize)
        return;
    if (Cmd->SlabAddress < SlabBase ||
        Cmd->SlabAddress - SlabBase + Cmd->ByteCount > Device->FrameBufferSize)
        return;
    if (Cmd->SystemAddress == 0)
        return;

    SlabVa = (PUCHAR)Device->FrameBuffer + (Cmd->SlabAddress - SlabBase);
    if ((Cmd->Flags & SOFTGPU_CMD_FLAG_TO_SLAB) != 0)
        RtlCopyMemory(SlabVa, (PVOID)(ULONG_PTR)Cmd->SystemAddress, (SIZE_T)Cmd->ByteCount);
    else
        RtlCopyMemory((PVOID)(ULONG_PTR)Cmd->SystemAddress, SlabVa, (SIZE_T)Cmd->ByteCount);
}

static VOID
SoftGpuExecuteFillLinear(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_CMD *Cmd)
{
    ULONGLONG SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;
    PULONG SlabVa;
    SIZE_T Count;
    SIZE_T Index;

    if (Cmd->ByteCount == 0 || Cmd->ByteCount > Device->FrameBufferSize)
        return;
    if (Cmd->SlabAddress < SlabBase ||
        Cmd->SlabAddress - SlabBase + Cmd->ByteCount > Device->FrameBufferSize)
        return;

    SlabVa = (PULONG)((PUCHAR)Device->FrameBuffer + (Cmd->SlabAddress - SlabBase));
    Count = (SIZE_T)(Cmd->ByteCount / sizeof(ULONG));
    for (Index = 0; Index < Count; Index++)
        SlabVa[Index] = Cmd->Color;
}

static VOID
SoftGpuExecuteDmaBuffer(
    _In_ PSOFTGPU_DEVICE Device,
    _In_ CONST SOFTGPU_SUBMIT *Submit)
{
    PUCHAR MapVa;
    ULONG Offset;

    if (Submit->EndOffset <= Submit->StartOffset ||
        Submit->EndOffset - Submit->StartOffset < sizeof(SOFTGPU_CMD) ||
        Submit->DmaPhys.QuadPart == 0)
        return;

    MapVa = MmMapIoSpace(Submit->DmaPhys, Submit->EndOffset, MmCached);
    if (MapVa == NULL)
        return;

    Offset = Submit->StartOffset;
    while (Offset + sizeof(SOFTGPU_CMD) <= Submit->EndOffset)
    {
        CONST SOFTGPU_CMD *Cmd = (CONST SOFTGPU_CMD *)(MapVa + Offset);

        if (Cmd->Magic != SOFTGPU_CMD_MAGIC ||
            Cmd->Size < sizeof(SOFTGPU_CMD) ||
            Offset + Cmd->Size > Submit->EndOffset)
            break;
        switch (Cmd->Op)
        {
            case SOFTGPU_CMD_OP_BLT:
                SoftGpuExecuteBlt(Device, Cmd);
                break;
            case SOFTGPU_CMD_OP_FILL:
                SoftGpuExecuteFill(Device, Cmd);
                break;
            case SOFTGPU_CMD_OP_PAGE:
                SoftGpuExecutePage(Device, Cmd);
                break;
            case SOFTGPU_CMD_OP_FILL_LINEAR:
                SoftGpuExecuteFillLinear(Device, Cmd);
                break;
            case SOFTGPU_CMD_OP_NOP:
                break;
            default:
                Offset = Submit->EndOffset;
                break;
        }
        if (Offset >= Submit->EndOffset)
            break;
        Offset += Cmd->Size;
    }

    MmUnmapIoSpace(MapVa, Submit->EndOffset);
}

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
    SOFTGPU_SUBMIT               Submit;
    BOOLEAN                      HaveSubmit;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Device = (PSOFTGPU_DEVICE)DeferredContext;
    if (Device == NULL)
        return;

    ASSERT(Device->Magic == SOFTGPU_DEVICE_MAGIC);
    Start100ns = SoftGpuTraceNow100ns();

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return;
    }
    HaveSubmit = Device->EngineActive == 0;
    if (HaveSubmit)
        Device->EngineActive = 1;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);

    if (HaveSubmit)
    {
        for (;;)
        {
            KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
            if (Device->Stopped || Device->SubmitRingHead == Device->SubmitRingTail)
            {
                Device->EngineActive = 0;
                KeReleaseSpinLock(&Device->FenceLock, OldIrql);
                break;
            }
            Submit = Device->SubmitRing[Device->SubmitRingHead % SOFTGPU_SUBMIT_RING_SIZE];
            Device->SubmitRingHead++;
            KeReleaseSpinLock(&Device->FenceLock, OldIrql);
            SoftGpuExecuteDmaBuffer(Device, &Submit);
            KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
            if (!Device->Stopped && (LONG)(Submit.Fence - Device->CompletedFence) > 0)
                Device->CompletedFence = Submit.Fence;
            KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        }
    }

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    if (Device->Stopped)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return;
    }
    CompletedFence = Device->CompletedFence;
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


VOID
NTAPI
SoftGpuVsyncDpcRoutine(
    _In_     PKDPC   Dpc,
    _In_opt_ PVOID   DeferredContext,
    _In_opt_ PVOID   SystemArgument1,
    _In_opt_ PVOID   SystemArgument2)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)DeferredContext;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData;
    KIRQL OldIrql;
    BOOLEAN Deliver;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC)
        return;

    KeAcquireSpinLock(&Device->FenceLock, &OldIrql);
    Deliver = !Device->Stopped &&
              InterlockedCompareExchange(&Device->VsyncEnabled, 0, 0) != 0 &&
              Device->DxgkInterface.DxgkCbNotifyInterrupt != NULL;
    KeReleaseSpinLock(&Device->FenceLock, OldIrql);
    if (!Deliver)
        return;

    RtlZeroMemory(&NotifyData, sizeof(NotifyData));
    NotifyData.InterruptType = DXGK_INTERRUPT_CRTC_VSYNC;
    NotifyData.CrtcVsync.VidPnTargetId = 0;
    NotifyData.CrtcVsync.PhysicalAddress = Device->FrameBufferPhys;
    NotifyData.CrtcVsync.PhysicalAdapterMask = 0;
    Device->DxgkInterface.DxgkCbNotifyInterrupt(
        Device->DxgkInterface.DeviceHandle,
        &NotifyData);
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
    if (Device->SubmitRingTail - Device->SubmitRingHead >= SOFTGPU_SUBMIT_RING_SIZE)
    {
        KeReleaseSpinLock(&Device->FenceLock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }
    Device->CurrentFence = SubmitCommand->SubmissionFenceId;
    {
        PSOFTGPU_SUBMIT Entry = &Device->SubmitRing[Device->SubmitRingTail % SOFTGPU_SUBMIT_RING_SIZE];

        Entry->DmaPhys = SubmitCommand->DmaBufferPhysicalAddress;
        Entry->StartOffset = SubmitCommand->DmaBufferSubmissionStartOffset;
        Entry->EndOffset = SubmitCommand->DmaBufferSubmissionEndOffset;
        Entry->Fence = SubmitCommand->SubmissionFenceId;
        Device->SubmitRingTail++;
    }
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
    CONST UCHAR *Command;
    UINT CopyLength;
    UINT Offset;
    UINT Records = 0;
    UINT i;

    UNREFERENCED_PARAMETER(hContext);

    if (pRender == NULL || pRender->pCommand == NULL || pRender->pDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pRender->CommandLength == 0 || pRender->CommandLength > pRender->DmaSize)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    if (pRender->PatchLocationListInSize > pRender->PatchLocationListOutSize)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    if (pRender->PatchLocationListInSize != 0 &&
        (pRender->pPatchLocationListIn == NULL || pRender->pPatchLocationListOut == NULL))
        return STATUS_INVALID_PARAMETER;

    /*
     * Validate the SOFTGPU_CMD stream before it becomes a DMA buffer: the
     * record chain must tile the command exactly, every opcode must be one
     * this engine implements, and every patch location must land inside a
     * record's address field.  A stream that fails here is rejected instead
     * of being handed to the engine to skip at execution time.
     */
    Command = (CONST UCHAR *)pRender->pCommand;
    for (Offset = 0; Offset < pRender->CommandLength; Records++)
    {
        CONST SOFTGPU_CMD *Cmd;

        if (pRender->CommandLength - Offset < sizeof(SOFTGPU_CMD))
            return STATUS_INVALID_PARAMETER;
        Cmd = (CONST SOFTGPU_CMD *)(Command + Offset);
        if (Cmd->Magic != SOFTGPU_CMD_MAGIC || Cmd->Size != sizeof(SOFTGPU_CMD))
            return STATUS_INVALID_PARAMETER;
        switch (Cmd->Op)
        {
            case SOFTGPU_CMD_OP_NOP:
            case SOFTGPU_CMD_OP_BLT:
            case SOFTGPU_CMD_OP_FILL:
                break;
            default:
                /* Paging opcodes are KMD-generated and are not accepted from
                 * a user-mode command stream. */
                return STATUS_INVALID_PARAMETER;
        }
        Offset += Cmd->Size;
    }
    if (Offset != pRender->CommandLength || Records == 0)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < pRender->PatchLocationListInSize; i++)
    {
        UINT PatchOffset = pRender->pPatchLocationListIn[i].PatchOffset;
        UINT RecordOffset = PatchOffset % sizeof(SOFTGPU_CMD);

        if (PatchOffset + sizeof(ULONGLONG) > pRender->CommandLength)
            return STATUS_INVALID_PARAMETER;
        if (RecordOffset != FIELD_OFFSET(SOFTGPU_CMD, SrcAddress) &&
            RecordOffset != FIELD_OFFSET(SOFTGPU_CMD, DstAddress))
            return STATUS_INVALID_PARAMETER;
    }

    CopyLength = pRender->CommandLength;
    RtlCopyMemory(pRender->pDmaBuffer, pRender->pCommand, CopyLength);
    pRender->pDmaBuffer = (PUCHAR)pRender->pDmaBuffer + CopyLength;

    for (i = 0; i < pRender->PatchLocationListInSize; i++)
        pRender->pPatchLocationListOut[i] = pRender->pPatchLocationListIn[i];
    pRender->pPatchLocationListOut += pRender->PatchLocationListInSize;

    return STATUS_SUCCESS;
}


/* =========================================================================
 * DxgkDdiBuildPagingBuffer
 * =========================================================================
 */

static PSOFTGPU_CMD
SoftGpuBeginPagingCommand(
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer)
{
    PSOFTGPU_CMD Cmd;

    if (BuildPagingBuffer->pDmaBuffer == NULL || BuildPagingBuffer->DmaSize < sizeof(SOFTGPU_CMD))
        return NULL;
    Cmd = (PSOFTGPU_CMD)BuildPagingBuffer->pDmaBuffer;
    RtlZeroMemory(Cmd, sizeof(*Cmd));
    Cmd->Magic = SOFTGPU_CMD_MAGIC;
    Cmd->Size = sizeof(*Cmd);
    Cmd->Op = SOFTGPU_CMD_OP_NOP;
    return Cmd;
}

static VOID
SoftGpuEndPagingCommand(
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer)
{
    BuildPagingBuffer->pDmaBuffer = (PUCHAR)BuildPagingBuffer->pDmaBuffer + sizeof(SOFTGPU_CMD);
    BuildPagingBuffer->MultipassOffset = 0;
}

/*
 * SoftGpuDdiBuildPagingBuffer
 *
 * Describes each supported paging operation as one SOFTGPU_CMD record the
 * engine executes at submission time.  The framebuffer slab is the memory
 * segment, so a transfer is a linear move between a slab physical address and
 * the kernel mapping of the backing MDL dxgkrnl supplied.  Operations this
 * software device genuinely has nothing to execute for (aperture mapping,
 * discard, residency notification) still emit an ordered no-op record so the
 * packet retires through the normal fence path rather than reporting false
 * completion without a packet.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiBuildPagingBuffer(
    _In_    PVOID                      MiniportDeviceContext,
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer)
{
    PSOFTGPU_DEVICE Device = (PSOFTGPU_DEVICE)MiniportDeviceContext;
    PSOFTGPU_CMD Cmd;
    ULONGLONG SlabBase;
    ULONGLONG SlabOffset;
    PVOID SystemVa;
    PMDL Mdl;

    if (Device == NULL || Device->Magic != SOFTGPU_DEVICE_MAGIC || BuildPagingBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    SlabBase = (ULONGLONG)Device->FrameBufferPhys.QuadPart;

    switch (BuildPagingBuffer->Operation)
    {
        case DXGK_OPERATION_TRANSFER:
        {
            CONST DXGK_BUILDPAGINGBUFFER_TRANSFER *Transfer = &BuildPagingBuffer->Transfer;
            BOOLEAN ToSlab;

            if (Transfer->TransferSize == 0)
                return STATUS_INVALID_PARAMETER;
            if (Transfer->Source.SegmentId == 0 && Transfer->Destination.SegmentId == SOFTGPU_SEGMENT_ID)
            {
                ToSlab = TRUE;
                Mdl = Transfer->Source.pMdl;
                SlabOffset = (ULONGLONG)Transfer->Destination.SegmentAddress.QuadPart;
            }
            else if (Transfer->Source.SegmentId == SOFTGPU_SEGMENT_ID && Transfer->Destination.SegmentId == 0)
            {
                ToSlab = FALSE;
                Mdl = Transfer->Destination.pMdl;
                SlabOffset = (ULONGLONG)Transfer->Source.SegmentAddress.QuadPart;
            }
            else
            {
                return STATUS_NOT_SUPPORTED;
            }
            if (Mdl == NULL)
                return STATUS_INVALID_PARAMETER;
            if (SlabOffset + Transfer->TransferSize > Device->FrameBufferSize)
                return STATUS_INVALID_PARAMETER;

            SystemVa = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
            if (SystemVa == NULL)
                return STATUS_INSUFFICIENT_RESOURCES;

            Cmd = SoftGpuBeginPagingCommand(BuildPagingBuffer);
            if (Cmd == NULL)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            Cmd->Op = SOFTGPU_CMD_OP_PAGE;
            Cmd->Flags = ToSlab ? SOFTGPU_CMD_FLAG_TO_SLAB : 0;
            Cmd->SlabAddress = SlabBase + SlabOffset;
            Cmd->SystemAddress = (ULONGLONG)(ULONG_PTR)SystemVa + Transfer->MdlOffset;
            Cmd->ByteCount = Transfer->TransferSize;
            SoftGpuEndPagingCommand(BuildPagingBuffer);
            return STATUS_SUCCESS;
        }

        case DXGK_OPERATION_FILL:
        {
            CONST DXGK_BUILDPAGINGBUFFER_FILL *Fill = &BuildPagingBuffer->Fill;

            if (Fill->Destination.SegmentId != SOFTGPU_SEGMENT_ID || Fill->FillSize == 0)
                return STATUS_NOT_SUPPORTED;
            SlabOffset = (ULONGLONG)Fill->Destination.SegmentAddress.QuadPart;
            if (SlabOffset + Fill->FillSize > Device->FrameBufferSize)
                return STATUS_INVALID_PARAMETER;

            Cmd = SoftGpuBeginPagingCommand(BuildPagingBuffer);
            if (Cmd == NULL)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            Cmd->Op = SOFTGPU_CMD_OP_FILL_LINEAR;
            Cmd->SlabAddress = SlabBase + SlabOffset;
            Cmd->ByteCount = Fill->FillSize;
            Cmd->Color = Fill->FillPattern;
            SoftGpuEndPagingCommand(BuildPagingBuffer);
            return STATUS_SUCCESS;
        }

        case DXGK_OPERATION_DISCARD_CONTENT:
#if (DXGKDDI_INTERFACE_VERSION >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
        case DXGK_OPERATION_NOTIFY_RESIDENCY:
#endif
            Cmd = SoftGpuBeginPagingCommand(BuildPagingBuffer);
            if (Cmd == NULL)
                return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
            SoftGpuEndPagingCommand(BuildPagingBuffer);
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
 * Writes the absolute physical placement of each referenced allocation into
 * the DMA buffer at the recorded patch offsets.
 *
 * IRQL: PASSIVE_LEVEL or DISPATCH_LEVEL (called from dxgkrnl scheduler)
 */
NTSTATUS
APIENTRY
SoftGpuDdiPatch(
    _In_ PVOID                   MiniportDeviceContext,
    _In_ CONST DXGKARG_PATCH    *Patch)
{
    UINT i;

    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (Patch == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Patch->PatchLocationListSubmissionLength == 0)
        return STATUS_SUCCESS;
    if (Patch->pPatchLocationList == NULL || Patch->pDmaBuffer == NULL ||
        Patch->pAllocationList == NULL)
        return STATUS_INVALID_PARAMETER;

    for (i = Patch->PatchLocationListSubmissionStart;
         i < Patch->PatchLocationListSubmissionStart + Patch->PatchLocationListSubmissionLength;
         i++)
    {
        CONST D3DDDI_PATCHLOCATIONLIST *Entry = &Patch->pPatchLocationList[i];
        CONST DXGK_ALLOCATIONLIST *Allocation;

        if (Entry->AllocationIndex >= Patch->AllocationListSize)
            return STATUS_INVALID_PARAMETER;
        if (Entry->PatchOffset + sizeof(ULONGLONG) > Patch->DmaBufferSubmissionEndOffset ||
            Entry->PatchOffset < Patch->DmaBufferSubmissionStartOffset)
            return STATUS_INVALID_PARAMETER;
        Allocation = &Patch->pAllocationList[Entry->AllocationIndex];
        *(ULONGLONG UNALIGNED *)((PUCHAR)Patch->pDmaBuffer + Entry->PatchOffset) =
            (ULONGLONG)Allocation->PhysicalAddress.QuadPart + Entry->AllocationOffset;
    }

    return STATUS_SUCCESS;
}

/*
 * SoftGpuDdiPresent
 *
 * Builds one SOFTGPU_CMD present record into the DMA buffer.  Blt becomes a
 * rect-packed copy between the patched source and destination placements,
 * ColorFill a rect fill; Flip and destination-less presents complete as
 * ordered no-ops (scan-out address updates travel the SetVidPnSourceAddress
 * path).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
APIENTRY
SoftGpuDdiPresent(
    _In_    PVOID            hContext,
    _Inout_ DXGKARG_PRESENT *pPresent)
{
    PSOFTGPU_CMD Cmd;
    LONG Width;
    LONG Height;
    UINT PatchesNeeded;

    UNREFERENCED_PARAMETER(hContext);

    if (pPresent == NULL || pPresent->pDmaBuffer == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pPresent->DmaSize < sizeof(SOFTGPU_CMD))
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    if (pPresent->Flags.Flip && pPresent->FlipInterval > D3DDDI_FLIPINTERVAL_FOUR)
        return STATUS_INVALID_PARAMETER;

    PatchesNeeded = 0;
    if (pPresent->Flags.Blt && pPresent->NumSrcAllocations != 0 && pPresent->NumDstAllocations != 0)
        PatchesNeeded = 2;
    else if (pPresent->Flags.ColorFill && pPresent->NumDstAllocations != 0)
        PatchesNeeded = 1;
    if (PatchesNeeded != 0 &&
        (pPresent->pPatchLocationListOut == NULL ||
         pPresent->PatchLocationListOutSize < PatchesNeeded))
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    Cmd = (PSOFTGPU_CMD)pPresent->pDmaBuffer;
    RtlZeroMemory(Cmd, sizeof(*Cmd));
    Cmd->Magic = SOFTGPU_CMD_MAGIC;
    Cmd->Size = sizeof(*Cmd);
    Cmd->Op = SOFTGPU_CMD_OP_NOP;

    if (PatchesNeeded != 0)
    {
        Width = pPresent->DstRect.right - pPresent->DstRect.left;
        Height = pPresent->DstRect.bottom - pPresent->DstRect.top;
        if (Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384)
            return STATUS_INVALID_PARAMETER;

        Cmd->SrcRect = pPresent->SrcRect;
        Cmd->DstRect = pPresent->DstRect;
        Cmd->Color = pPresent->Color;
        Cmd->SrcPitch = (ULONG)Width * 4;
        Cmd->DstPitch = (ULONG)Width * 4;

        if (pPresent->Flags.Blt && pPresent->NumSrcAllocations != 0)
        {
            Cmd->Op = SOFTGPU_CMD_OP_BLT;
            pPresent->pPatchLocationListOut[0].AllocationIndex = DXGK_PRESENT_SOURCE_INDEX;
            pPresent->pPatchLocationListOut[0].PatchOffset = FIELD_OFFSET(SOFTGPU_CMD, SrcAddress);
            pPresent->pPatchLocationListOut[1].AllocationIndex = DXGK_PRESENT_DESTINATION_INDEX;
            pPresent->pPatchLocationListOut[1].PatchOffset = FIELD_OFFSET(SOFTGPU_CMD, DstAddress);
            pPresent->pPatchLocationListOut += 2;
        }
        else
        {
            Cmd->Op = SOFTGPU_CMD_OP_FILL;
            pPresent->pPatchLocationListOut[0].AllocationIndex = DXGK_PRESENT_DESTINATION_INDEX;
            pPresent->pPatchLocationListOut[0].PatchOffset = FIELD_OFFSET(SOFTGPU_CMD, DstAddress);
            pPresent->pPatchLocationListOut += 1;
        }
    }

    pPresent->pDmaBuffer = (PUCHAR)pPresent->pDmaBuffer + sizeof(SOFTGPU_CMD);
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
