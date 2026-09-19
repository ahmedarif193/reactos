/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     NtOpenKeyEx syscall relocation, probing and symbolic-link opens
 */
#include "precomp.h"
#include <winreg.h>

typedef NTSTATUS (NTAPI *OPEN_KEY_EX)(PHANDLE, ACCESS_MASK, POBJECT_ATTRIBUTES, ULONG);

START_TEST(NtOpenKeyEx)
{
    OPEN_KEY_EX open_key = (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtOpenKeyEx");
    UNICODE_STRING name, current_user, value_name;
    OBJECT_ATTRIBUTES attributes;
    NTSTATUS status;
    HANDLE key = NULL, parent = NULL, target = NULL, link = NULL;
    WCHAR path[512], target_path[544];
    BYTE value[2048];
    ULONG result_length, disposition;

    if (!open_key)
    {
        skip("NtOpenKeyEx unavailable\n");
        return;
    }
    RtlInitUnicodeString(&name, L"\\Registry\\Machine\\SOFTWARE");
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = open_key(&key, KEY_READ, &attributes, 0);
    ok_ntstatus(status, STATUS_SUCCESS);
    if (NT_SUCCESS(status)) NtClose(key);

#if defined(_M_ARM64)
    /* Sandboxes relocate these complete service veneers into their original-
     * call trampolines. A C wrapper cannot be relocated this way. */
    {
        ULONG code[4];
        PVOID thunk;
        DWORD old_protect;
        memcpy(code, (void *)open_key, sizeof(code));
        ok_hex(code[0] & ~0x001fffe0, 0xd4000001); /* svc #service */
        ok_hex(code[1], 0xd65f03c0);               /* ret */
        ok_hex(code[2], 0);
        ok_hex(code[3], 0);
        if ((code[0] & ~0x001fffe0) == 0xd4000001 && code[1] == 0xd65f03c0 &&
            !code[2] && !code[3])
        {
            thunk = VirtualAlloc(NULL, sizeof(code), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
            ok(thunk != NULL, "allocate thunk: %lu\n", GetLastError());
            if (thunk)
            {
                memcpy(thunk, code, sizeof(code));
                if (VirtualProtect(thunk, sizeof(code), PAGE_EXECUTE_READ, &old_protect))
                {
                    FlushInstructionCache(GetCurrentProcess(), thunk, sizeof(code));
                    status = ((OPEN_KEY_EX)thunk)(&key, KEY_READ, &attributes, 0);
                    ok_ntstatus(status, STATUS_SUCCESS);
                    if (NT_SUCCESS(status)) NtClose(key);
                }
                else ok(0, "protect thunk: %lu\n", GetLastError());
                VirtualFree(thunk, 0, MEM_RELEASE);
            }
        }
    }
#endif
    status = open_key(NULL, KEY_READ, &attributes, 0);
    ok_ntstatus(status, STATUS_ACCESS_VIOLATION);
    /* The extended call must retain NtOpenKey's null-attributes behavior. */
    {
        NTSTATUS basic_status = NtOpenKey(&key, KEY_READ, NULL);
        status = open_key(&key, KEY_READ, NULL, 0);
        ok(!NT_SUCCESS(status), "NULL attributes unexpectedly accepted\n");
        ok_ntstatus(status, basic_status);
    }

    status = RtlFormatCurrentUserKeyPath(&current_user);
    ok_ntstatus(status, STATUS_SUCCESS);
    if (!NT_SUCCESS(status)) return;
    StringCchPrintfW(path, ARRAYSIZE(path), L"%wZ\\NtOpenKeyEx_%lu", &current_user, GetCurrentProcessId());
    RtlFreeUnicodeString(&current_user);
    RtlInitUnicodeString(&name, path);
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, NULL, NULL);
    status = NtCreateKey(&parent, KEY_ALL_ACCESS, &attributes, 0, NULL,
                         REG_OPTION_VOLATILE, &disposition);
    ok_ntstatus(status, STATUS_SUCCESS);
    if (!NT_SUCCESS(status)) return;
    ok(disposition == REG_CREATED_NEW_KEY, "Test key already exists\n");
    if (disposition != REG_CREATED_NEW_KEY) { NtClose(parent); return; }

    RtlInitUnicodeString(&name, L"Target");
    InitializeObjectAttributes(&attributes, &name, OBJ_CASE_INSENSITIVE, parent, NULL);
    status = NtCreateKey(&target, KEY_ALL_ACCESS, &attributes, 0, NULL, REG_OPTION_VOLATILE, NULL);
    ok_ntstatus(status, STATUS_SUCCESS);
    if (!NT_SUCCESS(status)) goto cleanup;
    RtlInitUnicodeString(&name, L"Link");
    status = NtCreateKey(&link, KEY_ALL_ACCESS, &attributes, 0, NULL,
                         REG_OPTION_VOLATILE | REG_OPTION_CREATE_LINK, NULL);
    ok_ntstatus(status, STATUS_SUCCESS);
    if (!NT_SUCCESS(status)) goto cleanup;
    StringCchPrintfW(target_path, ARRAYSIZE(target_path), L"%s\\Target", path);
    RtlInitUnicodeString(&value_name, L"SymbolicLinkValue");
    status = NtSetValueKey(link, &value_name, 0, REG_LINK, target_path,
                           wcslen(target_path) * sizeof(WCHAR));
    ok_ntstatus(status, STATUS_SUCCESS);
    if (!NT_SUCCESS(status)) goto cleanup;

    /* Repeat to exercise both newly parsed and cached link keys. */
    for (unsigned int pass = 0; pass < 2; ++pass)
    {
        status = open_key(&key, KEY_QUERY_VALUE, &attributes, REG_OPTION_OPEN_LINK);
        ok_ntstatus(status, STATUS_SUCCESS);
        if (NT_SUCCESS(status))
        {
            status = NtQueryValueKey(key, &value_name, KeyValuePartialInformation,
                                     value, sizeof(value), &result_length);
            ok_ntstatus(status, STATUS_SUCCESS);
            NtClose(key);
        }
        status = open_key(&key, KEY_QUERY_VALUE, &attributes, 0);
        ok_ntstatus(status, STATUS_SUCCESS);
        if (NT_SUCCESS(status))
        {
            status = NtQueryValueKey(key, &value_name, KeyValuePartialInformation,
                                     value, sizeof(value), &result_length);
            ok_ntstatus(status, STATUS_OBJECT_NAME_NOT_FOUND);
            NtClose(key);
        }
    }
cleanup:
    if (link) { NtDeleteKey(link); NtClose(link); }
    if (target) { NtDeleteKey(target); NtClose(target); }
    NtDeleteKey(parent);
    NtClose(parent);
}
