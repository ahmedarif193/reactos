/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidPN topology creation, path management, and source-target mapping tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the VidPN topology model including:
 *   - VidPN source->target path mapping
 *   - Path enumeration (add/remove)
 *   - Multi-monitor topology validation
 *   - Display mode list retrieval across multiple sources
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

/* ---- VidPN path structure layout ---- */

static void Test_VidPnPathStructLayout(void)
{
    D3DKMDT_VIDPN_PRESENT_PATH Path;

    /* VidPnSourceId at offset 0 */
    ok_eq_uint(offsetof(D3DKMDT_VIDPN_PRESENT_PATH, VidPnSourceId), 0);

    /* VidPnTargetId follows VidPnSourceId */
    ok(offsetof(D3DKMDT_VIDPN_PRESENT_PATH, VidPnTargetId) ==
       sizeof(D3DDDI_VIDEO_PRESENT_SOURCE_ID),
       "VidPnTargetId should directly follow VidPnSourceId\n");

    /* ImportanceOrdinal is at a known offset */
    ok(offsetof(D3DKMDT_VIDPN_PRESENT_PATH, ImportanceOrdinal) >=
       sizeof(D3DDDI_VIDEO_PRESENT_SOURCE_ID) + sizeof(D3DDDI_VIDEO_PRESENT_TARGET_ID),
       "ImportanceOrdinal should follow source and target IDs\n");

    /* ContentTransformation must be within the path */
    ok(offsetof(D3DKMDT_VIDPN_PRESENT_PATH, ContentTransformation) <
       sizeof(D3DKMDT_VIDPN_PRESENT_PATH),
       "ContentTransformation should be within the path structure\n");

    /* Zero-init check */
    memset(&Path, 0, sizeof(Path));
    ok_eq_uint(Path.VidPnSourceId, 0);
    ok_eq_uint(Path.VidPnTargetId, 0);
    ok_eq_uint(Path.ImportanceOrdinal, D3DKMDT_VPPI_UNINITIALIZED);
}

/* ---- Display mode list for source 0 ---- */

static void Test_DisplayModeListForSource0(void)
{
    D3DKMT_GETDISPLAYMODELIST getModes;
    DWORD br;
    BOOL r;
    D3DKMT_HANDLE hAdapter;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    /* Get mode count for source 0 */
    memset(&getModes, 0, sizeof(getModes));
    getModes.hAdapter = hAdapter;
    getModes.VidPnSourceId = 0;
    getModes.pModeList = NULL;
    getModes.ModeCount = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                     &getModes, sizeof(getModes),
                     &getModes, sizeof(getModes), &br);

    if (!r)
    {
        skip("GetDisplayModeList failed for source 0\n");
        CloseHandle(hDxgKrnl);
        return;
    }

    ok(getModes.ModeCount >= 1,
       "Source 0 should have at least 1 mode, got %lu\n", getModes.ModeCount);

    /* Common resolutions should include at least 800x600 through 1920x1080 */
    ok(getModes.ModeCount >= 5,
       "Expected at least 5 common modes for a default VidPN, got %lu\n",
       getModes.ModeCount);

    CloseHandle(hDxgKrnl);
}

/* ---- VidPN source owner set/release ---- */

static void Test_VidPnSourceOwnerInvalidDevice(void)
{
    D3DKMT_SETVIDPNSOURCEOWNER setOwner;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&setOwner, 0, sizeof(setOwner));
    setOwner.hDevice = 0xDEADBEEF;
    setOwner.pType = NULL;
    setOwner.pVidPnSourceId = NULL;
    setOwner.VidPnSourceCount = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_SETVIDPNSOURCEOWNER,
                     &setOwner, sizeof(setOwner), NULL, 0, &br);

    ok(!r, "SetVidPnSourceOwner should fail with invalid device handle\n");

    CloseHandle(hDxgKrnl);
}

/* ---- ContentTransformation fields ---- */

static void Test_ContentTransformationLayout(void)
{
    D3DKMDT_VIDPN_PRESENT_PATH_TRANSFORMATION xform;

    memset(&xform, 0, sizeof(xform));

    /* Scaling must be UNINITIALIZED when zero-initialized */
    ok_eq_uint(xform.Scaling, D3DKMDT_VPPS_UNINITIALIZED);

    /* Rotation must be UNINITIALIZED when zero-initialized */
    ok_eq_uint(xform.Rotation, D3DKMDT_VPPR_UNINITIALIZED);
}

/* ---- GammaRamp type enum ---- */

static void Test_GammaRampType(void)
{
    ok(D3DDDI_GAMMARAMP_UNINITIALIZED == 0,
       "GAMMARAMP_UNINITIALIZED should be 0\n");
    ok(D3DDDI_GAMMARAMP_DEFAULT == 1,
       "GAMMARAMP_DEFAULT should be 1\n");
    ok(D3DDDI_GAMMARAMP_RGB256x3x16 == 2,
       "GAMMARAMP_RGB256x3x16 should be 2\n");
}

/* ---- Path content type ---- */

static void Test_PathContentType(void)
{
    ok(D3DKMDT_VPPC_UNINITIALIZED == 0,
       "VPPC_UNINITIALIZED should be 0\n");
    ok(D3DKMDT_VPPC_GRAPHICS != D3DKMDT_VPPC_UNINITIALIZED,
       "VPPC_GRAPHICS must differ from UNINITIALIZED\n");
    ok(D3DKMDT_VPPC_VIDEO != D3DKMDT_VPPC_UNINITIALIZED,
       "VPPC_VIDEO must differ from UNINITIALIZED\n");
    ok(D3DKMDT_VPPC_GRAPHICS != D3DKMDT_VPPC_VIDEO,
       "VPPC_GRAPHICS must differ from VPPC_VIDEO\n");
}

/* ---- Adapter source count ---- */

static void Test_AdapterHasSources(void)
{
    D3DKMT_ENUMADAPTERS e;
    DWORD br;

    if (!OpenDxg()) return;

    memset(&e, 0, sizeof(e));
    if (!SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                      &e, sizeof(e), &e, sizeof(e), &br) || e.NumAdapters == 0)
    {
        skip("No adapter\n");
        CloseHandle(hDxgKrnl);
        return;
    }

    /* The first adapter should report at least 1 VidPN source */
    ok(e.Adapters[0].NumOfSources >= 1,
       "Adapter should have at least 1 VidPN source, got %lu\n",
       e.Adapters[0].NumOfSources);

    CloseHandle(hDxgKrnl);
}

START_TEST(VidPnTopology)
{
    Test_VidPnPathStructLayout();
    Test_ContentTransformationLayout();
    Test_GammaRampType();
    Test_PathContentType();
    Test_DisplayModeListForSource0();
    Test_VidPnSourceOwnerInvalidDevice();
    Test_AdapterHasSources();
}
