/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Application frame: custom chrome, nav rail, header, pages
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define FRAME_CLASS L"TaskManager11Frame"
#define TRAY_ID 1
#define TIMER_TICK 1

AppState g_app;

static ULONG_PTR s_gdiplusToken;

/* pages */
static Page* s_pages[PG_COUNT];
static BtnStrip s_strip;
static HWND s_search;
static HICON s_trayIcon;
static BOOL s_trayAdded;
static int s_trayBar = -1;
static int s_trayPct = -1;

/* chrome state (the frame itself is the standard system one) */
static int  s_hotRail = -1;       /* index into rail items, 100 = hamburger */
static BOOL s_tracking;
static BOOL s_wasMinimized;
static BOOL s_smpDiag;
static ULONGLONG s_smpDiagOriginTick;
static ULONGLONG s_smpDiagTickStart;
static ULONGLONG s_smpDiagLastTick;
static ULONGLONG s_smpDiagSampleTick;
static ULONGLONG s_smpDiagLastPaintTick;
static ULONG s_smpDiagSequence;
static ULONG s_smpDiagPaintedSequence;

/* nav rail items */
struct RailItem { int page; IconId icon; const WCHAR* label; };
static const RailItem s_rail[] =
{
    { PG_PROCESSES,   IC_PROCESSES, L"Processes" },
    { PG_PERFORMANCE, IC_PERF,      L"Performance" },
    { PG_APPHISTORY,  IC_HISTORY,   L"App history" },
    { PG_STARTUP,     IC_STARTUP,   L"Startup apps" },
    { PG_USERS,       IC_USERS,     L"Users" },
    { PG_DETAILS,     IC_DETAILS,   L"Details" },
    { PG_SERVICES,    IC_SERVICES,  L"Services" },
    { PG_SENSORS,     IC_SENSORS,   L"Sensors" },
};

const WCHAR* PageTitle(int id)
{
    for (int i = 0; i < (int)_countof(s_rail); i++)
        if (s_rail[i].page == id) return s_rail[i].label;
    return L"";
}

/* ------------------------------------------------------------------ */
/*  Metrics                                                            */
/* ------------------------------------------------------------------ */

static int RailW(void)    { return g_app.st.navExpanded ? S(210) : S(48); }
static int HeaderH(void)  { return S(54); }

static int TextWidth(HWND hwnd, HFONT font, const WCHAR* text)
{
    SIZE size = { 0, 0 };
    HDC dc = GetDC(hwnd);
    HGDIOBJ old = SelectObject(dc, font);
    GetTextExtentPoint32W(dc, text, lstrlenW(text), &size);
    SelectObject(dc, old);
    ReleaseDC(hwnd, dc);
    return size.cx;
}

static void HamburgerRect(HWND hwnd, RECT* r)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    r->left = rc.left + S(6);
    r->top = rc.top + S(4);
    r->right = r->left + S(36);
    r->bottom = r->top + S(36);
}

static void RailItemRect(HWND hwnd, int i, RECT* r)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    int y = rc.top + S(46);
    if (i < (int)_countof(s_rail))
    {
        r->top = y + i * S(40);
        r->bottom = r->top + S(36);
    }
    else
    {
        /* settings pinned to bottom */
        r->bottom = rc.bottom - S(8);
        r->top = r->bottom - S(36);
    }
    r->left = rc.left + S(6);
    r->right = rc.left + RailW() - S(6);
}

static void HeaderRect(HWND hwnd, RECT* r)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    r->left = rc.left + RailW();
    r->top = rc.top;
    r->right = rc.right;
    r->bottom = r->top + HeaderH();
}

static void PageRect(HWND hwnd, RECT* r)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    r->left = rc.left + RailW();
    r->top = rc.top + HeaderH();
    r->right = rc.right - S(4);
    r->bottom = rc.bottom - S(4);
}

/* ------------------------------------------------------------------ */
/*  Settings                                                           */
/* ------------------------------------------------------------------ */

static const WCHAR* SETTINGS_KEY = L"Software\\ReactOS\\TaskMgr11";

static DWORD RegReadDw(HKEY hk, const WCHAR* name, DWORD def)
{
    DWORD v = def, cb = sizeof(v);
    if (RegQueryValueExW(hk, name, NULL, NULL, (LPBYTE)&v, &cb) != ERROR_SUCCESS)
        return def;
    return v;
}

void Settings_Load(void)
{
    Settings& st = g_app.st;
    ZeroMemory(&st, sizeof(st));
    st.theme = TM_DARK;
    st.startPage = PG_PROCESSES;
    st.speed = SPD_NORMAL;
    st.navExpanded = TRUE;

    HKEY hk;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, KEY_QUERY_VALUE, &hk)
        == ERROR_SUCCESS)
    {
        st.theme = RegReadDw(hk, L"Theme", st.theme);
        st.startPage = RegReadDw(hk, L"StartPage", st.startPage);
        st.speed = RegReadDw(hk, L"UpdateSpeed", st.speed);
        st.onTop = RegReadDw(hk, L"AlwaysOnTop", 0);
        st.minOnUse = RegReadDw(hk, L"MinimizeOnUse", 0);
        st.hideWhenMin = RegReadDw(hk, L"HideWhenMinimized", 0);
        st.navExpanded = RegReadDw(hk, L"NavExpanded", 1);
        st.noEffPrompt = RegReadDw(hk, L"NoEfficiencyPrompt", 0);
        st.fullAcctName = RegReadDw(hk, L"FullAccountName", 0);

        DWORD cb = sizeof(st.wp);
        if (RegQueryValueExW(hk, L"Placement", NULL, NULL, (LPBYTE)&st.wp, &cb)
            != ERROR_SUCCESS || cb != sizeof(st.wp))
            st.wp.length = 0;
        RegCloseKey(hk);
    }
    if (st.theme > TM_DARK) st.theme = TM_DARK;
    if (st.startPage >= PG_SETTINGS) st.startPage = PG_PROCESSES;
    if (st.speed > SPD_PAUSED) st.speed = SPD_NORMAL;
}

void Settings_Save(void)
{
    Settings& st = g_app.st;
    HKEY hk;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, SETTINGS_KEY, 0, NULL, 0, KEY_SET_VALUE,
                        NULL, &hk, NULL) != ERROR_SUCCESS)
        return;
#define WR(name, val) { DWORD v = (DWORD)(val); \
    RegSetValueExW(hk, name, 0, REG_DWORD, (LPBYTE)&v, sizeof(v)); }
    WR(L"Theme", st.theme);
    WR(L"StartPage", st.startPage);
    WR(L"UpdateSpeed", st.speed);
    WR(L"AlwaysOnTop", st.onTop);
    WR(L"MinimizeOnUse", st.minOnUse);
    WR(L"HideWhenMinimized", st.hideWhenMin);
    WR(L"NavExpanded", st.navExpanded);
    WR(L"NoEfficiencyPrompt", st.noEffPrompt);
    WR(L"FullAccountName", st.fullAcctName);
#undef WR
    if (g_app.hFrame)
    {
        WINDOWPLACEMENT wp;
        wp.length = sizeof(wp);
        if (GetWindowPlacement(g_app.hFrame, &wp))
            RegSetValueExW(hk, L"Placement", 0, REG_BINARY, (LPBYTE)&wp, sizeof(wp));
    }
    RegCloseKey(hk);
}

UINT App_TimerMs(void)
{
    switch (g_app.st.speed)
    {
    case SPD_HIGH:   return 1000;
    case SPD_NORMAL: return 1000;
    case SPD_LOW:    return 4000;
    default:         return 0;
    }
}

static UINT EffectiveTimerMs(HWND hwnd)
{
    UINT milliseconds = App_TimerMs();
    if (milliseconds && (!IsWindowVisible(hwnd) || IsIconic(hwnd)) &&
        milliseconds < 2000)
        milliseconds = 2000;
    return milliseconds;
}

static void RearmTimer(HWND hwnd)
{
    KillTimer(hwnd, TIMER_TICK);
    UINT milliseconds = EffectiveTimerMs(hwnd);
    if (milliseconds)
        SetTimer(hwnd, TIMER_TICK, milliseconds, NULL);
}

static void SmpDiagInitialize(void)
{
    CHAR Buffer[128];

    s_smpDiagOriginTick = GetTickCount64();
    StringCchPrintfA(Buffer, _countof(Buffer), "TASKMGR11_DIAG_BEGIN period_ms=1000 tick_ms=%I64u page=performance graph=logical\n", s_smpDiagOriginTick);
    OutputDebugStringA(Buffer);
}

static void SmpDiagTickBegin(void)
{
    if (s_smpDiag)
        s_smpDiagTickStart = GetTickCount64();
}

static void SmpDiagTickEnd(void)
{
    ULONGLONG Now;
    ULONGLONG AtMs, DeltaMs, LateMs, QueryMs;
    CHAR Buffer[256];

    if (!s_smpDiag || !s_smpDiagTickStart)
        return;

    Now = GetTickCount64();
    AtMs = s_smpDiagTickStart - s_smpDiagOriginTick;
    DeltaMs = s_smpDiagLastTick ? s_smpDiagTickStart - s_smpDiagLastTick : 0;
    LateMs = DeltaMs > App_TimerMs() ? DeltaMs - App_TimerMs() : 0;
    QueryMs = Now - s_smpDiagTickStart;
    s_smpDiagSequence++;
    s_smpDiagSampleTick = Now;
    s_smpDiagLastTick = s_smpDiagTickStart;
    StringCchPrintfA(Buffer, _countof(Buffer), "TASKMGR11_SAMPLE seq=%lu tick_ms=%I64u at_ms=%I64u delta_ms=%I64u late_ms=%I64u query_ms=%I64u cpu_pct=%.1f\n", s_smpDiagSequence, s_smpDiagTickStart, AtMs, DeltaMs, LateMs, QueryMs, Data::g.cpuTotalPct);
    OutputDebugStringA(Buffer);
}

void App_SmpDiagGraphPaint(void)
{
    ULONGLONG Now;
    ULONGLONG AtMs, DeltaMs, SampleToPaintMs;
    float Cpu0, Cpu1, Cpu2, Cpu3;
    CHAR Buffer[384];

    if (!s_smpDiag || !s_smpDiagSequence || (s_smpDiagPaintedSequence == s_smpDiagSequence))
        return;

    Now = GetTickCount64();
    AtMs = Now - s_smpDiagOriginTick;
    DeltaMs = s_smpDiagLastPaintTick ? Now - s_smpDiagLastPaintTick : 0;
    SampleToPaintMs = Now - s_smpDiagSampleTick;
    Cpu0 = Data::g.nCpu > 0 ? Data::g.hCpuLogical[0].Last() : 0.0f;
    Cpu1 = Data::g.nCpu > 1 ? Data::g.hCpuLogical[1].Last() : 0.0f;
    Cpu2 = Data::g.nCpu > 2 ? Data::g.hCpuLogical[2].Last() : 0.0f;
    Cpu3 = Data::g.nCpu > 3 ? Data::g.hCpuLogical[3].Last() : 0.0f;
    StringCchPrintfA(Buffer, _countof(Buffer), "TASKMGR11_GRAPH seq=%lu tick_ms=%I64u at_ms=%I64u delta_ms=%I64u sample_to_paint_ms=%I64u cpu_pct=%.1f cpu0_pct=%.1f cpu1_pct=%.1f cpu2_pct=%.1f cpu3_pct=%.1f\n", s_smpDiagSequence, Now, AtMs, DeltaMs, SampleToPaintMs, Data::g.cpuTotalPct, Cpu0, Cpu1, Cpu2, Cpu3);
    OutputDebugStringA(Buffer);
    s_smpDiagPaintedSequence = s_smpDiagSequence;
    s_smpDiagLastPaintTick = Now;
}

/* ------------------------------------------------------------------ */
/*  Tray icon                                                          */
/* ------------------------------------------------------------------ */

static HICON MakeTrayIcon(int barHeight)
{
    HDC scr = GetDC(NULL);
    if (!scr) return NULL;
    HDC dc = CreateCompatibleDC(scr);
    HBITMAP color = CreateCompatibleBitmap(scr, 16, 16);
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, NULL);
    if (!dc || !color || !mask)
    {
        if (color) DeleteObject(color);
        if (mask) DeleteObject(mask);
        if (dc) DeleteDC(dc);
        ReleaseDC(NULL, scr);
        return NULL;
    }
    HGDIOBJ old = SelectObject(dc, color);

    RECT r = { 0, 0, 16, 16 };
    FillRect32(dc, r, RGB(0x0A, 0x0A, 0x0A));
    RECT frame = { 0, 0, 16, 16 };
    FrameRect(dc, &frame, (HBRUSH)GetStockObject(DKGRAY_BRUSH));
    RECT bar = { 3, 14 - barHeight, 13, 14 };
    FillRect32(dc, bar, RGB(0x30, 0xC0, 0x40));

    SelectObject(dc, mask);
    RECT mr = { 0, 0, 16, 16 };
    FillRect(dc, &mr, (HBRUSH)GetStockObject(BLACK_BRUSH));

    SelectObject(dc, old);
    ICONINFO ii = { TRUE, 0, 0, mask, color };
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    DeleteDC(dc);
    ReleaseDC(NULL, scr);
    return icon;
}

static void UpdateTray(HWND hwnd)
{
    int cpuPct = (int)(Data::g.cpuTotalPct + 0.5);
    if (cpuPct < 0) cpuPct = 0;
    if (cpuPct > 100) cpuPct = 100;
    int barHeight = MulDiv(cpuPct, 12, 100);
    BOOL updateIcon = !s_trayAdded || barHeight != s_trayBar;
    BOOL updateTip = !s_trayAdded || cpuPct != s_trayPct;
    if (!updateIcon && !updateTip)
        return;

    NOTIFYICONDATAW nid;
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.hWnd = hwnd;
    nid.uID = TRAY_ID;
    if (!s_trayAdded)
    {
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_APP_TRAYICON;
    }
    else
    {
        if (updateIcon) nid.uFlags |= NIF_ICON;
        if (updateTip) nid.uFlags |= NIF_TIP;
    }

    HICON icon = NULL;
    if (updateIcon)
    {
        icon = MakeTrayIcon(barHeight);
        if (!icon) return;
        nid.hIcon = icon;
    }
    if (updateTip)
        StringCchPrintfW(nid.szTip, _countof(nid.szTip),
                         L"CPU Usage: %d%%", cpuPct);

    BOOL updated = Shell_NotifyIconW(s_trayAdded ? NIM_MODIFY : NIM_ADD, &nid);
    if (!updated)
    {
        if (icon) DestroyIcon(icon);
        return;
    }

    s_trayAdded = TRUE;
    if (updateIcon)
    {
        if (s_trayIcon) DestroyIcon(s_trayIcon);
        s_trayIcon = icon;
        s_trayBar = barHeight;
    }
    if (updateTip)
        s_trayPct = cpuPct;
}

static void RemoveTray(HWND hwnd)
{
    if (s_trayAdded)
    {
        NOTIFYICONDATAW nid;
        ZeroMemory(&nid, sizeof(nid));
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = TRAY_ID;
        Shell_NotifyIconW(NIM_DELETE, &nid);
        s_trayAdded = FALSE;
    }
    if (s_trayIcon)
    {
        DestroyIcon(s_trayIcon);
        s_trayIcon = NULL;
    }
    s_trayBar = -1;
    s_trayPct = -1;
}

/* ------------------------------------------------------------------ */
/*  Page plumbing                                                      */
/* ------------------------------------------------------------------ */

static Page* GetPage(int id)
{
    if (id < 0 || id >= PG_COUNT) return NULL;
    if (!s_pages[id])
    {
        switch (id)
        {
        case PG_PROCESSES:   s_pages[id] = CreateProcessesPage(); break;
        case PG_PERFORMANCE: s_pages[id] = CreatePerformancePage(); break;
        case PG_APPHISTORY:  s_pages[id] = CreateAppHistoryPage(); break;
        case PG_STARTUP:     s_pages[id] = CreateStartupPage(); break;
        case PG_USERS:       s_pages[id] = CreateUsersPage(); break;
        case PG_DETAILS:     s_pages[id] = CreateDetailsPage(); break;
        case PG_SERVICES:    s_pages[id] = CreateServicesPage(); break;
        case PG_SENSORS:     s_pages[id] = CreateSensorsPage(); break;
        case PG_SETTINGS:    s_pages[id] = CreateSettingsPage(); break;
        }
        if (s_pages[id])
        {
            s_pages[id]->Create(g_app.hFrame);
            RECT pr;
            PageRect(g_app.hFrame, &pr);
            MoveWindow(s_pages[id]->hwnd, pr.left, pr.top,
                       pr.right - pr.left, pr.bottom - pr.top, TRUE);
        }
    }
    return s_pages[id];
}

void Frame_UpdateCommandStates(void)
{
    Page* pg = s_pages[g_app.page];
    if (pg)
        pg->UpdateCommands(s_strip);
}

static void LayoutChildren(HWND hwnd)
{
    RECT pr;
    PageRect(hwnd, &pr);
    for (int i = 0; i < PG_COUNT; i++)
    {
        if (s_pages[i] && s_pages[i]->hwnd)
            MoveWindow(s_pages[i]->hwnd, pr.left, pr.top,
                       pr.right - pr.left, pr.bottom - pr.top, TRUE);
    }

    RECT hr;
    HeaderRect(hwnd, &hr);

    /* command strip lives at header right */
    RECT band = { hr.left, hr.top + S(6), hr.right - S(14), hr.top + S(6) + S(40) };
    int stripWidth = s_strip.LayoutRight(band, S(8));
    int stripLeft = band.right - stripWidth;

    /* fit the search box between the measured title and command strip */
    Page* pg = s_pages[g_app.page];
    if (s_search)
    {
        if (pg && pg->WantSearch())
        {
            int titleLeft = hr.left + S(16);
            int availL = titleLeft + TextWidth(hwnd, g_t.fTitle, pg->Title()) + S(24);
            int availR = stripLeft - S(8);
            int available = availR - availL;

            if (available > S(120))
            {
                int w = available < S(320) ? available : S(320);
                int x = (hr.left + hr.right - w) / 2;
                if (x < availL) x = availL;
                if (x + w > availR) x = availR - w;
                MoveWindow(s_search, x, hr.top + S(11), w, S(32), TRUE);
                ShowWindow(s_search, SW_SHOW);
            }
            else
            {
                ShowWindow(s_search, SW_HIDE);
            }
        }
        else
        {
            ShowWindow(s_search, SW_HIDE);
        }
    }
}

void App_SetPage(int id)
{
    if (id < 0 || id >= PG_COUNT) return;

    Page* old = s_pages[g_app.page];
    Page* nw = GetPage(id);
    if (!nw) return;

    if (old && old->hwnd && old != nw)
    {
        old->OnShow(FALSE);
        ShowWindow(old->hwnd, SW_HIDE);
    }

    g_app.page = id;

    /* rebuild commands */
    s_strip.Clear();
    s_strip.hwnd = g_app.hFrame;
    nw->BuildCommands(s_strip);
    nw->UpdateCommands(s_strip);

    /* search */
    g_app.search[0] = 0;
    if (s_search)
    {
        Search_Clear(s_search);
        Search_SetPlaceholder(s_search, nw->SearchHint());
    }

    LayoutChildren(g_app.hFrame);
    ShowWindow(nw->hwnd, SW_SHOW);
    nw->OnShow(TRUE);
    nw->OnTick();
    InvalidateRect(g_app.hFrame, NULL, FALSE);
}

void Frame_SwitchToDetails(ULONG pid)
{
    GetPage(PG_DETAILS);            /* ensure created so select works */
    App_SetPage(PG_DETAILS);
    DetailsPage_SelectPid(pid);
}

void Frame_SwitchToServices(void)
{
    App_SetPage(PG_SERVICES);
}

static void DoTick(HWND hwnd)
{
    SmpDiagTickBegin();
    Data::Tick();
    SmpDiagTickEnd();
    BOOL visible = IsWindowVisible(hwnd) && !IsIconic(hwnd);
    if (visible)
    {
        Page* pg = s_pages[g_app.page];
        if (pg)
            pg->OnTick();
        Frame_UpdateCommandStates();
    }
    UpdateTray(hwnd);

}

void App_UpdateNow(void)
{
    HWND hwnd = g_app.hFrame;
    KillTimer(hwnd, TIMER_TICK);
    DoTick(hwnd);
    RearmTimer(hwnd);
}

void App_ApplyTheme(void)
{
    BOOL dark;
    switch (g_app.st.theme)
    {
    case TM_LIGHT: dark = FALSE; break;
    case TM_DARK:  dark = TRUE; break;
    default:       dark = Theme_SystemPrefersDark(); break;
    }
    Theme_Apply(dark, g_app.dpi);

    if (s_search)
        SendMessageW(s_search, WM_APP_THEMECHG, 0, 0);
    for (int i = 0; i < PG_COUNT; i++)
    {
        if (s_pages[i])
        {
            s_pages[i]->OnThemeChanged();
            if (s_pages[i]->hwnd)
                InvalidateRect(s_pages[i]->hwnd, NULL, TRUE);
        }
    }
    if (g_app.hFrame)
        InvalidateRect(g_app.hFrame, NULL, TRUE);
}

/* ------------------------------------------------------------------ */
/*  Frame painting                                                     */
/* ------------------------------------------------------------------ */

static void PaintFrame(HWND hwnd, HDC dc, const RECT& rcPaint)
{
    RECT rc;
    GetClientRect(hwnd, &rc);
    FillRect32(dc, rcPaint, g_t.winBg);

    /* ---- nav rail ---- */
    RECT nav = { rc.left, rc.top, rc.left + RailW(), rc.bottom };
    RECT dirty;
    if (IntersectRect(&dirty, &nav, &rcPaint))
    {
        RECT ham;
        HamburgerRect(hwnd, &ham);
        if (s_hotRail == 100)
            FillRoundRect(dc, ham, g_t.railHover, CLR_NONE, S(4));
        RECT hg = { ham.left + S(9), ham.top + S(9), ham.right - S(9), ham.bottom - S(9) };
        DrawGlyph(dc, hg, IC_HAMBURGER, g_t.textMain);

        int nItems = (int)_countof(s_rail) + 1;   /* + settings */
        for (int i = 0; i < nItems; i++)
        {
            RECT ir;
            RailItemRect(hwnd, i, &ir);
            BOOL isSettings = (i == (int)_countof(s_rail));
            int page = isSettings ? PG_SETTINGS : s_rail[i].page;
            IconId icon = isSettings ? IC_SETTINGS : s_rail[i].icon;
            const WCHAR* label = isSettings ? L"Settings" : s_rail[i].label;
            BOOL sel = (g_app.page == page);

            if (sel)
                FillRoundRect(dc, ir, g_t.railSel, CLR_NONE, S(4));
            else if (s_hotRail == i)
                FillRoundRect(dc, ir, g_t.railHover, CLR_NONE, S(4));

            if (sel)
            {
                RECT bar = { ir.left, (ir.top + ir.bottom) / 2 - S(8),
                             ir.left + S(3), (ir.top + ir.bottom) / 2 + S(8) };
                FillRoundRect(dc, bar, g_t.accent, CLR_NONE, S(2));
            }

            RECT gi = { ir.left + S(11), (ir.top + ir.bottom) / 2 - S(8),
                        ir.left + S(27), (ir.top + ir.bottom) / 2 + S(8) };
            DrawGlyph(dc, gi, icon, sel ? g_t.textMain : g_t.textSec);

            if (g_app.st.navExpanded)
            {
                RECT lr = { ir.left + S(38), ir.top, ir.right - S(6), ir.bottom };
                DrawTextClip(dc, label, lr, sel ? g_t.fBodySemi : g_t.fBody,
                             g_t.textMain, DT_LEFT | DT_SINGLELINE | DT_VCENTER);
            }
        }
    }

    /* ---- header ---- */
    RECT hr;
    HeaderRect(hwnd, &hr);
    if (IntersectRect(&dirty, &hr, &rcPaint))
    {
        Page* pg = s_pages[g_app.page];
        if (pg)
        {
            int right = hr.right - S(14);
            for (int i = 0; i < s_strip.b.n; i++)
                if (s_strip.b[i].r.left - S(12) < right)
                    right = s_strip.b[i].r.left - S(12);
            if (s_search && IsWindowVisible(s_search))
            {
                RECT sr;
                GetWindowRect(s_search, &sr);
                MapWindowPoints(NULL, hwnd, (POINT*)&sr, 2);
                if (sr.left - S(12) < right)
                    right = sr.left - S(12);
            }

            RECT tr = { hr.left + S(16), hr.top, right, hr.bottom };
            if (tr.right > tr.left)
                DrawTextClip(dc, pg->Title(), tr, g_t.fTitle, g_t.textMain,
                             DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        }
        s_strip.Paint(dc);
    }
}

/* ------------------------------------------------------------------ */
/*  Frame window proc                                                  */
/* ------------------------------------------------------------------ */

static int RailHit(HWND hwnd, POINT pt)
{
    if (pt.x >= RailW()) return -1;
    RECT ham;
    HamburgerRect(hwnd, &ham);
    if (PtInRect(&ham, pt)) return 100;
    for (int i = 0; i <= (int)_countof(s_rail); i++)
    {
        RECT ir;
        RailItemRect(hwnd, i, &ir);
        if (PtInRect(&ir, pt)) return i;
    }
    return -1;
}

static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
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
        PaintFrame(hwnd, dc, ps.rcPaint);
        bp.End();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
    {
        if (wp == SIZE_MINIMIZED)
        {
            s_wasMinimized = TRUE;
            RearmTimer(hwnd);
            if (g_app.st.hideWhenMin)
                ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        BOOL restored = s_wasMinimized;
        s_wasMinimized = FALSE;
        g_app.maximized = (wp == SIZE_MAXIMIZED);
        LayoutChildren(hwnd);
        if (restored)
        {
            Page* pg = s_pages[g_app.page];
            if (pg) pg->OnTick();
            Frame_UpdateCommandStates();
            RearmTimer(hwnd);
        }
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_GETMINMAXINFO:
    {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = S(720);
        mmi->ptMinTrackSize.y = S(480);
        return 0;
    }

    case WM_ACTIVATE:
        g_app.active = (LOWORD(wp) != WA_INACTIVE);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_MOUSEMOVE:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (!s_tracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            s_tracking = TRUE;
        }
        int oldRail = s_hotRail;
        s_hotRail = RailHit(hwnd, pt);
        if (oldRail != s_hotRail)
            InvalidateRect(hwnd, NULL, FALSE);
        s_strip.OnMouse(msg, pt);
        return 0;
    }

    case WM_MOUSELEAVE:
        s_tracking = FALSE;
        if (s_hotRail != -1)
        {
            s_hotRail = -1;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        s_strip.ClearHot();
        return 0;

    case WM_LBUTTONDOWN:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int rail = RailHit(hwnd, pt);
        if (rail >= 0)
        {
            if (rail == 100)
            {
                g_app.st.navExpanded = !g_app.st.navExpanded;
                Settings_Save();
                LayoutChildren(hwnd);
                InvalidateRect(hwnd, NULL, TRUE);
            }
            else if (rail == (int)_countof(s_rail))
            {
                App_SetPage(PG_SETTINGS);
            }
            else
            {
                App_SetPage(s_rail[rail].page);
            }
            return 0;
        }
        s_strip.OnMouse(msg, pt);
        return 0;
    }

    case WM_LBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        int cmd = s_strip.OnMouse(msg, pt);
        if (cmd)
        {
            Page* pg = s_pages[g_app.page];
            if (pg) pg->OnCommand(cmd);

            if (cmd == CMD_ENDTASK && g_app.st.minOnUse)
                ShowWindow(hwnd, SW_MINIMIZE);
        }
        return 0;
    }

    case WM_TIMER:
        if (wp == TIMER_TICK)
            DoTick(hwnd);
        return 0;

    case WM_APP_SEARCH:
    {
        WCHAR text[128];
        Search_GetText(s_search, text, _countof(text));
        StringCchCopyW(g_app.search, _countof(g_app.search), text);
        Page* pg = s_pages[g_app.page];
        if (pg) pg->OnSearch();
        return 0;
    }

    case WM_APP_SELCHANGED:
        Frame_UpdateCommandStates();
        return 0;

    case WM_APP_TRAYICON:
        if (lp == WM_LBUTTONDBLCLK)
        {
            ShowWindow(hwnd, SW_RESTORE);
            SetForegroundWindow(hwnd);
        }
        else if (lp == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            MItem items[] =
            {
                { 1, L"Restore", MIF_DEFAULT, NULL, 0 },
                { 0, NULL, MIF_SEP, NULL, 0 },
                { 2, L"Always on top", g_app.st.onTop ? MIF_CHECKED : 0u, NULL, 0 },
                { 0, NULL, MIF_SEP, NULL, 0 },
                { 3, L"Exit", 0, NULL, 0 },
            };
            UINT cmd = Menu_Show(hwnd, pt, items, _countof(items));
            switch (cmd)
            {
            case 1:
                ShowWindow(hwnd, SW_RESTORE);
                SetForegroundWindow(hwnd);
                break;
            case 2:
                g_app.st.onTop = !g_app.st.onTop;
                SetWindowPos(hwnd, g_app.st.onTop ? HWND_TOPMOST : HWND_NOTOPMOST,
                             0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
                Settings_Save();
                break;
            case 3:
                PostMessageW(hwnd, WM_CLOSE, 0, 0);
                break;
            }
        }
        return 0;

    case WM_KEYDOWN:
        if (wp == VK_F5)
        {
            App_UpdateNow();
            return 0;
        }
        return 0;

    case WM_SETTINGCHANGE:
        if (g_app.st.theme == TM_SYSTEM)
            App_ApplyTheme();
        return 0;

    case WM_CLOSE:
        Settings_Save();
        RemoveTray(hwnd);
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/*  Entry                                                              */
/* ------------------------------------------------------------------ */

typedef BOOL (WINAPI *PFN_SetProcessDPIAware)(void);

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow)
{
    (void)hPrev;

    /* single instance */
    HANDLE mutex = CreateMutexW(NULL, TRUE, L"Local\\ROS_TaskMgr11");
    if (mutex && GetLastError() == ERROR_ALREADY_EXISTS)
    {
        HWND prev = FindWindowW(FRAME_CLASS, NULL);
        if (prev)
        {
            ShowWindow(prev, SW_RESTORE);
            SetForegroundWindow(prev);
        }
        return 0;
    }

    g_app.hInst = hInstance;

    /* Match Windows Task Manager's process priority. */
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);

    /* dpi awareness (dynamic; ancient user32 may lack it) */
    {
        PFN_SetProcessDPIAware pAware = (PFN_SetProcessDPIAware)
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDPIAware");
        if (pAware) pAware();
        HDC dc = GetDC(NULL);
        g_app.dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
        if (g_app.dpi < 96) g_app.dpi = 96;
    }

    CoInitialize(NULL);
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    Gdiplus::GdiplusStartupInput gsi;
    Gdiplus::GdiplusStartup(&s_gdiplusToken, &gsi, NULL);

    Settings_Load();
    s_smpDiag = StrStrIW(lpCmdLine, L"/smpdiag") != NULL;
    if (s_smpDiag)
    {
        g_app.st.startPage = PG_PERFORMANCE;
        g_app.st.speed = SPD_HIGH;
        SmpDiagInitialize();
    }
    App_ApplyTheme();

    g_app.hAppIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_TASKMGR11),
                                       IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
    g_app.hAppIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_TASKMGR11),
                                         IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);

    Data::Init();

    /* window classes */
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = FrameProc;
    wc.hInstance = hInstance;
    wc.hIcon = g_app.hAppIcon;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = FRAME_CLASS;
    RegisterClassW(&wc);
    TL_Register();
    Search_Register();

    int w = S(1000), h = S(660);
    int x = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;

    g_app.hFrame = CreateWindowExW(
        g_app.st.onTop ? WS_EX_TOPMOST : 0,
        FRAME_CLASS, L"Task Manager",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        x, y, w, h, NULL, NULL, hInstance, NULL);
    if (!g_app.hFrame)
        return 1;

    s_search = Search_Create(g_app.hFrame, L"Type a name, publisher, or PID to search");

    /* restore placement */
    if (g_app.st.wp.length == sizeof(WINDOWPLACEMENT))
    {
        g_app.st.wp.showCmd = SW_HIDE;
        SetWindowPlacement(g_app.hFrame, &g_app.st.wp);
    }

    g_app.page = -1;
    int startPage = (int)g_app.st.startPage;
    if (startPage < 0 || startPage >= PG_SETTINGS) startPage = PG_PROCESSES;
    g_app.page = startPage;
    GetPage(startPage);
    App_SetPage(startPage);

    ShowWindow(g_app.hFrame, (nCmdShow == SW_SHOWDEFAULT) ? SW_SHOW : nCmdShow);
    UpdateWindow(g_app.hFrame);

    DoTick(g_app.hFrame);
    RearmTimer(g_app.hFrame);

    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0))
    {
        /* app-wide keys */
        if (msg.message == WM_KEYDOWN)
        {
            if (msg.wParam == VK_F5)
            {
                App_UpdateNow();
                continue;
            }
            if (msg.wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))
            {
                if (s_search && IsWindowVisible(s_search))
                {
                    Search_Focus(s_search);
                    continue;
                }
            }
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    RemoveTray(g_app.hFrame);
    Data::Shutdown();
    Theme_Free();
    Gdiplus::GdiplusShutdown(s_gdiplusToken);
    CoUninitialize();
    if (mutex) CloseHandle(mutex);
    return (int)msg.wParam;
}
