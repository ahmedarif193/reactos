/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1+ (https://spdx.org/licenses/LGPL-2.1+)
 * PURPOSE:     NUMA-aware contiguous memory allocation
 *
 * Win7 added two NUMA-aware contiguous allocators:
 *   PVOID MmAllocateContiguousMemorySpecifyCacheNode(
 *           SIZE_T NumberOfBytes,
 *           PHYSICAL_ADDRESS LowestAcceptableAddress,
 *           PHYSICAL_ADDRESS HighestAcceptableAddress,
 *           PHYSICAL_ADDRESS BoundaryAddressMultiple,
 *           MEMORY_CACHING_TYPE CacheType,
 *           NODE_REQUIREMENT PreferredNode);
 *
 *   PVOID MmAllocateContiguousNodeMemory(
 *           SIZE_T NumberOfBytes,
 *           PHYSICAL_ADDRESS LowestAcceptableAddress,
 *           PHYSICAL_ADDRESS HighestAcceptableAddress,
 *           PHYSICAL_ADDRESS BoundaryAddressMultiple,
 *           ULONG Protect,
 *           NODE_REQUIREMENT PreferredNode);
 *
 * Both are freed with MmFreeContiguousMemory().
 *
 * The test asks for one page on node 0, which is valid on every NUMA and
 * non-NUMA system, and verifies that the returned mapping is writable.
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

typedef PVOID (NTAPI *PMM_ALLOC_CONTIG_SPECIFY_CACHE_NODE)(
    SIZE_T NumberOfBytes,
    PHYSICAL_ADDRESS LowestAcceptableAddress,
    PHYSICAL_ADDRESS HighestAcceptableAddress,
    PHYSICAL_ADDRESS BoundaryAddressMultiple,
    MEMORY_CACHING_TYPE CacheType,
    ULONG PreferredNode);

typedef PVOID (NTAPI *PMM_ALLOC_CONTIG_NODE_MEMORY)(
    SIZE_T NumberOfBytes,
    PHYSICAL_ADDRESS LowestAcceptableAddress,
    PHYSICAL_ADDRESS HighestAcceptableAddress,
    PHYSICAL_ADDRESS BoundaryAddressMultiple,
    ULONG Protect,
    ULONG PreferredNode);

START_TEST(MmAllocateContiguousNode)
{
    PMM_ALLOC_CONTIG_SPECIFY_CACHE_NODE pMmAllocateContiguousMemorySpecifyCacheNode;
    PMM_ALLOC_CONTIG_NODE_MEMORY pMmAllocateContiguousNodeMemory;
    PHYSICAL_ADDRESS LowAddress, HighAddress, ZeroBoundary;
    PVOID Buffer;
    BOOLEAN AnyTested = FALSE;

    pMmAllocateContiguousMemorySpecifyCacheNode = (PMM_ALLOC_CONTIG_SPECIFY_CACHE_NODE)
        KmtGetSystemRoutineAddress(L"MmAllocateContiguousMemorySpecifyCacheNode");
    pMmAllocateContiguousNodeMemory = (PMM_ALLOC_CONTIG_NODE_MEMORY)
        KmtGetSystemRoutineAddress(L"MmAllocateContiguousNodeMemory");

    /* Skip cleanly if neither routine is exported (pre-Win7). */
    if (skip(pMmAllocateContiguousMemorySpecifyCacheNode != NULL ||
             pMmAllocateContiguousNodeMemory != NULL,
             "Neither MmAllocateContiguous*Node* routine is exported "
             "(pre-Win7 kernel)\n"))
    {
        return;
    }

    trace("SpecifyCacheNode = %p, NodeMemory = %p\n",
          pMmAllocateContiguousMemorySpecifyCacheNode,
          pMmAllocateContiguousNodeMemory);

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = (LONGLONG)-1;
    ZeroBoundary.QuadPart = 0;

    /* Test 1: MmAllocateContiguousMemorySpecifyCacheNode on node 0. */
    if (pMmAllocateContiguousMemorySpecifyCacheNode != NULL)
    {
        AnyTested = TRUE;
        Buffer = pMmAllocateContiguousMemorySpecifyCacheNode(
                     PAGE_SIZE,
                     LowAddress,
                     HighAddress,
                     ZeroBoundary,
                     MmCached,
                     0u);
        ok(Buffer != NULL,
           "MmAllocateContiguousMemorySpecifyCacheNode(node=0) returned NULL\n");
        if (Buffer != NULL)
        {
            /* Memory must be writable through to readback. */
            RtlFillMemory(Buffer, PAGE_SIZE, 0xA5);
            ok_eq_uint(((PUCHAR)Buffer)[0], 0xA5);
            ok_eq_uint(((PUCHAR)Buffer)[PAGE_SIZE - 1], 0xA5);
            MmFreeContiguousMemory(Buffer);
        }
    }

    /* Test 2: MmAllocateContiguousNodeMemory on node 0 with PAGE_READWRITE. */
    if (pMmAllocateContiguousNodeMemory != NULL)
    {
        AnyTested = TRUE;
        Buffer = pMmAllocateContiguousNodeMemory(
                     PAGE_SIZE,
                     LowAddress,
                     HighAddress,
                     ZeroBoundary,
                     PAGE_READWRITE,
                     0u);
        ok(Buffer != NULL,
           "MmAllocateContiguousNodeMemory(node=0) returned NULL\n");
        if (Buffer != NULL)
        {
            RtlFillMemory(Buffer, PAGE_SIZE, 0x5A);
            ok_eq_uint(((PUCHAR)Buffer)[0], 0x5A);
            ok_eq_uint(((PUCHAR)Buffer)[PAGE_SIZE - 1], 0x5A);
            MmFreeContiguousMemory(Buffer);
        }
    }

    ok(AnyTested == TRUE, "Neither contiguous-node allocator was tested\n");
}
