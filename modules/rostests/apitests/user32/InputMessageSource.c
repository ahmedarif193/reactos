/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture tests for GetCurrentInputMessageSource and EnableMouseInPointer
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef BOOL (WINAPI *FN_GetCurrentInputMessageSource)(INPUT_MESSAGE_SOURCE *);
typedef BOOL (WINAPI *FN_EnableMouseInPointer)(BOOL);
typedef BOOL (WINAPI *FN_IsMouseInPointerEnabled)(VOID);
typedef BOOL (WINAPI *FN_GetCIMSSM)(INPUT_MESSAGE_SOURCE *);

static FN_GetCurrentInputMessageSource pGetCurrentInputMessageSource;
static FN_EnableMouseInPointer pEnableMouseInPointer;
static FN_IsMouseInPointerEnabled pIsMouseInPointerEnabled;
static FN_GetCIMSSM pGetCIMSSM;

static BOOL
IsValidDeviceType(DWORD dt)
{
    switch (dt)
    {
        case IMDT_UNAVAILABLE:
        case IMDT_KEYBOARD:
        case IMDT_MOUSE:
        case IMDT_TOUCH:
        case IMDT_PEN:
        case IMDT_TOUCHPAD:
            return TRUE;
        default:
            return FALSE;
    }
}

static BOOL
IsValidOriginId(DWORD id)
{
    switch (id)
    {
        case IMO_UNAVAILABLE:
        case IMO_HARDWARE:
        case IMO_INJECTED:
        case IMO_SYSTEM:
            return TRUE;
        default:
            return FALSE;
    }
}

static void
CheckSource(_In_ const INPUT_MESSAGE_SOURCE *pSource, _In_ const char *pszWhere)
{
    ok(IsValidDeviceType(pSource->deviceType), "%s: bogus deviceType %u\n",
       pszWhere, pSource->deviceType);
    ok(IsValidOriginId(pSource->originId), "%s: bogus originId %u\n",
       pszWhere, pSource->originId);
}

static void
Test_BadPointers(void)
{
    BOOL ret;

    SetLastError(0xdeadbeef);
    ret = TRUE;
    _SEH2_TRY
    {
        ret = pGetCurrentInputMessageSource(NULL);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("NULL raised 0x%08lx\n", _SEH2_GetExceptionCode());
        ret = FALSE;
    }
    _SEH2_END;
    ok(!ret, "GetCurrentInputMessageSource(NULL) must not report success\n");

    ret = TRUE;
    _SEH2_TRY
    {
        ret = pGetCurrentInputMessageSource((INPUT_MESSAGE_SOURCE *)InvalidPointer);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("bogus pointer raised 0x%08lx\n", _SEH2_GetExceptionCode());
        ret = FALSE;
    }
    _SEH2_END;
    ok(!ret, "a bogus pointer must not report success\n");
}

static LRESULT CALLBACK
SourceWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_USER + 1 || msg == WM_MOUSEMOVE || msg == WM_KEYDOWN)
    {
        INPUT_MESSAGE_SOURCE source;

        memset(&source, 0xcc, sizeof(source));
        if (pGetCurrentInputMessageSource(&source))
            CheckSource(&source, "inside a window procedure");
        else
            ok(FALSE, "GetCurrentInputMessageSource failed inside a wndproc, error %lu\n",
               GetLastError());
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void
Test_MessageContexts(void)
{
    static const WCHAR szClass[] = L"InputMessageSourceTestClass";
    WNDCLASSEXW wc;
    INPUT_MESSAGE_SOURCE source;
    HWND hwnd;
    MSG msg;
    UINT i;
    LONG Bad = 0;

    memset(&source, 0xcc, sizeof(source));
    ok(pGetCurrentInputMessageSource(&source), "outside any message failed, error %lu\n",
       GetLastError());
    CheckSource(&source, "outside any message");

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = SourceWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = szClass;
    if (!RegisterClassExW(&wc))
    {
        skip("RegisterClassExW failed, error %lu\n", GetLastError());
        return;
    }

    hwnd = CreateWindowExW(0, szClass, L"src", WS_OVERLAPPEDWINDOW,
                           10, 10, 120, 120, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hwnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwnd)
    {
        UnregisterClassW(szClass, GetModuleHandleW(NULL));
        return;
    }

    SendMessageW(hwnd, WM_USER + 1, 0, 0);
    SendMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(5, 5));
    SendMessageW(hwnd, WM_KEYDOWN, VK_SPACE, 0);

    for (i = 0; i < 200; i++)
        PostMessageW(hwnd, WM_USER + 1, i, 0);

    while (PeekMessageW(&msg, hwnd, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);

        memset(&source, 0xcc, sizeof(source));
        if (!pGetCurrentInputMessageSource(&source))
            Bad++;
        else if (!IsValidDeviceType(source.deviceType) || !IsValidOriginId(source.originId))
            Bad++;
    }
    ok(Bad == 0, "%ld message-loop samples were invalid\n", Bad);

    DestroyWindow(hwnd);
    UnregisterClassW(szClass, GetModuleHandleW(NULL));
}

static void
Test_SendInputSource(void)
{
    INPUT_MESSAGE_SOURCE source;
    INPUT input;
    MSG msg;
    HWND hwnd;
    UINT i;

    hwnd = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"inj", WS_POPUP | WS_VISIBLE,
                           0, 0, 200, 200, NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (!hwnd)
    {
        skip("CreateWindowExW failed, error %lu\n", GetLastError());
        return;
    }
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    memset(&input, 0, sizeof(input));
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VK_SHIFT;
    SendInput(1, &input, sizeof(input));
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(input));

    for (i = 0; i < 50 && PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE); i++)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);

        if (msg.message == WM_KEYDOWN || msg.message == WM_KEYUP)
        {
            memset(&source, 0xcc, sizeof(source));
            ok(pGetCurrentInputMessageSource(&source),
               "after an injected key failed, error %lu\n", GetLastError());
            trace("injected key: deviceType %u originId %u\n",
                  source.deviceType, source.originId);
            CheckSource(&source, "injected keyboard");
            ok(source.deviceType == IMDT_KEYBOARD,
               "an injected key must report IMDT_KEYBOARD, got %u\n", source.deviceType);
            ok(source.originId == IMO_INJECTED,
               "an injected key must report IMO_INJECTED, got %u\n", source.originId);
        }
    }

    DestroyWindow(hwnd);
}

static void
Test_GetCIMSSM(void)
{
    INPUT_MESSAGE_SOURCE a, b;

    if (!pGetCIMSSM)
    {
        skip("GetCIMSSM is unavailable\n");
        return;
    }

    memset(&a, 0xcc, sizeof(a));
    memset(&b, 0xdd, sizeof(b));
    ok(pGetCurrentInputMessageSource(&a), "GetCurrentInputMessageSource failed\n");
    ok(pGetCIMSSM(&b), "GetCIMSSM failed, error %lu\n", GetLastError());
    CheckSource(&b, "GetCIMSSM");
    trace("GetCurrentInputMessageSource %u/%u vs GetCIMSSM %u/%u\n",
          a.deviceType, a.originId, b.deviceType, b.originId);
}

static void
Test_EnableMouseInPointer(void)
{
    BOOL enabled, ret;

    if (!pIsMouseInPointerEnabled)
    {
        skip("IsMouseInPointerEnabled is unavailable; the state cannot be observed\n");
        return;
    }

    enabled = pIsMouseInPointerEnabled();
    trace("IsMouseInPointerEnabled = %d\n", enabled);

    SetLastError(0xdeadbeef);
    ret = pEnableMouseInPointer(enabled);
    ok(ret, "re-applying the current setting must succeed, error %lu\n", GetLastError());
    ok(pIsMouseInPointerEnabled() == enabled, "the setting must not change\n");

    SetLastError(0xdeadbeef);
    ret = pEnableMouseInPointer(!enabled);
    ok(!ret, "flipping a process-lifetime setting must fail\n");
    ok(GetLastError() == ERROR_ACCESS_DENIED,
       "expected ERROR_ACCESS_DENIED, got %lu\n", GetLastError());
    ok(pIsMouseInPointerEnabled() == enabled, "a failed flip must not change the setting\n");

    SetLastError(0xdeadbeef);
    ok(pEnableMouseInPointer(enabled ? 0x1234 : 0),
       "any non-zero value must be treated as TRUE\n");
    ok(pIsMouseInPointerEnabled() == enabled, "the setting must still be unchanged\n");
}

static DWORD WINAPI
HammerThread(LPVOID pv)
{
    LONG *pFailures = (LONG *)pv;
    INPUT_MESSAGE_SOURCE source;
    UINT i;

    for (i = 0; i < 10000; i++)
    {
        memset(&source, 0xcc, sizeof(source));
        if (!pGetCurrentInputMessageSource(&source))
        {
            InterlockedIncrement(pFailures);
            break;
        }
        if (!IsValidDeviceType(source.deviceType) || !IsValidOriginId(source.originId))
        {
            InterlockedIncrement(pFailures);
            break;
        }
    }
    return 0;
}

static void
Test_Threaded(void)
{
    HANDLE hThreads[4];
    LONG Failures = 0;
    UINT i;

    for (i = 0; i < ARRAYSIZE(hThreads); i++)
        hThreads[i] = CreateThread(NULL, 0, HammerThread, &Failures, 0, NULL);

    WaitForMultipleObjects(ARRAYSIZE(hThreads), hThreads, TRUE, 60000);
    for (i = 0; i < ARRAYSIZE(hThreads); i++)
    {
        if (hThreads[i])
            CloseHandle(hThreads[i]);
    }
    ok(Failures == 0, "%ld threaded iterations misbehaved\n", Failures);
}

START_TEST(InputMessageSource)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");

    pGetCurrentInputMessageSource =
        (FN_GetCurrentInputMessageSource)GetProcAddress(hUser32, "GetCurrentInputMessageSource");
    pEnableMouseInPointer = (FN_EnableMouseInPointer)GetProcAddress(hUser32, "EnableMouseInPointer");
    pIsMouseInPointerEnabled =
        (FN_IsMouseInPointerEnabled)GetProcAddress(hUser32, "IsMouseInPointerEnabled");
    pGetCIMSSM = (FN_GetCIMSSM)GetProcAddress(hUser32, "GetCIMSSM");

    ok(pGetCurrentInputMessageSource != NULL, "user32!GetCurrentInputMessageSource is missing\n");
    ok(pEnableMouseInPointer != NULL, "user32!EnableMouseInPointer is missing\n");

    if (pGetCurrentInputMessageSource)
    {
        Test_BadPointers();
        Test_MessageContexts();
        Test_SendInputSource();
        Test_GetCIMSSM();
        Test_Threaded();
    }
    if (pEnableMouseInPointer)
        Test_EnableMouseInPointer();
}
