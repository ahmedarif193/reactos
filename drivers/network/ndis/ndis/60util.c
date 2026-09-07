/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60util.c
 * PURPOSE:     NDIS 6 utility primitives.
 *
 *              General-purpose primitives every NDIS 6 driver expects from
 *              ndis.sys but that don't fit cleanly into the lifecycle
 *              (60adapter.c), datapath (60thunk_*.c), or registration
 *              (60driver.c, 60stubs.c) files:
 *
 *                - Timer object API (NdisAllocateTimerObject etc.) —
 *                  KTIMER + KDPC wrapper used for periodic work
 *                - RW lock API (NdisAllocateRWLock etc.) — shared/exclusive
 *                  synchronization primitive on top of EX_PUSH_LOCK
 *              The legacy DDK header doesn't carry these declarations
 *              (NDIS_TIMER_CHARACTERISTICS, NDIS_RW_LOCK_EX, etc.), so
 *              ndis6_internal.h declares the prototypes locally and the
 *              .spec file exports them.
 *
 *              Created on the dev-nt6-1 branch as part of the NDIS 6.20
 *              utility surface (Phase 9 of the bridge plan).
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"

#define NDIS6_TIMER_TAG     'tTNn'  /* "nNTt" */
#define NDIS6_RWLOCK_TAG    'lWNn'  /* "nNWl" */
#define NDIS6_MAX_PORT_NUMBER 0x00ffffff

#define NDIS6_RESET_IDLE       0
#define NDIS6_RESET_QUEUED     1
#define NDIS6_RESET_PENDING    2
#define NDIS6_RESET_COMPLETING 3

/* ============================================================================
 *  NDIS 6 timer object
 *
 *  An NDIS 6 timer object is a KTIMER + KDPC pair plus the driver's
 *  NDIS_TIMER_FUNCTION callback and a per-callback context. The bridge
 *  wraps it in a private struct that the driver receives as an opaque
 *  NDIS_HANDLE; on every Set/Cancel call we round-trip back through the
 *  handle to find the underlying kernel objects.
 * ============================================================================ */

typedef VOID (NTAPI *PNDIS6_TIMER_CALLBACK)(
    PVOID SystemSpecific1,
    PVOID FunctionContext,
    PVOID SystemSpecific2,
    PVOID SystemSpecific3);

typedef struct _NDIS6_TIMER_OBJECT
{
    ULONG                    Magic;
    KTIMER                   Timer;
    KDPC                     Dpc;
    PNDIS6_TIMER_CALLBACK    Function;
    PVOID                    DefaultFunctionContext;
    PVOID                    FunctionContext;
    LONG                     PeriodMs;
} NDIS6_TIMER_OBJECT, *PNDIS6_TIMER_OBJECT;

#define NDIS6_TIMER_MAGIC   0xB16CB16D

static VOID NTAPI
Ndis6TimerDpcWrapper(
    _In_     PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PNDIS6_TIMER_OBJECT TimerObject = (PNDIS6_TIMER_OBJECT)DeferredContext;

    if (TimerObject == NULL || TimerObject->Magic != NDIS6_TIMER_MAGIC ||
        TimerObject->Function == NULL)
    {
        return;
    }

    /* NDIS_TIMER_FUNCTION signature is
     *   (SystemSpecific1, FunctionContext, SystemSpecific2, SystemSpecific3)
     * We pass the KDPC as SystemSpecific1 (matches Windows convention),
     * the user's stored FunctionContext as the second arg, and the
     * system DPC arguments through unchanged. */
    TimerObject->Function(
        Dpc,
        TimerObject->FunctionContext,
        SystemArgument1,
        SystemArgument2);
}

NDIS_STATUS
NTAPI
NdisAllocateTimerObject(
    _In_opt_ NDIS_HANDLE                  NdisHandle,
    _In_     PNDIS_TIMER_CHARACTERISTICS  TimerCharacteristics,
    _Out_    PNDIS_HANDLE                 pTimerObject)
{
    PNDIS6_TIMER_OBJECT TimerObject;

    UNREFERENCED_PARAMETER(NdisHandle);

    if (TimerCharacteristics == NULL || pTimerObject == NULL ||
        TimerCharacteristics->TimerFunction == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    *pTimerObject = NULL;

    TimerObject = (PNDIS6_TIMER_OBJECT)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_TIMER_OBJECT), NDIS6_TIMER_TAG);
    if (TimerObject == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(TimerObject, sizeof(*TimerObject));
    TimerObject->Magic           = NDIS6_TIMER_MAGIC;
    TimerObject->Function        = (PNDIS6_TIMER_CALLBACK)TimerCharacteristics->TimerFunction;
    TimerObject->DefaultFunctionContext = TimerCharacteristics->FunctionContext;
    TimerObject->FunctionContext = TimerCharacteristics->FunctionContext;
    TimerObject->PeriodMs        = 0;

    KeInitializeTimer(&TimerObject->Timer);
    KeInitializeDpc(&TimerObject->Dpc, Ndis6TimerDpcWrapper, TimerObject);

    *pTimerObject = (NDIS_HANDLE)TimerObject;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisFreeTimerObject(
    _In_ NDIS_HANDLE TimerObject)
{
    PNDIS6_TIMER_OBJECT t = (PNDIS6_TIMER_OBJECT)TimerObject;

    if (t == NULL || t->Magic != NDIS6_TIMER_MAGIC)
        return;

    /* Cancel any pending fire and flush queued DPCs. */
    KeCancelTimer(&t->Timer);
    KeRemoveQueueDpc(&t->Dpc);

    t->Magic = 0;
    ExFreePoolWithTag(t, NDIS6_TIMER_TAG);
}

BOOLEAN
NTAPI
NdisSetTimerObject(
    _In_     NDIS_HANDLE   TimerObject,
    _In_     LARGE_INTEGER DueTime,
    _In_opt_ LONG          MillisecondsPeriod,
    _In_opt_ PVOID         FunctionContext)
{
    PNDIS6_TIMER_OBJECT t = (PNDIS6_TIMER_OBJECT)TimerObject;
    BOOLEAN WasInQueue;

    if (t == NULL || t->Magic != NDIS6_TIMER_MAGIC)
        return FALSE;

    /* NULL selects the context registered at allocation time. */
    t->FunctionContext = FunctionContext != NULL
        ? FunctionContext
        : t->DefaultFunctionContext;

    t->PeriodMs = MillisecondsPeriod;

    if (MillisecondsPeriod > 0)
    {
        WasInQueue = KeSetTimerEx(&t->Timer, DueTime,
                                  MillisecondsPeriod, &t->Dpc);
    }
    else
    {
        WasInQueue = KeSetTimer(&t->Timer, DueTime, &t->Dpc);
    }

    return WasInQueue;
}

BOOLEAN
NTAPI
NdisSetCoalescableTimerObject(
    _In_     NDIS_HANDLE   TimerObject,
    _In_     LARGE_INTEGER DueTime,
    _In_opt_ LONG          MillisecondsPeriod,
    _In_opt_ PVOID         FunctionContext,
    _In_     ULONG         Tolerance)
{
    PNDIS6_TIMER_OBJECT t = (PNDIS6_TIMER_OBJECT)TimerObject;

    if (t == NULL || t->Magic != NDIS6_TIMER_MAGIC)
        return FALSE;

    t->FunctionContext = FunctionContext != NULL
        ? FunctionContext
        : t->DefaultFunctionContext;

    t->PeriodMs = MillisecondsPeriod;
    return KeSetCoalescableTimer(&t->Timer,
                                 DueTime,
                                 MillisecondsPeriod > 0 ? MillisecondsPeriod : 0,
                                 Tolerance,
                                 &t->Dpc);
}

BOOLEAN
NTAPI
NdisCancelTimerObject(
    _In_ NDIS_HANDLE TimerObject)
{
    PNDIS6_TIMER_OBJECT t = (PNDIS6_TIMER_OBJECT)TimerObject;

    if (t == NULL || t->Magic != NDIS6_TIMER_MAGIC)
        return FALSE;

    return KeCancelTimer(&t->Timer);
}

/* ============================================================================
 *  NDIS miniport ports
 * ============================================================================ */

typedef struct _NDIS6_PORT_TRANSITION
{
    PNDIS6_PORT_ENTRY Entry;
    PNDIS_PORT InputPort;
    NDIS_PORT_NUMBER PortNumber;
    NDIS_PORT_CHARACTERISTICS OldCharacteristics;
} NDIS6_PORT_TRANSITION, *PNDIS6_PORT_TRANSITION;

static PNDIS6_ADAPTER_EXT
Ndis6ExtFromMiniportHandle(
    _In_ NDIS_HANDLE NdisMiniportHandle)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NULL;

    return NDIS6_EXT(Adapter);
}

static PNDIS6_PORT_ENTRY
Ndis6FindPortLocked(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ NDIS_PORT_NUMBER PortNumber)
{
    PLIST_ENTRY Link;

    for (Link = Ext->PortList.Flink;
         Link != &Ext->PortList;
         Link = Link->Flink)
    {
        PNDIS6_PORT_ENTRY Entry =
            CONTAINING_RECORD(Link, NDIS6_PORT_ENTRY, ListEntry);

        if (Entry->Characteristics.PortNumber == PortNumber)
            return Entry;
    }

    return NULL;
}

NDIS_STATUS
NTAPI
NdisMAllocatePort(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _Inout_ PNDIS_PORT_CHARACTERISTICS PortCharacteristics)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_PORT_ENTRY Entry;
    NDIS_PORT_NUMBER Candidate;
    ULONG Attempts;
    KIRQL OldIrql;

    if (PortCharacteristics == NULL ||
        PortCharacteristics->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
        PortCharacteristics->Header.Revision != NDIS_PORT_CHARACTERISTICS_REVISION_1 ||
        PortCharacteristics->Header.Size < NDIS_SIZEOF_PORT_CHARACTERISTICS_REVISION_1 ||
        (PortCharacteristics->Flags & ~NDIS_PORT_CHAR_USE_DEFAULT_AUTH_SETTINGS) != 0)
    {
        return NDIS_STATUS_INVALID_DATA;
    }

    Ext = Ndis6ExtFromMiniportHandle(NdisMiniportHandle);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (!Ndis6ReferenceAdapterLifecycle(Adapter))
        return NDIS_STATUS_CLOSING;

    if (!Ext->RegistrationAttrsValid || Ext->ProtocolBindingsClosing)
    {
        Ndis6DereferenceAdapterLifecycle(Adapter);
        return Ext->ProtocolBindingsClosing
            ? NDIS_STATUS_CLOSING
            : NDIS_STATUS_INVALID_DATA;
    }

    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(*Entry),
                                  NDIS6_PORT_TAG);
    if (Entry == NULL)
    {
        Ndis6DereferenceAdapterLifecycle(Adapter);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(Entry, sizeof(*Entry));
    RtlCopyMemory(&Entry->Characteristics,
                  PortCharacteristics,
                  NDIS_SIZEOF_PORT_CHARACTERISTICS_REVISION_1);
    if (Entry->Characteristics.Flags & NDIS_PORT_CHAR_USE_DEFAULT_AUTH_SETTINGS)
    {
        Entry->Characteristics.SendControlState = Ext->DefaultPortAuthStates.SendControlState;
        Entry->Characteristics.RcvControlState = Ext->DefaultPortAuthStates.RcvControlState;
        Entry->Characteristics.SendAuthorizationState = Ext->DefaultPortAuthStates.SendAuthorizationState;
        Entry->Characteristics.RcvAuthorizationState = Ext->DefaultPortAuthStates.RcvAuthorizationState;
    }

    KeAcquireSpinLock(&Ext->PortListLock, &OldIrql);
    Candidate = Ext->NextPortNumber;
    for (Attempts = 0; Attempts < NDIS6_MAX_PORT_NUMBER; Attempts++)
    {
        if (Candidate == 0 || Candidate > NDIS6_MAX_PORT_NUMBER)
            Candidate = 1;

        if (Ndis6FindPortLocked(Ext, Candidate) == NULL)
            break;

        Candidate++;
    }

    if (Attempts == NDIS6_MAX_PORT_NUMBER)
    {
        KeReleaseSpinLock(&Ext->PortListLock, OldIrql);
        ExFreePoolWithTag(Entry, NDIS6_PORT_TAG);
        Ndis6DereferenceAdapterLifecycle(Adapter);
        return NDIS_STATUS_RESOURCES;
    }

    Entry->Characteristics.PortNumber = Candidate;
    InsertTailList(&Ext->PortList, &Entry->ListEntry);
    Ext->NextPortNumber = Candidate == NDIS6_MAX_PORT_NUMBER ? 1 : Candidate + 1;
    KeReleaseSpinLock(&Ext->PortListLock, OldIrql);

    PortCharacteristics->PortNumber = Candidate;
    Ndis6DereferenceAdapterLifecycle(Adapter);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NdisMFreePort(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_PORT_NUMBER PortNumber)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_PORT_ENTRY Entry;
    KIRQL OldIrql;

    if (PortNumber == NDIS_DEFAULT_PORT_NUMBER ||
        PortNumber > NDIS6_MAX_PORT_NUMBER)
    {
        return NDIS_STATUS_INVALID_PORT;
    }

    Ext = Ndis6ExtFromMiniportHandle(NdisMiniportHandle);
    if (Ext == NULL || !Ndis6ReferenceAdapterLifecycle(Adapter))
        return NDIS_STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&Ext->PortListLock, &OldIrql);
    Entry = Ndis6FindPortLocked(Ext, PortNumber);
    if (Entry == NULL)
    {
        KeReleaseSpinLock(&Ext->PortListLock, OldIrql);
        Ndis6DereferenceAdapterLifecycle(Adapter);
        return NDIS_STATUS_INVALID_PORT;
    }

    if (Entry->Active || Entry->Transitioning)
    {
        KeReleaseSpinLock(&Ext->PortListLock, OldIrql);
        Ndis6DereferenceAdapterLifecycle(Adapter);
        return NDIS_STATUS_INVALID_PORT_STATE;
    }

    RemoveEntryList(&Entry->ListEntry);
    KeReleaseSpinLock(&Ext->PortListLock, OldIrql);

    ExFreePoolWithTag(Entry, NDIS6_PORT_TAG);
    Ndis6DereferenceAdapterLifecycle(Adapter);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Ndis6TransitionPorts(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ PNET_PNP_EVENT_NOTIFICATION Notification,
    _In_ BOOLEAN Activate)
{
    PNDIS6_PORT_TRANSITION Transitions;
    PNDIS_PORT Port;
    PNDIS_PORT_NUMBER PortNumbers;
    SIZE_T Count = 0;
    SIZE_T Index;
    SIZE_T Previous;
    KIRQL OldIrql;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    if (Notification->NetPnPEvent.Buffer == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Activate)
    {
        for (Port = (PNDIS_PORT)Notification->NetPnPEvent.Buffer;
             Port != NULL;
             Port = Port->Next)
        {
            if (++Count > NDIS6_MAX_PORT_NUMBER)
                return NDIS_STATUS_INVALID_LENGTH;
        }

        if (Count == 0 || Count > MAXULONG_PTR / sizeof(NDIS_PORT) ||
            Notification->NetPnPEvent.BufferLength < Count * sizeof(NDIS_PORT))
        {
            return NDIS_STATUS_INVALID_LENGTH;
        }
    }
    else
    {
        if (Notification->NetPnPEvent.BufferLength == 0 ||
            (Notification->NetPnPEvent.BufferLength % sizeof(NDIS_PORT_NUMBER)) != 0)
        {
            return NDIS_STATUS_INVALID_LENGTH;
        }

        Count = Notification->NetPnPEvent.BufferLength / sizeof(NDIS_PORT_NUMBER);
    }

    if (Count > MAXULONG_PTR / sizeof(*Transitions))
        return NDIS_STATUS_RESOURCES;

    Transitions = ExAllocatePoolWithTag(NonPagedPool,
                                        Count * sizeof(*Transitions),
                                        NDIS6_PORT_TAG);
    if (Transitions == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Transitions, Count * sizeof(*Transitions));
    Port = Activate ? (PNDIS_PORT)Notification->NetPnPEvent.Buffer : NULL;
    PortNumbers = Activate ? NULL : (PNDIS_PORT_NUMBER)Notification->NetPnPEvent.Buffer;

    KeAcquireSpinLock(&Ext->PortListLock, &OldIrql);
    for (Index = 0; Index < Count; Index++)
    {
        NDIS_PORT_NUMBER PortNumber;
        PNDIS6_PORT_ENTRY Entry;

        if (Activate)
        {
            if (Port->PortCharacteristics.Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
                Port->PortCharacteristics.Header.Revision != NDIS_PORT_CHARACTERISTICS_REVISION_1 ||
                Port->PortCharacteristics.Header.Size < NDIS_SIZEOF_PORT_CHARACTERISTICS_REVISION_1)
            {
                Status = NDIS_STATUS_INVALID_PARAMETER;
                break;
            }

            PortNumber = Port->PortCharacteristics.PortNumber;
        }
        else
        {
            PortNumber = PortNumbers[Index];
        }

        Entry = PortNumber == NDIS_DEFAULT_PORT_NUMBER
            ? NULL
            : Ndis6FindPortLocked(Ext, PortNumber);

        if (PortNumber > NDIS6_MAX_PORT_NUMBER ||
            (PortNumber != NDIS_DEFAULT_PORT_NUMBER && Entry == NULL))
        {
            Status = NDIS_STATUS_INVALID_PORT;
            break;
        }

        if (PortNumber == NDIS_DEFAULT_PORT_NUMBER)
        {
            if (Ext->DefaultPortTransitioning ||
                Ext->DefaultPortActive == Activate)
            {
                Status = NDIS_STATUS_INVALID_PORT_STATE;
                break;
            }
        }
        else if (Entry->Transitioning || Entry->Active == Activate)
        {
            Status = NDIS_STATUS_INVALID_PORT_STATE;
            break;
        }

        for (Previous = 0; Previous < Index; Previous++)
        {
            if (Transitions[Previous].PortNumber == PortNumber)
            {
                Status = NDIS_STATUS_INVALID_PORT;
                break;
            }
        }
        if (Status != NDIS_STATUS_SUCCESS)
            break;

        Transitions[Index].Entry = Entry;
        Transitions[Index].InputPort = Port;
        Transitions[Index].PortNumber = PortNumber;
        if (Entry != NULL)
            Transitions[Index].OldCharacteristics = Entry->Characteristics;
        if (Activate)
            Port = Port->Next;
    }

    if (Status == NDIS_STATUS_SUCCESS)
    {
        for (Index = 0; Index < Count; Index++)
        {
            if (Transitions[Index].Entry != NULL)
            {
                Transitions[Index].Entry->Transitioning = TRUE;
                Transitions[Index].Entry->Active = Activate;
                if (Activate)
                {
                    RtlCopyMemory(&Transitions[Index].Entry->Characteristics,
                                  &Transitions[Index].InputPort->PortCharacteristics,
                                  NDIS_SIZEOF_PORT_CHARACTERISTICS_REVISION_1);
                    if (Transitions[Index].Entry->Characteristics.Flags &
                        NDIS_PORT_CHAR_USE_DEFAULT_AUTH_SETTINGS)
                    {
                        Transitions[Index].Entry->Characteristics.SendControlState = Ext->DefaultPortAuthStates.SendControlState;
                        Transitions[Index].Entry->Characteristics.RcvControlState = Ext->DefaultPortAuthStates.RcvControlState;
                        Transitions[Index].Entry->Characteristics.SendAuthorizationState = Ext->DefaultPortAuthStates.SendAuthorizationState;
                        Transitions[Index].Entry->Characteristics.RcvAuthorizationState = Ext->DefaultPortAuthStates.RcvAuthorizationState;
                    }
                }
            }
            else
            {
                Ext->DefaultPortTransitioning = TRUE;
                Ext->DefaultPortActive = Activate;
            }
        }
    }
    KeReleaseSpinLock(&Ext->PortListLock, OldIrql);

    if (Status == NDIS_STATUS_SUCCESS)
        Status = Ndis6FilterDispatchNetPnPEvent(Ext->Adapter, Notification);

    if (Index == Count)
    {
        KeAcquireSpinLock(&Ext->PortListLock, &OldIrql);
        for (Index = 0; Index < Count; Index++)
        {
            if (Transitions[Index].Entry != NULL)
            {
                if (Status != NDIS_STATUS_SUCCESS)
                {
                    Transitions[Index].Entry->Active = !Activate;
                    if (Activate)
                        Transitions[Index].Entry->Characteristics =
                            Transitions[Index].OldCharacteristics;
                }
                Transitions[Index].Entry->Transitioning = FALSE;
            }
            else
            {
                if (Status != NDIS_STATUS_SUCCESS)
                    Ext->DefaultPortActive = !Activate;
                Ext->DefaultPortTransitioning = FALSE;
            }
        }
        KeReleaseSpinLock(&Ext->PortListLock, OldIrql);
    }

    ExFreePoolWithTag(Transitions, NDIS6_PORT_TAG);
    return Status;
}

NDIS_STATUS
NTAPI
NdisMNetPnPEvent(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNDIS6_ADAPTER_EXT Ext;
    NDIS_STATUS Status;

    if (NetPnPEventNotification == NULL ||
        NetPnPEventNotification->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
        NetPnPEventNotification->Header.Revision < NET_PNP_EVENT_NOTIFICATION_REVISION_1 ||
        NetPnPEventNotification->Header.Size < NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_1 ||
        KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Ext = Ndis6ExtFromMiniportHandle(NdisMiniportHandle);
    if (Ext == NULL || !Ndis6ReferenceAdapterLifecycle(Adapter))
        return NDIS_STATUS_INVALID_PARAMETER;

    if (!Ext->RegistrationAttrsValid || Ext->ProtocolBindingsClosing)
    {
        Ndis6DereferenceAdapterLifecycle(Adapter);
        return NDIS_STATUS_INVALID_STATE;
    }

    switch (NetPnPEventNotification->NetPnPEvent.NetEvent)
    {
        case NetEventPortActivation:
            Status = Ndis6TransitionPorts(Ext, NetPnPEventNotification, TRUE);
            break;

        case NetEventPortDeactivation:
            Status = Ndis6TransitionPorts(Ext, NetPnPEventNotification, FALSE);
            break;

        case NetEventQueryPower:
        case NetEventQueryRemoveDevice:
        case NetEventCancelRemoveDevice:
        case NetEventPnPCapabilities:
            Status = Ndis6FilterDispatchNetPnPEvent(Adapter,
                                                    NetPnPEventNotification);
            break;

        default:
            Status = NDIS_STATUS_SUCCESS;
            break;
    }

    Ndis6DereferenceAdapterLifecycle(Adapter);
    return Status;
}

VOID
Ndis6FreePorts(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    PNDIS6_PORT_ENTRY Entry;
    KIRQL OldIrql;

    if (Ext == NULL)
        return;

    for (;;)
    {
        KeAcquireSpinLock(&Ext->PortListLock, &OldIrql);
        if (IsListEmpty(&Ext->PortList))
        {
            KeReleaseSpinLock(&Ext->PortListLock, OldIrql);
            break;
        }

        Entry = CONTAINING_RECORD(RemoveHeadList(&Ext->PortList),
                                  NDIS6_PORT_ENTRY,
                                  ListEntry);
        KeReleaseSpinLock(&Ext->PortListLock, OldIrql);
        ExFreePoolWithTag(Entry, NDIS6_PORT_TAG);
    }
}

/* ============================================================================
 *  Miniport-requested reset
 * ============================================================================ */

VOID
Ndis6ResetMiniportComplete(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NDIS_STATUS Status,
    _In_ BOOLEAN AddressingReset)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || !Adapter->IsNdis6 ||
        (Ext = NDIS6_EXT(Adapter)) == NULL)
    {
        return;
    }

    if (InterlockedCompareExchange(&Ext->ResetState,
                                   NDIS6_RESET_COMPLETING,
                                   NDIS6_RESET_PENDING) != NDIS6_RESET_PENDING)
    {
        return;
    }

    MiniResetComplete(Adapter, Status, AddressingReset);
    ExReleaseRundownProtection(&Ext->LifecycleRundown);

    if (InterlockedCompareExchange(&Ext->ResetWorkerActive, 0, 0) == 0)
    {
        InterlockedCompareExchange(&Ext->ResetState,
                                   NDIS6_RESET_IDLE,
                                   NDIS6_RESET_COMPLETING);
    }
}

static VOID NTAPI
Ndis6ResetWorker(
    _In_ PVOID Context)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)Context;
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);
    NDIS_STATUS Status;
    BOOLEAN AddressingReset = FALSE;
    KIRQL OldIrql;

    InterlockedExchange(&Ext->ResetWorkerActive, 1);
    InterlockedExchange(&Ext->ResetState, NDIS6_RESET_PENDING);

    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    Adapter->NdisMiniportBlock.ResetStatus = NDIS_STATUS_PENDING;
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

    NdisMIndicateStatus(Adapter, NDIS_STATUS_RESET_START, NULL, 0);
    NdisMIndicateStatusComplete(Adapter);

    Status = Ndis6CallResetHandlerEx(Adapter, &AddressingReset);
    if (Status != NDIS_STATUS_PENDING)
        Ndis6ResetMiniportComplete(Adapter, Status, AddressingReset);

    InterlockedExchange(&Ext->ResetWorkerActive, 0);
    InterlockedCompareExchange(&Ext->ResetState,
                               NDIS6_RESET_IDLE,
                               NDIS6_RESET_COMPLETING);
    ExReleaseRundownProtection(&Ext->LifecycleRundown);
}

NDIS_STATUS
Ndis6QueueMiniportReset(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || !Adapter->IsNdis6 ||
        (Ext = NDIS6_EXT(Adapter)) == NULL ||
        Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.ResetHandlerEx == NULL)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* The first reference owns the worker; the second owns the reset until
     * its synchronous or asynchronous completion path runs. */
    if (!ExAcquireRundownProtection(&Ext->LifecycleRundown))
        return NDIS_STATUS_CLOSING;

    if (!ExAcquireRundownProtection(&Ext->LifecycleRundown))
    {
        ExReleaseRundownProtection(&Ext->LifecycleRundown);
        return NDIS_STATUS_CLOSING;
    }

    if (InterlockedCompareExchange(&Ext->ResetState,
                                   NDIS6_RESET_QUEUED,
                                   NDIS6_RESET_IDLE) != NDIS6_RESET_IDLE)
    {
        ExReleaseRundownProtection(&Ext->LifecycleRundown);
        ExReleaseRundownProtection(&Ext->LifecycleRundown);
        return NDIS_STATUS_RESET_IN_PROGRESS;
    }

    ExInitializeWorkItem(&Ext->ResetWorkItem, Ndis6ResetWorker, Adapter);
    ExQueueWorkItem(&Ext->ResetWorkItem, DelayedWorkQueue);
    return NDIS_STATUS_PENDING;
}

VOID
NTAPI
NdisMResetMiniport(
    _In_ NDIS_HANDLE NdisMiniportHandle)
{
    Ndis6QueueMiniportReset((PLOGICAL_ADAPTER)NdisMiniportHandle);
}

/* ============================================================================
 *  Status and interface-identity helpers used by NetAdapterCx.
 * ============================================================================ */

NTSTATUS
NTAPI
NdisConvertNdisStatusToNtStatus(
    _In_ NDIS_STATUS Status)
{
    /* Modern NDIS status values used by NetAdapterCx are NTSTATUS values.
     * Keep the conversion explicit so legacy 0xC001xxxx values do not get
     * silently reported as success. */
    switch (Status)
    {
        case NDIS_STATUS_CLOSING:
        case NDIS_STATUS_ADAPTER_NOT_READY:
            return STATUS_DEVICE_NOT_READY;

        case NDIS_STATUS_BAD_VERSION:
        case NDIS_STATUS_BAD_CHARACTERISTICS:
            return STATUS_REVISION_MISMATCH;

        case NDIS_STATUS_ADAPTER_NOT_FOUND:
            return STATUS_NO_SUCH_DEVICE;

        case NDIS_STATUS_INVALID_LENGTH:
        case NDIS_STATUS_BUFFER_TOO_SHORT:
            return STATUS_BUFFER_TOO_SMALL;

        case NDIS_STATUS_INVALID_DATA:
        case NDIS_STATUS_INVALID_OID:
            return STATUS_INVALID_PARAMETER;

        default:
            return (NTSTATUS)Status;
    }
}

NDIS_STATUS
NTAPI
NdisConvertNtStatusToNdisStatus(
    _In_ NTSTATUS Status)
{
    return (NDIS_STATUS)Status;
}

NDIS_STATUS
NTAPI
NdisIfGetInterfaceIndexFromNetLuid(
    _In_ NET_LUID NetLuid,
    _Out_ PNET_IFINDEX IfIndex)
{
    if (IfIndex == NULL || NetLuid.Info.NetLuidIndex == 0)
        return NDIS_STATUS_INTERFACE_NOT_FOUND;

    *IfIndex = (NET_IFINDEX)NetLuid.Info.NetLuidIndex;
    return NDIS_STATUS_SUCCESS;
}

/* ============================================================================
 *  NDIS 6 RW lock
 *
 *  Wraps ERESOURCE which gives us shared/exclusive semantics. The
 *  driver-visible PNDIS_RW_LOCK_EX is the bridge's wrapper; LOCK_STATE_EX
 *  is used to remember whether the caller acquired shared or exclusive
 *  so the matching Release path runs.
 * ============================================================================ */

struct _NDIS_RW_LOCK_EX
{
    ULONG       Magic;
    ERESOURCE   Resource;
};

#define NDIS6_RWLOCK_MAGIC  0xB16CB16E

PNDIS_RW_LOCK_EX
NTAPI
NdisAllocateRWLock(
    _In_opt_ NDIS_HANDLE NdisHandle)
{
    PNDIS_RW_LOCK_EX Lock;

    UNREFERENCED_PARAMETER(NdisHandle);

    Lock = (PNDIS_RW_LOCK_EX)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(*Lock), NDIS6_RWLOCK_TAG);
    if (Lock == NULL)
        return NULL;

    Lock->Magic = NDIS6_RWLOCK_MAGIC;
    ExInitializeResourceLite(&Lock->Resource);
    return Lock;
}

VOID
NTAPI
NdisFreeRWLock(
    _In_ PNDIS_RW_LOCK_EX Lock)
{
    if (Lock == NULL || Lock->Magic != NDIS6_RWLOCK_MAGIC)
        return;
    ExDeleteResourceLite(&Lock->Resource);
    Lock->Magic = 0;
    ExFreePoolWithTag(Lock, NDIS6_RWLOCK_TAG);
}

VOID
NTAPI
NdisAcquireRWLockRead(
    _In_  PNDIS_RW_LOCK_EX Lock,
    _Out_ PLOCK_STATE_EX   LockState,
    _In_  UCHAR            Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    if (Lock == NULL || Lock->Magic != NDIS6_RWLOCK_MAGIC || LockState == NULL)
        return;

    /* ERESOURCE requires APC disabled before acquire. */
    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&Lock->Resource, TRUE);

    LockState->Reserved[0] = (PVOID)(ULONG_PTR)1;  /* "shared" */
    LockState->Reserved[1] = NULL;
}

VOID
NTAPI
NdisAcquireRWLockWrite(
    _In_  PNDIS_RW_LOCK_EX Lock,
    _Out_ PLOCK_STATE_EX   LockState,
    _In_  UCHAR            Flags)
{
    UNREFERENCED_PARAMETER(Flags);

    if (Lock == NULL || Lock->Magic != NDIS6_RWLOCK_MAGIC || LockState == NULL)
        return;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&Lock->Resource, TRUE);

    LockState->Reserved[0] = (PVOID)(ULONG_PTR)2;  /* "exclusive" */
    LockState->Reserved[1] = NULL;
}

VOID
NTAPI
NdisReleaseRWLock(
    _In_ PNDIS_RW_LOCK_EX Lock,
    _In_ PLOCK_STATE_EX   LockState)
{
    if (Lock == NULL || Lock->Magic != NDIS6_RWLOCK_MAGIC || LockState == NULL)
        return;

    ExReleaseResourceLite(&Lock->Resource);
    KeLeaveCriticalRegion();

    LockState->Reserved[0] = NULL;
}

/* ============================================================================
 *  E3: miscellaneous utility APIs
 *
 *  NdisMSleep and NdisGetCurrentProcessorCounts are already implemented
 *  in the legacy NDIS 5 library (miniport.c / misc.c) and callable from
 *  NDIS 6 drivers unchanged — nothing extra needed here.
 *
 *  NdisGetSystemUpTimeEx is new (NDIS 6.0+) and returns a LARGE_INTEGER
 *  in 100ns units since boot. Wraps KeQueryTickCount * KeQueryTimeIncrement.
 * ============================================================================ */

VOID
NTAPI
NdisGetSystemUpTimeEx(
    _Out_ PLARGE_INTEGER pSystemUpTime)
{
    LARGE_INTEGER TickCount;
    if (pSystemUpTime == NULL)
        return;
    KeQueryTickCount(&TickCount);
    /* TickCount is in units of KeQueryTimeIncrement (100ns). The
     * result is a straight 100ns-since-boot value. */
    pSystemUpTime->QuadPart = TickCount.QuadPart * KeQueryTimeIncrement();
}

/* ============================================================================
 *  E4: NDIS 6.30+ polling API — NdisRegisterPoll / NdisMPollComplete
 *
 *  The polling API lets a driver run its RX/TX work via a kernel poll
 *  callback instead of DPC-based dispatch. We model each registered poll
 *  as a KDPC that the driver can request via NdisRequestPoll; the DPC
 *  calls back into the driver's NDIS_POLL routine, which does work and
 *  optionally re-requests.
 *
 *  This is a minimal functional implementation — no NUMA affinity, no
 *  budget tracking, no separate DPC queue priority. Sufficient for
 *  drivers that opt-in but not full-performance poll mode.
 * ============================================================================ */

typedef VOID (NTAPI *PNDIS6_POLL_HANDLER)(
    _In_ PVOID PollContext,
    _In_ PVOID Parameters);

typedef struct _NDIS6_POLL_CONTEXT
{
    ULONG                   Magic;
    KDPC                    Dpc;
    PNDIS6_POLL_HANDLER     PollHandler;
    PVOID                   PollContext;
    LONG                    RequestCount;       /* atomic: pending requests */
    BOOLEAN                 Unregistered;
} NDIS6_POLL_CONTEXT, *PNDIS6_POLL_CONTEXT;

#define NDIS6_POLL_MAGIC   0x504F4C4CU  /* 'POLL' */
#define NDIS6_POLL_TAG     'PlNn'

static VOID NTAPI
Ndis6PollDpcRoutine(
    _In_     PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PNDIS6_POLL_CONTEXT Poll = (PNDIS6_POLL_CONTEXT)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (Poll == NULL || Poll->Magic != NDIS6_POLL_MAGIC || Poll->Unregistered)
        return;

    if (Poll->PollHandler != NULL)
    {
        /* Windows passes a NDIS_POLL_PARAMETERS struct. Most driver-side
         * NDIS_POLL routines tolerate NULL (they only look at
         * Parameters->MaxNblsToIndicate and fall through to the "no
         * limit" path when it's 0). */
        Poll->PollHandler(Poll->PollContext, NULL);
    }

    /* If the driver re-requested a poll while we were inside the handler,
     * queue another DPC round. Otherwise clear the count. */
    if (InterlockedExchange(&Poll->RequestCount, 0) > 1)
    {
        InterlockedIncrement(&Poll->RequestCount);
        KeInsertQueueDpc(&Poll->Dpc, NULL, NULL);
    }
}

NDIS_STATUS
NTAPI
NdisRegisterPoll(
    _In_     NDIS_HANDLE   NdisHandle,
    _In_opt_ PVOID         PollContext,
    _In_     PVOID         PollCharacteristics,  /* NDIS_POLL_CHARACTERISTICS */
    _Out_    PNDIS_HANDLE  PollHandle)
{
    PNDIS6_POLL_CONTEXT Poll;
    PNDIS6_POLL_HANDLER Handler = NULL;

    UNREFERENCED_PARAMETER(NdisHandle);

    if (PollHandle == NULL || PollCharacteristics == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* NDIS_POLL_CHARACTERISTICS layout: Header + PollHandler +
     * SetPollNotificationHandler. We read PollHandler at offset
     * sizeof(NDIS_OBJECT_HEADER). */
    Handler = *(PNDIS6_POLL_HANDLER*)((PUCHAR)PollCharacteristics +
                                      sizeof(NDIS_OBJECT_HEADER));
    if (Handler == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Poll = (PNDIS6_POLL_CONTEXT)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_POLL_CONTEXT), NDIS6_POLL_TAG);
    if (Poll == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Poll, sizeof(*Poll));
    Poll->Magic        = NDIS6_POLL_MAGIC;
    Poll->PollHandler  = Handler;
    Poll->PollContext  = PollContext;
    Poll->RequestCount = 0;
    KeInitializeDpc(&Poll->Dpc, Ndis6PollDpcRoutine, Poll);

    *PollHandle = (NDIS_HANDLE)Poll;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisDeregisterPoll(
    _In_ NDIS_HANDLE PollHandle)
{
    PNDIS6_POLL_CONTEXT Poll = (PNDIS6_POLL_CONTEXT)PollHandle;
    if (Poll == NULL || Poll->Magic != NDIS6_POLL_MAGIC)
        return;

    Poll->Unregistered = TRUE;
    /* Flush any pending DPC. */
    KeRemoveQueueDpc(&Poll->Dpc);

    Poll->Magic = 0;
    ExFreePoolWithTag(Poll, NDIS6_POLL_TAG);
}

VOID
NTAPI
NdisRequestPoll(
    _In_ NDIS_HANDLE PollHandle,
    _In_opt_ PVOID   Reserved)
{
    PNDIS6_POLL_CONTEXT Poll = (PNDIS6_POLL_CONTEXT)PollHandle;

    UNREFERENCED_PARAMETER(Reserved);

    if (Poll == NULL || Poll->Magic != NDIS6_POLL_MAGIC || Poll->Unregistered)
        return;

    /* Only queue a DPC on the 0→1 transition. Subsequent requests just
     * bump the counter; the running handler will re-queue itself on exit
     * if RequestCount > 1. */
    if (InterlockedIncrement(&Poll->RequestCount) == 1)
        KeInsertQueueDpc(&Poll->Dpc, NULL, NULL);
}

/* EOF */
