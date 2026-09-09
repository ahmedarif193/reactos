/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Retained drawing context clipping regression tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <wine/test.h>

static LRESULT CALLBACK RegionWindowProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    return DefWindowProcA(Window, Message, WParam, LParam);
}

static void DispatchPaint(void)
{
    MSG Message;
    DWORD End = GetTickCount() + 150;

    do
    {
        if (PeekMessageA(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageA(&Message);
        }
        else
            Sleep(1);
    } while ((LONG)(GetTickCount() - End) < 0);
}

static void CheckClip(HWND Window, HDC Dc, const char *Name, const RECT *Expected)
{
    RECT Clip, Client;
    int Type;

    DispatchPaint();
    GetClientRect(Window, &Client);
    if (!Expected)
        Expected = &Client;
    Type = GetClipBox(Dc, &Clip);
    ok(Type == SIMPLEREGION || Type == COMPLEXREGION, "%s: clip type %d, error %lu\n", Name, Type, GetLastError());
    ok(EqualRect(&Clip, Expected), "%s: clip (%ld,%ld)-(%ld,%ld), expected (%ld,%ld)-(%ld,%ld)\n", Name, Clip.left, Clip.top, Clip.right, Clip.bottom, Expected->left, Expected->top, Expected->right, Expected->bottom);
}

static void SetRegion(HWND Window, const RECT *Rect)
{
    HRGN Region = Rect ? CreateRectRgnIndirect(Rect) : NULL;
    int Result;

    if (Rect)
    {
        ok(Region != NULL, "CreateRectRgnIndirect failed: %lu\n", GetLastError());
        if (!Region)
            return;
    }
    Result = SetWindowRgn(Window, Region, TRUE);
    ok(Result != 0, "SetWindowRgn failed: %lu\n", GetLastError());
    if (!Result && Region)
        DeleteObject(Region);
}

static void TestWindowRegionDC(UINT ClassStyle)
{
    static const RECT Small = {0, 0, 160, 120};
    static const RECT Full = {0, 0, 320, 240};
    static const RECT Tiny = {0, 0, 96, 80};
    WNDCLASSA Class = {0};
    HWND Window;
    HDC Dc;
    HBRUSH Brush;
    COLORREF Pixel;
    BOOL Result;

    trace("window region: class style %x\n", ClassStyle);
    Class.style = ClassStyle;
    Class.lpfnWndProc = RegionWindowProc;
    Class.hInstance = GetModuleHandleA(NULL);
    Class.lpszClassName = "WindowRegionDC";
    ok(RegisterClassA(&Class) != 0, "RegisterClassA failed: %lu\n", GetLastError());
    Window = CreateWindowExA(WS_EX_TOPMOST, Class.lpszClassName, "Window region", WS_POPUP | WS_VISIBLE, 32, 32, 320, 240, NULL, NULL, Class.hInstance, NULL);
    ok(Window != NULL, "CreateWindowExA failed: %lu\n", GetLastError());
    if (!Window)
        goto CleanupClass;

    SetRegion(Window, &Small);
    Dc = GetDC(Window);
    ok(Dc != NULL, "GetDC failed: %lu\n", GetLastError());
    if (!Dc)
        goto CleanupWindow;
    CheckClip(Window, Dc, "initial region", &Small);
    SetRegion(Window, &Full);
    CheckClip(Window, Dc, "expanded region", &Full);
    Brush = CreateSolidBrush(RGB(0, 255, 0));
    ok(Brush != NULL, "CreateSolidBrush failed: %lu\n", GetLastError());
    ok(FillRect(Dc, &Full, Brush) != 0, "FillRect failed: %lu\n", GetLastError());
    GdiFlush();
    Pixel = GetPixel(Dc, 250, 180);
    ok(Pixel == RGB(0, 255, 0), "newly exposed pixel is %#lx, expected green\n", Pixel);
    DeleteObject(Brush);
    SetRegion(Window, &Tiny);
    CheckClip(Window, Dc, "shrunk region", &Tiny);
    SetRegion(Window, NULL);
    CheckClip(Window, Dc, "cleared region", &Full);
    Result = SetWindowPos(Window, NULL, 0, 0, 420, 300, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ok(Result, "SetWindowPos failed: %lu\n", GetLastError());
    CheckClip(Window, Dc, "resized popup", NULL);
    ReleaseDC(Window, Dc);
    DestroyWindow(Window);

    /* The nonclient theme may install its own region after a resize. */
    Window = CreateWindowExA(WS_EX_TOPMOST, Class.lpszClassName, "Themed resize", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 32, 32, 320, 240, NULL, NULL, Class.hInstance, NULL);
    ok(Window != NULL, "CreateWindowExA failed: %lu\n", GetLastError());
    if (!Window)
        goto CleanupClass;
    DispatchPaint();
    Dc = GetDC(Window);
    ok(Dc != NULL, "GetDC failed: %lu\n", GetLastError());
    if (!Dc)
        goto CleanupWindow;
    CheckClip(Window, Dc, "themed initial", NULL);
    Result = SetWindowPos(Window, NULL, 0, 0, 420, 340, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ok(Result, "SetWindowPos failed: %lu\n", GetLastError());
    CheckClip(Window, Dc, "themed expanded", NULL);
    SetWindowLongA(Window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    Result = SetWindowPos(Window, NULL, 0, 0, 520, 400, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ok(Result, "SetWindowPos failed: %lu\n", GetLastError());
    CheckClip(Window, Dc, "caption removed", NULL);

    /* An application-owned region must survive the same style transition. */
    SetWindowLongA(Window, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
    Result = SetWindowPos(Window, NULL, 0, 0, 520, 400, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ok(Result, "SetWindowPos failed: %lu\n", GetLastError());
    DispatchPaint();
    SetRegion(Window, &Small);
    SetWindowLongA(Window, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    Result = SetWindowPos(Window, NULL, 0, 0, 520, 400, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ok(Result, "SetWindowPos failed: %lu\n", GetLastError());
    CheckClip(Window, Dc, "application region after caption removal", &Small);
    ReleaseDC(Window, Dc);

CleanupWindow:
    DestroyWindow(Window);
CleanupClass:
    UnregisterClassA(Class.lpszClassName, Class.hInstance);
}

START_TEST(WindowRegionDC)
{
    TestWindowRegionDC(CS_HREDRAW | CS_VREDRAW);
    TestWindowRegionDC(CS_HREDRAW | CS_VREDRAW | CS_OWNDC);
}
