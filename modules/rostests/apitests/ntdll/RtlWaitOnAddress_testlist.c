/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Focused RtlWaitOnAddress API test entry point
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#define STANDALONE
#include <apitest.h>

extern void func_RtlWaitOnAddress(void);

const struct test winetest_testlist[] =
{
    { "RtlWaitOnAddress", func_RtlWaitOnAddress },
    { 0, 0 }
};
