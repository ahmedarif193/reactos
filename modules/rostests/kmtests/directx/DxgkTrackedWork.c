/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgkrnl tracked GPU-work terminal transition tests
 */

#include <kmt_test.h>
#include "submit_reservation_core.h"
#include "tracked_work_core.h"

typedef enum _DXGK_TRACKED_WORK_TEST_EVENT
{
    DxgkTrackedWorkTestEventIncrement = 1,
    DxgkTrackedWorkTestEventDecrement = 2,
    DxgkTrackedWorkTestEventPublish = 3,
    DxgkTrackedWorkTestEventComplete = 4
} DXGK_TRACKED_WORK_TEST_EVENT;

typedef struct _DXGK_TRACKED_WORK_TEST_CONTEXT
{
    DXGK_TRACKED_WORK_CORE Core;
    LONG InFlight;
    ULONG Events[16];
    ULONG EventCount;
    volatile LONG WrongIrql;
    BOOLEAN RequireDispatch;
} DXGK_TRACKED_WORK_TEST_CONTEXT, *PDXGK_TRACKED_WORK_TEST_CONTEXT;

static VOID DxgkTrackedWorkTestRecord(_Inout_ PDXGK_TRACKED_WORK_TEST_CONTEXT Context, _In_ DXGK_TRACKED_WORK_TEST_EVENT Event)
{
    if (Context->RequireDispatch && KeGetCurrentIrql() != DISPATCH_LEVEL)
        InterlockedIncrement(&Context->WrongIrql);
    if (Context->EventCount < RTL_NUMBER_OF(Context->Events))
        Context->Events[Context->EventCount++] = Event;
}

static VOID NTAPI DxgkTrackedWorkTestAdjust(_In_opt_ PVOID OpaqueContext, _In_ LONG Delta)
{
    PDXGK_TRACKED_WORK_TEST_CONTEXT Context = OpaqueContext;

    Context->InFlight += Delta;
    DxgkTrackedWorkTestRecord(Context, Delta > 0 ? DxgkTrackedWorkTestEventIncrement : DxgkTrackedWorkTestEventDecrement);
}

static VOID NTAPI DxgkTrackedWorkTestPublish(_In_opt_ PVOID OpaqueContext)
{
    PDXGK_TRACKED_WORK_TEST_CONTEXT Context = OpaqueContext;

    DxgkTrackedWorkTestRecord(Context, DxgkTrackedWorkTestEventPublish);
}

static VOID NTAPI DxgkTrackedWorkTestComplete(_In_opt_ PVOID OpaqueContext)
{
    PDXGK_TRACKED_WORK_TEST_CONTEXT Context = OpaqueContext;

    DxgkTrackedWorkTestRecord(Context, DxgkTrackedWorkTestEventComplete);
}

static const DXGK_TRACKED_WORK_CALLBACKS DxgkTrackedWorkTestCallbacks = { DxgkTrackedWorkTestAdjust, DxgkTrackedWorkTestPublish, DxgkTrackedWorkTestComplete };

static VOID DxgkTrackedWorkTestInitialize(_Out_ PDXGK_TRACKED_WORK_TEST_CONTEXT Context, _In_ BOOLEAN DeviceWorkOwned)
{
    RtlZeroMemory(Context, sizeof(*Context));
    DxgkTrackedWorkCoreInitialize(&Context->Core, &DxgkTrackedWorkTestCallbacks, Context, DeviceWorkOwned);
}

static VOID DxgkTrackedWorkTestExpectEvent(_In_ const DXGK_TRACKED_WORK_TEST_CONTEXT *Context, _In_ ULONG Index, _In_ DXGK_TRACKED_WORK_TEST_EVENT Event)
{
    ok(Index < Context->EventCount, "event %lu exists (count %lu)\n", Index, Context->EventCount);
    if (Index < Context->EventCount)
        ok_eq_ulong(Context->Events[Index], Event);
}

static VOID DxgkTrackedWorkTestNormalOrder(VOID)
{
    DXGK_TRACKED_WORK_TEST_CONTEXT Context;
    BOOLEAN RetiredNow;

    DxgkTrackedWorkTestInitialize(&Context, TRUE);
    ok_bool_true(DxgkTrackedWorkCoreCommit(&Context.Core, FALSE, &RetiredNow), "prepared work commits");
    ok_bool_false(RetiredNow, "ordinary commit remains live");
    ok_eq_long(DxgkTrackedWorkCoreGetState(&Context.Core), DxgkTrackedWorkCommitted);
    ok_eq_long(Context.InFlight, 1);
    ok_eq_ulong(Context.EventCount, 1);
    DxgkTrackedWorkTestExpectEvent(&Context, 0, DxgkTrackedWorkTestEventIncrement);
    ok_bool_false(DxgkTrackedWorkCoreCommit(&Context.Core, FALSE, &RetiredNow), "duplicate commit loses");
    ok_eq_ulong(Context.EventCount, 1);
    ok_bool_true(DxgkTrackedWorkCoreRetire(&Context.Core), "committed work retires");
    ok_eq_long(DxgkTrackedWorkCoreGetState(&Context.Core), DxgkTrackedWorkRetired);
    ok_eq_long(Context.InFlight, 0);
    ok_eq_ulong(Context.EventCount, 4);
    DxgkTrackedWorkTestExpectEvent(&Context, 1, DxgkTrackedWorkTestEventDecrement);
    DxgkTrackedWorkTestExpectEvent(&Context, 2, DxgkTrackedWorkTestEventPublish);
    DxgkTrackedWorkTestExpectEvent(&Context, 3, DxgkTrackedWorkTestEventComplete);
    ok_bool_false(DxgkTrackedWorkCoreRetire(&Context.Core), "duplicate retire loses");
    ok_bool_false(DxgkTrackedWorkCoreCancel(&Context.Core), "cancel loses after retire");
    ok_eq_ulong(Context.EventCount, 4);
}

static VOID DxgkTrackedWorkTestCancellation(VOID)
{
    DXGK_TRACKED_WORK_TEST_CONTEXT Context;
    BOOLEAN RetiredNow;

    DxgkTrackedWorkTestInitialize(&Context, TRUE);
    ok_bool_true(DxgkTrackedWorkCoreCancel(&Context.Core), "prepared owned work cancels");
    ok_eq_long(DxgkTrackedWorkCoreGetState(&Context.Core), DxgkTrackedWorkCancelled);
    ok_eq_long(Context.InFlight, 0);
    ok_eq_ulong(Context.EventCount, 1);
    DxgkTrackedWorkTestExpectEvent(&Context, 0, DxgkTrackedWorkTestEventComplete);
    ok_bool_false(DxgkTrackedWorkCoreCancel(&Context.Core), "duplicate prepared cancel loses");
    ok_bool_false(DxgkTrackedWorkCoreRetire(&Context.Core), "late retire loses after prepared cancel");

    DxgkTrackedWorkTestInitialize(&Context, TRUE);
    ok_bool_true(DxgkTrackedWorkCoreCommit(&Context.Core, FALSE, &RetiredNow), "work commits before cancellation");
    ok_bool_true(DxgkTrackedWorkCoreCancel(&Context.Core), "cancel wins against committed work");
    ok_eq_long(Context.InFlight, 0);
    ok_eq_ulong(Context.EventCount, 3);
    DxgkTrackedWorkTestExpectEvent(&Context, 0, DxgkTrackedWorkTestEventIncrement);
    DxgkTrackedWorkTestExpectEvent(&Context, 1, DxgkTrackedWorkTestEventDecrement);
    DxgkTrackedWorkTestExpectEvent(&Context, 2, DxgkTrackedWorkTestEventComplete);
    ok_bool_false(DxgkTrackedWorkCoreRetire(&Context.Core), "retire loses after committed cancellation");
    ok_eq_ulong(Context.EventCount, 3);
}

static VOID DxgkTrackedWorkTestPrecompleted(VOID)
{
    DXGK_TRACKED_WORK_TEST_CONTEXT Context;
    BOOLEAN RetiredNow;

    DxgkTrackedWorkTestInitialize(&Context, TRUE);
    ok_bool_true(DxgkTrackedWorkCoreCommit(&Context.Core, TRUE, &RetiredNow), "already-complete work commits");
    ok_bool_true(RetiredNow, "already-complete commit retires atomically");
    ok_eq_long(DxgkTrackedWorkCoreGetState(&Context.Core), DxgkTrackedWorkRetired);
    ok_eq_long(Context.InFlight, 0);
    ok_eq_ulong(Context.EventCount, 4);
    DxgkTrackedWorkTestExpectEvent(&Context, 0, DxgkTrackedWorkTestEventIncrement);
    DxgkTrackedWorkTestExpectEvent(&Context, 1, DxgkTrackedWorkTestEventDecrement);
    DxgkTrackedWorkTestExpectEvent(&Context, 2, DxgkTrackedWorkTestEventPublish);
    DxgkTrackedWorkTestExpectEvent(&Context, 3, DxgkTrackedWorkTestEventComplete);
    ok_bool_false(DxgkTrackedWorkCoreCommit(&Context.Core, TRUE, &RetiredNow), "duplicate precompleted commit loses");
    ok_bool_false(RetiredNow, "losing commit reports no new retirement");
    ok_eq_ulong(Context.EventCount, 4);
}

static VOID DxgkTrackedWorkTestOwnership(VOID)
{
    DXGK_TRACKED_WORK_TEST_CONTEXT Context;
    BOOLEAN RetiredNow;

    DxgkTrackedWorkTestInitialize(&Context, FALSE);
    ok_bool_false(DxgkTrackedWorkCoreOwnsDeviceWork(&Context.Core), "external work begins unowned");
    ok_bool_false(DxgkTrackedWorkCoreOwnsExternalCleanup(&Context.Core), "prepared work does not own external cleanup before transfer");
    ok_bool_true(DxgkTrackedWorkCoreCancel(&Context.Core), "unowned work cancels");
    ok_eq_ulong(Context.EventCount, 0);
    ok_bool_false(DxgkTrackedWorkCoreClaimDeviceWork(&Context.Core), "terminal work cannot be claimed");
    ok_bool_false(DxgkTrackedWorkCoreClaimExternalCleanup(&Context.Core), "terminal work cannot claim external cleanup");
    ok_bool_false(DxgkTrackedWorkCoreOwnsExternalCleanup(&Context.Core), "pre-transfer cancellation leaves external cleanup with the caller");

    DxgkTrackedWorkTestInitialize(&Context, FALSE);
    ok_bool_true(DxgkTrackedWorkCoreClaimDeviceWork(&Context.Core), "activated external work transfers ownership");
    ok_bool_true(DxgkTrackedWorkCoreOwnsDeviceWork(&Context.Core), "claimed external work is owned");
    ok_bool_true(DxgkTrackedWorkCoreClaimExternalCleanup(&Context.Core), "queue adoption transfers external cleanup");
    ok_bool_true(DxgkTrackedWorkCoreOwnsExternalCleanup(&Context.Core), "adopted work owns external cleanup");
    ok_bool_true(DxgkTrackedWorkCoreClaimDeviceWork(&Context.Core), "ownership claim is idempotent while prepared");
    ok_bool_true(DxgkTrackedWorkCoreCommit(&Context.Core, FALSE, &RetiredNow), "claimed external work commits");
    ok_bool_true(DxgkTrackedWorkCoreRetire(&Context.Core), "claimed external work retires");
    ok_eq_ulong(Context.EventCount, 4);
    DxgkTrackedWorkTestExpectEvent(&Context, 3, DxgkTrackedWorkTestEventComplete);
    ok_bool_false(DxgkTrackedWorkCoreClaimDeviceWork(&Context.Core), "retired work cannot be reclaimed");

    DxgkTrackedWorkTestInitialize(&Context, FALSE);
    ok_bool_true(DxgkTrackedWorkCoreCommit(&Context.Core, FALSE, &RetiredNow), "direct submission commit transfers cleanup ownership");
    ok_bool_true(DxgkTrackedWorkCoreOwnsExternalCleanup(&Context.Core), "committed work owns external cleanup without prior queue adoption");
    ok_bool_true(DxgkTrackedWorkCoreCancel(&Context.Core), "committed work can cancel at teardown");
    ok_bool_true(DxgkTrackedWorkCoreOwnsExternalCleanup(&Context.Core), "terminal cancellation preserves transferred cleanup ownership");
}

static VOID DxgkTrackedWorkTestDispatchSafety(VOID)
{
    DXGK_TRACKED_WORK_TEST_CONTEXT CancelContext;
    DXGK_TRACKED_WORK_TEST_CONTEXT Context;
    KSPIN_LOCK OuterLock;
    BOOLEAN CancelSucceeded;
    BOOLEAN ClaimSucceeded;
    BOOLEAN CommitSucceeded;
    BOOLEAN CleanupClaimed;
    BOOLEAN CleanupOwned;
    BOOLEAN OwnsDeviceWork;
    BOOLEAN RetiredNow;
    BOOLEAN RetireSucceeded;
    DXGK_TRACKED_WORK_STATE State;
    KIRQL OldIrql;

    DxgkTrackedWorkTestInitialize(&Context, TRUE);
    Context.RequireDispatch = TRUE;
    DxgkTrackedWorkTestInitialize(&CancelContext, FALSE);
    CancelContext.RequireDispatch = TRUE;
    KeInitializeSpinLock(&OuterLock);
    KeAcquireSpinLock(&OuterLock, &OldIrql);
    CommitSucceeded = DxgkTrackedWorkCoreCommit(&Context.Core, FALSE, &RetiredNow);
    RetireSucceeded = DxgkTrackedWorkCoreRetire(&Context.Core);
    State = DxgkTrackedWorkCoreGetState(&Context.Core);
    OwnsDeviceWork = DxgkTrackedWorkCoreOwnsDeviceWork(&Context.Core);
    ClaimSucceeded = DxgkTrackedWorkCoreClaimDeviceWork(&CancelContext.Core);
    CleanupClaimed = DxgkTrackedWorkCoreClaimExternalCleanup(&CancelContext.Core);
    CancelSucceeded = DxgkTrackedWorkCoreCancel(&CancelContext.Core);
    CleanupOwned = DxgkTrackedWorkCoreOwnsExternalCleanup(&CancelContext.Core);
    KeReleaseSpinLock(&OuterLock, OldIrql);
    ok_bool_true(CommitSucceeded, "commit is safe below an outer DISPATCH spinlock");
    ok_bool_true(RetireSucceeded, "retire is safe below an outer DISPATCH spinlock");
    ok_eq_long(State, DxgkTrackedWorkRetired);
    ok_bool_true(OwnsDeviceWork, "ownership query is DISPATCH-safe");
    ok_bool_true(ClaimSucceeded, "ownership claim is DISPATCH-safe");
    ok_bool_true(CleanupClaimed, "external cleanup claim is DISPATCH-safe");
    ok_bool_true(CancelSucceeded, "cancel is safe below an outer DISPATCH spinlock");
    ok_bool_true(CleanupOwned, "external cleanup ownership query is DISPATCH-safe");
    ok_eq_long(Context.WrongIrql, 0);
    ok_eq_ulong(Context.EventCount, 4);
    ok_eq_long(CancelContext.WrongIrql, 0);
    ok_eq_ulong(CancelContext.EventCount, 1);
    DxgkTrackedWorkTestExpectEvent(&CancelContext, 0, DxgkTrackedWorkTestEventComplete);
}

static VOID DxgkTrackedWorkTestReservationDrainInvariant(VOID)
{
    LONG ActiveReservations = 0;
    BOOLEAN ChangeEvent;

    ok_bool_true(DxgkSubmitReservationCoreIsDrainedLocked(&ActiveReservations), "zero reservations begin drained");
    ok_bool_true(DxgkSubmitReservationCoreTryAcquireLocked(&ActiveReservations, FALSE, &ChangeEvent), "first reservation is admitted");
    ok_bool_true(ChangeEvent, "zero-to-one transition clears the drained event");
    ok_eq_long(ActiveReservations, 1);
    ok_bool_false(DxgkSubmitReservationCoreIsDrainedLocked(&ActiveReservations), "one reservation is not drained");
    ok_bool_true(DxgkSubmitReservationCoreTryAcquireLocked(&ActiveReservations, FALSE, &ChangeEvent), "second reservation is admitted");
    ok_bool_false(ChangeEvent, "nonzero admission leaves the event clear");
    ok_eq_long(ActiveReservations, 2);
    ok_bool_true(DxgkSubmitReservationCoreReleaseLocked(&ActiveReservations, &ChangeEvent), "first reservation releases");
    ok_bool_false(ChangeEvent, "nonfinal release leaves the event clear");
    ok_eq_long(ActiveReservations, 1);
    ok_bool_true(DxgkSubmitReservationCoreReleaseLocked(&ActiveReservations, &ChangeEvent), "final reservation releases");
    ok_bool_true(ChangeEvent, "one-to-zero transition signals the drained event");
    ok_eq_long(ActiveReservations, 0);
    ok_bool_true(DxgkSubmitReservationCoreIsDrainedLocked(&ActiveReservations), "final release is drained");
    ok_bool_false(DxgkSubmitReservationCoreTryAcquireLocked(&ActiveReservations, TRUE, &ChangeEvent), "stopping blocks a new reservation");
    ok_bool_false(ChangeEvent, "rejected admission leaves the drained event signaled");
    ok_eq_long(ActiveReservations, 0);
    ok_bool_false(DxgkSubmitReservationCoreReleaseLocked(&ActiveReservations, &ChangeEvent), "reservation count cannot underflow");
    ok_bool_false(ChangeEvent, "underflow rejection leaves the event unchanged");
    ok_eq_long(ActiveReservations, 0);
}

static VOID DxgkTrackedWorkTestSubmissionAccounting(VOID)
{
    DXGK_SUBMISSION_ACCOUNTING Accounting;
    DXGK_SUBMISSION_ACCOUNTING OtherAccounting;
    LONG DeviceCount;
    LONG OtherDeviceCount;
    LONG ProcessCount;

    DeviceCount = 0;
    ProcessCount = 0;
    DxgkSubmissionAccountingInitialize(&Accounting);
    ok_eq_long(DxgkSubmissionAccountingGetState(&Accounting), DxgkSubmissionUnaccounted);
    ok_bool_true(DxgkSubmissionAccountingTryPrechargeLocked(&Accounting, &DeviceCount, &ProcessCount, 2, 3), "prepared submission is precharged");
    ok_eq_long(DxgkSubmissionAccountingGetState(&Accounting), DxgkSubmissionPrecharged);
    ok_eq_long(DeviceCount, 1);
    ok_eq_long(ProcessCount, 1);
    ok_bool_true(DxgkSubmissionAccountingCommitLocked(&Accounting, &DeviceCount, &ProcessCount), "precharged submission commits");
    ok_eq_long(DxgkSubmissionAccountingGetState(&Accounting), DxgkSubmissionCommitted);
    ok_eq_long(DeviceCount, 1);
    ok_eq_long(ProcessCount, 1);
    ok_bool_true(DxgkSubmissionAccountingRelease(&Accounting, &DeviceCount, &ProcessCount), "committed submission releases");
    ok_eq_long(DxgkSubmissionAccountingGetState(&Accounting), DxgkSubmissionReleased);
    ok_eq_long(DeviceCount, 0);
    ok_eq_long(ProcessCount, 0);
    ok_bool_false(DxgkSubmissionAccountingRelease(&Accounting, &DeviceCount, &ProcessCount), "duplicate terminal release loses");
    ok_eq_long(DeviceCount, 0);
    ok_eq_long(ProcessCount, 0);

    DxgkSubmissionAccountingInitialize(&Accounting);
    ok_bool_true(DxgkSubmissionAccountingTryPrechargeLocked(&Accounting, &DeviceCount, &ProcessCount, 2, 3), "queued prepared submission is precharged");
    ok_bool_true(DxgkSubmissionAccountingRelease(&Accounting, &DeviceCount, &ProcessCount), "prepared cancellation releases its precharge");
    ok_eq_long(DeviceCount, 0);
    ok_eq_long(ProcessCount, 0);

    DxgkSubmissionAccountingInitialize(&Accounting);
    ok_bool_true(DxgkSubmissionAccountingCommitLocked(&Accounting, &DeviceCount, &ProcessCount), "non-precharged internal submission commits");
    ok_eq_long(DeviceCount, 1);
    ok_eq_long(ProcessCount, 1);
    ok_bool_true(DxgkSubmissionAccountingRelease(&Accounting, &DeviceCount, &ProcessCount), "immediately completed internal submission releases");
    ok_eq_long(DeviceCount, 0);
    ok_eq_long(ProcessCount, 0);

    DeviceCount = 2;
    ProcessCount = 1;
    DxgkSubmissionAccountingInitialize(&Accounting);
    ok_bool_false(DxgkSubmissionAccountingTryPrechargeLocked(&Accounting, &DeviceCount, &ProcessCount, 2, 4), "device limit rejects admission");
    ok_eq_long(DxgkSubmissionAccountingGetState(&Accounting), DxgkSubmissionUnaccounted);
    ok_eq_long(DeviceCount, 2);
    ok_eq_long(ProcessCount, 1);

    DeviceCount = 0;
    OtherDeviceCount = 0;
    ProcessCount = 1;
    DxgkSubmissionAccountingInitialize(&Accounting);
    DxgkSubmissionAccountingInitialize(&OtherAccounting);
    ok_bool_true(DxgkSubmissionAccountingTryPrechargeLocked(&Accounting, &DeviceCount, &ProcessCount, 2, 2), "second device consumes the final process slot");
    ok_bool_false(DxgkSubmissionAccountingTryPrechargeLocked(&OtherAccounting, &OtherDeviceCount, &ProcessCount, 2, 2), "process limit spans distinct devices");
    ok_eq_long(DeviceCount, 1);
    ok_eq_long(OtherDeviceCount, 0);
    ok_eq_long(ProcessCount, 2);
    ok_bool_true(DxgkSubmissionAccountingRelease(&Accounting, &DeviceCount, &ProcessCount), "process-wide charge releases exactly once");
    ok_eq_long(DeviceCount, 0);
    ok_eq_long(ProcessCount, 1);
}

static VOID DxgkTrackedWorkTestSubmissionResidencyPin(VOID)
{
    LONG PinCount;

    PinCount = 0;
    ok_bool_false(DxgkSubmissionResidencyPinIsHeld(&PinCount), "zero submission pins are unpinned");
    ok_bool_true(DxgkSubmissionResidencyPinTryAcquire(&PinCount), "first submission pin is acquired");
    ok_bool_true(DxgkSubmissionResidencyPinTryAcquire(&PinCount), "duplicate resource use acquires an independent pin");
    ok_bool_true(DxgkSubmissionResidencyPinIsHeld(&PinCount), "positive submission pin count is held");
    ok_eq_long(PinCount, 2);
    ok_bool_true(DxgkSubmissionResidencyPinRelease(&PinCount), "first submission pin releases");
    ok_bool_true(DxgkSubmissionResidencyPinRelease(&PinCount), "final submission pin releases");
    ok_bool_false(DxgkSubmissionResidencyPinIsHeld(&PinCount), "final release unpins placement");
    ok_bool_false(DxgkSubmissionResidencyPinRelease(&PinCount), "submission pin cannot underflow");
    ok_eq_long(PinCount, 0);

    PinCount = MAXLONG;
    ok_bool_false(DxgkSubmissionResidencyPinTryAcquire(&PinCount), "submission pin cannot overflow");
    ok_eq_long(PinCount, MAXLONG);
    PinCount = -1;
    ok_bool_false(DxgkSubmissionResidencyPinTryAcquire(&PinCount), "corrupt negative submission pin count is rejected");
    ok_bool_true(DxgkSubmissionResidencyPinIsHeld(&PinCount), "corrupt negative submission pin count fails closed");
    ok_eq_long(PinCount, -1);
}

START_TEST(DxgkTrackedWork)
{
    DxgkTrackedWorkTestNormalOrder();
    DxgkTrackedWorkTestCancellation();
    DxgkTrackedWorkTestPrecompleted();
    DxgkTrackedWorkTestOwnership();
    DxgkTrackedWorkTestDispatchSafety();
    DxgkTrackedWorkTestReservationDrainInvariant();
    DxgkTrackedWorkTestSubmissionAccounting();
    DxgkTrackedWorkTestSubmissionResidencyPin();
}
