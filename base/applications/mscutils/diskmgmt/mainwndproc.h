/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Main-window procedure and event helpers.
 */

#ifndef DISKMGMT_MAINWNDPROC_H
#define DISKMGMT_MAINWNDPROC_H

#ifdef __cplusplus
extern "C" {
#endif

LRESULT CALLBACK
DmMainWndProc(
    _In_ HWND hwnd,
    _In_ UINT uMsg,
    _In_ WPARAM wParam,
    _In_ LPARAM lParam);

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_MAINWNDPROC_H */
