/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Testable KMT-to-miniport Present contract helpers
 */

#pragma once

#include <ntddk.h>
#include <windef.h>

#define DXGK_PRESENT_CORE_MAX_SUBRECTS 64U

typedef struct _DXGK_PRESENT_SCANOUT_RETIRE_POLICY
{
    BOOLEAN RefreshSharedPrimary;
    BOOLEAN ProgramSource;
    BOOLEAN HoldSharedSurfaceRundown;
} DXGK_PRESENT_SCANOUT_RETIRE_POLICY,
  *PDXGK_PRESENT_SCANOUT_RETIRE_POLICY;

#if (REACTOS_WDDM_TARGET_LEVEL >= 1200)
/*
 * Sync refresh counts are public 32-bit serial numbers.  Targets are expected
 * to be within one signed 32-bit interval of the current count; this comparison
 * remains correct when the counter wraps through zero.
 */
FORCEINLINE
BOOLEAN
DxgkPresentCoreRefreshTargetReached(
    _In_ ULONG CurrentRefreshCount,
    _In_ ULONG TargetRefreshCount)
{
    return (LONG)(CurrentRefreshCount - TargetRefreshCount) >= 0;
}

/*
 * A zero target is the native "next vertical blank" sentinel.  Even when the
 * current count is also zero it must remain pending until a vblank pulse
 * evaluates the target.
 */
FORCEINLINE
BOOLEAN
DxgkPresentCoreRefreshTargetShouldSignalImmediately(
    _In_ ULONG CurrentRefreshCount,
    _In_ ULONG TargetRefreshCount)
{
    return TargetRefreshCount != 0 &&
           DxgkPresentCoreRefreshTargetReached(CurrentRefreshCount,
                                               TargetRefreshCount);
}
#endif

FORCEINLINE
NTSTATUS
DxgkPresentCoreEvaluateScanoutRetirement(
    _In_ BOOLEAN Flip,
    _In_ BOOLEAN WritesDestination,
    _In_ BOOLEAN HasSource,
    _In_ BOOLEAN HasDestination,
    _In_ BOOLEAN DestinationIsSharedPrimary,
    _Out_ PDXGK_PRESENT_SCANOUT_RETIRE_POLICY Policy)
{
    if (Policy == NULL)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Policy, sizeof(*Policy));
    if (Flip)
    {
        if (!HasSource || HasDestination)
            return STATUS_INVALID_PARAMETER;
        Policy->ProgramSource = TRUE;
        Policy->HoldSharedSurfaceRundown = TRUE;
    }
    else if (WritesDestination &&
             HasDestination &&
             DestinationIsSharedPrimary)
    {
        Policy->RefreshSharedPrimary = TRUE;
        Policy->HoldSharedSurfaceRundown = TRUE;
    }

    return STATUS_SUCCESS;
}

FORCEINLINE
BOOLEAN
DxgkPresentCoreRectValid(
    _In_ const RECT *Rect)
{
    return Rect != NULL &&
           Rect->left < Rect->right &&
           Rect->top < Rect->bottom;
}

FORCEINLINE
BOOLEAN
DxgkPresentCoreRectEqual(
    _In_ const RECT *Left,
    _In_ const RECT *Right)
{
    return Left != NULL &&
           Right != NULL &&
           Left->left == Right->left &&
           Left->top == Right->top &&
           Left->right == Right->right &&
           Left->bottom == Right->bottom;
}

FORCEINLINE
BOOLEAN
DxgkPresentCoreRectContained(
    _In_ const RECT *Outer,
    _In_ const RECT *Inner)
{
    return DxgkPresentCoreRectValid(Outer) &&
           DxgkPresentCoreRectValid(Inner) &&
           Inner->left >= Outer->left &&
           Inner->top >= Outer->top &&
           Inner->right <= Outer->right &&
           Inner->bottom <= Outer->bottom;
}

FORCEINLINE
NTSTATUS
DxgkPresentCoreMapCoordinate(
    _In_ LONG SourceCoordinate,
    _In_ LONG SourceOrigin,
    _In_ ULONGLONG SourceExtent,
    _In_ LONG DestinationOrigin,
    _In_ ULONGLONG DestinationExtent,
    _Out_ PLONG DestinationCoordinate)
{
    ULONGLONG SourceOffset;
    ULONGLONG ScaledOffset;
    LONGLONG Result;

    if (DestinationCoordinate == NULL ||
        SourceCoordinate < SourceOrigin ||
        SourceExtent == 0 ||
        DestinationExtent == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SourceOffset =
        (ULONGLONG)((LONGLONG)SourceCoordinate - (LONGLONG)SourceOrigin);
    if (SourceOffset > SourceExtent ||
        (SourceOffset != 0 &&
         DestinationExtent > MAXULONGLONG / SourceOffset))
    {
        return STATUS_INVALID_PARAMETER;
    }

    ScaledOffset = SourceOffset * DestinationExtent;
    if ((ScaledOffset % SourceExtent) != 0)
        return STATUS_NOT_SUPPORTED;

    ScaledOffset /= SourceExtent;
    if (ScaledOffset > (ULONGLONG)MAXLONGLONG)
        return STATUS_INVALID_PARAMETER;
    Result = (LONGLONG)DestinationOrigin + (LONGLONG)ScaledOffset;
    if (Result < MINLONG || Result > MAXLONG)
        return STATUS_INVALID_PARAMETER;

    *DestinationCoordinate = (LONG)Result;
    return STATUS_SUCCESS;
}

/*
 * D3DKMT_PRESENT supplies source-space subrectangles, whereas
 * DXGKARG_PRESENT requires destination-space subrectangles.  Only mappings
 * whose four edges are integral can be represented exactly by RECT.
 */
FORCEINLINE
NTSTATUS
DxgkPresentCoreMapSourceSubRects(
    _In_ const RECT *SourceRect,
    _In_ const RECT *DestinationRect,
    _In_reads_(SubRectCount) const RECT *SourceSubRects,
    _In_ UINT SubRectCount,
    _Out_writes_(SubRectCapacity) RECT *DestinationSubRects,
    _In_ UINT SubRectCapacity)
{
    ULONGLONG SourceWidth;
    ULONGLONG SourceHeight;
    ULONGLONG DestinationWidth;
    ULONGLONG DestinationHeight;
    NTSTATUS Status;
    UINT Index;

    if (!DxgkPresentCoreRectValid(SourceRect) ||
        !DxgkPresentCoreRectValid(DestinationRect) ||
        SourceSubRects == NULL ||
        DestinationSubRects == NULL ||
        SubRectCount == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (SubRectCount > DXGK_PRESENT_CORE_MAX_SUBRECTS ||
        SubRectCount > SubRectCapacity)
    {
        return STATUS_NOT_SUPPORTED;
    }

    for (Index = 0; Index < SubRectCount; ++Index)
    {
        if (!DxgkPresentCoreRectContained(SourceRect,
                                         &SourceSubRects[Index]))
        {
            return STATUS_INVALID_PARAMETER;
        }
    }

    SourceWidth = (ULONGLONG)
        ((LONGLONG)SourceRect->right - (LONGLONG)SourceRect->left);
    SourceHeight = (ULONGLONG)
        ((LONGLONG)SourceRect->bottom - (LONGLONG)SourceRect->top);
    DestinationWidth = (ULONGLONG)
        ((LONGLONG)DestinationRect->right -
         (LONGLONG)DestinationRect->left);
    DestinationHeight = (ULONGLONG)
        ((LONGLONG)DestinationRect->bottom -
         (LONGLONG)DestinationRect->top);

    for (Index = 0; Index < SubRectCount; ++Index)
    {
        Status = DxgkPresentCoreMapCoordinate(
                     SourceSubRects[Index].left,
                     SourceRect->left,
                     SourceWidth,
                     DestinationRect->left,
                     DestinationWidth,
                     &DestinationSubRects[Index].left);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = DxgkPresentCoreMapCoordinate(
                     SourceSubRects[Index].top,
                     SourceRect->top,
                     SourceHeight,
                     DestinationRect->top,
                     DestinationHeight,
                     &DestinationSubRects[Index].top);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = DxgkPresentCoreMapCoordinate(
                     SourceSubRects[Index].right,
                     SourceRect->left,
                     SourceWidth,
                     DestinationRect->left,
                     DestinationWidth,
                     &DestinationSubRects[Index].right);
        if (!NT_SUCCESS(Status))
            return Status;
        Status = DxgkPresentCoreMapCoordinate(
                     SourceSubRects[Index].bottom,
                     SourceRect->top,
                     SourceHeight,
                     DestinationRect->top,
                     DestinationHeight,
                     &DestinationSubRects[Index].bottom);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

FORCEINLINE
NTSTATUS
DxgkPresentCoreCopyDestinationSubRects(
    _In_ const RECT *DestinationRect,
    _In_reads_(SubRectCount) const RECT *InputSubRects,
    _In_ UINT SubRectCount,
    _Out_writes_(SubRectCapacity) RECT *OutputSubRects,
    _In_ UINT SubRectCapacity)
{
    UINT Index;

    if (!DxgkPresentCoreRectValid(DestinationRect) ||
        InputSubRects == NULL ||
        OutputSubRects == NULL ||
        SubRectCount == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (SubRectCount > DXGK_PRESENT_CORE_MAX_SUBRECTS ||
        SubRectCount > SubRectCapacity)
    {
        return STATUS_NOT_SUPPORTED;
    }

    for (Index = 0; Index < SubRectCount; ++Index)
    {
        if (!DxgkPresentCoreRectContained(DestinationRect,
                                         &InputSubRects[Index]))
        {
            return STATUS_INVALID_PARAMETER;
        }
    }
    RtlCopyMemory(OutputSubRects,
                  InputSubRects,
                  (SIZE_T)SubRectCount * sizeof(OutputSubRects[0]));
    return STATUS_SUCCESS;
}

FORCEINLINE
BOOLEAN
DxgkPresentCoreIsFullDestinationRegion(
    _In_ const RECT *DestinationRect,
    _In_reads_opt_(SubRectCount) const RECT *DestinationSubRects,
    _In_ UINT SubRectCount)
{
    return SubRectCount == 0 ||
           (SubRectCount == 1 &&
            DxgkPresentCoreRectEqual(DestinationRect,
                                     DestinationSubRects));
}

FORCEINLINE
NTSTATUS
DxgkPresentCoreCompleteUnsupportedOverlayCreate(
    _In_ NTSTATUS DeviceValidationStatus,
    _Out_ PUINT OverlayHandle)
{
    if (OverlayHandle == NULL)
        return STATUS_INVALID_PARAMETER;

    *OverlayHandle = 0;
    return NT_SUCCESS(DeviceValidationStatus)
               ? STATUS_NOT_SUPPORTED
               : DeviceValidationStatus;
}

/*
 * The v1 MPO capability query may report TRUE only when both miniport DDIs
 * and both user-facing KMT execution paths exist.  Internal scan-out use of
 * the MPO DDI alone is not a user-mode MPO implementation.
 */
FORCEINLINE
BOOLEAN
DxgkPresentCoreMpoV1Supported(
    _In_ ULONG ConfiguredLevel,
    _In_ BOOLEAN HasMiniportCheck,
    _In_ BOOLEAN HasMiniportPresent,
    _In_ BOOLEAN HasKmtCheck,
    _In_ BOOLEAN HasKmtPresent)
{
    return ConfiguredLevel >= 1300 &&
           HasMiniportCheck &&
           HasMiniportPresent &&
           HasKmtCheck &&
           HasKmtPresent;
}
