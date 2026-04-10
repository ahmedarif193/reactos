/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main-window layout and splitter helpers.
 */

#ifndef DISKMGMT_MAINWNDLAYOUT_H
#define DISKMGMT_MAINWNDLAYOUT_H

typedef struct _MAIN_WND_INFO MAIN_WND_INFO, *PMAIN_WND_INFO;

VOID
DmMainWndGetCurrentLayoutPanes(
    _In_ PMAIN_WND_INFO Info,
    _Out_ PDM_LAYOUT_PANES Panes);

VOID
DmMainWndLayoutMainWindow(
    _In_ PMAIN_WND_INFO Info);

BOOL
DmMainWndBeginSplitterDrag(
    _In_ PMAIN_WND_INFO Info,
    _In_ const POINT *Point);

VOID
DmMainWndUpdateSplitterDrag(
    _In_ PMAIN_WND_INFO Info,
    _In_ const POINT *Point);

BOOL
DmMainWndHandleSplitterSetCursor(
    _In_ PMAIN_WND_INFO Info);

#endif /* DISKMGMT_MAINWNDLAYOUT_H */
