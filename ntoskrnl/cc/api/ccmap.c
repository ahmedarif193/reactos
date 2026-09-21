/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/api/ccmap.c
 * PURPOSE:     Cached file mapping interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccnt.h"

VOID
NTAPI
CcInitializeCacheMap(
    _In_ PFILE_OBJECT FileObject,
    _In_ PCC_FILE_SIZES FileSizes,
    _In_ BOOLEAN PinAccess,
    _In_ PCACHE_MANAGER_CALLBACKS Callbacks,
    _In_ PVOID LazyWriteContext)
{
    KIRQL OldIrql;
    PSECTION_OBJECT_POINTERS Pointers = FileObject->SectionObjectPointer;
    PCC_NT_PRIVATE Private;
    PCC_NT_MAP NtMap;
    PCC_NT_MAP Fresh = NULL;
    ULONG64 SectionSize;
    NTSTATUS Status;

    ASSERT(Pointers != NULL);

    if (FileObject->PrivateCacheMap != NULL)
        return;

    NtMap = CcNtReferenceMap(Pointers);

    if (NtMap == NULL)
    {
        Fresh = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Fresh), CC_NT_MAP_TAG);
        if (Fresh == NULL)
            ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);

        RtlZeroMemory(Fresh, sizeof(*Fresh));
        Fresh->Pointers = Pointers;
        Fresh->PinAccess = PinAccess;
        Fresh->Callbacks = *Callbacks;
        Fresh->LazyWriteContext = LazyWriteContext;
        Fresh->AllocationSize = FileSizes->AllocationSize;
        Fresh->ReferenceCount = 1;

        SectionSize = (ULONG64)max(FileSizes->AllocationSize.QuadPart, FileSizes->FileSize.QuadPart);
        if (SectionSize == 0)
            SectionSize = PAGE_SIZE;

        Status = MiCreateDataControlArea(FileObject, SectionSize, &Fresh->Control);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Fresh, CC_NT_MAP_TAG);
            ExRaiseStatus(Status);
        }

        ObReferenceObject(FileObject);
        Fresh->FileObject = FileObject;
        CcMapInitialize(&Fresh->Map, &CcNtCache, &CcNtBackingOps, Fresh, SectionSize,
                        (ULONG64)FileSizes->FileSize.QuadPart, (ULONG64)FileSizes->ValidDataLength.QuadPart);
        Fresh->Map.NodeByteSize = sizeof(*Fresh);

        KeAcquireSpinLock(&CcNtMapListLock, &OldIrql);
        if (Pointers->SharedCacheMap == NULL)
        {
            Pointers->SharedCacheMap = Fresh;
            InsertTailList(&CcNtMapList, &Fresh->Link);
            NtMap = Fresh;
            Fresh = NULL;
        }
        else
        {
            NtMap = Pointers->SharedCacheMap;
            InterlockedIncrement(&NtMap->ReferenceCount);
        }
        KeReleaseSpinLock(&CcNtMapListLock, OldIrql);

        if (Fresh != NULL)
        {
            CcMapUninitialize(&Fresh->Map);
            MiDereferenceControlArea(Fresh->Control);
            ObDereferenceObject(Fresh->FileObject);
            ExFreePoolWithTag(Fresh, CC_NT_MAP_TAG);
        }
    }

    Private = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Private), CC_NT_PRIVATE_TAG);
    if (Private == NULL)
    {
        CcNtDereferenceMap(NtMap);
        ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
    }

    CcReadAheadInitialize(&Private->ReadAhead);
    Private->FileObject = FileObject;

    InterlockedIncrement(&NtMap->Map.OpenCount);
    FileObject->PrivateCacheMap = Private;
    CcNtDereferenceMap(NtMap);
}

BOOLEAN
NTAPI
CcUninitializeCacheMap(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PLARGE_INTEGER TruncateSize,
    _In_opt_ PCACHE_UNINITIALIZE_EVENT UninitializeEvent)
{
    PCC_NT_PRIVATE Private = FileObject->PrivateCacheMap;
    PCC_NT_MAP NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);

    if (NtMap == NULL)
    {
        if (UninitializeEvent != NULL)
            KeSetEvent(&UninitializeEvent->Event, IO_NO_INCREMENT, FALSE);

        return FALSE;
    }

    if (TruncateSize != NULL)
    {
        ULONG64 NewSize = (ULONG64)TruncateSize->QuadPart;

        if (NewSize < NtMap->Map.FileSize)
        {
            CcDirtyDiscard(&NtMap->Map, NewSize, (ULONG64)-1);
            CcMapSetSizes(&NtMap->Map, NtMap->Map.SectionSize, NewSize, min(NtMap->Map.ValidDataLength, NewSize));
        }
    }

    if (Private != NULL)
    {
        FileObject->PrivateCacheMap = NULL;
        ExFreePoolWithTag(Private, CC_NT_PRIVATE_TAG);

        if (UninitializeEvent != NULL && NtMap->Map.OpenCount == 1)
            NtMap->UninitializeEvent = &UninitializeEvent->Event;
        else if (UninitializeEvent != NULL)
            KeSetEvent(&UninitializeEvent->Event, IO_NO_INCREMENT, FALSE);

        InterlockedDecrement(&NtMap->Map.OpenCount);
    }
    else if (UninitializeEvent != NULL)
    {
        KeSetEvent(&UninitializeEvent->Event, IO_NO_INCREMENT, FALSE);
    }

    CcNtDereferenceMap(NtMap);
    return TRUE;
}

VOID
NTAPI
CcSetFileSizes(
    _In_ PFILE_OBJECT FileObject,
    _In_ PCC_FILE_SIZES FileSizes)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);
    ULONG64 NewFileSize = (ULONG64)FileSizes->FileSize.QuadPart;
    ULONG64 SectionSize;

    if (NtMap == NULL)
        return;

    SectionSize = (ULONG64)max(FileSizes->AllocationSize.QuadPart, FileSizes->FileSize.QuadPart);
    if (SectionSize < NtMap->Map.SectionSize)
        SectionSize = NtMap->Map.SectionSize;

    if (SectionSize > NtMap->Map.SectionSize)
    {
        NTSTATUS Status = MiSegmentExtend(NtMap->Control->Segment, SectionSize);

        if (!NT_SUCCESS(Status))
        {
            CcNtDereferenceMap(NtMap);
            ExRaiseStatus(Status);
        }
    }

    if (NewFileSize < NtMap->Map.FileSize)
    {
        ULONG64 Boundary = ROUND_TO_PAGES(NewFileSize);

        CcDirtyDiscard(&NtMap->Map, Boundary, (ULONG64)-1);
        CcMapDetachViews(&NtMap->Map, ROUND_UP(NewFileSize, CC_VIEW_SIZE), (ULONG64)-1);
        MiSegmentPurge(NtMap->Control->Segment, Boundary, 0);
    }

    NtMap->AllocationSize = FileSizes->AllocationSize;
    CcMapSetSizes(&NtMap->Map, SectionSize, NewFileSize, (ULONG64)FileSizes->ValidDataLength.QuadPart);
    CcNtDereferenceMap(NtMap);
}

PFILE_OBJECT
NTAPI
CcGetFileObjectFromSectionPtrs(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(SectionObjectPointer);
    PFILE_OBJECT FileObject = NULL;

    if (NtMap != NULL)
    {
        FileObject = NtMap->FileObject;
        CcNtDereferenceMap(NtMap);
    }

    return FileObject;
}

PFILE_OBJECT
NTAPI
CcGetFileObjectFromBcb(
    _In_ PVOID Bcb)
{
    PCC_NT_BCB NtBcb = CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb);

    return CONTAINING_RECORD(NtBcb->Engine.Map, CC_NT_MAP, Map)->FileObject;
}

VOID
NTAPI
CcSetLogHandleForFile(
    _In_ PFILE_OBJECT FileObject,
    _In_ PVOID LogHandle,
    _In_ PFLUSH_TO_LSN FlushToLsnRoutine)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);

    if (NtMap == NULL)
        return;

    NtMap->LogHandle = LogHandle;
    NtMap->FlushToLsn = FlushToLsnRoutine;
    CcNtDereferenceMap(NtMap);
}

LARGE_INTEGER
NTAPI
CcGetLsnForFileObject(
    _In_ PFILE_OBJECT FileObject,
    _Out_opt_ PLARGE_INTEGER OldestLsn)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);
    LARGE_INTEGER Result;
    ULONG64 Oldest = 0, Newest = 0;
    ULONG DirtyBcbs;

    if (NtMap != NULL)
    {
        CcBcbDirtyLsns(&NtMap->Map, &Oldest, &Newest, &DirtyBcbs);
        CcNtDereferenceMap(NtMap);
    }

    if (OldestLsn != NULL)
        OldestLsn->QuadPart = (LONGLONG)Oldest;

    Result.QuadPart = (LONGLONG)Newest;
    return Result;
}

LARGE_INTEGER
NTAPI
CcGetDirtyPages(
    _In_ PVOID LogHandle,
    _In_ PDIRTY_PAGE_ROUTINE DirtyPageRoutine,
    _In_ PVOID Context1,
    _In_ PVOID Context2)
{
    LARGE_INTEGER Result;
    ULONG64 OldestOverall = 0;
    PCC_NT_MAP Previous = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&CcNtMapListLock, &OldIrql);
    Entry = CcNtMapList.Flink;

    while (Entry != &CcNtMapList)
    {
        PCC_NT_MAP NtMap = CONTAINING_RECORD(Entry, CC_NT_MAP, Link);
        ULONG64 Oldest, Newest;
        ULONG DirtyBcbs;

        if (NtMap->LogHandle != LogHandle)
        {
            Entry = Entry->Flink;
            continue;
        }

        InterlockedIncrement(&NtMap->ReferenceCount);
        KeReleaseSpinLock(&CcNtMapListLock, OldIrql);

        if (Previous != NULL)
            CcNtDereferenceMap(Previous);

        Previous = NtMap;

        if (CcBcbDirtyLsns(&NtMap->Map, &Oldest, &Newest, &DirtyBcbs) && DirtyBcbs != 0)
        {
            LARGE_INTEGER Offset, OldestLsn, NewestLsn;

            Offset.QuadPart = 0;
            OldestLsn.QuadPart = (LONGLONG)Oldest;
            NewestLsn.QuadPart = (LONGLONG)Newest;
            DirtyPageRoutine(NtMap->FileObject, &Offset, (ULONG)min(NtMap->Map.FileSize, 0xFFFFFFFFULL),
                             &OldestLsn, &NewestLsn, Context1, Context2);

            if (Oldest != 0 && (OldestOverall == 0 || Oldest < OldestOverall))
                OldestOverall = Oldest;
        }

        KeAcquireSpinLock(&CcNtMapListLock, &OldIrql);
        Entry = NtMap->Link.Flink;
    }

    KeReleaseSpinLock(&CcNtMapListLock, OldIrql);

    if (Previous != NULL)
        CcNtDereferenceMap(Previous);

    Result.QuadPart = (LONGLONG)OldestOverall;
    return Result;
}

BOOLEAN
NTAPI
CcIsThereDirtyData(
    _In_ PVPB Vpb)
{
    KIRQL OldIrql;
    BOOLEAN Dirty = FALSE;
    PLIST_ENTRY Entry;

    KeAcquireSpinLock(&CcNtMapListLock, &OldIrql);

    for (Entry = CcNtMapList.Flink; Entry != &CcNtMapList && !Dirty; Entry = Entry->Flink)
    {
        PCC_NT_MAP NtMap = CONTAINING_RECORD(Entry, CC_NT_MAP, Link);

        if (NtMap->FileObject->Vpb == Vpb && !(NtMap->FileObject->Flags & FO_TEMPORARY_FILE) &&
            CC_ATOMIC_READ64(&NtMap->Map.DirtyPages) != 0)
        {
            Dirty = TRUE;
        }
    }

    KeReleaseSpinLock(&CcNtMapListLock, OldIrql);
    return Dirty;
}

VOID
NTAPI
CcSetAdditionalCacheAttributes(
    _In_ PFILE_OBJECT FileObject,
    _In_ BOOLEAN DisableReadAhead,
    _In_ BOOLEAN DisableWriteBehind)
{
    PCC_NT_PRIVATE Private = FileObject->PrivateCacheMap;

    UNREFERENCED_PARAMETER(DisableWriteBehind);

    if (Private != NULL)
        Private->ReadAhead.Disabled = DisableReadAhead;
}

VOID
NTAPI
CcSetAdditionalCacheAttributesEx(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(FileObject);
    UNREFERENCED_PARAMETER(Flags);
}

VOID
NTAPI
CcSetReadAheadGranularity(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG Granularity)
{
    PCC_NT_PRIVATE Private = FileObject->PrivateCacheMap;

    if (Private != NULL && Granularity >= PAGE_SIZE)
        Private->ReadAhead.Granularity = Granularity;
}

VOID
NTAPI
CcSetDirtyPageThreshold(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG DirtyPageThreshold)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(FileObject->SectionObjectPointer);

    if (NtMap == NULL)
        return;

    NtMap->Map.DirtyPageThreshold = DirtyPageThreshold;
    CcNtDereferenceMap(NtMap);
}

LARGE_INTEGER
NTAPI
CcGetFlushedValidData(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer,
    _In_ BOOLEAN BcbListHeld)
{
    PCC_NT_MAP NtMap = CcNtReferenceMap(SectionObjectPointer);
    LARGE_INTEGER Result;

    UNREFERENCED_PARAMETER(BcbListHeld);

    Result.QuadPart = MAXLONGLONG;

    if (NtMap != NULL)
    {
        if (CC_ATOMIC_READ64(&NtMap->Map.DirtyPages) != 0)
            Result.QuadPart = 0;

        CcNtDereferenceMap(NtMap);
    }

    return Result;
}
