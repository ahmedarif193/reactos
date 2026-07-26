/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Retained scheduler-ordered synchronization batches
 */

#include "dxgkrnl_private.h"

C_ASSERT(DXGK_CONTEXT_SYNC_TYPE_MONITORED_FENCE == D3DDDI_MONITORED_FENCE);

static VOID
NTAPI
DxgkpContextSyncReleaseReference(
    _In_ PVOID Object,
    _In_ DXGK_CONTEXT_SYNC_REFERENCE_KIND Kind,
    _In_opt_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    switch (Kind)
    {
        case DxgkContextSyncReferenceDevice:
            DxgkDereferenceDevice(Object);
            break;
        case DxgkContextSyncReferenceOwner:
        case DxgkContextSyncReferenceEvent:
            ObDereferenceObject(Object);
            break;
        case DxgkContextSyncReferenceObject:
            DxgkpDereferenceSyncObject(Object);
            break;
        default:
            ASSERT(FALSE);
            break;
    }
}

static VOID
DxgkpContextSyncInitializeCoreObject(
    _Out_ PDXGK_CONTEXT_SYNC_CORE_OBJECT CoreObject,
    _In_ PDXGKRNL_SYNC_OBJECT SyncObject)
{
    RtlZeroMemory(CoreObject, sizeof(*CoreObject));
    CoreObject->Identity = SyncObject;
    CoreObject->Type = SyncObject->PublicType;
    CoreObject->ObjectFlags = SyncObject->Flags.Value;
    CoreObject->Destroying = &SyncObject->Destroying;
    CoreObject->FenceValue = &SyncObject->FenceValue;
    CoreObject->MutexOwned = &SyncObject->MutexOwned;
    CoreObject->SemaphoreCount = &SyncObject->SemaphoreCount;
    CoreObject->SemaphoreLimit = SyncObject->SemaphoreLimit;
    CoreObject->StateEvent = &SyncObject->CpuEvent;
    CoreObject->NotificationEvent = SyncObject->CpuNotificationEvent;
}

static NTSTATUS
DxgkpContextSyncRetain(
    _Inout_ PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture,
    _In_ PVOID Object,
    _In_ DXGK_CONTEXT_SYNC_REFERENCE_KIND Kind)
{
    NTSTATUS Status = DxgkContextSyncCoreAddReference(&Capture->Retention, Object, Kind);

    ASSERT(NT_SUCCESS(Status));
    return Status;
}

NTSTATUS
DxgkContextSyncCapture(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ PEPROCESS OwnerProcess,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ DXGK_CONTEXT_SYNC_OPERATION Operation,
    _In_reads_opt_(ObjectCount) CONST D3DKMT_HANDLE *ObjectHandles,
    _In_ ULONG ObjectCount,
    _In_ ULONG SignalFlags,
    _In_ UINT64 PayloadValue,
    _In_reads_opt_(ObjectCount) CONST UINT64 *PayloadValueArray,
    _Out_ PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture)
{
    NTSTATUS Status;
    ULONG Index;

    PAGED_CODE();
    if (Capture == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(Capture, sizeof(*Capture));
    KeInitializeSpinLock(&Capture->Lock);
    DxgkContextSyncCoreInitializeRetention(&Capture->Retention, DxgkpContextSyncReleaseReference, NULL);
    if (Device == NULL || OwnerProcess == NULL || ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED)
        return STATUS_INVALID_PARAMETER;
    if ((SignalFlags & DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT) != 0 && ObjectCount != 0)
        return STATUS_INVALID_PARAMETER;
    if ((SignalFlags & DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT) == 0 && ObjectCount != 0 && ObjectHandles == NULL)
        return STATUS_INVALID_PARAMETER;
    Capture->Operation = Operation;
    Capture->OwnerProcess = OwnerProcess;
    Capture->ObjectCount = ObjectCount;
    Capture->SignalFlags = SignalFlags;
    Capture->PayloadValue = PayloadValue;
    Capture->PerObjectValues = FALSE;
    if (PayloadValueArray != NULL)
    {
        if (ObjectCount > RTL_NUMBER_OF(Capture->PayloadValues))
            return STATUS_INVALID_PARAMETER;
        RtlCopyMemory(Capture->PayloadValues, PayloadValueArray, ObjectCount * sizeof(UINT64));
        Capture->PerObjectValues = TRUE;
    }
    if (!DxgkReferenceDevice(Device))
        return STATUS_DELETE_PENDING;
    Capture->Device = Device;
    Status = DxgkpContextSyncRetain(Capture, Device, DxgkContextSyncReferenceDevice);
    if (!NT_SUCCESS(Status))
        goto Failure;
    ObReferenceObject(OwnerProcess);
    Status = DxgkpContextSyncRetain(Capture, OwnerProcess, DxgkContextSyncReferenceOwner);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(OwnerProcess);
        goto Failure;
    }
    if ((SignalFlags & DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT) != 0)
    {
        HANDLE EventHandle = (HANDLE)(ULONG_PTR)PayloadValue;

        if (OwnerProcess != PsGetCurrentProcess())
        {
            Status = STATUS_ACCESS_DENIED;
            goto Failure;
        }
        Status = ObReferenceObjectByHandle(EventHandle, EVENT_MODIFY_STATE, *ExEventObjectType, AccessMode, (PVOID *)&Capture->EnqueueEvent, NULL);
        if (!NT_SUCCESS(Status))
            goto Failure;
        Status = DxgkpContextSyncRetain(Capture, Capture->EnqueueEvent, DxgkContextSyncReferenceEvent);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(Capture->EnqueueEvent);
            Capture->EnqueueEvent = NULL;
            goto Failure;
        }
    }
    for (Index = 0; Index < ObjectCount && Index < RTL_NUMBER_OF(Capture->Objects); ++Index)
    {
        Status = DxgkpReferenceSyncObjectByHandle(ObjectHandles[Index], OwnerProcess, &Capture->Objects[Index]);
        if (!NT_SUCCESS(Status))
            goto Failure;
        if (Capture->Objects[Index]->Device != Device || Capture->Objects[Index]->OwnerProcess != OwnerProcess)
        {
            DxgkpDereferenceSyncObject(Capture->Objects[Index]);
            Capture->Objects[Index] = NULL;
            Status = STATUS_INVALID_HANDLE;
            goto Failure;
        }
        Status = DxgkpContextSyncRetain(Capture, Capture->Objects[Index], DxgkContextSyncReferenceObject);
        if (!NT_SUCCESS(Status))
        {
            DxgkpDereferenceSyncObject(Capture->Objects[Index]);
            Capture->Objects[Index] = NULL;
            goto Failure;
        }
        DxgkpContextSyncInitializeCoreObject(&Capture->CoreObjects[Index], Capture->Objects[Index]);
    }
    Status = DxgkContextSyncCoreValidate(Operation, Capture->CoreObjects, ObjectCount, SignalFlags, (SignalFlags & DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT) != 0 ? 0 : PayloadValue, Capture->PerObjectValues ? Capture->PayloadValues : NULL, Capture->EnqueueEvent != NULL);
    if (!NT_SUCCESS(Status))
        goto Failure;
    return STATUS_SUCCESS;

Failure:
    DxgkContextSyncCoreRelease(&Capture->Retention);
    Capture->Device = NULL;
    Capture->OwnerProcess = NULL;
    Capture->EnqueueEvent = NULL;
    Capture->ObjectCount = 0;
    return Status;
}

/*
 * A signal batch made entirely of monitored fences.  These cannot be completed
 * the way the legacy types are: their value lives in a page shared with user
 * mode, and anything blocked in WaitForSynchronizationObjectFromCpu is parked
 * in the device's wait registry.  Both are updated by DxgkSyncPublishFenceBatch,
 * which takes that registry's lock itself -- so this batch has to run outside
 * the lock DxgkContextSyncExecute otherwise holds, not merely differently under
 * it.
 */
static BOOLEAN
DxgkpContextSyncIsMonitoredFenceSignal(
    _In_ PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture)
{
    ULONG Index;

    if (Capture->Operation != DxgkContextSyncOperationSignal2)
        return FALSE;
    if (Capture->ObjectCount == 0 || !Capture->PerObjectValues || Capture->EnqueueEvent != NULL)
        return FALSE;
    if ((Capture->SignalFlags & DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT) != 0)
        return FALSE;
    for (Index = 0; Index < Capture->ObjectCount; ++Index)
    {
        if (Capture->CoreObjects[Index].Type != DXGK_CONTEXT_SYNC_TYPE_MONITORED_FENCE)
            return FALSE;
    }
    return TRUE;
}

NTSTATUS
DxgkContextSyncExecute(
    _Inout_ PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture)
{
    PDXGKRNL_DEVICE Device;
    KIRQL CaptureOldIrql;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Capture == NULL)
        return STATUS_INVALID_PARAMETER;
    KeAcquireSpinLock(&Capture->Lock, &CaptureOldIrql);
    if (InterlockedCompareExchange(&Capture->Retention.ReleaseClaimed, 0, 0) != 0 || Capture->Device == NULL)
    {
        KeReleaseSpinLock(&Capture->Lock, CaptureOldIrql);
        return STATUS_DELETE_PENDING;
    }
    Device = Capture->Device;

    if (DxgkpContextSyncIsMonitoredFenceSignal(Capture))
    {
        if (Capture->Executed != 0)
        {
            KeReleaseSpinLock(&Capture->Lock, CaptureOldIrql);
            return STATUS_ALREADY_COMPLETE;
        }
        if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 ||
            InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        {
            KeReleaseSpinLock(&Capture->Lock, CaptureOldIrql);
            return STATUS_DEVICE_REMOVED;
        }
        /* Claim the batch before dropping the lock so a second drainer cannot
         * publish the same values twice; hand it back only if the publish is
         * refused, which leaves nothing behind to have completed. */
        Capture->Executed = 1;
        KeReleaseSpinLock(&Capture->Lock, CaptureOldIrql);

        Status = DxgkSyncPublishFenceBatch(Capture->Objects, Capture->PayloadValues,
                                           Capture->ObjectCount,
                                           (Capture->SignalFlags & DXGK_CONTEXT_SYNC_ALLOW_FENCE_REWIND) != 0,
                                           FALSE);
        if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
        {
            KeAcquireSpinLock(&Capture->Lock, &CaptureOldIrql);
            Capture->Executed = 0;
            KeReleaseSpinLock(&Capture->Lock, CaptureOldIrql);
        }
        return Status;
    }

    KeAcquireSpinLock(&Device->SyncWaitRegistry.Lock, &OldIrql);
    if (Capture->Executed != 0)
        Status = STATUS_ALREADY_COMPLETE;
    else if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        Status = STATUS_DEVICE_REMOVED;
    else if (Capture->Operation == DxgkContextSyncOperationLegacyWait || Capture->Operation == DxgkContextSyncOperationWait2)
        Status = DxgkContextSyncCoreExecuteWait(Capture->CoreObjects, Capture->ObjectCount, Capture->PayloadValue, Capture->PerObjectValues ? Capture->PayloadValues : NULL);
    else
        Status = DxgkContextSyncCoreExecuteSignal(Capture->CoreObjects, Capture->ObjectCount, Capture->SignalFlags, Capture->PayloadValue, Capture->PerObjectValues ? Capture->PayloadValues : NULL, Capture->EnqueueEvent);
    if (Status != STATUS_PENDING && Status != STATUS_ALREADY_COMPLETE)
        Capture->Executed = 1;
    KeReleaseSpinLock(&Device->SyncWaitRegistry.Lock, OldIrql);
    KeReleaseSpinLock(&Capture->Lock, CaptureOldIrql);
    return Status;
}

VOID
DxgkContextSyncRelease(
    _Inout_ PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture)
{
    BOOLEAN Claimed;
    KIRQL OldIrql;

    PAGED_CODE();
    if (Capture == NULL)
        return;
    KeAcquireSpinLock(&Capture->Lock, &OldIrql);
    Claimed = DxgkContextSyncCoreClaimRelease(&Capture->Retention);
    if (Claimed)
    {
        Capture->Device = NULL;
        Capture->OwnerProcess = NULL;
        Capture->EnqueueEvent = NULL;
        Capture->ObjectCount = 0;
    }
    KeReleaseSpinLock(&Capture->Lock, OldIrql);
    if (Claimed)
        DxgkContextSyncCoreReleaseClaimed(&Capture->Retention);
}
