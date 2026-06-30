/*
 * PROJECT:     ReactOS DirectX Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU command-buffer scheduler — public interface exported to the
 *              rest of dxgmms1 and to dxgkrnl via the DXGMMS_INTERFACE table.
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * WDDM scheduling model overview
 * --------------------------------
 * Each physical GPU exposes one or more "engines" (nodes).  The canonical
 * assignments used by WDDM 1.x hardware are:
 *
 *   Node 0 — 3D / graphics engine
 *   Node 1 — Video decode engine
 *   Node 2 — Video encode / overlay engine
 *   Node 3 — Copy / BLT engine
 *   Node 4+ — Compute engines (optional)
 *
 * Applications submit command buffers (DMA buffers) to a specific node
 * through dxgkrnl.  dxgmms1 arbitrates access to each node across
 * multiple simultaneously-active GPU contexts using a priority-aware,
 * preemptive round-robin policy:
 *
 *   - Four priority bands: background (0), normal (1), high (2),
 *     real-time (3).  Video/audio work typically runs at priority 3.
 *   - Each context receives a 16 ms quantum.  When the quantum expires
 *     and higher-priority work is waiting, the running buffer is
 *     preempted via DxgkDdiPreemptCommand.
 *   - The miniport raises a GPU interrupt on DMA buffer completion;
 *     dxgkrnl calls DxgkMmsCommandCompleted from its DPC handler.
 *
 * Locking discipline
 * -------------------
 * All per-node state is protected by DXGMMS_NODE::Lock (a KSPIN_LOCK).
 * Callers that already hold the node lock must use the *AtDpcLevel
 * variants of spinlock operations.  The quantum DPC acquires the lock
 * before inspecting or mutating node state.
 *
 * The global adapter list is protected by DxgkMmsAdapterListLock.
 *
 * IRQL contracts
 * ---------------
 *   DxgkMmsAddAdapter          PASSIVE_LEVEL
 *   DxgkMmsRemoveAdapter        PASSIVE_LEVEL
 *   DxgkMmsScheduleCommand      <= DISPATCH_LEVEL  (acquires spinlock)
 *   DxgkMmsScheduleNext         DISPATCH_LEVEL     (called with lock held or
 *                                                   lock not yet held — takes it)
 *   DxgkMmsQuantumExpired       DISPATCH_LEVEL     (DPC routine)
 *   DxgkMmsCommandCompleted     DISPATCH_LEVEL     (called from dxgkrnl DPC)
 *   DxgkMmsQueryLastCompletedFence  <= DISPATCH_LEVEL
 */

#ifndef _DXGMMS1_SCHEDULER_H_
#define _DXGMMS1_SCHEDULER_H_

#include "dxgmms1_private.h"

/* =========================================================================
 * Constants
 * ========================================================================= */

/*
 * Maximum number of GPU engines (nodes) per adapter.  WDDM 1.x hardware
 * rarely exposes more than four, but we reserve space for eight to cover
 * future extensions without requiring a recompile.
 */
#define DXGMMS_MAX_NODES        8

/*
 * Default scheduling quantum in milliseconds.  16 ms corresponds to one
 * 60 Hz video frame interval, which is the conventional WDDM choice.
 * Expressed as a negative 100-ns interval for KeSetTimer:
 *   -16 ms * 10,000 (ms -> 100ns) = -160,000
 */
#define DXGMMS_QUANTUM_MS       16
#define DXGMMS_QUANTUM_100NS    (-1LL * DXGMMS_QUANTUM_MS * 10000LL)

/*
 * Number of priority levels.  These map to WDDM D3DDDI_ALLOCATIONPRIORITY
 * bands collapsed into four scheduler buckets.
 */
#define DXGMMS_PRIORITY_LEVELS  4

/* Priority level symbolic names — used as indices into ReadyQueue[]. */
#define DXGMMS_PRIORITY_BACKGROUND  0   /* idle / background transfers */
#define DXGMMS_PRIORITY_NORMAL      1   /* default application rendering */
#define DXGMMS_PRIORITY_HIGH        2   /* DWM compositor / foreground    */
#define DXGMMS_PRIORITY_REALTIME    3   /* video decode / audio sync      */

/* Pool tag for DXGMMS_COMMAND_BUFFER allocations ('BmdG' → GdmB). */
#define TAG_DXGMMS_CMD_BUF      'BmdG'

/* Pool tag for DXGMMS_ADAPTER allocations ('AdmG' → GmdA). */
#define TAG_DXGMMS_ADAPTER      'AdmG'

/* =========================================================================
 * Structures
 * ========================================================================= */

/*
 * DXGMMS_COMMAND_BUFFER
 *
 * Represents a single DMA command buffer submitted by dxgkrnl on behalf
 * of a user-mode GPU context.  One instance is allocated per
 * DxgkMmsScheduleCommand call and freed in DxgkMmsCommandCompleted.
 *
 * All fields other than QueueEntry are write-once at allocation time and
 * read-only thereafter (no lock needed for reads after insertion into
 * the ready queue, except IsPreempted/IsCompleted which are mutated
 * under Node->Lock).
 */
typedef struct _DXGMMS_COMMAND_BUFFER
{
    /*
     * Intrusive list link.  When queued in a ready queue this entry is
     * owned by DXGMMS_NODE::ReadyQueue[Priority]; when running it is
     * referenced by DXGMMS_NODE::RunningBuffer (not on any list).
     */
    LIST_ENTRY          QueueEntry;

    /*
     * Opaque back-pointer to the dxgkrnl-side DMA buffer descriptor.
     * dxgmms1 treats this as a cookie — it is passed verbatim to the
     * miniport's DxgkDdiSubmitCommand.
     */
    PVOID               DxgkrnlBuffer;

    /* Which GPU engine this buffer targets (0..NumNodes-1). */
    ULONG               NodeOrdinal;

    /*
     * Scheduling priority: 0=background, 1=normal, 2=high, 3=realtime.
     * Must be < DXGMMS_PRIORITY_LEVELS.
     */
    ULONG               Priority;

    /*
     * Monotonically increasing fence identifier assigned by dxgkrnl at
     * submission time.  The GPU writes this value into a fence location
     * when the buffer completes; the ISR reads it back and reports it
     * via DxgkCbNotifyInterrupt.
     */
    ULONG64             SubmitFenceId;

    /*
     * Set to TRUE under Node->Lock when DxgkDdiPreemptCommand has been
     * issued for this buffer.  The buffer is re-queued at the head of
     * its priority queue on preemption completion.
     */
    BOOLEAN             IsPreempted;

    /*
     * Set to TRUE under Node->Lock when the GPU interrupt reports
     * CompletedFenceId >= SubmitFenceId.
     */
    BOOLEAN             IsCompleted;

} DXGMMS_COMMAND_BUFFER, *PDXGMMS_COMMAND_BUFFER;

/* Forward-declare DXGMMS_ADAPTER so DXGMMS_NODE can hold a pointer. */
typedef struct _DXGMMS_ADAPTER  DXGMMS_ADAPTER;
typedef struct _DXGMMS_ADAPTER *PDXGMMS_ADAPTER;

/*
 * DXGMMS_NODE
 *
 * Per-engine scheduling state for one GPU node.  Each adapter contains
 * NumNodes instances of this structure embedded in DXGMMS_ADAPTER::Nodes[].
 *
 * Accessed at DISPATCH_LEVEL; all mutations require Lock to be held.
 */
typedef struct _DXGMMS_NODE
{
    /* Index of this node (0 = 3D engine, etc.). */
    ULONG               NodeOrdinal;

    /*
     * Protects all mutable fields below.  Always acquired before any
     * ReadyQueue manipulation or RunningBuffer update.
     */
    KSPIN_LOCK          Lock;

    /*
     * Four singly-linked work queues, one per priority band.
     * Index 0 = DXGMMS_PRIORITY_BACKGROUND ... index 3 = REALTIME.
     * Within each band buffers are dispatched FIFO.
     */
    LIST_ENTRY          ReadyQueue[DXGMMS_PRIORITY_LEVELS];

    /*
     * Pointer to the command buffer currently executing on the GPU
     * (NULL when the node is idle).  Protected by Lock.
     */
    PDXGMMS_COMMAND_BUFFER RunningBuffer;

    /*
     * Most recently completed GPU fence identifier for this node.
     * Updated in DxgkMmsCommandCompleted under Lock.
     */
    ULONG64             CurrentFenceId;

    /*
     * Fence identifier used in the most recent preemption request.
     * Incremented atomically (still under Lock) each time a preemption
     * is issued so the GPU can report back which preemption completed.
     */
    ULONG64             LastPreemptedFence;

    /*
     * DPC and one-shot timer used to implement the scheduling quantum.
     * On quantum expiry, DxgkMmsQuantumExpired fires as a DPC.
     */
    KDPC                QuantumDpc;
    KTIMER              QuantumTimer;

    /*
     * Set to TRUE (under Lock) when a preemption has been submitted to
     * hardware but the completion interrupt has not yet been received.
     * Prevents issuing a second overlapping preemption request.
     */
    BOOLEAN             PreemptionPending;

    /* Pointer back to the owning adapter. */
    PDXGMMS_ADAPTER     Adapter;

} DXGMMS_NODE, *PDXGMMS_NODE;

/*
 * DXGMMS_ADAPTER
 *
 * Per-GPU scheduler state.  One instance is allocated for each adapter
 * registered via DxgkMmsAddAdapter.  The pointer returned in *OutMmsContext
 * is cast to PDXGMMS_ADAPTER by all subsequent calls.
 */
struct _DXGMMS_ADAPTER
{
    /*
     * Link into DxgkMmsAdapterListHead.  Protected by
     * DxgkMmsAdapterListLock.
     */
    LIST_ENTRY          AdapterEntry;

    /*
     * Opaque handle that dxgkrnl passed to DxgkMmsAddAdapter.  Stored
     * for use as the DeviceHandle in DxgkCbNotifyDpc / DxgkCbNotifyInterrupt
     * callbacks back to dxgkrnl.
     */
    PVOID               DxgkrnlAdapterContext;

    /*
     * The miniport's per-adapter context pointer, also supplied by
     * dxgkrnl.  Passed as the first argument to all DxgkDdi* miniport
     * callbacks.
     */
    PVOID               MiniportContext;

    /* Number of valid entries in Nodes[]. */
    ULONG               NumNodes;

    /* Per-engine scheduling state, indexed by NodeOrdinal. */
    DXGMMS_NODE         Nodes[DXGMMS_MAX_NODES];

    /*
     * Miniport DDI callbacks installed by dxgkrnl at AddAdapter time.
     * Stored as PVOID and cast to the correct function-pointer type
     * at the call site to avoid including dispmprt.h in this header.
     *
     * DxgkDdiSubmitCommand  — submit a DMA buffer to GPU HW
     * DxgkDdiPreemptCommand — request hardware preemption of running buffer
     * DxgkDdiQueryCurrentFence — poll current fence counter from HW
     */
    PVOID               DxgkDdiSubmitCommand;
    PVOID               DxgkDdiPreemptCommand;
    PVOID               DxgkDdiQueryCurrentFence;

    /*
     * Completion callback provided by dxgkrnl.  Called (with dxgkrnl's
     * context) once a command buffer has finished so that dxgkrnl can
     * retire the buffer and unblock waiting user threads.
     */
    PVOID               CompletionCallback;
    PVOID               CompletionCallbackContext;
};

/* =========================================================================
 * Global scheduler state (defined in scheduler.c)
 * ========================================================================= */

extern KSPIN_LOCK  DxgkMmsAdapterListLock;
extern LIST_ENTRY  DxgkMmsAdapterListHead;

/* =========================================================================
 * Public scheduler entry points
 * ========================================================================= */

/*
 * DxgkMmsSchedulerInit
 *
 * One-time initialisation of the scheduler's global state (list head,
 * spinlock).  Called from DriverEntry / DxgkMmsRegister before any
 * adapter is added.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
DxgkMmsSchedulerInit(VOID);

/*
 * DxgkMmsAddAdapter
 *
 * Called by dxgkrnl when a new GPU adapter is started.  Allocates and
 * initialises a DXGMMS_ADAPTER with NumNodes nodes, then registers it
 * in the global adapter list.
 *
 * Parameters
 *   DxgkrnlContext  — opaque dxgkrnl handle (stored in Adapter->DxgkrnlAdapterContext)
 *   NumNodes        — number of GPU engines reported by the miniport
 *   NumSegments     — number of memory segments (reserved, not used by scheduler)
 *   OutMmsContext   — receives the allocated DXGMMS_ADAPTER pointer
 *
 * Returns STATUS_SUCCESS or a failure NTSTATUS.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkMmsAddAdapter(
    _In_  PVOID  DxgkrnlContext,
    _In_  ULONG  NumNodes,
    _In_  ULONG  NumSegments,
    _Out_ PVOID *OutMmsContext);

/*
 * DxgkMmsRemoveAdapter
 *
 * Tears down scheduling state for an adapter being removed.  Cancels
 * all quantum timers, drains queues, and frees the DXGMMS_ADAPTER.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
DxgkMmsRemoveAdapter(
    _In_ PVOID MmsContext);

/*
 * DxgkMmsScheduleCommand
 *
 * Enqueues a command buffer for execution on the GPU node indicated by
 * the Priority and NodeOrdinal fields embedded in CommandBuffer.  If the
 * target node is idle the buffer is dispatched immediately.
 *
 * CommandBuffer must be a pointer to a caller-allocated structure that
 * contains at minimum the fields copied into DXGMMS_COMMAND_BUFFER; the
 * caller's descriptor is not referenced after this call returns.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
NTSTATUS
DxgkMmsScheduleCommand(
    _In_ PVOID AdapterContext,
    _In_ PVOID CommandBuffer);

/*
 * DxgkMmsCommandCompleted
 *
 * Notification from dxgkrnl's DPC handler that the GPU has completed the
 * DMA buffer identified by CompletedFenceId on node NodeOrdinal.  Updates
 * fence tracking, frees the command buffer, and schedules the next buffer.
 *
 * IRQL: DISPATCH_LEVEL
 */
VOID
DxgkMmsCommandCompleted(
    _In_ PVOID  MmsContext,
    _In_ ULONG  NodeOrdinal,
    _In_ ULONG64 CompletedFenceId);

/*
 * DxgkMmsQueryLastCompletedFence
 *
 * Returns the most recently completed GPU fence identifier for the
 * specified node.  Thread-safe; internally acquires the node spinlock.
 *
 * IRQL: <= DISPATCH_LEVEL
 */
ULONG64
DxgkMmsQueryLastCompletedFence(
    _In_ PVOID MmsContext,
    _In_ ULONG NodeOrdinal);

#endif /* _DXGMMS1_SCHEDULER_H_ */
