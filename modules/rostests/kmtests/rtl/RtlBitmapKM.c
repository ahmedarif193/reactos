/*
 * PROJECT:         ReactOS kernel-mode tests
 * LICENSE:         LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:         Kernel-Mode Test Suite RTL_BITMAP API
 */

#include <kmt_test.h>

#define NDEBUG
#include <debug.h>

static
VOID
TestBitmapBasic(VOID)
{
    RTL_BITMAP Bitmap;
    ULONG Buffer[4];

    RtlInitializeBitMap(&Bitmap, Buffer, 128);
    RtlClearAllBits(&Bitmap);
    ok_eq_ulong(RtlNumberOfSetBits(&Bitmap), 0UL);
    ok_eq_ulong(RtlNumberOfClearBits(&Bitmap), 128UL);

    RtlSetAllBits(&Bitmap);
    ok_eq_ulong(RtlNumberOfSetBits(&Bitmap), 128UL);
    ok_eq_ulong(RtlNumberOfClearBits(&Bitmap), 0UL);

    RtlClearAllBits(&Bitmap);
    RtlSetBits(&Bitmap, 10, 20);
    ok_eq_ulong(RtlNumberOfSetBits(&Bitmap), 20UL);
    ok_bool_true(RtlAreBitsSet(&Bitmap, 10, 20), "bits 10..29 set");
    ok_bool_false(RtlAreBitsSet(&Bitmap, 9, 2), "bit 9 not set");
    ok_bool_true(RtlAreBitsClear(&Bitmap, 30, 98), "bits 30..127 clear");
    ok_bool_true(RtlCheckBit(&Bitmap, 15) != 0, "bit 15");
    ok_bool_true(RtlCheckBit(&Bitmap, 30) == 0, "bit 30");

    RtlClearBits(&Bitmap, 15, 5);
    ok_eq_ulong(RtlNumberOfSetBits(&Bitmap), 15UL);
    ok_bool_false(RtlAreBitsSet(&Bitmap, 10, 20), "hole at 15..19");
}

static
VOID
TestBitmapSearch(VOID)
{
    RTL_BITMAP Bitmap;
    ULONG Buffer[4];
    ULONG Index;

    RtlInitializeBitMap(&Bitmap, Buffer, 128);
    RtlClearAllBits(&Bitmap);
    RtlSetBits(&Bitmap, 0, 8);
    RtlSetBits(&Bitmap, 16, 8);

    Index = RtlFindClearBits(&Bitmap, 8, 0);
    ok_eq_ulong(Index, 8UL);

    Index = RtlFindClearBitsAndSet(&Bitmap, 8, 0);
    ok_eq_ulong(Index, 8UL);
    ok_bool_true(RtlAreBitsSet(&Bitmap, 0, 24), "0..23 all set");

    Index = RtlFindSetBits(&Bitmap, 4, 0);
    ok_eq_ulong(Index, 0UL);

    Index = RtlFindSetBitsAndClear(&Bitmap, 24, 0);
    ok_eq_ulong(Index, 0UL);
    ok_eq_ulong(RtlNumberOfSetBits(&Bitmap), 0UL);

    RtlSetBits(&Bitmap, 100, 28);
    Index = RtlFindClearBits(&Bitmap, 29, 0);
    ok_eq_ulong(Index, 0UL);
    Index = RtlFindClearBits(&Bitmap, 100, 0);
    ok_eq_ulong(Index, 0UL);
    Index = RtlFindClearBits(&Bitmap, 101, 0);
    ok_eq_ulong(Index, 0xFFFFFFFFUL);
}

static
VOID
TestBitmapRuns(VOID)
{
    RTL_BITMAP Bitmap;
    ULONG Buffer[4];
    RTL_BITMAP_RUN Runs[4];
    ULONG Count;
    ULONG Longest, Start;

    RtlInitializeBitMap(&Bitmap, Buffer, 128);
    RtlSetAllBits(&Bitmap);
    RtlClearBits(&Bitmap, 4, 4);
    RtlClearBits(&Bitmap, 20, 12);
    RtlClearBits(&Bitmap, 60, 2);

    Count = RtlFindClearRuns(&Bitmap, Runs, 4, TRUE);
    ok_eq_ulong(Count, 3UL);
    if (Count == 3)
    {
        ok_eq_ulong(Runs[0].StartingIndex, 20UL);
        ok_eq_ulong(Runs[0].NumberOfBits, 12UL);
        ok_eq_ulong(Runs[1].StartingIndex, 4UL);
        ok_eq_ulong(Runs[1].NumberOfBits, 4UL);
        ok_eq_ulong(Runs[2].StartingIndex, 60UL);
        ok_eq_ulong(Runs[2].NumberOfBits, 2UL);
    }

    Start = 0;
    Longest = RtlFindLongestRunClear(&Bitmap, &Start);
    ok_eq_ulong(Longest, 12UL);
    ok_eq_ulong(Start, 20UL);
}

START_TEST(RtlBitmapKM)
{
    TestBitmapBasic();
    TestBitmapSearch();
    TestBitmapRuns();
}
