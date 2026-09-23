/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that 30 levels of SetWindowPos from WM_SIZE reach every child window
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define NESTED_LEVELS 30

static HWND g_Windows[NESTED_LEVELS + 1];
static LONG g_Deepest;

static LRESULT CALLBACK
NestedWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
    if (Msg == WM_SIZE)
    {
        LONG Level = (LONG)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        RECT Rect;

        if (Level > g_Deepest)
            g_Deepest = Level;
        if (Level < NESTED_LEVELS && g_Windows[Level + 1])
        {
            GetClientRect(hWnd, &Rect);
            SetWindowPos(g_Windows[Level + 1], NULL, 1, 1,
                         Rect.right > 2 ? Rect.right - 2 : 1,
                         Rect.bottom > 2 ? Rect.bottom - 2 : 1,
                         SWP_NOZORDER | SWP_NOACTIVATE);
        }
    }
    return DefWindowProcW(hWnd, Msg, wParam, lParam);
}

START_TEST(NestedWindowPos)
{
    WNDCLASSW Class = { 0 };
    LONG Level;

    Class.lpfnWndProc = NestedWndProc;
    Class.hInstance = GetModuleHandleW(NULL);
    Class.lpszClassName = L"NestedWindowPos";
    ok(RegisterClassW(&Class) != 0, "RegisterClassW failed, error %lu\n", GetLastError());

    g_Windows[0] = CreateWindowExW(0, Class.lpszClassName, L"NestedWindowPos", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                   50, 50, 800, 700, NULL, NULL, Class.hInstance, NULL);
    ok(g_Windows[0] != NULL, "CreateWindowExW failed, error %lu\n", GetLastError());
    if (!g_Windows[0])
        return;
    SetWindowLongPtrW(g_Windows[0], GWLP_USERDATA, 0);

    for (Level = 1; Level <= NESTED_LEVELS; Level++)
    {
        g_Windows[Level] = CreateWindowExW(0, Class.lpszClassName, NULL, WS_CHILD | WS_VISIBLE,
                                           1, 1, 10, 10, g_Windows[Level - 1], NULL, Class.hInstance, NULL);
        ok(g_Windows[Level] != NULL, "CreateWindowExW(%ld) failed, error %lu\n", Level, GetLastError());
        if (!g_Windows[Level])
            break;
        SetWindowLongPtrW(g_Windows[Level], GWLP_USERDATA, Level);
    }

    g_Deepest = -1;
    SetWindowPos(g_Windows[0], NULL, 0, 0, 900, 750, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    ok(g_Deepest == NESTED_LEVELS, "WM_SIZE reached level %ld, expected %d\n", g_Deepest, NESTED_LEVELS);

    DestroyWindow(g_Windows[0]);
    UnregisterClassW(Class.lpszClassName, Class.hInstance);
}
