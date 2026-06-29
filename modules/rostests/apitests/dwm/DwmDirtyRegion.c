/*
 * PROJECT:     ReactOS DWM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM dirty region tracking tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "precomp.h"

/* Replicate DWM_DIRTY_REGION from dwm.h */
typedef struct _TEST_DWM_DIRTY_REGION
{
    RECT    rcBounds;
    BOOL    HasDirty;
    BOOL    FullRecomposite;
} TEST_DWM_DIRTY_REGION;

/* Replicate DwmMarkDirty */
static void TestMarkDirty(TEST_DWM_DIRTY_REGION *pDirty, PRECT pRect)
{
    if (!pDirty || !pRect)
        return;

    if (!pDirty->HasDirty)
    {
        pDirty->rcBounds = *pRect;
        pDirty->HasDirty = TRUE;
    }
    else
    {
        if (pRect->left < pDirty->rcBounds.left)
            pDirty->rcBounds.left = pRect->left;
        if (pRect->top < pDirty->rcBounds.top)
            pDirty->rcBounds.top = pRect->top;
        if (pRect->right > pDirty->rcBounds.right)
            pDirty->rcBounds.right = pRect->right;
        if (pRect->bottom > pDirty->rcBounds.bottom)
            pDirty->rcBounds.bottom = pRect->bottom;
    }
}

/* Replicate DwmClearDirty */
static void TestClearDirty(TEST_DWM_DIRTY_REGION *pDirty)
{
    if (!pDirty)
        return;

    memset(&pDirty->rcBounds, 0, sizeof(pDirty->rcBounds));
    pDirty->HasDirty = FALSE;
    pDirty->FullRecomposite = FALSE;
}

static void Test_InitialState(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    memset(&dirty, 0, sizeof(dirty));

    ok(dirty.HasDirty == FALSE, "Initial HasDirty should be FALSE\n");
    ok(dirty.FullRecomposite == FALSE, "Initial FullRecomposite should be FALSE\n");
    ok_eq_long(dirty.rcBounds.left, 0);
    ok_eq_long(dirty.rcBounds.top, 0);
    ok_eq_long(dirty.rcBounds.right, 0);
    ok_eq_long(dirty.rcBounds.bottom, 0);
}

static void Test_SingleDirtyRect(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT r = { 10, 20, 100, 200 };

    memset(&dirty, 0, sizeof(dirty));
    TestMarkDirty(&dirty, &r);

    ok(dirty.HasDirty == TRUE, "HasDirty should be TRUE after marking\n");
    ok_eq_long(dirty.rcBounds.left, 10);
    ok_eq_long(dirty.rcBounds.top, 20);
    ok_eq_long(dirty.rcBounds.right, 100);
    ok_eq_long(dirty.rcBounds.bottom, 200);
}

static void Test_TwoDirtyRects(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT r1 = { 10, 20, 100, 200 };
    RECT r2 = { 50, 10, 200, 150 };

    memset(&dirty, 0, sizeof(dirty));
    TestMarkDirty(&dirty, &r1);
    TestMarkDirty(&dirty, &r2);

    /* Bounding box should be union of r1 and r2 */
    ok_eq_long(dirty.rcBounds.left, 10);    /* min(10, 50) */
    ok_eq_long(dirty.rcBounds.top, 10);     /* min(20, 10) */
    ok_eq_long(dirty.rcBounds.right, 200);  /* max(100, 200) */
    ok_eq_long(dirty.rcBounds.bottom, 200); /* max(200, 150) */
}

static void Test_ManyDirtyRects(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT rects[] = {
        { 0, 0, 10, 10 },
        { 100, 100, 200, 200 },
        { 50, 50, 150, 150 },
        { 0, 500, 800, 600 },
        { 300, 0, 400, 300 },
    };
    ULONG i;

    memset(&dirty, 0, sizeof(dirty));
    for (i = 0; i < sizeof(rects)/sizeof(rects[0]); ++i)
        TestMarkDirty(&dirty, &rects[i]);

    ok(dirty.HasDirty == TRUE, "HasDirty after many rects\n");
    ok_eq_long(dirty.rcBounds.left, 0);     /* min of all lefts */
    ok_eq_long(dirty.rcBounds.top, 0);      /* min of all tops */
    ok_eq_long(dirty.rcBounds.right, 800);  /* max of all rights */
    ok_eq_long(dirty.rcBounds.bottom, 600); /* max of all bottoms */
}

static void Test_ClearDirty(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT r = { 10, 20, 100, 200 };

    memset(&dirty, 0, sizeof(dirty));
    TestMarkDirty(&dirty, &r);
    dirty.FullRecomposite = TRUE;

    TestClearDirty(&dirty);

    ok(dirty.HasDirty == FALSE, "HasDirty should be FALSE after clear\n");
    ok(dirty.FullRecomposite == FALSE, "FullRecomposite should be FALSE after clear\n");
    ok_eq_long(dirty.rcBounds.left, 0);
    ok_eq_long(dirty.rcBounds.top, 0);
    ok_eq_long(dirty.rcBounds.right, 0);
    ok_eq_long(dirty.rcBounds.bottom, 0);
}

static void Test_ClearThenMark(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT r1 = { 10, 20, 100, 200 };
    RECT r2 = { 50, 60, 80, 90 };

    memset(&dirty, 0, sizeof(dirty));

    TestMarkDirty(&dirty, &r1);
    TestClearDirty(&dirty);

    /* Mark again after clear */
    TestMarkDirty(&dirty, &r2);

    ok(dirty.HasDirty == TRUE, "HasDirty after re-mark\n");
    ok_eq_long(dirty.rcBounds.left, 50);
    ok_eq_long(dirty.rcBounds.top, 60);
    ok_eq_long(dirty.rcBounds.right, 80);
    ok_eq_long(dirty.rcBounds.bottom, 90);
}

static void Test_FullRecomposite(void)
{
    TEST_DWM_DIRTY_REGION dirty;

    memset(&dirty, 0, sizeof(dirty));

    /* Set FullRecomposite manually (e.g., after mode change) */
    dirty.FullRecomposite = TRUE;
    ok(dirty.FullRecomposite == TRUE, "FullRecomposite should be settable\n");

    /* HasDirty can be independent of FullRecomposite */
    ok(dirty.HasDirty == FALSE, "HasDirty should still be FALSE\n");

    TestClearDirty(&dirty);
    ok(dirty.FullRecomposite == FALSE, "Clear should reset FullRecomposite\n");
}

static void Test_NegativeCoordinates(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT r1 = { -100, -50, 100, 50 };
    RECT r2 = { -200, -200, -10, -10 };

    memset(&dirty, 0, sizeof(dirty));
    TestMarkDirty(&dirty, &r1);
    TestMarkDirty(&dirty, &r2);

    ok_eq_long(dirty.rcBounds.left, -200);
    ok_eq_long(dirty.rcBounds.top, -200);
    ok_eq_long(dirty.rcBounds.right, 100);
    ok_eq_long(dirty.rcBounds.bottom, 50);
}

static void Test_ZeroSizedRect(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT r = { 50, 50, 50, 50 }; /* Zero-area rect */

    memset(&dirty, 0, sizeof(dirty));
    TestMarkDirty(&dirty, &r);

    ok(dirty.HasDirty == TRUE, "Zero-area rect still marks dirty\n");
    ok_eq_long(dirty.rcBounds.left, 50);
    ok_eq_long(dirty.rcBounds.top, 50);
    ok_eq_long(dirty.rcBounds.right, 50);
    ok_eq_long(dirty.rcBounds.bottom, 50);
}

static void Test_FullScreenDirty(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT r = { 0, 0, 1920, 1080 };

    memset(&dirty, 0, sizeof(dirty));
    TestMarkDirty(&dirty, &r);

    ok_eq_long(dirty.rcBounds.left, 0);
    ok_eq_long(dirty.rcBounds.top, 0);
    ok_eq_long(dirty.rcBounds.right, 1920);
    ok_eq_long(dirty.rcBounds.bottom, 1080);
}

static void Test_AdjacentRects(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT r1 = { 0, 0, 100, 100 };
    RECT r2 = { 100, 0, 200, 100 };

    memset(&dirty, 0, sizeof(dirty));
    TestMarkDirty(&dirty, &r1);
    TestMarkDirty(&dirty, &r2);

    ok_eq_long(dirty.rcBounds.left, 0);
    ok_eq_long(dirty.rcBounds.top, 0);
    ok_eq_long(dirty.rcBounds.right, 200);
    ok_eq_long(dirty.rcBounds.bottom, 100);
}

static void Test_OverlappingRects(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT r1 = { 0, 0, 150, 150 };
    RECT r2 = { 50, 50, 200, 200 };

    memset(&dirty, 0, sizeof(dirty));
    TestMarkDirty(&dirty, &r1);
    TestMarkDirty(&dirty, &r2);

    ok_eq_long(dirty.rcBounds.left, 0);
    ok_eq_long(dirty.rcBounds.top, 0);
    ok_eq_long(dirty.rcBounds.right, 200);
    ok_eq_long(dirty.rcBounds.bottom, 200);
}

static void Test_ContainedRect(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT outer = { 0, 0, 500, 500 };
    RECT inner = { 100, 100, 200, 200 };

    memset(&dirty, 0, sizeof(dirty));
    TestMarkDirty(&dirty, &outer);
    TestMarkDirty(&dirty, &inner);

    /* Inner rect fully contained — bounds should not change */
    ok_eq_long(dirty.rcBounds.left, 0);
    ok_eq_long(dirty.rcBounds.top, 0);
    ok_eq_long(dirty.rcBounds.right, 500);
    ok_eq_long(dirty.rcBounds.bottom, 500);
}

static void Test_DirtyRegionStructLayout(void)
{
    ok_eq_uint(sizeof(TEST_DWM_DIRTY_REGION),
               sizeof(RECT) + sizeof(BOOL) * 2);

    ok_eq_uint(offsetof(TEST_DWM_DIRTY_REGION, rcBounds), 0);
    ok(offsetof(TEST_DWM_DIRTY_REGION, HasDirty) >= sizeof(RECT),
       "HasDirty should follow rcBounds\n");
    ok(offsetof(TEST_DWM_DIRTY_REGION, FullRecomposite) >
       offsetof(TEST_DWM_DIRTY_REGION, HasDirty),
       "FullRecomposite should follow HasDirty\n");
}

static void Test_NullPointers(void)
{
    TEST_DWM_DIRTY_REGION dirty;
    RECT r = { 10, 20, 30, 40 };

    memset(&dirty, 0, sizeof(dirty));

    /* NULL dirty pointer */
    TestMarkDirty(NULL, &r);
    ok(TRUE, "MarkDirty with NULL dirty should not crash\n");

    /* NULL rect pointer */
    TestMarkDirty(&dirty, NULL);
    ok(dirty.HasDirty == FALSE, "NULL rect should not mark dirty\n");

    /* NULL clear */
    TestClearDirty(NULL);
    ok(TRUE, "ClearDirty with NULL should not crash\n");
}

START_TEST(DwmDirtyRegion)
{
    Test_InitialState();
    Test_SingleDirtyRect();
    Test_TwoDirtyRects();
    Test_ManyDirtyRects();
    Test_ClearDirty();
    Test_ClearThenMark();
    Test_FullRecomposite();
    Test_NegativeCoordinates();
    Test_ZeroSizedRect();
    Test_FullScreenDirty();
    Test_AdjacentRects();
    Test_OverlappingRects();
    Test_ContainedRect();
    Test_DirtyRegionStructLayout();
    Test_NullPointers();
}
