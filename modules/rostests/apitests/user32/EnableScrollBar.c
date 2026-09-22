/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests EnableScrollBar return values
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(EnableScrollBar)
{
    HWND hWnd;

    hWnd = CreateWindowExW(0, L"static", NULL, WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VSCROLL,
                           0, 0, 200, 200, NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd)
        return;

    SetScrollRange(hWnd, SB_HORZ, 0, 100, FALSE);
    SetScrollRange(hWnd, SB_VERT, 0, 100, FALSE);

    ok(EnableScrollBar(hWnd, SB_HORZ, ESB_DISABLE_BOTH), "Disabling the horizontal bar failed\n");
    ok(!EnableScrollBar(hWnd, SB_HORZ, ESB_DISABLE_BOTH), "Disabling a disabled bar succeeded\n");
    ok(EnableScrollBar(hWnd, SB_HORZ, ESB_ENABLE_BOTH), "Enabling the horizontal bar failed\n");
    ok(!EnableScrollBar(hWnd, SB_HORZ, ESB_ENABLE_BOTH), "Enabling an enabled bar succeeded\n");

    ok(EnableScrollBar(hWnd, SB_BOTH, ESB_DISABLE_LTUP), "Disabling both left/up arrows failed\n");
    ok(!EnableScrollBar(hWnd, SB_BOTH, ESB_DISABLE_LTUP), "Disabling disabled arrows succeeded\n");
    ok(EnableScrollBar(hWnd, SB_VERT, ESB_ENABLE_BOTH), "Enabling the vertical bar failed\n");

    DestroyWindow(hWnd);
}
