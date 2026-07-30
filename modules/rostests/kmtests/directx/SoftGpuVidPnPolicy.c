/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     SoftGPU VidPN transformation and pivot policy tests
 */

#include <kmt_test.h>
#include "vidpn_policy_core.h"

static VOID
CheckPolicy(
    _In_ D3DKMDT_ENUMCOFUNCMODALITY_PIVOT_TYPE PivotType,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID PivotSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID PivotTargetId,
    _In_ D3DDDI_VIDEO_PRESENT_SOURCE_ID PathSourceId,
    _In_ D3DDDI_VIDEO_PRESENT_TARGET_ID PathTargetId,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_SCALING Scaling,
    _In_ D3DKMDT_VIDPN_PRESENT_PATH_ROTATION Rotation,
    _In_ BOOLEAN UpdateScalingSupport,
    _In_ BOOLEAN UpdateRotationSupport)
{
    SOFTGPU_VIDPN_TRANSFORM_POLICY Policy;

    RtlZeroMemory(&Policy, sizeof(Policy));
    SoftGpuVidPnEvaluateTransformPolicy(
        PivotType,
        PivotSourceId,
        PivotTargetId,
        PathSourceId,
        PathTargetId,
        Scaling,
        Rotation,
        &Policy);

    ok_eq_bool(Policy.UpdateScalingSupport, UpdateScalingSupport);
    ok_eq_bool(Policy.UpdateRotationSupport, UpdateRotationSupport);
}

START_TEST(SoftGpuVidPnPolicy)
{
    static CONST D3DKMDT_VIDPN_PRESENT_PATH_SCALING PinnedScaling[] =
    {
        D3DKMDT_VPPS_IDENTITY,
        D3DKMDT_VPPS_CENTERED,
        D3DKMDT_VPPS_STRETCHED,
        D3DKMDT_VPPS_ASPECTRATIOCENTEREDMAX,
        D3DKMDT_VPPS_CUSTOM
    };
    static CONST D3DKMDT_VIDPN_PRESENT_PATH_ROTATION PinnedRotation[] =
    {
        D3DKMDT_VPPR_IDENTITY,
        D3DKMDT_VPPR_ROTATE90,
        D3DKMDT_VPPR_ROTATE180,
        D3DKMDT_VPPR_ROTATE270
    };
    ULONG Index;

    for (Index = 0; Index < RTL_NUMBER_OF(PinnedScaling); ++Index)
    {
        ok_bool_true(
            SoftGpuVidPnScalingIsPinned(PinnedScaling[Index]),
            "concrete scaling must be pinned");
    }
    ok_bool_false(
        SoftGpuVidPnScalingIsPinned(D3DKMDT_VPPS_UNINITIALIZED),
        "uninitialized scaling");
    ok_bool_false(
        SoftGpuVidPnScalingIsPinned(D3DKMDT_VPPS_UNPINNED),
        "unpinned scaling");
    ok_bool_false(
        SoftGpuVidPnScalingIsPinned(D3DKMDT_VPPS_NOTSPECIFIED),
        "unspecified scaling");

    for (Index = 0; Index < RTL_NUMBER_OF(PinnedRotation); ++Index)
    {
        ok_bool_true(
            SoftGpuVidPnRotationIsPinned(PinnedRotation[Index]),
            "concrete rotation must be pinned");
    }
    ok_bool_false(
        SoftGpuVidPnRotationIsPinned(D3DKMDT_VPPR_UNINITIALIZED),
        "uninitialized rotation");
    ok_bool_false(
        SoftGpuVidPnRotationIsPinned(D3DKMDT_VPPR_UNPINNED),
        "unpinned rotation");
    ok_bool_false(
        SoftGpuVidPnRotationIsPinned(D3DKMDT_VPPR_NOTSPECIFIED),
        "unspecified rotation");

    /* With no pivot, both unpinned support sets must be rebuilt. */
    CheckPolicy(
        D3DKMDT_EPT_NOPIVOT,
        0,
        0,
        0,
        0,
        D3DKMDT_VPPS_UNPINNED,
        D3DKMDT_VPPR_UNPINNED,
        TRUE,
        TRUE);

    /* A matching transform pivot suppresses only its own support field. */
    CheckPolicy(
        D3DKMDT_EPT_SCALING,
        0,
        0,
        0,
        0,
        D3DKMDT_VPPS_UNPINNED,
        D3DKMDT_VPPR_UNPINNED,
        FALSE,
        TRUE);
    CheckPolicy(
        D3DKMDT_EPT_ROTATION,
        0,
        0,
        0,
        0,
        D3DKMDT_VPPS_UNPINNED,
        D3DKMDT_VPPR_UNPINNED,
        TRUE,
        FALSE);

    /* A pivot for another path or a mode-set pivot changes neither rule. */
    CheckPolicy(
        D3DKMDT_EPT_SCALING,
        1,
        0,
        0,
        0,
        D3DKMDT_VPPS_UNPINNED,
        D3DKMDT_VPPR_UNPINNED,
        TRUE,
        TRUE);
    CheckPolicy(
        D3DKMDT_EPT_VIDPNSOURCE,
        0,
        0,
        0,
        0,
        D3DKMDT_VPPS_UNPINNED,
        D3DKMDT_VPPR_UNPINNED,
        TRUE,
        TRUE);

    /* Concrete transforms are pinned and their support is left unchanged. */
    CheckPolicy(
        D3DKMDT_EPT_NOPIVOT,
        0,
        0,
        0,
        0,
        D3DKMDT_VPPS_IDENTITY,
        D3DKMDT_VPPR_IDENTITY,
        FALSE,
        FALSE);

    ok_bool_true(
        SoftGpuVidPnActiveTransformSupported(
            D3DKMDT_VPPS_IDENTITY,
            D3DKMDT_VPPR_IDENTITY),
        "identity-only active path");
    ok_bool_false(
        SoftGpuVidPnActiveTransformSupported(
            D3DKMDT_VPPS_UNPINNED,
            D3DKMDT_VPPR_IDENTITY),
        "active scaling must be pinned");
    ok_bool_false(
        SoftGpuVidPnActiveTransformSupported(
            D3DKMDT_VPPS_CENTERED,
            D3DKMDT_VPPR_IDENTITY),
        "centered scaling is not implemented");
    ok_bool_false(
        SoftGpuVidPnActiveTransformSupported(
            D3DKMDT_VPPS_IDENTITY,
            D3DKMDT_VPPR_ROTATE90),
        "rotation is not implemented");
}

/* EOF */
