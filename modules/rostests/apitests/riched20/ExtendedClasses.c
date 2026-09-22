/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the rich edit list box and combo box classes
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>

typedef LRESULT (WINAPI *PFN_REEXTENDEDREGISTERCLASS)(VOID);

static void
TestListBox(void)
{
    WCHAR Text[16];
    HWND hWnd;

    hWnd = CreateWindowExW(0, L"REListBox20W", NULL, WS_POPUP, 0, 0, 100, 100, NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW(REListBox20W) failed: %lu\n", GetLastError());
    if (!hWnd)
        return;

    ok(SendMessageW(hWnd, LB_ADDSTRING, 0, (LPARAM)L"one") == 0, "LB_ADDSTRING failed\n");
    ok(SendMessageW(hWnd, LB_ADDSTRING, 0, (LPARAM)L"two") == 1, "LB_ADDSTRING failed\n");
    ok(SendMessageW(hWnd, LB_GETCOUNT, 0, 0) == 2, "Unexpected item count\n");
    ok(SendMessageW(hWnd, LB_GETTEXT, 1, (LPARAM)Text) == 3, "LB_GETTEXT failed\n");
    ok(!wcscmp(Text, L"two"), "Unexpected item %s\n", wine_dbgstr_w(Text));

    DestroyWindow(hWnd);
}

static void
TestComboBox(void)
{
    WCHAR Text[16];
    HWND hWnd;

    hWnd = CreateWindowExW(0, L"REComboBox20W", NULL, WS_POPUP | CBS_DROPDOWNLIST,
                           0, 0, 100, 100, NULL, NULL, NULL, NULL);
    ok(hWnd != NULL, "CreateWindowExW(REComboBox20W) failed: %lu\n", GetLastError());
    if (!hWnd)
        return;

    ok(SendMessageW(hWnd, CB_ADDSTRING, 0, (LPARAM)L"one") == 0, "CB_ADDSTRING failed\n");
    ok(SendMessageW(hWnd, CB_ADDSTRING, 0, (LPARAM)L"two") == 1, "CB_ADDSTRING failed\n");
    ok(SendMessageW(hWnd, CB_GETCOUNT, 0, 0) == 2, "Unexpected item count\n");
    ok(SendMessageW(hWnd, CB_GETLBTEXT, 1, (LPARAM)Text) == 3, "CB_GETLBTEXT failed\n");
    ok(!wcscmp(Text, L"two"), "Unexpected item %s\n", wine_dbgstr_w(Text));

    DestroyWindow(hWnd);
}

START_TEST(ExtendedClasses)
{
    PFN_REEXTENDEDREGISTERCLASS pREExtendedRegisterClass;
    HMODULE Module;

    Module = LoadLibraryW(L"riched20.dll");
    ok(Module != NULL, "LoadLibraryW failed: %lu\n", GetLastError());
    if (!Module)
        return;

    pREExtendedRegisterClass = (PFN_REEXTENDEDREGISTERCLASS)GetProcAddress(Module, "REExtendedRegisterClass");
    if (!pREExtendedRegisterClass)
    {
        skip("REExtendedRegisterClass is not exported\n");
        FreeLibrary(Module);
        return;
    }

    ok(pREExtendedRegisterClass() != 0, "REExtendedRegisterClass registered nothing\n");
    TestListBox();
    TestComboBox();

    FreeLibrary(Module);
}
