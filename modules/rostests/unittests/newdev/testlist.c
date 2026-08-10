/*
 * PROJECT:     ReactOS New Device Installer Unit Tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 */

#define STANDALONE
#include <apitest.h>

extern void func_BatchDriverCache(void);

const struct test winetest_testlist[] =
{
    { "BatchDriverCache", func_BatchDriverCache },
    { 0, 0 }
};
