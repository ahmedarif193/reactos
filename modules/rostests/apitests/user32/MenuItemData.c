/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that menu item data keeps every bit of a pointer-sized value
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#ifdef _WIN64
#define DATA_ONE ((ULONG_PTR)0x8877665544332211ULL)
#define DATA_TWO ((ULONG_PTR)0x1122334488776655ULL)
#else
#define DATA_ONE ((ULONG_PTR)0x88776655)
#define DATA_TWO ((ULONG_PTR)0x44332211)
#endif

#define ITEM_ID 100

static ULONG_PTR MeasuredData;
static ULONG_PTR DrawnData;

static LRESULT CALLBACK
OwnerProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    switch (Msg)
    {
        case WM_MEASUREITEM:
        {
            MEASUREITEMSTRUCT *pmis = (MEASUREITEMSTRUCT *)lParam;

            if (pmis->CtlType == ODT_MENU && pmis->itemID == ITEM_ID)
            {
                MeasuredData = pmis->itemData;
                pmis->itemWidth = 40;
                pmis->itemHeight = 16;
                return TRUE;
            }
            break;
        }

        case WM_DRAWITEM:
        {
            DRAWITEMSTRUCT *pdis = (DRAWITEMSTRUCT *)lParam;

            if (pdis->CtlType == ODT_MENU && pdis->itemID == ITEM_ID)
            {
                DrawnData = pdis->itemData;
                return TRUE;
            }
            break;
        }
    }

    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

static void
TestItemInfo(void)
{
    MENUITEMINFOW mii;
    HMENU hMenu;

    hMenu = CreatePopupMenu();
    ok(hMenu != NULL, "CreatePopupMenu failed: %lu\n", GetLastError());
    if (!hMenu)
        return;

    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_ID | MIIM_STRING | MIIM_DATA;
    mii.wID = ITEM_ID;
    mii.dwTypeData = L"Item";
    mii.dwItemData = DATA_ONE;
    ok(InsertMenuItemW(hMenu, 0, TRUE, &mii), "InsertMenuItemW failed: %lu\n", GetLastError());

    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_DATA;
    ok(GetMenuItemInfoW(hMenu, ITEM_ID, FALSE, &mii), "GetMenuItemInfoW failed: %lu\n", GetLastError());
    ok(mii.dwItemData == DATA_ONE, "Inserted data is %p\n", (PVOID)mii.dwItemData);

    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_DATA;
    mii.dwItemData = DATA_TWO;
    ok(SetMenuItemInfoW(hMenu, ITEM_ID, FALSE, &mii), "SetMenuItemInfoW failed: %lu\n", GetLastError());

    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_DATA;
    ok(GetMenuItemInfoW(hMenu, 0, TRUE, &mii), "GetMenuItemInfoW failed: %lu\n", GetLastError());
    ok(mii.dwItemData == DATA_TWO, "Updated data is %p\n", (PVOID)mii.dwItemData);

    DestroyMenu(hMenu);
}

static void
TestOwnerDraw(void)
{
    WNDCLASSW Class = { 0 };
    MENUITEMINFOW mii;
    HMENU hMenu;
    HWND hWnd;

    Class.lpfnWndProc = OwnerProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    Class.lpszClassName = L"MenuItemDataOwner";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed: %lu\n", GetLastError());

    hMenu = CreateMenu();
    ZeroMemory(&mii, sizeof(mii));
    mii.cbSize = sizeof(mii);
    mii.fMask = MIIM_ID | MIIM_FTYPE | MIIM_DATA;
    mii.fType = MFT_OWNERDRAW;
    mii.wID = ITEM_ID;
    mii.dwItemData = DATA_ONE;
    ok(InsertMenuItemW(hMenu, 0, TRUE, &mii), "InsertMenuItemW failed: %lu\n", GetLastError());

    hWnd = CreateWindowExW(0, L"MenuItemDataOwner", L"MenuItemData", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                           100, 100, 300, 200, NULL, hMenu, GetModuleHandleW(NULL), NULL);
    ok(hWnd != NULL, "CreateWindowExW failed: %lu\n", GetLastError());
    if (hWnd)
    {
        MeasuredData = 0;
        DrawnData = 0;
        DrawMenuBar(hWnd);
        RedrawWindow(hWnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
        ok(MeasuredData == DATA_ONE, "WM_MEASUREITEM data is %p\n", (PVOID)MeasuredData);
        ok(DrawnData == DATA_ONE, "WM_DRAWITEM data is %p\n", (PVOID)DrawnData);
        DestroyWindow(hWnd);
    }
    else
    {
        DestroyMenu(hMenu);
    }

    UnregisterClassW(L"MenuItemDataOwner", GetModuleHandleW(NULL));
}

START_TEST(MenuItemData)
{
    TestItemInfo();
    TestOwnerDraw();
}
