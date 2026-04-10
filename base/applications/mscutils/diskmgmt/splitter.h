/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Splitter drag state and helpers.
 */

#ifndef DISKMGMT_SPLITTER_H
#define DISKMGMT_SPLITTER_H

#include <windef.h>

#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _DM_SPLITTER_STATE
{
    BOOL Active;
    DM_SPLITTER_HIT Hit;
    POINT StartPoint;
    RECT ClientRect;
    DM_LAYOUT_METRICS InitialMetrics;
    DM_LAYOUT_PANES InitialPanes;
} DM_SPLITTER_STATE, *PDM_SPLITTER_STATE;

VOID
DmSplitterInitializeState(
    _Out_ PDM_SPLITTER_STATE State);

BOOL
DmSplitterBeginDrag(
    _Out_ PDM_SPLITTER_STATE State,
    _In_ const DM_LAYOUT_METRICS *Metrics,
    _In_ const DM_LAYOUT_PANES *Panes,
    _In_ DM_SPLITTER_HIT Hit,
    _In_ const POINT *StartPoint,
    _In_ const RECT *ClientRect);

BOOL
DmSplitterUpdateDrag(
    _In_ const DM_SPLITTER_STATE *State,
    _In_ const POINT *CurrentPoint,
    _Out_ PDM_LAYOUT_METRICS Metrics,
    _Out_opt_ PDM_LAYOUT_PANES Panes);

VOID
DmSplitterEndDrag(
    _Inout_ PDM_SPLITTER_STATE State);

VOID
DmSplitterCancelDrag(
    _Inout_ PDM_SPLITTER_STATE State,
    _Out_opt_ PDM_LAYOUT_METRICS Metrics);

BOOL
DmSplitterIsDragging(
    _In_ const DM_SPLITTER_STATE *State);

HCURSOR
DmSplitterGetCursor(
    _In_ DM_SPLITTER_HIT Hit);

BOOL
DmSplitterSetCursor(
    _In_ DM_SPLITTER_HIT Hit);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_SPLITTER_H */
