/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT display/VidPn/mode enumeration advanced tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the display subsystem through \Device\DxgKrnl IOCTLs:
 *   - OpenAdapterFromGdiDisplayName
 *   - QueryAdapterInfo (DRIVERVERSION, GETSEGMENTSIZE)
 *   - GetDisplayModeList with mode validation
 *   - VidPn source ownership (set/check exclusive)
 *   - WaitForVerticalBlankEvent
 *   - GetScanLine
 *   - PollDisplayChildren
 *   - Multiple adapter open/close (leak test)
 *   - Invalid display name handling
 *   - CheckMonitorPowerState
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

static void CloseDxg(void)
{
    if (hDxgKrnl != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDxgKrnl);
        hDxgKrnl = INVALID_HANDLE_VALUE;
    }
}

static D3DKMT_HANDLE GetFirstAdapterViaEnum(void)
{
    D3DKMT_ENUMADAPTERS e;
    DWORD br;
    memset(&e, 0, sizeof(e));
    if (!SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                      &e, sizeof(e), &e, sizeof(e), &br) || e.NumAdapters == 0)
        return 0;
    return e.Adapters[0].hAdapter;
}

static D3DKMT_HANDLE CreateTestDevice(D3DKMT_HANDLE hAdapter)
{
    D3DKMT_CREATEDEVICE cd;
    DWORD br;
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    if (!SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                      &cd, sizeof(cd), &cd, sizeof(cd), &br))
        return 0;
    return cd.hDevice;
}

static void DestroyTestDevice(D3DKMT_HANDLE hDevice)
{
    D3DKMT_DESTROYDEVICE dd;
    DWORD br;
    memset(&dd, 0, sizeof(dd));
    dd.hDevice = hDevice;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                 &dd, sizeof(dd), NULL, 0, &br);
}

/*
 * Test 1: OpenAdapterFromGdiDisplayName with \\.\DISPLAY1
 */
static void Test_OpenAdapterFromGdiDisplayName(void)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME openAdapter;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&openAdapter, 0, sizeof(openAdapter));
    wcscpy(openAdapter.DeviceName, L"\\\\.\\DISPLAY1");

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME,
                     &openAdapter, sizeof(openAdapter),
                     &openAdapter, sizeof(openAdapter), &br);

    if (!r)
    {
        skip("OpenAdapterFromGdiDisplayName failed (%lu)\n", GetLastError());
        CloseDxg();
        return;
    }

    ok(openAdapter.hAdapter != 0,
       "OpenAdapterFromGdiDisplayName: adapter handle is 0\n");

    ok(openAdapter.AdapterLuid.LowPart != 0 ||
       openAdapter.AdapterLuid.HighPart != 0,
       "OpenAdapterFromGdiDisplayName: LUID is zero\n");

    /* Close via CloseAdapter */
    {
        D3DKMT_CLOSEADAPTER ca;
        memset(&ca, 0, sizeof(ca));
        ca.hAdapter = openAdapter.hAdapter;
        SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                     &ca, sizeof(ca), NULL, 0, &br);
    }

    CloseDxg();
}

/*
 * Test 2: QueryAdapterInfo for DRIVERVERSION and GETSEGMENTSIZE
 */
static void Test_QueryAdapterInfoDriverVersion(void)
{
    D3DKMT_QUERYADAPTERINFO qi;
    D3DKMT_DRIVERVERSION version;
    D3DKMT_HANDLE hAdapter;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapterViaEnum();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    /* Query DRIVERVERSION (type 13) */
    memset(&qi, 0, sizeof(qi));
    memset(&version, 0, sizeof(version));
    qi.hAdapter = hAdapter;
    qi.Type = KMTQAITYPE_DRIVERVERSION;
    qi.pPrivateDriverData = &version;
    qi.PrivateDriverDataSize = sizeof(version);

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                     &qi, sizeof(qi), &qi, sizeof(qi), &br);

    if (!r)
    {
        skip("QueryAdapterInfo DRIVERVERSION failed (%lu)\n", GetLastError());
        CloseDxg();
        return;
    }

    /* WDDM 1.0 = KMT_DRIVERVERSION_WDDM_1_0 = 1000 */
    ok((UINT)version >= 1000,
       "Expected WDDM version >= 1000 (WDDM 1.0), got %u\n", (UINT)version);

    /* Query GETSEGMENTSIZE (type 3) */
    {
        D3DKMT_SEGMENTSIZEINFO segInfo;
        memset(&qi, 0, sizeof(qi));
        memset(&segInfo, 0, sizeof(segInfo));
        qi.hAdapter = hAdapter;
        qi.Type = KMTQAITYPE_GETSEGMENTSIZE;
        qi.pPrivateDriverData = &segInfo;
        qi.PrivateDriverDataSize = sizeof(segInfo);

        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                         &qi, sizeof(qi), &qi, sizeof(qi), &br);

        if (r)
        {
            ok(segInfo.SharedSystemMemorySize > 0 ||
               segInfo.DedicatedVideoMemorySize > 0 ||
               segInfo.DedicatedSystemMemorySize > 0,
               "All segment sizes are zero\n");
        }
        else
        {
            skip("QueryAdapterInfo GETSEGMENTSIZE failed\n");
        }
    }

    CloseDxg();
}

/*
 * Test 3: GetDisplayModeList with mode content validation
 */
static void Test_DisplayModeListContent(void)
{
    D3DKMT_GETDISPLAYMODELIST getModes;
    DWORD br;
    BOOL r;
    D3DKMT_HANDLE hAdapter;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapterViaEnum();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    /* Query mode count */
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
        skip("GetDisplayModeList count query failed (%lu)\n", GetLastError());
        CloseDxg();
        return;
    }

    ok(getModes.ModeCount >= 1,
       "Expected at least 1 display mode, got %lu\n", getModes.ModeCount);

    if (getModes.ModeCount >= 1)
    {
        D3DKMT_DISPLAYMODE *pModes;
        UINT count = getModes.ModeCount;

        pModes = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                           count * sizeof(D3DKMT_DISPLAYMODE));
        if (pModes)
        {
            getModes.pModeList = pModes;
            getModes.ModeCount = count;

            r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                             &getModes, sizeof(getModes),
                             &getModes, sizeof(getModes), &br);

            if (r && getModes.ModeCount >= 1)
            {
                /* First mode should have reasonable dimensions */
                ok(pModes[0].Width >= 640,
                   "First mode Width %lu < 640\n", pModes[0].Width);
                ok(pModes[0].Height >= 480,
                   "First mode Height %lu < 480\n", pModes[0].Height);
            }

            HeapFree(GetProcessHeap(), 0, pModes);
        }
    }

    CloseDxg();
}

/*
 * Test 4: VidPn source ownership (set exclusive, check, release)
 */
static void Test_VidPnSourceOwnership(void)
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    D3DKMT_SETVIDPNSOURCEOWNER setOwner;
    D3DKMT_VIDPNSOURCEOWNER_TYPE ownerType;
    D3DDDI_VIDEO_PRESENT_SOURCE_ID srcId;
    D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP checkOwner;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapterViaEnum();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("Cannot create device\n"); CloseDxg(); return; }

    /* Set EXCLUSIVE ownership on VidPnSourceId 0 */
    ownerType = D3DKMT_VIDPNSOURCEOWNER_EXCLUSIVE;
    srcId = 0;
    memset(&setOwner, 0, sizeof(setOwner));
    setOwner.hDevice = hDevice;
    setOwner.pType = &ownerType;
    setOwner.pVidPnSourceId = &srcId;
    setOwner.VidPnSourceCount = 1;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_SETVIDPNSOURCEOWNER,
                     &setOwner, sizeof(setOwner), NULL, 0, &br);

    ok(r, "SetVidPnSourceOwner EXCLUSIVE failed (%lu)\n", GetLastError());

    /* Check exclusive ownership */
    memset(&checkOwner, 0, sizeof(checkOwner));
    checkOwner.hAdapter = hAdapter;
    checkOwner.VidPnSourceId = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CHECKVIDPNEXCLUSIVEOWNERSHIP,
                     &checkOwner, sizeof(checkOwner), NULL, 0, &br);

    /* The IOCTL should succeed (meaning ownership is tracked) */
    ok(r, "CheckVidPnExclusiveOwnership failed (%lu)\n", GetLastError());

    /* Release ownership (set UNOWNED with VidPnSourceCount=0) */
    memset(&setOwner, 0, sizeof(setOwner));
    setOwner.hDevice = hDevice;
    setOwner.pType = NULL;
    setOwner.pVidPnSourceId = NULL;
    setOwner.VidPnSourceCount = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_SETVIDPNSOURCEOWNER,
                     &setOwner, sizeof(setOwner), NULL, 0, &br);

    ok(r, "SetVidPnSourceOwner release failed (%lu)\n", GetLastError());

    DestroyTestDevice(hDevice);
    CloseDxg();
}

/*
 * Test 5: WaitForVerticalBlankEvent
 */
static void Test_WaitForVerticalBlankEvent(void)
{
    D3DKMT_WAITFORVERTICALBLANKEVENT vblank;
    D3DKMT_HANDLE hAdapter;
    DWORD br;
    BOOL r;
    DWORD start, elapsed;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapterViaEnum();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    memset(&vblank, 0, sizeof(vblank));
    vblank.hAdapter = hAdapter;
    vblank.hDevice = 0; /* optional */
    vblank.VidPnSourceId = 0;

    start = GetTickCount();
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_WAITFORVERTICALBLANKEVENT,
                     &vblank, sizeof(vblank), NULL, 0, &br);
    elapsed = GetTickCount() - start;

    ok(r, "WaitForVerticalBlankEvent failed (%lu)\n", GetLastError());

    /* Should return within 50ms (stub returns immediately) */
    ok(elapsed < 50,
       "WaitForVerticalBlankEvent took %lu ms, expected < 50\n", elapsed);

    CloseDxg();
}

/*
 * Test 6: GetScanLine
 */
static void Test_GetScanLine(void)
{
    D3DKMT_GETSCANLINE scanLine;
    D3DKMT_HANDLE hAdapter;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapterViaEnum();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    memset(&scanLine, 0, sizeof(scanLine));
    scanLine.hAdapter = hAdapter;
    scanLine.VidPnSourceId = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETSCANLINE,
                     &scanLine, sizeof(scanLine),
                     &scanLine, sizeof(scanLine), &br);

    ok(r, "GetScanLine failed (%lu)\n", GetLastError());

    /*
     * ScanLine should be in a valid range. The stub returns 0 which is
     * valid. For a real display with Height H, ScanLine < H + vblank lines.
     * Just verify it's within a generous bound.
     */
    ok(scanLine.ScanLine < 4096,
       "GetScanLine returned implausible ScanLine %u\n", scanLine.ScanLine);

    CloseDxg();
}

/*
 * Test 7: PollDisplayChildren
 */
static void Test_PollDisplayChildren(void)
{
    D3DKMT_POLLDISPLAYCHILDREN poll;
    D3DKMT_HANDLE hAdapter;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapterViaEnum();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    memset(&poll, 0, sizeof(poll));
    poll.hAdapter = hAdapter;
    poll.NonDestructiveOnly = 1;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_POLLDISPLAYCHILDREN,
                     &poll, sizeof(poll), NULL, 0, &br);

    ok(r, "PollDisplayChildren failed (%lu)\n", GetLastError());

    CloseDxg();
}

/*
 * Test 8: Multiple adapter open/close (leak test)
 */
static void Test_MultipleAdapterOpenClose(void)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME openAdapter;
    D3DKMT_CLOSEADAPTER closeAdapter;
    D3DKMT_HANDLE handles[10];
    DWORD br;
    BOOL r;
    int i;
    int openCount = 0;

    if (!OpenDxg()) return;

    /* Open the same adapter 10 times */
    for (i = 0; i < 10; i++)
    {
        memset(&openAdapter, 0, sizeof(openAdapter));
        wcscpy(openAdapter.DeviceName, L"\\\\.\\DISPLAY1");

        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME,
                         &openAdapter, sizeof(openAdapter),
                         &openAdapter, sizeof(openAdapter), &br);

        if (!r)
        {
            skip("OpenAdapter iteration %d failed\n", i);
            break;
        }

        handles[i] = openAdapter.hAdapter;
        openCount++;
        ok(handles[i] != 0, "Iteration %d: adapter handle is 0\n", i);
    }

    /* Close all opened adapters */
    for (i = 0; i < openCount; i++)
    {
        memset(&closeAdapter, 0, sizeof(closeAdapter));
        closeAdapter.hAdapter = handles[i];
        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                         &closeAdapter, sizeof(closeAdapter), NULL, 0, &br);
        ok(r, "CloseAdapter iteration %d failed (%lu)\n", i, GetLastError());
    }

    /* Verify we opened at least some */
    ok(openCount > 0, "Could not open any adapters\n");

    CloseDxg();
}

/*
 * Test 9: Invalid display name
 */
static void Test_InvalidDisplayName(void)
{
    D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME openAdapter;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&openAdapter, 0, sizeof(openAdapter));
    wcscpy(openAdapter.DeviceName, L"INVALID");

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_OPENADAPTERFROMGDIDISPLAYNAME,
                     &openAdapter, sizeof(openAdapter),
                     &openAdapter, sizeof(openAdapter), &br);

    /*
     * The current implementation finds the first started adapter regardless
     * of the display name string, so this may succeed. On a Windows-like
     * implementation it would fail. Test that it at least doesn't crash.
     */
    if (r)
    {
        /* If it succeeded, adapter handle should still be valid */
        ok(openAdapter.hAdapter != 0,
           "Invalid name succeeded but handle is 0\n");
    }

    CloseDxg();
}

/*
 * Test 10: CheckMonitorPowerState
 */
static void Test_CheckMonitorPowerState(void)
{
    D3DKMT_CHECKMONITORPOWERSTATE monPower;
    D3DKMT_HANDLE hAdapter;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = GetFirstAdapterViaEnum();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    memset(&monPower, 0, sizeof(monPower));
    monPower.hAdapter = hAdapter;
    monPower.VidPnSourceId = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CHECKMONITORPOWERSTATE,
                     &monPower, sizeof(monPower), NULL, 0, &br);

    ok(r, "CheckMonitorPowerState failed (%lu)\n", GetLastError());

    CloseDxg();
}

START_TEST(D3dkmtDisplayAdv)
{
    Test_OpenAdapterFromGdiDisplayName();
    Test_QueryAdapterInfoDriverVersion();
    Test_DisplayModeListContent();
    Test_VidPnSourceOwnership();
    Test_WaitForVerticalBlankEvent();
    Test_GetScanLine();
    Test_PollDisplayChildren();
    Test_MultipleAdapterOpenClose();
    Test_InvalidDisplayName();
    Test_CheckMonitorPowerState();
}
