/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video Scheduler (VidSch) — GPU engine state machine and
 *              DMA command lifecycle management
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * This file implements the Video Scheduler subsystem of dxgkrnl.sys.
 * VidSch is responsible for:
 *
 *   1. Managing per-GPU-engine run queues of DMA command packets.
 *   2. Submitting command buffers to the miniport via DxgkDdiSubmitCommand.
 *   3. Tracking command completion through GPU interrupts and fences.
 *   4. Processing completions in DPC context and retiring finished packets.
 *   5. Providing idle-wait capability for adapter teardown.
 *
 * On Windows 8.1 this is implemented as the separate dxgmms1.sys binary
 * (~400 KB, ~1228 functions).  This ReactOS implementation provides the
 * Phase 1 foundation: engine state machine, basic command submission,
 * interrupt/DPC completion, and fence tracking.
 *
 * Architecture notes (amd64/x86):
 *
 *   - Engine state transitions use InterlockedCompareExchange which maps
 *     to LOCK CMPXCHG on x86/amd64 — a fully serializing instruction that
 *     provides the required acquire/release semantics for the state machine.
 *
 *   - The QueueLock spinlock is acquired at DISPATCH_LEVEL from both the
 *     thread submission path and the DPC completion path.  It is NOT
 *     acquired from ISR context; the ISR path only touches the atomic
 *     LastCompletedFence via InterlockedExchange and queues the DPC.
 *
 *   - Fence IDs are monotonically increasing per-engine ULONG values.
 *     Comparison uses signed subtraction to handle wrap-around correctly
 *     (same pattern as TCP sequence number comparison).
 *
 * Pool tags:
 *   'VScD' — scheduler context, engine array, DMA packets  (TAG_VIDSCH)
 */

#include "dxgkrnl_private.h"
#include "vidsch.h"
#include "present.h"
#include "debug.h"

#define NDEBUG
#include <debug.h>

#define VIDSCH_SUSPEND_TIMEOUT_MS 5000

/* ========================================================================
 * Internal helpers
 * ====================================================================== */

/*
 * VidSchpFenceReached — signed fence comparison for wrap-around safety.
 *
 * Returns TRUE if CompletedFence >= TargetFence in the signed-distance
 * sense.  This correctly handles the 32-bit wrap case as long as the
 * distance between any two live fence IDs is less than 2^31.
 */
FORCEINLINE BOOLEAN
VidSchpFenceReached(
    _In_ LONG CompletedFence,
    _In_ LONG TargetFence)
{
    return ((CompletedFence - TargetFence) >= 0);
}

static BOOLEAN
VidSchpIsValidTransition(
    _In_ VIDSCH_ENGINE_STATE OldState,
    _In_ VIDSCH_ENGINE_STATE NewState)
{
    if (OldState == NewState)
        return TRUE;

    if (NewState == VidSchEngineResetting)
        return (OldState >= VidSchEngineIdle && OldState < VidSchEngineStateCount && OldState != VidSchEngineResetComplete);

    switch (OldState)
    {
        case VidSchEngineIdle:
            return (NewState == VidSchEngineBudgetComputed || NewState == VidSchEngineSubmitting || NewState == VidSchEngineSuspended || NewState == VidSchEngineFlipPending || NewState == VidSchEngineError);
        case VidSchEngineBudgetComputed:
            return (NewState == VidSchEngineIdle || NewState == VidSchEngineSubmitting || NewState == VidSchEngineSuspended || NewState == VidSchEngineError);
        case VidSchEngineSubmitting:
            return (NewState == VidSchEngineRunning || NewState == VidSchEngineIdle || NewState == VidSchEngineError);
        case VidSchEngineRunning:
            return (NewState == VidSchEngineCompleting || NewState == VidSchEnginePreempting || NewState == VidSchEngineIdle || NewState == VidSchEngineError);
        case VidSchEnginePreempting:
            return (NewState == VidSchEnginePreempted || NewState == VidSchEngineRunning || NewState == VidSchEngineError);
        case VidSchEnginePreempted:
            return (NewState == VidSchEngineIdle || NewState == VidSchEngineSubmitting || NewState == VidSchEngineSuspended || NewState == VidSchEngineError);
        case VidSchEngineResetting:
            return (NewState == VidSchEngineResetComplete || NewState == VidSchEngineError);
        case VidSchEngineResetComplete:
            return (NewState == VidSchEngineIdle || NewState == VidSchEngineSuspended || NewState == VidSchEngineError);
        case VidSchEngineSuspended:
            return (NewState == VidSchEngineResuming || NewState == VidSchEngineError);
        case VidSchEngineResuming:
            return (NewState == VidSchEngineIdle || NewState == VidSchEngineSuspended || NewState == VidSchEngineError);
        case VidSchEngineFlipPending:
            return (NewState == VidSchEngineFlipExecuting || NewState == VidSchEngineIdle || NewState == VidSchEngineError);
        case VidSchEngineFlipExecuting:
            return (NewState == VidSchEngineCompleting || NewState == VidSchEngineIdle || NewState == VidSchEngineError);
        case VidSchEngineCompleting:
            return (NewState == VidSchEngineIdle || NewState == VidSchEngineRunning || NewState == VidSchEngineError);
        case VidSchEngineError:
            return FALSE;
        default:
            return FALSE;
    }
}

/*
 * VidSchpTryTransition — attempt a validated atomic engine transition.
 *
 * Returns TRUE if the transition succeeded (old state matched Expected).
 * On failure, *ActualState receives the current state value.
 */
FORCEINLINE BOOLEAN
VidSchpTryTransition(
    _Inout_ volatile LONG *StateField,
    _In_    LONG            Expected,
    _In_    LONG            New,
    _Out_opt_ LONG         *ActualState)
{
    LONG Old;

    if (!VidSchpIsValidTransition((VIDSCH_ENGINE_STATE)Expected, (VIDSCH_ENGINE_STATE)New))
    {
        if (ActualState != NULL)
            *ActualState = InterlockedCompareExchange(StateField, 0, 0);
        return FALSE;
    }

    Old = InterlockedCompareExchange(StateField, New, Expected);

    if (ActualState != NULL)
        *ActualState = Old;

    return (Old == Expected);
}

/*
 * VidSchpReadState — read the current engine state without modifying it.
 */
FORCEINLINE VIDSCH_ENGINE_STATE
VidSchpReadState(
    _In_ PVIDSCH_ENGINE Engine)
{
    return (VIDSCH_ENGINE_STATE)InterlockedCompareExchange(&Engine->State, 0, 0);
}

FORCEINLINE VIDSCH_SCHEDULER_STATE
VidSchpReadSchedulerState(
    _In_ PVIDSCH_CONTEXT Context)
{
    return (VIDSCH_SCHEDULER_STATE)InterlockedCompareExchange(&Context->LifecycleState, 0, 0);
}

static BOOLEAN
VidSchpAcquireCall(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || InterlockedCompareExchange(&Adapter->VidSchStopping, 0, 0) != 0)
        return FALSE;
    InterlockedIncrement(&Adapter->VidSchActiveCalls);
    if (InterlockedCompareExchange(&Adapter->VidSchStopping, 0, 0) != 0)
    {
        InterlockedDecrement(&Adapter->VidSchActiveCalls);
        return FALSE;
    }
    return TRUE;
}

static VOID
VidSchpReferenceActiveCall(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    InterlockedIncrement(&Adapter->VidSchActiveCalls);
}

static VOID
VidSchpReleaseCall(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG ActiveCalls = InterlockedDecrement(&Adapter->VidSchActiveCalls);

    ASSERT(ActiveCalls >= 0);
}

static VOID
VidSchpWaitForCalls(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LARGE_INTEGER Delay;

    Delay.QuadPart = -10000;
    while (InterlockedCompareExchange(&Adapter->VidSchActiveCalls, 0, 0) != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
}

static ULONG
VidSchpFirstEngineOrdinal(
    _In_ UINT EngineAffinity)
{
    ULONG EngineOrdinal;

    if (EngineAffinity == 0)
        return 0;
    for (EngineOrdinal = 0; EngineOrdinal < 32; ++EngineOrdinal)
    {
        if ((EngineAffinity & (1u << EngineOrdinal)) != 0)
            return EngineOrdinal;
    }
    return 0;
}

/* Forward declaration. */
static VOID VidSchpKickEngine(_In_ PVIDSCH_ENGINE Engine);

typedef struct _VIDSCH_VIRTUAL_SUBMIT_WORK
{
    WORK_QUEUE_ITEM WorkItem;
    PVIDSCH_ENGINE Engine;
} VIDSCH_VIRTUAL_SUBMIT_WORK, *PVIDSCH_VIRTUAL_SUBMIT_WORK;

static VOID NTAPI VidSchpVirtualSubmitWorker(_In_ PVOID Parameter);
static VOID NTAPI VidSchpPacketCleanupWorker(_In_ PVOID Parameter);

static VOID
VidSchpDereferencePacket(
    _In_ PVIDSCH_DMA_PACKET Packet)
{
    if (InterlockedDecrement(&Packet->ReferenceCount) != 0)
        return;

    if (Packet->TrackerReservation != NULL)
        DxgkCancelTrackedDmaBuffer(Packet->TrackerReservation);
    if (Packet->DmaBuffer != NULL)
        DxgkFreeDmaBuffer(Packet->DmaBuffer);
    if (Packet->OwnedDriverPrivateData != NULL)
        ExFreePoolWithTag(Packet->OwnedDriverPrivateData, TAG_VIDSCH);
    if (Packet->VirtualSubmitWorkItem != NULL)
        ExFreePoolWithTag(Packet->VirtualSubmitWorkItem, TAG_VIDSCH);
    if (Packet->HoldsContextReference)
        DxgkDereferenceContext((PDXGKRNL_CONTEXT)Packet->Context);
    ExFreePoolWithTag(Packet, TAG_VIDSCH);
}

static VOID
NTAPI
VidSchpPacketCleanupWorker(
    _In_ PVOID Parameter)
{
    PVIDSCH_DMA_PACKET Packet = (PVIDSCH_DMA_PACKET)Parameter;
    PVIDSCH_ENGINE Engine = Packet->OwnerEngine;

    VidSchpDereferencePacket(Packet);
    if (InterlockedDecrement(&Engine->OutstandingWorkers) == 0)
        KeSetEvent(&Engine->WorkersDrainedEvent, IO_NO_INCREMENT, FALSE);
    VidSchpReleaseCall(Engine->Adapter);
}

/* ========================================================================
 * Completion DPC callback
 *
 * Queued by VidSchNotifyInterrupt from ISR context.  Runs at
 * DISPATCH_LEVEL and processes completed commands.
 * ====================================================================== */
static KDEFERRED_ROUTINE VidSchpCompletionDpcRoutine;

static VOID
NTAPI
VidSchpCompletionDpcRoutine(
    _In_     PKDPC  Dpc,
    _In_opt_ PVOID  DeferredContext,
    _In_opt_ PVOID  SystemArgument1,
    _In_opt_ PVOID  SystemArgument2)
{
    PVIDSCH_ENGINE Engine = (PVIDSCH_ENGINE)DeferredContext;
    LONG Completed;
    KIRQL OldIrql;
    LIST_ENTRY RetireList;
    PLIST_ENTRY Link;
    BOOLEAN PreemptionInterrupt;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Engine == NULL)
        return;

    Completed = Engine->LastCompletedFence;
    PreemptionInterrupt = (InterlockedExchange(&Engine->PreemptionInterruptPending, 0) != 0);
    InitializeListHead(&RetireList);

    /*
     * Under QueueLock, walk the run queue from head and retire all packets
     * whose fence ID has been reached by the hardware.
     */
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);

    while (!IsListEmpty(&Engine->RunQueueHead))
    {
        PVIDSCH_DMA_PACKET Packet;

        Link = Engine->RunQueueHead.Flink;
        Packet = CONTAINING_RECORD(Link, VIDSCH_DMA_PACKET, RunQueueEntry);

        /* Never retire un-kicked work: its DMA has not executed, whatever
         * the fence threshold says. */
        if (!Packet->Kicked ||
            !VidSchpFenceReached(Completed, (LONG)Packet->SubmissionFenceId))
            break;

        RemoveEntryList(Link);
        Engine->PendingPacketCount--;
        InsertTailList(&RetireList, Link);
    }

    /*
     * Transition engine state: if the run queue is now empty and the engine
     * was RUNNING, move to COMPLETING then IDLE.  If packets remain, the
     * engine stays RUNNING.
     */
    if (IsListEmpty(&Engine->RunQueueHead))
    {
        VidSchpTryTransition(&Engine->State, (LONG)VidSchEnginePreempted, (LONG)VidSchEngineIdle, NULL);
        /* Try RUNNING -> COMPLETING -> IDLE */
        if (VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineRunning, (LONG)VidSchEngineCompleting, NULL))
        {
            VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineCompleting, (LONG)VidSchEngineIdle, NULL);
        }
    }
    else
    {
        /*
         * Packets remain.  If everything submitted so far has retired
         * (head is an unkicked packet), the engine is no longer
         * executing anything: force RUNNING -> IDLE so the kick below can
         * take the IDLE -> SUBMITTING transition.  Without this the
         * engine wedges in RUNNING forever once a completion retires the
         * last in-flight packet while unkicked work sits queued.
         */
        PVIDSCH_DMA_PACKET HeadPkt = CONTAINING_RECORD(Engine->RunQueueHead.Flink, VIDSCH_DMA_PACKET, RunQueueEntry);

        if (!HeadPkt->Kicked)
        {
            VidSchpTryTransition(&Engine->State, (LONG)VidSchEnginePreempted, (LONG)VidSchEngineIdle, NULL);
            VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineRunning, (LONG)VidSchEngineIdle, NULL);
        }

        /* A DMA_PREEMPTED notification does not complete the preempted head.
         * Leave it parked for TDR reset instead of submitting the same DMA
         * descriptor twice. If all submitted work retired, the next unkicked
         * packet is safe to dispatch normally. */
        if (!PreemptionInterrupt || !HeadPkt->Kicked)
            VidSchpKickEngine(Engine);
    }
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

    /* Signal completion event so VidSchWaitForIdle can wake up. */
    KeSetEvent(&Engine->CompletionEvent, IO_NO_INCREMENT, FALSE);

    /* Free retired packets outside the spinlock. */
    while (!IsListEmpty(&RetireList))
    {
        PVIDSCH_DMA_PACKET Packet;

        Link = RemoveHeadList(&RetireList);
        Packet = CONTAINING_RECORD(Link, VIDSCH_DMA_PACKET, RunQueueEntry);

        VidSchpDereferencePacket(Packet);
    }
    VidSchpReleaseCall(Engine->Adapter);
}

/* ========================================================================
 * Per-engine timer placeholder
 *
 * The adapter watchdog owns timeout detection and queues PASSIVE_LEVEL
 * recovery. This DPC must not start a second reset in parallel.
 * ====================================================================== */
static KDEFERRED_ROUTINE VidSchpTdrDpcRoutine;

static VOID
NTAPI
VidSchpTdrDpcRoutine(
    _In_     PKDPC  Dpc,
    _In_opt_ PVOID  DeferredContext,
    _In_opt_ PVOID  SystemArgument1,
    _In_opt_ PVOID  SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    /* Intentionally empty: adapter.c owns the single TDR watchdog. */
}

static VOID
NTAPI
VidSchpVirtualSubmitWorker(
    _In_ PVOID Parameter)
{
    PVIDSCH_VIRTUAL_SUBMIT_WORK Work = (PVIDSCH_VIRTUAL_SUBMIT_WORK)Parameter;
    PVIDSCH_ENGINE Engine = Work->Engine;
    PDXGKRNL_ADAPTER Adapter = Engine->Adapter;
    PVIDSCH_DMA_PACKET Packet = NULL;
    DXGKARG_SUBMITCOMMANDVIRTUAL SubmitArgs;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_DEVICE_NOT_READY;
    BOOLEAN Removed = FALSE;

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (!IsListEmpty(&Engine->RunQueueHead))
    {
        Packet = CONTAINING_RECORD(Engine->RunQueueHead.Flink, VIDSCH_DMA_PACKET, RunQueueEntry);
        if (!Packet->VirtualAddressing || VidSchpReadState(Engine) != VidSchEngineSubmitting)
            Packet = NULL;
    }

    if (Packet != NULL)
    {
        InterlockedIncrement(&Packet->ReferenceCount);
        Packet->Kicked = TRUE;
        Engine->LastSubmittedFence = (LONG)Packet->SubmissionFenceId;
        VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineSubmitting, (LONG)VidSchEngineRunning, NULL);
        RtlZeroMemory(&SubmitArgs, sizeof(SubmitArgs));
        SubmitArgs.hContext = ((PDXGKRNL_CONTEXT)Packet->Context)->hMiniportContext;
        SubmitArgs.DmaBufferVirtualAddress = Packet->DmaBufferGpuVa;
        SubmitArgs.DmaBufferSize = Packet->VirtualDmaBufferSize;
        SubmitArgs.pDmaBufferPrivateData = Packet->DriverPrivateData;
        SubmitArgs.DmaBufferPrivateDataSize = Packet->DriverPrivateDataSize;
        SubmitArgs.DmaBufferUmdPrivateDataSize = Packet->DriverPrivateDataSize;
        SubmitArgs.SubmissionFenceId = Packet->SubmissionFenceId;
        SubmitArgs.VidPnSourceId = 0;
        SubmitArgs.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
        SubmitArgs.Flags.Value = 0;
        SubmitArgs.Flags.NullRendering = Packet->SubmitFlags != 0;
        SubmitArgs.EngineOrdinal = Packet->EngineOrdinal;
        SubmitArgs.NodeOrdinal = Packet->NodeOrdinal;
    }
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

    if (Packet != NULL && DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual) != NULL)
        Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual)(Adapter->MiniportDeviceContext, &SubmitArgs);

    if (Packet != NULL && !NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("VidSch: DxgkDdiSubmitCommandVirtual failed 0x%08lX engine=%lu fence=%lu\n", Status, Engine->EngineOrdinal, Packet->SubmissionFenceId);
        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        if (!IsListEmpty(&Engine->RunQueueHead) && Engine->RunQueueHead.Flink == &Packet->RunQueueEntry)
        {
            RemoveEntryList(&Packet->RunQueueEntry);
            Engine->PendingPacketCount--;
            Removed = TRUE;
        }
        VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineRunning, (LONG)VidSchEngineIdle, NULL);
        VidSchpKickEngine(Engine);
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        if (Removed)
            VidSchpDereferencePacket(Packet);
    }

    if (Packet != NULL)
        VidSchpDereferencePacket(Packet);
    ExFreePoolWithTag(Work, TAG_VIDSCH);
    if (InterlockedDecrement(&Engine->OutstandingWorkers) == 0)
        KeSetEvent(&Engine->WorkersDrainedEvent, IO_NO_INCREMENT, FALSE);
    VidSchpReleaseCall(Adapter);
}

/* ========================================================================
 * VidSchpKickEngine — submit the next queued packet to the miniport
 *
 * MUST be called with Engine->QueueLock held at DISPATCH_LEVEL.
 * Transitions the engine from IDLE to SUBMITTING to RUNNING.
 * ====================================================================== */
static VOID
VidSchpKickEngine(
    _In_ PVIDSCH_ENGINE Engine)
{
    PVIDSCH_DMA_PACKET Packet;
    PLIST_ENTRY Link;
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;
    DXGKARG_SUBMITCOMMAND SubmitArgs;

    if (IsListEmpty(&Engine->RunQueueHead))
        return;

    if (Engine->Scheduler == NULL || (VidSchpReadSchedulerState(Engine->Scheduler) != VidSchSchedulerRunning && VidSchpReadSchedulerState(Engine->Scheduler) != VidSchSchedulerSuspending))
        return;

    Adapter = Engine->Adapter;
    if (Adapter == NULL)
        return;

    /*
     * Only kick if the engine is IDLE.  If it's already RUNNING or in
     * another active state, the completion DPC will re-kick when the
     * current command finishes.
     */
    if (!VidSchpTryTransition(&Engine->State,
                              (LONG)VidSchEngineIdle,
                              (LONG)VidSchEngineSubmitting,
                              NULL))
    {
        return;
    }

    /* Peek the head packet (don't dequeue — it stays until completion). */
    Link = Engine->RunQueueHead.Flink;
    Packet = CONTAINING_RECORD(Link, VIDSCH_DMA_PACKET, RunQueueEntry);

    if (Packet->VirtualAddressing)
    {
        PVIDSCH_VIRTUAL_SUBMIT_WORK Work = (PVIDSCH_VIRTUAL_SUBMIT_WORK)Packet->VirtualSubmitWorkItem;

        if (Work == NULL)
        {
            VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineSubmitting, (LONG)VidSchEngineError, NULL);
            return;
        }

        Packet->VirtualSubmitWorkItem = NULL;
        VidSchpReferenceActiveCall(Adapter);
        if (InterlockedIncrement(&Engine->OutstandingWorkers) == 1)
            KeClearEvent(&Engine->WorkersDrainedEvent);
        ExQueueWorkItem(&Work->WorkItem, DelayedWorkQueue);
        return;
    }

    if (Packet->SubmissionFenceId == 0)
        Packet->SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
    Packet->Kicked = TRUE;

    Engine->LastSubmittedFence = (LONG)Packet->SubmissionFenceId;

    /*
     * Build the DXGKARG_SUBMITCOMMAND for the miniport.
     * The miniport reads the DMA buffer and programs the GPU to execute it.
     */
    RtlZeroMemory(&SubmitArgs, sizeof(SubmitArgs));
    if (Adapter->SchedulingCaps.MultiEngineAware)
        SubmitArgs.hContext = Packet->MiniportContextHandle;
    else
        SubmitArgs.hDevice = Packet->MiniportDeviceHandle;
    if (Packet->DmaBuffer != NULL)
    {
        SubmitArgs.DmaBufferSegmentId = Packet->DmaBuffer->SegmentId;
        SubmitArgs.DmaBufferPhysicalAddress = Packet->DmaBuffer->SegmentAddress;
        SubmitArgs.DmaBufferSize = Packet->DmaBuffer->Capacity;
        SubmitArgs.DmaBufferSubmissionStartOffset = Packet->DmaBuffer->SubmissionStartOffset;
        SubmitArgs.DmaBufferSubmissionEndOffset = Packet->DmaBuffer->SubmissionEndOffset;
    }
    SubmitArgs.pDmaBufferPrivateData = Packet->DriverPrivateData;
    SubmitArgs.DmaBufferPrivateDataSize = Packet->DriverPrivateDataSize;
    SubmitArgs.DmaBufferPrivateDataSubmissionStartOffset = 0;
    SubmitArgs.DmaBufferPrivateDataSubmissionEndOffset = Packet->DriverPrivateDataSize;
    SubmitArgs.SubmissionFenceId = Packet->SubmissionFenceId;
    SubmitArgs.VidPnSourceId = Packet->VidPnSourceId;
    SubmitArgs.FlipInterval = D3DDDI_FLIPINTERVAL_IMMEDIATE;
    SubmitArgs.NodeOrdinal = Packet->NodeOrdinal;
    SubmitArgs.EngineOrdinal = Packet->EngineOrdinal;
    SubmitArgs.Flags.Value = Packet->SubmitFlags;
    if (Packet->IsPresent)
        SubmitArgs.Flags.Present = 1;

    /*
     * Call the miniport's DxgkDdiSubmitCommand.
     * This is a full-WDDM-only callback — DOD adapters don't have it.
     */
    Status = STATUS_NOT_SUPPORTED;
    if (DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand) != NULL)
        Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand)(Adapter->MiniportDeviceContext, &SubmitArgs);

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("VidSch: DxgkDdiSubmitCommand failed 0x%08lX engine=%lu fence=%lu\n", Status, Packet->NodeOrdinal, Packet->SubmissionFenceId);
        RemoveEntryList(&Packet->RunQueueEntry);
        Engine->PendingPacketCount--;
        /* Packet cleanup runs at PASSIVE_LEVEL and cancels the adopted
         * tracker reservation, including its miniport allocation handles. */
        VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineSubmitting, (LONG)VidSchEngineIdle, NULL);
        VidSchpReferenceActiveCall(Adapter);
        if (InterlockedIncrement(&Engine->OutstandingWorkers) == 1)
            KeClearEvent(&Engine->WorkersDrainedEvent);
        ExQueueWorkItem(&Packet->CleanupWorkItem, DelayedWorkQueue);
        VidSchpKickEngine(Engine);
        return;
    }

    if (Packet->TrackerReservation != NULL)
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Reservation = Packet->TrackerReservation;

        Packet->TrackerReservation = NULL;
        Packet->DmaBuffer = NULL;
        DxgkCommitTrackedDmaBuffer(Adapter, Reservation);
    }

    /* Transition SUBMITTING -> RUNNING. */
    VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineSubmitting, (LONG)VidSchEngineRunning, NULL);
}

/* ========================================================================
 * Public interface implementation
 * ====================================================================== */

/*
 * VidSchInitialize
 */
NTSTATUS
VidSchInitialize(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    ULONG i;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    /*
     * DOD (Display-Only Driver) adapters have no GPU engines and do not
     * need a scheduler.  NodeCount will be 0 for these adapters.
     */
    if (Adapter->NodeCount == 0)
    {
        DXGKRNL_TRACE("VidSch: no GPU nodes — scheduler not needed\n");
        Adapter->VidSchContext = NULL;
        return STATUS_SUCCESS;
    }

    /* Allocate the top-level scheduler context. */
    Ctx = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(VIDSCH_CONTEXT),
                                TAG_VIDSCH);
    if (Ctx == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Ctx, sizeof(*Ctx));
    Ctx->Adapter = Adapter;
    Ctx->EngineCount = Adapter->NodeCount;
    ExInitializeFastMutex(&Ctx->LifecycleMutex);
    Ctx->LifecycleState = (LONG)VidSchSchedulerRunning;
    Ctx->CallbacksEnabled = TRUE;

    /* Allocate the per-engine array. */
    Ctx->Engines = ExAllocatePoolWithTag(
                       NonPagedPool,
                       Ctx->EngineCount * sizeof(VIDSCH_ENGINE),
                       TAG_VIDSCH);
    if (Ctx->Engines == NULL)
    {
        ExFreePoolWithTag(Ctx, TAG_VIDSCH);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Ctx->Engines, Ctx->EngineCount * sizeof(VIDSCH_ENGINE));

    /* Initialize each engine. */
    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];

        Engine->Adapter = Adapter;
        Engine->Scheduler = Ctx;
        Engine->EngineOrdinal = i;
        Engine->State = (LONG)VidSchEngineIdle;
        Engine->NextFenceId = 1;

        KeInitializeSpinLock(&Engine->QueueLock);
        InitializeListHead(&Engine->RunQueueHead);
        KeInitializeEvent(&Engine->CompletionEvent, SynchronizationEvent, FALSE);
        KeInitializeEvent(&Engine->WorkersDrainedEvent, NotificationEvent, TRUE);

        KeInitializeDpc(&Engine->CompletionDpc, VidSchpCompletionDpcRoutine, Engine);

        KeInitializeTimer(&Engine->TdrTimer);
        KeInitializeDpc(&Engine->TdrDpc, VidSchpTdrDpcRoutine, Engine);
    }

    Ctx->Initialized = TRUE;
    Adapter->VidSchContext = Ctx;

    DXGKRNL_TRACE("VidSch: initialized %lu engines for adapter %p\n",
                  Ctx->EngineCount, Adapter);

    return STATUS_SUCCESS;
}

VOID
VidSchPrepareForStop(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    LIST_ENTRY CancelList;
    ULONG i;

    PAGED_CODE();

    if (Adapter == NULL)
        return;

    InterlockedExchange(&Adapter->VidSchStopping, 1);
    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL)
    {
        VidSchpWaitForCalls(Adapter);
        return;
    }

    InitializeListHead(&CancelList);
    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    Ctx->LifecycleState = (LONG)VidSchSchedulerStopping;
    Ctx->Initialized = FALSE;
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    /* Stop every asynchronous producer before examining packet ownership. */
    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];

        KeCancelTimer(&Engine->TdrTimer);
        KeRemoveQueueDpc(&Engine->TdrDpc);
        if (KeRemoveQueueDpc(&Engine->CompletionDpc))
            VidSchpReleaseCall(Adapter);
    }
    KeFlushQueuedDpcs();

    /* Calls which passed the first admission check, queued DPCs, and
     * derived worker references must all finish before engine storage moves. */
    VidSchpWaitForCalls(Adapter);

    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];

        if (InterlockedCompareExchange(&Engine->OutstandingWorkers, 0, 0) != 0)
            KeWaitForSingleObject(&Engine->WorkersDrainedEvent, Executive, KernelMode, FALSE, NULL);
    }

    /* Packets not accepted by the miniport can be cancelled now. Kicked
     * packets stay pinned until StopDevice has quiesced hardware ownership. */
    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];
        PLIST_ENTRY Link;
        KIRQL OldIrql;

        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        Link = Engine->RunQueueHead.Flink;
        while (Link != &Engine->RunQueueHead)
        {
            PVIDSCH_DMA_PACKET Packet = CONTAINING_RECORD(Link, VIDSCH_DMA_PACKET, RunQueueEntry);
            PLIST_ENTRY Next = Link->Flink;

            if (!Packet->Kicked)
            {
                RemoveEntryList(Link);
                InsertTailList(&CancelList, Link);
                Engine->PendingPacketCount--;
            }
            Link = Next;
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    }

    while (!IsListEmpty(&CancelList))
    {
        PVIDSCH_DMA_PACKET Packet = CONTAINING_RECORD(RemoveHeadList(&CancelList), VIDSCH_DMA_PACKET, RunQueueEntry);
        VidSchpDereferencePacket(Packet);
    }
}

/*
 * VidSchDestroy
 */
VOID
VidSchDestroy(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    LIST_ENTRY FreeList;
    ULONG i;

    PAGED_CODE();

    if (Adapter == NULL)
        return;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL)
        return;

    VidSchPrepareForStop(Adapter);
    InitializeListHead(&FreeList);

    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];
        KIRQL OldIrql;

        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        while (!IsListEmpty(&Engine->RunQueueHead))
        {
            PLIST_ENTRY Link = RemoveHeadList(&Engine->RunQueueHead);
            InsertTailList(&FreeList, Link);
            Engine->PendingPacketCount--;
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    }

    while (!IsListEmpty(&FreeList))
    {
        PVIDSCH_DMA_PACKET Packet = CONTAINING_RECORD(RemoveHeadList(&FreeList), VIDSCH_DMA_PACKET, RunQueueEntry);
        VidSchpDereferencePacket(Packet);
    }

    Adapter->VidSchContext = NULL;
    if (Ctx->Engines != NULL)
        ExFreePoolWithTag(Ctx->Engines, TAG_VIDSCH);

    ExFreePoolWithTag(Ctx, TAG_VIDSCH);

    DXGKRNL_TRACE("VidSch: destroyed for adapter %p\n", Adapter);
}

/*
 * Common submit core: validate the scheduler/engine/list sizes and engine
 * state, then allocate a zeroed packet bound to the engine.  Tracked
 * submissions additionally take the run-queue flood guard and stay silent
 * on state rejection (STATUS_DEVICE_BUSY is their UMD backpressure signal,
 * spammed at frame rate; the untracked paths hit it rarely and want the
 * diagnostic).
 */
static NTSTATUS
VidSchpPrepareSubmit(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _In_ ULONG EngineOrdinal,
    _In_ ULONG AllocationListCount,
    _In_ ULONG PatchLocationListCount,
    _In_ BOOLEAN Tracked,
    _Outptr_ PVIDSCH_ENGINE *OutEngine,
    _Outptr_ PVIDSCH_DMA_PACKET *OutPacket)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    PVIDSCH_DMA_PACKET Packet;
    VIDSCH_ENGINE_STATE CurrentState;

    if (Adapter == NULL || Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0)
        return STATUS_DELETE_PENDING;
    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
        return STATUS_DEVICE_NOT_READY;

    if (VidSchpReadSchedulerState(Ctx) != VidSchSchedulerRunning)
        return STATUS_DEVICE_BUSY;

    if (NodeOrdinal >= Ctx->EngineCount)
        return STATUS_INVALID_PARAMETER;

    /* Larger lists would need a pool copy; nothing submits them yet. */
    if (AllocationListCount > RTL_NUMBER_OF(((PVIDSCH_DMA_PACKET)0)->InlineAllocationList) ||
        PatchLocationListCount > RTL_NUMBER_OF(((PVIDSCH_DMA_PACKET)0)->InlinePatchList))
        return STATUS_NOT_SUPPORTED;

    Engine = &Ctx->Engines[NodeOrdinal];

    /*
     * Validate engine state.  IDLE/RUNNING accept and may kick;
     * SUBMITTING/COMPLETING are transient windows of the kick/retire
     * paths — queueing is safe there (the completion DPC re-kicks), and
     * rejecting them would surface spurious STATUS_DEVICE_BUSY races.
     */
    CurrentState = VidSchpReadState(Engine);
    if (CurrentState != VidSchEngineIdle &&
        CurrentState != VidSchEngineRunning &&
        CurrentState != VidSchEngineSubmitting &&
        CurrentState != VidSchEngineCompleting)
    {
        if (!Tracked)
        {
            DXGKRNL_WARN("VidSch: submit rejected — engine %lu in state %d\n", NodeOrdinal, (int)CurrentState);
        }
        return STATUS_DEVICE_BUSY;
    }

    Packet = ExAllocatePoolWithTag(NonPagedPool, sizeof(VIDSCH_DMA_PACKET), TAG_VIDSCH);
    if (Packet == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Packet, sizeof(*Packet));
    Packet->EngineOrdinal = EngineOrdinal;
    Packet->NodeOrdinal = NodeOrdinal;
    Packet->OwnerEngine = Engine;
    Packet->ReferenceCount = 1;
    ExInitializeWorkItem(&Packet->CleanupWorkItem, VidSchpPacketCleanupWorker, Packet);

    *OutEngine = Engine;
    *OutPacket = Packet;
    return STATUS_SUCCESS;
}

/*
 * VidSchSubmitCommand
 */
NTSTATUS
VidSchSubmitCommand(
    _In_  PDXGKRNL_ADAPTER Adapter,
    _In_  ULONG            NodeOrdinal,
    _In_  ULONG            EngineOrdinal,
    _In_  ULONG            SubmissionFenceId,
    _In_opt_ HANDLE        MiniportDeviceHandle,
    _In_opt_ HANDLE        MiniportContextHandle,
    _In_  BOOLEAN          IsPresent,
    _In_  ULONG            SubmitFlags,
    _In_  ULONG            VidPnSourceId,
    _Out_ ULONG           *OutFenceId)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    PVIDSCH_DMA_PACKET Packet;
    KIRQL OldIrql;
    ULONG FenceId;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || OutFenceId == NULL || (SubmitFlags & ~0xffu) != 0 || (Adapter->SchedulingCaps.MultiEngineAware && MiniportContextHandle == NULL) || (!Adapter->SchedulingCaps.MultiEngineAware && MiniportDeviceHandle == NULL))
        return STATUS_INVALID_PARAMETER;

    *OutFenceId = 0;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Status = VidSchpPrepareSubmit(Adapter, NodeOrdinal, EngineOrdinal, 0, 0, FALSE, &Engine, &Packet);
    if (!NT_SUCCESS(Status))
    {
        VidSchpReleaseCall(Adapter);
        return Status;
    }

    /* One adapter-wide fence space, minted in submit order: queue order
     * == fence order == kick order, the invariant threshold retirement
     * and the DMA tracker depend on.  A second counter (or minting at
     * kick) aliases fence numbers across paths and signals UMD waits for
     * work that never executed. */
    if (SubmissionFenceId != 0)
        Packet->SubmissionFenceId = SubmissionFenceId;
    else
        Packet->SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
    Packet->MiniportDeviceHandle = MiniportDeviceHandle;
    Packet->MiniportContextHandle = MiniportContextHandle;
    Packet->IsPresent = IsPresent;
    Packet->SubmitFlags = SubmitFlags;
    Packet->VidPnSourceId = VidPnSourceId;
    FenceId = Packet->SubmissionFenceId;

    /* Enqueue the packet and try to kick the engine. */
    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    if (VidSchpReadSchedulerState(Ctx) != VidSchSchedulerRunning)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (Engine->PendingPacketCount >= VIDSCH_MAX_PENDING_PACKETS)
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    InsertTailList(&Engine->RunQueueHead, &Packet->RunQueueEntry);
    Engine->PendingPacketCount++;
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    VidSchpKickEngine(Engine);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    *OutFenceId = FenceId;
    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

/*
 * Queues a WDDM 2.0 GPU-virtual command. Context arrives with one transient
 * reference; ownership transfers to the packet only on STATUS_SUCCESS.
 */
NTSTATUS
VidSchSubmitCommandVirtual(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_CONTEXT Context,
    _In_ D3DGPU_VIRTUAL_ADDRESS DmaBufferGpuVa,
    _In_ ULONG DmaBufferSize,
    _In_reads_bytes_opt_(DriverPrivateDataSize) PVOID DriverPrivateData,
    _In_ ULONG DriverPrivateDataSize,
    _In_ BOOLEAN NullRendering,
    _Out_ ULONG *OutFenceId)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    PVIDSCH_DMA_PACKET Packet;
    PVIDSCH_VIRTUAL_SUBMIT_WORK Work;
    KIRQL OldIrql;
    ULONG EngineOrdinal;
    ULONG FenceId;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Context == NULL || DmaBufferGpuVa == 0 || DmaBufferSize == 0 || OutFenceId == NULL || (DriverPrivateDataSize != 0 && DriverPrivateData == NULL))
        return STATUS_INVALID_PARAMETER;

    *OutFenceId = 0;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;
    if (!Context->VirtualAddressing)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_PARAMETER;
    }
    EngineOrdinal = VidSchpFirstEngineOrdinal(Context->EngineAffinity);
    Status = VidSchpPrepareSubmit(Adapter, Context->NodeOrdinal, EngineOrdinal, 0, 0, FALSE, &Engine, &Packet);
    if (!NT_SUCCESS(Status))
    {
        VidSchpReleaseCall(Adapter);
        return Status;
    }

    if (DriverPrivateDataSize != 0)
    {
        Packet->OwnedDriverPrivateData = ExAllocatePoolWithTag(NonPagedPool, DriverPrivateDataSize, TAG_VIDSCH);
        if (Packet->OwnedDriverPrivateData == NULL)
        {
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(Packet->OwnedDriverPrivateData, DriverPrivateData, DriverPrivateDataSize);
        Packet->DriverPrivateData = Packet->OwnedDriverPrivateData;
        Packet->DriverPrivateDataSize = DriverPrivateDataSize;
    }

    Work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Work), TAG_VIDSCH);
    if (Work == NULL)
    {
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Work, sizeof(*Work));
    Work->Engine = Engine;
    ExInitializeWorkItem(&Work->WorkItem, VidSchpVirtualSubmitWorker, Work);
    Packet->VirtualSubmitWorkItem = Work;
    Packet->SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
    Packet->DmaBufferGpuVa = DmaBufferGpuVa;
    Packet->VirtualDmaBufferSize = DmaBufferSize;
    Packet->Context = Context;
    Packet->VirtualAddressing = TRUE;
    Packet->SubmitFlags = NullRendering ? 1u : 0u;
    FenceId = Packet->SubmissionFenceId;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    if (VidSchpReadSchedulerState(Ctx) != VidSchSchedulerRunning)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (Engine->PendingPacketCount >= VIDSCH_MAX_PENDING_PACKETS)
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    InsertTailList(&Engine->RunQueueHead, &Packet->RunQueueEntry);
    Engine->PendingPacketCount++;
    Packet->HoldsContextReference = TRUE;
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    ExReleaseFastMutex(&Ctx->LifecycleMutex);
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    VidSchpKickEngine(Engine);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    *OutFenceId = FenceId;
    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

NTSTATUS
VidSchSubmitCommandTracked(
    _In_  PDXGKRNL_ADAPTER Adapter,
    _In_  ULONG            NodeOrdinal,
    _In_  ULONG            EngineOrdinal,
    _In_  PDXGKRNL_DMA_BUFFER DmaBuffer,
    _In_reads_bytes_opt_(DriverPrivateDataSize) CONST VOID *DriverPrivateData,
    _In_  ULONG            DriverPrivateDataSize,
    _In_reads_opt_(AllocationListCount) CONST DXGK_ALLOCATIONLIST *AllocationList,
    _In_  ULONG            AllocationListCount,
    _In_reads_opt_(PatchLocationListCount) CONST D3DDDI_PATCHLOCATIONLIST *PatchLocationList,
    _In_  ULONG            PatchLocationListCount,
    _In_opt_ HANDLE        MiniportDeviceHandle,
    _In_opt_ HANDLE        MiniportContextHandle,
    _In_  LONG             Priority,
    _In_  const DXGKRNL_TRACK_DMA_ARGS *TrackArgs,
    _In_  ULONG            SubmitFlags,
    _In_  ULONG            VidPnSourceId,
    _Out_ ULONG           *OutFenceId)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    PVIDSCH_DMA_PACKET Packet;
    PDXGKRNL_SUBMIT_DMA_BUFFER Reservation = NULL;
    DXGKRNL_TRACK_DMA_ARGS LocalTrackArgs;
    DXGKARG_PATCH PatchArgs;
    KIRQL OldIrql;
    ULONG FenceId;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Priority);

    if (OutFenceId == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutFenceId = 0;
    if (Adapter == NULL || DmaBuffer == NULL || DmaBuffer->VirtualAddress == NULL || DmaBuffer->Capacity == 0 || DmaBuffer->SubmissionStartOffset >= DmaBuffer->SubmissionEndOffset || DmaBuffer->SubmissionEndOffset > DmaBuffer->Capacity || TrackArgs == NULL || (SubmitFlags & ~0xffu) != 0 || (DriverPrivateDataSize != 0 && DriverPrivateData == NULL) || (AllocationListCount != 0 && AllocationList == NULL) || (PatchLocationListCount != 0 && PatchLocationList == NULL) || (Adapter->SchedulingCaps.MultiEngineAware && MiniportContextHandle == NULL) || (!Adapter->SchedulingCaps.MultiEngineAware && MiniportDeviceHandle == NULL))
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Status = VidSchpPrepareSubmit(Adapter, NodeOrdinal, EngineOrdinal, AllocationListCount, PatchLocationListCount, TRUE, &Engine, &Packet);
    if (!NT_SUCCESS(Status))
    {
        VidSchpReleaseCall(Adapter);
        return Status;
    }

    Packet->SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
    if (Packet->SubmissionFenceId == 0)
    {
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_INTEGER_OVERFLOW;
    }
    Packet->MiniportDeviceHandle = MiniportDeviceHandle;
    Packet->MiniportContextHandle = MiniportContextHandle;
    Packet->Priority = Priority;
    Packet->SubmitFlags = SubmitFlags;
    Packet->IsPresent = ((SubmitFlags & 0x2u) != 0);
    Packet->VidPnSourceId = VidPnSourceId;

    if (DriverPrivateDataSize != 0)
    {
        Packet->OwnedDriverPrivateData = ExAllocatePoolWithTag(NonPagedPool, DriverPrivateDataSize, TAG_VIDSCH);
        if (Packet->OwnedDriverPrivateData == NULL)
        {
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(Packet->OwnedDriverPrivateData, DriverPrivateData, DriverPrivateDataSize);
        Packet->DriverPrivateData = Packet->OwnedDriverPrivateData;
        Packet->DriverPrivateDataSize = DriverPrivateDataSize;
    }

    if (AllocationList != NULL && AllocationListCount != 0)
    {
        RtlCopyMemory(Packet->InlineAllocationList, AllocationList, AllocationListCount * sizeof(DXGK_ALLOCATIONLIST));
        Packet->InlineAllocationCount = AllocationListCount;
        Packet->AllocationList = Packet->InlineAllocationList;
        Packet->AllocationListSize = AllocationListCount;
    }
    if (PatchLocationList != NULL && PatchLocationListCount != 0)
    {
        RtlCopyMemory(Packet->InlinePatchList, PatchLocationList, PatchLocationListCount * sizeof(D3DDDI_PATCHLOCATIONLIST));
        Packet->InlinePatchCount = PatchLocationListCount;
    }

    LocalTrackArgs = *TrackArgs;
    LocalTrackArgs.SubmissionFenceId = Packet->SubmissionFenceId;
    LocalTrackArgs.NodeOrdinal = NodeOrdinal;
    LocalTrackArgs.EngineOrdinal = EngineOrdinal;
    LocalTrackArgs.DmaBuffer = DmaBuffer;
    Status = DxgkPrepareTrackedDmaBuffer(Adapter, &LocalTrackArgs, &Reservation);
    if (!NT_SUCCESS(Status))
    {
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }

    if (DXGK_CB_FULL(Adapter, DxgkDdiPatch) != NULL)
    {
        RtlZeroMemory(&PatchArgs, sizeof(PatchArgs));
        if (Adapter->SchedulingCaps.MultiEngineAware)
            PatchArgs.hContext = MiniportContextHandle;
        else
            PatchArgs.hDevice = MiniportDeviceHandle;
        PatchArgs.DmaBufferSegmentId = DmaBuffer->SegmentId;
        PatchArgs.DmaBufferPhysicalAddress = DmaBuffer->SegmentAddress;
        PatchArgs.pDmaBuffer = DmaBuffer->VirtualAddress;
        PatchArgs.DmaBufferSize = DmaBuffer->Capacity;
        PatchArgs.DmaBufferSubmissionStartOffset = DmaBuffer->SubmissionStartOffset;
        PatchArgs.DmaBufferSubmissionEndOffset = DmaBuffer->SubmissionEndOffset;
        PatchArgs.pDmaBufferPrivateData = Packet->DriverPrivateData;
        PatchArgs.DmaBufferPrivateDataSize = Packet->DriverPrivateDataSize;
        PatchArgs.DmaBufferPrivateDataSubmissionStartOffset = 0;
        PatchArgs.DmaBufferPrivateDataSubmissionEndOffset = Packet->DriverPrivateDataSize;
        PatchArgs.pAllocationList = Packet->InlineAllocationList;
        PatchArgs.AllocationListSize = Packet->InlineAllocationCount;
        PatchArgs.pPatchLocationList = Packet->InlinePatchList;
        PatchArgs.PatchLocationListSize = Packet->InlinePatchCount;
        PatchArgs.PatchLocationListSubmissionStart = 0;
        PatchArgs.PatchLocationListSubmissionLength = Packet->InlinePatchCount;
        PatchArgs.SubmissionFenceId = Packet->SubmissionFenceId;
        PatchArgs.Flags.Value = SubmitFlags & 0x0fu;
        PatchArgs.EngineOrdinal = EngineOrdinal;

        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiPatch)(Adapter->MiniportDeviceContext, &PatchArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (!NT_SUCCESS(Status))
        {
            DxgkCancelTrackedDmaBuffer(Reservation);
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return Status;
        }
    }

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    if (VidSchpReadSchedulerState(Ctx) != VidSchSchedulerRunning)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkCancelTrackedDmaBuffer(Reservation);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    FenceId = Packet->SubmissionFenceId;
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (Engine->PendingPacketCount >= VIDSCH_MAX_PENDING_PACKETS)
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkCancelTrackedDmaBuffer(Reservation);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    InsertTailList(&Engine->RunQueueHead, &Packet->RunQueueEntry);
    Engine->PendingPacketCount++;
    Packet->DmaBuffer = DmaBuffer;
    Packet->TrackerReservation = Reservation;
    DxgkAdoptTrackedDmaBuffer(Reservation);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    VidSchpKickEngine(Engine);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    *OutFenceId = FenceId;
    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

/*
 * VidSchNotifyInterrupt
 *
 * Called from ISR context at DIRQL.  Must be minimal and non-blocking.
 */
VOID
VidSchNotifyInterrupt(
    _In_ PDXGKRNL_ADAPTER                      Adapter,
    _In_ CONST DXGKARGCB_NOTIFY_INTERRUPT_DATA *NotifyData)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    ULONG FenceId;
    ULONG NodeOrdinal;

    if (Adapter == NULL || NotifyData == NULL)
        return;
    if (!VidSchpAcquireCall(Adapter))
        return;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
        goto Exit;

    if (NotifyData->InterruptType == DXGK_INTERRUPT_DMA_COMPLETED)
    {
        FenceId       = NotifyData->DmaCompleted.SubmissionFenceId;
        NodeOrdinal = NotifyData->DmaCompleted.NodeOrdinal;
    }
    else if (NotifyData->InterruptType == DXGK_INTERRUPT_DMA_PREEMPTED)
    {
        FenceId       = NotifyData->DmaPreempted.LastCompletedFenceId;
        NodeOrdinal = NotifyData->DmaPreempted.NodeOrdinal;
    }
    else
    {
        /* Other interrupt types are not handled by VidSch Phase 1. */
        goto Exit;
    }

    if (NodeOrdinal >= Ctx->EngineCount)
        goto Exit;

    Engine = &Ctx->Engines[NodeOrdinal];
    if (NotifyData->InterruptType == DXGK_INTERRUPT_DMA_PREEMPTED)
        InterlockedExchange(&Engine->PreemptionInterruptPending, 1);

    /* Update the completed fence — monotonic max, safe at DIRQL. */
    for (;;)
    {
        LONG Current = Engine->LastCompletedFence;

        if (VidSchpFenceReached(Current, (LONG)FenceId))
            break;
        if (InterlockedCompareExchange(&Engine->LastCompletedFence, (LONG)FenceId, Current) == Current)
            break;
    }

    /* Transfer a scheduler rundown reference to every queued DPC. */
    VidSchpReferenceActiveCall(Adapter);
    if (!KeInsertQueueDpc(&Engine->CompletionDpc, NULL, NULL))
        VidSchpReleaseCall(Adapter);
Exit:
    VidSchpReleaseCall(Adapter);
}

/*
 * VidSchNotifyDpc
 *
 * Called from DISPATCH_LEVEL after the miniport's DPC routine.
 * Signals the adapter-wide SyncEvent and lets per-engine DPCs handle
 * the actual retirement.
 */
VOID
VidSchNotifyDpc(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    /* The per-engine CompletionDpc handles actual work. */
    UNREFERENCED_PARAMETER(Adapter);
}

/*
 * VidSchWaitForIdle
 */
NTSTATUS
VidSchWaitForIdle(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG            TimeoutMs)
{
    PVIDSCH_CONTEXT Ctx;
    ULONG i;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    /* Convert milliseconds to 100ns units (negative = relative). */
    Timeout.QuadPart = -(LONGLONG)TimeoutMs * 10000LL;

    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];

        while (VidSchpReadState(Engine) != VidSchEngineIdle ||
               Engine->PendingPacketCount > 0)
        {
            Status = KeWaitForSingleObject(&Engine->CompletionEvent, Executive, KernelMode, FALSE, &Timeout);
            if (Status == STATUS_TIMEOUT)
            {
                DXGKRNL_WARN("VidSch: WaitForIdle timeout on engine %lu "
                             "(state=%d pending=%lu)\n",
                             i, (int)VidSchpReadState(Engine),
                             Engine->PendingPacketCount);
                VidSchpReleaseCall(Adapter);
                return STATUS_TIMEOUT;
            }
        }
    }

    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

/*
 * VidSchQueryEngineStatus
 */
NTSTATUS
VidSchQueryEngineStatus(
    _In_  PDXGKRNL_ADAPTER    Adapter,
    _In_  ULONG                EngineOrdinal,
    _Out_ VIDSCH_ENGINE_STATE *OutState,
    _Out_ ULONG               *OutPendingCount,
    _Out_ ULONG               *OutLastSubmittedFence,
    _Out_ ULONG               *OutLastCompletedFence)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;

    if (Adapter == NULL || OutState == NULL || OutPendingCount == NULL || OutLastSubmittedFence == NULL || OutLastCompletedFence == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_NOT_READY;
    }

    if (EngineOrdinal >= Ctx->EngineCount)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_PARAMETER;
    }

    Engine = &Ctx->Engines[EngineOrdinal];

    *OutState              = VidSchpReadState(Engine);
    *OutPendingCount       = Engine->PendingPacketCount;
    *OutLastSubmittedFence = (ULONG)Engine->LastSubmittedFence;
    *OutLastCompletedFence = (ULONG)Engine->LastCompletedFence;

    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

/*
 * VidSchStartScheduler
 *
 * Scheduling is driven directly by submit and DPC paths, so starting an
 * initialized scheduler means resuming it from a suspended lifecycle state.
 */
NTSTATUS
VidSchStartScheduler(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    BOOLEAN Running;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    Running = (VidSchpReadSchedulerState(Ctx) == VidSchSchedulerRunning);
    VidSchpReleaseCall(Adapter);
    if (Running)
        return STATUS_SUCCESS;

    return VidSchResumeScheduler(Adapter);
}

/*
 * VidSchFlipPresent
 *
 * Queues a flip/present for execution on the specified engine.
 * For IMMEDIATE flips: submits directly through VidSchSubmitCommand
 * with the IsPresent flag set and a NULL DMA buffer (the miniport
 * handles the flip as a special-case present submission).
 * For VSync-synchronized flips: queues through the present queue
 * (present.c) and the VSync DPC will trigger execution.
 */
NTSTATUS
VidSchFlipPresent(
    _In_ PDXGKRNL_ADAPTER                  Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    VidPnSourceId,
    _In_ ULONG                             EngineOrdinal,
    _In_ D3DDDI_FLIPINTERVAL_TYPE          FlipInterval,
    _In_opt_ PVOID                         Context,
    _Out_ ULONG                           *OutFenceId)
{
    PVIDSCH_CONTEXT Ctx;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || OutFenceId == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutFenceId = 0;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        /*
         * No scheduler — fall through to legacy present path.
         * present.c will handle this directly.
         */
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    if (EngineOrdinal >= Ctx->EngineCount)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_PARAMETER;
    }

    if (FlipInterval == D3DDDI_FLIPINTERVAL_IMMEDIATE)
    {
        /*
         * Immediate flip: submit directly to the engine.
         * The miniport recognizes present submissions by the Flags field
         * (IsPresent = TRUE) and the VidPnSourceId.
         */
        Status = VidSchSubmitCommand(Adapter, EngineOrdinal, 0, 0, Adapter->SchedulingCaps.MultiEngineAware ? NULL : (HANDLE)Context, Adapter->SchedulingCaps.MultiEngineAware ? (HANDLE)Context : NULL, TRUE, 0, VidPnSourceId, OutFenceId);

        VidSchpReleaseCall(Adapter);
        return Status;
    }

    /* Only present.c owns an executable, referenced VSync present entry. */
    VidSchpReleaseCall(Adapter);
    return STATUS_NOT_SUPPORTED;
}

/* ========================================================================
 * VidSchGetInterface — fill the interface function pointer table
 *
 * This is the ReactOS equivalent of the dxgmms1.sys VidSchInterface
 * export (ordinal 2).  On Windows, dxgkrnl calls this export during
 * adapter init to receive the function pointer table.  In ReactOS the
 * scheduler is built inline, so this simply wires up the local functions.
 * ====================================================================== */

/*
 * Internal thunk wrappers — adapt the generic PVOID-based interface
 * signatures to the typed internal functions.
 */
static NTSTATUS NTAPI
VidSchpIfInitialize(PVOID Adapter)
{
    return VidSchInitialize((PDXGKRNL_ADAPTER)Adapter);
}

static NTSTATUS NTAPI
VidSchpIfStartScheduler(PVOID Adapter)
{
    return VidSchStartScheduler((PDXGKRNL_ADAPTER)Adapter);
}

static NTSTATUS NTAPI
VidSchpIfSubmitCommand(PVOID Adapter, PVOID SubmitArgs)
{
    UNREFERENCED_PARAMETER(SubmitArgs);
    /* Full integration would unpack SubmitArgs here; Phase 1 uses
     * VidSchSubmitCommand directly from typed callers. */
    UNREFERENCED_PARAMETER(Adapter);
    return STATUS_NOT_IMPLEMENTED;
}

static VOID NTAPI
VidSchpIfNotifyInterrupt(PVOID Adapter, CONST DXGKARGCB_NOTIFY_INTERRUPT_DATA *NotifyData)
{
    VidSchNotifyInterrupt((PDXGKRNL_ADAPTER)Adapter, NotifyData);
}

static VOID NTAPI
VidSchpIfNotifyDpc(PVOID Adapter)
{
    VidSchNotifyDpc((PDXGKRNL_ADAPTER)Adapter);
}

static NTSTATUS NTAPI
VidSchpIfPreemptEngine(PVOID Adapter, ULONG EngineOrdinal)
{
    return VidSchPreemptEngine((PDXGKRNL_ADAPTER)Adapter, EngineOrdinal, 0);
}

static NTSTATUS NTAPI
VidSchpIfSuspendScheduler(PVOID Adapter)
{
    return VidSchSuspendScheduler((PDXGKRNL_ADAPTER)Adapter);
}

static NTSTATUS NTAPI
VidSchpIfResumeScheduler(PVOID Adapter)
{
    return VidSchResumeScheduler((PDXGKRNL_ADAPTER)Adapter);
}

static NTSTATUS NTAPI
VidSchpIfSetEngineState(PVOID Adapter, ULONG EngineOrdinal, LONG NewState)
{
    return VidSchSetEngineState((PDXGKRNL_ADAPTER)Adapter, EngineOrdinal, (VIDSCH_ENGINE_STATE)NewState);
}

static NTSTATUS NTAPI
VidSchpIfResetEngine(PVOID Adapter, ULONG EngineOrdinal)
{
    return VidSchResetEngine((PDXGKRNL_ADAPTER)Adapter, EngineOrdinal);
}

static NTSTATUS NTAPI
VidSchpIfFlipPresent(PVOID Adapter, PVOID FlipArgs)
{
    UNREFERENCED_PARAMETER(FlipArgs);
    UNREFERENCED_PARAMETER(Adapter);
    /* Full integration would unpack FlipArgs; Phase 1 uses
     * VidSchFlipPresent directly from typed callers. */
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS NTAPI
VidSchpIfWaitForIdle(PVOID Adapter, ULONG TimeoutMs)
{
    return VidSchWaitForIdle((PDXGKRNL_ADAPTER)Adapter, TimeoutMs);
}

static NTSTATUS NTAPI
VidSchpIfQueryEngineStatus(PVOID Adapter, ULONG EngineOrdinal, PVOID OutStatus)
{
    UNREFERENCED_PARAMETER(OutStatus);
    UNREFERENCED_PARAMETER(EngineOrdinal);
    UNREFERENCED_PARAMETER(Adapter);
    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS NTAPI
VidSchpIfSetSchedulerCallback(PVOID Adapter, PVOID CallbackContext)
{
    return VidSchSetSchedulerCallback((PDXGKRNL_ADAPTER)Adapter, CallbackContext);
}

static NTSTATUS NTAPI
VidSchpIfGetEngineTdrInfo(PVOID Adapter, ULONG EngineOrdinal, PVOID TdrInfo)
{
    return VidSchGetEngineTdrInfo((PDXGKRNL_ADAPTER)Adapter, EngineOrdinal, TdrInfo);
}

VOID
VidSchGetInterface(
    _Out_ PVIDSCH_INTERFACE Interface)
{
    PAGED_CODE();

    RtlZeroMemory(Interface, sizeof(*Interface));

    Interface->Initialize           = VidSchpIfInitialize;
    Interface->StartScheduler       = VidSchpIfStartScheduler;
    Interface->SubmitCommand        = VidSchpIfSubmitCommand;
    Interface->NotifyInterrupt      = VidSchpIfNotifyInterrupt;
    Interface->NotifyDpc            = VidSchpIfNotifyDpc;
    Interface->PreemptEngine        = VidSchpIfPreemptEngine;
    Interface->SuspendScheduler     = VidSchpIfSuspendScheduler;
    Interface->ResumeScheduler      = VidSchpIfResumeScheduler;
    Interface->SetEngineState       = VidSchpIfSetEngineState;
    Interface->ResetEngine          = VidSchpIfResetEngine;
    Interface->FlipPresent          = VidSchpIfFlipPresent;
    Interface->WaitForIdle          = VidSchpIfWaitForIdle;
    Interface->QueryEngineStatus    = VidSchpIfQueryEngineStatus;
    Interface->SetSchedulerCallback = VidSchpIfSetSchedulerCallback;
    Interface->GetEngineTdrInfo     = VidSchpIfGetEngineTdrInfo;
}

/* ========================================================================
 * Phase 2+ stubs
 * ====================================================================== */

NTSTATUS
VidSchPreemptEngine(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG            NodeOrdinal,
    _In_ ULONG            EngineOrdinal)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    DXGKARG_PREEMPTCOMMAND PreemptArgs;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_NOT_READY;
    }
    if (NodeOrdinal >= Ctx->EngineCount)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_PARAMETER;
    }
    if (DXGK_CB_FULL(Adapter, DxgkDdiPreemptCommand) == NULL)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    Engine = &Ctx->Engines[NodeOrdinal];

    /* Nothing running: nothing to preempt. */
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (!VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineRunning, (LONG)VidSchEnginePreempting, NULL))
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        VidSchpReleaseCall(Adapter);
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&PreemptArgs, sizeof(PreemptArgs));
    PreemptArgs.PreemptionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
    PreemptArgs.NodeOrdinal = NodeOrdinal;
    PreemptArgs.EngineOrdinal = EngineOrdinal;

    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiPreemptCommand)(Adapter->MiniportDeviceContext, &PreemptArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status))
    {
        /* The miniport reports DMA_PREEMPTED; the completion DPC retires
         * finished packets and returns the engine to IDLE (re-kicking any
         * remaining queued work). */
        VidSchpTryTransition(&Engine->State, (LONG)VidSchEnginePreempting, (LONG)VidSchEnginePreempted, NULL);
    }
    else
    {
        VidSchpTryTransition(&Engine->State, (LONG)VidSchEnginePreempting, (LONG)VidSchEngineRunning, NULL);
        DXGKRNL_WARN("VidSch: PreemptCommand failed 0x%08lX node=%lu engine=%lu\n", Status, NodeOrdinal, EngineOrdinal);
    }

    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    VidSchpReleaseCall(Adapter);
    return Status;
}

NTSTATUS
VidSchSuspendScheduler(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    NTSTATUS Status;
    ULONG i;
    ULONG SuspendedCount = 0;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    if (VidSchpReadSchedulerState(Ctx) == VidSchSchedulerSuspended)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_SUCCESS;
    }
    if (VidSchpReadSchedulerState(Ctx) != VidSchSchedulerRunning)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    Ctx->LifecycleState = (LONG)VidSchSchedulerSuspending;
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    Status = VidSchWaitForIdle(Adapter, VIDSCH_SUSPEND_TIMEOUT_MS);
    if (!NT_SUCCESS(Status))
    {
        ExAcquireFastMutex(&Ctx->LifecycleMutex);
        if (VidSchpReadSchedulerState(Ctx) == VidSchSchedulerSuspending)
            Ctx->LifecycleState = (LONG)VidSchSchedulerRunning;
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return Status;
    }

    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    if (VidSchpReadSchedulerState(Ctx) != VidSchSchedulerSuspending)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }

    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];
        KIRQL OldIrql;

        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        if (!IsListEmpty(&Engine->RunQueueHead) || !VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineIdle, (LONG)VidSchEngineSuspended, NULL))
        {
            KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
            break;
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        SuspendedCount++;
    }

    if (SuspendedCount != Ctx->EngineCount)
    {
        while (SuspendedCount != 0)
        {
            PVIDSCH_ENGINE Engine = &Ctx->Engines[--SuspendedCount];

            VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineSuspended, (LONG)VidSchEngineResuming, NULL);
            VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineResuming, (LONG)VidSchEngineIdle, NULL);
        }
        Ctx->LifecycleState = (LONG)VidSchSchedulerRunning;
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }

    Ctx->LifecycleState = (LONG)VidSchSchedulerSuspended;
    ExReleaseFastMutex(&Ctx->LifecycleMutex);
    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

NTSTATUS
VidSchResumeScheduler(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    ULONG i;
    ULONG ResumedCount = 0;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    if (VidSchpReadSchedulerState(Ctx) == VidSchSchedulerRunning)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_SUCCESS;
    }
    if (VidSchpReadSchedulerState(Ctx) != VidSchSchedulerSuspended)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_DEVICE_STATE;
    }

    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];

        if (!VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineSuspended, (LONG)VidSchEngineResuming, NULL))
            break;
        if (!VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineResuming, (LONG)VidSchEngineIdle, NULL))
        {
            VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineResuming, (LONG)VidSchEngineSuspended, NULL);
            break;
        }
        ResumedCount++;
    }

    if (ResumedCount != Ctx->EngineCount)
    {
        while (ResumedCount != 0)
            VidSchpTryTransition(&Ctx->Engines[--ResumedCount].State, (LONG)VidSchEngineIdle, (LONG)VidSchEngineSuspended, NULL);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_DEVICE_STATE;
    }

    Ctx->LifecycleState = (LONG)VidSchSchedulerRunning;
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    for (i = 0; i < Ctx->EngineCount; i++)
    {
        KIRQL OldIrql;

        KeAcquireSpinLock(&Ctx->Engines[i].QueueLock, &OldIrql);
        VidSchpKickEngine(&Ctx->Engines[i]);
        KeReleaseSpinLock(&Ctx->Engines[i].QueueLock, OldIrql);
    }

    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

NTSTATUS
VidSchResetEngine(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG            EngineOrdinal)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    DXGKARG_RESETENGINE ResetArgs;
    VIDSCH_ENGINE_STATE OldState;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }
    if (EngineOrdinal >= Ctx->EngineCount)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_PARAMETER;
    }
    if (DXGK_CB_FULL(Adapter, DxgkDdiResetEngine) == NULL)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    Engine = &Ctx->Engines[EngineOrdinal];
    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    if (VidSchpReadSchedulerState(Ctx) != VidSchSchedulerRunning && VidSchpReadSchedulerState(Ctx) != VidSchSchedulerSuspended)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (!IsListEmpty(&Engine->RunQueueHead) || Engine->PendingPacketCount != 0)
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    OldState = VidSchpReadState(Engine);
    if (!VidSchpTryTransition(&Engine->State, (LONG)OldState, (LONG)VidSchEngineResetting, NULL))
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_DEVICE_STATE;
    }
    KeCancelTimer(&Engine->TdrTimer);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    RtlZeroMemory(&ResetArgs, sizeof(ResetArgs));
    ResetArgs.NodeOrdinal = EngineOrdinal;
    ResetArgs.EngineOrdinal = 0;
    ResetArgs.LastAbortedFenceId = (UINT)Engine->LastCompletedFence;

    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiResetEngine)(Adapter->MiniportDeviceContext, &ResetArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (NT_SUCCESS(Status) && ((LONG)(ResetArgs.LastAbortedFenceId - (ULONG)Engine->LastCompletedFence) < 0 || (LONG)((ULONG)Engine->LastSubmittedFence - ResetArgs.LastAbortedFenceId) < 0))
        Status = STATUS_DEVICE_PROTOCOL_ERROR;

    if (NT_SUCCESS(Status))
    {
        VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineResetting, (LONG)VidSchEngineResetComplete, NULL);
        if (VidSchpReadSchedulerState(Ctx) == VidSchSchedulerSuspended)
            VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineResetComplete, (LONG)VidSchEngineSuspended, NULL);
        else
            VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineResetComplete, (LONG)VidSchEngineIdle, NULL);
    }
    else
    {
        VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineResetting, (LONG)VidSchEngineError, NULL);
    }

    VidSchpReleaseCall(Adapter);
    return Status;
}

NTSTATUS
VidSchSetEngineState(
    _In_ PDXGKRNL_ADAPTER    Adapter,
    _In_ ULONG                EngineOrdinal,
    _In_ VIDSCH_ENGINE_STATE  NewState)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    VIDSCH_ENGINE_STATE OldState;
    NTSTATUS Status;

    if (Adapter == NULL || NewState < VidSchEngineIdle || NewState >= VidSchEngineStateCount)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_NOT_READY;
    }
    if (EngineOrdinal >= Ctx->EngineCount)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_PARAMETER;
    }

    Engine = &Ctx->Engines[EngineOrdinal];
    for (;;)
    {
        OldState = VidSchpReadState(Engine);
        if (OldState == NewState)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        if (!VidSchpIsValidTransition(OldState, NewState))
        {
            Status = STATUS_INVALID_DEVICE_STATE;
            break;
        }
        if (VidSchpTryTransition(&Engine->State, (LONG)OldState, (LONG)NewState, NULL))
        {
            Status = STATUS_SUCCESS;
            break;
        }
    }
    VidSchpReleaseCall(Adapter);
    return Status;
}

NTSTATUS
VidSchSetSchedulerCallback(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PVOID             CallbackContext)
{
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    UNREFERENCED_PARAMETER(CallbackContext);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
VidSchGetSchedulerCallbackState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ PBOOLEAN Enabled)
{
    PVIDSCH_CONTEXT Ctx;

    if (Adapter == NULL || Enabled == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    *Enabled = (InterlockedCompareExchange(&Ctx->CallbacksEnabled, 0, 0) != 0);
    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

NTSTATUS
VidSchSetSchedulerCallbackState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN Enabled)
{
    PVIDSCH_CONTEXT Ctx;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    InterlockedExchange(&Ctx->CallbacksEnabled, Enabled ? TRUE : FALSE);
    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

NTSTATUS
VidSchPrepareAdapterReset(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    VIDSCH_SCHEDULER_STATE SchedulerState;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG i;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    SchedulerState = VidSchpReadSchedulerState(Ctx);
    if (SchedulerState == VidSchSchedulerResetting)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    if (SchedulerState == VidSchSchedulerStopping || SchedulerState == VidSchSchedulerError || SchedulerState == VidSchSchedulerUninitialized)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_DEVICE_STATE;
    }

    Ctx->LifecycleState = (LONG)VidSchSchedulerResetting;
    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];
        VIDSCH_ENGINE_STATE EngineState;
        KIRQL OldIrql;

        KeCancelTimer(&Engine->TdrTimer);
        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        EngineState = VidSchpReadState(Engine);
        if (EngineState != VidSchEngineResetting && !VidSchpTryTransition(&Engine->State, (LONG)EngineState, (LONG)VidSchEngineResetting, NULL))
            Status = STATUS_INVALID_DEVICE_STATE;
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    }

    if (!NT_SUCCESS(Status))
        Ctx->LifecycleState = (LONG)VidSchSchedulerError;
    ExReleaseFastMutex(&Ctx->LifecycleMutex);
    if (!NT_SUCCESS(Status))
    {
        VidSchpReleaseCall(Adapter);
        return Status;
    }

    for (i = 0; i < Ctx->EngineCount; i++)
    {
        if (InterlockedCompareExchange(&Ctx->Engines[i].OutstandingWorkers, 0, 0) != 0)
            KeWaitForSingleObject(&Ctx->Engines[i].WorkersDrainedEvent, Executive, KernelMode, FALSE, NULL);
    }
    KeFlushQueuedDpcs();
    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

VOID
VidSchCompleteAdapterReset(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ BOOLEAN ResetSucceeded)
{
    PVIDSCH_CONTEXT Ctx;
    LIST_ENTRY RetireList;
    PLIST_ENTRY Link;
    BOOLEAN LifecycleRecovered = ResetSucceeded;
    ULONG i;

    PAGED_CODE();

    if (Adapter == NULL)
        return;
    if (!VidSchpAcquireCall(Adapter))
        return;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return;
    }

    InitializeListHead(&RetireList);
    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    if (VidSchpReadSchedulerState(Ctx) != VidSchSchedulerResetting)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return;
    }

    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];
        KIRQL OldIrql;

        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        if (ResetSucceeded)
        {
            while (!IsListEmpty(&Engine->RunQueueHead))
            {
                Link = RemoveHeadList(&Engine->RunQueueHead);
                InsertTailList(&RetireList, Link);
                Engine->PendingPacketCount--;
            }
            /* Reset aborts queued/in-flight packets; it is not hardware
             * completion and must not advance the observed completion fence. */
            if (!VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineResetting, (LONG)VidSchEngineResetComplete, NULL) || !VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineResetComplete, (LONG)VidSchEngineSuspended, NULL))
                LifecycleRecovered = FALSE;
        }
        if (!ResetSucceeded)
            VidSchpTryTransition(&Engine->State, (LONG)VidSchEngineResetting, (LONG)VidSchEngineError, NULL);
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        KeSetEvent(&Engine->CompletionEvent, IO_NO_INCREMENT, FALSE);
    }

    Ctx->LifecycleState = LifecycleRecovered ? (LONG)VidSchSchedulerSuspended : (LONG)VidSchSchedulerError;
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    while (!IsListEmpty(&RetireList))
    {
        PVIDSCH_DMA_PACKET Packet;

        Link = RemoveHeadList(&RetireList);
        Packet = CONTAINING_RECORD(Link, VIDSCH_DMA_PACKET, RunQueueEntry);
        VidSchpDereferencePacket(Packet);
    }

    VidSchpReleaseCall(Adapter);
}

NTSTATUS
VidSchGetEngineTdrInfo(
    _In_  PDXGKRNL_ADAPTER Adapter,
    _In_  ULONG             EngineOrdinal,
    _Out_ PVOID             TdrInfo)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(EngineOrdinal);

    if (TdrInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    return STATUS_NOT_SUPPORTED;
}

/* ========================================================================
 * VidSchInterface — Exported entry point (dxgkrnl.spec)
 *
 * On Windows 8.1, dxgmms1.sys exports this as ordinal 2.  The caller
 * (dxgkrnl) passes a pointer to a VIDSCH_INTERFACE structure which this
 * function fills with the scheduler's function pointer table.
 *
 * In ReactOS, this is exported directly from dxgkrnl.sys since the
 * scheduler is compiled inline.  The export exists for ABI compatibility
 * with any code that expects to call VidSchInterface by name.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
NTAPI
VidSchInterface(
    _Out_ PVIDSCH_INTERFACE Interface)
{
    PAGED_CODE();

    if (Interface == NULL)
        return STATUS_INVALID_PARAMETER;

    VidSchGetInterface(Interface);

    return STATUS_SUCCESS;
}
