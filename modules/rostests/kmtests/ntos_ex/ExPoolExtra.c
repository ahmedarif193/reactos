/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite extended pool allocator coverage
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'xPmK'
#define BASE_POOL_TYPE_MASK 3

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
    PVOID Zeroed;
    PULONG p;
    ULONG i;

    Zeroed = ExAllocatePoolZero(NonPagedPool, 256, TAG_TEST);
    if (Zeroed == NULL)
        Zeroed = ExAllocatePoolWithTag(NonPagedPool, 256, TAG_TEST);
    ok(Zeroed != NULL, "alloc failed\n");
    if (Zeroed != NULL)
    {
        p = Zeroed;
        for (i = 0; i < 256 / sizeof(ULONG); i++)
        {
            if (p[i] != 0)
            {
                p[0] = 0xDEAD;
                break;
            }
        }
        ExFreePoolWithTag(Zeroed, TAG_TEST);
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
        ok((KmtGetPoolType(NonPaged) - 1 & BASE_POOL_TYPE_MASK) == NonPagedPool,
           "nonpaged type %x\n", KmtGetPoolType(NonPaged));
    if (Paged && ((ULONG_PTR)Paged & (PAGE_SIZE - 1)))
        ok((KmtGetPoolType(Paged) - 1 & BASE_POOL_TYPE_MASK) == PagedPool,
           "paged type %x\n", KmtGetPoolType(Paged));

    if (NonPaged) ExFreePoolWithTag(NonPaged, TAG_TEST);
    if (Paged) ExFreePoolWithTag(Paged, TAG_TEST);
}

START_TEST(ExPoolExtra)
{
    TestTagPreservation();
    TestZeroAndUninit();
    TestAlignment();
    TestLargeAllocations();
    TestPoolType();
}
