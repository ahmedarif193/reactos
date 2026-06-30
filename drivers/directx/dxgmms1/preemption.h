/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU preemption and Timeout Detection & Recovery (TDR) subsystem
 *              for dxgmms1.sys — structure definitions and public API.
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * Overview
 * ---------
 * This header defines two closely related subsystems that provide GPU
 * context-switch and hang-recovery support in the WDDM memory manager:
 *
 *  1. GPU PREEMPTION
 *     When a higher-priority context needs to run, dxgmms1 requests the GPU
 *     to pause mid-execution at the next legal preemption boundary and save
 *     its state.  The preempted context is re-queued at the head of its
 *     priority band and the higher-priority work is dispatched immediately.
 *
 *     Node state machine:
 *       IDLE  ─► PENDING  ─► COMPLETED
 *                   │
 *                   └── (100 ms timeout) ─► TDR RESET
 *
 *  2. TIMEOUT DETECTION AND RECOVERY (TDR)
 *     If the GPU fails to make forward progress (hardware fence counter does
 *     not advance) within TDR_TIMEOUT_SECONDS, the TDR subsystem assumes the
 *     GPU is hung and initiates a controlled adapter reset:
 *
 *       timer fires → DxgkMmsTdrDpcRoutine (DISPATCH_LEVEL)
 *           |── GPU made progress?  → update snapshot, continue waiting
 *           └── GPU hung           → queue work item
 *                                     → DxgkMmsTdrWorkItemRoutine (PASSIVE_LEVEL)
 *                                         → DxgkMmsTdrReset
 *                                             |── drain queues
 *                                             |── DxgkDdiResetFromTimeout
 *                                             └── DxgkDdiRestartFromTimeout
 *
 * Inclusion order
 * ----------------
 * This header must be included from dxgmms1_private.h (or after it), as it
 * relies on forward declarations of DXGMMS_NODE and DXGMMS_ADAPTER_CONTEXT.
 * The structures DXGMMS_PREEMPTION_STATE and DXGMMS_TDR_STATE are embedded
 * by value in those types and must therefore be fully defined here before
 * the enclosing struct definitions appear.
 *
 * IRQL discipline
 * ----------------
 *   DxgkMmsTdrInit              PASSIVE_LEVEL
 *   DxgkMmsTdrStart             <= DISPATCH_LEVEL
 *   DxgkMmsTdrStop              <= DISPATCH_LEVEL
 *   DxgkMmsTdrDpcRoutine        DISPATCH_LEVEL (timer DPC, internal)
 *   DxgkMmsTdrQueueReset        DISPATCH_LEVEL
 *   DxgkMmsTdrReset             PASSIVE_LEVEL  (work item)
 *   DxgkMmsInitPreemption       PASSIVE_LEVEL
 *   DxgkMmsPreemptionCompleted  DISPATCH_LEVEL
 *   DxgkMmsPreemptionTimeout    DISPATCH_LEVEL
 */

#pragma once

#ifndef _DXGMMS1_PREEMPTION_H_
#define _DXGMMS1_PREEMPTION_H_

/* =========================================================================
 * Timing constants
 * ========================================================================= */

/*
 * TDR_TIMEOUT_SECONDS
 *
 * Number of seconds a GPU engine is allowed to remain unresponsive before
 * TDR initiates a hardware reset.  Windows Vista/7/10 use 2 seconds as the
 * default (registry-overridable in production; hard-coded here).
 *
 * The value is expressed as a negative 100-ns KTIMER due time (relative):
 *   DueTime.QuadPart = TDR_TIMEOUT_100NS
 */
#define TDR_TIMEOUT_SECONDS         2
#define TDR_TIMEOUT_100NS           (-(LONGLONG)(TDR_TIMEOUT_SECONDS) * 10000000LL)

/*
 * PREEMPTION_TIMEOUT_MS
 *
 * Maximum milliseconds allowed for the GPU to honour a preemption request
 * before the request is escalated to a full TDR reset.  100 ms matches the
 * Windows Vista/7 internal default.
 */
#define PREEMPTION_TIMEOUT_MS       100
#define PREEMPTION_TIMEOUT_100NS    (-(LONGLONG)(PREEMPTION_TIMEOUT_MS) * 10000LL)

/* =========================================================================
 * Forward declarations
 *
 * DXGMMS_NODE and DXGMMS_ADAPTER_CONTEXT are defined in dxgmms1_private.h
 * which includes this header.  We use struct-tag forward declarations here
 * so that DXGMMS_PREEMPTION_STATE can hold a PDXGMMS_NODE back-pointer.
 *
 * The typedef aliases (DXGMMS_NODE, PDXGMMS_NODE, etc.) are provided by
 * dxgmms1_private.h before this header is included; we use only the struct
 * tags here to avoid redefinition errors.
 * ========================================================================= */
struct _DXGMMS_NODE;
struct _DXGMMS_ADAPTER_CONTEXT;

/* Pointer typedefs — defined here only if dxgmms1_private.h has not yet
 * provided them (i.e. when preemption.h is included in isolation).
 * The #ifndef guard prevents duplicate typedef errors when both headers
 * are processed in the same translation unit. */
#ifndef _DXGMMS1_PRIVATE_H_
typedef struct _DXGMMS_NODE              DXGMMS_NODE,            *PDXGMMS_NODE;
typedef struct _DXGMMS_ADAPTER_CONTEXT   DXGMMS_ADAPTER_CONTEXT, *PDXGMMS_ADAPTER_CONTEXT;
#endif

/* =========================================================================
 * DXGMMS_PREEMPTION_STATE
 *
 * Per-node preemption tracking state.  Embedded by value in DXGMMS_NODE
 * as the PreemptState field.
 *
 * LOCKING: all mutable fields are protected by DXGMMS_NODE::Lock
 * (a KSPIN_LOCK acquired at DISPATCH_LEVEL).  The timer and DPC objects
 * are initialised once at PASSIVE_LEVEL and then only armed/cancelled.
 *
 * INITIALISATION: DxgkMmsInitPreemption at PASSIVE_LEVEL; called once
 * per node from DxgkMmsAddAdapter after the node array is zeroed.
 * ========================================================================= */
typedef struct _DXGMMS_PREEMPTION_STATE
{
    /*
     * Back-pointer to the GPU engine node that owns this preemption state.
     * Set once at DxgkMmsInitPreemption time; never changed thereafter.
     * Used by the timeout DPC to navigate from DPC context → node → adapter.
     */
    PDXGMMS_NODE        Node;

    /*
     * Fence identifier forwarded to the miniport in the most recent
     * DxgkDdiPreemptCommand call.  Zero when no preemption is in flight.
     * Protected by Node->Lock.
     */
    ULONG64             PreemptionFenceId;

    /*
     * Wall-clock time (100-ns units) recorded immediately before
     * DxgkDdiPreemptCommand was called.  Used for diagnostic logging and to
     * verify the timeout window in the timeout DPC.
     */
    LARGE_INTEGER       PreemptionTimestamp;

    /*
     * TRUE once DxgkDdiPreemptCommand has been issued to the miniport and
     * the GPU has not yet signalled preemption-complete.  Cleared by
     * DxgkMmsPreemptionCompleted when the interrupt arrives.
     * Mirrors DXGMMS_NODE::PreemptionPending; maintained here as the
     * authoritative state for the timeout escalation path.
     * Protected by Node->Lock.
     */
    BOOLEAN             PreemptionRequested;

    /*
     * Set to TRUE by DxgkMmsPreemptionCompleted when the GPU raises the
     * preemption-complete interrupt.  Cleared on each new preemption
     * request and at DxgkMmsInitPreemption time.
     * Protected by Node->Lock.
     */
    BOOLEAN             PreemptionCompleted;

    /*
     * One-shot timer armed when PreemptionRequested becomes TRUE.
     * Fires after PREEMPTION_TIMEOUT_100NS if the GPU does not signal
     * preemption completion.
     *
     * Type: SynchronizationTimer (auto-reset) so that exactly one DPC fires
     * per expiry and extra firings do not accumulate.
     */
    KTIMER              PreemptionTimer;

    /*
     * DPC associated with PreemptionTimer.  DeferredContext = the owning
     * DXGMMS_PREEMPTION_STATE so the DPC can recover the node without a
     * global search.  Implemented by DxgkMmsPreemptionTimeoutDpc (static,
     * preemption.c).
     */
    KDPC                PreemptionTimeoutDpc;

    /*
     * Auto-reset event (SynchronizationEvent) signalled by
     * DxgkMmsPreemptionCompleted.  Allows optional PASSIVE_LEVEL waiters
     * (e.g. WaitForIdleGpu) to block without busy-polling.
     */
    KEVENT              PreemptionEvent;

} DXGMMS_PREEMPTION_STATE, *PDXGMMS_PREEMPTION_STATE;

/* =========================================================================
 * DXGMMS_TDR_WORK_ITEM
 *
 * Context for the ExQueueWorkItem work item queued by DxgkMmsTdrDpcRoutine
 * or DxgkMmsPreemptionTimeout.  Allocated from NonPagedPool at
 * DISPATCH_LEVEL; freed by DxgkMmsTdrWorkItemRoutine after the reset.
 *
 * The WORK_QUEUE_ITEM must be the first field so that ExInitializeWorkItem
 * and ExQueueWorkItem receive a pointer to the right type.
 * ========================================================================= */
typedef struct _DXGMMS_TDR_WORK_ITEM
{
    WORK_QUEUE_ITEM         WorkQueueItem;  /* ExQueueWorkItem anchor       */
    PDXGMMS_ADAPTER_CONTEXT Adapter;        /* adapter to reset             */
    ULONG                   NodeOrdinal;    /* hung node; MAXULONG = all    */

} DXGMMS_TDR_WORK_ITEM, *PDXGMMS_TDR_WORK_ITEM;

/* =========================================================================
 * DXGMMS_TDR_STATE
 *
 * Per-adapter TDR watchdog state.  Embedded by value in
 * DXGMMS_ADAPTER_CONTEXT as the TdrState field.
 *
 * INITIALISATION: DxgkMmsTdrInit at PASSIVE_LEVEL; called once per adapter
 * from DxgkMmsAddAdapter after the adapter context is zeroed.
 * ========================================================================= */
typedef struct _DXGMMS_TDR_STATE
{
    /*
     * Periodic watchdog timer.  Type: SynchronizationTimer (auto-reset).
     *
     * Armed by DxgkMmsTdrStart as periodic:
     *   DueTime = TDR_TIMEOUT_100NS (relative)
     *   Period  = TDR_TIMEOUT_SECONDS * 1000  (ms, per KeSetTimerEx)
     *
     * Cancelled by DxgkMmsTdrStop when the adapter becomes idle.
     */
    KTIMER              TdrTimer;

    /*
     * DPC associated with TdrTimer.  Context = owning DXGMMS_ADAPTER_CONTEXT.
     * Implemented by DxgkMmsTdrDpcRoutine (static, preemption.c).
     * Pinned to processor 0 to reduce cache-line bouncing.
     */
    KDPC                TdrDpc;

    /*
     * GPU fence snapshots used by the DPC to detect forward progress.
     *
     * LastSubmitFence    — fence value at the time of the last command
     *                      submission or at the previous DPC firing that
     *                      detected progress.  Updated by DxgkMmsTdrStart.
     *
     * LastCompletedFence — highest fence value seen completed by the GPU.
     *                      Updated by the TDR DPC when progress is detected.
     *
     * Declared volatile to prevent the compiler from hoisting reads across
     * the KeSetTimerEx call that arms the watchdog.  On x86-64 (TSO) a plain
     * MOV is sufficient for ordering, but volatile is correct on all targets.
     */
    volatile ULONG64    LastSubmitFence;    /* fence snapshot at submission  */
    volatile ULONG64    LastCompletedFence; /* highest fence seen completed  */

    /*
     * TRUE while a TDR reset is queued (work item in flight) or currently
     * executing in DxgkMmsTdrReset.  Set before queuing the work item;
     * cleared at the end of DxgkMmsTdrReset under ResetLock.
     *
     * Written from DISPATCH_LEVEL (TDR DPC) as a volatile assignment; the
     * fast mutex in DxgkMmsTdrReset serialises the PASSIVE_LEVEL body.
     */
    volatile BOOLEAN    TdrActive;

    /*
     * TRUE while DxgkMmsTdrReset is actively executing.  Prevents the DPC
     * from queuing a second reset while the first is still in progress.
     * Set just before ExQueueWorkItem; cleared at the end of
     * DxgkMmsTdrReset while ResetLock is still held.
     */
    volatile BOOLEAN    InReset;

    /*
     * Monotonically increasing count of successfully completed TDR resets
     * on this adapter.  Incremented under ResetLock in DxgkMmsTdrReset.
     * Useful for diagnostics and user-mode event logging.
     */
    ULONG               TdrCount;

    /*
     * Fast mutex serialising concurrent TDR reset attempts.  Acquired at
     * PASSIVE_LEVEL at the start of DxgkMmsTdrReset; released before
     * return.  Ensures at most one reset path executes the hardware reset
     * sequence at a time.
     *
     * Initialised by ExInitializeFastMutex in DxgkMmsTdrInit.
     */
    FAST_MUTEX          ResetLock;

} DXGMMS_TDR_STATE, *PDXGMMS_TDR_STATE;

/* =========================================================================
 * Public API — implemented in preemption.c
 * ========================================================================= */

/*
 * DxgkMmsInitPreemption
 *
 * Initialise all per-node preemption state (timer, DPC, event, back-link).
 * Called from DxgkMmsAddAdapter after each DXGMMS_NODE has been zeroed.
 * Returns STATUS_SUCCESS on success (init is infallible for timers/events).
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkMmsInitPreemption(
    _In_ PDXGMMS_NODE Node);

/*
 * DxgkMmsPreemptionCompleted
 *
 * Called at DISPATCH_LEVEL when the GPU raises the preemption-complete
 * interrupt for the specified node.
 *
 *   - Clears PreemptionPending / PreemptState.PreemptionRequested.
 *   - Cancels the preemption timeout timer.
 *   - Sets PreemptState.PreemptionCompleted.
 *   - Signals PreemptState.PreemptionEvent.
 *   - Re-queues the preempted command buffer at the head of its priority band.
 *   - Calls DxgkMmsScheduleNext to dispatch the waiting higher-priority work.
 *
 * IRQL: DISPATCH_LEVEL
 */
VOID
DxgkMmsPreemptionCompleted(
    _In_ PDXGMMS_NODE Node,
    _In_ ULONG64      CompletedFenceId);

/*
 * DxgkMmsHandlePreemptionComplete
 *
 * Backward-compatibility alias for DxgkMmsPreemptionCompleted.
 * Some call sites in dxgmms1_private.h use this older name.
 *
 * IRQL: DISPATCH_LEVEL
 */
VOID
DxgkMmsHandlePreemptionComplete(
    _In_ PDXGMMS_NODE Node,
    _In_ ULONG64      CompletedFenceId);

/*
 * DxgkMmsTdrInit
 *
 * Initialise per-adapter TDR state (timer, DPC, fast mutex).
 * Must be called once from DxgkMmsAddAdapter at PASSIVE_LEVEL before any
 * command buffers are submitted to the adapter.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkMmsTdrInit(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter);

/*
 * DxgkMmsTdrStart
 *
 * Arm (or re-arm) the TDR watchdog timer for an adapter.  Called whenever
 * a command buffer is submitted to a previously idle adapter so the
 * watchdog can detect a hang if the GPU stops making progress.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
VOID
DxgkMmsTdrStart(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   NodeOrdinal);

/*
 * DxgkMmsTdrStop
 *
 * Cancel the TDR watchdog timer.  Called when all command buffers on an
 * adapter have completed and the adapter becomes idle.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
VOID
DxgkMmsTdrStop(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   NodeOrdinal);

/*
 * DxgkMmsTdrQueueReset
 *
 * Allocate a DXGMMS_TDR_WORK_ITEM from NonPagedPool and queue it to the
 * DelayedWorkQueue.  The worker thread calls DxgkMmsTdrReset at
 * PASSIVE_LEVEL.  Called from the TDR or preemption-timeout DPCs.
 *
 * IRQL: DISPATCH_LEVEL
 */
VOID
DxgkMmsTdrQueueReset(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   NodeOrdinal);

/*
 * DxgkMmsTdrReset
 *
 * Full GPU reset sequence at PASSIVE_LEVEL:
 *   1. Acquire TdrState.ResetLock (fast mutex).
 *   2. For each node: drain ready queues and running-buffer slot, failing
 *      pending commands with STATUS_GRAPHICS_ADAPTER_WAS_RESET.
 *   3. Call miniport DxgkDdiResetFromTimeout.
 *   4. Call miniport DxgkDdiRestartFromTimeout.
 *   5. Increment TdrCount; clear InReset and TdrActive.
 *   6. Release TdrState.ResetLock.
 *   7. For each node: call DxgkMmsScheduleNext in case commands were
 *      submitted while the reset was in progress.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
DxgkMmsTdrReset(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter);

/*
 * DxgkMmsPreemptionTimeout
 *
 * Called at DISPATCH_LEVEL when the per-node preemption timeout DPC fires
 * and the GPU has still not signalled preemption completion.  Sets the
 * adapter into reset state and queues a work item via DxgkMmsTdrQueueReset.
 *
 * IRQL: DISPATCH_LEVEL
 */
VOID
DxgkMmsPreemptionTimeout(
    _In_ PDXGMMS_NODE Node);

#endif /* _DXGMMS1_PREEMPTION_H_ */
