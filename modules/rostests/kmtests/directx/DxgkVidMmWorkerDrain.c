/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgkrnl VidMm destroy-worker drain tests
 */

#include <kmt_test.h>
#include "vidmm_worker_drain_core.h"

typedef struct _DXGK_VIDMM_WORKER_DRAIN_TEST_STATE
{
    LONG ActiveWorkers;
    BOOLEAN AdmissionBlocked;
    BOOLEAN DrainedEventSignaled;
} DXGK_VIDMM_WORKER_DRAIN_TEST_STATE, *PDXGK_VIDMM_WORKER_DRAIN_TEST_STATE;

static BOOLEAN DxgkVidMmWorkerDrainTestAdmit(_Inout_ PDXGK_VIDMM_WORKER_DRAIN_TEST_STATE State)
{
    BOOLEAN ResetEvent;
    BOOLEAN Admitted;

    Admitted = DxgkVidMmWorkerDrainCoreTryAdmitLocked(&State->ActiveWorkers, State->AdmissionBlocked, &ResetEvent);
    if (ResetEvent)
        State->DrainedEventSignaled = FALSE;
    return Admitted;
}

static BOOLEAN DxgkVidMmWorkerDrainTestRetire(_Inout_ PDXGK_VIDMM_WORKER_DRAIN_TEST_STATE State)
{
    BOOLEAN SetEvent;
    BOOLEAN Retired;

    Retired = DxgkVidMmWorkerDrainCoreRetireLocked(&State->ActiveWorkers, &SetEvent);
    if (SetEvent)
        State->DrainedEventSignaled = TRUE;
    return Retired;
}

static VOID DxgkVidMmWorkerDrainTestInitialize(_Out_ PDXGK_VIDMM_WORKER_DRAIN_TEST_STATE State)
{
    RtlZeroMemory(State, sizeof(*State));
    State->DrainedEventSignaled = TRUE;
}

static VOID DxgkVidMmWorkerDrainTestTransitions(VOID)
{
    DXGK_VIDMM_WORKER_DRAIN_TEST_STATE State;

    DxgkVidMmWorkerDrainTestInitialize(&State);
    ok_bool_true(DxgkVidMmWorkerDrainCoreIsDrainedLocked(&State.ActiveWorkers), "zero workers begin drained");
    ok_bool_true(State.DrainedEventSignaled, "zero workers begin with a signaled event");
    ok_bool_true(DxgkVidMmWorkerDrainTestAdmit(&State), "first worker is admitted");
    ok_eq_long(State.ActiveWorkers, 1);
    ok_bool_false(State.DrainedEventSignaled, "zero-to-one admission resets the event");
    ok_bool_true(DxgkVidMmWorkerDrainTestAdmit(&State), "second worker is admitted");
    ok_eq_long(State.ActiveWorkers, 2);
    ok_bool_false(State.DrainedEventSignaled, "nonzero admission leaves the event reset");
    ok_bool_true(DxgkVidMmWorkerDrainTestRetire(&State), "first worker retires");
    ok_eq_long(State.ActiveWorkers, 1);
    ok_bool_false(State.DrainedEventSignaled, "nonfinal retirement leaves the event reset");
    ok_bool_true(DxgkVidMmWorkerDrainTestRetire(&State), "last worker retires");
    ok_eq_long(State.ActiveWorkers, 0);
    ok_bool_true(State.DrainedEventSignaled, "one-to-zero retirement signals the event");
    ok_bool_true(DxgkVidMmWorkerDrainCoreIsDrainedLocked(&State.ActiveWorkers), "last retirement is drained");
}

static VOID DxgkVidMmWorkerDrainTestQuiesceInterleaving(VOID)
{
    DXGK_VIDMM_WORKER_DRAIN_TEST_STATE State;

    DxgkVidMmWorkerDrainTestInitialize(&State);
    ok_bool_true(DxgkVidMmWorkerDrainTestAdmit(&State), "worker is admitted before quiesce");
    State.AdmissionBlocked = TRUE;
    ok_bool_false(DxgkVidMmWorkerDrainCoreIsDrainedLocked(&State.ActiveWorkers), "quiesce observes the live worker under the lock");
    ok_bool_false(DxgkVidMmWorkerDrainTestAdmit(&State), "closed admission rejects a racing worker");
    ok_eq_long(State.ActiveWorkers, 1);
    ok_bool_false(State.DrainedEventSignaled, "rejected admission does not corrupt the event");
    ok_bool_true(DxgkVidMmWorkerDrainTestRetire(&State), "live worker retires after admission closes");
    ok_bool_true(DxgkVidMmWorkerDrainCoreIsDrainedLocked(&State.ActiveWorkers), "lock recheck observes the drain");
    ok_bool_true(State.DrainedEventSignaled, "the final retirement wakes quiesce");
    ok_bool_false(DxgkVidMmWorkerDrainTestAdmit(&State), "admission remains closed after the wake");
    State.AdmissionBlocked = FALSE;
    ok_bool_true(DxgkVidMmWorkerDrainTestAdmit(&State), "resume admits a new worker");
    ok_bool_false(State.DrainedEventSignaled, "new admission resets the prior drain signal before unlock");
    ok_bool_true(DxgkVidMmWorkerDrainTestRetire(&State), "resumed worker retires");
    ok_bool_true(State.DrainedEventSignaled, "resumed worker restores the drained state");
}

static VOID DxgkVidMmWorkerDrainTestInvalidTransitions(VOID)
{
    DXGK_VIDMM_WORKER_DRAIN_TEST_STATE State;

    DxgkVidMmWorkerDrainTestInitialize(&State);
    ok_bool_false(DxgkVidMmWorkerDrainTestRetire(&State), "zero worker count cannot underflow");
    ok_eq_long(State.ActiveWorkers, 0);
    ok_bool_true(State.DrainedEventSignaled, "underflow rejection leaves the event signaled");
    State.ActiveWorkers = MAXLONG;
    State.DrainedEventSignaled = FALSE;
    ok_bool_false(DxgkVidMmWorkerDrainTestAdmit(&State), "maximum worker count cannot overflow");
    ok_eq_long(State.ActiveWorkers, MAXLONG);
    ok_bool_false(State.DrainedEventSignaled, "overflow rejection leaves the event unchanged");
    State.ActiveWorkers = -1;
    ok_bool_false(DxgkVidMmWorkerDrainTestAdmit(&State), "corrupt negative count rejects admission");
    ok_bool_false(DxgkVidMmWorkerDrainTestRetire(&State), "corrupt negative count rejects retirement");
}

START_TEST(DxgkVidMmWorkerDrain)
{
    DxgkVidMmWorkerDrainTestTransitions();
    DxgkVidMmWorkerDrainTestQuiesceInterleaving();
    DxgkVidMmWorkerDrainTestInvalidTransitions();
}
