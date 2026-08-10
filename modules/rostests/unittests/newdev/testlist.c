/*
 * PROJECT:     LiberNT New Device Installer Unit Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 */

#define STANDALONE
#include <apitest.h>

extern void func_BatchDriverCache(void);

const struct test winetest_testlist[] =
{
    { "BatchDriverCache", func_BatchDriverCache },
    { 0, 0 }
};
