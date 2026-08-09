/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Battery telemetry contract and conversion tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>

#define WIN32_NO_STATUS
#include <windows.h>
#include <initguid.h>
#include <batclass.h>
#include <setupapi.h>

#include "battery_telemetry.h"

static BOOL close_double(double first, double second, double tolerance)
{
    double difference = first - second;
    if (difference < 0.0) difference = -difference;
    return difference <= tolerance;
}

static void test_conversion_contract(void)
{
    BATTERY_INFORMATION information;
    BATTERY_STATUS status;
    double value;

    ZeroMemory(&information, sizeof(information));
    ZeroMemory(&status, sizeof(status));
    information.DesignedCapacity = 60000;
    information.FullChargedCapacity = 50000;
    status.Capacity = 25000;
    status.Voltage = 12000;
    status.Rate = -24000;

    ok(TmBatteryChargePercent(status.Capacity, information.FullChargedCapacity, &value), "expected a charge percentage\n");
    ok(close_double(value, 50.0, 0.0001), "charge percentage is %.6f, expected 50\n", value);
    ok(TmBatteryHealthPercent(&information, &value), "expected a health percentage\n");
    ok(close_double(value, 83.333333, 0.0001), "health percentage is %.6f, expected 83.333333\n", value);
    ok(TmBatteryPowerWatts(&information, &status, &value), "expected absolute battery power\n");
    ok(close_double(value, -24.0, 0.0001), "power is %.6f W, expected -24 W\n", value);
    ok(TmBatteryCurrentAmps(&information, &status, &value), "expected derived battery current\n");
    ok(close_double(value, -2.0, 0.0001), "current is %.6f A, expected -2 A\n", value);

    status.Rate = 18000;
    ok(TmBatteryPowerWatts(&information, &status, &value), "expected positive charging power\n");
    ok(close_double(value, 18.0, 0.0001), "charging power is %.6f W, expected 18 W\n", value);
    ok(TmBatteryCurrentAmps(&information, &status, &value), "expected positive charging current\n");
    ok(close_double(value, 1.5, 0.0001), "charging current is %.6f A, expected 1.5 A\n", value);
    status.Rate = -24000;

    status.Capacity = 55000;
    ok(TmBatteryChargePercent(status.Capacity, information.FullChargedCapacity, &value), "expected a clamped charge percentage\n");
    ok(close_double(value, 100.0, 0.0001), "clamped charge percentage is %.6f, expected 100\n", value);

    information.Capabilities = BATTERY_CAPACITY_RELATIVE;
    ok(TmBatteryChargePercent(status.Capacity, information.FullChargedCapacity, &value), "relative capacities should still produce a percentage\n");
    ok(!TmBatteryPowerWatts(&information, &status, &value), "relative rate must not be labelled as watts\n");
    ok(!TmBatteryCurrentAmps(&information, &status, &value), "relative rate must not produce amperes\n");

    information.Capabilities = 0;
    status.Rate = (LONG)BATTERY_UNKNOWN_RATE;
    ok(!TmBatteryPowerWatts(&information, &status, &value), "unknown rate must not produce watts\n");
    ok(!TmBatteryCurrentAmps(&information, &status, &value), "unknown rate must not produce amperes\n");
    status.Rate = -24000;
    status.Voltage = BATTERY_UNKNOWN_VOLTAGE;
    ok(!TmBatteryCurrentAmps(&information, &status, &value), "unknown voltage must not produce amperes\n");
    status.Voltage = 0;
    ok(!TmBatteryCurrentAmps(&information, &status, &value), "zero voltage must not produce amperes\n");
    ok(!TmBatteryChargePercent(BATTERY_UNKNOWN_CAPACITY, information.FullChargedCapacity, &value), "unknown remaining capacity must not produce a percentage\n");
    ok(!TmBatteryChargePercent(100, 0, &value), "zero full capacity must not produce a percentage\n");
    ok(!TmBatteryChargePercent(100, 200, NULL), "a null percentage output must be rejected\n");
    ok(!TmBatteryHealthPercent(NULL, &value), "a null information record must be rejected\n");
    ok(!TmBatteryPowerWatts(&information, NULL, &value), "a null status record must be rejected\n");
    ok(!TmBatteryCurrentAmps(&information, &status, NULL), "a null current output must be rejected\n");
}

static void test_system_power_status(void)
{
    SYSTEM_POWER_STATUS status;
    BOOL result;

    SetLastError(0xdeadbeef);
    result = GetSystemPowerStatus(&status);
    ok(result, "GetSystemPowerStatus failed with %lu\n", GetLastError());
    if (!result) return;
    ok(status.ACLineStatus == 0 || status.ACLineStatus == 1 || status.ACLineStatus == 255, "unexpected AC line status %u\n", status.ACLineStatus);
    ok(status.BatteryLifePercent <= 100 || status.BatteryLifePercent == 255, "unexpected battery percentage %u\n", status.BatteryLifePercent);
    trace("system ac=%u flags=%#x percent=%u life=%lu full_life=%lu\n", status.ACLineStatus, status.BatteryFlag, status.BatteryLifePercent, status.BatteryLifeTime, status.BatteryFullLifeTime);
}

static void trace_battery(HANDLE handle, ULONG tag, DWORD index)
{
    BATTERY_QUERY_INFORMATION query;
    BATTERY_INFORMATION information;
    BATTERY_WAIT_STATUS waitStatus;
    BATTERY_STATUS status;
    ULONG estimatedTime = BATTERY_UNKNOWN_TIME;
    DWORD returned = 0;
    BOOL infoResult;
    BOOL statusResult;
    double value;

    ZeroMemory(&query, sizeof(query));
    ZeroMemory(&information, sizeof(information));
    query.BatteryTag = tag;
    query.InformationLevel = BatteryInformation;
    infoResult = DeviceIoControl(handle, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), &information, sizeof(information), &returned, NULL);
    ok(infoResult, "battery %lu information query failed with %lu\n", index, GetLastError());
    if (infoResult) ok(returned >= sizeof(information), "battery %lu information returned %lu bytes\n", index, returned);

    ZeroMemory(&waitStatus, sizeof(waitStatus));
    ZeroMemory(&status, sizeof(status));
    waitStatus.BatteryTag = tag;
    statusResult = DeviceIoControl(handle, IOCTL_BATTERY_QUERY_STATUS, &waitStatus, sizeof(waitStatus), &status, sizeof(status), &returned, NULL);
    ok(statusResult, "battery %lu status query failed with %lu\n", index, GetLastError());
    if (statusResult)
    {
        ok(returned >= sizeof(status), "battery %lu status returned %lu bytes\n", index, returned);
        ok(!(status.PowerState & ~(BATTERY_POWER_ON_LINE | BATTERY_DISCHARGING | BATTERY_CHARGING | BATTERY_CRITICAL)), "battery %lu returned unknown power-state bits %#lx\n", index, status.PowerState);
    }

    if (infoResult && statusResult)
    {
        trace("battery %lu caps=%#lx design=%lu full=%lu cycles=%lu state=%#lx remaining=%lu voltage_mv=%lu rate=%ld\n", index, information.Capabilities, information.DesignedCapacity, information.FullChargedCapacity, information.CycleCount, status.PowerState, status.Capacity, status.Voltage, status.Rate);
        if (TmBatteryChargePercent(status.Capacity, information.FullChargedCapacity, &value)) trace("battery %lu charge_pct=%.3f\n", index, value);
        if (TmBatteryHealthPercent(&information, &value)) trace("battery %lu health_pct=%.3f\n", index, value);
        if (TmBatteryPowerWatts(&information, &status, &value)) trace("battery %lu power_w=%.3f\n", index, value);
        if (TmBatteryCurrentAmps(&information, &status, &value)) trace("battery %lu current_a=%.3f\n", index, value);
    }

    query.InformationLevel = BatteryEstimatedTime;
    query.AtRate = 0;
    if (DeviceIoControl(handle, IOCTL_BATTERY_QUERY_INFORMATION, &query, sizeof(query), &estimatedTime, sizeof(estimatedTime), &returned, NULL) && returned >= sizeof(estimatedTime) && estimatedTime != BATTERY_UNKNOWN_TIME) trace("battery %lu estimated_seconds=%lu\n", index, estimatedTime);
}

static void test_battery_interfaces(void)
{
    HDEVINFO devices;
    DWORD batteries = 0;

    devices = SetupDiGetClassDevsW(&GUID_DEVICE_BATTERY, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    ok(devices != INVALID_HANDLE_VALUE, "SetupDiGetClassDevsW failed with %lu\n", GetLastError());
    if (devices == INVALID_HANDLE_VALUE) return;

    for (DWORD index = 0; ; index++)
    {
        PSP_DEVICE_INTERFACE_DETAIL_DATA_W detail;
        SP_DEVICE_INTERFACE_DATA interfaceData;
        HANDLE handle;
        ULONG tag = BATTERY_TAG_INVALID;
        ULONG wait = 0;
        DWORD required = 0;
        DWORD returned = 0;
        DWORD error;

        ZeroMemory(&interfaceData, sizeof(interfaceData));
        interfaceData.cbSize = sizeof(interfaceData);
        if (!SetupDiEnumDeviceInterfaces(devices, NULL, &GUID_DEVICE_BATTERY, index, &interfaceData))
        {
            ok(GetLastError() == ERROR_NO_MORE_ITEMS, "battery enumeration stopped with %lu\n", GetLastError());
            break;
        }
        SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, NULL, 0, &required, NULL);
        error = GetLastError();
        ok(error == ERROR_INSUFFICIENT_BUFFER, "battery %lu detail sizing returned %lu\n", index, error);
        if (error != ERROR_INSUFFICIENT_BUFFER) continue;
        detail = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, required);
        ok(detail != NULL, "battery %lu detail allocation failed\n", index);
        if (!detail) break;
        detail->cbSize = sizeof(*detail);
        if (!SetupDiGetDeviceInterfaceDetailW(devices, &interfaceData, detail, required, NULL, NULL))
        {
            ok(FALSE, "battery %lu detail query failed with %lu\n", index, GetLastError());
            HeapFree(GetProcessHeap(), 0, detail);
            continue;
        }
        handle = CreateFileW(detail->DevicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        HeapFree(GetProcessHeap(), 0, detail);
        ok(handle != INVALID_HANDLE_VALUE, "battery %lu open failed with %lu\n", index, GetLastError());
        if (handle == INVALID_HANDLE_VALUE) continue;
        if (!DeviceIoControl(handle, IOCTL_BATTERY_QUERY_TAG, &wait, sizeof(wait), &tag, sizeof(tag), &returned, NULL) || returned < sizeof(tag) || tag == BATTERY_TAG_INVALID)
        {
            win_skip("battery %lu is present but has no active battery tag\n", index);
            CloseHandle(handle);
            continue;
        }
        batteries++;
        trace_battery(handle, tag, index);
        CloseHandle(handle);
    }
    SetupDiDestroyDeviceInfoList(devices);
    if (!batteries) win_skip("no active battery-class device is installed\n");
}

START_TEST(battery)
{
    test_conversion_contract();
    test_system_power_status();
    test_battery_interfaces();
}
