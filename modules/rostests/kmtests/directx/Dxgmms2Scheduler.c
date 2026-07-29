/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgmms2 scheduler queue ownership contract tests
 *
 * dxgmms2 owns the per-engine run queues, their admission order, the dispatch
 * claim protocol, and the engine state machine.  These tests pin the rules
 * dxgkrnl relies on, including the ones a live stack only exposes as a hang.
 */

#include <kmt_test.h>
#include <reactos/drivers/directx/dxgmms2.h>
#include "scheduler_core.h"

#define TAG_DXGMMS2_SCHED_TEST 'sS2D'
#define DXGMMS2_SCHED_TEST_ENGINES 2
#define DXGMMS2_SCHED_TEST_BATCH 8

typedef struct _DXGMMS2_SCHED_TEST_STATE
{
    DXGMMS2_SCHED_CORE Core;
    DXGMMS2_SCHED_PACKET Packets[DXGMMS2_SCHED_MAX_PACKETS];
    ULONG NextPacket;
} DXGMMS2_SCHED_TEST_STATE, *PDXGMMS2_SCHED_TEST_STATE;

static PDXGMMS2_SCHED_PACKET
AllocateTestPacket(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    PDXGMMS2_SCHED_PACKET Packet;

    if (State->NextPacket >= RTL_NUMBER_OF(State->Packets))
        return NULL;
    Packet = &State->Packets[State->NextPacket++];
    RtlZeroMemory(Packet, sizeof(*Packet));
    InitializeListHead(&Packet->Entry);
    return Packet;
}

static VOID
InitAdmitInfo(
    _Out_ DXGMMS2_SCHEDULER_ADMIT_INFO_V1 *Info,
    _In_ ULONG EngineOrdinal,
    _In_ ULONGLONG PacketCookie,
    _In_ ULONGLONG OwnerCookie)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Size = DXGMMS2_SCHEDULER_ADMIT_INFO_V1_SIZE;
    Info->Version = DXGMMS2_SCHEDULER_VERSION_1;
    Info->NodeOrdinal = EngineOrdinal;
    Info->EngineOrdinal = EngineOrdinal;
    Info->PacketCookie = PacketCookie;
    Info->OwnerCookie = OwnerCookie;
}

static VOID
InitClaim(
    _Out_ DXGMMS2_SCHEDULER_CLAIM_V1 *Claim)
{
    RtlZeroMemory(Claim, sizeof(*Claim));
    Claim->Size = DXGMMS2_SCHEDULER_CLAIM_V1_SIZE;
    Claim->Version = DXGMMS2_SCHEDULER_VERSION_1;
}

static VOID
InitEngineStatus(
    _Out_ DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 *Status)
{
    RtlZeroMemory(Status, sizeof(*Status));
    Status->Size = DXGMMS2_SCHEDULER_ENGINE_STATUS_V1_SIZE;
    Status->Version = DXGMMS2_SCHEDULER_VERSION_1;
}

/* Admits one packet on the given engine and returns its assigned fence. */
static ULONG
AdmitOne(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State,
    _In_ ULONG EngineOrdinal,
    _In_ ULONGLONG OwnerCookie,
    _Out_ PDXGMMS2_SCHED_PACKET *OutPacket)
{
    DXGMMS2_SCHEDULER_ADMIT_INFO_V1 Info;
    PDXGMMS2_SCHED_PACKET Packet;
    ULONG FenceId = 0;
    NTSTATUS Status;

    *OutPacket = NULL;
    Packet = AllocateTestPacket(State);
    if (Packet == NULL)
        return 0;
    InitAdmitInfo(&Info, EngineOrdinal, (ULONGLONG)(ULONG_PTR)Packet, OwnerCookie);
    Status = Dxgmms2SchedCoreAdmit(&State->Core, &Info, Packet, &FenceId);
    if (!NT_SUCCESS(Status))
        return 0;
    *OutPacket = Packet;
    return FenceId;
}

static VOID
TestStartAndAdmissionGate(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    DXGMMS2_SCHEDULER_ADMIT_INFO_V1 Info;
    PDXGMMS2_SCHED_PACKET Packet;
    ULONG FenceId = 0;
    NTSTATUS Status;

    Dxgmms2SchedCoreInitialize(&State->Core);
    ok_bool_true(Dxgmms2SchedCoreIsIdle(&State->Core), "a fresh core is idle");

    /* Admission before start is refused, not silently queued. */
    Packet = AllocateTestPacket(State);
    ok(Packet != NULL, "packet pool exhausted\n");
    InitAdmitInfo(&Info, 0, (ULONGLONG)(ULONG_PTR)Packet, 1);
    Status = Dxgmms2SchedCoreAdmit(&State->Core, &Info, Packet, &FenceId);
    ok_eq_hex(Status, STATUS_DEVICE_NOT_READY);

    Status = Dxgmms2SchedCoreStart(&State->Core, 0);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    Status = Dxgmms2SchedCoreStart(&State->Core, DXGMMS2_SCHEDULER_MAX_ENGINES + 1);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = Dxgmms2SchedCoreStart(&State->Core, DXGMMS2_SCHED_TEST_ENGINES);
    ok_eq_hex(Status, STATUS_SUCCESS);
    /* A second start would silently rebase the queues under a live caller. */
    Status = Dxgmms2SchedCoreStart(&State->Core, DXGMMS2_SCHED_TEST_ENGINES);
    ok_eq_hex(Status, STATUS_INVALID_DEVICE_STATE);

    /* A closed gate refuses admission and reopening restores it. */
    Dxgmms2SchedCoreSetAdmission(&State->Core, FALSE);
    Status = Dxgmms2SchedCoreAdmit(&State->Core, &Info, Packet, &FenceId);
    ok_eq_hex(Status, STATUS_DEVICE_NOT_READY);
    Dxgmms2SchedCoreSetAdmission(&State->Core, TRUE);
    Status = Dxgmms2SchedCoreAdmit(&State->Core, &Info, Packet, &FenceId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(FenceId != 0, "an admitted packet must carry a nonzero fence\n");

    /* Clean the queue back out for the next case. */
    {
        PDXGMMS2_SCHED_PACKET Aborted[DXGMMS2_SCHED_TEST_BATCH];
        ULONG Count = Dxgmms2SchedCoreAbortAll(&State->Core, TRUE, Aborted, RTL_NUMBER_OF(Aborted));

        ok_eq_ulong(Count, 1UL);
    }
}

static VOID
TestFifoOrderAndClaimProtocol(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    DXGMMS2_SCHEDULER_CLAIM_V1 Claim;
    DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 EngineStatus;
    PDXGMMS2_SCHED_PACKET First;
    PDXGMMS2_SCHED_PACKET Second;
    PDXGMMS2_SCHED_PACKET Failed = NULL;
    ULONGLONG HeadCookie = 0;
    ULONG FirstFence;
    ULONG SecondFence;
    NTSTATUS Status;

    FirstFence = AdmitOne(State, 0, 100, &First);
    SecondFence = AdmitOne(State, 0, 100, &Second);
    ok(First != NULL && Second != NULL, "admission failed\n");
    if (First == NULL || Second == NULL)
        return;

    /* Fence order must equal queue order: tracked-DMA retirement depends on it. */
    ok(SecondFence > FirstFence, "fences must increase with queue order\n");

    /* A peek reports the packet a claim would hand out, without taking it. */
    ok_bool_true(Dxgmms2SchedCorePeekNext(&State->Core, 0, &HeadCookie), "peek the head");
    ok_eq_pointer((PVOID)(ULONG_PTR)HeadCookie, First);
    ok_bool_true(Dxgmms2SchedCorePeekNext(&State->Core, 0, &HeadCookie), "peek is not destructive");
    ok_eq_pointer((PVOID)(ULONG_PTR)HeadCookie, First);

    InitClaim(&Claim);
    ok_bool_true(Dxgmms2SchedCoreClaim(&State->Core, 0, &Claim), "claim the head");
    ok_eq_pointer((PVOID)(ULONG_PTR)Claim.PacketCookie, First);
    ok_eq_ulong(Claim.SubmissionFenceId, FirstFence);
    ok(Claim.ClaimToken != 0, "a claim must carry a nonzero token\n");

    /* The claim serialises dispatch: no second claim while one is outstanding. */
    {
        DXGMMS2_SCHEDULER_CLAIM_V1 Second2;

        InitClaim(&Second2);
        ok_bool_false(Dxgmms2SchedCoreClaim(&State->Core, 0, &Second2), "no concurrent claim");
    }
    /* A claimed head is not offered by peek either. */
    ok_bool_false(Dxgmms2SchedCorePeekNext(&State->Core, 0, &HeadCookie), "no peek while claimed");

    InitEngineStatus(&EngineStatus);
    Status = Dxgmms2SchedCoreQueryEngine(&State->Core, 0, &EngineStatus);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(EngineStatus.State, (ULONG)Dxgmms2EngineSubmitting);
    ok_eq_ulong(EngineStatus.PendingPacketCount, 2UL);

    /* Publishing marks the packet as owned by the miniport, exactly once. */
    Status = Dxgmms2SchedCorePublishDispatch(&State->Core, 0, Claim.ClaimToken);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2SchedCorePublishDispatch(&State->Core, 0, Claim.ClaimToken);
    ok_eq_hex(Status, STATUS_INVALID_DEVICE_STATE);
    Status = Dxgmms2SchedCorePublishDispatch(&State->Core, 0, Claim.ClaimToken + 0x1000);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    Status = Dxgmms2SchedCoreCompleteDispatch(&State->Core, 0, Claim.ClaimToken, STATUS_SUCCESS, &Failed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Failed, NULL);
    /* A committed claim cannot be committed again. */
    Status = Dxgmms2SchedCoreCompleteDispatch(&State->Core, 0, Claim.ClaimToken, STATUS_SUCCESS, &Failed);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    InitEngineStatus(&EngineStatus);
    (VOID)Dxgmms2SchedCoreQueryEngine(&State->Core, 0, &EngineStatus);
    ok_eq_ulong(EngineStatus.State, (ULONG)Dxgmms2EngineRunning);
    ok_eq_ulong(EngineStatus.LastSubmittedFenceId, SecondFence);
    ok_eq_ulong(EngineStatus.OldestKickedFenceId, FirstFence);

    /* The oldest dispatched packet is what a TDR watchdog blames. */
    {
        ULONG OldestEngine = 0xFFFFFFFF;
        ULONG OldestFence = 0;
        ULONGLONG OldestCookie = 0;

        ok_bool_true(Dxgmms2SchedCoreGetOldestDispatched(&State->Core, &OldestEngine, &OldestFence, &OldestCookie),
                     "an in-flight packet is reported");
        ok_eq_ulong(OldestEngine, 0UL);
        ok_eq_ulong(OldestFence, FirstFence);
        ok_eq_pointer((PVOID)(ULONG_PTR)OldestCookie, First);
    }

    /* Retirement stops at the first packet the miniport never received. */
    {
        PDXGMMS2_SCHED_PACKET Retired[DXGMMS2_SCHED_TEST_BATCH];
        ULONG Count;

        Count = Dxgmms2SchedCoreNotifyCompletion(&State->Core, 0, SecondFence, Retired, RTL_NUMBER_OF(Retired));
        ok_eq_ulong(Count, 1UL);
        ok_eq_pointer(Retired[0], First);
    }

    {
        PDXGMMS2_SCHED_PACKET Aborted[DXGMMS2_SCHED_TEST_BATCH];

        (VOID)Dxgmms2SchedCoreAbortAll(&State->Core, TRUE, Aborted, RTL_NUMBER_OF(Aborted));
    }
}

/*
 * The race that only shows up once the queue leaves dxgkrnl: hardware can
 * signal a packet's fence while its dispatch claim is still outstanding, so
 * that notification cannot retire it.  Committing the claim must replay the
 * sweep, or the packet is stranded and the device wedges.
 */
static VOID
TestCompletionDuringOutstandingClaim(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    DXGMMS2_SCHEDULER_CLAIM_V1 Claim;
    PDXGMMS2_SCHED_PACKET Packet;
    PDXGMMS2_SCHED_PACKET Retired[DXGMMS2_SCHED_TEST_BATCH];
    PDXGMMS2_SCHED_PACKET Failed = NULL;
    ULONG Fence;
    ULONG Count;
    NTSTATUS Status;

    Fence = AdmitOne(State, 0, 200, &Packet);
    ok(Packet != NULL, "admission failed\n");
    if (Packet == NULL)
        return;

    InitClaim(&Claim);
    ok_bool_true(Dxgmms2SchedCoreClaim(&State->Core, 0, &Claim), "claim");
    Status = Dxgmms2SchedCorePublishDispatch(&State->Core, 0, Claim.ClaimToken);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* Completion arrives while the claim is still open: nothing may retire. */
    Count = Dxgmms2SchedCoreNotifyCompletion(&State->Core, 0, Fence, Retired, RTL_NUMBER_OF(Retired));
    ok_eq_ulong(Count, 0UL);

    /* Committing the claim replays the watermark and retires the packet. */
    Status = Dxgmms2SchedCoreCompleteDispatch(&State->Core, 0, Claim.ClaimToken, STATUS_SUCCESS, &Failed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Count = Dxgmms2SchedCoreNotifyCompletion(&State->Core, 0, 0, Retired, RTL_NUMBER_OF(Retired));
    ok_eq_ulong(Count, 1UL);
    ok_eq_pointer(Retired[0], Packet);
    ok_bool_true(Dxgmms2SchedCoreIsIdle(&State->Core), "the engine drains");
}

/*
 * The scheduler packet cookie is opaque caller state.  Watchdog users may
 * consume the engine/fence snapshot after the provider lock is released, but
 * must not infer that the returned cookie still names live storage.
 */
static VOID
TestOldestDispatchedOpaqueCookie(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    const ULONGLONG OpaqueCookie = 0xDEADC0DEBAADF00DULL;
    DXGMMS2_SCHEDULER_ADMIT_INFO_V1 Info;
    DXGMMS2_SCHEDULER_CLAIM_V1 Claim;
    PDXGMMS2_SCHED_PACKET Packet;
    PDXGMMS2_SCHED_PACKET Failed = NULL;
    PDXGMMS2_SCHED_PACKET Batch[DXGMMS2_SCHED_TEST_BATCH];
    ULONGLONG OldestCookie = 0;
    ULONG OldestEngine = 0xFFFFFFFF;
    ULONG OldestFence = 0;
    ULONG Fence = 0;
    NTSTATUS Status;

    Packet = AllocateTestPacket(State);
    ok(Packet != NULL, "packet pool exhausted\n");
    if (Packet == NULL)
        return;

    InitAdmitInfo(&Info, 1, OpaqueCookie, 250);
    Status = Dxgmms2SchedCoreAdmit(&State->Core, &Info, Packet, &Fence);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;

    InitClaim(&Claim);
    ok_bool_true(Dxgmms2SchedCoreClaim(&State->Core, 1, &Claim), "claim opaque-cookie packet");
    Status = Dxgmms2SchedCorePublishDispatch(&State->Core, 1, Claim.ClaimToken);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2SchedCoreCompleteDispatch(
                 &State->Core,
                 1,
                 Claim.ClaimToken,
                 STATUS_SUCCESS,
                 &Failed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Failed, NULL);

    ok_bool_true(
        Dxgmms2SchedCoreGetOldestDispatched(
            &State->Core,
            &OldestEngine,
            &OldestFence,
            &OldestCookie),
        "provider reports scalar watchdog identity");
    ok_eq_ulong(OldestEngine, 1UL);
    ok_eq_ulong(OldestFence, Fence);
    ok_eq_ulonglong(OldestCookie, OpaqueCookie);

    (VOID)Dxgmms2SchedCoreAbortAll(
        &State->Core,
        TRUE,
        Batch,
        RTL_NUMBER_OF(Batch));
}

static VOID
TestFailedDispatchAndCancellation(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    DXGMMS2_SCHEDULER_CLAIM_V1 Claim;
    DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 EngineStatus;
    PDXGMMS2_SCHED_PACKET Owned;
    PDXGMMS2_SCHED_PACKET Other;
    PDXGMMS2_SCHED_PACKET Failed = NULL;
    PDXGMMS2_SCHED_PACKET Batch[DXGMMS2_SCHED_TEST_BATCH];
    ULONG Count;
    NTSTATUS Status;

    (VOID)AdmitOne(State, 0, 300, &Owned);
    (VOID)AdmitOne(State, 0, 301, &Other);
    ok(Owned != NULL && Other != NULL, "admission failed\n");
    if (Owned == NULL || Other == NULL)
        return;

    /* A failed dispatch hands the packet back for terminal cleanup. */
    InitClaim(&Claim);
    ok_bool_true(Dxgmms2SchedCoreClaim(&State->Core, 0, &Claim), "claim");
    Status = Dxgmms2SchedCoreCompleteDispatch(&State->Core, 0, Claim.ClaimToken, STATUS_UNSUCCESSFUL, &Failed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_pointer(Failed, Owned);

    InitEngineStatus(&EngineStatus);
    (VOID)Dxgmms2SchedCoreQueryEngine(&State->Core, 0, &EngineStatus);
    ok_eq_ulong(EngineStatus.PendingPacketCount, 1UL);

    /* Cancelling by owner takes only that owner's undispatched work. */
    Count = Dxgmms2SchedCoreCancelOwner(&State->Core, 999, Batch, RTL_NUMBER_OF(Batch));
    ok_eq_ulong(Count, 0UL);
    Count = Dxgmms2SchedCoreCancelOwner(&State->Core, 301, Batch, RTL_NUMBER_OF(Batch));
    ok_eq_ulong(Count, 1UL);
    ok_eq_pointer(Batch[0], Other);
    ok_bool_true(Dxgmms2SchedCoreIsIdle(&State->Core), "queue empty after cancel");
}

/*
 * A packet the miniport already owns must not be pulled back by an ordinary
 * abort: its claim would be invalidated in flight.  Teardown, which has
 * quiesced hardware, asks for it explicitly.
 */
static VOID
TestAbortRespectsDispatchOwnership(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    DXGMMS2_SCHEDULER_CLAIM_V1 Claim;
    PDXGMMS2_SCHED_PACKET Dispatched;
    PDXGMMS2_SCHED_PACKET Queued;
    PDXGMMS2_SCHED_PACKET Failed = NULL;
    PDXGMMS2_SCHED_PACKET Batch[DXGMMS2_SCHED_TEST_BATCH];
    ULONG Count;
    NTSTATUS Status;

    (VOID)AdmitOne(State, 0, 400, &Dispatched);
    (VOID)AdmitOne(State, 0, 400, &Queued);
    ok(Dispatched != NULL && Queued != NULL, "admission failed\n");
    if (Dispatched == NULL || Queued == NULL)
        return;

    InitClaim(&Claim);
    ok_bool_true(Dxgmms2SchedCoreClaim(&State->Core, 0, &Claim), "claim");
    Status = Dxgmms2SchedCorePublishDispatch(&State->Core, 0, Claim.ClaimToken);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2SchedCoreCompleteDispatch(&State->Core, 0, Claim.ClaimToken, STATUS_SUCCESS, &Failed);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Count = Dxgmms2SchedCoreAbortAll(&State->Core, FALSE, Batch, RTL_NUMBER_OF(Batch));
    ok_eq_ulong(Count, 1UL);
    ok_eq_pointer(Batch[0], Queued);

    Count = Dxgmms2SchedCoreAbortAll(&State->Core, TRUE, Batch, RTL_NUMBER_OF(Batch));
    ok_eq_ulong(Count, 1UL);
    ok_eq_pointer(Batch[0], Dispatched);
    ok_bool_true(Dxgmms2SchedCoreIsIdle(&State->Core), "teardown empties the queue");
}

/*
 * Preemption interrupts everything the miniport accepted but did not finish.
 * The packets stay queued in the same order with the same fence identity, so
 * the next kick resubmits exactly the same work.
 */
static VOID
TestPreemptionResetsDispatchOnly(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    DXGMMS2_SCHEDULER_CLAIM_V1 Claim;
    DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 EngineStatus;
    PDXGMMS2_SCHED_PACKET Packet;
    PDXGMMS2_SCHED_PACKET Failed = NULL;
    PDXGMMS2_SCHED_PACKET Batch[DXGMMS2_SCHED_TEST_BATCH];
    ULONGLONG HeadCookie = 0;
    ULONG Fence;
    ULONG Count;
    NTSTATUS Status;

    Fence = AdmitOne(State, 0, 500, &Packet);
    ok(Packet != NULL, "admission failed\n");
    if (Packet == NULL)
        return;

    InitClaim(&Claim);
    ok_bool_true(Dxgmms2SchedCoreClaim(&State->Core, 0, &Claim), "claim");
    (VOID)Dxgmms2SchedCorePublishDispatch(&State->Core, 0, Claim.ClaimToken);
    Status = Dxgmms2SchedCoreCompleteDispatch(&State->Core, 0, Claim.ClaimToken, STATUS_SUCCESS, &Failed);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Count = Dxgmms2SchedCoreResetDispatched(&State->Core, 0, Batch, RTL_NUMBER_OF(Batch));
    ok_eq_ulong(Count, 1UL);
    ok_eq_pointer(Batch[0], Packet);

    /* Still queued, same fence, and claimable again. */
    InitEngineStatus(&EngineStatus);
    (VOID)Dxgmms2SchedCoreQueryEngine(&State->Core, 0, &EngineStatus);
    ok_eq_ulong(EngineStatus.PendingPacketCount, 1UL);
    ok_eq_ulong(EngineStatus.OldestKickedFenceId, 0UL);
    ok_bool_true(Dxgmms2SchedCorePeekNext(&State->Core, 0, &HeadCookie), "resubmittable");
    ok_eq_pointer((PVOID)(ULONG_PTR)HeadCookie, Packet);

    InitClaim(&Claim);
    ok_bool_true(Dxgmms2SchedCoreClaim(&State->Core, 0, &Claim), "reclaim after preemption");
    ok_eq_ulong(Claim.SubmissionFenceId, Fence);
    (VOID)Dxgmms2SchedCoreCompleteDispatch(&State->Core, 0, Claim.ClaimToken, STATUS_UNSUCCESSFUL, &Failed);
    ok_eq_pointer(Failed, Packet);
}

static VOID
TestReservationAccounting(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    DXGMMS2_SCHEDULER_ADMIT_INFO_V1 Info;
    PDXGMMS2_SCHED_PACKET Packet;
    PDXGMMS2_SCHED_PACKET Batch[DXGMMS2_SCHED_TEST_BATCH];
    ULONG FenceId = 0;
    NTSTATUS Status;

    Packet = AllocateTestPacket(State);
    ok(Packet != NULL, "packet pool exhausted\n");
    if (Packet == NULL)
        return;

    /* Claiming a reservation that was never taken must not underflow. */
    InitAdmitInfo(&Info, 0, (ULONGLONG)(ULONG_PTR)Packet, 600);
    Info.Flags = DXGMMS2_SCHEDULER_ADMIT_CONSUME_RESERVATION;
    Status = Dxgmms2SchedCoreAdmit(&State->Core, &Info, Packet, &FenceId);
    ok_eq_hex(Status, STATUS_INVALID_DEVICE_STATE);

    Status = Dxgmms2SchedCoreReserve(&State->Core, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2SchedCoreAdmit(&State->Core, &Info, Packet, &FenceId);
    ok_eq_hex(Status, STATUS_SUCCESS);

    /* The reservation was consumed, so a second consuming admit fails. */
    {
        PDXGMMS2_SCHED_PACKET Again = AllocateTestPacket(State);

        ok(Again != NULL, "packet pool exhausted\n");
        if (Again != NULL)
        {
            InitAdmitInfo(&Info, 0, (ULONGLONG)(ULONG_PTR)Again, 600);
            Info.Flags = DXGMMS2_SCHEDULER_ADMIT_CONSUME_RESERVATION;
            Status = Dxgmms2SchedCoreAdmit(&State->Core, &Info, Again, &FenceId);
            ok_eq_hex(Status, STATUS_INVALID_DEVICE_STATE);
        }
    }

    /* Reservations are refused once the gate is closed. */
    Dxgmms2SchedCoreSetAdmission(&State->Core, FALSE);
    Status = Dxgmms2SchedCoreReserve(&State->Core, 0);
    ok_eq_hex(Status, STATUS_DEVICE_NOT_READY);
    Dxgmms2SchedCoreSetAdmission(&State->Core, TRUE);

    Status = Dxgmms2SchedCoreReserve(&State->Core, 0);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Dxgmms2SchedCoreUnreserve(&State->Core, 0);

    (VOID)Dxgmms2SchedCoreAbortAll(&State->Core, TRUE, Batch, RTL_NUMBER_OF(Batch));
}

/*
 * The engine state machine is dxgmms2's.  SetEngineState is a validated
 * compare-and-set so a caller cannot drive an engine into a state the
 * scheduler does not model, and cannot clobber a state it no longer owns.
 */
static VOID
TestEngineStateMachine(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    DXGMMS2_SCHEDULER_CLAIM_V1 Claim;
    DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 EngineStatus;
    PDXGMMS2_SCHED_PACKET Packet;
    PDXGMMS2_SCHED_PACKET Failed = NULL;
    PDXGMMS2_SCHED_PACKET Batch[DXGMMS2_SCHED_TEST_BATCH];
    ULONG Previous = 0xFFFFFFFF;
    NTSTATUS Status;

    ok_bool_true(Dxgmms2SchedCoreIsValidTransition(Dxgmms2EngineIdle, Dxgmms2EngineSubmitting), "idle -> submitting");
    ok_bool_true(Dxgmms2SchedCoreIsValidTransition(Dxgmms2EngineSubmitting, Dxgmms2EngineRunning), "submitting -> running");
    /* The scheduler pipelines: it claims the next packet while one is in flight. */
    ok_bool_true(Dxgmms2SchedCoreIsValidTransition(Dxgmms2EngineRunning, Dxgmms2EngineSubmitting), "running -> submitting");
    ok_bool_true(Dxgmms2SchedCoreIsValidTransition(Dxgmms2EngineRunning, Dxgmms2EngineResetting), "any -> resetting");
    ok_bool_false(Dxgmms2SchedCoreIsValidTransition(Dxgmms2EngineResetComplete, Dxgmms2EngineResetting), "reset-complete is terminal for reset");
    ok_bool_false(Dxgmms2SchedCoreIsValidTransition(Dxgmms2EngineIdle, Dxgmms2EngineRunning), "idle -> running is not modelled");
    ok_bool_false(Dxgmms2SchedCoreIsValidTransition(Dxgmms2EngineError, Dxgmms2EngineIdle), "error is terminal");
    ok_bool_false(Dxgmms2SchedCoreIsValidTransition(Dxgmms2EngineStateCount, Dxgmms2EngineIdle), "out-of-range rejected");

    /* Compare-and-set: a stale expectation is refused and reports what it saw. */
    Status = Dxgmms2SchedCoreSetEngineState(&State->Core, 0, Dxgmms2EngineRunning, Dxgmms2EngineIdle, &Previous);
    ok_eq_hex(Status, STATUS_INVALID_DEVICE_STATE);
    ok_eq_ulong(Previous, (ULONG)Dxgmms2EngineIdle);

    Status = Dxgmms2SchedCoreSetEngineState(&State->Core, 0, DXGMMS2_ENGINE_STATE_ANY, Dxgmms2EngineSuspended, &Previous);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Previous, (ULONG)Dxgmms2EngineIdle);
    /* Even unconditionally, a move outside the table is refused. */
    Status = Dxgmms2SchedCoreSetEngineState(&State->Core, 0, DXGMMS2_ENGINE_STATE_ANY, Dxgmms2EngineRunning, &Previous);
    ok_eq_hex(Status, STATUS_INVALID_DEVICE_STATE);
    Status = Dxgmms2SchedCoreSetEngineState(&State->Core, 0, Dxgmms2EngineSuspended, Dxgmms2EngineResuming, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2SchedCoreSetEngineState(&State->Core, 0, Dxgmms2EngineResuming, Dxgmms2EngineIdle, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = Dxgmms2SchedCoreSetEngineState(&State->Core, DXGMMS2_SCHED_TEST_ENGINES, DXGMMS2_ENGINE_STATE_ANY, Dxgmms2EngineIdle, NULL);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);

    /*
     * Committing a dispatch must only retire the state it owns: reset takes the
     * engine away from the submitter, and the commit must not put it back.
     */
    (VOID)AdmitOne(State, 0, 700, &Packet);
    ok(Packet != NULL, "admission failed\n");
    if (Packet == NULL)
        return;
    InitClaim(&Claim);
    ok_bool_true(Dxgmms2SchedCoreClaim(&State->Core, 0, &Claim), "claim");
    Status = Dxgmms2SchedCoreSetEngineState(&State->Core, 0, Dxgmms2EngineSubmitting, Dxgmms2EngineResetting, NULL);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2SchedCoreCompleteDispatch(&State->Core, 0, Claim.ClaimToken, STATUS_SUCCESS, &Failed);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitEngineStatus(&EngineStatus);
    (VOID)Dxgmms2SchedCoreQueryEngine(&State->Core, 0, &EngineStatus);
    ok_eq_ulong(EngineStatus.State, (ULONG)Dxgmms2EngineResetting);

    (VOID)Dxgmms2SchedCoreAbortAll(&State->Core, TRUE, Batch, RTL_NUMBER_OF(Batch));
    (VOID)Dxgmms2SchedCoreSetEngineState(&State->Core, 0, DXGMMS2_ENGINE_STATE_ANY, Dxgmms2EngineResetComplete, NULL);
    (VOID)Dxgmms2SchedCoreSetEngineState(&State->Core, 0, Dxgmms2EngineResetComplete, Dxgmms2EngineIdle, NULL);
}

/* A stopped core returns to its pre-start state so the adapter can restart. */
static VOID
TestStopAndRestart(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    PDXGMMS2_SCHED_PACKET Packet;
    PDXGMMS2_SCHED_PACKET Batch[DXGMMS2_SCHED_TEST_BATCH];
    NTSTATUS Status;

    (VOID)AdmitOne(State, 0, 800, &Packet);
    ok(Packet != NULL, "admission failed\n");

    /* Stopping with work still queued would strand it. */
    Status = Dxgmms2SchedCoreStop(&State->Core);
    ok_eq_hex(Status, STATUS_DEVICE_BUSY);

    (VOID)Dxgmms2SchedCoreAbortAll(&State->Core, TRUE, Batch, RTL_NUMBER_OF(Batch));
    Status = Dxgmms2SchedCoreStop(&State->Core);
    ok_eq_hex(Status, STATUS_SUCCESS);
    /* Idempotent. */
    Status = Dxgmms2SchedCoreStop(&State->Core);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Status = Dxgmms2SchedCoreStart(&State->Core, DXGMMS2_SCHED_TEST_ENGINES);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_bool_true(Dxgmms2SchedCoreIsIdle(&State->Core), "a restarted core is idle");

    State->NextPacket = 0;
    (VOID)AdmitOne(State, 0, 801, &Packet);
    ok(Packet != NULL, "admission works again after restart\n");
    (VOID)Dxgmms2SchedCoreAbortAll(&State->Core, TRUE, Batch, RTL_NUMBER_OF(Batch));
}

/* Engines are independent queues: one draining must not disturb another. */
static VOID
TestEnginesAreIndependent(
    _Inout_ PDXGMMS2_SCHED_TEST_STATE State)
{
    DXGMMS2_SCHEDULER_CLAIM_V1 Claim;
    DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 EngineStatus;
    PDXGMMS2_SCHED_PACKET Zero;
    PDXGMMS2_SCHED_PACKET One;
    PDXGMMS2_SCHED_PACKET Failed = NULL;
    PDXGMMS2_SCHED_PACKET Batch[DXGMMS2_SCHED_TEST_BATCH];
    ULONG Count;

    (VOID)AdmitOne(State, 0, 900, &Zero);
    (VOID)AdmitOne(State, 1, 901, &One);
    ok(Zero != NULL && One != NULL, "admission failed\n");
    if (Zero == NULL || One == NULL)
        return;

    InitClaim(&Claim);
    ok_bool_true(Dxgmms2SchedCoreClaim(&State->Core, 1, &Claim), "engine 1 claims its own head");
    ok_eq_pointer((PVOID)(ULONG_PTR)Claim.PacketCookie, One);
    (VOID)Dxgmms2SchedCorePublishDispatch(&State->Core, 1, Claim.ClaimToken);
    (VOID)Dxgmms2SchedCoreCompleteDispatch(&State->Core, 1, Claim.ClaimToken, STATUS_SUCCESS, &Failed);

    Count = Dxgmms2SchedCoreNotifyCompletion(&State->Core, 1, 0xFFFF, Batch, RTL_NUMBER_OF(Batch));
    ok_eq_ulong(Count, 1UL);
    ok_eq_pointer(Batch[0], One);

    InitEngineStatus(&EngineStatus);
    (VOID)Dxgmms2SchedCoreQueryEngine(&State->Core, 0, &EngineStatus);
    ok_eq_ulong(EngineStatus.PendingPacketCount, 1UL);
    ok_eq_ulong(EngineStatus.State, (ULONG)Dxgmms2EngineIdle);
    ok_bool_false(Dxgmms2SchedCoreIsIdle(&State->Core), "engine 0 still holds work");

    (VOID)Dxgmms2SchedCoreAbortAll(&State->Core, TRUE, Batch, RTL_NUMBER_OF(Batch));
    ok_bool_true(Dxgmms2SchedCoreIsIdle(&State->Core), "both engines drained");
}

START_TEST(Dxgmms2Scheduler)
{
    PDXGMMS2_SCHED_TEST_STATE State;

    State = ExAllocatePoolWithTag(NonPagedPool, sizeof(*State), TAG_DXGMMS2_SCHED_TEST);
    ok(State != NULL, "scheduler test state allocation failed\n");
    if (State == NULL)
        return;
    RtlZeroMemory(State, sizeof(*State));

    TestStartAndAdmissionGate(State);
    TestFifoOrderAndClaimProtocol(State);
    TestCompletionDuringOutstandingClaim(State);
    TestOldestDispatchedOpaqueCookie(State);
    TestFailedDispatchAndCancellation(State);
    TestAbortRespectsDispatchOwnership(State);
    TestPreemptionResetsDispatchOnly(State);
    TestReservationAccounting(State);
    TestEngineStateMachine(State);
    TestEnginesAreIndependent(State);
    TestStopAndRestart(State);

    ExFreePoolWithTag(State, TAG_DXGMMS2_SCHED_TEST);
}

/* EOF */
