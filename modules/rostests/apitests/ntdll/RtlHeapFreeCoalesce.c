/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test reuse of adjacent freed blocks in a fixed-size heap
 */

#include "precomp.h"

static VOID
TestFreeOrder(BOOLEAN Reverse)
{
    const SIZE_T ReserveSize = 16 * 1024 * 1024;
    const SIZE_T BlockSize = 256 * 1024;
    const SIZE_T LargeSize = 384 * 1024;
    PVOID Blocks[128];
    PVOID Heap;
    ULONG Count, Index, LargeCount;

    /* Do not allow another segment or a virtual allocation to hide fragmentation. */
    Heap = RtlCreateHeap(0, NULL, ReserveSize, 0, NULL, NULL);
    ok(Heap != NULL, "Could not create fixed-size heap\n");
    if (!Heap)
        return;

    for (Count = 0; Count < RTL_NUMBER_OF(Blocks); ++Count)
    {
        Blocks[Count] = RtlAllocateHeap(Heap, 0, BlockSize);
        if (!Blocks[Count])
            break;
    }
    trace("%s free order: allocated %lu blocks of %lu bytes\n",
          Reverse ? "Reverse" : "Forward", Count, (ULONG)BlockSize);
    ok(Count > 3 && Count < RTL_NUMBER_OF(Blocks), "Unexpected block count %lu\n", Count);

    for (Index = 0; Index < Count; ++Index)
    {
        ULONG BlockIndex = Reverse ? Count - Index - 1 : Index;
        ok(RtlFreeHeap(Heap, 0, Blocks[BlockIndex]), "Failed to free block %lu\n", BlockIndex);
    }

    /* Reuse less than the original capacity. One larger allocation alone might
     * fit in the unused tail, without joining any of the freed blocks. */
    for (LargeCount = 0; LargeCount < Count / 2; ++LargeCount)
    {
        Blocks[LargeCount] = RtlAllocateHeap(Heap, 0, LargeSize);
        if (!Blocks[LargeCount])
            break;
        RtlFillMemory(Blocks[LargeCount], LargeSize, 0xa5);
        ok(RtlSizeHeap(Heap, 0, Blocks[LargeCount]) == LargeSize, "Unexpected size of reused allocation\n");
    }
    ok(LargeCount == Count / 2, "%s free order: reused %lu of %lu blocks of %lu bytes\n",
       Reverse ? "Reverse" : "Forward", LargeCount, Count / 2, (ULONG)LargeSize);
    for (Index = 0; Index < LargeCount; ++Index)
        ok(RtlFreeHeap(Heap, 0, Blocks[Index]), "Failed to free reused allocation %lu\n", Index);

    ok(RtlValidateHeap(Heap, 0, NULL), "Heap validation failed\n");
    ok(RtlDestroyHeap(Heap) == NULL, "Failed to destroy heap\n");
}

START_TEST(RtlHeapFreeCoalesce)
{
    TestFreeOrder(FALSE);
    TestFreeOrder(TRUE);
}
