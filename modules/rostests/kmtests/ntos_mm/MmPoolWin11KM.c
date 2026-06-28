/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Aggressive Win11-parity coverage of the executive POOL allocator
 *
 * Win11 <-> ReactOS parity push.  The SAME compiled kmtest driver runs on the
 * Windows 11 ARM64 reference kernel (ground truth) AND on ReactOS.  Every check
 * below asserts a behaviour that is OBSERVABLE from a kernel-mode driver
 * (non-NULL, alignment, writability, round-trip block size, zero-on-alloc), so
 * a contract mismatch between the two runs pinpoints a ReactOS divergence
 * instead of a host-specific artefact.
 *
 * ARM64 notes that drive the assertions:
 *   - MEMORY_ALLOCATION_ALIGNMENT == 16 on ARM64 (it is an _WIN64 target), so
 *     every pool block must be at least 16-byte aligned.
 *   - PAGE_SIZE == 0x1000; allocations of a page or more come from the big-pool
 *     path and are PAGE-aligned, while sub-page allocations are 16-aligned.
 *   - The cache line used by the *CacheAligned pool types is 64 bytes on ARM64.
 *   - ARM64 is weakly ordered; the writability read-backs use volatile loads so
 *     the compiler cannot elide the store/load pair.
 *
 * SAFETY (bug-checking the Win11 reference is extremely expensive):
 *   - Only valid sizes (>= 1, never 0) and printable, non-protected tags.
 *   - EVERY allocation is freed on EVERY path, including SEH unwinds.
 *   - Freed memory is never read; no bogus pointers are ever passed to an API.
 *   - APIs that may be absent on a runner are reached only via a runtime lookup
 *     (KmtGetSystemRoutineAddress) AND a GetNTVersion() gate AND an SEH guard,
 *     so an unresolved/odd call degrades to a graceful skip().
 *   - ROS-private pool-header probes (KmtGetPoolTag/KmtGetPoolType) are
 *     deliberately NOT used here: the Win11 kernel pool header layout differs,
 *     which would produce false "divergences".  Tag handling is exercised purely
 *     through alloc/fill/free round-trips.
 *
 * ROS divergence target:
 *   - ExQueryPoolBlockSize() is a stub on ReactOS (UNIMPLEMENTED; returns 0 and
 *     never writes *QuotaCharged).  Win11 returns a sane block size and sets the
 *     flag.  TestQueryPoolBlockSize() asserts the Win11 contract, so it passes on
 *     Win11 and fails (cleanly, no crash) on ReactOS.
 */

#include <kmt_test.h>

/* Distinct, printable, non-protected (bit 31 clear) pool tags. */
#define TAG_BASIC    'BlpK'   /* basic alloc/free               */
#define TAG_ALIGN    'AlpK'   /* alignment contract             */
#define TAG_QUERY    'QlpK'   /* ExQueryPoolBlockSize           */
#define TAG_QUOTA    'TlpK'   /* ExAllocatePoolWithQuotaTag     */
#define TAG_CACHE    'ClpK'   /* cache-aligned pools            */
#define TAG_ZERO     'ZlpK'   /* zero-on-alloc                  */
#define TAG_PRIO     'RlpK'   /* ExAllocatePoolWithTagPriority  */
#define TAG_FREE     'FlpK'   /* free-path variants             */
#define TAG_SMALL    'SlpK'   /* smallest valid allocation      */

/* A distinct, printable, non-protected tag per index (0..31 -> 'K','P','m','0'+i). */
#define MANYTAG(i)   (0x4B506D00u | (ULONG)(0x30 + (i)))

/*
 * POOL_FLAG_* values are not present in the ReactOS headers (ExAllocatePool2 is
 * a Win10 2004+ API).  Only the flag we actually use is defined locally.  By
 * contract ExAllocatePool2 zero-initialises unless POOL_FLAG_UNINITIALIZED is
 * set, so POOL_FLAG_NON_PAGED alone yields a zeroed non-paged block.
 */
#define POOLW_FLAG_NON_PAGED  0x0000000000000040ULL

/* Pool cache-line alignment is 64 bytes on ARM64 (and x64). */
#define POOLW_CACHE_LINE      64

typedef PVOID (NTAPI *PFN_EXALLOCATEPOOL2)(ULONGLONG Flags, SIZE_T NumberOfBytes, ULONG Tag);

#define POOLW_IS_ALIGNED(p, a)  (((ULONG_PTR)(p) & ((ULONG_PTR)(a) - 1)) == 0)

/*
 * Fill the whole block and read back the first and last byte through volatile
 * loads.  Returns TRUE if the block is genuine, writable RAM.  Size must be >=1.
 */
static
BOOLEAN
PoolwWritable(
    _In_ PVOID Block,
    _In_ SIZE_T Size,
    _In_ UCHAR Pattern)
{
    volatile UCHAR *Bytes = (volatile UCHAR *)Block;

    RtlFillMemory(Block, Size, Pattern);
    return (BOOLEAN)(Bytes[0] == Pattern && Bytes[Size - 1] == Pattern);
}

/*
 * ExAllocatePoolWithTag for both paged and non-paged pool across a spread of
 * sizes (smallest valid, sub-page, page, multi-page).  Each block must be
 * non-NULL, at least MEMORY_ALLOCATION_ALIGNMENT (16) aligned, PAGE-aligned when
 * it is a page or larger, and real writable memory; then it must free cleanly.
 */
static
VOID
TestBasicAllocFree(VOID)
{
    static const POOL_TYPE Pools[] = { NonPagedPool, PagedPool };
    static const SIZE_T Sizes[] =
    {
        1, 8, 16, 64, 100, 1024,
        PAGE_SIZE - 256, PAGE_SIZE, 2 * PAGE_SIZE, 4 * PAGE_SIZE
    };
    ULONG p, s;

    for (p = 0; p < RTL_NUMBER_OF(Pools); p++)
    {
        for (s = 0; s < RTL_NUMBER_OF(Sizes); s++)
        {
            POOL_TYPE PoolType = Pools[p];
            SIZE_T Size = Sizes[s];
            PVOID Block;

            Block = ExAllocatePoolWithTag(PoolType, Size, TAG_BASIC);
            ok(Block != NULL, "alloc pool=%d size=%Iu returned NULL\n",
               (int)PoolType, Size);
            if (Block == NULL)
                continue;

            ok(POOLW_IS_ALIGNED(Block, MEMORY_ALLOCATION_ALIGNMENT),
               "pool=%d size=%Iu block %p not %d-aligned\n",
               (int)PoolType, Size, Block, MEMORY_ALLOCATION_ALIGNMENT);

            if (Size >= PAGE_SIZE)
                ok(POOLW_IS_ALIGNED(Block, PAGE_SIZE),
                   "pool=%d size=%Iu large block %p not page-aligned\n",
                   (int)PoolType, Size, Block);

            ok(PoolwWritable(Block, Size, 0xAB),
               "pool=%d size=%Iu block %p not writable\n",
               (int)PoolType, Size, Block);

            ExFreePoolWithTag(Block, TAG_BASIC);
        }
    }
}

/*
 * The alignment contract, stated explicitly: every sub-page allocation is
 * 16-aligned, every allocation of a page or more is PAGE-aligned.
 */
static
VOID
TestAlignmentContract(VOID)
{
    static const SIZE_T SmallSizes[] = { 1, 8, 15, 16, 17, 31, 32, 48, 63, 64, 128, 255 };
    static const SIZE_T LargeSizes[] = { PAGE_SIZE, PAGE_SIZE + 1, 2 * PAGE_SIZE, 8 * PAGE_SIZE };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(SmallSizes); i++)
    {
        PVOID Block = ExAllocatePoolWithTag(NonPagedPool, SmallSizes[i], TAG_ALIGN);
        ok(Block != NULL, "small alloc %Iu returned NULL\n", SmallSizes[i]);
        if (Block == NULL)
            continue;
        ok(POOLW_IS_ALIGNED(Block, MEMORY_ALLOCATION_ALIGNMENT),
           "small alloc %Iu block %p not %d-aligned\n",
           SmallSizes[i], Block, MEMORY_ALLOCATION_ALIGNMENT);
        ExFreePoolWithTag(Block, TAG_ALIGN);
    }

    for (i = 0; i < RTL_NUMBER_OF(LargeSizes); i++)
    {
        PVOID Block = ExAllocatePoolWithTag(NonPagedPool, LargeSizes[i], TAG_ALIGN);
        ok(Block != NULL, "large alloc %Iu returned NULL\n", LargeSizes[i]);
        if (Block == NULL)
            continue;
        ok(POOLW_IS_ALIGNED(Block, PAGE_SIZE),
           "large alloc %Iu block %p not page-aligned\n", LargeSizes[i], Block);
        ExFreePoolWithTag(Block, TAG_ALIGN);
    }
}

/*
 * Many simultaneously-live allocations carrying distinct tags must each be
 * non-NULL, mutually distinct (the allocator never aliases live blocks) and
 * independently writable.  Exercises tag handling without probing the (runner-
 * specific) pool header.
 */
static
VOID
TestDistinctTags(VOID)
{
    static const POOL_TYPE Pools[] = { NonPagedPool, PagedPool };
    static const ULONG Tags[] = { 'aT01', 'bT02', 'cT03', 'dT04',
                                  'eT05', 'fT06', 'gT07', 'hT08' };
    ULONG p, i, j;

    for (p = 0; p < RTL_NUMBER_OF(Pools); p++)
    {
        PVOID Blocks[RTL_NUMBER_OF(Tags)];

        for (i = 0; i < RTL_NUMBER_OF(Tags); i++)
        {
            Blocks[i] = ExAllocatePoolWithTag(Pools[p], 64, Tags[i]);
            ok(Blocks[i] != NULL, "pool=%d tag %lu alloc NULL\n", (int)Pools[p], i);
            if (Blocks[i] != NULL)
                ok(PoolwWritable(Blocks[i], 64, (UCHAR)(0x10 + i)),
                   "pool=%d tag %lu block not writable\n", (int)Pools[p], i);
        }

        for (i = 0; i < RTL_NUMBER_OF(Tags); i++)
            for (j = i + 1; j < RTL_NUMBER_OF(Tags); j++)
                if (Blocks[i] != NULL && Blocks[j] != NULL)
                    ok(Blocks[i] != Blocks[j],
                       "pool=%d live blocks %lu and %lu alias at %p\n",
                       (int)Pools[p], i, j, Blocks[i]);

        for (i = 0; i < RTL_NUMBER_OF(Tags); i++)
            if (Blocks[i] != NULL)
                ExFreePoolWithTag(Blocks[i], Tags[i]);
    }
}

/*
 * ExQueryPoolBlockSize: ROS STUB (UNIMPLEMENTED -> returns 0, never touches
 * *QuotaCharged).  The Win11 contract is: the reported block size is at least
 * the requested size and stays sane (a sub-page request cannot report more than
 * a page of headroom), and *QuotaCharged is written (FALSE for a non-quota
 * block).  SEH-guarded; the allocation is freed on every path.  Passes on Win11,
 * diverges on ReactOS.
 */
static
VOID
TestQueryPoolBlockSize(VOID)
{
    static const POOL_TYPE Pools[] = { NonPagedPool, PagedPool };
    static const SIZE_T Requested = 64;
    ULONG p;

    for (p = 0; p < RTL_NUMBER_OF(Pools); p++)
    {
        PVOID Block;
        SIZE_T BlockSize = 0;
        BOOLEAN QuotaCharged = (BOOLEAN)0xAA;   /* sentinel: must be overwritten */
        NTSTATUS Status = STATUS_SUCCESS;

        Block = ExAllocatePoolWithTag(Pools[p], Requested, TAG_QUERY);
        ok(Block != NULL, "pool=%d alloc NULL\n", (int)Pools[p]);
        if (Block == NULL)
            continue;

        _SEH2_TRY
        {
            BlockSize = ExQueryPoolBlockSize(Block, &QuotaCharged);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        ok(Status == STATUS_SUCCESS,
           "pool=%d ExQueryPoolBlockSize raised 0x%08lx\n", (int)Pools[p], Status);

        if (Status == STATUS_SUCCESS)
        {
            ok(BlockSize >= Requested,
               "pool=%d block size %Iu < requested %Iu (ROS stub returns 0)\n",
               (int)Pools[p], BlockSize, Requested);
            ok(BlockSize <= Requested + PAGE_SIZE,
               "pool=%d block size %Iu implausibly large\n",
               (int)Pools[p], BlockSize);
            ok(QuotaCharged == FALSE || QuotaCharged == TRUE,
               "pool=%d QuotaCharged not written (=0x%x)\n",
               (int)Pools[p], QuotaCharged);
        }

        ExFreePoolWithTag(Block, TAG_QUERY);
    }
}

/*
 * ExAllocatePoolWithQuotaTag (kept light - the process-reference accounting is
 * already covered by ntos_ex/ExPools).  POOL_QUOTA_FAIL_INSTEAD_OF_RAISE makes
 * failure return NULL instead of raising.  A small block is at least 16-aligned;
 * a page-sized block is PAGE-aligned.  SEH-guarded; freed on every path.
 */
static
VOID
TestQuotaTag(VOID)
{
    static const POOL_TYPE Pools[] = { PagedPool, NonPagedPool };
    ULONG p;

    for (p = 0; p < RTL_NUMBER_OF(Pools); p++)
    {
        PVOID Small = NULL;
        PVOID Page = NULL;
        NTSTATUS Status = STATUS_SUCCESS;

        _SEH2_TRY
        {
            Small = ExAllocatePoolWithQuotaTag(Pools[p] | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                               sizeof(LIST_ENTRY), TAG_QUOTA);
            Page = ExAllocatePoolWithQuotaTag(Pools[p] | POOL_QUOTA_FAIL_INSTEAD_OF_RAISE,
                                              PAGE_SIZE, TAG_QUOTA);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        ok(Status == STATUS_SUCCESS,
           "pool=%d quota alloc raised 0x%08lx with FAIL flag\n", (int)Pools[p], Status);

        ok(Small != NULL, "pool=%d small quota alloc NULL\n", (int)Pools[p]);
        if (Small != NULL)
        {
            ok(POOLW_IS_ALIGNED(Small, MEMORY_ALLOCATION_ALIGNMENT),
               "pool=%d small quota block %p not %d-aligned\n",
               (int)Pools[p], Small, MEMORY_ALLOCATION_ALIGNMENT);
            ok(PoolwWritable(Small, sizeof(LIST_ENTRY), 0x5A),
               "pool=%d small quota block not writable\n", (int)Pools[p]);
            ExFreePoolWithTag(Small, TAG_QUOTA);
        }

        ok(Page != NULL, "pool=%d page quota alloc NULL\n", (int)Pools[p]);
        if (Page != NULL)
        {
            ok(POOLW_IS_ALIGNED(Page, PAGE_SIZE),
               "pool=%d page quota block %p not page-aligned\n", (int)Pools[p], Page);
            ok(PoolwWritable(Page, PAGE_SIZE, 0x5A),
               "pool=%d page quota block not writable\n", (int)Pools[p]);
            ExFreePoolWithTag(Page, TAG_QUOTA);
        }
    }
}

/*
 * NonPagedPoolCacheAligned / PagedPoolCacheAligned: the returned VA must be
 * cache-line aligned (64 bytes on ARM64).  We hard-require the 16-byte floor
 * (always true on Win11) and assert the 64-byte cache-line contract; the
 * observed low bits are traced so a mismatch is easy to interpret.
 */
static
VOID
TestCacheAligned(VOID)
{
    static const POOL_TYPE Pools[] = { NonPagedPoolCacheAligned, PagedPoolCacheAligned };
    static const SIZE_T Sizes[] = { 8, 64, 200, 512 };
    ULONG p, s;

    for (p = 0; p < RTL_NUMBER_OF(Pools); p++)
    {
        for (s = 0; s < RTL_NUMBER_OF(Sizes); s++)
        {
            PVOID Block = ExAllocatePoolWithTag(Pools[p], Sizes[s], TAG_CACHE);

            ok(Block != NULL, "cache pool=%d size=%Iu alloc NULL\n",
               (int)Pools[p], Sizes[s]);
            if (Block == NULL)
                continue;

            ok(POOLW_IS_ALIGNED(Block, MEMORY_ALLOCATION_ALIGNMENT),
               "cache pool=%d size=%Iu block %p not %d-aligned\n",
               (int)Pools[p], Sizes[s], Block, MEMORY_ALLOCATION_ALIGNMENT);

            ok(POOLW_IS_ALIGNED(Block, POOLW_CACHE_LINE),
               "cache pool=%d size=%Iu block %p not %d-byte cache-aligned (low=0x%Ix)\n",
               (int)Pools[p], Sizes[s], Block, POOLW_CACHE_LINE,
               (ULONG_PTR)Block & (POOLW_CACHE_LINE - 1));

            ok(PoolwWritable(Block, Sizes[s], 0xCC),
               "cache pool=%d size=%Iu block not writable\n", (int)Pools[p], Sizes[s]);

            ExFreePoolWithTag(Block, TAG_CACHE);
        }
    }
}

/*
 * Zero-on-allocation.
 *   Part A: ExAllocatePoolZero (a header FORCEINLINE: alloc + RtlZeroMemory) is
 *           always present and must return an all-zero buffer on both runners.
 *   Part B: ExAllocatePool2 (Win10 2004+) zero-initialises by default.  It is a
 *           ROS ARM64 spec stub, so it is reached only when GetNTVersion()
 *           reports Win10+ AND the symbol resolves at runtime AND inside SEH;
 *           otherwise the case is skipped with a trace.
 */
static
VOID
TestZeroOnAlloc(VOID)
{
    static const POOL_TYPE Pools[] = { NonPagedPool, PagedPool };
    const SIZE_T Size = 512;
    ULONG p, i;
    PFN_EXALLOCATEPOOL2 pExAllocatePool2;

    /* Part A - ExAllocatePoolZero, both pools. */
    for (p = 0; p < RTL_NUMBER_OF(Pools); p++)
    {
        PVOID Block = ExAllocatePoolZero(Pools[p], Size, TAG_ZERO);
        ok(Block != NULL, "ExAllocatePoolZero pool=%d NULL\n", (int)Pools[p]);
        if (Block != NULL)
        {
            volatile UCHAR *Bytes = (volatile UCHAR *)Block;
            BOOLEAN Zeroed = TRUE;
            for (i = 0; i < Size; i++)
            {
                if (Bytes[i] != 0)
                {
                    Zeroed = FALSE;
                    break;
                }
            }
            ok(Zeroed, "ExAllocatePoolZero pool=%d not zeroed at +%lu\n", (int)Pools[p], i);
            ExFreePoolWithTag(Block, TAG_ZERO);
        }
    }

    /* Part B - ExAllocatePool2 (Win10+), non-paged, zero by default. */
    if (skip(GetNTVersion() >= _WIN32_WINNT_WIN10,
             "ExAllocatePool2 requires Win10+ (NT 0x%04x)\n", GetNTVersion()))
        return;

    pExAllocatePool2 = (PFN_EXALLOCATEPOOL2)KmtGetSystemRoutineAddress(L"ExAllocatePool2");
    if (skip(pExAllocatePool2 != NULL, "ExAllocatePool2 not exported by this kernel\n"))
        return;

    {
        PVOID Block = NULL;
        NTSTATUS Status = STATUS_SUCCESS;

        _SEH2_TRY
        {
            Block = pExAllocatePool2(POOLW_FLAG_NON_PAGED, Size, TAG_ZERO);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        if (Status != STATUS_SUCCESS)
        {
            trace("ExAllocatePool2 raised 0x%08lx - treating as unavailable\n", Status);
            return;
        }

        ok(Block != NULL, "ExAllocatePool2 returned NULL\n");
        if (Block != NULL)
        {
            volatile UCHAR *Bytes = (volatile UCHAR *)Block;
            BOOLEAN Zeroed = TRUE;
            for (i = 0; i < Size; i++)
            {
                if (Bytes[i] != 0)
                {
                    Zeroed = FALSE;
                    break;
                }
            }
            ok(Zeroed, "ExAllocatePool2 buffer not zeroed at +%lu\n", i);
            ok(POOLW_IS_ALIGNED(Block, MEMORY_ALLOCATION_ALIGNMENT),
               "ExAllocatePool2 block %p not %d-aligned\n",
               Block, MEMORY_ALLOCATION_ALIGNMENT);
            ExFreePoolWithTag(Block, TAG_ZERO);
        }
    }
}

/*
 * ExAllocatePoolWithTagPriority (exported by both runners): exercise normal and
 * high priority across a sub-page and a page-sized request.  Every block must be
 * non-NULL, correctly aligned and writable, then free cleanly.
 */
static
VOID
TestAllocPriority(VOID)
{
    static const EX_POOL_PRIORITY Priorities[] = { NormalPoolPriority, HighPoolPriority };
    static const SIZE_T Sizes[] = { 64, PAGE_SIZE };
    ULONG pr, s;

    for (pr = 0; pr < RTL_NUMBER_OF(Priorities); pr++)
    {
        for (s = 0; s < RTL_NUMBER_OF(Sizes); s++)
        {
            PVOID Block = ExAllocatePoolWithTagPriority(NonPagedPool, Sizes[s],
                                                        TAG_PRIO, Priorities[pr]);
            ok(Block != NULL, "priority=%d size=%Iu alloc NULL\n",
               (int)Priorities[pr], Sizes[s]);
            if (Block == NULL)
                continue;

            ok(POOLW_IS_ALIGNED(Block, MEMORY_ALLOCATION_ALIGNMENT),
               "priority=%d size=%Iu block %p not %d-aligned\n",
               (int)Priorities[pr], Sizes[s], Block, MEMORY_ALLOCATION_ALIGNMENT);
            if (Sizes[s] >= PAGE_SIZE)
                ok(POOLW_IS_ALIGNED(Block, PAGE_SIZE),
                   "priority=%d size=%Iu block %p not page-aligned\n",
                   (int)Priorities[pr], Sizes[s], Block);
            ok(PoolwWritable(Block, Sizes[s], 0x3C),
               "priority=%d size=%Iu block not writable\n",
               (int)Priorities[pr], Sizes[s]);

            ExFreePoolWithTag(Block, TAG_PRIO);
        }
    }
}

/*
 * Free-path variants: a tagged block frees with ExFreePoolWithTag, and a block
 * also frees with the untagged ExFreePool (legal on Windows regardless of the
 * allocation tag).  After freeing, the pool is still usable for a fresh block.
 */
static
VOID
TestFreePathVariants(VOID)
{
    static const POOL_TYPE Pools[] = { NonPagedPool, PagedPool };
    ULONG p;

    for (p = 0; p < RTL_NUMBER_OF(Pools); p++)
    {
        PVOID A, B, C;

        A = ExAllocatePoolWithTag(Pools[p], 64, TAG_FREE);
        ok(A != NULL, "pool=%d alloc A NULL\n", (int)Pools[p]);
        if (A != NULL)
            ExFreePoolWithTag(A, TAG_FREE);

        B = ExAllocatePoolWithTag(Pools[p], 64, TAG_FREE);
        ok(B != NULL, "pool=%d alloc B NULL\n", (int)Pools[p]);
        if (B != NULL)
            ExFreePool(B);              /* untagged free of a tagged block */

        C = ExAllocatePoolWithTag(Pools[p], 128, TAG_FREE);
        ok(C != NULL, "pool=%d re-alloc after free NULL\n", (int)Pools[p]);
        if (C != NULL)
        {
            ok(PoolwWritable(C, 128, 0x77), "pool=%d re-alloc not writable\n", (int)Pools[p]);
            ExFreePoolWithTag(C, TAG_FREE);
        }
    }
}

/*
 * Smallest valid allocation.  Zero-byte behaviour is undefined and may
 * assert/bug-check, so it is NOT tested; 1 byte is the smallest valid request.
 * Even a 1-byte block is MEMORY_ALLOCATION_ALIGNMENT aligned and writable.
 */
static
VOID
TestSmallestValid(VOID)
{
    static const POOL_TYPE Pools[] = { NonPagedPool, PagedPool };
    ULONG p, iter;

    for (p = 0; p < RTL_NUMBER_OF(Pools); p++)
    {
        for (iter = 0; iter < 4; iter++)
        {
            PVOID Block = ExAllocatePoolWithTag(Pools[p], 1, TAG_SMALL);
            ok(Block != NULL, "pool=%d 1-byte alloc NULL (iter %lu)\n", (int)Pools[p], iter);
            if (Block == NULL)
                continue;
            ok(POOLW_IS_ALIGNED(Block, MEMORY_ALLOCATION_ALIGNMENT),
               "pool=%d 1-byte block %p not %d-aligned\n",
               (int)Pools[p], Block, MEMORY_ALLOCATION_ALIGNMENT);
            ok(PoolwWritable(Block, 1, 0xE7),
               "pool=%d 1-byte block not writable\n", (int)Pools[p]);
            ExFreePoolWithTag(Block, TAG_SMALL);
        }
    }
}

/*
 * Tag round-trip across many distinct tags: allocate, stamp, read back and free
 * with each tag.  Drives the tag path hard without inspecting the pool header.
 */
static
VOID
TestManyTags(VOID)
{
    ULONG i;

    for (i = 0; i < 32; i++)
    {
        ULONG Tag = MANYTAG(i);
        PVOID Block = ExAllocatePoolWithTag(NonPagedPool, 48, Tag);

        ok(Block != NULL, "many-tag alloc %lu (tag 0x%08lx) NULL\n", i, Tag);
        if (Block == NULL)
            continue;
        ok(PoolwWritable(Block, 48, (UCHAR)(i + 1)),
           "many-tag block %lu not writable\n", i);
        ExFreePoolWithTag(Block, Tag);
    }
}

START_TEST(MmPoolWin11KM)
{
    trace("MmPoolWin11KM: NT version 0x%04x, MEMORY_ALLOCATION_ALIGNMENT=%d, PAGE_SIZE=%lu\n",
          GetNTVersion(), MEMORY_ALLOCATION_ALIGNMENT, (ULONG)PAGE_SIZE);

    TestBasicAllocFree();
    TestAlignmentContract();
    TestDistinctTags();
    TestQueryPoolBlockSize();   /* ROS stub -> diverges */
    TestQuotaTag();
    TestCacheAligned();
    TestZeroOnAlloc();          /* ExAllocatePoolZero always; ExAllocatePool2 Win10+ */
    TestAllocPriority();
    TestFreePathVariants();
    TestSmallestValid();
    TestManyTags();
}
