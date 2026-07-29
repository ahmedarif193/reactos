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

#define TRANSACTION_THREAD_COUNT 4
#define TRANSACTION_ITERATIONS   2000

typedef struct _RESIDENCY_TRANSACTION_STRESS
{
    PVOID volatile Owner;
    volatile LONG Inside;
    volatile LONG Violations;
    volatile LONG Acquisitions;
} RESIDENCY_TRANSACTION_STRESS, *PRESIDENCY_TRANSACTION_STRESS;

typedef struct _RESIDENCY_TRANSACTION_THREAD
{
    PRESIDENCY_TRANSACTION_STRESS Stress;
    KEVENT DoneEvent;
} RESIDENCY_TRANSACTION_THREAD, *PRESIDENCY_TRANSACTION_THREAD;

static VOID
NTAPI
ResidencyTransactionThread(
    _In_ PVOID Parameter)
{
    PRESIDENCY_TRANSACTION_THREAD Thread =
        (PRESIDENCY_TRANSACTION_THREAD)Parameter;
    ULONG Iteration;

    for (Iteration = 0; Iteration < TRANSACTION_ITERATIONS; ++Iteration)
    {
        if (!DxgkResidencyCoreTransactionTryAcquire(
                 &Thread->Stress->Owner,
                 Thread))
        {
            YieldProcessor();
            continue;
        }
        if (InterlockedIncrement(&Thread->Stress->Inside) != 1)
            InterlockedIncrement(&Thread->Stress->Violations);
        InterlockedIncrement(&Thread->Stress->Acquisitions);
        KeStallExecutionProcessor(1);
        InterlockedDecrement(&Thread->Stress->Inside);
        if (!DxgkResidencyCoreTransactionRelease(
                 &Thread->Stress->Owner,
                 Thread))
        {
            InterlockedIncrement(&Thread->Stress->Violations);
        }
    }

    KeSetEvent(&Thread->DoneEvent, IO_NO_INCREMENT, FALSE);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

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

static VOID TestTransactionOwnerRace(VOID)
{
    RESIDENCY_TRANSACTION_STRESS Stress;
    RESIDENCY_TRANSACTION_THREAD Threads[TRANSACTION_THREAD_COUNT];
    HANDLE Handles[TRANSACTION_THREAD_COUNT] = { 0 };
    ULONG Index;

    RtlZeroMemory(&Stress, sizeof(Stress));
    RtlZeroMemory(Threads, sizeof(Threads));
    for (Index = 0; Index < TRANSACTION_THREAD_COUNT; ++Index)
    {
        NTSTATUS Status;

        Threads[Index].Stress = &Stress;
        KeInitializeEvent(&Threads[Index].DoneEvent,
                          NotificationEvent,
                          FALSE);
        Status = PsCreateSystemThread(&Handles[Index],
                                      THREAD_ALL_ACCESS,
                                      NULL,
                                      NULL,
                                      NULL,
                                      ResidencyTransactionThread,
                                      &Threads[Index]);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (!NT_SUCCESS(Status))
            break;
    }

    while (Index != 0)
    {
        --Index;
        KeWaitForSingleObject(&Threads[Index].DoneEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        ZwClose(Handles[Index]);
    }
    ok_eq_long(Stress.Violations, 0L);
    ok_eq_long(Stress.Inside, 0L);
    ok(Stress.Acquisitions > 0,
       "expected at least one transaction acquisition\n");
    ok(Stress.Owner == NULL, "owner leaked at terminal edge: %p\n", Stress.Owner);
}

static VOID TestRollbackAndEvictOwnership(VOID)
{
    BOOLEAN Evict;
    BOOLEAN Trim;
    NTSTATUS Status;

    /*
     * Two duplicate handles in this request resolve to one physical backing.
     * A third reference from another request must prevent physical eviction.
     */
    Status = DxgkResidencyCorePlanEvict(2, 3, 2, TRUE, FALSE,
                                        &Evict, &Trim);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_false(Evict, "another request still owns the placement");
    ok_bool_false(Trim, "not a zero-reference trim candidate");
    ok_bool_false(DxgkResidencyCoreShouldRollbackPlacement(TRUE, FALSE),
                  "rollback must not evict across another reference");

    Status = DxgkResidencyCorePlanEvict(2, 2, 2, TRUE, FALSE,
                                        &Evict, &Trim);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(Evict, "this request owns the zero-reference transition");
    ok_bool_false(Trim, "physical eviction, not trim-only");
    ok_bool_true(DxgkResidencyCoreShouldRollbackPlacement(TRUE, TRUE),
                 "owned placement may roll back at zero references");
    ok_bool_false(DxgkResidencyCoreShouldRollbackPlacement(FALSE, TRUE),
                  "pre-existing placement is never rollback-owned");

    Status = DxgkResidencyCorePlanEvict(2, 2, 2, TRUE, TRUE,
                                        &Evict, &Trim);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_false(Evict, "EvictOnlyIfNecessary preserves placement");
    ok_bool_true(Trim, "zero-reference resident allocation is trimmable");

    Status = DxgkResidencyCorePlanEvict(1, 2, 2, TRUE, FALSE,
                                        &Evict, &Trim);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_bool_false(Evict, "under-owned duplicate request cannot evict");
}

START_TEST(DxgkResidencyReference)
{
    TestExplicitReferences();
    TestImplicitReference();
    TestDeviceTeardown();
    TestSubmissionPin();
    TestTransactionOwnerRace();
    TestRollbackAndEvictOwnership();
}

/* EOF */
