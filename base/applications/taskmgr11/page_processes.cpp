/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Processes page (grouped heat-map process list)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define PGP_CLASS L"TM11PageProcs"

/* column ids */
enum { PC_NAME = 0, PC_STATUS = 1, PC_CPU = 2, PC_MEM = 3, PC_DISK = 4, PC_NET = 5 };

/* row cookie: low 3 bits = subtype, rest = pid (or category) */
#define RK_PROC(pid)      (((LONGLONG)(pid) << 3) | 0)
#define RK_GROUP(cat)     (((LONGLONG)(cat) << 3) | 1)
#define RK_SVC(pid, i)    (((LONGLONG)(pid) << 16) | ((LONGLONG)(i) << 3) | 2)

static const COLORREF LEAF_LIGHT = RGB(0x2E, 0x7D, 0x32);
static const COLORREF LEAF_DARK  = RGB(0x6C, 0xCB, 0x5F);

/* ------------------------------------------------------------------ */
/*  Shared Win11-UX process actions (also used by details page)        */
/* ------------------------------------------------------------------ */

void Action_EndTask(HWND owner, ProcRow* p, BOOL confirm)
{
    if (!p || p->pid <= 4) return;

    if ((p->flags & PF_CRITICAL) || p->category == CAT_WINDOWS)
    {
        WCHAR body[512];
        StringCchPrintfW(body, _countof(body),
            L"Ending %s might cause Windows to become unstable or shut down, "
            L"causing you to lose any unsaved data. Are you sure you want to "
            L"end this process?", p->image);
        ConfirmOpts o = { L"Do you want to end the system process?",
                          body, L"Shut down", L"Cancel", NULL, NULL, TRUE };
        if (!Dlg_Confirm(owner, o))
            return;
    }
    else if (confirm)
    {
        WCHAR body[512];
        StringCchPrintfW(body, _countof(body),
            L"If an open program is associated with this process, it will close "
            L"and you will lose any unsaved data.\n\nEnd %s?", p->image);
        ConfirmOpts o = { L"Do you want to end this process?",
                          body, L"End process", L"Cancel", NULL, NULL, TRUE };
        if (!Dlg_Confirm(owner, o))
            return;
    }

    if (!Data::KillProcess(p->pid, FALSE))
    {
        ConfirmOpts o = { L"Unable to terminate process",
                          L"The operation could not be completed.\nAccess is denied.",
                          L"OK", NULL, NULL, NULL, FALSE };
        Dlg_Confirm(owner, o);
    }
}

void Action_ToggleEfficiency(HWND owner, ProcRow* p)
{
    if (!p || p->pid <= 4) return;

    BOOL on = Data::IsEfficiency(*p);
    if (!on && !g_app.st.noEffPrompt)
    {
        BOOL dontAsk = FALSE;
        WCHAR body[512];
        StringCchPrintfW(body, _countof(body),
            L"Efficiency mode reduces process priority and improves power "
            L"efficiency, but may cause instability for certain processes.\n\n"
            L"Turn on efficiency mode for %s?", p->image);
        ConfirmOpts o = { L"Turn on efficiency mode",
                          body, L"Turn on efficiency mode", L"Cancel",
                          L"Don't ask me again", &dontAsk, FALSE };
        BOOL go = Dlg_Confirm(owner, o);
        if (dontAsk)
        {
            g_app.st.noEffPrompt = TRUE;
            Settings_Save();
        }
        if (!go) return;
    }
    Data::SetEfficiency(p->pid, !on);
    Frame_UpdateCommandStates();
}

/* ------------------------------------------------------------------ */
/*  Page                                                               */
/* ------------------------------------------------------------------ */

struct ProcessesPage : Page, ITreeListOwner
{
    HWND tl;
    int  sortCol;
    BOOL sortDesc;
    BOOL catCollapsed[3];
    Vec<ULONG> collapsedPids;    /* parents the user collapsed (default expanded) */
    Vec<TLRow> rows;

    ProcessesPage() : tl(NULL), sortCol(PC_NAME), sortDesc(FALSE)
    {
        catCollapsed[0] = catCollapsed[1] = catCollapsed[2] = FALSE;
    }

    const WCHAR* Title() { return L"Processes"; }
    BOOL WantSearch() { return TRUE; }

    /* ---------- helpers ---------- */

    static ProcRow* PR(LPARAM data) { return (ProcRow*)data; }

    BOOL IsCollapsed(ULONG pid)
    {
        for (int i = 0; i < collapsedPids.n; i++)
            if (collapsedPids[i] == pid) return TRUE;
        return FALSE;
    }

    void SetCollapsed(ULONG pid, BOOL c)
    {
        for (int i = 0; i < collapsedPids.n; i++)
        {
            if (collapsedPids[i] == pid)
            {
                if (!c) collapsedPids.RemoveAt(i);
                return;
            }
        }
        if (c) collapsedPids.Push(pid);
    }

    static void DisplayName(const ProcRow& p, WCHAR* buf, int cch)
    {
        if (p.category == CAT_APP && p.wndTitle[0])
            StringCchCopyW(buf, cch, p.wndTitle);
        else if (p.flags & PF_SVCHOST)
        {
            const WCHAR* grp = Data::SvchostGroup(p.pid);
            if (grp)
                StringCchPrintfW(buf, cch, L"Service Host: %s", grp);
            else
                StringCchCopyW(buf, cch, L"Service Host");
        }
        else if (p.x && p.x->resolved && p.x->desc[0])
            StringCchCopyW(buf, cch, p.x->desc);
        else
            StringCchCopyW(buf, cch, p.image);
    }

    BOOL MatchesSearch(const ProcRow& p)
    {
        if (!g_app.search[0]) return TRUE;
        WCHAR name[192];
        DisplayName(p, name, _countof(name));
        WCHAR pid[16];
        StringCchPrintfW(pid, _countof(pid), L"%u", p.pid);
        if (StrStrIW(name, g_app.search)) return TRUE;
        if (StrStrIW(p.image, g_app.search)) return TRUE;
        if (StrStrIW(pid, g_app.search)) return TRUE;
        if (p.x && p.x->resolved && StrStrIW(p.x->desc, g_app.search)) return TRUE;
        return FALSE;
    }

    /* is c a nested child of some app/parent row? */
    static BOOL IsChildOf(const ProcRow& c, const ProcRow& par)
    {
        return c.ppid == par.pid && c.pid != par.pid &&
               c.category != CAT_APP &&
               c.createTime >= par.createTime;
    }

    /* ---------- model build ---------- */

    struct SortCtx { int col; BOOL desc; ProcessesPage* pg; };
    static SortCtx s_sc;

    static int __cdecl CmpProc(const void* a, const void* b)
    {
        const ProcRow* pa = *(const ProcRow* const*)a;
        const ProcRow* pb = *(const ProcRow* const*)b;
        double d = 0;
        switch (s_sc.col)
        {
        case PC_CPU:  d = pa->cpuPct - pb->cpuPct; break;
        case PC_MEM:  d = (double)pa->memBytes - (double)pb->memBytes; break;
        case PC_DISK: d = pa->diskBps - pb->diskBps; break;
        case PC_NET:  d = pa->netBps - pb->netBps; break;
        case PC_STATUS:
        {
            int sa = (pa->flags & (PF_SUSPENDED | PF_EFFICIENCY | PF_HUNG)) ? 1 : 0;
            int sb = (pb->flags & (PF_SUSPENDED | PF_EFFICIENCY | PF_HUNG)) ? 1 : 0;
            d = sa - sb;
            break;
        }
        default:
        {
            WCHAR na[192], nb[192];
            DisplayName(*pa, na, _countof(na));
            DisplayName(*pb, nb, _countof(nb));
            int c = lstrcmpiW(na, nb);
            d = c;
            break;
        }
        }
        if (d == 0)
            d = (double)pa->pid - (double)pb->pid;
        int r = (d > 0) ? 1 : (d < 0) ? -1 : 0;
        return s_sc.desc ? -r : r;
    }

    void AddProcRow(ProcRow* p, int depth)
    {
        TLRow* r = rows.Add();
        if (!r) return;
        r->key = RK_PROC(p->pid);
        r->data = (LPARAM)p;
        r->kind = TLR_ITEM;
        r->depth = (BYTE)depth;

        /* children: nested processes (apps only, like Win11) + svchost services */
        Vec<ProcRow*> kids;
        if (depth == 0 && p->category == CAT_APP)
        {
            for (int i = 0; i < Data::g.procs.n; i++)
            {
                ProcRow& c = Data::g.procs[i];
                if (c.pid <= 4) continue;
                if (IsChildOf(c, *p))
                    kids.Push(&Data::g.procs[i]);
            }
        }
        const WCHAR* svc = (p->flags & PF_SVCHOST) ? Data::SvchostServices(p->pid) : NULL;

        BOOL hasKids = kids.n > 0 || (svc && svc[0]);
        if (hasKids)
        {
            r->expandable = TRUE;
            r->expanded = !IsCollapsed(p->pid);
            if (r->expanded)
            {
                if (kids.n > 1)
                    qsort(kids.p, kids.n, sizeof(ProcRow*), CmpProc);
                for (int i = 0; i < kids.n; i++)
                    AddProcRow(kids[i], depth + 1);

                if (svc && svc[0])
                {
                    /* split display names on ", " */
                    const WCHAR* s = svc;
                    int idx = 0;
                    while (*s && idx < 64)
                    {
                        TLRow* sr = rows.Add();
                        if (!sr) break;
                        sr->key = RK_SVC(p->pid, idx);
                        sr->data = (LPARAM)(s);   /* points into stable buffer */
                        sr->kind = TLR_ITEM;
                        sr->depth = (BYTE)(depth + 1);
                        const WCHAR* comma = wcsstr(s, L", ");
                        if (!comma) break;
                        s = comma + 2;
                        idx++;
                    }
                }
            }
        }
    }

    void Rebuild(void)
    {
        rows.Clear();

        /* bucket top-level processes by category */
        for (int cat = 0; cat < 3; cat++)
        {
            Vec<ProcRow*> top;
            for (int i = 0; i < Data::g.procs.n; i++)
            {
                ProcRow& p = Data::g.procs[i];
                if (p.pid == 0) continue;                     /* idle hidden here */
                if (p.category != cat) continue;
                if (!MatchesSearch(p)) continue;

                /* skip if it will be nested under an app parent */
                BOOL nested = FALSE;
                if (cat != CAT_APP && !g_app.search[0])
                {
                    for (int j = 0; j < Data::g.procs.n; j++)
                    {
                        ProcRow& par = Data::g.procs[j];
                        if (par.category == CAT_APP && IsChildOf(p, par))
                        {
                            nested = TRUE;
                            break;
                        }
                    }
                }
                if (!nested)
                    top.Push(&Data::g.procs[i]);
            }

            if (!top.n) continue;

            static const WCHAR* catNames[3] = { L"Apps", L"Background processes",
                                                L"Windows processes" };
            TLRow* g = rows.Add();
            if (!g) return;
            g->key = RK_GROUP(cat);
            /* low-bit tag marks category rows; real pointers are 8-aligned */
            g->data = (LPARAM)(((DWORD_PTR)cat << 3) | 1);
            g->kind = TLR_GROUP;
            g->expandable = TRUE;
            g->expanded = !catCollapsed[cat];
            (void)catNames;

            if (catCollapsed[cat]) continue;

            s_sc.col = sortCol;
            s_sc.desc = sortDesc;
            s_sc.pg = this;
            if (top.n > 1)
                qsort(top.p, top.n, sizeof(ProcRow*), CmpProc);

            for (int i = 0; i < top.n; i++)
                AddProcRow(top[i], 0);
        }

        TL_SetRows(tl, rows.p, rows.n);
    }

    /* map row cookie back to a live ProcRow (or NULL for pseudo rows) */
    ProcRow* RowProc(LPARAM data)
    {
        if (!data || (data & 1)) return NULL;
        /* svc name rows point into the svchost names buffer (outside proc array) */
        BYTE* pd = (BYTE*)data;
        BYTE* base = (BYTE*)Data::g.procs.p;
        BYTE* end = base + Data::g.procs.n * sizeof(ProcRow);
        if (pd >= base && pd < end)
            return (ProcRow*)data;
        return NULL;
    }

    int RowCat(LPARAM data)
    {
        if (data & 1)
            return (int)((DWORD_PTR)data >> 3);
        return -1;
    }

    /* ---------- ITreeListOwner ---------- */

    void TLCellText(LPARAM data, int col, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        int cat = RowCat(data);
        if (cat >= 0)
        {
            if (col == -1)
            {
                static const WCHAR* catNames[3] = { L"Apps", L"Background processes",
                                                    L"Windows processes" };
                int count = 0;
                for (int i = 0; i < Data::g.procs.n; i++)
                    if (Data::g.procs[i].pid != 0 &&
                        Data::g.procs[i].category == cat &&
                        MatchesSearch(Data::g.procs[i]))
                        count++;
                StringCchPrintfW(buf, cch, L"%s (%d)", catNames[cat], count);
            }
            return;
        }

        ProcRow* p = RowProc(data);
        if (!p)
        {
            /* hosted service pseudo row */
            if (col == PC_NAME)
            {
                const WCHAR* s = (const WCHAR*)data;
                const WCHAR* comma = wcsstr(s, L", ");
                int len = comma ? (int)(comma - s) : lstrlenW(s);
                if (len >= cch) len = cch - 1;
                CopyMemory(buf, s, len * sizeof(WCHAR));
                buf[len] = 0;
            }
            return;
        }

        switch (col)
        {
        case PC_NAME:
            DisplayName(*p, buf, cch);
            break;
        case PC_STATUS:
            if (p->flags & PF_HUNG)
                StringCchCopyW(buf, cch, L"Not responding");
            break;
        case PC_CPU:
            FmtPct(p->cpuPct, buf, cch);
            break;
        case PC_MEM:
            FmtMemMB(p->memBytes, buf, cch);
            break;
        case PC_DISK:
        {
            double mbs = p->diskBps / (1024.0 * 1024.0);
            StringCchPrintfW(buf, cch, L"%.1f MB/s", mbs < 0.05 ? 0.0 : mbs);
            break;
        }
        case PC_NET:
            StringCchPrintfW(buf, cch, L"0 Mbps");
            break;
        }
    }

    double TLCellHeat(LPARAM data, int col)
    {
        ProcRow* p = RowProc(data);
        if (!p) return (RowCat(data) >= 0) ? -1.0 : 0.0;
        switch (col)
        {
        case PC_CPU:  return p->cpuPct / 100.0;
        case PC_MEM:  return Data::g.memTotal ?
                             (double)p->memBytes * 4.0 / Data::g.memTotal : 0;
        case PC_DISK: return p->diskBps / (100.0 * 1024.0 * 1024.0);
        case PC_NET:  return 0.0;
        }
        return -1.0;
    }

    double TLHeaderHeat(int col)
    {
        switch (col)
        {
        case PC_CPU:  return Data::g.cpuTotalPct / 100.0;
        case PC_MEM:  return Data::g.memTotal ?
                             (double)Data::g.memInUse / Data::g.memTotal : 0;
        case PC_DISK: return (Data::g.diskReadBps + Data::g.diskWriteBps) /
                             (100.0 * 1024.0 * 1024.0);
        case PC_NET:  return (Data::g.netRecvBps + Data::g.netSendBps) /
                             (12.5 * 1024.0 * 1024.0);
        }
        return -1.0;
    }

    void TLHeaderValue(int col, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        switch (col)
        {
        case PC_CPU:
            FmtPct(Data::g.cpuTotalPct, buf, cch);
            break;
        case PC_MEM:
            FmtPct(Data::g.memTotal ? 100.0 * Data::g.memInUse / Data::g.memTotal : 0,
                   buf, cch);
            break;
        case PC_DISK:
            StringCchPrintfW(buf, cch, L"%.1f MB/s",
                (Data::g.diskReadBps + Data::g.diskWriteBps) / (1024.0 * 1024.0));
            break;
        case PC_NET:
            FmtMbps(Data::g.netRecvBps + Data::g.netSendBps, buf, cch);
            break;
        }
    }

    HICON TLRowIcon(LPARAM data)
    {
        ProcRow* p = RowProc(data);
        return (p && p->x) ? p->x->icon : NULL;
    }

    int TLRowGlyph(LPARAM data)
    {
        if (RowCat(data) >= 0) return IC_NONE;
        ProcRow* p = RowProc(data);
        if (!p) return IC_SERVICES;
        if (p->flags & PF_SVCHOST) return IC_SERVICES;
        if (p->category == CAT_WINDOWS) return IC_SETTINGS;
        if (p->category == CAT_APP) return IC_WINDOW;
        return IC_WINDOW;
    }

    int TLStatusGlyph(LPARAM data, int col, COLORREF* c)
    {
        if (col != PC_STATUS) return IC_NONE;
        ProcRow* p = RowProc(data);
        if (!p) return IC_NONE;
        if (p->flags & PF_SUSPENDED)
        {
            *c = g_t.dark ? LEAF_DARK : LEAF_LIGHT;
            return IC_PAUSE;
        }
        if (p->flags & PF_EFFICIENCY)
        {
            *c = g_t.dark ? LEAF_DARK : LEAF_LIGHT;
            return IC_LEAF;
        }
        return IC_NONE;
    }

    void TLOnSort(int col, BOOL desc)
    {
        sortCol = col;
        sortDesc = desc;
        Rebuild();
    }

    void TLOnToggle(LPARAM data, BOOL expanded)
    {
        int cat = RowCat(data);
        if (cat >= 0)
        {
            catCollapsed[cat] = !expanded;
        }
        else
        {
            ProcRow* p = RowProc(data);
            if (p) SetCollapsed(p->pid, !expanded);
        }
        Rebuild();
    }

    void TLOnSelChanged(void)
    {
        Frame_UpdateCommandStates();
    }

    void TLOnDelete(LPARAM data)
    {
        ProcRow* p = RowProc(data);
        if (p) Action_EndTask(hwnd, p, FALSE);
    }

    void TLOnActivate(LPARAM data)
    {
        /* double-click app row -> switch to its window */
        ProcRow* p = RowProc(data);
        if (p && p->mainWnd && IsWindow(p->mainWnd))
        {
            if (IsIconic(p->mainWnd)) ShowWindow(p->mainWnd, SW_RESTORE);
            SetForegroundWindow(p->mainWnd);
        }
    }

    void TLOnContext(LPARAM data, POINT scr)
    {
        ProcRow* p = RowProc(data);
        if (!p) return;

        BOOL eff = Data::IsEfficiency(*p);
        BOOL canAct = p->pid > 4;

        MItem items[] =
        {
            { 1, L"End task", canAct ? 0u : MIF_DISABLED, NULL, 0 },
            { 2, L"Efficiency mode",
              (canAct ? 0u : MIF_DISABLED) | (eff ? MIF_CHECKED : 0u), NULL, 0 },
            { 0, NULL, MIF_SEP, NULL, 0 },
            { 3, L"Open file location",
              (p->x && p->x->path[0]) ? 0u : MIF_DISABLED, NULL, 0 },
            { 4, L"Search online", 0, NULL, 0 },
            { 0, NULL, MIF_SEP, NULL, 0 },
            { 5, L"Go to details", 0, NULL, 0 },
            { 6, L"Properties", (p->x && p->x->path[0]) ? 0u : MIF_DISABLED, NULL, 0 },
        };

        UINT cmd = Menu_Show(hwnd, scr, items, _countof(items));
        switch (cmd)
        {
        case 1: Action_EndTask(hwnd, p, FALSE); break;
        case 2: Action_ToggleEfficiency(hwnd, p); break;
        case 3: Data::OpenFileLocation(*p); break;
        case 4:
        {
            WCHAR term[192];
            DisplayName(*p, term, _countof(term));
            WCHAR q[256];
            StringCchPrintfW(q, _countof(q), L"%s %s", p->image, term);
            Data::SearchOnline(q);
            break;
        }
        case 5: Frame_SwitchToDetails(p->pid); break;
        case 6: Data::ShowFileProperties(*p); break;
        }
    }

    /* ---------- Page ---------- */

    void BuildCommands(BtnStrip& s)
    {
        s.Add(CMD_RUNTASK, L"Run new task", IC_RUNTASK, BS_OUTLINE);
        s.Add(CMD_ENDTASK, L"End task", IC_ENDTASK, BS_ACCENT, FALSE);
        s.Add(CMD_EFFICIENCY, L"Efficiency mode", IC_LEAF, BS_SUBTLE, FALSE);
    }

    void UpdateCommands(BtnStrip& s)
    {
        ProcRow* p = RowProc(TL_GetSelData(tl));
        s.SetEnabled(CMD_ENDTASK, p && p->pid > 4);
        s.SetEnabled(CMD_EFFICIENCY, p && p->pid > 4);
    }

    void OnCommand(int id)
    {
        ProcRow* p = RowProc(TL_GetSelData(tl));
        switch (id)
        {
        case CMD_RUNTASK:
        {
            WCHAR cmd[1024];
            BOOL admin = FALSE;
            if (Dlg_RunTask(g_app.hFrame, cmd, _countof(cmd), &admin))
            {
                if (!Data::RunTask(cmd, admin))
                {
                    ConfirmOpts o = { L"Create new task",
                                      L"The task could not be started.",
                                      L"OK", NULL, NULL, NULL, FALSE };
                    Dlg_Confirm(g_app.hFrame, o);
                }
            }
            break;
        }
        case CMD_ENDTASK:
            Action_EndTask(hwnd, p, FALSE);
            break;
        case CMD_EFFICIENCY:
            Action_ToggleEfficiency(hwnd, p);
            break;
        }
    }

    void OnTick()
    {
        Rebuild();
    }

    void OnSearch()
    {
        Rebuild();
    }

    void OnThemeChanged()
    {
        TL_Refresh(tl);
    }

    HWND Create(HWND parent);
};

ProcessesPage::SortCtx ProcessesPage::s_sc;

static ProcessesPage* s_page;

static LRESULT CALLBACK PgProcsProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
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

HWND ProcessesPage::Create(HWND parent)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = PgProcsProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PGP_CLASS;
    RegisterClassW(&wc);

    hwnd = CreateWindowExW(0, PGP_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN,
                           0, 0, 100, 100, parent, NULL, g_app.hInst, NULL);
    tl = TL_Create(hwnd, this, TRUE);

    TLColumn cols[6];
    ZeroMemory(cols, sizeof(cols));
    cols[0].id = PC_NAME;   StringCchCopyW(cols[0].title, 48, L"Name");
    cols[0].width = S(320); cols[0].minWidth = S(200);
    cols[1].id = PC_STATUS; StringCchCopyW(cols[1].title, 48, L"Status");
    cols[1].width = S(120); cols[1].minWidth = S(70);
    cols[2].id = PC_CPU;    StringCchCopyW(cols[2].title, 48, L"CPU");
    cols[2].width = S(80);  cols[2].minWidth = S(60);
    cols[2].flags = TLC_RIGHT | TLC_HEAT;
    cols[3].id = PC_MEM;    StringCchCopyW(cols[3].title, 48, L"Memory");
    cols[3].width = S(110); cols[3].minWidth = S(80);
    cols[3].flags = TLC_RIGHT | TLC_HEAT;
    cols[4].id = PC_DISK;   StringCchCopyW(cols[4].title, 48, L"Disk");
    cols[4].width = S(100); cols[4].minWidth = S(80);
    cols[4].flags = TLC_RIGHT | TLC_HEAT;
    cols[5].id = PC_NET;    StringCchCopyW(cols[5].title, 48, L"Network");
    cols[5].width = S(100); cols[5].minWidth = S(80);
    cols[5].flags = TLC_RIGHT | TLC_HEAT;
    TL_SetColumns(tl, cols, 6);
    TL_SetSort(tl, PC_NAME, FALSE);
    TL_SetEmptyText(tl, L"No matching processes");

    Rebuild();
    return hwnd;
}

Page* CreateProcessesPage(void)
{
    s_page = new ProcessesPage();
    return s_page;
}
