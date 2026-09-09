/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Combo box dropped rectangle regression tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <wine/test.h>

static LRESULT CALLBACK ComboOwnerProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_MEASUREITEM)
    {
        ((MEASUREITEMSTRUCT *)LParam)->itemHeight = 18;
        return TRUE;
    }
    return DefWindowProcW(Window, Message, WParam, LParam);
}

START_TEST(ComboBox)
{
    const DWORD Styles[] = {CBS_SIMPLE, CBS_DROPDOWN, CBS_DROPDOWNLIST, CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS};
    WNDCLASSW Class = {0};
    HWND Parent, Combo;
    RECT WindowRect, DropA, DropW;
    LRESULT Result;
    unsigned i, Unicode;

    Class.lpfnWndProc = ComboOwnerProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"ComboBoxOwner";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed: %lu\n", GetLastError());
    Parent = CreateWindowW(Class.lpszClassName, L"Combo box rectangle", WS_OVERLAPPEDWINDOW, 96, 64, 600, 400, NULL, NULL, Class.hInstance, NULL);
    ok(Parent != NULL, "CreateWindowW failed: %lu\n", GetLastError());
    if (!Parent)
        goto Cleanup;

    for (Unicode = 0; Unicode != 2; ++Unicode)
    {
        for (i = 0; i < sizeof(Styles) / sizeof(Styles[0]); ++i)
        {
            if (Unicode)
                Combo = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | Styles[i], 50, 40, 160, 140, Parent, NULL, Class.hInstance, NULL);
            else
                Combo = CreateWindowA("COMBOBOX", "", WS_CHILD | Styles[i], 50, 40, 160, 140, Parent, NULL, Class.hInstance, NULL);
            ok(Combo != NULL, "CreateWindow(%u, %#lx) failed: %lu\n", Unicode, Styles[i], GetLastError());
            if (!Combo)
                continue;

            ok(GetWindowRect(Combo, &WindowRect), "GetWindowRect failed: %lu\n", GetLastError());
            SetRectEmpty(&DropA);
            Result = SendMessageA(Combo, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)&DropA);
            ok(Result != 0, "CB_GETDROPPEDCONTROLRECT A(%u, %#lx) returned %Id\n", Unicode, Styles[i], Result);
            ok(DropA.left == WindowRect.left && DropA.top == WindowRect.top, "Dropped rectangle origin (%ld,%ld), window origin (%ld,%ld)\n", DropA.left, DropA.top, WindowRect.left, WindowRect.top);
            ok(DropA.right > DropA.left && DropA.bottom > DropA.top, "Empty dropped rectangle (%ld,%ld)-(%ld,%ld)\n", DropA.left, DropA.top, DropA.right, DropA.bottom);
            SetRectEmpty(&DropW);
            Result = SendMessageW(Combo, CB_GETDROPPEDCONTROLRECT, 0, (LPARAM)&DropW);
            ok(Result != 0, "CB_GETDROPPEDCONTROLRECT W(%u, %#lx) returned %Id\n", Unicode, Styles[i], Result);
            ok(EqualRect(&DropA, &DropW), "ANSI and Unicode dropped rectangles differ\n");
            DestroyWindow(Combo);
        }
    }

    DestroyWindow(Parent);
Cleanup:
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
