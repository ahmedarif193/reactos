/*
 * PROJECT:     ReactOS D3DKMT API Tests
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
#include <winuser.h>

#define DWM_ESCAPE_SUPPRESS_CURSOR  0x44574D01
#define DWM_ESCAPE_COMPOSITION_SYNC 0x44574D02

/* ---- Test 1: Cursor suppress ExtEscape ---- */
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

    /*
     * The DWM cursor-suppress escape is a ReactOS WDDM compositor private
     * interface. ExtEscape returns 0 for an escape the display driver does not
     * implement, so on Win11 (which has no such escape) we skip; on the ReactOS
     * compositor (Phase B) it returns 1 and we validate the contract.
     */
    suppress = 1;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(suppress), (LPCSTR)&suppress, 0, NULL);
    if (ret <= 0)
    {
        skip("DWM_ESCAPE_SUPPRESS_CURSOR not implemented by the display driver (ret %d)\n", ret);
        ReleaseDC(NULL, hDcScreen);
        return;
    }
    ok(ret == 1, "CursorSuppress(1) should return 1, got %d\n", ret);

    /* Unsuppress cursor (end) */
    suppress = 0;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(suppress), (LPCSTR)&suppress, 0, NULL);
    ok(ret == 1, "CursorSuppress(0) should return 1, got %d\n", ret);

    ReleaseDC(NULL, hDcScreen);
}

/* ---- Test 2: Composition sync ExtEscape ---- */
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

    /* Begin composition sync (ReactOS compositor private escape; see above) */
    sync = 1;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC,
                    sizeof(sync), (LPCSTR)&sync, 0, NULL);
    if (ret <= 0)
    {
        skip("DWM_ESCAPE_COMPOSITION_SYNC not implemented by the display driver (ret %d)\n", ret);
        ReleaseDC(NULL, hDcScreen);
        return;
    }
    ok(ret == 1, "CompositionSync(begin=1) should return 1, got %d\n", ret);

    /* End composition sync */
    sync = 0;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC,
                    sizeof(sync), (LPCSTR)&sync, 0, NULL);
    ok(ret == 1, "CompositionSync(end=0) should return 1, got %d\n", ret);

    ReleaseDC(NULL, hDcScreen);
}

/* ---- Test 3: Invalid ExtEscape (NULL input) ---- */
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

    /* NULL input data with zero size */
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR, 0, NULL, 0, NULL);
    ok(ret <= 0, "CursorSuppress with NULL input should fail, got %d\n", ret);

    /* NULL input data with non-zero size */
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_SUPPRESS_CURSOR,
                    sizeof(LONG), NULL, 0, NULL);
    ok(ret <= 0, "CursorSuppress with NULL data ptr should fail, got %d\n", ret);

    /* Zero-size input with valid pointer */
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

/* ---- Test 4: Composition sync under load (BitBlt stress) ---- */
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

    /* Begin composition sync (ReactOS compositor private escape; see above) */
    sync = 1;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC,
                    sizeof(sync), (LPCSTR)&sync, 0, NULL);
    if (ret <= 0)
    {
        skip("DWM_ESCAPE_COMPOSITION_SYNC not implemented by the display driver (ret %d)\n", ret);
        SelectObject(hDcMem, hOldBitmap);
        DeleteObject(hBitmap);
        DeleteDC(hDcMem);
        ReleaseDC(NULL, hDcScreen);
        return;
    }
    ok(ret == 1, "CompositionSync begin should return 1, got %d\n", ret);

    /*
     * Snapshot the live screen region into the scratch bitmap first so the
     * BitBlt load below copies the desktop back onto itself instead of an
     * uninitialised bitmap.  This keeps the test non-destructive: on a
     * compositor that does not continuously recomposite an idle desktop,
     * blitting a blank bitmap to the screen DC would otherwise leave a black
     * rectangle behind.  The escape contract + BitBlt-under-sync path are
     * exercised identically either way.
     */
    BitBlt(hDcMem, 0, 0, 640, 480, hDcScreen, 0, 0, SRCCOPY);

    /* BitBlt under composition sync */
    for (i = 0; i < 10; ++i)
    {
        BitBlt(hDcScreen, 0, 0, 640, 480, hDcMem, 0, 0, SRCCOPY);
    }

    /* End composition sync */
    sync = 0;
    ret = ExtEscape(hDcScreen, DWM_ESCAPE_COMPOSITION_SYNC,
                    sizeof(sync), (LPCSTR)&sync, 0, NULL);
    ok(ret == 1, "CompositionSync end should return 1, got %d\n", ret);

    /* If we get here, no crash occurred */
    ok(TRUE, "Composition sync under BitBlt load completed without crash\n");

    SelectObject(hDcMem, hOldBitmap);
    DeleteObject(hBitmap);
    DeleteDC(hDcMem);
    ReleaseDC(NULL, hDcScreen);
}

START_TEST(dwm)
{
    Test_CursorSuppressExtEscape();
    Test_CompositionSyncExtEscape();
    Test_InvalidExtEscape();
    Test_CompositionSyncUnderLoad();
}
