/*
 * PROJECT:     ReactOS WDDM dxgkrnl - DxgkDdi callback chain tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Test list
 * COPYRIGHT:   Copyright 2025 ReactOS WDDM Team
 */

#define STANDALONE
#include <wine/test.h>

extern void func_DxgkDdiCallbacks(void);

const struct test winetest_testlist[] =
{
    { "DxgkDdiCallbacks", func_DxgkDdiCallbacks },
    { 0, 0 }
};
