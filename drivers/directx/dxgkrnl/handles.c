/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Owner-scoped typed D3DKMT handle namespace
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */

#include "dxgkrnl_private.h"
#include "handles.h"

#define DXGKP_HANDLE_TYPE_SHIFT 29
#define DXGKP_HANDLE_TYPE_MASK 0xE0000000UL
#define DXGKP_HANDLE_GENERATION_MASK 0x1FFFFFFFUL
#define DXGKP_HANDLE_COOKIE 0x19B753A1UL

typedef struct _DXGKRNL_HANDLE_ENTRY
{
    LIST_ENTRY ListEntry;
    D3DKMT_HANDLE Handle;
    DXGKRNL_HANDLE_TYPE Type;
    PVOID Object;
    PDXGKRNL_ADAPTER Adapter;
    PEPROCESS OwnerProcess;
    volatile LONG *Destroying;
    volatile LONG *TeardownClaimed;
    BOOLEAN OwnsAdapterReference;
} DXGKRNL_HANDLE_ENTRY, *PDXGKRNL_HANDLE_ENTRY;

static FAST_MUTEX DxgkHandleTableLock;
static LIST_ENTRY DxgkHandleTableHead;
static volatile LONG DxgkNextHandleGeneration;
static BOOLEAN DxgkHandleTableInitialized;

static ULONG
DxgkpHandleTypeBits(
    _In_ DXGKRNL_HANDLE_TYPE Type)
{
    return ((ULONG)Type << DXGKP_HANDLE_TYPE_SHIFT) & DXGKP_HANDLE_TYPE_MASK;
}

static DXGKRNL_HANDLE_TYPE
DxgkpHandleType(
    _In_ D3DKMT_HANDLE Handle)
{
    return (DXGKRNL_HANDLE_TYPE)(((ULONG)Handle & DXGKP_HANDLE_TYPE_MASK) >> DXGKP_HANDLE_TYPE_SHIFT);
}

static BOOLEAN
DxgkpHandleExistsLocked(
    _In_ D3DKMT_HANDLE Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = DxgkHandleTableHead.Flink; Entry != &DxgkHandleTableHead; Entry = Entry->Flink)
    {
        PDXGKRNL_HANDLE_ENTRY HandleEntry = CONTAINING_RECORD(Entry, DXGKRNL_HANDLE_ENTRY, ListEntry);

        if (HandleEntry->Handle == Handle)
            return TRUE;
    }

    return FALSE;
}

static D3DKMT_HANDLE
DxgkpAllocateHandleLocked(
    _In_ DXGKRNL_HANDLE_TYPE Type)
{
    ULONG Generation;
    D3DKMT_HANDLE Handle;

    do
    {
        Generation = (ULONG)InterlockedIncrement(&DxgkNextHandleGeneration) & DXGKP_HANDLE_GENERATION_MASK;
        if (Generation == 0)
            continue;
        Handle = (D3DKMT_HANDLE)(DxgkpHandleTypeBits(Type) | ((Generation ^ DXGKP_HANDLE_COOKIE) & DXGKP_HANDLE_GENERATION_MASK));
    } while (Handle == 0 || DxgkpHandleExistsLocked(Handle));

    return Handle;
}

static NTSTATUS
DxgkpCreateHandle(
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_ PVOID Object,
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS OwnerProcess,
    _In_opt_ volatile LONG *Destroying,
    _In_opt_ volatile LONG *TeardownClaimed,
    _In_ BOOLEAN OwnsAdapterReference,
    _Out_ D3DKMT_HANDLE *OutHandle)
{
    PDXGKRNL_HANDLE_ENTRY Entry;

    if (!DxgkHandleTableInitialized || Object == NULL || Adapter == NULL || OwnerProcess == NULL || OutHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    *OutHandle = 0;
    Entry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Entry), TAG_DXGK_HANDLE);
    if (Entry == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Entry, sizeof(*Entry));
    Entry->Type = Type;
    Entry->Object = Object;
    Entry->Adapter = Adapter;
    Entry->OwnerProcess = OwnerProcess;
    Entry->Destroying = Destroying;
    Entry->TeardownClaimed = TeardownClaimed;
    Entry->OwnsAdapterReference = OwnsAdapterReference;
    InitializeListHead(&Entry->ListEntry);
    ObReferenceObject(OwnerProcess);

    ExAcquireFastMutex(&DxgkHandleTableLock);
    if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
    {
        ExReleaseFastMutex(&DxgkHandleTableLock);
        ObDereferenceObject(OwnerProcess);
        ExFreePoolWithTag(Entry, TAG_DXGK_HANDLE);
        return STATUS_DELETE_PENDING;
    }
    Entry->Handle = DxgkpAllocateHandleLocked(Type);
    InsertTailList(&DxgkHandleTableHead, &Entry->ListEntry);
    ExReleaseFastMutex(&DxgkHandleTableLock);

    *OutHandle = Entry->Handle;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkTryClaimTeardown(
    _Inout_ volatile LONG *TeardownClaimed)
{
    /* Destroying is only an admission gate and can be set by handle purge.
     * This independent claim transfers the object's one final-release right. */
    ASSERT(TeardownClaimed != NULL);
    return (TeardownClaimed != NULL && InterlockedCompareExchange(TeardownClaimed, 1, 0) == 0);
}

static PDXGKRNL_HANDLE_ENTRY
DxgkpFindHandleLocked(
    _In_ D3DKMT_HANDLE Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = DxgkHandleTableHead.Flink; Entry != &DxgkHandleTableHead; Entry = Entry->Flink)
    {
        PDXGKRNL_HANDLE_ENTRY HandleEntry = CONTAINING_RECORD(Entry, DXGKRNL_HANDLE_ENTRY, ListEntry);

        if (HandleEntry->Handle == Handle)
            return HandleEntry;
    }

    return NULL;
}

static NTSTATUS
DxgkpValidateHandleEntry(
    _In_opt_ PDXGKRNL_HANDLE_ENTRY Entry,
    _In_ D3DKMT_HANDLE Handle,
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_ PEPROCESS OwnerProcess)
{
    /*
     * A D3DKMT_HANDLE is a private handle-table index, not an NT handle, and
     * Windows 11 rejects one that names nothing -- or names the wrong kind of
     * object -- as a bad *parameter*.  Measured across the whole D3DKMT surface
     * against the reference VM; a bad NT handle (hProcess) still yields
     * STATUS_INVALID_HANDLE, which is why that conversion does not belong in
     * the callers.
     */
    if (DxgkpHandleType(Handle) != Type)
        return STATUS_INVALID_PARAMETER;
    if (Entry == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Entry->Type != Type)
        return STATUS_INVALID_PARAMETER;
    if (Entry->OwnerProcess != OwnerProcess)
        return STATUS_ACCESS_DENIED;
    if (Entry->Destroying != NULL && InterlockedCompareExchange(Entry->Destroying, 0, 0) != 0)
        return STATUS_DELETE_PENDING;
    if (InterlockedCompareExchange(&Entry->Adapter->RundownStarted, 0, 0) != 0)
        return STATUS_DELETE_PENDING;
    return STATUS_SUCCESS;
}

static VOID
DxgkpFreeHandleEntry(
    _In_ PDXGKRNL_HANDLE_ENTRY Entry)
{
    if (Entry->OwnsAdapterReference)
        DxgkDereferenceAdapter(Entry->Adapter);
    ObDereferenceObject(Entry->OwnerProcess);
    ExFreePoolWithTag(Entry, TAG_DXGK_HANDLE);
}

NTSTATUS
DxgkHandleManagerInitialize(VOID)
{
    PAGED_CODE();

    if (DxgkHandleTableInitialized)
        return STATUS_ALREADY_INITIALIZED;
    ExInitializeFastMutex(&DxgkHandleTableLock);
    InitializeListHead(&DxgkHandleTableHead);
    DxgkNextHandleGeneration = 0;
    DxgkHandleTableInitialized = TRUE;
    return STATUS_SUCCESS;
}

VOID
DxgkHandleManagerUninitialize(VOID)
{
    LIST_ENTRY Retired;

    PAGED_CODE();

    if (!DxgkHandleTableInitialized)
        return;
    InitializeListHead(&Retired);
    ExAcquireFastMutex(&DxgkHandleTableLock);
    while (!IsListEmpty(&DxgkHandleTableHead))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&DxgkHandleTableHead);

        InsertTailList(&Retired, Entry);
    }
    DxgkHandleTableInitialized = FALSE;
    ExReleaseFastMutex(&DxgkHandleTableLock);

    while (!IsListEmpty(&Retired))
    {
        PDXGKRNL_HANDLE_ENTRY Entry = CONTAINING_RECORD(RemoveHeadList(&Retired), DXGKRNL_HANDLE_ENTRY, ListEntry);

        DxgkpFreeHandleEntry(Entry);
    }
}

BOOLEAN
DxgkReferenceAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
        return FALSE;
    if (!ExAcquireRundownProtection(&Adapter->RundownRef))
        return FALSE;
    if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0 || Adapter->State != DxgkAdapterStateStarted)
    {
        ExReleaseRundownProtection(&Adapter->RundownRef);
        return FALSE;
    }
    return TRUE;
}

BOOLEAN
DxgkReferenceAdapterObject(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    if (Adapter == NULL)
        return FALSE;
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);
    for (Entry = DxgkAdapterGlobalListHead.Flink; Entry != &DxgkAdapterGlobalListHead; Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, DXGKRNL_ADAPTER, GlobalAdapterListEntry) != Adapter)
            continue;
        Found = DxgkReferenceAdapter(Adapter);
        break;
    }
    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);
    return Found;
}

VOID
DxgkDereferenceAdapter(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ExReleaseRundownProtection(&Adapter->RundownRef);
}

BOOLEAN
DxgkBeginDeviceLifecycleOperation(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG ActiveOperations;

    if (Adapter == NULL)
        return FALSE;
    ActiveOperations = InterlockedIncrement(&Adapter->DeviceLifecycleActiveOperations);
    ASSERT(ActiveOperations > 0);
    if (ActiveOperations == 1)
        KeResetEvent(&Adapter->DeviceLifecycleOperationsDrainedEvent);
    KeMemoryBarrier();
    if (InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) == 0)
        return TRUE;
    DxgkEndDeviceLifecycleOperation(Adapter);
    return FALSE;
}

VOID
DxgkEndDeviceLifecycleOperation(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LONG ActiveOperations;

    ASSERT(Adapter != NULL);
    ActiveOperations = InterlockedDecrement(&Adapter->DeviceLifecycleActiveOperations);
    ASSERT(ActiveOperations >= 0);
    if (ActiveOperations == 0)
        KeSetEvent(&Adapter->DeviceLifecycleOperationsDrainedEvent, IO_NO_INCREMENT, FALSE);
}

VOID
DxgkWaitForDeviceLifecycleOperations(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return;
    while (InterlockedCompareExchange(&Adapter->DeviceLifecycleActiveOperations, 0, 0) != 0)
        KeWaitForSingleObject(&Adapter->DeviceLifecycleOperationsDrainedEvent, Executive, KernelMode, FALSE, NULL);
}

ULONG
DxgkReferenceStartedAdapters(
    _Out_writes_(Capacity) PDXGKRNL_ADAPTER *Adapters,
    _In_ ULONG Capacity)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    ULONG Count = 0;

    if (Adapters == NULL || Capacity == 0)
        return 0;
    KeAcquireSpinLock(&DxgkAdapterGlobalListLock, &OldIrql);
    for (Entry = DxgkAdapterGlobalListHead.Flink; Entry != &DxgkAdapterGlobalListHead && Count < Capacity; Entry = Entry->Flink)
    {
        PDXGKRNL_ADAPTER Adapter = CONTAINING_RECORD(Entry, DXGKRNL_ADAPTER, GlobalAdapterListEntry);

        if (DxgkReferenceAdapter(Adapter))
            Adapters[Count++] = Adapter;
    }
    KeReleaseSpinLock(&DxgkAdapterGlobalListLock, OldIrql);
    return Count;
}

NTSTATUS
DxgkCreateAdapterHandle(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS OwnerProcess,
    _Out_ D3DKMT_HANDLE *OutHandle)
{
    NTSTATUS Status;

    if (!DxgkReferenceAdapterObject(Adapter))
        return STATUS_DELETE_PENDING;
    Status = DxgkpCreateHandle(DxgkHandleTypeAdapter, Adapter, Adapter, OwnerProcess, NULL, NULL, TRUE, OutHandle);
    if (!NT_SUCCESS(Status))
        DxgkDereferenceAdapter(Adapter);
    return Status;
}

NTSTATUS
DxgkReferenceAdapterByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter)
{
    PDXGKRNL_HANDLE_ENTRY Entry;
    NTSTATUS Status;

    if (OwnerProcess == NULL || OutAdapter == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAdapter = NULL;
    ExAcquireFastMutex(&DxgkHandleTableLock);
    Entry = DxgkpFindHandleLocked(Handle);
    Status = DxgkpValidateHandleEntry(Entry, Handle, DxgkHandleTypeAdapter, OwnerProcess);
    if (NT_SUCCESS(Status) && !DxgkReferenceAdapter((PDXGKRNL_ADAPTER)Entry->Object))
        Status = STATUS_DELETE_PENDING;
    if (NT_SUCCESS(Status))
        *OutAdapter = (PDXGKRNL_ADAPTER)Entry->Object;
    ExReleaseFastMutex(&DxgkHandleTableLock);
    return Status;
}

NTSTATUS
DxgkCloseAdapterHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter)
{
    PDXGKRNL_HANDLE_ENTRY Entry;
    NTSTATUS Status;

    if (OwnerProcess == NULL || OutAdapter == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAdapter = NULL;
    ExAcquireFastMutex(&DxgkHandleTableLock);
    Entry = DxgkpFindHandleLocked(Handle);
    Status = DxgkpValidateHandleEntry(Entry, Handle, DxgkHandleTypeAdapter, OwnerProcess);
    if (NT_SUCCESS(Status))
    {
        RemoveEntryList(&Entry->ListEntry);
        InitializeListHead(&Entry->ListEntry);
        Entry->OwnsAdapterReference = FALSE;
        *OutAdapter = (PDXGKRNL_ADAPTER)Entry->Object;
    }
    ExReleaseFastMutex(&DxgkHandleTableLock);
    if (NT_SUCCESS(Status))
        DxgkpFreeHandleEntry(Entry);
    return Status;
}

NTSTATUS
DxgkCreateDeviceHandle(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ PEPROCESS OwnerProcess,
    _Out_ D3DKMT_HANDLE *OutHandle)
{
    if (Device == NULL || Device->Adapter == NULL || Device->OwnerProcess != OwnerProcess || InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
        return STATUS_INVALID_PARAMETER;
    return DxgkpCreateHandle(DxgkHandleTypeDevice, Device, Device->Adapter, OwnerProcess, &Device->Destroying, &Device->TeardownClaimed, FALSE, OutHandle);
}

NTSTATUS
DxgkReferenceDeviceByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice)
{
    PDXGKRNL_HANDLE_ENTRY Entry;
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;

    if (OwnerProcess == NULL || OutAdapter == NULL || OutDevice == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAdapter = NULL;
    *OutDevice = NULL;
    ExAcquireFastMutex(&DxgkHandleTableLock);
    Entry = DxgkpFindHandleLocked(Handle);
    Status = DxgkpValidateHandleEntry(Entry, Handle, DxgkHandleTypeDevice, OwnerProcess);
    Device = NT_SUCCESS(Status) ? (PDXGKRNL_DEVICE)Entry->Object : NULL;
    if (NT_SUCCESS(Status) && !DxgkReferenceDevice(Device))
        Status = STATUS_DELETE_PENDING;
    if (NT_SUCCESS(Status))
    {
        *OutAdapter = Device->Adapter;
        *OutDevice = Device;
    }
    ExReleaseFastMutex(&DxgkHandleTableLock);
    return Status;
}

NTSTATUS
DxgkDetachDeviceHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_DEVICE *OutDevice)
{
    PDXGKRNL_HANDLE_ENTRY Entry;
    NTSTATUS Status;

    if (OwnerProcess == NULL || OutDevice == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutDevice = NULL;
    ExAcquireFastMutex(&DxgkHandleTableLock);
    Entry = DxgkpFindHandleLocked(Handle);
    Status = DxgkpValidateHandleEntry(Entry, Handle, DxgkHandleTypeDevice, OwnerProcess);
    if (NT_SUCCESS(Status) && !DxgkBeginDeviceLifecycleOperation(Entry->Adapter))
        Status = STATUS_DELETE_PENDING;
    if (NT_SUCCESS(Status) && !DxgkTryClaimTeardown(Entry->TeardownClaimed))
    {
        DxgkEndDeviceLifecycleOperation(Entry->Adapter);
        Status = STATUS_DELETE_PENDING;
    }
    if (NT_SUCCESS(Status))
    {
        DxgkDeviceBeginDestroy((PDXGKRNL_DEVICE)Entry->Object);
        RemoveEntryList(&Entry->ListEntry);
        InitializeListHead(&Entry->ListEntry);
        *OutDevice = (PDXGKRNL_DEVICE)Entry->Object;
    }
    ExReleaseFastMutex(&DxgkHandleTableLock);
    if (NT_SUCCESS(Status))
        DxgkpFreeHandleEntry(Entry);
    return Status;
}

NTSTATUS
DxgkCreateContextHandle(
    _In_ PDXGKRNL_CONTEXT Context,
    _In_ PEPROCESS OwnerProcess,
    _Out_ D3DKMT_HANDLE *OutHandle)
{
    if (Context == NULL || Context->Device == NULL || Context->Device->OwnerProcess != OwnerProcess || InterlockedCompareExchange(&Context->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Context->Device->Destroying, 0, 0) != 0)
        return STATUS_INVALID_PARAMETER;
    return DxgkpCreateHandle(DxgkHandleTypeContext, Context, Context->Device->Adapter, OwnerProcess, &Context->Destroying, &Context->TeardownClaimed, FALSE, OutHandle);
}

NTSTATUS
DxgkReferenceContextByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_ADAPTER *OutAdapter,
    _Out_ PDXGKRNL_DEVICE *OutDevice,
    _Out_ PDXGKRNL_CONTEXT *OutContext)
{
    PDXGKRNL_HANDLE_ENTRY Entry;
    PDXGKRNL_CONTEXT Context;
    NTSTATUS Status;

    if (OwnerProcess == NULL || OutAdapter == NULL || OutDevice == NULL || OutContext == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutAdapter = NULL;
    *OutDevice = NULL;
    *OutContext = NULL;
    ExAcquireFastMutex(&DxgkHandleTableLock);
    Entry = DxgkpFindHandleLocked(Handle);
    Status = DxgkpValidateHandleEntry(Entry, Handle, DxgkHandleTypeContext, OwnerProcess);
    Context = NT_SUCCESS(Status) ? (PDXGKRNL_CONTEXT)Entry->Object : NULL;
    if (NT_SUCCESS(Status) && !DxgkReferenceContext(Context))
        Status = STATUS_DELETE_PENDING;
    if (NT_SUCCESS(Status))
    {
        *OutAdapter = Context->Device->Adapter;
        *OutDevice = Context->Device;
        *OutContext = Context;
    }
    ExReleaseFastMutex(&DxgkHandleTableLock);
    return Status;
}

static VOID
DxgkpRemoveObjectHandle(
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_ PVOID Object)
{
    PDXGKRNL_HANDLE_ENTRY Found = NULL;
    PLIST_ENTRY Entry;

    if (!DxgkHandleTableInitialized || Object == NULL)
        return;
    ExAcquireFastMutex(&DxgkHandleTableLock);
    for (Entry = DxgkHandleTableHead.Flink; Entry != &DxgkHandleTableHead; Entry = Entry->Flink)
    {
        PDXGKRNL_HANDLE_ENTRY HandleEntry = CONTAINING_RECORD(Entry, DXGKRNL_HANDLE_ENTRY, ListEntry);

        if (HandleEntry->Type != Type || HandleEntry->Object != Object)
            continue;
        RemoveEntryList(&HandleEntry->ListEntry);
        InitializeListHead(&HandleEntry->ListEntry);
        Found = HandleEntry;
        break;
    }
    ExReleaseFastMutex(&DxgkHandleTableLock);
    if (Found != NULL)
        DxgkpFreeHandleEntry(Found);
}

NTSTATUS
DxgkCreateOwnedHandle(
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_ PVOID Object,
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PEPROCESS OwnerProcess,
    _In_opt_ volatile LONG *Destroying,
    _Inout_ volatile LONG *TeardownClaimed,
    _Out_ D3DKMT_HANDLE *OutHandle)
{
    if (Type < DxgkHandleTypeSynchronizationObject || Type > DxgkHandleTypeKeyedMutex)
        return STATUS_INVALID_PARAMETER;
    if (TeardownClaimed == NULL)
        return STATUS_INVALID_PARAMETER;
    return DxgkpCreateHandle(Type, Object, Adapter, OwnerProcess, Destroying, TeardownClaimed, FALSE, OutHandle);
}

NTSTATUS
DxgkReferenceOwnedHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_opt_ PEPROCESS OwnerProcess,
    _In_ PDXGKRNL_HANDLE_REFERENCE_ROUTINE ReferenceRoutine,
    _Out_ PVOID *OutObject)
{
    PDXGKRNL_HANDLE_ENTRY Entry;
    NTSTATUS Status;

    if (ReferenceRoutine == NULL || OutObject == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutObject = NULL;
    ExAcquireFastMutex(&DxgkHandleTableLock);
    Entry = DxgkpFindHandleLocked(Handle);
    if (OwnerProcess != NULL)
        Status = DxgkpValidateHandleEntry(Entry, Handle, Type, OwnerProcess);
    else if (DxgkpHandleType(Handle) != Type)
        Status = STATUS_INVALID_PARAMETER;
    else if (Entry == NULL || Entry->Type != Type)
        Status = STATUS_INVALID_PARAMETER;
    else
        Status = STATUS_SUCCESS;
    if (NT_SUCCESS(Status) && !ReferenceRoutine(Entry->Object))
        Status = STATUS_DELETE_PENDING;
    if (NT_SUCCESS(Status))
        *OutObject = Entry->Object;
    ExReleaseFastMutex(&DxgkHandleTableLock);
    return Status;
}

NTSTATUS
DxgkDetachOwnedHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PVOID *OutObject)
{
    PDXGKRNL_HANDLE_ENTRY Entry;
    NTSTATUS Status;

    if (OwnerProcess == NULL || OutObject == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutObject = NULL;
    ExAcquireFastMutex(&DxgkHandleTableLock);
    Entry = DxgkpFindHandleLocked(Handle);
    Status = DxgkpValidateHandleEntry(Entry, Handle, Type, OwnerProcess);
    if (NT_SUCCESS(Status) && !DxgkTryClaimTeardown(Entry->TeardownClaimed))
        Status = STATUS_DELETE_PENDING;
    if (NT_SUCCESS(Status))
    {
        if (Entry->Destroying != NULL)
            InterlockedExchange(Entry->Destroying, 1);
        RemoveEntryList(&Entry->ListEntry);
        InitializeListHead(&Entry->ListEntry);
        *OutObject = Entry->Object;
    }
    ExReleaseFastMutex(&DxgkHandleTableLock);
    if (NT_SUCCESS(Status))
        DxgkpFreeHandleEntry(Entry);
    return Status;
}

VOID
DxgkRemoveOwnedHandleObject(
    _In_ DXGKRNL_HANDLE_TYPE Type,
    _In_ PVOID Object)
{
    DxgkpRemoveObjectHandle(Type, Object);
}

VOID
DxgkRemoveDeviceHandleObject(
    _In_ PDXGKRNL_DEVICE Device)
{
    DxgkpRemoveObjectHandle(DxgkHandleTypeDevice, Device);
}

VOID
DxgkRemoveContextHandleObject(
    _In_ PDXGKRNL_CONTEXT Context)
{
    DxgkpRemoveObjectHandle(DxgkHandleTypeContext, Context);
}

NTSTATUS
DxgkDetachContextHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_CONTEXT *OutContext)
{
    PDXGKRNL_HANDLE_ENTRY Entry;
    NTSTATUS Status;

    if (OwnerProcess == NULL || OutContext == NULL)
        return STATUS_INVALID_PARAMETER;
    *OutContext = NULL;
    ExAcquireFastMutex(&DxgkHandleTableLock);
    Entry = DxgkpFindHandleLocked(Handle);
    Status = DxgkpValidateHandleEntry(Entry, Handle, DxgkHandleTypeContext, OwnerProcess);
    if (NT_SUCCESS(Status) && !DxgkTryClaimTeardown(Entry->TeardownClaimed))
        Status = STATUS_DELETE_PENDING;
    if (NT_SUCCESS(Status))
    {
        InterlockedExchange(&((PDXGKRNL_CONTEXT)Entry->Object)->Destroying, 1);
        RemoveEntryList(&Entry->ListEntry);
        InitializeListHead(&Entry->ListEntry);
        *OutContext = (PDXGKRNL_CONTEXT)Entry->Object;
    }
    ExReleaseFastMutex(&DxgkHandleTableLock);
    if (NT_SUCCESS(Status))
        DxgkpFreeHandleEntry(Entry);
    return Status;
}

static VOID
DxgkpPurgeHandles(
    _In_opt_ PEPROCESS Process,
    _In_opt_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY Retired;
    PLIST_ENTRY Entry;

    InitializeListHead(&Retired);
    ExAcquireFastMutex(&DxgkHandleTableLock);
    Entry = DxgkHandleTableHead.Flink;
    while (Entry != &DxgkHandleTableHead)
    {
        PDXGKRNL_HANDLE_ENTRY HandleEntry = CONTAINING_RECORD(Entry, DXGKRNL_HANDLE_ENTRY, ListEntry);
        PLIST_ENTRY Next = Entry->Flink;

        if ((Process == NULL || HandleEntry->OwnerProcess == Process) && (Adapter == NULL || HandleEntry->Adapter == Adapter))
        {
            if (HandleEntry->Type == DxgkHandleTypeDevice)
                DxgkDeviceBeginDestroy((PDXGKRNL_DEVICE)HandleEntry->Object);
            else if (HandleEntry->Type == DxgkHandleTypeContext)
                InterlockedExchange(&((PDXGKRNL_CONTEXT)HandleEntry->Object)->Destroying, 1);
            RemoveEntryList(&HandleEntry->ListEntry);
            InsertTailList(&Retired, &HandleEntry->ListEntry);
        }
        Entry = Next;
    }
    ExReleaseFastMutex(&DxgkHandleTableLock);

    while (!IsListEmpty(&Retired))
    {
        PDXGKRNL_HANDLE_ENTRY HandleEntry = CONTAINING_RECORD(RemoveHeadList(&Retired), DXGKRNL_HANDLE_ENTRY, ListEntry);

        DxgkpFreeHandleEntry(HandleEntry);
    }
}

VOID
DxgkPurgeProcessHandles(
    _In_ PEPROCESS Process)
{
    if (DxgkHandleTableInitialized && Process != NULL)
        DxgkpPurgeHandles(Process, NULL);
}

VOID
DxgkPurgeAdapterHandles(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (DxgkHandleTableInitialized && Adapter != NULL)
        DxgkpPurgeHandles(NULL, Adapter);
}

static VOID
DxgkpPurgeAdapterOpenHandles(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    LIST_ENTRY Retired;
    PLIST_ENTRY Entry;

    InitializeListHead(&Retired);
    ExAcquireFastMutex(&DxgkHandleTableLock);
    Entry = DxgkHandleTableHead.Flink;
    while (Entry != &DxgkHandleTableHead)
    {
        PDXGKRNL_HANDLE_ENTRY HandleEntry = CONTAINING_RECORD(Entry, DXGKRNL_HANDLE_ENTRY, ListEntry);
        PLIST_ENTRY Next = Entry->Flink;

        if (HandleEntry->Adapter == Adapter && HandleEntry->Type == DxgkHandleTypeAdapter)
        {
            RemoveEntryList(&HandleEntry->ListEntry);
            InsertTailList(&Retired, &HandleEntry->ListEntry);
        }
        Entry = Next;
    }
    ExReleaseFastMutex(&DxgkHandleTableLock);
    while (!IsListEmpty(&Retired))
    {
        PDXGKRNL_HANDLE_ENTRY HandleEntry = CONTAINING_RECORD(RemoveHeadList(&Retired), DXGKRNL_HANDLE_ENTRY, ListEntry);

        DxgkpFreeHandleEntry(HandleEntry);
    }
}

VOID
DxgkBeginAdapterRundown(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return;
    if (InterlockedCompareExchange(&Adapter->RundownStarted, 1, 0) != 0)
        return;
    if (Adapter->State == DxgkAdapterStateStarted)
        InterlockedCompareExchange((volatile LONG *)&Adapter->State, DxgkAdapterStateStopping, DxgkAdapterStateStarted);
    DxgkpPurgeAdapterOpenHandles(Adapter);
}

VOID
DxgkWaitForAdapterRundown(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter != NULL && InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) != 0)
        ExWaitForRundownProtectionRelease(&Adapter->RundownRef);
}

VOID
DxgkReinitializeAdapterRundown(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || InterlockedCompareExchange(&Adapter->RundownStarted, 0, 0) == 0)
        return;
    ExReInitializeRundownProtection(&Adapter->RundownRef);
    InterlockedExchange(&Adapter->RundownStarted, 0);
}
