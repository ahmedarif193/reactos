/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/api/ccclose.c
 * PURPOSE:     Cache map teardown and cached-view reclamation
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccnt.h"

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
    if (Event != NULL || FileSize == 0)
        MiSegmentDereferenceAndClose(Segment);
    else
        MiSegmentDereference(Segment);

    ObDereferenceObject(NtMap->FileObject);
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
