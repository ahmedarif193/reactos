/*
 * PROJECT:     ReactOS Raspberry Pi 5 WDDM miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Full-WDDM render/memory/scheduling DDIs: adapter caps and
 *              the VRAM segment, device/context/allocation objects, the
 *              Render/Present DMA packet stream, and the in-order fence
 *              pipeline that executes it (V3D control-list jobs on the
 *              real 3D engine, everything else completing immediately).
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "rpi5vc4.h"
#include "rpi5vc4_v3d.h"
#include "rpi5vc4_crtc.h"
#include "rpi5vc4_mbox.h"
#include <reactos/vc4cle.h>

#define NDEBUG
#include <reactos/debug.h>

/* ========================================================================
 * In-order submission pipeline
 *
 * Every SubmitCommand appends one entry.  Entries complete strictly in
 * submission order: CPU-executed packets complete as soon as they reach
 * the queue head; V3D jobs are programmed to the CLE when they reach the
 * head and complete when the core signals FLDONE/FRDONE (polled from a
 * 1 ms timer DPC — the root-enumerated devnode has no interrupt line).
 * Completion raises DXGK_INTERRUPT_TYPE_DMA_COMPLETED through
 * DxgkCbNotifyInterrupt followed by DxgkCbNotifyDpc, exactly like a real
 * ISR/DPC pair would.
 * ====================================================================== */

static VOID
Rpi5Vc4ArmV3dPollTimer(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    LARGE_INTEGER Due;

    if (DeviceExtension->StopAccepting || !DeviceExtension->DmaPipelineInitialized)
        return;

    Due.QuadPart = -10000; /* 1 ms */
    KeSetTimer(&DeviceExtension->V3dPollTimer, Due,
               &DeviceExtension->V3dPollDpc);
}

BOOLEAN
Rpi5V3dMmucFlushBounded(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

BOOLEAN
Rpi5V3dMmuProgramBounded(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension);

BOOLEAN
Rpi5V3dSmsPowerUpBounded(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PULONG TeeUs,
    _Out_ PULONG ReeUs);

static BOOLEAN
Rpi5Vc4GpuJobActiveLocked(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    ULONG Node;

    for (Node = 0; Node < RPI5VC4_GPU_NODE_COUNT; ++Node)
    {
        ULONG Offset;

        for (Offset = 0;
             Offset < DeviceExtension->NodeQueue[Node].Count;
             ++Offset)
        {
            ULONG Index =
                (DeviceExtension->NodeQueue[Node].Head + Offset) %
                RPI5VC4_MAX_PENDING;
            PRPI5VC4_PENDING_SUBMIT Entry =
                &DeviceExtension->NodeQueue[Node].Pending[Index];

            if (Entry->BinSubmitted || Entry->RenderSubmitted)
                return TRUE;
        }
    }
    return FALSE;
}

static BOOLEAN
Rpi5Vc4SelectAddressSpaceLocked(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_opt_ PRPI5VC4_PROCESS Process)
{
    PHYSICAL_ADDRESS PageTablePhysical;

    if (DeviceExtension->V3dActiveProcess == Process)
        return TRUE;
    if (Rpi5Vc4GpuJobActiveLocked(DeviceExtension))
        return FALSE;

    if (Process != NULL)
    {
        if (Process->Magic != RPI5VC4_PROCESS_MAGIC ||
            Process->Adapter != DeviceExtension ||
            Process->V3dPageTable == NULL)
        {
            return FALSE;
        }
        PageTablePhysical = Process->V3dPageTablePhys;
    }
    else
    {
        PageTablePhysical = DeviceExtension->V3dPageTablePhys;
    }

    if (!Rpi5V3dMmuProgramPageTableBounded(DeviceExtension,
                                            PageTablePhysical))
    {
        return FALSE;
    }
    DeviceExtension->V3dActiveProcess = Process;
    return TRUE;
}

/*
 * Advance the pipeline.  Called with DmaLock held at DISPATCH_LEVEL.
 * Returns TRUE when at least one fence completed (caller queues FenceDpc).
 *
 * V3D jobs execute in the Linux-equivalent phased pipeline: the binning
 * list runs on CLE thread 0; the render list is kicked on thread 1 only
 * once binning completed; and while the head job renders, the next V3D
 * job's binning may already run (bin(N+1) overlaps render(N)).
 */
static BOOLEAN
Rpi5Vc4ProcessPendingLocked(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _Out_ PBOOLEAN NeedPoll,
    _Out_ PBOOLEAN PipelineAborted)
{
    BOOLEAN Completed = FALSE;
    BOOLEAN FlDone = FALSE;   /* latch bookkeeping only (OUTOMEM service) */
    BOOLEAN FrDone = FALSE;
    ULONGLONG Now = KeQueryInterruptTime();
    ULONG Node;

#define Queue(n) (&DeviceExtension->NodeQueue[(n)])

    *NeedPoll = FALSE;
    *PipelineAborted = FALSE;

    if (DeviceExtension->StopAccepting)
        return FALSE;

    /* A synchronous exec-engine escape owns the CLE: park the pipeline
     * (jobs stay queued) until the gate is released. */
    if (DeviceExtension->V3dExecGateActive)
    {
        *NeedPoll = TRUE;
        return FALSE;
    }

    /* Consume the latched completion bits once per pass. */
    if (DeviceExtension->V3dReady && !DeviceExtension->StopAccepting)
    {
        ULONG i;

        Rpi5V3dPollDone(DeviceExtension, &FlDone, &FrDone);

    }

    for (Node = 0; Node < RPI5VC4_GPU_NODE_COUNT; Node++)
    {
    PRPI5VC4_PENDING_SUBMIT Head;

    while (Queue(Node)->Count != 0)
    {
        Head = &Queue(Node)->Pending[Queue(Node)->Head];

        if ((Head->IsTfuJob || Head->IsCsdJob || Head->IsV3dJob) && !DeviceExtension->V3dReady)
            goto AbortPipeline;

        if (Head->IsTfuJob || Head->IsCsdJob || Head->IsV3dJob)
        {
            if (DeviceExtension->V3dActiveProcess != Head->Process &&
                Rpi5Vc4GpuJobActiveLocked(DeviceExtension))
            {
                *NeedPoll = TRUE;
                goto NextNode;
            }
            if (!Rpi5Vc4SelectAddressSpaceLocked(DeviceExtension,
                                                  Head->Process))
            {
                goto AbortPipeline;
            }
        }

        if ((Head->IsTfuJob || Head->IsCsdJob) && DeviceExtension->V3dReady &&
            !DeviceExtension->StopAccepting)
        {
            /* Single-phase TFU conversion / CSD compute job. */
            /* Unpatched/non-slab addresses are invalid and never retire. */
            if (Head->IsTfuJob &&
                ((Head->TfuRegs[1] == 0 || Head->TfuRegs[6] == 0) ||
                 (Head->Process == NULL &&
                  (Head->TfuRegs[1] < RPI5VC4_V3D_SLAB_GPUVA ||
                   Head->TfuRegs[6] < RPI5VC4_V3D_SLAB_GPUVA))))
            {
                goto AbortPipeline;
            }

            if (!Head->RenderSubmitted)
            {
                BOOLEAN Kicked;

                if (Head->QueuedTime100ns == 0)
                    Head->QueuedTime100ns = Now;

                if (Head->IsTfuJob)
                    Kicked = Rpi5V3dSubmitTfu(DeviceExtension, Head->TfuRegs);
                else
                    Kicked = Rpi5V3dSubmitCsd(DeviceExtension, Head->CsdCfg);

                if (Kicked)
                {
                    Head->RenderSubmitted = TRUE;
                    Head->QueuedTime100ns = Now;

                    /* A 300x300 TFU conversion should retire in ~50us yet
                     * measures ~15ms.  Spin up to 2ms and log CS to split
                     * slow-execution from lost-done-latch. */
                    if (Head->IsTfuJob)
                    {
                        ULONG Spin;

                        for (Spin = 0; Spin < 400; Spin++)
                        {
                            if (Rpi5V3dTfuDone(DeviceExtension))
                                goto CompleteHead;
                            KeStallExecutionProcessor(5);
                        }
                    }
                }
                else
                {
                    if (Head->IsTfuJob && Now - Head->QueuedTime100ns < RPI5VC4_V3D_JOB_TIMEOUT_100NS)
                    {
                        *NeedPoll = TRUE;
                        goto NextNode;
                    }
                    DPRINT1("RPI5VC4: %s submit failed for fence=%lu — aborting\n", Head->IsTfuJob ? "TFU" : "CSD", Head->Fence);
                    if (Head->IsTfuJob)
                        Rpi5V3dResetCore(DeviceExtension);
                    goto AbortPipeline;
                }
            }

            if (Head->RenderSubmitted &&
                (Head->IsTfuJob ? Rpi5V3dTfuDone(DeviceExtension)
                                : Rpi5V3dCsdDone(DeviceExtension)))
                goto CompleteHead;

            if (Now - Head->QueuedTime100ns < RPI5VC4_V3D_JOB_TIMEOUT_100NS)
            {
                *NeedPoll = TRUE;
                goto NextNode;
            }

            DPRINT1("RPI5VC4: %s job fence=%lu timed out — aborting and resetting\n", Head->IsTfuJob ? "TFU" : "CSD", Head->Fence);
            Rpi5V3dResetCore(DeviceExtension);
            goto AbortPipeline;
        }

        if (Head->IsV3dJob && DeviceExtension->V3dReady &&
            !DeviceExtension->StopAccepting)
        {
            BOOLEAN HasBin = (Head->BclEnd != Head->BclStart);
            BOOLEAN BinPhaseOver;

            /* Phase 1: kick binning. */
            if (HasBin && !Head->BinSubmitted)
            {
                /* Completion baseline BEFORE the doorbell (same construction
                 * as RenderKickRfc): per-job ownership of the BFC edge. */
                Head->BinKickBfc = (UCHAR)
                    (READ_REGISTER_ULONG((PULONG)
                        ((PUCHAR)DeviceExtension->V3dCoreBase +
                         V3D_CLE_BFC)) & 0xff);
                if (Rpi5V3dSubmitBin(DeviceExtension,
                                     Head->BclStart, Head->BclEnd,
                                     Head->Qma, Head->Qms, Head->Qts))
                {
                    Head->BinSubmitted = TRUE;
                    Head->QueuedTime100ns = Now;
                }
                else
                {
                    DPRINT1("RPI5VC4: V3D bin submit failed for fence=%lu — aborting\n", Head->Fence);
                    goto AbortPipeline;
                }
            }

            /* Per-job bin completion is the BFC edge past this job's own
             * pre-kick baseline.  CT0 reaching EA is not completion: the
             * FLUSH packet still has to cap the tile lists and retire the
             * binner before CT1 may consume them. */
            if (HasBin && Head->BinSubmitted && !Head->BinDone)
            {
                PUCHAR CoreB = (PUCHAR)DeviceExtension->V3dCoreBase;
                ULONG Bfc = READ_REGISTER_ULONG((PULONG)
                                (CoreB + V3D_CLE_BFC)) & 0xff;

                if ((UCHAR)Bfc != Head->BinKickBfc)
                    Head->BinDone = TRUE;

            }

            BinPhaseOver = !HasBin || Head->BinDone;

            /* Phase 2: kick rendering once binning finished. */
            if (BinPhaseOver && !Head->RenderSubmitted)
            {
                /* Completion baseline BEFORE the doorbell (simulator reads
                 * last_rfc before CT1QBA): a fast render can retire before
                 * a post-kick read, and a baseline taken after swallows the
                 * completion — the fence never retires and TDRs (the
                 * "render-park"). Renders are serialized, so RFC moving
                 * past this snapshot uniquely means THIS render finished. */
                Head->RenderKickRfc = (UCHAR)
                    (READ_REGISTER_ULONG((PULONG)
                        ((PUCHAR)DeviceExtension->V3dCoreBase +
                         V3D_CLE_RFC)) & 0xff);
                if (Rpi5V3dSubmitRender(DeviceExtension,
                                        Head->RclStart, Head->RclEnd,
                                        !HasBin))
                {
                    Head->RenderSubmitted = TRUE;
                    Head->QueuedTime100ns = Now;
                }
                else
                {
                    DPRINT1("RPI5VC4: V3D render submit failed for fence=%lu — aborting\n", Head->Fence);
                    goto AbortPipeline;
                }
            }

            /* Phase 3: retire on render completion — RFC advancing past the
             * per-job kick snapshot.  Rendering is sequential (overlap off,
             * only Head is render-kicked), so RFC past the snapshot uniquely
             * means THIS render finished.  Replaces the global FrDone test,
             * which double-counts vs the FRDONE latch and mis-retires the
             * wrong Head — the root of the spurious TDR + reset cascade. */
            if (Head->RenderSubmitted &&
                (UCHAR)(READ_REGISTER_ULONG((PULONG)
                    ((PUCHAR)DeviceExtension->V3dCoreBase + V3D_CLE_RFC)) & 0xff)
                    != Head->RenderKickRfc)
                goto CompleteHead;

            /* Still in flight: enforce the per-phase timeout. */
            if (Now - Head->QueuedTime100ns < RPI5VC4_V3D_JOB_TIMEOUT_100NS)
            {
                *NeedPoll = TRUE;
                goto NextNode;
            }

            {
                PVOID Core = DeviceExtension->V3dCoreBase;

                /* Last-chance completion recheck before the destructive reset:
                 * if RFC advanced past the kick snapshot the render actually
                 * finished (lost/late completion) — retire it and SKIP the core
                 * reset, which would otherwise wedge the next in-flight job and
                 * cascade every subsequent job into a TDR. */
                if (Head->RenderSubmitted &&
                    (UCHAR)(READ_REGISTER_ULONG((PULONG)
                        ((PUCHAR)Core + V3D_CLE_RFC)) & 0xff)
                        != Head->RenderKickRfc)
                {
                    goto CompleteHead;
                }

                DPRINT1("RPI5VC4: V3D job fence=%lu timed out — aborting (TDR)\n", Head->Fence);
                DPRINT1("RPI5VC4: TDR resetting V3D core: %s\n", Rpi5V3dResetCore(DeviceExtension) ? "ok" : "FAILED");
                /* The reset invalidates every engine queue. Drop all pending
                 * entries without completing their fences and leave admission
                 * closed until dxgkrnl runs Reset/RestartFromTimeout. */
            }
            goto AbortPipeline;
        }

CompleteHead:
        if (Head->Fence != 0)
        {
            /* Report on the node dxgkrnl SUBMITTED on (vidsch retires
             * per engine); internal QueueIndex routing is private.
             * Monotonic max: concurrent hardware queues may finish out
             * of global fence order. */
            ULONG Lane = Head->ReportNode;

            if (Lane >= RPI5VC4_GPU_NODE_COUNT)
                goto AbortPipeline;

            if ((LONG)(Head->Fence -
                       DeviceExtension->LastCompletedFence) > 0)
                DeviceExtension->LastCompletedFence = Head->Fence;
            if ((LONG)(Head->Fence -
                       DeviceExtension->LastCompletedFencePerNode[Lane]) > 0)
                DeviceExtension->LastCompletedFencePerNode[Lane] = Head->Fence;
            Completed = TRUE;
        }
        Queue(Node)->Head = (Queue(Node)->Head + 1) % RPI5VC4_MAX_PENDING;
        Queue(Node)->Count--;
    }

        goto NextNode;

AbortPipeline:
        *PipelineAborted = TRUE;
        *NeedPoll = FALSE;
        DeviceExtension->StopAccepting = TRUE;
        RtlZeroMemory(DeviceExtension->NodeQueue, sizeof(DeviceExtension->NodeQueue));
        goto Finished;

NextNode:;
    }

Finished:

#undef Queue

    return Completed;
}

/* DPC: report the last completed fence to dxgkrnl (ISR+DPC contract). */
static VOID
NTAPI
Rpi5Vc4FenceDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = DeferredContext;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData;
    KIRQL OldIrql;
    ULONG CompletedFence;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (DeviceExtension == NULL || DeviceExtension->StopAccepting)
        return;

    {
        ULONG NodeFence[RPI5VC4_GPU_NODE_COUNT];
        ULONG Node, Pass;

        KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
        CompletedFence = DeviceExtension->LastCompletedFence;
        for (Node = 0; Node < RPI5VC4_GPU_NODE_COUNT; Node++)
            NodeFence[Node] = DeviceExtension->LastCompletedFencePerNode[Node];
        KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);

        /*
         * One DMA_COMPLETED per node whose fence advanced.  The shared
         * ring completes in global fence order, so reporting nodes in
         * ascending fence order keeps the adapter-wide completed fence
         * monotonic for dxgkrnl's DMA-buffer tracker.
         */
        for (Pass = 0; Pass < RPI5VC4_GPU_NODE_COUNT; Pass++)
        {
            ULONG Best = RPI5VC4_GPU_NODE_COUNT;

            for (Node = 0; Node < RPI5VC4_GPU_NODE_COUNT; Node++)
            {
                if (NodeFence[Node] ==
                    DeviceExtension->LastReportedFencePerNode[Node])
                {
                    continue;
                }
                if (Best == RPI5VC4_GPU_NODE_COUNT ||
                    (LONG)(NodeFence[Node] - NodeFence[Best]) < 0)
                {
                    Best = Node;
                }
            }

            if (Best == RPI5VC4_GPU_NODE_COUNT)
                break;

            RtlZeroMemory(&NotifyData, sizeof(NotifyData));
            NotifyData.InterruptType = DXGK_INTERRUPT_TYPE_DMA_COMPLETED;
            NotifyData.DmaCompleted.SubmissionFenceId = NodeFence[Best];
            NotifyData.DmaCompleted.NodeOrdinal = Best;
            NotifyData.DmaCompleted.EngineOrdinal = 0;

            if (DeviceExtension->DxgkInterface.DxgkCbNotifyInterrupt != NULL)
            {
                DeviceExtension->DxgkInterface.DxgkCbNotifyInterrupt(
                    DeviceExtension->DxgkInterface.DeviceHandle,
                    &NotifyData);
            }

            DeviceExtension->LastReportedFencePerNode[Best] = NodeFence[Best];
        }
    }

    if (DeviceExtension->DxgkInterface.DxgkCbNotifyDpc != NULL)
    {
        DeviceExtension->DxgkInterface.DxgkCbNotifyDpc(
            DeviceExtension->DxgkInterface.DeviceHandle);
    }
}

/* Timer DPC: poll V3D completion and advance the pipeline. */
static VOID
NTAPI
Rpi5Vc4V3dPollDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = DeferredContext;
    KIRQL OldIrql;
    BOOLEAN Completed;
    BOOLEAN NeedPoll;
    BOOLEAN PipelineAborted;
    BOOLEAN InterruptWasMasked;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (DeviceExtension == NULL)
        return;

    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    Completed = Rpi5Vc4ProcessPendingLocked(DeviceExtension, &NeedPoll, &PipelineAborted);
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);

    InterruptWasMasked =
        InterlockedExchange(&DeviceExtension->V3dIsrMasked, 0) != 0;
    if (InterruptWasMasked &&
        DeviceExtension->V3dCoreBase != NULL &&
        !DeviceExtension->StopAccepting)
    {
        InterlockedIncrement(&DeviceExtension->V3dDpcFromIsr);
        if (DeviceExtension->V3dCoreIrqConnected)
        {
            WRITE_REGISTER_ULONG(
                (PULONG)((PUCHAR)DeviceExtension->V3dCoreBase + V3D_CTL_INT_MSK_CLR),
                V3D_INT_FLDONE | V3D_INT_FRDONE | V3D_INT_OUTOMEM |
                V3D_V7_INT_CSDDONE);
        }
        if (DeviceExtension->V3dHubIrqConnected &&
            DeviceExtension->V3dHubBase != NULL)
        {
            WRITE_REGISTER_ULONG(
                (PULONG)((PUCHAR)DeviceExtension->V3dHubBase + V3D_HUB_INT_MSK_CLR),
                V3D_HUB_INT_TFUC);
        }
    }

    if (Completed)
        KeInsertQueueDpc(&DeviceExtension->FenceDpc, NULL, NULL);
    if (NeedPoll && !DeviceExtension->StopAccepting)
        Rpi5Vc4ArmV3dPollTimer(DeviceExtension);
}

/* ========================================================================
 * Vsync source
 *
 * The devnode is root-enumerated (no interrupt resource), so the vblank
 * "interrupt" is a refresh-rate timer that samples and clears the
 * PixelValve VFP-start latch, raising DXGK_INTERRUPT_TYPE_CRTC_VSYNC to
 * dxgkrnl whenever the raster really entered vertical blanking.
 * ====================================================================== */

/* PASSIVE hotplug probe for headless boots: firmware DDC doubles as
 * HPD; on connect the firmware modeset lights the display and the
 * driver adopts its framebuffer (same shape as the POST handover). */
static VOID
Rpi5Vc4HpdWorker(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = Context;
    ULONGLONG FbPhys;
    ULONG FbSize, Pitch;
    ULONG Width, Height;
    PHYSICAL_ADDRESS Phys;
    PVOID Va;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (DeviceExtension == NULL)
        return;

    /* Device stopping: don't touch the mailbox/extension the drain is freeing. */
    if (DeviceExtension->StopAccepting)
    {
        InterlockedExchange(&DeviceExtension->HpdCheckQueued, 0);
        return;
    }

    if (DeviceExtension->Headless &&
        Rpi5MboxGetEdidBlock(DeviceExtension, 0, DeviceExtension->Edid))
    {
        /* Preferred timing from EDID detailed descriptor 1 (bytes 54..71):
         * a fixed 1920x1080 shows no signal on panels that don't support it. */
        Width  = ((ULONG)(DeviceExtension->Edid[58] & 0xF0) << 4) |
                 DeviceExtension->Edid[56];
        Height = ((ULONG)(DeviceExtension->Edid[61] & 0xF0) << 4) |
                 DeviceExtension->Edid[59];
        if (Width < 640 || Width > 7680 || Height < 480 || Height > 4320)
        {
            Width = 1920;
            Height = 1080;
        }

        DeviceExtension->EdidValid = TRUE;
        DPRINT1("RPI5VC4: HPD — display detected on headless adapter "
                "(EDID mfr %02x%02x), firmware modeset %ux%u\n",
                DeviceExtension->Edid[8], DeviceExtension->Edid[9], Width, Height);

        if (Rpi5MboxFbSetMode(DeviceExtension, Width, Height,
                              &FbPhys, &FbSize, &Pitch))
        {
            Phys.QuadPart = (LONGLONG)FbPhys;
            Va = MmMapIoSpace(Phys, FbSize, MmWriteCombined);
            if (Va != NULL)
            {
                /* Release the headless RAM scanout the firmware FB replaces
                 * (freed with its own allocation size, still in FrameBufferSize)
                 * before overwriting the geometry. */
                if (DeviceExtension->HeadlessFbVa != NULL)
                {
                    MmFreeContiguousMemorySpecifyCache(
                        DeviceExtension->HeadlessFbVa,
                        DeviceExtension->FrameBufferSize,
                        MmWriteCombined);
                    DeviceExtension->HeadlessFbVa = NULL;
                }

                DeviceExtension->FrameBufferPhysical = Phys;
                DeviceExtension->FrameBufferVa = Va;
                DeviceExtension->FrameBufferSize = FbSize;
                DeviceExtension->ScreenWidth = Width;
                DeviceExtension->ScreenHeight = Height;
                DeviceExtension->BytesPerScanLine = Pitch;
                DeviceExtension->Headless = FALSE;
                DPRINT1("RPI5VC4: cold-start complete — scanout %ux%u "
                        "pitch %lu at %I64x\n", Width, Height, Pitch, FbPhys);

                if (DeviceExtension->DxgkInterface.DxgkCbIndicateChildStatus != NULL)
                {
                    DXGK_CHILD_STATUS Status;

                    RtlZeroMemory(&Status, sizeof(Status));
                    Status.Type = StatusConnection;
                    Status.ChildUid = 1;
                    Status.HotPlug.Connected = TRUE;
                    DeviceExtension->DxgkInterface.DxgkCbIndicateChildStatus(
                        DeviceExtension->DxgkInterface.DeviceHandle, &Status);
                }
            }
            else
            {
                DPRINT1("RPI5VC4: cold-start fb map failed\n");
            }
        }
        else
        {
            DPRINT1("RPI5VC4: firmware modeset failed (no display lit)\n");
        }
    }

    InterlockedExchange(&DeviceExtension->HpdCheckQueued, 0);
}

/* Headless hotplug poll timer. dxgkrnl never enables the display vsync
 * interrupt while headless (no active target), so the EDID probe cannot ride
 * the vsync DPC — it runs on its own ~1 Hz timer, armed by StartDevice once
 * started+headless and self-stopping once a display is adopted (Headless
 * clears) or the device is stopping (StopAccepting). */
VOID
Rpi5Vc4ArmHpdTimer(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    LARGE_INTEGER Due;

    Due.QuadPart = -10000000; /* 1 s */
    KeSetTimer(&DeviceExtension->HpdTimer, Due, &DeviceExtension->HpdDpc);
}

static VOID
NTAPI
Rpi5Vc4HpdDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    /* Stop polling once a display is adopted, the device is stopping, or gone. */
    if (DeviceExtension == NULL || DeviceExtension->StopAccepting ||
        !DeviceExtension->Headless || DeviceExtension->HpdWorkItem == NULL)
        return;

    if (InterlockedCompareExchange(&DeviceExtension->HpdCheckQueued, 1, 0) == 0)
    {
        IoQueueWorkItem(DeviceExtension->HpdWorkItem, Rpi5Vc4HpdWorker,
                        DelayedWorkQueue, DeviceExtension);
    }

    Rpi5Vc4ArmHpdTimer(DeviceExtension);
}

static VOID
NTAPI
Rpi5Vc4VsyncDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = DeferredContext;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData;
    BOOLEAN VblankSeen = TRUE;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (DeviceExtension == NULL || DeviceExtension->StopAccepting || !DeviceExtension->VsyncEnabled)
        return;

    /*
     * Sample the real raster position when the PixelValve is mapped; a
     * clear latch means no vblank started since the last tick, so stay
     * silent rather than reporting a fake vsync.
     */
    if (DeviceExtension->PixelValveBase != NULL)
    {
        PUCHAR Base = DeviceExtension->PixelValveBase;
        ULONG IntStat = READ_REGISTER_ULONG((PULONG)(Base + RPI5_PV_INTSTAT));

        if (IntStat & RPI5_PV_INT_VFP_START)
        {
            WRITE_REGISTER_ULONG((PULONG)(Base + RPI5_PV_INTSTAT),
                                 RPI5_PV_INT_VFP_START);
        }
        else
        {
            VblankSeen = FALSE;
        }
    }

    if (VblankSeen)
    {
        RtlZeroMemory(&NotifyData, sizeof(NotifyData));
        NotifyData.InterruptType = DXGK_INTERRUPT_TYPE_CRTC_VSYNC;
        NotifyData.CrtcVsync.VidPnTargetId = 0;
        NotifyData.CrtcVsync.PhysicalAddress = DeviceExtension->FrameBufferPhysical;
        NotifyData.CrtcVsync.PhysicalAdapterMask = 1;
        NotifyData.Flags.ValidPhysicalAdapterMask = 1;

        if (DeviceExtension->DxgkInterface.DxgkCbNotifyInterrupt != NULL)
        {
            DeviceExtension->DxgkInterface.DxgkCbNotifyInterrupt(
                DeviceExtension->DxgkInterface.DeviceHandle,
                &NotifyData);
        }

        if (DeviceExtension->DxgkInterface.DxgkCbNotifyDpc != NULL)
        {
            DeviceExtension->DxgkInterface.DxgkCbNotifyDpc(
                DeviceExtension->DxgkInterface.DeviceHandle);
        }
    }
}

static VOID
Rpi5Vc4VsyncControl(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN Enable)
{
    if (Enable && !DeviceExtension->VsyncEnabled)
    {
        LARGE_INTEGER Due;

        DeviceExtension->VsyncEnabled = TRUE;
        Due.QuadPart = -166667; /* ~16.7 ms, 60 Hz */
        KeSetTimerEx(&DeviceExtension->VsyncTimer, Due, 16,
                     &DeviceExtension->VsyncDpc);
    }
    else if (!Enable && DeviceExtension->VsyncEnabled)
    {
        DeviceExtension->VsyncEnabled = FALSE;
        KeCancelTimer(&DeviceExtension->VsyncTimer);
    }
}

VOID
Rpi5Vc4DmaPipelineInit(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    KeInitializeSpinLock(&DeviceExtension->DmaLock);
    DeviceExtension->V3dKickPrints = 0;
    KeInitializeDpc(&DeviceExtension->FenceDpc,
                    Rpi5Vc4FenceDpcRoutine, DeviceExtension);
    KeInitializeDpc(&DeviceExtension->V3dPollDpc,
                    Rpi5Vc4V3dPollDpcRoutine, DeviceExtension);
    KeInitializeTimer(&DeviceExtension->V3dPollTimer);
    KeInitializeDpc(&DeviceExtension->VsyncDpc,
                    Rpi5Vc4VsyncDpcRoutine, DeviceExtension);
    KeInitializeTimer(&DeviceExtension->VsyncTimer);
    DeviceExtension->VsyncEnabled = FALSE;
    KeInitializeDpc(&DeviceExtension->HpdDpc,
                    Rpi5Vc4HpdDpcRoutine, DeviceExtension);
    KeInitializeTimer(&DeviceExtension->HpdTimer);
    /* The Hpd timer is armed by StartDevice after Started is set, so a failed
     * StartDevice (which skips DmaPipelineDrain) never leaves it running. */
    RtlZeroMemory(DeviceExtension->NodeQueue, sizeof(DeviceExtension->NodeQueue));
    DeviceExtension->LastCompletedFence = 0;
    RtlZeroMemory((PVOID)DeviceExtension->LastCompletedFencePerNode,
                  sizeof(DeviceExtension->LastCompletedFencePerNode));
    RtlZeroMemory((PVOID)DeviceExtension->LastReportedFencePerNode,
                  sizeof(DeviceExtension->LastReportedFencePerNode));
    RtlZeroMemory(DeviceExtension->DmaMappings, sizeof(DeviceExtension->DmaMappings));
    DeviceExtension->DmaMappingNext = 0;
    DeviceExtension->DmaPipelineInitialized = TRUE;
    DeviceExtension->StopAccepting = FALSE;
}

VOID
Rpi5Vc4DmaPipelineDrain(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;

    if (!DeviceExtension->DmaPipelineInitialized)
        return;

    DeviceExtension->StopAccepting = TRUE;
    Rpi5Vc4VsyncControl(DeviceExtension, FALSE);
    /*
     * HPD teardown: StopAccepting (set above) stops Rpi5Vc4HpdDpcRoutine from
     * queuing new work or re-arming its timer. Cancel + flush twice so a DPC
     * that had already passed the guard and re-armed the timer before the flag
     * was visible is caught on the second pass; then wait for an already-queued
     * Rpi5Vc4HpdWorker to finish before the caller frees the mailbox/extension.
     */
    KeCancelTimer(&DeviceExtension->HpdTimer);
    KeCancelTimer(&DeviceExtension->V3dPollTimer);
    KeRemoveQueueDpc(&DeviceExtension->V3dPollDpc);
    KeRemoveQueueDpc(&DeviceExtension->VsyncDpc);
    KeRemoveQueueDpc(&DeviceExtension->HpdDpc);
    KeRemoveQueueDpc(&DeviceExtension->FenceDpc);
    KeFlushQueuedDpcs();
    KeCancelTimer(&DeviceExtension->HpdTimer);
    KeRemoveQueueDpc(&DeviceExtension->HpdDpc);
    KeFlushQueuedDpcs();
    while (InterlockedCompareExchange(&DeviceExtension->HpdCheckQueued, 0, 0) != 0)
    {
        LARGE_INTEGER HpdWait;

        HpdWait.QuadPart = -100000; /* 10 ms */
        KeDelayExecutionThread(KernelMode, FALSE, &HpdWait);
    }

    /* Stop/remove aborts queued work; only observed hardware completion may
     * advance a fence or emit DMA_COMPLETED. */
    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    RtlZeroMemory(DeviceExtension->NodeQueue, sizeof(DeviceExtension->NodeQueue));
    RtlZeroMemory(DeviceExtension->DmaMappings, sizeof(DeviceExtension->DmaMappings));
    DeviceExtension->DmaMappingNext = 0;
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
    DeviceExtension->DmaPipelineInitialized = FALSE;
}

/* ========================================================================
 * DMA packet stream helpers
 * ====================================================================== */

/*
 * Walk a packet stream.  Returns FALSE on malformed input or more than one
 * hardware job/PRESENT marker.  When Job is non-NULL, the single hardware
 * job packet is copied out.  PRESENT is descriptive CPU-completed work and
 * is reported separately.
 */
static BOOLEAN
Rpi5Vc4ParseDmaStream(
    _In_reads_bytes_(Length) const VOID *Stream,
    _In_ SIZE_T Length,
    _Out_opt_ PRPI5VC4_DMA_PACKET Job,
    _Out_opt_ PBOOLEAN HasJob,
    _Out_opt_ PBOOLEAN HasPresent)
{
    const UCHAR *Cursor = Stream;
    SIZE_T Remaining = Length;
    BOOLEAN FoundJob = FALSE;
    BOOLEAN FoundPresent = FALSE;

    if (HasJob != NULL)
        *HasJob = FALSE;
    if (HasPresent != NULL)
        *HasPresent = FALSE;

    while (Remaining >= sizeof(RPI5VC4_DMA_PACKET))
    {
        const RPI5VC4_DMA_PACKET *Packet = (const RPI5VC4_DMA_PACKET *)Cursor;

        if (Packet->Magic != RPI5VC4_DMA_PACKET_MAGIC ||
            Packet->Length < sizeof(RPI5VC4_DMA_PACKET) ||
            Packet->Length > Remaining)
        {
            return FALSE;
        }

        if (Packet->Op == RPI5VC4_DMA_OP_V3D_JOB ||
            Packet->Op == RPI5VC4_DMA_OP_TFU_JOB ||
            Packet->Op == RPI5VC4_DMA_OP_CSD_JOB)
        {
            if (FoundJob)
                return FALSE;
            FoundJob = TRUE;
            if (Job != NULL)
                *Job = *Packet;
            if (HasJob != NULL)
                *HasJob = TRUE;
        }
        else if (Packet->Op == RPI5VC4_DMA_OP_PRESENT)
        {
            if (FoundPresent)
                return FALSE;
            FoundPresent = TRUE;
            if (HasPresent != NULL)
                *HasPresent = TRUE;
        }
        else if (Packet->Op != RPI5VC4_DMA_OP_NOP)
        {
            return FALSE;
        }

        Cursor += Packet->Length;
        Remaining -= Packet->Length;
    }

    return Remaining == 0;
}

/* ========================================================================
 * Adapter capabilities and the VRAM segment
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiQueryAdapterInfo(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_QUERYADAPTERINFO *QueryAdapterInfo)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    if (DeviceExtension == NULL || QueryAdapterInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (QueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_UMDRIVERPRIVATE:
            if (QueryAdapterInfo->pOutputData == NULL)
                return STATUS_INVALID_PARAMETER;
            RtlZeroMemory(QueryAdapterInfo->pOutputData,
                          QueryAdapterInfo->OutputDataSize);
            return STATUS_SUCCESS;

        case DXGKQAITYPE_GPUMMUCAPS:
        {
            DXGK_GPUMMUCAPS *Caps = QueryAdapterInfo->pOutputData;

            if (Caps == NULL)
                return STATUS_INVALID_PARAMETER;
            if (QueryAdapterInfo->OutputDataSize < sizeof(*Caps))
                return STATUS_BUFFER_TOO_SMALL;

            RtlZeroMemory(Caps, sizeof(*Caps));
            Caps->ReadOnlyMemorySupported = 1;
            Caps->ExplicitPageTableInvalidation = 1;
            Caps->PageTableUpdateRequireAddressSpaceIdle = 1;
            Caps->PageTableUpdateMode = DXGK_PAGETABLEUPDATE_CPU_VIRTUAL;
            Caps->VirtualAddressBitCount = RPI5VC4_GPUVA_BITS;
            Caps->PageTableLevelCount = RPI5VC4_GPUVA_LEVELS;
            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_PAGETABLELEVELDESC:
        {
            const DXGK_QUERYPAGETABLELEVELDESCIN *Input =
                QueryAdapterInfo->pInputData;
            DXGK_PAGE_TABLE_LEVEL_DESC *Desc =
                QueryAdapterInfo->pOutputData;

            if (Input == NULL ||
                QueryAdapterInfo->InputDataSize < sizeof(*Input) ||
                Desc == NULL)
            {
                return STATUS_INVALID_PARAMETER;
            }
            if (QueryAdapterInfo->OutputDataSize < sizeof(*Desc))
                return STATUS_BUFFER_TOO_SMALL;
            if (Input->PhysicalAdapterIndex != 0 ||
                Input->LevelIndex >= RPI5VC4_GPUVA_LEVELS)
            {
                return STATUS_INVALID_PARAMETER;
            }

            RtlZeroMemory(Desc, sizeof(*Desc));
            Desc->PageTableIndexBitCount = RPI5VC4_GPUVA_INDEX_BITS;
            Desc->PageTableSegmentId = 0;
            Desc->PagingProcessPageTableSegmentId = 0;
            Desc->PageTableSizeInBytes =
                (1u << RPI5VC4_GPUVA_INDEX_BITS) * sizeof(DXGK_PTE);
            Desc->PageTableAlignmentInBytes = PAGE_SIZE;
            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_DRIVERCAPS:
        {
            PDXGK_DRIVERCAPS Caps = QueryAdapterInfo->pOutputData;

            if (Caps == NULL)
                return STATUS_INVALID_PARAMETER;
            if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_DRIVERCAPS))
                return STATUS_BUFFER_TOO_SMALL;

            RtlZeroMemory(Caps, sizeof(DXGK_DRIVERCAPS));
            Caps->HighestAcceptableAddress.QuadPart = (LONGLONG)-1;

            /* 64x64 ARGB hardware cursor on the HVS overlay plane. */
            if (DeviceExtension->CursorVa != NULL)
            {
                Caps->MaxPointerWidth = RPI5VC4_CURSOR_WIDTH;
                Caps->MaxPointerHeight = RPI5VC4_CURSOR_HEIGHT;
                Caps->PointerCaps.Color = 1;
            }

            Caps->MaxAllocationListSlotId = 255;
            Caps->ApertureSegmentCommitLimit =
                RPI5VC4_APERTURE_COMMIT_LIMIT;
            Caps->GpuEngineTopology.NbAsymetricProcessingNodes =
                RPI5VC4_GPU_NODE_COUNT;
            Caps->WDDMVersion = DXGKDDI_WDDMv2_ENUM;
            Caps->SupportNonVGA = TRUE;
            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_QUERYSEGMENT:
        {
            PDXGK_QUERYSEGMENTOUT SegOut = QueryAdapterInfo->pOutputData;
            PDXGK_SEGMENTDESCRIPTOR Desc;

            if (SegOut == NULL)
                return STATUS_INVALID_PARAMETER;
            if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT))
                return STATUS_BUFFER_TOO_SMALL;

            if (SegOut->pSegmentDescriptor == NULL)
            {
                /* Phase 1: report the segment count. */
                SegOut->NbSegment = RPI5VC4_SEGMENT_COUNT;
                SegOut->PagingBufferSegmentId = RPI5VC4_SEGMENT_ID;
                SegOut->PagingBufferSize = 64 * 1024;
                return STATUS_SUCCESS;
            }

            if (SegOut->NbSegment < RPI5VC4_SEGMENT_COUNT)
                return STATUS_INVALID_PARAMETER;

            /*
             * Phase 2: one CPU-visible local VRAM segment — the contiguous
             * write-combined slab both the HVS scanout and the V3D can
             * reach directly (no aperture indirection).
             */
            Desc = &SegOut->pSegmentDescriptor[0];
            RtlZeroMemory(Desc, sizeof(*Desc));
            Desc->BaseAddress = DeviceExtension->VramPhysical;
            Desc->CpuTranslatedAddress = DeviceExtension->VramPhysical;
            Desc->Size = DeviceExtension->VramSize;
            Desc->CommitLimit = DeviceExtension->VramSize;
            Desc->Flags.CpuVisible = 1;
            Desc->Flags.LocalBudgetGroup = 1;

            Desc = &SegOut->pSegmentDescriptor[1];
            RtlZeroMemory(Desc, sizeof(*Desc));
            Desc->Size = (SIZE_T)RPI5VC4_APERTURE_SIZE;
            Desc->CommitLimit =
                (SIZE_T)RPI5VC4_APERTURE_COMMIT_LIMIT;
            Desc->Flags.Aperture = 1;
            Desc->Flags.CpuVisible = 1;
            Desc->Flags.ApplicationTarget = 1;
            Desc->Flags.NonLocalBudgetGroup = 1;
            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_QUERYSEGMENT4:
        {
            /* WDDM 2.0 flavour: stride-addressed descriptor array. */
            PDXGK_QUERYSEGMENTOUT4 SegOut = QueryAdapterInfo->pOutputData;
            PDXGK_SEGMENTDESCRIPTOR4 Desc;

            if (SegOut == NULL)
                return STATUS_INVALID_PARAMETER;
            if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT4))
                return STATUS_BUFFER_TOO_SMALL;

            if (SegOut->pSegmentDescriptor == NULL)
            {
                SegOut->NbSegment = RPI5VC4_SEGMENT_COUNT;
                SegOut->PagingBufferSegmentId = RPI5VC4_SEGMENT_ID;
                SegOut->PagingBufferSize = 64 * 1024;
                SegOut->PagingBufferPrivateDataSize = 0;
                return STATUS_SUCCESS;
            }

            if (SegOut->NbSegment < RPI5VC4_SEGMENT_COUNT ||
                SegOut->SegmentDescriptorStride < sizeof(DXGK_SEGMENTDESCRIPTOR4))
            {
                return STATUS_INVALID_PARAMETER;
            }

            Desc = (PDXGK_SEGMENTDESCRIPTOR4)SegOut->pSegmentDescriptor;
            RtlZeroMemory(Desc, sizeof(*Desc));
            Desc->Flags.CpuVisible = 1;
            Desc->Flags.LocalBudgetGroup = 1;
            Desc->BaseAddress = DeviceExtension->VramPhysical;
            Desc->CpuTranslatedAddress = DeviceExtension->VramPhysical;
            Desc->Size = DeviceExtension->VramSize;
            Desc->CommitLimit = DeviceExtension->VramSize;

            Desc = (PDXGK_SEGMENTDESCRIPTOR4)
                ((PUCHAR)SegOut->pSegmentDescriptor +
                 SegOut->SegmentDescriptorStride);
            RtlZeroMemory(Desc, sizeof(*Desc));
            Desc->Flags.Aperture = 1;
            Desc->Flags.CpuVisible = 1;
            Desc->Flags.ApplicationTarget = 1;
            Desc->Flags.NonLocalBudgetGroup = 1;
            Desc->Size = (SIZE_T)RPI5VC4_APERTURE_SIZE;
            Desc->CommitLimit =
                (SIZE_T)RPI5VC4_APERTURE_COMMIT_LIMIT;
            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_QUERYSEGMENT3:
        {
            /* WDDM 2.x flavour of the same two-pass protocol. */
            PDXGK_QUERYSEGMENTOUT3 SegOut = QueryAdapterInfo->pOutputData;
            PDXGK_SEGMENTDESCRIPTOR3 Desc;

            if (SegOut == NULL)
                return STATUS_INVALID_PARAMETER;
            if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT3))
                return STATUS_BUFFER_TOO_SMALL;

            if (SegOut->pSegmentDescriptor == NULL)
            {
                SegOut->NbSegment = RPI5VC4_SEGMENT_COUNT;
                SegOut->PagingBufferSegmentId = RPI5VC4_SEGMENT_ID;
                SegOut->PagingBufferSize = 64 * 1024;
                SegOut->PagingBufferPrivateDataSize = 0;
                return STATUS_SUCCESS;
            }

            if (SegOut->NbSegment < RPI5VC4_SEGMENT_COUNT)
                return STATUS_INVALID_PARAMETER;

            Desc = &SegOut->pSegmentDescriptor[0];
            RtlZeroMemory(Desc, sizeof(*Desc));
            Desc->Flags.CpuVisible = 1;
            Desc->Flags.LocalBudgetGroup = 1;
            Desc->BaseAddress = DeviceExtension->VramPhysical;
            Desc->CpuTranslatedAddress = DeviceExtension->VramPhysical;
            Desc->Size = DeviceExtension->VramSize;
            Desc->CommitLimit = DeviceExtension->VramSize;

            Desc = &SegOut->pSegmentDescriptor[1];
            RtlZeroMemory(Desc, sizeof(*Desc));
            Desc->Flags.Aperture = 1;
            Desc->Flags.CpuVisible = 1;
            Desc->Flags.ApplicationTarget = 1;
            Desc->Flags.NonLocalBudgetGroup = 1;
            Desc->Size = (SIZE_T)RPI5VC4_APERTURE_SIZE;
            Desc->CommitLimit =
                (SIZE_T)RPI5VC4_APERTURE_COMMIT_LIMIT;
            return STATUS_SUCCESS;
        }

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiGetStandardAllocationDriverData(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_GETSTANDARDALLOCATIONDRIVERDATA GetStandardAllocationDriverData)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    RPI5VC4_STANDARD_ALLOCATION_DATA PrivateData;
    UINT SuppliedPrivateSize;

    if (DeviceExtension == NULL || GetStandardAllocationDriverData == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (GetStandardAllocationDriverData->StandardAllocationType)
    {
        case DXGK_STDALLOCATION_SHAREDPRIMARYSURFACE:
        case DXGK_STDALLOCATION_SHADOWSURFACE:
        case DXGK_STDALLOCATION_STAGINGSURFACE:
        case DXGK_STDALLOCATION_GDISURFACE:
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&PrivateData, sizeof(PrivateData));
    PrivateData.Magic = RPI5VC4_STANDARD_ALLOCATION_MAGIC;
    PrivateData.Version = RPI5VC4_STANDARD_ALLOCATION_VERSION;
    PrivateData.Type =
        GetStandardAllocationDriverData->StandardAllocationType;

    SuppliedPrivateSize =
        GetStandardAllocationDriverData->AllocationPrivateDriverDataSize;
    GetStandardAllocationDriverData->AllocationPrivateDriverDataSize =
        sizeof(PrivateData);
    GetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0;

    if (GetStandardAllocationDriverData->pAllocationPrivateDriverData == NULL)
        return STATUS_SUCCESS;
    if (SuppliedPrivateSize < sizeof(PrivateData))
        return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(
        GetStandardAllocationDriverData->pAllocationPrivateDriverData,
        &PrivateData,
        sizeof(PrivateData));
    return STATUS_SUCCESS;
}

/* ========================================================================
 * Device / context / allocation objects
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiCreateProcess(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_CREATEPROCESS *CreateProcess)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension =
        (PRPI5VC4_DEVICE_EXTENSION)hAdapter;
    PRPI5VC4_PROCESS Process;
    PHYSICAL_ADDRESS Low;
    PHYSICAL_ADDRESS High;
    PHYSICAL_ADDRESS Boundary;

    if (DeviceExtension == NULL || CreateProcess == NULL)
        return STATUS_INVALID_PARAMETER;

    Process = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Process),
                                    RPI5VC4_POOL_TAG);
    if (Process == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Process, sizeof(*Process));

    Low.QuadPart = 0;
    High.QuadPart = 0xFFFFFFFFFFULL;
    Boundary.QuadPart = 0;
    Process->V3dPageTable = MmAllocateContiguousMemorySpecifyCache(
        RPI5VC4_V3D_PT_SIZE, Low, High, Boundary, MmWriteCombined);
    if (Process->V3dPageTable == NULL)
    {
        ExFreePoolWithTag(Process, RPI5VC4_POOL_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (DeviceExtension->V3dPageTable != NULL)
    {
        RtlCopyMemory(Process->V3dPageTable,
                      DeviceExtension->V3dPageTable,
                      RPI5VC4_V3D_PT_SIZE);
    }
    else
    {
        RtlZeroMemory(Process->V3dPageTable, RPI5VC4_V3D_PT_SIZE);
    }
    __dsb(_ARM64_BARRIER_SY);
    KeMemoryBarrier();

    Process->Magic = RPI5VC4_PROCESS_MAGIC;
    Process->Adapter = DeviceExtension;
    Process->hDxgkProcess = CreateProcess->hDxgkProcess;
    Process->V3dPageTablePhys = MmGetPhysicalAddress(Process->V3dPageTable);
    CreateProcess->hKmdProcess = (HANDLE)Process;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiDestroyProcess(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST HANDLE hKmdProcess)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension =
        (PRPI5VC4_DEVICE_EXTENSION)hAdapter;
    PRPI5VC4_PROCESS Process = (PRPI5VC4_PROCESS)hKmdProcess;
    KIRQL OldIrql;
    ULONG Node;
    ULONG TeeUs = 0;
    ULONG ReeUs = 0;
    BOOLEAN SmsOk = TRUE;
    BOOLEAN MmuOk = TRUE;
    ULONG HardwarePageTable = 0;

    if (Process == NULL)
        return STATUS_SUCCESS;
    if (DeviceExtension == NULL ||
        Process->Magic != RPI5VC4_PROCESS_MAGIC ||
        Process->Adapter != DeviceExtension)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    for (Node = 0; Node < RPI5VC4_GPU_NODE_COUNT; ++Node)
    {
        ULONG Offset;

        for (Offset = 0;
             Offset < DeviceExtension->NodeQueue[Node].Count;
             ++Offset)
        {
            ULONG Index =
                (DeviceExtension->NodeQueue[Node].Head + Offset) %
                RPI5VC4_MAX_PENDING;

            if (DeviceExtension->NodeQueue[Node].Pending[Index].Process ==
                Process)
            {
                KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
                return STATUS_DEVICE_BUSY;
            }
        }
    }

    if (DeviceExtension->V3dActiveProcess == Process)
    {
        if (InterlockedCompareExchange(
                &DeviceExtension->V3dExecutionBusy, 0, 0) != 0 ||
            DeviceExtension->V3dExecGateActive ||
            Rpi5Vc4GpuJobActiveLocked(DeviceExtension))
        {
            KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
            return STATUS_DEVICE_BUSY;
        }

        /*
         * The V3D MMU is a single hardware address space.  A completed fence
         * proves command retirement, but the hub may still retain page-walk
         * or cache state for the process table.  Reset the already-idle core,
         * restore the invariant registers, then bind and flush the permanent
         * adapter table before the process table can return to the page
         * allocator.  This is the normal final-context-switch boundary, not a
         * delay-based lifetime workaround.
         */
        if (DeviceExtension->V3dReady)
        {
            SmsOk = Rpi5V3dSmsPowerUpBounded(DeviceExtension,
                                             &TeeUs,
                                             &ReeUs);
            if (SmsOk)
            {
                PUCHAR Core = (PUCHAR)DeviceExtension->V3dCoreBase;
                PUCHAR Hub = (PUCHAR)DeviceExtension->V3dHubBase;

                WRITE_REGISTER_ULONG((PULONG)(Core + V3D_CTL_L2TFLSTA), 0);
                WRITE_REGISTER_ULONG((PULONG)(Core + V3D_CTL_L2TFLEND), ~0u);
                WRITE_REGISTER_ULONG((PULONG)(Core + V3D_CTL_INT_MSK_SET), ~0u);
                WRITE_REGISTER_ULONG((PULONG)(Core + V3D_CTL_INT_CLR), ~0u);
                WRITE_REGISTER_ULONG((PULONG)(Hub + V3D_HUB_INT_MSK_SET), ~0u);
                WRITE_REGISTER_ULONG((PULONG)(Hub + V3D_HUB_INT_CLR), ~0u);
                if (DeviceExtension->V3dCoreIrqConnected)
                {
                    WRITE_REGISTER_ULONG(
                        (PULONG)(Core + V3D_CTL_INT_MSK_CLR),
                        V3D_INT_FLDONE | V3D_INT_FRDONE |
                            V3D_INT_OUTOMEM | V3D_V7_INT_CSDDONE);
                }
                if (DeviceExtension->V3dHubIrqConnected)
                {
                    WRITE_REGISTER_ULONG(
                        (PULONG)(Hub + V3D_HUB_INT_MSK_CLR),
                        V3D_HUB_INT_TFUC);
                }

                MmuOk = Rpi5V3dMmuProgramPageTableBounded(
                    DeviceExtension,
                    DeviceExtension->V3dPageTablePhys);
                HardwarePageTable = READ_REGISTER_ULONG(
                    (PULONG)(Hub + V3D_MMU_PT_PA_BASE));
                MmuOk = MmuOk &&
                    HardwarePageTable ==
                        (ULONG)(DeviceExtension->V3dPageTablePhys.QuadPart >>
                                V3D_MMU_PAGE_SHIFT);
            }
        }

        if (!SmsOk || !MmuOk)
        {
            KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
            return STATUS_DEVICE_HARDWARE_ERROR;
        }

        DeviceExtension->V3dActiveProcess = NULL;
    }
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);

    __dsb(_ARM64_BARRIER_SY);
    KeMemoryBarrier();
    Process->Magic = 0;
    Process->Adapter = NULL;
    if (Process->V3dPageTable != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(Process->V3dPageTable,
                                           RPI5VC4_V3D_PT_SIZE,
                                           MmWriteCombined);
    }
    __dsb(_ARM64_BARRIER_SY);
    KeMemoryBarrier();
    Process->V3dPageTable = NULL;
    Process->V3dPageTablePhys.QuadPart = 0;
    ExFreePoolWithTag(Process, RPI5VC4_POOL_TAG);
    return STATUS_SUCCESS;
}

SIZE_T
APIENTRY
Rpi5Vc4DdiGetRootPageTableSize(
    _In_ CONST HANDLE hAdapter,
    _Inout_ DXGKARG_GETROOTPAGETABLESIZE *Args)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension =
        (PRPI5VC4_DEVICE_EXTENSION)hAdapter;

    if (DeviceExtension == NULL || Args == NULL ||
        Args->PhysicalAdapterIndex != 0 ||
        Args->NumberOfPte == 0 ||
        Args->NumberOfPte > (1u << RPI5VC4_GPUVA_INDEX_BITS))
    {
        return 0;
    }

    return (SIZE_T)Args->NumberOfPte * sizeof(DXGK_PTE);
}

VOID
APIENTRY
Rpi5Vc4DdiSetRootPageTable(
    _In_ CONST HANDLE hAdapter,
    _In_ CONST DXGKARG_SETROOTPAGETABLE *SetPageTable)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension =
        (PRPI5VC4_DEVICE_EXTENSION)hAdapter;
    PRPI5VC4_CONTEXT Context;
    PRPI5VC4_PROCESS Process;

    if (DeviceExtension == NULL || SetPageTable == NULL ||
        SetPageTable->Address.SegmentId != 0 ||
        SetPageTable->Address.SegmentOffset == 0 ||
        SetPageTable->NumEntries == 0 ||
        SetPageTable->NumEntries >
            (1u << RPI5VC4_GPUVA_INDEX_BITS))
    {
        return;
    }

    Context = (PRPI5VC4_CONTEXT)SetPageTable->hContext;
    if (Context == NULL || Context->Magic != RPI5VC4_CONTEXT_MAGIC ||
        Context->Device == NULL ||
        Context->Device->Magic != RPI5VC4_DEVICE_MAGIC)
    {
        return;
    }

    Process = Context->Device->Process;
    if (Process == NULL || Process->Magic != RPI5VC4_PROCESS_MAGIC ||
        Process->Adapter != DeviceExtension)
    {
        return;
    }

    Process->RootPageTableAddress = SetPageTable->Address;
    Process->RootPageTableEntries = SetPageTable->NumEntries;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiCreateDevice(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEDEVICE CreateDevice)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    PRPI5VC4_WDDM_DEVICE Device;

    if (DeviceExtension == NULL || CreateDevice == NULL)
        return STATUS_INVALID_PARAMETER;

    Device = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Device),
                                   RPI5VC4_POOL_TAG);
    if (Device == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Device, sizeof(*Device));
    Device->Magic = RPI5VC4_DEVICE_MAGIC;
    Device->Adapter = DeviceExtension;
    Device->Process = (PRPI5VC4_PROCESS)CreateDevice->hKmdProcess;
    if (Device->Process == NULL ||
        Device->Process->Magic != RPI5VC4_PROCESS_MAGIC ||
        Device->Process->Adapter != DeviceExtension)
    {
        ExFreePoolWithTag(Device, RPI5VC4_POOL_TAG);
        return STATUS_INVALID_PARAMETER;
    }

    CreateDevice->hDevice = (HANDLE)Device;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiDestroyDevice(
    _In_ PVOID MiniportDeviceContext)
{
    PRPI5VC4_WDDM_DEVICE Device = MiniportDeviceContext;

    if (Device == NULL)
        return STATUS_SUCCESS;
    if (Device->Magic != RPI5VC4_DEVICE_MAGIC)
        return STATUS_INVALID_PARAMETER;

    Device->Magic = 0;
    ExFreePoolWithTag(Device, RPI5VC4_POOL_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiCreateContext(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATECONTEXT CreateContext)
{
    PRPI5VC4_WDDM_DEVICE Device = MiniportDeviceContext;
    PRPI5VC4_CONTEXT Context;

    if (Device == NULL || Device->Magic != RPI5VC4_DEVICE_MAGIC ||
        CreateContext == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (CreateContext->NodeOrdinal >= RPI5VC4_GPU_NODE_COUNT)
        return STATUS_INVALID_PARAMETER;

    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context),
                                    RPI5VC4_POOL_TAG);
    if (Context == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Magic = RPI5VC4_CONTEXT_MAGIC;
    Context->Device = Device;

    CreateContext->ContextInfo.DmaBufferSize = 64 * 1024;
    CreateContext->ContextInfo.DmaBufferSegmentSet = 0; /* system memory */
    CreateContext->ContextInfo.DmaBufferPrivateDataSize = 0;
    CreateContext->ContextInfo.AllocationListSize = 256;
    CreateContext->ContextInfo.PatchLocationListSize = 256;

    CreateContext->hContext = (HANDLE)Context;
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiDestroyContext(
    _In_ PVOID MiniportDeviceContext)
{
    PRPI5VC4_CONTEXT Context = MiniportDeviceContext;

    if (Context == NULL)
        return STATUS_SUCCESS;
    if (Context->Magic != RPI5VC4_CONTEXT_MAGIC)
        return STATUS_INVALID_PARAMETER;

    Context->Magic = 0;
    ExFreePoolWithTag(Context, RPI5VC4_POOL_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiCreateAllocation(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEALLOCATION CreateAllocation)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    ULONG i;

    if (DeviceExtension == NULL || CreateAllocation == NULL ||
        (CreateAllocation->NumAllocations != 0 &&
         CreateAllocation->pAllocationInfo == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < CreateAllocation->NumAllocations; i++)
    {
        DXGK_ALLOCATIONINFO *Info = &CreateAllocation->pAllocationInfo[i];
        PRPI5VC4_ALLOCATION Allocation;
        CONST RPI5VC4_STANDARD_ALLOCATION_DATA *PrivateData;
        BOOLEAN StandardAllocation;
        ULONG SegmentId;
        SIZE_T Size = (Info->Size != 0) ? Info->Size : PAGE_SIZE;

        PrivateData = (CONST RPI5VC4_STANDARD_ALLOCATION_DATA *)
            Info->pPrivateDriverData;
        StandardAllocation =
            Info->PrivateDriverDataSize >= sizeof(*PrivateData) &&
            PrivateData != NULL &&
            PrivateData->Magic == RPI5VC4_STANDARD_ALLOCATION_MAGIC &&
            PrivateData->Version == RPI5VC4_STANDARD_ALLOCATION_VERSION &&
            (PrivateData->Type == DXGK_STDALLOCATION_SHAREDPRIMARYSURFACE ||
             PrivateData->Type == DXGK_STDALLOCATION_SHADOWSURFACE ||
             PrivateData->Type == DXGK_STDALLOCATION_STAGINGSURFACE ||
             PrivateData->Type == DXGK_STDALLOCATION_GDISURFACE);
        SegmentId = StandardAllocation ? RPI5VC4_LOCAL_SEGMENT_ID :
                                         RPI5VC4_APERTURE_SEGMENT_ID;

        if (Size > MAXULONG_PTR - (PAGE_SIZE - 1))
        {
            while (i > 0)
            {
                --i;
                ExFreePoolWithTag((PVOID)CreateAllocation->pAllocationInfo[i].hAllocation, RPI5VC4_POOL_TAG);
                CreateAllocation->pAllocationInfo[i].hAllocation = NULL;
            }
            return STATUS_INTEGER_OVERFLOW;
        }
        Size = (Size + PAGE_SIZE - 1) & ~(SIZE_T)(PAGE_SIZE - 1);
        if ((StandardAllocation && Size > DeviceExtension->VramSize) ||
            (!StandardAllocation &&
             (ULONGLONG)Size > RPI5VC4_APERTURE_COMMIT_LIMIT))
        {
            while (i > 0)
            {
                --i;
                ExFreePoolWithTag(
                    (PVOID)CreateAllocation->pAllocationInfo[i].hAllocation,
                    RPI5VC4_POOL_TAG);
                CreateAllocation->pAllocationInfo[i].hAllocation = NULL;
            }
            return STATUS_GRAPHICS_NO_VIDEO_MEMORY;
        }

        Allocation = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Allocation),
                                           RPI5VC4_POOL_TAG);
        if (Allocation == NULL)
        {
            while (i > 0)
            {
                --i;
                ExFreePoolWithTag(
                    (PVOID)CreateAllocation->pAllocationInfo[i].hAllocation,
                    RPI5VC4_POOL_TAG);
                CreateAllocation->pAllocationInfo[i].hAllocation = NULL;
            }
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(Allocation, sizeof(*Allocation));
        Allocation->Magic = RPI5VC4_ALLOCATION_MAGIC;
        Allocation->Size = Size;

        Info->Size = Size;
        Info->Alignment = PAGE_SIZE;
        Info->SupportedReadSegmentSet = (1 << (SegmentId - 1));
        Info->SupportedWriteSegmentSet = (1 << (SegmentId - 1));
        Info->EvictionSegmentSet = 0;
        Info->PreferredSegment.Value = 0;
        Info->PreferredSegment.SegmentId0 = SegmentId;
        Info->Flags.CpuVisible = 1;
        Info->Flags.AccessedPhysically = StandardAllocation;
        Info->hAllocation = (HANDLE)Allocation;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiDestroyAllocation(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_DESTROYALLOCATION *DestroyAllocation)
{
    ULONG i;

    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (DestroyAllocation == NULL ||
        (DestroyAllocation->NumAllocations != 0 &&
         DestroyAllocation->phAllocation == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < DestroyAllocation->NumAllocations; i++)
    {
        PRPI5VC4_ALLOCATION Allocation = (PRPI5VC4_ALLOCATION)DestroyAllocation->phAllocation[i];

        if (Allocation == NULL)
            continue;
        if (Allocation->Magic != RPI5VC4_ALLOCATION_MAGIC)
            return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < DestroyAllocation->NumAllocations; i++)
    {
        PRPI5VC4_ALLOCATION Allocation = (PRPI5VC4_ALLOCATION)DestroyAllocation->phAllocation[i];

        if (Allocation == NULL)
            continue;
        Allocation->Magic = 0;
        ExFreePoolWithTag(Allocation, RPI5VC4_POOL_TAG);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiOpenAllocation(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_OPENALLOCATION *OpenAllocation)
{
    ULONG i;

    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (OpenAllocation == NULL ||
        (OpenAllocation->NumAllocations != 0 &&
         OpenAllocation->pOpenAllocation == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < OpenAllocation->NumAllocations; i++)
    {
        DXGK_OPENALLOCATIONINFO *Info = &OpenAllocation->pOpenAllocation[i];
        PRPI5VC4_OPENALLOCATION Open;

        Open = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Open),
                                     RPI5VC4_POOL_TAG);
        if (Open == NULL)
        {
            while (i > 0)
            {
                --i;
                ExFreePoolWithTag(
                    OpenAllocation->pOpenAllocation[i].hDeviceSpecificAllocation,
                    RPI5VC4_POOL_TAG);
                OpenAllocation->pOpenAllocation[i].hDeviceSpecificAllocation = NULL;
            }
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(Open, sizeof(*Open));
        Open->Magic = RPI5VC4_OPENALLOC_MAGIC;
        Open->hVidMmAllocation = Info->hAllocation;

        Info->hDeviceSpecificAllocation = (HANDLE)Open;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiCloseAllocation(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_CLOSEALLOCATION *CloseAllocation)
{
    ULONG i;

    UNREFERENCED_PARAMETER(MiniportDeviceContext);

    if (CloseAllocation == NULL ||
        (CloseAllocation->NumAllocations != 0 &&
         CloseAllocation->pOpenHandleList == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < CloseAllocation->NumAllocations; i++)
    {
        PRPI5VC4_OPENALLOCATION Open = (PRPI5VC4_OPENALLOCATION)CloseAllocation->pOpenHandleList[i];

        if (Open == NULL)
            continue;
        if (Open->Magic != RPI5VC4_OPENALLOC_MAGIC)
            return STATUS_INVALID_PARAMETER;
    }

    for (i = 0; i < CloseAllocation->NumAllocations; i++)
    {
        PRPI5VC4_OPENALLOCATION Open = (PRPI5VC4_OPENALLOCATION)CloseAllocation->pOpenHandleList[i];

        if (Open == NULL)
            continue;
        Open->Magic = 0;
        ExFreePoolWithTag(Open, RPI5VC4_POOL_TAG);
    }

    return STATUS_SUCCESS;
}

/* ========================================================================
 * Render / Present — DMA stream generation
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiRender(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_RENDER Render)
{
    PRPI5VC4_CONTEXT Context = MiniportDeviceContext;

    if (Context == NULL ||
        (Context->Magic != RPI5VC4_CONTEXT_MAGIC &&
         Context->Magic != RPI5VC4_DEVICE_MAGIC) ||
        Render == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Render->pCommand == NULL || Render->CommandLength == 0 ||
        Render->pDmaBuffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Render->DmaSize < Render->CommandLength)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    /*
     * The user-mode command stream is the hardware-consumable RPI5VC4
     * packet stream (see rpi5vc4.h); validate before accepting it.
     */
    if (!Rpi5Vc4ParseDmaStream(Render->pCommand, Render->CommandLength,
                               NULL, NULL, NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlCopyMemory(Render->pDmaBuffer, Render->pCommand,
                  Render->CommandLength);

    /*
     * Emit patch locations for allocation-relative V3D jobs (WDDM
     * contract: the KMD parses its own stream and reports every dword
     * the scheduler must relocate at Patch time).  AllocationOffset
     * carries the offset-within-allocation the packet supplied.
     */
    {
        PUCHAR Base = (PUCHAR)Render->pDmaBuffer;
        ULONG Offset = 0;
        D3DDDI_PATCHLOCATIONLIST *Out = Render->pPatchLocationListOut;
        UINT OutSpace = Render->PatchLocationListOutSize;

        while (Offset + sizeof(RPI5VC4_DMA_PACKET) <= Render->CommandLength)
        {
            PRPI5VC4_DMA_PACKET Packet = (PRPI5VC4_DMA_PACKET)(Base + Offset);
            ULONG Pair;

            if (Packet->Magic != RPI5VC4_DMA_PACKET_MAGIC ||
                Packet->Length < sizeof(RPI5VC4_DMA_PACKET))
            {
                break;
            }

            if (Packet->Op == RPI5VC4_DMA_OP_V3D_JOB)
            {
                /* Pair 0 = BCL (Start,End), pair 1 = RCL. */
                for (Pair = 0; Pair < 2; Pair++)
                {
                    ULONG IndexPlusOne = (Pair == 0)
                        ? Packet->V3dJob.BclAllocIndexPlusOne
                        : Packet->V3dJob.RclAllocIndexPlusOne;
                    ULONG FieldOffset = (ULONG)FIELD_OFFSET(RPI5VC4_DMA_PACKET,
                                                            V3dJob.BclStart) +
                                        Pair * 2 * sizeof(ULONG);
                    ULONG Word;

                    if (IndexPlusOne == 0)
                        continue;

                    if (IndexPlusOne - 1 >= Render->AllocationListSize)
                        return STATUS_INVALID_PARAMETER;
                    if (Out == NULL || OutSpace < 2)
                        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

                    for (Word = 0; Word < 2; Word++)
                    {
                        RtlZeroMemory(Out, sizeof(*Out));
                        Out->AllocationIndex = IndexPlusOne - 1;
                        Out->PatchOffset = Offset + FieldOffset +
                                           Word * sizeof(ULONG);
                        Out->AllocationOffset =
                            ((PULONG)((PUCHAR)Packet +
                                      FieldOffset))[Word];
                        Out++;
                        OutSpace--;
                    }
                }
            }

            Offset += Packet->Length;
        }

        Render->pPatchLocationListOut = Out;
    }

    Render->pDmaBuffer = (PUCHAR)Render->pDmaBuffer + Render->CommandLength;
    Render->MultipassOffset = 0;

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiPresent(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_PRESENT Present)
{
    PRPI5VC4_WDDM_DEVICE Device;
    PRPI5VC4_CONTEXT Context;
    PRPI5VC4_DMA_PACKET Packet;

    if (MiniportDeviceContext == NULL || Present == NULL)
        return STATUS_INVALID_PARAMETER;

    Context = MiniportDeviceContext;
    if (Context->Magic == RPI5VC4_CONTEXT_MAGIC)
        Device = Context->Device;
    else if (Context->Magic == RPI5VC4_DEVICE_MAGIC)
        Device = MiniportDeviceContext;
    else
        return STATUS_INVALID_HANDLE;
    if (Device == NULL || Device->Magic != RPI5VC4_DEVICE_MAGIC || Device->Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (Present->pDmaBuffer == NULL ||
        Present->DmaSize < sizeof(RPI5VC4_DMA_PACKET))
    {
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
    }

    /*
     * Presents are executed by dxgkrnl's blit path and, for scanout
     * changes, DxgkDdiSetVidPnSourceAddress.  Emit a descriptive packet
     * so the submission carries the operation through the fence pipeline.
     */
    Packet = Present->pDmaBuffer;
    RtlZeroMemory(Packet, sizeof(*Packet));
    Packet->Magic = RPI5VC4_DMA_PACKET_MAGIC;
    Packet->Op = RPI5VC4_DMA_OP_PRESENT;
    Packet->Length = sizeof(*Packet);
    Packet->Present.DstRect = Present->DstRect;
    Packet->Present.Color = Present->Color;
    Packet->Present.Flags = Present->Flags.Value;

    Present->pDmaBuffer = (PUCHAR)Present->pDmaBuffer + sizeof(*Packet);

    /*
     * UMD-track Stage 2 (A/B bring-up form): for plain equal-size 32bpp
     * blits, additionally emit a TFU raster-copy job over the same
     * pixels — allocation-relative, so DxgkDdiPatch resolves real GPU
     * VAs.  The CPU present path still moves the pixels (correctness is
     * never at risk); on V3D silicon the TFU runs the same copy under a
     * real workload, validating the engine via fence completion.  The
     * executor skips the job if patching didn't produce slab GPU VAs.
     */
    if (Device->Adapter != NULL &&
        Device->Adapter->V3dReady &&
        Device->Adapter->BitsPerPixel == 32 &&
        Present->Flags.Blt &&
        Present->pPatchLocationListOut != NULL &&
        Present->PatchLocationListOutSize >= 2 &&
        Present->DmaSize >= 2 * sizeof(RPI5VC4_DMA_PACKET) &&
        (Present->SrcRect.right - Present->SrcRect.left) ==
            (Present->DstRect.right - Present->DstRect.left) &&
        (Present->SrcRect.bottom - Present->SrcRect.top) ==
            (Present->DstRect.bottom - Present->DstRect.top) &&
        Present->DstRect.right > Present->DstRect.left &&
        Present->DstRect.bottom > Present->DstRect.top)
    {
        PRPI5VC4_DEVICE_EXTENSION Adapter = Device->Adapter;
        PRPI5VC4_DMA_PACKET Tfu = (PRPI5VC4_DMA_PACKET)Present->pDmaBuffer;
        ULONG Pitch = Adapter->BytesPerScanLine;
        ULONG Width = Present->DstRect.right - Present->DstRect.left;
        ULONG Height = Present->DstRect.bottom - Present->DstRect.top;
        D3DDDI_PATCHLOCATIONLIST *Loc = Present->pPatchLocationListOut;
        ULONG TfuPacketOffset =
            (ULONG)((PUCHAR)Tfu - (PUCHAR)Packet) /* second packet */;

        RtlZeroMemory(Tfu, sizeof(*Tfu));
        Tfu->Magic = RPI5VC4_DMA_PACKET_MAGIC;
        Tfu->Op = RPI5VC4_DMA_OP_TFU_JOB;
        Tfu->Length = sizeof(*Tfu);
        Vc4CleTfuRasterCopyV71(Tfu->TfuJob.Regs, 0, Pitch, 0, Pitch, Width, Height, V3D71_TEXFMT_RGBA8, 4);

        /* IIA = source allocation + source-rect byte offset. */
        RtlZeroMemory(Loc, sizeof(*Loc));
        Loc->AllocationIndex = DXGK_PRESENT_SOURCE_INDEX;
        Loc->PatchOffset = TfuPacketOffset +
                           (ULONG)FIELD_OFFSET(RPI5VC4_DMA_PACKET,
                                               TfuJob.Regs[1]);
        Loc->AllocationOffset = Present->SrcRect.top * Pitch +
                                Present->SrcRect.left * 4;
        Loc++;

        /* IOA = destination allocation + dest-rect byte offset. */
        RtlZeroMemory(Loc, sizeof(*Loc));
        Loc->AllocationIndex = DXGK_PRESENT_DESTINATION_INDEX;
        Loc->PatchOffset = TfuPacketOffset +
                           (ULONG)FIELD_OFFSET(RPI5VC4_DMA_PACKET,
                                               TfuJob.Regs[6]);
        Loc->AllocationOffset = Present->DstRect.top * Pitch +
                                Present->DstRect.left * 4;
        Loc++;

        Present->pPatchLocationListOut = Loc;
        Present->pDmaBuffer = (PUCHAR)Present->pDmaBuffer + sizeof(*Tfu);
    }

    Present->MultipassOffset = 0;

    return STATUS_SUCCESS;
}

static NTSTATUS
Rpi5Vc4RememberDmaMapping(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST DXGKARG_PATCH *Patch)
{
    KIRQL OldIrql;
    ULONG FreeIndex = MAXULONG;
    ULONG i;

    if (Patch->pDmaBuffer == NULL || Patch->DmaBufferSize == 0)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    if (DeviceExtension->StopAccepting)
    {
        KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
        return STATUS_DELETE_PENDING;
    }
    for (i = 0; i < RPI5VC4_DMA_MAPPING_COUNT; ++i)
    {
        ULONG Index = (DeviceExtension->DmaMappingNext + i) % RPI5VC4_DMA_MAPPING_COUNT;
        PRPI5VC4_DMA_MAPPING Mapping = &DeviceExtension->DmaMappings[Index];

        if (Mapping->VirtualAddress != NULL && Mapping->SegmentId == Patch->DmaBufferSegmentId && Mapping->PhysicalAddress.QuadPart == Patch->DmaBufferPhysicalAddress.QuadPart)
        {
            KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
            return STATUS_DEVICE_BUSY;
        }
        if (FreeIndex == MAXULONG && Mapping->VirtualAddress == NULL)
            FreeIndex = Index;
    }
    if (FreeIndex == MAXULONG)
    {
        KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }
    DeviceExtension->DmaMappings[FreeIndex].SegmentId = Patch->DmaBufferSegmentId;
    DeviceExtension->DmaMappings[FreeIndex].PhysicalAddress = Patch->DmaBufferPhysicalAddress;
    DeviceExtension->DmaMappings[FreeIndex].VirtualAddress = Patch->pDmaBuffer;
    DeviceExtension->DmaMappings[FreeIndex].Size = Patch->DmaBufferSize;
    DeviceExtension->DmaMappingNext = (FreeIndex + 1) % RPI5VC4_DMA_MAPPING_COUNT;
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
    return STATUS_SUCCESS;
}

static BOOLEAN
Rpi5Vc4TakeDmaMappingLocked(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ UINT SegmentId,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _Out_ PRPI5VC4_DMA_MAPPING OutMapping)
{
    ULONG i;

    for (i = 0; i < RPI5VC4_DMA_MAPPING_COUNT; ++i)
    {
        PRPI5VC4_DMA_MAPPING Mapping = &DeviceExtension->DmaMappings[i];

        if (Mapping->SegmentId == SegmentId && Mapping->PhysicalAddress.QuadPart == PhysicalAddress.QuadPart && Mapping->VirtualAddress != NULL)
        {
            *OutMapping = *Mapping;
            RtlZeroMemory(Mapping, sizeof(*Mapping));
            return TRUE;
        }
    }
    return FALSE;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiPatch(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_PATCH *Patch)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    ULONGLONG SlabPhys;
    NTSTATUS Status;
    UINT i;

    if (DeviceExtension == NULL || Patch == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Patch->pDmaBuffer == NULL || Patch->DmaBufferSize == 0 || Patch->DmaBufferSubmissionStartOffset > Patch->DmaBufferSubmissionEndOffset || Patch->DmaBufferSubmissionEndOffset > Patch->DmaBufferSize || Patch->PatchLocationListSubmissionStart > Patch->PatchLocationListSize || Patch->PatchLocationListSubmissionLength > Patch->PatchLocationListSize - Patch->PatchLocationListSubmissionStart || (Patch->PatchLocationListSize != 0 && Patch->pPatchLocationList == NULL) || (Patch->AllocationListSize != 0 && Patch->pAllocationList == NULL))
        return STATUS_INVALID_PARAMETER;

    /*
     * Documented WDDM relocation: each patch-location entry names an
     * allocation (whose placement vidmm reports in pAllocationList as a
     * segment physical address) and a dword offset in the DMA buffer to
     * rewrite.  The V3D reads GPU virtual addresses, so the patched
     * value is SlabGpuVa + (allocation physical - slab physical) +
     * AllocationOffset.  Packets that already carry absolute GPU VAs
     * simply come with no patch locations.
     */
    if (Patch->PatchLocationListSize == 0)
        return Rpi5Vc4RememberDmaMapping(DeviceExtension, Patch);

    SlabPhys = (ULONGLONG)DeviceExtension->VramPhysical.QuadPart;

    for (i = Patch->PatchLocationListSubmissionStart; i < Patch->PatchLocationListSubmissionStart + Patch->PatchLocationListSubmissionLength; i++)
    {
        CONST D3DDDI_PATCHLOCATIONLIST *Loc = &Patch->pPatchLocationList[i];
        CONST DXGK_ALLOCATIONLIST *Alloc;
        ULONGLONG Phys;
        ULONGLONG SlabOffset;
        ULONGLONG GpuAddress;
        ULONG GpuVa;

        if (Loc->AllocationIndex >= Patch->AllocationListSize ||
            Patch->pAllocationList == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (Loc->PatchOffset < Patch->DmaBufferSubmissionStartOffset || Loc->PatchOffset > Patch->DmaBufferSubmissionEndOffset || sizeof(ULONG) > Patch->DmaBufferSubmissionEndOffset - Loc->PatchOffset)
        {
            return STATUS_INVALID_PARAMETER;
        }

        Alloc = &Patch->pAllocationList[Loc->AllocationIndex];
        Phys = (ULONGLONG)Alloc->PhysicalAddress.QuadPart;

        if (DeviceExtension->VramVa == NULL || Phys < SlabPhys)
        {
            /* Not slab memory: the V3D cannot reach it. */
            DPRINT1("RPI5VC4: Patch reject idx=%lu phys=%I64x seg=%u slab=%I64x+%lx\n",
                    Loc->AllocationIndex, Phys, Alloc->SegmentId,
                    SlabPhys, DeviceExtension->VramSize);
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        }
        SlabOffset = Phys - SlabPhys;
        if (SlabOffset >= DeviceExtension->VramSize || Loc->AllocationOffset >= DeviceExtension->VramSize - SlabOffset || SlabOffset > MAXULONG - RPI5VC4_V3D_SLAB_GPUVA || Loc->AllocationOffset > MAXULONG - RPI5VC4_V3D_SLAB_GPUVA - SlabOffset)
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;

        GpuAddress = RPI5VC4_V3D_SLAB_GPUVA + SlabOffset + Loc->AllocationOffset;
        GpuVa = (ULONG)GpuAddress;

        *(ULONG UNALIGNED *)((PUCHAR)Patch->pDmaBuffer + Loc->PatchOffset) = GpuVa;
    }

    Status = Rpi5Vc4RememberDmaMapping(DeviceExtension, Patch);
    return Status;
}

/* ========================================================================
 * Submission / fences
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiSubmitCommand(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_SUBMITCOMMAND *SubmitCommand)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    RPI5VC4_DMA_PACKET Job;
    BOOLEAN HasJob = FALSE;
    BOOLEAN HasPresent = FALSE;
    PRPI5VC4_PENDING_SUBMIT Entry;
    KIRQL OldIrql;
    BOOLEAN Completed;
    BOOLEAN NeedPoll;
    BOOLEAN PipelineAborted;
    BOOLEAN MappingFound;
    BOOLEAN Stopping;
    RPI5VC4_DMA_MAPPING DmaMapping;
    PRPI5VC4_WDDM_DEVICE KmdDevice;
    PRPI5VC4_PROCESS Process;
    PVOID DmaBuffer;
    ULONG ExpectedNode;

    if (DeviceExtension == NULL || SubmitCommand == NULL)
        return STATUS_INVALID_PARAMETER;

    KmdDevice = (PRPI5VC4_WDDM_DEVICE)SubmitCommand->hDevice;
    if (KmdDevice == NULL ||
        KmdDevice->Magic != RPI5VC4_DEVICE_MAGIC ||
        KmdDevice->Adapter != DeviceExtension ||
        KmdDevice->Process == NULL ||
        KmdDevice->Process->Magic != RPI5VC4_PROCESS_MAGIC ||
        KmdDevice->Process->Adapter != DeviceExtension)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Process = KmdDevice->Process;

    RtlZeroMemory(&DmaMapping, sizeof(DmaMapping));
    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    MappingFound = Rpi5Vc4TakeDmaMappingLocked(DeviceExtension, SubmitCommand->DmaBufferSegmentId, SubmitCommand->DmaBufferPhysicalAddress, &DmaMapping);
    Stopping = DeviceExtension->StopAccepting;
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
    DmaBuffer = MappingFound ? DmaMapping.VirtualAddress : NULL;

    if (Stopping)
        return STATUS_DELETE_PENDING;

    if (SubmitCommand->NodeOrdinal >= RPI5VC4_GPU_NODE_COUNT ||
        SubmitCommand->EngineOrdinal != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((MappingFound && DmaMapping.Size != SubmitCommand->DmaBufferSize) || SubmitCommand->DmaBufferSubmissionStartOffset > SubmitCommand->DmaBufferSubmissionEndOffset || SubmitCommand->DmaBufferSubmissionEndOffset > SubmitCommand->DmaBufferSize)
        return STATUS_INVALID_PARAMETER;

    /*
     * Parse the submitted range strictly.  Hardware jobs execute on their
     * engine; the descriptive packet emitted by DxgkDdiPresent is valid work
     * that completes on the CPU after dxgkrnl has performed the CPU blit (or
     * the scanout change has gone through SetVidPnSourceAddress).  It must not
     * be mistaken for a missing GPU job.
     */
    if (!SubmitCommand->Flags.NullRendering)
    {
        if (DmaBuffer == NULL || SubmitCommand->DmaBufferSubmissionStartOffset == SubmitCommand->DmaBufferSubmissionEndOffset)
            return STATUS_INVALID_PARAMETER;
        if (!Rpi5Vc4ParseDmaStream((PUCHAR)DmaBuffer + SubmitCommand->DmaBufferSubmissionStartOffset, SubmitCommand->DmaBufferSubmissionEndOffset - SubmitCommand->DmaBufferSubmissionStartOffset, &Job, &HasJob, &HasPresent))
            return STATUS_INVALID_PARAMETER;

        /* PRESENT is kernel-generated work, never a user Render opcode. */
        if (HasPresent &&
            !SubmitCommand->Flags.Present &&
            !SubmitCommand->Flags.RedirectedPresent)
        {
            return STATUS_INVALID_PARAMETER;
        }
        if ((SubmitCommand->Flags.Present ||
             SubmitCommand->Flags.RedirectedPresent) &&
            !HasPresent)
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (!HasJob && !HasPresent)
            return STATUS_INVALID_PARAMETER;
    }

    ExpectedNode = SubmitCommand->NodeOrdinal;
    if (HasJob)
    {
        if (!DeviceExtension->V3dReady)
            return STATUS_DEVICE_NOT_READY;
        if (Job.Op == RPI5VC4_DMA_OP_TFU_JOB &&
            (Job.TfuJob.Regs[1] == 0 || Job.TfuJob.Regs[6] == 0))
            return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
        if (Job.Op == RPI5VC4_DMA_OP_TFU_JOB)
            ExpectedNode = RPI5VC4_NODE_TFU;
        else if (Job.Op == RPI5VC4_DMA_OP_CSD_JOB)
            ExpectedNode = RPI5VC4_NODE_CSD;
        else
            ExpectedNode = RPI5VC4_NODE_3D;
        if (ExpectedNode != SubmitCommand->NodeOrdinal)
            return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);

    /*
     * The validated packet type and caller NodeOrdinal select the same queue;
     * CPU-completed null packets retain the caller's node for fence reporting.
     */
    {
        ULONG QueueIndex = SubmitCommand->NodeOrdinal;

        if (DeviceExtension->NodeQueue[QueueIndex].Count >= RPI5VC4_MAX_PENDING ||
            DeviceExtension->StopAccepting)
        {
            KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
            return STATUS_DEVICE_BUSY;
        }

        Entry = &DeviceExtension->NodeQueue[QueueIndex].Pending[
            (DeviceExtension->NodeQueue[QueueIndex].Head +
             DeviceExtension->NodeQueue[QueueIndex].Count) %
            RPI5VC4_MAX_PENDING];
        RtlZeroMemory(Entry, sizeof(*Entry));
        Entry->Fence = SubmitCommand->SubmissionFenceId;
        Entry->NodeOrdinal = QueueIndex;
        Entry->ReportNode = SubmitCommand->NodeOrdinal;
        Entry->RenderKicks = 0;
        Entry->Process = Process;
        if (HasJob && DeviceExtension->V3dReady)
        {
            if (Job.Op == RPI5VC4_DMA_OP_TFU_JOB)
            {
                Entry->IsTfuJob = TRUE;
                RtlCopyMemory(Entry->TfuRegs, Job.TfuJob.Regs,
                              sizeof(Entry->TfuRegs));
            }
            else if (Job.Op == RPI5VC4_DMA_OP_CSD_JOB)
            {
                Entry->IsCsdJob = TRUE;
                RtlCopyMemory(Entry->CsdCfg, Job.CsdJob.Cfg,
                              sizeof(Entry->CsdCfg));
            }
            else
            {
                Entry->IsV3dJob = TRUE;
                Entry->BclStart = Job.V3dJob.BclStart;
                Entry->BclEnd = Job.V3dJob.BclEnd;
                Entry->RclStart = Job.V3dJob.RclStart;
                Entry->RclEnd = Job.V3dJob.RclEnd;
                Entry->Qma = Job.V3dJob.Qma;
                Entry->Qms = Job.V3dJob.Qms;
                Entry->Qts = Job.V3dJob.Qts;
            }
        }
        DeviceExtension->NodeQueue[QueueIndex].Count++;
    }

    Completed = Rpi5Vc4ProcessPendingLocked(DeviceExtension, &NeedPoll, &PipelineAborted);

    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);

    if (Completed)
        KeInsertQueueDpc(&DeviceExtension->FenceDpc, NULL, NULL);
    if (NeedPoll)
        Rpi5Vc4ArmV3dPollTimer(DeviceExtension);

    if (PipelineAborted)
        return STATUS_DEVICE_HARDWARE_ERROR;

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiPreemptCommand(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_PREEMPTCOMMAND *PreemptCommand)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    if (DeviceExtension == NULL || PreemptCommand == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiQueryCurrentFence(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_QUERYCURRENTFENCE CurrentFence)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    KIRQL OldIrql;

    if (DeviceExtension == NULL || CurrentFence == NULL)
        return STATUS_INVALID_PARAMETER;

    if (CurrentFence->NodeOrdinal >= RPI5VC4_GPU_NODE_COUNT || CurrentFence->EngineOrdinal != 0)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    CurrentFence->CurrentFence = DeviceExtension->LastCompletedFencePerNode[CurrentFence->NodeOrdinal];
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);

    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiGetNodeMetadata(
    _In_ PVOID MiniportDeviceContext,
    _In_ UINT NodeOrdinalAndAdapterIndex,
    _Out_ DXGKARG_GETNODEMETADATA *GetNodeMetadata)
{
    UINT NodeOrdinal;

    if (MiniportDeviceContext == NULL || GetNodeMetadata == NULL)
        return STATUS_INVALID_PARAMETER;

    if ((NodeOrdinalAndAdapterIndex & 0xFFFF0000u) != 0)
        return STATUS_INVALID_PARAMETER;

    NodeOrdinal = NodeOrdinalAndAdapterIndex & 0xFFFFu;
    if (NodeOrdinal >= RPI5VC4_GPU_NODE_COUNT)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(GetNodeMetadata, sizeof(*GetNodeMetadata));
    if (NodeOrdinal == RPI5VC4_NODE_3D)
    {
        GetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_3D;
        RtlCopyMemory(GetNodeMetadata->FriendlyName, L"V3D 7.1 3D", sizeof(L"V3D 7.1 3D"));
    }
    else if (NodeOrdinal == RPI5VC4_NODE_TFU)
    {
        GetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_COPY;
        RtlCopyMemory(GetNodeMetadata->FriendlyName, L"V3D 7.1 TFU copy", sizeof(L"V3D 7.1 TFU copy"));
    }
    else
    {
        GetNodeMetadata->EngineType = DXGK_ENGINE_TYPE_OTHER;
        RtlCopyMemory(GetNodeMetadata->FriendlyName, L"V3D 7.1 CSD compute", sizeof(L"V3D 7.1 CSD compute"));
    }

    return STATUS_SUCCESS;
}

/* ========================================================================
 * Paging operations — real copies within/into the VRAM slab
 * ====================================================================== */

static PVOID
Rpi5Vc4SegmentAddressToVa(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ LARGE_INTEGER SegmentAddress,
    _In_ SIZE_T Bytes)
{
    ULONGLONG Offset = (ULONGLONG)SegmentAddress.QuadPart;

    /* Segment addresses may be absolute (CpuTranslated) or segment-relative. */
    if (Offset >= (ULONGLONG)DeviceExtension->VramPhysical.QuadPart)
        Offset -= (ULONGLONG)DeviceExtension->VramPhysical.QuadPart;

    if (Offset > DeviceExtension->VramSize ||
        Bytes > DeviceExtension->VramSize - Offset)
    {
        return NULL;
    }

    return (PUCHAR)DeviceExtension->VramVa + Offset;
}

static PRPI5VC4_PROCESS
Rpi5Vc4PagingProcess(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ HANDLE ProcessHandle)
{
    PRPI5VC4_PROCESS Process = (PRPI5VC4_PROCESS)ProcessHandle;

    if (Process == NULL ||
        Process->Magic != RPI5VC4_PROCESS_MAGIC ||
        Process->Adapter != DeviceExtension ||
        Process->V3dPageTable == NULL)
    {
        return NULL;
    }
    return Process;
}

static NTSTATUS
Rpi5Vc4EncodeNativePte(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST DXGK_PTE *Pte,
    _Out_ PULONG NativePte)
{
    ULONGLONG Physical;
    ULONGLONG PageNumber;

    *NativePte = 0;
    if (Pte->Valid && Pte->Zero)
        return STATUS_INVALID_PARAMETER;
    if (Pte->LargePage ||
        Pte->PageTablePageSize != DXGK_PTE_PAGE_TABLE_PAGE_4KB ||
        Pte->PhysicalAdapterIndex != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Pte->Valid)
    {
        Physical = Pte->PageAddress;
    }
    else if (Pte->Zero)
    {
        if (DeviceExtension->V3dScratchPage == NULL)
            return STATUS_DEVICE_NOT_READY;
        Physical = (ULONGLONG)
            DeviceExtension->V3dScratchPagePhys.QuadPart;
    }
    else
    {
        return STATUS_SUCCESS;
    }

    if ((Physical & (PAGE_SIZE - 1)) != 0)
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    PageNumber = Physical >> V3D_MMU_PAGE_SHIFT;
    if (PageNumber > V3D_PTE_PAGE_NUMBER_MASK)
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;

    *NativePte = (ULONG)PageNumber | V3D_PTE_VALID;
    if (Pte->Valid && !Pte->ReadOnly)
        *NativePte |= V3D_PTE_WRITEABLE;
    return STATUS_SUCCESS;
}

static NTSTATUS
Rpi5Vc4UpdateNativePageTable(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST DXGK_BUILDPAGINGBUFFER_UPDATEPAGETABLE *Update)
{
    PRPI5VC4_PROCESS Process;
    ULONGLONG FirstPage;
    ULONGLONG EndPage;
    ULONG Index;
    NTSTATUS Status;
    KIRQL OldIrql;

    Process = Rpi5Vc4PagingProcess(DeviceExtension, Update->hProcess);
    if (Process == NULL ||
        Update->UpdateMode != DXGK_PAGETABLEUPDATE_CPU_VIRTUAL ||
        Update->PageTableLevel >= RPI5VC4_GPUVA_LEVELS ||
        Update->PageTableAddress.CpuVirtual == NULL ||
        Update->pPageTableEntries == NULL ||
        Update->NumPageTableEntries == 0 ||
        Update->StartIndex >= (1u << RPI5VC4_GPUVA_INDEX_BITS) ||
        Update->NumPageTableEntries >
            (1u << RPI5VC4_GPUVA_INDEX_BITS) - Update->StartIndex ||
        Update->Flags.Repeat || Update->Flags.Use64KBPages ||
        (Update->FirstPteVirtualAddress & (PAGE_SIZE - 1)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* The portable upper level links dxgkrnl's radix tables.  V3D has a
     * single flat hardware table, so only leaf descriptors need encoding. */
    if (Update->PageTableLevel != 0)
        return STATUS_SUCCESS;

    FirstPage = Update->FirstPteVirtualAddress >> V3D_MMU_PAGE_SHIFT;
    EndPage = FirstPage + Update->NumPageTableEntries;
    if (EndPage < FirstPage ||
        EndPage > (RPI5VC4_V3D_PT_SIZE / sizeof(ULONG)))
    {
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;
    }

    /* Validate the complete span before changing the hardware table. */
    for (Index = 0; Index < Update->NumPageTableEntries; ++Index)
    {
        ULONG NativePte;

        Status = Rpi5Vc4EncodeNativePte(
            DeviceExtension,
            &Update->pPageTableEntries[Index],
            &NativePte);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    /* PageTableUpdateRequireAddressSpaceIdle requires the live native table
     * to remain unchanged until the address space is actually idle. */
    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    if (DeviceExtension->V3dActiveProcess == Process &&
        (InterlockedCompareExchange(
             &DeviceExtension->V3dExecutionBusy, 0, 0) != 0 ||
         DeviceExtension->V3dExecGateActive ||
         Rpi5Vc4GpuJobActiveLocked(DeviceExtension)))
    {
        KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }

    for (Index = 0; Index < Update->NumPageTableEntries; ++Index)
    {
        ULONG NativePte;

        Status = Rpi5Vc4EncodeNativePte(
            DeviceExtension,
            &Update->pPageTableEntries[Index],
            &NativePte);
        ASSERT(NT_SUCCESS(Status));
        Process->V3dPageTable[FirstPage + Index] = NativePte;
    }
    __dsb(_ARM64_BARRIER_SY);
    KeMemoryBarrier();
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiBuildPagingBuffer(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    if (DeviceExtension == NULL || BuildPagingBuffer == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (BuildPagingBuffer->Operation)
    {
        case DXGK_OPERATION_TRANSFER:
        {
            SIZE_T Bytes = BuildPagingBuffer->Transfer.TransferSize;
            PVOID SourceVa = NULL;
            PVOID DestinationVa = NULL;

            if (Bytes == 0)
                return STATUS_SUCCESS;

            if (BuildPagingBuffer->Transfer.Source.SegmentId ==
                RPI5VC4_SEGMENT_ID)
            {
                SourceVa = Rpi5Vc4SegmentAddressToVa(
                    DeviceExtension,
                    BuildPagingBuffer->Transfer.Source.SegmentAddress,
                    Bytes);
            }
            else if (BuildPagingBuffer->Transfer.Source.pMdl != NULL)
            {
                SourceVa = MmGetSystemAddressForMdlSafe(
                    BuildPagingBuffer->Transfer.Source.pMdl,
                    NormalPagePriority);
            }

            if (BuildPagingBuffer->Transfer.Destination.SegmentId ==
                RPI5VC4_SEGMENT_ID)
            {
                DestinationVa = Rpi5Vc4SegmentAddressToVa(
                    DeviceExtension,
                    BuildPagingBuffer->Transfer.Destination.SegmentAddress,
                    Bytes);
            }
            else if (BuildPagingBuffer->Transfer.Destination.pMdl != NULL)
            {
                DestinationVa = MmGetSystemAddressForMdlSafe(
                    BuildPagingBuffer->Transfer.Destination.pMdl,
                    NormalPagePriority);
            }

            if (SourceVa == NULL || DestinationVa == NULL)
                return STATUS_INVALID_PARAMETER;

            RtlCopyMemory(DestinationVa, SourceVa, Bytes);
            return STATUS_SUCCESS;
        }

        case DXGK_OPERATION_FILL:
        {
            SIZE_T Bytes = BuildPagingBuffer->Fill.FillSize;
            PVOID DestinationVa;
            SIZE_T i;

            if (Bytes == 0)
                return STATUS_SUCCESS;

            DestinationVa = Rpi5Vc4SegmentAddressToVa(DeviceExtension, BuildPagingBuffer->Fill.Destination.SegmentAddress, Bytes);
            if (DestinationVa == NULL)
                return STATUS_INVALID_PARAMETER;

            for (i = 0; i + sizeof(ULONG) <= Bytes; i += sizeof(ULONG))
            {
                *(PULONG)((PUCHAR)DestinationVa + i) =
                    BuildPagingBuffer->Fill.FillPattern;
            }
            return STATUS_SUCCESS;
        }

        case DXGK_OPERATION_DISCARD_CONTENT:
        case DXGK_OPERATION_MAP_APERTURE_SEGMENT:
        case DXGK_OPERATION_UNMAP_APERTURE_SEGMENT:
            return STATUS_SUCCESS;

        case DXGK_OPERATION_UPDATE_PAGE_TABLE:
            return Rpi5Vc4UpdateNativePageTable(
                DeviceExtension,
                &BuildPagingBuffer->UpdatePageTable);

        case DXGK_OPERATION_FLUSH_TLB:
        {
            PRPI5VC4_PROCESS Process = Rpi5Vc4PagingProcess(
                DeviceExtension,
                BuildPagingBuffer->FlushTlb.hProcess);
            KIRQL OldIrql;
            BOOLEAN Flushed = TRUE;

            if (Process == NULL ||
                BuildPagingBuffer->FlushTlb.StartVirtualAddress >=
                    BuildPagingBuffer->FlushTlb.EndVirtualAddress ||
                BuildPagingBuffer->FlushTlb.EndVirtualAddress >
                    (1ULL << RPI5VC4_GPUVA_BITS))
            {
                return STATUS_INVALID_PARAMETER;
            }

            KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
            if (DeviceExtension->V3dActiveProcess == Process)
            {
                if (Rpi5Vc4GpuJobActiveLocked(DeviceExtension))
                {
                    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
                    return STATUS_DEVICE_BUSY;
                }
                Flushed = Rpi5V3dMmucFlushBounded(DeviceExtension);
            }
            KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
            return Flushed ? STATUS_SUCCESS :
                             STATUS_DEVICE_HARDWARE_ERROR;
        }

        case DXGK_OPERATION_NOTIFY_RESIDENCY:
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

/* ========================================================================
 * TDR / interrupt control / escape
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiResetFromTimeout(
    _In_ PVOID MiniportDeviceContext)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    KIRQL OldIrql;
    BOOLEAN ResetSucceeded;

    if (DeviceExtension == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!DeviceExtension->DmaPipelineInitialized)
        return STATUS_DEVICE_NOT_READY;

    /* A timeout reset aborts outstanding commands; it must never advance a
     * completion fence or synthesize success for work the GPU did not finish. */
    DeviceExtension->StopAccepting = TRUE;
    Rpi5V3dDisconnectInterrupt(DeviceExtension);
    KeCancelTimer(&DeviceExtension->V3dPollTimer);
    KeRemoveQueueDpc(&DeviceExtension->V3dPollDpc);
    KeRemoveQueueDpc(&DeviceExtension->FenceDpc);
    KeFlushQueuedDpcs();
    ResetSucceeded = !DeviceExtension->V3dReady || Rpi5V3dResetCore(DeviceExtension);

    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    RtlZeroMemory(DeviceExtension->NodeQueue, sizeof(DeviceExtension->NodeQueue));
    RtlZeroMemory(DeviceExtension->DmaMappings, sizeof(DeviceExtension->DmaMappings));
    DeviceExtension->DmaMappingNext = 0;
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
    return ResetSucceeded ? STATUS_SUCCESS : STATUS_DEVICE_HARDWARE_ERROR;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiRestartFromTimeout(
    _In_ PVOID MiniportDeviceContext)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    if (DeviceExtension == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!DeviceExtension->DmaPipelineInitialized)
        return STATUS_DEVICE_NOT_READY;
    if (DeviceExtension->V3dReady)
        Rpi5V3dConnectInterrupt(DeviceExtension);
    DeviceExtension->StopAccepting = FALSE;
    if (DeviceExtension->Headless && DeviceExtension->HpdWorkItem != NULL)
        Rpi5Vc4ArmHpdTimer(DeviceExtension);
    return STATUS_SUCCESS;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiControlInterrupt(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGK_INTERRUPT_TYPE InterruptType,
    _In_ BOOLEAN EnableInterrupt)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    if (DeviceExtension == NULL)
        return STATUS_INVALID_PARAMETER;

    switch (InterruptType)
    {
        case DXGK_INTERRUPT_TYPE_CRTC_VSYNC:
            Rpi5Vc4VsyncControl(DeviceExtension, EnableInterrupt);
            return STATUS_SUCCESS;

        case DXGK_INTERRUPT_TYPE_DMA_COMPLETED:
            /* Completion is synthesized by the fence pipeline. */
            return STATUS_SUCCESS;

        case DXGK_INTERRUPT_TYPE_DMA_PREEMPTED:
            return STATUS_NOT_SUPPORTED;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

BOOLEAN
APIENTRY
Rpi5Vc4DdiInterruptRoutine(
    _In_ PVOID MiniportDeviceContext,
    _In_ ULONG MessageNumber)
{
    UNREFERENCED_PARAMETER(MessageNumber);
    return Rpi5V3dInterrupt(MiniportDeviceContext);
}

VOID
APIENTRY
Rpi5Vc4DdiDpcRoutine(
    _In_ PVOID MiniportDeviceContext)
{
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
}

/* ======================================================================
 * Submit escape (vc4kmt transport): PacketType==2 framing carrying a
 * resource list, a signal block, and one RPI5VC4_DMA_PACKET, matching
 * sdk/lib/vc4kmt/vc4kmt.c's Vc4KmtBuildSubmitSignalView cursor layout.
 * ====================================================================== */

#define RPI5VC4_SUBMIT_PACKET_TYPE   2u
#define RPI5VC4_SUBMIT_COMMAND_TYPE  2u
#define RPI5VC4_RESOURCE_LIST_MAGIC_V1 0x5652474cUL
#define RPI5VC4_RESOURCE_LIST_MAGIC_V2 0x3252474cUL

typedef struct _RPI5VC4_ESC_HDR
{
    USHORT PacketType;
    USHORT PayloadBytes;
} RPI5VC4_ESC_HDR;

typedef struct _RPI5VC4_ESC_RESLIST
{
    ULONG Magic;
    ULONG ResourceCount;
} RPI5VC4_ESC_RESLIST;

typedef struct _RPI5VC4_ESC_CMD
{
    ULONG CommandType;
    ULONG PayloadBytes;
} RPI5VC4_ESC_CMD;

#include <pshpack1.h>
typedef struct _RPI5VC4_ESC_SIGNAL
{
    ULONG hSyncObject;      /* D3DKMT_HANDLE — ignored (synchronous) */
    ULONG64 FenceValue;     /* ignored (synchronous) */
} RPI5VC4_ESC_SIGNAL;
#include <poppack.h>

static NTSTATUS
Rpi5Vc4QueueEscapeJob(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST RPI5VC4_DMA_PACKET *Packet)
{
    PRPI5VC4_PENDING_SUBMIT Entry;
    KIRQL OldIrql;
    ULONG QueueIndex;
    BOOLEAN Completed;
    BOOLEAN NeedPoll;
    BOOLEAN PipelineAborted;

    if (!DeviceExtension->V3dReady)
        return STATUS_DEVICE_NOT_READY;

    if (Packet->Op == RPI5VC4_DMA_OP_TFU_JOB && (Packet->TfuJob.Regs[1] < RPI5VC4_V3D_SLAB_GPUVA || Packet->TfuJob.Regs[6] < RPI5VC4_V3D_SLAB_GPUVA))
        return STATUS_GRAPHICS_INVALID_ALLOCATION_USAGE;

    if (Packet->Op == RPI5VC4_DMA_OP_TFU_JOB)
        QueueIndex = RPI5VC4_NODE_TFU;
    else if (Packet->Op == RPI5VC4_DMA_OP_CSD_JOB)
        QueueIndex = RPI5VC4_NODE_CSD;
    else
        QueueIndex = RPI5VC4_NODE_3D;

    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);

    if (DeviceExtension->NodeQueue[QueueIndex].Count >= RPI5VC4_MAX_PENDING ||
        DeviceExtension->StopAccepting)
    {
        KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }

    Entry = &DeviceExtension->NodeQueue[QueueIndex].Pending[
        (DeviceExtension->NodeQueue[QueueIndex].Head +
         DeviceExtension->NodeQueue[QueueIndex].Count) %
        RPI5VC4_MAX_PENDING];
    RtlZeroMemory(Entry, sizeof(*Entry));
    Entry->Fence = 0;
    Entry->NodeOrdinal = QueueIndex;
    if (Packet->Op == RPI5VC4_DMA_OP_TFU_JOB)
    {
        Entry->IsTfuJob = TRUE;
        RtlCopyMemory(Entry->TfuRegs, Packet->TfuJob.Regs,
                      sizeof(Entry->TfuRegs));
    }
    else if (Packet->Op == RPI5VC4_DMA_OP_CSD_JOB)
    {
        Entry->IsCsdJob = TRUE;
        RtlCopyMemory(Entry->CsdCfg, Packet->CsdJob.Cfg,
                      sizeof(Entry->CsdCfg));
    }
    else
    {
        Entry->IsV3dJob = TRUE;
        Entry->BclStart = Packet->V3dJob.BclStart;
        Entry->BclEnd = Packet->V3dJob.BclEnd;
        Entry->RclStart = Packet->V3dJob.RclStart;
        Entry->RclEnd = Packet->V3dJob.RclEnd;
        Entry->Qma = Packet->V3dJob.Qma;
        Entry->Qms = Packet->V3dJob.Qms;
        Entry->Qts = Packet->V3dJob.Qts;
    }
    DeviceExtension->NodeQueue[QueueIndex].Count++;

    Completed = Rpi5Vc4ProcessPendingLocked(DeviceExtension, &NeedPoll, &PipelineAborted);

    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);

    if (Completed)
        KeInsertQueueDpc(&DeviceExtension->FenceDpc, NULL, NULL);
    if (NeedPoll)
        Rpi5Vc4ArmV3dPollTimer(DeviceExtension);

    if (PipelineAborted)
        return STATUS_DEVICE_HARDWARE_ERROR;

    return STATUS_SUCCESS;
}

static NTSTATUS
Rpi5Vc4EscapeSubmit(
    _In_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST DXGKARG_ESCAPE *Escape)
{
    PUCHAR Base = (PUCHAR)Escape->pPrivateDriverData;
    ULONG Size = Escape->PrivateDriverDataSize;
    ULONG Cursor;
    const RPI5VC4_ESC_HDR *Header = (const RPI5VC4_ESC_HDR *)Base;
    const RPI5VC4_ESC_RESLIST *Resources;
    const RPI5VC4_ESC_CMD *Command;
    const RPI5VC4_DMA_PACKET *Packet;
    ULONG ResourceCount;
    ULONG ResourceEntrySize;

    /*
     * Walk the cursor layout: HDR, RESLIST, hResource[], CMD, [SIGNAL], PACKET.
     * The signal block is present only for PacketType 2 (submit-and-signal);
     * PacketType 1 (fire-and-forget) omits it.  The job is queued to the async
     * fence pipeline; a signal block cannot be honoured on this path (dxgkrnl
     * intercepts well-formed submit escapes before they reach the miniport),
     * so it is skipped only to land on the DMA packet at the right offset.
     */
    Cursor = sizeof(RPI5VC4_ESC_HDR);
    if (Cursor + sizeof(RPI5VC4_ESC_RESLIST) > Size)
        return STATUS_INVALID_PARAMETER;

    Resources = (const RPI5VC4_ESC_RESLIST *)(Base + Cursor);
    if (Resources->Magic != RPI5VC4_RESOURCE_LIST_MAGIC_V1 &&
        Resources->Magic != RPI5VC4_RESOURCE_LIST_MAGIC_V2)
        return STATUS_INVALID_PARAMETER;
    ResourceCount = Resources->ResourceCount;
    ResourceEntrySize = Resources->Magic == RPI5VC4_RESOURCE_LIST_MAGIC_V2
                            ? 2 * sizeof(ULONG)
                            : sizeof(ULONG);
    if (ResourceCount > 4096)
        return STATUS_INVALID_PARAMETER;
    Cursor += sizeof(RPI5VC4_ESC_RESLIST);

    /* Skip the residency handle list — the slab is always resident. */
    if (ResourceCount > ((ULONG)-1) / ResourceEntrySize ||
        Cursor + ResourceCount * ResourceEntrySize < Cursor) /* overflow */
        return STATUS_INVALID_PARAMETER;
    Cursor += ResourceCount * ResourceEntrySize;

    if (Cursor + sizeof(RPI5VC4_ESC_CMD) > Size)
        return STATUS_INVALID_PARAMETER;
    Command = (const RPI5VC4_ESC_CMD *)(Base + Cursor);
    (VOID)Command; /* CommandType 1 (smoke) or 2 (vc4kmt): both are submits */
    Cursor += sizeof(RPI5VC4_ESC_CMD);

    /* Signal block only when PacketType requests a fence signal. */
    if (Header->PacketType == RPI5VC4_SUBMIT_PACKET_TYPE)
        Cursor += sizeof(RPI5VC4_ESC_SIGNAL);

    if (Cursor + sizeof(RPI5VC4_DMA_PACKET) > Size)
        return STATUS_INVALID_PARAMETER;

    Packet = (const RPI5VC4_DMA_PACKET *)(Base + Cursor);
    DPRINT1("RPI5VC4: esc submit packet op=%d magic=%08lx\n",
            (int)Packet->Op, (ULONG)Packet->Magic);
    if (Packet->Magic != RPI5VC4_DMA_PACKET_MAGIC)
        return STATUS_INVALID_PARAMETER;

    /* Present packets are descriptive only (dxgkrnl blit moved the pixels). */
    if (Packet->Op == RPI5VC4_DMA_OP_NOP ||
        Packet->Op == RPI5VC4_DMA_OP_PRESENT)
    {
        return STATUS_SUCCESS;
    }

    return Rpi5Vc4QueueEscapeJob(DeviceExtension, Packet);
}

/* ========================================================================
 * Bounded exec-engine escapes (RPI5VC4_WDDM_GPU_ESCAPE wrapper)
 *
 * The OpenGL ICD packs the XPDM IOCTL_VIDEO_RPI5VC4_* request bodies behind
 * a RPI5VC4_WDDM_GPU_ESCAPE header; the matching engine entry point runs
 * synchronously at PASSIVE_LEVEL and rewrites the payload in place (the
 * XPDM VRP shared a single system buffer the same way).  GPU-kicking ops
 * first park the async fence pipeline: the gate is taken only with all
 * three node queues empty, so no CLE/TFU/CSD job is in flight, and
 * ProcessPendingLocked defers new kicks until release.
 * ====================================================================== */

static BOOLEAN
Rpi5Vc4GpuEscapeGateAcquire(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    LARGE_INTEGER Interval;
    KIRQL OldIrql;
    BOOLEAN Taken;
    BOOLEAN Stopping;
    ULONG Tries;

    if (!DeviceExtension->DmaPipelineInitialized)
        return FALSE;

    for (Tries = 0; Tries < 500; Tries++)
    {
        KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
        Stopping = DeviceExtension->StopAccepting;
        /* Exclusive: a concurrent escape already holding the gate must not
         * be released from under its running job by this thread. */
        Taken = !Stopping && !DeviceExtension->V3dExecGateActive && DeviceExtension->NodeQueue[0].Count == 0 && DeviceExtension->NodeQueue[1].Count == 0 && DeviceExtension->NodeQueue[2].Count == 0;
        if (Taken)
            DeviceExtension->V3dExecGateActive = TRUE;
        KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
        if (Stopping)
            return FALSE;
        if (Taken)
            return TRUE;
        Interval.QuadPart = -10000; /* 1 ms */
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }

    return FALSE;
}

static VOID
Rpi5Vc4GpuEscapeGateRelease(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    BOOLEAN Pending;
    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    if (DeviceExtension->V3dReady)
    {
        PUCHAR Core = DeviceExtension->V3dCoreBase;
        PUCHAR Hub = DeviceExtension->V3dHubBase;

        /* The exec engine masked every interrupt source, consumed INT_STS
         * latches and bumped the BFC/RFC flush counters; resync the poll
         * shadows and restore the WDDM unmask state so pipeline jobs are
         * neither falsely completed nor left without their interrupts. */
        WRITE_REGISTER_ULONG((PULONG)(Core + V3D_CTL_INT_CLR), 0xFFFFFFFFu);
        WRITE_REGISTER_ULONG((PULONG)(Hub + V3D_HUB_INT_CLR), 0xFFFFFFFFu);
        DeviceExtension->V3dLastBfc = READ_REGISTER_ULONG((PULONG)(Core + V3D_CLE_BFC)) & 0xff;
        DeviceExtension->V3dLastRfc = READ_REGISTER_ULONG((PULONG)(Core + V3D_CLE_RFC)) & 0xff;
        if (DeviceExtension->V3dCoreIrqConnected)
        {
            WRITE_REGISTER_ULONG((PULONG)(Core + V3D_CTL_INT_MSK_SET), 0xFFFFFFFFu);
            WRITE_REGISTER_ULONG((PULONG)(Core + V3D_CTL_INT_MSK_CLR), V3D_INT_FLDONE | V3D_INT_FRDONE | V3D_INT_OUTOMEM | V3D_V7_INT_CSDDONE);
        }
        if (DeviceExtension->V3dHubIrqConnected)
        {
            WRITE_REGISTER_ULONG((PULONG)(Hub + V3D_HUB_INT_MSK_SET), 0xFFFFFFFFu);
            WRITE_REGISTER_ULONG((PULONG)(Hub + V3D_HUB_INT_MSK_CLR), V3D_HUB_INT_TFUC);
        }
    }
    DeviceExtension->V3dExecGateActive = FALSE;
    Pending = DeviceExtension->NodeQueue[0].Count != 0 ||
              DeviceExtension->NodeQueue[1].Count != 0 ||
              DeviceExtension->NodeQueue[2].Count != 0;
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);

    /* Catch up jobs queued while the gate was held. */
    if (Pending)
        KeInsertQueueDpc(&DeviceExtension->V3dPollDpc, NULL, NULL);
}

static NTSTATUS
Rpi5Vc4GpuEscape(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST DXGKARG_ESCAPE *Escape)
{
    PRPI5VC4_WDDM_GPU_ESCAPE Header = Escape->pPrivateDriverData;
    ULONG HeaderBytes = FIELD_OFFSET(RPI5VC4_WDDM_GPU_ESCAPE, Payload);
    ULONG PayloadCapacity;
    PUCHAR Payload;
    ULONG InputLength;
    ULONG OutputLength;
    ULONG Returned = 0;
    BOOLEAN NeedGate;
    VP_STATUS VpStatus;

    if (Escape->PrivateDriverDataSize < HeaderBytes)
        return STATUS_BUFFER_TOO_SMALL;

    PayloadCapacity = Escape->PrivateDriverDataSize - HeaderBytes;
    InputLength = Header->InputLength;
    OutputLength = Header->OutputLength;
    if (InputLength > PayloadCapacity || OutputLength > PayloadCapacity)
        return STATUS_INVALID_PARAMETER;

    Payload = Header->Payload;

    /* Per-op minimum sizes, copied from the XPDM StartIO dispatch. */
    switch (Header->Op)
    {
        case RPI5VC4_ESCAPE_QUERY_V3D:
            if (OutputLength < sizeof(RPI5VC4_V3D_INFO))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        case RPI5VC4_ESCAPE_QUERY_PLATFORM:
            if (OutputLength < sizeof(RPI5VC4_PLATFORM_INFO))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        case RPI5VC4_ESCAPE_RUN_V3D_SELFTEST:
            if (OutputLength < sizeof(RPI5VC4_V3D_SELFTEST))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        case RPI5VC4_ESCAPE_RENDER_CLEAR:
            if (InputLength < sizeof(RPI5VC4_V3D_CLEAR_REQUEST) || OutputLength < FIELD_OFFSET(RPI5VC4_V3D_CLEAR_RESULT, Pixels))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        case RPI5VC4_ESCAPE_RENDER_TRIANGLE:
            if (InputLength < sizeof(RPI5VC4_V3D_TRIANGLE_REQUEST) || OutputLength < FIELD_OFFSET(RPI5VC4_V3D_TRIANGLE_RESULT, Pixels))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        case RPI5VC4_ESCAPE_RENDER_BATCH:
            if (InputLength < FIELD_OFFSET(RPI5VC4_V3D_BATCH_REQUEST, Vertices) || OutputLength < FIELD_OFFSET(RPI5VC4_V3D_BATCH_RESULT, Pixels))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        case RPI5VC4_ESCAPE_UPLOAD_TEXTURE:
            if (InputLength < FIELD_OFFSET(RPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST, Pixels) || OutputLength < sizeof(RPI5VC4_V3D_TEXTURE_UPLOAD_RESULT))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        case RPI5VC4_ESCAPE_RENDER_GRAPH:
            if (InputLength < FIELD_OFFSET(RPI5VC4_V3D_RENDER_GRAPH_REQUEST, Pixels) || OutputLength < sizeof(RPI5VC4_V3D_RENDER_GRAPH_RESULT))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        case RPI5VC4_ESCAPE_READ_GRAPH:
            if (InputLength < sizeof(RPI5VC4_V3D_READ_GRAPH_REQUEST) || OutputLength < FIELD_OFFSET(RPI5VC4_V3D_READ_GRAPH_RESULT, Pixels))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        case RPI5VC4_ESCAPE_READ_TEXTURE:
            if (InputLength < sizeof(RPI5VC4_V3D_READ_TEXTURE_REQUEST) || OutputLength < FIELD_OFFSET(RPI5VC4_V3D_READ_TEXTURE_RESULT, Pixels))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        case RPI5VC4_ESCAPE_WAIT_VBLANK:
            if (OutputLength < sizeof(RPI5VC4_VBLANK_RESULT))
                return STATUS_BUFFER_TOO_SMALL;
            break;
        default:
            return STATUS_NOT_SUPPORTED;
    }

    NeedGate = Header->Op == RPI5VC4_ESCAPE_RUN_V3D_SELFTEST || Header->Op == RPI5VC4_ESCAPE_RENDER_CLEAR || Header->Op == RPI5VC4_ESCAPE_RENDER_TRIANGLE || Header->Op == RPI5VC4_ESCAPE_RENDER_BATCH || Header->Op == RPI5VC4_ESCAPE_RENDER_GRAPH || Header->Op == RPI5VC4_ESCAPE_READ_TEXTURE;
    if (NeedGate && !Rpi5Vc4GpuEscapeGateAcquire(DeviceExtension))
        return STATUS_DEVICE_BUSY;

    switch (Header->Op)
    {
        case RPI5VC4_ESCAPE_QUERY_V3D:
            VpStatus = Rpi5V3dQuery(DeviceExtension, (PRPI5VC4_V3D_INFO)Payload);
            Returned = sizeof(RPI5VC4_V3D_INFO);
            break;
        case RPI5VC4_ESCAPE_QUERY_PLATFORM:
            VpStatus = Rpi5Vc4QueryPlatformInfo((PRPI5VC4_PLATFORM_INFO)Payload);
            Returned = sizeof(RPI5VC4_PLATFORM_INFO);
            break;
        case RPI5VC4_ESCAPE_RUN_V3D_SELFTEST:
            VpStatus = Rpi5V3dRunSelfTest(DeviceExtension, (PRPI5VC4_V3D_SELFTEST)Payload);
            Returned = sizeof(RPI5VC4_V3D_SELFTEST);
            break;
        case RPI5VC4_ESCAPE_RENDER_CLEAR:
            VpStatus = Rpi5V3dRenderClear(DeviceExtension, (PRPI5VC4_V3D_CLEAR_REQUEST)Payload, (PRPI5VC4_V3D_CLEAR_RESULT)Payload, OutputLength, &Returned);
            break;
        case RPI5VC4_ESCAPE_RENDER_TRIANGLE:
            VpStatus = Rpi5V3dRenderTriangle(DeviceExtension, (PRPI5VC4_V3D_TRIANGLE_REQUEST)Payload, (PRPI5VC4_V3D_TRIANGLE_RESULT)Payload, OutputLength, &Returned);
            break;
        case RPI5VC4_ESCAPE_RENDER_BATCH:
            VpStatus = Rpi5V3dRenderBatch(DeviceExtension, (PRPI5VC4_V3D_BATCH_REQUEST)Payload, InputLength, (PRPI5VC4_V3D_BATCH_RESULT)Payload, OutputLength, &Returned);
            break;
        case RPI5VC4_ESCAPE_UPLOAD_TEXTURE:
            VpStatus = Rpi5V3dUploadTexture(DeviceExtension, (PRPI5VC4_V3D_TEXTURE_UPLOAD_REQUEST)Payload, InputLength, (PRPI5VC4_V3D_TEXTURE_UPLOAD_RESULT)Payload, &Returned);
            break;
        case RPI5VC4_ESCAPE_RENDER_GRAPH:
            VpStatus = Rpi5V3dRenderGraph(DeviceExtension, (PRPI5VC4_V3D_RENDER_GRAPH_REQUEST)Payload, InputLength, (PRPI5VC4_V3D_RENDER_GRAPH_RESULT)Payload, &Returned);
            break;
        case RPI5VC4_ESCAPE_READ_GRAPH:
            VpStatus = Rpi5V3dReadGraph(DeviceExtension, (PRPI5VC4_V3D_READ_GRAPH_REQUEST)Payload, (PRPI5VC4_V3D_READ_GRAPH_RESULT)Payload, OutputLength, &Returned);
            break;
        case RPI5VC4_ESCAPE_READ_TEXTURE:
            VpStatus = Rpi5V3dReadTexture(DeviceExtension, (PRPI5VC4_V3D_READ_TEXTURE_REQUEST)Payload, (PRPI5VC4_V3D_READ_TEXTURE_RESULT)Payload, OutputLength, &Returned);
            break;
        case RPI5VC4_ESCAPE_WAIT_VBLANK:
        {
            PRPI5VC4_VBLANK_RESULT Result =
                (PRPI5VC4_VBLANK_RESULT)Payload;

            RtlZeroMemory(Result, sizeof(*Result));
            Result->Size = sizeof(*Result);
            Result->AbiVersion = RPI5VC4_XPDM_ABI_VERSION;
            Result->Status = Rpi5CrtcWaitForVBlank(DeviceExtension) ?
                RPI5VC4_V3D_SELFTEST_STATUS_SUCCESS :
                RPI5VC4_V3D_SELFTEST_STATUS_NOT_SUPPORTED;
            Returned = sizeof(*Result);
            VpStatus = NO_ERROR;
            break;
        }
        default:
            VpStatus = ERROR_INVALID_FUNCTION;
            break;
    }

    if (NeedGate)
        Rpi5Vc4GpuEscapeGateRelease(DeviceExtension);

    if (VpStatus == NO_ERROR)
    {
        Header->OutputLength = Returned;
        return STATUS_SUCCESS;
    }
    if (VpStatus == ERROR_INSUFFICIENT_BUFFER)
        return STATUS_BUFFER_TOO_SMALL;
    if (VpStatus == ERROR_INVALID_PARAMETER)
        return STATUS_INVALID_PARAMETER;
    return STATUS_UNSUCCESSFUL;
}

NTSTATUS
APIENTRY
Rpi5Vc4DdiEscape(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST DXGKARG_ESCAPE *Escape)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    PRPI5VC4_ESCAPE_INFO Info;
    ULONG Caps;

    if (DeviceExtension == NULL || Escape == NULL ||
        Escape->pPrivateDriverData == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /*
     * Two escape framings share this entry point:
     *  - the QUERY_INFO escape, tagged by RPI5VC4_ESCAPE_MAGIC at offset 0;
     *  - a submit escape (resource list + signal + one RPI5VC4_DMA_PACKET),
     *    whose leading dword is NOT the magic.
     * A submit is queued to the in-order fence pipeline and executes
     * asynchronously (poll/DPC completion) — no busy-wait on this thread.
     */
    if (Escape->PrivateDriverDataSize < sizeof(ULONG))
        return STATUS_BUFFER_TOO_SMALL;

    /* Third framing: the exec-engine wrapper for the OpenGL ICD. */
    if (*(const ULONG *)Escape->pPrivateDriverData == RPI5VC4_WDDM_GPU_ESCAPE_MAGIC)
        return Rpi5Vc4GpuEscape(DeviceExtension, Escape);

    if (*(const ULONG *)Escape->pPrivateDriverData != RPI5VC4_ESCAPE_MAGIC)
        return Rpi5Vc4EscapeSubmit(DeviceExtension, Escape);

    if (Escape->PrivateDriverDataSize <
        FIELD_OFFSET(RPI5VC4_ESCAPE_INFO, AbiVersion))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Info = Escape->pPrivateDriverData;

    switch (Info->Op)
    {
        case RPI5VC4_ESCAPE_OP_QUERY_INFO:
            Info->V3dReady = DeviceExtension->V3dReady ? 1 : 0;
            Info->V3dVersion = DeviceExtension->V3dVersion;
            RtlCopyMemory(Info->V3dHubIdent, DeviceExtension->V3dHubIdent,
                          sizeof(Info->V3dHubIdent));
            RtlCopyMemory(Info->V3dCoreIdent, DeviceExtension->V3dCoreIdent,
                          sizeof(Info->V3dCoreIdent));
            Info->SlabGpuVa = RPI5VC4_V3D_SLAB_GPUVA;
            Info->SlabPhysical = (ULONGLONG)DeviceExtension->VramPhysical.QuadPart;
            Info->SlabSize = DeviceExtension->VramSize;
            Info->ScreenWidth = DeviceExtension->ScreenWidth;
            Info->ScreenHeight = DeviceExtension->ScreenHeight;
            Info->ScreenPitch = DeviceExtension->BytesPerScanLine;

            if (Escape->PrivateDriverDataSize <
                sizeof(RPI5VC4_ESCAPE_INFO))
            {
                return STATUS_SUCCESS;
            }

            Caps = RPI5VC4_CAP_GPUVA_MAP | RPI5VC4_CAP_ALLOCATION_RELOCATION | RPI5VC4_CAP_MONITORED_FENCE | RPI5VC4_CAP_SUBMIT_SIGNAL | RPI5VC4_CAP_CPU_WAIT_SIGNAL | RPI5VC4_CAP_WIN32_PRESENT | RPI5VC4_CAP_LINEAR_SCANOUT;

            if (DeviceExtension->V3dReady)
            {
                Caps |= RPI5VC4_CAP_CL_SUBMIT |
                        RPI5VC4_CAP_TFU_SUBMIT |
                        RPI5VC4_CAP_CSD_SUBMIT |
                        RPI5VC4_CAP_CACHE_FLUSH;
            }

            Info->AbiVersion = RPI5VC4_ESCAPE_INFO_ABI_VERSION;
            Info->Caps = Caps;
            Info->NodeCount = RPI5VC4_GPU_NODE_COUNT;
            Info->MaxPendingSubmits = RPI5VC4_MAX_PENDING;
            Info->AllocationAlignment = PAGE_SIZE;
            Info->TfuRegisterCount = 12;
            Info->CsdConfigCount = 8;
            Info->LinearFormatMask = RPI5VC4_LINEAR_FORMAT_X8R8G8B8 |
                                     RPI5VC4_LINEAR_FORMAT_A8R8G8B8;
            RtlZeroMemory(Info->Reserved, sizeof(Info->Reserved));
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}
