/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Touch registration flags, ownership and lifetime
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>

#include <wine/test.h>

#define CHECK(expression) ok((expression), "%s failed, error %lu\n", #expression, GetLastError())

static HWND window;
static BOOL (WINAPI *pRegisterTouchWindow)(HWND, ULONG);
static BOOL (WINAPI *pIsTouchWindow)(HWND, ULONG *);
static BOOL (WINAPI *pUnregisterTouchWindow)(HWND);
static DWORD WINAPI touch_other(void *parameter)
{
    ULONG f = 0xdeadbeef;
    SetLastError(0x1234);
    CHECK(!pRegisterTouchWindow(window, 3));
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    CHECK(pIsTouchWindow(window, &f));
    CHECK(f == 1);
    CHECK(!pUnregisterTouchWindow(window));
    CHECK(GetLastError() == ERROR_ACCESS_DENIED);
    return 0;
}
START_TEST(RegisterTouchWindow)
{
    HMODULE user32 = GetModuleHandleW(L"user32.dll");
    pRegisterTouchWindow = (void *)GetProcAddress(user32, "RegisterTouchWindow");
    pIsTouchWindow = (void *)GetProcAddress(user32, "IsTouchWindow");
    pUnregisterTouchWindow = (void *)GetProcAddress(user32, "UnregisterTouchWindow");
    CHECK(pRegisterTouchWindow != NULL);
    CHECK(pIsTouchWindow != NULL);
    CHECK(pUnregisterTouchWindow != NULL);
    if (!pRegisterTouchWindow || !pIsTouchWindow || !pUnregisterTouchWindow)
        return;
    window = CreateWindowExW(0, L"STATIC", L"Touch test", WS_OVERLAPPEDWINDOW, 0, 0, 100, 100, 0, 0, 0, 0);
    CHECK(window != 0);
    ULONG f = 0xdeadbeef;
    SetLastError(0x1234);
    CHECK(!pIsTouchWindow(window, &f));
    CHECK(f == 0xdeadbeef);
    CHECK(GetLastError() == 0x1234);
    for (ULONG flags = 0; flags < 4; flags++)
    {
        CHECK(pRegisterTouchWindow(window, flags));
        f = 99;
        CHECK(pIsTouchWindow(window, &f));
        CHECK(f == flags);
        CHECK(pIsTouchWindow(window, 0));
    }
    CHECK(!pRegisterTouchWindow(window, 4));
    CHECK(GetLastError() == ERROR_INVALID_FLAGS);
    CHECK(pIsTouchWindow(window, &f));
    CHECK(f == 3);
    CHECK(pRegisterTouchWindow(window, 1));
    HANDLE thread = CreateThread(0, 0, touch_other, 0, 0, 0);
    CHECK(thread != 0);
    CHECK(WaitForSingleObject(thread, 5000) == WAIT_OBJECT_0);
    CloseHandle(thread);
    CHECK(pUnregisterTouchWindow(window));
    CHECK(pUnregisterTouchWindow(window));
    f = 0xdeadbeef;
    CHECK(!pIsTouchWindow(window, &f));
    CHECK(f == 0xdeadbeef);
    CHECK(!pRegisterTouchWindow(0, 0));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
    CHECK(DestroyWindow(window));
    CHECK(!pIsTouchWindow(window, &f));
    CHECK(GetLastError() == ERROR_INVALID_WINDOW_HANDLE);
}
