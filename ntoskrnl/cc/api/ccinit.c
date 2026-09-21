/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/api/ccinit.c
 * PURPOSE:     Cache manager and file cache initialization
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccnt.h"

typedef struct _CC_NT_DEFERRED_WRITE
{
    LIST_ENTRY Link;
    PFILE_OBJECT FileObject;
    PCC_POST_DEFERRED_WRITE PostRoutine;
    PVOID Context1;
    PVOID Context2;
    ULONG BytesToWrite;
} CC_NT_DEFERRED_WRITE, *PCC_NT_DEFERRED_WRITE;

CC_CACHE CcNtCache;
KSPIN_LOCK CcNtMapListLock;
LIST_ENTRY CcNtMapList;

ULONG CcLazyWritePages;
ULONG CcLazyWriteIos;
ULONG CcMapDataWait;
ULONG CcMapDataNoWait;
ULONG CcPinReadWait;
ULONG CcPinReadNoWait;
ULONG CcPinMappedDataCount;
ULONG CcDataPages;
ULONG CcDataFlushes;
ULONG CcFastMdlReadNotPossible;
ULONG CcFastMdlReadWait;
ULONG CcFastReadNoWait;
ULONG CcFastReadNotPossible;
ULONG CcFastReadResourceMiss;
ULONG CcFastReadWait;

static KEVENT CcNtLazyWriterEvent;
static KEVENT CcNtLazyPassEvent;
static LIST_ENTRY CcNtDeferredWrites;
static KSPIN_LOCK CcNtDeferredLock;
static BOOLEAN CcNtReady;

PCC_NT_MAP
CcNtReferenceMap(
    _In_ PSECTION_OBJECT_POINTERS Pointers)
{
    PCC_NT_MAP NtMap;
    KIRQL OldIrql;

    if (Pointers == NULL)
        return NULL;

    KeAcquireSpinLock(&CcNtMapListLock, &OldIrql);
    NtMap = Pointers->SharedCacheMap;
    if (NtMap != NULL)
        InterlockedIncrement(&NtMap->ReferenceCount);
    KeReleaseSpinLock(&CcNtMapListLock, OldIrql);

    return NtMap;
}

NTSTATUS
CcNtFlushMap(
    _Inout_ PCC_NT_MAP NtMap,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length,
    _In_ ULONG MaximumPages,
    _Out_opt_ PULONG PagesFlushed)
{
    ULONG64 End = (Length == (ULONG64)-1 || Offset + Length < Offset) ? (ULONG64)-1 : Offset + Length;
    ULONG64 Generation = CcBcbBeginFlush(&NtMap->Map);
    ULONG Flushed = 0;
    NTSTATUS Status;

    if (NtMap->FlushToLsn != NULL && NtMap->LogHandle != NULL)
    {
        ULONG64 Oldest, Newest;
        ULONG DirtyBcbs;

        if (CcBcbDirtyLsns(&NtMap->Map, &Oldest, &Newest, &DirtyBcbs) && Newest != 0)
        {
            LARGE_INTEGER Lsn;

            Lsn.QuadPart = (LONGLONG)Newest;
            NtMap->FlushToLsn(NtMap->LogHandle, Lsn);
        }
    }

    Status = CcDirtyFlush(&NtMap->Map, Offset, Length, MaximumPages, &Flushed);

    if (NT_SUCCESS(Status) && MaximumPages == ~0u)
        CcBcbCompleteFlush(&NtMap->Map, Offset, End, Generation);

    if (Flushed != 0)
    {
        CcDataFlushes++;
        CcDataPages += Flushed;
    }

    if (PagesFlushed != NULL)
        *PagesFlushed = Flushed;

    return Status;
}

VOID
CcNtDereferenceMap(
    _Inout_ PCC_NT_MAP NtMap)
{
    BOOLEAN Destroy = FALSE;
    KIRQL OldIrql;

    KeAcquireSpinLock(&CcNtMapListLock, &OldIrql);

    if (InterlockedDecrement(&NtMap->ReferenceCount) == 0 && NtMap->Map.OpenCount == 0)
    {
        if (NtMap->Pointers->SharedCacheMap == NtMap)
            NtMap->Pointers->SharedCacheMap = NULL;

        RemoveEntryList(&NtMap->Link);
        Destroy = TRUE;
    }

    KeReleaseSpinLock(&CcNtMapListLock, OldIrql);

    if (Destroy)
        CcNtDestroyMap(NtMap);
}

VOID
CcNtKickLazyWriter(VOID)
{
    if (CcNtReady)
        KeSetEvent(&CcNtLazyWriterEvent, IO_NO_INCREMENT, FALSE);
}

VOID
CcNtProcessDeferredWrites(VOID)
{
    for (;;)
    {
        PCC_NT_DEFERRED_WRITE Deferred = NULL;
        KIRQL OldIrql;

        KeAcquireSpinLock(&CcNtDeferredLock, &OldIrql);

        if (!IsListEmpty(&CcNtDeferredWrites))
        {
            PCC_NT_DEFERRED_WRITE First = CONTAINING_RECORD(CcNtDeferredWrites.Flink, CC_NT_DEFERRED_WRITE, Link);

            if (CcDirtyCanWrite(&CcNtCache, NULL, First->BytesToWrite))
            {
                RemoveEntryList(&First->Link);
                Deferred = First;
            }
        }

        KeReleaseSpinLock(&CcNtDeferredLock, OldIrql);

        if (Deferred == NULL)
            return;

        Deferred->PostRoutine(Deferred->Context1, Deferred->Context2);
        ExFreePoolWithTag(Deferred, 'wDcC');
    }
}

VOID
NTAPI
CcDeferWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_ PCC_POST_DEFERRED_WRITE PostRoutine,
    _In_ PVOID Context1,
    _In_ PVOID Context2,
    _In_ ULONG BytesToWrite,
    _In_ BOOLEAN Retrying)
{
    PCC_NT_DEFERRED_WRITE Deferred = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Deferred), 'wDcC');
    KIRQL OldIrql;

    if (Deferred == NULL)
    {
        PostRoutine(Context1, Context2);
        return;
    }

    Deferred->FileObject = FileObject;
    Deferred->PostRoutine = PostRoutine;
    Deferred->Context1 = Context1;
    Deferred->Context2 = Context2;
    Deferred->BytesToWrite = BytesToWrite;

    KeAcquireSpinLock(&CcNtDeferredLock, &OldIrql);
    if (Retrying)
        InsertHeadList(&CcNtDeferredWrites, &Deferred->Link);
    else
        InsertTailList(&CcNtDeferredWrites, &Deferred->Link);
    KeReleaseSpinLock(&CcNtDeferredLock, OldIrql);

    CcNtKickLazyWriter();
}

static
PCC_NT_MAP
CcNtNextDirtyMap(VOID)
{
    PCC_NT_MAP Found = NULL;
    PCC_MAP Map;
    KIRQL OldIrql;

    KeAcquireSpinLock(&CcNtMapListLock, &OldIrql);

    Map = CcDirtyNextMap(&CcNtCache);
    if (Map != NULL)
    {
        Found = CONTAINING_RECORD(Map, CC_NT_MAP, Map);
        InterlockedIncrement(&Found->ReferenceCount);
    }

    KeReleaseSpinLock(&CcNtMapListLock, OldIrql);
    return Found;
}

NTSTATUS
CcRosFlushDirtyPages(
    ULONG Target,
    PULONG Count,
    BOOLEAN Wait,
    BOOLEAN CalledFromLazy)
{
    ULONG Total = 0;
    ULONG Visited = 0;

    UNREFERENCED_PARAMETER(Wait);

    while (Total < Target && Visited++ < 256)
    {
        PCC_NT_MAP NtMap = CcNtNextDirtyMap();
        ULONG Flushed = 0;

        if (NtMap == NULL)
            break;

        if (NtMap->Callbacks.AcquireForLazyWrite == NULL ||
            NtMap->Callbacks.AcquireForLazyWrite(NtMap->LazyWriteContext, Wait))
        {
            CcNtFlushMap(NtMap, 0, (ULONG64)-1, Target - Total, &Flushed);

            if (NtMap->Callbacks.ReleaseFromLazyWrite != NULL)
                NtMap->Callbacks.ReleaseFromLazyWrite(NtMap->LazyWriteContext);
        }

        if (CalledFromLazy && Flushed != 0)
        {
            CcLazyWriteIos++;
            CcLazyWritePages += Flushed;
        }

        Total += Flushed;
        CcNtDereferenceMap(NtMap);
    }

    if (Count != NULL)
        *Count = Total;

    return STATUS_SUCCESS;
}

static
VOID
NTAPI
CcNtLazyWriter(
    _In_ PVOID Context)
{
    LARGE_INTEGER Period;

    UNREFERENCED_PARAMETER(Context);
    Period.QuadPart = -10 * 1000 * 1000;

    for (;;)
    {
        ULONG Target;

        KeWaitForSingleObject(&CcNtLazyWriterEvent, Executive, KernelMode, FALSE, &Period);

        Target = CcDirtyLazyTarget(&CcNtCache);
        if (Target != 0)
            CcRosFlushDirtyPages(Target, NULL, TRUE, TRUE);

        CcNtProcessDeferredWrites();
        KePulseEvent(&CcNtLazyPassEvent, IO_NO_INCREMENT, FALSE);
    }
}

NTSTATUS
NTAPI
CcWaitForCurrentLazyWriterActivity(VOID)
{
    CcNtKickLazyWriter();
    return KeWaitForSingleObject(&CcNtLazyPassEvent, Executive, KernelMode, FALSE, NULL);
}

BOOLEAN
CcInitializeCacheManager(VOID)
{
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG64 PhysicalBytes = (ULONG64)MmNumberOfPhysicalPages << PAGE_SHIFT;
    ULONG Views = (ULONG)((PhysicalBytes / 2) >> CC_VIEW_SHIFT);
    LONG64 DirtyThreshold = (LONG64)(MmNumberOfPhysicalPages / 8);
    HANDLE Handle;

    if (Views < CC_MIN_VIEWS)
        Views = CC_MIN_VIEWS;

    if (Views > CC_MAX_VIEWS)
        Views = CC_MAX_VIEWS;

    if (!NT_SUCCESS(CcCacheInitialize(&CcNtCache, Views, DirtyThreshold)))
        return FALSE;

    CcNtCache.BcbBytes = sizeof(CC_NT_BCB);
    CcNtCache.BcbCreated = CcNtBcbCreated;
    CcNtCache.BcbDeleting = CcNtBcbDeleting;

    KeInitializeSpinLock(&CcNtMapListLock);
    InitializeListHead(&CcNtMapList);
    InitializeListHead(&CcNtDeferredWrites);
    KeInitializeSpinLock(&CcNtDeferredLock);
    KeInitializeEvent(&CcNtLazyWriterEvent, SynchronizationEvent, FALSE);
    KeInitializeEvent(&CcNtLazyPassEvent, NotificationEvent, FALSE);

    InitializeObjectAttributes(&ObjectAttributes, NULL, OBJ_KERNEL_HANDLE, NULL, NULL);
    if (!NT_SUCCESS(PsCreateSystemThread(&Handle, THREAD_ALL_ACCESS, &ObjectAttributes, NULL, NULL, CcNtLazyWriter,
                                         NULL)))
    {
        return FALSE;
    }

    ZwClose(Handle);
    CcNtReady = TRUE;
    return TRUE;
}

VOID
NTAPI
CcShutdownSystem(VOID)
{
    ULONG Round;

    for (Round = 0; Round < 64; Round++)
    {
        ULONG Flushed = 0;

        CcRosFlushDirtyPages(~0u, &Flushed, TRUE, FALSE);
        if (Flushed == 0)
            break;
    }
}

VOID
NTAPI
CcPfInitializePrefetcher(VOID)
{
}

#ifdef KDBG
#include <kdbg/kdb.h>

BOOLEAN
ExpKdbgExtFileCache(
    ULONG Argc,
    PCHAR Argv[])
{
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    KdbpPrint("views %lu hits %I64d misses %I64d reclaims %I64d dirty %I64d\n", CcNtCache.ViewCount,
              CcNtCache.ViewHits, CcNtCache.ViewMisses, CcNtCache.ViewReclaims, CcNtCache.TotalDirtyPages);

    for (Entry = CcNtMapList.Flink; Entry != &CcNtMapList; Entry = Entry->Flink)
    {
        PCC_NT_MAP NtMap = CONTAINING_RECORD(Entry, CC_NT_MAP, Link);

        KdbpPrint("%p size %I64d views %lu dirty %I64d %wZ\n", NtMap, (LONGLONG)NtMap->Map.FileSize,
                  NtMap->Map.ViewsAttached, NtMap->Map.DirtyPages, &NtMap->FileObject->FileName);
    }

    return TRUE;
}

BOOLEAN
ExpKdbgExtDefWrites(
    ULONG Argc,
    PCHAR Argv[])
{
    UNREFERENCED_PARAMETER(Argc);
    UNREFERENCED_PARAMETER(Argv);

    KdbpPrint("dirty pages %I64d threshold %I64d deferred %s\n", CcNtCache.TotalDirtyPages,
              CcNtCache.DirtyPageThreshold, IsListEmpty(&CcNtDeferredWrites) ? "none" : "pending");
    return TRUE;
}
#endif
