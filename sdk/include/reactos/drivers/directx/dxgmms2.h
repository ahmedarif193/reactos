/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Private typed contract between dxgkrnl.sys and dxgmms2.sys
 */

#ifndef _REACTOS_DXGMMS2_H_
#define _REACTOS_DXGMMS2_H_

#include <ntddk.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DXGMMS2_ABI_VERSION_1                         0x00010000UL
#define DXGMMS2_ABI_VERSION_2                         0x00020000UL
#define DXGMMS2_ABI_VERSION_3                         0x00030000UL
#define DXGMMS2_ABI_VERSION_4                         0x00040000UL
#define DXGMMS2_ABI_VERSION_5                         0x00050000UL
#define DXGMMS2_SCHEDULER_TIMELINE_VERSION_1          0x00010000UL
#define DXGMMS2_CONTEXT_STREAM_VERSION_1              0x00010000UL
#define DXGMMS2_SCHEDULER_VERSION_1                   0x00010000UL

#define DXGMMS2_TIMELINE_FEATURE_FENCE_IDENTITIES     0x00000001UL
#define DXGMMS2_TIMELINE_FEATURE_VALID_MASK           0x00000001UL
#define DXGMMS2_TIMELINE_NOTIFY_PREEMPTED             0x00000001UL
#define DXGMMS2_TIMELINE_NOTIFY_VALID_MASK             0x00000001UL

#define DXGMMS2_ADAPTER_FLAG_DISPLAY_ONLY             0x00000001UL
#define DXGMMS2_ADAPTER_FLAG_PHYSICAL_ADDRESSING      0x00000002UL
#define DXGMMS2_ADAPTER_FLAG_VIRTUAL_ADDRESSING       0x00000004UL
#define DXGMMS2_ADAPTER_FLAG_VALID_MASK               0x00000007UL

/*
 * The highest WDDM DDI version whose scheduling and video-memory contracts
 * dxgmms2 implements completely, reported back as HighestCompleteWddmVersion.
 * Must equal DXGKDDI_INTERFACE_VERSION_WDDM2_0; dxgkrnl asserts that.
 */
#define DXGMMS2_WDDM_VERSION_2_0                      0x5023UL

#define DXGMMS2_SUBSYSTEM_SCHEDULER                   0x0000000000000001ULL
#define DXGMMS2_SUBSYSTEM_VIDMM                       0x0000000000000002ULL
#define DXGMMS2_SUBSYSTEM_VALID_MASK                  0x0000000000000003ULL

#define DXGMMS2_COMPLETE_STOP_HARDWARE_RETIRED        0x00000001UL
#define DXGMMS2_COMPLETE_STOP_VALID_MASK              0x00000001UL

typedef PVOID DXGMMS2_REGISTRATION_HANDLE;
typedef PVOID DXGMMS2_ADAPTER_HANDLE;
typedef PVOID DXGMMS2_SCHEDULER_TIMELINE_HANDLE;
typedef PVOID DXGMMS2_CONTEXT_STREAM_HANDLE;

typedef enum _DXGMMS2_STOP_REASON
{
    Dxgmms2StopReasonPnpStop = 1,
    Dxgmms2StopReasonRemove = 2,
    Dxgmms2StopReasonSurpriseRemove = 3,
    Dxgmms2StopReasonStartRollback = 4
} DXGMMS2_STOP_REASON;

typedef BOOLEAN (NTAPI *PDXGMMS2_REFERENCE_ADAPTER)(_In_opt_ PVOID ClientContext, _In_ PVOID AdapterCookie);
typedef VOID (NTAPI *PDXGMMS2_DEREFERENCE_ADAPTER)(_In_opt_ PVOID ClientContext, _In_ PVOID AdapterCookie);

/* These callbacks guard future transient reverse calls; v1 retains no adapter reference. */
typedef struct _DXGMMS2_DXGKRNL_INTERFACE_V1
{
    ULONG Size;
    ULONG Version;
    PVOID ClientContext;
    PDXGMMS2_REFERENCE_ADAPTER ReferenceAdapter;
    PDXGMMS2_DEREFERENCE_ADAPTER DereferenceAdapter;
} DXGMMS2_DXGKRNL_INTERFACE_V1, *PDXGMMS2_DXGKRNL_INTERFACE_V1;

typedef struct _DXGMMS2_CREATE_ADAPTER_INFO_V1
{
    ULONG Size;
    ULONG Version;
    PVOID AdapterCookie;
    ULONG AdapterFlags;
    ULONG Reserved;
} DXGMMS2_CREATE_ADAPTER_INFO_V1, *PDXGMMS2_CREATE_ADAPTER_INFO_V1;

typedef struct _DXGMMS2_START_ADAPTER_INFO_V1
{
    ULONG Size;
    ULONG Version;
    ULONG MiniportDdiVersion;
    ULONG RequestedWddmVersion;
    ULONG NodeCount;
    ULONG SegmentCount;
    ULONG AdapterFlags;
    ULONG SchedulingCaps;
} DXGMMS2_START_ADAPTER_INFO_V1, *PDXGMMS2_START_ADAPTER_INFO_V1;

typedef struct _DXGMMS2_START_ADAPTER_RESULT_V1
{
    ULONG Size;
    ULONG Version;
    ULONGLONG EnabledSubsystems;
    ULONG HighestCompleteWddmVersion;
    ULONG Reserved;
} DXGMMS2_START_ADAPTER_RESULT_V1, *PDXGMMS2_START_ADAPTER_RESULT_V1;

typedef struct _DXGMMS2_STOP_ADAPTER_INFO_V1
{
    ULONG Size;
    ULONG Version;
    ULONG Reason;
    ULONG Flags;
} DXGMMS2_STOP_ADAPTER_INFO_V1, *PDXGMMS2_STOP_ADAPTER_INFO_V1;

typedef NTSTATUS (NTAPI *PDXGMMS2_CREATE_ADAPTER)(_In_ DXGMMS2_REGISTRATION_HANDLE Registration, _In_ const DXGMMS2_CREATE_ADAPTER_INFO_V1 *Info, _Out_ DXGMMS2_ADAPTER_HANDLE *Adapter);
typedef NTSTATUS (NTAPI *PDXGMMS2_START_ADAPTER)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_START_ADAPTER_INFO_V1 *Info, _Inout_ DXGMMS2_START_ADAPTER_RESULT_V1 *Result);
typedef NTSTATUS (NTAPI *PDXGMMS2_BEGIN_STOP_ADAPTER)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_STOP_ADAPTER_INFO_V1 *Info);
typedef NTSTATUS (NTAPI *PDXGMMS2_COMPLETE_STOP_ADAPTER)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_STOP_ADAPTER_INFO_V1 *Info);
typedef NTSTATUS (NTAPI *PDXGMMS2_DESTROY_ADAPTER)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter);

typedef struct _DXGMMS2_PROVIDER_INTERFACE_V1
{
    ULONG Size;
    ULONG Version;
    ULONG ProviderFlags;
    ULONG Reserved;
    DXGMMS2_REGISTRATION_HANDLE RegistrationHandle;
    PDXGMMS2_CREATE_ADAPTER CreateAdapter;
    PDXGMMS2_START_ADAPTER StartAdapter;
    PDXGMMS2_BEGIN_STOP_ADAPTER BeginStopAdapter;
    PDXGMMS2_COMPLETE_STOP_ADAPTER CompleteStopAdapter;
    PDXGMMS2_DESTROY_ADAPTER DestroyAdapter;
} DXGMMS2_PROVIDER_INTERFACE_V1, *PDXGMMS2_PROVIDER_INTERFACE_V1;

typedef struct _DXGMMS2_FENCE_SNAPSHOT_V1
{
    ULONG Size;
    ULONG Version;
    ULONG Generation;
    ULONG NodeOrdinal;
    ULONG LastSubmittedFence;
    ULONG LastCompletedFence;
    ULONG GlobalLastCompletedFence;
    ULONG Reserved;
} DXGMMS2_FENCE_SNAPSHOT_V1, *PDXGMMS2_FENCE_SNAPSHOT_V1;

/* Allocate, Reserve, Publish, Notify, Is, and Release are nonpaged,
 * nonblocking callbacks callable from ISR/DIRQL. Notify supplies the ISR
 * snapshot. Reset and Query are PASSIVE_LEVEL-only maintenance operations. */
typedef ULONG (NTAPI *PDXGMMS2_ALLOCATE_FENCE)(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation);
typedef BOOLEAN (NTAPI *PDXGMMS2_RESERVE_FENCE)(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId);
typedef BOOLEAN (NTAPI *PDXGMMS2_PUBLISH_FENCE)(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId);
typedef NTSTATUS (NTAPI *PDXGMMS2_NOTIFY_FENCE_COMPLETION)(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId, _In_ ULONG Flags, _Inout_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot);
typedef BOOLEAN (NTAPI *PDXGMMS2_IS_FENCE_PUBLISHED)(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId);
typedef BOOLEAN (NTAPI *PDXGMMS2_RELEASE_FENCE)(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _In_ ULONG FenceId);
typedef NTSTATUS (NTAPI *PDXGMMS2_RESET_FENCE_IDENTITIES)(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation);
typedef NTSTATUS (NTAPI *PDXGMMS2_QUERY_FENCE_SNAPSHOT)(_In_ DXGMMS2_SCHEDULER_TIMELINE_HANDLE Timeline, _In_ ULONG Generation, _In_ ULONG NodeOrdinal, _Inout_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot);

typedef struct _DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1
{
    ULONG Size;
    ULONG Version;
    ULONG FeatureFlags;
    ULONG Generation;
    DXGMMS2_SCHEDULER_TIMELINE_HANDLE TimelineHandle;
    ULONG NodeCount;
    ULONG Reserved;
    PDXGMMS2_ALLOCATE_FENCE AllocateFence;
    PDXGMMS2_RESERVE_FENCE ReserveFence;
    PDXGMMS2_PUBLISH_FENCE PublishFence;
    PDXGMMS2_NOTIFY_FENCE_COMPLETION NotifyFenceCompletion;
    PDXGMMS2_IS_FENCE_PUBLISHED IsFencePublished;
    PDXGMMS2_RELEASE_FENCE ReleaseFence;
    PDXGMMS2_RESET_FENCE_IDENTITIES ResetFenceIdentities;
    PDXGMMS2_QUERY_FENCE_SNAPSHOT QueryFenceSnapshot;
} DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1, *PDXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1;

typedef NTSTATUS (NTAPI *PDXGMMS2_QUERY_SCHEDULER_TIMELINE_INTERFACE)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _Inout_ DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 *TimelineInterface);

typedef struct _DXGMMS2_PROVIDER_INTERFACE_V2
{
    ULONG Size;
    ULONG Version;
    ULONG ProviderFlags;
    ULONG Reserved;
    DXGMMS2_REGISTRATION_HANDLE RegistrationHandle;
    PDXGMMS2_CREATE_ADAPTER CreateAdapter;
    PDXGMMS2_START_ADAPTER StartAdapter;
    PDXGMMS2_BEGIN_STOP_ADAPTER BeginStopAdapter;
    PDXGMMS2_COMPLETE_STOP_ADAPTER CompleteStopAdapter;
    PDXGMMS2_DESTROY_ADAPTER DestroyAdapter;
    PDXGMMS2_QUERY_SCHEDULER_TIMELINE_INTERFACE QuerySchedulerTimelineInterface;
} DXGMMS2_PROVIDER_INTERFACE_V2, *PDXGMMS2_PROVIDER_INTERFACE_V2;

/* The context-stream contract is an internal phase-1 ordering primitive.  It
 * does not advertise a WDDM level or transfer public scheduler ownership.
 * All callbacks are PASSIVE_LEVEL-only. ClientTag and ObjectId are opaque and
 * remain caller-owned. Every successful admission produces exactly one
 * retirement record, including cancellation/TDR/stop paths; callers must
 * drain those records before destroying a stream. Every successful action
 * claim must be committed exactly once, and stop remains busy until claims
 * are returned. All pointed-to descriptors, arrays, and output buffers must
 * remain resident and stable for the duration of a callback. */
#define DXGMMS2_CONTEXT_STREAM_DEFAULT_QUEUE_DEPTH     64UL
#define DXGMMS2_CONTEXT_STREAM_MIN_QUEUE_DEPTH         2UL
#define DXGMMS2_CONTEXT_STREAM_MAX_QUEUE_DEPTH         1024UL
/* D3DKMT signal2 permits 64 additional broadcast contexts in addition to
 * the owning hContext, so an atomic transaction can contain 65 streams. */
#define DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS  65UL

#define DXGMMS2_CONTEXT_SIGNAL_AT_SUBMISSION           0x00000001UL
#define DXGMMS2_CONTEXT_SIGNAL_VALID_MASK              0x00000001UL

#define DXGMMS2_CONTEXT_CANCEL_FORCE_SUBMITTED         0x00000001UL
#define DXGMMS2_CONTEXT_CANCEL_TDR                      0x00000002UL
#define DXGMMS2_CONTEXT_CANCEL_VALID_MASK               0x00000003UL

typedef enum _DXGMMS2_CONTEXT_STREAM_PUBLIC_STATE
{
    Dxgmms2ContextStreamPublicActive = 1,
    Dxgmms2ContextStreamPublicClosing = 2
} DXGMMS2_CONTEXT_STREAM_PUBLIC_STATE;

typedef enum _DXGMMS2_CONTEXT_ACTION_TYPE
{
    Dxgmms2ContextActionWork = 1,
    Dxgmms2ContextActionSignal = 2,
    Dxgmms2ContextActionWait = 3
} DXGMMS2_CONTEXT_ACTION_TYPE;

typedef struct _DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1
{
    ULONG Size;
    ULONG Version;
    ULONG NodeOrdinal;
    ULONG QueueDepth;
    ULONG Flags;
    ULONG Reserved;
} DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1, *PDXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1;

typedef struct _DXGMMS2_ADMIT_CONTEXT_WORK_V1
{
    ULONG Size;
    ULONG Version;
    ULONGLONG ClientTag;
} DXGMMS2_ADMIT_CONTEXT_WORK_V1, *PDXGMMS2_ADMIT_CONTEXT_WORK_V1;

typedef struct _DXGMMS2_ADMIT_CONTEXT_WAIT_V1
{
    ULONG Size;
    ULONG Version;
    ULONG Flags;
    ULONG Reserved;
    ULONGLONG ObjectId;
    ULONGLONG FenceValue;
    ULONGLONG ClientTag;
} DXGMMS2_ADMIT_CONTEXT_WAIT_V1, *PDXGMMS2_ADMIT_CONTEXT_WAIT_V1;

typedef struct _DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1
{
    ULONG Size;
    ULONG Version;
    ULONG Flags;
    ULONG ContextCount;
    const DXGMMS2_CONTEXT_STREAM_HANDLE *ContextHandles;
    ULONGLONG *Sequences;
    ULONGLONG ObjectId;
    ULONGLONG FenceValue;
    ULONGLONG ClientTag;
} DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1, *PDXGMMS2_ADMIT_CONTEXT_SIGNAL_V1;

typedef struct _DXGMMS2_CONTEXT_ACTION_V1
{
    ULONG Size;
    ULONG Version;
    ULONG Type;
    ULONG Flags;
    DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle;
    ULONGLONG Sequence;
    ULONGLONG TransactionId;
    ULONGLONG ClaimToken;
    ULONGLONG ObjectId;
    ULONGLONG FenceValue;
    ULONGLONG ClientTag;
} DXGMMS2_CONTEXT_ACTION_V1, *PDXGMMS2_CONTEXT_ACTION_V1;

typedef struct _DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1
{
    ULONG Size;
    ULONG Version;
    ULONG Flags;
    ULONG Reason;
} DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1, *PDXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1;

typedef struct _DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1
{
    ULONG Size;
    ULONG Version;
    ULONG Generation;
    ULONG State;
    ULONG NodeOrdinal;
    ULONG QueueDepth;
    ULONG QueuedEntryCount;
    ULONG ActiveClaimCount;
    ULONG CancelReason;
    ULONG RetiredEntryCount;
    ULONGLONG LastAdmittedSequence;
    ULONGLONG LastSubmittedSequence;
    ULONGLONG LastCompletedSequence;
} DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1, *PDXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1;

typedef struct _DXGMMS2_CONTEXT_RETIREMENT_V1
{
    ULONGLONG Sequence;
    ULONGLONG TransactionId;
    ULONGLONG ClientTag;
    NTSTATUS TerminalStatus;
    ULONG Type;
    ULONG Flags;
    ULONG Reserved;
} DXGMMS2_CONTEXT_RETIREMENT_V1, *PDXGMMS2_CONTEXT_RETIREMENT_V1;

typedef NTSTATUS (NTAPI *PDXGMMS2_CREATE_CONTEXT_STREAM)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1 *Info, _Out_ DXGMMS2_CONTEXT_STREAM_HANDLE *Context);
typedef NTSTATUS (NTAPI *PDXGMMS2_DESTROY_CONTEXT_STREAM)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context);
typedef NTSTATUS (NTAPI *PDXGMMS2_ADMIT_CONTEXT_WORK)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _In_ const DXGMMS2_ADMIT_CONTEXT_WORK_V1 *Info, _Out_ ULONGLONG *Sequence);
typedef NTSTATUS (NTAPI *PDXGMMS2_ADMIT_CONTEXT_WAIT)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _In_ const DXGMMS2_ADMIT_CONTEXT_WAIT_V1 *Info, _Out_ ULONGLONG *Sequence);
typedef NTSTATUS (NTAPI *PDXGMMS2_ADMIT_CONTEXT_SIGNAL)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1 *Info, _Out_ ULONGLONG *TransactionId);
typedef NTSTATUS (NTAPI *PDXGMMS2_RESOLVE_CONTEXT_WAIT)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _In_ ULONGLONG Sequence, _In_ NTSTATUS WaitStatus);
typedef NTSTATUS (NTAPI *PDXGMMS2_CLAIM_CONTEXT_ACTION)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _Inout_ DXGMMS2_CONTEXT_ACTION_V1 *Action);
typedef NTSTATUS (NTAPI *PDXGMMS2_COMMIT_CONTEXT_ACTION)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _In_ ULONGLONG Sequence, _In_ ULONGLONG ClaimToken, _In_ NTSTATUS ActionStatus);
typedef NTSTATUS (NTAPI *PDXGMMS2_COMPLETE_CONTEXT_WORK)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _In_ ULONGLONG Sequence, _In_ NTSTATUS CompletionStatus);
typedef NTSTATUS (NTAPI *PDXGMMS2_CANCEL_CONTEXT_STREAM)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _In_ const DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1 *Info);
typedef NTSTATUS (NTAPI *PDXGMMS2_QUERY_CONTEXT_STREAM)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _Inout_ DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1 *Snapshot);
typedef NTSTATUS (NTAPI *PDXGMMS2_DRAIN_CONTEXT_RETIREMENTS)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _Out_writes_to_(RecordCapacity, *RetiredCount) DXGMMS2_CONTEXT_RETIREMENT_V1 *Records, _In_ ULONG RecordCapacity, _Out_ ULONG *RetiredCount);

typedef struct _DXGMMS2_CONTEXT_STREAM_INTERFACE_V1
{
    ULONG Size;
    ULONG Version;
    ULONG InterfaceFlags;
    ULONG Generation;
    DXGMMS2_ADAPTER_HANDLE AdapterHandle;
    ULONG MaximumQueueDepth;
    ULONG MaximumBroadcastContexts;
    PDXGMMS2_CREATE_CONTEXT_STREAM CreateContextStream;
    PDXGMMS2_DESTROY_CONTEXT_STREAM DestroyContextStream;
    PDXGMMS2_ADMIT_CONTEXT_WORK AdmitWork;
    PDXGMMS2_ADMIT_CONTEXT_WAIT AdmitWait;
    PDXGMMS2_ADMIT_CONTEXT_SIGNAL AdmitSignal;
    PDXGMMS2_RESOLVE_CONTEXT_WAIT ResolveWait;
    PDXGMMS2_CLAIM_CONTEXT_ACTION ClaimNextAction;
    PDXGMMS2_COMMIT_CONTEXT_ACTION CommitAction;
    PDXGMMS2_COMPLETE_CONTEXT_WORK CompleteWork;
    PDXGMMS2_CANCEL_CONTEXT_STREAM CancelContextStream;
    PDXGMMS2_QUERY_CONTEXT_STREAM QueryContextStream;
    PDXGMMS2_DRAIN_CONTEXT_RETIREMENTS DrainRetirements;
} DXGMMS2_CONTEXT_STREAM_INTERFACE_V1, *PDXGMMS2_CONTEXT_STREAM_INTERFACE_V1;

typedef NTSTATUS (NTAPI *PDXGMMS2_QUERY_CONTEXT_STREAM_INTERFACE)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _Inout_ DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *ContextStreamInterface);

typedef struct _DXGMMS2_PROVIDER_INTERFACE_V3
{
    ULONG Size;
    ULONG Version;
    ULONG ProviderFlags;
    ULONG Reserved;
    DXGMMS2_REGISTRATION_HANDLE RegistrationHandle;
    PDXGMMS2_CREATE_ADAPTER CreateAdapter;
    PDXGMMS2_START_ADAPTER StartAdapter;
    PDXGMMS2_BEGIN_STOP_ADAPTER BeginStopAdapter;
    PDXGMMS2_COMPLETE_STOP_ADAPTER CompleteStopAdapter;
    PDXGMMS2_DESTROY_ADAPTER DestroyAdapter;
    PDXGMMS2_QUERY_SCHEDULER_TIMELINE_INTERFACE QuerySchedulerTimelineInterface;
    PDXGMMS2_QUERY_CONTEXT_STREAM_INTERFACE QueryContextStreamInterface;
} DXGMMS2_PROVIDER_INTERFACE_V3, *PDXGMMS2_PROVIDER_INTERFACE_V3;

/* ========================================================================
 * Scheduler ownership (v4)
 *
 * dxgmms2 owns the per-engine run queues, their admission order, the engine
 * state machines, and the exactly-once retirement of every admitted packet.
 * dxgkrnl owns packet content and the miniport DDI calls: it admits an opaque
 * packet cookie, claims the next runnable one, publishes the dispatch, calls
 * the miniport outside every dxgmms2 lock, then reports the outcome.
 *
 * Invariants this contract preserves:
 *   - fence order equals queue order equals kick order on a node;
 *   - work that was never dispatched is never retired as completed;
 *   - no dxgmms2 lock is held across a miniport DDI;
 *   - every successful admission produces exactly one retirement record.
 * ====================================================================== */

typedef PVOID DXGMMS2_SCHEDULER_HANDLE;

#define DXGMMS2_SCHEDULER_MAX_ENGINES                 8UL
#define DXGMMS2_SCHEDULER_MAX_RETIREMENTS             64UL

/* Admission flags mirror the submission kinds the queue must order. */
#define DXGMMS2_SCHEDULER_ADMIT_PAGING                0x00000001UL
#define DXGMMS2_SCHEDULER_ADMIT_PRESENT               0x00000002UL
#define DXGMMS2_SCHEDULER_ADMIT_VIRTUAL               0x00000004UL
#define DXGMMS2_SCHEDULER_ADMIT_PREFENCED             0x00000008UL
/* The caller holds a ReserveSlot reservation on this engine; admission
 * consumes it instead of taking a second slot against the depth bound. */
#define DXGMMS2_SCHEDULER_ADMIT_CONSUME_RESERVATION   0x00000010UL
#define DXGMMS2_SCHEDULER_ADMIT_VALID_MASK            0x0000001FUL

typedef enum _DXGMMS2_SCHEDULER_ENGINE_STATE
{
    Dxgmms2EngineIdle           = 0,
    Dxgmms2EngineRunning        = 1,
    Dxgmms2EngineBudgetComputed = 2,
    Dxgmms2EngineSubmitting     = 3,
    Dxgmms2EnginePreempting     = 4,
    Dxgmms2EnginePreempted      = 5,
    Dxgmms2EngineResetting      = 6,
    Dxgmms2EngineResetComplete  = 7,
    Dxgmms2EngineSuspended      = 8,
    Dxgmms2EngineResuming       = 9,
    Dxgmms2EngineFlipPending    = 10,
    Dxgmms2EngineFlipExecuting  = 11,
    Dxgmms2EngineCompleting     = 12,
    Dxgmms2EngineError          = 13,
    Dxgmms2EngineStateCount     = 14
} DXGMMS2_SCHEDULER_ENGINE_STATE;

typedef enum _DXGMMS2_SCHEDULER_RETIRE_REASON
{
    Dxgmms2RetireCompleted = 1,
    Dxgmms2RetireCancelled = 2,
    Dxgmms2RetireAborted   = 3
} DXGMMS2_SCHEDULER_RETIRE_REASON;

/*
 * Admission is strictly FIFO per engine, and deliberately so: dxgkrnl's
 * tracked-DMA retirement requires fence order to equal queue order to equal
 * kick order, so reordering by Priority would let a later fence retire work
 * that has not executed.  Priority is recorded because it ranks eviction
 * victims, not because it reorders dispatch.
 */
typedef struct _DXGMMS2_SCHEDULER_ADMIT_INFO_V1
{
    ULONG     Size;
    ULONG     Version;
    ULONG     NodeOrdinal;
    ULONG     EngineOrdinal;
    ULONG     Flags;
    LONG      Priority;
    ULONGLONG PacketCookie;      /* opaque dxgkrnl packet identity */
    ULONGLONG OwnerCookie;       /* opaque dxgkrnl context identity, or zero */
    ULONG     SubmissionFenceId; /* nonzero when the caller pre-assigned it */
    ULONG     Reserved;
} DXGMMS2_SCHEDULER_ADMIT_INFO_V1, *PDXGMMS2_SCHEDULER_ADMIT_INFO_V1;

typedef struct _DXGMMS2_SCHEDULER_CLAIM_V1
{
    ULONG     Size;
    ULONG     Version;
    ULONG     NodeOrdinal;
    ULONG     EngineOrdinal;
    ULONGLONG PacketCookie;
    ULONGLONG ClaimToken;
    ULONG     SubmissionFenceId;
    ULONG     Flags;
} DXGMMS2_SCHEDULER_CLAIM_V1, *PDXGMMS2_SCHEDULER_CLAIM_V1;

typedef struct _DXGMMS2_SCHEDULER_RETIREMENT_V1
{
    ULONGLONG PacketCookie;
    ULONGLONG OwnerCookie;
    ULONG     SubmissionFenceId;
    ULONG     Reason;
    NTSTATUS  TerminalStatus;
    ULONG     Reserved;
} DXGMMS2_SCHEDULER_RETIREMENT_V1, *PDXGMMS2_SCHEDULER_RETIREMENT_V1;

typedef struct _DXGMMS2_SCHEDULER_ENGINE_STATUS_V1
{
    ULONG Size;
    ULONG Version;
    ULONG State;
    ULONG PendingPacketCount;
    ULONG LastSubmittedFenceId;
    ULONG LastCompletedFenceId;
    ULONG OldestKickedFenceId;
    ULONG Reserved;
} DXGMMS2_SCHEDULER_ENGINE_STATUS_V1, *PDXGMMS2_SCHEDULER_ENGINE_STATUS_V1;

typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_START)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineCount);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_ADMIT_PACKET)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ const DXGMMS2_SCHEDULER_ADMIT_INFO_V1 *Info, _Out_ PULONG OutFenceId);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_CLAIM_PACKET)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineOrdinal, _Inout_ DXGMMS2_SCHEDULER_CLAIM_V1 *Claim);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_PUBLISH_DISPATCH)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineOrdinal, _In_ ULONGLONG ClaimToken);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_COMPLETE_DISPATCH)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineOrdinal, _In_ ULONGLONG ClaimToken, _In_ NTSTATUS DispatchStatus);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_NOTIFY_COMPLETION)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineOrdinal, _In_ ULONG CompletedFenceId);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_DRAIN_RETIREMENTS)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _Out_writes_to_(Capacity, *RetiredCount) DXGMMS2_SCHEDULER_RETIREMENT_V1 *Records, _In_ ULONG Capacity, _Out_ PULONG RetiredCount);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_CANCEL_OWNER)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONGLONG OwnerCookie, _In_ NTSTATUS CancelStatus);
/* Teardown must be able to reclaim packets the miniport already owns, once
 * the caller has quiesced hardware; ordinary aborts leave them alone. */
#define DXGMMS2_SCHEDULER_ABORT_INCLUDE_DISPATCHED  0x00000001UL

typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_ABORT_ALL)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG Flags, _In_ NTSTATUS AbortStatus);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_SET_ADMISSION)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ BOOLEAN Open);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_QUERY_ENGINE)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineOrdinal, _Inout_ DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 *Status);
/*
 * SetEngineState is a validated compare-and-set.  The transition table is
 * dxgmms2's, so an out-of-contract move is rejected rather than applied; pass
 * DXGMMS2_ENGINE_STATE_ANY as ExpectedState for an unconditional set.  On
 * STATUS_INVALID_DEVICE_STATE the engine did not match ExpectedState, and
 * OutPreviousState reports the state that was observed either way.
 */
#define DXGMMS2_ENGINE_STATE_ANY    0xFFFFFFFFUL

typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_SET_ENGINE_STATE)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineOrdinal, _In_ ULONG ExpectedState, _In_ ULONG NewState, _Out_opt_ PULONG OutPreviousState);
typedef BOOLEAN  (NTAPI *PDXGMMS2_SCHEDULER_IS_IDLE)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler);
/* Reports the packet a claim would hand out next, without taking the claim.
 * A caller that must authorise a specific packet before dispatching it asks
 * this rather than inferring order from engine state. */
typedef BOOLEAN  (NTAPI *PDXGMMS2_SCHEDULER_PEEK_NEXT)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineOrdinal, _Out_ PULONGLONG OutPacketCookie);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_RESERVE)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineOrdinal);
typedef VOID     (NTAPI *PDXGMMS2_SCHEDULER_UNRESERVE)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineOrdinal);
typedef NTSTATUS (NTAPI *PDXGMMS2_SCHEDULER_RESET_DISPATCHED)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _In_ ULONG EngineOrdinal, _Out_writes_to_(Capacity, *Count) ULONGLONG *PacketCookies, _In_ ULONG Capacity, _Out_ PULONG Count);
typedef BOOLEAN  (NTAPI *PDXGMMS2_SCHEDULER_OLDEST_DISPATCHED)(_In_ DXGMMS2_SCHEDULER_HANDLE Scheduler, _Out_ PULONG EngineOrdinal, _Out_ PULONG FenceId, _Out_ PULONGLONG PacketCookie);

typedef struct _DXGMMS2_SCHEDULER_INTERFACE_V1
{
    ULONG Size;
    ULONG Version;
    ULONG InterfaceFlags;
    ULONG Generation;
    DXGMMS2_SCHEDULER_HANDLE SchedulerHandle;
    ULONG MaximumEngines;
    ULONG Reserved;
    PDXGMMS2_SCHEDULER_START Start;
    PDXGMMS2_SCHEDULER_ADMIT_PACKET AdmitPacket;
    PDXGMMS2_SCHEDULER_CLAIM_PACKET ClaimNextPacket;
    PDXGMMS2_SCHEDULER_PUBLISH_DISPATCH PublishDispatch;
    PDXGMMS2_SCHEDULER_COMPLETE_DISPATCH CompleteDispatch;
    PDXGMMS2_SCHEDULER_NOTIFY_COMPLETION NotifyCompletion;
    PDXGMMS2_SCHEDULER_DRAIN_RETIREMENTS DrainRetirements;
    PDXGMMS2_SCHEDULER_CANCEL_OWNER CancelOwnerPackets;
    PDXGMMS2_SCHEDULER_ABORT_ALL AbortAllPackets;
    PDXGMMS2_SCHEDULER_SET_ADMISSION SetAdmission;
    PDXGMMS2_SCHEDULER_QUERY_ENGINE QueryEngineStatus;
    PDXGMMS2_SCHEDULER_SET_ENGINE_STATE SetEngineState;
    PDXGMMS2_SCHEDULER_IS_IDLE IsIdle;
    PDXGMMS2_SCHEDULER_RESERVE ReserveSlot;
    PDXGMMS2_SCHEDULER_UNRESERVE ReleaseSlot;
    PDXGMMS2_SCHEDULER_RESET_DISPATCHED ResetDispatched;
    PDXGMMS2_SCHEDULER_OLDEST_DISPATCHED GetOldestDispatched;
    PDXGMMS2_SCHEDULER_PEEK_NEXT PeekNextPacket;
} DXGMMS2_SCHEDULER_INTERFACE_V1, *PDXGMMS2_SCHEDULER_INTERFACE_V1;

typedef NTSTATUS (NTAPI *PDXGMMS2_QUERY_SCHEDULER_INTERFACE)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _Inout_ DXGMMS2_SCHEDULER_INTERFACE_V1 *SchedulerInterface);

typedef struct _DXGMMS2_PROVIDER_INTERFACE_V4
{
    ULONG Size;
    ULONG Version;
    ULONG ProviderFlags;
    ULONG Reserved;
    DXGMMS2_REGISTRATION_HANDLE RegistrationHandle;
    PDXGMMS2_CREATE_ADAPTER CreateAdapter;
    PDXGMMS2_START_ADAPTER StartAdapter;
    PDXGMMS2_BEGIN_STOP_ADAPTER BeginStopAdapter;
    PDXGMMS2_COMPLETE_STOP_ADAPTER CompleteStopAdapter;
    PDXGMMS2_DESTROY_ADAPTER DestroyAdapter;
    PDXGMMS2_QUERY_SCHEDULER_TIMELINE_INTERFACE QuerySchedulerTimelineInterface;
    PDXGMMS2_QUERY_CONTEXT_STREAM_INTERFACE QueryContextStreamInterface;
    PDXGMMS2_QUERY_SCHEDULER_INTERFACE QuerySchedulerInterface;
} DXGMMS2_PROVIDER_INTERFACE_V4, *PDXGMMS2_PROVIDER_INTERFACE_V4;


/* ------------------------------------------------------------------------
 * Video memory ownership contract (v1)
 *
 * dxgmms2 owns segment space: the commit ledger, the virgin-space cursor,
 * and the placement decision.  dxgkrnl asks for an offset and reports when a
 * placement is gone; it does not decide where anything lands and does not
 * keep a second copy of segment occupancy.
 * ------------------------------------------------------------------------ */

#define DXGMMS2_VIDMM_VERSION_1                       0x00000001UL
#define DXGMMS2_VIDMM_MAX_SEGMENTS                    32
#define DXGMMS2_VIDMM_MAX_RELEASE_BATCH               64

/* Segment descriptor flags mirror what the miniport reported. */
#define DXGMMS2_VIDMM_SEGMENT_APERTURE                0x00000001UL
#define DXGMMS2_VIDMM_SEGMENT_CPU_VISIBLE             0x00000002UL

/* Per-placement state dxgkrnl publishes so eviction policy can run here. */
#define DXGMMS2_VIDMM_RANGE_PINNED                    0x00000001UL
#define DXGMMS2_VIDMM_RANGE_OFFERED                   0x00000002UL
#define DXGMMS2_VIDMM_RANGE_RESIDENCY_REFERENCED      0x00000004UL

typedef PVOID DXGMMS2_VIDMM_HANDLE;

typedef struct _DXGMMS2_VIDMM_SEGMENT_DESC
{
    ULONGLONG Size;
    ULONGLONG CommitLimit;
    ULONG     Flags;
    ULONG     Reserved;
} DXGMMS2_VIDMM_SEGMENT_DESC_V1, *PDXGMMS2_VIDMM_SEGMENT_DESC_V1;

typedef struct _DXGMMS2_VIDMM_RESERVE_INFO_V1
{
    ULONGLONG Size;
    ULONGLONG Alignment;
    ULONGLONG OwnerCookie;
    LONG      Priority;
    ULONG     Flags;
} DXGMMS2_VIDMM_RESERVE_INFO_V1, *PDXGMMS2_VIDMM_RESERVE_INFO_V1;

typedef struct _DXGMMS2_VIDMM_SEGMENT_STATUS_V1
{
    ULONGLONG Size;
    ULONGLONG CommitLimit;
    ULONGLONG UsedSize;
    ULONGLONG BumpOffset;
    ULONG     PlacementCount;
    ULONG     Flags;
} DXGMMS2_VIDMM_SEGMENT_STATUS_V1, *PDXGMMS2_VIDMM_SEGMENT_STATUS_V1;

typedef NTSTATUS (NTAPI *PDXGMMS2_VIDMM_START)(_In_ DXGMMS2_VIDMM_HANDLE VidMm, _In_ ULONG SegmentCount);
typedef NTSTATUS (NTAPI *PDXGMMS2_VIDMM_SET_SEGMENT)(_In_ DXGMMS2_VIDMM_HANDLE VidMm, _In_ ULONG SegmentIndex, _In_ const DXGMMS2_VIDMM_SEGMENT_DESC_V1 *Desc);
typedef NTSTATUS (NTAPI *PDXGMMS2_VIDMM_RESERVE)(_In_ DXGMMS2_VIDMM_HANDLE VidMm, _In_ ULONG SegmentIndex, _In_ const DXGMMS2_VIDMM_RESERVE_INFO_V1 *Info, _Out_ PULONGLONG OutOffset);
typedef NTSTATUS (NTAPI *PDXGMMS2_VIDMM_RELEASE)(_In_ DXGMMS2_VIDMM_HANDLE VidMm, _In_ ULONG SegmentIndex, _In_ ULONGLONG OwnerCookie);
typedef NTSTATUS (NTAPI *PDXGMMS2_VIDMM_SET_RANGE_STATE)(_In_ DXGMMS2_VIDMM_HANDLE VidMm, _In_ ULONG SegmentIndex, _In_ ULONGLONG OwnerCookie, _In_ LONG Priority, _In_ ULONG Flags);
typedef NTSTATUS (NTAPI *PDXGMMS2_VIDMM_QUERY_SEGMENT)(_In_ DXGMMS2_VIDMM_HANDLE VidMm, _In_ ULONG SegmentIndex, _Inout_ DXGMMS2_VIDMM_SEGMENT_STATUS_V1 *Status);
typedef BOOLEAN  (NTAPI *PDXGMMS2_VIDMM_FIND_VICTIM)(_In_ DXGMMS2_VIDMM_HANDLE VidMm, _In_ ULONG SegmentIndex, _In_ ULONG ExcludeFlags, _Out_ PULONGLONG OutOwnerCookie);
typedef NTSTATUS (NTAPI *PDXGMMS2_VIDMM_RELEASE_ALL)(_In_ DXGMMS2_VIDMM_HANDLE VidMm, _Out_writes_to_(Capacity, *Count) PULONGLONG OwnerCookies, _In_ ULONG Capacity, _Out_ PULONG Count);

typedef struct _DXGMMS2_VIDMM_INTERFACE_V1
{
    ULONG Size;
    ULONG Version;
    DXGMMS2_VIDMM_HANDLE VidMmHandle;
    PDXGMMS2_VIDMM_START Start;
    PDXGMMS2_VIDMM_SET_SEGMENT SetSegment;
    PDXGMMS2_VIDMM_RESERVE ReservePlacement;
    PDXGMMS2_VIDMM_RELEASE ReleasePlacement;
    PDXGMMS2_VIDMM_SET_RANGE_STATE SetPlacementState;
    PDXGMMS2_VIDMM_QUERY_SEGMENT QuerySegmentStatus;
    PDXGMMS2_VIDMM_FIND_VICTIM FindEvictionCandidate;
    PDXGMMS2_VIDMM_RELEASE_ALL ReleaseAllPlacements;
} DXGMMS2_VIDMM_INTERFACE_V1, *PDXGMMS2_VIDMM_INTERFACE_V1;

typedef NTSTATUS (NTAPI *PDXGMMS2_QUERY_VIDMM_INTERFACE)(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _Inout_ DXGMMS2_VIDMM_INTERFACE_V1 *VidMmInterface);

typedef struct _DXGMMS2_PROVIDER_INTERFACE_V5
{
    ULONG Size;
    ULONG Version;
    ULONG ProviderFlags;
    ULONG Reserved;
    DXGMMS2_REGISTRATION_HANDLE RegistrationHandle;
    PDXGMMS2_CREATE_ADAPTER CreateAdapter;
    PDXGMMS2_START_ADAPTER StartAdapter;
    PDXGMMS2_BEGIN_STOP_ADAPTER BeginStopAdapter;
    PDXGMMS2_COMPLETE_STOP_ADAPTER CompleteStopAdapter;
    PDXGMMS2_DESTROY_ADAPTER DestroyAdapter;
    PDXGMMS2_QUERY_SCHEDULER_TIMELINE_INTERFACE QuerySchedulerTimelineInterface;
    PDXGMMS2_QUERY_CONTEXT_STREAM_INTERFACE QueryContextStreamInterface;
    PDXGMMS2_QUERY_SCHEDULER_INTERFACE QuerySchedulerInterface;
    PDXGMMS2_QUERY_VIDMM_INTERFACE QueryVidMmInterface;
} DXGMMS2_PROVIDER_INTERFACE_V5, *PDXGMMS2_PROVIDER_INTERFACE_V5;

#define DXGMMS2_PROVIDER_INTERFACE_V5_SIZE              ((ULONG)sizeof(DXGMMS2_PROVIDER_INTERFACE_V5))
#define DXGMMS2_VIDMM_INTERFACE_V1_SIZE                 ((ULONG)sizeof(DXGMMS2_VIDMM_INTERFACE_V1))

#define DXGMMS2_PROVIDER_INTERFACE_V4_SIZE              ((ULONG)sizeof(DXGMMS2_PROVIDER_INTERFACE_V4))
#define DXGMMS2_SCHEDULER_INTERFACE_V1_SIZE             ((ULONG)sizeof(DXGMMS2_SCHEDULER_INTERFACE_V1))
#define DXGMMS2_SCHEDULER_ADMIT_INFO_V1_SIZE            ((ULONG)sizeof(DXGMMS2_SCHEDULER_ADMIT_INFO_V1))
#define DXGMMS2_SCHEDULER_CLAIM_V1_SIZE                 ((ULONG)sizeof(DXGMMS2_SCHEDULER_CLAIM_V1))
#define DXGMMS2_SCHEDULER_ENGINE_STATUS_V1_SIZE         ((ULONG)sizeof(DXGMMS2_SCHEDULER_ENGINE_STATUS_V1))

#define DXGMMS2_DXGKRNL_INTERFACE_V1_SIZE             ((ULONG)sizeof(DXGMMS2_DXGKRNL_INTERFACE_V1))
#define DXGMMS2_CREATE_ADAPTER_INFO_V1_SIZE            ((ULONG)sizeof(DXGMMS2_CREATE_ADAPTER_INFO_V1))
#define DXGMMS2_START_ADAPTER_INFO_V1_SIZE             ((ULONG)sizeof(DXGMMS2_START_ADAPTER_INFO_V1))
#define DXGMMS2_START_ADAPTER_RESULT_V1_SIZE           ((ULONG)sizeof(DXGMMS2_START_ADAPTER_RESULT_V1))
#define DXGMMS2_STOP_ADAPTER_INFO_V1_SIZE              ((ULONG)sizeof(DXGMMS2_STOP_ADAPTER_INFO_V1))
#define DXGMMS2_PROVIDER_INTERFACE_V1_SIZE              ((ULONG)sizeof(DXGMMS2_PROVIDER_INTERFACE_V1))
#define DXGMMS2_PROVIDER_INTERFACE_V2_SIZE              ((ULONG)sizeof(DXGMMS2_PROVIDER_INTERFACE_V2))
#define DXGMMS2_PROVIDER_INTERFACE_V3_SIZE              ((ULONG)sizeof(DXGMMS2_PROVIDER_INTERFACE_V3))
#define DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1_SIZE    ((ULONG)sizeof(DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1))
#define DXGMMS2_FENCE_SNAPSHOT_V1_SIZE                  ((ULONG)sizeof(DXGMMS2_FENCE_SNAPSHOT_V1))
#define DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1_SIZE      ((ULONG)sizeof(DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1))
#define DXGMMS2_ADMIT_CONTEXT_WORK_V1_SIZE              ((ULONG)sizeof(DXGMMS2_ADMIT_CONTEXT_WORK_V1))
#define DXGMMS2_ADMIT_CONTEXT_WAIT_V1_SIZE              ((ULONG)sizeof(DXGMMS2_ADMIT_CONTEXT_WAIT_V1))
#define DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1_SIZE            ((ULONG)sizeof(DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1))
#define DXGMMS2_CONTEXT_ACTION_V1_SIZE                  ((ULONG)sizeof(DXGMMS2_CONTEXT_ACTION_V1))
#define DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1_SIZE      ((ULONG)sizeof(DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1))
#define DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1_SIZE         ((ULONG)sizeof(DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1))
#define DXGMMS2_CONTEXT_RETIREMENT_V1_SIZE               ((ULONG)sizeof(DXGMMS2_CONTEXT_RETIREMENT_V1))
#define DXGMMS2_CONTEXT_STREAM_INTERFACE_V1_SIZE        ((ULONG)sizeof(DXGMMS2_CONTEXT_STREAM_INTERFACE_V1))

NTSTATUS NTAPI DxgkMms2Register(_In_ const DXGMMS2_DXGKRNL_INTERFACE_V1 *DxgkrnlInterface, _Inout_ DXGMMS2_PROVIDER_INTERFACE_V1 *ProviderInterface);
NTSTATUS NTAPI DxgkMms2Unregister(_In_ DXGMMS2_REGISTRATION_HANDLE Registration);

#ifdef __cplusplus
}
#endif

#endif /* _REACTOS_DXGMMS2_H_ */
