/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Control Flow Guard API-test registration
 */

#define STANDALONE
#include <apitest.h>

extern void func_cfg(void);

const struct test winetest_testlist[] =
{
    { "cfg", func_cfg },
    { 0, 0 }
};
