/*
 * PROJECT:     ReactOS ACPI bus driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows-compatible ACPI thermal-zone control
 * COPYRIGHT:   Copyright 2026 ReactOS Project
 */

#include "precomp.h"

#include <poclass.h>
#include <reactos/drivers/acpi/acpithermal.h>

#define NDEBUG
#include <debug.h>

#define ACPI_THERMAL_MAX_COOLING_DEVICES (MAX_ACTIVE_COOLING_LEVELS * ACPI_MAX_HANDLES)
#define ACPI_THERMAL_REQUEST_TAG 'rThA'
#define ACPI_THERMAL_WORK_TAG    'wThA'
#define ACPI_THERMAL_DEVICE_TAG  'dThA'

typedef struct _ACPI_THERMAL_COOLING_DEVICE
{
    ACPI_HANDLE Handle;
    BOOLEAN Engaged;
} ACPI_THERMAL_COOLING_DEVICE, *PACPI_THERMAL_COOLING_DEVICE;

typedef struct _ACPI_THERMAL_REQUEST_ENTRY
{
    LIST_ENTRY Link;
    ACPI_HANDLE Handle;
    PVOID Request;
    EX_RUNDOWN_REF Rundown;
} ACPI_THERMAL_REQUEST_ENTRY, *PACPI_THERMAL_REQUEST_ENTRY;

typedef struct _ACPI_THERMAL_WORK_CONTEXT
{
    WORK_QUEUE_ITEM WorkItem;
    PPDO_DEVICE_DATA DeviceData;
} ACPI_THERMAL_WORK_CONTEXT, *PACPI_THERMAL_WORK_CONTEXT;

static VOID NTAPI AcpiThermalCsqInsertIrp(PIO_CSQ Csq, PIRP Irp)
{
    PPDO_DEVICE_DATA DeviceData = CONTAINING_RECORD(Csq, PDO_DEVICE_DATA, ThermalCsq);

    InsertTailList(&DeviceData->ThermalPendingIrpList, &Irp->Tail.Overlay.ListEntry);
}

static VOID NTAPI AcpiThermalCsqRemoveIrp(PIO_CSQ Csq, PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}

static PIRP NTAPI AcpiThermalCsqPeekNextIrp(PIO_CSQ Csq, PIRP Irp, PVOID PeekContext)
{
    PPDO_DEVICE_DATA DeviceData = CONTAINING_RECORD(Csq, PDO_DEVICE_DATA, ThermalCsq);
    PIO_STACK_LOCATION Stack;
    PLIST_ENTRY Head = &DeviceData->ThermalPendingIrpList;
    PLIST_ENTRY Link = Irp ? Irp->Tail.Overlay.ListEntry.Flink : Head->Flink;
    ULONG CurrentStamp;

    while (Link != Head)
    {
        Irp = CONTAINING_RECORD(Link, IRP, Tail.Overlay.ListEntry);
        Link = Link->Flink;
        if (!PeekContext)
            return Irp;

        CurrentStamp = *(PULONG)PeekContext;
        Stack = IoGetCurrentIrpStackLocation(Irp);
        if (Stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG) || !Irp->AssociatedIrp.SystemBuffer || *(PULONG)Irp->AssociatedIrp.SystemBuffer != CurrentStamp)
            return Irp;
    }

    return NULL;
}

static VOID NTAPI AcpiThermalCsqAcquireLock(PIO_CSQ Csq, PKIRQL Irql)
{
    PPDO_DEVICE_DATA DeviceData = CONTAINING_RECORD(Csq, PDO_DEVICE_DATA, ThermalCsq);

    KeAcquireSpinLock(&DeviceData->ThermalPendingIrpLock, Irql);
}

static VOID NTAPI AcpiThermalCsqReleaseLock(PIO_CSQ Csq, KIRQL Irql)
{
    PPDO_DEVICE_DATA DeviceData = CONTAINING_RECORD(Csq, PDO_DEVICE_DATA, ThermalCsq);

    KeReleaseSpinLock(&DeviceData->ThermalPendingIrpLock, Irql);
}

static VOID NTAPI AcpiThermalCsqCompleteCanceledIrp(PIO_CSQ Csq, PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

VOID AcpiThermalInitialize(PPDO_DEVICE_DATA DeviceData)
{
    NTSTATUS Status;

    DeviceData->ThermalStamp = 1;
    DeviceData->ThermalCoolingPolicy = ACTIVE_COOLING;
    DeviceData->ThermalPassiveLimit = 100;
    DeviceData->ThermalActiveLevel = MAX_ACTIVE_COOLING_LEVELS;
    InitializeListHead(&DeviceData->ThermalRequestList);
    ExInitializeFastMutex(&DeviceData->ThermalRequestMutex);
    InitializeListHead(&DeviceData->ThermalPendingIrpList);
    KeInitializeSpinLock(&DeviceData->ThermalPendingIrpLock);
    ExInitializeRundownProtection(&DeviceData->ThermalWorkRundown);
    Status = IoCsqInitialize(&DeviceData->ThermalCsq, AcpiThermalCsqInsertIrp, AcpiThermalCsqRemoveIrp, AcpiThermalCsqPeekNextIrp, AcpiThermalCsqAcquireLock, AcpiThermalCsqReleaseLock, AcpiThermalCsqCompleteCanceledIrp);
    ASSERT(NT_SUCCESS(Status));
}

static NTSTATUS AcpiThermalStatusToNtStatus(ACPI_STATUS Status)
{
    switch (Status)
    {
        case AE_OK:
            return STATUS_SUCCESS;
        case AE_NO_MEMORY:
            return STATUS_INSUFFICIENT_RESOURCES;
        case AE_NOT_FOUND:
        case AE_NOT_EXIST:
            return STATUS_OBJECT_NAME_NOT_FOUND;
        case AE_BAD_PARAMETER:
        case AE_BAD_DATA:
        case AE_TYPE:
            return STATUS_DEVICE_DATA_ERROR;
        default:
            return STATUS_UNSUCCESSFUL;
    }
}

static BOOLEAN AcpiThermalIsZone(PPDO_DEVICE_DATA DeviceData)
{
    return AcpiHardwareIdContains(DeviceData, L"ThermalZone");
}

static ACPI_STATUS AcpiThermalGetInteger(ACPI_HANDLE Handle, ACPI_STRING Name, PULONG Value)
{
    unsigned long long Integer;
    ACPI_STATUS Status;

    Status = acpi_evaluate_integer(Handle, Name, NULL, &Integer);
    if (ACPI_FAILURE(Status))
        return Status;

    if (Integer > MAXULONG)
        return AE_LIMIT;

    *Value = (ULONG)Integer;
    return AE_OK;
}

static VOID AcpiThermalFillLegacy(PTHERMAL_INFORMATION Legacy, PACPI_THERMAL_INFORMATION_EX Extended)
{
    RtlZeroMemory(Legacy, sizeof(*Legacy));
    Legacy->ThermalStamp = Extended->ThermalStamp;
    Legacy->ThermalConstant1 = Extended->ThermalConstant1;
    Legacy->ThermalConstant2 = Extended->ThermalConstant2;
    Legacy->Processors = KeQueryActiveProcessors();
    Legacy->SamplingPeriod = Extended->SamplingPeriod;
    Legacy->CurrentTemperature = Extended->CurrentTemperature;
    Legacy->PassiveTripPoint = Extended->PassiveTripPoint;
    Legacy->CriticalTripPoint = Extended->CriticalTripPoint;
    Legacy->ActiveTripPointCount = Extended->ActiveTripPointCount;
    RtlCopyMemory(Legacy->ActiveTripPoint, Extended->ActiveTripPoint, sizeof(Legacy->ActiveTripPoint));
}

static NTSTATUS AcpiThermalBuildInformation(PPDO_DEVICE_DATA DeviceData, PACPI_THERMAL_INFORMATION_EX Information)
{
    struct acpi_handle_list PassiveDevices;
    ACPI_STATUS Status;
    CHAR Name[] = "_AC0";
    ULONG Index;

    RtlZeroMemory(Information, sizeof(*Information));
    Information->ThermalStamp = (ULONG)InterlockedCompareExchange(&DeviceData->ThermalStamp, 0, 0);
    Information->PassiveTripPoint = MAXULONG;
    Information->ThermalStandbyTripPoint = MAXULONG;
    Information->CriticalTripPoint = MAXULONG;
    Information->S4TransitionTripPoint = MAXULONG;
    for (Index = 0; Index < MAX_ACTIVE_COOLING_LEVELS; Index++)
        Information->ActiveTripPoint[Index] = MAXULONG;

    Status = AcpiThermalGetInteger(DeviceData->AcpiHandle, "_TMP", &Information->CurrentTemperature);
    if (ACPI_FAILURE(Status))
        return AcpiThermalStatusToNtStatus(Status);

    (void)AcpiThermalGetInteger(DeviceData->AcpiHandle, "_TC1", &Information->ThermalConstant1);
    (void)AcpiThermalGetInteger(DeviceData->AcpiHandle, "_TC2", &Information->ThermalConstant2);
    (void)AcpiThermalGetInteger(DeviceData->AcpiHandle, "_TSP", &Information->SamplingPeriod);
    (void)AcpiThermalGetInteger(DeviceData->AcpiHandle, "_PSV", &Information->PassiveTripPoint);
    if (ACPI_SUCCESS(AcpiThermalGetInteger(DeviceData->AcpiHandle, "_HOT", &Information->S4TransitionTripPoint)))
        Information->ThermalStandbyTripPoint = Information->S4TransitionTripPoint;
    (void)AcpiThermalGetInteger(DeviceData->AcpiHandle, "_CRT", &Information->CriticalTripPoint);
    if (ACPI_SUCCESS(AcpiThermalGetInteger(DeviceData->AcpiHandle, "_TZP", &Information->PollingPeriod)))
        Information->PollingPeriod *= 100;

    RtlZeroMemory(&PassiveDevices, sizeof(PassiveDevices));
    Information->PassiveCoolingDevicesPresent = ACPI_SUCCESS(acpi_evaluate_reference(DeviceData->AcpiHandle, "_PSL", NULL, &PassiveDevices)) && PassiveDevices.count != 0;

    for (Index = 0; Index < MAX_ACTIVE_COOLING_LEVELS; Index++)
    {
        Name[3] = (CHAR)('0' + Index);
        Status = AcpiThermalGetInteger(DeviceData->AcpiHandle, Name, &Information->ActiveTripPoint[Index]);
        if (ACPI_FAILURE(Status))
            break;
        Information->ActiveTripPointCount++;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS AcpiThermalCompleteQuery(PPDO_DEVICE_DATA DeviceData, PIRP Irp)
{
    ACPI_THERMAL_INFORMATION_EX Information;
    PIO_STACK_LOCATION Stack;
    ULONG OutputLength;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    Status = AcpiThermalBuildInformation(DeviceData, &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (OutputLength >= sizeof(Information))
    {
        RtlCopyMemory(Irp->AssociatedIrp.SystemBuffer, &Information, sizeof(Information));
        Irp->IoStatus.Information = sizeof(Information);
        return STATUS_SUCCESS;
    }
    if (OutputLength >= sizeof(THERMAL_INFORMATION))
    {
        AcpiThermalFillLegacy(Irp->AssociatedIrp.SystemBuffer, &Information);
        Irp->IoStatus.Information = sizeof(THERMAL_INFORMATION);
        return STATUS_SUCCESS;
    }
    return STATUS_BUFFER_TOO_SMALL;
}

static VOID NTAPI AcpiThermalQueryWorkItem(PVOID Context)
{
    PACPI_THERMAL_WORK_CONTEXT WorkContext = Context;
    PPDO_DEVICE_DATA DeviceData = WorkContext->DeviceData;
    ULONG CurrentStamp;
    PIRP Irp;
    NTSTATUS Status;

    CurrentStamp = (ULONG)InterlockedCompareExchange(&DeviceData->ThermalStamp, 0, 0);
    while ((Irp = IoCsqRemoveNextIrp(&DeviceData->ThermalCsq, &CurrentStamp)) != NULL)
    {
        Status = AcpiThermalCompleteQuery(DeviceData, Irp);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    ExReleaseRundownProtection(&DeviceData->ThermalWorkRundown);
    ExFreePoolWithTag(WorkContext, ACPI_THERMAL_WORK_TAG);
}

static VOID AcpiThermalQueueQueryWork(PPDO_DEVICE_DATA DeviceData)
{
    PACPI_THERMAL_WORK_CONTEXT WorkContext;

    if (InterlockedCompareExchange(&DeviceData->ThermalStopping, FALSE, FALSE) || !ExAcquireRundownProtection(&DeviceData->ThermalWorkRundown))
        return;

    WorkContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*WorkContext), ACPI_THERMAL_WORK_TAG);
    if (!WorkContext)
    {
        ExReleaseRundownProtection(&DeviceData->ThermalWorkRundown);
        DPRINT1("ACPI: Unable to allocate thermal notification work item\n");
        return;
    }

    WorkContext->DeviceData = DeviceData;
    ExInitializeWorkItem(&WorkContext->WorkItem, AcpiThermalQueryWorkItem, WorkContext);
    ExQueueWorkItem(&WorkContext->WorkItem, DelayedWorkQueue);
}

static PDEVICE_OBJECT AcpiThermalFindPdo(PPDO_DEVICE_DATA ThermalData, ACPI_HANDLE Handle)
{
    PFDO_DEVICE_DATA FdoData;
    PPDO_DEVICE_DATA PdoData;
    PDEVICE_OBJECT DeviceObject = NULL;
    PLIST_ENTRY Link;

    if (!ThermalData || !ThermalData->ParentFdo)
        return NULL;

    FdoData = FDO_FROM_PDO(ThermalData);
    ExAcquireFastMutex(&FdoData->Mutex);
    for (Link = FdoData->ListOfPDOs.Flink; Link != &FdoData->ListOfPDOs; Link = Link->Flink)
    {
        PdoData = CONTAINING_RECORD(Link, PDO_DEVICE_DATA, Link);
        if (PdoData->AcpiHandle == Handle)
        {
            DeviceObject = PdoData->Common.Self;
            ObReferenceObject(DeviceObject);
            break;
        }
    }
    ExReleaseFastMutex(&FdoData->Mutex);
    return DeviceObject;
}

static PACPI_THERMAL_REQUEST_ENTRY AcpiThermalFindRequestLocked(PPDO_DEVICE_DATA ThermalData, ACPI_HANDLE Handle)
{
    PACPI_THERMAL_REQUEST_ENTRY Entry;
    PLIST_ENTRY Link;

    for (Link = ThermalData->ThermalRequestList.Flink; Link != &ThermalData->ThermalRequestList; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, ACPI_THERMAL_REQUEST_ENTRY, Link);
        if (Entry->Handle == Handle)
            return Entry;
    }

    return NULL;
}

static VOID AcpiThermalDestroyRequest(PACPI_THERMAL_REQUEST_ENTRY Entry)
{
    PoDeleteThermalRequest(Entry->Request);
    ExFreePoolWithTag(Entry, ACPI_THERMAL_REQUEST_TAG);
}

static NTSTATUS AcpiThermalCreateRequest(PPDO_DEVICE_DATA ThermalData, ACPI_HANDLE Handle, PACPI_THERMAL_REQUEST_ENTRY *RequestEntry)
{
    COUNTED_REASON_CONTEXT ReasonContext;
    PACPI_THERMAL_REQUEST_ENTRY Entry;
    PDEVICE_OBJECT Pdo;
    NTSTATUS Status;

    Pdo = AcpiThermalFindPdo(ThermalData, Handle);
    if (!Pdo)
        return STATUS_NOT_SUPPORTED;

    Entry = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Entry), ACPI_THERMAL_REQUEST_TAG);
    if (!Entry)
    {
        ObDereferenceObject(Pdo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Entry, sizeof(*Entry));
    ExInitializeRundownProtection(&Entry->Rundown);
    RtlZeroMemory(&ReasonContext, sizeof(ReasonContext));
    ReasonContext.Version = POWER_REQUEST_CONTEXT_VERSION;
    ReasonContext.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    RtlInitUnicodeString(&ReasonContext.SimpleString, L"ACPI thermal zone");
    Status = PoCreateThermalRequest(&Entry->Request, Pdo, ThermalData->Common.Self, &ReasonContext, 0);
    ObDereferenceObject(Pdo);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Entry, ACPI_THERMAL_REQUEST_TAG);
        return Status;
    }

    Entry->Handle = Handle;
    *RequestEntry = Entry;
    return STATUS_SUCCESS;
}

static NTSTATUS AcpiThermalGetRequest(PPDO_DEVICE_DATA ThermalData, ACPI_HANDLE Handle, PACPI_THERMAL_REQUEST_ENTRY *RequestEntry)
{
    PACPI_THERMAL_REQUEST_ENTRY Candidate = NULL;
    PACPI_THERMAL_REQUEST_ENTRY Entry;
    NTSTATUS Status;

    *RequestEntry = NULL;
    ExAcquireFastMutex(&ThermalData->ThermalRequestMutex);
    if (ThermalData->ThermalStopping)
    {
        ExReleaseFastMutex(&ThermalData->ThermalRequestMutex);
        return STATUS_DELETE_PENDING;
    }

    Entry = AcpiThermalFindRequestLocked(ThermalData, Handle);
    if (Entry)
    {
        if (ExAcquireRundownProtection(&Entry->Rundown))
            *RequestEntry = Entry;
        ExReleaseFastMutex(&ThermalData->ThermalRequestMutex);
        return *RequestEntry ? STATUS_SUCCESS : STATUS_DELETE_PENDING;
    }
    ExReleaseFastMutex(&ThermalData->ThermalRequestMutex);

    Status = AcpiThermalCreateRequest(ThermalData, Handle, &Candidate);
    if (!NT_SUCCESS(Status))
        return Status;

    ExAcquireFastMutex(&ThermalData->ThermalRequestMutex);
    if (ThermalData->ThermalStopping)
    {
        Status = STATUS_DELETE_PENDING;
    }
    else
    {
        Entry = AcpiThermalFindRequestLocked(ThermalData, Handle);
        if (!Entry)
        {
            Entry = Candidate;
            Candidate = NULL;
            InsertTailList(&ThermalData->ThermalRequestList, &Entry->Link);
        }

        if (ExAcquireRundownProtection(&Entry->Rundown))
        {
            *RequestEntry = Entry;
            Status = STATUS_SUCCESS;
        }
        else
        {
            Status = STATUS_DELETE_PENDING;
        }
    }
    ExReleaseFastMutex(&ThermalData->ThermalRequestMutex);

    if (Candidate)
        AcpiThermalDestroyRequest(Candidate);
    return Status;
}

NTSTATUS AcpiThermalSetPower(ACPI_HANDLE Handle, BOOLEAN Engaged)
{
    struct acpi_device *Device = NULL;
    UINT32 ForcePowerState = 0;
    BOOLEAN RestoreForce = FALSE;
    int Result;

    if (!acpi_bus_get_device(Handle, &Device) && Device)
    {
        ForcePowerState = Device->flags.force_power_state;
        Device->flags.force_power_state = 1;
        RestoreForce = TRUE;
    }

    Result = acpi_bus_set_power(Handle, Engaged ? ACPI_STATE_D0 : ACPI_STATE_D3);
    if (RestoreForce)
        Device->flags.force_power_state = ForcePowerState;
    return Result ? STATUS_UNSUCCESSFUL : STATUS_SUCCESS;
}

static NTSTATUS AcpiThermalApplyActive(PPDO_DEVICE_DATA ThermalData, ACPI_HANDLE Handle, BOOLEAN Engaged)
{
    PACPI_THERMAL_REQUEST_ENTRY Entry;
    BOOLEAN Supported;
    NTSTATUS Status;

    Status = AcpiThermalGetRequest(ThermalData, Handle, &Entry);
    if (NT_SUCCESS(Status))
    {
        Supported = PoGetThermalRequestSupport(Entry->Request, PoThermalRequestActive);
        Status = Supported ? PoSetThermalActiveCooling(Entry->Request, Engaged) : STATUS_NOT_SUPPORTED;
        ExReleaseRundownProtection(&Entry->Rundown);
        if (Supported)
            return Status;
    }

    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED)
        return Status;

    return AcpiThermalSetPower(Handle, Engaged);
}

static NTSTATUS AcpiThermalApplyPassive(PPDO_DEVICE_DATA ThermalData, ACPI_HANDLE Handle, UCHAR Percentage)
{
    PACPI_THERMAL_REQUEST_ENTRY Entry;
    BOOLEAN Supported;
    NTSTATUS Status;

    Status = AcpiThermalGetRequest(ThermalData, Handle, &Entry);
    if (NT_SUCCESS(Status))
    {
        Supported = PoGetThermalRequestSupport(Entry->Request, PoThermalRequestPassive);
        Status = Supported ? PoSetThermalPassiveCooling(Entry->Request, Percentage) : STATUS_NOT_SUPPORTED;
        ExReleaseRundownProtection(&Entry->Rundown);
        if (Supported)
            return Status;
    }
    return NT_SUCCESS(Status) ? STATUS_NOT_SUPPORTED : Status;
}

static NTSTATUS AcpiThermalSetActiveLevel(PPDO_DEVICE_DATA DeviceData, UCHAR Level)
{
    PACPI_THERMAL_COOLING_DEVICE Devices;
    struct acpi_handle_list List;
    CHAR Name[] = "_AL0";
    NTSTATUS FirstFailure = STATUS_SUCCESS;
    NTSTATUS Status;
    ULONG DeviceCount = 0;
    ULONG Index;
    ULONG ListIndex;
    ULONG DeviceIndex;
    BOOLEAN FoundList = FALSE;

    Devices = ExAllocatePoolWithTag(PagedPool, ACPI_THERMAL_MAX_COOLING_DEVICES * sizeof(*Devices), ACPI_THERMAL_DEVICE_TAG);
    if (!Devices)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Devices, ACPI_THERMAL_MAX_COOLING_DEVICES * sizeof(*Devices));
    for (ListIndex = 0; ListIndex < MAX_ACTIVE_COOLING_LEVELS; ListIndex++)
    {
        Name[3] = (CHAR)('0' + ListIndex);
        RtlZeroMemory(&List, sizeof(List));
        if (ACPI_FAILURE(acpi_evaluate_reference(DeviceData->AcpiHandle, Name, NULL, &List)))
            continue;

        FoundList = TRUE;
        for (Index = 0; Index < List.count; Index++)
        {
            for (DeviceIndex = 0; DeviceIndex < DeviceCount; DeviceIndex++)
            {
                if (Devices[DeviceIndex].Handle == List.handles[Index])
                    break;
            }

            if (DeviceIndex == DeviceCount)
            {
                if (DeviceCount == ACPI_THERMAL_MAX_COOLING_DEVICES)
                {
                    ExFreePoolWithTag(Devices, ACPI_THERMAL_DEVICE_TAG);
                    return STATUS_BUFFER_OVERFLOW;
                }
                Devices[DeviceCount].Handle = List.handles[Index];
                Devices[DeviceCount].Engaged = FALSE;
                DeviceIndex = DeviceCount++;
            }

            if (ListIndex >= Level)
                Devices[DeviceIndex].Engaged = TRUE;
        }
    }

    if (!FoundList)
    {
        ExFreePoolWithTag(Devices, ACPI_THERMAL_DEVICE_TAG);
        return STATUS_NOT_SUPPORTED;
    }

    for (DeviceIndex = 0; DeviceIndex < DeviceCount; DeviceIndex++)
    {
        Status = AcpiThermalApplyActive(DeviceData, Devices[DeviceIndex].Handle, Devices[DeviceIndex].Engaged);
        if (!NT_SUCCESS(Status) && NT_SUCCESS(FirstFailure))
            FirstFailure = Status;
    }

    if (NT_SUCCESS(FirstFailure))
    {
        DeviceData->ThermalActiveLevel = Level;
        DPRINT1("ACPI: Thermal active cooling level set to %u (%lu device(s))\n", Level, DeviceCount);
    }
    ExFreePoolWithTag(Devices, ACPI_THERMAL_DEVICE_TAG);
    return FirstFailure;
}

static NTSTATUS AcpiThermalSetPassiveLimit(PPDO_DEVICE_DATA DeviceData, UCHAR Percentage)
{
    struct acpi_handle_list List;
    NTSTATUS FirstFailure = STATUS_SUCCESS;
    NTSTATUS Status;
    ULONG Index;

    RtlZeroMemory(&List, sizeof(List));
    if (ACPI_FAILURE(acpi_evaluate_reference(DeviceData->AcpiHandle, "_PSL", NULL, &List)) || List.count == 0)
        return STATUS_NOT_SUPPORTED;

    for (Index = 0; Index < List.count; Index++)
    {
        Status = AcpiThermalApplyPassive(DeviceData, List.handles[Index], Percentage);
        if (!NT_SUCCESS(Status) && NT_SUCCESS(FirstFailure))
            FirstFailure = Status;
    }

    if (NT_SUCCESS(FirstFailure))
        DeviceData->ThermalPassiveLimit = Percentage;
    return FirstFailure;
}

static NTSTATUS AcpiThermalSetCoolingPolicy(PPDO_DEVICE_DATA DeviceData, UCHAR Policy)
{
    ACPI_OBJECT Argument;
    ACPI_OBJECT_LIST Arguments;
    ACPI_STATUS Status;

    Argument.Type = ACPI_TYPE_INTEGER;
    Argument.Integer.Value = Policy;
    Arguments.Count = 1;
    Arguments.Pointer = &Argument;
    Status = AcpiEvaluateObject(DeviceData->AcpiHandle, "_SCP", &Arguments, NULL);
    if (Status == AE_NOT_FOUND || Status == AE_NOT_EXIST)
        Status = AE_OK;
    if (ACPI_SUCCESS(Status))
        DeviceData->ThermalCoolingPolicy = Policy;
    return AcpiThermalStatusToNtStatus(Status);
}

static VOID NTAPI AcpiThermalNotify(PVOID Context, ULONG Value)
{
    PPDO_DEVICE_DATA DeviceData = Context;

    if (!DeviceData || (Value != 0x80 && Value != 0x81 && Value != 0x82))
        return;

    InterlockedIncrement(&DeviceData->ThermalStamp);
    DPRINT1("ACPI: Thermal notification 0x%lx, stamp %ld\n", Value, DeviceData->ThermalStamp);
    AcpiThermalQueueQueryWork(DeviceData);
}

NTSTATUS AcpiThermalStart(PPDO_DEVICE_DATA DeviceData)
{
    NTSTATUS Status;

    if (!AcpiThermalIsZone(DeviceData) || !DeviceData->AcpiHandle)
        return STATUS_SUCCESS;
    if (DeviceData->ThermalNotificationRegistered)
        return STATUS_SUCCESS;

    if (DeviceData->ThermalStopping)
    {
        ExReInitializeRundownProtection(&DeviceData->ThermalWorkRundown);
        InterlockedExchange(&DeviceData->ThermalStopping, FALSE);
    }
    Status = AcpiInterfaceNotificationsRegister(DeviceData->Common.Self, AcpiThermalNotify, DeviceData);
    if (NT_SUCCESS(Status))
        DeviceData->ThermalNotificationRegistered = TRUE;
    return Status;
}

VOID AcpiThermalStop(PPDO_DEVICE_DATA DeviceData)
{
    LIST_ENTRY RequestList;
    PACPI_THERMAL_REQUEST_ENTRY Entry;
    PIRP Irp;

    if (!DeviceData)
        return;

    InterlockedExchange(&DeviceData->ThermalStopping, TRUE);
    if (DeviceData->ThermalNotificationRegistered)
    {
        AcpiInterfaceNotificationsUnregister(DeviceData->Common.Self, AcpiThermalNotify);
        DeviceData->ThermalNotificationRegistered = FALSE;
    }
    ExWaitForRundownProtectionRelease(&DeviceData->ThermalWorkRundown);

    while ((Irp = IoCsqRemoveNextIrp(&DeviceData->ThermalCsq, NULL)) != NULL)
    {
        Irp->IoStatus.Status = STATUS_CANCELLED;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    InitializeListHead(&RequestList);
    ExAcquireFastMutex(&DeviceData->ThermalRequestMutex);
    while (!IsListEmpty(&DeviceData->ThermalRequestList))
        InsertTailList(&RequestList, RemoveHeadList(&DeviceData->ThermalRequestList));
    ExReleaseFastMutex(&DeviceData->ThermalRequestMutex);

    while (!IsListEmpty(&RequestList))
    {
        Entry = CONTAINING_RECORD(RemoveHeadList(&RequestList), ACPI_THERMAL_REQUEST_ENTRY, Link);
        ExWaitForRundownProtectionRelease(&Entry->Rundown);
        AcpiThermalDestroyRequest(Entry);
    }
}

NTSTATUS AcpiThermalDeviceControl(PPDO_DEVICE_DATA DeviceData, PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    ULONG InputLength;
    ULONG OutputLength;
    PUCHAR Input;

    if (!DeviceData || !Irp || !AcpiThermalIsZone(DeviceData) || !DeviceData->AcpiHandle)
        return STATUS_INVALID_DEVICE_REQUEST;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    Input = Irp->AssociatedIrp.SystemBuffer;

    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_THERMAL_QUERY_INFORMATION:
            if (InputLength < sizeof(ULONG) || !Input)
                return STATUS_BUFFER_TOO_SMALL;
            if (OutputLength < sizeof(THERMAL_INFORMATION))
                return STATUS_BUFFER_TOO_SMALL;
            if (*(PULONG)Input != (ULONG)InterlockedCompareExchange(&DeviceData->ThermalStamp, 0, 0))
                return AcpiThermalCompleteQuery(DeviceData, Irp);
            IoMarkIrpPending(Irp);
            IoCsqInsertIrp(&DeviceData->ThermalCsq, Irp, NULL);
            return STATUS_PENDING;

        case IOCTL_THERMAL_SET_COOLING_POLICY:
            if (InputLength < sizeof(UCHAR) || !Input)
                return STATUS_BUFFER_TOO_SMALL;
            if (*Input != ACTIVE_COOLING && *Input != PASSIVE_COOLING)
                return STATUS_INVALID_PARAMETER;
            return AcpiThermalSetCoolingPolicy(DeviceData, *Input);

        case IOCTL_RUN_ACTIVE_COOLING_METHOD:
            if (InputLength < sizeof(UCHAR) || !Input)
                return STATUS_BUFFER_TOO_SMALL;
            if (*Input > MAX_ACTIVE_COOLING_LEVELS)
                return STATUS_INVALID_PARAMETER;
            return AcpiThermalSetActiveLevel(DeviceData, *Input);

        case IOCTL_THERMAL_SET_PASSIVE_LIMIT:
            if (InputLength < sizeof(UCHAR) || !Input)
                return STATUS_BUFFER_TOO_SMALL;
            if (*Input > 100)
                return STATUS_INVALID_PARAMETER;
            return AcpiThermalSetPassiveLimit(DeviceData, *Input);

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}
