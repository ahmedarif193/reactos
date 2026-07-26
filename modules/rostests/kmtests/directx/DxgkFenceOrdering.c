/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Submission fence identity and wraparound-safe ordering
 *
 * Fence comparison decides what has executed.  A comparison that gets 32-bit
 * wraparound wrong retires work the GPU never ran, and the client is handed a
 * buffer that is still being written.
 */

#include <kmt_test.h>
#include "fence_core.h"

static VOID TestReachedAndDistance(VOID)
{
    ok_bool_true(DxgkFenceCoreReached(5, 5), "reached itself");
    ok_bool_true(DxgkFenceCoreReached(6, 5), "past");
    ok_bool_false(DxgkFenceCoreReached(4, 5), "not yet");
    ok_bool_true(DxgkFenceCoreReached(0, 0), "zero");

    /*
     * Across the 32-bit wrap the numeric order inverts but the real order does
     * not: 1 comes after 0xFFFFFFFF.  A plain '>=' would say the opposite and
     * retire everything outstanding.
     */
    ok_bool_true(DxgkFenceCoreReached(1, 0xFFFFFFFFUL), "wrapped completion is ahead");
    ok_bool_false(DxgkFenceCoreReached(0xFFFFFFFFUL, 1), "pre-wrap is behind");
    ok_bool_true(DxgkFenceCoreReached(0, 0xFFFFFFFFUL), "wrap to zero is ahead");

    { LONG Observed = DxgkFenceCoreDistance(5, 7); ok_eq_long(Observed, 2L); }
    { LONG Observed = DxgkFenceCoreDistance(7, 5); ok_eq_long(Observed, -2L); }
    { LONG Observed = DxgkFenceCoreDistance(0xFFFFFFFFUL, 1); ok_eq_long(Observed, 2L); }
    { LONG Observed = DxgkFenceCoreDistance(1, 0xFFFFFFFFUL); ok_eq_long(Observed, -2L); }
}

static VOID TestMonotonicAdvance(VOID)
{
    volatile LONG Watermark = 0;

    ok_bool_true(DxgkFenceCoreAdvance(&Watermark, 5), "first advance");
    ok_eq_long(Watermark, 5L);
    ok_bool_true(DxgkFenceCoreAdvance(&Watermark, 9), "forward");
    ok_eq_long(Watermark, 9L);

    /* A late or duplicated interrupt must not move the watermark backwards;
     * doing so would let already-retired work look outstanding again. */
    ok_bool_false(DxgkFenceCoreAdvance(&Watermark, 7), "backwards refused");
    ok_eq_long(Watermark, 9L);
    ok_bool_false(DxgkFenceCoreAdvance(&Watermark, 9), "repeat is not an advance");
    ok_eq_long(Watermark, 9L);

    Watermark = (LONG)0xFFFFFFF0UL;
    ok_bool_true(DxgkFenceCoreAdvance(&Watermark, 5), "advance across the wrap");
    ok_eq_ulong((ULONG)Watermark, 5UL);
    ok_bool_false(DxgkFenceCoreAdvance(&Watermark, 0xFFFFFFF8UL), "pre-wrap value refused after wrap");
}

static VOID TestAllocation(VOID)
{
    volatile LONG Next = 0;
    ULONG First;
    ULONG Second;

    First = DxgkFenceCoreAllocate(&Next);
    Second = DxgkFenceCoreAllocate(&Next);
    ok(First != 0, "fence identity is never zero\n");
    ok(Second != 0, "fence identity is never zero\n");
    ok_eq_ulong(Second, First + 1);

    /* Zero means "no fence", so the sequence must step over it on wrap
     * instead of handing out an identity that reads as absent. */
    Next = (LONG)0xFFFFFFFFUL;
    { ULONG Observed = DxgkFenceCoreAllocate(&Next); ok_eq_ulong(Observed, 1UL); }
}

START_TEST(DxgkFenceOrdering)
{
    TestReachedAndDistance();
    TestMonotonicAdvance();
    TestAllocation();
}

/* EOF */
