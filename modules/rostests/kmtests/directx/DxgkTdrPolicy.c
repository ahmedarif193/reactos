/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Timeout detection and recovery escalation
 *
 * TDR is what keeps a wedged GPU from wedging the machine.  Escalating too
 * fast throws away a recoverable engine; never escalating leaves the display
 * frozen forever.
 */

#include <kmt_test.h>
#include "object_core.h"

static VOID TestNoTimeoutWithoutWork(VOID)
{
    DXGK_TDR_STATE State;

    { NTSTATUS Observed = DxgkTdrCoreInitialize(&State, 2000, 3); ok_eq_hex(Observed, STATUS_SUCCESS); }
    { NTSTATUS Observed = DxgkTdrCoreInitialize(&State, 0, 3); ok_eq_hex(Observed, STATUS_INVALID_PARAMETER); }
    { NTSTATUS Observed = DxgkTdrCoreInitialize(&State, 2000, 3); ok_eq_hex(Observed, STATUS_SUCCESS); }

    /* An idle GPU is not a hung GPU, however long it stays idle. */
    { LONG Observed = DxgkTdrCoreTick(&State, 10000); ok_eq_long(Observed, (LONG)DxgkTdrActionNone); }
    { LONG Observed = DxgkTdrCoreTick(&State, 10000); ok_eq_long(Observed, (LONG)DxgkTdrActionNone); }
}

static VOID TestTimeoutDetection(VOID)
{
    DXGK_TDR_STATE State;

    { NTSTATUS Observed = DxgkTdrCoreInitialize(&State, 2000, 3); ok_eq_hex(Observed, STATUS_SUCCESS); }
    DxgkTdrCoreBeginPacket(&State);

    /* Work in progress under the timeout is left alone. */
    { LONG Observed = DxgkTdrCoreTick(&State, 500); ok_eq_long(Observed, (LONG)DxgkTdrActionNone); }
    { LONG Observed = DxgkTdrCoreTick(&State, 500); ok_eq_long(Observed, (LONG)DxgkTdrActionNone); }
    { LONG Observed = DxgkTdrCoreTick(&State, 999); ok_eq_long(Observed, (LONG)DxgkTdrActionNone); }

    /* Crossing the timeout starts with the gentlest recovery. */
    { LONG Observed = DxgkTdrCoreTick(&State, 1); ok_eq_long(Observed, (LONG)DxgkTdrActionPreempt); }

    /* Completion stops the clock and clears the escalation ladder. */
    DxgkTdrCoreCompletePacket(&State);
    { LONG Observed = DxgkTdrCoreTick(&State, 10000); ok_eq_long(Observed, (LONG)DxgkTdrActionNone); }
}

static VOID TestEscalation(VOID)
{
    DXGK_TDR_STATE State;

    /* Cap of three resets: preempt, reset engine, reset adapter, then give up. */
    { NTSTATUS Observed = DxgkTdrCoreInitialize(&State, 1000, 3); ok_eq_hex(Observed, STATUS_SUCCESS); }
    DxgkTdrCoreBeginPacket(&State);

    { LONG Observed = DxgkTdrCoreTick(&State, 1000); ok_eq_long(Observed, (LONG)DxgkTdrActionPreempt); }
    DxgkTdrCoreNoteResetOutcome(&State, TRUE);

    /* Preempting did not help, so escalate to resetting the engine. */
    { LONG Observed = DxgkTdrCoreTick(&State, 1000); ok_eq_long(Observed, (LONG)DxgkTdrActionResetEngine); }
    DxgkTdrCoreNoteResetOutcome(&State, TRUE);

    /* Still stuck: take the whole adapter. */
    { LONG Observed = DxgkTdrCoreTick(&State, 1000); ok_eq_long(Observed, (LONG)DxgkTdrActionResetAdapter); }
    DxgkTdrCoreNoteResetOutcome(&State, TRUE);

    /* Resetting has stopped helping, so the adapter is removed rather than
     * being reset in a loop forever. */
    { LONG Observed = DxgkTdrCoreTick(&State, 1000); ok_eq_long(Observed, (LONG)DxgkTdrActionRemoveAdapter); }
}

static VOID TestProgressResetsEscalation(VOID)
{
    DXGK_TDR_STATE State;

    { NTSTATUS Observed = DxgkTdrCoreInitialize(&State, 1000, 4); ok_eq_hex(Observed, STATUS_SUCCESS); }
    DxgkTdrCoreBeginPacket(&State);
    { LONG Observed = DxgkTdrCoreTick(&State, 1000); ok_eq_long(Observed, (LONG)DxgkTdrActionPreempt); }
    DxgkTdrCoreNoteResetOutcome(&State, TRUE);
    { LONG Observed = DxgkTdrCoreTick(&State, 1000); ok_eq_long(Observed, (LONG)DxgkTdrActionResetEngine); }

    /*
     * The GPU finished something, so it is making progress.  The next hang
     * must start from the gentlest step again rather than continuing to
     * escalate against a working device.
     */
    DxgkTdrCoreCompletePacket(&State);
    DxgkTdrCoreBeginPacket(&State);
    { LONG Observed = DxgkTdrCoreTick(&State, 1000); ok_eq_long(Observed, (LONG)DxgkTdrActionPreempt); }
}

static VOID TestFailedResetEscalatesFaster(VOID)
{
    DXGK_TDR_STATE State;

    { NTSTATUS Observed = DxgkTdrCoreInitialize(&State, 1000, 0); ok_eq_hex(Observed, STATUS_SUCCESS); }
    DxgkTdrCoreBeginPacket(&State);
    { LONG Observed = DxgkTdrCoreTick(&State, 1000); ok_eq_long(Observed, (LONG)DxgkTdrActionPreempt); }

    /* A reset that failed must not be retried at the same level; the next
     * tick moves past the step that just did not work. */
    DxgkTdrCoreNoteResetOutcome(&State, FALSE);
    { LONG Observed = DxgkTdrCoreTick(&State, 1000); ok_eq_long(Observed, (LONG)DxgkTdrActionResetAdapter); }
}

START_TEST(DxgkTdrPolicy)
{
    TestNoTimeoutWithoutWork();
    TestTimeoutDetection();
    TestEscalation();
    TestProgressResetsEscalation();
    TestFailedResetEscalatesFaster();
}

/* EOF */
