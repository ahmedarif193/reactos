/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Render allocation lists
 *
 * The allocation list is the set of memory a submission may touch.  A
 * duplicate entry lets one allocation be patched under two different
 * residency decisions in the same submission.
 */

#include <kmt_test.h>
#include "dma_core.h"

static VOID TestAddAndFind(VOID)
{
    DXGK_DMA_ALLOCATION_LIST List;
    ULONG Index = 0xFFFFFFFF;

    DxgkDmaCoreAllocationListInitialize(&List);
    ok_eq_ulong(List.Count, 0UL);

    { NTSTATUS Observed = DxgkDmaCoreAllocationListAdd(&List, 0x100, 0x1000, 1, FALSE, &Index); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(Index, 0UL);
    { NTSTATUS Observed = DxgkDmaCoreAllocationListAdd(&List, 0x200, 0x2000, 1, TRUE, &Index); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(Index, 1UL);
    ok_eq_ulong(List.Count, 2UL);

    ok_bool_true(DxgkDmaCoreAllocationListFind(&List, 0x100, &Index), "find first");
    ok_eq_ulong(Index, 0UL);
    ok_bool_true(DxgkDmaCoreAllocationListFind(&List, 0x200, &Index), "find second");
    ok_eq_ulong(Index, 1UL);
    ok_bool_false(DxgkDmaCoreAllocationListFind(&List, 0x999, &Index), "absent");
    ok_bool_false(DxgkDmaCoreAllocationListFind(&List, 0, &Index), "zero cookie is never present");

    /* One entry per allocation: two would let it be patched under two
     * different residency decisions in one submission. */
    { NTSTATUS Observed = DxgkDmaCoreAllocationListAdd(&List, 0x100, 0x1000, 1, FALSE, &Index); ok_eq_hex(Observed, STATUS_OBJECT_NAME_COLLISION); }
    ok_eq_ulong(List.Count, 2UL);

    { NTSTATUS Observed = DxgkDmaCoreAllocationListAdd(&List, 0, 0x1000, 1, FALSE, &Index); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkDmaCoreAllocationListAdd(&List, 0x300, 0, 1, FALSE, &Index); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulong(List.Count, 2UL);
}

static VOID TestCapacity(VOID)
{
    DXGK_DMA_ALLOCATION_LIST List;
    ULONG Index;
    ULONG Added;

    DxgkDmaCoreAllocationListInitialize(&List);
    for (Added = 0; Added < DXGK_DMA_CORE_MAX_ALLOCATIONS; ++Added)
    {
        NTSTATUS Status = DxgkDmaCoreAllocationListAdd(&List, 0x1000 + Added, 0x1000, 1, FALSE, &Index);

        if (!NT_SUCCESS(Status))
            break;
    }
    ok_eq_ulong(Added, (ULONG)DXGK_DMA_CORE_MAX_ALLOCATIONS);
    ok_eq_ulong(List.Count, (ULONG)DXGK_DMA_CORE_MAX_ALLOCATIONS);

    /* Overflowing the list must be refused, not written past the array. */
    { NTSTATUS Observed = DxgkDmaCoreAllocationListAdd(&List, 0xABCDE, 0x1000, 1, FALSE, &Index); ok_eq_hex(Observed, STATUS_INSUFFICIENT_RESOURCES); }
    ok_eq_ulong(List.Count, (ULONG)DXGK_DMA_CORE_MAX_ALLOCATIONS);
}

static VOID TestResidencyRecorded(VOID)
{
    DXGK_DMA_ALLOCATION_LIST List;
    ULONG Index = 0;

    DxgkDmaCoreAllocationListInitialize(&List);
    /* Segment 0 means not resident; the entry is still recorded so the
     * patch stage can refuse it with a specific error. */
    { NTSTATUS Observed = DxgkDmaCoreAllocationListAdd(&List, 0x400, 0x1000, 0, FALSE, &Index); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(List.Entries[Index].SegmentId, 0UL);
    ok_bool_false(List.Entries[Index].WriteOperation, "read reference");

    { NTSTATUS Observed = DxgkDmaCoreAllocationListAdd(&List, 0x500, 0x8000, 2, TRUE, &Index); ok_eq_hex(Observed, STATUS_SUCCESS); }
    ok_eq_ulong(List.Entries[Index].SegmentId, 2UL);
    ok_bool_true(List.Entries[Index].WriteOperation, "write reference");
    ok_eq_ulonglong(List.Entries[Index].SizeInBytes, 0x8000ULL);
}

START_TEST(DxgkAllocationList)
{
    TestAddAndFind();
    TestCapacity();
    TestResidencyRecorded();
}

/* EOF */
