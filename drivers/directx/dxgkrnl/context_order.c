/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgmms2 ordered-context client retirement boundary
 */

#include "dxgkrnl_private.h"
#include "vidsch.h"

#define NDEBUG
#include <debug.h>

#define DXGK_CONTEXT_ORDER_OPERATION_SIGNATURE 'rOCX'
#define DXGK_CONTEXT_ORDER_OPERATION_TAG       'oOCX'
#define DXGK_CONTEXT_ORDER_DRAIN_BATCH          16

#define DXGK_CONTEXT_ORDER_TYPE_WORK            1
#define DXGK_CONTEXT_ORDER_TYPE_WAIT            2
#define DXGK_CONTEXT_ORDER_TYPE_SIGNAL          3

typedef struct _DXGK_CONTEXT_ORDER_OPERATION DXGK_CONTEXT_ORDER_OPERATION, *PDXGK_CONTEXT_ORDER_OPERATION;

typedef struct _DXGK_CONTEXT_ORDER_MARKER
{
    LIST_ENTRY ContextEntry;
    PDXGKRNL_CONTEXT Context;
    PDXGK_CONTEXT_ORDER_OPERATION Operation;
    ULONGLONG Sequence;
} DXGK_CONTEXT_ORDER_MARKER, *PDXGK_CONTEXT_ORDER_MARKER;

typedef VOID (*PDXGK_CONTEXT_ORDER_RETIRE_CALLBACK)(_Inout_ PDXGK_CONTEXT_ORDER_OPERATION Operation, _In_ const DXGMMS2_CONTEXT_RETIREMENT_V1 *Retirement);
typedef VOID (*PDXGK_CONTEXT_ORDER_RELEASE_CALLBACK)(_Inout_ PDXGK_CONTEXT_ORDER_OPERATION Operation);

struct _DXGK_CONTEXT_ORDER_OPERATION
{
    ULONG Signature;
    ULONG Type;
    volatile LONG RemainingRetirements;
    ULONG MarkerCount;
    NTSTATUS TerminalStatus;
    PVOID Payload;
    PDXGK_CONTEXT_ORDER_RETIRE_CALLBACK RetireCallback;
    PDXGK_CONTEXT_ORDER_RELEASE_CALLBACK ReleaseCallback;
    DXGK_CONTEXT_ORDER_MARKER Markers[ANYSIZE_ARRAY];
};

static NTSTATUS DxgkpContextOrderCaptureInterface(_In_ PDXGKRNL_CONTEXT Context, _In_ BOOLEAN RequireAdmission, _Out_ DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *Interface)
{
    PDXGKRNL_ADAPTER Adapter;

    if (Context == NULL || Context->Device == NULL || Context->Mms2ContextStream == NULL || Interface == NULL)
        return STATUS_INVALID_PARAMETER;
    Adapter = Context->Device->Adapter;
    if (RequireAdmission && InterlockedCompareExchange(&Adapter->Mms2ContextStreamValid, 0, 0) == 0)
        return STATUS_DEVICE_NOT_READY;
    KeMemoryBarrier();
    *Interface = Adapter->Mms2ContextStreamInterface;
    KeMemoryBarrier();
    if ((RequireAdmission && InterlockedCompareExchange(&Adapter->Mms2ContextStreamValid, 0, 0) == 0) || Interface->AdapterHandle != Adapter->Mms2Adapter || Interface->QueryContextStream == NULL || Interface->DrainRetirements == NULL)
        return STATUS_DEVICE_NOT_READY;
    return STATUS_SUCCESS;
}

static PDXGK_CONTEXT_ORDER_OPERATION DxgkpContextOrderAllocateOperation(_In_ ULONG Type, _In_ ULONG MarkerCount, _In_opt_ PVOID Payload, _In_opt_ PDXGK_CONTEXT_ORDER_RETIRE_CALLBACK RetireCallback, _In_opt_ PDXGK_CONTEXT_ORDER_RELEASE_CALLBACK ReleaseCallback)
{
    PDXGK_CONTEXT_ORDER_OPERATION Operation;
    SIZE_T AllocationSize;
    ULONG Index;

    if (MarkerCount == 0 || MarkerCount > DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS)
        return NULL;
    if ((SIZE_T)MarkerCount > (MAXULONG_PTR - FIELD_OFFSET(DXGK_CONTEXT_ORDER_OPERATION, Markers)) / sizeof(DXGK_CONTEXT_ORDER_MARKER))
        return NULL;
    AllocationSize = FIELD_OFFSET(DXGK_CONTEXT_ORDER_OPERATION, Markers) + ((SIZE_T)MarkerCount * sizeof(DXGK_CONTEXT_ORDER_MARKER));
    Operation = ExAllocatePoolWithTag(NonPagedPool, AllocationSize, DXGK_CONTEXT_ORDER_OPERATION_TAG);
    if (Operation == NULL)
        return NULL;
    RtlZeroMemory(Operation, AllocationSize);
    Operation->Signature = DXGK_CONTEXT_ORDER_OPERATION_SIGNATURE;
    Operation->Type = Type;
    Operation->RemainingRetirements = MarkerCount;
    Operation->MarkerCount = MarkerCount;
    Operation->TerminalStatus = STATUS_SUCCESS;
    Operation->Payload = Payload;
    Operation->RetireCallback = RetireCallback;
    Operation->ReleaseCallback = ReleaseCallback;
    for (Index = 0; Index < MarkerCount; ++Index)
    {
        InitializeListHead(&Operation->Markers[Index].ContextEntry);
        Operation->Markers[Index].Operation = Operation;
    }
    return Operation;
}

static VOID DxgkpContextOrderFreeUnpublishedOperation(_Inout_ PDXGK_CONTEXT_ORDER_OPERATION Operation)
{
    ULONG Index;

    ASSERT(Operation != NULL && Operation->Signature == DXGK_CONTEXT_ORDER_OPERATION_SIGNATURE);
    for (Index = 0; Index < Operation->MarkerCount; ++Index)
    {
        if (Operation->Markers[Index].Context != NULL)
        {
            DxgkDereferenceContext(Operation->Markers[Index].Context);
            Operation->Markers[Index].Context = NULL;
        }
    }
    if (Operation->ReleaseCallback != NULL)
        Operation->ReleaseCallback(Operation);
    Operation->Signature = 0;
    ExFreePoolWithTag(Operation, DXGK_CONTEXT_ORDER_OPERATION_TAG);
}

static VOID DxgkpContextOrderPublishMarker(_Inout_ PDXGK_CONTEXT_ORDER_OPERATION Operation, _In_ ULONG MarkerIndex, _Inout_ PDXGKRNL_CONTEXT Context, _In_ ULONGLONG Sequence)
{
    PDXGK_CONTEXT_ORDER_MARKER Marker = &Operation->Markers[MarkerIndex];
    KIRQL OldIrql;

    ASSERT(Marker->Context == Context && Marker->Sequence == 0 && Sequence != 0 && IsListEmpty(&Marker->ContextEntry));
    Marker->Sequence = Sequence;
    KeAcquireSpinLock(&Context->StreamLock, &OldIrql);
    InsertTailList(&Context->StreamOperationList, &Marker->ContextEntry);
    KeReleaseSpinLock(&Context->StreamLock, OldIrql);
}

static NTSTATUS DxgkpContextOrderDrainRetirements(_Inout_ PDXGKRNL_CONTEXT Context, _In_ const DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *Interface)
{
    DXGMMS2_CONTEXT_RETIREMENT_V1 Records[DXGK_CONTEXT_ORDER_DRAIN_BATCH];
    ULONG RetiredCount;
    ULONG Index;
    NTSTATUS Status;

    do
    {
        RetiredCount = 0;
        RtlZeroMemory(Records, sizeof(Records));
        Status = Interface->DrainRetirements(Interface->AdapterHandle, Context->Mms2ContextStream, Records, RTL_NUMBER_OF(Records), &RetiredCount);
        if (!NT_SUCCESS(Status) && Status != STATUS_MORE_ENTRIES)
            return Status;
        if (RetiredCount > RTL_NUMBER_OF(Records))
            return STATUS_DATA_ERROR;
        for (Index = 0; Index < RetiredCount; ++Index)
            DxgkContextOrderRetire(Context, &Records[Index]);
    } while (Status == STATUS_MORE_ENTRIES);
    return STATUS_SUCCESS;
}

static VOID DxgkpContextOrderWorkRetired(_Inout_ PDXGK_CONTEXT_ORDER_OPERATION Operation, _In_ const DXGMMS2_CONTEXT_RETIREMENT_V1 *Retirement)
{
    PVIDSCH_DMA_PACKET Packet = Operation->Payload;

    UNREFERENCED_PARAMETER(Retirement);
    if (Packet == NULL)
        return;
    InterlockedExchange(&Packet->ContextOrderState, VIDSCH_CONTEXT_ORDER_TERMINAL);
    InterlockedExchangePointer(&Packet->ContextOrderOperation, NULL);
}

static VOID DxgkpContextOrderReleaseWork(_Inout_ PDXGK_CONTEXT_ORDER_OPERATION Operation)
{
    PVIDSCH_DMA_PACKET Packet = Operation->Payload;

    Operation->Payload = NULL;
    if (Packet != NULL)
        VidSchDereferenceContextOrderPacket(Packet);
}

static VOID DxgkpContextOrderReleaseSync(_Inout_ PDXGK_CONTEXT_ORDER_OPERATION Operation)
{
    PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture = Operation->Payload;

    Operation->Payload = NULL;
    if (Capture == NULL)
        return;
    DxgkContextSyncRelease(Capture);
    ExFreePoolWithTag(Capture, DXGK_CONTEXT_ORDER_OPERATION_TAG);
}

VOID DxgkContextOrderWakeDevice(_Inout_ PDXGKRNL_DEVICE Device)
{
    PLIST_ENTRY Entry;

    if (Device == NULL)
        return;
    ExAcquireFastMutex(&Device->DeviceMutex);
    for (Entry = Device->ContextListHead.Flink; Entry != &Device->ContextListHead; Entry = Entry->Flink)
    {
        PDXGKRNL_CONTEXT Context = CONTAINING_RECORD(Entry, DXGKRNL_CONTEXT, ContextListEntry);

        DxgkContextOrderScheduleReferenced(Context);
    }
    ExReleaseFastMutex(&Device->DeviceMutex);
}

static BOOLEAN DxgkpContextOrderFindNextMarker(_In_ PDXGKRNL_CONTEXT Context, _In_ ULONGLONG Sequence, _Out_ PDXGK_CONTEXT_ORDER_MARKER *Marker)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    *Marker = NULL;
    KeAcquireSpinLock(&Context->StreamLock, &OldIrql);
    for (Entry = Context->StreamOperationList.Flink; Entry != &Context->StreamOperationList; Entry = Entry->Flink)
    {
        PDXGK_CONTEXT_ORDER_MARKER Candidate = CONTAINING_RECORD(Entry, DXGK_CONTEXT_ORDER_MARKER, ContextEntry);

        if (Candidate->Sequence == Sequence)
        {
            *Marker = Candidate;
            break;
        }
    }
    KeReleaseSpinLock(&Context->StreamLock, OldIrql);
    return *Marker != NULL;
}

static BOOLEAN DxgkpContextOrderCompleteOneWork(_Inout_ PDXGKRNL_CONTEXT Context, _In_ const DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *Interface)
{
    PDXGK_CONTEXT_ORDER_OPERATION Operation = NULL;
    PVIDSCH_DMA_PACKET Packet = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    NTSTATUS CompletionStatus = STATUS_SUCCESS;

    KeAcquireSpinLock(&Context->StreamLock, &OldIrql);
    for (Entry = Context->StreamOperationList.Flink; Entry != &Context->StreamOperationList; Entry = Entry->Flink)
    {
        PDXGK_CONTEXT_ORDER_MARKER Marker = CONTAINING_RECORD(Entry, DXGK_CONTEXT_ORDER_MARKER, ContextEntry);

        if (Marker->Operation->Type != DXGK_CONTEXT_ORDER_TYPE_WORK)
            continue;
        Packet = Marker->Operation->Payload;
        if (Packet != NULL && InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) == VIDSCH_CONTEXT_ORDER_SUBMITTED && InterlockedCompareExchange(&Packet->ContextOrderCompletionPending, 0, 1) == 1)
        {
            Operation = Marker->Operation;
            CompletionStatus = Packet->ContextOrderCompletionStatus;
            break;
        }
        Packet = NULL;
    }
    KeReleaseSpinLock(&Context->StreamLock, OldIrql);
    if (Operation == NULL || Packet == NULL)
        return FALSE;
    CompletionStatus = Interface->CompleteWork(Interface->AdapterHandle, Context->Mms2ContextStream, Packet->ContextOrderSequence, CompletionStatus);
    if (NT_SUCCESS(CompletionStatus) || CompletionStatus == STATUS_ALREADY_COMPLETE)
        return TRUE;
    InterlockedExchange(&Packet->ContextOrderCompletionPending, 1);
    return FALSE;
}

static BOOLEAN DxgkpContextOrderResubmitOneWork(_Inout_ PDXGKRNL_CONTEXT Context)
{
    PVIDSCH_DMA_PACKET Packet = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Context->StreamLock, &OldIrql);
    for (Entry = Context->StreamOperationList.Flink; Entry != &Context->StreamOperationList; Entry = Entry->Flink)
    {
        PDXGK_CONTEXT_ORDER_MARKER Marker = CONTAINING_RECORD(Entry, DXGK_CONTEXT_ORDER_MARKER, ContextEntry);

        if (Marker->Operation->Type == DXGK_CONTEXT_ORDER_TYPE_WORK && Marker->Operation->Payload != NULL && InterlockedCompareExchange(&((PVIDSCH_DMA_PACKET)Marker->Operation->Payload)->ContextOrderState, 0, 0) == VIDSCH_CONTEXT_ORDER_SUBMITTED && InterlockedCompareExchange(&((PVIDSCH_DMA_PACKET)Marker->Operation->Payload)->ContextOrderResubmissionPending, 0, 0) != 0)
        {
            Packet = Marker->Operation->Payload;
            break;
        }
    }
    KeReleaseSpinLock(&Context->StreamLock, OldIrql);
    return Packet != NULL && VidSchDispatchContextOrderPacketResubmission(Packet);
}

static VOID NTAPI DxgkpContextOrderWorker(_In_ PVOID Parameter)
{
    PDXGKRNL_CONTEXT Context = Parameter;
    DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 Interface;
    DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1 Snapshot;
    DXGMMS2_CONTEXT_ACTION_V1 Action;
    PDXGK_CONTEXT_ORDER_MARKER Marker;
    PDXGK_CONTEXT_ORDER_OPERATION Operation;
    PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture;
    PVIDSCH_DMA_PACKET Packet;
    PDXGKRNL_DEVICE WakeDevice;
    PDXGKRNL_CONTEXT WakeContexts[DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS];
    NTSTATUS ActionStatus;
    NTSTATUS Status;
    ULONG WakeContextCount;
    ULONG WakeIndex;

ContinueWorker:
    (VOID)KeWaitForSingleObject(&Context->StreamAdmissionMutex, Executive, KernelMode, FALSE, NULL);
    Status = DxgkpContextOrderCaptureInterface(Context, FALSE, &Interface);
    if (!NT_SUCCESS(Status))
        goto ReleaseWorker;
    for (;;)
    {
        Status = DxgkpContextOrderDrainRetirements(Context, &Interface);
        if (!NT_SUCCESS(Status))
            break;
        if (DxgkpContextOrderCompleteOneWork(Context, &Interface))
            continue;
        if (DxgkpContextOrderResubmitOneWork(Context))
            break;
        RtlZeroMemory(&Snapshot, sizeof(Snapshot));
        Snapshot.Size = DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1_SIZE;
        Snapshot.Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
        Status = Interface.QueryContextStream(Interface.AdapterHandle, Context->Mms2ContextStream, &Snapshot);
        if (!NT_SUCCESS(Status) || Snapshot.LastSubmittedSequence == Snapshot.LastAdmittedSequence)
            break;
        if (!DxgkpContextOrderFindNextMarker(Context, Snapshot.LastSubmittedSequence + 1, &Marker))
            break;
        Operation = Marker->Operation;
        if (Operation->Type != DXGK_CONTEXT_ORDER_TYPE_WORK && Operation->Type != DXGK_CONTEXT_ORDER_TYPE_WAIT && Operation->Type != DXGK_CONTEXT_ORDER_TYPE_SIGNAL)
            break;
        Packet = NULL;
        if (Operation->Type == DXGK_CONTEXT_ORDER_TYPE_WORK)
        {
            Packet = Operation->Payload;
            if (Packet == NULL || (!VidSchIsContextOrderPacketDispatchable(Packet) && InterlockedCompareExchange((volatile LONG *)&Packet->ContextOrderAbortStatus, 0, 0) == STATUS_PENDING))
                break;
        }
        RtlZeroMemory(&Action, sizeof(Action));
        Action.Size = DXGMMS2_CONTEXT_ACTION_V1_SIZE;
        Action.Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
        Status = Interface.ClaimNextAction(Interface.AdapterHandle, Context->Mms2ContextStream, &Action);
        if (!NT_SUCCESS(Status))
            break;
        if (Action.ClientTag != (ULONGLONG)(ULONG_PTR)Operation || Action.ObjectId != (Operation->Type == DXGK_CONTEXT_ORDER_TYPE_WORK ? 0 : (ULONGLONG)(ULONG_PTR)Operation) || Action.Sequence != Marker->Sequence)
        {
            (VOID)Interface.CommitAction(Interface.AdapterHandle, Context->Mms2ContextStream, Action.Sequence, Action.ClaimToken, STATUS_CANCELLED);
            continue;
        }
        if (Operation->Type == DXGK_CONTEXT_ORDER_TYPE_WORK)
        {
            if (Action.Type != Dxgmms2ContextActionWork)
            {
                (VOID)Interface.CommitAction(Interface.AdapterHandle, Context->Mms2ContextStream, Action.Sequence, Action.ClaimToken, STATUS_CANCELLED);
                continue;
            }
            Packet->ContextOrderClaimToken = Action.ClaimToken;
            KeMemoryBarrier();
            if (InterlockedCompareExchange(&Packet->ContextOrderState, VIDSCH_CONTEXT_ORDER_CLAIMED, VIDSCH_CONTEXT_ORDER_ADMITTED) != VIDSCH_CONTEXT_ORDER_ADMITTED)
            {
                (VOID)Interface.CommitAction(Interface.AdapterHandle, Context->Mms2ContextStream, Action.Sequence, Action.ClaimToken, STATUS_CANCELLED);
                continue;
            }
            if (InterlockedCompareExchange((volatile LONG *)&Packet->ContextOrderAbortStatus, 0, 0) != STATUS_PENDING)
            {
                DxgkContextOrderAbortPacket(Packet, Packet->ContextOrderAbortStatus);
                break;
            }
            VidSchDispatchClaimedContextOrderPacket(Packet);
            break;
        }
        Capture = Operation->Payload;
        if (Operation->Type == DXGK_CONTEXT_ORDER_TYPE_WAIT)
        {
            if (Action.Type != Dxgmms2ContextActionWait || Capture == NULL)
            {
                (VOID)Interface.CommitAction(Interface.AdapterHandle, Context->Mms2ContextStream, Action.Sequence, Action.ClaimToken, STATUS_CANCELLED);
                continue;
            }
            ActionStatus = DxgkContextSyncExecute(Capture);
            if (ActionStatus == STATUS_ALREADY_COMPLETE)
                ActionStatus = STATUS_SUCCESS;
            Status = Interface.CommitAction(Interface.AdapterHandle, Context->Mms2ContextStream, Action.Sequence, Action.ClaimToken, ActionStatus);
            if (ActionStatus == STATUS_PENDING)
                break;
            if (Status == STATUS_INVALID_HANDLE || Status == STATUS_INVALID_PARAMETER || Status == STATUS_OBJECT_TYPE_MISMATCH)
                break;
            continue;
        }
        if (Action.Type != Dxgmms2ContextActionSignal || Capture == NULL)
        {
            (VOID)Interface.CommitAction(Interface.AdapterHandle, Context->Mms2ContextStream, Action.Sequence, Action.ClaimToken, STATUS_CANCELLED);
            continue;
        }
        WakeDevice = Capture->Device;
        if (WakeDevice != NULL && !DxgkReferenceDevice(WakeDevice))
            WakeDevice = NULL;
        ActionStatus = DxgkContextSyncExecute(Capture);
        if (ActionStatus == STATUS_ALREADY_COMPLETE)
            ActionStatus = STATUS_SUCCESS;
        if (ActionStatus == STATUS_PENDING)
            ActionStatus = STATUS_INTERNAL_ERROR;
        WakeContextCount = 0;
        for (WakeIndex = 0; WakeIndex < Operation->MarkerCount; ++WakeIndex)
        {
            PDXGKRNL_CONTEXT WakeContext = Operation->Markers[WakeIndex].Context;

            ASSERT(WakeContext != NULL);
            if (WakeContext != NULL)
            {
                InterlockedIncrement(&WakeContext->ReferenceCount);
                WakeContexts[WakeContextCount++] = WakeContext;
            }
        }
        (VOID)Interface.CommitAction(Interface.AdapterHandle, Context->Mms2ContextStream, Action.Sequence, Action.ClaimToken, ActionStatus);
        for (WakeIndex = 0; WakeIndex < WakeContextCount; ++WakeIndex)
        {
            DxgkContextOrderScheduleReferenced(WakeContexts[WakeIndex]);
            DxgkDereferenceContext(WakeContexts[WakeIndex]);
        }
        if (WakeDevice != NULL)
        {
            DxgkContextOrderWakeDevice(WakeDevice);
            DxgkDereferenceDevice(WakeDevice);
        }
        continue;
    }

ReleaseWorker:
    KeReleaseMutex(&Context->StreamAdmissionMutex, FALSE);
    if (InterlockedCompareExchange(&Context->StreamWorkerQueued, 0, 1) == 1)
    {
        KeSetEvent(&Context->StreamDrainedEvent, IO_NO_INCREMENT, FALSE);
        DxgkDereferenceContext(Context);
        return;
    }
    if (InterlockedCompareExchange(&Context->StreamWorkerQueued, 1, 2) == 2)
        goto ContinueWorker;
    ASSERT(FALSE);
    InterlockedExchange(&Context->StreamWorkerQueued, 0);
    KeSetEvent(&Context->StreamDrainedEvent, IO_NO_INCREMENT, FALSE);
    DxgkDereferenceContext(Context);
}

static VOID DxgkpContextOrderSortContexts(_Out_writes_(ContextCount) PDXGKRNL_CONTEXT *SortedContexts, _In_reads_(ContextCount) PDXGKRNL_CONTEXT const *Contexts, _In_ ULONG ContextCount)
{
    ULONG ContextIndex;

    for (ContextIndex = 0; ContextIndex < ContextCount; ++ContextIndex)
    {
        ULONG InsertIndex = ContextIndex;

        while (InsertIndex != 0 && (ULONG_PTR)SortedContexts[InsertIndex - 1] > (ULONG_PTR)Contexts[ContextIndex])
        {
            SortedContexts[InsertIndex] = SortedContexts[InsertIndex - 1];
            --InsertIndex;
        }
        SortedContexts[InsertIndex] = Contexts[ContextIndex];
    }
}

NTSTATUS DxgkContextOrderAdmitWait(_Inout_ PDXGKRNL_CONTEXT Context, _In_ DXGK_CONTEXT_SYNC_OPERATION SyncOperation, _In_reads_(ObjectCount) const D3DKMT_HANDLE *ObjectHandles, _In_ ULONG ObjectCount, _In_ UINT64 FenceValue, _In_reads_opt_(ObjectCount) CONST UINT64 *FenceValueArray, _In_ KPROCESSOR_MODE AccessMode)
{
    PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture;
    PDXGK_CONTEXT_ORDER_OPERATION Operation;
    DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 Interface;
    DXGMMS2_ADMIT_CONTEXT_WAIT_V1 Info;
    ULONGLONG Sequence = 0;
    NTSTATUS Status;

    PAGED_CODE();
    if (Context == NULL || Context->Device == NULL)
        return STATUS_INVALID_PARAMETER;
    Capture = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Capture), DXGK_CONTEXT_ORDER_OPERATION_TAG);
    if (Capture == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = DxgkContextSyncCapture(Context->Device, Context->Device->OwnerProcess, AccessMode, SyncOperation, ObjectHandles, ObjectCount, 0, FenceValue, FenceValueArray, Capture);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Capture, DXGK_CONTEXT_ORDER_OPERATION_TAG);
        return Status;
    }
    Operation = DxgkpContextOrderAllocateOperation(DXGK_CONTEXT_ORDER_TYPE_WAIT, 1, Capture, NULL, DxgkpContextOrderReleaseSync);
    if (Operation == NULL)
    {
        DxgkContextSyncRelease(Capture);
        ExFreePoolWithTag(Capture, DXGK_CONTEXT_ORDER_OPERATION_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    (VOID)KeWaitForSingleObject(&Context->StreamAdmissionMutex, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&Context->StreamStopping, 0, 0) != 0 || !DxgkReferenceContext(Context))
    {
        Status = STATUS_DELETE_PENDING;
        goto Failure;
    }
    Operation->Markers[0].Context = Context;
    Status = DxgkpContextOrderCaptureInterface(Context, TRUE, &Interface);
    if (!NT_SUCCESS(Status))
        goto Failure;
    if (Interface.AdmitWait == NULL || Interface.ResolveWait == NULL)
    {
        Status = STATUS_REVISION_MISMATCH;
        goto Failure;
    }
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Size = DXGMMS2_ADMIT_CONTEXT_WAIT_V1_SIZE;
    Info.Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Info.ObjectId = (ULONGLONG)(ULONG_PTR)Operation;
    Info.FenceValue = FenceValue;
    Info.ClientTag = (ULONGLONG)(ULONG_PTR)Operation;
    Status = Interface.AdmitWait(Interface.AdapterHandle, Context->Mms2ContextStream, &Info, &Sequence);
    if (!NT_SUCCESS(Status))
        goto Failure;
    DxgkpContextOrderPublishMarker(Operation, 0, Context, Sequence);
    KeReleaseMutex(&Context->StreamAdmissionMutex, FALSE);
    DxgkContextOrderScheduleReferenced(Context);
    return STATUS_SUCCESS;

Failure:
    KeReleaseMutex(&Context->StreamAdmissionMutex, FALSE);
    DxgkpContextOrderFreeUnpublishedOperation(Operation);
    return Status;
}

NTSTATUS DxgkContextOrderAdmitSignal(_In_reads_(ContextCount) PDXGKRNL_CONTEXT const *Contexts, _In_ ULONG ContextCount, _In_ DXGK_CONTEXT_SYNC_OPERATION SyncOperation, _In_reads_opt_(ObjectCount) const D3DKMT_HANDLE *ObjectHandles, _In_ ULONG ObjectCount, _In_ ULONG SignalFlags, _In_ UINT64 PayloadValue, _In_reads_opt_(ObjectCount) CONST UINT64 *PayloadValueArray, _In_ KPROCESSOR_MODE AccessMode)
{
    PDXGKRNL_CONTEXT SortedContexts[DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS];
    DXGMMS2_CONTEXT_STREAM_HANDLE StreamHandles[DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS];
    ULONGLONG Sequences[DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS];
    PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture;
    PDXGK_CONTEXT_ORDER_OPERATION Operation;
    DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 Interface;
    DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1 Info;
    PDXGKRNL_DEVICE Device;
    ULONGLONG TransactionId = 0;
    ULONG LockedCount = 0;
    ULONG ReferencedCount = 0;
    ULONG ContextIndex;
    ULONG CompareIndex;
    NTSTATUS Status;

    PAGED_CODE();
    if (Contexts == NULL || ContextCount == 0 || ContextCount > DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS || Contexts[0] == NULL || Contexts[0]->Device == NULL)
        return STATUS_INVALID_PARAMETER;
    Device = Contexts[0]->Device;
    for (ContextIndex = 0; ContextIndex < ContextCount; ++ContextIndex)
    {
        if (Contexts[ContextIndex] == NULL || Contexts[ContextIndex]->Device != Device)
            return STATUS_INVALID_HANDLE;
        for (CompareIndex = 0; CompareIndex < ContextIndex; ++CompareIndex)
        {
            if (Contexts[CompareIndex] == Contexts[ContextIndex])
                return STATUS_INVALID_PARAMETER;
        }
    }
    Capture = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Capture), DXGK_CONTEXT_ORDER_OPERATION_TAG);
    if (Capture == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = DxgkContextSyncCapture(Device, Device->OwnerProcess, AccessMode, SyncOperation, ObjectHandles, ObjectCount, SignalFlags, PayloadValue, PayloadValueArray, Capture);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Capture, DXGK_CONTEXT_ORDER_OPERATION_TAG);
        return Status;
    }
    Operation = DxgkpContextOrderAllocateOperation(DXGK_CONTEXT_ORDER_TYPE_SIGNAL, ContextCount, Capture, NULL, DxgkpContextOrderReleaseSync);
    if (Operation == NULL)
    {
        DxgkContextSyncRelease(Capture);
        ExFreePoolWithTag(Capture, DXGK_CONTEXT_ORDER_OPERATION_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    DxgkpContextOrderSortContexts(SortedContexts, Contexts, ContextCount);
    for (LockedCount = 0; LockedCount < ContextCount; ++LockedCount)
        (VOID)KeWaitForSingleObject(&SortedContexts[LockedCount]->StreamAdmissionMutex, Executive, KernelMode, FALSE, NULL);
    for (ContextIndex = 0; ContextIndex < ContextCount; ++ContextIndex)
    {
        if (InterlockedCompareExchange(&Contexts[ContextIndex]->StreamStopping, 0, 0) != 0 || !DxgkReferenceContext(Contexts[ContextIndex]))
        {
            Status = STATUS_DELETE_PENDING;
            goto Failure;
        }
        Operation->Markers[ContextIndex].Context = Contexts[ContextIndex];
        ReferencedCount++;
        StreamHandles[ContextIndex] = Contexts[ContextIndex]->Mms2ContextStream;
        Sequences[ContextIndex] = 0;
    }
    Status = DxgkpContextOrderCaptureInterface(Contexts[0], TRUE, &Interface);
    if (!NT_SUCCESS(Status))
        goto Failure;
    if (Interface.AdmitSignal == NULL)
    {
        Status = STATUS_REVISION_MISMATCH;
        goto Failure;
    }
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Size = DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1_SIZE;
    Info.Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Info.Flags = (SignalFlags & DXGK_CONTEXT_SYNC_SIGNAL_AT_SUBMISSION) != 0 ? DXGMMS2_CONTEXT_SIGNAL_AT_SUBMISSION : 0;
    Info.ContextCount = ContextCount;
    Info.ContextHandles = StreamHandles;
    Info.Sequences = Sequences;
    Info.ObjectId = (ULONGLONG)(ULONG_PTR)Operation;
    Info.FenceValue = PayloadValue;
    Info.ClientTag = (ULONGLONG)(ULONG_PTR)Operation;
    Status = Interface.AdmitSignal(Interface.AdapterHandle, &Info, &TransactionId);
    if (!NT_SUCCESS(Status))
        goto Failure;
    ASSERT(TransactionId != 0);
    for (ContextIndex = 0; ContextIndex < ContextCount; ++ContextIndex)
        DxgkpContextOrderPublishMarker(Operation, ContextIndex, Contexts[ContextIndex], Sequences[ContextIndex]);
    while (LockedCount != 0)
        KeReleaseMutex(&SortedContexts[--LockedCount]->StreamAdmissionMutex, FALSE);
    for (ContextIndex = 0; ContextIndex < ContextCount; ++ContextIndex)
        DxgkContextOrderScheduleReferenced(Contexts[ContextIndex]);
    return STATUS_SUCCESS;

Failure:
    while (LockedCount != 0)
        KeReleaseMutex(&SortedContexts[--LockedCount]->StreamAdmissionMutex, FALSE);
    ASSERT(ReferencedCount <= ContextCount);
    DxgkpContextOrderFreeUnpublishedOperation(Operation);
    return Status;
}

VOID DxgkContextOrderScheduleReferenced(_Inout_ PDXGKRNL_CONTEXT Context)
{
    LONG State;

    if (Context == NULL || Context->Mms2ContextStream == NULL)
        return;
    InterlockedIncrement(&Context->ReferenceCount);
    for (;;)
    {
        State = InterlockedCompareExchange(&Context->StreamWorkerQueued, 0, 0);
        if (State == 0)
        {
            if (InterlockedCompareExchange(&Context->StreamWorkerQueued, 1, 0) != 0)
                continue;
            KeClearEvent(&Context->StreamDrainedEvent);
            ExInitializeWorkItem(&Context->StreamWorkItem, DxgkpContextOrderWorker, Context);
            ExQueueWorkItem(&Context->StreamWorkItem, DelayedWorkQueue);
            return;
        }
        if (State == 1)
        {
            if (InterlockedCompareExchange(&Context->StreamWorkerQueued, 2, 1) != 1)
                continue;
            break;
        }
        if (State == 2)
            break;
        ASSERT(FALSE);
        break;
    }
    DxgkDereferenceContext(Context);
}

NTSTATUS DxgkContextOrderAdmitPacket(_Inout_ PDXGKRNL_CONTEXT Context, _Inout_ PVIDSCH_DMA_PACKET Packet)
{
    DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 Interface;
    DXGMMS2_ADMIT_CONTEXT_WORK_V1 Info;
    PDXGK_CONTEXT_ORDER_OPERATION Operation;
    ULONGLONG Sequence = 0;
    NTSTATUS Status;

    PAGED_CODE();
    if (Context == NULL || Packet == NULL || Packet->Context != Context || Packet->ContextOrderOperation != NULL)
        return STATUS_INVALID_PARAMETER;
    Operation = DxgkpContextOrderAllocateOperation(DXGK_CONTEXT_ORDER_TYPE_WORK, 1, Packet, DxgkpContextOrderWorkRetired, DxgkpContextOrderReleaseWork);
    if (Operation == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    VidSchReferenceContextOrderPacket(Packet);
    (VOID)KeWaitForSingleObject(&Context->StreamAdmissionMutex, Executive, KernelMode, FALSE, NULL);
    if (InterlockedCompareExchange(&Context->StreamStopping, 0, 0) != 0 || !DxgkReferenceContext(Context))
    {
        Status = STATUS_DELETE_PENDING;
        goto Failure;
    }
    Operation->Markers[0].Context = Context;
    Status = DxgkpContextOrderCaptureInterface(Context, TRUE, &Interface);
    if (!NT_SUCCESS(Status))
        goto Failure;
    if (Interface.AdmitWork == NULL)
    {
        Status = STATUS_REVISION_MISMATCH;
        goto Failure;
    }
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Size = DXGMMS2_ADMIT_CONTEXT_WORK_V1_SIZE;
    Info.Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Info.ClientTag = (ULONGLONG)(ULONG_PTR)Operation;
    Status = Interface.AdmitWork(Interface.AdapterHandle, Context->Mms2ContextStream, &Info, &Sequence);
    if (!NT_SUCCESS(Status))
        goto Failure;
    DxgkpContextOrderPublishMarker(Operation, 0, Context, Sequence);
    Packet->ContextOrderSequence = Sequence;
    Packet->ContextOrderClaimToken = 0;
    Packet->ContextOrderCompletionStatus = STATUS_PENDING;
    Packet->ContextOrderCompletionPending = 0;
    Packet->ContextOrderResubmissionPending = 0;
    Packet->ContextOrderAbortStatus = STATUS_PENDING;
    Packet->ContextOrderOperation = Operation;
    InterlockedExchange(&Packet->ContextOrderState, VIDSCH_CONTEXT_ORDER_ADMITTED);
    return STATUS_SUCCESS;

Failure:
    KeReleaseMutex(&Context->StreamAdmissionMutex, FALSE);
    DxgkpContextOrderFreeUnpublishedOperation(Operation);
    return Status;
}

VOID DxgkContextOrderPublishAdmittedPacket(_Inout_ PDXGKRNL_CONTEXT Context)
{
    PAGED_CODE();
    ASSERT(Context != NULL);
    KeReleaseMutex(&Context->StreamAdmissionMutex, FALSE);
    DxgkContextOrderScheduleReferenced(Context);
}

static VOID DxgkpContextOrderCommitClaimedPacket(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS SubmissionStatus)
{
    PDXGKRNL_CONTEXT Context;
    DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 Interface;
    NTSTATUS AbortStatus;
    NTSTATUS Status;

    PAGED_CODE();
    ASSERT(Packet != NULL && Packet->ContextOrderOperation != NULL && InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) == VIDSCH_CONTEXT_ORDER_COMPLETING);
    if (Packet == NULL || Packet->ContextOrderOperation == NULL)
        return;
    Context = Packet->Context;
    Status = DxgkpContextOrderCaptureInterface(Context, FALSE, &Interface);
    if (NT_SUCCESS(Status) && Interface.CommitAction != NULL)
        Status = Interface.CommitAction(Interface.AdapterHandle, Context->Mms2ContextStream, Packet->ContextOrderSequence, Packet->ContextOrderClaimToken, SubmissionStatus);
    Packet->ContextOrderClaimToken = 0;
    InterlockedExchange(&Packet->ContextOrderState, NT_SUCCESS(SubmissionStatus) && NT_SUCCESS(Status) ? VIDSCH_CONTEXT_ORDER_SUBMITTED : VIDSCH_CONTEXT_ORDER_TERMINAL);
    AbortStatus = (NTSTATUS)InterlockedCompareExchange((volatile LONG *)&Packet->ContextOrderAbortStatus, 0, 0);
    if (InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0) == VIDSCH_CONTEXT_ORDER_SUBMITTED && AbortStatus != STATUS_PENDING)
        DxgkContextOrderCompletePacket(Packet, AbortStatus);
    DxgkContextOrderScheduleReferenced(Context);
}

VOID DxgkContextOrderCommitPacket(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS SubmissionStatus)
{
    PAGED_CODE();
    if (Packet == NULL || Packet->ContextOrderOperation == NULL || InterlockedCompareExchange(&Packet->ContextOrderState, VIDSCH_CONTEXT_ORDER_COMPLETING, VIDSCH_CONTEXT_ORDER_DISPATCHING) != VIDSCH_CONTEXT_ORDER_DISPATCHING)
        return;
    DxgkpContextOrderCommitClaimedPacket(Packet, SubmissionStatus);
}

VOID DxgkContextOrderCompletePacket(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS CompletionStatus)
{
    PDXGKRNL_CONTEXT Context;

    if (Packet == NULL || CompletionStatus == STATUS_PENDING || InterlockedCompareExchangePointer(&Packet->ContextOrderOperation, NULL, NULL) == NULL)
        return;
    Context = Packet->Context;
    if ((NTSTATUS)InterlockedCompareExchange((volatile LONG *)&Packet->ContextOrderCompletionStatus, (LONG)CompletionStatus, (LONG)STATUS_PENDING) == STATUS_PENDING)
    {
        KeMemoryBarrier();
        InterlockedExchange(&Packet->ContextOrderCompletionPending, 1);
    }
    DxgkContextOrderScheduleReferenced(Context);
}

VOID DxgkContextOrderAbortPacket(_Inout_ PVIDSCH_DMA_PACKET Packet, _In_ NTSTATUS AbortStatus)
{
    LONG State;

    PAGED_CODE();
    if (Packet == NULL || AbortStatus == STATUS_PENDING || NT_SUCCESS(AbortStatus) || InterlockedCompareExchangePointer(&Packet->ContextOrderOperation, NULL, NULL) == NULL)
        return;
    (VOID)InterlockedCompareExchange((volatile LONG *)&Packet->ContextOrderAbortStatus, (LONG)AbortStatus, (LONG)STATUS_PENDING);
    AbortStatus = (NTSTATUS)InterlockedCompareExchange((volatile LONG *)&Packet->ContextOrderAbortStatus, 0, 0);
    for (;;)
    {
        State = InterlockedCompareExchange(&Packet->ContextOrderState, 0, 0);
        if (State == VIDSCH_CONTEXT_ORDER_ADMITTED)
        {
            DxgkContextOrderScheduleReferenced((PDXGKRNL_CONTEXT)Packet->Context);
            return;
        }
        if (State == VIDSCH_CONTEXT_ORDER_CLAIMED || State == VIDSCH_CONTEXT_ORDER_DISPATCHING)
        {
            if (InterlockedCompareExchange(&Packet->ContextOrderState, VIDSCH_CONTEXT_ORDER_COMPLETING, State) != State)
                continue;
            DxgkpContextOrderCommitClaimedPacket(Packet, AbortStatus);
            return;
        }
        if (State == VIDSCH_CONTEXT_ORDER_SUBMITTED)
        {
            DxgkContextOrderCompletePacket(Packet, AbortStatus);
            return;
        }
        if (State == VIDSCH_CONTEXT_ORDER_TERMINAL)
            DxgkContextOrderScheduleReferenced((PDXGKRNL_CONTEXT)Packet->Context);
        return;
    }
}

/*
 * Phase-2 operations retain their client payload until dxgmms2 returns the
 * corresponding retirement record.  The lifecycle plumbing can already
 * drain records before the first live operation is admitted, so zero tags
 * are deliberately harmless.  Nonzero tags are handled by the operation
 * owner added in this module; silently discarding one would leak retained
 * synchronization objects or a scheduler packet.
 */
VOID
DxgkContextOrderRetire(
    _Inout_ PDXGKRNL_CONTEXT Context,
    _In_ const DXGMMS2_CONTEXT_RETIREMENT_V1 *Retirement)
{
    PDXGK_CONTEXT_ORDER_OPERATION Operation = NULL;
    PDXGK_CONTEXT_ORDER_MARKER Marker = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    ULONG Index;

    if (Retirement == NULL || Retirement->ClientTag == 0)
        return;
    if (Context == NULL)
    {
        DPRINT1("DXGKRNL: invalid context retirement tag %I64u sequence %I64u\n", Retirement->ClientTag, Retirement->Sequence);
        ASSERT(FALSE);
        return;
    }
    KeAcquireSpinLock(&Context->StreamLock, &OldIrql);
    for (Entry = Context->StreamOperationList.Flink; Entry != &Context->StreamOperationList; Entry = Entry->Flink)
    {
        PDXGK_CONTEXT_ORDER_MARKER Candidate = CONTAINING_RECORD(Entry, DXGK_CONTEXT_ORDER_MARKER, ContextEntry);

        if ((ULONGLONG)(ULONG_PTR)Candidate->Operation == Retirement->ClientTag && Candidate->Sequence == Retirement->Sequence)
        {
            Marker = Candidate;
            Operation = Candidate->Operation;
            RemoveEntryList(&Marker->ContextEntry);
            InitializeListHead(&Marker->ContextEntry);
            break;
        }
    }
    KeReleaseSpinLock(&Context->StreamLock, OldIrql);
    if (Marker == NULL || Operation == NULL || Operation->Signature != DXGK_CONTEXT_ORDER_OPERATION_SIGNATURE)
    {
        DPRINT1("DXGKRNL: unmatched context retirement tag %I64u sequence %I64u\n", Retirement->ClientTag, Retirement->Sequence);
        ASSERT(FALSE);
        return;
    }
    ASSERT(Marker->Context == Context);
    Marker->Context = NULL;
    if (!NT_SUCCESS(Retirement->TerminalStatus) && NT_SUCCESS(Operation->TerminalStatus))
        Operation->TerminalStatus = Retirement->TerminalStatus;
    if (Operation->RetireCallback != NULL)
        Operation->RetireCallback(Operation, Retirement);
    DxgkDereferenceContext(Context);
    if (InterlockedDecrement(&Operation->RemainingRetirements) != 0)
        return;
    for (Index = 0; Index < Operation->MarkerCount; ++Index)
        ASSERT(Operation->Markers[Index].Context == NULL && IsListEmpty(&Operation->Markers[Index].ContextEntry));
    if (Operation->ReleaseCallback != NULL)
        Operation->ReleaseCallback(Operation);
    Operation->Signature = 0;
    ExFreePoolWithTag(Operation, DXGK_CONTEXT_ORDER_OPERATION_TAG);
}
