/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-only (https://spdx.org/licenses/GPL-3.0-only)
 * PURPOSE:     Verifies that nonclient drawing does not overwrite the client area
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "precomp.h"

static const COLORREF ClientColor = RGB(0x35, 0xc7, 0x91);

static LRESULT CALLBACK
NonClientWindowProc(
    HWND hwnd,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    RECT *ClientRect;
    PAINTSTRUCT Paint;

    switch (message)
    {
        case WM_NCCALCSIZE:
            if (wParam)
                ClientRect = &((NCCALCSIZE_PARAMS *)lParam)->rgrc[0];
            else
                ClientRect = (RECT *)lParam;
            InflateRect(ClientRect, -1, -1);
            return 0;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            BeginPaint(hwnd, &Paint);
            EndPaint(hwnd, &Paint);
            return 0;
    }

    return DefWindowProcA(hwnd, message, wParam, lParam);
}

static BOOL
PaintAndSampleClient(
    _In_ HWND hwnd,
    _Out_ POINT *SamplePoint)
{
    HBRUSH Brush;
    RECT ClientRect;
    COLORREF Color;
    HDC hdc;

    if (!GetClientRect(hwnd, &ClientRect) || IsRectEmpty(&ClientRect)) return FALSE;

    SamplePoint->x = (ClientRect.right - ClientRect.left) / 2;
    SamplePoint->y = min(2, ClientRect.bottom - ClientRect.top - 1);
    hdc = GetDC(hwnd);
    if (hdc == NULL) return FALSE;

    Brush = CreateSolidBrush(ClientColor);
    if (Brush == NULL)
    {
        ReleaseDC(hwnd, hdc);
        return FALSE;
    }

    FillRect(hdc, &ClientRect, Brush);
    DeleteObject(Brush);
    Color = GetPixel(hdc, SamplePoint->x, SamplePoint->y);
    ReleaseDC(hwnd, hdc);
    return Color == ClientColor;
}

static COLORREF
SampleClient(
    _In_ HWND hwnd,
    _In_ const POINT *SamplePoint)
{
    COLORREF Color;
    HDC hdc;

    hdc = GetDC(hwnd);
    if (hdc == NULL) return CLR_INVALID;
    Color = GetPixel(hdc, SamplePoint->x, SamplePoint->y);
    ReleaseDC(hwnd, hdc);
    return Color;
}

START_TEST(NonClientPaint)
{
    WNDCLASSA WindowClass;
    COLORREF Color;
    POINT SamplePoint;
    HWND hwnd;

    ZeroMemory(&WindowClass, sizeof(WindowClass));
    WindowClass.lpfnWndProc = NonClientWindowProc;
    WindowClass.hInstance = GetModuleHandleA(NULL);
    WindowClass.hCursor = LoadCursorA(NULL, IDC_ARROW);
    WindowClass.lpszClassName = "NonClientPaintTestClass";
    if (!RegisterClassA(&WindowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        skip("RegisterClassA failed, error %lu\n", GetLastError());
        return;
    }

    hwnd = CreateWindowExA(0, WindowClass.lpszClassName, "Nonclient clipping test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 320, 200, NULL, NULL, WindowClass.hInstance, NULL);
    ok(hwnd != NULL, "CreateWindowExA failed, error %lu\n", GetLastError());
    if (hwnd == NULL) return;

    UpdateWindow(hwnd);
    if (!PaintAndSampleClient(hwnd, &SamplePoint))
    {
        skip("The display driver cannot sample window pixels\n");
        DestroyWindow(hwnd);
        return;
    }
    SendMessageA(hwnd, WM_NCPAINT, 1, 0);
    Color = SampleClient(hwnd, &SamplePoint);
    ok(Color == ClientColor, "WM_NCPAINT overwrote the client pixel with color %#lx\n", Color);

    ok(PaintAndSampleClient(hwnd, &SamplePoint), "Failed to reset the client-area sample pixel\n");
    SendMessageA(hwnd, WM_NCACTIVATE, TRUE, 0);
    Color = SampleClient(hwnd, &SamplePoint);
    ok(Color == ClientColor, "WM_NCACTIVATE overwrote the client pixel with color %#lx\n", Color);

    DestroyWindow(hwnd);
}
