/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Test Folder PIDL command-template dispatch
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "shelltest.h"
#include <strsafe.h>

static BOOL read_capture(LPCWSTR path, WCHAR *buffer, DWORD count)
{
    HANDLE file;
    DWORD bytes;

    file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return FALSE;
    if (!ReadFile(file, buffer, (count - 1) * sizeof(WCHAR), &bytes, NULL))
    {
        CloseHandle(file);
        return FALSE;
    }
    CloseHandle(file);
    buffer[bytes / sizeof(WCHAR)] = 0;
    return TRUE;
}

START_TEST(FolderExecute)
{
    static const WCHAR verb[] = L"ReactOSFolderExecuteTest";
    static const WCHAR command_path[] =
        L"Software\\Classes\\Folder\\shell\\ReactOSFolderExecuteTest\\command";
    WCHAR module[MAX_PATH], helper[MAX_PATH], output[MAX_PATH], command[4 * MAX_PATH];
    WCHAR capture[4 * MAX_PATH];
    SHELLEXECUTEINFOW sei = {sizeof(sei)};
    PIDLIST_ABSOLUTE pidl = NULL;
    HKEY command_key = NULL;
    DWORD size, wait;
    LSTATUS status;
    BOOL ret;

    GetModuleFileNameW(NULL, module, _countof(module));
    lstrcpynW(helper, module, _countof(helper));
    PathRemoveFileSpecW(helper);
    PathAppendW(helper, L"testdata\\folder_execute_helper.exe");
    ok(PathFileExistsW(helper), "helper does not exist: %s.\n", wine_dbgstr_w(helper));

    GetTempPathW(_countof(output), output);
    PathAppendW(output, L"folder_execute_capture.txt");
    DeleteFileW(output);

    status = RegCreateKeyExW(HKEY_LOCAL_MACHINE, command_path, 0, NULL, 0,
                             KEY_SET_VALUE, NULL, &command_key, NULL);
    ok(status == ERROR_SUCCESS, "creating command key returned %ld.\n", status);
    StringCchPrintfW(command, _countof(command),
                     L"\"%s\" --capture \"%s\" marker-before \"%%1\" marker-after",
                     helper, output);
    size = (lstrlenW(command) + 1) * sizeof(WCHAR);
    status = RegSetValueExW(command_key, NULL, 0, REG_SZ, (BYTE *)command, size);
    ok(status == ERROR_SUCCESS, "RegSetValueExW returned %ld.\n", status);
    RegCloseKey(command_key);

    {
        HRESULT hr = SHGetSpecialFolderLocation(NULL, CSIDL_DRIVES, &pidl);
        ok(hr == S_OK, "SHGetSpecialFolderLocation returned %#lx.\n", hr);
        if (SUCCEEDED(hr))
        {
            sei.fMask = SEE_MASK_IDLIST | SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
            sei.lpVerb = verb;
            sei.lpIDList = pidl;
            sei.nShow = SW_HIDE;
            ret = ShellExecuteExW(&sei);
            ok(ret, "ShellExecuteExW failed, error %lu, hInstApp %p.\n",
               GetLastError(), sei.hInstApp);
            if (ret && sei.hProcess)
            {
                wait = WaitForSingleObject(sei.hProcess, 10000);
                ok(wait == WAIT_OBJECT_0, "handler wait returned %#lx.\n", wait);
                CloseHandle(sei.hProcess);
            }
            ILFree(pidl);
        }
    }

    ret = read_capture(output, capture, _countof(capture));
    ok(ret,
       "handler did not create %s.\n", wine_dbgstr_w(output));
    if (ret)
    {
        ok(wcsstr(capture, L"marker-before\r\n") != NULL,
           "prefix argument missing: %s.\n", wine_dbgstr_w(capture));
        ok(wcsstr(capture, L"::{") != NULL,
           "PIDL parsing name missing: %s.\n", wine_dbgstr_w(capture));
        ok(wcsstr(capture, L"marker-after\r\n") != NULL,
           "suffix argument missing: %s.\n", wine_dbgstr_w(capture));
    }

    DeleteFileW(output);
    RegDeleteTreeW(HKEY_LOCAL_MACHINE,
                   L"Software\\Classes\\Folder\\shell\\ReactOSFolderExecuteTest");
}
