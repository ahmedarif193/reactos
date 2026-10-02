/*
 * PROJECT:     LiberNT Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel Transaction Manager private definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

typedef union _CLS_LSN
{
    ULONGLONG ullOffset;
} CLS_LSN, *PCLS_LSN;

typedef struct _KTMOBJECT_NAMESPACE_LINK
{
    RTL_BALANCED_LINKS Links;
    BOOLEAN Expired;
} KTMOBJECT_NAMESPACE_LINK, *PKTMOBJECT_NAMESPACE_LINK;

typedef struct _KTMOBJECT_NAMESPACE
{
    RTL_AVL_TABLE Table;
    KMUTANT Mutex;
    USHORT LinksOffset;
    USHORT GuidOffset;
    BOOLEAN Expired;
} KTMOBJECT_NAMESPACE, *PKTMOBJECT_NAMESPACE;

typedef enum _KTRANSACTION_STATE
{
    KTransactionUninitialized = 0,
    KTransactionActive = 1,
    KTransactionPreparing = 2,
    KTransactionPrepared = 3,
    KTransactionInDoubt = 4,
    KTransactionCommitted = 5,
    KTransactionAborted = 6,
    KTransactionDelegated = 7,
    KTransactionPrePreparing = 8,
    KTransactionForgotten = 9,
    KTransactionRecovering = 10,
    KTransactionPrePrepared = 11
} KTRANSACTION_STATE;

typedef enum _KTRANSACTION_OUTCOME
{
    KTxOutcomeUninitialized = 0,
    KTxOutcomeUndetermined = 1,
    KTxOutcomeCommitted = 2,
    KTxOutcomeAborted = 3,
    KTxOutcomeUnavailable = 4
} KTRANSACTION_OUTCOME;

typedef enum _KENLISTMENT_STATE
{
    KEnlistmentUninitialized = 0,
    KEnlistmentActive = 256,
    KEnlistmentPreparing = 257,
    KEnlistmentPrepared = 258,
    KEnlistmentInDoubt = 259,
    KEnlistmentCommitted = 260,
    KEnlistmentCommittedNotify = 261,
    KEnlistmentCommitRequested = 262,
    KEnlistmentAborted = 263,
    KEnlistmentDelegated = 264,
    KEnlistmentDelegatedDisconnected = 265,
    KEnlistmentPrePreparing = 266,
    KEnlistmentForgotten = 267,
    KEnlistmentRecovering = 268,
    KEnlistmentAborting = 269,
    KEnlistmentReadOnly = 270,
    KEnlistmentOutcomeUnavailable = 271,
    KEnlistmentOffline = 272,
    KEnlistmentPrePrepared = 273,
    KEnlistmentInitialized = 274
} KENLISTMENT_STATE;

typedef enum _KRESOURCEMANAGER_STATE
{
    KResourceManagerUninitialized = 0,
    KResourceManagerOffline = 1,
    KResourceManagerOnline = 2
} KRESOURCEMANAGER_STATE;

typedef enum _KTM_STATE
{
    KKtmUninitialized = 0,
    KKtmInitialized = 1,
    KKtmRecovering = 2,
    KKtmOnline = 3,
    KKtmRecoveryFailed = 4,
    KKtmOffline = 5
} KTM_STATE;

typedef struct _KTRANSACTION_HISTORY
{
    ULONG RecordType;
    ULONG Payload;
} KTRANSACTION_HISTORY, *PKTRANSACTION_HISTORY;

typedef struct _KENLISTMENT_HISTORY
{
    ULONG Notification;
    KENLISTMENT_STATE NewState;
} KENLISTMENT_HISTORY, *PKENLISTMENT_HISTORY;

typedef struct _KRESOURCEMANAGER_COMPLETION_BINDING
{
    LIST_ENTRY NotificationListHead;
    PVOID Port;
    ULONG_PTR Key;
    PEPROCESS BindingProcess;
} KRESOURCEMANAGER_COMPLETION_BINDING, *PKRESOURCEMANAGER_COMPLETION_BINDING;

typedef struct _KTMNOTIFICATION_PACKET
{
    LIST_ENTRY ListEntry;
    PVOID Key;
    ULONG Notification;
    LARGE_INTEGER VirtualClock;
} KTMNOTIFICATION_PACKET, *PKTMNOTIFICATION_PACKET;

typedef struct _KTM
{
    ULONG cookie;
    KMUTANT Mutex;
    KTM_STATE State;
    KTMOBJECT_NAMESPACE_LINK NamespaceLink;
    GUID TmIdentity;
    ULONG Flags;
    ULONG VolatileFlags;
    UNICODE_STRING LogFileName;
    PFILE_OBJECT LogFileObject;
    PVOID MarshallingContext;
    PVOID LogManagementContext;
    KTMOBJECT_NAMESPACE Transactions;
    KTMOBJECT_NAMESPACE ResourceManagers;
    KMUTANT LsnOrderedMutex;
    LIST_ENTRY LsnOrderedList;
    LARGE_INTEGER CommitVirtualClock;
    FAST_MUTEX CommitVirtualClockMutex;
    CLS_LSN BaseLsn;
    CLS_LSN CurrentReadLsn;
    CLS_LSN LastRecoveredLsn;
    PVOID TmRmHandle;
    struct _KRESOURCEMANAGER *TmRm;
    KEVENT LogFullNotifyEvent;
    WORK_QUEUE_ITEM CheckpointWorkItem;
    CLS_LSN CheckpointTargetLsn;
    WORK_QUEUE_ITEM LogFullCompletedWorkItem;
    ERESOURCE LogWriteResource;
    ULONG LogFlags;
    NTSTATUS LogFullStatus;
    NTSTATUS RecoveryStatus;
    CLS_LSN LastCheckBaseLsn;
    LIST_ENTRY RestartOrderedList;
    WORK_QUEUE_ITEM OfflineWorkItem;
} KTM;

typedef struct _KRESOURCEMANAGER
{
    KEVENT NotificationAvailable;
    ULONG cookie;
    KRESOURCEMANAGER_STATE State;
    ULONG Flags;
    KMUTANT Mutex;
    KTMOBJECT_NAMESPACE_LINK NamespaceLink;
    GUID RmId;
    KQUEUE NotificationQueue;
    KMUTANT NotificationMutex;
    LIST_ENTRY EnlistmentHead;
    ULONG EnlistmentCount;
    PTM_RM_NOTIFICATION NotificationRoutine;
    PVOID Key;
    LIST_ENTRY ProtocolListHead;
    LIST_ENTRY PendingPropReqListHead;
    LIST_ENTRY CRMListEntry;
    PKTM Tm;
    UNICODE_STRING Description;
    KTMOBJECT_NAMESPACE Enlistments;
    KRESOURCEMANAGER_COMPLETION_BINDING CompletionBinding;
} KRESOURCEMANAGER;

typedef struct _KTRANSACTION
{
    KEVENT OutcomeEvent;
    ULONG cookie;
    KMUTANT Mutex;
    struct _KTRANSACTION *TreeTx;
    KTMOBJECT_NAMESPACE_LINK GlobalNamespaceLink;
    KTMOBJECT_NAMESPACE_LINK TmNamespaceLink;
    GUID UOW;
    KTRANSACTION_STATE State;
    ULONG Flags;
    LIST_ENTRY EnlistmentHead;
    ULONG EnlistmentCount;
    ULONG RecoverableEnlistmentCount;
    ULONG PrePrepareRequiredEnlistmentCount;
    ULONG PrepareRequiredEnlistmentCount;
    ULONG OutcomeRequiredEnlistmentCount;
    ULONG PendingResponses;
    struct _KENLISTMENT *SuperiorEnlistment;
    CLS_LSN LastLsn;
    LIST_ENTRY PromotedEntry;
    struct _KTRANSACTION *PromoterTransaction;
    PVOID PromotePropagation;
    ULONG IsolationLevel;
    ULONG IsolationFlags;
    LARGE_INTEGER Timeout;
    UNICODE_STRING Description;
    PKTHREAD RollbackThread;
    WORK_QUEUE_ITEM RollbackWorkItem;
    KDPC RollbackDpc;
    KTIMER RollbackTimer;
    LIST_ENTRY LsnOrderedEntry;
    KTRANSACTION_OUTCOME Outcome;
    PKTM Tm;
    LONGLONG CommitReservation;
    KTRANSACTION_HISTORY TransactionHistory[10];
    ULONG TransactionHistoryCount;
    PVOID DTCPrivateInformation;
    ULONG DTCPrivateInformationLength;
    KMUTANT DTCPrivateInformationMutex;
    PVOID PromotedTxSelfHandle;
    ULONG PendingPromotionCount;
    KEVENT PromotionCompletedEvent;
} KTRANSACTION;

typedef struct _KENLISTMENT
{
    ULONG cookie;
    KTMOBJECT_NAMESPACE_LINK NamespaceLink;
    GUID EnlistmentId;
    KMUTANT Mutex;
    LIST_ENTRY NextSameTx;
    LIST_ENTRY NextSameRm;
    PKRESOURCEMANAGER ResourceManager;
    PKTRANSACTION Transaction;
    KENLISTMENT_STATE State;
    ULONG Flags;
    ULONG NotificationMask;
    PVOID Key;
    ULONG KeyRefCount;
    PVOID RecoveryInformation;
    ULONG RecoveryInformationLength;
    PVOID DynamicNameInformation;
    ULONG DynamicNameInformationLength;
    PKTMNOTIFICATION_PACKET FinalNotification;
    struct _KENLISTMENT *SupSubEnlistment;
    PVOID SupSubEnlHandle;
    PVOID SubordinateTxHandle;
    GUID CrmEnlistmentEnId;
    GUID CrmEnlistmentTmId;
    GUID CrmEnlistmentRmId;
    ULONG NextHistory;
    KENLISTMENT_HISTORY History[20];
} KENLISTMENT;

C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KTM, NamespaceLink) == 0x48);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KTM, Transactions) == 0xb0);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KTM, CommitVirtualClock) == 0x248);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KTM, LogWriteResource) == 0x310);
C_ASSERT(sizeof(PVOID) != 8 || sizeof(KTM) == 0x3c0);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KRESOURCEMANAGER, NamespaceLink) == 0x60);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KRESOURCEMANAGER, NotificationQueue) == 0x98);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KRESOURCEMANAGER, NotificationRoutine) == 0x128);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KRESOURCEMANAGER, Enlistments) == 0x180);
C_ASSERT(sizeof(PVOID) != 8 || sizeof(KRESOURCEMANAGER) == 0x250);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KTRANSACTION, GlobalNamespaceLink) == 0x60);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KTRANSACTION, UOW) == 0xb0);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KTRANSACTION, EnlistmentHead) == 0xc8);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KTRANSACTION, RollbackTimer) == 0x1a8);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KTRANSACTION, Tm) == 0x200);
C_ASSERT(sizeof(PVOID) != 8 || sizeof(KTRANSACTION) == 0x2d8);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KENLISTMENT, EnlistmentId) == 0x30);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KENLISTMENT, ResourceManager) == 0x98);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KENLISTMENT, Key) == 0xb8);
C_ASSERT(sizeof(PVOID) != 8 || FIELD_OFFSET(KENLISTMENT, History) == 0x13c);
C_ASSERT(sizeof(PVOID) != 8 || sizeof(KENLISTMENT) == 0x1e0);

#define KTM_COOKIE              'mTmT'
#define KRESOURCEMANAGER_COOKIE 'mRmT'
#define KTRANSACTION_COOKIE     'xTmT'
#define KENLISTMENT_COOKIE      'nEmT'

#define TAG_TM '  mT'

#define KTRANSACTION_FLAG_PUMPING            0x00000001
#define KTRANSACTION_FLAG_COMMIT_REQUESTED   0x00000002
#define KTRANSACTION_FLAG_ROLLBACK_REQUESTED 0x00000004
#define KTRANSACTION_FLAG_SINGLE_PHASE       0x00000008
#define KTRANSACTION_FLAG_NO_SINGLE_PHASE    0x00000010
#define KTRANSACTION_FLAG_COMPLETED          0x00000020
#define KTRANSACTION_FLAG_TIMER              0x00000040

#define KENLISTMENT_FLAG_SUPERIOR            0x00000001
#define KENLISTMENT_FLAG_CLOSED              0x00000002
#define KENLISTMENT_FLAG_OFFLINE             0x00000004

extern POBJECT_TYPE TmTransactionManagerObjectType;
extern POBJECT_TYPE TmTransactionObjectType;
extern POBJECT_TYPE TmResourceManagerObjectType;
extern POBJECT_TYPE TmEnlistmentObjectType;

extern KTMOBJECT_NAMESPACE TmpTransactionNamespace;
extern KTMOBJECT_NAMESPACE TmpTransactionManagerNamespace;

BOOLEAN
NTAPI
TmpInitSystem(VOID);

VOID
TmpInitializeNamespace(
    _Out_ PKTMOBJECT_NAMESPACE Namespace,
    _In_ USHORT LinksOffset,
    _In_ USHORT GuidOffset);

NTSTATUS
TmpInsertNamespace(
    _Inout_ PKTMOBJECT_NAMESPACE Namespace,
    _In_ PVOID Object);

VOID
TmpRemoveNamespace(
    _Inout_ PKTMOBJECT_NAMESPACE Namespace,
    _In_ PVOID Object);

PVOID
TmpLookupNamespace(
    _Inout_ PKTMOBJECT_NAMESPACE Namespace,
    _In_ CONST GUID *Guid);

NTSTATUS
TmpEnumerateNamespace(
    _Inout_ PKTMOBJECT_NAMESPACE Namespace,
    _Inout_ PKTMOBJECT_CURSOR Cursor,
    _In_ ULONG CursorLength,
    _Out_ PULONG ReturnLength);

VOID
TmpAcquireMutex(
    _Inout_ PKMUTANT Mutex);

VOID
TmpReleaseMutex(
    _Inout_ PKMUTANT Mutex);

LARGE_INTEGER
TmpNextVirtualClock(
    _In_opt_ PKTM Tm);

NTSTATUS
TmpCaptureGuid(
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_opt_ LPGUID Guid,
    _Out_ LPGUID Captured,
    _Out_ PBOOLEAN Present);

NTSTATUS
TmpWriteHandle(
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PHANDLE HandlePointer,
    _In_ HANDLE Handle);

NTSTATUS
TmpCaptureDescription(
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_opt_ PUNICODE_STRING Source,
    _In_ ULONG MaximumCharacters,
    _Out_ PUNICODE_STRING Description);

NTSTATUS
TmpReturnInformation(
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG ReturnLength,
    _In_reads_bytes_(SourceLength) PVOID Source,
    _In_ ULONG SourceLength,
    _In_ ULONG FixedLength);

BOOLEAN
TmpPumpTransaction(
    _Inout_ PKTRANSACTION Transaction);

NTSTATUS
TmpRequestRollback(
    _Inout_ PKTRANSACTION Transaction);

VOID
TmpAbandonEnlistment(
    _In_ PKENLISTMENT Enlistment,
    _In_ ULONG Notification);

VOID
TmpAdvanceVirtualClock(
    _In_opt_ PKTM Tm,
    _In_opt_ PLARGE_INTEGER Clock);

BOOLEAN
TmpComponentFiltered(
    _In_ KPROCESSOR_MODE PreviousMode);

BOOLEAN
TmpDeliverNotification(
    _In_ PKENLISTMENT Enlistment,
    _In_ ULONG Notification);

VOID
TmpBindTransaction(
    _Inout_ PKTRANSACTION Transaction,
    _In_ PKTM Tm);

VOID NTAPI TmpDeleteTransaction(_In_ PVOID Object);
VOID NTAPI TmpCloseTransaction(_In_opt_ PEPROCESS Process, _In_ PVOID Object, _In_ ULONG_PTR ProcessHandleCount, _In_ ULONG_PTR SystemHandleCount);
VOID NTAPI TmpDeleteResourceManager(_In_ PVOID Object);
VOID NTAPI TmpCloseResourceManager(_In_opt_ PEPROCESS Process, _In_ PVOID Object, _In_ ULONG_PTR ProcessHandleCount, _In_ ULONG_PTR SystemHandleCount);
VOID NTAPI TmpDeleteEnlistment(_In_ PVOID Object);
VOID NTAPI TmpCloseEnlistment(_In_opt_ PEPROCESS Process, _In_ PVOID Object, _In_ ULONG_PTR ProcessHandleCount, _In_ ULONG_PTR SystemHandleCount);
