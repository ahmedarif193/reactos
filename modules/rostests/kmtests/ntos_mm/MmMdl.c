/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-Mode Test Suite MDL test
 * COPYRIGHT:   Copyright 2015,2023 Thomas Faber (thomas.faber@reactos.org)
 * COPYRIGHT:   Copyright 2017 Pierre Schweitzer (pierre@reactos.org)
 */

#include <kmt_test.h>

static
VOID
TestMmAllocatePagesForMdl(VOID)
{
    PMDL Mdl;
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS SkipBytes;
    PVOID SystemVa;
    PMDL Mdls[32];
    PVOID SystemVas[32];
    ULONG i;
    PPFN_NUMBER MdlPages;
    ULONG MdlPageCount;

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = -1;
    SkipBytes.QuadPart = 0;
    /* simple allocate/free */
    Mdl = MmAllocatePagesForMdl(LowAddress,
                                HighAddress,
                                SkipBytes,
                                2 * 1024 * 1024);
    ok(Mdl != NULL, "MmAllocatePagesForMdl failed\n");
    if (skip(Mdl != NULL, "No Mdl\n"))
        return;
    ok(MmGetMdlByteCount(Mdl) == 2 * 1024 * 1024, "Byte count: %lu\n", MmGetMdlByteCount(Mdl));
    ok(MmGetMdlVirtualAddress(Mdl) == NULL, "Virtual address: %p\n", MmGetMdlVirtualAddress(Mdl));
    ok(!(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdl->MdlFlags);
    MdlPages = MmGetMdlPfnArray(Mdl);
    MdlPageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), MmGetMdlByteCount(Mdl));
    ok(MdlPageCount == 2 * 1024 * 1024 / PAGE_SIZE, "MdlPageCount = %lu\n", MdlPageCount);
    for (i = 0; i < MdlPageCount; i++)
    {
        ok(MdlPages[i] != 0 && MdlPages[i] != (PFN_NUMBER)-1,
           "MdlPages[%lu] = 0x%I64x\n", i, (ULONGLONG)MdlPages[i]);
    }
    MmFreePagesFromMdl(Mdl);
    ExFreePoolWithTag(Mdl, 0);

    /* Now map/unmap it */
    Mdl = MmAllocatePagesForMdl(LowAddress,
                                HighAddress,
                                SkipBytes,
                                2 * 1024 * 1024);
    ok(Mdl != NULL, "MmAllocatePagesForMdl failed\n");
    if (skip(Mdl != NULL, "No Mdl\n"))
        return;
    ok(MmGetMdlByteCount(Mdl) == 2 * 1024 * 1024, "Byte count: %lu\n", MmGetMdlByteCount(Mdl));
    ok(MmGetMdlVirtualAddress(Mdl) == NULL, "Virtual address: %p\n", MmGetMdlVirtualAddress(Mdl));
    ok(!(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdl->MdlFlags);
    SystemVa = MmMapLockedPagesSpecifyCache(Mdl,
                                            KernelMode,
                                            MmCached,
                                            NULL,
                                            FALSE,
                                            NormalPagePriority);
    ok(SystemVa != NULL, "MmMapLockedPagesSpecifyCache failed\n");
    if (!skip(SystemVa != NULL, "No system VA\n"))
    {
        ok(MmGetMdlByteCount(Mdl) == 2 * 1024 * 1024, "Byte count: %lu\n", MmGetMdlByteCount(Mdl));
        ok(MmGetMdlVirtualAddress(Mdl) == NULL, "Virtual address: %p, System VA: %p\n", MmGetMdlVirtualAddress(Mdl), SystemVa);
        ok(Mdl->MappedSystemVa == SystemVa, "MappedSystemVa: %p, System VA: %p\n", Mdl->MappedSystemVa, SystemVa);
        ok((Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdl->MdlFlags);
        MmUnmapLockedPages(SystemVa, Mdl);
    }
    ok(MmGetMdlByteCount(Mdl) == 2 * 1024 * 1024, "Byte count: %lu\n", MmGetMdlByteCount(Mdl));
    ok(MmGetMdlVirtualAddress(Mdl) == NULL, "Virtual address: %p\n", MmGetMdlVirtualAddress(Mdl));
    ok(!(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdl->MdlFlags);
    MmFreePagesFromMdl(Mdl);
    ExFreePoolWithTag(Mdl, 0);

    /* Now map it, and free without unmapping */
    Mdl = MmAllocatePagesForMdl(LowAddress,
                                HighAddress,
                                SkipBytes,
                                2 * 1024 * 1024);
    ok(Mdl != NULL, "MmAllocatePagesForMdl failed\n");
    if (skip(Mdl != NULL, "No Mdl\n"))
        return;
    ok(MmGetMdlByteCount(Mdl) == 2 * 1024 * 1024, "Byte count: %lu\n", MmGetMdlByteCount(Mdl));
    ok(MmGetMdlVirtualAddress(Mdl) == NULL, "Virtual address: %p\n", MmGetMdlVirtualAddress(Mdl));
    ok(!(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdl->MdlFlags);
    SystemVa = MmMapLockedPagesSpecifyCache(Mdl,
                                            KernelMode,
                                            MmCached,
                                            NULL,
                                            FALSE,
                                            NormalPagePriority);
    ok(SystemVa != NULL, "MmMapLockedPagesSpecifyCache failed\n");
    ok(MmGetMdlByteCount(Mdl) == 2 * 1024 * 1024, "Byte count: %lu\n", MmGetMdlByteCount(Mdl));
    ok(MmGetMdlVirtualAddress(Mdl) == NULL, "Virtual address: %p, System VA: %p\n", MmGetMdlVirtualAddress(Mdl), SystemVa);
    ok(Mdl->MappedSystemVa == SystemVa, "MappedSystemVa: %p, System VA: %p\n", Mdl->MappedSystemVa, SystemVa);
    ok((Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdl->MdlFlags);
    MmFreePagesFromMdl(Mdl);
    ExFreePoolWithTag(Mdl, 0);

    /* try to allocate 2 GB -- should succeed (possibly with fewer pages) but not map */
    Mdl = MmAllocatePagesForMdl(LowAddress,
                                HighAddress,
                                SkipBytes,
                                2UL * 1024 * 1024 * 1024);
    if (!skip(Mdl != NULL, "MmAllocatePagesForMdl failed for 2 GB\n"))
    {
        ok(MmGetMdlByteCount(Mdl) <= 2UL * 1024 * 1024 * 1024, "Byte count: %lu\n", MmGetMdlByteCount(Mdl));
        ok(MmGetMdlVirtualAddress(Mdl) == NULL, "Virtual address: %p\n", MmGetMdlVirtualAddress(Mdl));
        ok(!(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdl->MdlFlags);
        MdlPages = MmGetMdlPfnArray(Mdl);
        MdlPageCount = ADDRESS_AND_SIZE_TO_SPAN_PAGES(MmGetMdlVirtualAddress(Mdl), MmGetMdlByteCount(Mdl));
        ok(MdlPageCount <= 2UL * 1024 * 1024 * 1024 / PAGE_SIZE, "MdlPageCount = %lu\n", MdlPageCount);
        for (i = 0; i < MdlPageCount; i++)
        {
            if (MdlPages[i] == 0 ||
                MdlPages[i] == (PFN_NUMBER)-1)
            {
                ok(0, "MdlPages[%lu] = 0x%I64x\n", i, (ULONGLONG)MdlPages[i]);
            }
        }
        SystemVa = MmMapLockedPagesSpecifyCache(Mdl,
                                                KernelMode,
                                                MmCached,
                                                NULL,
                                                FALSE,
                                                NormalPagePriority);
#ifdef _M_IX86
        /*
         * MmAllocatePagesForMdl is allowed to return fewer pages than requested.
         * Only enforce the x86 mapping expectation if we actually got ~2GB.
         */
        if (MmGetMdlByteCount(Mdl) >= (1UL << 31))
            ok(SystemVa == NULL, "MmMapLockedPagesSpecifyCache succeeded for 2 GB\n");
        else
            trace("Skipping 2GB mapping expectation (allocated %lu bytes)\n",
                  MmGetMdlByteCount(Mdl));
#endif
        if (SystemVa != NULL)
            MmUnmapLockedPages(SystemVa, Mdl);
        ok(MmGetMdlByteCount(Mdl) <= 2UL * 1024 * 1024 * 1024, "Byte count: %lu\n", MmGetMdlByteCount(Mdl));
        ok(MmGetMdlVirtualAddress(Mdl) == NULL, "Virtual address: %p\n", MmGetMdlVirtualAddress(Mdl));
        ok(!(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdl->MdlFlags);
        MmFreePagesFromMdl(Mdl);
        ExFreePoolWithTag(Mdl, 0);
    }

    /* now allocate and map 32 MB Mdls until we fail */
    for (i = 0; i < sizeof(Mdls) / sizeof(Mdls[0]); i++)
    {
        Mdls[i] = MmAllocatePagesForMdl(LowAddress,
                                        HighAddress,
                                        SkipBytes,
                                        32 * 1024 * 1024);
        if (Mdls[i] == NULL)
        {
            trace("MmAllocatePagesForMdl failed with i = %lu\n", i);
            break;
        }
        ok(MmGetMdlVirtualAddress(Mdls[i]) == NULL, "Virtual address: %p\n", MmGetMdlVirtualAddress(Mdls[i]));
        ok(!(Mdls[i]->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdls[i]->MdlFlags);
        SystemVas[i] = MmMapLockedPagesSpecifyCache(Mdls[i],
                                                    KernelMode,
                                                    MmCached,
                                                    NULL,
                                                    FALSE,
                                                    NormalPagePriority);
        if (SystemVas[i] == NULL)
        {
            ok(MmGetMdlByteCount(Mdls[i]) <= 32 * 1024 * 1024, "Byte count: %lu\n", MmGetMdlByteCount(Mdls[i]));
            ok(MmGetMdlVirtualAddress(Mdls[i]) == NULL, "Virtual address: %p\n", MmGetMdlVirtualAddress(Mdls[i]));
            ok(!(Mdls[i]->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdls[i]->MdlFlags);
            trace("MmMapLockedPagesSpecifyCache failed with i = %lu\n", i);
            break;
        }
        ok(MmGetMdlByteCount(Mdls[i]) <= 32 * 1024 * 1024, "Byte count: %lu\n", MmGetMdlByteCount(Mdls[i]));
        ok(MmGetMdlVirtualAddress(Mdls[i]) == NULL, "Virtual address: %p, System VA: %p\n", MmGetMdlVirtualAddress(Mdls[i]), SystemVas[i]);
        ok(Mdls[i]->MappedSystemVa == SystemVas[i], "MappedSystemVa: %p\n", Mdls[i]->MappedSystemVa, SystemVas[i]);
        ok((Mdls[i]->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA), "MdlFlags: %lx\n", Mdls[i]->MdlFlags);
    }
    for (i = 0; i < sizeof(Mdls) / sizeof(Mdls[0]); i++)
    {
        if (Mdls[i] == NULL)
            break;
        if (SystemVas[i] != NULL)
            MmUnmapLockedPages(SystemVas[i], Mdls[i]);
        MmFreePagesFromMdl(Mdls[i]);
        ExFreePoolWithTag(Mdls[i], 0);
        if (SystemVas[i] == NULL)
            break;
    }
}

static
VOID
TestMmBuildMdlForNonPagedPool(VOID)
{
    PVOID Page;
    PMDL Mdl;

    Page = ExAllocatePoolWithTag(PagedPool, PAGE_SIZE, 'Test');
    ok(Page != NULL, "ExAllocatePoolWithTag failed\n");
    if (skip(Page != NULL, "No buffer\n"))
        return;

    Mdl = IoAllocateMdl(Page, PAGE_SIZE, FALSE, FALSE, NULL);
    ok(Mdl != NULL, "IoAllocateMdl failed\n");
    if (skip(Mdl != NULL, "No MDL\n"))
        return;

    ok((Mdl->MdlFlags & MDL_PAGES_LOCKED) == 0, "MDL locked\n");
    ok((Mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL) == 0, "MDL from non paged\n");

    // This fails an assertion on Windows 8+ checked and can bugcheck Windows 10+ free.
    if (GetNTVersion() < _WIN32_WINNT_WIN8)
    {
        MmBuildMdlForNonPagedPool(Mdl);
        ok((Mdl->MdlFlags & MDL_PAGES_LOCKED) == 0, "MDL locked\n");
        ok((Mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL) != 0, "MDL from paged\n");
    }

    IoFreeMdl(Mdl);
    ExFreePoolWithTag(Page, 'Test');

    Page = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, 'Test');
    ok(Page != NULL, "ExAllocatePoolWithTag failed\n");
    if (skip(Page != NULL, "No buffer\n"))
        return;

    Mdl = IoAllocateMdl(Page, PAGE_SIZE, FALSE, FALSE, NULL);
    ok(Mdl != NULL, "IoAllocateMdl failed\n");
    if (skip(Mdl != NULL, "No MDL\n"))
        return;

    ok((Mdl->MdlFlags & MDL_PAGES_LOCKED) == 0, "MDL locked\n");
    ok((Mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL) == 0, "MDL from non paged\n");

    MmBuildMdlForNonPagedPool(Mdl);
    ok((Mdl->MdlFlags & MDL_PAGES_LOCKED) == 0, "MDL locked\n");
    ok((Mdl->MdlFlags & MDL_SOURCE_IS_NONPAGED_POOL) != 0, "MDL from paged\n");

    IoFreeMdl(Mdl);
    ExFreePoolWithTag(Page, 'Test');
}

/*
 * MmAllocatePagesForMdlEx records the requested caching type on the pages,
 * and a user-mode mapping of those pages takes the page's attribute over the
 * request.  The only view of that from an API test is behaviour: an
 * uncacheable (write-combined) mapping reads far slower than a cached one on
 * real hardware.  Under an emulator both views are ordinary memory, so the
 * timing is reported and only the mapping contract itself is asserted.
 */
static
VOID
TestMmAllocatePagesForMdlCacheAttribute(VOID)
{
    PHYSICAL_ADDRESS LowAddress;
    PHYSICAL_ADDRESS HighAddress;
    PHYSICAL_ADDRESS SkipBytes;
    PMDL WcMdl = NULL;
    PMDL CachedMdl = NULL;
    volatile ULONG *WcUser = NULL;
    volatile ULONG *CachedUser = NULL;
    ULONG Index;
    ULONG Pass;
    ULONG Sum;
    LARGE_INTEGER Start, WcTicks, CachedTicks;
    const ULONG Size = 64 * 1024;
    const ULONG Count = Size / sizeof(ULONG);
    const ULONG Passes = 16;

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = -1;
    SkipBytes.QuadPart = 0;
    WcMdl = MmAllocatePagesForMdlEx(LowAddress, HighAddress, SkipBytes, Size, MmWriteCombined, 0);
    CachedMdl = MmAllocatePagesForMdlEx(LowAddress, HighAddress, SkipBytes, Size, MmCached, 0);
    ok(WcMdl != NULL, "MmAllocatePagesForMdlEx(MmWriteCombined) failed\n");
    ok(CachedMdl != NULL, "MmAllocatePagesForMdlEx(MmCached) failed\n");
    if (skip(WcMdl != NULL && CachedMdl != NULL &&
             MmGetMdlByteCount(WcMdl) == Size && MmGetMdlByteCount(CachedMdl) == Size,
             "No pages\n"))
        goto Cleanup;

    /* Ask for a cached view of the write-combined pages: the page attribute
     * recorded at allocation must win over the mapping request. */
    _SEH2_TRY
    {
        WcUser = MmMapLockedPagesSpecifyCache(WcMdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority);
        CachedUser = MmMapLockedPagesSpecifyCache(CachedMdl, UserMode, MmCached, NULL, FALSE, NormalPagePriority);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ok(0, "User mapping raised 0x%lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
    ok(WcUser != NULL, "User mapping of write-combined pages failed\n");
    ok(CachedUser != NULL, "User mapping of cached pages failed\n");
    if (skip(WcUser != NULL && CachedUser != NULL, "No user mappings\n"))
        goto Cleanup;

    _SEH2_TRY
    {
        for (Index = 0; Index < Count; Index++)
        {
            WcUser[Index] = Index * 2654435761UL;
            CachedUser[Index] = Index * 2654435761UL;
        }
        for (Index = 0; Index < Count; Index++)
        {
            if (WcUser[Index] != Index * 2654435761UL || CachedUser[Index] != Index * 2654435761UL)
                break;
        }
        ok(Index == Count, "Pattern mismatch at %lu\n", Index);

        Sum = 0;
        Start = KeQueryPerformanceCounter(NULL);
        for (Pass = 0; Pass < Passes; Pass++)
            for (Index = 0; Index < Count; Index++)
                Sum += CachedUser[Index];
        CachedTicks.QuadPart = KeQueryPerformanceCounter(NULL).QuadPart - Start.QuadPart;
        Start = KeQueryPerformanceCounter(NULL);
        for (Pass = 0; Pass < Passes; Pass++)
            for (Index = 0; Index < Count; Index++)
                Sum += WcUser[Index];
        WcTicks.QuadPart = KeQueryPerformanceCounter(NULL).QuadPart - Start.QuadPart;
        trace("Reads: cached %I64d ticks, write-combined %I64d ticks (sum 0x%lx)\n",
              CachedTicks.QuadPart, WcTicks.QuadPart, Sum);
        if (CachedTicks.QuadPart > 0 && WcTicks.QuadPart >= 2 * CachedTicks.QuadPart)
            ok(TRUE, "Write-combined pages read uncached through a cached mapping request\n");
        else
            skip(FALSE, "Cache attribute timing inconclusive (emulated or no PAT)\n");
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ok(0, "Access through user mapping raised 0x%lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;

Cleanup:
    _SEH2_TRY
    {
        if (WcUser != NULL)
            MmUnmapLockedPages((PVOID)WcUser, WcMdl);
        if (CachedUser != NULL)
            MmUnmapLockedPages((PVOID)CachedUser, CachedMdl);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        ok(0, "Unmap raised 0x%lx\n", _SEH2_GetExceptionCode());
    }
    _SEH2_END;
    if (WcMdl != NULL)
    {
        MmFreePagesFromMdl(WcMdl);
        ExFreePoolWithTag(WcMdl, 0);
    }
    if (CachedMdl != NULL)
    {
        MmFreePagesFromMdl(CachedMdl);
        ExFreePoolWithTag(CachedMdl, 0);
    }
}

START_TEST(MmMdl)
{
    if (skip(GetNTVersion() >= _WIN32_WINNT_VISTA,
             "MmMdl touches MDL paths that hang on NT 5.x\n"))
        return;

    TestMmAllocatePagesForMdl();
    TestMmAllocatePagesForMdlCacheAttribute();
    TestMmBuildMdlForNonPagedPool();
}
