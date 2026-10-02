/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform filter engine: sessions, objects and classification
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wfp.h"

#define WFP_MAX_MATCHES 16

typedef struct _WFP_MATCH
{
    PWFP_FILTER Filter;
    PWFP_FLOW_CONTEXT FlowContext;
    UINT64 Context;
    BOOLEAN Call;
} WFP_MATCH, *PWFP_MATCH;

static const GUID WfpUniversalSubLayerKey =
    { 0xeebecc03, 0xced4, 0x4380, { 0x81, 0x9a, 0x27, 0x34, 0x39, 0x7b, 0x2b, 0x74 } };

KSPIN_LOCK WfpLock;
LIST_ENTRY WfpCallouts = { &WfpCallouts, &WfpCallouts };
LONG WfpLayerFilters[WFP_MAX_LAYERS];
NDIS_HANDLE WfpNblPool;

static LIST_ENTRY WfpSessions = { &WfpSessions, &WfpSessions };
static LIST_ENTRY WfpProviders = { &WfpProviders, &WfpProviders };
static LIST_ENTRY WfpSubLayers = { &WfpSubLayers, &WfpSubLayers };
static LIST_ENTRY WfpFilters = { &WfpFilters, &WfpFilters };
static WFP_SUBLAYER WfpUniversalSubLayer;
static KSEMAPHORE WfpTransactionLock;
static LONG WfpInitState;
static UINT32 WfpNextCalloutId;
static UINT64 WfpNextFilterId;
static UINT64 WfpFilterSequence;

static
VOID
WfpDelay(VOID)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10 * 1000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

VOID
WfpInitialize(VOID)
{
    NET_BUFFER_LIST_POOL_PARAMETERS Parameters;
    KIRQL OldIrql;

    if (InterlockedCompareExchange(&WfpInitState, 1, 0) != 0)
    {
        while (WfpInitState != 2)
        {
            WfpDelay();
        }
        return;
    }

    KeInitializeSemaphore(&WfpTransactionLock, 1, 1);

    RtlZeroMemory(&Parameters, sizeof(Parameters));
    Parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Parameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    Parameters.Header.Size = NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    Parameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    Parameters.fAllocateNetBuffer = TRUE;
    Parameters.PoolTag = WFP_TAG;
    WfpNblPool = NdisAllocateNetBufferListPool(NULL, &Parameters);

    WfpUniversalSubLayer.Key = WfpUniversalSubLayerKey;
    KeAcquireSpinLock(&WfpLock, &OldIrql);
    InsertTailList(&WfpSubLayers, &WfpUniversalSubLayer.Link);
    KeReleaseSpinLock(&WfpLock, OldIrql);

    InterlockedExchange(&WfpInitState, 2);
}

static
BOOLEAN
WfpFindLayer(
    _In_ const GUID *Key,
    _Out_ PUINT16 LayerId)
{
    static const GUID Zero;
    ULONG Index;

    if (IsEqualGUID(Key, &Zero))
    {
        return FALSE;
    }

    for (Index = 0; Index < WfpLayerCount; Index++)
    {
        if (IsEqualGUID(Key, &WfpLayers[Index].Key))
        {
            *LayerId = (UINT16)Index;
            return TRUE;
        }
    }
    return FALSE;
}

static
BOOLEAN
WfpFindField(
    _In_ UINT16 LayerId,
    _In_ const GUID *Key,
    _Out_ PUINT16 FieldId)
{
    static const GUID Zero;
    const WFP_LAYER *Layer = &WfpLayers[LayerId];
    ULONG Index;

    if (IsEqualGUID(Key, &Zero))
    {
        return FALSE;
    }

    for (Index = 0; Index < Layer->FieldCount; Index++)
    {
        if (IsEqualGUID(Key, &Layer->Fields[Index]))
        {
            *FieldId = (UINT16)Index;
            return TRUE;
        }
    }
    return FALSE;
}

static
PWFP_SESSION
WfpFindSessionLocked(
    _In_ HANDLE Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = WfpSessions.Flink; Entry != &WfpSessions; Entry = Entry->Flink)
    {
        if (Entry == (PLIST_ENTRY)Handle)
        {
            return CONTAINING_RECORD(Entry, WFP_SESSION, Link);
        }
    }
    return NULL;
}

static
PWFP_SESSION
WfpFindSession(
    _In_ HANDLE Handle)
{
    PWFP_SESSION Session;
    KIRQL OldIrql;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Session = WfpFindSessionLocked(Handle);
    KeReleaseSpinLock(&WfpLock, OldIrql);
    return Session;
}

static
PWFP_PROVIDER
WfpFindProviderLocked(
    _In_ const GUID *Key)
{
    PWFP_PROVIDER Provider;
    PLIST_ENTRY Entry;

    for (Entry = WfpProviders.Flink; Entry != &WfpProviders; Entry = Entry->Flink)
    {
        Provider = CONTAINING_RECORD(Entry, WFP_PROVIDER, Link);
        if (IsEqualGUID(&Provider->Key, Key) && !(Provider->State & WFP_STATE_DELETE_PENDING))
        {
            return Provider;
        }
    }
    return NULL;
}

static
PWFP_SUBLAYER
WfpFindSubLayerLocked(
    _In_ const GUID *Key)
{
    PWFP_SUBLAYER SubLayer;
    PLIST_ENTRY Entry;

    for (Entry = WfpSubLayers.Flink; Entry != &WfpSubLayers; Entry = Entry->Flink)
    {
        SubLayer = CONTAINING_RECORD(Entry, WFP_SUBLAYER, Link);
        if (IsEqualGUID(&SubLayer->Key, Key) && !(SubLayer->State & WFP_STATE_DELETE_PENDING))
        {
            return SubLayer;
        }
    }
    return NULL;
}

static
PWFP_CALLOUT
WfpFindCalloutByKeyLocked(
    _In_ const GUID *Key)
{
    PWFP_CALLOUT Callout;
    PLIST_ENTRY Entry;

    for (Entry = WfpCallouts.Flink; Entry != &WfpCallouts; Entry = Entry->Flink)
    {
        Callout = CONTAINING_RECORD(Entry, WFP_CALLOUT, Link);
        if (IsEqualGUID(&Callout->Key, Key))
        {
            return Callout;
        }
    }
    return NULL;
}

PWFP_CALLOUT
WfpFindCalloutById(
    _In_ UINT32 Id)
{
    PWFP_CALLOUT Callout;
    PLIST_ENTRY Entry;

    for (Entry = WfpCallouts.Flink; Entry != &WfpCallouts; Entry = Entry->Flink)
    {
        Callout = CONTAINING_RECORD(Entry, WFP_CALLOUT, Link);
        if (Callout->Id == Id)
        {
            return Callout;
        }
    }
    return NULL;
}

static
BOOLEAN
WfpReleaseCalloutLocked(
    _In_ PWFP_CALLOUT Callout)
{
    if (--Callout->References != 0)
    {
        return FALSE;
    }
    RemoveEntryList(&Callout->Link);
    return TRUE;
}

VOID
WfpDereferenceCallout(
    _In_ PWFP_CALLOUT Callout)
{
    BOOLEAN Free;
    KIRQL OldIrql;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Free = WfpReleaseCalloutLocked(Callout);
    KeReleaseSpinLock(&WfpLock, OldIrql);
    if (Free)
    {
        ExFreePoolWithTag(Callout, WFP_TAG);
    }
}

static
VOID
WfpDereferenceFilter(
    _In_ PWFP_FILTER Filter)
{
    if (InterlockedDecrement(&Filter->References) != 0)
    {
        return;
    }

    if (Filter->Callout != NULL)
    {
        WfpDereferenceCallout(Filter->Callout);
    }
    ExFreePoolWithTag(Filter, WFP_TAG);
}

static
VOID
WfpNotifyFilter(
    _In_ PWFP_FILTER Filter,
    _In_ FWPS_CALLOUT_NOTIFY_TYPE Type,
    _In_opt_ const GUID *Key)
{
    PWFP_CALLOUT Callout = Filter->Callout;
    FWPS_CALLOUT_NOTIFY_FN0 NotifyFn = NULL;
    KIRQL OldIrql;

    if (Callout == NULL)
    {
        return;
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    if (Callout->Registered && Callout->NotifyFn != NULL)
    {
        NotifyFn = Callout->NotifyFn;
        InterlockedIncrement(&Callout->Active);
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);
    if (NotifyFn == NULL)
    {
        return;
    }

    if (Type == FWPS_CALLOUT_NOTIFY_ADD_FILTER)
    {
        Filter->NotifyStatus = NotifyFn(Type, Key, &Filter->Runtime);
        Filter->Notified = NT_SUCCESS(Filter->NotifyStatus);
    }
    else if (Filter->Notified)
    {
        NotifyFn(Type, Key, &Filter->Runtime);
        Filter->Notified = FALSE;
    }
    InterlockedDecrement(&Callout->Active);
}

static
VOID
WfpWaitForCallout(
    _In_ PWFP_CALLOUT Callout)
{
    while (Callout->Active != 0)
    {
        WfpDelay();
    }
}

static
NTSTATUS
WfpCommit(VOID)
{
    PWFP_FILTER Filter, Added = NULL, Deleted = NULL, Next;
    PWFP_FILTER *Link, *DeletedTail = &Deleted;
    PWFP_PROVIDER Provider;
    PWFP_SUBLAYER SubLayer;
    PWFP_CALLOUT Callout;
    LIST_ENTRY FreeList;
    PLIST_ENTRY Entry, NextEntry;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    InitializeListHead(&FreeList);

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    for (Entry = WfpFilters.Flink; Entry != &WfpFilters; Entry = Entry->Flink)
    {
        Filter = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
        if ((Filter->State & (WFP_STATE_UNCOMMITTED | WFP_STATE_DELETE_PENDING)) == WFP_STATE_UNCOMMITTED)
        {
            InterlockedIncrement(&Filter->References);
            for (Link = &Added; *Link != NULL; Link = &(*Link)->Next)
            {
                if ((*Link)->Runtime.filterId > Filter->Runtime.filterId)
                {
                    break;
                }
            }
            Filter->Next = *Link;
            *Link = Filter;
        }
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    for (Filter = Added; Filter != NULL; Filter = Filter->Next)
    {
        Filter->NotifyStatus = STATUS_SUCCESS;
        WfpNotifyFilter(Filter, FWPS_CALLOUT_NOTIFY_ADD_FILTER, &Filter->Key);
        if (!NT_SUCCESS(Filter->NotifyStatus))
        {
            Status = STATUS_FWP_CALLOUT_NOTIFICATION_FAILED;
        }
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    for (Entry = WfpFilters.Flink; Entry != &WfpFilters; Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        Filter = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
        if ((Filter->State & WFP_STATE_UNCOMMITTED) && !NT_SUCCESS(Filter->NotifyStatus))
        {
            Filter->State |= WFP_STATE_DELETE_PENDING;
        }

        if (Filter->State & WFP_STATE_DELETE_PENDING)
        {
            RemoveEntryList(&Filter->Link);
            if (!(Filter->State & WFP_STATE_UNCOMMITTED))
            {
                InterlockedDecrement(&WfpLayerFilters[Filter->LayerId]);
            }
            Filter->Next = NULL;
            *DeletedTail = Filter;
            DeletedTail = &Filter->Next;
        }
        else if (Filter->State & WFP_STATE_UNCOMMITTED)
        {
            Filter->State = 0;
            InterlockedIncrement(&WfpLayerFilters[Filter->LayerId]);
        }
    }

    for (Entry = WfpCallouts.Flink; Entry != &WfpCallouts; Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        Callout = CONTAINING_RECORD(Entry, WFP_CALLOUT, Link);
        if (Callout->State & WFP_STATE_DELETE_PENDING)
        {
            Callout->State = 0;
            Callout->Added = FALSE;
            Callout->Owner = NULL;
            if (WfpReleaseCalloutLocked(Callout))
            {
                InsertTailList(&FreeList, &Callout->Link);
            }
        }
        else
        {
            Callout->State = 0;
        }
    }

    for (Entry = WfpSubLayers.Flink; Entry != &WfpSubLayers; Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        SubLayer = CONTAINING_RECORD(Entry, WFP_SUBLAYER, Link);
        if (SubLayer->State & WFP_STATE_DELETE_PENDING)
        {
            RemoveEntryList(&SubLayer->Link);
            InsertTailList(&FreeList, &SubLayer->Link);
        }
        SubLayer->State = 0;
    }

    for (Entry = WfpProviders.Flink; Entry != &WfpProviders; Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        Provider = CONTAINING_RECORD(Entry, WFP_PROVIDER, Link);
        if (Provider->State & WFP_STATE_DELETE_PENDING)
        {
            RemoveEntryList(&Provider->Link);
            InsertTailList(&FreeList, &Provider->Link);
        }
        Provider->State = 0;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    for (Filter = Added; Filter != NULL; Filter = Next)
    {
        Next = Filter->Next;
        WfpDereferenceFilter(Filter);
    }

    for (Filter = Deleted; Filter != NULL; Filter = Next)
    {
        Next = Filter->Next;
        if (Filter->Callout != NULL)
        {
            WfpWaitForCallout(Filter->Callout);
        }
        WfpNotifyFilter(Filter, FWPS_CALLOUT_NOTIFY_DELETE_FILTER, NULL);
        WfpDereferenceFilter(Filter);
    }

    while (!IsListEmpty(&FreeList))
    {
        ExFreePoolWithTag(RemoveHeadList(&FreeList), WFP_TAG);
    }
    return Status;
}

static
VOID
WfpAbort(VOID)
{
    PWFP_FILTER Filter, Removed = NULL, Next;
    PWFP_PROVIDER Provider;
    PWFP_SUBLAYER SubLayer;
    PWFP_CALLOUT Callout;
    LIST_ENTRY FreeList;
    PLIST_ENTRY Entry, NextEntry;
    KIRQL OldIrql;

    InitializeListHead(&FreeList);

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    for (Entry = WfpFilters.Flink; Entry != &WfpFilters; Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        Filter = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
        if (Filter->State & WFP_STATE_UNCOMMITTED)
        {
            RemoveEntryList(&Filter->Link);
            Filter->Next = Removed;
            Removed = Filter;
        }
        else
        {
            Filter->State = 0;
        }
    }

    for (Entry = WfpCallouts.Flink; Entry != &WfpCallouts; Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        Callout = CONTAINING_RECORD(Entry, WFP_CALLOUT, Link);
        if (Callout->State & WFP_STATE_UNCOMMITTED)
        {
            Callout->State = 0;
            Callout->Added = FALSE;
            Callout->Owner = NULL;
            if (WfpReleaseCalloutLocked(Callout))
            {
                InsertTailList(&FreeList, &Callout->Link);
            }
        }
        else
        {
            Callout->State = 0;
        }
    }

    for (Entry = WfpSubLayers.Flink; Entry != &WfpSubLayers; Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        SubLayer = CONTAINING_RECORD(Entry, WFP_SUBLAYER, Link);
        if (SubLayer->State & WFP_STATE_UNCOMMITTED)
        {
            RemoveEntryList(&SubLayer->Link);
            InsertTailList(&FreeList, &SubLayer->Link);
        }
        SubLayer->State = 0;
    }

    for (Entry = WfpProviders.Flink; Entry != &WfpProviders; Entry = NextEntry)
    {
        NextEntry = Entry->Flink;
        Provider = CONTAINING_RECORD(Entry, WFP_PROVIDER, Link);
        if (Provider->State & WFP_STATE_UNCOMMITTED)
        {
            RemoveEntryList(&Provider->Link);
            InsertTailList(&FreeList, &Provider->Link);
        }
        Provider->State = 0;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    for (Filter = Removed; Filter != NULL; Filter = Next)
    {
        Next = Filter->Next;
        WfpDereferenceFilter(Filter);
    }

    while (!IsListEmpty(&FreeList))
    {
        ExFreePoolWithTag(RemoveHeadList(&FreeList), WFP_TAG);
    }
}

static
NTSTATUS
WfpAcquireTransaction(
    _In_ PWFP_SESSION Session)
{
    LARGE_INTEGER Timeout;
    NTSTATUS Status;

    Timeout.QuadPart = -10000LL * Session->TransactionTimeout;
    Status = KeWaitForSingleObject(&WfpTransactionLock,
                                   Executive,
                                   KernelMode,
                                   FALSE,
                                   Session->TransactionTimeout != 0 ? &Timeout : NULL);
    return Status == STATUS_SUCCESS ? STATUS_SUCCESS : STATUS_FWP_TIMEOUT;
}

static
NTSTATUS
WfpBeginOperation(
    _In_ HANDLE EngineHandle,
    _Out_ PWFP_SESSION *Session,
    _Out_ PBOOLEAN Implicit)
{
    NTSTATUS Status;

    WfpInitialize();

    *Implicit = FALSE;
    *Session = WfpFindSession(EngineHandle);
    if (*Session == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    if ((*Session)->InTransaction)
    {
        return STATUS_SUCCESS;
    }

    Status = WfpAcquireTransaction(*Session);
    if (NT_SUCCESS(Status))
    {
        *Implicit = TRUE;
    }
    return Status;
}

static
NTSTATUS
WfpEndOperation(
    _In_ BOOLEAN Implicit,
    _In_ NTSTATUS Status)
{
    if (!Implicit)
    {
        return Status;
    }

    if (NT_SUCCESS(Status))
    {
        Status = WfpCommit();
    }
    else
    {
        WfpAbort();
    }
    KeReleaseSemaphore(&WfpTransactionLock, IO_NO_INCREMENT, 1, FALSE);
    return Status;
}

static
BOOLEAN
WfpSubLayerInUseLocked(
    _In_ PWFP_SUBLAYER SubLayer)
{
    PWFP_FILTER Filter;
    PLIST_ENTRY Entry;

    for (Entry = WfpFilters.Flink; Entry != &WfpFilters; Entry = Entry->Flink)
    {
        Filter = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
        if (Filter->SubLayer == SubLayer && !(Filter->State & WFP_STATE_DELETE_PENDING))
        {
            return TRUE;
        }
    }
    return FALSE;
}

NTSTATUS
NTAPI
FwpmEngineOpen0(
    _In_opt_ const wchar_t *serverName,
    _In_ UINT32 authnService,
    _In_opt_ SEC_WINNT_AUTH_IDENTITY_W *authIdentity,
    _In_opt_ const FWPM_SESSION0 *session,
    _Out_ HANDLE *engineHandle)
{
    PWFP_SESSION Session;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(authnService);
    UNREFERENCED_PARAMETER(authIdentity);

    if (engineHandle == NULL || serverName != NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    WfpInitialize();

    Session = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Session), WFP_TAG);
    if (Session == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Session, sizeof(*Session));
    if (session != NULL)
    {
        Session->Flags = session->flags;
        Session->TransactionTimeout = session->txnWaitTimeoutInMSec;
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    InsertTailList(&WfpSessions, &Session->Link);
    KeReleaseSpinLock(&WfpLock, OldIrql);

    *engineHandle = Session;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FwpmEngineClose0(
    _In_ HANDLE engineHandle)
{
    PWFP_SESSION Session;
    PWFP_PROVIDER Provider;
    PWFP_SUBLAYER SubLayer;
    PWFP_CALLOUT Callout;
    PWFP_FILTER Filter;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    KIRQL OldIrql;

    WfpInitialize();

    Session = WfpFindSession(engineHandle);
    if (Session == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    if (Session->InTransaction)
    {
        WfpAbort();
        Session->InTransaction = FALSE;
        KeReleaseSemaphore(&WfpTransactionLock, IO_NO_INCREMENT, 1, FALSE);
    }

    if (Session->Flags & FWPM_SESSION_FLAG_DYNAMIC)
    {
        Status = WfpAcquireTransaction(Session);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        KeAcquireSpinLock(&WfpLock, &OldIrql);
        for (Entry = WfpFilters.Flink; Entry != &WfpFilters; Entry = Entry->Flink)
        {
            Filter = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
            if (Filter->Owner == Session)
            {
                Filter->State |= WFP_STATE_DELETE_PENDING;
            }
        }
        for (Entry = WfpCallouts.Flink; Entry != &WfpCallouts; Entry = Entry->Flink)
        {
            Callout = CONTAINING_RECORD(Entry, WFP_CALLOUT, Link);
            if (Callout->Owner == Session && Callout->Added)
            {
                Callout->State |= WFP_STATE_DELETE_PENDING;
            }
        }
        for (Entry = WfpSubLayers.Flink; Entry != &WfpSubLayers; Entry = Entry->Flink)
        {
            SubLayer = CONTAINING_RECORD(Entry, WFP_SUBLAYER, Link);
            if (SubLayer->Owner == Session && !WfpSubLayerInUseLocked(SubLayer))
            {
                SubLayer->State |= WFP_STATE_DELETE_PENDING;
            }
        }
        for (Entry = WfpProviders.Flink; Entry != &WfpProviders; Entry = Entry->Flink)
        {
            Provider = CONTAINING_RECORD(Entry, WFP_PROVIDER, Link);
            if (Provider->Owner == Session)
            {
                Provider->State |= WFP_STATE_DELETE_PENDING;
            }
        }
        KeReleaseSpinLock(&WfpLock, OldIrql);

        WfpCommit();
        KeReleaseSemaphore(&WfpTransactionLock, IO_NO_INCREMENT, 1, FALSE);
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    RemoveEntryList(&Session->Link);
    KeReleaseSpinLock(&WfpLock, OldIrql);
    ExFreePoolWithTag(Session, WFP_TAG);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FwpmTransactionBegin0(
    _In_ HANDLE engineHandle,
    _In_ UINT32 flags)
{
    PWFP_SESSION Session;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(flags);

    WfpInitialize();

    Session = WfpFindSession(engineHandle);
    if (Session == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (Session->InTransaction)
    {
        return STATUS_FWP_TXN_IN_PROGRESS;
    }

    Status = WfpAcquireTransaction(Session);
    if (NT_SUCCESS(Status))
    {
        Session->InTransaction = TRUE;
    }
    return Status;
}

NTSTATUS
NTAPI
FwpmTransactionCommit0(
    _In_ HANDLE engineHandle)
{
    PWFP_SESSION Session;
    NTSTATUS Status;

    WfpInitialize();

    Session = WfpFindSession(engineHandle);
    if (Session == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (!Session->InTransaction)
    {
        return STATUS_FWP_NO_TXN_IN_PROGRESS;
    }

    Status = WfpCommit();
    Session->InTransaction = FALSE;
    KeReleaseSemaphore(&WfpTransactionLock, IO_NO_INCREMENT, 1, FALSE);
    return Status;
}

NTSTATUS
NTAPI
FwpmTransactionAbort0(
    _In_ HANDLE engineHandle)
{
    PWFP_SESSION Session;

    WfpInitialize();

    Session = WfpFindSession(engineHandle);
    if (Session == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }
    if (!Session->InTransaction)
    {
        return STATUS_FWP_NO_TXN_IN_PROGRESS;
    }

    WfpAbort();
    Session->InTransaction = FALSE;
    KeReleaseSemaphore(&WfpTransactionLock, IO_NO_INCREMENT, 1, FALSE);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FwpmProviderAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_PROVIDER0 *provider,
    _In_opt_ PSECURITY_DESCRIPTOR sd)
{
    PWFP_PROVIDER Provider;
    PWFP_SESSION Session;
    BOOLEAN Implicit;
    NTSTATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(sd);

    if (provider == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WfpBeginOperation(engineHandle, &Session, &Implicit);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Provider = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Provider), WFP_TAG);
    if (Provider == NULL)
    {
        return WfpEndOperation(Implicit, STATUS_INSUFFICIENT_RESOURCES);
    }

    RtlZeroMemory(Provider, sizeof(*Provider));
    Provider->Key = provider->providerKey;
    Provider->State = WFP_STATE_UNCOMMITTED;
    Provider->Owner = (Session->Flags & FWPM_SESSION_FLAG_DYNAMIC) ? Session : NULL;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    if (WfpFindProviderLocked(&Provider->Key) != NULL)
    {
        Status = STATUS_FWP_ALREADY_EXISTS;
    }
    else
    {
        InsertTailList(&WfpProviders, &Provider->Link);
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Provider, WFP_TAG);
    }
    return WfpEndOperation(Implicit, Status);
}

NTSTATUS
NTAPI
FwpmProviderDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key)
{
    PWFP_PROVIDER Provider;
    PWFP_SESSION Session;
    BOOLEAN Implicit;
    NTSTATUS Status;
    KIRQL OldIrql;

    if (key == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WfpBeginOperation(engineHandle, &Session, &Implicit);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Provider = WfpFindProviderLocked(key);
    if (Provider == NULL)
    {
        Status = STATUS_FWP_PROVIDER_NOT_FOUND;
    }
    else
    {
        Provider->State |= WFP_STATE_DELETE_PENDING;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    return WfpEndOperation(Implicit, Status);
}

NTSTATUS
NTAPI
FwpmSubLayerAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_SUBLAYER0 *subLayer,
    _In_opt_ PSECURITY_DESCRIPTOR sd)
{
    PWFP_SUBLAYER SubLayer;
    PWFP_SESSION Session;
    BOOLEAN Implicit;
    NTSTATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(sd);

    if (subLayer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WfpBeginOperation(engineHandle, &Session, &Implicit);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    SubLayer = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*SubLayer), WFP_TAG);
    if (SubLayer == NULL)
    {
        return WfpEndOperation(Implicit, STATUS_INSUFFICIENT_RESOURCES);
    }

    RtlZeroMemory(SubLayer, sizeof(*SubLayer));
    SubLayer->Key = subLayer->subLayerKey;
    SubLayer->Weight = subLayer->weight;
    SubLayer->State = WFP_STATE_UNCOMMITTED;
    SubLayer->Owner = (Session->Flags & FWPM_SESSION_FLAG_DYNAMIC) ? Session : NULL;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    if (subLayer->providerKey != NULL && WfpFindProviderLocked(subLayer->providerKey) == NULL)
    {
        Status = STATUS_FWP_PROVIDER_NOT_FOUND;
    }
    else if (WfpFindSubLayerLocked(&SubLayer->Key) != NULL)
    {
        Status = STATUS_FWP_ALREADY_EXISTS;
    }
    else
    {
        InsertTailList(&WfpSubLayers, &SubLayer->Link);
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(SubLayer, WFP_TAG);
    }
    return WfpEndOperation(Implicit, Status);
}

NTSTATUS
NTAPI
FwpmSubLayerDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key)
{
    PWFP_SUBLAYER SubLayer;
    PWFP_SESSION Session;
    BOOLEAN Implicit;
    NTSTATUS Status;
    KIRQL OldIrql;

    if (key == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WfpBeginOperation(engineHandle, &Session, &Implicit);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    SubLayer = WfpFindSubLayerLocked(key);
    if (SubLayer == NULL)
    {
        Status = STATUS_FWP_SUBLAYER_NOT_FOUND;
    }
    else if (SubLayer == &WfpUniversalSubLayer)
    {
        Status = STATUS_FWP_BUILTIN_OBJECT;
    }
    else if (WfpSubLayerInUseLocked(SubLayer))
    {
        Status = STATUS_FWP_IN_USE;
    }
    else
    {
        SubLayer->State |= WFP_STATE_DELETE_PENDING;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    return WfpEndOperation(Implicit, Status);
}

NTSTATUS
NTAPI
FwpmCalloutAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_CALLOUT0 *callout,
    _In_opt_ PSECURITY_DESCRIPTOR sd,
    _Out_opt_ UINT32 *id)
{
    PWFP_CALLOUT Callout, New;
    PWFP_SESSION Session;
    BOOLEAN Implicit;
    UINT16 LayerId;
    NTSTATUS Status;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(sd);

    if (callout == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WfpBeginOperation(engineHandle, &Session, &Implicit);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!WfpFindLayer(&callout->applicableLayer, &LayerId))
    {
        return WfpEndOperation(Implicit, STATUS_FWP_LAYER_NOT_FOUND);
    }

    New = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*New), WFP_TAG);
    if (New == NULL)
    {
        return WfpEndOperation(Implicit, STATUS_INSUFFICIENT_RESOURCES);
    }
    RtlZeroMemory(New, sizeof(*New));
    New->Key = callout->calloutKey;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Callout = WfpFindCalloutByKeyLocked(&callout->calloutKey);
    if (callout->providerKey != NULL && WfpFindProviderLocked(callout->providerKey) == NULL)
    {
        Status = STATUS_FWP_PROVIDER_NOT_FOUND;
    }
    else if (Callout != NULL && Callout->Added)
    {
        Status = STATUS_FWP_ALREADY_EXISTS;
    }
    else
    {
        if (Callout == NULL)
        {
            Callout = New;
            New = NULL;
            Callout->Id = ++WfpNextCalloutId;
            InsertTailList(&WfpCallouts, &Callout->Link);
        }
        Callout->References++;
        Callout->Added = TRUE;
        Callout->State = WFP_STATE_UNCOMMITTED;
        Callout->LayerId = LayerId;
        Callout->Owner = (Session->Flags & FWPM_SESSION_FLAG_DYNAMIC) ? Session : NULL;
        if (id != NULL)
        {
            *id = Callout->Id;
        }
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    if (New != NULL)
    {
        ExFreePoolWithTag(New, WFP_TAG);
    }
    return WfpEndOperation(Implicit, Status);
}

static
NTSTATUS
WfpDeleteCallout(
    _In_ HANDLE EngineHandle,
    _In_opt_ const GUID *Key,
    _In_ UINT32 Id)
{
    PWFP_CALLOUT Callout;
    PWFP_SESSION Session;
    PWFP_FILTER Filter;
    PLIST_ENTRY Entry;
    BOOLEAN Implicit;
    NTSTATUS Status;
    KIRQL OldIrql;

    Status = WfpBeginOperation(EngineHandle, &Session, &Implicit);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Callout = Key != NULL ? WfpFindCalloutByKeyLocked(Key) : WfpFindCalloutById(Id);
    if (Callout == NULL || !Callout->Added || (Callout->State & WFP_STATE_DELETE_PENDING))
    {
        Status = STATUS_FWP_CALLOUT_NOT_FOUND;
    }
    else
    {
        for (Entry = WfpFilters.Flink; Entry != &WfpFilters; Entry = Entry->Flink)
        {
            Filter = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
            if (Filter->Callout == Callout && !(Filter->State & WFP_STATE_DELETE_PENDING))
            {
                Status = STATUS_FWP_IN_USE;
                break;
            }
        }
        if (NT_SUCCESS(Status))
        {
            Callout->State |= WFP_STATE_DELETE_PENDING;
        }
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    return WfpEndOperation(Implicit, Status);
}

NTSTATUS
NTAPI
FwpmCalloutDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key)
{
    if (key == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return WfpDeleteCallout(engineHandle, key, 0);
}

NTSTATUS
NTAPI
FwpmCalloutDeleteById0(
    _In_ HANDLE engineHandle,
    _In_ UINT32 id)
{
    return WfpDeleteCallout(engineHandle, NULL, id);
}

static
NTSTATUS
WfpValueSize(
    _In_ FWP_DATA_TYPE Type,
    _In_ const FWP_CONDITION_VALUE0 *Value,
    _Out_ PSIZE_T Size)
{
    NTSTATUS Status;
    SIZE_T Low, High;

    *Size = 0;
    switch (Type)
    {
        case FWP_EMPTY:
        case FWP_UINT8:
        case FWP_UINT16:
        case FWP_UINT32:
        case FWP_INT8:
        case FWP_INT16:
        case FWP_INT32:
        case FWP_FLOAT:
            return STATUS_SUCCESS;

        case FWP_UINT64:
        case FWP_INT64:
        case FWP_DOUBLE:
            *Size = sizeof(UINT64);
            break;

        case FWP_BYTE_ARRAY16_TYPE:
            *Size = sizeof(FWP_BYTE_ARRAY16);
            break;

        case FWP_BYTE_ARRAY6_TYPE:
            *Size = sizeof(FWP_BYTE_ARRAY6);
            break;

        case FWP_BYTE_BLOB_TYPE:
        case FWP_SECURITY_DESCRIPTOR_TYPE:
        case FWP_TOKEN_ACCESS_INFORMATION_TYPE:
            if (Value->byteBlob == NULL)
            {
                return STATUS_FWP_NULL_POINTER;
            }
            *Size = sizeof(FWP_BYTE_BLOB) + Value->byteBlob->size;
            break;

        case FWP_SID:
            if (Value->sid == NULL || !RtlValidSid(Value->sid))
            {
                return STATUS_FWP_NULL_POINTER;
            }
            *Size = RtlLengthSid(Value->sid);
            break;

        case FWP_UNICODE_STRING_TYPE:
            if (Value->unicodeString == NULL)
            {
                return STATUS_FWP_NULL_POINTER;
            }
            *Size = (wcslen(Value->unicodeString) + 1) * sizeof(WCHAR);
            break;

        case FWP_V4_ADDR_MASK:
            *Size = sizeof(FWP_V4_ADDR_AND_MASK);
            break;

        case FWP_V6_ADDR_MASK:
            *Size = sizeof(FWP_V6_ADDR_AND_MASK);
            break;

        case FWP_RANGE_TYPE:
            if (Value->rangeValue == NULL ||
                Value->rangeValue->valueLow.type >= FWP_SINGLE_DATA_TYPE_MAX ||
                Value->rangeValue->valueHigh.type >= FWP_SINGLE_DATA_TYPE_MAX)
            {
                return STATUS_FWP_INVALID_RANGE;
            }
            Status = WfpValueSize(Value->rangeValue->valueLow.type,
                                  (const FWP_CONDITION_VALUE0 *)&Value->rangeValue->valueLow,
                                  &Low);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            Status = WfpValueSize(Value->rangeValue->valueHigh.type,
                                  (const FWP_CONDITION_VALUE0 *)&Value->rangeValue->valueHigh,
                                  &High);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
            *Size = sizeof(FWP_RANGE0) + ALIGN_UP_BY(Low, sizeof(UINT64)) + High;
            break;

        default:
            return STATUS_FWP_TYPE_MISMATCH;
    }

    if (Type != FWP_RANGE_TYPE && Value->uint64 == NULL)
    {
        return STATUS_FWP_NULL_POINTER;
    }
    *Size = ALIGN_UP_BY(*Size, sizeof(UINT64));
    return STATUS_SUCCESS;
}

static
VOID
WfpCopyValue(
    _Out_ FWP_CONDITION_VALUE0 *Target,
    _In_ const FWP_CONDITION_VALUE0 *Source,
    _Inout_ PUCHAR *Cursor)
{
    FWP_BYTE_BLOB *Blob;
    FWP_RANGE0 *Range;
    SIZE_T Size;

    *Target = *Source;
    if (!NT_SUCCESS(WfpValueSize(Source->type, Source, &Size)) || Size == 0)
    {
        return;
    }

    Target->uint64 = (UINT64 *)*Cursor;
    switch (Source->type)
    {
        case FWP_BYTE_BLOB_TYPE:
        case FWP_SECURITY_DESCRIPTOR_TYPE:
        case FWP_TOKEN_ACCESS_INFORMATION_TYPE:
            Blob = (FWP_BYTE_BLOB *)*Cursor;
            Blob->size = Source->byteBlob->size;
            Blob->data = (UINT8 *)(Blob + 1);
            RtlCopyMemory(Blob->data, Source->byteBlob->data, Blob->size);
            *Cursor += Size;
            break;

        case FWP_SID:
            RtlCopyMemory(*Cursor, Source->sid, RtlLengthSid(Source->sid));
            *Cursor += Size;
            break;

        case FWP_UNICODE_STRING_TYPE:
            RtlCopyMemory(*Cursor, Source->unicodeString, (wcslen(Source->unicodeString) + 1) * sizeof(WCHAR));
            *Cursor += Size;
            break;

        case FWP_RANGE_TYPE:
            Range = (FWP_RANGE0 *)*Cursor;
            *Cursor += sizeof(FWP_RANGE0);
            WfpCopyValue((FWP_CONDITION_VALUE0 *)&Range->valueLow,
                         (const FWP_CONDITION_VALUE0 *)&Source->rangeValue->valueLow,
                         Cursor);
            WfpCopyValue((FWP_CONDITION_VALUE0 *)&Range->valueHigh,
                         (const FWP_CONDITION_VALUE0 *)&Source->rangeValue->valueHigh,
                         Cursor);
            *Cursor = (PUCHAR)Range + Size;
            break;

        case FWP_UINT64:
        case FWP_INT64:
        case FWP_DOUBLE:
            RtlCopyMemory(*Cursor, Source->uint64, sizeof(UINT64));
            *Cursor += Size;
            break;

        case FWP_BYTE_ARRAY16_TYPE:
            RtlCopyMemory(*Cursor, Source->byteArray16, sizeof(FWP_BYTE_ARRAY16));
            *Cursor += Size;
            break;

        case FWP_BYTE_ARRAY6_TYPE:
            RtlCopyMemory(*Cursor, Source->byteArray6, sizeof(FWP_BYTE_ARRAY6));
            *Cursor += Size;
            break;

        case FWP_V4_ADDR_MASK:
            RtlCopyMemory(*Cursor, Source->v4AddrMask, sizeof(FWP_V4_ADDR_AND_MASK));
            *Cursor += Size;
            break;

        default:
            RtlCopyMemory(*Cursor, Source->v6AddrMask, sizeof(FWP_V6_ADDR_AND_MASK));
            *Cursor += Size;
            break;
    }
}

static
BOOLEAN
WfpFilterPrecedes(
    _In_ PWFP_FILTER Filter,
    _In_ PWFP_FILTER Other)
{
    if (Filter->SubLayer == Other->SubLayer)
    {
        return Filter->Weight > Other->Weight;
    }
    if (Filter->SubLayer->Weight != Other->SubLayer->Weight)
    {
        return Filter->SubLayer->Weight > Other->SubLayer->Weight;
    }
    return (ULONG_PTR)Filter->SubLayer > (ULONG_PTR)Other->SubLayer;
}

static
VOID
WfpInsertFilterLocked(
    _In_ PWFP_FILTER Filter)
{
    PWFP_FILTER Other;
    PLIST_ENTRY Entry;

    for (Entry = WfpFilters.Flink; Entry != &WfpFilters; Entry = Entry->Flink)
    {
        Other = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
        if (Other->LayerId == Filter->LayerId && WfpFilterPrecedes(Filter, Other))
        {
            break;
        }
    }
    InsertTailList(Entry, &Filter->Link);
}

NTSTATUS
NTAPI
FwpmFilterAdd0(
    _In_ HANDLE engineHandle,
    _In_ const FWPM_FILTER0 *filter,
    _In_opt_ PSECURITY_DESCRIPTOR sd,
    _Out_opt_ UINT64 *id)
{
    static const GUID Zero;
    PWFP_SESSION Session;
    PWFP_SUBLAYER SubLayer;
    PWFP_CALLOUT Callout = NULL;
    PWFP_FILTER Filter, Other;
    PLIST_ENTRY Entry;
    BOOLEAN Implicit, IsCallout;
    UINT16 LayerId, FieldId;
    SIZE_T Size, ValueSize;
    PUCHAR Cursor;
    UINT64 Weight;
    NTSTATUS Status;
    KIRQL OldIrql;
    ULONG Index;

    UNREFERENCED_PARAMETER(sd);

    if (filter == NULL || (filter->numFilterConditions != 0 && filter->filterCondition == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = WfpBeginOperation(engineHandle, &Session, &Implicit);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (!WfpFindLayer(&filter->layerKey, &LayerId))
    {
        return WfpEndOperation(Implicit, STATUS_FWP_LAYER_NOT_FOUND);
    }

    switch (filter->action.type)
    {
        case FWP_ACTION_BLOCK:
        case FWP_ACTION_PERMIT:
            IsCallout = FALSE;
            break;

        case FWP_ACTION_CALLOUT_TERMINATING:
        case FWP_ACTION_CALLOUT_INSPECTION:
        case FWP_ACTION_CALLOUT_UNKNOWN:
            IsCallout = TRUE;
            break;

        default:
            return WfpEndOperation(Implicit, STATUS_FWP_INVALID_ACTION_TYPE);
    }

    switch (filter->weight.type)
    {
        case FWP_EMPTY:
            Weight = (UINT64)filter->numFilterConditions << 32;
            break;

        case FWP_UINT8:
            if (filter->weight.uint8 > 15)
            {
                return WfpEndOperation(Implicit, STATUS_FWP_INVALID_WEIGHT);
            }
            Weight = ((UINT64)filter->weight.uint8 << 60) | ((UINT64)filter->numFilterConditions << 32);
            break;

        case FWP_UINT64:
            if (filter->weight.uint64 == NULL)
            {
                return WfpEndOperation(Implicit, STATUS_FWP_NULL_POINTER);
            }
            Weight = *filter->weight.uint64;
            break;

        default:
            return WfpEndOperation(Implicit, STATUS_FWP_INVALID_WEIGHT);
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    SubLayer = IsEqualGUID(&filter->subLayerKey, &Zero) ? &WfpUniversalSubLayer : WfpFindSubLayerLocked(&filter->subLayerKey);
    if (SubLayer == NULL)
    {
        Status = STATUS_FWP_SUBLAYER_NOT_FOUND;
    }
    else if (IsCallout)
    {
        Callout = WfpFindCalloutByKeyLocked(&filter->action.calloutKey);
        if (Callout == NULL || !Callout->Added || (Callout->State & WFP_STATE_DELETE_PENDING))
        {
            Callout = NULL;
            Status = STATUS_FWP_CALLOUT_NOT_FOUND;
        }
        else if (Callout->LayerId != LayerId)
        {
            Callout = NULL;
            Status = STATUS_FWP_INCOMPATIBLE_LAYER;
        }
        else
        {
            Callout->References++;
        }
    }
    if (NT_SUCCESS(Status) && filter->providerKey != NULL && WfpFindProviderLocked(filter->providerKey) == NULL)
    {
        Status = STATUS_FWP_PROVIDER_NOT_FOUND;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    if (NT_SUCCESS(Status) && (filter->flags & FWPM_FILTER_FLAG_HAS_PROVIDER_CONTEXT))
    {
        Status = STATUS_FWP_PROVIDER_CONTEXT_NOT_FOUND;
    }

    Size = sizeof(*Filter) + filter->numFilterConditions * sizeof(FWPS_FILTER_CONDITION0);
    for (Index = 0; NT_SUCCESS(Status) && Index < filter->numFilterConditions; Index++)
    {
        if (!WfpFindField(LayerId, &filter->filterCondition[Index].fieldKey, &FieldId))
        {
            Status = STATUS_FWP_CONDITION_NOT_FOUND;
        }
        else if (filter->filterCondition[Index].matchType >= FWP_MATCH_TYPE_MAX)
        {
            Status = STATUS_FWP_MATCH_TYPE_MISMATCH;
        }
        else
        {
            Status = WfpValueSize(filter->filterCondition[Index].conditionValue.type,
                                  &filter->filterCondition[Index].conditionValue,
                                  &ValueSize);
            Size += ValueSize;
        }
    }

    Filter = NULL;
    if (NT_SUCCESS(Status))
    {
        Filter = ExAllocatePoolWithTag(NonPagedPoolNx, Size, WFP_TAG);
        if (Filter == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        if (Callout != NULL)
        {
            WfpDereferenceCallout(Callout);
        }
        return WfpEndOperation(Implicit, Status);
    }

    RtlZeroMemory(Filter, Size);
    Filter->References = 1;
    Filter->Key = filter->filterKey;
    if (IsEqualGUID(&Filter->Key, &Zero))
    {
        ExUuidCreate(&Filter->Key);
    }
    Filter->LayerId = LayerId;
    Filter->State = WFP_STATE_UNCOMMITTED;
    Filter->Owner = (Session->Flags & FWPM_SESSION_FLAG_DYNAMIC) ? Session : NULL;
    Filter->SubLayer = SubLayer;
    Filter->Callout = Callout;
    Filter->Weight = Weight;
    Filter->Flags = filter->flags;
    Filter->Runtime.weight.type = FWP_UINT64;
    Filter->Runtime.weight.uint64 = &Filter->Weight;
    Filter->Runtime.subLayerWeight = SubLayer->Weight;
    Filter->Runtime.flags = (filter->flags & FWPM_FILTER_FLAG_CLEAR_ACTION_RIGHT) ? FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT : 0;
    Filter->Runtime.numFilterConditions = filter->numFilterConditions;
    Filter->Runtime.filterCondition = (FWPS_FILTER_CONDITION0 *)(Filter + 1);
    Filter->Runtime.action.type = filter->action.type;
    Filter->Runtime.action.calloutId = Callout != NULL ? Callout->Id : 0;
    Filter->Runtime.context = filter->rawContext;

    Cursor = (PUCHAR)(Filter->Runtime.filterCondition + filter->numFilterConditions);
    for (Index = 0; Index < filter->numFilterConditions; Index++)
    {
        WfpFindField(LayerId, &filter->filterCondition[Index].fieldKey, &FieldId);
        Filter->Runtime.filterCondition[Index].fieldId = FieldId;
        Filter->Runtime.filterCondition[Index].matchType = filter->filterCondition[Index].matchType;
        WfpCopyValue(&Filter->Runtime.filterCondition[Index].conditionValue,
                     &filter->filterCondition[Index].conditionValue,
                     &Cursor);
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    for (Entry = WfpFilters.Flink; Entry != &WfpFilters; Entry = Entry->Flink)
    {
        Other = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
        if (IsEqualGUID(&Other->Key, &Filter->Key) && !(Other->State & WFP_STATE_DELETE_PENDING))
        {
            Status = STATUS_FWP_ALREADY_EXISTS;
            break;
        }
    }
    if (NT_SUCCESS(Status))
    {
        Filter->Runtime.filterId = ++WfpNextFilterId;
        if (filter->weight.type != FWP_UINT64)
        {
            Filter->Weight |= 0xFFFFFFFF - (UINT32)++WfpFilterSequence;
        }
        WfpInsertFilterLocked(Filter);
        if (id != NULL)
        {
            *id = Filter->Runtime.filterId;
        }
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    if (!NT_SUCCESS(Status))
    {
        WfpDereferenceFilter(Filter);
    }
    return WfpEndOperation(Implicit, Status);
}

static
PWFP_FILTER
WfpFindFilterLocked(
    _In_opt_ const GUID *Key,
    _In_ UINT64 Id)
{
    PWFP_FILTER Filter;
    PLIST_ENTRY Entry;

    for (Entry = WfpFilters.Flink; Entry != &WfpFilters; Entry = Entry->Flink)
    {
        Filter = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
        if (Filter->State & WFP_STATE_DELETE_PENDING)
        {
            continue;
        }
        if (Key != NULL ? IsEqualGUID(&Filter->Key, Key) : Filter->Runtime.filterId == Id)
        {
            return Filter;
        }
    }
    return NULL;
}

static
NTSTATUS
WfpDeleteFilter(
    _In_ HANDLE EngineHandle,
    _In_opt_ const GUID *Key,
    _In_ UINT64 Id)
{
    PWFP_SESSION Session;
    PWFP_FILTER Filter;
    BOOLEAN Implicit;
    NTSTATUS Status;
    KIRQL OldIrql;

    Status = WfpBeginOperation(EngineHandle, &Session, &Implicit);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Filter = WfpFindFilterLocked(Key, Id);
    if (Filter == NULL)
    {
        Status = STATUS_FWP_FILTER_NOT_FOUND;
    }
    else
    {
        Filter->State |= WFP_STATE_DELETE_PENDING;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    return WfpEndOperation(Implicit, Status);
}

NTSTATUS
NTAPI
FwpmFilterDeleteByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key)
{
    if (key == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return WfpDeleteFilter(engineHandle, key, 0);
}

NTSTATUS
NTAPI
FwpmFilterDeleteById0(
    _In_ HANDLE engineHandle,
    _In_ UINT64 id)
{
    return WfpDeleteFilter(engineHandle, NULL, id);
}

static
NTSTATUS
WfpGetFilter(
    _In_ HANDLE EngineHandle,
    _In_opt_ const GUID *Key,
    _In_ UINT64 Id,
    _Out_ FWPM_FILTER0 **Result)
{
    FWPM_FILTER_CONDITION0 *Conditions;
    PWFP_FILTER Filter;
    FWPM_FILTER0 *Copy;
    SIZE_T Size, ValueSize;
    PUCHAR Cursor;
    KIRQL OldIrql;
    ULONG Index;

    if (Result == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *Result = NULL;

    WfpInitialize();
    if (WfpFindSession(EngineHandle) == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Filter = WfpFindFilterLocked(Key, Id);
    if (Filter != NULL)
    {
        InterlockedIncrement(&Filter->References);
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);
    if (Filter == NULL)
    {
        return STATUS_FWP_FILTER_NOT_FOUND;
    }

    Size = sizeof(*Copy) + sizeof(UINT64) + Filter->Runtime.numFilterConditions * sizeof(*Conditions);
    for (Index = 0; Index < Filter->Runtime.numFilterConditions; Index++)
    {
        WfpValueSize(Filter->Runtime.filterCondition[Index].conditionValue.type,
                     &Filter->Runtime.filterCondition[Index].conditionValue,
                     &ValueSize);
        Size += ValueSize;
    }

    Copy = ExAllocatePoolWithTag(NonPagedPoolNx, Size, WFP_TAG);
    if (Copy == NULL)
    {
        WfpDereferenceFilter(Filter);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Copy, Size);
    Copy->filterKey = Filter->Key;
    Copy->flags = Filter->Flags;
    Copy->layerKey = WfpLayers[Filter->LayerId].Key;
    Copy->subLayerKey = Filter->SubLayer->Key;
    Copy->weight.type = FWP_UINT64;
    Copy->weight.uint64 = (UINT64 *)(Copy + 1);
    *Copy->weight.uint64 = Filter->Weight;
    Copy->effectiveWeight = Copy->weight;
    Copy->numFilterConditions = Filter->Runtime.numFilterConditions;
    Conditions = (FWPM_FILTER_CONDITION0 *)(Copy->weight.uint64 + 1);
    Copy->filterCondition = Copy->numFilterConditions != 0 ? Conditions : NULL;
    Copy->action.type = Filter->Runtime.action.type;
    if (Filter->Callout != NULL)
    {
        Copy->action.calloutKey = Filter->Callout->Key;
    }
    Copy->rawContext = Filter->Runtime.context;
    Copy->filterId = Filter->Runtime.filterId;

    Cursor = (PUCHAR)(Conditions + Copy->numFilterConditions);
    for (Index = 0; Index < Copy->numFilterConditions; Index++)
    {
        Conditions[Index].fieldKey = WfpLayers[Filter->LayerId].Fields[Filter->Runtime.filterCondition[Index].fieldId];
        Conditions[Index].matchType = Filter->Runtime.filterCondition[Index].matchType;
        WfpCopyValue(&Conditions[Index].conditionValue,
                     &Filter->Runtime.filterCondition[Index].conditionValue,
                     &Cursor);
    }

    WfpDereferenceFilter(Filter);
    *Result = Copy;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FwpmFilterGetByKey0(
    _In_ HANDLE engineHandle,
    _In_ const GUID *key,
    _Out_ FWPM_FILTER0 **filter)
{
    if (key == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return WfpGetFilter(engineHandle, key, 0, filter);
}

NTSTATUS
NTAPI
FwpmFilterGetById0(
    _In_ HANDLE engineHandle,
    _In_ UINT64 id,
    _Out_ FWPM_FILTER0 **filter)
{
    return WfpGetFilter(engineHandle, NULL, id, filter);
}

VOID
NTAPI
FwpmFreeMemory0(
    _Inout_ PVOID *p)
{
    if (p != NULL && *p != NULL)
    {
        ExFreePoolWithTag(*p, WFP_TAG);
        *p = NULL;
    }
}

static
NTSTATUS
WfpRegisterCallout(
    _In_ const GUID *Key,
    _In_ UINT32 Flags,
    _In_ UCHAR Version,
    _In_opt_ PVOID ClassifyFn,
    _In_opt_ FWPS_CALLOUT_NOTIFY_FN0 NotifyFn,
    _In_opt_ FWPS_CALLOUT_FLOW_DELETE_NOTIFY_FN0 FlowDeleteFn,
    _Out_opt_ UINT32 *CalloutId)
{
    PWFP_FILTER Filter, Existing = NULL, Next;
    PWFP_CALLOUT Callout, New;
    PLIST_ENTRY Entry;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    WfpInitialize();
    if (WfpNblPool == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    New = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*New), WFP_TAG);
    if (New == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(New, sizeof(*New));
    New->Key = *Key;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Callout = WfpFindCalloutByKeyLocked(Key);
    if (Callout != NULL && Callout->Registered)
    {
        Status = STATUS_FWP_ALREADY_EXISTS;
    }
    else
    {
        if (Callout == NULL)
        {
            Callout = New;
            New = NULL;
            Callout->Id = ++WfpNextCalloutId;
            InsertTailList(&WfpCallouts, &Callout->Link);
        }
        Callout->References++;
        Callout->Flags = Flags;
        Callout->Version = Version;
        Callout->ClassifyFn = ClassifyFn;
        Callout->NotifyFn = NotifyFn;
        Callout->FlowDeleteFn = FlowDeleteFn;
        Callout->Registered = TRUE;
        if (CalloutId != NULL)
        {
            *CalloutId = Callout->Id;
        }

        for (Entry = WfpFilters.Flink; Entry != &WfpFilters; Entry = Entry->Flink)
        {
            Filter = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
            if (Filter->Callout == Callout && Filter->State == 0)
            {
                InterlockedIncrement(&Filter->References);
                Filter->Next = Existing;
                Existing = Filter;
            }
        }
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    if (New != NULL)
    {
        ExFreePoolWithTag(New, WFP_TAG);
    }

    for (Filter = Existing; Filter != NULL; Filter = Next)
    {
        Next = Filter->Next;
        WfpNotifyFilter(Filter, FWPS_CALLOUT_NOTIFY_ADD_FILTER, NULL);
        WfpDereferenceFilter(Filter);
    }
    return Status;
}

NTSTATUS
NTAPI
FwpsCalloutRegister0(
    _Inout_ void *deviceObject,
    _In_ const FWPS_CALLOUT0 *callout,
    _Out_opt_ UINT32 *calloutId)
{
    if (deviceObject == NULL || callout == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return WfpRegisterCallout(&callout->calloutKey,
                              callout->flags,
                              0,
                              callout->classifyFn,
                              callout->notifyFn,
                              callout->flowDeleteFn,
                              calloutId);
}

static
NTSTATUS
WfpRegisterCallout3(
    _Inout_ void *DeviceObject,
    _In_ const FWPS_CALLOUT3 *Callout,
    _In_ UCHAR Version,
    _Out_opt_ UINT32 *CalloutId)
{
    if (DeviceObject == NULL || Callout == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return WfpRegisterCallout(&Callout->calloutKey,
                              Callout->flags,
                              Version,
                              Callout->classifyFn,
                              (FWPS_CALLOUT_NOTIFY_FN0)Callout->notifyFn,
                              Callout->flowDeleteFn,
                              CalloutId);
}

NTSTATUS
NTAPI
FwpsCalloutRegister1(
    _Inout_ void *deviceObject,
    _In_ const FWPS_CALLOUT3 *callout,
    _Out_opt_ UINT32 *calloutId)
{
    return WfpRegisterCallout3(deviceObject, callout, 1, calloutId);
}

NTSTATUS
NTAPI
FwpsCalloutRegister2(
    _Inout_ void *deviceObject,
    _In_ const FWPS_CALLOUT3 *callout,
    _Out_opt_ UINT32 *calloutId)
{
    return WfpRegisterCallout3(deviceObject, callout, 2, calloutId);
}

NTSTATUS
NTAPI
FwpsCalloutRegister3(
    _Inout_ void *deviceObject,
    _In_ const FWPS_CALLOUT3 *callout,
    _Out_opt_ UINT32 *calloutId)
{
    return WfpRegisterCallout3(deviceObject, callout, 3, calloutId);
}

static
NTSTATUS
WfpUnregisterCallout(
    _In_opt_ const GUID *Key,
    _In_ UINT32 Id)
{
    PWFP_CALLOUT Callout;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Callout = Key != NULL ? WfpFindCalloutByKeyLocked(Key) : WfpFindCalloutById(Id);
    if (Callout == NULL || !Callout->Registered)
    {
        Status = STATUS_FWP_CALLOUT_NOT_FOUND;
    }
    else if (Callout->FlowContexts != 0)
    {
        Status = STATUS_DEVICE_BUSY;
    }
    else
    {
        Callout->Registered = FALSE;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    WfpWaitForCallout(Callout);
    WfpDereferenceCallout(Callout);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FwpsCalloutUnregisterById0(
    _In_ const UINT32 calloutId)
{
    return WfpUnregisterCallout(NULL, calloutId);
}

NTSTATUS
NTAPI
FwpsCalloutUnregisterByKey0(
    _In_ const GUID *calloutKey)
{
    if (calloutKey == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    return WfpUnregisterCallout(calloutKey, 0);
}

static
BOOLEAN
WfpNumber(
    _In_ FWP_DATA_TYPE Type,
    _In_ const FWP_CONDITION_VALUE0 *Value,
    _Out_ PUINT64 Number)
{
    switch (Type)
    {
        case FWP_UINT8:
            *Number = Value->uint8;
            return TRUE;

        case FWP_UINT16:
            *Number = Value->uint16;
            return TRUE;

        case FWP_UINT32:
            *Number = Value->uint32;
            return TRUE;

        case FWP_UINT64:
            *Number = *Value->uint64;
            return TRUE;

        default:
            return FALSE;
    }
}

static
SIZE_T
WfpBytes(
    _In_ FWP_DATA_TYPE Type,
    _In_ const FWP_CONDITION_VALUE0 *Value,
    _Out_ const UCHAR **Bytes)
{
    switch (Type)
    {
        case FWP_BYTE_ARRAY16_TYPE:
            *Bytes = Value->byteArray16->byteArray16;
            return sizeof(FWP_BYTE_ARRAY16);

        case FWP_BYTE_ARRAY6_TYPE:
            *Bytes = Value->byteArray6->byteArray6;
            return sizeof(FWP_BYTE_ARRAY6);

        case FWP_BYTE_BLOB_TYPE:
        case FWP_SECURITY_DESCRIPTOR_TYPE:
        case FWP_TOKEN_ACCESS_INFORMATION_TYPE:
            *Bytes = Value->byteBlob->data;
            return Value->byteBlob->size;

        case FWP_SID:
            *Bytes = (const UCHAR *)Value->sid;
            return RtlLengthSid(Value->sid);

        case FWP_UNICODE_STRING_TYPE:
            *Bytes = (const UCHAR *)Value->unicodeString;
            return wcslen(Value->unicodeString) * sizeof(WCHAR);

        default:
            *Bytes = NULL;
            return 0;
    }
}

static
BOOLEAN
WfpCompare(
    _In_ const FWP_CONDITION_VALUE0 *Condition,
    _In_ const FWP_VALUE0 *Value,
    _Out_ PLONG Result)
{
    const FWP_CONDITION_VALUE0 *Incoming = (const FWP_CONDITION_VALUE0 *)Value;
    const UCHAR *Left, *Right;
    UINT64 A, B;
    SIZE_T LeftSize, RightSize, Index;

    if (WfpNumber(Value->type, Incoming, &A) && WfpNumber(Condition->type, Condition, &B))
    {
        *Result = A < B ? -1 : (A > B ? 1 : 0);
        return TRUE;
    }

    if (Value->type != Condition->type || Value->uint64 == NULL)
    {
        return FALSE;
    }

    LeftSize = WfpBytes(Value->type, Incoming, &Left);
    RightSize = WfpBytes(Condition->type, Condition, &Right);
    if (Left == NULL || Right == NULL)
    {
        return FALSE;
    }

    for (Index = 0; Index < min(LeftSize, RightSize); Index++)
    {
        if (Left[Index] != Right[Index])
        {
            *Result = Left[Index] < Right[Index] ? -1 : 1;
            return TRUE;
        }
    }
    *Result = LeftSize < RightSize ? -1 : (LeftSize > RightSize ? 1 : 0);
    return TRUE;
}

static
BOOLEAN
WfpMatchCondition(
    _In_ const FWPS_FILTER_CONDITION0 *Condition,
    _In_ const FWP_VALUE0 *Value)
{
    const FWP_CONDITION_VALUE0 *Incoming = (const FWP_CONDITION_VALUE0 *)Value;
    const FWP_V6_ADDR_AND_MASK *Mask6;
    LONG Result, High;
    UINT64 A, B;
    ULONG Index;
    BOOLEAN Equal;

    switch (Condition->matchType)
    {
        case FWP_MATCH_EQUAL:
        case FWP_MATCH_EQUAL_CASE_INSENSITIVE:
        case FWP_MATCH_NOT_EQUAL:
            if (Condition->conditionValue.type == FWP_V4_ADDR_MASK && Value->type == FWP_UINT32)
            {
                Equal = ((Value->uint32 ^ Condition->conditionValue.v4AddrMask->addr) &
                         Condition->conditionValue.v4AddrMask->mask) == 0;
            }
            else if (Condition->conditionValue.type == FWP_V6_ADDR_MASK && Value->type == FWP_BYTE_ARRAY16_TYPE)
            {
                Mask6 = Condition->conditionValue.v6AddrMask;
                Equal = Mask6->prefixLength <= 128;
                for (Index = 0; Equal && Index < Mask6->prefixLength; Index++)
                {
                    if ((Value->byteArray16->byteArray16[Index / 8] ^ Mask6->addr[Index / 8]) & (0x80 >> (Index % 8)))
                    {
                        Equal = FALSE;
                    }
                }
            }
            else if (WfpCompare(&Condition->conditionValue, Value, &Result))
            {
                Equal = (Result == 0);
            }
            else
            {
                return FALSE;
            }
            return Condition->matchType == FWP_MATCH_NOT_EQUAL ? !Equal : Equal;

        case FWP_MATCH_GREATER:
            return WfpCompare(&Condition->conditionValue, Value, &Result) && Result > 0;

        case FWP_MATCH_LESS:
            return WfpCompare(&Condition->conditionValue, Value, &Result) && Result < 0;

        case FWP_MATCH_GREATER_OR_EQUAL:
            return WfpCompare(&Condition->conditionValue, Value, &Result) && Result >= 0;

        case FWP_MATCH_LESS_OR_EQUAL:
            return WfpCompare(&Condition->conditionValue, Value, &Result) && Result <= 0;

        case FWP_MATCH_RANGE:
            if (Condition->conditionValue.type != FWP_RANGE_TYPE)
            {
                return FALSE;
            }
            return WfpCompare((const FWP_CONDITION_VALUE0 *)&Condition->conditionValue.rangeValue->valueLow, Value, &Result) &&
                   WfpCompare((const FWP_CONDITION_VALUE0 *)&Condition->conditionValue.rangeValue->valueHigh, Value, &High) &&
                   Result >= 0 && High <= 0;

        case FWP_MATCH_FLAGS_ALL_SET:
        case FWP_MATCH_FLAGS_ANY_SET:
        case FWP_MATCH_FLAGS_NONE_SET:
            if (!WfpNumber(Value->type, Incoming, &A) ||
                !WfpNumber(Condition->conditionValue.type, &Condition->conditionValue, &B))
            {
                return FALSE;
            }
            if (Condition->matchType == FWP_MATCH_FLAGS_ALL_SET)
            {
                return (A & B) == B;
            }
            return Condition->matchType == FWP_MATCH_FLAGS_ANY_SET ? (A & B) != 0 : (A & B) == 0;

        default:
            return FALSE;
    }
}

static
BOOLEAN
WfpMatchFilter(
    _In_ PWFP_FILTER Filter,
    _In_reads_(ValueCount) const FWPS_INCOMING_VALUE0 *Values,
    _In_ ULONG ValueCount)
{
    const FWPS_FILTER_CONDITION0 *Conditions = Filter->Runtime.filterCondition;
    ULONG Count = Filter->Runtime.numFilterConditions;
    ULONG Index, Other;
    BOOLEAN Matched;

    for (Index = 0; Index < Count; Index++)
    {
        for (Other = 0; Other < Index; Other++)
        {
            if (Conditions[Other].fieldId == Conditions[Index].fieldId)
            {
                break;
            }
        }
        if (Other != Index)
        {
            continue;
        }
        if (Conditions[Index].fieldId >= ValueCount)
        {
            return FALSE;
        }

        Matched = FALSE;
        for (Other = Index; !Matched && Other < Count; Other++)
        {
            if (Conditions[Other].fieldId == Conditions[Index].fieldId)
            {
                Matched = WfpMatchCondition(&Conditions[Other], &Values[Conditions[Index].fieldId].value);
            }
        }
        if (!Matched)
        {
            return FALSE;
        }
    }
    return TRUE;
}

static
VOID
WfpFinishMatch(
    _In_ PWFP_MATCH Match)
{
    PWFP_FLOW_CONTEXT FlowContext = Match->FlowContext;
    PWFP_CALLOUT Callout = Match->Filter->Callout;
    BOOLEAN Delete = FALSE;
    KIRQL OldIrql;

    if (Match->Call)
    {
        InterlockedDecrement(&Callout->Active);
    }

    if (FlowContext != NULL)
    {
        KeAcquireSpinLock(&WfpLock, &OldIrql);
        Delete = (--FlowContext->Active == 0 && FlowContext->Removed);
        KeReleaseSpinLock(&WfpLock, OldIrql);
        if (Delete)
        {
            WfpDeleteFlowContext(FlowContext);
        }
    }
    WfpDereferenceFilter(Match->Filter);
}

VOID
WfpClassify(
    _In_ UINT16 LayerId,
    _In_reads_(ValueCount) FWPS_INCOMING_VALUE0 *Values,
    _In_ ULONG ValueCount,
    _In_ const FWPS_INCOMING_METADATA_VALUES0 *Metadata,
    _In_ UINT64 FlowId,
    _In_opt_ PWFP_LAYER_DATA_ROUTINE LayerDataRoutine,
    _In_opt_ PVOID LayerDataContext,
    _Out_ FWPS_CLASSIFY_OUT0 *ClassifyOut)
{
    WFP_MATCH Matches[WFP_MAX_MATCHES];
    FWPS_INCOMING_VALUES0 Incoming;
    PWFP_SUBLAYER SubLayer = NULL;
    PWFP_CALLOUT Callout;
    PWFP_FILTER Filter;
    PWFP_MATCH Match;
    PLIST_ENTRY Entry;
    PVOID LayerData = NULL;
    BOOLEAN Decided = FALSE, HaveData = FALSE;
    FWP_ACTION_TYPE Action = FWP_ACTION_PERMIT, Before;
    UINT32 Rights = FWPS_RIGHT_ACTION_WRITE, RightsBefore;
    ULONG Count = 0, Index;
    KIRQL OldIrql;

    RtlZeroMemory(ClassifyOut, sizeof(*ClassifyOut));
    ClassifyOut->actionType = FWP_ACTION_PERMIT;
    ClassifyOut->rights = FWPS_RIGHT_ACTION_WRITE;
    if (LayerId >= WFP_MAX_LAYERS || WfpLayerFilters[LayerId] == 0)
    {
        return;
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    for (Entry = WfpFilters.Flink; Entry != &WfpFilters && Count < WFP_MAX_MATCHES; Entry = Entry->Flink)
    {
        Filter = CONTAINING_RECORD(Entry, WFP_FILTER, Link);
        if (Filter->LayerId != LayerId || (Filter->State & WFP_STATE_UNCOMMITTED) ||
            (Filter->Flags & FWPM_FILTER_FLAG_DISABLED) || !WfpMatchFilter(Filter, Values, ValueCount))
        {
            continue;
        }

        Match = &Matches[Count];
        Match->Filter = Filter;
        Match->FlowContext = NULL;
        Match->Context = 0;
        Match->Call = FALSE;
        Callout = Filter->Callout;
        if (Callout != NULL && Callout->Registered && Callout->ClassifyFn != NULL)
        {
            Match->FlowContext = WfpFindFlowContextLocked(FlowId, LayerId, Callout);
            if (Match->FlowContext == NULL && (Callout->Flags & FWP_CALLOUT_FLAG_CONDITIONAL_ON_FLOW))
            {
                continue;
            }
            if (Match->FlowContext != NULL)
            {
                Match->FlowContext->Active++;
                Match->Context = Match->FlowContext->Context;
            }
            Match->Call = TRUE;
            InterlockedIncrement(&Callout->Active);
        }
        InterlockedIncrement(&Filter->References);
        Count++;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    Incoming.layerId = LayerId;
    Incoming.valueCount = ValueCount;
    Incoming.incomingValue = Values;

    for (Index = 0; Index < Count; Index++)
    {
        Match = &Matches[Index];
        Filter = Match->Filter;
        if (Filter->SubLayer != SubLayer)
        {
            SubLayer = Filter->SubLayer;
            Decided = FALSE;
        }
        if (Decided)
        {
            continue;
        }

        if (Filter->Callout == NULL || !Match->Call)
        {
            Before = Filter->Runtime.action.type;
            if (Filter->Callout != NULL)
            {
                if (Before == FWP_ACTION_CALLOUT_INSPECTION)
                {
                    continue;
                }
                Before = (Filter->Flags & FWPM_FILTER_FLAG_PERMIT_IF_CALLOUT_UNREGISTERED) ? FWP_ACTION_PERMIT : FWP_ACTION_BLOCK;
            }

            if (Before == FWP_ACTION_BLOCK)
            {
                Action = FWP_ACTION_BLOCK;
                Rights &= ~FWPS_RIGHT_ACTION_WRITE;
                ClassifyOut->filterId = Filter->Runtime.filterId;
            }
            else if (Rights & FWPS_RIGHT_ACTION_WRITE)
            {
                Action = FWP_ACTION_PERMIT;
                ClassifyOut->filterId = Filter->Runtime.filterId;
                if (Filter->Runtime.flags & FWPS_FILTER_FLAG_CLEAR_ACTION_RIGHT)
                {
                    Rights &= ~FWPS_RIGHT_ACTION_WRITE;
                }
            }
            Decided = TRUE;
            continue;
        }

        if (!HaveData && LayerDataRoutine != NULL)
        {
            LayerData = LayerDataRoutine(LayerDataContext);
            HaveData = TRUE;
        }

        Callout = Filter->Callout;
        Before = Action;
        RightsBefore = Rights;
        ClassifyOut->actionType = 0;
        ClassifyOut->rights = Rights;
        if (Callout->Version == 0)
        {
            ((FWPS_CALLOUT_CLASSIFY_FN0)Callout->ClassifyFn)(&Incoming,
                                                             Metadata,
                                                             LayerData,
                                                             &Filter->Runtime,
                                                             Match->Context,
                                                             ClassifyOut);
        }
        else
        {
            ((FWPS_CALLOUT_CLASSIFY_FN3)Callout->ClassifyFn)(&Incoming,
                                                             Metadata,
                                                             LayerData,
                                                             NULL,
                                                             (const FWPS_FILTER3 *)&Filter->Runtime,
                                                             Match->Context,
                                                             ClassifyOut);
        }

        if (Filter->Runtime.action.type == FWP_ACTION_CALLOUT_INSPECTION)
        {
            continue;
        }

        if (ClassifyOut->actionType == FWP_ACTION_BLOCK)
        {
            Action = FWP_ACTION_BLOCK;
            Rights = (RightsBefore & FWPS_RIGHT_ACTION_WRITE) ? ClassifyOut->rights : 0;
            ClassifyOut->filterId = Filter->Runtime.filterId;
            Decided = TRUE;
        }
        else if (ClassifyOut->actionType == FWP_ACTION_PERMIT)
        {
            if (RightsBefore & FWPS_RIGHT_ACTION_WRITE)
            {
                Action = FWP_ACTION_PERMIT;
                Rights = ClassifyOut->rights;
                ClassifyOut->filterId = Filter->Runtime.filterId;
            }
            else
            {
                Action = Before;
            }
            Decided = TRUE;
        }
    }

    ClassifyOut->actionType = Action;
    ClassifyOut->rights = Rights;

    for (Index = 0; Index < Count; Index++)
    {
        WfpFinishMatch(&Matches[Index]);
    }
}
