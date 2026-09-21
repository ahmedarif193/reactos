/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/cc/host/t_system.c
 * PURPOSE:     Cache manager end-to-end host-native tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccharness.h"

#define MB (1024 * 1024)

#ifdef TEST_LIGHT
#define COHERENCY_OPERATIONS   4000
#define SMP_OPERATIONS         4000
#else
#define COHERENCY_OPERATIONS   40000
#define SMP_OPERATIONS         40000
#endif

#define SMP_THREADS   8
#define SMP_FILES     4
#define SMP_FILE_SIZE (24 * MB)

void
TestCoherency(void)
{
    const ULONG64 Size = 40 * MB + 12345;
    TEST_FILE File;
    CC_CACHE Cache;
    CC_MAP Map;
    PUCHAR Shadow = malloc((size_t)Size);
    PUCHAR Buffer = malloc(MB);
    ULONG64 Seed = 0xABCDEF12345ULL;
    ULONG Operation;
    ULONG64 i;

    FileCreate(&File, Size);
    for (i = 0; i < Size; i++)
        Shadow[i] = FilePattern(i);

    CHECK(CcCacheInitialize(&Cache, 64, 4000) == STATUS_SUCCESS);
    CcMapInitialize(&Map, &Cache, &File.Ops, &File, Size, Size, Size);

    for (Operation = 0; Operation < COHERENCY_OPERATIONS; Operation++)
    {
        ULONG64 Offset = Rng(&Seed) % Size;
        ULONG Length = 1 + (ULONG)(Rng(&Seed) % ((Rng(&Seed) & 7) ? 9000 : 700000));
        MOVE_CONTEXT Move = { Buffer, Offset };
        ULONG Kind = (ULONG)(Rng(&Seed) % 100);

        if (Offset + Length > Size)
            Length = (ULONG)(Size - Offset);

        if (Kind < 45)
        {
            CHECK(CcCopyRange(&Map, Offset, Length, FALSE, MoveToBuffer, &Move, NULL) == STATUS_SUCCESS);
            CHECK(memcmp(Buffer, Shadow + Offset, Length) == 0);
        }
        else if (Kind < 85)
        {
            ULONG j;

            for (j = 0; j < Length; j++)
                Buffer[j] = (UCHAR)Rng(&Seed);

            CHECK(CcCopyRange(&Map, Offset, Length, TRUE, MoveFromBuffer, &Move, NULL) == STATUS_SUCCESS);
            memcpy(Shadow + Offset, Buffer, Length);
            CHECK(memcmp(File.Pages + Offset, Shadow + Offset, Length) == 0);
        }
        else if (Kind < 93)
        {
            ULONG Target = CcDirtyLazyTarget(&Cache);
            PCC_MAP Dirty = CcDirtyNextMap(&Cache);

            if (Dirty != NULL)
                CHECK(CcDirtyFlush(Dirty, 0, (ULONG64)-1, Target, NULL) == STATUS_SUCCESS);
        }
        else if (Kind < 97)
        {
            CHECK(CcDirtyFlush(&Map, Offset, Length, 1000000, NULL) == STATUS_SUCCESS);
            CHECK(!CcDirtyQuery(&Map, Offset, Length));
            CHECK(memcmp(File.Disk + Offset, Shadow + Offset, Length) == 0);
        }
        else
        {
            CHECK(CcDirtyFlush(&Map, 0, (ULONG64)-1, 1000000, NULL) == STATUS_SUCCESS);
            CHECK(CcMapDetachViews(&Map, 0, (ULONG64)-1));
            CHECK(File.MappedViews == 0);
            CHECK(File.Ops.Purge(&File, 0, (ULONG64)-1));
        }
    }

    CHECK(CcDirtyFlush(&Map, 0, (ULONG64)-1, 1000000, NULL) == STATUS_SUCCESS);
    CHECK(Cache.TotalDirtyPages == 0);
    CHECK(memcmp(File.Disk, Shadow, (size_t)Size) == 0);
    CHECK(CcCacheCheck(&Cache) == 0);
    printf("  coherency: %lld view hits, %lld misses, %lld reclaims, %lld pages flushed in %lld calls\n",
           Cache.ViewHits, Cache.ViewMisses, Cache.ViewReclaims, Cache.PagesFlushed, Cache.FlushCalls);

    CHECK(CcMapUninitialize(&Map));
    CHECK(File.MappedViews == 0 && File.Errors == 0);
    CcCacheUninitialize(&Cache);
    FileDestroy(&File);
    free(Shadow);
    free(Buffer);
}

typedef struct _SMP_SHARED
{
    CC_CACHE Cache;
    TEST_FILE File[SMP_FILES];
    CC_MAP Map[SMP_FILES];
    PUCHAR Shadow[SMP_FILES];
    volatile LONG Stop;
} SMP_SHARED;

typedef struct _SMP_THREAD
{
    SMP_SHARED *Shared;
    ULONG Index;
    ULONG64 Operations;
} SMP_THREAD;

static
void *
SmpWorker(void *Argument)
{
    SMP_THREAD *Thread = Argument;
    SMP_SHARED *Shared = Thread->Shared;
    ULONG64 Seed = 0x9E3779B97F4A7C15ULL * (Thread->Index + 1);
    UCHAR Buffer[PAGE_SIZE];
    ULONG Operation;

    CcHostProcessor = Thread->Index;

    for (Operation = 0; Operation < SMP_OPERATIONS; Operation++)
    {
        ULONG FileIndex = (ULONG)(Rng(&Seed) % SMP_FILES);
        ULONG64 Pages = SMP_FILE_SIZE / PAGE_SIZE;
        ULONG64 Page = (Rng(&Seed) % (Pages / SMP_THREADS)) * SMP_THREADS + Thread->Index;
        ULONG64 Offset = Page * PAGE_SIZE;
        MOVE_CONTEXT Move = { Buffer, Offset };

        if (Rng(&Seed) & 1)
        {
            CHECK(CcCopyRange(&Shared->Map[FileIndex], Offset, PAGE_SIZE, FALSE, MoveToBuffer, &Move, NULL) ==
                  STATUS_SUCCESS);
            CHECK(memcmp(Buffer, Shared->Shadow[FileIndex] + Offset, PAGE_SIZE) == 0);
        }
        else
        {
            memset(Buffer, (int)Rng(&Seed), PAGE_SIZE);
            Buffer[0] = (UCHAR)Thread->Index;
            CHECK(CcCopyRange(&Shared->Map[FileIndex], Offset, PAGE_SIZE, TRUE, MoveFromBuffer, &Move, NULL) ==
                  STATUS_SUCCESS);
            memcpy(Shared->Shadow[FileIndex] + Offset, Buffer, PAGE_SIZE);
        }

        Thread->Operations++;
    }

    return NULL;
}

static
void *
SmpLazyWriter(void *Argument)
{
    SMP_SHARED *Shared = Argument;

    CcHostProcessor = SMP_THREADS;

    while (!__atomic_load_n(&Shared->Stop, __ATOMIC_SEQ_CST))
    {
        PCC_MAP Map = CcDirtyNextMap(&Shared->Cache);

        if (Map != NULL)
            CcDirtyFlush(Map, 0, (ULONG64)-1, CcDirtyLazyTarget(&Shared->Cache) + 8, NULL);
        else
            sched_yield();
    }

    return NULL;
}

void
TestSmp(void)
{
    static SMP_SHARED Shared;
    SMP_THREAD Threads[SMP_THREADS];
    pthread_t Handles[SMP_THREADS];
    pthread_t Lazy;
    double Start = NowSeconds();
    ULONG i;

    memset(&Shared, 0, sizeof(Shared));
    CHECK(CcCacheInitialize(&Shared.Cache, 96, 100000) == STATUS_SUCCESS);

    for (i = 0; i < SMP_FILES; i++)
    {
        ULONG64 j;

        FileCreate(&Shared.File[i], SMP_FILE_SIZE);
        Shared.Shadow[i] = malloc(SMP_FILE_SIZE);
        for (j = 0; j < SMP_FILE_SIZE; j++)
            Shared.Shadow[i][j] = FilePattern(j);

        CcMapInitialize(&Shared.Map[i], &Shared.Cache, &Shared.File[i].Ops, &Shared.File[i],
                        SMP_FILE_SIZE, SMP_FILE_SIZE, SMP_FILE_SIZE);
    }

    pthread_create(&Lazy, NULL, SmpLazyWriter, &Shared);
    for (i = 0; i < SMP_THREADS; i++)
    {
        Threads[i].Shared = &Shared;
        Threads[i].Index = i;
        Threads[i].Operations = 0;
        pthread_create(&Handles[i], NULL, SmpWorker, &Threads[i]);
    }
    for (i = 0; i < SMP_THREADS; i++)
        pthread_join(Handles[i], NULL);

    __atomic_store_n(&Shared.Stop, 1, __ATOMIC_SEQ_CST);
    pthread_join(Lazy, NULL);

    for (i = 0; i < SMP_FILES; i++)
    {
        CHECK(CcDirtyFlush(&Shared.Map[i], 0, (ULONG64)-1, 100000000, NULL) == STATUS_SUCCESS);
        CHECK(memcmp(Shared.File[i].Disk, Shared.Shadow[i], SMP_FILE_SIZE) == 0);
        CHECK(CcMapUninitialize(&Shared.Map[i]));
        CHECK(Shared.File[i].MappedViews == 0 && Shared.File[i].Errors == 0);
    }

    CHECK(Shared.Cache.TotalDirtyPages == 0);
    CHECK(CcCacheCheck(&Shared.Cache) == 0);
    printf("  smp: %u threads x %u ops, %.3fs, %lld hits, %lld misses, %lld reclaims\n", (unsigned)SMP_THREADS,
           (unsigned)SMP_OPERATIONS, NowSeconds() - Start, Shared.Cache.ViewHits, Shared.Cache.ViewMisses,
           Shared.Cache.ViewReclaims);

    for (i = 0; i < SMP_FILES; i++)
    {
        FileDestroy(&Shared.File[i]);
        free(Shared.Shadow[i]);
    }
    CcCacheUninitialize(&Shared.Cache);
}

typedef struct _BENCH_THREAD
{
    PCC_MAP Map;
    ULONG Index;
    ULONG64 Bytes;
    volatile LONG *Go;
    volatile LONG *Stop;
} BENCH_THREAD;

static
void *
BenchWorker(void *Argument)
{
    BENCH_THREAD *Thread = Argument;
    static _Thread_local UCHAR Buffer[65536];
    MOVE_CONTEXT Move = { Buffer, 0 };
    ULONG64 Seed = Thread->Index + 1;
    ULONG64 Bytes = 0;

    CcHostProcessor = Thread->Index;
    while (!__atomic_load_n(Thread->Go, __ATOMIC_SEQ_CST))
        sched_yield();

    while (!__atomic_load_n(Thread->Stop, __ATOMIC_SEQ_CST))
    {
        Move.BaseOffset = (Rng(&Seed) % 200) * 65536;
        CcCopyRange(Thread->Map, Move.BaseOffset, 65536, FALSE, MoveToBuffer, &Move, NULL);
        Bytes += 65536;
    }

    Thread->Bytes = Bytes;
    return NULL;
}

void
TestBench(void)
{
    static const ULONG Counts[] = { 1, 2, 4, 8 };
    TEST_FILE File;
    CC_CACHE Cache;
    CC_MAP Map;
    ULONG c, i;

    FileCreate(&File, 16 * MB);
    CHECK(CcCacheInitialize(&Cache, 256, 100000) == STATUS_SUCCESS);
    CcMapInitialize(&Map, &Cache, &File.Ops, &File, File.Size, File.Size, File.Size);
    CHECK(CcPrefetchRange(&Map, 0, File.Size) == STATUS_SUCCESS);

    printf("  hot 64 KB cached reads, GB/s:");
    for (c = 0; c < RTL_NUMBER_OF(Counts); c++)
    {
        BENCH_THREAD Threads[8];
        pthread_t Handles[8];
        volatile LONG Go = 0, Stop = 0;
        ULONG64 Total = 0;
        double Start;

        for (i = 0; i < Counts[c]; i++)
        {
            Threads[i].Map = &Map;
            Threads[i].Index = i;
            Threads[i].Bytes = 0;
            Threads[i].Go = &Go;
            Threads[i].Stop = &Stop;
            pthread_create(&Handles[i], NULL, BenchWorker, &Threads[i]);
        }

        Start = NowSeconds();
        __atomic_store_n(&Go, 1, __ATOMIC_SEQ_CST);
        while (NowSeconds() - Start < 0.5)
            sched_yield();
        __atomic_store_n(&Stop, 1, __ATOMIC_SEQ_CST);

        for (i = 0; i < Counts[c]; i++)
        {
            pthread_join(Handles[i], NULL);
            Total += Threads[i].Bytes;
        }

        printf("  %ut %.1f", (unsigned)Counts[c], (double)Total / (NowSeconds() - Start) / 1e9);
    }
    printf("\n");

    CHECK(CcMapUninitialize(&Map));
    CcCacheUninitialize(&Cache);
    FileDestroy(&File);
}
