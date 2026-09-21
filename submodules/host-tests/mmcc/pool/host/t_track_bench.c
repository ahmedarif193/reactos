/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/pool/host/t_track_bench.c
 * PURPOSE:     Pool allocation tracking host-native benchmarks
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "harness.h"

#define TRACK_THREADS   8
#define TRACK_ROUNDS    50000
#define TRACK_TAGS      300

static POOL_TAG_TRACKER Tracker;

static
void *
TrackWorker(void *Argument)
{
    ULONG Index = (ULONG)(ULONG_PTR)Argument;
    ULONG Round;

    PoolHostProcessor = Index;

    for (Round = 0; Round < TRACK_ROUNDS; Round++)
    {
        ULONG Tag = 0x54000000 + (Round % TRACK_TAGS);

        PoolTagCharge(&Tracker, Tag, Round & 1, 64);
        PoolHostProcessor = (Index + Round) % TRACK_THREADS;
        PoolTagRelease(&Tracker, Tag, Round & 1, 64);
        PoolHostProcessor = Index;
        PoolTagCharge(&Tracker, 'peeK', 0, 10);
    }

    return NULL;
}

void
TestTrack(void)
{
    static POOL_TAG_USAGE Usage[POOL_TAG_TABLE_ENTRIES];
    POOL_TAG_TABLE *Tables = aligned_alloc(64, sizeof(POOL_TAG_TABLE) * 4);
    POOL_TAG_USAGE One;
    void *Args[TRACK_THREADS];
    ULONG Count;
    ULONG i;

    PoolTagTrackerInitialize(&Tracker);
    PoolTagCharge(&Tracker, 'enoN', 0, 1);
    for (i = 0; i < 4; i++)
        CHECK(PoolTagTrackerAddTable(&Tracker, &Tables[i]));

    for (i = 0; i < TRACK_THREADS; i++)
        Args[i] = (void *)(ULONG_PTR)i;
    RunThreads(TRACK_THREADS, TrackWorker, Args);

    CHECK(PoolTagQuery(&Tracker, 'peeK', &One));
    CHECK(One.Allocations[0] == (ULONG64)TRACK_THREADS * TRACK_ROUNDS);
    CHECK(One.Bytes[0] == (LONG64)TRACK_THREADS * TRACK_ROUNDS * 10);
    CHECK(!PoolTagQuery(&Tracker, 'enoN', &One));

    Count = PoolTagSnapshot(&Tracker, Usage, POOL_TAG_TABLE_ENTRIES);
    CHECK(Count == TRACK_TAGS + 1);
    for (i = 0; i < Count; i++)
    {
        if (Usage[i].Tag == 'peeK' || Usage[i].Tag == POOL_TAG_OVERFLOW)
            continue;

        CHECK(Usage[i].Allocations[0] + Usage[i].Allocations[1] == Usage[i].Frees[0] + Usage[i].Frees[1]);
        CHECK(Usage[i].Bytes[0] == 0 && Usage[i].Bytes[1] == 0);
    }

    PoolTagTrackerInitialize(&Tracker);
    CHECK(PoolTagTrackerAddTable(&Tracker, &Tables[0]));
    for (i = 1; i <= POOL_TAG_TABLE_ENTRIES + 100; i++)
        PoolTagCharge(&Tracker, 0x10000000 + i, 0, 1);
    CHECK(PoolTagQuery(&Tracker, POOL_TAG_OVERFLOW, &One));
    CHECK(One.Allocations[0] == 101);

    free(Tables);
}

#define BENCH_BATCH    64
#define BENCH_SECONDS  0.5

typedef struct _BENCH_CONTEXT
{
    PPOOL_HEAP Heap;
    SIZE_T Size;
    ULONG Index;
    ULONG64 Operations;
    volatile LONG *Go;
    volatile LONG *Stop;
} BENCH_CONTEXT;

static
void *
BenchWorker(void *Argument)
{
    BENCH_CONTEXT *Context = Argument;
    PVOID Batch[BENCH_BATCH];
    ULONG64 Seed = Context->Index + 1;
    ULONG64 Operations = 0;
    ULONG i;

    PoolHostProcessor = Context->Index;

    while (!*Context->Go)
        sched_yield();

    while (!*Context->Stop)
    {
        for (i = 0; i < BENCH_BATCH; i++)
        {
            SIZE_T Size = Context->Size ? Context->Size : 16 + (SIZE_T)(Rng(&Seed) % 2000);

            Batch[i] = PoolHeapAllocate(Context->Heap, Size, 'hcnB', 0);
        }

        for (i = 0; i < BENCH_BATCH; i++)
        {
            if (Batch[i] != NULL)
                PoolHeapFree(Context->Heap, Batch[i], NULL);
        }

        Operations += BENCH_BATCH;
    }

    Context->Operations = Operations;
    return NULL;
}

void
TestBench(void)
{
    static const SIZE_T Sizes[] = { 32, 128, 2048, 4096, 65536, 0 };
    static const ULONG Threads[] = { 1, 2, 4, 8 };
    TEST_ARENA Arena;
    POOL_VA_REGION Region;
    PPOOL_HEAP Heap = NULL;
    ULONG s, t, i;

    ArenaCreate(&Arena, 2048 * POOL_UNIT_SIZE);
    CHECK(PoolVaRegionInitialize(&Region, Arena.Base, Arena.Bytes, FALSE, &Arena.Backing) == STATUS_SUCCESS);
    CHECK(PoolHeapCreate(&Heap, &Region, 0, 8, 1, 0) == STATUS_SUCCESS);

    printf("  %-8s %10s %10s %10s %10s   (alloc+free pairs per second, batch %u)\n",
           "size", "1t", "2t", "4t", "8t", (unsigned)BENCH_BATCH);

    for (s = 0; s < RTL_NUMBER_OF(Sizes); s++)
    {
        char Label[16];

        if (Sizes[s])
            snprintf(Label, sizeof(Label), "%zu", Sizes[s]);
        else
            snprintf(Label, sizeof(Label), "mixed");

        printf("  %-8s", Label);

        for (t = 0; t < RTL_NUMBER_OF(Threads); t++)
        {
            BENCH_CONTEXT Contexts[8];
            pthread_t Handles[8];
            volatile LONG Go = 0, Stop = 0;
            ULONG64 Total = 0;
            double Start, Elapsed;

            for (i = 0; i < Threads[t]; i++)
            {
                Contexts[i].Heap = Heap;
                Contexts[i].Size = Sizes[s];
                Contexts[i].Index = i;
                Contexts[i].Operations = 0;
                Contexts[i].Go = &Go;
                Contexts[i].Stop = &Stop;
                pthread_create(&Handles[i], NULL, BenchWorker, &Contexts[i]);
            }

            Start = NowSeconds();
            Go = 1;
            while (NowSeconds() - Start < BENCH_SECONDS)
                sched_yield();
            Stop = 1;

            for (i = 0; i < Threads[t]; i++)
            {
                pthread_join(Handles[i], NULL);
                Total += Contexts[i].Operations;
            }

            Elapsed = NowSeconds() - Start;
            printf(" %9.2fM", (double)Total / Elapsed / 1e6);
        }

        printf("\n");
    }

    CHECK(Arena.Errors == 0);
    ArenaDestroy(&Arena);
}
