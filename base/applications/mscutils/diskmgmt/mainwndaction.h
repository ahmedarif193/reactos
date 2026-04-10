/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main-window action and menu-state helpers.
 */

#ifndef DISKMGMT_MAINWNDACTION_H
#define DISKMGMT_MAINWNDACTION_H

#include "actions.h"

typedef struct _MAIN_WND_INFO MAIN_WND_INFO, *PMAIN_WND_INFO;

VOID
DmMainWndCacheSelectedVolumeName(
    _In_ PMAIN_WND_INFO Info);

VOID
DmMainWndRestoreSelectedVolume(
    _In_ PMAIN_WND_INFO Info);

VOID
DmMainWndBuildActionContext(
    _In_ PMAIN_WND_INFO Info,
    _Out_ PDM_ACTION_CONTEXT Context);

VOID
DmMainWndUpdateMenuState(
    _In_ PMAIN_WND_INFO Info);

VOID
DmMainWndPopulateVolumeList(
    _In_ PMAIN_WND_INFO Info);

BOOL
DmMainWndExecuteCommand(
    _In_ PMAIN_WND_INFO Info,
    _In_ UINT CommandId);

#endif /* DISKMGMT_MAINWNDACTION_H */
