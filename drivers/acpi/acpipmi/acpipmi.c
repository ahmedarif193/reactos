/*
 * PROJECT:     ReactOS ACPI Power Meter Driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Power Meter Interface for ACPI000D devices
 */

#include <ntddk.h>
#include <initguid.h>
#include <wdmguid.h>
#include <acpiioct.h>
#include <pmi.h>

#define NDEBUG
#include <debug.h>

#define ACPIPMI_TAG 'iMPA'
#define ACPIPMI_MAX_OUTPUT 4096
#define ACPIPMI_READ_ATTEMPTS 5
#define ACPIPMI_READ_RETRY_DELAY_100NS (-10 * 1000)
#define ACPIPMI_METHOD(a, b, c, d) ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))

typedef struct _ACPIPMI_DEVICE_EXTENSION ACPIPMI_DEVICE_EXTENSION, *PACPIPMI_DEVICE_EXTENSION;

typedef struct _ACPIPMI_WORK_CONTEXT
{
    WORK_QUEUE_ITEM WorkItem;
    PACPIPMI_DEVICE_EXTENSION DeviceExtension;
    ULONG NotifyCode;
} ACPIPMI_WORK_CONTEXT, *PACPIPMI_WORK_CONTEXT;

typedef struct _ACPIPMI_FILE_CONTEXT
{
    LIST_ENTRY DeviceLink;
    IO_CSQ EventCsq;
    LIST_ENTRY PendingEvents;
    KSPIN_LOCK PendingEventLock;
    KSPIN_LOCK EventStateLock;
    ULONG PendingEventMask;
    PACPIPMI_DEVICE_EXTENSION DeviceExtension;
    BOOLEAN Linked;
    BOOLEAN RemoveLockHeld;
} ACPIPMI_FILE_CONTEXT, *PACPIPMI_FILE_CONTEXT;

struct _ACPIPMI_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    KMUTEX MethodMutex;
    FAST_MUTEX FileListMutex;
    LIST_ENTRY FileList;
    UNICODE_STRING InterfaceName;
    ACPI_INTERFACE_STANDARD AcpiInterface;
    KEVENT WorkIdleEvent;
    volatile LONG WorkCount;
    BOOLEAN InterfaceRegistered;
    BOOLEAN InterfaceEnabled;
    BOOLEAN InterfaceAcquired;
    BOOLEAN NotificationsRegistered;
    volatile LONG Started;
    volatile LONG Removing;
    PMI_REPORTED_CAPABILITIES ReportedCapabilities;
};

static NTSTATUS AcpiPmiComplete(PIRP Irp, NTSTATUS Status, ULONG_PTR Information);

static
VOID
NTAPI
AcpiPmiCsqInsertIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    PACPIPMI_FILE_CONTEXT FileContext = CONTAINING_RECORD(Csq, ACPIPMI_FILE_CONTEXT, EventCsq);

    InsertTailList(&FileContext->PendingEvents, &Irp->Tail.Overlay.ListEntry);
}

static
VOID
NTAPI
AcpiPmiCsqRemoveIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}

static
PIRP
NTAPI
AcpiPmiCsqPeekNextIrp(
    _In_ PIO_CSQ Csq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PACPIPMI_FILE_CONTEXT FileContext = CONTAINING_RECORD(Csq, ACPIPMI_FILE_CONTEXT, EventCsq);
    PLIST_ENTRY Entry;

    UNREFERENCED_PARAMETER(PeekContext);
    Entry = Irp ? Irp->Tail.Overlay.ListEntry.Flink : FileContext->PendingEvents.Flink;
    if (Entry == &FileContext->PendingEvents)
        return NULL;
    return CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
}

static
VOID
NTAPI
AcpiPmiCsqAcquireLock(
    _In_ PIO_CSQ Csq,
    _Out_ PKIRQL Irql)
{
    PACPIPMI_FILE_CONTEXT FileContext = CONTAINING_RECORD(Csq, ACPIPMI_FILE_CONTEXT, EventCsq);

    KeAcquireSpinLock(&FileContext->PendingEventLock, Irql);
}

static
VOID
NTAPI
AcpiPmiCsqReleaseLock(
    _In_ PIO_CSQ Csq,
    _In_ KIRQL Irql)
{
    PACPIPMI_FILE_CONTEXT FileContext = CONTAINING_RECORD(Csq, ACPIPMI_FILE_CONTEXT, EventCsq);

    KeReleaseSpinLock(&FileContext->PendingEventLock, Irql);
}

static
VOID
NTAPI
AcpiPmiCsqCompleteCanceledIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    PACPIPMI_FILE_CONTEXT FileContext = CONTAINING_RECORD(Csq, ACPIPMI_FILE_CONTEXT, EventCsq);

    IoReleaseRemoveLock(&FileContext->DeviceExtension->RemoveLock, Irp);
    AcpiPmiComplete(Irp, STATUS_CANCELLED, 0);
}

static
VOID
AcpiPmiDrainFileEvents(
    _In_ PACPIPMI_FILE_CONTEXT FileContext,
    _In_ NTSTATUS Status)
{
    LIST_ENTRY RemovedEvents;
    PIRP Irp;
    KIRQL OldIrql;

    InitializeListHead(&RemovedEvents);
    KeAcquireSpinLock(&FileContext->EventStateLock, &OldIrql);
    FileContext->PendingEventMask = 0;
    while ((Irp = IoCsqRemoveNextIrp(&FileContext->EventCsq, NULL)) != NULL)
        InsertTailList(&RemovedEvents, &Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&FileContext->EventStateLock, OldIrql);

    while (!IsListEmpty(&RemovedEvents))
    {
        Irp = CONTAINING_RECORD(RemoveHeadList(&RemovedEvents),
                                IRP,
                                Tail.Overlay.ListEntry);
        IoReleaseRemoveLock(&FileContext->DeviceExtension->RemoveLock, Irp);
        AcpiPmiComplete(Irp, Status, 0);
    }
}

static
BOOLEAN
AcpiPmiTakePendingEventLocked(
    _In_ PACPIPMI_FILE_CONTEXT FileContext,
    _Out_ PMI_EVENT_TYPE *EventType)
{
    ULONG Index;

    for (Index = 0; Index < PmiEventMax; Index++)
    {
        if (FileContext->PendingEventMask & (1UL << Index))
        {
            FileContext->PendingEventMask &= ~(1UL << Index);
            *EventType = (PMI_EVENT_TYPE)Index;
            return TRUE;
        }
    }
    return FALSE;
}

static
VOID
AcpiPmiDeliverEventToFile(
    _In_ PACPIPMI_FILE_CONTEXT FileContext,
    _In_ PMI_EVENT_TYPE EventType)
{
    PACPIPMI_DEVICE_EXTENSION DeviceExtension =
        FileContext->DeviceExtension;
    PIRP Irp;
    KIRQL OldIrql;

    KeAcquireSpinLock(&FileContext->EventStateLock, &OldIrql);
    Irp = IoCsqRemoveNextIrp(&FileContext->EventCsq, NULL);
    if (Irp == NULL)
        FileContext->PendingEventMask |= 1UL << EventType;
    KeReleaseSpinLock(&FileContext->EventStateLock, OldIrql);

    if (Irp != NULL)
    {
        PPMI_EVENT Event = Irp->AssociatedIrp.SystemBuffer;

        Event->Version = PMI_VERSION;
        Event->EventType = EventType;
        IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
        AcpiPmiComplete(Irp, STATUS_SUCCESS, sizeof(*Event));
    }
}

static
VOID
AcpiPmiPublishEvent(
    _In_ PACPIPMI_DEVICE_EXTENSION DeviceExtension,
    _In_ PMI_EVENT_TYPE EventType)
{
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&DeviceExtension->FileListMutex);
    for (Entry = DeviceExtension->FileList.Flink;
         Entry != &DeviceExtension->FileList;
         Entry = Entry->Flink)
    {
        AcpiPmiDeliverEventToFile(
            CONTAINING_RECORD(Entry, ACPIPMI_FILE_CONTEXT, DeviceLink),
            EventType);
    }
    ExReleaseFastMutex(&DeviceExtension->FileListMutex);
}

static
VOID
AcpiPmiDrainAllEvents(
    _In_ PACPIPMI_DEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS Status)
{
    PLIST_ENTRY Entry;

    ExAcquireFastMutex(&DeviceExtension->FileListMutex);
    for (Entry = DeviceExtension->FileList.Flink; Entry != &DeviceExtension->FileList; Entry = Entry->Flink)
        AcpiPmiDrainFileEvents(CONTAINING_RECORD(Entry, ACPIPMI_FILE_CONTEXT, DeviceLink), Status);
    ExReleaseFastMutex(&DeviceExtension->FileListMutex);
}

static
NTSTATUS
NTAPI
AcpiPmiCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);
    KeSetEvent((PKEVENT)Context, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
AcpiPmiForwardSynchronously(
    _In_ PACPIPMI_DEVICE_EXTENSION DeviceExtension,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, AcpiPmiCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
AcpiPmiEvaluate(
    _In_ PACPIPMI_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _Outptr_ PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer)
{
    ACPI_EVAL_INPUT_BUFFER Input;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    *OutputBuffer = NULL;
    Output = ExAllocatePoolWithTag(PagedPool, ACPIPMI_MAX_OUTPUT, ACPIPMI_TAG);
    if (!Output)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(&Input, sizeof(Input));
    RtlZeroMemory(Output, ACPIPMI_MAX_OUTPUT);
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    Input.MethodNameAsUlong = MethodName;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD, DeviceExtension->LowerDevice, &Input, sizeof(Input), Output, ACPIPMI_MAX_OUTPUT, FALSE, &Event, &IoStatus);
    if (!Irp)
    {
        ExFreePoolWithTag(Output, ACPIPMI_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    if (!NT_SUCCESS(Status))
        goto Failure;
    if (IoStatus.Information < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + ACPI_METHOD_ARGUMENT_LENGTH(0) || IoStatus.Information > ACPIPMI_MAX_OUTPUT || Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Output->Length > IoStatus.Information || Output->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(&Output->Argument[0]) || Output->Count == 0)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Failure;
    }
    *OutputBuffer = Output;
    return STATUS_SUCCESS;

Failure:
    ExFreePoolWithTag(Output, ACPIPMI_TAG);
    return Status;
}

static
PACPI_METHOD_ARGUMENT
AcpiPmiOutputArgument(
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG Index)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Current;
    ULONG ArgumentLength;

    if (!Output ||
        Output->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ||
        Index >= Output->Count)
    {
        return NULL;
    }
    Argument = &Output->Argument[0];
    End = (PUCHAR)Output + Output->Length;
    for (Current = 0; Current <= Index; Current++)
    {
        if ((PUCHAR)Argument > End ||
            (ULONG)(End - (PUCHAR)Argument) < ACPI_METHOD_ARGUMENT_LENGTH(0))
        {
            return NULL;
        }
        ArgumentLength = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Argument);
        if (ArgumentLength > (ULONG)(End - (PUCHAR)Argument))
            return NULL;
        if (Current == Index)
            return Argument;
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
    return NULL;
}

static
PACPI_METHOD_ARGUMENT
AcpiPmiPackageArgument(
    _In_ PACPI_METHOD_ARGUMENT Package,
    _In_ ULONG Index)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Current;
    ULONG ArgumentLength;

    if (!Package || Package->Type != ACPI_METHOD_ARGUMENT_PACKAGE || Package->DataLength < ACPI_METHOD_ARGUMENT_LENGTH(0))
        return NULL;
    Argument = (PACPI_METHOD_ARGUMENT)Package->Data;
    End = Package->Data + Package->DataLength;
    for (Current = 0; Current <= Index; Current++)
    {
        if ((PUCHAR)Argument > End || (ULONG)(End - (PUCHAR)Argument) < ACPI_METHOD_ARGUMENT_LENGTH(0))
            return NULL;
        ArgumentLength = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Argument);
        if (ArgumentLength > (ULONG)(End - (PUCHAR)Argument))
            return NULL;
        if (Current == Index)
            return Argument;
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
    return NULL;
}

static
PACPI_METHOD_ARGUMENT
AcpiPmiResultArgument(
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG Index)
{
    if (Output->Count == 1 &&
        Output->Argument[0].Type == ACPI_METHOD_ARGUMENT_PACKAGE)
    {
        return AcpiPmiPackageArgument(&Output->Argument[0], Index);
    }
    return AcpiPmiOutputArgument(Output, Index);
}

static
BOOLEAN
AcpiPmiResultInteger(
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG Index,
    _Out_ PULONG Value)
{
    PACPI_METHOD_ARGUMENT Argument = AcpiPmiResultArgument(Output, Index);

    if (!Argument || Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER)
        return FALSE;
    *Value = Argument->Argument;
    return TRUE;
}

static
BOOLEAN
AcpiPmiResultString(
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG Index,
    _Out_writes_(DestinationCount) PWCHAR Destination,
    _In_ SIZE_T DestinationCount)
{
    PACPI_METHOD_ARGUMENT Argument = AcpiPmiResultArgument(Output, Index);
    SIZE_T Character;

    if (!Argument || Argument->Type != ACPI_METHOD_ARGUMENT_STRING || !DestinationCount)
        return FALSE;
    for (Character = 0; Character + 1 < DestinationCount && Character < Argument->DataLength && Argument->Data[Character] != ANSI_NULL; Character++)
        Destination[Character] = Argument->Data[Character];
    Destination[Character] = UNICODE_NULL;
    return TRUE;
}

static
NTSTATUS
AcpiPmiReadCapabilities(
    _Inout_ PACPIPMI_DEVICE_EXTENSION DeviceExtension)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PPMI_REPORTED_CAPABILITIES Capabilities = &DeviceExtension->ReportedCapabilities;
    ULONG Values[11];
    ULONG Index;
    NTSTATUS Status;

    Status = AcpiPmiEvaluate(DeviceExtension, ACPIPMI_METHOD('_', 'P', 'M', 'C'), &Output);
    if (!NT_SUCCESS(Status))
        return Status;
    for (Index = 0; Index < RTL_NUMBER_OF(Values); Index++)
    {
        if (!AcpiPmiResultInteger(Output, Index, &Values[Index]))
        {
            Status = STATUS_ACPI_INVALID_DATA;
            goto Done;
        }
    }
    if (Values[1] >= PmiMeasurementUnitMax || Values[2] >= PmiMeasurementTypeMax || Values[8] > TRUE || !(Values[0] & PMI_CAPABILITIES_SUPPORT_MEASUREMENT))
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Done;
    }
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Flags = Values[0];
    Capabilities->MeasurementUnit = Values[1];
    Capabilities->MeasurementType = Values[2];
    Capabilities->Accuracy = Values[3];
    Capabilities->SamplingPeriod = Values[4];
    Capabilities->MinimumAverageInterval = Values[5];
    Capabilities->MaximumAverageInterval = Values[6];
    Capabilities->Hysteresis = Values[7];
    Capabilities->Writeable = (BOOLEAN)Values[8];
    Capabilities->MinBudget = Values[9];
    Capabilities->MaxBudget = Values[10];
    if (!AcpiPmiResultString(Output, 11, Capabilities->ModelNumber, RTL_NUMBER_OF(Capabilities->ModelNumber)) ||
        !AcpiPmiResultString(Output, 12, Capabilities->SerialNumber, RTL_NUMBER_OF(Capabilities->SerialNumber)) ||
        !AcpiPmiResultString(Output, 13, Capabilities->OEMInformation, RTL_NUMBER_OF(Capabilities->OEMInformation)))
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Done;
    }
    Status = STATUS_SUCCESS;

Done:
    ExFreePoolWithTag(Output, ACPIPMI_TAG);
    return Status;
}

static
NTSTATUS
AcpiPmiReadMeasurement(
    _In_ PACPIPMI_DEVICE_EXTENSION DeviceExtension,
    _Out_ PULONG Milliwatts)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    LARGE_INTEGER RetryDelay;
    ULONG Attempt;
    NTSTATUS Status;

    RetryDelay.QuadPart = ACPIPMI_READ_RETRY_DELAY_100NS;
    for (Attempt = 0; Attempt < ACPIPMI_READ_ATTEMPTS; Attempt++)
    {
        Status = AcpiPmiEvaluate(DeviceExtension, ACPIPMI_METHOD('_', 'P', 'M', 'M'), &Output);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Output->Count != 1 || Output->Argument[0].Type != ACPI_METHOD_ARGUMENT_INTEGER || Output->Argument[0].Argument == MAXULONG)
            Status = STATUS_DEVICE_DATA_ERROR;
        else
        {
            *Milliwatts = Output->Argument[0].Argument;
            Status = STATUS_SUCCESS;
        }
        ExFreePoolWithTag(Output, ACPIPMI_TAG);
        if (Status != STATUS_DEVICE_DATA_ERROR || Attempt + 1 == ACPIPMI_READ_ATTEMPTS)
            break;
        KeDelayExecutionThread(KernelMode, FALSE, &RetryDelay);
    }
    if (NT_SUCCESS(Status) && Attempt != 0)
        DPRINT1("ACPIPMI: recovered transient firmware measurement after %lu attempts\n", Attempt + 1);
    return Status;
}

static
PMI_EVENT_TYPE
AcpiPmiEventTypeFromNotify(
    _In_ ULONG NotifyCode)
{
    switch (NotifyCode)
    {
        case 0x80:
            return PmiCapabilitiesChangedEvent;
        case 0x81:
            return PmiThresholdEvent;
        case 0x82:
            return PmiConfigurationChangedEvent;
        case 0x83:
            return PmiBudgetEvent;
        case 0x84:
            return PmiAveragingIntervalChangedEvent;
        default:
            return PmiEventMax;
    }
}

static
VOID
NTAPI
AcpiPmiNotificationWorker(
    _In_ PVOID Context)
{
    PACPIPMI_WORK_CONTEXT WorkContext = Context;
    PACPIPMI_DEVICE_EXTENSION DeviceExtension =
        WorkContext->DeviceExtension;
    PMI_EVENT_TYPE EventType;
    NTSTATUS Status = STATUS_SUCCESS;

    EventType = AcpiPmiEventTypeFromNotify(WorkContext->NotifyCode);
    if (EventType == PmiCapabilitiesChangedEvent)
    {
        KeWaitForSingleObject(&DeviceExtension->MethodMutex,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        if (DeviceExtension->Started && !DeviceExtension->Removing)
            Status = AcpiPmiReadCapabilities(DeviceExtension);
        else
            Status = STATUS_DEVICE_NOT_READY;
        KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
    }

    if (NT_SUCCESS(Status) && DeviceExtension->Started &&
        !DeviceExtension->Removing && EventType < PmiEventMax)
    {
        AcpiPmiPublishEvent(DeviceExtension, EventType);
    }

    if (InterlockedDecrement(&DeviceExtension->WorkCount) == 0)
        KeSetEvent(&DeviceExtension->WorkIdleEvent, IO_NO_INCREMENT, FALSE);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, WorkContext);
    ExFreePoolWithTag(WorkContext, ACPIPMI_TAG);
}

static
VOID
NTAPI
AcpiPmiNotification(
    _In_ PVOID Context,
    _In_ ULONG NotifyCode)
{
    PACPIPMI_DEVICE_EXTENSION DeviceExtension = Context;
    PACPIPMI_WORK_CONTEXT WorkContext;
    PMI_EVENT_TYPE EventType;
    NTSTATUS Status;

    EventType = AcpiPmiEventTypeFromNotify(NotifyCode);
    if (EventType >= PmiEventMax || !DeviceExtension->Started ||
        DeviceExtension->Removing)
    {
        return;
    }

    WorkContext = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(*WorkContext),
                                        ACPIPMI_TAG);
    if (WorkContext == NULL)
        return;
    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, WorkContext);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(WorkContext, ACPIPMI_TAG);
        return;
    }

    WorkContext->DeviceExtension = DeviceExtension;
    WorkContext->NotifyCode = NotifyCode;
    ExInitializeWorkItem(&WorkContext->WorkItem,
                         AcpiPmiNotificationWorker,
                         WorkContext);
    if (InterlockedIncrement(&DeviceExtension->WorkCount) == 1)
        KeClearEvent(&DeviceExtension->WorkIdleEvent);
    ExQueueWorkItem(&WorkContext->WorkItem, DelayedWorkQueue);
}

static
NTSTATUS
AcpiPmiQueryAcpiInterface(
    _Inout_ PACPIPMI_DEVICE_EXTENSION DeviceExtension)
{
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(&DeviceExtension->AcpiInterface,
                  sizeof(DeviceExtension->AcpiInterface));
    DeviceExtension->AcpiInterface.Size =
        sizeof(DeviceExtension->AcpiInterface);
    DeviceExtension->AcpiInterface.Version = 1;
    Irp = IoAllocateIrp(DeviceExtension->LowerDevice->StackSize, FALSE);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType =
        &GUID_ACPI_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size =
        sizeof(DeviceExtension->AcpiInterface);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface =
        (PINTERFACE)&DeviceExtension->AcpiInterface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    IoSetCompletionRoutine(Irp,
                           AcpiPmiCompletion,
                           &Event,
                           TRUE,
                           TRUE,
                           TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = Irp->IoStatus.Status;
    }
    IoFreeIrp(Irp);
    if (NT_SUCCESS(Status))
        DeviceExtension->InterfaceAcquired = TRUE;
    return Status;
}

static
VOID
AcpiPmiReleaseAcpiInterface(
    _Inout_ PACPIPMI_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->NotificationsRegistered)
    {
        DeviceExtension->AcpiInterface.UnregisterForDeviceNotifications(
            DeviceExtension->AcpiInterface.Context,
            AcpiPmiNotification);
        DeviceExtension->NotificationsRegistered = FALSE;
    }
    if (DeviceExtension->InterfaceAcquired)
    {
        DeviceExtension->AcpiInterface.InterfaceDereference(
            DeviceExtension->AcpiInterface.Context);
        DeviceExtension->InterfaceAcquired = FALSE;
    }
}

static
NTSTATUS
AcpiPmiStart(
    _Inout_ PACPIPMI_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
    Status = AcpiPmiReadCapabilities(DeviceExtension);
    KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIPMI: _PMC failed, status 0x%08lx\n", Status);
        return Status;
    }

    Status = AcpiPmiQueryAcpiInterface(DeviceExtension);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DeviceExtension->AcpiInterface.RegisterForDeviceNotifications(
        DeviceExtension->AcpiInterface.Context,
        AcpiPmiNotification,
        DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        AcpiPmiReleaseAcpiInterface(DeviceExtension);
        return Status;
    }
    DeviceExtension->NotificationsRegistered = TRUE;
    InterlockedExchange(&DeviceExtension->Started, TRUE);
    Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, TRUE);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&DeviceExtension->Started, FALSE);
        AcpiPmiReleaseAcpiInterface(DeviceExtension);
    }
    else
        DeviceExtension->InterfaceEnabled = TRUE;
    DPRINT1("ACPIPMI: flags=0x%08lx unit=%u type=%u model=%ls status=0x%08lx\n",
            DeviceExtension->ReportedCapabilities.Flags, DeviceExtension->ReportedCapabilities.MeasurementUnit,
            DeviceExtension->ReportedCapabilities.MeasurementType, DeviceExtension->ReportedCapabilities.ModelNumber, Status);
    return Status;
}

static
VOID
AcpiPmiStop(
    _Inout_ PACPIPMI_DEVICE_EXTENSION DeviceExtension,
    _In_ NTSTATUS PendingStatus)
{
    InterlockedExchange(&DeviceExtension->Started, FALSE);
    AcpiPmiReleaseAcpiInterface(DeviceExtension);
    KeWaitForSingleObject(&DeviceExtension->WorkIdleEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
    if (DeviceExtension->InterfaceEnabled)
    {
        IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
        DeviceExtension->InterfaceEnabled = FALSE;
    }
    AcpiPmiDrainAllEvents(DeviceExtension, PendingStatus);
}

static
NTSTATUS
AcpiPmiComplete(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
AcpiPmiCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPMI_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PACPIPMI_FILE_CONTEXT FileContext;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return AcpiPmiComplete(Irp, Status, 0);
    if (!DeviceExtension->Started || DeviceExtension->Removing)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Done;
    }
    FileContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*FileContext), ACPIPMI_TAG);
    if (!FileContext)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlZeroMemory(FileContext, sizeof(*FileContext));
    FileContext->DeviceExtension = DeviceExtension;
    InitializeListHead(&FileContext->PendingEvents);
    KeInitializeSpinLock(&FileContext->PendingEventLock);
    KeInitializeSpinLock(&FileContext->EventStateLock);
    Status = IoCsqInitialize(&FileContext->EventCsq, AcpiPmiCsqInsertIrp, AcpiPmiCsqRemoveIrp, AcpiPmiCsqPeekNextIrp, AcpiPmiCsqAcquireLock, AcpiPmiCsqReleaseLock, AcpiPmiCsqCompleteCanceledIrp);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(FileContext, ACPIPMI_TAG);
        goto Done;
    }
    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, FileContext);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(FileContext, ACPIPMI_TAG);
        goto Done;
    }
    FileContext->RemoveLockHeld = TRUE;
    ExAcquireFastMutex(&DeviceExtension->FileListMutex);
    InsertTailList(&DeviceExtension->FileList, &FileContext->DeviceLink);
    FileContext->Linked = TRUE;
    ExReleaseFastMutex(&DeviceExtension->FileListMutex);
    Stack->FileObject->FsContext = FileContext;
    Status = STATUS_SUCCESS;

Done:
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return AcpiPmiComplete(Irp, Status, 0);
}

static
NTSTATUS
NTAPI
AcpiPmiCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PACPIPMI_FILE_CONTEXT FileContext = Stack->FileObject->FsContext;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (FileContext)
        AcpiPmiDrainFileEvents(FileContext, STATUS_CANCELLED);
    return AcpiPmiComplete(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NTAPI
AcpiPmiClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPMI_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PACPIPMI_FILE_CONTEXT FileContext = Stack->FileObject->FsContext;

    if (FileContext)
    {
        AcpiPmiDrainFileEvents(FileContext, STATUS_CANCELLED);
        ExAcquireFastMutex(&DeviceExtension->FileListMutex);
        if (FileContext->Linked)
        {
            RemoveEntryList(&FileContext->DeviceLink);
            FileContext->Linked = FALSE;
        }
        ExReleaseFastMutex(&DeviceExtension->FileListMutex);
        Stack->FileObject->FsContext = NULL;
        if (FileContext->RemoveLockHeld)
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, FileContext);
        ExFreePoolWithTag(FileContext, ACPIPMI_TAG);
    }
    return AcpiPmiComplete(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
AcpiPmiGetCapabilities(
    _In_ PACPIPMI_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PPMI_CAPABILITIES Capabilities,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    PMI_CAPABILITIES_TYPE CapabilityType;

    if (InputLength < sizeof(*Capabilities) || Capabilities->Version != PMI_VERSION || Capabilities->CapabilityType >= PmiCapabilitiesMax)
        return STATUS_INVALID_PARAMETER;
    if (OutputLength < sizeof(*Capabilities))
        return STATUS_BUFFER_TOO_SMALL;
    CapabilityType = Capabilities->CapabilityType;
    if (CapabilityType == PmiMeteredHardware)
    {
        /*
         * The firmware deliberately omits _PMD because its PMIC rail sum
         * cannot be mapped accurately to ACPI device objects.  Returning an
         * empty list would mean a system-wide meter in the Windows PMI ABI,
         * which this meter is not.
         */
        return STATUS_NOT_SUPPORTED;
    }
    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    Capabilities->Version = PMI_VERSION;
    Capabilities->Size = sizeof(*Capabilities);
    Capabilities->CapabilityType = CapabilityType;
    Capabilities->Capabilities.ReportedCapabilities = DeviceExtension->ReportedCapabilities;
    *Information = sizeof(*Capabilities);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiPmiGetConfiguration(
    _Inout_ PPMI_CONFIGURATION Configuration,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    PMI_CONFIGURATION_TYPE ConfigurationType;

    if (InputLength < sizeof(*Configuration) || Configuration->Version != PMI_VERSION || Configuration->ConfigurationType >= PmiConfigurationMax)
        return STATUS_INVALID_PARAMETER;
    if (Configuration->ConfigurationType != PmiMeasurementConfiguration)
        return STATUS_NOT_SUPPORTED;
    if (OutputLength < sizeof(*Configuration))
        return STATUS_BUFFER_TOO_SMALL;
    ConfigurationType = Configuration->ConfigurationType;
    RtlZeroMemory(Configuration, sizeof(*Configuration));
    Configuration->Version = PMI_VERSION;
    Configuration->Size = sizeof(*Configuration);
    Configuration->ConfigurationType = ConfigurationType;
    Configuration->Configuration.MeasurementConfiguration.AveragingInterval = 0;
    *Information = sizeof(*Configuration);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AcpiPmiSetConfiguration(
    _In_ PACPIPMI_DEVICE_EXTENSION DeviceExtension,
    _In_ PPMI_CONFIGURATION Configuration,
    _In_ ULONG InputLength)
{
    if (InputLength < sizeof(*Configuration) || Configuration->Version != PMI_VERSION || Configuration->Size < sizeof(*Configuration) || Configuration->ConfigurationType >= PmiConfigurationMax)
        return STATUS_INVALID_PARAMETER;
    if (!DeviceExtension->ReportedCapabilities.Writeable)
        return STATUS_NOT_SUPPORTED;
    return STATUS_NOT_SUPPORTED;
}

static
NTSTATUS
AcpiPmiQueueEvent(
    _In_ PACPIPMI_FILE_CONTEXT FileContext,
    _Inout_ PIRP Irp,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    PPMI_EVENT Event = Irp->AssociatedIrp.SystemBuffer;
    PMI_EVENT_TYPE EventType;
    KIRQL OldIrql;

    if (!FileContext)
        return STATUS_INVALID_HANDLE;
    if (InputLength < sizeof(*Event) || OutputLength < sizeof(*Event))
        return STATUS_BUFFER_TOO_SMALL;
    if (Event->Version != PMI_VERSION)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&FileContext->EventStateLock, &OldIrql);
    if (AcpiPmiTakePendingEventLocked(FileContext, &EventType))
    {
        KeReleaseSpinLock(&FileContext->EventStateLock, OldIrql);
        Event->Version = PMI_VERSION;
        Event->EventType = EventType;
        *Information = sizeof(*Event);
        return STATUS_SUCCESS;
    }

    IoMarkIrpPending(Irp);
    IoCsqInsertIrp(&FileContext->EventCsq, Irp, NULL);
    KeReleaseSpinLock(&FileContext->EventStateLock, OldIrql);

    return STATUS_PENDING;
}

static
NTSTATUS
NTAPI
AcpiPmiDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPMI_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PVOID Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        return AcpiPmiComplete(Irp, Status, 0);
    if (!DeviceExtension->Started || DeviceExtension->Removing)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Done;
    }
    switch (Stack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_PMI_GET_CAPABILITIES:
            KeWaitForSingleObject(&DeviceExtension->MethodMutex,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            Status = AcpiPmiGetCapabilities(DeviceExtension, Buffer, InputLength, OutputLength, &Information);
            KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
            break;

        case IOCTL_PMI_GET_CONFIGURATION:
            Status = AcpiPmiGetConfiguration(Buffer, InputLength, OutputLength, &Information);
            break;

        case IOCTL_PMI_SET_CONFIGURATION:
            KeWaitForSingleObject(&DeviceExtension->MethodMutex,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            Status = AcpiPmiSetConfiguration(DeviceExtension, Buffer, InputLength);
            KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
            break;

        case IOCTL_PMI_GET_MEASUREMENT:
            if (OutputLength < sizeof(PMI_MEASUREMENT_DATA))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
            {
                PPMI_MEASUREMENT_DATA Measurement = Buffer;
                ULONG Milliwatts;

                KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
                Status = AcpiPmiReadMeasurement(DeviceExtension, &Milliwatts);
                KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
                if (NT_SUCCESS(Status))
                {
                    Measurement->Version = PMI_VERSION;
                    Measurement->CurrentPower = Milliwatts;
                    Information = sizeof(*Measurement);
                }
            }
            break;

        case IOCTL_PMI_REGISTER_EVENT_NOTIFY:
            Status = AcpiPmiQueueEvent(Stack->FileObject->FsContext,
                                       Irp,
                                       InputLength,
                                       OutputLength,
                                       &Information);
            if (Status == STATUS_PENDING)
                return STATUS_PENDING;
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

Done:
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return AcpiPmiComplete(Irp, Status, Information);
}

static
NTSTATUS
NTAPI
AcpiPmiPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPMI_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = AcpiPmiForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = AcpiPmiStart(DeviceExtension);
            return AcpiPmiComplete(Irp, Status, 0);

        case IRP_MN_STOP_DEVICE:
            AcpiPmiStop(DeviceExtension, STATUS_DEVICE_NOT_READY);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            InterlockedExchange(&DeviceExtension->Removing, TRUE);
            AcpiPmiStop(DeviceExtension, STATUS_DEVICE_REMOVED);
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (!NT_SUCCESS(Status))
                return AcpiPmiComplete(Irp, Status, 0);
            InterlockedExchange(&DeviceExtension->Removing, TRUE);
            AcpiPmiStop(DeviceExtension, STATUS_DEVICE_REMOVED);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = AcpiPmiForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            if (DeviceExtension->InterfaceRegistered)
                RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
            AcpiPmiComplete(Irp, Status, 0);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AcpiPmiPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPMI_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AcpiPmiPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPMI_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AcpiPmiAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PACPIPMI_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(*DeviceExtension), NULL, FILE_DEVICE_PMI, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, ACPIPMI_TAG, 0, 0);
    KeInitializeMutex(&DeviceExtension->MethodMutex, 0);
    ExInitializeFastMutex(&DeviceExtension->FileListMutex);
    InitializeListHead(&DeviceExtension->FileList);
    KeInitializeEvent(&DeviceExtension->WorkIdleEvent,
                      NotificationEvent,
                      TRUE);
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject, PhysicalDeviceObject, &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_DEVICE_POWER_METER, NULL, &DeviceExtension->InterfaceName);
    if (!NT_SUCCESS(Status))
    {
        IoDetachDevice(DeviceExtension->LowerDevice);
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceExtension->InterfaceRegistered = TRUE;
    DeviceObject->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
AcpiPmiUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->MajorFunction[IRP_MJ_CREATE] = AcpiPmiCreate;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AcpiPmiClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = AcpiPmiCleanup;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AcpiPmiDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = AcpiPmiPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AcpiPmiPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = AcpiPmiPassThrough;
    DriverObject->DriverExtension->AddDevice = AcpiPmiAddDevice;
    DriverObject->DriverUnload = AcpiPmiUnload;
    return STATUS_SUCCESS;
}
