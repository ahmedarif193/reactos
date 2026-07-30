/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main-window layout and splitter helpers.
 */

#include "precomp.h"

VOID
DmMainWndGetCurrentLayoutPanes(
    _In_ PMAIN_WND_INFO Info,
    _Out_ PDM_LAYOUT_PANES Panes)
{
    RECT rcClient;
    RECT rcStatus;
    RECT rcToolbar;
    INT StatusHeight;
    INT ToolbarHeight;

    StatusHeight = 0;
    ToolbarHeight = 0;
    GetClientRect(Info->hMainWnd, &rcClient);

    /* Use the WS_VISIBLE style bit instead of IsWindowVisible().
       IsWindowVisible() walks the parent chain and returns FALSE during
       WM_CREATE because the top-level window hasn't been shown yet. */
    if (Info->hToolbar != NULL &&
        (GetWindowLongPtrW(Info->hToolbar, GWL_STYLE) & WS_VISIBLE))
    {
        SendMessageW(Info->hToolbar, TB_AUTOSIZE, 0, 0);
        GetWindowRect(Info->hToolbar, &rcToolbar);
        ToolbarHeight = rcToolbar.bottom - rcToolbar.top;
    }

    if (Info->hStatusBar != NULL && Info->bStatusBarVisible)
    {
        SendMessageW(Info->hStatusBar, WM_SIZE, 0, 0);
        GetWindowRect(Info->hStatusBar, &rcStatus);
        StatusHeight = rcStatus.bottom - rcStatus.top;
    }

    Info->LayoutMetrics.ToolbarVisible = (Info->hToolbar != NULL &&
                                         (GetWindowLongPtrW(Info->hToolbar, GWL_STYLE) & WS_VISIBLE));
    Info->LayoutMetrics.ToolbarHeight = ToolbarHeight;
    Info->LayoutMetrics.StatusBarVisible = Info->bStatusBarVisible;
    Info->LayoutMetrics.StatusBarHeight = StatusHeight;
    DmLayoutComputePanes(&rcClient,
                         &Info->LayoutMetrics,
                         Panes);
}

VOID
DmMainWndLayoutMainWindow(
    _In_ PMAIN_WND_INFO Info)
{
    DM_LAYOUT_PANES Panes;
    HDWP hdwp;

    DmMainWndGetCurrentLayoutPanes(Info, &Panes);

    hdwp = BeginDeferWindowPos(3);
    if (hdwp != NULL && Info->hToolbar != NULL)
    {
        hdwp = DeferWindowPos(hdwp,
                              Info->hToolbar,
                              NULL,
                              Panes.ToolbarRect.left,
                              Panes.ToolbarRect.top,
                              Panes.ToolbarRect.right - Panes.ToolbarRect.left,
                              Panes.ToolbarRect.bottom - Panes.ToolbarRect.top,
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (hdwp != NULL)
    {
        hdwp = DeferWindowPos(hdwp,
                              Info->hVolumeView,
                              NULL,
                              Panes.TopRect.left,
                              Panes.TopRect.top,
                              Panes.TopRect.right - Panes.TopRect.left,
                              Panes.TopRect.bottom - Panes.TopRect.top,
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (hdwp != NULL)
    {
        hdwp = DeferWindowPos(hdwp,
                              Info->hDiskView,
                              NULL,
                              Panes.BottomRect.left,
                              Panes.BottomRect.top,
                              Panes.BottomRect.right - Panes.BottomRect.left,
                              Panes.BottomRect.bottom - Panes.BottomRect.top,
                              SWP_NOZORDER | SWP_NOACTIVATE);
    }
    if (hdwp != NULL)
        EndDeferWindowPos(hdwp);

    DmMainWndUpdateDiskViewScrollInfo(Info);
}

BOOL
DmMainWndBeginSplitterDrag(
    _In_ PMAIN_WND_INFO Info,
    _In_ const POINT *Point)
{
    DM_LAYOUT_PANES Panes;
    DM_SPLITTER_HIT Hit;
    RECT ClientRect;

    DmMainWndGetCurrentLayoutPanes(Info, &Panes);
    Hit = DmLayoutHitTestSplitter(Point, &Panes);
    if (Hit == DmSplitterHitNone)
        return FALSE;

    GetClientRect(Info->hMainWnd, &ClientRect);
    if (!DmSplitterBeginDrag(&Info->SplitterState,
                             &Info->LayoutMetrics,
                             &Panes,
                             Hit,
                             Point,
                             &ClientRect))
    {
        return FALSE;
    }

    SetCapture(Info->hMainWnd);
    DmSplitterSetCursor(Hit);
    return TRUE;
}

VOID
DmMainWndUpdateSplitterDrag(
    _In_ PMAIN_WND_INFO Info,
    _In_ const POINT *Point)
{
    DM_LAYOUT_METRICS Metrics;

    if (!DmSplitterUpdateDrag(&Info->SplitterState,
                              Point,
                              &Metrics,
                              NULL))
    {
        return;
    }

    Info->LayoutMetrics = Metrics;
    DmMainWndLayoutMainWindow(Info);
    InvalidateRect(Info->hMainWnd, NULL, TRUE);
    DmSplitterSetCursor(Info->SplitterState.Hit);
}

BOOL
DmMainWndHandleSplitterSetCursor(
    _In_ PMAIN_WND_INFO Info)
{
    DM_LAYOUT_PANES Panes;
    DM_SPLITTER_HIT Hit;
    POINT Point;

    if (DmSplitterIsDragging(&Info->SplitterState))
        return DmSplitterSetCursor(Info->SplitterState.Hit);

    GetCursorPos(&Point);
    ScreenToClient(Info->hMainWnd, &Point);
    DmMainWndGetCurrentLayoutPanes(Info, &Panes);
    Hit = DmLayoutHitTestSplitter(&Point, &Panes);
    if (Hit == DmSplitterHitNone)
        return FALSE;

    return DmSplitterSetCursor(Hit);
}
