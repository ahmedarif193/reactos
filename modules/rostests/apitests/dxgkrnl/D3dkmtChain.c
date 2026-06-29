/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Full DxgkDdi callback chain lifecycle tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the full D3DKMT call chains through \Device\DxgKrnl IOCTLs:
 *   - OpenAdapter -> QueryAdapterInfo(DRIVERCAPS) -> verify WDDM version -> CloseAdapter
 *   - OpenAdapter -> CreateDevice -> CreateContext -> Render(empty) -> DestroyContext -> DestroyDevice -> CloseAdapter
 *   - OpenAdapter -> CreateDevice -> CreateAllocation -> Lock -> write -> Unlock -> Lock -> verify -> DestroyAllocation -> DestroyDevice -> CloseAdapter
 *   - Error paths: NULL handles, invalid handles, wrong buffer sizes
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

/*
 * Chain 1: OpenAdapter -> QueryAdapterInfo(SegmentSize) -> verify -> CloseAdapter
 *
 * Opens adapter via LUID, queries segment size info to verify the adapter
 * reports sensible memory values, then closes.
 */
static void Test_AdapterQueryChain(void)
{
    D3DKMT_ENUMADAPTERS ea;
    D3DKMT_OPENADAPTERFROMLUID oa;
    D3DKMT_QUERYADAPTERINFO qi;
    D3DKMT_SEGMENTSIZEINFO seg;
    D3DKMT_CLOSEADAPTER ca;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    /* Enumerate to get a valid LUID */
    memset(&ea, 0, sizeof(ea));
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                     &ea, sizeof(ea), &ea, sizeof(ea), &br);
    if (!r || ea.NumAdapters == 0)
    {
        skip("No adapters for AdapterQueryChain\n");
        CloseDxg();
        return;
    }

    /* Open adapter from LUID */
    memset(&oa, 0, sizeof(oa));
    oa.AdapterLuid = ea.Adapters[0].AdapterLuid;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_OPENADAPTERFROMLUID,
                     &oa, sizeof(oa), &oa, sizeof(oa), &br);
    if (!r)
    {
        skip("OpenAdapterFromLuid failed (%lu)\n", GetLastError());
        CloseDxg();
        return;
    }
    ok(oa.hAdapter != 0, "OpenAdapterFromLuid returned zero handle\n");

    /* Query segment size info (KMTQAITYPE_GETSEGMENTSIZE = 3) */
    memset(&qi, 0, sizeof(qi));
    memset(&seg, 0, sizeof(seg));
    qi.hAdapter = oa.hAdapter;
    qi.Type = 3; /* KMTQAITYPE_GETSEGMENTSIZE */
    qi.pPrivateDriverData = &seg;
    qi.PrivateDriverDataSize = sizeof(seg);

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                     &qi, sizeof(qi), &qi, sizeof(qi), &br);
    if (!r)
    {
        skip("QueryAdapterInfo(GETSEGMENTSIZE) failed (%lu)\n", GetLastError());
    }
    else
    {
        /* At least one memory pool should be non-zero */
        ok(seg.DedicatedVideoMemorySize > 0 ||
           seg.DedicatedSystemMemorySize > 0 ||
           seg.SharedSystemMemorySize > 0,
           "All segment sizes are zero\n");
    }

    /* Close adapter */
    memset(&ca, 0, sizeof(ca));
    ca.hAdapter = oa.hAdapter;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                     &ca, sizeof(ca), NULL, 0, &br);
    ok(r, "CloseAdapter failed (%lu)\n", GetLastError());

    CloseDxg();
}

/*
 * Chain 2: OpenAdapter -> CreateDevice -> CreateContext -> Render(empty) ->
 *          DestroyContext -> DestroyDevice -> CloseAdapter
 *
 * Exercises the full rendering pipeline setup and teardown without
 * submitting any actual GPU work (empty render).
 */
static void Test_DeviceContextRenderChain(void)
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_CREATECONTEXT cc;
    D3DKMT_RENDER rn;
    D3DKMT_DESTROYCONTEXT dc;
    D3DKMT_DESTROYDEVICE dd;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    /* CreateDevice */
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    if (!r)
    {
        skip("CreateDevice failed (%lu)\n", GetLastError());
        CloseDxg();
        return;
    }
    ok(cd.hDevice != 0, "CreateDevice returned zero handle\n");

    /* CreateContext on the device */
    memset(&cc, 0, sizeof(cc));
    cc.hDevice = cd.hDevice;
    cc.NodeOrdinal = 0;
    cc.EngineAffinity = 1;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                     &cc, sizeof(cc), &cc, sizeof(cc), &br);
    if (!r)
    {
        skip("CreateContext failed (%lu)\n", GetLastError());
        /* Cleanup device */
        memset(&dd, 0, sizeof(dd));
        dd.hDevice = cd.hDevice;
        SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
        CloseDxg();
        return;
    }
    ok(cc.hContext != 0, "CreateContext returned zero handle\n");
    ok(cc.hContext != cd.hDevice,
       "Context handle should differ from device handle\n");

    /* Verify context got a command buffer */
    ok(cc.CommandBufferSize > 0,
       "Expected non-zero CommandBufferSize, got %u\n", cc.CommandBufferSize);

    /* Render with empty command buffer (zero length) */
    memset(&rn, 0, sizeof(rn));
    rn.hContext = cc.hContext;
    rn.CommandOffset = 0;
    rn.CommandLength = 0;
    rn.AllocationCount = 0;
    rn.PatchLocationCount = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_RENDER,
                     &rn, sizeof(rn), &rn, sizeof(rn), &br);
    /* Empty render may succeed or fail depending on driver; just log */
    if (!r)
    {
        trace("Render(empty) returned error %lu (acceptable)\n", GetLastError());
    }

    /* DestroyContext */
    memset(&dc, 0, sizeof(dc));
    dc.hContext = cc.hContext;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                     &dc, sizeof(dc), NULL, 0, &br);
    ok(r, "DestroyContext failed (%lu)\n", GetLastError());

    /* DestroyDevice */
    memset(&dd, 0, sizeof(dd));
    dd.hDevice = cd.hDevice;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(r, "DestroyDevice failed (%lu)\n", GetLastError());

    CloseDxg();
}

/*
 * Chain 3: OpenAdapter -> CreateDevice -> CreateAllocation -> Lock ->
 *          write pattern -> Unlock -> Lock -> verify pattern ->
 *          Unlock -> DestroyAllocation -> DestroyDevice -> CloseAdapter
 *
 * Exercises the allocation/lock/unlock memory path.
 */
static void Test_AllocationLockChain(void)
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_CREATEALLOCATION ca;
    D3DDDI_ALLOCATIONINFO allocInfo;
    D3DKMT_LOCK lk;
    D3DKMT_UNLOCK ul;
    D3DKMT_DESTROYALLOCATION da;
    D3DKMT_DESTROYDEVICE dd;
    D3DKMT_HANDLE hAlloc;
    DWORD br;
    BOOL r;
    ULONG i;
    BYTE *pData;
    BYTE pattern = 0xA5;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    /* CreateDevice */
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    if (!r)
    {
        skip("CreateDevice failed (%lu)\n", GetLastError());
        CloseDxg();
        return;
    }

    /* CreateAllocation with 1 allocation, no private driver data */
    memset(&allocInfo, 0, sizeof(allocInfo));
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = cd.hDevice;
    ca.NumAllocations = 1;
    ca.pAllocationInfo = &allocInfo;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEALLOCATION,
                     &ca, sizeof(ca), &ca, sizeof(ca), &br);
    if (!r)
    {
        skip("CreateAllocation failed (%lu)\n", GetLastError());
        /* Cleanup device */
        memset(&dd, 0, sizeof(dd));
        dd.hDevice = cd.hDevice;
        SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
        CloseDxg();
        return;
    }

    hAlloc = allocInfo.hAllocation;
    ok(hAlloc != 0, "CreateAllocation returned zero allocation handle\n");

    /* Lock the allocation */
    memset(&lk, 0, sizeof(lk));
    lk.hDevice = cd.hDevice;
    lk.hAllocation = hAlloc;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_LOCK,
                     &lk, sizeof(lk), &lk, sizeof(lk), &br);
    if (!r)
    {
        skip("Lock failed (%lu)\n", GetLastError());
        goto cleanup_alloc;
    }
    ok(lk.pData != NULL, "Lock returned NULL data pointer\n");

    if (lk.pData != NULL)
    {
        /* Write a test pattern (first 256 bytes) */
        pData = (BYTE *)lk.pData;
        for (i = 0; i < 256; i++)
            pData[i] = pattern;
    }

    /* Unlock */
    memset(&ul, 0, sizeof(ul));
    ul.hDevice = cd.hDevice;
    ul.NumAllocations = 1;
    ul.phAllocations = &hAlloc;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_UNLOCK,
                     &ul, sizeof(ul), NULL, 0, &br);
    ok(r, "Unlock failed (%lu)\n", GetLastError());

    /* Lock again and verify the pattern persists */
    memset(&lk, 0, sizeof(lk));
    lk.hDevice = cd.hDevice;
    lk.hAllocation = hAlloc;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_LOCK,
                     &lk, sizeof(lk), &lk, sizeof(lk), &br);
    if (!r)
    {
        skip("Second Lock failed (%lu)\n", GetLastError());
        goto cleanup_alloc;
    }

    if (lk.pData != NULL)
    {
        pData = (BYTE *)lk.pData;
        for (i = 0; i < 256; i++)
        {
            if (pData[i] != pattern)
            {
                ok(0, "Data mismatch at offset %lu: expected 0x%02X, got 0x%02X\n",
                   i, pattern, pData[i]);
                break;
            }
        }
        if (i == 256)
            ok(1, "Pattern verified across lock/unlock cycle\n");
    }

    /* Unlock again */
    memset(&ul, 0, sizeof(ul));
    ul.hDevice = cd.hDevice;
    ul.NumAllocations = 1;
    ul.phAllocations = &hAlloc;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_UNLOCK,
                 &ul, sizeof(ul), NULL, 0, &br);

cleanup_alloc:
    /* DestroyAllocation */
    memset(&da, 0, sizeof(da));
    da.hDevice = cd.hDevice;
    da.AllocationCount = 1;
    da.phAllocationList = &hAlloc;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYALLOCATION,
                     &da, sizeof(da), NULL, 0, &br);
    ok(r, "DestroyAllocation failed (%lu)\n", GetLastError());

    /* DestroyDevice */
    memset(&dd, 0, sizeof(dd));
    dd.hDevice = cd.hDevice;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(r, "DestroyDevice failed (%lu)\n", GetLastError());

    CloseDxg();
}

/*
 * Error paths: every function with NULL/invalid handles and wrong buffer sizes.
 */
static void Test_ErrorPaths_NullHandles(void)
{
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_DESTROYDEVICE dd;
    D3DKMT_CREATECONTEXT cc;
    D3DKMT_DESTROYCONTEXT dc;
    D3DKMT_CREATEALLOCATION ca;
    D3DKMT_LOCK lk;
    D3DKMT_UNLOCK ul;
    D3DKMT_RENDER rn;
    D3DKMT_DESTROYALLOCATION da;
    D3DKMT_CLOSEADAPTER cla;
    D3DKMT_QUERYADAPTERINFO qi;
    D3DKMT_HANDLE zeroHandle = 0;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    /* CreateDevice with zero adapter */
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    ok(!r, "CreateDevice(hAdapter=0) should fail\n");

    /* CreateDevice with bogus adapter */
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = 0xDEADBEEF;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    ok(!r, "CreateDevice(hAdapter=0xDEADBEEF) should fail\n");

    /* DestroyDevice with zero handle */
    memset(&dd, 0, sizeof(dd));
    dd.hDevice = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(!r, "DestroyDevice(hDevice=0) should fail\n");

    /* DestroyDevice with bogus handle */
    memset(&dd, 0, sizeof(dd));
    dd.hDevice = 0xBAD0CAFE;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(!r, "DestroyDevice(hDevice=0xBAD0CAFE) should fail\n");

    /* CreateContext with zero device */
    memset(&cc, 0, sizeof(cc));
    cc.hDevice = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                     &cc, sizeof(cc), &cc, sizeof(cc), &br);
    ok(!r, "CreateContext(hDevice=0) should fail\n");

    /* CreateContext with bogus device */
    memset(&cc, 0, sizeof(cc));
    cc.hDevice = 0xDEADBEEF;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                     &cc, sizeof(cc), &cc, sizeof(cc), &br);
    ok(!r, "CreateContext(hDevice=0xDEADBEEF) should fail\n");

    /* DestroyContext with zero handle */
    memset(&dc, 0, sizeof(dc));
    dc.hContext = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                     &dc, sizeof(dc), NULL, 0, &br);
    ok(!r, "DestroyContext(hContext=0) should fail\n");

    /* DestroyContext with bogus handle */
    memset(&dc, 0, sizeof(dc));
    dc.hContext = 0xBAD0CAFE;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                     &dc, sizeof(dc), NULL, 0, &br);
    ok(!r, "DestroyContext(hContext=0xBAD0CAFE) should fail\n");

    /* Render with zero context */
    memset(&rn, 0, sizeof(rn));
    rn.hContext = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_RENDER,
                     &rn, sizeof(rn), &rn, sizeof(rn), &br);
    ok(!r, "Render(hContext=0) should fail\n");

    /* Render with bogus context */
    memset(&rn, 0, sizeof(rn));
    rn.hContext = 0xDEADBEEF;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_RENDER,
                     &rn, sizeof(rn), &rn, sizeof(rn), &br);
    ok(!r, "Render(hContext=0xDEADBEEF) should fail\n");

    /* CreateAllocation with zero device */
    memset(&ca, 0, sizeof(ca));
    ca.hDevice = 0;
    ca.NumAllocations = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEALLOCATION,
                     &ca, sizeof(ca), &ca, sizeof(ca), &br);
    ok(!r, "CreateAllocation(hDevice=0) should fail\n");

    /* Lock with zero device */
    memset(&lk, 0, sizeof(lk));
    lk.hDevice = 0;
    lk.hAllocation = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_LOCK,
                     &lk, sizeof(lk), &lk, sizeof(lk), &br);
    ok(!r, "Lock(hDevice=0) should fail\n");

    /* Lock with bogus device and allocation */
    memset(&lk, 0, sizeof(lk));
    lk.hDevice = 0xDEADBEEF;
    lk.hAllocation = 0xDEADBEEF;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_LOCK,
                     &lk, sizeof(lk), &lk, sizeof(lk), &br);
    ok(!r, "Lock(bogus handles) should fail\n");

    /* Unlock with zero device */
    memset(&ul, 0, sizeof(ul));
    ul.hDevice = 0;
    ul.NumAllocations = 1;
    ul.phAllocations = &zeroHandle;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_UNLOCK,
                     &ul, sizeof(ul), NULL, 0, &br);
    ok(!r, "Unlock(hDevice=0) should fail\n");

    /* DestroyAllocation with zero device */
    memset(&da, 0, sizeof(da));
    da.hDevice = 0;
    da.AllocationCount = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYALLOCATION,
                     &da, sizeof(da), NULL, 0, &br);
    ok(!r, "DestroyAllocation(hDevice=0) should fail\n");

    /* CloseAdapter with zero handle */
    memset(&cla, 0, sizeof(cla));
    cla.hAdapter = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                     &cla, sizeof(cla), NULL, 0, &br);
    ok(!r, "CloseAdapter(hAdapter=0) should fail\n");

    /* CloseAdapter with bogus handle */
    memset(&cla, 0, sizeof(cla));
    cla.hAdapter = 0xBAD0CAFE;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                     &cla, sizeof(cla), NULL, 0, &br);
    ok(!r, "CloseAdapter(hAdapter=0xBAD0CAFE) should fail\n");

    /* QueryAdapterInfo with zero adapter */
    memset(&qi, 0, sizeof(qi));
    qi.hAdapter = 0;
    qi.Type = 3; /* KMTQAITYPE_GETSEGMENTSIZE */
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                     &qi, sizeof(qi), &qi, sizeof(qi), &br);
    ok(!r, "QueryAdapterInfo(hAdapter=0) should fail\n");

    CloseDxg();
}

/*
 * Error paths: wrong IOCTL buffer sizes.
 */
static void Test_ErrorPaths_BufferSizes(void)
{
    D3DKMT_ENUMADAPTERS ea;
    DWORD br;
    BOOL r;
    BYTE smallBuf[4];
    BYTE largeBuf[4096];

    if (!OpenDxg()) return;

    /* EnumAdapters with undersized input buffer */
    memset(smallBuf, 0, sizeof(smallBuf));
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                     smallBuf, sizeof(smallBuf),
                     smallBuf, sizeof(smallBuf), &br);
    ok(!r, "EnumAdapters with 4-byte buffer should fail\n");

    /* EnumAdapters with zero-size buffer */
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                     NULL, 0, NULL, 0, &br);
    ok(!r, "EnumAdapters with NULL/0 buffer should fail\n");

    /* CreateDevice with undersized buffer */
    memset(smallBuf, 0, sizeof(smallBuf));
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     smallBuf, sizeof(smallBuf),
                     smallBuf, sizeof(smallBuf), &br);
    ok(!r, "CreateDevice with 4-byte buffer should fail\n");

    /* CreateContext with undersized buffer */
    memset(smallBuf, 0, sizeof(smallBuf));
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                     smallBuf, sizeof(smallBuf),
                     smallBuf, sizeof(smallBuf), &br);
    ok(!r, "CreateContext with 4-byte buffer should fail\n");

    /* Lock with undersized buffer */
    memset(smallBuf, 0, sizeof(smallBuf));
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_LOCK,
                     smallBuf, sizeof(smallBuf),
                     smallBuf, sizeof(smallBuf), &br);
    ok(!r, "Lock with 4-byte buffer should fail\n");

    /* Render with undersized buffer */
    memset(smallBuf, 0, sizeof(smallBuf));
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_RENDER,
                     smallBuf, sizeof(smallBuf),
                     smallBuf, sizeof(smallBuf), &br);
    ok(!r, "Render with 4-byte buffer should fail\n");

    /* QueryAdapterInfo with oversized buffer (should still work or fail gracefully) */
    memset(largeBuf, 0, sizeof(largeBuf));
    memset(&ea, 0, sizeof(ea));
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                     &ea, sizeof(ea), &ea, sizeof(ea), &br);
    if (r && ea.NumAdapters > 0)
    {
        D3DKMT_QUERYADAPTERINFO qi;
        memset(&qi, 0, sizeof(qi));
        qi.hAdapter = ea.Adapters[0].hAdapter;
        qi.Type = 3; /* KMTQAITYPE_GETSEGMENTSIZE */
        qi.pPrivateDriverData = largeBuf;
        qi.PrivateDriverDataSize = sizeof(largeBuf);

        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                         &qi, sizeof(qi), &qi, sizeof(qi), &br);
        /* Oversized buffer: driver may accept or reject; just don't crash */
        trace("QueryAdapterInfo with oversized buffer: %s (error %lu)\n",
              r ? "succeeded" : "failed", GetLastError());
    }

    CloseDxg();
}

/*
 * Test that double-close of adapter is properly rejected.
 */
static void Test_DoubleCloseAdapter(void)
{
    D3DKMT_ENUMADAPTERS ea;
    D3DKMT_OPENADAPTERFROMLUID oa;
    D3DKMT_CLOSEADAPTER ca;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&ea, 0, sizeof(ea));
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                     &ea, sizeof(ea), &ea, sizeof(ea), &br);
    if (!r || ea.NumAdapters == 0)
    {
        skip("No adapters for DoubleCloseAdapter\n");
        CloseDxg();
        return;
    }

    /* Open adapter */
    memset(&oa, 0, sizeof(oa));
    oa.AdapterLuid = ea.Adapters[0].AdapterLuid;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_OPENADAPTERFROMLUID,
                     &oa, sizeof(oa), &oa, sizeof(oa), &br);
    if (!r)
    {
        skip("OpenAdapterFromLuid failed\n");
        CloseDxg();
        return;
    }

    /* First close should succeed */
    memset(&ca, 0, sizeof(ca));
    ca.hAdapter = oa.hAdapter;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                     &ca, sizeof(ca), NULL, 0, &br);
    ok(r, "First CloseAdapter failed (%lu)\n", GetLastError());

    /* Second close of same handle should fail */
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                     &ca, sizeof(ca), NULL, 0, &br);
    ok(!r, "Double CloseAdapter should fail\n");

    CloseDxg();
}

/*
 * Test creating multiple contexts on one device, then tearing down
 * in reverse order.
 */
static void Test_MultipleContexts(void)
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_CREATECONTEXT cc;
    D3DKMT_DESTROYCONTEXT dc;
    D3DKMT_DESTROYDEVICE dd;
    D3DKMT_HANDLE hContexts[4];
    DWORD br;
    BOOL r;
    ULONG i, j, created;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    if (!r) { skip("CreateDevice failed\n"); CloseDxg(); return; }

    /* Create 4 contexts */
    created = 0;
    for (i = 0; i < 4; i++)
    {
        memset(&cc, 0, sizeof(cc));
        cc.hDevice = cd.hDevice;
        cc.NodeOrdinal = 0;
        cc.EngineAffinity = 1;
        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                         &cc, sizeof(cc), &cc, sizeof(cc), &br);
        if (!r)
        {
            skip("CreateContext #%lu failed\n", i);
            break;
        }
        hContexts[i] = cc.hContext;
        created++;
    }

    /* All created context handles must be unique */
    for (i = 0; i < created; i++)
    {
        ok(hContexts[i] != 0, "Context[%lu] handle is zero\n", i);
        for (j = i + 1; j < created; j++)
        {
            ok(hContexts[i] != hContexts[j],
               "Context[%lu] and Context[%lu] have same handle 0x%08X\n",
               i, j, hContexts[i]);
        }
    }

    /* Destroy in reverse order */
    for (i = created; i > 0; i--)
    {
        memset(&dc, 0, sizeof(dc));
        dc.hContext = hContexts[i - 1];
        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                         &dc, sizeof(dc), NULL, 0, &br);
        ok(r, "DestroyContext[%lu] failed (%lu)\n", i - 1, GetLastError());
    }

    memset(&dd, 0, sizeof(dd));
    dd.hDevice = cd.hDevice;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                 &dd, sizeof(dd), NULL, 0, &br);

    CloseDxg();
}

/*
 * Test that destroying a device implicitly invalidates its contexts.
 * After DestroyDevice, DestroyContext on a formerly-valid context should fail.
 */
static void Test_DeviceDestroyInvalidatesContext(void)
{
    D3DKMT_HANDLE hAdapter;
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_CREATECONTEXT cc;
    D3DKMT_DESTROYCONTEXT dc;
    D3DKMT_DESTROYDEVICE dd;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseDxg(); return; }

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    if (!r) { skip("CreateDevice failed\n"); CloseDxg(); return; }

    memset(&cc, 0, sizeof(cc));
    cc.hDevice = cd.hDevice;
    cc.NodeOrdinal = 0;
    cc.EngineAffinity = 1;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                     &cc, sizeof(cc), &cc, sizeof(cc), &br);
    if (!r)
    {
        skip("CreateContext failed\n");
        memset(&dd, 0, sizeof(dd));
        dd.hDevice = cd.hDevice;
        SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
        CloseDxg();
        return;
    }

    /* Destroy the device first (should cascade-invalidate contexts) */
    memset(&dd, 0, sizeof(dd));
    dd.hDevice = cd.hDevice;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(r, "DestroyDevice failed (%lu)\n", GetLastError());

    /* Now try to destroy the orphaned context -- should fail */
    memset(&dc, 0, sizeof(dc));
    dc.hContext = cc.hContext;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                     &dc, sizeof(dc), NULL, 0, &br);
    ok(!r, "DestroyContext after DestroyDevice should fail (orphaned handle)\n");

    CloseDxg();
}

START_TEST(D3dkmtChain)
{
    Test_AdapterQueryChain();
    Test_DeviceContextRenderChain();
    Test_AllocationLockChain();
    Test_ErrorPaths_NullHandles();
    Test_ErrorPaths_BufferSizes();
    Test_DoubleCloseAdapter();
    Test_MultipleContexts();
    Test_DeviceDestroyInvalidatesContext();
}
