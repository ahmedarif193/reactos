/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/include/pooltrack.h
 * PURPOSE:     Pool allocation tracking definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define POOL_TAG_TABLE_ENTRIES   2048
#define POOL_TAG_OVERFLOW        0x6C66764F
#define POOL_TAG_CLASSES         2

typedef struct _POOL_TAG_ENTRY
{
    volatile LONG Tag;
    volatile LONG Allocations[POOL_TAG_CLASSES];
    volatile LONG Frees[POOL_TAG_CLASSES];
    volatile LONG64 Bytes[POOL_TAG_CLASSES];
} POOL_TAG_ENTRY, *PPOOL_TAG_ENTRY;

typedef struct _POOL_TAG_TABLE
{
    POOL_TAG_ENTRY Entry[POOL_TAG_TABLE_ENTRIES];
} POOL_CACHE_ALIGNED POOL_TAG_TABLE, *PPOOL_TAG_TABLE;

typedef struct _POOL_TAG_USAGE
{
    ULONG Tag;
    ULONG64 Allocations[POOL_TAG_CLASSES];
    ULONG64 Frees[POOL_TAG_CLASSES];
    LONG64 Bytes[POOL_TAG_CLASSES];
} POOL_TAG_USAGE, *PPOOL_TAG_USAGE;

typedef struct _POOL_TAG_TRACKER
{
    ULONG TableCount;
    PPOOL_TAG_TABLE Table[POOL_MAX_SLOTS];
} POOL_TAG_TRACKER, *PPOOL_TAG_TRACKER;

VOID PoolTagTrackerInitialize(_Out_ PPOOL_TAG_TRACKER Tracker);
BOOLEAN PoolTagTrackerAddTable(_Inout_ PPOOL_TAG_TRACKER Tracker, _In_ PPOOL_TAG_TABLE Table);
VOID PoolTagCharge(_Inout_ PPOOL_TAG_TRACKER Tracker, _In_ ULONG Tag, _In_ ULONG Class, _In_ SIZE_T Bytes);
VOID PoolTagRelease(_Inout_ PPOOL_TAG_TRACKER Tracker, _In_ ULONG Tag, _In_ ULONG Class, _In_ SIZE_T Bytes);
BOOLEAN PoolTagEnumerate(_In_ PPOOL_TAG_TRACKER Tracker, _Inout_ PULONG64 Cursor, _Out_ PPOOL_TAG_USAGE Usage);
ULONG PoolTagSnapshot(_In_ PPOOL_TAG_TRACKER Tracker, _Out_ PPOOL_TAG_USAGE Usage, _In_ ULONG MaxEntries);
BOOLEAN PoolTagQuery(_In_ PPOOL_TAG_TRACKER Tracker, _In_ ULONG Tag, _Out_ PPOOL_TAG_USAGE Usage);
