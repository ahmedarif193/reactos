/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU virtual address algebra, page-table geometry and PTE format
 *
 * The arithmetic a GPU MMU depends on, with no dxgkrnl or miniport types so it
 * can be exercised on its own.  Every operation here is total: a request that
 * cannot be represented is refused rather than wrapping, because a silently
 * wrapped GPU address is a write into somebody else's memory.
 */

#ifndef _DXGK_GPUVA_CORE_H_
#define _DXGK_GPUVA_CORE_H_

#include <ntddk.h>

#define DXGK_GPUVA_CORE_MAX_LEVELS      6
#define DXGK_GPUVA_CORE_PAGE_SHIFT      12
#define DXGK_GPUVA_CORE_PAGE_SIZE       (1ULL << DXGK_GPUVA_CORE_PAGE_SHIFT)

/* One miniport-declared page-table level, innermost (level 0) first. */
typedef struct _DXGK_GPUVA_LEVEL_DESC
{
    ULONG IndexBitCount;        /* VA bits this level consumes */
    ULONG TableSizeInBytes;     /* bytes of one table at this level */
} DXGK_GPUVA_LEVEL_DESC, *PDXGK_GPUVA_LEVEL_DESC;

typedef struct _DXGK_GPUVA_GEOMETRY
{
    DXGK_GPUVA_LEVEL_DESC Levels[DXGK_GPUVA_CORE_MAX_LEVELS];
    ULONG LevelCount;
    ULONG VirtualAddressBitCount;
} DXGK_GPUVA_GEOMETRY, *PDXGK_GPUVA_GEOMETRY;

/* Mirrors D3DDDIGPUVIRTUALADDRESS_PROTECTION_TYPE's meaningful bits without
 * pulling the D3D headers into the core. */
#define DXGK_GPUVA_PROT_WRITE           0x00000001UL
#define DXGK_GPUVA_PROT_EXECUTE         0x00000002UL
#define DXGK_GPUVA_PROT_ZERO            0x00000004UL
#define DXGK_GPUVA_PROT_NO_ACCESS       0x00000008UL
#define DXGK_GPUVA_PROT_SYSTEM_USE_ONLY 0x00000010UL
#define DXGK_GPUVA_PROT_VALID_MASK      0x0000001FUL

/* A page-table entry in the software format dxgkrnl publishes to the miniport. */
typedef struct _DXGK_GPUVA_PTE
{
    ULONGLONG PageAddress;      /* 4K-aligned physical/segment address */
    ULONG     Protection;       /* DXGK_GPUVA_PROT_* */
    BOOLEAN   Valid;
    BOOLEAN   LargePage;
} DXGK_GPUVA_PTE, *PDXGK_GPUVA_PTE;

/* --- address algebra ------------------------------------------------- */

BOOLEAN DxgkGpuVaCoreAlignUp(_In_ ULONGLONG Value, _In_ ULONGLONG Alignment, _Out_ PULONGLONG Result);
BOOLEAN DxgkGpuVaCoreRangeEnd(_In_ ULONGLONG Address, _In_ ULONGLONG Size, _Out_ PULONGLONG EndAddress);
BOOLEAN DxgkGpuVaCoreRangesOverlap(_In_ ULONGLONG StartA, _In_ ULONGLONG SizeA, _In_ ULONGLONG StartB, _In_ ULONGLONG SizeB);
BOOLEAN DxgkGpuVaCoreRangeContains(_In_ ULONGLONG OuterStart, _In_ ULONGLONG OuterSize, _In_ ULONGLONG InnerStart, _In_ ULONGLONG InnerSize);
BOOLEAN DxgkGpuVaCoreIsPageAligned(_In_ ULONGLONG Value);
BOOLEAN DxgkGpuVaCorePageCount(_In_ ULONGLONG Address, _In_ ULONGLONG Size, _Out_ PULONGLONG PageCount);

/* --- page-table geometry --------------------------------------------- */

NTSTATUS DxgkGpuVaCoreValidateGeometry(_In_ const DXGK_GPUVA_GEOMETRY *Geometry);
BOOLEAN DxgkGpuVaCoreLevelShift(_In_ const DXGK_GPUVA_GEOMETRY *Geometry, _In_ ULONG Level, _Out_ PULONG Shift);
BOOLEAN DxgkGpuVaCoreEntriesPerTable(_In_ const DXGK_GPUVA_GEOMETRY *Geometry, _In_ ULONG Level, _Out_ PULONGLONG Entries);
BOOLEAN DxgkGpuVaCorePteIndex(_In_ const DXGK_GPUVA_GEOMETRY *Geometry, _In_ ULONGLONG Va, _In_ ULONG Level, _Out_ PULONG Index);
BOOLEAN DxgkGpuVaCoreCoveragePerEntry(_In_ const DXGK_GPUVA_GEOMETRY *Geometry, _In_ ULONG Level, _Out_ PULONGLONG Coverage);
BOOLEAN DxgkGpuVaCoreAddressIsRepresentable(_In_ const DXGK_GPUVA_GEOMETRY *Geometry, _In_ ULONGLONG Va);
BOOLEAN DxgkGpuVaCoreTableCountForRange(_In_ const DXGK_GPUVA_GEOMETRY *Geometry, _In_ ULONG Level, _In_ ULONGLONG Address, _In_ ULONGLONG Size, _Out_ PULONGLONG TableCount);

/* --- page-table entries ---------------------------------------------- */

/* PTE-level protection encoding.  Distinct from the map/unmap request
 * validation in gpuva.c, which polices the D3D bitfield a caller supplies. */
BOOLEAN DxgkGpuVaCoreProtectionValid(_In_ ULONG Protection);
NTSTATUS DxgkGpuVaCoreEncodePte(_In_ ULONGLONG PageAddress, _In_ ULONG Protection, _In_ BOOLEAN LargePage, _Out_ PDXGK_GPUVA_PTE Pte);
VOID DxgkGpuVaCoreEncodeInvalidPte(_Out_ PDXGK_GPUVA_PTE Pte);
BOOLEAN DxgkGpuVaCorePteGrantsWrite(_In_ const DXGK_GPUVA_PTE *Pte);
BOOLEAN DxgkGpuVaCorePteGrantsRead(_In_ const DXGK_GPUVA_PTE *Pte);
BOOLEAN DxgkGpuVaCorePteEqual(_In_ const DXGK_GPUVA_PTE *Left, _In_ const DXGK_GPUVA_PTE *Right);

#endif /* _DXGK_GPUVA_CORE_H_ */
