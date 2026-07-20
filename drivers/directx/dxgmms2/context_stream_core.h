/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Deterministic per-context logical admission stream core
 */

#ifndef _DXGMMS2_CONTEXT_STREAM_CORE_H_
#define _DXGMMS2_CONTEXT_STREAM_CORE_H_

#define DXGMMS2_CONTEXT_MANAGER_SIGNATURE             'M2CG'
#define DXGMMS2_CONTEXT_SIGNATURE                     'C2CG'
#define DXGMMS2_CONTEXT_TRANSACTION_SIGNATURE         'T2CG'
#define DXGMMS2_CONTEXT_TAG                           'C2mG'
#define DXGMMS2_CONTEXT_TRANSACTION_TAG               'T2mG'

typedef enum _DXGMMS2_CONTEXT_MANAGER_STATE
{
    Dxgmms2ContextManagerCreated = 0,
    Dxgmms2ContextManagerActive = 1,
    Dxgmms2ContextManagerStopping = 2,
    Dxgmms2ContextManagerStopped = 3,
    Dxgmms2ContextManagerDestroying = 4
} DXGMMS2_CONTEXT_MANAGER_STATE;

typedef enum _DXGMMS2_CONTEXT_ENTRY_TYPE
{
    Dxgmms2ContextEntryWork = 1,
    Dxgmms2ContextEntryWait = 2,
    Dxgmms2ContextEntrySignal = 3
} DXGMMS2_CONTEXT_ENTRY_TYPE;

struct _DXGMMS2_CONTEXT_STREAM_CONTEXT;
struct _DXGMMS2_CONTEXT_TRANSACTION;

typedef struct _DXGMMS2_CONTEXT_ENTRY
{
    ULONGLONG Sequence;
    ULONGLONG ClientTag;
    ULONGLONG ObjectId;
    ULONGLONG FenceValue;
    ULONGLONG ClaimToken;
    struct _DXGMMS2_CONTEXT_TRANSACTION *Transaction;
    NTSTATUS TerminalStatus;
    ULONG Type;
    ULONG Flags;
    BOOLEAN Submitted;
    BOOLEAN Terminal;
    BOOLEAN Claimed;
    BOOLEAN CancelRequested;
    BOOLEAN WaitResolved;
    UCHAR Reserved[3];
} DXGMMS2_CONTEXT_ENTRY, *PDXGMMS2_CONTEXT_ENTRY;

typedef struct _DXGMMS2_CONTEXT_TRANSACTION_MEMBER
{
    struct _DXGMMS2_CONTEXT_STREAM_CONTEXT *Context;
    ULONGLONG Sequence;
} DXGMMS2_CONTEXT_TRANSACTION_MEMBER, *PDXGMMS2_CONTEXT_TRANSACTION_MEMBER;

typedef struct _DXGMMS2_CONTEXT_TRANSACTION
{
    ULONG Signature;
    LIST_ENTRY ManagerEntry;
    ULONGLONG TransactionId;
    ULONGLONG ClaimToken;
    ULONG Flags;
    ULONG MemberCount;
    ULONG RemainingMembers;
    BOOLEAN Claimed;
    BOOLEAN CancelRequested;
    UCHAR Reserved[2];
    DXGMMS2_CONTEXT_TRANSACTION_MEMBER Members[ANYSIZE_ARRAY];
} DXGMMS2_CONTEXT_TRANSACTION, *PDXGMMS2_CONTEXT_TRANSACTION;

typedef struct _DXGMMS2_CONTEXT_STREAM_CONTEXT
{
    ULONG Signature;
    LIST_ENTRY ManagerEntry;
    DXGMMS2_CONTEXT_STREAM_HANDLE PublicHandle;
    ULONG State;
    ULONG NodeOrdinal;
    ULONG QueueDepth;
    ULONG HeadIndex;
    ULONG QueuedEntryCount;
    ULONG ActiveClaimCount;
    ULONG CancelReason;
    ULONG RetirementHeadIndex;
    ULONG RetiredEntryCount;
    PDXGMMS2_CONTEXT_RETIREMENT_V1 Retirements;
    ULONGLONG LastAdmittedSequence;
    ULONGLONG LastSubmittedSequence;
    ULONGLONG LastCompletedSequence;
    DXGMMS2_CONTEXT_ENTRY Entries[ANYSIZE_ARRAY];
} DXGMMS2_CONTEXT_STREAM_CONTEXT, *PDXGMMS2_CONTEXT_STREAM_CONTEXT;

typedef struct _DXGMMS2_CONTEXT_STREAM_MANAGER
{
    ULONG Signature;
    KSPIN_LOCK Lock;
    ULONG State;
    ULONG Generation;
    ULONG NodeCount;
    ULONG ContextCount;
    ULONG TransactionCount;
    ULONG ActiveClaimCount;
    ULONG_PTR NextContextHandle;
    ULONGLONG NextTransactionId;
    ULONGLONG NextClaimToken;
    LIST_ENTRY ContextListHead;
    LIST_ENTRY TransactionListHead;
} DXGMMS2_CONTEXT_STREAM_MANAGER, *PDXGMMS2_CONTEXT_STREAM_MANAGER;

VOID Dxgmms2ContextStreamManagerInitialize(_Out_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager);
NTSTATUS Dxgmms2ContextStreamManagerStart(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ ULONG NodeCount);
NTSTATUS Dxgmms2ContextStreamManagerBeginStop(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ ULONG Reason);
NTSTATUS Dxgmms2ContextStreamManagerCompleteStop(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ BOOLEAN HardwareRetired);
NTSTATUS Dxgmms2ContextStreamManagerCanDestroy(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager);
NTSTATUS Dxgmms2ContextStreamManagerPrepareDestroy(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager);
ULONG Dxgmms2ContextStreamManagerGetGeneration(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager);
NTSTATUS Dxgmms2ContextStreamCreate(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ const DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1 *Info, _Out_ DXGMMS2_CONTEXT_STREAM_HANDLE *ContextHandle);
NTSTATUS Dxgmms2ContextStreamDestroy(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle);
NTSTATUS Dxgmms2ContextStreamAdmitWork(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ const DXGMMS2_ADMIT_CONTEXT_WORK_V1 *Info, _Out_ ULONGLONG *Sequence);
NTSTATUS Dxgmms2ContextStreamAdmitWait(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ const DXGMMS2_ADMIT_CONTEXT_WAIT_V1 *Info, _Out_ ULONGLONG *Sequence);
NTSTATUS Dxgmms2ContextStreamAdmitSignal(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ const DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1 *Info, _Out_ ULONGLONG *TransactionId);
NTSTATUS Dxgmms2ContextStreamResolveWait(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ ULONGLONG Sequence, _In_ NTSTATUS WaitStatus);
NTSTATUS Dxgmms2ContextStreamClaimNextAction(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _Inout_ DXGMMS2_CONTEXT_ACTION_V1 *Action);
NTSTATUS Dxgmms2ContextStreamCommitAction(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ ULONGLONG Sequence, _In_ ULONGLONG ClaimToken, _In_ NTSTATUS ActionStatus);
NTSTATUS Dxgmms2ContextStreamCompleteWork(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ ULONGLONG Sequence, _In_ NTSTATUS CompletionStatus);
NTSTATUS Dxgmms2ContextStreamCancel(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ const DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1 *Info);
NTSTATUS Dxgmms2ContextStreamQuery(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _Inout_ DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1 *Snapshot);
NTSTATUS Dxgmms2ContextStreamDrainRetirements(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _Out_writes_to_(RecordCapacity, *RetiredCount) DXGMMS2_CONTEXT_RETIREMENT_V1 *Records, _In_ ULONG RecordCapacity, _Out_ ULONG *RetiredCount);

#endif /* _DXGMMS2_CONTEXT_STREAM_CORE_H_ */
