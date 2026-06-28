/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-Mode Test Suite for Win11-parity Mm behavior (kmwin11new)
 *
 * Memory-manager parity checks. Each contract is observable from a kernel-mode
 * driver (MDL flags, alignment, round-trip identity), so the same binary can be
 * run against the Win11 ARM64 reference kernel (ground truth) and ReactOS.
 */

#include <kmt_test.h>

#define TAG_MMW 'WmmK'

/*
 * MmProbeAndLockPages / MmUnlockPages: a freshly built MDL is unlocked; after
 * probing-and-locking a resident kernel buffer the MDL is marked locked and its
 * PFN array is populated; unlocking clears the locked flag again.
 */
static
VOID
TestProbeAndLockPages(VOID)
{
    PVOID Buffer;
    PMDL Mdl;
    PPFN_NUMBER Pfns;
    BOOLEAN Locked = FALSE;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_MMW);
    ok(Buffer != NULL, "ExAllocatePoolWithTag failed\n");
    if (skip(Buffer != NULL, "No buffer\n"))
        return;

    Mdl = IoAllocateMdl(Buffer, PAGE_SIZE, FALSE, FALSE, NULL);
    ok(Mdl != NULL, "IoAllocateMdl failed\n");
    if (skip(Mdl != NULL, "No MDL\n"))
    {
        ExFreePoolWithTag(Buffer, TAG_MMW);
        return;
    }

    ok((Mdl->MdlFlags & MDL_PAGES_LOCKED) == 0,
       "Fresh MDL already locked: %lx\n", Mdl->MdlFlags);

    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, KernelMode, IoModifyAccess);
        Locked = TRUE;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ok(0, "MmProbeAndLockPages raised 0x%08lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;

    if (Locked)
    {
        ok((Mdl->MdlFlags & MDL_PAGES_LOCKED) != 0,
           "MDL not locked after probe-and-lock: %lx\n", Mdl->MdlFlags);

        Pfns = MmGetMdlPfnArray(Mdl);
        ok(Pfns[0] != 0 && Pfns[0] != (PFN_NUMBER)-1,
           "MDL PFN[0] = 0x%I64x\n", (ULONGLONG)Pfns[0]);

        MmUnlockPages(Mdl);
        ok((Mdl->MdlFlags & MDL_PAGES_LOCKED) == 0,
           "MDL still locked after unlock: %lx\n", Mdl->MdlFlags);
    }

    IoFreeMdl(Mdl);
    ExFreePoolWithTag(Buffer, TAG_MMW);
}

/*
 * MmAllocateNonCachedMemory / MmFreeNonCachedMemory: returns a page-aligned,
 * usable, non-NULL allocation for a multi-page request, and frees it cleanly.
 */
static
VOID
TestNonCachedMemory(VOID)
{
    PVOID Memory;
    SIZE_T Size = 2 * PAGE_SIZE;

    Memory = MmAllocateNonCachedMemory(Size);
    ok(Memory != NULL, "MmAllocateNonCachedMemory failed\n");
    if (!skip(Memory != NULL, "No memory\n"))
    {
        ok(((ULONG_PTR)Memory % PAGE_SIZE) == 0,
           "Non-cached allocation %p is not page-aligned\n", Memory);

        /* It is real, writable memory */
        RtlFillMemory(Memory, Size, 0xAB);
        ok(*(volatile UCHAR *)Memory == 0xAB, "Non-cached memory not writable\n");

        MmFreeNonCachedMemory(Memory, Size);
    }
}

/*
 * MmGetPhysicalAddress / MmGetVirtualForPhysical: for a resident kernel buffer
 * the physical address is non-zero and translating it back yields the same VA.
 */
static
VOID
TestPhysicalRoundTrip(VOID)
{
    PVOID Buffer;
    PHYSICAL_ADDRESS Physical;
    PVOID Virtual;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_MMW);
    ok(Buffer != NULL, "ExAllocatePoolWithTag failed\n");
    if (skip(Buffer != NULL, "No buffer\n"))
        return;

    Physical = MmGetPhysicalAddress(Buffer);
    ok(Physical.QuadPart != 0, "MmGetPhysicalAddress returned 0\n");

    Virtual = MmGetVirtualForPhysical(Physical);
    ok_eq_pointer(Virtual, Buffer);

    ExFreePoolWithTag(Buffer, TAG_MMW);
}

/*
 * MmAllocateContiguousMemory: returns a non-NULL, physically contiguous block
 * (consecutive virtual pages map to consecutive physical pages).
 */
static
VOID
TestContiguousMemory(VOID)
{
    PHYSICAL_ADDRESS Highest;
    PVOID Memory;
    PHYSICAL_ADDRESS Phys0, Phys1;

    Highest.QuadPart = (LONGLONG)-1;
    Memory = MmAllocateContiguousMemory(2 * PAGE_SIZE, Highest);
    ok(Memory != NULL, "MmAllocateContiguousMemory failed\n");
    if (!skip(Memory != NULL, "No memory\n"))
    {
        Phys0 = MmGetPhysicalAddress(Memory);
        Phys1 = MmGetPhysicalAddress((PUCHAR)Memory + PAGE_SIZE);
        ok(Phys0.QuadPart != 0, "Contiguous phys0 = 0\n");
        ok(Phys1.QuadPart == Phys0.QuadPart + PAGE_SIZE,
           "Not contiguous: phys0=0x%I64x phys1=0x%I64x\n",
           Phys0.QuadPart, Phys1.QuadPart);
        MmFreeContiguousMemory(Memory);
    }
}

/*
 * MmAllocatePagesForMdlEx (Win8+ extended MDL allocation): returns a usable MDL
 * with at most the requested byte count and a populated PFN array.
 */
static
VOID
TestAllocatePagesForMdlEx(VOID)
{
    PHYSICAL_ADDRESS Low, High, Skip;
    PMDL Mdl;
    PPFN_NUMBER Pfns;

    Low.QuadPart = 0;
    High.QuadPart = (LONGLONG)-1;
    Skip.QuadPart = 0;

    Mdl = MmAllocatePagesForMdlEx(Low, High, Skip, 2 * PAGE_SIZE, MmCached, 0);
    ok(Mdl != NULL, "MmAllocatePagesForMdlEx failed\n");
    if (!skip(Mdl != NULL, "No MDL\n"))
    {
        ok(MmGetMdlByteCount(Mdl) <= 2 * PAGE_SIZE,
           "Byte count too large: %lu\n", MmGetMdlByteCount(Mdl));
        Pfns = MmGetMdlPfnArray(Mdl);
        ok(Pfns[0] != 0 && Pfns[0] != (PFN_NUMBER)-1,
           "MDL PFN[0] = 0x%I64x\n", (ULONGLONG)Pfns[0]);
        MmFreePagesFromMdl(Mdl);
        ExFreePool(Mdl);
    }
}

START_TEST(MmWin11KM)
{
    TestProbeAndLockPages();
    TestNonCachedMemory();
    TestPhysicalRoundTrip();
    TestContiguousMemory();
    TestAllocatePagesForMdlEx();
}
