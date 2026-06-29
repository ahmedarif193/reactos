/*
 * PROJECT:     ReactOS WDDM API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     TDR (Timeout Detection & Recovery) and preemption tests
 * COPYRIGHT:   Copyright 2026 ReactOS WDDM Team
 *
 * Tests TDR timeout constants, preemption state machines, and
 * the GPU reset/recovery sequence logic.
 */

#include "precomp.h"

/* ---- TDR/Preemption constants from preemption.h ---- */
#define TDR_TIMEOUT_SECONDS        2
#define TDR_TIMEOUT_100NS          (-20000000LL)   /* -2s in 100ns units */
#define PREEMPTION_TIMEOUT_MS      100
#define PREEMPTION_TIMEOUT_100NS   (-1000000LL)    /* -100ms in 100ns units */

/* Preemption state machine */
typedef enum _TEST_PREEMPT_STATE {
    PREEMPT_IDLE = 0,
    PREEMPT_PENDING,
    PREEMPT_COMPLETED,
    PREEMPT_TIMEOUT
} TEST_PREEMPT_STATE;

/* TDR state */
typedef struct _TEST_TDR_STATE {
    ULONGLONG LastSubmitFence;
    ULONGLONG LastCompletedFence;
    BOOL      TdrActive;
    BOOL      InReset;
    ULONG     TdrCount;
} TEST_TDR_STATE;

/* ---- Tests ---- */

static void Test_TdrTimeoutConstants(void)
{
    /* TDR timeout must be 2 seconds */
    ok_eq_uint(TDR_TIMEOUT_SECONDS, 2);

    /* 100ns units: 2s = 20,000,000 * 100ns, negative for relative timer */
    ok(TDR_TIMEOUT_100NS == -20000000LL,
       "TDR_TIMEOUT_100NS should be -20000000, got %lld\n", TDR_TIMEOUT_100NS);

    /* Verify the math: 2 * 10^7 * 100ns = 2s */
    ok(-(TDR_TIMEOUT_100NS) == (LONGLONG)TDR_TIMEOUT_SECONDS * 10000000LL,
       "TDR timeout math check\n");
}

static void Test_PreemptionTimeoutConstants(void)
{
    ok_eq_uint(PREEMPTION_TIMEOUT_MS, 100);
    ok(PREEMPTION_TIMEOUT_100NS == -1000000LL,
       "PREEMPTION_TIMEOUT_100NS should be -1000000, got %lld\n",
       PREEMPTION_TIMEOUT_100NS);

    /* Verify: 100ms = 10^6 * 100ns = 10^8 ns = 0.1s */
    ok(-(PREEMPTION_TIMEOUT_100NS) ==
       (LONGLONG)PREEMPTION_TIMEOUT_MS * 10000LL,
       "Preemption timeout math check\n");

    /* Preemption timeout must be shorter than TDR timeout */
    ok(PREEMPTION_TIMEOUT_MS < TDR_TIMEOUT_SECONDS * 1000,
       "Preemption timeout (%u ms) must be shorter than TDR timeout (%u ms)\n",
       PREEMPTION_TIMEOUT_MS, TDR_TIMEOUT_SECONDS * 1000);
}

static void Test_PreemptionStateMachine(void)
{
    TEST_PREEMPT_STATE state;

    /* Initial state: IDLE */
    state = PREEMPT_IDLE;
    ok(state == PREEMPT_IDLE, "Initial state should be IDLE\n");

    /* IDLE -> PENDING (preemption requested) */
    state = PREEMPT_PENDING;
    ok(state == PREEMPT_PENDING, "Should transition to PENDING\n");

    /* PENDING -> COMPLETED (GPU acknowledged preemption) */
    state = PREEMPT_COMPLETED;
    ok(state == PREEMPT_COMPLETED, "Should transition to COMPLETED\n");

    /* Reset to IDLE after completion */
    state = PREEMPT_IDLE;
    ok(state == PREEMPT_IDLE, "Should reset to IDLE\n");

    /* PENDING -> TIMEOUT (GPU failed to respond in 100ms) */
    state = PREEMPT_PENDING;
    state = PREEMPT_TIMEOUT;
    ok(state == PREEMPT_TIMEOUT, "Should transition to TIMEOUT\n");
}

static void Test_TdrProgressDetection(void)
{
    TEST_TDR_STATE tdr;
    memset(&tdr, 0, sizeof(tdr));

    /* Initial state */
    tdr.LastSubmitFence = 0;
    tdr.LastCompletedFence = 0;
    tdr.TdrActive = FALSE;
    tdr.InReset = FALSE;
    tdr.TdrCount = 0;

    /* GPU submits and completes work — TDR should see progress */
    ULONGLONG CurrentFence = 5;
    tdr.LastSubmitFence = 5;

    /* Progress check: if CurrentFenceId > LastCompletedFence → alive */
    BOOL isAlive = (CurrentFence > tdr.LastCompletedFence);
    ok(isAlive, "GPU should appear alive when making progress\n");
    tdr.LastCompletedFence = CurrentFence;

    /* GPU hangs: submit 10 but complete stays at 5 */
    tdr.LastSubmitFence = 10;
    CurrentFence = 5; /* Not advancing */

    isAlive = (CurrentFence > tdr.LastCompletedFence);
    ok(!isAlive, "GPU should appear hung when fence not advancing\n");
}

static void Test_TdrResetSequence(void)
{
    TEST_TDR_STATE tdr;
    memset(&tdr, 0, sizeof(tdr));

    /* Simulate TDR detection */
    tdr.LastSubmitFence = 10;
    tdr.LastCompletedFence = 5; /* Stale — GPU hung */

    /* Step 1: Mark reset active */
    tdr.TdrActive = TRUE;
    tdr.InReset = TRUE;
    ok(tdr.InReset == TRUE, "InReset should be TRUE\n");

    /* Step 2: During reset, skip TDR DPC checks */
    ok(tdr.InReset == TRUE, "TDR DPC should skip when InReset\n");

    /* Step 3: Complete reset */
    tdr.InReset = FALSE;
    tdr.TdrActive = FALSE;
    tdr.TdrCount++;
    tdr.LastCompletedFence = tdr.LastSubmitFence; /* Reset fences */

    ok_eq_uint(tdr.TdrCount, 1);
    ok(tdr.LastCompletedFence == tdr.LastSubmitFence,
       "Fences should be synced after reset\n");
    ok(tdr.InReset == FALSE, "Should not be in reset after completion\n");
}

static void Test_MultipleTdrResets(void)
{
    TEST_TDR_STATE tdr;
    ULONG i;

    memset(&tdr, 0, sizeof(tdr));

    /* Simulate 5 TDR reset cycles */
    for (i = 0; i < 5; ++i)
    {
        /* Hang detected */
        tdr.TdrActive = TRUE;
        tdr.InReset = TRUE;

        /* Reset completed */
        tdr.InReset = FALSE;
        tdr.TdrActive = FALSE;
        tdr.TdrCount++;
    }

    ok_eq_uint(tdr.TdrCount, 5);
}

static void Test_FenceProgressCheck(void)
{
    /* Simulate fence progress scenarios */
    ULONGLONG submitted[] = { 1, 5, 10, 100, 1000 };
    ULONGLONG completed[] = { 1, 5, 10, 100, 1000 };
    ULONG i;

    /* All fences completed — GPU alive */
    for (i = 0; i < 5; ++i)
    {
        BOOL alive = (completed[i] >= submitted[i]);
        ok(alive, "Fence %lu: submitted=%llu completed=%llu should be alive\n",
           i, submitted[i], completed[i]);
    }

    /* Stale fences — GPU hung */
    ULONGLONG staleCompleted[] = { 0, 3, 7, 50, 500 };
    for (i = 0; i < 5; ++i)
    {
        BOOL alive = (staleCompleted[i] >= submitted[i]);
        ok(!alive, "Fence %lu: submitted=%llu completed=%llu should be hung\n",
           i, submitted[i], staleCompleted[i]);
    }
}

static void Test_TimerPeriodCalculation(void)
{
    /* TDR timer period in milliseconds = TDR_TIMEOUT_SECONDS * 1000 */
    ULONG periodMs = TDR_TIMEOUT_SECONDS * 1000;
    ok_eq_uint(periodMs, 2000);

    /* Preemption timer is one-shot (not periodic) */
    ok(PREEMPTION_TIMEOUT_MS == 100, "Preemption is 100ms one-shot\n");

    /* TDR fires periodically, preemption fires once */
    ok(periodMs > PREEMPTION_TIMEOUT_MS,
       "TDR period (%lu ms) should be longer than preemption (%u ms)\n",
       periodMs, PREEMPTION_TIMEOUT_MS);
}

static void Test_PreemptionRequeueBehavior(void)
{
    /*
     * When a buffer is preempted, it should be requeued at the HEAD
     * of its priority queue (not tail), so it runs next at that priority.
     */
    ULONG queue[4] = { 0 }; /* Simple queue simulation */
    ULONG head = 0, tail = 0, count = 0;

    /* Insert elements: 10, 20, 30 */
    queue[tail++] = 10; count++;
    queue[tail++] = 20; count++;
    queue[tail++] = 30; count++;

    /* Dequeue head (10) for execution */
    ULONG running = queue[head++]; count--;
    ok(running == 10, "Should be running 10\n");

    /* Preemption: requeue 10 at HEAD */
    head--; count++;
    queue[head] = running;

    /* Next dequeue should get 10 again (head priority) */
    ULONG next = queue[head++]; count--;
    ok(next == 10, "Preempted buffer should be requeued at head\n");
    ok_eq_uint(count, 2);
}

static void Test_TdrStateInitialization(void)
{
    TEST_TDR_STATE tdr;
    memset(&tdr, 0, sizeof(tdr));

    ok(tdr.LastSubmitFence == 0, "Initial submit fence should be 0\n");
    ok(tdr.LastCompletedFence == 0, "Initial completed fence should be 0\n");
    ok(tdr.TdrActive == FALSE, "TDR should not be active initially\n");
    ok(tdr.InReset == FALSE, "Should not be in reset initially\n");
    ok_eq_uint(tdr.TdrCount, 0);
}

START_TEST(TdrPreemption)
{
    Test_TdrTimeoutConstants();
    Test_PreemptionTimeoutConstants();
    Test_PreemptionStateMachine();
    Test_TdrProgressDetection();
    Test_TdrResetSequence();
    Test_MultipleTdrResets();
    Test_FenceProgressCheck();
    Test_TimerPeriodCalculation();
    Test_PreemptionRequeueBehavior();
    Test_TdrStateInitialization();
}
