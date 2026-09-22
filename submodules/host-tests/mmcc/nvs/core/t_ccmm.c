/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_ccmm.c
 * PURPOSE:     Cache and memory manager host-native integration tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include <cc/include/ccengine.h>
#include <cc/api/ccnt.h>

_Thread_local ULONG CcHostProcessor;
_Thread_local CC_HOST_EXCEPTION *CcHostException;

typedef struct _CCMM_FILE
{
    TEST_WORLD *World;
    TEST_FILE File;
    PMI_SEGMENT Segment;
    CC_MAP Map;
    volatile LONG64 Views;
    volatile LONG Released;
    volatile LONG BlockWrites;
    volatile LONG WriteEntered;
    volatile LONG ReleaseWriter;
    volatile LONG CloseStarted;
    MI_READ_COMPLETION ReadCompletion[2];
    PVOID ReadContext[2];
    ULONG ReadCount;
} CCMM_FILE;

typedef struct _CCMM_MOVE
{
    TEST_WORLD *World;
    ULONG Cpu;
    PUCHAR Buffer;
    ULONG64 BufferOffset;
    BOOLEAN ToCache;
} CCMM_MOVE;

static
NTSTATUS
CcmmMapView(PVOID Context, ULONG64 Offset, SIZE_T Length, PVOID *Base)
{
    CCMM_FILE *File = Context;
    ULONG64 Size = Length;
    ULONG64 Address = 0;
    NTSTATUS Status;

    Status = MiMapCacheView(File->Segment, &Address, Offset, &Size);
    if (NT_SUCCESS(Status))
    {
        *Base = (PVOID)(ULONG_PTR)Address;
        __sync_fetch_and_add(&File->Views, 1);
    }

    return Status;
}

static
VOID
CcmmUnmapView(PVOID Context, PVOID Base)
{
    CCMM_FILE *File = Context;

    CHECK(NT_SUCCESS(MiUnmapView(&File->World->System.SystemSpace, (ULONG64)(ULONG_PTR)Base)));
    __sync_fetch_and_add(&File->Views, -1);
}

static
BOOLEAN
CcmmIsResident(PVOID Context, ULONG64 Offset, ULONG Length)
{
    return MiSegmentIsResident(((CCMM_FILE *)Context)->Segment, Offset, Length);
}

static
NTSTATUS
CcmmMakeResident(PVOID Context, ULONG64 Offset, ULONG Length, ULONG64 ValidDataLength)
{
    return MiSegmentMakeResidentBeyond(((CCMM_FILE *)Context)->Segment, Offset, Length, ValidDataLength);
}

static
NTSTATUS
CcmmMarkDirty(PVOID Context, ULONG64 Offset, ULONG Length)
{
    return MiSegmentMarkDirty(((CCMM_FILE *)Context)->Segment, Offset, Length);
}

static
NTSTATUS
CcmmFlush(PVOID Context, ULONG64 Offset, ULONG Length)
{
    return MiSegmentFlush(((CCMM_FILE *)Context)->Segment, Offset, Length);
}

static
BOOLEAN
CcmmPurge(PVOID Context, ULONG64 Offset, ULONG64 Length)
{
    return MiSegmentPurge(((CCMM_FILE *)Context)->Segment, Offset, Length);
}

static
NTSTATUS
CcmmMakeViewResident(PVOID Context, PVOID Base, ULONG Length)
{
    CCMM_FILE *File = Context;
    ULONG64 Address = (ULONG64)(ULONG_PTR)Base;
    ULONG64 End = Address + Length;

    while (Address < End)
    {
        UCHAR Byte;
        NTSTATUS Status = MachineAccessMemory(&File->World->Machine, MiHostCpu, Address, &Byte, 1,
                                               MachineRead, FALSE);

        if (!NT_SUCCESS(Status))
            return Status;
        Address = (Address | (PAGE_SIZE - 1)) + 1;
    }
    return STATUS_SUCCESS;
}

static CC_BACKING_OPS CcmmOps =
{
    CcmmMapView, CcmmUnmapView, CcmmIsResident, CcmmMakeResident, CcmmMarkDirty, CcmmFlush, CcmmPurge, NULL, NULL,
    CcmmMakeViewResident
};

static
NTSTATUS
CcmmMove(PVOID Context, PVOID CacheAddress, ULONG64 FileOffset, ULONG Length)
{
    CCMM_MOVE *Move = Context;
    ULONG Attempt;
    NTSTATUS Status = STATUS_SUCCESS;

    for (Attempt = 0; Attempt < 100000; Attempt++)
    {
        Status = MachineAccessMemory(&Move->World->Machine, Move->Cpu, (ULONG64)(ULONG_PTR)CacheAddress,
                                     Move->Buffer + (FileOffset - Move->BufferOffset), Length,
                                     Move->ToCache ? MachineWrite : MachineRead, FALSE);
        if (Status != STATUS_NO_MEMORY)
            break;

        sched_yield();
    }

    return Status;
}

static volatile LONG64 CcmmMemoryWaits;

static
NTSTATUS
CcmmCopy(CCMM_FILE *File, ULONG64 Offset, ULONG Length, BOOLEAN ForWrite, CCMM_MOVE *Move)
{
    NTSTATUS Status = STATUS_SUCCESS;
    ULONG Attempt;

    for (Attempt = 0; Attempt < 100000; Attempt++)
    {
        Status = CcCopyRange(&File->Map, Offset, Length, ForWrite, CcmmMove, Move, NULL);
        if (Status != STATUS_NO_MEMORY)
            break;

        __sync_fetch_and_add(&CcmmMemoryWaits, 1);
        sched_yield();
    }

    if (!NT_SUCCESS(Status) && getenv("MM_DEBUG"))
        fprintf(stderr, "copy status %x offset %llx length %x write %u\n", Status, Offset, Length, ForWrite);

    return Status;
}

static
NTSTATUS
CachedRead(CCMM_FILE *File, ULONG Cpu, ULONG64 Offset, PVOID Buffer, ULONG Length)
{
    CCMM_MOVE Move = { File->World, Cpu, Buffer, Offset, FALSE };

    return CcmmCopy(File, Offset, Length, FALSE, &Move);
}

static
NTSTATUS
CachedWrite(CCMM_FILE *File, ULONG Cpu, ULONG64 Offset, PVOID Buffer, ULONG Length)
{
    CCMM_MOVE Move = { File->World, Cpu, Buffer, Offset, TRUE };

    return CcmmCopy(File, Offset, Length, TRUE, &Move);
}

static
void
CcmmFileRelease(PVOID Context)
{
    CCMM_FILE *File = CONTAINING_RECORD(Context, CCMM_FILE, File);

    MI_ATOMIC_ADD32(&File->Released, 1);
}

static NTSTATUS
CcmmFileWrite(PVOID Context, ULONG64 Offset, ULONG Length, PVOID Buffer)
{
    CCMM_FILE *File = CONTAINING_RECORD(Context, CCMM_FILE, File);

    CHECK(MI_ATOMIC_READ32(&File->Released) == 0);
    if (MI_ATOMIC_READ32(&File->BlockWrites))
    {
        __atomic_store_n(&File->WriteEntered, 1, __ATOMIC_SEQ_CST);
        while (!MI_ATOMIC_READ32(&File->ReleaseWriter))
            sched_yield();
    }
    return TestFileOps.Write(Context, Offset, Length, Buffer);
}

static
void
CcmmFileCreate(CCMM_FILE *File, TEST_WORLD *World, PCC_CACHE Cache, ULONG64 Size)
{
    MI_FILE_OPS Ops = TestFileOps;

    memset(File, 0, sizeof(*File));
    File->World = World;
    FileCreate(&File->File, Size);
    Ops.Release = CcmmFileRelease;
    Ops.Write = CcmmFileWrite;
    CHECK(NT_SUCCESS(MiSegmentCreate(&World->System, MiSegmentDataFile, Size, MI_PROT_READWRITE, &Ops,
                                     &File->File, NULL, 0, &File->Segment)));
    CcMapInitialize(&File->Map, Cache, &CcmmOps, File, Size, Size, Size);
}

static
void
CcmmFileDestroy(CCMM_FILE *File)
{
    ULONG Flushed;

    CHECK(NT_SUCCESS(CcDirtyFlush(&File->Map, 0, File->File.Size, ~0u, &Flushed)));
    CHECK(CcMapUninitialize(&File->Map));
    CHECK(File->Views == 0);
    MiSegmentDereference(File->Segment);
    FileDestroy(&File->File);
}

static
NTSTATUS
CcmmRejectMove(PVOID Context, PVOID CacheAddress, ULONG64 FileOffset, ULONG Length)
{
    CCMM_FILE *File = Context;

    CHECK(MiSegmentIsResident(File->Segment, FileOffset, Length));
    return STATUS_UNEXPECTED_IO_ERROR;
}

static
void
CcmmExtendingCopy(void)
{
    TEST_WORLD World;
    CCMM_FILE File;
    CC_CACHE Cache;
    PUCHAR Data = malloc(PAGE_SIZE);
    PUCHAR Buffer = malloc(PAGE_SIZE);
    ULONG i;

    WorldCreate(&World, 256, 1, 100000);
    WorldAttach(&World, 0, NULL);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    CcmmFileCreate(&File, &World, &Cache, 4 * PAGE_SIZE);
    File.Map.ValidDataLength = PAGE_SIZE + 17;
    memset(Data, 0x5A, PAGE_SIZE);
    CHECK(NT_SUCCESS(CachedWrite(&File, 0, 2 * PAGE_SIZE + 7, Data, PAGE_SIZE - 14)));
    CHECK(File.File.Reads == 0);
    CHECK(NT_SUCCESS(CachedRead(&File, 0, 2 * PAGE_SIZE, Buffer, PAGE_SIZE)));
    for (i = 0; i < PAGE_SIZE; i++)
        CHECK(Buffer[i] == (i < 7 || i >= PAGE_SIZE - 7 ? 0 : 0x5A));
    CHECK(NT_SUCCESS(CachedWrite(&File, 0, PAGE_SIZE + 31, Data, 1)));
    CHECK(File.File.Reads == 1);
    CHECK(NT_SUCCESS(CachedRead(&File, 0, PAGE_SIZE, Buffer, PAGE_SIZE)));
    CHECK(memcmp(Buffer, File.File.Data + PAGE_SIZE, 17) == 0);
    CHECK(Buffer[31] == 0x5A);
    CHECK(NT_SUCCESS(CcPrefetchRange(&File.Map, 3 * PAGE_SIZE, PAGE_SIZE)));
    CHECK(File.File.Reads == 2);
    CHECK(NT_SUCCESS(CachedRead(&File, 0, 3 * PAGE_SIZE, Buffer, PAGE_SIZE)));
    CHECK(memcmp(Buffer, File.File.Data + 3 * PAGE_SIZE, PAGE_SIZE) == 0);
    CHECK(NT_SUCCESS(CcDirtyFlush(&File.Map, 0, 4 * PAGE_SIZE, ~0u, NULL)));
    CcmmFileDestroy(&File);
    CcCacheUninitialize(&Cache);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
    free(Buffer);
    free(Data);
}

static
void
CcmmFaultableCopy(void)
{
    TEST_WORLD World;
    CCMM_FILE File;
    CC_CACHE Cache;
    UCHAR Data[PAGE_SIZE + 19];
    PUCHAR Buffer = malloc(4 * PAGE_SIZE);
    PUCHAR Expected = malloc(4 * PAGE_SIZE);
    ULONG64 Offset = PAGE_SIZE - 7;
    BOOLEAN (*Resident)(PVOID, ULONG64, ULONG);
    NTSTATUS (*MakeResident)(PVOID, ULONG64, ULONG, ULONG64);

    WorldCreate(&World, 256, 1, 100000);
    World.Machine.StrictTlb = TRUE;
    WorldAttach(&World, 0, NULL);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    CcmmFileCreate(&File, &World, &Cache, 4 * PAGE_SIZE);
    memcpy(Expected, File.File.Data, 4 * PAGE_SIZE);
    memset(Data, 0x5A, sizeof(Data));
    memcpy(Expected + Offset, Data, sizeof(Data));
    Resident = File.Map.Ops.IsResident;
    MakeResident = File.Map.Ops.MakeResident;
    File.Map.Ops.IsResident = NULL;
    File.Map.Ops.MakeResident = NULL;
    CHECK(CcCopyRange(&File.Map, Offset, sizeof(Data), TRUE, CcmmRejectMove, &File, NULL) ==
          STATUS_UNEXPECTED_IO_ERROR);
    CHECK(File.File.Reads == 3 && File.Map.DirtyPages == 0);
    CHECK(NT_SUCCESS(CachedWrite(&File, 0, Offset, Data, sizeof(Data))));
    CHECK(File.File.Reads == 3 && File.Map.DirtyPages == 3);
    CHECK(NT_SUCCESS(CcDirtyFlush(&File.Map, 0, 4 * PAGE_SIZE, ~0u, NULL)));
    CHECK(memcmp(File.File.Data, Expected, 4 * PAGE_SIZE) == 0);
    CHECK(NT_SUCCESS(CachedRead(&File, 0, 0, Buffer, 4 * PAGE_SIZE)));
    CHECK(memcmp(Buffer, Expected, 4 * PAGE_SIZE) == 0);
    CHECK(MiTrimAddressSpace(&World.System.SystemSpace, 64, TRUE) == 4);
    CHECK(NT_SUCCESS(CachedRead(&File, 0, 0, Buffer, 4 * PAGE_SIZE)));
    CHECK(memcmp(Buffer, Expected, 4 * PAGE_SIZE) == 0);
    File.Map.Ops.IsResident = Resident;
    File.Map.Ops.MakeResident = MakeResident;
    CHECK(CcMapUninitialize(&File.Map));
    CHECK(MiSegmentDereferenceAndClose(File.Segment));
    CHECK(File.Views == 0 && File.Released == 1);
    CcCacheUninitialize(&Cache);
    FileDestroy(&File.File);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
    free(Buffer);
    free(Expected);
}

static
void
CcmmBasic(void)
{
    static TEST_WORLD World;
    static CCMM_FILE File;
    CC_CACHE Cache;
    MI_ADDRESS_SPACE Process;
    const ULONG64 Size = 4 << 20;
    PUCHAR Buffer = malloc(Size);
    PUCHAR Shadow = malloc(Size);
    ULONG64 UserBase = 0;
    ULONG64 ViewSize = 0;
    ULONG64 Seed = 42;
    ULONG Flushed;
    ULONG i;

    WorldCreate(&World, 4096, 2, 1000000);
    World.Machine.StrictTlb = TRUE;
    WorldAttach(&World, 0, NULL);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 100000)));
    CcmmFileCreate(&File, &World, &Cache, Size);
    memcpy(Shadow, File.File.Data, Size);

    CHECK(NT_SUCCESS(CachedRead(&File, 0, 0, Buffer, (ULONG)Size)));
    CHECK(memcmp(Buffer, Shadow, Size) == 0);
    CHECK(File.File.Reads == 1024);
    CHECK(NT_SUCCESS(CachedRead(&File, 0, 0x1234, Buffer, 0x80000)));
    CHECK(memcmp(Buffer, Shadow + 0x1234, 0x80000) == 0);
    CHECK(File.File.Reads == 1024);

    for (i = 0; i < 200; i++)
    {
        ULONG64 Offset = Rng(&Seed) % (Size - 0x5000);
        ULONG Length = (ULONG)(Rng(&Seed) % 0x5000) + 1;
        ULONG j;

        for (j = 0; j < Length; j++)
            Shadow[Offset + j] = (UCHAR)(Rng(&Seed) >> 11);

        CHECK(NT_SUCCESS(CachedWrite(&File, 0, Offset, Shadow + Offset, Length)));
    }

    CHECK(NT_SUCCESS(CachedRead(&File, 0, 0, Buffer, (ULONG)Size)));
    CHECK(memcmp(Buffer, Shadow, Size) == 0);
    CHECK(memcmp(File.File.Data, Shadow, Size) != 0);
    CHECK(CcDirtyQuery(&File.Map, 0, Size));

    CHECK(NT_SUCCESS(MiAddressSpaceCreate(&World.System, &Process)));
    WorldAttach(&World, 1, &Process);
    CHECK(NT_SUCCESS(MiMapView(&Process, File.Segment, &UserBase, 0, &ViewSize, MI_PROT_READWRITE, 0)));
    CHECK(NT_SUCCESS(UserRead(&World, 1, UserBase + 0x100000, Buffer, 0x20000)));
    CHECK(memcmp(Buffer, Shadow + 0x100000, 0x20000) == 0);
    Shadow[0x180000] ^= 0xFF;
    CHECK(NT_SUCCESS(UserWrite(&World, 1, UserBase + 0x180000, Shadow + 0x180000, 1)));
    CHECK(NT_SUCCESS(CachedRead(&File, 0, 0x180000, Buffer, 16)));
    CHECK(Buffer[0] == Shadow[0x180000]);
    CHECK(NT_SUCCESS(MiUnmapView(&Process, UserBase)));
    WorldAttach(&World, 1, NULL);
    MiAddressSpaceDestroy(&Process);

    CHECK(NT_SUCCESS(CcDirtyFlush(&File.Map, 0, Size, ~0u, &Flushed)));
    CHECK(Flushed != 0);
    CHECK(!CcDirtyQuery(&File.Map, 0, Size));
    while (MiWriteModifiedPages(&World.System, 4096) != 0)
        ;
    CHECK(NT_SUCCESS(MiSegmentFlush(File.Segment, 0, Size)));
    CHECK(memcmp(File.File.Data, Shadow, Size) == 0);

    File.File.Data[0x200000] ^= 0x55;
    Shadow[0x200000] ^= 0x55;
    CHECK(CcMapDetachViews(&File.Map, 0, (ULONG64)-1));
    CHECK(MiSegmentPurge(File.Segment, 0, 0));
    CHECK(NT_SUCCESS(CachedRead(&File, 0, 0x200000, Buffer, 16)));
    CHECK(Buffer[0] == Shadow[0x200000]);

    CHECK(CcCacheCheck(&Cache) == 0);
    CcmmFileDestroy(&File);
    CcCacheUninitialize(&Cache);
    CHECK(MI_ATOMIC_READ64(&World.System.SystemSpace.PageTablePages) == 0);
    MiPfnDrainCaches(&World.System.Pfn);
    CHECK(WorldCheck(&World) == 0);
    CHECK(MiPfnAvailablePages(&World.System.Pfn) == 4096 - 2);
    free(Buffer);
    free(Shadow);
    WorldDestroy(&World);
}

static
void
CcmmGrow(void)
{
    static TEST_WORLD World;
    static CCMM_FILE File;
    CC_CACHE Cache;
    const ULONG64 Backing = 1 << 20;
    const ULONG64 Grown = 0x31200;
    PUCHAR Buffer = malloc(Backing);
    ULONG Flushed;

    WorldCreate(&World, 1024, 1, 1000000);
    World.Machine.StrictTlb = TRUE;
    WorldAttach(&World, 0, NULL);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 100000)));

    memset(&File, 0, sizeof(File));
    File.World = &World;
    FileCreate(&File.File, Backing);
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, 0x5A, MI_PROT_READWRITE, &TestFileOps,
                                     &File.File, NULL, 0, &File.Segment)));
    CcMapInitialize(&File.Map, &Cache, &CcmmOps, &File, 0x5A, 0x5A, 0x5A);

    CHECK(NT_SUCCESS(CachedRead(&File, 0, 0, Buffer, 0x5A)));
    CHECK(memcmp(Buffer, File.File.Data, 0x5A) == 0);
    CHECK(File.Views == 1);

    CHECK(NT_SUCCESS(MiSegmentExtend(File.Segment, Grown)));
    CcMapSetSizes(&File.Map, Grown, Grown, Grown);

    CHECK(NT_SUCCESS(CachedRead(&File, 0, 0x4000, Buffer, (ULONG)(Grown - 0x4000))));
    CHECK(memcmp(Buffer, File.File.Data + 0x4000, Grown - 0x4000) == 0);
    CHECK(File.Views == 1);

    Buffer[0] = 0x77;
    CHECK(NT_SUCCESS(CachedWrite(&File, 0, 0x31000, Buffer, 1)));
    CHECK(NT_SUCCESS(CachedRead(&File, 0, 0x31000, Buffer + 1, 1)));
    CHECK(Buffer[1] == 0x77);

    CHECK(NT_SUCCESS(CcDirtyFlush(&File.Map, 0, Grown, ~0u, &Flushed)));
    CHECK(NT_SUCCESS(MiSegmentFlush(File.Segment, 0, Grown)));
    CHECK(File.File.Data[0x31000] == 0x77);

    CHECK(CcCacheCheck(&Cache) == 0);
    CcmmFileDestroy(&File);
    CcCacheUninitialize(&Cache);
    MiPfnDrainCaches(&World.System.Pfn);
    CHECK(WorldCheck(&World) == 0);
    free(Buffer);
    WorldDestroy(&World);
}

#if defined(TEST_LIGHT)
#define CCMM_THREADS     3
#define CCMM_ITERATIONS  300
#else
#define CCMM_THREADS     6
#define CCMM_ITERATIONS  4000
#endif
#define CCMM_FILES       3
#define CCMM_FILE_SIZE   (3 << 20)

typedef struct _CCMM_SHARED
{
    TEST_WORLD *World;
    CC_CACHE *Cache;
    CCMM_FILE Files[CCMM_FILES];
    PUCHAR Shadow[CCMM_FILES];
    volatile LONG Stop;
} CCMM_SHARED;

typedef struct _CCMM_THREAD
{
    CCMM_SHARED *Shared;
    ULONG Index;
} CCMM_THREAD;

static
void *
CcmmWorker(void *Argument)
{
    CCMM_THREAD *Thread = Argument;
    CCMM_SHARED *Shared = Thread->Shared;
    ULONG64 Seed = 0xD1B54A32D192ED03ULL * (Thread->Index + 1);
    ULONG64 Stripe = CCMM_FILE_SIZE / CCMM_THREADS;
    PUCHAR Buffer = malloc(0x10000);
    ULONG i;

    MiHostCpu = Thread->Index;
    CcHostProcessor = Thread->Index;

    for (i = 0; i < CCMM_ITERATIONS; i++)
    {
        ULONG FileIndex = (ULONG)(Rng(&Seed) % CCMM_FILES);
        CCMM_FILE *File = &Shared->Files[FileIndex];
        PUCHAR Shadow = Shared->Shadow[FileIndex];
        ULONG Length = (ULONG)(Rng(&Seed) % 0x6000) + 1;
        ULONG64 Offset = Stripe * Thread->Index + Rng(&Seed) % (Stripe - Length);
        ULONG j;

        if (Rng(&Seed) & 1)
        {
            for (j = 0; j < Length; j++)
                Shadow[Offset + j] = (UCHAR)(Rng(&Seed) >> 13);

            CHECK(NT_SUCCESS(CachedWrite(File, Thread->Index, Offset, Shadow + Offset, Length)));
        }
        else
        {
            CHECK(NT_SUCCESS(CachedRead(File, Thread->Index, Offset, Buffer, Length)));
            CHECK(memcmp(Buffer, Shadow + Offset, Length) == 0);
        }
    }

    free(Buffer);
    return NULL;
}

static
void *
CcmmReclaimer(void *Argument)
{
    CCMM_SHARED *Shared = Argument;
    ULONG Round = 0;

    MiHostCpu = MACHINE_MAX_CPUS - 1;
    CcHostProcessor = MACHINE_MAX_CPUS - 1;

    while (!__atomic_load_n(&Shared->Stop, __ATOMIC_SEQ_CST))
    {
        PMI_SYSTEM System = &Shared->World->System;
        ULONG Flushed;

        if (MiPfnAvailablePages(&System->Pfn) < 256)
            MiTrimAddressSpace(&System->SystemSpace, 128, (BOOLEAN)(Round & 1));

        MiWriteModifiedPages(System, 128);

        if ((Round & 15) == 0)
            CcDirtyFlush(&Shared->Files[(Round >> 4) % CCMM_FILES].Map, 0, CCMM_FILE_SIZE, 64, &Flushed);

        if ((Round & 31) == 0)
            CcCacheTrim(Shared->Cache, 4);

        Round++;
        sched_yield();
    }

    return NULL;
}

static
void
CcmmSmp(void)
{
    static TEST_WORLD World;
    static CCMM_SHARED Shared;
    static CC_CACHE Cache;
    CCMM_THREAD Threads[CCMM_THREADS];
    pthread_t Workers[CCMM_THREADS];
    pthread_t Reclaimer;
    ULONG i;

    memset(&Shared, 0, sizeof(Shared));
    WorldCreate(&World, 1536, MACHINE_MAX_CPUS, 1000000);
    World.Machine.StrictTlb = FALSE;
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 100000)));
    Shared.World = &World;
    Shared.Cache = &Cache;

    for (i = 0; i < CCMM_FILES; i++)
    {
        CcmmFileCreate(&Shared.Files[i], &World, &Cache, CCMM_FILE_SIZE);
        Shared.Shadow[i] = malloc(CCMM_FILE_SIZE);
        memcpy(Shared.Shadow[i], Shared.Files[i].File.Data, CCMM_FILE_SIZE);
    }

    pthread_create(&Reclaimer, NULL, CcmmReclaimer, &Shared);
    for (i = 0; i < CCMM_THREADS; i++)
    {
        Threads[i].Shared = &Shared;
        Threads[i].Index = i;
        pthread_create(&Workers[i], NULL, CcmmWorker, &Threads[i]);
    }

    for (i = 0; i < CCMM_THREADS; i++)
        pthread_join(Workers[i], NULL);

    __atomic_store_n(&Shared.Stop, 1, __ATOMIC_SEQ_CST);
    pthread_join(Reclaimer, NULL);

    printf("  ccmm: faults %lld repurposed %lld file reads %lld writes %lld memory waits %lld\n",
           (LONG64)World.Machine.Faults, (LONG64)World.System.Pfn.Repurposed, (LONG64)Shared.Files[0].File.Reads,
           (LONG64)Shared.Files[0].File.Writes, (LONG64)CcmmMemoryWaits);

    MiHostCpu = 0;
    CHECK(CcCacheCheck(&Cache) == 0);

    for (i = 0; i < CCMM_FILES; i++)
    {
        PUCHAR Data;

        CHECK(NT_SUCCESS(CcDirtyFlush(&Shared.Files[i].Map, 0, CCMM_FILE_SIZE, ~0u, NULL)));
        CHECK(CcMapUninitialize(&Shared.Files[i].Map));
        MiSegmentDereference(Shared.Files[i].Segment);
        Data = Shared.Files[i].File.Data;
        CHECK(memcmp(Data, Shared.Shadow[i], CCMM_FILE_SIZE) == 0);
        FileDestroy(&Shared.Files[i].File);
        free(Shared.Shadow[i]);
    }

    CcCacheUninitialize(&Cache);
    CHECK(IsListEmpty(&World.System.SegmentList));
    CHECK(MI_ATOMIC_READ64(&World.System.SystemSpace.PageTablePages) == 0);
    MiPfnDrainCaches(&World.System.Pfn);
    CHECK(WorldCheck(&World) == 0);
    CHECK(MiPfnListCount(&World.System.Pfn, MiPageModified) == 0);
    CHECK(MiPfnAvailablePages(&World.System.Pfn) == 1536 - 2);
    WorldDestroy(&World);
}

static void *
CcmmCloseWorker(void *Context)
{
    PCC_NT_MAP NtMap = Context;
    CCMM_FILE *File = NtMap->FileObject->FsContext;

    WorldAttach(File->World, 1, NULL);
    __atomic_store_n(&File->CloseStarted, 1, __ATOMIC_SEQ_CST);
    CcNtDestroyMap(NtMap);
    return NULL;
}

static void *
CcmmWriteWorker(void *Context)
{
    CCMM_FILE *File = Context;

    WorldAttach(File->World, 2, NULL);
    CHECK(MiWriteModifiedPages(&File->World->System, 1) == 1);
    return NULL;
}

static
NTSTATUS
CcmmNtMakeViewResident(PVOID Context, PVOID Base, ULONG Length)
{
    PCC_NT_MAP NtMap = Context;

    return CcmmMakeViewResident(NtMap->FileObject->FsContext, Base, Length);
}

static void
CcmmNtClose(ULONG64 FileSize, BOOLEAN PendingWrite, BOOLEAN DirtyPinned)
{
    static TEST_WORLD World;
    CCMM_FILE File;
    CC_CACHE Cache;
    FILE_OBJECT Object = { .References = 1, .FsContext = &File };
    MI_CONTROL_AREA Control;
    KEVENT Event = { .Context = &File };
    PCC_NT_MAP NtMap = calloc(1, sizeof(*NtMap));
    CCMM_MOVE Move = { &World, 0, NULL, 0, TRUE };
    UCHAR Data[16] = { 1, 2, 3, 4 };
    UCHAR Tail[sizeof(Data)];

    WorldCreate(&World, 256, 3, 10000);
    World.System.UnusedSegmentLimit = 4;
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    CcmmFileCreate(&File, &World, &Cache, PAGE_SIZE * 3);
    memcpy(Tail, File.File.Data + PAGE_SIZE * 2, sizeof(Tail));
    CHECK(CcMapUninitialize(&File.Map));
    Control.Segment = File.Segment;
    NtMap->Control = &Control;
    NtMap->FileObject = &Object;
    NtMap->UninitializeEvent = &Event;
    CcMapInitialize(&NtMap->Map, &Cache, &CcNtBackingOps, NtMap, PAGE_SIZE * 3, PAGE_SIZE * 3, PAGE_SIZE * 3);
    NtMap->Map.Ops.MakeViewResident = CcmmNtMakeViewResident;
    Move.Buffer = Data;
    CHECK(NT_SUCCESS(CcCopyRange(&NtMap->Map, 0, sizeof(Data), TRUE, CcmmMove, &Move, NULL)));
    Move.BufferOffset = PAGE_SIZE * 2;
    CHECK(NT_SUCCESS(CcCopyRange(&NtMap->Map, PAGE_SIZE * 2, sizeof(Data), TRUE, CcmmMove, &Move, NULL)));
    if (DirtyPinned)
    {
        PCC_BCB Bcb;
        BOOLEAN Created;

        CHECK(NT_SUCCESS(CcBcbAcquire(&NtMap->Map, 0, sizeof(Data), TRUE, FALSE, &Bcb, &Created)));
        CHECK(NT_SUCCESS(CcBcbSetDirty(Bcb, 42)));
        CcBcbDereference(Bcb);
        CHECK(!IsListEmpty(&NtMap->Map.BcbList));
    }
    CcDirtyDiscard(&NtMap->Map, FileSize, (ULONG64)-1);
    CcMapSetSizes(&NtMap->Map, PAGE_SIZE * 3, FileSize, FileSize);
    if (PendingWrite)
    {
        pthread_t Writer, Closer;
        double Deadline = NowSeconds() + 2.0;

        CHECK(CcMapDetachViews(&NtMap->Map, 0, (ULONG64)-1));
        __atomic_store_n(&File.BlockWrites, 1, __ATOMIC_SEQ_CST);
        pthread_create(&Writer, NULL, CcmmWriteWorker, &File);
        while (!MI_ATOMIC_READ32(&File.WriteEntered) && NowSeconds() < Deadline)
            sched_yield();
        CHECK(MI_ATOMIC_READ32(&File.WriteEntered) == 1);
        pthread_create(&Closer, NULL, CcmmCloseWorker, NtMap);
        while (!MI_ATOMIC_READ32(&File.CloseStarted) && NowSeconds() < Deadline)
            sched_yield();
        CHECK(MI_ATOMIC_READ32(&File.CloseStarted) == 1);
        CHECK(MI_ATOMIC_READ32(&Event.State) == 0);
        CHECK(MI_ATOMIC_READ32(&File.Released) == 0);
        __atomic_store_n(&File.ReleaseWriter, 1, __ATOMIC_SEQ_CST);
        pthread_join(Writer, NULL);
        pthread_join(Closer, NULL);
        CHECK(File.File.Writes == 1);
    }
    else
    {
        CcNtDestroyMap(NtMap);
        CHECK((File.File.Writes == 0) == (FileSize == 0 && !DirtyPinned));
        CHECK(File.File.Writes == (FileSize == 0 ? (DirtyPinned ? 1 : 0) : FileSize > PAGE_SIZE * 2 ? 2 : 1));
        if (FileSize != 0 || DirtyPinned)
            CHECK(memcmp(File.File.Data, Data, sizeof(Data)) == 0);
        if (FileSize <= PAGE_SIZE * 2)
            CHECK(memcmp(File.File.Data + PAGE_SIZE * 2, Tail, sizeof(Tail)) == 0);
    }
    CHECK(Event.State == 1 && Object.References == 0);
    CHECK(File.Released == 1);
    CHECK(MiWriteModifiedPages(&World.System, 256) == 0);
    CHECK(World.System.UnusedSegmentCount == 0);
    MiSegmentPurgeUnused(&World.System, ~0u);
    CcCacheUninitialize(&Cache);
    FileDestroy(&File.File);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
}

static VOID
CcmmCloseDuringUnmap(PVOID Context, PVOID Base)
{
    PCC_NT_MAP NtMap = Context;
    CCMM_FILE *File = NtMap->FileObject->FsContext;

    CHECK(MiHostIrql == 0);
    if (!File->CloseStarted)
    {
        File->CloseStarted = TRUE;
        CHECK(NtMap->Map.ReclaimsInProgress == 1);
        CcNtDestroyMap(NtMap);
        CHECK(NtMap->Map.ReclaimsDraining);
        CHECK(NtMap->UninitializeEvent->State == 0);
        CHECK(NtMap->FileObject->References == 1);
        CHECK(File->Released == 0);
        CHECK(CcCacheTrim(NtMap->Map.Cache, 1) == 0);
    }
    CcNtBackingOps.UnmapView(Context, Base);
    CHECK(File->Released == 0);
}

static void
CcmmNtCloseReclaim(BOOLEAN Trim)
{
    TEST_WORLD World;
    CCMM_FILE File, Other;
    CC_CACHE Cache;
    FILE_OBJECT Object = { .References = 1, .FsContext = &File };
    MI_CONTROL_AREA Control;
    KEVENT Event = { .Context = &File };
    PCC_NT_MAP NtMap = calloc(1, sizeof(*NtMap));
    CC_VIEW_RANGE Range;
    ULONG i;

    WorldCreate(&World, 256, 1, 10000);
    WorldAttach(&World, 0, NULL);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    CcmmFileCreate(&File, &World, &Cache, (ULONG64)Cache.ViewCount * CC_VIEW_SIZE);
    CcmmFileCreate(&Other, &World, &Cache, PAGE_SIZE);
    CHECK(CcMapUninitialize(&File.Map));
    Control.Segment = File.Segment;
    NtMap->Control = &Control;
    NtMap->FileObject = &Object;
    NtMap->UninitializeEvent = &Event;
    CcMapInitialize(&NtMap->Map, &Cache, &CcNtBackingOps, NtMap,
                    File.File.Size, File.File.Size, File.File.Size);
    NtMap->Map.Ops.UnmapView = CcmmCloseDuringUnmap;
    for (i = 0; i < Cache.ViewCount; i++)
    {
        CHECK(NT_SUCCESS(CcViewAcquire(&NtMap->Map, (ULONG64)i * CC_VIEW_SIZE, 1, &Range)));
        CcViewRelease(&Range);
    }
    CHECK(File.Segment->MappedViews == (LONG)Cache.ViewCount);
    if (Trim)
        CHECK(CcCacheTrim(&Cache, 1) == 1);
    else
    {
        CHECK(NT_SUCCESS(CcViewAcquire(&Other.Map, 0, 1, &Range)));
        CcViewRelease(&Range);
    }
    CHECK(File.CloseStarted && File.Released == 1);
    CHECK(Event.State == 1 && Object.References == 0);
    CcmmFileDestroy(&Other);
    CHECK(CcCacheCheck(&Cache) == 0);
    CcCacheUninitialize(&Cache);
    WorldExpectClean(&World, 256);
    FileDestroy(&File.File);
    WorldDestroy(&World);
}

static void
CcmmTruncation(void)
{
    TEST_WORLD World;
    CCMM_FILE File;
    CC_CACHE Cache;
    CC_NT_MAP NtMap = {0};
    FILE_OBJECT Object = { .FsContext = &File };
    MI_CONTROL_AREA Control;
    SECTION_OBJECT_POINTERS Pointers = { .DataSectionObject = &Control };
    LARGE_INTEGER Size = { .QuadPart = PAGE_SIZE };
    MI_ADDRESS_SPACE Process, Child;
    CC_VIEW_RANGE Range;
    UCHAR Data = 42;
    CCMM_MOVE Move = { &World, 0, &Data, 0, TRUE };
    ULONG64 Base = 0, ViewSize = PAGE_SIZE, Conflict;

    WorldCreate(&World, 256, 1, 10000);
    WorldAttach(&World, 0, NULL);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    CcmmFileCreate(&File, &World, &Cache, 3 * PAGE_SIZE);
    CHECK(CcMapUninitialize(&File.Map));
    Control.Segment = File.Segment;
    NtMap.Control = &Control;
    NtMap.FileObject = &Object;
    CcMapInitialize(&NtMap.Map, &Cache, &CcNtBackingOps, &NtMap, File.File.Size, File.File.Size, File.File.Size);
    NtMap.Map.Ops.MakeViewResident = CcmmNtMakeViewResident;
    CHECK(NT_SUCCESS(CcCopyRange(&NtMap.Map, 0, 1, TRUE, CcmmMove, &Move, NULL)));
    Move.ToCache = FALSE;
    CHECK(NT_SUCCESS(CcCopyRange(&NtMap.Map, 0, 1, FALSE, CcmmMove, &Move, NULL)));
    CHECK(Data == 42);
    CHECK(NT_SUCCESS(CcViewAcquire(&NtMap.Map, 0, 1, &Range)));
    CHECK(File.Segment->MappedViews == 1 && File.Segment->TruncationViews == 0);
    CHECK(MmCanFileBeTruncated(&Pointers, &Size));
    CcViewRelease(&Range);
    CHECK(File.Segment->MappedViews == 1);
    CHECK(MmCanFileBeTruncated(&Pointers, &Size));
    CHECK(MmCanFileBeTruncated(&Pointers, NULL));

    ProcessCreate(&World, &Process);
    ProcessCreate(&World, &Child);
    CHECK(NT_SUCCESS(MiMapView(&Process, File.Segment, &Base, 0, &ViewSize, MI_PROT_READONLY, 0)));
    CHECK(File.Segment->TruncationViews == 1);
    CHECK(!MmCanFileBeTruncated(&Pointers, &Size));
    CHECK(!MmCanFileBeTruncated(&Pointers, NULL));
    Size.QuadPart = File.File.Size;
    CHECK(MmCanFileBeTruncated(&Pointers, &Size));
    Conflict = Base;
    CHECK(MiMapView(&Process, File.Segment, &Conflict, 0, &ViewSize, MI_PROT_READONLY, 0) == STATUS_CONFLICTING_ADDRESSES);
    CHECK(File.Segment->TruncationViews == 1);
    CHECK(NT_SUCCESS(MiCloneAddressSpace(&Process, &Child)));
    CHECK(File.Segment->TruncationViews == 2);
    CHECK(NT_SUCCESS(MiUnmapView(&Process, Base)));
    Size.QuadPart = PAGE_SIZE;
    CHECK(!MmCanFileBeTruncated(&Pointers, &Size));
    ProcessDestroy(&World, &Child);
    ProcessDestroy(&World, &Process);
    CHECK(File.Segment->TruncationViews == 0);
    CHECK(MmCanFileBeTruncated(&Pointers, &Size));
    WorldAttach(&World, 0, NULL);
    Base = 0;
    CHECK(NT_SUCCESS(MiMapView(&World.System.SystemSpace, File.Segment, &Base, 0, &ViewSize, MI_PROT_READONLY, 0)));
    CHECK(!MmCanFileBeTruncated(&Pointers, &Size));
    CHECK(NT_SUCCESS(MiUnmapView(&World.System.SystemSpace, Base)));
    CHECK(MmCanFileBeTruncated(&Pointers, &Size));
    Pointers.ImageSectionObject = &Control;
    CHECK(!MmCanFileBeTruncated(&Pointers, &Size));
    Pointers.ImageSectionObject = NULL;
    CHECK(NT_SUCCESS(CcDirtyFlush(&NtMap.Map, 0, File.File.Size, ~0u, NULL)));
    CHECK(CcMapUninitialize(&NtMap.Map));
    CHECK(File.Segment->MappedViews == 0 && File.Segment->TruncationViews == 0);
    CHECK(MiSegmentDereferenceAndClose(File.Segment));
    CcCacheUninitialize(&Cache);
    WorldExpectClean(&World, 256);
    FileDestroy(&File.File);
    WorldDestroy(&World);
}

static NTSTATUS
CcmmReadAsync(PVOID Context, ULONG64 Offset, ULONG Frame, PVOID Buffer,
              MI_READ_COMPLETION Completion, PVOID CompletionContext)
{
    CCMM_FILE *File = CONTAINING_RECORD(Context, CCMM_FILE, File);
    ULONG Index = File->ReadCount++;

    CHECK(Index < RTL_NUMBER_OF(File->ReadCompletion));
    memcpy(Buffer, File->File.Data + Offset, PAGE_SIZE);
    File->ReadCompletion[Index] = Completion;
    File->ReadContext[Index] = CompletionContext;
    return STATUS_PENDING;
}

static void
CcmmNtCloseRead(NTSTATUS ReadStatus, BOOLEAN WithEvent, BOOLEAN Concurrent)
{
    TEST_WORLD World;
    CCMM_FILE File;
    CC_CACHE Cache;
    FILE_OBJECT Object = { .References = 1, .FsContext = &File };
    MI_CONTROL_AREA Control;
    KEVENT Event = { .Context = &File };
    PCC_NT_MAP NtMap = calloc(1, sizeof(*NtMap));
    pthread_t Closer;
    ULONG i;

    WorldCreate(&World, 256, 2, 10000);
    World.System.UnusedSegmentLimit = 4;
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    CcmmFileCreate(&File, &World, &Cache, 2 * PAGE_SIZE);
    CHECK(CcMapUninitialize(&File.Map));
    File.Segment->FileOps.ReadAsync = CcmmReadAsync;
    Control.Segment = File.Segment;
    NtMap->Control = &Control;
    NtMap->FileObject = &Object;
    NtMap->UninitializeEvent = WithEvent ? &Event : NULL;
    CcMapInitialize(&NtMap->Map, &Cache, &CcNtBackingOps, NtMap, 2 * PAGE_SIZE, 2 * PAGE_SIZE, 2 * PAGE_SIZE);
    CHECK(NT_SUCCESS(CcNtBackingOps.Prefetch(NtMap, 0, 2 * PAGE_SIZE)));
    CHECK(File.ReadCount == 2 && File.Segment->PendingReadCount == 2);
    if (Concurrent)
    {
        double Deadline = NowSeconds() + 2.0;

        CHECK(pthread_create(&Closer, NULL, CcmmCloseWorker, NtMap) == 0);
        while (!MI_ATOMIC_READ32(&File.CloseStarted) && NowSeconds() < Deadline)
            sched_yield();
        CHECK(MI_ATOMIC_READ32(&File.CloseStarted) == 1);
    }
    else
    {
        CcNtDestroyMap(NtMap);
        CHECK(Event.State == 0 && Object.References == 1 && File.Released == 0);
    }
    for (i = 0; i < File.ReadCount; i++)
    {
        File.ReadCompletion[i](File.ReadContext[i], ReadStatus);
        if (!Concurrent && i + 1 < File.ReadCount)
            CHECK(Event.State == 0 && Object.References == 1 && File.Released == 0);
    }
    if (Concurrent)
        CHECK(pthread_join(Closer, NULL) == 0);
    CHECK(Event.State == WithEvent && Object.References == 0);
    CHECK(File.Released == WithEvent);
    CHECK(World.System.UnusedSegmentCount == !WithEvent);
    if (!WithEvent && File.Released == 0)
    {
        CHECK(MiSegmentTryReference(File.Segment));
        CHECK(World.System.UnusedSegmentCount == 0);
        CHECK(File.Segment->ReferenceCount == 1);
        CHECK(NT_SUCCESS(MiSegmentMakeResident(File.Segment, 0, PAGE_SIZE)));
        CHECK(MiSegmentDereferenceAndClose(File.Segment));
        CHECK(File.Released == 1);
    }
    MiSegmentPurgeUnused(&World.System, ~0u);
    CcCacheUninitialize(&Cache);
    FileDestroy(&File.File);
    WorldExpectClean(&World, 256);
    WorldDestroy(&World);
}

static void
CcmmNtPinExtent(void)
{
    static const ULONG64 Offsets[] = {0, CC_VIEW_SIZE + PAGE_SIZE, 2 * CC_VIEW_SIZE + PAGE_SIZE};
    static TEST_WORLD World;
    CCMM_FILE File = { .World = &World };
    CC_CACHE Cache;
    FILE_OBJECT Object = { .References = 1, .FsContext = &File };
    MI_CONTROL_AREA Control;
    KEVENT Event = { .Context = &File };
    PCC_NT_MAP NtMap = calloc(1, sizeof(*NtMap));
    MI_FILE_OPS Ops = TestFileOps;
    UCHAR Data[4 * PAGE_SIZE];
    CCMM_MOVE Move = { &World, 0, Data, 0, TRUE };
    ULONG i;

    WorldCreate(&World, 512, 1, 10000);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    FileCreate(&File.File, 3 * CC_VIEW_SIZE);
    Ops.Release = CcmmFileRelease;
    CHECK(NT_SUCCESS(MiSegmentCreate(&World.System, MiSegmentDataFile, 512, MI_PROT_READWRITE,
                                     &Ops, &File.File, NULL, 0, &File.Segment)));
    Control.Segment = File.Segment;
    NtMap->Control = &Control;
    NtMap->FileObject = &Object;
    NtMap->UninitializeEvent = &Event;
    NtMap->PinAccess = TRUE;
    CcMapInitialize(&NtMap->Map, &Cache, &CcNtBackingOps, NtMap, 512, 496, 496);
    memset(Data, 0xBD, sizeof(Data));

    for (i = 0; i < RTL_NUMBER_OF(Offsets); i++)
    {
        ULONG Length = i == 0 ? sizeof(Data) : PAGE_SIZE;
        PCC_BCB Bcb;
        BOOLEAN Created;
        NTSTATUS Status = CcBcbAcquire(&NtMap->Map, Offsets[i], Length, TRUE, FALSE, &Bcb, &Created);

        CHECK(NT_SUCCESS(Status));
        CHECK(NtMap->Map.FileSize == 496 && NtMap->Map.ValidDataLength == 496);
        CHECK(NtMap->Map.SectionSize >= Offsets[i] + Length);
        CHECK((ULONG64)File.Segment->SizeInBytes >= Offsets[i] + Length);
        if (!NT_SUCCESS(Status))
            continue;
        if ((ULONG64)File.Segment->SizeInBytes >= Offsets[i] + Length)
        {
            CHECK(NT_SUCCESS(CcBcbMakeResident(Bcb, FALSE)));
            Move.BufferOffset = Offsets[i];
            CHECK(NT_SUCCESS(CcmmMove(&Move, CcBcbAddress(Bcb, Offsets[i]), Offsets[i], Length)));
            CHECK(NT_SUCCESS(CcBcbSetDirty(Bcb, 0)));
        }
        CcBcbDereference(Bcb);
    }

    CcDirtyDiscard(&NtMap->Map, 0, (ULONG64)-1);
    CcMapSetSizes(&NtMap->Map, NtMap->Map.SectionSize, 0, 0);
    CcNtDestroyMap(NtMap);
    CHECK(File.File.Writes == 6);
    CHECK(memcmp(File.File.Data, Data, sizeof(Data)) == 0);
    for (i = 1; i < RTL_NUMBER_OF(Offsets); i++)
        CHECK(memcmp(File.File.Data + Offsets[i], Data, PAGE_SIZE) == 0);
    CHECK(Event.State == 1 && Object.References == 0 && File.Released == 1);
    CHECK(MiWriteModifiedPages(&World.System, 512) == 0);
    CcCacheUninitialize(&Cache);
    FileDestroy(&File.File);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

static void
CcmmNtDirty(void)
{
    TEST_WORLD World;
    CCMM_FILE File;
    CC_CACHE Cache;
    MI_CONTROL_AREA Control;
    CC_NT_MAP NtMap = {0};
    CC_VIEW_RANGE Range;
    UCHAR Data[16 * PAGE_SIZE];
    CCMM_MOVE Move = { &World, 0, Data, 0, TRUE };
    ULONG64 Raises;
    LONG64 Invalidations;

    WorldCreate(&World, 512, 1, 10000);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    CcmmFileCreate(&File, &World, &Cache, sizeof(Data));
    CHECK(CcMapUninitialize(&File.Map));
    Control.Segment = File.Segment;
    NtMap.Control = &Control;
    CcMapInitialize(&NtMap.Map, &Cache, &CcNtBackingOps, &NtMap, sizeof(Data), sizeof(Data), sizeof(Data));
    CHECK(NT_SUCCESS(CcViewAcquire(&NtMap.Map, 0, sizeof(Data), &Range)));
    memset(Data, 0x6C, sizeof(Data));
    CHECK(NT_SUCCESS(CcmmMove(&Move, Range.Address, 0, sizeof(Data))));
    Raises = MiHostIrqlRaises;
    Invalidations = World.Machine.TlbInvalidations;
    CHECK(NT_SUCCESS(CcDirtyMark(&NtMap.Map, 0, sizeof(Data))));
    CHECK(MiHostIrqlRaises <= Raises + 1);
    CHECK(World.Machine.TlbInvalidations == Invalidations);
    CHECK(NT_SUCCESS(CcDirtyFlush(&NtMap.Map, 0, sizeof(Data), ~0u, NULL)));
    CHECK(memcmp(File.File.Data, Data, sizeof(Data)) == 0);
    CHECK(File.File.Writes == 16);

    memset(Data, 0xA9, sizeof(Data));
    CHECK(NT_SUCCESS(CcmmMove(&Move, Range.Address, 0, sizeof(Data))));
    CHECK(NT_SUCCESS(CcDirtyMark(&NtMap.Map, 0, sizeof(Data))));
    CHECK(NT_SUCCESS(CcDirtyFlush(&NtMap.Map, 0, sizeof(Data), ~0u, NULL)));
    CHECK(memcmp(File.File.Data, Data, sizeof(Data)) == 0 && File.File.Writes == 32);
    CHECK(MiTrimAddressSpace(&World.System.SystemSpace, 32, TRUE) != 0);
    CHECK(NT_SUCCESS(CcDirtyMark(&NtMap.Map, 0, sizeof(Data))));
    CHECK(NT_SUCCESS(CcDirtyFlush(&NtMap.Map, 0, sizeof(Data), ~0u, NULL)));
    CHECK(memcmp(File.File.Data, Data, sizeof(Data)) == 0 && File.File.Writes == 48);
    CcViewRelease(&Range);
    CHECK(CcMapUninitialize(&NtMap.Map));
    CHECK(MiSegmentDereferenceAndClose(File.Segment));
    CcCacheUninitialize(&Cache);
    FileDestroy(&File.File);
    WorldExpectClean(&World, 512);
    WorldDestroy(&World);
}

typedef struct _CCMM_DIRTY_BENCH
{
    PCC_MAP Map;
    ULONG Cpu;
    volatile LONG *Ready;
    volatile LONG *Go;
    volatile LONG *Stop;
    ULONG64 Calls;
} CCMM_DIRTY_BENCH;

static void *
CcmmDirtyBenchWorker(void *Context)
{
    CCMM_DIRTY_BENCH *Thread = Context;

    MiHostCpu = Thread->Cpu;
    CcHostProcessor = Thread->Cpu;
    MI_ATOMIC_ADD32(Thread->Ready, 1);
    while (!MI_ATOMIC_READ32(Thread->Go))
        sched_yield();
    while (!MI_ATOMIC_READ32(Thread->Stop))
    {
        if (!NT_SUCCESS(CcDirtyMark(Thread->Map, 0, 16 * PAGE_SIZE)))
            abort();
        Thread->Calls++;
    }
    return NULL;
}

void
TestBenchCcDirty(void)
{
    ULONG Count;

    for (Count = 1; Count <= MACHINE_MAX_CPUS; Count *= 2)
    {
        TEST_WORLD World;
        CCMM_FILE Files[MACHINE_MAX_CPUS];
        MI_CONTROL_AREA Controls[MACHINE_MAX_CPUS];
        CC_NT_MAP Maps[MACHINE_MAX_CPUS] = {0};
        CC_VIEW_RANGE Ranges[MACHINE_MAX_CPUS];
        CCMM_DIRTY_BENCH Threads[MACHINE_MAX_CPUS] = {0};
        pthread_t Workers[MACHINE_MAX_CPUS];
        CC_CACHE Cache;
        UCHAR Data[16 * PAGE_SIZE];
        CCMM_MOVE Move = { &World, 0, Data, 0, TRUE };
        volatile LONG Ready = 0, Go = 0, Stop = 0;
        ULONG64 Calls = 0;
        double Start, Elapsed;
        ULONG i;

        WorldCreate(&World, 2048, Count, 100000);
        CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 100000)));
        memset(Data, 0xC3, sizeof(Data));
        for (i = 0; i < Count; i++)
        {
            CcmmFileCreate(&Files[i], &World, &Cache, sizeof(Data));
            CHECK(CcMapUninitialize(&Files[i].Map));
            Controls[i].Segment = Files[i].Segment;
            Maps[i].Control = &Controls[i];
            CcMapInitialize(&Maps[i].Map, &Cache, &CcNtBackingOps, &Maps[i], sizeof(Data), sizeof(Data), sizeof(Data));
            CHECK(NT_SUCCESS(CcViewAcquire(&Maps[i].Map, 0, sizeof(Data), &Ranges[i])));
            CHECK(NT_SUCCESS(CcmmMove(&Move, Ranges[i].Address, 0, sizeof(Data))));
            CHECK(NT_SUCCESS(CcDirtyMark(&Maps[i].Map, 0, sizeof(Data))));
            Threads[i].Map = &Maps[i].Map;
            Threads[i].Cpu = i;
            Threads[i].Ready = &Ready;
            Threads[i].Go = &Go;
            Threads[i].Stop = &Stop;
            CHECK(pthread_create(&Workers[i], NULL, CcmmDirtyBenchWorker, &Threads[i]) == 0);
        }
        while (MI_ATOMIC_READ32(&Ready) != (LONG)Count)
            sched_yield();
        Start = NowSeconds();
        MI_ATOMIC_ADD32(&Go, 1);
        while (NowSeconds() - Start < 0.6)
            sched_yield();
        MI_ATOMIC_ADD32(&Stop, 1);
        for (i = 0; i < Count; i++)
        {
            CHECK(pthread_join(Workers[i], NULL) == 0);
            Calls += Threads[i].Calls;
        }
        Elapsed = NowSeconds() - Start;
        printf("cc_dirty_64k threads=%u calls_per_sec=%.0f\n", Count, Calls / Elapsed);
        for (i = 0; i < Count; i++)
        {
            CHECK(NT_SUCCESS(CcDirtyFlush(&Maps[i].Map, 0, sizeof(Data), ~0u, NULL)));
            CcViewRelease(&Ranges[i]);
            CHECK(CcMapUninitialize(&Maps[i].Map));
            CHECK(MiSegmentDereferenceAndClose(Files[i].Segment));
            FileDestroy(&Files[i].File);
        }
        CcCacheUninitialize(&Cache);
        WorldExpectClean(&World, 2048);
        WorldDestroy(&World);
    }
}

LONG
KeSetEvent(KEVENT *Event, LONG Increment, BOOLEAN Wait)
{
    CCMM_FILE *File = Event->Context;

    CHECK(File->Released == 1);
    CHECK(File->Views == 0);
    CHECK(MiPfnListCount(&File->World->System.Pfn, MiPageModified) == 0);
    __atomic_store_n(&Event->State, 1, __ATOMIC_SEQ_CST);
    return 0;
}

VOID
ObDereferenceObject(PVOID Object)
{
    PFILE_OBJECT File = Object;

    CHECK(File->References-- > 0);
}

VOID
MiDereferenceControlArea(PMI_CONTROL_AREA Control)
{
    MiSegmentDereference(Control->Segment);
}

BOOLEAN
MmFlushImageSection(PSECTION_OBJECT_POINTERS Pointers, ULONG Type)
{
    CHECK(Type == MmFlushForWrite);
    return Pointers->ImageSectionObject == NULL;
}

PMI_CONTROL_AREA
MiReferenceDataControlArea(PSECTION_OBJECT_POINTERS Pointers)
{
    PMI_CONTROL_AREA Control = Pointers->DataSectionObject;

    return Control != NULL && MiSegmentTryReference(Control->Segment) ? Control : NULL;
}

static
void
CcmmCachedReopen(BOOLEAN Purge)
{
    TEST_WORLD World;
    CCMM_FILE File;
    CC_CACHE Cache;
    FILE_OBJECT Object = { .References = 1, .FsContext = &File };
    MI_CONTROL_AREA Control;
    PCC_NT_MAP NtMap = calloc(1, sizeof(*NtMap));
    SECTION_OBJECT_POINTERS Pointers = { .DataSectionObject = &Control, .SharedCacheMap = NtMap };
    UCHAR Data[PAGE_SIZE];
    CCMM_MOVE Move = { &World, 0, Data, 0, FALSE };
    ULONG Reads;

    WorldCreate(&World, 256, 1, 10000);
    World.System.UnusedSegmentLimit = 4;
    WorldAttach(&World, 0, NULL);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    CcmmFileCreate(&File, &World, &Cache, sizeof(Data));
    CHECK(CcMapUninitialize(&File.Map));
    Control.Segment = File.Segment;
    NtMap->Control = &Control;
    NtMap->Pointers = &Pointers;
    NtMap->FileObject = &Object;
    NtMap->ReferenceCount = 1;
    InsertTailList(&CcNtMapList, &NtMap->Link);
    CcMapInitialize(&NtMap->Map, &Cache, &CcNtBackingOps, NtMap, sizeof(Data), sizeof(Data), sizeof(Data));
    NtMap->Map.Ops.MakeViewResident = CcmmNtMakeViewResident;
    NtMap->Map.OpenCount = 1;
    CHECK(NT_SUCCESS(CcCopyRange(&NtMap->Map, 0, sizeof(Data), FALSE, CcmmMove, &Move, NULL)));
    CHECK(memcmp(Data, File.File.Data, sizeof(Data)) == 0);
    Reads = File.File.Reads;
    NtMap->Map.OpenCount = 0;
    CcNtDereferenceMap(NtMap);
    CHECK(Pointers.SharedCacheMap == NtMap && Object.References == 1 && File.Released == 0);
    CHECK(File.Segment->MappedViews == 1 && NtMap->ReferenceCount == 0);
    CHECK(CcNtReferenceMap(&Pointers) == NtMap);
    NtMap->Map.OpenCount = 1;
    CcNtDereferenceMap(NtMap);
    CcNtExpireClosedMaps();
    CHECK(Pointers.SharedCacheMap == NtMap && File.Segment->MappedViews == 1);
    CHECK(NT_SUCCESS(CcCopyRange(&NtMap->Map, 0, sizeof(Data), FALSE, CcmmMove, &Move, NULL)));
    CHECK(memcmp(Data, File.File.Data, sizeof(Data)) == 0 && File.File.Reads == Reads);
    CHECK(CcNtReferenceMap(&Pointers) == NtMap);
    NtMap->Map.OpenCount = 0;
    CcNtDereferenceMap(NtMap);
    if (Purge)
        CHECK(CcPurgeCacheSection(&Pointers, NULL, 0, UNINITIALIZE_CACHE_MAPS));
    else
        CcNtExpireClosedMaps();
    CHECK(Pointers.SharedCacheMap == NULL && IsListEmpty(&CcNtMapList));
    CHECK(Object.References == 0 && File.Released == Purge);
    MiSegmentPurgeUnused(&World.System, ~0u);
    CHECK(File.Released == 1);
    CcCacheUninitialize(&Cache);
    WorldExpectClean(&World, 256);
    FileDestroy(&File.File);
    WorldDestroy(&World);
}

static
void
CcmmPurgeCached(ULONG Flags, BOOLEAN Partial)
{
    TEST_WORLD World;
    CCMM_FILE File;
    CC_CACHE Cache;
    FILE_OBJECT Object = { .References = 1, .FsContext = &File };
    MI_CONTROL_AREA Control;
    PCC_NT_MAP NtMap = calloc(1, sizeof(*NtMap));
    SECTION_OBJECT_POINTERS Pointers = { .DataSectionObject = &Control, .SharedCacheMap = NtMap };
    LARGE_INTEGER Offset = { .QuadPart = PAGE_SIZE };
    UCHAR Data[2 * PAGE_SIZE];
    CCMM_MOVE Move = { &World, 0, Data, 0, TRUE };
    ULONG64 Base = 0, Size = PAGE_SIZE;
    UCHAR Byte;
    BOOLEAN Close = (BOOLEAN)(Flags != 0 && !Partial);

    WorldCreate(&World, 256, 1, 10000);
    World.System.UnusedSegmentLimit = 4;
    WorldAttach(&World, 0, NULL);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    CcmmFileCreate(&File, &World, &Cache, sizeof(Data));
    CHECK(CcMapUninitialize(&File.Map));
    Control.Segment = File.Segment;
    NtMap->Control = &Control;
    NtMap->FileObject = &Object;
    NtMap->ReferenceCount = 1;
    CcMapInitialize(&NtMap->Map, &Cache, &CcNtBackingOps, NtMap, sizeof(Data), sizeof(Data), sizeof(Data));
    NtMap->Map.Ops.MakeViewResident = CcmmNtMakeViewResident;
    CHECK(NT_SUCCESS(MiMapView(&World.System.SystemSpace, File.Segment, &Base, 0, &Size, MI_PROT_READONLY, 0)));
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base, &Byte, 1, MachineRead, FALSE)));
    CHECK(!CcPurgeCacheSection(&Pointers, NULL, 0, UNINITIALIZE_CACHE_MAPS));
    CHECK(!NtMap->PurgeOnClose);
    CHECK(NT_SUCCESS(MiUnmapView(&World.System.SystemSpace, Base)));
    memset(Data, 0xA7, sizeof(Data));
    CHECK(NT_SUCCESS(CcCopyRange(&NtMap->Map, 0, sizeof(Data), TRUE, CcmmMove, &Move, NULL)));
    CHECK(CcPurgeCacheSection(&Pointers, Partial ? &Offset : NULL, 0, Flags));
    CHECK(NtMap->PurgeOnClose == Close);
    CcNtDestroyMap(NtMap);
    CHECK(Object.References == 0 && File.Released == Close);
    CHECK(World.System.UnusedSegmentCount == !Close);
    CHECK(File.File.Writes == Partial);
    if (Partial)
        CHECK(memcmp(File.File.Data, Data, PAGE_SIZE) == 0);
    MiSegmentPurgeUnused(&World.System, ~0u);
    CHECK(File.Released == 1);
    CcCacheUninitialize(&Cache);
    WorldExpectClean(&World, 256);
    FileDestroy(&File.File);
    WorldDestroy(&World);
}

static
void
CcmmPurgeWithoutCache(void)
{
    TEST_WORLD World;
    CCMM_FILE File;
    CC_CACHE Cache;
    MI_CONTROL_AREA Control;
    SECTION_OBJECT_POINTERS Pointers = { .DataSectionObject = &Control };
    LARGE_INTEGER Offset = { .QuadPart = PAGE_SIZE };
    ULONG64 Base = 0, Size = PAGE_SIZE;
    UCHAR Byte;

    WorldCreate(&World, 256, 1, 10000);
    World.System.UnusedSegmentLimit = 4;
    WorldAttach(&World, 0, NULL);
    CHECK(NT_SUCCESS(CcCacheInitialize(&Cache, CC_MIN_VIEWS, 1024)));
    CcmmFileCreate(&File, &World, &Cache, 3 * PAGE_SIZE);
    CHECK(CcMapUninitialize(&File.Map));
    Control.Segment = File.Segment;
    CHECK(NT_SUCCESS(MiMapView(&World.System.SystemSpace, File.Segment, &Base, 0, &Size, MI_PROT_READONLY, 0)));
    CHECK(NT_SUCCESS(MachineAccessMemory(&World.Machine, 0, Base, &Byte, 1, MachineRead, FALSE)));
    MiSegmentDereference(File.Segment);
    CHECK(!CcPurgeCacheSection(&Pointers, NULL, 0, 0));
    CHECK(File.Released == 0 && File.Segment->MappedViews == 1);
    CHECK(NT_SUCCESS(MiUnmapView(&World.System.SystemSpace, Base)));
    CHECK(World.System.UnusedSegmentCount == 1);
    CHECK(CcPurgeCacheSection(&Pointers, &Offset, PAGE_SIZE, 0));
    CHECK(File.Released == 0 && World.System.UnusedSegmentCount == 1);
    CHECK(CcPurgeCacheSection(&Pointers, NULL, 0, 0));
    CHECK(File.Released == 1 && World.System.UnusedSegmentCount == 0);
    if (File.Released == 0)
        CHECK(MiSegmentPurgeUnused(&World.System, 1) == 1);
    CcCacheUninitialize(&Cache);
    WorldExpectClean(&World, 256);
    FileDestroy(&File.File);
    WorldDestroy(&World);
}

NTSTATUS
MiWaitForMemory(NTSTATUS Status, PULONG Attempts)
{
    return Status;
}

NTSTATUS
CcNtFlushMap(PCC_NT_MAP Map, ULONG64 Offset, ULONG64 Length, ULONG MaximumPages, PULONG PagesFlushed)
{
    return CcDirtyFlush(&Map->Map, Offset, Length, MaximumPages, PagesFlushed);
}

void
TestCcOnMm(void)
{
    ULONG i;

    CC_LOCK_INIT(&CcNtMapListLock);
    InitializeListHead(&CcNtMapList);
    CcmmFaultableCopy();
    CcmmExtendingCopy();
    CcmmTruncation();
    CcmmNtCloseReclaim(FALSE);
    CcmmNtCloseReclaim(TRUE);
    CcmmPurgeWithoutCache();
    CcmmPurgeCached(UNINITIALIZE_CACHE_MAPS, FALSE);
    CcmmPurgeCached(UNINITIALIZE_CACHE_MAPS, TRUE);
    CcmmPurgeCached(0, FALSE);
    CcmmCachedReopen(FALSE);
    CcmmCachedReopen(TRUE);
    CcmmBasic();
    CcmmGrow();
    CcmmNtClose(0, FALSE, FALSE);
    CcmmNtClose(PAGE_SIZE, FALSE, FALSE);
    CcmmNtClose(PAGE_SIZE + 1, FALSE, FALSE);
    CcmmNtClose(PAGE_SIZE * 3, FALSE, FALSE);
    CcmmNtClose(0, TRUE, FALSE);
    CcmmNtClose(0, FALSE, TRUE);
    CcmmNtCloseRead(STATUS_SUCCESS, TRUE, FALSE);
    CcmmNtCloseRead(STATUS_UNEXPECTED_IO_ERROR, TRUE, FALSE);
    CcmmNtCloseRead(STATUS_SUCCESS, FALSE, FALSE);
    for (i = 0; i < 64; i++)
        CcmmNtCloseRead(i & 1 ? STATUS_SUCCESS : STATUS_UNEXPECTED_IO_ERROR, (BOOLEAN)((i & 2) != 0), TRUE);
    CcmmNtPinExtent();
    CcmmNtDirty();
    CcmmSmp();
}
