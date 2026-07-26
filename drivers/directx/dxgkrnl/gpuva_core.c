/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU virtual address algebra, page-table geometry and PTE format
 */

#include "gpuva_core.h"

/* --- address algebra ------------------------------------------------- */

BOOLEAN
DxgkGpuVaCoreAlignUp(
    _In_ ULONGLONG Value,
    _In_ ULONGLONG Alignment,
    _Out_ PULONGLONG Result)
{
    *Result = 0;
    if (Alignment == 0 || (Alignment & (Alignment - 1)) != 0)
        return FALSE;
    /* Rounding up past the top of the address space must be refused, not
     * wrapped to a small address that then looks like a valid mapping. */
    if (Value > MAXULONGLONG - (Alignment - 1))
        return FALSE;
    *Result = (Value + Alignment - 1) & ~(Alignment - 1);
    return TRUE;
}

BOOLEAN
DxgkGpuVaCoreRangeEnd(
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Size,
    _Out_ PULONGLONG EndAddress)
{
    *EndAddress = 0;
    if (Size == 0 || Address > MAXULONGLONG - Size)
        return FALSE;
    *EndAddress = Address + Size;
    return TRUE;
}

BOOLEAN
DxgkGpuVaCoreRangesOverlap(
    _In_ ULONGLONG StartA,
    _In_ ULONGLONG SizeA,
    _In_ ULONGLONG StartB,
    _In_ ULONGLONG SizeB)
{
    ULONGLONG EndA;
    ULONGLONG EndB;

    /* An unrepresentable range cannot be reasoned about; treat it as not
     * overlapping so the caller's validation refuses it on its own terms. */
    if (!DxgkGpuVaCoreRangeEnd(StartA, SizeA, &EndA))
        return FALSE;
    if (!DxgkGpuVaCoreRangeEnd(StartB, SizeB, &EndB))
        return FALSE;
    return (StartA < EndB) && (StartB < EndA);
}

BOOLEAN
DxgkGpuVaCoreRangeContains(
    _In_ ULONGLONG OuterStart,
    _In_ ULONGLONG OuterSize,
    _In_ ULONGLONG InnerStart,
    _In_ ULONGLONG InnerSize)
{
    ULONGLONG OuterEnd;
    ULONGLONG InnerEnd;

    if (!DxgkGpuVaCoreRangeEnd(OuterStart, OuterSize, &OuterEnd))
        return FALSE;
    if (!DxgkGpuVaCoreRangeEnd(InnerStart, InnerSize, &InnerEnd))
        return FALSE;
    return (InnerStart >= OuterStart) && (InnerEnd <= OuterEnd);
}

BOOLEAN
DxgkGpuVaCoreIsPageAligned(
    _In_ ULONGLONG Value)
{
    return (Value & (DXGK_GPUVA_CORE_PAGE_SIZE - 1)) == 0;
}

BOOLEAN
DxgkGpuVaCorePageCount(
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Size,
    _Out_ PULONGLONG PageCount)
{
    ULONGLONG End;
    ULONGLONG FirstPage;
    ULONGLONG LastPage;

    *PageCount = 0;
    if (!DxgkGpuVaCoreRangeEnd(Address, Size, &End))
        return FALSE;
    /* Count the pages the range touches, not the pages its length spans: an
     * unaligned range crosses one more page than its size implies. */
    FirstPage = Address >> DXGK_GPUVA_CORE_PAGE_SHIFT;
    LastPage = (End - 1) >> DXGK_GPUVA_CORE_PAGE_SHIFT;
    *PageCount = LastPage - FirstPage + 1;
    return TRUE;
}

/* --- page-table geometry --------------------------------------------- */

NTSTATUS
DxgkGpuVaCoreValidateGeometry(
    _In_ const DXGK_GPUVA_GEOMETRY *Geometry)
{
    ULONG TotalBits = DXGK_GPUVA_CORE_PAGE_SHIFT;
    ULONG Level;

    if (Geometry->LevelCount == 0 || Geometry->LevelCount > DXGK_GPUVA_CORE_MAX_LEVELS)
        return STATUS_INVALID_PARAMETER;
    for (Level = 0; Level < Geometry->LevelCount; ++Level)
    {
        const DXGK_GPUVA_LEVEL_DESC *Desc = &Geometry->Levels[Level];

        if (Desc->IndexBitCount == 0 || Desc->IndexBitCount >= 64)
            return STATUS_INVALID_PARAMETER;
        if (Desc->TableSizeInBytes == 0)
            return STATUS_INVALID_PARAMETER;
        /* One table must hold the entries its index width implies; a table
         * smaller than that indexes off the end of its own allocation. */
        if ((ULONGLONG)Desc->TableSizeInBytes < (1ULL << Desc->IndexBitCount))
            return STATUS_INVALID_PARAMETER;
        if (TotalBits > 64 - Desc->IndexBitCount)
            return STATUS_INVALID_PARAMETER;
        TotalBits += Desc->IndexBitCount;
    }
    if (Geometry->VirtualAddressBitCount != 0 && Geometry->VirtualAddressBitCount != TotalBits)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

BOOLEAN
DxgkGpuVaCoreLevelShift(
    _In_ const DXGK_GPUVA_GEOMETRY *Geometry,
    _In_ ULONG Level,
    _Out_ PULONG Shift)
{
    ULONG Result = DXGK_GPUVA_CORE_PAGE_SHIFT;
    ULONG Index;

    *Shift = 0;
    if (Level >= Geometry->LevelCount)
        return FALSE;
    for (Index = 0; Index < Level; ++Index)
        Result += Geometry->Levels[Index].IndexBitCount;
    if (Result >= 64)
        return FALSE;
    *Shift = Result;
    return TRUE;
}

BOOLEAN
DxgkGpuVaCoreEntriesPerTable(
    _In_ const DXGK_GPUVA_GEOMETRY *Geometry,
    _In_ ULONG Level,
    _Out_ PULONGLONG Entries)
{
    *Entries = 0;
    if (Level >= Geometry->LevelCount)
        return FALSE;
    *Entries = 1ULL << Geometry->Levels[Level].IndexBitCount;
    return TRUE;
}

BOOLEAN
DxgkGpuVaCorePteIndex(
    _In_ const DXGK_GPUVA_GEOMETRY *Geometry,
    _In_ ULONGLONG Va,
    _In_ ULONG Level,
    _Out_ PULONG Index)
{
    ULONGLONG Entries;
    ULONG Shift;

    *Index = 0;
    if (!DxgkGpuVaCoreLevelShift(Geometry, Level, &Shift))
        return FALSE;
    if (!DxgkGpuVaCoreEntriesPerTable(Geometry, Level, &Entries))
        return FALSE;
    *Index = (ULONG)((Va >> Shift) & (Entries - 1ULL));
    return TRUE;
}

BOOLEAN
DxgkGpuVaCoreCoveragePerEntry(
    _In_ const DXGK_GPUVA_GEOMETRY *Geometry,
    _In_ ULONG Level,
    _Out_ PULONGLONG Coverage)
{
    ULONG Shift;

    *Coverage = 0;
    if (!DxgkGpuVaCoreLevelShift(Geometry, Level, &Shift))
        return FALSE;
    *Coverage = 1ULL << Shift;
    return TRUE;
}

BOOLEAN
DxgkGpuVaCoreAddressIsRepresentable(
    _In_ const DXGK_GPUVA_GEOMETRY *Geometry,
    _In_ ULONGLONG Va)
{
    ULONG TotalBits = DXGK_GPUVA_CORE_PAGE_SHIFT;
    ULONG Level;

    for (Level = 0; Level < Geometry->LevelCount; ++Level)
        TotalBits += Geometry->Levels[Level].IndexBitCount;
    if (TotalBits >= 64)
        return TRUE;
    return Va < (1ULL << TotalBits);
}

BOOLEAN
DxgkGpuVaCoreTableCountForRange(
    _In_ const DXGK_GPUVA_GEOMETRY *Geometry,
    _In_ ULONG Level,
    _In_ ULONGLONG Address,
    _In_ ULONGLONG Size,
    _Out_ PULONGLONG TableCount)
{
    ULONGLONG Coverage;
    ULONGLONG End;
    ULONGLONG First;
    ULONGLONG Last;

    *TableCount = 0;
    if (!DxgkGpuVaCoreCoveragePerEntry(Geometry, Level, &Coverage))
        return FALSE;
    if (!DxgkGpuVaCoreRangeEnd(Address, Size, &End))
        return FALSE;
    First = Address / Coverage;
    Last = (End - 1) / Coverage;
    *TableCount = Last - First + 1;
    return TRUE;
}

/* --- page-table entries ---------------------------------------------- */

BOOLEAN
DxgkGpuVaCoreProtectionValid(
    _In_ ULONG Protection)
{
    if ((Protection & ~DXGK_GPUVA_PROT_VALID_MASK) != 0)
        return FALSE;
    /* No-access is exclusive: a range cannot simultaneously deny access and
     * grant a right, or the two readers of the flag disagree. */
    if ((Protection & DXGK_GPUVA_PROT_NO_ACCESS) != 0 &&
        (Protection & ~(DXGK_GPUVA_PROT_NO_ACCESS | DXGK_GPUVA_PROT_SYSTEM_USE_ONLY)) != 0)
        return FALSE;
    return TRUE;
}

NTSTATUS
DxgkGpuVaCoreEncodePte(
    _In_ ULONGLONG PageAddress,
    _In_ ULONG Protection,
    _In_ BOOLEAN LargePage,
    _Out_ PDXGK_GPUVA_PTE Pte)
{
    RtlZeroMemory(Pte, sizeof(*Pte));
    if (!DxgkGpuVaCoreProtectionValid(Protection))
        return STATUS_INVALID_PARAMETER;
    if (!DxgkGpuVaCoreIsPageAligned(PageAddress))
        return STATUS_INVALID_PARAMETER;
    Pte->PageAddress = PageAddress;
    Pte->Protection = Protection;
    Pte->LargePage = LargePage;
    /* A no-access entry is a hole that must fault, so it is never valid. */
    Pte->Valid = ((Protection & DXGK_GPUVA_PROT_NO_ACCESS) == 0);
    return STATUS_SUCCESS;
}

VOID
DxgkGpuVaCoreEncodeInvalidPte(
    _Out_ PDXGK_GPUVA_PTE Pte)
{
    RtlZeroMemory(Pte, sizeof(*Pte));
    Pte->Protection = DXGK_GPUVA_PROT_NO_ACCESS;
}

BOOLEAN
DxgkGpuVaCorePteGrantsWrite(
    _In_ const DXGK_GPUVA_PTE *Pte)
{
    return Pte->Valid && (Pte->Protection & DXGK_GPUVA_PROT_WRITE) != 0;
}

BOOLEAN
DxgkGpuVaCorePteGrantsRead(
    _In_ const DXGK_GPUVA_PTE *Pte)
{
    return Pte->Valid && (Pte->Protection & DXGK_GPUVA_PROT_NO_ACCESS) == 0;
}

BOOLEAN
DxgkGpuVaCorePteEqual(
    _In_ const DXGK_GPUVA_PTE *Left,
    _In_ const DXGK_GPUVA_PTE *Right)
{
    return Left->PageAddress == Right->PageAddress &&
           Left->Protection == Right->Protection &&
           Left->Valid == Right->Valid &&
           Left->LargePage == Right->LargePage;
}

/* EOF */
