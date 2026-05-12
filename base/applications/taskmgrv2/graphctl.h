/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Graph control — single or tiled mini-graph, GDI+ fill, kernel overlay
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.7. This file is independent of the legacy base/applications/taskmgr/graphctl.c.
 */
#pragma once

/* -------------------------------------------------------------------------
 * Mode
 * ----------------------------------------------------------------------- */
typedef enum _GC_MODE
{
    GC_MODE_SINGLE,   /* one full-area line+fill graph  */
    GC_MODE_GRID      /* NxM mini-graphs, one per series */
} GC_MODE;

/* -------------------------------------------------------------------------
 * Format / appearance
 * ----------------------------------------------------------------------- */
typedef struct _GC_FORMAT
{
    COLORREF clrBg;
    COLORREF clrLine;
    COLORREF clrFill;
    BYTE     fillAlpha;
    COLORREF clrKernel;
    COLORREF clrGrid;
    COLORREF clrBorder;
    int      gridCellPx;   /* unscaled px — minimum tile dimension hint */
    BOOL     showKernel;
    BOOL     summaryView;
} GC_FORMAT;

/* -------------------------------------------------------------------------
 * Opaque handle
 * ----------------------------------------------------------------------- */
typedef struct _GC_CONTROL GC_CONTROL;

/* -------------------------------------------------------------------------
 * Lifecycle
 * ----------------------------------------------------------------------- */

/* Registers the "TaskMgrV2.GraphCtl" window class (lazy) and creates a
 * child HWND. ctlId is used as the child-window integer identifier. */
GC_CONTROL *GraphCtl_Create (HWND hParent, int ctlId, const GC_FORMAT *fmt);
void        GraphCtl_Destroy(GC_CONTROL *gc);
HWND        GraphCtl_Hwnd   (GC_CONTROL *gc);

/* -------------------------------------------------------------------------
 * Configuration
 * ----------------------------------------------------------------------- */
void        GraphCtl_SetMode      (GC_CONTROL *gc, GC_MODE mode);
void        GraphCtl_SetSeries    (GC_CONTROL *gc, DWORD seriesCount);
void        GraphCtl_SetFormat    (GC_CONTROL *gc, const GC_FORMAT *fmt);
void        GraphCtl_SetTitles    (GC_CONTROL *gc, LPCWSTR const *titles, DWORD count);
void        GraphCtl_SetShowKernel(GC_CONTROL *gc, BOOL bShow);
void        GraphCtl_SetSummaryView(GC_CONTROL *gc, BOOL bSummary);

/* -------------------------------------------------------------------------
 * Data — push/set history
 * ----------------------------------------------------------------------- */

/* Push a single new frame for all series simultaneously.
 * values[i] and kernels[i] are the latest BYTE (0..100) for series i.
 * kernels may be NULL if showKernel is FALSE. */
void        GraphCtl_PushFrame (GC_CONTROL *gc,
                                const BYTE *values,
                                const BYTE *kernels,
                                DWORD       count);

/* Bulk-replace the entire history for one series.
 * vals / kernels point to arrays of 'count' BYTE values, newest last.
 * kernels may be NULL. */
void        GraphCtl_SetHistory(GC_CONTROL *gc,
                                DWORD       seriesIdx,
                                const BYTE *vals,
                                const BYTE *kernels,
                                DWORD       count);

/* -------------------------------------------------------------------------
 * Rendering / hit-test
 * ----------------------------------------------------------------------- */
void        GraphCtl_Invalidate  (GC_CONTROL *gc);
int         GraphCtl_HitTestTile (GC_CONTROL *gc, POINT pt); /* -1 = no tile */

/* Render the graph (series 0, GC_MODE_SINGLE) directly into an arbitrary DC.
 * Used by the owner-draw category list to paint mini-graphs off-screen. */
void        GraphCtl_PaintToDC   (GC_CONTROL *gc, HDC hdc, const RECT *rc);
