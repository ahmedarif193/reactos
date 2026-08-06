/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     USBPort I/O control functions
 * COPYRIGHT:   Copyright 2017 Vadim Galyant <vgal@rambler.ru>
 */

#include "usbport.h"

#define NDEBUG
#include <debug.h>

static VOID USBPORT_FillTransportCharacteristics(IN PUSBPORT_DEVICE_EXTENSION FdoExtension,
                                                 OUT PUSB_TRANSPORT_CHARACTERISTICS Characteristics);
static VOID USBPORT_FillDeviceCharacteristics(IN PUSBPORT_DEVICE_EXTENSION FdoExtension,
                                              OUT PUSB_DEVICE_CHARACTERISTICS Characteristics);
static WDMUSB_POWER_STATE USBPORT_MapSystemPowerState(IN SYSTEM_POWER_STATE State);
static WDMUSB_POWER_STATE USBPORT_MapDevicePowerState(IN DEVICE_POWER_STATE State);

/*
 * bmRequestType values for the Bulk-Only reset sequence the dump provider
 * issues before its first transfer. USB 2.0 9.3.1: Dir[7], Type[6:5],
 * Recipient[4:0].
 */
#define USBPORT_DUMP_RT_CLASS_INTERFACE_OUT ((BMREQUEST_HOST_TO_DEVICE << 7) | (BMREQUEST_CLASS << 5) | BMREQUEST_TO_INTERFACE)
#define USBPORT_DUMP_RT_STANDARD_ENDPOINT_OUT ((BMREQUEST_HOST_TO_DEVICE << 7) | (BMREQUEST_STANDARD << 5) | BMREQUEST_TO_ENDPOINT)

/* Bulk-Only Mass Storage Reset (USB MSC Bulk-Only Transport 3.1). */
#define USBPORT_DUMP_BULK_RESET_DEVICE 0xFF

static NTSTATUS USBPORT_DumpSubmit(_In_ PUSBPORT_DUMP_CONTEXT DumpContext, _In_ PUSBPORT_ENDPOINT Endpoint, _In_opt_ PUSB_DEFAULT_PIPE_SETUP_PACKET SetupPacket, _In_ BOOLEAN DirectionIn, _Inout_updates_bytes_opt_(Length) PVOID Buffer, _In_ PHYSICAL_ADDRESS PhysicalAddress, _In_ BOOLEAN UsePhysicalAddress, _In_ ULONG Length)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PUSBPORT_TRANSFER Transfer;
    MPSTATUS MpStatus;
    ULONG Timeout;

    if ((Endpoint == NULL) || (Length > PAGE_SIZE) || ((Length != 0) && !UsePhysicalAddress && (Buffer == NULL)) || (UsePhysicalAddress && DirectionIn))
    {
        return STATUS_INVALID_PARAMETER;
    }

    FdoExtension = DumpContext->FdoExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;
    Transfer = DumpContext->Transfer;
    RtlZeroMemory(Transfer, DumpContext->TransferAllocationSize);

    if ((Length != 0) && UsePhysicalAddress)
    {
        Buffer = MmGetVirtualForPhysical(PhysicalAddress);
        if (Buffer == NULL)
            return STATUS_INVALID_ADDRESS;
    }

    if (Length != 0)
    {
        if (DirectionIn)
            RtlZeroMemory(DumpContext->BounceBuffer, Length);
        else
            RtlCopyMemory(DumpContext->BounceBuffer, Buffer, Length);
    }

    Transfer->Flags = TRANSFER_FLAG_DUMP;
    Transfer->Endpoint = Endpoint;
    Transfer->FdoDevice = Endpoint->FdoDevice;
    Transfer->PortTransferLength = sizeof(*Transfer);
    Transfer->FullTransferLength = DumpContext->TransferAllocationSize;
    Transfer->DumpContext = DumpContext;
    Transfer->TransferParameters.TransferBufferLength = Length;
    Transfer->TransferParameters.TransferFlags = USBPORT_TRANSFER_FLAG_DUMP | USBD_SHORT_TRANSFER_OK | (DirectionIn ? USBD_TRANSFER_DIRECTION_IN : 0);
    if (SetupPacket != NULL)
    {
        Transfer->TransferParameters.SetupPacket = *SetupPacket;
    }

    Transfer->SgList.CurrentVa = (ULONG_PTR)DumpContext->BounceBuffer;
    Transfer->SgList.MappedSystemVa = DumpContext->BounceBuffer;
    if (Length != 0)
    {
        Transfer->SgList.SgElementCount = 1;
        Transfer->SgList.SgElement[0].SgPhysicalAddress = DumpContext->BouncePhysicalAddress;
        Transfer->SgList.SgElement[0].SgTransferLength = Length;
        Transfer->SgList.SgElement[0].SgOffset = 0;
    }

    USBPORT_InitializeMiniportTransfer(Transfer);
    DumpContext->TransferStatus = USBD_STATUS_PENDING;
    DumpContext->CompletedLength = 0;
    InterlockedExchange(&DumpContext->Completed, 0);
    KeMemoryBarrier();

    MpStatus = Packet->SubmitTransfer(FdoExtension->MiniPortExt, Endpoint + 1, &Transfer->TransferParameters, Transfer->MiniportTransfer, &Transfer->SgList);
    if (MpStatus != MP_STATUS_SUCCESS)
        return STATUS_IO_DEVICE_ERROR;

    for (Timeout = 0; Timeout < ROS_DUMP_POLL_ITERATIONS; Timeout++)
    {
        Packet->PollEndpoint(FdoExtension->MiniPortExt, Endpoint + 1);
        if (InterlockedCompareExchange(&DumpContext->Completed, 0, 0) != 0)
        {
            break;
        }

        KeStallExecutionProcessor(1);
    }

    if (Timeout == ROS_DUMP_POLL_ITERATIONS)
        return STATUS_IO_TIMEOUT;
    if (!USBD_SUCCESS(DumpContext->TransferStatus))
    {
        return USBPORT_USBDStatusToNtStatus(NULL, DumpContext->TransferStatus);
    }
    if (DumpContext->CompletedLength != Length)
        return STATUS_DEVICE_DATA_ERROR;

    if (DirectionIn && (Length != 0) && !UsePhysicalAddress)
    {
        RtlCopyMemory(Buffer, DumpContext->BounceBuffer, Length);
    }

    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI USBPORT_DumpTransfer(_In_ PVOID Context, _In_ BOOLEAN DirectionIn, _In_reads_bytes_(Length) PVOID Buffer, _In_ ULONG Length)
{
    PUSBPORT_DUMP_CONTEXT DumpContext = Context;
    PUSBPORT_ENDPOINT Endpoint;
    PHYSICAL_ADDRESS PhysicalAddress;

    PhysicalAddress.QuadPart = 0;
    Endpoint = DirectionIn ? DumpContext->BulkInEndpoint : DumpContext->BulkOutEndpoint;
    return USBPORT_DumpSubmit(DumpContext, Endpoint, NULL, DirectionIn, Buffer, PhysicalAddress, FALSE, Length);
}

static NTSTATUS NTAPI USBPORT_DumpTransferPhysical(_In_ PVOID Context, _In_ PHYSICAL_ADDRESS Buffer, _In_ ULONG Length)
{
    PUSBPORT_DUMP_CONTEXT DumpContext = Context;

    return USBPORT_DumpSubmit(DumpContext, DumpContext->BulkOutEndpoint, NULL, FALSE, NULL, Buffer, TRUE, Length);
}

static NTSTATUS USBPORT_DumpControl(_In_ PUSBPORT_DUMP_CONTEXT DumpContext, _In_ UCHAR RequestType, _In_ UCHAR Request, _In_ USHORT Value, _In_ USHORT Index)
{
    USB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
    PHYSICAL_ADDRESS PhysicalAddress;

    RtlZeroMemory(&SetupPacket, sizeof(SetupPacket));
    PhysicalAddress.QuadPart = 0;
    SetupPacket.bmRequestType.B = RequestType;
    SetupPacket.bRequest = Request;
    SetupPacket.wValue.W = Value;
    SetupPacket.wIndex.W = Index;
    return USBPORT_DumpSubmit(DumpContext, DumpContext->ControlEndpoint, &SetupPacket, FALSE, NULL, PhysicalAddress, FALSE, 0);
}

static NTSTATUS NTAPI USBPORT_DumpPrepare(_In_ PVOID Context)
{
    PUSBPORT_DUMP_CONTEXT DumpContext = Context;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    NTSTATUS Status;

    FdoExtension = DumpContext->FdoExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;
    DumpContext->DumpMode = TRUE;
    KeMemoryBarrier();
    Packet->DisableInterrupts(FdoExtension->MiniPortExt);

    /* Bulk-Only Mass Storage Reset. */
    Status = USBPORT_DumpControl(DumpContext, USBPORT_DUMP_RT_CLASS_INTERFACE_OUT, USBPORT_DUMP_BULK_RESET_DEVICE, 0, DumpContext->InterfaceNumber);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Clear ENDPOINT_HALT on both bulk endpoints. */
    Status = USBPORT_DumpControl(DumpContext, USBPORT_DUMP_RT_STANDARD_ENDPOINT_OUT, USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_STALL, DumpContext->BulkInEndpointAddress);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = USBPORT_DumpControl(DumpContext, USBPORT_DUMP_RT_STANDARD_ENDPOINT_OUT, USB_REQUEST_CLEAR_FEATURE, USB_FEATURE_ENDPOINT_STALL, DumpContext->BulkOutEndpointAddress);
    if (!NT_SUCCESS(Status))
        return Status;

    return STATUS_SUCCESS;
}

VOID USBPORT_FreeDumpContext(_In_opt_ PUSBPORT_DUMP_CONTEXT DumpContext)
{
    if (DumpContext == NULL)
        return;

    if (DumpContext->BounceBuffer != NULL)
    {
        MmFreeContiguousMemorySpecifyCache(DumpContext->BounceBuffer, PAGE_SIZE, MmNonCached);
    }
    if (DumpContext->Transfer != NULL)
    {
        ExFreePoolWithTag(DumpContext->Transfer, USBPORT_DUMP_CONTEXT_TAG);
    }
    ExFreePoolWithTag(DumpContext, USBPORT_DUMP_CONTEXT_TAG);
}

static NTSTATUS USBPORT_GetDumpInterface(_Inout_ PROS_USB_DUMP_INTERFACE Interface)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_DUMP_CONTEXT DumpContext;
    PUSBPORT_DEVICE_HANDLE DeviceHandle;
    PUSBPORT_PIPE_HANDLE BulkInPipe;
    PUSBPORT_PIPE_HANDLE BulkOutPipe;
    PUSBPORT_ENDPOINT ControlEndpoint;
    PUSBPORT_ENDPOINT BulkInEndpoint;
    PUSBPORT_ENDPOINT BulkOutEndpoint;
    PHYSICAL_ADDRESS LowestAddress;
    PHYSICAL_ADDRESS HighestAddress;
    PHYSICAL_ADDRESS BoundaryAddress;
    SIZE_T TransferSize;

    if ((Interface == NULL) || (Interface->Version != ROS_USB_DUMP_INTERFACE_VERSION) || (Interface->Size < sizeof(*Interface)) || (Interface->DeviceHandle == NULL) || (Interface->BulkInPipe == NULL) || (Interface->BulkOutPipe == NULL))
    {
        DPRINT1("USBPORT: dump interface validation failed: interface %p version %lu size %lu device %p in %p out %p\n", Interface, Interface != NULL ? Interface->Version : 0, Interface != NULL ? Interface->Size : 0, Interface != NULL ? Interface->DeviceHandle : NULL, Interface != NULL ? Interface->BulkInPipe : NULL, Interface != NULL ? Interface->BulkOutPipe : NULL);
        return STATUS_INVALID_PARAMETER;
    }

    DeviceHandle = Interface->DeviceHandle;
    BulkInPipe = Interface->BulkInPipe;
    BulkOutPipe = Interface->BulkOutPipe;
    if (!USBPORT_ValidatePipeHandle(DeviceHandle, BulkInPipe) || !USBPORT_ValidatePipeHandle(DeviceHandle, BulkOutPipe))
    {
        DPRINT1("USBPORT: dump interface pipe validation failed: device %p in %p out %p\n", DeviceHandle, BulkInPipe, BulkOutPipe);
        return STATUS_INVALID_PARAMETER;
    }

    ControlEndpoint = DeviceHandle->PipeHandle.Endpoint;
    BulkInEndpoint = BulkInPipe->Endpoint;
    BulkOutEndpoint = BulkOutPipe->Endpoint;
    if ((ControlEndpoint == NULL) || (BulkInEndpoint == NULL) || (BulkOutEndpoint == NULL) || (BulkInEndpoint->FdoDevice == NULL) || (BulkInEndpoint->FdoDevice != BulkOutEndpoint->FdoDevice) || (BulkInEndpoint->FdoDevice != ControlEndpoint->FdoDevice) || (BulkInEndpoint->EndpointProperties.TransferType != USBPORT_TRANSFER_TYPE_BULK) || (BulkOutEndpoint->EndpointProperties.TransferType != USBPORT_TRANSFER_TYPE_BULK) || (BulkInEndpoint->EndpointProperties.Direction == USBPORT_TRANSFER_DIRECTION_OUT) || (BulkOutEndpoint->EndpointProperties.Direction != USBPORT_TRANSFER_DIRECTION_OUT))
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    FdoExtension = BulkInEndpoint->FdoDevice->DeviceExtension;
    if (!(FdoExtension->MiniPortInterface->Packet.MiniPortFlags & USB_MINIPORT_FLAGS_DUMP_POLLING))
    {
        return STATUS_NOT_SUPPORTED;
    }

    DumpContext = FdoExtension->Aux.DumpContext;
    if (DumpContext == NULL)
    {
        DumpContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(*DumpContext), USBPORT_DUMP_CONTEXT_TAG);
        if (DumpContext == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(DumpContext, sizeof(*DumpContext));

        TransferSize = sizeof(USBPORT_TRANSFER) + sizeof(USBPORT_MINIPORT_TRANSFER_HEADER) + FdoExtension->MiniPortInterface->Packet.MiniPortTransferSize;
        DumpContext->Transfer = ExAllocatePoolWithTag(NonPagedPool, TransferSize, USBPORT_DUMP_CONTEXT_TAG);
        if (DumpContext->Transfer == NULL)
            goto Failure;

        LowestAddress.QuadPart = 0;
        HighestAddress.QuadPart = MAXULONG;
        BoundaryAddress.QuadPart = 0;
        DumpContext->BounceBuffer = MmAllocateContiguousMemorySpecifyCache(PAGE_SIZE, LowestAddress, HighestAddress, BoundaryAddress, MmNonCached);
        if (DumpContext->BounceBuffer == NULL)
            goto Failure;

        DumpContext->FdoExtension = FdoExtension;
        DumpContext->DeviceHandle = DeviceHandle;
        DumpContext->ControlEndpoint = ControlEndpoint;
        DumpContext->BulkInEndpoint = BulkInEndpoint;
        DumpContext->BulkOutEndpoint = BulkOutEndpoint;
        DumpContext->TransferAllocationSize = TransferSize;
        DumpContext->BouncePhysicalAddress = MmGetPhysicalAddress(DumpContext->BounceBuffer);
        DumpContext->InterfaceNumber = Interface->InterfaceNumber;
        DumpContext->BulkInEndpointAddress = Interface->BulkInEndpointAddress;
        DumpContext->BulkOutEndpointAddress = Interface->BulkOutEndpointAddress;
        FdoExtension->Aux.DumpContext = DumpContext;
    }
    else if ((DumpContext->DeviceHandle != DeviceHandle) || (DumpContext->BulkInEndpoint != BulkInEndpoint) || (DumpContext->BulkOutEndpoint != BulkOutEndpoint))
    {
        return STATUS_DEVICE_BUSY;
    }

    Interface->Version = ROS_USB_DUMP_INTERFACE_VERSION;
    Interface->Size = sizeof(*Interface);
    Interface->Context = DumpContext;
    Interface->Prepare = USBPORT_DumpPrepare;
    Interface->Transfer = USBPORT_DumpTransfer;
    Interface->TransferPhysical = USBPORT_DumpTransferPhysical;
    return STATUS_SUCCESS;

Failure:
    USBPORT_FreeDumpContext(DumpContext);
    return STATUS_INSUFFICIENT_RESOURCES;
}

static
WDMUSB_POWER_STATE
USBPORT_MapSystemPowerState(IN SYSTEM_POWER_STATE State)
{
    switch (State)
    {
        case PowerSystemUnspecified:
            return WdmUsbPowerSystemUnspecified;
        case PowerSystemWorking:
            return WdmUsbPowerSystemWorking;
        case PowerSystemSleeping1:
            return WdmUsbPowerSystemSleeping1;
        case PowerSystemSleeping2:
            return WdmUsbPowerSystemSleeping2;
        case PowerSystemSleeping3:
            return WdmUsbPowerSystemSleeping3;
        case PowerSystemHibernate:
            return WdmUsbPowerSystemHibernate;
        case PowerSystemShutdown:
            return WdmUsbPowerSystemShutdown;
        default:
            return WdmUsbPowerNotMapped;
    }
}

static
WDMUSB_POWER_STATE
USBPORT_MapDevicePowerState(IN DEVICE_POWER_STATE State)
{
    switch (State)
    {
        case PowerDeviceUnspecified:
            return WdmUsbPowerDeviceUnspecified;
        case PowerDeviceD0:
            return WdmUsbPowerDeviceD0;
        case PowerDeviceD1:
            return WdmUsbPowerDeviceD1;
        case PowerDeviceD2:
            return WdmUsbPowerDeviceD2;
        case PowerDeviceD3:
            return WdmUsbPowerDeviceD3;
        default:
            return WdmUsbPowerNotMapped;
    }
}

static
VOID
NTAPI
USBPORT_CancelTransportNotificationIrp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp);

VOID
NTAPI
USBPORT_CompleteTransportNotificationIrp(
    IN PUSBPORT_DEVICE_EXTENSION FdoExtension,
    IN PIRP Irp,
    IN NTSTATUS Status,
    IN BOOLEAN UpdateData)
{
    KIRQL CancelIrql;

    if (!Irp)
        return;

    IoAcquireCancelSpinLock(&CancelIrql);
    if (!IoSetCancelRoutine(Irp, NULL))
    {
        IoReleaseCancelSpinLock(CancelIrql);
        return;
    }

    if (Irp->Cancel)
        Status = STATUS_CANCELLED;

    IoReleaseCancelSpinLock(CancelIrql);

    Irp->Tail.Overlay.DriverContext[0] = NULL;

    if (Status == STATUS_SUCCESS && UpdateData)
    {
        PUSB_TRANSPORT_CHARACTERISTICS_CHANGE_NOTIFICATION Notification;
        PIO_STACK_LOCATION IoStack;

        IoStack = IoGetCurrentIrpStackLocation(Irp);
        Notification = Irp->AssociatedIrp.SystemBuffer;

        if (Notification &&
            IoStack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(*Notification))
        {
            USBPORT_FillTransportCharacteristics(FdoExtension,
                                                 &Notification->UsbTransportCharacteristics);
            Irp->IoStatus.Information = sizeof(*Notification);
        }
        else
        {
            Status = STATUS_BUFFER_TOO_SMALL;
            Irp->IoStatus.Information = 0;
        }
    }
    else
    {
        Irp->IoStatus.Information = 0;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

VOID
NTAPI
USBPORT_SignalTransportChange(
    IN PUSBPORT_DEVICE_EXTENSION FdoExtension,
    IN ULONG ChangeFlags)
{
    PLIST_ENTRY ListEntry;
    PUSBPORT_TRANSPORT_REGISTRATION Registration;
    KIRQL OldIrql;

    if (ChangeFlags == 0)
    {
        ChangeFlags = USB_REGISTER_FOR_TRANSPORT_LATENCY_CHANGE |
                      USB_REGISTER_FOR_TRANSPORT_BANDWIDTH_CHANGE;
    }

    KeAcquireSpinLock(&FdoExtension->Aux.TransportListSpinLock, &OldIrql);

RestartScan:
    ListEntry = FdoExtension->Aux.TransportRegistrationList.Flink;
    while (ListEntry != &FdoExtension->Aux.TransportRegistrationList)
    {
        Registration = CONTAINING_RECORD(ListEntry,
                                         USBPORT_TRANSPORT_REGISTRATION,
                                         ListEntry);

        if (Registration->PendingIrp)
        {
            ULONG Flags = Registration->Flags;

            if (Flags == 0)
            {
                Flags = USB_REGISTER_FOR_TRANSPORT_LATENCY_CHANGE |
                        USB_REGISTER_FOR_TRANSPORT_BANDWIDTH_CHANGE;
            }

            if ((Flags & ChangeFlags) != 0)
            {
                PIRP Irp = Registration->PendingIrp;
                Registration->PendingIrp = NULL;

                KeReleaseSpinLock(&FdoExtension->Aux.TransportListSpinLock, OldIrql);

                USBPORT_CompleteTransportNotificationIrp(FdoExtension,
                                                         Irp,
                                                         STATUS_SUCCESS,
                                                         TRUE);

                KeAcquireSpinLock(&FdoExtension->Aux.TransportListSpinLock, &OldIrql);
                goto RestartScan;
            }
        }

        ListEntry = ListEntry->Flink;
    }

    KeReleaseSpinLock(&FdoExtension->Aux.TransportListSpinLock, OldIrql);
}

VOID
NTAPI
USBPORT_InvalidateTimeSyncGeneration(
    IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    PLIST_ENTRY ListEntry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, &OldIrql);
    FdoExtension->Aux.NextTimeSyncId++;
    if (FdoExtension->Aux.NextTimeSyncId == 0)
        FdoExtension->Aux.NextTimeSyncId = 1;

    ListEntry = FdoExtension->Aux.TimeSyncTrackingList.Flink;
    while (ListEntry != &FdoExtension->Aux.TimeSyncTrackingList)
    {
        PUSBPORT_TIMESYNC_CONTEXT Context;

        Context = CONTAINING_RECORD(ListEntry,
                                    USBPORT_TIMESYNC_CONTEXT,
                                    ListEntry);
        Context->GenerationID = FdoExtension->Aux.NextTimeSyncId;
        ListEntry = ListEntry->Flink;
    }

    KeReleaseSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, OldIrql);
}

static
VOID
NTAPI
USBPORT_CancelTransportNotificationIrp(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_TRANSPORT_REGISTRATION Registration;
    KIRQL OldIrql;

    FdoExtension = DeviceObject->DeviceExtension;
    Registration = (PUSBPORT_TRANSPORT_REGISTRATION)Irp->Tail.Overlay.DriverContext[0];
    Irp->Tail.Overlay.DriverContext[0] = NULL;

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    if (Registration)
    {
        KeAcquireSpinLock(&FdoExtension->Aux.TransportListSpinLock, &OldIrql);
        if (Registration->PendingIrp == Irp)
            Registration->PendingIrp = NULL;
        KeReleaseSpinLock(&FdoExtension->Aux.TransportListSpinLock, OldIrql);
    }

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
ULONGLONG
USBPORT_QueryMaxBandwidth(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    ULONG MiniportFlags = 0;

    if (FdoExtension->MiniPortInterface)
        MiniportFlags = FdoExtension->MiniPortInterface->Packet.MiniPortFlags;

    if (MiniportFlags & USB_MINIPORT_FLAGS_USB3)
        return 5000000000ULL;

    if (MiniportFlags & USB_MINIPORT_FLAGS_USB2)
        return 480000000ULL;

    return 12000000ULL;
}

static
ULONG
USBPORT_QueryDefaultLatency(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    ULONG MiniportFlags = 0;

    if (FdoExtension->MiniPortInterface)
        MiniportFlags = FdoExtension->MiniPortInterface->Packet.MiniPortFlags;

    if (MiniportFlags & USB_MINIPORT_FLAGS_USB3)
        return 0;

    if (MiniportFlags & USB_MINIPORT_FLAGS_USB2)
        return 1;

    return 5;
}

static
VOID
USBPORT_FillTransportCharacteristics(IN PUSBPORT_DEVICE_EXTENSION FdoExtension,
                                     OUT PUSB_TRANSPORT_CHARACTERISTICS Characteristics)
{
    RtlZeroMemory(Characteristics, sizeof(*Characteristics));
    Characteristics->Version = USB_TRANSPORT_CHARACTERISTICS_VERSION_1;
    Characteristics->TransportCharacteristicsFlags =
        USB_TRANSPORT_CHARACTERISTICS_LATENCY_AVAILABLE |
        USB_TRANSPORT_CHARACTERISTICS_BANDWIDTH_AVAILABLE;

    Characteristics->CurrentRoundtripLatencyInMilliSeconds =
        USBPORT_QueryDefaultLatency(FdoExtension);
    Characteristics->MaxPotentialBandwidth =
        USBPORT_QueryMaxBandwidth(FdoExtension);
}

static
VOID
USBPORT_FillDeviceCharacteristics(IN PUSBPORT_DEVICE_EXTENSION FdoExtension,
                                  OUT PUSB_DEVICE_CHARACTERISTICS Characteristics)
{
    ULONG Latency = USBPORT_QueryDefaultLatency(FdoExtension);

    RtlZeroMemory(Characteristics, sizeof(*Characteristics));
    Characteristics->Version = USB_DEVICE_CHARACTERISTICS_VERSION_1;
    Characteristics->UsbDeviceCharacteristicsFlags =
        USB_DEVICE_CHARACTERISTICS_MAXIMUM_PATH_DELAYS_AVAILABLE;
    Characteristics->MaximumSendPathDelayInMilliSeconds = Latency;
    Characteristics->MaximumCompletionPathDelayInMilliSeconds = Latency;
}

VOID
NTAPI
USBPORT_UserGetHcName(IN PDEVICE_OBJECT FdoDevice,
                      IN PUSBUSER_CONTROLLER_UNICODE_NAME ControllerName,
                      IN PUSB_UNICODE_NAME UnicodeName)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    ULONG Length;
    NTSTATUS Status;
    ULONG ResultLength;

    DPRINT("USBPORT_UserGetHcName: ... \n");

    FdoExtension = FdoDevice->DeviceExtension;

    Length = ControllerName->Header.RequestBufferLength -
             sizeof(USBUSER_CONTROLLER_UNICODE_NAME);

    RtlZeroMemory(UnicodeName, Length);

    Status = IoGetDeviceProperty(FdoExtension->CommonExtension.LowerPdoDevice,
                                 DevicePropertyDriverKeyName,
                                 Length,
                                 UnicodeName->String,
                                 &ResultLength);

    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            ControllerName->Header.UsbUserStatusCode = UsbUserBufferTooSmall;
        }
        else
        {
            ControllerName->Header.UsbUserStatusCode = UsbUserInvalidParameter;
        }
    }
    else
    {
        ControllerName->Header.UsbUserStatusCode = UsbUserSuccess;
        UnicodeName->Length = ResultLength + sizeof(UNICODE_NULL);
    }

    ControllerName->Header.ActualBufferLength = sizeof(USBUSER_CONTROLLER_UNICODE_NAME) +
                                                ResultLength;
}

NTSTATUS
NTAPI
USBPORT_GetSymbolicName(IN PDEVICE_OBJECT RootHubPdo,
                        IN PUNICODE_STRING DestinationString)
{
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUNICODE_STRING RootHubName;
    PWCHAR Buffer;
    SIZE_T LengthName;
    SIZE_T Length;
    PWSTR SourceString;
    WCHAR Character;

    DPRINT("USBPORT_GetSymbolicName: ... \n");

    PdoExtension = RootHubPdo->DeviceExtension;
    RootHubName = &PdoExtension->CommonExtension.SymbolicLinkName;
    Buffer = RootHubName->Buffer;

    if (!Buffer)
    {
        return STATUS_UNSUCCESSFUL;
    }

    LengthName = RootHubName->Length;

    SourceString = ExAllocatePoolWithTag(PagedPool, LengthName, USB_PORT_TAG);

    if (!SourceString)
    {
        RtlInitUnicodeString(DestinationString, NULL);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(SourceString, LengthName);

    if (*Buffer == L'\\')
    {
         Buffer += 1;

        if (*Buffer == L'\\')
        {
            Buffer += 1;
            goto Exit;
        }

        Character = *Buffer;

        do
        {
            if (Character == UNICODE_NULL)
            {
                break;
            }

            Buffer += 1;
            Character = *Buffer;
        }
        while (*Buffer != L'\\');

        if (*Buffer == L'\\')
        {
            Buffer += 1;
        }

Exit:
        Length = (ULONG_PTR)Buffer - (ULONG_PTR)RootHubName->Buffer;
    }
    else
    {
        Length = 0;
    }

    RtlCopyMemory(SourceString,
                  (PVOID)((ULONG_PTR)RootHubName->Buffer + Length),
                  RootHubName->Length - Length);

    RtlInitUnicodeString(DestinationString, SourceString);

    DPRINT("USBPORT_RegisterDeviceInterface: DestinationString  - %wZ\n",
           DestinationString);

    return STATUS_SUCCESS;
}

VOID
NTAPI
USBPORT_UserGetRootHubName(IN PDEVICE_OBJECT FdoDevice,
                           IN PUSBUSER_CONTROLLER_UNICODE_NAME RootHubName,
                           IN PUSB_UNICODE_NAME UnicodeName)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    UNICODE_STRING UnicodeString;
    ULONG Length;
    ULONG ResultLength = 0;
    NTSTATUS Status;

    DPRINT("USBPORT_UserGetRootHubName: ... \n");

    FdoExtension = FdoDevice->DeviceExtension;

    Length = RootHubName->Header.RequestBufferLength -
             sizeof(USBUSER_CONTROLLER_UNICODE_NAME);

    RtlZeroMemory(UnicodeName, Length);

    Status = USBPORT_GetSymbolicName(FdoExtension->RootHubPdo, &UnicodeString);

    if (NT_SUCCESS(Status))
    {
        ResultLength = UnicodeString.Length;

        if (UnicodeString.Length > Length)
        {
            UnicodeString.Length = Length;
            Status = STATUS_BUFFER_TOO_SMALL;
        }

        if (UnicodeString.Length)
        {
            RtlCopyMemory(UnicodeName->String,
                          UnicodeString.Buffer,
                          UnicodeString.Length);
        }

        RtlFreeUnicodeString(&UnicodeString);
    }

    if (!NT_SUCCESS(Status))
    {
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            RootHubName->Header.UsbUserStatusCode = UsbUserBufferTooSmall;
        }
        else
        {
            RootHubName->Header.UsbUserStatusCode = UsbUserInvalidParameter;
        }
    }
    else
    {
        RootHubName->Header.UsbUserStatusCode = UsbUserSuccess;
        UnicodeName->Length = ResultLength + sizeof(UNICODE_NULL);
    }

    RootHubName->Header.ActualBufferLength = sizeof(USBUSER_CONTROLLER_UNICODE_NAME) +
                                             ResultLength;
}

NTSTATUS
NTAPI
USBPORT_GetUnicodeName(IN PDEVICE_OBJECT FdoDevice,
                       IN PIRP Irp,
                       IN PULONG_PTR Information)
{
    PUSB_HCD_DRIVERKEY_NAME DriverKey;
    PIO_STACK_LOCATION IoStack;
    ULONG OutputBufferLength;
    ULONG IoControlCode;
    ULONG Length;
    PUSBUSER_CONTROLLER_UNICODE_NAME ControllerName;
    PUSB_UNICODE_NAME UnicodeName;
    ULONG ActualLength;

    DPRINT("USBPORT_GetUnicodeName: ... \n");

    *Information = 0;
    DriverKey = Irp->AssociatedIrp.SystemBuffer;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    OutputBufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;
    IoControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;

    if (OutputBufferLength < sizeof(USB_UNICODE_NAME))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    Length = sizeof(USBUSER_CONTROLLER_UNICODE_NAME);

    while (TRUE)
    {
        ControllerName = ExAllocatePoolWithTag(PagedPool,
                                               Length,
                                               USB_PORT_TAG);

        if (!ControllerName)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(ControllerName, Length);

        ControllerName->Header.RequestBufferLength = Length;
        UnicodeName = &ControllerName->UnicodeName;

        if (IoControlCode == IOCTL_GET_HCD_DRIVERKEY_NAME)
        {
            ControllerName->Header.UsbUserRequest = USBUSER_GET_CONTROLLER_DRIVER_KEY;
            USBPORT_UserGetHcName(FdoDevice, ControllerName, UnicodeName);
        }
        else
        {
            ControllerName->Header.UsbUserRequest = USBUSER_GET_ROOTHUB_SYMBOLIC_NAME;
            USBPORT_UserGetRootHubName(FdoDevice, ControllerName, UnicodeName);
        }

        if (ControllerName->Header.UsbUserStatusCode != UsbUserBufferTooSmall)
        {
            break;
        }

        Length = ControllerName->Header.ActualBufferLength;

        ExFreePoolWithTag(ControllerName, USB_PORT_TAG);
    }

    if (ControllerName->Header.UsbUserStatusCode != UsbUserSuccess)
    {
        ExFreePoolWithTag(ControllerName, USB_PORT_TAG);
        return STATUS_UNSUCCESSFUL;
    }

    ActualLength = sizeof(ULONG) + ControllerName->UnicodeName.Length;

    DriverKey->ActualLength = ActualLength;

    if (OutputBufferLength < ActualLength)
    {
        DriverKey->DriverKeyName[0] = UNICODE_NULL;
        *Information = sizeof(USB_UNICODE_NAME);
    }
    else
    {
        RtlCopyMemory(DriverKey->DriverKeyName,
                      ControllerName->UnicodeName.String,
                      ControllerName->UnicodeName.Length);

        *Information = DriverKey->ActualLength;
    }

    ExFreePoolWithTag(ControllerName, USB_PORT_TAG);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
USBPORT_PdoDeviceControl(IN PDEVICE_OBJECT PdoDevice,
                         IN PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    ULONG IoCtl;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    IoCtl = IoStack->Parameters.DeviceIoControl.IoControlCode;

    DPRINT("USBPORT_PdoDeviceControl: PdoDevice - %p, Irp - %p, IoCtl - %x\n",
           PdoDevice,
           Irp,
           IoCtl);

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

NTSTATUS
NTAPI
USBPORT_PdoInternalDeviceControl(IN PDEVICE_OBJECT PdoDevice,
                                 IN PIRP Irp)
{
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PIO_STACK_LOCATION IoStack;
    ULONG IoCtl;
    NTSTATUS Status;

    PdoExtension = PdoDevice->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    IoCtl = IoStack->Parameters.DeviceIoControl.IoControlCode;

    //DPRINT("USBPORT_PdoInternalDeviceControl: PdoDevice - %p, Irp - %p, IoCtl - %x\n",
    //       PdoDevice,
    //       Irp,
    //       IoCtl);

    if (IoCtl == IOCTL_INTERNAL_REACTOS_USB_GET_DUMP_INTERFACE)
    {
        Status = USBPORT_GetDumpInterface(IoStack->Parameters.Others.Argument1);
        goto Exit;
    }

    if (IoCtl == IOCTL_INTERNAL_USB_SUBMIT_URB)
    {
        return USBPORT_HandleSubmitURB(PdoDevice, Irp, URB_FROM_IRP(Irp));
    }

    if (IoCtl == IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO)
    {
        DPRINT1("USBPORT_PdoInternalDeviceControl: IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO Pdo=%p Arg1=%p Arg2=%p\n",
                PdoDevice,
                IoStack->Parameters.Others.Argument1,
                IoStack->Parameters.Others.Argument2);

        if (IoStack->Parameters.Others.Argument1)
            *(PVOID *)IoStack->Parameters.Others.Argument1 = PdoDevice;

        if (IoStack->Parameters.Others.Argument2)
            *(PVOID *)IoStack->Parameters.Others.Argument2 = PdoDevice;

        Status = STATUS_SUCCESS;
        goto Exit;
    }

    if (IoCtl == IOCTL_INTERNAL_USB_GET_HUB_COUNT)
    {
        DPRINT("USBPORT_PdoInternalDeviceControl: IOCTL_INTERNAL_USB_GET_HUB_COUNT\n");

        if (IoStack->Parameters.Others.Argument1)
        {
            ++*(PULONG)IoStack->Parameters.Others.Argument1;
        }

        Status = STATUS_SUCCESS;
        goto Exit;
    }

    if (IoCtl == IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE)
    {
        DPRINT("USBPORT_PdoInternalDeviceControl: IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE\n");

        if (IoStack->Parameters.Others.Argument1)
        {
            *(PVOID *)IoStack->Parameters.Others.Argument1 = &PdoExtension->DeviceHandle;
        }

        Status = STATUS_SUCCESS;
        goto Exit;
    }

    if (IoCtl == IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION)
    {
        DPRINT("USBPORT_PdoInternalDeviceControl: IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION\n");
        return USBPORT_IdleNotification(PdoDevice, Irp);
    }

    DPRINT("USBPORT_PdoInternalDeviceControl: INVALID INTERNAL DEVICE CONTROL\n");
    Status = STATUS_INVALID_DEVICE_REQUEST;

Exit:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NTAPI
USBPORT_FdoDeviceControl(IN PDEVICE_OBJECT FdoDevice,
                         IN PIRP Irp)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PIO_STACK_LOCATION IoStack;
    PUSBUSER_REQUEST_HEADER UserHeader;
    ULONG ControlCode;
    NTSTATUS Status = STATUS_INVALID_DEVICE_REQUEST;
    ULONG_PTR Information = 0;

    DPRINT("USBPORT_FdoDeviceControl: Irp - %p\n", Irp);

    FdoExtension = FdoDevice->DeviceExtension;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    ControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;

    switch (ControlCode)
    {
        case IOCTL_USB_DIAGNOSTIC_MODE_ON:
            DPRINT("USBPORT_FdoDeviceControl: IOCTL_USB_DIAGNOSTIC_MODE_ON\n");
            FdoExtension->Flags |= USBPORT_FLAG_DIAGNOSTIC_MODE;
            break;

        case IOCTL_USB_DIAGNOSTIC_MODE_OFF:
            DPRINT("USBPORT_FdoDeviceControl: IOCTL_USB_DIAGNOSTIC_MODE_OFF\n");
            FdoExtension->Flags &= ~USBPORT_FLAG_DIAGNOSTIC_MODE;
            break;

        case IOCTL_USB_GET_NODE_INFORMATION:
            DPRINT1("USBPORT_FdoDeviceControl: IOCTL_USB_GET_NODE_INFORMATION\n");
            Status = USBPORT_GetUnicodeName(FdoDevice, Irp, &Information);
            break;

        case IOCTL_GET_HCD_DRIVERKEY_NAME:
            DPRINT1("USBPORT_FdoDeviceControl: IOCTL_GET_HCD_DRIVERKEY_NAME\n");
            Status = USBPORT_GetUnicodeName(FdoDevice, Irp, &Information);
            break;

        case IOCTL_USB_USER_REQUEST:
        {
            DPRINT("USBPORT_FdoDeviceControl: IOCTL_USB_USER_REQUEST\n");

            if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(USBUSER_REQUEST_HEADER) ||
                IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(USBUSER_REQUEST_HEADER) ||
                !Irp->AssociatedIrp.SystemBuffer)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            UserHeader = (PUSBUSER_REQUEST_HEADER)Irp->AssociatedIrp.SystemBuffer;

            if (UserHeader->RequestBufferLength > IoStack->Parameters.DeviceIoControl.OutputBufferLength)
            {
                UserHeader->UsbUserStatusCode = UsbUserBufferTooSmall;
                UserHeader->ActualBufferLength = 0;
                Status = STATUS_BUFFER_TOO_SMALL;
                Information = sizeof(USBUSER_REQUEST_HEADER);
                break;
            }

            switch (UserHeader->UsbUserRequest)
            {
                case USBUSER_GET_CONTROLLER_DRIVER_KEY:
                case USBUSER_GET_ROOTHUB_SYMBOLIC_NAME:
                {
                    PUSBUSER_CONTROLLER_UNICODE_NAME CtlName;
                    PUSB_UNICODE_NAME UnicodeName;

                    if (UserHeader->RequestBufferLength < sizeof(USBUSER_CONTROLLER_UNICODE_NAME))
                    {
                        UserHeader->UsbUserStatusCode = UsbUserInvalidParameter;
                        UserHeader->ActualBufferLength = sizeof(USBUSER_CONTROLLER_UNICODE_NAME);
                        Status = STATUS_INVALID_PARAMETER;
                        Information = sizeof(USBUSER_REQUEST_HEADER);
                        break;
                    }

                    CtlName = (PUSBUSER_CONTROLLER_UNICODE_NAME)UserHeader;
                    UnicodeName = &CtlName->UnicodeName;

                    if (UserHeader->UsbUserRequest == USBUSER_GET_CONTROLLER_DRIVER_KEY)
                    {
                        USBPORT_UserGetHcName(FdoDevice, CtlName, UnicodeName);
                    }
                    else
                    {
                        USBPORT_UserGetRootHubName(FdoDevice, CtlName, UnicodeName);
                    }

                    if (CtlName->Header.UsbUserStatusCode == UsbUserSuccess)
                    {
                        Status = STATUS_SUCCESS;
                    }
                    else if (CtlName->Header.UsbUserStatusCode == UsbUserBufferTooSmall)
                    {
                        Status = STATUS_BUFFER_TOO_SMALL;
                    }
                    else
                    {
                        Status = STATUS_UNSUCCESSFUL;
                    }

                    Information = CtlName->Header.ActualBufferLength;
                    break;
                }

                case USBUSER_GET_CONTROLLER_INFO_0:
                {
                    PUSBUSER_CONTROLLER_INFO_0 CtlInfo;
                    PUSBPORT_RHDEVICE_EXTENSION RhExtension;
                    PUSB_HUB_DESCRIPTOR HubDesc;
                    USB_CONTROLLER_FLAVOR Flavor = USB_HcGeneric;

                    if (UserHeader->RequestBufferLength < sizeof(USBUSER_CONTROLLER_INFO_0))
                    {
                        UserHeader->UsbUserStatusCode = UsbUserInvalidParameter;
                        UserHeader->ActualBufferLength = sizeof(USBUSER_CONTROLLER_INFO_0);
                        Status = STATUS_INVALID_PARAMETER;
                        Information = sizeof(USBUSER_REQUEST_HEADER);
                        break;
                    }

                    CtlInfo = (PUSBUSER_CONTROLLER_INFO_0)UserHeader;

                    RtlZeroMemory(&CtlInfo->Info0, sizeof(USB_CONTROLLER_INFO_0));

                    CtlInfo->Info0.PciVendorId = FdoExtension->VendorID;
                    CtlInfo->Info0.PciDeviceId = FdoExtension->DeviceID;
                    CtlInfo->Info0.PciRevision = FdoExtension->RevisionID;

                    RhExtension = USBPORT_GetRootHubExtension(FdoExtension);
                    if (RhExtension && RhExtension->RootHubDescriptors)
                    {
                        HubDesc = &RhExtension->RootHubDescriptors->Descriptor;
                        CtlInfo->Info0.NumberOfRootPorts = HubDesc->bNumberOfPorts;
                    }

                    if (FdoExtension->BaseClass == PCI_CLASS_SERIAL_BUS_CTLR &&
                        FdoExtension->SubClass == PCI_SUBCLASS_SB_USB)
                    {
                        switch (FdoExtension->ProgIf)
                        {
                            case PCI_INTERFACE_USB_ID_OHCI:
                                Flavor = OHCI_Generic;
                                break;

                            case PCI_INTERFACE_USB_ID_UHCI:
                                Flavor = UHCI_Generic;
                                break;

                            case PCI_INTERFACE_USB_ID_EHCI:
                                Flavor = EHCI_Generic;
                                break;

                            default:
                                Flavor = USB_HcGeneric;
                                break;
                        }
                    }

                    CtlInfo->Info0.ControllerFlavor = Flavor;

                    CtlInfo->Info0.HcFeatureFlags = 0;

                    UserHeader->UsbUserStatusCode = UsbUserSuccess;
                    UserHeader->ActualBufferLength = sizeof(USBUSER_CONTROLLER_INFO_0);
                    Status = STATUS_SUCCESS;
                    Information = UserHeader->ActualBufferLength;
                    break;
                }

                case USBUSER_GET_BANDWIDTH_INFORMATION:
                {
                    PUSBUSER_BANDWIDTH_INFO_REQUEST BwReq;
                    PUSB_BANDWIDTH_INFO BwInfo;
                    KIRQL OldIrql;
                    PLIST_ENTRY Entry;
                    PUSBPORT_DEVICE_HANDLE Handle;
                    ULONG DeviceCount = 0;

                    if (UserHeader->RequestBufferLength < sizeof(USBUSER_BANDWIDTH_INFO_REQUEST))
                    {
                        UserHeader->UsbUserStatusCode = UsbUserInvalidParameter;
                        UserHeader->ActualBufferLength = sizeof(USBUSER_BANDWIDTH_INFO_REQUEST);
                        Status = STATUS_INVALID_PARAMETER;
                        Information = sizeof(USBUSER_REQUEST_HEADER);
                        break;
                    }

                    BwReq = (PUSBUSER_BANDWIDTH_INFO_REQUEST)UserHeader;
                    BwInfo = &BwReq->BandwidthInformation;

                    RtlZeroMemory(BwInfo, sizeof(USB_BANDWIDTH_INFO));

                    KeAcquireSpinLock(&FdoExtension->DeviceHandleSpinLock, &OldIrql);
                    Entry = FdoExtension->DeviceHandleList.Flink;
                    while (Entry != &FdoExtension->DeviceHandleList)
                    {
                        Handle = CONTAINING_RECORD(Entry,
                                                   USBPORT_DEVICE_HANDLE,
                                                   DeviceHandleLink);

                        if (!(Handle->Flags & DEVICE_HANDLE_FLAG_REMOVED))
                        {
                            DeviceCount++;
                        }

                        Entry = Entry->Flink;
                    }
                    KeReleaseSpinLock(&FdoExtension->DeviceHandleSpinLock, OldIrql);

                    BwInfo->DeviceCount = DeviceCount;
                    BwInfo->TotalBusBandwidth = FdoExtension->TotalBusBandwidth;
                    BwInfo->Total32secBandwidth = FdoExtension->TotalBusBandwidth * USB2_FRAMES;

                    UserHeader->UsbUserStatusCode = UsbUserSuccess;
                    UserHeader->ActualBufferLength = sizeof(USBUSER_BANDWIDTH_INFO_REQUEST);
                    Status = STATUS_SUCCESS;
                    Information = UserHeader->ActualBufferLength;
                    break;
                }

                case USBUSER_GET_POWER_STATE_MAP:
                {
                    PUSBUSER_POWER_INFO_REQUEST PowerReq;
                    PUSB_POWER_INFO PowerInfo;
                    PUSBPORT_RHDEVICE_EXTENSION RhExtension;

                    if (UserHeader->RequestBufferLength < sizeof(USBUSER_POWER_INFO_REQUEST))
                    {
                        UserHeader->UsbUserStatusCode = UsbUserInvalidParameter;
                        UserHeader->ActualBufferLength = sizeof(USBUSER_POWER_INFO_REQUEST);
                        Status = STATUS_INVALID_PARAMETER;
                        Information = sizeof(USBUSER_REQUEST_HEADER);
                        break;
                    }

                    PowerReq = (PUSBUSER_POWER_INFO_REQUEST)UserHeader;
                    PowerInfo = &PowerReq->PowerInformation;
                    RhExtension = FdoExtension->RootHubPdoExtension;

                    RtlZeroMemory(PowerInfo, sizeof(USB_POWER_INFO));

                    PowerInfo->SystemState = WdmUsbPowerSystemWorking;
                    PowerInfo->HcDevicePowerState =
                        USBPORT_MapDevicePowerState(FdoExtension->CommonExtension.DevicePowerState);
                    PowerInfo->HcDeviceWake =
                        USBPORT_MapDevicePowerState(FdoExtension->Capabilities.DeviceWake);
                    PowerInfo->HcSystemWake =
                        USBPORT_MapSystemPowerState(FdoExtension->Capabilities.SystemWake);

                    if (RhExtension)
                    {
                        PowerInfo->RhDevicePowerState =
                            USBPORT_MapDevicePowerState(RhExtension->CommonExtension.DevicePowerState);
                        PowerInfo->RhDeviceWake =
                            USBPORT_MapDevicePowerState(RhExtension->Capabilities.DeviceWake);
                        PowerInfo->RhSystemWake =
                            USBPORT_MapSystemPowerState(RhExtension->Capabilities.SystemWake);
                    }
                    else
                    {
                        PowerInfo->RhDevicePowerState =
                            USBPORT_MapDevicePowerState(FdoExtension->Capabilities.DeviceState[PowerSystemWorking]);
                        PowerInfo->RhDeviceWake =
                            USBPORT_MapDevicePowerState(FdoExtension->Capabilities.DeviceWake);
                        PowerInfo->RhSystemWake =
                            USBPORT_MapSystemPowerState(FdoExtension->Capabilities.SystemWake);
                    }

                    PowerInfo->LastSystemSleepState = WdmUsbPowerNotMapped;
                    PowerInfo->CanWakeup =
                        FdoExtension->Capabilities.SystemWake != PowerSystemUnspecified &&
                        FdoExtension->Capabilities.DeviceWake != PowerDeviceUnspecified;
                    PowerInfo->IsPowered =
                        FdoExtension->CommonExtension.DevicePowerState == PowerDeviceD0;

                    UserHeader->UsbUserStatusCode = UsbUserSuccess;
                    UserHeader->ActualBufferLength = sizeof(USBUSER_POWER_INFO_REQUEST);
                    Status = STATUS_SUCCESS;
                    Information = UserHeader->ActualBufferLength;
                    break;
                }

                case USBUSER_GET_USB_DRIVER_VERSION:
                {
                    PUSBUSER_GET_DRIVER_VERSION Ver;
                    USB_DRIVER_VERSION_PARAMETERS *Params;

                    if (UserHeader->RequestBufferLength < sizeof(USBUSER_GET_DRIVER_VERSION))
                    {
                        UserHeader->UsbUserStatusCode = UsbUserInvalidParameter;
                        UserHeader->ActualBufferLength = sizeof(USBUSER_GET_DRIVER_VERSION);
                        Status = STATUS_INVALID_PARAMETER;
                        Information = sizeof(USBUSER_REQUEST_HEADER);
                        break;
                    }

                    Ver = (PUSBUSER_GET_DRIVER_VERSION)UserHeader;
                    Params = &Ver->Parameters;

                    RtlZeroMemory(Params, sizeof(USB_DRIVER_VERSION_PARAMETERS));

                    Params->DriverTrackingCode = 0;
                    Params->USBDI_Version = USBDI_VERSION;
                    Params->USBUSER_Version = USBUSER_VERSION;
#if DBG
                    Params->CheckedPortDriver = TRUE;
#else
                    Params->CheckedPortDriver = FALSE;
#endif
                    Params->CheckedMiniportDriver = FALSE;

                    if (FdoExtension->MiniPortInterface &&
                        (FdoExtension->MiniPortInterface->Packet.MiniPortFlags & USB_MINIPORT_FLAGS_USB2))
                    {
                        Params->USB_Version = 0x0200;
                    }
                    else
                    {
                        Params->USB_Version = 0x0110;
                    }

                    UserHeader->UsbUserStatusCode = UsbUserSuccess;
                    UserHeader->ActualBufferLength = sizeof(USBUSER_GET_DRIVER_VERSION);
                    Status = STATUS_SUCCESS;
                    Information = UserHeader->ActualBufferLength;
                    break;
                }

                case USBUSER_GET_USB2_HW_VERSION:
                {
                    PUSBUSER_GET_USB2HW_VERSION HwVersion;

                    if (UserHeader->RequestBufferLength < sizeof(USBUSER_GET_USB2HW_VERSION))
                    {
                        UserHeader->UsbUserStatusCode = UsbUserInvalidParameter;
                        UserHeader->ActualBufferLength = sizeof(USBUSER_GET_USB2HW_VERSION);
                        Status = STATUS_INVALID_PARAMETER;
                        Information = sizeof(USBUSER_REQUEST_HEADER);
                        break;
                    }

                    HwVersion = (PUSBUSER_GET_USB2HW_VERSION)UserHeader;
                    RtlZeroMemory(&HwVersion->Parameters,
                                  sizeof(HwVersion->Parameters));

                    if (FdoExtension->MiniPortInterface &&
                        (FdoExtension->MiniPortInterface->Packet.MiniPortFlags & USB_MINIPORT_FLAGS_USB2))
                    {
                        HwVersion->Parameters.Usb2HwRevision = FdoExtension->RevisionID;
                    }

                    UserHeader->UsbUserStatusCode = UsbUserSuccess;
                    UserHeader->ActualBufferLength = sizeof(USBUSER_GET_USB2HW_VERSION);
                    Status = STATUS_SUCCESS;
                    Information = UserHeader->ActualBufferLength;
                    break;
                }

                case USBUSER_GET_BUS_STATISTICS_0:
                {
                    PUSBUSER_BUS_STATISTICS_0_REQUEST StatReq;
                    PUSB_BUS_STATISTICS_0 Stats;
                    KIRQL OldIrql;
                    PLIST_ENTRY Entry;
                    PUSBPORT_DEVICE_HANDLE Handle;
                    ULONG DeviceCount = 0;
                    LARGE_INTEGER Now;
                    PUSBPORT_REGISTRATION_PACKET Packet;
                    ULONG FrameNumber = 0;

                    if (UserHeader->RequestBufferLength < sizeof(USBUSER_BUS_STATISTICS_0_REQUEST))
                    {
                        UserHeader->UsbUserStatusCode = UsbUserInvalidParameter;
                        UserHeader->ActualBufferLength = sizeof(USBUSER_BUS_STATISTICS_0_REQUEST);
                        Status = STATUS_INVALID_PARAMETER;
                        Information = sizeof(USBUSER_REQUEST_HEADER);
                        break;
                    }

                    StatReq = (PUSBUSER_BUS_STATISTICS_0_REQUEST)UserHeader;
                    Stats = &StatReq->BusStatistics0;

                    RtlZeroMemory(Stats, sizeof(USB_BUS_STATISTICS_0));

                    KeAcquireSpinLock(&FdoExtension->DeviceHandleSpinLock, &OldIrql);
                    Entry = FdoExtension->DeviceHandleList.Flink;
                    while (Entry != &FdoExtension->DeviceHandleList)
                    {
                        Handle = CONTAINING_RECORD(Entry,
                                                   USBPORT_DEVICE_HANDLE,
                                                   DeviceHandleLink);

                        if (!(Handle->Flags & DEVICE_HANDLE_FLAG_REMOVED))
                        {
                            DeviceCount++;
                        }

                        Entry = Entry->Flink;
                    }
                    KeReleaseSpinLock(&FdoExtension->DeviceHandleSpinLock, OldIrql);

                    Stats->DeviceCount = DeviceCount;

                    KeQuerySystemTime(&Now);
                    Stats->CurrentSystemTime = Now;

                    Packet = &FdoExtension->MiniPortInterface->Packet;
                    KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
                    FrameNumber = Packet->Get32BitFrameNumber(FdoExtension->MiniPortExt);
                    KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

                    Stats->CurrentUsbFrame = FrameNumber;

                    Stats->RootHubEnabled = TRUE;
                    Stats->RootHubDevicePowerState = (UCHAR)FdoExtension->Capabilities.DeviceState[PowerSystemWorking];

                    UserHeader->UsbUserStatusCode = UsbUserSuccess;
                    UserHeader->ActualBufferLength = sizeof(USBUSER_BUS_STATISTICS_0_REQUEST);
                    Status = STATUS_SUCCESS;
                    Information = UserHeader->ActualBufferLength;
                    break;
                }

                default:
                    UserHeader->UsbUserStatusCode = UsbUserNotSupported;
                    UserHeader->ActualBufferLength = sizeof(USBUSER_REQUEST_HEADER);
                    Status = STATUS_NOT_SUPPORTED;
                    Information = sizeof(USBUSER_REQUEST_HEADER);
                    break;
            }
            break;
        }

        case IOCTL_USB_GET_TRANSPORT_CHARACTERISTICS:
        {
            PUSB_TRANSPORT_CHARACTERISTICS Transport;

            if (!Irp->AssociatedIrp.SystemBuffer ||
                IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Transport))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Transport = Irp->AssociatedIrp.SystemBuffer;
            USBPORT_FillTransportCharacteristics(FdoExtension, Transport);
            Information = sizeof(*Transport);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_USB_REGISTER_FOR_TRANSPORT_CHARACTERISTICS_CHANGE:
        {
            PUSB_TRANSPORT_CHARACTERISTICS_CHANGE_REGISTRATION Registration;
            PUSBPORT_TRANSPORT_REGISTRATION Entry;
            KIRQL OldIrql;

            if (!Irp->AssociatedIrp.SystemBuffer ||
                IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Registration))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Registration = Irp->AssociatedIrp.SystemBuffer;

            Entry = ExAllocatePoolWithTag(NonPagedPool,
                                          sizeof(*Entry),
                                          USB_PORT_TAG);
            if (!Entry)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            Entry->Handle = (USB_CHANGE_REGISTRATION_HANDLE)Entry;
            Entry->Flags = Registration->ChangeNotificationInputFlags;
            Entry->PendingIrp = NULL;

            KeAcquireSpinLock(&FdoExtension->Aux.TransportListSpinLock, &OldIrql);
            InsertTailList(&FdoExtension->Aux.TransportRegistrationList, &Entry->ListEntry);
            KeReleaseSpinLock(&FdoExtension->Aux.TransportListSpinLock, OldIrql);

            Registration->Handle = Entry->Handle;
            USBPORT_FillTransportCharacteristics(FdoExtension,
                                                 &Registration->UsbTransportCharacteristics);

            Information = sizeof(*Registration);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_USB_NOTIFY_ON_TRANSPORT_CHARACTERISTICS_CHANGE:
        {
            PUSB_TRANSPORT_CHARACTERISTICS_CHANGE_NOTIFICATION Notification;
            PLIST_ENTRY ListEntry;
            PUSBPORT_TRANSPORT_REGISTRATION Entry;
            BOOLEAN Found = FALSE;
            BOOLEAN Busy = FALSE;
            KIRQL OldIrql;
            KIRQL CancelIrql;

            if (!Irp->AssociatedIrp.SystemBuffer ||
                IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Notification))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Notification = Irp->AssociatedIrp.SystemBuffer;

            KeAcquireSpinLock(&FdoExtension->Aux.TransportListSpinLock, &OldIrql);
            ListEntry = FdoExtension->Aux.TransportRegistrationList.Flink;
            while (ListEntry != &FdoExtension->Aux.TransportRegistrationList)
            {
                Entry = CONTAINING_RECORD(ListEntry,
                                          USBPORT_TRANSPORT_REGISTRATION,
                                          ListEntry);
                if (Entry->Handle == Notification->Handle)
                {
                    Found = TRUE;
                    if (Entry->PendingIrp)
                        Busy = TRUE;
                    else
                        Entry->PendingIrp = Irp;
                    break;
                }

                ListEntry = ListEntry->Flink;
            }
            KeReleaseSpinLock(&FdoExtension->Aux.TransportListSpinLock, OldIrql);

            if (!Found)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (Busy)
            {
                Status = STATUS_DEVICE_BUSY;
                break;
            }

            IoAcquireCancelSpinLock(&CancelIrql);
            if (Irp->Cancel)
            {
                IoReleaseCancelSpinLock(CancelIrql);

                KeAcquireSpinLock(&FdoExtension->Aux.TransportListSpinLock, &OldIrql);
                if (Entry->PendingIrp == Irp)
                    Entry->PendingIrp = NULL;
                KeReleaseSpinLock(&FdoExtension->Aux.TransportListSpinLock, OldIrql);

                Status = STATUS_CANCELLED;
                break;
            }

            IoMarkIrpPending(Irp);
            Irp->Tail.Overlay.DriverContext[0] = Entry;
            IoSetCancelRoutine(Irp, USBPORT_CancelTransportNotificationIrp);
            IoReleaseCancelSpinLock(CancelIrql);

            Irp->IoStatus.Status = STATUS_PENDING;
            Irp->IoStatus.Information = 0;
            return STATUS_PENDING;
        }

        case IOCTL_USB_UNREGISTER_FOR_TRANSPORT_CHARACTERISTICS_CHANGE:
        {
            PUSB_TRANSPORT_CHARACTERISTICS_CHANGE_UNREGISTRATION Unregister;
            PLIST_ENTRY ListEntry;
            PUSBPORT_TRANSPORT_REGISTRATION Entry;
            BOOLEAN Found = FALSE;
            KIRQL OldIrql;
            PIRP PendingIrp = NULL;

            if (!Irp->AssociatedIrp.SystemBuffer ||
                IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*Unregister))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Unregister = Irp->AssociatedIrp.SystemBuffer;

            KeAcquireSpinLock(&FdoExtension->Aux.TransportListSpinLock, &OldIrql);
            ListEntry = FdoExtension->Aux.TransportRegistrationList.Flink;
            while (ListEntry != &FdoExtension->Aux.TransportRegistrationList)
            {
                Entry = CONTAINING_RECORD(ListEntry,
                                          USBPORT_TRANSPORT_REGISTRATION,
                                          ListEntry);
                if (Entry->Handle == Unregister->Handle)
                {
                    RemoveEntryList(&Entry->ListEntry);
                    PendingIrp = Entry->PendingIrp;
                    Entry->PendingIrp = NULL;
                    Found = TRUE;
                    break;
                }

                ListEntry = ListEntry->Flink;
            }
            KeReleaseSpinLock(&FdoExtension->Aux.TransportListSpinLock, OldIrql);

            if (!Found)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (PendingIrp)
            {
                USBPORT_CompleteTransportNotificationIrp(FdoExtension,
                                                         PendingIrp,
                                                         STATUS_CANCELLED,
                                                         FALSE);
            }

            ExFreePoolWithTag(Entry, USB_PORT_TAG);
            Information = sizeof(*Unregister);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_USB_GET_DEVICE_CHARACTERISTICS:
        {
            PUSB_DEVICE_CHARACTERISTICS Characteristics;

            if (!Irp->AssociatedIrp.SystemBuffer ||
                IoStack->Parameters.DeviceIoControl.OutputBufferLength < sizeof(*Characteristics))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Characteristics = Irp->AssociatedIrp.SystemBuffer;
            USBPORT_FillDeviceCharacteristics(FdoExtension, Characteristics);
            Information = sizeof(*Characteristics);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_USB_START_TRACKING_FOR_TIME_SYNC:
        {
            PUSB_START_TRACKING_FOR_TIME_SYNC_INFORMATION TrackingInfo;
            PUSBPORT_TIMESYNC_CONTEXT Context;
            KIRQL OldIrql;

            if (!Irp->AssociatedIrp.SystemBuffer ||
                IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*TrackingInfo))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            TrackingInfo = Irp->AssociatedIrp.SystemBuffer;

            Context = ExAllocatePoolWithTag(NonPagedPool,
                                            sizeof(*Context),
                                            USB_PORT_TAG);
            if (!Context)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            Context->Handle = (HANDLE)Context;
            if (FdoExtension->Aux.NextTimeSyncId == 0)
                FdoExtension->Aux.NextTimeSyncId = 1;
            Context->GenerationID = FdoExtension->Aux.NextTimeSyncId;

            KeAcquireSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, &OldIrql);
            InsertTailList(&FdoExtension->Aux.TimeSyncTrackingList, &Context->ListEntry);
            KeReleaseSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, OldIrql);

            TrackingInfo->TimeTrackingHandle = Context->Handle;
            Information = sizeof(*TrackingInfo);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_USB_STOP_TRACKING_FOR_TIME_SYNC:
        {
            PUSB_STOP_TRACKING_FOR_TIME_SYNC_INFORMATION StopInfo;
            PLIST_ENTRY ListEntry;
            PUSBPORT_TIMESYNC_CONTEXT Context = NULL;
            KIRQL OldIrql;

            if (!Irp->AssociatedIrp.SystemBuffer ||
                IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*StopInfo))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            StopInfo = Irp->AssociatedIrp.SystemBuffer;

            KeAcquireSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, &OldIrql);
            ListEntry = FdoExtension->Aux.TimeSyncTrackingList.Flink;
            while (ListEntry != &FdoExtension->Aux.TimeSyncTrackingList)
            {
                Context = CONTAINING_RECORD(ListEntry,
                                            USBPORT_TIMESYNC_CONTEXT,
                                            ListEntry);
                if (Context->Handle == StopInfo->TimeTrackingHandle)
                {
                    RemoveEntryList(&Context->ListEntry);
                    break;
                }

                Context = NULL;
                ListEntry = ListEntry->Flink;
            }
            KeReleaseSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, OldIrql);

            if (!Context)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            ExFreePoolWithTag(Context, USB_PORT_TAG);
            Information = sizeof(*StopInfo);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_USB_GET_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC:
        {
            PUSB_FRAME_NUMBER_AND_QPC_FOR_TIME_SYNC_INFORMATION FrameInfo;
            PLIST_ENTRY ListEntry;
            PUSBPORT_TIMESYNC_CONTEXT Context = NULL;
            ULONG GenerationId = 0;
            KIRQL OldIrql;
            LARGE_INTEGER PerfFreq;
            LARGE_INTEGER Counter;
            ULONG FrameNumber = 0;
            ULONG MicroFrame = 0;
            PUSBPORT_REGISTRATION_PACKET Packet;

            if (!Irp->AssociatedIrp.SystemBuffer ||
                IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(*FrameInfo))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            FrameInfo = Irp->AssociatedIrp.SystemBuffer;

            KeAcquireSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, &OldIrql);
            ListEntry = FdoExtension->Aux.TimeSyncTrackingList.Flink;
            while (ListEntry != &FdoExtension->Aux.TimeSyncTrackingList)
            {
                Context = CONTAINING_RECORD(ListEntry,
                                            USBPORT_TIMESYNC_CONTEXT,
                                            ListEntry);
                if (Context->Handle == FrameInfo->TimeTrackingHandle)
                {
                    GenerationId = Context->GenerationID;
                    break;
                }

                Context = NULL;
                ListEntry = ListEntry->Flink;
            }
            KeReleaseSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, OldIrql);

            if (!Context)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            Counter = KeQueryPerformanceCounter(&PerfFreq);

            Packet = FdoExtension->MiniPortInterface ?
                     &FdoExtension->MiniPortInterface->Packet :
                     NULL;

            if (Packet && Packet->Get32BitFrameNumber)
            {
                KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
                FrameNumber = Packet->Get32BitFrameNumber(FdoExtension->MiniPortExt);
                KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);
            }

            if (Packet &&
                (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2))
            {
                MicroFrame = FrameNumber & 0x7;
            }

            FrameInfo->QueryPerformanceCounterAtInputFrameOrMicroFrame = Counter;
            FrameInfo->QueryPerformanceCounterFrequency = PerfFreq;
            FrameInfo->PredictedAccuracyInMicroSeconds = 50;
            FrameInfo->CurrentGenerationID = GenerationId;
            FrameInfo->CurrentQueryPerformanceCounter = Counter;
            FrameInfo->CurrentHardwareFrameNumber = FrameNumber & 0x7FF;
            FrameInfo->CurrentHardwareMicroFrameNumber = MicroFrame;
            FrameInfo->CurrentUSBFrameNumber = FrameNumber;

            Information = sizeof(*FrameInfo);
            Status = STATUS_SUCCESS;
            break;
        }

        default:
            DPRINT1("USBPORT_FdoDeviceControl: Not supported IoControlCode - %x\n",
                    ControlCode);
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

NTSTATUS
NTAPI
USBPORT_FdoInternalDeviceControl(IN PDEVICE_OBJECT FdoDevice,
                                 IN PIRP Irp)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PIO_STACK_LOCATION IoStack;
    ULONG IoCtl;
    NTSTATUS Status;

    FdoExtension = FdoDevice->DeviceExtension;
    IoStack = IoGetCurrentIrpStackLocation(Irp);
    IoCtl = IoStack->Parameters.DeviceIoControl.IoControlCode;

    //DPRINT("USBPORT_FdoInternalDeviceControl: FdoDevice - %p, Irp - %p, IoCtl - %x\n",
    //       FdoDevice,
    //       Irp,
    //       IoCtl);

    if (IoCtl == IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO)
    {
        PDEVICE_OBJECT RootHubPdo;

        RootHubPdo = FdoExtension->RootHubPdo;

        DPRINT1("USBPORT_FdoInternalDeviceControl: IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO Fdo=%p RootHubPdo=%p Arg1=%p Arg2=%p\n",
                FdoDevice,
                RootHubPdo,
                IoStack->Parameters.Others.Argument1,
                IoStack->Parameters.Others.Argument2);

        if (IoStack->Parameters.Others.Argument1)
        {
            *(PDEVICE_OBJECT *)IoStack->Parameters.Others.Argument1 = RootHubPdo;
        }

        if (IoStack->Parameters.Others.Argument2)
        {
            *(PDEVICE_OBJECT *)IoStack->Parameters.Others.Argument2 = RootHubPdo;
        }

        Status = RootHubPdo ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
        goto Exit;
    }

    if (IoCtl == IOCTL_INTERNAL_USB_GET_HUB_COUNT)
    {
        if (IoStack->Parameters.Others.Argument1)
        {
            ++*(PULONG)IoStack->Parameters.Others.Argument1;
        }

        Status = STATUS_SUCCESS;
        goto Exit;
    }

    DPRINT("USBPORT_FdoInternalDeviceControl: INVALID INTERNAL DEVICE CONTROL\n");
    Status = STATUS_INVALID_DEVICE_REQUEST;

Exit:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}
