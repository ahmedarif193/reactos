/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Themed caption text must keep its ascenders and descenders
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <windows.h>
#include <uxtheme.h>

#define CAPTURE_WIDTH 200
#define TEXT_LEFT 32

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

static BOOL
capture_caption(HWND Window, DWORD *Pixels, int Height)
{
    BITMAPINFO Info;
    HDC WindowDc, MemDc;
    HBITMAP Bitmap, Old;
    DWORD *Bits = NULL;
    BOOL Result = FALSE;

    ZeroMemory(&Info, sizeof(Info));
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = CAPTURE_WIDTH;
    Info.bmiHeader.biHeight = -Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;

    WindowDc = GetWindowDC(Window);
    MemDc = CreateCompatibleDC(WindowDc);
    Bitmap = CreateDIBSection(WindowDc, &Info, DIB_RGB_COLORS, (void **)&Bits, NULL, 0);
    if (WindowDc && MemDc && Bitmap)
    {
        Old = SelectObject(MemDc, Bitmap);
        Result = BitBlt(MemDc, 0, 0, CAPTURE_WIDTH, Height, WindowDc, 0, 0, SRCCOPY);
        GdiFlush();
        CopyMemory(Pixels, Bits, CAPTURE_WIDTH * Height * sizeof(DWORD));
        SelectObject(MemDc, Old);
    }
    if (Bitmap)
        DeleteObject(Bitmap);
    if (MemDc)
        DeleteDC(MemDc);
    if (WindowDc)
        ReleaseDC(Window, WindowDc);
    return Result;
}

static void
text_rows(const DWORD *Background, const DWORD *Image, int Height, int *Top, int *Bottom)
{
    int x, y;

    *Top = -1;
    *Bottom = -1;
    for (y = 0; y < Height; y++)
    {
        for (x = TEXT_LEFT; x < CAPTURE_WIDTH; x++)
        {
            if ((Background[y * CAPTURE_WIDTH + x] ^ Image[y * CAPTURE_WIDTH + x]) & 0xffffff)
            {
                if (*Top < 0)
                    *Top = y;
                *Bottom = y;
                break;
            }
        }
    }
}

static BOOL
render_title(HWND Window, const WCHAR *Title, DWORD *Pixels, int Height)
{
    SetWindowTextW(Window, Title);
    RedrawWindow(Window, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW);
    flush_messages(150);
    return capture_caption(Window, Pixels, Height);
}

START_TEST(NonClientCaption)
{
    HWND Window;
    RECT WindowRect;
    POINT ClientOrigin = { 0, 0 };
    DWORD *Background, *Round, *Descender, *Ascender;
    int Height, RoundTop, RoundBottom, DescTop, DescBottom, AscTop, AscBottom;

    if (!IsThemeActive())
    {
        skip("Visual styles are not active\n");
        return;
    }

    Window = CreateWindowExW(WS_EX_TOPMOST, L"STATIC", L"", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                             60, 60, 360, 160, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(Window != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!Window)
        return;
    flush_messages(300);

    GetWindowRect(Window, &WindowRect);
    ClientToScreen(Window, &ClientOrigin);
    Height = ClientOrigin.y - WindowRect.top;
    ok(Height > 0 && Height < 100, "unexpected caption band height %d\n", Height);
    if (Height <= 0 || Height >= 100)
    {
        DestroyWindow(Window);
        return;
    }

    Background = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, 4 * CAPTURE_WIDTH * Height * sizeof(DWORD));
    Round = Background + CAPTURE_WIDTH * Height;
    Descender = Round + CAPTURE_WIDTH * Height;
    Ascender = Descender + CAPTURE_WIDTH * Height;

    ok(render_title(Window, L"", Background, Height), "cannot capture the empty caption\n");
    ok(render_title(Window, L"oooo", Round, Height), "cannot capture the round caption\n");
    ok(render_title(Window, L"gggg", Descender, Height), "cannot capture the descender caption\n");
    ok(render_title(Window, L"llll", Ascender, Height), "cannot capture the ascender caption\n");

    text_rows(Background, Round, Height, &RoundTop, &RoundBottom);
    text_rows(Background, Descender, Height, &DescTop, &DescBottom);
    text_rows(Background, Ascender, Height, &AscTop, &AscBottom);
    trace("band %d: oooo rows %d-%d, gggg rows %d-%d, llll rows %d-%d\n",
          Height, RoundTop, RoundBottom, DescTop, DescBottom, AscTop, AscBottom);

    ok(RoundTop >= 0, "caption text was not rendered\n");
    if (RoundTop >= 0)
    {
        ok(DescBottom >= RoundBottom + 2, "descenders are clipped: g ends at row %d, o at row %d\n", DescBottom, RoundBottom);
        ok(AscTop <= RoundTop - 2, "ascenders are clipped: l starts at row %d, o at row %d\n", AscTop, RoundTop);
        ok(DescBottom < Height - 1, "descenders touch the client edge: g ends at row %d of %d\n", DescBottom, Height);
        ok(AscTop > 0, "ascenders touch the frame edge\n");
    }

    HeapFree(GetProcessHeap(), 0, Background);
    DestroyWindow(Window);
}
