/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite RTL GUID string API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static const GUID TestGuid = { 0x12345678, 0x9ABC, 0xDEF0, { 0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0 } };

START_TEST(RtlGuidKM)
{
    UNICODE_STRING String;
    UNICODE_STRING Expected;
    GUID Parsed;
    NTSTATUS Status;

    RtlInitUnicodeString(&Expected, L"{12345678-9abc-def0-1234-56789abcdef0}");

    Status = RtlStringFromGUID(&TestGuid, &String);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (NT_SUCCESS(Status))
    {
        ok_eq_ulong((ULONG)String.Length, 38UL * sizeof(WCHAR));
        ok(RtlEqualUnicodeString(&String, &Expected, TRUE), "GUID string %wZ\n", &String);

        RtlZeroMemory(&Parsed, sizeof(Parsed));
        Status = RtlGUIDFromString(&String, &Parsed);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(RtlCompareMemory(&Parsed, &TestGuid, sizeof(GUID)) == sizeof(GUID), "roundtrip GUID mismatch\n");

        RtlFreeUnicodeString(&String);
    }

    RtlInitUnicodeString(&Expected, L"{12345678-9abc-def0-1234-56789abcdef0");
    Status = RtlGUIDFromString(&Expected, &Parsed);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    RtlInitUnicodeString(&Expected, L"12345678-9abc-def0-1234-56789abcdef0}");
    Status = RtlGUIDFromString(&Expected, &Parsed);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    RtlInitUnicodeString(&Expected, L"{12345678-9abc-def0-1234-56789abcdeXX}");
    Status = RtlGUIDFromString(&Expected, &Parsed);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}
