/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Validate OS image trust without trusting a pathname
 */
#include "precomp.h"

typedef BOOL (WINAPI *SET_POLICY)(PROCESS_MITIGATION_POLICY, PVOID, SIZE_T);

static void test_catalog_child(void)
{
    static const WCHAR *images[] = { L"version.dll", L"rpi5vc4d3d.dll", L"mesa_gallium.dll" };
    WCHAR system[MAX_PATH], temp[MAX_PATH], source[MAX_PATH];
    WCHAR copies[sizeof(images) / sizeof(images[0])][MAX_PATH] = {{0}}, changed[MAX_PATH] = {0};
    BOOL present[sizeof(images) / sizeof(images[0])] = {0};
    PROCESS_MITIGATION_BINARY_SIGNATURE_POLICY policy = {0};
    SET_POLICY set_policy = (void *)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "SetProcessMitigationPolicy");
    HMODULE module;
    HANDLE file;
    DWORD count, error;
    unsigned int i;
    BYTE byte = 0x5a;

    if (!set_policy) { skip("Signature policy unavailable\n"); return; }
    if (!GetSystemDirectoryW(system, ARRAYSIZE(system)) || !GetTempPathW(ARRAYSIZE(temp), temp))
    {
        ok(0, "Cannot obtain test directories: %lu\n", GetLastError());
        return;
    }
    for (i = 0; i < ARRAYSIZE(images); ++i)
    {
        StringCchPrintfW(source, ARRAYSIZE(source), L"%s\\%s", system, images[i]);
        if (GetFileAttributesW(source) == INVALID_FILE_ATTRIBUTES)
        {
            if (i) skip("%ls not installed\n", images[i]);
            else ok(0, "Missing %ls\n", images[i]);
            continue;
        }
        if (!GetTempFileNameW(temp, L"cit", 0, copies[i]))
        {
            ok(0, "GetTempFileName: %lu\n", GetLastError());
            continue;
        }
        present[i] = CopyFileW(source, copies[i], FALSE);
        ok(present[i], "Copy %ls: %lu\n", images[i], GetLastError());
    }
    if (present[0] && GetTempFileNameW(temp, L"cim", 0, changed) && CopyFileW(copies[0], changed, FALSE))
    {
        file = CreateFileW(changed, FILE_APPEND_DATA, 0, NULL, OPEN_EXISTING, 0, NULL);
        ok(file != INVALID_HANDLE_VALUE, "Open modified copy: %lu\n", GetLastError());
        if (file != INVALID_HANDLE_VALUE)
        {
            ok(WriteFile(file, &byte, 1, &count, NULL) && count == 1, "Append test byte\n");
            CloseHandle(file);
        }
        /* The changed file must still be a loadable PE without signing policy. */
        module = LoadLibraryExW(changed, NULL, DONT_RESOLVE_DLL_REFERENCES);
        ok(module != NULL, "Modified PE failed before policy: %lu\n", GetLastError());
        if (module) FreeLibrary(module);
    }
    else ok(0, "Create modified copy: %lu\n", GetLastError());

    policy.MicrosoftSignedOnly = 1;
    if (!set_policy(ProcessSignaturePolicy, &policy, sizeof(policy)))
    {
        ok(0, "Set signature policy: %lu\n", GetLastError());
        goto cleanup;
    }
    for (i = 0; i < ARRAYSIZE(images); ++i)
    {
        if (!present[i]) continue;
        StringCchPrintfW(source, ARRAYSIZE(source), L"%s\\%s", system, images[i]);
        module = LoadLibraryExW(source, NULL, DONT_RESOLVE_DLL_REFERENCES);
        ok(module != NULL, "Policy rejected system image %ls: %lu\n", images[i], GetLastError());
        if (module) FreeLibrary(module);
        module = LoadLibraryExW(copies[i], NULL, DONT_RESOLVE_DLL_REFERENCES);
        ok(module != NULL, "Policy rejected identical relocated image %ls: %lu\n", images[i], GetLastError());
        if (module) FreeLibrary(module);
    }
    if (changed[0])
    {
        SetLastError(0);
        module = LoadLibraryExW(changed, NULL, DONT_RESOLVE_DLL_REFERENCES);
        error = GetLastError();
        ok(module == NULL, "Policy accepted modified image\n");
        ok(error == ERROR_INVALID_IMAGE_HASH, "Modified image error %lu\n", error);
        if (module) FreeLibrary(module);
    }
cleanup:
    for (i = 0; i < ARRAYSIZE(images); ++i) if (copies[i][0]) DeleteFileW(copies[i]);
    if (changed[0]) DeleteFileW(changed);
}

START_TEST(SystemImageCatalog)
{
    char **argv;
    int argc = winetest_get_mainargs(&argv);
    WCHAR application[MAX_PATH], command[MAX_PATH + 80];
    STARTUPINFOW startup = {sizeof(startup)};
    PROCESS_INFORMATION process;
    BOOL success;
    DWORD wait, code;

    if (argc >= 3 && !strcmp(argv[2], "child"))
    {
        test_catalog_child();
        return;
    }
    /* The signing policy cannot be removed, so keep it out of the test runner. */
    GetModuleFileNameW(NULL, application, ARRAYSIZE(application));
    StringCchPrintfW(command, ARRAYSIZE(command), L"\"%s\" SystemImageCatalog child", application);
    success = CreateProcessW(application, command, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process);
    ok(success, "CreateProcess: %lu\n", GetLastError());
    if (!success) return;
    wait = WaitForSingleObject(process.hProcess, 30000);
    ok(wait == WAIT_OBJECT_0, "Child wait %lu\n", wait);
    if (wait != WAIT_OBJECT_0) TerminateProcess(process.hProcess, 1);
    else
    {
        GetExitCodeProcess(process.hProcess, &code);
        ok(code == 0, "Catalog child failed: %lu\n", code);
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
}
