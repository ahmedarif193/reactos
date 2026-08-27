/*
 * PROJECT:     ReactOS WDDM UI presentation diagnostics
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Exercise popup-menu, cursor and overlapping-WGL presentation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <windows.h>
#include <GL/gl.h>
#include <reactos/dwmframe.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>

#define MENU_ITEM_COUNT 6
#define MENU_MOVE_COUNT 120
#define MENU_MOVE_DELAY_MS 40
#define MENU_SAMPLE_COUNT 10
#define MENU_SAMPLE_ATTEMPTS 40
#define MENU_SAMPLE_DELAY_MS 4
#define SUBMENU_COUNT 4
#define SUBMENU_ITEM_COUNT 5
#define SUBMENU_SWITCH_COUNT 40
#define CURSOR_MOVE_COUNT 180
#define CURSOR_FRAME_MS 16
#define GL_FRAME_COUNT 240
#define GL_FRAME_MS 16
#define CURSOR_P95_LIMIT_US 50000ULL

typedef struct _MENU_TEST_CONTEXT
{
    HWND Owner;
    HMENU Menu;
    HMENU Submenus[SUBMENU_COUNT];
    DWORD ProcessId;
    HANDLE Thread;
    LONG HardFailures;
    LONG BlankTextSamples;
    LONG UnstableSamples;
    LONG UnsettledTransitions;
    LONG SelectionMisses;
    LONG MultiPresentMoves;
    LONG SubmenuBlankTextSamples;
    LONG SubmenuUnstableSamples;
    LONG SubmenuUnsettledTransitions;
    LONG SubmenuSelectionMisses;
    LONG SubmenuOpenFailures;
    LONG PositionFailures;
    ULONG MaxPresentsPerMove;
    ULONG MaxPresentsPerSubmenuSwitch;
    ULONGLONG CursorSamples[MENU_MOVE_COUNT];
    ULONG CursorSampleCount;
} MENU_TEST_CONTEXT, *PMENU_TEST_CONTEXT;

static const WCHAR OwnerClassName[] = L"WddmUiTestOwner";
static const WCHAR BackgroundClassName[] = L"WddmUiTestBackground";
static const WCHAR GlClassName[] = L"WddmUiTestGl";
static LARGE_INTEGER PerformanceFrequency;

static VOID
TestPrint(
    _In_z_ _Printf_format_string_ PCSTR Format,
    ...)
{
    CHAR Buffer[1024];
    va_list Arguments;

    va_start(Arguments, Format);
    _vsnprintf(Buffer, sizeof(Buffer) - 1, Format, Arguments);
    va_end(Arguments);
    Buffer[sizeof(Buffer) - 1] = '\0';
    OutputDebugStringA(Buffer);
    fputs(Buffer, stdout);
    fflush(stdout);
}

static VOID
PrintGuiResources(
    _In_z_ PCSTR Phase)
{
    HANDLE Process = GetCurrentProcess();

    TestPrint("WDDM_UI_RESOURCES phase=%s gdi=%lu user=%lu\n",
              Phase,
              GetGuiResources(Process, GR_GDIOBJECTS),
              GetGuiResources(Process, GR_USEROBJECTS));
}

static VOID
PumpMessages(VOID)
{
    MSG Message;

    while (PeekMessageW(&Message, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&Message);
        DispatchMessageW(&Message);
    }
}

static BOOL
QueryPresentStats(
    _Out_ PDXGK_PRESENT_STATS Stats)
{
    HDC ScreenDc;
    INT Result;

    ZeroMemory(Stats, sizeof(*Stats));
    ScreenDc = GetDC(NULL);
    if (ScreenDc == NULL)
        return FALSE;

    Result = ExtEscape(ScreenDc,
                       CDD_ESCAPE_PRESENT_STATS,
                       0,
                       NULL,
                       sizeof(*Stats),
                       (LPSTR)Stats);
    ReleaseDC(NULL, ScreenDc);
    return Result > 0 && Stats->StructSize == sizeof(*Stats);
}

static ULONGLONG
ElapsedMicroseconds(
    _In_ LARGE_INTEGER Start,
    _In_ LARGE_INTEGER End)
{
    return (ULONGLONG)((End.QuadPart - Start.QuadPart) * 1000000 /
                       PerformanceFrequency.QuadPart);
}

static INT __cdecl
CompareUnsignedLongLong(
    _In_ const VOID *Left,
    _In_ const VOID *Right)
{
    ULONGLONG A = *(const ULONGLONG *)Left;
    ULONGLONG B = *(const ULONGLONG *)Right;

    return A < B ? -1 : A > B ? 1 : 0;
}

static ULONGLONG
Percentile95(
    _In_reads_(Count) const ULONGLONG *Samples,
    _In_ ULONG Count)
{
    ULONGLONG Sorted[CURSOR_MOVE_COUNT];
    ULONG Index;

    if (Count == 0 || Count > ARRAYSIZE(Sorted))
        return 0;

    CopyMemory(Sorted, Samples, Count * sizeof(Sorted[0]));
    qsort(Sorted, Count, sizeof(Sorted[0]), CompareUnsignedLongLong);
    Index = (Count * 95 + 99) / 100 - 1;
    return Sorted[Index];
}

static BOOL
PixelNearColor(
    _In_reads_(4) const BYTE *Pixel,
    _In_ COLORREF Color,
    _In_ BYTE Tolerance)
{
    INT RedDelta = (INT)Pixel[2] - (INT)GetRValue(Color);
    INT GreenDelta = (INT)Pixel[1] - (INT)GetGValue(Color);
    INT BlueDelta = (INT)Pixel[0] - (INT)GetBValue(Color);

    if (RedDelta < 0) RedDelta = -RedDelta;
    if (GreenDelta < 0) GreenDelta = -GreenDelta;
    if (BlueDelta < 0) BlueDelta = -BlueDelta;
    return RedDelta <= Tolerance &&
           GreenDelta <= Tolerance &&
           BlueDelta <= Tolerance;
}

static BOOL
SampleMenuItem(
    _In_ HMENU Menu,
    _In_ UINT Item,
    _Out_ PBOOL HighlightVisible,
    _Out_ PBOOL TextVisible,
    _Out_ PBOOL Stable)
{
    BITMAPINFO BitmapInfo;
    RECT Rect;
    HDC ScreenDc = NULL;
    HDC MemoryDc = NULL;
    HBITMAP Bitmap = NULL;
    HGDIOBJ OldBitmap = NULL;
    BYTE *Pixels = NULL;
    COLORREF Highlight = GetSysColor(COLOR_HIGHLIGHT);
    COLORREF HighlightText = GetSysColor(COLOR_HIGHLIGHTTEXT);
    ULONG HighlightPixels = 0;
    ULONG TextPixels = 0;
    ULONG SamplePixels = 0;
    INT Width;
    INT Height;
    INT LeftMargin;
    INT RightMargin;
    INT x;
    INT y;
    BOOL Result = FALSE;
    DXGK_PRESENT_STATS Before;
    DXGK_PRESENT_STATS After;
    BOOL HaveBefore;
    BOOL HaveAfter;

    *HighlightVisible = FALSE;
    *TextVisible = FALSE;
    *Stable = FALSE;

    /* The CDD screen DC reads the GDI shadow primary, not necessarily the
     * physical scanout. Inspect it only between completed paint transactions;
     * otherwise this diagnostic observes an intermediate state that dxgkrnl
     * is deliberately withholding from the display. */
    HaveBefore = QueryPresentStats(&Before);
    if (!HaveBefore || Before.PresentBatchDepth != 0)
        return TRUE;

    if (!GetMenuItemRect(NULL, Menu, Item, &Rect))
        return FALSE;

    Width = Rect.right - Rect.left;
    Height = Rect.bottom - Rect.top;
    if (Width <= 0 || Height <= 0)
        return FALSE;

    ZeroMemory(&BitmapInfo, sizeof(BitmapInfo));
    BitmapInfo.bmiHeader.biSize = sizeof(BitmapInfo.bmiHeader);
    BitmapInfo.bmiHeader.biWidth = Width;
    BitmapInfo.bmiHeader.biHeight = -Height;
    BitmapInfo.bmiHeader.biPlanes = 1;
    BitmapInfo.bmiHeader.biBitCount = 32;
    BitmapInfo.bmiHeader.biCompression = BI_RGB;

    ScreenDc = GetDC(NULL);
    if (ScreenDc == NULL)
        goto Cleanup;
    MemoryDc = CreateCompatibleDC(ScreenDc);
    if (MemoryDc == NULL)
        goto Cleanup;
    Bitmap = CreateDIBSection(ScreenDc,
                              &BitmapInfo,
                              DIB_RGB_COLORS,
                              (PVOID *)&Pixels,
                              NULL,
                              0);
    if (Bitmap == NULL || Pixels == NULL)
        goto Cleanup;
    OldBitmap = SelectObject(MemoryDc, Bitmap);
    if (OldBitmap == NULL || OldBitmap == HGDI_ERROR)
        goto Cleanup;
    if (!BitBlt(MemoryDc,
                0,
                0,
                Width,
                Height,
                ScreenDc,
                Rect.left,
                Rect.top,
                SRCCOPY))
    {
        goto Cleanup;
    }

    LeftMargin = min(Width / 3, GetSystemMetrics(SM_CXMENUCHECK) + 6);
    RightMargin = min(Width / 5, GetSystemMetrics(SM_CXMENUCHECK) + 4);
    for (y = 2; y < Height - 2; ++y)
    {
        for (x = LeftMargin; x < Width - RightMargin; ++x)
        {
            const BYTE *Pixel = Pixels + ((y * Width + x) * 4);

            ++SamplePixels;
            if (PixelNearColor(Pixel, Highlight, 12))
                ++HighlightPixels;
            if (PixelNearColor(Pixel, HighlightText, 20))
                ++TextPixels;
        }
    }

    *HighlightVisible = SamplePixels != 0 && HighlightPixels >= SamplePixels / 5;
    *TextVisible = TextPixels >= 3;
    Result = TRUE;

Cleanup:
    if (OldBitmap != NULL && OldBitmap != HGDI_ERROR)
        SelectObject(MemoryDc, OldBitmap);
    if (Bitmap != NULL)
        DeleteObject(Bitmap);
    if (MemoryDc != NULL)
        DeleteDC(MemoryDc);
    if (ScreenDc != NULL)
        ReleaseDC(NULL, ScreenDc);

    HaveAfter = QueryPresentStats(&After);
    if (Result && HaveAfter &&
        After.PresentBatchDepth == 0 &&
        After.PresentBatchBegins == Before.PresentBatchBegins &&
        After.PresentBatchEnds == Before.PresentBatchEnds)
    {
        *Stable = TRUE;
    }
    return Result;
}

static VOID
ObserveMenuItemTransition(
    _In_ HMENU Menu,
    _In_ UINT Item,
    _Inout_ PLONG BlankTextSamples,
    _Inout_ PLONG SelectionMisses,
    _Inout_ PLONG UnstableSamples,
    _Inout_ PLONG UnsettledTransitions)
{
    BOOL HighlightSeen = FALSE;
    BOOL BlankTextSeen = FALSE;
    ULONG Attempt;
    ULONG StableSamples = 0;

    for (Attempt = 0;
         Attempt < MENU_SAMPLE_ATTEMPTS && StableSamples < MENU_SAMPLE_COUNT;
         ++Attempt)
    {
        BOOL HighlightVisible = FALSE;
        BOOL TextVisible = FALSE;
        BOOL Stable = FALSE;

        if (SampleMenuItem(Menu,
                           Item,
                           &HighlightVisible,
                           &TextVisible,
                           &Stable))
        {
            if (!Stable)
            {
                InterlockedIncrement(UnstableSamples);
            }
            else
            {
                ++StableSamples;
                if (HighlightVisible)
                {
                    HighlightSeen = TRUE;
                    if (!TextVisible)
                        BlankTextSeen = TRUE;
                }
            }
        }
        Sleep(MENU_SAMPLE_DELAY_MS);
    }

    if (!HighlightSeen)
        InterlockedIncrement(SelectionMisses);
    if (BlankTextSeen)
        InterlockedIncrement(BlankTextSamples);
    if (StableSamples != MENU_SAMPLE_COUNT)
        InterlockedIncrement(UnsettledTransitions);
}

typedef struct _MENU_WINDOW_SEARCH
{
    DWORD ProcessId;
    HWND Window;
} MENU_WINDOW_SEARCH, *PMENU_WINDOW_SEARCH;

static BOOL CALLBACK
FindMenuWindowCallback(
    _In_ HWND Window,
    _In_ LPARAM Parameter)
{
    PMENU_WINDOW_SEARCH Search = (PMENU_WINDOW_SEARCH)Parameter;
    WCHAR ClassName[32];
    DWORD ProcessId;

    GetWindowThreadProcessId(Window, &ProcessId);
    if (ProcessId != Search->ProcessId || !IsWindowVisible(Window))
        return TRUE;
    if (!GetClassNameW(Window, ClassName, ARRAYSIZE(ClassName)))
        return TRUE;
    if (lstrcmpW(ClassName, L"#32768") != 0)
        return TRUE;

    Search->Window = Window;
    return FALSE;
}

static HWND
FindTestMenuWindow(
    _In_ DWORD ProcessId)
{
    MENU_WINDOW_SEARCH Search;

    ZeroMemory(&Search, sizeof(Search));
    Search.ProcessId = ProcessId;
    EnumWindows(FindMenuWindowCallback, (LPARAM)&Search);
    return Search.Window;
}

static VOID
SendVirtualKey(
    _In_ WORD VirtualKey)
{
    INPUT Inputs[2];

    ZeroMemory(Inputs, sizeof(Inputs));
    Inputs[0].type = INPUT_KEYBOARD;
    Inputs[0].ki.wVk = VirtualKey;
    Inputs[1] = Inputs[0];
    Inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(ARRAYSIZE(Inputs), Inputs, sizeof(Inputs[0]));
}

static VOID
SendEscapeKey(VOID)
{
    SendVirtualKey(VK_ESCAPE);
}

static VOID
RunSubmenuHoverTransitions(
    _Inout_ PMENU_TEST_CONTEXT Context)
{
    ULONG Switch;

    TestPrint("WDDM_UI_SUBMENU_BEGIN switches=%u submenus=%u items=%u\n",
              SUBMENU_SWITCH_COUNT,
              SUBMENU_COUNT,
              SUBMENU_ITEM_COUNT);

    for (Switch = 0; Switch < SUBMENU_SWITCH_COUNT; ++Switch)
    {
        UINT RootItem = (Switch / SUBMENU_COUNT) & 1
                      ? SUBMENU_COUNT - 1 - (Switch % SUBMENU_COUNT)
                      : Switch % SUBMENU_COUNT;
        UINT SubItem = Switch % SUBMENU_ITEM_COUNT;
        DXGK_PRESENT_STATS Before;
        DXGK_PRESENT_STATS After;
        RECT RootRect;
        RECT SubRect;
        POINT Position;
        BOOL HaveBefore;
        BOOL HaveAfter;
        ULONG Attempt;

        HaveBefore = QueryPresentStats(&Before);
        if (!GetMenuItemRect(NULL, Context->Menu, RootItem, &RootRect))
        {
            InterlockedIncrement(&Context->SubmenuOpenFailures);
            continue;
        }

        Position.x = (RootRect.left + RootRect.right) / 2;
        Position.y = (RootRect.top + RootRect.bottom) / 2;
        if (!SetCursorPos(Position.x, Position.y))
            InterlockedIncrement(&Context->PositionFailures);
        Sleep(MENU_MOVE_DELAY_MS);
        SendVirtualKey(VK_RIGHT);

        for (Attempt = 0; Attempt < 50; ++Attempt)
        {
            if (GetMenuItemRect(NULL,
                                Context->Submenus[RootItem],
                                SubItem,
                                &SubRect))
            {
                break;
            }
            Sleep(10);
        }
        if (Attempt == 50)
        {
            InterlockedIncrement(&Context->SubmenuOpenFailures);
            SendVirtualKey(VK_LEFT);
            continue;
        }

        Position.x = (SubRect.left + SubRect.right) / 2;
        Position.y = (SubRect.top + SubRect.bottom) / 2;
        if (!SetCursorPos(Position.x, Position.y))
            InterlockedIncrement(&Context->PositionFailures);
        ObserveMenuItemTransition(Context->Submenus[RootItem],
                                  SubItem,
                                  &Context->SubmenuBlankTextSamples,
                                  &Context->SubmenuSelectionMisses,
                                  &Context->SubmenuUnstableSamples,
                                  &Context->SubmenuUnsettledTransitions);

        HaveAfter = QueryPresentStats(&After);
        if (HaveBefore && HaveAfter)
        {
            ULONG PresentDelta = After.ScanoutCopies - Before.ScanoutCopies;

            if (PresentDelta > Context->MaxPresentsPerSubmenuSwitch)
                Context->MaxPresentsPerSubmenuSwitch = PresentDelta;
        }

        SendVirtualKey(VK_LEFT);
        Sleep(MENU_MOVE_DELAY_MS);
    }

    TestPrint("WDDM_UI_SUBMENU_RESULT switches=%u open_fail=%ld "
              "blank_text=%ld unstable_samples=%ld settle_fail=%ld "
              "selection_miss=%ld "
              "max_presents_per_switch=%lu\n",
              SUBMENU_SWITCH_COUNT,
              Context->SubmenuOpenFailures,
              Context->SubmenuBlankTextSamples,
              Context->SubmenuUnstableSamples,
              Context->SubmenuUnsettledTransitions,
              Context->SubmenuSelectionMisses,
              Context->MaxPresentsPerSubmenuSwitch);
    TestPrint("WDDM_UI_SUBMENU_%s\n",
              Context->SubmenuOpenFailures == 0 &&
              Context->SubmenuBlankTextSamples == 0 &&
              Context->SubmenuUnsettledTransitions == 0 &&
              Context->SubmenuSelectionMisses == 0
                  ? "PASS" : "FAIL");
}

static DWORD WINAPI
MenuHoverThread(
    _In_ PVOID Parameter)
{
    PMENU_TEST_CONTEXT Context = (PMENU_TEST_CONTEXT)Parameter;
    DXGK_PRESENT_STATS Before;
    DXGK_PRESENT_STATS After;
    LARGE_INTEGER Start;
    LARGE_INTEGER End;
    HWND MenuWindow = NULL;
    DWORD WaitStart = GetTickCount();
    ULONG i;

    while (GetTickCount() - WaitStart < 5000)
    {
        MenuWindow = FindTestMenuWindow(Context->ProcessId);
        if (MenuWindow != NULL)
            break;
        Sleep(10);
    }
    if (MenuWindow == NULL)
    {
        InterlockedIncrement(&Context->HardFailures);
        TestPrint("WDDM_UI_MENU_ERROR menu_window_not_found\n");
        SendEscapeKey();
        return 1;
    }

    TestPrint("WDDM_UI_MENU_CAPTURE_READY hwnd=%p moves=%u delay_ms=%u\n",
              MenuWindow,
              MENU_MOVE_COUNT,
              MENU_MOVE_DELAY_MS);

    for (i = 0; i < MENU_MOVE_COUNT; ++i)
    {
        UINT Item = (i / MENU_ITEM_COUNT) & 1
                  ? MENU_ITEM_COUNT - 1 - (i % MENU_ITEM_COUNT)
                  : i % MENU_ITEM_COUNT;
        RECT Rect;
        POINT Expected;
        POINT Actual;
        BOOL HaveBefore;
        BOOL HaveAfter;
        ULONG PresentDelta = 0;

        if (!GetMenuItemRect(NULL, Context->Menu, Item, &Rect))
        {
            InterlockedIncrement(&Context->HardFailures);
            continue;
        }

        Expected.x = (Rect.left + Rect.right) / 2;
        Expected.y = (Rect.top + Rect.bottom) / 2;
        HaveBefore = QueryPresentStats(&Before);
        QueryPerformanceCounter(&Start);
        if (!SetCursorPos(Expected.x, Expected.y))
            InterlockedIncrement(&Context->PositionFailures);
        QueryPerformanceCounter(&End);
        if (Context->CursorSampleCount < ARRAYSIZE(Context->CursorSamples))
        {
            Context->CursorSamples[Context->CursorSampleCount++] =
                ElapsedMicroseconds(Start, End);
        }
        if (!GetCursorPos(&Actual) ||
            Actual.x != Expected.x ||
            Actual.y != Expected.y)
        {
            InterlockedIncrement(&Context->PositionFailures);
        }

        ObserveMenuItemTransition(Context->Menu,
                                  Item,
                                  &Context->BlankTextSamples,
                                  &Context->SelectionMisses,
                                  &Context->UnstableSamples,
                                  &Context->UnsettledTransitions);
        HaveAfter = QueryPresentStats(&After);
        if (HaveBefore && HaveAfter)
        {
            PresentDelta = After.ScanoutCopies - Before.ScanoutCopies;
            if (PresentDelta > Context->MaxPresentsPerMove)
                Context->MaxPresentsPerMove = PresentDelta;
            if (i >= MENU_ITEM_COUNT && PresentDelta > 1)
                InterlockedIncrement(&Context->MultiPresentMoves);
        }

    }

    RunSubmenuHoverTransitions(Context);
    TestPrint("WDDM_UI_MENU_CAPTURE_END\n");
    SendEscapeKey();
    return 0;
}

static HMENU
CreateHoverMenu(
    _Inout_ PMENU_TEST_CONTEXT Context)
{
    static const WCHAR *Labels[MENU_ITEM_COUNT] =
    {
        L"Programs and Applications",
        L"Documents and Settings",
        L"System Configuration",
        L"Hardware Diagnostics",
        L"Network Connections",
        L"Shut Down ReactOS"
    };
    HMENU Menu = CreatePopupMenu();
    UINT i;

    if (Menu == NULL)
        return NULL;
    for (i = 0; i < MENU_ITEM_COUNT; ++i)
    {
        if (i < SUBMENU_COUNT)
        {
            HMENU Submenu = CreatePopupMenu();
            UINT SubItem;

            if (Submenu == NULL)
            {
                DestroyMenu(Menu);
                return NULL;
            }
            for (SubItem = 0; SubItem < SUBMENU_ITEM_COUNT; ++SubItem)
            {
                WCHAR Text[80];

                _snwprintf(Text,
                           ARRAYSIZE(Text) - 1,
                           L"%s - option %u",
                           Labels[i],
                           SubItem + 1);
                Text[ARRAYSIZE(Text) - 1] = L'\0';
                if (!AppendMenuW(Submenu,
                                 MF_STRING,
                                 2000 + i * SUBMENU_ITEM_COUNT + SubItem,
                                 Text))
                {
                    DestroyMenu(Submenu);
                    DestroyMenu(Menu);
                    return NULL;
                }
            }
            if (!AppendMenuW(Menu,
                             MF_POPUP | MF_STRING,
                             (UINT_PTR)Submenu,
                             Labels[i]))
            {
                DestroyMenu(Submenu);
                DestroyMenu(Menu);
                return NULL;
            }
            Context->Submenus[i] = Submenu;
        }
        else if (!AppendMenuW(Menu, MF_STRING, 1000 + i, Labels[i]))
        {
            DestroyMenu(Menu);
            return NULL;
        }
    }
    return Menu;
}

static BOOL
RunMenuHoverTest(
    _In_ HWND Owner)
{
    MENU_TEST_CONTEXT Context;
    DXGK_PRESENT_STATS Before;
    DXGK_PRESENT_STATS After;
    RECT OwnerRect;
    DWORD WaitResult;
    ULONGLONG CursorP95;
    BOOL HaveBefore;
    BOOL HaveAfter;
    BOOL Passed;

    ZeroMemory(&Context, sizeof(Context));
    Context.Owner = Owner;
    Context.ProcessId = GetCurrentProcessId();
    Context.Menu = CreateHoverMenu(&Context);
    if (Context.Menu == NULL)
    {
        TestPrint("WDDM_UI_MENU_ERROR create_menu=%lu\n", GetLastError());
        return FALSE;
    }

    SetWindowTextW(Owner, L"WDDM popup-menu hover presentation test");
    ShowWindow(Owner, SW_SHOW);
    SetForegroundWindow(Owner);
    UpdateWindow(Owner);
    Sleep(200);
    GetWindowRect(Owner, &OwnerRect);

    HaveBefore = QueryPresentStats(&Before);
    TestPrint("WDDM_UI_MENU_BEGIN\n");
    Context.Thread = CreateThread(NULL,
                                  0,
                                  MenuHoverThread,
                                  &Context,
                                  0,
                                  NULL);
    if (Context.Thread == NULL)
    {
        TestPrint("WDDM_UI_MENU_ERROR create_thread=%lu\n", GetLastError());
        DestroyMenu(Context.Menu);
        return FALSE;
    }

    TrackPopupMenuEx(Context.Menu,
                     TPM_LEFTALIGN | TPM_TOPALIGN,
                     OwnerRect.left + 40,
                     OwnerRect.top + 70,
                     Owner,
                     NULL);
    WaitResult = WaitForSingleObject(Context.Thread, 10000);
    if (WaitResult != WAIT_OBJECT_0)
    {
        InterlockedIncrement(&Context.HardFailures);
        TestPrint("WDDM_UI_MENU_ERROR worker_wait=%lu\n", WaitResult);
    }
    CloseHandle(Context.Thread);
    DestroyMenu(Context.Menu);

    HaveAfter = QueryPresentStats(&After);
    CursorP95 = Percentile95(Context.CursorSamples,
                             Context.CursorSampleCount);
    TestPrint("WDDM_UI_MENU_RESULT moves=%lu blank_text=%ld unstable_samples=%ld "
              "settle_fail=%ld selection_miss=%ld "
              "multi_present_moves=%ld max_presents_per_move=%lu "
              "submenu_open_fail=%ld submenu_blank_text=%ld "
              "submenu_unstable_samples=%ld submenu_settle_fail=%ld "
              "submenu_selection_miss=%ld max_presents_per_submenu=%lu "
              "cursor_position_fail=%ld cursor_p95_us=%llu "
              "dirty_delta=%lu scanout_delta=%lu pending=%lu "
              "batch_begin_delta=%lu batch_end_delta=%lu "
              "batch_defer_delta=%lu batch_max_depth=%lu batch_depth=%lu\n",
              Context.CursorSampleCount,
              Context.BlankTextSamples,
              Context.UnstableSamples,
              Context.UnsettledTransitions,
              Context.SelectionMisses,
              Context.MultiPresentMoves,
              Context.MaxPresentsPerMove,
              Context.SubmenuOpenFailures,
              Context.SubmenuBlankTextSamples,
              Context.SubmenuUnstableSamples,
              Context.SubmenuUnsettledTransitions,
              Context.SubmenuSelectionMisses,
              Context.MaxPresentsPerSubmenuSwitch,
              Context.PositionFailures,
              (unsigned long long)CursorP95,
              HaveBefore && HaveAfter
                  ? After.DirtyRectRequests - Before.DirtyRectRequests : 0,
              HaveBefore && HaveAfter
                  ? After.ScanoutCopies - Before.ScanoutCopies : 0,
              HaveAfter ? After.PendingDirtyRect : ~0UL,
              HaveBefore && HaveAfter
                  ? After.PresentBatchBegins - Before.PresentBatchBegins : 0,
              HaveBefore && HaveAfter
                  ? After.PresentBatchEnds - Before.PresentBatchEnds : 0,
              HaveBefore && HaveAfter
                  ? After.PresentBatchFlushDeferrals - Before.PresentBatchFlushDeferrals : 0,
              HaveAfter ? After.PresentBatchMaxDepth : 0,
              HaveAfter ? After.PresentBatchDepth : ~0UL);

    /* ScanoutCopies is adapter-global and each 40 ms observation spans more
     * than two 60 Hz refreshes, so per-move counts are diagnostic rather than
     * a correctness gate. The pixel samples are the intermediate-frame test. */
    Passed = Context.HardFailures == 0 &&
             Context.BlankTextSamples == 0 &&
             Context.UnsettledTransitions == 0 &&
             Context.SelectionMisses == 0 &&
             Context.SubmenuOpenFailures == 0 &&
             Context.SubmenuBlankTextSamples == 0 &&
             Context.SubmenuUnsettledTransitions == 0 &&
             Context.SubmenuSelectionMisses == 0 &&
             Context.PositionFailures == 0 &&
             Context.CursorSampleCount == MENU_MOVE_COUNT &&
             HaveBefore && HaveAfter &&
             After.ScanoutCopies > Before.ScanoutCopies &&
             CursorP95 <= CURSOR_P95_LIMIT_US;
    TestPrint("WDDM_UI_MENU_%s\n", Passed ? "PASS" : "FAIL");
    return Passed;
}

static LRESULT CALLBACK
OwnerWindowProcedure(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    if (Message == WM_ERASEBKGND)
        return 1;
    if (Message == WM_PAINT)
    {
        PAINTSTRUCT Paint;
        RECT Rect;
        HDC Dc = BeginPaint(Window, &Paint);
        INT Height;
        INT y;

        GetClientRect(Window, &Rect);
        FillRect(Dc, &Rect, (HBRUSH)GetStockObject(BLACK_BRUSH));
        Height = max(1, (Rect.bottom - Rect.top) / 8);
        SetBkMode(Dc, TRANSPARENT);
        for (y = 0; y < Rect.bottom; y += Height)
        {
            RECT Stripe = { 0, y, Rect.right, min(Rect.bottom, y + Height) };
            if ((y / Height) & 1)
                FillRect(Dc, &Stripe, (HBRUSH)GetStockObject(DKGRAY_BRUSH));
        }
        SetTextColor(Dc, RGB(255, 255, 255));
        DrawTextW(Dc,
                  L"Cursor motion over active GDI repaint",
                  -1,
                  &Rect,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        EndPaint(Window, &Paint);
        return 0;
    }
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static BOOL
RunCursorMotionTest(
    _In_ HWND Owner)
{
    ULONGLONG Samples[CURSOR_MOVE_COUNT];
    POINT Original;
    RECT Client;
    LARGE_INTEGER Start;
    LARGE_INTEGER End;
    DWORD NextFrame;
    ULONG PositionFailures = 0;
    ULONG i;
    ULONGLONG P95;

    if (!GetCursorPos(&Original))
        return FALSE;
    SetWindowTextW(Owner, L"WDDM cursor motion and GDI repaint test");
    ShowWindow(Owner, SW_SHOW);
    SetForegroundWindow(Owner);
    GetClientRect(Owner, &Client);
    InvalidateRect(Owner, NULL, FALSE);
    UpdateWindow(Owner);

    TestPrint("WDDM_UI_CURSOR_CAPTURE_READY moves=%u frame_ms=%u\n",
              CURSOR_MOVE_COUNT,
              CURSOR_FRAME_MS);
    NextFrame = GetTickCount();
    for (i = 0; i < CURSOR_MOVE_COUNT; ++i)
    {
        POINT Expected;
        POINT Actual;
        INT SpanX = max(1, Client.right - 80);
        INT SpanY = max(1, Client.bottom - 80);

        Expected.x = 40 + (LONG)((i * 17) % SpanX);
        Expected.y = 40 + (LONG)((i * 11) % SpanY);
        ClientToScreen(Owner, &Expected);
        QueryPerformanceCounter(&Start);
        if (!SetCursorPos(Expected.x, Expected.y))
            ++PositionFailures;
        QueryPerformanceCounter(&End);
        Samples[i] = ElapsedMicroseconds(Start, End);
        if (!GetCursorPos(&Actual) ||
            Actual.x != Expected.x ||
            Actual.y != Expected.y)
        {
            ++PositionFailures;
        }

        InvalidateRect(Owner, NULL, FALSE);
        UpdateWindow(Owner);
        PumpMessages();
        NextFrame += CURSOR_FRAME_MS;
        if ((LONG)(NextFrame - GetTickCount()) > 0)
            Sleep(NextFrame - GetTickCount());
    }
    SetCursorPos(Original.x, Original.y);
    P95 = Percentile95(Samples, ARRAYSIZE(Samples));
    TestPrint("WDDM_UI_CURSOR_RESULT moves=%u position_fail=%lu p95_us=%llu limit_us=%llu\n",
              CURSOR_MOVE_COUNT,
              PositionFailures,
              (unsigned long long)P95,
              (unsigned long long)CURSOR_P95_LIMIT_US);
    TestPrint("WDDM_UI_CURSOR_%s\n",
              PositionFailures == 0 && P95 <= CURSOR_P95_LIMIT_US
                  ? "PASS" : "FAIL");
    return PositionFailures == 0 && P95 <= CURSOR_P95_LIMIT_US;
}

static LRESULT CALLBACK
ColorWindowProcedure(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    if (Message == WM_ERASEBKGND)
    {
        RECT Rect;
        HDC Dc = (HDC)wParam;
        COLORREF OldColor;

        GetClientRect(Window, &Rect);
        OldColor = SetDCBrushColor(Dc, RGB(255, 0, 255));
        FillRect(Dc, &Rect, GetStockObject(DC_BRUSH));
        SetDCBrushColor(Dc, OldColor);
        return 1;
    }
    if (Message == WM_PAINT)
    {
        PAINTSTRUCT Paint;
        RECT Rect;
        HDC Dc = BeginPaint(Window, &Paint);
        COLORREF OldColor;

        GetClientRect(Window, &Rect);
        OldColor = SetDCBrushColor(Dc, RGB(255, 0, 255));
        FillRect(Dc, &Rect, GetStockObject(DC_BRUSH));
        SetDCBrushColor(Dc, OldColor);
        EndPaint(Window, &Paint);
        return 0;
    }
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static LRESULT CALLBACK
GlWindowProcedure(
    _In_ HWND Window,
    _In_ UINT Message,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam)
{
    if (Message == WM_ERASEBKGND)
        return 1;
    return DefWindowProcW(Window, Message, wParam, lParam);
}

static BOOL
SetGlPixelFormat(
    _In_ HDC Dc)
{
    PIXELFORMATDESCRIPTOR Descriptor;
    INT Format;

    ZeroMemory(&Descriptor, sizeof(Descriptor));
    Descriptor.nSize = sizeof(Descriptor);
    Descriptor.nVersion = 1;
    Descriptor.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL |
                         PFD_DOUBLEBUFFER;
    Descriptor.iPixelType = PFD_TYPE_RGBA;
    Descriptor.cColorBits = 32;
    Descriptor.cDepthBits = 24;
    Descriptor.iLayerType = PFD_MAIN_PLANE;
    Format = ChoosePixelFormat(Dc, &Descriptor);
    return Format != 0 && SetPixelFormat(Dc, Format, &Descriptor);
}

static ULONG
CountMagentaExposureSamples(
    _In_ HWND GlWindow)
{
    static const INT Positions[9][2] =
    {
        { 1, 1 }, { 2, 1 }, { 3, 1 },
        { 1, 2 }, { 2, 2 }, { 3, 2 },
        { 1, 3 }, { 2, 3 }, { 3, 3 }
    };
    RECT Client;
    HDC ScreenDc;
    ULONG Exposures = 0;
    ULONG i;

    if (!GetClientRect(GlWindow, &Client))
        return 0;
    ScreenDc = GetDC(NULL);
    if (ScreenDc == NULL)
        return 0;

    for (i = 0; i < ARRAYSIZE(Positions); ++i)
    {
        POINT Point;
        COLORREF Pixel;

        Point.x = Client.right * Positions[i][0] / 4;
        Point.y = Client.bottom * Positions[i][1] / 4;
        ClientToScreen(GlWindow, &Point);
        Pixel = GetPixel(ScreenDc, Point.x, Point.y);
        if (Pixel != CLR_INVALID &&
            GetRValue(Pixel) >= 240 &&
            GetGValue(Pixel) <= 16 &&
            GetBValue(Pixel) >= 240)
        {
            ++Exposures;
        }
    }
    ReleaseDC(NULL, ScreenDc);
    return Exposures;
}

static BOOL
RunGlOverlapTest(
    _In_ HINSTANCE Instance,
    _In_ const RECT *WorkArea)
{
    HWND Background = NULL;
    HWND GlWindow = NULL;
    HDC GlDc = NULL;
    HGLRC GlContext = NULL;
    ULONG ExposureSamples = 0;
    ULONG SwapFailures = 0;
    ULONG i;
    INT Width = min(720, WorkArea->right - WorkArea->left - 120);
    INT Height = min(440, WorkArea->bottom - WorkArea->top - 140);
    INT x = WorkArea->left + 60;
    INT y = WorkArea->top + 70;
    BOOL Result = FALSE;

    Background = CreateWindowExW(WS_EX_TOOLWINDOW,
                                 BackgroundClassName,
                                 L"Bright background: any magenta inside the GL window is exposure",
                                 WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                 x,
                                 y,
                                 Width + 120,
                                 Height + 120,
                                 NULL,
                                 NULL,
                                 Instance,
                                 NULL);
    GlWindow = CreateWindowExW(WS_EX_TOOLWINDOW,
                               GlClassName,
                               L"WDDM WGL overlap presentation test",
                               WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                               x + 55,
                               y + 55,
                               Width,
                               Height,
                               NULL,
                               NULL,
                               Instance,
                               NULL);
    if (Background == NULL || GlWindow == NULL)
        goto Cleanup;
    PrintGuiResources("gl_windows_created");

    SetWindowPos(Background, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(GlWindow, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    UpdateWindow(Background);
    UpdateWindow(GlWindow);
    GlDc = GetDC(GlWindow);
    PrintGuiResources("gl_dc_acquired");
    if (GlDc == NULL || !SetGlPixelFormat(GlDc))
        goto Cleanup;
    PrintGuiResources("gl_pixel_format_set");
    GlContext = wglCreateContext(GlDc);
    PrintGuiResources("gl_context_created");
    if (GlContext == NULL || !wglMakeCurrent(GlDc, GlContext))
        goto Cleanup;
    PrintGuiResources("gl_context_current");

    TestPrint("WDDM_UI_GL_OVERLAP_CAPTURE_READY frames=%u frame_ms=%u\n",
              GL_FRAME_COUNT,
              GL_FRAME_MS);
    for (i = 0; i < GL_FRAME_COUNT; ++i)
    {
        RECT Client;
        GLfloat Phase = (GLfloat)(i % 120) / 120.0f;

        GetClientRect(GlWindow, &Client);
        glViewport(0, 0, Client.right, Client.bottom);
        glClearColor(0.02f, 0.08f, 0.20f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        glColor3f(0.0f, 0.7f, 1.0f);
        glVertex2f(-0.85f + Phase * 0.2f, -0.65f);
        glVertex2f( 0.85f, -0.65f);
        glColor3f(1.0f, 0.8f, 0.0f);
        glVertex2f( 0.85f,  0.65f);
        glVertex2f(-0.85f + Phase * 0.2f,  0.65f);
        glEnd();
        if (!SwapBuffers(GlDc))
            ++SwapFailures;

        if (i >= 20)
            ExposureSamples += CountMagentaExposureSamples(GlWindow);
        if (!(i % 12))
        {
            SetWindowPos(GlWindow,
                         HWND_TOP,
                         x + 55 + ((i / 12) & 1 ? 3 : 0),
                         y + 55,
                         0,
                         0,
                         SWP_NOSIZE | SWP_SHOWWINDOW);
        }
        PumpMessages();
        Sleep(GL_FRAME_MS);
    }

    TestPrint("WDDM_UI_GL_OVERLAP_RESULT frames=%u swap_fail=%lu magenta_exposure_samples=%lu\n",
              GL_FRAME_COUNT,
              SwapFailures,
              ExposureSamples);
    Result = SwapFailures == 0 && ExposureSamples == 0;
    TestPrint("WDDM_UI_GL_OVERLAP_%s\n", Result ? "PASS" : "FAIL");

Cleanup:
    if (GlContext != NULL)
    {
        wglMakeCurrent(NULL, NULL);
        PrintGuiResources("gl_context_released");
        wglDeleteContext(GlContext);
        PrintGuiResources("gl_context_deleted");
    }
    if (GlDc != NULL && GlWindow != NULL)
    {
        ReleaseDC(GlWindow, GlDc);
        PrintGuiResources("gl_dc_released");
    }
    if (GlWindow != NULL)
    {
        DestroyWindow(GlWindow);
        PrintGuiResources("gl_window_destroyed");
    }
    if (Background != NULL)
    {
        DestroyWindow(Background);
        PrintGuiResources("gl_background_destroyed");
    }
    PumpMessages();
    return Result;
}

static BOOL
RegisterTestClasses(
    _In_ HINSTANCE Instance)
{
    WNDCLASSW WindowClass;

    ZeroMemory(&WindowClass, sizeof(WindowClass));
    WindowClass.lpfnWndProc = OwnerWindowProcedure;
    WindowClass.hInstance = Instance;
    WindowClass.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32512));
    WindowClass.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    WindowClass.lpszClassName = OwnerClassName;
    if (!RegisterClassW(&WindowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return FALSE;
    }

    WindowClass.lpfnWndProc = ColorWindowProcedure;
    WindowClass.hbrBackground = NULL;
    WindowClass.lpszClassName = BackgroundClassName;
    if (!RegisterClassW(&WindowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return FALSE;
    }

    WindowClass.lpfnWndProc = GlWindowProcedure;
    WindowClass.hbrBackground = NULL;
    WindowClass.lpszClassName = GlClassName;
    if (!RegisterClassW(&WindowClass) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
    {
        return FALSE;
    }
    return TRUE;
}

int
main(VOID)
{
    HINSTANCE Instance = GetModuleHandleW(NULL);
    HWND Owner = NULL;
    RECT WorkArea;
    DXGK_PRESENT_STATS Stats;
    BOOL MenuPassed = FALSE;
    BOOL CursorPassed = FALSE;
    BOOL GlPassed = FALSE;
    INT Failures = 0;

    SetProcessDPIAware();
    if (!QueryPerformanceFrequency(&PerformanceFrequency) ||
        PerformanceFrequency.QuadPart == 0)
    {
        TestPrint("WDDM_UI_TEST_ERROR performance_counter_unavailable\n");
        return 1;
    }
    if (!QueryPresentStats(&Stats))
    {
        TestPrint("WDDM_UI_TEST_ERROR cdd_present_stats_unavailable\n");
        return 1;
    }
    if (!RegisterTestClasses(Instance))
    {
        TestPrint("WDDM_UI_TEST_ERROR register_class=%lu\n", GetLastError());
        return 1;
    }
    if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &WorkArea, 0))
    {
        SetRect(&WorkArea,
                0,
                0,
                GetSystemMetrics(SM_CXSCREEN),
                GetSystemMetrics(SM_CYSCREEN));
    }

    Owner = CreateWindowExW(WS_EX_TOOLWINDOW,
                            OwnerClassName,
                            L"WDDM UI presentation diagnostics",
                            WS_OVERLAPPEDWINDOW,
                            WorkArea.left + 30,
                            WorkArea.top + 30,
                            min(760, WorkArea.right - WorkArea.left - 60),
                            min(480, WorkArea.bottom - WorkArea.top - 60),
                            NULL,
                            NULL,
                            Instance,
                            NULL);
    if (Owner == NULL)
    {
        TestPrint("WDDM_UI_TEST_ERROR create_owner=%lu\n", GetLastError());
        return 1;
    }

    TestPrint("WDDM_UI_TEST_BEGIN stats_dirty=%lu stats_scanout=%lu pending=%lu composition=%lu batch=%lu\n",
              Stats.DirtyRectRequests,
              Stats.ScanoutCopies,
              Stats.PendingDirtyRect,
              Stats.CompositionActive,
              Stats.PresentBatchDepth);
    PrintGuiResources("owner_ready");

    MenuPassed = RunMenuHoverTest(Owner);
    PrintGuiResources("after_menu");
    if (!MenuPassed)
        ++Failures;
    CursorPassed = RunCursorMotionTest(Owner);
    PrintGuiResources("after_cursor");
    if (!CursorPassed)
        ++Failures;
    ShowWindow(Owner, SW_HIDE);
    GlPassed = RunGlOverlapTest(Instance, &WorkArea);
    PrintGuiResources("after_gl");
    if (!GlPassed)
        ++Failures;

    DestroyWindow(Owner);
    PumpMessages();
    UnregisterClassW(GlClassName, Instance);
    UnregisterClassW(BackgroundClassName, Instance);
    UnregisterClassW(OwnerClassName, Instance);
    PrintGuiResources("before_exit");
    TestPrint("WDDM_UI_TEST_END menu=%s cursor=%s gl_overlap=%s automated_failures=%d visual_result=inspect_hdmi_frames\n",
              MenuPassed ? "pass" : "fail",
              CursorPassed ? "pass" : "fail",
              GlPassed ? "pass" : "fail",
              Failures);
    return Failures == 0 ? 0 : 1;
}
