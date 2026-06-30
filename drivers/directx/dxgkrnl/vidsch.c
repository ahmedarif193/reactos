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

/*
 * VidSchpTryTransition — attempt an atomic engine state transition.
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
    LONG Old = InterlockedCompareExchange(StateField, New, Expected);

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
    /* A volatile read is sufficient on x86/amd64 (TSO). */
    return (VIDSCH_ENGINE_STATE)Engine->State;
}

/* Forward declaration. */
static VOID VidSchpKickEngine(_In_ PVIDSCH_ENGINE Engine);

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
    LONG ActualState;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Engine == NULL)
        return;

    Completed = Engine->LastCompletedFence;
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

        if (!VidSchpFenceReached(Completed, (LONG)Packet->SubmissionFenceId))
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
        /* Try RUNNING -> COMPLETING -> IDLE */
        if (VidSchpTryTransition(&Engine->State,
                                 (LONG)VidSchEngineRunning,
                                 (LONG)VidSchEngineCompleting,
                                 &ActualState))
        {
            InterlockedExchange(&Engine->State, (LONG)VidSchEngineIdle);
        }
    }
    else
    {
        /* More packets remain — kick the engine to submit the next one. */
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

        ExFreePoolWithTag(Packet, TAG_VIDSCH);
    }
}

/* ========================================================================
 * TDR timer DPC callback (Phase 1 stub)
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

    /* Phase 1: TDR detection is not yet implemented. */
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
    LONG ActualState;
    DXGKARG_SUBMITCOMMAND SubmitArgs;

    if (IsListEmpty(&Engine->RunQueueHead))
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
                              &ActualState))
    {
        return;
    }

    /* Peek the head packet (don't dequeue — it stays until completion). */
    Link = Engine->RunQueueHead.Flink;
    Packet = CONTAINING_RECORD(Link, VIDSCH_DMA_PACKET, RunQueueEntry);

    Engine->LastSubmittedFence = (LONG)Packet->SubmissionFenceId;

    /*
     * Build the DXGKARG_SUBMITCOMMAND for the miniport.
     * The miniport reads the DMA buffer and programs the GPU to execute it.
     */
    RtlZeroMemory(&SubmitArgs, sizeof(SubmitArgs));
    SubmitArgs.pDmaBuffer                    = Packet->DmaBufferVa;
    SubmitArgs.DmaBufferSize                 = Packet->DmaBufferSize;
    SubmitArgs.pDmaBufferPrivateData         = Packet->DriverPrivateData;
    SubmitArgs.DmaBufferPrivateDataSize      = Packet->DriverPrivateDataSize;
    SubmitArgs.DmaBufferSubmissionStartOffset = 0;
    SubmitArgs.DmaBufferSubmissionEndOffset   = Packet->DmaBufferSize;
    SubmitArgs.pAllocationList               = (CONST D3DDDI_ALLOCATIONLIST *)Packet->AllocationList;
    SubmitArgs.AllocationListSize             = Packet->AllocationListSize;
    SubmitArgs.SubmissionFenceId             = Packet->SubmissionFenceId;
    SubmitArgs.NodeOrdinal                   = Packet->NodeOrdinal;
    SubmitArgs.EngineOrdinal                 = Packet->EngineOrdinal;
    SubmitArgs.hContext                      = (HANDLE)Packet->Context;
    SubmitArgs.Flags                         = Packet->IsPresent ? 1 : 0;

    /*
     * Call the miniport's DxgkDdiSubmitCommand.
     * This is a full-WDDM-only callback — DOD adapters don't have it.
     */
    if (DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand) != NULL)
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand)(
                     Adapter->MiniportDeviceContext,
                     &SubmitArgs);

        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("VidSch: DxgkDdiSubmitCommand failed 0x%08lX "
                        "engine=%lu fence=%lu\n",
                        Status, Engine->EngineOrdinal,
                        Packet->SubmissionFenceId);

            /* Revert to IDLE on failure so the engine isn't stuck. */
            InterlockedExchange(&Engine->State, (LONG)VidSchEngineIdle);
            return;
        }
    }

    /* Transition SUBMITTING -> RUNNING. */
    VidSchpTryTransition(&Engine->State,
                         (LONG)VidSchEngineSubmitting,
                         (LONG)VidSchEngineRunning,
                         NULL);
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
    Ctx->Adapter     = Adapter;
    Ctx->EngineCount = Adapter->NodeCount;

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

        Engine->Adapter        = Adapter;
        Engine->EngineOrdinal  = i;
        Engine->State          = (LONG)VidSchEngineIdle;
        Engine->NextFenceId    = 1;

        KeInitializeSpinLock(&Engine->QueueLock);
        InitializeListHead(&Engine->RunQueueHead);
        KeInitializeEvent(&Engine->CompletionEvent, SynchronizationEvent, FALSE);

        KeInitializeDpc(&Engine->CompletionDpc,
                        VidSchpCompletionDpcRoutine,
                        Engine);

        KeInitializeTimer(&Engine->TdrTimer);
        KeInitializeDpc(&Engine->TdrDpc,
                        VidSchpTdrDpcRoutine,
                        Engine);
    }

    Ctx->Initialized = TRUE;
    Adapter->VidSchContext = Ctx;

    DXGKRNL_TRACE("VidSch: initialized %lu engines for adapter %p\n",
                  Ctx->EngineCount, Adapter);

    return STATUS_SUCCESS;
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

    /* Cancel all TDR timers and drain run queues. */
    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];
        KIRQL OldIrql;
        PLIST_ENTRY Link;

        KeCancelTimer(&Engine->TdrTimer);

        /* Drain any remaining packets from the run queue. */
        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        while (!IsListEmpty(&Engine->RunQueueHead))
        {
            PVIDSCH_DMA_PACKET Packet;

            Link = RemoveHeadList(&Engine->RunQueueHead);
            Packet = CONTAINING_RECORD(Link, VIDSCH_DMA_PACKET, RunQueueEntry);
            Engine->PendingPacketCount--;

            ExFreePoolWithTag(Packet, TAG_VIDSCH);
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    }

    if (Ctx->Engines != NULL)
        ExFreePoolWithTag(Ctx->Engines, TAG_VIDSCH);

    ExFreePoolWithTag(Ctx, TAG_VIDSCH);
    Adapter->VidSchContext = NULL;

    DXGKRNL_TRACE("VidSch: destroyed for adapter %p\n", Adapter);
}

/*
 * VidSchSubmitCommand
 */
NTSTATUS
VidSchSubmitCommand(
    _In_  PDXGKRNL_ADAPTER Adapter,
    _In_  ULONG            EngineOrdinal,
    _In_  PVOID            DmaBufferVa,
    _In_  ULONG            DmaBufferSize,
    _In_opt_ PVOID         DriverPrivateData,
    _In_  ULONG            DriverPrivateDataSize,
    _In_opt_ PVOID         AllocationList,
    _In_  ULONG            AllocationListSize,
    _In_opt_ PVOID         PatchLocationList,
    _In_  ULONG            PatchLocationListSize,
    _In_opt_ PVOID         Context,
    _In_  BOOLEAN          IsPresent,
    _Out_ ULONG           *OutFenceId)
{
    PVIDSCH_CONTEXT Ctx;
    PVIDSCH_ENGINE Engine;
    PVIDSCH_DMA_PACKET Packet;
    KIRQL OldIrql;
    VIDSCH_ENGINE_STATE CurrentState;

    PAGED_CODE();

    if (Adapter == NULL || OutFenceId == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutFenceId = 0;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
        return STATUS_DEVICE_NOT_READY;

    if (EngineOrdinal >= Ctx->EngineCount)
        return STATUS_INVALID_PARAMETER;

    Engine = &Ctx->Engines[EngineOrdinal];

    /* Validate engine state — only IDLE or RUNNING can accept new work. */
    CurrentState = VidSchpReadState(Engine);
    if (CurrentState != VidSchEngineIdle &&
        CurrentState != VidSchEngineRunning)
    {
        DXGKRNL_WARN("VidSch: submit rejected — engine %lu in state %d\n",
                     EngineOrdinal, (int)CurrentState);
        return STATUS_DEVICE_BUSY;
    }

    /* Allocate the DMA packet descriptor. */
    Packet = ExAllocatePoolWithTag(NonPagedPool,
                                   sizeof(VIDSCH_DMA_PACKET),
                                   TAG_VIDSCH);
    if (Packet == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Packet, sizeof(*Packet));

    /* Assign a monotonically increasing fence ID for this engine. */
    Packet->SubmissionFenceId    = (ULONG)InterlockedIncrement(&Engine->NextFenceId);
    Packet->EngineOrdinal        = EngineOrdinal;
    Packet->NodeOrdinal          = EngineOrdinal; /* 1:1 for now */
    Packet->DmaBufferVa          = DmaBufferVa;
    Packet->DmaBufferSize        = DmaBufferSize;
    Packet->DriverPrivateData    = DriverPrivateData;
    Packet->DriverPrivateDataSize = DriverPrivateDataSize;
    Packet->AllocationList       = AllocationList;
    Packet->AllocationListSize   = AllocationListSize;
    Packet->PatchLocationList    = PatchLocationList;
    Packet->PatchLocationListSize = PatchLocationListSize;
    Packet->Context              = Context;
    Packet->IsPresent            = IsPresent;

    *OutFenceId = Packet->SubmissionFenceId;

    /* Enqueue the packet and try to kick the engine. */
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);

    InsertTailList(&Engine->RunQueueHead, &Packet->RunQueueEntry);
    Engine->PendingPacketCount++;

    /* If the engine is idle, kick it to start processing. */
    VidSchpKickEngine(Engine);

    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

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
    ULONG EngineOrdinal;

    if (Adapter == NULL || NotifyData == NULL)
        return;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
        return;

    if (NotifyData->InterruptType == DXGK_INTERRUPT_DMA_COMPLETED)
    {
        FenceId       = NotifyData->DmaCompleted.SubmissionFenceId;
        EngineOrdinal = NotifyData->DmaCompleted.NodeOrdinal;
    }
    else if (NotifyData->InterruptType == DXGK_INTERRUPT_DMA_PREEMPTED)
    {
        FenceId       = NotifyData->DmaPreempted.LastCompletedFenceId;
        EngineOrdinal = NotifyData->DmaPreempted.NodeOrdinal;
    }
    else
    {
        /* Other interrupt types are not handled by VidSch Phase 1. */
        return;
    }

    if (EngineOrdinal >= Ctx->EngineCount)
        return;

    Engine = &Ctx->Engines[EngineOrdinal];

    /* Update the completed fence — atomic, safe at DIRQL. */
    InterlockedExchange(&Engine->LastCompletedFence, (LONG)FenceId);

    /* Queue the completion DPC for deferred processing. */
    KeInsertQueueDpc(&Engine->CompletionDpc, NULL, NULL);
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

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
        return STATUS_NOT_SUPPORTED;

    /* Convert milliseconds to 100ns units (negative = relative). */
    Timeout.QuadPart = -(LONGLONG)TimeoutMs * 10000LL;

    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];

        while (VidSchpReadState(Engine) != VidSchEngineIdle ||
               Engine->PendingPacketCount > 0)
        {
            Status = KeWaitForSingleObject(&Engine->CompletionEvent,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           &Timeout);
            if (Status == STATUS_TIMEOUT)
            {
                DXGKRNL_WARN("VidSch: WaitForIdle timeout on engine %lu "
                             "(state=%d pending=%lu)\n",
                             i, (int)VidSchpReadState(Engine),
                             Engine->PendingPacketCount);
                return STATUS_TIMEOUT;
            }
        }
    }

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

    if (Adapter == NULL || OutState == NULL)
        return STATUS_INVALID_PARAMETER;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL)
        return STATUS_DEVICE_NOT_READY;

    if (EngineOrdinal >= Ctx->EngineCount)
        return STATUS_INVALID_PARAMETER;

    Engine = &Ctx->Engines[EngineOrdinal];

    *OutState              = VidSchpReadState(Engine);
    *OutPendingCount       = Engine->PendingPacketCount;
    *OutLastSubmittedFence = (ULONG)Engine->LastSubmittedFence;
    *OutLastCompletedFence = (ULONG)Engine->LastCompletedFence;

    return STATUS_SUCCESS;
}

/*
 * VidSchStartScheduler
 *
 * Phase 1 stub: scheduling is driven directly by submit and DPC paths.
 * On Windows this creates a system thread; not needed for Phase 1.
 */
NTSTATUS
VidSchStartScheduler(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    if (Adapter->VidSchContext == NULL)
        return STATUS_NOT_SUPPORTED;

    DXGKRNL_TRACE("VidSch: StartScheduler — using inline scheduling "
                  "(no worker thread in Phase 1)\n");

    return STATUS_SUCCESS;
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
    PVIDSCH_ENGINE Engine;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || OutFenceId == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutFenceId = 0;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        /*
         * No scheduler — fall through to legacy present path.
         * present.c will handle this directly.
         */
        return STATUS_NOT_SUPPORTED;
    }

    if (EngineOrdinal >= Ctx->EngineCount)
        return STATUS_INVALID_PARAMETER;

    Engine = &Ctx->Engines[EngineOrdinal];

    if (FlipInterval == D3DDDI_FLIPINTERVAL_IMMEDIATE)
    {
        /*
         * Immediate flip: submit directly to the engine.
         * The miniport recognizes present submissions by the Flags field
         * (IsPresent = TRUE) and the VidPnSourceId.
         */
        Status = VidSchSubmitCommand(
                     Adapter,
                     EngineOrdinal,
                     NULL,          /* No DMA buffer — flip is a control op */
                     0,
                     NULL,          /* No private data */
                     0,
                     NULL,          /* No allocation list */
                     0,
                     NULL,          /* No patch location list */
                     0,
                     Context,
                     TRUE,          /* IsPresent */
                     OutFenceId);

        return Status;
    }

    /*
     * VSync-synchronized flip: queue into the present queue.
     * The VSync DPC path (DxgkpNotifyVSync -> DxgkpProcessPresentQueue)
     * will dequeue and execute at the appropriate VBlank.
     *
     * For Phase 1 we assign a fence ID for tracking but let the present
     * queue handle the actual VSync synchronization and miniport call.
     */
    *OutFenceId = (ULONG)InterlockedIncrement(&Engine->NextFenceId);

    DXGKRNL_TRACE("VidSch: FlipPresent queued — engine=%lu vidpn=%lu "
                  "interval=%d fence=%lu\n",
                  EngineOrdinal, VidPnSourceId,
                  (int)FlipInterval, *OutFenceId);

    return STATUS_SUCCESS;
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
    return VidSchPreemptEngine((PDXGKRNL_ADAPTER)Adapter, EngineOrdinal);
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
    return VidSchSetEngineState((PDXGKRNL_ADAPTER)Adapter, EngineOrdinal,
                                (VIDSCH_ENGINE_STATE)NewState);
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
    _In_ ULONG            EngineOrdinal)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(EngineOrdinal);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
VidSchSuspendScheduler(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    UNREFERENCED_PARAMETER(Adapter);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
VidSchResumeScheduler(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    UNREFERENCED_PARAMETER(Adapter);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
VidSchResetEngine(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG            EngineOrdinal)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(EngineOrdinal);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
VidSchSetEngineState(
    _In_ PDXGKRNL_ADAPTER    Adapter,
    _In_ ULONG                EngineOrdinal,
    _In_ VIDSCH_ENGINE_STATE  NewState)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(EngineOrdinal);
    UNREFERENCED_PARAMETER(NewState);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
VidSchSetSchedulerCallback(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PVOID             CallbackContext)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(CallbackContext);
    return STATUS_NOT_IMPLEMENTED;
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

    return STATUS_NOT_IMPLEMENTED;
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
