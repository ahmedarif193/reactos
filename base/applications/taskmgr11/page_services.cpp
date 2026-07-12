/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Services page
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define PGS_CLASS L"TM11PageSvcs"

enum { SC_NAME = 0, SC_PID, SC_DESC, SC_STATUS, SC_GROUP };

struct ServicesPage : Page, ITreeListOwner
{
    HWND tl;
    int  sortCol;
    BOOL sortDesc;
    Vec<TLRow> rows;

    ServicesPage() : tl(NULL), sortCol(SC_NAME), sortDesc(FALSE) {}

    const WCHAR* Title() { return L"Services"; }
    BOOL WantSearch() { return TRUE; }
    const WCHAR* SearchHint() { return L"Type a service name to search"; }

    static SvcRow* SR(LPARAM data) { return (SvcRow*)data; }

    BOOL MatchesSearch(const SvcRow& s)
    {
        if (!g_app.search[0]) return TRUE;
        WCHAR pid[16];
        StringCchPrintfW(pid, _countof(pid), L"%u", s.pid);
        return StrStrIW(s.name, g_app.search) || StrStrIW(s.disp, g_app.search) ||
               StrStrIW(s.group, g_app.search) || StrStrIW(pid, g_app.search);
    }

    struct SortCtx { int col; BOOL desc; };
    static SortCtx s_sc;

    static const WCHAR* StateName(DWORD st)
    {
        switch (st)
        {
        case SERVICE_RUNNING: return L"Running";
        case SERVICE_STOPPED: return L"Stopped";
        case SERVICE_PAUSED:  return L"Paused";
        case SERVICE_START_PENDING: return L"Starting";
        case SERVICE_STOP_PENDING:  return L"Stopping";
        default: return L"Unknown";
        }
    }

    static int __cdecl Cmp(const void* a, const void* b)
    {
        const SvcRow* sa = *(const SvcRow* const*)a;
        const SvcRow* sb = *(const SvcRow* const*)b;
        int d = 0;
        switch (s_sc.col)
        {
        case SC_PID:    d = (int)sa->pid - (int)sb->pid; break;
        case SC_DESC:   d = lstrcmpiW(sa->disp, sb->disp); break;
        case SC_STATUS: d = (int)sa->state - (int)sb->state; break;
        case SC_GROUP:  d = lstrcmpiW(sa->group, sb->group); break;
        default:        d = lstrcmpiW(sa->name, sb->name); break;
        }
        if (d == 0) d = lstrcmpiW(sa->name, sb->name);
        return s_sc.desc ? -d : d;
    }

    void Rebuild(void)
    {
        rows.Clear();
        Vec<SvcRow>& svcs = Data::Services();
        Vec<SvcRow*> list;
        for (int i = 0; i < svcs.n; i++)
            if (MatchesSearch(svcs[i]))
                list.Push(&svcs[i]);

        s_sc.col = sortCol;
        s_sc.desc = sortDesc;
        if (list.n > 1)
            qsort(list.p, list.n, sizeof(SvcRow*), Cmp);

        for (int i = 0; i < list.n; i++)
        {
            TLRow* r = rows.Add();
            if (!r) break;
            /* key by name hash for stability */
            LONGLONG k = 5381;
            for (const WCHAR* c = list[i]->name; *c; c++)
                k = k * 33 + *c;
            r->key = (k << 3) | 0;
            r->data = (LPARAM)list[i];
            r->kind = TLR_ITEM;
        }
        TL_SetRows(tl, rows.p, rows.n);
    }

    /* ---------- ITreeListOwner ---------- */

    void TLCellText(LPARAM data, int col, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        SvcRow* s = SR(data);
        if (!s) return;
        switch (col)
        {
        case SC_NAME:   StringCchCopyW(buf, cch, s->name); break;
        case SC_PID:
            if (s->pid) StringCchPrintfW(buf, cch, L"%u", s->pid);
            break;
        case SC_DESC:   StringCchCopyW(buf, cch, s->disp); break;
        case SC_STATUS: StringCchCopyW(buf, cch, StateName(s->state)); break;
        case SC_GROUP:  StringCchCopyW(buf, cch, s->group); break;
        }
    }

    IconId TLRowGlyph(LPARAM data)
    {
        (void)data;
        return IC_SERVICES;
    }

    void TLOnSort(int col, BOOL desc)
    {
        sortCol = col;
        sortDesc = desc;
        Rebuild();
    }

    void TLOnSelChanged(void) { Frame_UpdateCommandStates(); }

    void TLOnContext(LPARAM data, POINT scr)
    {
        SvcRow* s = SR(data);
        if (!s) return;
        BOOL running = (s->state == SERVICE_RUNNING);
        BOOL stopped = (s->state == SERVICE_STOPPED);

        MItem items[] =
        {
            { 1, L"Start", stopped ? 0u : MIF_DISABLED, NULL, 0 },
            { 2, L"Stop", running ? 0u : MIF_DISABLED, NULL, 0 },
            { 3, L"Restart", running ? 0u : MIF_DISABLED, NULL, 0 },
            { 0, NULL, MIF_SEP, NULL, 0 },
            { 4, L"Open Services", 0, NULL, 0 },
            { 5, L"Search online", 0, NULL, 0 },
            { 6, L"Go to details", s->pid ? 0u : MIF_DISABLED, NULL, 0 },
        };

        UINT cmd = Menu_Show(hwnd, scr, items, _countof(items));
        switch (cmd)
        {
        case 1: case 2: case 3:
        {
            int op = (cmd == 1) ? 1 : (cmd == 2) ? 0 : 2;
            if (!Data::SvcControl(s->name, op))
            {
                ConfirmOpts o = { L"Services",
                                  L"The operation could not be completed.\n"
                                  L"Access is denied or the service did not respond.",
                                  L"OK", NULL, NULL, NULL, FALSE };
                Dlg_Confirm(hwnd, o);
            }
            Data::RefreshServices();
            Rebuild();
            break;
        }
        case 4:
            ShellExecuteW(NULL, L"open", L"servman.exe", NULL, NULL, SW_SHOWNORMAL);
            break;
        case 5:
        {
            WCHAR q[256];
            StringCchPrintfW(q, _countof(q), L"%s %s", s->name, s->disp);
            Data::SearchOnline(q);
            break;
        }
        case 6:
            Frame_SwitchToDetails(s->pid);
            break;
        }
    }

    /* ---------- Page ---------- */

    void BuildCommands(BtnStrip& s)
    {
        s.Add(CMD_SVC_OPENMSC, L"Open Services", IC_SERVICES, BS_SUBTLE);
    }

    void OnCommand(int id)
    {
        if (id == CMD_SVC_OPENMSC)
            ShellExecuteW(NULL, L"open", L"servman.exe", NULL, NULL, SW_SHOWNORMAL);
    }

    void OnTick()
    {
        static int s_age = 0;
        if (++s_age >= 4)
        {
            s_age = 0;
            Data::RefreshServices();
        }
        Rebuild();
    }

    void OnSearch() { Rebuild(); }
    void OnThemeChanged() { TL_Refresh(tl); }

    HWND Create(HWND parent);
};

ServicesPage::SortCtx ServicesPage::s_sc;

static ServicesPage* s_page;

static LRESULT CALLBACK PgSvcProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
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

HWND ServicesPage::Create(HWND parent)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = PgSvcProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PGS_CLASS;
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(0, PGS_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN,
                           0, 0, 100, 100, parent, NULL, g_app.hInst, NULL);
    tl = TL_Create(hwnd, this, FALSE);

    TLColumn cols[5];
    ZeroMemory(cols, sizeof(cols));
    cols[0].id = SC_NAME;   StringCchCopyW(cols[0].title, 48, L"Name");
    cols[0].width = S(180); cols[0].minWidth = S(120);
    cols[1].id = SC_PID;    StringCchCopyW(cols[1].title, 48, L"PID");
    cols[1].width = S(70);  cols[1].minWidth = S(50); cols[1].flags = TLC_RIGHT;
    cols[2].id = SC_DESC;   StringCchCopyW(cols[2].title, 48, L"Description");
    cols[2].width = S(320); cols[2].minWidth = S(150);
    cols[3].id = SC_STATUS; StringCchCopyW(cols[3].title, 48, L"Status");
    cols[3].width = S(100); cols[3].minWidth = S(70);
    cols[4].id = SC_GROUP;  StringCchCopyW(cols[4].title, 48, L"Group");
    cols[4].width = S(140); cols[4].minWidth = S(80);
    TL_SetColumns(tl, cols, 5);
    TL_SetSort(tl, SC_NAME, FALSE);
    TL_SetEmptyText(tl, L"No matching services");

    Rebuild();
    return hwnd;
}

Page* CreateServicesPage(void)
{
    s_page = new ServicesPage();
    return s_page;
}
