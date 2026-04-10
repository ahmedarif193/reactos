/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Pane layout and splitter helpers.
 */

#ifndef DISKMGMT_LAYOUT_H
#define DISKMGMT_LAYOUT_H

#include <windef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DISKMGMT_LAYOUT_SPLITTER_WIDTH     3
#define DISKMGMT_LAYOUT_MIN_TOP_HEIGHT     110
#define DISKMGMT_LAYOUT_MIN_BOTTOM_HEIGHT   80

typedef enum _DM_VIEW_TARGET
{
    DmViewTargetNone = 0,
    DmViewTargetTop,
    DmViewTargetBottom,
    DmViewTargetDiskList
} DM_VIEW_TARGET;

typedef enum _DM_SPLITTER_HIT
{
    DmSplitterHitNone = 0,
    DmSplitterHitVertical,
    DmSplitterHitHorizontal
} DM_SPLITTER_HIT;

typedef struct _DM_LAYOUT_METRICS
{
    INT LeftPaneWidth;
    INT SplitterWidth;
    INT TopPanePercent;
    INT MinLeftPaneWidth;
    INT MinTopPaneHeight;
    INT MinBottomPaneHeight;
    INT ToolbarHeight;
    INT StatusBarHeight;
    BOOL ToolbarVisible;
    BOOL StatusBarVisible;
    BOOL NavigationVisible;
} DM_LAYOUT_METRICS, *PDM_LAYOUT_METRICS;

typedef struct _DM_LAYOUT_PANES
{
    RECT ClientRect;
    RECT ToolbarRect;
    RECT TreeRect;
    RECT TopRect;
    RECT BottomRect;
    RECT StatusRect;
    RECT VerticalSplitterRect;
    RECT HorizontalSplitterRect;
    INT ToolbarHeight;
    INT LeftPaneWidth;
    INT TopPaneHeight;
    INT StatusBarHeight;
} DM_LAYOUT_PANES, *PDM_LAYOUT_PANES;

VOID
DmLayoutSetDefaults(
    _Out_ PDM_LAYOUT_METRICS Metrics);

VOID
DmLayoutComputePanes(
    _In_ const RECT *ClientRect,
    _In_ const DM_LAYOUT_METRICS *Metrics,
    _Out_ PDM_LAYOUT_PANES Panes);

DM_SPLITTER_HIT
DmLayoutHitTestSplitter(
    _In_ const POINT *Pt,
    _In_ const DM_LAYOUT_PANES *Panes);

BOOL
DmLayoutHitTestPane(
    _In_ const POINT *Pt,
    _In_ const DM_LAYOUT_PANES *Panes,
    _Out_ DM_VIEW_TARGET *Target);

DM_VIEW_TARGET
DmLayoutViewTargetFromCommand(
    _In_ UINT CommandId);

UINT
DmLayoutViewCommandFromTarget(
    _In_ DM_VIEW_TARGET Target);

BOOL
DmLayoutIsTargetFocusable(
    _In_ DM_VIEW_TARGET Target);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_LAYOUT_H */
