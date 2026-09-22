/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for NtUserDeferWindowPos
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "../win32nt.h"

START_TEST(NtUserDeferWindowPos)
{
    HDWP Defer, Next;
    RECT Rect;
    HWND hWnd;

    hWnd = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd) return;

    Defer = BeginDeferWindowPos(1);
    ok(Defer != NULL, "BeginDeferWindowPos failed: %lu\n", GetLastError());
    if (Defer)
    {
        Next = NtUserDeferWindowPos(Defer, hWnd, HWND_TOP, 20, 30, 40, 50, SWP_NOZORDER | SWP_NOACTIVATE);
        ok(Next != NULL, "NtUserDeferWindowPos failed: %lu\n", GetLastError());
        if (Next)
        {
            ok(EndDeferWindowPos(Next), "EndDeferWindowPos failed: %lu\n", GetLastError());
            ok(GetWindowRect(hWnd, &Rect), "GetWindowRect failed\n");
            ok_long(Rect.left, 20);
            ok_long(Rect.top, 30);
            ok_long(Rect.right, 60);
            ok_long(Rect.bottom, 80);
        }
    }

    DestroyWindow(hWnd);
}
