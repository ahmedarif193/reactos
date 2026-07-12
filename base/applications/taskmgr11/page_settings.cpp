/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Settings page (WinUI-style cards)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define PGO_CLASS L"TM11PageSettings"

enum RowKind { RW_HEADER, RW_DROPDOWN, RW_TOGGLE, RW_RADIO };

struct SetRow
{
    int   kind;
    const WCHAR* label;
    int   id;
    RECT  r;          /* computed at paint */
    RECT  rCtl;
};

enum
{
    SID_HDR_START = 1, SID_STARTPAGE,
    SID_HDR_SPEED, SID_SPEED,
    SID_HDR_WINDOW, SID_ONTOP, SID_MINONUSE, SID_HIDEMIN,
    SID_HDR_OTHER, SID_FULLACCT,
    SID_HDR_THEME, SID_TH_SYSTEM, SID_TH_LIGHT, SID_TH_DARK,
};

static SetRow s_rows[] =
{
    { RW_HEADER,   L"Default Start Page", SID_HDR_START },
    { RW_DROPDOWN, L"Page that opens when Task Manager starts", SID_STARTPAGE },
    { RW_HEADER,   L"Real time update speed", SID_HDR_SPEED },
    { RW_DROPDOWN, L"How often resource usage refreshes", SID_SPEED },
    { RW_HEADER,   L"Window management", SID_HDR_WINDOW },
    { RW_TOGGLE,   L"Always on top", SID_ONTOP },
    { RW_TOGGLE,   L"Minimize on use", SID_MINONUSE },
    { RW_TOGGLE,   L"Hide when minimized", SID_HIDEMIN },
    { RW_HEADER,   L"Other options", SID_HDR_OTHER },
    { RW_TOGGLE,   L"Show full account name", SID_FULLACCT },
    { RW_HEADER,   L"App theme", SID_HDR_THEME },
    { RW_RADIO,    L"Use system setting", SID_TH_SYSTEM },
    { RW_RADIO,    L"Light", SID_TH_LIGHT },
    { RW_RADIO,    L"Dark", SID_TH_DARK },
};

static const WCHAR* s_pageNames[] =
{
    L"Processes", L"Performance", L"App history", L"Startup apps",
    L"Users", L"Details", L"Services"
};

static const WCHAR* s_speedNames[] = { L"High", L"Normal", L"Low", L"Paused" };

struct SettingsPage : Page
{
    int scrollY;
    int hotRow;
    int contentH;

    SettingsPage() : scrollY(0), hotRow(-1), contentH(0) {}

    const WCHAR* Title() { return L"Settings"; }

    BOOL RowValue(const SetRow& row)
    {
        switch (row.id)
        {
        case SID_ONTOP:    return g_app.st.onTop;
        case SID_MINONUSE: return g_app.st.minOnUse;
        case SID_HIDEMIN:  return g_app.st.hideWhenMin;
        case SID_FULLACCT: return g_app.st.fullAcctName;
        case SID_TH_SYSTEM: return g_app.st.theme == TM_SYSTEM;
        case SID_TH_LIGHT:  return g_app.st.theme == TM_LIGHT;
        case SID_TH_DARK:   return g_app.st.theme == TM_DARK;
        }
        return FALSE;
    }

    void Layout(const RECT& rc)
    {
        int maxW = S(760);
        int w = rc.right - rc.left - S(48);
        if (w > maxW) w = maxW;
        int x = rc.left + S(24);
        int y = rc.top + S(10) - scrollY;

        for (int i = 0; i < (int)_countof(s_rows); i++)
        {
            SetRow& row = s_rows[i];
            int h;
            if (row.kind == RW_HEADER)
            {
                h = S(42);
                row.r.left = x;
                row.r.right = x + w;
                row.r.top = y + S(12);
                row.r.bottom = y + h;
            }
            else
            {
                h = S(58);
                row.r.left = x;
                row.r.right = x + w;
                row.r.top = y;
                row.r.bottom = y + h - S(4);
            }

            /* control area on the right */
            row.rCtl = row.r;
            if (row.kind == RW_DROPDOWN)
            {
                row.rCtl.left = row.r.right - S(190);
                row.rCtl.right = row.r.right - S(14);
                row.rCtl.top = row.r.top + ((row.r.bottom - row.r.top) - S(32)) / 2;
                row.rCtl.bottom = row.rCtl.top + S(32);
            }
            else if (row.kind == RW_TOGGLE)
            {
                row.rCtl.left = row.r.right - S(58);
                row.rCtl.right = row.r.right - S(14);
            }
            else if (row.kind == RW_RADIO)
            {
                row.rCtl.left = row.r.left + S(14);
                row.rCtl.right = row.r.left + S(38);
            }
            y += h;
        }
        contentH = y + scrollY + S(20) - rc.top;
    }

    void Paint(HDC dc, const RECT& rcPaint)
    {
        FillRect32(dc, rcPaint, g_t.listBg);
        RECT rc;
        GetClientRect(hwnd, &rc);
        Layout(rc);

        for (int i = 0; i < (int)_countof(s_rows); i++)
        {
            SetRow& row = s_rows[i];
            if (row.r.bottom < rcPaint.top || row.r.top > rcPaint.bottom)
                continue;

            if (row.kind == RW_HEADER)
            {
                DrawTextClip(dc, row.label, row.r, g_t.fBodySemi, g_t.textMain,
                             DT_LEFT | DT_SINGLELINE | DT_BOTTOM);
                continue;
            }

            /* card */
            FillRoundRect(dc, row.r, g_t.cardBg,
                          (hotRow == i) ? g_t.inputBorder : g_t.cardBorder, S(6));

            RECT lr = { row.r.left + S(16), row.r.top, row.rCtl.left - S(10), row.r.bottom };
            if (row.kind == RW_RADIO)
            {
                lr.left = row.r.left + S(46);
                lr.right = row.r.right - S(14);
            }
            DrawTextClip(dc, row.label, lr, g_t.fBody, g_t.textMain,
                         DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

            if (row.kind == RW_DROPDOWN)
            {
                const WCHAR* val = L"";
                if (row.id == SID_STARTPAGE)
                {
                    DWORD pg = g_app.st.startPage;
                    if (pg >= _countof(s_pageNames)) pg = 0;
                    val = s_pageNames[pg];
                }
                else
                {
                    DWORD sp = g_app.st.speed;
                    if (sp >= _countof(s_speedNames)) sp = SPD_NORMAL;
                    val = s_speedNames[sp];
                }
                FillRoundRect(dc, row.rCtl, g_t.inputBg, g_t.inputBorder, S(4));
                RECT vr = { row.rCtl.left + S(12), row.rCtl.top,
                            row.rCtl.right - S(26), row.rCtl.bottom };
                DrawTextClip(dc, val, vr, g_t.fBody, g_t.textMain,
                             DT_LEFT | DT_SINGLELINE | DT_VCENTER);
                RECT ar = { row.rCtl.right - S(24), row.rCtl.top + S(10),
                            row.rCtl.right - S(8), row.rCtl.bottom - S(10) };
                DrawGlyph(dc, ar, IC_CHEV_D, g_t.textSec);
            }
            else if (row.kind == RW_TOGGLE)
            {
                BOOL on = RowValue(row);
                DrawToggle(dc, row.rCtl, on, hotRow == i, TRUE);
                /* On/Off caption to the left of the switch */
                RECT sr = { row.rCtl.left - S(38), row.r.top, row.rCtl.left - S(6),
                            row.r.bottom };
                DrawTextClip(dc, on ? L"On" : L"Off", sr, g_t.fBody, g_t.textSec,
                             DT_RIGHT | DT_SINGLELINE | DT_VCENTER);
            }
            else if (row.kind == RW_RADIO)
            {
                DrawRadio(dc, row.rCtl, RowValue(row), hotRow == i);
            }
        }
    }

    void ApplyChanged(void)
    {
        Settings_Save();
        Frame_UpdateCommandStates();
    }

    void Click(int i)
    {
        SetRow& row = s_rows[i];
        switch (row.kind)
        {
        case RW_DROPDOWN:
        {
            if (row.id == SID_STARTPAGE)
            {
                MItem items[_countof(s_pageNames)];
                for (int k = 0; k < (int)_countof(s_pageNames); k++)
                {
                    items[k].id = 100 + k;
                    items[k].text = s_pageNames[k];
                    items[k].flags = (g_app.st.startPage == (DWORD)k) ? MIF_RADIO : 0;
                    items[k].sub = NULL;
                    items[k].nSub = 0;
                }
                UINT cmd = Menu_ShowBelow(hwnd, row.rCtl, items, _countof(items));
                if (cmd >= 100)
                {
                    g_app.st.startPage = cmd - 100;
                    ApplyChanged();
                }
            }
            else
            {
                MItem items[_countof(s_speedNames)];
                for (int k = 0; k < (int)_countof(s_speedNames); k++)
                {
                    items[k].id = 200 + k;
                    items[k].text = s_speedNames[k];
                    items[k].flags = (g_app.st.speed == (DWORD)k) ? MIF_RADIO : 0;
                    items[k].sub = NULL;
                    items[k].nSub = 0;
                }
                UINT cmd = Menu_ShowBelow(hwnd, row.rCtl, items, _countof(items));
                if (cmd >= 200)
                {
                    g_app.st.speed = cmd - 200;
                    ApplyChanged();
                    App_UpdateNow();   /* re-arm timer at new cadence */
                }
            }
            break;
        }
        case RW_TOGGLE:
            switch (row.id)
            {
            case SID_ONTOP:
                g_app.st.onTop = !g_app.st.onTop;
                SetWindowPos(g_app.hFrame,
                             g_app.st.onTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                break;
            case SID_MINONUSE:
                g_app.st.minOnUse = !g_app.st.minOnUse;
                break;
            case SID_HIDEMIN:
                g_app.st.hideWhenMin = !g_app.st.hideWhenMin;
                break;
            case SID_FULLACCT:
                g_app.st.fullAcctName = !g_app.st.fullAcctName;
                break;
            }
            ApplyChanged();
            break;
        case RW_RADIO:
        {
            DWORD want = (row.id == SID_TH_LIGHT) ? TM_LIGHT :
                         (row.id == SID_TH_DARK) ? TM_DARK : TM_SYSTEM;
            if (g_app.st.theme != want)
            {
                g_app.st.theme = want;
                Settings_Save();
                App_ApplyTheme();
            }
            break;
        }
        }
        InvalidateRect(hwnd, NULL, FALSE);
    }

    void OnThemeChanged() { InvalidateRect(hwnd, NULL, TRUE); }

    HWND Create(HWND parent);
};

static SettingsPage* s_page;

static LRESULT CALLBACK PgSetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    SettingsPage* pg = s_page;
    switch (msg)
    {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        BufPaint bp;
        HDC dc = bp.Begin(hdc, &ps.rcPaint);
        pg->Paint(dc, ps.rcPaint);
        bp.End();
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_SIZE:
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
    {
        RECT rc;
        GetClientRect(hwnd, &rc);
        int page = rc.bottom - rc.top;
        int maxY = pg->contentH - page;
        if (maxY < 0) maxY = 0;
        pg->scrollY -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * S(48);
        if (pg->scrollY < 0) pg->scrollY = 0;
        if (pg->scrollY > maxY) pg->scrollY = maxY;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int hot = -1;
        for (int i = 0; i < (int)_countof(s_rows); i++)
        {
            if (s_rows[i].kind != RW_HEADER && PtInRect(&s_rows[i].r, pt))
            {
                hot = i;
                break;
            }
        }
        if (hot != pg->hotRow)
        {
            pg->hotRow = hot;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        pg->hotRow = -1;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        for (int i = 0; i < (int)_countof(s_rows); i++)
        {
            if (s_rows[i].kind != RW_HEADER && PtInRect(&s_rows[i].r, pt))
            {
                pg->Click(i);
                break;
            }
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND SettingsPage::Create(HWND parent)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = PgSetProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PGO_CLASS;
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(0, PGO_CLASS, L"", WS_CHILD,
                           0, 0, 100, 100, parent, NULL, g_app.hInst, NULL);
    return hwnd;
}

Page* CreateSettingsPage(void)
{
    s_page = new SettingsPage();
    return s_page;
}
