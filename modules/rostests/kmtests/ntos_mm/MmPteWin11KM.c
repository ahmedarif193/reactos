/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-Mode Test Suite for the Win11-parity PTE / virtual-memory
 *              query surface (kmwin11new)
 *
 * Aggressive Win11<->ReactOS parity checks for the page-table / VA-translation
 * APIs:
 *
 *   MmIsAddressValid, MmGetPhysicalAddress, MmGetVirtualForPhysical,
 *   MmAllocateContiguousMemory, MmSecureVirtualMemory / MmUnsecureVirtualMemory,
 *   MmFlushVirtualMemory, MmIsNonPagedSystemAddressValid.
 *
 * Every contract asserted here is observable from a kernel-mode driver, so the
 * SAME binary is meant to run against the Win11 ARM64 reference kernel (ground
 * truth) and against ReactOS; a failing assertion on ReactOS pinpoints a stub
 * or a divergence from the Windows contract. This is common, architecture-
 * neutral test code: nothing here is gated to a single architecture.
 *
 * SAFETY (critical): this suite NEVER dereferences an address it is probing for
 * validity. Positive cases use ONLY buffers we allocated. Negative cases use
 * NULL, a caller-owned page we RESERVE but never COMMIT, or a high canonical
 * kernel address - each of which is only ever PASSED to the (contractually
 * non-faulting) query API, inside SEH, and never read or written. Every
 * allocation is freed. Each test function is self-contained and skip()s on a
 * setup failure.
 *
 * Implementation status on the current ReactOS parity target (for reference):
 *   - MmIsAddressValid                 implemented
 *   - MmGetPhysicalAddress             implemented
 *   - MmGetVirtualForPhysical          implemented (recently added)
 *   - MmAllocateContiguousMemory       implemented
 *   - MmSecureVirtualMemory            STUB (returns the address unchanged)
 *   - MmUnsecureVirtualMemory          STUB (no-op)
 *   - MmFlushVirtualMemory             STUB (and not exported by name -> it is
 *                                      resolved dynamically; the test skips
 *                                      cleanly where it is unavailable)
 *   - MmIsNonPagedSystemAddressValid   thin wrapper over MmIsAddressValid
 */

#include <kmt_test.h>

#define TAG_PTE 'tPmK'

/* MmFlushVirtualMemory is internal on Windows; resolve it by name at runtime. */
typedef NTSTATUS (NTAPI *PMM_FLUSH_VIRTUAL_MEMORY)(
    PEPROCESS Process,
    PVOID *BaseAddress,
    PSIZE_T RegionSize,
    PIO_STATUS_BLOCK IoStatusBlock);

typedef BOOLEAN (NTAPI *PMM_IS_NONPAGED_SYSTEM_ADDRESS_VALID)(
    PVOID VirtualAddress);

/* ------------------------------------------------------------------------- */
/* Safety helpers - all probe calls go through SEH so a divergent kernel that */
/* faults instead of returning FALSE/NULL is reported, not fatal.            */
/* ------------------------------------------------------------------------- */

static
BOOLEAN
PteSafeIsAddressValid(
    _In_ PVOID Address,
    _Out_ PBOOLEAN Faulted)
{
    BOOLEAN Result = FALSE;

    *Faulted = FALSE;
    _SEH2_TRY
    {
        /* MmIsAddressValid never dereferences the target; it walks the tables. */
        Result = MmIsAddressValid(Address);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *Faulted = TRUE;
    }
    _SEH2_END;

    return Result;
}

static
HANDLE
PteSafeSecure(
    _In_ PVOID Address,
    _In_ SIZE_T Size,
    _In_ ULONG Mode,
    _Out_ PBOOLEAN Faulted)
{
    HANDLE Secure = NULL;

    *Faulted = FALSE;
    _SEH2_TRY
    {
        Secure = MmSecureVirtualMemory(Address, Size, Mode);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        *Faulted = TRUE;
    }
    _SEH2_END;

    return Secure;
}

static
BOOLEAN /* returns TRUE if MmUnsecureVirtualMemory raised */
PteUnsecureFaulted(
    _In_opt_ HANDLE Secure)
{
    BOOLEAN Faulted = FALSE;

    if (Secure == NULL)
        return FALSE;

    _SEH2_TRY
    {
        MmUnsecureVirtualMemory(Secure);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Faulted = TRUE;
    }
    _SEH2_END;

    return Faulted;
}

/* ------------------------------------------------------------------------- */
/* MmIsAddressValid - positive cases                                         */
/* ------------------------------------------------------------------------- */

static
VOID
TestIsAddressValidPositive(VOID)
{
    static const SIZE_T Sizes[] = { 1, 64, PAGE_SIZE, 3 * PAGE_SIZE };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(Sizes); i++)
    {
        PVOID Buffer;
        BOOLEAN Faulted;
        BOOLEAN Valid;

        Buffer = ExAllocatePoolWithTag(NonPagedPool, Sizes[i], TAG_PTE);
        ok(Buffer != NULL, "ExAllocatePoolWithTag(%Iu) failed\n", Sizes[i]);
        if (Buffer == NULL)
            continue;

        RtlFillMemory(Buffer, Sizes[i], 0x5A);

        Valid = PteSafeIsAddressValid(Buffer, &Faulted);
        ok_bool_false(Faulted, "MmIsAddressValid(start) faulted");
        ok_bool_true(Valid, "NonPagedPool start valid");

        /* The last byte of the allocation must be valid too. */
        Valid = PteSafeIsAddressValid((PUCHAR)Buffer + Sizes[i] - 1, &Faulted);
        ok_bool_false(Faulted, "MmIsAddressValid(last byte) faulted");
        ok_bool_true(Valid, "NonPagedPool last byte valid");

        ExFreePoolWithTag(Buffer, TAG_PTE);
    }
}

/* ------------------------------------------------------------------------- */
/* MmIsAddressValid - negative cases (never dereferenced)                    */
/* ------------------------------------------------------------------------- */

static
VOID
TestIsAddressValidNegative(VOID)
{
    PVOID Reserved = NULL;
    SIZE_T Size = PAGE_SIZE;
    NTSTATUS Status;
    BOOLEAN Faulted;
    BOOLEAN Valid;

    /* NULL is never valid. */
    Valid = PteSafeIsAddressValid(NULL, &Faulted);
    ok_bool_false(Faulted, "MmIsAddressValid(NULL) faulted");
    ok_bool_false(Valid, "NULL valid");

    /* The page near address zero is never mapped in kernel context. */
    Valid = PteSafeIsAddressValid((PVOID)(ULONG_PTR)0x10, &Faulted);
    ok_bool_false(Faulted, "MmIsAddressValid(0x10) faulted");
    ok_bool_false(Valid, "page-zero address valid");

    /*
     * A high canonical kernel address at the very top of the address space.
     * It is essentially never mapped. We only PASS it to the (non-faulting)
     * query API and NEVER dereference it.
     */
    Valid = PteSafeIsAddressValid((PVOID)(ULONG_PTR)0xFFFFFFFFFFFF0000ULL, &Faulted);
    ok_bool_false(Faulted, "MmIsAddressValid(high kernel) faulted");
    ok_bool_false(Valid, "high unmapped kernel address valid");

    /*
     * A caller-owned page we RESERVE but never COMMIT: it has an address but no
     * backing page, so it is known-unmapped and entirely safe (we never touch
     * it). This is a controlled, freeable negative case.
     */
    Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &Reserved, 0, &Size,
                                     MEM_RESERVE, PAGE_NOACCESS);
    if (!skip(NT_SUCCESS(Status) && Reserved != NULL,
              "Could not reserve probe page: 0x%08lx\n", Status))
    {
        Valid = PteSafeIsAddressValid(Reserved, &Faulted);
        ok_bool_false(Faulted, "MmIsAddressValid(reserved) faulted");
        ok_bool_false(Valid, "reserved-uncommitted page valid");

        Size = 0;
        Status = ZwFreeVirtualMemory(ZwCurrentProcess(), &Reserved, &Size, MEM_RELEASE);
        ok_eq_hex(Status, STATUS_SUCCESS);
    }
}

/* ------------------------------------------------------------------------- */
/* MmGetPhysicalAddress - page-offset preservation                          */
/* ------------------------------------------------------------------------- */

static
VOID
TestPhysicalAddressOffsets(VOID)
{
    static const ULONG Offsets[] = { 0, 1, 0x10, 0x123, 0x555, PAGE_SIZE - 1 };
    PVOID Buffer;
    ULONG i;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_PTE);
    ok(Buffer != NULL, "ExAllocatePoolWithTag failed\n");
    if (skip(Buffer != NULL, "No buffer\n"))
        return;

    ok(((ULONG_PTR)Buffer & (PAGE_SIZE - 1)) == 0,
       "Page-sized NonPagedPool buffer %p not page-aligned\n", Buffer);

    RtlFillMemory(Buffer, PAGE_SIZE, 0x33);

    for (i = 0; i < RTL_NUMBER_OF(Offsets); i++)
    {
        PVOID Va = (PUCHAR)Buffer + Offsets[i];
        PHYSICAL_ADDRESS Phys = MmGetPhysicalAddress(Va);

        ok(Phys.QuadPart != 0, "MmGetPhysicalAddress(%p) == 0\n", Va);
        /* The low PAGE bits of the physical address equal the VA's offset. */
        ok((Phys.QuadPart & (PAGE_SIZE - 1)) == Offsets[i],
           "offset 0x%lx: phys low bits 0x%I64x mismatch\n",
           Offsets[i], Phys.QuadPart & (PAGE_SIZE - 1));
    }

    ExFreePoolWithTag(Buffer, TAG_PTE);
}

/* ------------------------------------------------------------------------- */
/* MmGetPhysicalAddress - distinct VA pages map to distinct physical frames  */
/* ------------------------------------------------------------------------- */

static
VOID
TestPhysicalDistinctPages(VOID)
{
    enum { PAGES = 4 };
    PVOID Buffer;
    PHYSICAL_ADDRESS Phys[PAGES];
    ULONG i, j;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PAGES * PAGE_SIZE, TAG_PTE);
    ok(Buffer != NULL, "ExAllocatePoolWithTag failed\n");
    if (skip(Buffer != NULL, "No buffer\n"))
        return;

    for (i = 0; i < PAGES; i++)
    {
        Phys[i] = MmGetPhysicalAddress((PUCHAR)Buffer + i * PAGE_SIZE);
        ok(Phys[i].QuadPart != 0, "page %lu phys == 0\n", i);
    }

    /* Distinct virtual pages must map to distinct physical page frames. */
    for (i = 0; i < PAGES; i++)
        for (j = i + 1; j < PAGES; j++)
            ok((Phys[i].QuadPart >> PAGE_SHIFT) != (Phys[j].QuadPart >> PAGE_SHIFT),
               "pages %lu and %lu share PFN 0x%I64x\n",
               i, j, Phys[i].QuadPart >> PAGE_SHIFT);

    ExFreePoolWithTag(Buffer, TAG_PTE);
}

/* ------------------------------------------------------------------------- */
/* MmGetVirtualForPhysical - single-page round trip (recently implemented)   */
/* ------------------------------------------------------------------------- */

static
VOID
TestVirtualForPhysicalRoundTrip(VOID)
{
    PVOID Buffer;
    PHYSICAL_ADDRESS Phys;
    PVOID Virtual;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_PTE);
    ok(Buffer != NULL, "ExAllocatePoolWithTag failed\n");
    if (skip(Buffer != NULL, "No buffer\n"))
        return;

    Phys = MmGetPhysicalAddress(Buffer);
    ok(Phys.QuadPart != 0, "MmGetPhysicalAddress == 0\n");

    /* MmGetVirtualForPhysical(MmGetPhysicalAddress(buf)) == buf */
    Virtual = MmGetVirtualForPhysical(Phys);
    ok_eq_pointer(Virtual, Buffer);

    ExFreePoolWithTag(Buffer, TAG_PTE);
}

/* ------------------------------------------------------------------------- */
/* MmGetVirtualForPhysical - multi-page buffer at several in-page offsets     */
/* ------------------------------------------------------------------------- */

static
VOID
TestVirtualForPhysicalMultiPage(VOID)
{
    enum { PAGES = 4 };
    static const ULONG InPageOffsets[] = { 0, 0x10, 0x40, 0xABC, PAGE_SIZE - 1 };
    PVOID Buffer;
    ULONG p, k;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PAGES * PAGE_SIZE, TAG_PTE);
    ok(Buffer != NULL, "ExAllocatePoolWithTag failed\n");
    if (skip(Buffer != NULL, "No buffer\n"))
        return;

    ok(((ULONG_PTR)Buffer & (PAGE_SIZE - 1)) == 0,
       "Multi-page buffer %p not page-aligned\n", Buffer);

    for (p = 0; p < PAGES; p++)
    {
        for (k = 0; k < RTL_NUMBER_OF(InPageOffsets); k++)
        {
            PVOID Va = (PUCHAR)Buffer + p * PAGE_SIZE + InPageOffsets[k];
            PHYSICAL_ADDRESS Phys = MmGetPhysicalAddress(Va);
            PVOID Back;

            ok(Phys.QuadPart != 0,
               "page %lu offset 0x%lx phys == 0\n", p, InPageOffsets[k]);

            /* The reverse translation must reproduce the exact VA + offset. */
            Back = MmGetVirtualForPhysical(Phys);
            ok_eq_pointer(Back, Va);
        }
    }

    ExFreePoolWithTag(Buffer, TAG_PTE);
}

/* ------------------------------------------------------------------------- */
/* MmAllocateContiguousMemory - physically contiguous, round-trippable       */
/* ------------------------------------------------------------------------- */

static
VOID
TestContiguousMemory(VOID)
{
    enum { PAGES = 4 };
    PHYSICAL_ADDRESS Highest;
    PVOID Buffer;
    PHYSICAL_ADDRESS Phys0, Phys;
    BOOLEAN Faulted, Valid;
    ULONG i;

    Highest.QuadPart = (LONGLONG)-1;
    Buffer = MmAllocateContiguousMemory(PAGES * PAGE_SIZE, Highest);
    ok(Buffer != NULL, "MmAllocateContiguousMemory failed\n");
    if (skip(Buffer != NULL, "No memory\n"))
        return;

    ok(((ULONG_PTR)Buffer & (PAGE_SIZE - 1)) == 0,
       "Contiguous buffer %p not page-aligned\n", Buffer);

    RtlFillMemory(Buffer, PAGES * PAGE_SIZE, 0xC7);

    Phys0 = MmGetPhysicalAddress(Buffer);
    ok(Phys0.QuadPart != 0, "Contiguous phys0 == 0\n");

    Valid = PteSafeIsAddressValid(Buffer, &Faulted);
    ok_bool_false(Faulted, "MmIsAddressValid(contiguous) faulted");
    ok_bool_true(Valid, "contiguous buffer valid");

    /* Consecutive VA pages must map to consecutive physical pages. */
    for (i = 1; i < PAGES; i++)
    {
        Phys = MmGetPhysicalAddress((PUCHAR)Buffer + i * PAGE_SIZE);
        ok(Phys.QuadPart == Phys0.QuadPart + (LONGLONG)i * PAGE_SIZE,
           "not contiguous at page %lu: phys=0x%I64x expected=0x%I64x\n",
           i, Phys.QuadPart, Phys0.QuadPart + (LONGLONG)i * PAGE_SIZE);

        /* And each contiguous page must round-trip back to its VA. */
        ok_eq_pointer(MmGetVirtualForPhysical(Phys), (PUCHAR)Buffer + i * PAGE_SIZE);
    }

    MmFreeContiguousMemory(Buffer);
}

/* ------------------------------------------------------------------------- */
/* MmSecureVirtualMemory / MmUnsecureVirtualMemory - handle + no-fault        */
/* ------------------------------------------------------------------------- */

static
VOID
TestSecureVirtualMemory(VOID)
{
    PVOID Base = NULL;
    SIZE_T Size = 2 * PAGE_SIZE;
    NTSTATUS Status;
    HANDLE Secure, Secure2;
    BOOLEAN Faulted;

    Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &Base, 0, &Size,
                                     MEM_COMMIT, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status) && Base != NULL, "No user buffer\n"))
        return;

    /* Secure a committed RW range for read/write -> a non-NULL opaque handle. */
    Secure = PteSafeSecure(Base, PAGE_SIZE, PAGE_READWRITE, &Faulted);
    ok_bool_false(Faulted, "MmSecureVirtualMemory(RW) faulted");
    ok(Secure != NULL, "MmSecureVirtualMemory(RW) returned NULL\n");
    /*
     * Diagnostic only (NOT asserted): on Windows the secure handle is an opaque
     * value that is typically NOT the base address. The ReactOS stub returns
     * the address unchanged, so "handle == Base" in the log flags the stub.
     */
    trace("Secure handle %p vs Base %p (equal => ReactOS stub)\n", Secure, Base);
    ok_bool_false(PteUnsecureFaulted(Secure), "MmUnsecureVirtualMemory(RW) faulted");

    /* Secure the same RW range but only for read access. */
    Secure = PteSafeSecure(Base, PAGE_SIZE, PAGE_READONLY, &Faulted);
    ok_bool_false(Faulted, "MmSecureVirtualMemory(RO) faulted");
    ok(Secure != NULL, "MmSecureVirtualMemory(RO on RW) returned NULL\n");
    ok_bool_false(PteUnsecureFaulted(Secure), "MmUnsecureVirtualMemory(RO) faulted");

    /* Two independent secure handles over the full range. */
    Secure = PteSafeSecure(Base, Size, PAGE_READWRITE, &Faulted);
    ok_bool_false(Faulted, "first full-range secure faulted");
    ok(Secure != NULL, "first full-range secure returned NULL\n");
    Secure2 = PteSafeSecure(Base, Size, PAGE_READWRITE, &Faulted);
    ok_bool_false(Faulted, "second full-range secure faulted");
    ok(Secure2 != NULL, "second full-range secure returned NULL\n");
    ok_bool_false(PteUnsecureFaulted(Secure2), "unsecure #2 faulted");
    ok_bool_false(PteUnsecureFaulted(Secure), "unsecure #1 faulted");

    Size = 0;
    Status = ZwFreeVirtualMemory(ZwCurrentProcess(), &Base, &Size, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

/* ------------------------------------------------------------------------- */
/* MmSecureVirtualMemory - securing for RW blocks a protection downgrade      */
/* (Win11 contract; the ReactOS stub does not really secure -> divergence)    */
/* ------------------------------------------------------------------------- */

static
VOID
TestSecureBlocksDowngrade(VOID)
{
    PVOID Base = NULL;
    SIZE_T Size = PAGE_SIZE;
    NTSTATUS Status;
    HANDLE Secure;
    BOOLEAN Faulted;

    Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &Base, 0, &Size,
                                     MEM_COMMIT, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status) && Base != NULL, "No user buffer\n"))
        return;

    Secure = PteSafeSecure(Base, Size, PAGE_READWRITE, &Faulted);
    ok_bool_false(Faulted, "MmSecureVirtualMemory faulted");
    ok(Secure != NULL, "MmSecureVirtualMemory returned NULL\n");

    if (!skip(Secure != NULL, "Region not secured\n"))
    {
        PVOID ProtBase = Base;
        SIZE_T ProtSize = Size;
        ULONG OldProtect = 0;

        /*
         * Changing the protection only touches PTE bits, never the page
         * contents, so this is safe. ZwProtectVirtualMemory to PAGE_NOACCESS
         * must be REJECTED while the range is secured for read/write.
         */
        Status = STATUS_SUCCESS;
        _SEH2_TRY
        {
            Status = ZwProtectVirtualMemory(ZwCurrentProcess(), &ProtBase,
                                            &ProtSize, PAGE_NOACCESS, &OldProtect);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;

        trace("Protect-to-NOACCESS while secured: 0x%08lx (old 0x%lx)\n",
              Status, OldProtect);
        ok(!NT_SUCCESS(Status),
           "Protection downgrade succeeded while secured (0x%08lx) - not secured?\n",
           Status);

        if (NT_SUCCESS(Status))
        {
            /* Stub path: restore RW so the release below is clean. */
            ProtBase = Base;
            ProtSize = Size;
            (VOID)ZwProtectVirtualMemory(ZwCurrentProcess(), &ProtBase, &ProtSize,
                                         PAGE_READWRITE, &OldProtect);
        }

        ok_bool_false(PteUnsecureFaulted(Secure), "MmUnsecureVirtualMemory faulted");
    }

    Size = 0;
    Status = ZwFreeVirtualMemory(ZwCurrentProcess(), &Base, &Size, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

/* ------------------------------------------------------------------------- */
/* MmFlushVirtualMemory - STUB on ReactOS; resolved dynamically, SEH-guarded */
/* ------------------------------------------------------------------------- */

static
VOID
TestFlushVirtualMemory(VOID)
{
    PMM_FLUSH_VIRTUAL_MEMORY pMmFlushVirtualMemory;
    PVOID Base = NULL;
    SIZE_T Size = PAGE_SIZE;
    NTSTATUS Status;
    IO_STATUS_BLOCK IoStatus;
    PVOID FlushBase;
    SIZE_T FlushSize;
    BOOLEAN Faulted = FALSE;

    pMmFlushVirtualMemory = (PMM_FLUSH_VIRTUAL_MEMORY)
        KmtGetSystemRoutineAddress(L"MmFlushVirtualMemory");
    if (skip(pMmFlushVirtualMemory != NULL,
             "MmFlushVirtualMemory is not exported on this kernel\n"))
        return;

    Status = ZwAllocateVirtualMemory(ZwCurrentProcess(), &Base, 0, &Size,
                                     MEM_COMMIT, PAGE_READWRITE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (skip(NT_SUCCESS(Status) && Base != NULL, "No user buffer\n"))
        return;

    RtlFillMemory(Base, PAGE_SIZE, 0x6D);

    FlushBase = Base;
    FlushSize = PAGE_SIZE;
    RtlZeroMemory(&IoStatus, sizeof(IoStatus));
    Status = STATUS_UNSUCCESSFUL;

    _SEH2_TRY
    {
        Status = pMmFlushVirtualMemory(PsGetCurrentProcess(), &FlushBase,
                                       &FlushSize, &IoStatus);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Faulted = TRUE;
    }
    _SEH2_END;

    ok_bool_false(Faulted, "MmFlushVirtualMemory faulted");
    trace("MmFlushVirtualMemory: ret=0x%08lx IoStatus.Status=0x%08lx Info=%Iu\n",
          Status, (ULONG)IoStatus.Status, IoStatus.Information);

    /*
     * Win11 contract: flushing a valid committed range completes successfully.
     * (Private, non-file-backed memory may report STATUS_NOT_MAPPED_DATA, which
     * we treat as success-ish.) The ReactOS implementation is a stub that fills
     * the IO_STATUS_BLOCK with STATUS_NOT_IMPLEMENTED -> divergence, captured by
     * the IoStatus assertion below where the routine is exported.
     */
    ok(NT_SUCCESS(Status) || Status == STATUS_NOT_MAPPED_DATA,
       "MmFlushVirtualMemory return 0x%08lx is not success-ish\n", Status);
    ok(NT_SUCCESS(IoStatus.Status) || IoStatus.Status == STATUS_NOT_MAPPED_DATA,
       "MmFlushVirtualMemory IoStatus 0x%08lx is not success-ish\n",
       (ULONG)IoStatus.Status);

    Size = 0;
    Status = ZwFreeVirtualMemory(ZwCurrentProcess(), &Base, &Size, MEM_RELEASE);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

/* ------------------------------------------------------------------------- */
/* MmIsNonPagedSystemAddressValid - TRUE for a NonPagedPool buffer            */
/* ------------------------------------------------------------------------- */

static
VOID
TestNonPagedSystemAddressValid(VOID)
{
    PMM_IS_NONPAGED_SYSTEM_ADDRESS_VALID pMmIsNonPagedSystemAddressValid;
    PVOID Buffer;
    BOOLEAN Result;
    BOOLEAN Faulted;

    pMmIsNonPagedSystemAddressValid = (PMM_IS_NONPAGED_SYSTEM_ADDRESS_VALID)
        KmtGetSystemRoutineAddress(L"MmIsNonPagedSystemAddressValid");
    if (skip(pMmIsNonPagedSystemAddressValid != NULL,
             "MmIsNonPagedSystemAddressValid is not exported on this kernel\n"))
        return;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_PTE);
    ok(Buffer != NULL, "ExAllocatePoolWithTag failed\n");
    if (skip(Buffer != NULL, "No buffer\n"))
        return;

    RtlFillMemory(Buffer, PAGE_SIZE, 0x42);

    Result = FALSE;
    Faulted = FALSE;
    _SEH2_TRY
    {
        Result = pMmIsNonPagedSystemAddressValid(Buffer);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Faulted = TRUE;
    }
    _SEH2_END;

    ok_bool_false(Faulted, "MmIsNonPagedSystemAddressValid faulted");
    ok_bool_true(Result, "NonPagedPool buffer reported non-paged-valid");

    ExFreePoolWithTag(Buffer, TAG_PTE);
}

/* ------------------------------------------------------------------------- */
/* MmGetPhysicalAddress <-> MmIsAddressValid consistency: PagedPool vs        */
/* NonPagedPool. The invariant "valid <=> non-zero physical" must always hold.*/
/* ------------------------------------------------------------------------- */

static
VOID
TestPhysicalValidConsistency(VOID)
{
    PVOID NonPaged;
    PVOID Paged;
    PHYSICAL_ADDRESS Phys;
    BOOLEAN Valid;
    BOOLEAN Faulted;

    /* NonPagedPool: always resident -> valid AND non-zero physical. */
    NonPaged = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_PTE);
    ok(NonPaged != NULL, "NonPagedPool alloc failed\n");
    if (!skip(NonPaged != NULL, "No nonpaged buffer\n"))
    {
        RtlFillMemory(NonPaged, PAGE_SIZE, 0x11);
        Valid = PteSafeIsAddressValid(NonPaged, &Faulted);
        Phys = MmGetPhysicalAddress(NonPaged);

        ok_bool_false(Faulted, "MmIsAddressValid(nonpaged) faulted");
        ok_bool_true(Valid, "nonpaged valid");
        ok(Phys.QuadPart != 0, "nonpaged phys == 0\n");
        ok((Valid == TRUE) == (Phys.QuadPart != 0),
           "nonpaged inconsistent: valid=%d phys=0x%I64x\n", Valid, Phys.QuadPart);

        ExFreePoolWithTag(NonPaged, TAG_PTE);
    }

    /* PagedPool: resident right after we touch it. */
    Paged = ExAllocatePoolWithTag(PagedPool, PAGE_SIZE, TAG_PTE);
    ok(Paged != NULL, "PagedPool alloc failed\n");
    if (!skip(Paged != NULL, "No paged buffer\n"))
    {
        RtlFillMemory(Paged, PAGE_SIZE, 0x22);   /* fault the page in */
        Valid = PteSafeIsAddressValid(Paged, &Faulted);
        Phys = MmGetPhysicalAddress(Paged);

        ok_bool_false(Faulted, "MmIsAddressValid(paged) faulted");
        /* The two queries must agree regardless of residency. */
        ok((Valid == TRUE) == (Phys.QuadPart != 0),
           "paged inconsistent: valid=%d phys=0x%I64x\n", Valid, Phys.QuadPart);
        trace("PagedPool: valid=%d phys=0x%I64x\n", Valid, Phys.QuadPart);

        ExFreePoolWithTag(Paged, TAG_PTE);
    }
}

START_TEST(MmPteWin11KM)
{
    trace("NT version 0x%04x, build %lu\n",
          GetNTVersion(), (ULONG)SharedUserData->NtBuildNumber);

    TestIsAddressValidPositive();
    TestIsAddressValidNegative();
    TestPhysicalAddressOffsets();
    TestPhysicalDistinctPages();
    TestVirtualForPhysicalRoundTrip();
    TestVirtualForPhysicalMultiPage();
    TestContiguousMemory();
    TestSecureVirtualMemory();
    TestSecureBlocksDowngrade();
    TestFlushVirtualMemory();
    TestNonPagedSystemAddressValid();
    TestPhysicalValidConsistency();
}
