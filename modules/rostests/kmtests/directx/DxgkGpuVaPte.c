/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     GPU page-table entry encoding and protection
 *
 * A PTE is the only thing standing between one process's GPU work and another
 * process's memory.  An entry that is valid when it should fault, or writable
 * when it was mapped read-only, is a containment failure.
 */

#include <kmt_test.h>
#include "gpuva_core.h"

static VOID TestProtectionValidation(VOID)
{
    ok_bool_true(DxgkGpuVaCoreProtectionValid(0), "read-only is the empty set");
    ok_bool_true(DxgkGpuVaCoreProtectionValid(DXGK_GPUVA_PROT_WRITE), "write");
    ok_bool_true(DxgkGpuVaCoreProtectionValid(DXGK_GPUVA_PROT_WRITE | DXGK_GPUVA_PROT_EXECUTE), "write+execute");
    ok_bool_true(DxgkGpuVaCoreProtectionValid(DXGK_GPUVA_PROT_NO_ACCESS), "no-access alone");
    ok_bool_true(DxgkGpuVaCoreProtectionValid(DXGK_GPUVA_PROT_NO_ACCESS | DXGK_GPUVA_PROT_SYSTEM_USE_ONLY),
                 "no-access on a kernel-owned range");

    /* Undefined bits would be handed to the miniport unexamined. */
    ok_bool_false(DxgkGpuVaCoreProtectionValid(0x80000000UL), "undefined bit");
    ok_bool_false(DxgkGpuVaCoreProtectionValid(~DXGK_GPUVA_PROT_VALID_MASK), "all undefined bits");

    /* No-access must be exclusive, or the two readers of the flag disagree
     * about whether the page faults. */
    ok_bool_false(DxgkGpuVaCoreProtectionValid(DXGK_GPUVA_PROT_NO_ACCESS | DXGK_GPUVA_PROT_WRITE),
                  "no-access with write");
    ok_bool_false(DxgkGpuVaCoreProtectionValid(DXGK_GPUVA_PROT_NO_ACCESS | DXGK_GPUVA_PROT_EXECUTE),
                  "no-access with execute");
    ok_bool_false(DxgkGpuVaCoreProtectionValid(DXGK_GPUVA_PROT_NO_ACCESS | DXGK_GPUVA_PROT_ZERO),
                  "no-access with zero");
}

static VOID TestEncoding(VOID)
{
    DXGK_GPUVA_PTE Pte;
    NTSTATUS Status;

    Status = DxgkGpuVaCoreEncodePte(0x2000, DXGK_GPUVA_PROT_WRITE, FALSE, &Pte);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(Pte.PageAddress, 0x2000ULL);
    ok_eq_ulong(Pte.Protection, (ULONG)DXGK_GPUVA_PROT_WRITE);
    ok_bool_true(Pte.Valid, "a writable mapping is valid");
    ok_bool_false(Pte.LargePage, "not a large page");

    /* An unaligned page address would put the low bits of an address where
     * hardware expects flags. */
    Status = DxgkGpuVaCoreEncodePte(0x2001, DXGK_GPUVA_PROT_WRITE, FALSE, &Pte);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = DxgkGpuVaCoreEncodePte(0xFFF, 0, FALSE, &Pte);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = DxgkGpuVaCoreEncodePte(0x2000, DXGK_GPUVA_PROT_NO_ACCESS | DXGK_GPUVA_PROT_WRITE, FALSE, &Pte);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    /* A no-access entry is a hole and must never be marked valid, or the GPU
     * reads through it instead of faulting. */
    Status = DxgkGpuVaCoreEncodePte(0x3000, DXGK_GPUVA_PROT_NO_ACCESS, FALSE, &Pte);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_false(Pte.Valid, "no-access is never valid");
    ok_bool_false(DxgkGpuVaCorePteGrantsRead(&Pte), "no-access denies read");
    ok_bool_false(DxgkGpuVaCorePteGrantsWrite(&Pte), "no-access denies write");

    Status = DxgkGpuVaCoreEncodePte(0x200000, DXGK_GPUVA_PROT_WRITE, TRUE, &Pte);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(Pte.LargePage, "large page recorded");
}

static VOID TestAccessRights(VOID)
{
    DXGK_GPUVA_PTE ReadOnly;
    DXGK_GPUVA_PTE Writable;
    DXGK_GPUVA_PTE Invalid;

    { NTSTATUS Observed = DxgkGpuVaCoreEncodePte(0x1000, 0, FALSE, &ReadOnly); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkGpuVaCoreEncodePte(0x1000, DXGK_GPUVA_PROT_WRITE, FALSE, &Writable); ok_eq_hex(Observed, STATUS_SUCCESS); }
    DxgkGpuVaCoreEncodeInvalidPte(&Invalid);

    ok_bool_true(DxgkGpuVaCorePteGrantsRead(&ReadOnly), "read-only grants read");
    /* The whole point of a read-only mapping: it must not silently permit a
     * write just because the page is valid. */
    ok_bool_false(DxgkGpuVaCorePteGrantsWrite(&ReadOnly), "read-only denies write");
    ok_bool_true(DxgkGpuVaCorePteGrantsWrite(&Writable), "writable grants write");
    ok_bool_true(DxgkGpuVaCorePteGrantsRead(&Writable), "writable grants read");

    ok_bool_false(Invalid.Valid, "the invalid pattern is not valid");
    ok_bool_false(DxgkGpuVaCorePteGrantsRead(&Invalid), "invalid denies read");
    ok_bool_false(DxgkGpuVaCorePteGrantsWrite(&Invalid), "invalid denies write");
    ok_eq_ulonglong(Invalid.PageAddress, 0ULL);

    /* Equality is what an incremental page-table update uses to decide a
     * write can be skipped; it must compare every field. */
    ok_bool_true(DxgkGpuVaCorePteEqual(&ReadOnly, &ReadOnly), "identical");
    ok_bool_false(DxgkGpuVaCorePteEqual(&ReadOnly, &Writable), "protection differs");
    ok_bool_false(DxgkGpuVaCorePteEqual(&ReadOnly, &Invalid), "validity differs");
    {
        DXGK_GPUVA_PTE Elsewhere;

        { NTSTATUS Observed = DxgkGpuVaCoreEncodePte(0x9000, 0, FALSE, &Elsewhere); ok_eq_hex(Observed, STATUS_SUCCESS); }
        ok_bool_false(DxgkGpuVaCorePteEqual(&ReadOnly, &Elsewhere), "address differs");
    }
}

START_TEST(DxgkGpuVaPte)
{
    TestProtectionValidation();
    TestEncoding();
    TestAccessRights();
}

/* EOF */
