/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Typed scheduler ownership interface exported to dxgkrnl
 *
 * dxgmms2 owns the run queues; dxgkrnl owns packet content and the miniport
 * DDI calls.  Every entry point here validates its typed request, takes the
 * scheduler spinlock, mutates the core, and releases the lock before
 * returning, so no dxgmms2 lock is ever held across a dxgkrnl or miniport
 * call.  Retirement records are produced under the same lock and drained
 * separately, which keeps the exactly-once terminal guarantee.
 */

#include "dxgmms2_private.h"

static PDXGMMS2_ADAPTER_CONTEXT
Dxgmms2SchedulerContext(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = (PDXGMMS2_ADAPTER_CONTEXT)Scheduler;

    if (Context == NULL || Context->Signature != DXGMMS2_ADAPTER_SIGNATURE)
        return NULL;
    return Context;
}

/* A retirement record is produced under the scheduler lock and queued for the
 * caller to drain; the packet slot returns to the free list at the same time. */
static VOID
Dxgmms2SchedulerQueueRetirementLocked(
    _Inout_ PDXGMMS2_ADAPTER_CONTEXT Context,
    _In_ PDXGMMS2_SCHED_PACKET Packet,
    _In_ ULONG Reason,
    _In_ NTSTATUS TerminalStatus)
{
    PDXGMMS2_SCHED_RETIREMENT Record;

    if (IsListEmpty(&Context->SchedulerRetirementFreeList))
    {
        /* The record pool is sized to the packet pool, so this cannot happen
         * while packet accounting is correct. */
        ASSERT(FALSE);
        return;
    }
    Record = CONTAINING_RECORD(RemoveHeadList(&Context->SchedulerRetirementFreeList), DXGMMS2_SCHED_RETIREMENT, Entry);
    RtlZeroMemory(&Record->Record, sizeof(Record->Record));
    Record->Record.PacketCookie = Packet->PacketCookie;
    Record->Record.OwnerCookie = Packet->OwnerCookie;
    Record->Record.SubmissionFenceId = Packet->SubmissionFenceId;
    Record->Record.Reason = Reason;
    Record->Record.TerminalStatus = TerminalStatus;
    InsertTailList(&Context->SchedulerRetirementList, &Record->Entry);

    RtlZeroMemory(Packet, sizeof(*Packet));
    InsertTailList(&Context->SchedulerPacketFreeList, &Packet->Entry);
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerStart(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineCount)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Status = Dxgmms2SchedCoreStart(&Context->SchedulerCore, EngineCount);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Status;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerAdmitPacket(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ const DXGMMS2_SCHEDULER_ADMIT_INFO_V1 *Info,
    _Out_ PULONG OutFenceId)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    PDXGMMS2_SCHED_PACKET Packet;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (OutFenceId == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutFenceId = 0;
    if (Context == NULL || Info == NULL)
        return STATUS_INVALID_HANDLE;
    if (Info->Size < DXGMMS2_SCHEDULER_ADMIT_INFO_V1_SIZE || Info->Version != DXGMMS2_SCHEDULER_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if ((Info->Flags & ~DXGMMS2_SCHEDULER_ADMIT_VALID_MASK) != 0 || Info->PacketCookie == 0 || Info->Reserved != 0)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    if (IsListEmpty(&Context->SchedulerPacketFreeList))
    {
        KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
        return STATUS_DEVICE_BUSY;
    }
    Packet = CONTAINING_RECORD(RemoveHeadList(&Context->SchedulerPacketFreeList), DXGMMS2_SCHED_PACKET, Entry);
    Status = Dxgmms2SchedCoreAdmit(&Context->SchedulerCore, Info, Packet, OutFenceId);
    if (!NT_SUCCESS(Status))
    {
        RtlZeroMemory(Packet, sizeof(*Packet));
        InsertTailList(&Context->SchedulerPacketFreeList, &Packet->Entry);
    }
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Status;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerClaimNextPacket(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineOrdinal,
    _Inout_ DXGMMS2_SCHEDULER_CLAIM_V1 *Claim)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;
    BOOLEAN Claimed;

    if (Context == NULL || Claim == NULL)
        return STATUS_INVALID_HANDLE;
    if (Claim->Size < DXGMMS2_SCHEDULER_CLAIM_V1_SIZE || Claim->Version != DXGMMS2_SCHEDULER_VERSION_1)
        return STATUS_REVISION_MISMATCH;

    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Claimed = Dxgmms2SchedCoreClaim(&Context->SchedulerCore, EngineOrdinal, Claim);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Claimed ? STATUS_SUCCESS : STATUS_NO_MORE_ENTRIES;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerPublishDispatch(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineOrdinal,
    _In_ ULONGLONG ClaimToken)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Status = Dxgmms2SchedCorePublishDispatch(&Context->SchedulerCore, EngineOrdinal, ClaimToken);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Status;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerCompleteDispatch(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineOrdinal,
    _In_ ULONGLONG ClaimToken,
    _In_ NTSTATUS DispatchStatus)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    PDXGMMS2_SCHED_PACKET Failed = NULL;
    PDXGMMS2_SCHED_PACKET Retired[DXGMMS2_SCHEDULER_MAX_RETIREMENTS];
    KIRQL OldIrql;
    NTSTATUS Status;
    ULONG Count;
    ULONG Index;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Status = Dxgmms2SchedCoreCompleteDispatch(&Context->SchedulerCore, EngineOrdinal, ClaimToken, DispatchStatus, &Failed);
    if (NT_SUCCESS(Status) && Failed != NULL)
        Dxgmms2SchedulerQueueRetirementLocked(Context, Failed, Dxgmms2RetireAborted, DispatchStatus);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * The hardware may have signalled this packet's fence while the claim was
     * still outstanding, so the completion notification could not retire it.
     * Replay the sweep against the reported watermark now that the claim is
     * released; otherwise that packet would sit in the queue forever.
     */
    do
    {
        KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
        Count = Dxgmms2SchedCoreNotifyCompletion(&Context->SchedulerCore, EngineOrdinal, 0, Retired, RTL_NUMBER_OF(Retired));
        for (Index = 0; Index < Count; ++Index)
            Dxgmms2SchedulerQueueRetirementLocked(Context, Retired[Index], Dxgmms2RetireCompleted, STATUS_SUCCESS);
        KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    } while (Count == RTL_NUMBER_OF(Retired));
    return Status;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerNotifyCompletion(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineOrdinal,
    _In_ ULONG CompletedFenceId)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    PDXGMMS2_SCHED_PACKET Retired[DXGMMS2_SCHEDULER_MAX_RETIREMENTS];
    KIRQL OldIrql;
    ULONG Count;
    ULONG Index;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    /* One watermark can retire more packets than a single batch holds; keep
     * going until the queue stops yielding, so nothing is left un-retired. */
    do
    {
        KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
        Count = Dxgmms2SchedCoreNotifyCompletion(&Context->SchedulerCore, EngineOrdinal, CompletedFenceId, Retired, RTL_NUMBER_OF(Retired));
        for (Index = 0; Index < Count; ++Index)
            Dxgmms2SchedulerQueueRetirementLocked(Context, Retired[Index], Dxgmms2RetireCompleted, STATUS_SUCCESS);
        KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    } while (Count == RTL_NUMBER_OF(Retired));
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerDrainRetirements(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _Out_writes_to_(Capacity, *RetiredCount) DXGMMS2_SCHEDULER_RETIREMENT_V1 *Records,
    _In_ ULONG Capacity,
    _Out_ PULONG RetiredCount)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;
    ULONG Count = 0;

    if (RetiredCount == NULL)
        return STATUS_INVALID_PARAMETER;
    *RetiredCount = 0;
    if (Context == NULL || (Capacity != 0 && Records == NULL))
        return STATUS_INVALID_HANDLE;

    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    while (Count < Capacity && !IsListEmpty(&Context->SchedulerRetirementList))
    {
        PDXGMMS2_SCHED_RETIREMENT Record = CONTAINING_RECORD(RemoveHeadList(&Context->SchedulerRetirementList), DXGMMS2_SCHED_RETIREMENT, Entry);

        Records[Count++] = Record->Record;
        RtlZeroMemory(&Record->Record, sizeof(Record->Record));
        InsertTailList(&Context->SchedulerRetirementFreeList, &Record->Entry);
    }
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    *RetiredCount = Count;
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerCancelOwnerPackets(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONGLONG OwnerCookie,
    _In_ NTSTATUS CancelStatus)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    PDXGMMS2_SCHED_PACKET Cancelled[DXGMMS2_SCHEDULER_MAX_RETIREMENTS];
    KIRQL OldIrql;
    ULONG Count;
    ULONG Index;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    do
    {
        KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
        Count = Dxgmms2SchedCoreCancelOwner(&Context->SchedulerCore, OwnerCookie, Cancelled, RTL_NUMBER_OF(Cancelled));
        for (Index = 0; Index < Count; ++Index)
            Dxgmms2SchedulerQueueRetirementLocked(Context, Cancelled[Index], Dxgmms2RetireCancelled, CancelStatus);
        KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    } while (Count == RTL_NUMBER_OF(Cancelled));
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerAbortAllPackets(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG Flags,
    _In_ NTSTATUS AbortStatus)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    PDXGMMS2_SCHED_PACKET Aborted[DXGMMS2_SCHEDULER_MAX_RETIREMENTS];
    KIRQL OldIrql;
    ULONG Count;
    ULONG Index;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    do
    {
        KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
        Count = Dxgmms2SchedCoreAbortAll(&Context->SchedulerCore, (Flags & DXGMMS2_SCHEDULER_ABORT_INCLUDE_DISPATCHED) != 0, Aborted, RTL_NUMBER_OF(Aborted));
        for (Index = 0; Index < Count; ++Index)
            Dxgmms2SchedulerQueueRetirementLocked(Context, Aborted[Index], Dxgmms2RetireAborted, AbortStatus);
        KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    } while (Count == RTL_NUMBER_OF(Aborted));
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerSetAdmission(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ BOOLEAN Open)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Dxgmms2SchedCoreSetAdmission(&Context->SchedulerCore, Open);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return STATUS_SUCCESS;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerQueryEngineStatus(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineOrdinal,
    _Inout_ DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 *Status)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;
    NTSTATUS QueryStatus;

    if (Context == NULL || Status == NULL)
        return STATUS_INVALID_HANDLE;
    if (Status->Size < DXGMMS2_SCHEDULER_ENGINE_STATUS_V1_SIZE || Status->Version != DXGMMS2_SCHEDULER_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    QueryStatus = Dxgmms2SchedCoreQueryEngine(&Context->SchedulerCore, EngineOrdinal, Status);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return QueryStatus;
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerSetEngineState(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineOrdinal,
    _In_ ULONG ExpectedState,
    _In_ ULONG NewState,
    _Out_opt_ PULONG OutPreviousState)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;
    NTSTATUS Status;

    if (OutPreviousState != NULL)
        *OutPreviousState = Dxgmms2EngineError;
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Status = Dxgmms2SchedCoreSetEngineState(&Context->SchedulerCore, EngineOrdinal, ExpectedState, NewState, OutPreviousState);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Status;
}

static BOOLEAN
NTAPI
Dxgmms2SchedulerIsIdle(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;
    BOOLEAN Idle;

    if (Context == NULL)
        return TRUE;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Idle = Dxgmms2SchedCoreIsIdle(&Context->SchedulerCore) && IsListEmpty(&Context->SchedulerRetirementList);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Idle;
}


static NTSTATUS
NTAPI
Dxgmms2SchedulerReserveSlot(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineOrdinal)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Status = Dxgmms2SchedCoreReserve(&Context->SchedulerCore, EngineOrdinal);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Status;
}

static VOID
NTAPI
Dxgmms2SchedulerReleaseSlot(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineOrdinal)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;

    if (Context == NULL)
        return;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Dxgmms2SchedCoreUnreserve(&Context->SchedulerCore, EngineOrdinal);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
}

static NTSTATUS
NTAPI
Dxgmms2SchedulerResetDispatched(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineOrdinal,
    _Out_writes_to_(Capacity, *Count) ULONGLONG *PacketCookies,
    _In_ ULONG Capacity,
    _Out_ PULONG Count)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    PDXGMMS2_SCHED_PACKET Packets[DXGMMS2_SCHEDULER_MAX_RETIREMENTS];
    KIRQL OldIrql;
    ULONG Found;
    ULONG Index;

    if (Count == NULL)
        return STATUS_INVALID_PARAMETER;
    *Count = 0;
    if (Context == NULL || (Capacity != 0 && PacketCookies == NULL))
        return STATUS_INVALID_HANDLE;

    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Found = Dxgmms2SchedCoreResetDispatched(&Context->SchedulerCore, EngineOrdinal, Packets,
                                            min(Capacity, (ULONG)RTL_NUMBER_OF(Packets)));
    for (Index = 0; Index < Found; ++Index)
        PacketCookies[Index] = Packets[Index]->PacketCookie;
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    *Count = Found;
    return STATUS_SUCCESS;
}

static BOOLEAN
NTAPI
Dxgmms2SchedulerGetOldestDispatched(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _Out_ PULONG EngineOrdinal,
    _Out_ PULONG FenceId,
    _Out_ PULONGLONG PacketCookie)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;
    BOOLEAN Found;

    if (EngineOrdinal == NULL || FenceId == NULL || PacketCookie == NULL)
        return FALSE;
    *EngineOrdinal = 0;
    *FenceId = 0;
    *PacketCookie = 0;
    if (Context == NULL)
        return FALSE;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Found = Dxgmms2SchedCoreGetOldestDispatched(&Context->SchedulerCore, EngineOrdinal, FenceId, PacketCookie);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Found;
}

static BOOLEAN
NTAPI
Dxgmms2SchedulerPeekNextPacket(
    _In_ DXGMMS2_SCHEDULER_HANDLE Scheduler,
    _In_ ULONG EngineOrdinal,
    _Out_ PULONGLONG OutPacketCookie)
{
    PDXGMMS2_ADAPTER_CONTEXT Context = Dxgmms2SchedulerContext(Scheduler);
    KIRQL OldIrql;
    BOOLEAN Found;

    if (OutPacketCookie == NULL)
        return FALSE;
    *OutPacketCookie = 0;
    if (Context == NULL)
        return FALSE;
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Found = Dxgmms2SchedCorePeekNext(&Context->SchedulerCore, EngineOrdinal, OutPacketCookie);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Found;
}

NTSTATUS
Dxgmms2SchedulerStartAdapter(
    _Inout_ PDXGMMS2_ADAPTER_CONTEXT Context,
    _In_ ULONG NodeCount)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Status = Dxgmms2SchedCoreStart(&Context->SchedulerCore, NodeCount != 0 ? NodeCount : 1);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Status;
}

VOID
Dxgmms2SchedulerStopAdapter(
    _Inout_ PDXGMMS2_ADAPTER_CONTEXT Context)
{
    /*
     * Closing admission is all a begin-stop may do.  Pulling a packet the
     * miniport already owns would invalidate a dispatch claim that is still
     * in flight, so dispatched work is left for dxgkrnl to abort once it has
     * established the stop/reset ownership boundary.
     */
    (VOID)Dxgmms2SchedulerSetAdmission((DXGMMS2_SCHEDULER_HANDLE)Context, FALSE);
    (VOID)Dxgmms2SchedulerAbortAllPackets((DXGMMS2_SCHEDULER_HANDLE)Context, 0, STATUS_DEVICE_REMOVED);
}

/*
 * Dxgmms2SchedulerResetAdapter — return the queue core to its pre-start state
 * so the same adapter can be started again after a PnP stop.
 */
NTSTATUS
Dxgmms2SchedulerResetAdapter(
    _Inout_ PDXGMMS2_ADAPTER_CONTEXT Context)
{
    KIRQL OldIrql;
    NTSTATUS Status;

    (VOID)Dxgmms2SchedulerAbortAllPackets((DXGMMS2_SCHEDULER_HANDLE)Context, DXGMMS2_SCHEDULER_ABORT_INCLUDE_DISPATCHED, STATUS_DEVICE_REMOVED);
    KeAcquireSpinLock(&Context->SchedulerLock, &OldIrql);
    Status = Dxgmms2SchedCoreStop(&Context->SchedulerCore);
    KeReleaseSpinLock(&Context->SchedulerLock, OldIrql);
    return Status;
}

VOID
Dxgmms2SchedulerInitializeContext(
    _Inout_ PDXGMMS2_ADAPTER_CONTEXT Context)
{
    ULONG Index;

    KeInitializeSpinLock(&Context->SchedulerLock);
    Dxgmms2SchedCoreInitialize(&Context->SchedulerCore);
    InitializeListHead(&Context->SchedulerPacketFreeList);
    InitializeListHead(&Context->SchedulerRetirementList);
    InitializeListHead(&Context->SchedulerRetirementFreeList);
    for (Index = 0; Index < DXGMMS2_SCHED_MAX_PACKETS; ++Index)
        InsertTailList(&Context->SchedulerPacketFreeList, &Context->SchedulerPackets[Index].Entry);
    for (Index = 0; Index < DXGMMS2_SCHED_MAX_PACKETS; ++Index)
        InsertTailList(&Context->SchedulerRetirementFreeList, &Context->SchedulerRetirements[Index].Entry);
}

NTSTATUS
NTAPI
Dxgmms2QuerySchedulerInterface(
    _In_ DXGMMS2_ADAPTER_HANDLE Adapter,
    _Inout_ DXGMMS2_SCHEDULER_INTERFACE_V1 *SchedulerInterface)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    ULONG Capacity;

    PAGED_CODE();
    if (SchedulerInterface == NULL)
        return STATUS_INVALID_PARAMETER;
    Capacity = SchedulerInterface->Size;
    if (Capacity < (FIELD_OFFSET(DXGMMS2_SCHEDULER_INTERFACE_V1, Version) + sizeof(SchedulerInterface->Version)))
    {
        if (Capacity >= sizeof(SchedulerInterface->Size))
            SchedulerInterface->Size = DXGMMS2_SCHEDULER_INTERFACE_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (SchedulerInterface->Version != DXGMMS2_SCHEDULER_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Capacity < DXGMMS2_SCHEDULER_INTERFACE_V1_SIZE)
    {
        SchedulerInterface->Size = DXGMMS2_SCHEDULER_INTERFACE_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;

    RtlZeroMemory(SchedulerInterface, DXGMMS2_SCHEDULER_INTERFACE_V1_SIZE);
    SchedulerInterface->Size = DXGMMS2_SCHEDULER_INTERFACE_V1_SIZE;
    SchedulerInterface->Version = DXGMMS2_SCHEDULER_VERSION_1;
    SchedulerInterface->Generation = Context->Generation;
    SchedulerInterface->SchedulerHandle = (DXGMMS2_SCHEDULER_HANDLE)Context;
    SchedulerInterface->MaximumEngines = DXGMMS2_SCHEDULER_MAX_ENGINES;
    SchedulerInterface->Start = Dxgmms2SchedulerStart;
    SchedulerInterface->AdmitPacket = Dxgmms2SchedulerAdmitPacket;
    SchedulerInterface->ClaimNextPacket = Dxgmms2SchedulerClaimNextPacket;
    SchedulerInterface->PublishDispatch = Dxgmms2SchedulerPublishDispatch;
    SchedulerInterface->CompleteDispatch = Dxgmms2SchedulerCompleteDispatch;
    SchedulerInterface->NotifyCompletion = Dxgmms2SchedulerNotifyCompletion;
    SchedulerInterface->DrainRetirements = Dxgmms2SchedulerDrainRetirements;
    SchedulerInterface->CancelOwnerPackets = Dxgmms2SchedulerCancelOwnerPackets;
    SchedulerInterface->AbortAllPackets = Dxgmms2SchedulerAbortAllPackets;
    SchedulerInterface->SetAdmission = Dxgmms2SchedulerSetAdmission;
    SchedulerInterface->QueryEngineStatus = Dxgmms2SchedulerQueryEngineStatus;
    SchedulerInterface->SetEngineState = Dxgmms2SchedulerSetEngineState;
    SchedulerInterface->IsIdle = Dxgmms2SchedulerIsIdle;
    SchedulerInterface->ReserveSlot = Dxgmms2SchedulerReserveSlot;
    SchedulerInterface->ReleaseSlot = Dxgmms2SchedulerReleaseSlot;
    SchedulerInterface->ResetDispatched = Dxgmms2SchedulerResetDispatched;
    SchedulerInterface->GetOldestDispatched = Dxgmms2SchedulerGetOldestDispatched;
    SchedulerInterface->PeekNextPacket = Dxgmms2SchedulerPeekNextPacket;

    Dxgmms2DereferenceAdapterContext(Context);
    return STATUS_SUCCESS;
}

/* EOF */
