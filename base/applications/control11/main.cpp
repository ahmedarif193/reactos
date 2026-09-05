/*
 * PROJECT:     ReactOS Control Panel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Control Panel frame: navigation bar, navigation pane,
 *              category home, category pages, all items, search
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "control11.h"

#define FRAME_CLASS     L"Control11Frame"
#define IDC_SEARCH      1001
#define TIMER_SEARCH    1
#define TIMER_TIP       2
#define IDM_VIEW_CATEGORY 2001
#define IDM_VIEW_LARGE    2002
#define IDM_VIEW_SMALL    2003

CPFRAME g_f;
static WNDPROC s_SearchProc;
static BOOL s_bSearchHasText;
static int s_TipLink = -1;

int S(int px)
{
    return MulDiv(px, g_f.dpi, 96);
}

COLORREF Blend(COLORREF a, COLORREF b, int pctB)
{
    int pa = 100 - pctB;
    return RGB((GetRValue(a) * pa + GetRValue(b) * pctB) / 100,
               (GetGValue(a) * pa + GetGValue(b) * pctB) / 100,
               (GetBValue(a) * pa + GetBValue(b) * pctB) / 100);
}

static BOOL PrefersDark(void)
{
    DWORD v = 1, cb = sizeof(v);
    if (SHGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    L"AppsUseLightTheme", NULL, &v, &cb) == ERROR_SUCCESS)
        return v == 0;
    COLORREF c = GetSysColor(COLOR_WINDOW);
    return (GetRValue(c) * 299 + GetGValue(c) * 587 + GetBValue(c) * 114) / 1000 < 128;
}

static void InitPalette(void)
{
    CPPALETTE *p = &g_f.pal;
    p->dark = PrefersDark();
    if (p->dark)
    {
        p->Window = RGB(32, 32, 32);
        p->NavPane = RGB(43, 43, 43);
        p->NavBorder = RGB(60, 60, 60);
        p->Text = RGB(235, 235, 235);
        p->DimText = RGB(160, 160, 160);
        p->Link = RGB(110, 170, 255);
        p->LinkHot = RGB(150, 195, 255);
        p->Disabled = RGB(110, 110, 110);
        p->Header = RGB(140, 180, 240);
        p->Bar = RGB(43, 43, 43);
        p->BarBorder = RGB(24, 24, 24);
        p->Edit = RGB(25, 25, 25);
        p->Rule = RGB(70, 70, 70);
        p->Accent = RGB(96, 155, 235);
    }
    else
    {
        p->Window = RGB(255, 255, 255);
        p->NavPane = RGB(245, 248, 252);
        p->NavBorder = RGB(217, 225, 233);
        p->Text = RGB(0, 0, 0);
        p->DimText = RGB(96, 96, 96);
        p->Link = RGB(6, 99, 214);
        p->LinkHot = RGB(51, 153, 255);
        p->Disabled = RGB(150, 150, 150);
        p->Header = RGB(30, 57, 91);
        p->Bar = RGB(240, 244, 249);
        p->BarBorder = RGB(200, 210, 222);
        p->Edit = RGB(255, 255, 255);
        p->Rule = RGB(220, 226, 233);
        p->Accent = RGB(51, 153, 255);
    }
}

static HFONT MakeFont(int pt, int weight, BOOL underline)
{
    static WCHAR s_face[LF_FACESIZE];
    if (!s_face[0])
    {
        const WCHAR *tries[] = { L"Segoe UI", L"Tahoma", L"MS Shell Dlg 2" };
        HDC dc = GetDC(NULL);
        StringCchCopyW(s_face, LF_FACESIZE, tries[2]);
        for (int i = 0; i < (int)_countof(tries); i++)
        {
            LOGFONTW lf = { 0 };
            lf.lfHeight = -12;
            lf.lfCharSet = DEFAULT_CHARSET;
            StringCchCopyW(lf.lfFaceName, LF_FACESIZE, tries[i]);
            HFONT f = CreateFontIndirectW(&lf);
            HGDIOBJ old = SelectObject(dc, f);
            WCHAR got[LF_FACESIZE] = L"";
            GetTextFaceW(dc, LF_FACESIZE, got);
            SelectObject(dc, old);
            DeleteObject(f);
            if (!lstrcmpiW(got, tries[i]))
            {
                StringCchCopyW(s_face, LF_FACESIZE, tries[i]);
                break;
            }
        }
        ReleaseDC(NULL, dc);
    }
    LOGFONTW lf = { 0 };
    lf.lfHeight = -MulDiv(pt, g_f.dpi, 72);
    lf.lfWeight = weight;
    lf.lfUnderline = underline ? TRUE : FALSE;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    StringCchCopyW(lf.lfFaceName, LF_FACESIZE, s_face);
    return CreateFontIndirectW(&lf);
}

static void InitFonts(void)
{
    if (g_f.hfText) DeleteObject(g_f.hfText);
    if (g_f.hfLink) DeleteObject(g_f.hfLink);
    if (g_f.hfBold) DeleteObject(g_f.hfBold);
    if (g_f.hfHeader) DeleteObject(g_f.hfHeader);
    if (g_f.hfTitle) DeleteObject(g_f.hfTitle);
    if (g_f.hfSmall) DeleteObject(g_f.hfSmall);
    g_f.hfText = MakeFont(9, FW_NORMAL, FALSE);
    g_f.hfLink = MakeFont(9, FW_NORMAL, TRUE);
    g_f.hfBold = MakeFont(9, FW_BOLD, FALSE);
    g_f.hfHeader = MakeFont(12, FW_NORMAL, FALSE);
    g_f.hfTitle = MakeFont(11, FW_NORMAL, FALSE);
    g_f.hfSmall = MakeFont(8, FW_NORMAL, FALSE);
}

HICON LoadFluent(UINT nId, int cx)
{
    static struct { UINT nId; int cx; HICON hIcon; } s_Cache[96];
    for (UINT i = 0; i < _countof(s_Cache); i++)
        if (s_Cache[i].nId == nId && s_Cache[i].cx == cx)
            return s_Cache[i].hIcon;
    static const int s_Sizes[] = { 16, 20, 24, 28, 32, 40, 48, 64, 96, 128, 256 };
    int best = s_Sizes[0];
    for (UINT i = 0; i < _countof(s_Sizes); i++)
        if (s_Sizes[i] <= cx) best = s_Sizes[i];
    HICON hIcon = (HICON)LoadImageW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(nId), IMAGE_ICON, best, best, 0);
    for (UINT i = 0; i < _countof(s_Cache); i++)
    {
        if (!s_Cache[i].nId)
        {
            s_Cache[i].nId = nId;
            s_Cache[i].cx = cx;
            s_Cache[i].hIcon = hIcon;
            break;
        }
    }
    return hIcon;
}

void DrawFluent(HDC hdc, const RECT *prc, UINT nId)
{
    int cx = prc->right - prc->left, cy = prc->bottom - prc->top;
    int n = min(cx, cy);
    HICON hIcon = LoadFluent(nId, n);
    if (hIcon)
        DrawIconEx(hdc, prc->left + (cx - n) / 2, prc->top + (cy - n) / 2, hIcon, n, n, 0, NULL, DI_NORMAL);
}

void DrawIconAt(HDC hdc, int x, int y, HICON hIcon, int cx, BOOL bEnabled)
{
    if (!hIcon) return;
    if (bEnabled)
    {
        DrawIconEx(hdc, x, y, hIcon, cx, cx, 0, NULL, DI_NORMAL);
        return;
    }
    HDC hdcMem = CreateCompatibleDC(hdc);
    BITMAPINFO bmi = { 0 };
    bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
    bmi.bmiHeader.biWidth = cx;
    bmi.bmiHeader.biHeight = -cx;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    PULONG pBits = NULL;
    HBITMAP hbm = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, (void **)&pBits, NULL, 0);
    if (hbm && pBits)
    {
        HGDIOBJ old = SelectObject(hdcMem, hbm);
        BitBlt(hdcMem, 0, 0, cx, cx, hdc, x, y, SRCCOPY);
        for (int i = 0; i < cx * cx; i++) pBits[i] |= 0xFF000000;
        DrawIconEx(hdcMem, 0, 0, hIcon, cx, cx, 0, NULL, DI_NORMAL);
        COLORREF bg = g_f.pal.Window;
        for (int i = 0; i < cx * cx; i++)
        {
            ULONG c = pBits[i];
            int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
            int lum = (r * 299 + g * 587 + b * 114) / 1000;
            int nr = (lum * 40 + GetRValue(bg) * 60) / 100;
            int ng = (lum * 40 + GetGValue(bg) * 60) / 100;
            int nb = (lum * 40 + GetBValue(bg) * 60) / 100;
            pBits[i] = 0xFF000000 | (nr << 16) | (ng << 8) | nb;
        }
        BitBlt(hdc, x, y, cx, cx, hdcMem, 0, 0, SRCCOPY);
        SelectObject(hdcMem, old);
        DeleteObject(hbm);
    }
    DeleteDC(hdcMem);
}

int TextH(HDC hdc, HFONT hf)
{
    TEXTMETRICW tm;
    HGDIOBJ old = SelectObject(hdc, hf);
    GetTextMetricsW(hdc, &tm);
    SelectObject(hdc, old);
    return tm.tmHeight;
}

int TextW(HDC hdc, HFONT hf, LPCWSTR psz)
{
    SIZE sz = { 0, 0 };
    HGDIOBJ old = SelectObject(hdc, hf);
    GetTextExtentPoint32W(hdc, psz, lstrlenW(psz), &sz);
    SelectObject(hdc, old);
    return sz.cx;
}

CPLINK *AddLink(const RECT *prc, int action, int param, void *ptr, LPCWSTR pszArg, LPCWSTR pszTip, BOOL bEnabled)
{
    if (g_f.cLinks >= CP_MAX_LINKS) return NULL;
    CPLINK *pl = &g_f.links[g_f.cLinks++];
    ZeroMemory(pl, sizeof(*pl));
    pl->rc = *prc;
    pl->action = action;
    pl->param = param;
    pl->ptr = ptr;
    pl->bEnabled = bEnabled;
    if (pszArg) StringCchCopyW(pl->szArg, _countof(pl->szArg), pszArg);
    if (pszTip) StringCchCopyW(pl->szTip, _countof(pl->szTip), pszTip);
    return pl;
}

int DrawLinkText(HDC hdc, int x, int y, int cxMax, LPCWSTR psz, HFONT hf, int action, int param, void *ptr, LPCWSTR pszArg, LPCWSTR pszTip, BOOL bEnabled)
{
    int idx = g_f.cLinks;
    BOOL hot = bEnabled && g_f.hotLink == idx;
    HFONT hfUse = hf;
    if (hot && hf == g_f.hfText) hfUse = g_f.hfLink;
    HGDIOBJ old = SelectObject(hdc, hfUse);
    SetTextColor(hdc, bEnabled ? (hot ? g_f.pal.LinkHot : g_f.pal.Link) : g_f.pal.Disabled);
    SetBkMode(hdc, TRANSPARENT);
    RECT rc = { x, y, x + cxMax, y + S(400) };
    DrawTextW(hdc, psz, -1, &rc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
    if (rc.right > x + cxMax) rc.right = x + cxMax;
    DrawTextW(hdc, psz, -1, &rc, DT_LEFT | DT_TOP | DT_WORDBREAK);
    if (hot && hfUse != g_f.hfLink)
    {
        HPEN pen = CreatePen(PS_SOLID, 1, g_f.pal.LinkHot);
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
        LineTo(hdc, rc.right, rc.bottom - 1);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
    SelectObject(hdc, old);
    AddLink(&rc, action, param, ptr, pszArg, pszTip, bEnabled);
    return rc.bottom - rc.top;
}

int DrawWrapped(HDC hdc, int x, int y, int cx, LPCWSTR psz, HFONT hf, COLORREF cr)
{
    HGDIOBJ old = SelectObject(hdc, hf);
    SetTextColor(hdc, cr);
    SetBkMode(hdc, TRANSPARENT);
    RECT rc = { x, y, x + cx, y + S(600) };
    DrawTextW(hdc, psz, -1, &rc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
    rc.right = x + cx;
    DrawTextW(hdc, psz, -1, &rc, DT_LEFT | DT_TOP | DT_WORDBREAK);
    SelectObject(hdc, old);
    return rc.bottom - rc.top;
}

/* ------------------------------------------------------------------ */
/*  Navigation                                                         */
/* ------------------------------------------------------------------ */

void Frame_Invalidate(void)
{
    InvalidateRect(g_f.hwnd, NULL, FALSE);
}

static void UpdateTitle(void)
{
    WCHAR szTitle[256] = L"Control Panel";
    const CPNAVVIEW *v = &g_f.view;
    if (v->kind == NV_CATEGORY)
    {
        int idx = Catalog_CategoryIndex(v->category);
        if (idx >= 0) StringCchCopyW(szTitle, _countof(szTitle), g_Categories[idx].pszName);
    }
    else if (v->kind == NV_ALL)
        StringCchCopyW(szTitle, _countof(szTitle), L"All Control Panel Items");
    else if (v->kind == NV_SEARCH)
        StringCchPrintfW(szTitle, _countof(szTitle), L"Search Results - %s", v->szQuery);
    else if (v->kind == NV_HUB)
        StringCchCopyW(szTitle, _countof(szTitle), Hub_Title(v->hub, v->szPage));
    SetWindowTextW(g_f.hwnd, szTitle);
}

static void ApplyView(const CPNAVVIEW *pView)
{
    g_f.view = *pView;
    g_f.hotLink = -1;
    g_f.bViewMenuOpen = FALSE;
    if (pView->kind != NV_SEARCH)
    {
        s_bSearchHasText = FALSE;
        SetWindowTextW(g_f.hwndSearch, L"");
    }
    Hub_ViewChanged(pView->kind == NV_HUB ? pView->hub : HUB_NONE, pView->szPage);
    UpdateTitle();
    Frame_Invalidate();
}

void Frame_Navigate(const CPNAVVIEW *pView)
{
    if (g_f.cHistory > 0)
        g_f.history[g_f.iHistory] = g_f.view;
    if (g_f.iHistory < g_f.cHistory - 1)
        g_f.cHistory = g_f.iHistory + 1;
    if (g_f.cHistory == CP_MAX_HISTORY)
    {
        memmove(&g_f.history[0], &g_f.history[1], sizeof(CPNAVVIEW) * (CP_MAX_HISTORY - 1));
        g_f.cHistory--;
    }
    g_f.history[g_f.cHistory++] = *pView;
    g_f.iHistory = g_f.cHistory - 1;
    ApplyView(pView);
}

static void GoBack(void)
{
    if (g_f.iHistory <= 0) return;
    g_f.history[g_f.iHistory] = g_f.view;
    g_f.iHistory--;
    ApplyView(&g_f.history[g_f.iHistory]);
}

static void GoForward(void)
{
    if (g_f.iHistory >= g_f.cHistory - 1) return;
    g_f.history[g_f.iHistory] = g_f.view;
    g_f.iHistory++;
    ApplyView(&g_f.history[g_f.iHistory]);
}

static void NavigateHome(void)
{
    CPNAVVIEW v = { 0 };
    v.kind = NV_HOME;
    Frame_Navigate(&v);
}

static void NavigateCategory(int cat)
{
    CPNAVVIEW v = { 0 };
    v.kind = NV_CATEGORY;
    v.category = cat;
    Frame_Navigate(&v);
}

static void NavigateAll(void)
{
    CPNAVVIEW v = { 0 };
    v.kind = NV_ALL;
    Frame_Navigate(&v);
}

static void NavigateSearch(LPCWSTR pszQuery)
{
    if (!pszQuery || !*pszQuery)
    {
        if (g_f.view.kind == NV_SEARCH) GoBack();
        return;
    }
    CPNAVVIEW v = { 0 };
    v.kind = NV_SEARCH;
    StringCchCopyW(v.szQuery, _countof(v.szQuery), pszQuery);
    if (g_f.view.kind == NV_SEARCH)
    {
        g_f.view = v;
        g_f.hotLink = -1;
        UpdateTitle();
        Frame_Invalidate();
        return;
    }
    Frame_Navigate(&v);
}

void Frame_OpenHub(int hub, LPCWSTR pszPage)
{
    CPNAVVIEW v = { 0 };
    v.kind = NV_HUB;
    v.hub = hub;
    if (pszPage) StringCchCopyW(v.szPage, _countof(v.szPage), pszPage);
    Frame_Navigate(&v);
}

/* ------------------------------------------------------------------ */
/*  Layout                                                             */
/* ------------------------------------------------------------------ */

static int BarH(void) { return S(40); }
static int NavW(void) { return S(200); }

static void Layout(void)
{
    RECT rc;
    GetClientRect(g_f.hwnd, &rc);
    g_f.rcBar = rc;
    g_f.rcBar.bottom = rc.top + BarH();
    g_f.rcNav = rc;
    g_f.rcNav.top = g_f.rcBar.bottom;
    g_f.rcNav.right = rc.left + NavW();
    g_f.rcContent = rc;
    g_f.rcContent.top = g_f.rcBar.bottom;
    g_f.rcContent.left = g_f.rcNav.right;
    if (g_f.hwndSearch)
    {
        int w = S(230), h = S(22);
        SetWindowPos(g_f.hwndSearch, NULL, rc.right - S(10) - w + S(4), (BarH() - h) / 2, w - S(28), h, SWP_NOZORDER);
    }
    Hub_Size();
}

/* ------------------------------------------------------------------ */
/*  Painting: navigation bar                                           */
/* ------------------------------------------------------------------ */

static void PaintBar(HDC hdc)
{
    RECT rc = g_f.rcBar;
    HBRUSH br = CreateSolidBrush(g_f.pal.Bar);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
    HPEN pen = CreatePen(PS_SOLID, 1, g_f.pal.BarBorder);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
    LineTo(hdc, rc.right, rc.bottom - 1);

    int btn = S(26);
    int y = rc.top + (BarH() - btn) / 2;
    RECT rcBack = { rc.left + S(8), y, rc.left + S(8) + btn, y + btn };
    RECT rcFwd = { rcBack.right + S(4), y, rcBack.right + S(4) + btn, y + btn };
    BOOL canBack = g_f.iHistory > 0;
    BOOL canFwd = g_f.iHistory < g_f.cHistory - 1;
    int idxBack = g_f.cLinks;
    AddLink(&rcBack, LA_BACK, 0, NULL, NULL, L"Back", canBack);
    int idxFwd = g_f.cLinks;
    AddLink(&rcFwd, LA_FORWARD, 0, NULL, NULL, L"Forward", canFwd);
    for (int i = 0; i < 2; i++)
    {
        RECT r = i ? rcFwd : rcBack;
        BOOL en = i ? canFwd : canBack;
        BOOL hot = en && g_f.hotLink == (i ? idxFwd : idxBack);
        if (hot)
        {
            HBRUSH hb = CreateSolidBrush(Blend(g_f.pal.Bar, g_f.pal.Accent, 25));
            FillRect(hdc, &r, hb);
            DeleteObject(hb);
        }
        RECT ri = { r.left + S(4), r.top + S(4), r.right - S(4), r.bottom - S(4) };
        HICON h = LoadFluent(i ? IDI_NAV_FWD : IDI_NAV_BACK, ri.right - ri.left);
        DrawIconAt(hdc, ri.left, ri.top, h, ri.right - ri.left, en);
    }

    int searchW = S(230);
    RECT rcCrumb = { rcFwd.right + S(10), y, rc.right - S(10) - searchW - S(8), y + btn };
    HBRUSH brEdit = CreateSolidBrush(g_f.pal.Edit);
    FillRect(hdc, &rcCrumb, brEdit);
    HPEN penB = CreatePen(PS_SOLID, 1, g_f.pal.BarBorder);
    SelectObject(hdc, penB);
    HGDIOBJ oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rcCrumb.left, rcCrumb.top, rcCrumb.right, rcCrumb.bottom);
    SelectObject(hdc, oldBr);

    SetBkMode(hdc, TRANSPARENT);
    int cx = rcCrumb.left + S(6);
    int th = TextH(hdc, g_f.hfText);
    int ty = rcCrumb.top + (btn - th) / 2;
    HICON hApp = LoadFluent(IDI_MAIN, S(16));
    DrawIconAt(hdc, cx, rcCrumb.top + (btn - S(16)) / 2, hApp, S(16), TRUE);
    cx += S(20);

    struct { LPCWSTR psz; int action; int param; LPCWSTR arg; } crumbs[4];
    int nCrumbs = 0;
    crumbs[nCrumbs].psz = L"Control Panel"; crumbs[nCrumbs].action = LA_HOME; crumbs[nCrumbs].param = 0; crumbs[nCrumbs].arg = NULL; nCrumbs++;
    const CPNAVVIEW *v = &g_f.view;
    if (v->kind == NV_CATEGORY)
    {
        int idx = Catalog_CategoryIndex(v->category);
        if (idx >= 0) { crumbs[nCrumbs].psz = g_Categories[idx].pszName; crumbs[nCrumbs].action = LA_CATEGORY; crumbs[nCrumbs].param = v->category; crumbs[nCrumbs].arg = NULL; nCrumbs++; }
    }
    else if (v->kind == NV_ALL)
    {
        crumbs[nCrumbs].psz = L"All Control Panel Items"; crumbs[nCrumbs].action = LA_ALL; crumbs[nCrumbs].param = 0; crumbs[nCrumbs].arg = NULL; nCrumbs++;
    }
    else if (v->kind == NV_SEARCH)
    {
        crumbs[nCrumbs].psz = L"Search Results"; crumbs[nCrumbs].action = LA_NONE; crumbs[nCrumbs].param = 0; crumbs[nCrumbs].arg = NULL; nCrumbs++;
    }
    else if (v->kind == NV_HUB)
    {
        CPITEM *pItem = Catalog_FindByCanonical(Hub_Canonical(v->hub));
        if (pItem && pItem->cCategories)
        {
            int idx = Catalog_CategoryIndex(pItem->categories[0]);
            if (idx >= 0) { crumbs[nCrumbs].psz = g_Categories[idx].pszName; crumbs[nCrumbs].action = LA_CATEGORY; crumbs[nCrumbs].param = pItem->categories[0]; crumbs[nCrumbs].arg = NULL; nCrumbs++; }
        }
        crumbs[nCrumbs].psz = Hub_Title(v->hub, NULL); crumbs[nCrumbs].action = LA_HUBPAGE; crumbs[nCrumbs].param = v->hub; crumbs[nCrumbs].arg = L""; nCrumbs++;
        if (v->szPage[0])
        {
            crumbs[nCrumbs].psz = Hub_Title(v->hub, v->szPage); crumbs[nCrumbs].action = LA_NONE; crumbs[nCrumbs].param = 0; crumbs[nCrumbs].arg = NULL; nCrumbs++;
        }
    }
    for (int i = 0; i < nCrumbs; i++)
    {
        int w = TextW(hdc, g_f.hfText, crumbs[i].psz);
        if (cx + w > rcCrumb.right - S(8)) break;
        int idx = g_f.cLinks;
        RECT r = { cx - S(2), rcCrumb.top + S(2), cx + w + S(2), rcCrumb.bottom - S(2) };
        BOOL hot = g_f.hotLink == idx && crumbs[i].action != LA_NONE;
        if (hot)
        {
            HBRUSH hb = CreateSolidBrush(Blend(g_f.pal.Edit, g_f.pal.Accent, 20));
            FillRect(hdc, &r, hb);
            DeleteObject(hb);
        }
        HGDIOBJ oldF = SelectObject(hdc, g_f.hfText);
        SetTextColor(hdc, g_f.pal.Text);
        TextOutW(hdc, cx, ty, crumbs[i].psz, lstrlenW(crumbs[i].psz));
        SelectObject(hdc, oldF);
        AddLink(&r, crumbs[i].action, crumbs[i].param, NULL, crumbs[i].arg, NULL, crumbs[i].action != LA_NONE);
        cx += w + S(4);
        if (i < nCrumbs - 1)
        {
            HICON hc = LoadFluent(IDI_NAV_CRUMB, S(12));
            DrawIconAt(hdc, cx, rcCrumb.top + (btn - S(12)) / 2, hc, S(12), TRUE);
            cx += S(16);
        }
    }

    RECT rcSearch = { rc.right - S(10) - searchW, y, rc.right - S(10), y + btn };
    FillRect(hdc, &rcSearch, brEdit);
    SelectObject(hdc, penB);
    oldBr = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rcSearch.left, rcSearch.top, rcSearch.right, rcSearch.bottom);
    SelectObject(hdc, oldBr);
    RECT rcGlyph = { rcSearch.right - S(22), rcSearch.top + S(5), rcSearch.right - S(6), rcSearch.bottom - S(5) };
    HICON hs = LoadFluent(s_bSearchHasText ? IDI_NAV_CLEAR : IDI_NAV_SEARCH, S(16));
    DrawIconAt(hdc, rcGlyph.left, rcGlyph.top, hs, S(16), TRUE);
    if (s_bSearchHasText)
        AddLink(&rcGlyph, LA_CUSTOM, -1, NULL, L"clearsearch", L"Clear search", TRUE);
    if (!IsWindowVisible(g_f.hwndSearch))
    {
        RECT rcBox = { rcSearch.left, rcSearch.top, rcGlyph.left, rcSearch.bottom };
        AddLink(&rcBox, LA_CUSTOM, -2, NULL, L"focussearch", NULL, TRUE);
    }
    if (!s_bSearchHasText && !IsWindowVisible(g_f.hwndSearch))
    {
        HGDIOBJ oldF = SelectObject(hdc, g_f.hfText);
        SetTextColor(hdc, g_f.pal.DimText);
        TextOutW(hdc, rcSearch.left + S(6), ty, L"Search Control Panel", 20);
        SelectObject(hdc, oldF);
    }
    DeleteObject(brEdit);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
    DeleteObject(penB);
}

/* ------------------------------------------------------------------ */
/*  Painting: navigation pane                                          */
/* ------------------------------------------------------------------ */

static void PaintNavPane(HDC hdc)
{
    RECT rc = g_f.rcNav;
    HBRUSH br = CreateSolidBrush(g_f.pal.NavPane);
    FillRect(hdc, &rc, br);
    DeleteObject(br);
    HPEN pen = CreatePen(PS_SOLID, 1, g_f.pal.NavBorder);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    MoveToEx(hdc, rc.right - 1, rc.top, NULL);
    LineTo(hdc, rc.right - 1, rc.bottom);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    int x = rc.left + S(14);
    int cx = rc.right - x - S(10);
    int y = rc.top + S(14);
    const CPNAVVIEW *v = &g_f.view;

    BOOL homeCurrent = v->kind == NV_HOME;
    if (homeCurrent)
        return;
    if (homeCurrent)
    {
        HGDIOBJ oldF = SelectObject(hdc, g_f.hfBold);
        SetTextColor(hdc, g_f.pal.Text);
        SetBkMode(hdc, TRANSPARENT);
        TextOutW(hdc, x, y, L"Control Panel Home", 18);
        SelectObject(hdc, oldF);
        y += TextH(hdc, g_f.hfBold) + S(10);
    }
    else
    {
        y += DrawLinkText(hdc, x, y, cx, L"Control Panel Home", g_f.hfText, LA_HOME, 0, NULL, NULL, NULL, TRUE) + S(10);
    }

    if (v->kind == NV_HUB)
    {
        Hub_PaintNavTasks(hdc, x, &y, cx, v->hub, v->szPage);
        return;
    }
    if (v->kind == NV_HOME)
        return;

    for (int i = 1; i < (int)_countof(g_Categories); i++)
    {
        BOOL cur = v->kind == NV_CATEGORY && v->category == g_Categories[i].id;
        if (cur)
        {
            HGDIOBJ oldF = SelectObject(hdc, g_f.hfBold);
            SetTextColor(hdc, g_f.pal.Text);
            SetBkMode(hdc, TRANSPARENT);
            RECT r = { x, y, x + cx, y + S(200) };
            DrawTextW(hdc, g_Categories[i].pszName, -1, &r, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
            DrawTextW(hdc, g_Categories[i].pszName, -1, &r, DT_LEFT | DT_TOP | DT_WORDBREAK);
            SelectObject(hdc, oldF);
            y += r.bottom - r.top + S(6);
        }
        else
        {
            y += DrawLinkText(hdc, x, y, cx, g_Categories[i].pszName, g_f.hfText, LA_CATEGORY, g_Categories[i].id, NULL, NULL, NULL, TRUE) + S(6);
        }
    }

    if (v->kind == NV_CATEGORY || v->kind == NV_ALL || v->kind == NV_SEARCH)
    {
        y += S(14);
        HGDIOBJ oldF = SelectObject(hdc, g_f.hfBold);
        SetTextColor(hdc, g_f.pal.Text);
        SetBkMode(hdc, TRANSPARENT);
        TextOutW(hdc, x, y, L"See also", 8);
        SelectObject(hdc, oldF);
        y += TextH(hdc, g_f.hfBold) + S(6);
        if (v->kind != NV_ALL)
            y += DrawLinkText(hdc, x, y, cx, L"All Control Panel Items", g_f.hfText, LA_ALL, 0, NULL, NULL, NULL, TRUE) + S(6);
        CPITEM *pTb = Catalog_FindByCanonical(L"Microsoft.TaskbarAndStartMenu");
        if (pTb) y += DrawLinkText(hdc, x, y, cx, L"Taskbar and Start Menu", g_f.hfText, LA_ITEM, 0, pTb, NULL, pTb->szInfoTip, pTb->bEnabled) + S(6);
        CPITEM *pAcc = Catalog_FindByCanonical(L"Microsoft.EaseOfAccessCenter");
        if (pAcc) y += DrawLinkText(hdc, x, y, cx, L"Ease of Access Center", g_f.hfText, LA_ITEM, 0, pAcc, NULL, pAcc->szInfoTip, pAcc->bEnabled) + S(6);
    }
}

/* ------------------------------------------------------------------ */
/*  Painting: content views                                            */
/* ------------------------------------------------------------------ */

static int PaintHeader(HDC hdc, int x, int y, int cx, LPCWSTR pszTitle, BOOL bViewBy)
{
    HGDIOBJ oldF = SelectObject(hdc, g_f.hfHeader);
    SetTextColor(hdc, g_f.pal.Header);
    SetBkMode(hdc, TRANSPARENT);
    TextOutW(hdc, x, y, pszTitle, lstrlenW(pszTitle));
    SelectObject(hdc, oldF);
    int h = TextH(hdc, g_f.hfHeader);
    if (bViewBy)
    {
        LPCWSTR pszMode = g_f.view.kind == NV_ALL ? (g_f.bLargeIcons ? L"Large icons" : L"Small icons") : L"Category";
        int wLabel = TextW(hdc, g_f.hfText, L"View by:");
        int wMode = TextW(hdc, g_f.hfText, pszMode);
        int right = x + cx;
        int ty = y + (h - TextH(hdc, g_f.hfText)) / 2;
        int lx = right - wLabel - S(6) - wMode - S(18);
        oldF = SelectObject(hdc, g_f.hfText);
        SetTextColor(hdc, g_f.pal.Text);
        TextOutW(hdc, lx, ty, L"View by:", 8);
        SelectObject(hdc, oldF);
        RECT r = { lx + wLabel + S(6) - S(2), ty - S(2), right, ty + TextH(hdc, g_f.hfText) + S(2) };
        int idx = g_f.cLinks;
        AddLink(&r, LA_VIEWBY, 0, NULL, NULL, NULL, TRUE);
        BOOL hot = g_f.hotLink == idx;
        oldF = SelectObject(hdc, hot ? g_f.hfLink : g_f.hfText);
        SetTextColor(hdc, hot ? g_f.pal.LinkHot : g_f.pal.Link);
        TextOutW(hdc, lx + wLabel + S(6), ty, pszMode, lstrlenW(pszMode));
        SelectObject(hdc, oldF);
        HICON hc = LoadFluent(IDI_NAV_CHEVRON, S(12));
        DrawIconAt(hdc, right - S(14), ty + S(2), hc, S(12), TRUE);
    }
    return h + S(16);
}

static void PaintTaskLinks(HDC hdc, int x, int *py, int cx, const GUID *pTasks, int cTasks, BOOL bInline)
{
    int y = *py;
    int lx = x;
    for (int i = 0; i < cTasks; i++)
    {
        CPTASK *pTask = Catalog_FindTask(pTasks[i]);
        if (!pTask) continue;
        if (bInline)
        {
            int w = TextW(hdc, g_f.hfText, pTask->szName);
            if (lx + w + (i < cTasks - 1 ? S(14) : 0) > x + cx && lx > x)
            {
                lx = x;
                y += TextH(hdc, g_f.hfText) + S(2);
            }
            DrawLinkText(hdc, lx, y, x + cx - lx, pTask->szName, g_f.hfText, LA_TASK, 0, pTask, NULL, NULL, pTask->bEnabled);
            lx += w;
            if (i < cTasks - 1)
            {
                HGDIOBJ oldF = SelectObject(hdc, g_f.hfText);
                SetTextColor(hdc, g_f.pal.DimText);
                TextOutW(hdc, lx + S(4), y, L"|", 1);
                SelectObject(hdc, oldF);
                lx += S(14);
            }
        }
        else
        {
            y += DrawLinkText(hdc, x, y, cx, pTask->szName, g_f.hfText, LA_TASK, 0, pTask, NULL, NULL, pTask->bEnabled) + S(3);
        }
    }
    if (bInline) y += TextH(hdc, g_f.hfText) + S(2);
    *py = y;
}

static int PaintHome(HDC hdc, const RECT *prc)
{
    int x = prc->left + S(24);
    int cx = prc->right - x - S(24);
    int y = prc->top + S(18);
    y += PaintHeader(hdc, x, y, cx, L"Adjust your computer's settings", TRUE);
    y += S(10);

    int colW = (cx - S(20)) / 2;
    int iconSz = S(48);
    int col = 0;
    int rowTop = y;
    int rowH = 0;
    static const int s_HomeOrder[8] = { 5, 9, 3, 1, 2, 6, 8, 7 };
    for (int n = 0; n < 8; n++)
    {
        int i = Catalog_CategoryIndex(s_HomeOrder[n]);
        if (i < 0) continue;
        CPCATEGORY *pc = &g_Categories[i];
        int cxCol = x + col * (colW + S(20));
        int ty = rowTop;
        HICON hi = LoadFluent(pc->nIcon, iconSz);
        RECT rIcon = { cxCol, ty, cxCol + iconSz, ty + iconSz };
        AddLink(&rIcon, LA_CATEGORY, pc->id, NULL, NULL, pc->pszDescription, TRUE);
        DrawIconAt(hdc, cxCol, ty, hi, iconSz, TRUE);
        int tx = cxCol + iconSz + S(12);
        int tcx = colW - iconSz - S(12);
        int hTitle = DrawLinkText(hdc, tx, ty, tcx, pc->pszName, g_f.hfTitle, LA_CATEGORY, pc->id, NULL, NULL, pc->pszDescription, TRUE);
        ty += hTitle + S(4);
        PaintTaskLinks(hdc, tx, &ty, tcx, pc->homeTasks, pc->cHomeTasks, FALSE);
        int h = max(ty - rowTop, iconSz + S(8));
        if (h > rowH) rowH = h;
        col++;
        if (col == 2)
        {
            col = 0;
            rowTop += rowH + S(22);
            rowH = 0;
        }
    }
    if (col) rowTop += rowH + S(22);
    return rowTop - prc->top + S(20);
}

static int PaintCategory(HDC hdc, const RECT *prc, int cat)
{
    int idx = Catalog_CategoryIndex(cat);
    if (idx < 0) return 0;
    int x = prc->left + S(24);
    int cx = prc->right - x - S(24);
    int y = prc->top + S(18);
    y += PaintHeader(hdc, x, y, cx, g_Categories[idx].pszName, FALSE);
    int iconSz = S(32);
    for (int i = 0; i < g_cItems; i++)
    {
        CPITEM *pItem = &g_Items[i];
        if (!Catalog_ItemInCategory(pItem, cat)) continue;
        int top = y;
        RECT rIcon = { x, y, x + iconSz, y + iconSz };
        AddLink(&rIcon, LA_ITEM, 0, pItem, NULL, pItem->szInfoTip, pItem->bEnabled);
        DrawIconAt(hdc, x, y, pItem->hIcon32, iconSz, pItem->bEnabled);
        int tx = x + iconSz + S(12);
        int tcx = cx - iconSz - S(12);
        y += DrawLinkText(hdc, tx, y + S(2), tcx, pItem->szName, g_f.hfTitle, LA_ITEM, 0, pItem, NULL, pItem->bEnabled ? pItem->szInfoTip : L"Not available in ReactOS yet", pItem->bEnabled) + S(4);
        GUID tasks[16];
        int cTasks = 0;
        for (int t = 0; t < g_cTasks && cTasks < 16; t++)
        {
            if (!pItem->bHasGuid || !IsEqualGUID(g_Tasks[t].app, pItem->guid)) continue;
            if (!Catalog_TaskInCategory(&g_Tasks[t], cat)) continue;
            tasks[cTasks++] = g_Tasks[t].id;
        }
        if (cTasks)
            PaintTaskLinks(hdc, tx, &y, tcx, tasks, cTasks, TRUE);
        if (y < top + iconSz + S(6)) y = top + iconSz + S(6);
        y += S(14);
    }
    return y - prc->top + S(10);
}

static int PaintAll(HDC hdc, const RECT *prc)
{
    int x = prc->left + S(24);
    int cx = prc->right - x - S(24);
    int y = prc->top + S(18);
    y += PaintHeader(hdc, x, y, cx, L"Adjust your computer's settings", TRUE);
    y += S(6);
    if (g_f.bLargeIcons)
    {
        int tileW = S(240), tileH = S(58), iconSz = S(48);
        int cols = max(1, cx / tileW);
        int rows = (g_cItems + cols - 1) / cols;
        for (int i = 0; i < g_cItems; i++)
        {
            CPITEM *pItem = &g_Items[i];
            int c = i / rows, rr = i % rows;
            int tx = x + c * tileW;
            int ty = y + rr * tileH;
            RECT r = { tx, ty, tx + tileW - S(8), ty + tileH - S(4) };
            int idx = g_f.cLinks;
            AddLink(&r, LA_ITEM, 0, pItem, NULL, pItem->bEnabled ? pItem->szInfoTip : L"Not available in ReactOS yet", pItem->bEnabled);
            if (g_f.hotLink == idx && pItem->bEnabled)
            {
                HBRUSH hb = CreateSolidBrush(Blend(g_f.pal.Window, g_f.pal.Accent, 12));
                FillRect(hdc, &r, hb);
                DeleteObject(hb);
            }
            DrawIconAt(hdc, tx + S(4), ty + (tileH - S(4) - iconSz) / 2, pItem->hIcon48, iconSz, pItem->bEnabled);
            HGDIOBJ oldF = SelectObject(hdc, g_f.hfText);
            SetTextColor(hdc, pItem->bEnabled ? g_f.pal.Text : g_f.pal.Disabled);
            SetBkMode(hdc, TRANSPARENT);
            RECT rt = { tx + S(4) + iconSz + S(8), ty, tx + tileW - S(12), ty + tileH - S(4) };
            RECT rc = rt;
            DrawTextW(hdc, pItem->szName, -1, &rc, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_CALCRECT);
            int th = min(rc.bottom - rc.top, rt.bottom - rt.top);
            rt.top += (rt.bottom - rt.top - th) / 2;
            DrawTextW(hdc, pItem->szName, -1, &rt, DT_LEFT | DT_TOP | DT_WORDBREAK | DT_END_ELLIPSIS);
            SelectObject(hdc, oldF);
        }
        y += rows * tileH;
    }
    else
    {
        int rowH = S(24), iconSz = S(16), colW = S(230);
        int cols = max(1, cx / colW);
        int rows = (g_cItems + cols - 1) / cols;
        for (int i = 0; i < g_cItems; i++)
        {
            CPITEM *pItem = &g_Items[i];
            int c = i / rows, r = i % rows;
            int tx = x + c * colW, ty = y + r * rowH;
            RECT rc = { tx, ty, tx + colW - S(8), ty + rowH };
            int idx = g_f.cLinks;
            AddLink(&rc, LA_ITEM, 0, pItem, NULL, pItem->bEnabled ? pItem->szInfoTip : L"Not available in ReactOS yet", pItem->bEnabled);
            if (g_f.hotLink == idx && pItem->bEnabled)
            {
                HBRUSH hb = CreateSolidBrush(Blend(g_f.pal.Window, g_f.pal.Accent, 12));
                FillRect(hdc, &rc, hb);
                DeleteObject(hb);
            }
            DrawIconAt(hdc, tx + S(2), ty + (rowH - iconSz) / 2, pItem->hIcon16, iconSz, pItem->bEnabled);
            HGDIOBJ oldF = SelectObject(hdc, g_f.hfText);
            SetTextColor(hdc, pItem->bEnabled ? g_f.pal.Text : g_f.pal.Disabled);
            SetBkMode(hdc, TRANSPARENT);
            RECT rt = { tx + iconSz + S(8), ty, tx + colW - S(10), ty + rowH };
            DrawTextW(hdc, pItem->szName, -1, &rt, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            SelectObject(hdc, oldF);
        }
        y += rows * rowH;
    }
    return y - prc->top + S(20);
}

static int PaintSearch(HDC hdc, const RECT *prc, LPCWSTR pszQuery)
{
    int x = prc->left + S(24);
    int cx = prc->right - x - S(24);
    int y = prc->top + S(18);
    WCHAR szHdr[200];
    StringCchPrintfW(szHdr, _countof(szHdr), L"Search results for \"%s\"", pszQuery);
    y += PaintHeader(hdc, x, y, cx, szHdr, FALSE);
    int iconSz = S(32);
    int found = 0;
    for (int i = 0; i < g_cItems; i++)
    {
        CPITEM *pItem = &g_Items[i];
        BOOL match = Catalog_MatchText(pItem->szName, pszQuery) || Catalog_MatchText(pItem->szInfoTip, pszQuery) || Catalog_MatchText(pItem->szCanonical, pszQuery);
        GUID tasks[16];
        int cTasks = 0;
        for (int t = 0; t < g_cTasks && cTasks < 16; t++)
        {
            if (!pItem->bHasGuid || !IsEqualGUID(g_Tasks[t].app, pItem->guid)) continue;
            if (Catalog_MatchText(g_Tasks[t].szName, pszQuery) || Catalog_MatchText(g_Tasks[t].szKeywords, pszQuery))
                tasks[cTasks++] = g_Tasks[t].id;
        }
        if (!match && !cTasks) continue;
        found++;
        int top = y;
        RECT rIcon = { x, y, x + iconSz, y + iconSz };
        AddLink(&rIcon, LA_ITEM, 0, pItem, NULL, pItem->szInfoTip, pItem->bEnabled);
        DrawIconAt(hdc, x, y, pItem->hIcon32, iconSz, pItem->bEnabled);
        int tx = x + iconSz + S(12);
        int tcx = cx - iconSz - S(12);
        y += DrawLinkText(hdc, tx, y + S(2), tcx, pItem->szName, g_f.hfTitle, LA_ITEM, 0, pItem, NULL, pItem->bEnabled ? pItem->szInfoTip : L"Not available in ReactOS yet", pItem->bEnabled) + S(4);
        if (cTasks)
            PaintTaskLinks(hdc, tx, &y, tcx, tasks, cTasks, FALSE);
        if (y < top + iconSz + S(6)) y = top + iconSz + S(6);
        y += S(12);
    }
    if (!found)
        y += DrawWrapped(hdc, x, y, cx, L"No items match your search.", g_f.hfText, g_f.pal.DimText);
    return y - prc->top + S(10);
}

static void PaintContent(HDC hdc)
{
    RECT rc = g_f.rcContent;
    HBRUSH br = CreateSolidBrush(g_f.pal.Window);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    RECT rcView = rc;
    rcView.top -= g_f.view.scroll;
    g_f.contentTop = rcView.top;
    int h = 0;
    switch (g_f.view.kind)
    {
    case NV_HOME:     h = PaintHome(hdc, &rcView); break;
    case NV_CATEGORY: h = PaintCategory(hdc, &rcView, g_f.view.category); break;
    case NV_ALL:      h = PaintAll(hdc, &rcView); break;
    case NV_SEARCH:   h = PaintSearch(hdc, &rcView, g_f.view.szQuery); break;
    case NV_HUB:      h = Hub_Paint(hdc, &rcView, g_f.view.hub, g_f.view.szPage); break;
    }
    g_f.contentHeight = h;

    int avail = rc.bottom - rc.top;
    if (h > avail)
    {
        int trackH = avail - S(4);
        int thumbH = max(S(20), trackH * avail / h);
        int maxScroll = h - avail;
        int thumbY = rc.top + S(2) + (trackH - thumbH) * g_f.view.scroll / max(1, maxScroll);
        RECT rt = { rc.right - S(8), thumbY, rc.right - S(3), thumbY + thumbH };
        HBRUSH hb = CreateSolidBrush(g_f.pal.Rule);
        FillRect(hdc, &rt, hb);
        DeleteObject(hb);
    }
}

static void Paint(HWND hwnd)
{
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);

    g_f.cLinks = 0;
    PaintBar(mem);
    PaintNavPane(mem);
    HRGN clip = CreateRectRgnIndirect(&g_f.rcContent);
    SelectClipRgn(mem, clip);
    PaintContent(mem);
    SelectClipRgn(mem, NULL);
    DeleteObject(clip);

    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

/* ------------------------------------------------------------------ */
/*  Interaction                                                        */
/* ------------------------------------------------------------------ */

static int HitLink(POINT pt)
{
    for (int i = g_f.cLinks - 1; i >= 0; i--)
    {
        if (PtInRect(&g_f.links[i].rc, pt))
        {
            if (PtInRect(&g_f.rcContent, pt) || !PtInRect(&g_f.rcContent, *(POINT *)&g_f.links[i].rc))
                return i;
        }
    }
    return -1;
}

static void ShowTip(int idx)
{
    if (!g_f.hwndTip) return;
    TOOLINFOW ti = { sizeof(ti) };
    ti.hwnd = g_f.hwnd;
    ti.uId = 1;
    if (idx < 0 || !g_f.links[idx].szTip[0])
    {
        SendMessageW(g_f.hwndTip, TTM_TRACKACTIVATE, FALSE, (LPARAM)&ti);
        s_TipLink = -1;
        return;
    }
    ti.lpszText = g_f.links[idx].szTip;
    SendMessageW(g_f.hwndTip, TTM_UPDATETIPTEXTW, 0, (LPARAM)&ti);
    POINT pt;
    GetCursorPos(&pt);
    SendMessageW(g_f.hwndTip, TTM_TRACKPOSITION, 0, MAKELPARAM(pt.x + S(12), pt.y + S(18)));
    SendMessageW(g_f.hwndTip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    s_TipLink = idx;
}

static void FocusSearch(void)
{
    ShowWindow(g_f.hwndSearch, SW_SHOW);
    SetFocus(g_f.hwndSearch);
    InvalidateRect(g_f.hwnd, &g_f.rcBar, FALSE);
}

static void ClearSearch(void)
{
    SetWindowTextW(g_f.hwndSearch, L"");
    s_bSearchHasText = FALSE;
    ShowWindow(g_f.hwndSearch, SW_HIDE);
    if (g_f.view.kind == NV_SEARCH) GoBack();
    else Frame_Invalidate();
}

BOOL Frame_HandleCustom(int param, LPCWSTR pszArg)
{
    if (param == -1 && !lstrcmpW(pszArg, L"clearsearch"))
    {
        ClearSearch();
        return TRUE;
    }
    if (param == -2 && !lstrcmpW(pszArg, L"focussearch"))
    {
        FocusSearch();
        return TRUE;
    }
    return Hub_Custom(g_f.hwnd, param, pszArg);
}

static void ShowViewMenu(void)
{
    HMENU hMenu = CreatePopupMenu();
    AppendMenuW(hMenu, MF_STRING | (g_f.view.kind != NV_ALL ? MF_CHECKED : 0), IDM_VIEW_CATEGORY, L"Category");
    AppendMenuW(hMenu, MF_STRING | (g_f.view.kind == NV_ALL && g_f.bLargeIcons ? MF_CHECKED : 0), IDM_VIEW_LARGE, L"Large icons");
    AppendMenuW(hMenu, MF_STRING | (g_f.view.kind == NV_ALL && !g_f.bLargeIcons ? MF_CHECKED : 0), IDM_VIEW_SMALL, L"Small icons");
    POINT pt;
    GetCursorPos(&pt);
    int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_f.hwnd, NULL);
    DestroyMenu(hMenu);
    if (cmd == IDM_VIEW_CATEGORY) { if (g_f.view.kind == NV_ALL) NavigateHome(); }
    else if (cmd == IDM_VIEW_LARGE) { g_f.bLargeIcons = TRUE; if (g_f.view.kind != NV_ALL) NavigateAll(); else Frame_Invalidate(); }
    else if (cmd == IDM_VIEW_SMALL) { g_f.bLargeIcons = FALSE; if (g_f.view.kind != NV_ALL) NavigateAll(); else Frame_Invalidate(); }
    SHSetValueW(HKEY_CURRENT_USER, L"Software\\ReactOS\\Control11", L"LargeIcons", REG_DWORD, &g_f.bLargeIcons, sizeof(DWORD));
}

static void ActivateLink(int idx)
{
    if (idx < 0) return;
    CPLINK lk = g_f.links[idx];
    if (!lk.bEnabled) return;
    switch (lk.action)
    {
    case LA_HOME:     NavigateHome(); break;
    case LA_CATEGORY: NavigateCategory(lk.param); break;
    case LA_ALL:      NavigateAll(); break;
    case LA_ITEM:     Catalog_OpenItem(g_f.hwnd, (CPITEM *)lk.ptr, lk.szArg[0] ? lk.szArg : NULL); break;
    case LA_TASK:     Catalog_RunTask(g_f.hwnd, (CPTASK *)lk.ptr); break;
    case LA_HUBPAGE:  Frame_OpenHub(lk.param, lk.szArg); break;
    case LA_COMMAND:  Catalog_RunCommand(g_f.hwnd, lk.szArg); break;
    case LA_VIEWBY:   ShowViewMenu(); break;
    case LA_BACK:     GoBack(); break;
    case LA_FORWARD:  GoForward(); break;
    case LA_CUSTOM:   Frame_HandleCustom(lk.param, lk.szArg); break;
    }
}

static void OnMouseMove(HWND hwnd, int x, int y)
{
    POINT pt = { x, y };
    int idx = HitLink(pt);
    if (idx != g_f.hotLink)
    {
        g_f.hotLink = idx;
        Frame_Invalidate();
        KillTimer(hwnd, TIMER_TIP);
        if (s_TipLink != idx) ShowTip(-1);
        if (idx >= 0 && g_f.links[idx].szTip[0])
            SetTimer(hwnd, TIMER_TIP, 600, NULL);
    }
    TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
    TrackMouseEvent(&tme);
}

static void OnScroll(int delta)
{
    int avail = g_f.rcContent.bottom - g_f.rcContent.top;
    int maxScroll = max(0, g_f.contentHeight - avail);
    int s = g_f.view.scroll - delta;
    if (s < 0) s = 0;
    if (s > maxScroll) s = maxScroll;
    if (s != g_f.view.scroll)
    {
        g_f.view.scroll = s;
        Hub_Size();
        Frame_Invalidate();
    }
}

static LRESULT CALLBACK SearchSubclass(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_KEYDOWN && wParam == VK_ESCAPE)
    {
        SetFocus(g_f.hwnd);
        ClearSearch();
        return 0;
    }
    if (msg == WM_KEYDOWN && wParam == VK_RETURN)
    {
        WCHAR sz[128];
        GetWindowTextW(hwnd, sz, _countof(sz));
        KillTimer(g_f.hwnd, TIMER_SEARCH);
        NavigateSearch(sz);
        return 0;
    }
    if (msg == WM_CHAR && (wParam == VK_RETURN || wParam == VK_ESCAPE))
        return 0;
    return CallWindowProcW(s_SearchProc, hwnd, msg, wParam, lParam);
}

static LRESULT CALLBACK FrameProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_CREATE:
    {
        g_f.hwnd = hwnd;
        g_f.hwndSearch = CreateWindowExW(0, L"EDIT", L"", WS_CHILD | ES_AUTOHSCROLL | ES_LEFT,
                                         0, 0, 10, 10, hwnd, (HMENU)IDC_SEARCH, GetModuleHandleW(NULL), NULL);
        SendMessageW(g_f.hwndSearch, WM_SETFONT, (WPARAM)g_f.hfText, TRUE);
        s_SearchProc = (WNDPROC)SetWindowLongPtrW(g_f.hwndSearch, GWLP_WNDPROC, (LONG_PTR)SearchSubclass);
        g_f.hwndTip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, NULL, WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
                                      0, 0, 0, 0, hwnd, NULL, GetModuleHandleW(NULL), NULL);
        if (g_f.hwndTip)
        {
            TOOLINFOW ti = { sizeof(ti) };
            ti.uFlags = TTF_TRACK | TTF_ABSOLUTE;
            ti.hwnd = hwnd;
            ti.uId = 1;
            ti.lpszText = (LPWSTR)L"";
            SendMessageW(g_f.hwndTip, TTM_ADDTOOLW, 0, (LPARAM)&ti);
            SendMessageW(g_f.hwndTip, TTM_SETMAXTIPWIDTH, 0, S(320));
        }
        Layout();
        return 0;
    }
    case WM_SIZE:
        Layout();
        Frame_Invalidate();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_CTLCOLOREDIT:
        if ((HWND)lParam == g_f.hwndSearch)
        {
            static HBRUSH s_hbrEdit;
            if (s_hbrEdit) DeleteObject(s_hbrEdit);
            s_hbrEdit = CreateSolidBrush(g_f.pal.Edit);
            SetTextColor((HDC)wParam, g_f.pal.Text);
            SetBkColor((HDC)wParam, g_f.pal.Edit);
            return (LRESULT)s_hbrEdit;
        }
        break;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_SEARCH && HIWORD(wParam) == EN_CHANGE)
        {
            BOOL had = s_bSearchHasText;
            s_bSearchHasText = GetWindowTextLengthW(g_f.hwndSearch) > 0;
            if (had != s_bSearchHasText) InvalidateRect(hwnd, &g_f.rcBar, FALSE);
            SetTimer(hwnd, TIMER_SEARCH, 300, NULL);
            return 0;
        }
        if (LOWORD(wParam) == IDC_SEARCH && (HIWORD(wParam) == EN_SETFOCUS || HIWORD(wParam) == EN_KILLFOCUS))
        {
            if (HIWORD(wParam) == EN_KILLFOCUS && GetWindowTextLengthW(g_f.hwndSearch) == 0)
                ShowWindow(g_f.hwndSearch, SW_HIDE);
            InvalidateRect(hwnd, &g_f.rcBar, FALSE);
            return 0;
        }
        if (Hub_Custom(hwnd, 0x10000 | LOWORD(wParam), NULL))
            return 0;
        break;
    case WM_TIMER:
        if (wParam == TIMER_SEARCH)
        {
            KillTimer(hwnd, TIMER_SEARCH);
            WCHAR sz[128];
            GetWindowTextW(g_f.hwndSearch, sz, _countof(sz));
            StrTrimW(sz, L" ");
            NavigateSearch(sz);
            if (sz[0]) FocusSearch();
            return 0;
        }
        if (wParam == TIMER_TIP)
        {
            KillTimer(hwnd, TIMER_TIP);
            ShowTip(g_f.hotLink);
            return 0;
        }
        break;
    case WM_MOUSEMOVE:
        OnMouseMove(hwnd, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSELEAVE:
        if (g_f.hotLink != -1)
        {
            g_f.hotLink = -1;
            Frame_Invalidate();
        }
        KillTimer(hwnd, TIMER_TIP);
        ShowTip(-1);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(hwnd);
        return 0;
    case WM_LBUTTONUP:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        int idx = HitLink(pt);
        ShowTip(-1);
        if (idx >= 0) ActivateLink(idx);
        return 0;
    }
    case WM_XBUTTONUP:
        if (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) GoBack();
        else if (GET_XBUTTON_WPARAM(wParam) == XBUTTON2) GoForward();
        return TRUE;
    case WM_SETCURSOR:
        if (LOWORD(lParam) == HTCLIENT)
        {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd, &pt);
            int idx = HitLink(pt);
            if (idx >= 0 && g_f.links[idx].bEnabled && g_f.links[idx].action != LA_NONE)
            {
                SetCursor(LoadCursorW(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    case WM_MOUSEWHEEL:
        OnScroll(GET_WHEEL_DELTA_WPARAM(wParam) / WHEEL_DELTA * S(60));
        return 0;
    case WM_KEYDOWN:
        if (wParam == VK_BACK) { GoBack(); return 0; }
        if (wParam == VK_F5) { Catalog_Load(); Frame_Invalidate(); return 0; }
        if (wParam == VK_HOME && (GetKeyState(VK_MENU) & 0x8000)) { NavigateHome(); return 0; }
        if (wParam == VK_LEFT && (GetKeyState(VK_MENU) & 0x8000)) { GoBack(); return 0; }
        if (wParam == VK_RIGHT && (GetKeyState(VK_MENU) & 0x8000)) { GoForward(); return 0; }
        if (wParam == VK_PRIOR) { OnScroll(S(200)); return 0; }
        if (wParam == VK_NEXT) { OnScroll(-S(200)); return 0; }
        if (wParam == VK_UP) { OnScroll(S(40)); return 0; }
        if (wParam == VK_DOWN) { OnScroll(-S(40)); return 0; }
        if (wParam == 'E' && (GetKeyState(VK_CONTROL) & 0x8000)) { FocusSearch(); return 0; }
        if (wParam == 'F' && (GetKeyState(VK_CONTROL) & 0x8000)) { FocusSearch(); return 0; }
        break;
    case WM_SYSCOMMAND:
        break;
    case WM_SETTINGCHANGE:
    case WM_SYSCOLORCHANGE:
    case WM_THEMECHANGED:
        InitPalette();
        Frame_Invalidate();
        break;
    case WM_DPICHANGED:
        g_f.dpi = HIWORD(wParam);
        InitFonts();
        SendMessageW(g_f.hwndSearch, WM_SETFONT, (WPARAM)g_f.hfText, TRUE);
        Layout();
        Frame_Invalidate();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

static BOOL IsSwitch(LPCWSTR pszSwitch, LPCWSTR pszArg)
{
    if (*pszArg == L'/' || *pszArg == L'-')
        return !lstrcmpiW(pszArg + 1, pszSwitch);
    return FALSE;
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrev, LPWSTR lpCmdLine, int nCmdShow)
{
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_TAB_CLASSES };
    InitCommonControlsEx(&icc);
    CoInitialize(NULL);

    LPCWSTR pszName = NULL, pszPage = NULL;
    int startCategory = -1;
    BOOL bAll = FALSE;
    int argc = 0;
    LPWSTR *argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv && i < argc; i++)
    {
        if (IsSwitch(L"name", argv[i]) && i + 1 < argc) pszName = argv[++i];
        else if (IsSwitch(L"page", argv[i]) && i + 1 < argc) pszPage = argv[++i];
        else if (IsSwitch(L"category", argv[i]) && i + 1 < argc) startCategory = _wtoi(argv[++i]);
        else if (IsSwitch(L"all", argv[i])) bAll = TRUE;
    }

    HDC hdc = GetDC(NULL);
    g_f.dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    ReleaseDC(NULL, hdc);
    if (!g_f.dpi) g_f.dpi = 96;
    InitPalette();
    InitFonts();
    g_f.hotLink = -1;
    DWORD dwLarge = 1, cb = sizeof(dwLarge);
    SHGetValueW(HKEY_CURRENT_USER, L"Software\\ReactOS\\Control11", L"LargeIcons", NULL, &dwLarge, &cb);
    g_f.bLargeIcons = dwLarge != 0;

    Catalog_Load();

    CPITEM *pStart = NULL;
    if (pszName)
    {
        pStart = Catalog_FindByCanonical(pszName);
        if (pStart && pStart->hub == HUB_NONE)
        {
            Catalog_OpenItem(NULL, pStart, pszPage);
            LocalFree(argv);
            CoUninitialize();
            return 0;
        }
    }

    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = FrameProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_MAIN), IMAGE_ICON, 32, 32, 0);
    wc.hIconSm = (HICON)LoadImageW(hInstance, MAKEINTRESOURCEW(IDI_MAIN), IMAGE_ICON, 16, 16, 0);
    wc.lpszClassName = FRAME_CLASS;
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
    RegisterClassExW(&wc);

    int w = S(960), h = S(640);
    RECT rcWork;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &rcWork, 0);
    if (w > rcWork.right - rcWork.left) w = rcWork.right - rcWork.left;
    if (h > rcWork.bottom - rcWork.top) h = rcWork.bottom - rcWork.top;
    int x = rcWork.left + (rcWork.right - rcWork.left - w) / 2;
    int y = rcWork.top + (rcWork.bottom - rcWork.top - h) / 2;
    HWND hwnd = CreateWindowExW(0, FRAME_CLASS, L"Control Panel", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                x, y, w, h, NULL, NULL, hInstance, NULL);
    if (!hwnd)
    {
        LocalFree(argv);
        CoUninitialize();
        return 1;
    }

    if (pStart)
        Frame_OpenHub(pStart->hub, pszPage);
    else if (bAll)
        NavigateAll();
    else if (startCategory > 0 && Catalog_CategoryIndex(startCategory) >= 0)
        NavigateCategory(startCategory);
    else
        NavigateHome();

    ShowWindow(hwnd, nCmdShow == SW_SHOWMINIMIZED ? SW_SHOWNORMAL : nCmdShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0))
    {
        if (m.message == WM_KEYDOWN && m.hwnd != g_f.hwndSearch && GetParent(m.hwnd) == g_f.hwnd)
        {
            if (m.wParam == VK_ESCAPE) { SetFocus(g_f.hwnd); continue; }
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    LocalFree(argv);
    if (g_pControlsFolder) g_pControlsFolder->Release();
    CoUninitialize();
    return (int)m.wParam;
}
