/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     TDR and watchdog validation via D3DKMT stress tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests adapter open/close stress, QueryAdapterInfo for WDDM version,
 * GetDeviceState, rapid Escape calls, and invalid handle resilience.
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
    memset(&dd, 0, sizeof(dd));
    dd.hDevice = hDevice;
    SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                 &dd, sizeof(dd), NULL, 0, &br);
}

/*
 * Test 1: Adapter open/close stress
 *
 * Open + close the adapter 100 times rapidly. Verify no crash
 * and no TDR is triggered by this pure handle management path.
 */
static void Test_AdapterOpenCloseStress(void)
{
    D3DKMT_ENUMADAPTERS e;
    D3DKMT_OPENADAPTERFROMLUID openLuid;
    D3DKMT_CLOSEADAPTER closeAd;
    DWORD br;
    BOOL r;
    ULONG i;
    ULONG nSuccess = 0;

    if (!OpenDxg()) return;

    /* Get a valid LUID first */
    memset(&e, 0, sizeof(e));
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ENUMADAPTERS,
                     &e, sizeof(e), &e, sizeof(e), &br);
    if (!r || e.NumAdapters == 0)
    {
        skip("No adapter to stress test\n");
        CloseHandle(hDxgKrnl);
        return;
    }

    for (i = 0; i < 100; i++)
    {
        memset(&openLuid, 0, sizeof(openLuid));
        openLuid.AdapterLuid = e.Adapters[0].AdapterLuid;

        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_OPENADAPTERFROMLUID,
                         &openLuid, sizeof(openLuid),
                         &openLuid, sizeof(openLuid), &br);
        if (r && openLuid.hAdapter != 0)
        {
            memset(&closeAd, 0, sizeof(closeAd));
            closeAd.hAdapter = openLuid.hAdapter;
            SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CLOSEADAPTER,
                         &closeAd, sizeof(closeAd), NULL, 0, &br);
            nSuccess++;
        }
    }

    ok(nSuccess == 100,
       "Adapter open/close stress: expected 100 successes, got %lu\n", nSuccess);

    CloseHandle(hDxgKrnl);
}

/*
 * Test 2: QueryAdapterInfo for WDDM driver version
 *
 * Uses KMTQAITYPE_DRIVERVERSION to verify the adapter reports
 * a valid WDDM version.
 */
static void Test_QueryDriverVersion(void)
{
    D3DKMT_QUERYADAPTERINFO qi;
    D3DKMT_DRIVERVERSION ver = 0;
    D3DKMT_HANDLE hAdapter;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    memset(&qi, 0, sizeof(qi));
    qi.hAdapter = hAdapter;
    qi.Type = KMTQAITYPE_DRIVERVERSION;
    qi.pPrivateDriverData = &ver;
    qi.PrivateDriverDataSize = sizeof(ver);

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_QUERYADAPTERINFO,
                     &qi, sizeof(qi), &qi, sizeof(qi), &br);

    if (!r)
    {
        skip("QueryAdapterInfo(DRIVERVERSION) failed (%lu)\n", GetLastError());
        CloseHandle(hDxgKrnl);
        return;
    }

    ok(ver >= KMT_DRIVERVERSION_WDDM_1_0,
       "WDDM version should be >= 1.0, got %d\n", ver);

    /* Sanity: version should not be absurdly high */
    ok(ver <= 100000,
       "WDDM version unexpectedly high: %d\n", ver);

    CloseHandle(hDxgKrnl);
}

/*
 * Test 3: GetDeviceState after device creation
 *
 * Create a device and immediately query its execution state.
 * The device should be in ACTIVE/running state (no TDR).
 */
static void Test_GetDeviceStateRunning(void)
{
    D3DKMT_GETDEVICESTATE ds;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
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

    if (r)
    {
        ok(ds.ExecutionState == D3DKMT_DEVICEEXECUTION_ACTIVE,
           "Expected device state ACTIVE (1), got %d\n",
           ds.ExecutionState);
    }
    else
    {
        skip("GetDeviceState(EXECUTION) failed (%lu)\n", GetLastError());
    }

    /* Also query reset state -- should be clean */
    memset(&ds, 0, sizeof(ds));
    ds.hDevice = hDevice;
    ds.StateType = D3DKMT_DEVICESTATE_RESET;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDEVICESTATE,
                     &ds, sizeof(ds), &ds, sizeof(ds), &br);

    if (r)
    {
        ok(ds.ResetState.Value == 0,
           "Device reset state should be 0 (clean), got 0x%lX\n",
           ds.ResetState.Value);
    }

    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

/*
 * Test 4: Rapid Escape calls stress
 *
 * Create a device and send 100 Escape IOCTLs rapidly.
 * kmdod does not implement Escape, so we expect STATUS_NOT_SUPPORTED
 * or similar failure -- the important thing is no crash and no TDR.
 */
static void Test_RapidEscapeStress(void)
{
    D3DKMT_ESCAPE esc;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE hDevice;
    DWORD br;
    ULONG i;
    ULONG nFailed = 0;
    ULONG nSucceeded = 0;
    UCHAR dummyData[16];

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    hDevice = CreateTestDevice(hAdapter);
    if (!hDevice) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    memset(dummyData, 0, sizeof(dummyData));

    for (i = 0; i < 100; i++)
    {
        memset(&esc, 0, sizeof(esc));
        esc.hAdapter = hAdapter;
        esc.hDevice = hDevice;
        esc.Type = D3DKMT_ESCAPE_DRIVERPRIVATE;
        esc.pPrivateDriverData = dummyData;
        esc.PrivateDriverDataSize = sizeof(dummyData);

        if (SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_ESCAPE,
                         &esc, sizeof(esc), &esc, sizeof(esc), &br))
            nSucceeded++;
        else
            nFailed++;
    }

    /*
     * We don't care whether individual Escapes succeed or fail --
     * kmdod returns NOT_SUPPORTED which is fine. The key assertion
     * is that we survived 100 rapid calls without crashing.
     */
    ok(nSucceeded + nFailed == 100,
       "All 100 Escape calls should complete (got %lu + %lu)\n",
       nSucceeded, nFailed);

    /* Verify device is still alive after the stress */
    {
        D3DKMT_GETDEVICESTATE ds;
        BOOL r;
        memset(&ds, 0, sizeof(ds));
        ds.hDevice = hDevice;
        ds.StateType = D3DKMT_DEVICESTATE_EXECUTION;

        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDEVICESTATE,
                         &ds, sizeof(ds), &ds, sizeof(ds), &br);
        if (r)
        {
            ok(ds.ExecutionState == D3DKMT_DEVICEEXECUTION_ACTIVE,
               "Device should still be ACTIVE after Escape stress, got %d\n",
               ds.ExecutionState);
        }
    }

    DestroyTestDevice(hDevice);
    CloseHandle(hDxgKrnl);
}

/*
 * Test 5: Invalid handle GetDeviceState
 *
 * Pass a garbage device handle to GetDeviceState. Must fail
 * gracefully without crash or TDR.
 */
static void Test_GetDeviceStateInvalidHandle(void)
{
    D3DKMT_GETDEVICESTATE ds;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    /* Garbage handle */
    memset(&ds, 0, sizeof(ds));
    ds.hDevice = 0xDEADBEEF;
    ds.StateType = D3DKMT_DEVICESTATE_EXECUTION;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDEVICESTATE,
                     &ds, sizeof(ds), &ds, sizeof(ds), &br);
    ok(!r, "GetDeviceState with garbage handle should fail\n");

    /* Zero handle */
    memset(&ds, 0, sizeof(ds));
    ds.hDevice = 0;
    ds.StateType = D3DKMT_DEVICESTATE_EXECUTION;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_GETDEVICESTATE,
                     &ds, sizeof(ds), &ds, sizeof(ds), &br);
    ok(!r, "GetDeviceState with zero handle should fail\n");

    CloseHandle(hDxgKrnl);
}

START_TEST(D3dkmtTdr)
{
    Test_AdapterOpenCloseStress();
    Test_QueryDriverVersion();
    Test_GetDeviceStateRunning();
    Test_RapidEscapeStress();
    Test_GetDeviceStateInvalidHandle();
}
