/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

static const WCHAR SysWow64SuffixW[] = L"\\SysWOW64";
static const CHAR SysWow64SuffixA[] = "\\SysWOW64";

static
BOOL
IsWow64DirectoryAvailable(VOID)
{
#ifdef _WIN64
    return TRUE;
#else
    BOOL Wow64Process = FALSE;
    BOOL Success;

    Success = IsWow64Process(GetCurrentProcess(), &Wow64Process);
    ok(Success, "IsWow64Process failed with %lu\n", GetLastError());
    return Success && Wow64Process;
#endif
}

static
VOID
TestGetSystemWow64DirectoryW(VOID)
{
    WCHAR Expected[MAX_PATH], Buffer[MAX_PATH];
    UINT ExpectedSize, Length;
    DWORD Error;

    Length = GetWindowsDirectoryW(Expected, RTL_NUMBER_OF(Expected));
    ok(Length && Length < RTL_NUMBER_OF(Expected) - RTL_NUMBER_OF(SysWow64SuffixW), "GetWindowsDirectoryW returned %u\n", Length);
    if (!Length || Length >= RTL_NUMBER_OF(Expected) - RTL_NUMBER_OF(SysWow64SuffixW))
        return;

    lstrcatW(Expected, SysWow64SuffixW);
    ExpectedSize = lstrlenW(Expected) + 1;

    SetLastError(0xdeadbeef);
    Length = GetSystemWow64DirectoryW(NULL, 0);
    Error = GetLastError();
    ok(Length == ExpectedSize, "Length is %u, expected %u\n", Length, ExpectedSize);
    ok(Error == 0xdeadbeef, "Last error is %lu\n", Error);

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    SetLastError(0xdeadbeef);
    Length = GetSystemWow64DirectoryW(Buffer, ExpectedSize - 1);
    Error = GetLastError();
    ok(Length == ExpectedSize, "Length is %u, expected %u\n", Length, ExpectedSize);
    ok(Buffer[0] == 0x5555, "Buffer was modified: %#x\n", Buffer[0]);
    ok(Error == 0xdeadbeef, "Last error is %lu\n", Error);

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    SetLastError(0xdeadbeef);
    Length = GetSystemWow64DirectoryW(Buffer, ExpectedSize);
    Error = GetLastError();
    ok(Length == ExpectedSize - 1, "Length is %u, expected %u\n", Length, ExpectedSize - 1);
    ok(!wcscmp(Buffer, Expected), "Path is %ls, expected %ls\n", Buffer, Expected);
    ok(Buffer[ExpectedSize] == 0x5555, "Buffer tail was modified: %#x\n", Buffer[ExpectedSize]);
    ok(Error == 0xdeadbeef, "Last error is %lu\n", Error);
}

static
VOID
TestGetSystemWow64DirectoryA(VOID)
{
    CHAR Expected[MAX_PATH], Buffer[MAX_PATH];
    UINT ExpectedSize, Length;
    DWORD Error;

    Length = GetWindowsDirectoryA(Expected, RTL_NUMBER_OF(Expected));
    ok(Length && Length < RTL_NUMBER_OF(Expected) - RTL_NUMBER_OF(SysWow64SuffixA), "GetWindowsDirectoryA returned %u\n", Length);
    if (!Length || Length >= RTL_NUMBER_OF(Expected) - RTL_NUMBER_OF(SysWow64SuffixA))
        return;

    lstrcatA(Expected, SysWow64SuffixA);
    ExpectedSize = lstrlenA(Expected) + 1;

    SetLastError(0xdeadbeef);
    Length = GetSystemWow64DirectoryA(NULL, 0);
    Error = GetLastError();
    ok(Length == ExpectedSize, "Length is %u, expected %u\n", Length, ExpectedSize);
    ok(Error == 0xdeadbeef, "Last error is %lu\n", Error);

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    SetLastError(0xdeadbeef);
    Length = GetSystemWow64DirectoryA(Buffer, ExpectedSize - 1);
    Error = GetLastError();
    ok(Length == ExpectedSize, "Length is %u, expected %u\n", Length, ExpectedSize);
    ok(Buffer[0] == 0x55, "Buffer was modified: %#x\n", Buffer[0]);
    ok(Error == 0xdeadbeef, "Last error is %lu\n", Error);

    RtlFillMemory(Buffer, sizeof(Buffer), 0x55);
    SetLastError(0xdeadbeef);
    Length = GetSystemWow64DirectoryA(Buffer, ExpectedSize);
    Error = GetLastError();
    ok(Length == ExpectedSize - 1, "Length is %u, expected %u\n", Length, ExpectedSize - 1);
    ok(!strcmp(Buffer, Expected), "Path is %s, expected %s\n", Buffer, Expected);
    ok(Buffer[ExpectedSize] == 0x55, "Buffer tail was modified: %#x\n", Buffer[ExpectedSize]);
    ok(Error == 0xdeadbeef, "Last error is %lu\n", Error);
}

START_TEST(GetSystemWow64Directory)
{
    if (!IsWow64DirectoryAvailable())
    {
        SetLastError(0xdeadbeef);
        ok(!GetSystemWow64DirectoryW(NULL, 0), "GetSystemWow64DirectoryW unexpectedly succeeded\n");
        ok(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED, "Last error is %lu\n", GetLastError());

        SetLastError(0xdeadbeef);
        ok(!GetSystemWow64DirectoryA(NULL, 0), "GetSystemWow64DirectoryA unexpectedly succeeded\n");
        ok(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED, "Last error is %lu\n", GetLastError());
        return;
    }

    TestGetSystemWow64DirectoryW();
    TestGetSystemWow64DirectoryA();
}
