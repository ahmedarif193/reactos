/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Window update region regression tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static BOOL ValidatePaint;
static UINT PaintCount;

static LRESULT CALLBACK PaintProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_PAINT)
    {
        ++PaintCount;
        if (ValidatePaint)
        {
            PAINTSTRUCT ps;
            BeginPaint(hwnd, &ps);
            EndPaint(hwnd, &ps);
        }
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

START_TEST(UpdateRegion)
{
    WNDCLASSW wc = {0};
    HWND hwnd;
    HRGN region;
    MSG msg = {0};
    RECT rect;
    UINT i;

    wc.lpfnWndProc = PaintProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = L"UpdateRegionApiTest";
    if (!RegisterClassW(&wc))
    {
        skip("RegisterClassW failed: %lu\n", GetLastError());
        return;
    }
    hwnd = CreateWindowW(wc.lpszClassName, L"Update region", WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, NULL, NULL, wc.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindowW failed: %lu\n", GetLastError());
    if (!hwnd) goto Cleanup;
    region = CreateRectRgn(0, 0, 0, 0);
    ok(region != NULL, "CreateRectRgn failed\n");
    if (!region) goto Destroy;

    ValidatePaint = TRUE;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    ValidateRect(hwnd, NULL);
    ValidatePaint = FALSE;
    PaintCount = 0;
    msg.hwnd = hwnd;
    msg.message = WM_PAINT;
    for (i = 0; i < 16; ++i) DispatchMessageW(&msg);
    ok(PaintCount == 16, "Expected 16 paint dispatches, got %u\n", PaintCount);
    ok(!GetUpdateRect(hwnd, &rect, FALSE), "Explicit WM_PAINT invalidated the window\n");
    ok(!PeekMessageW(&msg, hwnd, WM_PAINT, WM_PAINT, PM_REMOVE), "Explicit WM_PAINT was requeued\n");

    /* An empty internal paint is delivered once and does not require BeginPaint. */
    for (i = 0; i < 64; ++i)
    {
        ok(RedrawWindow(hwnd, NULL, NULL, RDW_INTERNALPAINT), "Requesting an internal paint failed\n");
        ok(!GetUpdateRect(hwnd, NULL, FALSE), "Internal paint created an update region\n");
        ok(PeekMessageW(&msg, hwnd, WM_PAINT, WM_PAINT, PM_REMOVE), "Internal paint was not queued\n");
        DispatchMessageW(&msg);
        ok(!GetUpdateRect(hwnd, NULL, FALSE), "Internal paint dispatch invalidated the window\n");
        ok(!PeekMessageW(&msg, hwnd, WM_PAINT, WM_PAINT, PM_REMOVE), "Internal paint was delivered more than once\n");
    }

    InvalidateRect(hwnd, NULL, FALSE);
    ok(GetUpdateRect(hwnd, &rect, FALSE), "GetUpdateRect lost the invalid region: %lu\n", GetLastError());
    ok(GetUpdateRgn(hwnd, region, FALSE) == SIMPLEREGION, "GetUpdateRgn lost the invalid region: %lu\n", GetLastError());
    ok(PeekMessageW(&msg, hwnd, WM_PAINT, WM_PAINT, PM_REMOVE), "No WM_PAINT for invalidated window\n");
    DispatchMessageW(&msg);
    ok(GetUpdateRect(hwnd, &rect, FALSE), "Unprocessed paint validated the update region\n");

    ValidatePaint = TRUE;
    DispatchMessageW(&msg);
    ok(!GetUpdateRect(hwnd, &rect, FALSE), "BeginPaint did not validate the rectangle\n");
    ok(GetUpdateRgn(hwnd, region, FALSE) == NULLREGION, "BeginPaint did not validate the region\n");
    InvalidateRect(hwnd, NULL, FALSE);
    PaintCount = 0;
    ok(UpdateWindow(hwnd), "UpdateWindow failed: %lu\n", GetLastError());
    ok(PaintCount == 1, "UpdateWindow dispatched %u paint messages\n", PaintCount);
    ok(!GetUpdateRect(hwnd, &rect, FALSE), "UpdateWindow did not complete painting\n");
    DeleteObject(region);
Destroy:
    ok(DestroyWindow(hwnd), "DestroyWindow failed: %lu\n", GetLastError());
Cleanup:
    ok(UnregisterClassW(wc.lpszClassName, wc.hInstance), "UnregisterClassW failed: %lu\n", GetLastError());
}
