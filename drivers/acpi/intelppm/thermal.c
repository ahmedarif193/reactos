/*
 * PROJECT:     ReactOS ACPI Processor Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Thermal trip handling and notifications
 * COPYRIGHT:   2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "intelppm.h"

#define NDEBUG
#include <debug.h>

#ifndef NonPagedPoolNx
#define NonPagedPoolNx NonPagedPool
#endif

typedef struct _ACPIPROC_THERMAL_WORKITEM_CONTEXT {
    PACPIPROC_DEVICE DeviceExtension;
    ACPIPROC_THERMAL_EVENT EventType;
    PIO_WORKITEM WorkItem;
} ACPIPROC_THERMAL_WORKITEM_CONTEXT, *PACPIPROC_THERMAL_WORKITEM_CONTEXT;

static
NTSTATUS
AcpiprocRefreshThermalTripPoints(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    NTSTATUS StatusHot;
    NTSTATUS StatusCritical;
    ULONG Value;

    DeviceExtension->Thermal.HotTripValid = FALSE;
    DeviceExtension->Thermal.CriticalTripValid = FALSE;
    DeviceExtension->Thermal.HotTripPoint = 0;
    DeviceExtension->Thermal.CriticalTripPoint = 0;

    StatusHot = AcpiprocEvaluateIntegerMethod(DeviceExtension, "_HOT", &Value);
    if (NT_SUCCESS(StatusHot))
    {
        DeviceExtension->Thermal.HotTripPoint = Value;
        DeviceExtension->Thermal.HotTripValid = TRUE;
    }
    else if (StatusHot == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        StatusHot = STATUS_SUCCESS;
    }
    else
    {
        DPRINT1("acpiproc: _HOT evaluation failed (status 0x%lx)\n", StatusHot);
    }

    StatusCritical = AcpiprocEvaluateIntegerMethod(DeviceExtension, "_CRT", &Value);
    if (NT_SUCCESS(StatusCritical))
    {
        DeviceExtension->Thermal.CriticalTripPoint = Value;
        DeviceExtension->Thermal.CriticalTripValid = TRUE;
    }
    else if (StatusCritical == STATUS_OBJECT_NAME_NOT_FOUND)
    {
        StatusCritical = STATUS_SUCCESS;
    }
    else
    {
        DPRINT1("acpiproc: _CRT evaluation failed (status 0x%lx)\n", StatusCritical);
    }

    if (!NT_SUCCESS(StatusHot))
        return StatusHot;

    if (!NT_SUCCESS(StatusCritical))
        return StatusCritical;

    return STATUS_SUCCESS;
}

static
VOID
NTAPI
AcpiprocThermalWorkItemRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PACPIPROC_THERMAL_WORKITEM_CONTEXT WorkContext = (PACPIPROC_THERMAL_WORKITEM_CONTEXT)Context;
    PACPIPROC_DEVICE DeviceExtension;
    ULONG TripPoint = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (!WorkContext)
        return;

    DeviceExtension = WorkContext->DeviceExtension;

    AcpiprocRefreshThermalTripPoints(DeviceExtension);

    switch (WorkContext->EventType)
    {
        case AcpiprocThermalEventHot:
            DeviceExtension->Thermal.HotEventPending = TRUE;
            if (DeviceExtension->Thermal.HotTripValid)
                TripPoint = DeviceExtension->Thermal.HotTripPoint;
            PoNotifyProcessorThermalEvent(DeviceExtension->ProcessorIndex,
                                          PoThermalEventHot,
                                          TripPoint);
            break;

        case AcpiprocThermalEventCritical:
            DeviceExtension->Thermal.CriticalEventPending = TRUE;
            if (DeviceExtension->Thermal.CriticalTripValid)
                TripPoint = DeviceExtension->Thermal.CriticalTripPoint;
            PoNotifyProcessorThermalEvent(DeviceExtension->ProcessorIndex,
                                          PoThermalEventCritical,
                                          TripPoint);
            break;

        default:
            break;
    }

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, WorkContext);
    IoFreeWorkItem(WorkContext->WorkItem);
    ExFreePoolWithTag(WorkContext, ACPIPROC_TAG);

    InterlockedExchange(&DeviceExtension->Thermal.WorkItemQueued, 0);
}

NTSTATUS
AcpiprocInitializeThermalInfo(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    NTSTATUS Status;

    AcpiprocCleanupThermalInfo(DeviceExtension);

    Status = AcpiprocRefreshThermalTripPoints(DeviceExtension);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED)
    {
        return Status;
    }

    return STATUS_SUCCESS;
}

VOID
AcpiprocCleanupThermalInfo(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    DeviceExtension->Thermal.HotTripValid = FALSE;
    DeviceExtension->Thermal.CriticalTripValid = FALSE;
    DeviceExtension->Thermal.HotTripPoint = 0;
    DeviceExtension->Thermal.CriticalTripPoint = 0;
    DeviceExtension->Thermal.HotEventPending = FALSE;
    DeviceExtension->Thermal.CriticalEventPending = FALSE;
    InterlockedExchange(&DeviceExtension->Thermal.WorkItemQueued, 0);
}

VOID
AcpiprocHandleThermalNotification(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _In_ ACPIPROC_THERMAL_EVENT EventType)
{
    PACPIPROC_THERMAL_WORKITEM_CONTEXT Context;
    NTSTATUS Status;

    if (!DeviceExtension || !DeviceExtension->Self)
        return;

    if (InterlockedCompareExchange(&DeviceExtension->Thermal.WorkItemQueued, 1, 0) != 0)
        return;

    Context = ExAllocatePoolWithTag(NonPagedPoolNx,
                                    sizeof(*Context),
                                    ACPIPROC_TAG);
    if (!Context)
    {
        InterlockedExchange(&DeviceExtension->Thermal.WorkItemQueued, 0);
        return;
    }

    Context->WorkItem = IoAllocateWorkItem(DeviceExtension->Self);
    if (!Context->WorkItem)
    {
        ExFreePoolWithTag(Context, ACPIPROC_TAG);
        InterlockedExchange(&DeviceExtension->Thermal.WorkItemQueued, 0);
        return;
    }

    Context->DeviceExtension = DeviceExtension;
    Context->EventType = EventType;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Context);
    if (!NT_SUCCESS(Status))
    {
        IoFreeWorkItem(Context->WorkItem);
        ExFreePoolWithTag(Context, ACPIPROC_TAG);
        InterlockedExchange(&DeviceExtension->Thermal.WorkItemQueued, 0);
        return;
    }

    IoQueueWorkItem(Context->WorkItem,
                    AcpiprocThermalWorkItemRoutine,
                    DelayedWorkQueue,
                    Context);
}
