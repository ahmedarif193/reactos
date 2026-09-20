/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Clearing window display affinity and validating handles
 */

#include "precomp.h"

START_TEST(WindowDisplayAffinity)
{
    HWND Window, Child;
    DWORD Affinity = 0xcccccccc;
    BOOL Result;

    Window = CreateWindowExW(0, L"STATIC", L"Affinity test", WS_OVERLAPPEDWINDOW,
                             0, 0, 200, 100, NULL, NULL, NULL, NULL);
    ok(Window != NULL, "CreateWindow failed: %lu\n", GetLastError());
    if (!Window) return;

    Result = SetWindowDisplayAffinity(Window, WDA_NONE);
    ok(Result, "Clearing affinity failed: %lu\n", GetLastError());
    Result = GetWindowDisplayAffinity(Window, &Affinity);
    ok(Result, "Query failed: %lu\n", GetLastError());
    if (Result) ok_long(Affinity, WDA_NONE);

    Child = CreateWindowExW(0, L"STATIC", L"", WS_CHILD, 0, 0, 10, 10, Window, NULL, NULL, NULL);
    ok(Child != NULL, "Create child failed\n");
    if (Child)
    {
        Result = SetWindowDisplayAffinity(Child, WDA_NONE);
        ok(!Result, "Child window accepted\n");
        ok_long(GetLastError(), ERROR_INVALID_PARAMETER);
        DestroyWindow(Child);
    }
    DestroyWindow(Window);
    Result = SetWindowDisplayAffinity(Window, WDA_NONE);
    ok(!Result, "Destroyed window accepted\n");
    ok_long(GetLastError(), ERROR_INVALID_WINDOW_HANDLE);
    Result = GetWindowDisplayAffinity(Window, &Affinity);
    ok(!Result, "Destroyed window query succeeded\n");
    ok_long(GetLastError(), ERROR_INVALID_WINDOW_HANDLE);
}
