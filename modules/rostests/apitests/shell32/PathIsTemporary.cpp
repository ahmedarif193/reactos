/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for PathIsTemporaryA/W
 * COPYRIGHT:   Copyright 2023 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#include "shelltest.h"
#include <undocshell.h>

static void Test_PathIsTemporaryA(void)
{
    CHAR szPath[MAX_PATH];
    BOOL bCreated;
    ok_int(PathIsTemporaryA("C:\\"), FALSE);
    ok_int(PathIsTemporaryA("C:\\TestTestTest"), FALSE);

    GetWindowsDirectoryA(szPath, _countof(szPath));
    ok_int(PathIsTemporaryA(szPath), FALSE);

    GetTempPathA(_countof(szPath), szPath);
    ok_int(PathIsTemporaryA(szPath), TRUE);

    if (_WIN32_WINNT <= _WIN32_WINNT_WS03)
    {
        /* This is not reliable on Vista+ */
        PathAppendA(szPath, "TestTestTest");
        ok_int(PathIsTemporaryA(szPath), FALSE);
    }

    bCreated = CreateDirectoryA(szPath, NULL);
    ok_int(PathIsTemporaryA(szPath), TRUE);

    if (bCreated)
        RemoveDirectoryA(szPath);
}

static void Test_PathIsTemporaryW(void)
{
    WCHAR szPath[MAX_PATH];
    BOOL bCreated;
    ok_int(PathIsTemporaryW(L"C:\\"), FALSE);
    ok_int(PathIsTemporaryW(L"C:\\TestTestTest"), FALSE);

    GetWindowsDirectoryW(szPath, _countof(szPath));
    ok_int(PathIsTemporaryW(szPath), FALSE);

    GetTempPathW(_countof(szPath), szPath);
    ok_int(PathIsTemporaryW(szPath), TRUE);

    if (_WIN32_WINNT <= _WIN32_WINNT_WS03)
    {
        /* This is not reliable on Vista+ */
        PathAppendW(szPath, L"TestTestTest");
        ok_int(PathIsTemporaryW(szPath), FALSE);
    }

    bCreated = CreateDirectoryW(szPath, NULL);
    ok_int(PathIsTemporaryW(szPath), TRUE);

    if (bCreated)
        RemoveDirectoryW(szPath);
}

START_TEST(PathIsTemporary)
{
    Test_PathIsTemporaryA();
    Test_PathIsTemporaryW();
}
