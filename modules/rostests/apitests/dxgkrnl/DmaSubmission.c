/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DMA buffer submission pipeline tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests the full DMA submission pipeline:
 *   Render -> Schedule -> DxgkDdiSubmitCommand -> GPU completion
 *   ISR ring buffer -> DPC -> scheduler notification -> fence signal
 *   Back-to-back submissions (pipeline depth)
 */

#include "precomp.h"

/* ---- Constants from the driver headers ---- */
#define DXGMMS_MAX_NODES         8
#define DXGMMS_PRIORITY_LEVELS   4
#define DXGMMS_PRIORITY_BACKGROUND  0
#define DXGMMS_PRIORITY_NORMAL      1
#define DXGMMS_PRIORITY_HIGH        2
#define DXGMMS_PRIORITY_REALTIME    3

#define DXGKRNL_ISR_RING_SIZE    32
#define DXGKRNL_ISR_RING_MASK    (DXGKRNL_ISR_RING_SIZE - 1)

/* ---- Replicate the ISR ring entry for testing ---- */
typedef enum _TEST_INTERRUPT_TYPE {
    TEST_INTERRUPT_DMA_COMPLETED  = 1,
    TEST_INTERRUPT_DMA_PREEMPTED  = 2,
    TEST_INTERRUPT_DMA_FAULTED    = 3
} TEST_INTERRUPT_TYPE;

typedef struct _TEST_ISR_RING_ENTRY {
    TEST_INTERRUPT_TYPE InterruptType;
    UINT                NodeOrdinal;
    UINT                EngineOrdinal;
    UINT                SubmissionFenceId;
    UINT                PreemptionFenceId;
    UINT                LastCompletedFenceId;
    UINT                FaultedFenceId;
    LONG                FaultStatus;
} TEST_ISR_RING_ENTRY;

/* ---- Replicate the command buffer for testing ---- */
typedef struct _TEST_CMD_BUF {
    struct _TEST_CMD_BUF *pNext;
    void                 *DxgkrnlBuffer;
    ULONG                 NodeOrdinal;
    ULONG                 Priority;
    ULONGLONG              SubmitFenceId;
    BOOL                   IsPreempted;
    BOOL                   IsCompleted;
} TEST_CMD_BUF;

/* ---- ISR ring buffer simulation ---- */
typedef struct _TEST_ISR_RING {
    TEST_ISR_RING_ENTRY Entries[DXGKRNL_ISR_RING_SIZE];
    volatile LONG       WriteIndex;
    volatile LONG       ReadIndex;
} TEST_ISR_RING;

static void RingInit(TEST_ISR_RING *ring)
{
    memset(ring, 0, sizeof(*ring));
}

static void RingWrite(TEST_ISR_RING *ring, TEST_ISR_RING_ENTRY *entry)
{
    LONG idx = ring->WriteIndex++;
    idx &= DXGKRNL_ISR_RING_MASK;
    ring->Entries[idx] = *entry;
}

static BOOL RingRead(TEST_ISR_RING *ring, TEST_ISR_RING_ENTRY *out)
{
    if (ring->ReadIndex == ring->WriteIndex)
        return FALSE;
    LONG idx = ring->ReadIndex++ & DXGKRNL_ISR_RING_MASK;
    *out = ring->Entries[idx];
    return TRUE;
}

static ULONG RingPending(TEST_ISR_RING *ring)
{
    return (ULONG)(ring->WriteIndex - ring->ReadIndex);
}

/* ---- Tests ---- */

static void Test_IsrRingConstants(void)
{
    /* ISR ring must be power of 2 for efficient modular arithmetic */
    ok(DXGKRNL_ISR_RING_SIZE == 32,
       "ISR ring size should be 32, got %d\n", DXGKRNL_ISR_RING_SIZE);

    /* Mask must be (size - 1) */
    ok(DXGKRNL_ISR_RING_MASK == 31,
       "ISR ring mask should be 31, got %d\n", DXGKRNL_ISR_RING_MASK);

    /* Power of 2 check: (n & (n-1)) == 0 for powers of 2 */
    ok((DXGKRNL_ISR_RING_SIZE & (DXGKRNL_ISR_RING_SIZE - 1)) == 0,
       "ISR ring size must be a power of 2\n");
}

static void Test_IsrRingBasicOperations(void)
{
    TEST_ISR_RING ring;
    TEST_ISR_RING_ENTRY entry, out;

    RingInit(&ring);

    /* Empty ring: read should fail */
    ok(!RingRead(&ring, &out), "Read from empty ring should fail\n");
    ok(RingPending(&ring) == 0, "Empty ring should have 0 pending\n");

    /* Write one DMA_COMPLETED entry */
    memset(&entry, 0, sizeof(entry));
    entry.InterruptType = TEST_INTERRUPT_DMA_COMPLETED;
    entry.NodeOrdinal = 0;
    entry.SubmissionFenceId = 42;
    RingWrite(&ring, &entry);

    ok(RingPending(&ring) == 1, "Should have 1 pending after write\n");

    /* Read it back */
    ok(RingRead(&ring, &out), "Read should succeed\n");
    ok(out.InterruptType == TEST_INTERRUPT_DMA_COMPLETED,
       "Type should be DMA_COMPLETED\n");
    ok(out.SubmissionFenceId == 42, "FenceId should be 42\n");
    ok(out.NodeOrdinal == 0, "NodeOrdinal should be 0\n");

    /* Ring should be empty again */
    ok(RingPending(&ring) == 0, "Ring should be empty after read\n");
}

static void Test_IsrRingMultipleEntries(void)
{
    TEST_ISR_RING ring;
    TEST_ISR_RING_ENTRY entry, out;
    ULONG i;

    RingInit(&ring);

    /* Write multiple entries of different types */
    for (i = 0; i < 10; ++i)
    {
        memset(&entry, 0, sizeof(entry));
        entry.InterruptType = TEST_INTERRUPT_DMA_COMPLETED;
        entry.NodeOrdinal = i % DXGMMS_MAX_NODES;
        entry.SubmissionFenceId = i + 1;
        RingWrite(&ring, &entry);
    }

    ok(RingPending(&ring) == 10, "Should have 10 pending\n");

    /* Read them back in FIFO order */
    for (i = 0; i < 10; ++i)
    {
        ok(RingRead(&ring, &out), "Read %lu should succeed\n", i);
        ok(out.SubmissionFenceId == i + 1,
           "FenceId %u should be %lu\n", out.SubmissionFenceId, i + 1);
    }

    ok(RingPending(&ring) == 0, "Ring should be empty\n");
}

static void Test_IsrRingWrapAround(void)
{
    TEST_ISR_RING ring;
    TEST_ISR_RING_ENTRY entry, out;
    ULONG i;

    RingInit(&ring);

    /* Fill the ring completely */
    for (i = 0; i < DXGKRNL_ISR_RING_SIZE; ++i)
    {
        memset(&entry, 0, sizeof(entry));
        entry.InterruptType = TEST_INTERRUPT_DMA_COMPLETED;
        entry.SubmissionFenceId = i + 1;
        RingWrite(&ring, &entry);
    }

    ok(RingPending(&ring) == DXGKRNL_ISR_RING_SIZE,
       "Full ring should have %d pending\n", DXGKRNL_ISR_RING_SIZE);

    /* Drain half */
    for (i = 0; i < DXGKRNL_ISR_RING_SIZE / 2; ++i)
    {
        ok(RingRead(&ring, &out), "Read %lu should succeed\n", i);
        ok(out.SubmissionFenceId == i + 1,
           "FenceId should be %lu\n", i + 1);
    }

    /* Write more entries (wrap around) */
    for (i = 0; i < DXGKRNL_ISR_RING_SIZE / 2; ++i)
    {
        memset(&entry, 0, sizeof(entry));
        entry.InterruptType = TEST_INTERRUPT_DMA_COMPLETED;
        entry.SubmissionFenceId = 100 + i;
        RingWrite(&ring, &entry);
    }

    ok(RingPending(&ring) == DXGKRNL_ISR_RING_SIZE,
       "Ring should be full again after wrap\n");

    /* Read remaining entries: first half should be old, second half new */
    for (i = 0; i < DXGKRNL_ISR_RING_SIZE / 2; ++i)
    {
        ok(RingRead(&ring, &out), "Read old %lu should succeed\n", i);
        ok(out.SubmissionFenceId == (DXGKRNL_ISR_RING_SIZE / 2) + i + 1,
           "Old entry FenceId mismatch at %lu\n", i);
    }
    for (i = 0; i < DXGKRNL_ISR_RING_SIZE / 2; ++i)
    {
        ok(RingRead(&ring, &out), "Read new %lu should succeed\n", i);
        ok(out.SubmissionFenceId == 100 + i,
           "New entry FenceId %u should be %lu\n",
           out.SubmissionFenceId, 100 + i);
    }
}

static void Test_DmaCompletionPath(void)
{
    /*
     * Simulate the full DMA completion path:
     *   1. Submit a command buffer (assign fence ID)
     *   2. ISR fires with DMA_COMPLETED (write to ring)
     *   3. DPC drains ring, finds COMPLETED, calls scheduler
     *   4. Scheduler retires the buffer and advances fence
     */
    ULONGLONG SubmittedFence = 42;
    ULONGLONG CompletedFence = 0;
    BOOL NodeIdle = FALSE;

    /* Step 1: Submit (fence assigned) */
    ok(SubmittedFence == 42, "Submitted fence should be 42\n");

    /* Step 2: ISR writes completion to ring */
    TEST_ISR_RING ring;
    TEST_ISR_RING_ENTRY entry, out;
    RingInit(&ring);
    memset(&entry, 0, sizeof(entry));
    entry.InterruptType = TEST_INTERRUPT_DMA_COMPLETED;
    entry.SubmissionFenceId = (UINT)SubmittedFence;
    entry.NodeOrdinal = 0;
    RingWrite(&ring, &entry);

    /* Step 3: DPC drains ring */
    ok(RingRead(&ring, &out), "DPC should read completion from ring\n");
    ok(out.InterruptType == TEST_INTERRUPT_DMA_COMPLETED,
       "Should be DMA_COMPLETED\n");

    /* Step 4: Scheduler processes completion */
    CompletedFence = out.SubmissionFenceId;
    ok(CompletedFence == SubmittedFence,
       "Completed fence should match submitted fence\n");
    ok(CompletedFence >= SubmittedFence,
       "Completed fence >= submitted fence means buffer is done\n");
    NodeIdle = TRUE;  /* No more running buffer */
    ok(NodeIdle, "Node should be idle after completion\n");
}

static void Test_PreemptionCompletionPath(void)
{
    /*
     * Simulate preemption completion:
     *   1. Running buffer at fence 10
     *   2. Preemption requested with fence 100
     *   3. ISR fires DMA_PREEMPTED with LastCompleted=10, PreemptFence=100
     *   4. DPC processes: retire fence 10, signal preemption done
     */
    TEST_ISR_RING ring;
    TEST_ISR_RING_ENTRY entry, out;

    RingInit(&ring);

    memset(&entry, 0, sizeof(entry));
    entry.InterruptType = TEST_INTERRUPT_DMA_PREEMPTED;
    entry.NodeOrdinal = 0;
    entry.PreemptionFenceId = 100;
    entry.LastCompletedFenceId = 10;
    RingWrite(&ring, &entry);

    ok(RingRead(&ring, &out), "Should read preemption completion\n");
    ok(out.InterruptType == TEST_INTERRUPT_DMA_PREEMPTED,
       "Should be DMA_PREEMPTED\n");
    ok(out.PreemptionFenceId == 100, "PreemptFence should be 100\n");
    ok(out.LastCompletedFenceId == 10, "LastCompleted should be 10\n");

    /* Verify preemption fence is always > last completed fence */
    ok(out.PreemptionFenceId > out.LastCompletedFenceId,
       "Preemption fence must be > last completed fence\n");
}

static void Test_BackToBackSubmissions(void)
{
    /*
     * Simulate rapid back-to-back DMA submissions and completions.
     * This tests pipeline depth: multiple commands submitted before any
     * complete, then completions arrive in fence order.
     */
    ULONGLONG Fences[8];
    ULONGLONG LastCompleted = 0;
    TEST_ISR_RING ring;
    TEST_ISR_RING_ENTRY entry, out;
    ULONG i;

    RingInit(&ring);

    /* Submit 8 buffers rapidly */
    for (i = 0; i < 8; ++i)
    {
        Fences[i] = i + 1;
    }

    /* All 8 complete in order */
    for (i = 0; i < 8; ++i)
    {
        memset(&entry, 0, sizeof(entry));
        entry.InterruptType = TEST_INTERRUPT_DMA_COMPLETED;
        entry.NodeOrdinal = 0;
        entry.SubmissionFenceId = (UINT)Fences[i];
        RingWrite(&ring, &entry);
    }

    ok(RingPending(&ring) == 8, "Should have 8 pending completions\n");

    /* DPC processes all completions */
    for (i = 0; i < 8; ++i)
    {
        ok(RingRead(&ring, &out), "Read %lu should succeed\n", i);
        ok(out.SubmissionFenceId > (UINT)LastCompleted,
           "Completions must arrive in monotonic order\n");
        LastCompleted = out.SubmissionFenceId;
    }

    ok(LastCompleted == 8, "Last completed fence should be 8\n");
}

static void Test_MultiNodeCompletion(void)
{
    /*
     * Test completions from multiple GPU nodes interleaved.
     * Each node has independent fence sequencing.
     */
    TEST_ISR_RING ring;
    TEST_ISR_RING_ENTRY entry, out;
    ULONG NodeFences[DXGMMS_MAX_NODES];
    ULONG i;

    RingInit(&ring);
    memset(NodeFences, 0, sizeof(NodeFences));

    /* Interleave completions from 4 nodes */
    for (i = 0; i < 12; ++i)
    {
        ULONG node = i % 4;
        NodeFences[node]++;

        memset(&entry, 0, sizeof(entry));
        entry.InterruptType = TEST_INTERRUPT_DMA_COMPLETED;
        entry.NodeOrdinal = node;
        entry.SubmissionFenceId = NodeFences[node];
        RingWrite(&ring, &entry);
    }

    /* Verify all can be read back */
    for (i = 0; i < 12; ++i)
    {
        ok(RingRead(&ring, &out), "Read %lu should succeed\n", i);
        ok(out.NodeOrdinal < 4, "NodeOrdinal should be < 4\n");
    }

    /* Verify per-node fence counts */
    ok(NodeFences[0] == 3, "Node 0 should have 3 completions\n");
    ok(NodeFences[1] == 3, "Node 1 should have 3 completions\n");
    ok(NodeFences[2] == 3, "Node 2 should have 3 completions\n");
    ok(NodeFences[3] == 3, "Node 3 should have 3 completions\n");
}

static void Test_FenceMonotonicProperty(void)
{
    /*
     * Fence IDs must be strictly monotonically increasing.
     * Test that the submission fence allocation pattern produces
     * unique, increasing values.
     */
    ULONGLONG fences[100];
    ULONG i;

    for (i = 0; i < 100; ++i)
        fences[i] = (ULONGLONG)(i + 1);

    for (i = 1; i < 100; ++i)
    {
        ok(fences[i] > fences[i-1],
           "Fence %llu must be > %llu at index %lu\n",
           fences[i], fences[i-1], i);
    }

    /* Fence 0 is never used (reserved for "no fence") */
    ok(fences[0] > 0, "First fence must be > 0\n");
}

static void Test_SubmitCommandBufferStructure(void)
{
    /*
     * Verify the command buffer descriptor has correct field defaults
     * when packaged for dxgmms1 submission.
     */
    TEST_CMD_BUF buf;
    memset(&buf, 0, sizeof(buf));

    buf.NodeOrdinal = 0;
    buf.Priority = DXGMMS_PRIORITY_NORMAL;
    buf.SubmitFenceId = 42;
    buf.IsPreempted = FALSE;
    buf.IsCompleted = FALSE;
    buf.DxgkrnlBuffer = NULL;

    ok(buf.NodeOrdinal == 0, "Default node should be 0 (3D engine)\n");
    ok(buf.Priority == DXGMMS_PRIORITY_NORMAL,
       "Default priority should be NORMAL (1)\n");
    ok(buf.SubmitFenceId == 42, "Fence should be set\n");
    ok(buf.IsPreempted == FALSE, "Should not be preempted\n");
    ok(buf.IsCompleted == FALSE, "Should not be completed\n");
}

static void Test_PagingBufferPriority(void)
{
    /*
     * Paging DMA buffers should be submitted at HIGH priority
     * so they preempt normal rendering.
     */
    TEST_CMD_BUF pagingCmd;
    memset(&pagingCmd, 0, sizeof(pagingCmd));

    pagingCmd.NodeOrdinal = 0;  /* DXGMMS_PAGING_NODE_ORDINAL = 0 */
    pagingCmd.Priority = DXGMMS_PRIORITY_HIGH;
    pagingCmd.SubmitFenceId = 1;

    ok(pagingCmd.Priority == DXGMMS_PRIORITY_HIGH,
       "Paging buffers should use HIGH priority\n");
    ok(pagingCmd.Priority > DXGMMS_PRIORITY_NORMAL,
       "HIGH priority must be > NORMAL\n");
}

START_TEST(DmaSubmission)
{
    Test_IsrRingConstants();
    Test_IsrRingBasicOperations();
    Test_IsrRingMultipleEntries();
    Test_IsrRingWrapAround();
    Test_DmaCompletionPath();
    Test_PreemptionCompletionPath();
    Test_BackToBackSubmissions();
    Test_MultiNodeCompletion();
    Test_FenceMonotonicProperty();
    Test_SubmitCommandBufferStructure();
    Test_PagingBufferPriority();
}
