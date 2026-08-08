/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Tests modern user32 query API fallback semantics
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"

typedef BOOL (WINAPI *PGETAUTOROTATIONSTATE)(PAR_STATE);
typedef BOOL (WINAPI *PGETPOINTERDEVICE)(HANDLE, POINTER_DEVICE_INFO *);
typedef BOOL (WINAPI *PGETPOINTERPENINFO)(UINT32, POINTER_PEN_INFO *);
typedef BOOL (WINAPI *PISWINDOWARRANGED)(HWND);

static void
TestAutoRotationState(
    _In_ HMODULE User32)
{
    PGETAUTOROTATIONSTATE GetAutoRotationStateFn;
    AR_STATE State;
    DWORD Error;
    BOOL Result;

    GetAutoRotationStateFn = (PGETAUTOROTATIONSTATE)GetProcAddress(User32, "GetAutoRotationState");
    if (GetAutoRotationStateFn == NULL)
    {
        skip("GetAutoRotationState is not exported\n");
        return;
    }

    SetLastError(0xdeadbeef);
    Result = GetAutoRotationStateFn(NULL);
    Error = GetLastError();
    ok(!Result, "GetAutoRotationState(NULL) unexpectedly succeeded\n");
    ok(Error == ERROR_INVALID_PARAMETER, "GetAutoRotationState(NULL) returned error %lu\n", Error);

    State = (AR_STATE)0xdeadbeef;
    Result = GetAutoRotationStateFn(&State);
    ok(Result, "GetAutoRotationState failed, error %lu\n", GetLastError());
    ok(State != (AR_STATE)0xdeadbeef, "GetAutoRotationState did not initialize the state\n");
}

static void
TestPointerQueries(
    _In_ HMODULE User32)
{
    PGETPOINTERDEVICE GetPointerDeviceFn;
    PGETPOINTERPENINFO GetPointerPenInfoFn;
    POINTER_DEVICE_INFO DeviceInfo;
    POINTER_DEVICE_INFO ExpectedDeviceInfo;
    POINTER_PEN_INFO PenInfo;
    POINTER_PEN_INFO ExpectedPenInfo;
    DWORD Error;
    BOOL Result;

    GetPointerDeviceFn = (PGETPOINTERDEVICE)GetProcAddress(User32, "GetPointerDevice");
    if (GetPointerDeviceFn != NULL)
    {
        memset(&ExpectedDeviceInfo, 0xcc, sizeof(ExpectedDeviceInfo));
        DeviceInfo = ExpectedDeviceInfo;
        SetLastError(0xdeadbeef);
        Result = GetPointerDeviceFn((HANDLE)(ULONG_PTR)0xdeadbeef, &DeviceInfo);
        Error = GetLastError();
        ok(!Result, "GetPointerDevice accepted an invalid device handle\n");
        ok(Error == ERROR_INVALID_HANDLE, "GetPointerDevice returned error %lu\n", Error);
        ok(!memcmp(&DeviceInfo, &ExpectedDeviceInfo, sizeof(DeviceInfo)), "GetPointerDevice changed the output buffer\n");
    }
    else
    {
        skip("GetPointerDevice is not exported\n");
    }

    GetPointerPenInfoFn = (PGETPOINTERPENINFO)GetProcAddress(User32, "GetPointerPenInfo");
    if (GetPointerPenInfoFn != NULL)
    {
        memset(&ExpectedPenInfo, 0xcc, sizeof(ExpectedPenInfo));
        PenInfo = ExpectedPenInfo;
        SetLastError(0xdeadbeef);
        Result = GetPointerPenInfoFn(0xdeadbeef, &PenInfo);
        Error = GetLastError();
        ok(!Result, "GetPointerPenInfo accepted an invalid pointer ID\n");
        ok(Error == ERROR_INVALID_PARAMETER, "GetPointerPenInfo returned error %lu\n", Error);
        ok(!memcmp(&PenInfo, &ExpectedPenInfo, sizeof(PenInfo)), "GetPointerPenInfo changed the output buffer\n");
    }
    else
    {
        skip("GetPointerPenInfo is not exported\n");
    }
}

static void
TestWindowArrangement(
    _In_ HMODULE User32)
{
    PISWINDOWARRANGED IsWindowArrangedFn;
    WNDCLASSA WindowClass;
    DWORD Error;
    BOOL Result;
    HWND hwnd;

    IsWindowArrangedFn = (PISWINDOWARRANGED)GetProcAddress(User32, "IsWindowArranged");
    if (IsWindowArrangedFn == NULL)
    {
        skip("IsWindowArranged is not exported\n");
        return;
    }

    SetLastError(0xdeadbeef);
    Result = IsWindowArrangedFn(NULL);
    Error = GetLastError();
    ok(!Result, "IsWindowArranged(NULL) unexpectedly succeeded\n");
    ok(Error == ERROR_INVALID_PARAMETER, "IsWindowArranged(NULL) returned error %lu\n", Error);

    SetLastError(0xdeadbeef);
    Result = IsWindowArrangedFn((HWND)(ULONG_PTR)0xdeadbeef);
    Error = GetLastError();
    ok(!Result, "IsWindowArranged accepted an invalid window handle\n");
    ok(Error == ERROR_INVALID_PARAMETER, "IsWindowArranged(invalid) returned error %lu\n", Error);

    ZeroMemory(&WindowClass, sizeof(WindowClass));
    WindowClass.lpfnWndProc = DefWindowProcA;
    WindowClass.hInstance = GetModuleHandleA(NULL);
    WindowClass.lpszClassName = "ModernUser32TestClass";
    if (!RegisterClassA(&WindowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        skip("RegisterClassA failed, error %lu\n", GetLastError());
        return;
    }

    hwnd = CreateWindowExA(0, WindowClass.lpszClassName, "Modern user32 test", WS_OVERLAPPED, 0, 0, 100, 100, NULL, NULL, WindowClass.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindowExA failed, error %lu\n", GetLastError());
    if (hwnd != NULL)
    {
        Result = IsWindowArrangedFn(hwnd);
        ok(!Result, "A normal window was reported as arranged\n");
        DestroyWindow(hwnd);
    }
}

START_TEST(ModernUser32)
{
    HMODULE User32;

    User32 = GetModuleHandleA("user32.dll");
    ok(User32 != NULL, "user32.dll is not loaded\n");
    if (User32 == NULL) return;

    TestAutoRotationState(User32);
    TestPointerQueries(User32);
    TestWindowArrangement(User32);
}
