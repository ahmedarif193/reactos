/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     App history page (accumulated resource usage)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define PGH_CLASS L"TM11PageHist"

enum { HC_NAME = 0, HC_CPUTIME, HC_NET, HC_NOTIFICATIONS };

struct AppHistoryPage : Page, ITreeListOwner
{
    HWND tl;
    int  sortCol;
    BOOL sortDesc;
    Vec<TLRow> rows;
    int  infoH;
    ULONGLONG cpuMaximum;
    ULONGLONG networkMaximum;
    ULONGLONG notificationMaximum;
    RECT deleteLink;
    BOOL hotDelete;
    BOOL trackingMouse;

    AppHistoryPage() : tl(NULL), sortCol(HC_CPUTIME), sortDesc(TRUE), infoH(0),
                       cpuMaximum(0), networkMaximum(0), notificationMaximum(0),
                       hotDelete(FALSE), trackingMouse(FALSE)
    {
        SetRectEmpty(&deleteLink);
    }

    const WCHAR* Title() { return L"App history"; }

    static AppHistRow* AH(LPARAM data) { return (AppHistRow*)data; }

    struct SortCtx { int col; BOOL desc; };
    static SortCtx s_sc;

    static int CompareUnsigned(ULONGLONG a, ULONGLONG b)
    {
        return a > b ? 1 : a < b ? -1 : 0;
    }

    static int __cdecl Cmp(const void* a, const void* b)
    {
        const AppHistRow* ha = *(const AppHistRow* const*)a;
        const AppHistRow* hb = *(const AppHistRow* const*)b;
        int result = 0;
        switch (s_sc.col)
        {
        case HC_CPUTIME:
            result = CompareUnsigned((ULONGLONG)ha->cpu100ns,
                                     (ULONGLONG)hb->cpu100ns);
            break;
        case HC_NET:
            result = CompareUnsigned(ha->netBytes, hb->netBytes);
            break;
        case HC_NOTIFICATIONS:
            result = CompareUnsigned(ha->notificationBytes,
                                     hb->notificationBytes);
            break;
        default:
            result = lstrcmpiW(ha->displayName, hb->displayName);
            break;
        }
        if (result == 0)
            result = lstrcmpiW(ha->displayName, hb->displayName);
        return s_sc.desc ? -result : result;
    }

    void Rebuild(void)
    {
        rows.Clear();
        cpuMaximum = networkMaximum = notificationMaximum = 0;
        Vec<AppHistRow>& hist = Data::AppHistory();
        Vec<AppHistRow*> list;
        for (int i = 0; i < hist.n; i++)
        {
            list.Push(&hist[i]);
            if ((ULONGLONG)hist[i].cpu100ns > cpuMaximum)
                cpuMaximum = hist[i].cpu100ns;
            if (hist[i].netBytes > networkMaximum)
                networkMaximum = hist[i].netBytes;
            if (hist[i].notificationBytes > notificationMaximum)
                notificationMaximum = hist[i].notificationBytes;
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
            const WCHAR* identity = list[i]->path[0] ? list[i]->path
                                                     : list[i]->image;
            for (const WCHAR* c = identity; *c; c++)
                k = k * 33 + *c;
            r->key = (k << 3);
            r->data = (LPARAM)list[i];
            r->kind = TLR_ITEM;
        }
        TL_SetRows(tl, rows.p, rows.n);
    }

    /* ---------- ITreeListOwner ---------- */

    static void FmtHistoryBytes(ULONGLONG bytes, WCHAR* buf, int cch)
    {
        if (bytes >= 1024ULL * 1024 * 1024)
        {
            StringCchPrintfW(buf, cch, L"%.1f GB",
                             bytes / (1024.0 * 1024.0 * 1024.0));
        }
        else
        {
            double megabytes = bytes / (1024.0 * 1024.0);
            if (megabytes < 0.05)
                StringCchCopyW(buf, cch, L"0 MB");
            else
                StringCchPrintfW(buf, cch, L"%.1f MB", megabytes);
        }
    }

    void TLCellText(LPARAM data, int col, WCHAR* buf, int cch)
    {
        buf[0] = 0;
        AppHistRow* a = AH(data);
        if (!a) return;
        switch (col)
        {
        case HC_NAME:
            StringCchCopyW(buf, cch, a->displayName[0] ? a->displayName
                                                       : a->image);
            break;
        case HC_CPUTIME: FmtCpuTime(a->cpu100ns, buf, cch); break;
        case HC_NET:
            if (Data::AppHistoryNetworkAvailable())
                FmtHistoryBytes(a->netBytes, buf, cch);
            else
                StringCchCopyW(buf, cch, L"Unavailable");
            break;
        case HC_NOTIFICATIONS:
            if (Data::AppHistoryNotificationsAvailable())
                FmtHistoryBytes(a->notificationBytes, buf, cch);
            else
                StringCchCopyW(buf, cch, L"Unavailable");
            break;
        }
    }

    static double HistoryHeat(ULONGLONG value, ULONGLONG maximum)
    {
        if (!maximum)
            return 0.0;
        double heat = (double)value / maximum;
        return 0.10 + heat * 0.90;
    }

    double TLCellHeat(LPARAM data, int col)
    {
        AppHistRow* a = AH(data);
        if (!a) return -1.0;
        switch (col)
        {
        case HC_CPUTIME:
            return HistoryHeat(a->cpu100ns, cpuMaximum);
        case HC_NET:
            return Data::AppHistoryNetworkAvailable() ?
                   HistoryHeat(a->netBytes, networkMaximum) : 0.0;
        case HC_NOTIFICATIONS:
            return Data::AppHistoryNotificationsAvailable() ?
                   HistoryHeat(a->notificationBytes, notificationMaximum) : 0.0;
        }
        return -1.0;
    }

    COLORREF TLCellHeatBackground(LPARAM data, int col, double heat)
    {
        (void)data;
        (void)col;
        COLORREF low = g_t.dark ? RGB(0x18, 0x31, 0x3D)
                                : RGB(0xD9, 0xF0, 0xFA);
        COLORREF high = g_t.dark ? RGB(0x0D, 0x70, 0x9D)
                                 : RGB(0x52, 0xBE, 0xEB);
        return Blend(low, high, (int)(heat * 100.0));
    }

    COLORREF TLCellHeatText(LPARAM data, int col, double heat)
    {
        (void)data;
        (void)col;
        (void)heat;
        return g_t.textMain;
    }

    HICON TLRowIcon(LPARAM data)
    {
        AppHistRow* a = AH(data);
        if (!a) return NULL;
        if (a->icon)
            return a->icon;
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

    void OpenHistoryApp(AppHistRow* app)
    {
        if (!app || !app->path[0])
            return;
        if (!Data::OpenAppHistory(*app))
        {
            ConfirmOpts error = { L"Open app",
                                  L"The selected app could not be opened.",
                                  L"OK", NULL, NULL, NULL, FALSE };
            Dlg_Confirm(g_app.hFrame, error);
        }
    }

    void DeleteHistory(void)
    {
        Data::ClearAppHistory();
        Rebuild();
        InvalidateRect(hwnd, NULL, FALSE);
        Frame_UpdateCommandStates();
    }

    void RunNewTask(void)
    {
        WCHAR command[1024];
        BOOL admin = FALSE;
        if (Dlg_RunTask(g_app.hFrame, command, _countof(command), &admin))
            Data::RunTask(command, admin);
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
            { 1, L"Open app", a->path[0] ? 0u : MIF_DISABLED, NULL, 0 },
            { 2, L"Search online", 0, NULL, 0 },
            { 0, NULL, MIF_SEP, NULL, 0 },
            { 3, L"Delete usage history", 0, NULL, 0 },
        };
        UINT cmd = Menu_Show(hwnd, scr, items, _countof(items));
        if (cmd == 1) OpenHistoryApp(a);
        if (cmd == 2)
            Data::SearchOnline(a->displayName[0] ? a->displayName : a->image);
        if (cmd == 3) DeleteHistory();
    }

    void TLOnActivate(LPARAM data) { OpenHistoryApp(AH(data)); }
    void TLOnSelChanged(void) { Frame_UpdateCommandStates(); }

    /* ---------- Page ---------- */

    void BuildCommands(BtnStrip& s)
    {
        s.Add(CMD_RUNTASK, L"Run new task", IC_RUNTASK, BS_OUTLINE);
        s.Add(CMD_OPENAPP, L"Open app", IC_OPENAPP, BS_SUBTLE, FALSE);
    }

    void UpdateCommands(BtnStrip& s)
    {
        AppHistRow* app = AH(TL_GetSelData(tl));
        s.SetEnabled(CMD_OPENAPP, app && app->path[0]);
    }

    void OnCommand(int id)
    {
        if (id == CMD_RUNTASK)
            RunNewTask();
        else if (id == CMD_OPENAPP)
            OpenHistoryApp(AH(TL_GetSelData(tl)));
    }

    void OnTick() { Rebuild(); Frame_UpdateCommandStates(); }
    void OnThemeChanged() { InvalidateRect(hwnd, NULL, TRUE); TL_Refresh(tl); }

    void PaintInfo(HDC dc, const RECT& rc)
    {
        FILETIME since, localSince;
        SYSTEMTIME systemTime;
        WCHAR date[64] = L"";
        Data::GetAppHistorySince(&since);
        if (FileTimeToLocalFileTime(&since, &localSince) &&
            FileTimeToSystemTime(&localSince, &systemTime))
        {
            GetDateFormatW(LOCALE_USER_DEFAULT, DATE_SHORTDATE, &systemTime,
                           NULL, date, _countof(date));
        }
        if (!date[0])
            StringCchCopyW(date, _countof(date), L"today");

        WCHAR text[256];
        StringCchPrintfW(text, _countof(text),
                         L"Resource usage since %s for current user account.",
                         date);
        RECT description = { rc.left + S(18), rc.top + S(7),
                             rc.right - S(18), rc.top + S(28) };
        DrawTextClip(dc, text, description, g_t.fBody, g_t.textMain,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);

        const WCHAR* linkText = L"Delete usage history";
        SIZE extent = { S(120), S(18) };
        HGDIOBJ oldFont = SelectObject(dc, g_t.fBody);
        GetTextExtentPoint32W(dc, linkText, lstrlenW(linkText), &extent);
        SelectObject(dc, oldFont);
        deleteLink.left = description.left;
        deleteLink.top = rc.top + S(30);
        deleteLink.right = deleteLink.left + extent.cx;
        deleteLink.bottom = deleteLink.top + S(23);
        DrawTextClip(dc, linkText, deleteLink, g_t.fBody, g_t.accent,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER);
        if (hotDelete)
            DrawHLine(dc, deleteLink.bottom - S(2), deleteLink.left,
                      deleteLink.right, g_t.accent);
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
    case WM_MOUSEMOVE:
        if (s_page)
        {
            if (!s_page->trackingMouse)
            {
                TRACKMOUSEEVENT tracking =
                    { sizeof(tracking), TME_LEAVE, hwnd, 0 };
                if (TrackMouseEvent(&tracking))
                    s_page->trackingMouse = TRUE;
            }
            POINT point = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            BOOL hot = PtInRect(&s_page->deleteLink, point);
            if (hot != s_page->hotDelete)
            {
                s_page->hotDelete = hot;
                InvalidateRect(hwnd, &s_page->deleteLink, FALSE);
            }
        }
        return 0;
    case WM_SETCURSOR:
        if (s_page && LOWORD(lp) == HTCLIENT)
        {
            POINT point;
            GetCursorPos(&point);
            ScreenToClient(hwnd, &point);
            if (PtInRect(&s_page->deleteLink, point))
            {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    case WM_MOUSELEAVE:
        if (s_page)
        {
            s_page->trackingMouse = FALSE;
            s_page->hotDelete = FALSE;
            InvalidateRect(hwnd, &s_page->deleteLink, FALSE);
        }
        return 0;
    case WM_LBUTTONUP:
        if (s_page)
        {
            POINT point = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            if (PtInRect(&s_page->deleteLink, point))
                s_page->DeleteHistory();
        }
        return 0;
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

    infoH = S(62);
    hwnd = CreateWindowExW(0, PGH_CLASS, L"", WS_CHILD | WS_CLIPCHILDREN,
                           0, 0, 100, 100, parent, NULL, g_app.hInst, NULL);
    tl = TL_Create(hwnd, this, FALSE);

    TLColumn cols[4];
    ZeroMemory(cols, sizeof(cols));
    cols[0].id = HC_NAME;    StringCchCopyW(cols[0].title, 48, L"Name");
    cols[0].width = S(300);  cols[0].minWidth = S(180);
    cols[1].id = HC_CPUTIME; StringCchCopyW(cols[1].title, 48, L"CPU time");
    cols[1].width = S(130);  cols[1].minWidth = S(90);
    cols[1].flags = TLC_RIGHT | TLC_HEAT;
    cols[2].id = HC_NET;     StringCchCopyW(cols[2].title, 48, L"Network");
    cols[2].width = S(130);  cols[2].minWidth = S(90);
    cols[2].flags = TLC_RIGHT | TLC_HEAT;
    cols[3].id = HC_NOTIFICATIONS;
    StringCchCopyW(cols[3].title, 48, L"Notifications");
    cols[3].width = S(140);  cols[3].minWidth = S(110);
    cols[3].flags = TLC_RIGHT | TLC_HEAT;
    TL_SetColumns(tl, cols, 4);
    TL_SetSort(tl, HC_CPUTIME, TRUE);
    TL_SetEmptyText(tl, L"No app usage history is available");

    Rebuild();
    return hwnd;
}

Page* CreateAppHistoryPage(void)
{
    s_page = new AppHistoryPage();
    return s_page;
}
