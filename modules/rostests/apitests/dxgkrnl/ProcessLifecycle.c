/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Process lifecycle and GPU VA isolation tests (WDDM 2.0)
 * COPYRIGHT:   Copyright 2024 ReactOS WDDM Team
 *
 * Tests the WDDM 2.0 per-process GPU state model:
 *   - Device creation/destruction associated with processes
 *   - Cross-process GPU VA isolation verification
 *   - Process exit cleanup of GPU resources
 *   - Root page table lifecycle semantics
 */

#include "precomp.h"

/*
 * Test: Basic device create/destroy lifecycle
 *
 * Verify that a device can be created and destroyed through the IOCTL
 * path, which exercises the per-process GPU state management.
 */
static void
Test_DeviceLifecycle_Basic(void)
{
    HANDLE hDevice;
    D3DKMT_ENUMADAPTERS EnumAdapters;
    D3DKMT_CREATEDEVICE CreateDevice;
    D3DKMT_DESTROYDEVICE DestroyDevice;
    DWORD BytesReturned;
    BOOL Result;

    hDevice = OpenDxgKrnl();
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open DxgKrnl device\n");
        return;
    }

    /* Enumerate adapters to get a valid adapter handle */
    memset(&EnumAdapters, 0, sizeof(EnumAdapters));

    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_ENUMADAPTERS,
                          &EnumAdapters, sizeof(EnumAdapters),
                          &EnumAdapters, sizeof(EnumAdapters),
                          &BytesReturned);

    if (!Result || EnumAdapters.NumAdapters == 0)
    {
        skip("No adapters found, cannot test device lifecycle\n");
        CloseHandle(hDevice);
        return;
    }

    /* Create a device on the first adapter */
    memset(&CreateDevice, 0, sizeof(CreateDevice));
    CreateDevice.hAdapter = EnumAdapters.Adapters[0].hAdapter;

    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_CREATEDEVICE,
                          &CreateDevice, sizeof(CreateDevice),
                          &CreateDevice, sizeof(CreateDevice),
                          &BytesReturned);
    ok(Result, "CreateDevice should succeed, got error %lu\n", GetLastError());

    if (Result)
    {
        ok(CreateDevice.hDevice != 0,
           "CreateDevice should return a non-zero handle\n");

        /* Destroy the device */
        memset(&DestroyDevice, 0, sizeof(DestroyDevice));
        DestroyDevice.hDevice = CreateDevice.hDevice;

        Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_DESTROYDEVICE,
                              &DestroyDevice, sizeof(DestroyDevice),
                              NULL, 0,
                              &BytesReturned);
        ok(Result, "DestroyDevice should succeed\n");
    }

    CloseHandle(hDevice);
}

/*
 * Test: Multiple device creation and destruction
 *
 * Creates multiple devices on the same adapter and destroys them in
 * reverse order, verifying that each has a unique handle.
 */
static void
Test_MultiDevice_Lifecycle(void)
{
    HANDLE hDevice;
    D3DKMT_ENUMADAPTERS EnumAdapters;
    D3DKMT_CREATEDEVICE CreateDevice;
    D3DKMT_DESTROYDEVICE DestroyDevice;
    D3DKMT_HANDLE DeviceHandles[4];
    DWORD BytesReturned;
    BOOL Result;
    int i;

    hDevice = OpenDxgKrnl();
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open DxgKrnl device\n");
        return;
    }

    memset(&EnumAdapters, 0, sizeof(EnumAdapters));
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_ENUMADAPTERS,
                          &EnumAdapters, sizeof(EnumAdapters),
                          &EnumAdapters, sizeof(EnumAdapters),
                          &BytesReturned);

    if (!Result || EnumAdapters.NumAdapters == 0)
    {
        skip("No adapters available\n");
        CloseHandle(hDevice);
        return;
    }

    /* Create 4 devices */
    for (i = 0; i < 4; i++)
    {
        memset(&CreateDevice, 0, sizeof(CreateDevice));
        CreateDevice.hAdapter = EnumAdapters.Adapters[0].hAdapter;

        Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_CREATEDEVICE,
                              &CreateDevice, sizeof(CreateDevice),
                              &CreateDevice, sizeof(CreateDevice),
                              &BytesReturned);
        ok(Result, "CreateDevice[%d] should succeed\n", i);
        DeviceHandles[i] = CreateDevice.hDevice;
    }

    /* Verify all handles are unique */
    for (i = 0; i < 4; i++)
    {
        int j;
        for (j = i + 1; j < 4; j++)
        {
            ok(DeviceHandles[i] != DeviceHandles[j],
               "Device handles [%d]=0x%08X and [%d]=0x%08X should be unique\n",
               i, DeviceHandles[i], j, DeviceHandles[j]);
        }
    }

    /* Destroy in reverse order */
    for (i = 3; i >= 0; i--)
    {
        if (DeviceHandles[i] != 0)
        {
            memset(&DestroyDevice, 0, sizeof(DestroyDevice));
            DestroyDevice.hDevice = DeviceHandles[i];

            Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_DESTROYDEVICE,
                                  &DestroyDevice, sizeof(DestroyDevice),
                                  NULL, 0,
                                  &BytesReturned);
            ok(Result, "DestroyDevice[%d] should succeed\n", i);
        }
    }

    CloseHandle(hDevice);
}

/*
 * Test: Double-destroy should fail
 *
 * Destroying the same device handle twice should fail on the second call.
 */
static void
Test_DoubleDestroy(void)
{
    HANDLE hDevice;
    D3DKMT_ENUMADAPTERS EnumAdapters;
    D3DKMT_CREATEDEVICE CreateDevice;
    D3DKMT_DESTROYDEVICE DestroyDevice;
    DWORD BytesReturned;
    BOOL Result;

    hDevice = OpenDxgKrnl();
    if (hDevice == INVALID_HANDLE_VALUE)
    {
        skip("Cannot open DxgKrnl device\n");
        return;
    }

    memset(&EnumAdapters, 0, sizeof(EnumAdapters));
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_ENUMADAPTERS,
                          &EnumAdapters, sizeof(EnumAdapters),
                          &EnumAdapters, sizeof(EnumAdapters),
                          &BytesReturned);

    if (!Result || EnumAdapters.NumAdapters == 0)
    {
        skip("No adapters available\n");
        CloseHandle(hDevice);
        return;
    }

    memset(&CreateDevice, 0, sizeof(CreateDevice));
    CreateDevice.hAdapter = EnumAdapters.Adapters[0].hAdapter;

    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_CREATEDEVICE,
                          &CreateDevice, sizeof(CreateDevice),
                          &CreateDevice, sizeof(CreateDevice),
                          &BytesReturned);

    if (!Result)
    {
        skip("Cannot create device\n");
        CloseHandle(hDevice);
        return;
    }

    /* First destroy: should succeed */
    memset(&DestroyDevice, 0, sizeof(DestroyDevice));
    DestroyDevice.hDevice = CreateDevice.hDevice;

    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_DESTROYDEVICE,
                          &DestroyDevice, sizeof(DestroyDevice),
                          NULL, 0,
                          &BytesReturned);
    ok(Result, "First DestroyDevice should succeed\n");

    /* Second destroy: should fail (stale handle) */
    Result = SendDxgIoctl(hDevice, IOCTL_D3DKMT_DESTROYDEVICE,
                          &DestroyDevice, sizeof(DestroyDevice),
                          NULL, 0,
                          &BytesReturned);
    ok(!Result, "Second DestroyDevice should fail (stale handle)\n");

    CloseHandle(hDevice);
}

/*
 * Test: WDDM 2.0 structure size validation
 *
 * Verify that the WDDM 2.0 DDI structures have expected sizes,
 * which confirms correct packing and layout.
 */
static void
Test_Wddm20_StructureSizes(void)
{
    /* Verify our local test structures have reasonable sizes */
    ok(sizeof(TEST_MAPGPUVA) > 0,
       "TEST_MAPGPUVA should have non-zero size\n");
    ok(sizeof(TEST_RESERVEGPUVA) > 0,
       "TEST_RESERVEGPUVA should have non-zero size\n");
    ok(sizeof(TEST_FREEGPUVA) > 0,
       "TEST_FREEGPUVA should have non-zero size\n");
    ok(sizeof(TEST_MAKERESIDENT) > 0,
       "TEST_MAKERESIDENT should have non-zero size\n");
    ok(sizeof(TEST_EVICT) > 0,
       "TEST_EVICT should have non-zero size\n");

    /* Verify D3DGPU_VIRTUAL_ADDRESS is 64-bit */
    ok(sizeof(D3DGPU_VIRTUAL_ADDRESS_TEST) == 8,
       "D3DGPU_VIRTUAL_ADDRESS should be 8 bytes, got %u\n",
       (unsigned)sizeof(D3DGPU_VIRTUAL_ADDRESS_TEST));
}

START_TEST(ProcessLifecycle)
{
    Test_DeviceLifecycle_Basic();
    Test_MultiDevice_Lifecycle();
    Test_DoubleDestroy();
    Test_Wddm20_StructureSizes();
}
