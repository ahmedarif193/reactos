
#include <ntdef.h>
#include <ntifs.h>
#include <debug.h>

typedef struct _ROS_PO_FX_COMPONENT_STATE
{
    volatile LONG ActiveReferences;
    ULONGLONG Latency;
    ULONGLONG Residency;
    ULONG CurrentIdleState;
    volatile LONG IdleConditionPending;
    volatile LONG IdleStatePending;
    /*
     * A pending F-state transition is either an activation (the component is
     * being returned to F0 because a driver took the first active reference)
     * or an idle transition.  Its completion path differs, so the direction
     * has to be remembered across the driver's PoFxCompleteIdleState.
     */
    volatile LONG ActiveTransitionPending;
    volatile LONG Active;
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
    volatile LONG Started;
    volatile LONG DevicePoweredOn;
    volatile LONG DevicePowerNotRequiredPending;
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

static
BOOLEAN
PopFxAllComponentsIdle(
    _In_ PROS_PO_FX_HANDLE FxHandle)
{
    ULONG Index;

    for (Index = 0; Index != FxHandle->ComponentCount; Index++)
    {
        if ((FxHandle->ComponentState[Index].ActiveReferences != 0) || (FxHandle->ComponentState[Index].Active != 0))
            return FALSE;
    }

    return TRUE;
}

static
VOID
PopFxRequestDevicePowerNotRequired(
    _In_ PROS_PO_FX_HANDLE FxHandle)
{
    if (!FxHandle->Started || !FxHandle->DevicePoweredOn || !PopFxAllComponentsIdle(FxHandle))
        return;

    if (InterlockedCompareExchange(&FxHandle->DevicePowerNotRequiredPending, TRUE, FALSE) != FALSE)
        return;

    if (FxHandle->Device->DevicePowerNotRequiredCallback != NULL)
        FxHandle->Device->DevicePowerNotRequiredCallback(FxHandle->Device->DeviceContext);
    else
        PoFxCompleteDevicePowerNotRequired((POHANDLE)FxHandle);
}

static
VOID
PopFxRequestIdleState(
    _In_ PROS_PO_FX_HANDLE FxHandle,
    _In_ ULONG Component)
{
    PROS_PO_FX_COMPONENT_STATE State = &FxHandle->ComponentState[Component];

    if ((State->ActiveReferences != 0) || (InterlockedCompareExchange(&State->IdleStatePending, TRUE, FALSE) != FALSE))
        return;

    State->CurrentIdleState = PopFxSelectIdleState(FxHandle, Component);
    if (FxHandle->Device->ComponentIdleStateCallback != NULL)
        FxHandle->Device->ComponentIdleStateCallback(FxHandle->Device->DeviceContext, Component, State->CurrentIdleState);
    else
        PoFxCompleteIdleState((POHANDLE)FxHandle, Component);
}

/*
 * A component that has just taken its first active reference must be returned
 * to F0 before its active condition is signalled.  PoFx owns that transition:
 * it asks the driver for idle state 0 and only reports the active condition
 * once the driver completes it.  Without this step a component registered
 * with more than one F-state stays in whatever idle state it last entered and
 * is never powered back up.
 */
static
VOID
PopFxRequestActiveState(
    _In_ PROS_PO_FX_HANDLE FxHandle,
    _In_ ULONG Component)
{
    PROS_PO_FX_COMPONENT_STATE State = &FxHandle->ComponentState[Component];

    /*
     * Claim the activation direction first.  If an idle transition is still
     * outstanding, its completion observes this flag and reports the active
     * condition instead of parking the component.
     */
    InterlockedExchange(&State->ActiveTransitionPending, TRUE);
    if (InterlockedCompareExchange(&State->IdleStatePending, TRUE, FALSE) != FALSE)
        return;

    State->CurrentIdleState = 0;
    if (FxHandle->Device->ComponentIdleStateCallback != NULL)
        FxHandle->Device->ComponentIdleStateCallback(FxHandle->Device->DeviceContext, Component, 0);
    else
        PoFxCompleteIdleState((POHANDLE)FxHandle, Component);
}

static
VOID
PopFxRequestIdleCondition(
    _In_ PROS_PO_FX_HANDLE FxHandle,
    _In_ ULONG Component)
{
    PROS_PO_FX_COMPONENT_STATE State = &FxHandle->ComponentState[Component];

    if ((State->ActiveReferences != 0) || (InterlockedCompareExchange(&State->IdleConditionPending, TRUE, FALSE) != FALSE))
        return;

    if (FxHandle->Device->ComponentIdleConditionCallback != NULL)
        FxHandle->Device->ComponentIdleConditionCallback(FxHandle->Device->DeviceContext, Component);
    else
        PoFxCompleteIdleCondition((POHANDLE)FxHandle, Component);
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

NTKRNLVISTAAPI
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

    if ((Pdo == NULL) || (Device == NULL) || (Handle == NULL))
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
    KeInitializeSpinLock(&NewHandle->RelationLock);
    InitializeListHead(&NewHandle->RelationList);
    ObReferenceObject(Pdo);
    *Handle = (POHANDLE)NewHandle;
    return STATUS_SUCCESS;
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxUnregisterDevice(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);
    PROS_PO_FX_RELATION Relation;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE))
    {
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

NTKRNLVISTAAPI
VOID
NTAPI
PoFxStartDevicePowerManagement(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);
    ULONG Component;

    if ((FxHandle == NULL) || (FxHandle->Signature != ROS_PO_FX_SIGNATURE) || (InterlockedCompareExchange(&FxHandle->Started, TRUE, FALSE) != FALSE))
        return;

    for (Component = 0; Component != FxHandle->ComponentCount; Component++)
        PopFxRequestIdleCondition(FxHandle, Component);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxActivateComponent(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ ULONG Flags)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);
    LONG References;
    UNREFERENCED_PARAMETER(Flags);

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE) &&
        (Component < FxHandle->ComponentCount))
    {
        References = InterlockedIncrement(&FxHandle->ComponentState[Component].ActiveReferences);
        if (References == 1)
        {
            InterlockedExchange(&FxHandle->ComponentState[Component].IdleConditionPending, FALSE);
            InterlockedExchange(&FxHandle->DevicePowerNotRequiredPending, FALSE);
            if (!FxHandle->DevicePoweredOn && (FxHandle->Device->DevicePowerRequiredCallback != NULL))
                FxHandle->Device->DevicePowerRequiredCallback(FxHandle->Device->DeviceContext);
            InterlockedExchange(&FxHandle->DevicePoweredOn, TRUE);
            PopFxRequestActiveState(FxHandle, Component);
        }
    }
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxCompleteDevicePowerNotRequired(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE) && (InterlockedExchange(&FxHandle->DevicePowerNotRequiredPending, FALSE) != FALSE))
        InterlockedExchange(&FxHandle->DevicePoweredOn, FALSE);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxIdleComponent(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ ULONG Flags)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);
    LONG References;
    LONG NewReferences;
    UNREFERENCED_PARAMETER(Flags);

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE) &&
        (Component < FxHandle->ComponentCount))
    {
        do
        {
            References = FxHandle->ComponentState[Component].ActiveReferences;
            if (References == 0)
                return;
            NewReferences = References - 1;
        } while (InterlockedCompareExchange(&FxHandle->ComponentState[Component].ActiveReferences, NewReferences, References) != References);

        if ((NewReferences == 0) && FxHandle->Started)
            PopFxRequestIdleCondition(FxHandle, Component);
    }
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxCompleteIdleCondition(
    _In_ POHANDLE Handle,
    _In_ ULONG Component)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE) && (Component < FxHandle->ComponentCount) && (InterlockedExchange(&FxHandle->ComponentState[Component].IdleConditionPending, FALSE) != FALSE))
        PopFxRequestIdleState(FxHandle, Component);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxCompleteIdleState(
    _In_ POHANDLE Handle,
    _In_ ULONG Component)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE) && (Component < FxHandle->ComponentCount) && (InterlockedExchange(&FxHandle->ComponentState[Component].IdleStatePending, FALSE) != FALSE))
    {
        if (InterlockedExchange(&FxHandle->ComponentState[Component].ActiveTransitionPending, FALSE) != FALSE)
        {
            /* The component is back at F0; report the active condition. */
            if ((InterlockedExchange(&FxHandle->ComponentState[Component].Active, TRUE) == FALSE) &&
                (FxHandle->Device->ComponentActiveConditionCallback != NULL))
            {
                FxHandle->Device->ComponentActiveConditionCallback(FxHandle->Device->DeviceContext, Component);
            }
            return;
        }
        InterlockedExchange(&FxHandle->ComponentState[Component].Active, FALSE);
        PopFxRequestDevicePowerNotRequired(FxHandle);
    }
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxSetDeviceIdleTimeout(
    _In_ POHANDLE Handle,
    _In_ ULONGLONG IdleTimeout)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE))
        FxHandle->IdleTimeout = IdleTimeout;
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxReportDevicePoweredOn(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE))
    {
        InterlockedExchange(&FxHandle->DevicePowerNotRequiredPending, FALSE);
        InterlockedExchange(&FxHandle->DevicePoweredOn, TRUE);
    }
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxSetComponentLatency(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ ULONGLONG Latency)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE) && (Component < FxHandle->ComponentCount))
        FxHandle->ComponentState[Component].Latency = Latency;
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxSetComponentResidency(
    _In_ POHANDLE Handle,
    _In_ ULONG Component,
    _In_ ULONGLONG Residency)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE) && (Component < FxHandle->ComponentCount))
        FxHandle->ComponentState[Component].Residency = Residency;
}

NTKRNLVISTAAPI
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

    if ((FxHandle == NULL) || (FxHandle->Signature != ROS_PO_FX_SIGNATURE) ||
        (PowerControlCode == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FxHandle->Device->PowerControlCallback == NULL)
        return STATUS_NOT_SUPPORTED;

    return FxHandle->Device->PowerControlCallback(FxHandle->Device->DeviceContext, PowerControlCode, InBuffer, InBufferSize, OutBuffer, OutBufferSize, BytesReturned);
}

NTKRNLVISTAAPI
VOID
NTAPI
PoFxCompleteDirectedPowerDown(
    _In_ POHANDLE Handle)
{
    PROS_PO_FX_HANDLE FxHandle = ROS_PO_FX_HANDLE(Handle);

    if ((FxHandle != NULL) && (FxHandle->Signature == ROS_PO_FX_SIGNATURE))
        InterlockedExchange(&FxHandle->DevicePoweredOn, FALSE);
}

NTKRNLVISTAAPI
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

NTKRNLVISTAAPI
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
