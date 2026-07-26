/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     GPU page-table geometry derived from the miniport's declaration
 *
 * dxgkrnl does not choose the page-table shape; the miniport declares it and
 * dxgkrnl must index it exactly.  A shift computed one level off walks the
 * wrong table and maps the wrong physical page.
 */

#include <kmt_test.h>
#include "gpuva_core.h"

/* The common 4-level, 9-bits-per-level, 48-bit layout. */
static VOID InitFourLevel(_Out_ PDXGK_GPUVA_GEOMETRY Geometry)
{
    ULONG Level;

    RtlZeroMemory(Geometry, sizeof(*Geometry));
    Geometry->LevelCount = 4;
    for (Level = 0; Level < 4; ++Level)
    {
        Geometry->Levels[Level].IndexBitCount = 9;
        Geometry->Levels[Level].TableSizeInBytes = 4096;
    }
    Geometry->VirtualAddressBitCount = 48;
}

static VOID TestGeometryValidation(VOID)
{
    DXGK_GPUVA_GEOMETRY Geometry;
    NTSTATUS Status;

    InitFourLevel(&Geometry);
    Status = DxgkGpuVaCoreValidateGeometry(&Geometry);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* A declared VA width that disagrees with the levels is a miniport bug we
     * must catch here rather than by mapping into nowhere. */
    Geometry.VirtualAddressBitCount = 47;
    Status = DxgkGpuVaCoreValidateGeometry(&Geometry);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Geometry.VirtualAddressBitCount = 0;   /* 0 = do not cross-check */
    Status = DxgkGpuVaCoreValidateGeometry(&Geometry);
    ok_eq_hex(Status, STATUS_SUCCESS);

    InitFourLevel(&Geometry);
    Geometry.LevelCount = 0;
    { NTSTATUS Observed = DxgkGpuVaCoreValidateGeometry(&Geometry); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    Geometry.LevelCount = DXGK_GPUVA_CORE_MAX_LEVELS + 1;
    { NTSTATUS Observed = DxgkGpuVaCoreValidateGeometry(&Geometry); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    InitFourLevel(&Geometry);
    Geometry.Levels[1].IndexBitCount = 0;
    { NTSTATUS Observed = DxgkGpuVaCoreValidateGeometry(&Geometry); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* A table too small for the entries its index width implies would be
     * indexed past the end of its own allocation. */
    InitFourLevel(&Geometry);
    Geometry.Levels[2].TableSizeInBytes = 256;   /* needs >= 512 */
    { NTSTATUS Observed = DxgkGpuVaCoreValidateGeometry(&Geometry); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* Levels that together demand more than 64 bits cannot be indexed. */
    InitFourLevel(&Geometry);
    Geometry.VirtualAddressBitCount = 0;
    Geometry.Levels[0].IndexBitCount = 30;
    Geometry.Levels[1].IndexBitCount = 30;
    Geometry.Levels[2].IndexBitCount = 30;
    { NTSTATUS Observed = DxgkGpuVaCoreValidateGeometry(&Geometry); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestShiftsAndIndexing(VOID)
{
    DXGK_GPUVA_GEOMETRY Geometry;
    ULONGLONG Entries = 0;
    ULONGLONG Coverage = 0;
    ULONG Shift = 0;
    ULONG Index = 0;

    InitFourLevel(&Geometry);

    /* Level 0 indexes immediately above the page offset. */
    ok_bool_true(DxgkGpuVaCoreLevelShift(&Geometry, 0, &Shift), "level 0 shift");
    ok_eq_ulong(Shift, 12UL);
    ok_bool_true(DxgkGpuVaCoreLevelShift(&Geometry, 1, &Shift), "level 1 shift");
    ok_eq_ulong(Shift, 21UL);
    ok_bool_true(DxgkGpuVaCoreLevelShift(&Geometry, 2, &Shift), "level 2 shift");
    ok_eq_ulong(Shift, 30UL);
    ok_bool_true(DxgkGpuVaCoreLevelShift(&Geometry, 3, &Shift), "level 3 shift");
    ok_eq_ulong(Shift, 39UL);
    ok_bool_false(DxgkGpuVaCoreLevelShift(&Geometry, 4, &Shift), "past the top level");

    ok_bool_true(DxgkGpuVaCoreEntriesPerTable(&Geometry, 0, &Entries), "entries per table");
    ok_eq_ulonglong(Entries, 512ULL);
    ok_bool_false(DxgkGpuVaCoreEntriesPerTable(&Geometry, 9, &Entries), "bad level");

    ok_bool_true(DxgkGpuVaCoreCoveragePerEntry(&Geometry, 0, &Coverage), "level 0 covers a page");
    ok_eq_ulonglong(Coverage, 0x1000ULL);
    ok_bool_true(DxgkGpuVaCoreCoveragePerEntry(&Geometry, 1, &Coverage), "level 1 covers 2MB");
    ok_eq_ulonglong(Coverage, 0x200000ULL);
    ok_bool_true(DxgkGpuVaCoreCoveragePerEntry(&Geometry, 2, &Coverage), "level 2 covers 1GB");
    ok_eq_ulonglong(Coverage, 0x40000000ULL);

    /* Index extraction must mask to the level's width, not just shift. */
    ok_bool_true(DxgkGpuVaCorePteIndex(&Geometry, 0, 0, &Index), "zero VA");
    ok_eq_ulong(Index, 0UL);
    ok_bool_true(DxgkGpuVaCorePteIndex(&Geometry, 0x1000, 0, &Index), "second page");
    ok_eq_ulong(Index, 1UL);
    ok_bool_true(DxgkGpuVaCorePteIndex(&Geometry, 0x200000, 0, &Index), "2MB wraps level 0");
    ok_eq_ulong(Index, 0UL);
    ok_bool_true(DxgkGpuVaCorePteIndex(&Geometry, 0x200000, 1, &Index), "2MB is level 1 entry 1");
    ok_eq_ulong(Index, 1UL);
    ok_bool_true(DxgkGpuVaCorePteIndex(&Geometry, 0x40000000, 2, &Index), "1GB is level 2 entry 1");
    ok_eq_ulong(Index, 1UL);
    ok_bool_true(DxgkGpuVaCorePteIndex(&Geometry, 0x1FF000, 0, &Index), "last entry of level 0");
    ok_eq_ulong(Index, 511UL);
}

static VOID TestRepresentableAndTableCount(VOID)
{
    DXGK_GPUVA_GEOMETRY Geometry;
    ULONGLONG Tables = 0;

    InitFourLevel(&Geometry);

    /* 48 bits of VA: anything above must be refused, not truncated. */
    ok_bool_true(DxgkGpuVaCoreAddressIsRepresentable(&Geometry, 0), "zero");
    ok_bool_true(DxgkGpuVaCoreAddressIsRepresentable(&Geometry, (1ULL << 48) - 1), "top of range");
    ok_bool_false(DxgkGpuVaCoreAddressIsRepresentable(&Geometry, 1ULL << 48), "one past the top");
    ok_bool_false(DxgkGpuVaCoreAddressIsRepresentable(&Geometry, MAXULONGLONG), "max");

    /* How many tables at a level a range touches, which is what the paging
     * path must allocate before it can map. */
    ok_bool_true(DxgkGpuVaCoreTableCountForRange(&Geometry, 0, 0, 0x1000, &Tables), "one page");
    ok_eq_ulonglong(Tables, 1ULL);
    ok_bool_true(DxgkGpuVaCoreTableCountForRange(&Geometry, 1, 0, 0x200000, &Tables), "exactly one 2MB span");
    ok_eq_ulonglong(Tables, 1ULL);
    ok_bool_true(DxgkGpuVaCoreTableCountForRange(&Geometry, 1, 0x1FF000, 0x2000, &Tables), "straddles a 2MB boundary");
    ok_eq_ulonglong(Tables, 2ULL);
    ok_bool_true(DxgkGpuVaCoreTableCountForRange(&Geometry, 1, 0, 0x400000, &Tables), "two full 2MB spans");
    ok_eq_ulonglong(Tables, 2ULL);
    ok_bool_false(DxgkGpuVaCoreTableCountForRange(&Geometry, 1, 0, 0, &Tables), "zero size");
    ok_bool_false(DxgkGpuVaCoreTableCountForRange(&Geometry, 9, 0, 0x1000, &Tables), "bad level");
}

START_TEST(DxgkGpuVaPageTable)
{
    TestGeometryValidation();
    TestShiftsAndIndexing();
    TestRepresentableAndTableCount();
}

/* EOF */
