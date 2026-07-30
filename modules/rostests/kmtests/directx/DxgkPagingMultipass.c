/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     The multipass BuildPagingBuffer loop
 *
 * A miniport may refuse a transfer that does not fit its DMA buffer and ask to
 * be called again from an offset.  The caller's completion fence must ride
 * only on the final pass: signalling early tells a waiter the copy finished
 * while its tail is still outstanding.
 */

#include <kmt_test.h>
#include "paging_core.h"

#define PAGING_FENCE_TEST_TIMEOUT_SECONDS 10

typedef struct _PAGING_FENCE_SERIAL_TEST
{
    DXGK_PAGING_FENCE_QUEUE_CORE Queue;
    KEVENT FirstBegan;
    KEVENT ReleaseFirst;
    KEVENT SecondAttempting;
    KEVENT SecondBegan;
    KEVENT FirstDone;
    KEVENT SecondDone;
    volatile LONG Sequence;
    ULONGLONG Order[2];
    NTSTATUS FirstStatus;
    NTSTATUS SecondStatus;
} PAGING_FENCE_SERIAL_TEST, *PPAGING_FENCE_SERIAL_TEST;

typedef struct _PAGING_FENCE_SHUTDOWN_TEST
{
    PDXGK_PAGING_FENCE_QUEUE_CORE Queue;
    KEVENT Attempting;
    KEVENT Done;
} PAGING_FENCE_SHUTDOWN_TEST, *PPAGING_FENCE_SHUTDOWN_TEST;

static NTSTATUS
WaitForPagingFenceTestEvent(
    _In_ PKEVENT Event)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart =
        -10LL * 1000LL * 1000LL * PAGING_FENCE_TEST_TIMEOUT_SECONDS;
    return KeWaitForSingleObject(Event,
                                 Executive,
                                 KernelMode,
                                 FALSE,
                                 &Timeout);
}

static NTSTATUS
WaitForPagingFenceTestEventMilliseconds(
    _In_ PKEVENT Event,
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Timeout;

    Timeout.QuadPart = -10LL * 1000LL * Milliseconds;
    return KeWaitForSingleObject(Event,
                                 Executive,
                                 KernelMode,
                                 FALSE,
                                 &Timeout);
}

static VOID NTAPI
PagingFenceFirstThread(
    _In_ PVOID Parameter)
{
    PPAGING_FENCE_SERIAL_TEST Test = Parameter;
    DXGK_PAGING_FENCE_TRANSACTION Transaction;
    ULONGLONG Value;
    LONG Slot;

    Test->FirstStatus =
        DxgkPagingFenceQueueCoreBegin(&Test->Queue, &Transaction);
    if (NT_SUCCESS(Test->FirstStatus))
    {
        Value = (ULONGLONG)InterlockedIncrement64(
                              &Transaction.CandidateCounter);
        KeSetEvent(&Test->FirstBegan, IO_NO_INCREMENT, FALSE);
        (VOID)KeWaitForSingleObject(&Test->ReleaseFirst,
                                    Executive,
                                    KernelMode,
                                    FALSE,
                                    NULL);
        Slot = InterlockedIncrement(&Test->Sequence) - 1;
        if ((ULONG)Slot < RTL_NUMBER_OF(Test->Order))
            Test->Order[Slot] = Value;
        Test->FirstStatus =
            DxgkPagingFenceQueueCoreComplete(&Transaction, Value);
    }
    else
    {
        KeSetEvent(&Test->FirstBegan, IO_NO_INCREMENT, FALSE);
    }
    KeSetEvent(&Test->FirstDone, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID NTAPI
PagingFenceSecondThread(
    _In_ PVOID Parameter)
{
    PPAGING_FENCE_SERIAL_TEST Test = Parameter;
    DXGK_PAGING_FENCE_TRANSACTION Transaction;
    ULONGLONG Value;
    LONG Slot;

    KeSetEvent(&Test->SecondAttempting, IO_NO_INCREMENT, FALSE);
    Test->SecondStatus =
        DxgkPagingFenceQueueCoreBegin(&Test->Queue, &Transaction);
    if (NT_SUCCESS(Test->SecondStatus))
    {
        Value = (ULONGLONG)InterlockedIncrement64(
                              &Transaction.CandidateCounter);
        KeSetEvent(&Test->SecondBegan, IO_NO_INCREMENT, FALSE);
        Slot = InterlockedIncrement(&Test->Sequence) - 1;
        if ((ULONG)Slot < RTL_NUMBER_OF(Test->Order))
            Test->Order[Slot] = Value;
        Test->SecondStatus =
            DxgkPagingFenceQueueCoreComplete(&Transaction, Value);
    }
    else
    {
        KeSetEvent(&Test->SecondBegan, IO_NO_INCREMENT, FALSE);
    }
    KeSetEvent(&Test->SecondDone, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID NTAPI
PagingFenceShutdownThread(
    _In_ PVOID Parameter)
{
    PPAGING_FENCE_SHUTDOWN_TEST Test = Parameter;

    KeSetEvent(&Test->Attempting, IO_NO_INCREMENT, FALSE);
    DxgkPagingFenceQueueCoreShutDown(Test->Queue);
    KeSetEvent(&Test->Done, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID TestSinglePass(VOID)
{
    DXGK_PAGING_CORE_MULTIPASS Pass;

    { NTSTATUS Observed = DxgkPagingCoreMultipassBegin(&Pass, 0x1000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkPagingCoreMultipassIsFirst(&Pass), "the first pass is marked");
    ok_bool_false(DxgkPagingCoreMultipassMayEmitFence(&Pass), "no fence before completion");

    { NTSTATUS Observed = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_SUCCESS, 0x1000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(Pass.Complete, "complete");
    ok_bool_false(DxgkPagingCoreMultipassIsFirst(&Pass), "no longer the first pass");
    ok_bool_true(DxgkPagingCoreMultipassMayEmitFence(&Pass), "the fence may ride the final pass");
    { NTSTATUS Observed = DxgkPagingCoreMultipassEmitFence(&Pass); ok_eq_hex(Observed, STATUS_SUCCESS); }
    /* Exactly once: a second emission would double-signal the waiter. */
    { NTSTATUS Observed = DxgkPagingCoreMultipassEmitFence(&Pass); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }

    { NTSTATUS Observed = DxgkPagingCoreMultipassBegin(&Pass, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestMultiplePasses(VOID)
{
    DXGK_PAGING_CORE_MULTIPASS Pass;

    { NTSTATUS Observed = DxgkPagingCoreMultipassBegin(&Pass, 0x3000); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* The miniport takes a page at a time and asks for another pass. */
    { NTSTATUS Observed = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER, 0x1000); ok_eq_hex(Observed, STATUS_MORE_PROCESSING_REQUIRED); }
    ok_eq_ulonglong(Pass.MultipassOffset, 0x1000ULL);
    ok_bool_false(Pass.Complete, "still outstanding");
    /* The fence must not ride an intermediate pass. */
    ok_bool_false(DxgkPagingCoreMultipassMayEmitFence(&Pass), "no fence mid-transfer");
    { NTSTATUS Observed = DxgkPagingCoreMultipassEmitFence(&Pass); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }

    { NTSTATUS Observed = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER, 0x1000); ok_eq_hex(Observed, STATUS_MORE_PROCESSING_REQUIRED); }
    ok_eq_ulonglong(Pass.MultipassOffset, 0x2000ULL);
    ok_bool_false(DxgkPagingCoreMultipassMayEmitFence(&Pass), "still no fence");

    { NTSTATUS Observed = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_SUCCESS, 0x1000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Pass.MultipassOffset, 0x3000ULL);
    ok_eq_ulong(Pass.PassCount, 3UL);
    ok_bool_true(DxgkPagingCoreMultipassMayEmitFence(&Pass), "the final pass may signal");
}

static VOID TestProtocolViolations(VOID)
{
    DXGK_PAGING_CORE_MULTIPASS Pass;

    /* Asking for another pass without consuming anything would loop forever
     * on the same bytes. */
    { NTSTATUS Observed = DxgkPagingCoreMultipassBegin(&Pass, 0x2000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER, 0); ok_eq_hex(Observed, STATUS_DEVICE_PROTOCOL_ERROR); }

    /* Reporting success with bytes left behind silently truncates the copy. */
    { NTSTATUS Observed = DxgkPagingCoreMultipassBegin(&Pass, 0x2000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_SUCCESS, 0x1000); ok_eq_hex(Observed, STATUS_DEVICE_PROTOCOL_ERROR); }

    /* More bytes than remain is nonsense and must not advance past the end. */
    { NTSTATUS Observed = DxgkPagingCoreMultipassBegin(&Pass, 0x1000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_SUCCESS, 0x2000); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulonglong(Pass.MultipassOffset, 0ULL);

    /* A miniport failure is reported as-is rather than being turned into a
     * completion. */
    { NTSTATUS Observed = DxgkPagingCoreMultipassBegin(&Pass, 0x1000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_INSUFFICIENT_RESOURCES, 0); ok_eq_hex(Observed, STATUS_INSUFFICIENT_RESOURCES); }
    ok_bool_false(Pass.Complete, "a failed transfer is not complete");
    ok_bool_false(DxgkPagingCoreMultipassMayEmitFence(&Pass), "and never signals");

    /* Advancing after completion is a use-after-finish. */
    { NTSTATUS Observed = DxgkPagingCoreMultipassBegin(&Pass, 0x1000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_SUCCESS, 0x1000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_SUCCESS, 0); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }
}

static VOID TestPassCeiling(VOID)
{
    DXGK_PAGING_CORE_MULTIPASS Pass;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Iteration;

    /* A miniport that consumes one byte per pass must not spin without bound;
     * the loop is capped so a wedged miniport fails instead of hanging. */
    { NTSTATUS Observed = DxgkPagingCoreMultipassBegin(&Pass, 0x10000); ok_eq_hex(Observed, STATUS_SUCCESS); }
    for (Iteration = 0; Iteration < DXGK_PAGING_CORE_MAX_PASSES + 4; ++Iteration)
    {
        Status = DxgkPagingCoreMultipassAdvance(&Pass, STATUS_GRAPHICS_INSUFFICIENT_DMA_BUFFER, 1);
        if (Status != STATUS_MORE_PROCESSING_REQUIRED)
            break;
    }
    ok_eq_hex(Status, STATUS_DEVICE_BUSY);
    ok_eq_ulong(Pass.PassCount, (ULONG)DXGK_PAGING_CORE_MAX_PASSES);
}

static VOID
TestMonitoredSignalAppendPolicy(VOID)
{
    BOOLEAN WrittenByGpu = TRUE;

    ok_bool_false(
        DxgkPagingCoreShouldAppendMonitoredSignal(
            2100, 2200, TRUE, TRUE),
        "WDDM 2.1 configuration appended operation 16");
    ok_bool_false(
        DxgkPagingCoreShouldAppendMonitoredSignal(
            2200, 2100, TRUE, TRUE),
        "WDDM 2.1 runtime appended operation 16");
    ok_bool_false(
        DxgkPagingCoreShouldAppendMonitoredSignal(
            2200, 2200, TRUE, FALSE),
        "missing GPU address appended operation 16");
    ok_bool_true(
        DxgkPagingCoreShouldAppendMonitoredSignal(
            2200, 2200, TRUE, TRUE),
        "WDDM 2.2 signal did not append operation 16");

    ok_eq_hex(
        DxgkPagingCoreBeginMonitoredSignal(FALSE),
        STATUS_INVALID_DEVICE_STATE);
    ok_eq_hex(
        DxgkPagingCoreBeginMonitoredSignal(TRUE),
        STATUS_SUCCESS);

    ok_eq_hex(
        DxgkPagingCoreFinishMonitoredSignal(
            STATUS_INSUFFICIENT_RESOURCES,
            0,
            &WrittenByGpu),
        STATUS_INSUFFICIENT_RESOURCES);
    ok_bool_false(
        WrittenByGpu,
        "failed signal build claimed a GPU write");

    WrittenByGpu = TRUE;
    ok_eq_hex(
        DxgkPagingCoreFinishMonitoredSignal(
            STATUS_SUCCESS,
            0,
            &WrittenByGpu),
        STATUS_SUCCESS);
    ok_bool_false(
        WrittenByGpu,
        "zero-byte signal build suppressed CPU publication");

    ok_eq_hex(
        DxgkPagingCoreFinishMonitoredSignal(
            STATUS_SUCCESS,
            sizeof(ULONGLONG),
            &WrittenByGpu),
        STATUS_SUCCESS);
    ok_bool_true(
        WrittenByGpu,
        "emitted operation 16 did not claim the GPU write");

    /*
     * The public handle may close after paging acquired the GPU-address
     * reference.  A no-work completion must publish through that retained
     * object instead of trying to resolve the now-stale handle again.
     */
    ok_eq_long(
        DxgkPagingCoreNoWorkSignalRoute(TRUE, TRUE),
        DxgkPagingNoWorkSignalRetainedReference);
    ok_eq_long(
        DxgkPagingCoreNoWorkSignalRoute(TRUE, FALSE),
        DxgkPagingNoWorkSignalHandle);
    ok_eq_long(
        DxgkPagingCoreNoWorkSignalRoute(FALSE, FALSE),
        DxgkPagingNoWorkSignalNone);
}

static VOID
TestPagingFenceCommitAndRollback(VOID)
{
    DXGK_PAGING_FENCE_QUEUE_CORE Queue;
    DXGK_PAGING_FENCE_TRANSACTION Transaction;
    ULONGLONG Value;
    NTSTATUS Status;

    DxgkPagingFenceQueueCoreInitialize(&Queue, 0);

    Status = DxgkPagingFenceQueueCoreBegin(&Queue, &Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Value = (ULONGLONG)InterlockedIncrement64(
                          &Transaction.CandidateCounter);
    ok_eq_ulonglong(Value, 1ULL);
    DxgkPagingFenceQueueCoreAbort(&Transaction);
    ok_eq_ulonglong(
        DxgkPagingFenceQueueCoreQueryCommitted(&Queue),
        0ULL);

    /* A rejected request did not consume value one, so the retry owns it. */
    Status = DxgkPagingFenceQueueCoreBegin(&Queue, &Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Value = (ULONGLONG)InterlockedIncrement64(
                          &Transaction.CandidateCounter);
    ok_eq_ulonglong(Value, 1ULL);
    Status = DxgkPagingFenceQueueCoreComplete(&Transaction, Value);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(
        DxgkPagingFenceQueueCoreQueryCommitted(&Queue),
        1ULL);

    /* Already-resident/no-op requests neither signal nor consume a value. */
    Status = DxgkPagingFenceQueueCoreBegin(&Queue, &Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkPagingFenceQueueCoreComplete(&Transaction, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(
        DxgkPagingFenceQueueCoreQueryCommitted(&Queue),
        1ULL);

    DxgkPagingFenceQueueCoreShutDown(&Queue);
    Status = DxgkPagingFenceQueueCoreBegin(&Queue, &Transaction);
    ok_eq_hex(Status, STATUS_DELETE_PENDING);

    DxgkPagingFenceQueueCoreInitialize(&Queue, MAXULONGLONG);
    Status = DxgkPagingFenceQueueCoreBegin(&Queue, &Transaction);
    ok_eq_hex(Status, STATUS_INTEGER_OVERFLOW);
}

static VOID
TestPagingFenceSerializedAdmission(VOID)
{
    PAGING_FENCE_SERIAL_TEST Test;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE FirstThread = NULL;
    HANDLE SecondThread = NULL;
    NTSTATUS Status;

    RtlZeroMemory(&Test, sizeof(Test));
    DxgkPagingFenceQueueCoreInitialize(&Test.Queue, 0);
    KeInitializeEvent(&Test.FirstBegan, NotificationEvent, FALSE);
    KeInitializeEvent(&Test.ReleaseFirst, NotificationEvent, FALSE);
    KeInitializeEvent(&Test.SecondAttempting, NotificationEvent, FALSE);
    KeInitializeEvent(&Test.SecondBegan, NotificationEvent, FALSE);
    KeInitializeEvent(&Test.FirstDone, NotificationEvent, FALSE);
    KeInitializeEvent(&Test.SecondDone, NotificationEvent, FALSE);
    InitializeObjectAttributes(&Attributes,
                               NULL,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = PsCreateSystemThread(&FirstThread,
                                  THREAD_ALL_ACCESS,
                                  &Attributes,
                                  NULL,
                                  NULL,
                                  PagingFenceFirstThread,
                                  &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    Status = WaitForPagingFenceTestEvent(&Test.FirstBegan);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = PsCreateSystemThread(&SecondThread,
                                  THREAD_ALL_ACCESS,
                                  &Attributes,
                                  NULL,
                                  NULL,
                                  PagingFenceSecondThread,
                                  &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        Status = WaitForPagingFenceTestEvent(&Test.SecondAttempting);
        ok_eq_hex(Status, STATUS_SUCCESS);
        /*
         * The second caller reached Begin, but cannot reserve value two while
         * value one is still between reservation and scheduler admission.
         */
        Status = WaitForPagingFenceTestEventMilliseconds(
                     &Test.SecondBegan,
                     100);
        ok_eq_hex(Status, STATUS_TIMEOUT);
    }

    KeSetEvent(&Test.ReleaseFirst, IO_NO_INCREMENT, FALSE);
    Status = WaitForPagingFenceTestEvent(&Test.FirstDone);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (SecondThread != NULL)
    {
        Status = WaitForPagingFenceTestEvent(&Test.SecondDone);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    ok_eq_hex(Test.FirstStatus, STATUS_SUCCESS);
    if (SecondThread != NULL)
        ok_eq_hex(Test.SecondStatus, STATUS_SUCCESS);
    ok_eq_long(Test.Sequence, SecondThread != NULL ? 2L : 1L);
    ok_eq_ulonglong(Test.Order[0], 1ULL);
    if (SecondThread != NULL)
        ok_eq_ulonglong(Test.Order[1], 2ULL);
    ok_eq_ulonglong(
        DxgkPagingFenceQueueCoreQueryCommitted(&Test.Queue),
        SecondThread != NULL ? 2ULL : 1ULL);

    ZwClose(FirstThread);
    if (SecondThread != NULL)
        ZwClose(SecondThread);
}

static VOID
TestPagingFenceShutdownDrainsTransaction(VOID)
{
    DXGK_PAGING_FENCE_QUEUE_CORE Queue;
    DXGK_PAGING_FENCE_TRANSACTION Transaction;
    PAGING_FENCE_SHUTDOWN_TEST Test;
    OBJECT_ATTRIBUTES Attributes;
    HANDLE Thread = NULL;
    NTSTATUS Status;

    DxgkPagingFenceQueueCoreInitialize(&Queue, 0);
    Status = DxgkPagingFenceQueueCoreBegin(&Queue, &Transaction);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    RtlZeroMemory(&Test, sizeof(Test));
    Test.Queue = &Queue;
    KeInitializeEvent(&Test.Attempting, NotificationEvent, FALSE);
    KeInitializeEvent(&Test.Done, NotificationEvent, FALSE);
    InitializeObjectAttributes(&Attributes,
                               NULL,
                               OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = PsCreateSystemThread(&Thread,
                                  THREAD_ALL_ACCESS,
                                  &Attributes,
                                  NULL,
                                  NULL,
                                  PagingFenceShutdownThread,
                                  &Test);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
    {
        DxgkPagingFenceQueueCoreAbort(&Transaction);
        return;
    }

    Status = WaitForPagingFenceTestEvent(&Test.Attempting);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = WaitForPagingFenceTestEventMilliseconds(&Test.Done, 100);
    ok_eq_hex(Status, STATUS_TIMEOUT);

    DxgkPagingFenceQueueCoreAbort(&Transaction);
    Status = WaitForPagingFenceTestEvent(&Test.Done);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkPagingFenceQueueCoreBegin(&Queue, &Transaction);
    ok_eq_hex(Status, STATUS_DELETE_PENDING);
    ZwClose(Thread);
}

START_TEST(DxgkPagingMultipass)
{
    TestSinglePass();
    TestMultiplePasses();
    TestProtocolViolations();
    TestPassCeiling();
    TestMonitoredSignalAppendPolicy();
    TestPagingFenceCommitAndRollback();
    TestPagingFenceSerializedAdmission();
    TestPagingFenceShutdownDrainsTransaction();
}

/* EOF */
