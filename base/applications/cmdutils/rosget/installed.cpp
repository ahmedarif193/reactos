/*
 * PROJECT:     ReactOS package manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Installed applications manager used by the rosget GUI
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#define NOMINMAX
#include <windows.h>
#include <commctrl.h>

#include "installed.hpp"
#include "resource.h"
#include "util.hpp"

#include <algorithm>
#include <cwctype>
#include <memory>
#include <set>

namespace rosget
{

namespace
{

constexpr wchar_t UninstallKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

enum : int
{
    IdInstalledList = 2001,
    IdInstalledStatus,
    IdUninstall,
    IdInstalledRefresh,
    IdInstalledClose,
};

struct InstalledApp
{
    std::wstring name;
    std::wstring version;
    std::wstring publisher;
    std::wstring location;
    std::wstring uninstall;
    unsigned long long estimatedBytes = 0;
};

struct InstalledWindow
{
    HWND window = nullptr;
    HWND owner = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HWND uninstall = nullptr;
    HWND refresh = nullptr;
    HWND close = nullptr;
    HFONT font = nullptr;
    std::vector<InstalledApp> apps;
};

bool ReadString(HKEY key, const wchar_t *name, std::wstring &value)
{
    DWORD type = 0;
    DWORD bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes < sizeof(wchar_t))
    {
        return false;
    }
    std::vector<wchar_t> buffer(bytes / sizeof(wchar_t) + 1);
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(buffer.data()), &bytes) != ERROR_SUCCESS)
        return false;
    buffer.back() = L'\0';
    value.assign(buffer.data());
    if (type == REG_EXPAND_SZ && !value.empty())
    {
        const DWORD needed = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (needed)
        {
            std::vector<wchar_t> expanded(needed);
            if (ExpandEnvironmentStringsW(value.c_str(), expanded.data(), needed))
                value.assign(expanded.data());
        }
    }
    return !value.empty();
}

DWORD ReadDword(HKEY key, const wchar_t *name)
{
    DWORD value = 0;
    DWORD type = 0;
    DWORD bytes = sizeof(value);
    return RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(&value), &bytes) == ERROR_SUCCESS &&
           type == REG_DWORD ? value : 0;
}

void EnumerateView(HKEY root, REGSAM view, std::vector<InstalledApp> &apps, std::set<std::wstring> &seen)
{
    HKEY uninstall = nullptr;
    if (RegOpenKeyExW(root, UninstallKey, 0, KEY_READ | view, &uninstall) != ERROR_SUCCESS)
        return;

    DWORD index = 0;
    for (;;)
    {
        wchar_t keyName[512]{};
        DWORD keyLength = ARRAYSIZE(keyName);
        const LONG enumeration = RegEnumKeyExW(uninstall, index++, keyName, &keyLength, nullptr, nullptr, nullptr, nullptr);
        if (enumeration == ERROR_NO_MORE_ITEMS)
            break;
        if (enumeration != ERROR_SUCCESS)
            continue;

        HKEY key = nullptr;
        if (RegOpenKeyExW(uninstall, keyName, 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;
        InstalledApp app;
        ReadString(key, L"DisplayName", app.name);
        if (app.name.empty() || ReadDword(key, L"SystemComponent"))
        {
            RegCloseKey(key);
            continue;
        }
        ReadString(key, L"DisplayVersion", app.version);
        ReadString(key, L"Publisher", app.publisher);
        ReadString(key, L"InstallLocation", app.location);
        if (!ReadString(key, L"UninstallString", app.uninstall))
            ReadString(key, L"QuietUninstallString", app.uninstall);
        app.estimatedBytes = static_cast<unsigned long long>(ReadDword(key, L"EstimatedSize")) * 1024;
        RegCloseKey(key);

        std::wstring identity = app.name + L"\n" + app.version + L"\n" + app.uninstall;
        std::transform(identity.begin(), identity.end(), identity.begin(),
                       [](wchar_t character) { return static_cast<wchar_t>(towlower(character)); });
        if (seen.insert(identity).second)
            apps.push_back(std::move(app));
    }
    RegCloseKey(uninstall);
}

std::vector<InstalledApp> EnumerateInstalledApps()
{
    std::vector<InstalledApp> apps;
    std::set<std::wstring> seen;
    const REGSAM views[] = {0, KEY_WOW64_32KEY, KEY_WOW64_64KEY};
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE})
    {
        for (REGSAM view : views)
            EnumerateView(root, view, apps, seen);
    }
    std::sort(apps.begin(), apps.end(), [](const InstalledApp &left, const InstalledApp &right) {
        return lstrcmpiW(left.name.c_str(), right.name.c_str()) < 0;
    });
    return apps;
}

std::wstring FormatInstalledSize(unsigned long long bytes)
{
    if (!bytes)
        return L"";
    if (bytes >= 1024ull * 1024 * 1024)
        return std::to_wstring(bytes / (1024ull * 1024 * 1024)) + L" GB";
    if (bytes >= 1024ull * 1024)
        return std::to_wstring(bytes / (1024ull * 1024)) + L" MB";
    return std::to_wstring(bytes / 1024) + L" KB";
}

void RefreshInstalled(InstalledWindow &state)
{
    state.apps = EnumerateInstalledApps();
    ListView_DeleteAllItems(state.list);
    for (std::size_t index = 0; index < state.apps.size(); ++index)
    {
        const InstalledApp &app = state.apps[index];
        LVITEMW item{};
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(index);
        item.pszText = const_cast<LPWSTR>(app.name.c_str());
        item.lParam = static_cast<LPARAM>(index);
        const int row = ListView_InsertItem(state.list, &item);
        ListView_SetItemText(state.list, row, 1, const_cast<LPWSTR>(app.version.c_str()));
        ListView_SetItemText(state.list, row, 2, const_cast<LPWSTR>(app.publisher.c_str()));
        std::wstring size = FormatInstalledSize(app.estimatedBytes);
        ListView_SetItemText(state.list, row, 3, const_cast<LPWSTR>(size.c_str()));
    }
    const std::wstring status = std::to_wstring(state.apps.size()) + L" installed applications";
    SetWindowTextW(state.status, status.c_str());
    EnableWindow(state.uninstall, FALSE);
}

void RunUninstaller(InstalledWindow &state)
{
    const int selected = ListView_GetNextItem(state.list, -1, LVNI_SELECTED);
    if (selected < 0 || static_cast<std::size_t>(selected) >= state.apps.size())
        return;
    const InstalledApp &app = state.apps[selected];
    if (app.uninstall.empty())
    {
        MessageBoxW(state.window, L"This application did not register an uninstall command.", app.name.c_str(),
                    MB_OK | MB_ICONINFORMATION);
        return;
    }
    const std::wstring prompt = L"Run the uninstall command registered by “" + app.name +
                                L"”?\n\nThe application publisher controls the uninstaller.";
    if (MessageBoxW(state.window, prompt.c_str(), L"Confirm uninstall", MB_OKCANCEL | MB_ICONWARNING) != IDOK)
        return;

    std::vector<wchar_t> command(app.uninstall.begin(), app.uninstall.end());
    command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, &process))
    {
        const DWORD error = GetLastError();
        const std::wstring message = L"Cannot start the registered uninstaller.\n\n" + WideFromUtf8(WindowsErrorMessage(error));
        MessageBoxW(state.window, message.c_str(), app.name.c_str(), MB_OK | MB_ICONERROR);
        return;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    SetWindowTextW(state.status, L"Uninstaller started. Press Refresh after it finishes.");
}

void LayoutInstalled(InstalledWindow &state)
{
    RECT client{};
    GetClientRect(state.window, &client);
    const int margin = 12;
    const int buttonWidth = 92;
    const int buttonHeight = 28;
    const int bottom = client.bottom - margin - buttonHeight;
    MoveWindow(state.list, margin, margin, client.right - margin * 2, std::max(80, bottom - margin * 2 - 24), TRUE);
    MoveWindow(state.status, margin, bottom - 22, std::max(80, static_cast<int>(client.right) - margin * 2), 20, TRUE);
    int right = client.right - margin;
    MoveWindow(state.close, right - buttonWidth, bottom, buttonWidth, buttonHeight, TRUE);
    right -= buttonWidth + 8;
    MoveWindow(state.refresh, right - buttonWidth, bottom, buttonWidth, buttonHeight, TRUE);
    right -= buttonWidth + 8;
    MoveWindow(state.uninstall, right - buttonWidth, bottom, buttonWidth, buttonHeight, TRUE);
}

LRESULT CALLBACK InstalledProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    InstalledWindow *state = reinterpret_cast<InstalledWindow *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE)
    {
        const CREATESTRUCTW *create = reinterpret_cast<const CREATESTRUCTW *>(lParam);
        state = static_cast<InstalledWindow *>(create->lpCreateParams);
        state->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }
    if (!state)
        return DefWindowProcW(window, message, wParam, lParam);

    switch (message)
    {
        case WM_CREATE:
        {
            HINSTANCE instance = GetModuleHandleW(nullptr);
            state->list = CreateWindowExW(WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"",
                                           WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                           0, 0, 10, 10, window, reinterpret_cast<HMENU>(IdInstalledList), instance, nullptr);
            ListView_SetExtendedListViewStyle(state->list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
            static const struct { const wchar_t *name; int width; } columns[] = {
                {L"Name", 260}, {L"Version", 110}, {L"Publisher", 220}, {L"Size", 90},
            };
            LVCOLUMNW column{};
            column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            for (int index = 0; index < 4; ++index)
            {
                column.iSubItem = index;
                column.pszText = const_cast<LPWSTR>(columns[index].name);
                column.cx = columns[index].width;
                ListView_InsertColumn(state->list, index, &column);
            }
            state->status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT,
                                             0, 0, 10, 10, window, reinterpret_cast<HMENU>(IdInstalledStatus), instance, nullptr);
            state->uninstall = CreateWindowExW(0, L"BUTTON", L"Uninstall", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_DISABLED,
                                                0, 0, 10, 10, window, reinterpret_cast<HMENU>(IdUninstall), instance, nullptr);
            state->refresh = CreateWindowExW(0, L"BUTTON", L"Refresh", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                              0, 0, 10, 10, window, reinterpret_cast<HMENU>(IdInstalledRefresh), instance, nullptr);
            state->close = CreateWindowExW(0, L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                            0, 0, 10, 10, window, reinterpret_cast<HMENU>(IdInstalledClose), instance, nullptr);
            NONCLIENTMETRICSW metrics{};
            metrics.cbSize = sizeof(metrics);
            if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics), &metrics, 0))
                state->font = CreateFontIndirectW(&metrics.lfMessageFont);
            for (HWND child : {state->list, state->status, state->uninstall, state->refresh, state->close})
                SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(state->font), TRUE);
            RefreshInstalled(*state);
            return 0;
        }
        case WM_SIZE:
            LayoutInstalled(*state);
            return 0;
        case WM_GETMINMAXINFO:
        {
            MINMAXINFO *info = reinterpret_cast<MINMAXINFO *>(lParam);
            info->ptMinTrackSize.x = 640;
            info->ptMinTrackSize.y = 380;
            return 0;
        }
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR *>(lParam)->idFrom == IdInstalledList &&
                reinterpret_cast<NMHDR *>(lParam)->code == LVN_ITEMCHANGED)
            {
                const int selected = ListView_GetNextItem(state->list, -1, LVNI_SELECTED);
                EnableWindow(state->uninstall, selected >= 0 &&
                             static_cast<std::size_t>(selected) < state->apps.size() &&
                             !state->apps[selected].uninstall.empty());
            }
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == IdUninstall)
                RunUninstaller(*state);
            else if (LOWORD(wParam) == IdInstalledRefresh)
                RefreshInstalled(*state);
            else if (LOWORD(wParam) == IdInstalledClose || LOWORD(wParam) == IDCANCEL)
                DestroyWindow(window);
            return 0;
        case WM_DESTROY:
            if (state->font)
                DeleteObject(state->font);
            state->font = nullptr;
            return 0;
        default:
            break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}

} // namespace

Status ShowInstalledAppsManager(HWND owner)
{
    HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.lpfnWndProc = InstalledProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_ROSGET));
    windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    windowClass.lpszClassName = L"RosGetInstalledApps";
    if (!RegisterClassExW(&windowClass) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return Status::Fail(GetLastError(), "cannot register the installed applications window");

    InstalledWindow state;
    state.owner = owner;
    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);
    HWND window = CreateWindowExW(WS_EX_DLGMODALFRAME, windowClass.lpszClassName, L"Installed apps — rosget",
                                  WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                  ownerRect.left + 36, ownerRect.top + 36, 780, 520,
                                  owner, nullptr, instance, &state);
    if (!window)
        return Status::Fail(GetLastError(), "cannot create the installed applications window");

    EnableWindow(owner, FALSE);
    MSG message{};
    int messageResult = 1;
    while (IsWindow(window) && (messageResult = GetMessageW(&message, nullptr, 0, 0)) > 0)
    {
        if (IsDialogMessageW(window, &message))
            continue;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    if (!messageResult)
        PostQuitMessage(static_cast<int>(message.wParam));
    return Status::Ok();
}

} // namespace rosget
