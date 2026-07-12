/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Win11-styled popup menu (rounded, custom drawn, submenus)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define MENU_CLASS L"TM11Menu"

struct MenuLevel
{
    HWND  hwnd;
    const MItem* items;
    int   n;
    int   hot;
    int   width;
};

static MenuLevel s_lvl[4];
static int  s_depth = 0;
static UINT s_result = 0;
static BOOL s_done = FALSE;

static int ItemH(const MItem& it)
{
    return (it.flags & MIF_SEP) ? S(9) : S(34);
}

static int MenuHeight(const MItem* items, int n)
{
    int h = S(8);
    for (int i = 0; i < n; i++)
        h += ItemH(items[i]);
    return h + S(8);
}

static int MenuWidth(const MItem* items, int n)
{
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, g_t.fBody);
    int w = S(150);
    for (int i = 0; i < n; i++)
    {
        if (items[i].flags & MIF_SEP) continue;
        SIZE sz;
        GetTextExtentPoint32W(dc, items[i].text, lstrlenW(items[i].text), &sz);
        int need = sz.cx + S(36) + S(40);
        if (need > w) w = need;
    }
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    if (w > S(380)) w = S(380);
    return w;
}

static int ItemAtY(const MenuLevel& lv, int y)
{
    int cy = S(8);
    for (int i = 0; i < lv.n; i++)
    {
        int h = ItemH(lv.items[i]);
        if (y >= cy && y < cy + h)
            return (lv.items[i].flags & MIF_SEP) ? -1 : i;
        cy += h;
    }
    return -1;
}

static void ItemRect(const MenuLevel& lv, int idx, RECT* r)
{
    int cy = S(8);
    for (int i = 0; i < lv.n; i++)
    {
        int h = ItemH(lv.items[i]);
        if (i == idx)
        {
            r->left = S(5);
            r->right = lv.width - S(5);
            r->top = cy;
            r->bottom = cy + h;
            return;
        }
        cy += h;
    }
    SetRectEmpty(r);
}

static void PaintMenu(MenuLevel& lv, HDC dc)
{
    RECT rc;
    GetClientRect(lv.hwnd, &rc);
    FillRect32(dc, rc, g_t.cardBg);

    for (int i = 0; i < lv.n; i++)
    {
        const MItem& it = lv.items[i];
        RECT r;
        ItemRect(lv, i, &r);

        if (it.flags & MIF_SEP)
        {
            DrawHLine(dc, (r.top + r.bottom) / 2, r.left + S(6), r.right - S(6), g_t.divider);
            continue;
        }

        BOOL dis = (it.flags & MIF_DISABLED) != 0;
        if (i == lv.hot && !dis)
            FillRoundRect(dc, r, g_t.hoverBg, CLR_NONE, S(4));

        COLORREF tc = dis ? g_t.textDis :
                      (it.flags & MIF_DANGER) ? g_t.dangerText : g_t.textMain;

        if (it.flags & (MIF_CHECKED | MIF_RADIO))
        {
            RECT ck = { r.left + S(8), r.top + S(9), r.left + S(24), r.bottom - S(9) };
            if (it.flags & MIF_RADIO)
            {
                RECT dot = { ck.left + S(4), ck.top + S(4), ck.left + S(11), ck.top + S(11) };
                FillRoundRect(dc, dot, tc, CLR_NONE, S(4));
            }
            else
            {
                DrawGlyph(dc, ck, IC_CHECK, tc);
            }
        }

        RECT tr = { r.left + S(31), r.top, r.right - S(28), r.bottom };
        DrawTextClip(dc, it.text, tr,
                     (it.flags & MIF_DEFAULT) ? g_t.fBodySemi : g_t.fBody, tc,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        if (it.sub && it.nSub)
        {
            RECT ar = { r.right - S(22), r.top + S(10), r.right - S(8), r.bottom - S(10) };
            DrawGlyph(dc, ar, IC_CHEV_R, dis ? g_t.textDis : g_t.textSec);
        }
    }
}

static void RoundWindow(HWND h)
{
    RECT rc;
    GetWindowRect(h, &rc);
    HRGN rgn = CreateRoundRectRgn(0, 0, rc.right - rc.left + 1, rc.bottom - rc.top + 1,
                                  S(10), S(10));
    SetWindowRgn(h, rgn, TRUE);
}

static LRESULT CALLBACK MenuProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        for (int d = 0; d < s_depth; d++)
        {
            if (s_lvl[d].hwnd == hwnd)
            {
                BufPaint bp;
                HDC dc = bp.Begin(hdc, &ps.rcPaint);
                PaintMenu(s_lvl[d], dc);
                bp.End();
                break;
            }
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterMenuClass(void)
{
    static BOOL s_reg = FALSE;
    if (s_reg) return;
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_DROPSHADOW | CS_SAVEBITS;
    wc.lpfnWndProc = MenuProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = MENU_CLASS;
    RegisterClassW(&wc);
    s_reg = TRUE;
}

static void OpenLevel(HWND owner, const MItem* items, int n, int x, int y, BOOL alignRight)
{
    MenuLevel& lv = s_lvl[s_depth];
    lv.items = items;
    lv.n = n;
    lv.hot = -1;
    lv.width = MenuWidth(items, n);
    int h = MenuHeight(items, n);

    /* clamp to monitor work area */
    POINT pt = { x, y };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(mon, &mi);
    if (alignRight || x + lv.width > mi.rcWork.right)
        x = x - lv.width;
    if (x < mi.rcWork.left) x = mi.rcWork.left;
    if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
    if (y < mi.rcWork.top) y = mi.rcWork.top;

    lv.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
                              MENU_CLASS, L"", WS_POPUP,
                              x, y, lv.width, h, owner, NULL, g_app.hInst, NULL);
    RoundWindow(lv.hwnd);
    ShowWindow(lv.hwnd, SW_SHOWNOACTIVATE);
    s_depth++;
}

static void CloseTo(int depth)
{
    while (s_depth > depth)
    {
        s_depth--;
        DestroyWindow(s_lvl[s_depth].hwnd);
        s_lvl[s_depth].hwnd = NULL;
    }
}

static int LevelFromPoint(POINT scr)
{
    for (int d = s_depth - 1; d >= 0; d--)
    {
        RECT wr;
        GetWindowRect(s_lvl[d].hwnd, &wr);
        if (PtInRect(&wr, scr)) return d;
    }
    return -1;
}

static void SetHot(int depth, int idx)
{
    MenuLevel& lv = s_lvl[depth];
    if (lv.hot == idx) return;
    lv.hot = idx;
    InvalidateRect(lv.hwnd, NULL, FALSE);

    /* open submenu on hover */
    if (idx >= 0)
    {
        const MItem& it = lv.items[idx];
        CloseTo(depth + 1);
        if (it.sub && it.nSub && !(it.flags & MIF_DISABLED) && s_depth < 4)
        {
            RECT ir;
            ItemRect(lv, idx, &ir);
            POINT tl = { ir.right, ir.top - S(8) };
            ClientToScreen(lv.hwnd, &tl);
            OpenLevel(GetParent(lv.hwnd) ? GetParent(lv.hwnd) : lv.hwnd,
                      it.sub, it.nSub, tl.x, tl.y, FALSE);
        }
    }
}

static void Finish(UINT result)
{
    s_result = result;
    s_done = TRUE;
}

static void MenuKey(WPARAM vk)
{
    if (!s_depth) return;
    MenuLevel& lv = s_lvl[s_depth - 1];
    switch (vk)
    {
    case VK_ESCAPE:
        if (s_depth > 1) CloseTo(s_depth - 1);
        else Finish(0);
        break;
    case VK_UP:
    case VK_DOWN:
    {
        int dir = (vk == VK_DOWN) ? 1 : -1;
        int i = lv.hot;
        for (int step = 0; step < lv.n; step++)
        {
            i += dir;
            if (i < 0) i = lv.n - 1;
            if (i >= lv.n) i = 0;
            if (!(lv.items[i].flags & MIF_SEP)) break;
        }
        lv.hot = i;
        InvalidateRect(lv.hwnd, NULL, FALSE);
        break;
    }
    case VK_RIGHT:
        if (lv.hot >= 0 && lv.items[lv.hot].sub)
            SetHot(s_depth - 1, lv.hot);   /* reopens sub */
        break;
    case VK_LEFT:
        if (s_depth > 1) CloseTo(s_depth - 1);
        break;
    case VK_RETURN:
        if (lv.hot >= 0)
        {
            const MItem& it = lv.items[lv.hot];
            if (it.sub && it.nSub)
                SetHot(s_depth - 1, lv.hot);
            else if (!(it.flags & (MIF_DISABLED | MIF_SEP)))
                Finish(it.id);
        }
        break;
    }
}

static UINT RunMenu(HWND owner, int x, int y, const MItem* items, int n, BOOL alignRight)
{
    RegisterMenuClass();
    s_depth = 0;
    s_result = 0;
    s_done = FALSE;

    OpenLevel(owner, items, n, x, y, alignRight);
    HWND cap = s_lvl[0].hwnd;
    SetCapture(cap);

    MSG msg;
    while (!s_done && GetMessageW(&msg, NULL, 0, 0))
    {
        if (msg.message == WM_KEYDOWN)
        {
            MenuKey(msg.wParam);
            continue;
        }
        if (msg.message == WM_MOUSEMOVE || msg.message == WM_LBUTTONDOWN ||
            msg.message == WM_RBUTTONDOWN || msg.message == WM_LBUTTONUP)
        {
            POINT scr = { GET_X_LPARAM(msg.lParam), GET_Y_LPARAM(msg.lParam) };
            ClientToScreen(msg.hwnd, &scr);
            int d = LevelFromPoint(scr);

            if (msg.message == WM_MOUSEMOVE)
            {
                if (d >= 0)
                {
                    POINT cl = scr;
                    ScreenToClient(s_lvl[d].hwnd, &cl);
                    SetHot(d, ItemAtY(s_lvl[d], cl.y));
                }
            }
            else if (msg.message == WM_LBUTTONDOWN || msg.message == WM_RBUTTONDOWN)
            {
                if (d < 0)
                {
                    Finish(0);
                }
            }
            else /* WM_LBUTTONUP */
            {
                if (d >= 0)
                {
                    POINT cl = scr;
                    ScreenToClient(s_lvl[d].hwnd, &cl);
                    int idx = ItemAtY(s_lvl[d], cl.y);
                    if (idx >= 0)
                    {
                        const MItem& it = s_lvl[d].items[idx];
                        if (it.sub && it.nSub)
                            SetHot(d, idx);
                        else if (!(it.flags & MIF_DISABLED))
                            Finish(it.id);
                    }
                }
            }
            /* keep capture */
            if (!s_done && GetCapture() != cap)
                SetCapture(cap);
            continue;
        }
        if (msg.message == WM_CAPTURECHANGED && msg.hwnd == cap)
        {
            Finish(0);
            continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    ReleaseCapture();
    CloseTo(0);
    return s_result;
}

UINT Menu_Show(HWND owner, POINT scrPt, const MItem* items, int n)
{
    return RunMenu(owner, scrPt.x, scrPt.y, items, n, FALSE);
}

UINT Menu_ShowBelow(HWND owner, const RECT& rcAnchor, const MItem* items, int n, int minWidth)
{
    (void)minWidth;
    POINT pt = { rcAnchor.left, rcAnchor.bottom + S(4) };
    ClientToScreen(owner, &pt);
    return RunMenu(owner, pt.x, pt.y, items, n, FALSE);
}
