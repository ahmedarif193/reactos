/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-Mode Test Suite for the user physical pages (AWE) syscalls
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */

#include <kmt_test.h>
#include <ndk/setypes.h>

#define PFN_COUNT 4

/* All four services are stubs in ntoskrnl/mm/ARM3/procsup.c.  These checks
 * pin the current contract: when Mm gains an AWE implementation they fail,
 * the semantic tests below take over, and the WoW64 PFN thunks in
 * dll/win32/wow64/rossyscall.c must be re-reviewed against the new kernel
 * behavior. */
static
BOOLEAN
TestNotImplementedContract(VOID)
{
    NTSTATUS Status;
    ULONG_PTR PageCount;
    ULONG_PTR Pfns[PFN_COUNT];

    PageCount = 1;
    RtlZeroMemory(Pfns, sizeof(Pfns));
    Status = NtAllocateUserPhysicalPages(NtCurrentProcess(), &PageCount, Pfns);
    if (Status != STATUS_NOT_IMPLEMENTED)
        return FALSE;

    ok_eq_hex(Status, STATUS_NOT_IMPLEMENTED);
    Status = NtMapUserPhysicalPages(NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_NOT_IMPLEMENTED);
    Status = NtMapUserPhysicalPagesScatter(NULL, 0, NULL);
    ok_eq_hex(Status, STATUS_NOT_IMPLEMENTED);
    PageCount = 0;
    Status = NtFreeUserPhysicalPages(NtCurrentProcess(), &PageCount, Pfns);
    ok_eq_hex(Status, STATUS_NOT_IMPLEMENTED);
    return TRUE;
}

static
VOID
TestAweSemantics(VOID)
{
    NTSTATUS Status;
    ULONG_PTR PageCount;
    ULONG_PTR FreeCount;
    ULONG_PTR Pfns[PFN_COUNT];
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
            Status = NtMapUserPhysicalPagesScatter(VaArray, PageCount, NULL);
            ok_eq_hex(Status, STATUS_SUCCESS);
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
    FreeCount = PageCount;
    Status = NtFreeUserPhysicalPages(NtCurrentProcess(), &FreeCount, Pfns);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(FreeCount == PageCount, "Freed %lu of %lu pages\n", (ULONG)FreeCount, (ULONG)PageCount);

    RtlAdjustPrivilege(SE_LOCK_MEMORY_PRIVILEGE, OriginalPrivilegeState, FALSE, &WasEnabled);
}

START_TEST(NtUserPhysicalPages)
{
    if (TestNotImplementedContract())
        return;

    trace("User physical pages syscalls are implemented, testing AWE semantics\n");
    TestAweSemantics();
}

/* EOF */
