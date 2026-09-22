/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test list for riched20
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_ContextMenu(void);
extern void func_EditStyle(void);
extern void func_ExtendedClasses(void);

const struct test winetest_testlist[] =
{
    { "ContextMenu", func_ContextMenu },
    { "EditStyle", func_EditStyle },
    { "ExtendedClasses", func_ExtendedClasses },

    { 0, 0 }
};
