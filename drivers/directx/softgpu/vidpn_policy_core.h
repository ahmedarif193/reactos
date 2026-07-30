/*
 * PROJECT:     ReactOS WDDM Software GPU Miniport
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Cofunctional VidPN transformation policy
 */

#ifndef _SOFTGPU_VIDPN_POLICY_CORE_H_
#define _SOFTGPU_VIDPN_POLICY_CORE_H_

#include <ntddk.h>
#include <windef.h>
#include <d3dkmddi.h>

typedef struct _SOFTGPU_VIDPN_TRANSFORM_POLICY
{
    BOOLEAN UpdateScalingSupport;
    BOOLEAN UpdateRotationSupport;
} SOFTGPU_VIDPN_TRANSFORM_POLICY, *PSOFTGPU_VIDPN_TRANSFORM_POLICY;

FORCEINLINE
BOOLEAN
SoftGpuVidPnScalingIsPinned(
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling)
{
    return Scaling == D3DKMDT_VPPS_IDENTITY ||
           Scaling == D3DKMDT_VPPS_CENTERED ||
           Scaling == D3DKMDT_VPPS_STRETCHED ||
           Scaling == D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX ||
           Scaling == D3DKMDT_VPPS_CUSTOM;
}

FORCEINLINE
BOOLEAN
SoftGpuVidPnRotationIsPinned(
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
{
    return Rotation == D3DKMDT_VPPR_IDENTITY ||
           Rotation == D3DKMDT_VPPR_ROTATE90 ||
           Rotation == D3DKMDT_VPPR_ROTATE180 ||
           Rotation == D3DKMDT_VPPR_ROTATE270;
}

FORCEINLINE
VOID
SoftGpuVidPnEvaluateTransformPolicy(
    _In_ D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE PivotType,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID PivotSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID PivotTargetId,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID PathSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID PathTargetId,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
    _Out_ PSOFTGPU_VIDPN_TRANSFORM_POLICY Policy)
{
    BOOLEAN PivotMatchesPath;

    PivotMatchesPath =
        PivotSourceId == PathSourceId &&
        PivotTargetId == PathTargetId;

    Policy->UpdateScalingSupport =
        !SoftGpuVidPnScalingIsPinned(Scaling) &&
        !(PivotType == D3DKMDT_EPT_SCALING && PivotMatchesPath);
    Policy->UpdateRotationSupport =
        !SoftGpuVidPnRotationIsPinned(Rotation) &&
        !(PivotType == D3DKMDT_EPT_ROTATION && PivotMatchesPath);
}

FORCEINLINE
BOOLEAN
SoftGpuVidPnActiveTransformSupported(
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation)
{
    return Scaling == D3DKMDT_VPPS_IDENTITY &&
           Rotation == D3DKMDT_VPPR_IDENTITY;
}

#endif /* _SOFTGPU_VIDPN_POLICY_CORE_H_ */
