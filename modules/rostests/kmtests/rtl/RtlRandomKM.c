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
    ULONG Seed;
    ULONG V1, V2;
    ULONG i, Changed;

    /*
     * Win11's RtlRandom/RtlRandomEx use mutable global state, so they are NOT
     * deterministic per seed: two calls with the same input seed yield
     * different results (the global vector advances between calls). ReactOS's
     * RtlRandom is currently deterministic per seed, but the only portable,
     * Win11-aligned contract is: the seed advances and the sequence varies.
     * (Validated against the Win11 reference kernel.)
     */
    Seed = 0x12345678;
    V1 = RtlRandom(&Seed);
    ok(Seed != 0x12345678, "RtlRandom: seed not advanced\n");
    V2 = RtlRandom(&Seed);
    ok(V2 != V1, "RtlRandom: consecutive values equal (0x%lx)\n", V1);

    Seed = 0x12345678;
    V1 = RtlRandomEx(&Seed);
    ok(Seed != 0x12345678, "RtlRandomEx: seed not advanced\n");

    Changed = 0;
    V1 = RtlRandomEx(&Seed);
    for (i = 0; i < 64; i++)
    {
        V2 = RtlRandomEx(&Seed);
        if (V2 != V1) Changed++;
        V1 = V2;
    }
    ok(Changed >= 60, "RtlRandomEx: sequence too static: %lu changes\n", Changed);
}
