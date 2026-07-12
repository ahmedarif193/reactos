/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     App history page (accumulated resource usage)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define PGH_CLASS L"TM11PageHist"

enum { HC_NAME = 0, HC_CPUTIME, HC_NET };

struct AppHistoryPage : Page, ITreeListOwner
{
    HWND tl;
    int  sortCol;
    BOOL sortDesc;
    Vec<TLRow> rows;
    int  infoH;

    AppHistoryPage() : tl(NULL), sortCol(HC_CPUTIME), sortDesc(TRUE), infoH(0) {}

    const WCHAR* Title() { return L"App history"; }
    BOOL WantSearch() { return TRUE; }
    const WCHAR* SearchHint() { return L"Type a name to search"; }

    static AppHistRow* AH(LPARAM data) { return (AppHistRow*)data; }

    BOOL MatchesSearch(const AppHistRow& a)
    {
        if (!g_app.search[0]) return TRUE;
        return StrStrIW(a.image, g_app.search) != NULL;
    }

    struct SortCtx { int col; BOOL desc; };
    static SortCtx s_sc;

    static int __cdecl Cmp(const void* a, const void* b)
    {
        const AppHistRow* ha = *(const AppHistRow* const*)a;
        const AppHistRow* hb = *(const AppHistRow* const*)b;
        LONGLONG d = 0;
        switch (s_sc.col)
        {
        case HC_CPUTIME: d = ha->cpu100ns - hb->cpu100ns; break;
        case HC_NET:     d = (LONGLONG)ha->netBytes - (LONGLONG)hb->netBytes; break;
        default:         d = lstrcmpiW(ha->image, hb->image); break;
        }
        if (d == 0) d = lstrcmpiW(ha->image, hb->image);
        int r = (d > 0) ? 1 : (d < 0) ? -1 : 0;
        return s_sc.desc ? -r : r;
    }

    void Rebuild(void)
    {
        rows.Clear();
        Vec<AppHistRow>& hist = Data::AppHistory();
        Vec<AppHistRow*> list;
        for (int i = 0; i < hist.n; i++)
        {
            /* only meaningful entries (>= 1s cpu) to keep the list tidy */
            if (hist[i].cpu100ns < 10000000LL) continue;
            if (!MatchesSearch(hist[i])) continue;
            list.Push(&hist[i]);
        }
        s_sc.col = sortCol;
        s_sc.desc = sortDesc;
        if (list.n > 1)
            qsort(list.p, list.n, sizeof(AppHistRow*), Cmp);

        for (int i = 0; i < list.n; i++)
        {
            TLRow* r = rows.Add();
            if (!r) break;
            LONGLONG k = 5381;
            for (const WCHAR* c = list[i]->image; *c; c++)
                k = k * 33 + *c;
            r->key = (k << 3);
            r->data = (LPARAM)list[i];
            r->kind = TLR_ITEM;
        }
        TL_SetRows(tl, rows.p, rows.n);
    }

    /* ---------- ITreeListOwner ---------- */

    void TLCellText(LPARAM data, int col, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        AppHistRow* a = AH(data);
        if (!a) return;
        switch (col)
        {
        case HC_NAME:    StringCchCopyW(buf, cch, a->image); break;
        case HC_CPUTIME: FmtCpuTime(a->cpu100ns, buf, cch); break;
        case HC_NET:     FmtBytes(a->netBytes, buf, cch); break;
        }
    }

    HICON TLRowIcon(LPARAM data)
    {
        AppHistRow* a = AH(data);
        if (!a) return NULL;
        /* borrow the icon from a live instance if one exists */
        for (int i = 0; i < Data::g.procs.n; i++)
        {
            if (lstrcmpiW(Data::g.procs[i].image, a->image) == 0 &&
                Data::g.procs[i].x && Data::g.procs[i].x->icon)
                return Data::g.procs[i].x->icon;
        }
        return NULL;
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

    void TLOnContext(LPARAM data, POINT scr)
    {
        AppHistRow* a = AH(data);
        if (!a) return;
        MItem items[] =
        {
            { 1, L"Search online", 0, NULL, 0 },
            { 2, L"Delete usage history", 0, NULL, 0 },
        };
        UINT cmd = Menu_Show(hwnd, scr, items, _countof(items));
        if (cmd == 1) Data::SearchOnline(a->image);
        if (cmd == 2)
        {
            Data::ClearAppHistory();
            Rebuild();
        }
    }

    /* ---------- Page ---------- */

    void BuildCommands(BtnStrip& s)
    {
        s.Add(CMD_DELHISTORY, L"Delete usage history", IC_ENDTASK, BS_OUTLINE);
    }

    void OnCommand(int id)
    {
        if (id == CMD_DELHISTORY)
        {
            Data::ClearAppHistory();
            Rebuild();
        }
    }

    void OnTick() { Rebuild(); }
    void OnSearch() { Rebuild(); }
    void OnThemeChanged() { InvalidateRect(hwnd, NULL, TRUE); TL_Refresh(tl); }

    void PaintInfo(HDC dc, const RECT& rc)
    {
        RECT ir = { rc.left + S(4), rc.top, rc.right - S(4), rc.top + infoH };
        WCHAR text[256];
        WCHAR up[64];
        FmtUptime(Data::g.upSeconds, up, _countof(up));
        StringCchPrintfW(text, _countof(text),
            L"Resource usage accumulated since Task Manager was started. "
            L"System up time: %s", up);
        DrawTextClip(dc, text, ir, g_t.fSmall, g_t.textSec,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
    }

    HWND Create(HWND parent);
};

AppHistoryPage::SortCtx AppHistoryPage::s_sc;

static AppHistoryPage* s_page;

static LRESULT CALLBACK PgHistProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg)
    {
    case WM_SIZE:
        if (s_page && s_page->tl)
            MoveWindow(s_page->tl, 0, s_page->infoH, LOWORD(lp),
                       HIWORD(lp) - s_page->infoH, TRUE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        FillRect32(dc, ps.rcPaint, g_t.listBg);
        if (s_page) s_page->PaintInfo(dc, rc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

HWND AppHistoryPage::Create(HWND parent)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = PgHistProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = PGH_CLASS;
    RegisterClassW(&wc);

    infoH = S(26);
    hwnd = CreateWindowExW(0, PGH_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN,
                           0, 0, 100, 100, parent, NULL, g_app.hInst, NULL);
    tl = TL_Create(hwnd, this, FALSE);

    TLColumn cols[3];
    ZeroMemory(cols, sizeof(cols));
    cols[0].id = HC_NAME;    StringCchCopyW(cols[0].title, 48, L"Name");
    cols[0].width = S(340);  cols[0].minWidth = S(180);
    cols[1].id = HC_CPUTIME; StringCchCopyW(cols[1].title, 48, L"CPU time");
    cols[1].width = S(130);  cols[1].minWidth = S(90); cols[1].flags = TLC_RIGHT;
    cols[2].id = HC_NET;     StringCchCopyW(cols[2].title, 48, L"Network");
    cols[2].width = S(130);  cols[2].minWidth = S(90); cols[2].flags = TLC_RIGHT;
    TL_SetColumns(tl, cols, 3);
    TL_SetSort(tl, HC_CPUTIME, TRUE);
    TL_SetEmptyText(tl, L"Usage history will appear here as apps run");

    Rebuild();
    return hwnd;
}

Page* CreateAppHistoryPage(void)
{
    s_page = new AppHistoryPage();
    return s_page;
}
