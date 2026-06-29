/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidPN mode set structure and API tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests VidPN mode set operations including:
 *   - Source mode set structure layout
 *   - Target mode set structure layout
 *   - Monitor source mode set structure layout
 *   - Mode list retrieval and validation
 *   - Mode attribute checks (resolution, format, refresh rate)
 */

#include "precomp.h"
#include <d3dkmdt.h>

static HANDLE hDxgKrnl = INVALID_HANDLE_VALUE;

static BOOL OpenDxg(void)
{
    hDxgKrnl = OpenDxgKrnl();
    if (hDxgKrnl == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open \\Device\\DxgKrnl\n");
        return FALSE;
    }
    return TRUE;
}

static D3DKMT_HANDLE GetFirstAdapter(void)
{
    D3DKMT_ENUMADAPTERS e;
    DWORD br;
    memset(&e, 0, sizeof(e));
    if (!SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                      &e, sizeof(e), &e, sizeof(e), &br) || e.NumAdapters == 0)
        return 0;
    return e.Adapters[0].hAdapter;
}

/* ---- Source mode structure layout ---- */

static void Test_SourceModeLayout(void)
{
    D3DKMDT_VIDPN_SOURCE_MODE Mode;

    /* Id must be at offset 0 */
    ok_eq_uint(offsetof(D3DKMDT_VIDPN_SOURCE_MODE, Id), 0);

    /* Type follows Id */
    ok(offsetof(D3DKMDT_VIDPN_SOURCE_MODE, Type) > 0,
       "Type should be at positive offset\n");

    /* Format union follows Type */
    ok(offsetof(D3DKMDT_VIDPN_SOURCE_MODE, Format) > offsetof(D3DKMDT_VIDPN_SOURCE_MODE, Type),
       "Format should follow Type\n");

    /* Structure size should be non-trivial */
    ok(sizeof(D3DKMDT_VIDPN_SOURCE_MODE) > sizeof(UINT) * 2,
       "Source mode struct should be larger than just Id+Type\n");

    /* Zero-init check */
    memset(&Mode, 0, sizeof(Mode));
    ok_eq_uint(Mode.Id, 0);
    ok_eq_uint(Mode.Type, D3DKMDT_RMT_UNINITIALIZED);
}

/* ---- Target mode structure layout ---- */

static void Test_TargetModeLayout(void)
{
    D3DKMDT_VIDPN_TARGET_MODE Mode;

    /* Id must be at offset 0 */
    ok_eq_uint(offsetof(D3DKMDT_VIDPN_TARGET_MODE, Id), 0);

    /* VideoSignalInfo follows Id */
    ok(offsetof(D3DKMDT_VIDPN_TARGET_MODE, VideoSignalInfo) > 0,
       "VideoSignalInfo should be at positive offset\n");

    /* Preference must exist */
    ok(offsetof(D3DKMDT_VIDPN_TARGET_MODE, Preference) <
       sizeof(D3DKMDT_VIDPN_TARGET_MODE),
       "Preference should be within the structure\n");

    /* Zero-init check */
    memset(&Mode, 0, sizeof(Mode));
    ok_eq_uint(Mode.Id, 0);
    ok_eq_uint(Mode.Preference, D3DKMDT_MP_UNINITIALIZED);
}

/* ---- Monitor source mode structure layout ---- */

static void Test_MonitorSourceModeLayout(void)
{
    D3DKMDT_MONITOR_SOURCE_MODE Mode;

    /* Id must be at offset 0 */
    ok_eq_uint(offsetof(D3DKMDT_MONITOR_SOURCE_MODE, Id), 0);

    /* VideoSignalInfo follows */
    ok(offsetof(D3DKMDT_MONITOR_SOURCE_MODE, VideoSignalInfo) > 0,
       "VideoSignalInfo should be at positive offset\n");

    /* ColorBasis exists */
    ok(offsetof(D3DKMDT_MONITOR_SOURCE_MODE, ColorBasis) <
       sizeof(D3DKMDT_MONITOR_SOURCE_MODE),
       "ColorBasis should be within the structure\n");

    /* Origin exists */
    ok(offsetof(D3DKMDT_MONITOR_SOURCE_MODE, Origin) <
       sizeof(D3DKMDT_MONITOR_SOURCE_MODE),
       "Origin should be within the structure\n");

    /* Preference exists */
    ok(offsetof(D3DKMDT_MONITOR_SOURCE_MODE, Preference) <
       sizeof(D3DKMDT_MONITOR_SOURCE_MODE),
       "Preference should be within the structure\n");

    memset(&Mode, 0, sizeof(Mode));
    ok_eq_uint(Mode.Id, 0);
    ok_eq_uint(Mode.Preference, D3DKMDT_MP_UNINITIALIZED);
}

/* ---- Mode list content validation ---- */

static void Test_ModeListContent(void)
{
    D3DKMT_GETDISPLAYMODELIST getModes;
    D3DKMT_DISPLAYMODE modeList[64];
    DWORD br;
    BOOL r;
    D3DKMT_HANDLE hAdapter;
    UINT i;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    /* Get mode count first */
    memset(&getModes, 0, sizeof(getModes));
    getModes.hAdapter = hAdapter;
    getModes.VidPnSourceId = 0;
    getModes.pModeList = NULL;
    getModes.ModeCount = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                     &getModes, sizeof(getModes),
                     &getModes, sizeof(getModes), &br);

    if (!r || getModes.ModeCount == 0)
    {
        skip("Could not get mode count\n");
        CloseHandle(hDxgKrnl);
        return;
    }

    if (getModes.ModeCount > 64)
    {
        skip("Too many modes (%lu), capping at 64\n", getModes.ModeCount);
        getModes.ModeCount = 64;
    }

    /* Get actual mode list */
    memset(modeList, 0, sizeof(modeList));
    getModes.pModeList = modeList;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                     &getModes, sizeof(getModes),
                     &getModes, sizeof(getModes), &br);

    if (!r)
    {
        skip("GetDisplayModeList failed to fill mode list\n");
        CloseHandle(hDxgKrnl);
        return;
    }

    /* Validate each mode in the list */
    for (i = 0; i < getModes.ModeCount; i++)
    {
        /* Width and height must be positive */
        ok(modeList[i].Width > 0,
           "Mode %u: Width should be > 0, got %lu\n", i, modeList[i].Width);
        ok(modeList[i].Height > 0,
           "Mode %u: Height should be > 0, got %lu\n", i, modeList[i].Height);

        /* Width should be reasonable (64 to 7680) */
        ok(modeList[i].Width >= 64 && modeList[i].Width <= 7680,
           "Mode %u: Width %lu is outside reasonable range\n",
           i, modeList[i].Width);

        /* Height should be reasonable (64 to 4320) */
        ok(modeList[i].Height >= 64 && modeList[i].Height <= 4320,
           "Mode %u: Height %lu is outside reasonable range\n",
           i, modeList[i].Height);

        /* Format should be X8R8G8B8 for 32bpp modes */
        ok(modeList[i].Format == D3DDDIFMT_X8R8G8B8 ||
           modeList[i].Format == D3DDDIFMT_A8R8G8B8,
           "Mode %u: Expected 32bpp format, got %d\n",
           i, modeList[i].Format);

        /* Refresh rate should be positive */
        ok(modeList[i].IntegerRefreshRate > 0,
           "Mode %u: IntegerRefreshRate should be > 0, got %lu\n",
           i, modeList[i].IntegerRefreshRate);

        /* Scan line ordering should be progressive */
        ok(modeList[i].ScanLineOrdering == D3DDDI_VSSLO_PROGRESSIVE ||
           modeList[i].ScanLineOrdering == D3DDDI_VSSLO_INTERLACED_UPPERFIELDFIRST ||
           modeList[i].ScanLineOrdering == D3DDDI_VSSLO_INTERLACED_LOWERFIELDFIRST,
           "Mode %u: unexpected ScanLineOrdering %d\n",
           i, modeList[i].ScanLineOrdering);
    }

    /* The default mode list should include 1024x768 */
    {
        BOOLEAN Found1024x768 = FALSE;
        for (i = 0; i < getModes.ModeCount; i++)
        {
            if (modeList[i].Width == 1024 && modeList[i].Height == 768)
            {
                Found1024x768 = TRUE;
                break;
            }
        }
        ok(Found1024x768, "Default mode list should include 1024x768\n");
    }

    /* The default mode list should include 1920x1080 */
    {
        BOOLEAN Found1920x1080 = FALSE;
        for (i = 0; i < getModes.ModeCount; i++)
        {
            if (modeList[i].Width == 1920 && modeList[i].Height == 1080)
            {
                Found1920x1080 = TRUE;
                break;
            }
        }
        ok(Found1920x1080, "Default mode list should include 1920x1080\n");
    }

    CloseHandle(hDxgKrnl);
}

/* ---- Mode preference enum ---- */

static void Test_ModePreference(void)
{
    ok(D3DKMDT_MP_UNINITIALIZED == 0,
       "MP_UNINITIALIZED should be 0\n");
    ok(D3DKMDT_MP_PREFERRED != D3DKMDT_MP_UNINITIALIZED,
       "MP_PREFERRED must differ from UNINITIALIZED\n");
    ok(D3DKMDT_MP_NOTPREFERRED != D3DKMDT_MP_UNINITIALIZED,
       "MP_NOTPREFERRED must differ from UNINITIALIZED\n");
    ok(D3DKMDT_MP_PREFERRED != D3DKMDT_MP_NOTPREFERRED,
       "MP_PREFERRED must differ from MP_NOTPREFERRED\n");
}

/* ---- Monitor origin enum ---- */

static void Test_MonitorOrigin(void)
{
    ok(D3DKMDT_MCO_UNINITIALIZED == 0,
       "MCO_UNINITIALIZED should be 0\n");
    ok(D3DKMDT_MCO_DEFAULTMONITORPROFILE != D3DKMDT_MCO_UNINITIALIZED,
       "MCO_DEFAULTMONITORPROFILE must differ from UNINITIALIZED\n");
    ok(D3DKMDT_MCO_MONITORDESCRIPTOR != D3DKMDT_MCO_UNINITIALIZED,
       "MCO_MONITORDESCRIPTOR must differ from UNINITIALIZED\n");
    ok(D3DKMDT_MCO_DRIVER != D3DKMDT_MCO_UNINITIALIZED,
       "MCO_DRIVER must differ from UNINITIALIZED\n");
}

/* ---- Rendering mode type enum ---- */

static void Test_RenderingModeType(void)
{
    ok(D3DKMDT_RMT_UNINITIALIZED == 0,
       "RMT_UNINITIALIZED should be 0\n");
    ok(D3DKMDT_RMT_GRAPHICS == 1,
       "RMT_GRAPHICS should be 1\n");
    ok(D3DKMDT_RMT_TEXT == 2,
       "RMT_TEXT should be 2\n");
}

/* ---- D3DKMT_DISPLAYMODE structure ---- */

static void Test_DisplayModeStruct(void)
{
    ok(sizeof(D3DKMT_DISPLAYMODE) > 0,
       "D3DKMT_DISPLAYMODE should have non-zero size\n");

    ok(offsetof(D3DKMT_DISPLAYMODE, Width) == 0,
       "Width should be at offset 0\n");

    ok(offsetof(D3DKMT_DISPLAYMODE, Height) > 0,
       "Height should be at positive offset\n");

    ok(offsetof(D3DKMT_DISPLAYMODE, Format) > offsetof(D3DKMT_DISPLAYMODE, Height),
       "Format should follow Height\n");
}

START_TEST(VidPnModeSet)
{
    Test_SourceModeLayout();
    Test_TargetModeLayout();
    Test_MonitorSourceModeLayout();
    Test_ModePreference();
    Test_MonitorOrigin();
    Test_RenderingModeType();
    Test_DisplayModeStruct();
    Test_ModeListContent();
}
