/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel-Mode Test Suite thread stack limits
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

START_TEST(IoStackLimits)
{
    ULONG_PTR Low = 0, High = 0;
    volatile UCHAR Local[32];
    ULONG_PTR Address = (ULONG_PTR)Local;
    PVOID Pool;

    IoGetStackLimits(&Low, &High);
    ok(Low < High, "Low = %p, High = %p\n", (PVOID)Low, (PVOID)High);
    ok(Address >= Low && Address < High, "Local %p outside %p-%p\n", (PVOID)Address, (PVOID)Low, (PVOID)High);

    ok_eq_ulong(IoWithinStackLimits(Address, sizeof(Local)), 1UL);
    ok_eq_ulong(IoWithinStackLimits(Address, 0), 1UL);
    ok_eq_ulong(IoWithinStackLimits(Low, High - Low), 1UL);
    ok_eq_ulong(IoWithinStackLimits(Low, High - Low + 1), 0UL);
    ok_eq_ulong(IoWithinStackLimits(Low - 1, 1), 0UL);
    ok_eq_ulong(IoWithinStackLimits(Low, 1), 1UL);
    ok_eq_ulong(IoWithinStackLimits(High - 1, 1), 1UL);
    ok_eq_ulong(IoWithinStackLimits(High, 0), 1UL);
    ok_eq_ulong(IoWithinStackLimits(High, 1), 0UL);
    ok_eq_ulong(IoWithinStackLimits(High + 1, 0), 0UL);
    ok_eq_ulong(IoWithinStackLimits(Address, (SIZE_T)-1), 1UL);
    ok_eq_ulong(IoWithinStackLimits(0, 0), 0UL);

    Pool = ExAllocatePoolWithTag(NonPagedPool, 64, 'tSoI');
    ok(Pool != NULL, "No pool\n");
    if (Pool != NULL)
    {
        ok_eq_ulong(IoWithinStackLimits((ULONG_PTR)Pool, 64), 0UL);
        ExFreePoolWithTag(Pool, 'tSoI');
    }
}
