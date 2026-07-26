/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     The per-context DMA command ring
 *
 * The submission window tells the miniport which bytes of a user-mapped ring
 * to execute.  An inverted window becomes a huge length by subtraction.
 */

#include <kmt_test.h>
#include "dma_core.h"

static VOID TestInitialization(VOID)
{
    DXGK_DMA_RING Ring;

    { NTSTATUS Observed = DxgkDmaCoreRingInitialize(&Ring, 0x10000, 4096); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulonglong(Ring.CapacityInBytes, 0x10000ULL);
    ok_bool_true(Ring.Initialized, "initialized");

    { NTSTATUS Observed = DxgkDmaCoreRingInitialize(&Ring, 0, 4096); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_bool_false(Ring.Initialized, "a rejected ring is not usable");
}

static VOID TestSubmissionWindow(VOID)
{
    DXGK_DMA_RING Ring;
    ULONGLONG Length = 0;

    { NTSTATUS Observed = DxgkDmaCoreRingInitialize(&Ring, 0x10000, 4096); ok_eq_hex(Observed, STATUS_SUCCESS); }

    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0, 0x1000, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkDmaCoreRingSubmissionLength(&Ring, &Length), "length");
    ok_eq_ulonglong(Length, 0x1000ULL);

    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0x800, 0x1000, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkDmaCoreRingSubmissionLength(&Ring, &Length), "offset length");
    ok_eq_ulonglong(Length, 0x800ULL);

    /* An empty window is legal: a submission may carry no commands. */
    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0x1000, 0x1000, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_bool_true(DxgkDmaCoreRingSubmissionLength(&Ring, &Length), "empty window");
    ok_eq_ulonglong(Length, 0ULL);

    /* An inverted window becomes an enormous length by subtraction, and the
     * miniport reads far past the mapping. */
    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0x1000, 0x800, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* Past the end of the ring is past the end of the user mapping. */
    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0, 0x10001, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0, 0x10000, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
}

static VOID TestPrivateData(VOID)
{
    DXGK_DMA_RING Ring;

    { NTSTATUS Observed = DxgkDmaCoreRingInitialize(&Ring, 0x10000, 4096); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0, 0x1000, 4096); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(Ring.PrivateDataSize, 4096UL);

    /* Private data past what the miniport asked for would overrun the buffer
     * it sized for its own per-submission state. */
    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0, 0x1000, 4097); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    { NTSTATUS Observed = DxgkDmaCoreRingInitialize(&Ring, 0x1000, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0, 0x100, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0, 0x100, 1); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestUninitialized(VOID)
{
    DXGK_DMA_RING Ring;
    ULONGLONG Length = 1;

    RtlZeroMemory(&Ring, sizeof(Ring));
    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(&Ring, 0, 0, 0); ok_eq_hex(Observed, STATUS_INVALID_DEVICE_STATE); }
    ok_bool_false(DxgkDmaCoreRingSubmissionLength(&Ring, &Length), "no length without a ring");
    ok_eq_ulonglong(Length, 0ULL);
}

START_TEST(DxgkDmaRing)
{
    TestInitialization();
    TestSubmissionWindow();
    TestPrivateData();
    TestUninitialized();
}

/* EOF */
