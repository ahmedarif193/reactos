/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Scheduler queue ownership core
 *
 * The ordering rules this core enforces are the ones the WDDM scheduler
 * contract depends on:
 *
 *   - Fence order equals queue order equals dispatch order on an engine.  A
 *     packet is appended behind every packet that already carries a fence, so
 *     retirement by fence watermark can never cross un-executed work.
 *   - A packet that was never dispatched is never retired as completed; only
 *     cancellation or abort may terminate it.
 *   - Every admitted packet reaches exactly one terminal edge, and produces
 *     exactly one retirement record.
 *   - A claim is issued to one caller at a time and is committed exactly once.
 */

#include "scheduler_core.h"

static VOID
Dxgmms2SchedCoreInitializeEngine(
    _Out_ PDXGMMS2_SCHED_ENGINE Engine)
{
    RtlZeroMemory(Engine, sizeof(*Engine));
    InitializeListHead(&Engine->RunQueue);
    Engine->State = Dxgmms2EngineIdle;
    Engine->NextClaimToken = 1;
}

static PDXGMMS2_SCHED_ENGINE
Dxgmms2SchedCoreEngine(
    _In_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal)
{
    if (!Core->Started || EngineOrdinal >= Core->EngineCount)
        return NULL;
    return &Core->Engines[EngineOrdinal];
}

static ULONG
Dxgmms2SchedCoreCountReservations(
    _In_ PDXGMMS2_SCHED_CORE Core)
{
    ULONG Count = 0;
    ULONG Index;

    for (Index = 0; Index < Core->EngineCount; ++Index)
        Count += Core->Engines[Index].ReservedPacketCount;
    return Count;
}

VOID
Dxgmms2SchedCoreInitialize(
    _Out_ PDXGMMS2_SCHED_CORE Core)
{
    ULONG Index;

    RtlZeroMemory(Core, sizeof(*Core));
    for (Index = 0; Index < DXGMMS2_SCHEDULER_MAX_ENGINES; ++Index)
        Dxgmms2SchedCoreInitializeEngine(&Core->Engines[Index]);
    InitializeListHead(&Core->RetirementList);
    InitializeListHead(&Core->FreeList);
    Core->NextFenceId = 0;
    Core->AdmissionOpen = FALSE;
    Core->Started = FALSE;
}

NTSTATUS
Dxgmms2SchedCoreStart(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineCount)
{
    if (EngineCount == 0 || EngineCount > DXGMMS2_SCHEDULER_MAX_ENGINES)
        return STATUS_INVALID_PARAMETER;
    if (Core->Started)
        return STATUS_INVALID_DEVICE_STATE;
    Core->NextFenceId = 0;
    Core->EngineCount = EngineCount;
    Core->Started = TRUE;
    Core->AdmissionOpen = TRUE;
    return STATUS_SUCCESS;
}

VOID
Dxgmms2SchedCoreSetAdmission(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ BOOLEAN Open)
{
    Core->AdmissionOpen = Open;
}

/*
 * Dxgmms2SchedCoreAdmit
 *
 * Appends the packet at the tail of its engine's run queue and assigns the
 * fence that orders it.  Appending unconditionally is what makes fence order
 * equal queue order: a later admission can never be given an earlier fence or
 * overtake work already queued.
 */
NTSTATUS
Dxgmms2SchedCoreAdmit(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ const DXGMMS2_SCHEDULER_ADMIT_INFO_V1 *Info,
    _Inout_ PDXGMMS2_SCHED_PACKET Packet,
    _Out_ PULONG OutFenceId)
{
    PDXGMMS2_SCHED_ENGINE Engine;
    ULONG FenceId;

    *OutFenceId = 0;
    if (!Core->AdmissionOpen)
        return STATUS_DEVICE_NOT_READY;
    Engine = Dxgmms2SchedCoreEngine(Core, Info->EngineOrdinal);
    if (Engine == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Core->TotalPackets >= DXGMMS2_SCHED_MAX_PACKETS)
        return STATUS_DEVICE_BUSY;
    if ((Info->Flags & DXGMMS2_SCHEDULER_ADMIT_CONSUME_RESERVATION) != 0)
    {
        if (Engine->ReservedPacketCount == 0)
            return STATUS_INVALID_DEVICE_STATE;
    }
    else if (Core->TotalPackets +
                 Dxgmms2SchedCoreCountReservations(Core) >=
                 DXGMMS2_SCHED_MAX_PACKETS ||
             Engine->PendingPacketCount +
                 Engine->ReservedPacketCount >=
                 DXGMMS2_SCHED_MAX_PACKETS)
    {
        return STATUS_DEVICE_BUSY;
    }
    if (Engine->State == Dxgmms2EngineError || Engine->State == Dxgmms2EngineResetting)
        return STATUS_DEVICE_NOT_READY;

    if ((Info->Flags & DXGMMS2_SCHEDULER_ADMIT_PREFENCED) != 0)
    {
        if (Info->SubmissionFenceId == 0)
            return STATUS_INVALID_PARAMETER;
        FenceId = Info->SubmissionFenceId;
    }
    else
    {
        do
        {
            FenceId = (ULONG)InterlockedIncrement(&Core->NextFenceId);
        } while (FenceId == 0);
    }

    /* A caller-supplied fence must not reorder the queue. */
    if (Engine->PendingPacketCount != 0 && (LONG)(FenceId - Engine->LastSubmittedFenceId) <= 0 &&
        Engine->LastSubmittedFenceId != 0)
        return STATUS_INVALID_PARAMETER;

    Packet->PacketCookie = Info->PacketCookie;
    Packet->OwnerCookie = Info->OwnerCookie;
    Packet->SubmissionFenceId = FenceId;
    Packet->Flags = Info->Flags;
    Packet->Priority = Info->Priority;
    Packet->Dispatched = FALSE;
    Packet->Claimed = FALSE;
    Packet->ClaimToken = 0;

    InsertTailList(&Engine->RunQueue, &Packet->Entry);
    Engine->PendingPacketCount++;
    if ((Info->Flags & DXGMMS2_SCHEDULER_ADMIT_CONSUME_RESERVATION) != 0)
        Engine->ReservedPacketCount--;
    Engine->LastSubmittedFenceId = FenceId;
    Core->TotalPackets++;
    *OutFenceId = FenceId;
    return STATUS_SUCCESS;
}

/*
 * Dxgmms2SchedCoreClaim
 *
 * Hands the head packet to one dispatcher.  Only the head may be claimed, and
 * only while no other claim is outstanding on that engine, so dispatch order
 * cannot diverge from queue order.
 */
BOOLEAN
Dxgmms2SchedCoreClaim(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal,
    _Out_ DXGMMS2_SCHEDULER_CLAIM_V1 *Claim)
{
    PDXGMMS2_SCHED_ENGINE Engine = Dxgmms2SchedCoreEngine(Core, EngineOrdinal);
    PDXGMMS2_SCHED_PACKET Packet;

    if (Engine == NULL || IsListEmpty(&Engine->RunQueue))
        return FALSE;
    if (Engine->State != Dxgmms2EngineIdle && Engine->State != Dxgmms2EngineRunning)
        return FALSE;

    Packet = CONTAINING_RECORD(Engine->RunQueue.Flink, DXGMMS2_SCHED_PACKET, Entry);
    if (Packet->Claimed || Packet->Dispatched)
        return FALSE;

    Packet->Claimed = TRUE;
    Packet->ClaimToken = Engine->NextClaimToken++;
    if (Engine->NextClaimToken == 0)
        Engine->NextClaimToken = 1;
    Engine->State = Dxgmms2EngineSubmitting;

    Claim->NodeOrdinal = EngineOrdinal;
    Claim->EngineOrdinal = EngineOrdinal;
    Claim->PacketCookie = Packet->PacketCookie;
    Claim->ClaimToken = Packet->ClaimToken;
    Claim->SubmissionFenceId = Packet->SubmissionFenceId;
    Claim->Flags = Packet->Flags;
    return TRUE;
}

static PDXGMMS2_SCHED_PACKET
Dxgmms2SchedCoreFindClaim(
    _In_ PDXGMMS2_SCHED_ENGINE Engine,
    _In_ ULONGLONG ClaimToken)
{
    PLIST_ENTRY Entry;

    if (ClaimToken == 0)
        return NULL;
    for (Entry = Engine->RunQueue.Flink; Entry != &Engine->RunQueue; Entry = Entry->Flink)
    {
        PDXGMMS2_SCHED_PACKET Packet = CONTAINING_RECORD(Entry, DXGMMS2_SCHED_PACKET, Entry);

        if (Packet->Claimed && Packet->ClaimToken == ClaimToken)
            return Packet;
    }
    return NULL;
}

/*
 * Dxgmms2SchedCorePublishDispatch
 *
 * Marks the claimed packet as dispatched.  The caller invokes this
 * immediately before the miniport submit, so the fence becomes visible as
 * submitted in the same order the hardware receives the work.
 */
NTSTATUS
Dxgmms2SchedCorePublishDispatch(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal,
    _In_ ULONGLONG ClaimToken)
{
    PDXGMMS2_SCHED_ENGINE Engine = Dxgmms2SchedCoreEngine(Core, EngineOrdinal);
    PDXGMMS2_SCHED_PACKET Packet;

    if (Engine == NULL)
        return STATUS_INVALID_PARAMETER;
    Packet = Dxgmms2SchedCoreFindClaim(Engine, ClaimToken);
    if (Packet == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Packet->Dispatched)
        return STATUS_INVALID_DEVICE_STATE;
    Packet->Dispatched = TRUE;
    return STATUS_SUCCESS;
}

/*
 * Dxgmms2SchedCoreCompleteDispatch
 *
 * Commits the outcome of the miniport submit exactly once.  A failed dispatch
 * removes the packet immediately and hands it back for terminalization; a
 * successful one leaves it queued until its fence retires.
 */
NTSTATUS
Dxgmms2SchedCoreCompleteDispatch(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal,
    _In_ ULONGLONG ClaimToken,
    _In_ NTSTATUS DispatchStatus,
    _Outptr_result_maybenull_ PDXGMMS2_SCHED_PACKET *OutFailed)
{
    PDXGMMS2_SCHED_ENGINE Engine = Dxgmms2SchedCoreEngine(Core, EngineOrdinal);
    PDXGMMS2_SCHED_PACKET Packet;

    *OutFailed = NULL;
    if (Engine == NULL)
        return STATUS_INVALID_PARAMETER;
    Packet = Dxgmms2SchedCoreFindClaim(Engine, ClaimToken);
    if (Packet == NULL)
        return STATUS_INVALID_PARAMETER;

    Packet->Claimed = FALSE;
    Packet->ClaimToken = 0;
    if (!NT_SUCCESS(DispatchStatus))
    {
        RemoveEntryList(&Packet->Entry);
        InitializeListHead(&Packet->Entry);
        Engine->PendingPacketCount--;
        Core->TotalPackets--;
        Packet->Dispatched = FALSE;
        *OutFailed = Packet;
        /* Only the dispatch's own state may be retired here.  Reset, preempt
         * and suspend take the engine away from the submitter, and committing
         * a claim must not put it back. */
        if (Engine->State == Dxgmms2EngineSubmitting)
            Engine->State = IsListEmpty(&Engine->RunQueue) ? Dxgmms2EngineIdle : Dxgmms2EngineRunning;
        return STATUS_SUCCESS;
    }
    if (Engine->State == Dxgmms2EngineSubmitting)
        Engine->State = Dxgmms2EngineRunning;
    return STATUS_SUCCESS;
}

/*
 * Dxgmms2SchedCoreNotifyCompletion
 *
 * Retires every dispatched packet at or below the reported fence watermark.
 * The walk stops at the first packet that was never dispatched, so a hardware
 * watermark can never retire work the miniport has not seen.
 */
ULONG
Dxgmms2SchedCoreNotifyCompletion(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal,
    _In_ ULONG CompletedFenceId,
    _Out_writes_to_(Capacity, return) PDXGMMS2_SCHED_PACKET *Retired,
    _In_ ULONG Capacity)
{
    PDXGMMS2_SCHED_ENGINE Engine = Dxgmms2SchedCoreEngine(Core, EngineOrdinal);
    ULONG Count = 0;

    if (Engine == NULL)
        return 0;
    /*
     * A zero watermark means "re-evaluate against what was already reported".
     * The hardware can complete a packet while its dispatch claim is still
     * outstanding, in which case that notification could not retire it; the
     * claim's committer replays the sweep so the packet is not stranded.
     */
    if (CompletedFenceId == 0)
        CompletedFenceId = Engine->LastCompletedFenceId;
    else if ((LONG)(CompletedFenceId - Engine->LastCompletedFenceId) > 0)
        Engine->LastCompletedFenceId = CompletedFenceId;
    if (CompletedFenceId == 0)
        return 0;

    while (Count < Capacity && !IsListEmpty(&Engine->RunQueue))
    {
        PDXGMMS2_SCHED_PACKET Packet = CONTAINING_RECORD(Engine->RunQueue.Flink, DXGMMS2_SCHED_PACKET, Entry);

        if (!Packet->Dispatched || Packet->Claimed)
            break;
        if ((LONG)(CompletedFenceId - Packet->SubmissionFenceId) < 0)
            break;
        RemoveEntryList(&Packet->Entry);
        InitializeListHead(&Packet->Entry);
        Engine->PendingPacketCount--;
        Core->TotalPackets--;
        Retired[Count++] = Packet;
    }
    if (IsListEmpty(&Engine->RunQueue) && Engine->State == Dxgmms2EngineRunning)
        Engine->State = Dxgmms2EngineIdle;
    return Count;
}

static ULONG
Dxgmms2SchedCoreRemoveMatching(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONGLONG OwnerCookie,
    _In_ BOOLEAN MatchAll,
    _In_ BOOLEAN IncludeDispatched,
    _Out_writes_to_(Capacity, return) PDXGMMS2_SCHED_PACKET *Removed,
    _In_ ULONG Capacity)
{
    ULONG Count = 0;
    ULONG Index;

    for (Index = 0; Index < Core->EngineCount && Count < Capacity; ++Index)
    {
        PDXGMMS2_SCHED_ENGINE Engine = &Core->Engines[Index];
        PLIST_ENTRY Entry = Engine->RunQueue.Flink;

        while (Entry != &Engine->RunQueue && Count < Capacity)
        {
            PDXGMMS2_SCHED_PACKET Packet = CONTAINING_RECORD(Entry, DXGMMS2_SCHED_PACKET, Entry);
            PLIST_ENTRY Next = Entry->Flink;

            /* A packet the miniport already owns cannot be pulled back here;
             * it terminalizes through completion or reset instead. */
            if ((MatchAll || Packet->OwnerCookie == OwnerCookie) && (IncludeDispatched || (!Packet->Dispatched && !Packet->Claimed)))
            {
                RemoveEntryList(&Packet->Entry);
                InitializeListHead(&Packet->Entry);
                Engine->PendingPacketCount--;
                Core->TotalPackets--;
                Removed[Count++] = Packet;
            }
            Entry = Next;
        }
        if (IsListEmpty(&Engine->RunQueue) && Engine->State == Dxgmms2EngineRunning)
            Engine->State = Dxgmms2EngineIdle;
    }
    return Count;
}

ULONG
Dxgmms2SchedCoreCancelOwner(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONGLONG OwnerCookie,
    _Out_writes_to_(Capacity, return) PDXGMMS2_SCHED_PACKET *Cancelled,
    _In_ ULONG Capacity)
{
    return Dxgmms2SchedCoreRemoveMatching(Core, OwnerCookie, FALSE, FALSE, Cancelled, Capacity);
}

ULONG
Dxgmms2SchedCoreAbortAll(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ BOOLEAN IncludeDispatched,
    _Out_writes_to_(Capacity, return) PDXGMMS2_SCHED_PACKET *Aborted,
    _In_ ULONG Capacity)
{
    return Dxgmms2SchedCoreRemoveMatching(Core, 0, TRUE, IncludeDispatched, Aborted, Capacity);
}

BOOLEAN
Dxgmms2SchedCoreIsIdle(
    _In_ PDXGMMS2_SCHED_CORE Core)
{
    return Core->TotalPackets == 0;
}

NTSTATUS
Dxgmms2SchedCoreQueryEngine(
    _In_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal,
    _Inout_ DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 *Status)
{
    PDXGMMS2_SCHED_ENGINE Engine = Dxgmms2SchedCoreEngine((PDXGMMS2_SCHED_CORE)Core, EngineOrdinal);

    if (Engine == NULL)
        return STATUS_INVALID_PARAMETER;
    Status->State = Engine->State;
    Status->PendingPacketCount = Engine->PendingPacketCount;
    Status->LastSubmittedFenceId = Engine->LastSubmittedFenceId;
    Status->LastCompletedFenceId = Engine->LastCompletedFenceId;
    Status->OldestKickedFenceId = 0;
    if (!IsListEmpty(&Engine->RunQueue))
    {
        PDXGMMS2_SCHED_PACKET Packet = CONTAINING_RECORD(Engine->RunQueue.Flink, DXGMMS2_SCHED_PACKET, Entry);

        if (Packet->Dispatched)
            Status->OldestKickedFenceId = Packet->SubmissionFenceId;
    }
    return STATUS_SUCCESS;
}

/*
 * Dxgmms2SchedCoreReserve / Unreserve
 *
 * A caller that must build a packet before it can admit it reserves its queue
 * slot first, so the depth bound is enforced against work in preparation as
 * well as work already queued and a build cannot fail admission afterwards.
 */
NTSTATUS
Dxgmms2SchedCoreReserve(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal)
{
    PDXGMMS2_SCHED_ENGINE Engine = Dxgmms2SchedCoreEngine(Core, EngineOrdinal);

    if (Engine == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!Core->AdmissionOpen)
        return STATUS_DEVICE_NOT_READY;
    /*
     * A reservation protects one slot in the adapter-wide packet pool, not
     * merely this engine's queue.  Without the global sum, another engine can
     * fill the pool after this call succeeds and make the promised consuming
     * admission fail.
     */
    if (Core->TotalPackets +
            Dxgmms2SchedCoreCountReservations(Core) >=
            DXGMMS2_SCHED_MAX_PACKETS ||
        Engine->PendingPacketCount +
            Engine->ReservedPacketCount >=
            DXGMMS2_SCHED_MAX_PACKETS)
        return STATUS_DEVICE_BUSY;
    Engine->ReservedPacketCount++;
    return STATUS_SUCCESS;
}

VOID
Dxgmms2SchedCoreUnreserve(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal)
{
    PDXGMMS2_SCHED_ENGINE Engine = Dxgmms2SchedCoreEngine(Core, EngineOrdinal);

    if (Engine != NULL && Engine->ReservedPacketCount != 0)
        Engine->ReservedPacketCount--;
}

/*
 * Dxgmms2SchedCoreResetDispatched
 *
 * Preemption interrupts every packet the miniport had accepted but not
 * completed.  Those packets stay queued in the same order with the same fence
 * identity -- changing it would orphan the reservation -- and are marked
 * undispatched so the next kick resubmits them.
 */
ULONG
Dxgmms2SchedCoreResetDispatched(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal,
    _Out_writes_to_(Capacity, return) PDXGMMS2_SCHED_PACKET *Packets,
    _In_ ULONG Capacity)
{
    PDXGMMS2_SCHED_ENGINE Engine = Dxgmms2SchedCoreEngine(Core, EngineOrdinal);
    PLIST_ENTRY Entry;
    ULONG Count = 0;

    if (Engine == NULL)
        return 0;
    for (Entry = Engine->RunQueue.Flink; Entry != &Engine->RunQueue && Count < Capacity; Entry = Entry->Flink)
    {
        PDXGMMS2_SCHED_PACKET Packet = CONTAINING_RECORD(Entry, DXGMMS2_SCHED_PACKET, Entry);

        if (!Packet->Dispatched || Packet->Claimed)
            continue;
        Packet->Dispatched = FALSE;
        Packets[Count++] = Packet;
    }
    return Count;
}

BOOLEAN
Dxgmms2SchedCorePeekNext(
    _In_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal,
    _Out_ PULONGLONG OutPacketCookie)
{
    PDXGMMS2_SCHED_ENGINE Engine = Dxgmms2SchedCoreEngine((PDXGMMS2_SCHED_CORE)Core, EngineOrdinal);
    PDXGMMS2_SCHED_PACKET Packet;

    *OutPacketCookie = 0;
    if (Engine == NULL || IsListEmpty(&Engine->RunQueue))
        return FALSE;
    if (Engine->State != Dxgmms2EngineIdle && Engine->State != Dxgmms2EngineRunning)
        return FALSE;
    Packet = CONTAINING_RECORD(Engine->RunQueue.Flink, DXGMMS2_SCHED_PACKET, Entry);
    if (Packet->Claimed || Packet->Dispatched)
        return FALSE;
    *OutPacketCookie = Packet->PacketCookie;
    return TRUE;
}

BOOLEAN
Dxgmms2SchedCoreGetOldestDispatched(
    _In_ PDXGMMS2_SCHED_CORE Core,
    _Out_ PULONG EngineOrdinal,
    _Out_ PULONG FenceId,
    _Out_ PULONGLONG PacketCookie)
{
    ULONG Index;

    *EngineOrdinal = 0;
    *FenceId = 0;
    *PacketCookie = 0;
    for (Index = 0; Index < Core->EngineCount; ++Index)
    {
        const DXGMMS2_SCHED_ENGINE *Engine = &Core->Engines[Index];

        if (IsListEmpty(&Engine->RunQueue))
            continue;
        {
            PDXGMMS2_SCHED_PACKET Packet = CONTAINING_RECORD(Engine->RunQueue.Flink, DXGMMS2_SCHED_PACKET, Entry);

            if (!Packet->Dispatched)
                continue;
            if (*FenceId == 0 || (LONG)(Packet->SubmissionFenceId - *FenceId) < 0)
            {
                *EngineOrdinal = Index;
                *FenceId = Packet->SubmissionFenceId;
                *PacketCookie = Packet->PacketCookie;
            }
        }
    }
    return *FenceId != 0;
}

/*
 * Dxgmms2SchedCoreIsValidTransition
 *
 * The engine state machine.  Every move an engine may make lives here, so a
 * caller cannot drive an engine into a state the scheduler does not model.
 */
BOOLEAN
Dxgmms2SchedCoreIsValidTransition(
    _In_ ULONG OldState,
    _In_ ULONG NewState)
{
    if (OldState >= Dxgmms2EngineStateCount || NewState >= Dxgmms2EngineStateCount)
        return FALSE;
    if (OldState == NewState)
        return TRUE;
    if (NewState == Dxgmms2EngineResetting)
        return (OldState != Dxgmms2EngineResetComplete);

    switch (OldState)
    {
        case Dxgmms2EngineIdle:
            return (NewState == Dxgmms2EngineBudgetComputed || NewState == Dxgmms2EngineSubmitting || NewState == Dxgmms2EngineSuspended || NewState == Dxgmms2EngineFlipPending || NewState == Dxgmms2EngineError);
        case Dxgmms2EngineBudgetComputed:
            return (NewState == Dxgmms2EngineIdle || NewState == Dxgmms2EngineSubmitting || NewState == Dxgmms2EngineSuspended || NewState == Dxgmms2EngineError);
        case Dxgmms2EngineSubmitting:
            return (NewState == Dxgmms2EngineRunning || NewState == Dxgmms2EngineIdle || NewState == Dxgmms2EngineError);
        case Dxgmms2EngineRunning:
            /* Submitting is reachable from Running: the scheduler pipelines,
             * claiming the next packet while the previous one is in flight. */
            return (NewState == Dxgmms2EngineCompleting || NewState == Dxgmms2EnginePreempting || NewState == Dxgmms2EngineSubmitting || NewState == Dxgmms2EngineIdle || NewState == Dxgmms2EngineError);
        case Dxgmms2EnginePreempting:
            return (NewState == Dxgmms2EnginePreempted || NewState == Dxgmms2EngineRunning || NewState == Dxgmms2EngineError);
        case Dxgmms2EnginePreempted:
            return (NewState == Dxgmms2EngineIdle || NewState == Dxgmms2EngineSubmitting || NewState == Dxgmms2EngineSuspended || NewState == Dxgmms2EngineError);
        case Dxgmms2EngineResetting:
            return (NewState == Dxgmms2EngineResetComplete || NewState == Dxgmms2EngineError);
        case Dxgmms2EngineResetComplete:
            return (NewState == Dxgmms2EngineIdle || NewState == Dxgmms2EngineSuspended || NewState == Dxgmms2EngineError);
        case Dxgmms2EngineSuspended:
            return (NewState == Dxgmms2EngineResuming || NewState == Dxgmms2EngineError);
        case Dxgmms2EngineResuming:
            return (NewState == Dxgmms2EngineIdle || NewState == Dxgmms2EngineSuspended || NewState == Dxgmms2EngineError);
        case Dxgmms2EngineFlipPending:
            return (NewState == Dxgmms2EngineFlipExecuting || NewState == Dxgmms2EngineIdle || NewState == Dxgmms2EngineError);
        case Dxgmms2EngineFlipExecuting:
            return (NewState == Dxgmms2EngineCompleting || NewState == Dxgmms2EngineIdle || NewState == Dxgmms2EngineError);
        case Dxgmms2EngineCompleting:
            return (NewState == Dxgmms2EngineIdle || NewState == Dxgmms2EngineRunning || NewState == Dxgmms2EngineError);
        default:
            return FALSE;
    }
}

NTSTATUS
Dxgmms2SchedCoreSetEngineState(
    _Inout_ PDXGMMS2_SCHED_CORE Core,
    _In_ ULONG EngineOrdinal,
    _In_ ULONG ExpectedState,
    _In_ ULONG NewState,
    _Out_opt_ PULONG OutPreviousState)
{
    PDXGMMS2_SCHED_ENGINE Engine = Dxgmms2SchedCoreEngine(Core, EngineOrdinal);

    if (OutPreviousState != NULL)
        *OutPreviousState = Dxgmms2EngineError;
    if (Engine == NULL || NewState >= Dxgmms2EngineStateCount)
        return STATUS_INVALID_PARAMETER;
    if (ExpectedState != DXGMMS2_ENGINE_STATE_ANY && ExpectedState >= Dxgmms2EngineStateCount)
        return STATUS_INVALID_PARAMETER;
    if (OutPreviousState != NULL)
        *OutPreviousState = Engine->State;
    if (ExpectedState != DXGMMS2_ENGINE_STATE_ANY && Engine->State != ExpectedState)
        return STATUS_INVALID_DEVICE_STATE;
    if (!Dxgmms2SchedCoreIsValidTransition(Engine->State, NewState))
        return STATUS_INVALID_DEVICE_STATE;
    Engine->State = NewState;
    return STATUS_SUCCESS;
}

/* EOF */

/*
 * Dxgmms2SchedCoreStop
 *
 * Returns the core to its pre-start state so the same adapter can be started
 * again after a PnP stop.  The caller must have drained every queue first.
 */
NTSTATUS
Dxgmms2SchedCoreStop(
    _Inout_ PDXGMMS2_SCHED_CORE Core)
{
    ULONG Index;

    if (!Core->Started)
        return STATUS_SUCCESS;
    if (Core->TotalPackets != 0)
        return STATUS_DEVICE_BUSY;
    for (Index = 0; Index < Core->EngineCount; ++Index)
    {
        PDXGMMS2_SCHED_ENGINE Engine = &Core->Engines[Index];

        if (!IsListEmpty(&Engine->RunQueue))
            return STATUS_DEVICE_BUSY;
        Dxgmms2SchedCoreInitializeEngine(Engine);
    }
    Core->EngineCount = 0;
    Core->AdmissionOpen = FALSE;
    Core->Started = FALSE;
    return STATUS_SUCCESS;
}

/* EOF */
