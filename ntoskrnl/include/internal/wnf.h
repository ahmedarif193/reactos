/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Windows Notification Facility (WNF) private definitions
 */

#pragma once

/* PUBLIC-MIRRORED TYPES *****************************************************/

typedef ULONG WNF_CHANGE_STAMP, *PWNF_CHANGE_STAMP;

typedef struct _WNF_STATE_NAME
{
    ULONG Data[2];
} WNF_STATE_NAME, *PWNF_STATE_NAME;
typedef const WNF_STATE_NAME *PCWNF_STATE_NAME;

typedef struct _WNF_TYPE_ID
{
    GUID TypeId;
} WNF_TYPE_ID, *PWNF_TYPE_ID;
typedef const WNF_TYPE_ID *PCWNF_TYPE_ID;

typedef enum _WNF_STATE_NAME_LIFETIME
{
    WnfWellKnownStateName = 0,
    WnfPermanentStateName = 1,
    WnfPersistentStateName = 2,
    WnfTemporaryStateName = 3
} WNF_STATE_NAME_LIFETIME;

typedef enum _WNF_DATA_SCOPE
{
    WnfDataScopeSystem = 0,
    WnfDataScopeSession = 1,
    WnfDataScopeUser = 2,
    WnfDataScopeProcess = 3,
    WnfDataScopeMachine = 4,
    WnfDataScopePhysicalMachine = 5
} WNF_DATA_SCOPE;

typedef enum _WNF_STATE_NAME_INFORMATION
{
    WnfInfoStateNameExist = 0,
    WnfInfoSubscribersPresent = 1,
    WnfInfoIsQuiescent = 2
} WNF_STATE_NAME_INFORMATION;

/* INTERNAL DEFINITIONS ******************************************************/

#define WNF_STATE_KEY 0x41C64E6DA3BC0074ULL
#define WNF_STATE_NAME_VERSION 1
#define WNF_MAX_STATE_SIZE 0x1000

typedef NTSTATUS
(NTAPI *PWNF_KERNEL_CALLBACK)(
    PCWNF_STATE_NAME StateName,
    WNF_CHANGE_STAMP ChangeStamp,
    PCWNF_TYPE_ID TypeId,
    PVOID CallbackContext,
    const VOID *Buffer,
    ULONG BufferSize);

typedef union _WNF_STATE_NAME_INTERNAL
{
    struct
    {
        ULONG64 Version : 4;
        ULONG64 NameLifetime : 2;
        ULONG64 DataScope : 4;
        ULONG64 PermanentData : 1;
        ULONG64 Unique : 53;
    };
    ULONG64 Value;
} WNF_STATE_NAME_INTERNAL, *PWNF_STATE_NAME_INTERNAL;

typedef struct _WNF_NAME_INSTANCE
{
    LIST_ENTRY NameListEntry;
    WNF_STATE_NAME_INTERNAL StateName;
    WNF_STATE_NAME_LIFETIME Lifetime;
    WNF_DATA_SCOPE DataScope;
    BOOLEAN PersistData;
    BOOLEAN HasTypeId;
    WNF_TYPE_ID TypeId;
    ULONG MaximumStateSize;
    WNF_CHANGE_STAMP ChangeStamp;
    PVOID DataBuffer;
    ULONG DataSize;
    PSECURITY_DESCRIPTOR SecurityDescriptor;
    LIST_ENTRY SubscriptionListHead;
    LONG SubscriptionCount;
    LONG DeliveriesInFlight;
    PEPROCESS CreatorProcess;
} WNF_NAME_INSTANCE, *PWNF_NAME_INSTANCE;

typedef struct _WNF_SUBSCRIPTION
{
    LIST_ENTRY SubscriptionListEntry;
    PWNF_NAME_INSTANCE NameInstance;
    ULONG64 SubscriptionId;
    WNF_CHANGE_STAMP ChangeStamp;
    ULONG EventMask;
    PEPROCESS Process;
    PETHREAD Thread;
    KEVENT NotificationEvent;
    PWNF_KERNEL_CALLBACK Callback;
    PVOID CallbackContext;
    WORK_QUEUE_ITEM WorkItem;
    KEVENT RundownEvent;
    LONG RefCount;
    LONG WorkQueued;
    BOOLEAN Removed;
} WNF_SUBSCRIPTION, *PWNF_SUBSCRIPTION;

/* INTERNAL PROTOTYPES *******************************************************/

CODE_SEG("INIT")
BOOLEAN
NTAPI
ExpWnfInitialization(VOID);

NTSTATUS
NTAPI
NtCreateWnfStateName(
    PWNF_STATE_NAME StateName,
    WNF_STATE_NAME_LIFETIME NameLifetime,
    WNF_DATA_SCOPE DataScope,
    BOOLEAN PersistData,
    PCWNF_TYPE_ID TypeId,
    ULONG MaximumStateSize,
    PSECURITY_DESCRIPTOR SecurityDescriptor);

NTSTATUS
NTAPI
NtDeleteWnfStateName(
    PCWNF_STATE_NAME StateName);

NTSTATUS
NTAPI
NtDeleteWnfStateData(
    PCWNF_STATE_NAME StateName,
    const VOID *ExplicitScope);

NTSTATUS
NTAPI
NtUpdateWnfStateData(
    PCWNF_STATE_NAME StateName,
    const VOID *Buffer,
    ULONG Length,
    PCWNF_TYPE_ID TypeId,
    const VOID *ExplicitScope,
    WNF_CHANGE_STAMP MatchingChangeStamp,
    LOGICAL CheckStamp);

NTSTATUS
NTAPI
NtQueryWnfStateData(
    PCWNF_STATE_NAME StateName,
    PCWNF_TYPE_ID TypeId,
    const VOID *ExplicitScope,
    PWNF_CHANGE_STAMP ChangeStamp,
    PVOID Buffer,
    PULONG BufferSize);

NTSTATUS
NTAPI
NtQueryWnfStateNameInformation(
    PCWNF_STATE_NAME StateName,
    ULONG NameInfoClass,
    const VOID *ExplicitScope,
    PVOID InfoBuffer,
    ULONG InfoBufferSize);

NTSTATUS
NTAPI
NtSubscribeWnfStateChange(
    PCWNF_STATE_NAME StateName,
    WNF_CHANGE_STAMP ChangeStamp,
    ULONG EventMask,
    PULONG64 SubscriptionId);

NTSTATUS
NTAPI
NtUnsubscribeWnfStateChange(
    PCWNF_STATE_NAME StateName);

NTSTATUS
NTAPI
ExQueryWnfStateData(
    PCWNF_STATE_NAME StateName,
    PWNF_CHANGE_STAMP ChangeStamp,
    PVOID Buffer,
    PULONG BufferSize);

NTSTATUS
NTAPI
ExSubscribeWnfStateChange(
    PVOID *SubscriptionHandle,
    PCWNF_STATE_NAME StateName,
    WNF_CHANGE_STAMP ChangeStamp,
    PWNF_KERNEL_CALLBACK Callback,
    PVOID CallbackContext);

VOID
NTAPI
ExUnsubscribeWnfStateChange(
    PVOID SubscriptionHandle);
