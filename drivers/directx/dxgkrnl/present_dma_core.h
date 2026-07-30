/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Testable Present DMA geometry and private-data invariants
 */

#pragma once

#define DXGK_PRESENT_DMA_COMPAT_BYTES         0x1000U
#define DXGK_PRESENT_DMA_MAX_BYTES            (1024U * 1024U)
#define DXGK_PRESENT_DMA_MAX_PRIVATE_BYTES    (64U * 1024U)
#define DXGK_PRESENT_DMA_MIN_ALLOCATIONS      3U
#define DXGK_PRESENT_DMA_MAX_ALLOCATIONS      4096U
#define DXGK_PRESENT_DMA_COMPAT_PATCHES       2U

typedef struct _DXGK_PRESENT_DMA_GEOMETRY
{
    ULONG DmaBufferSize;
    ULONG DmaBufferSegmentSet;
    ULONG DmaBufferPrivateDataSize;
    ULONG AllocationListSize;
    ULONG PatchLocationListSize;
    BOOLEAN Declared;
} DXGK_PRESENT_DMA_GEOMETRY, *PDXGK_PRESENT_DMA_GEOMETRY;

/*
 * Keep the compatibility geometry only for miniports that returned no legacy
 * DXGK_DEVICEINFO at all.  A declared zero-sized DMA buffer is invalid: it is
 * not equivalent to an old miniport that omitted the obsolete pInfo output.
 *
 * AllocationListSize is a minimum guaranteed to the miniport and therefore
 * cannot be clamped.  PatchLocationListSize is an explicit field in
 * DXGKARG_PRESENT and can be bounded to the scheduler's inline copy.
 */
FORCEINLINE
NTSTATUS
DxgkPresentDmaCoreSelectGeometry(
    _In_ BOOLEAN HasDeclaredGeometry,
    _In_ ULONG DmaBufferSize,
    _In_ ULONG DmaBufferSegmentSet,
    _In_ ULONG DmaBufferPrivateDataSize,
    _In_ ULONG AllocationListSize,
    _In_ ULONG PatchLocationListSize,
    _In_ ULONG MaximumPatchLocationListSize,
    _Out_ PDXGK_PRESENT_DMA_GEOMETRY Geometry)
{
    if (Geometry == NULL ||
        MaximumPatchLocationListSize < DXGK_PRESENT_DMA_COMPAT_PATCHES)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Geometry, sizeof(*Geometry));
    if (!HasDeclaredGeometry)
    {
        Geometry->DmaBufferSize = DXGK_PRESENT_DMA_COMPAT_BYTES;
        Geometry->AllocationListSize = DXGK_PRESENT_DMA_MIN_ALLOCATIONS;
        Geometry->PatchLocationListSize = DXGK_PRESENT_DMA_COMPAT_PATCHES;
        return STATUS_SUCCESS;
    }

    if (DmaBufferSize == 0 ||
        DmaBufferSize > DXGK_PRESENT_DMA_MAX_BYTES ||
        DmaBufferPrivateDataSize > DXGK_PRESENT_DMA_MAX_PRIVATE_BYTES)
    {
        return STATUS_INVALID_PARAMETER;
    }
    /*
     * DxgkAllocateDmaBuffer currently creates contiguous system-memory DMA
     * buffers (SegmentId 0).  It cannot honestly satisfy a miniport request
     * for one of the aperture segments in this mask.
     */
    if (DmaBufferSegmentSet != 0)
        return STATUS_NOT_SUPPORTED;

    if (AllocationListSize < DXGK_PRESENT_DMA_MIN_ALLOCATIONS ||
        AllocationListSize > DXGK_PRESENT_DMA_MAX_ALLOCATIONS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PatchLocationListSize == 0 ||
        PatchLocationListSize > MaximumPatchLocationListSize)
    {
        PatchLocationListSize = MaximumPatchLocationListSize;
    }

    Geometry->DmaBufferSize = DmaBufferSize;
    Geometry->DmaBufferSegmentSet = DmaBufferSegmentSet;
    Geometry->DmaBufferPrivateDataSize = DmaBufferPrivateDataSize;
    Geometry->AllocationListSize = AllocationListSize;
    Geometry->PatchLocationListSize = PatchLocationListSize;
    Geometry->Declared = TRUE;
    return STATUS_SUCCESS;
}

FORCEINLINE
NTSTATUS
DxgkPresentDmaCoreAllocationListBytes(
    _In_ ULONG AllocationListSize,
    _In_ SIZE_T AllocationEntrySize,
    _Out_ PSIZE_T AllocationListBytes)
{
    if (AllocationListBytes == NULL)
        return STATUS_INVALID_PARAMETER;
    *AllocationListBytes = 0;

    if (AllocationListSize < DXGK_PRESENT_DMA_MIN_ALLOCATIONS ||
        AllocationListSize > DXGK_PRESENT_DMA_MAX_ALLOCATIONS ||
        AllocationEntrySize == 0 ||
        (SIZE_T)AllocationListSize > MAXULONG_PTR / AllocationEntrySize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *AllocationListBytes =
        (SIZE_T)AllocationListSize * AllocationEntrySize;
    return STATUS_SUCCESS;
}

FORCEINLINE
NTSTATUS
DxgkPresentDmaCoreInitializeAllocationList(
    _Out_writes_bytes_(AllocationListBytes) PVOID AllocationList,
    _In_ SIZE_T AllocationListBytes)
{
    if (AllocationList == NULL || AllocationListBytes == 0)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(AllocationList, AllocationListBytes);
    return STATUS_SUCCESS;
}

/*
 * A zero private-data declaration is represented by exactly NULL/0.  For a
 * nonzero declaration, initialize precisely the caller-provided extent; the
 * canary test verifies that neither adjacent byte is touched.
 */
FORCEINLINE
NTSTATUS
DxgkPresentDmaCoreInitializePrivateData(
    _Out_writes_bytes_opt_(PrivateDataSize) PVOID PrivateData,
    _In_ ULONG PrivateDataSize)
{
    if (PrivateDataSize == 0)
        return PrivateData == NULL ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;
    if (PrivateData == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(PrivateData, PrivateDataSize);
    return STATUS_SUCCESS;
}
