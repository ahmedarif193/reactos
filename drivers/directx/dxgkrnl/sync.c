/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU synchronisation objects (fences, semaphores)
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * Implements D3DKMT sync object lifecycle.  For the current display-only
 * DOD path, sync objects track CPU-side fences using KEVENT and an atomic
 * fence counter.  Full GPU-fence integration requires dxgmms1 scheduler.
 */

#include "dxgkrnl_private.h"

/* Sync-object handles are opaque 32-bit ids tracked in a validated list. */
static ULONG DxgkSyncHandleCookie = 0x434E5953; /* "SYNC" */
static LONG DxgkSyncInitialized = 0;
static FAST_MUTEX DxgkSyncListLock;
static LIST_ENTRY DxgkSyncListHead;
static volatile LONG DxgkNextSyncHandle = 0;

FORCEINLINE BOOLEAN
DxgkpIsListEntryLinked(
    _In_ PLIST_ENTRY Entry)
{
    return (Entry->Flink != Entry);
}

static VOID
DxgkpSyncInit(VOID)
{
    if (InterlockedCompareExchange(&DxgkSyncInitialized, 1, 0) != 0)
        return;

    ExInitializeFastMutex(&DxgkSyncListLock);
    InitializeListHead(&DxgkSyncListHead);
}

static D3DKMT_HANDLE
DxgkpAllocateSyncHandle(VOID)
{
    ULONG Sequence;

    do
    {
        Sequence = (ULONG)InterlockedIncrement(&DxgkNextSyncHandle);
    } while (Sequence == 0);

    return (D3DKMT_HANDLE)(Sequence ^ DxgkSyncHandleCookie);
}

static PDXGKRNL_SYNC_OBJECT
DxgkpReferenceSyncObjectByHandle(
    _In_ D3DKMT_HANDLE Handle)
{
    PLIST_ENTRY Entry;
    PDXGKRNL_SYNC_OBJECT Found = NULL;

    if (Handle == 0)
        return NULL;

    DxgkpSyncInit();

    ExAcquireFastMutex(&DxgkSyncListLock);
    for (Entry = DxgkSyncListHead.Flink;
         Entry != &DxgkSyncListHead;
         Entry = Entry->Flink)
    {
        PDXGKRNL_SYNC_OBJECT Candidate;

        Candidate = CONTAINING_RECORD(Entry, DXGKRNL_SYNC_OBJECT,
                                      SyncObjListEntry);
        if (Candidate->Handle == Handle)
        {
            InterlockedIncrement(&Candidate->RefCount);
            Found = Candidate;
            break;
        }
    }
    ExReleaseFastMutex(&DxgkSyncListLock);

    return Found;
}

static VOID
DxgkpDereferenceSyncObject(
    _In_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    if (SyncObj != NULL &&
        InterlockedDecrement(&SyncObj->RefCount) == 0)
    {
        ExFreePoolWithTag(SyncObj, TAG_DXGK_SYNC);
    }
}

static PDXGKRNL_SYNC_OBJECT
DxgkpUnlinkSyncObject(
    _In_ D3DKMT_HANDLE Handle)
{
    PLIST_ENTRY Entry;
    PDXGKRNL_SYNC_OBJECT Found = NULL;

    if (Handle == 0)
        return NULL;

    DxgkpSyncInit();

    ExAcquireFastMutex(&DxgkSyncListLock);
    for (Entry = DxgkSyncListHead.Flink;
         Entry != &DxgkSyncListHead;
         Entry = Entry->Flink)
    {
        PDXGKRNL_SYNC_OBJECT Candidate;

        Candidate = CONTAINING_RECORD(Entry, DXGKRNL_SYNC_OBJECT,
                                      SyncObjListEntry);
        if (Candidate->Handle == Handle)
        {
            RemoveEntryList(&Candidate->SyncObjListEntry);
            InitializeListHead(&Candidate->SyncObjListEntry);
            Candidate->Handle = 0;
            Found = Candidate;
            break;
        }
    }
    ExReleaseFastMutex(&DxgkSyncListLock);

    return Found;
}

/*
 * DxgkCreateSynchronizationObject
 *
 * Creates a CPU-side sync object (fence) and links it to the owning device.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkCreateSynchronizationObject(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT *pCreateSyncObject)
{
    PDXGKRNL_ADAPTER     Adapter;
    PDXGKRNL_DEVICE      Device;
    PDXGKRNL_SYNC_OBJECT SyncObj;

    PAGED_CODE();

    if (pCreateSyncObject == NULL)
        return STATUS_INVALID_PARAMETER;

    DxgkpSyncInit();

    Device = DxgkLookupDeviceByHandle(pCreateSyncObject->hDevice, &Adapter);
    if (Device == NULL || Adapter == NULL)
        return STATUS_INVALID_HANDLE;

    if (pCreateSyncObject->Info.Type != D3DDDI_SYNCHRONIZATION_MUTEX &&
        pCreateSyncObject->Info.Type != D3DDDI_SEMAPHORE &&
        pCreateSyncObject->Info.Type != D3DDDI_FENCE &&
        pCreateSyncObject->Info.Type != D3DDDI_CPU_NOTIFICATION)
    {
        return STATUS_NOT_SUPPORTED;
    }

    SyncObj = (PDXGKRNL_SYNC_OBJECT)ExAllocatePoolWithTag(
                  NonPagedPool, sizeof(DXGKRNL_SYNC_OBJECT), TAG_DXGK_SYNC);
    if (SyncObj == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(SyncObj, sizeof(*SyncObj));
    SyncObj->hDevice = Device->Handle;
    SyncObj->Info = pCreateSyncObject->Info;
    SyncObj->RefCount = 1;
    SyncObj->FenceValue = 0;
    KeInitializeEvent(&SyncObj->CpuEvent, SynchronizationEvent, FALSE);
    InitializeListHead(&SyncObj->SyncObjListEntry);
    InitializeListHead(&SyncObj->DeviceSyncObjListEntry);

    SyncObj->Handle = DxgkpAllocateSyncHandle();

    ExAcquireFastMutex(&DxgkSyncListLock);
    InsertTailList(&DxgkSyncListHead, &SyncObj->SyncObjListEntry);
    ExReleaseFastMutex(&DxgkSyncListLock);

    ExAcquireFastMutex(&Device->DeviceMutex);
    InsertTailList(&Device->SyncObjListHead, &SyncObj->DeviceSyncObjListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);

    pCreateSyncObject->hSyncObject = SyncObj->Handle;

    DXGKRNL_TRACE("DxgkCreateSynchronizationObject: handle=0x%X type=%d\n",
                  SyncObj->Handle, SyncObj->Info.Type);
    return STATUS_SUCCESS;
}

/*
 * DxgkDestroySynchronizationObject
 *
 * Destroys a previously created sync object.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkDestroySynchronizationObject(
    _In_ D3DKMT_DESTROYSYNCHRONIZATIONOBJECT *pDestroySyncObject)
{
    PDXGKRNL_ADAPTER     Adapter;
    PDXGKRNL_DEVICE      Device;
    PDXGKRNL_SYNC_OBJECT SyncObj;

    PAGED_CODE();

    if (pDestroySyncObject == NULL)
        return STATUS_INVALID_PARAMETER;

    SyncObj = DxgkpUnlinkSyncObject(pDestroySyncObject->hSyncObject);
    if (SyncObj == NULL)
        return STATUS_INVALID_HANDLE;

    Device = DxgkLookupDeviceByHandle(SyncObj->hDevice, &Adapter);
    if (Device != NULL)
    {
        ExAcquireFastMutex(&Device->DeviceMutex);
        if (DxgkpIsListEntryLinked(&SyncObj->DeviceSyncObjListEntry))
        {
            RemoveEntryList(&SyncObj->DeviceSyncObjListEntry);
            InitializeListHead(&SyncObj->DeviceSyncObjListEntry);
        }
        ExReleaseFastMutex(&Device->DeviceMutex);
    }

    SyncObj->hDevice = 0;

    DXGKRNL_TRACE("DxgkDestroySynchronizationObject: handle=0x%X\n",
                  pDestroySyncObject->hSyncObject);

    DxgkpDereferenceSyncObject(SyncObj);
    return STATUS_SUCCESS;
}

/*
 * DxgkSignalSynchronizationObject
 *
 * Signals a sync object by incrementing its fence value and waking waiters.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkSignalSynchronizationObject(
    _In_ D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *pSignalSyncObject)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE  Device;
    PDXGKRNL_SYNC_OBJECT SyncObjs[D3DDDI_MAX_OBJECT_SIGNALED];
    D3DKMT_HANDLE DeviceHandle;
    ULONG i;
    ULONG CleanupIndex;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pSignalSyncObject == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pSignalSyncObject->ObjectCount == 0 ||
        pSignalSyncObject->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DxgkLookupContextByHandle(pSignalSyncObject->hContext,
                                  &Adapter,
                                  &Device) == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    DeviceHandle = Device->Handle;
    RtlZeroMemory(SyncObjs, sizeof(SyncObjs));

    for (i = 0; i < pSignalSyncObject->ObjectCount; ++i)
    {
        PDXGKRNL_SYNC_OBJECT SyncObj;

        SyncObj = DxgkpReferenceSyncObjectByHandle(
                      pSignalSyncObject->ObjectHandleArray[i]);
        if (SyncObj == NULL)
        {
            Status = STATUS_INVALID_HANDLE;
            goto CleanupReferences;
        }
        if (SyncObj->hDevice != DeviceHandle)
        {
            DxgkpDereferenceSyncObject(SyncObj);
            Status = STATUS_INVALID_HANDLE;
            goto CleanupReferences;
        }

        SyncObjs[i] = SyncObj;
    }

    for (i = 0; i < pSignalSyncObject->ObjectCount; ++i)
    {
        PDXGKRNL_SYNC_OBJECT SyncObj = SyncObjs[i];
        InterlockedIncrement64(&SyncObj->FenceValue);
        KeSetEvent(&SyncObj->CpuEvent, IO_NO_INCREMENT, FALSE);
        DxgkpDereferenceSyncObject(SyncObj);
        SyncObjs[i] = NULL;
    }

    return STATUS_SUCCESS;

CleanupReferences:
    for (CleanupIndex = 0;
         CleanupIndex < pSignalSyncObject->ObjectCount;
         ++CleanupIndex)
    {
        if (SyncObjs[CleanupIndex] != NULL)
            DxgkpDereferenceSyncObject(SyncObjs[CleanupIndex]);
    }

    return Status;
}

/*
 * DxgkWaitForSynchronizationObject
 *
 * Waits for a sync object's fence value to reach the requested level.
 * Currently implements CPU-side waiting only.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkWaitForSynchronizationObject(
    _In_ D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *pWaitSyncObject)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE  Device;
    PDXGKRNL_SYNC_OBJECT SyncObjs[D3DDDI_MAX_OBJECT_WAITED_ON];
    D3DKMT_HANDLE DeviceHandle;
    ULONG i;
    ULONG CleanupIndex;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (pWaitSyncObject == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pWaitSyncObject->ObjectCount == 0 ||
        pWaitSyncObject->ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DxgkLookupContextByHandle(pWaitSyncObject->hContext,
                                  &Adapter,
                                  &Device) == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    DeviceHandle = Device->Handle;
    RtlZeroMemory(SyncObjs, sizeof(SyncObjs));

    for (i = 0; i < pWaitSyncObject->ObjectCount; ++i)
    {
        PDXGKRNL_SYNC_OBJECT SyncObj;

        SyncObj = DxgkpReferenceSyncObjectByHandle(
                      pWaitSyncObject->ObjectHandleArray[i]);
        if (SyncObj == NULL)
        {
            Status = STATUS_INVALID_HANDLE;
            goto CleanupReferences;
        }
        if (SyncObj->hDevice != DeviceHandle)
        {
            DxgkpDereferenceSyncObject(SyncObj);
            Status = STATUS_INVALID_HANDLE;
            goto CleanupReferences;
        }

        SyncObjs[i] = SyncObj;
    }

    for (i = 0; i < pWaitSyncObject->ObjectCount; ++i)
    {
        PDXGKRNL_SYNC_OBJECT SyncObj = SyncObjs[i];
        /*
         * For CPU-side fences: wait on the event with a short timeout.
         * A full implementation would integrate with the GPU scheduler to
         * do GPU-side waits (inserting fence waits into the command stream).
         */
        if (SyncObj->FenceValue == 0)
        {
            LARGE_INTEGER Timeout;
            Timeout.QuadPart = -10LL * 1000LL * 100LL; /* 100ms */
            Status = KeWaitForSingleObject(&SyncObj->CpuEvent,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           &Timeout);
        }
        DxgkpDereferenceSyncObject(SyncObj);
        SyncObjs[i] = NULL;

        if (Status != STATUS_SUCCESS)
            goto CleanupReferences;
    }

    return STATUS_SUCCESS;

CleanupReferences:
    for (CleanupIndex = 0;
         CleanupIndex < pWaitSyncObject->ObjectCount;
         ++CleanupIndex)
    {
        if (SyncObjs[CleanupIndex] != NULL)
            DxgkpDereferenceSyncObject(SyncObjs[CleanupIndex]);
    }

    return Status;
}

/*
 * DxgkSyncObjectCpuSignal / DxgkSyncObjectCpuWait
 *
 * Monitored-fence CPU signal/wait primitives backing
 * D3DKMTSignalSynchronizationObjectFromCpu / WaitForSynchronizationObjectFromCpu.
 * They operate on a single sync object by handle so the per-object array
 * marshalling (and user-pointer SEH capture) stays in the d3dkmt.c dispatch.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkSyncObjectCpuSignal(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 FenceValue)
{
    PDXGKRNL_SYNC_OBJECT SyncObj;

    PAGED_CODE();

    SyncObj = DxgkpReferenceSyncObjectByHandle(hSyncObject);
    if (SyncObj == NULL)
        return STATUS_INVALID_HANDLE;

    if (hDevice != 0 && SyncObj->hDevice != hDevice)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_INVALID_HANDLE;
    }

    InterlockedExchange64(&SyncObj->FenceValue, (LONG64)FenceValue);
    KeSetEvent(&SyncObj->CpuEvent, IO_NO_INCREMENT, FALSE);
    DxgkpDereferenceSyncObject(SyncObj);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkSyncObjectCpuWait(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 FenceValue,
    _In_ BOOLEAN NonBlocking)
{
    PDXGKRNL_SYNC_OBJECT SyncObj;

    PAGED_CODE();

    SyncObj = DxgkpReferenceSyncObjectByHandle(hSyncObject);
    if (SyncObj == NULL)
        return STATUS_INVALID_HANDLE;

    if (hDevice != 0 && SyncObj->hDevice != hDevice)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_INVALID_HANDLE;
    }

    if ((UINT64)SyncObj->FenceValue < FenceValue && !NonBlocking)
    {
        LARGE_INTEGER Timeout;
        Timeout.QuadPart = -10LL * 1000LL * 100LL; /* 100ms bound */
        KeWaitForSingleObject(&SyncObj->CpuEvent, Executive, KernelMode,
                              FALSE, &Timeout);
    }

    DxgkpDereferenceSyncObject(SyncObj);
    return STATUS_SUCCESS;
}

VOID
NTAPI
DxgkCleanupDeviceSynchronizationObjects(
    _In_ PDXGKRNL_DEVICE Device)
{
    PLIST_ENTRY Entry;
    ULONG Cleaned = 0;

    PAGED_CODE();

    if (Device == NULL)
        return;

    for (;;)
    {
        PDXGKRNL_SYNC_OBJECT SyncObj;

        ExAcquireFastMutex(&Device->DeviceMutex);
        if (IsListEmpty(&Device->SyncObjListHead))
        {
            ExReleaseFastMutex(&Device->DeviceMutex);
            break;
        }

        Entry = RemoveHeadList(&Device->SyncObjListHead);
        InitializeListHead(Entry);
        ExReleaseFastMutex(&Device->DeviceMutex);

        SyncObj = CONTAINING_RECORD(Entry, DXGKRNL_SYNC_OBJECT,
                                    DeviceSyncObjListEntry);

        ExAcquireFastMutex(&DxgkSyncListLock);
        if (DxgkpIsListEntryLinked(&SyncObj->SyncObjListEntry))
        {
            RemoveEntryList(&SyncObj->SyncObjListEntry);
            InitializeListHead(&SyncObj->SyncObjListEntry);
            SyncObj->Handle = 0;
        }
        ExReleaseFastMutex(&DxgkSyncListLock);

        SyncObj->hDevice = 0;
        DxgkpDereferenceSyncObject(SyncObj);
        ++Cleaned;
    }

    if (Cleaned != 0)
    {
        DXGKRNL_WARN("DxgkCleanupDeviceSynchronizationObjects: cleaned %lu leaked sync objects\n",
                     Cleaned);
    }
}
