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
    PVOID Reservation;
    PVOID Buffer;
    PMDL Mdl;
    PVOID Mapped1, Mapped2;
    PUCHAR p;

    Reservation = MmAllocateMappingAddress(PAGE_SIZE * 2, TAG_TEST);
    ok(Reservation != NULL, "MmAllocateMappingAddress failed\n");
    if (Reservation == NULL) return;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_TEST);
    ok(Buffer != NULL, "no pool\n");
    if (Buffer == NULL)
    {
        MmFreeMappingAddress(Reservation, TAG_TEST);
        return;
    }
    RtlFillMemory(Buffer, PAGE_SIZE, 0x5E);

    Mdl = IoAllocateMdl(Buffer, PAGE_SIZE, FALSE, FALSE, NULL);
    ok(Mdl != NULL, "IoAllocateMdl failed\n");
    if (Mdl != NULL)
    {
        MmBuildMdlForNonPagedPool(Mdl);

        Mapped1 = MmMapLockedPagesWithReservedMapping(Reservation, TAG_TEST, Mdl, MmCached);
        ok(Mapped1 != NULL, "first reserved map failed\n");
        if (Mapped1 != NULL)
        {
            p = Mapped1;
            ok(p[0] == 0x5E, "reserved mapping readback failed: %02x\n", p[0]);
            p[0] = 0xA5;
            ok(*(PUCHAR)Buffer == 0xA5, "write-through reserved mapping failed\n");
            MmUnmapReservedMapping(Mapped1, TAG_TEST, Mdl);
        }

        Mapped2 = MmMapLockedPagesWithReservedMapping(Reservation, TAG_TEST, Mdl, MmCached);
        ok(Mapped2 != NULL, "reuse reserved map failed\n");
        if (Mapped2 != NULL)
        {
            ok_eq_pointer(Mapped2, Mapped1);
            MmUnmapReservedMapping(Mapped2, TAG_TEST, Mdl);
        }

        IoFreeMdl(Mdl);
    }

    ExFreePoolWithTag(Buffer, TAG_TEST);
    MmFreeMappingAddress(Reservation, TAG_TEST);
}
