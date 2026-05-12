/*
 * PROJECT:     ReactOS Task Manager v2
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Performance page — CPU/Mem/Disk/Net/GPU categories,
 *              Win11-style graph panel + stats grid.
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 *
 * Spec §3.8, §5, §6, §7, §7.1, §8.
 *
 * Layout (96 DPI, see §5):
 *   24 px outer padding all sides.
 *   Left column: 256 px — category list, items 76 px tall each.
 *   Right column: remainder — heading, main graph, stats grid.
 *   Stats grid: 216 px tall, 8 rows × 2 cols.
 */

#include "precomp.h"
#include <windowsx.h>  /* GET_X_LPARAM / GET_Y_LPARAM */

/* =========================================================================
 * Layout constants (all unscaled, 96 DPI)
 * ========================================================================= */
#define PAD             24
#define LEFT_LIST_W     256
#define CAT_ITEM_H       76
#define CAT_GRAPH_W     168
#define CAT_GRAPH_H      42
#define HEADING_H        32
#define STATS_H         216
#define BRAND_FONT_H     12   /* pt — not a direct pixel measure */

/* Registry subkey for persistence (§7.1) */
#define PERF_REG_SUBKEY  L"Software\\ReactOS\\TaskMgrV2\\PerfPage"

/* Window class name */
#define PAGE_PERF_CLASS  L"TaskMgrV2.Page.Performance"

/* Context-menu child submenu item IDs for update-speed sub-menu (§7) */
#define IDC_CTX_VIEW_SUBMENU    1231  /* same as IDC_CTX_SPEED_MENU in resource.h */

/* Number of stats rows per column (§5) */
#define STATS_ROWS  8
#define STATS_COLS  2

/* =========================================================================
 * Per-instance state (stored as window property via SetPropW)
 * ========================================================================= */
typedef struct _PERF_PAGE_STATE
{
    /* Category list child HWNDs (owner-draw) */
    HWND hCatList;

    /* Right panel */
    HWND hBrandLabel;   /* CPU brand string, right-aligned */
    HWND hTimeAxisLeft; /* "60 seconds" */
    HWND hTimeAxisRight;/* "0" */

    /* Main graph control (full / grid depending on mode) */
    GC_CONTROL *gc;

    /* Per-category mini-graph controls (inside the category list) */
    GC_CONTROL *catGc[PERFCAT__COUNT];

    /* Stats label pairs: [row][col] — label text then value HWND */
    HWND hStatLabel[STATS_ROWS][STATS_COLS];
    HWND hStatValue[STATS_ROWS][STATS_COLS];

    /* State */
    PERF_CATEGORY   curCategory;
    CPU_GRAPH_MODE  cpuMode;
    BOOL            showKernel;
    BOOL            summaryView;

    /* Cached for layout */
    int cx, cy;
} PERF_PAGE_STATE;

/* =========================================================================
 * Persisted prefs (module-level, loaded by PagePerf_LoadPrefs)
 * ========================================================================= */
static CPU_GRAPH_MODE s_savedCpuMode    = CPU_GMODE_OVERALL;
static BOOL           s_savedKernel     = FALSE;
static BOOL           s_savedSummary    = FALSE;
static PERF_CATEGORY  s_savedCategory   = PERFCAT_CPU;

/* =========================================================================
 * Forward declarations
 * ========================================================================= */
static LRESULT CALLBACK PerfPage_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
static void PerfPage_Layout(HWND hPage, PERF_PAGE_STATE *st, int cx, int cy);
static void PerfPage_BuildContextMenu(HWND hPage, PERF_PAGE_STATE *st, POINT pt);
static void PerfPage_UpdateCpuMode(HWND hPage, PERF_PAGE_STATE *st, CPU_GRAPH_MODE mode);
static void PerfPage_UpdateStats(PERF_PAGE_STATE *st);
static void PerfPage_SetCategoryActive(PERF_PAGE_STATE *st, PERF_CATEGORY cat);
static GC_FORMAT PerfPage_MakeFormat(const MODERN_THEME *th, BOOL mini);
static void PerfPage_CreateStaticLabel(HWND hParent, HWND *phOut,
                                       DWORD style, HINSTANCE hInst);
static void PerfPage_CreateChildren(HWND hPage, HINSTANCE hInst, PERF_PAGE_STATE *st);
static void PerfPage_TickCpu(PERF_PAGE_STATE *st);
static void PerfPage_FormatUptime(ULONG64 seconds, LPWSTR buf, int cch);
static void PerfPage_FormatBytes(SIZE_T bytes, LPWSTR buf, int cch, BOOL wantMB);

/* =========================================================================
 * Class registration
 * ========================================================================= */
static BOOL s_perfPageClassReg = FALSE;

static BOOL PerfPage_EnsureClass(HINSTANCE hInst)
{
    WNDCLASSEXW wc;
    if (s_perfPageClassReg) return TRUE;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = PerfPage_WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW);
    wc.hbrBackground = NULL;
    wc.lpszClassName = PAGE_PERF_CLASS;
    wc.cbWndExtra    = 0;

    if (!RegisterClassExW(&wc)) return FALSE;
    s_perfPageClassReg = TRUE;
    return TRUE;
}

/* =========================================================================
 * Public API
 * ========================================================================= */

HWND PagePerf_Create(HWND hHost, HINSTANCE hInst)
{
    HWND hPage;
    PERF_PAGE_STATE *st;

    if (!PerfPage_EnsureClass(hInst))
        return NULL;

    hPage = CreateWindowExW(
        0, PAGE_PERF_CLASS, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 100, 100,
        hHost, NULL, hInst, NULL);

    if (!hPage) return NULL;

    st = (PERF_PAGE_STATE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*st));
    if (!st) { DestroyWindow(hPage); return NULL; }

    st->curCategory = s_savedCategory;
    st->cpuMode     = s_savedCpuMode;
    st->showKernel  = s_savedKernel;
    st->summaryView = s_savedSummary;

    SetPropW(hPage, L"state", (HANDLE)st);

    PerfPage_CreateChildren(hPage, hInst, st);

    return hPage;
}

void PagePerf_Tick(HWND hPage)
{
    PERF_PAGE_STATE *st = (PERF_PAGE_STATE *)GetPropW(hPage, L"state");
    if (!st) return;

    if (st->curCategory == PERFCAT_CPU)
        PerfPage_TickCpu(st);

    PerfPage_UpdateStats(st);
    GraphCtl_Invalidate(st->gc);

    /* Refresh mini-graphs in the category list */
    {
        BYTE buf[PERFV2_HISTORY_SECONDS];
        BYTE kbuf[PERFV2_HISTORY_SECONDS];
        DWORD cnt = 0;

        /* CPU mini-graph: overall */
        if (PerfV2_GetOverallHistory(buf, &cnt))
        {
            GraphCtl_SetHistory(st->catGc[PERFCAT_CPU], 0, buf, NULL, cnt);
            GraphCtl_Invalidate(st->catGc[PERFCAT_CPU]);
        }
        /* Other categories: zero-history in v1 — just invalidate */
        {
            int c;
            for (c = PERFCAT_MEMORY; c < PERFCAT__COUNT; c++)
            {
                GraphCtl_Invalidate(st->catGc[c]);
            }
        }
    }
}

void PagePerf_Resize(HWND hPage, int cx, int cy)
{
    PERF_PAGE_STATE *st = (PERF_PAGE_STATE *)GetPropW(hPage, L"state");
    if (!st) return;
    PerfPage_Layout(hPage, st, cx, cy);
}

void PagePerf_Destroy(HWND hPage)
{
    PERF_PAGE_STATE *st = (PERF_PAGE_STATE *)GetPropW(hPage, L"state");
    if (!st) return;

    PagePerf_SavePrefs();

    if (st->gc)
    {
        GraphCtl_Destroy(st->gc);
        st->gc = NULL;
    }
    {
        int c;
        for (c = 0; c < PERFCAT__COUNT; c++)
        {
            if (st->catGc[c])
            {
                GraphCtl_Destroy(st->catGc[c]);
                st->catGc[c] = NULL;
            }
        }
    }

    RemovePropW(hPage, L"state");
    HeapFree(GetProcessHeap(), 0, st);
    DestroyWindow(hPage);
}

void PagePerf_Theme(HWND hPage)
{
    PERF_PAGE_STATE *st = (PERF_PAGE_STATE *)GetPropW(hPage, L"state");
    const MODERN_THEME *th;
    GC_FORMAT fmt;
    int c;

    if (!st) return;
    th = Theme_Current();

    /* Refresh main graph format */
    fmt = PerfPage_MakeFormat(th, FALSE);
    GraphCtl_SetFormat(st->gc, &fmt);

    /* Refresh mini-graph formats */
    fmt = PerfPage_MakeFormat(th, TRUE);
    for (c = 0; c < PERFCAT__COUNT; c++)
        GraphCtl_SetFormat(st->catGc[c], &fmt);

    InvalidateRect(hPage, NULL, TRUE);
}

void PagePerf_SelectCategory(HWND hPage, PERF_CATEGORY cat)
{
    PERF_PAGE_STATE *st = (PERF_PAGE_STATE *)GetPropW(hPage, L"state");
    if (!st) return;
    if ((DWORD)cat >= PERFCAT__COUNT) return;
    PerfPage_SetCategoryActive(st, cat);
    InvalidateRect(hPage, NULL, FALSE);
    PagePerf_SavePrefs();
}

void PagePerf_SetCpuGraphMode(HWND hPage, CPU_GRAPH_MODE mode)
{
    PERF_PAGE_STATE *st = (PERF_PAGE_STATE *)GetPropW(hPage, L"state");
    if (!st) return;
    PerfPage_UpdateCpuMode(hPage, st, mode);
    PagePerf_SavePrefs();
}

CPU_GRAPH_MODE PagePerf_GetCpuGraphMode(HWND hPage)
{
    PERF_PAGE_STATE *st = (PERF_PAGE_STATE *)GetPropW(hPage, L"state");
    return st ? st->cpuMode : CPU_GMODE_OVERALL;
}

/* =========================================================================
 * Persistence (§7.1)
 * ========================================================================= */
void PagePerf_LoadPrefs(void)
{
    DWORD val;
    if (Reg_ReadDword(HKEY_CURRENT_USER, PERF_REG_SUBKEY, L"CpuGraphMode", &val))
        s_savedCpuMode = (CPU_GRAPH_MODE)(val <= CPU_GMODE_NUMA ? val : 0);
    if (Reg_ReadDword(HKEY_CURRENT_USER, PERF_REG_SUBKEY, L"ShowKernelTimes", &val))
        s_savedKernel = (val != 0);
    if (Reg_ReadDword(HKEY_CURRENT_USER, PERF_REG_SUBKEY, L"SummaryView", &val))
        s_savedSummary = (val != 0);
    if (Reg_ReadDword(HKEY_CURRENT_USER, PERF_REG_SUBKEY, L"LastCategory", &val))
        s_savedCategory = (PERF_CATEGORY)(val < PERFCAT__COUNT ? val : 0);
}

void PagePerf_SavePrefs(void)
{
    Reg_WriteDword(HKEY_CURRENT_USER, PERF_REG_SUBKEY, L"CpuGraphMode",    (DWORD)s_savedCpuMode);
    Reg_WriteDword(HKEY_CURRENT_USER, PERF_REG_SUBKEY, L"ShowKernelTimes", s_savedKernel  ? 1 : 0);
    Reg_WriteDword(HKEY_CURRENT_USER, PERF_REG_SUBKEY, L"SummaryView",     s_savedSummary ? 1 : 0);
    Reg_WriteDword(HKEY_CURRENT_USER, PERF_REG_SUBKEY, L"LastCategory",    (DWORD)s_savedCategory);
}

/* =========================================================================
 * Helpers: format strings
 * ========================================================================= */
static void PerfPage_FormatUptime(ULONG64 seconds, LPWSTR buf, int cch)
{
    ULONG64 s  = seconds % 60;
    ULONG64 m  = (seconds / 60) % 60;
    ULONG64 h  = (seconds / 3600) % 24;
    ULONG64 d  = seconds / 86400;
    StringCchPrintfW(buf, cch, L"%I64u:%02I64u:%02I64u:%02I64u", d, h, m, s);
}

/* Format bytes as MB or KB with one decimal. wantMB = TRUE for MB, FALSE = auto KB/MB */
static void PerfPage_FormatBytes(SIZE_T bytes, LPWSTR buf, int cch, BOOL wantMB)
{
    if (wantMB || bytes >= (1024 * 1024))
    {
        double mb = (double)bytes / (1024.0 * 1024.0);
        StringCchPrintfW(buf, cch, L"%.1f MB", mb);
    }
    else
    {
        double kb = (double)bytes / 1024.0;
        StringCchPrintfW(buf, cch, L"%.0f KB", kb);
    }
}

/* =========================================================================
 * Helper: GC_FORMAT
 * ========================================================================= */
static GC_FORMAT PerfPage_MakeFormat(const MODERN_THEME *th, BOOL mini)
{
    GC_FORMAT fmt;
    ZeroMemory(&fmt, sizeof(fmt));
    fmt.clrBg      = th->clrBg;
    fmt.clrLine    = th->clrGraphLine;
    fmt.clrFill    = th->clrGraphFill;
    fmt.fillAlpha  = th->graphFillAlpha;
    fmt.clrKernel  = th->clrGraphKernel;
    fmt.clrGrid    = th->clrGraphGrid;
    fmt.clrBorder  = th->clrCardBorder;
    fmt.gridCellPx = mini ? 0 : 60;
    fmt.showKernel = FALSE;
    fmt.summaryView= FALSE;
    return fmt;
}

/* =========================================================================
 * Helper: create a static text control
 * ========================================================================= */
static void PerfPage_CreateStaticLabel(HWND hParent, HWND *phOut,
                                       DWORD style, HINSTANCE hInst)
{
    *phOut = CreateWindowExW(0, L"STATIC", L"",
                             WS_CHILD | WS_VISIBLE | SS_LEFT | style,
                             0, 0, 10, 10,
                             hParent, NULL, hInst, NULL);
}

/* =========================================================================
 * Helper: create all child controls
 * ========================================================================= */

/* Stat labels in order (spec §5, 8 rows × 2 cols): */
static const UINT s_statLabelIds[STATS_ROWS][STATS_COLS] =
{
    /* row 0 */ { IDS_STAT_UTILIZATION, IDS_STAT_MAXSPEED   },
    /* row 1 */ { IDS_STAT_SPEED,       IDS_STAT_SOCKETS    },
    /* row 2 */ { IDS_STAT_PROCESSES,   IDS_STAT_CORES      },
    /* row 3 */ { IDS_STAT_THREADS,     IDS_STAT_LOGICAL_PROCS },
    /* row 4 */ { IDS_STAT_HANDLES,     IDS_STAT_VIRT       },
    /* row 5 */ { IDS_STAT_UPTIME,      IDS_STAT_L1CACHE    },
    /* row 6 */ { 0,                    IDS_STAT_L2CACHE    },
    /* row 7 */ { 0,                    IDS_STAT_L3CACHE    },
};

static void PerfPage_CreateChildren(HWND hPage, HINSTANCE hInst, PERF_PAGE_STATE *st)
{
    const MODERN_THEME *th = Theme_Current();
    GC_FORMAT fmt;
    int r, c;

    /* Brand label */
    st->hBrandLabel = CreateWindowExW(0, L"STATIC", L"",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        0, 0, 10, 14,
        hPage, NULL, hInst, NULL);
    if (st->hBrandLabel)
        SendMessageW(st->hBrandLabel, WM_SETFONT, (WPARAM)Theme_FontTitle(), FALSE);

    /* Category list (owner-draw listbox) */
    st->hCatList = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | LBS_OWNERDRAWFIXED | LBS_NOINTEGRALHEIGHT |
        LBS_NOTIFY | WS_VSCROLL,
        0, 0, Theme_DpiScale(LEFT_LIST_W), 100,
        hPage, (HMENU)(INT_PTR)1100, hInst, NULL);
    if (st->hCatList)
    {
        SendMessageW(st->hCatList, LB_SETITEMHEIGHT, 0, Theme_DpiScale(CAT_ITEM_H));
        SendMessageW(st->hCatList, LB_ADDSTRING, 0, (LPARAM)L"CPU");
        SendMessageW(st->hCatList, LB_ADDSTRING, 0, (LPARAM)L"Memory");
        SendMessageW(st->hCatList, LB_ADDSTRING, 0, (LPARAM)L"Disk 0");
        SendMessageW(st->hCatList, LB_ADDSTRING, 0, (LPARAM)L"Ethernet 0");
        SendMessageW(st->hCatList, LB_ADDSTRING, 0, (LPARAM)L"GPU 0");
        SendMessageW(st->hCatList, LB_SETCURSEL, (WPARAM)st->curCategory, 0);
    }

    /* Time axis labels */
    st->hTimeAxisLeft = CreateWindowExW(0, L"STATIC", L"60 seconds",
        WS_CHILD | WS_VISIBLE | SS_LEFT,
        0, 0, 80, 14, hPage, NULL, hInst, NULL);
    st->hTimeAxisRight = CreateWindowExW(0, L"STATIC", L"0",
        WS_CHILD | WS_VISIBLE | SS_RIGHT,
        0, 0, 30, 14, hPage, NULL, hInst, NULL);
    if (st->hTimeAxisLeft)
        SendMessageW(st->hTimeAxisLeft,  WM_SETFONT, (WPARAM)Theme_FontUISmall(), FALSE);
    if (st->hTimeAxisRight)
        SendMessageW(st->hTimeAxisRight, WM_SETFONT, (WPARAM)Theme_FontUISmall(), FALSE);

    /* Main graph control */
    fmt = PerfPage_MakeFormat(th, FALSE);
    fmt.showKernel  = st->showKernel;
    fmt.summaryView = st->summaryView;
    st->gc = GraphCtl_Create(hPage, 1101, &fmt);
    if (st->gc)
    {
        PerfPage_UpdateCpuMode(hPage, st, st->cpuMode);
    }

    /* Category mini-graph controls */
    {
        GC_FORMAT mfmt = PerfPage_MakeFormat(th, TRUE);
        int cat;
        for (cat = 0; cat < PERFCAT__COUNT; cat++)
        {
            st->catGc[cat] = GraphCtl_Create(hPage, 1110 + cat, &mfmt);
        }
    }

    /* Stats grid labels + value slots */
    for (r = 0; r < STATS_ROWS; r++)
    {
        for (c = 0; c < STATS_COLS; c++)
        {
            PerfPage_CreateStaticLabel(hPage, &st->hStatLabel[r][c], 0, hInst);
            PerfPage_CreateStaticLabel(hPage, &st->hStatValue[r][c], 0, hInst);

            if (st->hStatLabel[r][c])
            {
                SendMessageW(st->hStatLabel[r][c], WM_SETFONT, (WPARAM)Theme_FontUISmall(), FALSE);
                if (s_statLabelIds[r][c])
                {
                    WCHAR buf[80];
                    LoadStringW(g_hInst, s_statLabelIds[r][c], buf, ARRAYSIZE(buf));
                    SetWindowTextW(st->hStatLabel[r][c], buf);
                }
                else
                {
                    /* empty rows (6,7 left col) */
                    ShowWindow(st->hStatLabel[r][c], SW_HIDE);
                    ShowWindow(st->hStatValue[r][c], SW_HIDE);
                }
            }
            if (st->hStatValue[r][c])
                SendMessageW(st->hStatValue[r][c], WM_SETFONT, (WPARAM)Theme_FontStatNumber(), FALSE);
        }
    }
}

/* =========================================================================
 * Layout
 * ========================================================================= */
static void PerfPage_Layout(HWND hPage, PERF_PAGE_STATE *st, int cx, int cy)
{
    int pad, listW, rightX, rightW;
    int headY, graphY, statsY;
    int graphH, statsH;
    int axisH = Theme_DpiScale(14);
    int r, c, statColW, statCellH;
    HDWP hdwp;

    st->cx = cx;
    st->cy = cy;

    pad   = Theme_DpiScale(PAD);
    listW = Theme_DpiScale(LEFT_LIST_W);

    rightX = pad + listW + pad;
    rightW = cx - rightX - pad;
    if (rightW < 10) rightW = 10;

    headY  = pad;
    statsH = Theme_DpiScale(STATS_H);

    /* In summaryView mode the stats grid is hidden */
    if (st->summaryView)
        statsH = 0;

    graphY = headY + Theme_DpiScale(HEADING_H) + pad;
    graphH = cy - graphY - statsH - axisH - pad * 2;
    if (graphH < Theme_DpiScale(220))
        graphH = Theme_DpiScale(220);

    statsY = graphY + graphH + axisH + pad;

    hdwp = BeginDeferWindowPos(30);

    /* Category list */
    if (st->hCatList)
        hdwp = DeferWindowPos(hdwp, st->hCatList, NULL,
                              pad, graphY,
                              listW, cy - graphY - pad,
                              SWP_NOZORDER | SWP_NOACTIVATE);

    /* Brand label */
    if (st->hBrandLabel)
    {
        LPCWSTR brand = PerfV2_GetCpuBrandString();
        SetWindowTextW(st->hBrandLabel, brand ? brand : L"");
        hdwp = DeferWindowPos(hdwp, st->hBrandLabel, NULL,
                              rightX, headY,
                              rightW, Theme_DpiScale(HEADING_H),
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }

    /* Main graph */
    if (st->gc)
        hdwp = DeferWindowPos(hdwp, GraphCtl_Hwnd(st->gc), NULL,
                              rightX, graphY, rightW, graphH,
                              SWP_NOZORDER | SWP_NOACTIVATE);

    /* Time axis */
    if (st->hTimeAxisLeft)
        hdwp = DeferWindowPos(hdwp, st->hTimeAxisLeft, NULL,
                              rightX, graphY + graphH + 4,
                              80, axisH,
                              SWP_NOZORDER | SWP_NOACTIVATE);
    if (st->hTimeAxisRight)
        hdwp = DeferWindowPos(hdwp, st->hTimeAxisRight, NULL,
                              rightX + rightW - 20, graphY + graphH + 4,
                              20, axisH,
                              SWP_NOZORDER | SWP_NOACTIVATE);

    /* Stats grid — 8 rows, 2 columns */
    statColW  = rightW / STATS_COLS;
    statCellH = statsH / STATS_ROWS;
    if (statCellH < 1) statCellH = 1;

    for (r = 0; r < STATS_ROWS; r++)
    {
        for (c = 0; c < STATS_COLS; c++)
        {
            int x = rightX + c * statColW;
            int y = statsY + r * statCellH;
            int lh = Theme_DpiScale(9) + 2;  /* label height */
            int vh = statCellH - lh - 2;     /* value height */

            BOOL show = (s_statLabelIds[r][c] != 0);

            if (st->hStatLabel[r][c])
            {
                if (show && !st->summaryView)
                {
                    ShowWindow(st->hStatLabel[r][c], SW_SHOW);
                    hdwp = DeferWindowPos(hdwp, st->hStatLabel[r][c], NULL,
                                          x + 4, y, statColW - 8, lh,
                                          SWP_NOZORDER | SWP_NOACTIVATE);
                }
                else
                    ShowWindow(st->hStatLabel[r][c], SW_HIDE);
            }
            if (st->hStatValue[r][c])
            {
                if (show && !st->summaryView)
                {
                    ShowWindow(st->hStatValue[r][c], SW_SHOW);
                    hdwp = DeferWindowPos(hdwp, st->hStatValue[r][c], NULL,
                                          x + 4, y + lh, statColW - 8, vh,
                                          SWP_NOZORDER | SWP_NOACTIVATE);
                }
                else
                    ShowWindow(st->hStatValue[r][c], SW_HIDE);
            }
        }
    }

    /* Category mini-graphs — positioned inside the listbox at draw time;
     * we just size them off-screen since they are drawn by owner-draw
     * callback. Keep them at 0,0,1,1 invisible so they remain valid HWNDs
     * that GraphCtl can paint to an off-screen DC if needed.
     * (Their actual rendering is in the WM_DRAWITEM handler.)
     */
    {
        int cat;
        for (cat = 0; cat < PERFCAT__COUNT; cat++)
        {
            if (st->catGc[cat])
                MoveWindow(GraphCtl_Hwnd(st->catGc[cat]), -1, -1, 1, 1, FALSE);
        }
    }

    EndDeferWindowPos(hdwp);
}

/* =========================================================================
 * CPU graph mode change
 * ========================================================================= */
static void PerfPage_UpdateCpuMode(HWND hPage, PERF_PAGE_STATE *st, CPU_GRAPH_MODE mode)
{
    DWORD lpCount, numaCount;

    st->cpuMode        = mode;
    s_savedCpuMode     = mode;

    lpCount   = PerfV2_GetLPCount();
    numaCount = PerfV2_GetNumaNodeCount();

    switch (mode)
    {
        case CPU_GMODE_OVERALL:
            GraphCtl_SetMode   (st->gc, GC_MODE_SINGLE);
            GraphCtl_SetSeries (st->gc, 1);
            break;

        case CPU_GMODE_LOGICAL:
        {
            DWORD i;
            LPCWSTR *titles;

            GraphCtl_SetMode  (st->gc, GC_MODE_GRID);
            GraphCtl_SetSeries(st->gc, lpCount > 0 ? lpCount : 1);

            titles = (LPCWSTR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                          lpCount * sizeof(LPCWSTR));
            if (titles)
            {
                WCHAR (*strbuf)[16] = (WCHAR (*)[16])HeapAlloc(GetProcessHeap(), 0,
                                       lpCount * 16 * sizeof(WCHAR));
                if (strbuf)
                {
                    for (i = 0; i < lpCount; i++)
                    {
                        StringCchPrintfW(strbuf[i], 16, L"CPU %u", i);
                        titles[i] = strbuf[i];
                    }
                    GraphCtl_SetTitles(st->gc, titles, lpCount);
                    HeapFree(GetProcessHeap(), 0, strbuf);
                }
                HeapFree(GetProcessHeap(), 0, titles);
            }
            break;
        }

        case CPU_GMODE_NUMA:
        {
            DWORD i;
            LPCWSTR *titles;

            GraphCtl_SetMode  (st->gc, numaCount <= 1 ? GC_MODE_SINGLE : GC_MODE_GRID);
            GraphCtl_SetSeries(st->gc, numaCount > 0  ? numaCount : 1);

            titles = (LPCWSTR *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                          numaCount * sizeof(LPCWSTR));
            if (titles)
            {
                WCHAR (*strbuf)[16] = (WCHAR (*)[16])HeapAlloc(GetProcessHeap(), 0,
                                       numaCount * 16 * sizeof(WCHAR));
                if (strbuf)
                {
                    for (i = 0; i < numaCount; i++)
                    {
                        StringCchPrintfW(strbuf[i], 16, L"NUMA %u", i);
                        titles[i] = strbuf[i];
                    }
                    GraphCtl_SetTitles(st->gc, titles, numaCount);
                    HeapFree(GetProcessHeap(), 0, strbuf);
                }
                HeapFree(GetProcessHeap(), 0, titles);
            }
            break;
        }
    }

    GraphCtl_SetShowKernel  (st->gc, st->showKernel);
    GraphCtl_SetSummaryView (st->gc, st->summaryView);
    GraphCtl_Invalidate     (st->gc);
    (void)hPage;
}

/* =========================================================================
 * Update stats values
 * ========================================================================= */
static void PerfPage_UpdateStats(PERF_PAGE_STATE *st)
{
    WCHAR buf[64];
    DWORD proc, thr, hnd;
    ULONG64 uptime;
    SIZE_T l1, l2, l3;
    DWORD speedMhz, baseSpeedMhz;
    BOOL virt;
    PERFV2_TOPOLOGY topo;

    if (st->summaryView) return;
    if (st->curCategory != PERFCAT_CPU) return;

    PerfV2_GetCounts (&proc, &thr, &hnd);
    PerfV2_GetTopology(&topo);
    PerfV2_GetCacheSizes(&l1, &l2, &l3);
    uptime      = PerfV2_GetUptime();
    speedMhz    = PerfV2_GetSpeedMHz();
    baseSpeedMhz= PerfV2_GetBaseSpeedMHz();
    virt        = PerfV2_IsVirtualizationEnabled();

    /* Row 0, col 0: Utilization */
    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u%%",
                     PerfV2_GetCurrentOverallUtilization());
    SetWindowTextW(st->hStatValue[0][0], buf);

    /* Row 0, col 1: Maximum speed */
    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%.2f GHz", baseSpeedMhz / 1000.0);
    SetWindowTextW(st->hStatValue[0][1], buf);

    /* Row 1, col 0: Speed */
    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%.2f GHz", speedMhz / 1000.0);
    SetWindowTextW(st->hStatValue[1][0], buf);

    /* Row 1, col 1: Sockets */
    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u", topo.Sockets);
    SetWindowTextW(st->hStatValue[1][1], buf);

    /* Row 2, col 0: Processes */
    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u", proc);
    SetWindowTextW(st->hStatValue[2][0], buf);

    /* Row 2, col 1: Cores */
    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u", topo.Cores);
    SetWindowTextW(st->hStatValue[2][1], buf);

    /* Row 3, col 0: Threads */
    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u", thr);
    SetWindowTextW(st->hStatValue[3][0], buf);

    /* Row 3, col 1: Logical processors */
    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u", topo.LogicalProcessors);
    SetWindowTextW(st->hStatValue[3][1], buf);

    /* Row 4, col 0: Handles */
    StringCchPrintfW(buf, ARRAYSIZE(buf), L"%u", hnd);
    SetWindowTextW(st->hStatValue[4][0], buf);

    /* Row 4, col 1: Virtualization */
    SetWindowTextW(st->hStatValue[4][1], virt ? L"Enabled" : L"Disabled");

    /* Row 5, col 0: Up time */
    PerfPage_FormatUptime(uptime, buf, ARRAYSIZE(buf));
    SetWindowTextW(st->hStatValue[5][0], buf);

    /* Row 5, col 1: L1 cache */
    PerfPage_FormatBytes(l1, buf, ARRAYSIZE(buf), FALSE);
    SetWindowTextW(st->hStatValue[5][1], buf);

    /* Row 6, col 1: L2 cache */
    PerfPage_FormatBytes(l2, buf, ARRAYSIZE(buf), TRUE);
    SetWindowTextW(st->hStatValue[6][1], buf);

    /* Row 7, col 1: L3 cache */
    PerfPage_FormatBytes(l3, buf, ARRAYSIZE(buf), TRUE);
    SetWindowTextW(st->hStatValue[7][1], buf);
}

/* =========================================================================
 * CPU tick — push histories to main graph control
 * ========================================================================= */
static void PerfPage_TickCpu(PERF_PAGE_STATE *st)
{
    BYTE  vals[PERFV2_HISTORY_SECONDS];
    BYTE  kvals[PERFV2_HISTORY_SECONDS];
    DWORD cnt = 0, kcnt = 0;

    switch (st->cpuMode)
    {
        case CPU_GMODE_OVERALL:
            if (PerfV2_GetOverallHistory(vals, &cnt))
            {
                PerfV2_GetOverallKernelHistory(kvals, &kcnt);
                GraphCtl_SetHistory(st->gc, 0, vals, st->showKernel ? kvals : NULL, cnt);
            }
            break;

        case CPU_GMODE_LOGICAL:
        {
            DWORD lp, lpCount = PerfV2_GetLPCount();
            for (lp = 0; lp < lpCount; lp++)
            {
                cnt  = 0; kcnt = 0;
                if (PerfV2_GetLPHistory(lp, vals, &cnt))
                {
                    PerfV2_GetLPKernelHistory(lp, kvals, &kcnt);
                    GraphCtl_SetHistory(st->gc, lp, vals,
                                        st->showKernel ? kvals : NULL, cnt);
                }
            }
            break;
        }

        case CPU_GMODE_NUMA:
        {
            DWORD node, numaCount = PerfV2_GetNumaNodeCount();
            for (node = 0; node < numaCount; node++)
            {
                cnt  = 0; kcnt = 0;
                if (PerfV2_GetNumaHistory(node, vals, &cnt))
                {
                    PerfV2_GetNumaKernelHistory(node, kvals, &kcnt);
                    GraphCtl_SetHistory(st->gc, node, vals,
                                        st->showKernel ? kvals : NULL, cnt);
                }
            }
            break;
        }
    }
}

/* =========================================================================
 * Category selection visual update
 * ========================================================================= */
static void PerfPage_SetCategoryActive(PERF_PAGE_STATE *st, PERF_CATEGORY cat)
{
    st->curCategory  = cat;
    s_savedCategory  = cat;
    if (st->hCatList)
        SendMessageW(st->hCatList, LB_SETCURSEL, (WPARAM)cat, 0);
}

/* =========================================================================
 * Context menu (§7)
 * ========================================================================= */
static void PerfPage_BuildContextMenu(HWND hPage, PERF_PAGE_STATE *st, POINT pt)
{
    HMENU hMenu   = CreatePopupMenu();
    HMENU hMode   = CreatePopupMenu();
    HMENU hView   = CreatePopupMenu();
    HMENU hSpeed  = CreatePopupMenu();
    DWORD numaCount = PerfV2_GetNumaNodeCount();
    int   result;

    if (!hMenu || !hMode || !hView || !hSpeed) goto cleanup;

    /* "Change graph to" sub-menu */
    AppendMenuW(hMode, MF_STRING |
                (st->cpuMode == CPU_GMODE_OVERALL ? MF_CHECKED : 0),
                IDC_CTX_MODE_OVERALL, L"Overall utilization");
    AppendMenuW(hMode, MF_STRING |
                (st->cpuMode == CPU_GMODE_LOGICAL ? MF_CHECKED : 0),
                IDC_CTX_MODE_LOGICAL, L"Logical processors");
    AppendMenuW(hMode, MF_STRING |
                (st->cpuMode == CPU_GMODE_NUMA ? MF_CHECKED : 0) |
                (numaCount <= 1 ? MF_GRAYED : 0),
                IDC_CTX_MODE_NUMA, L"NUMA nodes");

    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hMode, L"Change graph to");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    /* Toggles */
    AppendMenuW(hMenu, MF_STRING | (st->showKernel  ? MF_CHECKED : 0),
                IDC_CTX_KERNEL,  L"Show kernel times");
    AppendMenuW(hMenu, MF_STRING | (st->summaryView ? MF_CHECKED : 0),
                IDC_CTX_SUMMARY, L"Graph summary view");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    /* "View" sub-menu */
    {
        DWORD ms = Shell_GetRefreshIntervalMs();
        AppendMenuW(hView, MF_STRING, IDC_CTX_REFRESH, L"Refresh now\tF5");
        AppendMenuW(hView, MF_SEPARATOR, 0, NULL);

        /* Update speed sub-menu */
        AppendMenuW(hSpeed, MF_STRING | (ms ==  250 ? MF_CHECKED : 0),
                    IDC_CTX_SPEED_HIGH,   L"High");
        AppendMenuW(hSpeed, MF_STRING | (ms == 1000 ? MF_CHECKED : 0),
                    IDC_CTX_SPEED_NORMAL, L"Normal");
        AppendMenuW(hSpeed, MF_STRING | (ms == 4000 ? MF_CHECKED : 0),
                    IDC_CTX_SPEED_LOW,    L"Low");
        AppendMenuW(hSpeed, MF_STRING | (ms ==    0 ? MF_CHECKED : 0),
                    IDC_CTX_SPEED_PAUSED, L"Paused");
        AppendMenuW(hView, MF_POPUP, (UINT_PTR)hSpeed, L"Update speed");
    }
    AppendMenuW(hMenu, MF_POPUP, (UINT_PTR)hView, L"View");
    AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);

    /* Copy */
    AppendMenuW(hMenu, MF_STRING, IDC_CTX_COPY, L"Copy\tCtrl+C");

    result = (int)TrackPopupMenu(hMenu,
                                 TPM_LEFTALIGN | TPM_TOPALIGN |
                                 TPM_RETURNCMD | TPM_NONOTIFY,
                                 pt.x, pt.y, 0, hPage, NULL);

    switch (result)
    {
        case IDC_CTX_MODE_OVERALL:
            PerfPage_UpdateCpuMode(hPage, st, CPU_GMODE_OVERALL);
            PagePerf_SavePrefs();
            break;
        case IDC_CTX_MODE_LOGICAL:
            PerfPage_UpdateCpuMode(hPage, st, CPU_GMODE_LOGICAL);
            PagePerf_SavePrefs();
            break;
        case IDC_CTX_MODE_NUMA:
            if (numaCount > 1)
            {
                PerfPage_UpdateCpuMode(hPage, st, CPU_GMODE_NUMA);
                PagePerf_SavePrefs();
            }
            break;

        case IDC_CTX_KERNEL:
            st->showKernel  = !st->showKernel;
            s_savedKernel   = st->showKernel;
            GraphCtl_SetShowKernel(st->gc, st->showKernel);
            PagePerf_SavePrefs();
            break;

        case IDC_CTX_SUMMARY:
            st->summaryView = !st->summaryView;
            s_savedSummary  = st->summaryView;
            GraphCtl_SetSummaryView(st->gc, st->summaryView);
            /* Re-layout so stats grid hides / shows */
            {
                RECT rc;
                GetClientRect(hPage, &rc);
                PerfPage_Layout(hPage, st, rc.right, rc.bottom);
            }
            PagePerf_SavePrefs();
            break;

        case IDC_CTX_REFRESH:
            Shell_RequestRefresh();
            break;

        case IDC_CTX_SPEED_HIGH:
            Shell_SetRefreshIntervalMs(250);
            break;
        case IDC_CTX_SPEED_NORMAL:
            Shell_SetRefreshIntervalMs(1000);
            break;
        case IDC_CTX_SPEED_LOW:
            Shell_SetRefreshIntervalMs(4000);
            break;
        case IDC_CTX_SPEED_PAUSED:
            Shell_SetRefreshIntervalMs(0);
            break;

        case IDC_CTX_COPY:
            /* Copy current CPU utilization as text to clipboard */
            {
                WCHAR cbuf[32];
                StringCchPrintfW(cbuf, ARRAYSIZE(cbuf), L"%u%%",
                                 PerfV2_GetCurrentOverallUtilization());
                if (OpenClipboard(hPage))
                {
                    SIZE_T sz = (lstrlenW(cbuf) + 1) * sizeof(WCHAR);
                    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sz);
                    if (hg)
                    {
                        void *p = GlobalLock(hg);
                        if (p) { CopyMemory(p, cbuf, sz); GlobalUnlock(hg); }
                        EmptyClipboard();
                        SetClipboardData(CF_UNICODETEXT, hg);
                    }
                    CloseClipboard();
                }
            }
            break;

        default:
            break;
    }

cleanup:
    if (hSpeed) DestroyMenu(hSpeed);
    if (hView)  DestroyMenu(hView);
    if (hMode)  DestroyMenu(hMode);
    if (hMenu)  DestroyMenu(hMenu);
}

/* =========================================================================
 * Window procedure
 * ========================================================================= */

/* Owner-draw category list item */
static void PerfPage_DrawCatItem(PERF_PAGE_STATE *st, DRAWITEMSTRUCT *dis)
{
    const MODERN_THEME *th = Theme_Current();
    RECT rc = dis->rcItem;
    BOOL selected = (dis->itemState & ODS_SELECTED) != 0;
    COLORREF clrBk = selected ? th->clrAccent : th->clrBg;
    COLORREF clrTx = selected ? th->clrAccentText : th->clrText;
    PERF_CATEGORY cat = (PERF_CATEGORY)dis->itemID;
    HBRUSH br;
    WCHAR label[64] = L"";
    HDC hdc = dis->hDC;

    /* Background */
    br = CreateSolidBrush(clrBk);
    FillRect(hdc, &rc, br);
    DeleteObject(br);

    /* Selection accent pill (3 px left, 24 px tall, vertically centered) */
    if (selected)
    {
        int pillH = Theme_DpiScale(24);
        int pillY = rc.top + (rc.bottom - rc.top - pillH) / 2;
        RECT pill = { rc.left, pillY, rc.left + Theme_DpiScale(3), pillY + pillH };
        br = CreateSolidBrush(th->clrAccentText);
        FillRect(hdc, &pill, br);
        DeleteObject(br);
    }

    /* Label */
    SendMessageW(dis->hwndItem, LB_GETTEXT, dis->itemID, (LPARAM)label);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, clrTx);
    {
        HFONT hOld = (HFONT)SelectObject(hdc, Theme_FontUI());
        RECT txtRc = rc;
        txtRc.left  += Theme_DpiScale(14);
        txtRc.top   += 4;
        txtRc.bottom = txtRc.top + Theme_DpiScale(14);
        DrawTextW(hdc, label, -1, &txtRc,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(hdc, hOld);
    }

    /* Mini-graph — render into an off-screen DC, then BitBlt */
    if (cat < PERFCAT__COUNT && st->catGc[cat])
    {
        int gW = Theme_DpiScale(CAT_GRAPH_W);
        int gH = Theme_DpiScale(CAT_GRAPH_H);
        int gX = rc.right  - gW - 4;
        int gY = rc.top    + (rc.bottom - rc.top - gH) / 2;
        RECT gRc = { 0, 0, gW, gH };

        HDC     hdcMem = CreateCompatibleDC(hdc);
        HBITMAP hbm    = CreateCompatibleBitmap(hdc, gW, gH);
        HBITMAP hOldBm = (HBITMAP)SelectObject(hdcMem, hbm);

        /* Render the mini-graph into the temp DC via the public API */
        GraphCtl_PaintToDC(st->catGc[cat], hdcMem, &gRc);

        BitBlt(hdc, gX, gY, gW, gH, hdcMem, 0, 0, SRCCOPY);

        SelectObject(hdcMem, hOldBm);
        DeleteObject(hbm);
        DeleteDC(hdcMem);
    }
}

static LRESULT CALLBACK PerfPage_WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    PERF_PAGE_STATE *st = (PERF_PAGE_STATE *)GetPropW(hwnd, L"state");

    switch (msg)
    {
        case WM_SIZE:
            if (st)
                PerfPage_Layout(hwnd, st, LOWORD(lp), HIWORD(lp));
            return 0;

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            {
                RECT rc;
                const MODERN_THEME *th = Theme_Current();
                HBRUSH br;
                GetClientRect(hwnd, &rc);
                br = CreateSolidBrush(th->clrBg);
                FillRect(hdc, &rc, br);
                DeleteObject(br);

                /* Page heading "Performance" */
                if (st)
                {
                    RECT  hRc;
                    HFONT hOld;
                    hRc        = rc;
                    hRc.left  += Theme_DpiScale(PAD) + Theme_DpiScale(LEFT_LIST_W) + Theme_DpiScale(PAD);
                    hRc.top   += Theme_DpiScale(PAD);
                    hRc.bottom = hRc.top + Theme_DpiScale(HEADING_H);
                    hRc.right -= Theme_DpiScale(PAD);

                    SetBkMode  (hdc, TRANSPARENT);
                    SetTextColor(hdc, th->clrText);
                    hOld = (HFONT)SelectObject(hdc, Theme_FontHeading());
                    DrawTextW(hdc, L"Performance", -1, &hRc,
                              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                    SelectObject(hdc, hOld);
                }
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT *dis = (DRAWITEMSTRUCT *)lp;
            if (st && dis && dis->hwndItem == st->hCatList)
            {
                PerfPage_DrawCatItem(st, dis);
                return TRUE;
            }
            break;
        }

        case WM_MEASUREITEM:
        {
            MEASUREITEMSTRUCT *mis = (MEASUREITEMSTRUCT *)lp;
            mis->itemHeight = (UINT)Theme_DpiScale(CAT_ITEM_H);
            return TRUE;
        }

        case WM_COMMAND:
        {
            WORD id = LOWORD(wp), notif = HIWORD(wp);
            if (st && id == 1100 && notif == LBN_SELCHANGE)
            {
                int sel = (int)SendMessageW(st->hCatList, LB_GETCURSEL, 0, 0);
                if (sel != LB_ERR && sel < PERFCAT__COUNT)
                    PerfPage_SetCategoryActive(st, (PERF_CATEGORY)sel);
            }
            return 0;
        }

        case WM_CONTEXTMENU:
        {
            if (st)
            {
                POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
                /* Only show menu if the click is over the main graph */
                if ((HWND)wp == GraphCtl_Hwnd(st->gc))
                    PerfPage_BuildContextMenu(hwnd, st, pt);
            }
            return 0;
        }

        /* Accelerator keys: forward from main window via WM_COMMAND */
        case WM_APP + 1:   /* posted by shell when IDA_PERF fires */
        {
            WORD cmdId = (WORD)wp;
            if (st) switch (cmdId)
            {
                case IDC_CTX_MODE_OVERALL:
                    PerfPage_UpdateCpuMode(hwnd, st, CPU_GMODE_OVERALL);
                    PagePerf_SavePrefs();
                    break;
                case IDC_CTX_MODE_LOGICAL:
                    PerfPage_UpdateCpuMode(hwnd, st, CPU_GMODE_LOGICAL);
                    PagePerf_SavePrefs();
                    break;
                case IDC_CTX_MODE_NUMA:
                    if (PerfV2_GetNumaNodeCount() > 1)
                    {
                        PerfPage_UpdateCpuMode(hwnd, st, CPU_GMODE_NUMA);
                        PagePerf_SavePrefs();
                    }
                    break;
                case IDC_CTX_KERNEL:
                    st->showKernel = !st->showKernel;
                    s_savedKernel  = st->showKernel;
                    GraphCtl_SetShowKernel(st->gc, st->showKernel);
                    PagePerf_SavePrefs();
                    break;
                case IDC_CTX_REFRESH:
                    Shell_RequestRefresh();
                    break;
                case IDC_CTX_COPY:
                {
                    POINT dummy = { 0, 0 };
                    ClientToScreen(hwnd, &dummy);
                    /* Re-use copy logic */
                    {
                        WCHAR cbuf[32];
                        StringCchPrintfW(cbuf, ARRAYSIZE(cbuf), L"%u%%",
                                         PerfV2_GetCurrentOverallUtilization());
                        if (OpenClipboard(hwnd))
                        {
                            SIZE_T sz = (lstrlenW(cbuf) + 1) * sizeof(WCHAR);
                            HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, sz);
                            if (hg)
                            {
                                void *p = GlobalLock(hg);
                                if (p) { CopyMemory(p, cbuf, sz); GlobalUnlock(hg); }
                                EmptyClipboard();
                                SetClipboardData(CF_UNICODETEXT, hg);
                            }
                            CloseClipboard();
                        }
                    }
                    break;
                }
            }
            return 0;
        }

        case WM_DESTROY:
            /* State freed by PagePerf_Destroy */
            return 0;

        default:
            break;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}
