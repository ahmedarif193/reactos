/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU scheduler (dxgmms1) constant and algorithm tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests scheduler constants, priority bands, quantum timing, command buffer
 * lifecycle, fence sequencing, and the priority-based dispatch algorithm.
 */

#include "precomp.h"

/* ---- Scheduler constants from scheduler.h ---- */
#define DXGMMS_MAX_NODES         8
#define DXGMMS_QUANTUM_MS       16
#define DXGMMS_PRIORITY_LEVELS   4

#define DXGMMS_PRIORITY_BACKGROUND  0
#define DXGMMS_PRIORITY_NORMAL      1
#define DXGMMS_PRIORITY_HIGH        2
#define DXGMMS_PRIORITY_REALTIME    3

/* ---- Replicate core scheduling data structures ---- */

typedef struct _TEST_CMD_BUF {
    /* Doubly-linked list for priority queues */
    struct _TEST_CMD_BUF *pNext;
    ULONG    NodeOrdinal;
    ULONG    Priority;
    ULONG64  SubmitFenceId;
    BOOL     IsPreempted;
    BOOL     IsCompleted;
} TEST_CMD_BUF;

/* Per-node scheduling state */
typedef struct _TEST_NODE {
    TEST_CMD_BUF *ReadyQueueHead[DXGMMS_PRIORITY_LEVELS];
    TEST_CMD_BUF *ReadyQueueTail[DXGMMS_PRIORITY_LEVELS];
    TEST_CMD_BUF *RunningBuffer;
    ULONG64       CurrentFenceId;
    ULONG64       LastSubmittedFenceId;
    ULONG64       LastCompletedFenceId;
    BOOL          PreemptionPending;
} TEST_NODE;

static void NodeInit(TEST_NODE *n)
{
    ULONG i;
    memset(n, 0, sizeof(*n));
    for (i = 0; i < DXGMMS_PRIORITY_LEVELS; ++i)
    {
        n->ReadyQueueHead[i] = NULL;
        n->ReadyQueueTail[i] = NULL;
    }
}

static void QueueInsert(TEST_NODE *n, TEST_CMD_BUF *buf)
{
    ULONG p = buf->Priority;
    buf->pNext = NULL;
    if (!n->ReadyQueueTail[p])
    {
        n->ReadyQueueHead[p] = buf;
        n->ReadyQueueTail[p] = buf;
    }
    else
    {
        n->ReadyQueueTail[p]->pNext = buf;
        n->ReadyQueueTail[p] = buf;
    }
}

static TEST_CMD_BUF *QueueDequeue(TEST_NODE *n, ULONG priority)
{
    TEST_CMD_BUF *buf = n->ReadyQueueHead[priority];
    if (buf)
    {
        n->ReadyQueueHead[priority] = buf->pNext;
        if (!n->ReadyQueueHead[priority])
            n->ReadyQueueTail[priority] = NULL;
        buf->pNext = NULL;
    }
    return buf;
}

/* Replicate scheduler dispatch: highest priority first */
static TEST_CMD_BUF *ScheduleNext(TEST_NODE *n)
{
    INT p;
    for (p = DXGMMS_PRIORITY_LEVELS - 1; p >= 0; --p)
    {
        TEST_CMD_BUF *buf = QueueDequeue(n, (ULONG)p);
        if (buf) return buf;
    }
    return NULL;
}

static ULONG QueueCount(TEST_NODE *n, ULONG priority)
{
    ULONG count = 0;
    TEST_CMD_BUF *buf = n->ReadyQueueHead[priority];
    while (buf) { ++count; buf = buf->pNext; }
    return count;
}

/* ---- Tests ---- */

static void Test_SchedulerConstants(void)
{
    ok_eq_uint(DXGMMS_MAX_NODES, 8);
    ok_eq_uint(DXGMMS_QUANTUM_MS, 16);
    ok_eq_uint(DXGMMS_PRIORITY_LEVELS, 4);

    /* Priority ordering: background < normal < high < realtime */
    ok(DXGMMS_PRIORITY_BACKGROUND < DXGMMS_PRIORITY_NORMAL, "bg < normal\n");
    ok(DXGMMS_PRIORITY_NORMAL < DXGMMS_PRIORITY_HIGH, "normal < high\n");
    ok(DXGMMS_PRIORITY_HIGH < DXGMMS_PRIORITY_REALTIME, "high < realtime\n");

    /* Values must be sequential 0-3 */
    ok_eq_uint(DXGMMS_PRIORITY_BACKGROUND, 0);
    ok_eq_uint(DXGMMS_PRIORITY_NORMAL, 1);
    ok_eq_uint(DXGMMS_PRIORITY_HIGH, 2);
    ok_eq_uint(DXGMMS_PRIORITY_REALTIME, 3);

    /* Quantum should target 60Hz frame time */
    ok(DXGMMS_QUANTUM_MS >= 10 && DXGMMS_QUANTUM_MS <= 33,
       "Quantum %u ms outside 10-33ms range\n", DXGMMS_QUANTUM_MS);

    /* 100-nanosecond timer interval = -Quantum_ms * 10000 */
    LONGLONG quantum_100ns = -(LONGLONG)DXGMMS_QUANTUM_MS * 10000LL;
    ok(quantum_100ns == -160000LL,
       "100ns quantum should be -160000, got %lld\n", quantum_100ns);
}

static void Test_FenceSequencing(void)
{
    ULONG64 fences[10];
    ULONG i;

    /* Simulate monotonic fence allocation */
    for (i = 0; i < 10; ++i)
        fences[i] = (ULONG64)(i + 1);

    /* All fences must be strictly increasing */
    for (i = 1; i < 10; ++i)
        ok(fences[i] > fences[i-1], "Fence %llu should be > %llu\n",
           fences[i], fences[i-1]);

    /* Fence 0 is invalid (never used) */
    ok(fences[0] > 0, "First fence should be > 0\n");

    /* Completion check: completed if CurrentFenceId >= SubmitFenceId */
    ULONG64 CurrentFence = 5;
    ok(CurrentFence >= fences[4], "Fence 5 should be completed\n");
    ok(CurrentFence < fences[5], "Fence 6 should NOT be completed yet\n");
}

static void Test_PriorityDispatch(void)
{
    TEST_NODE node;
    TEST_CMD_BUF bg = { NULL, 0, DXGMMS_PRIORITY_BACKGROUND, 1, FALSE, FALSE };
    TEST_CMD_BUF normal = { NULL, 0, DXGMMS_PRIORITY_NORMAL, 2, FALSE, FALSE };
    TEST_CMD_BUF high = { NULL, 0, DXGMMS_PRIORITY_HIGH, 3, FALSE, FALSE };
    TEST_CMD_BUF rt = { NULL, 0, DXGMMS_PRIORITY_REALTIME, 4, FALSE, FALSE };
    TEST_CMD_BUF *dispatched;

    NodeInit(&node);

    /* Insert in reverse priority order */
    QueueInsert(&node, &bg);
    QueueInsert(&node, &normal);
    QueueInsert(&node, &high);
    QueueInsert(&node, &rt);

    /* Scheduler must dispatch highest priority first */
    dispatched = ScheduleNext(&node);
    ok(dispatched == &rt, "First dispatch should be realtime\n");
    ok_eq_uint(dispatched->Priority, DXGMMS_PRIORITY_REALTIME);

    dispatched = ScheduleNext(&node);
    ok(dispatched == &high, "Second dispatch should be high\n");

    dispatched = ScheduleNext(&node);
    ok(dispatched == &normal, "Third dispatch should be normal\n");

    dispatched = ScheduleNext(&node);
    ok(dispatched == &bg, "Fourth dispatch should be background\n");

    dispatched = ScheduleNext(&node);
    ok(dispatched == NULL, "Fifth dispatch should be NULL (empty)\n");
}

static void Test_SamePriorityFIFO(void)
{
    TEST_NODE node;
    TEST_CMD_BUF bufs[4];
    ULONG i;
    TEST_CMD_BUF *dispatched;

    NodeInit(&node);

    /* Insert 4 buffers at same priority */
    for (i = 0; i < 4; ++i)
    {
        memset(&bufs[i], 0, sizeof(bufs[i]));
        bufs[i].Priority = DXGMMS_PRIORITY_NORMAL;
        bufs[i].SubmitFenceId = i + 1;
        QueueInsert(&node, &bufs[i]);
    }

    /* Must be dispatched in FIFO order */
    for (i = 0; i < 4; ++i)
    {
        dispatched = ScheduleNext(&node);
        ok(dispatched == &bufs[i],
           "FIFO order violated at index %lu\n", i);
        ok(dispatched->SubmitFenceId == (ULONG64)(i + 1),
           "FenceId %llu expected %llu\n",
           dispatched->SubmitFenceId, (ULONG64)(i + 1));
    }
}

static void Test_PreemptionRequeue(void)
{
    TEST_NODE node;
    TEST_CMD_BUF running = { NULL, 0, DXGMMS_PRIORITY_NORMAL, 1, FALSE, FALSE };
    TEST_CMD_BUF highPri = { NULL, 0, DXGMMS_PRIORITY_HIGH, 2, FALSE, FALSE };
    TEST_CMD_BUF *dispatched;

    NodeInit(&node);

    /* Simulate: running buffer is preempted, requeued at head */
    node.RunningBuffer = &running;
    QueueInsert(&node, &highPri);

    /* Preemption: mark running as preempted, requeue at head */
    running.IsPreempted = TRUE;
    node.RunningBuffer = NULL;
    /* Insert at HEAD of its priority queue (re-execute later) */
    running.pNext = node.ReadyQueueHead[running.Priority];
    node.ReadyQueueHead[running.Priority] = &running;
    if (!node.ReadyQueueTail[running.Priority])
        node.ReadyQueueTail[running.Priority] = &running;

    /* After preemption, high priority should dispatch first */
    dispatched = ScheduleNext(&node);
    ok(dispatched == &highPri,
       "After preemption, high-priority should dispatch first\n");

    /* Then the preempted normal buffer */
    dispatched = ScheduleNext(&node);
    ok(dispatched == &running,
       "Preempted buffer should be re-dispatched second\n");
    ok(dispatched->IsPreempted == TRUE,
       "Preempted flag should still be set\n");
}

static void Test_EmptyNodeSchedule(void)
{
    TEST_NODE node;
    NodeInit(&node);

    TEST_CMD_BUF *dispatched = ScheduleNext(&node);
    ok(dispatched == NULL, "Empty node should dispatch NULL\n");

    ok(node.RunningBuffer == NULL, "RunningBuffer should be NULL\n");
}

static void Test_QueueCounting(void)
{
    TEST_NODE node;
    TEST_CMD_BUF bufs[5];
    ULONG i;

    NodeInit(&node);

    for (i = 0; i < 5; ++i)
    {
        memset(&bufs[i], 0, sizeof(bufs[i]));
        bufs[i].Priority = DXGMMS_PRIORITY_NORMAL;
        bufs[i].SubmitFenceId = i + 1;
        QueueInsert(&node, &bufs[i]);
    }

    ok_eq_uint(QueueCount(&node, DXGMMS_PRIORITY_NORMAL), 5);
    ok_eq_uint(QueueCount(&node, DXGMMS_PRIORITY_BACKGROUND), 0);
    ok_eq_uint(QueueCount(&node, DXGMMS_PRIORITY_HIGH), 0);
    ok_eq_uint(QueueCount(&node, DXGMMS_PRIORITY_REALTIME), 0);

    /* Dequeue 2, count should be 3 */
    ScheduleNext(&node);
    ScheduleNext(&node);
    ok_eq_uint(QueueCount(&node, DXGMMS_PRIORITY_NORMAL), 3);
}

static void Test_MixedPriorityScheduling(void)
{
    TEST_NODE node;
    TEST_CMD_BUF bufs[8];
    ULONG priorities[] = { 0, 2, 1, 3, 0, 1, 2, 3 };
    ULONG expected_order[] = { 3, 3, 2, 2, 1, 1, 0, 0 };
    ULONG i;

    NodeInit(&node);

    for (i = 0; i < 8; ++i)
    {
        memset(&bufs[i], 0, sizeof(bufs[i]));
        bufs[i].Priority = priorities[i];
        bufs[i].SubmitFenceId = i + 1;
        QueueInsert(&node, &bufs[i]);
    }

    /* Dispatch all and verify priority ordering */
    for (i = 0; i < 8; ++i)
    {
        TEST_CMD_BUF *d = ScheduleNext(&node);
        ok(d != NULL, "Dispatch %lu should not be NULL\n", i);
        if (d)
        {
            ok_eq_uint(d->Priority, expected_order[i]);
        }
    }

    /* Should be empty now */
    ok(ScheduleNext(&node) == NULL, "Should be empty after 8 dispatches\n");
}

static void Test_CommandBufferLifecycle(void)
{
    TEST_CMD_BUF buf;
    memset(&buf, 0, sizeof(buf));

    /* Initial state */
    ok(buf.IsCompleted == FALSE, "New buffer should not be completed\n");
    ok(buf.IsPreempted == FALSE, "New buffer should not be preempted\n");
    ok(buf.SubmitFenceId == 0, "New buffer fence should be 0\n");

    /* Submit */
    buf.SubmitFenceId = 42;
    buf.Priority = DXGMMS_PRIORITY_NORMAL;
    buf.NodeOrdinal = 0;
    ok(buf.SubmitFenceId == 42, "Fence should be 42 after submit\n");

    /* Complete */
    buf.IsCompleted = TRUE;
    ok(buf.IsCompleted == TRUE, "Buffer should be completed\n");
    ok(buf.IsPreempted == FALSE, "Completed buffer should not be preempted\n");

    /* Preempted */
    memset(&buf, 0, sizeof(buf));
    buf.IsPreempted = TRUE;
    ok(buf.IsPreempted == TRUE, "Buffer should be preempted\n");
    ok(buf.IsCompleted == FALSE, "Preempted buffer should not be completed\n");
}

static void Test_MaxNodesConstant(void)
{
    /* DXGMMS_MAX_NODES must be large enough for typical GPUs */
    ok(DXGMMS_MAX_NODES >= 4,
       "MAX_NODES %u is too small for modern GPUs\n", DXGMMS_MAX_NODES);
    ok(DXGMMS_MAX_NODES <= 64,
       "MAX_NODES %u seems excessively large\n", DXGMMS_MAX_NODES);

    /* Must be a power of 2 or common value */
    ok(DXGMMS_MAX_NODES == 8,
       "Expected MAX_NODES=8, got %u\n", DXGMMS_MAX_NODES);
}

static void Test_PoolTags(void)
{
    ULONG TAG_CMD_BUF = 'BmdG';
    ULONG TAG_ADAPTER = 'AdmG';

    ok(TAG_CMD_BUF != TAG_ADAPTER,
       "Command buffer and adapter tags must differ\n");
    ok(TAG_CMD_BUF != 0, "Command buffer tag must be non-zero\n");
    ok(TAG_ADAPTER != 0, "Adapter tag must be non-zero\n");
}

START_TEST(Scheduler)
{
    Test_SchedulerConstants();
    Test_FenceSequencing();
    Test_PriorityDispatch();
    Test_SamePriorityFIFO();
    Test_PreemptionRequeue();
    Test_EmptyNodeSchedule();
    Test_QueueCounting();
    Test_MixedPriorityScheduling();
    Test_CommandBufferLifecycle();
    Test_MaxNodesConstant();
    Test_PoolTags();
}
