/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Double buffering, GDI/GDI+ drawing helpers, graphs, formatting
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include "app.h"

using namespace Gdiplus;

/* ------------------------------------------------------------------ */
/*  Double buffer                                                      */
/* ------------------------------------------------------------------ */

HDC BufPaint::Begin(HDC hdcTarget, const RECT* rcPaint)
{
    target = hdcTarget;
    rc = *rcPaint;
    int w = rc.right - rc.left;
    int h = rc.bottom - rc.top;
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    dc = CreateCompatibleDC(target);
    bmp = CreateCompatibleBitmap(target, w, h);
    bmpOld = (HBITMAP)SelectObject(dc, bmp);
    SetViewportOrgEx(dc, -rc.left, -rc.top, NULL);
    return dc;
}

void BufPaint::End(void)
{
    if (!dc) return;
    SetViewportOrgEx(dc, 0, 0, NULL);
    BitBlt(target, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top,
           dc, 0, 0, SRCCOPY);
    SelectObject(dc, bmpOld);
    DeleteObject(bmp);
    DeleteDC(dc);
    dc = NULL; bmp = NULL; bmpOld = NULL;
}

/* ------------------------------------------------------------------ */
/*  Primitives                                                         */
/* ------------------------------------------------------------------ */

Color GP(COLORREF c, BYTE a)
{
    return Color(a, GetRValue(c), GetGValue(c), GetBValue(c));
}

void FillRect32(HDC dc, const RECT& r, COLORREF c)
{
    COLORREF old = SetBkColor(dc, c);
    ExtTextOutW(dc, 0, 0, ETO_OPAQUE, &r, L"", 0, NULL);
    SetBkColor(dc, old);
}

static void AddRoundPath(GraphicsPath& path, const RectF& r, float rad)
{
    float d = rad * 2.0f;
    if (d > r.Width) d = r.Width;
    if (d > r.Height) d = r.Height;
    path.AddArc(r.X, r.Y, d, d, 180, 90);
    path.AddArc(r.X + r.Width - d, r.Y, d, d, 270, 90);
    path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0, 90);
    path.AddArc(r.X, r.Y + r.Height - d, d, d, 90, 90);
    path.CloseFigure();
}

void FillRoundRect(HDC dc, const RECT& r, COLORREF fill, COLORREF border, int radius)
{
    Graphics g(dc);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    RectF rf((float)r.left, (float)r.top,
             (float)(r.right - r.left), (float)(r.bottom - r.top));

    GraphicsPath path(FillModeAlternate);
    if (radius > 0)
        AddRoundPath(path, RectF(rf.X, rf.Y, rf.Width - 1.0f, rf.Height - 1.0f), (float)radius);
    else
        path.AddRectangle(RectF(rf.X, rf.Y, rf.Width - 1.0f, rf.Height - 1.0f));

    if (fill != CLR_NONE)
    {
        SolidBrush br(GP(fill));
        g.FillPath((Brush*)&br, &path);
    }
    if (border != CLR_NONE)
    {
        Pen pen(GP(border), 1.0f);
        g.DrawPath(&pen, &path);
    }
}

void DrawTextClip(HDC dc, const WCHAR* txt, const RECT& r, HFONT f, COLORREF c, UINT dtFlags)
{
    if (!txt || !*txt) return;
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT rc = r;
    DrawTextW(dc, txt, -1, &rc, dtFlags | DT_NOPREFIX);
    SelectObject(dc, old);
}

void DrawVLine(HDC dc, int x, int y0, int y1, COLORREF c)
{
    RECT r = { x, y0, x + 1, y1 };
    FillRect32(dc, r, c);
}

void DrawHLine(HDC dc, int y, int x0, int x1, COLORREF c)
{
    RECT r = { x0, y, x1, y + 1 };
    FillRect32(dc, r, c);
}

/* ------------------------------------------------------------------ */
/*  Graph rendering (Win11 perf style: grid + line + soft fill)        */
/* ------------------------------------------------------------------ */

static void DrawGraphContent(HDC dc,
                             const RECT& r,
                             const HistRing* h,
                             const GraphStyle& gs,
                             REAL scale)
{
    int w = r.right - r.left;
    int ht = r.bottom - r.top;
    if (w < 8 || ht < 8) return;

    Graphics g(dc);
    g.SetClip(Rect(r.left, r.top, w, ht), CombineModeIntersect);
    g.SetSmoothingMode(SmoothingModeHighQuality);
    g.SetCompositingQuality(CompositingQualityHighQuality);
    g.SetPixelOffsetMode(PixelOffsetModeHalf);

    COLORREF bg = g_t.dark ? Blend(g_t.listBg, RGB(0, 0, 0), 20)
                           : RGB(0xFF, 0xFF, 0xFF);
    /* panel */
    {
        SolidBrush br(GP(bg));
        g.FillRectangle((Brush*)&br, r.left, r.top, w, ht);
    }

    /* grid: vertical lines scroll conceptually; static 10x4 grid reads the same */
    if (gs.grid)
    {
        Pen grid(GP(gs.line, g_t.dark ? 46 : 36), scale);
        int cols = 10, rows = 4;
        for (int i = 1; i < cols; i++)
        {
            int x = r.left + MulDiv(w, i, cols);
            g.DrawLine(&grid, x, r.top, x, r.bottom - 1);
        }
        for (int i = 1; i < rows; i++)
        {
            int y = r.top + MulDiv(ht, i, rows);
            g.DrawLine(&grid, r.left, y, r.right - 1, y);
        }
    }

    double yMax = gs.yMax;
    if (yMax <= 0)
    {
        /* auto-scale to a tidy ceiling above the peak */
        double peak = h ? h->Max() : 0;
        if (gs.second && gs.second->Max() > peak) peak = gs.second->Max();
        double ceil0 = 1024.0;          /* 1 KB/s floor */
        while (ceil0 < peak) ceil0 *= 2;
        yMax = ceil0;
    }

    /* build polyline; ring holds HIST_N samples across the width */
    const HistRing* series[2] = { h, gs.second };
    for (int s = 1; s >= 0; s--)
    {
        const HistRing* hr = series[s];
        if (!hr || hr->count < 2) continue;

        PointF pts[HIST_N + 2];
        int n = hr->count;
        /* newest sample pinned at right edge; window slides left */
        for (int i = 0; i < n; i++)
        {
            double vx = r.right - 1 - (double)(n - 1 - i) * (w - 1) / (HIST_N - 1);
            double t = hr->v[i] / yMax;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            double vy = r.bottom - 1 - t * (ht - 2);
            pts[i] = PointF((float)vx, (float)vy);
        }

        if (s == 0)
        {
            /* soft fill under primary line */
            GraphicsPath fillPath(FillModeAlternate);
            PointF* fp = pts;
            fillPath.AddLines(fp, n);
            fillPath.AddLine(pts[n - 1], PointF((float)(r.right - 1), (float)(r.bottom - 1)));
            fillPath.AddLine(PointF((float)(r.right - 1), (float)(r.bottom - 1)),
                             PointF(pts[0].X, (float)(r.bottom - 1)));
            fillPath.CloseFigure();
            SolidBrush fillBr(GP(gs.line, g_t.dark ? 60 : 40));
            g.FillPath((Brush*)&fillBr, &fillPath);

            Pen pen(GP(gs.line), 1.6f * scale);
            /* The local GDI+ path widener implements this shape as bevel. */
            pen.SetLineJoin(LineJoinBevel);
            pen.SetStartCap(LineCapRound);
            pen.SetEndCap(LineCapRound);
            g.DrawLines(&pen, pts, n);
        }
        else
        {
            COLORREF secondLine = gs.secondLine ? gs.secondLine : gs.line;
            Pen pen(GP(secondLine, 220), 1.4f * scale);
            REAL dash[2] = { 3.0f, 3.0f };
            pen.SetDashPattern(dash, 2);
            g.DrawLines(&pen, pts, n);
        }
    }

    if (gs.border)
    {
        Pen pen(GP(gs.line, g_t.dark ? 140 : 120), scale);
        g.DrawRectangle(&pen, r.left, r.top, w - 1, ht - 1);
    }
}

void DrawGraph(HDC dc, const RECT& r, const HistRing* h, const GraphStyle& gs)
{
    int width = r.right - r.left;
    int height = r.bottom - r.top;
    if (width < 8 || height < 8)
        return;

    GraphStyle directStyle = gs;
    directStyle.border = FALSE;
    DrawGraphContent(dc, r, h, directStyle, 1.0f);

    if (gs.border)
    {
        COLORREF background = g_t.dark ? Blend(g_t.listBg, RGB(0, 0, 0), 20)
                                       : RGB(0xFF, 0xFF, 0xFF);
        int alpha = g_t.dark ? 140 : 120;
        COLORREF border = Blend(background, gs.line, alpha * 100 / 255);
        DrawHLine(dc, r.top, r.left, r.right, border);
        DrawHLine(dc, r.bottom - 1, r.left, r.right, border);
        DrawVLine(dc, r.left, r.top, r.bottom, border);
        DrawVLine(dc, r.right - 1, r.top, r.bottom, border);
    }
}

/* ------------------------------------------------------------------ */
/*  Value formatting                                                   */
/* ------------------------------------------------------------------ */

void FmtThousands(ULONGLONG v, WCHAR* buf, int cch)
{
    WCHAR raw[32];
    StringCchPrintfW(raw, _countof(raw), L"%I64u", v);
    int len = lstrlenW(raw);
    int out = 0;
    for (int i = 0; i < len && out < cch - 1; i++)
    {
        if (i && ((len - i) % 3) == 0)
            buf[out++] = L',';
        buf[out++] = raw[i];
    }
    buf[out] = 0;
}

void FmtBytes(ULONGLONG v, WCHAR* buf, int cch)
{
    const WCHAR* unit = L"B";
    double d = (double)v;
    if (d >= 1024.0 * 1024.0 * 1024.0 * 1024.0) { d /= 1024.0 * 1024.0 * 1024.0 * 1024.0; unit = L"TB"; }
    else if (d >= 1024.0 * 1024.0 * 1024.0) { d /= 1024.0 * 1024.0 * 1024.0; unit = L"GB"; }
    else if (d >= 1024.0 * 1024.0) { d /= 1024.0 * 1024.0; unit = L"MB"; }
    else if (d >= 1024.0) { d /= 1024.0; unit = L"KB"; }
    StringCchPrintfW(buf, cch, (d >= 100 || d == (double)(LONGLONG)d) ? L"%.0f %s" : L"%.1f %s", d, unit);
}

void FmtMemMB(ULONGLONG v, WCHAR* buf, int cch)
{
    double mb = (double)v / (1024.0 * 1024.0);
    if (mb >= 1000.0)
    {
        WCHAR th[32];
        FmtThousands((ULONGLONG)mb, th, _countof(th));
        WCHAR frac[8];
        StringCchPrintfW(frac, _countof(frac), L"%.1f", mb - (double)(ULONGLONG)mb);
        /* "1,234.5 MB" - graft fractional digit onto grouped integer */
        StringCchPrintfW(buf, cch, L"%s%s MB", th, frac + 1);
    }
    else
    {
        StringCchPrintfW(buf, cch, L"%.1f MB", mb);
    }
}

void FmtRate(double bps, WCHAR* buf, int cch)
{
    if (bps < 1024.0 * 100.0)  /* < 0.1 MB/s */
    {
        StringCchPrintfW(buf, cch, L"%.1f KB/s", bps / 1024.0);
        return;
    }
    StringCchPrintfW(buf, cch, L"%.1f MB/s", bps / (1024.0 * 1024.0));
}

void FmtMbps(double bps, WCHAR* buf, int cch)
{
    double mbps = bps * 8.0 / 1000000.0;
    StringCchPrintfW(buf, cch, L"%.1f Mbps", mbps);
}

void FmtPct(double p, WCHAR* buf, int cch)
{
    if (p < 0) p = 0;
    if (p > 100) p = 100;
    if (p < 9.95)
        StringCchPrintfW(buf, cch, L"%.1f%%", p);
    else
        StringCchPrintfW(buf, cch, L"%.0f%%", p);
}

void FmtCpuTime(LONGLONG t100ns, WCHAR* buf, int cch)
{
    ULONGLONG secs = (ULONGLONG)(t100ns / 10000000);
    StringCchPrintfW(buf, cch, L"%I64u:%02u:%02u",
                     secs / 3600, (UINT)((secs / 60) % 60), (UINT)(secs % 60));
}

void FmtUptime(ULONGLONG secs, WCHAR* buf, int cch)
{
    StringCchPrintfW(buf, cch, L"%I64u:%02u:%02u:%02u",
                     secs / 86400, (UINT)((secs / 3600) % 24),
                     (UINT)((secs / 60) % 60), (UINT)(secs % 60));
}
