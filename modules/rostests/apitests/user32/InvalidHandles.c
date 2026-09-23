/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that user handles outside the handle table are rejected
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static const ULONG_PTR InvalidHandles[] =
{
    0x0000FFFE,
    0x1234FFFE,
    0x0403D1E0,
    0x7FFFFFFE,
};

START_TEST(InvalidHandles)
{
    ULONG i;

    for (i = 0; i < ARRAYSIZE(InvalidHandles); i++)
    {
        HWND Window = (HWND)InvalidHandles[i];
        HMENU Menu = (HMENU)InvalidHandles[i];

        ok(!IsWindow(Window), "[%lu] IsWindow succeeded\n", i);
        ok(!IsWindowUnicode(Window), "[%lu] IsWindowUnicode succeeded\n", i);
        ok(DefWindowProcW(Window, WM_IME_SETCONTEXT, TRUE, 0) == 0, "[%lu] DefWindowProcW returned nonzero\n", i);
        ok(GetMenuItemCount(Menu) == -1, "[%lu] GetMenuItemCount succeeded\n", i);
    }
}
