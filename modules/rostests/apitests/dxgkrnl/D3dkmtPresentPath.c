/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT present path end-to-end tests via IOCTLs
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the full present path: adapter open -> device create -> allocation
 * create -> present -> cleanup.  Also exercises GetPresentHistory and
 * GetSharedPrimaryHandle IOCTLs.
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

static D3DKMT_HANDLE EnumFirstAdapter(void)
{
    D3DKMT_ENUMADAPTERS e;
    DWORD br;
    memset(&e, 0, sizeof(e));
    if (!SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                      &e, sizeof(e), &e, sizeof(e), &br) ||
        e.NumAdapters == 0)
    {
        return 0;
    }
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

static D3DKMT_HANDLE CreateTestAllocation(D3DKMT_HANDLE hDevice,
                                          UINT Width, UINT Height, UINT Bpp)
{
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO allocInfo;
    DWORD br;

    memset(&allocInfo, 0, sizeof(allocInfo));
    /* Private driver data encodes width/height/bpp for the miniport */
    struct {
        UINT Width;
        UINT Height;
        UINT Bpp;
    } privData;
    privData.Width = Width;
    privData.Height = Height;
    privData.Bpp = Bpp;
    allocInfo.pPrivateDriverData = &privData;
    allocInfo.PrivateDriverDataSize = sizeof(privData);

    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &allocInfo;

    if (!SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEALLOCATION,
                      &ca, sizeof(ca), &ca, sizeof(ca), &br))
        return 0;
    return allocInfo.hAllocation;
}

static void DestroyTestAllocation(D3DKMT_HANDLE hDevice,
                                  D3DKMT_HANDLE hAllocation)
{
    D3DKMT_DESTROYALLOCATION da;
    DWORD br;
    memset(&da, 0, sizeof(da));
    da.hDevice = hDevice;
    da.AllocationCount = 1;
    da.phAllocationList = &hAllocation;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYALLOCATION,
                 &da, sizeof(da), NULL, 0, &br);
}

/* ---- Test: Basic present with source allocation ---- */
static void Test_BasicPresent(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_PRESENT pres;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter for BasicPresent\n"); CloseDxg(); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed for BasicPresent\n"); CloseDxg(); return; }

    hAlloc = CreateTestAllocation(hDevice, 1024, 768, 32);
    if (!hAlloc)
    {
        skip("CreateAllocation failed for BasicPresent\n");
        DestroyTestDevice(hDevice);
        CloseDxg();
        return;
    }

    memset(&pres, 0, sizeof(pres));
    pres.hDevice = hDevice;
    pres.hSource = hAlloc;
    pres.VidPnSourceId = 0;
    pres.SrcRect.left = 0;
    pres.SrcRect.top = 0;
    pres.SrcRect.right = 1024;
    pres.SrcRect.bottom = 768;
    pres.DstRect = pres.SrcRect;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                     &pres, sizeof(pres), &pres, sizeof(pres), &br);
    ok(r, "Basic present with valid source allocation should succeed (error %lu)\n",
       GetLastError());

    DestroyTestAllocation(hDevice, hAlloc);
    DestroyTestDevice(hDevice);
    CloseDxg();
}

/* ---- Test: BLT present (source to destination) ---- */
static void Test_BltPresent(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hSrc, hDst;
    D3DKMT_PRESENT pres;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter for BltPresent\n"); CloseDxg(); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed for BltPresent\n"); CloseDxg(); return; }

    hSrc = CreateTestAllocation(hDevice, 640, 480, 32);
    hDst = CreateTestAllocation(hDevice, 640, 480, 32);
    if (!hSrc || !hDst)
    {
        skip("CreateAllocation failed for BltPresent\n");
        if (hSrc) DestroyTestAllocation(hDevice, hSrc);
        if (hDst) DestroyTestAllocation(hDevice, hDst);
        DestroyTestDevice(hDevice);
        CloseDxg();
        return;
    }

    memset(&pres, 0, sizeof(pres));
    pres.hDevice = hDevice;
    pres.hSource = hSrc;
    pres.hDestination = hDst;
    pres.VidPnSourceId = 0;
    pres.SrcRect.right = 640;
    pres.SrcRect.bottom = 480;
    pres.DstRect = pres.SrcRect;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                     &pres, sizeof(pres), &pres, sizeof(pres), &br);
    ok(r, "BLT present with source and destination should succeed (error %lu)\n",
       GetLastError());

    DestroyTestAllocation(hDevice, hDst);
    DestroyTestAllocation(hDevice, hSrc);
    DestroyTestDevice(hDevice);
    CloseDxg();
}

/* ---- Test: Present with NULL source should fail ---- */
static void Test_PresentNullSource(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_PRESENT pres;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter for PresentNullSource\n"); CloseDxg(); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed for PresentNullSource\n"); CloseDxg(); return; }

    memset(&pres, 0, sizeof(pres));
    pres.hDevice = hDevice;
    pres.hSource = 0; /* NULL source */
    pres.VidPnSourceId = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                     &pres, sizeof(pres), &pres, sizeof(pres), &br);
    ok(!r, "Present with NULL source should fail gracefully\n");

    DestroyTestDevice(hDevice);
    CloseDxg();
}

/* ---- Test: Present with invalid device handle ---- */
static void Test_PresentInvalidDevice(void)
{
    D3DKMT_PRESENT pres;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&pres, 0, sizeof(pres));
    pres.hDevice = 0xBAD0CAFE;
    pres.VidPnSourceId = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                     &pres, sizeof(pres), &pres, sizeof(pres), &br);
    ok(!r, "Present with garbage device handle should return failure\n");

    CloseDxg();
}

/* ---- Test: Rapid presents in a loop ---- */
static void Test_RapidPresents(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_PRESENT pres;
    DWORD br;
    BOOL r;
    ULONG i;
    ULONG successCount = 0;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter for RapidPresents\n"); CloseDxg(); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed for RapidPresents\n"); CloseDxg(); return; }

    hAlloc = CreateTestAllocation(hDevice, 640, 480, 32);
    if (!hAlloc)
    {
        skip("CreateAllocation failed for RapidPresents\n");
        DestroyTestDevice(hDevice);
        CloseDxg();
        return;
    }

    memset(&pres, 0, sizeof(pres));
    pres.hDevice = hDevice;
    pres.hSource = hAlloc;
    pres.VidPnSourceId = 0;
    pres.SrcRect.right = 640;
    pres.SrcRect.bottom = 480;
    pres.DstRect = pres.SrcRect;

    for (i = 0; i < 100; ++i)
    {
        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                         &pres, sizeof(pres), &pres, sizeof(pres), &br);
        if (r)
            successCount++;
    }

    ok(successCount == 100,
       "All 100 rapid presents should succeed, got %lu\n", successCount);

    DestroyTestAllocation(hDevice, hAlloc);
    DestroyTestDevice(hDevice);
    CloseDxg();
}

/* ---- Test: GetPresentHistory IOCTL ---- */
static void Test_GetPresentHistory(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_PRESENT pres;
    D3DKMT_GETPRESENTHISTORY gph;
    UCHAR tokenBuf[4096];
    DWORD br;
    BOOL r;
    ULONG i;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter for GetPresentHistory\n"); CloseDxg(); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed for GetPresentHistory\n"); CloseDxg(); return; }

    hAlloc = CreateTestAllocation(hDevice, 640, 480, 32);
    if (!hAlloc)
    {
        skip("CreateAllocation failed for GetPresentHistory\n");
        DestroyTestDevice(hDevice);
        CloseDxg();
        return;
    }

    /* Issue several presents to populate history */
    memset(&pres, 0, sizeof(pres));
    pres.hDevice = hDevice;
    pres.hSource = hAlloc;
    pres.VidPnSourceId = 0;
    pres.SrcRect.right = 640;
    pres.SrcRect.bottom = 480;
    pres.DstRect = pres.SrcRect;

    for (i = 0; i < 5; ++i)
    {
        SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                     &pres, sizeof(pres), &pres, sizeof(pres), &br);
    }

    /* Query present history */
    memset(&gph, 0, sizeof(gph));
    gph.hAdapter = hAdapter;
    gph.ProvidedSize = sizeof(tokenBuf);
    gph.pTokens = (D3DKMT_PRESENTHISTORYTOKEN *)tokenBuf;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETPRESENTHISTORY,
                     &gph, sizeof(gph), &gph, sizeof(gph), &br);
    /* The IOCTL may or may not be implemented; just verify no crash */
    if (r)
    {
        ok(gph.WrittenSize <= gph.ProvidedSize,
           "WrittenSize %u should not exceed ProvidedSize %u\n",
           gph.WrittenSize, gph.ProvidedSize);
    }
    else
    {
        /* Acceptable: not all implementations populate present history */
        trace("GetPresentHistory IOCTL returned failure (error %lu) -- acceptable\n",
              GetLastError());
    }

    DestroyTestAllocation(hDevice, hAlloc);
    DestroyTestDevice(hDevice);
    CloseDxg();
}

/* ---- Test: GetSharedPrimaryHandle IOCTL ---- */
static void Test_GetSharedPrimaryHandle(void)
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_GETSHAREDPRIMARYHANDLE gsph;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter for GetSharedPrimaryHandle\n"); CloseDxg(); return; }

    memset(&gsph, 0, sizeof(gsph));
    gsph.hAdapter = hAdapter;
    gsph.VidPnSourceId = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE,
                     &gsph, sizeof(gsph), &gsph, sizeof(gsph), &br);
    if (r)
    {
        /* On a system with an active primary surface, the handle should be non-zero */
        ok(gsph.hSharedPrimary != 0,
           "SharedPrimary handle should be non-zero when active\n");
    }
    else
    {
        /* May fail if no primary has been committed yet; verify no crash */
        trace("GetSharedPrimaryHandle failed (error %lu) -- no primary active\n",
              GetLastError());
    }

    CloseDxg();
}

/* ---- Test: GetSharedPrimaryHandle with invalid adapter ---- */
static void Test_GetSharedPrimaryHandleInvalidAdapter(void)
{
    D3DKMT_GETSHAREDPRIMARYHANDLE gsph;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&gsph, 0, sizeof(gsph));
    gsph.hAdapter = 0xDEADBEEF;
    gsph.VidPnSourceId = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE,
                     &gsph, sizeof(gsph), &gsph, sizeof(gsph), &br);
    ok(!r, "GetSharedPrimaryHandle with invalid adapter should fail\n");

    CloseDxg();
}

/* ---- Test: Present IOCTL code values ---- */
static void Test_PresentPathIoctlCodes(void)
{
    /* Present and related IOCTL function codes */
    ok_eq_uint((IOCTL_D3DKMT_PRESENT >> 2) & 0xFFF, 0x141);
    ok_eq_uint((IOCTL_D3DKMT_GETPRESENTHISTORY >> 2) & 0xFFF, 0x142);
    ok_eq_uint((IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE >> 2) & 0xFFF, 0x136);

    /* Must be distinct */
    ok(IOCTL_D3DKMT_PRESENT != IOCTL_D3DKMT_GETPRESENTHISTORY,
       "Present and GetPresentHistory IOCTLs must differ\n");
    ok(IOCTL_D3DKMT_PRESENT != IOCTL_D3DKMT_GETSHAREDPRIMARYHANDLE,
       "Present and GetSharedPrimaryHandle IOCTLs must differ\n");
}

START_TEST(D3dkmtPresentPath)
{
    Test_PresentPathIoctlCodes();
    Test_BasicPresent();
    Test_BltPresent();
    Test_PresentNullSource();
    Test_PresentInvalidDevice();
    Test_RapidPresents();
    Test_GetPresentHistory();
    Test_GetSharedPrimaryHandle();
    Test_GetSharedPrimaryHandleInvalidAdapter();
}
