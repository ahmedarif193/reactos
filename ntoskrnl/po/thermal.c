/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later
 * PURPOSE:         Windows thermal cooling request aggregation
 * COPYRIGHT:       Copyright 2026 ReactOS Project
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define POP_THERMAL_REQUEST_SIGNATURE 'qRhT'
#define POP_THERMAL_REQUEST_TAG       'hToP'

typedef struct _POP_THERMAL_TARGET POP_THERMAL_TARGET, *PPOP_THERMAL_TARGET;

typedef struct _POP_THERMAL_REQUEST
{
    ULONG Signature;
    LIST_ENTRY Link;
    PPOP_THERMAL_TARGET Target;
    PDEVICE_OBJECT PolicyDeviceObject;
    ULONG Flags;
    UCHAR PassiveThrottle;
    BOOLEAN ActiveEngaged;
} POP_THERMAL_REQUEST, *PPOP_THERMAL_REQUEST;

struct _POP_THERMAL_TARGET
{
    LIST_ENTRY Link;
    LIST_ENTRY RequestList;
    PDEVICE_OBJECT DeviceObject;
    THERMAL_COOLING_INTERFACE Interface;
    UCHAR EffectivePassiveThrottle;
    BOOLEAN EffectiveActiveEngaged;
    BOOLEAN ApplyInProgress;
};

static LIST_ENTRY PopThermalTargetList;
static FAST_MUTEX PopThermalLock;

static PPOP_THERMAL_TARGET PopThermalFindTargetLocked(PDEVICE_OBJECT DeviceObject)
{
    PPOP_THERMAL_TARGET Target;
    PLIST_ENTRY Link;

    for (Link = PopThermalTargetList.Flink; Link != &PopThermalTargetList; Link = Link->Flink)
    {
        Target = CONTAINING_RECORD(Link, POP_THERMAL_TARGET, Link);
        if (Target->DeviceObject == DeviceObject)
            return Target;
    }

    return NULL;
}

static NTSTATUS PopThermalQueryCoolingInterface(PDEVICE_OBJECT DeviceObject, PTHERMAL_COOLING_INTERFACE Interface)
{
    PIO_STACK_LOCATION Stack;
    PDEVICE_OBJECT TargetObject;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(Interface, sizeof(*Interface));
    TargetObject = IoGetAttachedDeviceReference(DeviceObject);
    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
    IoStatus.Status = STATUS_NOT_SUPPORTED;
    IoStatus.Information = 0;
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP, TargetObject, NULL, 0, NULL, &Event, &IoStatus);
    if (!Irp)
    {
        ObDereferenceObject(TargetObject);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_THERMAL_COOLING_INTERFACE;
    Stack->Parameters.QueryInterface.Size = sizeof(*Interface);
    Stack->Parameters.QueryInterface.Version = THERMAL_COOLING_INTERFACE_VERSION;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)Interface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(TargetObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    ObDereferenceObject(TargetObject);

    if (NT_SUCCESS(Status) && (Interface->Size < sizeof(*Interface) || Interface->Version != THERMAL_COOLING_INTERFACE_VERSION))
        Status = STATUS_NOT_SUPPORTED;
    if (!NT_SUCCESS(Status))
        RtlZeroMemory(Interface, sizeof(*Interface));
    return Status;
}

static PPOP_THERMAL_TARGET PopThermalCreateTarget(PDEVICE_OBJECT DeviceObject)
{
    PPOP_THERMAL_TARGET Target;
    NTSTATUS Status;

    Target = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*Target), POP_THERMAL_REQUEST_TAG);
    if (!Target)
        return NULL;

    InitializeListHead(&Target->RequestList);
    Target->DeviceObject = DeviceObject;
    Target->EffectivePassiveThrottle = 100;
    ObReferenceObject(DeviceObject);

    Status = PopThermalQueryCoolingInterface(DeviceObject, &Target->Interface);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED)
        DPRINT("Po: Thermal cooling interface query returned 0x%lx\n", Status);

    return Target;
}

static VOID PopThermalDestroyTarget(PPOP_THERMAL_TARGET Target)
{
    if (Target->Interface.InterfaceDereference)
        Target->Interface.InterfaceDereference(Target->Interface.Context);
    ObDereferenceObject(Target->DeviceObject);
    ExFreePoolWithTag(Target, POP_THERMAL_REQUEST_TAG);
}

static VOID PopThermalComputeTargetStateLocked(PPOP_THERMAL_TARGET Target, PUCHAR PassiveThrottle, PBOOLEAN ActiveEngaged)
{
    PPOP_THERMAL_REQUEST Request;
    PLIST_ENTRY Link;

    *PassiveThrottle = 100;
    *ActiveEngaged = FALSE;
    for (Link = Target->RequestList.Flink; Link != &Target->RequestList; Link = Link->Flink)
    {
        Request = CONTAINING_RECORD(Link, POP_THERMAL_REQUEST, Link);
        if (Request->PassiveThrottle < *PassiveThrottle)
            *PassiveThrottle = Request->PassiveThrottle;
        if (Request->ActiveEngaged)
            *ActiveEngaged = TRUE;
    }
}

static BOOLEAN PopThermalBeginApplyLocked(PPOP_THERMAL_TARGET Target)
{
    UCHAR PassiveThrottle;
    BOOLEAN ActiveEngaged;

    if (Target->ApplyInProgress)
        return FALSE;

    PopThermalComputeTargetStateLocked(Target, &PassiveThrottle, &ActiveEngaged);
    if (PassiveThrottle == Target->EffectivePassiveThrottle && ActiveEngaged == Target->EffectiveActiveEngaged)
        return FALSE;

    Target->ApplyInProgress = TRUE;
    return TRUE;
}

static VOID PopThermalApplyTargetState(PPOP_THERMAL_TARGET Target)
{
    PDEVICE_PASSIVE_COOLING PassiveCooling;
    PDEVICE_ACTIVE_COOLING ActiveCooling;
    UCHAR PassiveThrottle;
    BOOLEAN ActiveEngaged;
    BOOLEAN ApplyPassive;
    BOOLEAN ApplyActive;
    BOOLEAN DestroyTarget;
    PVOID Context;

    for (;;)
    {
        DestroyTarget = FALSE;
        ExAcquireFastMutex(&PopThermalLock);
        PopThermalComputeTargetStateLocked(Target, &PassiveThrottle, &ActiveEngaged);
        ApplyPassive = PassiveThrottle != Target->EffectivePassiveThrottle;
        ApplyActive = ActiveEngaged != Target->EffectiveActiveEngaged;
        if (!ApplyPassive && !ApplyActive)
        {
            Target->ApplyInProgress = FALSE;
            if (IsListEmpty(&Target->RequestList))
            {
                RemoveEntryList(&Target->Link);
                DestroyTarget = TRUE;
            }
            ExReleaseFastMutex(&PopThermalLock);
            if (DestroyTarget)
                PopThermalDestroyTarget(Target);
            return;
        }

        PassiveCooling = Target->Interface.PassiveCooling;
        ActiveCooling = Target->Interface.ActiveCooling;
        Context = Target->Interface.Context;
        if (ApplyPassive)
            Target->EffectivePassiveThrottle = PassiveThrottle;
        if (ApplyActive)
            Target->EffectiveActiveEngaged = ActiveEngaged;
        ExReleaseFastMutex(&PopThermalLock);

        if (ApplyPassive && PassiveCooling)
            PassiveCooling(Context, PassiveThrottle);
        if (ApplyActive && ActiveCooling)
            ActiveCooling(Context, ActiveEngaged);
    }
}

VOID NTAPI PopInitializeThermalRequests(VOID)
{
    InitializeListHead(&PopThermalTargetList);
    ExInitializeFastMutex(&PopThermalLock);
}

NTSTATUS NTAPI PoCreateThermalRequest(PVOID *ThermalRequest, PDEVICE_OBJECT TargetDeviceObject, PDEVICE_OBJECT PolicyDeviceObject, PCOUNTED_REASON_CONTEXT Context, ULONG Flags)
{
    PPOP_THERMAL_TARGET CandidateTarget;
    PPOP_THERMAL_TARGET Target;
    PPOP_THERMAL_REQUEST Request;

    PAGED_CODE();

    if (!ThermalRequest || !TargetDeviceObject || !PolicyDeviceObject || !Context)
        return STATUS_INVALID_PARAMETER;
    *ThermalRequest = NULL;

    Request = ExAllocatePool2(POOL_FLAG_NON_PAGED, sizeof(*Request), POP_THERMAL_REQUEST_TAG);
    if (!Request)
        return STATUS_INSUFFICIENT_RESOURCES;

    Request->Signature = POP_THERMAL_REQUEST_SIGNATURE;
    Request->Flags = Flags;
    Request->PassiveThrottle = 100;
    Request->PolicyDeviceObject = PolicyDeviceObject;
    ObReferenceObject(PolicyDeviceObject);

    ExAcquireFastMutex(&PopThermalLock);
    Target = PopThermalFindTargetLocked(TargetDeviceObject);
    if (Target)
    {
        Request->Target = Target;
        InsertTailList(&Target->RequestList, &Request->Link);
        ExReleaseFastMutex(&PopThermalLock);
        *ThermalRequest = Request;
        return STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&PopThermalLock);

    CandidateTarget = PopThermalCreateTarget(TargetDeviceObject);
    if (!CandidateTarget)
    {
        ObDereferenceObject(PolicyDeviceObject);
        ExFreePoolWithTag(Request, POP_THERMAL_REQUEST_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    ExAcquireFastMutex(&PopThermalLock);
    Target = PopThermalFindTargetLocked(TargetDeviceObject);
    if (!Target)
    {
        Target = CandidateTarget;
        CandidateTarget = NULL;
        InsertTailList(&PopThermalTargetList, &Target->Link);
    }
    Request->Target = Target;
    InsertTailList(&Target->RequestList, &Request->Link);
    ExReleaseFastMutex(&PopThermalLock);

    if (CandidateTarget)
        PopThermalDestroyTarget(CandidateTarget);
    *ThermalRequest = Request;
    return STATUS_SUCCESS;
}

BOOLEAN NTAPI PoGetThermalRequestSupport(PVOID ThermalRequest, PO_THERMAL_REQUEST_TYPE Type)
{
    PPOP_THERMAL_REQUEST Request = ThermalRequest;
    BOOLEAN Supported = FALSE;

    PAGED_CODE();

    if (!Request)
        return FALSE;

    ExAcquireFastMutex(&PopThermalLock);
    if (Request->Signature == POP_THERMAL_REQUEST_SIGNATURE)
    {
        if (Type == PoThermalRequestPassive)
            Supported = (Request->Target->Interface.Flags & ThermalDeviceFlagPassiveCooling) != 0 && Request->Target->Interface.PassiveCooling != NULL;
        else if (Type == PoThermalRequestActive)
            Supported = (Request->Target->Interface.Flags & ThermalDeviceFlagActiveCooling) != 0 && Request->Target->Interface.ActiveCooling != NULL;
    }
    ExReleaseFastMutex(&PopThermalLock);
    return Supported;
}

NTSTATUS NTAPI PoSetThermalPassiveCooling(PVOID ThermalRequest, UCHAR Throttle)
{
    PPOP_THERMAL_REQUEST Request = ThermalRequest;
    PPOP_THERMAL_TARGET Target = NULL;
    BOOLEAN RunApply = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    if (!Request || Throttle > 100)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&PopThermalLock);
    if (Request->Signature != POP_THERMAL_REQUEST_SIGNATURE)
        Status = STATUS_INVALID_PARAMETER;
    else if (!(Request->Target->Interface.Flags & ThermalDeviceFlagPassiveCooling) || !Request->Target->Interface.PassiveCooling)
        Status = STATUS_NOT_SUPPORTED;
    else
    {
        Request->PassiveThrottle = Throttle;
        Target = Request->Target;
        RunApply = PopThermalBeginApplyLocked(Target);
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&PopThermalLock);
    if (RunApply)
        PopThermalApplyTargetState(Target);
    return Status;
}

NTSTATUS NTAPI PoSetThermalActiveCooling(PVOID ThermalRequest, BOOLEAN Engaged)
{
    PPOP_THERMAL_REQUEST Request = ThermalRequest;
    PPOP_THERMAL_TARGET Target = NULL;
    BOOLEAN RunApply = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    if (!Request)
        return STATUS_INVALID_PARAMETER;

    Engaged = !!Engaged;
    ExAcquireFastMutex(&PopThermalLock);
    if (Request->Signature != POP_THERMAL_REQUEST_SIGNATURE)
        Status = STATUS_INVALID_PARAMETER;
    else if (!(Request->Target->Interface.Flags & ThermalDeviceFlagActiveCooling) || !Request->Target->Interface.ActiveCooling)
        Status = STATUS_NOT_SUPPORTED;
    else
    {
        Request->ActiveEngaged = Engaged;
        Target = Request->Target;
        RunApply = PopThermalBeginApplyLocked(Target);
        Status = STATUS_SUCCESS;
    }
    ExReleaseFastMutex(&PopThermalLock);
    if (RunApply)
        PopThermalApplyTargetState(Target);
    return Status;
}

VOID NTAPI PoDeleteThermalRequest(PVOID ThermalRequest)
{
    PPOP_THERMAL_TARGET Target = NULL;
    PPOP_THERMAL_REQUEST Request = ThermalRequest;
    BOOLEAN RunApply = FALSE;
    BOOLEAN DestroyTarget = FALSE;

    PAGED_CODE();

    if (!Request)
        return;

    ExAcquireFastMutex(&PopThermalLock);
    if (Request->Signature != POP_THERMAL_REQUEST_SIGNATURE)
    {
        ExReleaseFastMutex(&PopThermalLock);
        DPRINT1("PoDeleteThermalRequest: Invalid request %p\n", ThermalRequest);
        return;
    }

    Target = Request->Target;
    Request->Signature = 0;
    RemoveEntryList(&Request->Link);
    RunApply = PopThermalBeginApplyLocked(Target);
    if (IsListEmpty(&Target->RequestList) && !RunApply && !Target->ApplyInProgress)
    {
        RemoveEntryList(&Target->Link);
        DestroyTarget = TRUE;
    }
    ExReleaseFastMutex(&PopThermalLock);

    ObDereferenceObject(Request->PolicyDeviceObject);
    ExFreePoolWithTag(Request, POP_THERMAL_REQUEST_TAG);
    if (RunApply)
        PopThermalApplyTargetState(Target);
    else if (DestroyTarget)
        PopThermalDestroyTarget(Target);
}
