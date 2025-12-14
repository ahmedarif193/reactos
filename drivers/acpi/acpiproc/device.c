/*
 * PROJECT:     ReactOS ACPI Processor Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ACPI helper routines for processor devices
 * COPYRIGHT:   2025 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "acpiproc.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
NTAPI
AcpiprocForwardIrpCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event = (PKEVENT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
AcpiprocForwardIrpAndWait(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           AcpiprocForwardIrpCompletion,
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
    else
    {
        Status = Irp->IoStatus.Status;
    }

    return Status;
}

NTSTATUS
AcpiprocSendAcpiIrp(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _In_ ULONG IoControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferLength,
    _Out_opt_ PVOID OutputBuffer,
    _In_ ULONG OutputBufferLength)
{
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        DeviceExtension->LowerDevice,
                                        InputBuffer,
                                        InputBufferLength,
                                        OutputBuffer,
                                        OutputBufferLength,
                                        FALSE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
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

    return Status;
}

VOID
AcpiprocCleanupUid(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    if (DeviceExtension->Uid.Valid &&
        DeviceExtension->Uid.IsString &&
        DeviceExtension->Uid.u.String.Buffer != NULL)
    {
        RtlFreeUnicodeString(&DeviceExtension->Uid.u.String);
    }

    RtlZeroMemory(&DeviceExtension->Uid, sizeof(DeviceExtension->Uid));
}

static
VOID
AcpiprocCopyMethodName(
    _Out_writes_(4) PUCHAR Destination,
    _In_reads_(4) PCSTR MethodName)
{
    SIZE_T Index;

    RtlZeroMemory(Destination, 4);

    for (Index = 0; Index < 4; ++Index)
    {
        CHAR Character = MethodName[Index];
        if (Character == '\0')
            break;

        Destination[Index] = Character;
    }
}

NTSTATUS
AcpiprocEvaluateIntegerMethod(
    _In_ PACPIPROC_DEVICE DeviceExtension,
    _In_reads_(4) PCSTR MethodName,
    _Out_ PULONG Value)
{
    ACPI_EVAL_INPUT_BUFFER InputBuffer;
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    NTSTATUS Status;

    RtlZeroMemory(&InputBuffer, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    AcpiprocCopyMethodName(InputBuffer.MethodName, MethodName);

    OutputLength = sizeof(ACPI_EVAL_OUTPUT_BUFFER) +
                   ACPI_METHOD_ARGUMENT_LENGTH(sizeof(ULONG));

    OutputBuffer = ExAllocatePoolWithTag(PagedPool,
                                         OutputLength,
                                         ACPIPROC_TAG);
    if (!OutputBuffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(OutputBuffer, OutputLength);

    Status = AcpiprocSendAcpiIrp(DeviceExtension,
                                 IOCTL_ACPI_EVAL_METHOD,
                                 &InputBuffer,
                                 sizeof(InputBuffer),
                                 OutputBuffer,
                                 OutputLength);
    if (NT_SUCCESS(Status))
    {
        if ((OutputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE) ||
            (OutputBuffer->Count == 0))
        {
            Status = STATUS_ACPI_INVALID_DATA;
        }
        else
        {
            PACPI_METHOD_ARGUMENT Argument = OutputBuffer->Argument;
            if (Argument->Type != ACPI_METHOD_ARGUMENT_INTEGER)
            {
                Status = STATUS_ACPI_INVALID_DATA;
            }
            else
            {
                *Value = Argument->Argument;
            }
        }
    }

    ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
    return Status;
}

NTSTATUS
AcpiprocQueryUid(
    _Inout_ PACPIPROC_DEVICE DeviceExtension)
{
    ACPI_EVAL_INPUT_BUFFER InputBuffer;
    PACPI_EVAL_OUTPUT_BUFFER OutputBuffer;
    ULONG OutputLength;
    PACPI_METHOD_ARGUMENT Argument;
    NTSTATUS Status;

    AcpiprocCleanupUid(DeviceExtension);

    RtlZeroMemory(&InputBuffer, sizeof(InputBuffer));
    InputBuffer.Signature = ACPI_EVAL_INPUT_BUFFER_SIGNATURE;
    AcpiprocCopyMethodName(InputBuffer.MethodName, "_UID");

    OutputLength = sizeof(ACPI_EVAL_OUTPUT_BUFFER) +
                   ACPI_METHOD_ARGUMENT_LENGTH(256);

    OutputBuffer = ExAllocatePoolWithTag(PagedPool,
                                         OutputLength,
                                         ACPIPROC_TAG);
    if (!OutputBuffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(OutputBuffer, OutputLength);

    Status = AcpiprocSendAcpiIrp(DeviceExtension,
                                 IOCTL_ACPI_EVAL_METHOD,
                                 &InputBuffer,
                                 sizeof(InputBuffer),
                                 OutputBuffer,
                                 OutputLength);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return Status;
    }

    if ((OutputBuffer->Signature != ACPI_EVAL_OUTPUT_BUFFER_SIGNATURE) ||
        (OutputBuffer->Count == 0))
    {
        ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
        return STATUS_ACPI_INVALID_DATA;
    }

    Argument = OutputBuffer->Argument;
    switch (Argument->Type)
    {
        case ACPI_METHOD_ARGUMENT_INTEGER:
            DeviceExtension->Uid.Valid = TRUE;
            DeviceExtension->Uid.IsString = FALSE;
            DeviceExtension->Uid.u.Integer = Argument->Argument;
            break;

        case ACPI_METHOD_ARGUMENT_STRING:
        case ACPI_METHOD_ARGUMENT_BUFFER:
        {
            ANSI_STRING AnsiString;
            UNICODE_STRING UnicodeString;

            AnsiString.Buffer = (PCHAR)Argument->Data;
            AnsiString.Length = (USHORT)Argument->DataLength;
            AnsiString.MaximumLength = AnsiString.Length;

            Status = RtlAnsiStringToUnicodeString(&UnicodeString,
                                                  &AnsiString,
                                                  TRUE);
            if (NT_SUCCESS(Status))
            {
                DeviceExtension->Uid.Valid = TRUE;
                DeviceExtension->Uid.IsString = TRUE;
                DeviceExtension->Uid.u.String = UnicodeString;
            }
            break;
        }

        default:
            Status = STATUS_ACPI_INVALID_DATA;
            break;
    }

    ExFreePoolWithTag(OutputBuffer, ACPIPROC_TAG);
    return Status;
}
