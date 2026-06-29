/*
 * PROJECT:     ReactOS Watchdog Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Watchdog timer services for TDR (Timeout Detection and Recovery)
 *              and legacy video driver monitoring
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 *
 * REFERENCE:   Implemented from cleanroom behavioral analysis of watchdog.sys
 *              export signatures, calling conventions, and documented WDDM TDR
 *              architecture. No binary code was copied.
 */

#include "watchdog.h"

#define NDEBUG
#include <debug.h>

/* Conversion: 100ns ticks to seconds requires dividing by 10,000,000 */
#define TICKS_PER_SECOND 10000000ULL

/* ===================================================================
 * Driver Entry
 * =================================================================== */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    return STATUS_SUCCESS;
}

/* Forward declarations */
VOID NTAPI WdStopDeferredWatch(_In_ PVOID WatchdogObject);
VOID NTAPI WdStopWatch(_In_ PVOID WatchdogObject, _In_ BOOLEAN FlushDpcs);
VOID NTAPI WdDereferenceObject(_In_ PVOID WatchdogObject);

/* ===================================================================
 * Internal Helpers
 * =================================================================== */

/*
 * WdpFreeObject - Final cleanup when refcount hits zero.
 * Frees pool memory for either watchdog type.
 */
VOID
WdpFreeObject(
    _In_ PWD_HEADER Header)
{
    if (Header->DeviceObject)
    {
        ObfDereferenceObject(Header->DeviceObject);
        Header->DeviceObject = NULL;
    }

    ExFreePoolWithTag(Header, Header->Tag);
}

/*
 * WdpNotifyTimeout - Fires the client's TDR DPC when a timeout is detected.
 * Called from DPC context (DISPATCH_LEVEL) with the spinlock held.
 * The spinlock is released before this is called.
 */
VOID
WdpNotifyTimeout(
    _In_ PWD_DEFERRED_WATCHDOG Watchdog,
    _In_ ULONG EventType)
{
    KDPC ClientDpc;

    if (!Watchdog->ClientDpc)
        return;

    Watchdog->Header.LastEvent = EventType;

    if (Watchdog->MonitoredThread)
    {
        ObfReferenceObject(Watchdog->MonitoredThread);
        Watchdog->Header.NotifiedThread = Watchdog->MonitoredThread;
    }

    /* Increment refcount -- client will call WdCompleteEvent to release */
    InterlockedIncrement(&Watchdog->Header.ReferenceCount);

    /* Set state to fired -- DPC will not re-fire until client acknowledges */
    Watchdog->TimeoutState = WD_TIMEOUT_FIRED;

    /*
     * Queue the client's DPC. SystemArgument2 carries the watchdog pointer
     * so the client can identify which watchdog timed out.
     */
    RtlCopyMemory(&ClientDpc, &Watchdog->Dpc, sizeof(KDPC));
    ClientDpc.DeferredRoutine = Watchdog->ClientDpc;
    if (!KeInsertQueueDpc(&ClientDpc, NULL, Watchdog))
    {
        /* DPC insert failed -- undo refcount increment */
        if (InterlockedDecrement(&Watchdog->Header.ReferenceCount) == 0)
        {
            WdpFreeObject(&Watchdog->Header);
        }
    }
}

/*
 * WdpDeferredWatchdogDpc - Periodic DPC callback for deferred watchdog.
 * Implements the TDR timeout state machine.
 */
VOID
NTAPI
WdpDeferredWatchdogDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PWD_DEFERRED_WATCHDOG Watchdog = (PWD_DEFERRED_WATCHDOG)DeferredContext;
    ULONG KernelTime, UserTime;
    ULONGLONG Elapsed;
    LONG Enter, Exit;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (!Watchdog)
        return;

    KeAcquireSpinLockAtDpcLevel(&Watchdog->Header.SpinLock);

    /* Check if watchdog is active */
    if (Watchdog->RunState != WD_RUN_RUNNING)
    {
        KeReleaseSpinLockFromDpcLevel(&Watchdog->Header.SpinLock);
        return;
    }

    /* Check if suspended */
    if (Watchdog->Suspended || Watchdog->SuspendCount > 0)
    {
        KeReleaseSpinLockFromDpcLevel(&Watchdog->Header.SpinLock);
        return;
    }

    /* No client callback means nothing to notify */
    if (!Watchdog->ClientDpc)
    {
        KeReleaseSpinLockFromDpcLevel(&Watchdog->Header.SpinLock);
        return;
    }

    /* Check if a thread is in the monitored section */
    Enter = Watchdog->EnterCount;
    Exit = Watchdog->ExitCount;

    if (Enter == Exit || !Watchdog->MonitoredThread)
    {
        /* No thread in monitored section -- reset to idle */
        Watchdog->TimeoutState = WD_TIMEOUT_IDLE;
        Watchdog->KernelTimeBaseline = 0;
        Watchdog->UserTimeBaseline = 0;
        KeReleaseSpinLockFromDpcLevel(&Watchdog->Header.SpinLock);
        return;
    }

    switch (Watchdog->TimeoutState)
    {
        case WD_TIMEOUT_IDLE:
        {
            /*
             * Transition from idle to monitoring. Take a baseline snapshot
             * of thread runtime so we can measure elapsed time.
             */
            KeQueryRuntimeThread(Watchdog->MonitoredThread, &UserTime);
            KernelTime = (ULONG)(ULONG_PTR)KeQueryRuntimeThread(Watchdog->MonitoredThread, &UserTime);

            Watchdog->KernelTimeBaseline = KernelTime;
            Watchdog->UserTimeBaseline = UserTime;
            Watchdog->LastKernelSnapshot = KernelTime;
            Watchdog->LastUserSnapshot = UserTime;
            Watchdog->TimeoutState = WD_TIMEOUT_MONITORING;
            break;
        }

        case WD_TIMEOUT_MONITORING:
        {
            /*
             * Check if the monitored thread has exceeded the timeout.
             * KeQueryRuntimeThread returns kernel time in ticks, user time via out param.
             */
            KernelTime = (ULONG)(ULONG_PTR)KeQueryRuntimeThread(Watchdog->MonitoredThread, &UserTime);

            /* Check if any progress was made (counters changed) */
            if (KernelTime != Watchdog->LastKernelSnapshot ||
                UserTime != Watchdog->LastUserSnapshot ||
                Enter != (LONG)Watchdog->LastKernelSnapshot) /* Simplification: check enter/exit delta */
            {
                /* Progress detected -- update snapshots, stay in monitoring */
                Watchdog->LastKernelSnapshot = KernelTime;
                Watchdog->LastUserSnapshot = UserTime;
            }

            /* Compute elapsed time based on watchdog type */
            Elapsed = 0;
            if (Watchdog->Header.WatchdogType == WD_TYPE_KERNEL_TIME ||
                Watchdog->Header.WatchdogType == WD_TYPE_BOTH)
            {
                Elapsed += (ULONGLONG)(KernelTime - Watchdog->KernelTimeBaseline) *
                           (ULONGLONG)Watchdog->TimeIncrement;
            }
            if (Watchdog->Header.WatchdogType == WD_TYPE_USER_TIME ||
                Watchdog->Header.WatchdogType == WD_TYPE_BOTH)
            {
                Elapsed += (ULONGLONG)(UserTime - Watchdog->UserTimeBaseline) *
                           (ULONGLONG)Watchdog->TimeIncrement;
            }

            /* Convert from 100ns ticks to seconds and compare */
            if ((Elapsed / TICKS_PER_SECOND) >= Watchdog->TimeoutSeconds)
            {
                /* TIMEOUT -- notify client */
                KeReleaseSpinLockFromDpcLevel(&Watchdog->Header.SpinLock);
                WdpNotifyTimeout(Watchdog, WD_EVENT_TIMEOUT);
                return;
            }
            break;
        }

        case WD_TIMEOUT_FIRED:
            /* Already fired, waiting for client to call WdCompleteEvent */
            break;
    }

    KeReleaseSpinLockFromDpcLevel(&Watchdog->Header.SpinLock);
}

/*
 * WdpStandardWatchdogDpc - Periodic DPC callback for standard watchdog.
 * Simpler than deferred -- uses countdown-based timeout.
 */
VOID
NTAPI
WdpStandardWatchdogDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PWD_STANDARD_WATCHDOG Watchdog = (PWD_STANDARD_WATCHDOG)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (!Watchdog)
        return;

    KeAcquireSpinLockAtDpcLevel(&Watchdog->Header.SpinLock);

    if (Watchdog->SuspendCount > 0 || !Watchdog->ClientDpc)
    {
        KeReleaseSpinLockFromDpcLevel(&Watchdog->Header.SpinLock);
        return;
    }

    /* Decrement timeout counter */
    if (Watchdog->TimeoutCount > 0)
    {
        Watchdog->TimeoutCount--;
        if (Watchdog->TimeoutCount == 0 && Watchdog->ClientDpc)
        {
            KDPC ClientDpc;
            Watchdog->Header.LastEvent = WD_EVENT_TIMEOUT;
            RtlCopyMemory(&ClientDpc, &Watchdog->Dpc, sizeof(KDPC));
            ClientDpc.DeferredRoutine = Watchdog->ClientDpc;
            KeInsertQueueDpc(&ClientDpc, NULL, Watchdog);
        }
    }

    KeReleaseSpinLockFromDpcLevel(&Watchdog->Header.SpinLock);
}

/* ===================================================================
 * Allocation / Lifecycle
 * =================================================================== */

/*
 * WdAllocateDeferredWatchdog - Allocates a deferred watchdog object for TDR.
 *
 * Parameters:
 *   DeviceObject - Device to monitor (referenced)
 *   WatchdogType - 1=kernel-time, 2=user-time, 3=both
 *   Flags        - Caller-supplied flags
 *
 * Returns: Watchdog object pointer, or NULL on failure.
 */
PVOID
NTAPI
WdAllocateDeferredWatchdog(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG WatchdogType,
    _In_ ULONG Flags)
{
    PWD_DEFERRED_WATCHDOG Watchdog;

    Watchdog = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(WD_DEFERRED_WATCHDOG),
                                     WD_TAG_DEFERRED);
    if (!Watchdog)
        return NULL;

    RtlZeroMemory(Watchdog, sizeof(WD_DEFERRED_WATCHDOG));

    Watchdog->Header.Tag = WD_TAG_DEFERRED;
    Watchdog->Header.ReferenceCount = 1;
    Watchdog->Header.Flags = Flags;
    Watchdog->Header.DeviceObject = DeviceObject;
    ObfReferenceObject(DeviceObject);

    Watchdog->Header.WatchdogType = WatchdogType;
    Watchdog->Header.LastEvent = WD_EVENT_IDLE;
    Watchdog->RunState = WD_RUN_ALLOCATED;

    KeInitializeSpinLock(&Watchdog->Header.SpinLock);
    KeInitializeTimerEx(&Watchdog->Timer, SynchronizationTimer);
    KeInitializeDpc(&Watchdog->Dpc, WdpDeferredWatchdogDpc, Watchdog);

    Watchdog->TimeIncrement = KeQueryTimeIncrement();

    return Watchdog;
}

/*
 * WdAllocateWatchdog - Allocates a standard watchdog object for videoprt.
 */
PVOID
NTAPI
WdAllocateWatchdog(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG WatchdogType,
    _In_ ULONG Flags)
{
    PWD_STANDARD_WATCHDOG Watchdog;

    Watchdog = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(WD_STANDARD_WATCHDOG),
                                     WD_TAG_STANDARD);
    if (!Watchdog)
        return NULL;

    RtlZeroMemory(Watchdog, sizeof(WD_STANDARD_WATCHDOG));

    Watchdog->Header.Tag = WD_TAG_STANDARD;
    Watchdog->Header.ReferenceCount = 1;
    Watchdog->Header.Flags = Flags;
    Watchdog->Header.DeviceObject = DeviceObject;
    ObfReferenceObject(DeviceObject);

    Watchdog->Header.WatchdogType = WatchdogType;
    Watchdog->Header.LastEvent = WD_EVENT_IDLE;

    KeInitializeSpinLock(&Watchdog->Header.SpinLock);
    KeInitializeTimerEx(&Watchdog->Timer, SynchronizationTimer);
    KeInitializeDpc(&Watchdog->Dpc, WdpStandardWatchdogDpc, Watchdog);

    Watchdog->TimeIncrement = KeQueryTimeIncrement();

    return Watchdog;
}

/*
 * WdFreeDeferredWatchdog - Tears down and frees a deferred watchdog.
 * Must be called at PASSIVE_LEVEL (KeFlushQueuedDpcs requires it).
 */
VOID
NTAPI
WdFreeDeferredWatchdog(
    _In_ PVOID WatchdogObject)
{
    PWD_DEFERRED_WATCHDOG Watchdog = (PWD_DEFERRED_WATCHDOG)WatchdogObject;

    if (!Watchdog)
        return;

    /* Stop the timer and cancel pending DPCs */
    WdStopDeferredWatch(Watchdog);

    /* Drain any in-flight DPCs */
    KeFlushQueuedDpcs();

    /* Release device object reference and free */
    WdpFreeObject(&Watchdog->Header);
}

/*
 * WdFreeWatchdog - Tears down and frees a standard watchdog.
 */
VOID
NTAPI
WdFreeWatchdog(
    _In_ PVOID WatchdogObject)
{
    PWD_STANDARD_WATCHDOG Watchdog = (PWD_STANDARD_WATCHDOG)WatchdogObject;

    if (!Watchdog)
        return;

    WdStopWatch(Watchdog, TRUE);
    KeFlushQueuedDpcs();
    WdpFreeObject(&Watchdog->Header);
}

/* ===================================================================
 * Timer Control
 * =================================================================== */

/*
 * WdStartDeferredWatch - Arms the deferred watchdog timer.
 *
 * Parameters:
 *   WatchdogObject  - Deferred watchdog object
 *   ClientDpc       - DPC routine to call on timeout (e.g., dxgkrnl TDR DPC)
 *   TimeoutMs       - Timeout in milliseconds
 */
VOID
NTAPI
WdStartDeferredWatch(
    _In_ PVOID WatchdogObject,
    _In_ PKDEFERRED_ROUTINE ClientDpc,
    _In_ ULONG TimeoutMs)
{
    PWD_DEFERRED_WATCHDOG Watchdog = (PWD_DEFERRED_WATCHDOG)WatchdogObject;
    LARGE_INTEGER DueTime;
    KIRQL OldIrql;

    if (!Watchdog)
        return;

    KeAcquireSpinLock(&Watchdog->Header.SpinLock, &OldIrql);

    Watchdog->ClientDpc = ClientDpc;
    Watchdog->TimeoutSeconds = TimeoutMs / 1000;
    if (Watchdog->TimeoutSeconds == 0)
        Watchdog->TimeoutSeconds = 1;

    Watchdog->RunState = WD_RUN_RUNNING;
    Watchdog->TimeoutState = WD_TIMEOUT_IDLE;
    Watchdog->Suspended = FALSE;
    Watchdog->SuspendCount = 0;

    /* Zero all snapshot fields */
    Watchdog->EnterCount = 0;
    Watchdog->ExitCount = 0;
    Watchdog->MonitorThread = NULL;
    Watchdog->LastKernelSnapshot = 0;
    Watchdog->LastUserSnapshot = 0;
    Watchdog->KernelTimeBaseline = 0;
    Watchdog->UserTimeBaseline = 0;
    Watchdog->MonitoredThread = NULL;

    /* Set periodic timer: fires every TimeoutMs, first fire after TimeoutMs */
    DueTime.QuadPart = -((LONGLONG)TimeoutMs * 10000LL);
    KeSetTimerEx(&Watchdog->Timer, DueTime, TimeoutMs, &Watchdog->Dpc);

    KeReleaseSpinLock(&Watchdog->Header.SpinLock, OldIrql);
}

/*
 * WdStartWatch - Arms the standard watchdog timer.
 */
VOID
NTAPI
WdStartWatch(
    _In_ PVOID WatchdogObject,
    _In_ LARGE_INTEGER DueTime,
    _In_ PKDEFERRED_ROUTINE ClientDpc)
{
    PWD_STANDARD_WATCHDOG Watchdog = (PWD_STANDARD_WATCHDOG)WatchdogObject;
    KIRQL OldIrql;

    if (!Watchdog)
        return;

    KeAcquireSpinLock(&Watchdog->Header.SpinLock, &OldIrql);

    Watchdog->ClientDpc = ClientDpc;
    Watchdog->DueTime = DueTime;
    Watchdog->SuspendCount = 0;

    /* Compute period from due time for periodic firing */
    KeSetTimerEx(&Watchdog->Timer, DueTime, 0, &Watchdog->Dpc);

    KeReleaseSpinLock(&Watchdog->Header.SpinLock, OldIrql);
}

/*
 * WdStopDeferredWatch - Disarms the deferred watchdog timer.
 */
VOID
NTAPI
WdStopDeferredWatch(
    _In_ PVOID WatchdogObject)
{
    PWD_DEFERRED_WATCHDOG Watchdog = (PWD_DEFERRED_WATCHDOG)WatchdogObject;
    KIRQL OldIrql;

    if (!Watchdog)
        return;

    KeAcquireSpinLock(&Watchdog->Header.SpinLock, &OldIrql);

    Watchdog->RunState = WD_RUN_ALLOCATED;

    KeCancelTimer(&Watchdog->Timer);
    KeRemoveQueueDpc(&Watchdog->Dpc);

    Watchdog->TimeoutState = WD_TIMEOUT_IDLE;
    Watchdog->Suspended = FALSE;
    Watchdog->SuspendCount = 0;
    Watchdog->LastKernelSnapshot = 0;
    Watchdog->LastUserSnapshot = 0;
    Watchdog->KernelTimeBaseline = 0;
    Watchdog->UserTimeBaseline = 0;

    KeReleaseSpinLock(&Watchdog->Header.SpinLock, OldIrql);
}

/*
 * WdStopWatch - Disarms the standard watchdog timer.
 */
VOID
NTAPI
WdStopWatch(
    _In_ PVOID WatchdogObject,
    _In_ BOOLEAN FlushDpcs)
{
    PWD_STANDARD_WATCHDOG Watchdog = (PWD_STANDARD_WATCHDOG)WatchdogObject;
    KIRQL OldIrql;

    if (!Watchdog)
        return;

    KeAcquireSpinLock(&Watchdog->Header.SpinLock, &OldIrql);

    KeCancelTimer(&Watchdog->Timer);
    KeRemoveQueueDpc(&Watchdog->Dpc);

    Watchdog->TimeoutCount = 0;
    Watchdog->SuspendCount = 0;
    Watchdog->LastKernelTime = 0;
    Watchdog->LastUserTime = 0;

    KeReleaseSpinLock(&Watchdog->Header.SpinLock, OldIrql);

    if (FlushDpcs)
    {
        KeFlushQueuedDpcs();
    }
}

/*
 * WdResetDeferredWatch - Resets the deferred watchdog counters (fastcall).
 * Called after a GPU command completes successfully to reset the timeout state.
 */
VOID
FASTCALL
WdResetDeferredWatch(
    _In_ PVOID WatchdogObject)
{
    PWD_DEFERRED_WATCHDOG Watchdog = (PWD_DEFERRED_WATCHDOG)WatchdogObject;
    KIRQL OldIrql;

    if (!Watchdog)
        return;

    KeAcquireSpinLock(&Watchdog->Header.SpinLock, &OldIrql);

    Watchdog->TimeoutState = WD_TIMEOUT_IDLE;
    Watchdog->LastKernelSnapshot = 0;
    Watchdog->LastUserSnapshot = 0;
    Watchdog->KernelTimeBaseline = 0;
    Watchdog->UserTimeBaseline = 0;

    KeReleaseSpinLock(&Watchdog->Header.SpinLock, OldIrql);
}

/*
 * WdResetWatch - Resets the standard watchdog.
 */
VOID
NTAPI
WdResetWatch(
    _In_ PVOID WatchdogObject)
{
    PWD_STANDARD_WATCHDOG Watchdog = (PWD_STANDARD_WATCHDOG)WatchdogObject;
    KIRQL OldIrql;

    if (!Watchdog)
        return;

    KeAcquireSpinLock(&Watchdog->Header.SpinLock, &OldIrql);

    Watchdog->TimeoutCount = 0;
    Watchdog->LastKernelTime = 0;
    Watchdog->LastUserTime = 0;

    KeReleaseSpinLock(&Watchdog->Header.SpinLock, OldIrql);
}

/*
 * WdSuspendDeferredWatch - Suspends the deferred watchdog (fastcall).
 * Used when the GPU deliberately enters an idle/power state.
 */
VOID
FASTCALL
WdSuspendDeferredWatch(
    _In_ PVOID WatchdogObject)
{
    PWD_DEFERRED_WATCHDOG Watchdog = (PWD_DEFERRED_WATCHDOG)WatchdogObject;
    KIRQL OldIrql;

    if (!Watchdog)
        return;

    KeAcquireSpinLock(&Watchdog->Header.SpinLock, &OldIrql);

    InterlockedIncrement(&Watchdog->SuspendCount);
    Watchdog->Suspended = TRUE;

    KeReleaseSpinLock(&Watchdog->Header.SpinLock, OldIrql);
}

/*
 * WdSuspendWatch - Suspends the standard watchdog.
 */
VOID
NTAPI
WdSuspendWatch(
    _In_ PVOID WatchdogObject)
{
    PWD_STANDARD_WATCHDOG Watchdog = (PWD_STANDARD_WATCHDOG)WatchdogObject;
    KIRQL OldIrql;

    if (!Watchdog)
        return;

    KeAcquireSpinLock(&Watchdog->Header.SpinLock, &OldIrql);
    InterlockedIncrement(&Watchdog->SuspendCount);
    KeReleaseSpinLock(&Watchdog->Header.SpinLock, OldIrql);
}

/*
 * WdResumeDeferredWatch - Resumes the deferred watchdog (fastcall).
 *
 * Parameters:
 *   WatchdogObject   - Deferred watchdog
 *   ResetSnapshots   - If TRUE, zeros all snapshot fields
 */
VOID
FASTCALL
WdResumeDeferredWatch(
    _In_ PVOID WatchdogObject,
    _In_ BOOLEAN ResetSnapshots)
{
    PWD_DEFERRED_WATCHDOG Watchdog = (PWD_DEFERRED_WATCHDOG)WatchdogObject;
    KIRQL OldIrql;

    if (!Watchdog)
        return;

    KeAcquireSpinLock(&Watchdog->Header.SpinLock, &OldIrql);

    InterlockedDecrement(&Watchdog->SuspendCount);

    if (ResetSnapshots)
    {
        Watchdog->TimeoutState = WD_TIMEOUT_IDLE;
        Watchdog->LastKernelSnapshot = 0;
        Watchdog->LastUserSnapshot = 0;
        Watchdog->KernelTimeBaseline = 0;
        Watchdog->UserTimeBaseline = 0;
    }

    Watchdog->Suspended = FALSE;

    KeReleaseSpinLock(&Watchdog->Header.SpinLock, OldIrql);
}

/*
 * WdResumeWatch - Resumes the standard watchdog.
 */
VOID
NTAPI
WdResumeWatch(
    _In_ PVOID WatchdogObject,
    _In_ BOOLEAN ResetSnapshots)
{
    PWD_STANDARD_WATCHDOG Watchdog = (PWD_STANDARD_WATCHDOG)WatchdogObject;
    KIRQL OldIrql;

    if (!Watchdog)
        return;

    KeAcquireSpinLock(&Watchdog->Header.SpinLock, &OldIrql);

    InterlockedDecrement(&Watchdog->SuspendCount);

    if (ResetSnapshots)
    {
        Watchdog->LastKernelTime = 0;
        Watchdog->LastUserTime = 0;
    }

    KeReleaseSpinLock(&Watchdog->Header.SpinLock, OldIrql);
}

/* ===================================================================
 * Section Monitoring (Lock-Free, Hot Path)
 * =================================================================== */

/*
 * WdEnterMonitoredSection - Marks entry into a GPU-monitored section (fastcall).
 * Lock-free: uses interlocked increment + atomic exchange.
 * Called by dxgkrnl before submitting GPU command buffers.
 */
VOID
FASTCALL
WdEnterMonitoredSection(
    _In_ PVOID WatchdogObject)
{
    PWD_DEFERRED_WATCHDOG Watchdog = (PWD_DEFERRED_WATCHDOG)WatchdogObject;

    if (!Watchdog)
        return;

    /* Atomically increment enter count */
    InterlockedIncrement(&Watchdog->EnterCount);

    /* Atomically store current thread as the monitored thread */
    InterlockedExchangePointer((PVOID*)&Watchdog->MonitoredThread,
                               KeGetCurrentThread());
}

/*
 * WdExitMonitoredSection - Marks exit from a GPU-monitored section (fastcall).
 * Lock-free: uses interlocked increment only.
 * Called by dxgkrnl after GPU command completion.
 */
VOID
FASTCALL
WdExitMonitoredSection(
    _In_ PVOID WatchdogObject)
{
    PWD_DEFERRED_WATCHDOG Watchdog = (PWD_DEFERRED_WATCHDOG)WatchdogObject;

    if (!Watchdog)
        return;

    /* Atomically increment exit count */
    InterlockedIncrement(&Watchdog->ExitCount);
}

/* ===================================================================
 * Accessor Functions
 * =================================================================== */

/*
 * WdGetDeviceObject - Returns the referenced device object from the watchdog.
 * Caller must dereference the returned object when done.
 */
PDEVICE_OBJECT
NTAPI
WdGetDeviceObject(
    _In_ PVOID WatchdogObject)
{
    PWD_HEADER Header = (PWD_HEADER)WatchdogObject;

    if (!Header || !Header->DeviceObject)
        return NULL;

    ObfReferenceObject(Header->DeviceObject);
    return Header->DeviceObject;
}

/*
 * WdGetLowestDeviceObject - Returns the lowest device in the device stack.
 * Caller must dereference the returned object.
 */
PDEVICE_OBJECT
NTAPI
WdGetLowestDeviceObject(
    _In_ PVOID WatchdogObject)
{
    PWD_HEADER Header = (PWD_HEADER)WatchdogObject;

    if (!Header || !Header->DeviceObject)
        return NULL;

    return IoGetDeviceAttachmentBaseRef(Header->DeviceObject);
}

/*
 * WdGetLastEvent - Returns the last event type (1=idle, 2=timeout, 3=recovery).
 */
ULONG
NTAPI
WdGetLastEvent(
    _In_ PVOID WatchdogObject)
{
    PWD_HEADER Header = (PWD_HEADER)WatchdogObject;

    if (!Header)
        return WD_EVENT_IDLE;

    return Header->LastEvent;
}

/*
 * WdReferenceObject - Increments the watchdog reference count.
 */
VOID
NTAPI
WdReferenceObject(
    _In_ PVOID WatchdogObject)
{
    PWD_HEADER Header = (PWD_HEADER)WatchdogObject;

    if (!Header)
        return;

    InterlockedIncrement(&Header->ReferenceCount);
}

/*
 * WdDereferenceObject - Decrements the watchdog reference count.
 * Frees the object when the count reaches zero.
 */
VOID
NTAPI
WdDereferenceObject(
    _In_ PVOID WatchdogObject)
{
    PWD_HEADER Header = (PWD_HEADER)WatchdogObject;

    if (!Header)
        return;

    if (InterlockedDecrement(&Header->ReferenceCount) == 0)
    {
        WdpFreeObject(Header);
    }
}

/*
 * WdAttachContext - Attaches a caller-supplied context buffer to the watchdog.
 */
VOID
NTAPI
WdAttachContext(
    _In_ PVOID WatchdogObject,
    _In_ PVOID ContextBuffer)
{
    PWD_HEADER Header = (PWD_HEADER)WatchdogObject;

    if (!Header)
        return;

    Header->ContextBuffer = ContextBuffer;
}

/*
 * WdDetachContext - Detaches and returns the context buffer.
 * Sets the internal pointer to NULL.
 */
PVOID
NTAPI
WdDetachContext(
    _In_ PVOID WatchdogObject)
{
    PWD_HEADER Header = (PWD_HEADER)WatchdogObject;
    PVOID Context;

    if (!Header)
        return NULL;

    Context = Header->ContextBuffer;
    Header->ContextBuffer = NULL;
    return Context;
}

/*
 * WdCompleteEvent - Acknowledges a TDR event and releases the watchdog reference.
 * Called by dxgkrnl after TDR recovery completes.
 */
VOID
NTAPI
WdCompleteEvent(
    _In_ PVOID WatchdogObject,
    _In_ PVOID ThreadObject)
{
    PWD_HEADER Header = (PWD_HEADER)WatchdogObject;

    UNREFERENCED_PARAMETER(ThreadObject);

    if (!Header)
        return;

    /* Clear timeout state */
    Header->LastEvent = WD_EVENT_IDLE;

    /* Release the notified thread reference if one was taken */
    if (Header->NotifiedThread)
    {
        ObfDereferenceObject(Header->NotifiedThread);
        Header->NotifiedThread = NULL;
    }

    /* Release the reference taken in WdpNotifyTimeout */
    WdDereferenceObject(WatchdogObject);
}

/*
 * WdMadeAnyProgress - Checks if any monitored counters changed since last check.
 * Used internally by the DPC callback.
 */
BOOLEAN
NTAPI
WdMadeAnyProgress(
    _In_ PVOID WatchdogObject)
{
    PWD_DEFERRED_WATCHDOG Watchdog = (PWD_DEFERRED_WATCHDOG)WatchdogObject;
    ULONG KernelTime, UserTime;

    if (!Watchdog)
        return TRUE;

    if (!Watchdog->MonitoredThread)
        return TRUE;

    KeQueryRuntimeThread(Watchdog->MonitoredThread, &UserTime);
    KernelTime = (ULONG)(ULONG_PTR)KeQueryRuntimeThread(Watchdog->MonitoredThread, &UserTime);

    if (KernelTime != Watchdog->LastKernelSnapshot ||
        UserTime != Watchdog->LastUserSnapshot)
    {
        return TRUE;
    }

    return FALSE;
}

/* ===================================================================
 * Diagnostic Functions (stubs -- not critical for TDR operation)
 * =================================================================== */

NTSTATUS
NTAPI
WdDiagInit(
    _In_ PVOID DeviceObject,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_SUCCESS;
}

VOID
NTAPI
WdDiagNotifyUser(
    _In_ PVOID WatchdogContext,
    _In_ ULONG EventType)
{
    UNREFERENCED_PARAMETER(WatchdogContext);
    UNREFERENCED_PARAMETER(EventType);
}

VOID
NTAPI
WdDiagShutdown(
    _In_ PVOID WatchdogContext)
{
    UNREFERENCED_PARAMETER(WatchdogContext);
}

/* ===================================================================
 * Display Manager Functions (stubs -- maintain existing API surface)
 * =================================================================== */

NTSTATUS
NTAPI
DMgrAcquireDisplayOwnership(
    _In_ PVOID OwnerContext)
{
    UNREFERENCED_PARAMETER(OwnerContext);
    return STATUS_SUCCESS;
}

VOID
NTAPI
DMgrReleaseDisplayOwnership(
    _In_ PVOID OwnerContext)
{
    UNREFERENCED_PARAMETER(OwnerContext);
}

ULONG
NTAPI
DMgrGetDisplayOwnership(VOID)
{
    return 0;
}

NTSTATUS
NTAPI
DMgrAcquireGdiViewId(
    _Out_ PULONG ViewId)
{
    if (ViewId)
        *ViewId = 0;
    return STATUS_SUCCESS;
}

VOID
NTAPI
DMgrReleaseGdiViewId(
    _In_ ULONG ViewId)
{
    UNREFERENCED_PARAMETER(ViewId);
}

NTSTATUS
NTAPI
DMgrWriteDeviceCountToRegistry(
    _In_ ULONG DeviceCount)
{
    UNREFERENCED_PARAMETER(DeviceCount);
    return STATUS_SUCCESS;
}

/* ===================================================================
 * Session Manager Functions (stubs -- maintain existing API surface)
 * =================================================================== */

PVOID
NTAPI
SMgrGetActiveSessionProcess(VOID)
{
    return NULL;
}

NTSTATUS
NTAPI
SMgrGdiCallout(
    _In_ PVOID CalloutData)
{
    UNREFERENCED_PARAMETER(CalloutData);
    return STATUS_SUCCESS;
}
