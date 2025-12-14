/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:            ntoskrnl/po/thermal.c
 * PURPOSE:         Thermal notification helpers
 * COPYRIGHT:       2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/drivers/acpi/acpiproc_ioctl.h>

#define NDEBUG
#include <debug.h>

#define PO_THERMAL_TAG 'hToP'

typedef struct _POP_THERMAL_EVENT_STATE {
    LARGE_INTEGER Timestamp;
    ULONG ProcessorIndex;
    ULONG TripPoint;
    PO_THERMAL_EVENT_TYPE EventType;
    ULONG Sequence;
    BOOLEAN HotThrottleAttempted;
    LARGE_INTEGER LastThrottleAttempt;
} POP_THERMAL_EVENT_STATE, *PPOP_THERMAL_EVENT_STATE;

static KSPIN_LOCK PopThermalLock;
static POP_THERMAL_EVENT_STATE PopThermalState;
static WORK_QUEUE_ITEM PopThermalWorkItem;
static volatile LONG PopThermalWorkItemQueued;
static KDPC PopThermalDpc;

static
NTSTATUS
PopSendProcessorIoctl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputBufferLength) PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_writes_bytes_opt_(OutputBufferLength) PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength,
    _Out_opt_ PIO_STATUS_BLOCK IoStatusBlock)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        DeviceObject,
                                        InputBuffer,
                                        InputBufferLength,
                                        OutputBuffer,
                                        OutputBufferLength,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        Status = IoStatus.Status;
    }
    else
    {
        Status = IoStatus.Status;
    }

    if (IoStatusBlock)
        *IoStatusBlock = IoStatus;

    return Status;
}

static
NTSTATUS
PopQueryProcessorThrottle(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Outptr_ PACPIPROC_THROTTLE_INFO *InfoOut,
    _Out_ PULONG BufferLength)
{
    ACPIPROC_THROTTLE_INFO SmallInfo;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;
    PACPIPROC_THROTTLE_INFO InfoBuffer = NULL;
    ULONG InfoSize;

    Status = PopSendProcessorIoctl(DeviceObject,
                                   IOCTL_ACPIPROC_QUERY_THROTTLE,
                                   NULL,
                                   0,
                                   &SmallInfo,
                                   sizeof(SmallInfo),
                                   &IoStatus);
    if (Status == STATUS_BUFFER_TOO_SMALL &&
        IoStatus.Information > sizeof(SmallInfo))
    {
        InfoSize = (ULONG)IoStatus.Information;
        InfoBuffer = ExAllocatePoolWithTag(PagedPool,
                                           InfoSize,
                                           PO_THERMAL_TAG);
        if (!InfoBuffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = PopSendProcessorIoctl(DeviceObject,
                                       IOCTL_ACPIPROC_QUERY_THROTTLE,
                                       NULL,
                                       0,
                                       InfoBuffer,
                                       InfoSize,
                                       &IoStatus);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(InfoBuffer, PO_THERMAL_TAG);
            return Status;
        }
    }
    else if (NT_SUCCESS(Status))
    {
        InfoSize = (ULONG)min(IoStatus.Information, sizeof(SmallInfo));
        if (InfoSize < sizeof(ACPIPROC_THROTTLE_INFO))
            InfoSize = sizeof(ACPIPROC_THROTTLE_INFO);

        InfoBuffer = ExAllocatePoolWithTag(PagedPool,
                                           InfoSize,
                                           PO_THERMAL_TAG);
        if (!InfoBuffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(InfoBuffer, &SmallInfo, min(InfoSize, sizeof(SmallInfo)));
    }
    else
    {
        return Status;
    }

    *InfoOut = InfoBuffer;
    *BufferLength = InfoSize;
    return STATUS_SUCCESS;
}

static
BOOLEAN
PopRequestThermalThrottling(VOID)
{
    PWSTR InterfaceList = NULL;
    PWSTR Link;
    NTSTATUS Status;
    BOOLEAN AnySuccess = FALSE;

    Status = IoGetDeviceInterfaces(&GUID_DEVICE_PROCESSOR,
                                   NULL,
                                   0,
                                   &InterfaceList);
    if (!NT_SUCCESS(Status) || !InterfaceList)
        return FALSE;

    for (Link = InterfaceList; Link && *Link; Link += wcslen(Link) + 1)
    {
        UNICODE_STRING LinkString;
        PFILE_OBJECT FileObject;
        PDEVICE_OBJECT DeviceObject;

        RtlInitUnicodeString(&LinkString, Link);
        Status = IoGetDeviceObjectPointer(&LinkString,
                                          FILE_READ_DATA | FILE_WRITE_DATA,
                                          &FileObject,
                                          &DeviceObject);
        if (!NT_SUCCESS(Status))
            continue;

        PACPIPROC_THROTTLE_INFO ThrottleInfo = NULL;
        ULONG BufferLength = 0;

        Status = PopQueryProcessorThrottle(DeviceObject,
                                           &ThrottleInfo,
                                           &BufferLength);
        if (NT_SUCCESS(Status) && ThrottleInfo)
        {
            if (ThrottleInfo->StateCount != 0)
            {
                ULONG TargetIndex = ThrottleInfo->StateCount - 1;

                if ((ThrottleInfo->Flags & ACPIPROC_THROTTLE_INFO_FLAG_TPC_VALID) &&
                    ThrottleInfo->LimitIndex > TargetIndex)
                {
                    TargetIndex = ThrottleInfo->LimitIndex;
                }

                ACPIPROC_THROTTLE_REQUEST Request;
                Request.StateIndex = TargetIndex;

                Status = PopSendProcessorIoctl(DeviceObject,
                                               IOCTL_ACPIPROC_SET_THROTTLE,
                                               &Request,
                                               sizeof(Request),
                                               NULL,
                                               0,
                                               NULL);
                if (NT_SUCCESS(Status))
                    AnySuccess = TRUE;
            }

            ExFreePoolWithTag(ThrottleInfo, PO_THERMAL_TAG);
        }

        ObDereferenceObject(FileObject);
    }

    ExFreePool(InterfaceList);
    return AnySuccess;
}

static
VOID
PopScheduleThermalWorker(VOID)
{
    if (InterlockedCompareExchange(&PopThermalWorkItemQueued, 1, 0) == 0)
    {
        ExQueueWorkItem(&PopThermalWorkItem, DelayedWorkQueue);
    }
}

static
VOID
NTAPI
PopThermalDpcRoutine(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(DeferredContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    PopScheduleThermalWorker();
}

static
VOID
NTAPI
PopThermalWorker(
    _In_ PVOID Context)
{
    POP_THERMAL_EVENT_STATE Snapshot;
    KIRQL OldIrql;
    SYSTEM_POWER_STATE TargetState = PowerSystemWorking;
    POWER_ACTION Action = PowerActionNone;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Context);

    BOOLEAN NeedThrottleAttempt = FALSE;
    LARGE_INTEGER Now;
    LARGE_INTEGER Timeout;

    KeQuerySystemTime(&Now);
    Timeout.QuadPart = 60 * 1000 * 1000 * 10LL; /* 60 seconds */

    KeAcquireSpinLock(&PopThermalLock, &OldIrql);
    Snapshot = PopThermalState;
    if (Snapshot.EventType == PoThermalEventHot &&
        !PopThermalState.HotThrottleAttempted)
    {
        PopThermalState.HotThrottleAttempted = TRUE;
        NeedThrottleAttempt = TRUE;
        Snapshot.HotThrottleAttempted = TRUE;
        PopThermalState.LastThrottleAttempt = Now;
    }
    else if (Snapshot.EventType == PoThermalEventHot &&
             PopThermalState.HotThrottleAttempted)
    {
        if ((Now.QuadPart - PopThermalState.LastThrottleAttempt.QuadPart) >= Timeout.QuadPart)
        {
            PopThermalState.HotThrottleAttempted = TRUE;
            NeedThrottleAttempt = TRUE;
            PopThermalState.LastThrottleAttempt = Now;
        }
    }
    KeReleaseSpinLock(&PopThermalLock, OldIrql);

    InterlockedExchange(&PopThermalWorkItemQueued, 0);

    switch (Snapshot.EventType)
    {
        case PoThermalEventHot:
            if (NeedThrottleAttempt)
            {
                if (PopRequestThermalThrottling())
                {
                    DPRINT1("pop: thermal throttling requested after hot event\n");
                    return;
                }
            }

            if (PopCapabilities.SystemS4 && PopCapabilities.HiberFilePresent)
            {
                TargetState = PowerSystemHibernate;
                Action = PowerActionHibernate;
            }
            else
            {
                TargetState = PowerSystemShutdown;
                Action = PowerActionShutdownOff;
            }
            break;

        case PoThermalEventCritical:
            TargetState = PowerSystemShutdown;
            Action = PowerActionShutdownOff;
            break;

        default:
            return;
    }

    Status = PopSetSystemPowerState(TargetState, Action);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("pop: thermal action %u failed (status 0x%lx)\n",
                Action,
                Status);
    }
}

VOID
PopInitThermalSupport(VOID)
{
    KeInitializeSpinLock(&PopThermalLock);
    RtlZeroMemory(&PopThermalState, sizeof(PopThermalState));
    ExInitializeWorkItem(&PopThermalWorkItem, PopThermalWorker, NULL);
    PopThermalWorkItemQueued = 0;
    KeInitializeDpc(&PopThermalDpc, PopThermalDpcRoutine, NULL);
}

VOID
NTAPI
PoNotifyProcessorThermalEvent(
    _In_ ULONG ProcessorIndex,
    _In_ PO_THERMAL_EVENT_TYPE EventType,
    _In_ ULONG TripPoint)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&PopThermalLock, &OldIrql);
    PopThermalState.ProcessorIndex = ProcessorIndex;
    PopThermalState.EventType = EventType;
    PopThermalState.TripPoint = TripPoint;
    KeQuerySystemTime(&PopThermalState.Timestamp);
    ++PopThermalState.Sequence;
    PopThermalState.HotThrottleAttempted = FALSE;
    PopThermalState.LastThrottleAttempt.QuadPart = 0;
    KeReleaseSpinLock(&PopThermalLock, OldIrql);

    DPRINT1("pop: thermal event cpu %lu type %u trip %lu\n",
            ProcessorIndex,
            EventType,
            TripPoint);

    if (KeGetCurrentIrql() <= DISPATCH_LEVEL)
    {
        PopScheduleThermalWorker();
    }
    else
    {
        KeInsertQueueDpc(&PopThermalDpc, NULL, NULL);
    }
}
