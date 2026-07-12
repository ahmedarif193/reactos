/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Virtual tree-list control with heat-map cells (Win11 style)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

#define TL_CLASS L"TM11TreeList"

struct TreeList
{
    HWND hwnd;
    ITreeListOwner* owner;
    BOOL dblHeader;

    Vec<TLColumn> cols;
    Vec<TLRow> rows;

    int  rowH, headerH;
    int  scrollY;         /* pixels */
    int  scrollX;
    int  hotRow;          /* index or -1 */
    LONGLONG selKey;      /* 0 = none */
    int  sortCol;         /* col id, -1 none */
    BOOL sortDesc;
    BOOL focus;
    WCHAR emptyText[128];

    /* header interaction */
    int  resizeCol;       /* col index being resized, -1 */
    int  resizeStartX, resizeStartW;
    int  hotHeader;       /* col index hovered, -1 */

    /* scrollbar drag */
    BOOL vDrag, hDrag;
    int  dragOff;
    BOOL vHot, hHot;
    BOOL tracking;

    TreeList() : hwnd(NULL), owner(NULL), dblHeader(FALSE), rowH(0), headerH(0),
                 scrollY(0), scrollX(0), hotRow(-1), selKey(0), sortCol(-1),
                 sortDesc(TRUE), focus(FALSE), resizeCol(-1), resizeStartX(0),
                 resizeStartW(0), hotHeader(-1), vDrag(FALSE), hDrag(FALSE),
                 dragOff(0), vHot(FALSE), hHot(FALSE), tracking(FALSE)
    { emptyText[0] = 0; }
};

static TreeList* TL(HWND h) { return (TreeList*)GetWindowLongPtrW(h, GWLP_USERDATA); }

/* ------------------------------------------------------------------ */
/*  Geometry                                                           */
/* ------------------------------------------------------------------ */

static void TLMetrics(TreeList* t)
{
    t->rowH = S(35);
    t->headerH = t->dblHeader ? S(48) : S(30);
}

static int VisRowsArea(TreeList* t, RECT* rcBody)
{
    RECT rc;
    GetClientRect(t->hwnd, &rc);
    rcBody->left = rc.left;
    rcBody->top = rc.top + t->headerH;
    rcBody->right = rc.right;
    rcBody->bottom = rc.bottom;
    return rcBody->bottom - rcBody->top;
}

static int TotalColsWidth(TreeList* t)
{
    int w = 0;
    for (int i = 0; i < t->cols.n; i++)
        w += t->cols[i].width;
    return w;
}

/* effective x/width for each column (first column stretches) */
static void ColRects(TreeList* t, int* xs, int* ws)
{
    RECT rc;
    GetClientRect(t->hwnd, &rc);
    int client = rc.right - rc.left;
    int total = TotalColsWidth(t);
    int stretch = 0;
    if (total < client)
        stretch = client - total;
    int x = -t->scrollX;
    for (int i = 0; i < t->cols.n; i++)
    {
        int w = t->cols[i].width + (i == 0 ? stretch : 0);
        xs[i] = x;
        ws[i] = w;
        x += w;
    }
}

static int ContentHeight(TreeList* t) { return t->rows.n * t->rowH; }

static void ClampScroll(TreeList* t)
{
    RECT body;
    int areaH = VisRowsArea(t, &body);
    int maxY = ContentHeight(t) - areaH;
    if (maxY < 0) maxY = 0;
    if (t->scrollY > maxY) t->scrollY = maxY;
    if (t->scrollY < 0) t->scrollY = 0;

    RECT rc;
    GetClientRect(t->hwnd, &rc);
    int maxX = TotalColsWidth(t) - (rc.right - rc.left);
    if (maxX < 0) maxX = 0;
    if (t->scrollX > maxX) t->scrollX = maxX;
    if (t->scrollX < 0) t->scrollX = 0;
}

static int RowAtY(TreeList* t, int y)
{
    RECT body;
    VisRowsArea(t, &body);
    if (y < body.top) return -1;
    int idx = (y - body.top + t->scrollY) / t->rowH;
    if (idx < 0 || idx >= t->rows.n) return -1;
    return idx;
}

static int SelIdx(TreeList* t)
{
    if (!t->selKey) return -1;
    for (int i = 0; i < t->rows.n; i++)
        if (t->rows[i].key == t->selKey) return i;
    return -1;
}

/* vertical scrollbar thumb rect; returns FALSE if no bar needed */
static BOOL VThumb(TreeList* t, RECT* rOut)
{
    RECT body;
    int areaH = VisRowsArea(t, &body);
    int contentH = ContentHeight(t);
    if (contentH <= areaH || areaH <= 0) return FALSE;
    int barW = S(12);
    int thumbH = MulDiv(areaH, areaH, contentH);
    if (thumbH < S(30)) thumbH = S(30);
    int maxScroll = contentH - areaH;
    int y = body.top + MulDiv(t->scrollY, areaH - thumbH, maxScroll);
    rOut->left = body.right - barW + S(3);
    rOut->right = body.right - S(3);
    rOut->top = y;
    rOut->bottom = y + thumbH;
    return TRUE;
}

static BOOL HThumb(TreeList* t, RECT* rOut)
{
    RECT rc;
    GetClientRect(t->hwnd, &rc);
    int client = rc.right - rc.left;
    int total = TotalColsWidth(t);
    if (total <= client || client <= 0) return FALSE;
    int thumbW = MulDiv(client, client, total);
    if (thumbW < S(30)) thumbW = S(30);
    int maxScroll = total - client;
    int x = MulDiv(t->scrollX, client - thumbW, maxScroll);
    rOut->left = x;
    rOut->right = x + thumbW;
    rOut->bottom = rc.bottom - S(2);
    rOut->top = rc.bottom - S(9);
    return TRUE;
}

/* ------------------------------------------------------------------ */
/*  Painting                                                           */
/* ------------------------------------------------------------------ */

static void PaintHeader(TreeList* t, HDC dc, int* xs, int* ws, const RECT& rcClient)
{
    RECT hr = { rcClient.left, rcClient.top, rcClient.right, rcClient.top + t->headerH };
    FillRect32(dc, hr, g_t.listBg);

    for (int i = 0; i < t->cols.n; i++)
    {
        TLColumn& c = t->cols[i];
        RECT cr = { xs[i], hr.top, xs[i] + ws[i], hr.bottom };
        if (cr.right < hr.left || cr.left > hr.right) continue;

        BOOL heat = (c.flags & TLC_HEAT) != 0;
        double hv = heat && t->owner ? t->owner->TLHeaderHeat(c.id) : -1.0;
        if (heat && hv >= 0)
        {
            RECT fill = { cr.left, cr.top, cr.right, cr.bottom };
            FillRect32(dc, fill, HeatBg(hv));
        }
        else if (t->hotHeader == i && t->resizeCol < 0)
        {
            FillRect32(dc, cr, g_t.hoverBg);
        }

        UINT align = (c.flags & TLC_RIGHT) ? DT_RIGHT : DT_LEFT;
        RECT pad = { cr.left + S(10), cr.top, cr.right - S(10), cr.bottom };

        COLORREF txt = (heat && hv >= 0) ? HeatText(hv) : g_t.textSec;

        if (t->dblHeader)
        {
            if (heat)
            {
                WCHAR val[64] = L"";
                if (t->owner) t->owner->TLHeaderValue(c.id, val, _countof(val));
                RECT top = { pad.left, cr.top + S(6), pad.right, cr.top + S(26) };
                DrawTextClip(dc, val, top, g_t.fBody, txt, align | DT_SINGLELINE | DT_TOP);
            }
            RECT bot = { pad.left, cr.bottom - S(20), pad.right, cr.bottom - S(4) };
            DrawTextClip(dc, c.title, bot, g_t.fSmall, txt, align | DT_SINGLELINE | DT_TOP);
        }
        else
        {
            DrawTextClip(dc, c.title, pad, g_t.fSmall, txt,
                         align | DT_SINGLELINE | DT_VCENTER);
        }

        /* sort caret */
        if (t->sortCol == c.id)
        {
            int cx = (cr.left + cr.right) / 2;
            RECT car = { cx - S(5), cr.top + S(1), cx + S(5), cr.top + S(9) };
            DrawGlyph(dc, car, t->sortDesc ? IC_CHEV_D : IC_CHEV_U, g_t.textSec);
        }

        /* separator */
        DrawVLine(dc, cr.right - 1, hr.top + S(6), hr.bottom - S(4),
                  Blend(g_t.listBg, g_t.textSec, 25));
    }

    DrawHLine(dc, hr.bottom - 1, hr.left, hr.right, g_t.divider);
}

static void PaintRow(TreeList* t, HDC dc, int idx, int* xs, int* ws, const RECT& rr)
{
    TLRow& row = t->rows[idx];
    BOOL sel = (row.key == t->selKey);
    BOOL hot = (idx == t->hotRow);

    RECT rc;
    GetClientRect(t->hwnd, &rc);

    if (row.kind == TLR_GROUP)
    {
        /* group header: bold text + chevron, no fill */
        if (hot || sel)
        {
            RECT hl = { rr.left + S(4), rr.top + S(2), rc.right - S(4), rr.bottom - S(2) };
            FillRoundRect(dc, hl, sel ? g_t.selBg : g_t.hoverBg, CLR_NONE, S(4));
        }
        RECT chev = { rr.left + S(10), rr.top + (t->rowH - S(14)) / 2,
                      rr.left + S(24), rr.top + (t->rowH + S(14)) / 2 };
        DrawGlyph(dc, chev, row.expanded ? IC_CHEV_D : IC_CHEV_R, g_t.textSec);

        WCHAR label[128] = L"";
        if (t->owner) t->owner->TLCellText(row.data, -1, label, _countof(label));
        RECT tr = { rr.left + S(32), rr.top, rc.right - S(10), rr.bottom };
        DrawTextClip(dc, label, tr, g_t.fBodySemi, g_t.textMain,
                     DT_LEFT | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        return;
    }

    /* selection / hover base across full row */
    if (sel || hot)
    {
        RECT hl = { rr.left + S(4), rr.top + S(1), rc.right - S(4), rr.bottom - S(1) };
        FillRoundRect(dc, hl, sel ? g_t.selBg : g_t.hoverBg, CLR_NONE, S(4));
    }

    for (int i = 0; i < t->cols.n; i++)
    {
        TLColumn& c = t->cols[i];
        RECT cr = { xs[i], rr.top, xs[i] + ws[i], rr.bottom };
        if (cr.right < rc.left || cr.left > rc.right) continue;

        WCHAR txt[192] = L"";
        if (t->owner) t->owner->TLCellText(row.data, c.id, txt, _countof(txt));

        double heat = (c.flags & TLC_HEAT) && t->owner ? t->owner->TLCellHeat(row.data, c.id) : -1.0;
        COLORREF txtCol = g_t.textMain;
        if (heat >= 0)
        {
            RECT fill = { cr.left + 1, rr.top, cr.right - 1, rr.bottom };
            FillRect32(dc, fill, HeatBg(heat));
            txtCol = HeatText(heat);
        }

        if (i == 0)
        {
            /* tree column: indent, chevron, icon, text, dim suffix */
            int x = cr.left + S(10) + row.depth * S(22);
            if (row.expandable)
            {
                RECT chev = { x, rr.top + (t->rowH - S(14)) / 2,
                              x + S(14), rr.top + (t->rowH + S(14)) / 2 };
                DrawGlyph(dc, chev, row.expanded ? IC_CHEV_D : IC_CHEV_R, g_t.textSec);
            }
            x += S(20);

            HICON ico = t->owner ? t->owner->TLRowIcon(row.data) : NULL;
            int icoY = rr.top + (t->rowH - S(16)) / 2;
            if (ico)
            {
                DrawIconEx(dc, x, icoY, ico, S(16), S(16), 0, NULL, DI_NORMAL);
            }
            else
            {
                int glyph = t->owner ? t->owner->TLRowGlyph(row.data) : IC_NONE;
                if (glyph != IC_NONE)
                {
                    RECT gr = { x, icoY, x + S(16), icoY + S(16) };
                    DrawGlyph(dc, gr, glyph, g_t.textSec);
                }
            }
            x += S(16) + S(10);

            WCHAR sub[128] = L"";
            if (t->owner) t->owner->TLSecondaryText(row.data, sub, _countof(sub));

            RECT tr = { x, rr.top, cr.right - S(8), rr.bottom };
            HGDIOBJ oldF = SelectObject(dc, g_t.fBody);
            SetBkMode(dc, TRANSPARENT);
            SetTextColor(dc, txtCol);
            RECT calc = tr;
            DrawTextW(dc, txt, -1, &calc, DT_LEFT | DT_SINGLELINE | DT_CALCRECT | DT_NOPREFIX);
            DrawTextW(dc, txt, -1, &tr, DT_LEFT | DT_SINGLELINE | DT_VCENTER |
                      DT_END_ELLIPSIS | DT_NOPREFIX);
            if (sub[0] && calc.right + S(8) < tr.right)
            {
                RECT sr = { calc.right + S(8), tr.top, tr.right, tr.bottom };
                SetTextColor(dc, g_t.textSec);
                DrawTextW(dc, sub, -1, &sr, DT_LEFT | DT_SINGLELINE | DT_VCENTER |
                          DT_END_ELLIPSIS | DT_NOPREFIX);
            }
            SelectObject(dc, oldF);
        }
        else
        {
            /* status glyph? */
            COLORREF gc = g_t.textSec;
            int glyph = t->owner ? t->owner->TLStatusGlyph(row.data, c.id, &gc) : IC_NONE;
            int tx = cr.left + S(10);
            if (glyph != IC_NONE)
            {
                RECT gr = { tx, rr.top + (t->rowH - S(15)) / 2,
                            tx + S(15), rr.top + (t->rowH + S(15)) / 2 };
                DrawGlyph(dc, gr, glyph, gc);
                tx += S(20);
            }
            UINT align = (c.flags & TLC_RIGHT) ? DT_RIGHT : DT_LEFT;
            RECT tr = { tx, rr.top, cr.right - S(10), rr.bottom };
            DrawTextClip(dc, txt, tr, g_t.fBody, txtCol,
                         align | DT_SINGLELINE | DT_VCENTER | DT_END_ELLIPSIS);
        }
    }
}

static void TLPaint(TreeList* t, HDC dc, const RECT& rcPaint)
{
    RECT rc;
    GetClientRect(t->hwnd, &rc);
    FillRect32(dc, rcPaint, g_t.listBg);

    int xs[32], ws[32];
    if (t->cols.n > 32) return;
    ColRects(t, xs, ws);

    /* faint full-height column separators under rows */
    RECT body;
    VisRowsArea(t, &body);

    /* rows */
    int first = t->scrollY / t->rowH;
    int y = body.top - (t->scrollY % t->rowH);
    for (int i = first; i < t->rows.n; i++)
    {
        RECT rr = { rc.left, y, rc.right, y + t->rowH };
        if (rr.top > rcPaint.bottom || rr.top >= body.bottom) break;
        if (rr.bottom >= rcPaint.top)
            PaintRow(t, dc, i, xs, ws, rr);
        y += t->rowH;
    }

    /* column separators (over rows, subtle) */
    for (int i = 0; i < t->cols.n; i++)
    {
        int x = xs[i] + ws[i] - 1;
        if (x >= rc.left && x <= rc.right)
            DrawVLine(dc, x, body.top, body.bottom, Blend(g_t.listBg, g_t.textSec, 10));
    }

    /* empty text */
    if (!t->rows.n && t->emptyText[0])
    {
        RECT er = { rc.left, body.top + S(30), rc.right, body.top + S(70) };
        DrawTextClip(dc, t->emptyText, er, g_t.fBody, g_t.textSec,
                     DT_CENTER | DT_SINGLELINE);
    }

    /* header (painted last, opaque) */
    PaintHeader(t, dc, xs, ws, rc);

    /* scrollbars */
    RECT th;
    if (VThumb(t, &th))
        FillRoundRect(dc, th, (t->vHot || t->vDrag) ? g_t.scrollThumbHot : g_t.scrollThumb,
                      CLR_NONE, S(3));
    if (HThumb(t, &th))
        FillRoundRect(dc, th, (t->hHot || t->hDrag) ? g_t.scrollThumbHot : g_t.scrollThumb,
                      CLR_NONE, S(3));
}

/* ------------------------------------------------------------------ */
/*  Interaction                                                        */
/* ------------------------------------------------------------------ */

static int HeaderSeparatorHit(TreeList* t, int x, int y)
{
    if (y >= t->headerH) return -1;
    int xs[32], ws[32];
    if (t->cols.n > 32) return -1;
    ColRects(t, xs, ws);
    for (int i = 0; i < t->cols.n; i++)
    {
        if (t->cols[i].flags & TLC_FIXED) continue;
        int sep = xs[i] + ws[i];
        if (x >= sep - S(4) && x <= sep + S(4))
            return i;
    }
    return -1;
}

static int HeaderColHit(TreeList* t, int x, int y)
{
    if (y >= t->headerH) return -1;
    int xs[32], ws[32];
    if (t->cols.n > 32) return -1;
    ColRects(t, xs, ws);
    for (int i = 0; i < t->cols.n; i++)
        if (x >= xs[i] && x < xs[i] + ws[i]) return i;
    return -1;
}

static BOOL ChevronHit(TreeList* t, int idx, int x)
{
    TLRow& row = t->rows[idx];
    if (row.kind == TLR_GROUP) return x < S(30);
    if (!row.expandable) return FALSE;
    int xs[32], ws[32];
    ColRects(t, xs, ws);
    int cx = xs[0] + S(10) + row.depth * S(22);
    return x >= cx - S(3) && x <= cx + S(17);
}

static void SetSel(TreeList* t, int idx, BOOL notify)
{
    LONGLONG nk = (idx >= 0 && idx < t->rows.n) ? t->rows[idx].key : 0;
    if (nk != t->selKey)
    {
        t->selKey = nk;
        InvalidateRect(t->hwnd, NULL, FALSE);
        if (notify && t->owner) t->owner->TLOnSelChanged();
    }
}

static void EnsureVisible(TreeList* t, int idx)
{
    if (idx < 0 || idx >= t->rows.n) return;
    RECT body;
    int areaH = VisRowsArea(t, &body);
    int top = idx * t->rowH;
    if (top < t->scrollY)
        t->scrollY = top;
    else if (top + t->rowH > t->scrollY + areaH)
        t->scrollY = top + t->rowH - areaH;
    ClampScroll(t);
}

static void KeyNav(TreeList* t, int delta, BOOL absolute)
{
    if (!t->rows.n) return;
    int cur = SelIdx(t);
    int idx;
    if (absolute)
        idx = delta < 0 ? 0 : t->rows.n - 1;
    else if (cur < 0)
        idx = 0;
    else
        idx = cur + delta;
    if (idx < 0) idx = 0;
    if (idx >= t->rows.n) idx = t->rows.n - 1;
    SetSel(t, idx, TRUE);
    EnsureVisible(t, idx);
    InvalidateRect(t->hwnd, NULL, FALSE);
}

static LRESULT CALLBACK TLProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    TreeList* t = TL(hwnd);

    switch (msg)
    {
    case WM_NCCREATE:
    {
        CREATESTRUCTW* cs = (CREATESTRUCTW*)lp;
        t = new TreeList();
        t->hwnd = hwnd;
        t->owner = (ITreeListOwner*)cs->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)t);
        TLMetrics(t);
        return TRUE;
    }
    case WM_NCDESTROY:
        delete t;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        BufPaint bp;
        HDC dc = bp.Begin(hdc, &ps.rcPaint);
        TLPaint(t, dc, ps.rcPaint);
        bp.End();
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_SIZE:
        ClampScroll(t);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_SETFOCUS:
        t->focus = TRUE;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_KILLFOCUS:
        t->focus = FALSE;
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;

    case WM_MOUSEWHEEL:
    {
        int lines = -GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * 3;
        t->scrollY += lines * t->rowH;
        ClampScroll(t);
        InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSEMOVE:
    {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);

        if (!t->tracking)
        {
            TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
            TrackMouseEvent(&tme);
            t->tracking = TRUE;
        }

        if (t->resizeCol >= 0)
        {
            int dw = x - t->resizeStartX;
            int nw = t->resizeStartW + dw;
            if (nw < t->cols[t->resizeCol].minWidth) nw = t->cols[t->resizeCol].minWidth;
            t->cols[t->resizeCol].width = nw;
            ClampScroll(t);
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (t->vDrag)
        {
            RECT body;
            int areaH = VisRowsArea(t, &body);
            int contentH = ContentHeight(t);
            RECT th;
            if (VThumb(t, &th))
            {
                int thumbH = th.bottom - th.top;
                int track = areaH - thumbH;
                if (track > 0)
                {
                    int ty = y - t->dragOff - body.top;
                    t->scrollY = MulDiv(ty, contentH - areaH, track);
                    ClampScroll(t);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }
        if (t->hDrag)
        {
            RECT rc;
            GetClientRect(hwnd, &rc);
            int client = rc.right - rc.left;
            int total = TotalColsWidth(t);
            RECT th;
            if (HThumb(t, &th))
            {
                int thumbW = th.right - th.left;
                int track = client - thumbW;
                if (track > 0)
                {
                    int tx = x - t->dragOff;
                    t->scrollX = MulDiv(tx, total - client, track);
                    ClampScroll(t);
                    InvalidateRect(hwnd, NULL, FALSE);
                }
            }
            return 0;
        }

        /* hover updates */
        int oldHot = t->hotRow, oldHH = t->hotHeader;
        BOOL oldVHot = t->vHot, oldHHot = t->hHot;

        RECT th;
        t->vHot = VThumb(t, &th) && PtInRect(&th, POINT{ x, y });
        t->hHot = HThumb(t, &th) && PtInRect(&th, POINT{ x, y });

        if (HeaderSeparatorHit(t, x, y) >= 0)
            SetCursor(LoadCursorW(NULL, IDC_SIZEWE));
        else
            SetCursor(LoadCursorW(NULL, IDC_ARROW));

        t->hotHeader = HeaderColHit(t, x, y);
        t->hotRow = (y >= t->headerH && !t->vHot && !t->hHot) ? RowAtY(t, y) : -1;

        if (oldHot != t->hotRow || oldHH != t->hotHeader ||
            oldVHot != t->vHot || oldHHot != t->hHot)
            InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    }

    case WM_MOUSELEAVE:
        t->tracking = FALSE;
        if (t->hotRow != -1 || t->hotHeader != -1 || t->vHot || t->hHot)
        {
            t->hotRow = -1;
            t->hotHeader = -1;
            t->vHot = t->hHot = FALSE;
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
    {
        int x = GET_X_LPARAM(lp), y = GET_Y_LPARAM(lp);
        SetFocus(hwnd);

        int sep = HeaderSeparatorHit(t, x, y);
        if (sep >= 0)
        {
            t->resizeCol = sep;
            t->resizeStartX = x;
            t->resizeStartW = t->cols[sep].width;
            SetCapture(hwnd);
            return 0;
        }

        RECT th;
        if (VThumb(t, &th) && PtInRect(&th, POINT{ x, y }))
        {
            t->vDrag = TRUE;
            t->dragOff = y - th.top;
            SetCapture(hwnd);
            return 0;
        }
        if (HThumb(t, &th) && PtInRect(&th, POINT{ x, y }))
        {
            t->hDrag = TRUE;
            t->dragOff = x - th.left;
            SetCapture(hwnd);
            return 0;
        }

        if (y < t->headerH)
        {
            int col = HeaderColHit(t, x, y);
            if (col >= 0 && t->owner)
            {
                int id = t->cols[col].id;
                BOOL desc = (t->sortCol == id) ? !t->sortDesc : TRUE;
                t->sortCol = id;
                t->sortDesc = desc;
                t->owner->TLOnSort(id, desc);
                InvalidateRect(hwnd, NULL, FALSE);
            }
            return 0;
        }

        int idx = RowAtY(t, y);
        if (idx >= 0)
        {
            TLRow& row = t->rows[idx];
            if (ChevronHit(t, idx, x) && (row.expandable || row.kind == TLR_GROUP))
            {
                row.expanded = !row.expanded;
                if (t->owner) t->owner->TLOnToggle(row.data, row.expanded);
                return 0;
            }
            SetSel(t, idx, TRUE);
        }
        else
        {
            SetSel(t, -1, TRUE);
        }
        return 0;
    }

    case WM_LBUTTONUP:
        if (t->resizeCol >= 0 || t->vDrag || t->hDrag)
        {
            t->resizeCol = -1;
            t->vDrag = t->hDrag = FALSE;
            ReleaseCapture();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDBLCLK:
    {
        int y = GET_Y_LPARAM(lp);
        int idx = RowAtY(t, y);
        if (idx >= 0)
        {
            TLRow& row = t->rows[idx];
            if (row.kind == TLR_GROUP || row.expandable)
            {
                row.expanded = !row.expanded;
                if (t->owner) t->owner->TLOnToggle(row.data, row.expanded);
            }
            else if (t->owner)
            {
                t->owner->TLOnActivate(row.data);
            }
        }
        return 0;
    }

    case WM_RBUTTONDOWN:
    {
        int y = GET_Y_LPARAM(lp);
        SetFocus(hwnd);
        int idx = RowAtY(t, y);
        if (idx >= 0)
            SetSel(t, idx, TRUE);
        return 0;
    }

    case WM_RBUTTONUP:
    {
        int idx = SelIdx(t);
        if (idx >= 0 && t->owner && t->rows[idx].kind != TLR_GROUP)
        {
            POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
            ClientToScreen(hwnd, &pt);
            t->owner->TLOnContext(t->rows[idx].data, pt);
        }
        return 0;
    }

    case WM_KEYDOWN:
    {
        int idx = SelIdx(t);
        switch (wp)
        {
        case VK_UP:    KeyNav(t, -1, FALSE); return 0;
        case VK_DOWN:  KeyNav(t, +1, FALSE); return 0;
        case VK_HOME:  KeyNav(t, -1, TRUE); return 0;
        case VK_END:   KeyNav(t, +1, TRUE); return 0;
        case VK_PRIOR: KeyNav(t, -10, FALSE); return 0;
        case VK_NEXT:  KeyNav(t, +10, FALSE); return 0;
        case VK_LEFT:
            if (idx >= 0)
            {
                TLRow& row = t->rows[idx];
                if ((row.expandable || row.kind == TLR_GROUP) && row.expanded)
                {
                    row.expanded = FALSE;
                    if (t->owner) t->owner->TLOnToggle(row.data, FALSE);
                }
            }
            return 0;
        case VK_RIGHT:
            if (idx >= 0)
            {
                TLRow& row = t->rows[idx];
                if ((row.expandable || row.kind == TLR_GROUP) && !row.expanded)
                {
                    row.expanded = TRUE;
                    if (t->owner) t->owner->TLOnToggle(row.data, TRUE);
                }
            }
            return 0;
        case VK_RETURN:
            if (idx >= 0 && t->owner)
            {
                TLRow& row = t->rows[idx];
                if (row.kind == TLR_GROUP || row.expandable)
                {
                    row.expanded = !row.expanded;
                    t->owner->TLOnToggle(row.data, row.expanded);
                }
                else
                {
                    t->owner->TLOnActivate(row.data);
                }
            }
            return 0;
        case VK_DELETE:
            if (idx >= 0 && t->owner && t->rows[idx].kind == TLR_ITEM)
                t->owner->TLOnDelete(t->rows[idx].data);
            return 0;
        case VK_APPS:
            if (idx >= 0 && t->owner && t->rows[idx].kind == TLR_ITEM)
            {
                RECT body;
                VisRowsArea(t, &body);
                POINT pt = { S(60), body.top + idx * t->rowH - t->scrollY + t->rowH / 2 };
                ClientToScreen(hwnd, &pt);
                t->owner->TLOnContext(t->rows[idx].data, pt);
            }
            return 0;
        }
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* ------------------------------------------------------------------ */
/*  Public API                                                         */
/* ------------------------------------------------------------------ */

void TL_Register(void)
{
    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.style = CS_DBLCLKS;
    wc.lpfnWndProc = TLProc;
    wc.hInstance = g_app.hInst;
    wc.hCursor = NULL;
    wc.lpszClassName = TL_CLASS;
    RegisterClassW(&wc);
}

HWND TL_Create(HWND parent, ITreeListOwner* owner, BOOL doubleHeader)
{
    HWND h = CreateWindowExW(0, TL_CLASS, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                             0, 0, 100, 100, parent, NULL, g_app.hInst, owner);
    if (h)
    {
        TreeList* t = TL(h);
        t->dblHeader = doubleHeader;
        TLMetrics(t);
    }
    return h;
}

void TL_SetColumns(HWND h, const TLColumn* cols, int n)
{
    TreeList* t = TL(h);
    t->cols.Clear();
    for (int i = 0; i < n; i++)
        t->cols.Push(cols[i]);
    TLMetrics(t);
    InvalidateRect(h, NULL, FALSE);
}

void TL_SetRows(HWND h, const TLRow* rows, int n)
{
    TreeList* t = TL(h);
    t->rows.Clear();
    for (int i = 0; i < n; i++)
        t->rows.Push(rows[i]);
    ClampScroll(t);
    if (t->hotRow >= t->rows.n) t->hotRow = -1;
    InvalidateRect(h, NULL, FALSE);
}

void TL_Refresh(HWND h)
{
    InvalidateRect(h, NULL, FALSE);
}

LONGLONG TL_GetSelKey(HWND h)
{
    return TL(h)->selKey;
}

void TL_SetSelKey(HWND h, LONGLONG key)
{
    TreeList* t = TL(h);
    t->selKey = key;
    int idx = SelIdx(t);
    if (idx >= 0)
        EnsureVisible(t, idx);
    InvalidateRect(h, NULL, FALSE);
    if (t->owner) t->owner->TLOnSelChanged();
}

LPARAM TL_GetSelData(HWND h)
{
    TreeList* t = TL(h);
    int idx = SelIdx(t);
    return idx >= 0 ? t->rows[idx].data : 0;
}

void TL_SetSort(HWND h, int col, BOOL desc)
{
    TreeList* t = TL(h);
    t->sortCol = col;
    t->sortDesc = desc;
    InvalidateRect(h, NULL, FALSE);
}

int TL_GetSortCol(HWND h, BOOL* desc)
{
    TreeList* t = TL(h);
    if (desc) *desc = t->sortDesc;
    return t->sortCol;
}

void TL_SetEmptyText(HWND h, const WCHAR* txt)
{
    TreeList* t = TL(h);
    StringCchCopyW(t->emptyText, _countof(t->emptyText), txt ? txt : L"");
    InvalidateRect(h, NULL, FALSE);
}
