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

typedef struct _TEST_SPAN
{
    DXGK_GPUVA_CORE_SPAN Core;
    ULONGLONG Payload;
} TEST_SPAN;

static VOID TestMapPieces(VOID)
{
    TEST_SPAN Spans[2] = { { { 0x20000, 0x40000 }, 1 },
                           { { 0x60000, 0x70000 }, 2 } };
    ULONG Index = 0;
    ULONGLONG End = 0;
    ULONG Cover = 0;

    ok_bool_true(DxgkGpuVaCoreNextMapPiece(Spans, 2, sizeof(TEST_SPAN), 0x10000, 0x80000, &Index, &End, &Cover), "leading gap");
    ok_eq_ulonglong(End, 0x20000ULL);
    ok_eq_ulong(Cover, MAXULONG);
    ok_bool_true(DxgkGpuVaCoreNextMapPiece(Spans, 2, sizeof(TEST_SPAN), End, 0x80000, &Index, &End, &Cover), "first range");
    ok_eq_ulonglong(End, 0x40000ULL);
    ok_eq_ulong(Cover, 0UL);
    ok_bool_true(DxgkGpuVaCoreNextMapPiece(Spans, 2, sizeof(TEST_SPAN), End, 0x80000, &Index, &End, &Cover), "middle gap");
    ok_eq_ulonglong(End, 0x60000ULL);
    ok_eq_ulong(Cover, MAXULONG);
    ok_bool_true(DxgkGpuVaCoreNextMapPiece(Spans, 2, sizeof(TEST_SPAN), End, 0x68000, &Index, &End, &Cover), "limit inside range");
    ok_eq_ulonglong(End, 0x68000ULL);
    ok_eq_ulong(Cover, 1UL);
    ok_bool_true(DxgkGpuVaCoreNextMapPiece(Spans, 2, sizeof(TEST_SPAN), 0x68000, 0x80000, &Index, &End, &Cover), "resume inside range");
    ok_eq_ulonglong(End, 0x70000ULL);
    ok_eq_ulong(Cover, 1UL);
    ok_bool_true(DxgkGpuVaCoreNextMapPiece(Spans, 2, sizeof(TEST_SPAN), End, 0x80000, &Index, &End, &Cover), "trailing gap");
    ok_eq_ulonglong(End, 0x80000ULL);
    ok_eq_ulong(Cover, MAXULONG);
    ok_bool_false(DxgkGpuVaCoreNextMapPiece(Spans, 2, sizeof(TEST_SPAN), 0x80000, 0x80000, &Index, &End, &Cover), "empty window");
    ok_bool_false(DxgkGpuVaCoreNextMapPiece(NULL, 2, sizeof(TEST_SPAN), 0, 0x1000, &Index, &End, &Cover), "null spans");
    ok_bool_false(DxgkGpuVaCoreNextMapPiece(Spans, 2, sizeof(ULONG), 0, 0x1000, &Index, &End, &Cover), "short stride");
    Index = 0;
    ok_bool_true(DxgkGpuVaCoreNextMapPiece(NULL, 0, sizeof(TEST_SPAN), 0x1000, 0x9000, &Index, &End, &Cover), "no spans");
    ok_eq_ulonglong(End, 0x9000ULL);
    ok_eq_ulong(Cover, MAXULONG);
}

static VOID TestAllocationRepeatMapping(VOID)
{
    static const struct
    {
        ULONGLONG AllocationSize, AllocationOffset, MappingSize, MappingOffset;
        BOOLEAN Valid;
        ULONGLONG SourceOffset, ChunkSize;
    } Cases[] = {
        /* Intel's internal 4 KiB allocation mapped across two virtual pages. */
        {0x1000, 0, 0x2000, 0, TRUE, 0, 0x1000},
        {0x1000, 0, 0x2000, 0x1000, TRUE, 0, 0x1000},
        /* Repeat pages 1,2 as 1,2,1,2,1. The final repeat is partial. */
        {0x3000, 0x1000, 0x5000, 0, TRUE, 0x1000, 0x2000},
        {0x3000, 0x1000, 0x5000, 0x1000, TRUE, 0x2000, 0x1000},
        {0x3000, 0x1000, 0x5000, 0x2000, TRUE, 0x1000, 0x2000},
        {0x3000, 0x1000, 0x5000, 0x4000, TRUE, 0x1000, 0x1000},
        {0x4000, 0x1000, 0x1000, 0, TRUE, 0x1000, 0x1000},
        {0x1000, 0x1000, 0x2000, 0, FALSE, 0, 0},
        {0x1000, 0x2000, 0x2000, 0, FALSE, 0, 0},
        {0, 0, 0x2000, 0, FALSE, 0, 0},
        {0x1000, 0, 0, 0, FALSE, 0, 0},
        {0x1000, 0, 0x2000, 0x2000, FALSE, 0, 0},
        {0x1000, 1, 0x2000, 0, FALSE, 0, 0},
        {0x1001, 0, 0x2000, 0, FALSE, 0, 0},
        {0x1000, 0, 0x2001, 0, FALSE, 0, 0},
        {0x1000, 0, 0x2000, 1, FALSE, 0, 0},
        /* No addition of the large virtual offset to the physical offset. */
        {0x3000, 0x2000, MAXULONGLONG - 0xfff, MAXULONGLONG - 0x1fff,
         TRUE, 0x2000, 0x1000},
    };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(Cases); ++Index)
    {
        ULONGLONG SourceOffset = MAXULONGLONG, ChunkSize = MAXULONGLONG;
        BOOLEAN Valid = DxgkGpuVaCoreAllocationMapChunk(
            Cases[Index].AllocationSize, Cases[Index].AllocationOffset,
            Cases[Index].MappingSize, Cases[Index].MappingOffset,
            &SourceOffset, &ChunkSize);

        ok(Valid == Cases[Index].Valid, "case %lu: valid %u\n", Index, Valid);
        ok_eq_ulonglong(SourceOffset, Cases[Index].SourceOffset);
        ok_eq_ulonglong(ChunkSize, Cases[Index].ChunkSize);
    }
}

START_TEST(DxgkGpuVaAddress)
{
    TestAlignUp();
    TestRangeEnd();
    TestOverlapAndContainment();
    TestPageMath();
    TestMapPieces();
    TestAllocationRepeatMapping();
}

/* EOF */
