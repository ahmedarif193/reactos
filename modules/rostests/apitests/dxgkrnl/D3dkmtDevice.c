/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     D3DKMT device and context lifecycle tests
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

static void Test_CreateDestroyDevice(void)
{
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_DESTROYDEVICE dd;
    DWORD br;
    BOOL r;
    D3DKMT_HANDLE hAdapter;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    if (!r) { skip("CreateDevice failed (%lu)\n", GetLastError()); CloseHandle(hDxgKrnl); return; }

    ok(cd.hDevice != 0, "CreateDevice returned zero handle\n");

    /* Device handle must differ from adapter handle */
    ok(cd.hDevice != hAdapter,
       "Device handle should differ from adapter handle\n");

    /* Destroy the device */
    memset(&dd, 0, sizeof(dd));
    dd.hDevice = cd.hDevice;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(r, "DestroyDevice failed (%lu)\n", GetLastError());

    CloseHandle(hDxgKrnl);
}

static void Test_CreateDeviceInvalidAdapter(void)
{
    D3DKMT_CREATEDEVICE cd;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = 0xDEADBEEF;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    ok(!r, "CreateDevice should fail with invalid adapter\n");

    CloseHandle(hDxgKrnl);
}

static void Test_CreateDeviceZeroAdapter(void)
{
    D3DKMT_CREATEDEVICE cd;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = 0;

    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    ok(!r, "CreateDevice should fail with zero adapter\n");

    CloseHandle(hDxgKrnl);
}

static void Test_DestroyDeviceInvalidHandle(void)
{
    D3DKMT_DESTROYDEVICE dd;
    DWORD br;
    BOOL r;

    if (!OpenDxg()) return;

    memset(&dd, 0, sizeof(dd));
    dd.hDevice = 0xBAD0CAFE;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(!r, "DestroyDevice should fail with invalid handle\n");

    dd.hDevice = 0;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(!r, "DestroyDevice should fail with zero handle\n");

    CloseHandle(hDxgKrnl);
}

static void Test_DestroyDeviceDouble(void)
{
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_DESTROYDEVICE dd;
    DWORD br;
    BOOL r;
    D3DKMT_HANDLE hAdapter;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    if (!r) { skip("CreateDevice failed\n"); CloseHandle(hDxgKrnl); return; }

    /* First destroy should succeed */
    memset(&dd, 0, sizeof(dd));
    dd.hDevice = cd.hDevice;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(r, "First DestroyDevice failed\n");

    /* Second destroy of same handle should fail */
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    ok(!r, "Double-destroy should fail\n");

    CloseHandle(hDxgKrnl);
}

static void Test_MultipleDevices(void)
{
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_DESTROYDEVICE dd;
    DWORD br;
    BOOL r;
    D3DKMT_HANDLE hAdapter;
    D3DKMT_HANDLE Devices[8];
    ULONG i, j;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    /* Create 8 devices on the same adapter */
    for (i = 0; i < 8; ++i)
    {
        memset(&cd, 0, sizeof(cd));
        cd.hAdapter = hAdapter;
        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                         &cd, sizeof(cd), &cd, sizeof(cd), &br);
        if (!r)
        {
            skip("CreateDevice #%lu failed\n", i);
            /* Cleanup already created */
            while (i-- > 0)
            {
                dd.hDevice = Devices[i];
                SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                             &dd, sizeof(dd), NULL, 0, &br);
            }
            CloseHandle(hDxgKrnl);
            return;
        }
        Devices[i] = cd.hDevice;
    }

    /* All handles must be unique and non-zero */
    for (i = 0; i < 8; ++i)
    {
        ok(Devices[i] != 0, "Device[%lu] handle is 0\n", i);
        for (j = i + 1; j < 8; ++j)
        {
            ok(Devices[i] != Devices[j],
               "Device[%lu] and Device[%lu] have same handle 0x%08X\n",
               i, j, Devices[i]);
        }
    }

    /* Destroy all */
    for (i = 0; i < 8; ++i)
    {
        dd.hDevice = Devices[i];
        r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                         &dd, sizeof(dd), NULL, 0, &br);
        ok(r, "DestroyDevice[%lu] failed\n", i);
    }

    CloseHandle(hDxgKrnl);
}

static void Test_CreateDeviceFlags(void)
{
    D3DKMT_CREATEDEVICE cd;
    D3DKMT_DESTROYDEVICE dd;
    DWORD br;
    BOOL r;
    D3DKMT_HANDLE hAdapter;

    if (!OpenDxg()) return;
    hAdapter = EnumFirstAdapter();
    if (!hAdapter) { skip("No adapter\n"); CloseHandle(hDxgKrnl); return; }

    /* Create with LegacyMode = 1 */
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    cd.Flags.LegacyMode = 1;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    if (r)
    {
        ok(cd.hDevice != 0, "CreateDevice with LegacyMode returned zero handle\n");
        dd.hDevice = cd.hDevice;
        SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    }

    /* Create with RequestVSync = 1 */
    memset(&cd, 0, sizeof(cd));
    cd.hAdapter = hAdapter;
    cd.Flags.RequestVSync = 1;
    r = SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_CREATEDEVICE,
                     &cd, sizeof(cd), &cd, sizeof(cd), &br);
    if (r)
    {
        ok(cd.hDevice != 0, "CreateDevice with VSync returned zero handle\n");
        dd.hDevice = cd.hDevice;
        SendDxgIoctl(hDxgKrnl, IOCTL_D3DKMT_DESTROYDEVICE,
                     &dd, sizeof(dd), NULL, 0, &br);
    }

    CloseHandle(hDxgKrnl);
}

START_TEST(D3dkmtDevice)
{
    Test_CreateDestroyDevice();
    Test_CreateDeviceInvalidAdapter();
    Test_CreateDeviceZeroAdapter();
    Test_DestroyDeviceInvalidHandle();
    Test_DestroyDeviceDouble();
    Test_MultipleDevices();
    Test_CreateDeviceFlags();
}
