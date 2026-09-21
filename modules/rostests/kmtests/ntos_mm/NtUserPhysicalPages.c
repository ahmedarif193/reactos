/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-Mode Test Suite for the user physical pages (AWE) syscalls
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */

#include <kmt_test.h>
#include <ndk/setypes.h>

#define PFN_COUNT 4

static
VOID
TestAweSemantics(VOID)
{
    NTSTATUS Status;
    ULONG_PTR PageCount;
    ULONG_PTR FreeCount;
    ULONG_PTR Pfns[PFN_COUNT];
    ULONG_PTR Remap[PFN_COUNT];
    PVOID VaArray[PFN_COUNT];
    PVOID Region = NULL;
    SIZE_T RegionSize = PFN_COUNT * PAGE_SIZE;
    BOOLEAN OriginalPrivilegeState;
    BOOLEAN OriginalStateKnown;
    BOOLEAN WasEnabled;
    ULONG_PTR i;

    /* the allocation must be refused without SeLockMemoryPrivilege */
    Status = RtlAdjustPrivilege(SE_LOCK_MEMORY_PRIVILEGE, FALSE, FALSE, &OriginalPrivilegeState);
    OriginalStateKnown = NT_SUCCESS(Status);
    if (!skip(NT_SUCCESS(Status), "Cannot disable lock memory privilege: 0x%lx\n", Status))
    {
        PageCount = 1;
        RtlZeroMemory(Pfns, sizeof(Pfns));
        Status = NtAllocateUserPhysicalPages(NtCurrentProcess(), &PageCount, Pfns);
        ok_eq_hex(Status, STATUS_PRIVILEGE_NOT_HELD);
    }

    Status = RtlAdjustPrivilege(SE_LOCK_MEMORY_PRIVILEGE, TRUE, FALSE, &WasEnabled);
    if (skip(NT_SUCCESS(Status), "SeLockMemoryPrivilege not available: 0x%lx\n", Status))
        return;
    if (!OriginalStateKnown) OriginalPrivilegeState = WasEnabled;

    /* parameters are probed */
    Status = NtAllocateUserPhysicalPages(NtCurrentProcess(), NULL, Pfns);
    ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);
    PageCount = 1;
    Status = NtAllocateUserPhysicalPages(NtCurrentProcess(), &PageCount, (PULONG_PTR)(ULONG_PTR)-16);
    ok_eq_hex(Status, STATUS_ACCESS_VIOLATION);

    /* allocation writes back the granted count and that many PFNs */
    PageCount = PFN_COUNT;
    RtlZeroMemory(Pfns, sizeof(Pfns));
    Status = NtAllocateUserPhysicalPages(NtCurrentProcess(), &PageCount, Pfns);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(PageCount <= PFN_COUNT, "PageCount %lu exceeds request %u\n", (ULONG)PageCount, PFN_COUNT);
    if (skip(NT_SUCCESS(Status) && PageCount != 0, "No physical pages granted\n"))
    {
            RtlAdjustPrivilege(SE_LOCK_MEMORY_PRIVILEGE, OriginalPrivilegeState, FALSE, &WasEnabled);
        return;
    }

    /* mapping requires a MEM_PHYSICAL reservation */
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Region, 0, &RegionSize, MEM_RESERVE | MEM_PHYSICAL, PAGE_READWRITE);
    if (!skip(NT_SUCCESS(Status), "MEM_PHYSICAL reservation failed: 0x%lx\n", Status))
    {
        Status = NtMapUserPhysicalPages(Region, PageCount, Pfns);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            /* the AWE view must be readable and writable */
            RtlFillMemory(Region, PageCount * PAGE_SIZE, 0x55);
            ok_eq_uint(*(PUCHAR)Region, 0x55);

            if (PageCount >= 2)
            {
                *(PULONG)Region = 0x12345678;
                *(PULONG)((PUCHAR)Region + PAGE_SIZE) = 0x9ABCDEF0;
                Remap[0] = Pfns[1];
                Remap[1] = Pfns[0];
                Status = NtMapUserPhysicalPages(Region, 2, Remap);
                ok_eq_hex(Status, STATUS_SUCCESS);
                ok_eq_ulong(*(PULONG)Region, 0x9ABCDEF0);
                ok_eq_ulong(*(PULONG)((PUCHAR)Region + PAGE_SIZE), 0x12345678);
                Remap[1] = (ULONG_PTR)-1;
                Status = NtMapUserPhysicalPages(Region, 2, Remap);
                ok(!NT_SUCCESS(Status), "Foreign PFN accepted\n");
                ok_eq_ulong(*(PULONG)Region, 0x9ABCDEF0);
                ok_eq_ulong(*(PULONG)((PUCHAR)Region + PAGE_SIZE), 0x12345678);
                Remap[1] = Remap[0];
                Status = NtMapUserPhysicalPages(Region, 2, Remap);
                ok(!NT_SUCCESS(Status), "Duplicate PFN accepted\n");
                ok_eq_ulong(*(PULONG)((PUCHAR)Region + PAGE_SIZE), 0x12345678);
            }

            /* unmap by passing no PFN array */
            Status = NtMapUserPhysicalPages(Region, PageCount, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }

        /* scatter variant maps page by page */
        for (i = 0; i < PageCount; i++)
            VaArray[i] = (PUCHAR)Region + i * PAGE_SIZE;
        Status = NtMapUserPhysicalPagesScatter(VaArray, PageCount, Pfns);
        ok_eq_hex(Status, STATUS_SUCCESS);
        if (NT_SUCCESS(Status))
        {
            if (PageCount >= 2)
            {
                RtlCopyMemory(Remap, Pfns, PageCount * sizeof(Pfns[0]));
                Remap[0] = 0;
                Status = NtMapUserPhysicalPagesScatter(VaArray, PageCount, Remap);
                ok_eq_hex(Status, STATUS_SUCCESS);
                KmtStartSeh();
                i = *(volatile UCHAR *)Region;
                KmtEndSeh(STATUS_ACCESS_VIOLATION);
                ok_eq_ulong(*(PULONG)((PUCHAR)Region + PAGE_SIZE), 0x9ABCDEF0);
                Status = NtMapUserPhysicalPagesScatter(VaArray, PageCount, Pfns);
                ok_eq_hex(Status, STATUS_SUCCESS);
            }
            Status = NtMapUserPhysicalPagesScatter(VaArray, PageCount, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
        }

        Status = NtMapUserPhysicalPages(Region, PageCount, Pfns);
        ok_eq_hex(Status, STATUS_SUCCESS);
        FreeCount = 1;
        Status = NtFreeUserPhysicalPages(NtCurrentProcess(), &FreeCount, &Pfns[PageCount - 1]);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_size(FreeCount, 1);
        if (NT_SUCCESS(Status))
        {
            KmtStartSeh();
            i = *((volatile UCHAR *)Region + (PageCount - 1) * PAGE_SIZE);
            KmtEndSeh(STATUS_ACCESS_VIOLATION);
            PageCount--;
        }

        RegionSize = 0;
        Status = NtFreeVirtualMemory(NtCurrentProcess(), &Region, &RegionSize, MEM_RELEASE);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    /* mapping into an ordinary reservation must fail */
    Region = NULL;
    RegionSize = PFN_COUNT * PAGE_SIZE;
    Status = NtAllocateVirtualMemory(NtCurrentProcess(), &Region, 0, &RegionSize, MEM_RESERVE, PAGE_READWRITE);
    if (!skip(NT_SUCCESS(Status), "Plain reservation failed: 0x%lx\n", Status))
    {
        Status = NtMapUserPhysicalPages(Region, PageCount, Pfns);
        ok(!NT_SUCCESS(Status), "Mapping into non-AWE region succeeded\n");
        RegionSize = 0;
        Status = NtFreeVirtualMemory(NtCurrentProcess(), &Region, &RegionSize, MEM_RELEASE);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }

    /* freeing writes back the number of pages actually freed */
    if (PageCount != 0)
    {
        FreeCount = PageCount;
        Status = NtFreeUserPhysicalPages(NtCurrentProcess(), &FreeCount, Pfns);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(FreeCount == PageCount, "Freed %lu of %lu pages\n", (ULONG)FreeCount, (ULONG)PageCount);
    }

    RtlAdjustPrivilege(SE_LOCK_MEMORY_PRIVILEGE, OriginalPrivilegeState, FALSE, &WasEnabled);
}

START_TEST(NtUserPhysicalPages)
{
    TestAweSemantics();
}

/* EOF */
