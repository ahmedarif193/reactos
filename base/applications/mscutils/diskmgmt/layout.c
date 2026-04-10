/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Pane layout and splitter helpers.
 */

#include "precomp.h"
#include "layout.h"

static INT
DmLayoutClamp(
    _In_ INT Value,
    _In_ INT Minimum,
    _In_ INT Maximum)
{
    if (Value < Minimum)
        return Minimum;
    if (Value > Maximum)
        return Maximum;
    return Value;
}

VOID
DmLayoutSetDefaults(
    _Out_ PDM_LAYOUT_METRICS Metrics)
{
    ZeroMemory(Metrics, sizeof(*Metrics));
    Metrics->LeftPaneWidth = DISKMGMT_LEFT_PANE_WIDTH;
    Metrics->SplitterWidth = DISKMGMT_LAYOUT_SPLITTER_WIDTH;
    Metrics->TopPanePercent = DISKMGMT_TOP_PANE_PERCENT;
    Metrics->MinLeftPaneWidth = DISKMGMT_MIN_LEFT_PANE;
    Metrics->MinTopPaneHeight = DISKMGMT_LAYOUT_MIN_TOP_HEIGHT;
    Metrics->MinBottomPaneHeight = DISKMGMT_LAYOUT_MIN_BOTTOM_HEIGHT;
    Metrics->ToolbarVisible = TRUE;
    Metrics->ToolbarHeight = 0;
    Metrics->StatusBarVisible = TRUE;
    Metrics->StatusBarHeight = 0;
    Metrics->NavigationVisible = FALSE;
}

VOID
DmLayoutComputePanes(
    _In_ const RECT *ClientRect,
    _In_ const DM_LAYOUT_METRICS *Metrics,
    _Out_ PDM_LAYOUT_PANES Panes)
{
    INT ClientWidth;
    INT ClientHeight;
    INT ToolbarHeight;
    INT StatusHeight;
    INT ContentTop;
    INT ContentHeight;
    INT LeftWidth;
    INT RightX;
    INT RightWidth;
    INT TopHeight;
    INT BottomY;
    INT BottomHeight;
    INT SplitterWidth;

    ZeroMemory(Panes, sizeof(*Panes));

    if (ClientRect == NULL || Metrics == NULL)
        return;

    Panes->ClientRect = *ClientRect;
    ClientWidth = max(ClientRect->right - ClientRect->left, 0);
    ClientHeight = max(ClientRect->bottom - ClientRect->top, 0);
    SplitterWidth = max(Metrics->SplitterWidth, 1);
    ToolbarHeight = Metrics->ToolbarVisible ? max(Metrics->ToolbarHeight, 0) : 0;
    StatusHeight = Metrics->StatusBarVisible ? max(Metrics->StatusBarHeight, 0) : 0;
    ContentTop = ToolbarHeight;
    ContentHeight = max(ClientHeight - ToolbarHeight - StatusHeight, 0);

    if (Metrics->NavigationVisible)
    {
        LeftWidth = DmLayoutClamp(Metrics->LeftPaneWidth,
                                  Metrics->MinLeftPaneWidth,
                                  max(ClientWidth - Metrics->MinLeftPaneWidth, Metrics->MinLeftPaneWidth));
        LeftWidth = min(LeftWidth, max(ClientWidth / 3, Metrics->MinLeftPaneWidth));
        RightX = LeftWidth + SplitterWidth;
        RightWidth = max(ClientWidth - RightX, 0);
    }
    else
    {
        LeftWidth = 0;
        RightX = 0;
        RightWidth = ClientWidth;
    }

    TopHeight = ContentHeight * max(Metrics->TopPanePercent, 1) / 100;
    TopHeight = DmLayoutClamp(TopHeight,
                              Metrics->MinTopPaneHeight,
                              max(ContentHeight - Metrics->MinBottomPaneHeight - SplitterWidth,
                                  Metrics->MinTopPaneHeight));
    BottomY = ContentTop + TopHeight + SplitterWidth;
    BottomHeight = max(ContentHeight - TopHeight - SplitterWidth, 0);

    if (ToolbarHeight > 0)
    {
        SetRect(&Panes->ToolbarRect,
                0,
                0,
                ClientWidth,
                ToolbarHeight);
        Panes->ToolbarHeight = ToolbarHeight;
    }

    if (Metrics->NavigationVisible)
    {
        SetRect(&Panes->TreeRect,
                0,
                ContentTop,
                LeftWidth,
                ClientHeight - StatusHeight);
    }

    SetRect(&Panes->TopRect,
            RightX,
            ContentTop,
            RightX + RightWidth,
            ContentTop + TopHeight);

    SetRect(&Panes->BottomRect,
            RightX,
            BottomY,
            RightX + RightWidth,
            BottomY + BottomHeight);

    if (Metrics->StatusBarVisible)
    {
        SetRect(&Panes->StatusRect,
                0,
                ClientHeight - StatusHeight,
                ClientWidth,
                ClientHeight);
        Panes->StatusBarHeight = StatusHeight;
    }

    if (Metrics->NavigationVisible)
    {
        SetRect(&Panes->VerticalSplitterRect,
                LeftWidth,
                ContentTop,
                LeftWidth + SplitterWidth,
                ClientHeight - StatusHeight);
    }

    SetRect(&Panes->HorizontalSplitterRect,
            RightX,
            ContentTop + TopHeight,
            RightX + RightWidth,
            ContentTop + TopHeight + SplitterWidth);

    Panes->LeftPaneWidth = LeftWidth;
    Panes->TopPaneHeight = TopHeight;
}

DM_SPLITTER_HIT
DmLayoutHitTestSplitter(
    _In_ const POINT *Pt,
    _In_ const DM_LAYOUT_PANES *Panes)
{
    if (Pt == NULL || Panes == NULL)
        return DmSplitterHitNone;

    if (Pt->x >= Panes->VerticalSplitterRect.left &&
        Pt->x < Panes->VerticalSplitterRect.right &&
        Pt->y >= Panes->VerticalSplitterRect.top &&
        Pt->y < Panes->VerticalSplitterRect.bottom)
    {
        return DmSplitterHitVertical;
    }

    if (Pt->x >= Panes->HorizontalSplitterRect.left &&
        Pt->x < Panes->HorizontalSplitterRect.right &&
        Pt->y >= Panes->HorizontalSplitterRect.top &&
        Pt->y < Panes->HorizontalSplitterRect.bottom)
    {
        return DmSplitterHitHorizontal;
    }

    return DmSplitterHitNone;
}

BOOL
DmLayoutHitTestPane(
    _In_ const POINT *Pt,
    _In_ const DM_LAYOUT_PANES *Panes,
    _Out_ DM_VIEW_TARGET *Target)
{
    if (Pt == NULL || Panes == NULL || Target == NULL)
        return FALSE;

    if (Pt->x >= Panes->TopRect.left &&
        Pt->x < Panes->TopRect.right &&
        Pt->y >= Panes->TopRect.top &&
        Pt->y < Panes->TopRect.bottom)
    {
        *Target = DmViewTargetTop;
        return TRUE;
    }

    if (Pt->x >= Panes->BottomRect.left &&
        Pt->x < Panes->BottomRect.right &&
        Pt->y >= Panes->BottomRect.top &&
        Pt->y < Panes->BottomRect.bottom)
    {
        *Target = DmViewTargetBottom;
        return TRUE;
    }

    if (Pt->x >= Panes->TreeRect.left &&
        Pt->x < Panes->TreeRect.right &&
        Pt->y >= Panes->TreeRect.top &&
        Pt->y < Panes->TreeRect.bottom)
    {
        *Target = DmViewTargetDiskList;
        return TRUE;
    }

    *Target = DmViewTargetNone;
    return FALSE;
}

DM_VIEW_TARGET
DmLayoutViewTargetFromCommand(
    _In_ UINT CommandId)
{
    switch (CommandId)
    {
        case IDM_VIEW_TOP:
            return DmViewTargetTop;

        case IDM_VIEW_BOTTOM:
            return DmViewTargetBottom;

        case IDM_VIEW_DISK_LIST:
            return DmViewTargetDiskList;

        default:
            return DmViewTargetNone;
    }
}

UINT
DmLayoutViewCommandFromTarget(
    _In_ DM_VIEW_TARGET Target)
{
    switch (Target)
    {
        case DmViewTargetTop:
            return IDM_VIEW_TOP;

        case DmViewTargetBottom:
            return IDM_VIEW_BOTTOM;

        case DmViewTargetDiskList:
            return IDM_VIEW_DISK_LIST;

        default:
            return 0;
    }
}

BOOL
DmLayoutIsTargetFocusable(
    _In_ DM_VIEW_TARGET Target)
{
    return (Target == DmViewTargetTop ||
            Target == DmViewTargetBottom ||
            Target == DmViewTargetDiskList);
}
