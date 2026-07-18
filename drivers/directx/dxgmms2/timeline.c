/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Provider-facing scheduler timeline interface
 */

#include "dxgmms2_private.h"

#ifdef _WIN64
C_ASSERT(sizeof(DXGMMS2_FENCE_SNAPSHOT_V1) == 32);
C_ASSERT(sizeof(DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1) == 96);
C_ASSERT((FIELD_OFFSET(DXGMMS2_TIMELINE_CONTEXT, Identities) & (sizeof(LONG64) - 1)) == 0);
#endif

static ULONG NTAPI Dxgmms2AllocateFence(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation)
{
    return Dxgmms2TimelineAllocateFence((PDXGMMS2_TIMELINE_CONTEXT)Timeline, Generation);
}

static BOOLEAN NTAPI Dxgmms2ReserveFence(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    return Dxgmms2TimelineReserveFence((PDXGMMS2_TIMELINE_CONTEXT)Timeline, Generation, NodeOrdinal, FenceId);
}

static BOOLEAN NTAPI Dxgmms2PublishFence(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    return Dxgmms2TimelinePublishFence((PDXGMMS2_TIMELINE_CONTEXT)Timeline, Generation, NodeOrdinal, FenceId);
}

static NTSTATUS NTAPI Dxgmms2NotifyFenceCompletion(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId, _In_ ULONG Flags, _Inout_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot)
{
    return Dxgmms2TimelineNotifyFenceCompletion((PDXGMMS2_TIMELINE_CONTEXT)Timeline, Generation, NodeOrdinal, FenceId, Flags, Snapshot);
}

static BOOLEAN NTAPI Dxgmms2IsFencePublished(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    return Dxgmms2TimelineIsFencePublished((PDXGMMS2_TIMELINE_CONTEXT)Timeline, Generation, NodeOrdinal, FenceId);
}

static BOOLEAN NTAPI Dxgmms2ReleaseFence(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId)
{
    return Dxgmms2TimelineReleaseFence((PDXGMMS2_TIMELINE_CONTEXT)Timeline, Generation, NodeOrdinal, FenceId);
}

static NTSTATUS NTAPI Dxgmms2ResetFenceIdentities(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation)
{
    return Dxgmms2TimelineResetFenceIdentities((PDXGMMS2_TIMELINE_CONTEXT)Timeline, Generation);
}

static NTSTATUS NTAPI Dxgmms2QueryFenceSnapshot(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _Inout_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot)
{
    return Dxgmms2TimelineQueryFenceSnapshot((PDXGMMS2_TIMELINE_CONTEXT)Timeline, Generation, NodeOrdinal, Snapshot);
}

NTSTATUS NTAPI Dxgmms2QuerySchedulerTimelineInterface(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _Inout_ DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 *TimelineInterface)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    ULONG Capacity;

    PAGED_CODE();
    if (TimelineInterface == NULL)
        return STATUS_INVALID_PARAMETER;
    Capacity = TimelineInterface->Size;
    if (Capacity < FIELD_OFFSET(DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1, Version) + sizeof(TimelineInterface->Version))
    {
        if (Capacity >= sizeof(TimelineInterface->Size))
            TimelineInterface->Size = DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (TimelineInterface->Version != DXGMMS2_SCHEDULER_TIMELINE_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Capacity < DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1_SIZE)
    {
        TimelineInterface->Size = DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    if (InterlockedCompareExchange(&Context->State, 0, 0) != Dxgmms2AdapterStarted)
    {
        Dxgmms2DereferenceAdapterContext(Context);
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlZeroMemory(TimelineInterface, DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1_SIZE);
    TimelineInterface->Size = DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1_SIZE;
    TimelineInterface->Version = DXGMMS2_SCHEDULER_TIMELINE_VERSION_1;
    TimelineInterface->FeatureFlags = DXGMMS2_TIMELINE_FEATURE_FENCE_IDENTITIES;
    TimelineInterface->Generation = (ULONG)InterlockedCompareExchange(&Context->Timeline.Generation, 0, 0);
    TimelineInterface->TimelineHandle = &Context->Timeline;
    TimelineInterface->NodeCount = Context->Timeline.NodeCount;
    TimelineInterface->AllocateFence = Dxgmms2AllocateFence;
    TimelineInterface->ReserveFence = Dxgmms2ReserveFence;
    TimelineInterface->PublishFence = Dxgmms2PublishFence;
    TimelineInterface->NotifyFenceCompletion = Dxgmms2NotifyFenceCompletion;
    TimelineInterface->IsFencePublished = Dxgmms2IsFencePublished;
    TimelineInterface->ReleaseFence = Dxgmms2ReleaseFence;
    TimelineInterface->ResetFenceIdentities = Dxgmms2ResetFenceIdentities;
    TimelineInterface->QueryFenceSnapshot = Dxgmms2QueryFenceSnapshot;
    Dxgmms2DereferenceAdapterContext(Context);
    return STATUS_SUCCESS;
}
