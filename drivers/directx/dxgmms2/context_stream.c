/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Typed provider-facing logical context-stream interface
 */

#include "dxgmms2_private.h"

#ifdef _WIN64
C_ASSERT(sizeof(DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1) == 24);
C_ASSERT(sizeof(DXGMMS2_ADMIT_CONTEXT_WORK_V1) == 16);
C_ASSERT(sizeof(DXGMMS2_ADMIT_CONTEXT_WAIT_V1) == 40);
C_ASSERT(sizeof(DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1) == 56);
C_ASSERT(sizeof(DXGMMS2_CONTEXT_ACTION_V1) == 72);
C_ASSERT(sizeof(DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1) == 16);
C_ASSERT(sizeof(DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1) == 64);
C_ASSERT(sizeof(DXGMMS2_CONTEXT_RETIREMENT_V1) == 40);
C_ASSERT(sizeof(DXGMMS2_CONTEXT_STREAM_INTERFACE_V1) == 128);
C_ASSERT(FIELD_OFFSET(DXGMMS2_CONTEXT_STREAM_INTERFACE_V1, AdapterHandle) == 16);
C_ASSERT(FIELD_OFFSET(DXGMMS2_CONTEXT_STREAM_INTERFACE_V1, QueryContextStream) == 112);
C_ASSERT(FIELD_OFFSET(DXGMMS2_CONTEXT_STREAM_INTERFACE_V1, DrainRetirements) == 120);
#endif

static NTSTATUS NTAPI Dxgmms2CreateContextStream(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_CREATE_CONTEXT_STREAM_INFO_V1 *Info, _Out_ DXGMMS2_CONTEXT_STREAM_HANDLE *ContextHandle)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamCreate(&Context->ContextStreamManager, Info, ContextHandle);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2DestroyContextStream(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamDestroy(&Context->ContextStreamManager, ContextHandle);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2AdmitContextWork(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ const DXGMMS2_ADMIT_CONTEXT_WORK_V1 *Info, _Out_ ULONGLONG *Sequence)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamAdmitWork(&Context->ContextStreamManager, ContextHandle, Info, Sequence);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2AdmitContextWait(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ const DXGMMS2_ADMIT_CONTEXT_WAIT_V1 *Info, _Out_ ULONGLONG *Sequence)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamAdmitWait(&Context->ContextStreamManager, ContextHandle, Info, Sequence);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2AdmitContextSignal(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_ADMIT_CONTEXT_SIGNAL_V1 *Info, _Out_ ULONGLONG *TransactionId)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamAdmitSignal(&Context->ContextStreamManager, Info, TransactionId);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2ResolveContextWait(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ ULONGLONG Sequence, _In_ NTSTATUS WaitStatus)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamResolveWait(&Context->ContextStreamManager, ContextHandle, Sequence, WaitStatus);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2ClaimContextAction(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _Inout_ DXGMMS2_CONTEXT_ACTION_V1 *Action)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamClaimNextAction(&Context->ContextStreamManager, ContextHandle, Action);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2CommitContextAction(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ ULONGLONG Sequence, _In_ ULONGLONG ClaimToken, _In_ NTSTATUS ActionStatus)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamCommitAction(&Context->ContextStreamManager, ContextHandle, Sequence, ClaimToken, ActionStatus);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2CompleteContextWork(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ ULONGLONG Sequence, _In_ NTSTATUS CompletionStatus)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamCompleteWork(&Context->ContextStreamManager, ContextHandle, Sequence, CompletionStatus);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2CancelContextStream(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _In_ const DXGMMS2_CANCEL_CONTEXT_STREAM_INFO_V1 *Info)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamCancel(&Context->ContextStreamManager, ContextHandle, Info);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2QueryContextStream(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _Inout_ DXGMMS2_CONTEXT_STREAM_SNAPSHOT_V1 *Snapshot)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamQuery(&Context->ContextStreamManager, ContextHandle, Snapshot);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

static NTSTATUS NTAPI Dxgmms2DrainContextRetirements(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ DXGMMS2_CONTEXT_STREAM_HANDLE ContextHandle, _Out_writes_to_(RecordCapacity, *RetiredCount) DXGMMS2_CONTEXT_RETIREMENT_V1 *Records, _In_ ULONG RecordCapacity, _Out_ ULONG *RetiredCount)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    NTSTATUS Status;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = Dxgmms2ContextStreamDrainRetirements(&Context->ContextStreamManager, ContextHandle, Records, RecordCapacity, RetiredCount);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

NTSTATUS NTAPI Dxgmms2QueryContextStreamInterface(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _Inout_ DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *ContextStreamInterface)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    ULONG Capacity;

    PAGED_CODE();
    if (ContextStreamInterface == NULL)
        return STATUS_INVALID_PARAMETER;
    Capacity = ContextStreamInterface->Size;
    if (Capacity < FIELD_OFFSET(DXGMMS2_CONTEXT_STREAM_INTERFACE_V1, Version) + sizeof(ContextStreamInterface->Version))
    {
        if (Capacity >= sizeof(ContextStreamInterface->Size))
            ContextStreamInterface->Size = DXGMMS2_CONTEXT_STREAM_INTERFACE_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (ContextStreamInterface->Version != DXGMMS2_CONTEXT_STREAM_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Capacity < DXGMMS2_CONTEXT_STREAM_INTERFACE_V1_SIZE)
    {
        ContextStreamInterface->Size = DXGMMS2_CONTEXT_STREAM_INTERFACE_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    if (InterlockedCompareExchange(&Context->State, 0, 0) != Dxgmms2AdapterStarted)
    {
        Dxgmms2DereferenceAdapterContext(Context);
        return STATUS_INVALID_DEVICE_STATE;
    }
    RtlZeroMemory(ContextStreamInterface, DXGMMS2_CONTEXT_STREAM_INTERFACE_V1_SIZE);
    ContextStreamInterface->Size = DXGMMS2_CONTEXT_STREAM_INTERFACE_V1_SIZE;
    ContextStreamInterface->Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    ContextStreamInterface->InterfaceFlags = 0;
    ContextStreamInterface->Generation = Dxgmms2ContextStreamManagerGetGeneration(&Context->ContextStreamManager);
    ContextStreamInterface->AdapterHandle = Context->PublicHandle;
    ContextStreamInterface->MaximumQueueDepth = DXGMMS2_CONTEXT_STREAM_MAX_QUEUE_DEPTH;
    ContextStreamInterface->MaximumBroadcastContexts = DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS;
    ContextStreamInterface->CreateContextStream = Dxgmms2CreateContextStream;
    ContextStreamInterface->DestroyContextStream = Dxgmms2DestroyContextStream;
    ContextStreamInterface->AdmitWork = Dxgmms2AdmitContextWork;
    ContextStreamInterface->AdmitWait = Dxgmms2AdmitContextWait;
    ContextStreamInterface->AdmitSignal = Dxgmms2AdmitContextSignal;
    ContextStreamInterface->ResolveWait = Dxgmms2ResolveContextWait;
    ContextStreamInterface->ClaimNextAction = Dxgmms2ClaimContextAction;
    ContextStreamInterface->CommitAction = Dxgmms2CommitContextAction;
    ContextStreamInterface->CompleteWork = Dxgmms2CompleteContextWork;
    ContextStreamInterface->CancelContextStream = Dxgmms2CancelContextStream;
    ContextStreamInterface->QueryContextStream = Dxgmms2QueryContextStream;
    ContextStreamInterface->DrainRetirements = Dxgmms2DrainContextRetirements;
    Dxgmms2DereferenceAdapterContext(Context);
    return STATUS_SUCCESS;
}
