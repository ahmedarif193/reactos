/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     End-to-end visual-style activation and rendering test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <uxtheme.h>
#include <vsstyle.h>

#define SURFACE_WIDTH 96
#define SURFACE_HEIGHT 48
#define SURFACE_SENTINEL 0x00ff00ff

typedef HRESULT (WINAPI *PSET_SYSTEM_VISUAL_STYLE)(PCWSTR, PCWSTR, PCWSTR, UINT);

static HTHEME g_Theme;
static LONG g_ThemeChangedCount;

static void
flush_messages(DWORD Timeout)
{
    DWORD Deadline = GetTickCount() + Timeout;
    MSG Message;

    do
    {
        while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&Message);
            DispatchMessageW(&Message);
        }

        if ((LONG)(Deadline - GetTickCount()) <= 0)
            break;

        MsgWaitForMultipleObjects(0, NULL, FALSE, 25, QS_ALLINPUT);
    } while (TRUE);
}

static DWORD
hash_pixels(const DWORD *Pixels)
{
    DWORD Hash = 2166136261u;
    UINT Index;

    for (Index = 0; Index < SURFACE_WIDTH * SURFACE_HEIGHT; ++Index)
    {
        Hash ^= Pixels[Index];
        Hash *= 16777619u;
    }

    return Hash;
}

static UINT
count_rendered_pixels(const DWORD *Pixels)
{
    UINT Count = 0;
    UINT Index;

    for (Index = 0; Index < SURFACE_WIDTH * SURFACE_HEIGHT; ++Index)
    {
        if ((Pixels[Index] & 0x00ffffff) != SURFACE_SENTINEL)
            ++Count;
    }

    return Count;
}

static DWORD
render_scrollbar_state(HTHEME Theme, HDC Dc, DWORD *Pixels, INT State, UINT *RenderedPixels)
{
    RECT Rect = { 0, 0, SURFACE_WIDTH, SURFACE_HEIGHT };
    HRESULT Result;
    UINT Index;

    for (Index = 0; Index < SURFACE_WIDTH * SURFACE_HEIGHT; ++Index)
        Pixels[Index] = SURFACE_SENTINEL;

    Result = DrawThemeBackground(Theme, Dc, SBP_ARROWBTN, State, &Rect, NULL);
    ok(Result == S_OK, "DrawThemeBackground state %d returned 0x%08lx\n", State, Result);
    GdiFlush();

    *RenderedPixels = count_rendered_pixels(Pixels);
    return hash_pixels(Pixels);
}

static LRESULT CALLBACK
theme_window_proc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT Paint;
    RECT Client;
    RECT Normal = { 28, 64, 124, 112 };
    RECT Hot = { 152, 64, 248, 112 };
    HDC Dc;

    switch (Message)
    {
        case WM_THEMECHANGED:
            InterlockedIncrement(&g_ThemeChangedCount);
            InvalidateRect(Window, NULL, TRUE);
            break;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            Dc = BeginPaint(Window, &Paint);
            GetClientRect(Window, &Client);
            FillRect(Dc, &Client, GetSysColorBrush(COLOR_WINDOW));
            SetBkMode(Dc, TRANSPARENT);
            SetTextColor(Dc, GetSysColor(COLOR_WINDOWTEXT));
            TextOutW(Dc, 28, 24, L"Current theme scrollbar states", 30);
            TextOutW(Dc, 28, 120, L"Normal", 6);
            TextOutW(Dc, 152, 120, L"Hot", 3);
            if (g_Theme)
            {
                DrawThemeBackground(g_Theme, Dc, SBP_ARROWBTN, ABS_RIGHTNORMAL, &Normal, NULL);
                DrawThemeBackground(g_Theme, Dc, SBP_ARROWBTN, ABS_RIGHTHOT, &Hot, NULL);
            }
            EndPaint(Window, &Paint);
            return 0;
    }

    return DefWindowProcW(Window, Message, wParam, lParam);
}

static BOOL
environment_enabled(PCWSTR Name)
{
    WCHAR Value[8];

    return GetEnvironmentVariableW(Name, Value, ARRAY_SIZE(Value)) && Value[0] != L'0';
}

START_TEST(ThemeRendering)
{
    const WCHAR ClassName[] = L"ReactOSThemeRenderingTest";
    WCHAR ThemePath[MAX_PATH] = L"";
    WCHAR ColorName[64] = L"";
    WCHAR SizeName[64] = L"";
    WCHAR CaptureValue[16];
    BITMAPINFO BitmapInfo;
    PSET_SYSTEM_VISUAL_STYLE SetSystemVisualStyle;
    DWORD OldProperties;
    DWORD NormalHash = 0;
    DWORD HotHash = 0;
    DWORD CaptureMilliseconds = 0;
    LONG ThemeChangedBefore;
    LONG FailuresBefore = winetest_get_failures();
    UINT NormalPixels = 0;
    UINT HotPixels = 0;
    BOOL Active;
    BOOL AppThemedBefore;
    BOOL AppThemedAfter = FALSE;
    HRESULT Result;
    WNDCLASSW Class;
    SCROLLINFO ScrollInfo;
    HMODULE UxTheme;
    HBITMAP Bitmap = NULL;
    HBITMAP OldBitmap = NULL;
    DWORD *Pixels = NULL;
    HWND Window = NULL;
    HDC MemoryDc = NULL;

    Active = IsThemeActive();
    AppThemedBefore = IsAppThemed();
    if (!Active)
    {
        if (environment_enabled(L"UXTHEME_E2E_REQUIRE_ACTIVE"))
            ok(FALSE, "Visual styles are inactive in required-active mode\n");
        else
            skip("Visual styles are inactive\n");
        trace("THEME_E2E_DONE active=0 app_before=%d failures=%ld\n", AppThemedBefore, winetest_get_failures() - FailuresBefore);
        return;
    }

    Result = GetCurrentThemeName(ThemePath, ARRAY_SIZE(ThemePath), ColorName, ARRAY_SIZE(ColorName), SizeName, ARRAY_SIZE(SizeName));
    ok(Result == S_OK, "GetCurrentThemeName returned 0x%08lx\n", Result);
    ok(ThemePath[0] != UNICODE_NULL, "GetCurrentThemeName returned an empty path\n");

    ZeroMemory(&Class, sizeof(Class));
    Class.style = CS_HREDRAW | CS_VREDRAW;
    Class.lpfnWndProc = theme_window_proc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    Class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    Class.lpszClassName = ClassName;
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed with %lu\n", GetLastError());

    Window = CreateWindowExW(0, ClassName, L"NT5 theme end-to-end rendering", WS_OVERLAPPEDWINDOW | WS_HSCROLL | WS_VISIBLE, 160, 140, 520, 280, NULL, NULL, Class.hInstance, NULL);
    ok(Window != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!Window)
        goto Cleanup;

    ZeroMemory(&ScrollInfo, sizeof(ScrollInfo));
    ScrollInfo.cbSize = sizeof(ScrollInfo);
    ScrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
    ScrollInfo.nMin = 0;
    ScrollInfo.nMax = 100;
    ScrollInfo.nPage = 20;
    ScrollInfo.nPos = 40;
    SetScrollInfo(Window, SB_HORZ, &ScrollInfo, TRUE);
    ShowWindow(Window, SW_SHOW);
    UpdateWindow(Window);
    flush_messages(100);

    AppThemedAfter = IsAppThemed();
    ok(AppThemedAfter, "The application did not become themed after creating a window\n");

    if (environment_enabled(L"UXTHEME_E2E_APPLY_CURRENT"))
    {
        UxTheme = GetModuleHandleW(L"uxtheme.dll");
        SetSystemVisualStyle = (PSET_SYSTEM_VISUAL_STYLE)GetProcAddress(UxTheme, MAKEINTRESOURCEA(65));
        ok(SetSystemVisualStyle != NULL, "SetSystemVisualStyle ordinal 65 is missing\n");
        if (SetSystemVisualStyle)
        {
            Result = SetSystemVisualStyle(ThemePath, ColorName, SizeName, 0);
            ok(Result == S_OK, "SetSystemVisualStyle returned 0x%08lx\n", Result);
            flush_messages(200);
        }
    }

    ThemeChangedBefore = g_ThemeChangedCount;
    Result = SetWindowTheme(Window, NULL, NULL);
    ok(Result == S_OK, "SetWindowTheme returned 0x%08lx\n", Result);
    ok(g_ThemeChangedCount > ThemeChangedBefore, "SetWindowTheme did not deliver WM_THEMECHANGED\n");

    OldProperties = GetThemeAppProperties();
    SetThemeAppProperties(STAP_ALLOW_NONCLIENT | STAP_ALLOW_CONTROLS);
    g_Theme = OpenThemeData(Window, VSCLASS_SCROLLBAR);
    ok(g_Theme != NULL, "OpenThemeData(SCROLLBAR) failed with 0x%08lx\n", GetLastError());
    if (!g_Theme)
        goto RestoreProperties;

    MemoryDc = CreateCompatibleDC(NULL);
    ok(MemoryDc != NULL, "CreateCompatibleDC failed with %lu\n", GetLastError());
    if (!MemoryDc)
        goto CloseTheme;

    ZeroMemory(&BitmapInfo, sizeof(BitmapInfo));
    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth = SURFACE_WIDTH;
    BitmapInfo.bmiHeader.biHeight = -SURFACE_HEIGHT;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;
    Bitmap = CreateDIBSection(MemoryDc, &BitmapInfo, DIB_RGB_COLORS, (PVOID *)&Pixels, NULL, 0);
    ok(Bitmap != NULL, "CreateDIBSection failed with %lu\n", GetLastError());
    if (!Bitmap)
        goto DeleteDc;

    OldBitmap = SelectObject(MemoryDc, Bitmap);
    NormalHash = render_scrollbar_state(g_Theme, MemoryDc, Pixels, ABS_RIGHTNORMAL, &NormalPixels);
    HotHash = render_scrollbar_state(g_Theme, MemoryDc, Pixels, ABS_RIGHTHOT, &HotPixels);
    ok(NormalPixels != 0, "The normal scrollbar state rendered no pixels\n");
    ok(HotPixels != 0, "The hot scrollbar state rendered no pixels\n");
    ok(NormalHash != HotHash, "Normal and hot scrollbar states rendered identical pixels (0x%08lx)\n", NormalHash);

    RedrawWindow(Window, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_FRAME);
    SetForegroundWindow(Window);
    trace("THEME_E2E_RENDER_READY active=1 app_before=%d app_after=%d messages=%ld normal=0x%08lx hot=0x%08lx pixels=%u/%u theme=%ls color=%ls size=%ls\n", AppThemedBefore, AppThemedAfter, g_ThemeChangedCount, NormalHash, HotHash, NormalPixels, HotPixels, ThemePath, ColorName, SizeName);

    if (GetEnvironmentVariableW(L"UXTHEME_E2E_CAPTURE_MS", CaptureValue, ARRAY_SIZE(CaptureValue)))
        CaptureMilliseconds = wcstoul(CaptureValue, NULL, 10);
    if (CaptureMilliseconds)
        flush_messages(CaptureMilliseconds);

    SelectObject(MemoryDc, OldBitmap);
    DeleteObject(Bitmap);

DeleteDc:
    DeleteDC(MemoryDc);

CloseTheme:
    CloseThemeData(g_Theme);
    g_Theme = NULL;

RestoreProperties:
    SetThemeAppProperties(OldProperties);

Cleanup:
    if (Window)
        DestroyWindow(Window);
    UnregisterClassW(ClassName, Class.hInstance);
    trace("THEME_E2E_DONE active=1 app_before=%d app_after=%d messages=%ld normal=0x%08lx hot=0x%08lx failures=%ld\n", AppThemedBefore, AppThemedAfter, g_ThemeChangedCount, NormalHash, HotHash, winetest_get_failures() - FailuresBefore);
}
