/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT present path end-to-end tests via gdi32 user-mode API
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the full present path: adapter open -> device create -> allocation
 * create -> present -> cleanup.  Also exercises GetPresentHistory and
 * GetSharedPrimaryHandle APIs.
 */

#include "precomp.h"

/*
 * Helper: create a single allocation with embedded width/height/bpp
 * for the miniport's private driver data.
 */
static D3DKMT_HANDLE
CreateSurfaceAllocation(
    PFN_D3DKMTCreateAllocation pfnCreate,
    D3DKMT_HANDLE hDevice,
    UINT Width, UINT Height, UINT Bpp)
{
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO ai;
    struct {
        UINT Width;
        UINT Height;
        UINT Bpp;
    } privData;
    NTSTATUS Status;

    privData.Width = Width;
    privData.Height = Height;
    privData.Bpp = Bpp;

    memset(&ai, 0, sizeof(ai));
    ai.pPrivateDriverData = &privData;
    ai.PrivateDriverDataSize = sizeof(privData);

    memset(&ca, 0, sizeof(ca));
    ca.hDevice = hDevice;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &ai;

    Status = pfnCreate(&ca);
    if (!NT_SUCCESS(Status))
        return 0;

    return ai.hAllocation;
}

static NTSTATUS
DestroySingleAllocation(
    PFN_D3DKMTDestroyAllocation pfnDestroy,
    D3DKMT_HANDLE hDevice,
    D3DKMT_HANDLE hAllocation)
{
    D3DKMT_DESTROYALLOCATION da;

    memset(&da, 0, sizeof(da));
    da.hDevice = hDevice;
    da.phAllocationList = &hAllocation;
    da.AllocationCount = 1;

    return pfnDestroy(&da);
}

/* ---- Test 1: Basic present with source allocation ---- */
static void Test_BasicPresent(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_PRESENT pres;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTPresent);
    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    hAlloc = CreateSurfaceAllocation(pfnD3DKMTCreateAllocation, hDevice,
                                     1024, 768, 32);
    if (!hAlloc)
    {
        skip("CreateAllocation failed for BasicPresent\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
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

    Status = pfnD3DKMTPresent(&pres);
    ok_succeeded(Status,
       "Basic present should succeed, got 0x%08lX\n", Status);

    DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 2: BLT present (source to destination) ---- */
static void Test_BltPresent(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hSrc, hDst;
    D3DKMT_PRESENT pres;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTPresent);
    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    hSrc = CreateSurfaceAllocation(pfnD3DKMTCreateAllocation, hDevice,
                                   640, 480, 32);
    hDst = CreateSurfaceAllocation(pfnD3DKMTCreateAllocation, hDevice,
                                   640, 480, 32);
    if (!hSrc || !hDst)
    {
        skip("CreateAllocation failed for BltPresent\n");
        if (hSrc) DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hSrc);
        if (hDst) DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hDst);
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
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

    Status = pfnD3DKMTPresent(&pres);
    ok_succeeded(Status,
       "BLT present should succeed, got 0x%08lX\n", Status);

    DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hDst);
    DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hSrc);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 3: Present with NULL source ---- */
static void Test_PresentNullSource(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_PRESENT pres;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTPresent);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    memset(&pres, 0, sizeof(pres));
    pres.hDevice = hDevice;
    pres.hSource = 0; /* NULL source */
    pres.VidPnSourceId = 0;

    Status = pfnD3DKMTPresent(&pres);
    ok_failed(Status,
       "Present with NULL source should fail, got 0x%08lX\n", Status);

    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 4: Present with invalid device ---- */
static void Test_PresentInvalidDevice(void)
{
    D3DKMT_PRESENT pres;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTPresent);

    memset(&pres, 0, sizeof(pres));
    pres.hDevice = 0xBAD0CAFE;
    pres.VidPnSourceId = 0;

    Status = pfnD3DKMTPresent(&pres);
    ok(Status == STATUS_INVALID_PARAMETER ||
       Status == STATUS_INVALID_HANDLE ||
       !NT_SUCCESS(Status),
       "Present with invalid device should fail, got 0x%08lX\n", Status);
}

/* ---- Test 5: Rapid presents (100 iterations) ---- */
static void Test_RapidPresents(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_PRESENT pres;
    NTSTATUS Status;
    ULONG i;
    ULONG successCount = 0;

    LOAD_D3DKMT(D3DKMTPresent);
    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    hAlloc = CreateSurfaceAllocation(pfnD3DKMTCreateAllocation, hDevice,
                                     640, 480, 32);
    if (!hAlloc)
    {
        skip("CreateAllocation failed for RapidPresents\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
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
        Status = pfnD3DKMTPresent(&pres);
        if (NT_SUCCESS(Status))
            successCount++;
    }

    ok(successCount == 100,
       "All 100 rapid presents should succeed, got %lu\n", successCount);

    DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 6: GetPresentHistory ---- */
static void Test_GetPresentHistory(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hAlloc;
    D3DKMT_PRESENT pres;
    D3DKMT_GETPRESENTHISTORY gph;
    UCHAR tokenBuf[4096];
    NTSTATUS Status;
    ULONG i;

    LOAD_D3DKMT(D3DKMTPresent);
    LOAD_D3DKMT(D3DKMTGetPresentHistory);
    LOAD_D3DKMT(D3DKMTCreateAllocation);
    LOAD_D3DKMT(D3DKMTDestroyAllocation);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }
    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseAdapter(hAdapter); return; }

    hAlloc = CreateSurfaceAllocation(pfnD3DKMTCreateAllocation, hDevice,
                                     640, 480, 32);
    if (!hAlloc)
    {
        skip("CreateAllocation failed for GetPresentHistory\n");
        DestroyTestDevice(hDevice);
        CloseAdapter(hAdapter);
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
        pfnD3DKMTPresent(&pres);

    /* Query present history */
    memset(&gph, 0, sizeof(gph));
    gph.hAdapter = hAdapter;
    gph.ProvidedSize = sizeof(tokenBuf);
    gph.pTokens = (D3DKMT_PRESENTHISTORYTOKEN *)tokenBuf;

    Status = pfnD3DKMTGetPresentHistory(&gph);
    if (NT_SUCCESS(Status))
    {
        ok(gph.WrittenSize <= gph.ProvidedSize,
           "WrittenSize %u should not exceed ProvidedSize %u\n",
           gph.WrittenSize, gph.ProvidedSize);
    }
    else
    {
        /* Not all drivers populate present history; verify no crash */
        trace("GetPresentHistory returned 0x%08lX -- acceptable\n", Status);
    }

    DestroySingleAllocation(pfnD3DKMTDestroyAllocation, hDevice, hAlloc);
    DestroyTestDevice(hDevice);
    CloseAdapter(hAdapter);
}

/* ---- Test 7: GetSharedPrimaryHandle ---- */
static void Test_GetSharedPrimaryHandle(void)
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_GETSHAREDPRIMARYHANDLE gsph;
    NTSTATUS Status;

    LOAD_D3DKMT(D3DKMTGetSharedPrimaryHandle);

    hAdapter = OpenAdapterFromDisplay1();
    if (!hAdapter) { skip("No adapter\n"); return; }

    memset(&gsph, 0, sizeof(gsph));
    gsph.hAdapter = hAdapter;
    gsph.VidPnSourceId = 0;

    Status = pfnD3DKMTGetSharedPrimaryHandle(&gsph);
    if (NT_SUCCESS(Status))
    {
        /* Win11 returns a zero shared-primary handle when there is no
         * kernel-managed shared primary for the source -- that is normal, not a
         * failure. Trace the value rather than asserting it is non-zero. */
        trace("GetSharedPrimaryHandle -> handle 0x%08lx\n",
              (unsigned long)gsph.hSharedPrimary);
    }
    else
    {
        /* May fail if no primary has been committed yet */
        trace("GetSharedPrimaryHandle returned 0x%08lX -- no primary active\n",
              Status);
    }

    CloseAdapter(hAdapter);
}

START_TEST(present)
{
    Test_BasicPresent();
    Test_BltPresent();
    Test_PresentNullSource();
    Test_PresentInvalidDevice();
    Test_RapidPresents();
    Test_GetPresentHistory();
    Test_GetSharedPrimaryHandle();
}
