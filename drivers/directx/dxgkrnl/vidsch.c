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
 * Windows 11 routes this work through dxgmms2.sys. This ReactOS implementation
 * provides the engine state machine, command submission, interrupt/DPC
 * completion, and fence tracking used by dxgkrnl.
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
#include "presenttrace.h"
#include "vidsch.h"

#include "vidsch_policy_core.h"
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
static __inline VIDSCH_ENGINE_STATE
VidSchpReadState(
    _In_ PVIDSCH_ENGINE Engine)
{
    return VidSchpEngineState(Engine->Adapter, Engine->SchedulerOrdinal);
}

static __inline VIDSCH_SCHEDULER_STATE
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
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
static VOID VidSchpQueueFaultCleanup(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS CompletionStatus);
#endif

FORCEINLINE PVIDSCH_DMA_PACKET
VidSchpPacketFromCookie(_In_ ULONGLONG Cookie)
{
    return (PVIDSCH_DMA_PACKET)(ULONG_PTR)Cookie;
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
static PVIDSCH_DMA_PACKET
VidSchpFirstActivePacketLocked(
    _In_ PVIDSCH_ENGINE Engine)
{
    if (IsListEmpty(&Engine->ActivePacketList))
        return NULL;

    return CONTAINING_RECORD(Engine->ActivePacketList.Flink,
                             VIDSCH_DMA_PACKET,
                             ActiveEngineEntry);
}

static VOID
VidSchpPublishActivePacket(
    _Inout_ PVIDSCH_DMA_PACKET Packet)
{
    PVIDSCH_ENGINE Engine;
    KIRQL OldIrql;

    if (Packet == NULL || Packet->OwnerEngine == NULL)
        return;

    Engine = Packet->OwnerEngine;
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    ASSERT(IsListEmpty(&Packet->ActiveEngineEntry));
    if (IsListEmpty(&Packet->ActiveEngineEntry))
        InsertTailList(&Engine->ActivePacketList,
                       &Packet->ActiveEngineEntry);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
}

/*
 * Snapshot the fault marker and unpublish a terminal packet while its
 * provider-owned reference is still live.  The same lock serializes this
 * boundary with page-fault marking.
 */
static BOOLEAN
VidSchpUnpublishTerminalPacket(
    _Inout_ PVIDSCH_DMA_PACKET Packet,
    _Out_ NTSTATUS *FaultStatus)
{
    PVIDSCH_ENGINE Engine;
    KIRQL OldIrql;
    BOOLEAN Faulted;

    *FaultStatus = STATUS_SUCCESS;
    Engine = Packet->OwnerEngine;
    if (Engine == NULL)
        return FALSE;

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (!IsListEmpty(&Packet->ActiveEngineEntry))
    {
        RemoveEntryList(&Packet->ActiveEngineEntry);
        InitializeListHead(&Packet->ActiveEngineEntry);
    }
    Faulted =
        InterlockedCompareExchange(&Packet->Faulted, 0, 0) != 0;
    if (Faulted)
        *FaultStatus = Packet->FaultStatus;
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    return Faulted;
}

/*
 * Transfer a miniport-accepted tracker away from packet destruction before
 * terminalizing it.  The adapter helper performs only DPC-safe state/list
 * work here and leaves all resource destruction to its PASSIVE worker.
 */
static VOID
VidSchpFailTrackedReservation(
    _Inout_ PVIDSCH_DMA_PACKET Packet)
{
    PDXGKRNL_SUBMIT_DMA_BUFFER Reservation = NULL;
    PVIDSCH_ENGINE Engine;
    KIRQL OldIrql;

    Engine = Packet->OwnerEngine;
    if (Engine == NULL)
        return;

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (Packet->TrackerOwnsDmaBuffer &&
        Packet->TrackerReservation != NULL)
    {
        Reservation = Packet->TrackerReservation;
        Packet->TrackerReservation = NULL;
        Packet->DmaBuffer = NULL;
        Packet->TrackerOwnsDmaBuffer = FALSE;
    }
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

    if (Reservation != NULL)
    {
        (VOID)DxgkFailTrackedDmaBuffer(
            Engine->Adapter,
            Reservation);
    }
}
#endif

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
    /*
     * dxgmms2 exposes a flattened scheduler-slot field here. Public WDDM
     * EngineOrdinal was validated separately and remains zero.
     */
    Info.EngineOrdinal = Packet->OwnerEngine->SchedulerOrdinal;
    Info.Flags = Flags;
    /* Snapshot the context at the common admission boundary. Physical
     * escapes and GPU-VA submissions must not silently become priority zero
     * just because their caller did not fill the packet's default priority. */
    if (Packet->Context != NULL)
        Packet->Priority = InterlockedCompareExchange(
            &((PDXGKRNL_CONTEXT)Packet->Context)->SchedulingPriority, 0, 0);
    Info.Priority = Packet->Priority;
    Info.PacketCookie = (ULONGLONG)(ULONG_PTR)Packet;
    Info.OwnerCookie = (ULONGLONG)(ULONG_PTR)
        VidSchPolicyOwnerCookie(Packet->Context, Packet->Device);
    if (Packet->SubmissionFenceId != 0)
    {
        Info.Flags |= DXGMMS2_SCHEDULER_ADMIT_PREFENCED;
        Info.SubmissionFenceId = Packet->SubmissionFenceId;
    }
    Packet->SchedulerCookie = Info.PacketCookie;
    {
        NTSTATUS Status;
        DPT_SCOPE Trace = DptBegin(&g_DxgPresentTrace, DPT_QUEUE);
        Packet->PresentationQueueTrace = Trace;
        Status = Sched->AdmitPacket(Sched->SchedulerHandle, &Info, OutFenceId);
        /* After successful publication, Packet may already have retired. */
        if (!NT_SUCCESS(Status))
        {
            DptEnd(&g_DxgPresentTrace, Trace, FALSE, 0);
            Packet->PresentationQueueTrace.Epoch = 0;
        }
        return Status;
    }
}

/* TDR diagnostics: remember what was handed to the miniport.  Called right
 * before the submit DDI with QueueLock available (any IRQL <= DISPATCH). */
static VOID
VidSchpRecordDispatch(
    _In_ PVIDSCH_ENGINE Engine,
    _In_ PVIDSCH_DMA_PACKET Packet)
{
    PVIDSCH_DISPATCH_RECORD Record;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    DptEnd(&g_DxgPresentTrace, Packet->PresentationQueueTrace, TRUE, 0);
    Packet->PresentationQueueTrace.Epoch = 0;
    if (!Packet->PresentationRetireTrace.Epoch)
        Packet->PresentationRetireTrace = DptBegin(&g_DxgPresentTrace, DPT_RETIRE);
    Record = &Engine->DispatchRing[Engine->DispatchRingNext % VIDSCH_DISPATCH_RING_SIZE];
    Engine->DispatchRingNext++;
    RtlZeroMemory(Record, sizeof(*Record));
    Record->Fence = Packet->SubmissionFenceId;
    Record->NodeOrdinal = Packet->NodeOrdinal;
    Engine->LastDispatchDmaGpuVa = Packet->DmaBufferGpuVa;
    Engine->LastDispatchDmaSize = Packet->VirtualDmaBufferSize;
    Engine->LastDispatchFence = Packet->SubmissionFenceId;
    Engine->LastDispatchProcess = Packet->GpuVaPinProcess;
    Record->Context = Packet->Context;
    Record->MiniportContext = Packet->MiniportContextHandle;
    Record->GpuVa = Packet->DmaBufferGpuVa;
    Record->Size = Packet->VirtualAddressing ? Packet->VirtualDmaBufferSize :
                   (Packet->DmaBuffer != NULL ? Packet->DmaBuffer->Capacity : 0);
    Record->SubmitFlags = Packet->SubmitFlags;
    Record->PrivateDataSize = Packet->DriverPrivateDataSize;
    Record->UmdPrivateDataSize = Packet->UmdPrivateDataSize;
    Record->AdmitTime100ns = Packet->AdmitTime100ns;
    Record->DispatchTime100ns = DxgkDiagNow100ns();
    Record->AdmitSequence = Packet->AdmitSequence;
    Record->DispatchSequence = DxgkDiagSequence();
    Record->Virtual = Packet->VirtualAddressing;
    Record->Ordered = Packet->ContextOrderOperation != NULL;
    Packet->DispatchTime100ns = Record->DispatchTime100ns;
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
}

static VOID
VidSchpRecordCompletion(
    _In_ PVIDSCH_DMA_PACKET Packet)
{
    PVIDSCH_ENGINE Engine = Packet->OwnerEngine;
    KIRQL OldIrql;
    ULONG Index;

    if (Engine == NULL)
        return;
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    for (Index = 0; Index < VIDSCH_DISPATCH_RING_SIZE; Index++)
    {
        PVIDSCH_DISPATCH_RECORD Record = &Engine->DispatchRing[Index];

        if (Record->DispatchTime100ns != 0 &&
            Record->Fence == Packet->SubmissionFenceId &&
            Record->CompleteTime100ns == 0)
        {
            Record->CompleteTime100ns = DxgkDiagNow100ns();
            Record->CompleteSequence = DxgkDiagSequence();
            break;
        }
    }
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
}

static VOID VidSchpDumpBatchHeadBytes(_In_reads_bytes_(Bytes) const UCHAR *Head, _In_ ULONG Bytes, _In_ ULONG DmaSize, _In_ PCSTR Tag);
static BOOLEAN VidSchpFindStateBaseAddress(_In_reads_bytes_(Bytes) const UCHAR *Head, _In_ ULONG Bytes, _Inout_ ULONG *Word, _Out_writes_(5) ULONGLONG *Bases, _Out_writes_(5) BOOLEAN *Enabled);
static VOID VidSchpNoteStateBaseAddress(_In_ PDXGKRNL_CONTEXT Context, _In_reads_bytes_(Bytes) const UCHAR *Head, _In_ ULONG Bytes);

static LONGLONG
VidSchpAgeUs(
    _In_ ULONGLONG Now100ns,
    _In_ ULONGLONG Then100ns)
{
    if (Then100ns == 0)
        return -1;
    return (LONGLONG)(Now100ns - Then100ns) / 10;
}

VOID
VidSchDumpEngineDiagnostics(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    ULONGLONG Now = DxgkDiagNow100ns();
    ULONG EngineIndex;

    if (Adapter == NULL)
        return;
    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized || Ctx->Engines == NULL)
        return;

    for (EngineIndex = 0; EngineIndex < Ctx->EngineCount; EngineIndex++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[EngineIndex];
        VIDSCH_DISPATCH_RECORD Ring[VIDSCH_DISPATCH_RING_SIZE];
        VIDSCH_ENGINE_STATE State = VidSchEngineError;
        ULONG Pending = 0, LastSubmitted = 0, LastCompleted = 0;
        ULONG Next;
        ULONG Index;
        KIRQL OldIrql;

        UCHAR OldestHead[256];
        ULONG OldestHeadBytes = 0;
        ULONG OldestFence = 0;
        ULONG OldestSize = 0;
        D3DGPU_VIRTUAL_ADDRESS OldestVa = 0;
        PVIDSCH_DMA_PACKET Oldest;

        (VOID)VidSchQueryEngineStatus(Adapter, EngineIndex, &State, &Pending, &LastSubmitted, &LastCompleted);
        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        RtlCopyMemory(Ring, Engine->DispatchRing, sizeof(Ring));
        Next = Engine->DispatchRingNext;
        /* The oldest active packet is the one the engine is stuck on. */
        Oldest = VidSchpFirstActivePacketLocked(Engine);
        if (Oldest != NULL)
        {
            OldestFence = Oldest->SubmissionFenceId;
            OldestVa = Oldest->DmaBufferGpuVa;
            OldestSize = Oldest->VirtualDmaBufferSize;
            OldestHeadBytes = min(Oldest->BatchHeadBytes, sizeof(OldestHead));
            if (OldestHeadBytes != 0)
                RtlCopyMemory(OldestHead, Oldest->BatchHead, OldestHeadBytes);
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

        DXGKRNL_ERR("TDR engine %lu: state=%d pending=%lu last-submitted=%lu last-completed=%lu isr-fence=%ld workers=%ld\n",
                    EngineIndex, State, Pending, LastSubmitted, LastCompleted,
                    Engine->LastCompletedFence, Engine->OutstandingWorkers);
        if (Oldest != NULL)
        {
            DXGKRNL_ERR("TDR engine %lu oldest active packet: fence=%lu dma va=0x%I64x size=%lu\n",
                        EngineIndex, OldestFence, OldestVa, OldestSize);
            VidSchpDumpBatchHeadBytes(OldestHead, OldestHeadBytes, OldestSize, "TDR");
        }
        for (Index = 0; Index < VIDSCH_DISPATCH_RING_SIZE; Index++)
        {
            PVIDSCH_DISPATCH_RECORD Record = &Ring[(Next + Index) % VIDSCH_DISPATCH_RING_SIZE];

            if (Record->DispatchTime100ns == 0)
                continue;
            DXGKRNL_ERR("TDR   fence=%lu seq(admit/dispatch/complete)=#%I64d/#%I64d/#%I64d node=%lu ctx=%p hctx=%p va=0x%I64x size=%lu flags=0x%lx priv=%lu/%lu virt=%d ordered=%d admit=-%I64dus dispatch=-%I64dus complete=%s%I64dus queue-lat=%I64dus\n",
                        Record->Fence, Record->AdmitSequence, Record->DispatchSequence, Record->CompleteSequence,
                        Record->NodeOrdinal, Record->Context, Record->MiniportContext,
                        Record->GpuVa, Record->Size, Record->SubmitFlags,
                        Record->PrivateDataSize, Record->UmdPrivateDataSize,
                        Record->Virtual, Record->Ordered,
                        VidSchpAgeUs(Now, Record->AdmitTime100ns),
                        VidSchpAgeUs(Now, Record->DispatchTime100ns),
                        Record->CompleteTime100ns != 0 ? "-" : "never ",
                        Record->CompleteTime100ns != 0 ? VidSchpAgeUs(Now, Record->CompleteTime100ns) : 0,
                        (LONGLONG)(Record->DispatchTime100ns - Record->AdmitTime100ns) / 10);
        }
    }
    if (KeGetCurrentIrql() <= APC_LEVEL)
    {
        for (EngineIndex = 0; EngineIndex < Ctx->EngineCount; EngineIndex++)
        {
            PVIDSCH_ENGINE Engine = &Ctx->Engines[EngineIndex];
            PVIDSCH_DMA_PACKET Packet = NULL;
            PDXGKRNL_PROCESS Process;
            KIRQL OldIrql;

            /* Fault recovery has its own referenced capture. For a silent
             * hang, retain a currently active packet instead of following
             * LastDispatchProcess after completion or owner teardown. */
            KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
            if (Engine->FaultDumpQueued == 0)
            {
                Packet = VidSchpFirstActivePacketLocked(Engine);
                if (Packet != NULL)
                    InterlockedIncrement(&Packet->ReferenceCount);
            }
            KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
            if (Packet == NULL)
                continue;

            Process = Packet->GpuVaPinProcess;
            if (Process == NULL && Packet->Device != NULL)
                Process = Packet->Device->ProcessRecord;
            if (Process != NULL && Packet->DmaBufferGpuVa != 0)
            {
                DXGKRNL_ERR("VidSch: engine %lu stuck packet fence=%lu dma va=0x%I64x size=%lu process=%p\n",
                            EngineIndex, Packet->SubmissionFenceId,
                            Packet->DmaBufferGpuVa, Packet->VirtualDmaBufferSize, Process);
                DxgkGpuVaVerifyProcessTables(Adapter, Process);
                DxgkGpuVaDumpBuffer(Process, Packet->DmaBufferGpuVa, Packet->VirtualDmaBufferSize);
                DxgkGpuVaDumpRecentEvents();
                DxgkGpuVaDumpProcessRanges(Process);
            }
            VidSchpDereferencePacket(Packet);
        }
    }
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
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
            NTSTATUS FaultStatus;
            BOOLEAN Faulted;
#endif

            if (Packet == NULL)
                continue;
            /* Every record here is a packet the miniport has handed back,
             * whatever the reason, so this is the one place the node's busy
             * charge has to close. */
            DptEnd(&g_DxgPresentTrace, Packet->PresentationQueueTrace, FALSE, 0);
            Packet->PresentationQueueTrace.Epoch = 0;
            DptEnd(&g_DxgPresentTrace, Packet->PresentationRetireTrace,
                   Records[Index].Reason == Dxgmms2RetireCompleted, 0);
            Packet->PresentationRetireTrace.Epoch = 0;
            VidSchAccountNodeRetire(Packet);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
            Faulted =
                VidSchpUnpublishTerminalPacket(Packet, &FaultStatus);
            if (!Faulted &&
                Records[Index].Reason == Dxgmms2RetireCompleted &&
                Packet->Device != NULL &&
                VidSchPolicyCompletionMustFail(
                    InterlockedCompareExchange(
                        &Packet->Device->ExecutionState, 0, 0),
                    D3DKMT_DEVICEEXECUTION_ERROR_DMAPAGEFAULT))
            {
                Faulted = TRUE;
                FaultStatus =
                    STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE;
            }
            if (Faulted)
            {
                VidSchpFailTrackedReservation(Packet);
                VidSchpQueueFaultCleanup(Packet, FaultStatus);
                continue;
            }
#endif
            if (Records[Index].Reason == Dxgmms2RetireCompleted)
            {
                VidSchpRecordCompletion(Packet);
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
    return NT_SUCCESS(Sched->SetEngineState(Sched->SchedulerHandle, Engine->SchedulerOrdinal, (ULONG)Expected, (ULONG)New, NULL));
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
    Status = Sched->SetEngineState(Sched->SchedulerHandle, Engine->SchedulerOrdinal, (ULONG)Expected, (ULONG)New, &Previous);
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
    return NT_SUCCESS(Sched->SetEngineState(Sched->SchedulerHandle, Engine->SchedulerOrdinal, DXGMMS2_ENGINE_STATE_ANY, (ULONG)New, NULL));
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
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    /* A root association may change only while this context is idle. Leave
     * the packet unclaimed until completion kicks its stream again. Other
     * contexts on the engine do not prevent this context's root update. */
    if (Packet->VirtualAddressing &&
        !(Packet->SubmitFlags & VIDSCH_SUBMITFLAG_NULLRENDERING) &&
        DxgkGpuVaRootPageTableNeedsUpdate(Engine->Adapter,
                                        Packet->Device->ProcessRecord,
                                        (PDXGKRNL_CONTEXT)Packet->Context))
    {
        PLIST_ENTRY Entry;
        BOOLEAN Active = FALSE;

        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        for (Entry = Engine->ActivePacketList.Flink;
             Entry != &Engine->ActivePacketList;
             Entry = Entry->Flink)
        {
            PVIDSCH_DMA_PACKET Other =
                CONTAINING_RECORD(Entry, VIDSCH_DMA_PACKET, ActiveEngineEntry);

            if (Other->Context == Packet->Context)
            {
                Active = TRUE;
                break;
            }
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        if (Active)
            return FALSE;
    }
#endif
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    /*
     * Engine state alone does not say whose turn it is.  Ask dxgmms2 which
     * packet a claim would hand out: authorising anything else would let the
     * kick claim a different packet and the caller cancel still-valid work.
     */
    Dispatchable = !Packet->Kicked && Engine->Scheduler != NULL &&
                   (VidSchpReadSchedulerState(Engine->Scheduler) == VidSchSchedulerRunning || VidSchpReadSchedulerState(Engine->Scheduler) == VidSchSchedulerSuspending) &&
                   Sched->PeekNextPacket(Sched->SchedulerHandle, Engine->SchedulerOrdinal, &HeadCookie) &&
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
    BOOLEAN Dispatched;

    if (Packet == NULL || Packet->OwnerEngine == NULL)
        return;
    if (Packet->ContextOrderAbortStatus != STATUS_PENDING)
    {
        DxgkContextOrderAbortPacket(Packet, Packet->ContextOrderAbortStatus);
        return;
    }
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
        /*
         * A completion DPC can claim this packet after the worker's peek,
         * then hand it back because only this worker may dispatch ordered
         * work.  Failure to acquire that temporary claim is not a failed
         * submission.  CancelOwnerPackets skips claimed packets: cancelling
         * the context action here would therefore leave a terminal packet
         * at the head of the provider queue, blocking every later job.
         *
         * Keep the context claim and retry when the holder returns its
         * provider claim and schedules this context.  Do not self-schedule
         * while the engine is unavailable; completion, reset and teardown
         * own the wake-up or cancellation.  A concurrent abort may already
         * have advanced the state, so never overwrite it.
         */
        (VOID)InterlockedCompareExchange(&Packet->ContextOrderState,
                                         VIDSCH_CONTEXT_ORDER_CLAIMED,
                                         VIDSCH_CONTEXT_ORDER_DISPATCHING);
    }
    VidSchpReleaseCall(Engine->Adapter);
}

static VOID VidSchpSubmitVirtualPacket(_In_ PVIDSCH_ENGINE Engine, _Inout_ PVIDSCH_DMA_PACKET Packet);
static VOID NTAPI VidSchpDestroyPacketWorker(_In_ PVOID Parameter);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
static VOID NTAPI VidSchpFaultCleanupWorker(_In_ PVOID Parameter);
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
/*
 * Print the submit-time copy of the faulted batch head and decode its
 * STATE_BASE_ADDRESS (Gen8+ layout: DW1-2 general, DW4-5 surface, DW6-7
 * dynamic, DW8-9 indirect object, DW10-11 instruction; bit 0 of the low
 * dword is the modify-enable).  A surface-state base of zero with modify
 * enabled explains a fault at a small address as base 0 + binding-table
 * offset; a valid base means the GPU executed something other than what the
 * CPU submitted (stale translation or coherency), which is the other branch.
 */
static VOID
VidSchpDumpCapturedBatchHead(
    _In_ PVIDSCH_ENGINE Engine)
{
    VidSchpDumpBatchHeadBytes(Engine->LastFaultBatchHead, Engine->LastFaultBatchHeadBytes, Engine->LastFaultDmaSize, "fault");
}
#endif

/* Gen8+ STATE_BASE_ADDRESS: DW1-2 general, DW4-5 surface, DW6-7 dynamic,
 * DW8-9 indirect object, DW10-11 instruction; bit 0 of the low dword is the
 * modify-enable.  Returns FALSE when the head carries no such command. */
static BOOLEAN
VidSchpFindStateBaseAddress(
    _In_reads_bytes_(Bytes) const UCHAR *Head,
    _In_ ULONG Bytes,
    _Inout_ ULONG *Word,
    _Out_writes_(5) ULONGLONG *Bases,
    _Out_writes_(5) BOOLEAN *Enabled)
{
    static const ULONG LowDword[5] = { 1, 4, 6, 8, 10 };
    const ULONG *Words = (const ULONG *)Head;
    ULONG Count = Bytes / sizeof(ULONG);
    ULONG i, Base;

    for (i = *Word; i + 11 < Count; i++)
    {
        if ((Words[i] & 0xFFFF0000UL) != 0x61010000UL)
            continue;
        for (Base = 0; Base < 5; Base++)
        {
            ULONG Low = Words[i + LowDword[Base]];

            Bases[Base] = ((((ULONGLONG)Words[i + LowDword[Base] + 1]) << 32) | Low) & ~0xFFFULL;
            Enabled[Base] = (Low & 1) != 0;
        }
        *Word = i + 12;
        return TRUE;
    }
    return FALSE;
}

static VOID
VidSchpNoteStateBaseAddress(
    _In_ PDXGKRNL_CONTEXT Context,
    _In_reads_bytes_(Bytes) const UCHAR *Head,
    _In_ ULONG Bytes)
{
    ULONGLONG Bases[5];
    BOOLEAN Enabled[5];
    LONG64 Sequence;
    ULONG Base;
    ULONG Word = 0;

    if (Context == NULL)
        return;
    while (VidSchpFindStateBaseAddress(Head, Bytes, &Word, Bases, Enabled))
    {
        Sequence = DxgkDiagSequence();
        InterlockedIncrement64(&Context->BaseCommandCount);
        for (Base = 0; Base < 5; Base++)
        {
            if (!Enabled[Base])
                continue;
            Context->LastBase[Base].Sequence = Sequence;
            Context->LastBase[Base].Address = Bases[Base];
        }
    }
}

/* Scan the batch prefix for STATE_BASE_ADDRESS: the whole batch (up to
 * 256 KB) for a context's first submissions, whose bases the later ones
 * inherit, and the first 4 KB afterwards.  Chunks overlap by the length
 * of the command so one on a chunk boundary is not missed. */
#define VIDSCH_BASE_SCAN_CHUNK      4096
#define VIDSCH_BASE_SCAN_FIRST      4
#define VIDSCH_BASE_SCAN_FIRST_LIMIT (256 * 1024)
static VOID
VidSchpScanStateBaseAddress(
    _In_ PDXGKRNL_CONTEXT Context,
    _In_ PDXGKRNL_PROCESS Process,
    _In_ D3DGPU_VIRTUAL_ADDRESS DmaBufferGpuVa,
    _In_ ULONG DmaBufferSize)
{
    PUCHAR Chunk;
    ULONG Limit, Offset;
    LONG64 Submission;

    Submission = InterlockedIncrement64(&Context->SubmissionCount);
    Limit = Submission <= VIDSCH_BASE_SCAN_FIRST ? VIDSCH_BASE_SCAN_FIRST_LIMIT : VIDSCH_BASE_SCAN_CHUNK;
    Limit = min(Limit, DmaBufferSize);
    Chunk = ExAllocatePoolWithTag(PagedPool, VIDSCH_BASE_SCAN_CHUNK, 'sbSV');
    if (Chunk == NULL)
        return;
    for (Offset = 0; Offset < Limit; Offset += VIDSCH_BASE_SCAN_CHUNK - 64)
    {
        ULONG Bytes = min(VIDSCH_BASE_SCAN_CHUNK, Limit - Offset);

        if (!DxgkGpuVaCopyFromProcess(Process, DmaBufferGpuVa + Offset, Chunk, Bytes))
            break;
        VidSchpNoteStateBaseAddress(Context, Chunk, Bytes);
    }
    ExFreePoolWithTag(Chunk, 'sbSV');
}

static VOID
VidSchpDumpBatchHeadBytes(
    _In_reads_bytes_(Bytes) const UCHAR *Head,
    _In_ ULONG Bytes,
    _In_ ULONG DmaSize,
    _In_ PCSTR Tag)
{
    const ULONG *Words = (const ULONG *)Head;
    ULONG Count = Bytes / sizeof(ULONG);
    ULONG i;

    if (Count == 0)
    {
        DXGKRNL_ERR("VidSch: no submit-time batch head captured for the %s packet\n", Tag);
        return;
    }
    DXGKRNL_ERR("VidSch: submit-time batch head of the %s packet (%lu bytes of %lu):\n",
                Tag, Bytes, DmaSize);
    for (i = 0; i < Count; i += 8)
    {
        DXGKRNL_ERR("BATCHHEAD[%04lx] %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx\n",
                    i * 4,
                    Words[i], i + 1 < Count ? Words[i + 1] : 0, i + 2 < Count ? Words[i + 2] : 0, i + 3 < Count ? Words[i + 3] : 0,
                    i + 4 < Count ? Words[i + 4] : 0, i + 5 < Count ? Words[i + 5] : 0, i + 6 < Count ? Words[i + 6] : 0, i + 7 < Count ? Words[i + 7] : 0);
    }
    for (i = 0; i + 11 < Count; i++)
    {
        if ((Words[i] & 0xFFFF0000UL) != 0x61010000UL)
            continue;
        DXGKRNL_ERR("STATE_BASE_ADDRESS at dw%lu: general=0x%I64x(en=%lu) surface=0x%I64x(en=%lu) dynamic=0x%I64x(en=%lu) indirect=0x%I64x(en=%lu) instruction=0x%I64x(en=%lu)\n",
                    i,
                    (((ULONGLONG)Words[i + 2] << 32) | Words[i + 1]) & ~0xFFFULL, Words[i + 1] & 1,
                    (((ULONGLONG)Words[i + 5] << 32) | Words[i + 4]) & ~0xFFFULL, Words[i + 4] & 1,
                    (((ULONGLONG)Words[i + 7] << 32) | Words[i + 6]) & ~0xFFFULL, Words[i + 6] & 1,
                    (((ULONGLONG)Words[i + 9] << 32) | Words[i + 8]) & ~0xFFFULL, Words[i + 8] & 1,
                    (((ULONGLONG)Words[i + 11] << 32) | Words[i + 10]) & ~0xFFFULL, Words[i + 10] & 1);
        break;
    }
    if (i + 11 >= Count)
        DXGKRNL_ERR("STATE_BASE_ADDRESS: not in the captured head (bases inherited from the context image)\n");
}

static VOID
VidSchpDestroyPacket(
    _In_ PVIDSCH_DMA_PACKET Packet)
{
    ASSERT(Packet != NULL);
    ASSERT(InterlockedCompareExchange(&Packet->ReferenceCount, 0, 0) == 0);
    ASSERT(!VidSchPolicyPacketCleanupMustDefer(KeGetCurrentIrql()));

    if (Packet->FenceIdentityReserved && Packet->OwnerEngine != NULL)
        DxgkReleaseSubmittedFenceIdentity(Packet->OwnerEngine->Adapter, Packet->NodeOrdinal, Packet->SubmissionFenceId, Packet->FenceIdentityEpoch);
    if (Packet->TrackerReservation != NULL && !Packet->TrackerOwnsDmaBuffer)
        DxgkCancelTrackedDmaBuffer(Packet->TrackerReservation);
    if (Packet->DmaBuffer != NULL && !Packet->TrackerOwnsDmaBuffer)
        DxgkFreeDmaBuffer(Packet->DmaBuffer);
    /* The miniport is done with the mapping that contains the command start;
     * it may be unmapped again.  VirtualDmaBufferSize is KMD geometry, not the
     * span dxgkrnl pinned. */
    if (Packet->GpuVaPinProcess != NULL)
    {
        DxgkGpuVaUnpinRange(Packet->GpuVaPinProcess,
                           Packet->DmaBufferGpuVa,
                           1);
        Packet->GpuVaPinProcess = NULL;
    }
    if (Packet->OwnedDriverPrivateData != NULL)
        ExFreePoolWithTag(Packet->OwnedDriverPrivateData, TAG_VIDSCH);
    DxgkDeviceWorkDestroy(Packet->DeviceWork);
    Packet->DeviceWork = NULL;
    if (Packet->HoldsContextReference)
        DxgkDereferenceContext((PDXGKRNL_CONTEXT)Packet->Context);
    if (Packet->Device != NULL)
        DxgkDereferenceDevice(Packet->Device);
    /* Cancelled before dispatch, or destroyed on a fault/teardown path. */
    DptEnd(&g_DxgPresentTrace, Packet->PresentationQueueTrace, FALSE, 0);
    DptEnd(&g_DxgPresentTrace, Packet->PresentationRetireTrace, FALSE, 0);
    ExFreePoolWithTag(Packet, TAG_VIDSCH);
}

static VOID
NTAPI
VidSchpDestroyPacketWorker(
    _In_ PVOID Parameter)
{
    PVIDSCH_DMA_PACKET Packet = Parameter;
    PVIDSCH_ENGINE Engine = Packet->OwnerEngine;
    PDXGKRNL_ADAPTER Adapter = Engine->Adapter;

    PAGED_CODE();

    VidSchpDestroyPacket(Packet);
    VidSchpReleaseOutstandingWorker(Engine);
    VidSchpReleaseCall(Adapter);
}

static VOID
VidSchpDereferencePacket(
    _In_ PVIDSCH_DMA_PACKET Packet)
{
    PVIDSCH_ENGINE Engine;
    KIRQL OldIrql;

    if (InterlockedDecrement(&Packet->ReferenceCount) != 0)
        return;

    if (!VidSchPolicyPacketCleanupMustDefer(KeGetCurrentIrql()))
    {
        VidSchpDestroyPacket(Packet);
        return;
    }

    /* Completion retirement runs at DISPATCH_LEVEL, while final teardown can
     * take the GPU-VA FAST_MUTEX and other PASSIVE/APC-level locks.  Transfer
     * the zero-reference packet to a system worker and keep its engine and
     * adapter scheduler storage alive until that worker is finished. */
    Engine = Packet->OwnerEngine;
    ASSERT(Engine != NULL);
    VidSchpReferenceActiveCall(Engine->Adapter);
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    VidSchpReferenceOutstandingWorkerLocked(Engine);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    ExQueueWorkItem(&Packet->DestroyWorkItem, DelayedWorkQueue);
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

/* ========================================================================
 * Node execution accounting
 *
 * A GPU node is busy for exactly as long as the miniport holds one of its
 * packets.  The dispatch that hands a packet over opens the charge and the
 * retirement that takes it back closes it, so the busy clock never counts
 * time the hardware was not asked to do anything and never stops while it
 * still is.
 *
 * Both edges are idempotent per packet.  Dispatch stores the start reading
 * only into a zero slot, and retirement takes the reading away before using
 * it, so a preemption resubmission cannot open a second charge and a packet
 * reported through more than one terminal path cannot credit twice.
 * ====================================================================== */

/*
 * Which of the four public DMA packet types a packet is.  The paging flag is
 * what separates paging traffic from a client's own rendering, and a paging
 * packet with no owning device is the system's own, not a client's.
 */
static D3DKMT_QUERYSTATISTICS_DMA_PACKET_TYPE
VidSchpPacketType(
    _In_ PVIDSCH_DMA_PACKET Packet)
{
    if ((Packet->SubmitFlags & VIDSCH_SUBMITFLAG_PAGING) == 0)
        return D3DKMT_ClientRenderBuffer;
    return (Packet->Device != NULL) ? D3DKMT_ClientPagingBuffer
                                    : D3DKMT_SystemPagingBuffer;
}

/*
 * The node ordinal a packet carries has already been bounded by the
 * submission path, but the accounting arrays are fixed size and this runs on
 * the completion path, so it is re-checked here rather than trusted.
 */
static PDXGKRNL_ADAPTER
VidSchpAccountingTarget(
    _In_ PVIDSCH_DMA_PACKET Packet,
    _Out_ PULONG NodeOrdinal)
{
    PDXGKRNL_ADAPTER Adapter;

    *NodeOrdinal = 0;
    if (Packet == NULL || Packet->OwnerEngine == NULL)
        return NULL;
    Adapter = Packet->OwnerEngine->Adapter;
    if (Adapter == NULL)
        return NULL;
    if (Packet->NodeOrdinal >= RTL_NUMBER_OF(Adapter->NodeStatistics))
        return NULL;
    *NodeOrdinal = Packet->NodeOrdinal;
    return Adapter;
}

VOID
VidSchAccountNodeDispatch(
    _Inout_ PVIDSCH_DMA_PACKET Packet)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_PROCESS ProcessRecord;
    D3DKMT_QUERYSTATISTICS_DMA_PACKET_TYPE PacketType;
    LARGE_INTEGER Now;
    ULONG NodeOrdinal;
    KIRQL OldIrql;

    Adapter = VidSchpAccountingTarget(Packet, &NodeOrdinal);
    if (Adapter == NULL)
        return;

    KeAcquireSpinLock(&Adapter->NodeStatisticsLock[NodeOrdinal], &OldIrql);
    Now = KeQueryPerformanceCounter(NULL);
    if (InterlockedCompareExchange64(&Packet->ExecutionChargeStart,
                                     Now.QuadPart, 0) != 0)
    {
        KeReleaseSpinLock(&Adapter->NodeStatisticsLock[NodeOrdinal], OldIrql);
        return;
    }

    PacketType = VidSchpPacketType(Packet);
    DxgkNodeStatsCoreOpen(&Adapter->NodeStatistics[NodeOrdinal], PacketType, Now.QuadPart);

    /*
     * The packet holds a device reference for its whole flight and the device
     * holds one on its process record, so the record is alive here without
     * any lock of its own.
     */
    ProcessRecord = (Packet->Device != NULL) ? Packet->Device->ProcessRecord : NULL;
    if (ProcessRecord != NULL)
    {
        DxgkNodeStatsCoreOpen(&ProcessRecord->NodeStatistics[NodeOrdinal], PacketType, Now.QuadPart);
        if (Packet->IsPresent &&
            Packet->VidPnSourceId < RTL_NUMBER_OF(ProcessRecord->PresentsSubmitted))
        {
            InterlockedIncrement(&ProcessRecord->PresentsSubmitted[Packet->VidPnSourceId]);
        }
    }
    else
    {
        /* Contextless work belongs to no client, so it is dxgkrnl's own. */
        DxgkNodeStatsCoreOpen(&Adapter->SystemNodeStatistics[NodeOrdinal], PacketType, Now.QuadPart);
    }
    KeReleaseSpinLock(&Adapter->NodeStatisticsLock[NodeOrdinal], OldIrql);
}

VOID
VidSchAccountNodeRetire(
    _Inout_ PVIDSCH_DMA_PACKET Packet)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_PROCESS ProcessRecord;
    D3DKMT_QUERYSTATISTICS_DMA_PACKET_TYPE PacketType;
    LARGE_INTEGER Now;
    ULONG NodeOrdinal;
    KIRQL OldIrql;

    Adapter = VidSchpAccountingTarget(Packet, &NodeOrdinal);
    if (Adapter == NULL)
        return;
    KeAcquireSpinLock(&Adapter->NodeStatisticsLock[NodeOrdinal], &OldIrql);
    if (InterlockedExchange64(&Packet->ExecutionChargeStart, 0) == 0)
    {
        KeReleaseSpinLock(&Adapter->NodeStatisticsLock[NodeOrdinal], OldIrql);
        return;
    }

    Now = KeQueryPerformanceCounter(NULL);
    PacketType = VidSchpPacketType(Packet);
    ProcessRecord = (Packet->Device != NULL) ? Packet->Device->ProcessRecord : NULL;
    if (ProcessRecord != NULL)
    {
        DxgkNodeStatsCoreClose(&ProcessRecord->NodeStatistics[NodeOrdinal], PacketType, Now.QuadPart);
        /*
         * A present packet the miniport has handed back is a frame that
         * reached the source, which is the one moment it can be charged to
         * the process that asked for it.
         */
        if (Packet->IsPresent &&
            Packet->VidPnSourceId < RTL_NUMBER_OF(ProcessRecord->PresentsRetired))
        {
            InterlockedIncrement(&ProcessRecord->PresentsRetired[Packet->VidPnSourceId]);
        }
    }
    else
    {
        DxgkNodeStatsCoreClose(&Adapter->SystemNodeStatistics[NodeOrdinal], PacketType, Now.QuadPart);
    }
    DxgkNodeStatsCoreClose(&Adapter->NodeStatistics[NodeOrdinal], PacketType, Now.QuadPart);
    KeReleaseSpinLock(&Adapter->NodeStatisticsLock[NodeOrdinal], OldIrql);
}

/*
 * Convert one accounting record into the public shape.  A node that is busy
 * right now has an interval that has not been added to RunningTicks yet;
 * leaving it out would make a saturated engine report a running time that
 * stalls, so the open interval is included in the snapshot without being
 * committed to the counter.
 */
static VOID
VidSchpSnapshotNodeStatistics(
    _In_ PDXGKRNL_NODE_STATISTICS Statistics,
    _In_ LONG64 Frequency,
    _Out_ D3DKMT_QUERYSTATISTICS_PROCESS_NODE_INFORMATION *Information)
{
    LARGE_INTEGER Now;
    ULONG Index;

    RtlZeroMemory(Information, sizeof(*Information));
    Now = KeQueryPerformanceCounter(NULL);
    /*
     * 100ns units.  The WDK header calls this field micro-seconds, but the
     * GPU Engine performance counter it feeds -- "Running Time" -- is a
     * PERF_100NSEC_TIMER and is documented in 100-nanosecond units, and every
     * consumer divides this by an elapsed 100ns interval to get a
     * utilization.  Emitting microseconds would make a saturated engine
     * report ten per cent.
     */
    Information->RunningTime.QuadPart =
        DxgkNodeStatsCoreTicksTo100ns(
            DxgkNodeStatsCoreRunningTicks(Statistics, Now.QuadPart),
            Frequency);

    Information->ContextSwitch =
        (ULONG)InterlockedCompareExchange64(&Statistics->ContextSwitches, 0, 0);
    for (Index = 0; Index < D3DKMT_QUERYSTATISTICS_DMA_PACKET_TYPE_MAX; ++Index)
    {
        Information->PacketStatistics.DmaPacket[Index].PacketSubmited =
            (ULONG)InterlockedCompareExchange64(&Statistics->PacketsDispatched[Index], 0, 0);
        Information->PacketStatistics.DmaPacket[Index].PacketCompleted =
            (ULONG)InterlockedCompareExchange64(&Statistics->PacketsRetired[Index], 0, 0);
        Information->PacketStatistics.DmaPacket[Index].PacketPreempted =
            (ULONG)InterlockedCompareExchange64(&Statistics->PacketsPreempted[Index], 0, 0);
    }
    Information->PreemptionStatistics.PreemptionCounter[D3DKMT_PreemptionAttempt] =
        (ULONG)InterlockedCompareExchange64(&Statistics->PreemptionsRequested, 0, 0);
    Information->PreemptionStatistics.PreemptionCounter[D3DKMT_PreemptionAttemptSuccess] =
        (ULONG)InterlockedCompareExchange64(&Statistics->PreemptionsCompleted, 0, 0);
}

static PDXGKRNL_NODE_STATISTICS
VidSchpPacketOwnerNodeStatistics(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PVIDSCH_DMA_PACKET Packet,
    _In_ ULONG NodeOrdinal)
{
    PDXGKRNL_PROCESS ProcessRecord;

    if (Packet == NULL)
        return NULL;
    ProcessRecord = (Packet->Device != NULL) ? Packet->Device->ProcessRecord : NULL;
    if (ProcessRecord != NULL)
        return &ProcessRecord->NodeStatistics[NodeOrdinal];
    return &Adapter->SystemNodeStatistics[NodeOrdinal];
}

NTSTATUS
VidSchQueryNodeStatistics(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_PROCESS ProcessRecord,
    _In_ ULONG NodeOrdinal,
    _Out_ D3DKMT_QUERYSTATISTICS_PROCESS_NODE_INFORMATION *Information)
{
    KIRQL OldIrql;

    if (Adapter == NULL || Information == NULL)
        return STATUS_INVALID_PARAMETER;
    if (NodeOrdinal >= Adapter->NodeCount ||
        NodeOrdinal >= RTL_NUMBER_OF(Adapter->NodeStatistics))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Adapter->NodeStatisticsLock[NodeOrdinal], &OldIrql);
    VidSchpSnapshotNodeStatistics(
        (ProcessRecord != NULL) ? &ProcessRecord->NodeStatistics[NodeOrdinal]
                                : &Adapter->NodeStatistics[NodeOrdinal],
        Adapter->PerformanceFrequency,
        Information);
    KeReleaseSpinLock(&Adapter->NodeStatisticsLock[NodeOrdinal], OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
VidSchQuerySystemNodeStatistics(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG NodeOrdinal,
    _Out_ D3DKMT_QUERYSTATISTICS_PROCESS_NODE_INFORMATION *Information)
{
    KIRQL OldIrql;

    if (Adapter == NULL || Information == NULL)
        return STATUS_INVALID_PARAMETER;
    if (NodeOrdinal >= Adapter->NodeCount ||
        NodeOrdinal >= RTL_NUMBER_OF(Adapter->SystemNodeStatistics))
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeAcquireSpinLock(&Adapter->NodeStatisticsLock[NodeOrdinal], &OldIrql);
    VidSchpSnapshotNodeStatistics(
        &Adapter->SystemNodeStatistics[NodeOrdinal],
        Adapter->PerformanceFrequency,
        Information);
    KeReleaseSpinLock(&Adapter->NodeStatisticsLock[NodeOrdinal], OldIrql);
    return STATUS_SUCCESS;
}

static VOID VidSchpFinalizeDequeuedPacket(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS CompletionStatus)
{
    VidSchAccountNodeRetire(Packet);
    Packet->SchedulerCookie = 0;
    DxgkDeviceWorkComplete(Packet->DeviceWork);
    if (Packet->ContextOrderOperation != NULL)
        DxgkContextOrderAbortPacket(Packet, CompletionStatus);
    VidSchpDereferencePacket(Packet);
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
static VOID
VidSchpQueueFaultCleanup(
    _Inout_ PVIDSCH_DMA_PACKET Packet,
    _In_ NTSTATUS CompletionStatus)
{
    PVIDSCH_ENGINE Engine = Packet->OwnerEngine;
    KIRQL OldIrql;

    ASSERT(Engine != NULL);
    Packet->FaultCleanupStatus = CompletionStatus;
    KeMemoryBarrier();
    if (InterlockedCompareExchange(
            &Packet->FaultCleanupQueued, 1, 0) != 0)
    {
        ASSERT(FALSE);
        return;
    }

    /*
     * The provider reference transfers to this worker.  The active-call and
     * per-engine worker references keep scheduler storage alive after the DPC
     * that queued us returns.
     */
    VidSchpReferenceActiveCall(Engine->Adapter);
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    VidSchpReferenceOutstandingWorkerLocked(Engine);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    ExQueueWorkItem(&Packet->CleanupWorkItem, DelayedWorkQueue);
}

static BOOLEAN
VidSchpReferenceDeviceContexts(
    _In_ PDXGKRNL_DEVICE Device,
    _Out_ PDXGKRNL_CONTEXT **ContextArray,
    _Out_ PULONG ContextCount)
{
    PDXGKRNL_CONTEXT *Contexts;
    ULONG Capacity;

    PAGED_CODE();
    *ContextArray = NULL;
    *ContextCount = 0;

    for (;;)
    {
        PLIST_ENTRY Link;
        ULONG Required = 0;
        ULONG Captured = 0;
        BOOLEAN Overflow = FALSE;

        ExAcquireFastMutex(&Device->DeviceMutex);
        for (Link = Device->ContextListHead.Flink;
             Link != &Device->ContextListHead;
             Link = Link->Flink)
        {
            if (Required == MAXULONG)
                break;
            Required++;
        }
        ExReleaseFastMutex(&Device->DeviceMutex);

        if (Required == 0)
            return TRUE;
        if (Required == MAXULONG ||
            (SIZE_T)Required >
                MAXULONG_PTR / sizeof(*Contexts))
        {
            return FALSE;
        }
        Capacity = Required;
        Contexts = ExAllocatePoolWithTag(
                       PagedPool,
                       (SIZE_T)Capacity * sizeof(*Contexts),
                       TAG_VIDSCH);
        if (Contexts == NULL)
            return FALSE;

        ExAcquireFastMutex(&Device->DeviceMutex);
        for (Link = Device->ContextListHead.Flink;
             Link != &Device->ContextListHead;
             Link = Link->Flink)
        {
            PDXGKRNL_CONTEXT Context;

            if (Captured == Capacity)
            {
                Overflow = TRUE;
                break;
            }
            Context = CONTAINING_RECORD(
                          Link,
                          DXGKRNL_CONTEXT,
                          ContextListEntry);
            if (DxgkReferenceContext(Context))
                Contexts[Captured++] = Context;
        }
        ExReleaseFastMutex(&Device->DeviceMutex);

        if (!Overflow)
        {
            *ContextArray = Contexts;
            *ContextCount = Captured;
            return TRUE;
        }

        while (Captured != 0)
            DxgkDereferenceContext(Contexts[--Captured]);
        ExFreePoolWithTag(Contexts, TAG_VIDSCH);
    }
}

static VOID
VidSchpCancelOwnerPacketsActive(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PVOID Owner,
    _In_ NTSTATUS CompletionStatus)
{
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;

    Sched = VidSchpScheduler(Adapter);
    if (Sched == NULL)
        return;
    (VOID)Sched->CancelOwnerPackets(
        Sched->SchedulerHandle,
        (ULONGLONG)(ULONG_PTR)Owner,
        CompletionStatus);
    (VOID)VidSchpDrainRetirements(Adapter);
}

static VOID
NTAPI
VidSchpFaultCleanupWorker(
    _In_ PVOID Parameter)
{
    PVIDSCH_DMA_PACKET Packet = (PVIDSCH_DMA_PACKET)Parameter;
    PVIDSCH_ENGINE Engine = Packet->OwnerEngine;
    PDXGKRNL_ADAPTER Adapter = Engine->Adapter;
    PDXGKRNL_CONTEXT PacketContext =
        (PDXGKRNL_CONTEXT)Packet->Context;
    PDXGKRNL_DEVICE Device =
        Packet->Device != NULL
            ? Packet->Device
            : (PacketContext != NULL ? PacketContext->Device : NULL);
    PDXGKRNL_CONTEXT *Contexts = NULL;
    ULONG ContextCount = 0;
    ULONG Index;
    BOOLEAN CancelDevicePackets =
        InterlockedCompareExchange(
            &Packet->CancelFaultedDevicePackets, 0, 0) != 0;

    PAGED_CODE();

    if (CancelDevicePackets && Device == NULL)
    {
        CancelDevicePackets = FALSE;
    }
    else if (CancelDevicePackets &&
             !VidSchpReferenceDeviceContexts(
                 Device,
                 &Contexts,
                 &ContextCount))
    {
        DXGKRNL_WARN(
            "VidSch: unable to snapshot faulted device contexts; "
            "falling back to lazy packet rejection\n");
    }

    /*
     * Contextless packets use the referenced device as their owner cookie.
     * Cancel them while the fault packet still pins Device; context-owned
     * streams are cancelled after their faulting head is terminalized.
     */
    if (CancelDevicePackets)
    {
        VidSchpCancelOwnerPacketsActive(
            Adapter,
            Device,
            STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE);
    }

    /*
     * Retire the faulted stream head before cancelling its later packets, so
     * their context-order aborts observe a terminal predecessor.
     */
    VidSchpFinalizeDequeuedPacket(
        Packet,
        Packet->FaultCleanupStatus);

    if (CancelDevicePackets)
    {
        PVIDSCH_CONTEXT Scheduler =
            (PVIDSCH_CONTEXT)Adapter->VidSchContext;

        for (Index = 0; Index < ContextCount; ++Index)
        {
            VidSchpCancelOwnerPacketsActive(
                Adapter,
                Contexts[Index],
                STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE);
        }
        if (Scheduler != NULL)
        {
            for (Index = 0;
                 Index < Scheduler->EngineCount;
                 ++Index)
            {
                (VOID)VidSchpKickEngine(
                    &Scheduler->Engines[Index],
                    NULL);
            }
        }
    }

    for (Index = 0; Index < ContextCount; ++Index)
        DxgkDereferenceContext(Contexts[Index]);
    if (Contexts != NULL)
        ExFreePoolWithTag(Contexts, TAG_VIDSCH);
    VidSchpReleaseOutstandingWorker(Engine);
    VidSchpReleaseCall(Adapter);
}
#endif

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
/*
 * Mark only the exact packet currently published to this engine. QueueLock
 * closes the race with retirement: either retirement unpublishes first and
 * this notification is stale, or the fault marker is visible before the
 * retirement record is finalized.
 */
static PVIDSCH_DMA_PACKET
VidSchpMarkActivePacketFaulted(
    _Inout_ PVIDSCH_ENGINE Engine,
    _In_ CONST DXGKARGCB_NOTIFY_INTERRUPT_DATA *NotifyData,
    _In_ ULONGLONG ExpectedCookie,
    _Out_ PDXGKRNL_DEVICE *FaultedDevice)
{
    PVIDSCH_DMA_PACKET Packet;
    PDXGKRNL_CONTEXT Context;
    PDXGKRNL_DEVICE Device;
    HANDLE ProcessHandle;
    KIRQL OldIrql;

    *FaultedDevice = NULL;
    ProcessHandle =
        NotifyData->DmaPageFaulted.FaultedProcessHandle;

    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    Packet = NULL;
    for (PLIST_ENTRY Entry = Engine->ActivePacketList.Flink;
         Entry != &Engine->ActivePacketList;
         Entry = Entry->Flink)
    {
        PVIDSCH_DMA_PACKET Candidate;

        Candidate = CONTAINING_RECORD(Entry,
                                      VIDSCH_DMA_PACKET,
                                      ActiveEngineEntry);
        if ((ULONGLONG)(ULONG_PTR)Candidate == ExpectedCookie)
        {
            Packet = Candidate;
            break;
        }
    }
    if (Packet == NULL ||
        Packet->OwnerEngine != Engine ||
        Packet->SubmissionFenceId !=
            NotifyData->DmaPageFaulted.FaultedFenceId ||
        Packet->NodeOrdinal !=
            NotifyData->DmaPageFaulted.NodeOrdinal ||
        Packet->EngineOrdinal !=
            NotifyData->DmaPageFaulted.EngineOrdinal)
    {
        Packet = NULL;
        goto Exit;
    }

    Context = (PDXGKRNL_CONTEXT)Packet->Context;
    Device = Packet->Device != NULL
                 ? Packet->Device
                 : (Context != NULL ? Context->Device : NULL);
    if (Device == NULL ||
        (Context != NULL &&
         Context->Device != NULL &&
         Context->Device != Device) ||
        (ProcessHandle != NULL &&
         (Device->ProcessRecord == NULL ||
          (HANDLE)Device->ProcessRecord != ProcessHandle)) ||
        (Packet->GpuVaPinProcess != NULL &&
         ProcessHandle != NULL &&
         (HANDLE)Packet->GpuVaPinProcess != ProcessHandle))
    {
        Packet = NULL;
        goto Exit;
    }

    Packet->FaultStatus =
        STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE;
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Packet->Faulted, 1, 0) != 0)
    {
        Packet = NULL;
        goto Exit;
    }

    ASSERT(InterlockedCompareExchange(
               &Packet->ReferenceCount, 0, 0) > 0);
    InterlockedIncrement(&Packet->ReferenceCount);
    *FaultedDevice = Device;

Exit:
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    return Packet;
}

static VOID
NTAPI
VidSchpFaultDumpWorker(
    _In_ PVOID Parameter)
{
    PVIDSCH_ENGINE Engine = (PVIDSCH_ENGINE)Parameter;
    PDXGKRNL_ADAPTER Adapter = Engine->Adapter;
    PVIDSCH_DMA_PACKET Packet;
    KIRQL OldIrql;
    ULONG FaultFlags;

    PAGED_CODE();
Recover:
    FaultFlags = (ULONG)InterlockedExchange(
                            (volatile LONG *)&Engine->LastFaultFlags,
                            0);

    /*
     * A reset-required page fault stops this engine until the prescribed
     * reset DDI completes.  Do that before the diagnostic walk: the walk can
     * take seconds on a large GPU-VA space, while the miniport is waiting for
     * DxgkDdiResetEngine and cannot retire another fence.  Native dxgmms2
     * likewise puts the node in resetting state, flushes queued DPCs, and
     * invokes the engine reset while the hardware queue still has outstanding
     * packets.
     */
    /* Read the faulted batch first: after the reset the device is reported
     * lost and the UMD's teardown unmaps the range within about a second
     * (2026-09-06: "cannot read batch" on every deferred dump). */
    if (Engine->LastFaultDmaGpuVa != 0)
    {
        if (Engine->LastFaultContext != NULL)
            DxgkVidMmDumpContextImages(Engine->LastFaultContext, NULL, "fault-before-reset");
        DxgkGpuVaDumpBuffer(Engine->LastFaultProcess, Engine->LastFaultDmaGpuVa,
                            min(Engine->LastFaultDmaSize, 8192));
    }

    if ((FaultFlags & DXGK_PAGE_FAULT_ENGINE_RESET_REQUIRED) != 0)
    {
        NTSTATUS ResetStatus =
            VidSchResetEngine(Engine->Adapter,
                              Engine->SchedulerOrdinal);

        DXGKRNL_ERR("VidSch: engine %lu reset after page fault -> 0x%08lX\n",
                    Engine->SchedulerOrdinal, ResetStatus);
    }

    if (Engine->LastFaultDmaGpuVa != 0)
    {
        DXGKRNL_ERR("VidSch: engine fault dump fence=%lu dma va=0x%I64x size=%lu process=%p ctx=%p\n",
                    Engine->LastFaultFence, Engine->LastFaultDmaGpuVa, Engine->LastFaultDmaSize, Engine->LastFaultProcess, Engine->LastFaultContext);
        VidSchpDumpCapturedBatchHead(Engine);
        DXGKRNL_ERR("VidSch: bases last enabled on ctx %p: general #%I64d=0x%I64x surface #%I64d=0x%I64x dynamic #%I64d=0x%I64x indirect #%I64d=0x%I64x instruction #%I64d=0x%I64x (0 = never on this context)\n",
                    Engine->LastFaultContext,
                    Engine->LastFaultBaseSequence[0], Engine->LastFaultBaseAddress[0],
                    Engine->LastFaultBaseSequence[1], Engine->LastFaultBaseAddress[1],
                    Engine->LastFaultBaseSequence[2], Engine->LastFaultBaseAddress[2],
                    Engine->LastFaultBaseSequence[3], Engine->LastFaultBaseAddress[3],
                    Engine->LastFaultBaseSequence[4], Engine->LastFaultBaseAddress[4]);
        DXGKRNL_ERR("VidSch: ctx %p had %I64d virtual submissions, %I64d STATE_BASE_ADDRESS in their scanned prefix (first %u scanned to %u KB, later to %u bytes)\n",
                    Engine->LastFaultContext, Engine->LastFaultSubmissionCount, Engine->LastFaultBaseCommandCount,
                    VIDSCH_BASE_SCAN_FIRST, VIDSCH_BASE_SCAN_FIRST_LIMIT / 1024, VIDSCH_BASE_SCAN_CHUNK);
        DxgkVidMmDumpApertureOps();
        DxgkPagingDumpRecentOps();
        DXGKRNL_ERR("VidSch: engine fault target va=0x%I64x (the address the GPU could not translate)\n",
                    Engine->LastFaultVa);
        DxgkGpuVaDumpTranslation(Engine->Adapter, Engine->LastFaultProcess, Engine->LastFaultVa);
        DxgkGpuVaDumpProcessRanges(Engine->LastFaultProcess);
        DxgkGpuVaAuditMappings(Engine->Adapter, Engine->LastFaultProcess);
        DxgkGpuVaDumpTranslation(Engine->Adapter, Engine->LastFaultProcess, Engine->LastFaultDmaGpuVa);
        DxgkGpuVaDumpBuffer(Engine->LastFaultProcess, Engine->LastFaultDmaGpuVa, Engine->LastFaultDmaSize);
        DxgkGpuVaVerifyProcessTables(Engine->Adapter, Engine->LastFaultProcess);
        DxgkGpuVaDumpTranslation(Engine->Adapter, Engine->LastFaultProcess, 0);
        DxgkGpuVaDumpTranslation(Engine->Adapter, Engine->LastFaultProcess, 0x4000);
        Engine->LastFaultDmaGpuVa = 0;
    }
    /*
     * DXGK_PAGE_FAULT_ENGINE_RESET_REQUIRED / _ADAPTER_RESET_REQUIRED are the
     * miniport telling us the fault left the engine in a state that cannot run
     * further work until it is reset.  Recording the fault and marking the
     * device lost is not enough: without the reset the engine never retires
     * another packet, so every later submission waits forever and the GPU goes
     * silent rather than reporting an error.  The reset DDI is PASSIVE_LEVEL
     * only, which is why it happens here and not in the fault consumer.
     */
    if ((FaultFlags &
             (DXGK_PAGE_FAULT_ADAPTER_RESET_REQUIRED |
              DXGK_PAGE_FAULT_FATAL_HARDWARE_ERROR)) != 0)
    {
        /*
         * ADAPTER_RESET_REQUIRED asks for a full adapter reset and
         * FATAL_HARDWARE_ERROR asks the OS to bugcheck; an engine reset is not
         * a substitute for either, so report them rather than silently
         * under-handling the notification.
         * TODO: drive DxgkDdiResetFromTimeout/DxgkDdiRestartFromTimeout (see
         * TdrResetFromTimeout in dxgkrnl.c) for the adapter case.
         */
        DXGKRNL_ERR("VidSch: page fault flags 0x%lx demand an adapter reset "
                    "or bugcheck, which is not implemented; the GPU will not "
                    "recover on its own\n",
                    FaultFlags);
    }
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    /* A second fault can arrive after the reset while the first diagnostic
     * walk is running. Preserve its reset request without overwriting the
     * first worker's referenced capture. */
    if (Engine->LastFaultFlags != 0)
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        goto Recover;
    }
    Packet = Engine->FaultDumpPacket;
    Engine->FaultDumpPacket = NULL;
    Engine->LastFaultDmaGpuVa = 0;
    Engine->LastFaultProcess = NULL;
    Engine->LastFaultContext = NULL;
    InterlockedExchange(&Engine->FaultDumpQueued, 0);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    VidSchpDereferencePacket(Packet);
    VidSchpReleaseOutstandingWorker(Engine);
    VidSchpReleaseCall(Adapter);
}

static VOID
VidSchpConsumePageFaultInterrupt(
    _Inout_ PVIDSCH_ENGINE Engine)
{
    DXGKARGCB_NOTIFY_INTERRUPT_DATA NotifyData;
    D3DKMT_DEVICEPAGEFAULT_STATE PageFaultState;
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
    PVIDSCH_DMA_PACKET Packet = NULL;
    PDXGKRNL_DEVICE Device = NULL;
    ULONG CompletedFence;
    ULONG OldestFence = 0;
    ULONGLONG OldestCookie = 0;

    if (InterlockedCompareExchange(
            &Engine->PageFaultInterruptState, 1, 2) != 2)
    {
        return;
    }

    KeMemoryBarrier();
    NotifyData = Engine->PageFaultInterruptData;
    Sched = VidSchpScheduler(Engine->Adapter);
    CompletedFence = (ULONG)InterlockedCompareExchange(
        (volatile LONG *)&Engine->Adapter->
            NodeLastCompletedFenceId[Engine->SchedulerOrdinal],
        0,
        0);

    if (NotifyData.InterruptType !=
            DXGK_INTERRUPT_DMA_PAGE_FAULTED ||
        NotifyData.DmaPageFaulted.FaultedFenceId == 0 ||
        (NotifyData.DmaPageFaulted.PageFaultFlags &
             DXGK_PAGE_FAULT_FENCE_INVALID) != 0 ||
        NotifyData.DmaPageFaulted.NodeOrdinal !=
            Engine->SchedulerOrdinal ||
        NotifyData.DmaPageFaulted.EngineOrdinal !=
            0 ||
        (CompletedFence != 0 &&
         VidSchpFenceReached(
             CompletedFence,
             NotifyData.DmaPageFaulted.FaultedFenceId)) ||
        Sched == NULL ||
        !DxgkIsSubmittedFenceIdentity(
            Engine->Adapter,
            NotifyData.DmaPageFaulted.NodeOrdinal,
            NotifyData.DmaPageFaulted.FaultedFenceId) ||
        !Sched->GetOldestDispatchedOnEngine(
            Sched->SchedulerHandle,
            Engine->SchedulerOrdinal,
            &OldestFence,
            &OldestCookie) ||
        OldestFence !=
            NotifyData.DmaPageFaulted.FaultedFenceId)
    {
        goto Exit;
    }

    /*
     * GetOldestDispatchedOnEngine's cookie is only a scalar cross-check: the
     * provider drops its lock before returning, so never dereference it.
     * ActivePacketList is the lifetime-safe local publication protected by
     * QueueLock.
     */
    Packet = VidSchpMarkActivePacketFaulted(
                 Engine,
                 &NotifyData,
                 OldestCookie,
                 &Device);
    if (Packet == NULL)
        goto Exit;

    /* Stop new claims before diagnostics or PASSIVE_LEVEL recovery work can
     * yield.  The miniport explicitly said this engine cannot execute more
     * work until it is reset. */
    if ((NotifyData.DmaPageFaulted.PageFaultFlags &
             DXGK_PAGE_FAULT_ENGINE_RESET_REQUIRED) != 0 &&
        !VidSchpForceEngineState(Engine, VidSchEngineResetting))
    {
        DXGKRNL_ERR("VidSch: unable to block engine %lu for page-fault reset\n",
                    Engine->SchedulerOrdinal);
    }

    RtlZeroMemory(&PageFaultState, sizeof(PageFaultState));
    PageFaultState.FaultedPrimitiveAPISequenceNumber =
        NotifyData.DmaPageFaulted.
            FaultedPrimitiveAPISequenceNumber;
    PageFaultState.FaultedPipelineStage =
        NotifyData.DmaPageFaulted.FaultedPipelineStage;
    PageFaultState.FaultedBindTableEntry =
        NotifyData.DmaPageFaulted.FaultedBindTableEntry;
    PageFaultState.PageFaultFlags =
        NotifyData.DmaPageFaulted.PageFaultFlags;
    PageFaultState.FaultErrorCode =
        NotifyData.DmaPageFaulted.FaultErrorCode;
    PageFaultState.FaultedVirtualAddress =
        NotifyData.DmaPageFaulted.FaultedVirtualAddress;
    DXGKRNL_ERR("VidSch: GPU page fault: seq=#%I64d fence=%u va=0x%I64x flags=0x%x error=0x%x level=%u node=%u engine=%u stage=%d bind=%u seq=0x%I64x process=%p packet=%p device=%p\n",
                DxgkDiagSequence(),
                NotifyData.DmaPageFaulted.FaultedFenceId,
                NotifyData.DmaPageFaulted.FaultedVirtualAddress,
                (UINT)NotifyData.DmaPageFaulted.PageFaultFlags,
                *(const UINT *)&NotifyData.DmaPageFaulted.FaultErrorCode,
                NotifyData.DmaPageFaulted.PageTableLevel,
                NotifyData.DmaPageFaulted.NodeOrdinal,
                NotifyData.DmaPageFaulted.EngineOrdinal,
                (int)NotifyData.DmaPageFaulted.FaultedPipelineStage,
                NotifyData.DmaPageFaulted.FaultedBindTableEntry,
                NotifyData.DmaPageFaulted.FaultedPrimitiveAPISequenceNumber,
                NotifyData.DmaPageFaulted.FaultedProcessHandle,
                Packet, Device);
    /* Record the faulted packet and queue the PASSIVE worker before the
     * ring dumps below: those take over a second of serial, and the worker
     * needs that time to read the batch while the UMD, not yet told
     * anything, still has it mapped. */
    if (Packet != NULL)
    {
        KIRQL OldIrql;
        BOOLEAN QueueDump = FALSE;

        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        if (Engine->FaultDumpQueued != 0)
        {
            InterlockedOr((volatile LONG *)&Engine->LastFaultFlags,
                          (LONG)NotifyData.DmaPageFaulted.PageFaultFlags);
        }
        else
        {
            InterlockedExchange(&Engine->FaultDumpQueued, 1);
            InterlockedIncrement(&Packet->ReferenceCount);
            Engine->FaultDumpPacket = Packet;
            VidSchpReferenceActiveCall(Engine->Adapter);
            VidSchpReferenceOutstandingWorkerLocked(Engine);
            Engine->LastFaultDmaGpuVa = Packet->DmaBufferGpuVa;
            Engine->LastFaultDmaSize = Packet->VirtualDmaBufferSize;
            Engine->LastFaultVa = NotifyData.DmaPageFaulted.FaultedVirtualAddress;
            Engine->LastFaultFence = NotifyData.DmaPageFaulted.FaultedFenceId;
            Engine->LastFaultFlags = (ULONG)NotifyData.DmaPageFaulted.PageFaultFlags;
            Engine->LastFaultProcess = Packet->GpuVaPinProcess;
            /* A submission that pinned no range still belongs to a process: the
             * batch, translation and table dumps need it. */
            if (Engine->LastFaultProcess == NULL && Packet->Device != NULL)
                Engine->LastFaultProcess = Packet->Device->ProcessRecord;
            Engine->LastFaultContext = Packet->Context;
            if (Packet->Context != NULL)
            {
                PDXGKRNL_CONTEXT FaultContext = (PDXGKRNL_CONTEXT)Packet->Context;
                ULONG Base;

                for (Base = 0; Base < 5; Base++)
                {
                    Engine->LastFaultBaseSequence[Base] = FaultContext->LastBase[Base].Sequence;
                    Engine->LastFaultBaseAddress[Base] = FaultContext->LastBase[Base].Address;
                }
                Engine->LastFaultSubmissionCount = FaultContext->SubmissionCount;
                Engine->LastFaultBaseCommandCount = FaultContext->BaseCommandCount;
            }
            Engine->LastFaultBatchHeadBytes = min(Packet->BatchHeadBytes, sizeof(Engine->LastFaultBatchHead));
            if (Engine->LastFaultBatchHeadBytes != 0)
                RtlCopyMemory(Engine->LastFaultBatchHead, Packet->BatchHead, Engine->LastFaultBatchHeadBytes);
            QueueDump = TRUE;
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        if (QueueDump)
        {
            ExInitializeWorkItem(&Engine->FaultDumpWorkItem, VidSchpFaultDumpWorker, Engine);
            ExQueueWorkItem(&Engine->FaultDumpWorkItem, DelayedWorkQueue);
        }
    }
    VidSchDumpEngineDiagnostics(Engine->Adapter);
    DxgkGpuVaDumpRecentEvents();
    DxgkDumpRecentKmtIoctls();
    DxgkDeviceSetPageFaultState(Device, &PageFaultState);
    if (DxgkDeviceSetPageFaultExecutionState(Device))
    {
        InterlockedExchange(
            &Packet->CancelFaultedDevicePackets,
            1);
    }

    /*
     * This advances only dxgmms2's private queue watermark so it can emit one
     * retirement record. The monitored/submission timeline is deliberately
     * not advanced for a fault.
     */
    (VOID)Sched->NotifyCompletion(
        Sched->SchedulerHandle,
        Engine->SchedulerOrdinal,
        NotifyData.DmaPageFaulted.FaultedFenceId);
    (VOID)VidSchpDrainRetirements(Engine->Adapter);
    VidSchpDereferencePacket(Packet);

Exit:
    KeMemoryBarrier();
    InterlockedExchange(&Engine->PageFaultInterruptState, 0);
}
#endif

/* ========================================================================
 * Completion DPC callback
 *
 * Queued by VidSchNotifyInterrupt from ISR context.  Runs at
 * DISPATCH_LEVEL and processes completed commands.
 * ====================================================================== */
static VOID
VidSchpProcessCompletion(
    _In_ PVIDSCH_ENGINE Engine)
{
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
    LONG Completed;
    KIRQL OldIrql;
    BOOLEAN PreemptionInterrupt;
    BOOLEAN PreemptionCompleted = FALSE;
    ULONG CompletedPreemptionFence = 0;

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
        if (CompletedPreemptionFence != 0 && VidSchpEngineState(Engine->Adapter, Engine->SchedulerOrdinal) == VidSchEnginePreempting)
        {
            Engine->CompletedPreemptionEngineOrdinal = Engine->PendingPreemptionEngineOrdinal;
            InterlockedExchange(&Engine->CompletedPreemptionFenceId, (LONG)CompletedPreemptionFence);
            if (Engine->Adapter != NULL &&
                Engine->SchedulerOrdinal < RTL_NUMBER_OF(Engine->Adapter->NodeStatistics))
            {
                PDXGKRNL_NODE_STATISTICS NodeStatistics =
                    &Engine->Adapter->NodeStatistics[Engine->SchedulerOrdinal];
                PDXGKRNL_NODE_STATISTICS OwnerStatistics;

                InterlockedIncrement64(&NodeStatistics->PreemptionsCompleted);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
                PVIDSCH_DMA_PACKET ActivePacket =
                    VidSchpFirstActivePacketLocked(Engine);

                OwnerStatistics = VidSchpPacketOwnerNodeStatistics(Engine->Adapter, ActivePacket, Engine->SchedulerOrdinal);
                if (OwnerStatistics != NULL)
                    InterlockedIncrement64(&OwnerStatistics->PreemptionsCompleted);
                if (ActivePacket != NULL)
                {
                    D3DKMT_QUERYSTATISTICS_DMA_PACKET_TYPE PacketType = VidSchpPacketType(ActivePacket);

                    InterlockedIncrement64(&NodeStatistics->PacketsPreempted[PacketType]);
                    if (OwnerStatistics != NULL)
                        InterlockedIncrement64(&OwnerStatistics->PacketsPreempted[PacketType]);
                }
#else
                UNREFERENCED_PARAMETER(OwnerStatistics);
#endif
            }
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
            while (!IsListEmpty(&Engine->ActivePacketList))
            {
                PLIST_ENTRY Entry =
                    RemoveHeadList(&Engine->ActivePacketList);

                InitializeListHead(Entry);
            }
#endif
            VidSchpTryTransitionEngine(Engine, VidSchEnginePreempting, VidSchEnginePreempted);
            PreemptionCompleted = TRUE;
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    }

    if (Sched != NULL)
        (VOID)Sched->NotifyCompletion(Sched->SchedulerHandle, Engine->SchedulerOrdinal, (ULONG)Completed);

    /*
     * Preemption interrupted every packet the miniport had accepted but not
     * completed.  dxgmms2 keeps them queued in the same order with the same
     * fence identity and clears only their dispatched mark, so the next kick
     * resubmits exactly the same work.
     */
    if (PreemptionCompleted && Sched != NULL)
    {
        ULONGLONG Cookies[DXGMMS2_SCHEDULER_MAX_RETIREMENTS];
        ULONG ResetCount;
        NTSTATUS ResetStatus;

        /*
         * ResetDispatched changes provider ownership under its scheduler lock.
         * Its returned cookies are diagnostic only: once that lock drops an
         * owner cancellation may retire the packet, so dereferencing a cookie
         * here would race terminal cleanup.  Kicked remains set as the durable
         * local reset marker and is consumed only after the provider grants a
         * new claim below.
         */
        do
        {
            ResetCount = 0;
            ResetStatus = Sched->ResetDispatched(Sched->SchedulerHandle,
                                                 Engine->SchedulerOrdinal,
                                                 Cookies,
                                                 RTL_NUMBER_OF(Cookies),
                                                 &ResetCount);
        } while (NT_SUCCESS(ResetStatus) &&
                 ResetCount == RTL_NUMBER_OF(Cookies));
        VidSchpTryTransitionEngine(Engine, VidSchEnginePreempted, VidSchEngineIdle);
    }
    else if (!PreemptionCompleted && VidSchpEngineState(Engine->Adapter, Engine->SchedulerOrdinal) == VidSchEnginePreempting &&
             InterlockedCompareExchange(&Engine->PreemptionDdiState, 0, 0) == 0)
    {
        InterlockedExchange(&Engine->PendingPreemptionFenceId, 0);
        VidSchpTryTransitionEngine(Engine, VidSchEnginePreempting, VidSchEngineRunning);
    }

    /* Turn the retirement records into dxgkrnl terminal cleanup. */
    VidSchpDrainRetirements(Engine->Adapter);

#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    /*
     * A miniport may publish an earlier successful watermark and then one
     * exact page fault before this DPC runs. Retire that success first, then
     * terminalize only the now-oldest faulted packet.
     */
    VidSchpConsumePageFaultInterrupt(Engine);
#endif

    (VOID)VidSchpKickEngine(Engine, NULL);

    /* Completion drives tracked-DMA retirement (fence signals, open-binding
     * closes, in-flight accounting); nothing may wait for a later submission
     * to flush a completed packet. */
    DxgkRetireCompletedDmaBuffers(Engine->Adapter);

    if (PreemptionCompleted)
        KeSetEvent(&Engine->PreemptionCompletedEvent, IO_NO_INCREMENT, FALSE);

    /* Signal completion event so VidSchWaitForIdle can wake up. */
    KeSetEvent(&Engine->CompletionEvent, IO_NO_INCREMENT, FALSE);

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
VidSchpSubmitVirtualPacket(
    _In_ PVIDSCH_ENGINE Engine,
    _Inout_ PVIDSCH_DMA_PACKET Packet)
{
    PDXGKRNL_ADAPTER Adapter = Engine->Adapter;
    DXGKARG_SUBMITCOMMANDVIRTUAL SubmitArgs;
    KIRQL OldIrql;
    NTSTATUS AbortStatus;
    NTSTATUS Status = STATUS_DELETE_PENDING;
    BOOLEAN KmdCallAcquired = FALSE;
    BOOLEAN Removed = FALSE;
    BOOLEAN SubmissionOwned = FALSE;
    PDXGKRNL_SUBMIT_DMA_BUFFER Reservation = NULL;

    /* The kick already claimed this packet from dxgmms2; the claim token is
     * this worker's authority to dispatch it. */
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    if (Packet != NULL && Packet->SchedulerClaimToken != 0 && Packet->VirtualAddressing && (Packet->ContextOrderOperation == NULL || InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) == VIDSCH_CONTEXT_ORDER_DISPATCHING || (InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) == VIDSCH_CONTEXT_ORDER_SUBMITTED && InterlockedCompareExchange(&Packet->ContextOrderResubmissionPending, 0, 0) != 0)))
    {
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        if (Packet->Device != NULL &&
            InterlockedCompareExchange(
                &Packet->Device->ExecutionState, 0, 0) ==
                D3DKMT_DEVICEEXECUTION_ERROR_DMAPAGEFAULT)
        {
            Status =
                STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE;
        }
        else
#endif
        {
            Packet->Kicked = TRUE;
            SubmissionOwned = TRUE;
            RtlZeroMemory(&SubmitArgs, sizeof(SubmitArgs));
            SubmitArgs.hContext = ((PDXGKRNL_CONTEXT)Packet->Context)->hMiniportContext;
            SubmitArgs.DmaBufferVirtualAddress = Packet->DmaBufferGpuVa;
            SubmitArgs.DmaBufferSize = Packet->VirtualDmaBufferSize;
            SubmitArgs.pDmaBufferPrivateData = Packet->DriverPrivateData;
            SubmitArgs.DmaBufferPrivateDataSize = Packet->DriverPrivateDataSize;
            SubmitArgs.DmaBufferUmdPrivateDataSize = Packet->UmdPrivateDataSize;
            SubmitArgs.SubmissionFenceId = Packet->SubmissionFenceId;
            SubmitArgs.VidPnSourceId = Packet->VidPnSourceId;
            SubmitArgs.FlipInterval = Packet->FlipInterval;
            SubmitArgs.Flags.Value = Packet->SubmitFlags;
            SubmitArgs.EngineOrdinal = Packet->EngineOrdinal;
            SubmitArgs.NodeOrdinal = Packet->NodeOrdinal;
        }
    }
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

    if (SubmissionOwned && DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual) != NULL)
        KmdCallAcquired = DxgkAcquireKmdCall(Adapter);

    if (KmdCallAcquired &&
        !(Packet->SubmitFlags & VIDSCH_SUBMITFLAG_NULLRENDERING))
    {
        /* Keep reset rundown across both DDIs so a reset cannot invalidate
         * the root association between publication and command submission. */
        Status = DxgkGpuVaSetRootPageTable(Adapter,
                                          Packet->Device->ProcessRecord,
                                          (PDXGKRNL_CONTEXT)Packet->Context);
        if (!NT_SUCCESS(Status))
        {
            DxgkReleaseKmdCall(Adapter);
            KmdCallAcquired = FALSE;
        }
    }

    if (KmdCallAcquired)
    {
        PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched = VidSchpScheduler(Adapter);

        if (Sched != NULL)
        {
            (VOID)Sched->PublishDispatch(Sched->SchedulerHandle, Engine->SchedulerOrdinal, Packet->SchedulerClaimToken);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
            VidSchpPublishActivePacket(Packet);
#endif
        }
        VidSchAccountNodeDispatch(Packet);
        DxgkPublishSubmittedFence(Adapter, Packet->NodeOrdinal, Packet->SubmissionFenceId);
        VidSchpRecordDispatch(Engine, Packet);
        /* Publication point.  Everything this submission depends on -- the
         * DMA buffer the client filled in, the page-table updates covering
         * it, and the fence values published just above -- must be globally
         * visible before the engine is told to execute it.  Client mappings
         * are write-combined, and WC stores retire out of order without an
         * explicit fence, so without this the engine can fetch a partially
         * written batch and dereference a not-yet-written pointer. */
        KeMemoryBarrier();
        {
            DPT_SCOPE DdiTrace = DptBegin(&g_DxgPresentTrace, DPT_KMD_SUBMIT);
            _SEH2_TRY
            {
                Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual)(Adapter->MiniportDeviceContext, &SubmitArgs);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            DptEnd(&g_DxgPresentTrace, DdiTrace, NT_SUCCESS(Status), 0);
        }
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(0x119, 0x2, (ULONG_PTR)Status, (ULONG_PTR)&SubmitArgs, (ULONG_PTR)Engine);
    }

    if (KmdCallAcquired)
    {
        /* Tracked virtual Presents own the same allocation/scanout lifetime
         * as physical packets. Publish the tracker before completion replay. */
        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        if (!Packet->TrackerOwnsDmaBuffer && Packet->TrackerReservation != NULL)
        {
            Reservation = Packet->TrackerReservation;
            Packet->TrackerOwnsDmaBuffer = TRUE;
            Reservation->FenceIdentityOwned = TRUE;
            Reservation->FenceIdentityEpoch = Packet->FenceIdentityEpoch;
            Packet->FenceIdentityReserved = FALSE;
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        if (Reservation != NULL)
            DxgkCommitTrackedDmaBuffer(Adapter, Reservation);
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
            DXGKRNL_ERR("VidSch: DxgkDdiSubmitCommandVirtual failed 0x%08lX engine=%lu fence=%lu\n", Status, Packet->EngineOrdinal, Packet->SubmissionFenceId);
            AbortStatus = (NTSTATUS)InterlockedCompareExchange((volatile LONG *)&Packet->ContextOrderAbortStatus, 0, 0);
            if (AbortStatus != STATUS_PENDING)
                CommitStatus = AbortStatus;
        }
        if (Sched != NULL)
        {
            ULONGLONG CommitToken = Packet->SchedulerClaimToken;

            Packet->SchedulerClaimToken = 0;
            (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->SchedulerOrdinal, CommitToken, CommitStatus);
            Removed =
                VidSchpDrainRetirements(Adapter) != 0;
            if (Removed)
            {
                DxgkRetireCompletedDmaBuffers(Adapter);
                KeSetEvent(&Engine->CompletionEvent, IO_NO_INCREMENT, FALSE);
            }
        }
        /* A successful dispatch leaves the FIFO head in flight. Wake the
         * context owning the next undispatched packet so the engine can keep
         * a hardware queue instead of waiting for the head to retire. */
        (VOID)VidSchpKickEngine(Engine, NULL);
    }

    if (Packet != NULL)
        VidSchpDereferencePacket(Packet);
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
    BOOLEAN DispatchedAny = FALSE;

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
            return DispatchedAny;

        RtlZeroMemory(&Claim, sizeof(Claim));
        Claim.Size = DXGMMS2_SCHEDULER_CLAIM_V1_SIZE;
        Claim.Version = DXGMMS2_SCHEDULER_VERSION_1;
        if (!NT_SUCCESS(Sched->ClaimNextPacket(Sched->SchedulerHandle, Engine->SchedulerOrdinal, &Claim)))
            return DispatchedAny;
        Packet = VidSchpPacketFromCookie(Claim.PacketCookie);
        if (Packet == NULL)
        {
            (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->SchedulerOrdinal, Claim.ClaimToken, STATUS_INVALID_PARAMETER);
            return DispatchedAny;
        }
        ClaimToken = Claim.ClaimToken;
        Packet->SchedulerClaimToken = ClaimToken;
        if (Packet->SubmissionFenceId == 0)
            Packet->SubmissionFenceId = Claim.SubmissionFenceId;

        /*
         * A provider claim cannot name an ordinarily dispatched packet.
         * Therefore Kicked on a newly claimed packet is the preemption reset
         * marker left by the completion DPC.  Consume it while the claim keeps
         * cancellation from removing the packet, before deciding whether the
         * context stream must authorize a resubmission.
         */
        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        if (Packet->Kicked)
        {
            Packet->Kicked = FALSE;
            InterlockedExchange(&Packet->ContextOrderResubmissionPending, 1);
            DXGKRNL_INFO("VidSch: packet %p fence=%lu ctx=%p marked for resubmission seq=#%I64d\n",
                         Packet, Packet->SubmissionFenceId, Packet->Context, DxgkDiagSequence());
            if (DxgkCapsCoreInterfaceVersionAtLeast(
                    Adapter->MiniportContext->InitData.s.Version,
                    DXGK_CAPS_CORE_LEVEL_WDDM_2_0))
                Packet->SubmitFlags |= VIDSCH_SUBMITFLAG_RESUBMISSION;
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);

        /* Ordered context work may only run for its authorised claimant and
         * only at PASSIVE_LEVEL; hand the claim back otherwise. */
        if (Packet->ContextOrderOperation != NULL && (AuthorizedPacket != Packet || (InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) != VIDSCH_CONTEXT_ORDER_DISPATCHING && (InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) != VIDSCH_CONTEXT_ORDER_SUBMITTED || InterlockedCompareExchange(&Packet->ContextOrderResubmissionPending, 0, 0) == 0)) || KeGetCurrentIrql() != PASSIVE_LEVEL))
        {
            VidSchReferenceContextOrderPacket(Packet);
            (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->SchedulerOrdinal, ClaimToken, STATUS_SUCCESS);
            DxgkContextOrderScheduleReferenced((PDXGKRNL_CONTEXT)Packet->Context);
            VidSchDereferenceContextOrderPacket(Packet);
            return DispatchedAny;
        }
        if (Packet->ContextOrderOperation == NULL && AuthorizedPacket != NULL)
        {
            (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->SchedulerOrdinal, ClaimToken, STATUS_SUCCESS);
            return DispatchedAny;
        }
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        if (Packet->Device != NULL &&
            InterlockedCompareExchange(
                &Packet->Device->ExecutionState,
                0,
                0) ==
                D3DKMT_DEVICEEXECUTION_ERROR_DMAPAGEFAULT)
        {
            NTSTATUS FaultStatus =
                STATUS_GRAPHICS_GPU_EXCEPTION_ON_DEVICE;

            /*
             * Provider cancellation can release GPU-VA and allocation state.
             * Leave the claim uncommitted in DPC context; the fault cleanup
             * worker cancels this device's queued owners at PASSIVE_LEVEL.
             */
            if (KeGetCurrentIrql() != PASSIVE_LEVEL)
            {
                (VOID)Sched->CompleteDispatch(
                    Sched->SchedulerHandle,
                    Engine->SchedulerOrdinal,
                    ClaimToken,
                    STATUS_SUCCESS);
                return DispatchedAny;
            }

            Packet->SchedulerClaimToken = 0;
            (VOID)Sched->CompleteDispatch(
                Sched->SchedulerHandle,
                Engine->SchedulerOrdinal,
                ClaimToken,
                FaultStatus);
            if (Packet->ContextOrderOperation != NULL)
                DxgkContextOrderCommitPacket(Packet, FaultStatus);
            (VOID)VidSchpDrainRetirements(Adapter);
            KeSetEvent(
                &Engine->CompletionEvent,
                IO_NO_INCREMENT,
                FALSE);
            if (AuthorizedPacket != NULL)
                return TRUE;
            continue;
        }
#endif
        if (Packet->VirtualAddressing)
        {
            /* Native dxgmms2 submits from its persistent scheduler worker.
             * Context-ordered virtual packets arrive here at PASSIVE_LEVEL
             * with an authorized claim, so retain the packet across the
             * callback and execute it on that same worker. */
            ASSERT(AuthorizedPacket == Packet);
            ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
            InterlockedIncrement(&Packet->ReferenceCount);
            VidSchpSubmitVirtualPacket(Engine, Packet);
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
        SubmitArgs.FlipInterval = Packet->FlipInterval;
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
            (VOID)Sched->PublishDispatch(Sched->SchedulerHandle, Engine->SchedulerOrdinal, ClaimToken);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
            VidSchpPublishActivePacket(Packet);
#endif
            VidSchAccountNodeDispatch(Packet);
            DxgkPublishSubmittedFence(Adapter, Packet->NodeOrdinal, Packet->SubmissionFenceId);
            VidSchpRecordDispatch(Engine, Packet);
            /* Same publication requirement as the virtual submission path. */
            KeMemoryBarrier();
            KeRaiseIrql(DISPATCH_LEVEL, &CallIrql);
            {
                DPT_SCOPE DdiTrace = DptBegin(&g_DxgPresentTrace, DPT_KMD_SUBMIT);
                _SEH2_TRY
                {
                    Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand)(Adapter->MiniportDeviceContext, &SubmitArgs);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                DptEnd(&g_DxgPresentTrace, DdiTrace, NT_SUCCESS(Status), 0);
            }
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
                Reservation->FenceIdentityEpoch = Packet->FenceIdentityEpoch;
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
        /*
         * Close tracker preparation before releasing the provider claim.
         * Completion/fault DPCs may have stored a watermark while the claim
         * was open; replaying it can retire the packet immediately, so the
         * tracker must already be list-owned before that replay is visible.
         */
        if (Reservation != NULL)
            DxgkCommitTrackedDmaBuffer(Adapter, Reservation);
        (VOID)Sched->CompleteDispatch(Sched->SchedulerHandle, Engine->SchedulerOrdinal, ClaimToken, KmdCallAcquired ? Status : STATUS_DELETE_PENDING);
        if (!KmdCallAcquired)
            KickNext = TRUE;

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
        {
            /* Completion can run while the provider claim is still open.
             * Its kick then finds the engine Submitting and cannot wake the
             * next context. Once the claim is committed, reconsider the next
             * FIFO entry here even if retirement already consumed this one.
             * Continue iteratively for contextless packets; ordered work is
             * handed back to its own worker by the authorization check. */
            DispatchedAny = TRUE;
            AuthorizedPacket = NULL;
        }
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
    NTSTATUS Status;
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
    Ctx->CallbackState = 1;

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
        Engine->SchedulerOrdinal = i;
        Engine->PendingPreemptionFenceId = 0;
        Engine->PendingPreemptionEngineOrdinal = 0;
        Engine->PreemptionDdiState = 0;
        Engine->PreemptionInterruptPending = 0;
        Engine->CompletedPreemptionFenceId = 0;
        Engine->CompletedPreemptionEngineOrdinal = 0;
        Engine->NextFenceId = 1;

        KeInitializeSpinLock(&Engine->QueueLock);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
        InitializeListHead(&Engine->ActivePacketList);
#endif
        KeInitializeEvent(&Engine->CompletionEvent, SynchronizationEvent, FALSE);
        KeInitializeEvent(&Engine->PreemptionCompletedEvent, NotificationEvent, FALSE);
        KeInitializeEvent(&Engine->WorkersDrainedEvent, NotificationEvent, TRUE);


        KeInitializeTimer(&Engine->TdrTimer);
        KeInitializeDpc(&Engine->TdrDpc, VidSchpTdrDpcRoutine, Engine);
    }

    Status = DxgkContextOrderStartSchedulerWorker(Ctx);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Ctx->Engines, TAG_VIDSCH);
        ExFreePoolWithTag(Ctx, TAG_VIDSCH);
        return Status;
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
    /*
     * Cancellation touches both the provider interface and engine storage.
     * Join the scheduler lifecycle before reading either so stop cannot pass
     * its active-call drain and free them underneath this path.
     */
    if (!VidSchpAcquireCall(Adapter))
        return;
    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL)
    {
        VidSchpReleaseCall(Adapter);
        return;
    }
    Sched = VidSchpScheduler(Adapter);
    if (Sched != NULL)
    {
        (VOID)Sched->CancelOwnerPackets(Sched->SchedulerHandle, (ULONGLONG)(ULONG_PTR)Context, CompletionStatus);
        VidSchpDrainRetirements(Adapter);
    }
    for (i = 0; i < Ctx->EngineCount; ++i)
        (VOID)VidSchpKickEngine(&Ctx->Engines[i], NULL);
    VidSchpReleaseCall(Adapter);
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

    PAGED_CODE();

    if (Adapter == NULL)
        return;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL)
        return;

    VidSchPrepareForStop(Adapter);
    VidSchAbortAllPackets(Adapter, STATUS_DEVICE_REMOVED);
    DxgkContextOrderStopSchedulerWorker(Ctx);

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

    if (!VidSchPolicyNodeEngineSupported(
            NodeOrdinal,
            EngineOrdinal,
            Ctx->EngineCount))
        return STATUS_INVALID_PARAMETER;

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
    InitializeListHead(&Packet->ActiveEngineEntry);
    Packet->ReferenceCount = 1;
    ExInitializeWorkItem(
        &Packet->DestroyWorkItem,
        VidSchpDestroyPacketWorker,
        Packet);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    ExInitializeWorkItem(
        &Packet->CleanupWorkItem,
        VidSchpFaultCleanupWorker,
        Packet);
#endif

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
    ULONG FenceId;
    ULONG AdmittedFenceId;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || OutFenceId == NULL || (SubmitFlags & ~0xffu) != 0 || (Adapter->SchedulingCaps.MultiEngineAware && MiniportContextHandle == NULL) || (!Adapter->SchedulingCaps.MultiEngineAware && MiniportDeviceHandle == NULL))
        return STATUS_INVALID_PARAMETER;

    *OutFenceId = 0;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Status = VidSchpPrepareSubmit(Adapter, NodeOrdinal, EngineOrdinal, FALSE, &Engine, &Packet);
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
    if (Packet->SubmissionFenceId == 0 || !DxgkReserveSubmissionFenceIdentity(Adapter, NodeOrdinal, Packet->SubmissionFenceId, &Packet->FenceIdentityEpoch))
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
    PDXGKRNL_CONTEXT KickContext;
    ULONG EngineOrdinal;
    ULONG FenceId;
    ULONG AdmittedFenceId;
    ULONG KmdPrivateDataSize;
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || Context == NULL || DmaBufferGpuVa == 0 || DmaBufferSize == 0 || OutFenceId == NULL || (DriverPrivateDataSize != 0 && DriverPrivateData == NULL))
        return STATUS_INVALID_PARAMETER;
    KmdPrivateDataSize = Context->ContextInfo.DmaBufferPrivateDataSize;
    if (DriverPrivateDataSize > KmdPrivateDataSize)
        return STATUS_INVALID_PARAMETER;
    /* A non-null virtual submission needs the miniport's virtual submission
     * callback, which interprets the command address and private payload. */
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
    Status = VidSchpPrepareSubmit(Adapter, Context->NodeOrdinal, EngineOrdinal, FALSE, &Engine, &Packet);
    if (!NT_SUCCESS(Status))
    {
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    if (Context->Device == NULL ||
        Context->Device->Adapter != Adapter ||
        !DxgkReferenceDevice(Context->Device))
    {
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DELETE_PENDING;
    }
    Packet->Device = Context->Device;

    if (KmdPrivateDataSize != 0)
    {
        Packet->OwnedDriverPrivateData = ExAllocatePoolWithTag(NonPagedPool, KmdPrivateDataSize, TAG_VIDSCH);
        if (Packet->OwnedDriverPrivateData == NULL)
        {
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlZeroMemory(Packet->OwnedDriverPrivateData, KmdPrivateDataSize);
        if (DriverPrivateDataSize != 0)
            RtlCopyMemory(Packet->OwnedDriverPrivateData, DriverPrivateData, DriverPrivateDataSize);
        Packet->DriverPrivateData = Packet->OwnedDriverPrivateData;
        Packet->DriverPrivateDataSize = KmdPrivateDataSize;
    }
    Packet->UmdPrivateDataSize = DriverPrivateDataSize;

    Packet->DmaBufferGpuVa = DmaBufferGpuVa;
    Packet->VirtualDmaBufferSize = DmaBufferSize;
    /*
     * Pin an existing command-start mapping before queueing. Miniports can
     * instead carry their commands in private data, with no VidMm mapping
     * at DmaBufferGpuVa. The lookup and optional pin are atomic, and CPU
     * inspection below is only valid when a mapping was actually pinned.
     */
    if (!NullRendering)
    {
        PDXGKRNL_DEVICE PinDevice = (PDXGKRNL_DEVICE)Context->Device;
        BOOLEAN Pinned;

        if (PinDevice == NULL || PinDevice->ProcessRecord == NULL ||
            !DxgkGpuVaPinCommandStart(Adapter,
                                      PinDevice->ProcessRecord,
                                      DmaBufferGpuVa,
                                      &Pinned))
        {
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return STATUS_INVALID_PARAMETER;
        }
        if (Pinned)
        {
            Packet->GpuVaPinProcess = PinDevice->ProcessRecord;
            /* Capture the batch head now, under the pin, for fault attribution. */
            Packet->BatchHeadBytes = min(DmaBufferSize, sizeof(Packet->BatchHead));
            if (!DxgkGpuVaCopyFromProcess(PinDevice->ProcessRecord,
                                          DmaBufferGpuVa,
                                          Packet->BatchHead,
                                          Packet->BatchHeadBytes))
            {
                Packet->BatchHeadBytes = 0;
            }
            else
            {
                VidSchpScanStateBaseAddress(Context, PinDevice->ProcessRecord, DmaBufferGpuVa, DmaBufferSize);
            }
        }
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
    Status = Sched->ReserveSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
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
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    /* Teardown must not cancel the logical marker before its packet reaches
     * the scheduler, then miss that packet when cancelling the owner queue. */
    if (!ExAcquireRundownProtection(&Context->StreamAdmissionRundown))
    {
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_DELETE_PENDING;
    }
    Status = DxgkContextOrderAdmitPacket(Context, Packet);
    if (!NT_SUCCESS(Status))
    {
        ExReleaseRundownProtection(&Context->StreamAdmissionRundown);
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    /*
     * Secure both admission slots before assigning the hardware fence.
     * SubmitCommand retries a full context stream internally; assigning a
     * fence before that check consumed IDs for packets the miniport never
     * received, stranding its completion window behind those missing IDs.
     * LifecycleMutex keeps assignment ordered with scheduler admission.
     */
    Packet->SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
    if (Packet->SubmissionFenceId == 0 || !DxgkReserveSubmissionFenceIdentity(Adapter, Context->NodeOrdinal, Packet->SubmissionFenceId, &Packet->FenceIdentityEpoch))
    {
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkContextOrderAbortPacket(Packet, STATUS_DEVICE_BUSY);
        VidSchpDereferencePacket(Packet);
        ExReleaseRundownProtection(&Context->StreamAdmissionRundown);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    Packet->FenceIdentityReserved = TRUE;
    FenceId = Packet->SubmissionFenceId;
    /* Publication can retire the packet and its original context reference
     * before AdmitPacket returns. Pin the later kick before that transfer. */
    KickContext = DxgkReferenceContext(Context) ? Context : NULL;
    Packet->HoldsContextReference = TRUE;
    Status = VidSchpAdmitPacket(Adapter, Packet, DXGMMS2_SCHEDULER_ADMIT_CONSUME_RESERVATION, &AdmittedFenceId);
    if (!NT_SUCCESS(Status))
    {
        /* The caller still owns its context reference on failed admission. */
        Packet->HoldsContextReference = FALSE;
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkContextOrderAbortPacket(Packet, Status);
        VidSchpDereferencePacket(Packet);
        if (KickContext != NULL)
            DxgkDereferenceContext(KickContext);
        ExReleaseRundownProtection(&Context->StreamAdmissionRundown);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    ExReleaseFastMutex(&Ctx->LifecycleMutex);
    ExReleaseRundownProtection(&Context->StreamAdmissionRundown);
    if (KickContext != NULL)
    {
        DxgkContextOrderKickContext(KickContext);
        DxgkDereferenceContext(KickContext);
    }
    *OutFenceId = FenceId;
    VidSchpReleaseCall(Adapter);
    return STATUS_SUCCESS;
}

static NTSTATUS
VidSchSubmitCommandTrackedMeasured(
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
    PDXGKRNL_CONTEXT KickContext;
    PDXGKRNL_CONTEXT OrderedContext = NULL;
    PDXGKRNL_SUBMIT_DMA_BUFFER Reservation = NULL;
    PDXGKRNL_DEVICE PacketDevice;
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
    if ((TrackArgs->Context != NULL && TrackArgs->Context->VirtualAddressing) !=
        (DmaBuffer->VirtualBacking != NULL))
        return STATUS_INVALID_PARAMETER;
    if (DmaBuffer->VirtualBacking != NULL &&
        (DmaBuffer->GpuVirtualAddress == 0 || PatchLocationListCount != 0 ||
         DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommandVirtual) == NULL))
        return STATUS_INVALID_PARAMETER;
    if (!VidSchpAcquireCall(Adapter))
        return STATUS_DELETE_PENDING;

    Status = VidSchpPrepareSubmit(Adapter, NodeOrdinal, EngineOrdinal, TRUE, &Engine, &Packet);
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
    if (!DxgkReserveSubmissionFenceIdentity(Adapter, NodeOrdinal, Packet->SubmissionFenceId, &Packet->FenceIdentityEpoch))
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
    Packet->FlipInterval = TrackArgs->FlipInterval;

    if (DmaBuffer->VirtualBacking != NULL)
    {
        Packet->VirtualAddressing = TRUE;
        Packet->DmaBufferGpuVa = DmaBuffer->GpuVirtualAddress + DmaBuffer->SubmissionStartOffset;
        Packet->VirtualDmaBufferSize = DmaBuffer->SubmissionEndOffset - DmaBuffer->SubmissionStartOffset;
        /* The DMA backing owner pins commands and Present data through
         * tracker retirement; this packet must not acquire a second owner. */
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

    LocalTrackArgs = *TrackArgs;
    LocalTrackArgs.SubmissionFenceId = Packet->SubmissionFenceId;
    LocalTrackArgs.NodeOrdinal = NodeOrdinal;
    LocalTrackArgs.EngineOrdinal = EngineOrdinal;
    LocalTrackArgs.DmaBuffer = DmaBuffer;
    PacketDevice = TrackArgs->Device;
    if (PacketDevice != NULL &&
        PacketDevice->Adapter != Adapter)
    {
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_HANDLE;
    }
    if (TrackArgs->Context != NULL)
    {
        if (TrackArgs->Context->Device == NULL ||
            TrackArgs->Context->Device->Adapter != Adapter ||
            (PacketDevice != NULL &&
             PacketDevice != TrackArgs->Context->Device))
        {
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return STATUS_INVALID_HANDLE;
        }
        PacketDevice = TrackArgs->Context->Device;
    }
    Status = DxgkPrepareTrackedDmaBuffer(Adapter, &LocalTrackArgs, &Reservation);
    if (!NT_SUCCESS(Status))
    {
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    if (PacketDevice != NULL)
    {
        if (!DxgkReferenceDevice(PacketDevice))
        {
            DxgkCancelTrackedDmaBuffer(Reservation);
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return STATUS_DELETE_PENDING;
        }
        Packet->Device = PacketDevice;
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

    if (!Packet->VirtualAddressing && DXGK_CB_FULL(Adapter, DxgkDdiPatch) != NULL)
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
        /* Patch completes before this call returns. The caller's captured
         * lists remain valid here; only DMA/private data and residency pins
         * must survive asynchronous scheduler dispatch and retirement. */
        PatchArgs.pAllocationList = AllocationList;
        PatchArgs.AllocationListSize = AllocationListCount;
        PatchArgs.pPatchLocationList = PatchLocationList;
        PatchArgs.PatchLocationListSize = PatchLocationListCount;
        PatchArgs.PatchLocationListSubmissionStart = 0;
        PatchArgs.PatchLocationListSubmissionLength = PatchLocationListCount;
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

    /* Render, BuildPagingBuffer, and Patch write a cached CPU mapping while
     * the adapter consumes the physical DMA buffer directly.  Publish the
     * final byte range itself instead of relying on an unrelated allocation
     * cache clean to evict these writes as a side effect. */
    Status = DxgkFlushDmaBufferForSubmission(DmaBuffer);
    if (!NT_SUCCESS(Status))
    {
        DxgkCancelTrackedDmaBuffer(Reservation);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
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
    Status = Sched->ReserveSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
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
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        DxgkCancelTrackedDmaBuffer(Reservation);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    if (Packet->Context != NULL)
    {
        OrderedContext = (PDXGKRNL_CONTEXT)Packet->Context;
        if (!ExAcquireRundownProtection(&OrderedContext->StreamAdmissionRundown))
        {
            Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
            ExReleaseFastMutex(&Ctx->LifecycleMutex);
            DxgkCancelTrackedDmaBuffer(Reservation);
            VidSchpDereferencePacket(Packet);
            VidSchpReleaseCall(Adapter);
            return STATUS_DELETE_PENDING;
        }
        Status = DxgkContextOrderAdmitPacket(OrderedContext, Packet);
        if (!NT_SUCCESS(Status))
        {
            ExReleaseRundownProtection(&OrderedContext->StreamAdmissionRundown);
            Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
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
        Sched->ReleaseSlot(Sched->SchedulerHandle, Engine->SchedulerOrdinal);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        if (Packet->ContextOrderOperation != NULL)
            DxgkContextOrderAbortPacket(Packet, Status);
        if (OrderedContext != NULL)
            ExReleaseRundownProtection(&OrderedContext->StreamAdmissionRundown);
        VidSchpDereferencePacket(Packet);
        VidSchpReleaseCall(Adapter);
        return Status;
    }
    KickContext = NULL;
    if (Packet->ContextOrderOperation != NULL)
    {
        if (DxgkReferenceContext((PDXGKRNL_CONTEXT)Packet->Context))
            KickContext = (PDXGKRNL_CONTEXT)Packet->Context;
    }
    ExReleaseFastMutex(&Ctx->LifecycleMutex);
    if (OrderedContext != NULL)
        ExReleaseRundownProtection(&OrderedContext->StreamAdmissionRundown);

    if (KickContext != NULL)
    {
        DxgkContextOrderKickContext(KickContext);
        DxgkDereferenceContext(KickContext);
    }
    else if (Packet->ContextOrderOperation == NULL)
        (VOID)VidSchpKickEngine(Engine, NULL);
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
    DPT_SCOPE Trace = DptBegin(&g_DxgPresentTrace, DPT_KERNEL_TRACK);
    NTSTATUS Result = VidSchSubmitCommandTrackedMeasured(Adapter, NodeOrdinal, EngineOrdinal, DmaBuffer, DriverPrivateData, DriverPrivateDataSize, AllocationList, AllocationListCount, PatchLocationList, PatchLocationListCount, MiniportDeviceHandle, MiniportContextHandle, Priority, TrackArgs, SubmitFlags, VidPnSourceId, OutFenceId);
    DptEnd(&g_DxgPresentTrace, Trace, NT_SUCCESS(Result), 0);
    return Result;
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
    ULONG EngineOrdinal = 0;
    BOOLEAN PreemptionNotification = FALSE;

    if (Adapter == NULL || NotifyData == NULL)
        return;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
    if (NotifyData->InterruptType ==
        DXGK_INTERRUPT_DMA_PAGE_FAULTED)
    {
        ULONG FaultedFence =
            NotifyData->DmaPageFaulted.FaultedFenceId;

        NodeOrdinal =
            NotifyData->DmaPageFaulted.NodeOrdinal;
        if (InterlockedCompareExchange(
                &Adapter->TdrCompletionNotificationsEnabled,
                0,
                0) == 0 ||
            Adapter->MiniportContext == NULL ||
            !DxgkCapsCoreInterfaceVersionAtLeast(
                Adapter->MiniportContext->InitData.s.Version,
                DXGK_CAPS_CORE_LEVEL_WDDM_2_0) ||
            Ctx == NULL ||
            !Ctx->Initialized ||
            !VidSchPolicyNodeEngineSupported(
                NodeOrdinal,
                NotifyData->DmaPageFaulted.EngineOrdinal,
                Adapter->NodeCount) ||
            NodeOrdinal >= DXGK_MAX_TRACKED_NODES ||
            NodeOrdinal >= Ctx->EngineCount ||
            FaultedFence == 0 ||
            (NotifyData->DmaPageFaulted.PageFaultFlags &
                 DXGK_PAGE_FAULT_FENCE_INVALID) != 0)
        {
            return;
        }

        LastSubmittedFence = (ULONG)InterlockedCompareExchange(
            (volatile LONG *)&Adapter->
                NodeLastSubmittedFenceId[NodeOrdinal],
            0,
            0);
        if (LastSubmittedFence == 0 ||
            !VidSchpFenceReached(
                LastSubmittedFence,
                FaultedFence) ||
            !DxgkIsSubmittedFenceIdentity(
                Adapter,
                NodeOrdinal,
                FaultedFence))
        {
            return;
        }

        Engine = &Ctx->Engines[NodeOrdinal];
        if (InterlockedCompareExchange(
                &Engine->PageFaultInterruptState, 1, 0) != 0)
        {
            return;
        }
        Engine->PageFaultInterruptData = *NotifyData;
        KeMemoryBarrier();
        InterlockedExchange(
            &Engine->PageFaultInterruptState, 2);

        InterlockedExchange(&Engine->CompletionPending, 1);
        return;
    }
#endif

    if (NotifyData->InterruptType == DXGK_INTERRUPT_DMA_COMPLETED)
    {
        FenceId = NotifyData->DmaCompleted.SubmissionFenceId;
        NodeOrdinal = NotifyData->DmaCompleted.NodeOrdinal;
        EngineOrdinal = NotifyData->DmaCompleted.EngineOrdinal;
    }
    else if (NotifyData->InterruptType == DXGK_INTERRUPT_DMA_PREEMPTED)
    {
        FenceId = NotifyData->DmaPreempted.LastCompletedFenceId;
        NodeOrdinal = NotifyData->DmaPreempted.NodeOrdinal;
        EngineOrdinal = NotifyData->DmaPreempted.EngineOrdinal;
        PreemptionNotification = TRUE;
    }
    else
        return;

    if (InterlockedCompareExchange(&Adapter->TdrCompletionNotificationsEnabled, 0, 0) == 0)
        KeBugCheckEx(0x119, 0x10, (ULONG_PTR)NotifyData, (ULONG_PTR)Adapter, 0);

    if (!VidSchPolicyNodeEngineSupported(
            NodeOrdinal,
            EngineOrdinal,
            Adapter->NodeCount) ||
        NodeOrdinal >= DXGK_MAX_TRACKED_NODES)
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
        InterlockedExchange(&Engine->CompletionPending, 1);
}

/*
 * VidSchNotifyDpc
 *
 * Called by the miniport at DISPATCH_LEVEL for events it published through
 * NotifyInterrupt. Process them here rather than queueing a second DPC.
 */
VOID
VidSchNotifyDpc(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    ULONG Index;

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);
    if (!VidSchpAcquireCall(Adapter))
        return;
    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx != NULL && Ctx->Initialized)
    {
        for (Index = 0; Index < Ctx->EngineCount; ++Index)
        {
            PVIDSCH_ENGINE Engine = &Ctx->Engines[Index];

            /* A submit made during cleanup may synchronously notify another
             * completion. Concurrent miniport DPCs can do the same. Publish
             * pending work first, and let one owner consume it. Never spin
             * waiting for that owner: it may be this CPU's outer callback. */
            while (InterlockedCompareExchange(
                       &Engine->CompletionPending, 0, 0) != 0)
            {
                if (InterlockedCompareExchange(
                        &Engine->CompletionActive, 1, 0) != 0)
                    break;

                while (InterlockedExchange(&Engine->CompletionPending, 0) != 0)
                    VidSchpProcessCompletion(Engine);

                InterlockedExchange(&Engine->CompletionActive, 0);
                /* Recheck after releasing ownership. A publisher could
                 * have set Pending and observed Active just before release;
                 * without this check its notification would be stranded. */
            }
        }
    }
    VidSchpReleaseCall(Adapter);
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
    ULONGLONG Deadline;
    ULONGLONG Now;
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

    /*
     * One deadline covers every wakeup and every engine.  Reusing the full
     * relative interval after each progress event can otherwise turn a
     * bounded suspend/teardown wait into an unbounded one.
     */
    Deadline = KeQueryInterruptTime() + (ULONGLONG)TimeoutMs * 10000ULL;

    for (i = 0; i < Ctx->EngineCount; i++)
    {
        PVIDSCH_ENGINE Engine = &Ctx->Engines[i];
        VIDSCH_ENGINE_STATE EngineState;
        VIDSCH_SCHEDULER_STATE SchedulerState;

        for (;;)
        {
            EngineState = VidSchpReadState(Engine);
            SchedulerState = VidSchpReadSchedulerState(Ctx);
            if (VidSchpEnginePendingCount(Adapter, Engine->SchedulerOrdinal) == 0 && (EngineState == VidSchEngineIdle || (EngineState == VidSchEngineSuspended && (SchedulerState == VidSchSchedulerSuspending || SchedulerState == VidSchSchedulerSuspended))))
                break;
            Now = KeQueryInterruptTime();
            if (Now >= Deadline)
                Status = STATUS_TIMEOUT;
            else
            {
                Timeout.QuadPart = -(LONGLONG)(Deadline - Now);
                Status = KeWaitForSingleObject(&Engine->CompletionEvent,
                                               Executive,
                                               KernelMode,
                                               FALSE,
                                               &Timeout);
            }
            if (Status == STATUS_TIMEOUT)
            {
                DXGKRNL_WARN("VidSch: WaitForIdle timeout on engine %lu "
                             "(state=%d pending=%lu)\n",
                             i, (int)VidSchpReadState(Engine),
                             VidSchpEnginePendingCount(Adapter, Engine->SchedulerOrdinal));
                VidSchpReleaseCall(Adapter);
                return STATUS_IO_TIMEOUT;
            }
            if (!NT_SUCCESS(Status))
            {
                VidSchpReleaseCall(Adapter);
                return Status;
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
        Status = Sched->QueryEngineStatus(Sched->SchedulerHandle, Engine->SchedulerOrdinal, &EngineStatus);
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
 * Preemption, reset, and recovery
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
    if (!VidSchPolicyNodeEngineSupported(
            NodeOrdinal,
            EngineOrdinal,
            Ctx->EngineCount))
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
    DXGKRNL_INFO("VidSch: DxgkDdiPreemptCommand node=%lu engine=%lu -> 0x%08lX seq=#%I64d\n",
                 NodeOrdinal, EngineOrdinal, Status, DxgkDiagSequence());
    if (NT_SUCCESS(Status))
    {
        InterlockedCompareExchange(&Engine->PreemptionDdiState, 2, 1);
        /* An attempt is counted where it was actually made: the miniport
         * accepted the request and the node now owes a DMA_PREEMPTED. */
        if (NodeOrdinal < RTL_NUMBER_OF(Adapter->NodeStatistics))
        {
            PDXGKRNL_NODE_STATISTICS OwnerStatistics;

            InterlockedIncrement64(&Adapter->NodeStatistics[NodeOrdinal].PreemptionsRequested);
#if (REACTOS_WDDM_TARGET_LEVEL >= 2000)
            KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
            OwnerStatistics = VidSchpPacketOwnerNodeStatistics(
                                  Adapter,
                                  VidSchpFirstActivePacketLocked(Engine),
                                  NodeOrdinal);
            if (OwnerStatistics != NULL)
                InterlockedIncrement64(&OwnerStatistics->PreemptionsRequested);
            KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
#else
            UNREFERENCED_PARAMETER(OwnerStatistics);
#endif
        }
    }
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
        if (VidSchpEnginePendingCount(Adapter, Engine->SchedulerOrdinal) != 0 || !VidSchpTryTransitionEngine(Engine, VidSchEngineIdle, VidSchEngineSuspended))
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
    PDXGMMS2_SCHEDULER_INTERFACE_V1 Sched;
    DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 EngineStatus;
    VIDSCH_ENGINE_STATE OldState;
    KIRQL OldIrql;
    ULONGLONG ResetCookies[DXGMMS2_SCHEDULER_MAX_RETIREMENTS];
    ULONG ResetCount;
    BOOLEAN KmdExclusive = FALSE;
    BOOLEAN KmdCallAcquired = FALSE;
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
    Sched = VidSchpScheduler(Adapter);
    if (Sched == NULL)
    {
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_NOT_READY;
    }
    ExAcquireFastMutex(&Ctx->LifecycleMutex);
    if (VidSchpReadSchedulerState(Ctx) != VidSchSchedulerRunning && VidSchpReadSchedulerState(Ctx) != VidSchSchedulerSuspended)
    {
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_DEVICE_BUSY;
    }
    KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
    OldState = VidSchpReadState(Engine);
    if (!VidSchpTryTransitionEngine(Engine, OldState, VidSchEngineResetting))
    {
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
        ExReleaseFastMutex(&Ctx->LifecycleMutex);
        VidSchpReleaseCall(Adapter);
        return STATUS_INVALID_DEVICE_STATE;
    }
    KeCancelTimer(&Engine->TdrTimer);
    KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    ExReleaseFastMutex(&Ctx->LifecycleMutex);

    /*
     * Native VidSchiResetEngine first publishes the resetting state, flushes
     * queued completion DPCs, and only then snapshots the queue passed to
     * DxgkDdiResetEngine.  The exclusive boundary additionally drains a KMD
     * submission that acquired admission just before the state publication.
     */
    DxgkBeginKmdExclusive(Adapter);
    KmdExclusive = TRUE;
    KeFlushQueuedDpcs();

    RtlZeroMemory(&EngineStatus, sizeof(EngineStatus));
    EngineStatus.Size = DXGMMS2_SCHEDULER_ENGINE_STATUS_V1_SIZE;
    EngineStatus.Version = DXGMMS2_SCHEDULER_VERSION_1;
    Status = Sched->QueryEngineStatus(Sched->SchedulerHandle,
                                      EngineOrdinal,
                                      &EngineStatus);
    if (!NT_SUCCESS(Status))
        goto ResetFailed;

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto ResetFailed;
    }
    KmdCallAcquired = TRUE;

    /*
     * Ask for the engine back before taking it.  Preemption is the graceful
     * half of the pair: a driver that can stop the current packet leaves the
     * engine in a state it defined, where a reset leaves it in whatever state
     * the hardware happened to be in.  Windows tries this first for that
     * reason.
     *
     * A refusal is expected and costs nothing -- softgpu has no atomic
     * cancellation primitive and says so rather than reporting a fence nothing
     * preempted -- so the reset below runs either way.  What this buys is that
     * a driver which *can* preempt gets the chance.
     */
    if (DXGK_CB_FULL(Adapter, DxgkDdiPreemptCommand) != NULL)
    {
        DXGKARG_PREEMPTCOMMAND PreemptArgs;
        NTSTATUS PreemptStatus;

        RtlZeroMemory(&PreemptArgs, sizeof(PreemptArgs));
        PreemptArgs.NodeOrdinal = EngineOrdinal;
        PreemptArgs.EngineOrdinal = 0;
        PreemptArgs.PreemptionFenceId = (UINT)Engine->LastCompletedFence;

        _SEH2_TRY
        {
            PreemptStatus = DXGK_CB_FULL(Adapter, DxgkDdiPreemptCommand)(Adapter->MiniportDeviceContext, &PreemptArgs);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            PreemptStatus = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(PreemptStatus))
            DXGKRNL_TRACE("TDR: preemption declined 0x%08lX, resetting engine %u\n", PreemptStatus, EngineOrdinal);
    }

    RtlZeroMemory(&ResetArgs, sizeof(ResetArgs));
    ResetArgs.NodeOrdinal = EngineOrdinal;
    ResetArgs.EngineOrdinal = 0;

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
    KmdCallAcquired = FALSE;

    if (NT_SUCCESS(Status))
    {
        if ((LONG)(ResetArgs.LastAbortedFenceId -
                   EngineStatus.LastCompletedFenceId) < 0 ||
            (LONG)(EngineStatus.LastSubmittedFenceId -
                   ResetArgs.LastAbortedFenceId) < 0)
        {
            Status = STATUS_DEVICE_PROTOCOL_ERROR;
        }
    }

    if (NT_SUCCESS(Status))
    {
        /*
         * LastAbortedFenceId is the native completion boundary: commands
         * through it are terminal after the reset, while later commands in
         * the hardware queue must be offered again.  Advance the provider to
         * that boundary first, then clear the dispatched mark only on the
         * surviving suffix.  The next claim carries the resubmission flag.
         */
        Status = Sched->NotifyCompletion(
                            Sched->SchedulerHandle,
                            EngineOrdinal,
                            ResetArgs.LastAbortedFenceId);
        if (NT_SUCCESS(Status))
            (VOID)VidSchpDrainRetirements(Adapter);
    }

    if (NT_SUCCESS(Status))
    {
        do
        {
            ResetCount = 0;
            Status = Sched->ResetDispatched(
                                Sched->SchedulerHandle,
                                EngineOrdinal,
                                ResetCookies,
                                RTL_NUMBER_OF(ResetCookies),
                                &ResetCount);
        } while (NT_SUCCESS(Status) &&
                 ResetCount == RTL_NUMBER_OF(ResetCookies));

        KeAcquireSpinLock(&Engine->QueueLock, &OldIrql);
        while (!IsListEmpty(&Engine->ActivePacketList))
        {
            PLIST_ENTRY Entry =
                RemoveHeadList(&Engine->ActivePacketList);

            InitializeListHead(Entry);
        }
        KeReleaseSpinLock(&Engine->QueueLock, OldIrql);
    }

    if (NT_SUCCESS(Status))
    {
        if (!VidSchpTryTransitionEngine(
                 Engine,
                 VidSchEngineResetting,
                 VidSchEngineResetComplete) ||
            !VidSchpTryTransitionEngine(
                 Engine,
                 VidSchEngineResetComplete,
                 VidSchpReadSchedulerState(Ctx) == VidSchSchedulerSuspended
                     ? VidSchEngineSuspended
                     : VidSchEngineIdle))
        {
            Status = STATUS_INVALID_DEVICE_STATE;
            goto ResetFailed;
        }

        DxgkEndKmdExclusive(Adapter, TRUE);
        KmdExclusive = FALSE;
        (VOID)VidSchpKickEngine(Engine, NULL);
        goto Exit;
    }

ResetFailed:
    if (KmdCallAcquired)
    {
        DxgkReleaseKmdCall(Adapter);
        KmdCallAcquired = FALSE;
    }
    (VOID)VidSchpForceEngineState(Engine, VidSchEngineError);
    if (KmdExclusive)
    {
        DxgkEndKmdExclusive(Adapter, FALSE);
        KmdExclusive = FALSE;
    }

Exit:
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

ULONG
VidSchGetSchedulerCallbackState(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PVIDSCH_CONTEXT Ctx;
    ULONG State;

    if (Adapter == NULL)
        return 0;
    if (!VidSchpAcquireCall(Adapter))
        return 0;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return 0;
    }

    State = (ULONG)InterlockedCompareExchange(&Ctx->CallbackState, 0, 0);
    VidSchpReleaseCall(Adapter);
    return State;
}

ULONG
VidSchSetSchedulerCallbackState(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ ULONG State)
{
    PVIDSCH_CONTEXT Ctx;
    ULONG PreviousState;

    if (Adapter == NULL)
        return 0;
    if (!VidSchpAcquireCall(Adapter))
        return 0;

    Ctx = (PVIDSCH_CONTEXT)Adapter->VidSchContext;
    if (Ctx == NULL || !Ctx->Initialized)
    {
        VidSchpReleaseCall(Adapter);
        return 0;
    }

    PreviousState = (ULONG)InterlockedExchange(&Ctx->CallbackState, (LONG)State);
    VidSchpReleaseCall(Adapter);
    return PreviousState;
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

        if (Sched != NULL && Sched->GetOldestDispatched(Sched->SchedulerHandle, &OldestEngine, &OldestFence, &OldestCookie))
        {
            /*
             * GetOldestDispatched drops the provider lock before returning.
             * The packet cookie is opaque and may already have been retired
             * and freed at that point; only copy the scalar values captured
             * while the provider lock was held.
             */
            if (OldestFence != 0 && OldestEngine < Ctx->EngineCount)
            {
                *FenceId = OldestFence;
                *NodeOrdinal = OldestEngine;
                *EngineOrdinal = OldestEngine;
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
