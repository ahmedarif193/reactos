/*
 * PROJECT:     ReactOS
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Input method window and context regression tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"
#include <imm.h>

static LRESULT CALLBACK ImeParentProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

START_TEST(ImeWindow)
{
    WNDCLASSW wc = {0};
    HINSTANCE instance = GetModuleHandleW(NULL);
    HWND parent, child, ime, explicitIme;
    HIMC context, custom, current;
    DWORD thread;
    UINT i, j;
    WCHAR description[80];

    ok(!ImmIsIME(NULL), "A null keyboard layout was reported as an IME\n");
    memset(description, 0xa5, sizeof(description));
    ok(ImmGetDescriptionW(NULL, description, _countof(description)) == 0, "A null layout returned an IME description\n");

    wc.lpfnWndProc = ImeParentProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"ImeWindowApiTest";
    if (!RegisterClassW(&wc))
    {
        skip("RegisterClassW failed: %lu\n", GetLastError());
        return;
    }

    for (i = 0; i < 32; ++i)
    {
        parent = CreateWindowW(wc.lpszClassName, L"IME parent", WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, NULL, NULL, instance, NULL);
        ok(parent != NULL, "Iteration %u: parent creation failed: %lu\n", i, GetLastError());
        if (!parent) break;

        ime = ImmGetDefaultIMEWnd(parent);
        ok(ime != NULL && IsWindow(ime), "Iteration %u: default IME window missing: %lu\n", i, GetLastError());
        if (ime)
        {
            thread = GetWindowThreadProcessId(ime, NULL);
            ok(thread == GetCurrentThreadId(), "IME belongs to thread %lu\n", thread);
        }

        child = CreateWindowW(L"EDIT", L"", WS_CHILD, 0, 0, 100, 20, parent, NULL, instance, NULL);
        ok(child != NULL, "Iteration %u: child creation failed: %lu\n", i, GetLastError());
        if (child) ok(ImmGetDefaultIMEWnd(child) == ime, "Parent and child have different default IME windows\n");

        context = ImmGetContext(parent);
        ok(context != NULL, "Iteration %u: default input context missing\n", i);
        if (context)
        {
            ok(ImmReleaseContext(parent, context), "Releasing the default context failed\n");
            for (j = 0; j < 16; ++j)
            {
                current = ImmGetContext(parent);
                ok(current == context, "Repeated context lookup returned %p instead of %p\n", current, context);
                ok(ImmReleaseContext(parent, current), "Repeated context release failed\n");
            }
        }

        custom = ImmCreateContext();
        ok(custom != NULL, "Creating an input context failed\n");
        if (custom)
        {
            ok(ImmAssociateContext(parent, custom) == context, "Associating a context did not return the previous context\n");
            current = ImmGetContext(parent);
            ok(current == custom, "Associated context was not returned: %p\n", current);
            if (current) ImmReleaseContext(parent, current);
            ok(ImmAssociateContext(parent, NULL) == custom, "Removing the association did not return the custom context\n");
            ok(ImmGetContext(parent) == NULL, "A disassociated window still has a context\n");
            ok(ImmAssociateContext(parent, context) == NULL, "Restoring the default context did not return NULL\n");
            ok(ImmDestroyContext(custom), "Destroying the custom context failed\n");
        }

        ok(ImmAssociateContextEx(parent, NULL, IACE_CHILDREN), "Disassociating the window tree failed\n");
        current = ImmGetContext(parent);
        ok(current == context, "IACE_CHILDREN changed the parent context\n");
        if (current) ImmReleaseContext(parent, current);
        if (child) ok(ImmGetContext(child) == NULL, "Child still has a context after disassociation\n");
        ok(ImmAssociateContextEx(parent, NULL, IACE_DEFAULT | IACE_CHILDREN), "Restoring the window tree failed\n");
        current = ImmGetContext(parent);
        ok(current == context, "Restoring the default context returned %p instead of %p\n", current, context);
        if (current) ImmReleaseContext(parent, current);

        explicitIme = CreateWindowW(L"IME", L"IME instance", WS_POPUP | WS_DISABLED, 0, 0, 0, 0, parent, NULL, instance, NULL);
        ok(explicitIme != NULL, "Iteration %u: explicit IME creation failed: %lu\n", i, GetLastError());
        if (explicitIme) ok(DestroyWindow(explicitIme), "DestroyWindow(IME) failed: %lu\n", GetLastError());

        ok(DestroyWindow(parent), "DestroyWindow(parent) failed: %lu\n", GetLastError());
        ok(!IsWindow(parent), "Destroyed parent remains valid\n");
    }
    ok(UnregisterClassW(wc.lpszClassName, instance), "UnregisterClassW failed: %lu\n", GetLastError());
}
