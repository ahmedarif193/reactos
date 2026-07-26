/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     GPU virtual address algebra
 *
 * Every one of these operations sits under a real GPU mapping.  A range that
 * wraps instead of being refused becomes a small address that looks valid, and
 * the GPU writes into whatever lives there.
 */

#include <kmt_test.h>
#include "gpuva_core.h"

#define GB (1024ULL * 1024ULL * 1024ULL)

static VOID TestAlignUp(VOID)
{
    ULONGLONG Result = 0;

    ok_bool_true(DxgkGpuVaCoreAlignUp(0, 0x1000, &Result), "align zero");
    ok_eq_ulonglong(Result, 0ULL);
    ok_bool_true(DxgkGpuVaCoreAlignUp(1, 0x1000, &Result), "align one");
    ok_eq_ulonglong(Result, 0x1000ULL);
    ok_bool_true(DxgkGpuVaCoreAlignUp(0x1000, 0x1000, &Result), "already aligned");
    ok_eq_ulonglong(Result, 0x1000ULL);
    ok_bool_true(DxgkGpuVaCoreAlignUp(0x1001, 0x1000, &Result), "one past");
    ok_eq_ulonglong(Result, 0x2000ULL);
    ok_bool_true(DxgkGpuVaCoreAlignUp(0x1234, 1, &Result), "alignment of one");
    ok_eq_ulonglong(Result, 0x1234ULL);

    /* Zero and non-power-of-two alignments have no meaning for a page table. */
    ok_bool_false(DxgkGpuVaCoreAlignUp(0x1000, 0, &Result), "zero alignment");
    ok_bool_false(DxgkGpuVaCoreAlignUp(0x1000, 3, &Result), "non-power-of-two");
    ok_bool_false(DxgkGpuVaCoreAlignUp(0x1000, 0x1800, &Result), "non-power-of-two large");

    /* Rounding past the top must be refused, never wrapped to a low address. */
    ok_bool_false(DxgkGpuVaCoreAlignUp(MAXULONGLONG, 0x1000, &Result), "wrap refused");
    ok_eq_ulonglong(Result, 0ULL);
    ok_bool_false(DxgkGpuVaCoreAlignUp(MAXULONGLONG - 0x100, 0x1000, &Result), "near-wrap refused");
    ok_bool_true(DxgkGpuVaCoreAlignUp(MAXULONGLONG - 0xFFF, 0x1000, &Result), "exact top aligns");
    ok_eq_ulonglong(Result, MAXULONGLONG - 0xFFF);
}

static VOID TestRangeEnd(VOID)
{
    ULONGLONG End = 0;

    ok_bool_true(DxgkGpuVaCoreRangeEnd(0x1000, 0x1000, &End), "simple range");
    ok_eq_ulonglong(End, 0x2000ULL);
    ok_bool_true(DxgkGpuVaCoreRangeEnd(0, 1, &End), "one byte at zero");
    ok_eq_ulonglong(End, 1ULL);

    /* A zero-length range has no end; treating it as valid makes every
     * containment test trivially true. */
    ok_bool_false(DxgkGpuVaCoreRangeEnd(0x1000, 0, &End), "zero size");
    ok_eq_ulonglong(End, 0ULL);

    ok_bool_false(DxgkGpuVaCoreRangeEnd(MAXULONGLONG, 1, &End), "wrap by one");
    ok_bool_false(DxgkGpuVaCoreRangeEnd(MAXULONGLONG - 0xFF, 0x1000, &End), "wrap by span");
    ok_bool_false(DxgkGpuVaCoreRangeEnd(MAXULONGLONG - 0xFFF, 0x1000, &End), "end at 2^64 is unrepresentable");
    ok_bool_true(DxgkGpuVaCoreRangeEnd(MAXULONGLONG - 0x1000, 0x1000, &End), "exact top");
    ok_eq_ulonglong(End, MAXULONGLONG);
}

static VOID TestOverlapAndContainment(VOID)
{
    /* Adjacent ranges do not overlap: [0,0x1000) and [0x1000,0x2000) share
     * no byte, and treating them as overlapping would reject valid layouts. */
    ok_bool_false(DxgkGpuVaCoreRangesOverlap(0, 0x1000, 0x1000, 0x1000), "adjacent");
    ok_bool_true(DxgkGpuVaCoreRangesOverlap(0, 0x1001, 0x1000, 0x1000), "one byte overlap");
    ok_bool_true(DxgkGpuVaCoreRangesOverlap(0x1000, 0x1000, 0, 0x2000), "fully contained");
    ok_bool_true(DxgkGpuVaCoreRangesOverlap(0, 0x2000, 0x1000, 0x1000), "contains");
    ok_bool_true(DxgkGpuVaCoreRangesOverlap(0x1000, 0x1000, 0x1000, 0x1000), "identical");
    ok_bool_false(DxgkGpuVaCoreRangesOverlap(0, 0x1000, 0x8000, 0x1000), "disjoint");

    /* An unrepresentable range must not be reported as overlapping something
     * it only touches by wrapping. */
    ok_bool_false(DxgkGpuVaCoreRangesOverlap(MAXULONGLONG - 0xFF, 0x1000, 0, 0x1000), "wrapped A");
    ok_bool_false(DxgkGpuVaCoreRangesOverlap(0, 0x1000, MAXULONGLONG - 0xFF, 0x1000), "wrapped B");
    ok_bool_false(DxgkGpuVaCoreRangesOverlap(0, 0, 0, 0x1000), "zero-size A");

    ok_bool_true(DxgkGpuVaCoreRangeContains(0, 0x2000, 0x1000, 0x1000), "contains tail");
    ok_bool_true(DxgkGpuVaCoreRangeContains(0, 0x2000, 0, 0x2000), "contains self");
    ok_bool_false(DxgkGpuVaCoreRangeContains(0, 0x2000, 0x1000, 0x2000), "runs past end");
    ok_bool_false(DxgkGpuVaCoreRangeContains(0x1000, 0x1000, 0, 0x1000), "before start");
    ok_bool_false(DxgkGpuVaCoreRangeContains(0, 0x1000, 0x1000, 0), "zero inner");
}

static VOID TestPageMath(VOID)
{
    ULONGLONG Pages = 0;

    ok_bool_true(DxgkGpuVaCoreIsPageAligned(0), "zero is aligned");
    ok_bool_true(DxgkGpuVaCoreIsPageAligned(0x1000), "page aligned");
    ok_bool_false(DxgkGpuVaCoreIsPageAligned(0x1), "byte one");
    ok_bool_false(DxgkGpuVaCoreIsPageAligned(0xFFF), "last byte of page");

    ok_bool_true(DxgkGpuVaCorePageCount(0, 0x1000, &Pages), "exactly one page");
    ok_eq_ulonglong(Pages, 1ULL);
    ok_bool_true(DxgkGpuVaCorePageCount(0, 0x1001, &Pages), "one byte into the second");
    ok_eq_ulonglong(Pages, 2ULL);

    /*
     * The count is of pages touched, not of pages the length spans: a range
     * of one page starting mid-page straddles two, and a mapping loop that
     * used size alone would leave the tail unmapped.
     */
    ok_bool_true(DxgkGpuVaCorePageCount(0x800, 0x1000, &Pages), "unaligned straddle");
    ok_eq_ulonglong(Pages, 2ULL);
    ok_bool_true(DxgkGpuVaCorePageCount(0xFFF, 2, &Pages), "two bytes across a boundary");
    ok_eq_ulonglong(Pages, 2ULL);
    ok_bool_true(DxgkGpuVaCorePageCount(0x1000, 0x4000, &Pages), "four pages");
    ok_eq_ulonglong(Pages, 4ULL);
    ok_bool_false(DxgkGpuVaCorePageCount(0x1000, 0, &Pages), "zero size");
}

START_TEST(DxgkGpuVaAddress)
{
    TestAlignUp();
    TestRangeEnd();
    TestOverlapAndContainment();
    TestPageMath();
}

/* EOF */
