/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/engine/viewpool.c
 * PURPOSE:     Cached-view allocation and reuse
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/ccengine.h"

FORCEINLINE
ULONG64
CcSlotNumber(ULONG64 FileOffset)
{
    return FileOffset >> CC_VIEW_SHIFT;
}

PCC_SLOT
CcMapSlot(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 SlotNumber,
    _In_ BOOLEAN Create)
{
    ULONG64 LeafIndex = SlotNumber >> CC_LEAF_SHIFT;

    if (LeafIndex >= Map->LeafCount)
    {
        PCC_LEAF *Grown;
        ULONG NewCount;

        if (!Create || LeafIndex >= 0x7FFFFFFF)
            return NULL;

        NewCount = Map->LeafCount ? Map->LeafCount : 4;
        while (NewCount <= LeafIndex)
            NewCount *= 2;

        Grown = CC_ALLOCATE(sizeof(PCC_LEAF) * NewCount);
        if (Grown == NULL)
            return NULL;

        RtlZeroMemory(Grown, sizeof(PCC_LEAF) * NewCount);
        if (Map->Leaf != NULL)
        {
            RtlCopyMemory(Grown, Map->Leaf, sizeof(PCC_LEAF) * Map->LeafCount);
            CC_FREE(Map->Leaf);
        }

        Map->Leaf = Grown;
        CC_ATOMIC_WRITE32(&Map->LeafCount, NewCount);
    }

    if (Map->Leaf[LeafIndex] == NULL)
    {
        PCC_LEAF Leaf;

        if (!Create)
            return NULL;

        Leaf = CC_ALLOCATE(sizeof(CC_LEAF));
        if (Leaf == NULL)
            return NULL;

        RtlZeroMemory(Leaf, sizeof(CC_LEAF));
        Map->Leaf[LeafIndex] = Leaf;
    }

    return &Map->Leaf[LeafIndex]->Slot[SlotNumber & CC_LEAF_MASK];
}

NTSTATUS
CcCacheInitialize(
    _Out_ PCC_CACHE Cache,
    _In_ ULONG ViewCount,
    _In_ LONG64 DirtyPageThreshold)
{
    ULONG i;

    RtlZeroMemory(Cache, sizeof(*Cache));

    if (ViewCount < CC_MIN_VIEWS)
        ViewCount = CC_MIN_VIEWS;
    if (ViewCount > CC_MAX_VIEWS)
        ViewCount = CC_MAX_VIEWS;

    Cache->View = CC_ALLOCATE(sizeof(CC_VIEW) * ViewCount);
    if (Cache->View == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Cache->View, sizeof(CC_VIEW) * ViewCount);
    Cache->ViewCount = ViewCount;
    Cache->DirtyPageThreshold = DirtyPageThreshold;
    CC_LOCK_INIT(&Cache->ReclaimLock);
    CC_LOCK_INIT(&Cache->DirtyLock);
    InitializeListHead(&Cache->DirtyMaps);

    for (i = 0; i < CC_LRU_SHARDS; i++)
    {
        CC_LOCK_INIT(&Cache->Lru[i].Lock);
        InitializeListHead(&Cache->Lru[i].List);
    }

    for (i = 0; i < ViewCount; i++)
    {
        PCC_VIEW View = &Cache->View[i];

        View->LruShard = (UCHAR)(i & (CC_LRU_SHARDS - 1));
        View->OnLru = TRUE;
        InsertTailList(&Cache->Lru[View->LruShard].List, &View->LruLink);
        Cache->Lru[View->LruShard].Count++;
    }

    return STATUS_SUCCESS;
}

VOID
CcCacheUninitialize(
    _Inout_ PCC_CACHE Cache)
{
    if (Cache->View != NULL)
        CC_FREE(Cache->View);

    Cache->View = NULL;
    Cache->ViewCount = 0;
}

static
VOID
CcLruRemove(
    _Inout_ PCC_CACHE Cache,
    _Inout_ PCC_VIEW View)
{
    PCC_LRU_SHARD Shard = &Cache->Lru[View->LruShard];
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);
    if (View->OnLru)
    {
        RemoveEntryList(&View->LruLink);
        View->OnLru = FALSE;
        Shard->Count--;
    }
    CC_LOCK_RELEASE(&Shard->Lock, OldIrql);
}

static
VOID
CcLruInsert(
    _Inout_ PCC_CACHE Cache,
    _Inout_ PCC_VIEW View)
{
    PCC_LRU_SHARD Shard = &Cache->Lru[View->LruShard];
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);
    if (!View->OnLru)
    {
        InsertTailList(&Shard->List, &View->LruLink);
        View->OnLru = TRUE;
        Shard->Count++;
    }
    CC_LOCK_RELEASE(&Shard->Lock, OldIrql);
}

static
PCC_VIEW
CcLruPop(
    _Inout_ PCC_CACHE Cache,
    _In_ ULONG ShardIndex)
{
    PCC_LRU_SHARD Shard = &Cache->Lru[ShardIndex];
    PCC_VIEW View = NULL;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Shard->Lock, &OldIrql);
    if (!IsListEmpty(&Shard->List))
    {
        View = CONTAINING_RECORD(Shard->List.Flink, CC_VIEW, LruLink);
        RemoveEntryList(&View->LruLink);
        View->OnLru = FALSE;
        Shard->Count--;
    }
    CC_LOCK_RELEASE(&Shard->Lock, OldIrql);
    return View;
}

static
PCC_VIEW
CcViewReclaim(
    _Inout_ PCC_CACHE Cache,
    _Out_ PCC_MAP *OldMap,
    _Out_ PVOID *OldBase)
{
    ULONG Budget = Cache->ViewCount;
    PCC_VIEW Claimed = NULL;
    KIRQL ReclaimIrql;
    ULONG Start;
    ULONG Turn = 0;

    *OldMap = NULL;
    *OldBase = NULL;

    CC_LOCK_ACQUIRE(&Cache->ReclaimLock, &ReclaimIrql);
    Start = Cache->ReclaimCursor++;

    while (Budget-- != 0 && Claimed == NULL)
    {
        PCC_VIEW View = CcLruPop(Cache, (Start + Turn++) & (CC_LRU_SHARDS - 1));
        PCC_MAP Map;
        KIRQL OldIrql;

        if (View == NULL)
            continue;

        Map = View->Map;
        if (Map == NULL)
        {
            if (CC_ATOMIC_READ32(&View->ReferenceCount) == 0)
                Claimed = View;
            continue;
        }

        CC_LOCK_ACQUIRE(&Map->IndexLock, &OldIrql);

        if (View->Map == Map && CC_ATOMIC_READ32(&View->ReferenceCount) == 0)
        {
            PCC_SLOT Slot = CcMapSlot(Map, CcSlotNumber(View->FileOffset), FALSE);

            CC_ASSERT(Slot != NULL && Slot->View == View);
            Slot->View = NULL;
            Map->ViewsAttached--;
            *OldMap = Map;
            *OldBase = View->BaseAddress;
            View->Map = NULL;
            View->BaseAddress = NULL;
            Claimed = View;
        }

        CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);
    }

    if (Claimed != NULL)
    {
        CcLruRemove(Cache, Claimed);
        Claimed->ReferenceCount = 1;
    }

    CC_LOCK_RELEASE(&Cache->ReclaimLock, ReclaimIrql);
    return Claimed;
}

VOID
CcViewReference(
    _Inout_ PCC_VIEW View)
{
    CC_ATOMIC_ADD32(&View->ReferenceCount, 1);
}

VOID
CcViewDereference(
    _Inout_ PCC_VIEW View)
{
    PCC_MAP Map = View->Map;
    PCC_CACHE Cache = (Map != NULL) ? Map->Cache : NULL;
    LONG Remaining = CC_ATOMIC_ADD32(&View->ReferenceCount, -1) - 1;

    CC_ASSERT(Remaining >= 0);
    if (Remaining == 0 && Cache != NULL)
        CcLruInsert(Cache, View);
}

static
NTSTATUS
CcViewGet(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 ViewOffset,
    _Out_ PCC_VIEW *OutView)
{
    PCC_CACHE Cache = Map->Cache;
    PCC_VIEW View;
    PCC_VIEW Fresh;
    PCC_MAP OldMap;
    PVOID OldBase;
    PVOID Base;
    PCC_SLOT Slot;
    NTSTATUS Status;
    KIRQL OldIrql;

    *OutView = NULL;

    CC_LOCK_ACQUIRE(&Map->IndexLock, &OldIrql);
    Slot = CcMapSlot(Map, CcSlotNumber(ViewOffset), FALSE);
    View = (Slot != NULL) ? Slot->View : NULL;
    if (View != NULL)
        CcViewReference(View);
    CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);

    if (View != NULL)
    {
        CcLruRemove(Cache, View);
        CC_ATOMIC_ADD64(&Cache->ViewHits, 1);
        *OutView = View;
        return STATUS_SUCCESS;
    }

    CC_ATOMIC_ADD64(&Cache->ViewMisses, 1);

    Fresh = CcViewReclaim(Cache, &OldMap, &OldBase);
    if (Fresh == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    if (OldBase != NULL)
    {
        OldMap->Ops.UnmapView(OldMap->Context, OldBase);
        CC_ATOMIC_ADD64(&Cache->ViewReclaims, 1);
    }

    Status = Map->Ops.MapView(Map->Context, ViewOffset, CC_VIEW_SIZE, &Base);
    if (!NT_SUCCESS(Status))
    {
        Fresh->ReferenceCount = 0;
        CcLruInsert(Cache, Fresh);
        return Status;
    }

    CC_LOCK_ACQUIRE(&Map->IndexLock, &OldIrql);
    Slot = CcMapSlot(Map, CcSlotNumber(ViewOffset), TRUE);

    if (Slot == NULL)
    {
        CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);
        Map->Ops.UnmapView(Map->Context, Base);
        Fresh->ReferenceCount = 0;
        CcLruInsert(Cache, Fresh);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Slot->View != NULL)
    {
        View = Slot->View;
        CcViewReference(View);
        CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);

        CcLruRemove(Cache, View);
        Map->Ops.UnmapView(Map->Context, Base);
        Fresh->ReferenceCount = 0;
        CcLruInsert(Cache, Fresh);
        *OutView = View;
        return STATUS_SUCCESS;
    }

    Fresh->Map = Map;
    Fresh->BaseAddress = Base;
    Fresh->FileOffset = ViewOffset;
    Slot->View = Fresh;
    Map->ViewsAttached++;
    CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);

    *OutView = Fresh;
    return STATUS_SUCCESS;
}

NTSTATUS
CcViewAcquire(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 FileOffset,
    _In_ ULONG Length,
    _Out_ PCC_VIEW_RANGE Range)
{
    ULONG Within = (ULONG)(FileOffset & CC_VIEW_MASK);
    NTSTATUS Status;

    RtlZeroMemory(Range, sizeof(*Range));

    if (FileOffset >= (ULONG64)CC_ATOMIC_READ64(&Map->SectionSize))
        return STATUS_END_OF_FILE;

    Status = CcViewGet(Map, FileOffset & ~(ULONG64)CC_VIEW_MASK, &Range->View);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Length > CC_VIEW_SIZE - Within)
        Length = CC_VIEW_SIZE - Within;

    Range->Address = (PUCHAR)Range->View->BaseAddress + Within;
    Range->FileOffset = FileOffset;
    Range->Length = Length;
    return STATUS_SUCCESS;
}

VOID
CcViewRelease(
    _Inout_ PCC_VIEW_RANGE Range)
{
    if (Range->View != NULL)
        CcViewDereference(Range->View);

    Range->View = NULL;
}

NTSTATUS
CcViewMakeResident(
    _Inout_ PCC_MAP Map,
    _In_ PCC_VIEW_RANGE Range,
    _In_ BOOLEAN SkipRead)
{
    if (SkipRead || Map->Ops.IsResident(Map->Context, Range->FileOffset, Range->Length))
        return STATUS_SUCCESS;

    return Map->Ops.MakeResident(Map->Context, Range->FileOffset, Range->Length, Map->ValidDataLength);
}

BOOLEAN
CcMapDetachViews(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 FileOffset,
    _In_ ULONG64 Length)
{
    PCC_CACHE Cache = Map->Cache;
    ULONG64 First = CcSlotNumber(FileOffset);
    ULONG64 Limit = (ULONG64)(ULONG)CC_ATOMIC_READ32(&Map->LeafCount) << CC_LEAF_SHIFT;
    ULONG64 End = (Length == (ULONG64)-1 || FileOffset + Length < FileOffset)
                      ? Limit
                      : CcSlotNumber(FileOffset + Length + CC_VIEW_MASK);
    BOOLEAN AllDetached = TRUE;
    ULONG64 Number;

    if (End > Limit)
        End = Limit;

    for (Number = First; Number < End; Number++)
    {
        PVOID Base = NULL;
        PCC_VIEW View = NULL;
        PCC_SLOT Slot;
        KIRQL ReclaimIrql;
        KIRQL OldIrql;

        CC_LOCK_ACQUIRE(&Cache->ReclaimLock, &ReclaimIrql);
        CC_LOCK_ACQUIRE(&Map->IndexLock, &OldIrql);

        Slot = CcMapSlot(Map, Number, FALSE);
        if (Slot == NULL)
        {
            CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);
            CC_LOCK_RELEASE(&Cache->ReclaimLock, ReclaimIrql);
            Number |= CC_LEAF_MASK;
            continue;
        }

        if (Slot->View != NULL)
        {
            if (CC_ATOMIC_READ32(&Slot->View->ReferenceCount) == 0)
            {
                View = Slot->View;
                Base = View->BaseAddress;
                Slot->View = NULL;
                Map->ViewsAttached--;
                View->Map = NULL;
                View->BaseAddress = NULL;
            }
            else
            {
                AllDetached = FALSE;
            }
        }

        CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);

        if (View != NULL)
        {
            CcLruRemove(Cache, View);
            CcLruInsert(Cache, View);
        }

        CC_LOCK_RELEASE(&Cache->ReclaimLock, ReclaimIrql);

        if (Base != NULL)
            Map->Ops.UnmapView(Map->Context, Base);
    }

    return AllDetached;
}

ULONG
CcCacheTrim(
    _Inout_ PCC_CACHE Cache,
    _In_ ULONG ViewTarget)
{
    ULONG Trimmed = 0;

    while (Trimmed < ViewTarget)
    {
        PCC_MAP OldMap;
        PVOID OldBase;
        PCC_VIEW View = CcViewReclaim(Cache, &OldMap, &OldBase);

        if (View == NULL)
            break;

        if (OldBase != NULL)
        {
            OldMap->Ops.UnmapView(OldMap->Context, OldBase);
            Trimmed++;
        }

        View->ReferenceCount = 0;
        CcLruInsert(Cache, View);

        if (OldBase == NULL)
            break;
    }

    return Trimmed;
}

ULONG
CcCacheCheck(
    _In_ PCC_CACHE Cache)
{
    ULONG Errors = 0;
    ULONG OnLru = 0;
    ULONG i;

    for (i = 0; i < Cache->ViewCount; i++)
    {
        PCC_VIEW View = &Cache->View[i];

        if (View->OnLru)
            OnLru++;
        if (View->ReferenceCount < 0)
            Errors++;
        if (View->Map == NULL && View->BaseAddress != NULL)
            Errors++;
        if (View->Map != NULL && View->BaseAddress == NULL)
            Errors++;
        if (View->ReferenceCount == 0 && !View->OnLru)
            Errors++;
    }

    for (i = 0; i < CC_LRU_SHARDS; i++)
    {
        if (OnLru < Cache->Lru[i].Count)
            Errors++;
        OnLru -= (OnLru < Cache->Lru[i].Count) ? 0 : Cache->Lru[i].Count;
    }

    return Errors + (OnLru != 0);
}
