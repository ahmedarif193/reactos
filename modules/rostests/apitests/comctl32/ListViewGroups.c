/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the list view group view state
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <commctrl.h>

START_TEST(ListViewGroups)
{
    HWND hWnd;

    InitCommonControls();

    hWnd = CreateWindowExW(0, WC_LISTVIEWW, NULL, WS_POPUP | LVS_REPORT,
                           0, 0, 200, 100, NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd)
        return;

    ok(SendMessageW(hWnd, LVM_ISGROUPVIEWENABLED, 0, 0) == FALSE, "Group view is enabled by default\n");

    DestroyWindow(hWnd);
}
