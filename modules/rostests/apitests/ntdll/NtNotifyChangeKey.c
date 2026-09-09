/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Synchronous registry notification filtering and cancellation
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <windows.h>
#include <wchar.h>
#include <winternl.h>
#include <wine/test.h>

#define CHECK(expression) ok((expression), "%s failed, error %lu\n", #expression, GetLastError())

typedef NTSTATUS(NTAPI *NotifyFn)(HANDLE, HANDLE, PVOID, PVOID, PIO_STATUS_BLOCK, ULONG, BOOLEAN, PVOID, ULONG, BOOLEAN);
static NotifyFn notify;
struct notify_request
{
    HKEY key;
    ULONG filter;
    BOOLEAN tree;
    NTSTATUS status;
    IO_STATUS_BLOCK iosb;
    BOOL win32;
};
static DWORD WINAPI waiter(void *p)
{
    struct notify_request *r = (struct notify_request *)p;
    r->iosb.Status = (NTSTATUS)0xdeadbeef;
    r->iosb.Information = 0xdeadbeef;
    if (r->win32)
    {
        r->status = RegNotifyChangeKeyValue(r->key, r->tree, r->filter, NULL, FALSE);
        return 0;
    }
    r->status = notify(r->key, 0, 0, 0, &r->iosb, r->filter, r->tree, 0, 0, FALSE);
    return 0;
}
START_TEST(NtNotifyChangeKey)
{
    WCHAR path[96];
    _snwprintf(path, sizeof(path) / sizeof(path[0]), L"Software\\ReactOSNotifyTest_%lu", GetCurrentProcessId());
    notify = (NotifyFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtNotifyChangeKey");
    CHECK(notify != 0);
    if (!notify)
        return;
    for (unsigned which = 0; which < 10; which++)
    {
        HKEY key = 0, child = 0;
        CHECK(RegCreateKeyExW(HKEY_CURRENT_USER, path, 0, 0, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, 0, &key, 0) == 0);
        CHECK(RegCreateKeyExW(key, L"Child", 0, 0, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, 0, &child, 0) == 0);
        struct notify_request r = {key, (ULONG)(which == 2 ? REG_NOTIFY_CHANGE_NAME : REG_NOTIFY_CHANGE_LAST_SET), (BOOLEAN)(which == 1), 0, {{0}, 0}, which >= 8};
        HANDLE thread = CreateThread(0, 0, waiter, &r, 0, 0);
        CHECK(thread != 0);
        Sleep(200);
        CHECK(WaitForSingleObject(thread, 0) == WAIT_TIMEOUT);
        DWORD value = 1;
        if (which == 0 || which == 1 || which == 8)
            CHECK(RegSetValueExW(which == 1 ? child : key, L"Value", 0, REG_DWORD, (BYTE *)&value, 4) == 0);
        else if (which == 2)
        {
            HKEY added = 0;
            CHECK(RegCreateKeyExW(key, L"Added", 0, 0, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, 0, &added, 0) == 0);
            if (added)
                RegCloseKey(added);
            CHECK(RegDeleteKeyW(key, L"Added") == 0);
        }
        else if (which == 3 || which == 9)
        {
            CHECK(RegCloseKey(key) == 0);
            key = 0;
        }
        else if (which == 4)
        {
            CHECK(RegCloseKey(child) == 0);
            child = 0;
            CHECK(RegDeleteKeyW(key, L"Child") == 0);
            CHECK(RegDeleteKeyW(HKEY_CURRENT_USER, path) == 0);
        }
        else if (which == 5)
        {
            CHECK(RegSetValueExW(child, L"Value", 0, REG_DWORD, (BYTE *)&value, 4) == 0);
            CHECK(WaitForSingleObject(thread, 100) == WAIT_TIMEOUT);
            CHECK(RegSetValueExW(key, L"Value", 0, REG_DWORD, (BYTE *)&value, 4) == 0);
        }
        else if (which == 6)
        {
            HKEY added = 0;
            CHECK(RegCreateKeyExW(key, L"Added", 0, 0, REG_OPTION_VOLATILE, KEY_ALL_ACCESS, 0, &added, 0) == 0);
            if (added)
                RegCloseKey(added);
            CHECK(WaitForSingleObject(thread, 100) == WAIT_TIMEOUT);
            CHECK(RegDeleteKeyW(key, L"Added") == 0);
            CHECK(RegSetValueExW(key, L"Value", 0, REG_DWORD, (BYTE *)&value, 4) == 0);
        }
        else
            CHECK(TerminateThread(thread, 77));
        DWORD wait = WaitForSingleObject(thread, 5000);
        CHECK(wait == WAIT_OBJECT_0);
        if (wait == WAIT_OBJECT_0 && which >= 8)
            CHECK(r.status == ERROR_SUCCESS);
        if (wait == WAIT_OBJECT_0 && which < 7)
        {
            NTSTATUS expected = (which == 3 || which == 4) ? 0x10b : 0x10c;
            ok(r.status == expected, "case %u: status %#lx, expected %#lx\n", which, r.status, expected);
            ok(r.iosb.Status == expected, "case %u: IOSB status %#lx, expected %#lx\n", which, r.iosb.Status, expected);
            CHECK(r.iosb.Information == 0);
        }
        if (wait != 0)
        {
            TerminateThread(thread, 1);
            WaitForSingleObject(thread, 1000);
        }
        CloseHandle(thread);
        if (child)
        {
            RegCloseKey(child);
            if (key)
                RegDeleteKeyW(key, L"Child");
        }
        if (key)
            RegCloseKey(key);
        RegDeleteKeyW(HKEY_CURRENT_USER, path);
    }
}
