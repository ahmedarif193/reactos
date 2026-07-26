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

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 256 * MB, 64 * MB); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Budget.Budget, 256 * MB);
    ok_eq_ulonglong(Budget.Reservation, 64 * MB);
    ok_eq_ulonglong(Budget.CurrentUsage, 0ULL);

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 256 * MB, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 0, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* Guaranteeing more than the budget allows is a contradiction. */
    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 64 * MB, 128 * MB); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_bool_false(Budget.Initialized, "a rejected budget is not usable");
}

static VOID TestChargeAndRelease(VOID)
{
    DXGK_RESIDENCY_BUDGET Budget;

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 100, 20); ok_eq_hex(Observed, STATUS_SUCCESS); }
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

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, MAXULONGLONG, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, MAXULONGLONG); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 1); ok_eq_hex(Observed, STATUS_INTEGER_OVERFLOW); }
}

static VOID TestPressureAndTrim(VOID)
{
    DXGK_RESIDENCY_BUDGET Budget;

    { NTSTATUS Observed = DxgkResidencyCoreBudgetInitialize(&Budget, 100, 20); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 100); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(DxgkResidencyCoreBudgetIsOver(&Budget), "exactly at budget is not over");
    { ULONGLONG Observed = DxgkResidencyCoreBudgetTrimTarget(&Budget); ok_eq_ulonglong(Observed, 0ULL); }

    /*
     * Going over budget is allowed: the manager reacts by trimming rather
     * than by failing the residency request, which is what lets a working set
     * exceed its budget briefly instead of the app failing outright.
     */
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 50); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkResidencyCoreBudgetIsOver(&Budget), "over budget");
    { ULONGLONG Observed = DxgkResidencyCoreBudgetTrimTarget(&Budget); ok_eq_ulonglong(Observed, 50ULL); }

    { NTSTATUS Observed = DxgkResidencyCoreBudgetRelease(&Budget, 50); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(DxgkResidencyCoreBudgetIsOver(&Budget), "back within budget");
    { ULONGLONG Observed = DxgkResidencyCoreBudgetTrimTarget(&Budget); ok_eq_ulonglong(Observed, 0ULL); }

    /* Usage under the reservation is protected from pressure trimming. */
    { NTSTATUS Observed = DxgkResidencyCoreBudgetRelease(&Budget, 90); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Budget.CurrentUsage, 10ULL);
    ok_bool_true(DxgkResidencyCoreBudgetIsProtected(&Budget), "below reservation");
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 10); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkResidencyCoreBudgetIsProtected(&Budget), "exactly at reservation");
    { NTSTATUS Observed = DxgkResidencyCoreBudgetCharge(&Budget, 1); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(DxgkResidencyCoreBudgetIsProtected(&Budget), "above reservation");
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

START_TEST(DxgkResidencyBudget)
{
    TestInitialization();
    TestChargeAndRelease();
    TestPressureAndTrim();
    TestUninitialized();
}

/* EOF */
