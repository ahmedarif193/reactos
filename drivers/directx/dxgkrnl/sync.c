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

#define DXGK_CPU_SIGNAL_ALLOW_FENCE_REWIND 0x00000004UL

typedef struct _DXGKRNL_CPU_WAIT_REQUEST
{
    DXGK_SYNC_WAIT_CORE_REQUEST CoreRequest;
    WORK_QUEUE_ITEM CleanupWorkItem;
    volatile LONG ReferenceCount;
    volatile LONG CleanupQueued;
    KEVENT SynchronousEvent;
    PKEVENT CompletionEvent;
    BOOLEAN EventObjectReferenced;
    PDXGKRNL_DEVICE Device;
    ULONG ObjectCount;
    PDXGKRNL_SYNC_OBJECT Objects[D3DDDI_MAX_OBJECT_WAITED_ON];
    DXGK_SYNC_WAIT_CORE_TARGET Targets[D3DDDI_MAX_OBJECT_WAITED_ON];
} DXGKRNL_CPU_WAIT_REQUEST, *PDXGKRNL_CPU_WAIT_REQUEST;

typedef struct _DXGKRNL_SYNC_PUBLISH_ADMISSION
{
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_SYNC_OBJECT *Objects;
    ULONG ObjectCount;
    BOOLEAN PublicCpuSignal;
} DXGKRNL_SYNC_PUBLISH_ADMISSION, *PDXGKRNL_SYNC_PUBLISH_ADMISSION;

FORCEINLINE BOOLEAN
DxgkpIsListEntryLinked(
    _In_ PLIST_ENTRY Entry)
{
    return (Entry->Flink != Entry);
}

static BOOLEAN DxgkpReferenceSyncObject(_In_ PVOID Object);
static NTSTATUS DxgkpSyncObjectPublishRetiredFence(_In_ PDXGKRNL_SYNC_OBJECT SyncObj, _In_ UINT64 FenceValue);
static PDXGKRNL_SYNC_OBJECT DxgkpSyncResolveShared(_In_ PDXGKRNL_SYNC_OBJECT SyncObj);
static VOID DxgkpSyncDestroyPeriodicNotification(_Inout_ PDXGKRNL_SYNC_OBJECT SyncObj);

/* ========================================================================
 * Sharing and periodic registries
 *
 * A shareable sync object is published in one global namespace keyed by its
 * share handle; an opener resolves that handle and creates an alias bound to
 * the same authoritative object.  WDDM 2.2 periodic monitored fences use a
 * separate DISPATCH-safe registry keyed by the stable KMD NotificationID.
 * ====================================================================== */

static LONG DxgkSyncShareInitialized = 0;
static FAST_MUTEX DxgkSyncShareLock;
static KSPIN_LOCK DxgkSyncPeriodicLock;
static LIST_ENTRY DxgkSyncShareListHead;
static LIST_ENTRY DxgkSyncPeriodicListHead;
static volatile LONG DxgkSyncNextShareHandle = 0;
static volatile LONG DxgkSyncNextPeriodicNotificationId = 0;
static CONST ULONG DxgkSyncShareHandleCookie = 0x4E595353; /* "SSYN" */

static VOID
DxgkpSyncShareInitialize(VOID)
{
    LONG State = InterlockedCompareExchange(&DxgkSyncShareInitialized, 1, 0);

    if (State == 0)
    {
        ExInitializeFastMutex(&DxgkSyncShareLock);
        KeInitializeSpinLock(&DxgkSyncPeriodicLock);
        InitializeListHead(&DxgkSyncShareListHead);
        InitializeListHead(&DxgkSyncPeriodicListHead);
        InterlockedExchange(&DxgkSyncShareInitialized, 2);
        return;
    }
    while (InterlockedCompareExchange(&DxgkSyncShareInitialized, 2, 2) != 2)
        YieldProcessor();
}

static D3DKMT_HANDLE
DxgkpSyncAllocateShareHandle(VOID)
{
    D3DKMT_HANDLE Handle;

    do
    {
        Handle = (D3DKMT_HANDLE)(((ULONG)InterlockedIncrement(&DxgkSyncNextShareHandle) << 8) ^ DxgkSyncShareHandleCookie);
    } while (Handle == 0);
    return Handle;
}

/* Publishes a shareable object. The caller owns a live reference. */
static NTSTATUS
DxgkpSyncPublishShare(
    _Inout_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    DxgkpSyncShareInitialize();
    ExAcquireFastMutex(&DxgkSyncShareLock);
    for (;;)
    {
        PLIST_ENTRY Entry;
        BOOLEAN Collision = FALSE;

        SyncObj->GlobalShareHandle = DxgkpSyncAllocateShareHandle();
        for (Entry = DxgkSyncShareListHead.Flink; Entry != &DxgkSyncShareListHead; Entry = Entry->Flink)
        {
            if (CONTAINING_RECORD(Entry, DXGKRNL_SYNC_OBJECT, GlobalShareListEntry)->GlobalShareHandle == SyncObj->GlobalShareHandle)
            {
                Collision = TRUE;
                break;
            }
        }
        if (!Collision)
            break;
    }
    SyncObj->Shareable = TRUE;
    InsertTailList(&DxgkSyncShareListHead, &SyncObj->GlobalShareListEntry);
    ExReleaseFastMutex(&DxgkSyncShareLock);
    return STATUS_SUCCESS;
}

static VOID
DxgkpSyncUnpublishShare(
    _Inout_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    if (InterlockedCompareExchange(&DxgkSyncShareInitialized, 2, 2) != 2)
        return;
    ExAcquireFastMutex(&DxgkSyncShareLock);
    if (SyncObj->Shareable && DxgkpIsListEntryLinked(&SyncObj->GlobalShareListEntry))
    {
        RemoveEntryList(&SyncObj->GlobalShareListEntry);
        InitializeListHead(&SyncObj->GlobalShareListEntry);
    }
    SyncObj->Shareable = FALSE;
    SyncObj->GlobalShareHandle = 0;
    ExReleaseFastMutex(&DxgkSyncShareLock);
}

BOOLEAN
DxgkSyncHasPeriodicFences(VOID)
{
    /*
     * Native WDDM 2.2 periodic fences are advanced only by interrupt type 14.
     * Reporting them to the generic VSync worker would make that worker also
     * advance them and double-signal one physical notification.
     */
    return FALSE;
}

/*
 * The old software VSync surrogate is deliberately inert.  A KMD-created
 * periodic notification can fire at a requested pre-VSync offset and carries
 * its exact NotificationID; a generic CRTC_VSYNC pulse proves neither.
 */
VOID
DxgkSyncAdvancePeriodicFences(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(VidPnSourceId);
}

static BOOLEAN
DxgkpSyncPeriodicCallbacksSupported(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->UseDodLayout ||
        REACTOS_WDDM_TARGET_LEVEL < DXGK_CAPS_CORE_LEVEL_WDDM_2_2 ||
        !DxgkCapsCoreInterfaceVersionAtLeast(
            Adapter->MiniportContext->InitData.s.Version,
            DXGK_CAPS_CORE_LEVEL_WDDM_2_2))
    {
        return FALSE;
    }
    return DXGK_CB_FULL(
               Adapter,
               DxgkDdiCreatePeriodicFrameNotification) != NULL &&
           DXGK_CB_FULL(
               Adapter,
               DxgkDdiDestroyPeriodicFrameNotification) != NULL;
}

/*
 * Reserve a globally unique nonzero ID before calling the KMD.  CREATING
 * entries participate in collision detection but are not resolvable by the
 * interrupt path, so callback failure cannot publish a half-created object.
 */
static NTSTATUS
DxgkpSyncReservePeriodicNotification(
    _Inout_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;

    DxgkpSyncShareInitialize();
    for (;;)
    {
        ULONG NotificationId;
        BOOLEAN Collision = FALSE;
        NTSTATUS Status;
        ULONG AdapterNotificationCount = 0;

        do
        {
            NotificationId =
                (ULONG)InterlockedIncrement(
                    &DxgkSyncNextPeriodicNotificationId);
        } while (NotificationId == 0);

        KeAcquireSpinLock(&DxgkSyncPeriodicLock, &OldIrql);
        if (DxgkpIsListEntryLinked(&SyncObj->PeriodicListEntry) ||
            InterlockedCompareExchange(
                &SyncObj->PeriodicNotification.State,
                DxgkPeriodicNotificationNone,
                DxgkPeriodicNotificationNone) !=
            DxgkPeriodicNotificationNone)
        {
            KeReleaseSpinLock(&DxgkSyncPeriodicLock, OldIrql);
            return STATUS_INVALID_DEVICE_STATE;
        }
        for (Entry = DxgkSyncPeriodicListHead.Flink;
             Entry != &DxgkSyncPeriodicListHead;
             Entry = Entry->Flink)
        {
            PDXGKRNL_SYNC_OBJECT Candidate =
                CONTAINING_RECORD(
                    Entry,
                    DXGKRNL_SYNC_OBJECT,
                    PeriodicListEntry);

            if (Candidate->PeriodicNotification.NotificationId ==
                NotificationId)
            {
                Collision = TRUE;
            }
            if (Candidate->Device != NULL &&
                SyncObj->Device != NULL &&
                Candidate->Device->Adapter ==
                    SyncObj->Device->Adapter)
            {
                ++AdapterNotificationCount;
            }
        }
        if (AdapterNotificationCount >=
            DXGKRNL_PERIODIC_NOTIFICATION_CAPACITY)
        {
            KeReleaseSpinLock(&DxgkSyncPeriodicLock, OldIrql);
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        if (!Collision)
        {
            Status = DxgkPeriodicNotificationCoreBeginCreate(
                &SyncObj->PeriodicNotification,
                SyncObj->PeriodicVidPnTargetId,
                NotificationId);
            if (NT_SUCCESS(Status))
                InsertTailList(
                    &DxgkSyncPeriodicListHead,
                    &SyncObj->PeriodicListEntry);
            KeReleaseSpinLock(&DxgkSyncPeriodicLock, OldIrql);
            return Status;
        }
        KeReleaseSpinLock(&DxgkSyncPeriodicLock, OldIrql);
    }
}

static VOID
DxgkpSyncCancelPeriodicReservation(
    _Inout_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&DxgkSyncPeriodicLock, &OldIrql);
    if (InterlockedCompareExchange(
            &SyncObj->PeriodicNotification.State,
            DxgkPeriodicNotificationCreating,
            DxgkPeriodicNotificationCreating) ==
        DxgkPeriodicNotificationCreating)
    {
        if (DxgkpIsListEntryLinked(&SyncObj->PeriodicListEntry))
        {
            RemoveEntryList(&SyncObj->PeriodicListEntry);
            InitializeListHead(&SyncObj->PeriodicListEntry);
        }
        (VOID)DxgkPeriodicNotificationCoreCancelCreate(
            &SyncObj->PeriodicNotification);
    }
    KeReleaseSpinLock(&DxgkSyncPeriodicLock, OldIrql);
}

static NTSTATUS
DxgkpSyncCallDestroyPeriodicNotification(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ HANDLE NotificationHandle)
{
    DXGKARG_DESTROYPERIODICFRAMENOTIFICATION DestroyArgs;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL ||
        Adapter->MiniportContext == NULL ||
        NotificationHandle == NULL ||
        DXGK_CB_FULL(
            Adapter,
            DxgkDdiDestroyPeriodicFrameNotification) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;

    RtlZeroMemory(&DestroyArgs, sizeof(DestroyArgs));
    DestroyArgs.hNotification = NotificationHandle;
    DestroyArgs.hAdapter = Adapter->MiniportDeviceContext;
    _SEH2_TRY
    {
        Status =
            DXGK_CB_FULL(
                Adapter,
                DxgkDdiDestroyPeriodicFrameNotification)(&DestroyArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    return Status;
}

static VOID
DxgkpSyncDestroyPeriodicNotification(
    _Inout_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    PDXGKRNL_ADAPTER Adapter;
    PVOID NotificationHandle = NULL;
    KIRQL OldIrql;
    BOOLEAN Claimed;
    NTSTATUS Status;

    PAGED_CODE();
    if (SyncObj == NULL || !SyncObj->Periodic)
        return;

    KeAcquireSpinLock(&DxgkSyncPeriodicLock, &OldIrql);
    Claimed = DxgkPeriodicNotificationCoreClaimDestroy(
        &SyncObj->PeriodicNotification,
        &NotificationHandle);
    if (Claimed &&
        DxgkpIsListEntryLinked(&SyncObj->PeriodicListEntry))
    {
        RemoveEntryList(&SyncObj->PeriodicListEntry);
        InitializeListHead(&SyncObj->PeriodicListEntry);
    }
    KeReleaseSpinLock(&DxgkSyncPeriodicLock, OldIrql);
    if (!Claimed)
        return;

    /*
     * An interrupt DPC that resolved the entry before removal owns this
     * rundown reference.  Drain it before telling the KMD to destroy the
     * notification or releasing the monitored page.
     */
    ExWaitForRundownProtectionRelease(
        &SyncObj->PeriodicNotificationRundown);
    Adapter =
        SyncObj->Device != NULL ? SyncObj->Device->Adapter : NULL;
    Status = DxgkpSyncCallDestroyPeriodicNotification(
        Adapter,
        (HANDLE)NotificationHandle);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_ERR(
            "DXGKRNL: periodic notification %lu destroy failed 0x%08lX\n",
            SyncObj->PeriodicNotification.NotificationId,
            Status);
    }
}

static NTSTATUS
DxgkpSyncCreatePeriodicNotification(
    _Inout_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    DXGKARG_CREATEPERIODICFRAMENOTIFICATION CreateArgs;
    PDXGKRNL_ADAPTER Adapter;
    PVOID RollbackHandle = NULL;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();
    if (SyncObj == NULL ||
        !SyncObj->Periodic ||
        SyncObj->Device == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Adapter = SyncObj->Device->Adapter;
    if (!DxgkpSyncPeriodicCallbacksSupported(Adapter))
        return STATUS_NOT_SUPPORTED;

    Status = DxgkpSyncReservePeriodicNotification(SyncObj);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlZeroMemory(&CreateArgs, sizeof(CreateArgs));
    CreateArgs.hAdapter = Adapter->MiniportDeviceContext;
    CreateArgs.VidPnTargetId = SyncObj->PeriodicVidPnTargetId;
    CreateArgs.Time = SyncObj->PeriodicTime;
    CreateArgs.NotificationID =
        SyncObj->PeriodicNotification.NotificationId;
    if (!DxgkAcquireKmdCall(Adapter))
    {
        DxgkpSyncCancelPeriodicReservation(SyncObj);
        return STATUS_DELETE_PENDING;
    }
    _SEH2_TRY
    {
        Status =
            DXGK_CB_FULL(
                Adapter,
                DxgkDdiCreatePeriodicFrameNotification)(&CreateArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DxgkpSyncCancelPeriodicReservation(SyncObj);
        return Status;
    }
    if (CreateArgs.hNotification == NULL)
    {
        DxgkpSyncCancelPeriodicReservation(SyncObj);
        return STATUS_DRIVER_INTERNAL_ERROR;
    }

    /*
     * Complete and publish under the registry lock.  Device teardown can set
     * Destroying while the KMD callback runs; in that case claim the newly
     * created handle for immediate rollback without ever exposing it to an
     * interrupt lookup.
     */
    KeAcquireSpinLock(&DxgkSyncPeriodicLock, &OldIrql);
    Status = DxgkPeriodicNotificationCoreCompleteCreate(
        &SyncObj->PeriodicNotification,
        CreateArgs.hNotification);
    if (NT_SUCCESS(Status) &&
        (InterlockedCompareExchange(
             &SyncObj->Destroying, 0, 0) != 0 ||
         InterlockedCompareExchange(
             &SyncObj->Device->Destroying, 0, 0) != 0 ||
         InterlockedCompareExchange(
             &SyncObj->Device->ExecutionState, 0, 0) !=
             D3DKMT_DEVICEEXECUTION_ACTIVE))
    {
        if (DxgkPeriodicNotificationCoreClaimDestroy(
                &SyncObj->PeriodicNotification,
                &RollbackHandle))
        {
            if (DxgkpIsListEntryLinked(
                    &SyncObj->PeriodicListEntry))
            {
                RemoveEntryList(&SyncObj->PeriodicListEntry);
                InitializeListHead(
                    &SyncObj->PeriodicListEntry);
            }
        }
        Status = STATUS_DELETE_PENDING;
    }
    else if (!NT_SUCCESS(Status))
    {
        if (DxgkpIsListEntryLinked(&SyncObj->PeriodicListEntry))
        {
            RemoveEntryList(&SyncObj->PeriodicListEntry);
            InitializeListHead(&SyncObj->PeriodicListEntry);
        }
        (VOID)DxgkPeriodicNotificationCoreCancelCreate(
            &SyncObj->PeriodicNotification);
    }
    KeReleaseSpinLock(&DxgkSyncPeriodicLock, OldIrql);

    if (!NT_SUCCESS(Status))
    {
        if (RollbackHandle == NULL)
            RollbackHandle = CreateArgs.hNotification;
        (VOID)DxgkpSyncCallDestroyPeriodicNotification(
            Adapter,
            (HANDLE)RollbackHandle);
    }
    return Status;
}

/*
 * Resolve one DXGK_INTERRUPT_PERIODIC_MONITORED_FENCE_SIGNALED payload.
 * The adapter DPC calls this after copying the ISR payload into stable
 * storage.  Registry lookup and fence publication are nonpaged and safe at
 * DISPATCH_LEVEL; destruction removes the entry and drains the rundown.
 */
_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
DxgkSyncNotifyPeriodicFence(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    _In_ UINT NotificationId)
{
    return DxgkSyncNotifyPeriodicFenceCount(
        Adapter,
        VidPnTargetId,
        NotificationId,
        1);
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
DxgkSyncNotifyPeriodicFenceCount(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID VidPnTargetId,
    _In_ UINT NotificationId,
    _In_ UINT64 NotificationCount)
{
    PDXGKRNL_SYNC_OBJECT SyncObj = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_NOT_FOUND;

    if (Adapter == NULL ||
        NotificationId == 0 ||
        NotificationCount == 0)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(
            &DxgkSyncShareInitialized, 2, 2) != 2)
    {
        return STATUS_NOT_FOUND;
    }

    KeAcquireSpinLock(&DxgkSyncPeriodicLock, &OldIrql);
    for (Entry = DxgkSyncPeriodicListHead.Flink;
         Entry != &DxgkSyncPeriodicListHead;
         Entry = Entry->Flink)
    {
        PDXGKRNL_SYNC_OBJECT Candidate =
            CONTAINING_RECORD(
                Entry,
                DXGKRNL_SYNC_OBJECT,
                PeriodicListEntry);

        if (Candidate->PeriodicNotification.NotificationId !=
            NotificationId)
        {
            continue;
        }
        if (Candidate->Device == NULL ||
            Candidate->Device->Adapter != Adapter ||
            Candidate->PeriodicVidPnTargetId != VidPnTargetId)
        {
            Status = STATUS_INVALID_PARAMETER;
            break;
        }
        if (!DxgkPeriodicNotificationCoreMatches(
                &Candidate->PeriodicNotification,
                VidPnTargetId,
                NotificationId))
        {
            Status = STATUS_DEVICE_NOT_READY;
            break;
        }
        if (InterlockedCompareExchange(
                &Candidate->Destroying, 0, 0) != 0 ||
            !ExAcquireRundownProtection(
                &Candidate->PeriodicNotificationRundown))
        {
            Status = STATUS_DELETE_PENDING;
            break;
        }
        SyncObj = Candidate;
        Status = STATUS_SUCCESS;
        break;
    }
    KeReleaseSpinLock(&DxgkSyncPeriodicLock, OldIrql);

    if (SyncObj != NULL)
    {
        UINT64 CurrentValue;
        UINT64 Value;

        KeAcquireSpinLock(&SyncObj->PeriodicSignalLock, &OldIrql);
        CurrentValue =
            (UINT64)InterlockedCompareExchange64(
                &SyncObj->FenceValue, 0, 0);
        if (NotificationCount > (UINT64)-1 - CurrentValue)
        {
            Status = STATUS_INTEGER_OVERFLOW;
        }
        else
        {
            Value = CurrentValue + NotificationCount;
            Status =
                DxgkpSyncObjectPublishRetiredFence(
                    SyncObj,
                    Value);
        }
        KeReleaseSpinLock(
            &SyncObj->PeriodicSignalLock,
            OldIrql);
        ExReleaseRundownProtection(
            &SyncObj->PeriodicNotificationRundown);
    }
    return Status;
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

NTSTATUS
DxgkpReferenceSyncObjectByHandle(
    _In_ D3DKMT_HANDLE Handle,
    _In_opt_ PEPROCESS OwnerProcess,
    _Out_ PDXGKRNL_SYNC_OBJECT *OutSyncObj)
{
    PDXGKRNL_SYNC_OBJECT SyncObj;
    PVOID Object;
    NTSTATUS Status;

    *OutSyncObj = NULL;
    Status = DxgkReferenceOwnedHandle(Handle, DxgkHandleTypeSynchronizationObject, OwnerProcess, DxgkpReferenceSyncObject, &Object);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * An opened alias carries the access flags of the view the caller holds
     * but none of the fence state: the created object stays authoritative.
     * Resolve to it and hand back a reference on that object, so every state
     * operation reads and writes one fence and one value page.  The alias's
     * own access flags are copied from the creator at open time, so they
     * still gate this request.
     */
    SyncObj = Object;
    if (SyncObj->BackingSyncObject != NULL)
    {
        PDXGKRNL_SYNC_OBJECT Backing = DxgkpSyncResolveShared(SyncObj);

        if (Backing == NULL || !DxgkpReferenceSyncObject(Backing))
        {
            DxgkpDereferenceSyncObject(SyncObj);
            return STATUS_DELETE_PENDING;
        }
        DxgkpDereferenceSyncObject(SyncObj);
        SyncObj = Backing;
    }
    *OutSyncObj = SyncObj;
    return STATUS_SUCCESS;
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
    BOOLEAN QuarantineKernelPage = FALSE;

    if (SyncObj->MonitoredValueGpuVa != 0)
    {
        NTSTATUS UnmapStatus;

        if (SyncObj->Device != NULL &&
            SyncObj->Device->ProcessRecord != NULL)
        {
            UnmapStatus =
                DxgkGpuVaUnmapFencePage(SyncObj->Device->ProcessRecord,
                                        SyncObj->MonitoredValueGpuVa);
        }
        else
        {
            UnmapStatus = STATUS_DEVICE_NOT_READY;
        }
        if (!NT_SUCCESS(UnmapStatus))
        {
            /*
             * A stale GPU PTE may still name the nonpaged backing.  Reusing
             * that page would turn a flush failure into GPU-accessible pool
             * corruption, so deliberately quarantine it after removing the
             * CPU mappings.
             */
            DXGKRNL_ERR("DXGKRNL: quarantining monitored-fence page %p after GPU VA 0x%I64x unmap failed 0x%08lX\n",
                        SyncObj->MonitoredValueKernelVa,
                        SyncObj->MonitoredValueGpuVa,
                        UnmapStatus);
            QuarantineKernelPage = TRUE;
        }
        SyncObj->MonitoredValueGpuVa = 0;
    }

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
        if (!QuarantineKernelPage)
            ExFreePoolWithTag(SyncObj->MonitoredValueKernelVa, TAG_DXGK_SYNC);
        SyncObj->MonitoredValueKernelVa = NULL;
    }

    if (SyncObj->MonitoredValueProcess != NULL)
    {
        ObDereferenceObject(SyncObj->MonitoredValueProcess);
        SyncObj->MonitoredValueProcess = NULL;
    }
}

VOID
DxgkpDereferenceSyncObject(
    _In_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    if (SyncObj != NULL &&
        InterlockedDecrement(&SyncObj->RefCount) == 0)
    {
        PDXGKRNL_DEVICE Device = SyncObj->Device;
        PDXGKRNL_SYNC_OBJECT Backing = SyncObj->BackingSyncObject;

        DxgkpSyncUnpublishShare(SyncObj);
        DxgkpSyncDestroyPeriodicNotification(SyncObj);
        DxgkpSyncReleaseMonitoredPage(SyncObj);
        if (SyncObj->CpuNotificationEventReferenced)
            ObDereferenceObject(SyncObj->CpuNotificationEvent);
        SyncObj->BackingSyncObject = NULL;
        ExFreePoolWithTag(SyncObj, TAG_DXGK_SYNC);
        if (Backing != NULL)
            DxgkpDereferenceSyncObject(Backing);
        if (Device != NULL)
            DxgkDereferenceDevice(Device);
    }
}

static VOID
DxgkpCpuWaitRequestDereference(
    _Inout_ PDXGKRNL_CPU_WAIT_REQUEST Request)
{
    ULONG Index;

    if (InterlockedDecrement(&Request->ReferenceCount) != 0)
        return;
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    for (Index = 0; Index < Request->ObjectCount; ++Index)
        DxgkpDereferenceSyncObject(Request->Objects[Index]);
    if (Request->EventObjectReferenced)
        ObDereferenceObject(Request->CompletionEvent);
    ExFreePoolWithTag(Request, TAG_DXGK_SYNC);
}

static VOID
NTAPI
DxgkpCpuWaitCleanupWorker(
    _In_ PVOID Context)
{
    PDXGKRNL_CPU_WAIT_REQUEST Request = Context;

    DxgkpCpuWaitRequestDereference(Request);
}

static VOID
NTAPI
DxgkpCpuWaitComplete(
    _Inout_ PDXGK_SYNC_WAIT_CORE_REQUEST CoreRequest,
    _In_ NTSTATUS Status,
    _In_opt_ PVOID Context)
{
    PDXGKRNL_CPU_WAIT_REQUEST Request = Context;
    LONG PreviousCleanupQueued;

    UNREFERENCED_PARAMETER(CoreRequest);
    UNREFERENCED_PARAMETER(Status);
    ASSERT(Request != NULL);
    PreviousCleanupQueued = InterlockedCompareExchange(&Request->CleanupQueued, 1, 0);
    ASSERT(PreviousCleanupQueued == 0);
    if (PreviousCleanupQueued != 0)
        return;
    KeSetEvent(Request->CompletionEvent, IO_NO_INCREMENT, FALSE);
    ExQueueWorkItem(&Request->CleanupWorkItem, DelayedWorkQueue);
}

static NTSTATUS
NTAPI
DxgkpCpuWaitAdmission(
    _In_ PDXGK_SYNC_WAIT_CORE_REQUEST CoreRequest,
    _In_opt_ PVOID Context)
{
    PDXGKRNL_CPU_WAIT_REQUEST Request = Context;
    ULONG Index;

    UNREFERENCED_PARAMETER(CoreRequest);
    if (Request == NULL || Request->Device == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&Request->Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Request->Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        return STATUS_DEVICE_REMOVED;
    for (Index = 0; Index < Request->ObjectCount; ++Index)
    {
        if (InterlockedCompareExchange(&Request->Objects[Index]->Destroying, 0, 0) != 0)
            return STATUS_DELETE_PENDING;
        if (Request->Objects[Index]->PublicType != D3DDDI_MONITORED_FENCE || Request->Objects[Index]->MonitoredValueKernelVa == NULL)
            return STATUS_INVALID_PARAMETER;
        if (Request->Objects[Index]->Flags.NoWait)
            return STATUS_ACCESS_DENIED;
    }
    return STATUS_SUCCESS;
}

static VOID
DxgkpReleaseSyncObjectArray(
    _In_reads_(ObjectCount) PDXGKRNL_SYNC_OBJECT *Objects,
    _In_ ULONG ObjectCount)
{
    ULONG Index;

    for (Index = 0; Index < ObjectCount; ++Index)
    {
        if (Objects[Index] != NULL)
            DxgkpDereferenceSyncObject(Objects[Index]);
    }
}

static NTSTATUS
DxgkpReferenceMonitoredFenceArray(
    _In_ PDXGKRNL_DEVICE Device,
    _In_reads_(ObjectCount) CONST D3DKMT_HANDLE *ObjectHandles,
    _In_ ULONG ObjectCount,
    _Out_writes_(ObjectCount) PDXGKRNL_SYNC_OBJECT *Objects)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Index;

    RtlZeroMemory(Objects, sizeof(*Objects) * ObjectCount);
    for (Index = 0; Index < ObjectCount; ++Index)
    {
        Status = DxgkpReferenceSyncObjectByHandle(ObjectHandles[Index], PsGetCurrentProcess(), &Objects[Index]);
        if (!NT_SUCCESS(Status))
            break;
        if (Objects[Index]->Device != Device || Objects[Index]->PublicType != D3DDDI_MONITORED_FENCE || Objects[Index]->MonitoredValueKernelVa == NULL)
        {
            Status = STATUS_INVALID_PARAMETER;
            ++Index;
            break;
        }
    }
    if (!NT_SUCCESS(Status))
        DxgkpReleaseSyncObjectArray(Objects, Index);
    return Status;
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
    _In_ D3DDDI_SYNCHRONIZATIONOBJECT_FLAGS Flags,
    _Out_ PVOID *UserVa,
    _Out_opt_ D3DGPU_VIRTUAL_ADDRESS *GpuVa)
{
    PDXGKRNL_SYNC_OBJECT SyncObj;
    NTSTATUS Status;

    PAGED_CODE();

    if (UserVa == NULL)
        return STATUS_INVALID_PARAMETER;
    *UserVa = NULL;
    if (GpuVa != NULL)
        *GpuVa = 0;

    Status = DxgkpReferenceSyncObjectByHandle(hSyncObject, PsGetCurrentProcess(), &SyncObj);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((SyncObj->PublicType != D3DDDI_MONITORED_FENCE &&
         SyncObj->PublicType != D3DDDI_PERIODIC_MONITORED_FENCE) ||
        SyncObj->MonitoredValueKernelVa != NULL)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_INVALID_PARAMETER;
    }

    /* Pool allocations of PAGE_SIZE are page-aligned. */
    SyncObj->MonitoredValueKernelVa =
        ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_DXGK_SYNC);
    if (SyncObj->MonitoredValueKernelVa == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }

    RtlZeroMemory(SyncObj->MonitoredValueKernelVa, PAGE_SIZE);

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

    /* GPU-side view of the value page on CPU_VIRTUAL GpuMmu adapters.
     * NoGPUAccess deliberately keeps this address zero and permits the
     * packet-only signal/wait contract without a GPU page-table mapping. */
    SyncObj->MonitoredValueGpuVa = 0;
    if (!Flags.NoGPUAccess && (SyncObj->Device == NULL || SyncObj->Device->ProcessRecord == NULL || SyncObj->Device->Adapter == NULL))
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Fail;
    }
    else if (!Flags.NoGPUAccess)
    {
        D3DGPU_VIRTUAL_ADDRESS FenceGpuVa = 0;

        Status = DxgkGpuVaMapFencePage(SyncObj->Device->Adapter, SyncObj->Device->ProcessRecord, SyncObj->MonitoredValueKernelVa, &FenceGpuVa);
        if (!NT_SUCCESS(Status))
            goto Fail;
        /*
         * Publish ownership before flushing so a rejected invalidation takes
         * the ordinary failure cleanup through UnmapFencePage.  That cleanup
         * either proves the translation gone or quarantines the backing.
         */
        SyncObj->MonitoredValueGpuVa = FenceGpuVa;
        Status = DxgkGpuVaFlushPageTableUpdates(SyncObj->Device->ProcessRecord);
        if (!NT_SUCCESS(Status))
            goto Fail;
    }

    ExAcquireFastMutex(&SyncObj->Device->DeviceMutex);
    if (InterlockedCompareExchange(&SyncObj->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&SyncObj->Device->Destroying, 0, 0) != 0)
    {
        ExReleaseFastMutex(&SyncObj->Device->DeviceMutex);
        Status = STATUS_DELETE_PENDING;
        goto Fail;
    }
    SyncObj->Flags = Flags;
    if ((InterlockedCompareExchange(&SyncObj->TdrAffected, 0, 0) != 0 || InterlockedCompareExchange(&SyncObj->Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE) && !Flags.NoSignal && !Flags.NoSignalMaxValueOnTdr)
        InterlockedExchange64(&SyncObj->FenceValue, -1);
    else
        InterlockedExchange64(&SyncObj->FenceValue, (LONG64)InitialFenceValue);
    *(volatile UINT64 *)SyncObj->MonitoredValueKernelVa = (UINT64)SyncObj->FenceValue;
    KeMemoryBarrier();
    ExReleaseFastMutex(&SyncObj->Device->DeviceMutex);

    if (SyncObj->Periodic)
    {
        Status = DxgkpSyncCreatePeriodicNotification(SyncObj);
        if (!NT_SUCCESS(Status))
            goto Fail;
    }

    *UserVa = SyncObj->MonitoredValueUserVa;
    if (GpuVa != NULL)
        *GpuVa = SyncObj->MonitoredValueGpuVa;
    DxgkpDereferenceSyncObject(SyncObj);
    return STATUS_SUCCESS;

Fail:
    DxgkpSyncDestroyPeriodicNotification(SyncObj);
    DxgkpSyncReleaseMonitoredPage(SyncObj);
    DxgkpDereferenceSyncObject(SyncObj);
    return Status;
}

static NTSTATUS
DxgkpCreateSynchronizationObjectInternal(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ CONST D3DDDI_SYNCHRONIZATIONOBJECTINFO2 *Info,
    _In_ BOOLEAN AllowMissingCpuNotificationEvent,
    _Out_ D3DKMT_HANDLE *SyncObjectHandle)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_SYNC_OBJECT SyncObj;
    BOOLEAN InitialEventState = FALSE;
    NTSTATUS Status;

    PAGED_CODE();
    if (Info == NULL || SyncObjectHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    *SyncObjectHandle = 0;
    if (Info->Type != D3DDDI_SYNCHRONIZATION_MUTEX && Info->Type != D3DDDI_SEMAPHORE && Info->Type != D3DDDI_FENCE && Info->Type != D3DDDI_CPU_NOTIFICATION && Info->Type != D3DDDI_MONITORED_FENCE && Info->Type != D3DDDI_PERIODIC_MONITORED_FENCE)
        return STATUS_NOT_SUPPORTED;
    if (Info->Type == D3DDDI_SEMAPHORE && (Info->Semaphore.MaxCount == 0 || Info->Semaphore.InitialCount > Info->Semaphore.MaxCount))
        return STATUS_INVALID_PARAMETER;
    if (Info->Type == D3DDDI_CPU_NOTIFICATION && Info->CPUNotification.Event == NULL && !AllowMissingCpuNotificationEvent)
        return STATUS_INVALID_PARAMETER;
    if ((Info->Type == D3DDDI_MONITORED_FENCE ||
         Info->Type == D3DDDI_PERIODIC_MONITORED_FENCE) &&
        (Info->Flags.TopOfPipeline || Info->Flags.Shared))
    {
        /*
         * TopOfPipeline needs a real scheduler submission-boundary signal,
         * and native sharing needs an NT synchronization-object/security
         * contract.  The private global-handle alias is neither.
         */
        return STATUS_NOT_SUPPORTED;
    }
    if (Info->Type == D3DDDI_PERIODIC_MONITORED_FENCE &&
        Info->PeriodicMonitoredFence.hAdapter == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * NoGPUAccess says the fence value gets no GPU-visible address, so nothing
     * on the GPU can read or write it.  Two combinations that asks for are
     * contradictory, and Windows refuses both:
     *
     *   Shared -- a shared fence exists so another device can wait on it, and a
     *   device waits on the GPU side.  Sharing a fence no GPU can see hands the
     *   opener something it can only ever poll from the CPU, which is not what
     *   the caller asked for and not what it will get told.
     *
     *   Periodic -- a periodic fence is advanced by the display hardware on its
     *   own vblank cadence.  That advance is a GPU-side write, so a periodic
     *   fence with no GPU access has nothing to move it and would sit at its
     *   initial value forever while looking healthy.
     *
     * Both used to be accepted here, which is worse than refusing: the caller
     * gets an object that behaves plausibly until something waits on it.
     */
    if (Info->Flags.NoGPUAccess)
    {
        if (Info->Type == D3DDDI_PERIODIC_MONITORED_FENCE)
            return STATUS_INVALID_PARAMETER;
        if (Info->Type == D3DDDI_MONITORED_FENCE && Info->Flags.Shared)
            return STATUS_INVALID_PARAMETER;
    }
    Status = DxgkReferenceOwnedDeviceByHandle(hDevice, PsGetCurrentProcess(), &Adapter, &Device);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Info->Type == D3DDDI_PERIODIC_MONITORED_FENCE &&
        !DxgkpSyncPeriodicCallbacksSupported(Adapter))
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
    SyncObj->Info.Type = Info->Type;
    SyncObj->PublicType = Info->Type;
    SyncObj->Flags = Info->Flags;
    SyncObj->RefCount = 1;
    switch (Info->Type)
    {
        case D3DDDI_SYNCHRONIZATION_MUTEX:
            SyncObj->Info.SynchronizationMutex.InitialState = Info->SynchronizationMutex.InitialState ? TRUE : FALSE;
            SyncObj->MutexOwned = Info->SynchronizationMutex.InitialState ? 1 : 0;
            SyncObj->FenceValue = SyncObj->MutexOwned ? 0 : 1;
            InitialEventState = !SyncObj->MutexOwned;
            break;
        case D3DDDI_SEMAPHORE:
            SyncObj->Info.Semaphore.MaxCount = Info->Semaphore.MaxCount;
            SyncObj->Info.Semaphore.InitialCount = Info->Semaphore.InitialCount;
            SyncObj->SemaphoreLimit = Info->Semaphore.MaxCount;
            SyncObj->SemaphoreCount = Info->Semaphore.InitialCount;
            SyncObj->FenceValue = Info->Semaphore.InitialCount;
            InitialEventState = Info->Semaphore.InitialCount != 0;
            break;
        case D3DDDI_FENCE:
            SyncObj->FenceValue = (LONG64)Info->Fence.FenceValue;
            InitialEventState = Info->Fence.FenceValue != 0;
            break;
        case D3DDDI_MONITORED_FENCE:
            SyncObj->FenceValue = (LONG64)Info->MonitoredFence.InitialFenceValue;
            InitialEventState = Info->MonitoredFence.InitialFenceValue != 0;
            break;
        case D3DDDI_PERIODIC_MONITORED_FENCE:
            /* The fence advances once per vertical blank of its bound source
             * instead of on explicit signals, so it starts at zero. */
            SyncObj->FenceValue = 0;
            SyncObj->Periodic = TRUE;
            SyncObj->PeriodicVidPnTargetId = Info->PeriodicMonitoredFence.VidPnTargetId;
            SyncObj->PeriodicTime = Info->PeriodicMonitoredFence.Time;
            InitialEventState = FALSE;
            break;
        case D3DDDI_CPU_NOTIFICATION:
            break;
        default:
            ASSERT(FALSE);
            break;
    }
    KeInitializeEvent(&SyncObj->CpuEvent, SynchronizationEvent, InitialEventState);
    InitializeListHead(&SyncObj->SyncObjListEntry);
    InitializeListHead(&SyncObj->DeviceSyncObjListEntry);
    InitializeListHead(&SyncObj->GlobalShareListEntry);
    InitializeListHead(&SyncObj->PeriodicListEntry);
    DxgkPeriodicNotificationCoreInitialize(
        &SyncObj->PeriodicNotification);
    ExInitializeRundownProtection(
        &SyncObj->PeriodicNotificationRundown);
    KeInitializeSpinLock(&SyncObj->PeriodicSignalLock);
    if (Info->Type == D3DDDI_CPU_NOTIFICATION && Info->CPUNotification.Event != NULL)
    {
        Status = ObReferenceObjectByHandle(Info->CPUNotification.Event, EVENT_MODIFY_STATE, *ExEventObjectType, UserMode, (PVOID *)&SyncObj->CpuNotificationEvent, NULL);
        if (!NT_SUCCESS(Status))
        {
            InterlockedExchange(&SyncObj->Destroying, 1);
            DxgkpDereferenceSyncObject(SyncObj);
            return Status;
        }
        SyncObj->CpuNotificationEventReferenced = TRUE;
    }
    Status = DxgkCreateOwnedHandle(DxgkHandleTypeSynchronizationObject, SyncObj, Adapter, SyncObj->OwnerProcess, &SyncObj->Destroying, &SyncObj->TeardownClaimed, &SyncObj->Handle);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&SyncObj->Destroying, 1);
        DxgkpDereferenceSyncObject(SyncObj);
        return Status;
    }

    ExAcquireFastMutex(&Device->DeviceMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        Status = InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 ? STATUS_DELETE_PENDING : STATUS_DEVICE_REMOVED;
        ExReleaseFastMutex(&Device->DeviceMutex);
        DxgkRemoveOwnedHandleObject(DxgkHandleTypeSynchronizationObject, SyncObj);
        InterlockedExchange(&SyncObj->Destroying, 1);
        DxgkpDereferenceSyncObject(SyncObj);
        return Status;
    }
    InsertTailList(&Device->SyncObjListHead, &SyncObj->DeviceSyncObjListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);
    if (SyncObj->Flags.Shared)
        (VOID)DxgkpSyncPublishShare(SyncObj);
    *SyncObjectHandle = SyncObj->Handle;
    DXGKRNL_TRACE("DxgkCreateSynchronizationObject: handle=0x%X type=%d shared=0x%X\n", SyncObj->Handle, SyncObj->Info.Type, SyncObj->GlobalShareHandle);
    return STATUS_SUCCESS;
}

/*
 * DxgkSyncObjectQueryShareHandle
 *
 * Reports the global share handle a shareable object was published under, or
 * zero when the object is not shareable.
 */
D3DKMT_HANDLE
DxgkSyncObjectQueryShareHandle(
    _In_ D3DKMT_HANDLE hSyncObject)
{
    PDXGKRNL_SYNC_OBJECT SyncObj;
    D3DKMT_HANDLE Share = 0;
    PVOID Object;

    if (!NT_SUCCESS(DxgkReferenceOwnedHandle(hSyncObject, DxgkHandleTypeSynchronizationObject, PsGetCurrentProcess(), DxgkpReferenceSyncObject, &Object)))
        return 0;
    SyncObj = Object;
    if (SyncObj->Shareable)
        Share = SyncObj->GlobalShareHandle;
    DxgkpDereferenceSyncObject(SyncObj);
    return Share;
}

/*
 * DxgkOpenSynchronizationObject
 *
 * Opens a shared synchronization object into the calling process's device.
 * The alias holds a reference on the authoritative object and observes the
 * same state, including the single monitored value page: a monitored fence
 * alias maps that page into the opening process and, on a GpuMmu adapter,
 * into its GPU address space, so both views read one fence value.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkOpenSynchronizationObject(
    _Inout_ D3DKMT_OPENSYNCHRONIZATIONOBJECT *pData)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_SYNC_OBJECT Backing = NULL;
    PDXGKRNL_SYNC_OBJECT Alias;
    PLIST_ENTRY Entry;
    NTSTATUS Status;
    UINT Index;

    PAGED_CODE();
    if (pData == NULL)
        return STATUS_INVALID_PARAMETER;
    if (pData->hSharedHandle == 0)
        return STATUS_INVALID_PARAMETER;
    for (Index = 0; Index < RTL_NUMBER_OF(pData->Reserved); ++Index)
    {
        if (pData->Reserved[Index] != 0)
            return STATUS_INVALID_PARAMETER;
    }
    pData->hSyncObject = 0;

    DxgkpSyncShareInitialize();
    ExAcquireFastMutex(&DxgkSyncShareLock);
    for (Entry = DxgkSyncShareListHead.Flink; Entry != &DxgkSyncShareListHead; Entry = Entry->Flink)
    {
        PDXGKRNL_SYNC_OBJECT Candidate = CONTAINING_RECORD(Entry, DXGKRNL_SYNC_OBJECT, GlobalShareListEntry);

        if (Candidate->Shareable && Candidate->GlobalShareHandle == pData->hSharedHandle)
        {
            if (DxgkpReferenceSyncObject(Candidate))
                Backing = Candidate;
            break;
        }
    }
    ExReleaseFastMutex(&DxgkSyncShareLock);
    if (Backing == NULL)
        return STATUS_INVALID_HANDLE;

    /* The alias belongs to a device the calling process owns on the backing
     * object's adapter; a process with no such device cannot open it. */
    Adapter = Backing->Device != NULL ? Backing->Device->Adapter : NULL;
    Device = NULL;
    if (Adapter != NULL)
    {
        (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
        for (Entry = Adapter->DeviceListHead.Flink; Entry != &Adapter->DeviceListHead; Entry = Entry->Flink)
        {
            PDXGKRNL_DEVICE Candidate = CONTAINING_RECORD(Entry, DXGKRNL_DEVICE, DeviceListEntry);

            if (Candidate->OwnerProcess == PsGetCurrentProcess() &&
                InterlockedCompareExchange(&Candidate->ExecutionState, 0, 0) == D3DKMT_DEVICEEXECUTION_ACTIVE &&
                DxgkReferenceDevice(Candidate))
            {
                Device = Candidate;
                break;
            }
        }
        KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
    }
    if (Device == NULL)
    {
        DxgkpDereferenceSyncObject(Backing);
        return STATUS_INVALID_HANDLE;
    }

    Alias = (PDXGKRNL_SYNC_OBJECT)ExAllocatePoolWithTag(NonPagedPool, sizeof(*Alias), TAG_DXGK_SYNC);
    if (Alias == NULL)
    {
        DxgkDereferenceDevice(Device);
        DxgkpDereferenceSyncObject(Backing);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Alias, sizeof(*Alias));
    Alias->hDevice = Device->Handle;
    Alias->Device = Device;
    Alias->OwnerProcess = PsGetCurrentProcess();
    Alias->Info.Type = Backing->Info.Type;
    Alias->PublicType = Backing->PublicType;
    Alias->Flags = Backing->Flags;
    Alias->Flags.Shared = 0;
    Alias->RefCount = 1;
    Alias->BackingSyncObject = Backing;
    Alias->SemaphoreLimit = Backing->SemaphoreLimit;
    Alias->Periodic = Backing->Periodic;
    Alias->PeriodicVidPnTargetId = Backing->PeriodicVidPnTargetId;
    KeInitializeEvent(&Alias->CpuEvent, SynchronizationEvent, FALSE);
    InitializeListHead(&Alias->SyncObjListEntry);
    InitializeListHead(&Alias->DeviceSyncObjListEntry);
    InitializeListHead(&Alias->GlobalShareListEntry);
    InitializeListHead(&Alias->PeriodicListEntry);

    Status = DxgkCreateOwnedHandle(DxgkHandleTypeSynchronizationObject, Alias, Adapter, Alias->OwnerProcess, &Alias->Destroying, &Alias->TeardownClaimed, &Alias->Handle);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Alias->Destroying, 1);
        DxgkpDereferenceSyncObject(Alias);
        return Status;
    }

    ExAcquireFastMutex(&Device->DeviceMutex);
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        Status = InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 ? STATUS_DELETE_PENDING : STATUS_DEVICE_REMOVED;
        ExReleaseFastMutex(&Device->DeviceMutex);
        DxgkRemoveOwnedHandleObject(DxgkHandleTypeSynchronizationObject, Alias);
        InterlockedExchange(&Alias->Destroying, 1);
        DxgkpDereferenceSyncObject(Alias);
        return Status;
    }
    InsertTailList(&Device->SyncObjListHead, &Alias->DeviceSyncObjListEntry);
    ExReleaseFastMutex(&Device->DeviceMutex);

    pData->hSyncObject = Alias->Handle;
    return STATUS_SUCCESS;
}

/*
 * DxgkpSyncResolveShared
 *
 * An opened alias holds no fence state of its own: the object created with
 * the Shared flag remains authoritative, including the single monitored value
 * page both views observe.  Every state operation therefore runs against the
 * backing object, while admission still checks the access flags of the view
 * the caller actually holds.
 */
static PDXGKRNL_SYNC_OBJECT
DxgkpSyncResolveShared(
    _In_ PDXGKRNL_SYNC_OBJECT SyncObj)
{
    while (SyncObj != NULL && SyncObj->BackingSyncObject != NULL)
        SyncObj = SyncObj->BackingSyncObject;
    return SyncObj;
}

NTSTATUS
NTAPI
DxgkCreateSynchronizationObject2Core(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ CONST D3DDDI_SYNCHRONIZATIONOBJECTINFO2 *Info,
    _Out_ D3DKMT_HANDLE *SyncObjectHandle)
{
    return DxgkpCreateSynchronizationObjectInternal(hDevice, Info, FALSE, SyncObjectHandle);
}

/*
 * DxgkCreateSynchronizationObject
 *
 * Creates a first-generation synchronization object while retaining the same
 * normalized lifetime and initial-state representation used by Create2.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkCreateSynchronizationObject(
    _Inout_ D3DKMT_CREATESYNCHRONIZATIONOBJECT *pCreateSyncObject)
{
    D3DDDI_SYNCHRONIZATIONOBJECTINFO2 Info;

    if (pCreateSyncObject == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Type = pCreateSyncObject->Info.Type;
    if (Info.Type == D3DDDI_SYNCHRONIZATION_MUTEX)
        Info.SynchronizationMutex.InitialState = pCreateSyncObject->Info.SynchronizationMutex.InitialState;
    else if (Info.Type == D3DDDI_SEMAPHORE)
    {
        Info.Semaphore.MaxCount = pCreateSyncObject->Info.Semaphore.MaxCount;
        Info.Semaphore.InitialCount = pCreateSyncObject->Info.Semaphore.InitialCount;
    }
    return DxgkpCreateSynchronizationObjectInternal(pCreateSyncObject->hDevice, &Info, TRUE, &pCreateSyncObject->hSyncObject);
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
    DxgkSyncWaitCoreCancelObject(&SyncObj->Device->SyncWaitRegistry, SyncObj, STATUS_DELETE_PENDING);
    DxgkpSyncDestroyPeriodicNotification(SyncObj);
    KeSetEvent(&SyncObj->CpuEvent, IO_NO_INCREMENT, FALSE);

    DXGKRNL_TRACE("DxgkDestroySynchronizationObject: handle=0x%X\n", pDestroySyncObject->hSyncObject);

    DxgkpDereferenceSyncObject(SyncObj);
    return STATUS_SUCCESS;
}

/*
 * DxgkSignalSynchronizationObject
 *
 * Inserts a signal marker in the specified GPU context stream.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkSignalSynchronizationObject(
    _In_ D3DKMT_SIGNALSYNCHRONIZATIONOBJECT *pSignalSyncObject)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();

    if (pSignalSyncObject == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pSignalSyncObject->ObjectCount == 0 || pSignalSyncObject->ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED)
        return STATUS_INVALID_PARAMETER;
    if ((pSignalSyncObject->Flags.Value & ~DXGK_CONTEXT_SYNC_SIGNAL_AT_SUBMISSION) != 0)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceContextByHandle(pSignalSyncObject->hContext, PsGetCurrentProcess(), &Adapter, &Device, &Context);
    if (!NT_SUCCESS(Status))
        return Status;
    ASSERT(Context->Device == Device && Device->Adapter == Adapter);
    Status = DxgkContextOrderAdmitSignal(&Context, 1, DxgkContextSyncOperationLegacySignal, pSignalSyncObject->ObjectHandleArray, pSignalSyncObject->ObjectCount, pSignalSyncObject->Flags.Value, 0, NULL, UserMode);
    DxgkDereferenceContext(Context);
    return Status;
}

/*
 * DxgkWaitForSynchronizationObject
 *
 * Inserts a wait marker in the specified GPU context stream.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
NTAPI
DxgkWaitForSynchronizationObject(
    _In_ D3DKMT_WAITFORSYNCHRONIZATIONOBJECT *pWaitSyncObject)
{
    PDXGKRNL_ADAPTER Adapter;
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();

    if (pWaitSyncObject == NULL)
        return STATUS_INVALID_PARAMETER;

    if (pWaitSyncObject->ObjectCount == 0 || pWaitSyncObject->ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkReferenceContextByHandle(pWaitSyncObject->hContext, PsGetCurrentProcess(), &Adapter, &Device, &Context);
    if (!NT_SUCCESS(Status))
        return Status;
    ASSERT(Context->Device == Device && Device->Adapter == Adapter);
    Status = DxgkContextOrderAdmitWait(Context, DxgkContextSyncOperationLegacyWait, pWaitSyncObject->ObjectHandleArray, pWaitSyncObject->ObjectCount, 0, NULL, UserMode);
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
static NTSTATUS
NTAPI
DxgkpSyncPublishAdmission(
    _In_opt_ PVOID Context)
{
    PDXGKRNL_SYNC_PUBLISH_ADMISSION Admission = Context;
    ULONG Index;

    if (Admission == NULL || Admission->Device == NULL || Admission->Objects == NULL || Admission->ObjectCount == 0)
        return STATUS_INVALID_PARAMETER;
    if (Admission->PublicCpuSignal && (InterlockedCompareExchange(&Admission->Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Admission->Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE))
        return STATUS_DEVICE_REMOVED;
    for (Index = 0; Index < Admission->ObjectCount; ++Index)
    {
        PDXGKRNL_SYNC_OBJECT SyncObj = Admission->Objects[Index];
        BOOLEAN InternalPeriodic =
            !Admission->PublicCpuSignal &&
            SyncObj != NULL &&
            SyncObj->PublicType == D3DDDI_PERIODIC_MONITORED_FENCE;

        if (SyncObj == NULL || SyncObj->Device != Admission->Device ||
            (SyncObj->PublicType != D3DDDI_MONITORED_FENCE &&
             !InternalPeriodic) ||
            SyncObj->MonitoredValueKernelVa == NULL)
            return STATUS_INVALID_PARAMETER;
        if (Admission->PublicCpuSignal && InterlockedCompareExchange(&SyncObj->Destroying, 0, 0) != 0)
            return STATUS_DELETE_PENDING;
        if (Admission->PublicCpuSignal && SyncObj->Flags.NoSignal)
            return STATUS_ACCESS_DENIED;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkSyncPublishFenceBatch(
    _In_reads_(ObjectCount) PDXGKRNL_SYNC_OBJECT *Objects,
    _In_reads_(ObjectCount) CONST UINT64 *FenceValues,
    _In_ ULONG ObjectCount,
    _In_ BOOLEAN AllowFenceRewind,
    _In_ BOOLEAN PublicCpuSignal)
{
    DXGK_SYNC_WAIT_CORE_UPDATE Updates[D3DDDI_MAX_OBJECT_SIGNALED];
    DXGKRNL_SYNC_PUBLISH_ADMISSION Admission;
    PDXGKRNL_DEVICE Device;
    NTSTATUS Status;
    ULONG Index;

    if (Objects == NULL || FenceValues == NULL || ObjectCount == 0 || ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED || Objects[0] == NULL)
        return STATUS_INVALID_PARAMETER;
    Device = Objects[0]->Device;
    for (Index = 0; Index < ObjectCount; ++Index)
    {
        if (Objects[Index] == NULL || Objects[Index]->Device != Device)
            return STATUS_INVALID_PARAMETER;
        Updates[Index].Object = Objects[Index];
        Updates[Index].FenceValue = &Objects[Index]->FenceValue;
        Updates[Index].PublishedValue = (volatile UINT64 *)Objects[Index]->MonitoredValueKernelVa;
        Updates[Index].NewValue = FenceValues[Index];
    }
    Admission.Device = Device;
    Admission.Objects = Objects;
    Admission.ObjectCount = ObjectCount;
    Admission.PublicCpuSignal = PublicCpuSignal;
    Status = DxgkSyncWaitCorePublishBatch(&Device->SyncWaitRegistry, Updates, ObjectCount, AllowFenceRewind, DxgkpSyncPublishAdmission, &Admission);
    if (NT_SUCCESS(Status))
    {
        for (Index = 0; Index < ObjectCount; ++Index)
            KeSetEvent(&Objects[Index]->CpuEvent, IO_NO_INCREMENT, FALSE);
    }
    return Status;
}

static NTSTATUS
DxgkpSyncObjectPublishRetiredFence(
    _In_ PDXGKRNL_SYNC_OBJECT SyncObj,
    _In_ UINT64 FenceValue)
{
    PDXGKRNL_SYNC_OBJECT Objects[1];
    UINT64 FenceValues[1];

    Objects[0] = SyncObj;
    FenceValues[0] = FenceValue;
    return DxgkSyncPublishFenceBatch(Objects, FenceValues, RTL_NUMBER_OF(Objects), FALSE, FALSE);
}

VOID
NTAPI
DxgkTdrResetAdapterSynchronizationObjects(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PLIST_ENTRY DeviceLink;

    PAGED_CODE();
    if (Adapter == NULL)
        return;

    (VOID)KeWaitForSingleObject(&Adapter->AdapterMutex, Executive, KernelMode, FALSE, NULL);
    for (DeviceLink = Adapter->DeviceListHead.Flink; DeviceLink != &Adapter->DeviceListHead; DeviceLink = DeviceLink->Flink)
    {
        PDXGKRNL_DEVICE Device = CONTAINING_RECORD(DeviceLink, DXGKRNL_DEVICE, DeviceListEntry);
        PLIST_ENTRY SyncLink;

        DxgkDeviceSetExecutionState(Device, D3DKMT_DEVICEEXECUTION_RESET);
        ExAcquireFastMutex(&Device->DeviceMutex);
        for (SyncLink = Device->SyncObjListHead.Flink; SyncLink != &Device->SyncObjListHead; SyncLink = SyncLink->Flink)
        {
            PDXGKRNL_SYNC_OBJECT SyncObj = CONTAINING_RECORD(SyncLink, DXGKRNL_SYNC_OBJECT, DeviceSyncObjListEntry);

            InterlockedExchange(&SyncObj->TdrAffected, 1);
            if (SyncObj->MonitoredValueKernelVa != NULL && !SyncObj->Flags.NoSignal && !SyncObj->Flags.NoSignalMaxValueOnTdr)
            {
                (VOID)DxgkpSyncObjectPublishRetiredFence(SyncObj, (UINT64)-1);
            }
            else
            {
                KeSetEvent(&SyncObj->CpuEvent, IO_NO_INCREMENT, FALSE);
                if (SyncObj->PublicType == D3DDDI_CPU_NOTIFICATION && SyncObj->CpuNotificationEvent != NULL)
                    KeSetEvent(SyncObj->CpuNotificationEvent, IO_NO_INCREMENT, FALSE);
            }
        }
        DxgkSyncObjectCancelDeviceWaits(Device, STATUS_DEVICE_REMOVED, TRUE);
        ExReleaseFastMutex(&Device->DeviceMutex);
        DxgkContextOrderWakeDevice(Device);
    }
    KeReleaseMutex(&Adapter->AdapterMutex, FALSE);
}

NTSTATUS
NTAPI
DxgkSyncObjectCpuSignal(
    _In_ D3DKMT_HANDLE hDevice,
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ UINT64 FenceValue)
{
    PDXGKRNL_SYNC_OBJECT SyncObj;
    PDXGKRNL_SYNC_OBJECT Objects[1];
    UINT64 FenceValues[1];
    NTSTATUS Status;

    PAGED_CODE();

    Status = DxgkpReferenceSyncObjectByHandle(hSyncObject, PsGetCurrentProcess(), &SyncObj);
    if (!NT_SUCCESS(Status))
        return Status;

    if ((hDevice != 0 && SyncObj->hDevice != hDevice) || SyncObj->PublicType != D3DDDI_MONITORED_FENCE || SyncObj->MonitoredValueKernelVa == NULL)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_INVALID_PARAMETER;
    }
    Objects[0] = SyncObj;
    FenceValues[0] = FenceValue;
    Status = DxgkSyncPublishFenceBatch(Objects, FenceValues, RTL_NUMBER_OF(Objects), FALSE, TRUE);
    DxgkpDereferenceSyncObject(SyncObj);
    return Status;
}

NTSTATUS
NTAPI
DxgkSyncObjectCpuSignalBatch(
    _In_ PDXGKRNL_DEVICE Device,
    _In_reads_(ObjectCount) CONST D3DKMT_HANDLE *ObjectHandles,
    _In_reads_(ObjectCount) CONST UINT64 *FenceValues,
    _In_ ULONG ObjectCount,
    _In_ D3DDDICB_SIGNALFLAGS Flags)
{
    PDXGKRNL_SYNC_OBJECT Objects[D3DDDI_MAX_OBJECT_SIGNALED];
    NTSTATUS Status;

    PAGED_CODE();
    if (Device == NULL || ObjectHandles == NULL || FenceValues == NULL || ObjectCount == 0 || ObjectCount > D3DDDI_MAX_OBJECT_SIGNALED)
        return STATUS_INVALID_PARAMETER;
    if ((Flags.Value & ~DXGK_CPU_SIGNAL_ALLOW_FENCE_REWIND) != 0)
        return STATUS_NOT_SUPPORTED;
    Status = DxgkpReferenceMonitoredFenceArray(Device, ObjectHandles, ObjectCount, Objects);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DxgkSyncPublishFenceBatch(Objects, FenceValues, ObjectCount, Flags.AllowFenceRewind != 0, TRUE);
    DxgkpReleaseSyncObjectArray(Objects, ObjectCount);
    return Status;
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
    NTSTATUS Status;

    PAGED_CODE();

    Status = DxgkpReferenceSyncObjectByHandle(hSyncObject, NULL, &SyncObj);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = SyncObj->Flags.NoSignal ? STATUS_ACCESS_DENIED : DxgkpSyncObjectPublishRetiredFence(SyncObj, FenceValue);
    DxgkpDereferenceSyncObject(SyncObj);
    return Status;
}

/*
 * Retain a monitored fence across an in-flight tracked submission.  The
 * public handle may be destroyed as soon as the packet is submitted; the
 * object and its value page must nevertheless survive until retire/cancel.
 */
NTSTATUS
NTAPI
DxgkSyncObjectReferenceTrackedSignal(
    _In_ D3DKMT_HANDLE hSyncObject,
    _In_ PDXGKRNL_DEVICE Device,
    _Outptr_ PVOID *Reference)
{
    PDXGKRNL_SYNC_OBJECT SyncObj;
    NTSTATUS Status;

    PAGED_CODE();
    if (Device == NULL || Reference == NULL)
        return STATUS_INVALID_PARAMETER;
    *Reference = NULL;

    Status = DxgkpReferenceSyncObjectByHandle(hSyncObject, PsGetCurrentProcess(), &SyncObj);
    if (!NT_SUCCESS(Status))
        return Status;
    if (SyncObj->Device != Device)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_INVALID_HANDLE;
    }
    if (SyncObj->PublicType != D3DDDI_MONITORED_FENCE || SyncObj->MonitoredValueKernelVa == NULL)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_NOT_SUPPORTED;
    }
    if (SyncObj->Flags.NoSignal)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_ACCESS_DENIED;
    }

    *Reference = SyncObj;
    return STATUS_SUCCESS;
}

VOID
NTAPI
DxgkSyncObjectPublishTrackedSignal(
    _In_opt_ PVOID Reference,
    _In_ UINT64 FenceValue)
{
    PDXGKRNL_SYNC_OBJECT SyncObj = Reference;

    if (SyncObj != NULL)
        (VOID)DxgkpSyncObjectPublishRetiredFence(SyncObj, FenceValue);
}

VOID
NTAPI
DxgkSyncObjectReleaseTrackedSignal(
    _In_opt_ PVOID Reference,
    _In_ BOOLEAN Completed,
    _In_ UINT64 FenceValue)
{
    PDXGKRNL_SYNC_OBJECT SyncObj = Reference;

    PAGED_CODE();
    if (SyncObj == NULL)
        return;
    if (Completed)
        DxgkSyncObjectPublishTrackedSignal(SyncObj, FenceValue);
    DxgkpDereferenceSyncObject(SyncObj);
}

NTSTATUS
NTAPI
DxgkSyncObjectCpuWaitBatch(
    _In_ PDXGKRNL_DEVICE Device,
    _In_reads_(ObjectCount) CONST D3DKMT_HANDLE *ObjectHandles,
    _In_reads_(ObjectCount) CONST UINT64 *FenceValues,
    _In_ ULONG ObjectCount,
    _In_opt_ HANDLE AsyncEventHandle,
    _In_ BOOLEAN WaitAny)
{
    PDXGKRNL_CPU_WAIT_REQUEST Request;
    NTSTATUS Status;
    ULONG Index;

    PAGED_CODE();
    if (Device == NULL || ObjectHandles == NULL || FenceValues == NULL || ObjectCount == 0 || ObjectCount > D3DDDI_MAX_OBJECT_WAITED_ON)
        return STATUS_INVALID_PARAMETER;
    Request = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Request), TAG_DXGK_SYNC);
    if (Request == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Request, sizeof(*Request));
    Request->ReferenceCount = 1;
    Request->Device = Device;
    /*
     * The event is taken first so a refused batch can still signal it: Windows
     * 11 wakes the caller's event even when it rejects the wait request, and an
     * application that waits on the event after a failed call would otherwise
     * hang here while it proceeds there.
     */
    if (AsyncEventHandle != NULL)
    {
        Status = ObReferenceObjectByHandle(AsyncEventHandle, EVENT_MODIFY_STATE, *ExEventObjectType, UserMode, (PVOID *)&Request->CompletionEvent, NULL);
        if (!NT_SUCCESS(Status))
        {
            DxgkpCpuWaitRequestDereference(Request);
            return Status;
        }
        Request->EventObjectReferenced = TRUE;
    }
    else
    {
        KeInitializeEvent(&Request->SynchronousEvent, NotificationEvent, FALSE);
        Request->CompletionEvent = &Request->SynchronousEvent;
    }
    Status = DxgkpReferenceMonitoredFenceArray(Device, ObjectHandles, ObjectCount, Request->Objects);
    if (!NT_SUCCESS(Status))
    {
        if (Request->EventObjectReferenced)
            KeSetEvent(Request->CompletionEvent, IO_NO_INCREMENT, FALSE);
        DxgkpCpuWaitRequestDereference(Request);
        return Status;
    }
    Request->ObjectCount = ObjectCount;
    for (Index = 0; Index < ObjectCount; ++Index)
    {
        Request->Targets[Index].Object = Request->Objects[Index];
        Request->Targets[Index].FenceValue = &Request->Objects[Index]->FenceValue;
        Request->Targets[Index].TargetValue = FenceValues[Index];
    }
    ExInitializeWorkItem(&Request->CleanupWorkItem, DxgkpCpuWaitCleanupWorker, Request);
    DxgkSyncWaitCoreInitializeRequest(&Request->CoreRequest, Request->Targets, ObjectCount, WaitAny, DxgkpCpuWaitAdmission, DxgkpCpuWaitComplete, Request);
    InterlockedIncrement(&Request->ReferenceCount);
    Status = DxgkSyncWaitCoreRegister(&Device->SyncWaitRegistry, &Request->CoreRequest);
    if (!NT_SUCCESS(Status))
    {
        if (Request->EventObjectReferenced)
            KeSetEvent(Request->CompletionEvent, IO_NO_INCREMENT, FALSE);
        DxgkpCpuWaitRequestDereference(Request);
        DxgkpCpuWaitRequestDereference(Request);
        return Status;
    }
    if (AsyncEventHandle != NULL)
    {
        DxgkpCpuWaitRequestDereference(Request);
        return STATUS_SUCCESS;
    }
    (VOID)KeWaitForSingleObject(&Request->SynchronousEvent, Executive, KernelMode, FALSE, NULL);
    Status = Request->CoreRequest.CompletionStatus;
    DxgkpCpuWaitRequestDereference(Request);
    return Status;
}

VOID
NTAPI
DxgkSyncObjectCancelDeviceWaits(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ NTSTATUS Status,
    _In_ BOOLEAN ShutDown)
{
    if (Device == NULL)
        return;
    DxgkSyncWaitCoreCancelAll(&Device->SyncWaitRegistry, Status, ShutDown);
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

    if ((hDevice != 0 && SyncObj->hDevice != hDevice) || SyncObj->PublicType != D3DDDI_MONITORED_FENCE || SyncObj->MonitoredValueKernelVa == NULL)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_INVALID_PARAMETER;
    }
    if (SyncObj->Flags.NoWait)
    {
        DxgkpDereferenceSyncObject(SyncObj);
        return STATUS_ACCESS_DENIED;
    }
    if (NonBlocking)
    {
        BOOLEAN Reached = ((UINT64)SyncObj->FenceValue >= FenceValue);
        BOOLEAN TdrAffected = InterlockedCompareExchange(&SyncObj->TdrAffected, 0, 0) != 0;

        DxgkpDereferenceSyncObject(SyncObj);
        return Reached ? STATUS_SUCCESS : (TdrAffected ? STATUS_DEVICE_REMOVED : STATUS_TIMEOUT);
    }
    Status = DxgkSyncObjectCpuWaitBatch(SyncObj->Device, &hSyncObject, &FenceValue, 1, NULL, FALSE);
    DxgkpDereferenceSyncObject(SyncObj);
    return Status;
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
        DxgkSyncWaitCoreCancelObject(&Device->SyncWaitRegistry, SyncObj, STATUS_DELETE_PENDING);

        if (!OwnsTeardown)
        {
            /* The direct owner retains this Device until its final release. */
            continue;
        }
        ASSERT(InterlockedCompareExchange(&SyncObj->TeardownClaimed, 1, 1) == 1);
        DxgkpSyncDestroyPeriodicNotification(SyncObj);
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
