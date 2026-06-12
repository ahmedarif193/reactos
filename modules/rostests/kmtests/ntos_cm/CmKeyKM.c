/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite ZwOpenKey/ZwQueryValueKey API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'kCmK'

START_TEST(CmKeyKM)
{
    UNICODE_STRING Name, ValueName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle = NULL;
    PKEY_VALUE_PARTIAL_INFORMATION Partial;
    UCHAR Buffer[512];
    ULONG ResultLength = 0;
    NTSTATUS Status;

    RtlInitUnicodeString(&Name, L"\\Registry\\Machine\\SYSTEM\\CurrentControlSet\\Control");
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);

    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status)) return;

    RtlInitUnicodeString(&ValueName, L"SystemStartOptions");
    Partial = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, Partial, sizeof(Buffer), &ResultLength);
    ok(Status == STATUS_SUCCESS || Status == STATUS_BUFFER_OVERFLOW, "SystemStartOptions status %lx\n", Status);
    if (NT_SUCCESS(Status))
        ok_eq_ulong(Partial->Type, (ULONG)REG_SZ);

    RtlInitUnicodeString(&ValueName, L"KmtNoSuchValue");
    Status = ZwQueryValueKey(KeyHandle, &ValueName, KeyValuePartialInformation, Partial, sizeof(Buffer), &ResultLength);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);

    Status = ZwClose(KeyHandle);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlInitUnicodeString(&Name, L"\\Registry\\Machine\\SYSTEM\\KmtNoSuchKey");
    InitializeObjectAttributes(&ObjectAttributes, &Name, OBJ_KERNEL_HANDLE | OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    ok_eq_hex(Status, STATUS_OBJECT_NAME_NOT_FOUND);
}
