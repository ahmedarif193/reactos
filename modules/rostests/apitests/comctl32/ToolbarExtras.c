/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests toolbar drop target registration and the drop-down gap message
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>
#include <ole2.h>
#include <commctrl.h>

#define TB_SETDROPDOWNGAP (WM_USER + 100)

static void
TestRegisterDrop(void)
{
    HWND hDrop, hPlain;
    HRESULT hr;

    hr = OleInitialize(NULL);
    ok(SUCCEEDED(hr), "OleInitialize returned 0x%lx\n", hr);
    if (FAILED(hr))
        return;

    hDrop = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL, WS_POPUP | TBSTYLE_REGISTERDROP,
                            0, 0, 200, 30, NULL, NULL, NULL, NULL);
    hPlain = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL, WS_POPUP,
                             0, 0, 200, 30, NULL, NULL, NULL, NULL);
    ok(hDrop && hPlain, "CreateWindowExW failed: %lu\n", GetLastError());

    if (hDrop)
        ok(GetPropW(hDrop, L"OleDropTargetInterface") != NULL, "TBSTYLE_REGISTERDROP toolbar is not a drop target\n");
    if (hPlain)
        ok(GetPropW(hPlain, L"OleDropTargetInterface") == NULL, "Plain toolbar is a drop target\n");

    if (hDrop)
    {
        DestroyWindow(hDrop);
        ok(GetPropW(hDrop, L"OleDropTargetInterface") == NULL, "Drop target survived the toolbar\n");
    }
    if (hPlain)
        DestroyWindow(hPlain);

    OleUninitialize();
}

static void
TestDropDownGap(void)
{
    TBBUTTON Button;
    RECT Before, After;
    HWND hWnd;
    LONG Delta;

    hWnd = CreateWindowExW(0, TOOLBARCLASSNAMEW, NULL, WS_POPUP | TBSTYLE_LIST | TBSTYLE_FLAT,
                           0, 0, 300, 30, NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (!hWnd)
        return;

    SendMessageW(hWnd, TB_BUTTONSTRUCTSIZE, sizeof(TBBUTTON), 0);
    SendMessageW(hWnd, TB_SETEXTENDEDSTYLE, 0, TBSTYLE_EX_DRAWDDARROWS | TBSTYLE_EX_MIXEDBUTTONS);

    ZeroMemory(&Button, sizeof(Button));
    Button.iBitmap = I_IMAGENONE;
    Button.idCommand = 100;
    Button.fsState = TBSTATE_ENABLED;
    Button.fsStyle = BTNS_DROPDOWN | BTNS_AUTOSIZE | BTNS_SHOWTEXT;
    Button.iString = (INT_PTR)L"Back";
    ok(SendMessageW(hWnd, TB_ADDBUTTONSW, 1, (LPARAM)&Button), "TB_ADDBUTTONSW failed\n");
    SendMessageW(hWnd, TB_AUTOSIZE, 0, 0);

    ok(SendMessageW(hWnd, TB_GETITEMRECT, 0, (LPARAM)&Before), "TB_GETITEMRECT failed\n");
    ok(SendMessageW(hWnd, TB_SETDROPDOWNGAP, 6, 0) == 1, "WM_USER + 100 failed\n");
    ok(SendMessageW(hWnd, TB_GETITEMRECT, 0, (LPARAM)&After), "TB_GETITEMRECT failed\n");

    Delta = (After.right - After.left) - (Before.right - Before.left);
    ok(Delta == 6, "Button width changed by %ld\n", Delta);

    DestroyWindow(hWnd);
}

START_TEST(ToolbarExtras)
{
    InitCommonControls();
    TestRegisterDrop();
    TestDropDownGap();
}
