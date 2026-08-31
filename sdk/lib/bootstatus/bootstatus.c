/*
 * PROJECT:     ReactOS Modern Boot Status
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Borderless boot-status surface with system busy animation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windef.h>
#include <winbase.h>
#include <wingdi.h>
#include <winuser.h>

#include <reactos/bootstatus.h>

#include <debug.h>

#define BOOT_STATUS_CLASS_NAME L"ReactOSBootStatusWindow"
#define BOOT_STATUS_TIMER_ID 1
/* The built-in ocr_wait.ani uses a two-jiffy (1/30 second) frame rate. */
#define BOOT_STATUS_TIMER_INTERVAL 34
#define BOOT_STATUS_MAX_CURSOR_FRAMES 64
#define BOOT_STATUS_COPYDATA_TAG 0x53544252 /* 'RBTS' */

typedef struct _BOOT_STATUS_UPDATE_DATA
{
    WCHAR PhaseText[256];
    WCHAR DetailText[256];
    ULONG Completed;
    ULONG Total;
} BOOT_STATUS_UPDATE_DATA, *PBOOT_STATUS_UPDATE_DATA;

typedef struct _BOOT_STATUS_CONTEXT
{
    HDC BackgroundDc;
    HBITMAP BackgroundBitmap;
    HBITMAP OldBackgroundBitmap;
    HFONT PhaseFont;
    HFONT DetailFont;
    HCURSOR BusyCursor;
    BOOT_STATUS_UPDATE_DATA Status;
    UINT Frame;
    UINT FrameCount;
    BOOL FirstPaintComplete;
    INT Width;
    INT Height;
    INT CursorWidth;
    INT CursorHeight;
} BOOT_STATUS_CONTEXT, *PBOOT_STATUS_CONTEXT;

static VOID
BootStatusReleaseBackground(
    _Inout_ PBOOT_STATUS_CONTEXT Context)
{
    if (Context->BackgroundDc)
    {
        if (Context->OldBackgroundBitmap)
            SelectObject(Context->BackgroundDc, Context->OldBackgroundBitmap);

        if (Context->BackgroundBitmap)
            DeleteObject(Context->BackgroundBitmap);

        DeleteDC(Context->BackgroundDc);
    }

    Context->BackgroundDc = NULL;
    Context->BackgroundBitmap = NULL;
    Context->OldBackgroundBitmap = NULL;
}

static BOOL
BootStatusCaptureBackground(
    _Inout_ PBOOT_STATUS_CONTEXT Context)
{
    HDC ScreenDc;

    BootStatusReleaseBackground(Context);

    ScreenDc = GetDC(NULL);
    if (!ScreenDc)
        return FALSE;

    Context->BackgroundDc = CreateCompatibleDC(ScreenDc);
    if (Context->BackgroundDc)
    {
        Context->BackgroundBitmap = CreateCompatibleBitmap(ScreenDc,
                                                            Context->Width,
                                                            Context->Height);
    }

    if (!Context->BackgroundDc || !Context->BackgroundBitmap)
    {
        ReleaseDC(NULL, ScreenDc);
        BootStatusReleaseBackground(Context);
        return FALSE;
    }

    Context->OldBackgroundBitmap = SelectObject(Context->BackgroundDc,
                                                 Context->BackgroundBitmap);
    if (!Context->OldBackgroundBitmap ||
        !BitBlt(Context->BackgroundDc,
                0,
                0,
                Context->Width,
                Context->Height,
                ScreenDc,
                0,
                0,
                SRCCOPY))
    {
        ReleaseDC(NULL, ScreenDc);
        BootStatusReleaseBackground(Context);
        return FALSE;
    }

    ReleaseDC(NULL, ScreenDc);
    return TRUE;
}

static UINT
BootStatusGetCursorFrameCount(
    _In_ HCURSOR Cursor)
{
    HDC ScreenDc;
    HDC TestDc = NULL;
    HBITMAP TestBitmap = NULL;
    HBITMAP OldBitmap = NULL;
    UINT FrameCount = 0;

    if (!Cursor)
        return 0;

    ScreenDc = GetDC(NULL);
    if (!ScreenDc)
        return 1;

    TestDc = CreateCompatibleDC(ScreenDc);
    if (TestDc)
        TestBitmap = CreateCompatibleBitmap(ScreenDc, 1, 1);

    if (TestDc && TestBitmap)
    {
        OldBitmap = SelectObject(TestDc, TestBitmap);
        if (OldBitmap)
        {
            while (FrameCount < BOOT_STATUS_MAX_CURSOR_FRAMES &&
                   DrawIconEx(TestDc,
                              0,
                              0,
                              Cursor,
                              1,
                              1,
                              FrameCount,
                              NULL,
                              DI_NORMAL))
            {
                ++FrameCount;
            }

            SelectObject(TestDc, OldBitmap);
        }
    }

    if (TestBitmap)
        DeleteObject(TestBitmap);
    if (TestDc)
        DeleteDC(TestDc);
    ReleaseDC(NULL, ScreenDc);

    return FrameCount ? FrameCount : 1;
}

static VOID
BootStatusGetLayout(
    _In_ PBOOT_STATUS_CONTEXT Context,
    _Out_opt_ PRECT CursorRect,
    _Out_opt_ PRECT PhaseRect,
    _Out_opt_ PRECT DetailRect)
{
    INT CenterX = Context->Width / 2;
    INT CenterY = Context->Height / 2;
    INT CursorLeft = CenterX - Context->CursorWidth / 2;
    INT CursorTop = CenterY - Context->CursorHeight - 12;

    if (CursorRect)
    {
        SetRect(CursorRect,
                CursorLeft - 4,
                CursorTop - 4,
                CursorLeft + Context->CursorWidth + 4,
                CursorTop + Context->CursorHeight + 4);
    }

    if (PhaseRect)
    {
        SetRect(PhaseRect,
                Context->Width / 8,
                CenterY + 16,
                Context->Width - Context->Width / 8,
                CenterY + 54);
    }

    if (DetailRect)
    {
        SetRect(DetailRect,
                Context->Width / 8,
                CenterY + 53,
                Context->Width - Context->Width / 8,
                CenterY + 84);
    }
}

static VOID
BootStatusDrawBusyCursor(
    _In_ HDC Dc,
    _In_ PBOOT_STATUS_CONTEXT Context)
{
    RECT CursorRect;
    INT X;
    INT Y;

    if (!Context->BusyCursor)
        return;

    BootStatusGetLayout(Context, &CursorRect, NULL, NULL);
    X = (CursorRect.left + CursorRect.right - Context->CursorWidth) / 2;
    Y = (CursorRect.top + CursorRect.bottom - Context->CursorHeight) / 2;

    DrawIconEx(Dc,
               X,
               Y,
               Context->BusyCursor,
               Context->CursorWidth,
               Context->CursorHeight,
               Context->Frame,
               NULL,
               DI_NORMAL);
}

static VOID
BootStatusDrawTextLine(
    _In_ HDC Dc,
    _In_ HFONT Font,
    _In_ PCWSTR Text,
    _In_ PRECT TextRect,
    _In_ COLORREF TextColor)
{
    RECT ShadowRect;
    HGDIOBJ OldFont;

    if (!Text[0])
        return;

    ShadowRect = *TextRect;
    OffsetRect(&ShadowRect, 1, 1);
    OldFont = SelectObject(Dc, Font);
    SetBkMode(Dc, TRANSPARENT);
    SetTextColor(Dc, RGB(0, 0, 0));
    DrawTextW(Dc,
              Text,
              -1,
              &ShadowRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SetTextColor(Dc, TextColor);
    DrawTextW(Dc,
              Text,
              -1,
              TextRect,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    SelectObject(Dc, OldFont);
}

static VOID
BootStatusDrawText(
    _In_ HDC Dc,
    _In_ PBOOT_STATUS_CONTEXT Context)
{
    WCHAR DetailText[320];
    RECT PhaseRect;
    RECT DetailRect;

    BootStatusGetLayout(Context, NULL, &PhaseRect, &DetailRect);
    DetailText[0] = UNICODE_NULL;

    if (Context->Status.Total)
    {
        ULONG Percent;

        if (Context->Status.Completed >= Context->Status.Total)
            Percent = 100;
        else
            Percent = Context->Status.Completed * 100 / Context->Status.Total;

        if (Context->Status.DetailText[0])
        {
            wsprintfW(DetailText,
                      L"%s    %lu / %lu    %lu%%",
                      Context->Status.DetailText,
                      Context->Status.Completed,
                      Context->Status.Total,
                      Percent);
        }
        else
        {
            wsprintfW(DetailText,
                      L"%lu / %lu    %lu%%",
                      Context->Status.Completed,
                      Context->Status.Total,
                      Percent);
        }
    }
    else
    {
        lstrcpynW(DetailText,
                  Context->Status.DetailText,
                  ARRAYSIZE(DetailText));
    }

    BootStatusDrawTextLine(Dc,
                           Context->PhaseFont,
                           Context->Status.PhaseText,
                           &PhaseRect,
                           RGB(250, 251, 253));
    BootStatusDrawTextLine(Dc,
                           Context->DetailFont,
                           DetailText,
                           &DetailRect,
                           RGB(220, 226, 235));
}

static VOID
BootStatusApplyUpdate(
    _In_ HWND Window,
    _Inout_ PBOOT_STATUS_CONTEXT Context,
    _In_ const BOOT_STATUS_UPDATE_DATA *Update)
{
    RECT PhaseRect;
    RECT DetailRect;
    RECT TextRect;

    lstrcpynW(Context->Status.PhaseText,
              Update->PhaseText,
              ARRAYSIZE(Context->Status.PhaseText));
    lstrcpynW(Context->Status.DetailText,
              Update->DetailText,
              ARRAYSIZE(Context->Status.DetailText));
    Context->Status.Completed = Update->Completed;
    Context->Status.Total = Update->Total;

    DefWindowProcW(Window,
                   WM_SETTEXT,
                   0,
                   (LPARAM)Context->Status.PhaseText);

    BootStatusGetLayout(Context, NULL, &PhaseRect, &DetailRect);
    UnionRect(&TextRect, &PhaseRect, &DetailRect);
    InvalidateRect(Window, &TextRect, FALSE);
    if (IsWindowVisible(Window))
        UpdateWindow(Window);

    DPRINT1("BOOT_STATUS: UPDATE window=%p phase=%S detail=%S progress=%lu/%lu\n",
            Window,
            Context->Status.PhaseText,
            Context->Status.DetailText,
            Context->Status.Completed,
            Context->Status.Total);
}

static LRESULT CALLBACK
BootStatusWindowProc(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    PBOOT_STATUS_CONTEXT Context;

    Context = (PBOOT_STATUS_CONTEXT)GetWindowLongPtrW(Window, GWLP_USERDATA);

    switch (Message)
    {
        case WM_NCCREATE:
        {
            HDC Dc;
            INT Dpi = 96;

            Context = HeapAlloc(GetProcessHeap(),
                                HEAP_ZERO_MEMORY,
                                sizeof(*Context));
            if (!Context)
                return FALSE;

            Context->Width = GetSystemMetrics(SM_CXSCREEN);
            Context->Height = GetSystemMetrics(SM_CYSCREEN);
            Context->CursorWidth = GetSystemMetrics(SM_CXCURSOR);
            Context->CursorHeight = GetSystemMetrics(SM_CYCURSOR);
            Context->BusyCursor = LoadCursorW(NULL, (LPCWSTR)IDC_WAIT);
            Context->FrameCount = BootStatusGetCursorFrameCount(Context->BusyCursor);
            if (!Context->CursorWidth)
                Context->CursorWidth = 32;
            if (!Context->CursorHeight)
                Context->CursorHeight = 32;

            Dc = GetDC(NULL);
            if (Dc)
            {
                Dpi = GetDeviceCaps(Dc, LOGPIXELSY);
                ReleaseDC(NULL, Dc);
            }

            Context->PhaseFont = CreateFontW(-MulDiv(18, Dpi, 72),
                                              0,
                                              0,
                                              0,
                                              FW_SEMIBOLD,
                                              FALSE,
                                              FALSE,
                                              FALSE,
                                              DEFAULT_CHARSET,
                                              OUT_DEFAULT_PRECIS,
                                              CLIP_DEFAULT_PRECIS,
                                              CLEARTYPE_QUALITY,
                                              DEFAULT_PITCH | FF_DONTCARE,
                                              L"Segoe UI");
            Context->DetailFont = CreateFontW(-MulDiv(11, Dpi, 72),
                                               0,
                                               0,
                                               0,
                                               FW_NORMAL,
                                               FALSE,
                                               FALSE,
                                               FALSE,
                                               DEFAULT_CHARSET,
                                               OUT_DEFAULT_PRECIS,
                                               CLIP_DEFAULT_PRECIS,
                                               CLEARTYPE_QUALITY,
                                               DEFAULT_PITCH | FF_DONTCARE,
                                               L"Segoe UI");
            if (!Context->PhaseFont)
                Context->PhaseFont = GetStockObject(DEFAULT_GUI_FONT);
            if (!Context->DetailFont)
                Context->DetailFont = GetStockObject(DEFAULT_GUI_FONT);

            SetWindowLongPtrW(Window, GWLP_USERDATA, (LONG_PTR)Context);
            return TRUE;
        }

        case WM_CREATE:
            SetTimer(Window,
                     BOOT_STATUS_TIMER_ID,
                     BOOT_STATUS_TIMER_INTERVAL,
                     NULL);
            return 0;

        case WM_COPYDATA:
        {
            PCOPYDATASTRUCT CopyData = (PCOPYDATASTRUCT)lParam;

            if (!Context || !CopyData ||
                CopyData->dwData != BOOT_STATUS_COPYDATA_TAG ||
                CopyData->cbData != sizeof(BOOT_STATUS_UPDATE_DATA) ||
                !CopyData->lpData)
            {
                return FALSE;
            }

            BootStatusApplyUpdate(Window,
                                  Context,
                                  (const BOOT_STATUS_UPDATE_DATA *)CopyData->lpData);
            return TRUE;
        }

        case WM_TIMER:
            if (Context && wParam == BOOT_STATUS_TIMER_ID)
            {
                RECT CursorRect;

                if (Context->FrameCount > 1)
                    Context->Frame = (Context->Frame + 1) % Context->FrameCount;
                BootStatusGetLayout(Context, &CursorRect, NULL, NULL);
                InvalidateRect(Window, &CursorRect, FALSE);
            }
            return 0;

        case WM_ERASEBKGND:
            return TRUE;

        case WM_PAINT:
            if (Context)
            {
                PAINTSTRUCT Paint;
                HDC Dc = BeginPaint(Window, &Paint);

                if (Context->BackgroundDc)
                {
                    BitBlt(Dc,
                           Paint.rcPaint.left,
                           Paint.rcPaint.top,
                           Paint.rcPaint.right - Paint.rcPaint.left,
                           Paint.rcPaint.bottom - Paint.rcPaint.top,
                           Context->BackgroundDc,
                           Paint.rcPaint.left,
                           Paint.rcPaint.top,
                           SRCCOPY);
                }
                else
                {
                    HBRUSH Background = CreateSolidBrush(RGB(12, 18, 28));
                    if (Background)
                    {
                        FillRect(Dc, &Paint.rcPaint, Background);
                        DeleteObject(Background);
                    }
                }

                BootStatusDrawBusyCursor(Dc, Context);
                BootStatusDrawText(Dc, Context);
                EndPaint(Window, &Paint);

                if (!Context->FirstPaintComplete)
                {
                    Context->FirstPaintComplete = TRUE;
                    DPRINT1("BOOT_STATUS: FIRST_PAINT_COMPLETE window=%p size=%dx%d cursor=IDC_WAIT frames=%u\n",
                            Window,
                            Context->Width,
                            Context->Height,
                            Context->FrameCount);
                }
            }
            return 0;

        case WM_CLOSE:
            DestroyWindow(Window);
            return 0;

        case WM_DESTROY:
            KillTimer(Window, BOOT_STATUS_TIMER_ID);
            DPRINT1("BOOT_STATUS: DESTROY window=%p\n", Window);
            PostQuitMessage(0);
            return 0;

        case WM_NCDESTROY:
            if (Context)
            {
                HFONT StockFont = GetStockObject(DEFAULT_GUI_FONT);

                BootStatusReleaseBackground(Context);
                if (Context->PhaseFont && Context->PhaseFont != StockFont)
                    DeleteObject(Context->PhaseFont);
                if (Context->DetailFont && Context->DetailFont != StockFont)
                    DeleteObject(Context->DetailFont);
                HeapFree(GetProcessHeap(), 0, Context);
                SetWindowLongPtrW(Window, GWLP_USERDATA, 0);
            }
            return DefWindowProcW(Window, Message, wParam, lParam);
    }

    return DefWindowProcW(Window, Message, wParam, lParam);
}

static BOOL
BootStatusRegisterClass(VOID)
{
    WNDCLASSEXW Class = {0};
    HINSTANCE Instance = GetModuleHandleW(NULL);

    Class.cbSize = sizeof(Class);
    Class.lpfnWndProc = BootStatusWindowProc;
    Class.hInstance = Instance;
    Class.hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    Class.lpszClassName = BOOT_STATUS_CLASS_NAME;

    if (RegisterClassExW(&Class))
        return TRUE;

    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

HWND
WINAPI
BootStatusFind(VOID)
{
    return FindWindowW(BOOT_STATUS_CLASS_NAME, NULL);
}

HWND
WINAPI
BootStatusCreate(
    _In_opt_ PCWSTR StatusText)
{
    HWND Window;
    PBOOT_STATUS_CONTEXT Context;
    INT Width;
    INT Height;

    if (!BootStatusRegisterClass())
        return NULL;

    Width = GetSystemMetrics(SM_CXSCREEN);
    Height = GetSystemMetrics(SM_CYSCREEN);
    Window = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             BOOT_STATUS_CLASS_NAME,
                             L"",
                             WS_POPUP,
                             0,
                             0,
                             Width,
                             Height,
                             NULL,
                             NULL,
                             GetModuleHandleW(NULL),
                             NULL);
    if (!Window)
        return NULL;

    Context = (PBOOT_STATUS_CONTEXT)GetWindowLongPtrW(Window, GWLP_USERDATA);
    if (Context)
        BootStatusCaptureBackground(Context);

    BootStatusUpdate(Window, StatusText, NULL, 0, 0);
    SetWindowPos(Window,
                 HWND_TOPMOST,
                 0,
                 0,
                 Width,
                 Height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
    InvalidateRect(Window, NULL, FALSE);
    UpdateWindow(Window);
    DPRINT1("BOOT_STATUS: READY window=%p phase=%S background=%s cursor=IDC_WAIT frames=%u\n",
            Window,
            StatusText ? StatusText : L"",
            (Context && Context->BackgroundDc) ? "captured" : "solid",
            Context ? Context->FrameCount : 0);
    return Window;
}

BOOL
WINAPI
BootStatusUpdate(
    _In_opt_ HWND Window,
    _In_opt_ PCWSTR PhaseText,
    _In_opt_ PCWSTR DetailText,
    _In_ ULONG Completed,
    _In_ ULONG Total)
{
    BOOT_STATUS_UPDATE_DATA Update = {{0}};
    COPYDATASTRUCT CopyData;
    DWORD_PTR Result = FALSE;

    if (!Window)
        Window = BootStatusFind();
    if (!Window)
        return FALSE;

    lstrcpynW(Update.PhaseText,
              PhaseText ? PhaseText : L"",
              ARRAYSIZE(Update.PhaseText));
    lstrcpynW(Update.DetailText,
              DetailText ? DetailText : L"",
              ARRAYSIZE(Update.DetailText));
    Update.Completed = Completed;
    Update.Total = Total;

    CopyData.dwData = BOOT_STATUS_COPYDATA_TAG;
    CopyData.cbData = sizeof(Update);
    CopyData.lpData = &Update;

    if (!SendMessageTimeoutW(Window,
                             WM_COPYDATA,
                             0,
                             (LPARAM)&CopyData,
                             SMTO_ABORTIFHUNG,
                             2000,
                             &Result))
    {
        return FALSE;
    }

    return Result != FALSE;
}

BOOL
WINAPI
BootStatusSetText(
    _In_opt_ HWND Window,
    _In_opt_ PCWSTR StatusText)
{
    return BootStatusUpdate(Window, StatusText, NULL, 0, 0);
}

VOID
WINAPI
BootStatusDestroy(
    _In_opt_ HWND Window)
{
    if (Window && IsWindow(Window))
        DestroyWindow(Window);
}
