/*
 * PROJECT:     ReactOS USB Hub Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     USBHub I/O control functions
 * COPYRIGHT:   Copyright 2017 Vadim Galyant <vgal@rambler.ru>
 */

#include "usbhub.h"

#define NDEBUG
#include <debug.h>

#define NDEBUG_USBHUB_IOCTL
#include "dbg_uhub.h"

static
NTSTATUS
USBH_GetMsOsVendorCode(IN PUSBHUB_PORT_PDO_EXTENSION PortExtension,
                       OUT PUCHAR VendorCode)
{
    struct _URB_CONTROL_DESCRIPTOR_REQUEST Urb;
    OS_STRING OsString;
    NTSTATUS Status;

    if (!VendorCode)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *VendorCode = 0;

    RtlZeroMemory(&OsString, sizeof(OsString));
    RtlZeroMemory(&Urb, sizeof(Urb));

    Urb.Hdr.Length = sizeof(Urb);
    Urb.Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    Urb.TransferBufferLength = sizeof(OsString);
    Urb.TransferBuffer = &OsString;
    Urb.Index = OS_STRING_DESCRIPTOR_INDEX;
    Urb.DescriptorType = USB_STRING_DESCRIPTOR_TYPE;
    Urb.LanguageId = 0;

    Status = USBH_SyncSubmitUrb(PortExtension->Common.SelfDevice, (PURB)&Urb);
    if (!NT_SUCCESS(Status) || !USBD_SUCCESS(Urb.Hdr.Status))
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (OsString.bLength < sizeof(OS_STRING) ||
        OsString.bDescriptorType != USB_STRING_DESCRIPTOR_TYPE ||
        RtlCompareMemory(OsString.MicrosoftString,
                         MS_OS_STRING_SIGNATURE,
                         sizeof(MS_OS_STRING_SIGNATURE) - sizeof(WCHAR)) !=
            sizeof(MS_OS_STRING_SIGNATURE) - sizeof(WCHAR))
    {
        return STATUS_NOT_SUPPORTED;
    }

    *VendorCode = OsString.bVendorCode;
    return STATUS_SUCCESS;
}

static
NTSTATUS
USBH_PdoHandleMsFeatureDescriptor(IN PUSBHUB_PORT_PDO_EXTENSION PortExtension,
                                  IN PIRP Irp,
                                  IN PURB Urb)
{
    PUSBHUB_FDO_EXTENSION HubExtension;
    ULONG TransferBufferLength;
    PVOID TransferBuffer;
    PMDL TransferBufferMDL;
    struct _URB *UrbLink;
    UCHAR InterfaceNumber;
    UCHAR PageIndex;
    USHORT FeatureIndex;
    UCHAR VendorCode;
    NTSTATUS Status;

    if (Urb->UrbHeader.Length < sizeof(struct _URB_OS_FEATURE_DESCRIPTOR_REQUEST))
    {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        USBH_CompleteIrp(Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    Status = USBH_GetMsOsVendorCode(PortExtension, &VendorCode);
    if (!NT_SUCCESS(Status))
    {
        Urb->UrbHeader.Status = USBD_STATUS_NOT_SUPPORTED;
        USBH_CompleteIrp(Irp, Status);
        return Status;
    }

    TransferBufferLength = Urb->UrbOSFeatureDescriptorRequest.TransferBufferLength;
    TransferBuffer = Urb->UrbOSFeatureDescriptorRequest.TransferBuffer;
    TransferBufferMDL = Urb->UrbOSFeatureDescriptorRequest.TransferBufferMDL;
    UrbLink = Urb->UrbOSFeatureDescriptorRequest.UrbLink;
    InterfaceNumber = Urb->UrbOSFeatureDescriptorRequest.InterfaceNumber;
    PageIndex = Urb->UrbOSFeatureDescriptorRequest.MS_PageIndex;
    FeatureIndex = Urb->UrbOSFeatureDescriptorRequest.MS_FeatureDescriptorIndex;

    RtlZeroMemory(&Urb->UrbControlVendorClassRequest,
                  sizeof(Urb->UrbControlVendorClassRequest));

    Urb->UrbControlVendorClassRequest.Hdr.Length =
        sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb->UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_VENDOR_DEVICE;
    Urb->UrbControlVendorClassRequest.TransferFlags =
        USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
    Urb->UrbControlVendorClassRequest.TransferBufferLength = TransferBufferLength;
    Urb->UrbControlVendorClassRequest.TransferBuffer = TransferBuffer;
    Urb->UrbControlVendorClassRequest.TransferBufferMDL = TransferBufferMDL;
    Urb->UrbControlVendorClassRequest.UrbLink = UrbLink;
    Urb->UrbControlVendorClassRequest.Request = VendorCode;
    Urb->UrbControlVendorClassRequest.Value =
        ((USHORT)InterfaceNumber << 8) | PageIndex;
    Urb->UrbControlVendorClassRequest.Index = FeatureIndex;

    HubExtension = PortExtension->HubExtension;
    return USBH_PassIrp(HubExtension->RootHubPdo2, Irp);
}

NTSTATUS
NTAPI
USBH_SelectConfigOrInterfaceComplete(IN PDEVICE_OBJECT DeviceObject,
                                     IN PIRP Irp,
                                     IN PVOID Context)
{
    PUSBHUB_PORT_PDO_EXTENSION PortExtension;
    PUSBHUB_FDO_EXTENSION HubExtension;
    PVOID TimeoutContext; // PUSBHUB_BANDWIDTH_TIMEOUT_CONTEXT
    PUSBHUB_PORT_DATA PortData = NULL;
    NTSTATUS Status;
    KIRQL OldIrql;
    PURB Urb;

    DPRINT("USBH_SelectConfigOrInterfaceComplete ... \n");

    if (Irp->PendingReturned)
    {
        IoMarkIrpPending(Irp);
    }

    PortExtension = Context;
    HubExtension = PortExtension->HubExtension;

    ASSERT(PortExtension->PortNumber > 0);

    if (HubExtension)
    {
        PortData = &HubExtension->PortData[PortExtension->PortNumber - 1];
    }

    Status = Irp->IoStatus.Status;

    if (NT_SUCCESS(Irp->IoStatus.Status))
    {
        KeAcquireSpinLock(&PortExtension->PortTimeoutSpinLock, &OldIrql);

        TimeoutContext = PortExtension->BndwTimeoutContext;

        if (TimeoutContext)
        {
            PortExtension->BndwTimeoutContext = NULL;
        }

        KeReleaseSpinLock(&PortExtension->PortTimeoutSpinLock, OldIrql);

        PortExtension->PortPdoFlags &= ~(USBHUB_PDO_FLAG_PORT_RESTORE_FAIL |
                                         USBHUB_PDO_FLAG_ALLOC_BNDW_FAILED);

        if (PortData && PortData->ConnectionStatus != DeviceHubNestedTooDeeply)
        {
            PortData->ConnectionStatus = DeviceConnected;
        }
    }
    else
    {
        Urb = URB_FROM_IRP(Irp);
        DPRINT1("USBH_SelectConfigOrInterfaceComplete: Status = 0x%lx, UsbdStatus = 0x%lx\n", Status, Urb->UrbHeader.Status);
        if (Urb->UrbHeader.Status == USBD_STATUS_NO_BANDWIDTH)
        {
            PortExtension->PortPdoFlags |= USBHUB_PDO_FLAG_ALLOC_BNDW_FAILED;

            if (PortData)
                PortData->ConnectionStatus = DeviceGeneralFailure;

            IoInvalidateDeviceState(DeviceObject);
        }
    }

    return Status;
}

NTSTATUS
NTAPI
USBH_PdoUrbFilter(IN PUSBHUB_PORT_PDO_EXTENSION PortExtension,
                  IN PIRP Irp)
{
    PUSB_CONFIGURATION_DESCRIPTOR ConfigDescriptor;
    PUSBHUB_FDO_EXTENSION HubExtension;
    PDEVICE_OBJECT DeviceObject;
    PURB Urb;
    USHORT Function;
    ULONG MaxPower;
    USBD_STATUS UrbStatus;
    BOOLEAN IsValidConfig;

    HubExtension = PortExtension->HubExtension;
    DeviceObject = PortExtension->Common.SelfDevice;

    Urb = URB_FROM_IRP(Irp);

    DPRINT_IOCTL("USBH_PdoUrbFilter: Device - %p, Irp - %p, Urb - %p\n",
                 DeviceObject,
                 Irp,
                 Urb);

    if (PortExtension->PortPdoFlags & (USBHUB_PDO_FLAG_PORT_RESTORE_FAIL |
                                       USBHUB_PDO_FLAG_PORT_RESSETING))
    {
        Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
        USBH_CompleteIrp(Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    Function = Urb->UrbHeader.Function;

    switch (Function)
    {
        case URB_FUNCTION_SELECT_CONFIGURATION:
        {
            ConfigDescriptor = Urb->UrbSelectConfiguration.ConfigurationDescriptor;

            if (ConfigDescriptor)
            {
                IsValidConfig = TRUE;

                if (ConfigDescriptor->bDescriptorType != USB_CONFIGURATION_DESCRIPTOR_TYPE)
                {
                    DPRINT1("USBH_PdoUrbFilter: Not valid Cfg. bDescriptorType\n");
                    IsValidConfig = FALSE;
                    UrbStatus = USBD_STATUS_INVALID_CONFIGURATION_DESCRIPTOR;
                }

                if (ConfigDescriptor->bLength < sizeof(USB_CONFIGURATION_DESCRIPTOR))
                {
                    DPRINT1("USBH_PdoUrbFilter: Size Cfg. descriptor is too small\n");
                    IsValidConfig = FALSE;
                    UrbStatus = USBD_STATUS_INVALID_CONFIGURATION_DESCRIPTOR;
                }

                if (!IsValidConfig)
                {
                    Urb->UrbHeader.Status = UrbStatus;
                    USBH_CompleteIrp(Irp, STATUS_INVALID_PARAMETER);
                    return STATUS_INVALID_PARAMETER;
                }

                MaxPower = 2 * ConfigDescriptor->MaxPower;
                PortExtension->MaxPower = MaxPower;

                if (HubExtension->MaxPowerPerPort < MaxPower)
                {
                    PortExtension->PortPdoFlags |= USBHUB_PDO_FLAG_INSUFFICIENT_PWR;

                    if (PortExtension->PortNumber > 0)
                    {
                        HubExtension->PortData[PortExtension->PortNumber - 1].ConnectionStatus =
                            DeviceGeneralFailure;
                    }

                    DPRINT1("USBH_PdoUrbFilter: insufficient power (requested=%lu, max=%lu), failing config\n",
                            MaxPower, HubExtension->MaxPowerPerPort);

                    Urb->UrbHeader.Status = USBD_STATUS_INSUFFICIENT_RESOURCES;
                    IoInvalidateDeviceState(DeviceObject);

                    USBH_CompleteIrp(Irp, STATUS_INVALID_PARAMETER);
                    return STATUS_INVALID_PARAMETER;
                }
            }
        }

        /* fall through */

        case URB_FUNCTION_SELECT_INTERFACE:
        {
            IoCopyCurrentIrpStackLocationToNext(Irp);

            IoSetCompletionRoutine(Irp,
                                   USBH_SelectConfigOrInterfaceComplete,
                                   PortExtension,
                                   TRUE,
                                   TRUE,
                                   TRUE);

            return IoCallDriver(HubExtension->RootHubPdo2, Irp);
        }

        case URB_FUNCTION_CONTROL_TRANSFER:
        case URB_FUNCTION_BULK_OR_INTERRUPT_TRANSFER:
        case URB_FUNCTION_ISOCH_TRANSFER:
        {
            if (PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_DELETE_PENDING)
            {
                Urb->UrbHeader.Status = USBD_STATUS_INVALID_PARAMETER;
                USBH_CompleteIrp(Irp, STATUS_DELETE_PENDING);
                return STATUS_DELETE_PENDING;
            }

            break;
        }

        case URB_FUNCTION_GET_MS_FEATURE_DESCRIPTOR:
            return USBH_PdoHandleMsFeatureDescriptor(PortExtension, Irp, Urb);

        default:
            break;
    }

    return USBH_PassIrp(HubExtension->RootHubPdo2, Irp);
}

NTSTATUS
NTAPI
USBH_PdoIoctlSubmitUrb(IN PUSBHUB_PORT_PDO_EXTENSION PortExtension,
                       IN PIRP Irp)
{
    PUSBHUB_FDO_EXTENSION HubExtension;
    PURB Urb;
    NTSTATUS Status;

    DPRINT_IOCTL("USBH_PdoIoctlSubmitUrb ... \n");

    HubExtension = PortExtension->HubExtension;

    Urb = URB_FROM_IRP(Irp);

    if (PortExtension->DeviceHandle == NULL)
    {
        /* USBPORT maps a NULL handle to the root hub itself; never forward for a removed device */
        DPRINT1("USBH_PdoIoctlSubmitUrb: failing URB 0x%02X for removed device\n", Urb->UrbHeader.Function);
        Urb->UrbHeader.Status = USBD_STATUS_DEVICE_GONE;
        USBH_CompleteIrp(Irp, STATUS_DEVICE_NOT_CONNECTED);
        Status = STATUS_DEVICE_NOT_CONNECTED;
    }
    else
    {
        Urb->UrbHeader.UsbdDeviceHandle = PortExtension->DeviceHandle;
        Status = USBH_PdoUrbFilter(PortExtension, Irp);
    }

    return Status;
}

NTSTATUS
NTAPI
USBH_PdoIoctlGetPortStatus(IN PUSBHUB_PORT_PDO_EXTENSION PortExtension,
                           IN PIRP Irp)
{
    PUSBHUB_FDO_EXTENSION HubExtension;
    PUSBHUB_PORT_DATA PortData;
    PIO_STACK_LOCATION IoStack;
    PULONG PortStatus;
    NTSTATUS Status;

    DPRINT1("USBH_PdoIoctlGetPortStatus: Port %u PDO %p\n",
            PortExtension->PortNumber,
            PortExtension->Common.SelfDevice);

    HubExtension = PortExtension->HubExtension;

    InterlockedIncrement(&HubExtension->PendingRequestCount);

    KeWaitForSingleObject(&HubExtension->HubSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    ASSERT(PortExtension->PortNumber > 0);
    PortData = &HubExtension->PortData[PortExtension->PortNumber - 1];

    Status = USBH_SyncGetPortStatus(HubExtension,
                                    PortExtension->PortNumber,
                                    &PortData->PortStatus,
                                    sizeof(USB_PORT_STATUS_AND_CHANGE));

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    PortStatus = IoStack->Parameters.Others.Argument1;

    *PortStatus = 0;

    if (PortExtension->Common.SelfDevice == PortData->DeviceObject)
    {
        if (PortData->PortStatus.PortStatus.Usb20PortStatus.PortEnabledDisabled)
        {
            *PortStatus |= USBD_PORT_ENABLED;
        }

        if (USBH_PortStatusIsConnected(&PortData->PortStatus))
        {
            *PortStatus |= USBD_PORT_CONNECTED;
        }
    }

    KeReleaseSemaphore(&HubExtension->HubSemaphore,
                       LOW_REALTIME_PRIORITY,
                       1,
                       FALSE);

    if (!InterlockedDecrement(&HubExtension->PendingRequestCount))
    {
        KeSetEvent(&HubExtension->PendingRequestEvent,
                   EVENT_INCREMENT,
                   FALSE);
    }

    DPRINT1("USBH_PdoIoctlGetPortStatus: Status %x RawPortStatus 0x%08lx Flags %s%s\n",
            Status,
            *(ULONG *)&PortData->PortStatus.PortStatus,
            (*PortStatus & USBD_PORT_CONNECTED) ? "C" : "-",
            (*PortStatus & USBD_PORT_ENABLED) ? "E" : "-");

    USBH_CompleteIrp(Irp, Status);

    return Status;
}

VOID
NTAPI
USBH_ResetPortWorker(IN PUSBHUB_FDO_EXTENSION HubExtension,
                     IN PVOID Context)
{
    PUSBHUB_RESET_PORT_CONTEXT WorkItemReset;
    PUSBHUB_PORT_PDO_EXTENSION PortExtension;
    PUSB_DEVICE_HANDLE DeviceHandle;
    NTSTATUS Status;
    USHORT Port;

    DPRINT("USBH_ResetPortWorker ... \n");

    WorkItemReset = Context;

    PortExtension = WorkItemReset->PortExtension;

    if (!HubExtension)
    {
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }

    InterlockedIncrement(&HubExtension->PendingRequestCount);

    KeWaitForSingleObject(&HubExtension->HubSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    Port = PortExtension->PortNumber;
    DeviceHandle = PortExtension->DeviceHandle;

    ASSERT(Port > 0);

    if (PortExtension->Common.SelfDevice == HubExtension->PortData[Port-1].DeviceObject &&
        DeviceHandle != NULL)
    {
        USBD_RemoveDeviceEx(HubExtension,
                            DeviceHandle,
                            USBD_MARK_DEVICE_BUSY);

        Status = USBH_ResetDevice(HubExtension,
                                  Port,
                                  TRUE,
                                  FALSE);
    }
    else
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    KeReleaseSemaphore(&HubExtension->HubSemaphore,
                       LOW_REALTIME_PRIORITY,
                       1,
                       FALSE);

    if (!InterlockedDecrement(&HubExtension->PendingRequestCount))
    {
        KeSetEvent(&HubExtension->PendingRequestEvent,
                   EVENT_INCREMENT,
                   FALSE);
    }

Exit:

    PortExtension->PortPdoFlags &= ~USBHUB_PDO_FLAG_PORT_RESSETING;

    USBH_CompleteIrp(WorkItemReset->Irp, Status);

    WorkItemReset->PortExtension->PortPdoFlags &= ~USBHUB_PDO_FLAG_PORT_RESTORE_FAIL;
}

NTSTATUS
NTAPI
USBH_PdoIoctlResetPort(IN PUSBHUB_PORT_PDO_EXTENSION PortExtension,
                       IN PIRP Irp)
{
    PUSBHUB_FDO_EXTENSION HubExtension;
    PUSBHUB_RESET_PORT_CONTEXT HubWorkItemBuffer;
    PUSBHUB_IO_WORK_ITEM HubIoWorkItem;
    NTSTATUS Status;

    HubExtension = PortExtension->HubExtension;

    DPRINT("USBH_PdoIoctlResetPort ... \n");

    if (PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_PORT_RESSETING)
    {
        Status = STATUS_UNSUCCESSFUL;
        USBH_CompleteIrp(Irp, Status);
        return Status;
    }

    Status = USBH_AllocateWorkItem(HubExtension,
                                   &HubIoWorkItem,
                                   USBH_ResetPortWorker,
                                   sizeof(USBHUB_RESET_PORT_CONTEXT),
                                   (PVOID *)&HubWorkItemBuffer,
                                   DelayedWorkQueue);

    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_UNSUCCESSFUL;
        USBH_CompleteIrp(Irp, Status);
        return Status;
    }

    PortExtension->PortPdoFlags |= USBHUB_PDO_FLAG_PORT_RESSETING;
    IoMarkIrpPending(Irp);

    HubWorkItemBuffer->PortExtension = PortExtension;
    HubWorkItemBuffer->Irp = Irp;

    Status = STATUS_PENDING;

    USBH_QueueWorkItem(PortExtension->HubExtension, HubIoWorkItem);

    return Status;
}

VOID
NTAPI
USBH_PortIdleNotificationCancelRoutine(IN PDEVICE_OBJECT Device,
                                       IN PIRP Irp)
{
    PUSBHUB_PORT_PDO_EXTENSION PortExtension;
    PUSBHUB_FDO_EXTENSION HubExtension;
    PIRP PendingIdleIrp = NULL;
    PUSBHUB_IO_WORK_ITEM HubIoWorkItem;
    PUSBHUB_IDLE_PORT_CANCEL_CONTEXT HubWorkItemBuffer;
    NTSTATUS Status;

    DPRINT("USBH_PortIdleNotificationCancelRoutine ... \n");

    PortExtension = Device->DeviceExtension;
    PortExtension->PortPdoFlags &= ~USBHUB_PDO_FLAG_IDLE_NOTIFICATION;

    HubExtension = PortExtension->HubExtension;
    ASSERT(HubExtension);

    PortExtension->IdleNotificationIrp = NULL;

    if (HubExtension->HubFlags & USBHUB_FDO_FLAG_WAIT_IDLE_REQUEST)
    {
        PendingIdleIrp = HubExtension->PendingIdleIrp;
        HubExtension->PendingIdleIrp = NULL;
    }

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    if (PendingIdleIrp)
    {
        USBH_HubCancelIdleIrp(HubExtension, PendingIdleIrp);
    }

    if (HubExtension->CurrentPowerState.DeviceState == PowerDeviceD0)
    {
        goto ErrorExit;
    }

    Status = USBH_AllocateWorkItem(HubExtension,
                                   &HubIoWorkItem,
                                   USBH_IdleCancelPowerHubWorker,
                                   sizeof(USBHUB_IDLE_PORT_CANCEL_CONTEXT),
                                   (PVOID *)&HubWorkItemBuffer,
                                   DelayedWorkQueue);

    if (NT_SUCCESS(Status))
    {
        HubWorkItemBuffer->Irp = Irp;
        USBH_QueueWorkItem(HubExtension, HubIoWorkItem);
        return;
    }

ErrorExit:

    Irp->IoStatus.Status = STATUS_CANCELLED;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

NTSTATUS
NTAPI
USBH_PortIdleNotificationRequest(IN PUSBHUB_PORT_PDO_EXTENSION PortExtension,
                                 IN PIRP Irp)
{
    PUSBHUB_FDO_EXTENSION HubExtension;
    PIO_STACK_LOCATION IoStack;
    PUSB_IDLE_CALLBACK_INFO IdleCallbackInfo;
    NTSTATUS Status;
    KIRQL Irql;

    DPRINT("USBH_PortIdleNotificationRequest ... \n");

    HubExtension = PortExtension->HubExtension;

    IoAcquireCancelSpinLock(&Irql);

    if (PortExtension->IdleNotificationIrp)
    {
        IoReleaseCancelSpinLock(Irql);
        Irp->IoStatus.Status = STATUS_DEVICE_BUSY;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_DEVICE_BUSY;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    IdleCallbackInfo = IoStack->Parameters.DeviceIoControl.Type3InputBuffer;

    if (!IdleCallbackInfo || !IdleCallbackInfo->IdleCallback)
    {
        IoReleaseCancelSpinLock(Irql);

        Status = STATUS_NO_CALLBACK_ACTIVE;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);

        return Status;
    }

    IoSetCancelRoutine(Irp, USBH_PortIdleNotificationCancelRoutine);

    if (Irp->Cancel)
    {
        if (IoSetCancelRoutine(Irp, NULL))
        {
            IoReleaseCancelSpinLock(Irql);
            Status = STATUS_CANCELLED;
            Irp->IoStatus.Status = STATUS_CANCELLED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
        }
        else
        {
            IoMarkIrpPending(Irp);
            IoReleaseCancelSpinLock(Irql);
            Status = STATUS_PENDING;
        }
    }
    else
    {
        PortExtension->PortPdoFlags |= USBHUB_PDO_FLAG_IDLE_NOTIFICATION;

        PortExtension->IdleNotificationIrp = Irp;
        IoMarkIrpPending(Irp);

        IoReleaseCancelSpinLock(Irql);
        Status = STATUS_PENDING;

        DPRINT("USBH_PortIdleNotificationRequest: IdleNotificationIrp - %p\n",
               PortExtension->IdleNotificationIrp);

        USBH_CheckIdleDeferred(HubExtension);
    }

    return Status;
}

NTSTATUS
NTAPI
USBH_IoctlGetNodeName(IN PUSBHUB_FDO_EXTENSION HubExtension,
                      IN PIRP Irp)
{
    PUSB_NODE_CONNECTION_NAME ConnectionName;
    PDEVICE_OBJECT PortDevice;
    PUSBHUB_PORT_PDO_EXTENSION PortExtension;
    size_t LengthSkip;
    PWCHAR Buffer;
    ULONG BufferLength;
    PWCHAR BufferEnd;
    ULONG_PTR LengthReturned;
    size_t LengthName;
    ULONG Length;
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    ULONG_PTR Information;

    DPRINT("USBH_IoctlGetNodeName ... \n");

    Status = STATUS_SUCCESS;

    ConnectionName = Irp->AssociatedIrp.SystemBuffer;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Length = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (Length < sizeof(USB_NODE_CONNECTION_NAME))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        Information = Irp->IoStatus.Information;
        goto Exit;
    }

    if (ConnectionName->ConnectionIndex == 0 ||
        ConnectionName->ConnectionIndex > HubExtension->HubDescriptor->bNumberOfPorts)
    {
        Status = STATUS_INVALID_PARAMETER;
        Information = Irp->IoStatus.Information;
        goto Exit;
    }

    PortDevice = HubExtension->PortData[ConnectionName->ConnectionIndex - 1].DeviceObject;

    if (!PortDevice)
    {
        ConnectionName->NodeName[0] = 0;
        ConnectionName->ActualLength = sizeof(USB_NODE_CONNECTION_NAME);

        Information = sizeof(USB_NODE_CONNECTION_NAME);
        goto Exit;
    }

    PortExtension = PortDevice->DeviceExtension;

    if (!(PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_HUB_DEVICE) ||
        !(PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_DEVICE_STARTED) ||
        !(PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_REG_DEV_INTERFACE))
    {
        ConnectionName->NodeName[0] = 0;
        ConnectionName->ActualLength = sizeof(USB_NODE_CONNECTION_NAME);

        Information = sizeof(USB_NODE_CONNECTION_NAME);
        goto Exit;
    }

    Buffer = PortExtension->SymbolicLinkName.Buffer;
    BufferLength = PortExtension->SymbolicLinkName.Length;

    ASSERT(Buffer[BufferLength / sizeof(WCHAR)] == UNICODE_NULL);

    LengthSkip = 0;

    if (*Buffer == L'\\')
    {
        BufferEnd = wcschr(Buffer + 1, L'\\');

        if (BufferEnd != NULL)
        {
            LengthSkip = (BufferEnd + 1 - Buffer) * sizeof(WCHAR);
        }
        else
        {
            LengthSkip = PortExtension->SymbolicLinkName.Length;
        }
    }

    LengthName = BufferLength - LengthSkip;

    ConnectionName->ActualLength = 0;

    RtlZeroMemory(ConnectionName->NodeName,
                  Length - FIELD_OFFSET(USB_NODE_CONNECTION_NAME, NodeName));

    LengthReturned = sizeof(USB_NODE_CONNECTION_NAME) + LengthName;

    if (Length < LengthReturned)
    {
        ConnectionName->NodeName[0] = 0;
        ConnectionName->ActualLength = LengthReturned;

        Information = sizeof(USB_NODE_CONNECTION_NAME);
        goto Exit;
    }

    RtlCopyMemory(&ConnectionName->NodeName[0],
                  &Buffer[LengthSkip / sizeof(WCHAR)],
                  LengthName);

    ConnectionName->ActualLength = LengthReturned;

    Status = STATUS_SUCCESS;
    Information = LengthReturned;

Exit:
    Irp->IoStatus.Information = Information;
    USBH_CompleteIrp(Irp, Status);
    return Status;
}

NTSTATUS
NTAPI
USBH_IoctlGetNodeInformation(IN PUSBHUB_FDO_EXTENSION HubExtension,
                             IN PIRP Irp)
{
    PUSB_NODE_INFORMATION NodeInfo;
    PIO_STACK_LOCATION IoStack;
    ULONG BufferLength;
    NTSTATUS Status;
    BOOLEAN HubIsBusPowered;

    DPRINT("USBH_IoctlGetNodeInformation ... \n");

    Status = STATUS_SUCCESS;

    NodeInfo = Irp->AssociatedIrp.SystemBuffer;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    BufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    RtlZeroMemory(Irp->AssociatedIrp.SystemBuffer, BufferLength);

    if (BufferLength < sizeof(USB_NODE_INFORMATION))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        USBH_CompleteIrp(Irp, Status);
        return Status;
    }

    NodeInfo->NodeType = UsbHub;

    RtlCopyMemory(&NodeInfo->u.HubInformation.HubDescriptor,
                  HubExtension->HubDescriptor,
                  sizeof(USB_HUB_DESCRIPTOR));

    HubIsBusPowered = USBH_HubIsBusPowered(HubExtension->Common.SelfDevice,
                                           HubExtension->HubConfigDescriptor);

    NodeInfo->u.HubInformation.HubIsBusPowered = HubIsBusPowered;

    Irp->IoStatus.Information = sizeof(USB_NODE_INFORMATION);

    USBH_CompleteIrp(Irp, Status);

    return Status;
}

NTSTATUS
NTAPI
USBH_IoctlGetHubCapabilities(IN PUSBHUB_FDO_EXTENSION HubExtension,
                             IN PIRP Irp)
{
    PUSB_HUB_CAPABILITIES Capabilities;
    PIO_STACK_LOCATION IoStack;
    ULONG BufferLength;
    ULONG Length;
    USB_HUB_CAPABILITIES HubCaps;

    DPRINT("USBH_IoctlGetHubCapabilities ... \n");

    Capabilities = Irp->AssociatedIrp.SystemBuffer;

    HubCaps.HubIs2xCapable = (HubExtension->HubFlags & USBHUB_FDO_FLAG_USB20_HUB) ==
                              USBHUB_FDO_FLAG_USB20_HUB;

    IoStack = IoGetCurrentIrpStackLocation(Irp);

    BufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (BufferLength == 0)
    {
        Irp->IoStatus.Information = BufferLength;
        USBH_CompleteIrp(Irp, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    if (BufferLength <= sizeof(HubCaps))
    {
        Length = IoStack->Parameters.DeviceIoControl.OutputBufferLength;
    }
    else
    {
        Length = sizeof(HubCaps);
    }

    RtlZeroMemory(Capabilities, BufferLength);
    RtlCopyMemory(Capabilities, &HubCaps, Length);

    Irp->IoStatus.Information = Length;

    USBH_CompleteIrp(Irp, STATUS_SUCCESS);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
USBH_IoctlGetHubInformationEx(IN PUSBHUB_FDO_EXTENSION HubExtension,
                              IN PIRP Irp)
{
    PUSB_HUB_INFORMATION_EX HubInfo;
    PIO_STACK_LOCATION IoStack;
    ULONG BufferLength;
    NTSTATUS Status = STATUS_SUCCESS;
    BOOLEAN IsRoot;
    ULONG CopyLength;

    DPRINT("USBH_IoctlGetHubInformationEx ... \n");

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    BufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (BufferLength < sizeof(USB_HUB_INFORMATION_EX) ||
        !Irp->AssociatedIrp.SystemBuffer)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        USBH_CompleteIrp(Irp, Status);
        return Status;
    }

    HubInfo = Irp->AssociatedIrp.SystemBuffer;
    RtlZeroMemory(HubInfo, BufferLength);

    IsRoot = (HubExtension->LowerPDO == HubExtension->RootHubPdo);

    if (IsRoot)
        HubInfo->HubType = UsbRootHub;
    else if (HubExtension->HubFlags & USBHUB_FDO_FLAG_USB20_HUB)
        HubInfo->HubType = Usb20Hub;
    else
        HubInfo->HubType = Usb20Hub;

    if (HubExtension->HubDescriptor)
    {
        HubInfo->HighestPortNumber = HubExtension->HubDescriptor->bNumberOfPorts;

        CopyLength = min(sizeof(HubInfo->u.UsbHubDescriptor),
                         HubExtension->HubDescriptor->bDescriptorLength);
        RtlCopyMemory(&HubInfo->u.UsbHubDescriptor,
                      HubExtension->HubDescriptor,
                      CopyLength);
    }

    Irp->IoStatus.Information = sizeof(USB_HUB_INFORMATION_EX);
    USBH_CompleteIrp(Irp, Status);

    return Status;
}

NTSTATUS
NTAPI
USBH_IoctlGetNodeConnectionAttributes(IN PUSBHUB_FDO_EXTENSION HubExtension,
                                      IN PIRP Irp)
{
    PUSB_NODE_CONNECTION_ATTRIBUTES Attributes;
    ULONG ConnectionIndex;
    ULONG NumPorts;
    NTSTATUS Status;
    PUSBHUB_PORT_DATA PortData;
    PIO_STACK_LOCATION IoStack;
    ULONG BufferLength;

    DPRINT("USBH_IoctlGetNodeConnectionAttributes ... \n");

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    BufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (BufferLength < sizeof(USB_NODE_CONNECTION_ATTRIBUTES))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Attributes = Irp->AssociatedIrp.SystemBuffer;

    ConnectionIndex = Attributes->ConnectionIndex;
    RtlZeroMemory(Attributes, BufferLength);
    Attributes->ConnectionIndex = ConnectionIndex;

    Status = STATUS_INVALID_PARAMETER;

    NumPorts = HubExtension->HubDescriptor->bNumberOfPorts;

    if (NumPorts == 0 ||
        ConnectionIndex == 0 ||
        ConnectionIndex > NumPorts)
    {
        goto Exit;
    }

    PortData = HubExtension->PortData + (ConnectionIndex - 1);

    Attributes->ConnectionStatus = PortData->ConnectionStatus;
    Attributes->PortAttributes = PortData->PortAttributes;

    Irp->IoStatus.Information = sizeof(USB_NODE_CONNECTION_ATTRIBUTES);
    Status = STATUS_SUCCESS;

Exit:

    USBH_CompleteIrp(Irp, Status);
    return Status;
}

NTSTATUS
NTAPI
USBH_IoctlGetPortConnectorProperties(IN PUSBHUB_FDO_EXTENSION HubExtension,
                                     IN PIRP Irp)
{
    PUSB_PORT_CONNECTOR_PROPERTIES Properties;
    PIO_STACK_LOCATION IoStack;
    ULONG BufferLength;
    ULONG ConnectionIndex;
    ULONG NumPorts;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;
    PUSBHUB_PORT_DATA PortData;
    WCHAR ValueName[48];
    ULONG ValueNameLength;
    ULONG SymbolicLinkLength;

    DPRINT("USBH_IoctlGetPortConnectorProperties ... \n");

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    BufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (BufferLength < sizeof(USB_PORT_CONNECTOR_PROPERTIES) ||
        !Irp->AssociatedIrp.SystemBuffer)
    {
        USBH_CompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL);
        return STATUS_BUFFER_TOO_SMALL;
    }

    Properties = Irp->AssociatedIrp.SystemBuffer;
    ConnectionIndex = Properties->ConnectionIndex;
    RtlZeroMemory(Properties, BufferLength);
    Properties->ConnectionIndex = ConnectionIndex;

    if (!HubExtension->HubDescriptor)
    {
        USBH_CompleteIrp(Irp, Status);
        return Status;
    }

    NumPorts = HubExtension->HubDescriptor->bNumberOfPorts;

    if (ConnectionIndex == 0 || ConnectionIndex > NumPorts)
    {
        USBH_CompleteIrp(Irp, Status);
        return Status;
    }

    PortData = HubExtension->PortData + (ConnectionIndex - 1);

    Properties->ActualLength = sizeof(USB_PORT_CONNECTOR_PROPERTIES);
    Properties->UsbPortProperties.PortIsUserConnectable =
        (PortData->PortAttributes & USB_PORTATTR_NO_CONNECTOR) ? 0 : 1;
    Properties->UsbPortProperties.PortIsDebugCapable =
        (PortData->PortAttributes & USB_PORTATTR_DEBUG_CAPABLE) ? 1 : 0;
    Properties->UsbPortProperties.PortHasMultipleCompanions =
        (PortData->PortAttributes & USB_PORTATTR_SHARED_USB2) ? 1 : 0;
    Properties->UsbPortProperties.PortConnectorIsTypeC =
        (PortData->PortAttributes & USB_PORTATTR_TYPEC_CONNECTOR) ? 1 : 0;

    Properties->CompanionIndex =
        (USHORT)((PortData->PortAttributes & USB_PORTATTR_COMPANION_INDEX_MASK) >>
                 USB_PORTATTR_COMPANION_INDEX_SHIFT);
    Properties->CompanionPortNumber =
        (USHORT)((PortData->PortAttributes & USB_PORTATTR_COMPANION_PORT_MASK) >>
                 USB_PORTATTR_COMPANION_PORT_SHIFT);
    Properties->CompanionHubSymbolicLinkName[0] = UNICODE_NULL;

    if (Properties->CompanionIndex && Properties->CompanionPortNumber)
    {
        if (NT_SUCCESS(RtlStringCchPrintfW(ValueName,
                                           ARRAYSIZE(ValueName),
                                           L"CompanionHub%02u",
                                           ConnectionIndex)))
        {
            ValueNameLength = (ULONG)((wcslen(ValueName) + 1) * sizeof(WCHAR));
            SymbolicLinkLength = BufferLength -
                FIELD_OFFSET(USB_PORT_CONNECTOR_PROPERTIES, CompanionHubSymbolicLinkName);
            if (SymbolicLinkLength == 0 ||
                !NT_SUCCESS(USBD_GetPdoRegistryParameter(HubExtension->LowerPDO,
                                                         Properties->CompanionHubSymbolicLinkName,
                                                         SymbolicLinkLength,
                                                         ValueName,
                                                         ValueNameLength)))
            {
                Properties->CompanionHubSymbolicLinkName[0] = UNICODE_NULL;
            }
            else
            {
                size_t CompanionBytes = 0;
                if (NT_SUCCESS(RtlStringCbLengthW(Properties->CompanionHubSymbolicLinkName,
                                                  SymbolicLinkLength,
                                                  &CompanionBytes)) &&
                    CompanionBytes > 0)
                {
                    if (CompanionBytes <= SymbolicLinkLength)
                        Properties->ActualLength += (ULONG)CompanionBytes;
                }
            }
        }
    }

    Irp->IoStatus.Information = sizeof(USB_PORT_CONNECTOR_PROPERTIES);
    Status = STATUS_SUCCESS;
    USBH_CompleteIrp(Irp, Status);

    return Status;
}

NTSTATUS
NTAPI
USBH_IoctlGetTypeCPortInformation(IN PUSBHUB_FDO_EXTENSION HubExtension,
                                  IN PIRP Irp)
{
    PUSB_TYPE_C_PORT_INFORMATION TypeCInfo;
    PIO_STACK_LOCATION IoStack;
    ULONG BufferLength;
    ULONG ConnectionIndex;
    ULONG NumPorts;
    NTSTATUS Status = STATUS_INVALID_PARAMETER;
    PUSBHUB_PORT_DATA PortData;
    ULONG Flags = 0;
    ULONG Attributes;
    ULONG Retimer;

    DPRINT("USBH_IoctlGetTypeCPortInformation ... \n");

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    BufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (BufferLength < sizeof(USB_TYPE_C_PORT_INFORMATION) ||
        !Irp->AssociatedIrp.SystemBuffer)
    {
        USBH_CompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL);
        return STATUS_BUFFER_TOO_SMALL;
    }

    TypeCInfo = Irp->AssociatedIrp.SystemBuffer;
    ConnectionIndex = TypeCInfo->ConnectionIndex;
    RtlZeroMemory(TypeCInfo, BufferLength);
    TypeCInfo->ConnectionIndex = ConnectionIndex;

    if (!HubExtension->HubDescriptor)
    {
        USBH_CompleteIrp(Irp, Status);
        return Status;
    }

    NumPorts = HubExtension->HubDescriptor->bNumberOfPorts;

    if (ConnectionIndex == 0 || ConnectionIndex > NumPorts)
    {
        USBH_CompleteIrp(Irp, Status);
        return Status;
    }

    PortData = HubExtension->PortData + (ConnectionIndex - 1);
    Attributes = PortData->PortAttributes;

    if (!(Attributes & USB_PORTATTR_TYPEC_CONNECTOR))
    {
        USBH_CompleteIrp(Irp, STATUS_NOT_SUPPORTED);
        return STATUS_NOT_SUPPORTED;
    }

    Flags |= USB_TYPE_C_PORT_FLAG_TYPEC_PRESENT;
    if (Attributes & USB_PORTATTR_TYPEC_USB4_CAPABLE)
        Flags |= USB_TYPE_C_PORT_FLAG_USB4_CAPABLE;
    if (Attributes & USB_PORTATTR_TYPEC_TBT3_CAPABLE)
        Flags |= USB_TYPE_C_PORT_FLAG_TBT3_CAPABLE;
    if (Attributes & USB_PORTATTR_TYPEC_PCIE_TUNNELING)
        Flags |= USB_TYPE_C_PORT_FLAG_PCIE_TUNNELING;
    if (Attributes & USB_PORTATTR_TYPEC_DP_ALT_MODE)
        Flags |= USB_TYPE_C_PORT_FLAG_DP_ALT_MODE;

    Retimer = (Attributes & USB_PORTATTR_TYPEC_RETIMER_MASK) >>
              USB_PORTATTR_TYPEC_RETIMER_SHIFT;
    Flags |= (Retimer << USB_TYPE_C_PORT_FLAG_RETIMER_COUNT_SHIFT) &
             USB_TYPE_C_PORT_FLAG_RETIMER_COUNT_MASK;

    TypeCInfo->ActualLength = sizeof(USB_TYPE_C_PORT_INFORMATION);
    TypeCInfo->Flags = Flags;

    Irp->IoStatus.Information = sizeof(USB_TYPE_C_PORT_INFORMATION);
    Status = STATUS_SUCCESS;
    USBH_CompleteIrp(Irp, Status);

    return Status;
}

NTSTATUS
NTAPI
USBH_IoctlGetNodeConnectionInformation(IN PUSBHUB_FDO_EXTENSION HubExtension,
                                       IN PIRP Irp,
                                       IN BOOLEAN IsExt)
{
    PUSBHUB_PORT_DATA PortData;
    ULONG BufferLength;
    PUSB_NODE_CONNECTION_INFORMATION_EX Info;
    ULONG ConnectionIndex;
    ULONG NumPorts;
    NTSTATUS Status;
    PDEVICE_OBJECT DeviceObject;
    PUSBHUB_PORT_PDO_EXTENSION PortExtension;
    PIO_STACK_LOCATION IoStack;

    DPRINT("USBH_IoctlGetNodeConnectionInformation ... \n");

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    BufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (BufferLength < (ULONG)FIELD_OFFSET(USB_NODE_CONNECTION_INFORMATION_EX,
                                           PipeList))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Info = Irp->AssociatedIrp.SystemBuffer;

    ConnectionIndex = Info->ConnectionIndex;
    RtlZeroMemory(Info, BufferLength);
    Info->ConnectionIndex = ConnectionIndex;

    Status = STATUS_INVALID_PARAMETER;

    NumPorts = HubExtension->HubDescriptor->bNumberOfPorts;

    if (NumPorts == 0 ||
        ConnectionIndex == 0 ||
        ConnectionIndex > NumPorts)
    {
        goto Exit;
    }

    PortData = HubExtension->PortData + (ConnectionIndex - 1);
    DeviceObject = PortData->DeviceObject;

    if (!DeviceObject)
    {
        Info->ConnectionStatus = PortData->ConnectionStatus;

        Irp->IoStatus.Information = FIELD_OFFSET(USB_NODE_CONNECTION_INFORMATION_EX,
                                                 PipeList);
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    PortExtension = DeviceObject->DeviceExtension;

    Info->ConnectionStatus = PortData->ConnectionStatus;

    Info->DeviceIsHub = (PortExtension->PortPdoFlags &
                         USBHUB_PDO_FLAG_HUB_DEVICE) ==
                         USBHUB_PDO_FLAG_HUB_DEVICE;

    RtlCopyMemory(&Info->DeviceDescriptor,
                  &PortExtension->DeviceDescriptor,
                  sizeof(USB_DEVICE_DESCRIPTOR));

    if (PortExtension->DeviceHandle)
    {
        Status = USBD_GetDeviceInformationEx(PortExtension,
                                             HubExtension,
                                             Info,
                                             BufferLength,
                                             PortExtension->DeviceHandle);
    }
    else
    {
        Status = STATUS_SUCCESS;
    }

    if (NT_SUCCESS(Status))
    {
        if (!IsExt)
        {
            /* IOCTL_USB_GET_NODE_CONNECTION_INFORMATION request reports
               only low and full speed connections. Info->Speed member
               is Info->LowSpeed in the non-EX version of the structure */

            Info->Speed = (Info->Speed == UsbLowSpeed);
        }

        Irp->IoStatus.Information = sizeof(USB_NODE_CONNECTION_INFORMATION_EX) +
                                    (Info->NumberOfOpenPipes - 1) * sizeof(USB_PIPE_INFO);
        goto Exit;
    }

    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        goto Exit;
    }

    Irp->IoStatus.Information = FIELD_OFFSET(USB_NODE_CONNECTION_INFORMATION_EX,
                                             PipeList);
    Status = STATUS_SUCCESS;

Exit:
    USBH_CompleteIrp(Irp, Status);
    return Status;
}

static
NTSTATUS
USBH_IoctlGetNodeConnectionInformationExV2(
    _In_ PUSBHUB_FDO_EXTENSION HubExtension,
    _In_ PIRP Irp)
{
    PIO_STACK_LOCATION IoStack;
    ULONG BufferLength;
    PUSB_NODE_CONNECTION_INFORMATION_EX_V2 Info;
    ULONG ConnectionIndex;
    ULONG NumPorts;
    PUSBHUB_PORT_DATA PortData;
    PDEVICE_OBJECT DeviceObject;
    PUSBHUB_PORT_PDO_EXTENSION PortExtension;
    USB_PROTOCOLS Protocols;
    USB_NODE_CONNECTION_INFORMATION_EX_V2_FLAGS Flags;
    NTSTATUS Status;

    DPRINT("USBH_IoctlGetNodeConnectionInformationExV2 ... \n");

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    BufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (BufferLength < sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2) ||
        !HubExtension->HubDescriptor ||
        HubExtension->HubDescriptor->bNumberOfPorts == 0)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Info = (PUSB_NODE_CONNECTION_INFORMATION_EX_V2)Irp->AssociatedIrp.SystemBuffer;
    ConnectionIndex = Info->ConnectionIndex;

    RtlZeroMemory(Info, sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2));
    Info->ConnectionIndex = ConnectionIndex;
    Info->Length = sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2);

    Status = STATUS_INVALID_PARAMETER;

    NumPorts = HubExtension->HubDescriptor->bNumberOfPorts;
    if (ConnectionIndex == 0 || ConnectionIndex > NumPorts)
        goto Exit;

    PortData = HubExtension->PortData + (ConnectionIndex - 1);
    DeviceObject = PortData->DeviceObject;

    Protocols.ul = 0;
    Flags.ul = 0;

    /* Assume all hubs support USB 1.1 and 2.0 signalling on downstream ports. */
    Protocols.Usb110 = 1;
    Protocols.Usb200 = 1;

    /* If this is a USB 3.x hub, advertise SuperSpeed support on all ports. */
    if (HubExtension->HubDescriptor->bDescriptorType == USB_30_HUB_DESCRIPTOR_TYPE)
        Protocols.Usb300 = 1;

    if (DeviceObject)
    {
        PortExtension = DeviceObject->DeviceExtension;

        if (PortData->ConnectionStatus == DeviceConnected)
        {
            USB_PORT_STATUS PortStatus = PortData->PortStatus.PortStatus;

            /* Operating at SuperSpeed if we are on a USB 3 hub and the negotiated
             * link speed is SuperSpeed (code 4 in USB_30_PORT_STATUS). */
            if (HubExtension->HubDescriptor->bDescriptorType == USB_30_HUB_DESCRIPTOR_TYPE &&
                PortStatus.Usb30PortStatus.NegotiatedDeviceSpeed == 4)
            {
                Flags.DeviceIsOperatingAtSuperSpeedOrHigher = 1;
            }

            /* SuperSpeed capable if the device advertises USB 3.x in its bcdUSB. */
            if (PortExtension->DeviceDescriptor.bcdUSB >= 0x0300)
            {
                Flags.DeviceIsSuperSpeedCapableOrHigher = 1;
            }
        }
    }

    Info->SupportedUsbProtocols = Protocols;
    Info->Flags = Flags;

    Irp->IoStatus.Information = sizeof(USB_NODE_CONNECTION_INFORMATION_EX_V2);
    Status = STATUS_SUCCESS;

Exit:
    USBH_CompleteIrp(Irp, Status);
    return Status;
}

NTSTATUS
NTAPI
USBH_IoctlGetNodeConnectionDriverKeyName(IN PUSBHUB_FDO_EXTENSION HubExtension,
                                         IN PIRP Irp)
{
    PUSBHUB_PORT_DATA PortData;
    PDEVICE_OBJECT PortDevice;
    ULONG Length;
    ULONG ResultLength;
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    ULONG BufferLength;
    PUSB_NODE_CONNECTION_DRIVERKEY_NAME KeyName;
    PUSBHUB_PORT_PDO_EXTENSION PortExtension;

    DPRINT("USBH_IoctlGetNodeConnectionDriverKeyName ... \n");

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    BufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    if (BufferLength < sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME) ||
        HubExtension->HubDescriptor->bNumberOfPorts == 0)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    KeyName = Irp->AssociatedIrp.SystemBuffer;
    Status = STATUS_INVALID_PARAMETER;

    PortData = &HubExtension->PortData[KeyName->ConnectionIndex - 1];
    PortDevice = PortData->DeviceObject;

    if (!PortDevice)
    {
        goto Exit;
    }

    PortExtension = PortDevice->DeviceExtension;

    if (!(PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_ENUMERATED))
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    ResultLength = BufferLength - sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME);

    Status = IoGetDeviceProperty(PortDevice,
                                 DevicePropertyDriverKeyName,
                                 BufferLength - sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME),
                                 &KeyName->DriverKeyName,
                                 &ResultLength);

    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        Status = STATUS_SUCCESS;
    }

    Length = ResultLength + sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME);
    KeyName->ActualLength = Length;

    if (BufferLength < Length)
    {
        KeyName->DriverKeyName[0] = 0;
        Irp->IoStatus.Information = sizeof(USB_NODE_CONNECTION_DRIVERKEY_NAME);
    }
    else
    {
        Irp->IoStatus.Information = Length;
    }

Exit:
    USBH_CompleteIrp(Irp, Status);
    return Status;
}

NTSTATUS
NTAPI
USBH_IoctlGetDescriptor(IN PUSBHUB_FDO_EXTENSION HubExtension,
                        IN PIRP Irp)
{
    ULONG BufferLength;
    PUSBHUB_PORT_DATA PortData;
    PUSB_DESCRIPTOR_REQUEST UsbRequest;
    PDEVICE_OBJECT PortDevice;
    PUSBHUB_PORT_PDO_EXTENSION PortExtension;
    struct _URB_CONTROL_TRANSFER * Urb;
    NTSTATUS Status;
    ULONG RequestBufferLength;
    PIO_STACK_LOCATION IoStack;
    ULONG NumPorts;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    BufferLength = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

    DPRINT("USBH_IoctlGetDescriptor: BufferLength - %x\n", BufferLength);

    if (BufferLength < sizeof(USB_DESCRIPTOR_REQUEST))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    UsbRequest = Irp->AssociatedIrp.SystemBuffer;
    RequestBufferLength = UsbRequest->SetupPacket.wLength;

    if (RequestBufferLength > BufferLength -
                              FIELD_OFFSET(USB_DESCRIPTOR_REQUEST, Data))
    {
        DPRINT("USBH_IoctlGetDescriptor: RequestBufferLength - %x\n",
               RequestBufferLength);

        Status = STATUS_BUFFER_TOO_SMALL;
        goto Exit;
    }

    Status = STATUS_INVALID_PARAMETER;

    NumPorts = HubExtension->HubDescriptor->bNumberOfPorts;

    if (NumPorts == 0 ||
        UsbRequest->ConnectionIndex == 0 ||
        UsbRequest->ConnectionIndex > NumPorts)
    {
        goto Exit;
    }

    PortData = HubExtension->PortData + (UsbRequest->ConnectionIndex - 1);
    PortDevice = PortData->DeviceObject;

    if (!PortDevice)
    {
        goto Exit;
    }

    PortExtension = PortDevice->DeviceExtension;

    if (UsbRequest->SetupPacket.bmRequest == USB_CONFIGURATION_DESCRIPTOR_TYPE &&
        RequestBufferLength == sizeof(USB_CONFIGURATION_DESCRIPTOR))
    {
        Status = STATUS_SUCCESS;

        RtlCopyMemory(&UsbRequest->Data[0],
                      &PortExtension->ConfigDescriptor,
                      sizeof(USB_CONFIGURATION_DESCRIPTOR));

        Irp->IoStatus.Information = sizeof(USB_DESCRIPTOR_REQUEST) - sizeof(UCHAR) +
                                    sizeof(USB_CONFIGURATION_DESCRIPTOR);
        goto Exit;
    }

    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                USB_HUB_TAG);

    if (!Urb)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    RtlZeroMemory(Urb, sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST));

    Urb->Hdr.Function = URB_FUNCTION_GET_DESCRIPTOR_FROM_DEVICE;
    Urb->Hdr.Length = sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST);

    Urb->TransferBuffer = &UsbRequest->Data[0];
    Urb->TransferBufferLength = RequestBufferLength;
    Urb->TransferBufferMDL = NULL;
    Urb->UrbLink = NULL;

    RtlCopyMemory(Urb->SetupPacket,
                  &UsbRequest->SetupPacket,
                  sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

    Status = USBH_SyncSubmitUrb(PortExtension->Common.SelfDevice,
                                (PURB)Urb);

    Irp->IoStatus.Information = (sizeof(USB_DESCRIPTOR_REQUEST) - sizeof(UCHAR)) +
                                Urb->TransferBufferLength;

    ExFreePoolWithTag(Urb, USB_HUB_TAG);

Exit:
    USBH_CompleteIrp(Irp, Status);
    return Status;
}

NTSTATUS
NTAPI
USBH_DeviceControl(IN PUSBHUB_FDO_EXTENSION HubExtension,
                   IN PIRP Irp)
{
    NTSTATUS Status = STATUS_DEVICE_BUSY;
    PIO_STACK_LOCATION IoStack;
    ULONG ControlCode;
    BOOLEAN IsCheckHubIdle = FALSE;

    DPRINT("USBH_DeviceControl: HubExtension - %p, Irp - %p\n",
           HubExtension,
           Irp);

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    ControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;
    DPRINT("USBH_DeviceControl: ControlCode - %lX\n", ControlCode);

    if ((HubExtension->CurrentPowerState.DeviceState != PowerDeviceD0) &&
        (HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STARTED))
    {
        IsCheckHubIdle = TRUE;
        USBH_HubSetD0(HubExtension);
    }

    switch (ControlCode)
    {
        case IOCTL_USB_GET_HUB_CAPABILITIES:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_HUB_CAPABILITIES\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetHubCapabilities(HubExtension, Irp);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_GET_HUB_INFORMATION_EX:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_HUB_INFORMATION_EX\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetHubInformationEx(HubExtension, Irp);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_HUB_CYCLE_PORT:
        {
            USHORT Port;

            DPRINT("USBH_DeviceControl: IOCTL_USB_HUB_CYCLE_PORT\n");

            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                if (IoStack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ULONG) ||
                    !Irp->AssociatedIrp.SystemBuffer)
                {
                    Status = STATUS_INVALID_PARAMETER;
                }
                else
                {
                    Port = (USHORT)(*(PULONG)Irp->AssociatedIrp.SystemBuffer);
                    DPRINT("USBH_DeviceControl: cycle port %u\n", Port);
                    Status = USBH_ResetDevice(HubExtension, Port, FALSE, TRUE);
                }
            }

            USBH_CompleteIrp(Irp, Status);
            break;
        }

        case IOCTL_USB_GET_NODE_INFORMATION:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_NODE_INFORMATION\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetNodeInformation(HubExtension, Irp);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_NODE_CONNECTION_INFORMATION\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetNodeConnectionInformation(HubExtension,
                                                                Irp,
                                                                FALSE);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetNodeConnectionInformation(HubExtension,
                                                                Irp,
                                                                TRUE);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_NODE_CONNECTION_INFORMATION_EX_V2\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetNodeConnectionInformationExV2(HubExtension,
                                                                    Irp);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_NODE_CONNECTION_ATTRIBUTES\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetNodeConnectionAttributes(HubExtension, Irp);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_PORT_CONNECTOR_PROPERTIES\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetPortConnectorProperties(HubExtension, Irp);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_GET_TYPE_C_PORT_INFORMATION:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_TYPE_C_PORT_INFORMATION\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetTypeCPortInformation(HubExtension, Irp);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_GET_NODE_CONNECTION_NAME:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_NODE_CONNECTION_NAME\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetNodeName(HubExtension, Irp);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_NODE_CONNECTION_DRIVERKEY_NAME\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetNodeConnectionDriverKeyName(HubExtension, Irp);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION:
            DPRINT("USBH_DeviceControl: IOCTL_USB_GET_DESCRIPTOR_FROM_NODE_CONNECTION\n");
            if (!(HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED))
            {
                Status = USBH_IoctlGetDescriptor(HubExtension, Irp);
                break;
            }

            USBH_CompleteIrp(Irp, Status);
            break;

        case IOCTL_KS_PROPERTY:
            DPRINT("USBH_DeviceControl: IOCTL_KS_PROPERTY\n");
            Status = STATUS_INVALID_DEVICE_REQUEST;
            USBH_CompleteIrp(Irp, Status);
            break;

        default:
            DPRINT1("USBH_DeviceControl: Unhandled IOCTL_ - %lX\n", ControlCode);
            Status = USBH_PassIrp(HubExtension->RootHubPdo, Irp);
            break;
    }

    if (IsCheckHubIdle)
    {
        USBH_CheckHubIdle(HubExtension);
    }

    return Status;
}

NTSTATUS
NTAPI
USBH_PdoInternalControl(IN PUSBHUB_PORT_PDO_EXTENSION PortExtension,
                        IN PIRP Irp)
{
    PUSBHUB_FDO_EXTENSION HubExtension;
    NTSTATUS Status = STATUS_NOT_SUPPORTED;
    ULONG ControlCode;
    PIO_STACK_LOCATION IoStack;
    PULONG HubCount;

    DPRINT_IOCTL("USBH_PdoInternalControl: PortExtension - %p, Irp - %p\n",
                 PortExtension,
                 Irp);

    HubExtension = PortExtension->HubExtension;

    if (PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_NOT_CONNECTED)
    {
        Status = STATUS_DEVICE_NOT_CONNECTED;
        goto Exit;
    }

    if (PortExtension->CurrentPowerState.DeviceState != PowerDeviceD0)
    {
        Status = STATUS_DEVICE_POWERED_OFF;
        goto Exit;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    ControlCode = IoStack->Parameters.DeviceIoControl.IoControlCode;

    if (ControlCode == IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO)
    {
        HubExtension = PortExtension->RootHubExtension;
        DPRINT("USBH_PdoInternalControl: HubExtension - %p\n", HubExtension);
    }

    if (!HubExtension)
    {
        Status = STATUS_DEVICE_BUSY;
        goto Exit;
    }

    switch (ControlCode)
    {
        case IOCTL_INTERNAL_USB_SUBMIT_URB:
            return USBH_PdoIoctlSubmitUrb(PortExtension, Irp);

        case IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION:
            DPRINT1("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_SUBMIT_IDLE_NOTIFICATION\n");
            return USBH_PortIdleNotificationRequest(PortExtension, Irp);

        case IOCTL_INTERNAL_USB_GET_PORT_STATUS:
            DPRINT("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_GET_PORT_STATUS\n");
            return USBH_PdoIoctlGetPortStatus(PortExtension, Irp);

        case IOCTL_INTERNAL_USB_RESET_PORT:
            DPRINT1("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_RESET_PORT\n");
            return USBH_PdoIoctlResetPort(PortExtension, Irp);

        case IOCTL_INTERNAL_USB_ENABLE_PORT:
        {
            USHORT Port;

            DPRINT("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_ENABLE_PORT\n");

            if (HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED)
            {
                Status = STATUS_DEVICE_NOT_CONNECTED;
                break;
            }

            Port = PortExtension->PortNumber;
            if (Port == 0)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Status = USBH_SyncPowerOnPort(HubExtension,
                                          Port,
                                          TRUE);
            break;
        }

        case IOCTL_INTERNAL_USB_CYCLE_PORT:
        {
            USHORT Port;

            DPRINT("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_CYCLE_PORT\n");

            if (PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_PORT_RESSETING)
            {
                Status = STATUS_UNSUCCESSFUL;
                break;
            }

            if (HubExtension->HubFlags & USBHUB_FDO_FLAG_DEVICE_STOPPED)
            {
                Status = STATUS_DEVICE_NOT_CONNECTED;
                break;
            }

            Port = PortExtension->PortNumber;
            if (Port == 0)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            Status = USBH_ResetDevice(HubExtension,
                                      Port,
                                      FALSE,
                                      TRUE);
            break;
        }

        case IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE:
            DPRINT("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_GET_DEVICE_HANDLE\n");
            *(PVOID *)IoStack->Parameters.Others.Argument1 = PortExtension->DeviceHandle;
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_INTERNAL_USB_GET_HUB_COUNT:
            DPRINT("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_GET_HUB_COUNT. PortPdoFlags - %lX\n",
                   PortExtension->PortPdoFlags);

            if (!(PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_HUB_DEVICE))
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            HubCount = IoStack->Parameters.Others.Argument1;

            ++*HubCount;

            Status = USBH_SyncGetHubCount(HubExtension->LowerDevice,
                                          HubCount);

            DPRINT("USBH_PdoInternalControl: *HubCount - %x\n", *HubCount);
            break;

        case IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO:
            DPRINT("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_GET_ROOTHUB_PDO. PortPdoFlags - %lX\n",
                   PortExtension->PortPdoFlags);

            if (!(PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_HUB_DEVICE))
            {
                Status = STATUS_SUCCESS;
                *(PVOID *)IoStack->Parameters.Others.Argument1 = NULL;
                break;
            }

            ASSERT(HubExtension->RootHubPdo);
            return USBH_PassIrp(HubExtension->RootHubPdo, Irp);

        case IOCTL_INTERNAL_USB_GET_HUB_NAME:
        {
            PUSB_HUB_NAME HubName;
            ULONG Length;
            PWCHAR Buffer;
            PWCHAR BufferEnd;
            ULONG BufferLength;
            ULONG LengthSkip = 0;
            ULONG NameLength;
            ULONG Required;

            DPRINT("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_GET_HUB_NAME\n");

            HubName = (PUSB_HUB_NAME)Irp->AssociatedIrp.SystemBuffer;
            Length = IoStack->Parameters.DeviceIoControl.OutputBufferLength;

            if (!HubName || Length < sizeof(USB_HUB_NAME))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                Irp->IoStatus.Information = sizeof(USB_HUB_NAME);
                break;
            }

            RtlZeroMemory(HubName, Length);
            HubName->ActualLength = sizeof(USB_HUB_NAME);

            if (!(PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_HUB_DEVICE) ||
                !(PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_DEVICE_STARTED) ||
                !(PortExtension->PortPdoFlags & USBHUB_PDO_FLAG_REG_DEV_INTERFACE) ||
                PortExtension->SymbolicLinkName.Buffer == NULL)
            {
                /* Not a hub PDO or no interface registered: return empty name */
                Irp->IoStatus.Information = sizeof(USB_HUB_NAME);
                Status = STATUS_SUCCESS;
                break;
            }

            Buffer = PortExtension->SymbolicLinkName.Buffer;
            BufferLength = PortExtension->SymbolicLinkName.Length;

            ASSERT(Buffer[BufferLength / sizeof(WCHAR)] == UNICODE_NULL);

            if (*Buffer == L'\\')
            {
                BufferEnd = wcschr(Buffer + 1, L'\\');

                if (BufferEnd != NULL)
                {
                    LengthSkip =
                        (ULONG)((BufferEnd + 1 - Buffer) * sizeof(WCHAR));
                }
                else
                {
                    LengthSkip = BufferLength;
                }
            }

            if (LengthSkip >= BufferLength)
            {
                Irp->IoStatus.Information = sizeof(USB_HUB_NAME);
                Status = STATUS_SUCCESS;
                break;
            }

            NameLength = BufferLength - LengthSkip;
            Required = sizeof(USB_HUB_NAME) + NameLength;

            HubName->ActualLength = Required;

            if (Length < Required)
            {
                /* Caller needs to resubmit with a larger buffer */
                Irp->IoStatus.Information = sizeof(USB_HUB_NAME);
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlCopyMemory(&HubName->HubName[0],
                          &Buffer[LengthSkip / sizeof(WCHAR)],
                          NameLength);

            /* Ensure trailing NULL */
            HubName->HubName[NameLength / sizeof(WCHAR)] = UNICODE_NULL;

            Irp->IoStatus.Information = Required;
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_GET_HCD_DRIVERKEY_NAME:
            DPRINT("USBH_PdoInternalControl: IOCTL_GET_HCD_DRIVERKEY_NAME (unsupported on PDO)\n");
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;

        case IOCTL_INTERNAL_USB_GET_BUS_INFO:
        {
            PUSB_BUS_NOTIFICATION BusNotification;
            PUSB_BUS_INFORMATION_LEVEL_1 BusInfo;
            ULONG BusInfoLength;
            ULONG BusInfoActualLength;
            NTSTATUS QueryStatus;

            DPRINT("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_GET_BUS_INFO\n");

            BusNotification =
                (PUSB_BUS_NOTIFICATION)IoStack->Parameters.Others.Argument1;

            if (!BusNotification)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            if (!HubExtension->BusInterfaceUSBDI.QueryBusInformation)
            {
                Status = STATUS_NOT_IMPLEMENTED;
                break;
            }

            BusInfo = NULL;
            BusInfoLength = 0;
            BusInfoActualLength = 0;

            /* First query to obtain required buffer length (Level 1) */
            QueryStatus =
                HubExtension->BusInterfaceUSBDI.QueryBusInformation(
                    HubExtension->BusInterfaceUSBDI.BusContext,
                    1,
                    NULL,
                    &BusInfoLength,
                    &BusInfoActualLength);

            if (QueryStatus != STATUS_BUFFER_TOO_SMALL &&
                !NT_SUCCESS(QueryStatus))
            {
                Status = QueryStatus;
                break;
            }

            if (BusInfoActualLength < sizeof(USB_BUS_INFORMATION_LEVEL_1))
            {
                BusInfoActualLength = sizeof(USB_BUS_INFORMATION_LEVEL_1);
            }

            BusInfo = ExAllocatePoolWithTag(NonPagedPool,
                                            BusInfoActualLength,
                                            USB_HUB_TAG);

            if (!BusInfo)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            BusInfoLength = BusInfoActualLength;

            QueryStatus =
                HubExtension->BusInterfaceUSBDI.QueryBusInformation(
                    HubExtension->BusInterfaceUSBDI.BusContext,
                    1,
                    BusInfo,
                    &BusInfoLength,
                    &BusInfoActualLength);

            if (!NT_SUCCESS(QueryStatus))
            {
                ExFreePoolWithTag(BusInfo, USB_HUB_TAG);
                Status = QueryStatus;
                break;
            }

            BusNotification->TotalBandwidth = BusInfo->TotalBandwidth;
            BusNotification->ConsumedBandwidth = BusInfo->ConsumedBandwidth;
            BusNotification->ControllerNameLength =
                BusInfo->ControllerNameLength;

            ExFreePoolWithTag(BusInfo, USB_HUB_TAG);

            Irp->IoStatus.Information = sizeof(USB_BUS_NOTIFICATION);
            Status = STATUS_SUCCESS;
            break;
        }

        case IOCTL_INTERNAL_USB_GET_PARENT_HUB_INFO:
            DPRINT("USBH_PdoInternalControl: IOCTL_INTERNAL_USB_GET_PARENT_HUB_INFO (not implemented)\n");
            Status = STATUS_NOT_IMPLEMENTED;
            break;

        default:
            DPRINT1("USBH_PdoInternalControl: unhandled IOCTL_ - %lX\n", ControlCode);
            break;
    }

Exit:
    USBH_CompleteIrp(Irp, Status);
    return Status;
}
