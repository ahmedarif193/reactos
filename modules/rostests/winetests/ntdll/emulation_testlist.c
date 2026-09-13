/* SPDX-License-Identifier: GPL-2.0-or-later */
#define STANDALONE
#include <wine/test.h>

extern void func_wow64(void);

const struct test winetest_testlist[] =
{
    { "wow64", func_wow64 },
    { 0, 0 }
};
