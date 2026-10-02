/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     S-list header updates must not overwrite list entries
 */

#include "precomp.h"

START_TEST(RtlSList)
{
    SLIST_HEADER Head;
    struct DECLSPEC_ALIGN(MEMORY_ALLOCATION_ALIGNMENT)
    {
        SLIST_ENTRY Entry;
        ULONG_PTR Guard;
    } Entries[3];
    PSLIST_ENTRY Entry;
    ULONG Index;

    RtlInitializeSListHead(&Head);
    ok_int(RtlQueryDepthSList(&Head), 0);
    ok_ptr(RtlInterlockedPopEntrySList(&Head), NULL);

    for (Index = 0; Index < _countof(Entries); Index++)
    {
        Entries[Index].Guard = 0x12345678 + Index;
        Entry = RtlInterlockedPushEntrySList(&Head, &Entries[Index].Entry);
        ok_ptr(Entry, Index ? &Entries[Index - 1].Entry : NULL);
        ok_int(RtlQueryDepthSList(&Head), Index + 1);
    }

    for (Index = _countof(Entries); Index != 0; Index--)
    {
        ok_ptr(RtlInterlockedPopEntrySList(&Head), &Entries[Index - 1].Entry);
        ok_int(RtlQueryDepthSList(&Head), Index - 1);
        ok_hex(Entries[Index - 1].Guard, 0x12345678 + Index - 1);
    }
    ok_ptr(RtlInterlockedPopEntrySList(&Head), NULL);

    /* Reuse the header after draining it; stale heads can hide the first bug. */
    ok_ptr(RtlInterlockedPushEntrySList(&Head, &Entries[0].Entry), NULL);
    ok_ptr(RtlInterlockedPushEntrySList(&Head, &Entries[1].Entry), &Entries[0].Entry);
    ok_ptr(RtlInterlockedFlushSList(&Head), &Entries[1].Entry);
    ok_int(RtlQueryDepthSList(&Head), 0);
    ok_ptr(RtlInterlockedPopEntrySList(&Head), NULL);
    for (Index = 0; Index < _countof(Entries); Index++)
        ok_hex(Entries[Index].Guard, 0x12345678 + Index);
}
