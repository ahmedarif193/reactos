/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main-window chrome helpers.
 */

#ifndef DISKMGMT_MAINWNDCHROME_H
#define DISKMGMT_MAINWNDCHROME_H

typedef struct _MAIN_WND_INFO MAIN_WND_INFO, *PMAIN_WND_INFO;

BOOL
DmMainWndBuildDynamicHintText(
    _In_ PMAIN_WND_INFO Info,
    _In_ UINT CommandId,
    _Out_writes_(cchBuffer) PWSTR Buffer,
    _In_ SIZE_T cchBuffer);

UINT
DmMainWndGetMainMenuHintStringId(
    _In_ UINT CommandId);

BOOL
DmMainWndShowCommandHint(
    _In_ PMAIN_WND_INFO Info,
    _In_ WORD CmdId,
    _In_ UINT DefHintId);

BOOL
DmMainWndShowSystemHint(
    _In_ PMAIN_WND_INFO Info,
    _In_ WORD CmdId,
    _In_ UINT DefHintId);

VOID
DmMainWndShowAboutDialog(
    _In_ HWND hWnd);

VOID
DmMainWndUpdateStatusBarMode(
    _In_ PMAIN_WND_INFO Info);

BOOL
DmMainWndCreateStatusBar(
    _In_ PMAIN_WND_INFO Info);

BOOL
DmMainWndCreateToolbar(
    _In_ PMAIN_WND_INFO Info);

BOOL
DmMainWndCreateVolumeView(
    _In_ PMAIN_WND_INFO Info);

LRESULT
DmMainWndOnPaint(
    _In_ PMAIN_WND_INFO Info,
    _In_ HWND hwnd);

#endif /* DISKMGMT_MAINWNDCHROME_H */
