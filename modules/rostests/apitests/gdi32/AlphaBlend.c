/*
 * PROJECT:     ReactOS API tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AlphaBlend pixel-center sampling and clipping regression tests
 */

#include <apitest.h>
#include <wingdi.h>
#include <winuser.h>

typedef struct
{
    HDC dc;
    HBITMAP bitmap, previous;
    BYTE *bits;
    UINT stride, bpp;
} BLEND_SURFACE;

static BOOL
CreateBlendSurface(BLEND_SURFACE *surface, UINT size, UINT bpp, BOOL rgb565)
{
    struct { BITMAPINFOHEADER header; RGBQUAD colors[256]; } info = { 0 };
    UINT i;

    ZeroMemory(surface, sizeof(*surface));
    info.header.biSize = sizeof(info.header);
    info.header.biWidth = size;
    info.header.biHeight = -(LONG)size;
    info.header.biPlanes = 1;
    info.header.biBitCount = bpp;
    if (rgb565)
    {
        DWORD *masks = (DWORD *)info.colors;
        info.header.biCompression = BI_BITFIELDS;
        masks[0] = 0xf800;
        masks[1] = 0x07e0;
        masks[2] = 0x001f;
    }
    else if (bpp == 8)
    {
        for (i = 0; i < 256; ++i)
            info.colors[i].rgbRed = info.colors[i].rgbGreen = info.colors[i].rgbBlue = i == 255 ? 255 : 0;
    }
    surface->bpp = bpp;
    surface->stride = ((size * bpp + 31) / 32) * 4;
    surface->dc = CreateCompatibleDC(NULL);
    surface->bitmap = CreateDIBSection(surface->dc, (BITMAPINFO *)&info, DIB_RGB_COLORS, (void **)&surface->bits, NULL, 0);
    if (!surface->dc || !surface->bitmap)
    {
        DeleteObject(surface->bitmap);
        DeleteDC(surface->dc);
        return FALSE;
    }
    surface->previous = SelectObject(surface->dc, surface->bitmap);
    return TRUE;
}

static void
DeleteBlendSurface(BLEND_SURFACE *surface)
{
    SelectObject(surface->dc, surface->previous);
    DeleteObject(surface->bitmap);
    DeleteDC(surface->dc);
}

static void
TestBlendScaling(UINT bpp, BOOL rgb565)
{
    static const struct { int sx, sy, sw, sh, dx, dy, dw, dh; } cases[] =
    {
        { 0, 0, 32, 32, 0, 0, 32, 32 },
        { 0, 0, 32, 32, 0, 0, 16, 16 },
        { 0, 0, 32, 32, 0, 0, 48, 48 },
        { 0, 0, 32, 32, 0, 0, 13, 31 },
        { 3, 5, 27, 21, 2, 3, 43, 37 },
        { 3, 5, 27, 21, -3, -2, 43, 37 },
        { 0, 0, 32, 32, 0, 0, 1, 1 }
    };
    BLEND_SURFACE source, actual, expected;
    BLENDFUNCTION blend = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    UINT t, clip, mode, x, y, mismatches, bytes = min(bpp / 8, 3);
    DWORD *pixels, white = 0xffffffff;
    BOOL ret;

    if (!CreateBlendSurface(&source, 32, 32, FALSE)) { ok(0, "source allocation failed\n"); return; }
    if (!CreateBlendSurface(&actual, 64, bpp, rgb565)) { ok(0, "actual allocation failed\n"); goto free_source; }
    if (!CreateBlendSurface(&expected, 64, bpp, rgb565)) { ok(0, "expected allocation failed\n"); goto free_actual; }

    pixels = (DWORD *)source.bits;
    for (y = 0; y < 32; ++y)
        for (x = 0; x < 32; ++x)
        {
            UINT alpha = 64 + ((x + y) % 4) * 63;
            pixels[y * 32 + x] = (alpha << 24) | (((x * 7 * alpha + 127) / 255) << 16) |
                                (((y * 7 * alpha + 127) / 255) << 8) | ((x + y) * 3 * alpha + 127) / 255;
            /* On palette targets isolate coordinate selection from the
               separate native scaled/unscaled color-quantization paths. */
            if (bpp == 8)
                pixels[y * 32 + x] = (x % 4 == 0 || y % 4 == 1 || x == y) ? 0xffffffff : 0xff000000;
        }

    for (t = 0; t < ARRAY_SIZE(cases); ++t)
    for (clip = 0; clip < 3; ++clip)
    for (mode = 0; mode < 3; ++mode)
    {
        HRGN region = NULL, hole = NULL;
        const int dx = cases[t].dx, dy = cases[t].dy, dw = cases[t].dw, dh = cases[t].dh;
        PatBlt(actual.dc, 0, 0, 64, 64, WHITENESS);
        PatBlt(expected.dc, 0, 0, 64, 64, WHITENESS);
        if (clip)
        {
            region = CreateRectRgn(5, 4, 39, 35);
            if (clip == 2)
            {
                hole = CreateRectRgn(11, 10, 23, 19);
                CombineRgn(region, region, hole, RGN_DIFF);
            }
            SelectClipRgn(actual.dc, region);
        }
        blend.AlphaFormat = mode == 2 ? 0 : AC_SRC_ALPHA;
        blend.SourceConstantAlpha = mode == 0 ? 255 : 137;
        ret = GdiAlphaBlend(actual.dc, dx, dy, dw, dh, source.dc, cases[t].sx, cases[t].sy, cases[t].sw, cases[t].sh, blend);
        ok(ret, "scaled blend failed, error %lu\n", GetLastError());
        /* Compose at source resolution first, then sample in the test.
           No scaled GDI operation is used to generate the expected pixels. */
        ret = GdiAlphaBlend(expected.dc, 0, 0, cases[t].sw, cases[t].sh, source.dc, cases[t].sx, cases[t].sy, cases[t].sw, cases[t].sh, blend);
        ok(ret, "reference blend failed, error %lu\n", GetLastError());
        GdiFlush();
        mismatches = 0;
        for (y = 0; y < 64; ++y)
            for (x = 0; x < 64; ++x)
            {
                UINT offset = y * actual.stride + x * bpp / 8;
                const BYTE *pixel = (const BYTE *)&white;
                if ((int)x >= dx && (int)x < dx + dw && (int)y >= dy && (int)y < dy + dh && (!region || PtInRegion(region, x, y)))
                {
                    UINT sx = ((2 * ((int)x - dx) + 1) * cases[t].sw) / (2 * dw);
                    UINT sy = ((2 * ((int)y - dy) + 1) * cases[t].sh) / (2 * dh);
                    pixel = expected.bits + sy * expected.stride + sx * bpp / 8;
                }
                if (memcmp(actual.bits + offset, pixel, bytes))
                    ++mismatches;
            }
        ok(!mismatches, "%u bpp/565=%u case %u clip %u mode %u: %u differing pixels\n", bpp, rgb565, t, clip, mode, mismatches);
        SelectClipRgn(actual.dc, NULL);
        DeleteObject(hole);
        DeleteObject(region);
    }
    DeleteBlendSurface(&expected);
free_actual:
    DeleteBlendSurface(&actual);
free_source:
    DeleteBlendSurface(&source);
}

START_TEST(AlphaBlend)
{
    TestBlendScaling(8, FALSE);
    TestBlendScaling(16, FALSE);
    TestBlendScaling(16, TRUE);
    TestBlendScaling(24, FALSE);
    TestBlendScaling(32, FALSE);
}
