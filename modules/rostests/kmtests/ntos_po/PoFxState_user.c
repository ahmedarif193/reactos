/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Create and remove the owned PoFx test device
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <setupapi.h>
#include <newdev.h>
#include <cfgmgr32.h>
#include <winioctl.h>
#include "PoFxState_pnp.h"

extern HANDLE KmtestHandle;

static const GUID TestClass = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR HardwareIds[] = L"ROOT\\KMTESTPOFX\0";

static VOID TestRemovalVeto(DEVINST Device)
{
    KMT_PNP_VETO_STATE Before = { 1 }, After = { 0 };
    WCHAR VetoName[MAX_PATH] = { 0 };
    WCHAR DeviceId[MAX_DEVICE_ID_LEN] = { 0 };
    PNP_VETO_TYPE VetoType = PNP_VetoTypeUnknown;
    ULONG Status = 0, Problem = 0;
    DWORD Returned;
    CONFIGRET Cr;
    BOOL Success;

    Success = DeviceIoControl(KmtestHandle, IOCTL_KMTEST_PNP_VETO, &Before, sizeof(Before), &Before, sizeof(Before), &Returned, NULL);
    ok(Success && Returned == sizeof(Before), "Setting removal veto failed: %lu\n", GetLastError());
    if (!Success)
        return;
    Cr = CM_Query_And_Remove_SubTreeW(Device, &VetoType, VetoName, RTL_NUMBER_OF(VetoName), CM_REMOVE_UI_NOT_OK);
    ok_eq_hex(Cr, CR_REMOVE_VETOED);
    trace("Removal veto: type %u, name %ls\n", VetoType, VetoName);
    ok_eq_uint(VetoType, PNP_VetoDevice);
    Cr = CM_Get_Device_IDW(Device, DeviceId, RTL_NUMBER_OF(DeviceId), 0);
    ok_eq_hex(Cr, CR_SUCCESS);
    ok(!_wcsicmp(VetoName, DeviceId), "Expected vetoing device %ls, got %ls\n", DeviceId, VetoName);
    Success = DeviceIoControl(KmtestHandle, IOCTL_KMTEST_PNP_VETO, &After, sizeof(After), &After, sizeof(After), &Returned, NULL);
    ok(Success && Returned == sizeof(After), "Reading removal state failed: %lu\n", GetLastError());
    if (!Success)
        return;
    ok_eq_ulong(After.Queries, Before.Queries + 1);
    ok_eq_ulong(After.Cancels, Before.Cancels + 1);
    ok_eq_ulong(After.Removes, Before.Removes);
    ok(After.Started, "Vetoed device is no longer started\n");
    Cr = CM_Get_DevNode_Status(&Status, &Problem, Device, 0);
    ok_eq_hex(Cr, CR_SUCCESS);
    ok(Status & DN_STARTED, "Vetoed devnode stopped: status %lx, problem %lu\n", Status, Problem);
}

static VOID TestWithPnpDevice(PCSTR TestName)
{
    HDEVINFO Devices = INVALID_HANDLE_VALUE;
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR InfPath[MAX_PATH];
    PWSTR Filename;
    DWORD Length, Error;
    BOOL Success, Reboot = FALSE, Registered = FALSE;

    Length = GetModuleFileNameW(NULL, InfPath, RTL_NUMBER_OF(InfPath));
    ok(Length != 0 && Length < RTL_NUMBER_OF(InfPath), "Cannot locate kmtest.exe: %lu\n", GetLastError());
    if (!Length || Length >= RTL_NUMBER_OF(InfPath))
        return;
    Filename = wcsrchr(InfPath, L'\\');
    if (!Filename || Filename - InfPath + RTL_NUMBER_OF(L"\\kmtest_pofx.inf") > RTL_NUMBER_OF(InfPath))
    {
        ok(FALSE, "PoFx INF path is too long\n");
        return;
    }
    wcscpy(Filename + 1, L"kmtest_pofx.inf");
    Error = GetFileAttributesW(InfPath);
    ok(Error != INVALID_FILE_ATTRIBUTES, "Missing %ls: %lu\n", InfPath, GetLastError());
    if (Error == INVALID_FILE_ATTRIBUTES)
        return;

    Devices = SetupDiCreateDeviceInfoList(&TestClass, NULL);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE)
        return;
    Success = SetupDiCreateDeviceInfoW(Devices, L"KmtestPoFx", &TestClass, L"Kmtest power framework fixture", NULL, DICD_GENERATE_ID, &Device);
    ok(Success, "SetupDiCreateDeviceInfo failed: %lu\n", GetLastError());
    if (!Success)
        goto Cleanup;
    Success = SetupDiSetDeviceRegistryPropertyW(Devices, &Device, SPDRP_HARDWAREID, (const BYTE *)HardwareIds, sizeof(HardwareIds));
    ok(Success, "Setting hardware ID failed: %lu\n", GetLastError());
    if (!Success)
        goto Cleanup;
    Success = SetupDiCallClassInstaller(DIF_REGISTERDEVICE, Devices, &Device);
    ok(Success, "Registering test devnode failed: %lu\n", GetLastError());
    if (!Success)
        goto Cleanup;
    Registered = TRUE;

    Success = UpdateDriverForPlugAndPlayDevicesW(NULL, HardwareIds, InfPath, INSTALLFLAG_FORCE | INSTALLFLAG_NONINTERACTIVE, &Reboot);
    ok(Success, "Installing PoFx fixture failed: %lu\n", GetLastError());
    if (!Success)
        goto Cleanup;
    trace("PoFx fixture installed, reboot requested: %u; kernel requires START_DEVICE\n", Reboot);
    Error = KmtRunKernelTest(TestName);
    ok_eq_ulong(Error, ERROR_SUCCESS);
    TestRemovalVeto(Device.DevInst);

Cleanup:
    if (Registered)
    {
        Success = SetupDiCallClassInstaller(DIF_REMOVE, Devices, &Device);
        ok(Success, "Removing owned PoFx devnode failed: %lu\n", GetLastError());
    }
    Success = SetupDiDestroyDeviceInfoList(Devices);
    ok(Success, "SetupDiDestroyDeviceInfoList failed: %lu\n", GetLastError());
}

START_TEST(PoFxState)
{
    TestWithPnpDevice("PoFxState");
}

START_TEST(ExWddmPoFx)
{
    TestWithPnpDevice("ExWddmPoFx");
}

START_TEST(IoDeviceNumaNode)
{
    TestWithPnpDevice("IoDeviceNumaNode");
}
