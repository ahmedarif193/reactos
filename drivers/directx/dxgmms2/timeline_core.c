/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Lock-free submission-fence timeline state machine
 */

#include "dxgmms2_private.h"

static BOOLEAN Dxgmms2TimelineFenceReached(_In_ ULONG CompletedFence, _In_ ULONG TargetFence)
{
    return (LONG)(CompletedFence - TargetFence) >= 0;
}

static VOID Dxgmms2TimelineUpdateFence(_Inout_ volatile ULONG *Fence, _In_ ULONG Value)
{
    LONG Current;

    for (;;)
    {
        Current = InterlockedCompareExchange((volatile LONG *)Fence, 0, 0);
        if (Dxgmms2TimelineFenceReached((ULONG)Current, Value) || InterlockedCompareExchange((volatile LONG *)Fence, (LONG)Value, Current) == Current)
            return;
    }
}

static BOOLEAN Dxgmms2TimelineAcquireFastCall(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _Out_ PLONG State)
{
    LONG LocalState;

    if (Timeline == NULL || Timeline->Signature != DXGMMS2_TIMELINE_SIGNATURE)
        return FALSE;
    InterlockedIncrement(&Timeline->ActiveFastCalls);
    KeMemoryBarrier();
    LocalState = InterlockedCompareExchange(&Timeline->State, 0, 0);
    if (InterlockedCompareExchange(&Timeline->FastCallsOpen, 0, 0) == 0 || (ULONG)InterlockedCompareExchange(&Timeline->Generation, 0, 0) != Generation || LocalState == Dxgmms2TimelineDestroying)
    {
        InterlockedDecrement(&Timeline->ActiveFastCalls);
        return FALSE;
    }
    *State = LocalState;
    return TRUE;
}

static VOID Dxgmms2TimelineReleaseFastCall(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline)
{
    ASSERT(InterlockedDecrement(&Timeline->ActiveFastCalls) >= 0);
}

static VOID Dxgmms2TimelineAcquireMaintenance(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline)
{
    PAGED_CODE();
    (VOID)KeWaitForSingleObject(&Timeline->MaintenanceMutex, Executive, KernelMode, FALSE, NULL);
}

static VOID Dxgmms2TimelineReleaseMaintenance(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline)
{
    PAGED_CODE();
    KeReleaseMutex(&Timeline->MaintenanceMutex, FALSE);
}

static VOID Dxgmms2TimelineCloseFastCalls(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline)
{
    PAGED_CODE();
    InterlockedExchange(&Timeline->FastCallsOpen, 0);
    KeMemoryBarrier();
}

static VOID Dxgmms2TimelineDrainFastCalls(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline)
{
    LARGE_INTEGER Delay;

    PAGED_CODE();
    Delay.QuadPart = -10000;
    while (InterlockedCompareExchange(&Timeline->ActiveFastCalls, 0, 0) != 0)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    KeMemoryBarrier();
}

static VOID Dxgmms2TimelineOpenFastCalls(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline)
{
    PAGED_CODE();
    KeMemoryBarrier();
    InterlockedExchange(&Timeline->FastCallsOpen, 1);
}

static ULONG Dxgmms2TimelineHashIdentity(_In_ LONG64 Identity)
{
    return (ULONG)(((ULONGLONG)Identity ^ ((ULONGLONG)Identity >> 32)) * 2654435761ULL) & (DXGMMS2_TIMELINE_IDENTITY_CAPACITY - 1);
}

static LONG64 Dxgmms2TimelineMakeIdentity(_In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    return (LONG64)(((ULONGLONG)(NodeOrdinal + 1) << 32) | FenceId);
}

static BOOLEAN Dxgmms2TimelineLookupPublished(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    LONG64 Identity = Dxgmms2TimelineMakeIdentity(NodeOrdinal, FenceId);
    LONG64 PublishedIdentity = (LONG64)((ULONGLONG)Identity | DXGMMS2_TIMELINE_PUBLISHED_BIT);
    ULONG StartSlot = Dxgmms2TimelineHashIdentity(Identity);
    ULONG Probe;

    for (Probe = 0; Probe < DXGMMS2_TIMELINE_IDENTITY_CAPACITY; ++Probe)
    {
        ULONG Slot = (StartSlot + Probe) & (DXGMMS2_TIMELINE_IDENTITY_CAPACITY - 1);
        LONG64 CurrentIdentity = InterlockedCompareExchange64(&Timeline->Identities[Slot], 0, 0);

        if (CurrentIdentity == PublishedIdentity)
            return TRUE;
        if (CurrentIdentity == 0)
            return FALSE;
    }
    return FALSE;
}

static NTSTATUS Dxgmms2TimelineValidateSnapshot(_Inout_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot)
{
    ULONG Capacity;

    if (Snapshot == NULL)
        return STATUS_INVALID_PARAMETER;
    Capacity = Snapshot->Size;
    if (Capacity < FIELD_OFFSET(DXGMMS2_FENCE_SNAPSHOT_V1, Version) + sizeof(Snapshot->Version))
    {
        if (Capacity >= sizeof(Snapshot->Size))
            Snapshot->Size = DXGMMS2_FENCE_SNAPSHOT_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (Snapshot->Version != DXGMMS2_SCHEDULER_TIMELINE_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Capacity < DXGMMS2_FENCE_SNAPSHOT_V1_SIZE)
    {
        Snapshot->Size = DXGMMS2_FENCE_SNAPSHOT_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    return STATUS_SUCCESS;
}

static VOID Dxgmms2TimelineFillSnapshot(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _Inout_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot)
{
    RtlZeroMemory(Snapshot, DXGMMS2_FENCE_SNAPSHOT_V1_SIZE);
    Snapshot->Size = DXGMMS2_FENCE_SNAPSHOT_V1_SIZE;
    Snapshot->Version = DXGMMS2_SCHEDULER_TIMELINE_VERSION_1;
    Snapshot->Generation = Generation;
    Snapshot->NodeOrdinal = NodeOrdinal;
    Snapshot->LastSubmittedFence = (ULONG)InterlockedCompareExchange((volatile LONG *)&Timeline->NodeLastSubmittedFenceId[NodeOrdinal], 0, 0);
    Snapshot->LastCompletedFence = (ULONG)InterlockedCompareExchange((volatile LONG *)&Timeline->NodeLastCompletedFenceId[NodeOrdinal], 0, 0);
    Snapshot->GlobalLastCompletedFence = (ULONG)InterlockedCompareExchange((volatile LONG *)&Timeline->LastCompletedFenceId, 0, 0);
}

VOID Dxgmms2TimelineInitialize(_Out_ PDXGMMS2_TIMELINE_CONTEXT Timeline)
{
    RtlZeroMemory(Timeline, sizeof(*Timeline));
    Timeline->Signature = DXGMMS2_TIMELINE_SIGNATURE;
    Timeline->State = Dxgmms2TimelineCreated;
    KeInitializeMutex(&Timeline->MaintenanceMutex, 0);
}

NTSTATUS Dxgmms2TimelineStart(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG NodeCount)
{
    ULONG Generation;
    ULONG Slot;
    LONG State;
    NTSTATUS Status;

    PAGED_CODE();
    if (Timeline == NULL || Timeline->Signature != DXGMMS2_TIMELINE_SIGNATURE || NodeCount > DXGMMS2_TIMELINE_MAX_NODES)
        return STATUS_INVALID_PARAMETER;
    Status = STATUS_SUCCESS;
    Dxgmms2TimelineAcquireMaintenance(Timeline);
    State = InterlockedCompareExchange(&Timeline->State, 0, 0);
    if (State != Dxgmms2TimelineCreated && State != Dxgmms2TimelineStopped)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    Dxgmms2TimelineCloseFastCalls(Timeline);
    Dxgmms2TimelineDrainFastCalls(Timeline);
    if (InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0) != 0)
    {
        if (State == Dxgmms2TimelineStopped)
            Dxgmms2TimelineOpenFastCalls(Timeline);
        Status = STATUS_DEVICE_BUSY;
        goto Exit;
    }
    for (Slot = 0; Slot < DXGMMS2_TIMELINE_IDENTITY_CAPACITY; ++Slot)
        InterlockedExchange64(&Timeline->Identities[Slot], 0);
    RtlZeroMemory((PVOID)Timeline->NodeLastSubmittedFenceId, sizeof(Timeline->NodeLastSubmittedFenceId));
    RtlZeroMemory((PVOID)Timeline->NodeLastCompletedFenceId, sizeof(Timeline->NodeLastCompletedFenceId));
    Timeline->LastCompletedFenceId = 0;
    Timeline->NodeCount = NodeCount;
    Generation = (ULONG)InterlockedIncrement(&Timeline->Generation);
    if (Generation == 0)
        Generation = (ULONG)InterlockedIncrement(&Timeline->Generation);
    InterlockedExchange(&Timeline->State, Dxgmms2TimelineActive);
    Dxgmms2TimelineOpenFastCalls(Timeline);

Exit:
    Dxgmms2TimelineReleaseMaintenance(Timeline);
    return Status;
}

NTSTATUS Dxgmms2TimelineBeginStop(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline)
{
    LONG PreviousState;
    NTSTATUS Status;

    PAGED_CODE();
    if (Timeline == NULL || Timeline->Signature != DXGMMS2_TIMELINE_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    Dxgmms2TimelineAcquireMaintenance(Timeline);
    PreviousState = InterlockedCompareExchange(&Timeline->State, Dxgmms2TimelineStopping, Dxgmms2TimelineActive);
    if (PreviousState == Dxgmms2TimelineActive || PreviousState == Dxgmms2TimelineStopping)
        Status = STATUS_SUCCESS;
    else
        Status = STATUS_INVALID_DEVICE_STATE;
    Dxgmms2TimelineReleaseMaintenance(Timeline);
    return Status;
}

NTSTATUS Dxgmms2TimelineCompleteStop(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline)
{
    LONG State;
    NTSTATUS Status;

    PAGED_CODE();
    if (Timeline == NULL || Timeline->Signature != DXGMMS2_TIMELINE_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    Dxgmms2TimelineAcquireMaintenance(Timeline);
    State = InterlockedCompareExchange(&Timeline->State, 0, 0);
    if (State == Dxgmms2TimelineStopping)
    {
        Dxgmms2TimelineCloseFastCalls(Timeline);
        Dxgmms2TimelineDrainFastCalls(Timeline);
        InterlockedExchange(&Timeline->State, Dxgmms2TimelineStopped);
        Dxgmms2TimelineOpenFastCalls(Timeline);
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = State == Dxgmms2TimelineStopped ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
    }
    Dxgmms2TimelineReleaseMaintenance(Timeline);
    return Status;
}

NTSTATUS Dxgmms2TimelinePrepareDestroy(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline)
{
    LONG State;
    NTSTATUS Status;

    PAGED_CODE();
    if (Timeline == NULL || Timeline->Signature != DXGMMS2_TIMELINE_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    Dxgmms2TimelineAcquireMaintenance(Timeline);
    State = InterlockedCompareExchange(&Timeline->State, 0, 0);
    if (State != Dxgmms2TimelineCreated && State != Dxgmms2TimelineStopped)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    Dxgmms2TimelineCloseFastCalls(Timeline);
    Dxgmms2TimelineDrainFastCalls(Timeline);
    if (InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0) != 0)
    {
        if (State == Dxgmms2TimelineStopped)
            Dxgmms2TimelineOpenFastCalls(Timeline);
        Status = STATUS_DEVICE_BUSY;
        goto Exit;
    }
    InterlockedExchange(&Timeline->State, Dxgmms2TimelineDestroying);
    Status = STATUS_SUCCESS;

Exit:
    Dxgmms2TimelineReleaseMaintenance(Timeline);
    return Status;
}

ULONG Dxgmms2TimelineAllocateFence(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation)
{
    ULONG CurrentFenceId;
    ULONG NextFenceId;
    LONG State;

    if (!Dxgmms2TimelineAcquireFastCall(Timeline, Generation, &State))
        return 0;
    if (State != Dxgmms2TimelineActive)
    {
        Dxgmms2TimelineReleaseFastCall(Timeline);
        return 0;
    }
    for (;;)
    {
        CurrentFenceId = (ULONG)InterlockedCompareExchange(&Timeline->NextFenceId, 0, 0);
        NextFenceId = CurrentFenceId + 1;
        if (NextFenceId == 0)
            NextFenceId = 1;
        if ((ULONG)InterlockedCompareExchange(&Timeline->NextFenceId, (LONG)NextFenceId, (LONG)CurrentFenceId) == CurrentFenceId)
            break;
    }
    Dxgmms2TimelineReleaseFastCall(Timeline);
    return NextFenceId;
}

BOOLEAN Dxgmms2TimelineReserveFence(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    LONG64 Identity;
    ULONG StartSlot;
    LONG State;
    BOOLEAN Reserved = FALSE;

    if (!Dxgmms2TimelineAcquireFastCall(Timeline, Generation, &State))
        return FALSE;
    if (State != Dxgmms2TimelineActive || NodeOrdinal >= Timeline->NodeCount || NodeOrdinal >= DXGMMS2_TIMELINE_MAX_NODES || FenceId == 0)
        goto Exit;
    Identity = Dxgmms2TimelineMakeIdentity(NodeOrdinal, FenceId);
    StartSlot = Dxgmms2TimelineHashIdentity(Identity);
    for (;;)
    {
        ULONG FirstTombstone = DXGMMS2_TIMELINE_IDENTITY_CAPACITY;
        ULONG Probe;
        ULONG Slot = 0;

        for (Probe = 0; Probe < DXGMMS2_TIMELINE_IDENTITY_CAPACITY; ++Probe)
        {
            LONG64 CurrentIdentity;

            Slot = (StartSlot + Probe) & (DXGMMS2_TIMELINE_IDENTITY_CAPACITY - 1);
            CurrentIdentity = InterlockedCompareExchange64(&Timeline->Identities[Slot], 0, 0);
            if (CurrentIdentity == Identity || CurrentIdentity == (LONG64)((ULONGLONG)Identity | DXGMMS2_TIMELINE_PUBLISHED_BIT))
                goto Exit;
            if (CurrentIdentity == DXGMMS2_TIMELINE_TOMBSTONE && FirstTombstone == DXGMMS2_TIMELINE_IDENTITY_CAPACITY)
                FirstTombstone = Slot;
            if (CurrentIdentity == 0)
                break;
        }
        if (Probe == DXGMMS2_TIMELINE_IDENTITY_CAPACITY && FirstTombstone == DXGMMS2_TIMELINE_IDENTITY_CAPACITY)
            goto Exit;
        {
            ULONG TargetSlot = FirstTombstone != DXGMMS2_TIMELINE_IDENTITY_CAPACITY ? FirstTombstone : Slot;
            LONG64 TargetValue = FirstTombstone != DXGMMS2_TIMELINE_IDENTITY_CAPACITY ? DXGMMS2_TIMELINE_TOMBSTONE : 0;

            InterlockedIncrement(&Timeline->LiveIdentityCount);
            if (InterlockedCompareExchange64(&Timeline->Identities[TargetSlot], Identity, TargetValue) == TargetValue)
            {
                Reserved = TRUE;
                break;
            }
            ASSERT(InterlockedDecrement(&Timeline->LiveIdentityCount) >= 0);
        }
    }

Exit:
    Dxgmms2TimelineReleaseFastCall(Timeline);
    return Reserved;
}

BOOLEAN Dxgmms2TimelinePublishFence(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    LONG64 Identity;
    LONG64 PublishedIdentity;
    ULONG StartSlot;
    ULONG Probe;
    LONG State;
    BOOLEAN Published = FALSE;

    if (!Dxgmms2TimelineAcquireFastCall(Timeline, Generation, &State))
        return FALSE;
    if ((State != Dxgmms2TimelineActive && State != Dxgmms2TimelineStopping) || NodeOrdinal >= Timeline->NodeCount || NodeOrdinal >= DXGMMS2_TIMELINE_MAX_NODES || FenceId == 0)
        goto Exit;
    Identity = Dxgmms2TimelineMakeIdentity(NodeOrdinal, FenceId);
    PublishedIdentity = (LONG64)((ULONGLONG)Identity | DXGMMS2_TIMELINE_PUBLISHED_BIT);
    StartSlot = Dxgmms2TimelineHashIdentity(Identity);
    for (Probe = 0; Probe < DXGMMS2_TIMELINE_IDENTITY_CAPACITY; ++Probe)
    {
        ULONG Slot = (StartSlot + Probe) & (DXGMMS2_TIMELINE_IDENTITY_CAPACITY - 1);
        LONG64 CurrentIdentity = InterlockedCompareExchange64(&Timeline->Identities[Slot], 0, 0);

        if (CurrentIdentity == PublishedIdentity)
        {
            Published = TRUE;
            break;
        }
        if (CurrentIdentity == 0)
            break;
        if (CurrentIdentity == Identity && InterlockedCompareExchange64(&Timeline->Identities[Slot], PublishedIdentity, Identity) == Identity)
        {
            Published = TRUE;
            break;
        }
    }
    if (Published)
        Dxgmms2TimelineUpdateFence(&Timeline->NodeLastSubmittedFenceId[NodeOrdinal], FenceId);

Exit:
    Dxgmms2TimelineReleaseFastCall(Timeline);
    return Published;
}

NTSTATUS Dxgmms2TimelineNotifyFenceCompletion(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId, _In_ ULONG Flags, _Inout_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot)
{
    ULONG CurrentCompletedFence;
    ULONG LastSubmittedFence;
    LONG State;
    NTSTATUS Status;

    if (!Dxgmms2TimelineAcquireFastCall(Timeline, Generation, &State))
        return STATUS_INVALID_DEVICE_STATE;
    if ((State != Dxgmms2TimelineActive && State != Dxgmms2TimelineStopping) || NodeOrdinal >= Timeline->NodeCount || NodeOrdinal >= DXGMMS2_TIMELINE_MAX_NODES || (Flags & ~DXGMMS2_TIMELINE_NOTIFY_VALID_MASK) != 0)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }
    Status = Dxgmms2TimelineValidateSnapshot(Snapshot);
    if (!NT_SUCCESS(Status))
        goto Exit;
    LastSubmittedFence = (ULONG)InterlockedCompareExchange((volatile LONG *)&Timeline->NodeLastSubmittedFenceId[NodeOrdinal], 0, 0);
    CurrentCompletedFence = (ULONG)InterlockedCompareExchange((volatile LONG *)&Timeline->NodeLastCompletedFenceId[NodeOrdinal], 0, 0);
    if (((Flags & DXGMMS2_TIMELINE_NOTIFY_PREEMPTED) == 0 && FenceId == 0) || LastSubmittedFence == 0 || (FenceId != 0 && !Dxgmms2TimelineFenceReached(LastSubmittedFence, FenceId)) || (((Flags & DXGMMS2_TIMELINE_NOTIFY_PREEMPTED) == 0 && !Dxgmms2TimelineLookupPublished(Timeline, NodeOrdinal, FenceId)) || ((Flags & DXGMMS2_TIMELINE_NOTIFY_PREEMPTED) != 0 && FenceId != 0 && !Dxgmms2TimelineFenceReached(CurrentCompletedFence, FenceId) && !Dxgmms2TimelineLookupPublished(Timeline, NodeOrdinal, FenceId))))
    {
        Status = STATUS_DATA_ERROR;
        goto Exit;
    }
    if (FenceId != 0)
    {
        Dxgmms2TimelineUpdateFence(&Timeline->LastCompletedFenceId, FenceId);
        Dxgmms2TimelineUpdateFence(&Timeline->NodeLastCompletedFenceId[NodeOrdinal], FenceId);
    }
    Dxgmms2TimelineFillSnapshot(Timeline, Generation, NodeOrdinal, Snapshot);
    Status = STATUS_SUCCESS;

Exit:
    Dxgmms2TimelineReleaseFastCall(Timeline);
    return Status;
}

BOOLEAN Dxgmms2TimelineIsFencePublished(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    LONG State;
    BOOLEAN Published = FALSE;

    if (!Dxgmms2TimelineAcquireFastCall(Timeline, Generation, &State))
        return FALSE;
    if ((State == Dxgmms2TimelineActive || State == Dxgmms2TimelineStopping || State == Dxgmms2TimelineStopped) && NodeOrdinal < Timeline->NodeCount && NodeOrdinal < DXGMMS2_TIMELINE_MAX_NODES && FenceId != 0)
        Published = Dxgmms2TimelineLookupPublished(Timeline, NodeOrdinal, FenceId);
    Dxgmms2TimelineReleaseFastCall(Timeline);
    return Published;
}

BOOLEAN Dxgmms2TimelineReleaseFence(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    LONG64 Identity;
    LONG64 PublishedIdentity;
    ULONG StartSlot;
    ULONG Probe;
    LONG State;
    BOOLEAN Released = FALSE;

    if (!Dxgmms2TimelineAcquireFastCall(Timeline, Generation, &State))
        return FALSE;
    if (NodeOrdinal >= Timeline->NodeCount || NodeOrdinal >= DXGMMS2_TIMELINE_MAX_NODES || FenceId == 0)
        goto Exit;
    Identity = Dxgmms2TimelineMakeIdentity(NodeOrdinal, FenceId);
    PublishedIdentity = (LONG64)((ULONGLONG)Identity | DXGMMS2_TIMELINE_PUBLISHED_BIT);
    StartSlot = Dxgmms2TimelineHashIdentity(Identity);
    for (Probe = 0; Probe < DXGMMS2_TIMELINE_IDENTITY_CAPACITY; ++Probe)
    {
        ULONG Slot = (StartSlot + Probe) & (DXGMMS2_TIMELINE_IDENTITY_CAPACITY - 1);
        LONG64 CurrentIdentity = InterlockedCompareExchange64(&Timeline->Identities[Slot], 0, 0);

        if (CurrentIdentity == 0)
            break;
        if (CurrentIdentity != Identity && CurrentIdentity != PublishedIdentity)
            continue;
        if (InterlockedCompareExchange64(&Timeline->Identities[Slot], DXGMMS2_TIMELINE_TOMBSTONE, CurrentIdentity) == CurrentIdentity)
        {
            ASSERT(InterlockedDecrement(&Timeline->LiveIdentityCount) >= 0);
            Released = TRUE;
            break;
        }
    }

Exit:
    Dxgmms2TimelineReleaseFastCall(Timeline);
    return Released;
}

NTSTATUS Dxgmms2TimelineResetFenceIdentities(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation)
{
    ULONG Slot;
    LONG State;
    NTSTATUS Status;

    PAGED_CODE();
    if (Timeline == NULL || Timeline->Signature != DXGMMS2_TIMELINE_SIGNATURE)
        return STATUS_INVALID_PARAMETER;
    Dxgmms2TimelineAcquireMaintenance(Timeline);
    State = InterlockedCompareExchange(&Timeline->State, 0, 0);
    if ((ULONG)InterlockedCompareExchange(&Timeline->Generation, 0, 0) != Generation || (State != Dxgmms2TimelineActive && State != Dxgmms2TimelineStopping))
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }
    Dxgmms2TimelineCloseFastCalls(Timeline);
    Dxgmms2TimelineDrainFastCalls(Timeline);
    for (Slot = 0; Slot < DXGMMS2_TIMELINE_IDENTITY_CAPACITY; ++Slot)
        InterlockedExchange64(&Timeline->Identities[Slot], 0);
    InterlockedExchange(&Timeline->LiveIdentityCount, 0);
    Dxgmms2TimelineOpenFastCalls(Timeline);
    Status = STATUS_SUCCESS;

Exit:
    Dxgmms2TimelineReleaseMaintenance(Timeline);
    return Status;
}

NTSTATUS Dxgmms2TimelineQueryFenceSnapshot(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _Inout_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot)
{
    LONG State;
    NTSTATUS Status;

    PAGED_CODE();
    if (!Dxgmms2TimelineAcquireFastCall(Timeline, Generation, &State))
        return STATUS_INVALID_DEVICE_STATE;
    if (NodeOrdinal >= Timeline->NodeCount || NodeOrdinal >= DXGMMS2_TIMELINE_MAX_NODES)
        Status = STATUS_INVALID_PARAMETER;
    else
    {
        Status = Dxgmms2TimelineValidateSnapshot(Snapshot);
        if (NT_SUCCESS(Status))
            Dxgmms2TimelineFillSnapshot(Timeline, Generation, NodeOrdinal, Snapshot);
    }
    Dxgmms2TimelineReleaseFastCall(Timeline);
    return Status;
}
