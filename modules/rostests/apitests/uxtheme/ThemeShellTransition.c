/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     End-to-end Explorer theme-transition test
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <tlhelp32.h>
#include <uxtheme.h>
#include <vssym32.h>

#define WM_APPLY_SHELL_THEME (WM_APP + 1)

typedef HRESULT (WINAPI *PSET_SYSTEM_VISUAL_STYLE)(PCWSTR, PCWSTR, PCWSTR, UINT);

static PSET_SYSTEM_VISUAL_STYLE g_SetSystemVisualStyle;
static PCWSTR g_ThemePath;
static PCWSTR g_ColorName;
static PCWSTR g_SizeName;
static HWND g_TrayWindow;
static HRESULT g_ApplyResult;
static LONG g_ThemeChangedCount;
static LONG g_ThemeChangedAtReturn;
static LONG_PTR g_TrayStyleAtReturn;

struct find_window_context
{
    PCWSTR ClassName;
    HWND Window;
};

static void
pump_messages(DWORD Timeout)
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

static BOOL CALLBACK
find_descendant(HWND Window, LPARAM Context)
{
    struct find_window_context *Find = (struct find_window_context *)Context;
    WCHAR ClassName[64];

    if (GetClassNameW(Window, ClassName, ARRAY_SIZE(ClassName)) && !lstrcmpW(ClassName, Find->ClassName))
    {
        Find->Window = Window;
        return FALSE;
    }

    return TRUE;
}

static HWND
find_shell_descendant(HWND Parent, PCWSTR ClassName)
{
    struct find_window_context Find = { ClassName, NULL };

    EnumChildWindows(Parent, find_descendant, (LPARAM)&Find);
    return Find.Window;
}

static HWND
find_shell_child(HWND Parent, PCWSTR ClassName)
{
    HWND Window;
    WCHAR CurrentClass[64];

    for (Window = GetWindow(Parent, GW_CHILD); Window; Window = GetWindow(Window, GW_HWNDNEXT))
    {
        if (GetClassNameW(Window, CurrentClass, ARRAY_SIZE(CurrentClass)) && !lstrcmpW(CurrentClass, ClassName))
            return Window;
    }

    return NULL;
}

static void
trace_shell_modules(HWND TrayWindow)
{
    MODULEENTRY32W Module;
    HANDLE Snapshot;
    DWORD ProcessId;
    BOOL More;

    GetWindowThreadProcessId(TrayWindow, &ProcessId);
    Snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, ProcessId);
    if (Snapshot == INVALID_HANDLE_VALUE)
    {
        trace("Explorer module snapshot for process %lu failed with %lu\n", ProcessId, GetLastError());
        return;
    }

    ZeroMemory(&Module, sizeof(Module));
    Module.dwSize = sizeof(Module);
    More = Module32FirstW(Snapshot, &Module);
    while (More)
    {
        if (!lstrcmpiW(Module.szModule, L"comctl32.dll") || !lstrcmpiW(Module.szModule, L"uxtheme.dll"))
            trace("Explorer module %ls=%ls base=%p size=%lu\n", Module.szModule, Module.szExePath, Module.modBaseAddr, Module.modBaseSize);
        More = Module32NextW(Snapshot, &Module);
    }
    CloseHandle(Snapshot);
}

static void
trace_shell_window(HWND Window)
{
    WCHAR ClassName[64];
    RECT Rect;

    if (!Window || !GetClassNameW(Window, ClassName, ARRAY_SIZE(ClassName)) || !GetWindowRect(Window, &Rect))
        return;

    trace("Explorer window class=%ls hwnd=%p rect=%ld,%ld-%ld,%ld style=%p exstyle=%p\n", ClassName, Window, Rect.left, Rect.top, Rect.right, Rect.bottom, (PVOID)GetWindowLongPtrW(Window, GWL_STYLE), (PVOID)GetWindowLongPtrW(Window, GWL_EXSTYLE));
}

static BOOL
rect_contains(const RECT *Outer, const RECT *Inner)
{
    return Inner->left >= Outer->left && Inner->top >= Outer->top && Inner->right <= Outer->right && Inner->bottom <= Outer->bottom && Inner->right > Inner->left && Inner->bottom > Inner->top;
}

static DWORD
capture_window_hash(HWND Window, UINT *PixelCount)
{
    BITMAPINFO BitmapInfo;
    RECT Rect;
    HBITMAP Bitmap;
    HBITMAP OldBitmap;
    HDC ScreenDc;
    HDC MemoryDc;
    DWORD *Pixels;
    DWORD Hash = 2166136261u;
    UINT Count;
    UINT Index;
    INT Width;
    INT Height;

    *PixelCount = 0;
    if (!GetWindowRect(Window, &Rect))
        return 0;

    Width = Rect.right - Rect.left;
    Height = Rect.bottom - Rect.top;
    if (Width <= 0 || Height <= 0)
        return 0;

    ScreenDc = GetDC(NULL);
    MemoryDc = CreateCompatibleDC(ScreenDc);
    ZeroMemory(&BitmapInfo, sizeof(BitmapInfo));
    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth = Width;
    BitmapInfo.bmiHeader.biHeight = -Height;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;
    Bitmap = CreateDIBSection(ScreenDc, &BitmapInfo, DIB_RGB_COLORS, (PVOID *)&Pixels, NULL, 0);
    if (!ScreenDc || !MemoryDc || !Bitmap)
    {
        if (Bitmap)
            DeleteObject(Bitmap);
        if (MemoryDc)
            DeleteDC(MemoryDc);
        if (ScreenDc)
            ReleaseDC(NULL, ScreenDc);
        return 0;
    }

    OldBitmap = SelectObject(MemoryDc, Bitmap);
    if (!BitBlt(MemoryDc, 0, 0, Width, Height, ScreenDc, Rect.left, Rect.top, SRCCOPY))
        Count = 0;
    else
        Count = Width * Height;

    for (Index = 0; Index < Count; ++Index)
    {
        Hash ^= Pixels[Index] & 0x00ffffff;
        Hash *= 16777619u;
    }

    SelectObject(MemoryDc, OldBitmap);
    DeleteObject(Bitmap);
    DeleteDC(MemoryDc);
    ReleaseDC(NULL, ScreenDc);
    *PixelCount = Count;
    return Count ? Hash : 0;
}

static UINT
compare_taskbar_background(HWND Window, const RECT *TrayRect, const RECT *TaskSwitchRect, COLORREF *ExpectedColor, COLORREF *ActualColor)
{
    static const UINT Fractions[] = { 5, 7, 9 };
    BITMAPINFO BitmapInfo;
    RECT ReferenceRect;
    HBITMAP Bitmap;
    HBITMAP OldBitmap;
    HTHEME Theme;
    HDC ScreenDc;
    HDC MemoryDc;
    PVOID Bits;
    UINT Matches = 0;
    UINT Index;
    INT Width = TrayRect->right - TrayRect->left;
    INT Height = TrayRect->bottom - TrayRect->top;

    *ExpectedColor = CLR_INVALID;
    *ActualColor = CLR_INVALID;
    if (Width <= 0 || Height <= 0)
        return 0;

    Theme = OpenThemeData(Window, L"TaskBar");
    ScreenDc = GetDC(NULL);
    MemoryDc = CreateCompatibleDC(ScreenDc);
    ZeroMemory(&BitmapInfo, sizeof(BitmapInfo));
    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth = Width;
    BitmapInfo.bmiHeader.biHeight = -Height;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;
    Bitmap = CreateDIBSection(ScreenDc, &BitmapInfo, DIB_RGB_COLORS, &Bits, NULL, 0);
    if (!Theme || !ScreenDc || !MemoryDc || !Bitmap)
    {
        if (Bitmap)
            DeleteObject(Bitmap);
        if (MemoryDc)
            DeleteDC(MemoryDc);
        if (ScreenDc)
            ReleaseDC(NULL, ScreenDc);
        if (Theme)
            CloseThemeData(Theme);
        return 0;
    }

    OldBitmap = SelectObject(MemoryDc, Bitmap);
    SetRect(&ReferenceRect, 0, 0, Width, Height);
    if (SUCCEEDED(DrawThemeBackground(Theme, MemoryDc, TBP_BACKGROUNDBOTTOM, 0, &ReferenceRect, NULL)))
    {
        INT SampleY = (TaskSwitchRect->top + TaskSwitchRect->bottom) / 2;

        GdiFlush();
        for (Index = 0; Index < ARRAY_SIZE(Fractions); ++Index)
        {
            INT SampleX = TaskSwitchRect->left + ((TaskSwitchRect->right - TaskSwitchRect->left) * Fractions[Index]) / 10;
            COLORREF Expected = GetPixel(MemoryDc, SampleX - TrayRect->left, SampleY - TrayRect->top);
            COLORREF Actual = GetPixel(ScreenDc, SampleX, SampleY);

            if (Index == 0)
            {
                *ExpectedColor = Expected;
                *ActualColor = Actual;
            }
            if (Expected != CLR_INVALID && Expected == Actual)
                ++Matches;
        }
    }

    SelectObject(MemoryDc, OldBitmap);
    DeleteObject(Bitmap);
    DeleteDC(MemoryDc);
    ReleaseDC(NULL, ScreenDc);
    CloseThemeData(Theme);
    return Matches;
}

static void
test_transparent_taskbar_rebar(HWND Parent)
{
    HTHEME Theme;
    HWND Window;
    HRESULT Result;
    INT BackgroundType = -1;

    Window = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD, 0, 0, 1, 1, Parent, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "The taskbar rebar theme probe window could not be created\n");
    if (!Window)
        return;

    Result = SetWindowTheme(Window, L"TaskBar", NULL);
    ok(Result == S_OK, "SetWindowTheme(TaskBar) returned 0x%08lx\n", Result);
    Theme = OpenThemeData(Window, L"Rebar");
    ok(Theme != NULL, "OpenThemeData(TaskBar::Rebar) failed with 0x%08lx\n", GetLastError());
    if (Theme)
    {
        Result = GetThemeEnumValue(Theme, 0, 0, TMT_BGTYPE, &BackgroundType);
        ok(Result == S_OK, "GetThemeEnumValue(TaskBar::Rebar, BGTYPE) returned 0x%08lx\n", Result);
        ok(BackgroundType == BT_NONE, "TaskBar::Rebar background type is %d instead of BT_NONE\n", BackgroundType);
        ok(IsThemeBackgroundPartiallyTransparent(Theme, 0, 0), "A BT_NONE taskbar rebar did not request its parent background\n");
        CloseThemeData(Theme);
    }
    DestroyWindow(Window);
}

static void
test_start_theme_margins(HWND Parent)
{
    MARGINS Margins;
    HTHEME Theme;
    HWND Window;
    HRESULT Result;

    Window = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD, 0, 0, 1, 1, Parent, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "The Start theme probe window could not be created\n");
    if (!Window)
        return;

    Result = SetWindowTheme(Window, L"Start", NULL);
    ok(Result == S_OK, "SetWindowTheme(Start) returned 0x%08lx\n", Result);
    Theme = OpenThemeData(Window, L"Button");
    ok(Theme != NULL, "OpenThemeData(Start::Button) failed with %lu\n", GetLastError());
    if (Theme)
    {
        ZeroMemory(&Margins, sizeof(Margins));
        Result = GetThemeMargins(Theme, NULL, BP_PUSHBUTTON, PBS_NORMAL, TMT_CONTENTMARGINS, NULL, &Margins);
        ok(Result == S_OK, "GetThemeMargins(Start::Button) returned 0x%08lx\n", Result);
        ok(Margins.cxLeftWidth == -30 && Margins.cxRightWidth == 52 && Margins.cyTopHeight == 1 && Margins.cyBottomHeight == 1, "Start::Button margins are %d,%d,%d,%d instead of -30,52,1,1\n", Margins.cxLeftWidth, Margins.cxRightWidth, Margins.cyTopHeight, Margins.cyBottomHeight);
        CloseThemeData(Theme);
    }
    DestroyWindow(Window);
}

static COLORREF
get_theme_text_color(HTHEME Theme)
{
    COLORREF Color = CLR_INVALID;

    if (!Theme || FAILED(GetThemeColor(Theme, 0, 0, TMT_TEXTCOLOR, &Color)))
        return CLR_INVALID;

    return Color;
}

static COLORREF
get_taskband_reference_text_color(HWND Parent)
{
    HTHEME Theme;
    HWND Window;
    HRESULT Result;
    COLORREF Color = CLR_INVALID;

    Window = CreateWindowExW(0, L"STATIC", NULL, WS_CHILD, 0, 0, 1, 1, Parent, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "The task-band theme probe window could not be created\n");
    if (!Window)
        return CLR_INVALID;

    Result = SetWindowTheme(Window, L"TaskBand", NULL);
    ok(Result == S_OK, "SetWindowTheme(TaskBand) returned 0x%08lx\n", Result);
    Theme = OpenThemeData(Window, L"Toolbar");
    ok(Theme != NULL, "OpenThemeData(TaskBand::Toolbar) failed with %lu\n", GetLastError());
    if (Theme)
    {
        Color = get_theme_text_color(Theme);
        ok(Color != CLR_INVALID, "TaskBand::Toolbar did not expose a text color\n");
        CloseThemeData(Theme);
    }

    DestroyWindow(Window);
    return Color;
}

static UINT
count_task_toolbar_text_pixels(HWND Toolbar, COLORREF Color)
{
    RECT Rect;
    DWORD ButtonSize;
    HDC ScreenDc;
    UINT Count = 0;
    INT ButtonWidth;
    INT Left;
    INT Right;
    INT X;
    INT Y;

    if (!Toolbar || Color == CLR_INVALID || !GetWindowRect(Toolbar, &Rect))
        return 0;

    ButtonSize = SendMessageW(Toolbar, TB_GETBUTTONSIZE, 0, 0);
    ButtonWidth = LOWORD(ButtonSize);
    if (ButtonWidth <= 0)
        return 0;

    Left = Rect.left;
    Right = min(Rect.left + ButtonWidth, Rect.right);
    Left += min(32, max(0, Right - Left));
    ScreenDc = GetDC(NULL);
    if (!ScreenDc)
        return 0;

    for (Y = Rect.top + 2; Y < Rect.bottom - 2; ++Y)
    {
        for (X = Left; X < Right - 3; ++X)
        {
            if (GetPixel(ScreenDc, X, Y) == Color)
                ++Count;
        }
    }

    ReleaseDC(NULL, ScreenDc);
    return Count;
}

static LRESULT CALLBACK
probe_window_proc(HWND Window, UINT Message, WPARAM wParam, LPARAM lParam)
{
    if (Message == WM_THEMECHANGED)
        InterlockedIncrement(&g_ThemeChangedCount);
    else if (Message == WM_APPLY_SHELL_THEME)
    {
        g_ApplyResult = g_SetSystemVisualStyle(g_ThemePath, g_ColorName, g_SizeName, 0);
        g_ThemeChangedAtReturn = g_ThemeChangedCount;
        g_TrayStyleAtReturn = GetWindowLongPtrW(g_TrayWindow, GWL_STYLE);
        return 0;
    }

    return DefWindowProcW(Window, Message, wParam, lParam);
}

START_TEST(ThemeShellTransition)
{
    const WCHAR ProbeClass[] = L"ReactOSThemeShellTransitionProbe";
    WCHAR ThemePath[MAX_PATH];
    WCHAR ColorName[64] = L"ReactOS";
    WCHAR SizeName[64] = L"Normal Size";
    WCHAR CaptureValue[16];
    WNDCLASSW Class;
    RECT TrayRect;
    RECT TrayClientRect;
    RECT ClassicStartRect;
    RECT StartRect;
    RECT TaskSwitchRect;
    RECT TrayNotifyRect;
    LONG FailuresBefore = winetest_get_failures();
    LONG_PTR ClassicStyle;
    LONG_PTR ThemedStyle;
    DWORD ClassicHash;
    DWORD ThemedHash;
    DWORD ProbeHashBeforeRedraw;
    DWORD ProbeHashAfterRedraw;
    DWORD CaptureMilliseconds = 0;
    COLORREF ExpectedTaskbarColor;
    COLORREF ActualTaskbarColor;
    COLORREF ExpectedTaskTextColor;
    UINT ClassicPixels;
    UINT ThemedPixels;
    UINT ProbePixelsBeforeRedraw;
    UINT ProbePixelsAfterRedraw;
    UINT TaskbarBackgroundMatches;
    UINT TaskToolbarTextPixels;
    HRESULT Result;
    HMODULE UxTheme;
    HWND ProbeWindow;
    HWND StartButton;
    HWND Rebar;
    HWND TaskSwitch;
    HWND TaskToolbar;
    HWND TrayNotify;
    HTHEME TaskToolbarTheme;
    HTHEME StartButtonTheme;

    if (!GetEnvironmentVariableW(L"UXTHEME_SHELL_THEME", ThemePath, ARRAY_SIZE(ThemePath)))
    {
        skip("UXTHEME_SHELL_THEME is not set; shell transition is an explicit end-to-end test\n");
        return;
    }
    GetEnvironmentVariableW(L"UXTHEME_SHELL_COLOR", ColorName, ARRAY_SIZE(ColorName));
    GetEnvironmentVariableW(L"UXTHEME_SHELL_SIZE", SizeName, ARRAY_SIZE(SizeName));

    g_TrayWindow = FindWindowW(L"Shell_TrayWnd", NULL);
    ok(g_TrayWindow != NULL, "Explorer Shell_TrayWnd was not found\n");
    if (!g_TrayWindow)
        return;
    trace_shell_modules(g_TrayWindow);

    UxTheme = LoadLibraryW(L"uxtheme.dll");
    g_SetSystemVisualStyle = (PSET_SYSTEM_VISUAL_STYLE)GetProcAddress(UxTheme, MAKEINTRESOURCEA(65));
    ok(g_SetSystemVisualStyle != NULL, "SetSystemVisualStyle ordinal 65 is missing\n");
    if (!g_SetSystemVisualStyle)
        return;

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = probe_window_proc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    Class.hbrBackground = GetSysColorBrush(COLOR_WINDOW);
    Class.lpszClassName = ProbeClass;
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed with %lu\n", GetLastError());
    ProbeWindow = CreateWindowExW(0, ProbeClass, L"Explorer theme transition", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 80, 80, 420, 180, NULL, NULL, Class.hInstance, NULL);
    ok(ProbeWindow != NULL, "CreateWindowExW failed with %lu\n", GetLastError());
    if (!ProbeWindow)
        return;
    pump_messages(250);

    Result = g_SetSystemVisualStyle(NULL, NULL, NULL, 0);
    ok(Result == S_OK, "Disabling the visual style returned 0x%08lx\n", Result);
    pump_messages(750);
    ok(!IsThemeActive(), "The visual style remained active after disabling it\n");
    ClassicStyle = GetWindowLongPtrW(g_TrayWindow, GWL_STYLE);
    ClassicHash = capture_window_hash(g_TrayWindow, &ClassicPixels);
    ok(ClassicPixels != 0, "The classic taskbar framebuffer could not be captured\n");
    StartButton = find_shell_child(g_TrayWindow, L"Button");
    ZeroMemory(&ClassicStartRect, sizeof(ClassicStartRect));
    if (StartButton)
        GetWindowRect(StartButton, &ClassicStartRect);

    g_ThemePath = ThemePath;
    g_ColorName = ColorName;
    g_SizeName = SizeName;
    g_ThemeChangedCount = 0;
    g_ThemeChangedAtReturn = 0;
    g_TrayStyleAtReturn = 0;
    SendMessageW(ProbeWindow, WM_APPLY_SHELL_THEME, 0, 0);

    ok(g_ApplyResult == S_OK, "Applying %ls returned 0x%08lx\n", ThemePath, g_ApplyResult);
    ok(g_ThemeChangedAtReturn > 0, "ApplyTheme returned before synchronously delivering WM_THEMECHANGED\n");
    ok((g_TrayStyleAtReturn & (WS_THICKFRAME | WS_BORDER)) == 0, "Explorer was still using classic tray styles when ApplyTheme returned (style %p)\n", (PVOID)g_TrayStyleAtReturn);

    pump_messages(1250);
    ThemedStyle = GetWindowLongPtrW(g_TrayWindow, GWL_STYLE);
    ThemedHash = capture_window_hash(g_TrayWindow, &ThemedPixels);
    Rebar = find_shell_descendant(g_TrayWindow, L"ReBarWindow32");
    TaskSwitch = find_shell_descendant(g_TrayWindow, L"MSTaskSwWClass");
    TrayNotify = find_shell_descendant(g_TrayWindow, L"TrayNotifyWnd");
    StartButton = find_shell_child(g_TrayWindow, L"Button");
    TaskToolbar = TaskSwitch ? find_shell_descendant(TaskSwitch, TOOLBARCLASSNAMEW) : NULL;

    trace_shell_window(g_TrayWindow);
    trace_shell_window(StartButton);
    trace_shell_window(Rebar);
    trace_shell_window(TaskSwitch);
    trace_shell_window(TaskToolbar);
    trace_shell_window(TrayNotify);

    test_transparent_taskbar_rebar(ProbeWindow);
    test_start_theme_margins(ProbeWindow);
    ProbeHashBeforeRedraw = capture_window_hash(ProbeWindow, &ProbePixelsBeforeRedraw);
    RedrawWindow(ProbeWindow, NULL, NULL, RDW_ERASE | RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
    ProbeHashAfterRedraw = capture_window_hash(ProbeWindow, &ProbePixelsAfterRedraw);
    ExpectedTaskbarColor = CLR_INVALID;
    ActualTaskbarColor = CLR_INVALID;
    if (GetWindowRect(g_TrayWindow, &TrayRect) && TaskSwitch && GetWindowRect(TaskSwitch, &TaskSwitchRect))
        TaskbarBackgroundMatches = compare_taskbar_background(ProbeWindow, &TrayRect, &TaskSwitchRect, &ExpectedTaskbarColor, &ActualTaskbarColor);
    else
        TaskbarBackgroundMatches = 0;
    ExpectedTaskTextColor = get_taskband_reference_text_color(ProbeWindow);
    TaskToolbarTheme = TaskToolbar ? GetWindowTheme(TaskToolbar) : NULL;
    StartButtonTheme = StartButton ? GetWindowTheme(StartButton) : NULL;
    TaskToolbarTextPixels = count_task_toolbar_text_pixels(TaskToolbar, ExpectedTaskTextColor);
    ZeroMemory(&StartRect, sizeof(StartRect));

    ok(IsThemeActive(), "The visual style is not active after the transition\n");
    ok(g_ThemeChangedCount > 0, "The probe window did not receive WM_THEMECHANGED\n");
    ok((ThemedStyle & (WS_THICKFRAME | WS_BORDER)) == 0, "Explorer retained classic tray styles after settling (style %p)\n", (PVOID)ThemedStyle);
    ok(ClassicStyle != ThemedStyle, "Explorer tray styles did not change across the transition (%p)\n", (PVOID)ThemedStyle);
    ok(ThemedPixels != 0, "The themed taskbar framebuffer could not be captured\n");
    ok(ClassicHash != ThemedHash, "The taskbar framebuffer did not change across the theme transition (0x%08lx)\n", ThemedHash);
    ok(ProbePixelsBeforeRedraw != 0 && ProbePixelsAfterRedraw != 0, "The probe window framebuffer could not be captured around its forced redraw\n");
    ok(ProbeHashBeforeRedraw == ProbeHashAfterRedraw, "The probe nonclient frame was stale until a forced redraw (0x%08lx != 0x%08lx)\n", ProbeHashBeforeRedraw, ProbeHashAfterRedraw);
    ok(Rebar != NULL, "Explorer rebar window was not found\n");
    ok(TaskSwitch != NULL, "Explorer task switch window was not found\n");
    ok(TaskToolbar != NULL, "Explorer task toolbar window was not found\n");
    ok(TrayNotify != NULL, "Explorer notification area window was not found\n");
    ok(StartButton != NULL, "Explorer Start button was not found\n");
    ok(TaskbarBackgroundMatches >= 2, "Only %u/3 blank task-switch pixels matched the rendered taskbar background (expected 0x%08lx, actual 0x%08lx)\n", TaskbarBackgroundMatches, ExpectedTaskbarColor, ActualTaskbarColor);
    ok(TaskToolbarTheme != NULL, "Explorer task toolbar is not bound to its TaskBand window theme\n");
    ok(ExpectedTaskTextColor != CLR_INVALID, "The TaskBand::Toolbar reference text color is unavailable\n");
    ok(TaskToolbarTextPixels != 0, "Explorer task toolbar rendered no TaskBand text-color pixels\n");
    ok(StartButtonTheme != NULL, "Explorer Start button is not bound to its Start window theme\n");

    if (GetClientRect(g_TrayWindow, &TrayClientRect))
    {
        MapWindowPoints(g_TrayWindow, NULL, (POINT *)&TrayClientRect, 2);
        if (StartButton && GetWindowRect(StartButton, &StartRect))
        {
            ok(StartRect.left == TrayClientRect.left, "Start button left edge %ld does not meet the tray client edge %ld\n", StartRect.left, TrayClientRect.left);
            ok(StartRect.top == TrayClientRect.top, "Start button top edge %ld does not meet the tray client edge %ld\n", StartRect.top, TrayClientRect.top);
            ok(ClassicStartRect.right - ClassicStartRect.left == 58, "Classic Start button width is %ld instead of 58 pixels\n", ClassicStartRect.right - ClassicStartRect.left);
            ok(StartRect.right - StartRect.left == 51, "Themed Start button width is %ld instead of 51 pixels\n", StartRect.right - StartRect.left);
            if (Rebar && GetWindowRect(Rebar, &TrayRect))
                ok(TrayRect.left - StartRect.right == GetSystemMetrics(SM_CXSIZEFRAME), "Start-to-rebar gap is %ld instead of %d pixels\n", TrayRect.left - StartRect.right, GetSystemMetrics(SM_CXSIZEFRAME));
        }
    }

    if (GetWindowRect(g_TrayWindow, &TrayRect) && TaskSwitch && GetWindowRect(TaskSwitch, &TaskSwitchRect))
        ok(rect_contains(&TrayRect, &TaskSwitchRect), "Task switch rect %ld,%ld-%ld,%ld is outside tray rect %ld,%ld-%ld,%ld\n", TaskSwitchRect.left, TaskSwitchRect.top, TaskSwitchRect.right, TaskSwitchRect.bottom, TrayRect.left, TrayRect.top, TrayRect.right, TrayRect.bottom);
    if (GetWindowRect(g_TrayWindow, &TrayRect) && TrayNotify && GetWindowRect(TrayNotify, &TrayNotifyRect))
        ok(rect_contains(&TrayRect, &TrayNotifyRect), "Notification rect %ld,%ld-%ld,%ld is outside tray rect %ld,%ld-%ld,%ld\n", TrayNotifyRect.left, TrayNotifyRect.top, TrayNotifyRect.right, TrayNotifyRect.bottom, TrayRect.left, TrayRect.top, TrayRect.right, TrayRect.bottom);

    SetForegroundWindow(ProbeWindow);
    trace("THEME_SHELL_READY sync_messages=%ld total_messages=%ld classic_style=%p immediate_style=%p themed_style=%p classic_hash=0x%08lx themed_hash=0x%08lx probe_hash=0x%08lx/0x%08lx taskbar_matches=%u/3 taskbar_color=0x%08lx/0x%08lx task_text=0x%08lx/%u start_width=%ld/%ld pixels=%u/%u theme=%ls failures=%ld\n", g_ThemeChangedAtReturn, g_ThemeChangedCount, (PVOID)ClassicStyle, (PVOID)g_TrayStyleAtReturn, (PVOID)ThemedStyle, ClassicHash, ThemedHash, ProbeHashBeforeRedraw, ProbeHashAfterRedraw, TaskbarBackgroundMatches, ExpectedTaskbarColor, ActualTaskbarColor, ExpectedTaskTextColor, TaskToolbarTextPixels, ClassicStartRect.right - ClassicStartRect.left, StartRect.right - StartRect.left, ClassicPixels, ThemedPixels, ThemePath, winetest_get_failures() - FailuresBefore);

    if (GetEnvironmentVariableW(L"UXTHEME_SHELL_CAPTURE_MS", CaptureValue, ARRAY_SIZE(CaptureValue)))
        CaptureMilliseconds = wcstoul(CaptureValue, NULL, 10);
    if (CaptureMilliseconds)
        pump_messages(CaptureMilliseconds);

    DestroyWindow(ProbeWindow);
    UnregisterClassW(ProbeClass, Class.hInstance);
    FreeLibrary(UxTheme);
    trace("THEME_SHELL_DONE failures=%ld\n", winetest_get_failures() - FailuresBefore);
}
