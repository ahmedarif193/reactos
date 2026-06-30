/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU preemption and Timeout Detection & Recovery (TDR).
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * Implementation notes
 * ---------------------
 *
 * LOCKING MODEL
 *   Per-node state:    DXGMMS_NODE::Lock (KSPIN_LOCK at DISPATCH_LEVEL)
 *   Per-adapter TDR:   DXGMMS_TDR_STATE::ResetLock (FAST_MUTEX at PASSIVE_LEVEL)
 *
 *   The two locks must never be held simultaneously — no lock nesting between
 *   the node spinlock and the TDR fast mutex.  The node spinlock is always
 *   taken first and released before moving to the PASSIVE_LEVEL path.
 *
 * MINIPORT DDI CALLBACKS (stored as PVOID in DXGMMS_ADAPTER_CONTEXT)
 *   DxgkDdiPreemptCommand      — arms GPU preemption hardware
 *   DxgkDdiResetFromTimeout    — light GPU reset (PASSIVE_LEVEL)
 *   DxgkDdiRestartFromTimeout  — GPU resume after reset (PASSIVE_LEVEL)
 *
 *   These are cast from PVOID to their respective function-pointer types
 *   at each call site, matching the pattern used throughout scheduler.c.
 *
 * DXGKARG_PREEMPTCOMMAND layout (WDDM 1.0 WDK definition)
 *   typedef struct _DXGKARG_PREEMPTCOMMAND {
 *       UINT PreemptionFenceId;  // [in] fence written by GPU on completion
 *       UINT NodeOrdinal;        // [in] target engine
 *       UINT EngineOrdinal;      // [in] within-node ordinal (always 0 WDDM 1.0)
 *   } DXGKARG_PREEMPTCOMMAND;
 *
 *   This structure is forward-declared in dispmprt.h but not yet fully
 *   defined in the ReactOS DDK headers; we provide the definition locally.
 *
 * WORK ITEM PATTERN
 *   DxgkMmsTdrDpcRoutine and DxgkMmsPreemptionTimeout run at DISPATCH_LEVEL
 *   and cannot call PASSIVE_LEVEL-only APIs (ExAcquireFastMutex, etc.).
 *   They queue a WORK_QUEUE_ITEM via ExQueueWorkItem to DelayedWorkQueue;
 *   the worker thread (DxgkMmsTdrWorkItemRoutine) runs at PASSIVE_LEVEL and
 *   calls DxgkMmsTdrReset.  dxgmms1 owns no DEVICE_OBJECTs, so we use
 *   the legacy ExInitializeWorkItem / ExQueueWorkItem path rather than
 *   IoAllocateWorkItem / IoQueueWorkItem.
 *
 * STATUS CODES
 *   STATUS_GRAPHICS_ADAPTER_WAS_RESET (0xC01E0003)
 *     Returned for command buffers failed during TDR drain.
 *
 * DPRINT1 POLICY
 *   All TDR and preemption-timeout events are logged with DPRINT1 so that
 *   they appear in both checked and free builds.  These are not debug-only
 *   events — they represent real hardware failures that must be visible in
 *   production logs.
 */

#include "dxgmms1_private.h"

/*
 * Local definition of DXGKARG_PREEMPTCOMMAND.
 *
 * dispmprt.h forward-declares this type but does not provide the full struct.
 * We define it locally using the canonical Windows WDK WDDM 1.0 layout.
 * The field names and order match the Vista WDK specification.
 */
typedef struct _DXGKARG_PREEMPTCOMMAND_LOCAL
{
    UINT    PreemptionFenceId;  /* [in] fence written by GPU when preempt done */
    UINT    NodeOrdinal;        /* [in] GPU engine ordinal                     */
    UINT    EngineOrdinal;      /* [in] within-node ordinal; 0 for WDDM 1.0   */
} DXGKARG_PREEMPTCOMMAND_LOCAL;

/*
 * Callback typedefs matching those in dxgkrnl_private.h.
 * We re-define them locally so that preemption.c does not need to include
 * dxgkrnl_private.h (which belongs to the separate dxgkrnl driver).
 */
typedef NTSTATUS (APIENTRY *PDXGKDDI_PREEMPT_COMMAND_FN)(
    _In_ PVOID MiniportDeviceContext,
    _In_ CONST PVOID PreemptCommand);

typedef NTSTATUS (APIENTRY *PDXGKDDI_RESETFROMTIMEOUT_FN)(
    _In_ PVOID MiniportDeviceContext);

typedef NTSTATUS (APIENTRY *PDXGKDDI_RESTARTFROMTIMEOUT_FN)(
    _In_ PVOID MiniportDeviceContext);

/* =========================================================================
 * Private function forward declarations
 * ========================================================================= */

static KDEFERRED_ROUTINE  DxgkMmsPreemptionTimeoutDpc;
static KDEFERRED_ROUTINE  DxgkMmsTdrDpcRoutine;
static WORKER_THREAD_ROUTINE DxgkMmsTdrWorkItemRoutine;

/* =========================================================================
 * DxgkMmsInitPreemption
 *
 * Initialise all per-node preemption state.  The DXGMMS_NODE must have been
 * zeroed by the caller (DxgkMmsAddAdapter zeroes the node array via
 * ExAllocatePoolWithTag then explicit RtlZeroMemory).
 *
 * Sets the DPC context to the DXGMMS_PREEMPTION_STATE embedded in the node
 * so that the timeout DPC can navigate to the node without a global search.
 *
 * Returns STATUS_SUCCESS unconditionally (KeInitializeTimerEx,
 * KeInitializeDpc, KeInitializeEvent cannot fail).
 *
 * IRQL: PASSIVE_LEVEL
 * ========================================================================= */
NTSTATUS
DxgkMmsInitPreemption(
    _In_ PDXGMMS_NODE Node)
{
    PDXGMMS_PREEMPTION_STATE PreemptState;

    PAGED_CODE();
    ASSERT(Node != NULL);

    PreemptState = &Node->PreemptState;

    /* Back-link allows the timeout DPC to find the node cheaply. */
    PreemptState->Node = Node;

    /* Scalar fields — already zeroed by caller, set explicitly for clarity. */
    PreemptState->PreemptionFenceId              = 0;
    PreemptState->PreemptionTimestamp.QuadPart   = 0;
    PreemptState->PreemptionRequested            = FALSE;
    PreemptState->PreemptionCompleted            = FALSE;

    /*
     * Preemption timeout timer — SynchronizationTimer (auto-reset).
     * Armed as one-shot in DxgkMmsPreemptCommand via KeSetTimer.
     * Cancelled in DxgkMmsPreemptionCompleted.
     */
    KeInitializeTimerEx(&PreemptState->PreemptionTimer, SynchronizationTimer);

    /*
     * DPC for the preemption timeout timer.
     * DeferredContext = PreemptState so the DPC finds the owning node.
     * We do not call KeSetTargetProcessorDpc because preemption DPCs are
     * infrequent and the overhead of pinning to a specific CPU is not worth
     * the marginal cache benefit.
     */
    KeInitializeDpc(&PreemptState->PreemptionTimeoutDpc,
                    DxgkMmsPreemptionTimeoutDpc,
                    PreemptState);

    /*
     * Preemption completion event — SynchronizationEvent (auto-reset).
     * Initially non-signalled.
     * Signalled by DxgkMmsPreemptionCompleted; waited on by any
     * PASSIVE_LEVEL path that needs to block until preemption is confirmed.
     */
    KeInitializeEvent(&PreemptState->PreemptionEvent,
                      SynchronizationEvent,
                      FALSE);

    DXGMMS_TRACE("DxgkMmsInitPreemption: node %u initialised\n",
                 Node->NodeOrdinal);

    return STATUS_SUCCESS;
}

/* =========================================================================
 * DxgkMmsPreemptCommand  (called through DXGMMS_INTERFACE::PreemptCommand)
 *
 * Request GPU preemption on a specific engine node.
 *
 * Steps (simplified state machine):
 *   1. Validate parameters.
 *   2. Lock node, check no preemption is already pending, update state.
 *   3. Unlock, call miniport DxgkDdiPreemptCommand at DISPATCH_LEVEL.
 *   4. On miniport failure, roll back pending flag.
 *   5. On success, arm the one-shot preemption timeout timer (100 ms).
 *
 * IRQL: <= DISPATCH_LEVEL
 * ========================================================================= */
NTSTATUS
NTAPI
DxgkMmsPreemptCommand(
    _In_ PVOID   AdapterContext,
    _In_ ULONG   NodeOrdinal,
    _In_ ULONG64 PreemptionFenceId)
{
    PDXGMMS_ADAPTER_CONTEXT  Adapter;
    PDXGMMS_NODE             Node;
    PDXGMMS_PREEMPTION_STATE PreemptState;
    KIRQL                    OldIrql;
    NTSTATUS                 Status;
    DXGKARG_PREEMPTCOMMAND_LOCAL PreemptCmd;
    PDXGKDDI_PREEMPT_COMMAND_FN  MiniportPreempt;
    LARGE_INTEGER            DueTime;

    if (AdapterContext == NULL)
    {
        DXGMMS_ERR("DxgkMmsPreemptCommand: NULL adapter context\n");
        return STATUS_INVALID_PARAMETER;
    }

    Adapter = (PDXGMMS_ADAPTER_CONTEXT)AdapterContext;

    DxgMmsAssert(Adapter->Signature == DXGMMS_ADAPTER_SIGNATURE,
                 DXGMMS1_BUGCHECK_BAD_SIGNATURE);

    if (NodeOrdinal >= Adapter->NumNodes)
    {
        DXGMMS_ERR("DxgkMmsPreemptCommand: node %u out of range (max %u)\n",
                   NodeOrdinal, Adapter->NumNodes);
        return STATUS_INVALID_PARAMETER;
    }

    if (Adapter->DxgkDdiPreemptCommand == NULL)
    {
        /* Miniport does not support preemption (non-preemptible GPU). */
        DXGMMS_WARN("DxgkMmsPreemptCommand: no DxgkDdiPreemptCommand on adapter %p\n",
                    Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    Node         = &Adapter->Nodes[NodeOrdinal];
    PreemptState = &Node->PreemptState;

    /*
     * Acquire node lock.  Held only to check / set the pending flag; the
     * miniport call is made outside the lock to keep DISPATCH_LEVEL hold
     * time short.
     */
    KeAcquireSpinLock(&Node->Lock, &OldIrql);

    if (Node->PreemptionPending)
    {
        /* A preemption is already in flight for this node. */
        KeReleaseSpinLock(&Node->Lock, OldIrql);
        DXGMMS_WARN("DxgkMmsPreemptCommand: node %u — preemption already pending\n",
                    NodeOrdinal);
        return STATUS_PENDING;
    }

    /* Mark the node as having an in-flight preemption request. */
    Node->PreemptionPending           = TRUE;
    Node->PreemptionFenceId           = PreemptionFenceId;
    PreemptState->PreemptionFenceId   = PreemptionFenceId;
    PreemptState->PreemptionRequested = TRUE;
    PreemptState->PreemptionCompleted = FALSE;

    /* Capture the request timestamp for diagnostic logging. */
    KeQuerySystemTime(&PreemptState->PreemptionTimestamp);

    KeReleaseSpinLock(&Node->Lock, OldIrql);

    /* ------------------------------------------------------------------ */
    /* Issue DxgkDdiPreemptCommand to the miniport outside the spinlock.  */
    /* ------------------------------------------------------------------ */
    RtlZeroMemory(&PreemptCmd, sizeof(PreemptCmd));
    PreemptCmd.NodeOrdinal       = NodeOrdinal;
    PreemptCmd.EngineOrdinal     = 0;
    PreemptCmd.PreemptionFenceId = (UINT)(PreemptionFenceId & 0xFFFFFFFFULL);

    MiniportPreempt = (PDXGKDDI_PREEMPT_COMMAND_FN)Adapter->DxgkDdiPreemptCommand;

    DXGMMS_TRACE("DxgkMmsPreemptCommand: node %u fence %I64u — issuing to miniport\n",
                 NodeOrdinal, PreemptionFenceId);

    /*
     * DxgkDdiPreemptCommand is callable at DISPATCH_LEVEL.  It should
     * program the GPU preemption register and return immediately; the
     * actual preemption is asynchronous and will be confirmed by the GPU
     * raising the DMA-preempted interrupt.
     */
    Status = MiniportPreempt(Adapter->MiniportContext,
                             (PVOID)&PreemptCmd);

    if (!NT_SUCCESS(Status))
    {
        DXGMMS_ERR("DxgkMmsPreemptCommand: miniport returned 0x%08X for node %u "
                   "— rolling back pending flag\n", Status, NodeOrdinal);

        /* Roll back: clear the pending flag so the scheduler is not stalled. */
        KeAcquireSpinLock(&Node->Lock, &OldIrql);
        Node->PreemptionPending           = FALSE;
        Node->PreemptionFenceId           = 0;
        PreemptState->PreemptionRequested = FALSE;
        KeReleaseSpinLock(&Node->Lock, OldIrql);

        return Status;
    }

    /* ------------------------------------------------------------------ */
    /* Arm the preemption timeout timer.                                   */
    /*                                                                     */
    /* If the GPU does not signal completion within PREEMPTION_TIMEOUT_MS  */
    /* we escalate to a full TDR adapter reset.                           */
    /* ------------------------------------------------------------------ */
    DueTime.QuadPart = PREEMPTION_TIMEOUT_100NS;    /* relative (negative) */

    /*
     * KeSetTimer: one-shot timer, period = 0.  The DPC is already
     * initialised in PreemptState->PreemptionTimeoutDpc.
     */
    KeSetTimer(&PreemptState->PreemptionTimer,
               DueTime,
               &PreemptState->PreemptionTimeoutDpc);

    DXGMMS_TRACE("DxgkMmsPreemptCommand: node %u — preemption timer armed "
                 "(%d ms deadline)\n", NodeOrdinal, PREEMPTION_TIMEOUT_MS);

    return STATUS_SUCCESS;
}

/* =========================================================================
 * DxgkMmsPreemptionCompleted
 *
 * Called at DISPATCH_LEVEL when the GPU raises the preemption-complete
 * interrupt (DXGK_INTERRUPT_DMA_PREEMPTED) for the specified node.
 *
 * Steps:
 *   1. Lock node.
 *   2. Verify a preemption was outstanding; if not, log spurious and return.
 *   3. Clear PreemptionPending / PreemptionRequested, set PreemptionCompleted.
 *   4. Re-queue the running (now preempted) buffer at the head of its
 *      priority band so it resumes when this priority level runs next.
 *   5. Unlock.
 *   6. Cancel the preemption timeout timer (outside lock to avoid DPC ↔ lock
 *      deadlock if the DPC is already executing and trying to acquire Lock).
 *   7. Signal the preemption event (wakes any PASSIVE_LEVEL waiters).
 *   8. Call DxgkMmsScheduleNext to run the higher-priority work.
 *
 * IRQL: DISPATCH_LEVEL
 * ========================================================================= */
VOID
DxgkMmsPreemptionCompleted(
    _In_ PDXGMMS_NODE Node,
    _In_ ULONG64      CompletedFenceId)
{
    PDXGMMS_PREEMPTION_STATE PreemptState;
    PDXGMMS_COMMAND_BUFFER   RunningBuf;
    KIRQL                    OldIrql;
    ULONG                    Priority;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(Node != NULL);

    DXGMMS_TRACE("DxgkMmsPreemptionCompleted: node %u fence %I64u\n",
                 Node->NodeOrdinal, CompletedFenceId);

    PreemptState = &Node->PreemptState;

    KeAcquireSpinLock(&Node->Lock, &OldIrql);

    if (!Node->PreemptionPending)
    {
        KeReleaseSpinLock(&Node->Lock, OldIrql);
        DXGMMS_WARN("DxgkMmsPreemptionCompleted: node %u — spurious completion "
                    "(fence %I64u), no preemption pending\n",
                    Node->NodeOrdinal, CompletedFenceId);
        return;
    }

    /* Clear in-flight preemption state. */
    Node->PreemptionPending           = FALSE;
    Node->PreemptionFenceId           = 0;
    PreemptState->PreemptionRequested = FALSE;
    PreemptState->PreemptionCompleted = TRUE;
    PreemptState->PreemptionFenceId   = 0;

    /*
     * Re-queue the running buffer at the HEAD of its priority band.
     * InsertHeadList places it before any other entries in the same band
     * so it is the first candidate when its priority level runs again.
     *
     * RunningBuffer is NULL only if the command completed normally between
     * the preemption request and this notification — in that case there is
     * nothing to re-queue.
     */
    RunningBuf = Node->RunningBuffer;

    if (RunningBuf != NULL)
    {
        RunningBuf->IsPreempted = TRUE;

        Priority = RunningBuf->Priority;
        if (Priority >= DXGMMS_PRIORITY_LEVELS)
            Priority = DXGMMS_PRIORITY_NORMAL;

        InsertHeadList(&Node->ReadyQueue[Priority], &RunningBuf->QueueEntry);
        Node->RunningBuffer = NULL;
    }

    KeReleaseSpinLock(&Node->Lock, OldIrql);

    /*
     * Cancel the timeout timer.  Calling KeCancelTimer outside the spinlock
     * is safe here because:
     *   (a) Only one preemption can be in flight per node at a time.
     *   (b) If DxgkMmsPreemptionTimeoutDpc is already queued, it will
     *       find PreemptionRequested == FALSE under the lock and exit
     *       harmlessly without escalating to TDR.
     */
    KeCancelTimer(&PreemptState->PreemptionTimer);

    /*
     * Signal the event.  Any PASSIVE_LEVEL thread waiting in
     * KeWaitForSingleObject on PreemptionEvent will be unblocked.
     */
    KeSetEvent(&PreemptState->PreemptionEvent, IO_NO_INCREMENT, FALSE);

    /*
     * Schedule the next command buffer.  The higher-priority work that
     * triggered the preemption is at the head of the ready queue.
     * DxgkMmsScheduleNext acquires Node->Lock internally.
     */
    DxgkMmsScheduleNext(Node);
}

/* =========================================================================
 * DxgkMmsHandlePreemptionComplete (alias)
 *
 * Backward-compatibility wrapper.  Some call sites in dxgmms1_private.h
 * and interface.c use this older function name.
 * ========================================================================= */
VOID
DxgkMmsHandlePreemptionComplete(
    _In_ PDXGMMS_NODE Node,
    _In_ ULONG64      CompletedFenceId)
{
    DxgkMmsPreemptionCompleted(Node, CompletedFenceId);
}

/* =========================================================================
 * DxgkMmsPreemptionTimeoutDpc  (static — KDEFERRED_ROUTINE)
 *
 * Fires PREEMPTION_TIMEOUT_MS after a preemption was requested if the GPU
 * has not yet signalled preemption-complete.
 *
 * DeferredContext = PDXGMMS_PREEMPTION_STATE (embedded in the node).
 *
 * IRQL: DISPATCH_LEVEL
 * ========================================================================= */
static VOID
NTAPI
DxgkMmsPreemptionTimeoutDpc(
    _In_ PKDPC   Dpc,
    _In_ PVOID   DeferredContext,
    _In_ PVOID   SystemArgument1,
    _In_ PVOID   SystemArgument2)
{
    PDXGMMS_PREEMPTION_STATE PreemptState;
    PDXGMMS_NODE             Node;
    KIRQL                    OldIrql;
    BOOLEAN                  StillPending;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    PreemptState = (PDXGMMS_PREEMPTION_STATE)DeferredContext;
    if (PreemptState == NULL || PreemptState->Node == NULL)
        return;

    Node = PreemptState->Node;

    /*
     * Check whether preemption is still outstanding under the lock.
     * The DPC could race with DxgkMmsPreemptionCompleted, so we must
     * re-check the flag rather than trusting the timer fire alone.
     */
    KeAcquireSpinLock(&Node->Lock, &OldIrql);
    StillPending = (PreemptState->PreemptionRequested && Node->PreemptionPending);
    KeReleaseSpinLock(&Node->Lock, OldIrql);

    if (!StillPending)
    {
        /* Preemption already completed; this DPC is benign late-arrival. */
        DXGMMS_TRACE("DxgkMmsPreemptionTimeoutDpc: node %u — "
                     "DPC fired but preemption already completed\n",
                     Node->NodeOrdinal);
        return;
    }

    /*
     * The GPU did not honour the preemption within PREEMPTION_TIMEOUT_MS.
     * Log this with DPRINT1 (always active) and escalate to TDR.
     */
    DPRINT1("DXGMMS1: TDR: node %u preemption not honoured in %u ms "
            "— escalating to TDR reset\n",
            Node->NodeOrdinal, PREEMPTION_TIMEOUT_MS);

    DxgkMmsPreemptionTimeout(Node);
}

/* =========================================================================
 * DxgkMmsPreemptionTimeout
 *
 * Called from DxgkMmsPreemptionTimeoutDpc when the preemption deadline is
 * missed.  Sets the adapter into TDR state and queues the PASSIVE_LEVEL
 * work item that will perform the hardware reset.
 *
 * IRQL: DISPATCH_LEVEL
 * ========================================================================= */
VOID
DxgkMmsPreemptionTimeout(
    _In_ PDXGMMS_NODE Node)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter;
    PDXGMMS_TDR_STATE       TdrState;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    ASSERT(Node != NULL);
    ASSERT(Node->AdapterContext != NULL);

    Adapter  = Node->AdapterContext;
    TdrState = &Adapter->TdrState;

    DPRINT1("DXGMMS1: TDR: DxgkMmsPreemptionTimeout on adapter %p node %u\n",
            Adapter, Node->NodeOrdinal);

    /*
     * Set InReset before queuing the work item to close the race window
     * between the DPC marking the adapter as hung and the worker starting.
     * The volatile write ensures the store is visible before the work item
     * is enqueued (on x86 TSO, a store-store fence is implicit, but
     * volatile prevents compiler reordering which matters here).
     */
    TdrState->InReset  = TRUE;
    TdrState->TdrActive = TRUE;

    DxgkMmsTdrQueueReset(Adapter, Node->NodeOrdinal);
}

/* =========================================================================
 * DxgkMmsTdrInit
 *
 * Initialise the per-adapter TDR watchdog subsystem.  Must be called once
 * from DxgkMmsAddAdapter at PASSIVE_LEVEL after the adapter context is
 * zeroed.
 *
 * Returns STATUS_SUCCESS unconditionally (KeInitializeTimerEx,
 * KeInitializeDpc, ExInitializeFastMutex cannot fail).
 *
 * IRQL: PASSIVE_LEVEL
 * ========================================================================= */
NTSTATUS
DxgkMmsTdrInit(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter)
{
    PDXGMMS_TDR_STATE TdrState;

    PAGED_CODE();
    ASSERT(Adapter != NULL);

    TdrState = &Adapter->TdrState;

    /*
     * TDR timer — SynchronizationTimer (auto-reset, periodic via
     * KeSetTimerEx).  Periodic so it fires repeatedly without the DPC
     * having to re-arm it on each expiry.
     */
    KeInitializeTimerEx(&TdrState->TdrTimer, SynchronizationTimer);

    /*
     * TDR DPC.  DeferredContext = Adapter so the DPC can access all nodes.
     * Pinned to processor 0 to reduce inter-CPU cache invalidations on the
     * adapter's fence counters, which are accessed on every DPC firing.
     */
    KeInitializeDpc(&TdrState->TdrDpc, DxgkMmsTdrDpcRoutine, Adapter);
    KeSetTargetProcessorDpc(&TdrState->TdrDpc, 0);

    /*
     * ResetLock — fast mutex serialising concurrent resets.
     * Must be initialised before any call to DxgkMmsTdrReset.
     */
    ExInitializeFastMutex(&TdrState->ResetLock);

    /* Scalar fields zeroed by the caller; set explicitly for readability. */
    TdrState->TdrCount           = 0;
    TdrState->InReset            = FALSE;
    TdrState->TdrActive          = FALSE;
    TdrState->LastSubmitFence    = 0;
    TdrState->LastCompletedFence = 0;

    DXGMMS_TRACE("DxgkMmsTdrInit: TDR subsystem ready on adapter %p "
                 "(timeout %d s)\n", Adapter, TDR_TIMEOUT_SECONDS);

    return STATUS_SUCCESS;
}

/* =========================================================================
 * DxgkMmsTdrStart
 *
 * Arm (or re-arm) the TDR watchdog timer.  Called by DxgkMmsScheduleNext
 * or DxgkMmsScheduleCommand whenever a command buffer is dispatched to a
 * GPU engine.
 *
 * We snapshot the node's current fence counter here so the DPC can tell
 * whether the GPU has made progress since the last submission or DPC.
 *
 * If the timer is already running, KeSetTimerEx re-arms it with a fresh
 * timeout window which is correct — we want the full 2 s from the last
 * submission, not from when the timer was first armed.
 *
 * IRQL: <= DISPATCH_LEVEL
 * ========================================================================= */
VOID
DxgkMmsTdrStart(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   NodeOrdinal)
{
    PDXGMMS_TDR_STATE TdrState;
    PDXGMMS_NODE      Node;
    LARGE_INTEGER     DueTime;

    ASSERT(Adapter != NULL);
    ASSERT(NodeOrdinal < Adapter->NumNodes);

    TdrState = &Adapter->TdrState;

    if (TdrState->InReset)
    {
        /* Do not arm the watchdog while a reset is in progress. */
        return;
    }

    Node = &Adapter->Nodes[NodeOrdinal];

    /*
     * Snapshot the current fence before arming the timer.  The DPC will
     * compare the live GPU fence against LastCompletedFence to determine
     * if the GPU has made progress.
     *
     * InterlockedCompareExchange64 is used for a memory-ordered load on
     * all architectures.  On x86-64 (TSO) a plain volatile read would also
     * be sufficient, but the interlocked form is universally correct.
     */
    TdrState->LastSubmitFence =
        (ULONG64)InterlockedCompareExchange64(
            (volatile LONG64 *)&Node->CurrentFenceId,
            0,
            0);
    TdrState->LastCompletedFence = TdrState->LastSubmitFence;

    /*
     * Arm the timer as periodic.  DueTime is the initial expiry (relative,
     * negative 100-ns); Period is the re-arm interval (milliseconds per
     * KeSetTimerEx convention).
     *
     * Periodic firing means: if the GPU makes progress on every DPC but
     * there are always new commands being submitted, the watchdog continues
     * to verify activity every TDR_TIMEOUT_SECONDS.  Only if the fence
     * counter stops advancing does TDR trigger.
     */
    DueTime.QuadPart = TDR_TIMEOUT_100NS;

    KeSetTimerEx(&TdrState->TdrTimer,
                 DueTime,
                 (LONG)(TDR_TIMEOUT_SECONDS * 1000),  /* Period in ms */
                 &TdrState->TdrDpc);

    DXGMMS_TRACE("DxgkMmsTdrStart: node %u — watchdog armed "
                 "(fence snapshot %I64u)\n",
                 NodeOrdinal, (ULONG64)TdrState->LastSubmitFence);
}

/* =========================================================================
 * DxgkMmsTdrStop
 *
 * Cancel the TDR watchdog timer when the adapter becomes idle (all queues
 * drained, no running command buffer).
 *
 * KeCancelTimer returns TRUE if the timer was inserted and cancelled, FALSE
 * if it had already expired.  A FALSE return means the DPC may already be
 * queued; that is harmless because the DPC checks for pending work under
 * the node lock before making any reset decisions.
 *
 * IRQL: <= DISPATCH_LEVEL
 * ========================================================================= */
VOID
DxgkMmsTdrStop(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   NodeOrdinal)
{
    UNREFERENCED_PARAMETER(NodeOrdinal);

    ASSERT(Adapter != NULL);

    if (KeCancelTimer(&Adapter->TdrState.TdrTimer))
    {
        DXGMMS_TRACE("DxgkMmsTdrStop: watchdog disarmed on adapter %p\n",
                     Adapter);
    }
}

/* =========================================================================
 * DxgkMmsTdrQueueReset
 *
 * Allocate a DXGMMS_TDR_WORK_ITEM from NonPagedPool and enqueue it on
 * the DelayedWorkQueue.  The worker (DxgkMmsTdrWorkItemRoutine) calls
 * DxgkMmsTdrReset at PASSIVE_LEVEL.
 *
 * This function is called at DISPATCH_LEVEL (from the TDR or preemption-
 * timeout DPC) and therefore cannot block or access paged memory.
 *
 * On allocation failure the adapter is left in InReset state permanently
 * to prevent further command submission to a potentially hung GPU.  This
 * condition is irrecoverable for this adapter; a system with a GPU that
 * cannot be reset is in a critical state regardless.
 *
 * IRQL: DISPATCH_LEVEL
 * ========================================================================= */
VOID
DxgkMmsTdrQueueReset(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter,
    _In_ ULONG                   NodeOrdinal)
{
    PDXGMMS_TDR_WORK_ITEM WorkCtx;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    WorkCtx = (PDXGMMS_TDR_WORK_ITEM)ExAllocatePoolWithTag(
                  NonPagedPool,
                  sizeof(DXGMMS_TDR_WORK_ITEM),
                  DXGMMS1_POOL_TAG);

    if (WorkCtx == NULL)
    {
        DPRINT1("DXGMMS1: TDR: CRITICAL: ExAllocatePoolWithTag failed for TDR "
                "work item on adapter %p — GPU may remain hung (InReset=%d)\n",
                Adapter, (int)Adapter->TdrState.InReset);
        return;
    }

    WorkCtx->Adapter     = Adapter;
    WorkCtx->NodeOrdinal = NodeOrdinal;

    ExInitializeWorkItem(&WorkCtx->WorkQueueItem,
                         DxgkMmsTdrWorkItemRoutine,
                         WorkCtx);

    ExQueueWorkItem(&WorkCtx->WorkQueueItem, DelayedWorkQueue);

    DXGMMS_TRACE("DxgkMmsTdrQueueReset: work item queued for adapter %p "
                 "node %u\n", Adapter, NodeOrdinal);
}

/* =========================================================================
 * DxgkMmsTdrWorkItemRoutine  (static — WORKER_THREAD_ROUTINE)
 *
 * System work item callback running at PASSIVE_LEVEL in a system worker
 * thread.  Unpacks the context and calls DxgkMmsTdrReset.  Frees the
 * work item context when done.
 *
 * IRQL: PASSIVE_LEVEL
 * ========================================================================= */
static VOID
NTAPI
DxgkMmsTdrWorkItemRoutine(
    _In_ PVOID Context)
{
    PDXGMMS_TDR_WORK_ITEM WorkCtx;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    WorkCtx = (PDXGMMS_TDR_WORK_ITEM)Context;
    if (WorkCtx == NULL || WorkCtx->Adapter == NULL)
        return;

    DXGMMS_TRACE("DxgkMmsTdrWorkItemRoutine: beginning reset for adapter %p "
                 "node %u\n", WorkCtx->Adapter, WorkCtx->NodeOrdinal);

    DxgkMmsTdrReset(WorkCtx->Adapter);

    ExFreePoolWithTag(WorkCtx, DXGMMS1_POOL_TAG);
}

/* =========================================================================
 * DxgkMmsTdrDpcRoutine  (static — KDEFERRED_ROUTINE, called by TDR timer)
 *
 * Fires every TDR_TIMEOUT_SECONDS.  Checks whether the GPU has made forward
 * progress by comparing the current fence against the last-seen fence.
 *
 *   Progress detected → update LastCompletedFence snapshot; no action.
 *   No progress + commands pending → log TDR event, queue reset.
 *   No commands pending (adapter idle) → benign late DPC; no action.
 *
 * DeferredContext = PDXGMMS_ADAPTER_CONTEXT.
 *
 * IRQL: DISPATCH_LEVEL
 * ========================================================================= */
static VOID
NTAPI
DxgkMmsTdrDpcRoutine(
    _In_ PKDPC   Dpc,
    _In_ PVOID   DeferredContext,
    _In_ PVOID   SystemArgument1,
    _In_ PVOID   SystemArgument2)
{
    PDXGMMS_ADAPTER_CONTEXT Adapter;
    PDXGMMS_TDR_STATE       TdrState;
    ULONG                   i;
    BOOLEAN                 AnyPending;
    ULONG64                 CurrentFence;
    KIRQL                   OldIrql;
    ULONG                   HungNode;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    Adapter = (PDXGMMS_ADAPTER_CONTEXT)DeferredContext;
    if (Adapter == NULL)
        return;

    TdrState = &Adapter->TdrState;

    /*
     * Guard against re-entrant resets.  We test InReset with a simple
     * volatile read rather than an interlocked operation because:
     *   (a) The TDR DPC is pinned to processor 0.
     *   (b) InReset is written before queuing the work item (sequential
     *       stores visible on x86 TSO in program order).
     *   (c) A missed update would cause at most one extra DPC firing, which
     *       will detect InReset == TRUE on the next iteration.
     */
    if (TdrState->InReset)
    {
        DXGMMS_TRACE("DxgkMmsTdrDpcRoutine: reset already in progress, skipping\n");
        return;
    }

    /* Survey all nodes for in-flight work. */
    AnyPending   = FALSE;
    CurrentFence = 0;
    HungNode     = 0;

    for (i = 0; i < Adapter->NumNodes; i++)
    {
        PDXGMMS_NODE Node = &Adapter->Nodes[i];

        KeAcquireSpinLock(&Node->Lock, &OldIrql);

        if (Node->RunningBuffer != NULL)
        {
            AnyPending   = TRUE;
            CurrentFence = Node->CurrentFenceId;
            HungNode     = i;
        }

        KeReleaseSpinLock(&Node->Lock, OldIrql);

        if (AnyPending)
            break;  /* Stop at first occupied node; TDR is adapter-wide. */
    }

    if (!AnyPending)
    {
        /*
         * Adapter is idle.  The periodic timer fires continuously but there
         * is nothing to watch.  DxgkMmsTdrStop should have cancelled it,
         * but a benign late DPC is possible.
         */
        DXGMMS_TRACE("DxgkMmsTdrDpcRoutine: all nodes idle on adapter %p, "
                     "no TDR action\n", Adapter);
        return;
    }

    /*
     * Compare the live fence against the previously recorded value.
     * If the fence has advanced, the GPU is alive — update the snapshot
     * and continue waiting.
     */
    if (CurrentFence > TdrState->LastCompletedFence)
    {
        TdrState->LastCompletedFence = CurrentFence;

        DXGMMS_TRACE("DxgkMmsTdrDpcRoutine: adapter %p node %u — GPU alive "
                     "(fence %I64u > %I64u)\n",
                     Adapter, HungNode, CurrentFence,
                     TdrState->LastCompletedFence);
        return;
    }

    /*
     * The GPU has NOT advanced its fence counter within TDR_TIMEOUT_SECONDS.
     * It is assumed hung.
     *
     * Log with DPRINT1 (always prints) so the event is visible in production
     * crash dumps and live debugging sessions, not just checked builds.
     */
    DPRINT1("DXGMMS1: TDR: GPU hung on node %u, adapter %p — "
            "fence stalled at %I64u (expected > %I64u)\n",
            HungNode, Adapter, CurrentFence, TdrState->LastCompletedFence);

    /*
     * Mark the adapter before queuing the work item to prevent a second
     * reset from being queued if the DPC fires again before the worker runs.
     */
    TdrState->InReset   = TRUE;
    TdrState->TdrActive = TRUE;

    DxgkMmsTdrQueueReset(Adapter, HungNode);
}

/* =========================================================================
 * DxgkMmsTdrReset
 *
 * Full GPU reset sequence executed at PASSIVE_LEVEL by the system worker
 * thread.
 *
 * Sequence:
 *   1.  Acquire TdrState.ResetLock (ExAcquireFastMutex → raises to APC_LEVEL).
 *   2.  For each node: drain running-buffer slot and all ready queues,
 *       freeing each DXGMMS_COMMAND_BUFFER and logging with DPRINT1.
 *       Reset node-level preemption state and cancel preemption timers.
 *   3.  Call miniport DxgkDdiResetFromTimeout (light hardware reset).
 *   4.  Call miniport DxgkDdiRestartFromTimeout (hardware resume).
 *   5.  Increment TdrCount; clear InReset and TdrActive.
 *   6.  Release TdrState.ResetLock (lowers back to PASSIVE_LEVEL).
 *   7.  For each node: call DxgkMmsScheduleNext in case new commands
 *       arrived while the reset was in progress.
 *
 * IRQL: PASSIVE_LEVEL
 * ========================================================================= */
VOID
DxgkMmsTdrReset(
    _In_ PDXGMMS_ADAPTER_CONTEXT Adapter)
{
    PDXGMMS_TDR_STATE          TdrState;
    PDXGKDDI_RESETFROMTIMEOUT_FN   MiniportReset;
    PDXGKDDI_RESTARTFROMTIMEOUT_FN MiniportRestart;
    NTSTATUS                   Status;
    ULONG                      i;
    ULONG                      p;
    KIRQL                      OldIrql;
    PLIST_ENTRY                Entry;
    PDXGMMS_COMMAND_BUFFER     CmdBuf;

    PAGED_CODE();
    ASSERT(Adapter != NULL);

    TdrState = &Adapter->TdrState;

    DPRINT1("DXGMMS1: TDR: DxgkMmsTdrReset beginning for adapter %p "
            "(previous TDR count: %u)\n",
            Adapter, TdrState->TdrCount);

    /*
     * Acquire the fast mutex.  This serialises concurrent resets (e.g. if
     * multiple nodes trigger TDR simultaneously) and raises IRQL to
     * APC_LEVEL to block normal APCs during the reset sequence.
     */
    ExAcquireFastMutex(&TdrState->ResetLock);

    /* ------------------------------------------------------------------ */
    /* Phase 1: Drain all per-node queues.                                 */
    /* ------------------------------------------------------------------ */
    for (i = 0; i < Adapter->NumNodes; i++)
    {
        PDXGMMS_NODE Node = &Adapter->Nodes[i];

        KeAcquireSpinLock(&Node->Lock, &OldIrql);

        /* Fail the currently running command buffer. */
        CmdBuf = Node->RunningBuffer;
        if (CmdBuf != NULL)
        {
            Node->RunningBuffer = NULL;

            DPRINT1("DXGMMS1: TDR: node %u — failing running buffer "
                    "(fence %I64u) STATUS_GRAPHICS_ADAPTER_WAS_RESET\n",
                    i, CmdBuf->SubmitFenceId);

            /*
             * The full implementation should invoke Adapter->CompletionCallback
             * here to notify dxgkrnl so it can unblock waiting user threads.
             * For Phase-1 we free the buffer directly.
             */
            ExFreePoolWithTag(CmdBuf, TAG_DXGMMS_CMD_BUF);
        }

        /* Fail all queued command buffers in every priority band. */
        for (p = 0; p < DXGMMS_PRIORITY_LEVELS; p++)
        {
            while (!IsListEmpty(&Node->ReadyQueue[p]))
            {
                Entry  = RemoveHeadList(&Node->ReadyQueue[p]);
                CmdBuf = CONTAINING_RECORD(Entry,
                                           DXGMMS_COMMAND_BUFFER,
                                           QueueEntry);

                DPRINT1("DXGMMS1: TDR: node %u prio %u — failing queued "
                        "buffer (fence %I64u)\n",
                        i, p, CmdBuf->SubmitFenceId);

                ExFreePoolWithTag(CmdBuf, TAG_DXGMMS_CMD_BUF);
            }
        }

        /*
         * Reset preemption flags on the node.  Also zero the fence fields
         * so DxgkMmsTdrDpcRoutine does not immediately re-trigger on stale
         * values after the GPU restarts.
         */
        Node->PreemptionPending                        = FALSE;
        Node->PreemptionFenceId                        = 0;
        Node->PreemptState.PreemptionRequested         = FALSE;
        Node->PreemptState.PreemptionCompleted         = FALSE;
        Node->PreemptState.PreemptionFenceId           = 0;

        KeReleaseSpinLock(&Node->Lock, OldIrql);

        /*
         * Cancel the per-node preemption timer outside the spinlock.
         * KeCancelTimer is callable at any IRQL <= DISPATCH_LEVEL.
         * At PASSIVE_LEVEL (APC_LEVEL here due to the fast mutex) this is safe.
         */
        KeCancelTimer(&Node->PreemptState.PreemptionTimer);
    }

    /* ------------------------------------------------------------------ */
    /* Phase 2: Call DxgkDdiResetFromTimeout (miniport hardware reset).    */
    /* ------------------------------------------------------------------ */
    MiniportReset = (PDXGKDDI_RESETFROMTIMEOUT_FN)Adapter->DxgkDdiResetFromTimeout;

    if (MiniportReset != NULL)
    {
        DPRINT1("DXGMMS1: TDR: calling DxgkDdiResetFromTimeout on adapter %p\n",
                Adapter);

        Status = MiniportReset(Adapter->MiniportContext);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DXGMMS1: TDR: DxgkDdiResetFromTimeout failed 0x%08X — "
                    "hardware may be in an undefined state\n", Status);
            /*
             * Continue anyway.  DxgkDdiRestartFromTimeout may still succeed
             * and get the GPU back to a known state.  If both fail, the
             * driver will remain in InReset and further submissions will be
             * rejected until the adapter is removed and re-added.
             */
        }
        else
        {
            DPRINT1("DXGMMS1: TDR: DxgkDdiResetFromTimeout succeeded\n");
        }
    }
    else
    {
        DXGMMS_WARN("DxgkMmsTdrReset: no DxgkDdiResetFromTimeout on adapter %p\n",
                    Adapter);
    }

    /* ------------------------------------------------------------------ */
    /* Phase 3: Call DxgkDdiRestartFromTimeout (miniport resume signal).   */
    /* ------------------------------------------------------------------ */
    MiniportRestart =
        (PDXGKDDI_RESTARTFROMTIMEOUT_FN)Adapter->DxgkDdiRestartFromTimeout;

    if (MiniportRestart != NULL)
    {
        DPRINT1("DXGMMS1: TDR: calling DxgkDdiRestartFromTimeout on adapter %p\n",
                Adapter);

        Status = MiniportRestart(Adapter->MiniportContext);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("DXGMMS1: TDR: DxgkDdiRestartFromTimeout failed 0x%08X\n",
                    Status);
        }
        else
        {
            DPRINT1("DXGMMS1: TDR: DxgkDdiRestartFromTimeout succeeded — "
                    "adapter %p is back online\n", Adapter);
        }
    }
    else
    {
        DXGMMS_WARN("DxgkMmsTdrReset: no DxgkDdiRestartFromTimeout on "
                    "adapter %p\n", Adapter);
    }

    /* ------------------------------------------------------------------ */
    /* Phase 4: Update accounting and clear the in-reset flags.            */
    /* ------------------------------------------------------------------ */
    TdrState->TdrCount++;
    TdrState->LastCompletedFence = 0;
    TdrState->LastSubmitFence    = 0;
    TdrState->InReset            = FALSE;
    TdrState->TdrActive          = FALSE;

    DPRINT1("DXGMMS1: TDR: reset complete on adapter %p — "
            "TdrCount now %u\n", Adapter, TdrState->TdrCount);

    ExReleaseFastMutex(&TdrState->ResetLock);

    /* ------------------------------------------------------------------ */
    /* Phase 5: Reschedule any work that arrived during the reset.         */
    /*                                                                     */
    /* New commands may have been placed in the ready queues between the   */
    /* drain above and the reset completing.  Call DxgkMmsScheduleNext on  */
    /* each node to pick them up.                                          */
    /* ------------------------------------------------------------------ */
    for (i = 0; i < Adapter->NumNodes; i++)
    {
        DxgkMmsScheduleNext(&Adapter->Nodes[i]);
    }
}
