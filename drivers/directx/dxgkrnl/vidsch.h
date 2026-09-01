/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video Scheduler (VidSch) internal header
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * Defines the GPU engine state machine, per-engine context structures,
 * and the VidSch interface used by the rest of dxgkrnl.
 *
 * The GPU scheduler is the component that sits between the D3D runtime
 * (via D3DKMT command submission) and the miniport's DxgkDdiSubmitCommand.
 * It manages:
 *   - Per-engine command queues with fence-ordered dispatch
 *   - Atomic engine state machine transitions
 *   - ISR-level interrupt notification and DPC-level completion processing
 *   - VSync-synchronized flip/present operations
 *
 * Windows 11 routes scheduling through dxgmms2.sys. ReactOS keeps it inline
 * in dxgkrnl.sys so scheduler state shares the adapter lifetime.
 */

#ifndef _VIDSCH_H_
#define _VIDSCH_H_

/* Pool tag: 'VScD' (DScV in tools) */
#define TAG_VIDSCH  'DcSV'

/* ========================================================================
 * Engine State Machine
 *
 * Each GPU engine maintains an atomic state field that governs what
 * operations are permitted.  All transitions use InterlockedCompareExchange
 * to ensure atomicity across ISR, DPC, and thread contexts.
 *
 * Supported state transitions:
 *
 *   IDLE --submit--> SUBMITTING --hw_accept--> RUNNING
 *   RUNNING --complete--> COMPLETING --processed--> IDLE
 *   RUNNING --preempt--> PREEMPTING --hw_ack--> PREEMPTED
 *   PREEMPTED --resubmit--> SUBMITTING
 *   RUNNING --timeout--> RESETTING --done--> RESET_COMPLETE --> IDLE
 *   IDLE --suspend--> SUSPENDED
 *   SUSPENDED --resume--> RESUMING --> IDLE
 *   IDLE --flip--> FLIP_PENDING --vsync--> FLIP_EXECUTING --> IDLE
 * ====================================================================== */
typedef enum _VIDSCH_ENGINE_STATE
{
    VidSchEngineIdle            = 0,
    VidSchEngineRunning         = 1,
    VidSchEngineBudgetComputed  = 2,
    VidSchEngineSubmitting      = 3,
    VidSchEnginePreempting      = 4,
    VidSchEnginePreempted       = 5,
    VidSchEngineResetting       = 6,
    VidSchEngineResetComplete   = 7,
    VidSchEngineSuspended       = 8,
    VidSchEngineResuming        = 9,
    VidSchEngineFlipPending     = 10,
    VidSchEngineFlipExecuting   = 11,
    VidSchEngineCompleting      = 12,
    VidSchEngineError           = 13,
    VidSchEngineStateCount      = 14
} VIDSCH_ENGINE_STATE;

typedef enum _VIDSCH_SCHEDULER_STATE
{
    VidSchSchedulerUninitialized = 0,
    VidSchSchedulerRunning       = 1,
    VidSchSchedulerSuspending    = 2,
    VidSchSchedulerSuspended     = 3,
    VidSchSchedulerResetting     = 4,
    VidSchSchedulerStopping      = 5,
    VidSchSchedulerError         = 6
} VIDSCH_SCHEDULER_STATE;

/* ========================================================================
 * VIDSCH_DMA_PACKET — Queued command descriptor
 *
 * Represents a single DMA command buffer submission waiting in an
 * engine's run queue.  Allocated from NonPagedPool with TAG_VIDSCH.
 * ====================================================================== */

/* Inline (deep-copied) list capacities; submissions with larger lists are
 * rejected with STATUS_NOT_SUPPORTED and take the caller's fallback path. */
#define VIDSCH_INLINE_ALLOCATIONS   32
#define VIDSCH_INLINE_PATCHES       64
#define VIDSCH_MAX_PENDING_PACKETS  512

/* VIDSCH_DMA_PACKET.SubmitFlags uses the DXGK_SUBMITCOMMANDFLAGS layout. */
#define VIDSCH_SUBMITFLAG_PAGING        0x00000001u
#define VIDSCH_SUBMITFLAG_PRESENT       0x00000002u
#define VIDSCH_SUBMITFLAG_NULLRENDERING 0x00000008u
#define VIDSCH_SUBMITFLAG_RESUBMISSION  0x00000080u

#define VIDSCH_CONTEXT_ORDER_NONE       0
#define VIDSCH_CONTEXT_ORDER_ADMITTED   1
#define VIDSCH_CONTEXT_ORDER_CLAIMED    2
#define VIDSCH_CONTEXT_ORDER_DISPATCHING 3
#define VIDSCH_CONTEXT_ORDER_SUBMITTED  4
#define VIDSCH_CONTEXT_ORDER_COMPLETING 5
#define VIDSCH_CONTEXT_ORDER_TERMINAL   6

typedef struct _VIDSCH_DMA_PACKET
{
    /* Retained while dxgmms2 holds this packet: the scheduler stores the
     * packet pointer as its opaque cookie, and dxgkrnl keeps one reference
     * from admission until it processes the retirement record. */
    ULONGLONG                   SchedulerCookie;
    /* Dispatch claim dxgmms2 issued for this packet, committed exactly once. */
    ULONGLONG                   SchedulerClaimToken;

    /* Monotonically increasing fence ID assigned at submit time. */
    ULONG                       SubmissionFenceId;

    /* Engine ordinal this packet targets. */
    ULONG                       EngineOrdinal;

    /* Node ordinal (GPU node index). */
    UINT                        NodeOrdinal;

    /* Physical DMA descriptor, or GPU VA geometry for virtual submits. */
    PDXGKRNL_DMA_BUFFER         DmaBuffer;
    D3DGPU_VIRTUAL_ADDRESS      DmaBufferGpuVa;
    /* Process whose GPU VA range this packet pinned, released once. */
    struct _DXGKRNL_PROCESS    *GpuVaPinProcess;
    ULONG                       VirtualDmaBufferSize;

    /* Private driver data passed through to DxgkDdiSubmitCommand. */
    PVOID                       DriverPrivateData;
    ULONG                       DriverPrivateDataSize;

    /* Allocation list and patch location list pointers. */
    PVOID                       AllocationList;
    ULONG                       AllocationListSize;

    PVOID                       PatchLocationList;
    ULONG                       PatchLocationListSize;

    /*
     * Node accounting: the performance counter reading taken when this
     * packet was handed to the miniport, or zero when no charge is open.
     * The charge opens exactly once per dispatch and closes exactly once
     * per retirement, both by interlocked exchange, so a resubmitted or
     * doubly-reported packet can neither double-charge nor double-credit.
     */
    volatile LONG64             ExecutionChargeStart;

    /* Back-pointer to the submitting context (for priority). */
    PVOID                       Context;        /* PDXGKRNL_CONTEXT */
    /*
     * Stable device identity for contextless tracked/paging packets as well as
     * ordinary context submissions.  The packet owns one device reference.
     */
    struct _DXGKRNL_DEVICE     *Device;
    HANDLE                      MiniportDeviceHandle;
    HANDLE                      MiniportContextHandle;

    /* TRUE if this packet represents a present/flip operation. */
    BOOLEAN                     IsPresent;

    /*
     * Deep-copied allocation list (stack lifetime at the submit site; the
     * packet may be kicked later from the completion DPC).  Tracked
     * (fence-at-kick) submissions store KERNEL-side DXGK_ALLOCATIONLIST
     * entries: SegmentId + PhysicalAddress must survive to the kick-time
     * DxgkDdiPatch. Public D3DDDI_ALLOCATIONLIST entries never own the
     * miniport-private allocation handle.
     */
    DXGK_ALLOCATIONLIST         InlineAllocationList[VIDSCH_INLINE_ALLOCATIONS];
    ULONG                       InlineAllocationCount;
    ULONG                       VidPnSourceId;
    ULONG                       SubmitFlags;
    BOOLEAN                     Tracked;
    BOOLEAN                     VirtualAddressing;
    BOOLEAN                     HoldsContextReference;
    PVOID                       OwnedDriverPrivateData;
    PVOID                       VirtualSubmitWorkItem;
    struct _VIDSCH_ENGINE      *OwnerEngine;
    volatile LONG               ReferenceCount;

    /*
     * Tracked mode: the DMA buffer is patched and handed to the
     * DMA-buffer tracker at KICK time.  Fences are minted at SUBMIT from
     * the one adapter-wide counter, so fence order == queue order ==
     * kick order; retirement never crosses un-executed work.
     */
    LONG                        Priority;
    BOOLEAN                     Kicked;
    /* Ownership is valid only until the adapter resets this epoch. */
    BOOLEAN                     FenceIdentityReserved;
    ULONG                       FenceIdentityEpoch;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    /* Set before the provider retires a faulted packet as a fence watermark. */
    volatile LONG               Faulted;
    NTSTATUS                    FaultStatus;
    volatile LONG               FaultCleanupQueued;
    volatile LONG               CancelFaultedDevicePackets;
    NTSTATUS                    FaultCleanupStatus;
#endif
    /* Borrowed after first kick so preemption can resubmit the same DMA. */
    PDXGKRNL_SUBMIT_DMA_BUFFER  TrackerReservation;
    BOOLEAN                     TrackerOwnsDmaBuffer;
    PDXGKRNL_DEVICE_WORK        DeviceWork;
    D3DKMT_HANDLE               SignalSyncObject;   /* fired at retire */
    ULONG64                     SignalFenceValue;
    D3DDDI_PATCHLOCATIONLIST    InlinePatchList[VIDSCH_INLINE_PATCHES];
    ULONG                       InlinePatchCount;
    WORK_QUEUE_ITEM             CleanupWorkItem;

    /* dxgmms2 context-stream ownership. The operation retains one packet
     * reference from successful admission through its retirement record. */
    PVOID                       ContextOrderOperation;
    ULONGLONG                   ContextOrderSequence;
    ULONGLONG                   ContextOrderClaimToken;
    volatile LONG               ContextOrderState;
    volatile LONG               ContextOrderCompletionPending;
    volatile LONG               ContextOrderResubmissionPending;
    NTSTATUS                    ContextOrderCompletionStatus;
    NTSTATUS                    ContextOrderAbortStatus;

} VIDSCH_DMA_PACKET, *PVIDSCH_DMA_PACKET;

/* ========================================================================
 * VIDSCH_ENGINE — Per-GPU-engine scheduling context
 *
 * The current scheduler supports exactly one WDDM engine (ordinal zero) per
 * node.  One instance therefore represents one node's sole engine.
 * ====================================================================== */
typedef struct _VIDSCH_ENGINE
{
    /* Back-pointer to owning adapter. */
    struct _DXGKRNL_ADAPTER    *Adapter;

    /* Back-pointer to the owning scheduler lifecycle context. */
    struct _VIDSCH_CONTEXT      *Scheduler;

    /*
     * Flattened dxgmms2 scheduler slot.  This is the node ordinal, not the
     * public WDDM EngineOrdinal (which is explicitly limited to zero).
     */
    ULONG                       SchedulerOrdinal;

    /* The engine state machine lives in dxgmms2; dxgkrnl reads it through
     * the scheduler interface and never caches it. */

    /*
     * Serializes this engine's dxgkrnl-side execution state only: the
     * preemption fields and the DPC/worker handshake.  The run queue, its
     * order, the pending/reserved counts, and the engine state machine are
     * owned by dxgmms2 and reached through Adapter->Mms2SchedulerInterface.
     */
    KSPIN_LOCK                  QueueLock;

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    /*
     * QueueLock protects ActivePacket.  The provider owns the packet reference
     * while this raw pointer is published; retirement clears it before
     * releasing that reference.
     */
    struct _VIDSCH_DMA_PACKET  *ActivePacket;

    /*
     * Lock-free interrupt-to-DPC handoff:
     *   0 = empty, 1 = writer/consumer owns the payload, 2 = ready.
     */
    volatile LONG               PageFaultInterruptState;
    DXGKARGCB_NOTIFY_INTERRUPT_DATA PageFaultInterruptData;
#endif

    /* Set by the ISR when a DMA_PREEMPTED notification must be consumed. */
    volatile LONG               PreemptionInterruptPending;
    volatile LONG               PendingPreemptionFenceId;
    ULONG                       PendingPreemptionEngineOrdinal;
    /* 0 = not issued, 1 = DDI in flight, 2 = awaiting DMA_PREEMPTED. */
    volatile LONG               PreemptionDdiState;
    volatile LONG               CompletedPreemptionFenceId;
    ULONG                       CompletedPreemptionEngineOrdinal;
    KEVENT                      PreemptionCompletedEvent;

    /*
     * Fence tracking.  dxgmms2 owns the submitted watermark; dxgkrnl keeps
     * only what the ISR reports back.
     *   NextFenceId        — next fence ID to assign (monotonic, per-engine).
     *   LastCompletedFence — fence of the most recently HW-completed command.
     *
     * NextFenceId is bumped via InterlockedIncrement.
     * LastCompletedFence is set from ISR context via InterlockedExchange.
     */
    volatile LONG               NextFenceId;
    volatile LONG               LastCompletedFence;

    /*
     * Completion event: signalled by the DPC handler when
     * LastCompletedFence advances.  Waited on by VidSchWaitForIdle.
     */
    KEVENT                      CompletionEvent;

    /*
     * DPC object for deferred completion processing.
     * Queued by VidSchNotifyInterrupt from ISR context.
     */
    KDPC                        CompletionDpc;

    /*
     * Reserved per-engine TDR timer. The adapter watchdog currently owns
     * timeout detection so only one PASSIVE_LEVEL reset can be active.
     */
    KTIMER                      TdrTimer;
    KDPC                        TdrDpc;

    volatile LONG               OutstandingWorkers;
    KEVENT                      WorkersDrainedEvent;

} VIDSCH_ENGINE, *PVIDSCH_ENGINE;

/* ========================================================================
 * VIDSCH_CONTEXT — Top-level scheduler context for an adapter
 *
 * Allocated once per adapter at VidSchInitialize time.
 * Holds the engine array and adapter-wide scheduler state.
 * ====================================================================== */
typedef struct _VIDSCH_CONTEXT
{
    /* Back-pointer to owning adapter. */
    struct _DXGKRNL_ADAPTER    *Adapter;

    /*
     * Array of VIDSCH_ENGINE structures, one per GPU node.
     * Count is Adapter->NodeCount (may be 0 for DOD adapters).
     */
    PVIDSCH_ENGINE              Engines;
    ULONG                       EngineCount;

    /* Serializes submit admission against suspend, reset, and teardown. */
    FAST_MUTEX                  LifecycleMutex;
    volatile LONG               LifecycleState;

    /* Typed DpiGet/SetSchedulerCallbackState bit-mask contract. */
    volatile LONG               CallbackState;

    /* TRUE once VidSchInitialize has completed successfully. */
    BOOLEAN                     Initialized;

} VIDSCH_CONTEXT, *PVIDSCH_CONTEXT;

/* ========================================================================
 * VidSch public interface — called by adapter.c, dma.c, present.c
 * ====================================================================== */

/*
 * VidSchInitialize
 *
 * Allocates and initializes the per-adapter scheduler context and
 * per-engine structures.  Called from DxgkAdapterStart after the
 * miniport has been started and NodeCount is known.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
VidSchInitialize(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

/*
 * VidSchDestroy
 *
 * Tears down the scheduler context and frees all engine structures after the
 * miniport has stopped owning submitted work.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
VidSchDestroy(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

/* Stops admission, DPCs, and workers, then cancels packets never submitted to
 * the miniport. Submitted packets remain pinned until VidSchDestroy. */
VOID
VidSchPrepareForStop(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

VOID VidSchCancelContextPackets(_In_ struct _DXGKRNL_ADAPTER *Adapter, _In_ struct _DXGKRNL_CONTEXT *Context, _In_ NTSTATUS CompletionStatus);
VOID VidSchAbortAllPackets(_In_ struct _DXGKRNL_ADAPTER *Adapter, _In_ NTSTATUS CompletionStatus);

/*
 * VidSchSubmitCommand
 *
 * Queues a DMA command buffer for execution on the specified engine.
 * Enqueues the packet and kicks the engine if idle.
 *
 * SubmissionFenceId: pass the adapter-wide fence (DxgkAllocateSubmission-
 * FenceId) so vidsch completion tracking and the adapter-wide DMA-buffer
 * retirement observe the same fence space; 0 mints a per-engine fence
 * (legacy/internal callers only).  The used fence returns in *OutFenceId.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
VidSchSubmitCommand(
    _In_  struct _DXGKRNL_ADAPTER *Adapter,
    _In_  ULONG                    NodeOrdinal,
    _In_  ULONG                    EngineOrdinal,
    _In_  ULONG                    SubmissionFenceId,
    _In_opt_ HANDLE                MiniportDeviceHandle,
    _In_opt_ HANDLE                MiniportContextHandle,
    _In_  BOOLEAN                  IsPresent,
    _In_  ULONG                    SubmitFlags,
    _In_  ULONG                    VidPnSourceId,
    _Out_ ULONG                   *OutFenceId);

NTSTATUS
VidSchSubmitCommandVirtual(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ struct _DXGKRNL_CONTEXT *Context,
    _In_ D3DGPU_VIRTUAL_ADDRESS DmaBufferGpuVa,
    _In_ ULONG DmaBufferSize,
    _In_reads_bytes_opt_(DriverPrivateDataSize) PVOID DriverPrivateData,
    _In_ ULONG DriverPrivateDataSize,
    _In_ BOOLEAN NullRendering,
    _Out_ ULONG *OutFenceId);

/*
 * VidSchSubmitCommandTracked
 *
 * Fence-at-kick submission: queues the DMA buffer WITHOUT a fence; the
 * kick path mints the adapter-wide fence, runs DxgkDdiPatch over the
 * deep-copied lists and hands the buffer to the DMA-buffer tracker
 * immediately before DxgkDdiSubmitCommand.  Buffer ownership transfers
 * to vidsch/the tracker on STATUS_SUCCESS.  Packets queued this way are
 * priority-ordered among themselves (higher Priority runs first, FIFO
 * within a priority, never overtaking fence-assigned packets or the
 * queue head).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
VidSchSubmitCommandTracked(
    _In_  struct _DXGKRNL_ADAPTER *Adapter,
    _In_  ULONG                    NodeOrdinal,
    _In_  ULONG                    EngineOrdinal,
    _In_  PDXGKRNL_DMA_BUFFER      DmaBuffer,
    _In_reads_bytes_opt_(DriverPrivateDataSize) CONST VOID *DriverPrivateData,
    _In_  ULONG                    DriverPrivateDataSize,
    _In_reads_opt_(AllocationListCount) CONST DXGK_ALLOCATIONLIST *AllocationList,
    _In_  ULONG                    AllocationListCount,
    _In_reads_opt_(PatchLocationListCount) CONST D3DDDI_PATCHLOCATIONLIST *PatchLocationList,
    _In_  ULONG                    PatchLocationListCount,
    _In_opt_ HANDLE                MiniportDeviceHandle,
    _In_opt_ HANDLE                MiniportContextHandle,
    _In_  LONG                     Priority,
    _In_  const DXGKRNL_TRACK_DMA_ARGS *TrackArgs,
    _In_  ULONG                    SubmitFlags,
    _In_  ULONG                    VidPnSourceId,
    _Out_ ULONG                   *OutFenceId);

/*
 * VidSchNotifyInterrupt
 *
 * Called from the ISR context (DIRQL) when the miniport reports a
 * DMA_COMPLETED or DMA_PREEMPTED interrupt.  Updates the engine's
 * LastCompletedFence and queues the completion DPC.
 *
 * IRQL: DIRQL (interrupt context)
 */
VOID
VidSchNotifyInterrupt(
    _In_ struct _DXGKRNL_ADAPTER              *Adapter,
    _In_ CONST DXGKARGCB_NOTIFY_INTERRUPT_DATA *NotifyData);

/*
 * VidSchNotifyDpc
 *
 * Called from DISPATCH_LEVEL after the miniport's DPC routine completes.
 * Per-engine retirement was scheduled when the interrupt data was
 * published; this callback marks the documented DPC protocol boundary.
 *
 * IRQL: DISPATCH_LEVEL
 */
VOID
VidSchNotifyDpc(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

/*
 * VidSchWaitForIdle
 *
 * Blocks the calling thread until all engines have drained their run
 * queues and returned to the Idle state.  Used during adapter stop.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS VidSchBeginStopDrain(_In_ struct _DXGKRNL_ADAPTER *Adapter);

NTSTATUS
VidSchWaitForIdle(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ ULONG                    TimeoutMs);

/*
 * VidSchQueryEngineStatus
 *
 * Returns the current state, pending count, and fence values for
 * the specified engine.
 *
 * IRQL: Any (reads volatile fields)
 */
/*
 * Node execution accounting.  Dispatch opens a node's busy charge and
 * retirement closes it; both are safe to call more than once for the same
 * packet, and both run at DISPATCH_LEVEL on the submission and completion
 * paths.
 */
VOID VidSchAccountNodeDispatch(_Inout_ PVIDSCH_DMA_PACKET Packet);
VOID VidSchAccountNodeRetire(_Inout_ PVIDSCH_DMA_PACKET Packet);

/*
 * Snapshot one node's accounting.  ProcessRecord selects whose share is
 * reported: NULL asks for the node as a whole.  Frequency conversion happens
 * here so no caller has to know the clock.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
NTSTATUS
VidSchQueryNodeStatistics(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_opt_ struct _DXGKRNL_PROCESS *ProcessRecord,
    _In_ ULONG NodeOrdinal,
    _Out_ D3DKMT_QUERYSTATISTICS_PROCESS_NODE_INFORMATION *Information);

/* The share of the node clock that belongs to dxgkrnl's own contextless
 * work rather than to any client process. */
NTSTATUS
VidSchQuerySystemNodeStatistics(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ ULONG NodeOrdinal,
    _Out_ D3DKMT_QUERYSTATISTICS_PROCESS_NODE_INFORMATION *Information);

NTSTATUS
VidSchQueryEngineStatus(
    _In_  struct _DXGKRNL_ADAPTER *Adapter,
    _In_  ULONG                    EngineOrdinal,
    _Out_ VIDSCH_ENGINE_STATE     *OutState,
    _Out_ ULONG                   *OutPendingCount,
    _Out_ ULONG                   *OutLastSubmittedFence,
    _Out_ ULONG                   *OutLastCompletedFence);

/*
 * VidSchStartScheduler
 *
 * Activates or resumes the inline scheduler for the adapter.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
VidSchStartScheduler(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

/*
 * VidSchFlipPresent
 *
 * Queues a flip/present operation for execution.  For immediate-mode
 * flips the engine is kicked directly.  For VSync-synchronized flips
 * the operation is deferred until the next vertical blank.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
VidSchFlipPresent(
    _In_ struct _DXGKRNL_ADAPTER          *Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    VidPnSourceId,
    _In_ ULONG                             EngineOrdinal,
    _In_ D3DDDI_FLIPINTERVAL_TYPE          FlipInterval,
    _In_opt_ PVOID                         Context,
    _Out_ ULONG                           *OutFenceId);

/* ========================================================================
 * Scheduler lifecycle and recovery interfaces
 * ====================================================================== */

NTSTATUS
VidSchPreemptEngine(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ ULONG                    NodeOrdinal,
    _In_ ULONG                    EngineOrdinal,
    _Out_opt_ PULONG              PreemptionFenceId);

NTSTATUS
VidSchWaitForPreemption(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ ULONG                    NodeOrdinal,
    _In_ ULONG                    EngineOrdinal,
    _In_ ULONG                    PreemptionFenceId,
    _In_ ULONG                    TimeoutMs);

NTSTATUS
VidSchSuspendScheduler(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

NTSTATUS
VidSchResumeScheduler(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

NTSTATUS
VidSchResetEngine(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ ULONG                    EngineOrdinal);

NTSTATUS
VidSchSetEngineState(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ ULONG                    EngineOrdinal,
    _In_ VIDSCH_ENGINE_STATE      NewState);

NTSTATUS
VidSchSetSchedulerCallback(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ PVOID                    CallbackContext);

ULONG
VidSchGetSchedulerCallbackState(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

ULONG
VidSchSetSchedulerCallbackState(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ ULONG                    State);

NTSTATUS
VidSchPrepareAdapterReset(
    _In_ struct _DXGKRNL_ADAPTER *Adapter);

VOID
VidSchCompleteAdapterReset(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ BOOLEAN                  ResetSucceeded);

NTSTATUS
VidSchGetEngineTdrInfo(
    _In_  struct _DXGKRNL_ADAPTER *Adapter,
    _In_  ULONG                    EngineOrdinal,
    _Out_ PVOID                    TdrInfo);

BOOLEAN VidSchGetOldestKickedPacket(_In_ struct _DXGKRNL_ADAPTER *Adapter, _Out_ PULONG FenceId, _Out_ PULONG NodeOrdinal, _Out_ PULONG EngineOrdinal);

VOID VidSchReferenceContextOrderPacket(_Inout_ PVIDSCH_DMA_PACKET Packet);
VOID VidSchDereferenceContextOrderPacket(_Inout_ PVIDSCH_DMA_PACKET Packet);
BOOLEAN VidSchIsContextOrderPacketDispatchable(_In_ PVIDSCH_DMA_PACKET Packet);
VOID VidSchDispatchClaimedContextOrderPacket(_Inout_ PVIDSCH_DMA_PACKET Packet);
BOOLEAN VidSchIsContextOrderPacketResubmittable(_In_ PVIDSCH_DMA_PACKET Packet);
BOOLEAN VidSchDispatchContextOrderPacketResubmission(_Inout_ PVIDSCH_DMA_PACKET Packet);
NTSTATUS DxgkContextOrderAdmitPacket(_Inout_ PDXGKRNL_CONTEXT Context, _Inout_ PVIDSCH_DMA_PACKET Packet);
VOID DxgkContextOrderPublishAdmittedPacket(_Inout_ PDXGKRNL_CONTEXT Context);
VOID DxgkContextOrderScheduleReferenced(_Inout_ PDXGKRNL_CONTEXT Context);
VOID DxgkContextOrderCommitPacket(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS SubmissionStatus);
VOID DxgkContextOrderCompletePacket(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS CompletionStatus);
VOID DxgkContextOrderAbortPacket(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS AbortStatus);

#endif /* _VIDSCH_H_ */
