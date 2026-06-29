/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Visual cursor move regression test
 */

#include "precomp.h"

#define TEST_CLASS_NAME "CursorMoveVisualTestWindow"
#define WINDOW_WIDTH 320
#define WINDOW_HEIGHT 240
#define CAPTURE_SIZE 64
#define CURSOR_DIFF_THRESHOLD 24
#define RESTORE_DIFF_TOLERANCE 2

struct capture_point
{
    POINT ClientPoint;
    RECT ScreenRect;
    DWORD *Baseline;
    UINT ImmediateDiff;
    UINT DelayedDiff;
};

static HCURSOR g_hArrow;

static VOID
PumpMessages(VOID)
{
    MSG Msg;

    while (PeekMessageA(&Msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Msg);
        DispatchMessageA(&Msg);
    }
}

static COLORREF
PatternColor(INT x, INT y)
{
    BYTE r = (BYTE)((x * 17 + y * 3) & 0xff);
    BYTE g = (BYTE)((x * 5 + y * 13) & 0xff);
    BYTE b = (BYTE)(((x ^ y) * 11) & 0xff);

    return RGB(r, g, b);
}

static VOID
PaintPattern(HDC hdc)
{
    INT x, y;
    RECT Rect;

    for (y = 0; y < WINDOW_HEIGHT; y += 8)
    {
        for (x = 0; x < WINDOW_WIDTH; x += 8)
        {
            HBRUSH hBrush;

            SetRect(&Rect, x, y, min(x + 8, WINDOW_WIDTH), min(y + 8, WINDOW_HEIGHT));
            hBrush = CreateSolidBrush(PatternColor(x, y));
            if (hBrush)
            {
                FillRect(hdc, &Rect, hBrush);
                DeleteObject(hBrush);
            }
        }
    }
}

static LRESULT CALLBACK
CursorVisualWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    PAINTSTRUCT Ps;

    switch (Msg)
    {
        case WM_ERASEBKGND:
            return 1;

        case WM_SETCURSOR:
            SetCursor(g_hArrow);
            return TRUE;

        case WM_PAINT:
        {
            HDC hdc = BeginPaint(hWnd, &Ps);
            if (hdc)
            {
                PaintPattern(hdc);
                EndPaint(hWnd, &Ps);
            }
            return 0;
        }
    }

    return DefWindowProcA(hWnd, Msg, wParam, lParam);
}

static BOOL
CaptureScreenRect(const RECT *Rect, DWORD *Pixels)
{
    BOOL Ret = FALSE;
    HDC hScreen, hMem;
    HBITMAP hBitmap, hOldBitmap;
    BITMAPINFO Bmi;
    VOID *Bits;
    INT Width = Rect->right - Rect->left;
    INT Height = Rect->bottom - Rect->top;

    hScreen = GetDC(NULL);
    if (!hScreen)
        return FALSE;

    hMem = CreateCompatibleDC(hScreen);
    if (!hMem)
    {
        ReleaseDC(NULL, hScreen);
        return FALSE;
    }

    ZeroMemory(&Bmi, sizeof(Bmi));
    Bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Bmi.bmiHeader.biWidth = Width;
    Bmi.bmiHeader.biHeight = -Height;
    Bmi.bmiHeader.biPlanes = 1;
    Bmi.bmiHeader.biBitCount = 32;
    Bmi.bmiHeader.biCompression = BI_RGB;

    hBitmap = CreateDIBSection(hScreen, &Bmi, DIB_RGB_COLORS, &Bits, NULL, 0);
    if (hBitmap)
    {
        hOldBitmap = SelectObject(hMem, hBitmap);
        if (hOldBitmap)
        {
            if (BitBlt(hMem, 0, 0, Width, Height, hScreen,
                       Rect->left, Rect->top, SRCCOPY | CAPTUREBLT))
            {
                CopyMemory(Pixels, Bits, Width * Height * sizeof(DWORD));
                Ret = TRUE;
            }
            SelectObject(hMem, hOldBitmap);
        }
        DeleteObject(hBitmap);
    }

    DeleteDC(hMem);
    ReleaseDC(NULL, hScreen);
    return Ret;
}

static UINT
CountDifferentPixels(const DWORD *Left, const DWORD *Right, UINT PixelCount)
{
    UINT Count = 0;
    UINT i;

    for (i = 0; i < PixelCount; ++i)
    {
        if ((Left[i] & 0x00ffffff) != (Right[i] & 0x00ffffff))
            ++Count;
    }

    return Count;
}

static VOID
SetCaptureRect(RECT *Rect, const POINT *ScreenPoint)
{
    Rect->left = ScreenPoint->x - 8;
    Rect->top = ScreenPoint->y - 8;
    Rect->right = Rect->left + CAPTURE_SIZE;
    Rect->bottom = Rect->top + CAPTURE_SIZE;
}

static DWORD *
AllocCaptureBuffer(VOID)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                     CAPTURE_SIZE * CAPTURE_SIZE * sizeof(DWORD));
}

START_TEST(CursorMoveVisual)
{
    static struct capture_point Points[] =
    {
        {{ 50,  50}, {0}, NULL, 0, 0},
        {{250,  50}, {0}, NULL, 0, 0},
        {{250, 170}, {0}, NULL, 0, 0},
        {{ 50, 170}, {0}, NULL, 0, 0},
        {{150, 110}, {0}, NULL, 0, 0}
    };
    WNDCLASSA Class;
    HWND hWnd = NULL;
    POINT ClientOrigin;
    POINT ScreenPoint;
    INT ScreenWidth, ScreenHeight;
    INT WindowX, WindowY;
    DWORD *Scratch = NULL;
    BOOL Registered = FALSE;
    BOOL SawCapturedCursor = FALSE;
    UINT i;
    DWORD Error;
    BOOL CaptureOk;
    BOOL ClientToScreenOk;

    ScreenWidth = GetSystemMetrics(SM_CXSCREEN);
    ScreenHeight = GetSystemMetrics(SM_CYSCREEN);
    ok(ScreenWidth >= 420 && ScreenHeight >= 320,
       "screen is too small for cursor visual test: %dx%d\n", ScreenWidth, ScreenHeight);
    if (ScreenWidth < 420 || ScreenHeight < 320)
        return;

    g_hArrow = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    ok(g_hArrow != NULL, "LoadCursor(IDC_ARROW) failed, error %lu\n", GetLastError());
    if (!g_hArrow)
        return;

    ZeroMemory(&Class, sizeof(Class));
    Class.lpfnWndProc = CursorVisualWndProc;
    Class.hInstance = GetModuleHandleA(NULL);
    Class.hCursor = g_hArrow;
    Class.hbrBackground = NULL;
    Class.lpszClassName = TEST_CLASS_NAME;

    Registered = RegisterClassA(&Class) != 0;
    Error = GetLastError();
    ok(Registered || Error == ERROR_CLASS_ALREADY_EXISTS,
       "RegisterClassA failed, error %lu\n", Error);
    if (!Registered && Error != ERROR_CLASS_ALREADY_EXISTS)
        return;

    WindowX = 40;
    WindowY = 40;
    hWnd = CreateWindowExA(WS_EX_TOPMOST,
                           TEST_CLASS_NAME,
                           TEST_CLASS_NAME,
                           WS_POPUP,
                           WindowX,
                           WindowY,
                           WINDOW_WIDTH,
                           WINDOW_HEIGHT,
                           NULL,
                           NULL,
                           GetModuleHandleA(NULL),
                           NULL);
    ok(hWnd != NULL, "CreateWindowExA failed, error %lu\n", GetLastError());
    if (!hWnd)
        goto Cleanup;

    ShowWindow(hWnd, SW_SHOWNORMAL);
    ok(IsWindowVisible(hWnd), "test window is not visible\n");
    ok(SetWindowPos(hWnd, HWND_TOPMOST, WindowX, WindowY, WINDOW_WIDTH, WINDOW_HEIGHT,
                    SWP_SHOWWINDOW), "SetWindowPos failed, error %lu\n", GetLastError());
    UpdateWindow(hWnd);
    SetForegroundWindow(hWnd);
    SetActiveWindow(hWnd);
    SetCursor(g_hArrow);
    PumpMessages();
    GdiFlush();
    Sleep(80);

    ClientOrigin.x = 0;
    ClientOrigin.y = 0;
    ClientToScreenOk = ClientToScreen(hWnd, &ClientOrigin);
    ok(ClientToScreenOk, "ClientToScreen failed, error %lu\n", GetLastError());
    if (!ClientToScreenOk)
        goto Cleanup;

    Scratch = AllocCaptureBuffer();
    ok(Scratch != NULL, "failed to allocate scratch capture buffer\n");
    if (!Scratch)
        goto Cleanup;

    ok(SetCursorPos(ScreenWidth - 2, ScreenHeight - 2),
       "SetCursorPos outside test window failed, error %lu\n", GetLastError());
    PumpMessages();
    GdiFlush();
    Sleep(80);

    for (i = 0; i < sizeof(Points) / sizeof(Points[0]); ++i)
    {
        ScreenPoint.x = ClientOrigin.x + Points[i].ClientPoint.x;
        ScreenPoint.y = ClientOrigin.y + Points[i].ClientPoint.y;
        SetCaptureRect(&Points[i].ScreenRect, &ScreenPoint);

        Points[i].Baseline = AllocCaptureBuffer();
        ok(Points[i].Baseline != NULL, "failed to allocate baseline buffer %u\n", i);
        if (!Points[i].Baseline)
            goto Cleanup;

        CaptureOk = CaptureScreenRect(&Points[i].ScreenRect, Points[i].Baseline);
        ok(CaptureOk, "failed to capture baseline rect %u, error %lu\n", i, GetLastError());
        if (!CaptureOk)
            goto Cleanup;
    }

    for (i = 0; i < sizeof(Points) / sizeof(Points[0]); ++i)
    {
        ScreenPoint.x = ClientOrigin.x + Points[i].ClientPoint.x;
        ScreenPoint.y = ClientOrigin.y + Points[i].ClientPoint.y;

        ok(SetCursorPos(ScreenPoint.x, ScreenPoint.y),
           "SetCursorPos(%ld,%ld) failed, error %lu\n",
           ScreenPoint.x, ScreenPoint.y, GetLastError());
        PumpMessages();
        GdiFlush();

        if (i > 0)
        {
            CaptureOk = CaptureScreenRect(&Points[i - 1].ScreenRect, Scratch);
            ok(CaptureOk, "failed to capture immediate old cursor rect %u\n", i - 1);
            if (CaptureOk)
            {
                ok(CountDifferentPixels(Scratch, Points[i - 1].Baseline,
                                        CAPTURE_SIZE * CAPTURE_SIZE) <= RESTORE_DIFF_TOLERANCE,
                   "old cursor rect %u kept stale pixels immediately after move\n", i - 1);
            }
        }

        CaptureOk = CaptureScreenRect(&Points[i].ScreenRect, Scratch);
        ok(CaptureOk, "failed to capture immediate new cursor rect %u\n", i);
        if (CaptureOk)
        {
            Points[i].ImmediateDiff = CountDifferentPixels(Scratch, Points[i].Baseline,
                                                           CAPTURE_SIZE * CAPTURE_SIZE);
        }

        Sleep(60);
        PumpMessages();
        GdiFlush();

        if (i > 0)
        {
            CaptureOk = CaptureScreenRect(&Points[i - 1].ScreenRect, Scratch);
            ok(CaptureOk, "failed to capture delayed old cursor rect %u\n", i - 1);
            if (CaptureOk)
            {
                ok(CountDifferentPixels(Scratch, Points[i - 1].Baseline,
                                        CAPTURE_SIZE * CAPTURE_SIZE) <= RESTORE_DIFF_TOLERANCE,
                   "old cursor rect %u kept stale pixels after settling\n", i - 1);
            }
        }

        CaptureOk = CaptureScreenRect(&Points[i].ScreenRect, Scratch);
        ok(CaptureOk, "failed to capture delayed new cursor rect %u\n", i);
        if (CaptureOk)
        {
            Points[i].DelayedDiff = CountDifferentPixels(Scratch, Points[i].Baseline,
                                                         CAPTURE_SIZE * CAPTURE_SIZE);
        }
        trace("cursor rect %u immediate diff %u delayed diff %u\n",
              i, Points[i].ImmediateDiff, Points[i].DelayedDiff);
    }

    for (i = 0; i < sizeof(Points) / sizeof(Points[0]); ++i)
    {
        if (Points[i].DelayedDiff > CURSOR_DIFF_THRESHOLD)
            SawCapturedCursor = TRUE;
    }

    if (SawCapturedCursor)
    {
        for (i = 0; i < sizeof(Points) / sizeof(Points[0]); ++i)
        {
            ok(Points[i].DelayedDiff > CURSOR_DIFF_THRESHOLD,
               "cursor disappeared at settled position %u: diff %u\n",
               i, Points[i].DelayedDiff);
            ok(Points[i].ImmediateDiff > CURSOR_DIFF_THRESHOLD,
               "cursor was delayed at new position %u: immediate diff %u delayed diff %u\n",
               i, Points[i].ImmediateDiff, Points[i].DelayedDiff);
        }
    }
    else
    {
        trace("screen capture does not include cursor pixels on this runtime\n");
    }

Cleanup:
    if (Scratch)
        HeapFree(GetProcessHeap(), 0, Scratch);

    for (i = 0; i < sizeof(Points) / sizeof(Points[0]); ++i)
    {
        if (Points[i].Baseline)
            HeapFree(GetProcessHeap(), 0, Points[i].Baseline);
    }

    if (hWnd)
        DestroyWindow(hWnd);
    if (Registered)
        UnregisterClassA(TEST_CLASS_NAME, GetModuleHandleA(NULL));
}
