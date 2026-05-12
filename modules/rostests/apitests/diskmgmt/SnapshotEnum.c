/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for diskmgmt disk-enumeration parsing helpers.
 */

#include <apitest.h>

#include "snapshot.h"
#include "snapshotenum.h"

static void
TestParsePhysicalDriveNumber(void)
{
    ULONG DiskNumber;

    ok(DmSnapshotEnumParsePhysicalDriveNumber(L"PhysicalDrive0", &DiskNumber),
       "PhysicalDrive0 should parse\n");
    ok_int(DiskNumber, 0);

    ok(DmSnapshotEnumParsePhysicalDriveNumber(L"physicaldrive42", &DiskNumber),
       "Case-insensitive physical drive names should parse\n");
    ok_int(DiskNumber, 42);

    ok(!DmSnapshotEnumParsePhysicalDriveNumber(L"PhysicalDrive", &DiskNumber),
       "Missing suffix should fail\n");
    ok(!DmSnapshotEnumParsePhysicalDriveNumber(L"CdRom0", &DiskNumber),
       "Non-disk DOS device names should fail\n");
    ok(!DmSnapshotEnumParsePhysicalDriveNumber(L"PhysicalDriveX", &DiskNumber),
       "Non-numeric suffixes should fail\n");
}

static void
TestParseDiskNumbersFiltersSortsAndDeduplicates(void)
{
    static const WCHAR DeviceList[] =
        L"CdRom0\0"
        L"PhysicalDrive9\0"
        L"PhysicalDrive2\0"
        L"PhysicalDrive9\0"
        L"PhysicalDrive10\0"
        L"PhysicalDriveX\0"
        L"\0";
    PULONG DiskNumbers;
    ULONG DiskCount;

    DiskNumbers = NULL;
    DiskCount = 0;
    ok(DmSnapshotEnumParseDiskNumbers(DeviceList, &DiskNumbers, &DiskCount),
       "Parsing the DOS device list should succeed\n");
    ok_int(DiskCount, 3);
    ok(DiskNumbers != NULL, "Expected a disk number array\n");
    if (DiskNumbers != NULL && DiskCount == 3)
    {
        ok_int(DiskNumbers[0], 2);
        ok_int(DiskNumbers[1], 9);
        ok_int(DiskNumbers[2], 10);
    }

    HeapFree(GetProcessHeap(), 0, DiskNumbers);
}

static void
TestParseDiskNumbersAllowsNoMatches(void)
{
    static const WCHAR DeviceList[] =
        L"CdRom0\0"
        L"HarddiskVolume1\0"
        L"\0";
    PULONG DiskNumbers;
    ULONG DiskCount;

    DiskNumbers = (PULONG)(ULONG_PTR)0x1;
    DiskCount = 99;
    ok(DmSnapshotEnumParseDiskNumbers(DeviceList, &DiskNumbers, &DiskCount),
       "Parsing a list with no physical drives should still succeed\n");
    ok_int(DiskCount, 0);
    ok(DiskNumbers == NULL, "No matches should leave the array pointer NULL\n");
}

START_TEST(SnapshotEnum)
{
    TestParsePhysicalDriveNumber();
    TestParseDiskNumbersFiltersSortsAndDeduplicates();
    TestParseDiskNumbersAllowsNoMatches();
}
