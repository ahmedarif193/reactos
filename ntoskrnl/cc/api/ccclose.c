/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/api/ccclose.c
 * PURPOSE:     Cache map teardown and cached-view reclamation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccnt.h"

KSPIN_LOCK CcNtMapListLock;
LIST_ENTRY CcNtMapList;
static ULONG CcNtClosedMapCount;

PCC_NT_MAP
CcNtReferenceMap(
    _In_ PSECTION_OBJECT_POINTERS Pointers)
{
    PCC_NT_MAP NtMap;
    KIRQL OldIrql;

    if (Pointers == NULL)
        return NULL;

    CC_LOCK_ACQUIRE(&CcNtMapListLock, &OldIrql);
    NtMap = Pointers->SharedCacheMap;
    if (NtMap != NULL)
    {
        if (NtMap->Closing)
        {
            NtMap->Closing = FALSE;
            CcNtClosedMapCount--;
        }
        CC_ATOMIC_ADD32(&NtMap->ReferenceCount, 1);
    }
    CC_LOCK_RELEASE(&CcNtMapListLock, OldIrql);

    return NtMap;
}

static
VOID
CcNtUnlinkMap(
    _Inout_ PCC_NT_MAP NtMap)
{
    if (NtMap->Pointers->SharedCacheMap == NtMap)
        NtMap->Pointers->SharedCacheMap = NULL;
    if (NtMap->Closing)
    {
        NtMap->Closing = FALSE;
        CcNtClosedMapCount--;
    }
    RemoveEntryList(&NtMap->Link);
    InitializeListHead(&NtMap->Link);
}

VOID
CcNtDereferenceMap(
    _Inout_ PCC_NT_MAP NtMap)
{
    BOOLEAN Destroy = FALSE;
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&CcNtMapListLock, &OldIrql);
    if (CC_ATOMIC_ADD32(&NtMap->ReferenceCount, -1) == 1 && NtMap->Map.OpenCount == 0 &&
        CC_ATOMIC_READ64(&NtMap->Map.DirtyPages) == 0)
    {
        if (NtMap->UninitializeEvent == NULL && !NtMap->PurgeOnClose && NtMap->Map.FileSize != 0 &&
            (NtMap->Closing || CcNtClosedMapCount < 128))
        {
            if (!NtMap->Closing)
            {
                NtMap->Closing = TRUE;
                CcNtClosedMapCount++;
            }
        }
        else
        {
            CcNtUnlinkMap(NtMap);
            Destroy = TRUE;
        }
    }
    CC_LOCK_RELEASE(&CcNtMapListLock, OldIrql);

    if (Destroy)
        CcNtDestroyMap(NtMap);
}

VOID
CcNtExpireClosedMaps(VOID)
{
    ULONG Remaining = 128;

    while (Remaining-- != 0)
    {
        PCC_NT_MAP Found = NULL;
        PLIST_ENTRY Entry;
        KIRQL OldIrql;

        CC_LOCK_ACQUIRE(&CcNtMapListLock, &OldIrql);
        for (Entry = CcNtMapList.Flink; Entry != &CcNtMapList; Entry = Entry->Flink)
        {
            PCC_NT_MAP NtMap = CONTAINING_RECORD(Entry, CC_NT_MAP, Link);

            if (NtMap->Closing && NtMap->ReferenceCount == 0 && NtMap->Map.OpenCount == 0 &&
                CC_ATOMIC_READ64(&NtMap->Map.DirtyPages) == 0)
            {
                CcNtUnlinkMap(NtMap);
                Found = NtMap;
                break;
            }
        }
        CC_LOCK_RELEASE(&CcNtMapListLock, OldIrql);
        if (Found == NULL)
            break;
        CcNtDestroyMap(Found);
    }
}

VOID
CcNtMapListBarrier(VOID)
{
    KIRQL OldIrql;

    CC_LOCK_ACQUIRE(&CcNtMapListLock, &OldIrql);
    CC_LOCK_RELEASE(&CcNtMapListLock, OldIrql);
}

static
VOID
CcNtCompleteDestroyMap(
    _In_opt_ PVOID Context)
{
    PCC_NT_MAP NtMap = Context;
    PMI_SEGMENT Segment = NtMap->Control->Segment;
    ULONG64 FileSize = NtMap->Map.FileSize;
    ULONG64 FlushSize = FileSize;
    KEVENT *Event = NtMap->UninitializeEvent;
    BOOLEAN Uninitialized;
    PLIST_ENTRY Entry;

    for (Entry = NtMap->Map.BcbList.Flink; Entry != &NtMap->Map.BcbList; Entry = Entry->Flink)
    {
        PCC_BCB Bcb = CONTAINING_RECORD(Entry, CC_BCB, Link);

        if (Bcb->Dirty)
        {
            CcDirtyMark(&NtMap->Map, Bcb->FileOffset, Bcb->Length);
            if (Bcb->FileOffset + Bcb->Length > FlushSize)
                FlushSize = Bcb->FileOffset + Bcb->Length;
        }
    }

    if (FlushSize != 0)
        CcNtFlushMap(NtMap, 0, FlushSize, ~0u, NULL);

    Uninitialized = CcMapUninitialize(&NtMap->Map);
    ASSERT(Uninitialized);
    MiSegmentPurge(Segment, ROUND_UP(FileSize, (ULONG64)PAGE_SIZE), 0);
    MiSegmentFlush(Segment, 0, FileSize);
    if (Event != NULL || FileSize == 0 || NtMap->PurgeOnClose)
        MiSegmentDereferenceAndClose(Segment);
    else
        MiSegmentDereference(Segment);

    ObDereferenceObject(NtMap->FileObject);
    CcNtMapListBarrier();
    ExFreePoolWithTag(NtMap, CC_NT_MAP_TAG);

    if (Event != NULL)
        KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
}

static
VOID
CcNtDrainMapReads(
    _In_opt_ PVOID Context)
{
    PCC_NT_MAP NtMap = Context;

    NtMap->ReadDrain.Complete = CcNtCompleteDestroyMap;
    NtMap->ReadDrain.Context = NtMap;
    MiSegmentDrainReads(NtMap->Control->Segment, &NtMap->ReadDrain);
}

VOID
CcNtDestroyMap(
    _Inout_ PCC_NT_MAP NtMap)
{
    CcMapDrainReclaims(&NtMap->Map, CcNtDrainMapReads, NtMap);
}
