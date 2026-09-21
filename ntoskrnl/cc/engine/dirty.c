/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/engine/dirty.c
 * PURPOSE:     Dirty cached-data tracking
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/ccengine.h"

PCC_SLOT CcMapSlot(_Inout_ PCC_MAP Map, _In_ ULONG64 SlotNumber, _In_ BOOLEAN Create);

FORCEINLINE
ULONG64
CcDirtyBits(
    _In_ ULONG FirstPage,
    _In_ ULONG LastPage)
{
    ULONG Count = LastPage - FirstPage + 1;
    ULONG64 Bits = (Count >= 64) ? ~(ULONG64)0 : (((ULONG64)1 << Count) - 1);

    return Bits << FirstPage;
}

static
ULONG64
CcDirtySlotBits(
    _In_ ULONG64 SlotNumber,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    ULONG64 SlotStart = SlotNumber << CC_VIEW_SHIFT;
    ULONG64 SlotEnd = SlotStart + CC_VIEW_SIZE;
    ULONG64 From = (Start > SlotStart) ? Start : SlotStart;
    ULONG64 To = (End < SlotEnd) ? End : SlotEnd;

    if (From >= To)
        return 0;

    return CcDirtyBits((ULONG)((From - SlotStart) >> PAGE_SHIFT),
                       (ULONG)((To - 1 - SlotStart) >> PAGE_SHIFT));
}

static
VOID
CcDirtyAccount(
    _Inout_ PCC_MAP Map,
    _In_ LONG64 Pages)
{
    PCC_CACHE Cache = Map->Cache;
    LONG64 Now;
    KIRQL OldIrql;

    CC_ATOMIC_ADD64(&Map->DirtyPages, Pages);
    CC_ATOMIC_ADD64(&Cache->TotalDirtyPages, Pages);

    CC_LOCK_ACQUIRE(&Cache->DirtyLock, &OldIrql);
    Now = CC_ATOMIC_READ64(&Map->DirtyPages);
    if (Now > 0 && !Map->OnDirtyList)
    {
        InsertTailList(&Cache->DirtyMaps, &Map->DirtyLink);
        Map->OnDirtyList = TRUE;
    }
    else if (Now <= 0 && Map->OnDirtyList)
    {
        RemoveEntryList(&Map->DirtyLink);
        Map->OnDirtyList = FALSE;
    }
    CC_LOCK_RELEASE(&Cache->DirtyLock, OldIrql);
}

NTSTATUS
CcDirtyMark(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 FileOffset,
    _In_ ULONG Length)
{
    ULONG64 End = FileOffset + Length;
    ULONG64 Number;
    LONG64 Added = 0;
    NTSTATUS Status;
    KIRQL OldIrql;

    if (Length == 0)
        return STATUS_SUCCESS;

    Status = Map->Ops.MarkDirty(Map->Context, FileOffset, Length);
    if (!NT_SUCCESS(Status))
        return Status;

    CC_LOCK_ACQUIRE(&Map->IndexLock, &OldIrql);

    for (Number = FileOffset >> CC_VIEW_SHIFT; Number <= ((End - 1) >> CC_VIEW_SHIFT); Number++)
    {
        PCC_SLOT Slot = CcMapSlot(Map, Number, TRUE);
        ULONG64 Bits;

        if (Slot == NULL)
        {
            CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);
            if (Added != 0)
                CcDirtyAccount(Map, Added);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Bits = CcDirtySlotBits(Number, FileOffset, End) & ~Slot->DirtyMap;
        Slot->DirtyMap |= Bits;
        Added += CC_POPCOUNT64(Bits);
    }

    CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);

    if (Added != 0)
        CcDirtyAccount(Map, Added);

    return STATUS_SUCCESS;
}

static
ULONG64
CcDirtyTake(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 Number,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG MaximumPages)
{
    PCC_SLOT Slot;
    ULONG64 Bits = 0;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Map->IndexLock, &OldIrql);

    Slot = CcMapSlot(Map, Number, FALSE);
    if (Slot != NULL)
    {
        Bits = Slot->DirtyMap & CcDirtySlotBits(Number, Start, End);

        while (CC_POPCOUNT64(Bits) > MaximumPages)
            Bits &= Bits - 1;

        Slot->DirtyMap &= ~Bits;
    }

    CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);
    return Bits;
}

static
VOID
CcDirtyRestore(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 Number,
    _In_ ULONG64 Bits)
{
    PCC_SLOT Slot;
    ULONG64 Restored = 0;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Map->IndexLock, &OldIrql);
    Slot = CcMapSlot(Map, Number, FALSE);
    if (Slot != NULL)
    {
        Restored = Bits & ~Slot->DirtyMap;
        Slot->DirtyMap |= Restored;
    }
    CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);

    if (Restored != 0)
        CcDirtyAccount(Map, CC_POPCOUNT64(Restored));
}

static
ULONG64
CcDirtySlotLimit(
    _In_ PCC_MAP Map,
    _In_ ULONG64 End)
{
    ULONG64 Limit = (ULONG64)(ULONG)CC_ATOMIC_READ32(&Map->LeafCount) << CC_LEAF_SHIFT;
    ULONG64 Wanted = (End == (ULONG64)-1) ? Limit : ((End + CC_VIEW_MASK) >> CC_VIEW_SHIFT);

    return (Wanted < Limit) ? Wanted : Limit;
}

NTSTATUS
CcDirtyFlush(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 FileOffset,
    _In_ ULONG64 Length,
    _In_ ULONG MaximumPages,
    _Out_opt_ PULONG PagesFlushed)
{
    PCC_CACHE Cache = Map->Cache;
    ULONG64 End = (Length == (ULONG64)-1 || FileOffset + Length < FileOffset) ? (ULONG64)-1
                                                                              : FileOffset + Length;
    NTSTATUS FirstError = STATUS_SUCCESS;
    ULONG Flushed = 0;
    ULONG64 Number;

    CC_ATOMIC_ADD32(&Map->FlushesInProgress, 1);

    for (Number = FileOffset >> CC_VIEW_SHIFT;
         Number < CcDirtySlotLimit(Map, End) && Flushed < MaximumPages;
         Number++)
    {
        ULONG64 Bits = CcDirtyTake(Map, Number, FileOffset, End, MaximumPages - Flushed);
        ULONG Page = 0;

        if (Bits == 0)
            continue;

        CcDirtyAccount(Map, -(LONG64)CC_POPCOUNT64(Bits));

        while (Page < CC_VIEW_PAGES)
        {
            ULONG First;
            ULONG64 RunBits;
            NTSTATUS Status;

            if (((Bits >> Page) & 1) == 0)
            {
                Page++;
                continue;
            }

            First = Page;
            while (Page < CC_VIEW_PAGES && ((Bits >> Page) & 1))
                Page++;

            RunBits = CcDirtyBits(First, Page - 1);
            Status = Map->Ops.Flush(Map->Context,
                                    (Number << CC_VIEW_SHIFT) + ((ULONG64)First << PAGE_SHIFT),
                                    (Page - First) << PAGE_SHIFT);
            CC_ATOMIC_ADD64(&Cache->FlushCalls, 1);

            if (!NT_SUCCESS(Status))
            {
                CcDirtyRestore(Map, Number, RunBits);
                if (NT_SUCCESS(FirstError))
                    FirstError = Status;
            }
            else
            {
                Flushed += Page - First;
                CC_ATOMIC_ADD64(&Cache->PagesFlushed, Page - First);
            }
        }
    }

    CC_ATOMIC_ADD32(&Map->FlushesInProgress, -1);

    if (PagesFlushed != NULL)
        *PagesFlushed = Flushed;

    return FirstError;
}

VOID
CcDirtyDiscard(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 FileOffset,
    _In_ ULONG64 Length)
{
    ULONG64 End = (Length == (ULONG64)-1 || FileOffset + Length < FileOffset) ? (ULONG64)-1
                                                                              : FileOffset + Length;
    ULONG64 Start = (FileOffset + PAGE_SIZE - 1) & ~((ULONG64)PAGE_SIZE - 1);
    LONG64 Dropped = 0;
    ULONG64 Number;

    for (Number = Start >> CC_VIEW_SHIFT; Number < CcDirtySlotLimit(Map, End); Number++)
        Dropped += CC_POPCOUNT64(CcDirtyTake(Map, Number, Start, End, 64));

    if (Dropped != 0)
        CcDirtyAccount(Map, -Dropped);
}

BOOLEAN
CcDirtyQuery(
    _In_ PCC_MAP Map,
    _In_ ULONG64 FileOffset,
    _In_ ULONG64 Length)
{
    ULONG64 End = (Length == (ULONG64)-1 || FileOffset + Length < FileOffset) ? (ULONG64)-1
                                                                              : FileOffset + Length;
    BOOLEAN Dirty = FALSE;
    ULONG64 Number;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Map->IndexLock, &OldIrql);

    for (Number = FileOffset >> CC_VIEW_SHIFT; Number < CcDirtySlotLimit(Map, End) && !Dirty; Number++)
    {
        PCC_SLOT Slot = CcMapSlot(Map, Number, FALSE);

        if (Slot == NULL)
        {
            Number |= CC_LEAF_MASK;
            continue;
        }

        Dirty = (Slot->DirtyMap & CcDirtySlotBits(Number, FileOffset, End)) != 0;
    }

    CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);
    return Dirty;
}

BOOLEAN
CcDirtyCanWrite(
    _In_ PCC_CACHE Cache,
    _In_opt_ PCC_MAP Map,
    _In_ ULONG BytesToWrite)
{
    LONG64 Pages = ((LONG64)BytesToWrite + PAGE_SIZE - 1) >> PAGE_SHIFT;

    if (CC_ATOMIC_READ64(&Cache->TotalDirtyPages) + Pages > Cache->DirtyPageThreshold)
        return FALSE;

    if (Map != NULL && Map->DirtyPageThreshold != 0 &&
        CC_ATOMIC_READ64(&Map->DirtyPages) + Pages > Map->DirtyPageThreshold)
    {
        return FALSE;
    }

    return TRUE;
}

PCC_MAP
CcDirtyNextMap(
    _Inout_ PCC_CACHE Cache)
{
    PCC_MAP Map = NULL;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Cache->DirtyLock, &OldIrql);
    if (!IsListEmpty(&Cache->DirtyMaps))
    {
        Map = CONTAINING_RECORD(Cache->DirtyMaps.Flink, CC_MAP, DirtyLink);
        RemoveEntryList(&Map->DirtyLink);
        InsertTailList(&Cache->DirtyMaps, &Map->DirtyLink);
    }
    CC_LOCK_RELEASE(&Cache->DirtyLock, OldIrql);
    return Map;
}

ULONG
CcDirtyLazyTarget(
    _In_ PCC_CACHE Cache)
{
    LONG64 Total = CC_ATOMIC_READ64(&Cache->TotalDirtyPages);
    LONG64 Target;

    if (Total <= 0)
        return 0;

    Target = Total / CC_LAZY_DIVISOR;
    if (Target < CC_LAZY_MIN_PAGES)
        Target = (Total < CC_LAZY_MIN_PAGES) ? Total : CC_LAZY_MIN_PAGES;

    return (ULONG)Target;
}
