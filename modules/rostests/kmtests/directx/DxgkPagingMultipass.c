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

START_TEST(DxgkPagingMultipass)
{
    TestSinglePass();
    TestMultiplePasses();
    TestProtocolViolations();
    TestPassCeiling();
}

/* EOF */
