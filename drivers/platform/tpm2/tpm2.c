/*
 * PROJECT:     ReactOS TPM 2.0 Driver
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ACPI MSFT0101 CRB and FIFO hardware transport
 */

#include <ntddk.h>
#include <acpiioct.h>
#include <reactos/drivers/acpi/acpisystem.h>
#include <reactos/drivers/tpm2.h>

#define NDEBUG
#include <debug.h>

#define TPM2_TAG '2mpT'
#define TPM2_ACPI_TABLE_LIMIT 4096
#define TPM2_MAXIMUM_BUFFER_SIZE (1024 * 1024)
#define TPM2_HEADER_SIZE 10
#define TPM2_READY_TIMEOUT_MS 2000
#define TPM2_COMMAND_TIMEOUT_MS 90000
#define TPM2_START_METHOD_ACPI 2
#define TPM2_START_METHOD_TIS 6
#define TPM2_START_METHOD_CRB 7
#define TPM2_START_METHOD_CRB_ACPI 8
#define TPM2_RC_INITIALIZE 0x00000100
#define TPM2_CC_STARTUP 0x00000144
#define TPM2_CC_GET_CAPABILITY 0x0000017A
#define TPM2_CAP_TPM_PROPERTIES 0x00000006
#define TPM2_PT_MANUFACTURER 0x00000105
#define TPM2_PT_FIRMWARE_VERSION_1 0x0000010B
#define TPM2_PT_FIRMWARE_VERSION_2 0x0000010C

#define CRB_LOC_STATE 0x00
#define CRB_LOC_CTRL 0x08
#define CRB_LOC_STATE_ASSIGNED 0x02
#define CRB_LOC_STATE_ACTIVE_MASK 0x1C
#define CRB_LOC_STATE_VALID 0x80
#define CRB_LOC_CTRL_REQUEST_ACCESS 0x01
#define CRB_LOC_CTRL_RELINQUISH 0x02
#define CRB_CTRL_REQ 0x00
#define CRB_CTRL_STS 0x04
#define CRB_CTRL_CANCEL 0x08
#define CRB_CTRL_START 0x0C
#define CRB_CTRL_CMD_SIZE 0x18
#define CRB_CTRL_CMD_ADDRESS_LOW 0x1C
#define CRB_CTRL_CMD_ADDRESS_HIGH 0x20
#define CRB_CTRL_RESPONSE_SIZE 0x24
#define CRB_CTRL_RESPONSE_ADDRESS_LOW 0x28
#define CRB_CTRL_RESPONSE_ADDRESS_HIGH 0x2C
#define CRB_CTRL_REQ_COMMAND_READY 0x01
#define CRB_CTRL_REQ_GO_IDLE 0x02
#define CRB_CTRL_STS_ERROR 0x01
#define CRB_CTRL_START_INVOKE 0x01
#define CRB_CTRL_CANCEL_INVOKE 0x01
#define CRB_CONTROL_AREA_SIZE 0x30
#define CRB_HEAD_SIZE 0x40

#define TIS_ACCESS 0x0000
#define TIS_STATUS 0x0018
#define TIS_DATA_FIFO 0x0024
#define TIS_DID_VID 0x0F00
#define TIS_REGISTER_SPACE 0x1000
#define TIS_ACCESS_VALID 0x80
#define TIS_ACCESS_ACTIVE_LOCALITY 0x20
#define TIS_ACCESS_REQUEST_USE 0x02
#define TIS_STATUS_VALID 0x80
#define TIS_STATUS_COMMAND_READY 0x40
#define TIS_STATUS_GO 0x20
#define TIS_STATUS_DATA_AVAILABLE 0x10
#define TIS_STATUS_DATA_EXPECT 0x08

#define TPM2_RESOURCE_RANGE_COUNT 8

typedef struct _TPM2_RESOURCE_RANGE
{
    ULONGLONG Start;
    ULONGLONG Length;
} TPM2_RESOURCE_RANGE, *PTPM2_RESOURCE_RANGE;

typedef struct _TPM2_DEVICE_EXTENSION
{
    PDEVICE_OBJECT Self;
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    KMUTEX CommandMutex;
    UNICODE_STRING InterfaceName;
    TPM2_RESOURCE_RANGE ResourceRanges[TPM2_RESOURCE_RANGE_COUNT];
    ULONG ResourceRangeCount;
    PVOID ControlArea;
    PVOID LocalityArea;
    PVOID CommandBuffer;
    PVOID ResponseBuffer;
    PVOID TisRegisters;
    ULONG CommandSize;
    ULONG ResponseSize;
    ULONG StartMethod;
    ULONG InterfaceType;
    ULONG Manufacturer;
    ULONG FirmwareVersion1;
    ULONG FirmwareVersion2;
    volatile LONG CommandActive;
    BOOLEAN SharedCommandResponseBuffer;
    BOOLEAN Started;
} TPM2_DEVICE_EXTENSION, *PTPM2_DEVICE_EXTENSION;

static const UCHAR Tpm2AcpiStartUuid[16] =
{
    0xAB, 0x6C, 0xBF, 0x6B, 0x63, 0x54, 0x14, 0x47,
    0xB7, 0xCD, 0xF0, 0x20, 0x3C, 0x03, 0x68, 0xD4
};

static
ULONG
Tpm2ReadBigEndian32(
    _In_reads_(4) const UCHAR *Buffer)
{
    return ((ULONG)Buffer[0] << 24) | ((ULONG)Buffer[1] << 16) | ((ULONG)Buffer[2] << 8) | Buffer[3];
}

static
VOID
Tpm2WriteBigEndian32(
    _Out_writes_(4) UCHAR *Buffer,
    _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)(Value >> 24);
    Buffer[1] = (UCHAR)(Value >> 16);
    Buffer[2] = (UCHAR)(Value >> 8);
    Buffer[3] = (UCHAR)Value;
}

static
VOID
Tpm2DelayOneMillisecond(VOID)
{
    LARGE_INTEGER Interval;

    Interval.QuadPart = -10000;
    KeDelayExecutionThread(KernelMode, FALSE, &Interval);
}

static
BOOLEAN
Tpm2WaitRegister32(
    _In_ PULONG Register,
    _In_ ULONG Mask,
    _In_ ULONG Expected,
    _In_ ULONG TimeoutMilliseconds)
{
    ULONGLONG Deadline;

    Deadline = KeQueryInterruptTime() + (ULONGLONG)TimeoutMilliseconds * 10000;
    do
    {
        if ((READ_REGISTER_ULONG(Register) & Mask) == Expected)
            return TRUE;
        Tpm2DelayOneMillisecond();
    } while (KeQueryInterruptTime() < Deadline);
    return (READ_REGISTER_ULONG(Register) & Mask) == Expected;
}

static
BOOLEAN
Tpm2WaitRegister8(
    _In_ PUCHAR Register,
    _In_ UCHAR Mask,
    _In_ UCHAR Expected,
    _In_ ULONG TimeoutMilliseconds)
{
    ULONGLONG Deadline;

    Deadline = KeQueryInterruptTime() + (ULONGLONG)TimeoutMilliseconds * 10000;
    do
    {
        if ((READ_REGISTER_UCHAR(Register) & Mask) == Expected)
            return TRUE;
        Tpm2DelayOneMillisecond();
    } while (KeQueryInterruptTime() < Deadline);
    return (READ_REGISTER_UCHAR(Register) & Mask) == Expected;
}

static
BOOLEAN
Tpm2GetResourceRemaining(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG Address,
    _Out_ PULONGLONG Remaining)
{
    ULONG Index;

    for (Index = 0; Index < DeviceExtension->ResourceRangeCount; Index++)
    {
        PTPM2_RESOURCE_RANGE Range = &DeviceExtension->ResourceRanges[Index];
        if (Address >= Range->Start && Address - Range->Start < Range->Length)
        {
            *Remaining = Range->Length - (Address - Range->Start);
            return TRUE;
        }
    }
    *Remaining = 0;
    return FALSE;
}

static
PVOID
Tpm2MapPhysicalRegion(
    _In_ ULONGLONG Address,
    _In_ ULONG Length)
{
    PHYSICAL_ADDRESS PhysicalAddress;

    if (Length == 0 || Address > MAXULONGLONG - Length + 1)
        return NULL;
    PhysicalAddress.QuadPart = Address;
    return MmMapIoSpace(PhysicalAddress, Length, MmNonCached);
}

static
VOID
Tpm2UnmapHardware(
    _Inout_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->TisRegisters)
        MmUnmapIoSpace(DeviceExtension->TisRegisters, TIS_REGISTER_SPACE);
    if (DeviceExtension->ResponseBuffer && !DeviceExtension->SharedCommandResponseBuffer)
        MmUnmapIoSpace(DeviceExtension->ResponseBuffer, DeviceExtension->ResponseSize);
    if (DeviceExtension->CommandBuffer)
        MmUnmapIoSpace(DeviceExtension->CommandBuffer, DeviceExtension->CommandSize);
    if (DeviceExtension->LocalityArea)
        MmUnmapIoSpace(DeviceExtension->LocalityArea, CRB_HEAD_SIZE);
    if (DeviceExtension->ControlArea)
        MmUnmapIoSpace(DeviceExtension->ControlArea, CRB_CONTROL_AREA_SIZE);
    DeviceExtension->TisRegisters = NULL;
    DeviceExtension->ResponseBuffer = NULL;
    DeviceExtension->CommandBuffer = NULL;
    DeviceExtension->LocalityArea = NULL;
    DeviceExtension->ControlArea = NULL;
    DeviceExtension->SharedCommandResponseBuffer = FALSE;
    DeviceExtension->Started = FALSE;
}

static
NTSTATUS
NTAPI
Tpm2Completion(
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
Tpm2ForwardSynchronously(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, Tpm2Completion, &Event, TRUE, TRUE, TRUE);
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
Tpm2SendIoctlSynchronously(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID OutputBuffer,
    _In_ ULONG OutputLength,
    _Out_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    *Information = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, DeviceObject, InputBuffer, InputLength, OutputBuffer, OutputLength, FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    *Information = IoStatus.Information;
    return Status;
}

static
NTSTATUS
Tpm2ReadAcpiTable(
    _Outptr_result_bytebuffer_(*TableLength) PUCHAR *TableBuffer,
    _Out_ PULONG TableLength)
{
    ACPI_GET_SYSTEM_TABLE_INPUT Input;
    PFILE_OBJECT FileObject = NULL;
    PDEVICE_OBJECT DeviceObject = NULL;
    UNICODE_STRING InterfaceName;
    PWSTR InterfaceList = NULL;
    PUCHAR Buffer = NULL;
    ULONG BufferLength = 256;
    ULONG_PTR Information;
    NTSTATUS Status;

    *TableBuffer = NULL;
    *TableLength = 0;
    Status = IoGetDeviceInterfaces(&GUID_ACPI_SYSTEM_INTERFACE, NULL, 0, &InterfaceList);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!InterfaceList || InterfaceList[0] == UNICODE_NULL)
    {
        Status = STATUS_NOT_FOUND;
        goto Cleanup;
    }
    RtlInitUnicodeString(&InterfaceName, InterfaceList);
    Status = IoGetDeviceObjectPointer(&InterfaceName, FILE_READ_DATA | SYNCHRONIZE, &FileObject, &DeviceObject);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    RtlCopyMemory(Input.Signature, "TPM2", sizeof(Input.Signature));
    Input.Instance = 1;
    for (;;)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, BufferLength, TPM2_TAG);
        if (!Buffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }
        Status = Tpm2SendIoctlSynchronously(DeviceObject, IOCTL_ACPI_GET_SYSTEM_TABLE, &Input, sizeof(Input), Buffer, BufferLength, &Information);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            break;
        ExFreePoolWithTag(Buffer, TPM2_TAG);
        Buffer = NULL;
        if (Information <= BufferLength || Information > TPM2_ACPI_TABLE_LIMIT)
        {
            Status = STATUS_ACPI_INVALID_DATA;
            break;
        }
        BufferLength = (ULONG)Information;
    }
    if (NT_SUCCESS(Status))
    {
        if (Information < 52 || Information > BufferLength)
            Status = STATUS_ACPI_INVALID_DATA;
        else
        {
            *TableBuffer = Buffer;
            *TableLength = (ULONG)Information;
            Buffer = NULL;
        }
    }

Cleanup:
    if (Buffer)
        ExFreePoolWithTag(Buffer, TPM2_TAG);
    if (FileObject)
        ObDereferenceObject(FileObject);
    if (InterfaceList)
        ExFreePool(InterfaceList);
    return Status;
}

static
NTSTATUS
Tpm2EvaluateAcpiStartRevision(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Revision)
{
    ULONG InputStorage[32];
    ULONG OutputStorage[16];
    PACPI_EVAL_INPUT_BUFFER_COMPLEX Input;
    PACPI_EVAL_OUTPUT_BUFFER Output;
    PACPI_METHOD_ARGUMENT Argument;
    ULONG_PTR Information;
    NTSTATUS Status;

    RtlZeroMemory(InputStorage, sizeof(InputStorage));
    RtlZeroMemory(OutputStorage, sizeof(OutputStorage));
    Input = (PACPI_EVAL_INPUT_BUFFER_COMPLEX)InputStorage;
    Output = (PACPI_EVAL_OUTPUT_BUFFER)OutputStorage;
    Input->Signature = ACPI_EVAL_INPUT_BUFFER_COMPLEX_SIGNATURE;
    Input->MethodNameAsUlong = 'MSD_';
    Input->ArgumentCount = 4;
    Argument = Input->Argument;
    ACPI_METHOD_SET_ARGUMENT_BUFFER(Argument, Tpm2AcpiStartUuid, sizeof(Tpm2AcpiStartUuid));
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, Revision);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    ACPI_METHOD_SET_ARGUMENT_INTEGER(Argument, 1);
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    Argument->Type = ACPI_METHOD_ARGUMENT_PACKAGE;
    Argument->DataLength = 0;
    Argument->Argument = 0;
    Argument = ACPI_METHOD_NEXT_ARGUMENT(Argument);
    Input->Size = (ULONG)((PUCHAR)Argument - (PUCHAR)Input);
    Status = Tpm2SendIoctlSynchronously(DeviceExtension->LowerDevice, IOCTL_ACPI_EVAL_METHOD, Input, Input->Size, Output, sizeof(OutputStorage), &Information);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Information < FIELD_OFFSET(ACPI_EVAL_OUTPUT_BUFFER, Argument) || Output->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE || Output->Count != 1 || Output->Argument[0].Type != ACPI_METHOD_ARGUMENT_INTEGER)
        return STATUS_ACPI_INVALID_DATA;
    return Output->Argument[0].Argument == 0 ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static
NTSTATUS
Tpm2EvaluateAcpiStart(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    NTSTATUS Status;

    Status = Tpm2EvaluateAcpiStartRevision(DeviceExtension, 1);
    if (!NT_SUCCESS(Status))
        Status = Tpm2EvaluateAcpiStartRevision(DeviceExtension, 0);
    return Status;
}

static
NTSTATUS
Tpm2CrbRequestLocality(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    PULONG State;
    PULONG Control;
    ULONG Mask;

    if (!DeviceExtension->LocalityArea)
        return STATUS_SUCCESS;
    State = (PULONG)((PUCHAR)DeviceExtension->LocalityArea + CRB_LOC_STATE);
    Control = (PULONG)((PUCHAR)DeviceExtension->LocalityArea + CRB_LOC_CTRL);
    Mask = CRB_LOC_STATE_VALID | CRB_LOC_STATE_ASSIGNED | CRB_LOC_STATE_ACTIVE_MASK;
    if ((READ_REGISTER_ULONG(State) & Mask) == (CRB_LOC_STATE_VALID | CRB_LOC_STATE_ASSIGNED))
        return STATUS_SUCCESS;
    WRITE_REGISTER_ULONG(Control, CRB_LOC_CTRL_REQUEST_ACCESS);
    return Tpm2WaitRegister32(State, Mask, CRB_LOC_STATE_VALID | CRB_LOC_STATE_ASSIGNED, TPM2_READY_TIMEOUT_MS) ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

static
VOID
Tpm2CrbRelinquishLocality(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    PULONG State;
    PULONG Control;
    ULONG Mask;

    if (!DeviceExtension->LocalityArea)
        return;
    State = (PULONG)((PUCHAR)DeviceExtension->LocalityArea + CRB_LOC_STATE);
    Control = (PULONG)((PUCHAR)DeviceExtension->LocalityArea + CRB_LOC_CTRL);
    Mask = CRB_LOC_STATE_VALID | CRB_LOC_STATE_ASSIGNED;
    WRITE_REGISTER_ULONG(Control, CRB_LOC_CTRL_RELINQUISH);
    if (!Tpm2WaitRegister32(State, Mask, CRB_LOC_STATE_VALID, TPM2_READY_TIMEOUT_MS))
        DPRINT1("TPM2: locality relinquish timed out\n");
}

static
NTSTATUS
Tpm2CrbCommandReady(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    PULONG Request;

    if (DeviceExtension->StartMethod != TPM2_START_METHOD_CRB)
        return STATUS_SUCCESS;
    Request = (PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_REQ);
    WRITE_REGISTER_ULONG(Request, CRB_CTRL_REQ_COMMAND_READY);
    return Tpm2WaitRegister32(Request, CRB_CTRL_REQ_COMMAND_READY, 0, TPM2_READY_TIMEOUT_MS) ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

static
VOID
Tpm2CrbGoIdle(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    PULONG Request;

    if (DeviceExtension->StartMethod != TPM2_START_METHOD_CRB)
        return;
    Request = (PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_REQ);
    WRITE_REGISTER_ULONG(Request, CRB_CTRL_REQ_GO_IDLE);
    if (!Tpm2WaitRegister32(Request, CRB_CTRL_REQ_GO_IDLE, 0, TPM2_READY_TIMEOUT_MS))
        DPRINT1("TPM2: CRB go-idle timed out\n");
}

static
VOID
Tpm2WriteMmioBuffer(
    _In_ PVOID Destination,
    _In_reads_bytes_(Length) const UCHAR *Source,
    _In_ ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
        WRITE_REGISTER_UCHAR((PUCHAR)Destination + Index, Source[Index]);
}

static
VOID
Tpm2ReadMmioBuffer(
    _Out_writes_bytes_(Length) UCHAR *Destination,
    _In_ PVOID Source,
    _In_ ULONG Length)
{
    ULONG Index;

    for (Index = 0; Index < Length; Index++)
        Destination[Index] = READ_REGISTER_UCHAR((PUCHAR)Source + Index);
}

static
NTSTATUS
Tpm2CrbSubmit(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(CommandLength) const UCHAR *Command,
    _In_ ULONG CommandLength,
    _Out_writes_bytes_(OutputLength) UCHAR *Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG ResponseLength)
{
    PULONG Cancel;
    PULONG Start;
    PULONG StatusRegister;
    ULONG Length;
    NTSTATUS Status;

    *ResponseLength = 0;
    if (CommandLength > DeviceExtension->CommandSize)
        return STATUS_INVALID_BUFFER_SIZE;
    Status = Tpm2CrbRequestLocality(DeviceExtension);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = Tpm2CrbCommandReady(DeviceExtension);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Cancel = (PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_CANCEL);
    Start = (PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_START);
    StatusRegister = (PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_STS);
    if (!Tpm2WaitRegister32(Start, CRB_CTRL_START_INVOKE, 0, TPM2_READY_TIMEOUT_MS))
    {
        Status = STATUS_DEVICE_BUSY;
        goto Cleanup;
    }
    WRITE_REGISTER_ULONG(Cancel, 0);
    Tpm2WriteMmioBuffer(DeviceExtension->CommandBuffer, Command, CommandLength);
    KeMemoryBarrier();
    InterlockedExchange(&DeviceExtension->CommandActive, 1);
    WRITE_REGISTER_ULONG(Start, CRB_CTRL_START_INVOKE);
    if (DeviceExtension->StartMethod == TPM2_START_METHOD_ACPI || DeviceExtension->StartMethod == TPM2_START_METHOD_CRB_ACPI)
    {
        Status = Tpm2EvaluateAcpiStart(DeviceExtension);
        if (!NT_SUCCESS(Status))
        {
            WRITE_REGISTER_ULONG(Start, 0);
            goto Finished;
        }
    }
    if (!Tpm2WaitRegister32(Start, CRB_CTRL_START_INVOKE, 0, TPM2_COMMAND_TIMEOUT_MS))
    {
        WRITE_REGISTER_ULONG(Cancel, CRB_CTRL_CANCEL_INVOKE);
        Status = STATUS_IO_TIMEOUT;
        goto Finished;
    }
    if (READ_REGISTER_ULONG(StatusRegister) & CRB_CTRL_STS_ERROR)
    {
        Status = STATUS_DEVICE_HARDWARE_ERROR;
        goto Finished;
    }
    if (DeviceExtension->ResponseSize < TPM2_HEADER_SIZE)
    {
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
        goto Finished;
    }
    if (OutputLength < TPM2_HEADER_SIZE)
    {
        *ResponseLength = TPM2_HEADER_SIZE;
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Finished;
    }
    Tpm2ReadMmioBuffer(Output, DeviceExtension->ResponseBuffer, TPM2_HEADER_SIZE);
    Length = Tpm2ReadBigEndian32(Output + 2);
    *ResponseLength = Length;
    if (Length < TPM2_HEADER_SIZE || Length > DeviceExtension->ResponseSize)
    {
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
        goto Finished;
    }
    if (Length > OutputLength)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Finished;
    }
    if (Length > TPM2_HEADER_SIZE)
        Tpm2ReadMmioBuffer(Output + TPM2_HEADER_SIZE, (PUCHAR)DeviceExtension->ResponseBuffer + TPM2_HEADER_SIZE, Length - TPM2_HEADER_SIZE);
    Status = STATUS_SUCCESS;

Finished:
    InterlockedExchange(&DeviceExtension->CommandActive, 0);
    WRITE_REGISTER_ULONG(Cancel, 0);
Cleanup:
    Tpm2CrbGoIdle(DeviceExtension);
    Tpm2CrbRelinquishLocality(DeviceExtension);
    return Status;
}

static
NTSTATUS
Tpm2TisRequestLocality(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    PUCHAR Access = (PUCHAR)DeviceExtension->TisRegisters + TIS_ACCESS;
    UCHAR Expected = TIS_ACCESS_VALID | TIS_ACCESS_ACTIVE_LOCALITY;

    if ((READ_REGISTER_UCHAR(Access) & Expected) == Expected)
        return STATUS_SUCCESS;
    WRITE_REGISTER_UCHAR(Access, TIS_ACCESS_REQUEST_USE);
    return Tpm2WaitRegister8(Access, Expected, Expected, TPM2_READY_TIMEOUT_MS) ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

static
VOID
Tpm2TisRelinquishLocality(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    PUCHAR Access = (PUCHAR)DeviceExtension->TisRegisters + TIS_ACCESS;

    WRITE_REGISTER_UCHAR(Access, TIS_ACCESS_ACTIVE_LOCALITY);
}

static
NTSTATUS
Tpm2TisCommandReady(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    PUCHAR StatusRegister = (PUCHAR)DeviceExtension->TisRegisters + TIS_STATUS;
    UCHAR Expected = TIS_STATUS_VALID | TIS_STATUS_COMMAND_READY;

    WRITE_REGISTER_UCHAR(StatusRegister, TIS_STATUS_COMMAND_READY);
    return Tpm2WaitRegister8(StatusRegister, Expected, Expected, TPM2_READY_TIMEOUT_MS) ? STATUS_SUCCESS : STATUS_IO_TIMEOUT;
}

static
NTSTATUS
Tpm2TisGetBurstCount(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _Out_ PUSHORT BurstCount)
{
    PULONG StatusRegister = (PULONG)((PUCHAR)DeviceExtension->TisRegisters + TIS_STATUS);
    ULONGLONG Deadline;
    ULONG Value;

    Deadline = KeQueryInterruptTime() + (ULONGLONG)TPM2_READY_TIMEOUT_MS * 10000;
    do
    {
        Value = READ_REGISTER_ULONG(StatusRegister);
        *BurstCount = (USHORT)((Value >> 8) & 0xFFFF);
        if (*BurstCount != 0)
            return STATUS_SUCCESS;
        Tpm2DelayOneMillisecond();
    } while (KeQueryInterruptTime() < Deadline);
    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
Tpm2TisWriteCommand(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(CommandLength) const UCHAR *Command,
    _In_ ULONG CommandLength)
{
    PUCHAR Fifo = (PUCHAR)DeviceExtension->TisRegisters + TIS_DATA_FIFO;
    PUCHAR StatusRegister = (PUCHAR)DeviceExtension->TisRegisters + TIS_STATUS;
    ULONG Offset = 0;
    USHORT BurstCount;
    ULONG Chunk;
    ULONG Index;
    NTSTATUS Status;

    while (Offset < CommandLength)
    {
        Status = Tpm2TisGetBurstCount(DeviceExtension, &BurstCount);
        if (!NT_SUCCESS(Status))
            return Status;
        Chunk = min((ULONG)BurstCount, CommandLength - Offset);
        for (Index = 0; Index < Chunk; Index++)
            WRITE_REGISTER_UCHAR(Fifo, Command[Offset + Index]);
        Offset += Chunk;
        if (Offset < CommandLength && !Tpm2WaitRegister8(StatusRegister, TIS_STATUS_VALID | TIS_STATUS_DATA_EXPECT, TIS_STATUS_VALID | TIS_STATUS_DATA_EXPECT, TPM2_READY_TIMEOUT_MS))
            return STATUS_DEVICE_PROTOCOL_ERROR;
    }
    if (!Tpm2WaitRegister8(StatusRegister, TIS_STATUS_VALID, TIS_STATUS_VALID, TPM2_READY_TIMEOUT_MS))
        return STATUS_IO_TIMEOUT;
    return (READ_REGISTER_UCHAR(StatusRegister) & TIS_STATUS_DATA_EXPECT) == 0 ? STATUS_SUCCESS : STATUS_DEVICE_PROTOCOL_ERROR;
}

static
NTSTATUS
Tpm2TisReadBytes(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(Length) UCHAR *Buffer,
    _In_ ULONG Length)
{
    PUCHAR Fifo = (PUCHAR)DeviceExtension->TisRegisters + TIS_DATA_FIFO;
    ULONG Offset = 0;
    USHORT BurstCount;
    ULONG Chunk;
    ULONG Index;
    NTSTATUS Status;

    while (Offset < Length)
    {
        Status = Tpm2TisGetBurstCount(DeviceExtension, &BurstCount);
        if (!NT_SUCCESS(Status))
            return Status;
        Chunk = min((ULONG)BurstCount, Length - Offset);
        for (Index = 0; Index < Chunk; Index++)
            Buffer[Offset + Index] = READ_REGISTER_UCHAR(Fifo);
        Offset += Chunk;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
Tpm2TisSubmit(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(CommandLength) const UCHAR *Command,
    _In_ ULONG CommandLength,
    _Out_writes_bytes_(OutputLength) UCHAR *Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG ResponseLength)
{
    PUCHAR StatusRegister;
    ULONG Length;
    NTSTATUS Status;

    *ResponseLength = 0;
    Status = Tpm2TisRequestLocality(DeviceExtension);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = Tpm2TisCommandReady(DeviceExtension);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = Tpm2TisWriteCommand(DeviceExtension, Command, CommandLength);
    if (!NT_SUCCESS(Status))
        goto Ready;
    StatusRegister = (PUCHAR)DeviceExtension->TisRegisters + TIS_STATUS;
    InterlockedExchange(&DeviceExtension->CommandActive, 1);
    WRITE_REGISTER_UCHAR(StatusRegister, TIS_STATUS_GO);
    if (!Tpm2WaitRegister8(StatusRegister, TIS_STATUS_VALID | TIS_STATUS_DATA_AVAILABLE, TIS_STATUS_VALID | TIS_STATUS_DATA_AVAILABLE, TPM2_COMMAND_TIMEOUT_MS))
    {
        Status = STATUS_IO_TIMEOUT;
        goto Finished;
    }
    if (OutputLength < TPM2_HEADER_SIZE)
    {
        *ResponseLength = TPM2_HEADER_SIZE;
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Finished;
    }
    Status = Tpm2TisReadBytes(DeviceExtension, Output, TPM2_HEADER_SIZE);
    if (!NT_SUCCESS(Status))
        goto Finished;
    Length = Tpm2ReadBigEndian32(Output + 2);
    *ResponseLength = Length;
    if (Length < TPM2_HEADER_SIZE || Length > TPM2_MAXIMUM_BUFFER_SIZE)
    {
        Status = STATUS_DEVICE_PROTOCOL_ERROR;
        goto Finished;
    }
    if (Length > OutputLength)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Finished;
    }
    if (Length > TPM2_HEADER_SIZE)
        Status = Tpm2TisReadBytes(DeviceExtension, Output + TPM2_HEADER_SIZE, Length - TPM2_HEADER_SIZE);

Finished:
    InterlockedExchange(&DeviceExtension->CommandActive, 0);
Ready:
    Tpm2TisCommandReady(DeviceExtension);
Cleanup:
    Tpm2TisRelinquishLocality(DeviceExtension);
    return Status;
}

static
NTSTATUS
Tpm2SubmitHardware(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(CommandLength) const UCHAR *Command,
    _In_ ULONG CommandLength,
    _Out_writes_bytes_(OutputLength) UCHAR *Output,
    _In_ ULONG OutputLength,
    _Out_ PULONG ResponseLength)
{
    if (DeviceExtension->InterfaceType == TPM2_INTERFACE_CRB)
        return Tpm2CrbSubmit(DeviceExtension, Command, CommandLength, Output, OutputLength, ResponseLength);
    if (DeviceExtension->InterfaceType == TPM2_INTERFACE_TIS)
        return Tpm2TisSubmit(DeviceExtension, Command, CommandLength, Output, OutputLength, ResponseLength);
    return STATUS_INVALID_DEVICE_STATE;
}

static
VOID
Tpm2CancelHardware(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    if (InterlockedCompareExchange(&DeviceExtension->CommandActive, 1, 1) == 0)
        return;
    if (DeviceExtension->InterfaceType == TPM2_INTERFACE_CRB && DeviceExtension->ControlArea)
    {
        WRITE_REGISTER_ULONG((PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_CANCEL), CRB_CTRL_CANCEL_INVOKE);
        if (DeviceExtension->StartMethod == TPM2_START_METHOD_ACPI || DeviceExtension->StartMethod == TPM2_START_METHOD_CRB_ACPI)
            Tpm2EvaluateAcpiStart(DeviceExtension);
    }
    else if (DeviceExtension->InterfaceType == TPM2_INTERFACE_TIS && DeviceExtension->TisRegisters)
    {
        WRITE_REGISTER_UCHAR((PUCHAR)DeviceExtension->TisRegisters + TIS_STATUS, TIS_STATUS_COMMAND_READY);
    }
}

static
NTSTATUS
Tpm2QueryProperty(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONG Property,
    _Out_ PULONG Value,
    _Out_ PULONG TpmResult)
{
    UCHAR Command[22] = {0x80, 0x01, 0x00, 0x00, 0x00, 0x16, 0x00, 0x00, 0x01, 0x7A, 0x00, 0x00, 0x00, 0x06, 0, 0, 0, 0, 0, 0, 0, 1};
    UCHAR Response[64];
    ULONG ResponseLength;
    NTSTATUS Status;

    *Value = 0;
    *TpmResult = 0;
    Tpm2WriteBigEndian32(Command + 14, Property);
    Status = Tpm2SubmitHardware(DeviceExtension, Command, sizeof(Command), Response, sizeof(Response), &ResponseLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ResponseLength < TPM2_HEADER_SIZE)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    *TpmResult = Tpm2ReadBigEndian32(Response + 6);
    if (*TpmResult != 0)
        return STATUS_UNSUCCESSFUL;
    if (ResponseLength < 27 || Tpm2ReadBigEndian32(Response + 11) != TPM2_CAP_TPM_PROPERTIES || Tpm2ReadBigEndian32(Response + 15) == 0 || Tpm2ReadBigEndian32(Response + 19) != Property)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    *Value = Tpm2ReadBigEndian32(Response + 23);
    return STATUS_SUCCESS;
}

static
NTSTATUS
Tpm2Startup(
    _In_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    UCHAR Command[12] = {0x80, 0x01, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x01, 0x44, 0x00, 0x00};
    UCHAR Response[32];
    ULONG ResponseLength;
    ULONG TpmResult;
    NTSTATUS Status;

    Status = Tpm2SubmitHardware(DeviceExtension, Command, sizeof(Command), Response, sizeof(Response), &ResponseLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (ResponseLength < TPM2_HEADER_SIZE)
        return STATUS_DEVICE_PROTOCOL_ERROR;
    TpmResult = Tpm2ReadBigEndian32(Response + 6);
    return TpmResult == 0 || TpmResult == TPM2_RC_INITIALIZE ? STATUS_SUCCESS : STATUS_UNSUCCESSFUL;
}

static
NTSTATUS
Tpm2InitializeChip(
    _Inout_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    ULONG TpmResult;
    NTSTATUS Status;

    Status = Tpm2QueryProperty(DeviceExtension, TPM2_PT_MANUFACTURER, &DeviceExtension->Manufacturer, &TpmResult);
    if (!NT_SUCCESS(Status) && TpmResult == TPM2_RC_INITIALIZE)
    {
        Status = Tpm2Startup(DeviceExtension);
        if (NT_SUCCESS(Status))
            Status = Tpm2QueryProperty(DeviceExtension, TPM2_PT_MANUFACTURER, &DeviceExtension->Manufacturer, &TpmResult);
    }
    if (!NT_SUCCESS(Status))
        return Status;
    if (!NT_SUCCESS(Tpm2QueryProperty(DeviceExtension, TPM2_PT_FIRMWARE_VERSION_1, &DeviceExtension->FirmwareVersion1, &TpmResult)))
        DeviceExtension->FirmwareVersion1 = 0;
    if (!NT_SUCCESS(Tpm2QueryProperty(DeviceExtension, TPM2_PT_FIRMWARE_VERSION_2, &DeviceExtension->FirmwareVersion2, &TpmResult)))
        DeviceExtension->FirmwareVersion2 = 0;
    DPRINT1("TPM2: active interface %s, start method %lu, manufacturer 0x%08lx, firmware %08lx.%08lx\n",
            DeviceExtension->InterfaceType == TPM2_INTERFACE_CRB ? "CRB" : "TIS",
            DeviceExtension->StartMethod,
            DeviceExtension->Manufacturer,
            DeviceExtension->FirmwareVersion1,
            DeviceExtension->FirmwareVersion2);
    return STATUS_SUCCESS;
}

static
NTSTATUS
Tpm2CollectResources(
    _Inout_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _In_ PCM_RESOURCE_LIST Resources)
{
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Index;

    DeviceExtension->ResourceRangeCount = 0;
    if (!Resources || Resources->Count == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    PartialList = &Resources->List[0].PartialResourceList;
    for (Index = 0; Index < PartialList->Count; Index++)
    {
        Descriptor = &PartialList->PartialDescriptors[Index];
        if (Descriptor->Type != CmResourceTypeMemory || Descriptor->u.Memory.Length == 0)
            continue;
        if (DeviceExtension->ResourceRangeCount == TPM2_RESOURCE_RANGE_COUNT)
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        DeviceExtension->ResourceRanges[DeviceExtension->ResourceRangeCount].Start = Descriptor->u.Memory.Start.QuadPart;
        DeviceExtension->ResourceRanges[DeviceExtension->ResourceRangeCount].Length = Descriptor->u.Memory.Length;
        DeviceExtension->ResourceRangeCount++;
    }
    return DeviceExtension->ResourceRangeCount ? STATUS_SUCCESS : STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
BOOLEAN
Tpm2RangesOverlap(
    _In_ ULONGLONG FirstAddress,
    _In_ ULONG FirstLength,
    _In_ ULONGLONG SecondAddress,
    _In_ ULONG SecondLength)
{
    if (FirstAddress <= SecondAddress)
        return SecondAddress - FirstAddress < FirstLength;
    return FirstAddress - SecondAddress < SecondLength;
}

static
NTSTATUS
Tpm2StartCrb(
    _Inout_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG ControlAddress)
{
    ULONGLONG CommandAddress;
    ULONGLONG ResponseAddress;
    ULONGLONG Remaining;
    ULONGLONG ResourceStart;
    ULONG CommandSize;
    ULONG ResponseSize;
    ULONG Index;

    if (ControlAddress == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    DeviceExtension->ControlArea = Tpm2MapPhysicalRegion(ControlAddress, CRB_CONTROL_AREA_SIZE);
    if (!DeviceExtension->ControlArea)
        return STATUS_INSUFFICIENT_RESOURCES;
    CommandSize = READ_REGISTER_ULONG((PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_CMD_SIZE));
    CommandAddress = READ_REGISTER_ULONG((PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_CMD_ADDRESS_LOW));
    CommandAddress |= (ULONGLONG)READ_REGISTER_ULONG((PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_CMD_ADDRESS_HIGH)) << 32;
    ResponseSize = READ_REGISTER_ULONG((PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_RESPONSE_SIZE));
    ResponseAddress = READ_REGISTER_ULONG((PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_RESPONSE_ADDRESS_LOW));
    ResponseAddress |= (ULONGLONG)READ_REGISTER_ULONG((PULONG)((PUCHAR)DeviceExtension->ControlArea + CRB_CTRL_RESPONSE_ADDRESS_HIGH)) << 32;
    if (CommandSize < TPM2_HEADER_SIZE || ResponseSize < TPM2_HEADER_SIZE || CommandSize > TPM2_MAXIMUM_BUFFER_SIZE || ResponseSize > TPM2_MAXIMUM_BUFFER_SIZE || CommandAddress == 0 || ResponseAddress == 0)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (Tpm2GetResourceRemaining(DeviceExtension, CommandAddress, &Remaining) && CommandSize > Remaining)
    {
        DPRINT1("TPM2: command buffer exceeds ACPI resource, clamping %lu to %I64u\n", CommandSize, Remaining);
        CommandSize = (ULONG)Remaining;
    }
    if (Tpm2GetResourceRemaining(DeviceExtension, ResponseAddress, &Remaining) && ResponseSize > Remaining)
    {
        DPRINT1("TPM2: response buffer exceeds ACPI resource, clamping %lu to %I64u\n", ResponseSize, Remaining);
        ResponseSize = (ULONG)Remaining;
    }
    if (CommandSize < TPM2_HEADER_SIZE || ResponseSize < TPM2_HEADER_SIZE)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (CommandAddress == ResponseAddress && CommandSize != ResponseSize)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    if (CommandAddress != ResponseAddress && Tpm2RangesOverlap(CommandAddress, CommandSize, ResponseAddress, ResponseSize))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    DeviceExtension->CommandSize = CommandSize;
    DeviceExtension->ResponseSize = ResponseSize;
    DeviceExtension->CommandBuffer = Tpm2MapPhysicalRegion(CommandAddress, CommandSize);
    if (!DeviceExtension->CommandBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (CommandAddress == ResponseAddress)
    {
        DeviceExtension->ResponseBuffer = DeviceExtension->CommandBuffer;
        DeviceExtension->SharedCommandResponseBuffer = TRUE;
    }
    else
    {
        DeviceExtension->ResponseBuffer = Tpm2MapPhysicalRegion(ResponseAddress, ResponseSize);
        if (!DeviceExtension->ResponseBuffer)
            return STATUS_INSUFFICIENT_RESOURCES;
    }
    if (DeviceExtension->StartMethod == TPM2_START_METHOD_CRB)
    {
        for (Index = 0; Index < DeviceExtension->ResourceRangeCount; Index++)
        {
            ResourceStart = DeviceExtension->ResourceRanges[Index].Start;
            if (DeviceExtension->ResourceRanges[Index].Length >= CRB_HEAD_SIZE + CRB_CONTROL_AREA_SIZE && ControlAddress == ResourceStart + CRB_HEAD_SIZE)
            {
                DeviceExtension->LocalityArea = Tpm2MapPhysicalRegion(ResourceStart, CRB_HEAD_SIZE);
                break;
            }
        }
    }
    DeviceExtension->InterfaceType = TPM2_INTERFACE_CRB;
    return STATUS_SUCCESS;
}

static
NTSTATUS
Tpm2StartTis(
    _Inout_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    ULONGLONG Address;
    ULONG DidVid;

    if (DeviceExtension->ResourceRanges[0].Length < TIS_REGISTER_SPACE)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    Address = DeviceExtension->ResourceRanges[0].Start;
    DeviceExtension->TisRegisters = Tpm2MapPhysicalRegion(Address, TIS_REGISTER_SPACE);
    if (!DeviceExtension->TisRegisters)
        return STATUS_INSUFFICIENT_RESOURCES;
    DidVid = READ_REGISTER_ULONG((PULONG)((PUCHAR)DeviceExtension->TisRegisters + TIS_DID_VID));
    if (DidVid == 0 || DidVid == MAXULONG)
        return STATUS_DEVICE_HARDWARE_ERROR;
    DeviceExtension->CommandSize = TPM2_MAXIMUM_BUFFER_SIZE;
    DeviceExtension->ResponseSize = TPM2_MAXIMUM_BUFFER_SIZE;
    DeviceExtension->InterfaceType = TPM2_INTERFACE_TIS;
    DPRINT1("TPM2: TIS DID_VID 0x%08lx\n", DidVid);
    return STATUS_SUCCESS;
}

static
NTSTATUS
Tpm2StartHardware(
    _Inout_ PTPM2_DEVICE_EXTENSION DeviceExtension,
    _In_ PCM_RESOURCE_LIST Resources)
{
    PUCHAR Table = NULL;
    ULONG TableLength;
    ULONG DeclaredLength;
    ULONGLONG ControlAddress;
    UCHAR Checksum = 0;
    ULONG Index;
    NTSTATUS Status;

    Status = Tpm2CollectResources(DeviceExtension, Resources);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = Tpm2ReadAcpiTable(&Table, &TableLength);
    if (!NT_SUCCESS(Status))
        return Status;
    RtlCopyMemory(&DeclaredLength, Table + 4, sizeof(DeclaredLength));
    if (DeclaredLength < 52 || DeclaredLength > TableLength)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }
    for (Index = 0; Index < DeclaredLength; Index++)
        Checksum = (UCHAR)(Checksum + Table[Index]);
    if (Checksum != 0 || RtlCompareMemory(Table, "TPM2", 4) != 4)
    {
        Status = STATUS_ACPI_INVALID_DATA;
        goto Cleanup;
    }
    RtlCopyMemory(&ControlAddress, Table + 40, sizeof(ControlAddress));
    RtlCopyMemory(&DeviceExtension->StartMethod, Table + 48, sizeof(DeviceExtension->StartMethod));
    if (DeviceExtension->StartMethod == TPM2_START_METHOD_TIS)
        Status = Tpm2StartTis(DeviceExtension);
    else if (DeviceExtension->StartMethod == TPM2_START_METHOD_ACPI || DeviceExtension->StartMethod == TPM2_START_METHOD_CRB || DeviceExtension->StartMethod == TPM2_START_METHOD_CRB_ACPI)
        Status = Tpm2StartCrb(DeviceExtension, ControlAddress);
    else
        Status = STATUS_NOT_SUPPORTED;
    if (NT_SUCCESS(Status))
        Status = Tpm2InitializeChip(DeviceExtension);
    if (NT_SUCCESS(Status))
        DeviceExtension->Started = TRUE;
    else
        Tpm2UnmapHardware(DeviceExtension);

Cleanup:
    ExFreePoolWithTag(Table, TPM2_TAG);
    return Status;
}

static
NTSTATUS
NTAPI
Tpm2CreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

static
VOID
Tpm2StopHardware(
    _Inout_ PTPM2_DEVICE_EXTENSION DeviceExtension)
{
    DeviceExtension->Started = FALSE;
    Tpm2CancelHardware(DeviceExtension);
    KeWaitForSingleObject(&DeviceExtension->CommandMutex, Executive, KernelMode, FALSE, NULL);
    Tpm2UnmapHardware(DeviceExtension);
    KeReleaseMutex(&DeviceExtension->CommandMutex, FALSE);
}

static
NTSTATUS
NTAPI
Tpm2DeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PTPM2_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    PUCHAR Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLength = IrpStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG ResponseLength = 0;
    ULONG CommandLength;
    NTSTATUS Status;

    Irp->IoStatus.Information = 0;
    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        goto Complete;
    if (!DeviceExtension->Started)
    {
        Status = STATUS_DEVICE_NOT_READY;
        goto Release;
    }
    switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_TPM2_QUERY_INFORMATION:
            if (!Buffer || OutputLength < sizeof(TPM2_DEVICE_INFORMATION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            RtlZeroMemory(Buffer, sizeof(TPM2_DEVICE_INFORMATION));
            ((PTPM2_DEVICE_INFORMATION)Buffer)->Version = TPM2_DRIVER_INTERFACE_VERSION;
            ((PTPM2_DEVICE_INFORMATION)Buffer)->InterfaceType = DeviceExtension->InterfaceType;
            ((PTPM2_DEVICE_INFORMATION)Buffer)->StartMethod = DeviceExtension->StartMethod;
            ((PTPM2_DEVICE_INFORMATION)Buffer)->MaximumCommandSize = DeviceExtension->CommandSize;
            ((PTPM2_DEVICE_INFORMATION)Buffer)->MaximumResponseSize = DeviceExtension->ResponseSize;
            ((PTPM2_DEVICE_INFORMATION)Buffer)->Manufacturer = DeviceExtension->Manufacturer;
            ((PTPM2_DEVICE_INFORMATION)Buffer)->FirmwareVersion1 = DeviceExtension->FirmwareVersion1;
            ((PTPM2_DEVICE_INFORMATION)Buffer)->FirmwareVersion2 = DeviceExtension->FirmwareVersion2;
            Irp->IoStatus.Information = sizeof(TPM2_DEVICE_INFORMATION);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_TPM2_SUBMIT_COMMAND:
            if (!Buffer || InputLength < TPM2_HEADER_SIZE)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            CommandLength = Tpm2ReadBigEndian32(Buffer + 2);
            if (CommandLength != InputLength || CommandLength > DeviceExtension->CommandSize)
            {
                Status = STATUS_INVALID_BUFFER_SIZE;
                break;
            }
            KeWaitForSingleObject(&DeviceExtension->CommandMutex, Executive, KernelMode, FALSE, NULL);
            Status = Tpm2SubmitHardware(DeviceExtension, Buffer, CommandLength, Buffer, OutputLength, &ResponseLength);
            KeReleaseMutex(&DeviceExtension->CommandMutex, FALSE);
            Irp->IoStatus.Information = ResponseLength;
            break;

        case IOCTL_TPM2_CANCEL_COMMAND:
            Tpm2CancelHardware(DeviceExtension);
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

Release:
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
Complete:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
Tpm2Pnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PTPM2_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    switch (IrpStack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = Tpm2ForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = Tpm2StartHardware(DeviceExtension, IrpStack->Parameters.StartDevice.AllocatedResourcesTranslated);
            if (NT_SUCCESS(Status))
                Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, TRUE);
            if (!NT_SUCCESS(Status))
                Tpm2UnmapHardware(DeviceExtension);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
            Tpm2StopHardware(DeviceExtension);
            break;

        case IRP_MN_REMOVE_DEVICE:
            IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
            DeviceExtension->Started = FALSE;
            Tpm2CancelHardware(DeviceExtension);
            Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
            if (NT_SUCCESS(Status))
                IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Tpm2UnmapHardware(DeviceExtension);
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
            IoDeleteDevice(DeviceObject);
            return Status;
    }
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
Tpm2Power(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PTPM2_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
NTSTATUS
NTAPI
Tpm2AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PTPM2_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject, sizeof(TPM2_DEVICE_EXTENSION), NULL, FILE_DEVICE_UNKNOWN, FILE_DEVICE_SECURE_OPEN, FALSE, &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->Self = DeviceObject;
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    DeviceExtension->LowerDevice = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (!DeviceExtension->LowerDevice)
    {
        IoDeleteDevice(DeviceObject);
        return STATUS_NO_SUCH_DEVICE;
    }
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock, TPM2_TAG, 0, 0);
    KeInitializeMutex(&DeviceExtension->CommandMutex, 0);
    Status = IoRegisterDeviceInterface(PhysicalDeviceObject, &GUID_DEVINTERFACE_REACTOS_TPM2, NULL, &DeviceExtension->InterfaceName);
    if (!NT_SUCCESS(Status))
    {
        IoDetachDevice(DeviceExtension->LowerDevice);
        IoDeleteDevice(DeviceObject);
        return Status;
    }
    DeviceObject->Flags |= DO_BUFFERED_IO | DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);
    DriverObject->DriverExtension->AddDevice = Tpm2AddDevice;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = Tpm2CreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Tpm2CreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = Tpm2DeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = Tpm2Pnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = Tpm2Power;
    return STATUS_SUCCESS;
}
