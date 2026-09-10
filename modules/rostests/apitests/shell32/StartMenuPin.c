/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed Arif
 *
 * Public shell pin contract probe. Only a private temporary executable and
 * shortcuts created by this test are pinned or removed. No application launches.
 */

#define COBJMACROS
#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <knownfolders.h>
#include <strsafe.h>

#ifdef PIN_TEST_STANDALONE
#include <stdio.h>
#include <stdarg.h>
static unsigned failures;
static void check(BOOL success, unsigned line, const char *format, ...)
{
    va_list args;
    if (success)
        return;
    ++failures;
    printf("FAIL line %u: ", line);
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
}
#define ok(condition, ...) check(!!(condition), __LINE__, __VA_ARGS__)
#define START_TEST(name) static void test_##name(void)
#else
#include <apitest.h>
#endif

static HWND contextOwner;

#define REQUIRE(call) do { hr = (call); ok(SUCCEEDED(hr), "%s returned %08lx\n", #call, hr); if (FAILED(hr)) goto cleanup; } while (0)

static HRESULT CreateLink(PCWSTR target, PCWSTR path, PCWSTR directory)
{
    IShellLinkW *link = NULL;
    IPersistFile *file = NULL;
    HRESULT hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link);
    if (SUCCEEDED(hr))
        hr = IShellLinkW_SetPath(link, target);
    if (SUCCEEDED(hr))
        hr = IShellLinkW_SetArguments(link, L"--pin-contract \"argument with spaces\"");
    if (SUCCEEDED(hr))
        hr = IShellLinkW_SetWorkingDirectory(link, directory);
    if (SUCCEEDED(hr))
        hr = IShellLinkW_SetIconLocation(link, target, 0);
    if (SUCCEEDED(hr))
        hr = IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&file);
    if (SUCCEEDED(hr))
        hr = IPersistFile_Save(file, path, TRUE);
    if (file) IPersistFile_Release(file);
    if (link) IShellLinkW_Release(link);
    return hr;
}

static HRESULT CreatePinContext(PCIDLIST_ABSOLUTE pidl, IContextMenu **context, HMENU *menu, UINT *commands)
{
    IShellFolder *parent = NULL;
    PCUITEMID_CHILD child = NULL;
    HRESULT hr = SHBindToParent(pidl, &IID_IShellFolder, (void **)&parent, &child);
    *context = NULL;
    *menu = NULL;
    *commands = 0;
    if (SUCCEEDED(hr))
        hr = IShellFolder_GetUIObjectOf(parent, contextOwner, 1, &child, &IID_IContextMenu, NULL, (void **)context);
    if (SUCCEEDED(hr))
    {
        *menu = CreatePopupMenu();
        hr = *menu ? IContextMenu_QueryContextMenu(*context, *menu, 0, 41, 255, CMF_NORMAL) : E_OUTOFMEMORY;
        if (SUCCEEDED(hr))
            *commands = HRESULT_CODE(hr);
    }
    if (parent) IShellFolder_Release(parent);
    return hr;
}

static UINT FindVerb(IContextMenu *context, UINT commands, PCWSTR verb)
{
    UINT i;
    for (i = 0; i < commands; ++i)
    {
        WCHAR buffer[64];
        if (SUCCEEDED(IContextMenu_GetCommandString(context, i, GCS_VERBW, NULL, (LPSTR)buffer, ARRAYSIZE(buffer))) && !lstrcmpiW(buffer, verb))
            return i;
    }
    return UINT_MAX;
}

static HRESULT Invoke(IContextMenu *context, UINT offset, PCSTR verb, PCWSTR wideVerb)
{
    CMINVOKECOMMANDINFOEX info = { 0 };
    info.cbSize = sizeof(info);
    info.fMask = CMIC_MASK_UNICODE | CMIC_MASK_FLAG_NO_UI;
    info.hwnd = contextOwner;
    info.lpVerb = verb ? verb : MAKEINTRESOURCEA(offset);
    info.lpVerbW = wideVerb ? wideVerb : MAKEINTRESOURCEW(offset);
    info.nShow = SW_SHOWNORMAL;
    return IContextMenu_InvokeCommand(context, (LPCMINVOKECOMMANDINFO)&info);
}

static UINT CountPins(PCWSTR target, PCWSTR directory)
{
    PIDLIST_ABSOLUTE folderPidl = NULL;
    WCHAR folder[MAX_PATH], pattern[MAX_PATH], path[MAX_PATH], buffer[MAX_PATH];
    WIN32_FIND_DATAW data;
    HANDLE find;
    UINT count = 0;
    HRESULT hr = SHGetKnownFolderIDList(&FOLDERID_UserPinned, KF_FLAG_DONT_VERIFY, NULL, &folderPidl);
    ok(SUCCEEDED(hr), "Pin folder: %08lx\n", hr);
    if (FAILED(hr))
        return UINT_MAX;
    BOOL gotPath = SHGetPathFromIDListW(folderPidl, folder);
    CoTaskMemFree(folderPidl);
    ok(gotPath, "Pin folder has no filesystem path\n");
    if (!gotPath)
        return UINT_MAX;
    hr = StringCchPrintfW(pattern, ARRAYSIZE(pattern), L"%s\\TaskBar\\*.lnk", folder);
    find = SUCCEEDED(hr) ? FindFirstFileW(pattern, &data) : INVALID_HANDLE_VALUE;
    if (find != INVALID_HANDLE_VALUE)
    {
        do
        {
            IShellLinkW *link = NULL;
            IPersistFile *file = NULL;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
                continue;
            hr = StringCchPrintfW(path, ARRAYSIZE(path), L"%s\\TaskBar\\%s", folder, data.cFileName);
            if (SUCCEEDED(hr))
                hr = CoCreateInstance(&CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, &IID_IShellLinkW, (void **)&link);
            if (SUCCEEDED(hr))
                hr = IShellLinkW_QueryInterface(link, &IID_IPersistFile, (void **)&file);
            if (SUCCEEDED(hr))
                hr = IPersistFile_Load(file, path, STGM_READ);
            if (SUCCEEDED(hr))
                hr = IShellLinkW_GetPath(link, buffer, ARRAYSIZE(buffer), NULL, SLGP_UNCPRIORITY);
            if (SUCCEEDED(hr) && !lstrcmpiW(target, buffer))
            {
                INT icon;
                ++count;
                hr = IShellLinkW_GetArguments(link, buffer, ARRAYSIZE(buffer));
                ok(SUCCEEDED(hr) && !lstrcmpW(buffer, L"--pin-contract \"argument with spaces\""), "Pin lost shortcut arguments\n");
                hr = IShellLinkW_GetWorkingDirectory(link, buffer, ARRAYSIZE(buffer));
                ok(SUCCEEDED(hr) && !lstrcmpiW(buffer, directory), "Pin lost working directory\n");
                hr = IShellLinkW_GetIconLocation(link, buffer, ARRAYSIZE(buffer), &icon);
                ok(SUCCEEDED(hr) && !lstrcmpiW(buffer, target) && icon == 0, "Pin lost icon location\n");
            }
            if (file) IPersistFile_Release(file);
            if (link) IShellLinkW_Release(link);
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    return count;
}

START_TEST(StartMenuPin)
{
    WCHAR temp[MAX_PATH], directory[MAX_PATH] = L"", target[MAX_PATH] = L"";
    WCHAR shortcut[MAX_PATH] = L"", module[MAX_PATH], verb[64], resolved[MAX_PATH] = L"";
    PWSTR displayName = NULL;
    BOOL haveDisplayName = FALSE;
    PIDLIST_ABSOLUTE pidl = NULL;
    IStartMenuPinnedList *pins = NULL;
    IShellItem *item = NULL;
    IContextMenu *first = NULL, *stale = NULL, *unpin = NULL;
    IShellMenu *menuband = NULL;
    HMENU firstMenu = NULL, staleMenu = NULL, unpinMenu = NULL;
    UINT commands, firstId, staleId, unpinId;
    HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    ok(SUCCEEDED(hr), "CoInitializeEx: %08lx\n", hr);
    if (FAILED(hr))
        return;
    contextOwner = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"Pin contract owner", WS_POPUP, 0, 0, 10, 10, NULL, NULL, GetModuleHandleW(NULL), NULL);
    ok(contextOwner != NULL, "Creating context owner failed: %lu\n", GetLastError());
    if (!contextOwner) goto cleanup;

    // A bad new registration resource must not prevent later shell classes registering.
    hr = CoCreateInstance(&CLSID_MenuBand, NULL, CLSCTX_INPROC_SERVER, &IID_IShellMenu, (void **)&menuband);
    ok(SUCCEEDED(hr), "MenuBand registration: %08lx\n", hr);
    if (menuband) IShellMenu_Release(menuband);
    REQUIRE(CoCreateInstance(&CLSID_StartMenuPin, NULL, CLSCTX_INPROC_SERVER, &IID_IStartMenuPinnedList, (void **)&pins));
    if (!GetTempPathW(ARRAYSIZE(temp), temp) || !GetTempFileNameW(temp, L"pin", 0, directory))
    {
        ok(FALSE, "Creating temporary name failed: %lu\n", GetLastError());
        goto cleanup;
    }
    DeleteFileW(directory);
    if (!CreateDirectoryW(directory, NULL))
    {
        ok(FALSE, "Creating temporary directory failed: %lu\n", GetLastError());
        directory[0] = 0;
        goto cleanup;
    }
    REQUIRE(StringCchPrintfW(target, ARRAYSIZE(target), L"%s\\pin-target.exe", directory));
    REQUIRE(StringCchPrintfW(shortcut, ARRAYSIZE(shortcut), L"%s\\Pin contract.lnk", directory));
    if (!GetModuleFileNameW(NULL, module, ARRAYSIZE(module)) || !CopyFileW(module, target, TRUE))
    {
        ok(FALSE, "Copying private test executable failed: %lu\n", GetLastError());
        goto cleanup;
    }
    REQUIRE(CreateLink(target, shortcut, directory));
    REQUIRE(SHParseDisplayName(shortcut, NULL, &pidl, 0, NULL));
    REQUIRE(SHCreateItemFromParsingName(shortcut, NULL, &IID_IShellItem, (void **)&item));
    ok(SHGetPathFromIDListW(pidl, resolved) && !lstrcmpiW(shortcut, resolved), "Shortcut PIDL roundtrip: %ls -> %ls\n", shortcut, resolved);
    REQUIRE(IShellItem_GetDisplayName(item, SIGDN_FILESYSPATH, &displayName));
    haveDisplayName = TRUE;
    ok(!lstrcmpiW(shortcut, displayName), "Shell item path: %ls -> %ls\n", shortcut, displayName);
    hr = IStartMenuPinnedList_RemoveFromList(pins, item);
    ok(hr == S_OK, "Already unpinned item: %08lx\n", hr);
    REQUIRE(CreatePinContext(pidl, &first, &firstMenu, &commands));
    firstId = FindVerb(first, commands, L"taskbarpin");
    ok(firstId != UINT_MAX, "Pin verb absent from filesystem context menu\n");
    if (firstId == UINT_MAX) goto cleanup;
    REQUIRE(CreatePinContext(pidl, &stale, &staleMenu, &commands));
    staleId = FindVerb(stale, commands, L"taskbarpin");
    ok(staleId != UINT_MAX, "Second pin verb absent\n");
    if (staleId == UINT_MAX) goto cleanup;

    REQUIRE(Invoke(first, firstId, NULL, NULL));
    ok(CountPins(target, directory) == 1, "First pin did not create exactly one shortcut\n");
    REQUIRE(Invoke(stale, staleId, NULL, NULL));
    ok(CountPins(target, directory) == 1, "Stale Pin command toggled or duplicated the pin\n");
    REQUIRE(IContextMenu_GetCommandString(first, firstId, GCS_VERBW, NULL, (LPSTR)verb, ARRAYSIZE(verb)));
    ok(!lstrcmpW(verb, L"taskbarpin"), "Canonical verb changed while its menu was open\n");
    REQUIRE(IContextMenu_GetCommandString(first, firstId, GCS_VALIDATEW, NULL, NULL, 0));
    REQUIRE(Invoke(first, firstId, "taskbarpin", NULL));
    ok(CountPins(target, directory) == 1, "Canonical Pin command toggled or duplicated the pin\n");

    REQUIRE(CreatePinContext(pidl, &unpin, &unpinMenu, &commands));
    unpinId = FindVerb(unpin, commands, L"taskbarunpin");
    ok(unpinId != UINT_MAX, "Unpin verb absent after pinning\n");
    if (unpinId == UINT_MAX) goto cleanup;
    REQUIRE(IStartMenuPinnedList_RemoveFromList(pins, item));
    ok(CountPins(target, directory) == 0, "RemoveFromList left a taskbar pin\n");
    hr = IStartMenuPinnedList_RemoveFromList(pins, item);
    ok(hr == S_OK, "Second removal: %08lx\n", hr);
    REQUIRE(Invoke(unpin, unpinId, NULL, NULL));
    ok(CountPins(target, directory) == 0, "Stale Unpin command pinned the app again\n");
    REQUIRE(Invoke(first, firstId, "taskbarpin", NULL));
    REQUIRE(Invoke(unpin, unpinId, "invalid-ansi-verb", L"taskbarunpin"));
    ok(CountPins(target, directory) == 0, "Unicode unpin did not remove the pin\n");
    ok(GetFileAttributesW(shortcut) != INVALID_FILE_ATTRIBUTES, "Unpin deleted the source shortcut\n");
    ok(GetFileAttributesW(target) != INVALID_FILE_ATTRIBUTES, "Unpin deleted the executable\n");

cleanup:
    if (pins && item) IStartMenuPinnedList_RemoveFromList(pins, item);
    if (unpinMenu) DestroyMenu(unpinMenu);
    if (staleMenu) DestroyMenu(staleMenu);
    if (firstMenu) DestroyMenu(firstMenu);
    if (unpin) IContextMenu_Release(unpin);
    if (stale) IContextMenu_Release(stale);
    if (first) IContextMenu_Release(first);
    if (item) IShellItem_Release(item);
    if (pins) IStartMenuPinnedList_Release(pins);
    CoTaskMemFree(pidl);
    if (haveDisplayName) CoTaskMemFree(displayName);
    if (shortcut[0]) DeleteFileW(shortcut);
    if (target[0]) DeleteFileW(target);
    if (directory[0]) RemoveDirectoryW(directory);
    if (contextOwner) DestroyWindow(contextOwner);
    contextOwner = NULL;
    CoUninitialize();
}

#ifdef PIN_TEST_STANDALONE
#ifdef PIN_TEST_NO_CRT
void mainCRTStartup(void)
{
    test_StartMenuPin();
    printf("START_MENU_PIN_PROBE_DONE failures=%u\n", failures);
    ExitProcess(failures ? 1 : 0);
}
#else
int main(void)
{
    test_StartMenuPin();
    printf("START_MENU_PIN_PROBE_DONE failures=%u\n", failures);
    return failures ? 1 : 0;
}
#endif
#endif
