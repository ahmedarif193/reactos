/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/engine/bcb.c
 * PURPOSE:     Cache buffer control block management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/ccengine.h"

static
PCC_BCB
CcBcbFind(
    _In_ PCC_MAP Map,
    _In_ ULONG64 FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Pinned)
{
    PLIST_ENTRY Entry;

    for (Entry = Map->BcbList.Flink; Entry != &Map->BcbList; Entry = Entry->Flink)
    {
        PCC_BCB Bcb = CONTAINING_RECORD(Entry, CC_BCB, Link);

        if (Bcb->Pinned == Pinned && Bcb->FileOffset <= FileOffset &&
            Bcb->BeyondLastByte >= FileOffset + Length)
        {
            return Bcb;
        }
    }

    return NULL;
}

static
VOID
CcBcbDestroy(
    _Inout_ PCC_BCB Bcb)
{
    PCC_MAP Map = Bcb->Map;

    CC_ASSERT(Bcb->ReferenceCount == 0 && Bcb->PinCount == 0);

    if (Bcb->OwnBase != NULL)
        Map->Ops.UnmapView(Map->Context, Bcb->OwnBase);
    else if (Bcb->View != NULL)
        CcViewDereference(Bcb->View);

    if (Map->Cache->BcbDeleting != NULL)
        Map->Cache->BcbDeleting(Bcb);

    CC_FREE(Bcb);
}

NTSTATUS
CcBcbAcquire(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 FileOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Pinned,
    _In_ BOOLEAN OnlyExisting,
    _Out_ PCC_BCB *OutBcb,
    _Out_ PBOOLEAN Created)
{
    PCC_CACHE Cache = Map->Cache;
    ULONG Bytes = (Cache->BcbBytes > sizeof(CC_BCB)) ? Cache->BcbBytes : sizeof(CC_BCB);
    CC_VIEW_RANGE Range;
    PCC_BCB Existing;
    PCC_BCB Bcb;
    NTSTATUS Status;
    KIRQL OldIrql;

    *OutBcb = NULL;
    *Created = FALSE;

    if (Length == 0 || FileOffset + Length < FileOffset)
        return STATUS_INVALID_PARAMETER;

    CC_LOCK_ACQUIRE(&Map->BcbLock, &OldIrql);
    Existing = CcBcbFind(Map, FileOffset, Length, Pinned);
    if (Existing != NULL)
        Existing->ReferenceCount++;
    CC_LOCK_RELEASE(&Map->BcbLock, OldIrql);

    if (Existing != NULL)
    {
        *OutBcb = Existing;
        return STATUS_SUCCESS;
    }

    if (OnlyExisting)
        return STATUS_CANT_WAIT;

    Bcb = CC_ALLOCATE(Bytes);
    if (Bcb == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Bcb, Bytes);
    Bcb->Map = Map;
    Bcb->FileOffset = FileOffset;
    Bcb->Length = Length;
    Bcb->BeyondLastByte = FileOffset + Length;
    Bcb->Pinned = Pinned;
    Bcb->ReferenceCount = 1;

    if (Bcb->BeyondLastByte > (ULONG64)CC_ATOMIC_READ64(&Map->SectionSize) && Map->Ops.Extend != NULL)
    {
        Status = Map->Ops.Extend(Map->Context, Bcb->BeyondLastByte);
        if (!NT_SUCCESS(Status))
        {
            CC_FREE(Bcb);
            return Status;
        }

        CC_LOCK_ACQUIRE(&Map->IndexLock, &OldIrql);
        if (Bcb->BeyondLastByte > Map->SectionSize)
            CC_ATOMIC_WRITE64(&Map->SectionSize, Bcb->BeyondLastByte);
        CC_LOCK_RELEASE(&Map->IndexLock, OldIrql);
    }

    if ((FileOffset >> CC_VIEW_SHIFT) == ((FileOffset + Length - 1) >> CC_VIEW_SHIFT))
    {
        Status = CcViewAcquire(Map, FileOffset, Length, &Range);
        if (NT_SUCCESS(Status))
        {
            Bcb->View = Range.View;
            Bcb->BaseAddress = Range.Address;
        }
    }
    else
    {
        Bcb->OwnOffset = FileOffset & ~((ULONG64)CC_BCB_ALIGNMENT - 1);
        Status = Map->Ops.MapView(Map->Context, Bcb->OwnOffset,
                                  (SIZE_T)(FileOffset + Length - Bcb->OwnOffset), &Bcb->OwnBase);
        if (NT_SUCCESS(Status))
            Bcb->BaseAddress = (PUCHAR)Bcb->OwnBase + (SIZE_T)(FileOffset - Bcb->OwnOffset);
    }

    if (!NT_SUCCESS(Status))
    {
        CC_FREE(Bcb);
        return Status;
    }

    if (Cache->BcbCreated != NULL)
        Cache->BcbCreated(Bcb);

    CC_LOCK_ACQUIRE(&Map->BcbLock, &OldIrql);
    Existing = CcBcbFind(Map, FileOffset, Length, Pinned);
    if (Existing != NULL)
        Existing->ReferenceCount++;
    else
        InsertTailList(&Map->BcbList, &Bcb->Link);
    CC_LOCK_RELEASE(&Map->BcbLock, OldIrql);

    if (Existing != NULL)
    {
        Bcb->ReferenceCount = 0;
        CcBcbDestroy(Bcb);
        *OutBcb = Existing;
        return STATUS_SUCCESS;
    }

    *OutBcb = Bcb;
    *Created = TRUE;
    return STATUS_SUCCESS;
}

PVOID
CcBcbAddress(
    _In_ PCC_BCB Bcb,
    _In_ ULONG64 FileOffset)
{
    return (PUCHAR)Bcb->BaseAddress + (SIZE_T)(FileOffset - Bcb->FileOffset);
}

VOID
CcBcbReference(
    _Inout_ PCC_BCB Bcb)
{
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Bcb->Map->BcbLock, &OldIrql);
    Bcb->ReferenceCount++;
    CC_LOCK_RELEASE(&Bcb->Map->BcbLock, OldIrql);
}

VOID
CcBcbDereference(
    _Inout_ PCC_BCB Bcb)
{
    PCC_MAP Map = Bcb->Map;
    BOOLEAN Destroy = FALSE;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Map->BcbLock, &OldIrql);
    CC_ASSERT(Bcb->ReferenceCount > 0);
    Bcb->ReferenceCount--;
    if (Bcb->ReferenceCount == 0 && !Bcb->Dirty)
    {
        RemoveEntryList(&Bcb->Link);
        Destroy = TRUE;
    }
    CC_LOCK_RELEASE(&Map->BcbLock, OldIrql);

    if (Destroy)
        CcBcbDestroy(Bcb);
}

VOID
CcBcbPin(
    _Inout_ PCC_BCB Bcb)
{
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Bcb->Map->BcbLock, &OldIrql);
    Bcb->PinCount++;
    CC_LOCK_RELEASE(&Bcb->Map->BcbLock, OldIrql);
}

VOID
CcBcbUnpin(
    _Inout_ PCC_BCB Bcb)
{
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Bcb->Map->BcbLock, &OldIrql);
    CC_ASSERT(Bcb->PinCount > 0);
    Bcb->PinCount--;
    CC_LOCK_RELEASE(&Bcb->Map->BcbLock, OldIrql);
}

NTSTATUS
CcBcbMakeResident(
    _Inout_ PCC_BCB Bcb,
    _In_ BOOLEAN SkipRead)
{
    PCC_MAP Map = Bcb->Map;
    ULONG64 Offset = Bcb->FileOffset;
    ULONG Remaining = Bcb->Length;

    if (SkipRead)
        return STATUS_SUCCESS;

    while (Remaining != 0)
    {
        ULONG Chunk = CC_VIEW_SIZE - (ULONG)(Offset & CC_VIEW_MASK);
        NTSTATUS Status;

        if (Chunk > Remaining)
            Chunk = Remaining;

        if (!Map->Ops.IsResident(Map->Context, Offset, Chunk))
        {
            Status = Map->Ops.MakeResident(Map->Context, Offset, Chunk, Map->ValidDataLength);
            if (!NT_SUCCESS(Status))
                return Status;
        }

        Offset += Chunk;
        Remaining -= Chunk;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
CcBcbSetDirty(
    _Inout_ PCC_BCB Bcb,
    _In_ ULONG64 Lsn)
{
    PCC_MAP Map = Bcb->Map;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Map->BcbLock, &OldIrql);
    Bcb->Dirty = TRUE;
    Bcb->DirtyGeneration = ++Map->BcbGeneration;
    if (Lsn != 0)
    {
        if (Bcb->OldestLsn == 0 || Lsn < Bcb->OldestLsn)
            Bcb->OldestLsn = Lsn;
        if (Lsn > Bcb->NewestLsn)
            Bcb->NewestLsn = Lsn;
    }
    CC_LOCK_RELEASE(&Map->BcbLock, OldIrql);

    return CcDirtyMark(Map, Bcb->FileOffset, Bcb->Length);
}

ULONG64
CcBcbBeginFlush(
    _Inout_ PCC_MAP Map)
{
    ULONG64 Generation;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Map->BcbLock, &OldIrql);
    Generation = Map->BcbGeneration;
    CC_LOCK_RELEASE(&Map->BcbLock, OldIrql);
    return Generation;
}

BOOLEAN
CcBcbCompleteFlush(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 Start,
    _In_ ULONG64 End,
    _In_ ULONG64 Generation)
{
    LIST_ENTRY Retired;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;
    BOOLEAN StillDirty = FALSE;
    KIRQL OldIrql;

    InitializeListHead(&Retired);
    CC_LOCK_ACQUIRE(&Map->BcbLock, &OldIrql);

    for (Entry = Map->BcbList.Flink; Entry != &Map->BcbList; Entry = Next)
    {
        PCC_BCB Bcb = CONTAINING_RECORD(Entry, CC_BCB, Link);

        Next = Entry->Flink;
        if (Bcb->FileOffset < Start || Bcb->FileOffset + Bcb->Length > End)
            continue;

        if (Bcb->Dirty && Bcb->PinCount == 0 && Bcb->DirtyGeneration <= Generation)
        {
            Bcb->Dirty = FALSE;
            Bcb->OldestLsn = 0;
            Bcb->NewestLsn = 0;

            if (Bcb->ReferenceCount == 0)
            {
                RemoveEntryList(Entry);
                InsertTailList(&Retired, Entry);
            }
        }

        StillDirty |= Bcb->Dirty;
    }

    CC_LOCK_RELEASE(&Map->BcbLock, OldIrql);

    while (!IsListEmpty(&Retired))
    {
        Entry = Retired.Flink;
        RemoveEntryList(Entry);
        CcBcbDestroy(CONTAINING_RECORD(Entry, CC_BCB, Link));
    }

    return StillDirty;
}

BOOLEAN
CcBcbRangeBusy(
    _Inout_ PCC_MAP Map,
    _In_ ULONG64 Start,
    _In_ ULONG64 End)
{
    BOOLEAN Busy = FALSE;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&Map->BcbLock, &OldIrql);

    for (Entry = Map->BcbList.Flink; Entry != &Map->BcbList; Entry = Entry->Flink)
    {
        PCC_BCB Bcb = CONTAINING_RECORD(Entry, CC_BCB, Link);

        if (Bcb->FileOffset < End && Bcb->FileOffset + Bcb->Length > Start &&
            (Bcb->ReferenceCount != 0 || Bcb->PinCount != 0))
        {
            Busy = TRUE;
            break;
        }
    }

    CC_LOCK_RELEASE(&Map->BcbLock, OldIrql);
    return Busy;
}

BOOLEAN
CcBcbDirtyLsns(
    _Inout_ PCC_MAP Map,
    _Out_ PULONG64 OldestLsn,
    _Out_ PULONG64 NewestLsn,
    _Out_ PULONG DirtyBcbs)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    *OldestLsn = 0;
    *NewestLsn = 0;
    *DirtyBcbs = 0;

    CC_LOCK_ACQUIRE(&Map->BcbLock, &OldIrql);

    for (Entry = Map->BcbList.Flink; Entry != &Map->BcbList; Entry = Entry->Flink)
    {
        PCC_BCB Bcb = CONTAINING_RECORD(Entry, CC_BCB, Link);

        if (!Bcb->Dirty)
            continue;

        (*DirtyBcbs)++;
        if (Bcb->OldestLsn != 0 && (*OldestLsn == 0 || Bcb->OldestLsn < *OldestLsn))
            *OldestLsn = Bcb->OldestLsn;
        if (Bcb->NewestLsn > *NewestLsn)
            *NewestLsn = Bcb->NewestLsn;
    }

    CC_LOCK_RELEASE(&Map->BcbLock, OldIrql);
    return (*DirtyBcbs != 0);
}
