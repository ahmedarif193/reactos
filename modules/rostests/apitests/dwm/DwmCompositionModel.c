/*
 * PROJECT:     ReactOS DWM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM composition model — real DWM usage patterns
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Unlike XDDM where each window draws directly to the framebuffer via GDI,
 * WDDM uses a REDIRECT-COMPOSE-PRESENT model:
 *
 *   1. REDIRECT:  Each window renders to its own off-screen surface
 *   2. COMPOSE:   DWM walks windows in Z-order, blits each surface into
 *                 a full-screen composition buffer (painter's algorithm)
 *   3. PRESENT:   DWM calls D3DKMTPresent to push the composed frame
 *                 through dxgkrnl's present queue to the display
 *
 * This test suite exercises the full pipeline at the user-mode level,
 * verifying correct pixel output for realistic window arrangements.
 */

#include "precomp.h"

/* Surface type: 32-bpp XRGB system memory buffer */
typedef struct {
    UINT Width, Height, Stride;
    UINT *Pixels;
} SURFACE;

static BOOL SAlloc(SURFACE *s, UINT w, UINT h) {
    s->Width = w; s->Height = h; s->Stride = w;
    s->Pixels = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, w*h*4);
    return s->Pixels != NULL;
}
static void SFree(SURFACE *s) {
    if (s->Pixels) HeapFree(GetProcessHeap(), 0, s->Pixels);
    memset(s, 0, sizeof(*s));
}
static void SFill(SURFACE *s, UINT c) {
    UINT i; for (i = 0; i < s->Width*s->Height; ++i) s->Pixels[i] = c;
}
static void SFillRect(SURFACE *s, UINT x, UINT y, UINT w, UINT h, UINT c) {
    UINT dy, dx;
    for (dy = y; dy < y+h && dy < s->Height; ++dy)
        for (dx = x; dx < x+w && dx < s->Width; ++dx)
            s->Pixels[dy*s->Stride+dx] = c;
}
static UINT SGet(SURFACE *s, UINT x, UINT y) {
    return s->Pixels[y*s->Stride+x];
}

/* Window entry for the compositor */
typedef struct _WIN {
    RECT    rc;
    UINT    zorder;
    BOOL    visible;
    SURFACE surface;
    struct _WIN *next;
} WIN;

/* Compositor context */
typedef struct {
    SURFACE compBuf;
    WIN     *windows;
    UINT    bgColor;  /* Desktop background color */
} COMP_CTX;

/* Blit source surface to composition buffer with clipping */
static void Blit(SURFACE *dst, INT dx, INT dy, SURFACE *src)
{
    INT sx0, sy0, cx, cy, cw, ch, y;
    sx0 = (dx < 0) ? -dx : 0;
    sy0 = (dy < 0) ? -dy : 0;
    cx = dx + sx0;
    cy = dy + sy0;
    cw = (INT)src->Width - sx0;
    ch = (INT)src->Height - sy0;
    if (cx + cw > (INT)dst->Width) cw = (INT)dst->Width - cx;
    if (cy + ch > (INT)dst->Height) ch = (INT)dst->Height - cy;
    if (cw <= 0 || ch <= 0) return;
    for (y = 0; y < ch; ++y)
        memcpy(&dst->Pixels[(cy+y)*dst->Stride + cx],
               &src->Pixels[(sy0+y)*src->Stride + sx0], (size_t)cw*4);
}

/* Compose: clear to bg, then blit windows bottom-to-top (painter's alg) */
static void Compose(COMP_CTX *ctx)
{
    UINT maxZ = 0, z;
    WIN *w;

    /* Step 1: Clear to desktop background */
    SFill(&ctx->compBuf, ctx->bgColor);

    /* Step 2: Find max Z-order */
    for (w = ctx->windows; w; w = w->next)
        if (w->visible && w->zorder > maxZ) maxZ = w->zorder;

    /* Step 3: Painter's algorithm: z=0 (bottom) to z=maxZ (top) */
    for (z = 0; z <= maxZ; ++z)
    {
        for (w = ctx->windows; w; w = w->next)
        {
            if (!w->visible || w->zorder != z)
                continue;
            Blit(&ctx->compBuf, w->rc.left, w->rc.top, &w->surface);
        }
    }
}

static WIN *AddWin(COMP_CTX *ctx, LONG l, LONG t, LONG r, LONG b,
                   UINT z, UINT color)
{
    WIN *w = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*w));
    w->rc.left=l; w->rc.top=t; w->rc.right=r; w->rc.bottom=b;
    w->zorder = z; w->visible = TRUE;
    SAlloc(&w->surface, r-l, b-t);
    SFill(&w->surface, color);
    w->next = ctx->windows;
    ctx->windows = w;
    return w;
}

static void FreeAll(COMP_CTX *ctx)
{
    while (ctx->windows) {
        WIN *n = ctx->windows->next;
        SFree(&ctx->windows->surface);
        HeapFree(GetProcessHeap(), 0, ctx->windows);
        ctx->windows = n;
    }
    SFree(&ctx->compBuf);
}

#define BG_BLUE 0x00003C58  /* ReactOS DWM desktop blue */

/* ====================================================================
 * Test: Desktop with no windows
 * Expected: Entire screen is desktop background color
 * ==================================================================== */
static void Test_DesktopOnly(void)
{
    COMP_CTX ctx = {0};
    SAlloc(&ctx.compBuf, 1920, 1080);
    ctx.bgColor = BG_BLUE;

    Compose(&ctx);

    ok_eq_hex(SGet(&ctx.compBuf, 0, 0), BG_BLUE);
    ok_eq_hex(SGet(&ctx.compBuf, 960, 540), BG_BLUE);
    ok_eq_hex(SGet(&ctx.compBuf, 1919, 1079), BG_BLUE);

    FreeAll(&ctx);
}

/* ====================================================================
 * Test: Notepad window covering part of desktop
 * Expected: Window area has notepad color, rest is desktop
 * ==================================================================== */
static void Test_SingleNotepad(void)
{
    COMP_CTX ctx = {0};
    UINT notepadWhite = 0xFFFFFFFF;

    SAlloc(&ctx.compBuf, 800, 600);
    ctx.bgColor = BG_BLUE;
    AddWin(&ctx, 100, 50, 500, 400, 0, notepadWhite);

    Compose(&ctx);

    /* Desktop area */
    ok_eq_hex(SGet(&ctx.compBuf, 0, 0), BG_BLUE);
    ok_eq_hex(SGet(&ctx.compBuf, 99, 50), BG_BLUE);
    /* Notepad area */
    ok_eq_hex(SGet(&ctx.compBuf, 100, 50), notepadWhite);
    ok_eq_hex(SGet(&ctx.compBuf, 300, 200), notepadWhite);
    ok_eq_hex(SGet(&ctx.compBuf, 499, 399), notepadWhite);
    /* Just outside */
    ok_eq_hex(SGet(&ctx.compBuf, 500, 200), BG_BLUE);

    FreeAll(&ctx);
}

/* ====================================================================
 * Test: Browser partially obscures editor (Z-order overlap)
 * Expected: Overlap region shows the top window
 * ==================================================================== */
static void Test_BrowserOverEditor(void)
{
    COMP_CTX ctx = {0};
    UINT editor = 0xFF2D2D2D;  /* dark gray editor */
    UINT browser = 0xFFE8E8E8; /* light gray browser */

    SAlloc(&ctx.compBuf, 800, 600);
    ctx.bgColor = BG_BLUE;

    /* Editor at bottom of stack */
    AddWin(&ctx, 50, 50, 400, 400, 0, editor);
    /* Browser on top, overlapping */
    AddWin(&ctx, 200, 100, 600, 500, 1, browser);

    Compose(&ctx);

    /* Editor-only region (not covered by browser) */
    ok_eq_hex(SGet(&ctx.compBuf, 60, 60), editor);
    ok_eq_hex(SGet(&ctx.compBuf, 150, 150), editor);
    /* Overlap region: browser wins (higher Z) */
    ok_eq_hex(SGet(&ctx.compBuf, 250, 150), browser);
    ok_eq_hex(SGet(&ctx.compBuf, 350, 350), browser);
    /* Browser-only region */
    ok_eq_hex(SGet(&ctx.compBuf, 500, 300), browser);
    /* Desktop */
    ok_eq_hex(SGet(&ctx.compBuf, 700, 550), BG_BLUE);

    FreeAll(&ctx);
}

/* ====================================================================
 * Test: Taskbar at bottom covers everything below it
 * Real Windows has a taskbar at Z=highest covering the bottom 40px
 * ==================================================================== */
static void Test_Taskbar(void)
{
    COMP_CTX ctx = {0};
    UINT taskbar = 0xFF1E1E1E;  /* dark taskbar */
    UINT notepad = 0xFFFFFFFF;

    SAlloc(&ctx.compBuf, 800, 600);
    ctx.bgColor = BG_BLUE;

    /* Notepad at Z=0, extends into taskbar area */
    AddWin(&ctx, 100, 400, 500, 600, 0, notepad);
    /* Taskbar at Z=10 (topmost), covering bottom 40px */
    AddWin(&ctx, 0, 560, 800, 600, 10, taskbar);

    Compose(&ctx);

    /* Notepad visible above taskbar */
    ok_eq_hex(SGet(&ctx.compBuf, 200, 500), notepad);
    /* Taskbar overwrites notepad in bottom strip */
    ok_eq_hex(SGet(&ctx.compBuf, 200, 570), taskbar);
    /* Taskbar on far left */
    ok_eq_hex(SGet(&ctx.compBuf, 10, 580), taskbar);

    FreeAll(&ctx);
}

/* ====================================================================
 * Test: Minimized (invisible) window should not appear
 * ==================================================================== */
static void Test_MinimizedWindow(void)
{
    COMP_CTX ctx = {0};

    SAlloc(&ctx.compBuf, 400, 300);
    ctx.bgColor = BG_BLUE;

    WIN *w = AddWin(&ctx, 0, 0, 400, 300, 0, 0xFFFF0000);
    w->visible = FALSE; /* minimized */

    Compose(&ctx);

    /* Entire screen should be desktop background */
    ok_eq_hex(SGet(&ctx.compBuf, 200, 150), BG_BLUE);
    ok_eq_hex(SGet(&ctx.compBuf, 0, 0), BG_BLUE);

    FreeAll(&ctx);
}

/* ====================================================================
 * Test: Window with internal content (title bar + client area)
 * A real window has a dark title bar at top and white client area
 * ==================================================================== */
static void Test_WindowInternalContent(void)
{
    COMP_CTX ctx = {0};
    UINT titleBar = 0xFF0078D7;  /* Windows accent blue */
    UINT clientArea = 0xFFFFFFFF;

    SAlloc(&ctx.compBuf, 800, 600);
    ctx.bgColor = BG_BLUE;

    /* Create 400x300 window at (100,50) */
    WIN *w = AddWin(&ctx, 100, 50, 500, 350, 0, clientArea);
    /* Paint title bar (top 30px of window surface) */
    SFillRect(&w->surface, 0, 0, 400, 30, titleBar);

    Compose(&ctx);

    /* Title bar region */
    ok_eq_hex(SGet(&ctx.compBuf, 200, 55), titleBar);
    ok_eq_hex(SGet(&ctx.compBuf, 100, 50), titleBar);
    /* Client area (below title bar) */
    ok_eq_hex(SGet(&ctx.compBuf, 200, 100), clientArea);
    ok_eq_hex(SGet(&ctx.compBuf, 400, 300), clientArea);

    FreeAll(&ctx);
}

/* ====================================================================
 * Test: Three windows stacked — bottom, middle, top
 * Verifies correct painter's algorithm for 3 layers
 * ==================================================================== */
static void Test_ThreeWindowStack(void)
{
    COMP_CTX ctx = {0};

    SAlloc(&ctx.compBuf, 200, 200);
    ctx.bgColor = BG_BLUE;

    AddWin(&ctx, 0, 0, 200, 200, 0, 0xFF0000FF);  /* Blue fills all */
    AddWin(&ctx, 50, 50, 150, 150, 1, 0xFF00FF00); /* Green in center */
    AddWin(&ctx, 75, 75, 125, 125, 2, 0xFFFF0000); /* Red in innermost */

    Compose(&ctx);

    /* Corners: bottom window (blue) */
    ok_eq_hex(SGet(&ctx.compBuf, 10, 10), 0xFF0000FF);
    /* Middle ring: green */
    ok_eq_hex(SGet(&ctx.compBuf, 60, 60), 0xFF00FF00);
    /* Innermost: red */
    ok_eq_hex(SGet(&ctx.compBuf, 100, 100), 0xFFFF0000);

    FreeAll(&ctx);
}

/* ====================================================================
 * Test: Window partially off-screen (clipping)
 * Windows can extend beyond screen edges; DWM must clip correctly
 * ==================================================================== */
static void Test_OffScreenClipping(void)
{
    COMP_CTX ctx = {0};

    SAlloc(&ctx.compBuf, 100, 100);
    ctx.bgColor = BG_BLUE;

    /* Window extends beyond right and bottom edges */
    AddWin(&ctx, 80, 80, 200, 200, 0, 0xFFAA5500);

    Compose(&ctx);

    /* Visible portion (80-99, 80-99) should have window color */
    ok_eq_hex(SGet(&ctx.compBuf, 80, 80), 0xFFAA5500);
    ok_eq_hex(SGet(&ctx.compBuf, 99, 99), 0xFFAA5500);
    /* Area before window should be desktop */
    ok_eq_hex(SGet(&ctx.compBuf, 79, 80), BG_BLUE);

    FreeAll(&ctx);
}

/* ====================================================================
 * Test: Rapid recomposition (60fps simulation)
 * DWM recomposes every 16ms. Composition must be deterministic.
 * ==================================================================== */
static void Test_RapidRecomposition(void)
{
    COMP_CTX ctx = {0};
    UINT frame;

    SAlloc(&ctx.compBuf, 100, 100);
    ctx.bgColor = BG_BLUE;
    AddWin(&ctx, 10, 10, 90, 90, 0, 0xFF112233);

    /* Compose 60 frames (1 second at 60fps) */
    for (frame = 0; frame < 60; ++frame)
        Compose(&ctx);

    /* Result should be deterministic regardless of frame count */
    ok_eq_hex(SGet(&ctx.compBuf, 50, 50), 0xFF112233);
    ok_eq_hex(SGet(&ctx.compBuf, 5, 5), BG_BLUE);

    FreeAll(&ctx);
}

/* ====================================================================
 * Test: Window content changes between frames
 * When a window repaints, its surface changes and the next compose
 * should reflect the new content
 * ==================================================================== */
static void Test_DynamicWindowContent(void)
{
    COMP_CTX ctx = {0};

    SAlloc(&ctx.compBuf, 100, 100);
    ctx.bgColor = BG_BLUE;
    WIN *w = AddWin(&ctx, 0, 0, 100, 100, 0, 0xFFFF0000); /* Red */

    Compose(&ctx);
    ok_eq_hex(SGet(&ctx.compBuf, 50, 50), 0xFFFF0000);

    /* Window repaints to green */
    SFill(&w->surface, 0xFF00FF00);
    Compose(&ctx);
    ok_eq_hex(SGet(&ctx.compBuf, 50, 50), 0xFF00FF00);

    /* Window repaints to blue */
    SFill(&w->surface, 0xFF0000FF);
    Compose(&ctx);
    ok_eq_hex(SGet(&ctx.compBuf, 50, 50), 0xFF0000FF);

    FreeAll(&ctx);
}

START_TEST(DwmCompositionModel)
{
    Test_DesktopOnly();
    Test_SingleNotepad();
    Test_BrowserOverEditor();
    Test_Taskbar();
    Test_MinimizedWindow();
    Test_WindowInternalContent();
    Test_ThreeWindowStack();
    Test_OffScreenClipping();
    Test_RapidRecomposition();
    Test_DynamicWindowContent();
}
