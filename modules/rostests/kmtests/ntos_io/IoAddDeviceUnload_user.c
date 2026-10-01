/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that a driver is not unloaded while its failing AddDevice still runs
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winuser.h>
#include <winreg.h>
#include <setupapi.h>
#include <newdev.h>

static const GUID TestClass = {0x4d36e97d, 0xe325, 0x11ce, {0xbf, 0xc1, 0x08, 0x00, 0x2b, 0xe1, 0x03, 0x18}};
static const WCHAR HardwareIds[] = L"ROOT\\KMTESTADDFAIL\0";
static const WCHAR ServiceKey[] = L"SYSTEM\\CurrentControlSet\\Services\\KmtestAddFail";

START_TEST(IoAddDeviceUnload)
{
    HDEVINFO Devices;
    SP_DEVINFO_DATA Device = { sizeof(Device) };
    WCHAR InfPath[MAX_PATH];
    PWSTR Filename;
    DWORD Length, Error, Value = MAXDWORD, Size = sizeof(Value), Type = 0;
    BOOL Success, Reboot = FALSE, Registered = FALSE;
    HKEY Key;

    Length = GetModuleFileNameW(NULL, InfPath, RTL_NUMBER_OF(InfPath));
    ok(Length != 0 && Length < RTL_NUMBER_OF(InfPath), "Cannot locate kmtest.exe: %lu\n", GetLastError());
    if (!Length || Length >= RTL_NUMBER_OF(InfPath))
        return;
    Filename = wcsrchr(InfPath, L'\\');
    if (!Filename || Filename - InfPath + RTL_NUMBER_OF(L"\\kmtest_addfail.inf") > RTL_NUMBER_OF(InfPath))
    {
        ok(FALSE, "INF path is too long\n");
        return;
    }
    wcscpy(Filename + 1, L"kmtest_addfail.inf");

    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_SET_VALUE, &Key) == ERROR_SUCCESS)
    {
        RegDeleteValueW(Key, L"UnloadDuringAddDevice");
        RegCloseKey(Key);
    }

    Devices = SetupDiCreateDeviceInfoList(&TestClass, NULL);
    ok(Devices != INVALID_HANDLE_VALUE, "SetupDiCreateDeviceInfoList failed: %lu\n", GetLastError());
    if (Devices == INVALID_HANDLE_VALUE)
        return;
    Success = SetupDiCreateDeviceInfoW(Devices, L"KmtestAddFail", &TestClass, L"Kmtest failing AddDevice fixture", NULL, DICD_GENERATE_ID, &Device);
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
    trace("Installing the failing fixture returned %d, error %lu\n", Success, GetLastError());

    Error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, ServiceKey, 0, KEY_QUERY_VALUE, &Key);
    ok_eq_ulong(Error, ERROR_SUCCESS);
    if (Error == ERROR_SUCCESS)
    {
        Error = RegQueryValueExW(Key, L"UnloadDuringAddDevice", NULL, &Type, (BYTE *)&Value, &Size);
        ok(Error == ERROR_SUCCESS, "AddDevice did not run: %lu\n", Error);
        if (Error == ERROR_SUCCESS)
        {
            ok_eq_ulong(Type, REG_DWORD);
            ok(Value == 0, "DriverUnload ran while AddDevice was still executing\n");
        }
        RegCloseKey(Key);
    }

Cleanup:
    if (Registered)
    {
        Success = SetupDiCallClassInstaller(DIF_REMOVE, Devices, &Device);
        ok(Success, "Removing the test devnode failed: %lu\n", GetLastError());
    }
    Success = SetupDiDestroyDeviceInfoList(Devices);
    ok(Success, "SetupDiDestroyDeviceInfoList failed: %lu\n", GetLastError());
}
