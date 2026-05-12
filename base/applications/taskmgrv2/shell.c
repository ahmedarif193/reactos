/*
 * shell.c — Outer top-level window, sidebar nav rail, content host,
 *            hamburger toggle, page registry, refresh timer, message pump.
 *
 * Main window class:  "TaskMgrV2.MainWindow"
 * Sidebar geometry:   See spec §4.1  (96 DPI values multiplied by Theme_DpiScale).
 * Page lazy creation: First navigation to a page calls pfnCreate and caches HWND.
 * Refresh timer:      Single WM_TIMER on the main window at Shell_GetRefreshIntervalMs().
 */
#include "precomp.h"

/* WM_DPICHANGED is defined in winuser.rh (resource scripts) but not in
 * winuser.h for this SDK version.  Add the define ourselves. */
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

/* -----------------------------------------------------------------------
 * Constants
 * ----------------------------------------------------------------------- */
#define MAIN_WND_CLASS          L"TaskMgrV2.MainWindow"
#define SIDEBAR_WND_CLASS       L"TaskMgrV2.Sidebar"
#define CONTENT_HOST_CLASS      L"TaskMgrV2.ContentHost"

/* Sidebar geometry at 96 DPI (scaled at runtime) */
#define RAIL_W_EXPANDED         220
#define RAIL_W_COLLAPSED         48
#define HAMBURGER_H              40
#define NAV_ITEM_H               48
#define NAV_ICON_W               20
#define NAV_ICON_H               20
#define NAV_ICON_PAD_L           14   /* left padding for icon in expanded */
#define NAV_LABEL_X              48   /* label start x in expanded mode */
#define ACCENT_PILL_W             3
#define ACCENT_PILL_H            24
#define ACCENT_PILL_RADIUS        2   /* 1.5 px spec — use 2 for integer GDI */
#define NAV_TOP_PAD               8
#define SETTINGS_SEPARATOR_H      1

/* Timer ID */
#define TIMER_REFRESH            1

/* BMP strip cell dimensions */
#define ICON_STRIP_CELL_W       20
#define ICON_STRIP_CELL_H       20

/* -----------------------------------------------------------------------
 * Module state
 * ----------------------------------------------------------------------- */
static HWND          s_hMainWnd       = NULL;
static HWND          s_hSidebar       = NULL;
static HWND          s_hContentHost   = NULL;
static HWND          s_hHamburgerBtn  = NULL;

static DWORD         s_refreshMs      = 1000;
static BOOL          s_bDark          = FALSE;
static BOOL          s_bExpanded      = TRUE;
static MODERN_PAGE_ID s_currentPage   = MPAGE_PROCESSES;

/* Page registry */
static PAGE_VTBL     s_pageVtbl[MPAGE__COUNT];
static HWND          s_pageHwnd[MPAGE__COUNT];
static BOOL          s_pageRegistered[MPAGE__COUNT];

/* Nav icon strip */
static HBITMAP       s_hNavStrip      = NULL;
static HBITMAP       s_hNavCells[9];  /* 9 nav-rail cells */
static BOOL          s_bStripLoaded   = FALSE;

/* -----------------------------------------------------------------------
 * Stub page support
 * Constraint #10: stub vtbl for pages not yet implemented.
 * ----------------------------------------------------------------------- */
typedef struct {
    WCHAR text[64];
} STUB_PAGE_DATA;

static LRESULT CALLBACK StubPageWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, Theme_BrushBg());
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, Theme_Current()->clrTextDim);
            SelectObject(hdc, Theme_FontUI());
            STUB_PAGE_DATA *pData = (STUB_PAGE_DATA *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            if (pData)
                DrawTextW(hdc, pData->text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_DESTROY:
        {
            STUB_PAGE_DATA *pData = (STUB_PAGE_DATA *)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
            if (pData)
            {
                free(pData);
                SetWindowLongPtrW(hWnd, GWLP_USERDATA, (LONG_PTR)NULL);
            }
            return 0;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static HWND CALLBACK StubPage_Create(HWND hHost, HINSTANCE hInst)
{
    static BOOL s_classRegistered = FALSE;
    WCHAR className[] = L"TaskMgrV2.StubPage";
    WNDCLASSEXW wc;

    if (!s_classRegistered)
    {
        ZeroMemory(&wc, sizeof(wc));
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = StubPageWndProc;
        wc.hInstance     = hInst;
        wc.hbrBackground = NULL;
        wc.lpszClassName = className;
        RegisterClassExW(&wc);
        s_classRegistered = TRUE;
    }

    return CreateWindowExW(0, className, NULL,
                           WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                           0, 0, 0, 0,
                           hHost, NULL, hInst, NULL);
}

/* Build a stub vtbl for a given page id. displayName is the nav label. */
static void RegisterStubPage(MODERN_PAGE_ID id, LPCWSTR displayName, int iconResId)
{
    PAGE_VTBL v;
    ZeroMemory(&v, sizeof(v));
    v.displayName = displayName;
    v.iconResId   = iconResId;
    v.pfnCreate   = StubPage_Create;
    Shell_RegisterPage(id, &v);
}

/* -----------------------------------------------------------------------
 * Icon strip loader
 * ----------------------------------------------------------------------- */
static void LoadNavStrip(void)
{
    if (s_bStripLoaded)
        return;

    s_hNavStrip = (HBITMAP)LoadImageW(g_hInst, MAKEINTRESOURCEW(IDB_NAV_STRIP),
                                       IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!s_hNavStrip)
        return;

    /* Slice 9 cells of ICON_STRIP_CELL_W x ICON_STRIP_CELL_H */
    {
        HDC hdcSrc = CreateCompatibleDC(NULL);
        HGDIOBJ hOldSrc = SelectObject(hdcSrc, s_hNavStrip);
        int i;
        for (i = 0; i < 9; i++)
        {
            HDC hdcDst = CreateCompatibleDC(NULL);
            s_hNavCells[i] = CreateBitmap(ICON_STRIP_CELL_W, ICON_STRIP_CELL_H, 1, 1, NULL);
            /* Use a compatible DC with a color bitmap instead */
            DeleteObject(s_hNavCells[i]);
            {
                HDC hdcScreen = GetDC(NULL);
                s_hNavCells[i] = CreateCompatibleBitmap(hdcScreen, ICON_STRIP_CELL_W, ICON_STRIP_CELL_H);
                ReleaseDC(NULL, hdcScreen);
            }
            HGDIOBJ hOldDst = SelectObject(hdcDst, s_hNavCells[i]);
            BitBlt(hdcDst, 0, 0, ICON_STRIP_CELL_W, ICON_STRIP_CELL_H,
                   hdcSrc, i * ICON_STRIP_CELL_W, 0, SRCCOPY);
            SelectObject(hdcDst, hOldDst);
            DeleteDC(hdcDst);
        }
        SelectObject(hdcSrc, hOldSrc);
        DeleteDC(hdcSrc);
    }
    s_bStripLoaded = TRUE;
}

/* -----------------------------------------------------------------------
 * Geometry helpers
 * ----------------------------------------------------------------------- */
static int RailWidth(void)
{
    return Theme_DpiScale(s_bExpanded ? RAIL_W_EXPANDED : RAIL_W_COLLAPSED);
}

/* Compute the rect of nav item i (0-based, excluding settings) within
 * the sidebar client area. */
static RECT NavItemRect(int idx)
{
    RECT rc;
    int itemH = Theme_DpiScale(NAV_ITEM_H);
    int topPad = Theme_DpiScale(NAV_TOP_PAD);
    int hambH  = Theme_DpiScale(HAMBURGER_H);

    rc.left   = 0;
    rc.right  = RailWidth();
    rc.top    = hambH + topPad + idx * itemH;
    rc.bottom = rc.top + itemH;
    return rc;
}

/* Compute the rect of the settings item (pinned to bottom). */
static RECT SettingsItemRect(void)
{
    RECT rc;
    RECT rcSidebar;
    int itemH = Theme_DpiScale(NAV_ITEM_H);

    GetClientRect(s_hSidebar, &rcSidebar);
    rc.left   = 0;
    rc.right  = RailWidth();
    rc.bottom = rcSidebar.bottom;
    rc.top    = rc.bottom - itemH;
    return rc;
}

/* -----------------------------------------------------------------------
 * Sidebar painting
 * ----------------------------------------------------------------------- */
static void PaintNavItem(HDC hdc, int pageIdx, MODERN_PAGE_ID id, BOOL isSettings)
{
    RECT itemRc = isSettings ? SettingsItemRect() : NavItemRect(pageIdx);
    const MODERN_THEME *t = Theme_Current();
    BOOL isSelected = (id == s_currentPage);

    /* Background */
    HBRUSH hbr = isSelected ? Theme_BrushAccent() : Theme_BrushBgAlt();
    /* For non-selected, use BgAlt only on hover; for simplicity use Bg */
    if (!isSelected)
        hbr = Theme_BrushBg();
    FillRect(hdc, &itemRc, hbr);

    /* Accent pill */
    if (isSelected)
    {
        RECT pill;
        int pillH  = Theme_DpiScale(ACCENT_PILL_H);
        int pillW  = Theme_DpiScale(ACCENT_PILL_W);
        int midY   = (itemRc.top + itemRc.bottom) / 2;
        pill.left   = itemRc.left;
        pill.right  = itemRc.left + pillW;
        pill.top    = midY - pillH / 2;
        pill.bottom = pill.top + pillH;
        Theme_FillRoundRect(hdc, &pill, Theme_DpiScale(ACCENT_PILL_RADIUS), t->clrAccent);
    }

    /* Icon */
    if (s_bStripLoaded && s_hNavCells[(int)id] && (int)id < 9)
    {
        int iconW  = Theme_DpiScale(ICON_STRIP_CELL_W);
        int iconH  = Theme_DpiScale(ICON_STRIP_CELL_H);
        int iconX, iconY;

        if (s_bExpanded)
            iconX = itemRc.left + Theme_DpiScale(NAV_ICON_PAD_L);
        else
            iconX = itemRc.left + (RailWidth() - iconW) / 2;

        iconY = itemRc.top + (itemRc.bottom - itemRc.top - iconH) / 2;

        HDC hdcMem = CreateCompatibleDC(hdc);
        HGDIOBJ hOld = SelectObject(hdcMem, s_hNavCells[(int)id]);
        TransparentBlt(hdc, iconX, iconY, iconW, iconH,
                       hdcMem, 0, 0, ICON_STRIP_CELL_W, ICON_STRIP_CELL_H,
                       RGB(255, 255, 255));
        SelectObject(hdcMem, hOld);
        DeleteDC(hdcMem);
    }

    /* Label (expanded only) */
    if (s_bExpanded && s_pageRegistered[(int)id])
    {
        RECT labelRc = itemRc;
        labelRc.left = Theme_DpiScale(NAV_LABEL_X);
        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, isSelected ? t->clrAccentText : t->clrText);
        SelectObject(hdc, Theme_FontUI());
        DrawTextW(hdc, s_pageVtbl[(int)id].displayName, -1,
                  &labelRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

static LRESULT CALLBACK SidebarWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);

            /* Fill background */
            FillRect(hdc, &rc, Theme_BrushBgAlt());

            /* Draw nav items (excluding settings) */
            {
                int i, idx = 0;
                for (i = 0; i < (int)MPAGE__COUNT; i++)
                {
                    if (i == (int)MPAGE_SETTINGS) continue;
                    PaintNavItem(hdc, idx, (MODERN_PAGE_ID)i, FALSE);
                    idx++;
                }
            }

            /* Separator above settings */
            {
                RECT sepRc = SettingsItemRect();
                HPEN hOld = (HPEN)SelectObject(hdc, Theme_PenDivider());
                MoveToEx(hdc, sepRc.left + Theme_DpiScale(8), sepRc.top - 1, NULL);
                LineTo(hdc, sepRc.right - Theme_DpiScale(8), sepRc.top - 1);
                SelectObject(hdc, hOld);
            }

            /* Settings item */
            PaintNavItem(hdc, 0, MPAGE_SETTINGS, TRUE);

            /* Right divider */
            Theme_DrawHairline(hdc, rc.right - 1, rc.top, rc.right - 1, rc.bottom);

            EndPaint(hWnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;

        case WM_LBUTTONDOWN:
        {
            POINT pt;
            pt.x = LOWORD(lParam);
            pt.y = HIWORD(lParam);

            /* Hit-test nav items */
            {
                int i, idx = 0;
                for (i = 0; i < (int)MPAGE__COUNT; i++)
                {
                    BOOL isSettings = (i == (int)MPAGE_SETTINGS);
                    RECT itemRc = isSettings ? SettingsItemRect() : NavItemRect(idx);
                    if (!isSettings) idx++;
                    if (PtInRect(&itemRc, pt))
                    {
                        Shell_NavigateTo((MODERN_PAGE_ID)i);
                        break;
                    }
                }
            }
            return 0;
        }

        case WM_SIZE:
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* -----------------------------------------------------------------------
 * Content host — transparent pass-through container
 * ----------------------------------------------------------------------- */
static LRESULT CALLBACK ContentHostWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_SIZE:
        {
            int cx = LOWORD(lParam);
            int cy = HIWORD(lParam);
            /* Resize whichever page is currently visible */
            if (s_currentPage < MPAGE__COUNT && s_pageHwnd[s_currentPage])
            {
                SetWindowPos(s_pageHwnd[s_currentPage], NULL, 0, 0, cx, cy,
                             SWP_NOMOVE | SWP_NOZORDER);
                if (s_pageVtbl[s_currentPage].pfnResize)
                    s_pageVtbl[s_currentPage].pfnResize(s_pageHwnd[s_currentPage], cx, cy);
            }
            return 0;
        }
        case WM_ERASEBKGND:
        {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, Theme_BrushBg());
            return 1;
        }
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* -----------------------------------------------------------------------
 * Layout — recalculate child positions after main window resize or
 * sidebar toggle.
 * ----------------------------------------------------------------------- */
static void LayoutChildren(void)
{
    RECT rc;
    int railW, hambH, sidebarH, contentX, contentW, contentH;

    if (!s_hMainWnd) return;
    GetClientRect(s_hMainWnd, &rc);

    railW    = RailWidth();
    hambH    = Theme_DpiScale(HAMBURGER_H);
    sidebarH = rc.bottom - hambH;
    contentX = railW;
    contentW = rc.right - railW;
    contentH = rc.bottom;

    if (s_hHamburgerBtn)
        SetWindowPos(s_hHamburgerBtn, NULL, 0, 0, hambH, hambH,
                     SWP_NOMOVE | SWP_NOZORDER);

    if (s_hSidebar)
        SetWindowPos(s_hSidebar, NULL, 0, hambH, railW, sidebarH,
                     SWP_NOZORDER);

    if (s_hContentHost)
        SetWindowPos(s_hContentHost, NULL, contentX, 0, contentW, contentH,
                     SWP_NOZORDER);

    if (s_hSidebar) InvalidateRect(s_hSidebar, NULL, TRUE);
}

/* -----------------------------------------------------------------------
 * Hamburger button
 * ----------------------------------------------------------------------- */
static LRESULT CALLBACK HamburgerWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hWnd, &ps);
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, Theme_BrushBgAlt());

            /* Draw three horizontal bars */
            {
                HPEN hOld = (HPEN)SelectObject(hdc, CreatePen(PS_SOLID, 1, Theme_Current()->clrText));
                int w  = rc.right - rc.left;
                int h  = rc.bottom - rc.top;
                int barW = w * 14 / 20;
                int x  = (w - barW) / 2;
                int y1 = h * 6  / 20;
                int y2 = h * 10 / 20;
                int y3 = h * 14 / 20;
                MoveToEx(hdc, x, y1, NULL); LineTo(hdc, x + barW, y1);
                MoveToEx(hdc, x, y2, NULL); LineTo(hdc, x + barW, y2);
                MoveToEx(hdc, x, y3, NULL); LineTo(hdc, x + barW, y3);
                DeleteObject(SelectObject(hdc, hOld));
            }
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_ERASEBKGND:
            return 1;
        case WM_LBUTTONUP:
            Shell_SetSidebarExpanded(!s_bExpanded);
            return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* -----------------------------------------------------------------------
 * Main window proc
 * ----------------------------------------------------------------------- */
static LRESULT CALLBACK MainWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_CREATE:
        {
            HINSTANCE hInst = ((CREATESTRUCTW *)lParam)->hInstance;

            /* Hamburger button */
            {
                static BOOL s_hambClass = FALSE;
                if (!s_hambClass)
                {
                    WNDCLASSEXW wc;
                    ZeroMemory(&wc, sizeof(wc));
                    wc.cbSize        = sizeof(wc);
                    wc.lpfnWndProc   = HamburgerWndProc;
                    wc.hInstance     = hInst;
                    wc.lpszClassName = L"TaskMgrV2.Hamburger";
                    RegisterClassExW(&wc);
                    s_hambClass = TRUE;
                }
                s_hHamburgerBtn = CreateWindowExW(0, L"TaskMgrV2.Hamburger", NULL,
                                                   WS_CHILD | WS_VISIBLE,
                                                   0, 0, Theme_DpiScale(HAMBURGER_H), Theme_DpiScale(HAMBURGER_H),
                                                   hWnd, NULL, hInst, NULL);
            }

            /* Sidebar */
            {
                static BOOL s_sidebarClass = FALSE;
                if (!s_sidebarClass)
                {
                    WNDCLASSEXW wc;
                    ZeroMemory(&wc, sizeof(wc));
                    wc.cbSize        = sizeof(wc);
                    wc.lpfnWndProc   = SidebarWndProc;
                    wc.hInstance     = hInst;
                    wc.lpszClassName = SIDEBAR_WND_CLASS;
                    RegisterClassExW(&wc);
                    s_sidebarClass = TRUE;
                }
                s_hSidebar = CreateWindowExW(0, SIDEBAR_WND_CLASS, NULL,
                                              WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                              0, 0, 0, 0,
                                              hWnd, NULL, hInst, NULL);
            }

            /* Content host */
            {
                static BOOL s_contentClass = FALSE;
                if (!s_contentClass)
                {
                    WNDCLASSEXW wc;
                    ZeroMemory(&wc, sizeof(wc));
                    wc.cbSize        = sizeof(wc);
                    wc.lpfnWndProc   = ContentHostWndProc;
                    wc.hInstance     = hInst;
                    wc.lpszClassName = CONTENT_HOST_CLASS;
                    RegisterClassExW(&wc);
                    s_contentClass = TRUE;
                }
                s_hContentHost = CreateWindowExW(0, CONTENT_HOST_CLASS, NULL,
                                                  WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
                                                  0, 0, 0, 0,
                                                  hWnd, NULL, hInst, NULL);
            }

            LoadNavStrip();
            return 0;
        }

        case WM_SIZE:
            LayoutChildren();
            return 0;

        case WM_DPICHANGED:
        {
            UINT dpi = HIWORD(wParam);
            Theme_SetShellDpi(dpi);
            LayoutChildren();
            InvalidateRect(hWnd, NULL, TRUE);

            /* Move/resize to suggested rect */
            {
                RECT *prc = (RECT *)lParam;
                SetWindowPos(hWnd, NULL,
                             prc->left, prc->top,
                             prc->right - prc->left, prc->bottom - prc->top,
                             SWP_NOZORDER | SWP_NOACTIVATE);
            }
            return 0;
        }

        case WM_TIMER:
            if (wParam == TIMER_REFRESH)
            {
                /* Tick perf data, then tick the visible page */
                PerfDataV2Tick();
                if (s_currentPage < MPAGE__COUNT
                    && s_pageHwnd[s_currentPage]
                    && s_pageVtbl[s_currentPage].pfnTick)
                {
                    s_pageVtbl[s_currentPage].pfnTick(s_pageHwnd[s_currentPage]);
                }
            }
            return 0;

        case WM_ERASEBKGND:
        {
            HDC hdc = (HDC)wParam;
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, Theme_BrushBg());
            return 1;
        }

        case WM_DESTROY:
        {
            int i;
            KillTimer(hWnd, TIMER_REFRESH);
            for (i = 0; i < (int)MPAGE__COUNT; i++)
            {
                if (s_pageHwnd[i])
                {
                    if (s_pageVtbl[i].pfnDestroy)
                        s_pageVtbl[i].pfnDestroy(s_pageHwnd[i]);
                    DestroyWindow(s_pageHwnd[i]);
                    s_pageHwnd[i] = NULL;
                }
            }
            PostQuitMessage(0);
            return 0;
        }

        case WM_SYSCOMMAND:
            if (wParam == SC_RESTORE)
            {
                ShowWindow(hWnd, SW_RESTORE);
                SetForegroundWindow(hWnd);
                return 0;
            }
            break;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* -----------------------------------------------------------------------
 * Public API
 * ----------------------------------------------------------------------- */
HWND Shell_GetMainWnd(void)       { return s_hMainWnd; }
HWND Shell_GetContentHost(void)   { return s_hContentHost; }

MODERN_PAGE_ID Shell_GetCurrentPage(void) { return s_currentPage; }

void Shell_RegisterPage(MODERN_PAGE_ID id, const PAGE_VTBL *vtbl)
{
    if ((int)id < 0 || id >= MPAGE__COUNT || !vtbl) return;
    s_pageVtbl[id]        = *vtbl;
    s_pageRegistered[id]  = TRUE;
}

void Shell_NavigateTo(MODERN_PAGE_ID id)
{
    HWND hHost;
    RECT rcHost;
    int cx, cy;

    if (id >= MPAGE__COUNT) return;
    if (!s_pageRegistered[id]) return;

    hHost = s_hContentHost;

    /* Lazy creation */
    if (!s_pageHwnd[id])
    {
        if (!s_pageVtbl[id].pfnCreate) return;
        s_pageHwnd[id] = s_pageVtbl[id].pfnCreate(hHost, g_hInst);
        if (!s_pageHwnd[id]) return;
    }

    /* Hide old page */
    if (s_currentPage < MPAGE__COUNT && s_pageHwnd[s_currentPage]
        && s_currentPage != id)
    {
        ShowWindow(s_pageHwnd[s_currentPage], SW_HIDE);
    }

    s_currentPage = id;

    /* Size and show new page */
    GetClientRect(hHost, &rcHost);
    cx = rcHost.right  - rcHost.left;
    cy = rcHost.bottom - rcHost.top;

    SetWindowPos(s_pageHwnd[id], NULL, 0, 0, cx, cy,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_SHOWWINDOW);

    if (s_pageVtbl[id].pfnResize)
        s_pageVtbl[id].pfnResize(s_pageHwnd[id], cx, cy);

    /* Repaint sidebar to reflect selection */
    if (s_hSidebar)
        InvalidateRect(s_hSidebar, NULL, TRUE);
}

void Shell_RequestRefresh(void)
{
    if (s_hMainWnd)
        PostMessageW(s_hMainWnd, WM_TIMER, TIMER_REFRESH, 0);
}

DWORD Shell_GetRefreshIntervalMs(void) { return s_refreshMs; }

void Shell_SetRefreshIntervalMs(DWORD ms)
{
    if (ms < 250)  ms = 250;
    if (ms > 5000) ms = 5000;
    s_refreshMs = ms;
    /* Restart timer */
    if (s_hMainWnd)
    {
        KillTimer(s_hMainWnd, TIMER_REFRESH);
        SetTimer(s_hMainWnd, TIMER_REFRESH, s_refreshMs, NULL);
    }
}

BOOL Shell_IsDarkMode(void)          { return s_bDark; }

void Shell_SetDarkMode(BOOL bDark)
{
    int i;
    if ((bDark ? TRUE : FALSE) == s_bDark) return;
    s_bDark = bDark ? TRUE : FALSE;
    Theme_SetDark(s_bDark);

    /* Notify all created pages */
    for (i = 0; i < (int)MPAGE__COUNT; i++)
    {
        if (s_pageHwnd[i] && s_pageVtbl[i].pfnTheme)
            s_pageVtbl[i].pfnTheme(s_pageHwnd[i]);
    }

    if (s_hMainWnd)   InvalidateRect(s_hMainWnd, NULL, TRUE);
    if (s_hSidebar)   InvalidateRect(s_hSidebar, NULL, TRUE);
}

BOOL Shell_IsSidebarExpanded(void)    { return s_bExpanded; }

void Shell_SetSidebarExpanded(BOOL bExpanded)
{
    s_bExpanded = bExpanded ? TRUE : FALSE;
    LayoutChildren();
    if (s_hSidebar) InvalidateRect(s_hSidebar, NULL, TRUE);
}

/* -----------------------------------------------------------------------
 * Shell_Main — register window class, create main window, message loop.
 * ----------------------------------------------------------------------- */
int Shell_Main(HINSTANCE hInst, LPWSTR lpCmdLine, int nCmdShow)
{
    WNDCLASSEXW wc;
    MSG msg;
    DWORD darkMode = 0;
    DWORD sidebarExpanded = 1;
    DWORD refreshMs = 1000;
    DWORD defaultPage = (DWORD)MPAGE_PROCESSES;
    HACCEL hAccel;

    /* Initialize page arrays */
    ZeroMemory(s_pageVtbl,       sizeof(s_pageVtbl));
    ZeroMemory(s_pageHwnd,       sizeof(s_pageHwnd));
    ZeroMemory(s_pageRegistered, sizeof(s_pageRegistered));

    /* Load shell settings */
    Reg_ReadDword(HKEY_CURRENT_USER, L"Software\\ReactOS\\TaskMgrV2\\Shell",
                  L"DarkMode", &darkMode);
    Reg_ReadDword(HKEY_CURRENT_USER, L"Software\\ReactOS\\TaskMgrV2\\Shell",
                  L"SidebarExpanded", &sidebarExpanded);
    Reg_ReadDword(HKEY_CURRENT_USER, L"Software\\ReactOS\\TaskMgrV2\\Shell",
                  L"RefreshIntervalMs", &refreshMs);
    Reg_ReadDword(HKEY_CURRENT_USER, L"Software\\ReactOS\\TaskMgrV2\\Shell",
                  L"DefaultPage", &defaultPage);

    if (refreshMs < 250 || refreshMs > 5000) refreshMs = 1000;
    s_refreshMs = refreshMs;
    s_bExpanded = (sidebarExpanded != 0);
    /* darkMode == 2 means "follow system"; in v1 treat as light */
    s_bDark = (darkMode == 1) ? TRUE : FALSE;
    Theme_SetDark(s_bDark);

    /* Register main window class */
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInst;
    wc.hIcon         = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_TASKMGRV2));
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = MAIN_WND_CLASS;
    wc.hIconSm       = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_TASKMGRV2));

    if (!RegisterClassExW(&wc))
        return -1;

    /* Create main window */
    s_hMainWnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        MAIN_WND_CLASS,
        L"Task Manager",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT,
        900, 600,
        NULL, NULL, hInst, NULL);

    if (!s_hMainWnd)
        return -1;

    g_hMainWnd = s_hMainWnd;

    /* Register stub pages for pages not yet implemented by other tasks.
     * Real pages will call Shell_RegisterPage from their pfnCreate or
     * from an init function before Shell_NavigateTo is called. */
    RegisterStubPage(MPAGE_PROCESSES,   L"Processes",    0);
    RegisterStubPage(MPAGE_PERFORMANCE, L"Performance",  1);
    RegisterStubPage(MPAGE_APPHISTORY,  L"App history",  2);
    RegisterStubPage(MPAGE_STARTUP,     L"Startup apps", 3);
    RegisterStubPage(MPAGE_USERS,       L"Users",        4);
    RegisterStubPage(MPAGE_DETAILS,     L"Details",      5);
    RegisterStubPage(MPAGE_SERVICES,    L"Services",     6);
    RegisterStubPage(MPAGE_SETTINGS,    L"Settings",     7);

    /* Restore window placement if saved */
    {
        WINDOWPLACEMENT wp;
        DWORD cb = sizeof(wp);
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_CURRENT_USER,
                          L"Software\\ReactOS\\TaskMgrV2\\Shell",
                          0, KEY_QUERY_VALUE, &hKey) == ERROR_SUCCESS)
        {
            if (RegQueryValueExW(hKey, L"WindowPlacement", NULL, NULL,
                                 (LPBYTE)&wp, &cb) == ERROR_SUCCESS
                && cb == sizeof(wp))
            {
                wp.showCmd = nCmdShow;
                SetWindowPlacement(s_hMainWnd, &wp);
            }
            else
            {
                ShowWindow(s_hMainWnd, nCmdShow);
            }
            RegCloseKey(hKey);
        }
        else
        {
            ShowWindow(s_hMainWnd, nCmdShow);
        }
    }

    UpdateWindow(s_hMainWnd);

    /* DPI initialization */
    {
        HDC hdc = GetDC(s_hMainWnd);
        Theme_SetShellDpi((UINT)GetDeviceCaps(hdc, LOGPIXELSX));
        ReleaseDC(s_hMainWnd, hdc);
    }

    LayoutChildren();

    /* Navigate to default page */
    if (defaultPage >= (DWORD)MPAGE__COUNT)
        defaultPage = (DWORD)MPAGE_PROCESSES;
    Shell_NavigateTo((MODERN_PAGE_ID)defaultPage);

    /* Start refresh timer */
    SetTimer(s_hMainWnd, TIMER_REFRESH, s_refreshMs, NULL);

    /* Load accelerators */
    hAccel = LoadAcceleratorsW(hInst, MAKEINTRESOURCEW(IDA_PERF));

    /* Message loop */
    while (GetMessageW(&msg, NULL, 0, 0) > 0)
    {
        if (!hAccel || !TranslateAcceleratorW(s_hMainWnd, hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    /* Save window placement */
    {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(wp);
        if (GetWindowPlacement(s_hMainWnd, &wp))
        {
            HKEY hKey;
            if (RegCreateKeyExW(HKEY_CURRENT_USER,
                                L"Software\\ReactOS\\TaskMgrV2\\Shell",
                                0, NULL, 0, KEY_SET_VALUE, NULL, &hKey, NULL) == ERROR_SUCCESS)
            {
                RegSetValueExW(hKey, L"WindowPlacement", 0, REG_BINARY,
                               (const BYTE *)&wp, sizeof(wp));
                RegCloseKey(hKey);
            }
        }
    }

    if (s_hNavStrip)
    {
        int i;
        for (i = 0; i < 9; i++)
        {
            if (s_hNavCells[i]) DeleteObject(s_hNavCells[i]);
        }
        DeleteObject(s_hNavStrip);
        s_hNavStrip   = NULL;
        s_bStripLoaded = FALSE;
    }

    return (int)msg.wParam;
}
