/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-Mode Test Suite for memory resources of 4 GB and above
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

/*
 * A discrete graphics adapter exposes its whole video memory through a single
 * prefetchable aperture, so its base address register asks for 4 GB or more.
 * That length does not fit the 32-bit field of an ordinary resource
 * descriptor, and the large-memory form carries it scaled instead.  A build
 * that mishandles the scaling silently drops the aperture and starts the
 * adapter with no frame buffer.
 */

#define KMT_LARGE_LEN_4G   0x100000000ULL
#define KMT_LARGE_LEN_8G   0x200000000ULL

static
VOID
TestLargeMemoryDescriptorLayout(VOID)
{
    IO_RESOURCE_DESCRIPTOR IoDesc;
    CM_PARTIAL_RESOURCE_DESCRIPTOR CmDesc;

    /* The scaled fields have to alias the ordinary ones, so that code reading
     * the start address does not need to know which form it holds. */
    ok_eq_size(FIELD_OFFSET(IO_RESOURCE_DESCRIPTOR, u.Memory40.MinimumAddress),
               FIELD_OFFSET(IO_RESOURCE_DESCRIPTOR, u.Memory.MinimumAddress));
    ok_eq_size(FIELD_OFFSET(IO_RESOURCE_DESCRIPTOR, u.Memory64.MaximumAddress),
               FIELD_OFFSET(IO_RESOURCE_DESCRIPTOR, u.Memory.MaximumAddress));
    ok_eq_size(FIELD_OFFSET(CM_PARTIAL_RESOURCE_DESCRIPTOR, u.Memory40.Start),
               FIELD_OFFSET(CM_PARTIAL_RESOURCE_DESCRIPTOR, u.Memory.Start));

    /* A 4 GB length scaled by 8 bits fits the 40-bit form exactly */
    RtlZeroMemory(&IoDesc, sizeof(IoDesc));
    IoDesc.Type = CmResourceTypeMemoryLarge;
    IoDesc.Flags = CM_RESOURCE_MEMORY_LARGE_40;
    IoDesc.u.Memory40.Length40 = (ULONG)(KMT_LARGE_LEN_4G >> 8);
    ok_eq_ulonglong((ULONGLONG)IoDesc.u.Memory40.Length40 << 8, KMT_LARGE_LEN_4G);
    ok(KMT_LARGE_LEN_4G <= CM_RESOURCE_MEMORY_LARGE_40_MAXLEN,
       "4 GB does not fit the 40-bit form\n");

    /* And the assigned descriptor round-trips the same way */
    RtlZeroMemory(&CmDesc, sizeof(CmDesc));
    CmDesc.Type = CmResourceTypeMemoryLarge;
    CmDesc.Flags = CM_RESOURCE_MEMORY_LARGE_40;
    CmDesc.u.Memory40.Start.QuadPart = 0x4000000000LL;
    CmDesc.u.Memory40.Length40 = (ULONG)(KMT_LARGE_LEN_8G >> 8);
    ok_eq_ulonglong((ULONGLONG)CmDesc.u.Memory40.Length40 << 8, KMT_LARGE_LEN_8G);
    ok_eq_longlong(CmDesc.u.Memory.Start.QuadPart, 0x4000000000LL);
}

static
VOID
TestLargeMemoryTruncation(VOID)
{
    ULONG Truncated;

    /*
     * The failure this guards against: storing a 4 GB length in a 32-bit
     * field yields zero, and a zero-length requirement is discarded rather
     * than rejected, so the loss is silent.
     */
    Truncated = (ULONG)KMT_LARGE_LEN_4G;
    ok_eq_ulong(Truncated, 0UL);

    Truncated = (ULONG)KMT_LARGE_LEN_8G;
    ok_eq_ulong(Truncated, 0UL);

    /* The scaled forms keep it */
    ok_eq_ulong((ULONG)(KMT_LARGE_LEN_4G >> 8), 0x1000000UL);
    ok_eq_ulong((ULONG)(KMT_LARGE_LEN_8G >> 32), 2UL);
}

START_TEST(IoLargeMemoryResource)
{
    TestLargeMemoryDescriptorLayout();
    TestLargeMemoryTruncation();
}
