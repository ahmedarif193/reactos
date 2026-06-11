/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64 loader + cache layout
 *
 * Validates Prcb->Cache[6] (CACHE_DESCRIPTOR array), Prcb->CacheCount,
 * Pcr->SecondLevelCacheSize / SecondLevelCacheAssociativity, MHz,
 * CycleCounterFrequency, and that ProcessorModel/ProcessorRevision are USHORT.
 */

#include <kmt_test.h>

VOID Test_KeArm64LoaderCache(VOID);

#define dump_trace(...) do { trace(__VA_ARGS__); DbgPrint(__VA_ARGS__); } while (0)

#ifdef _M_ARM64

static VOID Arm64LoaderCacheCheck(VOID)
{
    PKIPCR Pcr = KeGetPcr();
    PKPRCB Prcb = KeGetCurrentPrcb();

    ok(Pcr != NULL && Prcb != NULL, "Pcr/Prcb NULL\n");
    if (!Pcr || !Prcb)
    {
        skip(FALSE, "Cannot probe loader/cache fields\n");
        return;
    }

    /* Prcb->Cache is 6 CACHE_DESCRIPTOR entries. */
    ok_eq_size(sizeof(Prcb->Cache) / sizeof(Prcb->Cache[0]), (SIZE_T)6);

    /* Prcb->CacheCount is a UCHAR. */
    ok_eq_size(sizeof(Prcb->CacheCount), (SIZE_T)1);

    /* Pcr->SecondLevelCacheSize is a ULONG; Associativity is a UCHAR. */
    ok_eq_size(sizeof(Pcr->SecondLevelCacheSize), (SIZE_T)4);
    ok_eq_size(sizeof(Pcr->SecondLevelCacheAssociativity), (SIZE_T)1);

    /* Frequency fields. */
    ok(Prcb->MHz > 0u, "MHz=%u\n", Prcb->MHz);
    ok(Prcb->CycleCounterFrequency > 0ULL,
       "CycleCounterFrequency=0\n");

    /* ProcessorVendorString is the Win11 2-byte slot. */
    ok_eq_size(sizeof(Prcb->ProcessorVendorString), (SIZE_T)2);

    /* ProcessorModel/ProcessorRevision are USHORTs (not legacy CpuType/CpuID). */
    ok_eq_size(sizeof(Prcb->ProcessorModel), (SIZE_T)2);
    ok_eq_size(sizeof(Prcb->ProcessorRevision), (SIZE_T)2);

    dump_trace("[arm64][KeArm64LoaderCache] L2=%u/%u MHz=%u CCF=%I64u Caches=%u\n",
               Pcr->SecondLevelCacheSize, Pcr->SecondLevelCacheAssociativity,
               Prcb->MHz, Prcb->CycleCounterFrequency,
               (ULONG)Prcb->CacheCount);
}

#endif /* _M_ARM64 */

START_TEST(KeArm64LoaderCache)
{
#ifndef _M_ARM64
    skip(FALSE, "KeArm64LoaderCache is ARM64-only\n");
#else
    dump_trace("[arm64][KeArm64LoaderCache] enter\n");
    Arm64LoaderCacheCheck();
#endif
}
