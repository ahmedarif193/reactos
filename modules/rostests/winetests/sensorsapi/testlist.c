/*
 * Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define STANDALONE
#include <wine/test.h>

extern void func_sensorsapi(void);

const struct test winetest_testlist[] =
{
    {"sensorsapi", func_sensorsapi},
    {0, 0}
};
