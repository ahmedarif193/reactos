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

FORCEINLINE BOOLEAN
DxgkpIsListEntryLinked(
    _In_ PLIST_ENTRY Entry)
{
    return (Entry->Flink != Entry);
}

static BOOLEAN
DxgkpReferenceSyncObject(
    _In_ PVOID Object)
{
    PDXGKRNL_SYNC_OBJECT SyncObj = Object;

    if (SyncObj == NULL || InterlockedCompareExchange(&SyncObj->Destroying, 0, 0) != 0)
        return FALSE;
    InterlockedIncrement(&SyncObj->RefCount);
    if (InterlockedCompareExchange(&SyncObj->Destroying, 0, 0) != 0)
    {
        if (InterlockedDecrement(&SyncObj->RefCount) == 0)
            KeSetEvent(&SyncObj->CpuEvent, IO_NO_INCREMENT, FALSE);
        return FALSE;
    }
    return TRUE;
}

static NTSTATUS
DxgkpReferenceSyncObjectByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_opt_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_SYNC_OBJECT *OutSyncObj)
{
    PVOID Object;
    NTSTATUS Status;

    *OutSyncObj = NULL;
    Status = DxgkReferenceOwnedHandle(Handle, DxgkHandleTypeSynchronizationObject, OwnerProcess, DxgkpReferenceSyncObject, &Object);
    if (NT_SUCCESS(Status))
        *OutSyncObj = Object;
    return Status;
}

/*
 * Release a monitored fence's CPU-mapped value page.  The user mapping was
 * made in the creating process; unmapping must run in that context, so
 * attach when freed from elsewhere (e.g. another process's cleanup).
 */
static VOID
DxgkpSyncReleaseMonitoredPage(
    _In_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    if (SyncObj->MonitoredValueUserVa != NULL &&
        SyncObj->MonitoredValueMdl != NULL &&
        SyncObj->MonitoredValueProcess != NULL)
    {
        KAPC_STATE ApcState;
        BOOLEAN Attached = FALSE;

        if (PsGetCurrentProcess() != SyncObj->MonitoredValueProcess)
        {
            KeStackAttachProcess((PKPROCESS)SyncObj->MonitoredValueProcess,
                                 &ApcState);
            Attached = TRUE;
        }

        MmUnmapLockedPages(SyncObj->MonitoredValueUserVa,
                           SyncObj->MonitoredValueMdl);

        if (Attached)
            KeUnstackDetachProcess(&ApcState);

        SyncObj->MonitoredValueUserVa = NULL;
    }

    if (SyncObj->MonitoredValueMdl != NULL)
    {
        IoFreeMdl(SyncObj->MonitoredValueMdl);
        SyncObj->MonitoredValueMdl = NULL;
    }

    if (SyncObj->MonitoredValueKernelVa != NULL)
    {
        ExFreePoolWithTag(SyncObj->MonitoredValueKernelVa, TAG_DXGK_SYNC);
        SyncObj->MonitoredValueKernelVa = NULL;
    }

    if (SyncObj->MonitoredValueProcess != NULL)
    {
        ObDereferenceObject(SyncObj->MonitoredValueProcess);
        SyncObj->MonitoredValueProcess = NULL;
    }
}

static VOID
DxgkpDereferenceSyncObject(
    _In_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    if (SyncObj != NULL &&
        InterlockedDecrement(&SyncObj->RefCount) == 0)
    {
        PDXGKRNL_DEVICE Device = SyncObj->Device;

        DxgkpSyncReleaseMonitoredPage(SyncObj);
        ExFreePoolWithTag(SyncObj, TAG_DXGK_SYNC);
        if (Device != NULL)
            DxgkDereferenceDevice(Device);
    }
}

/*
 * DxgkSyncObjectAttachMonitoredPage
 *
 * Backs a monitored fence with a nonpaged value page and maps it read-only
 * into the current (creating) process.  Returns the user VA of the 64-bit
 * fence value; the kernel-side signal paths keep the page current
 * (documented D3DDDI_SYNCHRONIZATIONOBJECTINFO2 MonitoredFence contract).
 */
NTSTATUS
NTAPI
DxgkSyncObjectAttachMonitoredPage(
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 InitialFenceValue,
    _Out_ PVOID *UserVa)
{
    PDXGKRNL_SYNC_OBJECT SyncObj;
    NTSTATUS Status;

    PAGED_CODE();

    if (UserVa == NULL)
        return STATUS_INVALID_PARAMETER;
    *UserVa = NULL;

    Status = DxgkpReferenceSyncObjectByHandle(hSyncObject, PsGetCurrentProcess(), &SyncObj);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Pool allocations of PAGE_SIZE are page-aligned. */
    SyncObj->MonitoredValueKernelVa =
        ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_DXGK_SYNC);
    if (SyncObj->MonitoredValueKernelVa == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }

    RtlZeroMemory(SyncObj->MonitoredValueKernelVa, PAGE_SIZE);
    *(volatile UINT64 *)SyncObj->MonitoredValueKernelVa = InitialFenceValue;
    InterlockedExchange64(&SyncObj->FenceValue, (LONG64)InitialFenceValue);

    SyncObj->MonitoredValueMdl = IoAllocateMdl(
        SyncObj->MonitoredValueKernelVa, PAGE_SIZE, FALSE, FALSE, NULL);
    if (SyncObj->MonitoredValueMdl == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }

    MmBuildMdlForNonPagedPool(SyncObj->MonitoredValueMdl);

    _SEH2_TRY
    {
        SyncObj->MonitoredValueUserVa = MmMapLockedPagesSpecifyCache(SyncObj->MonitoredValueMdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority | MdlMappingNoWrite | MdlMappingNoExecute);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        SyncObj->MonitoredValueUserVa = NULL;
    }
    _SEH2_END;

    if (SyncObj->MonitoredValueUserVa == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }

    SyncObj->MonitoredValueProcess = PsGetCurrentProcess();
    ObReferenceObject(SyncObj->MonitoredValueProcess);

    *UserVa = SyncObj->MonitoredValueUserVa;
    DxgkpDereferenceSyncObject(SyncObj);
    return STATUS_SUCCESS;

Fail:
    DxgkpSyncReleaseMonitoredPage(SyncObj);
    DxgkpDereferenceSyncObject(SyncObj);
    return Status;
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
    NTSTATUS Status;

    PAGED_CODE();

    if (pCreateSyncObject == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceOwnedDeviceByHandle(pCreateSyncObject->hDevice, PsGetCurrentProcess(), &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;

    if (pCreateSyncObject->Info.Type != D3DDDI_SYNCHRONIZATION_MUTEX &&
        pCreateSyncObject->Info.Type != D3DDDI_SEMAPHORE &&
        pCreateSyncObject->Info.Type != D3DDDI_FENCE &&
        pCreateSyncObject->Info.Type != D3DDDI_CPU_NOTIFICATION)
    {
        DxgkDereferenceDevice(Device);
        return STATUS_NOT_SUPPORTED;
    }

    SyncObj = (PDXGKRNL_SYNC_OBJECT)ExAllocatePoolWithTag(NonPagedPool, sizeof(DXGKRNL_SYNC_OBJECT), TAG_DXGK_SYNC);
    if (SyncObj == NULL)
    {
        DxgkDereferenceDevice(Device);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(SyncObj, sizeof(*SyncObj));
    SyncObj->hDevice = Device->Handle;
    SyncObj->Device = Device;
    SyncObj->OwnerProcess = PsGetCurrentProcess();
    SyncObj->Info = pCreateSyncObject->Info;
    SyncObj->RefCount = 1;
    SyncObj->FenceValue = 0;
    KeInitializeEvent(&SyncObj->CpuEvent, SynchronizationEvent, FALSE);
    InitializeListHead(&SyncObj->SyncObjListEntry);
    InitializeListHead(&SyncObj->DeviceSyncObjListEntry);

    Status = DxgkCreateOwnedHandle(DxgkHandleTypeSynchronizationObject, SyncObj, Adapter, SyncObj->OwnerProcess, &SyncObj->Destroying, &SyncObj->TeardownClaimed, &SyncObj->Handle);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&SyncObj->Destroying, 1);
        DxgkpDereferenceSyncObject(SyncObj);
        return Status;
    }

    ExAcquireFastMutex(&Device->DeviceMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&Device->DeviceMutex);
        DxgkRemoveOwnedHandleObject(DxgkHandleTypeSynchronizationObject, SyncObj);
        InterlockedExchange(&SyncObj->Destroying, 1);
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_DELETE_PENDING;
    }
    InsertTailList(&Device->SyncObjListHead, &SyncObj->DeviceSyncObjListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);

    pCreateSyncObject->hSyncObject = SyncObj->Handle;

    DXGKRNL_TRACE("DxgkCreateSynchronizationObject: handle=0x%X type=%d\n", SyncObj->Handle, SyncObj->Info.Type);
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
    PDXGKRNL_SYNC_OBJECT SyncObj;
    PVOID Object;
    NTSTATUS Status;

    PAGED_CODE();

    if (pDestroySyncObject == NULL)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkDetachOwnedHandle(pDestroySyncObject->hSyncObject, DxgkHandleTypeSynchronizationObject, PsGetCurrentProcess(), &Object);
    if (!NT_SUCCESS(Status))
        return Status;
    SyncObj = Object;
    ASSERT(InterlockedCompareExchange(&SyncObj->TeardownClaimed, 1, 1) == 1);
    ExAcquireFastMutex(&SyncObj->Device->DeviceMutex);
    if (DxgkpIsListEntryLinked(&SyncObj->DeviceSyncObjListEntry))
    {
        RemoveEntryList(&SyncObj->DeviceSyncObjListEntry);
        InitializeListHead(&SyncObj->DeviceSyncObjListEntry);
    }
    ExReleaseFastMutex(&SyncObj->Device->DeviceMutex);
    KeSetEvent(&SyncObj->CpuEvent, IO_NO_INCREMENT, FALSE);

    DXGKRNL_TRACE("DxgkDestroySynchronizationObject: handle=0x%X\n", pDestroySyncObject->hSyncObject);

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
    PDXGKRNL_CONTEXT Context;
    PDXGKRNL_SYNC_OBJECT SyncObjs[D3DDDI_MAX_OBJECT_SIGNALED];
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

    Status = DxgkReferenceContextByHandle(pSignalSyncObject->hContext, PsGetCurrentProcess(), &Adapter, &Device, &Context);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlZeroMemory(SyncObjs, sizeof(SyncObjs));

    for (i = 0; i < pSignalSyncObject->ObjectCount; ++i)
    {
        PDXGKRNL_SYNC_OBJECT SyncObj;

        Status = DxgkpReferenceSyncObjectByHandle(pSignalSyncObject->ObjectHandleArray[i], PsGetCurrentProcess(), &SyncObj);
        if (!NT_SUCCESS(Status))
        {
            goto CleanupReferences;
        }
        if (SyncObj->Device != Device)
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

    DxgkDereferenceContext(Context);
    return STATUS_SUCCESS;

CleanupReferences:
    for (CleanupIndex = 0; CleanupIndex < pSignalSyncObject->ObjectCount; ++CleanupIndex)
    {
        if (SyncObjs[CleanupIndex] != NULL)
            DxgkpDereferenceSyncObject(SyncObjs[CleanupIndex]);
    }
    DxgkDereferenceContext(Context);
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
    PDXGKRNL_CONTEXT Context;
    PDXGKRNL_SYNC_OBJECT SyncObjs[D3DDDI_MAX_OBJECT_WAITED_ON];
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

    Status = DxgkReferenceContextByHandle(pWaitSyncObject->hContext, PsGetCurrentProcess(), &Adapter, &Device, &Context);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlZeroMemory(SyncObjs, sizeof(SyncObjs));

    for (i = 0; i < pWaitSyncObject->ObjectCount; ++i)
    {
        PDXGKRNL_SYNC_OBJECT SyncObj;

        Status = DxgkpReferenceSyncObjectByHandle(pWaitSyncObject->ObjectHandleArray[i], PsGetCurrentProcess(), &SyncObj);
        if (!NT_SUCCESS(Status))
        {
            goto CleanupReferences;
        }
        if (SyncObj->Device != Device)
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
            Status = KeWaitForSingleObject(&SyncObj->CpuEvent, Executive, KernelMode, FALSE, &Timeout);
            if (InterlockedCompareExchange(&SyncObj->Destroying, 0, 0) != 0)
                Status = STATUS_DELETE_PENDING;
        }
        DxgkpDereferenceSyncObject(SyncObj);
        SyncObjs[i] = NULL;

        if (Status != STATUS_SUCCESS)
            goto CleanupReferences;
    }

    DxgkDereferenceContext(Context);
    return STATUS_SUCCESS;

CleanupReferences:
    for (CleanupIndex = 0; CleanupIndex < pWaitSyncObject->ObjectCount; ++CleanupIndex)
    {
        if (SyncObjs[CleanupIndex] != NULL)
            DxgkpDereferenceSyncObject(SyncObjs[CleanupIndex]);
    }
    DxgkDereferenceContext(Context);
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
/* Publish the current fence value to the CPU-visible page and wake waiters. */
static VOID
DxgkpSyncPublishFenceValue(
    _In_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    if (SyncObj->MonitoredValueKernelVa != NULL)
    {
        *(volatile UINT64 *)SyncObj->MonitoredValueKernelVa = (UINT64)SyncObj->FenceValue;
        KeMemoryBarrier();
    }

    KeSetEvent(&SyncObj->CpuEvent, IO_NO_INCREMENT, FALSE);
}

NTSTATUS
NTAPI
DxgkSyncObjectCpuSignal(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 FenceValue)
{
    PDXGKRNL_SYNC_OBJECT SyncObj;
    NTSTATUS Status;

    PAGED_CODE();

    Status = DxgkpReferenceSyncObjectByHandle(hSyncObject, PsGetCurrentProcess(), &SyncObj);
    if (!NT_SUCCESS(Status))
        return Status;

    if (hDevice != 0 && SyncObj->hDevice != hDevice)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_INVALID_HANDLE;
    }

    InterlockedExchange64(&SyncObj->FenceValue, (LONG64)FenceValue);

    DxgkpSyncPublishFenceValue(SyncObj);
    DxgkpDereferenceSyncObject(SyncObj);
    return STATUS_SUCCESS;
}

/*
 * DxgkSyncObjectGpuRetireSignal
 *
 * GPU-retire flavour of DxgkSyncObjectCpuSignal: monotonic (max) — tracked
 * submissions on independent nodes may retire out of global fence order and
 * must never rewind the monitored fence's CPU-visible value.
 */
NTSTATUS
NTAPI
DxgkSyncObjectGpuRetireSignal(
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 FenceValue)
{
    PDXGKRNL_SYNC_OBJECT SyncObj;
    LONG64 Current;
    NTSTATUS Status;

    PAGED_CODE();

    Status = DxgkpReferenceSyncObjectByHandle(hSyncObject, NULL, &SyncObj);
    if (!NT_SUCCESS(Status))
        return Status;

    for (;;)
    {
        Current = SyncObj->FenceValue;
        if ((UINT64)Current >= FenceValue)
            break;
        if (InterlockedCompareExchange64(&SyncObj->FenceValue, (LONG64)FenceValue, Current) == Current)
        {
            break;
        }
    }

    DxgkpSyncPublishFenceValue(SyncObj);
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
    NTSTATUS Status;

    PAGED_CODE();

    Status = DxgkpReferenceSyncObjectByHandle(hSyncObject, PsGetCurrentProcess(), &SyncObj);
    if (!NT_SUCCESS(Status))
        return Status;

    if (hDevice != 0 && SyncObj->hDevice != hDevice)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_INVALID_HANDLE;
    }

    if (NonBlocking)
    {
        BOOLEAN Reached = ((UINT64)SyncObj->FenceValue >= FenceValue);

        DxgkpDereferenceSyncObject(SyncObj);
        return Reached ? STATUS_SUCCESS : STATUS_TIMEOUT;
    }

    /* Block until the monitored fence actually reaches the value.  The
     * 100ms bound only re-checks after a lost wake; TDR guarantees the
     * fence always advances, so this cannot hang past a dead GPU. */
    while ((UINT64)SyncObj->FenceValue < FenceValue)
    {
        LARGE_INTEGER Timeout;
        Timeout.QuadPart = -10LL * 1000LL * 100LL;
        KeWaitForSingleObject(&SyncObj->CpuEvent, Executive, KernelMode, FALSE, &Timeout);
        if (InterlockedCompareExchange(&SyncObj->Destroying, 0, 0) != 0)
        {
            DxgkpDereferenceSyncObject(SyncObj);
            return STATUS_DELETE_PENDING;
        }
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
        BOOLEAN OwnsTeardown;

        ExAcquireFastMutex(&Device->DeviceMutex);
        if (IsListEmpty(&Device->SyncObjListHead))
        {
            ExReleaseFastMutex(&Device->DeviceMutex);
            break;
        }

        Entry = Device->SyncObjListHead.Flink;
        SyncObj = CONTAINING_RECORD(Entry, DXGKRNL_SYNC_OBJECT, DeviceSyncObjListEntry);
        OwnsTeardown = DxgkTryClaimTeardown(&SyncObj->TeardownClaimed);
        InterlockedExchange(&SyncObj->Destroying, 1);
        RemoveEntryList(Entry);
        InitializeListHead(Entry);
        ExReleaseFastMutex(&Device->DeviceMutex);

        if (!OwnsTeardown)
        {
            /* The direct owner retains this Device until its final release. */
            continue;
        }
        ASSERT(InterlockedCompareExchange(&SyncObj->TeardownClaimed, 1, 1) == 1);
        DxgkRemoveOwnedHandleObject(DxgkHandleTypeSynchronizationObject, SyncObj);
        KeSetEvent(&SyncObj->CpuEvent, IO_NO_INCREMENT, FALSE);
        DxgkpDereferenceSyncObject(SyncObj);
        ++Cleaned;
    }

    if (Cleaned != 0)
    {
        DXGKRNL_WARN("DxgkCleanupDeviceSynchronizationObjects: cleaned %lu leaked sync objects\n", Cleaned);
    }
}
