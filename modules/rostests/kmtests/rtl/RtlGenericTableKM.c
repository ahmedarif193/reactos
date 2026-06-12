/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite RTL_GENERIC_TABLE API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'tGmK'

static volatile LONG TableAllocs;
static volatile LONG TableFrees;

static
RTL_GENERIC_COMPARE_RESULTS
NTAPI
TableCompare(
    _In_ PRTL_GENERIC_TABLE Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct)
{
    ULONG A = *(PULONG)FirstStruct;
    ULONG B = *(PULONG)SecondStruct;

    UNREFERENCED_PARAMETER(Table);
    if (A < B) return GenericLessThan;
    if (A > B) return GenericGreaterThan;
    return GenericEqual;
}

static
PVOID
NTAPI
TableAllocate(
    _In_ PRTL_GENERIC_TABLE Table,
    _In_ CLONG ByteSize)
{
    UNREFERENCED_PARAMETER(Table);
    InterlockedIncrement(&TableAllocs);
    return ExAllocatePoolWithTag(NonPagedPool, ByteSize, TAG_TEST);
}

static
VOID
NTAPI
TableFree(
    _In_ PRTL_GENERIC_TABLE Table,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Table);
    InterlockedIncrement(&TableFrees);
    ExFreePoolWithTag(Buffer, TAG_TEST);
}

START_TEST(RtlGenericTableKM)
{
    RTL_GENERIC_TABLE Table;
    ULONG Values[] = { 30, 10, 50, 20, 40 };
    ULONG Probe;
    PULONG Found;
    BOOLEAN NewElement;
    BOOLEAN Deleted;
    PVOID Restart;
    ULONG i, Expected;

    TableAllocs = 0;
    TableFrees = 0;

    RtlInitializeGenericTable(&Table, TableCompare, TableAllocate, TableFree, NULL);
    ok_bool_true(RtlIsGenericTableEmpty(&Table), "fresh table empty");
    ok_eq_ulong(RtlNumberGenericTableElements(&Table), 0UL);

    for (i = 0; i < RTL_NUMBER_OF(Values); i++)
    {
        NewElement = FALSE;
        Found = RtlInsertElementGenericTable(&Table, &Values[i], sizeof(ULONG), &NewElement);
        ok(Found != NULL, "insert %lu failed\n", Values[i]);
        ok_bool_true(NewElement, "insert reported new");
        if (Found) ok_eq_ulong(*Found, Values[i]);
    }
    ok_eq_ulong(RtlNumberGenericTableElements(&Table), 5UL);
    ok_bool_false(RtlIsGenericTableEmpty(&Table), "table not empty");
    ok_eq_long(TableAllocs, 5L);

    Probe = 30;
    NewElement = TRUE;
    Found = RtlInsertElementGenericTable(&Table, &Probe, sizeof(ULONG), &NewElement);
    ok(Found != NULL, "duplicate insert returned NULL\n");
    ok_bool_false(NewElement, "duplicate reported new");
    ok_eq_ulong(RtlNumberGenericTableElements(&Table), 5UL);

    Probe = 20;
    Found = RtlLookupElementGenericTable(&Table, &Probe);
    ok(Found != NULL, "lookup 20 failed\n");
    if (Found) ok_eq_ulong(*Found, 20UL);

    Probe = 99;
    Found = RtlLookupElementGenericTable(&Table, &Probe);
    ok_eq_pointer(Found, NULL);

    Expected = 10;
    for (Found = RtlEnumerateGenericTable(&Table, TRUE);
         Found != NULL;
         Found = RtlEnumerateGenericTable(&Table, FALSE))
    {
        ok_eq_ulong(*Found, Expected);
        Expected += 10;
    }
    ok_eq_ulong(Expected, 60UL);

    Restart = NULL;
    Expected = 10;
    for (Found = RtlEnumerateGenericTableWithoutSplaying(&Table, &Restart);
         Found != NULL;
         Found = RtlEnumerateGenericTableWithoutSplaying(&Table, &Restart))
    {
        ok_eq_ulong(*Found, Expected);
        Expected += 10;
    }
    ok_eq_ulong(Expected, 60UL);

    Found = RtlGetElementGenericTable(&Table, 0);
    ok(Found != NULL, "GetElement 0 failed\n");
    Found = RtlGetElementGenericTable(&Table, 4);
    ok(Found != NULL, "GetElement 4 failed\n");
    Found = RtlGetElementGenericTable(&Table, 5);
    ok_eq_pointer(Found, NULL);

    Probe = 30;
    Deleted = RtlDeleteElementGenericTable(&Table, &Probe);
    ok_bool_true(Deleted, "delete 30");
    ok_eq_ulong(RtlNumberGenericTableElements(&Table), 4UL);
    Deleted = RtlDeleteElementGenericTable(&Table, &Probe);
    ok_bool_false(Deleted, "delete 30 again");

    for (i = 0; i < RTL_NUMBER_OF(Values); i++)
        RtlDeleteElementGenericTable(&Table, &Values[i]);
    ok_bool_true(RtlIsGenericTableEmpty(&Table), "table empty after deletes");
    ok_eq_long(TableFrees, 5L);
}
