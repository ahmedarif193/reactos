/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Disk-view child window helpers.
 */

#ifndef DISKMGMT_DISKVIEWWND_H
#define DISKMGMT_DISKVIEWWND_H

#include "snapshot.h"
#include "layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _MAIN_WND_INFO MAIN_WND_INFO, *PMAIN_WND_INFO;

#define DISKMGMT_DISKVIEW_CLASS_NAME L"DiskMgmtDiskViewClass"

PDM_VOLUME
DmMainWndGetSelectedVolume(
    _In_ PMAIN_WND_INFO Info);

VOID
DmMainWndSetFocusTarget(
    _In_ PMAIN_WND_INFO Info,
    _In_ DM_VIEW_TARGET Target);

VOID
DmMainWndUpdateVolumeSelectionFromDiskViewPoint(
    _In_ PMAIN_WND_INFO Info,
    _In_ INT x,
    _In_ INT y);

BOOL
DmMainWndRegisterDiskViewClass(
    _In_ HINSTANCE Instance);

VOID
DmMainWndUnregisterDiskViewClass(
    _In_ HINSTANCE Instance);

BOOL
DmMainWndCreateDiskView(
    _In_ PMAIN_WND_INFO Info);

VOID
DmMainWndUpdateDiskViewScrollInfo(
    _In_ PMAIN_WND_INFO Info);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_DISKVIEWWND_H */
