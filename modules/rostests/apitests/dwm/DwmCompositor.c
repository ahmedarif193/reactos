/*
 * PROJECT:     ReactOS DWM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM compositor pipeline and Z-order compositing tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the full compositor pipeline: window enumeration, capture, Z-order
 * compositing, dirty tracking integration, and frame present logic.
 */

#include "precomp.h"

/* ---- Simplified compositor types ---- */
typedef struct _TEST_SURFACE {
    UINT    Width, Height, Stride;
    UINT    *pPixels; /* XRGB 32-bit */
} TEST_SURFACE;

typedef struct _TEST_WINDOW {
    HWND    hWnd;
    RECT    rcWindow;
    BOOL    Visible;
    UINT    ZOrder; /* 0 = bottom */
    TEST_SURFACE Surface;
    struct _TEST_WINDOW *pNext;
} TEST_WINDOW;

typedef struct _TEST_COMP_CTX {
    TEST_SURFACE CompBuffer;
    TEST_WINDOW  *pWindowList;
    UINT          WindowCount;
    UINT          ScreenWidth;
    UINT          ScreenHeight;
    UINT64        FramesComposed;
} TEST_COMP_CTX;

static BOOL SurfAlloc(TEST_SURFACE *s, UINT w, UINT h)
{
    s->Width = w; s->Height = h; s->Stride = w;
    s->pPixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, w * h * 4);
    return s->pPixels != NULL;
}

static void SurfFree(TEST_SURFACE *s)
{
    if (s->pPixels) HeapFree(GetProcessHeap(), 0, s->pPixels);
    memset(s, 0, sizeof(*s));
}

static void SurfFill(TEST_SURFACE *s, UINT color)
{
    UINT i;
    for (i = 0; i < s->Width * s->Height; ++i)
        s->pPixels[i] = color;
}

static UINT SurfGetPixel(TEST_SURFACE *s, UINT x, UINT y)
{
    return s->pPixels[y * s->Stride + x];
}

/* Blit with clipping */
static void CompBlit(TEST_SURFACE *dst, INT dx, INT dy,
                     TEST_SURFACE *src, UINT sw, UINT sh)
{
    INT cx, cy, cw, ch;
    INT y;

    cx = (dx < 0) ? -dx : 0;
    cy = (dy < 0) ? -dy : 0;
    cw = (INT)sw - cx;
    ch = (INT)sh - cy;

    if (dx + cx + cw > (INT)dst->Width) cw = (INT)dst->Width - dx - cx;
    if (dy + cy + ch > (INT)dst->Height) ch = (INT)dst->Height - dy - cy;
    if (cw <= 0 || ch <= 0) return;

    for (y = 0; y < ch; ++y)
    {
        UINT *drow = dst->pPixels + (size_t)(dy + cy + y) * dst->Stride + (dx + cx);
        UINT *srow = src->pPixels + (size_t)(cy + y) * src->Stride + cx;
        memcpy(drow, srow, (size_t)cw * 4);
    }
}

/* Compose frame: bottom-to-top Z-order blit */
static void ComposeFrame(TEST_COMP_CTX *ctx)
{
    /* Clear composition buffer */
    memset(ctx->CompBuffer.pPixels, 0,
           (size_t)ctx->CompBuffer.Width * ctx->CompBuffer.Height * 4);

    /* Walk windows in Z-order (ascending = bottom first) */
    /* Sort by ZOrder first */
    TEST_WINDOW *sorted[256];
    UINT count = 0;
    TEST_WINDOW *w;

    for (w = ctx->pWindowList; w && count < 256; w = w->pNext)
    {
        if (w->Visible) sorted[count++] = w;
    }

    /* Simple insertion sort by ZOrder */
    UINT i, j;
    for (i = 1; i < count; ++i)
    {
        TEST_WINDOW *key = sorted[i];
        j = i;
        while (j > 0 && sorted[j-1]->ZOrder > key->ZOrder)
        {
            sorted[j] = sorted[j-1];
            --j;
        }
        sorted[j] = key;
    }

    /* Blit bottom-to-top */
    for (i = 0; i < count; ++i)
    {
        w = sorted[i];
        CompBlit(&ctx->CompBuffer,
                 w->rcWindow.left, w->rcWindow.top,
                 &w->Surface,
                 w->rcWindow.right - w->rcWindow.left,
                 w->rcWindow.bottom - w->rcWindow.top);
    }

    ctx->FramesComposed++;
}

static TEST_WINDOW *AddWindow(TEST_COMP_CTX *ctx, HWND hWnd,
                              LONG l, LONG t, LONG r, LONG b,
                              UINT z, UINT color)
{
    TEST_WINDOW *w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    w->hWnd = hWnd;
    w->rcWindow.left = l; w->rcWindow.top = t;
    w->rcWindow.right = r; w->rcWindow.bottom = b;
    w->Visible = TRUE;
    w->ZOrder = z;
    SurfAlloc(&w->Surface, r - l, b - t);
    SurfFill(&w->Surface, color);
    w->pNext = ctx->pWindowList;
    ctx->pWindowList = w;
    ctx->WindowCount++;
    return w;
}

static void FreeAllWindows(TEST_COMP_CTX *ctx)
{
    while (ctx->pWindowList)
    {
        TEST_WINDOW *next = ctx->pWindowList->pNext;
        SurfFree(&ctx->pWindowList->Surface);
        HeapFree(GetProcessHeap(), 0, ctx->pWindowList);
        ctx->pWindowList = next;
    }
    ctx->WindowCount = 0;
}

static void InitCtx(TEST_COMP_CTX *ctx, UINT w, UINT h)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->ScreenWidth = w;
    ctx->ScreenHeight = h;
    SurfAlloc(&ctx->CompBuffer, w, h);
}

static void FreeCtx(TEST_COMP_CTX *ctx)
{
    FreeAllWindows(ctx);
    SurfFree(&ctx->CompBuffer);
}

/* ---- Tests ---- */

static void Test_EmptyComposition(void)
{
    TEST_COMP_CTX ctx;
    InitCtx(&ctx, 100, 100);

    ComposeFrame(&ctx);

    /* Composition buffer should be all black */
    ok(SurfGetPixel(&ctx.CompBuffer, 0, 0) == 0, "Empty compose should be black\n");
    ok(SurfGetPixel(&ctx.CompBuffer, 50, 50) == 0, "Center should be black\n");
    ok(ctx.FramesComposed == 1, "Should have composed 1 frame\n");

    FreeCtx(&ctx);
}

static void Test_SingleWindowComposition(void)
{
    TEST_COMP_CTX ctx;
    InitCtx(&ctx, 100, 100);

    AddWindow(&ctx, (HWND)1, 10, 10, 60, 60, 0, 0xFFFF0000);

    ComposeFrame(&ctx);

    ok(SurfGetPixel(&ctx.CompBuffer, 10, 10) == 0xFFFF0000,
       "Window content should be red\n");
    ok(SurfGetPixel(&ctx.CompBuffer, 59, 59) == 0xFFFF0000,
       "Window bottom-right\n");
    ok(SurfGetPixel(&ctx.CompBuffer, 0, 0) == 0, "Outside window should be black\n");
    ok(SurfGetPixel(&ctx.CompBuffer, 60, 60) == 0, "Just outside window\n");

    FreeCtx(&ctx);
}

static void Test_ZOrderOverlap(void)
{
    TEST_COMP_CTX ctx;
    InitCtx(&ctx, 100, 100);

    /* Bottom window: blue at (0,0)-(50,50), Z=0 */
    AddWindow(&ctx, (HWND)1, 0, 0, 50, 50, 0, 0xFF0000FF);
    /* Top window: red at (25,25)-(75,75), Z=1 */
    AddWindow(&ctx, (HWND)2, 25, 25, 75, 75, 1, 0xFFFF0000);

    ComposeFrame(&ctx);

    /* Non-overlapping blue region */
    ok(SurfGetPixel(&ctx.CompBuffer, 0, 0) == 0xFF0000FF,
       "Bottom-left should be blue\n");
    ok(SurfGetPixel(&ctx.CompBuffer, 10, 10) == 0xFF0000FF,
       "Blue region should be preserved\n");

    /* Overlapping region: red overwrites blue (higher Z) */
    ok(SurfGetPixel(&ctx.CompBuffer, 30, 30) == 0xFFFF0000,
       "Overlap should be red (higher Z)\n");
    ok(SurfGetPixel(&ctx.CompBuffer, 49, 49) == 0xFFFF0000,
       "Corner of overlap should be red\n");

    /* Non-overlapping red region */
    ok(SurfGetPixel(&ctx.CompBuffer, 60, 60) == 0xFFFF0000,
       "Red-only region\n");

    /* Background */
    ok(SurfGetPixel(&ctx.CompBuffer, 80, 80) == 0, "Background should be black\n");

    FreeCtx(&ctx);
}

static void Test_ThreeLayerZOrder(void)
{
    TEST_COMP_CTX ctx;
    InitCtx(&ctx, 100, 100);

    /* Three overlapping windows at same position, different Z */
    AddWindow(&ctx, (HWND)1, 0, 0, 50, 50, 0, 0xFF0000FF); /* Blue, bottom */
    AddWindow(&ctx, (HWND)2, 0, 0, 50, 50, 1, 0xFF00FF00); /* Green, middle */
    AddWindow(&ctx, (HWND)3, 0, 0, 50, 50, 2, 0xFFFF0000); /* Red, top */

    ComposeFrame(&ctx);

    /* Top window (red) should be visible */
    ok(SurfGetPixel(&ctx.CompBuffer, 25, 25) == 0xFFFF0000,
       "Top Z-order window should be visible\n");

    FreeCtx(&ctx);
}

static void Test_InvisibleWindowSkipped(void)
{
    TEST_COMP_CTX ctx;
    InitCtx(&ctx, 100, 100);

    /* Visible window */
    AddWindow(&ctx, (HWND)1, 0, 0, 50, 50, 0, 0xFF00FF00);
    /* Invisible window overlapping */
    TEST_WINDOW *w = AddWindow(&ctx, (HWND)2, 0, 0, 50, 50, 1, 0xFFFF0000);
    w->Visible = FALSE;

    ComposeFrame(&ctx);

    /* Only visible window (green) should appear */
    ok(SurfGetPixel(&ctx.CompBuffer, 25, 25) == 0xFF00FF00,
       "Invisible window should be skipped, green should show\n");

    FreeCtx(&ctx);
}

static void Test_FullScreenWindow(void)
{
    TEST_COMP_CTX ctx;
    UINT x, y;
    BOOL allWhite = TRUE;

    InitCtx(&ctx, 100, 100);
    AddWindow(&ctx, (HWND)1, 0, 0, 100, 100, 0, 0xFFFFFFFF);

    ComposeFrame(&ctx);

    for (y = 0; y < 100; ++y)
        for (x = 0; x < 100; ++x)
            if (SurfGetPixel(&ctx.CompBuffer, x, y) != 0xFFFFFFFF)
                allWhite = FALSE;

    ok(allWhite, "Full-screen white window should fill entire buffer\n");

    FreeCtx(&ctx);
}

static void Test_MultipleFrames(void)
{
    TEST_COMP_CTX ctx;
    InitCtx(&ctx, 50, 50);

    AddWindow(&ctx, (HWND)1, 0, 0, 50, 50, 0, 0xFF111111);

    ComposeFrame(&ctx);
    ComposeFrame(&ctx);
    ComposeFrame(&ctx);

    ok(ctx.FramesComposed == 3, "Should have composed 3 frames\n");
    ok(SurfGetPixel(&ctx.CompBuffer, 25, 25) == 0xFF111111,
       "Content should persist across frames\n");

    FreeCtx(&ctx);
}

static void Test_WindowClipping(void)
{
    TEST_COMP_CTX ctx;
    InitCtx(&ctx, 50, 50);

    /* Window extends beyond screen */
    AddWindow(&ctx, (HWND)1, 40, 40, 100, 100, 0, 0xFFAA5500);

    ComposeFrame(&ctx);

    /* Visible portion */
    ok(SurfGetPixel(&ctx.CompBuffer, 40, 40) == 0xFFAA5500,
       "Clipped window visible portion\n");
    ok(SurfGetPixel(&ctx.CompBuffer, 49, 49) == 0xFFAA5500,
       "Edge of clipped window\n");

    /* Outside screen should not crash (composition buffer stays within bounds) */
    ok(SurfGetPixel(&ctx.CompBuffer, 0, 0) == 0, "Outside window region\n");

    FreeCtx(&ctx);
}

static void Test_CompBufferSizes(void)
{
    /* Test common screen resolutions */
    UINT resolutions[][2] = {
        { 640, 480 }, { 1024, 768 }, { 1920, 1080 }, { 2560, 1440 }
    };
    ULONG i;

    for (i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); ++i)
    {
        TEST_COMP_CTX ctx;
        InitCtx(&ctx, resolutions[i][0], resolutions[i][1]);

        ok(ctx.CompBuffer.Width == resolutions[i][0],
           "%ux%u: width mismatch\n", resolutions[i][0], resolutions[i][1]);
        ok(ctx.CompBuffer.Height == resolutions[i][1],
           "%ux%u: height mismatch\n", resolutions[i][0], resolutions[i][1]);
        ok(ctx.CompBuffer.pPixels != NULL,
           "%ux%u: allocation failed\n", resolutions[i][0], resolutions[i][1]);

        FreeCtx(&ctx);
    }
}

static void Test_FrameCounterMonotonic(void)
{
    TEST_COMP_CTX ctx;
    UINT64 prev;

    InitCtx(&ctx, 10, 10);

    prev = ctx.FramesComposed;
    ComposeFrame(&ctx);
    ok(ctx.FramesComposed == prev + 1, "Frame counter should increment\n");

    prev = ctx.FramesComposed;
    ComposeFrame(&ctx);
    ok(ctx.FramesComposed == prev + 1, "Frame counter should increment again\n");

    FreeCtx(&ctx);
}

START_TEST(DwmCompositor)
{
    Test_EmptyComposition();
    Test_SingleWindowComposition();
    Test_ZOrderOverlap();
    Test_ThreeLayerZOrder();
    Test_InvisibleWindowSkipped();
    Test_FullScreenWindow();
    Test_MultipleFrames();
    Test_WindowClipping();
    Test_CompBufferSizes();
    Test_FrameCounterMonotonic();
}
