/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that GetTempFileNameW keeps prefix + hex id + .tmp names when generated names collide
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static UINT ParseTempName(PCWSTR Dir, PCWSTR Name, PCWSTR Prefix, BOOL *Valid)
{
    SIZE_T DirLen = wcslen(Dir), PrefixLen = wcslen(Prefix), Digits;
    PCWSTR p;

    *Valid = FALSE;
    if (_wcsnicmp(Name, Dir, DirLen) || Name[DirLen] != L'\\')
        return 0;
    p = Name + DirLen + 1;
    if (_wcsnicmp(p, Prefix, PrefixLen))
        return 0;
    p += PrefixLen;
    for (Digits = 0; iswxdigit(p[Digits]); Digits++);
    if (Digits == 0 || Digits > 4 || _wcsicmp(p + Digits, L".tmp"))
        return 0;
    *Valid = TRUE;
    return wcstoul(p, NULL, 16);
}

static void CleanupDir(PCWSTR Dir)
{
    WCHAR Pattern[MAX_PATH], Path[MAX_PATH];
    WIN32_FIND_DATAW Data;
    HANDLE Find;

    StringCchPrintfW(Pattern, ARRAYSIZE(Pattern), L"%s\\*.tmp", Dir);
    Find = FindFirstFileW(Pattern, &Data);
    if (Find != INVALID_HANDLE_VALUE)
    {
        do
        {
            StringCchPrintfW(Path, ARRAYSIZE(Path), L"%s\\%s", Dir, Data.cFileName);
            DeleteFileW(Path);
        } while (FindNextFileW(Find, &Data));
        FindClose(Find);
    }
    RemoveDirectoryW(Dir);
}

START_TEST(GetTempFileName)
{
    WCHAR TempPath[MAX_PATH], Dir[MAX_PATH], Name[MAX_PATH], Blocker[MAX_PATH];
    UINT First, Id, Parsed, i;
    HANDLE File;
    BOOL Valid;

    ok(GetTempPathW(ARRAYSIZE(TempPath), TempPath) != 0, "GetTempPathW failed, error %lu\n", GetLastError());
    StringCchPrintfW(Dir, ARRAYSIZE(Dir), L"%sGetTempFileName_%lx", TempPath, GetCurrentProcessId());
    CleanupDir(Dir);
    if (!CreateDirectoryW(Dir, NULL))
    {
        skip("CreateDirectoryW(%S) failed, error %lu\n", Dir, GetLastError());
        return;
    }

    Id = GetTempFileNameW(Dir, L"abc", 0x1234, Name);
    ok(Id == 0x1234, "GetTempFileNameW returned %x\n", Id);
    Parsed = ParseTempName(Dir, Name, L"abc", &Valid);
    ok(Valid && Parsed == 0x1234, "unexpected name '%S'\n", Name);
    ok(GetFileAttributesW(Name) == INVALID_FILE_ATTRIBUTES, "file '%S' was created\n", Name);

    First = GetTempFileNameW(Dir, L"abc", 0, Name);
    ok(First != 0, "GetTempFileNameW failed, error %lu\n", GetLastError());
    Parsed = ParseTempName(Dir, Name, L"abc", &Valid);
    ok(Valid && Parsed == First, "unexpected name '%S' for id %x\n", Name, First);
    ok(GetFileAttributesW(Name) != INVALID_FILE_ATTRIBUTES, "file '%S' was not created\n", Name);

    for (i = 1; i <= 16; i++)
    {
        StringCchPrintfW(Blocker, ARRAYSIZE(Blocker), L"%s\\abc%x.tmp", Dir, (First + i) & 0xFFFF);
        File = CreateFileW(Blocker, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (File != INVALID_HANDLE_VALUE)
            CloseHandle(File);
    }

    for (i = 0; i < 4; i++)
    {
        Id = GetTempFileNameW(Dir, L"abc", 0, Name);
        ok(Id != 0, "GetTempFileNameW failed, error %lu\n", GetLastError());
        Parsed = ParseTempName(Dir, Name, L"abc", &Valid);
        ok(Valid && Parsed == Id, "unexpected name '%S' for id %x\n", Name, Id);
        ok(GetFileAttributesW(Name) != INVALID_FILE_ATTRIBUTES, "file '%S' was not created\n", Name);
    }

    CleanupDir(Dir);
}
