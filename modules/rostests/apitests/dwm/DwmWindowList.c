/*
 * PROJECT:     ReactOS DWM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DWM window list management and Z-order tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 */

#include "precomp.h"

/* Replicate DWM window entry for testing */
typedef struct _TEST_WIN_ENTRY
{
    HWND    hWnd;
    RECT    rcWindow;
    BOOL    Visible;
    UINT    ZOrder;
    PVOID   pPixels; /* simplified surface */
    struct _TEST_WIN_ENTRY *pNext;
    struct _TEST_WIN_ENTRY *pPrev;
} TEST_WIN_ENTRY;

static TEST_WIN_ENTRY *AllocEntry(HWND hWnd, LONG l, LONG t, LONG r, LONG b,
                                  BOOL visible, UINT z)
{
    TEST_WIN_ENTRY *e = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*e));
    if (e)
    {
        e->hWnd = hWnd;
        e->rcWindow.left = l;
        e->rcWindow.top = t;
        e->rcWindow.right = r;
        e->rcWindow.bottom = b;
        e->Visible = visible;
        e->ZOrder = z;
        e->pNext = NULL;
        e->pPrev = NULL;
    }
    return e;
}

static void FreeEntry(TEST_WIN_ENTRY *e)
{
    if (e) HeapFree(GetProcessHeap(), 0, e);
}

/* Insert at tail of doubly-linked list */
static void ListInsertTail(TEST_WIN_ENTRY **ppHead, TEST_WIN_ENTRY *e)
{
    if (!*ppHead)
    {
        *ppHead = e;
        e->pNext = NULL;
        e->pPrev = NULL;
    }
    else
    {
        TEST_WIN_ENTRY *tail = *ppHead;
        while (tail->pNext) tail = tail->pNext;
        tail->pNext = e;
        e->pPrev = tail;
        e->pNext = NULL;
    }
}

/* Remove entry from list */
static void ListRemove(TEST_WIN_ENTRY **ppHead, TEST_WIN_ENTRY *e)
{
    if (e->pPrev)
        e->pPrev->pNext = e->pNext;
    else
        *ppHead = e->pNext;

    if (e->pNext)
        e->pNext->pPrev = e->pPrev;

    e->pNext = NULL;
    e->pPrev = NULL;
}

/* Count entries */
static UINT ListCount(TEST_WIN_ENTRY *pHead)
{
    UINT count = 0;
    while (pHead) { ++count; pHead = pHead->pNext; }
    return count;
}

/* Find by hWnd */
static TEST_WIN_ENTRY *ListFind(TEST_WIN_ENTRY *pHead, HWND hWnd)
{
    while (pHead)
    {
        if (pHead->hWnd == hWnd)
            return pHead;
        pHead = pHead->pNext;
    }
    return NULL;
}

/* Free entire list */
static void ListFreeAll(TEST_WIN_ENTRY **ppHead)
{
    while (*ppHead)
    {
        TEST_WIN_ENTRY *next = (*ppHead)->pNext;
        FreeEntry(*ppHead);
        *ppHead = next;
    }
}

/* ---- Tests ---- */

static void Test_EmptyList(void)
{
    TEST_WIN_ENTRY *head = NULL;
    ok_eq_uint(ListCount(head), 0);
    ok(ListFind(head, (HWND)1) == NULL, "Find in empty list should return NULL\n");
}

static void Test_SingleInsert(void)
{
    TEST_WIN_ENTRY *head = NULL;
    TEST_WIN_ENTRY *e = AllocEntry((HWND)0x100, 0, 0, 640, 480, TRUE, 0);

    ListInsertTail(&head, e);
    ok_eq_uint(ListCount(head), 1);
    ok(head == e, "Head should point to the entry\n");
    ok(e->pNext == NULL, "Single entry pNext should be NULL\n");
    ok(e->pPrev == NULL, "Single entry pPrev should be NULL\n");

    ListFreeAll(&head);
}

static void Test_MultipleInserts(void)
{
    TEST_WIN_ENTRY *head = NULL;
    TEST_WIN_ENTRY *e1 = AllocEntry((HWND)1, 0, 0, 100, 100, TRUE, 0);
    TEST_WIN_ENTRY *e2 = AllocEntry((HWND)2, 100, 0, 200, 100, TRUE, 1);
    TEST_WIN_ENTRY *e3 = AllocEntry((HWND)3, 0, 100, 100, 200, TRUE, 2);

    ListInsertTail(&head, e1);
    ListInsertTail(&head, e2);
    ListInsertTail(&head, e3);

    ok_eq_uint(ListCount(head), 3);

    /* Check forward traversal */
    ok(head == e1, "Head should be e1\n");
    ok(e1->pNext == e2, "e1->pNext should be e2\n");
    ok(e2->pNext == e3, "e2->pNext should be e3\n");
    ok(e3->pNext == NULL, "e3->pNext should be NULL\n");

    /* Check backward traversal */
    ok(e3->pPrev == e2, "e3->pPrev should be e2\n");
    ok(e2->pPrev == e1, "e2->pPrev should be e1\n");
    ok(e1->pPrev == NULL, "e1->pPrev should be NULL\n");

    ListFreeAll(&head);
}

static void Test_FindByHwnd(void)
{
    TEST_WIN_ENTRY *head = NULL;
    TEST_WIN_ENTRY *e1 = AllocEntry((HWND)0x10, 0, 0, 10, 10, TRUE, 0);
    TEST_WIN_ENTRY *e2 = AllocEntry((HWND)0x20, 0, 0, 10, 10, TRUE, 1);
    TEST_WIN_ENTRY *e3 = AllocEntry((HWND)0x30, 0, 0, 10, 10, TRUE, 2);

    ListInsertTail(&head, e1);
    ListInsertTail(&head, e2);
    ListInsertTail(&head, e3);

    ok(ListFind(head, (HWND)0x10) == e1, "Find 0x10\n");
    ok(ListFind(head, (HWND)0x20) == e2, "Find 0x20\n");
    ok(ListFind(head, (HWND)0x30) == e3, "Find 0x30\n");
    ok(ListFind(head, (HWND)0x40) == NULL, "Find non-existent\n");

    ListFreeAll(&head);
}

static void Test_RemoveHead(void)
{
    TEST_WIN_ENTRY *head = NULL;
    TEST_WIN_ENTRY *e1 = AllocEntry((HWND)1, 0, 0, 10, 10, TRUE, 0);
    TEST_WIN_ENTRY *e2 = AllocEntry((HWND)2, 0, 0, 10, 10, TRUE, 1);

    ListInsertTail(&head, e1);
    ListInsertTail(&head, e2);

    ListRemove(&head, e1);
    ok_eq_uint(ListCount(head), 1);
    ok(head == e2, "Head should now be e2\n");
    ok(e2->pPrev == NULL, "New head pPrev should be NULL\n");

    FreeEntry(e1);
    ListFreeAll(&head);
}

static void Test_RemoveTail(void)
{
    TEST_WIN_ENTRY *head = NULL;
    TEST_WIN_ENTRY *e1 = AllocEntry((HWND)1, 0, 0, 10, 10, TRUE, 0);
    TEST_WIN_ENTRY *e2 = AllocEntry((HWND)2, 0, 0, 10, 10, TRUE, 1);

    ListInsertTail(&head, e1);
    ListInsertTail(&head, e2);

    ListRemove(&head, e2);
    ok_eq_uint(ListCount(head), 1);
    ok(head == e1, "Head should still be e1\n");
    ok(e1->pNext == NULL, "e1->pNext should be NULL after removing tail\n");

    FreeEntry(e2);
    ListFreeAll(&head);
}

static void Test_RemoveMiddle(void)
{
    TEST_WIN_ENTRY *head = NULL;
    TEST_WIN_ENTRY *e1 = AllocEntry((HWND)1, 0, 0, 10, 10, TRUE, 0);
    TEST_WIN_ENTRY *e2 = AllocEntry((HWND)2, 0, 0, 10, 10, TRUE, 1);
    TEST_WIN_ENTRY *e3 = AllocEntry((HWND)3, 0, 0, 10, 10, TRUE, 2);

    ListInsertTail(&head, e1);
    ListInsertTail(&head, e2);
    ListInsertTail(&head, e3);

    ListRemove(&head, e2);
    ok_eq_uint(ListCount(head), 2);
    ok(e1->pNext == e3, "e1->pNext should be e3 after removing e2\n");
    ok(e3->pPrev == e1, "e3->pPrev should be e1 after removing e2\n");

    FreeEntry(e2);
    ListFreeAll(&head);
}

static void Test_RemoveAll(void)
{
    TEST_WIN_ENTRY *head = NULL;
    TEST_WIN_ENTRY *e1 = AllocEntry((HWND)1, 0, 0, 10, 10, TRUE, 0);
    TEST_WIN_ENTRY *e2 = AllocEntry((HWND)2, 0, 0, 10, 10, TRUE, 1);

    ListInsertTail(&head, e1);
    ListInsertTail(&head, e2);

    ListFreeAll(&head);
    ok(head == NULL, "Head should be NULL after freeing all\n");
    ok_eq_uint(ListCount(head), 0);
}

static void Test_ZOrderProperty(void)
{
    TEST_WIN_ENTRY *head = NULL;
    TEST_WIN_ENTRY *entries[5];
    UINT i;

    for (i = 0; i < 5; ++i)
    {
        entries[i] = AllocEntry((HWND)(ULONG_PTR)(i + 1),
                                i * 100, 0, (i + 1) * 100, 100,
                                TRUE, i);
        ListInsertTail(&head, entries[i]);
    }

    /* Verify Z-order is sequential */
    TEST_WIN_ENTRY *cur = head;
    UINT expectedZ = 0;
    while (cur)
    {
        ok_eq_uint(cur->ZOrder, expectedZ);
        ++expectedZ;
        cur = cur->pNext;
    }

    ListFreeAll(&head);
}

static void Test_VisibilityFilter(void)
{
    TEST_WIN_ENTRY *head = NULL;
    TEST_WIN_ENTRY *e1 = AllocEntry((HWND)1, 0, 0, 100, 100, TRUE, 0);
    TEST_WIN_ENTRY *e2 = AllocEntry((HWND)2, 0, 0, 100, 100, FALSE, 1); /* invisible */
    TEST_WIN_ENTRY *e3 = AllocEntry((HWND)3, 0, 0, 100, 100, TRUE, 2);

    ListInsertTail(&head, e1);
    ListInsertTail(&head, e2);
    ListInsertTail(&head, e3);

    /* Count visible entries */
    UINT visibleCount = 0;
    TEST_WIN_ENTRY *cur = head;
    while (cur)
    {
        if (cur->Visible) ++visibleCount;
        cur = cur->pNext;
    }
    ok_eq_uint(visibleCount, 2);

    ListFreeAll(&head);
}

static void Test_WindowRects(void)
{
    TEST_WIN_ENTRY *e = AllocEntry((HWND)1, 10, 20, 310, 260, TRUE, 0);

    ok_eq_long(e->rcWindow.left, 10);
    ok_eq_long(e->rcWindow.top, 20);
    ok_eq_long(e->rcWindow.right, 310);
    ok_eq_long(e->rcWindow.bottom, 260);

    /* Width and height */
    LONG width = e->rcWindow.right - e->rcWindow.left;
    LONG height = e->rcWindow.bottom - e->rcWindow.top;
    ok_eq_long(width, 300);
    ok_eq_long(height, 240);

    FreeEntry(e);
}

static void Test_LargeWindowList(void)
{
    TEST_WIN_ENTRY *head = NULL;
    UINT i;

    /* Create 100 window entries */
    for (i = 0; i < 100; ++i)
    {
        TEST_WIN_ENTRY *e = AllocEntry((HWND)(ULONG_PTR)(i + 1),
                                       0, 0, 100, 100, TRUE, i);
        ok(e != NULL, "AllocEntry %u failed\n", i);
        if (e) ListInsertTail(&head, e);
    }

    ok_eq_uint(ListCount(head), 100);

    /* Find last entry */
    TEST_WIN_ENTRY *found = ListFind(head, (HWND)100);
    ok(found != NULL, "Should find entry 100\n");
    if (found) ok_eq_uint(found->ZOrder, 99);

    ListFreeAll(&head);
    ok(head == NULL, "Head should be NULL after cleanup\n");
}

static void Test_InsertAfterRemove(void)
{
    TEST_WIN_ENTRY *head = NULL;
    TEST_WIN_ENTRY *e1 = AllocEntry((HWND)1, 0, 0, 10, 10, TRUE, 0);
    TEST_WIN_ENTRY *e2 = AllocEntry((HWND)2, 0, 0, 10, 10, TRUE, 1);
    TEST_WIN_ENTRY *e3 = AllocEntry((HWND)3, 0, 0, 10, 10, TRUE, 2);

    ListInsertTail(&head, e1);
    ListInsertTail(&head, e2);
    ListRemove(&head, e2);
    FreeEntry(e2);

    /* Insert new entry after removal */
    ListInsertTail(&head, e3);

    ok_eq_uint(ListCount(head), 2);
    ok(head == e1, "Head should be e1\n");
    ok(e1->pNext == e3, "e1->pNext should be e3\n");
    ok(e3->pPrev == e1, "e3->pPrev should be e1\n");

    ListFreeAll(&head);
}

START_TEST(DwmWindowList)
{
    Test_EmptyList();
    Test_SingleInsert();
    Test_MultipleInserts();
    Test_FindByHwnd();
    Test_RemoveHead();
    Test_RemoveTail();
    Test_RemoveMiddle();
    Test_RemoveAll();
    Test_ZOrderProperty();
    Test_VisibilityFilter();
    Test_WindowRects();
    Test_LargeWindowList();
    Test_InsertAfterRemove();
}
