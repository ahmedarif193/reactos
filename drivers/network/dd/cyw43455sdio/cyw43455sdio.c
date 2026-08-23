/*
 * PROJECT:     ReactOS CYW43455 SDIO Function Transport
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Own CYW43455 SDIO control and companion function stacks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntddk.h>
#include <ntddsd.h>
#include <initguid.h>
#include <reactos/drivers/cyw43455sdio.h>

#define NDEBUG
#include <debug.h>

#define CYW43455_SDIO_TAG 'cSyC'
#define CYW43455_SDIO_FUNCTION_CONTROL 1
#define CYW43455_SDIO_FUNCTION_COMPANION 3
#define CYW43455_SDIO_MAX_ADDRESS 0x1FFFFUL
#define CYW43455_SDIO_MAX_BYTE_COUNT 512
#define CYW43455_SDIO_MAX_BLOCK_COUNT 511
#define CYW43455_SDIO_MAX_BLOCK_SIZE 0x0FFF
#define CYW43455_SDIO_CONTROL_BLOCK_SIZE 64
#define CYW43455_SDIO_CCCR_IO_ENABLE 0x02
#define CYW43455_SDIO_CCCR_IO_READY 0x03

typedef struct _CYW43455_SDIO_DEVICE_EXTENSION
{
    PDEVICE_OBJECT PhysicalDevice;
    PDEVICE_OBJECT LowerDevice;
    IO_REMOVE_LOCK RemoveLock;
    SDBUS_INTERFACE_STANDARD SdBus;
    UNICODE_STRING InterfaceName;
    KMUTEX TransferLock;
    volatile LONG OpenCount;
    ULONG FunctionNumber;
    BOOLEAN SdBusOpened;
    BOOLEAN ControlFunctionEnabled;
    BOOLEAN InterfaceEnabled;
    volatile BOOLEAN Started;
} CYW43455_SDIO_DEVICE_EXTENSION, *PCYW43455_SDIO_DEVICE_EXTENSION;

static
NTSTATUS
NTAPI
Cyw43455SdioSynchronousCompletion(
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
Cyw43455SdioForwardSynchronously(
    _In_ PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           Cyw43455SdioSynchronousCompletion,
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

    return Status;
}

static
NTSTATUS
NTAPI
Cyw43455SdioReleaseRemoveLock(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension = Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);
    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    return STATUS_CONTINUE_COMPLETION;
}

static
NTSTATUS
Cyw43455SdioForwardWithRemoveLock(
    _In_ PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PIRP Irp,
    _In_ BOOLEAN PowerIrp)
{
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           Cyw43455SdioReleaseRemoveLock,
                           DeviceExtension,
                           TRUE,
                           TRUE,
                           TRUE);

    return PowerIrp ? PoCallDriver(DeviceExtension->LowerDevice, Irp) :
                      IoCallDriver(DeviceExtension->LowerDevice, Irp);
}

static
VOID
Cyw43455SdioCloseBus(
    _Inout_ PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension)
{
    if (DeviceExtension->SdBusOpened &&
        DeviceExtension->SdBus.InterfaceDereference != NULL)
    {
        DeviceExtension->SdBus.InterfaceDereference(
            DeviceExtension->SdBus.Context);
    }

    DeviceExtension->SdBusOpened = FALSE;
    RtlZeroMemory(&DeviceExtension->SdBus, sizeof(DeviceExtension->SdBus));
}

static
NTSTATUS
Cyw43455SdioDirectByte(
    _In_ PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR Function,
    _In_ BOOLEAN Write,
    _In_ ULONG Address,
    _Inout_ PUCHAR Value)
{
    SDBUS_REQUEST_PACKET Packet;
    NTSTATUS Status;

    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_DIRECT);
    Packet.Parameters.IoDirect.Function = Function;
    Packet.Parameters.IoDirect.Write = Write;
    Packet.Parameters.IoDirect.RawMode = FALSE;
    Packet.Parameters.IoDirect.Address = Address;
    Packet.Parameters.IoDirect.DataIn = *Value;
    Status = SdBusSubmitRequest(DeviceExtension->SdBus.Context, &Packet);
    if (NT_SUCCESS(Status) && !Write)
        *Value = Packet.Parameters.IoDirect.DataOut;
    return Status;
}

static
NTSTATUS
Cyw43455SdioSetControlFunctionEnabled(
    _Inout_ PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension,
    _In_ BOOLEAN Enable)
{
    UCHAR IoEnable;
    UCHAR IoReady;
    UCHAR FunctionBit = 1U << CYW43455_SDIO_FUNCTION_CONTROL;
    ULONG Retry;
    LARGE_INTEGER Delay;
    NTSTATUS Status;

    Status = Cyw43455SdioDirectByte(DeviceExtension,
                                    0,
                                    FALSE,
                                    CYW43455_SDIO_CCCR_IO_ENABLE,
                                    &IoEnable);
    if (!NT_SUCCESS(Status))
        return Status;

    if (Enable)
        IoEnable |= FunctionBit;
    else
        IoEnable &= (UCHAR)~FunctionBit;
    Status = Cyw43455SdioDirectByte(DeviceExtension,
                                    0,
                                    TRUE,
                                    CYW43455_SDIO_CCCR_IO_ENABLE,
                                    &IoEnable);
    if (!NT_SUCCESS(Status))
        return Status;

    if (!Enable)
    {
        DeviceExtension->ControlFunctionEnabled = FALSE;
        return STATUS_SUCCESS;
    }

    Delay.QuadPart = -10000LL;
    for (Retry = 0; Retry < 500; Retry++)
    {
        Status = Cyw43455SdioDirectByte(DeviceExtension,
                                        0,
                                        FALSE,
                                        CYW43455_SDIO_CCCR_IO_READY,
                                        &IoReady);
        if (!NT_SUCCESS(Status))
            break;
        if (IoReady & FunctionBit)
        {
            DeviceExtension->ControlFunctionEnabled = TRUE;
            return STATUS_SUCCESS;
        }
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }

    IoEnable &= (UCHAR)~FunctionBit;
    (VOID)Cyw43455SdioDirectByte(DeviceExtension,
                                 0,
                                 TRUE,
                                 CYW43455_SDIO_CCCR_IO_ENABLE,
                                 &IoEnable);
    return NT_SUCCESS(Status) ? STATUS_IO_TIMEOUT : Status;
}

static
NTSTATUS
Cyw43455SdioInitializeControlFunction(
    _Inout_ PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension)
{
    SDBUS_REQUEST_PACKET Packet;
    USHORT BlockSize = CYW43455_SDIO_CONTROL_BLOCK_SIZE;
    NTSTATUS Status;

    SD_INIT_SET_PROPERTY(&Packet,
                         SDP_FUNCTION_BLOCK_LENGTH,
                         &BlockSize,
                         sizeof(BlockSize));
    Status = SdBusSubmitRequest(DeviceExtension->SdBus.Context, &Packet);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Cyw43455SdioSetControlFunctionEnabled(DeviceExtension, TRUE);
    return Status;
}

static
NTSTATUS
Cyw43455SdioStartHardware(
    _Inout_ PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension)
{
    SDBUS_REQUEST_PACKET Packet;
    UCHAR FunctionNumber = 0;
    NTSTATUS Status;

    Status = SdBusOpenInterface(DeviceExtension->PhysicalDevice,
                                &DeviceExtension->SdBus,
                                sizeof(DeviceExtension->SdBus),
                                SDBUS_INTERFACE_VERSION);
    if (!NT_SUCCESS(Status))
        return Status;
    DeviceExtension->SdBusOpened = TRUE;

    SD_INIT_GET_PROPERTY(&Packet,
                         SDP_FUNCTION_NUMBER,
                         &FunctionNumber,
                         sizeof(FunctionNumber));
    Status = SdBusSubmitRequest(DeviceExtension->SdBus.Context, &Packet);
    if (!NT_SUCCESS(Status))
        goto Failure;

    if (FunctionNumber != CYW43455_SDIO_FUNCTION_CONTROL &&
        FunctionNumber != CYW43455_SDIO_FUNCTION_COMPANION)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Failure;
    }

    DeviceExtension->FunctionNumber = FunctionNumber;
    DeviceExtension->Started = TRUE;
    if (FunctionNumber == CYW43455_SDIO_FUNCTION_CONTROL)
    {
        Status = IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName,
                                           TRUE);
        if (!NT_SUCCESS(Status))
            goto Failure;
        DeviceExtension->InterfaceEnabled = TRUE;
    }

    DPRINT1("CYW43455SDIO: started function %u\n", FunctionNumber);
    return STATUS_SUCCESS;

Failure:
    DeviceExtension->Started = FALSE;
    if (DeviceExtension->ControlFunctionEnabled)
        (VOID)Cyw43455SdioSetControlFunctionEnabled(DeviceExtension, FALSE);
    DeviceExtension->FunctionNumber = 0;
    Cyw43455SdioCloseBus(DeviceExtension);
    return Status;
}

static
VOID
Cyw43455SdioStopHardware(
    _Inout_ PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension)
{
    DeviceExtension->Started = FALSE;

    KeWaitForSingleObject(&DeviceExtension->TransferLock,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
    if (DeviceExtension->InterfaceEnabled)
    {
        IoSetDeviceInterfaceState(&DeviceExtension->InterfaceName, FALSE);
        DeviceExtension->InterfaceEnabled = FALSE;
    }
    if (DeviceExtension->ControlFunctionEnabled)
        (VOID)Cyw43455SdioSetControlFunctionEnabled(DeviceExtension, FALSE);
    Cyw43455SdioCloseBus(DeviceExtension);
    DeviceExtension->FunctionNumber = 0;
    KeReleaseMutex(&DeviceExtension->TransferLock, FALSE);
}

static
NTSTATUS
Cyw43455SdioValidateTransfer(
    _In_ PCYW43455_SDIO_TRANSFER Transfer,
    _In_ ULONG InputLength,
    _In_ ULONG OutputLength,
    _Out_ PULONG RequiredLength)
{
    ULONG Flags;
    ULONG Count;

    if (InputLength < CYW43455_SDIO_TRANSFER_HEADER_SIZE ||
        Transfer->Version != CYW43455_SDIO_TRANSPORT_VERSION ||
        Transfer->Size != CYW43455_SDIO_TRANSFER_HEADER_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Flags = Transfer->Flags;
    if ((Flags & ~CYW43455_SDIO_TRANSFER_VALID_FLAGS) != 0 ||
        Transfer->Length == 0 ||
        Transfer->Length > CYW43455_SDIO_MAX_TRANSFER ||
        Transfer->Address > CYW43455_SDIO_MAX_ADDRESS ||
        Transfer->Length > MAXULONG - CYW43455_SDIO_TRANSFER_HEADER_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *RequiredLength = CYW43455_SDIO_TRANSFER_SIZE(Transfer->Length);
    if (Flags & CYW43455_SDIO_TRANSFER_WRITE)
    {
        if (InputLength < *RequiredLength)
            return STATUS_BUFFER_TOO_SMALL;
    }
    else if (OutputLength < *RequiredLength)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (Flags & CYW43455_SDIO_TRANSFER_DIRECT)
    {
        if (Transfer->Length != sizeof(UCHAR) ||
            Transfer->BlockSize != 0 ||
            (Flags & (CYW43455_SDIO_TRANSFER_BLOCK_MODE |
                      CYW43455_SDIO_TRANSFER_INCREMENT)) != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        return STATUS_SUCCESS;
    }

    if (Flags & CYW43455_SDIO_TRANSFER_BLOCK_MODE)
    {
        if (Transfer->BlockSize == 0 ||
            Transfer->BlockSize > CYW43455_SDIO_MAX_BLOCK_SIZE ||
            (Transfer->Length % Transfer->BlockSize) != 0)
        {
            return STATUS_INVALID_PARAMETER;
        }
        Count = Transfer->Length / Transfer->BlockSize;
    }
    else
    {
        if (Transfer->BlockSize != 0)
            return STATUS_INVALID_PARAMETER;
        Count = Transfer->Length;
    }

    if (Count == 0 ||
        Count > ((Flags & CYW43455_SDIO_TRANSFER_BLOCK_MODE) ?
                     CYW43455_SDIO_MAX_BLOCK_COUNT :
                     CYW43455_SDIO_MAX_BYTE_COUNT) ||
        ((Flags & CYW43455_SDIO_TRANSFER_INCREMENT) != 0 &&
         Transfer->Length - 1 >
             CYW43455_SDIO_MAX_ADDRESS - Transfer->Address))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
Cyw43455SdioSubmitTransfer(
    _In_ PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension,
    _Inout_ PCYW43455_SDIO_TRANSFER Transfer)
{
    SDBUS_REQUEST_PACKET Packet;
    PMDL Mdl;
    ULONG Count;
    NTSTATUS Status;

    if (Transfer->Flags & CYW43455_SDIO_TRANSFER_DIRECT)
    {
        return Cyw43455SdioDirectByte(
            DeviceExtension,
            CYW43455_SDIO_FUNCTION_CONTROL,
            (Transfer->Flags & CYW43455_SDIO_TRANSFER_WRITE) != 0,
            Transfer->Address,
            &Transfer->Data[0]);
    }

    Mdl = IoAllocateMdl(Transfer->Data,
                        Transfer->Length,
                        FALSE,
                        FALSE,
                        NULL);
    if (Mdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    MmBuildMdlForNonPagedPool(Mdl);

    Count = (Transfer->Flags & CYW43455_SDIO_TRANSFER_BLOCK_MODE) ?
        Transfer->Length / Transfer->BlockSize : Transfer->Length;
    SD_INIT_REQUEST_PACKET(&Packet, SDRF_IO_RW_EXTENDED);
    Packet.Parameters.IoExtended.Function =
        CYW43455_SDIO_FUNCTION_CONTROL;
    Packet.Parameters.IoExtended.Write =
        (Transfer->Flags & CYW43455_SDIO_TRANSFER_WRITE) != 0;
    Packet.Parameters.IoExtended.BlockMode =
        (Transfer->Flags & CYW43455_SDIO_TRANSFER_BLOCK_MODE) != 0;
    Packet.Parameters.IoExtended.Increment =
        (Transfer->Flags & CYW43455_SDIO_TRANSFER_INCREMENT) != 0;
    Packet.Parameters.IoExtended.Address = Transfer->Address;
    Packet.Parameters.IoExtended.BlockCount = Count;
    Packet.Parameters.IoExtended.BlockSize = Transfer->BlockSize;
    Packet.Parameters.IoExtended.Mdl = Mdl;
    Status = SdBusSubmitRequest(DeviceExtension->SdBus.Context, &Packet);
    IoFreeMdl(Mdl);
    return Status;
}

static
NTSTATUS
NTAPI
Cyw43455SdioCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension =
        DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (NT_SUCCESS(Status))
    {
        if (Stack->MajorFunction == IRP_MJ_CREATE)
        {
            Status = KeWaitForSingleObject(&DeviceExtension->TransferLock,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           NULL);
            if (NT_SUCCESS(Status))
            {
                if (!DeviceExtension->Started ||
                    DeviceExtension->FunctionNumber !=
                        CYW43455_SDIO_FUNCTION_CONTROL)
                {
                    Status = STATUS_DEVICE_NOT_READY;
                }
                else
                {
                    if (InterlockedCompareExchange(&DeviceExtension->OpenCount,
                                                   0,
                                                   0) == 0)
                    {
                        Status = Cyw43455SdioInitializeControlFunction(
                            DeviceExtension);
                    }
                    if (NT_SUCCESS(Status))
                    {
                        Stack->FileObject->FsContext = DeviceExtension;
                        InterlockedIncrement(&DeviceExtension->OpenCount);
                    }
                }
                KeReleaseMutex(&DeviceExtension->TransferLock, FALSE);
            }
        }
        else if (Stack->MajorFunction == IRP_MJ_CLOSE &&
                 Stack->FileObject->FsContext == DeviceExtension)
        {
            NTSTATUS DisableStatus;

            Status = KeWaitForSingleObject(&DeviceExtension->TransferLock,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           NULL);
            if (NT_SUCCESS(Status))
            {
                Stack->FileObject->FsContext = NULL;
                if (InterlockedDecrement(&DeviceExtension->OpenCount) == 0 &&
                    DeviceExtension->ControlFunctionEnabled)
                {
                    DisableStatus = Cyw43455SdioSetControlFunctionEnabled(
                        DeviceExtension,
                        FALSE);
                    if (!NT_SUCCESS(DisableStatus))
                    {
                        DPRINT1("CYW43455SDIO: failed to disable function 1: 0x%08lx\n",
                                DisableStatus);
                    }
                }
                KeReleaseMutex(&DeviceExtension->TransferLock, FALSE);
                Status = STATUS_SUCCESS;
            }
        }
        IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
Cyw43455SdioInternalDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension =
        DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PCYW43455_SDIO_TRANSFER Transfer = Irp->AssociatedIrp.SystemBuffer;
    ULONG RequiredLength = 0;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
        goto Complete;

    if (Stack->Parameters.DeviceIoControl.IoControlCode !=
            IOCTL_CYW43455_SDIO_TRANSFER)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
    }
    else if (KeGetCurrentIrql() > APC_LEVEL)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    else
    {
        Status = Cyw43455SdioValidateTransfer(
            Transfer,
            Stack->Parameters.DeviceIoControl.InputBufferLength,
            Stack->Parameters.DeviceIoControl.OutputBufferLength,
            &RequiredLength);
        if (NT_SUCCESS(Status))
        {
            Status = KeWaitForSingleObject(&DeviceExtension->TransferLock,
                                           Executive,
                                           KernelMode,
                                           FALSE,
                                           NULL);
            if (NT_SUCCESS(Status))
            {
                if (!DeviceExtension->Started ||
                    !DeviceExtension->SdBusOpened ||
                    DeviceExtension->FunctionNumber !=
                        CYW43455_SDIO_FUNCTION_CONTROL)
                {
                    Status = STATUS_DEVICE_NOT_READY;
                }
                else
                {
                    Status = Cyw43455SdioSubmitTransfer(DeviceExtension,
                                                        Transfer);
                    if (!NT_SUCCESS(Status))
                    {
                        DPRINT1("CYW43455SDIO: FN1 transfer flags=0x%lx address=0x%05lx length=%lu failed 0x%08lx\n",
                                Transfer->Flags, Transfer->Address,
                                Transfer->Length, Status);
                    }
                }
                KeReleaseMutex(&DeviceExtension->TransferLock, FALSE);
            }
        }
    }

    if (NT_SUCCESS(Status) &&
        !(Transfer->Flags & CYW43455_SDIO_TRANSFER_WRITE))
    {
        Information = RequiredLength;
    }

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);

Complete:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
NTAPI
Cyw43455SdioPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension =
        DeviceObject->DeviceExtension;
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = Cyw43455SdioForwardSynchronously(DeviceExtension, Irp);
            if (NT_SUCCESS(Status))
                Status = Cyw43455SdioStartHardware(DeviceExtension);
            IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Cyw43455SdioStopHardware(DeviceExtension);
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            if (InterlockedCompareExchange(&DeviceExtension->OpenCount,
                                           0,
                                           0) != 0)
            {
                Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;
            }
            break;

        case IRP_MN_REMOVE_DEVICE:
            Cyw43455SdioStopHardware(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(DeviceExtension->LowerDevice, Irp);
            IoDetachDevice(DeviceExtension->LowerDevice);
            RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
            IoDeleteDevice(DeviceObject);
            return Status;

        default:
            break;
    }

    return Cyw43455SdioForwardWithRemoveLock(DeviceExtension, Irp, FALSE);
}

static
NTSTATUS
NTAPI
Cyw43455SdioPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension =
        DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    PoStartNextPowerIrp(Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    return Cyw43455SdioForwardWithRemoveLock(DeviceExtension, Irp, TRUE);
}

static
NTSTATUS
NTAPI
Cyw43455SdioPassThrough(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension =
        DeviceObject->DeviceExtension;
    NTSTATUS Status;

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    return Cyw43455SdioForwardWithRemoveLock(DeviceExtension, Irp, FALSE);
}

static
NTSTATUS
NTAPI
Cyw43455SdioUnsupported(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_INVALID_DEVICE_REQUEST;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_INVALID_DEVICE_REQUEST;
}

static
NTSTATUS
NTAPI
Cyw43455SdioAddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PCYW43455_SDIO_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_OBJECT DeviceObject;
    NTSTATUS Status;

    Status = IoCreateDevice(DriverObject,
                            sizeof(*DeviceExtension),
                            NULL,
                            FILE_DEVICE_NETWORK,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    DeviceExtension = DeviceObject->DeviceExtension;
    RtlZeroMemory(DeviceExtension, sizeof(*DeviceExtension));
    DeviceExtension->PhysicalDevice = PhysicalDeviceObject;
    IoInitializeRemoveLock(&DeviceExtension->RemoveLock,
                           CYW43455_SDIO_TAG,
                           0,
                           0);
    KeInitializeMutex(&DeviceExtension->TransferLock, 0);

    Status = IoRegisterDeviceInterface(
        PhysicalDeviceObject,
        &GUID_DEVINTERFACE_REACTOS_CYW43455_SDIO_CONTROL,
        NULL,
        &DeviceExtension->InterfaceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    Status = IoAttachDeviceToDeviceStackSafe(DeviceObject,
                                              PhysicalDeviceObject,
                                              &DeviceExtension->LowerDevice);
    if (!NT_SUCCESS(Status))
    {
        RtlFreeUnicodeString(&DeviceExtension->InterfaceName);
        IoDeleteDevice(DeviceObject);
        return Status;
    }

    DeviceObject->Flags |= DO_BUFFERED_IO;
    if (DeviceExtension->LowerDevice->Flags & DO_POWER_PAGABLE)
        DeviceObject->Flags |= DO_POWER_PAGABLE;
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
Cyw43455SdioUnload(
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
    ULONG Index;

    UNREFERENCED_PARAMETER(RegistryPath);

    for (Index = 0; Index <= IRP_MJ_MAXIMUM_FUNCTION; Index++)
        DriverObject->MajorFunction[Index] = Cyw43455SdioUnsupported;
    DriverObject->MajorFunction[IRP_MJ_CREATE] = Cyw43455SdioCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = Cyw43455SdioCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = Cyw43455SdioCreateClose;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] =
        Cyw43455SdioInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_PNP] = Cyw43455SdioPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = Cyw43455SdioPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] =
        Cyw43455SdioPassThrough;
    DriverObject->DriverExtension->AddDevice = Cyw43455SdioAddDevice;
    DriverObject->DriverUnload = Cyw43455SdioUnload;
    return STATUS_SUCCESS;
}
