/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite RTL_DYNAMIC_HASH_TABLE API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

typedef struct _KMT_HASH_ENTRY
{
    RTL_DYNAMIC_HASH_TABLE_ENTRY HashEntry;
    ULONG Value;
} KMT_HASH_ENTRY, *PKMT_HASH_ENTRY;

static
VOID
TestHashTableBasic(VOID)
{
    PRTL_DYNAMIC_HASH_TABLE HashTable = NULL;
    KMT_HASH_ENTRY Entries[8];
    RTL_DYNAMIC_HASH_TABLE_CONTEXT Context;
    PRTL_DYNAMIC_HASH_TABLE_ENTRY Found;
    BOOLEAN Ok;
    ULONG i, FoundCount;

    Ok = RtlCreateHashTable(&HashTable, 0, 0);
    ok_bool_true(Ok, "RtlCreateHashTable");
    if (!Ok || HashTable == NULL) return;
    ok(HashTable->Flags & RTL_HASH_ALLOCATED_HEADER, "header not marked allocated\n");
    ok_eq_ulong(HashTable->NumEntries, 0UL);

    for (i = 0; i < 8; i++)
    {
        Entries[i].Value = i;
        Ok = RtlInsertEntryHashTable(HashTable, &Entries[i].HashEntry, (ULONG_PTR)(0x100 + (i % 4)), NULL);
        ok_bool_true(Ok, "insert");
    }
    ok_eq_ulong(HashTable->NumEntries, 8UL);

    FoundCount = 0;
    for (Found = RtlLookupEntryHashTable(HashTable, 0x101, &Context);
         Found != NULL;
         Found = RtlGetNextEntryHashTable(HashTable, &Context))
    {
        PKMT_HASH_ENTRY Entry = CONTAINING_RECORD(Found, KMT_HASH_ENTRY, HashEntry);
        ok(Entry->Value == 1 || Entry->Value == 5, "unexpected value %lu for signature 0x101\n", Entry->Value);
        FoundCount++;
        if (FoundCount > 8) break;
    }
    ok_eq_ulong(FoundCount, 2UL);

    Found = RtlLookupEntryHashTable(HashTable, 0x999, &Context);
    ok_eq_pointer(Found, NULL);

    for (i = 0; i < 8; i++)
    {
        Ok = RtlRemoveEntryHashTable(HashTable, &Entries[i].HashEntry, NULL);
        ok_bool_true(Ok, "remove");
    }
    ok_eq_ulong(HashTable->NumEntries, 0UL);

    RtlDeleteHashTable(HashTable);
}

static
VOID
TestHashTableEnum(VOID)
{
    PRTL_DYNAMIC_HASH_TABLE HashTable = NULL;
    KMT_HASH_ENTRY Entries[16];
    RTL_DYNAMIC_HASH_TABLE_ENUMERATOR Enumerator;
    PRTL_DYNAMIC_HASH_TABLE_ENTRY Found;
    BOOLEAN Ok;
    ULONG i, Seen;

    Ok = RtlCreateHashTable(&HashTable, 0, 0);
    ok_bool_true(Ok, "create");
    if (!Ok || HashTable == NULL) return;

    for (i = 0; i < 16; i++)
    {
        Entries[i].Value = i;
        RtlInsertEntryHashTable(HashTable, &Entries[i].HashEntry, (ULONG_PTR)(i * 7 + 3), NULL);
    }

    Ok = RtlInitEnumerationHashTable(HashTable, &Enumerator);
    ok_bool_true(Ok, "init enumeration");
    if (Ok)
    {
        Seen = 0;
        while ((Found = RtlEnumerateEntryHashTable(HashTable, &Enumerator)) != NULL)
        {
            Seen++;
            if (Seen > 16) break;
        }
        ok_eq_ulong(Seen, 16UL);
        RtlEndEnumerationHashTable(HashTable, &Enumerator);
    }

    Ok = RtlInitWeakEnumerationHashTable(HashTable, &Enumerator);
    ok_bool_true(Ok, "init weak enumeration");
    if (Ok)
    {
        Seen = 0;
        while ((Found = RtlWeaklyEnumerateEntryHashTable(HashTable, &Enumerator)) != NULL)
        {
            Seen++;
            if (Seen > 16) break;
        }
        ok_eq_ulong(Seen, 16UL);
        RtlEndWeakEnumerationHashTable(HashTable, &Enumerator);
    }

    for (i = 0; i < 16; i++)
        RtlRemoveEntryHashTable(HashTable, &Entries[i].HashEntry, NULL);

    RtlDeleteHashTable(HashTable);
}

static
VOID
TestHashTableExpand(VOID)
{
    PRTL_DYNAMIC_HASH_TABLE HashTable = NULL;
    PKMT_HASH_ENTRY Entries;
    PRTL_DYNAMIC_HASH_TABLE_ENTRY Found;
    RTL_DYNAMIC_HASH_TABLE_CONTEXT Context;
    BOOLEAN Ok;
    ULONG i;

    Entries = ExAllocatePoolWithTag(PagedPool, 512 * sizeof(KMT_HASH_ENTRY), 'tHmK');
    ok(Entries != NULL, "no pool for entries\n");
    if (Entries == NULL) return;

    Ok = RtlCreateHashTable(&HashTable, 0, 0);
    ok_bool_true(Ok, "create");
    if (!Ok || HashTable == NULL)
    {
        ExFreePoolWithTag(Entries, 'tHmK');
        return;
    }

    for (i = 0; i < 512; i++)
    {
        Entries[i].Value = i;
        RtlInsertEntryHashTable(HashTable, &Entries[i].HashEntry, (ULONG_PTR)(i + 1), NULL);
    }
    ok_eq_ulong(HashTable->NumEntries, 512UL);

    for (i = 0; i < 128; i++)
    {
        Ok = RtlExpandHashTable(HashTable);
        ok_bool_true(Ok, "expand");
    }
    ok_eq_ulong(HashTable->TableSize, 256UL);

    for (i = 0; i < 512; i += 37)
    {
        Found = RtlLookupEntryHashTable(HashTable, (ULONG_PTR)(i + 1), &Context);
        ok(Found != NULL, "lookup %lu after expand failed\n", i);
        if (Found) ok_eq_ulong(CONTAINING_RECORD(Found, KMT_HASH_ENTRY, HashEntry)->Value, i);
    }

    for (i = 0; i < 128; i++)
    {
        Ok = RtlContractHashTable(HashTable);
        ok_bool_true(Ok, "contract");
    }
    ok_eq_ulong(HashTable->TableSize, 128UL);

    for (i = 0; i < 512; i += 41)
    {
        Found = RtlLookupEntryHashTable(HashTable, (ULONG_PTR)(i + 1), &Context);
        ok(Found != NULL, "lookup %lu after contract failed\n", i);
    }

    for (i = 0; i < 512; i++)
        RtlRemoveEntryHashTable(HashTable, &Entries[i].HashEntry, NULL);
    ok_eq_ulong(HashTable->NumEntries, 0UL);

    RtlDeleteHashTable(HashTable);
    ExFreePoolWithTag(Entries, 'tHmK');
}

START_TEST(RtlHashTableKM)
{
    TestHashTableBasic();
    TestHashTableEnum();
    TestHashTableExpand();
}
