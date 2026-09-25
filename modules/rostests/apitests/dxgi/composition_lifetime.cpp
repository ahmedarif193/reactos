/* SPDX-License-Identifier: GPL-3.0-or-later
 * A WM_NCDESTROY callback may still acquire a DC. Its redirection must not
 * survive the window and be traversed by a later DwmFlush. Uses public APIs
 * so the same executable can also run against Windows. */
#include <apitest.h>
#include <dwmapi.h>

static unsigned int destroy_callbacks, destroy_dcs;

static LRESULT CALLBACK LifetimeWindowProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    if (message == WM_NCDESTROY)
    {
        ++destroy_callbacks;
        HDC dc = GetDC(window);
        if (dc)
        {
            RECT rect = {0, 0, 16, 16};
            ++destroy_dcs;
            FillRect(dc, &rect, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
            ReleaseDC(window, dc);
        }
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

START_TEST(composition_lifetime)
{
    typedef HRESULT (WINAPI *FLUSH)(void);
    typedef HRESULT (WINAPI *IS_ENABLED)(BOOL *);
    HMODULE module = LoadLibraryW(L"dwmapi.dll");
    if (!module) { skip("DWM unavailable\n"); return; }
    FLUSH flush = reinterpret_cast<FLUSH>(GetProcAddress(module, "DwmFlush"));
    IS_ENABLED is_enabled = reinterpret_cast<IS_ENABLED>(GetProcAddress(module, "DwmIsCompositionEnabled"));
    BOOL enabled = FALSE;
    if (!flush || !is_enabled || FAILED(is_enabled(&enabled)) || !enabled)
    {
        skip("Active composition required for the lifetime regression\n");
        FreeLibrary(module);
        return;
    }
    WNDCLASSW cls = {};
    cls.lpfnWndProc = LifetimeWindowProc;
    cls.hInstance = GetModuleHandleW(NULL);
    cls.lpszClassName = L"DxgiCompositionLifetime";
    ATOM atom = RegisterClassW(&cls);
    ok(atom != 0, "RegisterClass returned %lu\n", GetLastError());
    if (!atom) { FreeLibrary(module); return; }
    destroy_callbacks = destroy_dcs = 0;
    for (UINT i = 0; i < 64; ++i)
    {
        HWND window = CreateWindowExW(0, cls.lpszClassName, L"Composition lifetime",
                WS_OVERLAPPEDWINDOW, 30, 30, 64, 64, NULL, NULL, cls.hInstance, NULL);
        ok(window != NULL, "CreateWindow iteration %u returned %lu\n", i, GetLastError());
        if (!window) break;
        /* Alternate hidden and shown windows. Both may acquire a redirected
         * DC, but neither may recreate compositor state during destruction. */
        if (i & 1) ShowWindow(window, SW_SHOWNOACTIVATE);
        BOOL destroyed = DestroyWindow(window);
        ok(destroyed, "DestroyWindow iteration %u returned %lu\n", i, GetLastError());
        HRESULT hr = flush();
        ok(hr == S_OK, "DwmFlush after destroy iteration %u returned %#lx\n", i, hr);
    }
    ok(destroy_callbacks == 64, "Expected 64 callbacks, got %u\n", destroy_callbacks);
    ok(destroy_dcs == 64, "Expected 64 destruction DCs, got %u\n", destroy_dcs);
    ok(UnregisterClassW(cls.lpszClassName, cls.hInstance), "UnregisterClass returned %lu\n", GetLastError());
    FreeLibrary(module);
}
