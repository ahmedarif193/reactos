/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/pool/host/t_heap_e2e.c
 * PURPOSE:     Pool heap end-to-end host-native tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "harness.h"

#ifdef TEST_LIGHT
#define E2E_OPERATIONS     60000
#define EXPANSION_COUNT    8192
#define SMP_OPERATIONS     30000
#else
#define E2E_OPERATIONS     400000
#define EXPANSION_COUNT    131072
#define SMP_OPERATIONS     300000
#endif

#define E2E_SLOTS          8192
#define SMP_THREADS        8
#define SMP_LOCAL_SLOTS    512
#define SMP_MAILBOXES      256

typedef struct _ENUM_STATE
{
    PVOID *Slots;
    ULONG SlotCount;
    ULONG Seen;
    ULONG Unknown;
    PPOOL_HEAP Heap;
} ENUM_STATE;

typedef struct _BLOCK_STAMP
{
    ULONG64 Size;
    ULONG64 Seed;
} BLOCK_STAMP;

static
SIZE_T
PickSize(ULONG64 *Seed)
{
    ULONG64 Roll = Rng(Seed) % 1000;

    if (Roll < 600)
        return sizeof(BLOCK_STAMP) + (SIZE_T)(Rng(Seed) % 256);
    if (Roll < 900)
        return sizeof(BLOCK_STAMP) + (SIZE_T)(Rng(Seed) % 4000);
    if (Roll < 990)
        return PAGE_SIZE + (SIZE_T)(Rng(Seed) % (16 * PAGE_SIZE));
    if (Roll < 998)
        return 64 * 1024 + (SIZE_T)(Rng(Seed) % (400 * 1024));
    return POOL_RANGE_MAX_BYTES + 1 + (SIZE_T)(Rng(Seed) % (2 * POOL_UNIT_SIZE));
}

static
VOID
Stamp(PVOID Block, SIZE_T Size, ULONG64 Seed)
{
    BLOCK_STAMP *Header = Block;
    PUCHAR Bytes = Block;
    SIZE_T i;

    Header->Size = Size;
    Header->Seed = Seed;

    for (i = sizeof(*Header); i < Size; i += 97)
        Bytes[i] = (UCHAR)(Seed + i);

    Bytes[Size - 1] = (UCHAR)(Seed ^ 0xA5);
    if (Size - 1 < sizeof(*Header))
        Header->Size = Size;
}

static
BOOLEAN
Verify(PVOID Block)
{
    BLOCK_STAMP *Header = Block;
    PUCHAR Bytes = Block;
    SIZE_T Size = (SIZE_T)Header->Size;
    SIZE_T i;

    for (i = sizeof(*Header); i + 1 < Size; i += 97)
    {
        if (Bytes[i] != (UCHAR)(Header->Seed + i))
            return FALSE;
    }

    return (Size <= sizeof(*Header) || Bytes[Size - 1] == (UCHAR)(Header->Seed ^ 0xA5));
}

static BOOLEAN EnumCheck(PVOID Context, PVOID Block, PPOOL_BLOCK_INFO Info);

void
TestHeap(void)
{
    static PVOID Slots[E2E_SLOTS];
    TEST_ARENA Arena;
    TEST_ARENA Second;
    POOL_VA_REGION Region;
    POOL_VA_REGION SecondRegion;
    POOL_HEAP_STATISTICS Stats;
    POOL_BLOCK_INFO Info;
    PPOOL_HEAP Heap = NULL;
    ULONG64 Seed = 0xC0FFEE;
    LONG64 Baseline;
    ULONG Operation;
    ULONG i;
    int Foreign;

    ArenaCreate(&Arena, 1024 * POOL_UNIT_SIZE);
    CHECK(PoolVaRegionInitialize(&Region, Arena.Base, Arena.Bytes, FALSE, &Arena.Backing) == STATUS_SUCCESS);
    CHECK(PoolHeapCreate(&Heap, &Region, 7, 4, 0xFEEDFACECAFEBEEFULL, POOL_HEAP_POISON_ON_FREE) == STATUS_SUCCESS);
    Baseline = Region.CommittedPages;

    CHECK(!PoolHeapContains(Heap, &Foreign));
    CHECK(!PoolHeapFree(Heap, &Foreign, NULL));
    CHECK(!PoolHeapQuery(Heap, &Foreign, &Info));
    CHECK(!PoolHeapFree(Heap, (PVOID)Region.Base, NULL));
    CHECK(!PoolHeapFree(Heap, (PVOID)(Region.End - 64), NULL));

    for (Operation = 0; Operation < E2E_OPERATIONS; Operation++)
    {
        ULONG Slot = (ULONG)(Rng(&Seed) % E2E_SLOTS);

        PoolHostProcessor = (ULONG)(Rng(&Seed) % 4);

        if (Slots[Slot] == NULL)
        {
            SIZE_T Size = PickSize(&Seed);
            ULONG Flags = (Rng(&Seed) & 7) == 0 ? POOL_ALLOC_ZERO : 0;

            Slots[Slot] = PoolHeapAllocate(Heap, Size, 0x30303030 + (Slot & 0xFF), Flags);
            CHECK(Slots[Slot] != NULL);
            if (Slots[Slot] == NULL)
                break;

            if (Flags & POOL_ALLOC_ZERO)
                CHECK(((PUCHAR)Slots[Slot])[0] == 0 && ((PUCHAR)Slots[Slot])[Size - 1] == 0);
            if (Size >= PAGE_SIZE)
                CHECK(((ULONG_PTR)Slots[Slot] & (PAGE_SIZE - 1)) == 0);

            Stamp(Slots[Slot], Size, Rng(&Seed));
        }
        else
        {
            SIZE_T Size = (SIZE_T)((BLOCK_STAMP *)Slots[Slot])->Size;

            CHECK(Verify(Slots[Slot]));
            CHECK(PoolHeapFree(Heap, Slots[Slot], &Info));
            CHECK(Info.Size >= Size && Info.Tag == 0x30303030 + (Slot & 0xFF));
            Slots[Slot] = NULL;
        }
    }

    {
        ENUM_STATE State = { Slots, E2E_SLOTS, 0, 0, Heap };
        ULONG Live = 0;

        for (i = 0; i < E2E_SLOTS; i++)
        {
            POOL_BLOCK_INFO Described;
            BOOLEAN Allocated;
            PVOID Base;
            SIZE_T Size;

            if (Slots[i] == NULL)
                continue;

            Live++;
            Size = (SIZE_T)((BLOCK_STAMP *)Slots[i])->Size;
            CHECK(PoolHeapDescribe(Heap, (PUCHAR)Slots[i] + Size - 1, &Base, &Described, &Allocated));
            CHECK(Base == Slots[i] && Allocated && Described.Tag == 0x30303030 + (i & 0xFF));
            CHECK(PoolHeapDescribe(Heap, (PUCHAR)Slots[i] + Size / 2, &Base, &Described, &Allocated) &&
                  Base == Slots[i]);
        }

        PoolHeapEnumerate(Heap, EnumCheck, &State);
        CHECK(State.Seen == Live);
        CHECK(State.Unknown == 0);
        CHECK(Live > 100);
    }

    for (i = 0; i < E2E_SLOTS; i++)
    {
        if (Slots[i] != NULL)
        {
            CHECK(Verify(Slots[i]));
            CHECK(PoolHeapFree(Heap, Slots[i], NULL));
            Slots[i] = NULL;
        }
    }

    PoolHeapTrim(Heap);
    PoolHeapQueryStatistics(Heap, &Stats);
    CHECK(Stats.Allocations == Stats.Frees);
    CHECK(Stats.BytesInUse == 0);
    CHECK(Stats.Failures == 0);
    CHECK(Stats.LargeCount == 0);
    CHECK(Stats.SegmentCount <= Heap->ShardCount);
    CHECK(Stats.LfhSubsegmentCount == 0 && Stats.VsSubsegmentCount == 0);
    CHECK(Stats.CommittedPages - Baseline <= 2 * (LONG64)Heap->ShardCount);
    CHECK(Region.CommittedPages == Arena.CommittedPages);
    printf("  heap: %llu allocations, peak %lld pages, resting %lld pages over baseline, %u segments\n",
           Stats.Allocations, Stats.PeakCommittedPages, Stats.CommittedPages - Baseline, Stats.SegmentCount);

    ArenaCreate(&Second, 16 * POOL_UNIT_SIZE);
    CHECK(PoolVaRegionInitialize(&SecondRegion, Second.Base, Second.Bytes, FALSE, &Second.Backing) == STATUS_SUCCESS);
    CHECK(PoolHeapAddRegion(Heap, &SecondRegion) == STATUS_SUCCESS);

    {
        static PVOID Fill[1100];
        BOOLEAN Spilled = FALSE;
        ULONG Count = 0;

        while (Count < RTL_NUMBER_OF(Fill))
        {
            Fill[Count] = PoolHeapAllocate(Heap, POOL_UNIT_SIZE - PAGE_SIZE, 'lliF', 0);
            if (Fill[Count] == NULL)
                break;

            Count++;
            if (PoolVaContains(&SecondRegion, Fill[Count - 1]))
            {
                Spilled = TRUE;
                break;
            }
        }

        CHECK(Spilled);
        CHECK(Region.UnitsInUse == Region.UnitCount);
        CHECK(PoolHeapQuery(Heap, Fill[Count - 1], &Info) && Info.Tag == 'lliF');

        Slots[0] = PoolHeapAllocate(Heap, 100, 'llmS', 0);
        CHECK(Slots[0] != NULL);
        CHECK(PoolHeapFree(Heap, Slots[0], NULL));

        while (Count != 0)
        {
            Count--;
            CHECK(PoolHeapFree(Heap, Fill[Count], NULL));
        }
    }

    CHECK(Arena.Errors == 0 && Second.Errors == 0);
    ArenaDestroy(&Second);
    ArenaDestroy(&Arena);
}

static
BOOLEAN
EnumCheck(PVOID Context, PVOID Block, PPOOL_BLOCK_INFO Info)
{
    ENUM_STATE *State = Context;
    ULONG i;

    for (i = 0; i < State->SlotCount; i++)
    {
        if (State->Slots[i] == Block)
        {
            if (Info->Tag == 0x30303030 + (i & 0xFF) && Info->Size >= ((BLOCK_STAMP *)Block)->Size)
                State->Seen++;
            else
                State->Unknown++;

            return TRUE;
        }
    }

    State->Unknown++;
    return TRUE;
}

void
TestExpansion(void)
{
    TEST_ARENA Arena;
    POOL_VA_REGION Region;
    POOL_HEAP_STATISTICS Stats;
    PPOOL_HEAP Heap = NULL;
    PVOID *Blocks = calloc(EXPANSION_COUNT, sizeof(PVOID));
    LONG64 Baseline;
    double Start;
    double Allocated;
    double Freed;
    ULONG i;

    ArenaCreate(&Arena, ((SIZE_T)EXPANSION_COUNT * PAGE_SIZE / POOL_UNIT_SIZE + 64) * POOL_UNIT_SIZE * 11 / 10);
    CHECK(PoolVaRegionInitialize(&Region, Arena.Base, Arena.Bytes, FALSE, &Arena.Backing) == STATUS_SUCCESS);
    CHECK(PoolHeapCreate(&Heap, &Region, 0, 4, 1, 0) == STATUS_SUCCESS);
    Baseline = Region.CommittedPages;

    Start = NowSeconds();
    for (i = 0; i < EXPANSION_COUNT; i++)
    {
        Blocks[i] = PoolHeapAllocate(Heap, PAGE_SIZE, 'apxE', 0);
        if (Blocks[i] == NULL)
            break;
        *(PULONG)Blocks[i] = i;
    }
    CHECK(i == EXPANSION_COUNT);
    Allocated = NowSeconds();

    for (i = 0; i < EXPANSION_COUNT; i++)
    {
        if (Blocks[i] == NULL)
            break;
        CHECK(*(PULONG)Blocks[i] == i);
        CHECK(PoolHeapFree(Heap, Blocks[i], NULL));
    }
    Freed = NowSeconds();

    PoolHeapTrim(Heap);
    PoolHeapQueryStatistics(Heap, &Stats);
    CHECK(Stats.BytesInUse == 0);
    CHECK(Stats.SegmentCount == 1);
    CHECK(Stats.CommittedPages - Baseline <= 4);
    CHECK(Allocated - Start < 20.0 && Freed - Allocated < 20.0);
    printf("  expansion: %u page allocations in %.3fs, freed in %.3fs\n",
           (unsigned)EXPANSION_COUNT, Allocated - Start, Freed - Allocated);

    CHECK(Arena.Errors == 0);
    free(Blocks);
    ArenaDestroy(&Arena);
}

typedef struct _SMP_CONTEXT
{
    PPOOL_HEAP Heap;
    PVOID volatile *Mailbox;
    ULONG Index;
    volatile LONG *Stop;
} SMP_CONTEXT;

static
void
SmpRelease(SMP_CONTEXT *Context, PVOID Block)
{
    POOL_BLOCK_INFO Info;
    SIZE_T Size = (SIZE_T)((BLOCK_STAMP *)Block)->Size;

    CHECK(Verify(Block));
    CHECK(PoolHeapFree(Context->Heap, Block, &Info));
    CHECK(Info.Size >= Size);
}

static
void *
SmpWorker(void *Argument)
{
    SMP_CONTEXT *Context = Argument;
    PVOID Local[SMP_LOCAL_SLOTS] = { 0 };
    ULONG64 Seed = 0x9E3779B97F4A7C15ULL * (Context->Index + 1);
    ULONG Operation;
    ULONG i;

    PoolHostProcessor = Context->Index;

    for (Operation = 0; Operation < SMP_OPERATIONS; Operation++)
    {
        ULONG Slot = (ULONG)(Rng(&Seed) % SMP_LOCAL_SLOTS);

        if (Local[Slot] == NULL)
        {
            SIZE_T Size = PickSize(&Seed);

            if (Size > 64 * 1024)
                Size = sizeof(BLOCK_STAMP) + (Size % 1024);

            Local[Slot] = PoolHeapAllocate(Context->Heap, Size, 'pmS0' + Context->Index, 0);
            CHECK(Local[Slot] != NULL);
            if (Local[Slot] != NULL)
                Stamp(Local[Slot], Size, Rng(&Seed));
        }
        else if ((Rng(&Seed) & 3) == 0)
        {
            ULONG Box = (ULONG)(Rng(&Seed) % SMP_MAILBOXES);
            PVOID Previous = __atomic_exchange_n(&Context->Mailbox[Box], Local[Slot], __ATOMIC_ACQ_REL);

            Local[Slot] = NULL;
            if (Previous != NULL)
                SmpRelease(Context, Previous);
        }
        else
        {
            SmpRelease(Context, Local[Slot]);
            Local[Slot] = NULL;
        }
    }

    for (i = 0; i < SMP_LOCAL_SLOTS; i++)
    {
        if (Local[i] != NULL)
            SmpRelease(Context, Local[i]);
    }

    return NULL;
}

static
void *
SmpTrimmer(void *Argument)
{
    SMP_CONTEXT *Context = Argument;

    PoolHostProcessor = Context->Index;

    while (!__atomic_load_n(Context->Stop, __ATOMIC_SEQ_CST))
    {
        PoolHeapTrim(Context->Heap);
        sched_yield();
    }

    return NULL;
}

void
TestSmp(void)
{
    static PVOID volatile Mailbox[SMP_MAILBOXES];
    TEST_ARENA Arena;
    POOL_VA_REGION Region;
    POOL_HEAP_STATISTICS Stats;
    SMP_CONTEXT Contexts[SMP_THREADS + 1];
    void *Args[SMP_THREADS];
    pthread_t Trimmer;
    PPOOL_HEAP Heap = NULL;
    volatile LONG Stop = 0;
    double Start;
    ULONG i;

    ArenaCreate(&Arena, 1024 * POOL_UNIT_SIZE);
    CHECK(PoolVaRegionInitialize(&Region, Arena.Base, Arena.Bytes, FALSE, &Arena.Backing) == STATUS_SUCCESS);
    CHECK(PoolHeapCreate(&Heap, &Region, 0, SMP_THREADS, 42, POOL_HEAP_POISON_ON_FREE) == STATUS_SUCCESS);

    for (i = 0; i <= SMP_THREADS; i++)
    {
        Contexts[i].Heap = Heap;
        Contexts[i].Mailbox = Mailbox;
        Contexts[i].Index = i;
        Contexts[i].Stop = &Stop;
        if (i < SMP_THREADS)
            Args[i] = &Contexts[i];
    }

    Start = NowSeconds();
    pthread_create(&Trimmer, NULL, SmpTrimmer, &Contexts[SMP_THREADS]);
    RunThreads(SMP_THREADS, SmpWorker, Args);
    __atomic_store_n(&Stop, 1, __ATOMIC_SEQ_CST);
    pthread_join(Trimmer, NULL);

    for (i = 0; i < SMP_MAILBOXES; i++)
    {
        if (Mailbox[i] != NULL)
        {
            SmpRelease(&Contexts[0], Mailbox[i]);
            Mailbox[i] = NULL;
        }
    }

    PoolHeapTrim(Heap);
    PoolHeapQueryStatistics(Heap, &Stats);
    CHECK(Stats.Allocations == Stats.Frees);
    CHECK(Stats.BytesInUse == 0);
    CHECK(Stats.Failures == 0);
    CHECK(Region.CommittedPages == Arena.CommittedPages);
    CHECK(Arena.Errors == 0);
    printf("  smp: %u threads, %llu allocations, %.3fs, peak %lld pages\n", (unsigned)SMP_THREADS,
           Stats.Allocations, NowSeconds() - Start, Stats.PeakCommittedPages);

    ArenaDestroy(&Arena);
}
