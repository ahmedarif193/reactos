/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Startup apps page
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define PGT_CLASS L"TM11PageStartup"

enum { TC_NAME = 0, TC_PUB, TC_STATUS, TC_IMPACT };

struct StartupPage : Page, ITreeListOwner
{
    HWND tl;
    int  sortCol;
    BOOL sortDesc;
    Vec<TLRow> rows;

    StartupPage() : tl(NULL), sortCol(TC_NAME), sortDesc(FALSE) {}

    const WCHAR* Title() { return L"Startup apps"; }
    BOOL WantSearch() { return TRUE; }
    const WCHAR* SearchHint() { return L"Type a name or publisher to search"; }

    static StartupRow* ST(LPARAM data) { return (StartupRow*)data; }

    BOOL MatchesSearch(const StartupRow& s)
    {
        if (!g_app.search[0]) return TRUE;
        return StrStrIW(s.name, g_app.search) || StrStrIW(s.publisher, g_app.search);
    }

    struct SortCtx { int col; BOOL desc; };
    static SortCtx s_sc;

    static int __cdecl Cmp(const void* a, const void* b)
    {
        const StartupRow* sa = *(const StartupRow* const*)a;
        const StartupRow* sb = *(const StartupRow* const*)b;
        int d = 0;
        switch (s_sc.col)
        {
        case TC_PUB:    d = lstrcmpiW(sa->publisher, sb->publisher); break;
        case TC_STATUS: d = (int)sa->enabled - (int)sb->enabled; break;
        default:        d = lstrcmpiW(sa->name, sb->name); break;
        }
        if (d == 0) d = lstrcmpiW(sa->name, sb->name);
        return s_sc.desc ? -d : d;
    }

    void Rebuild(void)
    {
        rows.Clear();
        Vec<StartupRow>& items = Data::StartupItems();
        Vec<StartupRow*> list;
        for (int i = 0; i < items.n; i++)
            if (MatchesSearch(items[i]))
                list.Push(&items[i]);

        s_sc.col = sortCol;
        s_sc.desc = sortDesc;
        if (list.n > 1)
            qsort(list.p, list.n, sizeof(StartupRow*), Cmp);

        for (int i = 0; i < list.n; i++)
        {
            TLRow* r = rows.Add();
            if (!r) break;
            LONGLONG k = 5381;
            for (const WCHAR* c = list[i]->valueName; *c; c++)
                k = k * 33 + *c;
            r->key = (k << 3) | ((LONGLONG)list[i]->source & 3);
            r->data = (LPARAM)list[i];
            r->kind = TLR_ITEM;
        }
        TL_SetRows(tl, rows.p, rows.n);
    }

    /* ---------- ITreeListOwner ---------- */

    void TLCellText(LPARAM data, int col, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        StartupRow* s = ST(data);
        if (!s) return;
        switch (col)
        {
        case TC_NAME:   StringCchCopyW(buf, cch, s->name); break;
        case TC_PUB:    StringCchCopyW(buf, cch, s->publisher); break;
        case TC_STATUS: StringCchCopyW(buf, cch, s->enabled ? L"Enabled" : L"Disabled"); break;
        case TC_IMPACT: StringCchCopyW(buf, cch, s->enabled ? L"Not measured" : L"None"); break;
        }
    }

    int TLRowGlyph(LPARAM data)
    {
        (void)data;
        return IC_WINDOW;
    }

    void TLOnSort(int col, BOOL desc)
    {
        sortCol = col;
        sortDesc = desc;
        Rebuild();
    }

    void TLOnSelChanged(void) { Frame_UpdateCommandStates(); }

    void Toggle(StartupRow* s, BOOL enable)
    {
        if (!s) return;
        if (!Data::SetStartupEnabled(*s, enable))
        {
            ConfirmOpts o = { L"Startup apps",
                              L"The startup setting could not be changed.\n"
                              L"Access is denied.",
                              L"OK", NULL, NULL, NULL, FALSE };
            Dlg_Confirm(hwnd, o);
        }
        Data::RefreshStartup();
        Rebuild();
        Frame_UpdateCommandStates();
    }

    void TLOnContext(LPARAM data, POINT scr)
    {
        StartupRow* s = ST(data);
        if (!s) return;

        WCHAR exe[MAX_PATH] = L"";
        /* first token of command for file ops */
        {
            const WCHAR* c = s->command;
            int o = 0;
            if (*c == L'"')
            {
                c++;
                while (*c && *c != L'"' && o < MAX_PATH - 1) exe[o++] = *c++;
            }
            else
            {
                while (*c && *c != L' ' && o < MAX_PATH - 1) exe[o++] = *c++;
            }
            exe[o] = 0;
        }
        BOOL hasFile = exe[0] && GetFileAttributesW(exe) != INVALID_FILE_ATTRIBUTES;

        MItem items[] =
        {
            { 1, s->enabled ? L"Disable" : L"Enable", 0, NULL, 0 },
            { 0, NULL, MIF_SEP, NULL, 0 },
            { 2, L"Open file location", hasFile ? 0u : MIF_DISABLED, NULL, 0 },
            { 3, L"Search online", 0, NULL, 0 },
            { 4, L"Properties", hasFile ? 0u : MIF_DISABLED, NULL, 0 },
        };

        UINT cmd = Menu_Show(hwnd, scr, items, _countof(items));
        switch (cmd)
        {
        case 1:
            Toggle(s, !s->enabled);
            break;
        case 2:
        {
            WCHAR params[MAX_PATH + 16];
            StringCchPrintfW(params, _countof(params), L"/select,\"%s\"", exe);
            if ((UINT_PTR)ShellExecuteW(NULL, NULL, L"explorer.exe", params, NULL,
                                        SW_SHOWNORMAL) <= 32)
            {
                WCHAR dir[MAX_PATH];
                StringCchCopyW(dir, _countof(dir), exe);
                PathRemoveFileSpecW(dir);
                ShellExecuteW(NULL, NULL, dir, NULL, NULL, SW_SHOWNORMAL);
            }
            break;
        }
        case 3:
        {
            WCHAR q[300];
            StringCchPrintfW(q, _countof(q), L"%s %s", s->name, s->publisher);
            Data::SearchOnline(q);
            break;
        }
        case 4:
        {
            SHELLEXECUTEINFOW sei;
            ZeroMemory(&sei, sizeof(sei));
            sei.cbSize = sizeof(sei);
            sei.fMask = SEE_MASK_INVOKEIDLIST;
            sei.hwnd = g_app.hFrame;
            sei.lpVerb = L"properties";
            sei.lpFile = exe;
            sei.nShow = SW_SHOW;
            ShellExecuteExW(&sei);
            break;
        }
        }
    }

    /* ---------- Page ---------- */

    void BuildCommands(BtnStrip& s)
    {
        s.Add(CMD_STARTUP_ENABLE, L"Enable", IC_ENABLE, BS_OUTLINE, FALSE);
        s.Add(CMD_STARTUP_DISABLE, L"Disable", IC_DISABLE, BS_OUTLINE, FALSE);
    }

    void UpdateCommands(BtnStrip& s)
    {
        StartupRow* it = ST(TL_GetSelData(tl));
        s.SetEnabled(CMD_STARTUP_ENABLE, it && !it->enabled);
        s.SetEnabled(CMD_STARTUP_DISABLE, it && it->enabled);
    }

    void OnCommand(int id)
    {
        StartupRow* it = ST(TL_GetSelData(tl));
        if (id == CMD_STARTUP_ENABLE) Toggle(it, TRUE);
        if (id == CMD_STARTUP_DISABLE) Toggle(it, FALSE);
    }

    void OnShow(BOOL shown)
    {
        if (shown)
        {
            Data::RefreshStartup();
            Rebuild();
        }
    }

    void OnTick() { /* static list; no per-tick churn */ }
    void OnSearch() { Rebuild(); }
    void OnThemeChanged() { TL_Refresh(tl); }

    HWND Create(HWND parent);
};

StartupPage::SortCtx StartupPage::s_sc;

static StartupPage* s_page;

static LRESULT CALLBACK PgStartProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
        if (s_page && s_page->tl)
            MoveWindow(s_page->tl, 0, 0, LOWORD(lp), HIWORD(lp), TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        FillRect32(dc, ps.rcPaint, g_t.listBg);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND StartupPage::Create(HWND parent)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = PgStartProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PGT_CLASS;
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(0, PGT_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN,
                           0, 0, 100, 100, parent, NULL, g_app.hInst, NULL);
    tl = TL_Create(hwnd, this, FALSE);

    TLColumn cols[4];
    ZeroMemory(cols, sizeof(cols));
    cols[0].id = TC_NAME;   StringCchCopyW(cols[0].title, 48, L"Name");
    cols[0].width = S(280); cols[0].minWidth = S(160);
    cols[1].id = TC_PUB;    StringCchCopyW(cols[1].title, 48, L"Publisher");
    cols[1].width = S(220); cols[1].minWidth = S(120);
    cols[2].id = TC_STATUS; StringCchCopyW(cols[2].title, 48, L"Status");
    cols[2].width = S(110); cols[2].minWidth = S(80);
    cols[3].id = TC_IMPACT; StringCchCopyW(cols[3].title, 48, L"Startup impact");
    cols[3].width = S(140); cols[3].minWidth = S(90);
    TL_SetColumns(tl, cols, 4);
    TL_SetSort(tl, TC_NAME, FALSE);
    TL_SetEmptyText(tl, L"There are no startup apps to show");

    Rebuild();
    return hwnd;
}

Page* CreateStartupPage(void)
{
    s_page = new StartupPage();
    return s_page;
}
