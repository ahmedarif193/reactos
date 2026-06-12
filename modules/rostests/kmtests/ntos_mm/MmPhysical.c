/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite physical memory query API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'hPmK'

static
VOID
TestPhysicalAddress(VOID)
{
    PVOID Buffer;
    PHYSICAL_ADDRESS Phys;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_TEST);
    ok(Buffer != NULL, "no pool\n");
    if (Buffer == NULL) return;

    RtlFillMemory(Buffer, PAGE_SIZE, 0x77);

    Phys = MmGetPhysicalAddress(Buffer);
    ok(Phys.QuadPart != 0, "physical address is zero\n");
    ok((Phys.QuadPart & (PAGE_SIZE - 1)) == ((ULONG_PTR)Buffer & (PAGE_SIZE - 1)), "page offset mismatch\n");

    ExFreePoolWithTag(Buffer, TAG_TEST);
}

static
VOID
TestAddressValidity(VOID)
{
    PVOID Buffer;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, 64, TAG_TEST);
    ok(Buffer != NULL, "no pool\n");
    if (Buffer != NULL)
    {
        ok_bool_true(MmIsAddressValid(Buffer), "nonpaged pool valid");
        ExFreePoolWithTag(Buffer, TAG_TEST);
    }

    ok_bool_false(MmIsAddressValid(NULL), "NULL valid");
    ok_bool_false(MmIsAddressValid((PVOID)(ULONG_PTR)0x10), "page zero valid");
}

static
VOID
TestMdlForNonPagedPool(VOID)
{
    PVOID Buffer;
    PMDL Mdl;
    PVOID SystemVa;
    SIZE_T MdlSize;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, 3 * PAGE_SIZE, TAG_TEST);
    ok(Buffer != NULL, "no pool\n");
    if (Buffer == NULL) return;

    MdlSize = MmSizeOfMdl(Buffer, 3 * PAGE_SIZE);
    ok(MdlSize >= sizeof(MDL) + 3 * sizeof(PFN_NUMBER), "MmSizeOfMdl %Iu too small\n", MdlSize);

    Mdl = IoAllocateMdl(Buffer, 3 * PAGE_SIZE, FALSE, FALSE, NULL);
    ok(Mdl != NULL, "IoAllocateMdl failed\n");
    if (Mdl != NULL)
    {
        MmBuildMdlForNonPagedPool(Mdl);
        ok(Mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL, "MDL flags %x\n", Mdl->MdlFlags);

        SystemVa = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        ok_eq_pointer(SystemVa, Buffer);

        ok_eq_ulong(MmGetMdlByteCount(Mdl), 3UL * PAGE_SIZE);
        ok_eq_pointer(MmGetMdlVirtualAddress(Mdl), Buffer);

        IoFreeMdl(Mdl);
    }

    ExFreePoolWithTag(Buffer, TAG_TEST);
}

static
VOID
TestAllocatePagesForMdl(VOID)
{
    PHYSICAL_ADDRESS Low, High, Skip;
    PMDL Mdl;
    PVOID Mapped;

    Low.QuadPart = 0;
    High.QuadPart = -1;
    Skip.QuadPart = 0;

    Mdl = MmAllocatePagesForMdl(Low, High, Skip, 2 * PAGE_SIZE);
    ok(Mdl != NULL, "MmAllocatePagesForMdl failed\n");
    if (Mdl == NULL) return;

    ok(MmGetMdlByteCount(Mdl) <= 2 * PAGE_SIZE, "byte count %lu\n", MmGetMdlByteCount(Mdl));
    ok(MmGetMdlByteCount(Mdl) > 0, "byte count zero\n");

    Mapped = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmCached, NULL, FALSE, NormalPagePriority);
    ok(Mapped != NULL, "map failed\n");
    if (Mapped != NULL)
    {
        RtlFillMemory(Mapped, MmGetMdlByteCount(Mdl), 0x3C);
        ok(*(PUCHAR)Mapped == 0x3C, "readback failed\n");
        MmUnmapLockedPages(Mapped, Mdl);
    }

    MmFreePagesFromMdl(Mdl);
    ExFreePool(Mdl);
}

START_TEST(MmPhysical)
{
    TestPhysicalAddress();
    TestAddressValidity();
    TestMdlForNonPagedPool();
    TestAllocatePagesForMdl();
}
