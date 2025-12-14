/*
 * PROJECT:     ReactOS ACPI Processor Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ACPI processor throttling helpers
 * COPYRIGHT:   2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "acpiproc.h"

#define NDEBUG
#include <debug.h>

#ifndef NonPagedPoolNx
#define NonPagedPoolNx NonPagedPool
#endif

#define ACPIPROC_TSS_FIELD_COUNT    5

typedef struct _ACPIPROC_TPC_WORKITEM_CONTEXT {
    PACPIPROC_DEVICE DeviceExtension;
    PIO_WORKITEM WorkItem;
} ACPIPROC_TPC_WORKITEM_CONTEXT, *PACPIPROC_TPC_WORKITEM_CONTEXT;

typedef struct _ACPIPROC_TPC_QUEUE_ENTRY {
    PACPIPROC_DEVICE DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
} ACPIPROC_TPC_QUEUE_ENTRY, *PACPIPROC_TPC_QUEUE_ENTRY;

static
VOID
NTAPI
AcpiprocTpcWorkItemRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context);

static
NTSTATUS
AcpiprocQueueTpcRefresh(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

static
VOID
AcpiprocQueueDomainPeersForTpc(
    _Inout_ PACPIPROC_DEVICE SourceDevice);

static
VOID
AcpiprocApplyTpcLimit(
    _Inout_ PACPIPROC_DEVICE DeviceExtension);

static
NTSTATUS
AcpiprocCapturePtc(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer = NULL;
    ULONG OutputLength = 0;
    NTSTATUS Status;
    PACPI_METHOD_ARGUMENT Argument;

    Status = AcpiprocExecuteMethod(DeviceExtension, "_PTC", &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!OutputBuffer || OutputBuffer->Count < 2)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    Argument = OutputBuffer->Argument;
    if (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    Status = AcpiprocParseGenericRegisterDescriptor(Argument->Data,
                                                    Argument->DataLength,
                                                    &DeviceExtension->Throttle.ControlRegister);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return Status;
    }
    DeviceExtension->Throttle.ControlRegisterValid = TRUE;

    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    if (Argument->Type != ACPI_METHOD_ARGUMENT_BUFFER)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    Status = AcpiprocParseGenericRegisterDescriptor(Argument->Data,
                                                    Argument->DataLength,
                                                    &DeviceExtension->Throttle.StatusRegister);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return Status;
    }
    DeviceExtension->Throttle.StatusRegisterValid = TRUE;

    ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiprocCaptureTss(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer = NULL;
    ULONG OutputLength = 0;
    NTSTATUS Status;
    PACPI_METHOD_ARGUMENT Argument;
    PACPIPROC_TSS_ENTRY States = NULL;
    SIZE_T AllocationSize;
    ULONG StateCount;

    Status = AcpiprocExecuteMethod(DeviceExtension, "_TSS", &OutputBuffer, &OutputLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!OutputBuffer || OutputBuffer->Count == 0)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    StateCount = OutputBuffer->Count;

    Status = RtlSizeTMult(StateCount,
                          sizeof(ACPIPROC_TSS_ENTRY),
                          &AllocationSize);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    States = ExAllocatePoolWithTag(NonPagedPoolNx, AllocationSize, ACPIPROC_TAG);
    if (!States)
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(States, AllocationSize);

    Argument = OutputBuffer->Argument;
    for (ULONG Index = 0; Index < StateCount; ++Index)
    {
        ULONG Values[ACPIPROC_TSS_FIELD_COUNT];

        if (Argument->Type != ACPI_METHOD_ARGUMENT_PACKAGE)
        {
            Status = STATUS_ACPI_INVALID_DATA;
            break;
        }

        Status = AcpiprocCopyPackageIntegers(Argument,
                                             Values,
                                             ACPIPROC_TSS_FIELD_COUNT);
        if (!NT_SUCCESS(Status))
            break;

        States[Index].Power = Values[0];
        States[Index].Performance = Values[1];
        States[Index].TransitionLatency = Values[2];
        States[Index].Control = Values[3];
        States[Index].Status = Values[4];

        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }

    ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(States, ACPIPROC_TAG);
        return Status;
    }

    DeviceExtension->Throttle.States = States;
    DeviceExtension->Throttle.StateCount = StateCount;
    return STATUS_SUCCESS;
}

NTSTATUS
AcpiprocRefreshTpcLimit(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _Out_opt_ PBOOLEAN LimitChanged)
{
    ULONG Value;
    NTSTATUS Status;
    BOOLEAN Changed = FALSE;

    Status = AcpiprocEvaluateIntegerMethod(DeviceExtension, "_TPC", &Value);
    if (!NT_SUCCESS(Status))
    {
        DeviceExtension->Throttle.TpcValid = FALSE;
        DeviceExtension->Throttle.TpcLimit = 0;
        if (LimitChanged)
            *LimitChanged = FALSE;
        return Status;
    }

    if (!DeviceExtension->Throttle.TpcValid ||
        DeviceExtension->Throttle.TpcLimit != Value)
    {
        Changed = TRUE;
    }

    DeviceExtension->Throttle.TpcLimit = Value;
    DeviceExtension->Throttle.TpcValid = TRUE;

    if (LimitChanged)
        *LimitChanged = Changed;
    return STATUS_SUCCESS;
}

static
ULONG
AcpiprocResolveMinimumThrottleIndex(
    _In_ PACPIPROC_DEVICE DeviceExtension)
{
    ULONG Limit;

    if (!DeviceExtension->Throttle.StateCount)
        return 0;

    if (!DeviceExtension->Throttle.TpcValid)
        return 0;

    Limit = DeviceExtension->Throttle.TpcLimit;
    if (Limit >= DeviceExtension->Throttle.StateCount)
    {
        Limit = DeviceExtension->Throttle.StateCount - 1;
    }

    return Limit;
}

static
VOID
AcpiprocApplyTpcLimit(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    ULONG MinimumIndex;
    NTSTATUS Status;

    if (!DeviceExtension->Throttle.ControlRegisterValid ||
        DeviceExtension->Throttle.StateCount == 0 ||
        !DeviceExtension->Throttle.States ||
        !DeviceExtension->Throttle.TpcValid)
    {
        return;
    }

    MinimumIndex = AcpiprocResolveMinimumThrottleIndex(DeviceExtension);

    if (DeviceExtension->Throttle.CurrentStateValid &&
        DeviceExtension->Throttle.CurrentStateIndex >= MinimumIndex)
    {
        return;
    }

    Status = AcpiprocSetThrottleIndex(DeviceExtension, MinimumIndex);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: Failed to clamp processor %lu to throttle state %lu (status 0x%lx)\n",
                DeviceExtension->ProcessorIndex,
                MinimumIndex,
                Status);
    }
}

NTSTATUS
AcpiprocSetThrottleIndex(
    _Inout_ PACPIPROC_DEVICE DeviceExtension,
    _In_ ULONG StateIndex)
{
    ULONG MaximumIndex;
    ULONG MinimumIndex;
    PACPIPROC_TSS_ENTRY States;
    NTSTATUS Status;

    States = DeviceExtension->Throttle.States;
    if (!States || DeviceExtension->Throttle.StateCount == 0)
        return STATUS_NOT_SUPPORTED;

    if (!DeviceExtension->Throttle.ControlRegisterValid)
        return STATUS_NOT_SUPPORTED;

    MaximumIndex = DeviceExtension->Throttle.StateCount - 1;
    if (StateIndex > MaximumIndex)
        return STATUS_INVALID_PARAMETER;

    MinimumIndex = AcpiprocResolveMinimumThrottleIndex(DeviceExtension);
    if (StateIndex < MinimumIndex)
        return STATUS_INVALID_PARAMETER;

    Status = AcpiprocWriteRegister(&DeviceExtension->Throttle.ControlRegister,
                                   AcpiprocRegisterKindControl,
                                   States[StateIndex].Control);
    if (!NT_SUCCESS(Status))
        return Status;

    DeviceExtension->Throttle.CurrentStateValid = TRUE;
    DeviceExtension->Throttle.CurrentStateIndex = StateIndex;
    return STATUS_SUCCESS;
}

NTSTATUS
AcpiprocInitializeThrottleStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    NTSTATUS Status;

    AcpiprocCleanupThrottleStates(DeviceExtension);

    Status = AcpiprocCapturePtc(DeviceExtension);
    if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        DPRINT1("acpiproc: _PTC evaluation failed (status 0x%lx)\n", Status);
        return Status;
    }

    Status = AcpiprocCaptureTss(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
            DPRINT1("acpiproc: _TSS evaluation failed (status 0x%lx)\n", Status);

        /* Lack of _TSS simply disables throttling support */
        return STATUS_SUCCESS;
    }

    Status = AcpiprocRefreshTpcLimit(DeviceExtension, NULL);
    if (!NT_SUCCESS(Status) && Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        DPRINT1("acpiproc: _TPC evaluation failed (status 0x%lx)\n", Status);
    }

    return STATUS_SUCCESS;
}

VOID
AcpiprocCleanupThrottleStates(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    if (DeviceExtension->Throttle.States)
    {
        ExFreePoolWithTag(DeviceExtension->Throttle.States, ACPIPROC_TAG);
        DeviceExtension->Throttle.States = NULL;
    }

    DeviceExtension->Throttle.StateCount = 0;
    DeviceExtension->Throttle.ControlRegisterValid = FALSE;
    DeviceExtension->Throttle.StatusRegisterValid = FALSE;
    DeviceExtension->Throttle.CurrentStateValid = FALSE;
    DeviceExtension->Throttle.CurrentStateIndex = 0;
    DeviceExtension->Throttle.TpcValid = FALSE;
    DeviceExtension->Throttle.TpcLimit = 0;
    InterlockedExchange(&DeviceExtension->Throttle.TpcWorkItemQueued, 0);
}

static
NTSTATUS
AcpiprocQueueTpcRefresh(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    PACPIPROC_TPC_WORKITEM_CONTEXT Context;
    NTSTATUS Status;

    if (!DeviceExtension || !DeviceExtension->Self)
        return STATUS_INVALID_DEVICE_STATE;

    if (InterlockedCompareExchange(&DeviceExtension->Throttle.TpcWorkItemQueued, 1, 0) != 0)
        return STATUS_SUCCESS;

    Context = ExAllocatePoolWithTag(NonPagedPoolNx,
                                    sizeof(*Context),
                                    ACPIPROC_TAG);
    if (!Context)
    {
        InterlockedExchange(&DeviceExtension->Throttle.TpcWorkItemQueued, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Context->WorkItem = IoAllocateWorkItem(DeviceExtension->Self);
    if (!Context->WorkItem)
    {
        ExFreePoolWithTag(Context, ACPIPROC_TAG);
        InterlockedExchange(&DeviceExtension->Throttle.TpcWorkItemQueued, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Context->DeviceExtension = DeviceExtension;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Context);
    if (!NT_SUCCESS(Status))
    {
        IoFreeWorkItem(Context->WorkItem);
        ExFreePoolWithTag(Context, ACPIPROC_TAG);
        InterlockedExchange(&DeviceExtension->Throttle.TpcWorkItemQueued, 0);
        return Status;
    }

    IoQueueWorkItem(Context->WorkItem,
                    AcpiprocTpcWorkItemRoutine,
                    DelayedWorkQueue,
                    Context);

    return STATUS_SUCCESS;
}

static
VOID
NTAPI
AcpiprocTpcWorkItemRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PVOID Context)
{
    PACPIPROC_TPC_WORKITEM_CONTEXT WorkContext = (PACPIPROC_TPC_WORKITEM_CONTEXT)Context;
    PACPIPROC_DEVICE DeviceExtension;
    BOOLEAN LimitChanged = FALSE;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (!WorkContext)
        return;

    DeviceExtension = WorkContext->DeviceExtension;

    Status = AcpiprocRefreshTpcLimit(DeviceExtension, &LimitChanged);
    if (NT_SUCCESS(Status))
    {
        if (LimitChanged)
        {
            AcpiprocApplyTpcLimit(DeviceExtension);
            AcpiprocQueueDomainPeersForTpc(DeviceExtension);
        }
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        DPRINT1("acpiproc: _TPC refresh failed (status 0x%lx)\n", Status);
    }

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, WorkContext);
    IoFreeWorkItem(WorkContext->WorkItem);
    ExFreePoolWithTag(WorkContext, ACPIPROC_TAG);

    InterlockedExchange(&DeviceExtension->Throttle.TpcWorkItemQueued, 0);
}

static
VOID
AcpiprocQueueDomainPeersForTpc(
    _Inout_ PACPIPROC_DEVICE SourceDevice)
{
    ACPIPROC_TPC_QUEUE_ENTRY StackEntries[4];
    PACPIPROC_TPC_QUEUE_ENTRY Entries = StackEntries;
    ULONG Capacity = RTL_NUMBER_OF(StackEntries);
    ULONG Count = 0;
    BOOLEAN Allocated = FALSE;
    PLIST_ENTRY Link;

    if (!SourceDevice->Perf.Psd.Valid)
        return;

    ExAcquireFastMutex(&AcpiprocDeviceListLock);
    for (Link = AcpiprocDeviceList.Flink;
         Link != &AcpiprocDeviceList;
         Link = Link->Flink)
    {
        PACPIPROC_DEVICE Peer = CONTAINING_RECORD(Link, ACPIPROC_DEVICE, ListEntry);

        if (Peer == SourceDevice)
            continue;

        if (!Peer->Started)
            continue;

        if (!Peer->Perf.Psd.Valid)
            continue;

        if (Peer->Perf.Psd.Domain != SourceDevice->Perf.Psd.Domain)
            continue;

        if (!Peer->Throttle.States || Peer->Throttle.StateCount == 0)
            continue;

        if (!Peer->Self)
            continue;

        if (Count == Capacity)
        {
            ULONG NewCapacity = Capacity * 2;
            PACPIPROC_TPC_QUEUE_ENTRY NewEntries;

            NewEntries = ExAllocatePoolWithTag(PagedPool,
                                               NewCapacity * sizeof(*NewEntries),
                                               ACPIPROC_TAG);
            if (!NewEntries)
                break;

            RtlCopyMemory(NewEntries,
                          Entries,
                          Count * sizeof(*NewEntries));

            if (Allocated)
            {
                ExFreePoolWithTag(Entries, ACPIPROC_TAG);
            }

            Entries = NewEntries;
            Capacity = NewCapacity;
            Allocated = TRUE;
        }

        ObReferenceObject(Peer->Self);
        Entries[Count].DeviceExtension = Peer;
        Entries[Count].DeviceObject = Peer->Self;
        ++Count;
    }
    ExReleaseFastMutex(&AcpiprocDeviceListLock);

    for (ULONG Index = 0; Index < Count; ++Index)
    {
        NTSTATUS Status = AcpiprocQueueTpcRefresh(Entries[Index].DeviceExtension);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("acpiproc: Failed to queue domain _TPC refresh (status 0x%lx)\n",
                    Status);
        }

        ObDereferenceObject(Entries[Index].DeviceObject);
    }

    if (Allocated)
    {
        ExFreePoolWithTag(Entries, ACPIPROC_TAG);
    }
}

VOID
AcpiprocHandleTpcNotification(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    NTSTATUS Status;

    if (!DeviceExtension)
        return;

    Status = AcpiprocQueueTpcRefresh(DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("acpiproc: Failed to schedule _TPC refresh (status 0x%lx)\n",
                Status);
    }
}
