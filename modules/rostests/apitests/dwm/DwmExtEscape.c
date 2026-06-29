/*
 * PROJECT:     ReactOS DWM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM compositor ExtEscape integration tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the ExtEscape-based DWM compositor control interface:
 *   0x44574D01 = DWM_ESCAPE_SUPPRESS_CURSOR
 *   0x44574D02 = DWM_ESCAPE_COMPOSITION_SYNC
 *
 * These escape codes are used by the DWM compositor (dwm.exe) to signal
 * the display driver to suppress cursor drawing and synchronize composition
 * frame boundaries with the present worker.
 */

#include "precomp.h"

#define DWM_ESCAPE_SUPPRESS_CURSOR  0x44574D01
#define DWM_ESCAPE_COMPOSITION_SYNC 0x44574D02

/* ---- Test: Cursor suppress ExtEscape ---- */
static void Test_CursorSuppressExtEscape(void)
{
    HDC hDcScreen;
    LONG suppress;
    int ret;

    hDcScreen = GetDC(NULL);
    if (!hDcScreen)
    {
        skip("GetDC(NULL) failed\n");
        return;
    }

    /* Suppress cursor (begin) */
    suppress = 1;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(suppress), (LPCSTR)&suppress, 0, NULL);
    ok(ret == 1, "CursorSuppress(1) should return 1, got %d\n", ret);

    /* Unsuppress cursor (end) */
    suppress = 0;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(suppress), (LPCSTR)&suppress, 0, NULL);
    ok(ret == 1, "CursorSuppress(0) should return 1, got %d\n", ret);

    ReleaseDC(NULL, hDcScreen);
}

/* ---- Test: Composition sync ExtEscape ---- */
static void Test_CompositionSyncExtEscape(void)
{
    HDC hDcScreen;
    LONG sync;
    int ret;

    hDcScreen = GetDC(NULL);
    if (!hDcScreen)
    {
        skip("GetDC(NULL) failed\n");
        return;
    }

    /* Begin composition sync */
    sync = 1;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC,
                    sizeof(sync), (LPCSTR)&sync, 0, NULL);
    ok(ret == 1, "CompositionSync(begin=1) should return 1, got %d\n", ret);

    /* End composition sync */
    sync = 0;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC,
                    sizeof(sync), (LPCSTR)&sync, 0, NULL);
    ok(ret == 1, "CompositionSync(end=0) should return 1, got %d\n", ret);

    ReleaseDC(NULL, hDcScreen);
}

/* ---- Test: Invalid ExtEscape (NULL input) ---- */
static void Test_InvalidExtEscape(void)
{
    HDC hDcScreen;
    int ret;

    hDcScreen = GetDC(NULL);
    if (!hDcScreen)
    {
        skip("GetDC(NULL) failed\n");
        return;
    }

    /* NULL input data with zero size -- should fail gracefully */
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR, 0, NULL, 0, NULL);
    ok(ret <= 0, "CursorSuppress with NULL input should fail, got %d\n", ret);

    /* NULL input data with non-zero size -- should fail gracefully */
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(LONG), NULL, 0, NULL);
    ok(ret <= 0, "CursorSuppress with NULL data ptr should fail, got %d\n", ret);

    /* Zero-size input with valid pointer -- should fail gracefully */
    {
        LONG val = 1;
        ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                        0, (LPCSTR)&val, 0, NULL);
        ok(ret <= 0, "CursorSuppress with zero-size should fail, got %d\n", ret);
    }

    /* Composition sync with NULL input */
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC, 0, NULL, 0, NULL);
    ok(ret <= 0, "CompositionSync with NULL input should fail, got %d\n", ret);

    ReleaseDC(NULL, hDcScreen);
}

/* ---- Test: Composition sync under load (BitBlt stress) ---- */
static void Test_CompositionSyncUnderLoad(void)
{
    HDC hDcScreen, hDcMem;
    HBITMAP hBitmap, hOldBitmap;
    LONG sync;
    int ret;
    ULONG i;

    hDcScreen = GetDC(NULL);
    if (!hDcScreen)
    {
        skip("GetDC(NULL) failed\n");
        return;
    }

    hDcMem = CreateCompatibleDC(hDcScreen);
    if (!hDcMem)
    {
        skip("CreateCompatibleDC failed\n");
        ReleaseDC(NULL, hDcScreen);
        return;
    }

    hBitmap = CreateCompatibleBitmap(hDcScreen, 640, 480);
    if (!hBitmap)
    {
        skip("CreateCompatibleBitmap failed\n");
        DeleteDC(hDcMem);
        ReleaseDC(NULL, hDcScreen);
        return;
    }

    hOldBitmap = (HBITMAP)SelectObject(hDcMem, hBitmap);

    /* Begin composition sync */
    sync = 1;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC,
                    sizeof(sync), (LPCSTR)&sync, 0, NULL);
    ok(ret == 1, "CompositionSync begin should return 1, got %d\n", ret);

    /* BitBlt under composition sync -- should not crash */
    for (i = 0; i < 10; ++i)
    {
        BitBlt(hDcScreen, 0, 0, 640, 480, hDcMem, 0, 0, SRCCOPY);
    }

    /* End composition sync */
    sync = 0;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC,
                    sizeof(sync), (LPCSTR)&sync, 0, NULL);
    ok(ret == 1, "CompositionSync end should return 1, got %d\n", ret);

    /* Verify we survived without crash */
    ok(TRUE, "Composition sync under BitBlt load completed without crash\n");

    SelectObject(hDcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hDcMem);
    ReleaseDC(NULL, hDcScreen);
}

/* ---- Test: Double suppress/unsuppress (idempotent) ---- */
static void Test_DoubleSuppressUnsuppress(void)
{
    HDC hDcScreen;
    LONG suppress;
    int ret;

    hDcScreen = GetDC(NULL);
    if (!hDcScreen)
    {
        skip("GetDC(NULL) failed\n");
        return;
    }

    /* Suppress twice -- second call should still succeed */
    suppress = 1;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(suppress), (LPCSTR)&suppress, 0, NULL);
    ok(ret == 1, "First suppress should return 1, got %d\n", ret);

    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(suppress), (LPCSTR)&suppress, 0, NULL);
    ok(ret == 1, "Second suppress should return 1, got %d\n", ret);

    /* Unsuppress twice */
    suppress = 0;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(suppress), (LPCSTR)&suppress, 0, NULL);
    ok(ret == 1, "First unsuppress should return 1, got %d\n", ret);

    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(suppress), (LPCSTR)&suppress, 0, NULL);
    ok(ret == 1, "Second unsuppress should return 1, got %d\n", ret);

    ReleaseDC(NULL, hDcScreen);
}

/* ---- Test: Mixed cursor + composition sync pattern (mirrors DWM compositor) ---- */
static void Test_MixedCursorCompositionPattern(void)
{
    HDC hDcScreen;
    LONG val;
    int ret;

    hDcScreen = GetDC(NULL);
    if (!hDcScreen)
    {
        skip("GetDC(NULL) failed\n");
        return;
    }

    /* Reproduce the exact pattern from dwm/compositor.c:
     * 1. Suppress cursor
     * 2. Begin composition sync
     * 3. (blit happens here)
     * 4. End composition sync
     * 5. Unsuppress cursor
     */
    val = 1;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(val), (LPCSTR)&val, 0, NULL);
    ok(ret == 1, "Suppress cursor should return 1, got %d\n", ret);

    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC,
                    sizeof(val), (LPCSTR)&val, 0, NULL);
    ok(ret == 1, "Begin composition sync should return 1, got %d\n", ret);

    val = 0;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC,
                    sizeof(val), (LPCSTR)&val, 0, NULL);
    ok(ret == 1, "End composition sync should return 1, got %d\n", ret);

    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(val), (LPCSTR)&val, 0, NULL);
    ok(ret == 1, "Unsuppress cursor should return 1, got %d\n", ret);

    ReleaseDC(NULL, hDcScreen);
}

START_TEST(DwmExtEscape)
{
    Test_CursorSuppressExtEscape();
    Test_CompositionSyncExtEscape();
    Test_InvalidExtEscape();
    Test_CompositionSyncUnderLoad();
    Test_DoubleSuppressUnsuppress();
    Test_MixedCursorCompositionPattern();
}
