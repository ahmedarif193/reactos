/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     End-to-end WDDM pipeline tests — real usage patterns
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * These tests mirror the EXACT call sequences that a D3D application and
 * DWM exercise against dxgkrnl via the D3DKMT IOCTL interface.
 *
 * Unlike XDDM where GDI draws directly to VRAM, WDDM requires:
 *   1. Adapter discovery (EnumAdapters/OpenAdapter)
 *   2. Device creation (CreateDevice)
 *   3. Allocation management (CreateAllocation/Lock/Unlock)
 *   4. Command submission (Render/Present)
 *   5. Synchronisation (Create/Signal/WaitForSyncObj)
 *   6. Ordered teardown (DestroyDevice/CloseAdapter)
 *
 * Each test models a real-world usage scenario.
 */

#include "precomp.h"

static HANDLE hDxg = INVALID_HANDLE_VALUE;

static BOOL Open(void)
{
    hDxg = OpenDxgKrnl();
    if (hDxg == INVALID_HANDLE_VALUE)
    {
        skip("\\Device\\DxgKrnl not available — WDDM not active\n");
        return FALSE;
    }
    return TRUE;
}

static void Close(void)
{
    if (hDxg != INVALID_HANDLE_VALUE)
    {
        CloseHandle(hDxg);
        hDxg = INVALID_HANDLE_VALUE;
    }
}

/* ====================================================================
 * Scenario 1: D3D Application Lifecycle
 *
 * This is the exact sequence that D3D11CreateDevice() exercises:
 *   EnumAdapters → CreateDevice → [render loop] → DestroyDevice
 *
 * On Windows, DXGI calls EnumAdapters to discover GPUs, then the
 * D3D runtime calls CreateDevice to establish a GPU connection.
 * ==================================================================== */

static void Test_D3DAppLifecycle(void)
{
    D3DKMT_ENUMADAPTERS enumAdapters;
    D3DKMT_CREATEDEVICE createDevice;
    D3DKMT_DESTROYDEVICE destroyDevice;
    DWORD br;
    BOOL r;

    if (!Open()) return;

    /* Step 1: DXGI enumerates adapters */
    memset(&enumAdapters, 0, sizeof(enumAdapters));
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_ENUMADAPTERS,
                     &enumAdapters, sizeof(enumAdapters),
                     &enumAdapters, sizeof(enumAdapters), &br);
    if (!r || enumAdapters.NumAdapters == 0)
    {
        skip("No WDDM adapter available\n");
        Close();
        return;
    }

    D3DKMT_HANDLE hAdapter = enumAdapters.Adapters[0].hAdapter;
    ok(hAdapter != 0, "Adapter handle must be non-zero\n");
    trace("Adapter handle: 0x%08X, LUID: %08X:%08X, Sources: %lu\n",
          hAdapter,
          enumAdapters.Adapters[0].AdapterLuid.HighPart,
          enumAdapters.Adapters[0].AdapterLuid.LowPart,
          enumAdapters.Adapters[0].NumOfSources);

    /* Step 2: D3D runtime creates a device on the adapter */
    memset(&createDevice, 0, sizeof(createDevice));
    createDevice.hAdapter = hAdapter;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_CREATEDEVICE,
                     &createDevice, sizeof(createDevice),
                     &createDevice, sizeof(createDevice), &br);
    if (!r)
    {
        skip("CreateDevice failed (%lu)\n", GetLastError());
        Close();
        return;
    }

    D3DKMT_HANDLE hDevice = createDevice.hDevice;
    ok(hDevice != 0, "Device handle must be non-zero\n");
    ok(hDevice != hAdapter, "Device and adapter handles must differ\n");
    trace("Device handle: 0x%08X\n", hDevice);

    /* Step 3: Application closes — destroy device */
    memset(&destroyDevice, 0, sizeof(destroyDevice));
    destroyDevice.hDevice = hDevice;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_DESTROYDEVICE,
                     &destroyDevice, sizeof(destroyDevice), NULL, 0, &br);
    ok(r, "DestroyDevice should succeed\n");

    /* Step 4: Verify device handle is now invalid */
    memset(&destroyDevice, 0, sizeof(destroyDevice));
    destroyDevice.hDevice = hDevice;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_DESTROYDEVICE,
                     &destroyDevice, sizeof(destroyDevice), NULL, 0, &br);
    ok(!r, "Double-destroy should fail — handle is invalidated\n");

    Close();
}

/* ====================================================================
 * Scenario 2: DWM Composition Path
 *
 * This is the exact sequence DWM executes on startup:
 *   OpenAdapterFromGdiDisplayName → CreateDevice(RequestVSync=1) →
 *   [composition loop with Present] → DestroyDevice → CloseAdapter
 *
 * DWM uses RequestVSync=1 to get VSync-synchronized presents.
 * This is fundamentally different from XDDM where GDI draws directly.
 * ==================================================================== */

static void Test_DwmCompositionPath(void)
{
    D3DKMT_ENUMADAPTERS enumAdapters;
    D3DKMT_CREATEDEVICE createDevice;
    D3DKMT_PRESENT present;
    D3DKMT_DESTROYDEVICE destroyDevice;
    DWORD br;
    BOOL r;

    if (!Open()) return;

    /* Step 1: DWM discovers adapter (like D3DKMTOpenAdapterFromGdiDisplayName) */
    memset(&enumAdapters, 0, sizeof(enumAdapters));
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_ENUMADAPTERS,
                     &enumAdapters, sizeof(enumAdapters),
                     &enumAdapters, sizeof(enumAdapters), &br);
    if (!r || enumAdapters.NumAdapters == 0)
    {
        skip("No adapter for DWM test\n");
        Close();
        return;
    }

    D3DKMT_HANDLE hAdapter = enumAdapters.Adapters[0].hAdapter;

    /* Step 2: DWM creates device with RequestVSync=1 */
    memset(&createDevice, 0, sizeof(createDevice));
    createDevice.hAdapter = hAdapter;
    createDevice.Flags.RequestVSync = 1;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_CREATEDEVICE,
                     &createDevice, sizeof(createDevice),
                     &createDevice, sizeof(createDevice), &br);
    if (!r)
    {
        skip("CreateDevice for DWM failed\n");
        Close();
        return;
    }

    D3DKMT_HANDLE hDevice = createDevice.hDevice;
    ok(hDevice != 0, "DWM device handle non-zero\n");

    /* Step 3: DWM presents a composed frame
     * On real Windows, DWM calls D3DKMTPresent with the composition
     * surface after blitting all windows into it. The present goes
     * through dxgkrnl's present queue. */
    memset(&present, 0, sizeof(present));
    present.hDevice = hDevice;
    present.Flags.Blt = 1; /* CPU blit present (Phase 1 DWM) */
    present.VidPnSourceId = 0;
    present.SrcRect.left = 0;
    present.SrcRect.top = 0;
    present.SrcRect.right = 1024;
    present.SrcRect.bottom = 768;
    present.DstRect = present.SrcRect;

    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_PRESENT,
                     &present, sizeof(present),
                     &present, sizeof(present), &br);
    /* Present may succeed or fail depending on adapter state, but must not crash */
    trace("DWM Present result: %s (error %lu)\n", r ? "OK" : "FAIL", GetLastError());

    /* Step 4: DWM shutdown — clean destroy */
    memset(&destroyDevice, 0, sizeof(destroyDevice));
    destroyDevice.hDevice = hDevice;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_DESTROYDEVICE,
                     &destroyDevice, sizeof(destroyDevice), NULL, 0, &br);
    ok(r, "DWM DestroyDevice should succeed\n");

    Close();
}

/* ====================================================================
 * Scenario 3: Multi-Device Concurrent Access
 *
 * In WDDM, multiple D3D applications can have devices on the same
 * GPU simultaneously. The GPU scheduler multiplexes them.
 * This is impossible in XDDM which has exclusive access.
 * ==================================================================== */

static void Test_MultiDeviceConcurrent(void)
{
    D3DKMT_ENUMADAPTERS enumAdapters;
    D3DKMT_CREATEDEVICE createDevice;
    D3DKMT_DESTROYDEVICE destroyDevice;
    D3DKMT_HANDLE devices[4];
    DWORD br;
    BOOL r;
    ULONG i, j;

    if (!Open()) return;

    memset(&enumAdapters, 0, sizeof(enumAdapters));
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_ENUMADAPTERS,
                     &enumAdapters, sizeof(enumAdapters),
                     &enumAdapters, sizeof(enumAdapters), &br);
    if (!r || enumAdapters.NumAdapters == 0)
    {
        skip("No adapter for multi-device test\n");
        Close();
        return;
    }

    D3DKMT_HANDLE hAdapter = enumAdapters.Adapters[0].hAdapter;

    /* Create 4 devices simultaneously (simulates 4 D3D apps) */
    for (i = 0; i < 4; ++i)
    {
        memset(&createDevice, 0, sizeof(createDevice));
        createDevice.hAdapter = hAdapter;
        r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_CREATEDEVICE,
                         &createDevice, sizeof(createDevice),
                         &createDevice, sizeof(createDevice), &br);
        if (!r)
        {
            skip("CreateDevice %lu failed\n", i);
            while (i-- > 0)
            {
                destroyDevice.hDevice = devices[i];
                SendDxgIoctl(hDxg, IOCTL_D3DKMT_DESTROYDEVICE,
                             &destroyDevice, sizeof(destroyDevice), NULL, 0, &br);
            }
            Close();
            return;
        }
        devices[i] = createDevice.hDevice;
    }

    /* All handles must be unique (WDDM assigns distinct handles per device) */
    for (i = 0; i < 4; ++i)
    {
        ok(devices[i] != 0, "Device[%lu] handle non-zero\n", i);
        for (j = i + 1; j < 4; ++j)
        {
            ok(devices[i] != devices[j],
               "Devices[%lu] and [%lu] must have distinct handles\n", i, j);
        }
    }

    /* All 4 devices must be independently destroyable */
    for (i = 0; i < 4; ++i)
    {
        memset(&destroyDevice, 0, sizeof(destroyDevice));
        destroyDevice.hDevice = devices[i];
        r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_DESTROYDEVICE,
                         &destroyDevice, sizeof(destroyDevice), NULL, 0, &br);
        ok(r, "DestroyDevice[%lu] should succeed\n", i);
    }

    Close();
}

/* ====================================================================
 * Scenario 4: Adapter Query — Segment & Version Info
 *
 * DXGI queries adapter properties to select the best GPU.
 * These queries go through D3DKMTQueryAdapterInfo.
 * ==================================================================== */

static void Test_AdapterQuery(void)
{
    D3DKMT_ENUMADAPTERS enumAdapters;
    D3DKMT_QUERYADAPTERINFO queryInfo;
    D3DKMT_SEGMENTSIZEINFO segInfo;
    D3DKMT_DRIVERVERSION driverVersion;
    DWORD br;
    BOOL r;

    if (!Open()) return;

    memset(&enumAdapters, 0, sizeof(enumAdapters));
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_ENUMADAPTERS,
                     &enumAdapters, sizeof(enumAdapters),
                     &enumAdapters, sizeof(enumAdapters), &br);
    if (!r || enumAdapters.NumAdapters == 0)
    {
        skip("No adapter for query test\n");
        Close();
        return;
    }

    D3DKMT_HANDLE hAdapter = enumAdapters.Adapters[0].hAdapter;

    /* Query segment sizes — DXGI uses this to report VRAM to applications */
    memset(&queryInfo, 0, sizeof(queryInfo));
    memset(&segInfo, 0, sizeof(segInfo));
    queryInfo.hAdapter = hAdapter;
    queryInfo.Type = KMTQAITYPE_GETSEGMENTSIZE;
    queryInfo.pPrivateDriverData = &segInfo;
    queryInfo.PrivateDriverDataSize = sizeof(segInfo);

    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_QUERYADAPTERINFO,
                     &queryInfo, sizeof(queryInfo),
                     &queryInfo, sizeof(queryInfo), &br);
    if (r)
    {
        trace("Segments: Dedicated VRAM=%llu, Dedicated Sys=%llu, Shared Sys=%llu\n",
              segInfo.DedicatedVideoMemorySize,
              segInfo.DedicatedSystemMemorySize,
              segInfo.SharedSystemMemorySize);

        /* At least one segment type should report non-zero memory */
        ok(segInfo.DedicatedVideoMemorySize > 0 ||
           segInfo.DedicatedSystemMemorySize > 0 ||
           segInfo.SharedSystemMemorySize > 0,
           "Adapter should report some memory\n");
    }

    /* Query driver version — DXGI reports this to D3D feature-level selection */
    memset(&queryInfo, 0, sizeof(queryInfo));
    driverVersion = 0;
    queryInfo.hAdapter = hAdapter;
    queryInfo.Type = KMTQAITYPE_DRIVERVERSION;
    queryInfo.pPrivateDriverData = &driverVersion;
    queryInfo.PrivateDriverDataSize = sizeof(driverVersion);

    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_QUERYADAPTERINFO,
                     &queryInfo, sizeof(queryInfo),
                     &queryInfo, sizeof(queryInfo), &br);
    if (r)
    {
        trace("Driver version: %d\n", driverVersion);
        /* WDDM 1.0 = 1000 */
        ok(driverVersion >= KMT_DRIVERVERSION_WDDM_1_0,
           "Driver version should be at least WDDM 1.0 (%d), got %d\n",
           KMT_DRIVERVERSION_WDDM_1_0, driverVersion);
    }

    Close();
}

/* ====================================================================
 * Scenario 5: Display Mode Enumeration
 *
 * The display control panel and DXGI enumerate supported display modes.
 * In WDDM this goes through VidPN, not IOCTL_VIDEO_QUERY_AVAIL_MODES.
 * ==================================================================== */

static void Test_DisplayModeEnum(void)
{
    D3DKMT_ENUMADAPTERS enumAdapters;
    D3DKMT_GETDISPLAYMODELIST getModes;
    DWORD br;
    BOOL r;

    if (!Open()) return;

    memset(&enumAdapters, 0, sizeof(enumAdapters));
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_ENUMADAPTERS,
                     &enumAdapters, sizeof(enumAdapters),
                     &enumAdapters, sizeof(enumAdapters), &br);
    if (!r || enumAdapters.NumAdapters == 0)
    {
        skip("No adapter for display mode test\n");
        Close();
        return;
    }

    /* Get mode count first (pModeList=NULL) */
    memset(&getModes, 0, sizeof(getModes));
    getModes.hAdapter = enumAdapters.Adapters[0].hAdapter;
    getModes.VidPnSourceId = 0;

    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_GETDISPLAYMODELIST,
                     &getModes, sizeof(getModes),
                     &getModes, sizeof(getModes), &br);
    if (r)
    {
        trace("Display supports %lu modes on VidPn source 0\n", getModes.ModeCount);
        ok(getModes.ModeCount >= 1,
           "WDDM adapter must support at least 1 display mode\n");
    }

    Close();
}

/* ====================================================================
 * Scenario 6: Invalid Handle Rejection
 *
 * WDDM handle validation must reject stale, forged, and NULL handles.
 * This is a security boundary: user-mode code should not be able to
 * crash the kernel with invalid handles.
 * ==================================================================== */

static void Test_HandleRejection(void)
{
    D3DKMT_CREATEDEVICE createDevice;
    D3DKMT_DESTROYDEVICE destroyDevice;
    D3DKMT_PRESENT present;
    D3DKMT_CLOSEADAPTER closeAdapter;
    DWORD br;
    BOOL r;

    if (!Open()) return;

    /* Forged adapter handle → CreateDevice must fail */
    memset(&createDevice, 0, sizeof(createDevice));
    createDevice.hAdapter = 0xDEADBEEF;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_CREATEDEVICE,
                     &createDevice, sizeof(createDevice),
                     &createDevice, sizeof(createDevice), &br);
    ok(!r, "CreateDevice with forged adapter must fail\n");

    /* Zero adapter → CreateDevice must fail */
    createDevice.hAdapter = 0;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_CREATEDEVICE,
                     &createDevice, sizeof(createDevice),
                     &createDevice, sizeof(createDevice), &br);
    ok(!r, "CreateDevice with zero adapter must fail\n");

    /* Forged device handle → DestroyDevice must fail */
    memset(&destroyDevice, 0, sizeof(destroyDevice));
    destroyDevice.hDevice = 0xCAFEBABE;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_DESTROYDEVICE,
                     &destroyDevice, sizeof(destroyDevice), NULL, 0, &br);
    ok(!r, "DestroyDevice with forged handle must fail\n");

    /* Forged device → Present must fail (not crash) */
    memset(&present, 0, sizeof(present));
    present.hDevice = 0xBAD0CAFE;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_PRESENT,
                     &present, sizeof(present),
                     &present, sizeof(present), &br);
    ok(!r, "Present with forged device must fail\n");

    /* Forged adapter → CloseAdapter must fail */
    memset(&closeAdapter, 0, sizeof(closeAdapter));
    closeAdapter.hAdapter = 0xF00DF00D;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_CLOSEADAPTER,
                     &closeAdapter, sizeof(closeAdapter), NULL, 0, &br);
    ok(!r, "CloseAdapter with forged handle must fail\n");

    Close();
}

/* ====================================================================
 * Scenario 7: Adapter Re-open After Close
 *
 * An application can open, close, and re-open an adapter. The adapter
 * handle from the second open must work correctly.
 * ==================================================================== */

static void Test_AdapterReopen(void)
{
    D3DKMT_ENUMADAPTERS enumAdapters;
    D3DKMT_OPENADAPTERFROMLUID openAdapter;
    D3DKMT_CLOSEADAPTER closeAdapter;
    D3DKMT_CREATEDEVICE createDevice;
    D3DKMT_DESTROYDEVICE destroyDevice;
    DWORD br;
    BOOL r;

    if (!Open()) return;

    memset(&enumAdapters, 0, sizeof(enumAdapters));
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_ENUMADAPTERS,
                     &enumAdapters, sizeof(enumAdapters),
                     &enumAdapters, sizeof(enumAdapters), &br);
    if (!r || enumAdapters.NumAdapters == 0)
    {
        skip("No adapter for re-open test\n");
        Close();
        return;
    }

    LUID luid = enumAdapters.Adapters[0].AdapterLuid;

    /* Open by LUID */
    memset(&openAdapter, 0, sizeof(openAdapter));
    openAdapter.AdapterLuid = luid;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_OPENADAPTERFROMLUID,
                     &openAdapter, sizeof(openAdapter),
                     &openAdapter, sizeof(openAdapter), &br);
    if (!r)
    {
        skip("OpenAdapterFromLuid failed\n");
        Close();
        return;
    }

    D3DKMT_HANDLE hAdapter1 = openAdapter.hAdapter;

    /* Close it */
    memset(&closeAdapter, 0, sizeof(closeAdapter));
    closeAdapter.hAdapter = hAdapter1;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_CLOSEADAPTER,
                     &closeAdapter, sizeof(closeAdapter), NULL, 0, &br);
    ok(r, "First close should succeed\n");

    /* Re-open the same adapter */
    memset(&openAdapter, 0, sizeof(openAdapter));
    openAdapter.AdapterLuid = luid;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_OPENADAPTERFROMLUID,
                     &openAdapter, sizeof(openAdapter),
                     &openAdapter, sizeof(openAdapter), &br);
    ok(r, "Re-open should succeed\n");

    D3DKMT_HANDLE hAdapter2 = openAdapter.hAdapter;
    ok(hAdapter2 != 0, "Re-opened handle should be non-zero\n");

    /* Create a device on the re-opened adapter to prove it works */
    memset(&createDevice, 0, sizeof(createDevice));
    createDevice.hAdapter = hAdapter2;
    r = SendDxgIoctl(hDxg, IOCTL_D3DKMT_CREATEDEVICE,
                     &createDevice, sizeof(createDevice),
                     &createDevice, sizeof(createDevice), &br);
    if (r)
    {
        ok(createDevice.hDevice != 0, "Device on re-opened adapter\n");
        destroyDevice.hDevice = createDevice.hDevice;
        SendDxgIoctl(hDxg, IOCTL_D3DKMT_DESTROYDEVICE,
                     &destroyDevice, sizeof(destroyDevice), NULL, 0, &br);
    }

    Close();
}

START_TEST(WddmPipeline)
{
    Test_D3DAppLifecycle();
    Test_DwmCompositionPath();
    Test_MultiDeviceConcurrent();
    Test_AdapterQuery();
    Test_DisplayModeEnum();
    Test_HandleRejection();
    Test_AdapterReopen();
}
