
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
    PPO_FX_DEVICE Device;
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
    PPO_FX_COMPONENT Description = &FxHandle->Device->Components[Component];
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


NTKRNLVISTAAPI
NTSTATUS
NTAPI
PoFxRegisterDevice(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ PPO_FX_DEVICE Device,
    _Out_ POHANDLE *Handle)
{
    PROS_PO_FX_HANDLE NewHandle;
    PPO_FX_DEVICE DeviceCopy;
    SIZE_T AllocationSize;
    SIZE_T DeviceSize;
    SIZE_T IdleStatesSize;
    PUCHAR IdleStatesCursor;
    ULONG Component;

    if ((Pdo == NULL) || (Device == NULL) || (Handle == NULL) ||
        (Device->Version != PO_FX_VERSION) || (Device->ComponentCount == 0))
    {
        return STATUS_INVALID_PARAMETER;
    }

    DeviceSize = FIELD_OFFSET(PO_FX_DEVICE, Components);
    if ((SIZE_T)Device->ComponentCount > (MAXULONG_PTR - DeviceSize) / sizeof(PO_FX_COMPONENT))
        return STATUS_INTEGER_OVERFLOW;
    DeviceSize += Device->ComponentCount * sizeof(PO_FX_COMPONENT);

    IdleStatesSize = 0;
    for (Component = 0; Component != Device->ComponentCount; Component++)
    {
        PPO_FX_COMPONENT Description = &Device->Components[Component];

        if ((Description->IdleStateCount == 0) || (Description->IdleStates == NULL) || (Description->DeepestWakeableIdleState >= Description->IdleStateCount))
            return STATUS_INVALID_PARAMETER;
        if ((Description->IdleStateCount > 1) && ((Device->ComponentIdleStateCallback == NULL) || (Device->ComponentActiveConditionCallback == NULL) || (Device->ComponentIdleConditionCallback == NULL)))
            return STATUS_INVALID_PARAMETER;
        if ((SIZE_T)Description->IdleStateCount > (MAXULONG_PTR - IdleStatesSize) / sizeof(PO_FX_COMPONENT_IDLE_STATE))
            return STATUS_INTEGER_OVERFLOW;
        IdleStatesSize += Description->IdleStateCount * sizeof(PO_FX_COMPONENT_IDLE_STATE);
    }
    if (DeviceSize > MAXULONG_PTR - IdleStatesSize)
        return STATUS_INTEGER_OVERFLOW;

    if ((SIZE_T)Device->ComponentCount > (MAXULONG_PTR - FIELD_OFFSET(ROS_PO_FX_HANDLE, ComponentState)) / sizeof(ROS_PO_FX_COMPONENT_STATE))
        return STATUS_INTEGER_OVERFLOW;
    AllocationSize = FIELD_OFFSET(ROS_PO_FX_HANDLE, ComponentState) + Device->ComponentCount * sizeof(ROS_PO_FX_COMPONENT_STATE);
    NewHandle = ExAllocatePoolZero(NonPagedPool, AllocationSize, ROS_PO_FX_SIGNATURE);
    if (NewHandle == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    DeviceCopy = ExAllocatePoolZero(NonPagedPool, DeviceSize + IdleStatesSize, ROS_PO_FX_SIGNATURE);
    if (DeviceCopy == NULL)
    {
        ExFreePoolWithTag(NewHandle, ROS_PO_FX_SIGNATURE);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(DeviceCopy, Device, DeviceSize);
    IdleStatesCursor = (PUCHAR)DeviceCopy + DeviceSize;
    for (Component = 0; Component != Device->ComponentCount; Component++)
    {
        SIZE_T Size = Device->Components[Component].IdleStateCount * sizeof(PO_FX_COMPONENT_IDLE_STATE);

        DeviceCopy->Components[Component].IdleStates = (PPO_FX_COMPONENT_IDLE_STATE)IdleStatesCursor;
        RtlCopyMemory(IdleStatesCursor, Device->Components[Component].IdleStates, Size);
        IdleStatesCursor += Size;
        NewHandle->ComponentState[Component].Latency = PO_FX_UNKNOWN_TIME;
        NewHandle->ComponentState[Component].Residency = PO_FX_UNKNOWN_TIME;
        NewHandle->ComponentState[Component].Active = TRUE;
    }

    NewHandle->Signature = ROS_PO_FX_SIGNATURE;
    NewHandle->Pdo = Pdo;
    NewHandle->Device = DeviceCopy;
    NewHandle->ComponentCount = Device->ComponentCount;
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
            InterlockedExchange(&FxHandle->ComponentState[Component].IdleStatePending, FALSE);
            InterlockedExchange(&FxHandle->DevicePowerNotRequiredPending, FALSE);
            if (!FxHandle->DevicePoweredOn && (FxHandle->Device->DevicePowerRequiredCallback != NULL))
                FxHandle->Device->DevicePowerRequiredCallback(FxHandle->Device->DeviceContext);
            InterlockedExchange(&FxHandle->DevicePoweredOn, TRUE);
            if ((InterlockedExchange(&FxHandle->ComponentState[Component].Active, TRUE) == FALSE) && (FxHandle->Device->ComponentActiveConditionCallback != NULL))
                FxHandle->Device->ComponentActiveConditionCallback(FxHandle->Device->DeviceContext, Component);
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
