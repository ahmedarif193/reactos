/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Details page (dense process grid, full context menu)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define PGD_CLASS L"TM11PageDetails"

enum { DC_NAME = 0, DC_PID, DC_STATUS, DC_USER, DC_CPU, DC_MEM, DC_ARCH, DC_DESC };

struct DetailsPage : Page, ITreeListOwner
{
    HWND tl;
    int  sortCol;
    BOOL sortDesc;
    Vec<TLRow> rows;
    Vec<ProcRow*> list;
    ULONG pendingSelPid;

    DetailsPage() : tl(NULL), sortCol(DC_NAME), sortDesc(FALSE), pendingSelPid(0) {}

    const WCHAR* Title() { return L"Details"; }
    BOOL WantSearch() { return TRUE; }

    static ProcRow* PR(LPARAM data) { return (ProcRow*)data; }

    BOOL MatchesSearch(const ProcRow& p)
    {
        if (!g_app.search[0]) return TRUE;
        WCHAR pid[16];
        StringCchPrintfW(pid, _countof(pid), L"%u", p.pid);
        if (StrStrIW(p.image, g_app.search)) return TRUE;
        if (StrStrIW(pid, g_app.search)) return TRUE;
        if (p.x && p.x->resolved &&
            (StrStrIW(p.x->desc, g_app.search) || StrStrIW(p.x->user, g_app.search)))
            return TRUE;
        return FALSE;
    }

    struct SortCtx { int col; BOOL desc; };
    static SortCtx s_sc;

    static int __cdecl Cmp(const void* a, const void* b)
    {
        const ProcRow* pa = *(const ProcRow* const*)a;
        const ProcRow* pb = *(const ProcRow* const*)b;
        double d = 0;
        switch (s_sc.col)
        {
        case DC_PID: d = (double)pa->pid - (double)pb->pid; break;
        case DC_CPU: d = pa->cpuPct - pb->cpuPct; break;
        case DC_MEM: d = (double)pa->memBytes - (double)pb->memBytes; break;
        case DC_STATUS:
            d = (pa->flags & PF_SUSPENDED) - (pb->flags & PF_SUSPENDED);
            break;
        case DC_USER:
        {
            const WCHAR* ua = (pa->x && pa->x->user[0]) ? pa->x->user : L"";
            const WCHAR* ub = (pb->x && pb->x->user[0]) ? pb->x->user : L"";
            d = lstrcmpiW(ua, ub);
            break;
        }
        case DC_ARCH:
        {
            const WCHAR* aa = (pa->x && pa->x->arch[0]) ? pa->x->arch : L"";
            const WCHAR* ab = (pb->x && pb->x->arch[0]) ? pb->x->arch : L"";
            d = lstrcmpiW(aa, ab);
            break;
        }
        case DC_DESC:
        {
            const WCHAR* da = (pa->x && pa->x->desc[0]) ? pa->x->desc : L"";
            const WCHAR* db = (pb->x && pb->x->desc[0]) ? pb->x->desc : L"";
            d = lstrcmpiW(da, db);
            break;
        }
        default: d = lstrcmpiW(pa->image, pb->image); break;
        }
        if (d == 0) d = (double)pa->pid - (double)pb->pid;
        int r = (d > 0) ? 1 : (d < 0) ? -1 : 0;
        return s_sc.desc ? -r : r;
    }

    void Rebuild(void)
    {
        rows.Clear();
        list.Clear();
        for (int i = 0; i < Data::g.procs.n; i++)
        {
            if (!MatchesSearch(Data::g.procs[i])) continue;
            list.Push(&Data::g.procs[i]);
        }
        s_sc.col = sortCol;
        s_sc.desc = sortDesc;
        if (list.n > 1)
            qsort(list.p, list.n, sizeof(ProcRow*), Cmp);

        for (int i = 0; i < list.n; i++)
        {
            TLRow* r = rows.Add();
            if (!r) break;
            r->key = ((LONGLONG)list[i]->pid << 3) | 0;
            r->data = (LPARAM)list[i];
            r->kind = TLR_ITEM;
        }
        TL_SetRows(tl, rows.p, rows.n);

        if (pendingSelPid)
        {
            /* select and reveal a pid requested via "Go to details" */
            TL_SetSelKey(tl, ((LONGLONG)pendingSelPid << 3) | 0);
            pendingSelPid = 0;
        }
    }

    /* ---------- ITreeListOwner ---------- */

    void TLCellText(LPARAM data, int col, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        ProcRow* p = PR(data);
        if (!p) return;
        switch (col)
        {
        case DC_NAME:
            StringCchCopyW(buf, cch, p->image);
            break;
        case DC_PID:
            StringCchPrintfW(buf, cch, L"%u", p->pid);
            break;
        case DC_STATUS:
            StringCchCopyW(buf, cch, (p->flags & PF_SUSPENDED) ? L"Suspended" : L"Running");
            break;
        case DC_USER:
            if (p->x && p->x->user[0]) StringCchCopyW(buf, cch, p->x->user);
            break;
        case DC_CPU:
        {
            /* classic taskmgr style: integer percent, "00" for idle-ish */
            int v = (int)(p->cpuPct + 0.5);
            StringCchPrintfW(buf, cch, L"%02d", v);
            break;
        }
        case DC_MEM:
        {
            WCHAR t[32];
            FmtThousands(p->memBytes / 1024, t, _countof(t));
            StringCchPrintfW(buf, cch, L"%s K", t);
            break;
        }
        case DC_ARCH:
            if (p->x && p->x->arch[0]) StringCchCopyW(buf, cch, p->x->arch);
            break;
        case DC_DESC:
            if (p->x && p->x->desc[0]) StringCchCopyW(buf, cch, p->x->desc);
            break;
        }
    }

    HICON TLRowIcon(LPARAM data)
    {
        ProcRow* p = PR(data);
        return (p && p->x) ? p->x->icon : NULL;
    }

    IconId TLRowGlyph(LPARAM data)
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

    void TLOnDelete(LPARAM data)
    {
        ProcRow* p = PR(data);
        if (p) Action_EndTask(hwnd, p, TRUE);
    }

    void TLOnContext(LPARAM data, POINT scr)
    {
        ProcRow* p = PR(data);
        if (!p) return;
        BOOL canAct = p->pid > 4;
        BOOL eff = Data::IsEfficiency(*p);
        DWORD cls = canAct ? Data::GetPriClass(p->pid) : 0;

        MItem prio[] =
        {
            { 20, L"Realtime",     (cls == REALTIME_PRIORITY_CLASS)     ? MIF_RADIO : 0u, NULL, 0 },
            { 21, L"High",         (cls == HIGH_PRIORITY_CLASS)         ? MIF_RADIO : 0u, NULL, 0 },
            { 22, L"Above normal", (cls == ABOVE_NORMAL_PRIORITY_CLASS) ? MIF_RADIO : 0u, NULL, 0 },
            { 23, L"Normal",       (cls == NORMAL_PRIORITY_CLASS)       ? MIF_RADIO : 0u, NULL, 0 },
            { 24, L"Below normal", (cls == BELOW_NORMAL_PRIORITY_CLASS) ? MIF_RADIO : 0u, NULL, 0 },
            { 25, L"Low",          (cls == IDLE_PRIORITY_CLASS)         ? MIF_RADIO : 0u, NULL, 0 },
        };

        MItem items[] =
        {
            { 1, L"End task", canAct ? 0u : MIF_DISABLED, NULL, 0 },
            { 2, L"End process tree", canAct ? 0u : MIF_DISABLED, NULL, 0 },
            { 0, NULL, MIF_SEP, NULL, 0 },
            { 3, L"Set priority", canAct ? 0u : MIF_DISABLED, prio, _countof(prio) },
            { 4, L"Set affinity", canAct ? 0u : MIF_DISABLED, NULL, 0 },
            { 5, L"Efficiency mode",
              (canAct ? 0u : MIF_DISABLED) | (eff ? MIF_CHECKED : 0u), NULL, 0 },
            { 0, NULL, MIF_SEP, NULL, 0 },
            { 6, L"Open file location",
              (p->x && p->x->path[0]) ? 0u : MIF_DISABLED, NULL, 0 },
            { 7, L"Search online", 0, NULL, 0 },
            { 8, L"Properties", (p->x && p->x->path[0]) ? 0u : MIF_DISABLED, NULL, 0 },
        };

        UINT cmd = Menu_Show(hwnd, scr, items, _countof(items));
        switch (cmd)
        {
        case 1: Action_EndTask(hwnd, p, TRUE); break;
        case 2:
        {
            WCHAR body[512];
            StringCchPrintfW(body, _countof(body),
                L"Do you want to end the process tree of %s?\n\nEnding a process "
                L"tree causes all processes created by the process to close.",
                p->image);
            ConfirmOpts o = { L"End process tree",
                              body, L"End process tree", L"Cancel", NULL, NULL, TRUE };
            if (Dlg_Confirm(hwnd, o))
                Data::KillProcess(p->pid, TRUE);
            break;
        }
        case 4: Dlg_Affinity(g_app.hFrame, p->pid); break;
        case 5: Action_ToggleEfficiency(hwnd, p); break;
        case 6: Data::OpenFileLocation(*p); break;
        case 7: Data::SearchOnline(p->image); break;
        case 8: Data::ShowFileProperties(*p); break;
        case 20: case 21: case 22: case 23: case 24: case 25:
        {
            static const DWORD clsMap[] =
            {
                REALTIME_PRIORITY_CLASS, HIGH_PRIORITY_CLASS,
                ABOVE_NORMAL_PRIORITY_CLASS, NORMAL_PRIORITY_CLASS,
                BELOW_NORMAL_PRIORITY_CLASS, IDLE_PRIORITY_CLASS
            };
            DWORD want = clsMap[cmd - 20];
            WCHAR body[256];
            StringCchPrintfW(body, _countof(body),
                L"Do you want to change the priority of %s?\n\nChanging the "
                L"priority of certain processes could cause system instability.",
                p->image);
            ConfirmOpts o = { L"Change priority",
                              body, L"Change priority", L"Cancel", NULL, NULL, FALSE };
            if (Dlg_Confirm(hwnd, o) && !Data::SetPriClass(p->pid, want))
            {
                ConfirmOpts e = { L"Change priority",
                                  L"Unable to change priority.\nAccess is denied.",
                                  L"OK", NULL, NULL, NULL, FALSE };
                Dlg_Confirm(hwnd, e);
            }
            break;
        }
        }
    }

    /* ---------- Page ---------- */

    void BuildCommands(BtnStrip& s)
    {
        s.Add(CMD_RUNTASK, L"Run new task", IC_RUNTASK, BS_OUTLINE);
        s.Add(CMD_ENDTASK, L"End task", IC_ENDTASK, BS_ACCENT, FALSE);
    }

    void UpdateCommands(BtnStrip& s)
    {
        ProcRow* p = PR(TL_GetSelData(tl));
        s.SetEnabled(CMD_ENDTASK, p && p->pid > 4);
    }

    void OnCommand(int id)
    {
        ProcRow* p = PR(TL_GetSelData(tl));
        switch (id)
        {
        case CMD_RUNTASK:
        {
            WCHAR cmd[1024];
            BOOL admin = FALSE;
            if (Dlg_RunTask(g_app.hFrame, cmd, _countof(cmd), &admin))
                Data::RunTask(cmd, admin);
            break;
        }
        case CMD_ENDTASK:
            if (p) Action_EndTask(hwnd, p, TRUE);
            break;
        }
    }

    void OnTick() { Rebuild(); }
    void OnSearch() { Rebuild(); }
    void OnThemeChanged() { TL_Refresh(tl); }

    HWND Create(HWND parent);
};

DetailsPage::SortCtx DetailsPage::s_sc;

static DetailsPage* s_page;

void DetailsPage_SelectPid(ULONG pid)
{
    if (s_page)
    {
        s_page->pendingSelPid = pid;
        if (s_page->tl)
            s_page->Rebuild();
    }
}

static LRESULT CALLBACK PgDetProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
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

HWND DetailsPage::Create(HWND parent)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = PgDetProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PGD_CLASS;
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(0, PGD_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN,
                           0, 0, 100, 100, parent, NULL, g_app.hInst, NULL);
    tl = TL_Create(hwnd, this, FALSE);

    TLColumn cols[8];
    ZeroMemory(cols, sizeof(cols));
    cols[0].id = DC_NAME;   StringCchCopyW(cols[0].title, 48, L"Name");
    cols[0].width = S(210); cols[0].minWidth = S(140);
    cols[1].id = DC_PID;    StringCchCopyW(cols[1].title, 48, L"PID");
    cols[1].width = S(70);  cols[1].minWidth = S(50); cols[1].flags = TLC_RIGHT;
    cols[2].id = DC_STATUS; StringCchCopyW(cols[2].title, 48, L"Status");
    cols[2].width = S(90);  cols[2].minWidth = S(70);
    cols[3].id = DC_USER;   StringCchCopyW(cols[3].title, 48, L"User name");
    cols[3].width = S(130); cols[3].minWidth = S(80);
    cols[4].id = DC_CPU;    StringCchCopyW(cols[4].title, 48, L"CPU");
    cols[4].width = S(60);  cols[4].minWidth = S(45); cols[4].flags = TLC_RIGHT;
    cols[5].id = DC_MEM;    StringCchCopyW(cols[5].title, 48, L"Memory (private)");
    cols[5].width = S(130); cols[5].minWidth = S(90); cols[5].flags = TLC_RIGHT;
    cols[6].id = DC_ARCH;   StringCchCopyW(cols[6].title, 48, L"Architecture");
    cols[6].width = S(95);  cols[6].minWidth = S(60);
    cols[7].id = DC_DESC;   StringCchCopyW(cols[7].title, 48, L"Description");
    cols[7].width = S(260); cols[7].minWidth = S(120);
    TL_SetColumns(tl, cols, 8);
    TL_SetSort(tl, DC_NAME, FALSE);
    TL_SetEmptyText(tl, L"No matching processes");

    Rebuild();
    return hwnd;
}

Page* CreateDetailsPage(void)
{
    s_page = new DetailsPage();
    return s_page;
}
