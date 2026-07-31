/*
 * PROJECT:     ReactOS D3DKMT API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM mouse-path tests (software cursor vs the scan-out present)
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * A WDDM display-only adapter has no hardware pointer, so the cursor is a GDI
 * software sprite drawn into the canonical display driver's primary -- which IS
 * the shadow framebuffer the display path scans out. Every sprite blit is then
 * a real scan-out copy, and any cursor work done for no reason turns into
 * cursor flicker, disappearance and stalls. None of that is visible in a
 * screenshot and none of it reproduces at low resolution, because the defect is
 * about HOW MUCH scan-out work the mouse path costs and WHEN it happens. So
 * these tests measure cost and latency, not pixels.
 *
 * Everything here is portable: the frame counter comes from the public
 * D3DKMTQueryStatistics (D3DKMT_QUERYSTATISTICS_VIDPNSOURCE.Frame, "both by Blt
 * and Flip"), and the throughput/latency checks use plain user32. The same
 * binary is meant to run on Windows 11 ARM64 as the reference and on ReactOS as
 * the subject; a host that does not implement the statistics query still runs
 * the throughput and latency tests.
 */

#include "precomp.h"
#include <winuser.h>

typedef NTSTATUS (APIENTRY *PFN_D3DKMTQueryStatistics)(D3DKMT_QUERYSTATISTICS *);

/*
 * Window geometry.
 *
 * The window origin is deliberately smaller than the window size, so a point
 * inside the window can lie either inside or outside the rectangle
 * (0,0)-(width,height). A composited window paints into a backing surface whose
 * coordinates start at (0,0) while the cursor is tracked in screen coordinates;
 * those two points are what separates an honest per-paint cost from a cursor
 * exclusion that compares the two coordinate spaces against each other. Both
 * points stay over the test window so no other window answers with a hover
 * repaint.
 */
#define WNDX   100
#define WNDY   100
#define WNDCX  400
#define WNDCY  300
#define CURSOR_IN_X   200   /* over the window AND inside (0,0)-(cx,cy) */
#define CURSOR_IN_Y   200
#define CURSOR_OUT_X  450   /* over the window, outside (0,0)-(cx,cy)  */
#define CURSOR_OUT_Y  350

#define PAINT_WINDOW_MS   1200
#define CURSOR_MOVES        60

static BOOL
GetPrimaryVidPnSource(LUID *Luid, UINT *SourceId)
{
    PFN_D3DKMTOpenAdapterFromHdc pfnOpen;
    PFN_D3DKMTCloseAdapter pfnClose;
    D3DKMT_OPENADAPTERFROMHDC Open;
    D3DKMT_CLOSEADAPTER Close;
    HDC hDc;

    pfnOpen = (PFN_D3DKMTOpenAdapterFromHdc)LoadD3DKMTProc("D3DKMTOpenAdapterFromHdc");
    pfnClose = (PFN_D3DKMTCloseAdapter)LoadD3DKMTProc("D3DKMTCloseAdapter");
    if (pfnOpen == NULL || pfnClose == NULL)
        return FALSE;

    hDc = GetDC(NULL);
    if (hDc == NULL)
        return FALSE;

    memset(&Open, 0, sizeof(Open));
    Open.hDc = hDc;
    if (!NT_SUCCESS(pfnOpen(&Open)) || Open.hAdapter == 0)
    {
        ReleaseDC(NULL, hDc);
        return FALSE;
    }

    *Luid = Open.AdapterLuid;
    *SourceId = Open.VidPnSourceId;

    memset(&Close, 0, sizeof(Close));
    Close.hAdapter = Open.hAdapter;
    pfnClose(&Close);
    ReleaseDC(NULL, hDc);
    return TRUE;
}

/* Frames presented on the primary VidPn source since adapter start. Returns
 * FALSE when the host does not implement the query (the tests then fall back
 * to their timing-only assertions). */
static BOOL
QueryPresentedFrames(ULONG *Frame, ULONG *QueuedPresent)
{
    PFN_D3DKMTQueryStatistics pfnQuery;
    D3DKMT_QUERYSTATISTICS Query;
    LUID Luid;
    UINT SourceId;

    pfnQuery = (PFN_D3DKMTQueryStatistics)LoadD3DKMTProc("D3DKMTQueryStatistics");
    if (pfnQuery == NULL)
        return FALSE;

    if (!GetPrimaryVidPnSource(&Luid, &SourceId))
        return FALSE;

    memset(&Query, 0, sizeof(Query));
    Query.Type = D3DKMT_QUERYSTATISTICS_VIDPNSOURCE;
    Query.AdapterLuid = Luid;
    Query.QueryVidPnSource.VidPnSourceId = SourceId;

    if (!NT_SUCCESS(pfnQuery(&Query)))
        return FALSE;

    if (Frame != NULL)
        *Frame = Query.QueryResult.VidPnSourceInformation.GlobalInformation.Frame;
    if (QueuedPresent != NULL)
        *QueuedPresent = Query.QueryResult.VidPnSourceInformation.GlobalInformation.QueuedPresent;
    return TRUE;
}

static void
DrainMessages(void)
{
    MSG msg;

    while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

static LRESULT CALLBACK
CursorTestWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_PAINT)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd, &ps);

        if (hdc != NULL)
        {
            RECT rc;
            GetClientRect(hWnd, &rc);
            FillRect(hdc, &rc, (HBRUSH)(COLOR_WINDOW + 1));
            rc.left += 10; rc.top += 10; rc.right -= 10; rc.bottom -= 10;
            FillRect(hdc, &rc, (HBRUSH)GetStockObject(GRAY_BRUSH));
            EndPaint(hWnd, &ps);
        }
        return 0;
    }

    return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

static HWND
CreateCursorTestWindow(void)
{
    WNDCLASSEXW wc;
    HWND hWnd;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = CursorTestWndProc;
    wc.hInstance     = GetModuleHandleW(NULL);
    wc.hCursor       = LoadCursorW(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = L"WddmCursorTestClass";
    if (RegisterClassExW(&wc) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        return NULL;

    hWnd = CreateWindowExW(WS_EX_TOPMOST, L"WddmCursorTestClass",
                           L"WDDM cursor test", WS_POPUP | WS_VISIBLE,
                           WNDX, WNDY, WNDCX, WNDCY,
                           NULL, NULL, GetModuleHandleW(NULL), NULL);
    if (hWnd != NULL)
    {
        UpdateWindow(hWnd);
        DrainMessages();
        Sleep(300);
        DrainMessages();
    }

    return hWnd;
}

/* Park the cursor, then repaint the window for a fixed wall-clock budget.
 * Returns the number of completed paints; *Frames (optional) receives the
 * presented-frame delta over the same window. */
static ULONG
MeasurePaintThroughput(HWND hWnd, int x, int y, ULONG *Frames)
{
    ULONG Start, Now, Paints = 0;
    ULONG FrameBefore = 0, FrameAfter = 0;
    BOOL HaveStats;

    SetCursorPos(x, y);
    DrainMessages();
    Sleep(250);                 /* let the compositor settle on the parked cursor */
    DrainMessages();

    HaveStats = QueryPresentedFrames(&FrameBefore, NULL);

    Start = GetTickCount();
    for (;;)
    {
        InvalidateRect(hWnd, NULL, FALSE);
        UpdateWindow(hWnd);
        Paints++;

        Now = GetTickCount();
        if ((Now - Start) >= PAINT_WINDOW_MS)
            break;
    }

    if (HaveStats && QueryPresentedFrames(&FrameAfter, NULL) && Frames != NULL)
        *Frames = FrameAfter - FrameBefore;
    else if (Frames != NULL)
        *Frames = 0;

    return Paints;
}

/*
 * Painting a window must cost the same wherever the cursor is.
 *
 * A composited window paints into an off-screen backing surface, so it cannot
 * touch the cursor sprite on the primary and must not disturb it. If the cursor
 * exclusion runs on those paints anyway, every paint hides the sprite before
 * drawing and redraws it after: extra scan-out copies per paint, the cursor
 * missing from the panel for the whole paint, and pointer updates queued behind
 * the display device lock. Because the exclusion tests the paint rectangle
 * (backing space) against the cursor rectangle (screen space), it only fires
 * for some cursor positions -- the "sometimes" in the bug report -- and this
 * test is exactly that difference.
 */
static void
Test_PaintThroughputIndependentOfCursorPosition(void)
{
    HWND hWnd;
    POINT ptOld;
    ULONG PaintsIn, PaintsOut, FramesIn = 0, FramesOut = 0;
    ULONG Slow, Fast, Budget;

    GetCursorPos(&ptOld);

    hWnd = CreateCursorTestWindow();
    if (hWnd == NULL)
    {
        skip("Could not create the test window\n");
        return;
    }

    /* Warm up once so first-paint costs land outside the measurement. */
    MeasurePaintThroughput(hWnd, CURSOR_OUT_X, CURSOR_OUT_Y, NULL);

    PaintsIn  = MeasurePaintThroughput(hWnd, CURSOR_IN_X, CURSOR_IN_Y, &FramesIn);
    PaintsOut = MeasurePaintThroughput(hWnd, CURSOR_OUT_X, CURSOR_OUT_Y, &FramesOut);

    trace("paints in %lu ms: cursor inside %lu (frames %lu), outside %lu (frames %lu)\n",
          (ULONG)PAINT_WINDOW_MS, PaintsIn, FramesIn, PaintsOut, FramesOut);

    Slow = (PaintsIn < PaintsOut) ? PaintsIn : PaintsOut;
    Fast = (PaintsIn < PaintsOut) ? PaintsOut : PaintsIn;

    if (Fast == 0)
    {
        skip("No paints completed -- no interactive desktop?\n");
        DestroyWindow(hWnd);
        DrainMessages();
        return;
    }

    /* One spurious hide/show pair costs about three scan-out copies per paint,
     * so the defect roughly triples the per-paint cost. Allow a generous 40%
     * spread for scheduling noise. */
    Budget = (Fast * 6) / 10;

    ok(Slow >= Budget,
       "Painting must not depend on the cursor position: %lu paints with the "
       "cursor inside the backing extent vs %lu outside (need >= %lu)\n",
       PaintsIn, PaintsOut, Budget);

    DestroyWindow(hWnd);
    DrainMessages();
    SetCursorPos(ptOld.x, ptOld.y);
}

/*
 * A cursor move redraws the sprite: restore the old position, draw the new one.
 * That is a small bounded amount of scan-out work. Anything beyond it means
 * intermediate cursor states -- no cursor, or a half-composed one -- are being
 * published to the panel, which is what makes the pointer flicker while it
 * moves.
 */
static void
Test_CursorMoveFrameBudget(void)
{
    HWND hWnd;
    POINT ptOld;
    ULONG Before = 0, After = 0, Delta, Budget;
    int i;

    if (!QueryPresentedFrames(&Before, NULL))
    {
        skip("D3DKMTQueryStatistics(VIDPNSOURCE) not available on this host\n");
        return;
    }

    GetCursorPos(&ptOld);

    /* Move over a window of ours so no other window repaints on hover. */
    hWnd = CreateCursorTestWindow();
    if (hWnd == NULL)
    {
        skip("Could not create the test window\n");
        return;
    }

    SetCursorPos(WNDX + 40, WNDY + 40);
    DrainMessages();
    Sleep(250);
    DrainMessages();

    if (!QueryPresentedFrames(&Before, NULL))
    {
        skip("present statistics query failed\n");
        DestroyWindow(hWnd);
        return;
    }

    for (i = 0; i < CURSOR_MOVES; i++)
    {
        SetCursorPos(WNDX + 40 + (i % 20) * 4, WNDY + 40 + (i % 10) * 4);
        DrainMessages();
    }

    Sleep(120);
    QueryPresentedFrames(&After, NULL);

    Delta = After - Before;
    /* Hide + show is 1 + 2 sprite blits for a mono cursor and 1 + 1 for an
     * alpha one; allow one extra per move for a compositor frame landing
     * inside the loop. A hardware pointer costs none of this. */
    Budget = (ULONG)CURSOR_MOVES * 4;

    trace("cursor moves: %d, presented frames %lu\n", CURSOR_MOVES, Delta);

    ok(Delta <= Budget,
       "A cursor move must cost a bounded number of presents: %lu frames for "
       "%d moves (budget %lu)\n", Delta, CURSOR_MOVES, Budget);

    DestroyWindow(hWnd);
    DrainMessages();
    SetCursorPos(ptOld.x, ptOld.y);
}

/*
 * Moving the pointer must never block for long.
 *
 * The sprite is drawn under the display device lock, and on a display-only
 * adapter that lock is also held across scan-out copies. When the mouse path
 * pays for work it does not need, a single pointer update waits behind those
 * copies: the pointer stops for a while and then jumps, which is what the
 * "cursor freezes then catches up" report looks like. The tail of the latency
 * distribution is the measurement -- the median stays fine even when the
 * pointer visibly stutters.
 */
static void
Test_CursorMoveLatencyTail(void)
{
    HWND hWnd;
    POINT ptOld;
    LARGE_INTEGER Freq, T0, T1;
    ULONG Samples[CURSOR_MOVES];
    ULONG Max = 0, Total = 0, Mean, Budget;
    int i;

    if (!QueryPerformanceFrequency(&Freq) || Freq.QuadPart == 0)
    {
        skip("no performance counter\n");
        return;
    }

    GetCursorPos(&ptOld);

    hWnd = CreateCursorTestWindow();
    if (hWnd == NULL)
    {
        skip("Could not create the test window\n");
        return;
    }

    SetCursorPos(WNDX + 40, WNDY + 40);
    DrainMessages();
    Sleep(250);
    DrainMessages();

    for (i = 0; i < CURSOR_MOVES; i++)
    {
        QueryPerformanceCounter(&T0);
        SetCursorPos(WNDX + 40 + (i % 20) * 4, WNDY + 40 + (i % 10) * 4);
        QueryPerformanceCounter(&T1);

        Samples[i] = (ULONG)(((T1.QuadPart - T0.QuadPart) * 1000000) / Freq.QuadPart);
        Total += Samples[i];
        if (Samples[i] > Max)
            Max = Samples[i];

        DrainMessages();
        Sleep(5);
    }

    Mean = Total / CURSOR_MOVES;
    trace("SetCursorPos latency: mean %lu us, max %lu us over %d moves\n",
          Mean, Max, CURSOR_MOVES);

    /* A pointer update that takes longer than a frame is visible as a stall. */
    Budget = 16000;

    ok(Max <= Budget,
       "A pointer update must not stall: worst SetCursorPos took %lu us "
       "(mean %lu us, budget %lu us)\n", Max, Mean, Budget);

    DestroyWindow(hWnd);
    DrainMessages();
    SetCursorPos(ptOld.x, ptOld.y);
}

START_TEST(cursor)
{
    ULONG Frame = 0, Queued = 0;

    if (QueryPresentedFrames(&Frame, &Queued))
        trace("primary VidPn source: %lu frames presented, %lu queued\n", Frame, Queued);
    else
        trace("D3DKMTQueryStatistics(VIDPNSOURCE) unavailable; timing tests only\n");

    Test_PaintThroughputIndependentOfCursorPosition();
    Test_CursorMoveFrameBudget();
    Test_CursorMoveLatencyTail();
}
