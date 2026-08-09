/*
 * Copyright 2026 Ahmed Arif
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#define STANDALONE
#include <wine/test.h>

extern void func_portabledevicetypes(void);

const struct test winetest_testlist[] =
{
    { "portabledevicetypes", func_portabledevicetypes },
    { 0, 0 }
};
