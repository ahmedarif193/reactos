/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests where minimized top-level windows are placed
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

START_TEST(MinimizedPosition)
{
    MINIMIZEDMETRICS Metrics = { sizeof(Metrics) };
    HWND hWnd;
    RECT Rect;

    ok(SystemParametersInfoW(SPI_GETMINIMIZEDMETRICS, sizeof(Metrics), &Metrics, 0),
       "SPI_GETMINIMIZEDMETRICS failed: %lu\n", GetLastError());
    if (!(Metrics.iArrange & ARW_HIDE))
    {
        skip("Minimized windows are not hidden, iArrange 0x%x\n", Metrics.iArrange);
        return;
    }

    hWnd = CreateWindowExW(0, L"static", L"MinimizedPosition", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           100, 100, 200, 200, NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd)
        return;

    ShowWindow(hWnd, SW_MINIMIZE);
    ok(IsIconic(hWnd), "Window is not minimized\n");
    ok(GetWindowRect(hWnd, &Rect), "GetWindowRect failed: %lu\n", GetLastError());
    ok(Rect.left == -32000 && Rect.top == -32000, "Minimized window is at (%ld,%ld)\n", Rect.left, Rect.top);

    DestroyWindow(hWnd);
}
