/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Swap chain / present infrastructure
 * COPYRIGHT:   Copyright 2024-2026 ReactOS WDDM Team
 *
 * Overview
 * --------
 * Implements the per-VidPnSource present queue that sits between the
 * D3DKMTPresent user-mode entry point (dma.c) and the miniport DDI
 * present callbacks.
 *
 * Two present execution paths:
 *
 *   1. Display-only (DOD) adapters:
 *      Present = copy the shadow framebuffer to the GPU via
 *      DxgkDdiPresentDisplayOnly.  This is the same mechanism used by
 *      the existing periodic present timer in display.c, but driven
 *      on-demand by the present queue rather than (only) by a timer.
 *
 *   2. Full WDDM adapters:
 *      Present = call DxgkDdiPresent on the miniport with the
 *      appropriate blit/flip/colour-fill flags.  The miniport writes
 *      a DMA packet that the GPU scheduler submits to the hardware.
 *
 * VSync synchronisation:
 *   Each queue tracks a VBlankCount incremented by DxgkpNotifyVSync
 *   (called from the CRTC_VSYNC interrupt path).  Flip presents with
 *   FlipInterval > 0 wait until VBlankCount reaches the target before
 *   being dequeued.  Immediate presents bypass this check.
 *
 * x86/amd64 notes
 * ---------------
 * The queue spinlock is acquired at DISPATCH_LEVEL (via KeAcquireSpinLock)
 * because the VSync DPC path must safely increment VBlankCount.  On
 * x86/amd64, DISPATCH_LEVEL spin locks use CLI/STI for IRQL management,
 * which is architecturally standard and imposes no special constraints
 * beyond the usual "no paged access while holding a spinlock" rule.
 *
 * The 64-bit VBlankCount and NextPresentId fields use InterlockedIncrement64
 * for atomic updates.  On x86-32, InterlockedIncrement64 compiles to a
 * LOCK CMPXCHG8B loop which is slightly slower than the native 64-bit
 * LOCK INC on amd64, but both are correct.
 */

#include "dxgkrnl_private.h"
#include "vidmm.h"
#include "vidsch.h"
#include "present.h"
#include "present_queue_core.h"

#define DXGK_PRESENT_EXEC_LOG_LIMIT  32
#define DXGK_PRESENT_EXEC_SLOW_US    5000ULL
#define DXGK_PRESENT_TRACE_BURST     8
#define DXGK_PRESENT_TRACE_PERIOD    128

static volatile LONG g_DodPresentTraceCount = 0;
static volatile LONG g_SharedPrimaryPresentTraceCount = 0;
static volatile LONG g_PresentOpenTraceCount = 0;
static volatile LONG g_SharedPrimaryScanoutTraceCount = 0;

typedef struct _DXGKRNL_VSYNC_WORK
{
    WORK_QUEUE_ITEM WorkItem;
    PDXGKRNL_PRESENT_QUEUE Queue;
} DXGKRNL_VSYNC_WORK, *PDXGKRNL_VSYNC_WORK;

typedef struct _DXGKRNL_VBLANK_WAITER
{
    LIST_ENTRY Entry;
    KEVENT Event;
    LONG64 TargetVBlank;
    LONG64 ResetGeneration;
} DXGKRNL_VBLANK_WAITER, *PDXGKRNL_VBLANK_WAITER;

static VOID NTAPI DxgkpVSyncWorker(_In_ PVOID Context);

static BOOLEAN
DxgkpAcquirePresentQueues(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (InterlockedCompareExchange(&Adapter->PresentQueueStopping, 0, 0) != 0)
        return FALSE;

    if (InterlockedIncrement(&Adapter->PresentQueueActiveCalls) == 1)
        KeClearEvent(&Adapter->PresentQueueCallsDrainedEvent);

    if (InterlockedCompareExchange(&Adapter->PresentQueueStopping, 0, 0) == 0)
        return TRUE;

    if (InterlockedDecrement(&Adapter->PresentQueueActiveCalls) == 0)
        KeSetEvent(&Adapter->PresentQueueCallsDrainedEvent, IO_NO_INCREMENT, FALSE);
    return FALSE;
}

static VOID
DxgkpReleasePresentQueues(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (InterlockedDecrement(&Adapter->PresentQueueActiveCalls) == 0)
        KeSetEvent(&Adapter->PresentQueueCallsDrainedEvent, IO_NO_INCREMENT, FALSE);
}

static VOID
DxgkpWaitForPresentQueues(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    while (InterlockedCompareExchange(&Adapter->PresentQueueActiveCalls, 0, 0) != 0)
        KeWaitForSingleObject(&Adapter->PresentQueueCallsDrainedEvent, Executive, KernelMode, FALSE, NULL);
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
static NTSTATUS
DxgkpCreateDwmVBlankEvent(
    _Outptr_ PKEVENT *Event)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE EventHandle = NULL;
    PKEVENT EventObject = NULL;
    NTSTATUS Status;

    if (Event == NULL)
        return STATUS_INVALID_PARAMETER;
    *Event = NULL;

    InitializeObjectAttributes(&ObjectAttributes,
                               NULL,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwCreateEvent(&EventHandle,
                           EVENT_MODIFY_STATE | SYNCHRONIZE,
                           &ObjectAttributes,
                           SynchronizationEvent,
                           FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = ObReferenceObjectByHandle(EventHandle,
                                       EVENT_MODIFY_STATE | SYNCHRONIZE,
                                       *ExEventObjectType,
                                       KernelMode,
                                       (PVOID *)&EventObject,
                                       NULL);
    ZwClose(EventHandle);
    if (!NT_SUCCESS(Status))
        return Status;

    *Event = EventObject;
    return STATUS_SUCCESS;
}
#endif

static VOID
DxgkpSignalQueueVBlankWaiters(
    _In_ PDXGKRNL_PRESENT_QUEUE Queue,
    _In_ BOOLEAN Force)
{
    LONG64 VBlankCount = InterlockedCompareExchange64(&Queue->VBlankCount, 0, 0);
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Queue->VBlankWaitLock, &OldIrql);
    for (Entry = Queue->VBlankWaiterList.Flink; Entry != &Queue->VBlankWaiterList; Entry = Entry->Flink)
    {
        PDXGKRNL_VBLANK_WAITER Waiter = CONTAINING_RECORD(Entry, DXGKRNL_VBLANK_WAITER, Entry);

        if (Force || VBlankCount >= Waiter->TargetVBlank)
            KeSetEvent(&Waiter->Event, IO_NO_INCREMENT, FALSE);
    }
#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
    if (Queue->DwmVBlankEvent != NULL &&
        (Force ||
         (Queue->DwmVBlankTargetArmed &&
          DxgkPresentCoreRefreshTargetReached(
              (ULONG)VBlankCount,
              Queue->DwmVBlankTarget))))
    {
        Queue->DwmVBlankTargetArmed = FALSE;
        KeSetEvent(Queue->DwmVBlankEvent, IO_NO_INCREMENT, FALSE);
    }
#endif
    KeReleaseSpinLock(&Queue->VBlankWaitLock, OldIrql);
}

static VOID
DxgkpSignalVBlankWaiters(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PDXGKRNL_PRESENT_QUEUE Queues = (PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues;
    ULONG QueueCount = Adapter->PresentQueueCount;
    ULONG Index;

    if (Queues == NULL)
        return;
    for (Index = 0; Index < QueueCount; ++Index)
        DxgkpSignalQueueVBlankWaiters(&Queues[Index], TRUE);
}

/*
 * Frames presented on a source, and how many presents are still queued for it.
 * This is what D3DKMT_QUERYSTATISTICS_VIDPNSOURCE reports; the display-only
 * scan-out path accounts its own copies through DxgkPresentAccountFrame.
 */
NTSTATUS
DxgkPresentQueryVidPnSourceStats(
    _In_  PDXGKRNL_ADAPTER               Adapter,
    _In_  D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ PULONG                         Frame,
    _Out_ PULONG                         QueuedPresent)
{
    PDXGKRNL_PRESENT_QUEUE Queue;
    NTSTATUS Status = STATUS_SUCCESS;
    KIRQL OldIrql;

    if (Adapter == NULL || Frame == NULL || QueuedPresent == NULL)
        return STATUS_INVALID_PARAMETER;

    *Frame = 0;
    *QueuedPresent = 0;

    if (!DxgkpAcquirePresentQueues(Adapter))
        return STATUS_DEVICE_REMOVED;

    if (Adapter->PresentQueues == NULL || VidPnSourceId >= Adapter->PresentQueueCount)
    {
        Status = (Adapter->PresentQueues == NULL)
                     ? Adapter->PresentQueueInitializationStatus
                     : STATUS_INVALID_PARAMETER;
        DxgkpReleasePresentQueues(Adapter);
        return Status;
    }

    Queue = &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[VidPnSourceId];

    *Frame = (ULONG)InterlockedCompareExchange(&Queue->PresentedFrameCount, 0, 0);
    KeAcquireSpinLock(&Queue->QueueLock, &OldIrql);
    *QueuedPresent = Queue->Count;
    KeReleaseSpinLock(&Queue->QueueLock, OldIrql);

    DxgkpReleasePresentQueues(Adapter);
    return Status;
}

/* Account one presented frame on a source from outside the present queue (the
 * display-only path scans out directly, without queueing a present). */
VOID
DxgkPresentAccountFrame(
    _In_ PDXGKRNL_ADAPTER               Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId)
{
    PDXGKRNL_PRESENT_QUEUE Queues;

    if (Adapter == NULL)
        return;

    if (!DxgkpAcquirePresentQueues(Adapter))
        return;

    Queues = (PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues;
    if (Queues != NULL && VidPnSourceId < Adapter->PresentQueueCount)
        InterlockedIncrement(&Queues[VidPnSourceId].PresentedFrameCount);

    DxgkpReleasePresentQueues(Adapter);
}

BOOLEAN
DxgkPresentTryBeginStop(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || InterlockedCompareExchange(&Adapter->PresentQueueStopping, 1, 0) != 0)
        return FALSE;
    KeMemoryBarrier();
    DxgkpSignalVBlankWaiters(Adapter);
    return TRUE;
}

VOID
DxgkPresentBeginStop(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    (VOID)DxgkPresentTryBeginStop(Adapter);
}

VOID
DxgkPresentResume(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return;
    InterlockedExchange(&Adapter->VBlankResetActive, 0);
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->PresentQueueStopping, 0);
}

VOID
DxgkPresentBeginReset(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return;
    InterlockedExchange(&Adapter->VBlankResetActive, 1);
    InterlockedIncrement64(&Adapter->VBlankResetGeneration);
    KeMemoryBarrier();
    if (!DxgkpAcquirePresentQueues(Adapter))
        return;
    DxgkpSignalVBlankWaiters(Adapter);
    DxgkpReleasePresentQueues(Adapter);
}

VOID
DxgkPresentCompleteReset(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL)
        return;
    KeMemoryBarrier();
    InterlockedExchange(&Adapter->VBlankResetActive, 0);
}

VOID
DxgkPresentNotifyDeviceRemoved(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    if (Adapter == NULL || !DxgkpAcquirePresentQueues(Adapter))
        return;
    DxgkpSignalVBlankWaiters(Adapter);
    DxgkpReleasePresentQueues(Adapter);
}

NTSTATUS
DxgkpWaitForVerticalBlank(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ UINT NumObjects,
    _In_reads_opt_(NumObjects) CONST D3DKMT_PTR_TYPE *ObjectHandleArray)
{
    PVOID WaitObjects[D3DKMT_MAX_WAITFORVERTICALBLANK_OBJECTS + 1];
    PVOID ReferencedObjects[D3DKMT_MAX_WAITFORVERTICALBLANK_OBJECTS];
    KWAIT_BLOCK WaitBlocks[D3DKMT_MAX_WAITFORVERTICALBLANK_OBJECTS + 1];
    PDXGKRNL_PRESENT_QUEUE Queue;
    DXGKRNL_VBLANK_WAITER Waiter;
    BOOLEAN WaiterLinked = FALSE;
    KIRQL OldIrql;
    ULONG Index;
    NTSTATUS Status;

    PAGED_CODE();
    if (Adapter == NULL || NumObjects > D3DKMT_MAX_WAITFORVERTICALBLANK_OBJECTS || (NumObjects != 0 && ObjectHandleArray == NULL))
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(ReferencedObjects, sizeof(ReferencedObjects));
    if (!DxgkpAcquirePresentQueues(Adapter))
        return STATUS_DEVICE_REMOVED;
    if (Adapter->PresentQueues == NULL || VidPnSourceId >= Adapter->PresentQueueCount)
    {
        Status = Adapter->PresentQueues == NULL ? Adapter->PresentQueueInitializationStatus : STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    Queue = &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[VidPnSourceId];
    for (Index = 0; Index < NumObjects; ++Index)
    {
        Status = ObReferenceObjectByHandle((HANDLE)(ULONG_PTR)ObjectHandleArray[Index], SYNCHRONIZE, *ExEventObjectType, UserMode, &ReferencedObjects[Index], NULL);
        if (!NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Cleanup;
        }
        WaitObjects[Index + 1] = ReferencedObjects[Index];
    }
    RtlZeroMemory(&Waiter, sizeof(Waiter));
    KeInitializeEvent(&Waiter.Event, NotificationEvent, FALSE);
    WaitObjects[0] = &Waiter.Event;
    KeAcquireSpinLock(&Queue->VBlankWaitLock, &OldIrql);
    if (InterlockedCompareExchange(&Adapter->PresentQueueStopping, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->VBlankResetActive, 0, 0) != 0)
    {
        KeReleaseSpinLock(&Queue->VBlankWaitLock, OldIrql);
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    if (Device != NULL && InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        KeReleaseSpinLock(&Queue->VBlankWaitLock, OldIrql);
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }
    Waiter.ResetGeneration = InterlockedCompareExchange64(&Adapter->VBlankResetGeneration, 0, 0);
    Waiter.TargetVBlank = InterlockedCompareExchange64(&Queue->VBlankCount, 0, 0) + 1;
    InsertTailList(&Queue->VBlankWaiterList, &Waiter.Entry);
    WaiterLinked = TRUE;
    KeReleaseSpinLock(&Queue->VBlankWaitLock, OldIrql);
    for (;;)
    {
        Status = KeWaitForMultipleObjects(NumObjects + 1, WaitObjects, WaitAny, UserRequest, KernelMode, FALSE, NULL, WaitBlocks);
        if (InterlockedCompareExchange(&Adapter->PresentQueueStopping, 0, 0) != 0 || InterlockedCompareExchange(&Adapter->VBlankResetActive, 0, 0) != 0 || InterlockedCompareExchange64(&Adapter->VBlankResetGeneration, 0, 0) != Waiter.ResetGeneration || (Device != NULL && InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE))
        {
            Status = STATUS_DEVICE_REMOVED;
            break;
        }
        if (Status >= STATUS_WAIT_1 && Status <= STATUS_WAIT_0 + NumObjects)
            break;
        if (Status != STATUS_WAIT_0)
            break;
        KeAcquireSpinLock(&Queue->VBlankWaitLock, &OldIrql);
        if (InterlockedCompareExchange64(&Queue->VBlankCount, 0, 0) >= Waiter.TargetVBlank)
        {
            KeReleaseSpinLock(&Queue->VBlankWaitLock, OldIrql);
            break;
        }
        KeClearEvent(&Waiter.Event);
        KeReleaseSpinLock(&Queue->VBlankWaitLock, OldIrql);
    }

Cleanup:
    if (WaiterLinked)
    {
        KeAcquireSpinLock(&Queue->VBlankWaitLock, &OldIrql);
        RemoveEntryList(&Waiter.Entry);
        KeReleaseSpinLock(&Queue->VBlankWaitLock, OldIrql);
    }
    for (Index = 0; Index < NumObjects; ++Index)
    {
        if (ReferencedObjects[Index] != NULL)
            ObDereferenceObject(ReferencedObjects[Index]);
    }
    DxgkpReleasePresentQueues(Adapter);
    return Status;
}

#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
NTSTATUS
DxgkPresentOpenDwmVBlankEvent(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _Out_ PHANDLE EventHandle)
{
    PDXGKRNL_PRESENT_QUEUE Queue;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL || EventHandle == NULL)
        return STATUS_INVALID_PARAMETER;
    *EventHandle = NULL;

    if (!DxgkpAcquirePresentQueues(Adapter))
        return STATUS_DEVICE_REMOVED;
    if (Adapter->PresentQueues == NULL ||
        VidPnSourceId >= Adapter->PresentQueueCount)
    {
        Status = Adapter->PresentQueues == NULL
                     ? Adapter->PresentQueueInitializationStatus
                     : STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    if (InterlockedCompareExchange(&Adapter->VBlankResetActive, 0, 0) != 0)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto Cleanup;
    }

    Queue = &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[VidPnSourceId];
    if (Queue->DwmVBlankEvent == NULL)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Cleanup;
    }

    /*
     * HandleAttributes, rather than AccessMode, selects the handle table.
     * With attributes zero this creates a normal wait-only handle in the
     * requesting process; KernelMode authorizes the kernel-owned unnamed
     * event without letting user mode signal it.
     */
    Status = ObOpenObjectByPointer(Queue->DwmVBlankEvent,
                                   0,
                                   NULL,
                                   SYNCHRONIZE,
                                   *ExEventObjectType,
                                   KernelMode,
                                   EventHandle);

Cleanup:
    DxgkpReleasePresentQueues(Adapter);
    return Status;
}

NTSTATUS
DxgkPresentSetSyncRefreshCountWaitTarget(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ ULONG TargetSyncRefreshCount)
{
    PDXGKRNL_PRESENT_QUEUE Queue;
    LONG64 VBlankCount;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    if (!DxgkpAcquirePresentQueues(Adapter))
        return STATUS_DEVICE_REMOVED;
    if (Adapter->PresentQueues == NULL ||
        VidPnSourceId >= Adapter->PresentQueueCount)
    {
        Status = Adapter->PresentQueues == NULL
                     ? Adapter->PresentQueueInitializationStatus
                     : STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }

    Queue = &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[VidPnSourceId];
    KeAcquireSpinLock(&Queue->VBlankWaitLock, &OldIrql);
    if (InterlockedCompareExchange(&Adapter->PresentQueueStopping, 0, 0) != 0 ||
        InterlockedCompareExchange(&Adapter->VBlankResetActive, 0, 0) != 0)
    {
        Status = STATUS_DEVICE_REMOVED;
    }
    else if (Queue->DwmVBlankEvent == NULL)
    {
        Status = STATUS_DEVICE_NOT_READY;
    }
    else
    {
        VBlankCount =
            InterlockedCompareExchange64(&Queue->VBlankCount, 0, 0);
        KeClearEvent(Queue->DwmVBlankEvent);
        Queue->DwmVBlankTarget = TargetSyncRefreshCount;
        Queue->DwmVBlankTargetArmed = TRUE;
        if (DxgkPresentCoreRefreshTargetShouldSignalImmediately(
                (ULONG)VBlankCount,
                TargetSyncRefreshCount))
        {
            Queue->DwmVBlankTargetArmed = FALSE;
            KeSetEvent(Queue->DwmVBlankEvent, IO_NO_INCREMENT, FALSE);
        }
        Status = STATUS_SUCCESS;
    }
    KeReleaseSpinLock(&Queue->VBlankWaitLock, OldIrql);

Cleanup:
    DxgkpReleasePresentQueues(Adapter);
    return Status;
}
#endif

VOID
DxgkpReleasePresentEntry(
    _Inout_ PDXGKRNL_PRESENT_ENTRY Entry)
{
    if (Entry == NULL)
        return;
    if (Entry->PresentLimitReservationOwned)
    {
        ASSERT(Entry->Device != NULL);
        if (Entry->Device != NULL)
            DxgkPresentLimitCoreRelease(&Entry->Device->PresentLimit);
        Entry->PresentLimitReservationOwned = FALSE;
    }
    if (Entry->DestinationOpenBindingReference != NULL)
        DxgkVidMmDereferenceLogicalAllocation(Entry->DestinationOpenBindingReference);
    if (Entry->SourceOpenBindingReference != NULL)
        DxgkVidMmDereferenceLogicalAllocation(Entry->SourceOpenBindingReference);
    if (Entry->DestinationAllocation != NULL)
        DxgkVidMmDereferenceAllocation(Entry->DestinationAllocation);
    if (Entry->SourceAllocation != NULL)
        DxgkVidMmDereferenceAllocation(Entry->SourceAllocation);
    if (Entry->Context != NULL)
        DxgkDereferenceContext(Entry->Context);
    DxgkDeviceWorkDestroy(Entry->DeviceWork);
    Entry->DeviceWork = NULL;
    if (Entry->Device != NULL)
        DxgkDereferenceDevice(Entry->Device);
    if (Entry->DstSubRects != NULL)
        ExFreePoolWithTag(Entry->DstSubRects, TAG_DXGK_PRESENT);
    Entry->DestinationAllocation = NULL;
    Entry->SourceAllocation = NULL;
    Entry->DestinationOpenBindingReference = NULL;
    Entry->SourceOpenBindingReference = NULL;
    Entry->DestinationOpenBindingHandle = NULL;
    Entry->SourceOpenBindingHandle = NULL;
    Entry->Context = NULL;
    Entry->Device = NULL;
    Entry->DstSubRects = NULL;
    Entry->DstSubRectCount = 0;
    DxgkpReleaseSharedSurfaceSnapshot(&Entry->SharedSurface);
    Entry->SourceIsSharedPrimary = FALSE;
    Entry->SourceIsSharedShadow = FALSE;
    Entry->DestinationIsSharedPrimary = FALSE;
    Entry->DestinationIsSharedShadow = FALSE;
}

NTSTATUS DxgkPresentSetQueuedLimit(_In_ PDXGKRNL_DEVICE Device, _In_ ULONG RequestedLimit)
{
    NTSTATUS Status;

    if (Device == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        return STATUS_DEVICE_REMOVED;
    if (!DxgkpAcquirePresentQueues(Device->Adapter))
        return STATUS_DEVICE_REMOVED;
    Status = DxgkPresentLimitCoreSet(&Device->PresentLimit, RequestedLimit, DXGKRNL_DEFAULT_QUEUED_PRESENT_LIMIT, DXGKRNL_PRESENT_QUEUE_DEPTH);
    DxgkpReleasePresentQueues(Device->Adapter);
    return Status;
}

NTSTATUS DxgkPresentGetQueuedLimit(_In_ PDXGKRNL_DEVICE Device, _Out_ PUINT QueuedPresentLimit)
{
    if (Device == NULL || QueuedPresentLimit == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        return STATUS_DEVICE_REMOVED;
    if (!DxgkpAcquirePresentQueues(Device->Adapter))
        return STATUS_DEVICE_REMOVED;
    *QueuedPresentLimit = DxgkPresentLimitCoreGetLimit(&Device->PresentLimit);
    DxgkpReleasePresentQueues(Device->Adapter);
    return STATUS_SUCCESS;
}

NTSTATUS DxgkPresentGetPendingFlipLimit(_In_ PDXGKRNL_DEVICE Device, _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _Out_ PUINT QueuedPendingFlipLimit)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;

    if (Device == NULL || QueuedPendingFlipLimit == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        return STATUS_DEVICE_REMOVED;
    Adapter = Device->Adapter;
    if (Adapter == NULL || !DxgkpAcquirePresentQueues(Adapter))
        return STATUS_DEVICE_REMOVED;
    if (Adapter->State != DxgkAdapterStateStarted || Adapter->PresentQueues == NULL || VidPnSourceId >= Adapter->PresentQueueCount)
        Status = Adapter->State != DxgkAdapterStateStarted ? STATUS_DEVICE_REMOVED : STATUS_INVALID_PARAMETER;
    else
    {
        *QueuedPendingFlipLimit = DXGKRNL_DEFAULT_PENDING_FLIP_LIMIT;
        Status = STATUS_SUCCESS;
    }
    DxgkpReleasePresentQueues(Adapter);
    return Status;
}

NTSTATUS DxgkPresentGetQueueLimitState(_In_ PDXGKRNL_DEVICE Device, _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId, _Out_ PBOOLEAN LimitReached)
{
    PDXGKRNL_ADAPTER Adapter;
    NTSTATUS Status;

    if (Device == NULL || LimitReached == NULL)
        return STATUS_INVALID_PARAMETER;
    if (InterlockedCompareExchange(&Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
        return STATUS_DEVICE_REMOVED;
    Adapter = Device->Adapter;
    if (Adapter == NULL || !DxgkpAcquirePresentQueues(Adapter))
        return STATUS_DEVICE_REMOVED;
    if (Adapter->State != DxgkAdapterStateStarted || Adapter->PresentQueues == NULL || VidPnSourceId >= Adapter->PresentQueueCount)
        Status = Adapter->State != DxgkAdapterStateStarted ? STATUS_DEVICE_REMOVED : STATUS_INVALID_PARAMETER;
    else
    {
        *LimitReached = DxgkPresentLimitCoreIsReached(&Device->PresentLimit);
        Status = STATUS_SUCCESS;
    }
    DxgkpReleasePresentQueues(Adapter);
    return Status;
}

NTSTATUS
DxgkpAcquireSharedSurfaceSnapshot(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _Out_ PDXGKRNL_SHARED_SURFACE_SNAPSHOT Snapshot)
{
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (Adapter == NULL || Snapshot == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    (VOID)KeWaitForSingleObject(&Adapter->SharedPrimaryMutex, Executive, KernelMode, FALSE, NULL);
    if (!ExAcquireRundownProtection(&Adapter->SharedSurfaceRundown))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }

    Snapshot->RundownHeld = TRUE;
    Snapshot->Adapter = Adapter;
    Snapshot->Generation = Adapter->SharedSurfaceGeneration;
    Snapshot->PrimaryHandle = Adapter->SharedPrimaryAllocationHandle;
    Snapshot->ShadowHandle = Adapter->SharedShadowAllocationHandle;
    Snapshot->VidPnSourceId = Adapter->SharedPrimaryVidPnSourceId;
    Snapshot->PrimaryWidth = Adapter->SharedPrimaryWidth;
    Snapshot->PrimaryHeight = Adapter->SharedPrimaryHeight;
    Snapshot->PrimaryFormat = Adapter->SharedPrimaryFormat;
    Snapshot->ShadowWidth = Adapter->SharedShadowWidth;
    Snapshot->ShadowHeight = Adapter->SharedShadowHeight;
    Snapshot->ShadowPitch = Adapter->SharedShadowPitch;
    Snapshot->ShadowFormat = Adapter->SharedShadowFormat;
    Snapshot->ShadowFb = Adapter->ShadowFb;
    Snapshot->ShadowFbPitch = Adapter->ShadowFbPitch;
    Snapshot->ShadowFbSize = Adapter->ShadowFbSize;
    Snapshot->ShadowFbPoolOwned = Adapter->ShadowFbPoolOwned;
    Snapshot->CommittedWidth = Adapter->CommittedWidth;
    Snapshot->CommittedHeight = Adapter->CommittedHeight;
    Snapshot->PostDisplayVirtualAddress = Adapter->PostDisplayVirtualAddress;
    Snapshot->PostDisplayMappingSize = Adapter->PostDisplayMappingSize;
    Snapshot->PostDisplayPitch = Adapter->PostDisplayPitch;
    Snapshot->PostDisplayHeight = Adapter->PostDisplayHeight;
    Snapshot->VidPnCommitted = Adapter->VidPnCommitted;

    if (Snapshot->PrimaryHandle != NULL)
    {
        Status = DxgkVidMmReferenceAllocation(Snapshot->PrimaryHandle, Adapter, NULL, &Snapshot->PrimaryAllocation);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    if (Snapshot->ShadowHandle != NULL)
    {
        Status = DxgkVidMmReferenceAllocation(Snapshot->ShadowHandle, Adapter, NULL, &Snapshot->ShadowAllocation);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

Cleanup:
    KeReleaseMutex(&Adapter->SharedPrimaryMutex, FALSE);
    if (!NT_SUCCESS(Status))
        DxgkpReleaseSharedSurfaceSnapshot(Snapshot);
    return Status;
}

VOID
DxgkpReleaseSharedSurfaceSnapshot(
    _Inout_ PDXGKRNL_SHARED_SURFACE_SNAPSHOT Snapshot)
{
    PDXGKRNL_ADAPTER Adapter;

    if (Snapshot == NULL)
        return;
    Adapter = Snapshot->Adapter;
    if (Snapshot->ShadowAllocation != NULL)
        DxgkVidMmDereferenceAllocation(Snapshot->ShadowAllocation);
    if (Snapshot->PrimaryAllocation != NULL)
        DxgkVidMmDereferenceAllocation(Snapshot->PrimaryAllocation);
    if (Snapshot->RundownHeld)
    {
        ASSERT(Adapter != NULL);
        if (Adapter != NULL)
            ExReleaseRundownProtection(&Adapter->SharedSurfaceRundown);
    }
    Snapshot->Adapter = NULL;
    Snapshot->PrimaryAllocation = NULL;
    Snapshot->ShadowAllocation = NULL;
    Snapshot->RundownHeld = FALSE;
}

VOID
DxgkpBeginSharedSurfaceMutationLocked(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();

    ASSERT(Adapter != NULL);
    if (Adapter == NULL)
        return;
    Adapter->SharedSurfaceMutationDepth++;
    if (Adapter->SharedSurfaceMutationDepth == 1)
        ExWaitForRundownProtectionRelease(&Adapter->SharedSurfaceRundown);
}

VOID
DxgkpEndSharedSurfaceMutationLocked(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    PAGED_CODE();

    ASSERT(Adapter != NULL);
    ASSERT(Adapter == NULL || Adapter->SharedSurfaceMutationDepth != 0);
    if (Adapter == NULL || Adapter->SharedSurfaceMutationDepth == 0)
        return;
    Adapter->SharedSurfaceMutationDepth--;
    if (Adapter->SharedSurfaceMutationDepth != 0)
        return;
    Adapter->SharedSurfaceGeneration++;
    InterlockedExchange(&Adapter->SharedSurfaceAvailable, Adapter->ShadowFb != NULL && Adapter->ShadowFbPitch != 0 ? 1 : 0);
    ExReInitializeRundownProtection(&Adapter->SharedSurfaceRundown);
}

FORCEINLINE ULONGLONG
DxgkpPresentTraceNow100ns(VOID)
{
    return KeQueryInterruptTime();
}

FORCEINLINE ULONGLONG
DxgkpPresentTraceElapsedUs(
    _In_ ULONGLONG Start100ns)
{
    ULONGLONG End100ns = KeQueryInterruptTime();

    if (End100ns <= Start100ns)
        return 0;

    return (End100ns - Start100ns) / 10ULL;
}

FORCEINLINE BOOLEAN
DxgkpShouldTraceOrdinal(
    _In_ LONG Ordinal)
{
    return (Ordinal <= DXGK_PRESENT_TRACE_BURST ||
            ((Ordinal % DXGK_PRESENT_TRACE_PERIOD) == 0));
}

static ULONG
DxgkpSurfaceCopyPitch(
    _In_opt_ PDXGKVMM_ALLOCATION Allocation,
    _In_ ULONG Width,
    _In_ ULONG Height,
    _In_ ULONG DefaultPitch)
{
    SIZE_T CandidatePitch;

    if (DefaultPitch != 0)
        return DefaultPitch;

    if (Allocation != NULL &&
        Height != 0 &&
        Allocation->Size >= (SIZE_T)Width * sizeof(ULONG) &&
        (Allocation->Size % Height) == 0)
    {
        CandidatePitch = Allocation->Size / Height;
        if (CandidatePitch >= (SIZE_T)Width * sizeof(ULONG) &&
            CandidatePitch <= MAXULONG)
        {
            return (ULONG)CandidatePitch;
        }
    }

    return Width * sizeof(ULONG);
}

static NTSTATUS
DxgkpCopyShadowToSharedPrimary(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION PrimaryAllocation,
    _In_ const DXGKRNL_SHARED_SURFACE_SNAPSHOT *SharedSurface)
{
    PBYTE DestinationVa = NULL;
    PBYTE SourceVa;
    ULONG Width;
    ULONG Height;
    ULONG SourcePitch;
    ULONG DestinationPitch;
    ULONG RowBytes;
    ULONG Row;
    NTSTATUS Status;

    if (Adapter == NULL || PrimaryAllocation == NULL || SharedSurface == NULL)
        return STATUS_INVALID_PARAMETER;

    if (SharedSurface->ShadowFb == NULL || SharedSurface->ShadowFbPitch == 0)
        return STATUS_INVALID_PARAMETER;

    Width = SharedSurface->PrimaryWidth;
    Height = SharedSurface->PrimaryHeight;

    if (SharedSurface->ShadowWidth != 0 && SharedSurface->ShadowWidth < Width)
        Width = SharedSurface->ShadowWidth;
    if (SharedSurface->ShadowHeight != 0 && SharedSurface->ShadowHeight < Height)
        Height = SharedSurface->ShadowHeight;

    if (Width == 0 || Height == 0 || Width > (MAXULONG / sizeof(ULONG)))
        return STATUS_INVALID_PARAMETER;

    SourcePitch = SharedSurface->ShadowFbPitch;
    RowBytes = Width * sizeof(ULONG);
    if (SourcePitch < RowBytes || SharedSurface->ShadowFbSize < ((SIZE_T)(Height - 1) * SourcePitch) + RowBytes)
        return STATUS_INVALID_PARAMETER;

    Status = DxgkVidMmMapAllocationCpu(PrimaryAllocation, (PVOID *)&DestinationVa);
    if (!NT_SUCCESS(Status))
        return Status;

    DestinationPitch = DxgkpSurfaceCopyPitch(PrimaryAllocation, SharedSurface->PrimaryWidth, SharedSurface->PrimaryHeight, 0);
    if (DestinationPitch < RowBytes || PrimaryAllocation->Size < ((SIZE_T)(Height - 1) * DestinationPitch) + RowBytes)
        return STATUS_INVALID_PARAMETER;

    SourceVa = (PBYTE)SharedSurface->ShadowFb;
    for (Row = 0; Row < Height; ++Row)
        RtlCopyMemory(DestinationVa + ((SIZE_T)Row * DestinationPitch), SourceVa + ((SIZE_T)Row * SourcePitch), RowBytes);

    return STATUS_SUCCESS;
}

/*
 * Program the base-plane flip through the multi-plane overlay DDI when the
 * miniport implements it (documented Windows behavior: MPO-capable drivers
 * receive flips as one-plane MPO configurations).  Returns STATUS_NOT_-
 * SUPPORTED to let the caller fall back to the legacy SetVidPnSourceAddress.
 */
static NTSTATUS
DxgkpProgramScanoutViaMpo(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ LARGE_INTEGER PrimaryAddress)
{
    DXGKARG_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY MpoArgs;
    DXGK_MULTIPLANE_OVERLAY_PLANE Plane;
    PDXGKDDI_SETVIDPNSOURCEADDRESSWITHMULTIPLANEOVERLAY PfnSetMpo;
    NTSTATUS Status;

    PfnSetMpo = DXGK_CB_FULL(Adapter,
                             DxgkDdiSetVidPnSourceAddressWithMultiPlaneOverlay);
    if (PfnSetMpo == NULL ||
        Adapter->CommittedWidth == 0 ||
        Adapter->CommittedHeight == 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&Plane, sizeof(Plane));
    Plane.LayerIndex = 0;
    Plane.Enabled = TRUE;
    Plane.AllocationSegment = Allocation->SegmentId;
    Plane.AllocationAddress.QuadPart = PrimaryAddress.QuadPart;
    Plane.hAllocation = Allocation->MiniportHandle;
    Plane.PlaneAttributes.SrcRect.right = (LONG)Adapter->CommittedWidth;
    Plane.PlaneAttributes.SrcRect.bottom = (LONG)Adapter->CommittedHeight;
    Plane.PlaneAttributes.DstRect = Plane.PlaneAttributes.SrcRect;
    Plane.PlaneAttributes.ClipRect = Plane.PlaneAttributes.SrcRect;

    RtlZeroMemory(&MpoArgs, sizeof(MpoArgs));
    MpoArgs.VidPnSourceId = VidPnSourceId;
    MpoArgs.PlaneCount = 1;
    MpoArgs.pPlanes = &Plane;
    MpoArgs.Flags.FlipImmediate = 1;

    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;
    _SEH2_TRY
    {
        Status = PfnSetMpo(Adapter->MiniportDeviceContext, &MpoArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

    return Status;
}

static NTSTATUS
DxgkpProgramSharedPrimaryScanout(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId,
    _In_ D3DKMT_HANDLE AllocationHandle,
    _In_ ULONG64 PresentId)
{
    PDXGKARG_SETVIDPNSOURCEADDRESS SetSourceAddress = NULL;
    LARGE_INTEGER PrimaryAddress;
    LONG TraceSeq;
    NTSTATUS Status;
    BOOLEAN KmdTransaction;

    if (Adapter == NULL ||
        Allocation == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver ||
        DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress) == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DELETE_PENDING;
    KmdTransaction = TRUE;

    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpProgramSharedPrimaryScanout: aperture map failed "
                     "0x%08lX for hAllocation=0x%X\n",
                     Status,
                     AllocationHandle);
        goto Cleanup;
    }

    PrimaryAddress = DxgkVidMmGetAllocationPrimaryAddress(Allocation);

    /* MPO-capable miniports get the flip as a one-plane configuration. */
    Status = DxgkpProgramScanoutViaMpo(Adapter, Allocation, VidPnSourceId, PrimaryAddress);
    if (Status == STATUS_DELETE_PENDING)
        goto Cleanup;
    if (NT_SUCCESS(Status))
    {
        if (DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility) != NULL)
        {
            DXGKARG_SETVIDPNSOURCEVISIBILITY MpoVisibility;

            RtlZeroMemory(&MpoVisibility, sizeof(MpoVisibility));
            MpoVisibility.VidPnSourceId = VidPnSourceId;
            MpoVisibility.Visible = TRUE;

            if (!DxgkAcquireKmdCall(Adapter))
            {
                Status = STATUS_DELETE_PENDING;
                goto Cleanup;
            }
            Status = DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility)(Adapter->MiniportDeviceContext, &MpoVisibility);
            DxgkReleaseKmdCall(Adapter);
        }
        goto Cleanup;
    }

    SetSourceAddress = ExAllocatePoolWithTag(NonPagedPool, sizeof(*SetSourceAddress), TAG_DXGK_PRESENT);
    if (SetSourceAddress == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    RtlZeroMemory(SetSourceAddress, sizeof(*SetSourceAddress));
    SetSourceAddress->VidPnSourceId = VidPnSourceId;
    SetSourceAddress->hAllocation = Allocation->MiniportHandle;
    SetSourceAddress->PrimaryAddress = PrimaryAddress;
    SetSourceAddress->PrimarySegment = Allocation->SegmentId;
    SetSourceAddress->Flags.FlipImmediate = 1;

    TraceSeq = InterlockedIncrement(&g_SharedPrimaryScanoutTraceCount);
    if (DxgkpShouldTraceOrdinal(TraceSeq))
    {
        DXGKRNL_TRACE("DxgkpProgramSharedPrimaryScanout: seq=%ld PresentId=%llu "
                      "VidPnSrc=%u alloc=0x%X miniport=%p seg=%u addr=0x%I64x\n",
                      TraceSeq,
                      PresentId,
                      VidPnSourceId,
                      AllocationHandle,
                      Allocation->MiniportHandle,
                      Allocation->SegmentId,
                      PrimaryAddress.QuadPart);
    }

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto Cleanup;
    }
    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress)(Adapter->MiniportDeviceContext, SetSourceAddress);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        DXGKRNL_ERR("DxgkpProgramSharedPrimaryScanout: "
                    "DxgkDdiSetVidPnSourceAddress FAULTED 0x%08lX\n",
                        Status);
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

    if (NT_SUCCESS(Status) &&
        DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility) != NULL)
    {
        DXGKARG_SETVIDPNSOURCEVISIBILITY Visibility;

        RtlZeroMemory(&Visibility, sizeof(Visibility));
        Visibility.VidPnSourceId = VidPnSourceId;
        Visibility.Visible = TRUE;

        if (!DxgkAcquireKmdCall(Adapter))
            Status = STATUS_DELETE_PENDING;
        else
        {
            DXGK_CB(Adapter, DxgkDdiSetVidPnSourceVisibility)(Adapter->MiniportDeviceContext, &Visibility);
            DxgkReleaseKmdCall(Adapter);
        }
    }

Cleanup:
    if (SetSourceAddress != NULL)
        ExFreePoolWithTag(SetSourceAddress, TAG_DXGK_PRESENT);
    if (KmdTransaction)
        DxgkEndKmdTransaction(Adapter);
    return Status;
}

static NTSTATUS
DxgkpRefreshSharedPrimaryScanout(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_PRESENT_ENTRY Entry,
    _Out_ PBOOLEAN Handled)
{
    PDXGKVMM_ALLOCATION Allocation;
    NTSTATUS Status;

    if (Handled == NULL)
        return STATUS_INVALID_PARAMETER;

    *Handled = FALSE;

    if (Adapter == NULL ||
        Entry == NULL ||
        Adapter->MiniportContext == NULL ||
        Adapter->MiniportContext->IsDisplayOnlyDriver ||
        DXGK_CB_FULL(Adapter, DxgkDdiSetVidPnSourceAddress) == NULL ||
        Entry->Type != DxgkPresentTypeBlt ||
        Entry->hSource == 0 ||
        Entry->SourceAllocation != Entry->DestinationAllocation ||
        !Entry->SourceIsSharedPrimary ||
        !Entry->DestinationIsSharedPrimary)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (!DxgkPresentCoreIsFullDestinationRegion(
             &Entry->DstRect,
             Entry->DstSubRects,
             Entry->DstSubRectCount))
    {
        *Handled = TRUE;
        return STATUS_NOT_SUPPORTED;
    }

    Allocation = Entry->SourceAllocation;
    if (Allocation == NULL || Allocation->Adapter != Adapter)
        return STATUS_INVALID_HANDLE;
    *Handled = TRUE;

    Status = DxgkVidMmEnsureAllocationApertureMapped(Allocation);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpRefreshSharedPrimaryScanout: aperture map failed "
                     "0x%08lX for hAllocation=0x%X\n",
                     Status,
                     Entry->hSource);
        return Status;
    }

    Status = DxgkpCopyShadowToSharedPrimary(Adapter, Allocation, &Entry->SharedSurface);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpRefreshSharedPrimaryScanout: shadow copy failed "
                     "0x%08lX for hAllocation=0x%X\n",
                     Status,
                     Entry->hSource);
        return Status;
    }

    Status = DxgkpProgramSharedPrimaryScanout(Adapter, Allocation, Entry->SharedSurface.VidPnSourceId, Entry->hSource, Entry->PresentId);

    return Status;
}

/* ========================================================================
 * DxgkPresentInit
 *
 * Allocates and initialises the present queue array for the adapter.
 * One DXGKRNL_PRESENT_QUEUE per VidPn source.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
DxgkPresentInit(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG i;
    ULONG NumSources;
    PDXGKRNL_PRESENT_QUEUE Queues;
#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
    NTSTATUS Status;
#endif

    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    NumSources = Adapter->NumberOfVideoPresentSources;
    if (NumSources == 0)
        NumSources = 1; /* At least one source for the primary display. */

    DXGKRNL_TRACE("DxgkPresentInit: allocating %lu present queue(s)\n",
                  NumSources);

    Queues = (PDXGKRNL_PRESENT_QUEUE)ExAllocatePoolWithTag(
                 NonPagedPool,
                 NumSources * sizeof(DXGKRNL_PRESENT_QUEUE),
                 TAG_DXGK_PRESENT);

    if (Queues == NULL)
    {
        DXGKRNL_ERR("DxgkPresentInit: pool alloc failed (%lu x %Iu bytes)\n",
                    NumSources, sizeof(DXGKRNL_PRESENT_QUEUE));
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Queues, NumSources * sizeof(DXGKRNL_PRESENT_QUEUE));

    for (i = 0; i < NumSources; i++)
    {
        Queues[i].VidPnSourceId = i;
        Queues[i].Head          = 0;
        Queues[i].Tail          = 0;
        Queues[i].Count         = 0;
        Queues[i].NextPresentId = 0; /* InterlockedIncrement returns ID 1 first. */
        Queues[i].PresentedFrameCount = 0;
        Queues[i].VBlankCount   = 0;
        Queues[i].LastPresentVBlank = 0;
        Queues[i].Adapter       = Adapter;
        Queues[i].VSyncWorkQueued = 0;
        Queues[i].PendingVBlanks = 0;
        KeInitializeSpinLock(&Queues[i].QueueLock);
        KeInitializeSpinLock(&Queues[i].VBlankWaitLock);
        InitializeListHead(&Queues[i].VBlankWaiterList);
#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
        Status = DxgkpCreateDwmVBlankEvent(&Queues[i].DwmVBlankEvent);
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_ERR("DxgkPresentInit: DWM vblank event %lu failed "
                        "(0x%08lx)\n",
                        i,
                        Status);
            while (i != 0)
            {
                --i;
                ObDereferenceObject(Queues[i].DwmVBlankEvent);
                Queues[i].DwmVBlankEvent = NULL;
            }
            ExFreePoolWithTag(Queues, TAG_DXGK_PRESENT);
            return Status;
        }
#endif
    }

    Adapter->PresentQueues     = Queues;
    Adapter->PresentQueueCount = NumSources;

    DXGKRNL_TRACE("DxgkPresentInit: %lu queue(s) ready at %p\n",
                  NumSources, Queues);

    return STATUS_SUCCESS;
}

typedef struct _DXGKP_PRESENT_QUEUE_MATCH_CONTEXT
{
    PDXGKRNL_DEVICE Device;
    PDXGKRNL_CONTEXT Context;
} DXGKP_PRESENT_QUEUE_MATCH_CONTEXT, *PDXGKP_PRESENT_QUEUE_MATCH_CONTEXT;

static BOOLEAN NTAPI
DxgkpMatchQueuedPresent(
    _In_ const VOID *OpaqueEntry,
    _In_opt_ PVOID OpaqueContext)
{
    const DXGKRNL_PRESENT_ENTRY *Entry = OpaqueEntry;
    const DXGKP_PRESENT_QUEUE_MATCH_CONTEXT *MatchContext = OpaqueContext;

    return !((MatchContext->Device != NULL && Entry->Device != MatchContext->Device) || (MatchContext->Context != NULL && Entry->Context != MatchContext->Context));
}

static BOOLEAN
DxgkpRemoveQueuedPresent(
    _Inout_ PDXGKRNL_PRESENT_QUEUE Queue,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_opt_ PDXGKRNL_CONTEXT Context,
    _Out_ PDXGKRNL_PRESENT_ENTRY RemovedEntry)
{
    DXGKP_PRESENT_QUEUE_MATCH_CONTEXT MatchContext;

    MatchContext.Device = Device;
    MatchContext.Context = Context;
    return DxgkPresentQueueCoreRemove(&Queue->QueueLock, Queue->Entries, sizeof(Queue->Entries[0]), DXGKRNL_PRESENT_QUEUE_DEPTH, &Queue->Head, &Queue->Tail, &Queue->Count, DxgkpMatchQueuedPresent, &MatchContext, RemovedEntry);
}

static ULONG
DxgkpCancelQueuedPresents(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_opt_ PDXGKRNL_DEVICE Device,
    _In_opt_ PDXGKRNL_CONTEXT Context)
{
    PDXGKRNL_PRESENT_QUEUE Queues = (PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues;
    ULONG Canceled = 0;
    ULONG Index;

    if (Queues == NULL)
        return 0;
    for (Index = 0; Index < Adapter->PresentQueueCount; ++Index)
    {
        DXGKRNL_PRESENT_ENTRY Entry;

        while (DxgkpRemoveQueuedPresent(&Queues[Index], Device, Context, &Entry))
        {
            Canceled++;
            DxgkpReleasePresentEntry(&Entry);
        }
    }
    return Canceled;
}

VOID
DxgkPresentCancelDevice(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_DEVICE Device)
{
    ULONG Canceled;

    PAGED_CODE();
    if (Adapter == NULL || Device == NULL || !DxgkpAcquirePresentQueues(Adapter))
        return;
    Canceled = DxgkpCancelQueuedPresents(Adapter, Device, NULL);
    DxgkpReleasePresentQueues(Adapter);
    if (Canceled != 0)
        DXGKRNL_TRACE("DxgkPresentCancelDevice: canceled %lu queued present(s) for device %p\n", Canceled, Device);
}

VOID
DxgkPresentCancelContext(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ PDXGKRNL_CONTEXT Context)
{
    ULONG Canceled;

    PAGED_CODE();
    if (Adapter == NULL || Context == NULL || !DxgkpAcquirePresentQueues(Adapter))
        return;
    Canceled = DxgkpCancelQueuedPresents(Adapter, NULL, Context);
    DxgkpReleasePresentQueues(Adapter);
    if (Canceled != 0)
        DXGKRNL_TRACE("DxgkPresentCancelContext: canceled %lu queued present(s) for context %p\n", Canceled, Context);
}

VOID
DxgkPresentCancelAllStopped(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
    ULONG Canceled;

    PAGED_CODE();
    if (Adapter == NULL || InterlockedCompareExchange(&Adapter->PresentQueueStopping, 0, 0) == 0)
        return;
    DxgkpWaitForPresentQueues(Adapter);
    Canceled = DxgkpCancelQueuedPresents(Adapter, NULL, NULL);
    if (Canceled != 0)
        DXGKRNL_TRACE("DxgkPresentCancelAllStopped: canceled %lu queued present(s)\n", Canceled);
}

/* ========================================================================
 * DxgkPresentTeardown
 *
 * Frees the present queue array.  Any queued presents are silently
 * discarded (the adapter is stopping, so there is no display to
 * present to).
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
VOID
DxgkPresentTeardown(
    _In_ PDXGKRNL_ADAPTER Adapter)
{
#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
    PDXGKRNL_PRESENT_QUEUE Queues;
    ULONG Index;
#endif

    PAGED_CODE();

    if (Adapter == NULL)
        return;

    DxgkPresentBeginStop(Adapter);
    DxgkpWaitForPresentQueues(Adapter);

    DxgkPresentCancelAllStopped(Adapter);

    if (Adapter->PresentQueues != NULL)
    {
#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
        Queues = (PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues;
        for (Index = 0; Index < Adapter->PresentQueueCount; ++Index)
        {
            if (Queues[Index].DwmVBlankEvent != NULL)
            {
                ObDereferenceObject(Queues[Index].DwmVBlankEvent);
                Queues[Index].DwmVBlankEvent = NULL;
            }
        }
#endif
        ExFreePoolWithTag(Adapter->PresentQueues, TAG_DXGK_PRESENT);
        Adapter->PresentQueues     = NULL;
        Adapter->PresentQueueCount = 0;

        DXGKRNL_TRACE("DxgkPresentTeardown: present queues freed\n");
    }
}

/* ========================================================================
 * DxgkpExecuteDodPresent  (private)
 *
 * Executes a present on a display-only (DOD) adapter by calling
 * DxgkDdiPresentDisplayOnly.  This pushes the shadow framebuffer
 * contents to the GPU.
 *
 * The logic mirrors DxgkpPresentShadowFb in display.c but is driven
 * by the present queue rather than the periodic timer.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
static NTSTATUS
DxgkpExecuteDodPresent(
    _In_ PDXGKRNL_ADAPTER        Adapter,
    _In_ PDXGKRNL_PRESENT_ENTRY  Entry)
{
    typedef NTSTATUS (APIENTRY *PFN_PRESENT_DISPLAY_ONLY)(
        _In_ PVOID MiniportDeviceContext,
        _In_ CONST DXGKARG_PRESENT_DISPLAYONLY *PresentDisplayOnly);

    PFN_PRESENT_DISPLAY_ONLY PfnPresent;
    DXGKARG_PRESENT_DISPLAYONLY PresentArgs;
    RECT DirtyRect;
    ULONGLONG Start100ns;
    ULONGLONG ElapsedUs;
    LONG ShadowPitch;
    LONG TraceSeq;
    NTSTATUS Status;

    if (Entry->SharedSurface.ShadowFb == NULL || !Entry->SharedSurface.VidPnCommitted || !Entry->SharedSurface.RundownHeld)
    {
        DXGKRNL_TRACE("DxgkpExecuteDodPresent: no shadow FB or VidPn not "
                      "committed\n");
        return STATUS_DEVICE_NOT_READY;
    }

    /*
     * Retrieve the display-only miniport callback. Full WDDM miniports do
     * not contain this slot; their presentation path is DxgkDdiPresent.
     */
    if (Adapter->MiniportContext->UseDodLayout)
    {
        SIZE_T Offset = FIELD_OFFSET(KMDDOD_INITIALIZATION_DATA,
                                     DxgkDdiPresentDisplayOnly);
        if (Adapter->MiniportContext->InitDataSize >= Offset + sizeof(PVOID))
        {
            PfnPresent = *(PFN_PRESENT_DISPLAY_ONLY *)
                ((PUCHAR)&Adapter->MiniportContext->InitData + Offset);
        }
        else
        {
            PfnPresent = NULL;
        }
    }
    else
    {
        PfnPresent = NULL;
    }

    if (Entry->DstSubRectCount == 0)
    {
        DirtyRect = Entry->DstRect;
        if (!DxgkPresentCoreRectValid(&DirtyRect))
            return STATUS_INVALID_PARAMETER;
    }

    /*
     * A display-only miniport that exposes no DxgkDdiPresentDisplayOnly (a
     * software miniport such as softgpu) is not a failure: the display path
     * copies the shadow framebuffer straight to the firmware GOP, which is how
     * every other present on such an adapter already reaches the panel.
     * Refusing here made D3DKMTPresent fail on exactly those adapters.
     */
    if (PfnPresent == NULL)
    {
        return DxgkDisplayPresentRect(Adapter,
                                      Entry->DstSubRectCount != 0
                                          ? &Entry->DstSubRects[0]
                                          : &DirtyRect);
    }

    ASSERT(Entry->SharedSurface.ShadowFbPitch != 0);
    ShadowPitch = (LONG)(Entry->SharedSurface.ShadowFbPitch != 0 ? Entry->SharedSurface.ShadowFbPitch : (Entry->SharedSurface.CommittedWidth * 4));
    if (Entry->SharedSurface.CommittedWidth == 0 || Entry->SharedSurface.CommittedHeight == 0 || ShadowPitch < (LONG)(Entry->SharedSurface.CommittedWidth * 4) || Entry->SharedSurface.ShadowFbSize < ((SIZE_T)(Entry->SharedSurface.CommittedHeight - 1) * (ULONG)ShadowPitch) + ((SIZE_T)Entry->SharedSurface.CommittedWidth * 4))
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(&PresentArgs, sizeof(PresentArgs));
    PresentArgs.VidPnSourceId = Entry->VidPnSourceId;
    PresentArgs.pSource       = Entry->SharedSurface.ShadowFb;
    PresentArgs.BytesPerPixel = 4;
    PresentArgs.Pitch         = ShadowPitch;
    PresentArgs.Flags.Value   = 0;
    PresentArgs.NumMoves      = 0;
    PresentArgs.pMoves        = NULL;
    PresentArgs.NumDirtyRects = Entry->DstSubRectCount != 0
                                    ? Entry->DstSubRectCount
                                    : 1;
    PresentArgs.pDirtyRect    = Entry->DstSubRectCount != 0
                                    ? Entry->DstSubRects
                                    : &DirtyRect;
    PresentArgs.pfnPresentDisplayOnlyProgress = NULL;

    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DELETE_PENDING;
    if (Entry->Device == NULL || InterlockedCompareExchange(&Entry->Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        DxgkEndKmdTransaction(Adapter);
        return STATUS_DEVICE_REMOVED;
    }
    if (!DxgkAcquireKmdCall(Adapter))
    {
        DxgkEndKmdTransaction(Adapter);
        return STATUS_DELETE_PENDING;
    }
    Start100ns = DxgkpPresentTraceNow100ns();
    Status = PfnPresent(Adapter->MiniportDeviceContext, &PresentArgs);
    DxgkReleaseKmdCall(Adapter);
    DxgkEndKmdTransaction(Adapter);
    ElapsedUs = DxgkpPresentTraceElapsedUs(Start100ns);
    TraceSeq = InterlockedIncrement(&g_DodPresentTraceCount);

    if (TraceSeq <= DXGK_PRESENT_EXEC_LOG_LIMIT ||
        ElapsedUs >= DXGK_PRESENT_EXEC_SLOW_US)
    {
        DXGKRNL_TRACE("DxgkpExecuteDodPresent: seq=%ld PresentId=%llu "
                      "VidPnSrc=%u status=0x%08lX dur=%I64u us size=%ux%u\n",
                      TraceSeq,
                      Entry->PresentId,
                      Entry->VidPnSourceId,
                      Status,
                      ElapsedUs,
                      Entry->SharedSurface.CommittedWidth,
                      Entry->SharedSurface.CommittedHeight);
    }

    return Status;
}

/* ========================================================================
 * DxgkpExecuteCpuPresent  (private)
 *
 * Handles CPU-backed blit presents created by the compatibility D3DKMT path.
 * These allocations have dxgkrnl backing memory but no miniport allocation
 * handle, so calling the full miniport present DDI would fail before a real
 * scheduler submission can exist.
 * ====================================================================== */
static PVOID
DxgkpGetCpuAllocationAddress(
    _In_ PDXGKVMM_ALLOCATION Allocation)
{
    if (Allocation == NULL)
        return NULL;
    if (Allocation->CpuAddress != NULL)
        return Allocation->CpuAddress;
    if (Allocation->SystemMemory != NULL)
        return Allocation->SystemMemory;
    return NULL;
}

static VOID
DxgkpFillPresentAllocationListEntry(
    _In_ PDXGKVMM_ALLOCATION Allocation,
    _Inout_ PDXGK_ALLOCATIONLIST ListEntry)
{
    if (Allocation == NULL || ListEntry == NULL)
        return;
    ListEntry->SegmentId = (Allocation->SegmentId <= 31) ? Allocation->SegmentId : 0;
    ListEntry->PhysicalAddress = Allocation->PhysicalAddress;
}

static NTSTATUS
DxgkpSelectPresentNode(
    _In_ PDXGKRNL_ADAPTER Adapter,
    _In_ DXGKRNL_PRESENT_TYPE PresentType,
    _Out_ PULONG OutNode)
{
    PDXGKDDI_GET_NODE_METADATA GetNodeMetadata;
    DXGK_ENGINE_TYPE DesiredType;
    DXGK_ENGINE_TYPE FallbackType;
    ULONG Pass;
    ULONG Node;

    if (Adapter == NULL || OutNode == NULL || Adapter->NodeCount == 0)
        return STATUS_NOT_SUPPORTED;
    *OutNode = 0;
    if (Adapter->NodeCount == 1)
        return STATUS_SUCCESS;

    GetNodeMetadata = DXGK_CB_FULL(Adapter, DxgkDdiGetNodeMetadata);
    if (GetNodeMetadata == NULL)
        return STATUS_NOT_SUPPORTED;
    if (!DxgkAcquireKmdCall(Adapter))
        return STATUS_DELETE_PENDING;

    DesiredType = (PresentType == DxgkPresentTypeFlip) ? DXGK_ENGINE_TYPE_3D : DXGK_ENGINE_TYPE_COPY;
    FallbackType = (DesiredType == DXGK_ENGINE_TYPE_COPY) ? DXGK_ENGINE_TYPE_3D : DXGK_ENGINE_TYPE_COPY;
    for (Pass = 0; Pass < 2; ++Pass)
    {
        DXGK_ENGINE_TYPE RequestedType = (Pass == 0) ? DesiredType : FallbackType;

        for (Node = 0; Node < Adapter->NodeCount; ++Node)
        {
            DXGKARG_GETNODEMETADATA Metadata;
            NTSTATUS Status;

            RtlZeroMemory(&Metadata, sizeof(Metadata));
            _SEH2_TRY
            {
                Status = GetNodeMetadata(Adapter->MiniportDeviceContext, Node, &Metadata);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            if (NT_SUCCESS(Status) && Metadata.EngineType == RequestedType)
            {
                *OutNode = Node;
                DxgkReleaseKmdCall(Adapter);
                return STATUS_SUCCESS;
            }
        }
    }

    DxgkReleaseKmdCall(Adapter);
    return STATUS_NOT_SUPPORTED;
}

static ULONG
DxgkpFirstEngineOrdinal(
    _In_ UINT EngineAffinity)
{
    ULONG EngineOrdinal;

    if (EngineAffinity == 0)
        return 0;
    for (EngineOrdinal = 0; EngineOrdinal < 32; ++EngineOrdinal)
    {
        if ((EngineAffinity & (1u << EngineOrdinal)) != 0)
            return EngineOrdinal;
    }
    return 0;
}

static NTSTATUS
DxgkpExecuteCpuPresent(
    _In_  PDXGKRNL_ADAPTER        Adapter,
    _In_  PDXGKRNL_PRESENT_ENTRY  Entry,
    _Out_ PBOOLEAN                Handled)
{
    PDXGKVMM_ALLOCATION SourceAllocation;
    PDXGKVMM_ALLOCATION DestinationAllocation = NULL;
    PVOID SourceAddress;
    PVOID DestinationAddress = NULL;
    SIZE_T BytesToCopy;
    NTSTATUS Status;

    if (Handled == NULL)
        return STATUS_INVALID_PARAMETER;

    *Handled = FALSE;

    if (Adapter == NULL ||
        Entry == NULL ||
        Entry->Type != DxgkPresentTypeBlt ||
        Entry->hSource == 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    SourceAllocation = Entry->SourceAllocation;
    if (SourceAllocation == NULL || SourceAllocation->Adapter != Adapter)
        return STATUS_INVALID_HANDLE;

    SourceAddress = DxgkpGetCpuAllocationAddress(SourceAllocation);
    if (SourceAddress == NULL)
        return STATUS_NOT_SUPPORTED;

    *Handled = TRUE;

    if (!DxgkPresentCoreIsFullDestinationRegion(
             &Entry->DstRect,
             Entry->DstSubRects,
             Entry->DstSubRectCount))
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Entry->hDestination != 0)
    {
        DestinationAllocation = Entry->DestinationAllocation;
        if (DestinationAllocation == NULL || DestinationAllocation->Adapter != Adapter)
            return STATUS_INVALID_HANDLE;

        DestinationAddress = DxgkpGetCpuAllocationAddress(DestinationAllocation);
        if (DestinationAddress == NULL)
        {
            Status = DxgkVidMmEnsureAllocationApertureMapped(DestinationAllocation);
            if (!NT_SUCCESS(Status))
                return Status;

            DestinationAddress = DxgkpGetCpuAllocationAddress(DestinationAllocation);
        }

        if (DestinationAddress == NULL)
            return STATUS_NOT_SUPPORTED;

        BytesToCopy = (SourceAllocation->Size < DestinationAllocation->Size) ?
                      SourceAllocation->Size : DestinationAllocation->Size;
        if (BytesToCopy != 0 && SourceAddress != DestinationAddress)
            RtlCopyMemory(DestinationAddress, SourceAddress, BytesToCopy);

        if (Entry->DestinationIsSharedPrimary)
        {
            Status = DxgkpProgramSharedPrimaryScanout(Adapter, DestinationAllocation, Entry->SharedSurface.VidPnSourceId, Entry->hDestination, Entry->PresentId);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("DxgkpExecuteCpuPresent: shared-primary scanout "
                             "refresh failed 0x%08lX dst=0x%X\n",
                             Status,
                             Entry->hDestination);
                return Status;
            }
        }
    }

    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkpExecuteFullPresent  (private)
 *
 * Executes a present on a full WDDM adapter by calling DxgkDdiPresent
 * on the miniport.  The miniport builds a DMA packet for the GPU
 * scheduler.
 *
 * The miniport builds a command into an owned DMA buffer; dxgkrnl patches it,
 * selects a metadata-declared engine, and submits it through VidSch (or the
 * tracked direct fallback when VidSch is unavailable).
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
static NTSTATUS
DxgkpExecuteFullPresent(
    _In_ PDXGKRNL_ADAPTER        Adapter,
    _In_ PDXGKRNL_PRESENT_ENTRY  Entry)
{
    DXGKARG_PRESENT PresentArgs;
    PDXGK_ALLOCATIONLIST PresentAllocationList = NULL;
    D3DDDI_PATCHLOCATIONLIST PatchLocationList[VIDSCH_INLINE_PATCHES];
    DXGK_PRESENT_DMA_GEOMETRY DmaGeometry;
    RECT DstSubRect;
    HANDLE SourceDeviceSpecificHandle = NULL;
    HANDLE DestinationDeviceSpecificHandle = NULL;
    PDXGKVMM_ALLOCATION PresentBindingReferences[2];
    UINT PresentBindingReferenceCount = 0;
    PDXGKRNL_DEVICE Device = NULL;
    PDXGKRNL_CONTEXT Context;
    HANDLE MiniportDeviceHandle;
    HANDLE MiniportContextHandle;
    HANDLE MiniportPresentContext;
    PDXGKRNL_DMA_BUFFER DmaBuffer = NULL;
    PDXGKRNL_SUBMIT_DMA_BUFFER Reservation = NULL;
    DXGKRNL_TRACK_DMA_ARGS TrackArgs;
    PVOID DmaBufferPrivateData = NULL;
    ULONG DmaBufferPrivateDataSize = 0;
    SIZE_T PresentAllocationListBytes = 0;
    ULONG SubmissionFenceId = 0;
    UINT DmaBytesUsed = 0;
    BOOLEAN PresentBindingsTracked = FALSE;
    BOOLEAN RefreshSharedPrimaryOnRetire = FALSE;
    ULONG PresentNode;
    ULONG PresentEngine;
    LONG PresentPriority;
    LONG TraceSeq;
    NTSTATUS Status;
    BOOLEAN Handled;
    BOOLEAN KmdTransaction = FALSE;

    if (!DxgkBeginKmdTransaction(Adapter))
        return STATUS_DELETE_PENDING;
    KmdTransaction = TRUE;
    Device = Entry->Device;
    if (Device == NULL || Device->Adapter != Adapter)
    {
        DXGKRNL_WARN("DxgkpExecuteFullPresent: invalid referenced device %p\n", Device);
        Status = STATUS_INVALID_HANDLE;
        goto PresentCleanup;
    }
    if (InterlockedCompareExchange(&Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE)
    {
        Status = STATUS_DEVICE_REMOVED;
        goto PresentCleanup;
    }
    Status = DxgkpExecuteCpuPresent(Adapter, Entry, &Handled);
    if (Handled)
        goto PresentCleanup;

    /*
     * Check that the miniport provides DxgkDdiPresent.
     * DOD drivers do not have this callback.
     */
    if (DXGK_CB_FULL(Adapter, DxgkDdiPresent) == NULL)
    {
        DXGKRNL_TRACE("DxgkpExecuteFullPresent: no DxgkDdiPresent DDI\n");
        Status = STATUS_NOT_SUPPORTED;
        goto PresentCleanup;
    }

    RtlZeroMemory(&PresentArgs, sizeof(PresentArgs));
    RtlZeroMemory(PresentBindingReferences, sizeof(PresentBindingReferences));

    MiniportDeviceHandle = Device->hMiniportDevice;
    if (MiniportDeviceHandle == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
        goto PresentCleanup;
    }
    MiniportContextHandle = NULL;
    Context = Entry->Context;
    PresentEngine = 0;
    PresentPriority = 0;
    if (Context != NULL)
    {
        if (Context->Device != Device || Context->hMiniportContext == NULL || Context->NodeOrdinal >= Adapter->NodeCount)
        {
            Status = STATUS_INVALID_HANDLE;
            goto PresentCleanup;
        }
        MiniportPresentContext = Context->hMiniportContext;
        MiniportContextHandle = Context->hMiniportContext;
        PresentNode = Context->NodeOrdinal;
        PresentEngine = DxgkpFirstEngineOrdinal(Context->EngineAffinity);
        PresentPriority = Context->SchedulingPriority;
    }
    else
    {
        if (Adapter->SchedulingCaps.MultiEngineAware)
        {
            Status = STATUS_INVALID_HANDLE;
            goto PresentCleanup;
        }
        MiniportPresentContext = MiniportDeviceHandle;
        Status = DxgkpSelectPresentNode(Adapter, Entry->Type, &PresentNode);
        if (!NT_SUCCESS(Status))
            goto PresentCleanup;
    }

    if (Context != NULL)
    {
        Status = DxgkPresentDmaCoreSelectGeometry(
                     TRUE,
                     Context->ContextInfo.DmaBufferSize,
                     Context->ContextInfo.DmaBufferSegmentSet,
                     Context->ContextInfo.DmaBufferPrivateDataSize,
                     Context->ContextInfo.AllocationListSize,
                     Context->ContextInfo.PatchLocationListSize,
                     VIDSCH_INLINE_PATCHES,
                     &DmaGeometry);
    }
    else if (Device->LegacyDeviceInfoValid)
    {
        Status = DxgkPresentDmaCoreSelectGeometry(
                     TRUE,
                     Device->LegacyDeviceInfo.DmaBufferSize,
                     Device->LegacyDeviceInfo.DmaBufferSegmentSet,
                     Device->LegacyDeviceInfo.DmaBufferPrivateDataSize,
                     Device->LegacyDeviceInfo.AllocationListSize,
                     Device->LegacyDeviceInfo.PatchLocationListSize,
                     VIDSCH_INLINE_PATCHES,
                     &DmaGeometry);
    }
    else
    {
        Status = DxgkPresentDmaCoreSelectGeometry(
                     FALSE,
                     0,
                     0,
                     0,
                     0,
                     0,
                     VIDSCH_INLINE_PATCHES,
                     &DmaGeometry);
    }
    if (!NT_SUCCESS(Status))
        goto PresentCleanup;

    if (Entry->hSource != 0 && (Entry->SourceAllocation == NULL || Entry->SourceAllocation->MiniportHandle == NULL))
    {
        Status = STATUS_NOT_SUPPORTED;
        goto PresentCleanup;
    }
    if (Entry->hDestination != 0 && (Entry->DestinationAllocation == NULL || Entry->DestinationAllocation->MiniportHandle == NULL))
    {
        Status = STATUS_NOT_SUPPORTED;
        goto PresentCleanup;
    }

    Status = DxgkpRefreshSharedPrimaryScanout(Adapter, Entry, &Handled);
    if (Handled)
    {
        if (!NT_SUCCESS(Status))
        {
            DXGKRNL_WARN("DxgkpExecuteFullPresent: shared-primary refresh "
                         "failed 0x%08lX\n",
                         Status);
        }

        goto PresentCleanup;
    }

    Status = DxgkPresentDmaCoreAllocationListBytes(
                 DmaGeometry.AllocationListSize,
                 sizeof(PresentAllocationList[0]),
                 &PresentAllocationListBytes);
    if (!NT_SUCCESS(Status))
        goto PresentCleanup;
    PresentAllocationList = ExAllocatePoolWithTag(
                                NonPagedPool,
                                PresentAllocationListBytes,
                                TAG_DXGK_PRESENT);
    if (PresentAllocationList == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto PresentCleanup;
    }
    Status = DxgkPresentDmaCoreInitializeAllocationList(
                 PresentAllocationList,
                 PresentAllocationListBytes);
    if (!NT_SUCCESS(Status))
        goto PresentCleanup;

    /*
     * Present uses an allocation list where slot 0 is always reserved.
     * Source and destination miniport handles live in slots 1 and 2.
     */
    if (Entry->hSource != 0)
    {
        if (Entry->SourceOpenBindingReference != NULL)
            SourceDeviceSpecificHandle = Entry->SourceOpenBindingHandle;
        else
        {
            BOOLEAN ReadOnly = Entry->hDestination == 0 || Entry->hDestination != Entry->hSource;

            Status = DxgkVidMmCreatePresentBinding(Device, Entry->SourceAllocation, ReadOnly, &SourceDeviceSpecificHandle, &PresentBindingReferences[PresentBindingReferenceCount]);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("DxgkpExecuteFullPresent: invalid hSource=0x%X\n",
                             Entry->hSource);
                goto PresentCleanup;
            }
            PresentBindingReferenceCount++;
        }

        PresentAllocationList[DXGK_PRESENT_SOURCE_INDEX].hDeviceSpecificAllocation = SourceDeviceSpecificHandle;
        DxgkpFillPresentAllocationListEntry(Entry->SourceAllocation, &PresentAllocationList[DXGK_PRESENT_SOURCE_INDEX]);
    }

    if (Entry->hDestination != 0)
    {
        if (Entry->hDestination == Entry->hSource &&
            SourceDeviceSpecificHandle != NULL)
        {
            DestinationDeviceSpecificHandle = SourceDeviceSpecificHandle;
        }
        else if (Entry->DestinationOpenBindingReference != NULL)
        {
            DestinationDeviceSpecificHandle = Entry->DestinationOpenBindingHandle;
        }
        else
        {
            Status = DxgkVidMmCreatePresentBinding(Device, Entry->DestinationAllocation, FALSE, &DestinationDeviceSpecificHandle, &PresentBindingReferences[PresentBindingReferenceCount]);
            if (!NT_SUCCESS(Status))
            {
                DXGKRNL_WARN("DxgkpExecuteFullPresent: invalid hDestination=0x%X\n",
                             Entry->hDestination);
                goto PresentCleanup;
            }
            PresentBindingReferenceCount++;
        }

        PresentAllocationList[DXGK_PRESENT_DESTINATION_INDEX].hDeviceSpecificAllocation = DestinationDeviceSpecificHandle;
        PresentAllocationList[DXGK_PRESENT_DESTINATION_INDEX].WriteOperation = 1;
        DxgkpFillPresentAllocationListEntry(Entry->DestinationAllocation, &PresentAllocationList[DXGK_PRESENT_DESTINATION_INDEX]);

    }

    TraceSeq = InterlockedIncrement(&g_PresentOpenTraceCount);
    if (DxgkpShouldTraceOrdinal(TraceSeq))
    {
        DXGKRNL_TRACE("DxgkpExecuteFullPresent: seq=%ld PresentId=%llu srcAlloc=0x%X "
                      "srcOpen=%p dstAlloc=0x%X dstOpen=%p type=%u rect=(%ld,%ld)-(%ld,%ld)\n",
                      TraceSeq,
                      Entry->PresentId,
                      Entry->hSource,
                      SourceDeviceSpecificHandle,
                      Entry->hDestination,
                      DestinationDeviceSpecificHandle,
                      Entry->Type,
                      Entry->DstRect.left,
                      Entry->DstRect.top,
                      Entry->DstRect.right,
                      Entry->DstRect.bottom);
    }

    Status = DxgkAllocateDmaBuffer(Adapter, DmaGeometry.DmaBufferSize, &DmaBuffer);
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpExecuteFullPresent: DMA buffer alloc failed\n");
        goto PresentCleanup;
    }

    DmaBufferPrivateDataSize = DmaGeometry.DmaBufferPrivateDataSize;
    if (DmaBufferPrivateDataSize != 0)
    {
        DmaBufferPrivateData = ExAllocatePoolWithTag(
                                   NonPagedPool,
                                   DmaBufferPrivateDataSize,
                                   TAG_DXGK_SUBMITDMA);
        if (DmaBufferPrivateData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto PresentCleanup;
        }
    }
    Status = DxgkPresentDmaCoreInitializePrivateData(
                 DmaBufferPrivateData,
                 DmaBufferPrivateDataSize);
    if (!NT_SUCCESS(Status))
        goto PresentCleanup;

    RtlZeroMemory(PatchLocationList, sizeof(PatchLocationList));
    if (Entry->DstSubRectCount == 0)
        DstSubRect = Entry->DstRect;

    PresentArgs.pDmaBuffer            = DmaBuffer->VirtualAddress;
    PresentArgs.DmaSize               = DmaGeometry.DmaBufferSize;
    PresentArgs.pDmaBufferPrivateData = DmaBufferPrivateData;
    PresentArgs.DmaBufferPrivateDataSize = DmaBufferPrivateDataSize;
    PresentArgs.pAllocationList       = PresentAllocationList;
    PresentArgs.pPatchLocationListOut = PatchLocationList;
    PresentArgs.PatchLocationListOutSize = DmaGeometry.PatchLocationListSize;
    PresentArgs.MultipassOffset       = 0;
    PresentArgs.DmaBufferSegmentId    = DmaBuffer->SegmentId;
    PresentArgs.DmaBufferPhysicalAddress = DmaBuffer->SegmentAddress;
    PresentArgs.Reserved              = 0;
    PresentArgs.NumSrcAllocations     = (SourceDeviceSpecificHandle != NULL) ? 1 : 0;
    PresentArgs.NumDstAllocations     = (DestinationDeviceSpecificHandle != NULL) ? 1 : 0;
    PresentArgs.PrivateDriverDataSize = 0;
    PresentArgs.pPrivateDriverData    = NULL;

    /* Source and destination rectangles. */
    PresentArgs.SrcRect      = Entry->SrcRect;
    PresentArgs.DstRect      = Entry->DstRect;
    PresentArgs.SubRectCnt   = Entry->DstSubRectCount != 0
                                   ? Entry->DstSubRectCount
                                   : 1;
    PresentArgs.pDstSubRects = Entry->DstSubRectCount != 0
                                   ? Entry->DstSubRects
                                   : &DstSubRect;

    /* Map the present type to miniport flags. */
    PresentArgs.Flags.Value = 0;
    switch (Entry->Type)
    {
        case DxgkPresentTypeBlt:
            PresentArgs.Flags.Blt = 1;
            break;
        case DxgkPresentTypeFlip:
            PresentArgs.Flags.Flip = 1;
            break;
        case DxgkPresentTypeColorFill:
            PresentArgs.Flags.ColorFill = 1;
            break;
    }

    PresentArgs.Color           = Entry->Color;
    PresentArgs.FlipInterval    = Entry->FlipInterval;

    if (!DxgkAcquireKmdCall(Adapter))
    {
        Status = STATUS_DELETE_PENDING;
        goto PresentCleanup;
    }
    _SEH2_TRY
    {
        Status = DXGK_CB_FULL(Adapter, DxgkDdiPresent)(MiniportPresentContext, &PresentArgs);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        DXGKRNL_ERR("DxgkpExecuteFullPresent: DxgkDdiPresent FAULTED "
                    "0x%08lX\n", Status);
    }
    _SEH2_END;
    DxgkReleaseKmdCall(Adapter);

    if (NT_SUCCESS(Status) &&
        (PresentArgs.pDmaBufferPrivateData != DmaBufferPrivateData ||
         PresentArgs.DmaBufferPrivateDataSize != DmaBufferPrivateDataSize))
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    if (NT_SUCCESS(Status))
    {
        ULONG_PTR DmaBufferStart = (ULONG_PTR)DmaBuffer->VirtualAddress;
        ULONG_PTR DmaBufferNext = (ULONG_PTR)PresentArgs.pDmaBuffer;

        if (PresentArgs.pDmaBuffer == NULL ||
            DmaBufferNext < DmaBufferStart ||
            DmaBufferNext - DmaBufferStart > DmaBuffer->Capacity)
        {
            Status = STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER;
        }
        else
        {
            DmaBytesUsed = (UINT)(DmaBufferNext - DmaBufferStart);
        }
    }
    if (NT_SUCCESS(Status) && DmaBytesUsed == 0)
        Status = STATUS_NOT_SUPPORTED;
    if (NT_SUCCESS(Status) && DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand) == NULL)
        Status = STATUS_NOT_SUPPORTED;
    if (NT_SUCCESS(Status) && DmaBytesUsed != 0)
    {
        DmaBuffer->SubmissionStartOffset = 0;
        DmaBuffer->SubmissionEndOffset = DmaBytesUsed;
    }

    if (NT_SUCCESS(Status) && DmaBytesUsed > 0)
    {
        DXGKARG_SUBMITCOMMAND SubmitArgs;
        DXGKARG_PATCH PatchArgs;
        DXGK_SUBMITCOMMANDFLAGS SubmitFlags;
        PDXGKRNL_SUBMIT_DMA_BUFFER CommittedReservation;
        ULONG VidSchFence = 0;
        UINT PatchEntries;
        UINT PatchIndex;
        DXGK_PRESENT_SCANOUT_RETIRE_POLICY
            ScanoutRetirePolicy;
        BOOLEAN TrackSourceScanout;
        BOOLEAN TrackRefresh;

        PatchEntries = 0;
        if (PresentArgs.pPatchLocationListOut != NULL)
        {
            ULONG_PTR PatchListStart = (ULONG_PTR)PatchLocationList;
            ULONG_PTR PatchListNext =
                (ULONG_PTR)PresentArgs.pPatchLocationListOut;
            SIZE_T PatchBytes =
                (SIZE_T)DmaGeometry.PatchLocationListSize *
                sizeof(PatchLocationList[0]);

            if (PatchListNext < PatchListStart ||
                PatchListNext - PatchListStart > PatchBytes ||
                ((PatchListNext - PatchListStart) %
                    sizeof(PatchLocationList[0])) != 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto PresentSubmissionDone;
            }
            PatchEntries = (UINT)((PatchListNext - PatchListStart) /
                                  sizeof(PatchLocationList[0]));
        }
        for (PatchIndex = 0; PatchIndex < PatchEntries; ++PatchIndex)
        {
            UINT AllocationIndex =
                PatchLocationList[PatchIndex].AllocationIndex;

            if (AllocationIndex == 0 ||
                AllocationIndex > DXGK_PRESENT_MAX_INDEX ||
                (AllocationIndex == DXGK_PRESENT_SOURCE_INDEX &&
                 SourceDeviceSpecificHandle == NULL) ||
                (AllocationIndex == DXGK_PRESENT_DESTINATION_INDEX &&
                 DestinationDeviceSpecificHandle == NULL))
            {
                Status = STATUS_INVALID_PARAMETER;
                goto PresentSubmissionDone;
            }
        }

        SubmitFlags.Value = 0;
        SubmitFlags.Present = 1;
        if (Entry->Type == DxgkPresentTypeFlip)
            SubmitFlags.Flip = 1;
        Status = DxgkPresentCoreEvaluateScanoutRetirement(
                     Entry->Type == DxgkPresentTypeFlip,
                     Entry->Type == DxgkPresentTypeBlt ||
                         Entry->Type == DxgkPresentTypeColorFill,
                     Entry->hSource != 0,
                     Entry->hDestination != 0,
                     Entry->DestinationIsSharedPrimary,
                     &ScanoutRetirePolicy);
        if (!NT_SUCCESS(Status))
            goto PresentSubmissionDone;
        TrackRefresh =
            ScanoutRetirePolicy.RefreshSharedPrimary;
        TrackSourceScanout =
            ScanoutRetirePolicy.ProgramSource;

        RtlZeroMemory(&TrackArgs, sizeof(TrackArgs));
        TrackArgs.PresentId = Entry->PresentId;
        TrackArgs.Device = Device;
        TrackArgs.DeviceWork = Entry->DeviceWork;
        TrackArgs.Context = Context;
        TrackArgs.SourceAllocation = Entry->SourceAllocation;
        TrackArgs.RefreshAllocation = TrackRefresh ? Entry->DestinationAllocation : NULL;
        TrackArgs.SourceOpenBindingReference = Entry->SourceOpenBindingReference;
        TrackArgs.DestinationOpenBindingReference = Entry->DestinationOpenBindingReference;
        TrackArgs.SourceAllocationHandle = (HANDLE)(ULONG_PTR)Entry->hSource;
        TrackArgs.RefreshAllocationHandle = TrackRefresh ? (HANDLE)(ULONG_PTR)Entry->hDestination : NULL;
        TrackArgs.RefreshVidPnSourceId = Entry->VidPnSourceId;
        TrackArgs.RefreshDstRect = &Entry->DstRect;
        TrackArgs.SharedSurfaceGeneration = Entry->SharedSurface.Generation;
        TrackArgs.SourceIsSharedPrimary = Entry->SourceIsSharedPrimary;
        TrackArgs.SourceIsSharedShadow = Entry->SourceIsSharedShadow;
        TrackArgs.RefreshIsSharedPrimary = TrackRefresh;
        TrackArgs.ProgramSourceScanoutOnRetire =
            TrackSourceScanout;
        TrackArgs.HoldSharedSurfaceRundown =
            ScanoutRetirePolicy.HoldSharedSurfaceRundown;
        TrackArgs.SourceWidth = Entry->SourceIsSharedShadow ? Entry->SharedSurface.ShadowWidth : Entry->SharedSurface.PrimaryWidth;
        TrackArgs.SourceHeight = Entry->SourceIsSharedShadow ? Entry->SharedSurface.ShadowHeight : Entry->SharedSurface.PrimaryHeight;
        TrackArgs.SourcePitch = Entry->SourceIsSharedShadow ? Entry->SharedSurface.ShadowPitch : 0;
        TrackArgs.RefreshWidth = Entry->SharedSurface.PrimaryWidth;
        TrackArgs.RefreshHeight = Entry->SharedSurface.PrimaryHeight;
        TrackArgs.PresentBindingReferences = PresentBindingReferences;
        TrackArgs.PresentBindingReferenceCount = PresentBindingReferenceCount;

        Status = VidSchSubmitCommandTracked(Adapter, PresentNode, PresentEngine, DmaBuffer, DmaBufferPrivateData, DmaBufferPrivateDataSize, PresentAllocationList, DXGK_PRESENT_MAX_INDEX + 1, PatchLocationList, PatchEntries, Adapter->SchedulingCaps.MultiEngineAware ? NULL : MiniportDeviceHandle, Adapter->SchedulingCaps.MultiEngineAware ? MiniportContextHandle : NULL, PresentPriority, &TrackArgs, SubmitFlags.Value, Entry->VidPnSourceId, &VidSchFence);
        if (NT_SUCCESS(Status))
        {
            SubmissionFenceId = VidSchFence;
            Entry->DeviceWork = NULL;
            DmaBuffer = NULL;
            PresentBindingsTracked = TRUE;
            RefreshSharedPrimaryOnRetire = TrackRefresh;
            goto PresentSubmissionDone;
        }

        if (!(Status == STATUS_DEVICE_NOT_READY && Adapter->VidSchContext == NULL))
            goto PresentSubmissionDone;

        SubmissionFenceId = DxgkAllocateSubmissionFenceId(Adapter);
        if (SubmissionFenceId == 0)
        {
            Status = STATUS_INTEGER_OVERFLOW;
            goto PresentSubmissionDone;
        }
        TrackArgs.SubmissionFenceId = SubmissionFenceId;
        TrackArgs.NodeOrdinal = PresentNode;
        TrackArgs.EngineOrdinal = PresentEngine;
        TrackArgs.DmaBuffer = DmaBuffer;
        Status = DxgkPrepareTrackedDmaBuffer(Adapter, &TrackArgs, &Reservation);
        if (!NT_SUCCESS(Status))
            goto PresentSubmissionDone;

        if (DXGK_CB_FULL(Adapter, DxgkDdiPatch) != NULL)
        {
            RtlZeroMemory(&PatchArgs, sizeof(PatchArgs));
            if (Adapter->SchedulingCaps.MultiEngineAware)
                PatchArgs.hContext = MiniportContextHandle;
            else
                PatchArgs.hDevice = MiniportDeviceHandle;
            PatchArgs.DmaBufferSegmentId = DmaBuffer->SegmentId;
            PatchArgs.DmaBufferPhysicalAddress = DmaBuffer->SegmentAddress;
            PatchArgs.pDmaBuffer = DmaBuffer->VirtualAddress;
            PatchArgs.DmaBufferSize = DmaBuffer->Capacity;
            PatchArgs.DmaBufferSubmissionStartOffset = DmaBuffer->SubmissionStartOffset;
            PatchArgs.DmaBufferSubmissionEndOffset = DmaBuffer->SubmissionEndOffset;
            PatchArgs.pDmaBufferPrivateData = DmaBufferPrivateData;
            PatchArgs.DmaBufferPrivateDataSize = DmaBufferPrivateDataSize;
            PatchArgs.DmaBufferPrivateDataSubmissionStartOffset = 0;
            PatchArgs.DmaBufferPrivateDataSubmissionEndOffset = DmaBufferPrivateDataSize;
            PatchArgs.pAllocationList = PresentAllocationList;
            PatchArgs.AllocationListSize = DXGK_PRESENT_MAX_INDEX + 1;
            PatchArgs.pPatchLocationList = PatchLocationList;
            PatchArgs.PatchLocationListSize = PatchEntries;
            PatchArgs.PatchLocationListSubmissionStart = 0;
            PatchArgs.PatchLocationListSubmissionLength = PatchEntries;
            PatchArgs.SubmissionFenceId = SubmissionFenceId;
            PatchArgs.Flags.Value = SubmitFlags.Value & 0x0fu;
            PatchArgs.EngineOrdinal = PresentEngine;

            if (!DxgkAcquireKmdCall(Adapter))
            {
                Status = STATUS_DELETE_PENDING;
                goto PresentSubmissionDone;
            }
            _SEH2_TRY
            {
                Status = DXGK_CB_FULL(Adapter, DxgkDdiPatch)(Adapter->MiniportDeviceContext, &PatchArgs);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            DxgkReleaseKmdCall(Adapter);
            if (!NT_SUCCESS(Status))
                goto PresentSubmissionDone;
        }

        RtlZeroMemory(&SubmitArgs, sizeof(SubmitArgs));
        if (Adapter->SchedulingCaps.MultiEngineAware)
            SubmitArgs.hContext = MiniportContextHandle;
        else
            SubmitArgs.hDevice = MiniportDeviceHandle;
        SubmitArgs.DmaBufferSegmentId = DmaBuffer->SegmentId;
        SubmitArgs.DmaBufferPhysicalAddress = DmaBuffer->SegmentAddress;
        SubmitArgs.DmaBufferSize = DmaBuffer->Capacity;
        SubmitArgs.pDmaBufferPrivateData = DmaBufferPrivateData;
        SubmitArgs.DmaBufferPrivateDataSize = DmaBufferPrivateDataSize;
        SubmitArgs.DmaBufferPrivateDataSubmissionStartOffset = 0;
        SubmitArgs.DmaBufferPrivateDataSubmissionEndOffset = DmaBufferPrivateDataSize;
        SubmitArgs.DmaBufferSubmissionStartOffset = DmaBuffer->SubmissionStartOffset;
        SubmitArgs.DmaBufferSubmissionEndOffset = DmaBuffer->SubmissionEndOffset;
        SubmitArgs.SubmissionFenceId = SubmissionFenceId;
        SubmitArgs.VidPnSourceId = Entry->VidPnSourceId;
        SubmitArgs.FlipInterval = PresentArgs.FlipInterval;
        SubmitArgs.NodeOrdinal = PresentNode;
        SubmitArgs.EngineOrdinal = PresentEngine;
        SubmitArgs.Flags = SubmitFlags;
        if (!DxgkAcquireKmdCall(Adapter))
        {
            Status = STATUS_DELETE_PENDING;
            goto PresentSubmissionDone;
        }
        if (!DxgkReserveSubmissionFenceIdentity(Adapter, PresentNode, SubmissionFenceId))
        {
            DxgkReleaseKmdCall(Adapter);
            Status = STATUS_DEVICE_BUSY;
            goto PresentSubmissionDone;
        }
        Reservation->FenceIdentityOwned = TRUE;
        Status = DxgkActivateTrackedDmaBuffer(Reservation);
        if (!NT_SUCCESS(Status))
        {
            DxgkReleaseKmdCall(Adapter);
            goto PresentSubmissionDone;
        }
        Entry->DeviceWork = NULL;
        DxgkPublishSubmittedFence(Adapter, PresentNode, SubmissionFenceId);
        Status = DXGK_CB_FULL(Adapter, DxgkDdiSubmitCommand)(Adapter->MiniportDeviceContext, &SubmitArgs);
        DxgkReleaseKmdCall(Adapter);
        if (!NT_SUCCESS(Status))
            KeBugCheckEx(0x119, 0x2, (ULONG_PTR)Status, (ULONG_PTR)&SubmitArgs, (ULONG_PTR)Adapter);

        CommittedReservation = Reservation;
        Reservation = NULL;
        DmaBuffer = NULL;
        PresentBindingsTracked = TRUE;
        RefreshSharedPrimaryOnRetire = TrackRefresh;
        DxgkCommitTrackedDmaBuffer(Adapter, CommittedReservation);

PresentSubmissionDone:
        if (Reservation != NULL)
        {
            DxgkCancelTrackedDmaBuffer(Reservation);
            Reservation = NULL;
        }
        if (NT_SUCCESS(Status) && RefreshSharedPrimaryOnRetire)
        {
            TraceSeq = InterlockedIncrement(&g_SharedPrimaryPresentTraceCount);
            if (DxgkpShouldTraceOrdinal(TraceSeq))
                DXGKRNL_TRACE("DxgkpExecuteFullPresent: seq=%ld PresentId=%llu queueing shared-primary refresh on retire fence=%u dst=0x%X src=0x%X rect=(%ld,%ld)-(%ld,%ld)\n", TraceSeq, Entry->PresentId, SubmissionFenceId, Entry->hDestination, Entry->hSource, Entry->DstRect.left, Entry->DstRect.top, Entry->DstRect.right, Entry->DstRect.bottom);
        }
    }

    if (NT_SUCCESS(Status) &&
        !RefreshSharedPrimaryOnRetire &&
        (Entry->Type == DxgkPresentTypeBlt ||
         Entry->Type == DxgkPresentTypeColorFill) &&
        Entry->hDestination != 0 &&
        Entry->DestinationIsSharedPrimary)
    {
        DXGKRNL_PRESENT_ENTRY RefreshEntry;
        BOOLEAN RefreshHandled;
        NTSTATUS RefreshStatus;

        RefreshEntry = *Entry;
        RefreshEntry.hSource = Entry->hDestination;
        RefreshEntry.hDestination = Entry->hDestination;
        RefreshEntry.SourceAllocation = Entry->DestinationAllocation;
        RefreshEntry.DestinationAllocation = Entry->DestinationAllocation;
        RefreshEntry.SourceIsSharedPrimary = TRUE;
        RefreshEntry.DestinationIsSharedPrimary = TRUE;
        RefreshEntry.SourceIsSharedShadow = FALSE;
        RefreshEntry.DestinationIsSharedShadow = FALSE;

        RefreshStatus = DxgkpRefreshSharedPrimaryScanout(Adapter, &RefreshEntry, &RefreshHandled);
        if (RefreshHandled)
        {
            TraceSeq = InterlockedIncrement(&g_SharedPrimaryPresentTraceCount);
            if (DxgkpShouldTraceOrdinal(TraceSeq))
            {
                DXGKRNL_TRACE("DxgkpExecuteFullPresent: seq=%ld PresentId=%llu "
                              "refreshed shared-primary immediately status=0x%08lX "
                              "dst=0x%X rect=(%ld,%ld)-(%ld,%ld)\n",
                              TraceSeq,
                              Entry->PresentId,
                              RefreshStatus,
                              Entry->hDestination,
                              Entry->DstRect.left,
                              Entry->DstRect.top,
                              Entry->DstRect.right,
                              Entry->DstRect.bottom);
            }
        }
        if (RefreshHandled && !NT_SUCCESS(RefreshStatus))
        {
            DXGKRNL_WARN("DxgkpExecuteFullPresent: shared-primary destination "
                         "refresh failed 0x%08lX\n",
                         RefreshStatus);
        }
    }

PresentCleanup:
    if (!NT_SUCCESS(Status))
    {
        DXGKRNL_WARN("DxgkpExecuteFullPresent: DxgkDdiPresent returned "
                     "0x%08lX\n", Status);
    }

    if (DmaBuffer != NULL)
        DxgkFreeDmaBuffer(DmaBuffer);
    if (DmaBufferPrivateData != NULL)
        ExFreePoolWithTag(DmaBufferPrivateData, TAG_DXGK_SUBMITDMA);
    if (PresentAllocationList != NULL)
        ExFreePoolWithTag(PresentAllocationList, TAG_DXGK_PRESENT);

    while (PresentBindingReferenceCount != 0)
    {
        NTSTATUS BindingStatus;

        PresentBindingReferenceCount--;
        if (PresentBindingsTracked)
            DxgkVidMmDereferenceLogicalAllocation(PresentBindingReferences[PresentBindingReferenceCount]);
        else
        {
            BindingStatus = DxgkVidMmDestroyPresentBinding(Device, PresentBindingReferences[PresentBindingReferenceCount]);
            if (!NT_SUCCESS(BindingStatus))
                DXGKRNL_WARN("DxgkpExecuteFullPresent: transient binding teardown deferred in VidMm 0x%08lX\n", BindingStatus);
        }
    }

    if (KmdTransaction)
        DxgkEndKmdTransaction(Adapter);
    return Status;
}

/* ========================================================================
 * DxgkpQueuePresent
 *
 * Enqueues a present into the per-VidPnSource FIFO.
 *
 * For display-only adapters with IMMEDIATE flip interval, the present
 * is executed synchronously (bypassing the queue) since there is no
 * hardware flip to synchronise with VSync.
 *
 * IRQL: PASSIVE_LEVEL
 * ====================================================================== */
NTSTATUS
DxgkpQueuePresent(
    _In_  PDXGKRNL_ADAPTER         Adapter,
    _Inout_ PDXGKRNL_PRESENT_ENTRY Entry,
    _Out_ ULONG64                  *OutPresentId)
{
    PDXGKRNL_PRESENT_QUEUE Queue;
    PDXGKRNL_DEVICE_WORK DeviceWork = NULL;
    KIRQL OldIrql;
    NTSTATUS Status;

    PAGED_CODE();

    if (OutPresentId == NULL)
    {
        DxgkpReleasePresentEntry(Entry);
        return STATUS_INVALID_PARAMETER;
    }
    *OutPresentId = 0;

    if (Adapter == NULL || Entry == NULL || Entry->Device == NULL || Entry->Device->Adapter != Adapter || (Entry->Context != NULL && Entry->Context->Device != Entry->Device))
    {
        DxgkpReleasePresentEntry(Entry);
        return STATUS_INVALID_PARAMETER;
    }
    if ((Entry->hSource != 0 && (Entry->SourceAllocation == NULL || Entry->SourceAllocation->Adapter != Adapter)) || (Entry->hDestination != 0 && (Entry->DestinationAllocation == NULL || Entry->DestinationAllocation->Adapter != Adapter)))
    {
        DxgkpReleasePresentEntry(Entry);
        return STATUS_INVALID_HANDLE;
    }
    if (InterlockedCompareExchange(&Entry->Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Entry->Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE || (Entry->Context != NULL && InterlockedCompareExchange(&Entry->Context->Destroying, 0, 0) != 0))
    {
        DxgkpReleasePresentEntry(Entry);
        return STATUS_DEVICE_REMOVED;
    }

    if (!DxgkpAcquirePresentQueues(Adapter))
    {
        DxgkpReleasePresentEntry(Entry);
        return STATUS_DELETE_PENDING;
    }

    if (Adapter->State != DxgkAdapterStateStarted || InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0)
    {
        DxgkpReleasePresentQueues(Adapter);
        DxgkpReleasePresentEntry(Entry);
        return STATUS_DELETE_PENDING;
    }

    if (Adapter->PresentQueues == NULL || Adapter->PresentQueueCount == 0)
    {
        DXGKRNL_ERR("DxgkpQueuePresent: no present queues initialised\n");
        DxgkpReleasePresentQueues(Adapter);
        DxgkpReleasePresentEntry(Entry);
        return STATUS_DEVICE_NOT_READY;
    }

    /* Validate VidPn source index. */
    if (Entry->VidPnSourceId >= Adapter->PresentQueueCount)
    {
        DXGKRNL_ERR("DxgkpQueuePresent: VidPnSourceId %u >= queue count %lu\n",
                    Entry->VidPnSourceId, Adapter->PresentQueueCount);
        DxgkpReleasePresentQueues(Adapter);
        DxgkpReleasePresentEntry(Entry);
        return STATUS_INVALID_PARAMETER;
    }

    Queue = &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[Entry->VidPnSourceId];
    Status = DxgkDeviceWorkCreate(Entry->Device, &DeviceWork);
    if (!NT_SUCCESS(Status))
    {
        DxgkpReleasePresentQueues(Adapter);
        DxgkpReleasePresentEntry(Entry);
        return Status;
    }

    /*
     * For display-only adapters, execute the present immediately and
     * inline — there is no hardware flip, so queueing adds latency
     * without benefit.  The periodic timer in display.c still runs as a
     * fallback for any pixels not pushed via the present queue.
     */
    if (Adapter->MiniportContext->IsDisplayOnlyDriver &&
        Entry->FlipInterval == D3DDDI_FLIPINTERVAL_IMMEDIATE)
    {
        /* Assign a present ID even for immediate presents. */
        if (InterlockedCompareExchange(&Entry->Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Entry->Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE || (Entry->Context != NULL && InterlockedCompareExchange(&Entry->Context->Destroying, 0, 0) != 0))
        {
            DxgkDeviceWorkDestroy(DeviceWork);
            DxgkpReleasePresentQueues(Adapter);
            DxgkpReleasePresentEntry(Entry);
            return STATUS_DEVICE_REMOVED;
        }
        Status = DxgkDeviceWorkActivate(DeviceWork);
        if (!NT_SUCCESS(Status))
        {
            DxgkDeviceWorkDestroy(DeviceWork);
            DxgkpReleasePresentQueues(Adapter);
            DxgkpReleasePresentEntry(Entry);
            return Status;
        }
        Entry->DeviceWork = DeviceWork;
        DeviceWork = NULL;
        Entry->PresentId = InterlockedIncrement64(&Queue->NextPresentId);
        *OutPresentId = Entry->PresentId;

        Status = DxgkpExecuteDodPresent(Adapter, Entry);
        DxgkpReleasePresentEntry(Entry);
        DxgkpReleasePresentQueues(Adapter);
        return Status;
    }

    /* --- Enqueue into the circular FIFO --------------------------------- */

    if (!DxgkPresentLimitCoreTryReserve(&Entry->Device->PresentLimit))
    {
        DxgkDeviceWorkDestroy(DeviceWork);
        DxgkpReleasePresentQueues(Adapter);
        DxgkpReleasePresentEntry(Entry);
        return STATUS_DEVICE_BUSY;
    }
    Entry->PresentLimitReservationOwned = TRUE;

    KeAcquireSpinLock(&Queue->QueueLock, &OldIrql);

    if (Queue->Count >= DXGKRNL_PRESENT_QUEUE_DEPTH)
    {
        KeReleaseSpinLock(&Queue->QueueLock, OldIrql);
        DXGKRNL_WARN("DxgkpQueuePresent: queue full (VidPnSrc=%u depth=%u)\n",
                     Entry->VidPnSourceId, DXGKRNL_PRESENT_QUEUE_DEPTH);
        DxgkDeviceWorkDestroy(DeviceWork);
        DxgkpReleasePresentQueues(Adapter);
        DxgkpReleasePresentEntry(Entry);
        return STATUS_DEVICE_BUSY;
    }

    if (InterlockedCompareExchange(&Entry->Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Entry->Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE || (Entry->Context != NULL && InterlockedCompareExchange(&Entry->Context->Destroying, 0, 0) != 0))
    {
        KeReleaseSpinLock(&Queue->QueueLock, OldIrql);
        DxgkDeviceWorkDestroy(DeviceWork);
        DxgkpReleasePresentQueues(Adapter);
        DxgkpReleasePresentEntry(Entry);
        return STATUS_DEVICE_REMOVED;
    }

    Status = DxgkDeviceWorkActivate(DeviceWork);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseSpinLock(&Queue->QueueLock, OldIrql);
        DxgkDeviceWorkDestroy(DeviceWork);
        DxgkpReleasePresentQueues(Adapter);
        DxgkpReleasePresentEntry(Entry);
        return Status;
    }
    Entry->DeviceWork = DeviceWork;
    DeviceWork = NULL;

    /* Assign a present ID. */
    Entry->PresentId = InterlockedIncrement64(&Queue->NextPresentId);
    *OutPresentId = Entry->PresentId;

    /* Copy the entry into the FIFO slot. */
    Queue->Entries[Queue->Tail] = *Entry;
    Entry->Context = NULL;
    Entry->Device = NULL;
    Entry->DeviceWork = NULL;
    Entry->PresentLimitReservationOwned = FALSE;
    Entry->SourceAllocation = NULL;
    Entry->DestinationAllocation = NULL;
    Entry->SourceOpenBindingReference = NULL;
    Entry->DestinationOpenBindingReference = NULL;
    Entry->SharedSurface.Adapter = NULL;
    Entry->SharedSurface.PrimaryAllocation = NULL;
    Entry->SharedSurface.ShadowAllocation = NULL;
    Entry->SharedSurface.RundownHeld = FALSE;
    Entry->DstSubRects = NULL;
    Entry->DstSubRectCount = 0;
    Queue->Tail = (Queue->Tail + 1) % DXGKRNL_PRESENT_QUEUE_DEPTH;
    Queue->Count++;

    KeReleaseSpinLock(&Queue->QueueLock, OldIrql);

    /*
     * For immediate flip interval (non-DOD), execute right away rather
     * than waiting for VSync.  This allows full WDDM adapters to present
     * without tearing protection when the application requests it.
     */
    if (Entry->FlipInterval == D3DDDI_FLIPINTERVAL_IMMEDIATE)
    {
        NTSTATUS Status;

        Status = DxgkpProcessPresentQueue(Adapter, Entry->VidPnSourceId);
        if (!NT_SUCCESS(Status))
        {
            DxgkpReleasePresentQueues(Adapter);
            return Status;
        }
    }

    DxgkpReleasePresentQueues(Adapter);
    return STATUS_SUCCESS;
}

/* ========================================================================
 * DxgkpProcessPresentQueue
 *
 * Dequeues and executes the head present from the specified queue.
 * For VSync-synchronized presents, checks that enough VBlanks have
 * elapsed before executing.
 *
 * IRQL: PASSIVE_LEVEL (called from work-item or direct call)
 * ====================================================================== */
NTSTATUS
DxgkpProcessPresentQueue(
    _In_ PDXGKRNL_ADAPTER                  Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    VidPnSourceId)
{
    PDXGKRNL_PRESENT_QUEUE Queue;
    DXGKRNL_PRESENT_ENTRY  Entry;
    KIRQL OldIrql;
    NTSTATUS Status;
    PAGED_CODE();

    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;

    if (!DxgkpAcquirePresentQueues(Adapter))
        return STATUS_DELETE_PENDING;

    if (InterlockedCompareExchange(&Adapter->SubmitDmaStopping, 0, 0) != 0)
    {
        DxgkpReleasePresentQueues(Adapter);
        return STATUS_DELETE_PENDING;
    }

    if (Adapter->PresentQueues == NULL)
    {
        DxgkpReleasePresentQueues(Adapter);
        return STATUS_INVALID_PARAMETER;
    }

    if (VidPnSourceId >= Adapter->PresentQueueCount)
    {
        DxgkpReleasePresentQueues(Adapter);
        return STATUS_INVALID_PARAMETER;
    }

    Queue = &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[VidPnSourceId];

    /* --- Dequeue the head entry under the spinlock ---------------------- */

    KeAcquireSpinLock(&Queue->QueueLock, &OldIrql);

    if (Queue->Count == 0)
    {
        KeReleaseSpinLock(&Queue->QueueLock, OldIrql);
        DxgkpReleasePresentQueues(Adapter);
        return STATUS_NO_MORE_ENTRIES;
    }

    /*
     * VSync gate: for flip presents with FlipInterval > 0, check that
     * at least FlipInterval VBlanks have elapsed since the last present.
     */
    {
        PDXGKRNL_PRESENT_ENTRY Head = &Queue->Entries[Queue->Head];

        if (Head->FlipInterval > D3DDDI_FLIPINTERVAL_IMMEDIATE)
        {
            LONG64 TargetVBlank = Queue->LastPresentVBlank +
                                  (LONG64)Head->FlipInterval;

            if (Queue->VBlankCount < TargetVBlank)
            {
                /* Not time yet — leave the entry queued. */
                KeReleaseSpinLock(&Queue->QueueLock, OldIrql);
                DxgkpReleasePresentQueues(Adapter);
                return STATUS_PENDING;
            }
        }
    }

    /* Copy the entry out and advance the head pointer. */
    Entry = Queue->Entries[Queue->Head];
    RtlZeroMemory(&Queue->Entries[Queue->Head], sizeof(Queue->Entries[Queue->Head]));
    Queue->Head = (Queue->Head + 1) % DXGKRNL_PRESENT_QUEUE_DEPTH;
    Queue->Count--;
    Queue->LastPresentVBlank = Queue->VBlankCount;

    KeReleaseSpinLock(&Queue->QueueLock, OldIrql);

    /* --- Execute the present ------------------------------------------- */

    if (Entry.Device == NULL || InterlockedCompareExchange(&Entry.Device->Destroying, 0, 0) != 0 || InterlockedCompareExchange(&Entry.Device->ExecutionState, 0, 0) != D3DKMT_DEVICEEXECUTION_ACTIVE || (Entry.Context != NULL && InterlockedCompareExchange(&Entry.Context->Destroying, 0, 0) != 0))
    {
        Status = STATUS_DEVICE_REMOVED;
    }
    else if (Adapter->MiniportContext->IsDisplayOnlyDriver)
    {
        Status = DxgkpExecuteDodPresent(Adapter, &Entry);
    }
    else
    {
        Status = DxgkpExecuteFullPresent(Adapter, &Entry);

        /*
         * If the full WDDM present path is not supported (e.g., the
         * miniport has no DxgkDdiPresent), fall back to the DOD path
         * if a shadow framebuffer is available.
         */
        if (Status == STATUS_NOT_SUPPORTED && Entry.SharedSurface.RundownHeld && Entry.SharedSurface.ShadowFb != NULL)
        {
            Status = DxgkpExecuteDodPresent(Adapter, &Entry);
        }
    }

    if (NT_SUCCESS(Status))
        InterlockedIncrement(&Queue->PresentedFrameCount);

    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED)
    {
        DXGKRNL_WARN("DxgkpProcessPresentQueue: present execution "
                     "returned 0x%08lX\n", Status);
    }

    DxgkpReleasePresentEntry(&Entry);

    DxgkpReleasePresentQueues(Adapter);
    return Status;
}

/* ========================================================================
 * DxgkpNotifyVSync
 *
 * Called from the adapter DPC to record a source-specific CRTC_VSYNC pulse.
 * The queued worker owns the active-call reference until it exits, preventing
 * teardown from freeing the queue while the dynamic work item is queued or
 * running.
 *
 * IRQL: DISPATCH_LEVEL (ISR DPC context)
 * ====================================================================== */
VOID
DxgkpNotifyVSync(
    _In_ PDXGKRNL_ADAPTER                  Adapter,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID    VidPnSourceId)
{
    PDXGKRNL_PRESENT_QUEUE Queue;
    PDXGKRNL_VSYNC_WORK Work;
    KIRQL OldIrql;
    BOOLEAN HasEntries;

    if (Adapter == NULL)
        return;

    if (!DxgkpAcquirePresentQueues(Adapter))
        return;

    if (Adapter->PresentQueues == NULL)
    {
        DxgkpReleasePresentQueues(Adapter);
        return;
    }

    if (VidPnSourceId >= Adapter->PresentQueueCount)
    {
        DxgkpReleasePresentQueues(Adapter);
        return;
    }

    Queue = &((PDXGKRNL_PRESENT_QUEUE)Adapter->PresentQueues)[VidPnSourceId];
    InterlockedIncrement64(&Queue->VBlankCount);
    DxgkpSignalQueueVBlankWaiters(Queue, FALSE);

    KeAcquireSpinLock(&Queue->QueueLock, &OldIrql);
    HasEntries = Queue->Count != 0;
    KeReleaseSpinLock(&Queue->QueueLock, OldIrql);
    /* A periodic monitored fence owes an advance on every pulse even when no
     * present is queued, and that advance runs at PASSIVE_LEVEL. */
    if (!HasEntries && !DxgkSyncHasPeriodicFences())
    {
        DxgkpReleasePresentQueues(Adapter);
        return;
    }

    InterlockedIncrement(&Queue->PendingVBlanks);
    if (InterlockedCompareExchange(&Queue->VSyncWorkQueued, 1, 0) == 0)
    {
        Work = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Work), TAG_DXGK_PRESENT);
        if (Work == NULL)
        {
            InterlockedExchange(&Queue->VSyncWorkQueued, 0);
            DxgkpReleasePresentQueues(Adapter);
            return;
        }
        Work->Queue = Queue;
        ExInitializeWorkItem(&Work->WorkItem, DxgkpVSyncWorker, Work);
        ExQueueWorkItem(&Work->WorkItem, DelayedWorkQueue);
        return;
    }
    DxgkpReleasePresentQueues(Adapter);
}

static VOID
NTAPI
DxgkpVSyncWorker(
    _In_ PVOID Context)
{
    PDXGKRNL_VSYNC_WORK Work = Context;
    PDXGKRNL_PRESENT_QUEUE Queue;
    PDXGKRNL_ADAPTER Adapter;

    ASSERT(Work != NULL && Work->Queue != NULL && Work->Queue->Adapter != NULL);
    Queue = Work->Queue;
    Adapter = Queue->Adapter;

    for (;;)
    {
        LONG Pulses = InterlockedExchange(&Queue->PendingVBlanks, 0);

        while (Pulses-- > 0)
        {
            NTSTATUS Status;

            /*
             * PendingVBlanks deliberately coalesces DPCs into one worker.
             * A periodic monitored fence is a pulse counter, so advance it
             * once for every coalesced VBlank rather than once per worker.
             */
            DxgkSyncAdvancePeriodicFences(Adapter, Queue->VidPnSourceId);
            do
            {
                Status = DxgkpProcessPresentQueue(Adapter, Queue->VidPnSourceId);
            } while (Status != STATUS_PENDING && Status != STATUS_NO_MORE_ENTRIES && Status != STATUS_DELETE_PENDING);
        }

        if (InterlockedCompareExchange(&Queue->PendingVBlanks, 0, 0) != 0)
            continue;

        InterlockedExchange(&Queue->VSyncWorkQueued, 0);
        KeMemoryBarrier();
        if (InterlockedCompareExchange(&Queue->PendingVBlanks, 0, 0) == 0)
            break;
        if (InterlockedCompareExchange(&Queue->VSyncWorkQueued, 1, 0) != 0)
            break;
    }

    ExFreePoolWithTag(Work, TAG_DXGK_PRESENT);
    DxgkpReleasePresentQueues(Adapter);
}

/* EOF */
