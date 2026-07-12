/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Shared widgets: command buttons, toggles, search box
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

/* ------------------------------------------------------------------ */
/*  BtnStrip                                                           */
/* ------------------------------------------------------------------ */

void BtnStrip::Add(int id, const WCHAR* text, IconId icon, UINT style, BOOL enabled)
{
    UiBtn* btn = b.Add();
    if (!btn) return;
    btn->id = id;
    StringCchCopyW(btn->text, _countof(btn->text), text ? text : L"");
    btn->icon = icon;
    btn->style = style;
    btn->enabled = enabled;
}

void BtnStrip::SetEnabled(int id, BOOL en)
{
    for (int i = 0; i < b.n; i++)
    {
        if (b[i].id == id && b[i].enabled != en)
        {
            b[i].enabled = en;
            if (hwnd) InvalidateRect(hwnd, &b[i].r, FALSE);
        }
    }
}

BOOL BtnStrip::GetEnabled(int id)
{
    for (int i = 0; i < b.n; i++)
        if (b[i].id == id) return b[i].enabled;
    return FALSE;
}

int BtnMeasure(const UiBtn& btn)
{
    if (btn.style == BS_ICONONLY)
        return S(36);
    HDC dc = GetDC(NULL);
    HGDIOBJ old = SelectObject(dc, g_t.fBody);
    SIZE sz = { 0, 0 };
    if (btn.text[0])
        GetTextExtentPoint32W(dc, btn.text, lstrlenW(btn.text), &sz);
    SelectObject(dc, old);
    ReleaseDC(NULL, dc);
    int w = sz.cx + S(24);
    if (btn.icon != IC_NONE)
        w += S(22);
    if (w < S(60)) w = S(60);
    return w;
}

int BtnStrip::LayoutRight(const RECT& area, int gap)
{
    int x = area.right;
    int h = S(32);
    int y = area.top + ((area.bottom - area.top) - h) / 2;
    for (int i = b.n - 1; i >= 0; i--)
    {
        int w = BtnMeasure(b[i]);
        x -= w;
        b[i].r.left = x;
        b[i].r.right = x + w;
        b[i].r.top = y;
        b[i].r.bottom = y + h;
        x -= gap;
    }
    return b.n ? area.right - x - gap : 0;
}

int BtnStrip::LayoutLeft(const RECT& area, int gap)
{
    int x = area.left;
    int h = S(32);
    int y = area.top + ((area.bottom - area.top) - h) / 2;
    for (int i = 0; i < b.n; i++)
    {
        int w = BtnMeasure(b[i]);
        b[i].r.left = x;
        b[i].r.right = x + w;
        b[i].r.top = y;
        b[i].r.bottom = y + h;
        x += w + gap;
    }
    return x - area.left;
}

void BtnStrip::Paint(HDC dc)
{
    for (int i = 0; i < b.n; i++)
    {
        UiBtn& btn = b[i];
        BOOL isHot = (hot == i) && btn.enabled;
        BOOL isDown = (pressed == i) && btn.enabled;

        COLORREF fill = CLR_NONE, border = CLR_NONE, txt = g_t.textMain;

        switch (btn.style)
        {
        case BS_ACCENT:
            if (!btn.enabled)
            {
                fill = Blend(g_t.winBg, g_t.textDis, 40);
                txt = Blend(g_t.winBg, g_t.textDis, 10);
            }
            else
            {
                fill = isDown ? g_t.accentPressed : isHot ? g_t.accentHover : g_t.accent;
                txt = g_t.accentText;
            }
            break;
        case BS_OUTLINE:
            fill = isDown ? g_t.hoverBg : isHot ? g_t.hoverBg : g_t.cardBg;
            border = g_t.inputBorder;
            if (!btn.enabled) txt = g_t.textDis;
            break;
        case BS_ICONONLY:
        case BS_SUBTLE:
        default:
            if (isDown || isHot) fill = g_t.hoverBg;
            if (!btn.enabled) txt = g_t.textDis;
            break;
        }

        if (fill != CLR_NONE || border != CLR_NONE)
            FillRoundRect(dc, btn.r, fill, border, S(4));

        int cx = btn.r.left + S(12);
        if (btn.style == BS_ICONONLY)
        {
            RECT gr = { btn.r.left + S(10), btn.r.top + S(8),
                        btn.r.right - S(10), btn.r.bottom - S(8) };
            DrawGlyph(dc, gr, btn.icon, txt);
            continue;
        }
        if (btn.icon != IC_NONE)
        {
            int gy = (btn.r.top + btn.r.bottom) / 2;
            RECT gr = { cx, gy - S(8), cx + S(16), gy + S(8) };
            DrawGlyph(dc, gr, btn.icon, txt);
            cx += S(22);
        }
        RECT tr = { cx, btn.r.top, btn.r.right - S(10), btn.r.bottom };
        DrawTextClip(dc, btn.text, tr, g_t.fBody, txt,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER);
    }
}

int BtnStrip::OnMouse(UINT msg, POINT pt)
{
    int over = -1;
    for (int i = 0; i < b.n; i++)
        if (PtInRect(&b[i].r, pt)) { over = i; break; }

    switch (msg)
    {
    case WM_MOUSEMOVE:
        if (hot != over)
        {
            if (hwnd)
            {
                if (hot >= 0) InvalidateRect(hwnd, &b[hot].r, FALSE);
                if (over >= 0) InvalidateRect(hwnd, &b[over].r, FALSE);
            }
            hot = over;
        }
        break;
    case WM_LBUTTONDOWN:
        if (over >= 0 && b[over].enabled)
        {
            pressed = over;
            if (hwnd) InvalidateRect(hwnd, &b[over].r, FALSE);
        }
        break;
    case WM_LBUTTONUP:
    {
        int wasPressed = pressed;
        if (pressed >= 0 && hwnd)
            InvalidateRect(hwnd, &b[pressed].r, FALSE);
        pressed = -1;
        if (wasPressed >= 0 && wasPressed == over && b[over].enabled)
            return b[over].id;
        break;
    }
    }
    return 0;
}

void BtnStrip::ClearHot(void)
{
    if (hot >= 0 || pressed >= 0)
    {
        hot = -1;
        pressed = -1;
        if (hwnd) InvalidateRect(hwnd, NULL, FALSE);
    }
}

/* ------------------------------------------------------------------ */
/*  Toggle switch / radio                                              */
/* ------------------------------------------------------------------ */

void DrawToggle(HDC dc, const RECT& r, BOOL on, BOOL hot, BOOL enabled)
{
    int w = S(40), h = S(20);
    RECT tr = { r.left, r.top + ((r.bottom - r.top) - h) / 2, 0, 0 };
    tr.right = tr.left + w;
    tr.bottom = tr.top + h;

    COLORREF fill, border, knob;
    if (on)
    {
        fill = enabled ? (hot ? g_t.accentHover : g_t.accent) : g_t.textDis;
        border = fill;
        knob = g_t.dark ? RGB(0, 0, 0) : RGB(0xFF, 0xFF, 0xFF);
        if (!g_t.dark) knob = RGB(0xFF, 0xFF, 0xFF);
    }
    else
    {
        fill = g_t.dark ? Blend(g_t.cardBg, RGB(255, 255, 255), 4)
                        : Blend(g_t.cardBg, RGB(0, 0, 0), 2);
        border = enabled ? g_t.textSec : g_t.textDis;
        knob = enabled ? g_t.textSec : g_t.textDis;
    }

    FillRoundRect(dc, tr, fill, border, h / 2);

    int kd = S(12);
    int kx = on ? tr.right - S(4) - kd : tr.left + S(4);
    int ky = tr.top + (h - kd) / 2;
    RECT kr = { kx, ky, kx + kd, ky + kd };
    FillRoundRect(dc, kr, knob, CLR_NONE, kd / 2);
}

void DrawRadio(HDC dc, const RECT& r, BOOL on, BOOL hot)
{
    int d = S(20);
    RECT rr = { r.left, r.top + ((r.bottom - r.top) - d) / 2, 0, 0 };
    rr.right = rr.left + d;
    rr.bottom = rr.top + d;

    if (on)
    {
        FillRoundRect(dc, rr, hot ? g_t.accentHover : g_t.accent, CLR_NONE, d / 2);
        int kd = S(8);
        RECT kr = { rr.left + (d - kd) / 2, rr.top + (d - kd) / 2, 0, 0 };
        kr.right = kr.left + kd;
        kr.bottom = kr.top + kd;
        FillRoundRect(dc, kr, g_t.dark ? RGB(0, 0, 0) : RGB(0xFF, 0xFF, 0xFF),
                      CLR_NONE, kd / 2);
    }
    else
    {
        FillRoundRect(dc, rr, g_t.inputBg, hot ? g_t.textMain : g_t.textSec, d / 2);
    }
}

/* ------------------------------------------------------------------ */
/*  Search box                                                         */
/* ------------------------------------------------------------------ */

struct SearchBox
{
    HWND hwnd;
    HWND edit;
    WNDPROC editProc;
    WCHAR placeholder[96];
    BOOL focus;
};

static LRESULT CALLBACK SearchEditProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
{
    SearchBox* sb = (SearchBox*)GetWindowLongPtrW(GetParent(h), GWLP_USERDATA);
    switch (msg)
    {
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE)
        {
            SetWindowTextW(h, L"");
            return 0;
        }
        break;
    case WM_CHAR:
        if (wp == 27) return 0;   /* swallow ESC beep */
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        if (sb)
        {
            sb->focus = (msg == WM_SETFOCUS);
            InvalidateRect(h, NULL, TRUE);
            InvalidateRect(sb->hwnd, NULL, TRUE);
        }
        break;
    case WM_PAINT:
    {
        LRESULT r = CallWindowProcW(sb ? sb->editProc : DefWindowProcW, h, msg, wp, lp);
        /* placeholder is drawn on the EDIT itself (it covers the parent) */
        if (sb && !sb->focus && GetWindowTextLengthW(h) == 0 && sb->placeholder[0])
        {
            HDC dc = GetDC(h);
            RECT rc;
            GetClientRect(h, &rc);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, g_t.textSec);
            HGDIOBJ old = SelectObject(dc, g_t.fBody);
            DrawTextW(dc, sb->placeholder, -1, &rc,
                      DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            SelectObject(dc, old);
            ReleaseDC(h, dc);
        }
        return r;
    }
    }
    return CallWindowProcW(sb ? sb->editProc : DefWindowProcW, h, msg, wp, lp);
}

static void SearchLayout(SearchBox* sb)
{
    RECT rc;
    GetClientRect(sb->hwnd, &rc);
    int h = S(16);
    MoveWindow(sb->edit, rc.left + S(30), rc.top + ((rc.bottom - rc.top) - h) / 2,
               rc.right - rc.left - S(40), h, TRUE);
}

static LRESULT CALLBACK SearchProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SearchBox* sb = (SearchBox*)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        sb = new SearchBox();
        ZeroMemory(sb, sizeof(*sb));
        sb->hwnd = hwnd;
        StringCchCopyW(sb->placeholder, _countof(sb->placeholder),
                       cs->lpszName ? cs->lpszName : L"Search");
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)sb);
        return TRUE;
    }
    case WM_CREATE:
        sb->edit = CreateWindowExW(0, L"EDIT", L"",
                                   WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                                   0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)1,
                                   g_app.hInst, NULL);
        sb->editProc = (WNDPROC)SetWindowLongPtrW(sb->edit, GWLP_WNDPROC,
                                                  (LONG_PTR)SearchEditProc);
        SendMessageW(sb->edit, WM_SETFONT, (WPARAM)g_t.fBody, 0);
        SearchLayout(sb);
        return 0;

    case WM_NCDESTROY:
        delete sb;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;

    case WM_SIZE:
        SearchLayout(sb);
        return 0;

    case WM_APP_THEMECHG:
        SendMessageW(sb->edit, WM_SETFONT, (WPARAM)g_t.fBody, 0);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;

    case WM_COMMAND:
        if (HIWORD(wp) == EN_CHANGE)
        {
            /* placeholder repaint + notify frame */
            InvalidateRect(hwnd, NULL, FALSE);
            SendMessageW(GetParent(hwnd), WM_APP_SEARCH, 0, (LPARAM)hwnd);
        }
        return 0;

    case WM_CTLCOLOREDIT:
    {
        HDC dc = (HDC)wp;
        SetTextColor(dc, g_t.textMain);
        SetBkColor(dc, g_t.inputBg);
        SetDCBrushColor(dc, g_t.inputBg);
        return (LRESULT)GetStockObject(DC_BRUSH);
    }

    case WM_LBUTTONDOWN:
        SetFocus(sb->edit);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        BufPaint bp;
        HDC dc = bp.Begin(hdc, &ps.rcPaint);

        RECT rc;
        GetClientRect(hwnd, &rc);
        /* parent shows through corners */
        FillRect32(dc, ps.rcPaint, g_t.winBg);
        FillRoundRect(dc, rc, g_t.inputBg, g_t.inputBorder, S(6));

        /* focus underline (WinUI style) */
        if (sb->focus)
        {
            RECT ul = { rc.left + S(6), rc.bottom - S(2), rc.right - S(6), rc.bottom - S(1) };
            FillRect32(dc, ul, g_t.accent);
        }

        RECT gr = { rc.left + S(8), rc.top + S(8), rc.left + S(24), rc.bottom - S(8) };
        DrawGlyph(dc, gr, IC_SEARCH, g_t.textSec);

        bp.End();
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void Search_Register(void)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = SearchProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_IBEAM);
    wc.lpszClassName = SB_CLASS;
    RegisterClassW(&wc);
}

HWND Search_Create(HWND parent, const WCHAR* placeholder)
{
    return CreateWindowExW(0, SB_CLASS, placeholder, WS_CHILD,
                           0, 0, S(300), S(32), parent, NULL, g_app.hInst, NULL);
}

void Search_GetText(HWND h, WCHAR* buf, int cch)
{
    SearchBox* sb = (SearchBox*)GetWindowLongPtrW(h, GWLP_USERDATA);
    buf[0] = 0;
    if (sb && sb->edit)
        GetWindowTextW(sb->edit, buf, cch);
}

void Search_Clear(HWND h)
{
    SearchBox* sb = (SearchBox*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (sb && sb->edit)
        SetWindowTextW(sb->edit, L"");
}

void Search_SetPlaceholder(HWND h, const WCHAR* placeholder)
{
    SearchBox* sb = (SearchBox*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (sb)
    {
        StringCchCopyW(sb->placeholder, _countof(sb->placeholder),
                       placeholder ? placeholder : L"Search");
        InvalidateRect(h, NULL, TRUE);
    }
}

void Search_Focus(HWND h)
{
    SearchBox* sb = (SearchBox*)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (sb && sb->edit)
        SetFocus(sb->edit);
}
