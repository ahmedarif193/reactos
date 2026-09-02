/*
 * PROJECT:     ReactOS API tests
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Folder execution test list
 */

#define STANDALONE
#include <apitest.h>

extern void func_FolderExecute(void);

const struct test winetest_testlist[] =
{
    { "FolderExecute", func_FolderExecute },
    { 0, 0 }
};
