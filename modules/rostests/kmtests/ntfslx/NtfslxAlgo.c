/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Tests for ntfslx pure algorithm functions:
 *              bitmap, MST fixup, collation, LZNT1 decompress, runlist
 *
 * These test the algorithms in isolation, without needing a mounted
 * NTFS volume.  The ntfslx source files are compiled directly into
 * this test module.
 */

#include <kmt_test.h>
#include <ntifs.h>

/* Pull in ntfslx headers for types and prototypes */
#include "../../../../drivers/filesystems/ntfslx/ntfslx.h"

/* Provide the global that ntfslx.h extern-references (defined in ntfslx.c) */
NTFSLX_GLOBAL_DATA NtfslxGlobalData = { 0 };

/* ========================================================================
 * Bitmap tests
 * ======================================================================== */
static void TestBitmap(void)
{
    UCHAR Bitmap[8];  /* 64 bits */
    NTSTATUS Status;

    /* All zeros = 64 free */
    RtlZeroMemory(Bitmap, sizeof(Bitmap));
    ok_eq_ulonglong(NtfslxBitmapCountFreeBits(Bitmap, sizeof(Bitmap)), 64ULL);

    /* Set bit 0 */
    Status = NtfslxBitmapSetBitsInRun(Bitmap, sizeof(Bitmap), 0, 1, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(NtfslxBitmapTestBit(Bitmap, sizeof(Bitmap), 0), "Bit 0 should be set\n");
    ok(!NtfslxBitmapTestBit(Bitmap, sizeof(Bitmap), 1), "Bit 1 should be clear\n");
    ok_eq_ulonglong(NtfslxBitmapCountFreeBits(Bitmap, sizeof(Bitmap)), 63ULL);

    /* Set bits 10-19 (10 bits) */
    Status = NtfslxBitmapSetBitsInRun(Bitmap, sizeof(Bitmap), 10, 10, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(NtfslxBitmapTestBit(Bitmap, sizeof(Bitmap), 10), "Bit 10 should be set\n");
    ok(NtfslxBitmapTestBit(Bitmap, sizeof(Bitmap), 19), "Bit 19 should be set\n");
    ok(!NtfslxBitmapTestBit(Bitmap, sizeof(Bitmap), 20), "Bit 20 should be clear\n");
    ok_eq_ulonglong(NtfslxBitmapCountFreeBits(Bitmap, sizeof(Bitmap)), 53ULL);

    /* Clear bits 10-14 */
    Status = NtfslxBitmapSetBitsInRun(Bitmap, sizeof(Bitmap), 10, 5, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(!NtfslxBitmapTestBit(Bitmap, sizeof(Bitmap), 10), "Bit 10 should be clear\n");
    ok(NtfslxBitmapTestBit(Bitmap, sizeof(Bitmap), 15), "Bit 15 should still be set\n");
    ok_eq_ulonglong(NtfslxBitmapCountFreeBits(Bitmap, sizeof(Bitmap)), 58ULL);

    /* All ones = 0 free */
    RtlFillMemory(Bitmap, sizeof(Bitmap), 0xFF);
    ok_eq_ulonglong(NtfslxBitmapCountFreeBits(Bitmap, sizeof(Bitmap)), 0ULL);

    /* Out of range */
    Status = NtfslxBitmapSetBitsInRun(Bitmap, sizeof(Bitmap), 60, 10, 1);
    ok_eq_hex(Status, STATUS_BUFFER_OVERFLOW);

    /* Bit beyond range */
    ok(!NtfslxBitmapTestBit(Bitmap, sizeof(Bitmap), 64), "Bit 64 out of range\n");
    ok(!NtfslxBitmapTestBit(Bitmap, sizeof(Bitmap), 100), "Bit 100 out of range\n");
}

/* ========================================================================
 * MST fixup tests
 * ======================================================================== */
static void TestMstFixup(void)
{
    /* Create a fake 1024-byte record with 2 sectors (512 each).
     * USA: offset 48, count 3 (1 USN + 2 fixup entries) */
    UCHAR Buffer[1024];
    PNTFSLX_RECORD_HEADER Hdr = (PNTFSLX_RECORD_HEADER)Buffer;
    PUSHORT Usa;
    NTSTATUS Status;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Hdr->Magic = NTFSLX_RECORD_MAGIC_FILE;
    Hdr->UsaOffset = 48;
    Hdr->UsaCount = 3;  /* 1 USN + 2 sectors */

    /* Fill the USA: USN=1, save original last-words */
    Usa = (PUSHORT)(Buffer + 48);
    Usa[0] = 0x0001;  /* USN */
    Usa[1] = 0;       /* saved sector 0 last word */
    Usa[2] = 0;       /* saved sector 1 last word */

    /* Put known values at last USHORT of each sector */
    *(PUSHORT)(Buffer + 510) = 0x0001;  /* must match USN for post-read */
    *(PUSHORT)(Buffer + 1022) = 0x0001;

    /* Post-read fixup should succeed */
    Status = NtfslxPostReadMstFixup(Hdr, 1024);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* Now test pre-write fixup */
    RtlZeroMemory(Buffer, sizeof(Buffer));
    Hdr->Magic = NTFSLX_RECORD_MAGIC_FILE;
    Hdr->UsaOffset = 48;
    Hdr->UsaCount = 3;
    Usa = (PUSHORT)(Buffer + 48);
    Usa[0] = 5;  /* starting USN */

    /* Put known data at sector boundaries */
    *(PUSHORT)(Buffer + 510) = 0xAAAA;
    *(PUSHORT)(Buffer + 1022) = 0xBBBB;

    Status = NtfslxPreWriteMstFixup(Hdr, 1024);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* USN should have been incremented to 6 */
    ok_eq_uint(Usa[0], 6);
    /* Original values saved in USA */
    ok_eq_uint(Usa[1], 0xAAAA);
    ok_eq_uint(Usa[2], 0xBBBB);
    /* Sector boundaries replaced with USN */
    ok_eq_uint(*(PUSHORT)(Buffer + 510), 6);
    ok_eq_uint(*(PUSHORT)(Buffer + 1022), 6);

    /* Post-write fixup restores originals */
    NtfslxPostWriteMstFixup(Hdr);
    ok_eq_uint(*(PUSHORT)(Buffer + 510), 0xAAAA);
    ok_eq_uint(*(PUSHORT)(Buffer + 1022), 0xBBBB);
}

/* ========================================================================
 * Collation tests
 * ======================================================================== */
static void TestCollation(void)
{
    LONG Result;
    ULONG A = 100, B = 200, C = 100;
    UCHAR D1[] = { 1, 2, 3 };
    UCHAR D2[] = { 1, 2, 4 };
    UCHAR D3[] = { 1, 2, 3 };

    /* Binary collation */
    Result = NtfslxCollate(NTFSLX_COLLATION_BINARY,
        D1, 3, D2, 3, NULL, 0);
    ok(Result < 0, "D1 < D2 binary\n");

    Result = NtfslxCollate(NTFSLX_COLLATION_BINARY,
        D2, 3, D1, 3, NULL, 0);
    ok(Result > 0, "D2 > D1 binary\n");

    Result = NtfslxCollate(NTFSLX_COLLATION_BINARY,
        D1, 3, D3, 3, NULL, 0);
    ok_eq_long(Result, 0L);

    /* Binary with different lengths */
    Result = NtfslxCollate(NTFSLX_COLLATION_BINARY,
        D1, 2, D1, 3, NULL, 0);
    ok(Result < 0, "shorter < longer when prefix matches\n");

    /* NTOFS_ULONG */
    Result = NtfslxCollate(NTFSLX_COLLATION_NTOFS_ULONG,
        &A, 4, &B, 4, NULL, 0);
    ok(Result < 0, "100 < 200\n");

    Result = NtfslxCollate(NTFSLX_COLLATION_NTOFS_ULONG,
        &A, 4, &C, 4, NULL, 0);
    ok_eq_long(Result, 0L);

    Result = NtfslxCollate(NTFSLX_COLLATION_NTOFS_ULONG,
        &B, 4, &A, 4, NULL, 0);
    ok(Result > 0, "200 > 100\n");

    /* NTOFS_ULONGS */
    {
        ULONG Arr1[] = { 1, 2, 3, 4 };
        ULONG Arr2[] = { 1, 2, 4, 0 };
        ULONG Arr3[] = { 1, 2, 3, 4 };

        Result = NtfslxCollate(NTFSLX_COLLATION_NTOFS_ULONGS,
            Arr1, 16, Arr2, 16, NULL, 0);
        ok(Result < 0, "Arr1 < Arr2 at element 2\n");

        Result = NtfslxCollate(NTFSLX_COLLATION_NTOFS_ULONGS,
            Arr1, 16, Arr3, 16, NULL, 0);
        ok_eq_long(Result, 0L);
    }

    /* Unknown collation rule */
    Result = NtfslxCollate(0x99,
        D1, 3, D2, 3, NULL, 0);
    ok_eq_long(Result, -2L);
}

/* ========================================================================
 * LZNT1 decompression tests
 * ======================================================================== */
static void TestLznt1(void)
{
    NTSTATUS Status;
    ULONG FinalSize;

    /* Test 1: uncompressed sub-block (bit 15 clear)
     * Header: size=4 (5 bytes - 1), not compressed
     * Data: "ABCD\0" */
    {
        UCHAR Compressed[] = {
            0x04, 0x00,             /* header: size=4+1=5, not compressed */
            'A', 'B', 'C', 'D', 0  /* 5 bytes literal data */
        };
        UCHAR Output[4096];

        RtlZeroMemory(Output, sizeof(Output));
        Status = NtfslxDecompressLznt1(
            Compressed, sizeof(Compressed),
            Output, sizeof(Output), &FinalSize);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok(Output[0] == 'A', "Output[0]='A'\n");
        ok(Output[1] == 'B', "Output[1]='B'\n");
        ok(Output[2] == 'C', "Output[2]='C'\n");
        ok(Output[3] == 'D', "Output[3]='D'\n");
    }

    /* Test 2: zero-terminated stream */
    {
        UCHAR Compressed[] = { 0x00, 0x00 };  /* zero header = end */
        UCHAR Output[16];

        RtlZeroMemory(Output, sizeof(Output));
        Status = NtfslxDecompressLznt1(
            Compressed, sizeof(Compressed),
            Output, sizeof(Output), &FinalSize);
        ok_eq_hex(Status, STATUS_SUCCESS);
        /* All output should be zero-filled */
        ok(Output[0] == 0, "zero-fill\n");
    }
}

/* ========================================================================
 * Runlist tests
 * ======================================================================== */
static void TestRunlist(void)
{
    NTFSLX_RUNLIST_ELEMENT Rl1[2] = {
        { 0, 100, 10 },   /* VCN 0-9 at LCN 100 */
        { 10, 200, 20 }   /* VCN 10-29 at LCN 200 */
    };

    /* Sparse check */
    ok(!NtfslxRunlistIsSparse(Rl1, 2), "Rl1 not sparse\n");

    {
        NTFSLX_RUNLIST_ELEMENT RlSparse[2] = {
            { 0, 100, 10 },
            { 10, NTFSLX_LCN_HOLE, 20 }  /* hole */
        };
        ok(NtfslxRunlistIsSparse(RlSparse, 2), "RlSparse is sparse\n");
    }

    /* Merge test */
    {
        NTFSLX_RUNLIST_ELEMENT Src[1] = {
            { 5, 150, 5 }  /* VCN 5-9 at LCN 150 */
        };
        PNTFSLX_RUNLIST_ELEMENT Merged = NULL;
        ULONG MergedCount = 0;
        NTSTATUS Status;

        Status = NtfslxRunlistsMerge(Rl1, 2, Src, 1, &Merged, &MergedCount);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(MergedCount, 3UL);
        if (Merged != NULL)
        {
            /* Should be sorted by VCN: {0,100,10}, {5,150,5}, {10,200,20} */
            ok_eq_longlong(Merged[0].Vcn, 0LL);
            ok_eq_longlong(Merged[1].Vcn, 5LL);
            ok_eq_longlong(Merged[2].Vcn, 10LL);
            ExFreePoolWithTag(Merged, NTFSLX_TAG);
        }
    }

    /* Truncate test */
    {
        NTFSLX_RUNLIST_ELEMENT Rl[3] = {
            { 0, 100, 10 },
            { 10, 200, 10 },
            { 20, 300, 10 }
        };
        PNTFSLX_RUNLIST_ELEMENT RlPtr = Rl;
        ULONG Count = 3;
        NTSTATUS Status;

        Status = NtfslxRunlistTruncate(&RlPtr, &Count, 15);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_ulong(Count, 2UL);
        ok_eq_longlong(Rl[0].Length, 10LL);
        ok_eq_longlong(Rl[1].Length, 5LL);  /* truncated from 10 to 5 */
    }

    /* Mapping pairs size */
    {
        LONG Size = NtfslxGetSizeForMappingPairs(12, Rl1, 2, 0, -1);
        ok(Size > 0, "Size should be positive, got %ld\n", Size);
        /* At minimum: 2 runs * (1 header + len bytes + offset bytes) + 1 terminator */
        ok(Size >= 3, "Size should be >= 3, got %ld\n", Size);
    }

    /* Mapping pairs build */
    {
        UCHAR MpBuf[64];
        LONGLONG StopVcn = 0;
        NTSTATUS Status;

        RtlZeroMemory(MpBuf, sizeof(MpBuf));
        Status = NtfslxMappingPairsBuild(12, MpBuf, sizeof(MpBuf),
            Rl1, 2, 0, -1, &StopVcn);
        ok_eq_hex(Status, STATUS_SUCCESS);
        ok_eq_longlong(StopVcn, 30LL);  /* VCN 10 + length 20 */
        /* First byte should be non-zero (header of first run) */
        ok(MpBuf[0] != 0, "First mapping pair header should be non-zero\n");
    }
}

/* ========================================================================
 * Core string tests
 * ======================================================================== */
static void TestStrings(void)
{
    static const USHORT Str1[] = { 'H', 'E', 'L', 'L', 'O' };
    static const USHORT Str2[] = { 'h', 'e', 'l', 'l', 'o' };
    static const USHORT Str3[] = { 'W', 'O', 'R', 'L', 'D' };

    /* Generate upcase table */
    PUSHORT Upcase = NtfslxGenerateDefaultUpcase();
    ok(Upcase != NULL, "Upcase table should not be NULL\n");
    if (Upcase == NULL)
        return;

    /* Case-sensitive compare */
    ok(NtfslxUcsNCompare(Str1, Str1, 5) == 0, "Same strings equal\n");
    ok(NtfslxUcsNCompare(Str1, Str2, 5) != 0, "Different case not equal\n");

    /* Case-insensitive compare */
    ok(NtfslxUcsNCaseCompare(Str1, Str2, 5, Upcase, 0x10000) == 0,
       "HELLO == hello case-insensitive\n");
    ok(NtfslxUcsNCaseCompare(Str1, Str3, 5, Upcase, 0x10000) != 0,
       "HELLO != WORLD\n");

    /* AreNamesEqual */
    ok(NtfslxAreNamesEqual(Str1, 5, Str2, 5, TRUE, Upcase, 0x10000),
       "HELLO == hello ignore-case\n");
    ok(!NtfslxAreNamesEqual(Str1, 5, Str2, 5, FALSE, Upcase, 0x10000),
       "HELLO != hello case-sensitive\n");
    ok(!NtfslxAreNamesEqual(Str1, 5, Str3, 5, TRUE, Upcase, 0x10000),
       "HELLO != WORLD\n");

    ExFreePoolWithTag(Upcase, NTFSLX_TAG);
}

/* ========================================================================
 * Entry point
 * ======================================================================== */
START_TEST(NtfslxAlgo)
{
    TestBitmap();
    TestMstFixup();
    TestCollation();
    TestLznt1();
    TestRunlist();
    TestStrings();
}
