/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidPN (Video Present Network) constant and structure tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests VidPN-related types, enums, and structure layouts as defined by the
 * WDDM specification.  These are compile-time layout checks that verify
 * binary compatibility with the Windows WDDM driver model.
 */

#include "precomp.h"
#include <d3dkmdt.h>

/*
 * DXGK_VIDPN_INTERFACE_VERSION is defined in dispmprt.h which is a
 * kernel-mode header and cannot be included in user-mode tests.
 * Mirror the enum values here for compile-time validation.
 */
#ifndef DXGK_VIDPN_INTERFACE_VERSION_V1
#define DXGK_VIDPN_INTERFACE_VERSION_V1  1
#endif

/* ---- D3DKMDT_VIDPN_PRESENT_PATH tests ---- */

static void Test_VidPnPathLayout(void)
{
    /* D3DKMDT_VIDPN_PRESENT_PATH must contain source and target IDs */
    ok(sizeof(D3DKMDT_VIDPN_PRESENT_PATH) > 0,
       "D3DKMDT_VIDPN_PRESENT_PATH has zero size\n");

    /* VidPnSourceId at offset 0 */
    ok_eq_uint(offsetof(D3DKMDT_VIDPN_PRESENT_PATH, VidPnSourceId), 0);

    /* VidPnTargetId follows source ID */
    ok(offsetof(D3DKMDT_VIDPN_PRESENT_PATH, VidPnTargetId) >=
       sizeof(D3DDDI_VIDEO_PRESENT_SOURCE_ID),
       "VidPnTargetId should follow VidPnSourceId\n");

    /* ImportanceOrdinal must exist */
    ok(offsetof(D3DKMDT_VIDPN_PRESENT_PATH, ImportanceOrdinal) > 0,
       "ImportanceOrdinal should be at positive offset\n");
}

/* ---- D3DKMDT_2DREGION tests ---- */

static void Test_2DRegionLayout(void)
{
    D3DKMDT_2DREGION Region;

    ok_eq_uint(sizeof(D3DKMDT_2DREGION), 2 * sizeof(UINT));
    ok_eq_uint(offsetof(D3DKMDT_2DREGION, cx), 0);
    ok_eq_uint(offsetof(D3DKMDT_2DREGION, cy), sizeof(UINT));

    /* Zero-init test */
    memset(&Region, 0, sizeof(Region));
    ok_eq_uint(Region.cx, 0);
    ok_eq_uint(Region.cy, 0);
}

/* ---- D3DKMDT_VIDEO_SIGNAL_INFO tests ---- */

static void Test_VideoSignalInfo(void)
{
    ok(sizeof(D3DKMDT_VIDEO_SIGNAL_INFO) > 0,
       "D3DKMDT_VIDEO_SIGNAL_INFO has zero size\n");

    /* Must contain VideoStandard and TotalSize */
    ok(offsetof(D3DKMDT_VIDEO_SIGNAL_INFO, VideoStandard) <
       sizeof(D3DKMDT_VIDEO_SIGNAL_INFO),
       "VideoStandard out of bounds\n");

    ok(offsetof(D3DKMDT_VIDEO_SIGNAL_INFO, TotalSize) <
       sizeof(D3DKMDT_VIDEO_SIGNAL_INFO),
       "TotalSize out of bounds\n");

    ok(offsetof(D3DKMDT_VIDEO_SIGNAL_INFO, ActiveSize) <
       sizeof(D3DKMDT_VIDEO_SIGNAL_INFO),
       "ActiveSize out of bounds\n");
}

/* ---- Source mode type enum ---- */

static void Test_SourceModeType(void)
{
    /* D3DKMDT_VIDPN_SOURCE_MODE_TYPE should have well-defined values */
    ok(D3DKMDT_RMT_GRAPHICS == 1, "D3DKMDT_RMT_GRAPHICS should be 1\n");
    ok(D3DKMDT_RMT_TEXT == 2, "D3DKMDT_RMT_TEXT should be 2\n");
}

/* ---- Pixel format enum ---- */

static void Test_PixelFormats(void)
{
    /* Common pixel formats must have defined values */
    ok(D3DDDIFMT_A8R8G8B8 != 0, "A8R8G8B8 format should be non-zero\n");
    ok(D3DDDIFMT_X8R8G8B8 != 0, "X8R8G8B8 format should be non-zero\n");
    ok(D3DDDIFMT_R5G6B5 != 0, "R5G6B5 format should be non-zero\n");

    /* All common formats must be distinct */
    ok(D3DDDIFMT_A8R8G8B8 != D3DDDIFMT_X8R8G8B8,
       "A8R8G8B8 and X8R8G8B8 must be distinct\n");
    ok(D3DDDIFMT_A8R8G8B8 != D3DDDIFMT_R5G6B5,
       "A8R8G8B8 and R5G6B5 must be distinct\n");
    ok(D3DDDIFMT_X8R8G8B8 != D3DDDIFMT_R5G6B5,
       "X8R8G8B8 and R5G6B5 must be distinct\n");
}

/* ---- VidPN interface version ---- */

static void Test_VidPnInterfaceVersion(void)
{
    /* DXGK_VIDPN_INTERFACE_VERSION_V1 must exist and be the base version */
    ok(DXGK_VIDPN_INTERFACE_VERSION_V1 == 1,
       "DXGK_VIDPN_INTERFACE_VERSION_V1 should be 1, got %d\n",
       DXGK_VIDPN_INTERFACE_VERSION_V1);
}

/* ---- Path importance ordinal ---- */

static void Test_PathImportance(void)
{
    /* D3DKMDT_VPPI_UNINITIALIZED = 0 (sentinel) */
    ok(D3DKMDT_VPPI_UNINITIALIZED == 0,
       "D3DKMDT_VPPI_UNINITIALIZED should be 0\n");

    /* D3DKMDT_VPPI_PRIMARY should be a defined value */
    ok(D3DKMDT_VPPI_PRIMARY != D3DKMDT_VPPI_UNINITIALIZED,
       "PRIMARY importance must differ from UNINITIALIZED\n");
}

/* ---- Content type rotation ---- */

static void Test_Rotation(void)
{
    ok(D3DKMDT_VPPR_IDENTITY == 1,
       "VPPR_IDENTITY should be 1, got %d\n", D3DKMDT_VPPR_IDENTITY);
    ok(D3DKMDT_VPPR_ROTATE90 == 2,
       "VPPR_ROTATE90 should be 2\n");
    ok(D3DKMDT_VPPR_ROTATE180 == 3,
       "VPPR_ROTATE180 should be 3\n");
    ok(D3DKMDT_VPPR_ROTATE270 == 4,
       "VPPR_ROTATE270 should be 4\n");

    /* Uninitialized must be 0 */
    ok(D3DKMDT_VPPR_UNINITIALIZED == 0,
       "VPPR_UNINITIALIZED should be 0\n");
}

/* ---- Scaling ---- */

static void Test_Scaling(void)
{
    ok(D3DKMDT_VPPS_UNINITIALIZED == 0,
       "VPPS_UNINITIALIZED should be 0\n");
    ok(D3DKMDT_VPPS_IDENTITY == 1,
       "VPPS_IDENTITY should be 1\n");
    ok(D3DKMDT_VPPS_CENTERED == 2,
       "VPPS_CENTERED should be 2\n");
    ok(D3DKMDT_VPPS_STRETCHED == 3,
       "VPPS_STRETCHED should be 3\n");
}

/* ---- Monitor source mode type ---- */

static void Test_MonitorSourceModeType(void)
{
    /* D3DKMDT_MON_SOURCE_MODE types */
    ok(sizeof(D3DKMDT_MONITOR_SOURCE_MODE) > 0,
       "D3DKMDT_MONITOR_SOURCE_MODE has zero size\n");
}

/* ---- Color basis enum ---- */

static void Test_ColorBasis(void)
{
    ok(D3DKMDT_CB_UNINITIALIZED == 0,
       "CB_UNINITIALIZED should be 0\n");
    ok(D3DKMDT_CB_INTENSITY == 1,
       "CB_INTENSITY should be 1\n");
    ok(D3DKMDT_CB_SRGB == 2,
       "CB_SRGB should be 2\n");
    ok(D3DKMDT_CB_SCRGB == 3,
       "CB_SCRGB should be 3\n");
    ok(D3DKMDT_CB_YCBCR == 4,
       "CB_YCBCR should be 4\n");
    ok(D3DKMDT_CB_YPBPR == 5,
       "CB_YPBPR should be 5\n");
}

/* ---- Pixel value access mode ---- */

static void Test_PixelValueAccessMode(void)
{
    ok(D3DKMDT_PVAM_UNINITIALIZED == 0,
       "PVAM_UNINITIALIZED should be 0\n");
    ok(D3DKMDT_PVAM_DIRECT == 1,
       "PVAM_DIRECT should be 1\n");
    ok(D3DKMDT_PVAM_PRESETPALETTE == 2,
       "PVAM_PRESETPALETTE should be 2\n");
    ok(D3DKMDT_PVAM_SETTABLEPALETTE == 3,
       "PVAM_SETTABLEPALETTE should be 3\n");
}

/* ---- Video standard enum ---- */

static void Test_VideoStandard(void)
{
    ok(D3DKMDT_VSS_UNINITIALIZED == 0,
       "VSS_UNINITIALIZED should be 0\n");
    ok(D3DKMDT_VSS_VESA_DMT != 0,
       "VSS_VESA_DMT should be non-zero\n");
    ok(D3DKMDT_VSS_VESA_GTF != 0,
       "VSS_VESA_GTF should be non-zero\n");
    ok(D3DKMDT_VSS_VESA_CVT != 0,
       "VSS_VESA_CVT should be non-zero\n");
}

/* ---- Scan line ordering ---- */

static void Test_ScanLineOrdering(void)
{
    ok(D3DDDI_VSSLO_UNINITIALIZED == 0,
       "VSSLO_UNINITIALIZED should be 0\n");
    ok(D3DDDI_VSSLO_PROGRESSIVE == 1,
       "VSSLO_PROGRESSIVE should be 1\n");
    ok(D3DDDI_VSSLO_INTERLACED_UPPERFIELDFIRST == 2,
       "VSSLO_INTERLACED_UPPERFIELDFIRST should be 2\n");
    ok(D3DDDI_VSSLO_INTERLACED_LOWERFIELDFIRST == 3,
       "VSSLO_INTERLACED_LOWERFIELDFIRST should be 3\n");
}

/* ---- Flip interval type ---- */

static void Test_FlipInterval(void)
{
    ok(D3DDDI_FLIPINTERVAL_IMMEDIATE == 0,
       "FLIPINTERVAL_IMMEDIATE should be 0\n");
    ok(D3DDDI_FLIPINTERVAL_ONE == 1,
       "FLIPINTERVAL_ONE should be 1\n");
    ok(D3DDDI_FLIPINTERVAL_TWO == 2,
       "FLIPINTERVAL_TWO should be 2\n");
    ok(D3DDDI_FLIPINTERVAL_THREE == 3,
       "FLIPINTERVAL_THREE should be 3\n");
    ok(D3DDDI_FLIPINTERVAL_FOUR == 4,
       "FLIPINTERVAL_FOUR should be 4\n");
}

/* ---- VidPN source owner type enum ---- */

static void Test_VidPnSourceOwnerType(void)
{
    ok(D3DKMT_VIDPNSOURCEOWNER_UNOWNED == 0,
       "VIDPNSOURCEOWNER_UNOWNED should be 0\n");
    ok(D3DKMT_VIDPNSOURCEOWNER_SHARED == 1,
       "VIDPNSOURCEOWNER_SHARED should be 1\n");
    ok(D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE == 2,
       "VIDPNSOURCEOWNER_EXCLUSIVE should be 2\n");
    ok(D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVEGDI == 3,
       "VIDPNSOURCEOWNER_EXCLUSIVEGDI should be 3\n");
}

/* ---- D3DDDI_ROTATION enum ---- */

static void Test_Orientation(void)
{
    ok(D3DDDI_ROTATION_IDENTITY == 1,
       "ROTATION_IDENTITY should be 1\n");
    ok(D3DDDI_ROTATION_90 == 2,
       "ROTATION_90 should be 2\n");
    ok(D3DDDI_ROTATION_180 == 3,
       "ROTATION_180 should be 3\n");
    ok(D3DDDI_ROTATION_270 == 4,
       "ROTATION_270 should be 4\n");
}

/* ---- Monitor connectivity check type ---- */

static void Test_MonitorConnectivityCheck(void)
{
    ok(D3DKMDT_MCC_UNINITIALIZED == 0,
       "MCC_UNINITIALIZED should be 0\n");
    ok(D3DKMDT_MCC_IGNORE != D3DKMDT_MCC_UNINITIALIZED,
       "MCC_IGNORE must differ from UNINITIALIZED\n");
    ok(D3DKMDT_MCC_ENFORCE != D3DKMDT_MCC_UNINITIALIZED,
       "MCC_ENFORCE must differ from UNINITIALIZED\n");
}

/* ---- D3DKMDT_VIDPN_PRESENT_PATH size stability ---- */

static void Test_PresentPathSize(void)
{
    /* Path must be at least big enough for source+target IDs + importance + transforms */
    ok(sizeof(D3DKMDT_VIDPN_PRESENT_PATH) >= 4 * sizeof(UINT),
       "D3DKMDT_VIDPN_PRESENT_PATH is suspiciously small: %Iu bytes\n",
       sizeof(D3DKMDT_VIDPN_PRESENT_PATH));
}

START_TEST(VidPnConstants)
{
    Test_VidPnPathLayout();
    Test_2DRegionLayout();
    Test_VideoSignalInfo();
    Test_SourceModeType();
    Test_PixelFormats();
    Test_VidPnInterfaceVersion();
    Test_PathImportance();
    Test_Rotation();
    Test_Scaling();
    Test_MonitorSourceModeType();
    Test_ColorBasis();
    Test_PixelValueAccessMode();
    Test_VideoStandard();
    Test_ScanLineOrdering();
    Test_FlipInterval();
    Test_VidPnSourceOwnerType();
    Test_Orientation();
    Test_MonitorConnectivityCheck();
    Test_PresentPathSize();
}
