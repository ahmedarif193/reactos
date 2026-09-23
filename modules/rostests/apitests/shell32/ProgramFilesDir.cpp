/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests that the Program Files locations agree with the system drive
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "shelltest.h"

static BOOL
QueryCurrentVersion(PCWSTR Name, PWSTR Value, DWORD Size)
{
    DWORD Type, Bytes = Size * sizeof(WCHAR);
    LONG Error;

    Error = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion",
                         Name, RRF_RT_REG_SZ, &Type, Value, &Bytes);
    ok(Error == ERROR_SUCCESS, "%ls: RegGetValueW failed with %ld\n", Name, Error);
    return Error == ERROR_SUCCESS;
}

static void
TestFolder(PCWSTR Name, INT Csidl, PCWSTR WindowsDir)
{
    WCHAR RegPath[MAX_PATH], ShellPath[MAX_PATH];
    DWORD Attributes;
    HRESULT hr;

    if (!QueryCurrentVersion(Name, RegPath, _countof(RegPath)))
        return;

    hr = SHGetFolderPathW(NULL, Csidl, NULL, SHGFP_TYPE_CURRENT, ShellPath);
    ok(hr == S_OK, "%ls: SHGetFolderPathW(%d) failed with 0x%lx\n", Name, Csidl, hr);
    if (hr == S_OK)
        ok(!lstrcmpiW(RegPath, ShellPath), "%ls: registry %ls, shell %ls\n", Name, RegPath, ShellPath);

    ok(towupper(RegPath[0]) == towupper(WindowsDir[0]) && RegPath[1] == L':',
       "%ls: %ls is not on the system drive of %ls\n", Name, RegPath, WindowsDir);

    Attributes = GetFileAttributesW(RegPath);
    ok(Attributes != INVALID_FILE_ATTRIBUTES && (Attributes & FILE_ATTRIBUTE_DIRECTORY),
       "%ls: %ls is not a directory (0x%lx)\n", Name, RegPath, Attributes);
}

START_TEST(ProgramFilesDir)
{
    WCHAR WindowsDir[MAX_PATH];

    ok(GetWindowsDirectoryW(WindowsDir, _countof(WindowsDir)) != 0,
       "GetWindowsDirectoryW failed: %lu\n", GetLastError());

    TestFolder(L"ProgramFilesDir", CSIDL_PROGRAM_FILES, WindowsDir);
    TestFolder(L"CommonFilesDir", CSIDL_PROGRAM_FILES_COMMON, WindowsDir);
#ifdef _WIN64
    TestFolder(L"ProgramFilesDir (x86)", CSIDL_PROGRAM_FILESX86, WindowsDir);
    TestFolder(L"CommonFilesDir (x86)", CSIDL_PROGRAM_FILES_COMMONX86, WindowsDir);
#endif
}
