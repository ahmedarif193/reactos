/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Keyed mutex objects
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * A keyed mutex is how two processes hand a shared surface back and forth: the
 * releaser names the key the next acquirer must ask for, so only the intended
 * partner wakes.  The object lives in a global namespace because the point is
 * to be opened from a process other than the creator's.
 *
 * The state transitions live in keyedmutex_core.c so they can be tested without
 * a GPU; this file owns lifetime, the global namespace, and the wait.
 */

#include "dxgkrnl_private.h"
#include "keyedmutex_core.h"

#define TAG_DXGK_KEYEDMUTEX 'MKxD'

typedef struct _DXGKRNL_KEYED_MUTEX
{
    LIST_ENTRY GlobalLink;
    volatile LONG ReferenceCount;
    PDXGKRNL_ADAPTER Adapter;
    D3DKMT_HANDLE SharedHandle;
    FAST_MUTEX Lock;
    KEVENT StateChanged;
    DXGK_KEYED_MUTEX_STATE State;
    PVOID PrivateRuntimeData;
    ULONG PrivateRuntimeDataSize;
} DXGKRNL_KEYED_MUTEX, *PDXGKRNL_KEYED_MUTEX;

/*
 * One of these per handle.  The shared state cannot carry the Destroying flag
 * itself: the handle table treats a set flag as "this object is going away", so
 * a process closing its own handle would make every other process's handle to
 * the same mutex fail with STATUS_DELETE_PENDING.  Each handle gets its own
 * lifetime bits and holds one reference to the state it names.
 */
typedef struct _DXGKRNL_KEYED_MUTEX_REF
{
    volatile LONG ReferenceCount;
    volatile LONG Destroying;
    volatile LONG TeardownClaimed;
    PDXGKRNL_KEYED_MUTEX Mutex;
} DXGKRNL_KEYED_MUTEX_REF, *PDXGKRNL_KEYED_MUTEX_REF;

static LIST_ENTRY DxgkKeyedMutexList;
static FAST_MUTEX DxgkKeyedMutexListLock;
static volatile LONG DxgkKeyedMutexNextSharedId;
static BOOLEAN DxgkKeyedMutexReady;

NTSTATUS
DxgkKeyedMutexInitialize(VOID)
{
    InitializeListHead(&DxgkKeyedMutexList);
    ExInitializeFastMutex(&DxgkKeyedMutexListLock);
    DxgkKeyedMutexNextSharedId = 0;
    DxgkKeyedMutexReady = TRUE;
    return STATUS_SUCCESS;
}

VOID
DxgkKeyedMutexUninitialize(VOID)
{
    DxgkKeyedMutexReady = FALSE;
}

static BOOLEAN
DxgkpReferenceKeyedMutex(
    _In_ PVOID Object)
{
    PDXGKRNL_KEYED_MUTEX Mutex = Object;
    LONG Current;

    /* Refuse to resurrect one that already reached zero, and refuse to hand a
     * reference to a caller racing a destroy. */
    for (;;)
    {
        Current = InterlockedCompareExchange(&Mutex->ReferenceCount, 0, 0);
        if (Current <= 0)
            return FALSE;
        if (InterlockedCompareExchange(&Mutex->ReferenceCount, Current + 1, Current) == Current)
            return TRUE;
    }
}

static VOID
DxgkpDereferenceKeyedMutex(
    _Inout_ PDXGKRNL_KEYED_MUTEX Mutex)
{
    if (InterlockedDecrement(&Mutex->ReferenceCount) != 0)
        return;

    ExAcquireFastMutex(&DxgkKeyedMutexListLock);
    if (!IsListEmpty(&Mutex->GlobalLink))
    {
        RemoveEntryList(&Mutex->GlobalLink);
        InitializeListHead(&Mutex->GlobalLink);
    }
    ExReleaseFastMutex(&DxgkKeyedMutexListLock);

    if (Mutex->PrivateRuntimeData != NULL)
        ExFreePoolWithTag(Mutex->PrivateRuntimeData, TAG_DXGK_KEYEDMUTEX);
    if (Mutex->Adapter != NULL)
        DxgkDereferenceAdapter(Mutex->Adapter);
    ExFreePoolWithTag(Mutex, TAG_DXGK_KEYEDMUTEX);
}

/* The namespace a keyed mutex is opened through.  Distinct from the per-process
 * handle table: the whole point is to be reachable from another process. */
static PDXGKRNL_KEYED_MUTEX
DxgkpFindKeyedMutexBySharedHandle(
    _In_ D3DKMT_HANDLE SharedHandle)
{
    PDXGKRNL_KEYED_MUTEX Found = NULL;
    PLIST_ENTRY Entry;

    if (SharedHandle == 0)
        return NULL;
    ExAcquireFastMutex(&DxgkKeyedMutexListLock);
    for (Entry = DxgkKeyedMutexList.Flink; Entry != &DxgkKeyedMutexList; Entry = Entry->Flink)
    {
        PDXGKRNL_KEYED_MUTEX Mutex = CONTAINING_RECORD(Entry, DXGKRNL_KEYED_MUTEX, GlobalLink);

        if (Mutex->SharedHandle == SharedHandle && DxgkpReferenceKeyedMutex(Mutex))
        {
            Found = Mutex;
            break;
        }
    }
    ExReleaseFastMutex(&DxgkKeyedMutexListLock);
    return Found;
}

static BOOLEAN
DxgkpReferenceKeyedMutexRef(
    _In_ PVOID Object)
{
    PDXGKRNL_KEYED_MUTEX_REF Ref = Object;
    LONG Current;

    for (;;)
    {
        Current = InterlockedCompareExchange(&Ref->ReferenceCount, 0, 0);
        if (Current <= 0)
            return FALSE;
        if (InterlockedCompareExchange(&Ref->ReferenceCount, Current + 1, Current) == Current)
            return TRUE;
    }
}

static VOID
DxgkpDereferenceKeyedMutexRef(
    _Inout_ PDXGKRNL_KEYED_MUTEX_REF Ref)
{
    if (InterlockedDecrement(&Ref->ReferenceCount) != 0)
        return;
    DxgkpDereferenceKeyedMutex(Ref->Mutex);
    ExFreePoolWithTag(Ref, TAG_DXGK_KEYEDMUTEX);
}

/* Publishes a handle naming Mutex.  On success the handle owns the caller's
 * reference to Mutex; on failure the caller still owns it. */
static NTSTATUS
DxgkpPublishKeyedMutexHandle(
    _In_ PDXGKRNL_KEYED_MUTEX Mutex,
    _Out_ D3DKMT_HANDLE *OutHandle)
{
    PDXGKRNL_KEYED_MUTEX_REF Ref;
    NTSTATUS Status;

    Ref = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Ref), TAG_DXGK_KEYEDMUTEX);
    if (Ref == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Ref, sizeof(*Ref));
    Ref->ReferenceCount = 1;
    Ref->Mutex = Mutex;

    Status = DxgkCreateOwnedHandle(DxgkHandleTypeKeyedMutex, Ref, Mutex->Adapter, PsGetCurrentProcess(),
                                   &Ref->Destroying, &Ref->TeardownClaimed, OutHandle);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Ref, TAG_DXGK_KEYEDMUTEX);
        return Status;
    }
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpReferenceKeyedMutexByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _Out_ PDXGKRNL_KEYED_MUTEX *OutMutex)
{
    PVOID Object = NULL;
    PDXGKRNL_KEYED_MUTEX_REF Ref;
    NTSTATUS Status;

    *OutMutex = NULL;
    Status = DxgkReferenceOwnedHandle(Handle, DxgkHandleTypeKeyedMutex, PsGetCurrentProcess(), DxgkpReferenceKeyedMutexRef, &Object);
    if (!NT_SUCCESS(Status))
        return Status;
    Ref = (PDXGKRNL_KEYED_MUTEX_REF)Object;
    /* Take a reference on the state itself, then drop the alias one: callers
     * only ever hold the state, and it must outlive the handle they came in on. */
    if (!DxgkpReferenceKeyedMutex(Ref->Mutex))
    {
        DxgkpDereferenceKeyedMutexRef(Ref);
        return STATUS_DEVICE_REMOVED;
    }
    *OutMutex = Ref->Mutex;
    DxgkpDereferenceKeyedMutexRef(Ref);
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpCaptureKeyedMutexPrivateData(
    _In_opt_ PVOID UserBuffer,
    _In_ ULONG Size,
    _In_ KPROCESSOR_MODE AccessMode,
    _Outptr_result_maybenull_ PVOID *OutBuffer)
{
    PVOID Buffer;
    NTSTATUS Status;

    *OutBuffer = NULL;
    if (!DxgkKeyedMutexCorePrivateDataSizeValid(Size, UserBuffer != NULL))
        return STATUS_INVALID_PARAMETER;
    if (Size == 0)
        return STATUS_SUCCESS;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_DXGK_KEYEDMUTEX);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = DxgkpCopyFromUserBuffer(Buffer, UserBuffer, Size, AccessMode);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_DXGK_KEYEDMUTEX);
        return Status;
    }
    *OutBuffer = Buffer;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpCreateKeyedMutexObject(
    _In_ UINT64 InitialValue,
    _In_opt_ PVOID PrivateData,
    _In_ ULONG PrivateDataSize,
    _Out_ PDXGKRNL_KEYED_MUTEX *OutMutex,
    _Out_ D3DKMT_HANDLE *OutHandle,
    _Out_ D3DKMT_HANDLE *OutSharedHandle)
{
    PDXGKRNL_ADAPTER Adapters[MAX_ENUM_ADAPTERS];
    PDXGKRNL_KEYED_MUTEX Mutex;
    ULONG AdapterCount;
    ULONG Index;
    NTSTATUS Status;

    *OutMutex = NULL;
    *OutHandle = 0;
    *OutSharedHandle = 0;

    if (!DxgkKeyedMutexReady)
        return STATUS_DEVICE_NOT_READY;

    /*
     * A keyed mutex is not bound to a device, but the handle table scopes every
     * entry to an adapter so teardown can find it.  Any started adapter serves.
     */
    AdapterCount = DxgkReferenceStartedAdapters(Adapters, MAX_ENUM_ADAPTERS);
    if (AdapterCount == 0)
        return STATUS_DEVICE_REMOVED;
    for (Index = 1; Index < AdapterCount; ++Index)
        DxgkDereferenceAdapter(Adapters[Index]);

    Mutex = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Mutex), TAG_DXGK_KEYEDMUTEX);
    if (Mutex == NULL)
    {
        DxgkDereferenceAdapter(Adapters[0]);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Mutex, sizeof(*Mutex));
    InitializeListHead(&Mutex->GlobalLink);
    Mutex->ReferenceCount = 1;
    Mutex->Adapter = Adapters[0];
    ExInitializeFastMutex(&Mutex->Lock);
    KeInitializeEvent(&Mutex->StateChanged, NotificationEvent, FALSE);
    DxgkKeyedMutexCoreInitialize(&Mutex->State, InitialValue);
    Mutex->PrivateRuntimeData = PrivateData;
    Mutex->PrivateRuntimeDataSize = PrivateDataSize;
    Mutex->SharedHandle = (D3DKMT_HANDLE)(ULONG)InterlockedIncrement(&DxgkKeyedMutexNextSharedId);

    Status = DxgkpPublishKeyedMutexHandle(Mutex, OutHandle);
    if (!NT_SUCCESS(Status))
    {
        Mutex->PrivateRuntimeData = NULL;   /* the caller still owns it on failure */
        DxgkpDereferenceKeyedMutex(Mutex);
        return Status;
    }

    ExAcquireFastMutex(&DxgkKeyedMutexListLock);
    InsertTailList(&DxgkKeyedMutexList, &Mutex->GlobalLink);
    ExReleaseFastMutex(&DxgkKeyedMutexListLock);

    *OutMutex = Mutex;
    *OutSharedHandle = Mutex->SharedHandle;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkCreateKeyedMutex(
    _Inout_ D3DKMT_CREATEKEYEDMUTEX *pData)
{
    PDXGKRNL_KEYED_MUTEX Mutex;
    D3DKMT_HANDLE Handle;
    D3DKMT_HANDLE SharedHandle;
    NTSTATUS Status;

    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpCreateKeyedMutexObject(pData->InitialValue, NULL, 0, &Mutex, &Handle, &SharedHandle);
    if (!NT_SUCCESS(Status))
        return Status;
    pData->hKeyedMutex = Handle;
    pData->hSharedHandle = SharedHandle;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkCreateKeyedMutex2(
    _Inout_ D3DKMT_CREATEKEYEDMUTEX2 *pData,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PDXGKRNL_KEYED_MUTEX Mutex;
    PVOID PrivateData = NULL;
    D3DKMT_HANDLE Handle;
    D3DKMT_HANDLE SharedHandle;
    NTSTATUS Status;

    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpCaptureKeyedMutexPrivateData(pData->pPrivateRuntimeData, pData->PrivateRuntimeDataSize, AccessMode, &PrivateData);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = DxgkpCreateKeyedMutexObject(pData->InitialValue, PrivateData, pData->PrivateRuntimeDataSize,
                                         &Mutex, &Handle, &SharedHandle);
    if (!NT_SUCCESS(Status))
    {
        if (PrivateData != NULL)
            ExFreePoolWithTag(PrivateData, TAG_DXGK_KEYEDMUTEX);
        return Status;
    }
    pData->hKeyedMutex = Handle;
    /* NtSecuritySharing hands out an NT handle instead of a global one; we do
     * not implement that path, so the global handle is always the answer. */
    pData->hSharedHandle = SharedHandle;
    return STATUS_SUCCESS;
}

static NTSTATUS
DxgkpOpenKeyedMutexCommon(
    _In_ D3DKMT_HANDLE SharedHandle,
    _In_opt_ PVOID PrivateData,
    _In_ ULONG PrivateDataSize,
    _Out_ D3DKMT_HANDLE *OutHandle)
{
    PDXGKRNL_KEYED_MUTEX Mutex;
    NTSTATUS Status;

    *OutHandle = 0;
    Mutex = DxgkpFindKeyedMutexBySharedHandle(SharedHandle);
    if (Mutex == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Private data is only adopted if the mutex does not already carry some;
     * the creator's copy is the authoritative one. */
    if (PrivateData != NULL && PrivateDataSize != 0)
    {
        ExAcquireFastMutex(&Mutex->Lock);
        if (Mutex->PrivateRuntimeData == NULL)
        {
            Mutex->PrivateRuntimeData = PrivateData;
            Mutex->PrivateRuntimeDataSize = PrivateDataSize;
            PrivateData = NULL;
        }
        ExReleaseFastMutex(&Mutex->Lock);
    }
    if (PrivateData != NULL)
        ExFreePoolWithTag(PrivateData, TAG_DXGK_KEYEDMUTEX);

    Status = DxgkpPublishKeyedMutexHandle(Mutex, OutHandle);
    if (!NT_SUCCESS(Status))
    {
        DxgkpDereferenceKeyedMutex(Mutex);
        return Status;
    }
    /* The new handle owns the reference taken by the lookup. */
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkOpenKeyedMutex(
    _Inout_ D3DKMT_OPENKEYEDMUTEX *pData)
{
    D3DKMT_HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpOpenKeyedMutexCommon(pData->hSharedHandle, NULL, 0, &Handle);
    if (!NT_SUCCESS(Status))
        return Status;
    pData->hKeyedMutex = Handle;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkOpenKeyedMutex2(
    _Inout_ D3DKMT_OPENKEYEDMUTEX2 *pData,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PVOID PrivateData = NULL;
    D3DKMT_HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpCaptureKeyedMutexPrivateData(pData->pPrivateRuntimeData, pData->PrivateRuntimeDataSize, AccessMode, &PrivateData);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkpOpenKeyedMutexCommon(pData->hSharedHandle, PrivateData, pData->PrivateRuntimeDataSize, &Handle);
    if (!NT_SUCCESS(Status))
    {
        if (PrivateData != NULL)
            ExFreePoolWithTag(PrivateData, TAG_DXGK_KEYEDMUTEX);
        return Status;
    }
    pData->hKeyedMutex = Handle;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkDestroyKeyedMutex(
    _In_ CONST D3DKMT_DESTROYKEYEDMUTEX *pData)
{
    PVOID Object = NULL;
    PDXGKRNL_KEYED_MUTEX_REF Ref;
    PDXGKRNL_KEYED_MUTEX Mutex;
    NTSTATUS Status;

    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkDetachOwnedHandle(pData->hKeyedMutex, DxgkHandleTypeKeyedMutex, PsGetCurrentProcess(), &Object);
    if (!NT_SUCCESS(Status))
        return Status;

    Ref = (PDXGKRNL_KEYED_MUTEX_REF)Object;
    Mutex = Ref->Mutex;
    InterlockedExchange(&Ref->Destroying, 1);
    /*
     * Wake anyone parked in an acquire so they re-check rather than waiting out
     * a timeout.  The state itself only becomes unusable when the last handle
     * to it goes away, which the reference count decides.
     */
    KeSetEvent(&Mutex->StateChanged, IO_NO_INCREMENT, FALSE);
    DxgkpDereferenceKeyedMutexRef(Ref);
    return STATUS_SUCCESS;
}

/*
 * Waits for the mutex to reach Key.  The deadline is computed once: a relative
 * timeout re-armed on every wake would never expire under a stream of releases
 * to other keys.
 */
static NTSTATUS
DxgkpAcquireKeyedMutexCommon(
    _In_ D3DKMT_HANDLE hKeyedMutex,
    _In_ UINT64 Key,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_ UINT64 *OutFenceValue,
    _Out_opt_ PVOID *OutPrivateData,
    _Out_opt_ ULONG *OutPrivateDataSize)
{
    PDXGKRNL_KEYED_MUTEX Mutex;
    LARGE_INTEGER Deadline;
    BOOLEAN HasDeadline = FALSE;
    NTSTATUS Status;

    *OutFenceValue = 0;
    if (OutPrivateData != NULL)
        *OutPrivateData = NULL;
    if (OutPrivateDataSize != NULL)
        *OutPrivateDataSize = 0;

    Status = DxgkpReferenceKeyedMutexByHandle(hKeyedMutex, &Mutex);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Timeout != NULL)
    {
        if (Timeout->QuadPart < 0)
        {
            KeQuerySystemTime(&Deadline);
            Deadline.QuadPart -= Timeout->QuadPart;   /* relative is negative */
        }
        else
        {
            Deadline = *Timeout;
        }
        HasDeadline = TRUE;
    }

    for (;;)
    {
        LARGE_INTEGER Now;
        BOOLEAN Acquired;

        ExAcquireFastMutex(&Mutex->Lock);
        Acquired = DxgkKeyedMutexCoreAcquire(&Mutex->State, Key);
        if (Acquired)
        {
            *OutFenceValue = Mutex->State.FenceValue;
            if (OutPrivateData != NULL)
            {
                *OutPrivateData = Mutex->PrivateRuntimeData;
                if (OutPrivateDataSize != NULL)
                    *OutPrivateDataSize = Mutex->PrivateRuntimeDataSize;
            }
            ExReleaseFastMutex(&Mutex->Lock);
            Status = STATUS_SUCCESS;
            break;
        }
        /* Clear before dropping the lock so a release that lands in the gap
         * still leaves the event set when the wait begins. */
        KeClearEvent(&Mutex->StateChanged);
        DxgkKeyedMutexCoreAddWaiter(&Mutex->State);
        ExReleaseFastMutex(&Mutex->Lock);

        if (HasDeadline)
        {
            KeQuerySystemTime(&Now);
            if (Now.QuadPart >= Deadline.QuadPart)
            {
                ExAcquireFastMutex(&Mutex->Lock);
                DxgkKeyedMutexCoreRemoveWaiter(&Mutex->State);
                ExReleaseFastMutex(&Mutex->Lock);
                Status = STATUS_TIMEOUT;
                break;
            }
        }

        Status = KeWaitForSingleObject(&Mutex->StateChanged, Executive, KernelMode, FALSE,
                                       HasDeadline ? &Deadline : NULL);
        ExAcquireFastMutex(&Mutex->Lock);
        DxgkKeyedMutexCoreRemoveWaiter(&Mutex->State);
        ExReleaseFastMutex(&Mutex->Lock);
        if (Status == STATUS_TIMEOUT)
            break;
    }

    DxgkpDereferenceKeyedMutex(Mutex);
    return Status;
}

NTSTATUS
DxgkAcquireKeyedMutex(
    _Inout_ D3DKMT_ACQUIREKEYEDMUTEX *pData)
{
    UINT64 FenceValue = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpAcquireKeyedMutexCommon(pData->hKeyedMutex, pData->Key, pData->pTimeout, &FenceValue, NULL, NULL);
    if (NT_SUCCESS(Status))
        pData->FenceValue = FenceValue;
    return Status;
}

NTSTATUS
DxgkAcquireKeyedMutex2(
    _Inout_ D3DKMT_ACQUIREKEYEDMUTEX2 *pData,
    _In_ KPROCESSOR_MODE AccessMode)
{
    UINT64 FenceValue = 0;
    PVOID PrivateData = NULL;
    ULONG PrivateDataSize = 0;
    NTSTATUS Status;

    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->PrivateRuntimeDataSize != 0 && pData->pPrivateRuntimeData == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkpAcquireKeyedMutexCommon(pData->hKeyedMutex, pData->Key, pData->pTimeout, &FenceValue,
                                          &PrivateData, &PrivateDataSize);
    if (!NT_SUCCESS(Status))
        return Status;

    pData->FenceValue = FenceValue;
    if (pData->PrivateRuntimeDataSize != 0)
    {
        ULONG Copy = min(pData->PrivateRuntimeDataSize, PrivateDataSize);

        if (Copy != 0 && PrivateData != NULL)
            Status = DxgkpCopyToUserBuffer(pData->pPrivateRuntimeData, PrivateData, Copy, AccessMode);
    }
    return Status;
}

static NTSTATUS
DxgkpReleaseKeyedMutexCommon(
    _In_ D3DKMT_HANDLE hKeyedMutex,
    _In_ UINT64 Key,
    _In_ UINT64 FenceValue,
    _In_opt_ PVOID PrivateData,
    _In_ ULONG PrivateDataSize)
{
    PDXGKRNL_KEYED_MUTEX Mutex;
    NTSTATUS Status;
    BOOLEAN Released;

    Status = DxgkpReferenceKeyedMutexByHandle(hKeyedMutex, &Mutex);
    if (!NT_SUCCESS(Status))
    {
        if (PrivateData != NULL)
            ExFreePoolWithTag(PrivateData, TAG_DXGK_KEYEDMUTEX);
        return Status;
    }

    ExAcquireFastMutex(&Mutex->Lock);
    Released = DxgkKeyedMutexCoreRelease(&Mutex->State, Key, FenceValue);
    if (Released && PrivateData != NULL)
    {
        if (Mutex->PrivateRuntimeData != NULL)
            ExFreePoolWithTag(Mutex->PrivateRuntimeData, TAG_DXGK_KEYEDMUTEX);
        Mutex->PrivateRuntimeData = PrivateData;
        Mutex->PrivateRuntimeDataSize = PrivateDataSize;
        PrivateData = NULL;
    }
    if (Released)
        KeSetEvent(&Mutex->StateChanged, IO_NO_INCREMENT, FALSE);
    ExReleaseFastMutex(&Mutex->Lock);

    if (PrivateData != NULL)
        ExFreePoolWithTag(PrivateData, TAG_DXGK_KEYEDMUTEX);
    DxgkpDereferenceKeyedMutex(Mutex);
    /* Releasing one nobody holds would hand the surface to a waiter while its
     * real writer is still drawing into it. */
    return Released ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
}

NTSTATUS
DxgkReleaseKeyedMutex(
    _Inout_ D3DKMT_RELEASEKEYEDMUTEX *pData)
{
    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    return DxgkpReleaseKeyedMutexCommon(pData->hKeyedMutex, pData->Key, pData->FenceValue, NULL, 0);
}

NTSTATUS
DxgkReleaseKeyedMutex2(
    _Inout_ D3DKMT_RELEASEKEYEDMUTEX2 *pData,
    _In_ KPROCESSOR_MODE AccessMode)
{
    PVOID PrivateData = NULL;
    NTSTATUS Status;

    PAGED_CODE();

    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = DxgkpCaptureKeyedMutexPrivateData(pData->pPrivateRuntimeData, pData->PrivateRuntimeDataSize, AccessMode, &PrivateData);
    if (!NT_SUCCESS(Status))
        return Status;
    return DxgkpReleaseKeyedMutexCommon(pData->hKeyedMutex, pData->Key, pData->FenceValue,
                                        PrivateData, pData->PrivateRuntimeDataSize);
}

VOID
DxgkKeyedMutexProcessCleanup(
    _In_ PEPROCESS Process)
{
    /*
     * Handle purge frees the table entries; the objects they named still hold
     * the references those entries owned.  Wake any acquire parked on a mutex
     * whose last handle just went away so it does not sit out its timeout.
     */
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(Process);

    if (!DxgkKeyedMutexReady)
        return;
    ExAcquireFastMutex(&DxgkKeyedMutexListLock);
    for (Entry = DxgkKeyedMutexList.Flink; Entry != &DxgkKeyedMutexList; Entry = Entry->Flink)
    {
        PDXGKRNL_KEYED_MUTEX Mutex = CONTAINING_RECORD(Entry, DXGKRNL_KEYED_MUTEX, GlobalLink);

        if (Mutex->State.WaiterCount != 0)
            KeSetEvent(&Mutex->StateChanged, IO_NO_INCREMENT, FALSE);
    }
    ExReleaseFastMutex(&DxgkKeyedMutexListLock);
}

/* EOF */
