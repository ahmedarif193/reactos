/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgmms2 submission-fence timeline contract tests
 */

#include <kmt_test.h>
#include <reactos/drivers/directx/dxgmms2.h>
#include "timeline_core.h"

#define TAG_DXGMMS2_TIMELINE_TEST 'tT2D'
#define DXGMMS2_RESERVE_RELEASE_ITERATIONS 512
#define DXGMMS2_RELEASE_ATTEMPTS 16384
#define DXGMMS2_MAINTENANCE_RESETS 128
#define DXGMMS2_MAINTENANCE_HOT_CALLS_PER_RESET 1024

typedef struct _DXGMMS2_RESERVE_RELEASE_STRESS
{
    PDXGMMS2_TIMELINE_CONTEXT Timeline;
    ULONG Generation;
    KEVENT ReserveStartEvent;
    KEVENT ReleaseStartEvent;
    KEVENT ReserveDoneEvent;
    KEVENT ReleaseDoneEvent;
    volatile LONG StopRequested;
    volatile LONG CurrentFence;
    volatile LONG ReserveResult;
    volatile LONG ReleaseResult;
    volatile LONG ReserveFailures;
    volatile LONG ReleaseFailures;
    volatile LONG ReserveOperations;
    volatile LONG ReleaseOperations;
} DXGMMS2_RESERVE_RELEASE_STRESS, *PDXGMMS2_RESERVE_RELEASE_STRESS;

typedef struct _DXGMMS2_MAINTENANCE_STRESS
{
    PDXGMMS2_TIMELINE_CONTEXT Timeline;
    ULONG Generation;
    ULONG Fence;
    KEVENT ReaderStartEvent;
    KEVENT ReleaserStartEvent;
    KEVENT ReaderStartedEvent;
    KEVENT ReleaserStartedEvent;
    KEVENT ReaderDoneEvent;
    KEVENT ReleaserDoneEvent;
    volatile LONG StopRequested;
    volatile LONG ReadOperations;
    volatile LONG ReleaseOperations;
    volatile LONG SuccessfulReleases;
} DXGMMS2_MAINTENANCE_STRESS, *PDXGMMS2_MAINTENANCE_STRESS;

static NTSTATUS WaitForStressObject(_In_ HANDLE Handle)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10LL * 1000LL * 1000LL * 10LL;
    return ZwWaitForSingleObject(Handle, FALSE, &Timeout);
}

static NTSTATUS WaitForStressEvent(_In_ PKEVENT Event)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10LL * 1000LL * 1000LL * 10LL;
    return KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, &Timeout);
}

static VOID NTAPI ReserveStressThread(_In_ PVOID Parameter)
{
    PDXGMMS2_RESERVE_RELEASE_STRESS Stress = Parameter;

    for (;;)
    {
        (VOID)KeWaitForSingleObject(&Stress->ReserveStartEvent, Executive, KernelMode, FALSE, NULL);
        if (InterlockedCompareExchange(&Stress->StopRequested, 0, 0) != 0)
            break;
        if (Dxgmms2TimelineReserveFence(Stress->Timeline, Stress->Generation, 0, (ULONG)InterlockedCompareExchange(&Stress->CurrentFence, 0, 0)))
            InterlockedExchange(&Stress->ReserveResult, 1);
        else
        {
            InterlockedExchange(&Stress->ReserveResult, 0);
            InterlockedIncrement(&Stress->ReserveFailures);
        }
        InterlockedIncrement(&Stress->ReserveOperations);
        KeSetEvent(&Stress->ReserveDoneEvent, IO_NO_INCREMENT, FALSE);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID NTAPI ReleaseStressThread(_In_ PVOID Parameter)
{
    PDXGMMS2_RESERVE_RELEASE_STRESS Stress = Parameter;
    LARGE_INTEGER Delay;

    Delay.QuadPart = -10000;
    for (;;)
    {
        ULONG Attempt;
        ULONG Fence;
        BOOLEAN Released = FALSE;

        (VOID)KeWaitForSingleObject(&Stress->ReleaseStartEvent, Executive, KernelMode, FALSE, NULL);
        if (InterlockedCompareExchange(&Stress->StopRequested, 0, 0) != 0)
            break;
        Fence = (ULONG)InterlockedCompareExchange(&Stress->CurrentFence, 0, 0);
        for (Attempt = 0; Attempt < DXGMMS2_RELEASE_ATTEMPTS; ++Attempt)
        {
            if (Dxgmms2TimelineReleaseFence(Stress->Timeline, Stress->Generation, 0, Fence))
            {
                Released = TRUE;
                break;
            }
            if ((Attempt & 31) == 31)
                (VOID)KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        }
        if (Released)
            InterlockedExchange(&Stress->ReleaseResult, 1);
        else
        {
            InterlockedExchange(&Stress->ReleaseResult, 0);
            InterlockedIncrement(&Stress->ReleaseFailures);
        }
        InterlockedIncrement(&Stress->ReleaseOperations);
        KeSetEvent(&Stress->ReleaseDoneEvent, IO_NO_INCREMENT, FALSE);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID NTAPI MaintenanceReaderThread(_In_ PVOID Parameter)
{
    PDXGMMS2_MAINTENANCE_STRESS Stress = Parameter;
    ULONG Cycle;

    for (Cycle = 0; Cycle < DXGMMS2_MAINTENANCE_RESETS; ++Cycle)
    {
        ULONG Iteration;

        (VOID)KeWaitForSingleObject(&Stress->ReaderStartEvent, Executive, KernelMode, FALSE, NULL);
        if (InterlockedCompareExchange(&Stress->StopRequested, 0, 0) != 0)
            break;
        KeSetEvent(&Stress->ReaderStartedEvent, IO_NO_INCREMENT, FALSE);
        for (Iteration = 0; Iteration < DXGMMS2_MAINTENANCE_HOT_CALLS_PER_RESET; ++Iteration)
        {
            (VOID)Dxgmms2TimelineIsFencePublished(Stress->Timeline, Stress->Generation, 0, Stress->Fence);
            InterlockedIncrement(&Stress->ReadOperations);
        }
        KeSetEvent(&Stress->ReaderDoneEvent, IO_NO_INCREMENT, FALSE);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID NTAPI MaintenanceReleaserThread(_In_ PVOID Parameter)
{
    PDXGMMS2_MAINTENANCE_STRESS Stress = Parameter;
    ULONG Cycle;

    for (Cycle = 0; Cycle < DXGMMS2_MAINTENANCE_RESETS; ++Cycle)
    {
        ULONG Iteration;

        (VOID)KeWaitForSingleObject(&Stress->ReleaserStartEvent, Executive, KernelMode, FALSE, NULL);
        if (InterlockedCompareExchange(&Stress->StopRequested, 0, 0) != 0)
            break;
        KeSetEvent(&Stress->ReleaserStartedEvent, IO_NO_INCREMENT, FALSE);
        for (Iteration = 0; Iteration < DXGMMS2_MAINTENANCE_HOT_CALLS_PER_RESET; ++Iteration)
        {
            if (Dxgmms2TimelineReleaseFence(Stress->Timeline, Stress->Generation, 0, Stress->Fence))
                InterlockedIncrement(&Stress->SuccessfulReleases);
            InterlockedIncrement(&Stress->ReleaseOperations);
        }
        KeSetEvent(&Stress->ReleaserDoneEvent, IO_NO_INCREMENT, FALSE);
    }
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID TestReserveReleaseStress(VOID)
{
    PDXGMMS2_RESERVE_RELEASE_STRESS Stress;
    PDXGMMS2_TIMELINE_CONTEXT Timeline;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE ReserveThread = NULL;
    HANDLE ReleaseThread = NULL;
    ULONG Generation;
    ULONG Iteration;
    LONG ControllerFailures = 0;
    NTSTATUS ReserveWaitStatus = STATUS_UNSUCCESSFUL;
    NTSTATUS ReleaseWaitStatus = STATUS_UNSUCCESSFUL;
    NTSTATUS Status;

    Timeline = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Timeline), TAG_DXGMMS2_TIMELINE_TEST);
    Stress = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Stress), TAG_DXGMMS2_TIMELINE_TEST);
    ok(Timeline != NULL && Stress != NULL, "reserve/release stress allocation failed\n");
    if (Timeline == NULL || Stress == NULL)
        goto CleanupAllocations;
    RtlZeroMemory(Stress, sizeof(*Stress));
    Dxgmms2TimelineInitialize(Timeline);
    Status = Dxgmms2TimelineStart(Timeline, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto CleanupAllocations;
    Generation = (ULONG)InterlockedCompareExchange(&Timeline->Generation, 0, 0);
    Stress->Timeline = Timeline;
    Stress->Generation = Generation;
    KeInitializeEvent(&Stress->ReserveStartEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Stress->ReleaseStartEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Stress->ReserveDoneEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Stress->ReleaseDoneEvent, SynchronizationEvent, FALSE);
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&ReserveThread, THREAD_ALL_ACCESS, &Attributes, NULL, NULL, ReserveStressThread, Stress);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto StopTimeline;
    Status = PsCreateSystemThread(&ReleaseThread, THREAD_ALL_ACCESS, &Attributes, NULL, NULL, ReleaseStressThread, Stress);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto StopThreads;

    for (Iteration = 0; Iteration < DXGMMS2_RESERVE_RELEASE_ITERATIONS; ++Iteration)
    {
        InterlockedExchange(&Stress->CurrentFence, (LONG)(Iteration + 1));
        InterlockedExchange(&Stress->ReserveResult, -1);
        InterlockedExchange(&Stress->ReleaseResult, -1);
        KeSetEvent(&Stress->ReleaseStartEvent, IO_NO_INCREMENT, FALSE);
        KeSetEvent(&Stress->ReserveStartEvent, IO_NO_INCREMENT, FALSE);
        Status = WaitForStressEvent(&Stress->ReserveDoneEvent);
        if (!NT_SUCCESS(Status))
        {
            ++ControllerFailures;
            break;
        }
        Status = WaitForStressEvent(&Stress->ReleaseDoneEvent);
        if (!NT_SUCCESS(Status))
        {
            ++ControllerFailures;
            break;
        }
        if (InterlockedCompareExchange(&Stress->ReserveResult, 0, 0) != 1 || InterlockedCompareExchange(&Stress->ReleaseResult, 0, 0) != 1 || InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0) != 0)
            ++ControllerFailures;
    }

StopThreads:
    InterlockedExchange(&Stress->StopRequested, 1);
    KeSetEvent(&Stress->ReserveStartEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Stress->ReleaseStartEvent, IO_NO_INCREMENT, FALSE);
    if (ReserveThread != NULL)
    {
        ReserveWaitStatus = WaitForStressObject(ReserveThread);
        ok_eq_hex(ReserveWaitStatus, STATUS_SUCCESS);
        (VOID)ZwClose(ReserveThread);
    }
    if (ReleaseThread != NULL)
    {
        ReleaseWaitStatus = WaitForStressObject(ReleaseThread);
        ok_eq_hex(ReleaseWaitStatus, STATUS_SUCCESS);
        (VOID)ZwClose(ReleaseThread);
    }
    if ((ReserveThread != NULL && !NT_SUCCESS(ReserveWaitStatus)) || (ReleaseThread != NULL && !NT_SUCCESS(ReleaseWaitStatus)))
        goto CleanupAllocations;
    ok_eq_long(ControllerFailures, 0);
    ok_eq_long(InterlockedCompareExchange(&Stress->ReserveFailures, 0, 0), 0);
    ok_eq_long(InterlockedCompareExchange(&Stress->ReleaseFailures, 0, 0), 0);
    if (ReserveThread != NULL && ReleaseThread != NULL)
    {
        ok_eq_long(InterlockedCompareExchange(&Stress->ReserveOperations, 0, 0), DXGMMS2_RESERVE_RELEASE_ITERATIONS);
        ok_eq_long(InterlockedCompareExchange(&Stress->ReleaseOperations, 0, 0), DXGMMS2_RESERVE_RELEASE_ITERATIONS);
    }
    if (InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0) != 0)
    {
        Status = Dxgmms2TimelineResetFenceIdentities(Timeline, Generation);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
    ok_eq_long(InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0), 0);

StopTimeline:
    Status = Dxgmms2TimelineBeginStop(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2TimelineCompleteStop(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2TimelinePrepareDestroy(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);

CleanupAllocations:
    if (Stress != NULL && (ReserveThread == NULL || NT_SUCCESS(ReserveWaitStatus)) && (ReleaseThread == NULL || NT_SUCCESS(ReleaseWaitStatus)))
        ExFreePoolWithTag(Stress, TAG_DXGMMS2_TIMELINE_TEST);
    if (Timeline != NULL && (ReserveThread == NULL || NT_SUCCESS(ReserveWaitStatus)) && (ReleaseThread == NULL || NT_SUCCESS(ReleaseWaitStatus)))
        ExFreePoolWithTag(Timeline, TAG_DXGMMS2_TIMELINE_TEST);
}

static VOID TestMaintenanceDrainStress(VOID)
{
    PDXGMMS2_MAINTENANCE_STRESS Stress;
    PDXGMMS2_TIMELINE_CONTEXT Timeline;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE ReaderThread = NULL;
    HANDLE ReleaserThread = NULL;
    ULONG Generation;
    ULONG Iteration;
    LONG ResetFailures = 0;
    NTSTATUS ReaderWaitStatus = STATUS_UNSUCCESSFUL;
    NTSTATUS ReleaserWaitStatus = STATUS_UNSUCCESSFUL;
    NTSTATUS Status;

    Timeline = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Timeline), TAG_DXGMMS2_TIMELINE_TEST);
    Stress = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Stress), TAG_DXGMMS2_TIMELINE_TEST);
    ok(Timeline != NULL && Stress != NULL, "maintenance stress allocation failed\n");
    if (Timeline == NULL || Stress == NULL)
        goto CleanupAllocations;
    RtlZeroMemory(Stress, sizeof(*Stress));
    Dxgmms2TimelineInitialize(Timeline);
    Status = Dxgmms2TimelineStart(Timeline, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto CleanupAllocations;
    Generation = (ULONG)InterlockedCompareExchange(&Timeline->Generation, 0, 0);
    Stress->Timeline = Timeline;
    Stress->Generation = Generation;
    Stress->Fence = Dxgmms2TimelineAllocateFence(Timeline, Generation);
    ok(Stress->Fence != 0, "maintenance stress fence allocation failed\n");
    ok_bool_true(Dxgmms2TimelineReserveFence(Timeline, Generation, 0, Stress->Fence), "maintenance stress reserve");
    ok_bool_true(Dxgmms2TimelinePublishFence(Timeline, Generation, 0, Stress->Fence), "maintenance stress publish");
    KeInitializeEvent(&Stress->ReaderStartEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Stress->ReleaserStartEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Stress->ReaderStartedEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Stress->ReleaserStartedEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Stress->ReaderDoneEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&Stress->ReleaserDoneEvent, SynchronizationEvent, FALSE);
    InitializeObjectAttributes(&Attributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    Status = PsCreateSystemThread(&ReaderThread, THREAD_ALL_ACCESS, &Attributes, NULL, NULL, MaintenanceReaderThread, Stress);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto StopTimeline;
    Status = PsCreateSystemThread(&ReleaserThread, THREAD_ALL_ACCESS, &Attributes, NULL, NULL, MaintenanceReleaserThread, Stress);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto StopThreads;
    for (Iteration = 0; Iteration < DXGMMS2_MAINTENANCE_RESETS; ++Iteration)
    {
        KeSetEvent(&Stress->ReaderStartEvent, IO_NO_INCREMENT, FALSE);
        KeSetEvent(&Stress->ReleaserStartEvent, IO_NO_INCREMENT, FALSE);
        Status = WaitForStressEvent(&Stress->ReaderStartedEvent);
        if (!NT_SUCCESS(Status))
        {
            ++ResetFailures;
            break;
        }
        Status = WaitForStressEvent(&Stress->ReleaserStartedEvent);
        if (!NT_SUCCESS(Status))
        {
            ++ResetFailures;
            break;
        }
        Status = Dxgmms2TimelineResetFenceIdentities(Timeline, Generation);
        if (!NT_SUCCESS(Status))
            ++ResetFailures;
        Status = WaitForStressEvent(&Stress->ReaderDoneEvent);
        if (!NT_SUCCESS(Status))
        {
            ++ResetFailures;
            break;
        }
        Status = WaitForStressEvent(&Stress->ReleaserDoneEvent);
        if (!NT_SUCCESS(Status))
        {
            ++ResetFailures;
            break;
        }
    }

StopThreads:
    InterlockedExchange(&Stress->StopRequested, 1);
    KeSetEvent(&Stress->ReaderStartEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Stress->ReleaserStartEvent, IO_NO_INCREMENT, FALSE);
    if (ReaderThread != NULL)
    {
        ReaderWaitStatus = WaitForStressObject(ReaderThread);
        ok_eq_hex(ReaderWaitStatus, STATUS_SUCCESS);
        (VOID)ZwClose(ReaderThread);
    }
    if (ReleaserThread != NULL)
    {
        ReleaserWaitStatus = WaitForStressObject(ReleaserThread);
        ok_eq_hex(ReleaserWaitStatus, STATUS_SUCCESS);
        (VOID)ZwClose(ReleaserThread);
    }
    if ((ReaderThread != NULL && !NT_SUCCESS(ReaderWaitStatus)) || (ReleaserThread != NULL && !NT_SUCCESS(ReleaserWaitStatus)))
        goto CleanupAllocations;
    ok_eq_long(ResetFailures, 0);
    if (ReaderThread != NULL && ReleaserThread != NULL)
    {
        ok_eq_long(InterlockedCompareExchange(&Stress->ReadOperations, 0, 0), DXGMMS2_MAINTENANCE_RESETS * DXGMMS2_MAINTENANCE_HOT_CALLS_PER_RESET);
        ok_eq_long(InterlockedCompareExchange(&Stress->ReleaseOperations, 0, 0), DXGMMS2_MAINTENANCE_RESETS * DXGMMS2_MAINTENANCE_HOT_CALLS_PER_RESET);
    }
    ok(InterlockedCompareExchange(&Stress->SuccessfulReleases, 0, 0) <= 1, "maintenance release succeeded more than once\n");
    ok_eq_long(InterlockedCompareExchange(&Timeline->ActiveFastCalls, 0, 0), 0);
    ok_eq_long(InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0), 0);
    ok_eq_long(InterlockedCompareExchange(&Timeline->FastCallsOpen, 0, 0), 1);

StopTimeline:
    Status = Dxgmms2TimelineBeginStop(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2TimelineCompleteStop(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2TimelinePrepareDestroy(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);

CleanupAllocations:
    if (Stress != NULL && (ReaderThread == NULL || NT_SUCCESS(ReaderWaitStatus)) && (ReleaserThread == NULL || NT_SUCCESS(ReleaserWaitStatus)))
        ExFreePoolWithTag(Stress, TAG_DXGMMS2_TIMELINE_TEST);
    if (Timeline != NULL && (ReaderThread == NULL || NT_SUCCESS(ReaderWaitStatus)) && (ReleaserThread == NULL || NT_SUCCESS(ReleaserWaitStatus)))
        ExFreePoolWithTag(Timeline, TAG_DXGMMS2_TIMELINE_TEST);
}

static VOID InitializeSnapshot(_Out_ DXGMMS2_FENCE_SNAPSHOT_V1 *Snapshot)
{
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    Snapshot->Size = DXGMMS2_FENCE_SNAPSHOT_V1_SIZE;
    Snapshot->Version = DXGMMS2_SCHEDULER_TIMELINE_VERSION_1;
}

START_TEST(Dxgmms2Timeline)
{
    PDXGMMS2_TIMELINE_CONTEXT Timeline;
    DXGMMS2_FENCE_SNAPSHOT_V1 Snapshot;
    ULONG FirstGeneration;
    ULONG SecondGeneration;
    ULONG Fence;
    ULONG PendingFence;
    NTSTATUS Status;

    Timeline = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Timeline), TAG_DXGMMS2_TIMELINE_TEST);
    ok(Timeline != NULL, "timeline allocation failed\n");
    if (Timeline == NULL)
        return;

    Dxgmms2TimelineInitialize(Timeline);
    ok_eq_long(InterlockedCompareExchange(&Timeline->State, 0, 0), Dxgmms2TimelineCreated);
    ok_eq_long(InterlockedCompareExchange(&Timeline->FastCallsOpen, 0, 0), 0);

    Status = Dxgmms2TimelineStart(Timeline, 2);
    ok_eq_hex(Status, STATUS_SUCCESS);
    FirstGeneration = (ULONG)InterlockedCompareExchange(&Timeline->Generation, 0, 0);
    ok(FirstGeneration != 0, "first generation must be nonzero\n");
    ok_eq_long(InterlockedCompareExchange(&Timeline->State, 0, 0), Dxgmms2TimelineActive);
    ok_eq_long(InterlockedCompareExchange(&Timeline->FastCallsOpen, 0, 0), 1);

    Fence = Dxgmms2TimelineAllocateFence(Timeline, FirstGeneration);
    ok(Fence != 0, "fence allocation failed\n");
    ok_bool_true(Dxgmms2TimelineReserveFence(Timeline, FirstGeneration, 0, Fence), "reserve first fence");
    ok_eq_long(InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0), 1);
    ok_bool_true(Dxgmms2TimelinePublishFence(Timeline, FirstGeneration, 0, Fence), "publish first fence");

    InitializeSnapshot(&Snapshot);
    Snapshot.Version = DXGMMS2_SCHEDULER_TIMELINE_VERSION_1 + 1;
    Status = Dxgmms2TimelineNotifyFenceCompletion(Timeline, FirstGeneration, 0, Fence, 0, &Snapshot);
    ok_eq_hex(Status, STATUS_REVISION_MISMATCH);
    ok_eq_ulong((ULONG)InterlockedCompareExchange((volatile LONG *)&Timeline->NodeLastCompletedFenceId[0], 0, 0), 0);
    ok_eq_ulong((ULONG)InterlockedCompareExchange((volatile LONG *)&Timeline->LastCompletedFenceId, 0, 0), 0);

    RtlZeroMemory(&Snapshot, sizeof(Snapshot));
    Snapshot.Size = sizeof(Snapshot.Size);
    Status = Dxgmms2TimelineNotifyFenceCompletion(Timeline, FirstGeneration, 0, Fence, 0, &Snapshot);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong((ULONG)InterlockedCompareExchange((volatile LONG *)&Timeline->NodeLastCompletedFenceId[0], 0, 0), 0);
    ok_eq_ulong((ULONG)InterlockedCompareExchange((volatile LONG *)&Timeline->LastCompletedFenceId, 0, 0), 0);

    InitializeSnapshot(&Snapshot);
    Status = Dxgmms2TimelineNotifyFenceCompletion(Timeline, FirstGeneration, 0, Fence, 0, &Snapshot);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Snapshot.LastCompletedFence, Fence);
    ok_eq_ulong(Snapshot.GlobalLastCompletedFence, Fence);
    ok_bool_true(Dxgmms2TimelineReleaseFence(Timeline, FirstGeneration, 0, Fence), "release first fence");
    ok_eq_long(InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0), 0);

    PendingFence = Dxgmms2TimelineAllocateFence(Timeline, FirstGeneration);
    ok(PendingFence != 0, "pending fence allocation failed\n");
    ok_bool_true(Dxgmms2TimelineReserveFence(Timeline, FirstGeneration, 1, PendingFence), "reserve pending fence");
    ok_eq_long(InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0), 1);
    ok_bool_true(Dxgmms2TimelinePublishFence(Timeline, FirstGeneration, 1, PendingFence), "publish pending fence");

    Status = Dxgmms2TimelineBeginStop(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Dxgmms2TimelineAllocateFence(Timeline, FirstGeneration), 0);
    ok_bool_false(Dxgmms2TimelineReserveFence(Timeline, FirstGeneration, 0, PendingFence + 1), "reserve while stopping");
    ok_bool_true(Dxgmms2TimelineIsFencePublished(Timeline, FirstGeneration, 1, PendingFence), "lookup while stopping");
    Status = Dxgmms2TimelineCompleteStop(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(InterlockedCompareExchange(&Timeline->State, 0, 0), Dxgmms2TimelineStopped);
    ok_eq_long(InterlockedCompareExchange(&Timeline->FastCallsOpen, 0, 0), 1);
    ok_bool_true(Dxgmms2TimelineIsFencePublished(Timeline, FirstGeneration, 1, PendingFence), "lookup after complete stop");

    Status = Dxgmms2TimelineStart(Timeline, 2);
    ok_eq_hex(Status, STATUS_DEVICE_BUSY);
    ok_eq_long(InterlockedCompareExchange(&Timeline->State, 0, 0), Dxgmms2TimelineStopped);
    ok_eq_long(InterlockedCompareExchange(&Timeline->FastCallsOpen, 0, 0), 1);
    ok_bool_true(Dxgmms2TimelineReleaseFence(Timeline, FirstGeneration, 1, PendingFence), "release after busy restart");
    ok_eq_long(InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0), 0);

    Status = Dxgmms2TimelineStart(Timeline, 2);
    ok_eq_hex(Status, STATUS_SUCCESS);
    SecondGeneration = (ULONG)InterlockedCompareExchange(&Timeline->Generation, 0, 0);
    ok(SecondGeneration != 0 && SecondGeneration != FirstGeneration, "restart did not advance generation\n");
    InitializeSnapshot(&Snapshot);
    Status = Dxgmms2TimelineQueryFenceSnapshot(Timeline, FirstGeneration, 0, &Snapshot);
    ok_eq_hex(Status, STATUS_INVALID_DEVICE_STATE);

    Fence = Dxgmms2TimelineAllocateFence(Timeline, SecondGeneration);
    ok(Fence != 0, "post-restart fence allocation failed\n");
    ok_bool_true(Dxgmms2TimelineReserveFence(Timeline, SecondGeneration, 0, Fence), "reserve post-restart fence");
    ok_bool_true(Dxgmms2TimelinePublishFence(Timeline, SecondGeneration, 0, Fence), "publish post-restart fence");
    Status = Dxgmms2TimelineResetFenceIdentities(Timeline, SecondGeneration);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(InterlockedCompareExchange(&Timeline->LiveIdentityCount, 0, 0), 0);
    ok_bool_false(Dxgmms2TimelineIsFencePublished(Timeline, SecondGeneration, 0, Fence), "reset removed published identity");
    ok_eq_long(InterlockedCompareExchange(&Timeline->FastCallsOpen, 0, 0), 1);

    PendingFence = Dxgmms2TimelineAllocateFence(Timeline, SecondGeneration);
    ok(PendingFence != 0, "destroy-busy fence allocation failed\n");
    ok_bool_true(Dxgmms2TimelineReserveFence(Timeline, SecondGeneration, 0, PendingFence), "reserve destroy-busy fence");
    ok_bool_true(Dxgmms2TimelinePublishFence(Timeline, SecondGeneration, 0, PendingFence), "publish destroy-busy fence");
    Status = Dxgmms2TimelineBeginStop(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2TimelineCompleteStop(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2TimelinePrepareDestroy(Timeline);
    ok_eq_hex(Status, STATUS_DEVICE_BUSY);
    ok_eq_long(InterlockedCompareExchange(&Timeline->State, 0, 0), Dxgmms2TimelineStopped);
    ok_eq_long(InterlockedCompareExchange(&Timeline->FastCallsOpen, 0, 0), 1);
    ok_bool_true(Dxgmms2TimelineReleaseFence(Timeline, SecondGeneration, 0, PendingFence), "release after busy destroy");
    Status = Dxgmms2TimelinePrepareDestroy(Timeline);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_long(InterlockedCompareExchange(&Timeline->State, 0, 0), Dxgmms2TimelineDestroying);
    ok_eq_long(InterlockedCompareExchange(&Timeline->FastCallsOpen, 0, 0), 0);
    ok_bool_false(Dxgmms2TimelineIsFencePublished(Timeline, SecondGeneration, 0, PendingFence), "lookup after destroy preparation");

    ExFreePoolWithTag(Timeline, TAG_DXGMMS2_TIMELINE_TEST);

    TestReserveReleaseStress();
    TestMaintenanceDrainStress();
}
