/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Residency budgets, reservations and trim targets
 *
 * Budget accounting decides when the memory manager starts evicting.  Usage
 * that wraps on release looks permanently over budget and the manager trims
 * forever; a trim target computed short never gets back under it.
 */

#include <kmt_test.h>
#include "residency_core.h"

#define MB (1024ULL * 1024ULL)

static VOID TestInitialization(VOID)
{
    DXGK_RESIDENCY_BUDGET Budget;

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 256 * MB, 512 * MB, 64 * MB); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Budget.Budget, 256 * MB);
    ok_eq_ulonglong(Budget.Maximum, 512 * MB);
    ok_eq_ulonglong(Budget.Reservation, 64 * MB);
    ok_eq_ulonglong(Budget.CurrentUsage, 0ULL);

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 256 * MB, 256 * MB, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 0, 0, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 128 * MB, 64 * MB, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_bool_false(Budget.Initialized, "current budget cannot exceed the hard maximum");
    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 64 * MB, 64 * MB, 128 * MB); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_bool_false(Budget.Initialized, "a rejected budget is not usable");
}

static VOID TestChargeAndRelease(VOID)
{
    DXGK_RESIDENCY_BUDGET Budget;

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 100, 100, 20); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 30); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Budget.CurrentUsage, 30ULL);
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 30); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Budget.CurrentUsage, 60ULL);
    { NTSTATUS Observed = DxgkResidencyCoreBudgetRelease(&Budget, 60); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Budget.CurrentUsage, 0ULL);

    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /*
     * Releasing more than was charged would wrap usage to a huge value and
     * the process would look permanently over budget, trimming forever.
     */
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 10); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetRelease(&Budget, 11); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulonglong(Budget.CurrentUsage, 10ULL);

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, MAXULONGLONG, MAXULONGLONG, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, MAXULONGLONG); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 1); ok_eq_hex(Observed, STATUS_INTEGER_OVERFLOW); }
}

static VOID TestPressureAndTrim(VOID)
{
    DXGK_RESIDENCY_BUDGET Budget;
    ULONGLONG BytesToTrim;

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 100, 150, 20); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 90); ok_eq_hex(Observed, STATUS_SUCCESS); }
    BytesToTrim = MAXULONGLONG;
    { NTSTATUS Observed = DxgkResidencyCoreBudgetTryCharge(&Budget, 20, FALSE, &BytesToTrim); ok_eq_hex(Observed, STATUS_GRAPHICS_NO_VIDEO_MEMORY); }
    ok_eq_ulonglong(BytesToTrim, 10ULL);
    ok_eq_ulonglong(Budget.CurrentUsage, 90ULL);

    /* CantTrimFurther may cross the current budget, but never the hard cap. */
    BytesToTrim = MAXULONGLONG;
    { NTSTATUS Observed = DxgkResidencyCoreBudgetTryCharge(&Budget, 20, TRUE, &BytesToTrim); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(BytesToTrim, 0ULL);
    ok_eq_ulonglong(Budget.CurrentUsage, 110ULL);
    ok_bool_true(DxgkResidencyCoreBudgetIsOver(&Budget), "over budget");
    { ULONGLONG Observed = DxgkResidencyCoreBudgetTrimTarget(&Budget); ok_eq_ulonglong(Observed, 10ULL); }

    BytesToTrim = MAXULONGLONG;
    { NTSTATUS Observed = DxgkResidencyCoreBudgetTryCharge(&Budget, 50, TRUE, &BytesToTrim); ok_eq_hex(Observed, STATUS_GRAPHICS_NO_VIDEO_MEMORY); }
    ok_eq_ulonglong(BytesToTrim, 10ULL);
    ok_eq_ulonglong(Budget.CurrentUsage, 110ULL);

    { NTSTATUS Observed = DxgkResidencyCoreBudgetRelease(&Budget, 20); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(DxgkResidencyCoreBudgetIsOver(&Budget), "back within budget");
    { ULONGLONG Observed = DxgkResidencyCoreBudgetTrimTarget(&Budget); ok_eq_ulonglong(Observed, 0ULL); }

    /* Usage under the reservation is protected from pressure trimming. */
    { NTSTATUS Observed = DxgkResidencyCoreBudgetRelease(&Budget, 80); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Budget.CurrentUsage, 10ULL);
    ok_bool_true(DxgkResidencyCoreBudgetIsProtected(&Budget), "below reservation");
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 10); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkResidencyCoreBudgetIsProtected(&Budget), "exactly at reservation");
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 1); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(DxgkResidencyCoreBudgetIsProtected(&Budget), "above reservation");

    { NTSTATUS Observed = DxgkResidencyCoreBudgetSetLimits(&Budget, 120, 150); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetSetReservation(&Budget, 140); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetSetReservation(&Budget, 151); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestUninitialized(VOID)
{
    DXGK_RESIDENCY_BUDGET Budget;

    RtlZeroMemory(&Budget, sizeof(Budget));
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 1); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetRelease(&Budget, 1); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }
    ok_bool_false(DxgkResidencyCoreBudgetIsOver(&Budget), "no budget, no pressure");
    { ULONGLONG Observed = DxgkResidencyCoreBudgetTrimTarget(&Budget); ok_eq_ulonglong(Observed, 0ULL); }
}

static VOID TestProcessExitAdmission(VOID)
{
    DXGK_RESIDENCY_PROCESS_ADMISSION Admission;

    /* A normal request keeps the wrapper alive through publication, then the
     * final leave makes it immediately retireable. */
    DxgkResidencyCoreProcessAdmissionInitialize(&Admission);
    ok_bool_true(
        DxgkResidencyCoreProcessAdmissionTryEnter(&Admission, FALSE),
        "live process enters");
    ok_bool_true(
        DxgkResidencyCoreProcessAdmissionCanPublish(&Admission),
        "live process publishes");
    ok_bool_true(
        DxgkResidencyCoreProcessAdmissionLeave(&Admission),
        "normal last leave retires the gate");
    ok_eq_ulong(Admission.ActiveEntrants, 0UL);
    ok_bool_false(
        DxgkResidencyCoreProcessAdmissionCanPublish(&Admission),
        "zero-entrant gate cannot publish");

    /*
     * Cleanup before the first entrant is represented by the process object's
     * persistent exit state.  No ledger-publication window is opened.
     */
    DxgkResidencyCoreProcessAdmissionInitialize(&Admission);
    ok_bool_false(
        DxgkResidencyCoreProcessAdmissionTryEnter(&Admission, TRUE),
        "an exited process cannot enter");
    ok_eq_ulong(Admission.ActiveEntrants, 0UL);

    /*
     * If cleanup runs after admission but before publication, its independent
     * tombstone invalidates the entrant's final under-lock recheck.
     */
    DxgkResidencyCoreProcessAdmissionInitialize(&Admission);
    ok_bool_true(
        DxgkResidencyCoreProcessAdmissionTryEnter(&Admission, FALSE),
        "request enters before cleanup");
    ok_bool_true(
        DxgkResidencyCoreProcessAdmissionCanPublish(&Admission),
        "publication allowed before cleanup");
    DxgkResidencyCoreProcessAdmissionMarkExiting(&Admission);
    ok_bool_false(
        DxgkResidencyCoreProcessAdmissionCanPublish(&Admission),
        "cleanup closes publication");
    ok_bool_true(
        DxgkResidencyCoreProcessAdmissionLeave(&Admission),
        "last entrant retires the gate");
    ok_bool_false(
        DxgkResidencyCoreProcessAdmissionTryEnter(&Admission, FALSE),
        "the tombstone rejects later entrants");
}

static VOID TestSharedProcessCharges(VOID)
{
    DXGK_RESIDENCY_PROCESS_CHARGE_STATE ProcessA;
    DXGK_RESIDENCY_PROCESS_CHARGE_STATE ProcessB;
    DXGK_RESIDENCY_BUDGET BudgetA;
    DXGK_RESIDENCY_BUDGET BudgetB;
    ULONGLONG BytesToTrim;
    BOOLEAN BudgetTransition;
    NTSTATUS Status;

    DxgkResidencyCoreProcessChargeInitialize(&ProcessA);
    DxgkResidencyCoreProcessChargeInitialize(&ProcessB);
    Status = DxgkResidencyCoreBudgetInitialize(&BudgetA, 128, 128, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkResidencyCoreBudgetInitialize(&BudgetB, 128, 128, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* Process A owns the first reference and therefore the first charge. */
    Status = DxgkResidencyCoreProcessChargePlanAcquire(
                 &ProcessA,
                 1,
                 &BudgetTransition);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(BudgetTransition, "A needs its first placement charge");
    Status = DxgkResidencyCoreBudgetTryCharge(
                 &BudgetA,
                 64,
                 FALSE,
                 &BytesToTrim);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkResidencyCoreProcessChargeCommitAcquire(
                 &ProcessA,
                 1,
                 TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /*
     * The same physical backing is already resident when process B opens it.
     * B still receives its own one-time usage charge.
     */
    Status = DxgkResidencyCoreProcessChargePlanAcquire(
                 &ProcessB,
                 1,
                 &BudgetTransition);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(BudgetTransition, "B needs an independent charge");
    Status = DxgkResidencyCoreBudgetTryCharge(
                 &BudgetB,
                 64,
                 FALSE,
                 &BytesToTrim);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = DxgkResidencyCoreProcessChargeCommitAcquire(
                 &ProcessB,
                 1,
                 TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(BudgetA.CurrentUsage, 64ULL);
    ok_eq_ulonglong(BudgetB.CurrentUsage, 64ULL);

    /* More aliases/references in A do not multiply its physical usage. */
    Status = DxgkResidencyCoreProcessChargePlanAcquire(
                 &ProcessA,
                 2,
                 &BudgetTransition);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_false(BudgetTransition, "A is already charged");
    Status = DxgkResidencyCoreProcessChargeCommitAcquire(
                 &ProcessA,
                 2,
                 FALSE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(BudgetA.CurrentUsage, 64ULL);

    /* B's last reference releases only B; A remains fully accounted. */
    Status = DxgkResidencyCoreProcessChargeRelease(
                 &ProcessB,
                 1,
                 &BudgetTransition);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(BudgetTransition, "B released its last reference");
    Status = DxgkResidencyCoreBudgetRelease(&BudgetB, 64);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(BudgetB.CurrentUsage, 0ULL);
    ok_eq_ulonglong(BudgetA.CurrentUsage, 64ULL);

    Status = DxgkResidencyCoreProcessChargeRelease(
                 &ProcessA,
                 2,
                 &BudgetTransition);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_false(BudgetTransition, "A still owns its implicit reference");
    Status = DxgkResidencyCoreProcessChargeRelease(
                 &ProcessA,
                 1,
                 &BudgetTransition);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(BudgetTransition, "A released its final reference");
    Status = DxgkResidencyCoreBudgetRelease(&BudgetA, 64);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(BudgetA.CurrentUsage, 0ULL);
}

START_TEST(DxgkResidencyBudget)
{
    TestInitialization();
    TestChargeAndRelease();
    TestPressureAndTrim();
    TestUninitialized();
    TestProcessExitAdmission();
    TestSharedProcessCharges();
}

/* EOF */
