/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Core Audio session registry API-test registration
 */

#define STANDALONE
#include <apitest.h>

extern void func_deviceenum(void);
extern void func_sessionapi(void);
extern void func_sessioncrossproc(void);
extern void func_sessionregistry(void);
extern void func_sessionpublic(void);

const struct test winetest_testlist[] =
{
    { "deviceenum", func_deviceenum },
    { "sessionapi", func_sessionapi },
    { "sessioncrossproc", func_sessioncrossproc },
    { "sessionregistry", func_sessionregistry },
    { "sessionpublic", func_sessionpublic },
    { 0, 0 }
};
