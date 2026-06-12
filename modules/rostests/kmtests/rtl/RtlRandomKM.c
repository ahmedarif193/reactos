/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite RTL random API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(RtlRandomKM)
{
    ULONG Seed1, Seed2, Seed;
    ULONG V1, V2, V3;
    ULONG i, Changed;

    Seed1 = 0x12345678;
    Seed2 = 0x12345678;
    V1 = RtlRandom(&Seed1);
    V2 = RtlRandom(&Seed2);
    ok_eq_ulong(V1, V2);
    ok(Seed1 != 0x12345678, "seed not advanced\n");
    ok_eq_ulong(Seed1, Seed2);

    V3 = RtlRandom(&Seed1);
    ok(V3 != V1, "consecutive randoms equal: %lx\n", V1);

    Seed1 = 0x12345678;
    Seed2 = 0x12345678;
    V1 = RtlRandomEx(&Seed1);
    V2 = RtlRandomEx(&Seed2);
    ok_eq_ulong(V1, V2);
    ok_eq_ulong(Seed1, Seed2);

    Seed = 0x12345678;
    Changed = 0;
    V1 = RtlRandomEx(&Seed);
    for (i = 0; i < 64; i++)
    {
        V2 = RtlRandomEx(&Seed);
        if (V2 != V1) Changed++;
        V1 = V2;
    }
    ok(Changed >= 60, "random sequence too static: %lu changes\n", Changed);
}
