/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture tests for GetWindowBand and CreateWindowInBand
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef BOOL (WINAPI *FN_GetWindowBand)(HWND, PDWORD);
typedef HWND (WINAPI *FN_CreateWindowInBand)(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int,
                                             HWND, HMENU, HINSTANCE, LPVOID, DWORD);

static FN_GetWindowBand pGetWindowBand;
static FN_CreateWindowInBand pCreateWindowInBand;

static HWND
MakeWindow(_In_ DWORD dwExStyle, _In_ DWORD dwStyle, _In_opt_ HWND hwndParent)
{
    return CreateWindowExW(dwExStyle, L"STATIC", L"band", dwStyle,
                           10, 10, 80, 80, hwndParent, NULL, GetModuleHandleW(NULL), NULL);
}

static void
Test_BadArguments(void)
{
    static const ULONG_PTR Handles[] = { 0, 1, 0xffff, 0xdeadbeef, (ULONG_PTR)-1 };
    HWND hwnd;
    DWORD dwBand;
    BOOL ret;
    UINT i;

    for (i = 0; i < ARRAYSIZE(Handles); i++)
    {
        dwBand = 0xdeadbeef;
        SetLastError(0xdeadbeef);
        ret = TRUE;
        _SEH2_TRY
        {
            ret = pGetWindowBand((HWND)Handles[i], &dwBand);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ok(FALSE, "handle %p raised 0x%08lx\n",
               (void *)Handles[i], _SEH2_GetExceptionCode());
            ret = FALSE;
        }
        _SEH2_END;
        ok(!ret, "GetWindowBand(%p) must fail\n", (void *)Handles[i]);
        ok(dwBand == 0xdeadbeef,
           "GetWindowBand(%p) must not write the output, got %lu\n",
           (void *)Handles[i], dwBand);
    }

    hwnd = MakeWindow(0, WS_OVERLAPPEDWINDOW, NULL);
    ok(hwnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwnd)
        return;

    SetLastError(0xdeadbeef);
    ret = pGetWindowBand(hwnd, NULL);
    ok(!ret, "GetWindowBand(hwnd, NULL) must fail\n");

    ret = TRUE;
    _SEH2_TRY
    {
        ret = pGetWindowBand(hwnd, (PDWORD)InvalidPointer);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("bogus output pointer raised 0x%08lx\n", _SEH2_GetExceptionCode());
        ret = FALSE;
    }
    _SEH2_END;
    ok(!ret, "a bogus output pointer must not report success\n");

    DestroyWindow(hwnd);
}

static void
Test_BandOfEveryStyle(void)
{
    static const struct
    {
        DWORD dwExStyle;
        DWORD dwStyle;
        const char *pszName;
    } Cases[] =
    {
        { 0,                 WS_OVERLAPPEDWINDOW, "overlapped" },
        { WS_EX_TOPMOST,     WS_POPUP,            "topmost popup" },
        { WS_EX_TOOLWINDOW,  WS_POPUP,            "tool window" },
        { WS_EX_NOACTIVATE,  WS_POPUP,            "noactivate" },
        { WS_EX_LAYERED,     WS_POPUP,            "layered" },
        { WS_EX_TRANSPARENT, WS_POPUP,            "transparent" },
    };
    UINT i;

    for (i = 0; i < ARRAYSIZE(Cases); i++)
    {
        HWND hwnd = MakeWindow(Cases[i].dwExStyle, Cases[i].dwStyle, NULL);
        DWORD dwBand = 0xdeadbeef;

        ok(hwnd != NULL, "%s: CreateWindowExW failed, error %lu\n",
           Cases[i].pszName, GetLastError());
        if (!hwnd)
            continue;

        ok(pGetWindowBand(hwnd, &dwBand), "%s: GetWindowBand failed, error %lu\n",
           Cases[i].pszName, GetLastError());
        ok(dwBand == ZBID_DESKTOP, "%s: expected band %u, got %lu\n",
           Cases[i].pszName, ZBID_DESKTOP, dwBand);

        DestroyWindow(hwnd);
    }
}

static void
Test_ChildAndDesktopBands(void)
{
    HWND hwndTop = MakeWindow(0, WS_OVERLAPPEDWINDOW, NULL);
    HWND hwndChild;
    DWORD dwBand;

    ok(hwndTop != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwndTop)
        return;

    hwndChild = MakeWindow(0, WS_CHILD, hwndTop);
    ok(hwndChild != NULL, "child failed, error %lu\n", GetLastError());
    if (hwndChild)
    {
        dwBand = 0xdeadbeef;
        ok(pGetWindowBand(hwndChild, &dwBand), "child GetWindowBand failed, error %lu\n",
           GetLastError());
        ok(dwBand == ZBID_DESKTOP, "child band expected %u, got %lu\n", ZBID_DESKTOP, dwBand);
        DestroyWindow(hwndChild);
    }

    dwBand = 0xdeadbeef;
    ok(pGetWindowBand(GetDesktopWindow(), &dwBand),
       "GetWindowBand(desktop) failed, error %lu\n", GetLastError());
    trace("desktop window band = %lu\n", dwBand);

    DestroyWindow(hwndTop);
}

static void
Test_CreateWindowInBand(void)
{
    static const DWORD Bands[] =
    {
        ZBID_DEFAULT, ZBID_DESKTOP, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15,
        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 100, 0xFFFF, 0xdeadbeef
    };
    UINT i;
    HWND hwnd;
    DWORD dwBand;
    RECT rc;

    hwnd = pCreateWindowInBand(0, L"STATIC", L"inband", WS_POPUP,
                               20, 25, 130, 140, NULL, NULL, GetModuleHandleW(NULL), NULL,
                               ZBID_DEFAULT);
    ok(hwnd != NULL, "CreateWindowInBand(ZBID_DEFAULT) failed, error %lu\n", GetLastError());
    if (hwnd)
    {
        ok(IsWindow(hwnd), "CreateWindowInBand must return a real window\n");
        ok(GetWindowRect(hwnd, &rc), "GetWindowRect failed, error %lu\n", GetLastError());
        ok(rc.right - rc.left == 130 && rc.bottom - rc.top == 140,
           "geometry lost: %ldx%ld\n", rc.right - rc.left, rc.bottom - rc.top);
        ok((GetWindowLongPtrW(hwnd, GWL_STYLE) & WS_POPUP) == WS_POPUP, "style lost\n");

        dwBand = 0xdeadbeef;
        ok(pGetWindowBand(hwnd, &dwBand), "GetWindowBand failed, error %lu\n", GetLastError());
        trace("CreateWindowInBand(ZBID_DEFAULT) -> band %lu\n", dwBand);

        DestroyWindow(hwnd);
    }

    for (i = 0; i < ARRAYSIZE(Bands); i++)
    {
        SetLastError(0xdeadbeef);
        hwnd = pCreateWindowInBand(0, L"STATIC", L"b", WS_POPUP,
                                   0, 0, 20, 20, NULL, NULL, GetModuleHandleW(NULL), NULL,
                                   Bands[i]);
        if (hwnd)
        {
            dwBand = 0xdeadbeef;
            pGetWindowBand(hwnd, &dwBand);
            trace("band %lu accepted -> reported band %lu\n", Bands[i], dwBand);
            ok(Bands[i] <= ZBID_DESKTOP,
               "band %lu must not be creatable without UIAccess\n", Bands[i]);
            DestroyWindow(hwnd);
        }
        else
        {
            DWORD dwErr = GetLastError();

            trace("band %lu rejected, error %lu\n", Bands[i], dwErr);
            if (Bands[i] <= ZBID_MAX && Bands[i] != ZBID_IMMERSIVE_RESTRICTED)
            {
                ok(dwErr == ERROR_ACCESS_DENIED,
                   "band %lu: expected ERROR_ACCESS_DENIED, got %lu\n", Bands[i], dwErr);
            }
            else
            {
                ok(dwErr == ERROR_INVALID_PARAMETER,
                   "band %lu: expected ERROR_INVALID_PARAMETER, got %lu\n", Bands[i], dwErr);
            }
        }
    }

    SetLastError(0xdeadbeef);
    hwnd = pCreateWindowInBand(0, L"ThisClassDoesNotExist", L"nope", WS_OVERLAPPEDWINDOW,
                               20, 20, 120, 120, NULL, NULL, GetModuleHandleW(NULL), NULL,
                               ZBID_DEFAULT);
    ok(hwnd == NULL, "a bogus class must fail\n");
    ok(GetLastError() == ERROR_CANNOT_FIND_WND_CLASS,
       "expected ERROR_CANNOT_FIND_WND_CLASS, got %lu\n", GetLastError());
    if (hwnd)
        DestroyWindow(hwnd);

    SetLastError(0xdeadbeef);
    hwnd = pCreateWindowInBand(0, NULL, NULL, WS_OVERLAPPEDWINDOW,
                               0, 0, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL,
                               ZBID_DEFAULT);
    ok(hwnd == NULL, "a NULL class must fail\n");
    if (hwnd)
        DestroyWindow(hwnd);
}

static void
Test_Churn(void)
{
    UINT i;
    LONG Bad = 0;

    for (i = 0; i < 1500; i++)
    {
        HWND hwnd = pCreateWindowInBand(0, L"STATIC", L"churn", WS_POPUP,
                                        0, 0, 16, 16, NULL, NULL, GetModuleHandleW(NULL), NULL,
                                        ZBID_DEFAULT);
        DWORD dwBand = 0xdeadbeef;

        if (!hwnd)
        {
            ok(FALSE, "CreateWindowInBand failed at %u, error %lu\n", i, GetLastError());
            break;
        }
        if (!pGetWindowBand(hwnd, &dwBand) || dwBand != ZBID_DESKTOP)
            Bad++;
        DestroyWindow(hwnd);
        if (pGetWindowBand(hwnd, &dwBand))
            Bad++;
    }
    ok(Bad == 0, "%ld of 1500 churn cycles misbehaved\n", Bad);
}

START_TEST(WindowBand)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");

    pGetWindowBand = (FN_GetWindowBand)GetProcAddress(hUser32, "GetWindowBand");
    pCreateWindowInBand = (FN_CreateWindowInBand)GetProcAddress(hUser32, "CreateWindowInBand");

    ok(pGetWindowBand != NULL, "user32!GetWindowBand is missing\n");
    ok(pCreateWindowInBand != NULL, "user32!CreateWindowInBand is missing\n");
    if (!pGetWindowBand)
        return;

    Test_BadArguments();
    Test_BandOfEveryStyle();
    Test_ChildAndDesktopBands();

    if (pCreateWindowInBand)
    {
        Test_CreateWindowInBand();
        Test_Churn();
    }
}
