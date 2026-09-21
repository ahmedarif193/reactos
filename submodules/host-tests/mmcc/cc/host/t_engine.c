/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/cc/host/t_engine.c
 * PURPOSE:     Cache engine host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "ccharness.h"

#define MB (1024 * 1024)

static
void
ReadCheck(PCC_MAP Map, ULONG64 Offset, ULONG Length, const UCHAR *Expected)
{
    PUCHAR Buffer = malloc(Length);
    MOVE_CONTEXT Move = { Buffer, Offset };
    ULONG64 Moved = 0;
    ULONG i;
    int Bad = 0;

    CHECK(CcCopyRange(Map, Offset, Length, FALSE, MoveToBuffer, &Move, &Moved) == STATUS_SUCCESS);
    CHECK(Moved == Length);
    for (i = 0; i < Length; i++)
    {
        UCHAR Want = Expected ? Expected[i] : FilePattern(Offset + i);

        if (Buffer[i] != Want)
            Bad++;
    }
    CHECK(Bad == 0);
    free(Buffer);
}

void
TestViews(void)
{
    static CC_VIEW_RANGE Held[CC_MIN_VIEWS + 1];
    TEST_FILE File;
    CC_CACHE Cache;
    CC_MAP Map;
    CC_VIEW_RANGE Range;
    ULONG i;

    FileCreate(&File, 64 * MB);
    CHECK(CcCacheInitialize(&Cache, 1, 1000) == STATUS_SUCCESS);
    CHECK(Cache.ViewCount == CC_MIN_VIEWS);
    CcMapInitialize(&Map, &Cache, &File.Ops, &File, File.Size, File.Size, File.Size);

    CHECK(CcViewAcquire(&Map, 5 * MB + 123, 1000, &Range) == STATUS_SUCCESS);
    CHECK(Range.Length == 1000 && Range.FileOffset == 5 * MB + 123);
    CHECK(Range.Address == File.Pages + 5 * MB + 123);
    CHECK(CcViewMakeResident(&Map, &Range, FALSE) == STATUS_SUCCESS);
    CHECK(*(PUCHAR)Range.Address == FilePattern(5 * MB + 123));
    CcViewRelease(&Range);
    CHECK(Cache.ViewMisses == 1 && Cache.ViewHits == 0);

    CHECK(CcViewAcquire(&Map, 5 * MB, 600000, &Range) == STATUS_SUCCESS);
    CHECK(Range.Length == CC_VIEW_SIZE);
    CcViewRelease(&Range);
    CHECK(Cache.ViewHits == 1 && File.MappedViews == 1);

    CHECK(CcViewAcquire(&Map, File.Size, 1, &Range) == STATUS_END_OF_FILE);

    for (i = 0; i < 256; i++)
        ReadCheck(&Map, (ULONG64)i * CC_VIEW_SIZE + 17, 5000, NULL);
    CHECK(File.MappedViews == (LONG)Cache.ViewCount);
    CHECK(Cache.ViewReclaims >= 256 - CC_MIN_VIEWS);
    CHECK(CcCacheCheck(&Cache) == 0);

    for (i = 0; i < CC_MIN_VIEWS; i++)
        CHECK(CcViewAcquire(&Map, (ULONG64)i * CC_VIEW_SIZE, 1, &Held[i]) == STATUS_SUCCESS);
    CHECK(CcViewAcquire(&Map, (ULONG64)200 * CC_VIEW_SIZE, 1, &Held[CC_MIN_VIEWS]) == STATUS_INSUFFICIENT_RESOURCES);
    CHECK(!CcMapDetachViews(&Map, 0, (ULONG64)-1));
    CHECK(!CcMapUninitialize(&Map));
    for (i = 0; i < CC_MIN_VIEWS; i++)
        CcViewRelease(&Held[i]);

    CHECK(CcCacheTrim(&Cache, 10) == 10);
    CHECK(File.MappedViews == (LONG)Cache.ViewCount - 10);
    CHECK(CcMapDetachViews(&Map, 0, 4 * CC_VIEW_SIZE));
    CHECK(CcMapUninitialize(&Map));
    CHECK(File.MappedViews == 0);
    CHECK(CcCacheCheck(&Cache) == 0);
    CHECK(File.Errors == 0);

    CcCacheUninitialize(&Cache);
    FileDestroy(&File);
}

void
TestDirty(void)
{
    TEST_FILE File;
    CC_CACHE Cache;
    CC_MAP Map;
    CC_MAP Other;
    PUCHAR Data = malloc(3 * MB);
    MOVE_CONTEXT Move = { Data, MB - 100 };
    ULONG Flushed;
    ULONG i;

    for (i = 0; i < 3 * MB; i++)
        Data[i] = (UCHAR)(i * 7 + 1);

    FileCreate(&File, 16 * MB);
    CHECK(CcCacheInitialize(&Cache, 128, 600) == STATUS_SUCCESS);
    CcMapInitialize(&Map, &Cache, &File.Ops, &File, File.Size, File.Size, File.Size);
    CcMapInitialize(&Other, &Cache, &File.Ops, &File, File.Size, File.Size, File.Size);

    CHECK(CcDirtyNextMap(&Cache) == NULL);
    CHECK(CcCopyRange(&Map, MB - 100, 2 * MB, TRUE, MoveFromBuffer, &Move, NULL) == STATUS_SUCCESS);
    CHECK(Map.DirtyPages == (LONG64)((((ULONG64)3 * MB - 101) >> PAGE_SHIFT) - (((ULONG64)MB - 100) >> PAGE_SHIFT) + 1));
    CHECK(Cache.TotalDirtyPages == Map.DirtyPages);
    CHECK(CcDirtyNextMap(&Cache) == &Map);
    CHECK(CcDirtyQuery(&Map, MB, 10) && !CcDirtyQuery(&Map, 8 * MB, MB));

    {
        LONG64 Before = Map.DirtyPages;

        CHECK(CcCopyRange(&Map, MB - 100, 2 * MB, TRUE, MoveFromBuffer, &Move, NULL) == STATUS_SUCCESS);
        CHECK(Map.DirtyPages == Before);
    }

    CHECK(memcmp(File.Pages + MB - 100, Data, 2 * MB) == 0);
    CHECK(File.Disk[MB] == FilePattern(MB));

    CHECK(!CcDirtyCanWrite(&Cache, &Map, (ULONG)(Cache.DirtyPageThreshold - Map.DirtyPages + 1) * PAGE_SIZE));
    CHECK(CcDirtyCanWrite(&Cache, &Map, PAGE_SIZE));
    Map.DirtyPageThreshold = Map.DirtyPages;
    CHECK(!CcDirtyCanWrite(&Cache, &Map, PAGE_SIZE));
    CHECK(CcDirtyCanWrite(&Cache, &Other, PAGE_SIZE));
    Map.DirtyPageThreshold = 0;
    {
        LONG64 Total = Cache.TotalDirtyPages;
        LONG64 Want = Total / CC_LAZY_DIVISOR;

        if (Want < CC_LAZY_MIN_PAGES)
            Want = (Total < CC_LAZY_MIN_PAGES) ? Total : CC_LAZY_MIN_PAGES;
        CHECK(CcDirtyLazyTarget(&Cache) == (ULONG)Want);
    }

    {
        LONG64 Before = Map.DirtyPages;

        ULONG Part = (ULONG)(Before / 3);

        CHECK(CcDirtyFlush(&Map, 0, (ULONG64)-1, Part, &Flushed) == STATUS_SUCCESS);
        CHECK(Flushed == Part && Map.DirtyPages == Before - Part);

        File.FlushFailuresLeft = 1;
        Before = Map.DirtyPages;
        CHECK(CcDirtyFlush(&Map, 0, (ULONG64)-1, 1000000, &Flushed) == STATUS_UNEXPECTED_IO_ERROR);
        CHECK(Map.DirtyPages == Before - Flushed && Map.DirtyPages > 0);
        CHECK(CcDirtyFlush(&Map, 0, (ULONG64)-1, 1000000, &Flushed) == STATUS_SUCCESS);
    }

    CHECK(Map.DirtyPages == 0 && Cache.TotalDirtyPages == 0);
    CHECK(CcDirtyNextMap(&Cache) == NULL);
    CHECK(memcmp(File.Disk + MB - 100, Data, 2 * MB) == 0);
    CHECK(File.Disk[MB - 101] == FilePattern(MB - 101));

    Move.BaseOffset = 4 * MB;
    CHECK(CcCopyRange(&Map, 4 * MB, MB, TRUE, MoveFromBuffer, &Move, NULL) == STATUS_SUCCESS);
    CcDirtyDiscard(&Map, 4 * MB + 10, (ULONG64)-1);
    CHECK(Map.DirtyPages == 1);
    CcDirtyDiscard(&Map, 4 * MB, (ULONG64)-1);
    CHECK(Map.DirtyPages == 0 && Cache.TotalDirtyPages == 0);

    CHECK(CcMapUninitialize(&Map) && CcMapUninitialize(&Other));
    CHECK(File.MappedViews == 0 && File.Errors == 0);
    CcCacheUninitialize(&Cache);
    FileDestroy(&File);
    free(Data);
}

void
TestBcb(void)
{
    TEST_FILE File;
    CC_CACHE Cache;
    CC_MAP Map;
    PCC_BCB First, Second, Spanning, Pinned;
    BOOLEAN Created;
    ULONG64 Oldest, Newest;
    ULONG Count;
    ULONG64 Generation;

    FileCreate(&File, 8 * MB);
    CHECK(CcCacheInitialize(&Cache, 64, 10000) == STATUS_SUCCESS);
    CcMapInitialize(&Map, &Cache, &File.Ops, &File, File.Size, File.Size, File.Size);

    CHECK(CcBcbAcquire(&Map, 4096, 512, FALSE, TRUE, &First, &Created) == STATUS_CANT_WAIT);
    CHECK(CcBcbAcquire(&Map, 4096, 512, FALSE, FALSE, &First, &Created) == STATUS_SUCCESS && Created);
    CHECK(CcBcbMakeResident(First, FALSE) == STATUS_SUCCESS);
    CHECK(*(PUCHAR)CcBcbAddress(First, 4100) == FilePattern(4100));
    CHECK(CcBcbAcquire(&Map, 4200, 100, FALSE, FALSE, &Second, &Created) == STATUS_SUCCESS && !Created);
    CHECK(Second == First && First->ReferenceCount == 2);
    CHECK(CcBcbAcquire(&Map, 4200, 100, TRUE, FALSE, &Pinned, &Created) == STATUS_SUCCESS && Created);
    CHECK(Pinned != First);
    CcBcbPin(Pinned);

    CHECK(CcBcbAcquire(&Map, CC_VIEW_SIZE - 3000, 9000, FALSE, FALSE, &Spanning, &Created) == STATUS_SUCCESS);
    CHECK(Created && Spanning->OwnBase != NULL && Spanning->View == NULL);
    CHECK(CcBcbMakeResident(Spanning, FALSE) == STATUS_SUCCESS);
    CHECK(*(PUCHAR)CcBcbAddress(Spanning, CC_VIEW_SIZE + 5) == FilePattern(CC_VIEW_SIZE + 5));
    CHECK(CcBcbAddress(Spanning, CC_VIEW_SIZE - 3000) == File.Pages + CC_VIEW_SIZE - 3000);

    CHECK(CcBcbRangeBusy(&Map, 0, 8192));
    CHECK(!CcBcbRangeBusy(&Map, MB, 2 * MB));
    CHECK(!CcMapUninitialize(&Map));

    Generation = CcBcbBeginFlush(&Map);
    CHECK(CcBcbSetDirty(Pinned, 50) == STATUS_SUCCESS);
    CHECK(CcBcbSetDirty(Pinned, 40) == STATUS_SUCCESS);
    CHECK(CcBcbDirtyLsns(&Map, &Oldest, &Newest, &Count) && Oldest == 40 && Newest == 50 && Count == 1);
    CHECK(Map.DirtyPages == 1);

    CHECK(CcBcbCompleteFlush(&Map, 0, (ULONG64)-1, Generation));
    CcBcbUnpin(Pinned);
    CcBcbDereference(Pinned);
    CHECK(CcBcbRangeBusy(&Map, 0, 8192));
    CcBcbDereference(First);
    CcBcbDereference(Second);
    CcBcbDereference(Spanning);
    CHECK(!CcBcbRangeBusy(&Map, 0, 8 * MB));

    CHECK(CcBcbAcquire(&Map, 4200, 100, TRUE, TRUE, &Second, &Created) == STATUS_SUCCESS && Second == Pinned);
    CcBcbDereference(Second);
    CHECK(CcDirtyFlush(&Map, 0, (ULONG64)-1, 100, NULL) == STATUS_SUCCESS);
    CHECK(!CcBcbCompleteFlush(&Map, 0, (ULONG64)-1, CcBcbBeginFlush(&Map)));
    CHECK(CcBcbAcquire(&Map, 4200, 100, TRUE, TRUE, &Second, &Created) == STATUS_CANT_WAIT);
    CHECK(!CcBcbDirtyLsns(&Map, &Oldest, &Newest, &Count));

    CHECK(CcMapUninitialize(&Map));
    CHECK(File.MappedViews == 0 && File.Errors == 0);
    CcCacheUninitialize(&Cache);
    FileDestroy(&File);
}

void
TestReadAhead(void)
{
    CC_READ_AHEAD State;
    ULONG64 Offset;
    ULONG Length;
    ULONG64 Position = 0;
    ULONG Triggers = 0;
    ULONG64 Covered = 0;
    ULONG i;

    CcReadAheadInitialize(&State);
    for (i = 0; i < 200; i++)
    {
        if (CcReadAheadNote(&State, Position, 4096, 64 * MB, &Offset, &Length))
        {
            Triggers++;
            CHECK(Offset >= Position && Offset + Length <= 64 * MB && Length != 0);
            CHECK(Offset >= Covered);
            Covered = Offset + Length;
        }
        Position += 4096;
    }
    CHECK(Triggers >= 2 && Triggers < 60);
    CHECK(Covered >= Position);

    CcReadAheadInitialize(&State);
    Triggers = 0;
    for (i = 0; i < 200; i++)
    {
        ULONG64 Seed = i * 7919 + 13;

        if (CcReadAheadNote(&State, (Rng(&Seed) % 1000) * 65536, 4096, 64 * MB, &Offset, &Length))
            Triggers++;
    }
    CHECK(Triggers == 0);

    CcReadAheadInitialize(&State);
    Position = 64 * MB - 5 * 4096;
    for (i = 0; i < 5; i++)
    {
        if (CcReadAheadNote(&State, Position, 4096, 64 * MB, &Offset, &Length))
            CHECK(Offset + Length <= 64 * MB);
        Position += 4096;
    }

    State.Disabled = TRUE;
    CHECK(!CcReadAheadNote(&State, 0, 4096, 64 * MB, &Offset, &Length));
}
