/*
 * PROJECT:     ReactOS DWM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM surface allocation, free, and resize tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the DWM redirection surface management logic.  Since DWM surface
 * functions are internal to dwm.exe, these tests replicate the allocation
 * algorithms and verify their correctness.
 */

#include "precomp.h"

/* Replicate DWM_SURFACE structure from dwm.h */
typedef struct _TEST_DWM_SURFACE
{
    UINT        Width;
    UINT        Height;
    UINT        Stride;
    UINT        BytesPerPixel;
    PVOID       pPixels;
    SIZE_T      AllocationSize;
    UINT        hAllocation; /* D3DKMT_HANDLE placeholder */
    BOOL        Dirty;
} TEST_DWM_SURFACE;

/* Replicate DwmSurfaceAlloc */
static BOOL TestSurfaceAlloc(TEST_DWM_SURFACE *pSurface, UINT Width, UINT Height)
{
    if (!pSurface || Width == 0 || Height == 0)
        return FALSE;

    pSurface->Width = Width;
    pSurface->Height = Height;
    pSurface->BytesPerPixel = 4;
    pSurface->Stride = ((Width * 4) + 3) & ~3; /* 4-byte aligned */
    pSurface->AllocationSize = (SIZE_T)pSurface->Stride * Height;
    pSurface->pPixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                                  pSurface->AllocationSize);
    pSurface->hAllocation = 0;
    pSurface->Dirty = FALSE;

    return pSurface->pPixels != NULL;
}

/* Replicate DwmSurfaceFree */
static void TestSurfaceFree(TEST_DWM_SURFACE *pSurface)
{
    if (pSurface && pSurface->pPixels)
    {
        HeapFree(GetProcessHeap(), 0, pSurface->pPixels);
        pSurface->pPixels = NULL;
        pSurface->Width = 0;
        pSurface->Height = 0;
        pSurface->Stride = 0;
        pSurface->AllocationSize = 0;
        pSurface->Dirty = FALSE;
    }
}

/* Replicate DwmSurfaceResize */
static BOOL TestSurfaceResize(TEST_DWM_SURFACE *pSurface, UINT NewWidth, UINT NewHeight)
{
    if (!pSurface || NewWidth == 0 || NewHeight == 0)
        return FALSE;

    if (pSurface->Width == NewWidth && pSurface->Height == NewHeight)
        return TRUE; /* No change needed */

    TestSurfaceFree(pSurface);
    return TestSurfaceAlloc(pSurface, NewWidth, NewHeight);
}

static void Test_SurfaceAllocBasic(void)
{
    TEST_DWM_SURFACE surface;
    BOOL result;

    memset(&surface, 0, sizeof(surface));

    /* Allocate a standard 1920x1080 surface */
    result = TestSurfaceAlloc(&surface, 1920, 1080);
    ok(result, "SurfaceAlloc 1920x1080 failed\n");
    ok(surface.pPixels != NULL, "pPixels is NULL\n");
    ok_eq_uint(surface.Width, 1920);
    ok_eq_uint(surface.Height, 1080);
    ok_eq_uint(surface.BytesPerPixel, 4);
    ok(surface.Stride >= 1920 * 4, "Stride too small: %u\n", surface.Stride);
    ok((surface.Stride & 3) == 0, "Stride not 4-byte aligned: %u\n", surface.Stride);
    ok(surface.AllocationSize >= (SIZE_T)surface.Stride * 1080,
       "AllocationSize too small\n");
    ok(surface.Dirty == FALSE, "Newly allocated surface should not be dirty\n");
    ok(surface.hAllocation == 0, "Phase 1 hAllocation should be 0\n");

    TestSurfaceFree(&surface);
}

static void Test_SurfaceAllocSmall(void)
{
    TEST_DWM_SURFACE surface;
    BOOL result;

    memset(&surface, 0, sizeof(surface));

    /* 1x1 minimum surface */
    result = TestSurfaceAlloc(&surface, 1, 1);
    ok(result, "SurfaceAlloc 1x1 failed\n");
    ok(surface.pPixels != NULL, "pPixels is NULL for 1x1\n");
    ok_eq_uint(surface.Width, 1);
    ok_eq_uint(surface.Height, 1);
    ok(surface.Stride >= 4, "Stride too small for 1 pixel\n");
    ok(surface.AllocationSize >= 4, "AllocationSize too small for 1 pixel\n");

    TestSurfaceFree(&surface);
}

static void Test_SurfaceAllocZero(void)
{
    TEST_DWM_SURFACE surface;
    BOOL result;

    memset(&surface, 0, sizeof(surface));

    /* Zero width — should fail */
    result = TestSurfaceAlloc(&surface, 0, 100);
    ok(!result, "SurfaceAlloc with width=0 should fail\n");

    /* Zero height — should fail */
    result = TestSurfaceAlloc(&surface, 100, 0);
    ok(!result, "SurfaceAlloc with height=0 should fail\n");

    /* Both zero — should fail */
    result = TestSurfaceAlloc(&surface, 0, 0);
    ok(!result, "SurfaceAlloc with both=0 should fail\n");
}

static void Test_SurfaceAllocNull(void)
{
    BOOL result;

    /* NULL pointer — should fail gracefully */
    result = TestSurfaceAlloc(NULL, 100, 100);
    ok(!result, "SurfaceAlloc with NULL pointer should fail\n");
}

static void Test_SurfaceFreeBasic(void)
{
    TEST_DWM_SURFACE surface;

    memset(&surface, 0, sizeof(surface));
    TestSurfaceAlloc(&surface, 640, 480);
    ok(surface.pPixels != NULL, "pPixels should be non-NULL before free\n");

    TestSurfaceFree(&surface);
    ok(surface.pPixels == NULL, "pPixels should be NULL after free\n");
    ok_eq_uint(surface.Width, 0);
    ok_eq_uint(surface.Height, 0);
    ok_eq_uint(surface.Stride, 0);
    ok_eq_size(surface.AllocationSize, 0);
    ok(surface.Dirty == FALSE, "Dirty should be FALSE after free\n");
}

static void Test_SurfaceDoubleFree(void)
{
    TEST_DWM_SURFACE surface;

    memset(&surface, 0, sizeof(surface));
    TestSurfaceAlloc(&surface, 640, 480);

    TestSurfaceFree(&surface);
    /* Second free should be a no-op (pPixels is NULL) */
    TestSurfaceFree(&surface);
    ok(surface.pPixels == NULL, "pPixels should still be NULL after double free\n");
}

static void Test_SurfaceFreeNull(void)
{
    /* Freeing NULL should not crash */
    TestSurfaceFree(NULL);
    ok(TRUE, "Free NULL should not crash\n");
}

static void Test_SurfaceResizeGrow(void)
{
    TEST_DWM_SURFACE surface;

    memset(&surface, 0, sizeof(surface));
    TestSurfaceAlloc(&surface, 640, 480);

    BOOL result = TestSurfaceResize(&surface, 1920, 1080);
    ok(result, "Resize from 640x480 to 1920x1080 failed\n");
    ok_eq_uint(surface.Width, 1920);
    ok_eq_uint(surface.Height, 1080);
    ok(surface.pPixels != NULL, "pPixels NULL after resize\n");
    ok(surface.AllocationSize >= (SIZE_T)surface.Stride * 1080,
       "AllocationSize too small after resize\n");

    TestSurfaceFree(&surface);
}

static void Test_SurfaceResizeShrink(void)
{
    TEST_DWM_SURFACE surface;

    memset(&surface, 0, sizeof(surface));
    TestSurfaceAlloc(&surface, 1920, 1080);

    BOOL result = TestSurfaceResize(&surface, 640, 480);
    ok(result, "Resize from 1920x1080 to 640x480 failed\n");
    ok_eq_uint(surface.Width, 640);
    ok_eq_uint(surface.Height, 480);

    TestSurfaceFree(&surface);
}

static void Test_SurfaceResizeSameSize(void)
{
    TEST_DWM_SURFACE surface;

    memset(&surface, 0, sizeof(surface));
    TestSurfaceAlloc(&surface, 800, 600);

    PVOID origPixels = surface.pPixels;
    BOOL result = TestSurfaceResize(&surface, 800, 600);
    ok(result, "Resize to same size should succeed\n");
    ok_eq_uint(surface.Width, 800);
    ok_eq_uint(surface.Height, 600);
    /* Pointer should be unchanged (no reallocation) */
    ok(surface.pPixels == origPixels,
       "Same-size resize should not reallocate\n");

    TestSurfaceFree(&surface);
}

static void Test_SurfaceResizeZero(void)
{
    TEST_DWM_SURFACE surface;

    memset(&surface, 0, sizeof(surface));
    TestSurfaceAlloc(&surface, 800, 600);

    BOOL result = TestSurfaceResize(&surface, 0, 600);
    ok(!result, "Resize to width=0 should fail\n");

    result = TestSurfaceResize(&surface, 800, 0);
    ok(!result, "Resize to height=0 should fail\n");
}

static void Test_SurfaceStride(void)
{
    TEST_DWM_SURFACE surface;
    UINT widths[] = { 1, 2, 3, 4, 5, 7, 8, 15, 16, 100, 640, 1024, 1920 };
    ULONG i;

    for (i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i)
    {
        memset(&surface, 0, sizeof(surface));
        TestSurfaceAlloc(&surface, widths[i], 1);

        /* Stride must be >= width * 4 */
        ok(surface.Stride >= widths[i] * 4,
           "Width %u: stride %u < minimum %u\n",
           widths[i], surface.Stride, widths[i] * 4);

        /* Stride must be 4-byte aligned */
        ok((surface.Stride & 3) == 0,
           "Width %u: stride %u not 4-byte aligned\n",
           widths[i], surface.Stride);

        TestSurfaceFree(&surface);
    }
}

static void Test_SurfaceZeroFill(void)
{
    TEST_DWM_SURFACE surface;
    UINT *pixels;
    ULONG i;
    BOOL allZero = TRUE;

    memset(&surface, 0, sizeof(surface));
    TestSurfaceAlloc(&surface, 100, 100);

    /* Verify that pixels are zero-filled */
    pixels = (UINT *)surface.pPixels;
    for (i = 0; i < 100 * 100; ++i)
    {
        if (pixels[i] != 0)
        {
            allZero = FALSE;
            break;
        }
    }
    ok(allZero, "Surface pixels should be zero-filled\n");

    TestSurfaceFree(&surface);
}

static void Test_SurfaceWriteRead(void)
{
    TEST_DWM_SURFACE surface;
    UINT *pixels;

    memset(&surface, 0, sizeof(surface));
    TestSurfaceAlloc(&surface, 10, 10);

    pixels = (UINT *)surface.pPixels;

    /* Write test pattern */
    pixels[0] = 0xFFFF0000; /* Red */
    pixels[1] = 0xFF00FF00; /* Green */
    pixels[2] = 0xFF0000FF; /* Blue */
    pixels[99] = 0xDEADBEEF; /* Last pixel */

    /* Read back */
    ok_eq_hex(pixels[0], 0xFFFF0000);
    ok_eq_hex(pixels[1], 0xFF00FF00);
    ok_eq_hex(pixels[2], 0xFF0000FF);
    ok_eq_hex(pixels[99], 0xDEADBEEF);

    TestSurfaceFree(&surface);
}

static void Test_SurfaceDirtyFlag(void)
{
    TEST_DWM_SURFACE surface;

    memset(&surface, 0, sizeof(surface));
    TestSurfaceAlloc(&surface, 100, 100);

    ok(surface.Dirty == FALSE, "New surface should not be dirty\n");

    surface.Dirty = TRUE;
    ok(surface.Dirty == TRUE, "Dirty flag should be settable\n");

    surface.Dirty = FALSE;
    ok(surface.Dirty == FALSE, "Dirty flag should be clearable\n");

    TestSurfaceFree(&surface);
}

static void Test_SurfaceCommonResolutions(void)
{
    TEST_DWM_SURFACE surface;
    struct { UINT w, h; } resolutions[] = {
        { 640, 480 },   /* VGA */
        { 800, 600 },   /* SVGA */
        { 1024, 768 },  /* XGA */
        { 1280, 720 },  /* 720p */
        { 1280, 1024 }, /* SXGA */
        { 1366, 768 },  /* HD */
        { 1600, 900 },  /* HD+ */
        { 1920, 1080 }, /* 1080p */
        { 2560, 1440 }, /* 1440p */
        { 3840, 2160 }, /* 4K */
    };
    ULONG i;

    for (i = 0; i < sizeof(resolutions) / sizeof(resolutions[0]); ++i)
    {
        memset(&surface, 0, sizeof(surface));
        BOOL result = TestSurfaceAlloc(&surface, resolutions[i].w, resolutions[i].h);
        ok(result, "%ux%u: SurfaceAlloc failed\n", resolutions[i].w, resolutions[i].h);
        ok_eq_uint(surface.Width, resolutions[i].w);
        ok_eq_uint(surface.Height, resolutions[i].h);
        ok(surface.pPixels != NULL, "%ux%u: pPixels is NULL\n",
           resolutions[i].w, resolutions[i].h);
        ok(surface.AllocationSize == (SIZE_T)surface.Stride * resolutions[i].h,
           "%ux%u: AllocationSize mismatch\n", resolutions[i].w, resolutions[i].h);
        TestSurfaceFree(&surface);
    }
}

START_TEST(DwmSurface)
{
    Test_SurfaceAllocBasic();
    Test_SurfaceAllocSmall();
    Test_SurfaceAllocZero();
    Test_SurfaceAllocNull();
    Test_SurfaceFreeBasic();
    Test_SurfaceDoubleFree();
    Test_SurfaceFreeNull();
    Test_SurfaceResizeGrow();
    Test_SurfaceResizeShrink();
    Test_SurfaceResizeSameSize();
    Test_SurfaceResizeZero();
    Test_SurfaceStride();
    Test_SurfaceZeroFill();
    Test_SurfaceWriteRead();
    Test_SurfaceDirtyFlag();
    Test_SurfaceCommonResolutions();
}
