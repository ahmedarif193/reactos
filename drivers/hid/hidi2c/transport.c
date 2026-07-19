/*
 * PROJECT:     ReactOS HID over I2C minidriver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ACPI discovery, Resource Hub transport, and HID-I2C protocol
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "hidi2c.h"

#define NDEBUG
#include <debug.h>

NTSYSAPI
NTSTATUS
NTAPI
ZwWaitForSingleObject(
    _In_ HANDLE Handle,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout);

NTHALAPI
VOID
NTAPI
HalDisableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql);

NTHALAPI
BOOLEAN
NTAPI
HalEnableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE InterruptMode);

#define HIDI2C_OPCODE_RESET       0x01
#define HIDI2C_OPCODE_GET_REPORT  0x02
#define HIDI2C_OPCODE_SET_REPORT  0x03
#define HIDI2C_OPCODE_SET_POWER   0x08

#define HIDI2C_REPORT_INPUT       0x01
#define HIDI2C_REPORT_OUTPUT      0x02
#define HIDI2C_REPORT_FEATURE     0x03

#define HIDI2C_POWER_ON           0x00
#define HIDI2C_POWER_SLEEP        0x01

/* HID-over-I2C 1.0 TIMEOUT_HostInitiatedReset. */
#define HIDI2C_RESET_TIMEOUT_100NS (5ULL * 1000 * 1000 * 10)

static const UCHAR Hidi2cDsmGuid[16] =
{
    0xF7, 0xF6, 0xDF, 0x3C, 0x67, 0x42, 0x55, 0x45,
    0xAD, 0x05, 0xB3, 0x0A, 0x3D, 0x89, 0x38, 0xDE
};

static
VOID
Hidi2cCompleteRead(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information);

static
VOID
Hidi2cCompleteQueuedRead(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information);

static __inline
USHORT
Hidi2cReadUshort(
    _In_reads_(2) const UCHAR *Buffer)
{
    return (USHORT)(Buffer[0] | ((USHORT)Buffer[1] << 8));
}

static __inline
VOID
Hidi2cWriteUshort(
    _Out_writes_(2) UCHAR *Buffer,
    _In_ USHORT Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
}

static
VOID
Hidi2cDelayMilliseconds(
    _In_ ULONG Milliseconds)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -(LONGLONG)Milliseconds * 10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
NTSTATUS
Hidi2cSpbSequence(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_opt_(WriteLength) PVOID WriteBuffer,
    _In_ ULONG WriteLength,
    _Out_writes_bytes_opt_(ReadLength) PVOID ReadBuffer,
    _In_ ULONG ReadLength,
    _Out_opt_ PULONG_PTR Information)
{
    SPB_TRANSFER_LIST_AND_ENTRIES(2) Sequence;
    IO_STATUS_BLOCK IoStatusBlock;
    ULONG TransferCount, InputLength;
    NTSTATUS Status;

    TransferCount = (WriteLength != 0) + (ReadLength != 0);
    if (TransferCount == 0)
        return STATUS_INVALID_PARAMETER;

    SPB_TRANSFER_LIST_INIT(&Sequence.List, TransferCount);
    TransferCount = 0;
    if (WriteLength != 0)
    {
        Sequence.List.Transfers[TransferCount++] =
            SPB_TRANSFER_LIST_ENTRY_INIT_NON_PAGED(
                SpbTransferDirectionToDevice,
                0,
                WriteBuffer,
                WriteLength);
    }
    if (ReadLength != 0)
    {
        Sequence.List.Transfers[TransferCount++] =
            SPB_TRANSFER_LIST_ENTRY_INIT_NON_PAGED(
                SpbTransferDirectionFromDevice,
                0,
                ReadBuffer,
                ReadLength);
    }

    InputLength = FIELD_OFFSET(SPB_TRANSFER_LIST, Transfers) +
                  TransferCount * sizeof(SPB_TRANSFER_LIST_ENTRY);
    Status = ZwDeviceIoControlFile(DeviceExtension->SpbHandle,
                                   NULL,
                                   NULL,
                                   NULL,
                                   &IoStatusBlock,
                                   IOCTL_SPB_EXECUTE_SEQUENCE,
                                   &Sequence.List,
                                   InputLength,
                                   NULL,
                                   0);
    if (Status != STATUS_SUCCESS)
    {
        if (Information != NULL)
            *Information = 0;
        return Status;
    }

    if (Information != NULL)
        *Information = IoStatusBlock.Information;
    return Hidi2cValidateTransferResult(Status,
                                        IoStatusBlock.Information,
                                        (ULONG_PTR)WriteLength + ReadLength);
}

static
NTSTATUS
Hidi2cSpbRead(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    Status = ZwReadFile(DeviceExtension->SpbHandle,
                        NULL,
                        NULL,
                        NULL,
                        &IoStatusBlock,
                        Buffer,
                        Length,
                        NULL,
                        NULL);
    if (Status != STATUS_SUCCESS)
        return Status;
    return Hidi2cValidateTransferResult(Status,
                                        IoStatusBlock.Information,
                                        Length);
}

static
NTSTATUS
Hidi2cSpbWrite(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    Status = ZwWriteFile(DeviceExtension->SpbHandle,
                         NULL,
                         NULL,
                         NULL,
                         &IoStatusBlock,
                         Buffer,
                         Length,
                         NULL,
                         NULL);
    if (Status != STATUS_SUCCESS)
        return Status;
    return Hidi2cValidateTransferResult(Status,
                                        IoStatusBlock.Information,
                                        Length);
}

static
NTSTATUS
Hidi2cReadRegister(
    _In_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ USHORT Register,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    UCHAR RegisterBuffer[2];
    NTSTATUS Status;

    Hidi2cWriteUshort(RegisterBuffer, Register);
    ExAcquireFastMutex(&DeviceExtension->IoMutex);
    Status = Hidi2cSpbSequence(DeviceExtension,
                               RegisterBuffer,
                               sizeof(RegisterBuffer),
                               Buffer,
                               Length,
                               NULL);
    ExReleaseFastMutex(&DeviceExtension->IoMutex);
    return Status;
}

static
NTSTATUS
Hidi2cEvaluateDescriptorRegister(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Out_ PUSHORT DescriptorRegister)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PACPI_EVAL_INPUT_BUFFER_COMPLEX InputBuffer;
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    PACPI_METHOD_ARGUMENT Argument;
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    PIRP Irp;
    ULONG InputLength, OutputLength;
    NTSTATUS Status;

    InputLength = FIELD_OFFSET(ACPI_EVAL_INPUT_BUFFER_COMPLEX, Argument) +
                  ACPI_METHOD_ARGUMENT_LENGTH(sizeof(Hidi2cDsmGuid)) +
                  ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) +
                  ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG)) +
                  ACPI_METHOD_ARGUMENT_LENGTH(0);
    OutputLength = FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) +
                   ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG));

    InputBuffer = ExAllocatePoolWithTag(NonPagedPool, InputLength, HIDI2C_TAG);
    OutputBuffer = ExAllocatePoolWithTag(NonPagedPool, OutputLength, HIDI2C_TAG);
    if (InputBuffer == NULL || OutputBuffer == NULL)
    {
        if (InputBuffer != NULL)
            ExFreePoolWithTag(InputBuffer, HIDI2C_TAG);
        if (OutputBuffer != NULL)
            ExFreePoolWithTag(OutputBuffer, HIDI2C_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(InputBuffer, InputLength);
    RtlZeroMemory(OutputBuffer, OutputLength);
    InputBuffer->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    InputBuffer->MethodNameAsUlong = 'MSD_';
    InputBuffer->Size = InputLength;
    InputBuffer->ArgumentCount = 4;

    Argument = InputBuffer->Argument;
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument,
                                    Hidi2cDsmGuid,
                                    sizeof(Hidi2cDsmGuid));
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, 1);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, 1);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    Argument->Type = ACPI_METHOD_ARGUMENT_PACKAGE;
    Argument->DataLength = 0;
    Argument->Argument = 0;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IOCTL_ACPI_EVAL_METHOD,
                                        HidExtension->NextDeviceObject,
                                        InputBuffer,
                                        InputLength,
                                        OutputBuffer,
                                        OutputLength,
                                        FALSE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    Status = IoCallDriver(HidExtension->NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }
    if (!NT_SUCCESS(Status))
        goto Exit;

    if (OutputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE ||
        OutputBuffer->Count < 1 ||
        OutputBuffer->Argument[0].Type != ACPI_METHOD_ARGUMENT_INTEGER)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Exit;
    }

    *DescriptorRegister = (USHORT)OutputBuffer->Argument[0].Argument;
    Status = (*DescriptorRegister != 0) ?
             STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;

Exit:
    ExFreePoolWithTag(OutputBuffer, HIDI2C_TAG);
    ExFreePoolWithTag(InputBuffer, HIDI2C_TAG);
    return Status;
}

static
NTSTATUS
Hidi2cFindResources(
    _In_opt_ PCM_RESOURCE_LIST Resources,
    _Out_ PLARGE_INTEGER ConnectionId,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR *InterruptResource)
{
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Connection, Interrupt;
    ULONG FullIndex;

    if (Resources == NULL)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    for (FullIndex = 0; FullIndex < Resources->Count; FullIndex++)
    {
        FullDescriptor = &Resources->List[FullIndex];
        if (FullDescriptor->PartialResourceList.Count < 2)
            continue;

        Connection = &FullDescriptor->PartialResourceList.PartialDescriptors[0];
        if (Connection->Type != CmResourceTypeConnection ||
            Connection->u.Connection.Class != CM_RESOURCE_CONNECTION_CLASS_SERIAL ||
            Connection->u.Connection.Type != CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
        {
            continue;
        }

        Interrupt = &FullDescriptor->PartialResourceList.PartialDescriptors[1];
        if (Interrupt->Type != CmResourceTypeInterrupt ||
            (Interrupt->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0)
        {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }

        ConnectionId->LowPart = Connection->u.Connection.IdLowPart;
        ConnectionId->HighPart = Connection->u.Connection.IdHighPart;
        *InterruptResource = Interrupt;
        return STATUS_SUCCESS;
    }

    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
BOOLEAN
NTAPI
Hidi2cInterruptService(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID Context)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = Context;

    UNREFERENCED_PARAMETER(Interrupt);

    if (InterlockedCompareExchange(&DeviceExtension->InterruptPending,
                                   TRUE,
                                   FALSE) == FALSE)
    {
        /*
         * HID-I2C uses a level interrupt which is cleared only by the SPB
         * read. Mask an exclusive line until the passive worker has drained
         * one report, matching threaded/one-shot GPIO interrupt semantics.
         */
        if (DeviceExtension->InterruptMode == LevelSensitive)
        {
            HalDisableSystemInterrupt(DeviceExtension->InterruptVector,
                                      DeviceExtension->InterruptIrql);
            InterlockedExchange(&DeviceExtension->InterruptMasked, TRUE);
        }
        KeSetEvent(&DeviceExtension->InputReadyEvent,
                   IO_NO_INCREMENT,
                   FALSE);
    }
    return TRUE;
}

static
NTSTATUS
Hidi2cConnectInterrupt(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor)
{
    NTSTATUS Status;

    InterlockedExchange(&DeviceExtension->InterruptPending, FALSE);
    InterlockedExchange(&DeviceExtension->InterruptMasked, FALSE);
    KeClearEvent(&DeviceExtension->InputReadyEvent);

    if (Descriptor->ShareDisposition != CmResourceShareDeviceExclusive ||
        (Descriptor->Flags & CM_RESOURCE_INTERRUPT_LEVEL_LATCHED_BITS) !=
            CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE)
    {
        DPRINT1("hidi2c: HID-I2C requires an exclusive level interrupt\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    DeviceExtension->InterruptVector = Descriptor->u.Interrupt.Vector;
    DeviceExtension->InterruptIrql = (KIRQL)Descriptor->u.Interrupt.Level;
    DeviceExtension->InterruptAffinity = Descriptor->u.Interrupt.Affinity;
    DeviceExtension->InterruptMode = LevelSensitive;
    Status = IoConnectInterrupt(&DeviceExtension->InterruptObject,
                                Hidi2cInterruptService,
                                DeviceExtension,
                                NULL,
                                DeviceExtension->InterruptVector,
                                DeviceExtension->InterruptIrql,
                                DeviceExtension->InterruptIrql,
                                DeviceExtension->InterruptMode,
                                FALSE,
                                DeviceExtension->InterruptAffinity,
                                FALSE);
    if (!NT_SUCCESS(Status))
        DeviceExtension->InterruptObject = NULL;
    return Status;
}

static
NTSTATUS
Hidi2cFinishInterrupt(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->InterruptObject == NULL ||
        InterlockedExchange(&DeviceExtension->InterruptPending, FALSE) == FALSE)
    {
        return STATUS_SUCCESS;
    }

    KeClearEvent(&DeviceExtension->InputReadyEvent);
    if (!DeviceExtension->StopThread &&
        InterlockedExchange(&DeviceExtension->InterruptMasked, FALSE) != FALSE &&
        !HalEnableSystemInterrupt(DeviceExtension->InterruptVector,
                                  DeviceExtension->InterruptIrql,
                                  DeviceExtension->InterruptMode))
    {
        DPRINT1("hidi2c: failed to re-enable interrupt vector %lu\n",
                DeviceExtension->InterruptVector);
        return STATUS_DEVICE_HARDWARE_ERROR;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
Hidi2cOpenConnection(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    WCHAR PathBuffer[RESOURCE_HUB_CONNECTION_PATH_CHARS];
    UNICODE_STRING Path;
    OBJECT_ATTRIBUTES ObjectAttributes;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    RtlInitEmptyUnicodeString(&Path, PathBuffer, sizeof(PathBuffer));
    Status = RESOURCE_HUB_CREATE_PATH_FROM_ID(
                 &Path,
                 DeviceExtension->ConnectionId.LowPart,
                 DeviceExtension->ConnectionId.HighPart);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               &Path,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    return ZwCreateFile(&DeviceExtension->SpbHandle,
                        GENERIC_READ | GENERIC_WRITE | SYNCHRONIZE,
                        &ObjectAttributes,
                        &IoStatusBlock,
                        NULL,
                        0,
                        FILE_SHARE_READ | FILE_SHARE_WRITE,
                        FILE_OPEN,
                        FILE_SYNCHRONOUS_IO_NONALERT,
                        NULL,
                        0);
}

static
ULONG
Hidi2cEncodeCommand(
    _Out_writes_(3) PUCHAR Buffer,
    _In_ UCHAR Opcode,
    _In_ UCHAR ReportType,
    _In_ UCHAR ReportId)
{
    if (ReportId < 0x0F)
    {
        Buffer[0] = (ReportType << 4) | ReportId;
        Buffer[1] = Opcode;
        return 2;
    }

    Buffer[0] = (ReportType << 4) | 0x0F;
    Buffer[1] = Opcode;
    Buffer[2] = ReportId;
    return 3;
}

static
NTSTATUS
Hidi2cPowerCommand(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR PowerState)
{
    ULONG Length;
    NTSTATUS Status;

    ExAcquireFastMutex(&DeviceExtension->IoMutex);
    Hidi2cWriteUshort(DeviceExtension->CommandBuffer,
                      DeviceExtension->I2cDescriptor.CommandRegister);
    Length = 2 + Hidi2cEncodeCommand(DeviceExtension->CommandBuffer + 2,
                                     HIDI2C_OPCODE_SET_POWER,
                                     0,
                                     PowerState);
    Status = Hidi2cSpbWrite(DeviceExtension,
                            DeviceExtension->CommandBuffer,
                            Length);
    ExReleaseFastMutex(&DeviceExtension->IoMutex);
    return Status;
}

static
NTSTATUS
Hidi2cResetDevice(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    LARGE_INTEGER Timeout;
    ULONGLONG Deadline, Now;
    ULONG Length;
    USHORT ResponseLength;
    NTSTATUS FinishStatus, Status;

    ExAcquireFastMutex(&DeviceExtension->IoMutex);
    Hidi2cWriteUshort(DeviceExtension->CommandBuffer,
                      DeviceExtension->I2cDescriptor.CommandRegister);
    Length = 2 + Hidi2cEncodeCommand(DeviceExtension->CommandBuffer + 2,
                                     HIDI2C_OPCODE_RESET,
                                     0,
                                     0);
    Status = Hidi2cSpbWrite(DeviceExtension,
                            DeviceExtension->CommandBuffer,
                            Length);
    ExReleaseFastMutex(&DeviceExtension->IoMutex);
    if (!NT_SUCCESS(Status))
        return Status;

    Deadline = KeQueryInterruptTime() + HIDI2C_RESET_TIMEOUT_100NS;
    for (;;)
    {
        if (!DeviceExtension->InterruptPending)
        {
            Now = KeQueryInterruptTime();
            if (Now >= Deadline)
                break;

            Timeout.QuadPart = -(LONGLONG)(Deadline - Now);
            Status = KeWaitForSingleObject(&DeviceExtension->InputReadyEvent,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           &Timeout);
            if (Status == STATUS_TIMEOUT)
                break;
            if (!NT_SUCCESS(Status))
                return Status;
            if (!DeviceExtension->InterruptPending)
            {
                KeClearEvent(&DeviceExtension->InputReadyEvent);
                continue;
            }
        }

        ExAcquireFastMutex(&DeviceExtension->IoMutex);
        Status = Hidi2cSpbRead(DeviceExtension,
                               DeviceExtension->InputBuffer,
                               DeviceExtension->I2cDescriptor.MaxInputLength);
        ExReleaseFastMutex(&DeviceExtension->IoMutex);
        FinishStatus = Hidi2cFinishInterrupt(DeviceExtension);
        if (!NT_SUCCESS(Status))
            return Status;
        if (!NT_SUCCESS(FinishStatus))
            return FinishStatus;

        ResponseLength = Hidi2cReadUshort(DeviceExtension->InputBuffer);
        if (ResponseLength == 0)
            return STATUS_SUCCESS;
        if (ResponseLength == 0xFFFF ||
            ResponseLength < sizeof(USHORT) ||
            ResponseLength > DeviceExtension->I2cDescriptor.MaxInputLength)
        {
            return STATUS_DEVICE_PROTOCOL_ERROR;
        }

        /* The protocol permits an input report before the reset acknowledgement. */
    }

    DPRINT1("hidi2c: reset acknowledgement timed out\n");
    return STATUS_IO_TIMEOUT;
}

NTSTATUS
Hidi2cSetDevicePower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN PowerOn)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidExtension->MiniDeviceExtension;
    NTSTATUS Status;

    if (!DeviceExtension->Started || DeviceExtension->SpbHandle == NULL)
        return STATUS_SUCCESS;

    if (!PowerOn)
        InterlockedExchange(&DeviceExtension->Powered, FALSE);

    Status = Hidi2cPowerCommand(DeviceExtension,
                                PowerOn ? HIDI2C_POWER_ON : HIDI2C_POWER_SLEEP);

    if (NT_SUCCESS(Status) && PowerOn)
    {
        InterlockedExchange(&DeviceExtension->Powered, TRUE);
    }
    return Status;
}

static
BOOLEAN
Hidi2cHasQueuedReads(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    KIRQL OldIrql;
    BOOLEAN HasReads;

    KeAcquireSpinLock(&DeviceExtension->ReadQueueLock, &OldIrql);
    HasReads = !IsListEmpty(&DeviceExtension->ReadQueue);
    if (!HasReads)
        KeClearEvent(&DeviceExtension->ReadQueueEvent);
    KeReleaseSpinLock(&DeviceExtension->ReadQueueLock, OldIrql);
    return HasReads;
}

static
NTSTATUS
NTAPI
Hidi2cCsqInsertIrpEx(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp,
    _In_opt_ PVOID InsertContext)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension =
        CONTAINING_RECORD(Csq, HIDI2C_DEVICE_EXTENSION, ReadCsq);

    UNREFERENCED_PARAMETER(InsertContext);

    if (DeviceExtension->StopThread || !DeviceExtension->Started)
        return STATUS_DEVICE_NOT_READY;

    if (InterlockedIncrement(&DeviceExtension->OutstandingReads) == 1)
        KeClearEvent(&DeviceExtension->ReadsDrainedEvent);
    InsertTailList(&DeviceExtension->ReadQueue,
                   &Irp->Tail.Overlay.ListEntry);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
Hidi2cCsqRemoveIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}

static
PIRP
NTAPI
Hidi2cCsqPeekNextIrp(
    _In_ PIO_CSQ Csq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension =
        CONTAINING_RECORD(Csq, HIDI2C_DEVICE_EXTENSION, ReadCsq);
    PLIST_ENTRY Entry;
    PIRP Candidate;
    UCHAR ExpectedReportId, ReportId;

    Entry = (Irp == NULL) ? DeviceExtension->ReadQueue.Flink :
                            Irp->Tail.Overlay.ListEntry.Flink;
    while (Entry != &DeviceExtension->ReadQueue)
    {
        Candidate = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        if (PeekContext == NULL)
            return Candidate;

        ReportId = *(PUCHAR)PeekContext;
        ExpectedReportId = DeviceExtension->UsesReportIds ?
                           *(PUCHAR)Candidate->UserBuffer : 0;
        if (ExpectedReportId == 0 || ExpectedReportId == ReportId)
            return Candidate;

        Entry = Entry->Flink;
    }
    return NULL;
}

static
VOID
NTAPI
Hidi2cCsqAcquireLock(
    _In_ PIO_CSQ Csq,
    _Out_ PKIRQL Irql)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension =
        CONTAINING_RECORD(Csq, HIDI2C_DEVICE_EXTENSION, ReadCsq);

    KeAcquireSpinLock(&DeviceExtension->ReadQueueLock, Irql);
}

static
VOID
NTAPI
Hidi2cCsqReleaseLock(
    _In_ PIO_CSQ Csq,
    _In_ KIRQL Irql)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension =
        CONTAINING_RECORD(Csq, HIDI2C_DEVICE_EXTENSION, ReadCsq);

    KeReleaseSpinLock(&DeviceExtension->ReadQueueLock, Irql);
}

static
VOID
NTAPI
Hidi2cCsqCompleteCanceledIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension =
        CONTAINING_RECORD(Csq, HIDI2C_DEVICE_EXTENSION, ReadCsq);

    KeSetEvent(&DeviceExtension->ReadQueueEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&DeviceExtension->InputReadyEvent, IO_NO_INCREMENT, FALSE);
    Hidi2cCompleteQueuedRead(DeviceExtension,
                            Irp,
                            STATUS_CANCELLED,
                            0);
}

NTSTATUS
Hidi2cInitializeTransport(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    ExInitializeFastMutex(&DeviceExtension->IoMutex);
    KeInitializeSpinLock(&DeviceExtension->ReadQueueLock);
    InitializeListHead(&DeviceExtension->ReadQueue);
    KeInitializeEvent(&DeviceExtension->ReadQueueEvent,
                      NotificationEvent,
                      FALSE);
    KeInitializeEvent(&DeviceExtension->ReadsDrainedEvent,
                      NotificationEvent,
                      TRUE);
    KeInitializeEvent(&DeviceExtension->InputReadyEvent,
                      NotificationEvent,
                      FALSE);
    return IoCsqInitializeEx(&DeviceExtension->ReadCsq,
                             Hidi2cCsqInsertIrpEx,
                             Hidi2cCsqRemoveIrp,
                             Hidi2cCsqPeekNextIrp,
                             Hidi2cCsqAcquireLock,
                             Hidi2cCsqReleaseLock,
                             Hidi2cCsqCompleteCanceledIrp);
}

static
PIRP
Hidi2cDequeueReadForReport(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR ReportId)
{
    return IoCsqRemoveNextIrp(&DeviceExtension->ReadCsq, &ReportId);
}

static
BOOLEAN
Hidi2cDescriptorUsesReportIds(
    _In_reads_bytes_(Length) const UCHAR *Descriptor,
    _In_ ULONG Length)
{
    ULONG Index = 0, ItemSize;
    UCHAR Prefix;

    while (Index < Length)
    {
        Prefix = Descriptor[Index++];
        if (Prefix == 0xFE)
        {
            if (Index + 2 > Length)
                return FALSE;
            ItemSize = Descriptor[Index];
            Index += 2;
        }
        else
        {
            ItemSize = Prefix & 0x03;
            if (ItemSize == 3)
                ItemSize = 4;
            if (((Prefix >> 2) & 0x03) == 1 &&
                ((Prefix >> 4) & 0x0F) == 8)
            {
                return TRUE;
            }
        }

        if (ItemSize > Length - Index)
            return FALSE;
        Index += ItemSize;
    }
    return FALSE;
}

static
VOID
Hidi2cCompleteRead(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
VOID
Hidi2cCompleteQueuedRead(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Hidi2cCompleteRead(Irp, Status, Information);
    if (InterlockedDecrement(&DeviceExtension->OutstandingReads) == 0)
        KeSetEvent(&DeviceExtension->ReadsDrainedEvent,
                   IO_NO_INCREMENT,
                   FALSE);
}

static
VOID
Hidi2cFlushQueuedReads(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension)
{
    PIRP Irp;

    for (;;)
    {
        while ((Irp = IoCsqRemoveNextIrp(&DeviceExtension->ReadCsq,
                                         NULL)) != NULL)
        {
            Hidi2cCompleteQueuedRead(DeviceExtension,
                                    Irp,
                                    STATUS_CANCELLED,
                                    0);
        }

        if (!Hidi2cHasQueuedReads(DeviceExtension))
            break;
        Hidi2cDelayMilliseconds(1);
    }
}

static
VOID
NTAPI
Hidi2cInputThread(
    _In_ PVOID Context)
{
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = Context;
    PIO_STACK_LOCATION IrpStack;
    PIRP Irp;
    ULONG OutputLength, PayloadLength;
    UCHAR ReportId;
    USHORT ResponseLength;
    NTSTATUS Status;

    for (;;)
    {
        if (DeviceExtension->StopThread)
            break;

        if (!Hidi2cHasQueuedReads(DeviceExtension))
        {
            KeWaitForSingleObject(&DeviceExtension->ReadQueueEvent,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            continue;
        }

        if (!DeviceExtension->InterruptPending)
        {
            KeWaitForSingleObject(&DeviceExtension->InputReadyEvent,
                                  Executive,
                                  KernelMode,
                                  FALSE,
                                  NULL);
            if (!DeviceExtension->InterruptPending)
                KeClearEvent(&DeviceExtension->InputReadyEvent);
            continue;
        }

        if (!DeviceExtension->Powered)
        {
            Hidi2cDelayMilliseconds(8);
            continue;
        }

        ExAcquireFastMutex(&DeviceExtension->IoMutex);
        Status = Hidi2cSpbRead(DeviceExtension,
                               DeviceExtension->InputBuffer,
                               DeviceExtension->I2cDescriptor.MaxInputLength);
        ExReleaseFastMutex(&DeviceExtension->IoMutex);
        if (!NT_SUCCESS(Hidi2cFinishInterrupt(DeviceExtension)))
        {
            InterlockedExchange(&DeviceExtension->StopThread, TRUE);
            break;
        }
        if (!NT_SUCCESS(Status))
        {
            Hidi2cDelayMilliseconds(8);
            continue;
        }

        ResponseLength = Hidi2cReadUshort(DeviceExtension->InputBuffer);
        if (ResponseLength == 0 || ResponseLength == 0xFFFF ||
            ResponseLength < sizeof(USHORT) ||
            ResponseLength > DeviceExtension->I2cDescriptor.MaxInputLength)
        {
            Hidi2cDelayMilliseconds(8);
            continue;
        }

        PayloadLength = ResponseLength - sizeof(USHORT);
        if (PayloadLength == 0)
            continue;

        ReportId = DeviceExtension->UsesReportIds ?
                   DeviceExtension->InputBuffer[sizeof(USHORT)] : 0;
        Irp = Hidi2cDequeueReadForReport(DeviceExtension, ReportId);
        if (Irp == NULL)
            continue;

        IrpStack = IoGetCurrentIrpStackLocation(Irp);
        OutputLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;
        if (Irp->Cancel)
        {
            Hidi2cCompleteQueuedRead(DeviceExtension,
                                    Irp,
                                    STATUS_CANCELLED,
                                    0);
        }
        else if (PayloadLength > OutputLength)
        {
            Hidi2cCompleteQueuedRead(DeviceExtension,
                                    Irp,
                                    STATUS_BUFFER_TOO_SMALL,
                                    0);
        }
        else
        {
            RtlCopyMemory(Irp->UserBuffer,
                          DeviceExtension->InputBuffer + sizeof(USHORT),
                          PayloadLength);
            Hidi2cCompleteQueuedRead(DeviceExtension,
                                    Irp,
                                    STATUS_SUCCESS,
                                    PayloadLength);
        }
    }

    Hidi2cFlushQueuedReads(DeviceExtension);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS
Hidi2cQueueReadReport(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidExtension->MiniDeviceExtension;
    NTSTATUS Status;

    if (!DeviceExtension->Started || DeviceExtension->StopThread)
    {
        Hidi2cCompleteRead(Irp, STATUS_DEVICE_NOT_READY, 0);
        return STATUS_DEVICE_NOT_READY;
    }

    if (Irp->UserBuffer == NULL ||
        IoGetCurrentIrpStackLocation(Irp)->Parameters.DeviceIoControl.OutputBufferLength == 0)
    {
        Hidi2cCompleteRead(Irp, STATUS_INVALID_PARAMETER, 0);
        return STATUS_INVALID_PARAMETER;
    }

    Irp->IoStatus.Status = STATUS_PENDING;
    Status = IoCsqInsertIrpEx(&DeviceExtension->ReadCsq,
                              Irp,
                              NULL,
                              NULL);
    if (!NT_SUCCESS(Status))
    {
        Hidi2cCompleteRead(Irp, Status, 0);
        return STATUS_PENDING;
    }

    KeSetEvent(&DeviceExtension->ReadQueueEvent, IO_NO_INCREMENT, FALSE);
    return STATUS_PENDING;
}

static
NTSTATUS
Hidi2cGetReport(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PHID_XFER_PACKET XferPacket,
    _In_ UCHAR ReportType)
{
    ULONG CommandLength, ResponseCapacity, PayloadLength;
    USHORT ResponseLength;
    NTSTATUS Status;

    ExAcquireFastMutex(&DeviceExtension->IoMutex);
    Hidi2cWriteUshort(DeviceExtension->CommandBuffer,
                      DeviceExtension->I2cDescriptor.CommandRegister);
    CommandLength = 2 + Hidi2cEncodeCommand(
                            DeviceExtension->CommandBuffer + 2,
                            HIDI2C_OPCODE_GET_REPORT,
                            ReportType,
                            XferPacket->reportId);
    Hidi2cWriteUshort(DeviceExtension->CommandBuffer + CommandLength,
                      DeviceExtension->I2cDescriptor.DataRegister);
    CommandLength += 2;

    ResponseCapacity = min(XferPacket->reportBufferLen,
                           DeviceExtension->TransferBufferSize - sizeof(USHORT)) +
                       sizeof(USHORT);
    Status = Hidi2cSpbSequence(DeviceExtension,
                               DeviceExtension->CommandBuffer,
                               CommandLength,
                               DeviceExtension->InputBuffer,
                               ResponseCapacity,
                               NULL);
    if (NT_SUCCESS(Status))
    {
        ResponseLength = Hidi2cReadUshort(DeviceExtension->InputBuffer);
        if (ResponseLength <= sizeof(USHORT))
        {
            XferPacket->reportBufferLen = 0;
        }
        else if (ResponseLength > ResponseCapacity)
        {
            Status = STATUS_DEVICE_PROTOCOL_ERROR;
        }
        else
        {
            PayloadLength = min((ULONG)ResponseLength - sizeof(USHORT),
                                XferPacket->reportBufferLen);
            RtlCopyMemory(XferPacket->reportBuffer,
                          DeviceExtension->InputBuffer + sizeof(USHORT),
                          PayloadLength);
            XferPacket->reportBufferLen = PayloadLength;
        }
    }
    ExReleaseFastMutex(&DeviceExtension->IoMutex);
    return Status;
}

static
NTSTATUS
Hidi2cSetReport(
    _Inout_ PHIDI2C_DEVICE_EXTENSION DeviceExtension,
    _In_ PHID_XFER_PACKET XferPacket,
    _In_ UCHAR ReportType,
    _In_ BOOLEAN UseCommandRegister)
{
    ULONG Length;
    NTSTATUS Status;

    if (XferPacket->reportBufferLen > MAXUSHORT - sizeof(USHORT) ||
        XferPacket->reportBufferLen > DeviceExtension->TransferBufferSize)
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    ExAcquireFastMutex(&DeviceExtension->IoMutex);
    if (UseCommandRegister)
    {
        Hidi2cWriteUshort(DeviceExtension->CommandBuffer,
                          DeviceExtension->I2cDescriptor.CommandRegister);
        Length = 2 + Hidi2cEncodeCommand(DeviceExtension->CommandBuffer + 2,
                                         HIDI2C_OPCODE_SET_REPORT,
                                         ReportType,
                                         XferPacket->reportId);
        Hidi2cWriteUshort(DeviceExtension->CommandBuffer + Length,
                          DeviceExtension->I2cDescriptor.DataRegister);
        Length += 2;
    }
    else
    {
        Hidi2cWriteUshort(DeviceExtension->CommandBuffer,
                          DeviceExtension->I2cDescriptor.OutputRegister);
        Length = 2;
    }

    Hidi2cWriteUshort(DeviceExtension->CommandBuffer + Length,
                      (USHORT)(XferPacket->reportBufferLen + sizeof(USHORT)));
    Length += 2;
    RtlCopyMemory(DeviceExtension->CommandBuffer + Length,
                  XferPacket->reportBuffer,
                  XferPacket->reportBufferLen);
    Length += XferPacket->reportBufferLen;
    Status = Hidi2cSpbWrite(DeviceExtension,
                            DeviceExtension->CommandBuffer,
                            Length);
    ExReleaseFastMutex(&DeviceExtension->IoMutex);
    return Status;
}

NTSTATUS
Hidi2cReportRequest(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ ULONG IoControlCode)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidExtension->MiniDeviceExtension;
    PHID_XFER_PACKET XferPacket = Irp->UserBuffer;
    NTSTATUS Status;

    if (!DeviceExtension->Started || !DeviceExtension->Powered)
    {
        Status = STATUS_DEVICE_NOT_READY;
    }
    else if (XferPacket == NULL || XferPacket->reportBuffer == NULL ||
             XferPacket->reportBufferLen == 0)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        switch (IoControlCode)
        {
            case IOCTL_HID_GET_INPUT_REPORT:
                Status = Hidi2cGetReport(DeviceExtension,
                                         XferPacket,
                                         HIDI2C_REPORT_INPUT);
                break;
            case IOCTL_HID_GET_FEATURE:
                Status = Hidi2cGetReport(DeviceExtension,
                                         XferPacket,
                                         HIDI2C_REPORT_FEATURE);
                break;
            case IOCTL_HID_SET_FEATURE:
                Status = Hidi2cSetReport(DeviceExtension,
                                         XferPacket,
                                         HIDI2C_REPORT_FEATURE,
                                         TRUE);
                break;
            case IOCTL_HID_SET_OUTPUT_REPORT:
                Status = Hidi2cSetReport(DeviceExtension,
                                         XferPacket,
                                         HIDI2C_REPORT_OUTPUT,
                                         TRUE);
                break;
            case IOCTL_HID_WRITE_REPORT:
                Status = (DeviceExtension->I2cDescriptor.MaxOutputLength != 0) ?
                         Hidi2cSetReport(DeviceExtension,
                                        XferPacket,
                                        HIDI2C_REPORT_OUTPUT,
                                        FALSE) :
                         Hidi2cSetReport(DeviceExtension,
                                        XferPacket,
                                        HIDI2C_REPORT_OUTPUT,
                                        TRUE);
                break;
            default:
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
        }
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = NT_SUCCESS(Status) && XferPacket != NULL ?
                                XferPacket->reportBufferLen : 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
Hidi2cStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_opt_ PCM_RESOURCE_LIST Resources)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidExtension->MiniDeviceExtension;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR InterruptResource;
    USHORT DescriptorRegister;
    ULONG BufferSize;
    NTSTATUS Status;

    InterlockedExchange(&DeviceExtension->StopThread, FALSE);

    Status = Hidi2cFindResources(Resources,
                                 &DeviceExtension->ConnectionId,
                                 &InterruptResource);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Hidi2cEvaluateDescriptorRegister(DeviceObject,
                                               &DescriptorRegister);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Hidi2cOpenConnection(DeviceExtension);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Hidi2cReadRegister(DeviceExtension,
                                DescriptorRegister,
                                &DeviceExtension->I2cDescriptor,
                                sizeof(DeviceExtension->I2cDescriptor));
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = Hidi2cValidateDeviceDescriptor(&DeviceExtension->I2cDescriptor);
    if (!NT_SUCCESS(Status))
        goto Failure;

    BufferSize = max((ULONG)DeviceExtension->I2cDescriptor.MaxInputLength,
                     (ULONG)DeviceExtension->I2cDescriptor.MaxOutputLength);
    BufferSize = max(BufferSize, (ULONG)HIDI2C_MIN_TRANSFER_BUFFER_SIZE);
    DeviceExtension->InputBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                                          BufferSize,
                                                          HIDI2C_TAG);
    DeviceExtension->CommandBuffer = ExAllocatePoolWithTag(
                                         NonPagedPool,
                                         BufferSize + HIDI2C_COMMAND_OVERHEAD,
                                         HIDI2C_TAG);
    DeviceExtension->ReportDescriptor = ExAllocatePoolWithTag(
                                            NonPagedPool,
                                            DeviceExtension->I2cDescriptor.ReportDescriptorLength,
                                            HIDI2C_TAG);
    if (DeviceExtension->InputBuffer == NULL ||
        DeviceExtension->CommandBuffer == NULL ||
        DeviceExtension->ReportDescriptor == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }
    DeviceExtension->TransferBufferSize = BufferSize;

    Status = Hidi2cConnectInterrupt(DeviceExtension, InterruptResource);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = Hidi2cPowerCommand(DeviceExtension, HIDI2C_POWER_ON);
    if (!NT_SUCCESS(Status))
        goto Failure;
    InterlockedExchange(&DeviceExtension->Powered, TRUE);

    Status = Hidi2cResetDevice(DeviceExtension);
    if (!NT_SUCCESS(Status))
        goto Failure;

    Status = Hidi2cReadRegister(
                 DeviceExtension,
                 DeviceExtension->I2cDescriptor.ReportDescriptorRegister,
                 DeviceExtension->ReportDescriptor,
                 DeviceExtension->I2cDescriptor.ReportDescriptorLength);
    if (!NT_SUCCESS(Status))
        goto Failure;

    DeviceExtension->UsesReportIds = Hidi2cDescriptorUsesReportIds(
                                         DeviceExtension->ReportDescriptor,
                                         DeviceExtension->I2cDescriptor.ReportDescriptorLength);

    RtlZeroMemory(&DeviceExtension->HidDescriptor,
                  sizeof(DeviceExtension->HidDescriptor));
    DeviceExtension->HidDescriptor.bLength = sizeof(HID_DESCRIPTOR);
    DeviceExtension->HidDescriptor.bDescriptorType = HID_HID_DESCRIPTOR_TYPE;
    DeviceExtension->HidDescriptor.bcdHID = 0x0111;
    DeviceExtension->HidDescriptor.bNumDescriptors = 1;
    DeviceExtension->HidDescriptor.DescriptorList[0].bReportType =
        HID_REPORT_DESCRIPTOR_TYPE;
    DeviceExtension->HidDescriptor.DescriptorList[0].wReportLength =
        DeviceExtension->I2cDescriptor.ReportDescriptorLength;

    InterlockedExchange(&DeviceExtension->StopThread, FALSE);
    InterlockedExchange(&DeviceExtension->Started, TRUE);
    InterlockedExchange(&DeviceExtension->Powered, TRUE);
    Status = PsCreateSystemThread(&DeviceExtension->InputThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  Hidi2cInputThread,
                                  DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&DeviceExtension->Started, FALSE);
        goto Failure;
    }

    DPRINT1("hidi2c: HID-I2C %04x:%04x, report descriptor %u bytes\n",
            DeviceExtension->I2cDescriptor.VendorId,
            DeviceExtension->I2cDescriptor.ProductId,
            DeviceExtension->I2cDescriptor.ReportDescriptorLength);
    return STATUS_SUCCESS;

Failure:
    Hidi2cStopDevice(DeviceObject);
    return Status;
}

VOID
Hidi2cStopDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PHID_DEVICE_EXTENSION HidExtension = DeviceObject->DeviceExtension;
    PHIDI2C_DEVICE_EXTENSION DeviceExtension = HidExtension->MiniDeviceExtension;

    KIRQL OldIrql;

    KeAcquireSpinLock(&DeviceExtension->ReadQueueLock, &OldIrql);
    InterlockedExchange(&DeviceExtension->StopThread, TRUE);
    KeReleaseSpinLock(&DeviceExtension->ReadQueueLock, OldIrql);
    KeSetEvent(&DeviceExtension->ReadQueueEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&DeviceExtension->InputReadyEvent, IO_NO_INCREMENT, FALSE);
    if (DeviceExtension->InputThreadHandle != NULL)
    {
        ZwWaitForSingleObject(DeviceExtension->InputThreadHandle,
                              FALSE,
                              NULL);
        ZwClose(DeviceExtension->InputThreadHandle);
        DeviceExtension->InputThreadHandle = NULL;
    }
    else
    {
        Hidi2cFlushQueuedReads(DeviceExtension);
    }
    KeWaitForSingleObject(&DeviceExtension->ReadsDrainedEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    if (DeviceExtension->InterruptObject != NULL)
    {
        IoDisconnectInterrupt(DeviceExtension->InterruptObject);
        DeviceExtension->InterruptObject = NULL;
    }

    if (DeviceExtension->SpbHandle != NULL)
    {
        if (DeviceExtension->Powered)
            Hidi2cPowerCommand(DeviceExtension, HIDI2C_POWER_SLEEP);
        ZwClose(DeviceExtension->SpbHandle);
        DeviceExtension->SpbHandle = NULL;
    }

    if (DeviceExtension->ReportDescriptor != NULL)
    {
        ExFreePoolWithTag(DeviceExtension->ReportDescriptor, HIDI2C_TAG);
        DeviceExtension->ReportDescriptor = NULL;
    }
    if (DeviceExtension->InputBuffer != NULL)
    {
        ExFreePoolWithTag(DeviceExtension->InputBuffer, HIDI2C_TAG);
        DeviceExtension->InputBuffer = NULL;
    }
    if (DeviceExtension->CommandBuffer != NULL)
    {
        ExFreePoolWithTag(DeviceExtension->CommandBuffer, HIDI2C_TAG);
        DeviceExtension->CommandBuffer = NULL;
    }

    DeviceExtension->TransferBufferSize = 0;
    InterlockedExchange(&DeviceExtension->Powered, FALSE);
    InterlockedExchange(&DeviceExtension->Started, FALSE);
}
