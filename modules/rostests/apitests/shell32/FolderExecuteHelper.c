/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Command-line capture helper for FolderExecute
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>

int wmain(int argc, WCHAR **argv)
{
    HANDLE file;
    DWORD written;
    int i;

    if (argc < 4 || lstrcmpW(argv[1], L"--capture"))
        return 2;

    file = CreateFileW(argv[2], GENERIC_WRITE, FILE_SHARE_READ, NULL,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return 3;

    for (i = 3; i < argc; ++i)
    {
        WriteFile(file, argv[i], lstrlenW(argv[i]) * sizeof(WCHAR), &written, NULL);
        WriteFile(file, L"\r\n", 2 * sizeof(WCHAR), &written, NULL);
    }
    CloseHandle(file);
    return 0;
}
