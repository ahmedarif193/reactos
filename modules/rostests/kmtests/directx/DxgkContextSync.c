/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Scheduler-ordered synchronization state contract tests
 */

#include <kmt_test.h>
#include "context_sync_core.h"

typedef struct _DXGK_CONTEXT_SYNC_TEST_OBJECT
{
    DXGK_CONTEXT_SYNC_CORE_OBJECT Core;
    volatile LONG Destroying;
    volatile LONG64 FenceValue;
    volatile LONG MutexOwned;
    volatile LONG64 SemaphoreCount;
    KEVENT StateEvent;
    KEVENT NotificationEvent;
} DXGK_CONTEXT_SYNC_TEST_OBJECT, *PDXGK_CONTEXT_SYNC_TEST_OBJECT;

typedef struct _DXGK_CONTEXT_SYNC_RELEASE_TEST
{
    LONG TotalCount;
    LONG KindCounts[DxgkContextSyncReferenceEvent + 1];
} DXGK_CONTEXT_SYNC_RELEASE_TEST, *PDXGK_CONTEXT_SYNC_RELEASE_TEST;

static VOID
DxgkContextSyncTestInitializeObject(
    _Out_ PDXGK_CONTEXT_SYNC_TEST_OBJECT Object,
    _In_ D3DDDI_SYNCHRONIZATIONOBJECT_TYPE Type,
    _In_ LONG64 InitialValue,
    _In_ ULONG SemaphoreLimit)
{
    RtlZeroMemory(Object, sizeof(*Object));
    Object->FenceValue = InitialValue;
    Object->MutexOwned = Type == D3DDDI_SYNCHRONIZATION_MUTEX && InitialValue == 0;
    Object->SemaphoreCount = InitialValue;
    KeInitializeEvent(&Object->StateEvent, NotificationEvent, InitialValue != 0);
    KeInitializeEvent(&Object->NotificationEvent, NotificationEvent, FALSE);
    Object->Core.Identity = Object;
    Object->Core.Type = Type;
    Object->Core.Destroying = &Object->Destroying;
    Object->Core.FenceValue = &Object->FenceValue;
    Object->Core.MutexOwned = &Object->MutexOwned;
    Object->Core.SemaphoreCount = &Object->SemaphoreCount;
    Object->Core.SemaphoreLimit = SemaphoreLimit;
    Object->Core.StateEvent = &Object->StateEvent;
    Object->Core.NotificationEvent = Type == D3DDDI_CPU_NOTIFICATION ? &Object->NotificationEvent : NULL;
}

static VOID
NTAPI
DxgkContextSyncTestRelease(
    _In_ PVOID Object,
    _In_ DXGK_CONTEXT_SYNC_REFERENCE_KIND Kind,
    _In_opt_ PVOID Context)
{
    PDXGK_CONTEXT_SYNC_RELEASE_TEST Test = Context;

    ok(Object != NULL, "released a null reference\n");
    ok(Kind > DxgkContextSyncReferenceInvalid && Kind <= DxgkContextSyncReferenceEvent, "invalid release kind %lu\n", Kind);
    InterlockedIncrement(&Test->TotalCount);
    InterlockedIncrement(&Test->KindCounts[Kind]);
}

static VOID
DxgkContextSyncTestValidation(VOID)
{
    DXGK_CONTEXT_SYNC_TEST_OBJECT Mutex;
    DXGK_CONTEXT_SYNC_TEST_OBJECT Semaphore;
    DXGK_CONTEXT_SYNC_TEST_OBJECT Fence;
    DXGK_CONTEXT_SYNC_TEST_OBJECT CpuNotification;
    DXGK_CONTEXT_SYNC_TEST_OBJECT MonitoredFence;
    DXGK_CONTEXT_SYNC_CORE_OBJECT Batch[2];
    NTSTATUS Status;

    DxgkContextSyncTestInitializeObject(&Mutex, D3DDDI_SYNCHRONIZATION_MUTEX, 1, 0);
    DxgkContextSyncTestInitializeObject(&Semaphore, D3DDDI_SEMAPHORE, 1, 3);
    DxgkContextSyncTestInitializeObject(&Fence, D3DDDI_FENCE, 7, 0);
    DxgkContextSyncTestInitializeObject(&CpuNotification, D3DDDI_CPU_NOTIFICATION, 0, 0);
    DxgkContextSyncTestInitializeObject(&MonitoredFence, DXGK_CONTEXT_SYNC_TYPE_MONITORED_FENCE, 7, 0);
    Batch[0] = Mutex.Core;
    Batch[1] = Semaphore.Core;
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationLegacyWait, Batch, RTL_NUMBER_OF(Batch), 0, 0, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationWait2, &Fence.Core, 1, 0, 7, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Batch[0] = Fence.Core;
    Batch[1] = Fence.Core;
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationWait2, Batch, RTL_NUMBER_OF(Batch), 0, 7, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationLegacyWait, &Fence.Core, 1, 0, 0, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationWait2, &CpuNotification.Core, 1, 0, 0, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationWait2, &MonitoredFence.Core, 1, 0, 7, FALSE);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationSignal2, NULL, 0, DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT, 0, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationSignal2, NULL, 0, DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT, 0, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationSignal2, &Mutex.Core, 1, DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT, 0, TRUE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationSignal2, &Fence.Core, 1, DXGK_CONTEXT_SYNC_ALLOW_FENCE_REWIND, 5, FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationSignal2, &Mutex.Core, 1, DXGK_CONTEXT_SYNC_ALLOW_FENCE_REWIND, 0, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationSignal2, &Mutex.Core, 1, 0x00000008UL, 0, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationSignal2, &Mutex.Core, 1, 0x80000000UL, 0, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationLegacySignal, &Mutex.Core, 1, DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT, 0, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationSignal2, &Semaphore.Core, 1, 0, 1, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationSignal2, &MonitoredFence.Core, 1, 0, 0, FALSE);
    ok_eq_hex(Status, STATUS_NOT_SUPPORTED);
    CpuNotification.Core.ObjectFlags = DXGK_CONTEXT_SYNC_OBJECT_SIGNAL_BY_KMD;
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationSignal2, &CpuNotification.Core, 1, 0, 0, FALSE);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}

static VOID
DxgkContextSyncTestWaitAtomicity(VOID)
{
    DXGK_CONTEXT_SYNC_TEST_OBJECT Mutex;
    DXGK_CONTEXT_SYNC_TEST_OBJECT Semaphore;
    DXGK_CONTEXT_SYNC_CORE_OBJECT Batch[2];
    NTSTATUS Status;

    DxgkContextSyncTestInitializeObject(&Mutex, D3DDDI_SYNCHRONIZATION_MUTEX, 1, 0);
    DxgkContextSyncTestInitializeObject(&Semaphore, D3DDDI_SEMAPHORE, 1, 3);
    Batch[0] = Mutex.Core;
    Batch[1] = Semaphore.Core;
    Status = DxgkContextSyncCoreExecuteWait(Batch, RTL_NUMBER_OF(Batch), 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Mutex.MutexOwned, 1);
    ok(Mutex.FenceValue == 0, "mutex state was not consumed\n");
    ok(Semaphore.SemaphoreCount == 0 && Semaphore.FenceValue == 0, "semaphore state was not consumed atomically\n");
    ok_eq_long(KeReadStateEvent(&Mutex.StateEvent), 0);
    ok_eq_long(KeReadStateEvent(&Semaphore.StateEvent), 0);

    Mutex.MutexOwned = 1;
    Mutex.FenceValue = 0;
    Semaphore.SemaphoreCount = 1;
    Semaphore.FenceValue = 1;
    Status = DxgkContextSyncCoreExecuteWait(Batch, RTL_NUMBER_OF(Batch), 0);
    ok_eq_hex(Status, STATUS_PENDING);
    ok(Semaphore.SemaphoreCount == 1 && Semaphore.FenceValue == 1, "pending batch partially consumed its semaphore\n");

    Batch[0] = Semaphore.Core;
    Batch[1] = Semaphore.Core;
    Status = DxgkContextSyncCoreExecuteWait(Batch, RTL_NUMBER_OF(Batch), 0);
    ok_eq_hex(Status, STATUS_PENDING);
    ok(Semaphore.SemaphoreCount == 1, "duplicate pending wait consumed a token\n");
    Semaphore.SemaphoreCount = 2;
    Semaphore.FenceValue = 2;
    Status = DxgkContextSyncCoreExecuteWait(Batch, RTL_NUMBER_OF(Batch), 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Semaphore.SemaphoreCount == 0, "duplicate semaphore wait did not consume both tokens\n");
}

static VOID
DxgkContextSyncTestSignalAtomicity(VOID)
{
    DXGK_CONTEXT_SYNC_TEST_OBJECT Mutex;
    DXGK_CONTEXT_SYNC_TEST_OBJECT Semaphore;
    DXGK_CONTEXT_SYNC_TEST_OBJECT Fence;
    DXGK_CONTEXT_SYNC_TEST_OBJECT CpuNotification;
    DXGK_CONTEXT_SYNC_CORE_OBJECT Batch[2];
    KEVENT EnqueueEvent;
    NTSTATUS Status;

    DxgkContextSyncTestInitializeObject(&Mutex, D3DDDI_SYNCHRONIZATION_MUTEX, 0, 0);
    DxgkContextSyncTestInitializeObject(&Semaphore, D3DDDI_SEMAPHORE, 2, 2);
    Batch[0] = Mutex.Core;
    Batch[1] = Semaphore.Core;
    Status = DxgkContextSyncCoreExecuteSignal(Batch, RTL_NUMBER_OF(Batch), 0, 0, NULL);
    ok_eq_hex(Status, STATUS_SEMAPHORE_LIMIT_EXCEEDED);
    ok_eq_long(Mutex.MutexOwned, 1);
    ok(Mutex.FenceValue == 0, "overflowing signal batch partially released mutex\n");
    Semaphore.SemaphoreCount = 1;
    Semaphore.FenceValue = 1;
    Status = DxgkContextSyncCoreExecuteSignal(Batch, RTL_NUMBER_OF(Batch), 0, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(Mutex.MutexOwned, 0);
    ok(Mutex.FenceValue == 1 && Semaphore.SemaphoreCount == 2, "successful signal did not publish all state\n");

    DxgkContextSyncTestInitializeObject(&Fence, D3DDDI_FENCE, 10, 0);
    Status = DxgkContextSyncCoreExecuteSignal(&Fence.Core, 1, 0, 5, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Fence.FenceValue == 10, "fence rewound without AllowFenceRewind\n");
    Status = DxgkContextSyncCoreExecuteSignal(&Fence.Core, 1, DXGK_CONTEXT_SYNC_ALLOW_FENCE_REWIND, 5, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Fence.FenceValue == 5, "AllowFenceRewind did not publish lower value\n");
    Status = DxgkContextSyncCoreExecuteSignal(&Fence.Core, 1, 0, 15, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Fence.FenceValue == 15, "monotonic fence signal did not advance\n");

    DxgkContextSyncTestInitializeObject(&CpuNotification, D3DDDI_CPU_NOTIFICATION, 0, 0);
    ok_eq_long(KeReadStateEvent(&CpuNotification.NotificationEvent), 0);
    Status = DxgkContextSyncCoreExecuteSignal(&CpuNotification.Core, 1, 0, 0, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(KeReadStateEvent(&CpuNotification.NotificationEvent), 1);
    KeInitializeEvent(&EnqueueEvent, NotificationEvent, FALSE);
    ok_eq_long(KeReadStateEvent(&EnqueueEvent), 0);
    Status = DxgkContextSyncCoreExecuteSignal(NULL, 0, DXGK_CONTEXT_SYNC_ENQUEUE_CPU_EVENT, 0, &EnqueueEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(KeReadStateEvent(&EnqueueEvent), 1);
}

static VOID
DxgkContextSyncTestDestroyAndRelease(VOID)
{
    DXGK_CONTEXT_SYNC_TEST_OBJECT Semaphore;
    DXGK_CONTEXT_SYNC_CORE_RETENTION Retention;
    DXGK_CONTEXT_SYNC_RELEASE_TEST ReleaseTest;
    ULONG References[3] = { 1, 2, 3 };
    NTSTATUS Status;

    DxgkContextSyncTestInitializeObject(&Semaphore, D3DDDI_SEMAPHORE, 1, 2);
    Semaphore.Destroying = 1;
    Status = DxgkContextSyncCoreValidate(DxgkContextSyncOperationLegacyWait, &Semaphore.Core, 1, 0, 0, FALSE);
    ok_eq_hex(Status, STATUS_DELETE_PENDING);
    ok(Semaphore.SemaphoreCount == 1, "destroying wait object was consumed\n");
    RtlZeroMemory(&ReleaseTest, sizeof(ReleaseTest));
    DxgkContextSyncCoreInitializeRetention(&Retention, DxgkContextSyncTestRelease, &ReleaseTest);
    Status = DxgkContextSyncCoreAddReference(&Retention, &References[0], DxgkContextSyncReferenceDevice);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkContextSyncCoreAddReference(&Retention, &References[1], DxgkContextSyncReferenceObject);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkContextSyncCoreAddReference(&Retention, &References[2], DxgkContextSyncReferenceEvent);
    ok_eq_hex(Status, STATUS_SUCCESS);
    DxgkContextSyncCoreRelease(&Retention);
    DxgkContextSyncCoreRelease(&Retention);
    ok_eq_long(ReleaseTest.TotalCount, 3);
    ok_eq_long(ReleaseTest.KindCounts[DxgkContextSyncReferenceDevice], 1);
    ok_eq_long(ReleaseTest.KindCounts[DxgkContextSyncReferenceObject], 1);
    ok_eq_long(ReleaseTest.KindCounts[DxgkContextSyncReferenceEvent], 1);
    Status = DxgkContextSyncCoreAddReference(&Retention, &References[0], DxgkContextSyncReferenceDevice);
    ok_eq_hex(Status, STATUS_DELETE_PENDING);
}

START_TEST(DxgkContextSync)
{
    DxgkContextSyncTestValidation();
    DxgkContextSyncTestWaitAtomicity();
    DxgkContextSyncTestSignalAtomicity();
    DxgkContextSyncTestDestroyAndRelease();
}
