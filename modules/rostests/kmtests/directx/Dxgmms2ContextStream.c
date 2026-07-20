/*
 * PROJECT:     ReactOS kernel-mode tests
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgmms2 logical context-stream ordering contract tests
 */

#include <kmt_test.h>
#include <reactos/drivers/directx/dxgmms2.h>
#include "context_stream_core.h"

static VOID InitializeCreateInfo(_Out_ PDXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1 Info, _In_ ULONG NodeOrdinal, _In_ ULONG QueueDepth)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Size = DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1_SIZE;
    Info->Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Info->NodeOrdinal = NodeOrdinal;
    Info->QueueDepth = QueueDepth;
}

static VOID InitializeWorkInfo(_Out_ PDXGMMS2_ADMIT_CONTEXT_WORK_V1 Info, _In_ ULONGLONG ClientTag)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Size = DXGMMS2_ADMIT_CONTEXT_WORK_V1_SIZE;
    Info->Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Info->ClientTag = ClientTag;
}

static VOID InitializeWaitInfo(_Out_ PDXGMMS2_ADMIT_CONTEXT_WAIT_V1 Info, _In_ ULONGLONG ObjectId, _In_ ULONGLONG FenceValue, _In_ ULONGLONG ClientTag)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Size = DXGMMS2_ADMIT_CONTEXT_WAIT_V1_SIZE;
    Info->Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Info->ObjectId = ObjectId;
    Info->FenceValue = FenceValue;
    Info->ClientTag = ClientTag;
}

static VOID InitializeSignalInfo(_Out_ PDXGMMS2_ADMIT_CONTEXT_SIGNAL_V1 Info, _In_ ULONG Flags, _In_ ULONG ContextCount, _In_reads_(ContextCount) const DXGMMS2_CONTEXT_STREAM_HANDLE *ContextHandles, _Out_writes_(ContextCount) ULONGLONG *Sequences, _In_ ULONGLONG ObjectId, _In_ ULONGLONG FenceValue, _In_ ULONGLONG ClientTag)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Size = DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1_SIZE;
    Info->Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Info->Flags = Flags;
    Info->ContextCount = ContextCount;
    Info->ContextHandles = ContextHandles;
    Info->Sequences = Sequences;
    Info->ObjectId = ObjectId;
    Info->FenceValue = FenceValue;
    Info->ClientTag = ClientTag;
}

static VOID InitializeAction(_Out_ PDXGMMS2_CONTEXT_ACTION_V1 Action)
{
    RtlZeroMemory(Action, sizeof(*Action));
    Action->Size = DXGMMS2_CONTEXT_ACTION_V1_SIZE;
    Action->Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
}

static VOID InitializeCancelInfo(_Out_ PDXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1 Info, _In_ ULONG Flags, _In_ ULONG Reason)
{
    RtlZeroMemory(Info, sizeof(*Info));
    Info->Size = DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1_SIZE;
    Info->Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Info->Flags = Flags;
    Info->Reason = Reason;
}

static NTSTATUS QueryContext(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _Out_ PDXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1 Snapshot)
{
    RtlZeroMemory(Snapshot, sizeof(*Snapshot));
    Snapshot->Size = DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1_SIZE;
    Snapshot->Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    return Dxgmms2ContextStreamQuery(Manager, Context, Snapshot);
}

static VOID CheckPrefixes(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _In_ ULONGLONG Admitted, _In_ ULONGLONG Submitted, _In_ ULONGLONG Completed, _In_ ULONG Queued)
{
    DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1 Snapshot;
    NTSTATUS Status;

    Status = QueryContext(Manager, Context, &Snapshot);
    ok_eq_hex(Status, STATUS_SUCCESS);
    if (!NT_SUCCESS(Status))
        return;
    ok_eq_ulonglong(Snapshot.LastAdmittedSequence, Admitted);
    ok_eq_ulonglong(Snapshot.LastSubmittedSequence, Submitted);
    ok_eq_ulonglong(Snapshot.LastCompletedSequence, Completed);
    ok_eq_ulong(Snapshot.QueuedEntryCount, Queued);
}

static ULONG DrainRetirements(_Inout_ PDXGMMS2_CONTEXT_STREAM_MANAGER Manager, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE Context, _Out_writes_opt_(16) DXGMMS2_CONTEXT_RETIREMENT_V1 *Retirements)
{
    DXGMMS2_CONTEXT_RETIREMENT_V1 LocalRetirements[16];
    PDXGMMS2_CONTEXT_RETIREMENT_V1 Output;
    ULONG RetiredCount;
    ULONG RecordIndex;
    ULONGLONG PreviousSequence = 0;
    NTSTATUS Status;

    Output = Retirements != NULL ? Retirements : LocalRetirements;
    Status = Dxgmms2ContextStreamDrainRetirements(Manager, Context, Output, RTL_NUMBER_OF(LocalRetirements), &RetiredCount);
    ok_eq_hex(Status, STATUS_SUCCESS);
    for (RecordIndex = 0; RecordIndex < RetiredCount; ++RecordIndex)
    {
        ok(Output[RecordIndex].Sequence > PreviousSequence, "retirement sequences must be strictly ordered\n");
        PreviousSequence = Output[RecordIndex].Sequence;
    }
    return RetiredCount;
}

static VOID TestOrderedPrefixesWaitsAndSignals(VOID)
{
    DXGMMS2_CONTEXT_STREAM_MANAGER Manager;
    DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1 CreateInfo;
    DXGMMS2_ADMIT_CONTEXT_WORK_V1 WorkInfo;
    DXGMMS2_ADMIT_CONTEXT_WAIT_V1 WaitInfo;
    DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1 SignalInfo;
    DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1 CancelInfo;
    DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1 Snapshot;
    DXGMMS2_CONTEXT_ACTION_V1 Action;
    DXGMMS2_CONTEXT_STREAM_HANDLE ContextA;
    DXGMMS2_CONTEXT_STREAM_HANDLE ContextB;
    DXGMMS2_CONTEXT_STREAM_HANDLE Handles[2];
    ULONGLONG SignalSequences[2];
    ULONGLONG Sequence1;
    ULONGLONG Sequence2;
    ULONGLONG TransactionId;
    ULONGLONG BeforeAdmitted;
    NTSTATUS Status;

    Dxgmms2ContextStreamManagerInitialize(&Manager);
    Status = Dxgmms2ContextStreamManagerStart(&Manager, 2);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(Dxgmms2ContextStreamManagerGetGeneration(&Manager) != 0, "context manager generation must be nonzero\n");

    InitializeCreateInfo(&CreateInfo, 0, 16);
    ContextA = NULL;
    Status = Dxgmms2ContextStreamCreate(&Manager, &CreateInfo, &ContextA);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(ContextA != NULL, "context A handle must be nonnull\n");
    InitializeCreateInfo(&CreateInfo, 1, 2);
    ContextB = NULL;
    Status = Dxgmms2ContextStreamCreate(&Manager, &CreateInfo, &ContextB);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(ContextB != NULL && ContextB != ContextA, "context B handle must be unique\n");

    InitializeWorkInfo(&WorkInfo, 0x101);
    Status = Dxgmms2ContextStreamAdmitWork(&Manager, ContextA, &WorkInfo, &Sequence1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(Sequence1, 1);
    InitializeWorkInfo(&WorkInfo, 0x102);
    Status = Dxgmms2ContextStreamAdmitWork(&Manager, ContextA, &WorkInfo, &Sequence2);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(Sequence2, 2);
    CheckPrefixes(&Manager, ContextA, 2, 0, 0, 2);

    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Action.Type, Dxgmms2ContextActionWork);
    ok_eq_ulonglong(Action.Sequence, Sequence1);
    ok_eq_ulonglong(Action.ClientTag, 0x101);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextA, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(Action.Sequence, Sequence2);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextA, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, ContextA, 2, 2, 0, 2);

    Status = Dxgmms2ContextStreamCompleteWork(&Manager, ContextA, Sequence2, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, ContextA, 2, 2, 0, 2);
    Status = Dxgmms2ContextStreamCompleteWork(&Manager, ContextA, Sequence1, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, ContextA, 2, 2, 2, 0);

    InitializeWaitInfo(&WaitInfo, 0x9001, 77, 0x103);
    Status = Dxgmms2ContextStreamAdmitWait(&Manager, ContextA, &WaitInfo, &Sequence1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(Sequence1, 3);
    InitializeWorkInfo(&WorkInfo, 0x104);
    Status = Dxgmms2ContextStreamAdmitWork(&Manager, ContextA, &WorkInfo, &Sequence2);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(Sequence2, 4);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Action.Type, Dxgmms2ContextActionWait);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextA, Action.Sequence, Action.ClaimToken, STATUS_PENDING);
    ok_eq_hex(Status, STATUS_PENDING);
    CheckPrefixes(&Manager, ContextA, 4, 2, 2, 2);
    Status = Dxgmms2ContextStreamResolveWait(&Manager, ContextA, Sequence1, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, ContextA, 4, 3, 3, 1);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(Action.Sequence, Sequence2);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextA, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamCompleteWork(&Manager, ContextA, Sequence2, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, ContextA, 4, 4, 4, 0);

    InitializeWorkInfo(&WorkInfo, 0x105);
    Status = Dxgmms2ContextStreamAdmitWork(&Manager, ContextA, &WorkInfo, &Sequence1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextA, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Handles[0] = ContextA;
    SignalSequences[0] = 0;
    InitializeSignalInfo(&SignalInfo, 0, 1, Handles, SignalSequences, 0x9002, 78, 0x106);
    Status = Dxgmms2ContextStreamAdmitSignal(&Manager, &SignalInfo, &TransactionId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok(TransactionId != 0, "completion signal transaction must be nonzero\n");
    ok_eq_ulonglong(SignalSequences[0], 6);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_PENDING);
    Status = Dxgmms2ContextStreamCompleteWork(&Manager, ContextA, Sequence1, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Action.Type, Dxgmms2ContextActionSignal);
    ok_eq_ulonglong(Action.TransactionId, TransactionId);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextA, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, ContextA, 6, 6, 6, 0);

    InitializeWorkInfo(&WorkInfo, 0x107);
    Status = Dxgmms2ContextStreamAdmitWork(&Manager, ContextA, &WorkInfo, &Sequence1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextA, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    SignalSequences[0] = 0;
    InitializeSignalInfo(&SignalInfo, DXGMMS2_CONTEXT_SIGNAL_AT_SUBMISSION, 1, Handles, SignalSequences, 0x9003, 79, 0x108);
    Status = Dxgmms2ContextStreamAdmitSignal(&Manager, &SignalInfo, &TransactionId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(SignalSequences[0], 8);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Action.Flags, DXGMMS2_CONTEXT_SIGNAL_AT_SUBMISSION);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextA, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, ContextA, 8, 8, 6, 2);
    Status = Dxgmms2ContextStreamCompleteWork(&Manager, ContextA, Sequence1, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, ContextA, 8, 8, 8, 0);

    InitializeWorkInfo(&WorkInfo, 0x109);
    Status = Dxgmms2ContextStreamAdmitWork(&Manager, ContextA, &WorkInfo, &Sequence1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextA, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Handles[0] = ContextA;
    Handles[1] = ContextB;
    SignalSequences[0] = 0;
    SignalSequences[1] = 0;
    InitializeSignalInfo(&SignalInfo, 0, 2, Handles, SignalSequences, 0x9004, 80, 0x110);
    Status = Dxgmms2ContextStreamAdmitSignal(&Manager, &SignalInfo, &TransactionId);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulonglong(SignalSequences[0], 10);
    ok_eq_ulonglong(SignalSequences[1], 1);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextB, &Action);
    ok_eq_hex(Status, STATUS_PENDING);
    Status = Dxgmms2ContextStreamCompleteWork(&Manager, ContextA, Sequence1, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextB, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Action.Type, Dxgmms2ContextActionSignal);
    ok_eq_ulonglong(Action.TransactionId, TransactionId);
    Status = QueryContext(&Manager, ContextA, &Snapshot);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Snapshot.ActiveClaimCount, 1);
    Status = QueryContext(&Manager, ContextB, &Snapshot);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Snapshot.ActiveClaimCount, 1);
    {
        DXGMMS2_CONTEXT_ACTION_V1 OtherAction;

        InitializeAction(&OtherAction);
        Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &OtherAction);
        ok_eq_hex(Status, STATUS_DEVICE_BUSY);
    }
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextB, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, ContextA, 10, 10, 10, 0);
    CheckPrefixes(&Manager, ContextB, 1, 1, 1, 0);
    ok_eq_ulong(DrainRetirements(&Manager, ContextB, NULL), 1);

    Status = QueryContext(&Manager, ContextA, &Snapshot);
    ok_eq_hex(Status, STATUS_SUCCESS);
    BeforeAdmitted = Snapshot.LastAdmittedSequence;
    Handles[0] = ContextA;
    Handles[1] = ContextA;
    SignalSequences[0] = 0xaaaaaaaaaaaaaaaaULL;
    SignalSequences[1] = 0xbbbbbbbbbbbbbbbbULL;
    InitializeSignalInfo(&SignalInfo, 0, 2, Handles, SignalSequences, 0x9005, 81, 0x111);
    Status = Dxgmms2ContextStreamAdmitSignal(&Manager, &SignalInfo, &TransactionId);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok_eq_ulonglong(SignalSequences[0], 0xaaaaaaaaaaaaaaaaULL);
    ok_eq_ulonglong(SignalSequences[1], 0xbbbbbbbbbbbbbbbbULL);
    CheckPrefixes(&Manager, ContextA, BeforeAdmitted, BeforeAdmitted, BeforeAdmitted, 0);

    InitializeWaitInfo(&WaitInfo, 0x9006, 82, 0x112);
    Status = Dxgmms2ContextStreamAdmitWait(&Manager, ContextB, &WaitInfo, &Sequence1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeWorkInfo(&WorkInfo, 0x113);
    Status = Dxgmms2ContextStreamAdmitWork(&Manager, ContextB, &WorkInfo, &Sequence2);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Handles[0] = ContextA;
    Handles[1] = ContextB;
    SignalSequences[0] = 0xccccccccccccccccULL;
    SignalSequences[1] = 0xddddddddddddddddULL;
    InitializeSignalInfo(&SignalInfo, 0, 2, Handles, SignalSequences, 0x9007, 83, 0x114);
    Status = Dxgmms2ContextStreamAdmitSignal(&Manager, &SignalInfo, &TransactionId);
    ok_eq_hex(Status, STATUS_DEVICE_BUSY);
    ok_eq_ulonglong(SignalSequences[0], 0xccccccccccccccccULL);
    ok_eq_ulonglong(SignalSequences[1], 0xddddddddddddddddULL);
    CheckPrefixes(&Manager, ContextA, BeforeAdmitted, BeforeAdmitted, BeforeAdmitted, 0);
    InitializeCancelInfo(&CancelInfo, DXGMMS2_CONTEXT_CANCEL_FORCE_SUBMITTED, 55);
    Status = Dxgmms2ContextStreamCancel(&Manager, ContextB, &CancelInfo);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, ContextB, 3, 3, 3, 0);

    InitializeWorkInfo(&WorkInfo, 0x115);
    Status = Dxgmms2ContextStreamAdmitWork(&Manager, ContextA, &WorkInfo, &Sequence1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, ContextA, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeCancelInfo(&CancelInfo, DXGMMS2_CONTEXT_CANCEL_FORCE_SUBMITTED | DXGMMS2_CONTEXT_CANCEL_TDR, 77);
    Status = Dxgmms2ContextStreamCancel(&Manager, ContextA, &CancelInfo);
    ok_eq_hex(Status, STATUS_PENDING);
    Status = QueryContext(&Manager, ContextA, &Snapshot);
    ok_eq_hex(Status, STATUS_SUCCESS);
    ok_eq_ulong(Snapshot.State, Dxgmms2ContextStreamPublicClosing);
    ok_eq_ulong(Snapshot.ActiveClaimCount, 1);
    ok_eq_ulong(Snapshot.CancelReason, 77);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, ContextA, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_CANCELLED);
    CheckPrefixes(&Manager, ContextA, 11, 11, 11, 0);

    {
        DXGMMS2_CONTEXT_RETIREMENT_V1 Retirements[16];
        ULONG RetiredCount;

        RetiredCount = DrainRetirements(&Manager, ContextA, Retirements);
        ok_eq_ulong(RetiredCount, 11);
        if (RetiredCount == 11)
        {
            ok_eq_ulonglong(Retirements[10].Sequence, 11);
            ok_eq_hex(Retirements[10].TerminalStatus, STATUS_CANCELLED);
            ok_eq_ulonglong(Retirements[10].ClientTag, 0x115);
        }
        ok_eq_ulong(DrainRetirements(&Manager, ContextA, NULL), 0);
    }
    ok_eq_ulong(DrainRetirements(&Manager, ContextB, NULL), 2);
    Status = Dxgmms2ContextStreamDestroy(&Manager, ContextA);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamDestroy(&Manager, ContextB);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerBeginStop(&Manager, Dxgmms2StopReasonPnpStop);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerCompleteStop(&Manager, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerPrepareDestroy(&Manager);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static VOID TestStopClaimDrainAndForcedRetirement(VOID)
{
    DXGMMS2_CONTEXT_STREAM_MANAGER Manager;
    DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1 CreateInfo;
    DXGMMS2_ADMIT_CONTEXT_WORK_V1 WorkInfo;
    DXGMMS2_CONTEXT_ACTION_V1 Action;
    DXGMMS2_CONTEXT_STREAM_HANDLE Context;
    ULONGLONG Sequence;
    NTSTATUS Status;

    Dxgmms2ContextStreamManagerInitialize(&Manager);
    Status = Dxgmms2ContextStreamManagerStart(&Manager, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeCreateInfo(&CreateInfo, 0, 4);
    Status = Dxgmms2ContextStreamCreate(&Manager, &CreateInfo, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeWorkInfo(&WorkInfo, 0x201);
    Status = Dxgmms2ContextStreamAdmitWork(&Manager, Context, &WorkInfo, &Sequence);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, Context, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamDestroy(&Manager, Context);
    ok_eq_hex(Status, STATUS_DEVICE_BUSY);
    Status = Dxgmms2ContextStreamManagerBeginStop(&Manager, Dxgmms2StopReasonRemove);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerCompleteStop(&Manager, TRUE);
    ok_eq_hex(Status, STATUS_DEVICE_BUSY);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, Context, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerCompleteStop(&Manager, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, Context, 1, 1, 1, 0);
    ok_eq_ulong(DrainRetirements(&Manager, Context, NULL), 1);
    Status = Dxgmms2ContextStreamDestroy(&Manager, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerPrepareDestroy(&Manager);
    ok_eq_hex(Status, STATUS_SUCCESS);

    Dxgmms2ContextStreamManagerInitialize(&Manager);
    Status = Dxgmms2ContextStreamManagerStart(&Manager, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeCreateInfo(&CreateInfo, 0, 4);
    Status = Dxgmms2ContextStreamCreate(&Manager, &CreateInfo, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeWorkInfo(&WorkInfo, 0x202);
    Status = Dxgmms2ContextStreamAdmitWork(&Manager, Context, &WorkInfo, &Sequence);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, Context, &Action);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamCommitAction(&Manager, Context, Action.Sequence, Action.ClaimToken, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerBeginStop(&Manager, Dxgmms2StopReasonSurpriseRemove);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerCompleteStop(&Manager, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    CheckPrefixes(&Manager, Context, 1, 1, 1, 0);
    Status = Dxgmms2ContextStreamCompleteWork(&Manager, Context, Sequence, STATUS_SUCCESS);
    ok_eq_hex(Status, STATUS_ALREADY_COMPLETE);
    ok_eq_ulong(DrainRetirements(&Manager, Context, NULL), 1);
    Status = Dxgmms2ContextStreamDestroy(&Manager, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerPrepareDestroy(&Manager);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

static VOID TestValidationAndCanaryBoundaries(VOID)
{
    DXGMMS2_CONTEXT_STREAM_MANAGER Manager;
    DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1 CreateInfo;
    DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1 Snapshot;
    DXGMMS2_CONTEXT_ACTION_V1 Action;
    DXGMMS2_CONTEXT_STREAM_HANDLE Context;
    NTSTATUS Status;

    Dxgmms2ContextStreamManagerInitialize(&Manager);
    Status = Dxgmms2ContextStreamManagerStart(&Manager, 1);
    ok_eq_hex(Status, STATUS_SUCCESS);
    InitializeCreateInfo(&CreateInfo, 1, 4);
    Context = (DXGMMS2_CONTEXT_STREAM_HANDLE)(ULONG_PTR)1;
    Status = Dxgmms2ContextStreamCreate(&Manager, &CreateInfo, &Context);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    ok(Context == NULL, "failed create must clear output context handle\n");
    InitializeCreateInfo(&CreateInfo, 0, DXGMMS2_CONTEXT_STREAM_MAX_QUEUE_DEPTH + 1);
    Status = Dxgmms2ContextStreamCreate(&Manager, &CreateInfo, &Context);
    ok_eq_hex(Status, STATUS_INVALID_PARAMETER);
    InitializeCreateInfo(&CreateInfo, 0, 4);
    Status = Dxgmms2ContextStreamCreate(&Manager, &CreateInfo, &Context);
    ok_eq_hex(Status, STATUS_SUCCESS);

    RtlZeroMemory(&Action, sizeof(Action));
    Action.Size = sizeof(Action.Size);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, Context, &Action);
    ok_eq_hex(Status, STATUS_BUFFER_TOO_SMALL);
    ok_eq_ulong(Action.Size, DXGMMS2_CONTEXT_ACTION_V1_SIZE);
    RtlZeroMemory(&Snapshot, sizeof(Snapshot));
    Snapshot.Size = DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1_SIZE;
    Snapshot.Version = DXGMMS2_CONTEXT_STREAM_VERSION_1 + 1;
    Status = Dxgmms2ContextStreamQuery(&Manager, Context, &Snapshot);
    ok_eq_hex(Status, STATUS_REVISION_MISMATCH);
    InitializeAction(&Action);
    Status = Dxgmms2ContextStreamClaimNextAction(&Manager, Context, &Action);
    ok_eq_hex(Status, STATUS_NO_MORE_ENTRIES);
    Status = Dxgmms2ContextStreamDestroy(&Manager, (DXGMMS2_CONTEXT_STREAM_HANDLE)(ULONG_PTR)0xdead);
    ok_eq_hex(Status, STATUS_INVALID_HANDLE);
    Status = Dxgmms2ContextStreamDestroy(&Manager, Context);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerBeginStop(&Manager, Dxgmms2StopReasonPnpStop);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerCompleteStop(&Manager, TRUE);
    ok_eq_hex(Status, STATUS_SUCCESS);
    Status = Dxgmms2ContextStreamManagerPrepareDestroy(&Manager);
    ok_eq_hex(Status, STATUS_SUCCESS);
}

START_TEST(Dxgmms2ContextStream)
{
    TestOrderedPrefixesWaitsAndSignals();
    TestStopClaimDrainAndForcedRetirement();
    TestValidationAndCanaryBoundaries();
}
