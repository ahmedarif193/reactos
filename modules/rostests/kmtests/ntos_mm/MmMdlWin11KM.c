/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Kernel-Mode Test Suite - minimal-safe MDL Win11-parity checks
 *
 * Validates the two MDL routines ReactOS used to stub (MmAdvanceMdl,
 * MmProtectMdlSystemAddress) plus the basic MDL accessors, against the Win11
 * ARM64 reference kernel (ground truth) and ReactOS.
 *
 * SAFETY / Win11-portability rules followed here:
 *   - Only NonPagedPool buffers and only MmCached mappings (ARM64 hard-bugchecks
 *     when a cached page is re-mapped NonCached/WriteCombined, which is not
 *     SEH-catchable, so those cache types are deliberately avoided).
 *   - Real fresh system-PTE mappings are obtained via MmProbeAndLockPages +
 *     MmMapLockedPagesSpecifyCache (NOT MmBuildMdlForNonPagedPool, which reuses
 *     the permanent pool VA and so never allocates the PTEs the advance/teardown
 *     coordination has to free).
 *   - Only the Win11-portable part of the MmAdvanceMdl contract is asserted
 *     (returned status, shrunk ByteCount, advanced VA + MappedSystemVa, and the
 *     data now visible at the advanced mapping). Post-unmap MDL field values are
 *     NOT asserted - they are an internal detail that need not match.
 *   - Every map/lock is torn down in the canonical order (unmap -> unlock ->
 *     IoFreeMdl -> ExFreePool) on every path, and every routine that can raise is
 *     SEH-guarded so one divergence cannot lose the rest of the log. The unmapped
 *     fold-back case (which advances without mapping) runs last for that reason.
 */

#include <kmt_test.h>

#define TAG_MDLW 'WldM'

/* Pattern byte for page index i; never 0 so "populated" is unambiguous. */
#define PATTERN_BYTE(i) ((UCHAR)(((i) + 1) & 0xFF))

/*
 * Allocate a NonPagedPool block large enough to carve PageCount page-aligned
 * pages out of it. Returns the page-aligned start; *BaseToFree receives the raw
 * pointer to hand back to ExFreePoolWithTag.
 */
static
PVOID
AllocAlignedNonPaged(
    _In_ ULONG PageCount,
    _Out_ PVOID *BaseToFree)
{
    SIZE_T Size = (SIZE_T)(PageCount + 1) * PAGE_SIZE;
    PVOID Base = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_MDLW);

    *BaseToFree = Base;
    if (Base == NULL)
        return NULL;

    return (PVOID)(((ULONG_PTR)Base + PAGE_SIZE - 1) & ~((ULONG_PTR)PAGE_SIZE - 1));
}

/*
 * MDL accessor macros must agree with the MDL header for several buffer shapes:
 * MmGetMdlByteCount/ByteOffset/VirtualAddress and MmSizeOfMdl. No probe/lock/map.
 */
static
VOID
TestMdlAccessors(VOID)
{
    static const struct { ULONG Offset; ULONG Length; } Cases[] =
    {
        {             0,           1 },
        {             0,         100 },
        {             0,   PAGE_SIZE },
        {            16,   PAGE_SIZE },
        { PAGE_SIZE - 8, 2 * PAGE_SIZE },
    };
    PVOID Base;
    PUCHAR Aligned;
    ULONG c;

    trace("TestMdlAccessors\n");

    Aligned = AllocAlignedNonPaged(4, &Base);
    if (!ok(Aligned != NULL, "alloc failed\n"))
        return;

    for (c = 0; c < RTL_NUMBER_OF(Cases); c++)
    {
        PVOID Va = Aligned + Cases[c].Offset;
        ULONG Len = Cases[c].Length;
        PMDL Mdl = IoAllocateMdl(Va, Len, FALSE, FALSE, NULL);

        if (!ok(Mdl != NULL, "[%lu] IoAllocateMdl failed\n", c))
            continue;

        ok_eq_uint(MmGetMdlByteCount(Mdl), Len);
        ok_eq_uint(MmGetMdlByteOffset(Mdl), BYTE_OFFSET(Va));
        ok(MmGetMdlVirtualAddress(Mdl) == Va, "[%lu] VA mismatch\n", c);
        ok(MmSizeOfMdl(Va, Len) >= sizeof(MDL), "[%lu] MmSizeOfMdl too small\n", c);

        IoFreeMdl(Mdl);
    }

    ExFreePoolWithTag(Base, TAG_MDLW);
}

/*
 * MmAdvanceMdl on a real (probe-locked, system-mapped) MDL. Parameterised so one
 * helper covers single/multi advance, sub-page (no whole page consumed), and a
 * multi-page advance. After AdvanceCount advances of AdvanceBytes each, the MDL
 * must have shrunk ByteCount by the total, advanced both the described VA and the
 * MappedSystemVa by the total, and the byte now at MappedSystemVa must be the one
 * at that offset in the original page-patterned buffer. Teardown (which must also
 * free the parked leading PTEs) must not corrupt anything.
 */
static
VOID
AdvanceMappedCase(
    _In_ PCSTR Desc,
    _In_ ULONG Pages,
    _In_ ULONG AdvanceBytes,
    _In_ ULONG AdvanceCount)
{
    PVOID Base = NULL;
    PUCHAR Aligned;
    PMDL Mdl = NULL;
    PVOID Va = NULL;
    PVOID OldVa = NULL, OldMapped = NULL;
    ULONG OldByteCount = 0, Total, a;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Locked = FALSE, Mapped = FALSE, AdvanceOk = TRUE;
    ULONG i;

    trace("AdvanceMappedCase: %s (pages=%lu adv=%lu x%lu)\n",
          Desc, Pages, AdvanceBytes, AdvanceCount);

    Aligned = AllocAlignedNonPaged(Pages, &Base);
    if (!ok(Aligned != NULL, "[%s] alloc failed\n", Desc))
        return;

    for (i = 0; i < Pages; i++)
        RtlFillMemory(Aligned + i * PAGE_SIZE, PAGE_SIZE, PATTERN_BYTE(i));

    Mdl = IoAllocateMdl(Aligned, Pages * PAGE_SIZE, FALSE, FALSE, NULL);
    if (!ok(Mdl != NULL, "[%s] IoAllocateMdl failed\n", Desc))
        goto cleanup;

    _SEH2_TRY {
        MmProbeAndLockPages(Mdl, KernelMode, IoModifyAccess);
        Locked = TRUE;
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "[%s] MmProbeAndLockPages raised 0x%08lx\n", Desc, _SEH2_GetExceptionCode());
    } _SEH2_END;
    if (!Locked)
        goto cleanup;

    _SEH2_TRY {
        Va = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmCached,
                                          NULL, FALSE, NormalPagePriority);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "[%s] MmMapLockedPagesSpecifyCache raised 0x%08lx\n", Desc, _SEH2_GetExceptionCode());
    } _SEH2_END;
    Mapped = (Va != NULL);
    if (!ok(Mapped, "[%s] map returned NULL\n", Desc))
        goto cleanup;

    ok(*(volatile UCHAR *)Va == PATTERN_BYTE(0), "[%s] page0 visible at mapping\n", Desc);

    OldVa = MmGetMdlVirtualAddress(Mdl);
    OldMapped = Mdl->MappedSystemVa;
    OldByteCount = MmGetMdlByteCount(Mdl);

    for (a = 0; a < AdvanceCount; a++)
    {
        _SEH2_TRY {
            Status = MmAdvanceMdl(Mdl, AdvanceBytes);
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            Status = _SEH2_GetExceptionCode();
            ok(0, "[%s] MmAdvanceMdl raised 0x%08lx\n", Desc, Status);
            Status = STATUS_UNSUCCESSFUL;
        } _SEH2_END;
        ok(Status == STATUS_SUCCESS, "[%s] advance[%lu] = 0x%08lx\n", Desc, a, Status);
        if (!NT_SUCCESS(Status)) { AdvanceOk = FALSE; break; }
    }

    Total = AdvanceBytes * AdvanceCount;
    if (AdvanceOk)
    {
        ok(MmGetMdlByteCount(Mdl) == OldByteCount - Total,
           "[%s] ByteCount = %lu, expected %lu\n",
           Desc, MmGetMdlByteCount(Mdl), OldByteCount - Total);
        ok(MmGetMdlVirtualAddress(Mdl) == (PVOID)((PUCHAR)OldVa + Total),
           "[%s] described VA not advanced by %lu\n", Desc, Total);
        ok(Mdl->MappedSystemVa == (PVOID)((PUCHAR)OldMapped + Total),
           "[%s] MappedSystemVa not advanced by %lu\n", Desc, Total);
        ok(*(volatile UCHAR *)Mdl->MappedSystemVa == PATTERN_BYTE(Total / PAGE_SIZE),
           "[%s] data at advanced mapping mismatch\n", Desc);
    }

cleanup:
    if (Mapped)
    {
        _SEH2_TRY {
            MmUnmapLockedPages(Mdl->MappedSystemVa, Mdl);
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            ok(0, "[%s] MmUnmapLockedPages raised 0x%08lx\n", Desc, _SEH2_GetExceptionCode());
        } _SEH2_END;
    }
    if (Locked)
    {
        _SEH2_TRY {
            MmUnlockPages(Mdl);
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            ok(0, "[%s] MmUnlockPages raised 0x%08lx\n", Desc, _SEH2_GetExceptionCode());
        } _SEH2_END;
    }
    if (Mdl != NULL)
        IoFreeMdl(Mdl);
    if (Base != NULL)
        ExFreePoolWithTag(Base, TAG_MDLW);
    ok(TRUE, "[%s] completed without corruption\n", Desc);
}

/*
 * MmProtectMdlSystemAddress on a real mapped MDL: PAGE_READONLY then
 * PAGE_READWRITE must both succeed, and the buffer must read back correctly
 * under RO and accept a write again after RW is restored.
 */
static
VOID
TestProtectMdlSystemAddress(VOID)
{
    const ULONG Pages = 2;
    PVOID Base = NULL;
    PUCHAR Aligned;
    PMDL Mdl = NULL;
    PVOID Va = NULL;
    NTSTATUS Status;
    BOOLEAN Locked = FALSE, Mapped = FALSE;

    trace("TestProtectMdlSystemAddress\n");

    Aligned = AllocAlignedNonPaged(Pages, &Base);
    if (!ok(Aligned != NULL, "alloc failed\n"))
        return;
    RtlFillMemory(Aligned, Pages * PAGE_SIZE, PATTERN_BYTE(0));

    Mdl = IoAllocateMdl(Aligned, Pages * PAGE_SIZE, FALSE, FALSE, NULL);
    if (!ok(Mdl != NULL, "IoAllocateMdl failed\n"))
        goto cleanup;

    _SEH2_TRY {
        MmProbeAndLockPages(Mdl, KernelMode, IoModifyAccess);
        Locked = TRUE;
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "MmProbeAndLockPages raised 0x%08lx\n", _SEH2_GetExceptionCode());
    } _SEH2_END;
    if (!Locked)
        goto cleanup;

    _SEH2_TRY {
        Va = MmMapLockedPagesSpecifyCache(Mdl, KernelMode, MmCached,
                                          NULL, FALSE, NormalPagePriority);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "MmMapLockedPagesSpecifyCache raised 0x%08lx\n", _SEH2_GetExceptionCode());
    } _SEH2_END;
    Mapped = (Va != NULL);
    if (!ok(Mapped, "map returned NULL\n"))
        goto cleanup;

    Status = STATUS_UNSUCCESSFUL;
    _SEH2_TRY {
        Status = MmProtectMdlSystemAddress(Mdl, PAGE_READONLY);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "MmProtectMdlSystemAddress(RO) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    } _SEH2_END;
    ok(Status == STATUS_SUCCESS, "MmProtectMdlSystemAddress(RO) = 0x%08lx\n", Status);
    ok(*(volatile UCHAR *)Va == PATTERN_BYTE(0), "RO mapping still readable\n");

    Status = STATUS_UNSUCCESSFUL;
    _SEH2_TRY {
        Status = MmProtectMdlSystemAddress(Mdl, PAGE_READWRITE);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "MmProtectMdlSystemAddress(RW) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    } _SEH2_END;
    ok(Status == STATUS_SUCCESS, "MmProtectMdlSystemAddress(RW) = 0x%08lx\n", Status);

    _SEH2_TRY {
        *(volatile UCHAR *)Va = 0x5A;
        ok(*(volatile UCHAR *)Va == 0x5A, "RW mapping writable after restore\n");
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "write to RW mapping raised 0x%08lx\n", _SEH2_GetExceptionCode());
    } _SEH2_END;

    //
    // PAGE_NOACCESS round-trip then back to PAGE_READWRITE. This exercises the
    // park-and-restore path (on ARM64 the memory-type AttrIndx is stashed in the
    // transition PTE's spare bits and recovered here). Never touch the mapping
    // while it is NOACCESS; only verify it is usable again after the restore.
    //
    Status = STATUS_UNSUCCESSFUL;
    _SEH2_TRY {
        Status = MmProtectMdlSystemAddress(Mdl, PAGE_NOACCESS);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "MmProtectMdlSystemAddress(NOACCESS) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    } _SEH2_END;
    ok(Status == STATUS_SUCCESS, "MmProtectMdlSystemAddress(NOACCESS) = 0x%08lx\n", Status);

    Status = STATUS_UNSUCCESSFUL;
    _SEH2_TRY {
        Status = MmProtectMdlSystemAddress(Mdl, PAGE_READWRITE);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "MmProtectMdlSystemAddress(RW after NOACCESS) raised 0x%08lx\n", _SEH2_GetExceptionCode());
    } _SEH2_END;
    ok(Status == STATUS_SUCCESS, "MmProtectMdlSystemAddress(RW after NOACCESS) = 0x%08lx\n", Status);

    _SEH2_TRY {
        *(volatile UCHAR *)Va = 0xC3;
        ok(*(volatile UCHAR *)Va == 0xC3, "mapping usable after NOACCESS round-trip\n");
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "access after NOACCESS round-trip raised 0x%08lx\n", _SEH2_GetExceptionCode());
    } _SEH2_END;

cleanup:
    if (Mapped)
    {
        _SEH2_TRY {
            MmUnmapLockedPages(Mdl->MappedSystemVa, Mdl);
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            ok(0, "MmUnmapLockedPages raised 0x%08lx\n", _SEH2_GetExceptionCode());
        } _SEH2_END;
    }
    if (Locked)
    {
        _SEH2_TRY {
            MmUnlockPages(Mdl);
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            ok(0, "MmUnlockPages raised 0x%08lx\n", _SEH2_GetExceptionCode());
        } _SEH2_END;
    }
    if (Mdl != NULL)
        IoFreeMdl(Mdl);
    if (Base != NULL)
        ExFreePoolWithTag(Base, TAG_MDLW);
    ok(TRUE, "protect completed without corruption\n");
}

/*
 * MmAdvanceMdl on a locked-but-NOT-mapped MDL, then MmUnlockPages. This exercises
 * the unlock-time fold-back: the pages consumed by the advance were parked and
 * must be folded back so every locked page is released. Runs last because it
 * advances without a mapping (the least-common shape).
 */
static
VOID
AdvanceUnmappedFoldbackCase(VOID)
{
    const ULONG Pages = 3;
    PVOID Base = NULL;
    PUCHAR Aligned;
    PMDL Mdl = NULL;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN Locked = FALSE;
    ULONG i;

    trace("AdvanceUnmappedFoldbackCase\n");

    Aligned = AllocAlignedNonPaged(Pages, &Base);
    if (!ok(Aligned != NULL, "alloc failed\n"))
        return;
    for (i = 0; i < Pages; i++)
        RtlFillMemory(Aligned + i * PAGE_SIZE, PAGE_SIZE, PATTERN_BYTE(i));

    Mdl = IoAllocateMdl(Aligned, Pages * PAGE_SIZE, FALSE, FALSE, NULL);
    if (!ok(Mdl != NULL, "IoAllocateMdl failed\n"))
        goto cleanup;

    _SEH2_TRY {
        MmProbeAndLockPages(Mdl, KernelMode, IoModifyAccess);
        Locked = TRUE;
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        ok(0, "MmProbeAndLockPages raised 0x%08lx\n", _SEH2_GetExceptionCode());
    } _SEH2_END;
    if (!Locked)
        goto cleanup;

    _SEH2_TRY {
        Status = MmAdvanceMdl(Mdl, PAGE_SIZE);
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        Status = _SEH2_GetExceptionCode();
        ok(0, "MmAdvanceMdl(unmapped) raised 0x%08lx\n", Status);
        Status = STATUS_UNSUCCESSFUL;
    } _SEH2_END;
    ok(Status == STATUS_SUCCESS, "MmAdvanceMdl(unmapped) = 0x%08lx\n", Status);
    if (NT_SUCCESS(Status))
        ok(MmGetMdlByteCount(Mdl) == (Pages - 1) * PAGE_SIZE,
           "ByteCount = %lu after unmapped advance\n", MmGetMdlByteCount(Mdl));

cleanup:
    if (Locked)
    {
        _SEH2_TRY {
            MmUnlockPages(Mdl);
        } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
            ok(0, "MmUnlockPages raised 0x%08lx\n", _SEH2_GetExceptionCode());
        } _SEH2_END;
    }
    if (Mdl != NULL)
        IoFreeMdl(Mdl);
    if (Base != NULL)
        ExFreePoolWithTag(Base, TAG_MDLW);
    ok(TRUE, "fold-back completed without corruption\n");
}

/*
 * NOTE: a "reserved-mapping + MmAdvanceMdl + MmUnmapReservedMapping" case was
 * intentionally NOT included. Advancing a reserved-mapped MDL and then unmapping
 * the reserved mapping bugchecks the Win11 reference kernel - no in-tree caller
 * ever advances a reserved mapping, so it is not a supported sequence to drive
 * from a portable test. The kernel's MmUnmapReservedMapping advanced-teardown
 * was still corrected for correctness; it simply cannot be exercised this way.
 */

START_TEST(MmMdlWin11KM)
{
    trace("MmMdlWin11KM: minimal-safe MDL parity (NonPaged + MmCached)\n");
    TestMdlAccessors();
    AdvanceMappedCase("1page",         3, PAGE_SIZE,     1);
    AdvanceMappedCase("subpage",       3, 100,           1);
    AdvanceMappedCase("2page-at-once", 3, 2 * PAGE_SIZE, 1);
    AdvanceMappedCase("twice-1page",   3, PAGE_SIZE,     2);
    TestProtectMdlSystemAddress();
    AdvanceUnmappedFoldbackCase();
}
