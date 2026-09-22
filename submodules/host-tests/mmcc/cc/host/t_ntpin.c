/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/cc/host/t_ntpin.c
 * PURPOSE:     NT cached pinning host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccharness.h"
#include <cc/api/ccnt.h>

ULONG CcMapDataWait, CcMapDataNoWait, CcPinReadWait, CcPinReadNoWait, CcPinMappedDataCount;
ULONG CcFastReadNoWait, CcFastReadWait;
_Thread_local CC_HOST_EXCEPTION *CcHostException;
CC_CACHE CcNtCache;
static LONG NtBcbLocks;

BOOLEAN
ExAcquireResourceExclusiveLite(PERESOURCE Resource, BOOLEAN Wait)
{
    int Result = pthread_rwlock_trywrlock(Resource->Native);

    if (Result != 0 && Wait)
    {
        CC_ATOMIC_ADD32(&Resource->Waiters, 1);
        Result = pthread_rwlock_wrlock(Resource->Native);
        CC_ATOMIC_ADD32(&Resource->Waiters, -1);
    }
    return Result == 0;
}

BOOLEAN
ExAcquireResourceSharedLite(PERESOURCE Resource, BOOLEAN Wait)
{
    int Result = pthread_rwlock_tryrdlock(Resource->Native);

    if (Result != 0 && Wait)
    {
        CC_ATOMIC_ADD32(&Resource->Waiters, 1);
        Result = pthread_rwlock_rdlock(Resource->Native);
        CC_ATOMIC_ADD32(&Resource->Waiters, -1);
    }
    return Result == 0;
}

VOID
ExReleaseResourceLite(PERESOURCE Resource)
{
    CC_ASSERT(pthread_rwlock_unlock(Resource->Native) == 0);
}

VOID
ExReleaseResourceForThreadLite(PERESOURCE Resource, ERESOURCE_THREAD Thread)
{
    CC_ASSERT(Thread == 0);
    ExReleaseResourceLite(Resource);
}

VOID
ExSetResourceOwnerPointer(PERESOURCE Resource, PVOID Owner)
{
    CC_ASSERT(FALSE);
}

_Noreturn VOID
ExRaiseStatus(NTSTATUS Status)
{
    if (CcHostException != NULL)
    {
        CcHostException->Status = Status;
        longjmp(CcHostException->Jump, 1);
    }
    fprintf(stderr, "unexpected Cc status %x\n", (ULONG)Status);
    abort();
}

VOID
KeDelayExecutionThread(ULONG Mode, BOOLEAN Alertable, PLARGE_INTEGER Interval)
{
    CC_ASSERT(FALSE);
}

VOID
CcNtMapListBarrier(VOID)
{
}

NTSTATUS
MiPagingIo(PFILE_OBJECT File, ULONG64 Offset, ULONG Length, PVOID Buffer, BOOLEAN Write, PULONG Transferred)
{
    CC_ASSERT(FALSE);
    return STATUS_UNEXPECTED_IO_ERROR;
}

VOID
CcNtKickLazyWriter(VOID)
{
}

PCC_NT_MAP
CcNtReferenceMap(PSECTION_OBJECT_POINTERS Pointers)
{
    PCC_NT_MAP Map = Pointers != NULL ? Pointers->SharedCacheMap : NULL;

    if (Map != NULL)
        InterlockedIncrement(&Map->ReferenceCount);
    return Map;
}

VOID
CcNtDereferenceMap(PCC_NT_MAP Map)
{
    CHECK(CC_ATOMIC_ADD32(&Map->ReferenceCount, -1) > 1);
}

NTSTATUS
CcNtFlushMap(PCC_NT_MAP Map, ULONG64 Offset, ULONG64 Length, ULONG MaximumPages, PULONG PagesFlushed)
{
    CC_ASSERT(FALSE);
    return STATUS_UNEXPECTED_IO_ERROR;
}

NTSTATUS
ExInitializeResourceLite(PERESOURCE Resource)
{
    Resource->Native = malloc(sizeof(*Resource->Native));
    CC_ASSERT(Resource->Native != NULL);
    CC_ASSERT(pthread_rwlock_init(Resource->Native, NULL) == 0);
    CC_ATOMIC_ADD32(&NtBcbLocks, 1);
    return STATUS_SUCCESS;
}

NTSTATUS
ExDeleteResourceLite(PERESOURCE Resource)
{
    CC_ASSERT(pthread_rwlock_destroy(Resource->Native) == 0);
    free(Resource->Native);
    Resource->Native = NULL;
    CHECK(CC_ATOMIC_ADD32(&NtBcbLocks, -1) > 0);
    return STATUS_SUCCESS;
}

typedef struct _NT_PIN_READER
{
    PFILE_OBJECT File;
    PVOID ExpectedBcb;
    PVOID ExpectedBuffer;
    ULONG Flags;
    BOOLEAN ExpectedResult;
    BOOLEAN MapOnly;
    volatile LONG Done;
} NT_PIN_READER;

static void *
NtPinReader(void *Argument)
{
    NT_PIN_READER *Reader = Argument;
    LARGE_INTEGER Offset = { .QuadPart = PAGE_SIZE };
    PVOID Bcb = NULL, Buffer = NULL;
    BOOLEAN Result;

    if (Reader->MapOnly)
        Result = CcMapData(Reader->File, &Offset, PAGE_SIZE, Reader->Flags, &Bcb, &Buffer);
    else
        Result = CcPinRead(Reader->File, &Offset, PAGE_SIZE, Reader->Flags, &Bcb, &Buffer);
    CHECK(Result == Reader->ExpectedResult);
    if (Result)
    {
        CHECK(Reader->MapOnly ? Bcb != Reader->ExpectedBcb : Bcb == Reader->ExpectedBcb);
        CHECK(Buffer == Reader->ExpectedBuffer);
        CcUnpinData(Bcb);
    }
    CC_ATOMIC_ADD32(&Reader->Done, 1);
    return NULL;
}

static void
NtPinContention(PCC_NT_MAP Map, PFILE_OBJECT File, ULONG HolderFlags, ULONG ReaderFlags,
                BOOLEAN ExpectedResult, BOOLEAN MapOnly)
{
    LARGE_INTEGER Offset = { .QuadPart = PAGE_SIZE };
    PVOID Bcb, Buffer;
    NT_PIN_READER Reader;
    pthread_t Thread;
    double Deadline;
    LONG Done;

    CHECK(CcPinRead(File, &Offset, PAGE_SIZE, HolderFlags, &Bcb, &Buffer));
    CHECK(((PPUBLIC_BCB)Bcb)->NodeTypeCode == 0x2FD);
    CHECK(((PPUBLIC_BCB)Bcb)->NodeByteSize == 0);
    CHECK(((PPUBLIC_BCB)Bcb)->MappedLength == PAGE_SIZE);
    CHECK(((PPUBLIC_BCB)Bcb)->MappedFileOffset.QuadPart == PAGE_SIZE);
    Reader = (NT_PIN_READER){ File, Bcb, Buffer, ReaderFlags, ExpectedResult, MapOnly, 0 };
    CC_ASSERT(pthread_create(&Thread, NULL, NtPinReader, &Reader) == 0);
    Deadline = NowSeconds() + 2.0;
    while (!CC_ATOMIC_READ32(&Reader.Done) && NowSeconds() < Deadline)
        sched_yield();
    Done = CC_ATOMIC_READ32(&Reader.Done);
    if (!Done)
        fprintf(stderr, "pin timeout: holder=%u reader=%u map=%u\n", HolderFlags, ReaderFlags, MapOnly);
    CHECK(Done == 1);
    if (Done)
    {
        CHECK(CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb)->Engine.ReferenceCount == 1);
        CHECK(CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb)->Engine.PinCount == 1);
        CHECK(Map->ReferenceCount == 2);
    }
    CcUnpinData(Bcb);
    CC_ASSERT(pthread_join(Thread, NULL) == 0);
    CHECK(Map->ReferenceCount == 1);
    CHECK(IsListEmpty(&Map->Map.BcbList));
}

static void
NtPinWaiter(PCC_NT_MAP Map, PFILE_OBJECT File, ULONG Flags)
{
    LARGE_INTEGER Offset = { .QuadPart = PAGE_SIZE };
    PVOID Bcb, Buffer;
    PCC_NT_BCB NtBcb;
    NT_PIN_READER Reader;
    pthread_t Thread;
    double Deadline;

    CHECK(CcPinRead(File, &Offset, PAGE_SIZE, PIN_WAIT | PIN_EXCLUSIVE, &Bcb, &Buffer));
    NtBcb = CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb);
    Reader = (NT_PIN_READER){ File, Bcb, Buffer, Flags, TRUE, FALSE, 0 };
    CC_ASSERT(pthread_create(&Thread, NULL, NtPinReader, &Reader) == 0);
    Deadline = NowSeconds() + 2.0;
    while (!CC_ATOMIC_READ32(&NtBcb->Engine.Resource.Waiters) && !CC_ATOMIC_READ32(&Reader.Done) &&
           NowSeconds() < Deadline)
    {
        sched_yield();
    }
    CHECK(CC_ATOMIC_READ32(&NtBcb->Engine.Resource.Waiters) == 1);
    CHECK(CC_ATOMIC_READ32(&Reader.Done) == 0);
    CcUnpinData(Bcb);
    CC_ASSERT(pthread_join(Thread, NULL) == 0);
    CHECK(Reader.Done == 1 && Map->ReferenceCount == 1 && IsListEmpty(&Map->Map.BcbList));
}

void
TestNtPin(void)
{
    static const ULONG SharedFlags[] = { 0, PIN_WAIT, PIN_IF_BCB, PIN_EXCLUSIVE, PIN_WAIT | PIN_IF_BCB };
    static const ULONG ExclusiveFlags[] = { 0, PIN_IF_BCB, PIN_EXCLUSIVE };
    CC_NT_MAP Map = { 0 };
    TEST_FILE Backing;
    CC_CACHE Cache;
    SECTION_OBJECT_POINTERS Pointers = { &Map };
    FILE_OBJECT File = { .SectionObjectPointer = &Pointers };
    LARGE_INTEGER Offset = { .QuadPart = PAGE_SIZE };
    PVOID Bcb = (PVOID)(ULONG_PTR)1, Buffer = (PVOID)(ULONG_PTR)1;
    ULONG i;

    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    Cache.BcbBytes = sizeof(CC_NT_BCB);
    Cache.BcbCreated = CcNtBcbCreated;
    Cache.BcbDeleting = CcNtBcbDeleting;
    FileCreate(&Backing, CC_VIEW_SIZE);
    CcMapInitialize(&Map.Map, &Cache, &Backing.Ops, &Backing, CC_VIEW_SIZE, CC_VIEW_SIZE, CC_VIEW_SIZE);
    Map.ReferenceCount = 1;
    Map.PinAccess = TRUE;

    CHECK(!CcPinRead(&File, &Offset, PAGE_SIZE, 0, &Bcb, &Buffer));
    CHECK(Backing.DiskReads == 0 && Map.ReferenceCount == 1);
    CHECK(IsListEmpty(&Map.Map.BcbList));
    CHECK(!CcPinRead(&File, &Offset, PAGE_SIZE, PIN_WAIT | PIN_IF_BCB, &Bcb, &Buffer));
    CHECK(Bcb == NULL && Backing.DiskReads == 0 && Map.ReferenceCount == 1);

    for (i = 0; i < RTL_NUMBER_OF(SharedFlags); i++)
        NtPinContention(&Map, &File, PIN_WAIT, SharedFlags[i], TRUE, FALSE);
    for (i = 0; i < RTL_NUMBER_OF(ExclusiveFlags); i++)
        NtPinContention(&Map, &File, PIN_WAIT | PIN_EXCLUSIVE, ExclusiveFlags[i], FALSE, FALSE);
    NtPinContention(&Map, &File, PIN_WAIT | PIN_EXCLUSIVE, 0, TRUE, TRUE);
    NtPinWaiter(&Map, &File, PIN_WAIT);
    NtPinWaiter(&Map, &File, PIN_WAIT | PIN_EXCLUSIVE);

    {
        PVOID SharedBcb, SharedBuffer;
        LONG64 Reads;

        CHECK(CcPinRead(&File, &Offset, 2 * PAGE_SIZE, PIN_WAIT, &Bcb, &Buffer));
        Reads = Backing.DiskReads;
        __atomic_store_n(&Backing.Resident[2], 0, __ATOMIC_SEQ_CST);
        CHECK(CcPinRead(&File, &Offset, PAGE_SIZE, 0, &SharedBcb, &SharedBuffer));
        CHECK(SharedBcb == Bcb && SharedBuffer == Buffer && Backing.DiskReads == Reads);
        CcUnpinData(SharedBcb);
        CcUnpinData(Bcb);
    }

    CHECK(CcMapData(&File, &Offset, PAGE_SIZE, 0, &Bcb, &Buffer));
    CHECK(CcPinMappedData(&File, &Offset, PAGE_SIZE, 0, &Bcb));
    CHECK(CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb)->Engine.Pinned);
    CHECK(Map.ReferenceCount == 2);
    CcUnpinData(Bcb);
    CHECK(Map.ReferenceCount == 1 && IsListEmpty(&Map.Map.BcbList));
    CHECK(NtBcbLocks == 0 && Backing.Errors == 0 && Backing.DiskReads == 2);
    Map.PinAccess = FALSE;
    Offset.QuadPart = 0x1000;
    CHECK(CcMapData(&File, &Offset, 0x3000, MAP_WAIT, &Bcb, &Buffer));
    CHECK(Buffer != NULL && Bcb != NULL);
    CHECK(memcmp(Buffer, Backing.Disk + Offset.QuadPart, 0x3000) == 0);
    CHECK(!CC_NT_BCB_FROM_PUBLIC((PPUBLIC_BCB)Bcb)->Engine.Pinned);
    CcUnpinData(Bcb);
    CHECK(Map.ReferenceCount == 1 && IsListEmpty(&Map.Map.BcbList));
    CHECK(NtBcbLocks == 0 && Backing.Errors == 0);
    CHECK(CcMapUninitialize(&Map.Map));
    CcCacheUninitialize(&Cache);
    CHECK(Backing.MappedViews == 0);
    FileDestroy(&Backing);
}
