/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/api/ccflush.c
 * PURPOSE:     Cache flushing and dirty-data writeback interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccnt.h"

VOID
NTAPI
CcFlushCache(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer,
    _In_opt_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _Out_opt_ PIO_STATUS_BLOCK IoStatus)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(SectionObjectPointer);
    ULONG64 Offset = (FileOffset != NULL) ? (ULONG64)FileOffset->QuadPart : 0;
    ULONG64 Bytes = (FileOffset != NULL) ? Length : (ULONG64)-1;
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Flushed = 0;

    if (NtMap != NULL)
    {
        Status = CcNtFlushMap(NtMap, Offset, Bytes, ~0u, &Flushed);

        if (NT_SUCCESS(Status))
        {
            ULONG64 SegmentBytes = (Bytes == (ULONG64)-1)
                                       ? (ULONG64)MI_ATOMIC_READ64(&NtMap->Control->Segment->SizeInBytes) - Offset
                                       : Bytes;

            Status = MiSegmentFlush(NtMap->Control->Segment, Offset, SegmentBytes);
        }

        CcNtDereferenceMap(NtMap);
    }
    else
    {
        PMI_CONTROL_AREA Control = MiReferenceDataControlArea(SectionObjectPointer);

        if (Control != NULL)
        {
            ULONG64 Size = (ULONG64)MI_ATOMIC_READ64(&Control->Segment->SizeInBytes);

            if (Offset < Size)
                Status = MiSegmentFlush(Control->Segment, Offset, (Bytes == (ULONG64)-1) ? Size - Offset : Bytes);

            MiDereferenceControlArea(Control);
        }
    }

    if (IoStatus != NULL)
    {
        IoStatus->Status = Status;
        IoStatus->Information = (ULONG_PTR)Flushed << PAGE_SHIFT;
    }
}

BOOLEAN
NTAPI
CcPurgeCacheSection(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer,
    _In_opt_ PLARGE_INTEGER FileOffset,
    _In_ ULONG Length,
    _In_ ULONG Flags)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(SectionObjectPointer);
    ULONG64 Offset = (FileOffset != NULL) ? (ULONG64)FileOffset->QuadPart : 0;
    ULONG64 Bytes = (FileOffset != NULL && Length != 0) ? Length : 0;
    BOOLEAN Purged = TRUE;

    if (NtMap != NULL)
    {
        ULONG64 End = (Bytes == 0) ? (ULONG64)-1 : Offset + Bytes;

        if (CcBcbRangeBusy(&NtMap->Map, Offset, End))
        {
            CcNtDereferenceMap(NtMap);
            return FALSE;
        }

        CcDirtyDiscard(&NtMap->Map, Offset, (Bytes == 0) ? (ULONG64)-1 : Bytes);
        CcMapDetachViews(&NtMap->Map, Offset, (Bytes == 0) ? (ULONG64)-1 : Bytes);
        Purged = MiSegmentPurge(NtMap->Control->Segment, Offset, Bytes);
        if (Purged && Offset == 0 && Bytes == 0 && (Flags & UNINITIALIZE_CACHE_MAPS))
            NtMap->PurgeOnClose = TRUE;
        CcNtDereferenceMap(NtMap);
    }
    else
    {
        PMI_CONTROL_AREA Control = MiReferenceDataControlArea(SectionObjectPointer);

        if (Control != NULL)
        {
            Purged = MiSegmentPurge(Control->Segment, Offset, Bytes);
            if (Purged && Offset == 0 && Bytes == 0)
                MiSegmentDereferenceAndClose(Control->Segment);
            else
                MiDereferenceControlArea(Control);
        }
    }

    return Purged;
}
