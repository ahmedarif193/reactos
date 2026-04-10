/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Splitter drag state and helpers.
 */

#include "splitter.h"

#include <winbase.h>
#include <winuser.h>

static INT
DmSplitterClamp(
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

static INT
DmSplitterGetClientWidth(
    _In_ const RECT *ClientRect)
{
    return max(ClientRect->right - ClientRect->left, 0);
}

static INT
DmSplitterGetClientHeight(
    _In_ const RECT *ClientRect)
{
    return max(ClientRect->bottom - ClientRect->top, 0);
}

VOID
DmSplitterInitializeState(
    _Out_ PDM_SPLITTER_STATE State)
{
    ZeroMemory(State, sizeof(*State));
}

BOOL
DmSplitterBeginDrag(
    _Out_ PDM_SPLITTER_STATE State,
    _In_ const DM_LAYOUT_METRICS *Metrics,
    _In_ const DM_LAYOUT_PANES *Panes,
    _In_ DM_SPLITTER_HIT Hit,
    _In_ const POINT *StartPoint,
    _In_ const RECT *ClientRect)
{
    if (State == NULL ||
        Metrics == NULL ||
        Panes == NULL ||
        StartPoint == NULL ||
        ClientRect == NULL ||
        Hit == DmSplitterHitNone)
    {
        return FALSE;
    }

    ZeroMemory(State, sizeof(*State));
    State->Active = TRUE;
    State->Hit = Hit;
    State->StartPoint = *StartPoint;
    State->ClientRect = *ClientRect;
    State->InitialMetrics = *Metrics;
    State->InitialPanes = *Panes;
    return TRUE;
}

static VOID
DmSplitterApplyVerticalDrag(
    _In_ const DM_SPLITTER_STATE *State,
    _In_ const POINT *CurrentPoint,
    _Inout_ PDM_LAYOUT_METRICS Metrics)
{
    INT ClientWidth;
    INT NewWidth;
    INT MaximumWidth;

    ClientWidth = DmSplitterGetClientWidth(&State->ClientRect);
    NewWidth = State->InitialPanes.LeftPaneWidth + (CurrentPoint->x - State->StartPoint.x);
    MaximumWidth = max(ClientWidth - State->InitialMetrics.MinLeftPaneWidth -
                       State->InitialMetrics.SplitterWidth,
                       State->InitialMetrics.MinLeftPaneWidth);
    Metrics->LeftPaneWidth = DmSplitterClamp(NewWidth,
                                             State->InitialMetrics.MinLeftPaneWidth,
                                             MaximumWidth);
}

static VOID
DmSplitterApplyHorizontalDrag(
    _In_ const DM_SPLITTER_STATE *State,
    _In_ const POINT *CurrentPoint,
    _Inout_ PDM_LAYOUT_METRICS Metrics)
{
    INT ClientHeight;
    INT StatusHeight;
    INT UsableHeight;
    INT NewHeight;
    INT MaximumHeight;
    INT NewPercent;

    ClientHeight = DmSplitterGetClientHeight(&State->ClientRect);
    StatusHeight = State->InitialMetrics.StatusBarVisible ?
                   max(State->InitialMetrics.StatusBarHeight, 0) : 0;
    UsableHeight = max(ClientHeight - StatusHeight, 0);

    NewHeight = State->InitialPanes.TopPaneHeight +
                (CurrentPoint->y - State->StartPoint.y);
    MaximumHeight = max(UsableHeight - State->InitialMetrics.MinBottomPaneHeight -
                        State->InitialMetrics.SplitterWidth,
                        State->InitialMetrics.MinTopPaneHeight);
    NewHeight = DmSplitterClamp(NewHeight,
                                State->InitialMetrics.MinTopPaneHeight,
                                MaximumHeight);

    if (UsableHeight > 0)
    {
        NewPercent = (NewHeight * 100 + (UsableHeight / 2)) / UsableHeight;
        Metrics->TopPanePercent = DmSplitterClamp(NewPercent, 1, 99);
    }
}

BOOL
DmSplitterUpdateDrag(
    _In_ const DM_SPLITTER_STATE *State,
    _In_ const POINT *CurrentPoint,
    _Out_ PDM_LAYOUT_METRICS Metrics,
    _Out_opt_ PDM_LAYOUT_PANES Panes)
{
    if (State == NULL ||
        !State->Active ||
        CurrentPoint == NULL ||
        Metrics == NULL)
    {
        return FALSE;
    }

    *Metrics = State->InitialMetrics;

    switch (State->Hit)
    {
        case DmSplitterHitVertical:
            DmSplitterApplyVerticalDrag(State, CurrentPoint, Metrics);
            break;

        case DmSplitterHitHorizontal:
            DmSplitterApplyHorizontalDrag(State, CurrentPoint, Metrics);
            break;

        default:
            return FALSE;
    }

    if (Panes != NULL)
    {
        DmLayoutComputePanes(&State->ClientRect, Metrics, Panes);
    }

    return TRUE;
}

VOID
DmSplitterEndDrag(
    _Inout_ PDM_SPLITTER_STATE State)
{
    if (State == NULL)
        return;

    ZeroMemory(State, sizeof(*State));
}

VOID
DmSplitterCancelDrag(
    _Inout_ PDM_SPLITTER_STATE State,
    _Out_opt_ PDM_LAYOUT_METRICS Metrics)
{
    if (State == NULL)
        return;

    if (Metrics != NULL)
    {
        *Metrics = State->InitialMetrics;
    }

    ZeroMemory(State, sizeof(*State));
}

BOOL
DmSplitterIsDragging(
    _In_ const DM_SPLITTER_STATE *State)
{
    return (State != NULL && State->Active);
}

HCURSOR
DmSplitterGetCursor(
    _In_ DM_SPLITTER_HIT Hit)
{
    switch (Hit)
    {
        case DmSplitterHitVertical:
            return LoadCursorW(NULL, MAKEINTRESOURCEW(32644));

        case DmSplitterHitHorizontal:
            return LoadCursorW(NULL, MAKEINTRESOURCEW(32645));

        default:
            return NULL;
    }
}

BOOL
DmSplitterSetCursor(
    _In_ DM_SPLITTER_HIT Hit)
{
    HCURSOR Cursor;

    Cursor = DmSplitterGetCursor(Hit);
    if (Cursor == NULL)
        return FALSE;

    return (SetCursor(Cursor) != NULL);
}
