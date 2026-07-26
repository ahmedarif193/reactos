/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     dxgkrnl-side client for the typed dxgmms2 provider ABI v3 boundary
 */

#include "dxgkrnl_private.h"
#include "handles.h"
#include "dxgmms2_client.h"

#define DXGKP_MMS2_CANARY 0x324D4D5343414E59ULL

typedef struct _DXGKP_MMS2_PROVIDER_BUFFER
{
    DXGMMS2_PROVIDER_INTERFACE_V5 Interface;
    ULONGLONG Canary;
} DXGKP_MMS2_PROVIDER_BUFFER;

typedef struct _DXGKP_MMS2_START_RESULT_BUFFER
{
    DXGMMS2_START_ADAPTER_RESULT_V1 Result;
    ULONGLONG Canary;
} DXGKP_MMS2_START_RESULT_BUFFER;

#ifdef _WIN64
C_ASSERT(sizeof(DXGMMS2_DXGKRNL_INTERFACE_V1) == 32);
C_ASSERT(sizeof(DXGMMS2_CREATE_ADAPTER_INFO_V1) == 24);
C_ASSERT(sizeof(DXGMMS2_START_ADAPTER_INFO_V1) == 32);
C_ASSERT(sizeof(DXGMMS2_START_ADAPTER_RESULT_V1) == 24);
C_ASSERT(sizeof(DXGMMS2_STOP_ADAPTER_INFO_V1) == 16);
C_ASSERT(sizeof(DXGMMS2_PROVIDER_INTERFACE_V1) == 64);
C_ASSERT(sizeof(DXGMMS2_PROVIDER_INTERFACE_V2) == 72);
C_ASSERT(sizeof(DXGMMS2_PROVIDER_INTERFACE_V3) == 80);
C_ASSERT(sizeof(DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1) == 96);
C_ASSERT(sizeof(DXGMMS2_FENCE_SNAPSHOT_V1) == 32);
C_ASSERT(sizeof(DXGMMS2_CONTEXT_STREAM_INTERFACE_V1) == 128);
C_ASSERT(FIELD_OFFSET(DXGMMS2_PROVIDER_INTERFACE_V3, QueryContextStreamInterface) == 72);
C_ASSERT(FIELD_OFFSET(DXGMMS2_CONTEXT_STREAM_INTERFACE_V1, AdapterHandle) == 16);
C_ASSERT(FIELD_OFFSET(DXGMMS2_CONTEXT_STREAM_INTERFACE_V1, QueryContextStream) == 112);
C_ASSERT(FIELD_OFFSET(DXGMMS2_CONTEXT_STREAM_INTERFACE_V1, DrainRetirements) == 120);
C_ASSERT(sizeof(DXGMMS2_PROVIDER_INTERFACE_V5) == 96);
C_ASSERT(DXGMMS2_WDDM_VERSION_2_0 == DXGKDDI_INTERFACE_VERSION_WDDM2_0);
C_ASSERT(FIELD_OFFSET(DXGMMS2_PROVIDER_INTERFACE_V5, QuerySchedulerInterface) == 80);
C_ASSERT(FIELD_OFFSET(DXGMMS2_PROVIDER_INTERFACE_V5, QueryVidMmInterface) == 88);
C_ASSERT(FIELD_OFFSET(DXGKP_MMS2_PROVIDER_BUFFER, Canary) == DXGMMS2_PROVIDER_INTERFACE_V5_SIZE);
C_ASSERT(FIELD_OFFSET(DXGKP_MMS2_START_RESULT_BUFFER, Canary) == DXGMMS2_START_ADAPTER_RESULT_V1_SIZE);
#endif

static volatile LONG DxgkpMms2ClientState = 0;
static DXGMMS2_PROVIDER_INTERFACE_V5 DxgkpMms2Provider;
static EX_RUNDOWN_REF DxgkpMms2CallRundown;

static BOOLEAN
DxgkpMms2AcquireProviderCall(VOID)
{
    if (InterlockedCompareExchange(&DxgkpMms2ClientState, 0, 0) != 2)
        return FALSE;
    if (!ExAcquireRundownProtection(&DxgkpMms2CallRundown))
        return FALSE;
    if (InterlockedCompareExchange(&DxgkpMms2ClientState, 0, 0) != 2)
    {
        ExReleaseRundownProtection(&DxgkpMms2CallRundown);
        return FALSE;
    }
    return TRUE;
}

static VOID
DxgkpMms2ReleaseProviderCall(VOID)
{
    ExReleaseRundownProtection(&DxgkpMms2CallRundown);
}

static BOOLEAN
NTAPI
DxgkpMms2ReferenceAdapter(_In_opt_ PVOID ClientContext, _In_ PVOID AdapterCookie)
{
    UNREFERENCED_PARAMETER(ClientContext);
    return DxgkReferenceAdapter((PDXGKRNL_ADAPTER)AdapterCookie);
}

static VOID
NTAPI
DxgkpMms2DereferenceAdapter(_In_opt_ PVOID ClientContext, _In_ PVOID AdapterCookie)
{
    UNREFERENCED_PARAMETER(ClientContext);
    DxgkDereferenceAdapter((PDXGKRNL_ADAPTER)AdapterCookie);
}

NTSTATUS
DxgkpMms2Initialize(VOID)
{
    DXGMMS2_DXGKRNL_INTERFACE_V1 ClientInterface;
    DXGKP_MMS2_PROVIDER_BUFFER ProviderBuffer;
    LARGE_INTEGER Delay;
    NTSTATUS Status;
    LONG State;

    PAGED_CODE();
    State = InterlockedCompareExchange(&DxgkpMms2ClientState, 1, 0);
    if (State != 0)
    {
        Delay.QuadPart = -(LONGLONG)(10 * 1000);
        while ((State = InterlockedCompareExchange(&DxgkpMms2ClientState, 0, 0)) == 1)
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);
        return State == 2 ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
    }
    ExInitializeRundownProtection(&DxgkpMms2CallRundown);

    RtlZeroMemory(&ClientInterface, sizeof(ClientInterface));
    ClientInterface.Size = DXGMMS2_DXGKRNL_INTERFACE_V1_SIZE;
    ClientInterface.Version = DXGMMS2_ABI_VERSION_1;
    ClientInterface.ClientContext = NULL;
    ClientInterface.ReferenceAdapter = DxgkpMms2ReferenceAdapter;
    ClientInterface.DereferenceAdapter = DxgkpMms2DereferenceAdapter;

    RtlZeroMemory(&ProviderBuffer, sizeof(ProviderBuffer));
    ProviderBuffer.Interface.Size = DXGMMS2_PROVIDER_INTERFACE_V5_SIZE;
    ProviderBuffer.Interface.Version = DXGMMS2_ABI_VERSION_5;
    ProviderBuffer.Canary = DXGKP_MMS2_CANARY;
    Status = DxgkMms2Register(&ClientInterface, (PDXGMMS2_PROVIDER_INTERFACE_V1)&ProviderBuffer.Interface);
    if (ProviderBuffer.Canary != DXGKP_MMS2_CANARY)
    {
        InterlockedExchange(&DxgkpMms2ClientState, 3);
        return STATUS_REVISION_MISMATCH;
    }
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&DxgkpMms2ClientState, 3);
        return Status;
    }

    if (ProviderBuffer.Canary != DXGKP_MMS2_CANARY || ProviderBuffer.Interface.Size != DXGMMS2_PROVIDER_INTERFACE_V5_SIZE || ProviderBuffer.Interface.Version != DXGMMS2_ABI_VERSION_5 || ProviderBuffer.Interface.ProviderFlags != 0 || ProviderBuffer.Interface.Reserved != 0 || ProviderBuffer.Interface.RegistrationHandle == NULL || ProviderBuffer.Interface.CreateAdapter == NULL || ProviderBuffer.Interface.StartAdapter == NULL || ProviderBuffer.Interface.BeginStopAdapter == NULL || ProviderBuffer.Interface.CompleteStopAdapter == NULL || ProviderBuffer.Interface.DestroyAdapter == NULL || ProviderBuffer.Interface.QuerySchedulerTimelineInterface == NULL || ProviderBuffer.Interface.QueryContextStreamInterface == NULL || ProviderBuffer.Interface.QuerySchedulerInterface == NULL || ProviderBuffer.Interface.QueryVidMmInterface == NULL)
    {
        if (ProviderBuffer.Interface.RegistrationHandle != NULL)
            (VOID)DxgkMms2Unregister(ProviderBuffer.Interface.RegistrationHandle);
        InterlockedExchange(&DxgkpMms2ClientState, 3);
        return STATUS_REVISION_MISMATCH;
    }

    RtlCopyMemory(&DxgkpMms2Provider, &ProviderBuffer.Interface, sizeof(DxgkpMms2Provider));
    KeMemoryBarrier();
    InterlockedExchange(&DxgkpMms2ClientState, 2);
    return STATUS_SUCCESS;
}

NTSTATUS DxgkpMms2QuerySchedulerTimeline(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _Out_ DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 *TimelineInterface)
{
    typedef struct _DXGKP_MMS2_TIMELINE_BUFFER { DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1 Interface; ULONGLONG Canary; } DXGKP_MMS2_TIMELINE_BUFFER;
    DXGKP_MMS2_TIMELINE_BUFFER Buffer;
    NTSTATUS Status;

    PAGED_CODE();
    if (TimelineInterface == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(TimelineInterface, sizeof(*TimelineInterface));
    if (!DxgkpMms2AcquireProviderCall())
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&Buffer, sizeof(Buffer));
    Buffer.Interface.Size = DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1_SIZE;
    Buffer.Interface.Version = DXGMMS2_SCHEDULER_TIMELINE_VERSION_1;
    Buffer.Canary = DXGKP_MMS2_CANARY;
    Status = DxgkpMms2Provider.QuerySchedulerTimelineInterface(Mms2Adapter, &Buffer.Interface);
    DxgkpMms2ReleaseProviderCall();
    if (Buffer.Canary != DXGKP_MMS2_CANARY)
        return STATUS_REVISION_MISMATCH;
    if (!NT_SUCCESS(Status))
        return Status;
    if (Buffer.Interface.Size != DXGMMS2_SCHEDULER_TIMELINE_INTERFACE_V1_SIZE || Buffer.Interface.Version != DXGMMS2_SCHEDULER_TIMELINE_VERSION_1 || (Buffer.Interface.FeatureFlags & ~DXGMMS2_TIMELINE_FEATURE_VALID_MASK) != 0 || (Buffer.Interface.FeatureFlags & DXGMMS2_TIMELINE_FEATURE_FENCE_IDENTITIES) == 0 || Buffer.Interface.Generation == 0 || Buffer.Interface.TimelineHandle == NULL || Buffer.Interface.NodeCount > DXGK_MAX_TRACKED_NODES || Buffer.Interface.Reserved != 0 || Buffer.Interface.AllocateFence == NULL || Buffer.Interface.ReserveFence == NULL || Buffer.Interface.PublishFence == NULL || Buffer.Interface.NotifyFenceCompletion == NULL || Buffer.Interface.IsFencePublished == NULL || Buffer.Interface.ReleaseFence == NULL || Buffer.Interface.ResetFenceIdentities == NULL || Buffer.Interface.QueryFenceSnapshot == NULL)
        return STATUS_REVISION_MISMATCH;
    *TimelineInterface = Buffer.Interface;
    return STATUS_SUCCESS;
}

NTSTATUS DxgkpMms2QueryContextStreamInterface(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _Out_ DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 *ContextStreamInterface)
{
    typedef struct _DXGKP_MMS2_CONTEXT_STREAM_BUFFER { DXGMMS2_CONTEXT_STREAM_INTERFACE_V1 Interface; ULONGLONG Canary; } DXGKP_MMS2_CONTEXT_STREAM_BUFFER;
    DXGKP_MMS2_CONTEXT_STREAM_BUFFER Buffer;
    NTSTATUS Status;

    PAGED_CODE();
    if (Mms2Adapter == NULL || ContextStreamInterface == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(ContextStreamInterface, sizeof(*ContextStreamInterface));
    if (!DxgkpMms2AcquireProviderCall())
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&Buffer, sizeof(Buffer));
    Buffer.Interface.Size = DXGMMS2_CONTEXT_STREAM_INTERFACE_V1_SIZE;
    Buffer.Interface.Version = DXGMMS2_CONTEXT_STREAM_VERSION_1;
    Buffer.Canary = DXGKP_MMS2_CANARY;
    Status = DxgkpMms2Provider.QueryContextStreamInterface(Mms2Adapter, &Buffer.Interface);
    DxgkpMms2ReleaseProviderCall();
    if (Buffer.Canary != DXGKP_MMS2_CANARY)
        return STATUS_REVISION_MISMATCH;
    if (!NT_SUCCESS(Status))
        return Status;
    if (Buffer.Interface.Size != DXGMMS2_CONTEXT_STREAM_INTERFACE_V1_SIZE || Buffer.Interface.Version != DXGMMS2_CONTEXT_STREAM_VERSION_1 || Buffer.Interface.InterfaceFlags != 0 || Buffer.Interface.Generation == 0 || Buffer.Interface.AdapterHandle != Mms2Adapter || Buffer.Interface.MaximumQueueDepth < DXGMMS2_CONTEXT_STREAM_DEFAULT_QUEUE_DEPTH || Buffer.Interface.MaximumQueueDepth > DXGMMS2_CONTEXT_STREAM_MAX_QUEUE_DEPTH || Buffer.Interface.MaximumBroadcastContexts == 0 || Buffer.Interface.MaximumBroadcastContexts > DXGMMS2_CONTEXT_STREAM_MAX_BROADCAST_CONTEXTS || Buffer.Interface.CreateContextStream == NULL || Buffer.Interface.DestroyContextStream == NULL || Buffer.Interface.AdmitWork == NULL || Buffer.Interface.AdmitWait == NULL || Buffer.Interface.AdmitSignal == NULL || Buffer.Interface.ResolveWait == NULL || Buffer.Interface.ClaimNextAction == NULL || Buffer.Interface.CommitAction == NULL || Buffer.Interface.CompleteWork == NULL || Buffer.Interface.CancelContextStream == NULL || Buffer.Interface.QueryContextStream == NULL || Buffer.Interface.DrainRetirements == NULL)
        return STATUS_REVISION_MISMATCH;
    *ContextStreamInterface = Buffer.Interface;
    return STATUS_SUCCESS;
}

NTSTATUS DxgkpMms2QuerySchedulerInterface(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _Out_ DXGMMS2_SCHEDULER_INTERFACE_V1 *SchedulerInterface)
{
    typedef struct _DXGKP_MMS2_SCHEDULER_BUFFER { DXGMMS2_SCHEDULER_INTERFACE_V1 Interface; ULONGLONG Canary; } DXGKP_MMS2_SCHEDULER_BUFFER;
    DXGKP_MMS2_SCHEDULER_BUFFER Buffer;
    NTSTATUS Status;

    PAGED_CODE();
    if (Mms2Adapter == NULL || SchedulerInterface == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(SchedulerInterface, sizeof(*SchedulerInterface));
    if (!DxgkpMms2AcquireProviderCall())
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&Buffer, sizeof(Buffer));
    Buffer.Interface.Size = DXGMMS2_SCHEDULER_INTERFACE_V1_SIZE;
    Buffer.Interface.Version = DXGMMS2_SCHEDULER_VERSION_1;
    Buffer.Canary = DXGKP_MMS2_CANARY;
    Status = DxgkpMms2Provider.QuerySchedulerInterface(Mms2Adapter, &Buffer.Interface);
    DxgkpMms2ReleaseProviderCall();
    if (Buffer.Canary != DXGKP_MMS2_CANARY)
        return STATUS_REVISION_MISMATCH;
    if (!NT_SUCCESS(Status))
        return Status;
    if (Buffer.Interface.Size != DXGMMS2_SCHEDULER_INTERFACE_V1_SIZE || Buffer.Interface.Version != DXGMMS2_SCHEDULER_VERSION_1 ||
        Buffer.Interface.InterfaceFlags != 0 || Buffer.Interface.SchedulerHandle == NULL ||
        Buffer.Interface.MaximumEngines == 0 || Buffer.Interface.MaximumEngines > DXGMMS2_SCHEDULER_MAX_ENGINES ||
        Buffer.Interface.Start == NULL || Buffer.Interface.AdmitPacket == NULL || Buffer.Interface.ClaimNextPacket == NULL ||
        Buffer.Interface.PublishDispatch == NULL || Buffer.Interface.CompleteDispatch == NULL ||
        Buffer.Interface.NotifyCompletion == NULL || Buffer.Interface.DrainRetirements == NULL ||
        Buffer.Interface.CancelOwnerPackets == NULL || Buffer.Interface.AbortAllPackets == NULL ||
        Buffer.Interface.SetAdmission == NULL || Buffer.Interface.QueryEngineStatus == NULL ||
        Buffer.Interface.SetEngineState == NULL || Buffer.Interface.IsIdle == NULL ||
        Buffer.Interface.ReserveSlot == NULL || Buffer.Interface.ReleaseSlot == NULL ||
        Buffer.Interface.ResetDispatched == NULL || Buffer.Interface.GetOldestDispatched == NULL ||
        Buffer.Interface.PeekNextPacket == NULL)
        return STATUS_REVISION_MISMATCH;
    *SchedulerInterface = Buffer.Interface;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkpMms2Uninitialize(VOID)
{
    NTSTATUS Status;

    PAGED_CODE();
    if (InterlockedCompareExchange(&DxgkpMms2ClientState, 4, 2) != 2)
        return STATUS_INVALID_DEVICE_STATE;
    ExWaitForRundownProtectionRelease(&DxgkpMms2CallRundown);
    Status = DxgkMms2Unregister(DxgkpMms2Provider.RegistrationHandle);
    if (!NT_SUCCESS(Status))
    {
        ExReInitializeRundownProtection(&DxgkpMms2CallRundown);
        InterlockedExchange(&DxgkpMms2ClientState, 2);
        return Status;
    }
    RtlZeroMemory(&DxgkpMms2Provider, sizeof(DxgkpMms2Provider));
    InterlockedExchange(&DxgkpMms2ClientState, 0);
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkpMms2CreateAdapter(_In_ PDXGKRNL_ADAPTER Adapter, _In_ ULONG AdapterFlags, _Out_ DXGMMS2_ADAPTER_HANDLE *Mms2Adapter)
{
    DXGMMS2_CREATE_ADAPTER_INFO_V1 Info;
    NTSTATUS Status;

    PAGED_CODE();
    if (!DxgkpMms2AcquireProviderCall())
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Size = DXGMMS2_CREATE_ADAPTER_INFO_V1_SIZE;
    Info.Version = DXGMMS2_ABI_VERSION_1;
    Info.AdapterCookie = Adapter;
    Info.AdapterFlags = AdapterFlags;
    Status = DxgkpMms2Provider.CreateAdapter(DxgkpMms2Provider.RegistrationHandle, &Info, Mms2Adapter);
    DxgkpMms2ReleaseProviderCall();
    return Status;
}

NTSTATUS
DxgkpMms2StartAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _In_ ULONG MiniportDdiVersion, _In_ ULONG RequestedWddmVersion, _In_ ULONG NodeCount, _In_ ULONG SegmentCount, _In_ ULONG AdapterFlags, _In_ ULONG SchedulingCaps, _Out_ PULONGLONG EnabledSubsystems, _Out_ PULONG HighestCompleteWddmVersion, _Out_ PBOOLEAN ProviderStarted)
{
    DXGMMS2_START_ADAPTER_INFO_V1 Info;
    DXGKP_MMS2_START_RESULT_BUFFER ResultBuffer;
    NTSTATUS Status;

    PAGED_CODE();
    if (EnabledSubsystems == NULL || HighestCompleteWddmVersion == NULL || ProviderStarted == NULL)
        return STATUS_INVALID_PARAMETER;
    *EnabledSubsystems = 0;
    *HighestCompleteWddmVersion = 0;
    *ProviderStarted = FALSE;
    if (!DxgkpMms2AcquireProviderCall())
        return STATUS_DEVICE_NOT_READY;

    RtlZeroMemory(&Info, sizeof(Info));
    Info.Size = DXGMMS2_START_ADAPTER_INFO_V1_SIZE;
    Info.Version = DXGMMS2_ABI_VERSION_1;
    Info.MiniportDdiVersion = MiniportDdiVersion;
    Info.RequestedWddmVersion = RequestedWddmVersion;
    Info.NodeCount = NodeCount;
    Info.SegmentCount = SegmentCount;
    Info.AdapterFlags = AdapterFlags;
    Info.SchedulingCaps = SchedulingCaps;

    RtlZeroMemory(&ResultBuffer, sizeof(ResultBuffer));
    ResultBuffer.Result.Size = DXGMMS2_START_ADAPTER_RESULT_V1_SIZE;
    ResultBuffer.Result.Version = DXGMMS2_ABI_VERSION_1;
    ResultBuffer.Canary = DXGKP_MMS2_CANARY;
    Status = DxgkpMms2Provider.StartAdapter(Mms2Adapter, &Info, &ResultBuffer.Result);
    *ProviderStarted = NT_SUCCESS(Status);
    DxgkpMms2ReleaseProviderCall();
    if (ResultBuffer.Canary != DXGKP_MMS2_CANARY)
        return STATUS_REVISION_MISMATCH;
    if (!NT_SUCCESS(Status))
        return Status;
    if (ResultBuffer.Result.Size != DXGMMS2_START_ADAPTER_RESULT_V1_SIZE || ResultBuffer.Result.Version != DXGMMS2_ABI_VERSION_1 || (ResultBuffer.Result.EnabledSubsystems & ~DXGMMS2_SUBSYSTEM_VALID_MASK) != 0 || ResultBuffer.Result.HighestCompleteWddmVersion == 0 || ResultBuffer.Result.HighestCompleteWddmVersion > RequestedWddmVersion || ResultBuffer.Result.HighestCompleteWddmVersion > DXGMMS2_WDDM_VERSION_2_0 || ResultBuffer.Result.Reserved != 0)
        return STATUS_REVISION_MISMATCH;
    *EnabledSubsystems = ResultBuffer.Result.EnabledSubsystems;
    *HighestCompleteWddmVersion = ResultBuffer.Result.HighestCompleteWddmVersion;
    return STATUS_SUCCESS;
}

NTSTATUS
DxgkpMms2BeginStopAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _In_ DXGMMS2_STOP_REASON Reason)
{
    DXGMMS2_STOP_ADAPTER_INFO_V1 Info;
    NTSTATUS Status;

    PAGED_CODE();
    if (!DxgkpMms2AcquireProviderCall())
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Size = DXGMMS2_STOP_ADAPTER_INFO_V1_SIZE;
    Info.Version = DXGMMS2_ABI_VERSION_1;
    Info.Reason = Reason;
    Status = DxgkpMms2Provider.BeginStopAdapter(Mms2Adapter, &Info);
    DxgkpMms2ReleaseProviderCall();
    return Status;
}

NTSTATUS
DxgkpMms2CompleteStopAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _In_ DXGMMS2_STOP_REASON Reason)
{
    DXGMMS2_STOP_ADAPTER_INFO_V1 Info;
    NTSTATUS Status;

    PAGED_CODE();
    if (!DxgkpMms2AcquireProviderCall())
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&Info, sizeof(Info));
    Info.Size = DXGMMS2_STOP_ADAPTER_INFO_V1_SIZE;
    Info.Version = DXGMMS2_ABI_VERSION_1;
    Info.Reason = Reason;
    Info.Flags = DXGMMS2_COMPLETE_STOP_HARDWARE_RETIRED;
    Status = DxgkpMms2Provider.CompleteStopAdapter(Mms2Adapter, &Info);
    DxgkpMms2ReleaseProviderCall();
    return Status;
}

NTSTATUS
DxgkpMms2DestroyAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter)
{
    NTSTATUS Status;

    PAGED_CODE();
    if (!DxgkpMms2AcquireProviderCall())
        return STATUS_DEVICE_NOT_READY;
    Status = DxgkpMms2Provider.DestroyAdapter(Mms2Adapter);
    DxgkpMms2ReleaseProviderCall();
    return Status;
}

NTSTATUS DxgkpMms2QueryVidMmInterface(_In_ DXGMMS2_ADAPTER_HANDLE Mms2Adapter, _Out_ DXGMMS2_VIDMM_INTERFACE_V1 *VidMmInterface)
{
    typedef struct _DXGKP_MMS2_VIDMM_BUFFER { DXGMMS2_VIDMM_INTERFACE_V1 Interface; ULONGLONG Canary; } DXGKP_MMS2_VIDMM_BUFFER;
    DXGKP_MMS2_VIDMM_BUFFER Buffer;
    NTSTATUS Status;

    PAGED_CODE();
    if (Mms2Adapter == NULL || VidMmInterface == NULL)
        return STATUS_INVALID_PARAMETER;
    RtlZeroMemory(VidMmInterface, sizeof(*VidMmInterface));
    if (!DxgkpMms2AcquireProviderCall())
        return STATUS_DEVICE_NOT_READY;
    RtlZeroMemory(&Buffer, sizeof(Buffer));
    Buffer.Interface.Size = DXGMMS2_VIDMM_INTERFACE_V1_SIZE;
    Buffer.Interface.Version = DXGMMS2_VIDMM_VERSION_1;
    Buffer.Canary = DXGKP_MMS2_CANARY;
    Status = DxgkpMms2Provider.QueryVidMmInterface(Mms2Adapter, &Buffer.Interface);
    DxgkpMms2ReleaseProviderCall();
    if (Buffer.Canary != DXGKP_MMS2_CANARY)
        return STATUS_REVISION_MISMATCH;
    if (!NT_SUCCESS(Status))
        return Status;
    if (Buffer.Interface.Size != DXGMMS2_VIDMM_INTERFACE_V1_SIZE || Buffer.Interface.Version != DXGMMS2_VIDMM_VERSION_1 ||
        Buffer.Interface.VidMmHandle == NULL ||
        Buffer.Interface.Start == NULL || Buffer.Interface.SetSegment == NULL ||
        Buffer.Interface.ReservePlacement == NULL || Buffer.Interface.ReleasePlacement == NULL ||
        Buffer.Interface.SetPlacementState == NULL || Buffer.Interface.QuerySegmentStatus == NULL ||
        Buffer.Interface.FindEvictionCandidate == NULL || Buffer.Interface.ReleaseAllPlacements == NULL)
        return STATUS_REVISION_MISMATCH;
    *VidMmInterface = Buffer.Interface;
    return STATUS_SUCCESS;
}

/* EOF */
