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
 * On Windows this is implemented as a separate dxgmms1.sys binary; in
 * ReactOS it is compiled inline into dxgkrnl.sys for simplicity.
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
 * State diagram (Phase 1 — foundation transitions only):
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
#define VIDSCH_INLINE_ALLOCATIONS   3
#define VIDSCH_INLINE_PATCHES       8
#define VIDSCH_MAX_PENDING_PACKETS  512

typedef struct _VIDSCH_DMA_PACKET
{
    /* Linkage in VIDSCH_ENGINE->RunQueueHead. */
    LIST_ENTRY                  RunQueueEntry;

    /* Monotonically increasing fence ID assigned at submit time. */
    ULONG                       SubmissionFenceId;

    /* Engine ordinal this packet targets. */
    ULONG                       EngineOrdinal;

    /* Node ordinal (GPU node index). */
    UINT                        NodeOrdinal;

    /* Physical DMA descriptor, or GPU VA geometry for virtual submits. */
    PDXGKRNL_DMA_BUFFER         DmaBuffer;
    D3DGPU_VIRTUAL_ADDRESS      DmaBufferGpuVa;
    ULONG                       VirtualDmaBufferSize;

    /* Private driver data passed through to DxgkDdiSubmitCommand. */
    PVOID                       DriverPrivateData;
    ULONG                       DriverPrivateDataSize;

    /* Allocation list and patch location list pointers. */
    PVOID                       AllocationList;
    ULONG                       AllocationListSize;

    PVOID                       PatchLocationList;
    ULONG                       PatchLocationListSize;

    /* Back-pointer to the submitting context (for priority). */
    PVOID                       Context;        /* PDXGKRNL_CONTEXT */
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
    PDXGKRNL_SUBMIT_DMA_BUFFER  TrackerReservation;
    D3DKMT_HANDLE               SignalSyncObject;   /* fired at retire */
    ULONG64                     SignalFenceValue;
    D3DDDI_PATCHLOCATIONLIST    InlinePatchList[VIDSCH_INLINE_PATCHES];
    ULONG                       InlinePatchCount;
    WORK_QUEUE_ITEM             CleanupWorkItem;

} VIDSCH_DMA_PACKET, *PVIDSCH_DMA_PACKET;

/* ========================================================================
 * VIDSCH_ENGINE — Per-GPU-engine scheduling context
 *
 * One instance per engine (node) in the adapter.  Manages the run queue,
 * fence tracking, and engine state machine for a single GPU engine.
 * ====================================================================== */
typedef struct _VIDSCH_ENGINE
{
    /* Back-pointer to owning adapter. */
    struct _DXGKRNL_ADAPTER    *Adapter;

    /* Back-pointer to the owning scheduler lifecycle context. */
    struct _VIDSCH_CONTEXT      *Scheduler;

    /* Zero-based engine ordinal within the adapter. */
    ULONG                       EngineOrdinal;

    /*
     * Atomic engine state machine field.
     * All transitions use InterlockedCompareExchange.
     * Read with InterlockedCompareExchange(field, 0, 0) or volatile read.
     */
    volatile LONG               State;

    /*
     * SpinLock protecting RunQueueHead and packet counters.
     * Acquired at DISPATCH_LEVEL from thread context and from the DPC path.
     */
    KSPIN_LOCK                  QueueLock;

    /*
     * Run queue: FIFO list of VIDSCH_DMA_PACKET entries awaiting submission.
     * Head = next packet to submit; packets appended at tail.
     */
    LIST_ENTRY                  RunQueueHead;

    /* Number of packets currently in the run queue. */
    ULONG                       PendingPacketCount;

    /* Set by the ISR when a DMA_PREEMPTED notification must be consumed. */
    volatile LONG               PreemptionInterruptPending;

    /*
     * Fence tracking.
     *   NextFenceId      — next fence ID to assign (monotonic, per-engine).
     *   LastSubmittedFence — fence of the most recently submitted command.
     *   LastCompletedFence — fence of the most recently HW-completed command.
     *
     * NextFenceId is bumped via InterlockedIncrement.
     * LastSubmittedFence is set under QueueLock.
     * LastCompletedFence is set from ISR context via InterlockedExchange.
     */
    volatile LONG               NextFenceId;
    volatile LONG               LastSubmittedFence;
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

    /* Typed DpiGet/SetSchedulerCallbackState contract. */
    volatile LONG               CallbacksEnabled;

    /* TRUE once VidSchInitialize has completed successfully. */
    BOOLEAN                     Initialized;

} VIDSCH_CONTEXT, *PVIDSCH_CONTEXT;

/* ========================================================================
 * VIDSCH_INTERFACE — Function pointer table exported to dxgkrnl
 *
 * On Windows 8.1, dxgmms1.sys exports ordinal 2 (VidSchInterface) which
 * fills this table.  In ReactOS, the scheduler is built inline into
 * dxgkrnl.sys.  We define the interface structure to match the Win8.1
 * layout (15 function pointer slots) so that future separation into a
 * standalone module is straightforward.
 *
 * Slot order matches the .text thunk catalog from the cleanroom analysis:
 *   0x1AC30  VidSchInitialize
 *   0x1AC5C  VidSchStartScheduler
 *   0x1ACFC  VidSchSubmitCommand
 *   0x1AD48  VidSchNotifyInterrupt
 *   0x1AD50  VidSchNotifyDpc
 *   0x1ADCC  VidSchPreemptEngine
 *   0x1B748  VidSchSuspendScheduler
 *   0x1B888  VidSchResumeScheduler
 *   0x1B890  VidSchSetEngineState
 *   0x1B9E4  VidSchResetEngine
 *   0x1BC20  VidSchFlipPresent
 *   0x1BC94  VidSchWaitForIdle
 *   0x1BCBC  VidSchQueryEngineStatus
 *   0x1BD88  VidSchSetSchedulerCallback
 *   0x1BD9C  VidSchGetEngineTdrInfo
 * ====================================================================== */

/* Function pointer typedefs for the interface table. */
typedef NTSTATUS (NTAPI *PFNVIDSCH_INITIALIZE)(PVOID Adapter);
typedef NTSTATUS (NTAPI *PFNVIDSCH_START_SCHEDULER)(PVOID Adapter);
typedef NTSTATUS (NTAPI *PFNVIDSCH_SUBMIT_COMMAND)(PVOID Adapter, PVOID SubmitArgs);
typedef VOID     (NTAPI *PFNVIDSCH_NOTIFY_INTERRUPT)(PVOID Adapter, CONST DXGKARGCB_NOTIFY_INTERRUPT_DATA *NotifyData);
typedef VOID     (NTAPI *PFNVIDSCH_NOTIFY_DPC)(PVOID Adapter);
typedef NTSTATUS (NTAPI *PFNVIDSCH_PREEMPT_ENGINE)(PVOID Adapter, ULONG EngineOrdinal);
typedef NTSTATUS (NTAPI *PFNVIDSCH_SUSPEND_SCHEDULER)(PVOID Adapter);
typedef NTSTATUS (NTAPI *PFNVIDSCH_RESUME_SCHEDULER)(PVOID Adapter);
typedef NTSTATUS (NTAPI *PFNVIDSCH_SET_ENGINE_STATE)(PVOID Adapter, ULONG EngineOrdinal, LONG NewState);
typedef NTSTATUS (NTAPI *PFNVIDSCH_RESET_ENGINE)(PVOID Adapter, ULONG EngineOrdinal);
typedef NTSTATUS (NTAPI *PFNVIDSCH_FLIP_PRESENT)(PVOID Adapter, PVOID FlipArgs);
typedef NTSTATUS (NTAPI *PFNVIDSCH_WAIT_FOR_IDLE)(PVOID Adapter, ULONG TimeoutMs);
typedef NTSTATUS (NTAPI *PFNVIDSCH_QUERY_ENGINE_STATUS)(PVOID Adapter, ULONG EngineOrdinal, PVOID OutStatus);
typedef NTSTATUS (NTAPI *PFNVIDSCH_SET_SCHEDULER_CALLBACK)(PVOID Adapter, PVOID CallbackContext);
typedef NTSTATUS (NTAPI *PFNVIDSCH_GET_ENGINE_TDR_INFO)(PVOID Adapter, ULONG EngineOrdinal, PVOID TdrInfo);

typedef struct _VIDSCH_INTERFACE
{
    /* Slot  0 */ PFNVIDSCH_INITIALIZE             Initialize;
    /* Slot  1 */ PFNVIDSCH_START_SCHEDULER         StartScheduler;
    /* Slot  2 */ PFNVIDSCH_SUBMIT_COMMAND          SubmitCommand;
    /* Slot  3 */ PFNVIDSCH_NOTIFY_INTERRUPT         NotifyInterrupt;
    /* Slot  4 */ PFNVIDSCH_NOTIFY_DPC               NotifyDpc;
    /* Slot  5 */ PFNVIDSCH_PREEMPT_ENGINE           PreemptEngine;
    /* Slot  6 */ PFNVIDSCH_SUSPEND_SCHEDULER        SuspendScheduler;
    /* Slot  7 */ PFNVIDSCH_RESUME_SCHEDULER         ResumeScheduler;
    /* Slot  8 */ PFNVIDSCH_SET_ENGINE_STATE         SetEngineState;
    /* Slot  9 */ PFNVIDSCH_RESET_ENGINE             ResetEngine;
    /* Slot 10 */ PFNVIDSCH_FLIP_PRESENT             FlipPresent;
    /* Slot 11 */ PFNVIDSCH_WAIT_FOR_IDLE            WaitForIdle;
    /* Slot 12 */ PFNVIDSCH_QUERY_ENGINE_STATUS      QueryEngineStatus;
    /* Slot 13 */ PFNVIDSCH_SET_SCHEDULER_CALLBACK   SetSchedulerCallback;
    /* Slot 14 */ PFNVIDSCH_GET_ENGINE_TDR_INFO      GetEngineTdrInfo;
} VIDSCH_INTERFACE, *PVIDSCH_INTERFACE;

/* Number of function pointer slots in the interface. */
#define VIDSCH_INTERFACE_SLOT_COUNT  15

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
 * Processes completed commands, retires packets, and kicks pending
 * submissions.
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

/*
 * VidSchGetInterface
 *
 * Fills the VIDSCH_INTERFACE function pointer table.  This is the
 * equivalent of the dxgmms1.sys ordinal 2 export.  Called once per
 * adapter during initialization.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
VidSchGetInterface(
    _Out_ PVIDSCH_INTERFACE Interface);

/* ========================================================================
 * Scheduler lifecycle and recovery interfaces
 * ====================================================================== */

NTSTATUS
VidSchPreemptEngine(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ ULONG                    NodeOrdinal,
    _In_ ULONG                    EngineOrdinal);

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

NTSTATUS
VidSchGetSchedulerCallbackState(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _Out_ PBOOLEAN                Enabled);

NTSTATUS
VidSchSetSchedulerCallbackState(
    _In_ struct _DXGKRNL_ADAPTER *Adapter,
    _In_ BOOLEAN                  Enabled);

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

#endif /* _VIDSCH_H_ */
