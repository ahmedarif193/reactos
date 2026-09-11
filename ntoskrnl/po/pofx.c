/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Power framework registration and component transitions
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif193@gmail.com>
 */

#include <ntoskrnl.h>
#include <debug.h>

typedef struct _ROS_PO_FX_COMPONENT_STATE
{
    LONG ActiveReferences;
    ULONGLONG Latency;
    ULONGLONG Residency;
    ULONG CurrentIdleState;
    ULONG RequestedIdleState;
    BOOLEAN IdleConditionPending;
    BOOLEAN IdleStatePending;
    BOOLEAN Active;
} ROS_PO_FX_COMPONENT_STATE, *PROS_PO_FX_COMPONENT_STATE;

typedef struct _ROS_PO_FX_RELATION
{
    LIST_ENTRY ListEntry;
    ULONG Component;
    PDEVICE_OBJECT RelatedDevice;
    PVOID RelationContext;
} ROS_PO_FX_RELATION, *PROS_PO_FX_RELATION;

typedef struct _ROS_PO_FX_HANDLE
{
    ULONG Signature;
    PDEVICE_OBJECT Pdo;
    /*
     * Normalized description.  PoFxRegisterDevice accepts a V1, V2 or V3
     * PO_FX_DEVICE, because a single exported entry point serves drivers
     * compiled against different PO_FX_VERSION defaults, and stores the
     * result in the widest layout so the rest of this file has one shape to
     * work with.  Windows 11 dxgkrnl registers a V3 device.
     */
    PPO_FX_DEVICE_V3 Device;
    KSPIN_LOCK RelationLock;
    LIST_ENTRY RelationList;
    ULONGLONG IdleTimeout;
    ULONG ComponentCount;
    KSPIN_LOCK StateLock;
    EX_RUNDOWN_REF Rundown;
    KEVENT UnregisterReady;
    BOOLEAN Started;
    BOOLEAN Unregistering;
    BOOLEAN Dispatching;
    BOOLEAN DevicePoweredOn;
    BOOLEAN DevicePowerRequiredPending;
    BOOLEAN DevicePowerNotRequiredPending;
    ROS_PO_FX_COMPONENT_STATE ComponentState[ANYSIZE_ARRAY];
} ROS_PO_FX_HANDLE, *PROS_PO_FX_HANDLE;

#define ROS_PO_FX_SIGNATURE 'xoFP'
#define ROS_PO_FX_RELATION_TAG 'roFP'
#define ROS_PO_FX_HANDLE(Handle) ((PROS_PO_FX_HANDLE)(Handle))

#ifndef PO_FX_UNKNOWN_TIME
#define PO_FX_UNKNOWN_TIME MAXULONGLONG
#endif

static
ULONG
PopFxSelectIdleState(
    _In_ PROS_PO_FX_HANDLE FxHandle,
    _In_ ULONG Component)
{
    PPO_FX_COMPONENT_V2 Description = &FxHandle->Device->Components[Component];
    PROS_PO_FX_COMPONENT_STATE State = &FxHandle->ComponentState[Component];
    ULONG Index;

    for (Index = Description->IdleStateCount; Index != 0; Index--)
    {
        PPO_FX_COMPONENT_IDLE_STATE IdleState = &Description->IdleStates[Index - 1];

        if ((State->Latency != PO_FX_UNKNOWN_TIME) && (IdleState->TransitionLatency > State->Latency))
            continue;
        if ((State->Residency != PO_FX_UNKNOWN_TIME) && (IdleState->ResidencyRequirement > State->Residency))
            continue;
        return Index - 1;
    }

    return 0;
}

typedef enum _ROS_PO_FX_ACTION
{
    PopFxNoAction,
    PopFxIdleCondition,
    PopFxIdleState,
    PopFxActiveCondition,
    PopFxPowerRequired,
    PopFxPowerNotRequired
} ROS_PO_FX_ACTION;

static
PROS_PO_FX_HANDLE
PopFxReferenceHandle(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);

    if (FxHandle == NULL || FxHandle->Signature != ROS_PO_FX_SIGNATURE ||
        !ExAcquireRundownProtection(&FxHandle->Rundown))
    {
        return NULL;
    }
    return FxHandle;
}

/* StateLock protects the requested condition and all outstanding callbacks.
 * A new transition cannot replace one that the driver has not completed. */
static
ROS_PO_FX_ACTION
PopFxNextAction(
    _In_ PROS_PO_FX_HANDLE FxHandle,
    _Out_ PULONG Component,
    _Out_ PULONG IdleState)
{
    ULONG Index;
    BOOLEAN AllIdle = TRUE;
    BOOLEAN AllActive = TRUE;

    for (Index = 0; Index < FxHandle->ComponentCount; Index++)
    {
        PROS_PO_FX_COMPONENT_STATE State = &FxHandle->ComponentState[Index];
        BOOLEAN WantActive = State->ActiveReferences != 0 || FxHandle->Unregistering;

        *Component = Index;
        if (State->IdleConditionPending || State->IdleStatePending)
        {
            AllIdle = AllActive = FALSE;
            continue;
        }

        if (WantActive)
        {
            AllIdle = FALSE;
            if (FxHandle->DevicePowerNotRequiredPending || FxHandle->DevicePowerRequiredPending)
            {
                AllActive = FALSE;
                continue;
            }
            if (!FxHandle->DevicePoweredOn)
            {
                FxHandle->DevicePowerRequiredPending = TRUE;
                return PopFxPowerRequired;
            }
            if (State->CurrentIdleState != 0)
            {
                State->IdleStatePending = TRUE;
                State->RequestedIdleState = *IdleState = 0;
                return PopFxIdleState;
            }
            if (!State->Active)
            {
                State->Active = TRUE;
                return PopFxActiveCondition;
            }
        }
        else if (FxHandle->Started)
        {
            AllActive = FALSE;
            if (State->Active)
            {
                State->IdleConditionPending = TRUE;
                return PopFxIdleCondition;
            }
            *IdleState = PopFxSelectIdleState(FxHandle, Index);
            if (State->CurrentIdleState != *IdleState)
            {
                AllIdle = FALSE;
                if (FxHandle->DevicePowerNotRequiredPending || FxHandle->DevicePowerRequiredPending)
                    continue;
                if (!FxHandle->DevicePoweredOn)
                {
                    FxHandle->DevicePowerRequiredPending = TRUE;
                    return PopFxPowerRequired;
                }
                State->IdleStatePending = TRUE;
                State->RequestedIdleState = *IdleState;
                return PopFxIdleState;
            }
        }
        else
        {
            AllIdle = FALSE;
        }
    }

    if (FxHandle->Started && AllIdle && FxHandle->DevicePoweredOn &&
        !FxHandle->DevicePowerNotRequiredPending && !FxHandle->DevicePowerRequiredPending)
    {
        FxHandle->DevicePowerNotRequiredPending = TRUE;
        return PopFxPowerNotRequired;
    }

    if (FxHandle->Unregistering && AllActive && FxHandle->DevicePoweredOn &&
        !FxHandle->DevicePowerRequiredPending && !FxHandle->DevicePowerNotRequiredPending)
    {
        KeSetEvent(&FxHandle->UnregisterReady, IO_NO_INCREMENT, FALSE);
    }
    return PopFxNoAction;
}

/* The caller holds rundown protection. Callbacks run outside StateLock and
 * may complete inline. One dispatcher drains these completions without
 * recursively issuing a second callback on the same component. */
static
VOID
PopFxDispatchTransitions(
    _In_ PROS_PO_FX_HANDLE FxHandle)
{
    ROS_PO_FX_ACTION Action;
    ULONG Component = 0, IdleState = 0;
    KIRQL OldIrql;
    PPO_FX_DEVICE_V3 Device = FxHandle->Device;

    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    if (FxHandle->Dispatching)
    {
        KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
        return;
    }
    FxHandle->Dispatching = TRUE;
    for (;;)
    {
        Action = PopFxNextAction(FxHandle, &Component, &IdleState);
        if (Action == PopFxNoAction)
        {
            FxHandle->Dispatching = FALSE;
            KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
            return;
        }
        KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
        switch (Action)
        {
            case PopFxIdleCondition:
                if (Device->ComponentIdleConditionCallback != NULL)
                    Device->ComponentIdleConditionCallback(Device->DeviceContext, Component);
                else
                    PoFxCompleteIdleCondition((POHANDLE)FxHandle, Component);
                break;
            case PopFxIdleState:
                if (Device->ComponentIdleStateCallback != NULL)
                    Device->ComponentIdleStateCallback(Device->DeviceContext, Component, IdleState);
                else
                    PoFxCompleteIdleState((POHANDLE)FxHandle, Component);
                break;
            case PopFxActiveCondition:
                if (Device->ComponentActiveConditionCallback != NULL)
                    Device->ComponentActiveConditionCallback(Device->DeviceContext, Component);
                break;
            case PopFxPowerRequired:
                if (Device->DevicePowerRequiredCallback != NULL)
                    Device->DevicePowerRequiredCallback(Device->DeviceContext);
                else
                    PoFxReportDevicePoweredOn((POHANDLE)FxHandle);
                break;
            case PopFxPowerNotRequired:
                if (Device->DevicePowerNotRequiredCallback != NULL)
                    Device->DevicePowerNotRequiredCallback(Device->DeviceContext);
                else
                    PoFxCompleteDevicePowerNotRequired((POHANDLE)FxHandle);
                break;
            default:
                ASSERT(FALSE);
                break;
        }
        KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    }
}

/*
 * Version-independent view of a caller's registration table.
 *
 * V1 places the callbacks directly after Version and has no device flags; V2
 * and V3 both carry Flags and describe components with PO_FX_COMPONENT_V2; V3
 * adds the directed-power callbacks and the directed-FX timeout.  One exported
 * PoFxRegisterDevice serves drivers built against any of the three, because
 * PO_FX_VERSION is a compile-time choice of the *caller*, not of the kernel.
 */
typedef struct _ROS_PO_FX_SOURCE
{
    ULONG Version;
    ULONGLONG Flags;
    PPO_FX_COMPONENT_ACTIVE_CONDITION_CALLBACK ComponentActiveConditionCallback;
    PPO_FX_COMPONENT_IDLE_CONDITION_CALLBACK ComponentIdleConditionCallback;
    PPO_FX_COMPONENT_IDLE_STATE_CALLBACK ComponentIdleStateCallback;
    PPO_FX_DEVICE_POWER_REQUIRED_CALLBACK DevicePowerRequiredCallback;
    PPO_FX_DEVICE_POWER_NOT_REQUIRED_CALLBACK DevicePowerNotRequiredCallback;
    PPO_FX_POWER_CONTROL_CALLBACK PowerControlCallback;
    PPO_FX_DIRECTED_POWER_UP_CALLBACK DirectedPowerUpCallback;
    PPO_FX_DIRECTED_POWER_DOWN_CALLBACK DirectedPowerDownCallback;
    ULONG DirectedFxTimeoutInSeconds;
    PVOID DeviceContext;
    ULONG ComponentCount;
} ROS_PO_FX_SOURCE, *PROS_PO_FX_SOURCE;

static
NTSTATUS
PopFxCaptureDevice(
    _In_ PVOID Device,
    _Out_ PROS_PO_FX_SOURCE Source)
{
    RtlZeroMemory(Source, sizeof(*Source));

    /* Version is the first ULONG of every PO_FX_DEVICE revision. */
    Source->Version = *(const ULONG *)Device;
    switch (Source->Version)
    {
        case PO_FX_VERSION_V1:
        {
            PPO_FX_DEVICE_V1 V1 = (PPO_FX_DEVICE_V1)Device;

            Source->ComponentActiveConditionCallback = V1->ComponentActiveConditionCallback;
            Source->ComponentIdleConditionCallback = V1->ComponentIdleConditionCallback;
            Source->ComponentIdleStateCallback = V1->ComponentIdleStateCallback;
            Source->DevicePowerRequiredCallback = V1->DevicePowerRequiredCallback;
            Source->DevicePowerNotRequiredCallback = V1->DevicePowerNotRequiredCallback;
            Source->PowerControlCallback = V1->PowerControlCallback;
            Source->DeviceContext = V1->DeviceContext;
            Source->ComponentCount = V1->ComponentCount;
            break;
        }
        case PO_FX_VERSION_V2:
        {
            PPO_FX_DEVICE_V2 V2 = (PPO_FX_DEVICE_V2)Device;

            Source->Flags = V2->Flags;
            Source->ComponentActiveConditionCallback = V2->ComponentActiveConditionCallback;
            Source->ComponentIdleConditionCallback = V2->ComponentIdleConditionCallback;
            Source->ComponentIdleStateCallback = V2->ComponentIdleStateCallback;
            Source->DevicePowerRequiredCallback = V2->DevicePowerRequiredCallback;
            Source->DevicePowerNotRequiredCallback = V2->DevicePowerNotRequiredCallback;
            Source->PowerControlCallback = V2->PowerControlCallback;
            Source->DeviceContext = V2->DeviceContext;
            Source->ComponentCount = V2->ComponentCount;
            break;
        }
        case PO_FX_VERSION_V3:
        {
            PPO_FX_DEVICE_V3 V3 = (PPO_FX_DEVICE_V3)Device;

            Source->Flags = V3->Flags;
            Source->ComponentActiveConditionCallback = V3->ComponentActiveConditionCallback;
            Source->ComponentIdleConditionCallback = V3->ComponentIdleConditionCallback;
            Source->ComponentIdleStateCallback = V3->ComponentIdleStateCallback;
            Source->DevicePowerRequiredCallback = V3->DevicePowerRequiredCallback;
            Source->DevicePowerNotRequiredCallback = V3->DevicePowerNotRequiredCallback;
            Source->PowerControlCallback = V3->PowerControlCallback;
            Source->DirectedPowerUpCallback = V3->DirectedPowerUpCallback;
            Source->DirectedPowerDownCallback = V3->DirectedPowerDownCallback;
            Source->DirectedFxTimeoutInSeconds = V3->DirectedFxTimeoutInSeconds;
            Source->DeviceContext = V3->DeviceContext;
            Source->ComponentCount = V3->ComponentCount;
            break;
        }
        default:
            return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

static
VOID
PopFxCaptureComponent(
    _In_ ULONG Version,
    _In_ PVOID Device,
    _In_ ULONG Index,
    _Out_ PPO_FX_COMPONENT_V2 Component)
{
    RtlZeroMemory(Component, sizeof(*Component));

    if (Version == PO_FX_VERSION_V1)
    {
        PPO_FX_COMPONENT_V1 Source = &((PPO_FX_DEVICE_V1)Device)->Components[Index];

        /* V1 has no per-component flags and no provider list. */
        Component->Id = Source->Id;
        Component->DeepestWakeableIdleState = Source->DeepestWakeableIdleState;
        Component->IdleStateCount = Source->IdleStateCount;
        Component->IdleStates = Source->IdleStates;
        return;
    }

    if (Version == PO_FX_VERSION_V2)
        *Component = ((PPO_FX_DEVICE_V2)Device)->Components[Index];
    else
        *Component = ((PPO_FX_DEVICE_V3)Device)->Components[Index];
}

NTSTATUS
NTAPI
PoFxRegisterDevice(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ PPO_FX_DEVICE Device,
    _Out_ POHANDLE *Handle)
{
    PROS_PO_FX_HANDLE NewHandle;
    PPO_FX_DEVICE_V3 DeviceCopy;
    ROS_PO_FX_SOURCE Source;
    NTSTATUS Status;
    SIZE_T AllocationSize;
    SIZE_T DeviceSize;
    SIZE_T IdleStatesSize;
    SIZE_T ProvidersSize;
    PUCHAR Cursor;
    ULONG Component;

    if (Handle == NULL)
        return STATUS_INVALID_PARAMETER;

    *Handle = NULL;
    if ((Pdo == NULL) || (Device == NULL))
        return STATUS_INVALID_PARAMETER;

    Status = PopFxCaptureDevice(Device, &Source);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Source.ComponentCount == 0)
        return STATUS_INVALID_PARAMETER;

    DeviceSize = FIELD_OFFSET(PO_FX_DEVICE_V3, Components);
    if ((SIZE_T)Source.ComponentCount > (MAXULONG_PTR - DeviceSize) / sizeof(PO_FX_COMPONENT_V2))
        return STATUS_INTEGER_OVERFLOW;
    DeviceSize += Source.ComponentCount * sizeof(PO_FX_COMPONENT_V2);

    IdleStatesSize = 0;
    ProvidersSize = 0;
    for (Component = 0; Component != Source.ComponentCount; Component++)
    {
        PO_FX_COMPONENT_V2 Description;

        PopFxCaptureComponent(Source.Version, Device, Component, &Description);
        if ((Description.IdleStateCount == 0) || (Description.IdleStates == NULL) || (Description.DeepestWakeableIdleState >= Description.IdleStateCount))
            return STATUS_INVALID_PARAMETER;
        if ((Description.IdleStateCount > 1) && ((Source.ComponentIdleStateCallback == NULL) || (Source.ComponentActiveConditionCallback == NULL) || (Source.ComponentIdleConditionCallback == NULL)))
            return STATUS_INVALID_PARAMETER;
        if ((Description.ProviderCount != 0) && (Description.Providers == NULL))
            return STATUS_INVALID_PARAMETER;
        if ((SIZE_T)Description.IdleStateCount > (MAXULONG_PTR - IdleStatesSize) / sizeof(PO_FX_COMPONENT_IDLE_STATE))
            return STATUS_INTEGER_OVERFLOW;
        IdleStatesSize += Description.IdleStateCount * sizeof(PO_FX_COMPONENT_IDLE_STATE);
        if ((SIZE_T)Description.ProviderCount > (MAXULONG_PTR - ProvidersSize) / sizeof(ULONG))
            return STATUS_INTEGER_OVERFLOW;
        ProvidersSize += Description.ProviderCount * sizeof(ULONG);
    }
    if ((DeviceSize > MAXULONG_PTR - IdleStatesSize) ||
        (DeviceSize + IdleStatesSize > MAXULONG_PTR - ProvidersSize))
    {
        return STATUS_INTEGER_OVERFLOW;
    }

    if ((SIZE_T)Source.ComponentCount > (MAXULONG_PTR - FIELD_OFFSET(ROS_PO_FX_HANDLE, ComponentState)) / sizeof(ROS_PO_FX_COMPONENT_STATE))
        return STATUS_INTEGER_OVERFLOW;
    AllocationSize = FIELD_OFFSET(ROS_PO_FX_HANDLE, ComponentState) + Source.ComponentCount * sizeof(ROS_PO_FX_COMPONENT_STATE);
    NewHandle = ExAllocatePoolZero(NonPagedPool, AllocationSize, ROS_PO_FX_SIGNATURE);
    if (NewHandle == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    DeviceCopy = ExAllocatePoolZero(NonPagedPool, DeviceSize + IdleStatesSize + ProvidersSize, ROS_PO_FX_SIGNATURE);
    if (DeviceCopy == NULL)
    {
        ExFreePoolWithTag(NewHandle, ROS_PO_FX_SIGNATURE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Publish the normalized description.  Idle-state and provider arrays are
     * copied into the same allocation so nothing points back at caller memory
     * that may be freed once registration returns.
     */
    DeviceCopy->Version = PO_FX_VERSION_V3;
    DeviceCopy->Flags = Source.Flags;
    DeviceCopy->ComponentActiveConditionCallback = Source.ComponentActiveConditionCallback;
    DeviceCopy->ComponentIdleConditionCallback = Source.ComponentIdleConditionCallback;
    DeviceCopy->ComponentIdleStateCallback = Source.ComponentIdleStateCallback;
    DeviceCopy->DevicePowerRequiredCallback = Source.DevicePowerRequiredCallback;
    DeviceCopy->DevicePowerNotRequiredCallback = Source.DevicePowerNotRequiredCallback;
    DeviceCopy->PowerControlCallback = Source.PowerControlCallback;
    DeviceCopy->DirectedPowerUpCallback = Source.DirectedPowerUpCallback;
    DeviceCopy->DirectedPowerDownCallback = Source.DirectedPowerDownCallback;
    DeviceCopy->DirectedFxTimeoutInSeconds = Source.DirectedFxTimeoutInSeconds;
    DeviceCopy->DeviceContext = Source.DeviceContext;
    DeviceCopy->ComponentCount = Source.ComponentCount;

    Cursor = (PUCHAR)DeviceCopy + DeviceSize;
    for (Component = 0; Component != Source.ComponentCount; Component++)
    {
        PPO_FX_COMPONENT_V2 Description = &DeviceCopy->Components[Component];
        SIZE_T Size;

        PopFxCaptureComponent(Source.Version, Device, Component, Description);

        Size = Description->IdleStateCount * sizeof(PO_FX_COMPONENT_IDLE_STATE);
        RtlCopyMemory(Cursor, Description->IdleStates, Size);
        Description->IdleStates = (PPO_FX_COMPONENT_IDLE_STATE)Cursor;
        Cursor += Size;

        Size = Description->ProviderCount * sizeof(ULONG);
        if (Size != 0)
        {
            RtlCopyMemory(Cursor, Description->Providers, Size);
            Description->Providers = (PULONG)Cursor;
            Cursor += Size;
        }
        else
        {
            Description->Providers = NULL;
        }

        NewHandle->ComponentState[Component].Latency = PO_FX_UNKNOWN_TIME;
        NewHandle->ComponentState[Component].Residency = PO_FX_UNKNOWN_TIME;
        NewHandle->ComponentState[Component].Active = TRUE;
    }

    NewHandle->Signature = ROS_PO_FX_SIGNATURE;
    NewHandle->Pdo = Pdo;
    NewHandle->Device = DeviceCopy;
    NewHandle->ComponentCount = Source.ComponentCount;
    NewHandle->DevicePoweredOn = TRUE;
    KeInitializeSpinLock(&NewHandle->StateLock);
    ExInitializeRundownProtection(&NewHandle->Rundown);
    KeInitializeEvent(&NewHandle->UnregisterReady, NotificationEvent, FALSE);
    KeInitializeSpinLock(&NewHandle->RelationLock);
    InitializeListHead(&NewHandle->RelationList);
    ObReferenceObject(Pdo);
    *Handle = (POHANDLE)NewHandle;
    return STATUS_SUCCESS;
}

VOID
NTAPI
PoFxUnregisterDevice(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    PROS_PO_FX_RELATION Relation;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (FxHandle != NULL)
    {
        ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
        KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
        FxHandle->Unregistering = TRUE;
        KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
        PopFxDispatchTransitions(FxHandle);
        ExReleaseRundownProtection(&FxHandle->Rundown);

        /* Complete outstanding driver handshakes and restore D0/F0 before
         * preventing callbacks from taking new references to the handle. */
        KeWaitForSingleObject(&FxHandle->UnregisterReady, Executive, KernelMode, FALSE, NULL);
        ExWaitForRundownProtectionRelease(&FxHandle->Rundown);
        FxHandle->Signature = 0;
        for (;;)
        {
            KeAcquireSpinLock(&FxHandle->RelationLock, &OldIrql);
            if (IsListEmpty(&FxHandle->RelationList))
            {
                KeReleaseSpinLock(&FxHandle->RelationLock, OldIrql);
                break;
            }
            Entry = RemoveHeadList(&FxHandle->RelationList);
            KeReleaseSpinLock(&FxHandle->RelationLock, OldIrql);
            Relation = CONTAINING_RECORD(Entry, ROS_PO_FX_RELATION, ListEntry);
            ObDereferenceObject(Relation->RelatedDevice);
            ExFreePoolWithTag(Relation, ROS_PO_FX_RELATION_TAG);
        }
        ObDereferenceObject(FxHandle->Pdo);
        ExFreePoolWithTag(FxHandle->Device, ROS_PO_FX_SIGNATURE);
        ExFreePoolWithTag(FxHandle, ROS_PO_FX_SIGNATURE);
    }
}

VOID
NTAPI
PoFxStartDevicePowerManagement(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    FxHandle->Started = TRUE;
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    PopFxDispatchTransitions(FxHandle);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

VOID
NTAPI
PoFxActivateComponent(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ ULONG Flags)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;
    UNREFERENCED_PARAMETER(Flags);

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    if (Component < FxHandle->ComponentCount && !FxHandle->Unregistering)
        ++FxHandle->ComponentState[Component].ActiveReferences;
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    PopFxDispatchTransitions(FxHandle);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

VOID
NTAPI
PoFxIdleComponent(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ ULONG Flags)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;
    UNREFERENCED_PARAMETER(Flags);

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    if (Component < FxHandle->ComponentCount && !FxHandle->Unregistering &&
        FxHandle->ComponentState[Component].ActiveReferences != 0)
    {
        --FxHandle->ComponentState[Component].ActiveReferences;
    }
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    PopFxDispatchTransitions(FxHandle);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

VOID
NTAPI
PoFxCompleteIdleCondition(
    _In_ POHANDLE Handle,
    _In_ ULONG Component)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    if (Component < FxHandle->ComponentCount && FxHandle->ComponentState[Component].IdleConditionPending)
    {
        FxHandle->ComponentState[Component].IdleConditionPending = FALSE;
        FxHandle->ComponentState[Component].Active = FALSE;
    }
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    PopFxDispatchTransitions(FxHandle);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

VOID
NTAPI
PoFxCompleteIdleState(
    _In_ POHANDLE Handle,
    _In_ ULONG Component)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    if (Component < FxHandle->ComponentCount && FxHandle->ComponentState[Component].IdleStatePending)
    {
        FxHandle->ComponentState[Component].IdleStatePending = FALSE;
        FxHandle->ComponentState[Component].CurrentIdleState = FxHandle->ComponentState[Component].RequestedIdleState;
    }
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    PopFxDispatchTransitions(FxHandle);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

VOID
NTAPI
PoFxCompleteDevicePowerNotRequired(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    if (FxHandle->DevicePowerNotRequiredPending)
    {
        FxHandle->DevicePowerNotRequiredPending = FALSE;
        FxHandle->DevicePoweredOn = FALSE;
    }
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    PopFxDispatchTransitions(FxHandle);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

VOID
NTAPI
PoFxSetDeviceIdleTimeout(
    _In_ POHANDLE Handle,
    _In_ ULONGLONG IdleTimeout)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    if (!FxHandle->Unregistering)
        FxHandle->IdleTimeout = IdleTimeout;
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

VOID
NTAPI
PoFxReportDevicePoweredOn(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    if (FxHandle->DevicePowerRequiredPending)
    {
        FxHandle->DevicePowerRequiredPending = FALSE;
        FxHandle->DevicePoweredOn = TRUE;
    }
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    PopFxDispatchTransitions(FxHandle);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

VOID
NTAPI
PoFxSetComponentLatency(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ ULONGLONG Latency)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    if (Component < FxHandle->ComponentCount && !FxHandle->Unregistering)
        FxHandle->ComponentState[Component].Latency = Latency;
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

VOID
NTAPI
PoFxSetComponentResidency(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ ULONGLONG Residency)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    if (Component < FxHandle->ComponentCount && !FxHandle->Unregistering)
        FxHandle->ComponentState[Component].Residency = Residency;
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

NTSTATUS
NTAPI
PoFxPowerControl(
    _In_ POHANDLE Handle,
    _In_ LPCGUID PowerControlCode,
    _In_opt_ PVOID InBuffer,
    _In_ SIZE_T InBufferSize,
    _Out_opt_ PVOID OutBuffer,
    _In_ SIZE_T OutBufferSize,
    _Out_opt_ PSIZE_T BytesReturned)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);
    UNREFERENCED_PARAMETER(InBuffer);
    UNREFERENCED_PARAMETER(InBufferSize);
    UNREFERENCED_PARAMETER(OutBuffer);
    UNREFERENCED_PARAMETER(OutBufferSize);
    UNREFERENCED_PARAMETER(BytesReturned);

    if ((FxHandle == NULL) || (FxHandle->Signature != ROS_PO_FX_SIGNATURE) ||
        (PowerControlCode == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Outbound requests belong to the platform PEP. No PEP is registered;
       PowerControlCallback handles requests in the opposite direction. */
    return STATUS_NOT_SUPPORTED;
}

VOID
NTAPI
PoFxCompleteDirectedPowerDown(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = PopFxReferenceHandle(Handle);
    KIRQL OldIrql;

    if (FxHandle == NULL)
        return;
    KeAcquireSpinLock(&FxHandle->StateLock, &OldIrql);
    FxHandle->DevicePoweredOn = FALSE;
    KeReleaseSpinLock(&FxHandle->StateLock, OldIrql);
    ExReleaseRundownProtection(&FxHandle->Rundown);
}

NTSTATUS
NTAPI
PoFxAddComponentRelation(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ PDEVICE_OBJECT RelatedDevice,
    _In_opt_ PVOID RelationContext)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);
    PROS_PO_FX_RELATION Relation;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if ((FxHandle == NULL) || (FxHandle->Signature != ROS_PO_FX_SIGNATURE) ||
        (Component >= FxHandle->ComponentCount) || (RelatedDevice == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Relation = ExAllocatePoolZero(NonPagedPool, sizeof(*Relation), ROS_PO_FX_RELATION_TAG);
    if (Relation == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    Relation->Component = Component;
    Relation->RelatedDevice = RelatedDevice;
    Relation->RelationContext = RelationContext;
    ObReferenceObject(RelatedDevice);

    KeAcquireSpinLock(&FxHandle->RelationLock, &OldIrql);
    for (Entry = FxHandle->RelationList.Flink; Entry != &FxHandle->RelationList; Entry = Entry->Flink)
    {
        PROS_PO_FX_RELATION Existing = CONTAINING_RECORD(Entry, ROS_PO_FX_RELATION, ListEntry);

        if ((Existing->Component == Component) && (Existing->RelatedDevice == RelatedDevice) && (Existing->RelationContext == RelationContext))
        {
            KeReleaseSpinLock(&FxHandle->RelationLock, OldIrql);
            ObDereferenceObject(RelatedDevice);
            ExFreePoolWithTag(Relation, ROS_PO_FX_RELATION_TAG);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    InsertTailList(&FxHandle->RelationList, &Relation->ListEntry);
    KeReleaseSpinLock(&FxHandle->RelationLock, OldIrql);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
PoFxRemoveComponentRelation(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ PDEVICE_OBJECT RelatedDevice,
    _In_opt_ PVOID RelationContext)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);
    PROS_PO_FX_RELATION Relation;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if ((FxHandle == NULL) || (FxHandle->Signature != ROS_PO_FX_SIGNATURE) || (Component >= FxHandle->ComponentCount) || (RelatedDevice == NULL))
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&FxHandle->RelationLock, &OldIrql);
    for (Entry = FxHandle->RelationList.Flink; Entry != &FxHandle->RelationList; Entry = Entry->Flink)
    {
        Relation = CONTAINING_RECORD(Entry, ROS_PO_FX_RELATION, ListEntry);
        if ((Relation->Component == Component) && (Relation->RelatedDevice == RelatedDevice) && (Relation->RelationContext == RelationContext))
        {
            RemoveEntryList(Entry);
            KeReleaseSpinLock(&FxHandle->RelationLock, OldIrql);
            ObDereferenceObject(Relation->RelatedDevice);
            ExFreePoolWithTag(Relation, ROS_PO_FX_RELATION_TAG);
            return STATUS_SUCCESS;
        }
    }
    KeReleaseSpinLock(&FxHandle->RelationLock, OldIrql);
    return STATUS_NOT_FOUND;
}
