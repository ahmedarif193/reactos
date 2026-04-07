/*
 * PROJECT:     ReactOS Task Manager
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Performance Page
 * COPYRIGHT:   Copyright 1999-2001 Brian Palmer <brianp@reactos.org>
 */

#include "precomp.h"
#include <shlwapi.h>
#include <math.h>

/* Per-LP CPU history graphs. [0] is the legacy dialog template control;
 * [1..nLPs-1] are dynamically created STATIC children used in "Per CPU" mode. */
static PTM_GRAPH_CONTROL PerfPageCpuGraphs = NULL;
static HWND             *PerfPageCpuHwnds  = NULL;
static ULONG             nLPs              = 1;

#define PerformancePageCpuUsageHistoryGraph   (PerfPageCpuGraphs[0])
#define hPerformancePageCpuUsageHistoryGraph  (PerfPageCpuHwnds[0])

TM_GRAPH_CONTROL PerformancePageMemUsageHistoryGraph;

HWND hPerformancePage;                /* Performance Property Page */
static HWND hCpuUsageGraph;                  /* CPU Usage Graph */
static HWND hMemUsageGraph;                  /* MEM Usage Graph */
HWND hPerformancePageMemUsageHistoryGraph;           /* Memory Usage History Graph */
static HWND hTotalsFrame;                    /* Totals Frame */
static HWND hCommitChargeFrame;              /* Commit Charge Frame */
static HWND hKernelMemoryFrame;              /* Kernel Memory Frame */
static HWND hPhysicalMemoryFrame;            /* Physical Memory Frame */
static HWND hCpuUsageFrame;
static HWND hMemUsageFrame;
static HWND hCpuUsageHistoryFrame;
static HWND hMemUsageHistoryFrame;
static HWND hCommitChargeTotalEdit;          /* Commit Charge Total Edit Control */
static HWND hCommitChargeLimitEdit;          /* Commit Charge Limit Edit Control */
static HWND hCommitChargePeakEdit;           /* Commit Charge Peak Edit Control */
static HWND hKernelMemoryTotalEdit;          /* Kernel Memory Total Edit Control */
static HWND hKernelMemoryPagedEdit;          /* Kernel Memory Paged Edit Control */
static HWND hKernelMemoryNonPagedEdit;       /* Kernel Memory NonPaged Edit Control */
static HWND hPhysicalMemoryTotalEdit;        /* Physical Memory Total Edit Control */
static HWND hPhysicalMemoryAvailableEdit;    /* Physical Memory Available Edit Control */
static HWND hPhysicalMemorySystemCacheEdit;  /* Physical Memory System Cache Edit Control */
static HWND hTotalsHandleCountEdit;          /* Total Handles Edit Control */
static HWND hTotalsProcessCountEdit;         /* Total Processes Edit Control */
static HWND hTotalsThreadCountEdit;          /* Total Threads Edit Control */

#ifdef RUN_PERF_PAGE
static HANDLE hPerformanceThread = NULL;
static DWORD  dwPerformanceThread;
#endif

static int nPerformancePageWidth;
static int nPerformancePageHeight;
static int lastX, lastY;
DWORD WINAPI PerformancePageRefreshThread(PVOID Parameter);

void AdjustFrameSize(HWND hCntrl, HWND hDlg, int nXDifference, int nYDifference, int pos)
{
    RECT  rc;
    int   cx, cy, sx, sy;

    GetClientRect(hCntrl, &rc);
    MapWindowPoints(hCntrl, hDlg, (LPPOINT)(PRECT)(&rc), sizeof(RECT)/sizeof(POINT));
    if (pos) {
        cx = rc.left;
        cy = rc.top;
        sx = rc.right - rc.left;
        switch (pos) {
        case 1:
            break;
        case 2:
            cy += nYDifference / 2;
            break;
        case 3:
            sx += nXDifference;
            break;
        case 4:
            cy += nYDifference / 2;
            sx += nXDifference;
            break;
        }
        sy = rc.bottom - rc.top + nYDifference / 2;
        SetWindowPos(hCntrl, NULL, cx, cy, sx, sy, SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOZORDER);
    } else {
        cx = rc.left + nXDifference;
        cy = rc.top + nYDifference;
        SetWindowPos(hCntrl, NULL, cx, cy, 0, 0, SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOSIZE|SWP_NOZORDER);
    }
    InvalidateRect(hCntrl, NULL, TRUE);
}

static inline
void AdjustControlPosition(HWND hCntrl, HWND hDlg, int nXDifference, int nYDifference)
{
    AdjustFrameSize(hCntrl, hDlg, nXDifference, nYDifference, 0);
}

/* Layout the N per-LP CPU history graphs inside hCpuUsageHistoryFrame.
 * "All CPUs" mode: graph[0] fills the frame, rest hidden.
 * "Per CPU" mode: NxM grid with cols = ceil(sqrt(N * aspect)), rebalanced. */
static void PerformancePage_LayoutCpuGraphs(HWND hDlg)
{
    RECT  cpuArea;
    ULONG i;

    if (!PerfPageCpuHwnds || !hCpuUsageHistoryFrame)
        return;

    GetWindowRect(hCpuUsageHistoryFrame, &cpuArea);
    MapWindowPoints(NULL, hDlg, (LPPOINT)&cpuArea, 2);
    InflateRect(&cpuArea, -3, -3);

    if (TaskManagerSettings.CPUHistory_OneGraphPerCPU)
    {
        int W = cpuArea.right - cpuArea.left;
        int H = cpuArea.bottom - cpuArea.top;
        double aspect;
        int cols, rows, gutter, tileW, tileH;

        if (W < 1) W = 1;
        if (H < 1) H = 1;
        aspect = (double)W / (double)H;
        cols = (int)ceil(sqrt((double)nLPs * aspect));
        if (cols < 1) cols = 1;
        rows = ((int)nLPs + cols - 1) / cols;
        while (rows > 1 && (rows - 1) * cols >= (int)nLPs)
            rows--;

        gutter = 2;
        tileW = (W - (cols + 1) * gutter) / cols;
        tileH = (H - (rows + 1) * gutter) / rows;
        if (tileW < 1) tileW = 1;
        if (tileH < 1) tileH = 1;

        for (i = 0; i < nLPs; i++)
        {
            int r = (int)i / cols;
            int c = (int)i % cols;
            int x = cpuArea.left + gutter + c * (tileW + gutter);
            int y = cpuArea.top  + gutter + r * (tileH + gutter);
            SetWindowPos(PerfPageCpuHwnds[i], NULL, x, y, tileW, tileH,
                         SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        }
    }
    else
    {
        SetWindowPos(PerfPageCpuHwnds[0], NULL,
                     cpuArea.left, cpuArea.top,
                     cpuArea.right - cpuArea.left,
                     cpuArea.bottom - cpuArea.top,
                     SWP_NOZORDER | SWP_NOACTIVATE | SWP_SHOWWINDOW);
        for (i = 1; i < nLPs; i++)
            ShowWindow(PerfPageCpuHwnds[i], SW_HIDE);
    }
}

static inline
void AdjustCntrlPos(int ctrl_id, HWND hDlg, int nXDifference, int nYDifference)
{
    AdjustFrameSize(GetDlgItem(hDlg, ctrl_id), hDlg, nXDifference, nYDifference, 0);
}

INT_PTR CALLBACK
PerformancePageWndProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    RECT rc;

    switch (message)
    {
        case WM_DESTROY:
            if (PerfPageCpuGraphs)
            {
                ULONG i;
                for (i = 0; i < nLPs; i++)
                    GraphCtrl_Dispose(&PerfPageCpuGraphs[i]);
                HeapFree(GetProcessHeap(), 0, PerfPageCpuGraphs);
                PerfPageCpuGraphs = NULL;
            }
            if (PerfPageCpuHwnds)
            {
                /* Index 0 is owned by the dialog template; only destroy [1..] */
                ULONG i;
                for (i = 1; i < nLPs; i++)
                {
                    if (PerfPageCpuHwnds[i])
                        DestroyWindow(PerfPageCpuHwnds[i]);
                }
                HeapFree(GetProcessHeap(), 0, PerfPageCpuHwnds);
                PerfPageCpuHwnds = NULL;
            }
            GraphCtrl_Dispose(&PerformancePageMemUsageHistoryGraph);
#ifdef RUN_PERF_PAGE
            EndLocalThread(&hPerformanceThread, dwPerformanceThread);
#endif
            break;

        case WM_INITDIALOG:
        {
            BOOL bGraph;
            TM_FORMAT fmt;

            /* Save the width and height */
            GetClientRect(hDlg, &rc);
            nPerformancePageWidth = rc.right;
            nPerformancePageHeight = rc.bottom;

            /* Update window position */
            SetWindowPos(hDlg, NULL, 15, 30, 0, 0, SWP_NOACTIVATE|SWP_NOOWNERZORDER|SWP_NOSIZE|SWP_NOZORDER);

            /*
             * Get handles to all the controls
             */
            hTotalsFrame = GetDlgItem(hDlg, IDC_TOTALS_FRAME);
            hCommitChargeFrame = GetDlgItem(hDlg, IDC_COMMIT_CHARGE_FRAME);
            hKernelMemoryFrame = GetDlgItem(hDlg, IDC_KERNEL_MEMORY_FRAME);
            hPhysicalMemoryFrame = GetDlgItem(hDlg, IDC_PHYSICAL_MEMORY_FRAME);

            hCpuUsageFrame = GetDlgItem(hDlg, IDC_CPU_USAGE_FRAME);
            hMemUsageFrame = GetDlgItem(hDlg, IDC_MEM_USAGE_FRAME);
            hCpuUsageHistoryFrame = GetDlgItem(hDlg, IDC_CPU_USAGE_HISTORY_FRAME);
            hMemUsageHistoryFrame = GetDlgItem(hDlg, IDC_MEMORY_USAGE_HISTORY_FRAME);

            hCommitChargeTotalEdit = GetDlgItem(hDlg, IDC_COMMIT_CHARGE_TOTAL);
            hCommitChargeLimitEdit = GetDlgItem(hDlg, IDC_COMMIT_CHARGE_LIMIT);
            hCommitChargePeakEdit = GetDlgItem(hDlg, IDC_COMMIT_CHARGE_PEAK);
            hKernelMemoryTotalEdit = GetDlgItem(hDlg, IDC_KERNEL_MEMORY_TOTAL);
            hKernelMemoryPagedEdit = GetDlgItem(hDlg, IDC_KERNEL_MEMORY_PAGED);
            hKernelMemoryNonPagedEdit = GetDlgItem(hDlg, IDC_KERNEL_MEMORY_NONPAGED);
            hPhysicalMemoryTotalEdit = GetDlgItem(hDlg, IDC_PHYSICAL_MEMORY_TOTAL);
            hPhysicalMemoryAvailableEdit = GetDlgItem(hDlg, IDC_PHYSICAL_MEMORY_AVAILABLE);
            hPhysicalMemorySystemCacheEdit = GetDlgItem(hDlg, IDC_PHYSICAL_MEMORY_SYSTEM_CACHE);
            hTotalsHandleCountEdit = GetDlgItem(hDlg, IDC_TOTALS_HANDLE_COUNT);
            hTotalsProcessCountEdit = GetDlgItem(hDlg, IDC_TOTALS_PROCESS_COUNT);
            hTotalsThreadCountEdit = GetDlgItem(hDlg, IDC_TOTALS_THREAD_COUNT);

            hCpuUsageGraph = GetDlgItem(hDlg, IDC_CPU_USAGE_GRAPH);
            hMemUsageGraph = GetDlgItem(hDlg, IDC_MEM_USAGE_GRAPH);
            hPerformancePageMemUsageHistoryGraph = GetDlgItem(hDlg, IDC_MEM_USAGE_HISTORY_GRAPH);

            nLPs = PerfDataGetProcessorCount();
            if (nLPs == 0)
                nLPs = 1;

            PerfPageCpuGraphs = (PTM_GRAPH_CONTROL)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                             sizeof(TM_GRAPH_CONTROL) * nLPs);
            PerfPageCpuHwnds  = (HWND *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                                  sizeof(HWND) * nLPs);
            if (!PerfPageCpuGraphs || !PerfPageCpuHwnds)
            {
                EndDialog(hDlg, 0);
                return FALSE;
            }

            PerfPageCpuHwnds[0] = GetDlgItem(hDlg, IDC_CPU_USAGE_HISTORY_GRAPH);
            {
                ULONG i;
                for (i = 1; i < nLPs; i++)
                {
                    PerfPageCpuHwnds[i] = CreateWindowExW(
                        0, L"STATIC", L"",
                        WS_CHILD | SS_BLACKRECT,
                        0, 0, 1, 1,
                        hDlg, NULL, hInst, NULL);
                    if (!PerfPageCpuHwnds[i])
                    {
                        EndDialog(hDlg, 0);
                        return FALSE;
                    }
                }
            }

            /* Create the controls */
            fmt.clrBack = RGB(0, 0, 0);
            fmt.clrGrid = RGB(0, 128, 64);
            fmt.clrPlot0 = RGB(0, 255, 0);
            fmt.clrPlot1 = RGB(255, 0, 0);
            fmt.GridCellWidth = fmt.GridCellHeight = 12;
            fmt.DrawSecondaryPlot = TaskManagerSettings.ShowKernelTimes;

            {
                ULONG i;
                for (i = 0; i < nLPs; i++)
                {
                    bGraph = GraphCtrl_Create(&PerfPageCpuGraphs[i], PerfPageCpuHwnds[i], hDlg, &fmt);
                    if (!bGraph)
                    {
                        EndDialog(hDlg, 0);
                        return FALSE;
                    }
                }
            }

            fmt.clrPlot0 = RGB(255, 255, 0);
            fmt.clrPlot1 = RGB(100, 255, 255);
            fmt.DrawSecondaryPlot = TRUE;
            bGraph = GraphCtrl_Create(&PerformancePageMemUsageHistoryGraph, hPerformancePageMemUsageHistoryGraph, hDlg, &fmt);
            if (!bGraph)
            {
                EndDialog(hDlg, 0);
                return FALSE;
            }

            /* Start our refresh thread */
#ifdef RUN_PERF_PAGE
            hPerformanceThread = CreateThread(NULL, 0, PerformancePageRefreshThread, NULL, 0, &dwPerformanceThread);
#endif

            /*
             * Subclass graph buttons
             */
            OldGraphWndProc = (WNDPROC)SetWindowLongPtrW(hCpuUsageGraph, GWLP_WNDPROC, (LONG_PTR)Graph_WndProc);
            SetWindowLongPtrW(hMemUsageGraph, GWLP_WNDPROC, (LONG_PTR)Graph_WndProc);
            OldGraphCtrlWndProc = (WNDPROC)SetWindowLongPtrW(hPerformancePageMemUsageHistoryGraph, GWLP_WNDPROC, (LONG_PTR)GraphCtrl_WndProc);
            {
                ULONG i;
                for (i = 0; i < nLPs; i++)
                {
                    SetWindowLongPtrW(PerfPageCpuHwnds[i], GWLP_WNDPROC, (LONG_PTR)GraphCtrl_WndProc);
                }
            }

            /* Reflect the persisted "One Graph Per CPU" setting in the View menu radio. */
            {
                HMENU hMenu = GetMenu(hMainWnd);
                HMENU hViewMenu = hMenu ? GetSubMenu(hMenu, 2) : NULL;
                HMENU hCPUHistoryMenu = hViewMenu ? GetSubMenu(hViewMenu, 3) : NULL;
                if (hCPUHistoryMenu)
                {
                    CheckMenuRadioItem(hCPUHistoryMenu,
                                       ID_VIEW_CPUHISTORY_ONEGRAPHALL,
                                       ID_VIEW_CPUHISTORY_ONEGRAPHPERCPU,
                                       TaskManagerSettings.CPUHistory_OneGraphPerCPU ?
                                           ID_VIEW_CPUHISTORY_ONEGRAPHPERCPU :
                                           ID_VIEW_CPUHISTORY_ONEGRAPHALL,
                                       MF_BYCOMMAND);
                }
            }

            PerformancePage_LayoutCpuGraphs(hDlg);
            return TRUE;
        }

        case WM_COMMAND:
            break;

        case WM_SIZE:
        {
            int  cx, cy;
            int  nXDifference;
            int  nYDifference;

            if (wParam == SIZE_MINIMIZED)
                return 0;

            cx = LOWORD(lParam);
            cy = HIWORD(lParam);
            nXDifference = cx - nPerformancePageWidth;
            nYDifference = cy - nPerformancePageHeight;
            nPerformancePageWidth = cx;
            nPerformancePageHeight = cy;

            /* Reposition the performance page's controls */
            AdjustFrameSize(hTotalsFrame, hDlg, 0, nYDifference, 0);
            AdjustFrameSize(hCommitChargeFrame, hDlg, 0, nYDifference, 0);
            AdjustFrameSize(hKernelMemoryFrame, hDlg, 0, nYDifference, 0);
            AdjustFrameSize(hPhysicalMemoryFrame, hDlg, 0, nYDifference, 0);
            AdjustCntrlPos(IDS_COMMIT_CHARGE_TOTAL, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_COMMIT_CHARGE_LIMIT, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_COMMIT_CHARGE_PEAK, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_KERNEL_MEMORY_TOTAL, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_KERNEL_MEMORY_PAGED, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_KERNEL_MEMORY_NONPAGED, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_PHYSICAL_MEMORY_TOTAL, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_PHYSICAL_MEMORY_AVAILABLE, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_PHYSICAL_MEMORY_SYSTEM_CACHE, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_TOTALS_HANDLE_COUNT, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_TOTALS_PROCESS_COUNT, hDlg, 0, nYDifference);
            AdjustCntrlPos(IDS_TOTALS_THREAD_COUNT, hDlg, 0, nYDifference);

            AdjustControlPosition(hCommitChargeTotalEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hCommitChargeLimitEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hCommitChargePeakEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hKernelMemoryTotalEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hKernelMemoryPagedEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hKernelMemoryNonPagedEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hPhysicalMemoryTotalEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hPhysicalMemoryAvailableEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hPhysicalMemorySystemCacheEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hTotalsHandleCountEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hTotalsProcessCountEdit, hDlg, 0, nYDifference);
            AdjustControlPosition(hTotalsThreadCountEdit, hDlg, 0, nYDifference);

            nXDifference += lastX;
            nYDifference += lastY;
            lastX = lastY = 0;
            if (nXDifference % 2)
            {
                if (nXDifference > 0)
                {
                    nXDifference--;
                    lastX++;
                }
                else
                {
                    nXDifference++;
                    lastX--;
                }
            }
            if (nYDifference % 2)
            {
                if (nYDifference > 0)
                {
                    nYDifference--;
                    lastY++;
                }
                else
                {
                    nYDifference++;
                    lastY--;
                }
            }
            AdjustFrameSize(hCpuUsageFrame, hDlg, nXDifference, nYDifference, 1);
            AdjustFrameSize(hMemUsageFrame, hDlg, nXDifference, nYDifference, 2);
            AdjustFrameSize(hCpuUsageHistoryFrame, hDlg, nXDifference, nYDifference, 3);
            AdjustFrameSize(hMemUsageHistoryFrame, hDlg, nXDifference, nYDifference, 4);
            AdjustFrameSize(hCpuUsageGraph, hDlg, nXDifference, nYDifference, 1);
            AdjustFrameSize(hMemUsageGraph, hDlg, nXDifference, nYDifference, 2);
            AdjustFrameSize(hPerformancePageMemUsageHistoryGraph, hDlg, nXDifference, nYDifference, 4);

            PerformancePage_LayoutCpuGraphs(hDlg);
            break;
        }
    }
    return 0;
}

void RefreshPerformancePage(void)
{
#ifdef RUN_PERF_PAGE
    /* Signal the event so that our refresh thread
     * will wake up and refresh the performance page */
    PostThreadMessage(dwPerformanceThread, WM_TIMER, 0, 0);
#endif
}

DWORD WINAPI PerformancePageRefreshThread(PVOID Parameter)
{
    ULONGLONG CommitChargeTotal;
    ULONGLONG CommitChargeLimit;
    ULONGLONG CommitChargePeak;

    ULONG CpuUsage;
    ULONG CpuKernelUsage;

    ULONGLONG KernelMemoryTotal;
    ULONGLONG KernelMemoryPaged;
    ULONGLONG KernelMemoryNonPaged;

    ULONGLONG PhysicalMemoryTotal;
    ULONGLONG PhysicalMemoryAvailable;
    ULONGLONG PhysicalMemorySystemCache;

    ULONG TotalHandles;
    ULONG TotalThreads;
    ULONG TotalProcesses;

    MSG msg;

    WCHAR Text[260];
    WCHAR szMemUsage[256], szCpuUsage[256], szProcesses[256];

    LoadStringW(hInst, IDS_STATUS_CPUUSAGE, szCpuUsage, _countof(szCpuUsage));
    LoadStringW(hInst, IDS_STATUS_MEMUSAGE, szMemUsage, _countof(szMemUsage));
    LoadStringW(hInst, IDS_STATUS_PROCESSES, szProcesses, _countof(szProcesses));

    while (1)
    {
        extern BOOL bTrackMenu; // From taskmgr.c

        int nBarsUsed1;
        int nBarsUsed2;

        WCHAR szChargeTotalFormat[256];
        WCHAR szChargeLimitFormat[256];

        /* Wait for an the event or application close */
        if (GetMessage(&msg, NULL, 0, 0) <= 0)
            return 0;

        if (msg.message == WM_TIMER)
        {
            /*
             * Update the commit charge info
             */
            CommitChargeTotal = PerfDataGetCommitChargeTotalK();
            CommitChargeLimit = PerfDataGetCommitChargeLimitK();
            CommitChargePeak  = PerfDataGetCommitChargePeakK();
            _ultow(CommitChargeTotal, Text, 10);
            SetWindowTextW(hCommitChargeTotalEdit, Text);
            _ultow(CommitChargeLimit, Text, 10);
            SetWindowTextW(hCommitChargeLimitEdit, Text);
            _ultow(CommitChargePeak, Text, 10);
            SetWindowTextW(hCommitChargePeakEdit, Text);

            StrFormatByteSizeW(CommitChargeTotal * 1024,
                               szChargeTotalFormat,
                               _countof(szChargeTotalFormat));

            StrFormatByteSizeW(CommitChargeLimit * 1024,
                               szChargeLimitFormat,
                               _countof(szChargeLimitFormat));

            if (!bTrackMenu)
            {
                wsprintfW(Text, szMemUsage, szChargeTotalFormat, szChargeLimitFormat,
                    (CommitChargeLimit ? ((CommitChargeTotal * 100) / CommitChargeLimit) : 0));
                SendMessageW(hStatusWnd, SB_SETTEXT, 2, (LPARAM)Text);
            }

            /*
             * Update the kernel memory info
             */
            KernelMemoryTotal = PerfDataGetKernelMemoryTotalK();
            KernelMemoryPaged = PerfDataGetKernelMemoryPagedK();
            KernelMemoryNonPaged = PerfDataGetKernelMemoryNonPagedK();
            _ultow(KernelMemoryTotal, Text, 10);
            SetWindowTextW(hKernelMemoryTotalEdit, Text);
            _ultow(KernelMemoryPaged, Text, 10);
            SetWindowTextW(hKernelMemoryPagedEdit, Text);
            _ultow(KernelMemoryNonPaged, Text, 10);
            SetWindowTextW(hKernelMemoryNonPagedEdit, Text);

            /*
             * Update the physical memory info
             */
            PhysicalMemoryTotal = PerfDataGetPhysicalMemoryTotalK();
            PhysicalMemoryAvailable = PerfDataGetPhysicalMemoryAvailableK();
            PhysicalMemorySystemCache = PerfDataGetPhysicalMemorySystemCacheK();
            _ultow(PhysicalMemoryTotal, Text, 10);
            SetWindowTextW(hPhysicalMemoryTotalEdit, Text);
            _ultow(PhysicalMemoryAvailable, Text, 10);
            SetWindowTextW(hPhysicalMemoryAvailableEdit, Text);
            _ultow(PhysicalMemorySystemCache, Text, 10);
            SetWindowTextW(hPhysicalMemorySystemCacheEdit, Text);

            /*
             * Update the totals info
             */
            TotalHandles = PerfDataGetSystemHandleCount();
            TotalThreads = PerfDataGetTotalThreadCount();
            TotalProcesses = PerfDataGetProcessCount();
            _ultow(TotalHandles, Text, 10);
            SetWindowTextW(hTotalsHandleCountEdit, Text);
            _ultow(TotalThreads, Text, 10);
            SetWindowTextW(hTotalsThreadCountEdit, Text);
            _ultow(TotalProcesses, Text, 10);
            SetWindowTextW(hTotalsProcessCountEdit, Text);
            if (!bTrackMenu)
            {
                wsprintfW(Text, szProcesses, TotalProcesses);
                SendMessageW(hStatusWnd, SB_SETTEXT, 0, (LPARAM)Text);
            }

            /*
             * Redraw the graphs
             */
            InvalidateRect(hCpuUsageGraph, NULL, FALSE);
            InvalidateRect(hMemUsageGraph, NULL, FALSE);

            /*
             * Get the CPU usage
             */
            CpuUsage = PerfDataGetProcessorUsage();
            CpuKernelUsage = PerfDataGetProcessorSystemUsage();

            if (!bTrackMenu)
            {
                wsprintfW(Text, szCpuUsage, CpuUsage);
                SendMessageW(hStatusWnd, SB_SETTEXT, 1, (LPARAM)Text);
            }

            /*
             * Get the memory usage
             */
            CommitChargeTotal = PerfDataGetCommitChargeTotalK();
            CommitChargeLimit = PerfDataGetCommitChargeLimitK();
            nBarsUsed1 = CommitChargeLimit ? ((CommitChargeTotal * 100) / CommitChargeLimit) : 0;

            PhysicalMemoryTotal = PerfDataGetPhysicalMemoryTotalK();
            PhysicalMemoryAvailable = PerfDataGetPhysicalMemoryAvailableK();
            nBarsUsed2 = PhysicalMemoryTotal ? ((PhysicalMemoryAvailable * 100) / PhysicalMemoryTotal) : 0;

            if (TaskManagerSettings.CPUHistory_OneGraphPerCPU)
            {
                ULONG i;
                for (i = 0; i < nLPs; i++)
                {
                    BYTE u = (BYTE)PerfDataGetProcessorUsageForLP(i);
                    BYTE k = (BYTE)PerfDataGetProcessorSystemUsageForLP(i);
                    GraphCtrl_AddPoint(&PerfPageCpuGraphs[i], u, k);
                    InvalidateRect(PerfPageCpuHwnds[i], NULL, FALSE);
                }
            }
            else
            {
                GraphCtrl_AddPoint(&PerfPageCpuGraphs[0], CpuUsage, CpuKernelUsage);
                InvalidateRect(PerfPageCpuHwnds[0], NULL, FALSE);
            }
            GraphCtrl_AddPoint(&PerformancePageMemUsageHistoryGraph, nBarsUsed1, nBarsUsed2);
            InvalidateRect(hPerformancePageMemUsageHistoryGraph, NULL, FALSE);
        }
    }
    return 0;
}

void PerformancePage_OnViewShowKernelTimes(void)
{
    HMENU hMenu;
    HMENU hViewMenu;
    BOOL  bShow;
    ULONG i;

    hMenu = GetMenu(hMainWnd);
    hViewMenu = GetSubMenu(hMenu, 2);

    if (GetMenuState(hViewMenu, ID_VIEW_SHOWKERNELTIMES, MF_BYCOMMAND) & MF_CHECKED)
    {
        CheckMenuItem(hViewMenu, ID_VIEW_SHOWKERNELTIMES, MF_BYCOMMAND|MF_UNCHECKED);
        TaskManagerSettings.ShowKernelTimes = FALSE;
        bShow = FALSE;
    }
    else
    {
        CheckMenuItem(hViewMenu, ID_VIEW_SHOWKERNELTIMES, MF_BYCOMMAND|MF_CHECKED);
        TaskManagerSettings.ShowKernelTimes = TRUE;
        bShow = TRUE;
    }

    if (PerfPageCpuGraphs)
    {
        for (i = 0; i < nLPs; i++)
        {
            PerfPageCpuGraphs[i].DrawSecondaryPlot = bShow;
            GraphCtrl_RedrawBitmap(&PerfPageCpuGraphs[i], PerfPageCpuGraphs[i].BitmapHeight);
        }
    }
    RefreshPerformancePage();
}

static void PerformancePage_RelayoutCpuGraphs(void)
{
    if (hPerformancePage)
    {
        PerformancePage_LayoutCpuGraphs(hPerformancePage);
        InvalidateRect(hPerformancePage, NULL, TRUE);
    }
}

void PerformancePage_OnViewCPUHistoryOneGraphAll(void)
{
    HMENU hMenu;
    HMENU hViewMenu;
    HMENU hCPUHistoryMenu;

    hMenu = GetMenu(hMainWnd);
    hViewMenu = GetSubMenu(hMenu, 2);
    hCPUHistoryMenu = GetSubMenu(hViewMenu, 3);

    TaskManagerSettings.CPUHistory_OneGraphPerCPU = FALSE;
    CheckMenuRadioItem(hCPUHistoryMenu, ID_VIEW_CPUHISTORY_ONEGRAPHALL, ID_VIEW_CPUHISTORY_ONEGRAPHPERCPU, ID_VIEW_CPUHISTORY_ONEGRAPHALL, MF_BYCOMMAND);
    PerformancePage_RelayoutCpuGraphs();
}

void PerformancePage_OnViewCPUHistoryOneGraphPerCPU(void)
{
    HMENU hMenu;
    HMENU hViewMenu;
    HMENU hCPUHistoryMenu;

    hMenu = GetMenu(hMainWnd);
    hViewMenu = GetSubMenu(hMenu, 2);
    hCPUHistoryMenu = GetSubMenu(hViewMenu, 3);

    TaskManagerSettings.CPUHistory_OneGraphPerCPU = TRUE;
    CheckMenuRadioItem(hCPUHistoryMenu, ID_VIEW_CPUHISTORY_ONEGRAPHALL, ID_VIEW_CPUHISTORY_ONEGRAPHPERCPU, ID_VIEW_CPUHISTORY_ONEGRAPHPERCPU, MF_BYCOMMAND);
    PerformancePage_RelayoutCpuGraphs();
}
