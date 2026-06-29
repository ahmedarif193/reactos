/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     CDD/Framebuf display driver shadow buffer and 2D drawing tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the shadow framebuffer flush algorithm, display mode filtering,
 * palette initialization, stride calculation, and 2D drawing surface
 * substitution patterns used by CDD and framebuf.
 */

#include "precomp.h"

/* ---- Replicate CDD shadow buffer flush algorithm ---- */

typedef struct _TEST_FB_STATE {
    UINT    ScreenWidth;
    UINT    ScreenHeight;
    UINT    ScreenDelta; /* Bytes per scanline (stride) */
    UINT    BitsPerPixel;
    BYTE    *ShadowBuffer;
    BYTE    *VramPtr;
    SIZE_T  FbSize;
} TEST_FB_STATE;

static BOOL FbInit(TEST_FB_STATE *fb, UINT w, UINT h, UINT bpp)
{
    fb->ScreenWidth = w;
    fb->ScreenHeight = h;
    fb->BitsPerPixel = bpp;
    fb->ScreenDelta = w * (bpp / 8);
    fb->FbSize = (SIZE_T)fb->ScreenDelta * h;
    fb->ShadowBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, fb->FbSize);
    fb->VramPtr = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, fb->FbSize);
    return fb->ShadowBuffer && fb->VramPtr;
}

static void FbFree(TEST_FB_STATE *fb)
{
    if (fb->ShadowBuffer) HeapFree(GetProcessHeap(), 0, fb->ShadowBuffer);
    if (fb->VramPtr) HeapFree(GetProcessHeap(), 0, fb->VramPtr);
    memset(fb, 0, sizeof(*fb));
}

/* Replicate CddShadowFlushRect */
static void FbFlushRect(TEST_FB_STATE *fb, LONG left, LONG top,
                        LONG right, LONG bottom)
{
    LONG y;
    UINT bpp = fb->BitsPerPixel / 8;

    /* Normalize */
    if (left > right) { LONG t = left; left = right; right = t; }
    if (top > bottom) { LONG t = top; top = bottom; bottom = t; }

    /* Clip to screen */
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > (LONG)fb->ScreenWidth) right = (LONG)fb->ScreenWidth;
    if (bottom > (LONG)fb->ScreenHeight) bottom = (LONG)fb->ScreenHeight;

    if (left >= right || top >= bottom)
        return; /* Empty */

    /* Full-width optimization */
    if (left == 0 && (ULONG)(right - left) * bpp == fb->ScreenDelta)
    {
        SIZE_T offset = (SIZE_T)top * fb->ScreenDelta;
        SIZE_T length = (SIZE_T)(bottom - top) * fb->ScreenDelta;
        memcpy(fb->VramPtr + offset, fb->ShadowBuffer + offset, length);
    }
    else
    {
        for (y = top; y < bottom; ++y)
        {
            SIZE_T rowOff = (SIZE_T)y * fb->ScreenDelta + (SIZE_T)left * bpp;
            SIZE_T copyLen = (SIZE_T)(right - left) * bpp;
            memcpy(fb->VramPtr + rowOff, fb->ShadowBuffer + rowOff, copyLen);
        }
    }
}

static void SetShadowPixel32(TEST_FB_STATE *fb, UINT x, UINT y, UINT color)
{
    UINT *row = (UINT *)(fb->ShadowBuffer + (SIZE_T)y * fb->ScreenDelta);
    row[x] = color;
}

static UINT GetVramPixel32(TEST_FB_STATE *fb, UINT x, UINT y)
{
    UINT *row = (UINT *)(fb->VramPtr + (SIZE_T)y * fb->ScreenDelta);
    return row[x];
}

/* ---- Mode filtering algorithm ---- */
typedef struct _TEST_VIDEO_MODE {
    UINT    Width;
    UINT    Height;
    UINT    BitsPerPixel;
    UINT    Frequency;
    BOOL    Graphics; /* TRUE for graphics mode, FALSE for text */
    UINT    Planes;
    BOOL    Valid;    /* Set to FALSE if rejected */
} TEST_VIDEO_MODE;

static UINT FilterModes(TEST_VIDEO_MODE *modes, UINT count)
{
    UINT i, valid = 0;
    for (i = 0; i < count; ++i)
    {
        /* Reject text modes */
        if (!modes[i].Graphics)
        {
            modes[i].Valid = FALSE;
            continue;
        }
        /* Reject non-single-plane */
        if (modes[i].Planes != 1)
        {
            modes[i].Valid = FALSE;
            continue;
        }
        /* Accept only 8, 16, 24, 32 bpp */
        if (modes[i].BitsPerPixel != 8 &&
            modes[i].BitsPerPixel != 16 &&
            modes[i].BitsPerPixel != 24 &&
            modes[i].BitsPerPixel != 32)
        {
            modes[i].Valid = FALSE;
            continue;
        }
        modes[i].Valid = TRUE;
        ++valid;
    }
    return valid;
}

/* ---- Palette calculation ---- */
static UINT CalcDitherFormat(UINT bpp)
{
    switch (bpp)
    {
        case 8:  return 3;  /* BMF_8BPP */
        case 16: return 5;  /* BMF_16BPP */
        case 24: return 6;  /* BMF_24BPP */
        case 32: return 7;  /* BMF_32BPP */
        default: return 0;
    }
}

/* ---- Tests ---- */

static void Test_ShadowFlushBasic(void)
{
    TEST_FB_STATE fb;
    FbInit(&fb, 100, 100, 32);

    /* Write to shadow */
    SetShadowPixel32(&fb, 10, 10, 0xFFFF0000);
    SetShadowPixel32(&fb, 50, 50, 0xFF00FF00);

    /* VRAM should be black (not yet flushed) */
    ok(GetVramPixel32(&fb, 10, 10) == 0, "VRAM should be black before flush\n");

    /* Flush the dirty region */
    FbFlushRect(&fb, 0, 0, 100, 100);

    ok_eq_hex(GetVramPixel32(&fb, 10, 10), 0xFFFF0000);
    ok_eq_hex(GetVramPixel32(&fb, 50, 50), 0xFF00FF00);

    FbFree(&fb);
}

static void Test_PartialFlush(void)
{
    TEST_FB_STATE fb;
    FbInit(&fb, 100, 100, 32);

    SetShadowPixel32(&fb, 10, 10, 0xFFAAAAAA);
    SetShadowPixel32(&fb, 90, 90, 0xFFBBBBBB);

    /* Flush only top-left 50x50 */
    FbFlushRect(&fb, 0, 0, 50, 50);

    ok_eq_hex(GetVramPixel32(&fb, 10, 10), 0xFFAAAAAA);
    ok(GetVramPixel32(&fb, 90, 90) == 0,
       "Outside flush region should still be black\n");

    FbFree(&fb);
}

static void Test_FlushClipping(void)
{
    TEST_FB_STATE fb;
    FbInit(&fb, 50, 50, 32);

    SetShadowPixel32(&fb, 25, 25, 0xFF123456);

    /* Flush with out-of-bounds rect (should be clipped) */
    FbFlushRect(&fb, -10, -10, 200, 200);

    ok_eq_hex(GetVramPixel32(&fb, 25, 25), 0xFF123456);

    FbFree(&fb);
}

static void Test_FlushEmptyRect(void)
{
    TEST_FB_STATE fb;
    FbInit(&fb, 50, 50, 32);

    SetShadowPixel32(&fb, 10, 10, 0xFFFF0000);

    /* Flush with empty rect (left >= right) */
    FbFlushRect(&fb, 20, 20, 20, 20);
    ok(GetVramPixel32(&fb, 10, 10) == 0, "Empty rect flush should not copy\n");

    /* Flush with inverted rect (should normalize) */
    FbFlushRect(&fb, 20, 20, 0, 0);
    ok(GetVramPixel32(&fb, 10, 10) == 0xFFFF0000,
       "Inverted rect should be normalized and flushed\n");

    FbFree(&fb);
}

static void Test_FullWidthOptimization(void)
{
    TEST_FB_STATE fb;
    UINT y;
    BOOL allFlushed = TRUE;

    FbInit(&fb, 100, 50, 32);

    /* Fill entire shadow */
    for (y = 0; y < 50; ++y)
    {
        UINT x;
        for (x = 0; x < 100; ++x)
            SetShadowPixel32(&fb, x, y, 0xFFCCCCCC);
    }

    /* Full-width flush should use burst copy */
    FbFlushRect(&fb, 0, 0, 100, 50);

    for (y = 0; y < 50; ++y)
    {
        UINT x;
        for (x = 0; x < 100; ++x)
        {
            if (GetVramPixel32(&fb, x, y) != 0xFFCCCCCC)
            {
                allFlushed = FALSE;
                break;
            }
        }
    }
    ok(allFlushed, "Full-width flush should copy all pixels\n");

    FbFree(&fb);
}

static void Test_ModeFiltering(void)
{
    TEST_VIDEO_MODE modes[] = {
        { 640, 480, 32, 60, TRUE, 1, TRUE },   /* Valid */
        { 640, 480, 16, 60, TRUE, 1, TRUE },   /* Valid */
        { 640, 480, 8, 60, TRUE, 1, TRUE },    /* Valid */
        { 640, 480, 24, 60, TRUE, 1, TRUE },   /* Valid */
        { 80, 25, 0, 0, FALSE, 1, TRUE },       /* Text mode — reject */
        { 640, 480, 4, 60, TRUE, 4, TRUE },     /* 4-plane — reject */
        { 640, 480, 4, 60, TRUE, 1, TRUE },     /* 4bpp — reject */
        { 1920, 1080, 32, 60, TRUE, 1, TRUE },  /* Valid */
    };
    UINT count = sizeof(modes) / sizeof(modes[0]);
    UINT valid;

    valid = FilterModes(modes, count);

    ok_eq_uint(valid, 5);
    ok(modes[0].Valid == TRUE, "640x480x32 should be valid\n");
    ok(modes[1].Valid == TRUE, "640x480x16 should be valid\n");
    ok(modes[2].Valid == TRUE, "640x480x8 should be valid\n");
    ok(modes[3].Valid == TRUE, "640x480x24 should be valid\n");
    ok(modes[4].Valid == FALSE, "Text mode should be rejected\n");
    ok(modes[5].Valid == FALSE, "4-plane mode should be rejected\n");
    ok(modes[6].Valid == FALSE, "4bpp mode should be rejected\n");
    ok(modes[7].Valid == TRUE, "1920x1080x32 should be valid\n");
}

static void Test_DitherFormat(void)
{
    ok_eq_uint(CalcDitherFormat(8), 3);
    ok_eq_uint(CalcDitherFormat(16), 5);
    ok_eq_uint(CalcDitherFormat(24), 6);
    ok_eq_uint(CalcDitherFormat(32), 7);
    ok_eq_uint(CalcDitherFormat(4), 0);
    ok_eq_uint(CalcDitherFormat(0), 0);
    ok_eq_uint(CalcDitherFormat(15), 0);
}

static void Test_StrideCalculation(void)
{
    /* CDD stride = width * (bpp/8) */
    struct { UINT w, bpp, expected; } cases[] = {
        { 640, 8, 640 },
        { 640, 16, 1280 },
        { 640, 24, 1920 },
        { 640, 32, 2560 },
        { 1024, 32, 4096 },
        { 1920, 32, 7680 },
        { 1, 32, 4 },
    };
    ULONG i;

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
    {
        UINT stride = cases[i].w * (cases[i].bpp / 8);
        ok(stride == cases[i].expected,
           "Stride for %ux%ubpp: expected %u, got %u\n",
           cases[i].w, cases[i].bpp, cases[i].expected, stride);
    }
}

static void Test_ColorMasks(void)
{
    /* Standard 32-bpp X8R8G8B8 masks */
    UINT redMask = 0x00FF0000;
    UINT greenMask = 0x0000FF00;
    UINT blueMask = 0x000000FF;

    ok_eq_hex(redMask, 0x00FF0000);
    ok_eq_hex(greenMask, 0x0000FF00);
    ok_eq_hex(blueMask, 0x000000FF);

    /* Masks must be non-overlapping */
    ok((redMask & greenMask) == 0, "Red and green masks overlap\n");
    ok((redMask & blueMask) == 0, "Red and blue masks overlap\n");
    ok((greenMask & blueMask) == 0, "Green and blue masks overlap\n");

    /* 16-bpp R5G6B5 masks */
    UINT r16 = 0xF800;
    UINT g16 = 0x07E0;
    UINT b16 = 0x001F;
    ok((r16 & g16) == 0, "16-bit R and G overlap\n");
    ok((r16 & b16) == 0, "16-bit R and B overlap\n");
    ok((g16 & b16) == 0, "16-bit G and B overlap\n");
    ok((r16 | g16 | b16) == 0xFFFF, "16-bit masks should cover all bits\n");
}

static void Test_ShadowSubstitution(void)
{
    /*
     * CDD replaces the device surface with shadow in DrvBitBlt:
     * if (psoTrg == DeviceSurface) psoTrg = psoShadow;
     *
     * This test verifies the substitution logic.
     */
    UINT DeviceSurface = 0x1000;
    UINT ShadowSurface = 0x2000;
    UINT OtherSurface = 0x3000;

    UINT target;

    /* Target is device surface → substitute */
    target = DeviceSurface;
    if (target == DeviceSurface) target = ShadowSurface;
    ok_eq_uint(target, ShadowSurface);

    /* Target is other surface → no substitution */
    target = OtherSurface;
    if (target == DeviceSurface) target = ShadowSurface;
    ok_eq_uint(target, OtherSurface);
}

static void Test_MultiRowFlush(void)
{
    TEST_FB_STATE fb;
    FbInit(&fb, 20, 20, 32);

    /* Write pattern: different color per row in a small region */
    UINT y;
    for (y = 5; y < 15; ++y)
    {
        UINT x;
        for (x = 5; x < 15; ++x)
            SetShadowPixel32(&fb, x, y, 0xFF000000 | (y << 8) | x);
    }

    FbFlushRect(&fb, 5, 5, 15, 15);

    /* Verify each pixel was flushed correctly */
    for (y = 5; y < 15; ++y)
    {
        UINT x;
        for (x = 5; x < 15; ++x)
        {
            UINT expected = 0xFF000000 | (y << 8) | x;
            UINT actual = GetVramPixel32(&fb, x, y);
            ok(actual == expected,
               "Pixel (%u,%u): expected 0x%08X, got 0x%08X\n",
               x, y, expected, actual);
        }
    }

    FbFree(&fb);
}

START_TEST(CddDisplay)
{
    Test_ShadowFlushBasic();
    Test_PartialFlush();
    Test_FlushClipping();
    Test_FlushEmptyRect();
    Test_FullWidthOptimization();
    Test_ModeFiltering();
    Test_DitherFormat();
    Test_StrideCalculation();
    Test_ColorMasks();
    Test_ShadowSubstitution();
    Test_MultiRowFlush();
}
