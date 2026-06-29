/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Video memory manager allocation algorithm tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the first-fit allocator with alignment, free-block coalescing,
 * segment initialization, and LRU eviction logic.
 */

#include "precomp.h"

/* ---- Segment flags ---- */
#define SEG_FLAG_APERTURE    0x01
#define SEG_FLAG_AGPTYPE     0x02
#define SEG_FLAG_CPUVISIBLE  0x04
#define SEG_FLAG_CACHECOHER  0x08

/* Replicate free block structure */
typedef struct _TEST_FREE_BLOCK {
    struct _TEST_FREE_BLOCK *pNext;
    ULONGLONG Offset;
    ULONGLONG Size;
} TEST_FREE_BLOCK;

/* Replicate allocation entry for LRU tracking */
typedef struct _TEST_ALLOC_ENTRY {
    struct _TEST_ALLOC_ENTRY *pNext;
    ULONGLONG  SegmentOffset;
    SIZE_T     AllocationSize;
    ULONG      Priority;
    ULONGLONG  LastUseTimestamp;
} TEST_ALLOC_ENTRY;

/* Simplified segment state */
typedef struct _TEST_SEGMENT {
    ULONGLONG       TotalSize;
    ULONGLONG       AllocatedSize;
    ULONG           Flags;
    TEST_FREE_BLOCK *FreeListHead;
    TEST_ALLOC_ENTRY *AllocationList;
} TEST_SEGMENT;

/* ---- Alignment helper ---- */
static ULONGLONG AlignUp(ULONGLONG val, ULONGLONG align)
{
    if (align == 0) return val;
    return (val + align - 1) & ~(align - 1);
}

/* ---- First-fit allocator ---- */
static BOOL SegmentAlloc(TEST_SEGMENT *seg, ULONGLONG size, ULONGLONG alignment,
                         ULONGLONG *outOffset)
{
    TEST_FREE_BLOCK *block, *prev = NULL;

    if (alignment == 0) alignment = 1;

    for (block = seg->FreeListHead; block; prev = block, block = block->pNext)
    {
        ULONGLONG alignedStart = AlignUp(block->Offset, alignment);
        ULONGLONG prefix = alignedStart - block->Offset;

        if (block->Size < prefix + size)
            continue; /* Too small */

        ULONGLONG suffix = block->Size - prefix - size;

        if (prefix == 0 && suffix == 0)
        {
            /* Exact fit — remove block entirely */
            if (prev) prev->pNext = block->pNext;
            else seg->FreeListHead = block->pNext;
            HeapFree(GetProcessHeap(), 0, block);
        }
        else if (prefix == 0)
        {
            /* Allocation at start of block, shrink block */
            block->Offset += size;
            block->Size = suffix;
        }
        else
        {
            /* Split: shrink block to prefix, optionally add suffix */
            block->Size = prefix;
            if (suffix > 0)
            {
                TEST_FREE_BLOCK *suffixBlock = HeapAlloc(GetProcessHeap(),
                    HEAP_ZERO_MEMORY, sizeof(TEST_FREE_BLOCK));
                suffixBlock->Offset = alignedStart + size;
                suffixBlock->Size = suffix;
                suffixBlock->pNext = block->pNext;
                block->pNext = suffixBlock;
            }
        }

        seg->AllocatedSize += size;
        *outOffset = alignedStart;
        return TRUE;
    }

    return FALSE; /* No suitable block found */
}

/* ---- Free with coalescing ---- */
static void SegmentFree(TEST_SEGMENT *seg, ULONGLONG offset, ULONGLONG size)
{
    TEST_FREE_BLOCK *newBlock, *cur, *prev = NULL;

    seg->AllocatedSize -= size;

    newBlock = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TEST_FREE_BLOCK));
    newBlock->Offset = offset;
    newBlock->Size = size;
    newBlock->pNext = NULL;

    /* Find insertion point (sorted by offset) */
    for (cur = seg->FreeListHead; cur; prev = cur, cur = cur->pNext)
    {
        if (cur->Offset > offset)
            break;
    }

    /* Insert */
    newBlock->pNext = cur;
    if (prev)
        prev->pNext = newBlock;
    else
        seg->FreeListHead = newBlock;

    /* Coalesce with successor */
    if (newBlock->pNext &&
        newBlock->Offset + newBlock->Size == newBlock->pNext->Offset)
    {
        TEST_FREE_BLOCK *succ = newBlock->pNext;
        newBlock->Size += succ->Size;
        newBlock->pNext = succ->pNext;
        HeapFree(GetProcessHeap(), 0, succ);
    }

    /* Coalesce with predecessor */
    if (prev && prev->Offset + prev->Size == newBlock->Offset)
    {
        prev->Size += newBlock->Size;
        prev->pNext = newBlock->pNext;
        HeapFree(GetProcessHeap(), 0, newBlock);
    }
}

static void SegmentInit(TEST_SEGMENT *seg, ULONGLONG totalSize, ULONG flags)
{
    TEST_FREE_BLOCK *initial;

    memset(seg, 0, sizeof(*seg));
    seg->TotalSize = totalSize;
    seg->Flags = flags;

    initial = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(TEST_FREE_BLOCK));
    initial->Offset = 0;
    initial->Size = totalSize;
    initial->pNext = NULL;
    seg->FreeListHead = initial;
}

static void SegmentCleanup(TEST_SEGMENT *seg)
{
    TEST_FREE_BLOCK *b = seg->FreeListHead;
    while (b)
    {
        TEST_FREE_BLOCK *next = b->pNext;
        HeapFree(GetProcessHeap(), 0, b);
        b = next;
    }
    seg->FreeListHead = NULL;
}

static ULONGLONG FreeSpaceTotal(TEST_SEGMENT *seg)
{
    ULONGLONG total = 0;
    TEST_FREE_BLOCK *b;
    for (b = seg->FreeListHead; b; b = b->pNext)
        total += b->Size;
    return total;
}

static ULONG FreeBlockCount(TEST_SEGMENT *seg)
{
    ULONG count = 0;
    TEST_FREE_BLOCK *b;
    for (b = seg->FreeListHead; b; b = b->pNext)
        ++count;
    return count;
}

/* ---- Tests ---- */

static void Test_SegmentFlags(void)
{
    ok_eq_uint(SEG_FLAG_APERTURE, 0x01);
    ok_eq_uint(SEG_FLAG_AGPTYPE, 0x02);
    ok_eq_uint(SEG_FLAG_CPUVISIBLE, 0x04);
    ok_eq_uint(SEG_FLAG_CACHECOHER, 0x08);

    /* All flags must be distinct bits */
    ok((SEG_FLAG_APERTURE & SEG_FLAG_AGPTYPE) == 0, "Aperture and AGP overlap\n");
    ok((SEG_FLAG_APERTURE & SEG_FLAG_CPUVISIBLE) == 0, "Aperture and CPU overlap\n");
    ok((SEG_FLAG_APERTURE & SEG_FLAG_CACHECOHER) == 0, "Aperture and cache overlap\n");
    ok((SEG_FLAG_AGPTYPE & SEG_FLAG_CPUVISIBLE) == 0, "AGP and CPU overlap\n");
    ok((SEG_FLAG_AGPTYPE & SEG_FLAG_CACHECOHER) == 0, "AGP and cache overlap\n");
    ok((SEG_FLAG_CPUVISIBLE & SEG_FLAG_CACHECOHER) == 0, "CPU and cache overlap\n");

    /* Combined flags */
    ULONG combined = SEG_FLAG_APERTURE | SEG_FLAG_CPUVISIBLE;
    ok((combined & SEG_FLAG_APERTURE) != 0, "Aperture bit set in combined\n");
    ok((combined & SEG_FLAG_CPUVISIBLE) != 0, "CPU bit set in combined\n");
    ok((combined & SEG_FLAG_AGPTYPE) == 0, "AGP bit should not be set\n");
}

static void Test_SegmentInit(void)
{
    TEST_SEGMENT seg;
    SegmentInit(&seg, 1024 * 1024, SEG_FLAG_CPUVISIBLE); /* 1MB */

    ok(seg.TotalSize == 1024 * 1024, "Total size should be 1MB\n");
    ok(seg.AllocatedSize == 0, "Allocated should be 0 initially\n");
    ok(seg.Flags == SEG_FLAG_CPUVISIBLE, "Flags mismatch\n");
    ok(seg.FreeListHead != NULL, "Free list should have initial block\n");
    ok(seg.FreeListHead->Offset == 0, "Initial block at offset 0\n");
    ok(seg.FreeListHead->Size == 1024 * 1024, "Initial block covers full segment\n");
    ok(seg.FreeListHead->pNext == NULL, "Only one initial free block\n");
    ok_eq_uint(FreeBlockCount(&seg), 1);

    SegmentCleanup(&seg);
}

static void Test_SimpleAllocation(void)
{
    TEST_SEGMENT seg;
    ULONGLONG offset;

    SegmentInit(&seg, 4096, 0);

    BOOL result = SegmentAlloc(&seg, 256, 1, &offset);
    ok(result, "Simple 256-byte allocation failed\n");
    ok(offset == 0, "First allocation should start at 0\n");
    ok(seg.AllocatedSize == 256, "AllocatedSize should be 256\n");
    ok(FreeSpaceTotal(&seg) == 4096 - 256, "Free space mismatch\n");

    SegmentCleanup(&seg);
}

static void Test_AlignedAllocation(void)
{
    TEST_SEGMENT seg;
    ULONGLONG off1, off2;

    SegmentInit(&seg, 4096, 0);

    /* Allocate 100 bytes unaligned, then 256 bytes at 256 alignment */
    SegmentAlloc(&seg, 100, 1, &off1);
    ok(off1 == 0, "First alloc at 0\n");

    BOOL result = SegmentAlloc(&seg, 256, 256, &off2);
    ok(result, "Aligned allocation failed\n");
    ok(off2 == 256, "Should be aligned to 256, got %llu\n", off2);
    ok((off2 % 256) == 0, "Offset should be 256-aligned\n");

    SegmentCleanup(&seg);
}

static void Test_MultipleAllocations(void)
{
    TEST_SEGMENT seg;
    ULONGLONG offsets[8];
    ULONG i;

    SegmentInit(&seg, 8192, 0);

    /* Allocate 8 x 512 bytes = 4096 bytes total */
    for (i = 0; i < 8; ++i)
    {
        BOOL result = SegmentAlloc(&seg, 512, 1, &offsets[i]);
        ok(result, "Allocation %lu failed\n", i);
        ok(offsets[i] == (ULONGLONG)i * 512,
           "Alloc %lu: expected offset %llu, got %llu\n",
           i, (ULONGLONG)i * 512, offsets[i]);
    }

    ok(seg.AllocatedSize == 4096, "Total allocated should be 4096\n");
    ok(FreeSpaceTotal(&seg) == 4096, "Free space should be 4096\n");

    SegmentCleanup(&seg);
}

static void Test_AllocationExhaustion(void)
{
    TEST_SEGMENT seg;
    ULONGLONG offset;

    SegmentInit(&seg, 256, 0);

    /* Fill completely */
    BOOL r = SegmentAlloc(&seg, 256, 1, &offset);
    ok(r, "Full allocation should succeed\n");

    /* Next allocation should fail */
    r = SegmentAlloc(&seg, 1, 1, &offset);
    ok(!r, "Allocation from full segment should fail\n");

    SegmentCleanup(&seg);
}

static void Test_FreeAndReuse(void)
{
    TEST_SEGMENT seg;
    ULONGLONG off1, off2;

    SegmentInit(&seg, 512, 0);

    SegmentAlloc(&seg, 256, 1, &off1);
    ok(seg.AllocatedSize == 256, "256 allocated\n");

    SegmentFree(&seg, off1, 256);
    ok(seg.AllocatedSize == 0, "0 after free\n");
    ok(FreeSpaceTotal(&seg) == 512, "Full segment free after free\n");

    /* Reallocate — should reuse the same space */
    SegmentAlloc(&seg, 256, 1, &off2);
    ok(off2 == 0, "Reallocation should start at 0\n");

    SegmentCleanup(&seg);
}

static void Test_Coalescing(void)
{
    TEST_SEGMENT seg;
    ULONGLONG off1, off2, off3;

    SegmentInit(&seg, 3072, 0);

    /* Allocate 3 x 1024 */
    SegmentAlloc(&seg, 1024, 1, &off1);
    SegmentAlloc(&seg, 1024, 1, &off2);
    SegmentAlloc(&seg, 1024, 1, &off3);
    ok(FreeSpaceTotal(&seg) == 0, "Fully allocated\n");

    /* Free middle block */
    SegmentFree(&seg, off2, 1024);
    ok_eq_uint(FreeBlockCount(&seg), 1);
    ok(FreeSpaceTotal(&seg) == 1024, "1024 free\n");

    /* Free first block — should coalesce with middle */
    SegmentFree(&seg, off1, 1024);
    ok_eq_uint(FreeBlockCount(&seg), 1);
    ok(FreeSpaceTotal(&seg) == 2048, "2048 free after coalesce\n");

    /* Free last block — should coalesce into one big block */
    SegmentFree(&seg, off3, 1024);
    ok_eq_uint(FreeBlockCount(&seg), 1);
    ok(FreeSpaceTotal(&seg) == 3072, "Full segment free after triple coalesce\n");

    /* The single free block should cover entire segment */
    ok(seg.FreeListHead->Offset == 0, "Coalesced block at offset 0\n");
    ok(seg.FreeListHead->Size == 3072, "Coalesced block covers full segment\n");

    SegmentCleanup(&seg);
}

static void Test_FragmentationAndCoalescing(void)
{
    TEST_SEGMENT seg;
    ULONGLONG offsets[4];

    SegmentInit(&seg, 4096, 0);

    /* Allocate 4 x 1024, then free alternating to create fragmentation */
    SegmentAlloc(&seg, 1024, 1, &offsets[0]);
    SegmentAlloc(&seg, 1024, 1, &offsets[1]);
    SegmentAlloc(&seg, 1024, 1, &offsets[2]);
    SegmentAlloc(&seg, 1024, 1, &offsets[3]);

    /* Free [0] and [2] — two non-adjacent free blocks */
    SegmentFree(&seg, offsets[0], 1024);
    SegmentFree(&seg, offsets[2], 1024);

    ok(FreeSpaceTotal(&seg) == 2048, "2048 free from fragments\n");
    ok_eq_uint(FreeBlockCount(&seg), 2);

    /* Cannot allocate 2048 contiguous — fragmented */
    ULONGLONG bigOffset;
    BOOL r = SegmentAlloc(&seg, 2048, 1, &bigOffset);
    ok(!r, "2048 contiguous should fail when fragmented\n");

    /* But 1024 should work (fits in either hole) */
    r = SegmentAlloc(&seg, 1024, 1, &bigOffset);
    ok(r, "1024 should fit in a fragment hole\n");

    SegmentCleanup(&seg);
}

static void Test_AlignmentEdgeCases(void)
{
    TEST_SEGMENT seg;
    ULONGLONG offset;

    SegmentInit(&seg, 4096, 0);

    /* Align to page size */
    BOOL r = SegmentAlloc(&seg, 256, 4096, &offset);
    ok(r, "Page-aligned allocation should succeed\n");
    ok(offset == 0, "First page-aligned alloc at 0\n");

    /* Second page-aligned alloc */
    r = SegmentAlloc(&seg, 256, 4096, &offset);
    ok(!r, "Second page-aligned alloc should fail (no room at 4096)\n");

    SegmentCleanup(&seg);

    /* Large alignment in larger segment */
    SegmentInit(&seg, 65536, 0);

    SegmentAlloc(&seg, 100, 1, &offset); /* waste 100 bytes */
    r = SegmentAlloc(&seg, 256, 4096, &offset);
    ok(r, "4K-aligned in 64K segment should succeed\n");
    ok(offset == 4096, "Should be aligned to 4096, got %llu\n", offset);
    ok((offset % 4096) == 0, "Must be page-aligned\n");

    SegmentCleanup(&seg);
}

static void Test_LRUEvictionOrder(void)
{
    /* Simulate LRU eviction: evict lowest priority, then oldest timestamp */
    TEST_ALLOC_ENTRY entries[4];
    ULONG i, evictIdx;
    ULONG lowestPriority;
    ULONGLONG oldestTimestamp;

    /* Setup: different priorities and timestamps */
    entries[0].Priority = 2; entries[0].LastUseTimestamp = 100;
    entries[1].Priority = 1; entries[1].LastUseTimestamp = 200; /* lowest prio */
    entries[2].Priority = 1; entries[2].LastUseTimestamp = 50;  /* lowest prio, oldest */
    entries[3].Priority = 3; entries[3].LastUseTimestamp = 10;

    /* Find eviction victim: lowest priority, then oldest timestamp */
    evictIdx = 0;
    lowestPriority = entries[0].Priority;
    oldestTimestamp = entries[0].LastUseTimestamp;

    for (i = 1; i < 4; ++i)
    {
        if (entries[i].Priority < lowestPriority ||
            (entries[i].Priority == lowestPriority &&
             entries[i].LastUseTimestamp < oldestTimestamp))
        {
            evictIdx = i;
            lowestPriority = entries[i].Priority;
            oldestTimestamp = entries[i].LastUseTimestamp;
        }
    }

    ok_eq_uint(evictIdx, 2);
    ok_eq_uint(lowestPriority, 1);
    ok(oldestTimestamp == 50, "Oldest timestamp should be 50\n");
}

static void Test_AlignUpHelper(void)
{
    ok(AlignUp(0, 16) == 0, "AlignUp(0,16)=0\n");
    ok(AlignUp(1, 16) == 16, "AlignUp(1,16)=16\n");
    ok(AlignUp(15, 16) == 16, "AlignUp(15,16)=16\n");
    ok(AlignUp(16, 16) == 16, "AlignUp(16,16)=16\n");
    ok(AlignUp(17, 16) == 32, "AlignUp(17,16)=32\n");
    ok(AlignUp(0, 4096) == 0, "AlignUp(0,4096)=0\n");
    ok(AlignUp(1, 4096) == 4096, "AlignUp(1,4096)=4096\n");
    ok(AlignUp(4095, 4096) == 4096, "AlignUp(4095,4096)=4096\n");
    ok(AlignUp(4096, 4096) == 4096, "AlignUp(4096,4096)=4096\n");
    ok(AlignUp(4097, 4096) == 8192, "AlignUp(4097,4096)=8192\n");

    /* Alignment of 1 is identity */
    ok(AlignUp(42, 1) == 42, "AlignUp(42,1)=42\n");
    ok(AlignUp(0, 1) == 0, "AlignUp(0,1)=0\n");

    /* Alignment of 0 returns unchanged */
    ok(AlignUp(42, 0) == 42, "AlignUp(42,0)=42\n");
}

START_TEST(VidMm)
{
    Test_SegmentFlags();
    Test_SegmentInit();
    Test_SimpleAllocation();
    Test_AlignedAllocation();
    Test_MultipleAllocations();
    Test_AllocationExhaustion();
    Test_FreeAndReuse();
    Test_Coalescing();
    Test_FragmentationAndCoalescing();
    Test_AlignmentEdgeCases();
    Test_LRUEvictionOrder();
    Test_AlignUpHelper();
}
