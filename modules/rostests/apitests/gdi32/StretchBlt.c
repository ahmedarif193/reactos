/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for the StretchBlt API
 * COPYRIGHT:   Copyright 2020, 2021 Doug Lyons (douglyons at douglyons dot com)
 *              Some Code copied and modified from Wine gdi32:bitmap test.
 */

#include <stdarg.h>
#include <assert.h>
#include <string.h>

#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winerror.h"
#include "wingdi.h"
#include "winuser.h"
#include "wine/test.h"
#include <debug.h>

/* Notes on using the StretchBlt function's general flip ability.
 *
 * The critical values for flipping are the signs of the values for DestWidth, DestHeight, SourceWidth and SourceHeight.
 * If we assign a '0' to values having a negative sign and a '1' to values having a positive sign these can be CaseWXYZ's.
 * Where the W, X, Y, and Z are replaced with the '0's and '1's representing the signs of the values in sequential order.
 *
 * If we take the normal StretchBlt function's copy/scaling with no flips, the generalized code can be represented as follows:
 *
 * StretchBlt(DestDC,   DestX,   DestY,   (DestWidth),   (DestHeight),
 *            SourceDC, SourceX, SourceY, (SourceWidth), (SourceHeight), SRCCOPY); // Case1111 (15) (all positive signs)
 *
 * For Horizontal flipping then the generalized form on the Destination side can be represented as follows:
 *
 * StretchBlt(DestDC,   DestX + DestWidth - 1,  DestY,   -DestWidth,   DestHeight,
 *            SourceDC, SourceX,                SourceY, SourceWidth,  SourceHeight, SRCCOPY);  // Case 0111 (7)
 *
 * and for Horizontal flipping the generalized form on the Source side can be represented as follows:
 *
 * StretchBlt(DestDC,   DestX,                     DestY,   DestWidth,    DestHeight,
 *            SourceDC, SourceX + SourceWidth - 1, SourceY, -SourceWidth, SourceHeight, SRCCOPY); // Case 1101 (13)
 *
 * I believe that the "- 1" is used because we are moving from the rightmost position back to the 0th position.
 *
 * But there are three "special" cases where no flip is done (there is a copy/scale only) and the "-1" is not used.
 * These are as follows:
 * 1)
 * StretchBlt(DestDC,   DestX + DestWidth,     DestY,   -DestWidth,   DestHeight,  // Both Widths negative
 *            SourceDC, SourceX + SourceWidth, SourceY, -SourceWidth, SourceHeight, SRCCOPY); // Case0101 (5)
 * 2)
 * StretchBlt(DestDC,   DestX,   DestY + DestHeight,     DestWidth,   -DestHeight, // Both heights negative
 *            SourceDC, SourceX, SourceY + SourceHeight, SourceWidth, -SourceHeight, SRCCOPY); // Case1010 (10)
 * 3)
 * StretchBlt(DestDC,   DestX + DestWidth,     DestY + DestHeight,     -DestWidth,   -DestHeight, // widths AND heights neg
 *            SourceDC, SourceX + SourceWidth, SourceY + SourceHeight, -SourceWidth, -SourceHeight, SRCCOPY); // Case0000 (0)
 *
 * I suspect that these are like this because of legacy reasons when StretchBlt did not support so many flip options.
 *
 * For Vertical flipping the generalized form on the Destination side can be represented as follows:
 *
 * StretchBlt(DestDC,   DestX,   DestY + DestHeight - 1,  DestWidth,   -DestHeight,
 *            SourceDC, SourceX, SourceY,                 SourceWidth, SourceHeight, SRCCOPY); // Case1011 (11)
 *
 * and for Vertical on the Source Side as folows:
 *
 * StretchBlt(DestDC,   DestX,   DestY,                      DestWidth,   -DestHeight,
 *            SourceDC, SourceX, SourceY + SourceHeight - 1, SourceWidth, -SourceHeight, SRCCOPY); // Case 1010 (10)
 */

static inline int get_dib_stride( int width, int bpp )
{
    return ((width * bpp + 31) >> 3) & ~3;
}

static inline int get_dib_image_size( const BITMAPINFO *info )
{
    return get_dib_stride( info->bmiHeader.biWidth, info->bmiHeader.biBitCount )
        * abs( info->bmiHeader.biHeight );
}

static void test_StretchBlt_halftone(void)
{
    BITMAPINFO src_info = {0};
    BITMAPINFO dst_info = {0};
    UINT32 *src_bits = NULL;
    UINT32 *dst_bits = NULL;
    UINT32 color_pixels[25];
    HBITMAP src_bitmap = NULL;
    HBITMAP dst_bitmap = NULL;
    HBITMAP old_src_bitmap = NULL;
    HBITMAP old_dst_bitmap = NULL;
    HDC hdc_src = NULL;
    HDC hdc_dst = NULL;
    BOOL differs = FALSE;
    BOOL has_intermediate = FALSE;
    INT old_mode;
    UINT i;

    src_info.bmiHeader.biSize = sizeof(src_info.bmiHeader);
    src_info.bmiHeader.biWidth = 2;
    src_info.bmiHeader.biHeight = -2;
    src_info.bmiHeader.biPlanes = 1;
    src_info.bmiHeader.biBitCount = 32;
    src_info.bmiHeader.biCompression = BI_RGB;
    dst_info = src_info;
    dst_info.bmiHeader.biWidth = 5;
    dst_info.bmiHeader.biHeight = -5;

    hdc_src = CreateCompatibleDC(NULL);
    hdc_dst = CreateCompatibleDC(NULL);
    src_bitmap = CreateDIBSection(hdc_src, &src_info, DIB_RGB_COLORS, (void **)&src_bits, NULL, 0);
    dst_bitmap = CreateDIBSection(hdc_dst, &dst_info, DIB_RGB_COLORS, (void **)&dst_bits, NULL, 0);
    ok(hdc_src != NULL && hdc_dst != NULL && src_bitmap != NULL && dst_bitmap != NULL, "Failed to create HALFTONE test DCs or bitmaps, error %lu\n", GetLastError());
    if (!hdc_src || !hdc_dst || !src_bitmap || !dst_bitmap)
        goto cleanup;

    old_src_bitmap = SelectObject(hdc_src, src_bitmap);
    old_dst_bitmap = SelectObject(hdc_dst, dst_bitmap);
    src_bits[0] = 0x00000000;
    src_bits[1] = 0x00ffffff;
    src_bits[2] = 0x00ffffff;
    src_bits[3] = 0x00000000;

    memset(dst_bits, 0, sizeof(color_pixels));
    old_mode = SetStretchBltMode(hdc_dst, COLORONCOLOR);
    ok(old_mode == BLACKONWHITE, "Expected default mode BLACKONWHITE, got %d\n", old_mode);
    ok(StretchBlt(hdc_dst, 0, 0, 5, 5, hdc_src, 0, 0, 2, 2, SRCCOPY), "COLORONCOLOR StretchBlt failed, error %lu\n", GetLastError());
    memcpy(color_pixels, dst_bits, sizeof(color_pixels));

    memset(dst_bits, 0, sizeof(color_pixels));
    old_mode = SetStretchBltMode(hdc_dst, HALFTONE);
    ok(old_mode == COLORONCOLOR, "Expected previous mode COLORONCOLOR, got %d\n", old_mode);
    ok(GetStretchBltMode(hdc_dst) == HALFTONE, "Expected current mode HALFTONE, got %d\n", GetStretchBltMode(hdc_dst));
    SetBrushOrgEx(hdc_dst, 0, 0, NULL);
    ok(StretchBlt(hdc_dst, 0, 0, 5, 5, hdc_src, 0, 0, 2, 2, SRCCOPY), "HALFTONE StretchBlt failed, error %lu\n", GetLastError());

    for (i = 0; i < ARRAYSIZE(color_pixels); ++i)
    {
        UINT32 pixel = dst_bits[i] & 0x00ffffff;

        if (dst_bits[i] != color_pixels[i])
            differs = TRUE;
        if (pixel != 0x00000000 && pixel != 0x00ffffff)
            has_intermediate = TRUE;
        ok((pixel & 0xff) == ((pixel >> 8) & 0xff) && (pixel & 0xff) == ((pixel >> 16) & 0xff), "HALFTONE pixel %u is not grayscale: %08x\n", i, dst_bits[i]);
    }
    ok(differs, "HALFTONE output unexpectedly matches COLORONCOLOR output\n");
    ok(has_intermediate, "HALFTONE output contains no filtered intermediate pixels\n");

cleanup:
    if (hdc_src && old_src_bitmap) SelectObject(hdc_src, old_src_bitmap);
    if (hdc_dst && old_dst_bitmap) SelectObject(hdc_dst, old_dst_bitmap);
    if (src_bitmap) DeleteObject(src_bitmap);
    if (dst_bitmap) DeleteObject(dst_bitmap);
    if (hdc_src) DeleteDC(hdc_src);
    if (hdc_dst) DeleteDC(hdc_dst);
}

static void check_StretchBlt_stretch(HDC hdcDst, HDC hdcSrc, BITMAPINFO *dst_info, UINT32 *dstBuffer, UINT32 *srcBuffer,
                                     int nXOriginDest, int nYOriginDest, int nWidthDest, int nHeightDest,
                                     int nXOriginSrc, int nYOriginSrc, int nWidthSrc, int nHeightSrc,
                                     UINT32 *expected, int line)
{
    int dst_size = get_dib_image_size( dst_info );

    memset(dstBuffer, 0, dst_size);
    StretchBlt(hdcDst, nXOriginDest, nYOriginDest, nWidthDest, nHeightDest,
               hdcSrc, nXOriginSrc, nYOriginSrc, nWidthSrc, nHeightSrc, SRCCOPY);
    ok(memcmp(dstBuffer, expected, dst_size) == 0,
        "StretchBlt \nexp { %08X, %08X, %08X, %08X } \ngot { %08X, %08X, %08X, %08X } \n"
        "destination { %d, %d, %d, %d } source { %d, %d, %d, %d } from line %d\n",
        expected[0], expected[1], expected[2], expected[3],
        dstBuffer[0], dstBuffer[1], dstBuffer[2], dstBuffer[3],
        nXOriginDest, nYOriginDest, nWidthDest, nHeightDest,
        nXOriginSrc, nYOriginSrc, nWidthSrc, nHeightSrc, line);
}

static void test_StretchBlt_stretch(HDC hdcDst, HDC hdcSrc, BITMAPINFO *dst_info, UINT32 *dstBuffer, UINT32 *srcBuffer,
                                    int nXOriginDest, int nYOriginDest, int nWidthDest, int nHeightDest,
                                    int nXOriginSrc, int nYOriginSrc, int nWidthSrc, int nHeightSrc,
                                    UINT32 *expected, int line, BOOL SrcTopDown, BOOL DstTopDown )
{
    int dst_size = get_dib_image_size( dst_info );

    memset(dstBuffer, 0, dst_size);
    StretchBlt(hdcDst, nXOriginDest, nYOriginDest, nWidthDest, nHeightDest,
               hdcSrc, nXOriginSrc, nYOriginSrc, nWidthSrc, nHeightSrc, SRCCOPY);
    ok(memcmp(dstBuffer, expected, dst_size) == 0,
        "Case%c%c%c%c %s - %s \nexp { %08X, %08X, %08X, %08X } \ngot { %08X, %08X, %08X, %08X }\n"
        "destination { %d, %d, %d, %d } source { %d, %d, %d, %d } from line %d\n",
         (nWidthDest < 0) ? '0' : '1', (nHeightDest < 0) ? '0' : '1',
         (nWidthSrc < 0) ? '0' : '1', (nHeightSrc < 0) ? '0' : '1',
         SrcTopDown ? "SrcTopDown" : "SrcBottomUp", DstTopDown ? "DstTopDown" : "DstBottomUp",
        expected[0], expected[1], expected[2], expected[3],
        dstBuffer[0], dstBuffer[1], dstBuffer[2], dstBuffer[3],
        nXOriginDest, nYOriginDest, nWidthDest, nHeightDest,
        nXOriginSrc, nYOriginSrc, nWidthSrc, nHeightSrc, line);
}

static void test_StretchBlt(void)
{
    HBITMAP bmpDst, bmpSrc;
    HBITMAP oldDst, oldSrc;
    HDC hdcScreen, hdcDst, hdcSrc;
    UINT32 *dstBuffer, *srcBuffer;
    BITMAPINFO biDst, biSrc;
    UINT32 expected[256];
    RGBQUAD colors[2];

    memset(&biDst, 0, sizeof(BITMAPINFO));
    biDst.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    biDst.bmiHeader.biWidth = 16;
    biDst.bmiHeader.biHeight = -16;
    biDst.bmiHeader.biPlanes = 1;
    biDst.bmiHeader.biBitCount = 32;
    biDst.bmiHeader.biCompression = BI_RGB;
    memcpy(&biSrc, &biDst, sizeof(BITMAPINFO));

    hdcScreen = CreateCompatibleDC(NULL);
    hdcDst = CreateCompatibleDC(hdcScreen);
    hdcSrc = CreateCompatibleDC(hdcDst);

    bmpDst = CreateDIBSection(hdcScreen, &biDst, DIB_RGB_COLORS, (void**)&dstBuffer, NULL, 0);
    oldDst = SelectObject(hdcDst, bmpDst);

    bmpSrc = CreateDIBSection(hdcScreen, &biSrc, DIB_RGB_COLORS, (void**)&srcBuffer, NULL, 0);
    oldSrc = SelectObject(hdcSrc, bmpSrc);

    /* Top-down to top-down tests */
    srcBuffer[0] = 0xCAFED00D, srcBuffer[1] = 0xFEEDFACE;
    srcBuffer[16] = 0xFEDCBA98, srcBuffer[17] = 0x76543210;

    memset( expected, 0, get_dib_image_size( &biDst ) );
    expected[0] = 0xCAFED00D, expected[1] = 0xFEEDFACE;
    expected[16] = 0xFEDCBA98, expected[17] = 0x76543210;
    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 2, 2, 0, 0, 2, 2, expected, __LINE__);

    expected[0] = 0xCAFED00D, expected[1] = 0x00000000;
    expected[16] = 0x00000000, expected[17] = 0x00000000;
    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 1, 1, 0, 0, 1, 1, expected, __LINE__);

    expected[0] = 0xCAFED00D, expected[1] = 0xCAFED00D;
    expected[16] = 0xCAFED00D, expected[17] = 0xCAFED00D;
    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 2, 2, 0, 0, 1, 1, expected, __LINE__);

    /* This is an example of the dst width (height) == 1 exception, explored below */
    expected[0] = 0xCAFED00D, expected[1] = 0x00000000;
    expected[16] = 0x00000000, expected[17] = 0x00000000;
    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 1, 1, 0, 0, 2, 2, expected, __LINE__);

    expected[0] = 0xCAFED00D, expected[1] = 0x00000000;
    expected[16] = 0x00000000, expected[17] = 0x00000000;
    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             1, 1, -2, -2, 1, 1, -2, -2, expected, __LINE__);

    expected[0] = 0x00000000, expected[1] = 0x00000000;
    expected[16] = 0x00000000, expected[17] = 0xCAFED00D, expected[18] = 0xFEEDFACE;
    expected[32] = 0x00000000, expected[33] = 0xFEDCBA98, expected[34] = 0x76543210;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             1, 1, 2, 2, 0, 0, 2, 2, expected, __LINE__);

    /* when dst width is 1 merge src width - 1 pixels */
    memset( srcBuffer, 0, get_dib_image_size( &biSrc ) );
    srcBuffer[0] = 0x0000ff00, srcBuffer[1] = 0x0000f0f0, srcBuffer[2] = 0x0000cccc, srcBuffer[3] = 0x0000aaaa;
    srcBuffer[16] = 0xFEDCBA98, srcBuffer[17] = 0x76543210;

    memset( expected, 0, get_dib_image_size( &biDst ) );
    expected[0] = srcBuffer[0];
    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 1, 1, 0, 0, 2, 1, expected, __LINE__);

    /* similarly in the vertical direction */
    memset( expected, 0, get_dib_image_size( &biDst ) );
    expected[0] = srcBuffer[0];
    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 1, 1, 0, 0, 1, 2, expected, __LINE__);

    /* check that it's the dst size in device units that needs to be 1 */
    SetMapMode( hdcDst, MM_ISOTROPIC );
    SetWindowExtEx( hdcDst, 200, 200, NULL );
    SetViewportExtEx( hdcDst, 100, 100, NULL );

    SetMapMode( hdcDst, MM_TEXT );

    SelectObject(hdcDst, oldDst);
    DeleteObject(bmpDst);

    /* Top-down to bottom-up tests */
    memset( srcBuffer, 0, get_dib_image_size( &biSrc ) );
    srcBuffer[0] = 0xCAFED00D, srcBuffer[1] = 0xFEEDFACE;
    srcBuffer[16] = 0xFEDCBA98, srcBuffer[17] = 0x76543210;

    biDst.bmiHeader.biHeight = 16;
    bmpDst = CreateDIBSection(hdcScreen, &biDst, DIB_RGB_COLORS, (void**)&dstBuffer, NULL, 0);
    oldDst = SelectObject(hdcDst, bmpDst);

    memset( expected, 0, get_dib_image_size( &biDst ) );

    expected[224] = 0xFEDCBA98, expected[225] = 0x76543210;
    expected[240] = 0xCAFED00D, expected[241] = 0xFEEDFACE;
    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 2, 2, 0, 0, 2, 2, expected, __LINE__);

    SelectObject(hdcSrc, oldSrc);
    DeleteObject(bmpSrc);

    /* Bottom-up to bottom-up tests */
    biSrc.bmiHeader.biHeight = 16;
    bmpSrc = CreateDIBSection(hdcScreen, &biSrc, DIB_RGB_COLORS, (void**)&srcBuffer, NULL, 0);
    srcBuffer[224] = 0xCAFED00D, srcBuffer[225] = 0xFEEDFACE;
    srcBuffer[240] = 0xFEDCBA98, srcBuffer[241] = 0x76543210;
    oldSrc = SelectObject(hdcSrc, bmpSrc);

    memset( expected, 0, get_dib_image_size( &biDst ) );

    expected[224] = 0xCAFED00D, expected[225] = 0xFEEDFACE;
    expected[240] = 0xFEDCBA98, expected[241] = 0x76543210;
    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 2, 2, 0, 0, 2, 2, expected, __LINE__);

    SelectObject(hdcDst, oldDst);
    DeleteObject(bmpDst);

    /* Bottom-up to top-down tests */
    biDst.bmiHeader.biHeight = -16;
    bmpDst = CreateDIBSection(hdcScreen, &biDst, DIB_RGB_COLORS, (void**)&dstBuffer, NULL, 0);
    oldDst = SelectObject(hdcDst, bmpDst);

    memset( expected, 0, get_dib_image_size( &biDst ) );
    expected[0] = 0xFEDCBA98, expected[1] = 0x76543210;
    expected[16] = 0xCAFED00D, expected[17] = 0xFEEDFACE;
    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 2, 2, 0, 0, 2, 2, expected, __LINE__);

    SelectObject(hdcSrc, oldSrc);
    DeleteObject(bmpSrc);

    biSrc.bmiHeader.biHeight = -2;
    biSrc.bmiHeader.biBitCount = 24;
    bmpSrc = CreateDIBSection(hdcScreen, &biSrc, DIB_RGB_COLORS, (void**)&srcBuffer, NULL, 0);
    oldSrc = SelectObject(hdcSrc, bmpSrc);

    memset( expected, 0, get_dib_image_size( &biDst ) );
    expected[0] = 0xFEEDFACE, expected[1] = 0xCAFED00D;
    expected[2] = 0x76543210, expected[3] = 0xFEDCBA98;
    memcpy(dstBuffer, expected, 4 * sizeof(*dstBuffer));
    StretchBlt(hdcSrc, 0, 0, 4, 1, hdcDst, 0, 0, 4, 1, SRCCOPY );
    memset(dstBuffer, 0x55, 4 * sizeof(*dstBuffer));
    StretchBlt(hdcDst, 0, 0, 4, 1, hdcSrc, 0, 0, 4, 1, SRCCOPY );
    expected[0] = 0x00EDFACE, expected[1] = 0x00FED00D;
    expected[2] = 0x00543210, expected[3] = 0x00DCBA98;
    ok(!memcmp(dstBuffer, expected, 16),
       "StretchBlt expected { %08X, %08X, %08X, %08X } got { %08X, %08X, %08X, %08X }\n",
        expected[0], expected[1], expected[2], expected[3],
        dstBuffer[0], dstBuffer[1], dstBuffer[2], dstBuffer[3] );

    expected[0] = 0xFEEDFACE, expected[1] = 0xCAFED00D;
    expected[2] = 0x76543210, expected[3] = 0xFEDCBA98;
    memcpy(srcBuffer, expected, 4 * sizeof(*dstBuffer));
    memset(dstBuffer, 0x55, 4 * sizeof(*dstBuffer));
    StretchBlt(hdcDst, 0, 0, 4, 1, hdcSrc, 0, 0, 4, 1, SRCCOPY );
    expected[0] = 0x00EDFACE, expected[1] = 0x00D00DFE;
    expected[2] = 0x0010CAFE, expected[3] = 0x00765432;
    ok(!memcmp(dstBuffer, expected, 16),
       "StretchBlt expected { %08X, %08X, %08X, %08X } got { %08X, %08X, %08X, %08X }\n",
        expected[0], expected[1], expected[2], expected[3],
        dstBuffer[0], dstBuffer[1], dstBuffer[2], dstBuffer[3] );

    SelectObject(hdcSrc, oldSrc);
    DeleteObject(bmpSrc);

    biSrc.bmiHeader.biBitCount = 1;
    bmpSrc = CreateDIBSection(hdcScreen, &biSrc, DIB_RGB_COLORS, (void**)&srcBuffer, NULL, 0);
    oldSrc = SelectObject(hdcSrc, bmpSrc);
    *((DWORD *)colors + 0) = 0x123456;
    *((DWORD *)colors + 1) = 0x335577;
    SetDIBColorTable( hdcSrc, 0, 2, colors );
    srcBuffer[0] = 0x55555555;
    memset(dstBuffer, 0xcc, 4 * sizeof(*dstBuffer));
    SetTextColor( hdcDst, 0 );
    SetBkColor( hdcDst, 0 );
    StretchBlt(hdcDst, 0, 0, 4, 1, hdcSrc, 0, 0, 4, 1, SRCCOPY );
    expected[0] = expected[2] = 0x00123456;
    expected[1] = expected[3] = 0x00335577;
    ok(!memcmp(dstBuffer, expected, 16),
       "StretchBlt expected { %08X, %08X, %08X, %08X } got { %08X, %08X, %08X, %08X }\n",
        expected[0], expected[1], expected[2], expected[3],
        dstBuffer[0], dstBuffer[1], dstBuffer[2], dstBuffer[3] );

    SelectObject(hdcSrc, oldSrc);
    DeleteObject(bmpSrc);

    bmpSrc = CreateBitmap( 16, 16, 1, 1, 0 );
    oldSrc = SelectObject(hdcSrc, bmpSrc);
    SetPixel( hdcSrc, 0, 0, 0 );
    SetPixel( hdcSrc, 1, 0, 0xffffff );
    SetPixel( hdcSrc, 2, 0, 0xffffff );
    SetPixel( hdcSrc, 3, 0, 0 );
    memset(dstBuffer, 0xcc, 4 * sizeof(*dstBuffer));
    SetTextColor(hdcDst, RGB(0x22, 0x44, 0x66));
    SetBkColor(hdcDst, RGB(0x65, 0x43, 0x21));
    StretchBlt(hdcDst, 0, 0, 4, 1, hdcSrc, 0, 0, 4, 1, SRCCOPY );
    expected[0] = expected[3] = 0x00224466;
    expected[1] = expected[2] = 0x00654321;
    ok(!memcmp(dstBuffer, expected, 16),
       "StretchBlt expected { %08X, %08X, %08X, %08X } got { %08X, %08X, %08X, %08X }\n",
        expected[0], expected[1], expected[2], expected[3],
        dstBuffer[0], dstBuffer[1], dstBuffer[2], dstBuffer[3] );

    SelectObject(hdcSrc, oldSrc);
    DeleteObject(bmpSrc);

    SelectObject(hdcSrc, oldSrc);
    DeleteObject(bmpSrc);
    SelectObject(hdcDst, oldDst);
    DeleteObject(bmpDst);

    memset(&biDst, 0, sizeof(BITMAPINFO));              // Clear our Bitmap to to all zeroes

    biDst.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);  // Set our Size to the header size
    biDst.bmiHeader.biWidth = 2;                        // Set our Width to 2 (left-to-right)
    biDst.bmiHeader.biHeight = -2;                      // Set our Height to -2 that's negative for top-down
    biDst.bmiHeader.biPlanes = 1;                       // Set out planes to 1 (1 required by Windows)
    biDst.bmiHeader.biBitCount = 32;                    // Set out BitCount to 32 (Full Color)
    biDst.bmiHeader.biCompression = BI_RGB;             // Set our Compression to BI_RBG (uncompressed)

    memcpy(&biSrc, &biDst, sizeof(BITMAPINFO));         // Put same Destination params into the Source

    bmpSrc = CreateDIBSection(hdcSrc, &biSrc, DIB_RGB_COLORS, (void**)&srcBuffer, NULL, 0);
    oldSrc = SelectObject(hdcSrc, bmpSrc);
    bmpDst = CreateDIBSection(hdcDst, &biDst, DIB_RGB_COLORS, (void**)&dstBuffer, NULL, 0);
    oldDst = SelectObject(hdcDst, bmpDst);

    srcBuffer[0] = 0x000000FF;   // BLUE
    srcBuffer[1] = 0x0000FF00;   // GREEN
    srcBuffer[2] = 0x00FF0000;   // RED
    srcBuffer[3] = 0xFF000000;   // ALPHA (Opacity)

    expected[0] = 0x000000FF;
    expected[1] = 0x0000FF00;
    expected[2] = 0x00FF0000;
    expected[3] = 0xFF000000;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 2, 2, 0, 0, 2, 2, expected, __LINE__); // Case 1111 (15) - No flip. Just copy.

    expected[0] = 0x00FF0000;
    expected[1] = 0xFF000000;
    expected[2] = 0x000000FF;
    expected[3] = 0x0000FF00;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 2, 2, 0, 1, 2, -2, expected, __LINE__); // Case 1110 (14) - Vertical flip.

    expected[0] = 0x0000FF00;
    expected[1] = 0x000000FF;
    expected[2] = 0xFF000000;
    expected[3] = 0x00FF0000;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 2, 2, 1, 0, -2, 2, expected, __LINE__); // Case 1101 (13) - Horizontal flip.

    expected[0] = 0xFF000000;
    expected[1] = 0x00FF0000;
    expected[2] = 0x0000FF00;
    expected[3] = 0x000000FF;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 0, 2, 2, 1, 1, -2, -2, expected, __LINE__); // Case 1100 (12) - Both flip.

    expected[0] = 0x00FF0000;
    expected[1] = 0xFF000000;
    expected[2] = 0x000000FF;
    expected[3] = 0x0000FF00;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 1, 2, -2, 0, 0, 2, 2, expected, __LINE__); // Case 1011 (11) - Vertical Flip.

    expected[0] = 0x000000FF;
    expected[1] = 0x0000FF00;
    expected[2] = 0x00FF0000;
    expected[3] = 0xFF000000;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 2, 2, -2, 0, 2, 2, -2, expected, __LINE__); // Case 1010 (10) - No Flip. Special Case.

    expected[0] = 0xFF000000;
    expected[1] = 0x00FF0000;
    expected[2] = 0x0000FF00;
    expected[3] = 0x000000FF;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 1, 2, -2, 1, 0, -2, 2, expected, __LINE__); // Case 1001 (9) - Both Flip.

    expected[0] = 0x0000FF00;
    expected[1] = 0x000000FF;
    expected[2] = 0xFF000000;
    expected[3] = 0x00FF0000;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             0, 1, 2, -2, 1, 1, -2, -2, expected, __LINE__); // Case 1000 (8) - Horizontal Flip.

    expected[0] = 0x0000FF00;
    expected[1] = 0x000000FF;
    expected[2] = 0xFF000000;
    expected[3] = 0x00FF0000;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             1, 0, -2, 2, 0, 0, 2, 2, expected, __LINE__); // Case 0111 (7) - Horizontal Flip.

    expected[0] = 0xFF000000;
    expected[1] = 0x00FF0000;
    expected[2] = 0x0000FF00;
    expected[3] = 0x000000FF;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             1, 0, -2, 2, 0, 1, 2, -2, expected, __LINE__); // Case 0110 (6) - Both Flip.

    expected[0] = 0x000000FF;
    expected[1] = 0x0000FF00;
    expected[2] = 0x00FF0000;
    expected[3] = 0xFF000000;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             2, 0, -2, 2, 2, 0, -2, 2, expected, __LINE__); // Case 0101 (5) - No Flip. Special Case.

    expected[0] = 0x00FF0000;
    expected[1] = 0xFF000000;
    expected[2] = 0x000000FF;
    expected[3] = 0x0000FF00;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             1, 0, -2, 2, 1, 1, -2, -2, expected, __LINE__); // Case 0100 (4) - Vertical Flip.

    expected[0] = 0xFF000000;
    expected[1] = 0x00FF0000;
    expected[2] = 0x0000FF00;
    expected[3] = 0x000000FF;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             1, 1, -2, -2, 0, 0, 2, 2, expected, __LINE__); // Case 0011 (3) - Both Flip.

    expected[0] = 0x0000FF00;
    expected[1] = 0x000000FF;
    expected[2] = 0xFF000000;
    expected[3] = 0x00FF0000;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             1, 1, -2, -2, 0, 1, 2, -2, expected, __LINE__); // Case 0010 (2) - Horizontal Flip.

    expected[0] = 0x00FF0000;
    expected[1] = 0xFF000000;
    expected[2] = 0x000000FF;
    expected[3] = 0x0000FF00;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             1, 1, -2, -2, 1, 0, -2, 2, expected, __LINE__); // Case 0001 (1) - Vertical Flip.

    expected[0] = 0x000000FF;
    expected[1] = 0x0000FF00;
    expected[2] = 0x00FF0000;
    expected[3] = 0xFF000000;

    check_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                             2, 2, -2, -2, 2, 2, -2, -2, expected, __LINE__); // Case 0000 (0) - No Flip. Special Case.

    DeleteDC(hdcSrc);

    SelectObject(hdcDst, oldDst);
    DeleteObject(bmpDst);
    DeleteDC(hdcDst);

    DeleteDC(hdcScreen);
}

static void test_StretchBlt_TopDownOptions(BOOL SrcTopDown, BOOL DstTopDown)
{
    HBITMAP bmpDst, bmpSrc;
    HBITMAP oldDst, oldSrc;
    HDC hdcScreen, hdcDst, hdcSrc;
    UINT32 *dstBuffer, *srcBuffer;
    BITMAPINFO biDst, biSrc;
    UINT32 expected[256];

    memset(&biDst, 0, sizeof(BITMAPINFO));

    biDst.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    biDst.bmiHeader.biWidth = 2;
    biDst.bmiHeader.biHeight = 2;
    biDst.bmiHeader.biPlanes = 1;
    biDst.bmiHeader.biBitCount = 32;
    biDst.bmiHeader.biCompression = BI_RGB;
    memcpy(&biSrc, &biDst, sizeof(BITMAPINFO));

    if (!SrcTopDown && !DstTopDown)
    {
        biSrc.bmiHeader.biHeight = -2;                // Converts Source bitmap to top down
        biDst.bmiHeader.biHeight = -2;                // Converts Destination bitmap to top down
    }

    if (SrcTopDown)
    {
        biSrc.bmiHeader.biHeight = -2;                // Converts Source bitmap to top down
    }

    if (DstTopDown)
    {
        biDst.bmiHeader.biHeight = -2;                // Converts Destination bitmap to top down
    }

    hdcScreen = CreateCompatibleDC(NULL);
    hdcDst = CreateCompatibleDC(hdcScreen);
    hdcSrc = CreateCompatibleDC(hdcDst);

    bmpSrc = CreateDIBSection(hdcScreen, &biSrc, DIB_RGB_COLORS, (void**)&srcBuffer, NULL, 0);
    oldSrc = SelectObject(hdcSrc, bmpSrc);
    bmpDst = CreateDIBSection(hdcScreen, &biDst, DIB_RGB_COLORS, (void**)&dstBuffer, NULL, 0);
    oldDst = SelectObject(hdcDst, bmpDst);

    srcBuffer[0] = 0x000000FF;   // BLUE
    srcBuffer[1] = 0x0000FF00;   // GREEN
    srcBuffer[2] = 0x00FF0000;   // RED
    srcBuffer[3] = 0xFF000000;   // ALPHA (Opacity)

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x00FF0000;
        expected[1] = 0xFF000000;
        expected[2] = 0x000000FF;
        expected[3] = 0x0000FF00;
    }
    else
    {
        expected[0] = 0x000000FF;
        expected[1] = 0x0000FF00;
        expected[2] = 0x00FF0000;
        expected[3] = 0xFF000000;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            0, 0, 2, 2, 0, 0, 2, 2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 1111 (15) - No flip. Just copy.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x000000FF;
        expected[1] = 0x0000FF00;
        expected[2] = 0x00FF0000;
        expected[3] = 0xFF000000;
    }
    else
    {
        expected[0] = 0x00FF0000;
        expected[1] = 0xFF000000;
        expected[2] = 0x000000FF;
        expected[3] = 0x0000FF00;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            0, 0, 2, 2, 0, 1, 2, -2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 1110 (14) - Vertical flip.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0xFF000000;
        expected[1] = 0x00FF0000;
        expected[2] = 0x0000FF00;
        expected[3] = 0x000000FF;
    }
    else
    {
        expected[0] = 0x0000FF00;
        expected[1] = 0x000000FF;
        expected[2] = 0xFF000000;
        expected[3] = 0x00FF0000;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            0, 0, 2, 2, 1, 0, -2, 2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 1101 (13) - Horizontal flip.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x0000FF00;
        expected[1] = 0x000000FF;
        expected[2] = 0xFF000000;
        expected[3] = 0x00FF0000;
    }
    else
    {
        expected[0] = 0xFF000000;
        expected[1] = 0x00FF0000;
        expected[2] = 0x0000FF00;
        expected[3] = 0x000000FF;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            0, 0, 2, 2, 1, 1, -2, -2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 1100 (12) - Both flip.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x000000FF;
        expected[1] = 0x0000FF00;
        expected[2] = 0x00FF0000;
        expected[3] = 0xFF000000;
    }
    else
    {
        expected[0] = 0x00FF0000;
        expected[1] = 0xFF000000;
        expected[2] = 0x000000FF;
        expected[3] = 0x0000FF00;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            0, 1, 2, -2, 0, 0, 2, 2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 1011 (11) - Vertical Flip.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x00FF0000;
        expected[1] = 0xFF000000;
        expected[2] = 0x000000FF;
        expected[3] = 0x0000FF00;
    }
    else
    {
        expected[0] = 0x000000FF;
        expected[1] = 0x0000FF00;
        expected[2] = 0x00FF0000;
        expected[3] = 0xFF000000;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            0, 2, 2, -2, 0, 2, 2, -2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 1010 (10) - No Flip. Special Case.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x0000FF00;
        expected[1] = 0x000000FF;
        expected[2] = 0xFF000000;
        expected[3] = 0x00FF0000;
    }
    else
    {
        expected[0] = 0xFF000000;
        expected[1] = 0x00FF0000;
        expected[2] = 0x0000FF00;
        expected[3] = 0x000000FF;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            0, 1, 2, -2, 1, 0, -2, 2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 1001 (9) - Both Flip.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0xFF000000;
        expected[1] = 0x00FF0000;
        expected[2] = 0x0000FF00;
        expected[3] = 0x000000FF;
    }
    else
    {
        expected[0] = 0x0000FF00;
        expected[1] = 0x000000FF;
        expected[2] = 0xFF000000;
        expected[3] = 0x00FF0000;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            0, 1, 2, -2, 1, 1, -2, -2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 1000 (8) - Horizontal Flip

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0xFF000000;
        expected[1] = 0x00FF0000;
        expected[2] = 0x0000FF00;
        expected[3] = 0x000000FF;
    }
    else
    {
        expected[0] = 0x0000FF00;
        expected[1] = 0x000000FF;
        expected[2] = 0xFF000000;
        expected[3] = 0x00FF0000;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            1, 0, -2, 2, 0, 0, 2, 2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 0111 (7) - Horizontal Flip

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x0000FF00;
        expected[1] = 0x000000FF;
        expected[2] = 0xFF000000;
        expected[3] = 0x00FF0000;
    }
    else
    {
        expected[0] = 0xFF000000;
        expected[1] = 0x00FF0000;
        expected[2] = 0x0000FF00;
        expected[3] = 0x000000FF;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            1, 0, -2, 2, 0, 1, 2, -2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 0110 (6) - Both Flip.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x00FF0000;
        expected[1] = 0xFF000000;
        expected[2] = 0x000000FF;
        expected[3] = 0x0000FF00;
    }
    else
    {
        expected[0] = 0x000000FF;
        expected[1] = 0x0000FF00;
        expected[2] = 0x00FF0000;
        expected[3] = 0xFF000000;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            2, 0, -2, 2, 2, 0, -2, 2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 0101 (5) - No Flip. Special Case.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x000000FF;
        expected[1] = 0x0000FF00;
        expected[2] = 0x00FF0000;
        expected[3] = 0xFF000000;
    }
    else
    {
        expected[0] = 0x00FF0000;
        expected[1] = 0xFF000000;
        expected[2] = 0x000000FF;
        expected[3] = 0x0000FF00;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            1, 0, -2, 2, 1, 1, -2, -2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 0100 (4) - Vertical Flip.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x0000FF00;
        expected[1] = 0x000000FF;
        expected[2] = 0xFF000000;
        expected[3] = 0x00FF0000;
    }
    else
    {
        expected[0] = 0xFF000000;
        expected[1] = 0x00FF0000;
        expected[2] = 0x0000FF00;
        expected[3] = 0x000000FF;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            1, 1, -2, -2, 0, 0, 2, 2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 0011 (3) - Both Flip.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0xFF000000;
        expected[1] = 0x00FF0000;
        expected[2] = 0x0000FF00;
        expected[3] = 0x000000FF;
    }
    else
    {
        expected[0] = 0x0000FF00;
        expected[1] = 0x000000FF;
        expected[2] = 0xFF000000;
        expected[3] = 0x00FF0000;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            1, 1, -2, -2, 0, 1, 2, -2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 0010 (2) - Horizontal Flip.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x000000FF;
        expected[1] = 0x0000FF00;
        expected[2] = 0x00FF0000;
        expected[3] = 0xFF000000;
    }
    else
    {
        expected[0] = 0x00FF0000;
        expected[1] = 0xFF000000;
        expected[2] = 0x000000FF;
        expected[3] = 0x0000FF00;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            1, 1, -2, -2, 1, 0, -2, 2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 0001 (1) - Vertical Flip.

    if ((SrcTopDown || DstTopDown) && !(SrcTopDown && DstTopDown))
    {
        expected[0] = 0x00FF0000;
        expected[1] = 0xFF000000;
        expected[2] = 0x000000FF;
        expected[3] = 0x0000FF00;

    }
    else
    {
        expected[0] = 0x000000FF;
        expected[1] = 0x0000FF00;
        expected[2] = 0x00FF0000;
        expected[3] = 0xFF000000;
    }

    test_StretchBlt_stretch(hdcDst, hdcSrc, &biDst, dstBuffer, srcBuffer,
                            2, 2, -2, -2, 2, 2, -2, -2, expected, __LINE__, SrcTopDown, DstTopDown); // Case 0000 (0) - No Flip. Special Case.

    SelectObject(hdcSrc, oldSrc);
    DeleteObject(bmpSrc);
    DeleteDC(hdcSrc);

    SelectObject(hdcDst, oldDst);
    DeleteObject(bmpDst);
    DeleteDC(hdcDst);

    DeleteDC(hdcScreen);
}

START_TEST(StretchBlt)
{
    test_StretchBlt_halftone();

    trace("\n\n## Start of generalized StretchBlt tests.\n\n");
    test_StretchBlt();

    trace("\n\n## Start of source top-down and destination top-down tests.\n\n");
    test_StretchBlt_TopDownOptions(TRUE, TRUE);

    trace("\n\n## Start of source top-down and destination bottom-up tests.\n\n");
    test_StretchBlt_TopDownOptions(TRUE, FALSE);

    trace("\n\n## Start of source bottom-up and destination top-down tests.\n\n");
    test_StretchBlt_TopDownOptions(FALSE, TRUE);

    trace("\n\n## Start of source bottom-up and destination bottom-up tests.\n\n");
    test_StretchBlt_TopDownOptions(FALSE, FALSE);
}
