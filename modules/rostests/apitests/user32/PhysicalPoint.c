/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture tests for GetPhysicalCursorPos and WindowFromPhysicalPoint
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef BOOL (WINAPI *FN_GetPhysicalCursorPos)(LPPOINT);
typedef HWND (WINAPI *FN_WindowFromPhysicalPoint)(POINT);
typedef BOOL (WINAPI *FN_PhysicalToLogicalPoint)(HWND, LPPOINT);
typedef BOOL (WINAPI *FN_LogicalToPhysicalPoint)(HWND, LPPOINT);

static FN_GetPhysicalCursorPos pGetPhysicalCursorPos;
static FN_WindowFromPhysicalPoint pWindowFromPhysicalPoint;
static FN_PhysicalToLogicalPoint pPhysicalToLogicalPoint;
static FN_LogicalToPhysicalPoint pLogicalToPhysicalPoint;

static void
Test_GetPhysicalCursorPos(void)
{
    POINT ptLogical, ptPhysical;
    BOOL ret;
    UINT i;
    LONG Mismatches = 0;

    SetLastError(0xdeadbeef);
    ret = pGetPhysicalCursorPos(NULL);
    ok(!ret, "GetPhysicalCursorPos(NULL) must fail\n");
    trace("NULL: error %lu\n", GetLastError());

    ret = TRUE;
    _SEH2_TRY
    {
        ret = pGetPhysicalCursorPos((LPPOINT)InvalidPointer);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        trace("bogus pointer raised 0x%08lx\n", _SEH2_GetExceptionCode());
        ret = FALSE;
    }
    _SEH2_END;
    ok(!ret, "a bogus pointer must not report success\n");

    ptPhysical.x = ptPhysical.y = 0x7fffffff;
    ok(pGetPhysicalCursorPos(&ptPhysical), "GetPhysicalCursorPos failed, error %lu\n",
       GetLastError());
    ok(ptPhysical.x != 0x7fffffff && ptPhysical.y != 0x7fffffff,
       "GetPhysicalCursorPos must fill the POINT\n");

    for (i = 0; i < 5000; i++)
    {
        if (!GetCursorPos(&ptLogical))
            continue;
        if (!pGetPhysicalCursorPos(&ptPhysical))
        {
            ok(FALSE, "iteration %u failed, error %lu\n", i, GetLastError());
            break;
        }
        if (ptPhysical.x != ptLogical.x || ptPhysical.y != ptLogical.y)
            Mismatches++;
    }
    ok(Mismatches == 0,
       "%ld of 5000 samples disagreed with GetCursorPos without DPI virtualization\n",
       Mismatches);
}

static void
Test_CursorRoundTrip(void)
{
    static const struct { int x, y; } Spots[] =
    {
        { 0, 0 }, { 1, 1 }, { 10, 10 }, { 100, 50 }, { 0, 0 }
    };
    POINT ptSaved, pt;
    UINT i;

    if (!GetCursorPos(&ptSaved))
    {
        skip("GetCursorPos failed, error %lu\n", GetLastError());
        return;
    }

    for (i = 0; i < ARRAYSIZE(Spots); i++)
    {
        if (!SetCursorPos(Spots[i].x, Spots[i].y))
            continue;

        pt.x = pt.y = 0x7fffffff;
        ok(pGetPhysicalCursorPos(&pt), "GetPhysicalCursorPos failed at %d,%d, error %lu\n",
           Spots[i].x, Spots[i].y, GetLastError());
        trace("SetCursorPos(%d,%d) -> physical (%ld,%ld)\n",
              Spots[i].x, Spots[i].y, pt.x, pt.y);
    }

    SetCursorPos(ptSaved.x, ptSaved.y);
}

static void
Test_WindowFromPhysicalPoint(void)
{
    HWND hwnd, hwndHit, hwndLogical;
    POINT pt;
    RECT rc;
    UINT i;
    LONG Mismatches = 0;

    hwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", L"physical",
                           WS_POPUP | WS_VISIBLE,
                           40, 40, 160, 160, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hwnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwnd)
        return;

    SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ok(GetWindowRect(hwnd, &rc), "GetWindowRect failed, error %lu\n", GetLastError());

    pt.x = rc.left + (rc.right - rc.left) / 2;
    pt.y = rc.top + (rc.bottom - rc.top) / 2;

    hwndHit = pWindowFromPhysicalPoint(pt);
    hwndLogical = WindowFromPoint(pt);
    ok(hwndHit == hwndLogical,
       "WindowFromPhysicalPoint %p != WindowFromPoint %p\n", hwndHit, hwndLogical);
    if (hwndHit != hwnd)
        trace("another window (%p) occludes ours (%p) at %ld,%ld\n", hwndHit, hwnd, pt.x, pt.y);

    for (i = 0; i < 400; i++)
    {
        pt.x = rc.left + (LONG)(i % 160);
        pt.y = rc.top + (LONG)(i % 160);
        if (pWindowFromPhysicalPoint(pt) != WindowFromPoint(pt))
            Mismatches++;
    }
    ok(Mismatches == 0,
       "%ld of 400 points disagreed with WindowFromPoint\n", Mismatches);

    pt.x = -30000;
    pt.y = -30000;
    ok(pWindowFromPhysicalPoint(pt) == NULL, "an off-desktop point must return NULL\n");

    pt.x = 0x7FFFFFFF;
    pt.y = 0x7FFFFFFF;
    ok(pWindowFromPhysicalPoint(pt) == WindowFromPoint(pt),
       "INT_MAX point must match WindowFromPoint\n");

    pt.x = (LONG)0x80000000;
    pt.y = (LONG)0x80000000;
    ok(pWindowFromPhysicalPoint(pt) == WindowFromPoint(pt),
       "INT_MIN point must match WindowFromPoint\n");

    DestroyWindow(hwnd);
}

static void
Test_PhysicalLogicalPair(void)
{
    HWND hwnd;
    POINT pt;
    RECT rc;
    BOOL ret;

    if (!pLogicalToPhysicalPoint)
    {
        skip("LogicalToPhysicalPoint is unavailable\n");
        return;
    }

    hwnd = CreateWindowExW(0, L"STATIC", L"pair", WS_POPUP | WS_VISIBLE,
                           60, 60, 120, 120, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hwnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwnd)
        return;

    ok(GetWindowRect(hwnd, &rc), "GetWindowRect failed\n");
    pt.x = rc.left + 10;
    pt.y = rc.top + 10;

    ret = pLogicalToPhysicalPoint(hwnd, &pt);
    trace("LogicalToPhysicalPoint -> %d (%ld,%ld)\n", ret, pt.x, pt.y);
    if (ret)
    {
        ok(pt.x == rc.left + 10 && pt.y == rc.top + 10,
           "no DPI virtualization: the point must be unchanged, got %ld,%ld\n", pt.x, pt.y);
    }

    if (pPhysicalToLogicalPoint)
    {
        ret = pPhysicalToLogicalPoint(hwnd, &pt);
        trace("PhysicalToLogicalPoint -> %d (%ld,%ld)\n", ret, pt.x, pt.y);
        if (ret)
        {
            ok(pt.x == rc.left + 10 && pt.y == rc.top + 10,
               "round trip must be identity, got %ld,%ld\n", pt.x, pt.y);
        }
    }

    SetLastError(0xdeadbeef);
    ok(!pLogicalToPhysicalPoint(NULL, &pt), "LogicalToPhysicalPoint(NULL hwnd) must fail\n");
    SetLastError(0xdeadbeef);
    ok(!pLogicalToPhysicalPoint(hwnd, NULL), "LogicalToPhysicalPoint(NULL point) must fail\n");

    DestroyWindow(hwnd);
}

static DWORD WINAPI
HammerThread(LPVOID pv)
{
    LONG *pFailures = (LONG *)pv;
    POINT pt;
    UINT i;

    for (i = 0; i < 4000; i++)
    {
        if (!pGetPhysicalCursorPos(&pt))
        {
            InterlockedIncrement(pFailures);
            break;
        }
        if (pWindowFromPhysicalPoint(pt) != WindowFromPoint(pt))
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

START_TEST(PhysicalPoint)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");

    pGetPhysicalCursorPos = (FN_GetPhysicalCursorPos)GetProcAddress(hUser32, "GetPhysicalCursorPos");
    pWindowFromPhysicalPoint = (FN_WindowFromPhysicalPoint)GetProcAddress(hUser32, "WindowFromPhysicalPoint");
    pPhysicalToLogicalPoint = (FN_PhysicalToLogicalPoint)GetProcAddress(hUser32, "PhysicalToLogicalPoint");
    pLogicalToPhysicalPoint = (FN_LogicalToPhysicalPoint)GetProcAddress(hUser32, "LogicalToPhysicalPoint");

    ok(pGetPhysicalCursorPos != NULL, "user32!GetPhysicalCursorPos is missing\n");
    ok(pWindowFromPhysicalPoint != NULL, "user32!WindowFromPhysicalPoint is missing\n");
    if (!pGetPhysicalCursorPos || !pWindowFromPhysicalPoint)
        return;

    Test_GetPhysicalCursorPos();
    Test_CursorRoundTrip();
    Test_WindowFromPhysicalPoint();
    Test_PhysicalLogicalPair();
    Test_Threaded();
}
