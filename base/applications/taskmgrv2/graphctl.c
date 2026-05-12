/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Graph control — window class "TaskMgrV2.GraphCtl"
 *              Single or tiled mini-graph with GDI+ gradient fill and
 *              optional kernel-times overlay.  ~300 lines of new code;
 *              no relationship to legacy base/applications/taskmgr/graphctl.c.
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.7, §5.3, §6.
 */

#include "precomp.h"
#include <math.h>

/* =========================================================================
 * GDI+ flat C API forward declarations
 * (Cannot include <gdiplus.h> in C mode — it requires PROPID/IStream from
 *  COM headers that are not pulled in by precomp.h.  Declare only what we
 *  use here, same pattern as theme.c.)
 * ========================================================================= */
typedef float                REAL;
typedef DWORD                ARGB;
typedef DWORD                GpStatus;
typedef void                 GpGraphics;
typedef void                 GpBrush;
typedef void                 GpPath;
typedef void                 GpLineGradient;

typedef struct { REAL X; REAL Y; }              GpPointF;
typedef struct { REAL X; REAL Y;
                 REAL Width; REAL Height; }      GpRectF;

/* GpStatus return values */
#define Ok  0

/* GDI+ enum values used by graphctl */
#define FillModeAlternate          0
#define SmoothingModeAntiAlias     4
#define LinearGradientModeVertical 1
#define WrapModeTile               0

GpStatus WINAPI GdipCreateFromHDC(HDC hdc, GpGraphics **graphics);
GpStatus WINAPI GdipDeleteGraphics(GpGraphics *graphics);
GpStatus WINAPI GdipSetSmoothingMode(GpGraphics *graphics, INT smoothingMode);
GpStatus WINAPI GdipCreatePath(INT fillMode, GpPath **path);
GpStatus WINAPI GdipDeletePath(GpPath *path);
GpStatus WINAPI GdipStartPathFigure(GpPath *path);
GpStatus WINAPI GdipClosePathFigure(GpPath *path);
GpStatus WINAPI GdipAddPathLine(GpPath *path, REAL x1, REAL y1, REAL x2, REAL y2);
GpStatus WINAPI GdipAddPathLine2(GpPath *path, const GpPointF *points, INT count);
GpStatus WINAPI GdipFillPath(GpGraphics *graphics, GpBrush *brush, GpPath *path);
GpStatus WINAPI GdipDeleteBrush(GpBrush *brush);
GpStatus WINAPI GdipCreateLineBrushFromRect(const GpRectF *rect, ARGB color1,
                                             ARGB color2, INT mode, INT wrapMode,
                                             GpLineGradient **lineGradient);

/* =========================================================================
 * Constants / helpers
 * ========================================================================= */

#define GC_MAX_SERIES       256
#define GC_HISTORY_LEN      PERFV2_HISTORY_SECONDS
#define GC_WNDCLASS         L"TaskMgrV2.GraphCtl"
#define GUTTER_UNSCALED     4   /* gap between tiles, unscaled px */

static BOOL s_classRegistered = FALSE;

/* =========================================================================
 * Per-series ring buffer
 * ========================================================================= */
typedef struct _GC_SERIES
{
    BYTE vals   [GC_HISTORY_LEN];
    BYTE kernels[GC_HISTORY_LEN];
    DWORD count;   /* valid samples, 0..GC_HISTORY_LEN */
    WCHAR title[32];
} GC_SERIES;

/* =========================================================================
 * GC_CONTROL structure (opaque from header)
 * ========================================================================= */
struct _GC_CONTROL
{
    HWND      hWnd;
    int       ctlId;
    GC_MODE   mode;
    GC_FORMAT fmt;

    DWORD     seriesCount;
    GC_SERIES series[GC_MAX_SERIES];
};

/* =========================================================================
 * Forward declarations
 * ========================================================================= */
static LRESULT CALLBACK GC_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

static void GC_Paint      (GC_CONTROL *gc, HDC hdc, const RECT *rcClient);
static void GC_PaintSingle(GC_CONTROL *gc, HDC hdc, const RECT *rc,
                           DWORD seriesIdx, BOOL showTitle);
static void GC_PaintGrid  (GC_CONTROL *gc, HDC hdc, const RECT *rcClient);
static void GC_DrawBorder (HDC hdc, const RECT *rc, COLORREF clr);
static void GC_DrawGridLines(HDC hdc, const RECT *rc, COLORREF clrGrid, BOOL summaryView);
static void GC_DrawSeries (HDC hdc, const RECT *rc,
                           const BYTE *vals, DWORD count,
                           COLORREF clrLine, COLORREF clrFill,
                           BYTE fillAlpha, COLORREF clrBg,
                           BOOL isKernel);

/* =========================================================================
 * Class registration
 * ========================================================================= */
static BOOL GC_EnsureClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc;
    if (s_classRegistered)
        return TRUE;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = GC_WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = NULL;   /* we paint entirely */
    wc.lpszClassName = GC_WNDCLASS;
    wc.cbWndExtra    = sizeof(GC_CONTROL *);

    if (!RegisterClassExW(&wc))
        return FALSE;

    s_classRegistered = TRUE;
    return TRUE;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

GC_CONTROL *GraphCtl_Create(HWND hParent, int ctlId, const GC_FORMAT *fmt)
{
    GC_CONTROL *gc;
    HINSTANCE hInst = (HINSTANCE)GetWindowLongPtrW(hParent, GWLP_HINSTANCE);

    if (!GC_EnsureClass(hInst))
        return NULL;

    gc = (GC_CONTROL *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*gc));
    if (!gc)
        return NULL;

    gc->ctlId       = ctlId;
    gc->mode        = GC_MODE_SINGLE;
    gc->seriesCount = 1;
    gc->fmt         = *fmt;

    gc->hWnd = CreateWindowExW(
        0, GC_WNDCLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 10, 10,
        hParent, (HMENU)(INT_PTR)ctlId,
        hInst, gc);

    if (!gc->hWnd)
    {
        HeapFree(GetProcessHeap(), 0, gc);
        return NULL;
    }

    return gc;
}

void GraphCtl_Destroy(GC_CONTROL *gc)
{
    if (!gc) return;
    if (gc->hWnd)
        DestroyWindow(gc->hWnd);
    HeapFree(GetProcessHeap(), 0, gc);
}

HWND GraphCtl_Hwnd(GC_CONTROL *gc)
{
    return gc ? gc->hWnd : NULL;
}

void GraphCtl_SetMode(GC_CONTROL *gc, GC_MODE mode)
{
    if (!gc) return;
    gc->mode = mode;
    GraphCtl_Invalidate(gc);
}

void GraphCtl_SetSeries(GC_CONTROL *gc, DWORD seriesCount)
{
    if (!gc) return;
    if (seriesCount > GC_MAX_SERIES)
        seriesCount = GC_MAX_SERIES;
    gc->seriesCount = seriesCount;
    GraphCtl_Invalidate(gc);
}

void GraphCtl_SetFormat(GC_CONTROL *gc, const GC_FORMAT *fmt)
{
    if (!gc || !fmt) return;
    gc->fmt = *fmt;
    GraphCtl_Invalidate(gc);
}

void GraphCtl_SetTitles(GC_CONTROL *gc, LPCWSTR const *titles, DWORD count)
{
    DWORD i;
    if (!gc || !titles) return;
    for (i = 0; i < count && i < GC_MAX_SERIES; i++)
    {
        if (titles[i])
            StringCchCopyW(gc->series[i].title,
                           ARRAYSIZE(gc->series[i].title),
                           titles[i]);
        else
            gc->series[i].title[0] = L'\0';
    }
}

void GraphCtl_SetShowKernel(GC_CONTROL *gc, BOOL bShow)
{
    if (!gc) return;
    gc->fmt.showKernel = bShow;
    GraphCtl_Invalidate(gc);
}

void GraphCtl_SetSummaryView(GC_CONTROL *gc, BOOL bSummary)
{
    if (!gc) return;
    gc->fmt.summaryView = bSummary;
    GraphCtl_Invalidate(gc);
}

void GraphCtl_PushFrame(GC_CONTROL *gc,
                        const BYTE *values,
                        const BYTE *kernels,
                        DWORD count)
{
    DWORD i;
    if (!gc || !values) return;
    if (count > gc->seriesCount) count = gc->seriesCount;

    for (i = 0; i < count; i++)
    {
        GC_SERIES *s = &gc->series[i];
        DWORD n = s->count;

        if (n < GC_HISTORY_LEN)
        {
            s->vals   [n] = values[i];
            s->kernels[n] = kernels ? kernels[i] : 0;
            s->count++;
        }
        else
        {
            /* ring-shift left */
            MoveMemory(s->vals,    s->vals    + 1, GC_HISTORY_LEN - 1);
            MoveMemory(s->kernels, s->kernels + 1, GC_HISTORY_LEN - 1);
            s->vals   [GC_HISTORY_LEN - 1] = values[i];
            s->kernels[GC_HISTORY_LEN - 1] = kernels ? kernels[i] : 0;
        }
    }
}

void GraphCtl_SetHistory(GC_CONTROL *gc,
                         DWORD seriesIdx,
                         const BYTE *vals,
                         const BYTE *kernels,
                         DWORD count)
{
    GC_SERIES *s;
    DWORD copy;

    if (!gc || !vals) return;
    if (seriesIdx >= GC_MAX_SERIES) return;

    s    = &gc->series[seriesIdx];
    copy = count < GC_HISTORY_LEN ? count : GC_HISTORY_LEN;

    ZeroMemory(s->vals,    sizeof(s->vals));
    ZeroMemory(s->kernels, sizeof(s->kernels));

    /* newest sample at index [count-1], place so newest is at [copy-1] */
    CopyMemory(s->vals,    vals    + (count - copy), copy);
    if (kernels)
        CopyMemory(s->kernels, kernels + (count - copy), copy);

    s->count = copy;

    /* grow seriesCount if caller pushed beyond current max */
    if (seriesIdx >= gc->seriesCount)
        gc->seriesCount = seriesIdx + 1;
}

void GraphCtl_Invalidate(GC_CONTROL *gc)
{
    if (gc && gc->hWnd)
        InvalidateRect(gc->hWnd, NULL, FALSE);
}

int GraphCtl_HitTestTile(GC_CONTROL *gc, POINT pt)
{
    RECT rc;
    int  W, H, gutter, cols, rows, tileW, tileH;
    DWORD LP, i;
    double aspect;

    if (!gc || gc->mode != GC_MODE_GRID)
        return -1;

    GetClientRect(gc->hWnd, &rc);
    W = rc.right  - rc.left;
    H = rc.bottom - rc.top;

    LP     = gc->seriesCount;
    gutter = Theme_DpiScale(GUTTER_UNSCALED);
    aspect = (W > 0 && H > 0) ? (double)W / (double)H : 1.0;

    cols = (DWORD)ceil(sqrt((double)LP * aspect));
    if (cols == 0) cols = 1;
    rows = (LP + cols - 1) / cols;
    while (rows > 1 && (DWORD)((rows - 1) * cols) >= LP) rows--;

    tileW = (W - (cols + 1) * gutter) / cols;
    tileH = (H - (rows + 1) * gutter) / rows;

    for (i = 0; i < LP; i++)
    {
        int col  = i % cols;
        int row  = i / cols;
        RECT tile;
        tile.left   = gutter + col * (tileW + gutter);
        tile.top    = gutter + row * (tileH + gutter);
        tile.right  = tile.left + tileW;
        tile.bottom = tile.top  + tileH;

        if (PtInRect(&tile, pt))
            return (int)i;
    }
    return -1;
}

/* =========================================================================
 * Window procedure
 * ========================================================================= */
static LRESULT CALLBACK GC_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    GC_CONTROL *gc = (GC_CONTROL *)GetWindowLongPtrW(hwnd, 0);

    switch (msg)
    {
        case WM_CREATE:
        {
            CREATESTRUCTW *cs = (CREATESTRUCTW *)lp;
            SetWindowLongPtrW(hwnd, 0, (LONG_PTR)cs->lpCreateParams);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;   /* we fill everything in WM_PAINT */

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            if (gc)
            {
                RECT rc;
                GetClientRect(hwnd, &rc);

                /* Double-buffer */
                HDC     hdcMem = CreateCompatibleDC(hdc);
                HBITMAP hbm    = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
                HBITMAP hOld   = (HBITMAP)SelectObject(hdcMem, hbm);

                GC_Paint(gc, hdcMem, &rc);

                BitBlt(hdc, 0, 0, rc.right, rc.bottom, hdcMem, 0, 0, SRCCOPY);

                SelectObject(hdcMem, hOld);
                DeleteObject(hbm);
                DeleteDC(hdcMem);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_SIZE:
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;

        default:
            break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

/* =========================================================================
 * Paint helpers
 * ========================================================================= */

static void GC_FillSolid(HDC hdc, const RECT *rc, COLORREF clr)
{
    HBRUSH br = CreateSolidBrush(clr);
    FillRect(hdc, rc, br);
    DeleteObject(br);
}

static void GC_DrawBorder(HDC hdc, const RECT *rc, COLORREF clr)
{
    HPEN pen  = CreatePen(PS_SOLID, 1, clr);
    HPEN pOld = (HPEN)SelectObject(hdc, pen);
    HBRUSH bOld = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));

    Rectangle(hdc, rc->left, rc->top, rc->right, rc->bottom);

    SelectObject(hdc, pOld);
    SelectObject(hdc, bOld);
    DeleteObject(pen);
}

/* Draw light hairlines — every 10% vertically, every 6 columns horizontally */
static void GC_DrawGridLines(HDC hdc, const RECT *rc, COLORREF clrGrid, BOOL summaryView)
{
    int i, W, H;
    HPEN pen, pOld;

    if (summaryView) return;

    W = rc->right  - rc->left;
    H = rc->bottom - rc->top;

    pen  = CreatePen(PS_SOLID, 1, clrGrid);
    pOld = (HPEN)SelectObject(hdc, pen);

    /* horizontal lines every 10% */
    for (i = 1; i < 10; i++)
    {
        int y = rc->top + (H * i) / 10;
        MoveToEx(hdc, rc->left,  y, NULL);
        LineTo  (hdc, rc->right, y);
    }

    /* vertical lines every 6 samples (= 6 seconds) */
    {
        int samples = GC_HISTORY_LEN; /* 60 */
        int step    = 6;
        for (i = step; i < samples; i += step)
        {
            int x = rc->right - (W * i) / samples;
            if (x > rc->left && x < rc->right)
            {
                MoveToEx(hdc, x, rc->top,    NULL);
                LineTo  (hdc, x, rc->bottom);
            }
        }
    }

    SelectObject(hdc, pOld);
    DeleteObject(pen);
}

/* Draw the line-series + gradient fill using GDI+ flat C API.
 * Falls back to a stippled rect fill if GDI+ is not available (GdipCreateFromHDC fails). */
static void GC_DrawSeries(HDC hdc, const RECT *rc,
                          const BYTE *vals, DWORD count,
                          COLORREF clrLine, COLORREF clrFill,
                          BYTE fillAlpha, COLORREF clrBg,
                          BOOL isKernel)
{
    int W, H, i;
    POINT *pts;

    if (count == 0) return;

    W = rc->right  - rc->left;
    H = rc->bottom - rc->top;

    if (W <= 0 || H <= 0) return;

    pts = (POINT *)HeapAlloc(GetProcessHeap(), 0, (count + 2) * sizeof(POINT));
    if (!pts) return;

    /* Build polyline points — newest sample at right edge */
    for (i = 0; i < (int)count; i++)
    {
        int x = rc->left + W - ((int)(count - 1 - i) * W) / ((int)count - 1 > 0 ? (int)count - 1 : 1);
        int y = rc->bottom - (vals[i] * H) / 100;
        pts[i].x = x;
        pts[i].y = y;
    }

    /* --- GDI+ gradient fill ------------------------------------------------ */
    if (!isKernel && fillAlpha > 0)
    {
        GpGraphics       *graphics = NULL;
        GpPath           *path     = NULL;
        GpLineGradient   *brush    = NULL;
        GpRectF           fillRect;
        ARGB           colorTop, colorBot;
        BYTE rT, gT, bT, rB, gB, bB;
        BOOL gdipOk = FALSE;

        if (GdipCreateFromHDC(hdc, &graphics) == Ok)
        {
            gdipOk = TRUE;
            GdipSetSmoothingMode(graphics, SmoothingModeAntiAlias);
        }

        if (gdipOk && GdipCreatePath(FillModeAlternate, &path) == Ok)
        {
            /* Path: line points, then bottom-right, bottom-left, close */
            GdipStartPathFigure(path);

            /* Add the data points as a polyline */
            {
                GpPointF *ptF = (GpPointF *)HeapAlloc(GetProcessHeap(), 0,
                                                       count * sizeof(GpPointF));
                if (ptF)
                {
                    DWORD pi;
                    for (pi = 0; pi < count; pi++)
                    {
                        ptF[pi].X = (REAL)pts[pi].x;
                        ptF[pi].Y = (REAL)pts[pi].y;
                    }
                    GdipAddPathLine2(path, ptF, (INT)count);
                    HeapFree(GetProcessHeap(), 0, ptF);
                }
            }

            /* Close down to bottom */
            GdipAddPathLine(path,
                            (REAL)pts[count - 1].x, (REAL)pts[count - 1].y,
                            (REAL)pts[count - 1].x, (REAL)rc->bottom);
            GdipAddPathLine(path,
                            (REAL)pts[count - 1].x, (REAL)rc->bottom,
                            (REAL)pts[0].x,          (REAL)rc->bottom);
            GdipClosePathFigure(path);

            /* Build top color with fillAlpha, bottom color fully transparent */
            rT = GetRValue(clrFill); gT = GetGValue(clrFill); bT = GetBValue(clrFill);
            rB = GetRValue(clrBg);   gB = GetGValue(clrBg);   bB = GetBValue(clrBg);
            colorTop = ((ARGB)fillAlpha << 24) | ((ARGB)rT << 16) | ((ARGB)gT << 8) | bT;
            colorBot = ((ARGB)0         << 24) | ((ARGB)rB << 16) | ((ARGB)gB << 8) | bB;

            fillRect.X      = (REAL)rc->left;
            fillRect.Y      = (REAL)rc->top;
            fillRect.Width  = (REAL)(rc->right  - rc->left);
            fillRect.Height = (REAL)(rc->bottom - rc->top);

            if (GdipCreateLineBrushFromRect(&fillRect, colorTop, colorBot,
                                            LinearGradientModeVertical, WrapModeTile,
                                            &brush) == Ok)
            {
                GdipFillPath(graphics, (GpBrush *)brush, path);
                GdipDeleteBrush((GpBrush *)brush);
            }

            GdipDeletePath(path);
        }

        if (!gdipOk)
        {
            /* Fallback: stippled fill using GDI */
            WORD stipple[8] = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };
            HBITMAP hStip = CreateBitmap(8, 8, 1, 1, stipple);
            HBRUSH  hBr   = CreatePatternBrush(hStip);
            SetBkColor  (hdc, clrBg);
            SetTextColor(hdc, clrFill);
            /* fill below the curve approximately */
            {
                RECT fillRc = *rc;
                fillRc.top = rc->top + H / 4;
                FillRect(hdc, &fillRc, hBr);
            }
            DeleteObject(hBr);
            DeleteObject(hStip);
        }

        if (graphics) GdipDeleteGraphics(graphics);
    }

    /* --- GDI line stroke --------------------------------------------------- */
    {
        HPEN pen, pOld;
        if (isKernel)
            pen = CreatePen(PS_SOLID, 1, clrLine);
        else
            pen = CreatePen(PS_SOLID, 2, clrLine);   /* ~1.5 px, best effort in GDI */

        pOld = (HPEN)SelectObject(hdc, pen);
        Polyline(hdc, pts, (int)count);
        SelectObject(hdc, pOld);
        DeleteObject(pen);
    }

    HeapFree(GetProcessHeap(), 0, pts);
}

/* Paint one graph tile for series 'seriesIdx' into 'rc'.
 * showTitle = TRUE draws "CPU N" in 8pt dim at top-left (non-summary mode). */
static void GC_PaintSingle(GC_CONTROL *gc, HDC hdc, const RECT *rc,
                           DWORD seriesIdx, BOOL showTitle)
{
    const GC_SERIES *s;
    const MODERN_THEME *th = Theme_Current();
    RECT inner = *rc;

    if (seriesIdx >= gc->seriesCount)
        return;
    s = &gc->series[seriesIdx];

    /* Background */
    GC_FillSolid(hdc, rc, gc->fmt.clrBg);

    /* Border (1 px inset) */
    GC_DrawBorder(hdc, rc, gc->fmt.clrBorder);
    InflateRect(&inner, -1, -1);

    /* Grid lines */
    GC_DrawGridLines(hdc, &inner, gc->fmt.clrGrid, gc->fmt.summaryView);

    /* Utilization fill + line */
    GC_DrawSeries(hdc, &inner,
                  s->vals, s->count,
                  gc->fmt.clrLine, gc->fmt.clrFill,
                  gc->fmt.fillAlpha, gc->fmt.clrBg,
                  FALSE);

    /* Kernel overlay (line only, fillAlpha = 0) */
    if (gc->fmt.showKernel && s->count > 0)
    {
        GC_DrawSeries(hdc, &inner,
                      s->kernels, s->count,
                      gc->fmt.clrKernel, gc->fmt.clrKernel,
                      0, gc->fmt.clrBg,
                      TRUE);
    }

    /* Series title in non-summary mode */
    if (showTitle && !gc->fmt.summaryView && s->title[0])
    {
        HFONT hOld;
        RECT  txtRc;
        SetBkMode  (hdc, TRANSPARENT);
        SetTextColor(hdc, th->clrTextDim);
        hOld        = (HFONT)SelectObject(hdc, Theme_FontUISmall());
        txtRc       = inner;
        txtRc.left  += 3;
        txtRc.top   += 2;
        txtRc.right -= 3;
        txtRc.bottom = txtRc.top + 14;
        DrawTextW(hdc, s->title, -1, &txtRc,
                  DT_LEFT | DT_TOP | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, hOld);
    }
}

/* Paint the NxM grid (GC_MODE_GRID) */
static void GC_PaintGrid(GC_CONTROL *gc, HDC hdc, const RECT *rcClient)
{
    int W, H, gutter, cols, rows, tileW, tileH;
    DWORD LP, i;
    double aspect;

    W = rcClient->right  - rcClient->left;
    H = rcClient->bottom - rcClient->top;

    LP     = gc->seriesCount;
    gutter = Theme_DpiScale(GUTTER_UNSCALED);

    if (LP == 0 || W <= 0 || H <= 0)
    {
        GC_FillSolid(hdc, rcClient, gc->fmt.clrBg);
        return;
    }

    /* --- Auto-grid algorithm (spec §6.2) ----------------------------------- */
    aspect = (double)W / (double)H;
    cols   = (int)ceil(sqrt((double)LP * aspect));
    if (cols == 0) cols = 1;
    rows   = ((int)LP + cols - 1) / cols;      /* ceil(LP / cols) */

    /* Rebalance: drop a row if it would be empty */
    while (rows > 1 && (DWORD)((rows - 1) * cols) >= LP) rows--;

    tileW = (W - (cols + 1) * gutter) / cols;
    tileH = (H - (rows + 1) * gutter) / rows;

    if (tileW < 4) tileW = 4;
    if (tileH < 4) tileH = 4;

    /* Background */
    GC_FillSolid(hdc, rcClient, gc->fmt.clrBg);

    /* Draw each tile */
    for (i = 0; i < LP; i++)
    {
        int col = (int)i % cols;
        int row = (int)i / cols;
        RECT tile;
        tile.left   = gutter + col * (tileW + gutter);
        tile.top    = gutter + row * (tileH + gutter);
        tile.right  = tile.left + tileW;
        tile.bottom = tile.top  + tileH;

        GC_PaintSingle(gc, hdc, &tile, i, TRUE /* showTitle */);
    }
}

/* Top-level paint dispatcher */
static void GC_Paint(GC_CONTROL *gc, HDC hdc, const RECT *rcClient)
{
    if (gc->mode == GC_MODE_GRID)
    {
        GC_PaintGrid(gc, hdc, rcClient);
    }
    else
    {
        /* GC_MODE_SINGLE — series 0 fills the whole area */
        GC_PaintSingle(gc, hdc, rcClient, 0, FALSE /* no title in single mode */);
    }
}

/* =========================================================================
 * GraphCtl_PaintToDC — public off-screen render of series 0
 * ========================================================================= */
void GraphCtl_PaintToDC(GC_CONTROL *gc, HDC hdc, const RECT *rc)
{
    if (!gc || !hdc || !rc) return;
    /* Render series 0 as a mini single-graph — no title, summary-view rules */
    GC_PaintSingle(gc, hdc, rc, 0, FALSE);
}
