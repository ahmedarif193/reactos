/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Torture tests for IsTopLevelWindow
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"
#include <pseh/pseh2.h>

typedef BOOL (WINAPI *FN_IsTopLevelWindow)(HWND);

static FN_IsTopLevelWindow pIsTopLevelWindow;

static HWND
MakeWindow(_In_ DWORD dwStyle, _In_opt_ HWND hwndParent)
{
    return CreateWindowExW(0, L"STATIC", L"itlw", dwStyle,
                           10, 10, 60, 60, hwndParent, NULL, GetModuleHandleW(NULL), NULL);
}

static void
Test_BadHandles(void)
{
    static const ULONG_PTR Handles[] =
    {
        0, 1, 2, 3, 0x10, 0xffff, 0xdeadbeef, (ULONG_PTR)-1, (ULONG_PTR)-2
    };
    UINT i;

    for (i = 0; i < ARRAYSIZE(Handles); i++)
    {
        BOOL ret = TRUE;

        SetLastError(0xdeadbeef);
        _SEH2_TRY
        {
            ret = pIsTopLevelWindow((HWND)Handles[i]);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ok(FALSE, "handle %p raised 0x%08lx\n",
               (void *)Handles[i], _SEH2_GetExceptionCode());
            ret = FALSE;
        }
        _SEH2_END;

        ok(!ret, "handle %p must not be top-level\n", (void *)Handles[i]);
    }
}

static void
Test_Semantics(void)
{
    HWND hwndDesktop = GetDesktopWindow();
    HWND hwndTop, hwndChild, hwndGrandChild, hwndPopup, hwndOwned, hwndMessage;
    LONG_PTR style;

    ok(!pIsTopLevelWindow(hwndDesktop),
       "the desktop window itself is not reported as top-level\n");

    hwndTop = MakeWindow(WS_OVERLAPPEDWINDOW, NULL);
    ok(hwndTop != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwndTop)
        return;

    ok(pIsTopLevelWindow(hwndTop), "an overlapped window is top-level\n");
    ok(GetAncestor(hwndTop, GA_PARENT) == hwndDesktop,
       "sanity: the overlapped window's parent is the desktop\n");

    hwndChild = MakeWindow(WS_CHILD, hwndTop);
    ok(hwndChild != NULL, "child CreateWindowExW failed, error %lu\n", GetLastError());
    if (hwndChild)
    {
        ok(!pIsTopLevelWindow(hwndChild), "a WS_CHILD window is not top-level\n");

        style = GetWindowLongPtrW(hwndChild, GWL_STYLE);
        SetWindowLongPtrW(hwndChild, GWL_STYLE, style & ~WS_CHILD);
        ok(!pIsTopLevelWindow(hwndChild),
           "clearing WS_CHILD alone must not make a child top-level\n");
        SetWindowLongPtrW(hwndChild, GWL_STYLE, style);

        hwndGrandChild = MakeWindow(WS_CHILD, hwndChild);
        ok(hwndGrandChild != NULL, "grandchild failed, error %lu\n", GetLastError());
        if (hwndGrandChild)
        {
            ok(!pIsTopLevelWindow(hwndGrandChild), "a grandchild is not top-level\n");
            DestroyWindow(hwndGrandChild);
        }

        ok(SetParent(hwndChild, hwndDesktop) != NULL,
           "SetParent to the desktop failed, error %lu\n", GetLastError());
        ok(pIsTopLevelWindow(hwndChild),
           "reparenting to the desktop makes a window top-level\n");

        ok(SetParent(hwndChild, hwndTop) != NULL,
           "SetParent back failed, error %lu\n", GetLastError());
        ok(!pIsTopLevelWindow(hwndChild),
           "reparenting away from the desktop removes top-level status\n");

        DestroyWindow(hwndChild);
    }

    hwndPopup = MakeWindow(WS_POPUP, NULL);
    ok(hwndPopup != NULL, "popup failed, error %lu\n", GetLastError());
    if (hwndPopup)
    {
        ok(pIsTopLevelWindow(hwndPopup), "a WS_POPUP window is top-level\n");
        DestroyWindow(hwndPopup);
    }

    hwndOwned = MakeWindow(WS_POPUP, hwndTop);
    ok(hwndOwned != NULL, "owned popup failed, error %lu\n", GetLastError());
    if (hwndOwned)
    {
        ok(GetAncestor(hwndOwned, GA_PARENT) == hwndDesktop,
           "sanity: an owned popup still parents to the desktop\n");
        ok(pIsTopLevelWindow(hwndOwned), "an owned popup is top-level\n");
        DestroyWindow(hwndOwned);
    }

    hwndMessage = CreateWindowExW(0, L"STATIC", L"msg", 0, 0, 0, 0, 0,
                                  HWND_MESSAGE, NULL, GetModuleHandleW(NULL), NULL);
    if (hwndMessage)
    {
        ok(!pIsTopLevelWindow(hwndMessage),
           "a message-only window is not top-level\n");
        DestroyWindow(hwndMessage);
    }
    else
    {
        trace("HWND_MESSAGE unavailable, error %lu\n", GetLastError());
    }

    DestroyWindow(hwndTop);
    ok(!pIsTopLevelWindow(hwndTop), "a destroyed window is not top-level\n");
}

static void
Test_ForeignWindows(void)
{
    HWND hwnd = GetShellWindow();
    UINT nTop = 0, nNotTop = 0;

    if (hwnd)
    {
        ok(pIsTopLevelWindow(hwnd) || !IsWindow(hwnd),
           "the shell window should be top-level\n");
    }

    for (hwnd = GetWindow(GetDesktopWindow(), GW_CHILD);
         hwnd != NULL;
         hwnd = GetWindow(hwnd, GW_HWNDNEXT))
    {
        if (pIsTopLevelWindow(hwnd))
            nTop++;
        else
            nNotTop++;
    }

    trace("desktop children: %u top-level, %u not\n", nTop, nNotTop);
    ok(nTop > 0, "at least one desktop child must be top-level\n");
    ok(nNotTop == 0, "every direct desktop child must be top-level, %u were not\n", nNotTop);
}

static void
Test_Hammer(void)
{
    HWND hwndTop = MakeWindow(WS_OVERLAPPEDWINDOW, NULL);
    HWND hwndChild;
    UINT i;
    LONG Bad = 0;

    ok(hwndTop != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!hwndTop)
        return;

    hwndChild = MakeWindow(WS_CHILD, hwndTop);
    ok(hwndChild != NULL, "child failed, error %lu\n", GetLastError());

    for (i = 0; i < 20000; i++)
    {
        if (!pIsTopLevelWindow(hwndTop))
            Bad++;
        if (hwndChild && pIsTopLevelWindow(hwndChild))
            Bad++;
    }
    ok(Bad == 0, "%ld of 40000 hammer calls returned the wrong answer\n", Bad);

    if (hwndChild)
        DestroyWindow(hwndChild);
    DestroyWindow(hwndTop);
}

static void
Test_ChurnHandles(void)
{
    UINT i;
    LONG Bad = 0;

    for (i = 0; i < 2000; i++)
    {
        HWND hwnd = MakeWindow(WS_OVERLAPPEDWINDOW, NULL);

        if (!hwnd)
        {
            ok(FALSE, "window creation failed at %u, error %lu\n", i, GetLastError());
            break;
        }
        if (!pIsTopLevelWindow(hwnd))
            Bad++;
        DestroyWindow(hwnd);
        if (pIsTopLevelWindow(hwnd))
            Bad++;
    }
    ok(Bad == 0, "%ld create/destroy cycles gave the wrong answer\n", Bad);
}

START_TEST(IsTopLevelWindow)
{
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");

    pIsTopLevelWindow = (FN_IsTopLevelWindow)GetProcAddress(hUser32, "IsTopLevelWindow");
    ok(pIsTopLevelWindow != NULL, "user32!IsTopLevelWindow is missing\n");
    if (!pIsTopLevelWindow)
        return;

    Test_BadHandles();
    Test_Semantics();
    Test_ForeignWindows();
    Test_Hammer();
    Test_ChurnHandles();
}
