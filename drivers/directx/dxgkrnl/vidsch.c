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
    _In_ ULONG CompletedFence,
    _In_ ULONG TargetFence)
{
    return ((LONG)(CompletedFence - TargetFence) >= 0);
}

static VOID
VidSchpUpdateFence(
    _Inout_ volatile LONG *Fence,
    _In_ ULONG ReportedFence)
{
    LONG Current;

    for (;;)
    {
        Current = InterlockedCompareExchange(Fence, 0, 0);
        if (VidSchpFenceReached((ULONG)Current, ReportedFence) || InterlockedCompareExchange(Fence, (LONG)ReportedFence, Current) == Current)
            return;
    }
}

static DECLSPEC_NORETURN VOID
VidSchpBugCheckInvalidFence(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG ReportedFence,
    _In_ ULONG LastSubmittedFence)
{
    KeBugCheckEx(0x119, 0x1, (ULONG_PTR)ReportedFence, (ULONG_PTR)LastSubmittedFence, (ULONG_PTR)Adapter);
}

/*
 * VidSchpTryTransitionEngine — validated engine transition through dxgmms2.
 *
 * The state machine and its transition table live in the scheduler provider;
 * dxgkrnl asks for a move and is told whether the engine took it.
 */
static BOOLEAN VidSchpTryTransitionEngine(_In_ PVIDSCH_ENGINE Engine, _In_ VIDSCH_ENGINE_STATE Expected, _In_ VIDSCH_ENGINE_STATE New);
static BOOLEAN VidSchpForceEngineState(_In_ PVIDSCH_ENGINE Engine, _In_ VIDSCH_ENGINE_STATE New);
static NTSTATUS VidSchpTransitionEngineEx(_In_ PVIDSCH_ENGINE Engine, _In_ VIDSCH_ENGINE_STATE Expected, _In_ VIDSCH_ENGINE_STATE New, _Out_ VIDSCH_ENGINE_STATE *OutPrevious);

/*
 * VidSchpReadState — read the current engine state without modifying it.
 */
static VIDSCH_ENGINE_STATE VidSchpEngineState(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG EngineOrdinal);

/* The engine state machine is dxgmms2's; read it through the contract. */
FORCEINLINE VIDSCH_ENGINE_STATE
VidSchpReadState(
    _In_ PVIDSCH_ENGINE Engine)
{
    return VidSchpEngineState(Engine->Adapter, Engine->EngineOrdinal);
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

static VOID VidSchpAssertOutstandingWorkersInvariantLocked(_In_ PVIDSCH_ENGINE Engine)
{
    ASSERT(Engine->OutstandingWorkers >= 0);
    ASSERT((KeReadStateEvent(&Engine->WorkersDrainedEvent) != 0) == (Engine->OutstandingWorkers == 0));
}

static VOID VidSchpReferenceOutstandingWorkerLocked(_In_ PVIDSCH_ENGINE Engine)
{
    ASSERT(Engine->OutstandingWorkers >= 0 && Engine->OutstandingWorkers < MAXLONG);
    if (Engine->OutstandingWorkers++ == 0)
        KeClearEvent(&Engine->WorkersDrainedEvent);
    VidSchpAssertOutstandingWorkersInvariantLocked(Engine);
}

static VOID VidSchpReleaseOutstandingWorker(_In_ PVIDSCH_ENGINE Engine)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    ASSERT(Engine->OutstandingWorkers > 0);
    if (--Engine->OutstandingWorkers == 0)
        KeSetEvent(&Engine->WorkersDrainedEvent, IO_NO_INCREMENT, FALSE);
    VidSchpAssertOutstandingWorkersInvariantLocked(Engine);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
}

static VOID VidSchpWaitForOutstandingWorkers(_In_ PVIDSCH_ENGINE Engine)
{
    BOOLEAN Drained;
    KIRQL OldIrql;

    for (;;)
    {
        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        Drained = Engine->OutstandingWorkers == 0;
        VidSchpAssertOutstandingWorkersInvariantLocked(Engine);
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        if (Drained)
            return;
        KeWaitForSingleObject(&Engine->WorkersDrainedEvent, Executive, KernelMode, FALSE, NULL);
    }
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

/* ========================================================================
 * dxgmms2 scheduler ownership
 *
 * The run queues, their order, the pending/reserved counts, and the engine
 * state machine live in dxgmms2.  dxgkrnl reaches them only through these
 * accessors, keeps the packet content, and performs the miniport DDI calls
 * that dxgmms2 cannot.  A packet pointer is the opaque cookie dxgmms2 stores;
 * dxgkrnl holds one packet reference from admission until it has processed
 * that packet's retirement record.
 * ====================================================================== */

static PDXGMMS2_SCHEDULER_INTERFACE_V1
VidSchpScheduler(_In_opt_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || InterlockedCompareExchange(&Adapter->Mms2SchedulerValid, 0, 0) == 0)
        return NULL;
    return &Adapter->Mms2SchedulerInterface;
}

static VOID VidSchpDereferencePacket(_In_ PVIDSCH_DMA_PACKET Packet);
static ULONG VidSchpDrainRetirements(_In_ PDXGKRNL_ADAPTER Adapter);
static VOID VidSchpFinalizeDequeuedPacket(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS CompletionStatus);

FORCEINLINE PVIDSCH_DMA_PACKET
VidSchpPacketFromCookie(_In_ ULONGLONG Cookie)
{
    return (PVIDSCH_DMA_PACKET)(ULONG_PTR)Cookie;
}

/* Admits a fully built packet into the dxgmms2 queue.  On success dxgmms2
 * owns the ordering and the packet reference this transfers to it. */
static NTSTATUS
VidSchpAdmitPacket(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Inout_ PVIDSCH_DMA_PACKET Packet,
    _In_ ULONG Flags,
    _Out_ PULONG OutFenceId)
{
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);
    DXGMMS2_SCHEDULER_ADMIT_INFO_V1 Info;

    *OutFenceId = 0;
    if (Sched == NULL)
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Size = DXGMMS2_SCHEDULER_ADMIT_INFO_V1_SIZE;
    Info.Version = DXGMMS2_SCHEDULER_VERSION_1;
    Info.NodeOrdinal = Packet->NodeOrdinal;
    Info.EngineOrdinal = Packet->NodeOrdinal;
    Info.Flags = Flags;
    Info.Priority = Packet->Priority;
    Info.PacketCookie = (ULONGLONG)(ULONG_PTR)Packet;
    Info.OwnerCookie = (ULONGLONG)(ULONG_PTR)Packet->Context;
    if (Packet->SubmissionFenceId != 0)
    {
        Info.Flags |= DXGMMS2_SCHEDULER_ADMIT_PREFENCED;
        Info.SubmissionFenceId = Packet->SubmissionFenceId;
    }
    Packet->SchedulerCookie = Info.PacketCookie;
    return Sched->AdmitPacket(Sched->SchedulerHandle, &Info, OutFenceId);
}

/* Drains retirement records and finalizes each packet exactly once.  Returns
 * how many packets reached a terminal state. */
static ULONG
VidSchpDrainRetirements(_In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);
    DXGMMS2_SCHEDULER_RETIREMENT_V1 Records[DXGMMS2_SCHEDULER_MAX_RETIREMENTS];
    ULONG Total = 0;
    ULONG Count;
    ULONG Index;

    if (Sched == NULL)
        return 0;
    for (;;)
    {
        Count = 0;
        if (!NT_SUCCESS(Sched->DrainRetirements(Sched->SchedulerHandle, Records, RTL_NUMBER_OF(Records), &Count)) || Count == 0)
            return Total;
        Total += Count;
        for (Index = 0; Index < Count; ++Index)
        {
            PVIDSCH_DMA_PACKET Packet = VidSchpPacketFromCookie(Records[Index].PacketCookie);

            if (Packet == NULL)
                continue;
            if (Records[Index].Reason == Dxgmms2RetireCompleted)
            {
                DxgkDeviceWorkComplete(Packet->DeviceWork);
                DxgkContextOrderCompletePacket(Packet, STATUS_SUCCESS);
                VidSchpDereferencePacket(Packet);
            }
            else
            {
                VidSchpFinalizeDequeuedPacket(Packet, Records[Index].TerminalStatus);
            }
        }
        if (Count < RTL_NUMBER_OF(Records))
            return Total;
    }
}

static VIDSCH_ENGINE_STATE
VidSchpEngineState(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG EngineOrdinal)
{
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);
    DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 Status;

    if (Sched == NULL)
        return VidSchEngineError;
    RtlZeroMemory(&Status, sizeof(Status));
    Status.Size = DXGMMS2_SCHEDULER_ENGINE_STATUS_V1_SIZE;
    Status.Version = DXGMMS2_SCHEDULER_VERSION_1;
    if (!NT_SUCCESS(Sched->QueryEngineStatus(Sched->SchedulerHandle, EngineOrdinal, &Status)))
        return VidSchEngineError;
    switch (Status.State)
    {
        case Dxgmms2EngineIdle:       return VidSchEngineIdle;
        case Dxgmms2EngineRunning:    return VidSchEngineRunning;
        case Dxgmms2EngineSubmitting: return VidSchEngineSubmitting;
        case Dxgmms2EnginePreempting: return VidSchEnginePreempting;
        case Dxgmms2EnginePreempted:  return VidSchEnginePreempted;
        case Dxgmms2EngineResetting:  return VidSchEngineResetting;
        case Dxgmms2EngineSuspended:  return VidSchEngineSuspended;
        default:                      return VidSchEngineError;
    }
}

/* VIDSCH_ENGINE_STATE and DXGMMS2_ENGINE_STATE share one numbering; the
 * assertions below keep them from drifting apart silently. */
C_ASSERT((ULONG)VidSchEngineIdle == Dxgmms2EngineIdle);
C_ASSERT((ULONG)VidSchEngineRunning == Dxgmms2EngineRunning);
C_ASSERT((ULONG)VidSchEngineBudgetComputed == Dxgmms2EngineBudgetComputed);
C_ASSERT((ULONG)VidSchEngineSubmitting == Dxgmms2EngineSubmitting);
C_ASSERT((ULONG)VidSchEnginePreempting == Dxgmms2EnginePreempting);
C_ASSERT((ULONG)VidSchEnginePreempted == Dxgmms2EnginePreempted);
C_ASSERT((ULONG)VidSchEngineResetting == Dxgmms2EngineResetting);
C_ASSERT((ULONG)VidSchEngineResetComplete == Dxgmms2EngineResetComplete);
C_ASSERT((ULONG)VidSchEngineSuspended == Dxgmms2EngineSuspended);
C_ASSERT((ULONG)VidSchEngineResuming == Dxgmms2EngineResuming);
C_ASSERT((ULONG)VidSchEngineFlipPending == Dxgmms2EngineFlipPending);
C_ASSERT((ULONG)VidSchEngineFlipExecuting == Dxgmms2EngineFlipExecuting);
C_ASSERT((ULONG)VidSchEngineCompleting == Dxgmms2EngineCompleting);
C_ASSERT((ULONG)VidSchEngineError == Dxgmms2EngineError);
C_ASSERT((ULONG)VidSchEngineStateCount == Dxgmms2EngineStateCount);

static BOOLEAN
VidSchpTryTransitionEngine(
    _In_ PVIDSCH_ENGINE Engine,
    _In_ VIDSCH_ENGINE_STATE Expected,
    _In_ VIDSCH_ENGINE_STATE New)
{
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Engine->Adapter);

    if (Sched == NULL)
        return FALSE;
    return NT_SUCCESS(Sched->SetEngineState(Sched->SchedulerHandle, Engine->EngineOrdinal, (ULONG)Expected, (ULONG)New, NULL));
}

/* Reports the observed state so a caller can tell a lost race (retry) from a
 * transition the scheduler does not model (give up). */
static NTSTATUS
VidSchpTransitionEngineEx(
    _In_ PVIDSCH_ENGINE Engine,
    _In_ VIDSCH_ENGINE_STATE Expected,
    _In_ VIDSCH_ENGINE_STATE New,
    _Out_ VIDSCH_ENGINE_STATE *OutPrevious)
{
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Engine->Adapter);
    ULONG Previous = (ULONG)VidSchEngineError;
    NTSTATUS Status;

    *OutPrevious = VidSchEngineError;
    if (Sched == NULL)
        return STATUS_DEVICE_NOT_READY;
    Status = Sched->SetEngineState(Sched->SchedulerHandle, Engine->EngineOrdinal, (ULONG)Expected, (ULONG)New, &Previous);
    *OutPrevious = (VIDSCH_ENGINE_STATE)Previous;
    return Status;
}

static BOOLEAN
VidSchpForceEngineState(
    _In_ PVIDSCH_ENGINE Engine,
    _In_ VIDSCH_ENGINE_STATE New)
{
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Engine->Adapter);

    if (Sched == NULL)
        return FALSE;
    return NT_SUCCESS(Sched->SetEngineState(Sched->SchedulerHandle, Engine->EngineOrdinal, DXGMMS2_ENGINE_STATE_ANY, (ULONG)New, NULL));
}

static ULONG
VidSchpEnginePendingCount(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG EngineOrdinal)
{
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);
    DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 Status;

    if (Sched == NULL)
        return 0;
    RtlZeroMemory(&Status, sizeof(Status));
    Status.Size = DXGMMS2_SCHEDULER_ENGINE_STATUS_V1_SIZE;
    Status.Version = DXGMMS2_SCHEDULER_VERSION_1;
    if (!NT_SUCCESS(Sched->QueryEngineStatus(Sched->SchedulerHandle, EngineOrdinal, &Status)))
        return 0;
    return Status.PendingPacketCount;
}

static BOOLEAN VidSchpKickEngine(_In_ PVIDSCH_ENGINE Engine, _In_opt_ PVIDSCH_DMA_PACKET AuthorizedPacket);
static VOID VidSchpDereferencePacket(_In_ PVIDSCH_DMA_PACKET Packet);

BOOLEAN VidSchIsContextOrderPacketDispatchable(_In_ PVIDSCH_DMA_PACKET Packet)
{
    PVIDSCH_ENGINE Engine;
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
    ULONGLONG HeadCookie = 0;
    KIRQL OldIrql;
    BOOLEAN Dispatchable;

    if (Packet == NULL || Packet->OwnerEngine == NULL || InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) != VIDSCH_CONTEXT_ORDER_ADMITTED)
        return FALSE;
    Engine = Packet->OwnerEngine;
    Sched = VidSchpScheduler(Engine->Adapter);
    if (Sched == NULL)
        return FALSE;
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    /*
     * Engine state alone does not say whose turn it is.  Ask dxgmms2 which
     * packet a claim would hand out: authorising anything else would let the
     * kick claim a different packet and the caller cancel still-valid work.
     */
    Dispatchable = !Packet->Kicked && Engine->Scheduler != NULL &&
                   (VidSchpReadSchedulerState(Engine->Scheduler) == VidSchSchedulerRunning || VidSchpReadSchedulerState(Engine->Scheduler) == VidSchSchedulerSuspending) &&
                   Sched->PeekNextPacket(Sched->SchedulerHandle, Engine->EngineOrdinal, &HeadCookie) &&
                   HeadCookie == (ULONGLONG)(ULONG_PTR)Packet;
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    return Dispatchable;
}

BOOLEAN VidSchIsContextOrderPacketResubmittable(_In_ PVIDSCH_DMA_PACKET Packet)
{
    PVIDSCH_ENGINE Engine;
    KIRQL OldIrql;
    BOOLEAN Resubmittable;

    if (Packet == NULL || Packet->OwnerEngine == NULL || Packet->ContextOrderOperation == NULL || InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) != VIDSCH_CONTEXT_ORDER_SUBMITTED || InterlockedCompareExchange(&Packet->ContextOrderResubmissionPending, 0, 0) == 0)
        return FALSE;
    Engine = Packet->OwnerEngine;
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    Resubmittable = !Packet->Kicked && VidSchpReadState(Engine) == VidSchEngineIdle && Engine->Scheduler != NULL && (VidSchpReadSchedulerState(Engine->Scheduler) == VidSchSchedulerRunning || VidSchpReadSchedulerState(Engine->Scheduler) == VidSchSchedulerSuspending);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    return Resubmittable;
}

BOOLEAN VidSchDispatchContextOrderPacketResubmission(_Inout_ PVIDSCH_DMA_PACKET Packet)
{
    BOOLEAN Dispatched;

    if (!VidSchIsContextOrderPacketResubmittable(Packet))
        return FALSE;
    if (!VidSchpAcquireCall(Packet->OwnerEngine->Adapter))
    {
        DxgkContextOrderAbortPacket(Packet, STATUS_DEVICE_REMOVED);
        return FALSE;
    }
    Dispatched = VidSchpKickEngine(Packet->OwnerEngine, Packet);
    VidSchpReleaseCall(Packet->OwnerEngine->Adapter);
    return Dispatched;
}

VOID VidSchDispatchClaimedContextOrderPacket(_Inout_ PVIDSCH_DMA_PACKET Packet)
{
    PVIDSCH_ENGINE Engine;
    KIRQL OldIrql;
    BOOLEAN Dispatched;
    BOOLEAN Removed = FALSE;

    if (Packet == NULL || Packet->OwnerEngine == NULL)
        return;
    if (InterlockedCompareExchange(&Packet->ContextOrderState, VIDSCH_CONTEXT_ORDER_DISPATCHING, VIDSCH_CONTEXT_ORDER_CLAIMED) != VIDSCH_CONTEXT_ORDER_CLAIMED)
        return;
    Engine = Packet->OwnerEngine;
    if (!VidSchpAcquireCall(Engine->Adapter))
    {
        DxgkContextOrderAbortPacket(Packet, STATUS_DEVICE_REMOVED);
        return;
    }
    Dispatched = VidSchpKickEngine(Engine, Packet);
    if (!Dispatched)
    {
        /* dxgmms2 owns removal: it withdraws only packets it never
          * dispatched and emits their retirement records. */
        if (!Packet->Kicked)
        {
            PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Engine->Adapter);

            if (Sched != NULL)
            {
                (VOID)Sched->CancelOwnerPackets(Sched->SchedulerHandle, (ULONGLONG)(ULONG_PTR)Packet->Context, STATUS_CANCELLED);
                VidSchpDrainRetirements(Engine->Adapter);
                Removed = TRUE;
            }
        }
        DxgkContextOrderCommitPacket(Packet, STATUS_CANCELLED);
        if (Removed)
        {
            DxgkDeviceWorkComplete(Packet->DeviceWork);
            VidSchpDereferencePacket(Packet);
        }
    }
    VidSchpReleaseCall(Engine->Adapter);
}

typedef struct _VIDSCH_VIRTUAL_SUBMIT_WORK
{
    WORK_QUEUE_ITEM WorkItem;
    PVIDSCH_ENGINE Engine;
    PVIDSCH_DMA_PACKET Packet;
} VIDSCH_VIRTUAL_SUBMIT_WORK, *PVIDSCH_VIRTUAL_SUBMIT_WORK;

static VOID NTAPI VidSchpVirtualSubmitWorker(_In_ PVOID Parameter);
static VOID NTAPI VidSchpPacketCleanupWorker(_In_ PVOID Parameter);

static VOID
VidSchpDereferencePacket(
    _In_ PVIDSCH_DMA_PACKET Packet)
{
    if (InterlockedDecrement(&Packet->ReferenceCount) != 0)
        return;

    if (Packet->FenceIdentityReserved && Packet->OwnerEngine != NULL)
        DxgkReleaseSubmittedFenceIdentity(Packet->OwnerEngine->Adapter, Packet->NodeOrdinal, Packet->SubmissionFenceId);
    if (Packet->TrackerReservation != NULL && !Packet->TrackerOwnsDmaBuffer)
        DxgkCancelTrackedDmaBuffer(Packet->TrackerReservation);
    if (Packet->DmaBuffer != NULL && !Packet->TrackerOwnsDmaBuffer)
        DxgkFreeDmaBuffer(Packet->DmaBuffer);
    /* The miniport is done with this command buffer's address; the range it
     * executes from may be unmapped again. */
    if (Packet->GpuVaPinProcess != NULL)
    {
        DxgkGpuVaUnpinRange(Packet->GpuVaPinProcess, Packet->DmaBufferGpuVa, Packet->VirtualDmaBufferSize);
        Packet->GpuVaPinProcess = NULL;
    }
    if (Packet->OwnedDriverPrivateData != NULL)
        ExFreePoolWithTag(Packet->OwnedDriverPrivateData, TAG_VIDSCH);
    if (Packet->VirtualSubmitWorkItem != NULL)
        ExFreePoolWithTag(Packet->VirtualSubmitWorkItem, TAG_VIDSCH);
    DxgkDeviceWorkDestroy(Packet->DeviceWork);
    Packet->DeviceWork = NULL;
    if (Packet->HoldsContextReference)
        DxgkDereferenceContext((PDXGKRNL_CONTEXT)Packet->Context);
    ExFreePoolWithTag(Packet, TAG_VIDSCH);
}

VOID VidSchReferenceContextOrderPacket(_Inout_ PVIDSCH_DMA_PACKET Packet)
{
    ASSERT(Packet != NULL && InterlockedCompareExchange(&Packet->ReferenceCount, 0, 0) > 0);
    InterlockedIncrement(&Packet->ReferenceCount);
}

VOID VidSchDereferenceContextOrderPacket(_Inout_ PVIDSCH_DMA_PACKET Packet)
{
    if (Packet != NULL)
        VidSchpDereferencePacket(Packet);
}

static BOOLEAN VidSchpPacketSubmissionOwned(_In_ PVIDSCH_DMA_PACKET Packet)
{
    return Packet->Kicked || InterlockedCompareExchange(&Packet->ContextOrderResubmissionPending, 0, 0) != 0;
}

static VOID VidSchpFinalizeDequeuedPacket(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS CompletionStatus)
{
    Packet->SchedulerCookie = 0;
    DxgkDeviceWorkComplete(Packet->DeviceWork);
    if (Packet->ContextOrderOperation != NULL)
        DxgkContextOrderAbortPacket(Packet, CompletionStatus);
    VidSchpDereferencePacket(Packet);
}

static VOID
NTAPI
VidSchpPacketCleanupWorker(
    _In_ PVOID Parameter)
{
    PVIDSCH_DMA_PACKET Packet = (PVIDSCH_DMA_PACKET)Parameter;
    PVIDSCH_ENGINE Engine = Packet->OwnerEngine;

    DxgkDeviceWorkComplete(Packet->DeviceWork);
    VidSchpDereferencePacket(Packet);
    VidSchpReleaseOutstandingWorker(Engine);
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
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
    LONG Completed;
    KIRQL OldIrql;
    BOOLEAN PreemptionInterrupt;
    BOOLEAN PreemptionCompleted = FALSE;
    ULONG CompletedPreemptionFence = 0;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Engine == NULL)
        return;
    Sched = VidSchpScheduler(Engine->Adapter);

    Completed = Engine->LastCompletedFence;
    PreemptionInterrupt = (InterlockedExchange(&Engine->PreemptionInterruptPending, 0) != 0);

    /*
     * dxgmms2 owns the queue: hand it the hardware watermark and let it decide
     * what that retires.  It stops at the first packet the miniport never
     * received, so a watermark can never retire un-executed work.
     */
    if (PreemptionInterrupt)
    {
        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        CompletedPreemptionFence = (ULONG)InterlockedExchange(&Engine->PendingPreemptionFenceId, 0);
        InterlockedExchange(&Engine->PreemptionDdiState, 0);
        if (CompletedPreemptionFence != 0 && VidSchpEngineState(Engine->Adapter, Engine->EngineOrdinal) == VidSchEnginePreempting)
        {
            Engine->CompletedPreemptionEngineOrdinal = Engine->PendingPreemptionEngineOrdinal;
            InterlockedExchange(&Engine->CompletedPreemptionFenceId, (LONG)CompletedPreemptionFence);
            VidSchpTryTransitionEngine(Engine, VidSchEnginePreempting, VidSchEnginePreempted);
            PreemptionCompleted = TRUE;
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    }

    if (Sched != NULL)
        (VOID)Sched->NotifyCompletion(Sched->SchedulerHandle, Engine->EngineOrdinal, (ULONG)Completed);

    /*
     * Preemption interrupted every packet the miniport had accepted but not
     * completed.  dxgmms2 keeps them queued in the same order with the same
     * fence identity and clears only their dispatched mark, so the next kick
     * resubmits exactly the same work.
     */
    if (PreemptionCompleted && Sched != NULL)
    {
        ULONGLONG Cookies[DXGMMS2_SCHEDULER_MAX_RETIREMENTS];
        ULONG ResetCount = 0;
        ULONG ResetIndex;

        if (NT_SUCCESS(Sched->ResetDispatched(Sched->SchedulerHandle, Engine->EngineOrdinal, Cookies, RTL_NUMBER_OF(Cookies), &ResetCount)))
        {
            for (ResetIndex = 0; ResetIndex < ResetCount; ++ResetIndex)
            {
                PVIDSCH_DMA_PACKET Preempted = VidSchpPacketFromCookie(Cookies[ResetIndex]);

                if (Preempted == NULL)
                    continue;
                Preempted->Kicked = FALSE;
                InterlockedExchange(&Preempted->ContextOrderResubmissionPending, 1);
                if (Engine->Adapter->MiniportContext->InitData.s.Version >= DXGKDDI_INTERFACE_VERSION_WDDM2_0)
                    Preempted->SubmitFlags |= VIDSCH_SUBMITFLAG_RESUBMISSION;
            }
        }
        VidSchpTryTransitionEngine(Engine, VidSchEnginePreempted, VidSchEngineIdle);
    }
    else if (!PreemptionCompleted && VidSchpEngineState(Engine->Adapter, Engine->EngineOrdinal) == VidSchEnginePreempting &&
             InterlockedCompareExchange(&Engine->PreemptionDdiState, 0, 0) == 0)
    {
        InterlockedExchange(&Engine->PendingPreemptionFenceId, 0);
        VidSchpTryTransitionEngine(Engine, VidSchEnginePreempting, VidSchEngineRunning);
    }

    /* Turn the retirement records into dxgkrnl terminal cleanup. */
    VidSchpDrainRetirements(Engine->Adapter);

    (VOID)VidSchpKickEngine(Engine, NULL);

    /* Completion drives tracked-DMA retirement (fence signals, open-binding
     * closes, in-flight accounting); nothing may wait for a later submission
     * to flush a completed packet. */
    DxgkRetireCompletedDmaBuffers(Engine->Adapter);

    if (PreemptionCompleted)
        KeSetEvent(&Engine->PreemptionCompletedEvent, IO_NO_INCREMENT, FALSE);

    /* Signal completion event so VidSchWaitForIdle can wake up. */
    KeSetEvent(&Engine->CompletionEvent, IO_NO_INCREMENT, FALSE);

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
    PVIDSCH_DMA_PACKET Packet = Work->Packet;
    DXGKARG_SUBMITCOMMANDVIRTUAL SubmitArgs;
    KIRQL OldIrql;
    NTSTATUS AbortStatus;
    NTSTATUS Status = STATUS_DELETE_PENDING;
    BOOLEAN KmdCallAcquired = FALSE;
    BOOLEAN Removed = FALSE;
    BOOLEAN SubmissionOwned = FALSE;

    /* The kick already claimed this packet from dxgmms2; the claim token is
     * this worker's authority to dispatch it. */
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (Packet != NULL && Packet->SchedulerClaimToken != 0 && Packet->VirtualAddressing && (Packet->ContextOrderOperation == NULL || InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) == VIDSCH_CONTEXT_ORDER_DISPATCHING || (InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) == VIDSCH_CONTEXT_ORDER_SUBMITTED && InterlockedCompareExchange(&Packet->ContextOrderResubmissionPending, 0, 0) != 0)))
    {
        Packet->Kicked = TRUE;
        SubmissionOwned = TRUE;
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
        SubmitArgs.Flags.Value = Packet->SubmitFlags;
        SubmitArgs.EngineOrdinal = Packet->EngineOrdinal;
        SubmitArgs.NodeOrdinal = Packet->NodeOrdinal;
    }
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

    if (SubmissionOwned && DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual) != NULL)
        KmdCallAcquired = DxgkAcquireKmdCall(Adapter);
    if (KmdCallAcquired)
    {
        PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);

        if (Sched != NULL)
            (VOID)Sched->PublishDispatch(Sched->SchedulerHandle, Engine->EngineOrdinal, Packet->SchedulerClaimToken);
        DxgkPublishSubmittedFence(Adapter, Packet->NodeOrdinal, Packet->SubmissionFenceId);
        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual)(Adapter->MiniportDeviceContext, &SubmitArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(0x119, 0x2, (ULONG_PTR)Status, (ULONG_PTR)&SubmitArgs, (ULONG_PTR)Engine);
    }

    if (Packet != NULL && Packet->ContextOrderOperation != NULL)
        DxgkContextOrderCommitPacket(Packet, Status);
    if (KmdCallAcquired)
    {
        InterlockedExchange(&Packet->ContextOrderResubmissionPending, 0);
        DxgkReleaseKmdCall(Adapter);
    }

    /*
     * Commit the claim this worker inherited from the kick, exactly once.  A
     * successful dispatch leaves the packet queued until its fence retires; a
     * failed one makes dxgmms2 emit the retirement record the drain turns
     * into terminal cleanup.
     */
    if (Packet != NULL && Packet->SchedulerClaimToken != 0)
    {
        PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);
        NTSTATUS CommitStatus = Status;

        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("VidSch: DxgkDdiSubmitCommandVirtual failed 0x%08lX engine=%lu fence=%lu\n", Status, Engine->EngineOrdinal, Packet->SubmissionFenceId);
            AbortStatus = (NTSTATUS)InterlockedCompareExchange((volatile LONG *)&Packet->ContextOrderAbortStatus, 0, 0);
            if (AbortStatus != STATUS_PENDING)
                CommitStatus = AbortStatus;
        }
        if (Sched != NULL)
        {
            ULONGLONG CommitToken = Packet->SchedulerClaimToken;

            Packet->SchedulerClaimToken = 0;
            (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->EngineOrdinal, CommitToken, CommitStatus);
            if (VidSchpDrainRetirements(Adapter) != 0)
            {
                DxgkRetireCompletedDmaBuffers(Adapter);
                KeSetEvent(&Engine->CompletionEvent, IO_NO_INCREMENT, FALSE);
            }
            Removed = TRUE;
        }
        if (!NT_SUCCESS(Status))
            (VOID)VidSchpKickEngine(Engine, NULL);
    }

    if (Packet != NULL)
        VidSchpDereferencePacket(Packet);
    ExFreePoolWithTag(Work, TAG_VIDSCH);
    VidSchpReleaseOutstandingWorker(Engine);
    VidSchpReleaseCall(Adapter);
}

/* ========================================================================
 * VidSchpKickEngine — submit the next queued packet to the miniport
 *
 * Claims queue state under QueueLock, but never holds that lock across the
 * miniport callback. This lets TDR claim the engine if SubmitCommand stalls.
 * ====================================================================== */
static BOOLEAN
VidSchpKickEngine(
    _In_ PVIDSCH_ENGINE Engine,
    _In_opt_ PVIDSCH_DMA_PACKET AuthorizedPacket)
{
    PDXGKRNL_ADAPTER Adapter = Engine != NULL ? Engine->Adapter : NULL;
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);

    if (Adapter == NULL)
        return FALSE;

    for (;;)
    {
        PDXGKRNL_SUBMIT_DMA_BUFFER Reservation = NULL;
        PVIDSCH_DMA_PACKET Packet;
        DXGMMS2_SCHEDULER_CLAIM_V1 Claim;
        ULONGLONG ClaimToken;
        DXGKARG_SUBMITCOMMAND SubmitArgs;
        KIRQL CallIrql;
        KIRQL OldIrql;
        NTSTATUS Status = STATUS_NOT_SUPPORTED;
        BOOLEAN KmdCallAcquired = FALSE;
        BOOLEAN KickNext = FALSE;

        /* dxgmms2 hands out the next runnable packet and, by issuing the
         * claim, performs the Idle/Running -> Submitting transition that
         * serialises dispatch.  dxgkrnl never inspects queue order itself. */
        if (Sched == NULL || Engine->Scheduler == NULL ||
            (VidSchpReadSchedulerState(Engine->Scheduler) != VidSchSchedulerRunning &&
             VidSchpReadSchedulerState(Engine->Scheduler) != VidSchSchedulerSuspending))
            return FALSE;

        RtlZeroMemory(&Claim, sizeof(Claim));
        Claim.Size = DXGMMS2_SCHEDULER_CLAIM_V1_SIZE;
        Claim.Version = DXGMMS2_SCHEDULER_VERSION_1;
        if (!NT_SUCCESS(Sched->ClaimNextPacket(Sched->SchedulerHandle, Engine->EngineOrdinal, &Claim)))
            return FALSE;
        Packet = VidSchpPacketFromCookie(Claim.PacketCookie);
        if (Packet == NULL)
        {
            (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->EngineOrdinal, Claim.ClaimToken, STATUS_INVALID_PARAMETER);
            return FALSE;
        }
        ClaimToken = Claim.ClaimToken;
        Packet->SchedulerClaimToken = ClaimToken;
        if (Packet->SubmissionFenceId == 0)
            Packet->SubmissionFenceId = Claim.SubmissionFenceId;

        /* Ordered context work may only run for its authorised claimant and
         * only at PASSIVE_LEVEL; hand the claim back otherwise. */
        if (Packet->ContextOrderOperation != NULL && (AuthorizedPacket != Packet || (InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) != VIDSCH_CONTEXT_ORDER_DISPATCHING && (InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) != VIDSCH_CONTEXT_ORDER_SUBMITTED || InterlockedCompareExchange(&Packet->ContextOrderResubmissionPending, 0, 0) == 0)) || KeGetCurrentIrql() != PASSIVE_LEVEL))
        {
            VidSchReferenceContextOrderPacket(Packet);
            (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->EngineOrdinal, ClaimToken, STATUS_SUCCESS);
            DxgkContextOrderScheduleReferenced((PDXGKRNL_CONTEXT)Packet->Context);
            VidSchDereferenceContextOrderPacket(Packet);
            return FALSE;
        }
        if (Packet->ContextOrderOperation == NULL && AuthorizedPacket != NULL)
        {
            (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->EngineOrdinal, ClaimToken, STATUS_SUCCESS);
            return FALSE;
        }
        if (Packet->VirtualAddressing)
        {
            PVIDSCH_VIRTUAL_SUBMIT_WORK Work = (PVIDSCH_VIRTUAL_SUBMIT_WORK)Packet->VirtualSubmitWorkItem;

            /* Resubmission after preemption: the original work item was
             * consumed by the first kick — mint a fresh one. */
            if (Work == NULL)
            {
                Work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Work), TAG_VIDSCH);
                if (Work != NULL)
                {
                    RtlZeroMemory(Work, sizeof(*Work));
                    Work->Engine = Engine;
                    Work->Packet = Packet;
                    ExInitializeWorkItem(&Work->WorkItem, VidSchpVirtualSubmitWorker, Work);
                }
            }
            if (Work == NULL)
            {
                /* The claim is committed as a failure, so dxgmms2 retires the
                 * packet and dxgkrnl finalizes it from the retirement record. */
                Packet->SchedulerClaimToken = 0;
                (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->EngineOrdinal, ClaimToken, STATUS_INSUFFICIENT_RESOURCES);
                VidSchpDrainRetirements(Adapter);
                KeSetEvent(&Engine->CompletionEvent, IO_NO_INCREMENT, FALSE);
                if (AuthorizedPacket != NULL)
                    return TRUE;
                continue;
            }
            Work->Packet = Packet;
            Packet->VirtualSubmitWorkItem = NULL;
            InterlockedIncrement(&Packet->ReferenceCount);
            VidSchpReferenceActiveCall(Adapter);
            KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
            VidSchpReferenceOutstandingWorkerLocked(Engine);
            KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
            ExQueueWorkItem(&Work->WorkItem, DelayedWorkQueue);
            return TRUE;
        }

        if (Packet->SubmissionFenceId == 0)
            Packet->SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
        Packet->Kicked = TRUE;
        InterlockedIncrement(&Packet->ReferenceCount);

        /* The packet's DMA-buffer and tracker fields are still mutable by
         * completion paths; snapshot them under the engine lock. */
        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
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
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

        if (DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand) != NULL)
            KmdCallAcquired = DxgkAcquireKmdCall(Adapter);
        if (KmdCallAcquired)
        {
            (VOID)Sched->PublishDispatch(Sched->SchedulerHandle, Engine->EngineOrdinal, ClaimToken);
            DxgkPublishSubmittedFence(Adapter, Packet->NodeOrdinal, Packet->SubmissionFenceId);
            KeRaiseIrql(DISPATCH_LEVEL, &CallIrql);
            _SEH2_TRY
            {
                Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand)(Adapter->MiniportDeviceContext, &SubmitArgs);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            KeLowerIrql(CallIrql);
            if (!NT_SUCCESS(Status))
                KeBugCheckEx(0x119, 0x2, (ULONG_PTR)Status, (ULONG_PTR)&SubmitArgs, (ULONG_PTR)Engine);
        }

        if (KmdCallAcquired)
        {
            KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
            if (!Packet->TrackerOwnsDmaBuffer && Packet->TrackerReservation != NULL)
            {
                Reservation = Packet->TrackerReservation;
                Packet->TrackerOwnsDmaBuffer = TRUE;
                Reservation->FenceIdentityOwned = TRUE;
                Packet->FenceIdentityReserved = FALSE;
            }
            KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        }

        /*
         * Commit the dispatch outcome exactly once.  A failed dispatch makes
         * dxgmms2 drop the packet and emit its retirement record, which the
         * drain below turns into dxgkrnl's terminal cleanup; a successful one
         * leaves the packet queued until its fence retires.
         */
        (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->EngineOrdinal, ClaimToken, KmdCallAcquired ? Status : STATUS_DELETE_PENDING);
        if (!KmdCallAcquired)
            KickNext = TRUE;

        if (Reservation != NULL)
            DxgkCommitTrackedDmaBuffer(Adapter, Reservation);
        if (Packet->ContextOrderOperation != NULL)
            DxgkContextOrderCommitPacket(Packet, KmdCallAcquired ? Status : STATUS_DELETE_PENDING);
        if (KmdCallAcquired)
        {
            InterlockedExchange(&Packet->ContextOrderResubmissionPending, 0);
            DxgkReleaseKmdCall(Adapter);
        }
        /* Drain first: the retirement record releases dxgmms2's reference
         * while this kick's transient one still pins the packet.  Committing
         * the claim can itself retire work whose fence had already passed, so
         * the completion side-effects must run here too. */
        if (VidSchpDrainRetirements(Adapter) != 0)
        {
            DxgkRetireCompletedDmaBuffers(Adapter);
            KeSetEvent(&Engine->CompletionEvent, IO_NO_INCREMENT, FALSE);
        }
        VidSchpDereferencePacket(Packet);
        if (KickNext)
        {
            if (AuthorizedPacket != NULL)
                return TRUE;
            continue;
        }
        if (KmdCallAcquired)
            return TRUE;
        if (AuthorizedPacket != NULL)
            return TRUE;
    }
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
        Engine->PendingPreemptionFenceId = 0;
        Engine->PendingPreemptionEngineOrdinal = 0;
        Engine->PreemptionDdiState = 0;
        Engine->PreemptionInterruptPending = 0;
        Engine->CompletedPreemptionFenceId = 0;
        Engine->CompletedPreemptionEngineOrdinal = 0;
        Engine->NextFenceId = 1;

        KeInitializeSpinLock(&Engine->QueueLock);
        KeInitializeEvent(&Engine->CompletionEvent, SynchronizationEvent, FALSE);
        KeInitializeEvent(&Engine->PreemptionCompletedEvent, NotificationEvent, FALSE);
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
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
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
        VidSchpWaitForOutstandingWorkers(&Ctx->Engines[i]);

    Sched = VidSchpScheduler(Adapter);
    /* No further admissions once the scheduler is stopping. */
    if (Sched != NULL)
        (VOID)Sched->SetAdmission(Sched->SchedulerHandle, FALSE);

    /* Packets not accepted by the miniport can be cancelled now. Kicked
     * packets stay pinned until StopDevice has quiesced hardware ownership,
     * which is exactly the set dxgmms2 leaves behind on a plain abort. */
    if (Sched != NULL)
    {
        (VOID)Sched->AbortAllPackets(Sched->SchedulerHandle, 0, STATUS_CANCELLED);
        VidSchpDrainRetirements(Adapter);
    }
}

VOID VidSchCancelContextPackets(_In_ PDXGKRNL_ADAPTER Adapter, _In_ PDXGKRNL_CONTEXT Context, _In_ NTSTATUS CompletionStatus)
{
    PVIDSCH_CONTEXT Ctx;
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
    ULONG i;

    PAGED_CODE();
    if (Adapter == NULL || Context == NULL || CompletionStatus == STATUS_PENDING || NT_SUCCESS(CompletionStatus))
        return;
    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL)
        return;
    Sched = VidSchpScheduler(Adapter);
    if (Sched != NULL)
    {
        (VOID)Sched->CancelOwnerPackets(Sched->SchedulerHandle, (ULONGLONG)(ULONG_PTR)Context, CompletionStatus);
        VidSchpDrainRetirements(Adapter);
    }
    if (VidSchpAcquireCall(Adapter))
    {
        for (i = 0; i < Ctx->EngineCount; ++i)
            (VOID)VidSchpKickEngine(&Ctx->Engines[i], NULL);
        VidSchpReleaseCall(Adapter);
    }
}

VOID VidSchAbortAllPackets(_In_ PDXGKRNL_ADAPTER Adapter, _In_ NTSTATUS CompletionStatus)
{
    PVIDSCH_CONTEXT Ctx;
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
    ULONG i;

    PAGED_CODE();
    if (Adapter == NULL || CompletionStatus == STATUS_PENDING || NT_SUCCESS(CompletionStatus))
        return;
    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL)
        return;
    Sched = VidSchpScheduler(Adapter);
    if (Sched != NULL)
    {
        (VOID)Sched->AbortAllPackets(Sched->SchedulerHandle, DXGMMS2_SCHEDULER_ABORT_INCLUDE_DISPATCHED, CompletionStatus);
        VidSchpDrainRetirements(Adapter);
    }
    for (i = 0; i < Ctx->EngineCount; ++i)
        KeSetEvent(&Ctx->Engines[i].CompletionEvent, IO_NO_INCREMENT, FALSE);
}

/*
 * VidSchDestroy
 */
VOID
VidSchDestroy(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    ULONG i;

    PAGED_CODE();

    if (Adapter == NULL)
        return;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL)
        return;

    VidSchPrepareForStop(Adapter);
    VidSchAbortAllPackets(Adapter, STATUS_DEVICE_REMOVED);

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
    ULONG AdmittedFenceId;
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
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
    if (Packet->SubmissionFenceId == 0 || !DxgkReserveSubmissionFenceIdentity(Adapter, NodeOrdinal, Packet->SubmissionFenceId))
    {
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    Packet->FenceIdentityReserved = TRUE;
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
    Status = VidSchpAdmitPacket(Adapter, Packet, 0, &AdmittedFenceId);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    (VOID)VidSchpKickEngine(Engine, NULL);
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
    ULONG AdmittedFenceId;
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Context == NULL || DmaBufferGpuVa == 0 || DmaBufferSize == 0 || OutFenceId == NULL || (DriverPrivateDataSize != 0 && DriverPrivateData == NULL))
        return STATUS_INVALID_PARAMETER;
    /* A non-null virtual submission needs a miniport that executes GPU
     * virtual addresses; the caller has already validated the buffer range
     * against the submitting process's page tables. */
    if (!NullRendering && DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual) == NULL)
        return STATUS_NOT_SUPPORTED;

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
    Work->Packet = Packet;
    ExInitializeWorkItem(&Work->WorkItem, VidSchpVirtualSubmitWorker, Work);
    Packet->VirtualSubmitWorkItem = Work;
    Packet->SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
    if (Packet->SubmissionFenceId == 0 || !DxgkReserveSubmissionFenceIdentity(Adapter, Context->NodeOrdinal, Packet->SubmissionFenceId))
    {
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    Packet->FenceIdentityReserved = TRUE;
    Packet->DmaBufferGpuVa = DmaBufferGpuVa;
    /* Set before the pin so the unpin on teardown always sees the same span. */
    Packet->VirtualDmaBufferSize = DmaBufferSize;
    /*
     * Real submissions execute out of this GPU virtual address after this
     * call returns, so pin it now: validating it at entry and reading it in
     * the worker would leave a window to unmap or remap it in between.
     */
    if (!NullRendering)
    {
        PDXGKRNL_DEVICE PinDevice = (PDXGKRNL_DEVICE)Context->Device;

        if (PinDevice == NULL || PinDevice->ProcessRecord == NULL ||
            !DxgkGpuVaPinRange(Adapter, PinDevice->ProcessRecord, DmaBufferGpuVa, DmaBufferSize))
        {
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return STATUS_INVALID_PARAMETER;
        }
        Packet->GpuVaPinProcess = PinDevice->ProcessRecord;
    }
    Packet->Context = Context;
    Packet->VirtualAddressing = TRUE;
    Packet->SubmitFlags = NullRendering ? VIDSCH_SUBMITFLAG_NULLRENDERING : 0u;
    Status = DxgkDeviceWorkCreate(Context->Device, &Packet->DeviceWork);
    if (!NT_SUCCESS(Status))
    {
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
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
    Sched = VidSchpScheduler(Adapter);
    if (Sched == NULL)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_NOT_READY;
    }
    /* Hold the queue slot across the build so admission cannot fail after the
     * device work and the context order have already been committed. */
    Status = Sched->ReserveSlot(Sched->SchedulerHandle, Engine->EngineOrdinal);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    Status = DxgkDeviceWorkActivate(Packet->DeviceWork);
    if (!NT_SUCCESS(Status))
    {
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->EngineOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    Status = DxgkContextOrderAdmitPacket(Context, Packet);
    if (!NT_SUCCESS(Status))
    {
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->EngineOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    Packet->HoldsContextReference = TRUE;
    Status = VidSchpAdmitPacket(Adapter, Packet, DXGMMS2_SCHEDULER_ADMIT_CONSUME_RESERVATION, &AdmittedFenceId);
    if (!NT_SUCCESS(Status))
    {
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->EngineOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkContextOrderAbortPacket(Packet, Status);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    DxgkContextOrderPublishAdmittedPacket(Context);
    ExReleaseFastMutex(&Ctx->LifecycleMutex);
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
    ULONG AdmittedFenceId;
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
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
    if (!DxgkReserveSubmissionFenceIdentity(Adapter, NodeOrdinal, Packet->SubmissionFenceId))
    {
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    Packet->FenceIdentityReserved = TRUE;
    Packet->MiniportDeviceHandle = MiniportDeviceHandle;
    Packet->MiniportContextHandle = MiniportContextHandle;
    Packet->Priority = Priority;
    Packet->Tracked = TRUE;
    Packet->SubmitFlags = SubmitFlags;
    Packet->IsPresent = ((SubmitFlags & VIDSCH_SUBMITFLAG_PRESENT) != 0);
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
    if (TrackArgs->Context != NULL)
    {
        if (TrackArgs->Context->Device == NULL || TrackArgs->Context->Device->Adapter != Adapter || !DxgkReferenceContext(TrackArgs->Context))
        {
            DxgkCancelTrackedDmaBuffer(Reservation);
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return STATUS_DELETE_PENDING;
        }
        Packet->Context = TrackArgs->Context;
        Packet->HoldsContextReference = TRUE;
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

        if (!DxgkAcquireKmdCall(Adapter))
        {
            DxgkCancelTrackedDmaBuffer(Reservation);
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return STATUS_DELETE_PENDING;
        }

        _SEH2_TRY
        {
            Status = DXGK_CB_FULL(Adapter, DxgkDdiPatch)(Adapter->MiniportDeviceContext, &PatchArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        DxgkReleaseKmdCall(Adapter);

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
    Sched = VidSchpScheduler(Adapter);
    if (Sched == NULL)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkCancelTrackedDmaBuffer(Reservation);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_NOT_READY;
    }
    Status = Sched->ReserveSlot(Sched->SchedulerHandle, Engine->EngineOrdinal);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkCancelTrackedDmaBuffer(Reservation);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    Status = DxgkActivateTrackedDmaBuffer(Reservation);
    if (!NT_SUCCESS(Status))
    {
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->EngineOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkCancelTrackedDmaBuffer(Reservation);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    if (Packet->Context != NULL)
    {
        Status = DxgkContextOrderAdmitPacket((PDXGKRNL_CONTEXT)Packet->Context, Packet);
        if (!NT_SUCCESS(Status))
        {
            Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->EngineOrdinal);
            ExReleaseFastMutex(&Ctx->LifecycleMutex);
            DxgkCancelTrackedDmaBuffer(Reservation);
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return Status;
        }
    }
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    Packet->DmaBuffer = DmaBuffer;
    Packet->TrackerReservation = Reservation;
    DxgkAdoptTrackedDmaBuffer(Reservation);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    Status = VidSchpAdmitPacket(Adapter, Packet, DXGMMS2_SCHEDULER_ADMIT_CONSUME_RESERVATION, &AdmittedFenceId);
    if (!NT_SUCCESS(Status))
    {
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->EngineOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        if (Packet->ContextOrderOperation != NULL)
            DxgkContextOrderAbortPacket(Packet, Status);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    if (Packet->ContextOrderOperation != NULL)
        DxgkContextOrderPublishAdmittedPacket((PDXGKRNL_CONTEXT)Packet->Context);
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    if (Packet->ContextOrderOperation == NULL)
        (VOID)VidSchpKickEngine(Engine, NULL);
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
    PVIDSCH_ENGINE Engine = NULL;
    DXGMMS2_FENCE_SNAPSHOT_V1 FenceSnapshot;
    ULONG CurrentCompletedFence;
    ULONG FenceId = 0;
    ULONG LastSubmittedFence;
    ULONG NodeOrdinal = 0;
    BOOLEAN PreemptionNotification = FALSE;

    if (Adapter == NULL || NotifyData == NULL)
        return;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (NotifyData->InterruptType == DXGK_INTERRUPT_DMA_COMPLETED)
    {
        FenceId = NotifyData->DmaCompleted.SubmissionFenceId;
        NodeOrdinal = NotifyData->DmaCompleted.NodeOrdinal;
    }
    else if (NotifyData->InterruptType == DXGK_INTERRUPT_DMA_PREEMPTED)
    {
        FenceId = NotifyData->DmaPreempted.LastCompletedFenceId;
        NodeOrdinal = NotifyData->DmaPreempted.NodeOrdinal;
        PreemptionNotification = TRUE;
    }
    else
        return;

    if (InterlockedCompareExchange(&Adapter->TdrCompletionNotificationsEnabled, 0, 0) == 0)
        KeBugCheckEx(0x119, 0x10, (ULONG_PTR)NotifyData, (ULONG_PTR)Adapter, 0);

    if (NodeOrdinal >= Adapter->NodeCount || NodeOrdinal >= DXGK_MAX_TRACKED_NODES)
        VidSchpBugCheckInvalidFence(Adapter, FenceId, 0);
    LastSubmittedFence = (ULONG)InterlockedCompareExchange((volatile LONG *)&Adapter->NodeLastSubmittedFenceId[NodeOrdinal], 0, 0);
    if ((!PreemptionNotification && FenceId == 0) || LastSubmittedFence == 0 || (FenceId != 0 && !VidSchpFenceReached(LastSubmittedFence, FenceId)))
        VidSchpBugCheckInvalidFence(Adapter, FenceId, LastSubmittedFence);
    CurrentCompletedFence = (ULONG)InterlockedCompareExchange((volatile LONG *)&Adapter->NodeLastCompletedFenceId[NodeOrdinal], 0, 0);
    if ((!PreemptionNotification && !DxgkIsSubmittedFenceIdentity(Adapter, NodeOrdinal, FenceId)) || (PreemptionNotification && FenceId != 0 && !VidSchpFenceReached(CurrentCompletedFence, FenceId) && !DxgkIsSubmittedFenceIdentity(Adapter, NodeOrdinal, FenceId)))
        VidSchpBugCheckInvalidFence(Adapter, FenceId, LastSubmittedFence);

    if (Ctx != NULL && Ctx->Initialized && NodeOrdinal < Ctx->EngineCount)
        Engine = &Ctx->Engines[NodeOrdinal];

    if (PreemptionNotification)
    {
        ULONG PendingFence = Engine != NULL ? (ULONG)InterlockedCompareExchange(&Engine->PendingPreemptionFenceId, 0, 0) : 0;

        KeMemoryBarrier();
        if (Engine == NULL || PendingFence == 0 || PendingFence != NotifyData->DmaPreempted.PreemptionFenceId || Engine->PendingPreemptionEngineOrdinal != NotifyData->DmaPreempted.EngineOrdinal || (VidSchpReadState(Engine) != VidSchEnginePreempting && VidSchpReadState(Engine) != VidSchEngineResetting) || InterlockedCompareExchange(&Engine->PreemptionInterruptPending, 1, 0) != 0)
            VidSchpBugCheckInvalidFence(Adapter, FenceId, LastSubmittedFence);
    }

    if (!NT_SUCCESS(DxgkNotifySubmissionFenceCompletion(Adapter, NodeOrdinal, FenceId, PreemptionNotification, &FenceSnapshot)))
        VidSchpBugCheckInvalidFence(Adapter, FenceId, LastSubmittedFence);
    if (FenceId != 0 && Engine != NULL)
        VidSchpUpdateFence(&Engine->LastCompletedFence, FenceSnapshot.LastCompletedFence);

    if (Engine != NULL)
    {
        VidSchpReferenceActiveCall(Adapter);
        if (!KeInsertQueueDpc(&Engine->CompletionDpc, NULL, NULL))
            VidSchpReleaseCall(Adapter);
    }
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
VidSchBeginStopDrain(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    VIDSCH_SCHEDULER_STATE State;
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
    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    State = VidSchpReadSchedulerState(Ctx);
    if (State == VidSchSchedulerRunning)
    {
        Ctx->LifecycleState = (LONG)VidSchSchedulerSuspending;
        Status = STATUS_SUCCESS;
    }
    else if (State == VidSchSchedulerSuspending || State == VidSchSchedulerSuspended)
    {
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = STATUS_DEVICE_BUSY;
    }
    ExReleaseFastMutex(&Ctx->LifecycleMutex);
    VidSchpReleaseCall(Adapter);
    return Status;
}

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
        VIDSCH_ENGINE_STATE EngineState;
        VIDSCH_SCHEDULER_STATE SchedulerState;

        for (;;)
        {
            EngineState = VidSchpReadState(Engine);
            SchedulerState = VidSchpReadSchedulerState(Ctx);
            if (VidSchpEnginePendingCount(Adapter, Engine->EngineOrdinal) == 0 && (EngineState == VidSchEngineIdle || (EngineState == VidSchEngineSuspended && (SchedulerState == VidSchSchedulerSuspending || SchedulerState == VidSchSchedulerSuspended))))
                break;
            Status = KeWaitForSingleObject(&Engine->CompletionEvent, Executive, KernelMode, FALSE, &Timeout);
            if (Status == STATUS_TIMEOUT)
            {
                DXGKRNL_WARN("VidSch: WaitForIdle timeout on engine %lu "
                             "(state=%d pending=%lu)\n",
                             i, (int)VidSchpReadState(Engine),
                             VidSchpEnginePendingCount(Adapter, Engine->EngineOrdinal));
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
    NTSTATUS Status;

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

    {
        PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);
        DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 EngineStatus;

        if (Sched == NULL)
        {
            VidSchpReleaseCall(Adapter);
            return STATUS_DEVICE_NOT_READY;
        }
        RtlZeroMemory(&EngineStatus, sizeof(EngineStatus));
        EngineStatus.Size = DXGMMS2_SCHEDULER_ENGINE_STATUS_V1_SIZE;
        EngineStatus.Version = DXGMMS2_SCHEDULER_VERSION_1;
        Status = Sched->QueryEngineStatus(Sched->SchedulerHandle, Engine->EngineOrdinal, &EngineStatus);
        if (!NT_SUCCESS(Status))
        {
            VidSchpReleaseCall(Adapter);
            return Status;
        }
        *OutState              = (VIDSCH_ENGINE_STATE)EngineStatus.State;
        *OutPendingCount       = EngineStatus.PendingPacketCount;
        *OutLastSubmittedFence = EngineStatus.LastSubmittedFenceId;
        *OutLastCompletedFence = (ULONG)Engine->LastCompletedFence;
    }

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
    PDXGKRNL_ADAPTER DxgkAdapter = (PDXGKRNL_ADAPTER)Adapter;

    if (!VidSchpAcquireCall(DxgkAdapter))
        return;
    VidSchNotifyInterrupt(DxgkAdapter, NotifyData);
    VidSchpReleaseCall(DxgkAdapter);
}

static VOID NTAPI
VidSchpIfNotifyDpc(PVOID Adapter)
{
    VidSchNotifyDpc((PDXGKRNL_ADAPTER)Adapter);
}

static NTSTATUS NTAPI
VidSchpIfPreemptEngine(PVOID Adapter, ULONG EngineOrdinal)
{
    return VidSchPreemptEngine((PDXGKRNL_ADAPTER)Adapter, EngineOrdinal, 0, NULL);
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
    _In_ ULONG            EngineOrdinal,
    _Out_opt_ PULONG      PreemptionFenceId)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    DXGKARG_PREEMPTCOMMAND PreemptArgs;
    KIRQL CallIrql;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (PreemptionFenceId != NULL)
        *PreemptionFenceId = 0;
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

    RtlZeroMemory(&PreemptArgs, sizeof(PreemptArgs));
    PreemptArgs.PreemptionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
    if (PreemptArgs.PreemptionFenceId == 0)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_INTEGER_OVERFLOW;
    }
    PreemptArgs.NodeOrdinal = NodeOrdinal;
    PreemptArgs.EngineOrdinal = EngineOrdinal;

    /* Nothing running: nothing to preempt. */
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (!VidSchpTryTransitionEngine(Engine, VidSchEngineRunning, VidSchEnginePreempting))
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        VidSchpReleaseCall(Adapter);
        return STATUS_SUCCESS;
    }
    Engine->PendingPreemptionEngineOrdinal = EngineOrdinal;
    Engine->CompletedPreemptionEngineOrdinal = 0;
    InterlockedExchange(&Engine->CompletedPreemptionFenceId, 0);
    KeResetEvent(&Engine->PreemptionCompletedEvent);
    InterlockedExchange(&Engine->PendingPreemptionFenceId, (LONG)PreemptArgs.PreemptionFenceId);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

    if (!DxgkAcquireKmdCall(Adapter))
    {
        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        if ((ULONG)InterlockedCompareExchange(&Engine->PendingPreemptionFenceId, 0, 0) == PreemptArgs.PreemptionFenceId)
        {
            InterlockedExchange(&Engine->PendingPreemptionFenceId, 0);
            VidSchpTryTransitionEngine(Engine, VidSchEnginePreempting, VidSchEngineRunning);
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        VidSchpReleaseCall(Adapter);
        return STATUS_DELETE_PENDING;
    }

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if ((ULONG)InterlockedCompareExchange(&Engine->PendingPreemptionFenceId, 0, 0) != PreemptArgs.PreemptionFenceId || Engine->PendingPreemptionEngineOrdinal != EngineOrdinal || VidSchpReadState(Engine) != VidSchEnginePreempting || InterlockedCompareExchange(&Engine->PreemptionDdiState, 1, 0) != 0)
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        DxgkReleaseKmdCall(Adapter);
        VidSchpReleaseCall(Adapter);
        return STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

    KeRaiseIrql(DISPATCH_LEVEL, &CallIrql);
    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiPreemptCommand)(Adapter->MiniportDeviceContext, &PreemptArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    KeLowerIrql(CallIrql);
    if (NT_SUCCESS(Status))
        InterlockedCompareExchange(&Engine->PreemptionDdiState, 2, 1);
    DxgkReleaseKmdCall(Adapter);

    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR("VidSch: PreemptCommand failed 0x%08lX node=%lu engine=%lu\n", Status, NodeOrdinal, EngineOrdinal);
        VidSchpReleaseCall(Adapter);
        KeBugCheckEx(0x119, 0x2, (ULONG_PTR)Status, (ULONG_PTR)&PreemptArgs, (ULONG_PTR)Engine);
        return Status;
    }

    if (PreemptionFenceId != NULL)
        *PreemptionFenceId = PreemptArgs.PreemptionFenceId;
    VidSchpReleaseCall(Adapter);
    return Status;
}

NTSTATUS
VidSchWaitForPreemption(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG            NodeOrdinal,
    _In_ ULONG            EngineOrdinal,
    _In_ ULONG            PreemptionFenceId,
    _In_ ULONG            TimeoutMs)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL || PreemptionFenceId == 0)
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;
    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized || NodeOrdinal >= Ctx->EngineCount)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_PARAMETER;
    }
    Engine = &Ctx->Engines[NodeOrdinal];
    if ((ULONG)InterlockedCompareExchange(&Engine->CompletedPreemptionFenceId, 0, 0) == PreemptionFenceId && Engine->CompletedPreemptionEngineOrdinal == EngineOrdinal)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_SUCCESS;
    }
    Timeout.QuadPart = -(LONGLONG)TimeoutMs * 10 * 1000;
    Status = KeWaitForSingleObject(&Engine->PreemptionCompletedEvent, Executive, KernelMode, FALSE, &Timeout);
    if (NT_SUCCESS(Status) && ((ULONG)InterlockedCompareExchange(&Engine->CompletedPreemptionFenceId, 0, 0) != PreemptionFenceId || Engine->CompletedPreemptionEngineOrdinal != EngineOrdinal))
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
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
        if (VidSchpEnginePendingCount(Adapter, Engine->EngineOrdinal) != 0 || !VidSchpTryTransitionEngine(Engine, VidSchEngineIdle, VidSchEngineSuspended))
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

            VidSchpTryTransitionEngine(Engine, VidSchEngineSuspended, VidSchEngineResuming);
            VidSchpTryTransitionEngine(Engine, VidSchEngineResuming, VidSchEngineIdle);
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

        if (!VidSchpTryTransitionEngine(Engine, VidSchEngineSuspended, VidSchEngineResuming))
            break;
        if (!VidSchpTryTransitionEngine(Engine, VidSchEngineResuming, VidSchEngineIdle))
        {
            VidSchpTryTransitionEngine(Engine, VidSchEngineResuming, VidSchEngineSuspended);
            break;
        }
        ResumedCount++;
    }

    if (ResumedCount != Ctx->EngineCount)
    {
        while (ResumedCount != 0)
            VidSchpTryTransitionEngine(&Ctx->Engines[--ResumedCount], VidSchEngineIdle, VidSchEngineSuspended);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_DEVICE_STATE;
    }

    Ctx->LifecycleState = (LONG)VidSchSchedulerRunning;
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    for (i = 0; i < Ctx->EngineCount; i++)
        (VOID)VidSchpKickEngine(&Ctx->Engines[i], NULL);

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
    if (!DxgkAcquireKmdCall(Adapter))
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_DELETE_PENDING;
    }

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (VidSchpEnginePendingCount(Adapter, Engine->EngineOrdinal) != 0)
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkReleaseKmdCall(Adapter);
        VidSchpReleaseCall(Adapter);
        return STATUS_NOT_SUPPORTED;
    }

    OldState = VidSchpReadState(Engine);
    if (!VidSchpTryTransitionEngine(Engine, OldState, VidSchEngineResetting))
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkReleaseKmdCall(Adapter);
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
    DxgkReleaseKmdCall(Adapter);

    if (NT_SUCCESS(Status))
    {
        PDXGMMS2_SCHEDULER_INTERFACE_V1 ResetSched = VidSchpScheduler(Adapter);
        DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 ResetStatus;

        RtlZeroMemory(&ResetStatus, sizeof(ResetStatus));
        ResetStatus.Size = DXGMMS2_SCHEDULER_ENGINE_STATUS_V1_SIZE;
        ResetStatus.Version = DXGMMS2_SCHEDULER_VERSION_1;
        if (ResetSched == NULL || !NT_SUCCESS(ResetSched->QueryEngineStatus(ResetSched->SchedulerHandle, Engine->EngineOrdinal, &ResetStatus)))
            Status = STATUS_DEVICE_NOT_READY;
        else if ((LONG)(ResetArgs.LastAbortedFenceId - (ULONG)Engine->LastCompletedFence) < 0 || (LONG)(ResetStatus.LastSubmittedFenceId - ResetArgs.LastAbortedFenceId) < 0)
            Status = STATUS_DEVICE_PROTOCOL_ERROR;
    }

    if (NT_SUCCESS(Status))
    {
        VidSchpTryTransitionEngine(Engine, VidSchEngineResetting, VidSchEngineResetComplete);
        if (VidSchpReadSchedulerState(Ctx) == VidSchSchedulerSuspended)
            VidSchpTryTransitionEngine(Engine, VidSchEngineResetComplete, VidSchEngineSuspended);
        else
            VidSchpTryTransitionEngine(Engine, VidSchEngineResetComplete, VidSchEngineIdle);
    }
    else
    {
        VidSchpTryTransitionEngine(Engine, VidSchEngineResetting, VidSchEngineError);
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
        VIDSCH_ENGINE_STATE Observed;

        OldState = VidSchpReadState(Engine);
        if (OldState == NewState)
        {
            Status = STATUS_SUCCESS;
            break;
        }
        Status = VidSchpTransitionEngineEx(Engine, OldState, NewState, &Observed);
        if (NT_SUCCESS(Status))
            break;
        /* Same state observed and still rejected: the move is not in the
         * scheduler's table, so retrying cannot help. */
        if (Observed == OldState)
        {
            Status = STATUS_INVALID_DEVICE_STATE;
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
        if (EngineState != VidSchEngineResetting && !VidSchpTryTransitionEngine(Engine, EngineState, VidSchEngineResetting))
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
        VidSchpWaitForOutstandingWorkers(&Ctx->Engines[i]);
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
            InterlockedExchange(&Engine->PendingPreemptionFenceId, 0);
            InterlockedExchange(&Engine->PreemptionDdiState, 0);
            InterlockedExchange(&Engine->PreemptionInterruptPending, 0);
            /* Reset aborts queued/in-flight packets; it is not hardware
             * completion and must not advance the observed completion fence. */
            if (!VidSchpTryTransitionEngine(Engine, VidSchEngineResetting, VidSchEngineResetComplete) || !VidSchpTryTransitionEngine(Engine, VidSchEngineResetComplete, VidSchEngineSuspended))
                LifecycleRecovered = FALSE;
        }
        if (!ResetSucceeded)
            VidSchpTryTransitionEngine(Engine, VidSchEngineResetting, VidSchEngineError);
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        KeSetEvent(&Engine->CompletionEvent, IO_NO_INCREMENT, FALSE);
    }

    Ctx->LifecycleState = LifecycleRecovered ? (LONG)VidSchSchedulerSuspended : (LONG)VidSchSchedulerError;
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    if (ResetSucceeded)
    {
        PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);

        if (Sched != NULL)
        {
            (VOID)Sched->AbortAllPackets(Sched->SchedulerHandle, DXGMMS2_SCHEDULER_ABORT_INCLUDE_DISPATCHED, STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE);
            VidSchpDrainRetirements(Adapter);
        }
    }

    VidSchpReleaseCall(Adapter);
}

BOOLEAN
VidSchGetOldestKickedPacket(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ PULONG FenceId,
    _Out_ PULONG NodeOrdinal,
    _Out_ PULONG EngineOrdinal)
{
    PVIDSCH_CONTEXT Ctx;
    BOOLEAN Found = FALSE;
    ULONG Index;

    if (Adapter == NULL || FenceId == NULL || NodeOrdinal == NULL || EngineOrdinal == NULL)
        return FALSE;
    *FenceId = 0;
    *NodeOrdinal = 0;
    *EngineOrdinal = 0;
    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
        return FALSE;

    {
        PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);
        ULONG OldestEngine = 0;
        ULONG OldestFence = 0;
        ULONGLONG OldestCookie = 0;

        UNREFERENCED_PARAMETER(Index);
        if (Sched != NULL && Sched->GetOldestDispatched(Sched->SchedulerHandle, &OldestEngine, &OldestFence, &OldestCookie))
        {
            PVIDSCH_DMA_PACKET Packet = VidSchpPacketFromCookie(OldestCookie);

            if (Packet != NULL && OldestFence != 0)
            {
                *FenceId = OldestFence;
                *NodeOrdinal = Packet->NodeOrdinal;
                *EngineOrdinal = Packet->EngineOrdinal;
                Found = TRUE;
            }
        }
    }
    return Found;
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
