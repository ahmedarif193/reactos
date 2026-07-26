/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Per-device residency references and eviction eligibility
 *
 * Residency is reference counted per device.  Evicting something a running
 * packet is reading corrupts that packet's results, so a submission pin
 * outranks every residency decision.
 */

#include <kmt_test.h>
#include "residency_core.h"

#define DEVICE_A 0x1001ULL
#define DEVICE_B 0x1002ULL

static VOID TestExplicitReferences(VOID)
{
    DXGK_RESIDENCY_REFS Refs;

    DxgkResidencyCoreRefsInitialize(&Refs, DEVICE_A, FALSE);
    ok_eq_ulong(Refs.TotalCount, 0UL);
    ok_bool_true(DxgkResidencyCoreIsEvictable(&Refs), "nothing holds it");

    { NTSTATUS Observed = DxgkResidencyCoreAcquire(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { ULONG Observed = DxgkResidencyCoreQueryDevice(&Refs, DEVICE_A); ok_eq_ulong(Observed, 1UL); }
    ok_bool_false(DxgkResidencyCoreIsEvictable(&Refs), "a reference pins it");

    /* References nest per device. */
    { NTSTATUS Observed = DxgkResidencyCoreAcquire(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { ULONG Observed = DxgkResidencyCoreQueryDevice(&Refs, DEVICE_A); ok_eq_ulong(Observed, 2UL); }
    { NTSTATUS Observed = DxgkResidencyCoreAcquire(&Refs, DEVICE_B); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { ULONG Observed = DxgkResidencyCoreQueryDevice(&Refs, DEVICE_B); ok_eq_ulong(Observed, 1UL); }
    ok_eq_ulong(Refs.TotalCount, 3UL);

    /* One device releasing must not free another device's hold. */
    { NTSTATUS Observed = DxgkResidencyCoreRelease(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreRelease(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { ULONG Observed = DxgkResidencyCoreQueryDevice(&Refs, DEVICE_A); ok_eq_ulong(Observed, 0UL); }
    ok_bool_false(DxgkResidencyCoreIsEvictable(&Refs), "device B still holds it");
    { NTSTATUS Observed = DxgkResidencyCoreRelease(&Refs, DEVICE_B); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkResidencyCoreIsEvictable(&Refs), "now evictable");

    /* An unbalanced release would let the next evict run under a live ref. */
    { NTSTATUS Observed = DxgkResidencyCoreRelease(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkResidencyCoreAcquire(&Refs, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestImplicitReference(VOID)
{
    DXGK_RESIDENCY_REFS Refs;

    /*
     * A created-resident allocation is resident without anyone having called
     * MakeResident, so it owes one implicit reference to its creating device.
     * Without it, the first Evict from that device would fail with no owner.
     */
    DxgkResidencyCoreRefsInitialize(&Refs, DEVICE_A, TRUE);
    ok_eq_ulong(Refs.TotalCount, 1UL);
    ok_bool_true(Refs.ImplicitReferenceHeld, "implicit reference held");
    { ULONG Observed = DxgkResidencyCoreQueryDevice(&Refs, DEVICE_A); ok_eq_ulong(Observed, 1UL); }
    { ULONG Observed = DxgkResidencyCoreQueryDevice(&Refs, DEVICE_B); ok_eq_ulong(Observed, 0UL); }
    ok_bool_false(DxgkResidencyCoreIsEvictable(&Refs), "not evictable while implicitly held");

    /* Another device cannot consume the creator's implicit reference. */
    { NTSTATUS Observed = DxgkResidencyCoreRelease(&Refs, DEVICE_B); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_bool_true(Refs.ImplicitReferenceHeld, "still held");

    { NTSTATUS Observed = DxgkResidencyCoreRelease(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(Refs.ImplicitReferenceHeld, "consumed exactly once");
    ok_bool_true(DxgkResidencyCoreIsEvictable(&Refs), "evictable after the implicit release");
    { NTSTATUS Observed = DxgkResidencyCoreRelease(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* Explicit references are consumed before the implicit one. */
    DxgkResidencyCoreRefsInitialize(&Refs, DEVICE_A, TRUE);
    { NTSTATUS Observed = DxgkResidencyCoreAcquire(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { ULONG Observed = DxgkResidencyCoreQueryDevice(&Refs, DEVICE_A); ok_eq_ulong(Observed, 2UL); }
    { NTSTATUS Observed = DxgkResidencyCoreRelease(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(Refs.ImplicitReferenceHeld, "implicit survives the explicit release");
    { NTSTATUS Observed = DxgkResidencyCoreRelease(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_false(Refs.ImplicitReferenceHeld, "then the implicit one goes");
}

static VOID TestDeviceTeardown(VOID)
{
    DXGK_RESIDENCY_REFS Refs;

    DxgkResidencyCoreRefsInitialize(&Refs, DEVICE_A, TRUE);
    { NTSTATUS Observed = DxgkResidencyCoreAcquire(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreAcquire(&Refs, DEVICE_A); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkResidencyCoreAcquire(&Refs, DEVICE_B); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* A device going away takes its implicit reference with it, or the
     * allocation stays resident forever with no owner left to release it. */
    { ULONG Observed = DxgkResidencyCoreReleaseAllForDevice(&Refs, DEVICE_A); ok_eq_ulong(Observed, 3UL); }
    ok_bool_false(Refs.ImplicitReferenceHeld, "implicit released at teardown");
    { ULONG Observed = DxgkResidencyCoreQueryDevice(&Refs, DEVICE_A); ok_eq_ulong(Observed, 0UL); }
    { ULONG Observed = DxgkResidencyCoreQueryDevice(&Refs, DEVICE_B); ok_eq_ulong(Observed, 1UL); }
    ok_bool_false(DxgkResidencyCoreIsEvictable(&Refs), "device B still holds it");
    { ULONG Observed = DxgkResidencyCoreReleaseAllForDevice(&Refs, DEVICE_B); ok_eq_ulong(Observed, 1UL); }
    ok_bool_true(DxgkResidencyCoreIsEvictable(&Refs), "fully released");
}

static VOID TestSubmissionPin(VOID)
{
    DXGK_RESIDENCY_REFS Refs;

    DxgkResidencyCoreRefsInitialize(&Refs, DEVICE_A, FALSE);
    ok_bool_true(DxgkResidencyCoreIsEvictable(&Refs), "evictable to start");

    /*
     * A running packet is reading this memory.  No residency bookkeeping may
     * override that: evicting underneath it corrupts the packet's results.
     */
    DxgkResidencyCorePinSubmission(&Refs);
    ok_bool_false(DxgkResidencyCoreIsEvictable(&Refs), "a submission pins it");
    DxgkResidencyCorePinSubmission(&Refs);
    DxgkResidencyCoreUnpinSubmission(&Refs);
    ok_bool_false(DxgkResidencyCoreIsEvictable(&Refs), "pins nest");
    DxgkResidencyCoreUnpinSubmission(&Refs);
    ok_bool_true(DxgkResidencyCoreIsEvictable(&Refs), "released when the last pin goes");

    /* Unpinning below zero would wrap and pin it forever. */
    DxgkResidencyCoreUnpinSubmission(&Refs);
    ok_eq_ulong(Refs.SubmissionPinCount, 0UL);
    ok_bool_true(DxgkResidencyCoreIsEvictable(&Refs), "still evictable");
}

START_TEST(DxgkResidencyReference)
{
    TestExplicitReferences();
    TestImplicitReference();
    TestDeviceTeardown();
    TestSubmissionPin();
}

/* EOF */
