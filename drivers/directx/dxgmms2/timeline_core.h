/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Lock-free submission-fence timeline state machine
 */

#ifndef _DXGMMS2_TIMELINE_CORE_H_
#define _DXGMMS2_TIMELINE_CORE_H_

#define DXGMMS2_TIMELINE_SIGNATURE                    'T2MG'
#define DXGMMS2_TIMELINE_MAX_NODES                    8
#define DXGMMS2_TIMELINE_IDENTITY_CAPACITY             8192
#define DXGMMS2_TIMELINE_PUBLISHED_BIT                 0x8000000000000000ULL
#define DXGMMS2_TIMELINE_TOMBSTONE                     ((LONG64)-1)

typedef enum _DXGMMS2_TIMELINE_STATE
{
    Dxgmms2TimelineCreated = 0,
    Dxgmms2TimelineActive = 1,
    Dxgmms2TimelineStopping = 2,
    Dxgmms2TimelineStopped = 3,
    Dxgmms2TimelineDestroying = 4
} DXGMMS2_TIMELINE_STATE;

typedef struct _DXGMMS2_TIMELINE_CONTEXT
{
    ULONG Signature;
    KMUTEX MaintenanceMutex;
    volatile LONG State;
    volatile LONG FastCallsOpen;
    volatile LONG ActiveFastCalls;
    volatile LONG Generation;
    ULONG NodeCount;
    volatile LONG NextFenceId;
    volatile LONG LiveIdentityCount;
    volatile ULONG LastCompletedFenceId;
    volatile ULONG NodeLastSubmittedFenceId[DXGMMS2_TIMELINE_MAX_NODES];
    volatile ULONG NodeLastCompletedFenceId[DXGMMS2_TIMELINE_MAX_NODES];
    volatile LONG64 Identities[DXGMMS2_TIMELINE_IDENTITY_CAPACITY];
} DXGMMS2_TIMELINE_CONTEXT, *PDXGMMS2_TIMELINE_CONTEXT;

VOID Dxgmms2TimelineInitialize(_Out_ PDXGMMS2_TIMELINE_CONTEXT Timeline);
NTSTATUS Dxgmms2TimelineStart(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG NodeCount);
NTSTATUS Dxgmms2TimelineBeginStop(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline);
NTSTATUS Dxgmms2TimelineCompleteStop(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline);
NTSTATUS Dxgmms2TimelinePrepareDestroy(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline);
ULONG Dxgmms2TimelineAllocateFence(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation);
BOOLEAN Dxgmms2TimelineReserveFence(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId);
BOOLEAN Dxgmms2TimelinePublishFence(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId);
NTSTATUS Dxgmms2TimelineNotifyFenceCompletion(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId, _In_ ULONG Flags, _Inout_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot);
BOOLEAN Dxgmms2TimelineIsFencePublished(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId);
BOOLEAN Dxgmms2TimelineReleaseFence(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId);
NTSTATUS Dxgmms2TimelineResetFenceIdentities(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation);
NTSTATUS Dxgmms2TimelineQueryFenceSnapshot(_Inout_ PDXGMMS2_TIMELINE_CONTEXT Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _Inout_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot);

#endif /* _DXGMMS2_TIMELINE_CORE_H_ */
