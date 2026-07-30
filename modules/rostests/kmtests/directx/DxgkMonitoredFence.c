/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Monitored fence value semantics
 *
 * A monitored fence is read directly by the GPU and by waiters without a
 * kernel transition, so its value can only ever move forward.
 */

#include <kmt_test.h>
#include "fence_core.h"

#define MONITORED_RUNDOWN_TIMEOUT_SECONDS 10

typedef struct _MONITORED_RUNDOWN_TEST
{
    EX_RUNDOWN_REF Rundown;
    KEVENT ReaderAcquired;
    KEVENT ReleaseReader;
    KEVENT DestroyAttempting;
    KEVENT ReaderDone;
    KEVENT DestroyDone;
    BOOLEAN ReaderOwnsRundown;
} MONITORED_RUNDOWN_TEST, *PMONITORED_RUNDOWN_TEST;

static NTSTATUS
WaitForMonitoredRundownEvent(
    _In_ PKEVENT Event,
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart =
        -10LL * 1000LL *
        (Milliseconds != 0
             ? Milliseconds
             : MONITORED_RUNDOWN_TIMEOUT_SECONDS * 1000);
    return KeWaitForSingleObject(
               Event,
               Executive,
               KernelMode,
               FALSE,
               &Timeout);
}

static VOID NTAPI
MonitoredRundownReaderThread(
    _In_ PVOID Parameter)
{
    PMONITORED_RUNDOWN_TEST Test = Parameter;

    Test->ReaderOwnsRundown =
        ExAcquireRundownProtection(&Test->Rundown);
    KeSetEvent(
        &Test->ReaderAcquired,
        IO_NO_INCREMENT,
        FALSE);
    if (Test->ReaderOwnsRundown)
    {
        (VOID)KeWaitForSingleObject(
            &Test->ReleaseReader,
            Executive,
            KernelMode,
            FALSE,
            NULL);
        ExReleaseRundownProtection(&Test->Rundown);
    }
    KeSetEvent(&Test->ReaderDone, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID NTAPI
MonitoredRundownDestroyThread(
    _In_ PVOID Parameter)
{
    PMONITORED_RUNDOWN_TEST Test = Parameter;

    KeSetEvent(
        &Test->DestroyAttempting,
        IO_NO_INCREMENT,
        FALSE);
    ExWaitForRundownProtectionRelease(&Test->Rundown);
    KeSetEvent(&Test->DestroyDone, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID TestInitialization(VOID)
{
    DXGK_MONITORED_FENCE Fence;

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 0, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 0ULL);
    ok_bool_true(Fence.Initialized, "initialized");

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 42, DXGK_MONITORED_FENCE_FLAG_SHARED); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 42ULL);

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 0, 0x8000UL); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* Neither signalable nor waitable carries no information at all. */
    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 0,
                  DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL | DXGK_MONITORED_FENCE_FLAG_NO_WAIT); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestMonotonicSignal(VOID)
{
    DXGK_MONITORED_FENCE Fence;

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 10, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 11); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 11ULL);
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 11); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 11ULL);
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 1000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 1000ULL);

    /*
     * A regressing signal is refused and leaves the published value alone.
     * Applying it would un-satisfy waits that already resolved, and a waiter
     * that has been released cannot be recalled.
     */
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 999); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulonglong(Fence.CurrentValue, 1000ULL);
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulonglong(Fence.CurrentValue, 1000ULL);

    /* 64-bit values must not be truncated to 32. */
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 0x1FFFFFFFFULL); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Fence.CurrentValue, 0x1FFFFFFFFULL);
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 0xFFFFFFFFULL); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestWaitResolution(VOID)
{
    DXGK_MONITORED_FENCE Fence;

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&Fence, 5, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkMonitoredFenceCoreIsSatisfied(&Fence, 5), "at the value");
    ok_bool_true(DxgkMonitoredFenceCoreIsSatisfied(&Fence, 4), "below the value");
    ok_bool_false(DxgkMonitoredFenceCoreIsSatisfied(&Fence, 6), "above the value");
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Fence, 6); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkMonitoredFenceCoreIsSatisfied(&Fence, 6), "satisfied after signal");
}

static VOID TestCapabilityGating(VOID)
{
    DXGK_MONITORED_FENCE NoSignal;
    DXGK_MONITORED_FENCE NoWait;
    DXGK_MONITORED_FENCE Uninitialized;

    RtlZeroMemory(&Uninitialized, sizeof(Uninitialized));
    { NTSTATUS Observed = DxgkMonitoredFenceCoreCanWait(&Uninitialized); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&Uninitialized, 1); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }
    ok_bool_false(DxgkMonitoredFenceCoreIsSatisfied(&Uninitialized, 0), "uninitialized satisfies nothing");

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&NoSignal, 0, DXGK_MONITORED_FENCE_FLAG_NO_SIGNAL); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreCanSignal(&NoSignal); ok_eq_hex(Observed, STATUS_NOT_SUPPORTED); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreSignal(&NoSignal, 1); ok_eq_hex(Observed, STATUS_NOT_SUPPORTED); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreCanWait(&NoSignal); ok_eq_hex(Observed, STATUS_SUCCESS); }

    { NTSTATUS Observed = DxgkMonitoredFenceCoreInitialize(&NoWait, 0, DXGK_MONITORED_FENCE_FLAG_NO_WAIT); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreCanWait(&NoWait); ok_eq_hex(Observed, STATUS_NOT_SUPPORTED); }
    { NTSTATUS Observed = DxgkMonitoredFenceCoreCanSignal(&NoWait); ok_eq_hex(Observed, STATUS_SUCCESS); }
}

static VOID
TestInterruptGatingAndCoalescing(VOID)
{
    volatile LONG PendingNodes = 0;
    ULONG Pending;

    ok_bool_false(
        DxgkMonitoredInterruptCoreSupported(2100, 2200, 1),
        "WDDM 2.1 configuration exposed interrupt 11");
    ok_bool_false(
        DxgkMonitoredInterruptCoreSupported(2200, 2100, 1),
        "WDDM 2.1 runtime exposed interrupt 11");
    ok_bool_false(
        DxgkMonitoredInterruptCoreSupported(2200, 2200, 0),
        "zero-node adapter exposed interrupt 11");
    ok_bool_false(
        DxgkMonitoredInterruptCoreSupported(2200, 2200, 33),
        "unrepresentable node topology exposed interrupt 11");
    ok_bool_true(
        DxgkMonitoredInterruptCoreSupported(2200, 2200, 2),
        "WDDM 2.2 two-node topology was rejected");

    ok_eq_hex(
        DxgkMonitoredInterruptCoreEnqueue(
            &PendingNodes, 2, 2, 0),
        STATUS_INVALID_PARAMETER);
    ok_eq_hex(
        DxgkMonitoredInterruptCoreEnqueue(
            &PendingNodes, 2, 0, 1),
        STATUS_INVALID_PARAMETER);
    ok_eq_hex(
        DxgkMonitoredInterruptCoreEnqueue(
            &PendingNodes, 2, 1, 0),
        STATUS_SUCCESS);
    ok_eq_hex(
        DxgkMonitoredInterruptCoreEnqueue(
            &PendingNodes, 2, 1, 0),
        STATUS_SUCCESS);

    Pending =
        DxgkMonitoredInterruptCoreDrain(&PendingNodes);
    ok_eq_ulong(Pending, 1UL << 1);
    ok_eq_ulong(
        DxgkMonitoredInterruptCoreDrain(&PendingNodes),
        0UL);

    ok_bool_true(
        DxgkMonitoredInterruptCoreAffinityMatches(0, 0),
        "zero affinity did not match all adapters");
    ok_bool_true(
        DxgkMonitoredInterruptCoreAffinityMatches(1, 0),
        "physical adapter zero was filtered");
    ok_bool_false(
        DxgkMonitoredInterruptCoreAffinityMatches(2, 0),
        "foreign physical adapter matched");
}

static VOID
TestDestroyWaitsForInterruptReader(VOID)
{
    MONITORED_RUNDOWN_TEST Test;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE ReaderThread = NULL;
    HANDLE DestroyThread = NULL;
    BOOLEAN AcquiredAfterDestroy;
    NTSTATUS Status;

    RtlZeroMemory(&Test, sizeof(Test));
    ExInitializeRundownProtection(&Test.Rundown);
    KeInitializeEvent(
        &Test.ReaderAcquired,
        NotificationEvent,
        FALSE);
    KeInitializeEvent(
        &Test.ReleaseReader,
        NotificationEvent,
        FALSE);
    KeInitializeEvent(
        &Test.DestroyAttempting,
        NotificationEvent,
        FALSE);
    KeInitializeEvent(
        &Test.ReaderDone,
        NotificationEvent,
        FALSE);
    KeInitializeEvent(
        &Test.DestroyDone,
        NotificationEvent,
        FALSE);
    InitializeObjectAttributes(
        &Attributes,
        NULL,
        OBJ_KERNEL_HANDLE,
        NULL,
        NULL);

    Status =
        PsCreateSystemThread(
            &ReaderThread,
            THREAD_ALL_ACCESS,
            &Attributes,
            NULL,
            NULL,
            MonitoredRundownReaderThread,
            &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    Status =
        WaitForMonitoredRundownEvent(
            &Test.ReaderAcquired,
            0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(
        Test.ReaderOwnsRundown,
        "interrupt reader did not acquire rundown");
    if (!Test.ReaderOwnsRundown)
        goto Cleanup;

    Status =
        PsCreateSystemThread(
            &DestroyThread,
            THREAD_ALL_ACCESS,
            &Attributes,
            NULL,
            NULL,
            MonitoredRundownDestroyThread,
            &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status =
        WaitForMonitoredRundownEvent(
            &Test.DestroyAttempting,
            0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status =
        WaitForMonitoredRundownEvent(
            &Test.DestroyDone,
            100);
    ok_eq_hex(Status, STATUS_TIMEOUT);

Cleanup:
    KeSetEvent(
        &Test.ReleaseReader,
        IO_NO_INCREMENT,
        FALSE);
    Status =
        WaitForMonitoredRundownEvent(
            &Test.ReaderDone,
            0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (DestroyThread != NULL)
    {
        Status =
            WaitForMonitoredRundownEvent(
                &Test.DestroyDone,
                0);
        ok_eq_hex(Status, STATUS_SUCCESS);
        /*
         * The completed destroy leaves rundown closed.  A late DPC scan
         * cannot acquire the object after registry removal.
         */
        AcquiredAfterDestroy =
            ExAcquireRundownProtection(&Test.Rundown);
        ok_bool_false(
            AcquiredAfterDestroy,
            "late interrupt reader entered after destroy");
        if (AcquiredAfterDestroy)
            ExReleaseRundownProtection(&Test.Rundown);
    }
    ZwClose(ReaderThread);
    if (DestroyThread != NULL)
        ZwClose(DestroyThread);
}

START_TEST(DxgkMonitoredFence)
{
    TestInitialization();
    TestMonotonicSignal();
    TestWaitResolution();
    TestCapabilityGating();
    TestInterruptGatingAndCoalescing();
    TestDestroyWaitsForInterruptReader();
}

/* EOF */
