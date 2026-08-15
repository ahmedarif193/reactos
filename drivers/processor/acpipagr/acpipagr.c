/*
 * PROJECT:     ReactOS ACPI Processor Aggregator Driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ACPI000C logical-processor idling policy
 */

#include <ntddk.h>
#include <wdmguid.h>
#include <acpiioct.h>
#include <reactos/drivers/acpipagr.h>

#define NDEBUG
#include <debug.h>

#define ACPIPAGR_TAG 'rPgA'
#define ACPIPAGR_MAX_OUTPUT PAGE_SIZE
#define ACPIPAGR_METHOD(a, b, c, d) ((ULONG)(UCHAR)(a) | ((ULONG)(UCHAR)(b) << 8) | ((ULONG)(UCHAR)(c) << 16) | ((ULONG)(UCHAR)(d) << 24))
#define ACPIPAGR_PUR_REVISION 1
#define ACPIPAGR_NOTIFY_PROCESSOR_AGGREGATOR 0x80
#define ACPIPAGR_OST_SUCCESS 0
#define ACPIPAGR_OST_NO_ACTION 1

typedef struct _ACPIPAGR_DEVICE_EXTENSION ACPIPAGR_DEVICE_EXTENSION, *PACPIPAGR_DEVICE_EXTENSION;

typedef struct _ACPIPAGR_WORK_CONTEXT
{
    WORK_QUEUE_ITEM WorkItem;
    PACPIPAGR_DEVICE_EXTENSION DeviceExtension;
} ACPIPAGR_WORK_CONTEXT, *PACPIPAGR_WORK_CONTEXT;

struct _ACPIPAGR_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    KMUTEX MethodMutex;
    ACPI_INTERFACE_STANDARD AcpiInterface;
    BOOLEAN InterfaceAcquired;
    BOOLEAN NotificationsRegistered;
    volatile LONG Started;
    volatile LONG Removing;
    volatile LONG WorkCount;
    KEVENT WorkIdleEvent;
    ULONG RequestedProcessors;
    ULONG ParkedProcessors;
};

static
NTSTATUS
NTAPI
AcpiPagrCompletion(
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
AcpiPagrForwardSynchronously(
    _In_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, AcpiPagrCompletion, &Event, TRUE, TRUE, TRUE);
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
AcpiPagrExecute(
    _In_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD,
                                        DeviceExtension->LowerDevice,
                                        InputBuffer,
                                        InputLength,
                                        NULL,
                                        0,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
AcpiPagrEvaluate(
    _In_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Outptr_ PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    *OutputBuffer = NULL;
    *OutputLength = 0;
    Output = ExAllocatePoolWithTag(PagedPool, ACPIPAGR_MAX_OUTPUT, ACPIPAGR_TAG);
    if (!Output)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Output, ACPIPAGR_MAX_OUTPUT);
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD,
                                        DeviceExtension->LowerDevice,
                                        InputBuffer,
                                        InputLength,
                                        Output,
                                        ACPIPAGR_MAX_OUTPUT,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
    {
        ExFreePoolWithTag(Output, ACPIPAGR_TAG);
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
    if (IoStatus.Information < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ||
        IoStatus.Information > ACPIPAGR_MAX_OUTPUT ||
        Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ||
        Output->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) ||
        Output->Length > IoStatus.Information)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Failure;
    }
    *OutputBuffer = Output;
    *OutputLength = (ULONG)IoStatus.Information;
    return STATUS_SUCCESS;

Failure:
    ExFreePoolWithTag(Output, ACPIPAGR_TAG);
    return Status;
}

static
NTSTATUS
AcpiPagrEvaluateNoArguments(
    _In_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG MethodName,
    _Outptr_ PACPI_EVAL_OUTPUT_BUFFER *OutputBuffer,
    _Out_ PULONG OutputLength)
{
    ACPI_EVAL_INPUT_BUFFER Input;

    RtlZeroMemory(&Input, sizeof(Input));
    Input.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    Input.MethodNameAsUlong = MethodName;
    return AcpiPagrEvaluate(DeviceExtension, &Input, sizeof(Input), OutputBuffer, OutputLength);
}

static
PACPI_METHOD_ARGUMENT
AcpiPagrOutputArgument(
    _In_ PACPI_EVAL_OUTPUT_BUFFER Output,
    _In_ ULONG OutputLength,
    _In_ ULONG Index)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Current;
    ULONG Length;

    if (!Output || Output->Length > OutputLength ||
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
        Length = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Argument);
        if (Length > (ULONG)(End - (PUCHAR)Argument))
            return NULL;
        if (Current == Index)
            return Argument;
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
    return NULL;
}

static
PACPI_METHOD_ARGUMENT
AcpiPagrPackageArgument(
    _In_ PACPI_METHOD_ARGUMENT Package,
    _In_ ULONG Index)
{
    PACPI_METHOD_ARGUMENT Argument;
    PUCHAR End;
    ULONG Current;
    ULONG Length;

    if (!Package || Package->Type != ACPI_METHOD_ARGUMENT_PACKAGE ||
        Package->DataLength < ACPI_METHOD_ARGUMENT_LENGTH(0))
    {
        return NULL;
    }
    Argument = (PACPI_METHOD_ARGUMENT)Package->Data;
    End = Package->Data + Package->DataLength;
    for (Current = 0; Current <= Index; Current++)
    {
        if ((PUCHAR)Argument > End ||
            (ULONG)(End - (PUCHAR)Argument) < ACPI_METHOD_ARGUMENT_LENGTH(0))
        {
            return NULL;
        }
        Length = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(Argument);
        if (Length > (ULONG)(End - (PUCHAR)Argument))
            return NULL;
        if (Current == Index)
            return Argument;
        Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    }
    return NULL;
}

static
NTSTATUS
AcpiPagrReadRequest(
    _In_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension,
    _Out_ PULONG RequestedProcessors)
{
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT RevisionArgument;
    PACPI_METHOD_ARGUMENT CountArgument;
    ULONG OutputLength;
    ULONG ArgumentLength;
    NTSTATUS Status;

    Status = AcpiPagrEvaluateNoArguments(DeviceExtension,
                                         ACPIPAGR_METHOD('_', 'P', 'U', 'R'),
                                         &Output,
                                         &OutputLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Output->Length < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) + ACPI_METHOD_ARGUMENT_LENGTH(0))
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Done;
    }
    ArgumentLength = ACPI_METHOD_ARGUMENT_LENGTH_FROM_ARGUMENT(&Output->Argument[0]);
    if (ArgumentLength > Output->Length - FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument))
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Done;
    }
    if (Output->Count == 1 && Output->Argument[0].Type == ACPI_METHOD_ARGUMENT_PACKAGE)
    {
        RevisionArgument = AcpiPagrPackageArgument(&Output->Argument[0], 0);
        CountArgument = AcpiPagrPackageArgument(&Output->Argument[0], 1);
        if (AcpiPagrPackageArgument(&Output->Argument[0], 2) != NULL)
        {
            Status = STATUS_ACPI_INVALID_DATA;
            goto Done;
        }
    }
    else if (Output->Count == 2)
    {
        RevisionArgument = AcpiPagrOutputArgument(Output, OutputLength, 0);
        CountArgument = AcpiPagrOutputArgument(Output, OutputLength, 1);
    }
    else
    {
        RevisionArgument = NULL;
        CountArgument = NULL;
    }
    if (!RevisionArgument || !CountArgument ||
        RevisionArgument->Type != ACPI_METHOD_ARGUMENT_INTEGER ||
        CountArgument->Type != ACPI_METHOD_ARGUMENT_INTEGER)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Done;
    }
    if (RevisionArgument->Argument != ACPIPAGR_PUR_REVISION)
    {
        Status = STATUS_REVISION_MISMATCH;
        goto Done;
    }
    *RequestedProcessors = CountArgument->Argument;
    Status = STATUS_SUCCESS;

Done:
    ExFreePoolWithTag(Output, ACPIPAGR_TAG);
    return Status;
}

static
NTSTATUS
AcpiPagrReportStatus(
    _In_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG StatusCode,
    _In_ ULONG ParkedProcessors)
{
    ULONG InputStorage[(FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) +
                        3 * ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) +
                        sizeof(ULONG) - 1) / sizeof(ULONG)];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Input = (PVOID)InputStorage;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG InputLength = sizeof(InputStorage);

    RtlZeroMemory(InputStorage, sizeof(InputStorage));
    Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input->MethodNameAsUlong = ACPIPAGR_METHOD('_', 'O', 'S', 'T');
    Input->Size = InputLength - FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument);
    Input->ArgumentCount = 3;
    Argument = Input->Argument;
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, ACPIPAGR_NOTIFY_PROCESSOR_AGGREGATOR);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, StatusCode);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument, &ParkedProcessors, sizeof(ParkedProcessors));
    return AcpiPagrExecute(DeviceExtension, Input, InputLength);
}

static
NTSTATUS
AcpiPagrRefresh(
    _Inout_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension)
{
    ULONG RequestedProcessors;
    ULONG ParkedProcessors = DeviceExtension->ParkedProcessors;
    ULONG OstCode;
    NTSTATUS OstStatus;
    NTSTATUS Status;

    Status = AcpiPagrReadRequest(DeviceExtension, &RequestedProcessors);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPIPAGR: _PUR failed, status 0x%08lx\n", Status);
        return Status;
    }
    Status = PoSetProcessorAggregatorParking(RequestedProcessors, &ParkedProcessors);
    OstCode = NT_SUCCESS(Status) ? ACPIPAGR_OST_SUCCESS : ACPIPAGR_OST_NO_ACTION;
    if (NT_SUCCESS(Status))
    {
        DeviceExtension->RequestedProcessors = RequestedProcessors;
        DeviceExtension->ParkedProcessors = ParkedProcessors;
    }
    OstStatus = AcpiPagrReportStatus(DeviceExtension, OstCode, ParkedProcessors);
    DPRINT1("ACPIPAGR: requested=%lu parked=%lu status=0x%08lx _OST=0x%08lx\n",
            RequestedProcessors, ParkedProcessors, Status, OstStatus);
    return Status;
}

static
VOID
NTAPI
AcpiPagrWorker(
    _In_ PVOID Context)
{
    PACPIPAGR_WORK_CONTEXT WorkContext = Context;
    PACPIPAGR_DEVICE_EXTENSION DeviceExtension = WorkContext->DeviceExtension;

    KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
    if (DeviceExtension->Started && !DeviceExtension->Removing)
        AcpiPagrRefresh(DeviceExtension);
    KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
    if (InterlockedDecrement(&DeviceExtension->WorkCount) == 0)
        KeSetEvent(&DeviceExtension->WorkIdleEvent, IO_NO_INCREMENT, FALSE);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, WorkContext);
    ExFreePoolWithTag(WorkContext, ACPIPAGR_TAG);
}

static
VOID
NTAPI
AcpiPagrNotification(
    _In_ PVOID Context,
    _In_ ULONG NotifyCode)
{
    PACPIPAGR_DEVICE_EXTENSION DeviceExtension = Context;
    PACPIPAGR_WORK_CONTEXT WorkContext;
    NTSTATUS Status;

    if (NotifyCode != ACPIPAGR_NOTIFY_PROCESSOR_AGGREGATOR ||
        !DeviceExtension->Started || DeviceExtension->Removing)
    {
        return;
    }
    WorkContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*WorkContext), ACPIPAGR_TAG);
    if (!WorkContext)
        return;
    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, WorkContext);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(WorkContext, ACPIPAGR_TAG);
        return;
    }
    WorkContext->DeviceExtension = DeviceExtension;
    ExInitializeWorkItem(&WorkContext->WorkItem, AcpiPagrWorker, WorkContext);
    if (InterlockedIncrement(&DeviceExtension->WorkCount) == 1)
        KeClearEvent(&DeviceExtension->WorkIdleEvent);
    ExQueueWorkItem(&WorkContext->WorkItem, DelayedWorkQueue);
}

static
NTSTATUS
AcpiPagrQueryAcpiInterface(
    _Inout_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension)
{
    PIO_STACK_LOCATION Stack;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    RtlZeroMemory(&DeviceExtension->AcpiInterface, sizeof(DeviceExtension->AcpiInterface));
    DeviceExtension->AcpiInterface.Size = sizeof(DeviceExtension->AcpiInterface);
    DeviceExtension->AcpiInterface.Version = 1;
    Irp = IoAllocateIrp(DeviceExtension->LowerDevice->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_ACPI_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size = sizeof(DeviceExtension->AcpiInterface);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)&DeviceExtension->AcpiInterface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;
    IoSetCompletionRoutine(Irp, AcpiPagrCompletion, &Event, TRUE, TRUE, TRUE);
    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }
    IoFreeIrp(Irp);
    if (NT_SUCCESS(Status))
        DeviceExtension->InterfaceAcquired = TRUE;
    return Status;
}

static
VOID
AcpiPagrReleaseAcpiInterface(
    _Inout_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->NotificationsRegistered)
    {
        DeviceExtension->AcpiInterface.UnregisterForDeviceNotifications(
            DeviceExtension->AcpiInterface.Context,
            AcpiPagrNotification);
        DeviceExtension->NotificationsRegistered = FALSE;
    }
    if (DeviceExtension->InterfaceAcquired)
    {
        DeviceExtension->AcpiInterface.InterfaceDereference(DeviceExtension->AcpiInterface.Context);
        DeviceExtension->InterfaceAcquired = FALSE;
    }
}

static
NTSTATUS
AcpiPagrStart(
    _Inout_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    Status = AcpiPagrQueryAcpiInterface(DeviceExtension);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = DeviceExtension->AcpiInterface.RegisterForDeviceNotifications(
        DeviceExtension->AcpiInterface.Context,
        AcpiPagrNotification,
        DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        AcpiPagrReleaseAcpiInterface(DeviceExtension);
        return Status;
    }
    DeviceExtension->NotificationsRegistered = TRUE;
    InterlockedExchange(&DeviceExtension->Started, TRUE);
    KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
    Status = AcpiPagrRefresh(DeviceExtension);
    KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&DeviceExtension->Started, FALSE);
        AcpiPagrReleaseAcpiInterface(DeviceExtension);
    }
    return Status;
}

static
VOID
AcpiPagrStop(
    _Inout_ PACPIPAGR_DEVICE_EXTENSION DeviceExtension)
{
    ULONG ParkedProcessors;

    InterlockedExchange(&DeviceExtension->Started, FALSE);
    AcpiPagrReleaseAcpiInterface(DeviceExtension);
    KeWaitForSingleObject(&DeviceExtension->WorkIdleEvent, Executive, KernelMode, FALSE, NULL);
    KeWaitForSingleObject(&DeviceExtension->MethodMutex, Executive, KernelMode, FALSE, NULL);
    if (NT_SUCCESS(PoSetProcessorAggregatorParking(0, &ParkedProcessors)))
    {
        DeviceExtension->RequestedProcessors = 0;
        DeviceExtension->ParkedProcessors = ParkedProcessors;
    }
    KeReleaseMutex(&DeviceExtension->MethodMutex, FALSE);
}

static
NTSTATUS
AcpiPagrComplete(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
AcpiPagrCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPAGR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status = STATUS_SUCCESS;

    if (Stack->MajorFunction == IRP_MJ_CREATE &&
        (!DeviceExtension->Started || DeviceExtension->Removing))
    {
        Status = STATUS_DEVICE_NOT_READY;
    }
    return AcpiPagrComplete(Irp, Status);
}

static
NTSTATUS
NTAPI
AcpiPagrPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPAGR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = AcpiPagrForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = AcpiPagrStart(DeviceExtension);
            return AcpiPagrComplete(Irp, Status);

        case IRP_MN_STOP_DEVICE:
            AcpiPagrStop(DeviceExtension);
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            InterlockedExchange(&DeviceExtension->Removing, TRUE);
            AcpiPagrStop(DeviceExtension);
            break;

        case IRP_MN_REMOVE_DEVICE:
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (!NT_SUCCESS(Status))
                return AcpiPagrComplete(Irp, Status);
            InterlockedExchange(&DeviceExtension->Removing, TRUE);
            AcpiPagrStop(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = AcpiPagrForwardSynchronously(DeviceExtension, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            AcpiPagrComplete(Irp, Status);
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
AcpiPagrPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPAGR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AcpiPagrPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PACPIPAGR_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
AcpiPagrAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PACPIPAGR_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(*DeviceExtension),
                            NULL,
                            FILE_DEVICE_UNKNOWN,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, ACPIPAGR_TAG, 0, 0);
    KeInitializeMutex(&DeviceExtension->MethodMutex, 0);
    KeInitializeEvent(&DeviceExtension->WorkIdleEvent, NotificationEvent, TRUE);
    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject,
                                             PhysicalDeviceObject,
                                             &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceObject->Flags |= DO_POWER_PAGABLE | DO_BUFFERED_IO;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
AcpiPagrUnload(
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
    DriverObject->MajorFunction[IRP_MJ_CREATE] = AcpiPagrCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AcpiPagrCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = AcpiPagrCreateClose;
    DriverObject->MajorFunction[IRP_MJ_PNP] = AcpiPagrPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = AcpiPagrPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = AcpiPagrPassThrough;
    DriverObject->DriverExtension->AddDevice = AcpiPagrAddDevice;
    DriverObject->DriverUnload = AcpiPagrUnload;
    return STATUS_SUCCESS;
}
