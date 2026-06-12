/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite RTL string support API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestPrefix(VOID)
{
    UNICODE_STRING Full, Pre, Wrong, CaseDiff;

    RtlInitUnicodeString(&Full, L"\\Device\\HarddiskVolume1");
    RtlInitUnicodeString(&Pre, L"\\Device\\");
    RtlInitUnicodeString(&Wrong, L"\\Filesystem\\");
    RtlInitUnicodeString(&CaseDiff, L"\\DEVICE\\");

    ok_bool_true(RtlPrefixUnicodeString(&Pre, &Full, FALSE), "prefix exact");
    ok_bool_false(RtlPrefixUnicodeString(&Wrong, &Full, FALSE), "wrong prefix");
    ok_bool_false(RtlPrefixUnicodeString(&CaseDiff, &Full, FALSE), "case-sensitive prefix");
    ok_bool_true(RtlPrefixUnicodeString(&CaseDiff, &Full, TRUE), "case-insensitive prefix");
    ok_bool_false(RtlPrefixUnicodeString(&Full, &Pre, FALSE), "longer than string");
}

static
VOID
TestHashString(VOID)
{
    UNICODE_STRING String, Upper;
    ULONG Hash1, Hash2, Hash3;
    NTSTATUS Status;

    RtlInitUnicodeString(&String, L"ntoskrnl.exe");
    RtlInitUnicodeString(&Upper, L"NTOSKRNL.EXE");

    Status = RtlHashUnicodeString(&String, FALSE, HASH_STRING_ALGORITHM_DEFAULT, &Hash1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlHashUnicodeString(&Upper, FALSE, HASH_STRING_ALGORITHM_DEFAULT, &Hash2);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Hash1 != Hash2, "case-sensitive hashes equal: %lx\n", Hash1);

    Status = RtlHashUnicodeString(&String, TRUE, HASH_STRING_ALGORITHM_DEFAULT, &Hash3);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = RtlHashUnicodeString(&Upper, TRUE, HASH_STRING_ALGORITHM_DEFAULT, &Hash2);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_hex(Hash2, Hash3);

    Status = RtlHashUnicodeString(&String, FALSE, 0xFF, &Hash1);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = RtlHashUnicodeString(NULL, FALSE, HASH_STRING_ALGORITHM_DEFAULT, &Hash1);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
}

static
VOID
TestCrc32(VOID)
{
    static const UCHAR Data[] = "123456789";
    ULONG Crc;

    Crc = RtlComputeCrc32(0, Data, 9);
    ok_eq_hex(Crc, 0xCBF43926UL);

    Crc = RtlComputeCrc32(0, Data, 0);
    ok_eq_hex(Crc, 0UL);

    Crc = RtlComputeCrc32(RtlComputeCrc32(0, Data, 4), Data + 4, 5);
    ok_eq_hex(Crc, 0xCBF43926UL);
}

START_TEST(RtlStringSupportKM)
{
    TestPrefix();
    TestHashString();
    TestCrc32();
}
