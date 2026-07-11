/*
 * PROJECT:     ReactOS api tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Z-order topmost band invariants (SetWindowPos/IntLinkHwnd)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif193@gmail.com>
 *
 * Verifies the WS_EX_TOPMOST band contract that the shell relies on (the
 * taskbar is topmost and normal windows must NEVER link above the band):
 *  - a normal window created/raised while ONLY topmost windows exist links
 *    BELOW the whole band (regression: IntLinkHwnd stopped its band walk on
 *    the last sibling and linked normal windows at the absolute head),
 *  - HWND_TOP/BringWindowToTop/activation never cross the band,
 *  - a new TOPMOST window (Task Manager "always on top") goes to the top of
 *    the band without evicting or hiding existing normal windows,
 *  - HWND_NOTOPMOST/HWND_TOPMOST round-trip keeps flags and order sane,
 *  - owned windows of a topmost owner are promoted above their owner.
 * All assertions are relative-order only, so the test is portable to real
 * Windows for ground truth.
 */

#include "precomp.h"

static const WCHAR s_ClassName[] = L"TopmostBandTestClass";

static HWND
CreateTestWindow(_In_ DWORD dwExStyle, _In_opt_ HWND hOwner, _In_ int n)
{
    HWND hwnd = CreateWindowExW(dwExStyle,
                                s_ClassName,
                                L"TopmostBandTest",
                                WS_POPUP | WS_VISIBLE,
                                10 + n * 30, 10 + n * 30, 120, 90,
                                hOwner, NULL,
                                GetModuleHandleW(NULL), NULL);
    return hwnd;
}

/* Position of hwnd in the desktop z-chain (0 = topmost). -1 = not found. */
static int
ZIndexOf(_In_ HWND hwnd)
{
    HWND h;
    int i = 0;

    for (h = GetTopWindow(NULL); h != NULL; h = GetWindow(h, GW_HWNDNEXT), i++)
    {
        if (h == hwnd)
            return i;
    }
    return -1;
}

static BOOL
IsTopmost(_In_ HWND hwnd)
{
    return (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
}

#define expect_above(hi, lo) \
    do { \
        int iHi = ZIndexOf(hi), iLo = ZIndexOf(lo); \
        ok(iHi >= 0 && iLo >= 0, "window missing from z-chain (%d, %d)\n", iHi, iLo); \
        ok(iHi < iLo, "expected %p (z=%d) above %p (z=%d)\n", (hi), iHi, (lo), iLo); \
    } while (0)

static void
Test_NormalNeverEntersBand(void)
{
    HWND hTop1, hTop2, hNorm;

    hTop1 = CreateTestWindow(WS_EX_TOPMOST, NULL, 0);
    hTop2 = CreateTestWindow(WS_EX_TOPMOST, NULL, 1);
    ok(hTop1 != NULL && hTop2 != NULL, "topmost creation failed\n");

    /* The regression case: every window above is topmost; the new normal
     * window must link BELOW the whole band, not at the absolute head. */
    hNorm = CreateTestWindow(0, NULL, 2);
    ok(hNorm != NULL, "normal creation failed\n");

    ok(!IsTopmost(hNorm), "normal window got WS_EX_TOPMOST on creation\n");
    expect_above(hTop1, hNorm);
    expect_above(hTop2, hNorm);

    /* Explicit raises must not cross the band either. */
    SetWindowPos(hNorm, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ok(!IsTopmost(hNorm), "HWND_TOP promoted a normal window to topmost\n");
    expect_above(hTop1, hNorm);
    expect_above(hTop2, hNorm);

    BringWindowToTop(hNorm);
    ok(!IsTopmost(hNorm), "BringWindowToTop promoted a normal window\n");
    expect_above(hTop1, hNorm);
    expect_above(hTop2, hNorm);

    DestroyWindow(hNorm);
    DestroyWindow(hTop2);
    DestroyWindow(hTop1);
}

static void
Test_TopmostOverNormals(void)
{
    HWND hNorm1, hNorm2, hTop;

    /* Task Manager scenario: normal windows exist, a topmost one opens. */
    hNorm1 = CreateTestWindow(0, NULL, 0);
    hNorm2 = CreateTestWindow(0, NULL, 1);
    hTop = CreateTestWindow(WS_EX_TOPMOST, NULL, 2);
    ok(hNorm1 && hNorm2 && hTop, "creation failed\n");

    ok(IsTopmost(hTop), "topmost creation lost WS_EX_TOPMOST\n");
    expect_above(hTop, hNorm1);
    expect_above(hTop, hNorm2);

    /* The normal windows must remain visible and enumerable. */
    ok(IsWindowVisible(hNorm1), "normal window 1 vanished\n");
    ok(IsWindowVisible(hNorm2), "normal window 2 vanished\n");
    ok(ZIndexOf(hNorm1) >= 0, "normal window 1 left the z-chain\n");
    ok(ZIndexOf(hNorm2) >= 0, "normal window 2 left the z-chain\n");

    /* Raising a normal window goes to the top of the NORMAL band only. */
    SetWindowPos(hNorm1, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    expect_above(hTop, hNorm1);
    expect_above(hNorm1, hNorm2);

    DestroyWindow(hTop);
    DestroyWindow(hNorm2);
    DestroyWindow(hNorm1);
}

static void
Test_TopmostToggle(void)
{
    HWND hNorm, hTop;

    hNorm = CreateTestWindow(0, NULL, 0);
    hTop = CreateTestWindow(WS_EX_TOPMOST, NULL, 1);
    ok(hNorm && hTop, "creation failed\n");

    SetWindowPos(hTop, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ok(!IsTopmost(hTop), "HWND_NOTOPMOST did not clear WS_EX_TOPMOST\n");

    SetWindowPos(hTop, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    ok(IsTopmost(hTop), "HWND_TOPMOST did not set WS_EX_TOPMOST\n");
    expect_above(hTop, hNorm);

    /* Toggling must not disturb the other window's visibility. */
    ok(IsWindowVisible(hNorm), "normal window vanished on topmost toggle\n");

    DestroyWindow(hTop);
    DestroyWindow(hNorm);
}

static void
Test_OwnedOfTopmostOwner(void)
{
    HWND hOwner, hOwned;

    hOwner = CreateTestWindow(WS_EX_TOPMOST, NULL, 0);
    hOwned = CreateTestWindow(0, hOwner, 1);
    ok(hOwner && hOwned, "creation failed\n");

    /* Owned windows stay above their owner; a topmost owner promotes them
     * into the band. */
    expect_above(hOwned, hOwner);
    ok(IsTopmost(hOwned), "owned window of topmost owner not promoted\n");

    DestroyWindow(hOwned);
    DestroyWindow(hOwner);
}

static void
Test_BandOrderAfterActivation(void)
{
    HWND hTop, hNorm1, hNorm2;
    int i;

    hTop = CreateTestWindow(WS_EX_TOPMOST, NULL, 0);
    hNorm1 = CreateTestWindow(0, NULL, 1);
    hNorm2 = CreateTestWindow(0, NULL, 2);
    ok(hTop && hNorm1 && hNorm2, "creation failed\n");

    for (i = 0; i < 4; i++)
    {
        HWND hRaise = (i & 1) ? hNorm1 : hNorm2;
        HWND hOther = (i & 1) ? hNorm2 : hNorm1;

        SetWindowPos(hRaise, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        expect_above(hTop, hRaise);
        expect_above(hRaise, hOther);
        ok(!IsTopmost(hRaise), "raise promoted a normal window (iter %d)\n", i);
    }

    DestroyWindow(hNorm2);
    DestroyWindow(hNorm1);
    DestroyWindow(hTop);
}

START_TEST(TopmostBand)
{
    WNDCLASSW wc;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.lpszClassName = s_ClassName;
    RegisterClassW(&wc);

    Test_NormalNeverEntersBand();
    Test_TopmostOverNormals();
    Test_TopmostToggle();
    Test_OwnedOfTopmostOwner();
    Test_BandOrderAfterActivation();

    UnregisterClassW(s_ClassName, GetModuleHandleW(NULL));
}
