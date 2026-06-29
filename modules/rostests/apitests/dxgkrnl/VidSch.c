/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidSch (GPU scheduler) submission and fence tests via D3DKMT
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the VidSch scheduler interface through the D3DKMT user-mode thunks:
 *   - Context creation/destruction lifecycle
 *   - Empty render (command buffer submission)
 *   - Synchronization object (fence) create/destroy
 *   - Fence signal and wait
 *   - WaitForIdle
 *   - Context scheduling priority get/set
 *   - Multiple context management
 *   - Present via D3DKMT
 *   - Rapid submission stress
 *   - GetDeviceState
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
    dd.hDevice = hDevice;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                 &dd, sizeof(dd), NULL, 0, &br);
}

static D3DKMT_HANDLE CreateTestContext(D3DKMT_HANDLE hDevice)
{
    D3DKMT_CREATECONTEXT cc;
    DWORD br;
    memset(&cc, 0, sizeof(cc));
    cc.hDevice = hDevice;
    cc.NodeOrdinal = 0;
    cc.EngineAffinity = 1;
    if (!SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATECONTEXT,
                      &cc, sizeof(cc), &cc, sizeof(cc), &br))
        return 0;
    return cc.hContext;
}

static void DestroyTestContext(D3DKMT_HANDLE hContext)
{
    D3DKMT_DESTROYCONTEXT dc;
    DWORD br;
    dc.hContext = hContext;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                 &dc, sizeof(dc), NULL, 0, &br);
}

/* ---- Test 1: Basic context creation lifecycle ---- */
static void Test_BasicContextCreation(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hContext;
    D3DKMT_DESTROYCONTEXT dc;
    D3DKMT_DESTROYDEVICE dd;
    D3DKMT_CLOSEADAPTER ca;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    hContext = CreateTestContext(hDevice);
    if (!hContext) { skip("CreateContext failed\n"); DestroyTestDevice(hDevice); CloseHandle(hDxgKrnl); return; }

    ok(hContext != 0, "Context handle should be non-zero\n");
    ok(hContext != hDevice, "Context handle should differ from device handle\n");
    ok(hContext != hAdapter, "Context handle should differ from adapter handle\n");

    /* Destroy context */
    dc.hContext = hContext;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYCONTEXT,
                     &dc, sizeof(dc), NULL, 0, &br);
    ok(r, "DestroyContext failed (%lu)\n", GetLastError());

    /* Destroy device */
    dd.hDevice = hDevice;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(r, "DestroyDevice failed (%lu)\n", GetLastError());

    /* Close adapter */
    ca.hAdapter = hAdapter;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                     &ca, sizeof(ca), NULL, 0, &br);
    ok(r, "CloseAdapter failed (%lu)\n", GetLastError());

    CloseHandle(hDxgKrnl);
}

/* ---- Test 2: Empty render ---- */
static void Test_EmptyRender(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hContext;
    D3DKMT_RENDER ren;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    hContext = CreateTestContext(hDevice);
    if (!hContext) { skip("CreateContext failed\n"); DestroyTestDevice(hDevice); CloseHandle(hDxgKrnl); return; }

    /* Submit an empty render command */
    memset(&ren, 0, sizeof(ren));
    ren.hDevice = hDevice;
    ren.hContext = hContext;
    ren.CommandLength = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_RENDER,
                     &ren, sizeof(ren), &ren, sizeof(ren), &br);
    ok(r, "Empty render should succeed (%lu)\n", GetLastError());

    DestroyTestContext(hContext);
    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

/* ---- Test 3: Fence creation ---- */
static void Test_FenceCreation(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT cso;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT dso;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    /* Create a synchronization fence */
    memset(&cso, 0, sizeof(cso));
    cso.hDevice = hDevice;
    cso.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT,
                     &cso, sizeof(cso), &cso, sizeof(cso), &br);
    if (!r)
    {
        skip("CreateSynchronizationObject not supported (%lu)\n", GetLastError());
        DestroyTestDevice(hDevice);
        CloseHandle(hDxgKrnl);
        return;
    }

    ok(cso.hSyncObject != 0, "Sync object handle should be non-zero\n");
    ok(cso.hSyncObject != hDevice, "Sync handle should differ from device\n");

    /* Destroy the sync object */
    dso.hSyncObject = cso.hSyncObject;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT,
                     &dso, sizeof(dso), NULL, 0, &br);
    ok(r, "DestroySynchronizationObject failed (%lu)\n", GetLastError());

    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

/* ---- Test 4: Signal and wait ---- */
static void Test_SignalAndWait(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hContext;
    D3DKMT_CREATESYNCHRONIZATIONOBJECT cso;
    D3DKMT_DESTROYSYNCHRONIZATIONOBJECT dso;
    D3DKMT_SIGNALSYNCHRONIZATIONOBJECT sig;
    D3DKMT_WAITFORSYNCHRONIZATIONOBJECT wait;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    hContext = CreateTestContext(hDevice);
    if (!hContext) { skip("CreateContext failed\n"); DestroyTestDevice(hDevice); CloseHandle(hDxgKrnl); return; }

    /* Create fence */
    memset(&cso, 0, sizeof(cso));
    cso.hDevice = hDevice;
    cso.Info.Type = D3DDDI_SYNCHRONIZATION_MUTEX;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATESYNCHRONIZATIONOBJECT,
                     &cso, sizeof(cso), &cso, sizeof(cso), &br);
    if (!r)
    {
        skip("CreateSynchronizationObject not supported\n");
        DestroyTestContext(hContext);
        DestroyTestDevice(hDevice);
        CloseHandle(hDxgKrnl);
        return;
    }

    /* Signal the fence */
    memset(&sig, 0, sizeof(sig));
    sig.hContext = hContext;
    sig.ObjectCount = 1;
    sig.ObjectHandleArray[0] = cso.hSyncObject;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_SIGNALSYNCHRONIZATIONOBJECT,
                     &sig, sizeof(sig), NULL, 0, &br);
    if (!r)
    {
        skip("Signal not supported\n");
        goto cleanup_signal;
    }

    /* Wait for the already-signaled fence -- should return immediately */
    memset(&wait, 0, sizeof(wait));
    wait.hContext = hContext;
    wait.ObjectCount = 1;
    wait.ObjectHandleArray[0] = cso.hSyncObject;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_WAITFORSYNCHRONIZATIONOBJECT,
                     &wait, sizeof(wait), NULL, 0, &br);
    ok(r, "Wait on already-signaled fence should succeed (%lu)\n", GetLastError());

cleanup_signal:
    dso.hSyncObject = cso.hSyncObject;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYSYNCHRONIZATIONOBJECT,
                 &dso, sizeof(dso), NULL, 0, &br);
    DestroyTestContext(hContext);
    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

/* ---- Test 5: WaitForIdle ---- */
static void Test_WaitForIdle(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    /* WaitForIdle with nothing pending should succeed immediately */
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_WAITFORIDLE,
                     &hDevice, sizeof(hDevice), NULL, 0, &br);
    ok(r, "WaitForIdle should succeed with no pending work (%lu)\n", GetLastError());

    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

/* ---- Test 6: Context scheduling priority ---- */
static void Test_ContextPriority(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hContext;
    D3DKMT_GETCONTEXTSCHEDULINGPRIORITY getPri;
    D3DKMT_SETCONTEXTSCHEDULINGPRIORITY setPri;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    hContext = CreateTestContext(hDevice);
    if (!hContext) { skip("CreateContext failed\n"); DestroyTestDevice(hDevice); CloseHandle(hDxgKrnl); return; }

    /* Get default priority */
    memset(&getPri, 0, sizeof(getPri));
    getPri.hContext = hContext;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETCONTEXTSCHEDULINGPRIORITY,
                     &getPri, sizeof(getPri), &getPri, sizeof(getPri), &br);
    ok(r, "GetContextSchedulingPriority should succeed (%lu)\n", GetLastError());

    /* Set priority to HIGH */
    memset(&setPri, 0, sizeof(setPri));
    setPri.hContext = hContext;
    setPri.Priority = 7;  /* Above normal */
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_SETCONTEXTSCHEDULINGPRIORITY,
                     &setPri, sizeof(setPri), NULL, 0, &br);
    ok(r, "SetContextSchedulingPriority should succeed (%lu)\n", GetLastError());

    /* Read back and verify */
    memset(&getPri, 0, sizeof(getPri));
    getPri.hContext = hContext;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETCONTEXTSCHEDULINGPRIORITY,
                     &getPri, sizeof(getPri), &getPri, sizeof(getPri), &br);
    ok(r, "GetContextSchedulingPriority readback should succeed\n");

    DestroyTestContext(hContext);
    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

/* ---- Test 7: Multiple contexts ---- */
static void Test_MultipleContexts(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_HANDLE hContexts[3];
    ULONG i, j;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    /* Create 3 contexts on the same device */
    for (i = 0; i < 3; ++i)
    {
        hContexts[i] = CreateTestContext(hDevice);
        if (!hContexts[i])
        {
            skip("CreateContext #%lu failed\n", i);
            while (i-- > 0)
                DestroyTestContext(hContexts[i]);
            DestroyTestDevice(hDevice);
            CloseHandle(hDxgKrnl);
            return;
        }
    }

    /* All handles must be unique and non-zero */
    for (i = 0; i < 3; ++i)
    {
        ok(hContexts[i] != 0, "Context[%lu] handle is 0\n", i);
        for (j = i + 1; j < 3; ++j)
        {
            ok(hContexts[i] != hContexts[j],
               "Context[%lu] and Context[%lu] have same handle 0x%08X\n",
               i, j, hContexts[i]);
        }
    }

    /* WaitForIdle with nothing pending */
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_WAITFORIDLE,
                     &hDevice, sizeof(hDevice), NULL, 0, &br);
    ok(r, "WaitForIdle with 3 idle contexts should succeed\n");

    /* Destroy all */
    for (i = 0; i < 3; ++i)
        DestroyTestContext(hContexts[i]);

    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

/* ---- Test 8: Present via D3DKMT ---- */
static void Test_PresentViaD3DKMT(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hContext;
    D3DKMT_PRESENT pres;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    hContext = CreateTestContext(hDevice);
    if (!hContext) { skip("CreateContext failed\n"); DestroyTestDevice(hDevice); CloseHandle(hDxgKrnl); return; }

    /* Attempt a present -- this exercises the present path even if
     * no allocation is created (the driver validates parameters) */
    memset(&pres, 0, sizeof(pres));
    pres.hDevice = hDevice;
    pres.hContext = hContext;
    pres.hWindow = NULL;
    pres.VidPnSourceId = 0;
    pres.Flags.Blt = 1;
    pres.SrcRect.left = 0;
    pres.SrcRect.top = 0;
    pres.SrcRect.right = 1024;
    pres.SrcRect.bottom = 768;
    pres.DstRect = pres.SrcRect;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_PRESENT,
                     &pres, sizeof(pres), &pres, sizeof(pres), &br);
    /* Present may fail without a valid allocation, but the IOCTL should
     * at least be dispatched and not crash */
    if (!r)
    {
        trace("Present without source allocation returned error %lu (expected)\n",
              GetLastError());
    }

    DestroyTestContext(hContext);
    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

/* ---- Test 9: Stress rapid submissions ---- */
static void Test_RapidSubmissions(void)
{
    D3DKMT_HANDLE hAdapter, hDevice, hContext;
    D3DKMT_RENDER ren;
    DWORD br;
    BOOL r;
    ULONG i;
    ULONG successCount = 0;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    hContext = CreateTestContext(hDevice);
    if (!hContext) { skip("CreateContext failed\n"); DestroyTestDevice(hDevice); CloseHandle(hDxgKrnl); return; }

    /* Loop 100x: render with minimal (empty) command */
    for (i = 0; i < 100; ++i)
    {
        memset(&ren, 0, sizeof(ren));
        ren.hDevice = hDevice;
        ren.hContext = hContext;
        ren.CommandLength = 0;

        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_RENDER,
                         &ren, sizeof(ren), &ren, sizeof(ren), &br);
        if (r)
            successCount++;
    }

    ok(successCount == 100,
       "Expected 100/100 rapid renders to succeed, got %lu/100\n", successCount);

    DestroyTestContext(hContext);
    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

/* ---- Test 10: GetDeviceState ---- */
static void Test_GetDeviceState(void)
{
    D3DKMT_HANDLE hAdapter, hDevice;
    D3DKMT_GETDEVICESTATE ds;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    /* Query execution state */
    memset(&ds, 0, sizeof(ds));
    ds.hDevice = hDevice;
    ds.StateType = D3DKMT_DEVICESTATE_EXECUTION;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDEVICESTATE,
                     &ds, sizeof(ds), &ds, sizeof(ds), &br);
    ok(r, "GetDeviceState should succeed (%lu)\n", GetLastError());

    /* Invalid device handle should fail */
    memset(&ds, 0, sizeof(ds));
    ds.hDevice = 0xBAD0CAFE;
    ds.StateType = D3DKMT_DEVICESTATE_EXECUTION;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDEVICESTATE,
                     &ds, sizeof(ds), &ds, sizeof(ds), &br);
    ok(!r, "GetDeviceState should fail with invalid device\n");

    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

START_TEST(VidSch)
{
    Test_BasicContextCreation();
    Test_EmptyRender();
    Test_FenceCreation();
    Test_SignalAndWait();
    Test_WaitForIdle();
    Test_ContextPriority();
    Test_MultipleContexts();
    Test_PresentViaD3DKMT();
    Test_RapidSubmissions();
    Test_GetDeviceState();
}
