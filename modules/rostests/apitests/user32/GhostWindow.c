/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture tests for GhostWindowFromHungWindow / HungWindowFromGhostWindow
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef HWND (WINAPI *FN_GhostWindowFromHungWindow)(HWND);
typedef HWND (WINAPI *FN_HungWindowFromGhostWindow)(HWND);

static FN_GhostWindowFromHungWindow pGhostWindowFromHungWindow;
static FN_HungWindowFromGhostWindow pHungWindowFromGhostWindow;

static void
Test_BadHandles(void)
{
    static const ULONG_PTR Handles[] =
    {
        0, 1, 2, 0x10, 0xffff, 0xdeadbeef, (ULONG_PTR)-1, (ULONG_PTR)-2
    };
    UINT i;

    for (i = 0; i < ARRAYSIZE(Handles); i++)
    {
        HWND hwndRet = (HWND)(ULONG_PTR)1;

        SetLastError(0xdeadbeef);
        _SEH2_TRY
        {
            hwndRet = pGhostWindowFromHungWindow((HWND)Handles[i]);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ok(FALSE, "GhostWindowFromHungWindow(%p) raised 0x%08lx\n",
               (void *)Handles[i], _SEH2_GetExceptionCode());
            hwndRet = NULL;
        }
        _SEH2_END;
        ok(hwndRet == NULL, "GhostWindowFromHungWindow(%p) returned %p\n",
           (void *)Handles[i], hwndRet);

        hwndRet = (HWND)(ULONG_PTR)1;
        SetLastError(0xdeadbeef);
        _SEH2_TRY
        {
            hwndRet = pHungWindowFromGhostWindow((HWND)Handles[i]);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ok(FALSE, "HungWindowFromGhostWindow(%p) raised 0x%08lx\n",
               (void *)Handles[i], _SEH2_GetExceptionCode());
            hwndRet = NULL;
        }
        _SEH2_END;
        ok(hwndRet == NULL, "HungWindowFromGhostWindow(%p) returned %p\n",
           (void *)Handles[i], hwndRet);
    }

    SetLastError(0xdeadbeef);
    ok(pGhostWindowFromHungWindow((HWND)(ULONG_PTR)0xdeadbeef) == NULL, "bogus handle\n");
    ok(GetLastError() == ERROR_INVALID_WINDOW_HANDLE,
       "expected ERROR_INVALID_WINDOW_HANDLE, got %lu\n", GetLastError());
}

static void
Test_HealthyWindows(void)
{
    HWND hwnd, hwndChild, hwndRet;

    hwnd = CreateWindowExW(0, L"STATIC", L"responsive", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           30, 30, 150, 150, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hwnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwnd)
        return;

    ok(!IsHungAppWindow(hwnd), "our own window must not be hung\n");

    hwndRet = pGhostWindowFromHungWindow(hwnd);
    ok(hwndRet == NULL, "a responsive window has no ghost, got %p\n", hwndRet);

    hwndRet = pHungWindowFromGhostWindow(hwnd);
    ok(hwndRet == NULL, "a normal window is not a ghost, got %p\n", hwndRet);

    hwndChild = CreateWindowExW(0, L"STATIC", L"kid", WS_CHILD | WS_VISIBLE,
                                0, 0, 40, 40, hwnd, NULL, GetModuleHandleW(NULL), NULL);
    if (hwndChild)
    {
        ok(pGhostWindowFromHungWindow(hwndChild) == NULL, "a child window has no ghost\n");
        ok(pHungWindowFromGhostWindow(hwndChild) == NULL, "a child window is not a ghost\n");
        DestroyWindow(hwndChild);
    }

    ok(pGhostWindowFromHungWindow(GetDesktopWindow()) == NULL,
       "the desktop window has no ghost\n");
    ok(pHungWindowFromGhostWindow(GetDesktopWindow()) == NULL,
       "the desktop window is not a ghost\n");

    DestroyWindow(hwnd);

    SetLastError(0xdeadbeef);
    ok(pGhostWindowFromHungWindow(hwnd) == NULL, "a destroyed window has no ghost\n");
    ok(pHungWindowFromGhostWindow(hwnd) == NULL, "a destroyed window is not a ghost\n");
}

static BOOL CALLBACK
SweepProc(HWND hwnd, LPARAM lParam)
{
    LONG *pCounts = (LONG *)lParam;
    HWND hwndGhost, hwndHung;
    WCHAR szClass[64];

    hwndGhost = pGhostWindowFromHungWindow(hwnd);
    hwndHung = pHungWindowFromGhostWindow(hwnd);

    if (hwndGhost)
    {
        pCounts[0]++;
        ok(IsWindow(hwndGhost), "the ghost of %p is not a window\n", hwnd);
        ok(pHungWindowFromGhostWindow(hwndGhost) == hwnd,
           "ghost %p does not map back to %p\n", hwndGhost, hwnd);
    }

    if (hwndHung)
    {
        pCounts[1]++;
        szClass[0] = L'\0';
        GetClassNameW(hwnd, szClass, ARRAYSIZE(szClass));
        ok(wcscmp(szClass, L"Ghost") == 0,
           "%p reports a hung window but its class is %ls\n", hwnd, szClass);
        ok(pGhostWindowFromHungWindow(hwndHung) == hwnd,
           "hung window %p does not map back to ghost %p\n", hwndHung, hwnd);
    }

    ok(!(hwndGhost && hwndHung), "%p cannot be both a ghost and a hung window\n", hwnd);
    return TRUE;
}

static void
Test_SweepDesktop(void)
{
    LONG Counts[2] = { 0, 0 };

    EnumWindows(SweepProc, (LPARAM)Counts);
    trace("desktop sweep: %ld hung windows with ghosts, %ld ghost windows\n",
          Counts[0], Counts[1]);
    ok(Counts[0] == Counts[1],
       "every ghost must pair with exactly one hung window (%ld vs %ld)\n",
       Counts[0], Counts[1]);
}

static void
Test_Hammer(void)
{
    HWND hwnd = CreateWindowExW(0, L"STATIC", L"hammer", WS_OVERLAPPEDWINDOW,
                                10, 10, 60, 60, NULL, NULL, GetModuleHandleW(NULL), NULL);
    UINT i;
    LONG Bad = 0;

    ok(hwnd != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwnd)
        return;

    for (i = 0; i < 20000; i++)
    {
        if (pGhostWindowFromHungWindow(hwnd) != NULL)
            Bad++;
        if (pHungWindowFromGhostWindow(hwnd) != NULL)
            Bad++;
    }
    ok(Bad == 0, "%ld of 40000 hammer calls returned a bogus window\n", Bad);

    DestroyWindow(hwnd);
}

START_TEST(GhostWindow)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");

    pGhostWindowFromHungWindow =
        (FN_GhostWindowFromHungWindow)GetProcAddress(hUser32, "GhostWindowFromHungWindow");
    pHungWindowFromGhostWindow =
        (FN_HungWindowFromGhostWindow)GetProcAddress(hUser32, "HungWindowFromGhostWindow");

    ok(pGhostWindowFromHungWindow != NULL, "user32!GhostWindowFromHungWindow is missing\n");
    ok(pHungWindowFromGhostWindow != NULL, "user32!HungWindowFromGhostWindow is missing\n");
    if (!pGhostWindowFromHungWindow || !pHungWindowFromGhostWindow)
        return;

    Test_BadHandles();
    Test_HealthyWindows();
    Test_SweepDesktop();
    Test_Hammer();
}
