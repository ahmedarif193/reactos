
/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     DWM stack API-test registration
 */

#define STANDALONE
#include <apitest.h>

extern void func_dwmstack(void);

const struct test winetest_testlist[] =
{
    { "dwmstack", func_dwmstack },
    { 0, 0 }
};
