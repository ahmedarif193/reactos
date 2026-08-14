/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite reserved mapping address API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'rMmK'

START_TEST(MmMapReserve)
{
    PHYSICAL_ADDRESS LowAddress, HighAddress, SkipBytes;
    PVOID Reservation;
    PMDL Mdl;
    PVOID Mapped1, Mapped2;
    PUCHAR p;

    Reservation = MmAllocateMappingAddress(PAGE_SIZE * 2, TAG_TEST);
    ok(Reservation != NULL, "MmAllocateMappingAddress failed\n");
    if (Reservation == NULL) return;

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = MAXLONGLONG;
    SkipBytes.QuadPart = 0;
    Mdl = MmAllocatePagesForMdlEx(LowAddress, HighAddress, SkipBytes, PAGE_SIZE, MmCached, 0);
    ok(Mdl != NULL, "MmAllocatePagesForMdlEx failed\n");
    if (Mdl == NULL)
    {
        MmFreeMappingAddress(Reservation, TAG_TEST);
        return;
    }

    ok_eq_ulong(MmGetMdlByteCount(Mdl), PAGE_SIZE);

    Mapped1 = MmMapLockedPagesWithReservedMapping(Reservation, TAG_TEST, Mdl, MmCached);
    ok(Mapped1 != NULL, "first reserved map failed\n");
    if (Mapped1 != NULL)
    {
        ok_eq_pointer(Mapped1, Reservation);
        RtlFillMemory(Mapped1, PAGE_SIZE, 0x5E);
        p = Mapped1;
        ok(p[0] == 0x5E, "reserved mapping readback failed: %02x\n", p[0]);
        p[0] = 0xA5;
        MmUnmapReservedMapping(Mapped1, TAG_TEST, Mdl);
    }

    Mapped2 = MmMapLockedPagesWithReservedMapping(Reservation, TAG_TEST, Mdl, MmCached);
    ok(Mapped2 != NULL, "reuse reserved map failed\n");
    if (Mapped2 != NULL)
    {
        ok_eq_pointer(Mapped2, Reservation);
        if (Mapped1 != NULL)
        {
            p = Mapped2;
            ok(p[0] == 0xA5, "reserved mapping contents not preserved: %02x\n", p[0]);
        }
        MmUnmapReservedMapping(Mapped2, TAG_TEST, Mdl);
    }

    MmFreePagesFromMdl(Mdl);
    ExFreePool(Mdl);
    MmFreeMappingAddress(Reservation, TAG_TEST);
}
