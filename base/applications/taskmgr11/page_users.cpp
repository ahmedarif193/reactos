/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Users page (sessions with expandable process lists)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define PGU_CLASS L"TM11PageUsers"

enum { UC_USER = 0, UC_STATUS, UC_CPU, UC_MEM };

struct UsersPage : Page, ITreeListOwner
{
    HWND tl;
    Vec<TLRow> rows;
    Vec<DWORD> expanded;      /* session ids expanded */

    UsersPage() : tl(NULL) {}

    const WCHAR* Title() { return L"Users"; }

    BOOL IsExpanded(DWORD sid)
    {
        for (int i = 0; i < expanded.n; i++)
            if (expanded[i] == sid) return TRUE;
        return FALSE;
    }

    void SetExpanded(DWORD sid, BOOL e)
    {
        for (int i = 0; i < expanded.n; i++)
        {
            if (expanded[i] == sid)
            {
                if (!e) expanded.RemoveAt(i);
                return;
            }
        }
        if (e) expanded.Push(sid);
    }

    /* users are tagged with low bit (they're indices, not pointers) */
    static LPARAM UserData(int idx) { return (LPARAM)(((DWORD_PTR)idx << 3) | 1); }
    static int UserIdx(LPARAM d) { return (d & 1) ? (int)((DWORD_PTR)d >> 3) : -1; }
    static ProcRow* PR(LPARAM d) { return (d & 1) ? NULL : (ProcRow*)d; }

    UserRow* User(LPARAM d)
    {
        int i = UserIdx(d);
        Vec<UserRow>& us = Data::Users();
        return (i >= 0 && i < us.n) ? &us[i] : NULL;
    }

    void Rebuild(void)
    {
        rows.Clear();
        Vec<UserRow>& us = Data::Users();
        for (int u = 0; u < us.n; u++)
        {
            TLRow* r = rows.Add();
            if (!r) break;
            r->key = ((LONGLONG)us[u].sessionId << 3) | 1;
            r->data = UserData(u);
            r->kind = TLR_ITEM;
            r->expandable = TRUE;
            r->expanded = IsExpanded(us[u].sessionId);

            if (r->expanded)
            {
                for (int i = 0; i < Data::g.procs.n; i++)
                {
                    ProcRow& p = Data::g.procs[i];
                    if (p.pid <= 4) continue;
                    if (p.sessionId != us[u].sessionId) continue;
                    TLRow* c = rows.Add();
                    if (!c) break;
                    c->key = ((LONGLONG)p.pid << 3) | 0;
                    c->data = (LPARAM)&Data::g.procs[i];
                    c->kind = TLR_ITEM;
                    c->depth = 1;
                }
            }
        }
        TL_SetRows(tl, rows.p, rows.n);
    }

    /* ---------- ITreeListOwner ---------- */

    void TLCellText(LPARAM data, int col, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        UserRow* u = User(data);
        if (u)
        {
            switch (col)
            {
            case UC_USER:   StringCchCopyW(buf, cch, u->user); break;
            case UC_STATUS: StringCchCopyW(buf, cch, u->state); break;
            case UC_CPU:    FmtPct(u->cpuPct, buf, cch); break;
            case UC_MEM:    FmtMemMB(u->memBytes, buf, cch); break;
            }
            return;
        }
        ProcRow* p = PR(data);
        if (!p) return;
        switch (col)
        {
        case UC_USER:
            if (p->x && p->x->resolved && p->x->desc[0])
                StringCchCopyW(buf, cch, p->x->desc);
            else
                StringCchCopyW(buf, cch, p->image);
            break;
        case UC_CPU: FmtPct(p->cpuPct, buf, cch); break;
        case UC_MEM: FmtMemMB(p->memBytes, buf, cch); break;
        }
    }

    void TLSecondaryText(LPARAM data, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        UserRow* u = User(data);
        if (u)
            StringCchPrintfW(buf, cch, L"(%d)", u->nProc);
    }

    double TLCellHeat(LPARAM data, int col)
    {
        UserRow* u = User(data);
        double cpu = u ? u->cpuPct : (PR(data) ? PR(data)->cpuPct : 0);
        ULONGLONG mem = u ? u->memBytes : (PR(data) ? PR(data)->memBytes : 0);
        switch (col)
        {
        case UC_CPU: return cpu / 100.0;
        case UC_MEM: return Data::g.memTotal ? (double)mem * 4.0 / Data::g.memTotal : 0;
        }
        return -1.0;
    }

    double TLHeaderHeat(int col)
    {
        switch (col)
        {
        case UC_CPU: return Data::g.cpuTotalPct / 100.0;
        case UC_MEM: return Data::g.memTotal ?
                            (double)Data::g.memInUse / Data::g.memTotal : 0;
        }
        return -1.0;
    }

    void TLHeaderValue(int col, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        if (col == UC_CPU) FmtPct(Data::g.cpuTotalPct, buf, cch);
        if (col == UC_MEM)
            FmtPct(Data::g.memTotal ? 100.0 * Data::g.memInUse / Data::g.memTotal : 0,
                   buf, cch);
    }

    HICON TLRowIcon(LPARAM data)
    {
        ProcRow* p = PR(data);
        return (p && p->x) ? p->x->icon : NULL;
    }

    int TLRowGlyph(LPARAM data)
    {
        return User(data) ? IC_USERS : IC_WINDOW;
    }

    void TLOnToggle(LPARAM data, BOOL exp)
    {
        UserRow* u = User(data);
        if (u)
        {
            SetExpanded(u->sessionId, exp);
            Rebuild();
        }
    }

    void TLOnSelChanged(void) { Frame_UpdateCommandStates(); }

    void DoDisconnect(UserRow* u)
    {
        if (!u) return;
        WCHAR body[256];
        StringCchPrintfW(body, _countof(body),
            L"%s's session will be disconnected and any unsaved work may be "
            L"lost. Do you want to continue?", u->user);
        ConfirmOpts o = { L"Disconnect user", body, L"Disconnect user",
                          L"Cancel", NULL, NULL, TRUE };
        if (Dlg_Confirm(hwnd, o) && !Data::UserDisconnect(u->sessionId))
        {
            ConfirmOpts e = { L"Disconnect user",
                              L"The session could not be disconnected on this system.",
                              L"OK", NULL, NULL, NULL, FALSE };
            Dlg_Confirm(hwnd, e);
        }
    }

    void DoLogoff(UserRow* u)
    {
        if (!u) return;
        WCHAR body[256];
        StringCchPrintfW(body, _countof(body),
            L"%s's unsaved data might be lost if you sign them out. "
            L"Do you want to continue?", u->user);
        ConfirmOpts o = { L"Sign out user", body, L"Sign out user",
                          L"Cancel", NULL, NULL, TRUE };
        if (Dlg_Confirm(hwnd, o) && !Data::UserLogoff(u->sessionId))
        {
            ConfirmOpts e = { L"Sign out user",
                              L"The session could not be signed out on this system.",
                              L"OK", NULL, NULL, NULL, FALSE };
            Dlg_Confirm(hwnd, e);
        }
    }

    void TLOnContext(LPARAM data, POINT scr)
    {
        UserRow* u = User(data);
        if (u)
        {
            MItem items[] =
            {
                { 1, IsExpanded(u->sessionId) ? L"Collapse" : L"Expand", 0, NULL, 0 },
                { 0, NULL, MIF_SEP, NULL, 0 },
                { 2, L"Disconnect", 0, NULL, 0 },
                { 3, L"Sign out", 0, NULL, 0 },
            };
            UINT cmd = Menu_Show(hwnd, scr, items, _countof(items));
            switch (cmd)
            {
            case 1:
                SetExpanded(u->sessionId, !IsExpanded(u->sessionId));
                Rebuild();
                break;
            case 2: DoDisconnect(u); break;
            case 3: DoLogoff(u); break;
            }
            return;
        }

        ProcRow* p = PR(data);
        if (p)
        {
            MItem items[] =
            {
                { 1, L"End task", p->pid > 4 ? 0u : MIF_DISABLED, NULL, 0 },
                { 2, L"Go to details", 0, NULL, 0 },
            };
            UINT cmd = Menu_Show(hwnd, scr, items, _countof(items));
            if (cmd == 1) Action_EndTask(hwnd, p, FALSE);
            if (cmd == 2) Frame_SwitchToDetails(p->pid);
        }
    }

    /* ---------- Page ---------- */

    void BuildCommands(BtnStrip& s)
    {
        s.Add(CMD_USER_DISCONNECT, L"Disconnect user", IC_DISCONNECT, BS_OUTLINE, FALSE);
        s.Add(CMD_USER_LOGOFF, L"Sign out user", IC_SIGNOUT, BS_ACCENT, FALSE);
    }

    void UpdateCommands(BtnStrip& s)
    {
        UserRow* u = User(TL_GetSelData(tl));
        s.SetEnabled(CMD_USER_DISCONNECT, u != NULL);
        s.SetEnabled(CMD_USER_LOGOFF, u != NULL);
    }

    void OnCommand(int id)
    {
        UserRow* u = User(TL_GetSelData(tl));
        if (id == CMD_USER_DISCONNECT) DoDisconnect(u);
        if (id == CMD_USER_LOGOFF) DoLogoff(u);
    }

    void OnTick()
    {
        Data::RefreshUsers();
        Rebuild();
    }

    void OnThemeChanged() { TL_Refresh(tl); }

    HWND Create(HWND parent);
};

static UsersPage* s_page;

static LRESULT CALLBACK PgUsersProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
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

HWND UsersPage::Create(HWND parent)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = PgUsersProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PGU_CLASS;
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(0, PGU_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN,
                           0, 0, 100, 100, parent, NULL, g_app.hInst, NULL);
    tl = TL_Create(hwnd, this, TRUE);

    TLColumn cols[4];
    ZeroMemory(cols, sizeof(cols));
    cols[0].id = UC_USER;   StringCchCopyW(cols[0].title, 48, L"User");
    cols[0].width = S(340); cols[0].minWidth = S(200);
    cols[1].id = UC_STATUS; StringCchCopyW(cols[1].title, 48, L"Status");
    cols[1].width = S(130); cols[1].minWidth = S(80);
    cols[2].id = UC_CPU;    StringCchCopyW(cols[2].title, 48, L"CPU");
    cols[2].width = S(80);  cols[2].minWidth = S(60);
    cols[2].flags = TLC_RIGHT | TLC_HEAT;
    cols[3].id = UC_MEM;    StringCchCopyW(cols[3].title, 48, L"Memory");
    cols[3].width = S(110); cols[3].minWidth = S(80);
    cols[3].flags = TLC_RIGHT | TLC_HEAT;
    TL_SetColumns(tl, cols, 4);
    TL_SetEmptyText(tl, L"There are no users signed in");

    Rebuild();
    return hwnd;
}

Page* CreateUsersPage(void)
{
    s_page = new UsersPage();
    return s_page;
}
