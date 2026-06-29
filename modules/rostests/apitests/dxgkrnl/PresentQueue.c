/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Present queue circular buffer and VSync tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the present FIFO queue used for VSync-synchronized display presents.
 */

#include "precomp.h"

#define PRESENT_QUEUE_DEPTH 16

/* Present types from present.h */
#define PRESENT_TYPE_BLT        0
#define PRESENT_TYPE_FLIP       1
#define PRESENT_TYPE_COLORFILL  2

typedef struct _TEST_PRESENT_ENTRY {
    ULONG64  PresentId;
    ULONG    Type;
    ULONG    VidPnSourceId;
    UINT     hSource;
    UINT     hDestination;
    RECT     SrcRect;
    RECT     DstRect;
    UINT     Color;
    ULONG    FlipInterval; /* 0=immediate, 1=one VSync, etc. */
    UINT     hDevice;
} TEST_PRESENT_ENTRY;

typedef struct _TEST_PRESENT_QUEUE {
    ULONG    VidPnSourceId;
    TEST_PRESENT_ENTRY Entries[PRESENT_QUEUE_DEPTH];
    ULONG    Head;
    ULONG    Tail;
    ULONG    Count;
    LONGLONG NextPresentId;
    LONGLONG VBlankCount;
    LONGLONG LastPresentVBlank;
} TEST_PRESENT_QUEUE;

static void QueueInit(TEST_PRESENT_QUEUE *q, ULONG srcId)
{
    memset(q, 0, sizeof(*q));
    q->VidPnSourceId = srcId;
    q->NextPresentId = 1; /* IDs start at 1; 0 = invalid */
}

static BOOL QueueEnqueue(TEST_PRESENT_QUEUE *q, TEST_PRESENT_ENTRY *entry,
                         ULONG64 *outPresentId)
{
    if (q->Count >= PRESENT_QUEUE_DEPTH)
        return FALSE; /* Full */

    entry->PresentId = q->NextPresentId++;
    q->Entries[q->Tail] = *entry;
    q->Tail = (q->Tail + 1) % PRESENT_QUEUE_DEPTH;
    q->Count++;
    *outPresentId = entry->PresentId;
    return TRUE;
}

static TEST_PRESENT_ENTRY *QueuePeekHead(TEST_PRESENT_QUEUE *q)
{
    if (q->Count == 0)
        return NULL;
    return &q->Entries[q->Head];
}

static BOOL QueueDequeue(TEST_PRESENT_QUEUE *q, TEST_PRESENT_ENTRY *out)
{
    if (q->Count == 0)
        return FALSE;

    *out = q->Entries[q->Head];
    q->Head = (q->Head + 1) % PRESENT_QUEUE_DEPTH;
    q->Count--;
    q->LastPresentVBlank = q->VBlankCount;
    return TRUE;
}

static void QueueNotifyVSync(TEST_PRESENT_QUEUE *q)
{
    q->VBlankCount++;
}

/* ---- Tests ---- */

static void Test_QueueConstants(void)
{
    ok_eq_uint(PRESENT_QUEUE_DEPTH, 16);
    ok(PRESENT_QUEUE_DEPTH >= 4, "Queue depth should be at least 4\n");
    ok(PRESENT_QUEUE_DEPTH <= 256, "Queue depth should be at most 256\n");

    /* Present types */
    ok_eq_uint(PRESENT_TYPE_BLT, 0);
    ok_eq_uint(PRESENT_TYPE_FLIP, 1);
    ok_eq_uint(PRESENT_TYPE_COLORFILL, 2);

    ok(PRESENT_TYPE_BLT != PRESENT_TYPE_FLIP, "BLT != FLIP\n");
    ok(PRESENT_TYPE_BLT != PRESENT_TYPE_COLORFILL, "BLT != FILL\n");
    ok(PRESENT_TYPE_FLIP != PRESENT_TYPE_COLORFILL, "FLIP != FILL\n");
}

static void Test_QueueInit(void)
{
    TEST_PRESENT_QUEUE q;
    QueueInit(&q, 0);

    ok_eq_uint(q.VidPnSourceId, 0);
    ok_eq_uint(q.Head, 0);
    ok_eq_uint(q.Tail, 0);
    ok_eq_uint(q.Count, 0);
    ok(q.NextPresentId == 1, "NextPresentId should start at 1\n");
    ok(q.VBlankCount == 0, "VBlankCount should be 0\n");
    ok(q.LastPresentVBlank == 0, "LastPresentVBlank should be 0\n");
}

static void Test_SingleEnqueueDequeue(void)
{
    TEST_PRESENT_QUEUE q;
    TEST_PRESENT_ENTRY entry, out;
    ULONG64 presentId = 0;

    QueueInit(&q, 0);
    memset(&entry, 0, sizeof(entry));
    memset(&out, 0, sizeof(out));
    entry.Type = PRESENT_TYPE_BLT;
    entry.hDevice = 0x1234;

    BOOL r = QueueEnqueue(&q, &entry, &presentId);
    ok(r, "Enqueue should succeed\n");
    ok(presentId == 1, "First present ID should be 1\n");
    ok_eq_uint(q.Count, 1);
    ok_eq_uint(q.Tail, 1);

    r = QueueDequeue(&q, &out);
    ok(r, "Dequeue should succeed\n");
    ok(out.PresentId == 1, "Dequeued present ID should be 1\n");
    ok_eq_uint(out.Type, PRESENT_TYPE_BLT);
    ok_eq_uint(out.hDevice, 0x1234);
    ok_eq_uint(q.Count, 0);
}

static void Test_FIFOOrdering(void)
{
    TEST_PRESENT_QUEUE q;
    TEST_PRESENT_ENTRY entry, out;
    ULONG64 ids[5];
    ULONG i;

    QueueInit(&q, 0);
    memset(&out, 0, sizeof(out));

    /* Enqueue 5 entries */
    for (i = 0; i < 5; ++i)
    {
        memset(&entry, 0, sizeof(entry));
        entry.Type = PRESENT_TYPE_BLT;
        entry.Color = i * 100;
        QueueEnqueue(&q, &entry, &ids[i]);
    }

    /* Present IDs should be monotonically increasing */
    for (i = 1; i < 5; ++i)
        ok(ids[i] > ids[i-1], "ID %llu should be > %llu\n", ids[i], ids[i-1]);

    /* Dequeue in FIFO order */
    for (i = 0; i < 5; ++i)
    {
        QueueDequeue(&q, &out);
        ok(out.PresentId == ids[i], "FIFO order: expected %llu got %llu\n",
           ids[i], out.PresentId);
        ok_eq_uint(out.Color, i * 100);
    }
}

static void Test_QueueFull(void)
{
    TEST_PRESENT_QUEUE q;
    TEST_PRESENT_ENTRY entry;
    ULONG64 id;
    ULONG i;

    QueueInit(&q, 0);
    memset(&entry, 0, sizeof(entry));

    /* Fill to capacity */
    for (i = 0; i < PRESENT_QUEUE_DEPTH; ++i)
    {
        BOOL r = QueueEnqueue(&q, &entry, &id);
        ok(r, "Enqueue %lu should succeed\n", i);
    }
    ok_eq_uint(q.Count, PRESENT_QUEUE_DEPTH);

    /* One more should fail */
    BOOL r = QueueEnqueue(&q, &entry, &id);
    ok(!r, "Enqueue beyond capacity should fail\n");
    ok_eq_uint(q.Count, PRESENT_QUEUE_DEPTH);
}

static void Test_QueueWrapAround(void)
{
    TEST_PRESENT_QUEUE q;
    TEST_PRESENT_ENTRY entry, out;
    ULONG64 id;
    ULONG i;

    QueueInit(&q, 0);
    memset(&entry, 0, sizeof(entry));

    /* Fill and drain multiple times to exercise circular wrap */
    for (i = 0; i < PRESENT_QUEUE_DEPTH * 3; ++i)
    {
        entry.Color = i;
        BOOL r = QueueEnqueue(&q, &entry, &id);
        ok(r, "Enqueue %lu should succeed\n", i);

        r = QueueDequeue(&q, &out);
        ok(r, "Dequeue %lu should succeed\n", i);
        ok_eq_uint(out.Color, i);
    }

    ok_eq_uint(q.Count, 0);
    /* Head and Tail should have wrapped around */
    ok_eq_uint(q.Head, (PRESENT_QUEUE_DEPTH * 3) % PRESENT_QUEUE_DEPTH);
    ok_eq_uint(q.Tail, (PRESENT_QUEUE_DEPTH * 3) % PRESENT_QUEUE_DEPTH);
}

static void Test_EmptyDequeue(void)
{
    TEST_PRESENT_QUEUE q;
    TEST_PRESENT_ENTRY out;

    QueueInit(&q, 0);

    BOOL r = QueueDequeue(&q, &out);
    ok(!r, "Dequeue from empty queue should fail\n");

    ok(QueuePeekHead(&q) == NULL, "Peek on empty queue should be NULL\n");
}

static void Test_PresentIdSequence(void)
{
    TEST_PRESENT_QUEUE q;
    TEST_PRESENT_ENTRY entry;
    ULONG64 ids[20];
    ULONG i;

    QueueInit(&q, 0);
    memset(&entry, 0, sizeof(entry));

    /* Enqueue/dequeue 20 times, IDs should always increase */
    for (i = 0; i < 20; ++i)
    {
        QueueEnqueue(&q, &entry, &ids[i]);
        TEST_PRESENT_ENTRY out;
        QueueDequeue(&q, &out);
    }

    for (i = 0; i < 20; ++i)
    {
        ok(ids[i] == (ULONG64)(i + 1),
           "ID[%lu] should be %llu, got %llu\n", i, (ULONG64)(i+1), ids[i]);
    }
}

static void Test_VBlankTracking(void)
{
    TEST_PRESENT_QUEUE q;
    QueueInit(&q, 0);

    ok(q.VBlankCount == 0, "Initial VBlankCount should be 0\n");

    /* Simulate 60 VSyncs */
    ULONG i;
    for (i = 0; i < 60; ++i)
        QueueNotifyVSync(&q);

    ok(q.VBlankCount == 60, "VBlankCount should be 60\n");
}

static void Test_VSyncFlipInterval(void)
{
    TEST_PRESENT_QUEUE q;
    TEST_PRESENT_ENTRY entry, out;
    ULONG64 id;

    QueueInit(&q, 0);
    memset(&entry, 0, sizeof(entry));

    /* FlipInterval=1 means wait for 1 VSync before presenting */
    entry.FlipInterval = 1;
    QueueEnqueue(&q, &entry, &id);

    /* Check: should we present? Only if VBlankCount >= LastPresentVBlank + FlipInterval */
    TEST_PRESENT_ENTRY *head = QueuePeekHead(&q);
    ok(head != NULL, "Should have an entry\n");

    BOOL shouldPresent = (q.VBlankCount >= q.LastPresentVBlank + head->FlipInterval);
    ok(!shouldPresent, "Should NOT present before VSync\n");

    /* VSync arrives */
    QueueNotifyVSync(&q);
    shouldPresent = (q.VBlankCount >= q.LastPresentVBlank + head->FlipInterval);
    ok(shouldPresent, "Should present after 1 VSync\n");

    QueueDequeue(&q, &out);
    ok(q.LastPresentVBlank == q.VBlankCount, "LastPresentVBlank updated\n");
}

static void Test_ImmediatePresent(void)
{
    TEST_PRESENT_QUEUE q;
    TEST_PRESENT_ENTRY entry;
    ULONG64 id;

    QueueInit(&q, 0);
    memset(&entry, 0, sizeof(entry));

    /* FlipInterval=0 (IMMEDIATE) — always present */
    entry.FlipInterval = 0;
    QueueEnqueue(&q, &entry, &id);

    TEST_PRESENT_ENTRY *head = QueuePeekHead(&q);
    BOOL shouldPresent = (head->FlipInterval == 0) ||
                         (q.VBlankCount >= q.LastPresentVBlank + head->FlipInterval);
    ok(shouldPresent, "Immediate present should always be ready\n");
}

static void Test_MultiSourceQueues(void)
{
    TEST_PRESENT_QUEUE q0, q1;
    TEST_PRESENT_ENTRY entry;
    ULONG64 id0 = 0, id1 = 0;

    QueueInit(&q0, 0);
    QueueInit(&q1, 1);

    ok_eq_uint(q0.VidPnSourceId, 0);
    ok_eq_uint(q1.VidPnSourceId, 1);

    memset(&entry, 0, sizeof(entry));
    QueueEnqueue(&q0, &entry, &id0);
    QueueEnqueue(&q1, &entry, &id1);

    /* Both queues start PresentId at 1 independently */
    ok(id0 == 1, "Queue 0 first ID should be 1\n");
    ok(id1 == 1, "Queue 1 first ID should be 1\n");

    /* VBlank on source 0 should not affect source 1 */
    QueueNotifyVSync(&q0);
    ok(q0.VBlankCount == 1, "Queue 0 VBlank should be 1\n");
    ok(q1.VBlankCount == 0, "Queue 1 VBlank should still be 0\n");
}

static void Test_EntryFields(void)
{
    TEST_PRESENT_ENTRY entry;
    memset(&entry, 0, sizeof(entry));

    entry.PresentId = 42;
    entry.Type = PRESENT_TYPE_COLORFILL;
    entry.VidPnSourceId = 0;
    entry.hSource = 0x100;
    entry.hDestination = 0x200;
    entry.SrcRect.left = 0;
    entry.SrcRect.top = 0;
    entry.SrcRect.right = 1920;
    entry.SrcRect.bottom = 1080;
    entry.DstRect = entry.SrcRect;
    entry.Color = 0xFF00FF00;
    entry.FlipInterval = 1;
    entry.hDevice = 0x300;

    ok(entry.PresentId == 42, "PresentId\n");
    ok_eq_uint(entry.Type, PRESENT_TYPE_COLORFILL);
    ok_eq_uint(entry.hSource, 0x100);
    ok_eq_uint(entry.hDestination, 0x200);
    ok_eq_long(entry.SrcRect.right, 1920);
    ok_eq_long(entry.SrcRect.bottom, 1080);
    ok_eq_hex(entry.Color, 0xFF00FF00);
    ok_eq_uint(entry.FlipInterval, 1);
    ok_eq_uint(entry.hDevice, 0x300);
}

START_TEST(PresentQueue)
{
    Test_QueueConstants();
    Test_QueueInit();
    Test_SingleEnqueueDequeue();
    Test_FIFOOrdering();
    Test_QueueFull();
    Test_QueueWrapAround();
    Test_EmptyDequeue();
    Test_PresentIdSequence();
    Test_VBlankTracking();
    Test_VSyncFlipInterval();
    Test_ImmediatePresent();
    Test_MultiSourceQueues();
    Test_EntryFields();
}
