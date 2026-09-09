/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ANSI and Unicode subclass procedure regression tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <windows.h>
#include <commctrl.h>
#include <wine/test.h>

static WNDPROC PreviousProc;
static BOOL UnicodeSubclass;
static BOOL ReceivedText;

static LRESULT CALLBACK ForwardProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (UnicodeSubclass)
        return CallWindowProcW(PreviousProc, hwnd, msg, wparam, lparam);
    return CallWindowProcA(PreviousProc, hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK TextProcW(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_SETTEXT)
    {
        ReceivedText = !lstrcmpW((LPCWSTR)lparam, L"callback text");
        return 123;
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

static LRESULT CALLBACK TextProcA(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_SETTEXT)
    {
        ReceivedText = !lstrcmpA((LPCSTR)lparam, "callback text");
        return 123;
    }
    return DefWindowProcA(hwnd, msg, wparam, lparam);
}

static void test_text_subclass(BOOL unicode)
{
    WNDCLASSW wcw = {0};
    WNDCLASSA wca = {0};
    HINSTANCE instance = GetModuleHandleW(NULL);
    HWND hwnd;
    ATOM atom;
    LRESULT result;

    if (unicode)
    {
        wcw.lpfnWndProc = TextProcW;
        wcw.hInstance = instance;
        wcw.lpszClassName = L"CallWindowProcTestW";
        atom = RegisterClassW(&wcw);
        hwnd = CreateWindowExW(0, wcw.lpszClassName, NULL, 0, 0, 0, 100, 100, NULL, NULL, instance, NULL);
    }
    else
    {
        wca.lpfnWndProc = TextProcA;
        wca.hInstance = instance;
        wca.lpszClassName = "CallWindowProcTestA";
        atom = RegisterClassA(&wca);
        hwnd = CreateWindowExA(0, wca.lpszClassName, NULL, 0, 0, 0, 100, 100, NULL, NULL, instance, NULL);
    }
    ok(atom != 0, "RegisterClass(%d) failed: %lu\n", unicode, GetLastError());
    ok(hwnd != NULL, "CreateWindow(%d) failed: %lu\n", unicode, GetLastError());
    if (hwnd)
    {
        UnicodeSubclass = !unicode;
        if (unicode)
            PreviousProc = (WNDPROC)SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)ForwardProc);
        else
            PreviousProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)ForwardProc);
        ok(PreviousProc != NULL, "Subclass(%d) failed: %lu\n", unicode, GetLastError());
        trace("original unicode=%d previous=%p\n", unicode, PreviousProc);
        if (PreviousProc)
        {
            ReceivedText = FALSE;
            if (unicode)
                result = CallWindowProcA(PreviousProc, hwnd, WM_SETTEXT, 0, (LPARAM)"callback text");
            else
                result = CallWindowProcW(PreviousProc, hwnd, WM_SETTEXT, 0, (LPARAM)L"callback text");
            ok(result == 123 && ReceivedText, "Direct conversion(%d): result=%ld text=%d\n", unicode, (LONG)result, ReceivedText);

            ReceivedText = FALSE;
            if (unicode)
                result = SendMessageA(hwnd, WM_SETTEXT, 0, (LPARAM)"callback text");
            else
                result = SendMessageW(hwnd, WM_SETTEXT, 0, (LPARAM)L"callback text");
            ok(result == 123 && ReceivedText, "Subclass conversion(%d): result=%ld text=%d\n", unicode, (LONG)result, ReceivedText);

            if (unicode)
                SetWindowLongPtrA(hwnd, GWLP_WNDPROC, (LONG_PTR)PreviousProc);
            else
                SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)PreviousProc);
        }
        DestroyWindow(hwnd);
    }
    if (unicode)
        UnregisterClassW(wcw.lpszClassName, instance);
    else
        UnregisterClassA(wca.lpszClassName, instance);
}

static void test_trackbar(void)
{
    HMODULE module = LoadLibraryW(L"comctl32.dll");
    BOOL (WINAPI *init_controls)(const INITCOMMONCONTROLSEX *);
    INITCOMMONCONTROLSEX controls = {sizeof(controls), ICC_BAR_CLASSES};
    HWND parent, trackbar;
    LRESULT result;

    ok(module != NULL, "LoadLibrary(comctl32) failed: %lu\n", GetLastError());
    if (!module) return;
    init_controls = (void *)GetProcAddress(module, "InitCommonControlsEx");
    ok(init_controls && init_controls(&controls), "InitCommonControlsEx failed\n");
    parent = CreateWindowExW(0, L"STATIC", NULL, 0, 0, 0, 200, 100, NULL, NULL, NULL, NULL);
    trackbar = CreateWindowExW(0, TRACKBAR_CLASSW, NULL, WS_CHILD, 0, 0, 150, 30, parent, NULL, NULL, NULL);
    ok(parent && trackbar, "Create trackbar failed: %lu\n", GetLastError());
    if (trackbar)
    {
        SendMessageW(trackbar, TBM_SETRANGEMIN, FALSE, 10);
        SendMessageW(trackbar, TBM_SETRANGEMAX, FALSE, 90);
        SendMessageW(trackbar, TBM_SETPOS, FALSE, 42);
        UnicodeSubclass = FALSE;
        PreviousProc = (WNDPROC)SetWindowLongPtrA(trackbar, GWLP_WNDPROC, (LONG_PTR)ForwardProc);
        ok(PreviousProc != NULL, "Subclass trackbar failed: %lu\n", GetLastError());
        trace("trackbar previous=%p\n", PreviousProc);
        if (PreviousProc)
        {
            result = CallWindowProcA(PreviousProc, trackbar, TBM_GETRANGEMIN, 0, 0);
            ok(result == 10, "TBM_GETRANGEMIN returned %ld\n", (LONG)result);
            result = CallWindowProcA(PreviousProc, trackbar, TBM_GETRANGEMAX, 0, 0);
            ok(result == 90, "TBM_GETRANGEMAX returned %ld\n", (LONG)result);
            result = CallWindowProcA(PreviousProc, trackbar, TBM_GETPOS, 0, 0);
            ok(result == 42, "TBM_GETPOS returned %ld\n", (LONG)result);
            result = SendMessageA(trackbar, TBM_GETRANGEMAX, 0, 0);
            ok(result == 90, "Subclass TBM_GETRANGEMAX returned %ld\n", (LONG)result);
            SetWindowLongPtrA(trackbar, GWLP_WNDPROC, (LONG_PTR)PreviousProc);
        }
        DestroyWindow(trackbar);
    }
    if (parent) DestroyWindow(parent);
    FreeLibrary(module);
}

START_TEST(CallWindowProc)
{
    test_text_subclass(TRUE);
    test_text_subclass(FALSE);
    test_trackbar();
    trace("CALLWINDOWPROC_TEST_COMPLETE\n");
}
