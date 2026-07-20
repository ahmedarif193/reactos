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

        if ((Head->IsTfuJob || Head->IsCsdJob) && DeviceExtension->V3dReady &&
            !DeviceExtension->StopAccepting)
        {
            /* Single-phase TFU conversion / CSD compute job. */
            /* Unpatched/non-slab addresses are invalid and never retire. */
            if (Head->IsTfuJob &&
                (Head->TfuRegs[1] < RPI5VC4_V3D_SLAB_GPUVA ||
                 Head->TfuRegs[6] < RPI5VC4_V3D_SLAB_GPUVA))
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
                            {
                                /* Round-16: any hardware engine retirement
                                 * arms the pre-bin pulse. */
                                DeviceExtension->V3dNeedsSmsPulse = TRUE;
                                goto CompleteHead;
                            }
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
            {
                DeviceExtension->V3dNeedsSmsPulse = TRUE;
                goto CompleteHead;
            }

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
            BOOLEAN HasBin = (Head->BclEnd > Head->BclStart);
            BOOLEAN BinPhaseOver;

            /* Phase 1: kick binning. */
            if (HasBin && !Head->BinSubmitted)
            {
                /* Pre-bin gate (ReactOS-specific workaround, codex round 4):
                 * our bin kick fires exactly at the previous render's RFC
                 * edge, inside its thread-tail window, and that timing
                 * deterministically wedges the PTB (BFC freezes, only a
                 * core reset recovers).  Defer the CT0 doorbell until CT1
                 * reads fully idle, bounded so an unproven CS decode can
                 * never strand the queue. */
                ULONG Ct1Cs = READ_REGISTER_ULONG((PULONG)
                    ((PUCHAR)DeviceExtension->V3dCoreBase + V3D_CLE_CT1CS));

                if (Ct1Cs != 0)
                {
                    if (Head->BinDeferStart100ns == 0)
                        Head->BinDeferStart100ns = Now;

                    if (Now - Head->BinDeferStart100ns <
                        (ULONGLONG)(50 * 10 * 1000))
                    {
                        *NeedPoll = TRUE;
                        goto NextNode;
                    }
                    /* gate expired: kick once anyway */
                }

                /* BCM2712 silicon-quirk workaround (codex rounds 9-11): the
                 * V3D core wedges the NEXT bin's final PTB flush after any
                 * completed render; no register-level recovery releases it —
                 * only an SMS-class reset.  Pulse proactively: bounded SMS
                 * reset + invariants + MMU reprogram (no PTE rebuild), gated
                 * on every V3D engine idle.  Pi5-specific driver code. */
                if (DeviceExtension->V3dNeedsSmsPulse)
                {
                    BOOLEAN TfuBusy = FALSE;
                    ULONG n;

                    for (n = RPI5VC4_NODE_TFU; n <= RPI5VC4_NODE_CSD; n++)
                    {
                        if (DeviceExtension->NodeQueue[n].Count != 0 &&
                            DeviceExtension->NodeQueue[n].Pending[
                                DeviceExtension->NodeQueue[n].Head].RenderSubmitted)
                        {
                            TfuBusy = TRUE;
                        }
                    }
                    if (!TfuBusy && DeviceExtension->V3dHubBase != NULL &&
                        (READ_REGISTER_ULONG((PULONG)
                            ((PUCHAR)DeviceExtension->V3dHubBase + V3D_V7_TFU_CS))
                         & 0x1))
                    {
                        TfuBusy = TRUE;
                    }

                    if (TfuBusy)
                    {
                        *NeedPoll = TRUE;
                        goto NextNode;
                    }

                    {
                        PUCHAR CoreP = (PUCHAR)DeviceExtension->V3dCoreBase;
                        BOOLEAN SmsOk, MmuOk;
                        ULONG TeeUs, ReeUs;
                        ULONG Ct1CsPre;

                        /* Round-14 hardening: never SMS-pulse a core whose
                         * CT1 thread is still finishing the retired render's
                         * epilogue (RFC edges before thread idle).  Defer a
                         * tick instead. */
                        Ct1CsPre = READ_REGISTER_ULONG((PULONG)(CoreP + V3D_CLE_CT1CS));
                        if (Ct1CsPre != 0)
                        {
                            *NeedPoll = TRUE;
                            goto NextNode;
                        }

                        SmsOk = Rpi5V3dSmsPowerUpBounded(DeviceExtension,
                                                         &TeeUs, &ReeUs);
                        WRITE_REGISTER_ULONG((PULONG)(CoreP + V3D_CTL_L2TFLSTA), 0);
                        WRITE_REGISTER_ULONG((PULONG)(CoreP + V3D_CTL_L2TFLEND), ~0u);
                        WRITE_REGISTER_ULONG((PULONG)(CoreP + V3D_CTL_INT_MSK_SET), ~0u);
                        WRITE_REGISTER_ULONG((PULONG)(CoreP + V3D_CTL_INT_CLR), ~0u);
                        if (DeviceExtension->V3dHubBase != NULL)
                        {
                            WRITE_REGISTER_ULONG((PULONG)
                                ((PUCHAR)DeviceExtension->V3dHubBase + V3D_HUB_INT_MSK_SET), ~0u);
                            WRITE_REGISTER_ULONG((PULONG)
                                ((PUCHAR)DeviceExtension->V3dHubBase + V3D_HUB_INT_CLR), ~0u);
                        }
                        if (DeviceExtension->V3dIrqConnected)
                        {
                            WRITE_REGISTER_ULONG((PULONG)(CoreP + V3D_CTL_INT_MSK_CLR),
                                V3D_INT_FLDONE | V3D_INT_FRDONE | V3D_INT_OUTOMEM |
                                V3D_V7_INT_CSDDONE);
                            if (DeviceExtension->V3dHubBase != NULL)
                            {
                                WRITE_REGISTER_ULONG((PULONG)
                                    ((PUCHAR)DeviceExtension->V3dHubBase + V3D_HUB_INT_MSK_CLR),
                                    V3D_HUB_INT_TFUC);
                            }
                        }
                        MmuOk = Rpi5V3dMmuProgramBounded(DeviceExtension);

                        if (!SmsOk || !MmuOk)
                        {
                            /* Failed pulse stage: do NOT run the bin on bad
                             * state — retry next tick; the 50ms backstop
                             * escalates if it never succeeds. */
                            *NeedPoll = TRUE;
                            goto NextNode;
                        }

                        DeviceExtension->V3dNeedsSmsPulse = FALSE;
                        DeviceExtension->V3dLastBfc = 0;
                        DeviceExtension->V3dLastRfc = 0;
                    }
                }

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

            /* Per-job bin completion: the BFC edge past this job's own
             * pre-kick baseline, with the CA==EA fallback for BCLs that
             * end without a BFC bump.  Replaces the global FLDONE/delta
             * attribution, which loses completions when its single global
             * consumption misses (the sub=0 park: BinDone never set, the
             * render never kicked, hardware idle and healthy). */
            if (HasBin && Head->BinSubmitted && !Head->BinDone)
            {
                PUCHAR CoreB = (PUCHAR)DeviceExtension->V3dCoreBase;
                ULONG Bfc = READ_REGISTER_ULONG((PULONG)
                                (CoreB + V3D_CLE_BFC)) & 0xff;

                if ((UCHAR)Bfc != Head->BinKickBfc)
                {
                    Head->BinDone = TRUE;
                }
                else
                {
                    ULONG Ct0Ea = READ_REGISTER_ULONG((PULONG)(CoreB + V3D_CLE_CT0EA));
                    ULONG Ct0Ca = READ_REGISTER_ULONG((PULONG)(CoreB + V3D_CLE_CT0CA));
                    ULONG Pcs = READ_REGISTER_ULONG((PULONG)(CoreB + V3D_CLE_PCS));

                    if (Ct0Ea == Head->BclEnd &&
                        (Ct0Ca & ~1ul) >= Ct0Ea &&
                        !(Pcs & 1))
                    {
                        Head->BinDone = TRUE;
                    }
                }

                if (Head->BinDone)
                {
                    /* One-tick handoff slack: kicking CT1 in the same pass
                     * that observed the bin edge wedges the FIRST job's
                     * PTB flush (variant testing, rounds 13-14: configs
                     * without this slack wedge fence-2 every boot). */
                    Head->QueuedTime100ns = Now;
                    *NeedPoll = TRUE;
                    goto NextNode;
                }
            }

            /* Internal warm-up job: bin-only by construction; retire the
             * moment the bin completes (never enters the render phase). */
            if (Head->IsWarmup && Head->BinDone)
                goto CompleteHead;

            BinPhaseOver = !HasBin || Head->BinDone;

            /* PCS&1 pre-render gate: bit semantics undocumented, but variant
             * testing (round 13) shows the FIRST job's handoff wedges without
             * it — it stays as the cold-start protector. */
            if (BinPhaseOver && HasBin && !Head->RenderSubmitted &&
                (READ_REGISTER_ULONG((PULONG)
                     ((PUCHAR)DeviceExtension->V3dCoreBase + V3D_CLE_PCS)) & 1))
            {
                *NeedPoll = TRUE;
                goto NextNode;
            }

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
            {
                DeviceExtension->V3dNeedsSmsPulse = TRUE;
                goto CompleteHead;
            }

            /* Known wedge signature: BCL fully consumed, BFC frozen in the
             * final PTB flush.  Only a core reset recovers; take it at 50 ms
             * instead of 300 ms (the stall the user sees). */
            if (Head->BinSubmitted && !Head->BinDone && !Head->RenderSubmitted &&
                Now - Head->QueuedTime100ns >
                    (ULONGLONG)RPI5VC4_V3D_BIN_FLUSH_TIMEOUT_100NS)
            {
                PUCHAR CoreT = (PUCHAR)DeviceExtension->V3dCoreBase;
                ULONG Ct0EaT = READ_REGISTER_ULONG((PULONG)(CoreT + V3D_CLE_CT0EA));
                ULONG Ct0CaT = READ_REGISTER_ULONG((PULONG)(CoreT + V3D_CLE_CT0CA));

                if (Ct0EaT == Head->BclEnd && (Ct0CaT & ~1ul) >= Ct0EaT)
                {
                    /* BCM2712 V3D quirk: the bin's final PTB flush can wedge
                     * after a completed render (BFC frozen, all engines and
                     * fault registers clean).  Only an SMS-class reset
                     * recovers; the proactive pulse above prevents most
                     * occurrences.  See v3d-render-park notes. */
                    DPRINT1("RPI5VC4: bin final-flush wedge fence=%lu — reset: %s\n", Head->Fence, Rpi5V3dResetCore(DeviceExtension) ? "ok" : "FAILED");
                    goto AbortPipeline;
                }
            }

            /* Render-park recovery (WDDM TDR parity): the RCL was kicked but
             * CT1 never began executing it (CT1CFG stays 0) and RFC has not
             * advanced past the kick snapshot.  Windows recovers a stuck engine
             * by RESET (DxgkDdiResetFromTimeout), never by re-submitting the
             * packet — a re-kick can double-execute a render that quietly did
             * start.  Reset + drop at the bin-flush bound instead of the 300 ms
             * full timeout; the app re-renders the dropped frame. */
            if (Head->RenderSubmitted && !Head->IsTfuJob && !Head->IsCsdJob &&
                Now - Head->QueuedTime100ns >
                    (ULONGLONG)RPI5VC4_V3D_BIN_FLUSH_TIMEOUT_100NS &&
                READ_REGISTER_ULONG((PULONG)
                    ((PUCHAR)DeviceExtension->V3dCoreBase + V3D_CLE_CT1CFG)) == 0)
            {
                DPRINT1("RPI5VC4: render-park fence=%lu (CT1 idle) — reset: %s\n", Head->Fence, Rpi5V3dResetCore(DeviceExtension) ? "ok" : "FAILED");
                goto AbortPipeline;
            }

            /* Still in flight: enforce the per-phase timeout. */
            if (Now - Head->QueuedTime100ns < RPI5VC4_V3D_JOB_TIMEOUT_100NS)
            {
                *NeedPoll = TRUE;
                goto NextNode;
            }

            {
                PVOID Core = DeviceExtension->V3dCoreBase;
                PVOID Hub = DeviceExtension->V3dHubBase;

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
        if (Head->IsWarmup)
        {
            Queue(Node)->Head = (Queue(Node)->Head + 1) % RPI5VC4_MAX_PENDING;
            Queue(Node)->Count--;
            goto NextNode;
        }
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

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (DeviceExtension == NULL)
        return;

    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    Completed = Rpi5Vc4ProcessPendingLocked(DeviceExtension, &NeedPoll, &PipelineAborted);
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);

    if (InterlockedExchange(&DeviceExtension->V3dIsrMasked, 0) != 0 &&
        DeviceExtension->V3dIrqConnected &&
        DeviceExtension->V3dCoreBase != NULL &&
        !DeviceExtension->StopAccepting)
    {
        InterlockedIncrement(&DeviceExtension->V3dDpcFromIsr);
        WRITE_REGISTER_ULONG(
            (PULONG)((PUCHAR)DeviceExtension->V3dCoreBase + V3D_CTL_INT_MSK_CLR),
            V3D_INT_FLDONE | V3D_INT_FRDONE | V3D_INT_OUTOMEM |
            V3D_V7_INT_CSDDONE);
        if (DeviceExtension->V3dHubBase != NULL)
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

/* Sacrificial cold-core warm-up (codex round 15): one Mesa-shaped
 * bin-only job through the normal queue so the first-job PTB final-flush
 * wedge fires (and is absorbed by the 50ms reset) at boot instead of on
 * the user's first frame. */
VOID
Rpi5Vc4QueueWarmupV3dJob(
    _Inout_ PRPI5VC4_DEVICE_EXTENSION DeviceExtension)
{
    static const UCHAR WarmupBcl[] =
    {
        0x77, 0x00,
        0x78, 0x04, 0x1b, 0x00, 0x00, 0x3f, 0x00, 0x3f, 0x00,
        0x13,
        0x06,
        0x04
    };
    PRPI5VC4_PENDING_SUBMIT Entry;
    KIRQL OldIrql;
    BOOLEAN NeedPoll = FALSE;
    BOOLEAN PipelineAborted = FALSE;
    ULONG Waited;

    if (!DeviceExtension->V3dReady ||
        DeviceExtension->V3dOverflowVa == NULL)
    {
        return;
    }

    RtlCopyMemory((PUCHAR)DeviceExtension->V3dOverflowVa + 0x6100,
                  WarmupBcl, sizeof(WarmupBcl));
    KeMemoryBarrier();

    KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
    if (DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Count <
        RPI5VC4_MAX_PENDING)
    {
        ULONG Tail = (DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Head +
                      DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Count) %
                     RPI5VC4_MAX_PENDING;

        Entry = &DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Pending[Tail];
        RtlZeroMemory(Entry, sizeof(*Entry));
        Entry->IsV3dJob = TRUE;
        Entry->IsWarmup = TRUE;
        Entry->BclStart = (ULONG)DeviceExtension->V3dOverflowGpuVa + 0x6100;
        Entry->BclEnd = Entry->BclStart + sizeof(WarmupBcl);
        Entry->Qma = (ULONG)DeviceExtension->V3dOverflowGpuVa + 0x3000;
        Entry->Qms = 0x3000;
        Entry->Qts = (ULONG)DeviceExtension->V3dOverflowGpuVa + 0x6000;
        DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Count++;
        Rpi5Vc4ProcessPendingLocked(DeviceExtension, &NeedPoll, &PipelineAborted);
    }
    KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
    if (NeedPoll)
        Rpi5Vc4ArmV3dPollTimer(DeviceExtension);

    for (Waited = 0; Waited < 100; Waited++)
    {
        LARGE_INTEGER WarmWait;

        if (DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Count == 0)
            break;
        WarmWait.QuadPart = -10000; /* 1 ms; yields the CPU (PASSIVE) */
        KeDelayExecutionThread(KernelMode, FALSE, &WarmWait);
    }

    if (PipelineAborted)
    {
        DPRINT1("RPI5VC4: warm-up job aborted without fence completion\n");
    }
    else if (DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Count != 0)
    {
        DPRINT1("RPI5VC4: warm-up bin stuck beyond 100 ms — reset+drop\n");
        Rpi5V3dResetCore(DeviceExtension);
        KeAcquireSpinLock(&DeviceExtension->DmaLock, &OldIrql);
        while (DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Count != 0)
        {
            DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Head =
                (DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Head + 1) %
                RPI5VC4_MAX_PENDING;
            DeviceExtension->NodeQueue[RPI5VC4_NODE_3D].Count--;
        }
        KeReleaseSpinLock(&DeviceExtension->DmaLock, OldIrql);
    }
    else
    {
        /* Round-17: fresh-init state wedges the first UMD bin's final
         * flush; post-ResetCore state never does (27-iteration
         * discriminator, never contradicted).  Normalize to the proven
         * state before any real work. */
        DPRINT1("RPI5VC4: warm-up bin retired — normalization reset: %s\n",
                Rpi5V3dResetCore(DeviceExtension) ? "ok" : "FAILED");
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
 * Walk a packet stream.  Returns FALSE on malformed input.  When Job is
 * non-NULL, the last V3D job packet found is copied out.
 */
static BOOLEAN
Rpi5Vc4ParseDmaStream(
    _In_reads_bytes_(Length) const VOID *Stream,
    _In_ SIZE_T Length,
    _Out_opt_ PRPI5VC4_DMA_PACKET Job,
    _Out_opt_ PBOOLEAN HasJob)
{
    const UCHAR *Cursor = Stream;
    SIZE_T Remaining = Length;

    if (HasJob != NULL)
        *HasJob = FALSE;

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
            if (Job != NULL)
                *Job = *Packet;
            if (HasJob != NULL)
                *HasJob = TRUE;
        }
        else if (Packet->Op != RPI5VC4_DMA_OP_NOP &&
                 Packet->Op != RPI5VC4_DMA_OP_PRESENT)
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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

    switch (QueryAdapterInfo->Type)
    {
        case DXGKQAITYPE_UMDRIVERPRIVATE:
            if (QueryAdapterInfo->pOutputData == NULL)
                do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
            RtlZeroMemory(QueryAdapterInfo->pOutputData,
                          QueryAdapterInfo->OutputDataSize);
            return STATUS_SUCCESS;

        case DXGKQAITYPE_DRIVERCAPS:
        {
            PDXGK_DRIVERCAPS Caps = QueryAdapterInfo->pOutputData;

            if (Caps == NULL)
                do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
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
            Caps->ApertureSegmentCommitLimit = DeviceExtension->VramSize;
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
                do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
            if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT))
                return STATUS_BUFFER_TOO_SMALL;

            if (SegOut->pSegmentDescriptor == NULL)
            {
                /* Phase 1: report the segment count. */
                SegOut->NbSegment = 1;
                SegOut->PagingBufferSegmentId = RPI5VC4_SEGMENT_ID;
                SegOut->PagingBufferSize = 64 * 1024;
                return STATUS_SUCCESS;
            }

            if (SegOut->NbSegment < 1)
                do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_QUERYSEGMENT4:
        {
            /* WDDM 2.0 flavour: stride-addressed descriptor array. */
            PDXGK_QUERYSEGMENTOUT4 SegOut = QueryAdapterInfo->pOutputData;
            PDXGK_SEGMENTDESCRIPTOR4 Desc;

            if (SegOut == NULL)
                do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
            if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT4))
                return STATUS_BUFFER_TOO_SMALL;

            if (SegOut->pSegmentDescriptor == NULL)
            {
                SegOut->NbSegment = 1;
                SegOut->PagingBufferSegmentId = RPI5VC4_SEGMENT_ID;
                SegOut->PagingBufferSize = 64 * 1024;
                SegOut->PagingBufferPrivateDataSize = 0;
                return STATUS_SUCCESS;
            }

            if (SegOut->NbSegment < 1 ||
                SegOut->SegmentDescriptorStride < sizeof(DXGK_SEGMENTDESCRIPTOR4))
            {
                do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
            }

            Desc = (PDXGK_SEGMENTDESCRIPTOR4)SegOut->pSegmentDescriptor;
            RtlZeroMemory(Desc, sizeof(*Desc));
            Desc->Flags.CpuVisible = 1;
            Desc->Flags.LocalBudgetGroup = 1;
            Desc->BaseAddress = DeviceExtension->VramPhysical;
            Desc->CpuTranslatedAddress = DeviceExtension->VramPhysical;
            Desc->Size = DeviceExtension->VramSize;
            Desc->CommitLimit = DeviceExtension->VramSize;
            return STATUS_SUCCESS;
        }

        case DXGKQAITYPE_QUERYSEGMENT3:
        {
            /* WDDM 2.x flavour of the same two-pass protocol. */
            PDXGK_QUERYSEGMENTOUT3 SegOut = QueryAdapterInfo->pOutputData;
            PDXGK_SEGMENTDESCRIPTOR3 Desc;

            if (SegOut == NULL)
                do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
            if (QueryAdapterInfo->OutputDataSize < sizeof(DXGK_QUERYSEGMENTOUT3))
                return STATUS_BUFFER_TOO_SMALL;

            if (SegOut->pSegmentDescriptor == NULL)
            {
                SegOut->NbSegment = 1;
                SegOut->PagingBufferSegmentId = RPI5VC4_SEGMENT_ID;
                SegOut->PagingBufferSize = 64 * 1024;
                SegOut->PagingBufferPrivateDataSize = 0;
                return STATUS_SUCCESS;
            }

            if (SegOut->NbSegment < 1)
                do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

            Desc = &SegOut->pSegmentDescriptor[0];
            RtlZeroMemory(Desc, sizeof(*Desc));
            Desc->Flags.CpuVisible = 1;
            Desc->Flags.LocalBudgetGroup = 1;
            Desc->BaseAddress = DeviceExtension->VramPhysical;
            Desc->CpuTranslatedAddress = DeviceExtension->VramPhysical;
            Desc->Size = DeviceExtension->VramSize;
            Desc->CommitLimit = DeviceExtension->VramSize;
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

    if (DeviceExtension == NULL || GetStandardAllocationDriverData == NULL)
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

    /*
     * No private data accompanies standard allocations: the HVS scans
     * plain linear surfaces, so pitch = width * 4 is already what the
     * callers pre-computed.  Report zero-sized private data blobs.
     */
    GetStandardAllocationDriverData->AllocationPrivateDriverDataSize = 0;
    GetStandardAllocationDriverData->ResourcePrivateDriverDataSize = 0;

    switch (GetStandardAllocationDriverData->StandardAllocationType)
    {
        case DXGK_STDALLOCATION_SHAREDPRIMARYSURFACE:
        case DXGK_STDALLOCATION_SHADOWSURFACE:
        case DXGK_STDALLOCATION_STAGINGSURFACE:
        case DXGK_STDALLOCATION_GDISURFACE:
            return STATUS_SUCCESS;

        default:
            return STATUS_NOT_SUPPORTED;
    }
}

/* ========================================================================
 * Device / context / allocation objects
 * ====================================================================== */

NTSTATUS
APIENTRY
Rpi5Vc4DdiCreateDevice(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_CREATEDEVICE CreateDevice)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;
    PRPI5VC4_WDDM_DEVICE Device;

    if (DeviceExtension == NULL || CreateDevice == NULL)
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

    Device = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Device),
                                   RPI5VC4_POOL_TAG);
    if (Device == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Device, sizeof(*Device));
    Device->Magic = RPI5VC4_DEVICE_MAGIC;
    Device->Adapter = DeviceExtension;

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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    }

    if (CreateContext->NodeOrdinal >= RPI5VC4_GPU_NODE_COUNT)
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    }

    for (i = 0; i < CreateAllocation->NumAllocations; i++)
    {
        DXGK_ALLOCATIONINFO *Info = &CreateAllocation->pAllocationInfo[i];
        PRPI5VC4_ALLOCATION Allocation;
        SIZE_T Size = (Info->Size != 0) ? Info->Size : PAGE_SIZE;

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
        if (Size > DeviceExtension->VramSize)
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
        Info->SupportedReadSegmentSet = (1 << (RPI5VC4_SEGMENT_ID - 1));
        Info->SupportedWriteSegmentSet = (1 << (RPI5VC4_SEGMENT_ID - 1));
        Info->EvictionSegmentSet = 0;
        Info->Flags.CpuVisible = 1;
        Info->Flags.AccessedPhysically = 1;
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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    }

    for (i = 0; i < DestroyAllocation->NumAllocations; i++)
    {
        PRPI5VC4_ALLOCATION Allocation = (PRPI5VC4_ALLOCATION)DestroyAllocation->phAllocation[i];

        if (Allocation == NULL)
            continue;
        if (Allocation->Magic != RPI5VC4_ALLOCATION_MAGIC)
            do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    }

    for (i = 0; i < CloseAllocation->NumAllocations; i++)
    {
        PRPI5VC4_OPENALLOCATION Open = (PRPI5VC4_OPENALLOCATION)CloseAllocation->pOpenHandleList[i];

        if (Open == NULL)
            continue;
        if (Open->Magic != RPI5VC4_OPENALLOC_MAGIC)
            do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    }

    if (Render->pCommand == NULL || Render->CommandLength == 0 ||
        Render->pDmaBuffer == NULL)
    {
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    }

    if (Render->DmaSize < Render->CommandLength)
        return STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;

    /*
     * The user-mode command stream is the hardware-consumable RPI5VC4
     * packet stream (see rpi5vc4.h); validate before accepting it.
     */
    if (!Rpi5Vc4ParseDmaStream(Render->pCommand, Render->CommandLength,
                               NULL, NULL))
    {
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
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
                        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
            do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
        }

        if (Loc->PatchOffset < Patch->DmaBufferSubmissionStartOffset || Loc->PatchOffset > Patch->DmaBufferSubmissionEndOffset || sizeof(ULONG) > Patch->DmaBufferSubmissionEndOffset - Loc->PatchOffset)
        {
            do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
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
    PRPI5VC4_PENDING_SUBMIT Entry;
    KIRQL OldIrql;
    BOOLEAN Completed;
    BOOLEAN NeedPoll;
    BOOLEAN PipelineAborted;
    BOOLEAN MappingFound;
    BOOLEAN Stopping;
    RPI5VC4_DMA_MAPPING DmaMapping;
    PVOID DmaBuffer;
    ULONG ExpectedNode;

    if (DeviceExtension == NULL || SubmitCommand == NULL)
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    }

    if ((MappingFound && DmaMapping.Size != SubmitCommand->DmaBufferSize) || SubmitCommand->DmaBufferSubmissionStartOffset > SubmitCommand->DmaBufferSubmissionEndOffset || SubmitCommand->DmaBufferSubmissionEndOffset > SubmitCommand->DmaBufferSize)
        return STATUS_INVALID_PARAMETER;

    /*
     * Look for a V3D control-list job in the submitted range. Missing or
     * malformed physical mappings are rejected; only explicit null rendering
     * may complete without parsing a mapped DMA stream.
     */
    if (!SubmitCommand->Flags.NullRendering)
    {
        if (DmaBuffer == NULL || SubmitCommand->DmaBufferSubmissionStartOffset == SubmitCommand->DmaBufferSubmissionEndOffset)
            return STATUS_INVALID_PARAMETER;
        if (!Rpi5Vc4ParseDmaStream((PUCHAR)DmaBuffer + SubmitCommand->DmaBufferSubmissionStartOffset, SubmitCommand->DmaBufferSubmissionEndOffset - SubmitCommand->DmaBufferSubmissionStartOffset, &Job, &HasJob))
            return STATUS_INVALID_PARAMETER;
        if (!HasJob)
            return STATUS_INVALID_PARAMETER;
    }

    ExpectedNode = SubmitCommand->NodeOrdinal;
    if (HasJob)
    {
        if (!DeviceExtension->V3dReady)
            return STATUS_DEVICE_NOT_READY;
        if (Job.Op == RPI5VC4_DMA_OP_TFU_JOB && (Job.TfuJob.Regs[1] < RPI5VC4_V3D_SLAB_GPUVA || Job.TfuJob.Regs[6] < RPI5VC4_V3D_SLAB_GPUVA))
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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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

NTSTATUS
APIENTRY
Rpi5Vc4DdiBuildPagingBuffer(
    _In_    PVOID MiniportDeviceContext,
    _Inout_ PDXGKARG_BUILDPAGINGBUFFER BuildPagingBuffer)
{
    PRPI5VC4_DEVICE_EXTENSION DeviceExtension = MiniportDeviceContext;

    if (DeviceExtension == NULL || BuildPagingBuffer == NULL)
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
                do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
                do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

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
    UNREFERENCED_PARAMETER(MiniportDeviceContext);
    UNREFERENCED_PARAMETER(MessageNumber);

    /* No interrupt line is connected (root-enumerated devnode). */
    return FALSE;
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
#define RPI5VC4_RESOURCE_LIST_MAGIC  0x5652474cUL  /* 'LGRV' */

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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

    Resources = (const RPI5VC4_ESC_RESLIST *)(Base + Cursor);
    if (Resources->Magic != RPI5VC4_RESOURCE_LIST_MAGIC)
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    ResourceCount = Resources->ResourceCount;
    if (ResourceCount > 4096)
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    Cursor += sizeof(RPI5VC4_ESC_RESLIST);

    /* Skip the residency handle list — the slab is always resident. */
    if (Cursor + ResourceCount * sizeof(ULONG) < Cursor) /* overflow */
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    Cursor += ResourceCount * sizeof(ULONG);

    if (Cursor + sizeof(RPI5VC4_ESC_CMD) > Size)
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
    Command = (const RPI5VC4_ESC_CMD *)(Base + Cursor);
    (VOID)Command; /* CommandType 1 (smoke) or 2 (vc4kmt): both are submits */
    Cursor += sizeof(RPI5VC4_ESC_CMD);

    /* Signal block only when PacketType requests a fence signal. */
    if (Header->PacketType == RPI5VC4_SUBMIT_PACKET_TYPE)
        Cursor += sizeof(RPI5VC4_ESC_SIGNAL);

    if (Cursor + sizeof(RPI5VC4_DMA_PACKET) > Size)
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

    Packet = (const RPI5VC4_DMA_PACKET *)(Base + Cursor);
    DPRINT1("RPI5VC4: esc submit packet op=%d magic=%08lx\n",
            (int)Packet->Op, (ULONG)Packet->Magic);
    if (Packet->Magic != RPI5VC4_DMA_PACKET_MAGIC)
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);

    /* Present packets are descriptive only (dxgkrnl blit moved the pixels). */
    if (Packet->Op == RPI5VC4_DMA_OP_NOP ||
        Packet->Op == RPI5VC4_DMA_OP_PRESENT)
    {
        return STATUS_SUCCESS;
    }

    return Rpi5Vc4QueueEscapeJob(DeviceExtension, Packet);
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
        do { DPRINT1("RPI5VC4: EJ reject L%d\n", __LINE__); return STATUS_INVALID_PARAMETER; } while (0);
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
