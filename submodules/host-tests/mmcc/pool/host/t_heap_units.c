/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/pool/host/t_heap_units.c
 * PURPOSE:     Pool heap host-native unit tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "harness.h"

static
PPOOL_HEAP
MakeHeap(TEST_ARENA *Arena, POOL_VA_REGION *Region, SIZE_T Bytes, ULONG Cpus)
{
    PPOOL_HEAP Heap = NULL;

    ArenaCreate(Arena, Bytes);
    CHECK(PoolVaRegionInitialize(Region, Arena->Base, Arena->Bytes, FALSE, &Arena->Backing) == STATUS_SUCCESS);
    CHECK(PoolHeapCreate(&Heap, Region, 0, Cpus, 0x1234567890ABCDEFULL, POOL_HEAP_POISON_ON_FREE) == STATUS_SUCCESS);
    return Heap;
}

void
TestSegment(void)
{
    TEST_ARENA Arena;
    POOL_VA_REGION Region;
    POOL_HEAP_STATISTICS Stats;
    PPOOL_HEAP Heap = MakeHeap(&Arena, &Region, 64 * POOL_UNIT_SIZE, 2);
    LONG64 Baseline = Region.CommittedPages;
    PVOID Pages[600];
    PVOID Block;
    ULONG i;

    Block = PoolSegAllocatePages(Heap, 3, PoolRangeBlock, 0, 'tseT');
    CHECK(Block != NULL && ((ULONG_PTR)Block & (PAGE_SIZE - 1)) == 0);
    CHECK(PoolVaUnit(&Region, Block)->State == PoolUnitSegment);
    CHECK(PoolSegFromAddress(Block)->Signature == POOL_SEGMENT_SIGNATURE);
    CHECK(PoolSegFromAddress(Block)->Range[PoolSegPageIndex(Block)].Pages == 3);
    CHECK(PoolSegFromAddress(Block)->Range[PoolSegPageIndex(Block) + 2].First == PoolSegPageIndex(Block));
    memset(Block, 0x5A, 3 * PAGE_SIZE);
    CHECK(Region.CommittedPages == Baseline + PoolSegFromAddress(Block)->HeaderPages + 3);
    PoolSegFreePages(Heap, Block);
    CHECK(Heap->Shard[0].FreeCommittedPages == 3);

    CHECK(PoolSegAllocatePages(Heap, 0, PoolRangeBlock, 0, 0) == NULL);
    CHECK(PoolSegAllocatePages(Heap, POOL_SEGMENT_PAGES, PoolRangeBlock, 0, 0) == NULL);

    for (i = 0; i < 600; i++)
    {
        Pages[i] = PoolSegAllocatePages(Heap, 1, PoolRangeBlock, 0, i);
        CHECK(Pages[i] != NULL);
        memset(Pages[i], (int)i, PAGE_SIZE);
    }

    PoolHeapQueryStatistics(Heap, &Stats);
    CHECK(Stats.SegmentCount >= 600 / POOL_SEGMENT_PAGES);

    for (i = 0; i < 600; i++)
    {
        CHECK(*(PUCHAR)Pages[i] == (UCHAR)i);
        PoolSegFreePages(Heap, Pages[i]);
    }

    PoolHeapQueryStatistics(Heap, &Stats);
    CHECK(Stats.SegmentCount == 1);
    CHECK(Heap->Shard[0].FreeCommittedPages <= POOL_SHARD_COMMIT_CACHE);

    PoolHeapTrim(Heap);
    CHECK(Heap->Shard[0].FreeCommittedPages == 0);
    PoolHeapQueryStatistics(Heap, &Stats);
    CHECK(Stats.SegmentCount == 1 && Stats.CommittedPages <= Baseline + 2);

    ArenaFailAfter(&Arena, 0);
    CHECK(PoolSegAllocatePages(Heap, 2, PoolRangeBlock, 0, 0) == NULL);
    ArenaFailAfter(&Arena, -1);
    Block = PoolSegAllocatePages(Heap, 2, PoolRangeBlock, 0, 0);
    CHECK(Block != NULL);
    PoolSegFreePages(Heap, Block);
    PoolHeapTrim(Heap);

    CHECK(Region.CommittedPages == Arena.CommittedPages);
    CHECK(Arena.Errors == 0);
    ArenaDestroy(&Arena);
}

void
TestLfh(void)
{
    TEST_ARENA Arena;
    POOL_VA_REGION Region;
    POOL_BLOCK_INFO Info;
    PPOOL_HEAP Heap = MakeHeap(&Arena, &Region, 64 * POOL_UNIT_SIZE, 4);
    PVOID Blocks[4096];
    SIZE_T Size;
    ULONG Class;
    ULONG i;

    for (Size = 1; Size <= POOL_LFH_MAX_BYTES; Size++)
    {
        Class = PoolLfhClassFromSize(Size);
        CHECK(Class < POOL_LFH_CLASS_COUNT);
        CHECK(PoolLfhClassSize(Class) >= Size);
        CHECK(Class == 0 || PoolLfhClassSize(Class - 1) < Size);
    }
    CHECK(PoolLfhClassFromSize(POOL_LFH_MAX_BYTES + 1) == POOL_LFH_CLASS_COUNT);

    for (Class = 0; Class < POOL_LFH_CLASS_COUNT; Class++)
    {
        USHORT BlockSize = PoolLfhClassSize(Class);
        ULONG Lead = (BlockSize % POOL_CACHE_ALIGNMENT == 0) ? POOL_CACHE_ALIGNMENT : POOL_ALIGNMENT;

        if (BlockSize + Lead > PAGE_SIZE)
        {
            CHECK(PoolLfhAllocate(Heap, Class, 0x41000000 | Class) == NULL);
            continue;
        }

        for (i = 0; i < 300; i++)
        {
            Blocks[i] = PoolLfhAllocate(Heap, Class, 0x41000000 | Class);
            CHECK(Blocks[i] != NULL);
            CHECK(((ULONG_PTR)Blocks[i] & (PAGE_SIZE - 1)) != 0);
            CHECK(((ULONG_PTR)Blocks[i] >> PAGE_SHIFT) == (((ULONG_PTR)Blocks[i] + BlockSize - 1) >> PAGE_SHIFT));
            CHECK(((ULONG_PTR)Blocks[i] % POOL_ALIGNMENT) == 0);
            if ((BlockSize % POOL_CACHE_ALIGNMENT) == 0)
                CHECK(((ULONG_PTR)Blocks[i] % POOL_CACHE_ALIGNMENT) == 0);
            memset(Blocks[i], (int)(i ^ Class), BlockSize);
        }

        for (i = 0; i < 300; i++)
        {
            PUCHAR Bytes = Blocks[i];

            CHECK(Bytes[0] == (UCHAR)(i ^ Class) && Bytes[BlockSize - 1] == (UCHAR)(i ^ Class));
            CHECK(PoolHeapQuery(Heap, Blocks[i], &Info));
            CHECK(Info.Size == BlockSize && Info.Tag == (0x41000000 | Class) && Info.Kind == PoolBlockLfh);
        }

        CHECK(PoolHeapSetFlags(Heap, Blocks[7], 0x5));
        *(ULONG_PTR *)((PUCHAR)Blocks[7] + BlockSize - sizeof(ULONG_PTR)) = 0xABCD00 + Class;
        CHECK(PoolHeapQuery(Heap, Blocks[7], &Info) && Info.Flags == 0x5 && Info.Trailer == 0xABCD00 + Class);
        CHECK(!PoolHeapQuery(Heap, (PUCHAR)Blocks[7] + 1, &Info));

        for (i = 0; i < 300; i += 2)
            CHECK(PoolHeapFree(Heap, Blocks[i], &Info));
        CHECK(!PoolHeapFree(Heap, Blocks[0], &Info));
        for (i = 1; i < 300; i += 2)
            CHECK(PoolHeapFree(Heap, Blocks[i], &Info));
    }

    for (i = 0; i < 4096; i++)
    {
        Blocks[i] = PoolLfhAllocate(Heap, 1, 'kluB');
        CHECK(Blocks[i] != NULL);
    }
    for (i = 0; i < 4096; i++)
    {
        CHECK(PoolHeapFree(Heap, Blocks[i], NULL));
        Blocks[i] = NULL;
    }

    {
        POOL_HEAP_STATISTICS Stats;

        PoolHeapQueryStatistics(Heap, &Stats);
        CHECK(Stats.LfhSubsegmentCount <= POOL_LFH_CLASS_COUNT);
    }

    {
        SIZE_T Size;
        ULONG Round;

        for (Round = 0; Round < 40; Round++)
        {
            for (Size = 1; Size < PAGE_SIZE + 64 && Size < 4200; Size += (Size < 256) ? 7 : 61)
            {
                ULONG Flags = (Round & 1) ? POOL_ALLOC_CACHE_ALIGNED : 0;
                PUCHAR Block = PoolHeapAllocate(Heap, Size, 'tnoC', Flags);
                ULONG Slot = (ULONG)((Size * 31 + Round) % 4096);

                CHECK(Block != NULL);
                if (Block == NULL)
                    continue;

                if (((ULONG_PTR)Block & (PAGE_SIZE - 1)) != 0)
                    CHECK(((ULONG_PTR)Block >> PAGE_SHIFT) == (((ULONG_PTR)Block + Size - 1) >> PAGE_SHIFT));
                else
                {
                    SIZE_T Rounded = (Flags != 0) ? ((Size + POOL_CACHE_ALIGNMENT - 1) & ~(SIZE_T)(POOL_CACHE_ALIGNMENT - 1))
                                                  : Size;
                    ULONG SizeClass = PoolLfhClassFromSize(Rounded);
                    SIZE_T Lead = (Flags != 0) ? POOL_CACHE_ALIGNMENT : POOL_ALIGNMENT;

                    CHECK(Rounded >= PAGE_SIZE || SizeClass == POOL_LFH_CLASS_COUNT ||
                          (PoolLfhClassSize(SizeClass) + Lead > PAGE_SIZE && (Flags != 0 || Size > PoolVsMaxBytes())));
                }

                if (Flags != 0)
                    CHECK(((ULONG_PTR)Block % POOL_CACHE_ALIGNMENT) == 0);

                memset(Block, 0x5A, Size);

                if (Blocks[Slot] != NULL)
                    CHECK(PoolHeapFree(Heap, Blocks[Slot], NULL));
                Blocks[Slot] = Block;
            }
        }

        for (i = 0; i < 4096; i++)
        {
            if (Blocks[i] != NULL)
                CHECK(PoolHeapFree(Heap, Blocks[i], NULL));
            Blocks[i] = NULL;
        }
    }

    Blocks[0] = PoolHeapAllocate(Heap, 100, 'ngiA', POOL_ALLOC_CACHE_ALIGNED | POOL_ALLOC_ZERO);
    CHECK(Blocks[0] != NULL && ((ULONG_PTR)Blocks[0] % POOL_CACHE_ALIGNMENT) == 0);
    for (i = 0; i < 100; i++)
        CHECK(((PUCHAR)Blocks[0])[i] == 0);
    CHECK(PoolHeapFree(Heap, Blocks[0], NULL));

    CHECK(Arena.Errors == 0);
    ArenaDestroy(&Arena);
}

void
TestVs(void)
{
    TEST_ARENA Arena;
    POOL_VA_REGION Region;
    POOL_BLOCK_INFO Info;
    POOL_HEAP_STATISTICS Stats;
    PPOOL_HEAP Heap = MakeHeap(&Arena, &Region, 64 * POOL_UNIT_SIZE, 1);
    PVOID Blocks[2000];
    SIZE_T Sizes[2000];
    SIZE_T Actual;
    ULONG64 Seed = 99;
    ULONG i;
    ULONG Round;

    for (Round = 0; Round < 4; Round++)
    {
        for (i = 0; i < 2000; i++)
        {
            Sizes[i] = 1 + (SIZE_T)(Rng(&Seed) % 3000);
            Blocks[i] = PoolVsAllocate(Heap, Sizes[i], (ULONG)i + 1, &Actual);
            CHECK(Blocks[i] != NULL && Actual >= Sizes[i]);
            CHECK(((ULONG_PTR)Blocks[i] % POOL_ALIGNMENT) == 0);
            memset(Blocks[i], (int)i, Sizes[i]);
        }

        for (i = 0; i < 2000; i++)
        {
            CHECK(PoolHeapQuery(Heap, Blocks[i], &Info));
            CHECK(Info.Kind == PoolBlockVs && Info.Tag == i + 1 && Info.Size >= Sizes[i]);
            CHECK(((PUCHAR)Blocks[i])[0] == (UCHAR)i && ((PUCHAR)Blocks[i])[Sizes[i] - 1] == (UCHAR)i);
        }

        for (i = 0; i < 2000; i += 3)
            CHECK(PoolHeapFree(Heap, Blocks[i], NULL));
        for (i = 1; i < 2000; i += 3)
            CHECK(PoolHeapFree(Heap, Blocks[i], NULL));
        CHECK(!PoolHeapFree(Heap, Blocks[0], NULL));
        for (i = 2; i < 2000; i += 3)
            CHECK(PoolHeapFree(Heap, Blocks[i], NULL));
    }

    PoolHeapQueryStatistics(Heap, &Stats);
    CHECK(Stats.VsSubsegmentCount == 1);

    Blocks[0] = PoolVsAllocate(Heap, 64, 'rroC', &Actual);
    CHECK(Blocks[0] != NULL);
    ((PPOOL_VS_CHUNK)Blocks[0] - 1)->Encoded ^= 0x10;
    CHECK(!PoolHeapFree(Heap, Blocks[0], NULL));
    ((PPOOL_VS_CHUNK)Blocks[0] - 1)->Encoded ^= 0x10;
    CHECK(PoolHeapFree(Heap, Blocks[0], NULL));

    CHECK(PoolVsAllocate(Heap, PoolVsMaxBytes() + 1, 0, &Actual) == NULL);
    Blocks[0] = PoolVsAllocate(Heap, PoolVsMaxBytes(), 'xaMV', &Actual);
    CHECK(Blocks[0] != NULL);
    memset(Blocks[0], 1, PoolVsMaxBytes());
    CHECK(PoolHeapFree(Heap, Blocks[0], NULL));

    CHECK(Arena.Errors == 0);
    ArenaDestroy(&Arena);
}

void
TestLarge(void)
{
    TEST_ARENA Arena;
    POOL_VA_REGION Region;
    POOL_BLOCK_INFO Info;
    POOL_HEAP_STATISTICS Stats;
    PPOOL_HEAP Heap = MakeHeap(&Arena, &Region, 64 * POOL_UNIT_SIZE, 2);
    SIZE_T UnitsBefore = Region.UnitsInUse;
    LONG64 PagesBefore = Region.CommittedPages;
    PVOID Block;
    PVOID Second;

    Block = PoolHeapAllocate(Heap, 3 * POOL_UNIT_SIZE + 5, 'graL', 0);
    CHECK(Block != NULL && ((ULONG_PTR)Block & POOL_UNIT_MASK) == 0);
    memset(Block, 0x77, 3 * POOL_UNIT_SIZE + 5);
    CHECK(PoolHeapQuery(Heap, Block, &Info));
    CHECK(Info.Kind == PoolBlockLarge && Info.Tag == 'graL' && Info.Size >= 3 * POOL_UNIT_SIZE + 5);
    CHECK(!PoolHeapQuery(Heap, (PUCHAR)Block + PAGE_SIZE, &Info));
    CHECK(!PoolHeapQuery(Heap, (PUCHAR)Block + POOL_UNIT_SIZE, &Info));
    CHECK(Region.UnitsInUse == UnitsBefore + 4);

    Second = PoolHeapAllocate(Heap, POOL_RANGE_MAX_BYTES + 1, '2raL', POOL_ALLOC_ZERO);
    CHECK(Second != NULL && *(PUCHAR)Second == 0);
    PoolHeapQueryStatistics(Heap, &Stats);
    CHECK(Stats.LargeCount == 2);

    CHECK(PoolHeapSetFlags(Heap, Block, 9));
    CHECK(PoolHeapQuery(Heap, Block, &Info));
    *(ULONG_PTR *)((PUCHAR)Block + Info.Size - sizeof(ULONG_PTR)) = 0x1234;
    CHECK(PoolHeapFree(Heap, Block, &Info) && Info.Flags == 9 && Info.Trailer == 0x1234);
    CHECK(!PoolHeapFree(Heap, Block, &Info));
    CHECK(PoolHeapFree(Heap, Second, NULL));
    CHECK(Region.UnitsInUse == UnitsBefore);
    CHECK(Region.CommittedPages == PagesBefore);

    CHECK(PoolHeapAllocate(Heap, 128 * POOL_UNIT_SIZE, 'giB!', 0) == NULL);
    CHECK(PoolHeapAllocate(Heap, (SIZE_T)-1, 'giB!', 0) == NULL);
    PoolHeapQueryStatistics(Heap, &Stats);
    CHECK(Stats.Failures == 2);

    Block = PoolHeapAllocate(Heap, PAGE_SIZE, 'egaP', 0);
    CHECK(Block != NULL && ((ULONG_PTR)Block & (PAGE_SIZE - 1)) == 0);
    CHECK(PoolHeapQuery(Heap, Block, &Info) && Info.Kind == PoolBlockRange && Info.Size == PAGE_SIZE);
    CHECK(PoolHeapFree(Heap, Block, NULL));
    Block = PoolHeapAllocate(Heap, POOL_RANGE_MAX_BYTES, 'xaMR', 0);
    CHECK(Block != NULL);
    CHECK(PoolHeapQuery(Heap, Block, &Info) && Info.Kind == PoolBlockRange);
    CHECK(!PoolHeapFree(Heap, (PUCHAR)Block + PAGE_SIZE, NULL));
    CHECK(PoolHeapFree(Heap, Block, NULL));

    CHECK(Arena.Errors == 0);
    ArenaDestroy(&Arena);
}
