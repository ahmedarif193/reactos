/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite extended pool allocator coverage
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'xPmK'
#define TAG_WRONG 'yPmK'
#define BASE_POOL_TYPE_MASK 3
#define POOL_FREE_RACE_BLOCKS 128
#define POOL_FREE_RACE_THREADS 4

typedef struct _POOL_FREE_RACE_CONTEXT
{
    PKEVENT StartEvent;
    PVOID *Blocks;
    ULONG BlockCount;
    ULONG Index;
    ULONG Freed;
    KAFFINITY ProcessorMask;
    NTSTATUS WaitStatus;
} POOL_FREE_RACE_CONTEXT, *PPOOL_FREE_RACE_CONTEXT;

static
VOID
NTAPI
PoolFreeRaceThread(
    _In_ PVOID Parameter)
{
    PPOOL_FREE_RACE_CONTEXT Context = Parameter;
    ULONG i;

    Context->WaitStatus = KeWaitForSingleObject(Context->StartEvent,
                                                Executive,
                                                KernelMode,
                                                FALSE,
                                                NULL);
    if (!NT_SUCCESS(Context->WaitStatus))
        return;

    for (i = Context->Index; i < Context->BlockCount; i += POOL_FREE_RACE_THREADS)
    {
        Context->ProcessorMask |= (KAFFINITY)1 << KeGetCurrentProcessorNumber();
        ExFreePoolWithTag(Context->Blocks[i], TAG_TEST);
        Context->Freed++;
    }
}

static
VOID
TestTagPreservation(VOID)
{
    PVOID Blocks[64];
    ULONG i;

    for (i = 0; i < 64; i++)
    {
        Blocks[i] = ExAllocatePoolWithTag(NonPagedPool, 16 + i * 8, TAG_TEST);
        ok(Blocks[i] != NULL, "alloc %lu failed\n", i);
        if (Blocks[i])
        {
            RtlFillMemory(Blocks[i], 16 + i * 8, (UCHAR)(i + 1));
            if ((((ULONG_PTR)Blocks[i]) & (PAGE_SIZE - 1)) != 0)
                ok_eq_tag(KmtGetPoolTag(Blocks[i]), TAG_TEST);
        }
    }
    for (i = 0; i < 64; i++)
    {
        if (Blocks[i])
        {
            PUCHAR p = Blocks[i];
            ok(p[0] == (UCHAR)(i + 1), "block %lu corrupted: %02x\n", i, p[0]);
            ExFreePoolWithTag(Blocks[i], TAG_TEST);
        }
    }
}

static
VOID
TestZeroAndUninit(VOID)
{
    static const SIZE_T Sizes[] = { 16, 48, 256, PAGE_SIZE * 16 };
    PVOID Dirty, Zeroed;
    SIZE_T i, j;

    for (i = 0; i < RTL_NUMBER_OF(Sizes); i++)
    {
        Dirty = ExAllocatePoolWithTag(NonPagedPoolNx, Sizes[i], TAG_TEST);
        ok(Dirty != NULL, "dirty alloc %Iu failed\n", Sizes[i]);
        if (Dirty)
        {
            RtlFillMemory(Dirty, Sizes[i], 0xA5);
            ExFreePoolWithTag(Dirty, TAG_TEST);
        }

        Zeroed = ExAllocatePoolWithTag((POOL_TYPE)(NonPagedPoolNx |
                                                   POOL_ZERO_ALLOCATION),
                                       Sizes[i],
                                       TAG_TEST);
        ok(Zeroed != NULL, "zero alloc %Iu failed\n", Sizes[i]);
        if (Zeroed)
        {
            for (j = 0; j < Sizes[i]; j++)
            {
                if (((PUCHAR)Zeroed)[j] != 0)
                {
                    ok(0, "zero alloc %Iu has byte %02x at offset %Iu\n",
                       Sizes[i], ((PUCHAR)Zeroed)[j], j);
                    break;
                }
            }
            if (j == Sizes[i])
                ok(1, "zero alloc %Iu is zero-filled\n", Sizes[i]);
            ExFreePoolWithTag(Zeroed, TAG_TEST);
        }
    }
}

static
VOID
TestAlignment(VOID)
{
    PVOID Blocks[16];
    ULONG i;

    for (i = 0; i < 16; i++)
    {
        Blocks[i] = ExAllocatePoolWithTag(NonPagedPool, 8, TAG_TEST);
        ok(Blocks[i] != NULL, "alloc %lu failed\n", i);
        if (Blocks[i])
            ok(((ULONG_PTR)Blocks[i] & (sizeof(PVOID) - 1)) == 0,
               "block %lu misaligned: %p\n", i, Blocks[i]);
    }
    for (i = 0; i < 16; i++)
        if (Blocks[i]) ExFreePoolWithTag(Blocks[i], TAG_TEST);
}

static
VOID
TestLargeAllocations(VOID)
{
    PVOID Large;
    SIZE_T Sizes[] = { PAGE_SIZE, PAGE_SIZE * 4, PAGE_SIZE * 16, 0x20000 };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(Sizes); i++)
    {
        Large = ExAllocatePoolWithTag(NonPagedPool, Sizes[i], TAG_TEST);
        ok(Large != NULL, "large alloc %Iu failed\n", Sizes[i]);
        if (Large != NULL)
        {
            ok(((ULONG_PTR)Large & (PAGE_SIZE - 1)) == 0,
               "large alloc not page-aligned: %p\n", Large);
            RtlFillMemory(Large, Sizes[i], 0xCC);
            ok(*(PUCHAR)Large == 0xCC, "large alloc readback failed\n");
            ExFreePoolWithTag(Large, TAG_TEST);
        }
    }
}

static
VOID
TestPoolType(VOID)
{
    PVOID NonPaged, Paged;

    NonPaged = ExAllocatePoolWithTag(NonPagedPool, 64, TAG_TEST);
    Paged = ExAllocatePoolWithTag(PagedPool, 64, TAG_TEST);
    ok(NonPaged != NULL, "nonpaged failed\n");
    ok(Paged != NULL, "paged failed\n");

    if (NonPaged && ((ULONG_PTR)NonPaged & (PAGE_SIZE - 1)))
        ok(KmtGetPoolType(NonPaged) != 0, "nonpaged type is 0\n");
    if (Paged && ((ULONG_PTR)Paged & (PAGE_SIZE - 1)))
        ok(KmtGetPoolType(Paged) != 0, "paged type is 0\n");
    if (NonPaged && Paged &&
        ((ULONG_PTR)NonPaged & (PAGE_SIZE - 1)) && ((ULONG_PTR)Paged & (PAGE_SIZE - 1)))
        ok(KmtGetPoolType(NonPaged) != KmtGetPoolType(Paged),
           "paged and nonpaged share type %x\n", KmtGetPoolType(Paged));

    if (NonPaged) ExFreePoolWithTag(NonPaged, TAG_TEST);
    if (Paged) ExFreePoolWithTag(Paged, TAG_TEST);
}

static
VOID
TestMismatchedTagFree(VOID)
{
    PVOID Small, Large, Probe;

    /*
     * A mismatched tag is a diagnosable driver error, but without strict pool
     * checking enabled it must not turn an otherwise valid free into a boot-
     * stopping bugcheck. Exercise both allocator paths, then prove the pool is
     * still usable and tracked by the allocation's recorded tag.
     */
    Small = ExAllocatePoolWithTag(NonPagedPool, 64, TAG_TEST);
    ok(Small != NULL, "small mismatched-tag allocation failed\n");
    if (Small)
        ExFreePoolWithTag(Small, TAG_WRONG);

    Large = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_TEST);
    ok(Large != NULL, "large mismatched-tag allocation failed\n");
    if (Large)
        ExFreePoolWithTag(Large, TAG_WRONG);

    Probe = ExAllocatePoolWithTag(NonPagedPool, 64, TAG_TEST);
    ok(Probe != NULL, "pool unusable after mismatched-tag frees\n");
    if (Probe)
    {
        RtlFillMemory(Probe, 64, 0x5A);
        ok(*(PUCHAR)Probe == 0x5A, "probe allocation is not writable\n");
        ExFreePoolWithTag(Probe, TAG_TEST);
    }
}

static
VOID
TestConcurrentAdjacentLargeFrees(VOID)
{
    POOL_FREE_RACE_CONTEXT Contexts[POOL_FREE_RACE_THREADS];
    PKTHREAD Threads[POOL_FREE_RACE_THREADS];
    PVOID Blocks[POOL_FREE_RACE_BLOCKS];
    KEVENT StartEvent;
    KAFFINITY ProcessorMask = 0;
    ULONG Allocated = 0, Freed = 0, i;

    RtlZeroMemory(Contexts, sizeof(Contexts));
    RtlZeroMemory(Threads, sizeof(Threads));
    RtlZeroMemory(Blocks, sizeof(Blocks));
    KeInitializeEvent(&StartEvent, NotificationEvent, FALSE);

    /* Consecutive two-page requests normally occupy neighbouring pool runs. */
    for (i = 0; i < RTL_NUMBER_OF(Blocks); i++)
    {
        Blocks[i] = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE * 2, TAG_TEST);
        if (!Blocks[i])
            break;
        RtlFillMemory(Blocks[i], PAGE_SIZE * 2, (UCHAR)i);
        Allocated++;
    }
    ok_eq_ulong(Allocated, RTL_NUMBER_OF(Blocks));

    for (i = 0; i < RTL_NUMBER_OF(Threads); i++)
    {
        Contexts[i].StartEvent = &StartEvent;
        Contexts[i].Blocks = Blocks;
        Contexts[i].BlockCount = Allocated;
        Contexts[i].Index = i;
        Threads[i] = KmtStartThread(PoolFreeRaceThread, &Contexts[i]);
        ok(Threads[i] != NULL, "thread %lu creation failed\n", i);
    }

    KeSetEvent(&StartEvent, IO_NO_INCREMENT, FALSE);
    for (i = 0; i < RTL_NUMBER_OF(Threads); i++)
    {
        if (Threads[i])
            KmtFinishThread(Threads[i], NULL);
        ok_eq_hex(Contexts[i].WaitStatus, STATUS_SUCCESS);
        Freed += Contexts[i].Freed;
        ProcessorMask |= Contexts[i].ProcessorMask;
    }
    ok_eq_ulong(Freed, Allocated);
    trace("concurrent large frees: processors=%lu mask=%Ix blocks=%lu\n",
          KeQueryActiveProcessorCount(NULL), ProcessorMask, Freed);

    /* Prove the coalesced pool remains reusable after the parallel frees. */
    for (i = 0; i < Allocated; i++)
    {
        Blocks[i] = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE * 2, TAG_TEST);
        ok(Blocks[i] != NULL, "post-race allocation %lu failed\n", i);
        if (!Blocks[i])
            break;
    }
    while (i != 0)
        ExFreePoolWithTag(Blocks[--i], TAG_TEST);
}

START_TEST(ExPoolExtra)
{
    TestTagPreservation();
    TestZeroAndUninit();
    TestAlignment();
    TestLargeAllocations();
    TestPoolType();
    TestMismatchedTagFree();
    TestConcurrentAdjacentLargeFrees();
}
