/*
 * PROJECT:     ReactOS DWM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM CPU blit and clipping tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the DwmBlitSurface logic which copies a source surface into the
 * composition buffer with rectangle clipping.
 */

#include "precomp.h"

/* Replicate needed structures */
typedef struct _TEST_COMP_BUFFER
{
    UINT    Width;
    UINT    Height;
    UINT    Stride;
    UINT    BytesPerPixel;
    PVOID   pPixels;
    SIZE_T  AllocationSize;
} TEST_COMP_BUFFER;

typedef struct _TEST_SURFACE
{
    UINT    Width;
    UINT    Height;
    UINT    Stride;
    UINT    BytesPerPixel;
    PVOID   pPixels;
    SIZE_T  AllocationSize;
} TEST_SURFACE;

static BOOL AllocBuffer(TEST_COMP_BUFFER *buf, UINT w, UINT h)
{
    buf->Width = w;
    buf->Height = h;
    buf->BytesPerPixel = 4;
    buf->Stride = w * 4;
    buf->AllocationSize = (SIZE_T)buf->Stride * h;
    buf->pPixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, buf->AllocationSize);
    return buf->pPixels != NULL;
}

static void FreeBuffer(TEST_COMP_BUFFER *buf)
{
    if (buf->pPixels) HeapFree(GetProcessHeap(), 0, buf->pPixels);
    memset(buf, 0, sizeof(*buf));
}

static BOOL AllocSurface(TEST_SURFACE *s, UINT w, UINT h)
{
    s->Width = w;
    s->Height = h;
    s->BytesPerPixel = 4;
    s->Stride = w * 4;
    s->AllocationSize = (SIZE_T)s->Stride * h;
    s->pPixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, s->AllocationSize);
    return s->pPixels != NULL;
}

static void FreeSurface(TEST_SURFACE *s)
{
    if (s->pPixels) HeapFree(GetProcessHeap(), 0, s->pPixels);
    memset(s, 0, sizeof(*s));
}

/* Replicate DwmBlitSurface logic */
static void TestBlitSurface(TEST_COMP_BUFFER *pDst, INT DstX, INT DstY,
                            TEST_SURFACE *pSrc, PRECT pSrcRect)
{
    INT SrcLeft, SrcTop, SrcWidth, SrcHeight;
    INT ClipLeft, ClipTop, ClipRight, ClipBottom;
    INT CopyX, CopyY, CopyWidth, CopyHeight;
    INT y;

    if (!pDst || !pSrc || !pDst->pPixels || !pSrc->pPixels)
        return;

    if (pSrcRect)
    {
        SrcLeft = pSrcRect->left;
        SrcTop = pSrcRect->top;
        SrcWidth = pSrcRect->right - pSrcRect->left;
        SrcHeight = pSrcRect->bottom - pSrcRect->top;
    }
    else
    {
        SrcLeft = 0;
        SrcTop = 0;
        SrcWidth = (INT)pSrc->Width;
        SrcHeight = (INT)pSrc->Height;
    }

    if (SrcWidth <= 0 || SrcHeight <= 0)
        return;

    /* Clip to destination bounds */
    ClipLeft = (DstX < 0) ? -DstX : 0;
    ClipTop = (DstY < 0) ? -DstY : 0;
    ClipRight = (DstX + SrcWidth > (INT)pDst->Width)
                    ? (DstX + SrcWidth - (INT)pDst->Width) : 0;
    ClipBottom = (DstY + SrcHeight > (INT)pDst->Height)
                     ? (DstY + SrcHeight - (INT)pDst->Height) : 0;

    CopyX = SrcLeft + ClipLeft;
    CopyY = SrcTop + ClipTop;
    CopyWidth = SrcWidth - ClipLeft - ClipRight;
    CopyHeight = SrcHeight - ClipTop - ClipBottom;

    if (CopyWidth <= 0 || CopyHeight <= 0)
        return;

    for (y = 0; y < CopyHeight; ++y)
    {
        BYTE *dst = (BYTE *)pDst->pPixels +
                    (size_t)(DstY + ClipTop + y) * pDst->Stride +
                    (size_t)(DstX + ClipLeft) * pDst->BytesPerPixel;
        BYTE *src = (BYTE *)pSrc->pPixels +
                    (size_t)(CopyY + y) * pSrc->Stride +
                    (size_t)CopyX * pSrc->BytesPerPixel;
        memcpy(dst, src, (size_t)CopyWidth * pDst->BytesPerPixel);
    }
}

static UINT GetPixel32(void *pixels, UINT stride, INT x, INT y)
{
    UINT *row = (UINT *)((BYTE *)pixels + (size_t)y * stride);
    return row[x];
}

static void SetPixel32(void *pixels, UINT stride, INT x, INT y, UINT val)
{
    UINT *row = (UINT *)((BYTE *)pixels + (size_t)y * stride);
    row[x] = val;
}

static void FillSurface(TEST_SURFACE *s, UINT color)
{
    UINT x, y;
    for (y = 0; y < s->Height; ++y)
        for (x = 0; x < s->Width; ++x)
            SetPixel32(s->pPixels, s->Stride, x, y, color);
}

/* ---- Tests ---- */

static void Test_BasicBlit(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;

    AllocBuffer(&dst, 100, 100);
    AllocSurface(&src, 10, 10);
    FillSurface(&src, 0xFF112233);

    TestBlitSurface(&dst, 5, 5, &src, NULL);

    /* Check corners of the blitted region */
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 5, 5), 0xFF112233);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 14, 14), 0xFF112233);

    /* Check outside region is still black */
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 0), 0x00000000);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 4, 4), 0x00000000);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 15, 15), 0x00000000);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 99, 99), 0x00000000);

    FreeBuffer(&dst);
    FreeSurface(&src);
}

static void Test_BlitAtOrigin(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;

    AllocBuffer(&dst, 50, 50);
    AllocSurface(&src, 5, 5);
    FillSurface(&src, 0xAABBCCDD);

    TestBlitSurface(&dst, 0, 0, &src, NULL);

    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 0), 0xAABBCCDD);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 4, 4), 0xAABBCCDD);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 5, 0), 0x00000000);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 5), 0x00000000);

    FreeBuffer(&dst);
    FreeSurface(&src);
}

static void Test_BlitClipRight(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;

    AllocBuffer(&dst, 20, 20);
    AllocSurface(&src, 10, 10);
    FillSurface(&src, 0xFF0000FF);

    /* Blit starting at x=15, so only 5 columns fit */
    TestBlitSurface(&dst, 15, 0, &src, NULL);

    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 15, 0), 0xFF0000FF);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 19, 0), 0xFF0000FF);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 14, 0), 0x00000000);

    FreeBuffer(&dst);
    FreeSurface(&src);
}

static void Test_BlitClipBottom(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;

    AllocBuffer(&dst, 20, 20);
    AllocSurface(&src, 10, 10);
    FillSurface(&src, 0xFF00FF00);

    /* Blit starting at y=15, so only 5 rows fit */
    TestBlitSurface(&dst, 0, 15, &src, NULL);

    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 15), 0xFF00FF00);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 19), 0xFF00FF00);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 14), 0x00000000);

    FreeBuffer(&dst);
    FreeSurface(&src);
}

static void Test_BlitClipLeft(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;

    AllocBuffer(&dst, 20, 20);
    AllocSurface(&src, 10, 10);
    FillSurface(&src, 0xFFFF0000);

    /* Blit starting at x=-5, so left half of src is clipped */
    TestBlitSurface(&dst, -5, 0, &src, NULL);

    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 0), 0xFFFF0000);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 4, 0), 0xFFFF0000);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 5, 0), 0x00000000);

    FreeBuffer(&dst);
    FreeSurface(&src);
}

static void Test_BlitClipTop(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;

    AllocBuffer(&dst, 20, 20);
    AllocSurface(&src, 10, 10);
    FillSurface(&src, 0xFFAAAA00);

    /* Blit starting at y=-5 */
    TestBlitSurface(&dst, 0, -5, &src, NULL);

    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 0), 0xFFAAAA00);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 4), 0xFFAAAA00);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 5), 0x00000000);

    FreeBuffer(&dst);
    FreeSurface(&src);
}

static void Test_BlitFullyClipped(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;

    AllocBuffer(&dst, 20, 20);
    AllocSurface(&src, 10, 10);
    FillSurface(&src, 0xFFFFFFFF);

    /* Blit entirely off-screen */
    TestBlitSurface(&dst, -100, -100, &src, NULL);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 0), 0x00000000);

    TestBlitSurface(&dst, 100, 100, &src, NULL);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 19, 19), 0x00000000);

    FreeBuffer(&dst);
    FreeSurface(&src);
}

static void Test_BlitWithSrcRect(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;
    RECT srcRect = { 2, 2, 8, 8 }; /* 6x6 sub-region */

    AllocBuffer(&dst, 20, 20);
    AllocSurface(&src, 10, 10);

    /* Set different color in the sub-region */
    FillSurface(&src, 0x00000000);
    {
        UINT x, y;
        for (y = 2; y < 8; ++y)
            for (x = 2; x < 8; ++x)
                SetPixel32(src.pPixels, src.Stride, x, y, 0xFFCC00CC);
    }

    TestBlitSurface(&dst, 5, 5, &src, &srcRect);

    /* Blitted region: dst[5..10, 5..10] should have the color */
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 5, 5), 0xFFCC00CC);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 10, 10), 0xFFCC00CC);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 4, 4), 0x00000000);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 11, 11), 0x00000000);

    FreeBuffer(&dst);
    FreeSurface(&src);
}

static void Test_BlitNullSafety(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;

    AllocBuffer(&dst, 10, 10);
    AllocSurface(&src, 5, 5);

    /* NULL destination */
    TestBlitSurface(NULL, 0, 0, &src, NULL);
    ok(TRUE, "NULL dst should not crash\n");

    /* NULL source */
    TestBlitSurface(&dst, 0, 0, NULL, NULL);
    ok(TRUE, "NULL src should not crash\n");

    FreeBuffer(&dst);
    FreeSurface(&src);
}

static void Test_BlitExactFit(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;
    UINT x, y;
    BOOL allFilled = TRUE;

    AllocBuffer(&dst, 10, 10);
    AllocSurface(&src, 10, 10);
    FillSurface(&src, 0xFF999999);

    /* Blit that exactly fills the destination */
    TestBlitSurface(&dst, 0, 0, &src, NULL);

    for (y = 0; y < 10; ++y)
    {
        for (x = 0; x < 10; ++x)
        {
            if (GetPixel32(dst.pPixels, dst.Stride, x, y) != 0xFF999999)
            {
                allFilled = FALSE;
                break;
            }
        }
    }
    ok(allFilled, "Exact-fit blit should fill entire destination\n");

    FreeBuffer(&dst);
    FreeSurface(&src);
}

static void Test_BlitOverwrite(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src1, src2;

    AllocBuffer(&dst, 20, 20);
    AllocSurface(&src1, 10, 10);
    AllocSurface(&src2, 10, 10);
    FillSurface(&src1, 0xFF111111);
    FillSurface(&src2, 0xFF222222);

    /* Blit src1 then src2 at same position — src2 should overwrite */
    TestBlitSurface(&dst, 5, 5, &src1, NULL);
    TestBlitSurface(&dst, 5, 5, &src2, NULL);

    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 5, 5), 0xFF222222);
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 14, 14), 0xFF222222);

    FreeBuffer(&dst);
    FreeSurface(&src1);
    FreeSurface(&src2);
}

static void Test_BlitZeroSizedSrcRect(void)
{
    TEST_COMP_BUFFER dst;
    TEST_SURFACE src;
    RECT zeroRect = { 5, 5, 5, 5 }; /* Zero-area */

    AllocBuffer(&dst, 20, 20);
    AllocSurface(&src, 10, 10);
    FillSurface(&src, 0xFFFFFFFF);

    TestBlitSurface(&dst, 0, 0, &src, &zeroRect);

    /* Nothing should have been written */
    ok_eq_hex(GetPixel32(dst.pPixels, dst.Stride, 0, 0), 0x00000000);

    FreeBuffer(&dst);
    FreeSurface(&src);
}

START_TEST(DwmBlit)
{
    Test_BasicBlit();
    Test_BlitAtOrigin();
    Test_BlitClipRight();
    Test_BlitClipBottom();
    Test_BlitClipLeft();
    Test_BlitClipTop();
    Test_BlitFullyClipped();
    Test_BlitWithSrcRect();
    Test_BlitNullSafety();
    Test_BlitExactFit();
    Test_BlitOverwrite();
    Test_BlitZeroSizedSrcRect();
}
