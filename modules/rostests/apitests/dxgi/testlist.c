/*
 * PROJECT:     ReactOS DXGI API Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Test list
 * COPYRIGHT:   Copyright 2025 ReactOS Contributors
 */

#define STANDALONE
#include <apitest.h>

extern void func_DxgiFactory(void);
extern void func_DxgiAdapter(void);
extern void func_DxgiOutput(void);
extern void func_DxgiSwapChain(void);

const struct test winetest_testlist[] =
{
    { "DxgiFactory",   func_DxgiFactory },
    { "DxgiAdapter",   func_DxgiAdapter },
    { "DxgiOutput",    func_DxgiOutput },
    { "DxgiSwapChain", func_DxgiSwapChain },
    { 0, 0 }
};
