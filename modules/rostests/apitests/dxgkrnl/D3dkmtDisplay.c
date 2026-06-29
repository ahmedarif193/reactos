/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT display mode enumeration and set tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "precomp.h"

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

static void Test_GetDisplayModeListStructLayout(void)
{
    /* D3DKMT_GETDISPLAYMODELIST */
    ok_eq_uint(offsetof(D3DKMT_GETDISPLAYMODELIST, hAdapter), 0);
    ok(sizeof(D3DKMT_GETDISPLAYMODELIST) > sizeof(D3DKMT_HANDLE),
       "GETDISPLAYMODELIST must contain more than just a handle\n");

    /* D3DKMT_SETDISPLAYMODE */
    ok_eq_uint(offsetof(D3DKMT_SETDISPLAYMODE, hDevice), 0);
}

static void Test_GetDisplayModeList(void)
{
    D3DKMT_GETDISPLAYMODELIST getModes;
    DWORD br;
    BOOL r;
    D3DKMT_HANDLE hAdapter;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    /* First call with NULL mode list to get count */
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
        skip("GetDisplayModeList failed (%lu)\n", GetLastError());
        CloseHandle(hDxgKrnl);
        return;
    }

    /* Should report at least 1 mode */
    ok(getModes.ModeCount >= 1,
       "Expected at least 1 display mode, got %lu\n", getModes.ModeCount);

    /* Mode count should be reasonable (< 256 for most monitors) */
    ok(getModes.ModeCount <= 256,
       "Unusually high mode count: %lu\n", getModes.ModeCount);

    CloseHandle(hDxgKrnl);
}

static void Test_GetDisplayModeListInvalidAdapter(void)
{
    D3DKMT_GETDISPLAYMODELIST getModes;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&getModes, 0, sizeof(getModes));
    getModes.hAdapter = 0xDEADBEEF;
    getModes.VidPnSourceId = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                     &getModes, sizeof(getModes),
                     &getModes, sizeof(getModes), &br);

    ok(!r, "GetDisplayModeList should fail with invalid adapter\n");

    CloseHandle(hDxgKrnl);
}

static void Test_GetDisplayModeListInvalidSource(void)
{
    D3DKMT_GETDISPLAYMODELIST getModes;
    DWORD br;
    BOOL r;
    D3DKMT_HANDLE hAdapter;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    memset(&getModes, 0, sizeof(getModes));
    getModes.hAdapter = hAdapter;
    getModes.VidPnSourceId = 0xFFFF; /* Invalid source ID */

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                     &getModes, sizeof(getModes),
                     &getModes, sizeof(getModes), &br);

    /* Should fail or return 0 modes for invalid source */
    ok(!r || getModes.ModeCount == 0,
       "GetDisplayModeList should fail or return 0 modes for invalid source\n");

    CloseHandle(hDxgKrnl);
}

static void Test_SetDisplayModeInvalidDevice(void)
{
    D3DKMT_SETDISPLAYMODE setMode;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&setMode, 0, sizeof(setMode));
    setMode.hDevice = 0xBAD0CAFE;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_SETDISPLAYMODE,
                     &setMode, sizeof(setMode), NULL, 0, &br);

    ok(!r, "SetDisplayMode should fail with invalid device\n");

    CloseHandle(hDxgKrnl);
}

static void Test_DisplayIoctlCodes(void)
{
    ok(IOCTL_D3DKMT_GETDISPLAYMODELIST != IOCTL_D3DKMT_SETDISPLAYMODE,
       "Get and Set display mode IOCTLs must differ\n");

    ok_eq_uint((IOCTL_D3DKMT_GETDISPLAYMODELIST >> 2) & 0xFFF, 0x105);
    ok_eq_uint((IOCTL_D3DKMT_SETDISPLAYMODE >> 2) & 0xFFF, 0x160);
}

static void Test_SetVidPnSourceOwnerIoctlCode(void)
{
    ok(IOCTL_D3DKMT_SETVIDPNSOURCEOWNER != IOCTL_D3DKMT_SETDISPLAYMODE,
       "SetVidPnSourceOwner and SetDisplayMode IOCTLs must differ\n");
    ok(IOCTL_D3DKMT_SETVIDPNSOURCEOWNER != IOCTL_D3DKMT_GETDISPLAYMODELIST,
       "SetVidPnSourceOwner and GetDisplayModeList IOCTLs must differ\n");
}

static void Test_GetDisplayModeListTwoPass(void)
{
    D3DKMT_GETDISPLAYMODELIST getModes;
    DWORD br;
    BOOL r;
    D3DKMT_HANDLE hAdapter;
    UINT firstCount;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    /* Pass 1: get count */
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
        skip("GetDisplayModeList pass 1 failed\n");
        CloseHandle(hDxgKrnl);
        return;
    }

    firstCount = getModes.ModeCount;
    ok(firstCount > 0, "Pass 1 should return non-zero mode count\n");

    /* Pass 2: verify count is consistent */
    getModes.pModeList = NULL;
    getModes.ModeCount = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                     &getModes, sizeof(getModes),
                     &getModes, sizeof(getModes), &br);

    if (r)
    {
        ok_eq_uint(getModes.ModeCount, firstCount);
    }

    CloseHandle(hDxgKrnl);
}

START_TEST(D3dkmtDisplay)
{
    Test_GetDisplayModeListStructLayout();
    Test_GetDisplayModeList();
    Test_GetDisplayModeListInvalidAdapter();
    Test_GetDisplayModeListInvalidSource();
    Test_SetDisplayModeInvalidDevice();
    Test_DisplayIoctlCodes();
    Test_SetVidPnSourceOwnerIoctlCode();
    Test_GetDisplayModeListTwoPass();
}
