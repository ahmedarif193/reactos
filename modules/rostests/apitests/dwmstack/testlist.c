
/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DWM stack API-test registration
 */

#define STANDALONE
#include <apitest.h>

extern void func_dwmstack(void);
extern void func_scene_cache(void);

const struct test winetest_testlist[] =
{
    { "dwmstack", func_dwmstack },
    { "scene_cache", func_scene_cache },
    { 0, 0 }
};
