/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Patch-location validation against the DMA buffer
 *
 * DXGK_ALLOCATIONLIST carries no size, so dxgkrnl is the only thing that can
 * bounds-check a patch.  An unchecked patch offset writes a GPU address into
 * memory past the end of the command buffer.
 */

#include <kmt_test.h>
#include "dma_core.h"

#define POINTER_SIZE 8

static VOID Setup(_Out_ PDXGK_DMA_ALLOCATION_LIST List, _Out_ PDXGK_DMA_RING Ring)
{
    ULONG Index;

    DxgkDmaCoreAllocationListInitialize(List);
    { NTSTATUS Observed = DxgkDmaCoreAllocationListAdd(List, 0x100, 0x1000, 1, FALSE, &Index); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDmaCoreAllocationListAdd(List, 0x200, 0x2000, 0, FALSE, &Index); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDmaCoreRingInitialize(Ring, 0x10000, 4096); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkDmaCoreRingSetSubmission(Ring, 0, 0x1000, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
}

static VOID TestInBounds(VOID)
{
    DXGK_DMA_ALLOCATION_LIST List;
    DXGK_DMA_RING Ring;
    DXGK_DMA_PATCH Patch;

    Setup(&List, &Ring);
    RtlZeroMemory(&Patch, sizeof(Patch));
    Patch.AllocationIndex = 0;
    Patch.PatchOffset = 0;
    Patch.AllocationOffset = 0;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* The last pointer that fits entirely inside the submitted span. */
    Patch.PatchOffset = 0x1000 - POINTER_SIZE;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_SUCCESS); }

    Patch.AllocationOffset = 0xFFF;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_SUCCESS); }
}

static VOID TestOutOfBounds(VOID)
{
    DXGK_DMA_ALLOCATION_LIST List;
    DXGK_DMA_RING Ring;
    DXGK_DMA_PATCH Patch;

    Setup(&List, &Ring);
    RtlZeroMemory(&Patch, sizeof(Patch));

    /*
     * A pointer that starts inside the buffer but ends past it is the case a
     * naive "offset < size" check misses; those trailing bytes land in
     * whatever follows the command buffer.
     */
    Patch.PatchOffset = 0x1000 - (POINTER_SIZE - 1);
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    Patch.PatchOffset = 0x1000;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    Patch.PatchOffset = 0xFFFFFFF0UL;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* An index past the list would read an allocation that is not there. */
    Patch.PatchOffset = 0;
    Patch.AllocationIndex = 2;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    Patch.AllocationIndex = 0xFFFFFFFFUL;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }

    /* An offset past the allocation would point the GPU past its end. */
    Patch.AllocationIndex = 0;
    Patch.AllocationOffset = 0x1000;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestAlignmentAndResidency(VOID)
{
    DXGK_DMA_ALLOCATION_LIST List;
    DXGK_DMA_RING Ring;
    DXGK_DMA_PATCH Patch;

    Setup(&List, &Ring);
    RtlZeroMemory(&Patch, sizeof(Patch));

    /* A GPU pointer must be naturally aligned or the GPU reads a torn value. */
    Patch.PatchOffset = 4;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_DATATYPE_MISALIGNMENT); }
    Patch.PatchOffset = 1;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE); ok_eq_hex(Observed, STATUS_DATATYPE_MISALIGNMENT); }
    /* A 4-byte pointer has a weaker requirement, and 4 is fine for it. */
    Patch.PatchOffset = 4;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, 4); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* Patching a GPU address for something not resident hands the GPU an
     * address that does not exist. */
    Patch.PatchOffset = 0;
    Patch.AllocationIndex = 1;   /* segment 0 */
    ok(!NT_SUCCESS(DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, POINTER_SIZE)),
       "a non-resident allocation cannot be patched\n");

    { NTSTATUS Observed = DxgkDmaCoreValidatePatch(&List, &Ring, &Patch, 0); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
}

static VOID TestPatchList(VOID)
{
    DXGK_DMA_ALLOCATION_LIST List;
    DXGK_DMA_RING Ring;
    DXGK_DMA_PATCH Patches[4];
    ULONG Failed = 0xFFFFFFFF;

    Setup(&List, &Ring);
    RtlZeroMemory(Patches, sizeof(Patches));
    Patches[0].PatchOffset = 0;
    Patches[1].PatchOffset = 8;
    Patches[2].PatchOffset = 16;
    Patches[3].PatchOffset = 24;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatchList(&List, &Ring, Patches, 4, POINTER_SIZE, &Failed); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* One bad patch fails the whole submission and names the offender, so a
     * partially applied patch list never reaches the GPU. */
    Patches[2].PatchOffset = 0x1000;
    { NTSTATUS Observed = DxgkDmaCoreValidatePatchList(&List, &Ring, Patches, 4, POINTER_SIZE, &Failed); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    ok_eq_ulong(Failed, 2UL);

    { NTSTATUS Observed = DxgkDmaCoreValidatePatchList(&List, &Ring, Patches, 0, POINTER_SIZE, &Failed); ok_eq_hex(Observed, STATUS_SUCCESS); }
    {
        NTSTATUS Observed = DxgkDmaCoreValidatePatchList(&List, &Ring, Patches,
                                DXGK_DMA_CORE_MAX_PATCHES + 1, POINTER_SIZE, &Failed);

        ok_eq_hex(Observed, STATUS_INVALID_PARAMETER);
    }
}

START_TEST(DxgkPatchLocation)
{
    TestInBounds();
    TestOutOfBounds();
    TestAlignmentAndResidency();
    TestPatchList();
}

/* EOF */
