/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Clean-room cache-range invalidation parity probe
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <kmt_test.h>

START_TEST(KeCacheInvalidation)
{
    DECLSPEC_ALIGN(64) UCHAR Data[128];

    RtlFillMemory(Data, sizeof(Data), 0x5A);
    trace("invalidating unaligned cache range %p, length %Iu\n",
          &Data[1],
          sizeof(Data) - 2);
    KeInvalidateRangeAllCaches(&Data[1], sizeof(Data) - 2);
    ok_eq_uint(Data[0], 0x5A);
    ok_eq_uint(Data[sizeof(Data) - 1], 0x5A);

    trace("invalidating zero-length cache range %p\n", Data);
    KeInvalidateRangeAllCaches(Data, 0);
    ok_eq_uint(Data[0], 0x5A);
}
