/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Element indexing of splay and AVL generic tables
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

#define TAG_TEST 'eTmK'
#define VALUE_COUNT 5

static CONST ULONG Values[VALUE_COUNT] = { 50, 10, 30, 20, 40 };
static CONST ULONG Probes[] = { 4, 0, 2, 1, 3, 3, 0, 4 };

static
RTL_GENERIC_COMPARE_RESULTS
NTAPI
CompareSplay(
    _In_ PRTL_GENERIC_TABLE Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct)
{
    ULONG A = *(PULONG)FirstStruct;
    ULONG B = *(PULONG)SecondStruct;

    UNREFERENCED_PARAMETER(Table);

    return A < B ? GenericLessThan : A > B ? GenericGreaterThan : GenericEqual;
}

static
PVOID
NTAPI
AllocateSplay(
    _In_ PRTL_GENERIC_TABLE Table,
    _In_ CLONG ByteSize)
{
    UNREFERENCED_PARAMETER(Table);

    return ExAllocatePoolWithTag(NonPagedPool, ByteSize, TAG_TEST);
}

static
VOID
NTAPI
FreeSplay(
    _In_ PRTL_GENERIC_TABLE Table,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Table);

    ExFreePoolWithTag(Buffer, TAG_TEST);
}

static
RTL_GENERIC_COMPARE_RESULTS
NTAPI
CompareAvl(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID FirstStruct,
    _In_ PVOID SecondStruct)
{
    ULONG A = *(PULONG)FirstStruct;
    ULONG B = *(PULONG)SecondStruct;

    UNREFERENCED_PARAMETER(Table);

    return A < B ? GenericLessThan : A > B ? GenericGreaterThan : GenericEqual;
}

static
PVOID
NTAPI
AllocateAvl(
    _In_ PRTL_AVL_TABLE Table,
    _In_ CLONG ByteSize)
{
    UNREFERENCED_PARAMETER(Table);

    return ExAllocatePoolWithTag(NonPagedPool, ByteSize, TAG_TEST);
}

static
VOID
NTAPI
FreeAvl(
    _In_ PRTL_AVL_TABLE Table,
    _In_ PVOID Buffer)
{
    UNREFERENCED_PARAMETER(Table);

    ExFreePoolWithTag(Buffer, TAG_TEST);
}

static
ULONG
ElementValue(
    _In_opt_ PVOID Element)
{
    return Element != NULL ? *(PULONG)Element : MAXULONG;
}

static
VOID
TestSplay(VOID)
{
    static CONST ULONG Inserted[VALUE_COUNT] = { 50, 10, 30, 20, 40 };
    static CONST ULONG Remaining[VALUE_COUNT - 1] = { 50, 10, 20, 40 };
    RTL_GENERIC_TABLE Table;
    ULONG Index, Value;

    RtlInitializeGenericTable(&Table, CompareSplay, AllocateSplay, FreeSplay, NULL);
    ok_eq_pointer(RtlGetElementGenericTable(&Table, 0), NULL);

    for (Index = 0; Index < VALUE_COUNT; Index++)
    {
        Value = Values[Index];
        ok(RtlInsertElementGenericTable(&Table, &Value, sizeof(Value), NULL) != NULL, "Insert %lu failed\n", Value);
    }

    for (Index = 0; Index < VALUE_COUNT; Index++)
    {
        ok_eq_ulong(ElementValue(RtlGetElementGenericTable(&Table, Index)), Inserted[Index]);
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Probes); Index++)
    {
        ok_eq_ulong(ElementValue(RtlGetElementGenericTable(&Table, Probes[Index])), Inserted[Probes[Index]]);
    }
    ok_eq_pointer(RtlGetElementGenericTable(&Table, VALUE_COUNT), NULL);
    ok_eq_pointer(RtlGetElementGenericTable(&Table, MAXULONG), NULL);

    Value = 30;
    ok_bool_true(RtlDeleteElementGenericTable(&Table, &Value), "Delete returned");
    for (Index = 0; Index < VALUE_COUNT - 1; Index++)
    {
        ok_eq_ulong(ElementValue(RtlGetElementGenericTable(&Table, Index)), Remaining[Index]);
    }
    ok_eq_pointer(RtlGetElementGenericTable(&Table, VALUE_COUNT - 1), NULL);

    while (!RtlIsGenericTableEmpty(&Table))
    {
        PVOID Element = RtlGetElementGenericTable(&Table, 0);

        ok(Element != NULL, "No first element\n");
        if (Element == NULL || !RtlDeleteElementGenericTable(&Table, Element))
        {
            break;
        }
    }
    ok_eq_ulong(RtlNumberGenericTableElements(&Table), 0UL);
}

static
VOID
TestAvl(VOID)
{
    static CONST ULONG Sorted[VALUE_COUNT] = { 10, 20, 30, 40, 50 };
    static CONST ULONG Remaining[VALUE_COUNT - 1] = { 10, 20, 40, 50 };
    RTL_AVL_TABLE Table;
    ULONG Index, Value;

    RtlInitializeGenericTableAvl(&Table, CompareAvl, AllocateAvl, FreeAvl, NULL);
    ok_eq_pointer(RtlGetElementGenericTableAvl(&Table, 0), NULL);

    for (Index = 0; Index < VALUE_COUNT; Index++)
    {
        Value = Values[Index];
        ok(RtlInsertElementGenericTableAvl(&Table, &Value, sizeof(Value), NULL) != NULL, "Insert %lu failed\n", Value);
    }

    for (Index = 0; Index < VALUE_COUNT; Index++)
    {
        ok_eq_ulong(ElementValue(RtlGetElementGenericTableAvl(&Table, Index)), Sorted[Index]);
    }
    for (Index = 0; Index < RTL_NUMBER_OF(Probes); Index++)
    {
        ok_eq_ulong(ElementValue(RtlGetElementGenericTableAvl(&Table, Probes[Index])), Sorted[Probes[Index]]);
    }
    ok_eq_pointer(RtlGetElementGenericTableAvl(&Table, VALUE_COUNT), NULL);
    ok_eq_pointer(RtlGetElementGenericTableAvl(&Table, MAXULONG), NULL);

    Value = 30;
    ok_bool_true(RtlDeleteElementGenericTableAvl(&Table, &Value), "Delete returned");
    for (Index = 0; Index < VALUE_COUNT - 1; Index++)
    {
        ok_eq_ulong(ElementValue(RtlGetElementGenericTableAvl(&Table, Index)), Remaining[Index]);
    }
    ok_eq_pointer(RtlGetElementGenericTableAvl(&Table, VALUE_COUNT - 1), NULL);

    Value = 5;
    ok(RtlInsertElementGenericTableAvl(&Table, &Value, sizeof(Value), NULL) != NULL, "Insert %lu failed\n", Value);
    ok_eq_ulong(ElementValue(RtlGetElementGenericTableAvl(&Table, 0)), 5UL);
    ok_eq_ulong(ElementValue(RtlGetElementGenericTableAvl(&Table, 4)), 50UL);

    while (!RtlIsGenericTableEmptyAvl(&Table))
    {
        PVOID Element = RtlGetElementGenericTableAvl(&Table, 0);

        ok(Element != NULL, "No first element\n");
        if (Element == NULL || !RtlDeleteElementGenericTableAvl(&Table, Element))
        {
            break;
        }
    }
    ok_eq_ulong(RtlNumberGenericTableElementsAvl(&Table), 0UL);
}

START_TEST(RtlTableElementKM)
{
    TestSplay();
    TestAvl();
}
