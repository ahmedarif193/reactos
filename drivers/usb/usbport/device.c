/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     USBPort device functions
 * COPYRIGHT:   Copyright 2017 Vadim Galyant <vgal@rambler.ru>
 */

#include "usbport.h"

#define NDEBUG
#include <debug.h>

#define USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR_TYPE 0x30

typedef struct _USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR {
    UCHAR bLength;
    UCHAR bDescriptorType;
    UCHAR bMaxBurst;
    UCHAR bmAttributes;
    USHORT wBytesPerInterval;
} USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR, *PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR;

static
VOID
USBPORT_FetchBosDescriptor(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                           IN PDEVICE_OBJECT FdoDevice);

static
VOID
USBPORT_ComputeLpmPolicy(IN PUSBPORT_DEVICE_HANDLE DeviceHandle);

NTSTATUS
NTAPI
USBPORT_SendSetupPacket(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                        IN PDEVICE_OBJECT FdoDevice,
                        IN PUSB_DEFAULT_PIPE_SETUP_PACKET SetupPacket,
                        IN PVOID Buffer,
                        IN ULONG Length,
                        IN OUT PULONG TransferedLen,
                        IN OUT PUSBD_STATUS pUSBDStatus)
{
    PURB Urb;
    PMDL Mdl;
    USBD_STATUS USBDStatus = USBD_STATUS_SUCCESS;
    KEVENT Event;
    NTSTATUS Status;

    DPRINT("USBPORT_SendSetupPacket: DeviceHandle - %p, FdoDevice - %p, SetupPacket - %p, Buffer - %p, Length - %x, TransferedLen - %x, pUSBDStatus - %x\n",
           DeviceHandle,
           FdoDevice,
           SetupPacket,
           Buffer,
           Length,
           TransferedLen,
           pUSBDStatus);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Urb = ExAllocatePoolWithTag(NonPagedPool,
                                sizeof(struct _URB_CONTROL_TRANSFER),
                                USB_PORT_TAG);

    if (Urb)
    {
        InterlockedIncrement(&DeviceHandle->DeviceHandleLock);

        RtlZeroMemory(Urb, sizeof(struct _URB_CONTROL_TRANSFER));

        RtlCopyMemory(Urb->UrbControlTransfer.SetupPacket,
                      SetupPacket,
                      sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

        Urb->UrbHeader.Length = sizeof(struct _URB_CONTROL_TRANSFER);
        Urb->UrbHeader.Function = URB_FUNCTION_CONTROL_TRANSFER;
        Urb->UrbHeader.UsbdDeviceHandle = DeviceHandle;
        Urb->UrbHeader.UsbdFlags = 0;

        Urb->UrbControlTransfer.PipeHandle = &DeviceHandle->PipeHandle;
        Urb->UrbControlTransfer.TransferBufferLength = Length;
        Urb->UrbControlTransfer.TransferBuffer = Buffer;
        Urb->UrbControlTransfer.TransferBufferMDL = NULL;

        Urb->UrbControlTransfer.TransferFlags = USBD_SHORT_TRANSFER_OK |
                                                USBD_TRANSFER_DIRECTION;

        if (SetupPacket->bmRequestType.Dir != BMREQUEST_DEVICE_TO_HOST)
        {
            Urb->UrbControlTransfer.TransferFlags &= ~USBD_TRANSFER_DIRECTION_IN;
        }

        Status = STATUS_SUCCESS;

        if (Length)
        {
            Mdl = IoAllocateMdl(Buffer, Length, FALSE, FALSE, NULL);

            Urb->UrbControlTransfer.TransferBufferMDL = Mdl;

            if (Mdl)
            {
                Urb->UrbHeader.UsbdFlags |= USBD_FLAG_ALLOCATED_MDL;
                MmBuildMdlForNonPagedPool(Mdl);

                KeFlushIoBuffers(Mdl,
                                 (SetupPacket->bmRequestType.Dir == BMREQUEST_DEVICE_TO_HOST),
                                 TRUE);
            }
            else
            {
                USBDStatus = USBD_STATUS_INSUFFICIENT_RESOURCES;
                Status = USBPORT_USBDStatusToNtStatus(NULL,
                                                      USBDStatus);
            }
        }

        if (NT_SUCCESS(Status))
        {
            USBDStatus = USBPORT_AllocateTransfer(FdoDevice,
                                                  Urb,
                                                  NULL,
                                                  NULL,
                                                  &Event);

            if (USBD_SUCCESS(USBDStatus))
            {
                InterlockedIncrement(&DeviceHandle->DeviceHandleLock);

                USBPORT_QueueTransferUrb(Urb);

                KeWaitForSingleObject(&Event,
                                      Suspended,
                                      KernelMode,
                                      FALSE,
                                      NULL);

                USBDStatus = Urb->UrbHeader.Status;
            }

            Status = USBPORT_USBDStatusToNtStatus(Urb, USBDStatus);

            if (TransferedLen)
                *TransferedLen = Urb->UrbControlTransfer.TransferBufferLength;

            if (pUSBDStatus)
                *pUSBDStatus = USBDStatus;

            /* No additional cache maintenance required after completion */
        }

        InterlockedDecrement(&DeviceHandle->DeviceHandleLock);
        ExFreePoolWithTag(Urb, USB_PORT_TAG);
    }
    else
    {
        USBDStatus = USBD_STATUS_INSUFFICIENT_RESOURCES;

        if (pUSBDStatus)
            *pUSBDStatus = USBDStatus;

        Status = USBPORT_USBDStatusToNtStatus(NULL,
                                              USBDStatus);
    }

    DPRINT("USBPORT_SendSetupPacket: Status - %x\n", Status);
    return Status;
}

static
BOOLEAN
NTAPI
USBPORT_QueryCurrentConfiguration(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                                  IN PDEVICE_OBJECT FdoDevice,
                                  OUT PUCHAR CurrentConfiguration,
                                  OUT PUSBD_STATUS QueryUsbdStatus)
{
    PVOID QueryBuffer;
    ULONG TransferedLen = 0;
    USB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
    NTSTATUS Status;
    USBD_STATUS USBDStatus = USBD_STATUS_SUCCESS;

    if (CurrentConfiguration)
        *CurrentConfiguration = 0;

    if (QueryUsbdStatus)
        *QueryUsbdStatus = USBD_STATUS_SUCCESS;

    if (!DeviceHandle || !FdoDevice || !CurrentConfiguration)
        return FALSE;

    QueryBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(UCHAR),
                                        USB_PORT_TAG);
    if (!QueryBuffer)
    {
        if (QueryUsbdStatus)
            *QueryUsbdStatus = USBD_STATUS_INSUFFICIENT_RESOURCES;

        return FALSE;
    }

    *(PUCHAR)QueryBuffer = 0;
    RtlZeroMemory(&SetupPacket, sizeof(SetupPacket));

    SetupPacket.bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
    SetupPacket.bmRequestType.Type = BMREQUEST_STANDARD;
    SetupPacket.bmRequestType.Recipient = BMREQUEST_TO_DEVICE;
    SetupPacket.bRequest = USB_REQUEST_GET_CONFIGURATION;
    SetupPacket.wValue.W = 0;
    SetupPacket.wIndex.W = 0;
    SetupPacket.wLength = sizeof(UCHAR);

    Status = USBPORT_SendSetupPacket(DeviceHandle,
                                     FdoDevice,
                                     &SetupPacket,
                                     QueryBuffer,
                                     sizeof(UCHAR),
                                     &TransferedLen,
                                     &USBDStatus);

    if (QueryUsbdStatus)
        *QueryUsbdStatus = USBDStatus;

    if (NT_SUCCESS(Status) &&
        USBD_SUCCESS(USBDStatus) &&
        TransferedLen >= sizeof(UCHAR))
    {
        *CurrentConfiguration = *(PUCHAR)QueryBuffer;
        ExFreePoolWithTag(QueryBuffer, USB_PORT_TAG);
        return TRUE;
    }

    ExFreePoolWithTag(QueryBuffer, USB_PORT_TAG);
    return FALSE;
}

static
BOOLEAN
NTAPI
USBPORT_QueryCurrentInterface(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                              IN PDEVICE_OBJECT FdoDevice,
                              IN UCHAR InterfaceNumber,
                              OUT PUCHAR CurrentAlternate,
                              OUT PUSBD_STATUS QueryUsbdStatus)
{
    PVOID QueryBuffer;
    ULONG TransferedLen = 0;
    USB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
    NTSTATUS Status;
    USBD_STATUS USBDStatus = USBD_STATUS_SUCCESS;

    if (CurrentAlternate)
        *CurrentAlternate = 0;

    if (QueryUsbdStatus)
        *QueryUsbdStatus = USBD_STATUS_SUCCESS;

    if (!DeviceHandle || !FdoDevice || !CurrentAlternate)
        return FALSE;

    QueryBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(UCHAR),
                                        USB_PORT_TAG);
    if (!QueryBuffer)
    {
        if (QueryUsbdStatus)
            *QueryUsbdStatus = USBD_STATUS_INSUFFICIENT_RESOURCES;

        return FALSE;
    }

    *(PUCHAR)QueryBuffer = 0;
    RtlZeroMemory(&SetupPacket, sizeof(SetupPacket));

    SetupPacket.bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
    SetupPacket.bmRequestType.Type = BMREQUEST_STANDARD;
    SetupPacket.bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;
    SetupPacket.bRequest = USB_REQUEST_GET_INTERFACE;
    SetupPacket.wValue.W = 0;
    SetupPacket.wIndex.W = InterfaceNumber;
    SetupPacket.wLength = sizeof(UCHAR);

    Status = USBPORT_SendSetupPacket(DeviceHandle,
                                     FdoDevice,
                                     &SetupPacket,
                                     QueryBuffer,
                                     sizeof(UCHAR),
                                     &TransferedLen,
                                     &USBDStatus);

    if (QueryUsbdStatus)
        *QueryUsbdStatus = USBDStatus;

    if (NT_SUCCESS(Status) &&
        USBD_SUCCESS(USBDStatus) &&
        TransferedLen >= sizeof(UCHAR))
    {
        *CurrentAlternate = *(PUCHAR)QueryBuffer;
        ExFreePoolWithTag(QueryBuffer, USB_PORT_TAG);
        return TRUE;
    }

    ExFreePoolWithTag(QueryBuffer, USB_PORT_TAG);
    return FALSE;
}

ULONG
NTAPI
USBPORT_GetInterfaceLength(IN PUSB_INTERFACE_DESCRIPTOR iDescriptor,
                           IN ULONG_PTR EndDescriptors)
{
    SIZE_T Length;
    PUSB_ENDPOINT_DESCRIPTOR Descriptor;
    ULONG ix;

    DPRINT("USBPORT_GetInterfaceLength ... \n");

    Length = iDescriptor->bLength;
    Descriptor = (PUSB_ENDPOINT_DESCRIPTOR)((ULONG_PTR)iDescriptor + Length);

    if (iDescriptor->bNumEndpoints)
    {
        for (ix = 0; ix < iDescriptor->bNumEndpoints; ix++)
        {
            while ((Descriptor->bDescriptorType != USB_ENDPOINT_DESCRIPTOR_TYPE) &&
                   (Descriptor->bLength > 0))
            {
                Length += Descriptor->bLength;
                Descriptor = (PUSB_ENDPOINT_DESCRIPTOR)((ULONG_PTR)Descriptor +
                                                        Descriptor->bLength);
            }

            Length += Descriptor->bLength;
            Descriptor = (PUSB_ENDPOINT_DESCRIPTOR)((ULONG_PTR)Descriptor +
                                                    Descriptor->bLength);

            /*
             * Skip over any SuperSpeed Endpoint Companion descriptors
             * or other class/vendor-specific descriptors that directly
             * follow this endpoint. They increase the interface length
             * but do not change the endpoint count.
             *
             * USB 3.0 companion descriptors use bDescriptorType 0x30.
             * We treat them as opaque and ignore their contents here.
             */
            while ((ULONG_PTR)Descriptor < EndDescriptors &&
                   Descriptor->bDescriptorType != USB_INTERFACE_DESCRIPTOR_TYPE &&
                   Descriptor->bDescriptorType != USB_ENDPOINT_DESCRIPTOR_TYPE &&
                   Descriptor->bLength > 0)
            {
                Length += Descriptor->bLength;
                Descriptor = (PUSB_ENDPOINT_DESCRIPTOR)((ULONG_PTR)Descriptor +
                                                        Descriptor->bLength);
            }
        }
    }

    while (((ULONG_PTR)Descriptor < EndDescriptors) &&
           (Descriptor->bDescriptorType != USB_INTERFACE_DESCRIPTOR_TYPE) &&
           (Descriptor->bLength > 0))
    {
        Length += Descriptor->bLength;
        Descriptor = (PUSB_ENDPOINT_DESCRIPTOR)((ULONG_PTR)Descriptor +
                                                Descriptor->bLength);
    }

    return Length;
}

PUSB_INTERFACE_DESCRIPTOR
NTAPI
USBPORT_ParseConfigurationDescriptor(IN PUSB_CONFIGURATION_DESCRIPTOR ConfigDescriptor,
                                     IN UCHAR InterfaceNumber,
                                     IN UCHAR Alternate,
                                     OUT PBOOLEAN HasAlternates)
{
    PUSB_CONFIGURATION_DESCRIPTOR TmpDescriptor;
    PUSB_INTERFACE_DESCRIPTOR iDescriptor;
    PUSB_INTERFACE_DESCRIPTOR OutDescriptor = NULL;
    ULONG_PTR Descriptor = (ULONG_PTR)ConfigDescriptor;
    ULONG_PTR EndDescriptors;
    ULONG ix;

    DPRINT("USBPORT_ParseConfigurationDescriptor ... \n");

    if (HasAlternates)
        *HasAlternates = FALSE;

    for (TmpDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)((ULONG_PTR)ConfigDescriptor + ConfigDescriptor->bLength);
         TmpDescriptor->bDescriptorType != USB_INTERFACE_DESCRIPTOR_TYPE && TmpDescriptor->bLength > 0;
         TmpDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)((ULONG_PTR)TmpDescriptor + TmpDescriptor->bLength))
    ;

    iDescriptor = (PUSB_INTERFACE_DESCRIPTOR)TmpDescriptor;

    EndDescriptors = (ULONG_PTR)ConfigDescriptor +
                                ConfigDescriptor->wTotalLength;

    while ((Descriptor < EndDescriptors) &&
           (iDescriptor->bInterfaceNumber != InterfaceNumber))
    {
        Descriptor = (ULONG_PTR)iDescriptor +
                     USBPORT_GetInterfaceLength(iDescriptor, EndDescriptors);

        iDescriptor = (PUSB_INTERFACE_DESCRIPTOR)Descriptor;
    }

    ix = 0;

    while (Descriptor < EndDescriptors &&
           iDescriptor->bInterfaceNumber == InterfaceNumber)
    {
        if (iDescriptor->bAlternateSetting == Alternate)
            OutDescriptor = iDescriptor;

        Descriptor = (ULONG_PTR)iDescriptor +
                     USBPORT_GetInterfaceLength(iDescriptor, EndDescriptors);

        iDescriptor = (PUSB_INTERFACE_DESCRIPTOR)Descriptor;

        ++ix;
    }

    if ((ix > 1) && HasAlternates)
        *HasAlternates = TRUE;

    return OutDescriptor;
}

USBD_STATUS
NTAPI
USBPORT_OpenInterface(IN PURB Urb,
                      IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                      IN PDEVICE_OBJECT FdoDevice,
                      IN PUSBPORT_CONFIGURATION_HANDLE ConfigHandle,
                      IN PUSBD_INTERFACE_INFORMATION InterfaceInfo,
                      IN OUT PUSBPORT_INTERFACE_HANDLE *iHandle,
                      IN BOOLEAN SendSetInterface,
                      IN BOOLEAN ResetPipesAfterOpen)
{
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    PUSBPORT_INTERFACE_HANDLE InterfaceHandle = NULL;
    PUSBPORT_PIPE_HANDLE PipeHandle;
    PUSB_ENDPOINT_DESCRIPTOR Descriptor;
    PUSBD_PIPE_INFORMATION PipeInfo;
    BOOLEAN HasAlternates;
    ULONG NumEndpoints;
    SIZE_T Length;
    USB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
    SIZE_T HandleLength;
    BOOLEAN IsAllocated = FALSE;
    USHORT MaxPacketSize;
    USHORT wMaxPacketSize;
    ULONG ix;
    USBD_STATUS USBDStatus = USBD_STATUS_SUCCESS;
    NTSTATUS Status;
    ULONG OpenedPipes = 0;

    DPRINT("USBPORT_OpenInterface: ...\n");

    InterfaceDescriptor = USBPORT_ParseConfigurationDescriptor(ConfigHandle->ConfigurationDescriptor,
                                                               InterfaceInfo->InterfaceNumber,
                                                               InterfaceInfo->AlternateSetting,
                                                               &HasAlternates);

    if (!InterfaceDescriptor)
    {
        return USBD_STATUS_INTERFACE_NOT_FOUND;
    }

    NumEndpoints = InterfaceDescriptor->bNumEndpoints;

    Length = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes) +
             NumEndpoints * sizeof(USBD_PIPE_INFORMATION);

    if (HasAlternates && SendSetInterface)
    {
        UCHAR CurrentAlternate;
        USBD_STATUS QueryUsbdStatus;

        if (USBPORT_QueryCurrentInterface(DeviceHandle,
                                          FdoDevice,
                                          InterfaceInfo->InterfaceNumber,
                                          &CurrentAlternate,
                                          &QueryUsbdStatus))
        {
            if (CurrentAlternate == InterfaceInfo->AlternateSetting)
            {
                goto SkipSetInterface;
            }
        }

        RtlZeroMemory(&SetupPacket, sizeof(SetupPacket));

        SetupPacket.bmRequestType.Dir = BMREQUEST_HOST_TO_DEVICE;
        SetupPacket.bmRequestType.Type = BMREQUEST_STANDARD;
        SetupPacket.bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;
        SetupPacket.bRequest = USB_REQUEST_SET_INTERFACE;
        SetupPacket.wValue.W = InterfaceInfo->AlternateSetting;
        SetupPacket.wIndex.W = InterfaceInfo->InterfaceNumber;
        SetupPacket.wLength = 0;

        USBPORT_SendSetupPacket(DeviceHandle,
                                FdoDevice,
                                &SetupPacket,
                                NULL,
                                0,
                                NULL,
                                &USBDStatus);

        if (USBD_STATUS(USBDStatus) == USBD_STATUS(USBD_STATUS_STALL_PID) &&
            USBPORT_QueryCurrentInterface(DeviceHandle,
                                          FdoDevice,
                                          InterfaceInfo->InterfaceNumber,
                                          &CurrentAlternate,
                                          &QueryUsbdStatus) &&
            CurrentAlternate == InterfaceInfo->AlternateSetting)
        {
            USBDStatus = USBD_STATUS_SUCCESS;
        }

        if (!USBD_SUCCESS(USBDStatus))
        {
            goto Exit;
        }
    }

SkipSetInterface:

    if (*iHandle)
    {
        InterfaceHandle = *iHandle;
    }
    else
    {
        HandleLength = FIELD_OFFSET(USBPORT_INTERFACE_HANDLE, PipeHandle) +
                       NumEndpoints * sizeof(USBPORT_PIPE_HANDLE);

        InterfaceHandle = ExAllocatePoolWithTag(NonPagedPool,
                                                HandleLength,
                                                USB_PORT_TAG);

        if (!InterfaceHandle)
        {
            USBDStatus = USBD_STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        RtlZeroMemory(InterfaceHandle, HandleLength);

        for (ix = 0; ix < NumEndpoints; ++ix)
        {
            PipeHandle = &InterfaceHandle->PipeHandle[ix];

            PipeHandle->Flags = PIPE_HANDLE_FLAG_CLOSED;
            PipeHandle->Endpoint = NULL;
        }

        IsAllocated = TRUE;
    }

    InterfaceHandle->AlternateSetting = InterfaceInfo->AlternateSetting;

    RtlCopyMemory(&InterfaceHandle->InterfaceDescriptor,
                  InterfaceDescriptor,
                  sizeof(USB_INTERFACE_DESCRIPTOR));

    InterfaceInfo->Class = InterfaceDescriptor->bInterfaceClass;
    InterfaceInfo->SubClass = InterfaceDescriptor->bInterfaceSubClass;
    InterfaceInfo->Protocol = InterfaceDescriptor->bInterfaceProtocol;
    InterfaceInfo->Reserved = 0;
    InterfaceInfo->NumberOfPipes = InterfaceDescriptor->bNumEndpoints;

    Descriptor = (PUSB_ENDPOINT_DESCRIPTOR)((ULONG_PTR)InterfaceDescriptor +
                                            InterfaceDescriptor->bLength);

    for (ix = 0; ix < NumEndpoints; ++ix)
    {
        PipeHandle = &InterfaceHandle->PipeHandle[ix];

        while (Descriptor->bDescriptorType != USB_ENDPOINT_DESCRIPTOR_TYPE)
        {
            if (Descriptor->bLength == 0)
            {
                break;
            }
            else
            {
                Descriptor = (PUSB_ENDPOINT_DESCRIPTOR)((ULONG_PTR)Descriptor +
                                                        Descriptor->bLength);
            }
        }

        if (InterfaceInfo->Pipes[ix].PipeFlags & USBD_PF_CHANGE_MAX_PACKET)
        {
            Descriptor->wMaxPacketSize = InterfaceInfo->Pipes[ix].MaximumPacketSize;
        }

        RtlCopyMemory(&PipeHandle->EndpointDescriptor,
                      Descriptor,
                      sizeof(USB_ENDPOINT_DESCRIPTOR));

        PipeHandle->SsCompanionValid = FALSE;
        PipeHandle->SsCompanionMaxBurst = 0;
        PipeHandle->SsCompanionAttributes = 0;
        PipeHandle->SsCompanionBytesPerInterval = 0;

        if (DeviceHandle->DeviceSpeed == UsbSuperSpeed)
        {
            PUCHAR ConfigStart = (PUCHAR)ConfigHandle->ConfigurationDescriptor;
            PUCHAR ConfigEnd = ConfigStart + ConfigHandle->ConfigurationDescriptor->wTotalLength;
            PUSB_COMMON_DESCRIPTOR Common = (PUSB_COMMON_DESCRIPTOR)((PUCHAR)Descriptor +
                                                                     Descriptor->bLength);

            if (((PUCHAR)Common + sizeof(USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR)) <= ConfigEnd &&
                Common->bDescriptorType == USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR_TYPE &&
                Common->bLength >= sizeof(USB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR))
            {
                PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR SsCompanion =
                    (PUSB_SUPERSPEED_ENDPOINT_COMPANION_DESCRIPTOR)Common;

                PipeHandle->SsCompanionValid = TRUE;
                PipeHandle->SsCompanionMaxBurst = SsCompanion->bMaxBurst;
                PipeHandle->SsCompanionAttributes = SsCompanion->bmAttributes;
                PipeHandle->SsCompanionBytesPerInterval = SsCompanion->wBytesPerInterval;
            }
        }

        PipeHandle->Flags = PIPE_HANDLE_FLAG_CLOSED;
        PipeHandle->PipeFlags = InterfaceInfo->Pipes[ix].PipeFlags;
        PipeHandle->Endpoint = NULL;

        wMaxPacketSize = Descriptor->wMaxPacketSize;

        /* USB 2.0 Specification, 5.9 High-Speed, High Bandwidth Endpoints */
        MaxPacketSize = (wMaxPacketSize & 0x7FF) * (((wMaxPacketSize >> 11) & 3) + 1);

        InterfaceInfo->Pipes[ix].EndpointAddress = Descriptor->bEndpointAddress;
        InterfaceInfo->Pipes[ix].PipeType = Descriptor->bmAttributes & USB_ENDPOINT_TYPE_MASK;
        InterfaceInfo->Pipes[ix].MaximumPacketSize = MaxPacketSize;
        InterfaceInfo->Pipes[ix].PipeHandle = (USBD_PIPE_HANDLE)-1;
        InterfaceInfo->Pipes[ix].Interval = Descriptor->bInterval;

        Descriptor = (PUSB_ENDPOINT_DESCRIPTOR)((ULONG_PTR)Descriptor +
                                                Descriptor->bLength);

        /*
         * Skip over any SuperSpeed Endpoint Companion descriptors and
         * other non-endpoint, non-interface descriptors that immediately
         * follow this endpoint. They do not correspond to additional
         * USBD pipes.
         */
        while ((ULONG_PTR)Descriptor < (ULONG_PTR)ConfigHandle->ConfigurationDescriptor +
                                      ConfigHandle->ConfigurationDescriptor->wTotalLength &&
               Descriptor->bDescriptorType != USB_INTERFACE_DESCRIPTOR_TYPE &&
               Descriptor->bDescriptorType != USB_ENDPOINT_DESCRIPTOR_TYPE &&
               Descriptor->bLength > 0)
        {
            Descriptor = (PUSB_ENDPOINT_DESCRIPTOR)((ULONG_PTR)Descriptor +
                                                    Descriptor->bLength);
        }
    }

    if (USBD_SUCCESS(USBDStatus))
    {
        DPRINT("USBPORT_OpenInterface: opening %lu endpoints\n", NumEndpoints);

        for (ix = 0; ix < NumEndpoints; ++ix)
        {
            PipeInfo = &InterfaceInfo->Pipes[ix];
            PipeHandle = &InterfaceHandle->PipeHandle[ix];

            Status = USBPORT_OpenPipe(FdoDevice,
                                      DeviceHandle,
                                      PipeHandle,
                                      &USBDStatus);

            if (!NT_SUCCESS(Status))
            {
                if (USBD_SUCCESS(USBDStatus))
                    USBDStatus = USBD_STATUS_INSUFFICIENT_RESOURCES;

                break;
            }

            if (ResetPipesAfterOpen &&
                !(PipeHandle->Flags & PIPE_HANDLE_FLAG_NULL_PACKET_SIZE))
            {
                struct _URB_PIPE_REQUEST ResetUrb;

                RtlZeroMemory(&ResetUrb, sizeof(ResetUrb));
                ResetUrb.Hdr.Length = sizeof(ResetUrb);
                ResetUrb.Hdr.Function = URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL;
                ResetUrb.Hdr.UsbdDeviceHandle = DeviceHandle;
                ResetUrb.PipeHandle = PipeHandle;

                Status = USBPORT_SyncResetPipeAndClearStall(FdoDevice,
                                                            NULL,
                                                            (PURB)&ResetUrb);
                if (!NT_SUCCESS(Status))
                {
                    USBDStatus = ResetUrb.Hdr.Status;
                    if (USBD_SUCCESS(USBDStatus))
                        USBDStatus = USBD_STATUS_INVALID_PIPE_HANDLE;

                    break;
                }
            }

            PipeInfo->PipeHandle = PipeHandle;
            OpenedPipes++;
        }

        if (NumEndpoints)
        {
            USBPORT_USBDStatusToNtStatus(Urb, USBDStatus);
        }
    }

Exit:

    if (USBD_SUCCESS(USBDStatus))
    {
        InterfaceInfo->InterfaceHandle = InterfaceHandle;
        *iHandle = InterfaceHandle;
        InterfaceInfo->Length = Length;
    }
    else
    {
        if (InterfaceHandle)
        {
            while (OpenedPipes > 0)
            {
                OpenedPipes--;
                USBPORT_ClosePipe(DeviceHandle,
                                  FdoDevice,
                                  &InterfaceHandle->PipeHandle[OpenedPipes]);
            }

            if (NumEndpoints)
            {
                DPRINT1("USBPORT_OpenInterface: USBDStatus - %lx\n", USBDStatus);
            }

            if (IsAllocated)
                ExFreePoolWithTag(InterfaceHandle, USB_PORT_TAG);
        }
    }

    return USBDStatus;
}

static VOID
NTAPI
USBPORT_FreeConfigurationHandle(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                                IN PDEVICE_OBJECT FdoDevice,
                                IN PUSBPORT_CONFIGURATION_HANDLE ConfigHandle)
{
    PLIST_ENTRY iHandleList;
    PUSBPORT_INTERFACE_HANDLE iHandle;
    ULONG NumEndpoints;
    PUSBPORT_PIPE_HANDLE PipeHandle;
    BOOLEAN ClearDeviceConfigHandle;

    DPRINT("USBPORT_FreeConfigurationHandle: ConfigHandle - %p\n", ConfigHandle);

    if (ConfigHandle)
    {
        ClearDeviceConfigHandle = (DeviceHandle->ConfigHandle == ConfigHandle);
        iHandleList = &ConfigHandle->InterfaceHandleList;

        while (!IsListEmpty(iHandleList))
        {
            iHandle = CONTAINING_RECORD(iHandleList->Flink,
                                        USBPORT_INTERFACE_HANDLE,
                                        InterfaceLink);

            DPRINT("USBPORT_CloseConfiguration: iHandle - %p\n", iHandle);

            RemoveHeadList(iHandleList);

            NumEndpoints = iHandle->InterfaceDescriptor.bNumEndpoints;

            PipeHandle = &iHandle->PipeHandle[0];

            while (NumEndpoints > 0)
            {
                USBPORT_ClosePipe(DeviceHandle, FdoDevice, PipeHandle);
                PipeHandle += 1;
                --NumEndpoints;
            }

            ExFreePoolWithTag(iHandle, USB_PORT_TAG);
        }

        ExFreePoolWithTag(ConfigHandle, USB_PORT_TAG);

        if (ClearDeviceConfigHandle)
        {
            DeviceHandle->ConfigHandle = NULL;
        }
    }
}

VOID
NTAPI
USBPORT_CloseConfiguration(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                           IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_CONFIGURATION_HANDLE ConfigHandle;

    DPRINT("USBPORT_CloseConfiguration: ... \n");

    ConfigHandle = DeviceHandle->ConfigHandle;

    if (ConfigHandle)
    {
        USBPORT_FreeConfigurationHandle(DeviceHandle, FdoDevice, ConfigHandle);
    }
}

NTSTATUS
NTAPI
USBPORT_InitInterfaceInfo(IN PUSBD_INTERFACE_INFORMATION InterfaceInfo,
                          IN PUSBPORT_CONFIGURATION_HANDLE ConfigHandle)
{
    PUSB_INTERFACE_DESCRIPTOR Descriptor;
    PUSBD_PIPE_INFORMATION Pipe;
    SIZE_T Length;
    ULONG PipeFlags;
    ULONG NumberOfPipes;
    USBD_STATUS USBDStatus = USBD_STATUS_SUCCESS;

    DPRINT("USBPORT_InitInterfaceInfo: InterfaceInfo - %p, ConfigHandle - %p\n",
           InterfaceInfo,
           ConfigHandle);

    Descriptor = USBPORT_ParseConfigurationDescriptor(ConfigHandle->ConfigurationDescriptor,
                                                      InterfaceInfo->InterfaceNumber,
                                                      InterfaceInfo->AlternateSetting,
                                                      NULL);

    Length = sizeof(USBD_INTERFACE_INFORMATION) +
             sizeof(USBD_PIPE_INFORMATION);

    if (Descriptor)
    {
        NumberOfPipes = Descriptor->bNumEndpoints;

        Length = FIELD_OFFSET(USBD_INTERFACE_INFORMATION, Pipes) +
                 NumberOfPipes * sizeof(USBD_PIPE_INFORMATION);

        if (InterfaceInfo->Length >= Length)
        {
            InterfaceInfo->Class = 0;
            InterfaceInfo->SubClass = 0;
            InterfaceInfo->Protocol = 0;
            InterfaceInfo->Reserved = 0;
            InterfaceInfo->InterfaceHandle = 0;
            InterfaceInfo->NumberOfPipes = NumberOfPipes;

            Pipe = InterfaceInfo->Pipes;

            while (NumberOfPipes > 0)
            {
                Pipe->EndpointAddress = 0;
                Pipe->Interval = 0;
                Pipe->PipeType = 0;
                Pipe->PipeHandle = 0;

                PipeFlags = Pipe->PipeFlags;

                if (PipeFlags & ~USBD_PF_VALID_MASK)
                    USBDStatus = USBD_STATUS_INVALID_PIPE_FLAGS;

                if (!(PipeFlags & USBD_PF_CHANGE_MAX_PACKET))
                    Pipe->MaximumPacketSize = 0;

                Pipe += 1;
                --NumberOfPipes;
            }
        }
        else
        {
            USBDStatus = USBD_STATUS_BUFFER_TOO_SMALL;
        }
    }
    else
    {
        USBDStatus = USBD_STATUS_INTERFACE_NOT_FOUND;
    }

    InterfaceInfo->Length = Length;
    return USBDStatus;
}

NTSTATUS
NTAPI
USBPORT_HandleSelectConfiguration(IN PDEVICE_OBJECT FdoDevice,
                                  IN PIRP Irp,
                                  IN PURB Urb)
{
    PUSB_CONFIGURATION_DESCRIPTOR ConfigDescriptor;
    PUSBPORT_DEVICE_HANDLE DeviceHandle;
    PUSBPORT_CONFIGURATION_HANDLE ConfigHandle = NULL;
    PUSBD_INTERFACE_INFORMATION InterfaceInfo;
    PUSBPORT_INTERFACE_HANDLE InterfaceHandle;
    ULONG iNumber;
    ULONG ix = 0;
    USB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
    NTSTATUS Status;
    USBD_STATUS USBDStatus = USBD_STATUS_SUCCESS;
    USBD_STATUS DeconfigureStatus;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    BOOLEAN ConfigurationSet = FALSE;
    BOOLEAN AlreadyConfigured = FALSE;

    DPRINT("USBPORT_HandleSelectConfiguration: ConfigDescriptor %p\n",
           Urb->UrbSelectConfiguration.ConfigurationDescriptor);

    FdoExtension = FdoDevice->DeviceExtension;

    DPRINT("USBPORT_HandleSelectConfiguration: waiting for DeviceSemaphore\n");

    KeWaitForSingleObject(&FdoExtension->DeviceSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    DPRINT("USBPORT_HandleSelectConfiguration: DeviceSemaphore acquired\n");

    DeviceHandle = Urb->UrbHeader.UsbdDeviceHandle;
    ConfigDescriptor = Urb->UrbSelectConfiguration.ConfigurationDescriptor;

    if (!ConfigDescriptor)
    {
        RtlZeroMemory(&SetupPacket, sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

        SetupPacket.bmRequestType.B = 0;
        SetupPacket.bRequest = USB_REQUEST_SET_CONFIGURATION;
        SetupPacket.wValue.W = 0;
        SetupPacket.wIndex.W = 0;
        SetupPacket.wLength = 0;

        USBPORT_SendSetupPacket(DeviceHandle,
                                FdoDevice,
                                &SetupPacket,
                                NULL,
                                0,
                                NULL,
                                &USBDStatus);

        Status = USBPORT_USBDStatusToNtStatus(Urb, USBDStatus);
        if (NT_SUCCESS(Status))
        {
            USBPORT_CloseConfiguration(DeviceHandle, FdoDevice);
        }

        goto Exit;
    }

    USBPORT_DumpingConfiguration(ConfigDescriptor);

    InterfaceInfo = &Urb->UrbSelectConfiguration.Interface;

    iNumber = 0;

    do
    {
        ++iNumber;
        InterfaceInfo = (PUSBD_INTERFACE_INFORMATION)
                        ((ULONG_PTR)InterfaceInfo +
                         InterfaceInfo->Length);
    }
    while ((ULONG_PTR)InterfaceInfo < (ULONG_PTR)Urb + Urb->UrbHeader.Length);

    if ((iNumber <= 0) || (iNumber != ConfigDescriptor->bNumInterfaces))
    {
        Status = USBPORT_USBDStatusToNtStatus(Urb,
                                              USBD_STATUS_INVALID_CONFIGURATION_DESCRIPTOR);
        goto Exit;
    }

    ConfigHandle = ExAllocatePoolWithTag(NonPagedPool,
                                         ConfigDescriptor->wTotalLength + sizeof(USBPORT_CONFIGURATION_HANDLE),
                                         USB_PORT_TAG);

    if (!ConfigHandle)
    {
        Status = USBPORT_USBDStatusToNtStatus(Urb,
                                              USBD_STATUS_INSUFFICIENT_RESOURCES);
        goto Exit;
    }

    RtlZeroMemory(ConfigHandle,
                  ConfigDescriptor->wTotalLength + sizeof(USBPORT_CONFIGURATION_HANDLE));

    InitializeListHead(&ConfigHandle->InterfaceHandleList);

    ConfigHandle->ConfigurationDescriptor = (PUSB_CONFIGURATION_DESCRIPTOR)(ConfigHandle + 1);

    RtlCopyMemory(ConfigHandle->ConfigurationDescriptor,
                  ConfigDescriptor,
                  ConfigDescriptor->wTotalLength);

    RtlZeroMemory(&SetupPacket, sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

    SetupPacket.bmRequestType.B = 0;
    SetupPacket.bRequest = USB_REQUEST_SET_CONFIGURATION;
    SetupPacket.wValue.W = ConfigDescriptor->bConfigurationValue;
    SetupPacket.wIndex.W = 0;
    SetupPacket.wLength = 0;

    USBPORT_SendSetupPacket(DeviceHandle,
                            FdoDevice,
                            &SetupPacket,
                            NULL,
                            0,
                            NULL,
                            &USBDStatus);

    if (USBD_STATUS(USBDStatus) == USBD_STATUS(USBD_STATUS_STALL_PID))
    {
        UCHAR CurrentConfiguration = 0;
        USBD_STATUS QueryUsbdStatus = USBD_STATUS_SUCCESS;

        if (USBPORT_QueryCurrentConfiguration(DeviceHandle,
                                              FdoDevice,
                                              &CurrentConfiguration,
                                              &QueryUsbdStatus) &&
            CurrentConfiguration == ConfigDescriptor->bConfigurationValue)
        {
            AlreadyConfigured = TRUE;
            USBDStatus = USBD_STATUS_SUCCESS;
        }
    }

    if (USBD_ERROR(USBDStatus))
    {
        Status = USBPORT_USBDStatusToNtStatus(Urb,
                                              USBD_STATUS_SET_CONFIG_FAILED);
        goto Exit;
    }

    ConfigurationSet = TRUE;

    if (iNumber <= 0)
    {
        Status = USBPORT_USBDStatusToNtStatus(Urb,
                                              USBD_STATUS_SUCCESS);

        goto Exit;
    }

    InterfaceInfo = &Urb->UrbSelectConfiguration.Interface;

    for (ix = 0; ix < iNumber; ++ix)
    {
        USBDStatus = USBPORT_InitInterfaceInfo(InterfaceInfo,
                                               ConfigHandle);

        InterfaceHandle = NULL;

        if (USBD_SUCCESS(USBDStatus))
        {
            USBDStatus = USBPORT_OpenInterface(Urb,
                                               DeviceHandle,
                                               FdoDevice,
                                               ConfigHandle,
                                               InterfaceInfo,
                                               &InterfaceHandle,
                                               (InterfaceInfo->AlternateSetting != 0),
                                               AlreadyConfigured);
        }
        else
        {
            DPRINT1("USBPORT_HandleSelectConfiguration: InitInterfaceInfo FAILED ix=%lu USBDStatus=0x%lx\n",
                    ix, USBDStatus);
        }

        if (InterfaceHandle)
        {
            InsertTailList(&ConfigHandle->InterfaceHandleList,
                           &InterfaceHandle->InterfaceLink);
        }

        if (USBD_ERROR(USBDStatus))
            break;

        InterfaceInfo = (PUSBD_INTERFACE_INFORMATION)
                         ((ULONG_PTR)InterfaceInfo +
                          InterfaceInfo->Length);
    }

    if (ix >= iNumber)
    {
        Status = USBPORT_USBDStatusToNtStatus(Urb,
                                              USBD_STATUS_SUCCESS);
    }
    else
    {
        Status = USBPORT_USBDStatusToNtStatus(Urb, USBDStatus);
    }

Exit:

    if (NT_SUCCESS(Status))
    {
        Urb->UrbSelectConfiguration.ConfigurationHandle = ConfigHandle;
        DeviceHandle->ConfigHandle = ConfigHandle;
    }
    else
    {
        if (ConfigHandle)
        {
            USBPORT_FreeConfigurationHandle(DeviceHandle,
                                            FdoDevice,
                                            ConfigHandle);
            ConfigHandle = NULL;

            if (ConfigurationSet)
            {
                RtlZeroMemory(&SetupPacket, sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));
                DeconfigureStatus = USBD_STATUS_SUCCESS;

                SetupPacket.bmRequestType.B = 0;
                SetupPacket.bRequest = USB_REQUEST_SET_CONFIGURATION;
                SetupPacket.wValue.W = 0;
                SetupPacket.wIndex.W = 0;
                SetupPacket.wLength = 0;

                USBPORT_SendSetupPacket(DeviceHandle,
                                        FdoDevice,
                                        &SetupPacket,
                                        NULL,
                                        0,
                                        NULL,
                                        &DeconfigureStatus);

                if (USBD_ERROR(DeconfigureStatus))
                {
                    DPRINT1("USBPORT_HandleSelectConfiguration: rollback SET_CONFIGURATION(0) failed USBDStatus=0x%lx\n",
                            DeconfigureStatus);
                }
            }
        }
    }

    KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                       LOW_REALTIME_PRIORITY,
                       1,
                       FALSE);

    return Status;
}

VOID
NTAPI
USBPORT_AddDeviceHandle(IN PDEVICE_OBJECT FdoDevice,
                        IN PUSBPORT_DEVICE_HANDLE DeviceHandle)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;

    DPRINT("USBPORT_AddDeviceHandle: ... \n");

    FdoExtension = FdoDevice->DeviceExtension;

    InsertTailList(&FdoExtension->DeviceHandleList,
                   &DeviceHandle->DeviceHandleLink);
}

VOID
NTAPI
USBPORT_RemoveDeviceHandle(IN PDEVICE_OBJECT FdoDevice,
                           IN PUSBPORT_DEVICE_HANDLE DeviceHandle)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    KIRQL OldIrql;

    DPRINT("USBPORT_RemoveDeviceHandle \n");

    FdoExtension = FdoDevice->DeviceExtension;

    KeAcquireSpinLock(&FdoExtension->DeviceHandleSpinLock, &OldIrql);
    RemoveEntryList(&DeviceHandle->DeviceHandleLink);
    KeReleaseSpinLock(&FdoExtension->DeviceHandleSpinLock, OldIrql);
}

VOID
NTAPI
USBPORT_DetachChildDeviceHandles(IN PDEVICE_OBJECT FdoDevice,
                                 IN PUSBPORT_DEVICE_HANDLE HubDeviceHandle,
                                 IN PUSBPORT_DEVICE_HANDLE NewHubDeviceHandle)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    /* Children keep a HubDeviceHandle pointer to their parent for the
     * topology walk; when a hub handle is freed while its descendants are
     * still alive (their PnP removal happens later), those pointers would
     * dangle. Repoint them at the replacement handle, or detach them when
     * the parent simply goes away. Callers hold the DeviceSemaphore, which
     * also serializes this against the topology walk in USBPORT_OpenPipe. */
    FdoExtension = FdoDevice->DeviceExtension;

    KeAcquireSpinLock(&FdoExtension->DeviceHandleSpinLock, &OldIrql);

    for (Entry = FdoExtension->DeviceHandleList.Flink; Entry != &FdoExtension->DeviceHandleList; Entry = Entry->Flink)
    {
        PUSBPORT_DEVICE_HANDLE Handle = CONTAINING_RECORD(Entry, USBPORT_DEVICE_HANDLE, DeviceHandleLink);

        if (Handle != HubDeviceHandle && Handle->HubDeviceHandle == HubDeviceHandle)
        {
            DPRINT1("USBPORT_DetachChildDeviceHandles: repointing child %p (addr %u) from hub %p to %p\n", Handle, Handle->DeviceAddress, HubDeviceHandle, NewHubDeviceHandle);
            Handle->HubDeviceHandle = NewHubDeviceHandle;
        }
    }

    KeReleaseSpinLock(&FdoExtension->DeviceHandleSpinLock, OldIrql);
}

VOID
NTAPI
USBPORT_NotifyMiniportRemoveDevice(IN PDEVICE_OBJECT FdoDevice,
                                   IN PUSBPORT_DEVICE_HANDLE DeviceHandle)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    USBPORT_ENDPOINT_PROPERTIES Topology;

    /* Tell the miniport the device at this bus position is gone so it can
     * release per-device controller state (the xHCI slot). This must come
     * from the device-lifetime paths, not from endpoint closes: the default
     * pipe is also closed and rebuilt transiently by USBPORT_ReopenPipe.
     * Callers hold the DeviceSemaphore, keeping the parent chain walkable. */
    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    if (!Packet->RemoveUsbDevice || DeviceHandle->IsRootHub)
        return;

    RtlZeroMemory(&Topology, sizeof(Topology));
    Topology.PortNumber = DeviceHandle->PortNumber;
    USBPORT_ComputeTopologyProperties(DeviceHandle, &Topology);

    Packet->RemoveUsbDevice(FdoExtension->MiniPortExt, DeviceHandle->DeviceAddress, Topology.RootPortNumber, Topology.RouteString);
}

BOOLEAN
NTAPI
USBPORT_ValidateDeviceHandle(IN PDEVICE_OBJECT FdoDevice,
                             IN PUSBPORT_DEVICE_HANDLE DeviceHandle)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    KIRQL OldIrql;
    PLIST_ENTRY HandleList;
    PUSBPORT_DEVICE_HANDLE CurrentHandle;
    BOOLEAN Result = FALSE;

    //DPRINT("USBPORT_ValidateDeviceHandle: DeviceHandle - %p\n", DeviceHandle \n");

    FdoExtension = FdoDevice->DeviceExtension;

    KeAcquireSpinLock(&FdoExtension->DeviceHandleSpinLock, &OldIrql);
    if (DeviceHandle)
    {
        HandleList = FdoExtension->DeviceHandleList.Flink;

        while (HandleList != &FdoExtension->DeviceHandleList)
        {
            CurrentHandle = CONTAINING_RECORD(HandleList,
                                              USBPORT_DEVICE_HANDLE,
                                              DeviceHandleLink);

            if (CurrentHandle == DeviceHandle)
            {
                Result = TRUE;
                break;
            }

            HandleList = HandleList->Flink;
        }
    }
    KeReleaseSpinLock(&FdoExtension->DeviceHandleSpinLock, OldIrql);

    return Result;
}

BOOLEAN
NTAPI
USBPORT_DeviceHasTransfers(IN PDEVICE_OBJECT FdoDevice,
                           IN PUSBPORT_DEVICE_HANDLE DeviceHandle)
{
    PLIST_ENTRY PipeHandleList;
    PUSBPORT_PIPE_HANDLE PipeHandle;

    DPRINT("USBPORT_DeviceHasTransfers: ... \n");

    PipeHandleList = DeviceHandle->PipeHandleList.Flink;

    while (PipeHandleList != &DeviceHandle->PipeHandleList)
    {
        PipeHandle = CONTAINING_RECORD(PipeHandleList,
                                       USBPORT_PIPE_HANDLE,
                                       PipeLink);

        PipeHandleList = PipeHandleList->Flink;

        if (!(PipeHandle->Flags & PIPE_HANDLE_FLAG_NULL_PACKET_SIZE) &&
            USBPORT_EndpointHasQueuedTransfers(FdoDevice, PipeHandle->Endpoint, NULL))
        {
            return TRUE;
        }
    }

    return FALSE;
}

VOID
NTAPI
USBPORT_AbortTransfers(IN PDEVICE_OBJECT FdoDevice,
                       IN PUSBPORT_DEVICE_HANDLE DeviceHandle)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PLIST_ENTRY HandleList;
    PUSBPORT_PIPE_HANDLE PipeHandle;
    BOOLEAN Result;
    ULONG WaitCount;
    ULONG MaxWaitCount;
    KIRQL OldIrql;

    DPRINT("USBPORT_AbortAllTransfers: ... \n");

    FdoExtension = FdoDevice->DeviceExtension;

    /* If the host controller never started, treat all endpoints as dead and
     * skip miniport abort callbacks that assume valid hardware state. */
    if (!(FdoExtension->Flags & USBPORT_FLAG_HC_STARTED))
    {
        DPRINT1("USBPORT_AbortTransfers: HC not started; treating endpoints as dead\n");
        return;
    }

    HandleList = DeviceHandle->PipeHandleList.Flink;

    while (HandleList != &DeviceHandle->PipeHandleList)
    {
        PipeHandle = CONTAINING_RECORD(HandleList,
                                       USBPORT_PIPE_HANDLE,
                                       PipeLink);

        HandleList = HandleList->Flink;

        if (!(PipeHandle->Flags & DEVICE_HANDLE_FLAG_ROOTHUB))
        {
            PipeHandle->Endpoint->Flags |= ENDPOINT_FLAG_ABORTING;

            USBPORT_AbortEndpoint(FdoDevice, PipeHandle->Endpoint, NULL);
            USBPORT_FlushMapTransfers(FdoDevice);
        }
    }

    /*
     * Wait for all transfers to be drained from the device's endpoints.
     * Use a timeout (5 seconds) to prevent infinite stalls when the
     * hardware cannot complete aborts (e.g., device disconnected and
     * the miniport's AbortTransfer returns early). After the timeout,
     * set ENDPOINT_FLAG_NUKE on all endpoints to force-complete any
     * remaining transfers.
     */
    WaitCount = 0;
    MaxWaitCount = 50; /* 50 * 100ms = 5 seconds */

    while (TRUE)
    {
        Result = USBPORT_DeviceHasTransfers(FdoDevice, DeviceHandle);

        if (!Result)
            break;

        WaitCount++;

        if (WaitCount >= MaxWaitCount)
        {
            DPRINT1("USBPORT_AbortTransfers: timeout after %lu ms, forcing NUKE on endpoints\n",
                    WaitCount * 100);

            /*
             * Timeout reached: transfers are stuck because the hardware
             * cannot abort them (e.g., device physically disconnected).
             * Set ENDPOINT_FLAG_NUKE on all device endpoints to allow
             * USBPORT_DmaEndpointPaused to skip the miniport abort call
             * and directly complete the transfers.
             */
            HandleList = DeviceHandle->PipeHandleList.Flink;

            while (HandleList != &DeviceHandle->PipeHandleList)
            {
                PipeHandle = CONTAINING_RECORD(HandleList,
                                               USBPORT_PIPE_HANDLE,
                                               PipeLink);

                HandleList = HandleList->Flink;

                if (!(PipeHandle->Flags & DEVICE_HANDLE_FLAG_ROOTHUB) &&
                    PipeHandle->Endpoint)
                {
                    KeAcquireSpinLock(&PipeHandle->Endpoint->EndpointSpinLock, &OldIrql);
                    PipeHandle->Endpoint->Flags |= ENDPOINT_FLAG_NUKE;
                    KeReleaseSpinLock(&PipeHandle->Endpoint->EndpointSpinLock, OldIrql);

                    /* Re-trigger abort processing with NUKE flag set */
                    USBPORT_AbortEndpoint(FdoDevice, PipeHandle->Endpoint, NULL);
                }
            }

            USBPORT_FlushMapTransfers(FdoDevice);
        }

        USBPORT_Wait(FdoDevice, 100);
    }

    DPRINT("USBPORT_AbortTransfers: completed after %lu iterations\n", WaitCount);
}

PUSB2_TT_EXTENSION
NTAPI
USBPORT_GetTt(IN PDEVICE_OBJECT FdoDevice,
              IN PUSBPORT_DEVICE_HANDLE HubDeviceHandle,
              OUT PUSHORT OutPort,
              OUT PUSBPORT_DEVICE_HANDLE * OutHubDeviceHandle)
{
    PUSBPORT_DEVICE_HANDLE DeviceHandle = HubDeviceHandle;
    ULONG TtCount;
    PLIST_ENTRY Entry;
    PUSB2_TT_EXTENSION TtExtension = NULL;

    DPRINT("USBPORT_GetTt: HubDeviceHandle - %p\n", HubDeviceHandle);

    *OutHubDeviceHandle = NULL;

    while (DeviceHandle->DeviceSpeed != UsbHighSpeed)
    {
        DPRINT("USBPORT_GetTt: DeviceHandle - %p, DeviceHandle->PortNumber - %X\n",
               DeviceHandle,
               DeviceHandle->PortNumber);

        *OutPort = DeviceHandle->PortNumber;

        DeviceHandle = DeviceHandle->HubDeviceHandle;

        if (!DeviceHandle)
            return NULL;
    }

    TtCount = DeviceHandle->TtCount;

    if (!TtCount)
        return NULL;

    if (IsListEmpty(&DeviceHandle->TtList))
        return NULL;

    Entry = DeviceHandle->TtList.Flink;

    if (TtCount > 1)
    {
        while (Entry != &DeviceHandle->TtList)
        {
            ASSERT(Entry != NULL);

            TtExtension = CONTAINING_RECORD(Entry,
                                            USB2_TT_EXTENSION,
                                            Link);

            if (TtExtension->TtNumber == *OutPort)
                break;

            Entry = Entry->Flink;

            TtExtension = NULL;
        }
    }
    else
    {
        TtExtension = CONTAINING_RECORD(Entry,
                                        USB2_TT_EXTENSION,
                                        Link);
    }

    *OutHubDeviceHandle = DeviceHandle;

    return TtExtension;
}

NTSTATUS
NTAPI
USBPORT_CreateDevice(IN OUT PUSB_DEVICE_HANDLE *pUsbdDeviceHandle,
                     IN PDEVICE_OBJECT FdoDevice,
                     IN PUSBPORT_DEVICE_HANDLE HubDeviceHandle,
                     IN USHORT PortStatus,
                     IN USHORT Port)
{
    USB_PORT_STATUS PortStatusUnion;
    PUSBPORT_DEVICE_HANDLE TtDeviceHandle = NULL;
    PUSB2_TT_EXTENSION TtExtension = NULL;
    USHORT port;
    PUSBPORT_DEVICE_HANDLE DeviceHandle;
    PUSBPORT_PIPE_HANDLE PipeHandle;
    PUSBPORT_ENDPOINT Endpoint;
    BOOL IsOpenedPipe;
    PVOID DeviceDescriptor;
    USB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
    ULONG TransferedLen;
    ULONG DescriptorMinSize;
    USHORT MaxPacketSize;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    NTSTATUS Status;

    DPRINT("USBPORT_CreateDevice: PortStatus - %p, Port - %x\n",
           PortStatus,
           Port);

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    KeWaitForSingleObject(&FdoExtension->DeviceSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    if (!USBPORT_ValidateDeviceHandle(FdoDevice, HubDeviceHandle))
    {
        KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                           LOW_REALTIME_PRIORITY,
                           1,
                           FALSE);

        DPRINT1("USBPORT_CreateDevice: Not valid hub DeviceHandle\n");
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    port = Port;

    if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2 &&
        !(PortStatus & USB_PORT_STATUS_HIGH_SPEED))
    {
        DPRINT1("USBPORT_CreateDevice: USB1 device connected to USB2 port\n");

        TtExtension = USBPORT_GetTt(FdoDevice,
                                    HubDeviceHandle,
                                    &port,
                                    &TtDeviceHandle);

        DPRINT("USBPORT_CreateDevice: TtDeviceHandle - %p, port - %x\n",
               TtDeviceHandle,
               port);
    }

    KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                       LOW_REALTIME_PRIORITY,
                       1,
                       FALSE);

    DeviceHandle = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(USBPORT_DEVICE_HANDLE),
                                         USB_PORT_TAG);

    if (!DeviceHandle)
    {
        DPRINT1("USBPORT_CreateDevice: Not allocated DeviceHandle\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(DeviceHandle, sizeof(USBPORT_DEVICE_HANDLE));

    *pUsbdDeviceHandle = NULL;

    DeviceHandle->TtExtension = TtExtension;
    DeviceHandle->PortNumber = Port;
    DeviceHandle->HubDeviceHandle = HubDeviceHandle;

    /*
     * Derive the initial device speed from the port status. USB 2.0 and USB
     * 3.0 port status fields overlap, so only treat NegotiatedDeviceSpeed as
     * authoritative for SuperSpeed. Otherwise fall back to the classic LOW/HIGH
     * bits used by USB 2.0 ports, including xHCI companion USB 2.0 ports.
     */
    PortStatusUnion.AsUshort16 = PortStatus;
    if ((Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB3) &&
        PortStatusUnion.Usb30PortStatus.NegotiatedDeviceSpeed >= 4)
    {
        DeviceHandle->DeviceSpeed = UsbSuperSpeed;
    }
    else if (PortStatus & USB_PORT_STATUS_LOW_SPEED)
    {
        DeviceHandle->DeviceSpeed = UsbLowSpeed;
    }
    else if (PortStatus & USB_PORT_STATUS_HIGH_SPEED)
    {
        DeviceHandle->DeviceSpeed = UsbHighSpeed;
    }
    else
    {
        DeviceHandle->DeviceSpeed = UsbFullSpeed;
    }

    KeWaitForSingleObject(&FdoExtension->DeviceSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    PipeHandle = &DeviceHandle->PipeHandle;
    Endpoint = NULL;

    PipeHandle->Flags = PIPE_HANDLE_FLAG_CLOSED;

    PipeHandle->EndpointDescriptor.bLength = sizeof(PipeHandle->EndpointDescriptor);
    PipeHandle->EndpointDescriptor.bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;

    /*
     * Initial EP0 max-packet size before we have seen the device descriptor.
     *
     * The USB 2.0 / 3.x specs fix EP0 MPS to:
     *  - 64 bytes for all High-Speed devices.
     *  - 512 bytes for all SuperSpeed devices (bMaxPacketSize0 == 9 encoding).
     *
     * Full-/Low-Speed devices still report their literal EP0 MPS in the
     * device descriptor (8/16/32/64), so until we have fetched at least
     * the bMaxPacketSize0 field we start them at 8 bytes here and remap
     * once the descriptor is available.
     *
     * NOTE: The "first 8 bytes only" rule for the initial GET_DESCRIPTOR
     * is enforced via the transfer length, not by programming an invalid
     * EP0 MPS into the controller. High-Speed devices must always use 64
     * and SuperSpeed devices must always use 512 at the hardware level.
     */
    if (DeviceHandle->DeviceSpeed == UsbSuperSpeed)
    {
        PipeHandle->EndpointDescriptor.wMaxPacketSize = 512;
    }
    else if (DeviceHandle->DeviceSpeed == UsbHighSpeed)
    {
        PipeHandle->EndpointDescriptor.wMaxPacketSize = USB_DEFAULT_MAX_PACKET;
    }
    else
    {
        PipeHandle->EndpointDescriptor.wMaxPacketSize = 8;
    }

    InitializeListHead(&DeviceHandle->PipeHandleList);
    InitializeListHead(&DeviceHandle->TtList);

    Status = USBPORT_OpenPipe(FdoDevice,
                              DeviceHandle,
                              PipeHandle,
                              NULL);

    IsOpenedPipe = NT_SUCCESS(Status);

    if (NT_ERROR(Status))
    {
        DPRINT1("USBPORT_CreateDevice: USBPORT_OpenPipe return - %lx\n", Status);

        KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                           LOW_REALTIME_PRIORITY,
                           1,
                           FALSE);

        ExFreePoolWithTag(DeviceHandle, USB_PORT_TAG);

        return Status;
    }

    Endpoint = PipeHandle->Endpoint;

    /* Allocate enough space for the full device descriptor, not just MPS0. */
    DeviceDescriptor = ExAllocatePoolWithTag(NonPagedPool,
                                             sizeof(USB_DEVICE_DESCRIPTOR),
                                             USB_PORT_TAG);

    if (!DeviceDescriptor)
    {
        DPRINT1("USBPORT_CreateDevice: Not allocated DeviceDescriptor\n");
        goto ErrorExit;
    }

    RtlZeroMemory(DeviceDescriptor, sizeof(USB_DEVICE_DESCRIPTOR));
    RtlZeroMemory(&SetupPacket, sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

    DescriptorMinSize = RTL_SIZEOF_THROUGH_FIELD(USB_DEVICE_DESCRIPTOR,
                                                 bMaxPacketSize0);

    SetupPacket.bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
    SetupPacket.bRequest = USB_REQUEST_GET_DESCRIPTOR;
    SetupPacket.wValue.HiByte = USB_DEVICE_DESCRIPTOR_TYPE;
    SetupPacket.wLength = DescriptorMinSize;

    TransferedLen = 0;

    Status = USBPORT_SendSetupPacket(DeviceHandle,
                                     FdoDevice,
                                     &SetupPacket,
                                     DeviceDescriptor,
                                     DescriptorMinSize,
                                     &TransferedLen,
                                     NULL);

    if (!NT_SUCCESS(Status) || TransferedLen < DescriptorMinSize)
        goto PostDescriptor;

    RtlZeroMemory(&DeviceHandle->DeviceDescriptor,
                  sizeof(USB_DEVICE_DESCRIPTOR));
    RtlCopyMemory(&DeviceHandle->DeviceDescriptor,
                  DeviceDescriptor,
                  (TransferedLen < sizeof(USB_DEVICE_DESCRIPTOR)) ?
                  TransferedLen : sizeof(USB_DEVICE_DESCRIPTOR));

    /*
     * Map bMaxPacketSize0 to an actual max packet size for EP0.
     * For USB 1.x/2.0 this is the literal size (8/16/32/64).
     * For USB 3.x SuperSpeed devices, the encoding is 9 -> 512 bytes.
     *
     * In debug builds we have seen cases where DeviceHandle->DeviceDescriptor
     * remained all zeros even though the transfer for the first 8 bytes of
     * the descriptor completed successfully (e.g. QEMU xHCI SW‑ENUM). To
     * avoid treating those as bogus devices, re-synchronize the cached
     * descriptor from the temporary buffer if it still looks empty here and
     * fall back to a spec‑compliant EP0 MPS whenever the raw value is zero.
     */
    {
        ULONG RawMaxPacketSize;

        if (DeviceHandle->DeviceDescriptor.bLength == 0 &&
            TransferedLen >= DescriptorMinSize &&
            DeviceDescriptor != NULL)
        {
            RtlCopyMemory(&DeviceHandle->DeviceDescriptor,
                          DeviceDescriptor,
                          (TransferedLen < sizeof(USB_DEVICE_DESCRIPTOR)) ?
                          TransferedLen : sizeof(USB_DEVICE_DESCRIPTOR));
        }

        RawMaxPacketSize = DeviceHandle->DeviceDescriptor.bMaxPacketSize0;

        /*
         * Treat a raw value of zero as a malformed descriptor and substitute
         * the minimum legal EP0 size for the negotiated device speed so that
         * enumeration can continue. This mirrors how Windows tolerates some
         * emulated controllers that do not fill bMaxPacketSize0 correctly.
         */
        if (RawMaxPacketSize == 0)
        {
            switch (DeviceHandle->DeviceSpeed)
            {
                case UsbHighSpeed:
                    MaxPacketSize = USB_DEFAULT_MAX_PACKET;
                    break;

                case UsbSuperSpeed:
                    MaxPacketSize = 512;
                    break;

                default:
                    /* Low-/Full-speed: start from 8 bytes for EP0. */
                    MaxPacketSize = 8;
                    break;
            }
        }
        else if (DeviceHandle->DeviceDescriptor.bcdUSB >= 0x0300 &&
                 RawMaxPacketSize == 9)
        {
            MaxPacketSize = 512;
        }
        else
        {
            MaxPacketSize = RawMaxPacketSize;
        }
    }

    if (MaxPacketSize != 8 &&
        MaxPacketSize != 16 &&
        MaxPacketSize != 32 &&
        MaxPacketSize != 64 &&
        MaxPacketSize != 512)
    {
        DPRINT1("USBPORT_CreateDevice: invalid EP0 MPS %u from descriptor\n",
                MaxPacketSize);
        goto PostDescriptor;
    }

    if (DeviceHandle->DeviceSpeed == UsbHighSpeed &&
        MaxPacketSize != USB_DEFAULT_MAX_PACKET)
    {
        DPRINT1("USBPORT_CreateDevice: HS device reported illegal EP0 MPS %u (expected 64)\n",
                MaxPacketSize);
        goto PostDescriptor;
    }

    if (DeviceHandle->DeviceSpeed == UsbSuperSpeed &&
        MaxPacketSize != 512)
    {
        DPRINT1("USBPORT_CreateDevice: SS device reported illegal EP0 MPS %u (expected 512)\n",
                MaxPacketSize);
        goto PostDescriptor;
    }

    PipeHandle->EndpointDescriptor.wMaxPacketSize = (USHORT)MaxPacketSize;

    if (Endpoint)
    {
        Endpoint->EndpointProperties.TotalMaxPacketSize = MaxPacketSize;
        Endpoint->EndpointProperties.MaxPacketSize = MaxPacketSize;
    }

    Status = USBPORT_ReopenPipe(FdoDevice, Endpoint);

    if (!NT_SUCCESS(Status))
        goto PostDescriptor;

    USBPORT_Wait(FdoDevice, 10);

    {
        NTSTATUS InitialStatus = Status;
        ULONG InitialTransferLen = TransferedLen;

        RtlZeroMemory(&SetupPacket, sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

        SetupPacket.bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
        SetupPacket.bRequest = USB_REQUEST_GET_DESCRIPTOR;
        SetupPacket.wValue.HiByte = USB_DEVICE_DESCRIPTOR_TYPE;
        SetupPacket.wLength = sizeof(USB_DEVICE_DESCRIPTOR);

        TransferedLen = 0;

        Status = USBPORT_SendSetupPacket(DeviceHandle,
                                         FdoDevice,
                                         &SetupPacket,
                                         DeviceDescriptor,
                                         sizeof(USB_DEVICE_DESCRIPTOR),
                                         &TransferedLen,
                                         NULL);

        /*
         * Some emulated controllers (notably the QEMU/xHCI SW-ENUM path)
         * can complete the full-device GET_DESCRIPTOR with a short length
         * even though the buffer contains a complete, well-formed device
         * descriptor.  Accept any successful transfer that returned at
         * least through bMaxPacketSize0 and copy whatever was delivered.
         */
        if (NT_SUCCESS(Status) &&
            TransferedLen >= DescriptorMinSize)
        {
            ULONG CopyLength =
                (TransferedLen < sizeof(USB_DEVICE_DESCRIPTOR)) ?
                TransferedLen : sizeof(USB_DEVICE_DESCRIPTOR);

            RtlCopyMemory(&DeviceHandle->DeviceDescriptor,
                          DeviceDescriptor,
                          CopyLength);
        }
        else if (!NT_SUCCESS(Status) &&
                 NT_SUCCESS(InitialStatus) &&
                 InitialTransferLen >= DescriptorMinSize)
        {
            Status = STATUS_SUCCESS;
            TransferedLen = InitialTransferLen;
        }
    }

PostDescriptor:

    ExFreePoolWithTag(DeviceDescriptor, USB_PORT_TAG);

    if (NT_SUCCESS(Status) && (TransferedLen >= DescriptorMinSize))
    {
        if ((DeviceHandle->DeviceDescriptor.bLength >= sizeof(USB_DEVICE_DESCRIPTOR)) &&
            (DeviceHandle->DeviceDescriptor.bDescriptorType == USB_DEVICE_DESCRIPTOR_TYPE))
        {
            /*
             * Re-evaluate EP0 max-packet size using the same mapping as
             * above so that SuperSpeed devices (bcdUSB >= 0x0300 with
             * bMaxPacketSize0 == 9 -> 512 bytes) and emulated devices
             * with a bogus zero value are accepted here too.
             */
            {
                ULONG RawMaxPacketSize = DeviceHandle->DeviceDescriptor.bMaxPacketSize0;

                if (RawMaxPacketSize == 0)
                {
                    switch (DeviceHandle->DeviceSpeed)
                    {
                        case UsbHighSpeed:
                            MaxPacketSize = USB_DEFAULT_MAX_PACKET;
                            break;

                        case UsbSuperSpeed:
                            MaxPacketSize = 512;
                            break;

                        default:
                            MaxPacketSize = 8;
                            break;
                    }
                }
                else if (DeviceHandle->DeviceDescriptor.bcdUSB >= 0x0300 &&
                         RawMaxPacketSize == 9)
                {
                    MaxPacketSize = 512;
                }
                else
                {
                    MaxPacketSize = RawMaxPacketSize;
                }
            }

            if (MaxPacketSize == 8 ||
                MaxPacketSize == 16 ||
                MaxPacketSize == 32 ||
                MaxPacketSize == 64 ||
                MaxPacketSize == 512)
            {
                USBPORT_AddDeviceHandle(FdoDevice, DeviceHandle);

                *pUsbdDeviceHandle = DeviceHandle;

                KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                                   LOW_REALTIME_PRIORITY,
                                   1,
                                   FALSE);

                return Status;
            }
        }
    }

    DPRINT1("USBPORT_CreateDevice: ERROR!!! TransferedLen - %x, Status - %lx\n",
            TransferedLen,
            Status);

    /* Keep the structured dump for anyone building with URB tracing enabled */
    USBPORT_DumpingDeviceDescriptor(&DeviceHandle->DeviceDescriptor);

ErrorExit:

    if (TtExtension && TtDeviceHandle)
    {
        SetupPacket.bmRequestType.Recipient = BMREQUEST_TO_OTHER;
        SetupPacket.bmRequestType.Reserved = 0;
        SetupPacket.bmRequestType.Type = BMREQUEST_CLASS;
        SetupPacket.bmRequestType.Dir = BMREQUEST_HOST_TO_DEVICE;

        /* Table 11-15.  Hub Class Requests */
        if (TtDeviceHandle == HubDeviceHandle)
        {
            SetupPacket.bRequest = USB_REQUEST_RESET_TT;
        }
        else
        {
            SetupPacket.bRequest = USB_REQUEST_CLEAR_TT_BUFFER;
        }

        SetupPacket.wValue.LowByte = 0;
        SetupPacket.wValue.HiByte = 0;
        SetupPacket.wIndex.W = port;
        SetupPacket.wLength = 0;

        USBPORT_SendSetupPacket(TtDeviceHandle,
                                FdoDevice,
                                &SetupPacket,
                                NULL,
                                0,
                                NULL,
                                NULL);
    }

    DPRINT1("USBPORT_CreateDevice: Failed with Status %x, masking to STATUS_DEVICE_DATA_ERROR\n", Status);
    Status = STATUS_DEVICE_DATA_ERROR;

    if (IsOpenedPipe)
    {
        USBPORT_NotifyMiniportRemoveDevice(FdoDevice, DeviceHandle);
        USBPORT_ClosePipe(DeviceHandle, FdoDevice, PipeHandle);
    }

    KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                       LOW_REALTIME_PRIORITY,
                       1,
                       FALSE);

    ExFreePoolWithTag(DeviceHandle, USB_PORT_TAG);

    return Status;
}

ULONG
NTAPI
USBPORT_AllocateUsbAddress(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    ULONG BitMapIdx;
    ULONG BitNumber;
    ULONG ix;

    DPRINT("USBPORT_AllocateUsbAddress \n");

    FdoExtension = FdoDevice->DeviceExtension;

    for (ix = 0; ix < 4; ++ix)
    {
        BitMapIdx = 1;

        for (BitNumber = 0; BitNumber < 32; ++BitNumber)
        {
            if (!(FdoExtension->UsbAddressBitMap[ix] & BitMapIdx))
            {
                FdoExtension->UsbAddressBitMap[ix] |= BitMapIdx;
                return 32 * ix + BitNumber;
            }

            BitMapIdx <<= 1;
        }
    }

    return 0;
}

VOID
NTAPI
USBPORT_FreeUsbAddress(IN PDEVICE_OBJECT FdoDevice,
                       IN USHORT DeviceAddress)
{
    PUSBPORT_DEVICE_EXTENSION  FdoExtension;
    ULONG ix;
    ULONG BitMapIdx;
    ULONG BitNumber;
    USHORT CurrentAddress;

    DPRINT("USBPORT_FreeUsbAddress: DeviceAddress - %x\n", DeviceAddress);

    FdoExtension = FdoDevice->DeviceExtension;

    for (ix = 0; ix < 4; ++ix)
    {
        BitMapIdx = 1;
        CurrentAddress = 32 * ix;

        for (BitNumber = 0; BitNumber < 32; ++BitNumber)
        {
            if (CurrentAddress == DeviceAddress)
            {
                FdoExtension->UsbAddressBitMap[ix] &= ~BitMapIdx;
                return;
            }

            BitMapIdx <<= 1;
            CurrentAddress++;
        }
    }
}

NTSTATUS
NTAPI
USBPORT_InitializeDevice(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                         IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_ENDPOINT Endpoint;
    USB_DEFAULT_PIPE_SETUP_PACKET CtrlSetup;
    ULONG EffectiveMaxPacketSize;
    ULONG TransferedLen;
    USHORT DeviceAddress = 0;
    UCHAR MaxPacketSize;
    NTSTATUS Status;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    MPSTATUS MpStatus;

    DPRINT("USBPORT_InitializeDevice: ... \n");

    ASSERT(DeviceHandle != NULL);

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    KeWaitForSingleObject(&FdoExtension->DeviceSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    /* The miniport ResetDevice callback takes a ROOT port number. For a
     * device behind an external hub, PortNumber is hub-relative and would
     * alias an unrelated root-attached slot; those devices get a fresh slot
     * per enumeration and the hub-port reset was already done by usbhub, so
     * no controller-side device reset is needed. */
    if (Packet->ResetDevice && DeviceHandle->HubDeviceHandle && DeviceHandle->HubDeviceHandle->IsRootHub)
    {
        MpStatus = Packet->ResetDevice(FdoExtension->MiniPortExt,
                                       DeviceHandle->PortNumber);
        if (MpStatus != MP_STATUS_SUCCESS)
        {
            DPRINT1("USBPORT_InitializeDevice: ResetDevice failed on port %u (MpStatus=%lu)\n",
                    DeviceHandle->PortNumber,
                    MpStatus);
            Status = STATUS_DEVICE_DATA_ERROR;
            goto ExitError;
        }
    }

    DeviceAddress = USBPORT_AllocateUsbAddress(FdoDevice);
    ASSERT(DeviceHandle->DeviceAddress == USB_DEFAULT_DEVICE_ADDRESS);

    RtlZeroMemory(&CtrlSetup, sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

    CtrlSetup.bRequest = USB_REQUEST_SET_ADDRESS;
    CtrlSetup.wValue.W = DeviceAddress;

    Status = USBPORT_SendSetupPacket(DeviceHandle,
                                     FdoDevice,
                                     &CtrlSetup,
                                     NULL,
                                     0,
                                     NULL,
                                     NULL);

    DPRINT("USBPORT_InitializeDevice: DeviceAddress - %x. SendSetupPacket Status - %x\n",
           DeviceAddress,
           Status);

    if (!NT_SUCCESS(Status))
        goto ExitError;

    DeviceHandle->DeviceAddress = DeviceAddress;
    Endpoint = DeviceHandle->PipeHandle.Endpoint;

    Endpoint->EndpointProperties.TotalMaxPacketSize = DeviceHandle->DeviceDescriptor.bMaxPacketSize0;
    Endpoint->EndpointProperties.DeviceAddress = DeviceAddress;

    Status = USBPORT_ReopenPipe(FdoDevice, Endpoint);

    if (!NT_SUCCESS(Status))
        goto ExitError;

    USBPORT_Wait(FdoDevice, 10);

    RtlZeroMemory(&CtrlSetup, sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

    CtrlSetup.bRequest = USB_REQUEST_GET_DESCRIPTOR;
    CtrlSetup.wValue.HiByte = USB_DEVICE_DESCRIPTOR_TYPE;
    CtrlSetup.wLength = sizeof(USB_DEVICE_DESCRIPTOR);
    CtrlSetup.bmRequestType.B = 0x80;

    /*
     * Issue the final full‑descriptor GET_DESCRIPTOR(DEVICE) with a
     * small retry budget for SuperSpeed devices. Some USB 3.x devices
     * are slow to respond immediately after SET_ADDRESS; giving them
     * a couple of extra attempts here avoids spurious enumeration
     * failures while still failing quickly on real errors.
     */
    TransferedLen = 0;
    if (DeviceHandle->DeviceSpeed == UsbSuperSpeed)
    {
        ULONG attempt;

        for (attempt = 0; attempt < 3; attempt++)
        {
            TransferedLen = 0;

            Status = USBPORT_SendSetupPacket(DeviceHandle,
                                             FdoDevice,
                                             &CtrlSetup,
                                             &DeviceHandle->DeviceDescriptor,
                                             sizeof(USB_DEVICE_DESCRIPTOR),
                                             &TransferedLen,
                                             NULL);

            if (NT_SUCCESS(Status) &&
                TransferedLen == sizeof(USB_DEVICE_DESCRIPTOR))
            {
                break;
            }

            DPRINT1("USBPORT_InitializeDevice: GET_DESCRIPTOR(DEVICE) attempt %lu failed (Status=%x, Len=%lu)\n",
                    attempt + 1,
                    Status,
                    TransferedLen);

            USBPORT_Wait(FdoDevice, 10);
        }
    }
    else
    {
        Status = USBPORT_SendSetupPacket(DeviceHandle,
                                         FdoDevice,
                                         &CtrlSetup,
                                         &DeviceHandle->DeviceDescriptor,
                                         sizeof(USB_DEVICE_DESCRIPTOR),
                                         &TransferedLen,
                                         NULL);
    }

    if (NT_SUCCESS(Status) &&
        TransferedLen == sizeof(USB_DEVICE_DESCRIPTOR))
    {
        ASSERT(DeviceHandle->DeviceDescriptor.bLength >= sizeof(USB_DEVICE_DESCRIPTOR));
        ASSERT(DeviceHandle->DeviceDescriptor.bDescriptorType == USB_DEVICE_DESCRIPTOR_TYPE);
        /*
         * Map bMaxPacketSize0 to the actual EP0 max-packet size.
         * For USB 1.x/2.0 this is the literal value (8/16/32/64).
         * For USB 3.x SuperSpeed devices the encoding is 9 -> 512 bytes.
         * Keep the Endpoint properties in sync with this mapping so that
         * the miniport and USBPORT agree on EP0 transfer sizes.
         */
        MaxPacketSize = DeviceHandle->DeviceDescriptor.bMaxPacketSize0;
        if (DeviceHandle->DeviceDescriptor.bcdUSB >= 0x0300 &&
            MaxPacketSize == 9)
        {
            EffectiveMaxPacketSize = 512;
        }
        else
        {
            EffectiveMaxPacketSize = MaxPacketSize;
        }

        ASSERT((EffectiveMaxPacketSize == 8)  ||
               (EffectiveMaxPacketSize == 16) ||
               (EffectiveMaxPacketSize == 32) ||
               (EffectiveMaxPacketSize == 64) ||
               (EffectiveMaxPacketSize == 512));

        if (DeviceHandle->DeviceSpeed == UsbHighSpeed &&
            EffectiveMaxPacketSize != USB_DEFAULT_MAX_PACKET)
        {
            DPRINT1("USBPORT_InitializeDevice: HS device has illegal EP0 MPS %lu (expected 64)\n",
                    EffectiveMaxPacketSize);
            goto ExitError;
        }

        if (DeviceHandle->DeviceSpeed == UsbSuperSpeed &&
            EffectiveMaxPacketSize != 512)
        {
            DPRINT1("USBPORT_InitializeDevice: SS device has illegal EP0 MPS %lu (expected 512)\n",
                    EffectiveMaxPacketSize);
            goto ExitError;
        }

        if (DeviceHandle->DeviceSpeed == UsbHighSpeed &&
            DeviceHandle->DeviceDescriptor.bDeviceClass == USB_DEVICE_CLASS_HUB)
        {
            DeviceHandle->Flags |= DEVICE_HANDLE_FLAG_USB2HUB;
        }

        Endpoint->EndpointProperties.TotalMaxPacketSize = EffectiveMaxPacketSize;
        Endpoint->EndpointProperties.MaxPacketSize = EffectiveMaxPacketSize;

        /*
         * For devices that support a BOS, retrieve and parse it so that
         * USB 2.0 LPM, SuperSpeed capabilities and Container ID are
         * available in the device handle for later policy and IOCTLs.
         * Failure to retrieve or parse BOS is non-fatal.
         */
        USBPORT_FetchBosDescriptor(DeviceHandle, FdoDevice);

        /*
         * Compute a basic link power management policy for SuperSpeed devices
         * based on the BOS SuperSpeed capability (U1/U2 exit latencies).
         * This only derives per-device flags; actual link state programming
         * is handled separately in the xHCI / hub policy code.
         */
        USBPORT_ComputeLpmPolicy(DeviceHandle);
    }
    else
    {
ExitError:
        DPRINT1("USBPORT_InitializeDevice: ExitError. Status - %x\n", Status);
    }

    KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                       LOW_REALTIME_PRIORITY,
                       1,
                       FALSE);

    return Status;
}

NTSTATUS
NTAPI
USBPORT_GetUsbDescriptor(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                         IN PDEVICE_OBJECT FdoDevice,
                         IN UCHAR Type,
                         IN PUCHAR ConfigDesc,
                         IN PULONG ConfigDescSize)
{
    USB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
    NTSTATUS Status;

    DPRINT("USBPORT_GetUsbDescriptor: Type - %x\n");

    RtlZeroMemory(&SetupPacket, sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

    SetupPacket.bmRequestType.Dir = BMREQUEST_DEVICE_TO_HOST;
    SetupPacket.bRequest = USB_REQUEST_GET_DESCRIPTOR;
    SetupPacket.wValue.HiByte = Type;
    SetupPacket.wLength = (USHORT)*ConfigDescSize;

    Status = USBPORT_SendSetupPacket(DeviceHandle,
                                     FdoDevice,
                                     &SetupPacket,
                                     ConfigDesc,
                                     *ConfigDescSize,
                                     ConfigDescSize,
                                     NULL);

    return Status;
}

static
VOID
USBPORT_FetchBosDescriptor(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                           IN PDEVICE_OBJECT FdoDevice)
{
    USB_BOS_DESCRIPTOR BosHeader;
    ULONG Length;
    NTSTATUS Status;
    PUCHAR BosBuffer = NULL;
    ULONG BosTotalLength;
    PUCHAR Ptr;
    PUCHAR End;

    /*
     * BOS (Binary Object Store) descriptor is only mandatory for USB 3.0+
     * devices. USB 2.1 (0x0210) devices MAY have BOS for LPM support, but
     * USB 2.0 (0x0200) and below devices typically do not support BOS and
     * will STALL the request, causing the endpoint to halt and triggering
     * expensive reset sequences with 5-second timeouts.
     *
     * Skip BOS fetch for USB 2.0 and below to avoid unnecessary stalls.
     */
    if (DeviceHandle->DeviceDescriptor.bcdUSB < 0x0201)
        return;

    RtlZeroMemory(&BosHeader, sizeof(BosHeader));
    Length = sizeof(BosHeader);

    Status = USBPORT_GetUsbDescriptor(DeviceHandle,
                                      FdoDevice,
                                      USB_BOS_DESCRIPTOR_TYPE,
                                      (PUCHAR)&BosHeader,
                                      &Length);

    if (!NT_SUCCESS(Status) ||
        Length < sizeof(USB_BOS_DESCRIPTOR) ||
        BosHeader.bLength < sizeof(USB_BOS_DESCRIPTOR) ||
        BosHeader.bDescriptorType != USB_BOS_DESCRIPTOR_TYPE ||
        BosHeader.wTotalLength < sizeof(USB_BOS_DESCRIPTOR))
    {
        return;
    }

    BosTotalLength = BosHeader.wTotalLength;

    if (BosTotalLength > 512)
        BosTotalLength = 512;

    BosBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                      BosTotalLength,
                                      USB_PORT_TAG);
    if (!BosBuffer)
        return;

    RtlZeroMemory(BosBuffer, BosTotalLength);
    Length = BosTotalLength;

    Status = USBPORT_GetUsbDescriptor(DeviceHandle,
                                      FdoDevice,
                                      USB_BOS_DESCRIPTOR_TYPE,
                                      BosBuffer,
                                      &Length);

    if (!NT_SUCCESS(Status) ||
        Length < sizeof(USB_BOS_DESCRIPTOR))
        goto Done;

    if (Length > BosTotalLength)
        Length = BosTotalLength;

    DeviceHandle->HasBosDescriptor = TRUE;

    Ptr = BosBuffer;
    End = BosBuffer + Length;

    if ((ULONG)(End - Ptr) < sizeof(USB_BOS_DESCRIPTOR))
        goto Done;

    Ptr += ((PUSB_BOS_DESCRIPTOR)Ptr)->bLength;

    while (Ptr + sizeof(USB_COMMON_DESCRIPTOR) <= End)
    {
        PUSB_COMMON_DESCRIPTOR Common;

        Common = (PUSB_COMMON_DESCRIPTOR)Ptr;

        if (Common->bLength < sizeof(USB_COMMON_DESCRIPTOR) ||
            Ptr + Common->bLength > End)
        {
            break;
        }

        if (Common->bDescriptorType == 0x10) /* Device Capability */
        {
            if (Common->bLength >= sizeof(USB_DEVICE_CAPABILITY_USB20_EXTENSION_DESCRIPTOR) &&
                !DeviceHandle->HasUsb20Extension)
            {
                const USB_DEVICE_CAPABILITY_USB20_EXTENSION_DESCRIPTOR *Ext =
                    (const USB_DEVICE_CAPABILITY_USB20_EXTENSION_DESCRIPTOR *)Common;

                if (Ext->bDevCapabilityType == USB_DEVICE_CAPABILITY_USB20_EXTENSION)
                {
                    DeviceHandle->HasUsb20Extension = TRUE;
                    DeviceHandle->Usb20LpmCapable = Ext->bmAttributes.LPMCapable ? TRUE : FALSE;
                    DeviceHandle->Usb20BeslSupported =
                        Ext->bmAttributes.BESLAndAlternateHIRDSupported ? TRUE : FALSE;
                    DeviceHandle->Usb20BaselineBesl = (UCHAR)Ext->bmAttributes.BaselineBESL;
                    DeviceHandle->Usb20DeepBesl = (UCHAR)Ext->bmAttributes.DeepBESL;
                }
            }
            else if (Common->bLength >= sizeof(USB_DEVICE_CAPABILITY_SUPERSPEED_USB_DESCRIPTOR) &&
                     !DeviceHandle->HasSsCapability)
            {
                const USB_DEVICE_CAPABILITY_SUPERSPEED_USB_DESCRIPTOR *Ss =
                    (const USB_DEVICE_CAPABILITY_SUPERSPEED_USB_DESCRIPTOR *)Common;

                if (Ss->bDevCapabilityType == USB_DEVICE_CAPABILITY_SUPERSPEED_USB)
                {
                    DeviceHandle->HasSsCapability = TRUE;
                    DeviceHandle->SsSpeedsSupported = Ss->wSpeedsSupported;
                    DeviceHandle->SsFunctionalitySupport = Ss->bFunctionalitySupport;
                    DeviceHandle->SsU1ExitLatency = Ss->bU1DevExitLat;
                    DeviceHandle->SsU2ExitLatency = Ss->wU2DevExitLat;
                }
            }
            else if (Common->bLength >= sizeof(USB_DEVICE_CAPABILITY_CONTAINER_ID_DESCRIPTOR) &&
                     !DeviceHandle->HasContainerId)
            {
                const USB_DEVICE_CAPABILITY_CONTAINER_ID_DESCRIPTOR *Cid =
                    (const USB_DEVICE_CAPABILITY_CONTAINER_ID_DESCRIPTOR *)Common;

                if (Cid->bDevCapabilityType == USB_DEVICE_CAPABILITY_CONTAINER_ID)
                {
                    DeviceHandle->HasContainerId = TRUE;
                    RtlCopyMemory(DeviceHandle->ContainerId,
                                  Cid->ContainerID,
                                  sizeof(DeviceHandle->ContainerId));
                }
            }
        }

        Ptr += Common->bLength;
    }

Done:
    ExFreePoolWithTag(BosBuffer, USB_PORT_TAG);
}

static
VOID
USBPORT_ComputeLpmPolicy(IN PUSBPORT_DEVICE_HANDLE DeviceHandle)
{
    BOOLEAN allowU1 = FALSE;
    BOOLEAN allowU2 = FALSE;

    if (DeviceHandle->DeviceSpeed == UsbSuperSpeed &&
        DeviceHandle->HasSsCapability)
    {
        if (DeviceHandle->SsU1ExitLatency <= USB_DEVICE_CAPABILITY_SUPERSPEED_U1_DEVICE_EXIT_MAX_VALUE)
        {
            allowU1 = TRUE;
        }

        if (DeviceHandle->SsU2ExitLatency <= USB_DEVICE_CAPABILITY_SUPERSPEED_U2_DEVICE_EXIT_MAX_VALUE)
        {
            allowU2 = TRUE;
        }
    }

    DeviceHandle->LpmPolicyComputed = TRUE;
    DeviceHandle->LpmAllowU1 = allowU1;
    DeviceHandle->LpmAllowU2 = allowU2;
}

PUSBPORT_INTERFACE_HANDLE
NTAPI
USBPORT_GetInterfaceHandle(IN PUSBPORT_CONFIGURATION_HANDLE ConfigurationHandle,
                           IN UCHAR InterfaceNumber)
{
    PUSBPORT_INTERFACE_HANDLE InterfaceHandle;
    PLIST_ENTRY iHandleList;
    UCHAR InterfaceNum;

    DPRINT("USBPORT_GetInterfaceHandle: ConfigurationHandle - %p, InterfaceNumber - %p\n",
           ConfigurationHandle,
           InterfaceNumber);

    iHandleList = ConfigurationHandle->InterfaceHandleList.Flink;

    while (iHandleList &&
           (iHandleList != &ConfigurationHandle->InterfaceHandleList))
    {
        InterfaceHandle = CONTAINING_RECORD(iHandleList,
                                            USBPORT_INTERFACE_HANDLE,
                                            InterfaceLink);

        InterfaceNum = InterfaceHandle->InterfaceDescriptor.bInterfaceNumber;

        if (InterfaceNum == InterfaceNumber)
            return InterfaceHandle;

        iHandleList = InterfaceHandle->InterfaceLink.Flink;
    }

    return NULL;
}

NTSTATUS
NTAPI
USBPORT_HandleSelectInterface(IN PDEVICE_OBJECT FdoDevice,
                              IN PIRP Irp,
                              IN PURB Urb)
{
    PUSBPORT_DEVICE_HANDLE DeviceHandle;
    PUSBPORT_CONFIGURATION_HANDLE ConfigurationHandle;
    PUSBD_INTERFACE_INFORMATION Interface;
    PUSBPORT_INTERFACE_HANDLE InterfaceHandle;
    PUSBPORT_INTERFACE_HANDLE iHandle;
    PUSBPORT_PIPE_HANDLE PipeHandle;
    USBD_STATUS USBDStatus;
    USHORT Length;
    ULONG ix;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;

    DPRINT("USBPORT_HandleSelectInterface: ... \n");

    FdoExtension = FdoDevice->DeviceExtension;

    KeWaitForSingleObject(&FdoExtension->DeviceSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    ConfigurationHandle = Urb->UrbSelectInterface.ConfigurationHandle;

    Interface = &Urb->UrbSelectInterface.Interface;

    Length = Interface->Length + sizeof(USBD_PIPE_INFORMATION);
    Urb->UrbHeader.Length = Length;

    USBDStatus = USBPORT_InitInterfaceInfo(Interface, ConfigurationHandle);

    if (USBDStatus)
    {
        Interface->InterfaceHandle = (USBD_INTERFACE_HANDLE)-1;
        goto Exit;
    }

    DeviceHandle = Urb->UrbHeader.UsbdDeviceHandle;

    InterfaceHandle = USBPORT_GetInterfaceHandle(ConfigurationHandle,
                                                 Interface->InterfaceNumber);

    if (InterfaceHandle)
    {
        RemoveEntryList(&InterfaceHandle->InterfaceLink);

        if (InterfaceHandle->InterfaceDescriptor.bNumEndpoints)
        {
            PipeHandle = &InterfaceHandle->PipeHandle[0];

            for (ix = 0;
                 ix < InterfaceHandle->InterfaceDescriptor.bNumEndpoints;
                 ix++)
            {
                USBPORT_ClosePipe(DeviceHandle, FdoDevice, PipeHandle);
                PipeHandle += 1;
            }
        }
    }

    iHandle = 0;

    USBDStatus = USBPORT_OpenInterface(Urb,
                                       DeviceHandle,
                                       FdoDevice,
                                       ConfigurationHandle,
                                       Interface,
                                       &iHandle,
                                       TRUE,
                                       FALSE);

    if (USBDStatus)
    {
        Interface->InterfaceHandle = (USBD_INTERFACE_HANDLE)-1;
    }
    else
    {
        if (InterfaceHandle)
            ExFreePoolWithTag(InterfaceHandle, USB_PORT_TAG);

        Interface->InterfaceHandle = iHandle;

        InsertTailList(&ConfigurationHandle->InterfaceHandleList,
                       &iHandle->InterfaceLink);
    }

Exit:

    KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                       LOW_REALTIME_PRIORITY,
                       1,
                       FALSE);

    return USBPORT_USBDStatusToNtStatus(Urb, USBDStatus);
}

NTSTATUS
NTAPI
USBPORT_RemoveDevice(IN PDEVICE_OBJECT FdoDevice,
                     IN OUT PUSBPORT_DEVICE_HANDLE DeviceHandle,
                     IN ULONG Flags)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSB2_TT_EXTENSION TtExtension;
    ULONG ix;
    KIRQL OldIrql;

    DPRINT("USBPORT_RemoveDevice: DeviceHandle - %p, Flags - %x\n",
           DeviceHandle,
           Flags);

    FdoExtension = FdoDevice->DeviceExtension;

    if ((Flags & USBD_KEEP_DEVICE_DATA) ||
        (Flags & USBD_MARK_DEVICE_BUSY))
    {
        return STATUS_SUCCESS;
    }

    KeWaitForSingleObject(&FdoExtension->DeviceSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    if (!USBPORT_ValidateDeviceHandle(FdoDevice, DeviceHandle))
    {
        KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                           LOW_REALTIME_PRIORITY,
                           1,
                           FALSE);

        DPRINT1("USBPORT_RemoveDevice: Not valid device handle\n");
        return STATUS_DEVICE_NOT_CONNECTED;
    }

#if DBG
    if (DeviceHandle->Flags & DEVICE_HANDLE_FLAG_ROOTHUB)
    {
        PVOID CallStack[8] = {0};
        USHORT Depth;
        USHORT Index;

        Depth = RtlCaptureStackBackTrace(1,
                                         RTL_NUMBER_OF(CallStack),
                                         CallStack,
                                         NULL);

        DPRINT1("USBPORT_RemoveDevice: root hub handle %p removal requested (Flags=0x%lx)\n",
                DeviceHandle,
                DeviceHandle->Flags);

        for (Index = 0; Index < Depth; Index++)
        {
            DPRINT1("USBPORT_RemoveDevice:  caller[%u]=%p\n",
                    Index,
                    CallStack[Index]);
        }
    }
#endif

    USBPORT_RemoveDeviceHandle(FdoDevice, DeviceHandle);

    DeviceHandle->Flags |= DEVICE_HANDLE_FLAG_REMOVED;

    USBPORT_AbortTransfers(FdoDevice, DeviceHandle);

    DPRINT("USBPORT_RemoveDevice: DeviceHandleLock - %x\n",
           DeviceHandle->DeviceHandleLock);

    while (InterlockedDecrement(&DeviceHandle->DeviceHandleLock) >= 0)
    {
        InterlockedIncrement(&DeviceHandle->DeviceHandleLock);
        USBPORT_Wait(FdoDevice, 100);
    }

    DPRINT("USBPORT_RemoveDevice: DeviceHandleLock ok\n");

    if (DeviceHandle->ConfigHandle)
    {
        USBPORT_CloseConfiguration(DeviceHandle, FdoDevice);
    }

    USBPORT_NotifyMiniportRemoveDevice(FdoDevice, DeviceHandle);

    USBPORT_ClosePipe(DeviceHandle, FdoDevice, &DeviceHandle->PipeHandle);

    if (DeviceHandle->DeviceAddress)
    {
        USBPORT_FreeUsbAddress(FdoDevice, DeviceHandle->DeviceAddress);
    }

    if (!IsListEmpty(&DeviceHandle->TtList))
    {
        DPRINT1("USBPORT_RemoveDevice: DeviceHandle->TtList not empty\n");
    }

    while (!IsListEmpty(&DeviceHandle->TtList))
    {
        TtExtension = CONTAINING_RECORD(DeviceHandle->TtList.Flink,
                                        USB2_TT_EXTENSION,
                                        Link);

        RemoveHeadList(&DeviceHandle->TtList);

        DPRINT("USBPORT_RemoveDevice: TtExtension - %p\n", TtExtension);

        KeAcquireSpinLock(&FdoExtension->TtSpinLock, &OldIrql);

        TtExtension->Flags |= USB2_TT_EXTENSION_FLAG_DELETED;

        if (IsListEmpty(&TtExtension->EndpointList))
        {
            USBPORT_UpdateAllocatedBwTt(TtExtension);

            for (ix = 0; ix < USB2_FRAMES; ix++)
            {
                FdoExtension->Bandwidth[ix] += TtExtension->MaxBandwidth;
            }

            DPRINT("USBPORT_RemoveDevice: ExFreePoolWithTag TtExtension - %p\n", TtExtension);
            ExFreePoolWithTag(TtExtension, USB_PORT_TAG);
        }

        KeReleaseSpinLock(&FdoExtension->TtSpinLock, OldIrql);
    }

    USBPORT_DetachChildDeviceHandles(FdoDevice, DeviceHandle, NULL);

    KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                       LOW_REALTIME_PRIORITY,
                       1,
                       FALSE);

    if (!(DeviceHandle->Flags & DEVICE_HANDLE_FLAG_ROOTHUB))
    {
        ExFreePoolWithTag(DeviceHandle, USB_PORT_TAG);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
USBPORT_RestoreDevice(IN PDEVICE_OBJECT FdoDevice,
                      IN OUT PUSBPORT_DEVICE_HANDLE OldDeviceHandle,
                      IN OUT PUSBPORT_DEVICE_HANDLE NewDeviceHandle)
{
    PUSBPORT_DEVICE_EXTENSION  FdoExtension;
    PLIST_ENTRY iHandleList;
    PUSBPORT_ENDPOINT Endpoint;
    USBPORT_ENDPOINT_REQUIREMENTS EndpointRequirements = {0};
    USB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
    NTSTATUS Status = STATUS_SUCCESS;
    USBD_STATUS USBDStatus;
    KIRQL OldIrql;
    PUSBPORT_INTERFACE_HANDLE InterfaceHandle;
    PUSBPORT_PIPE_HANDLE PipeHandle;
    PUSBPORT_REGISTRATION_PACKET Packet;

    DPRINT("USBPORT_RestoreDevice: OldDeviceHandle - %p, NewDeviceHandle - %p\n",
           OldDeviceHandle,
           NewDeviceHandle);

    FdoExtension = FdoDevice->DeviceExtension;

    KeWaitForSingleObject(&FdoExtension->DeviceSemaphore,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    if (!USBPORT_ValidateDeviceHandle(FdoDevice, OldDeviceHandle))
    {
        KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                           LOW_REALTIME_PRIORITY,
                           1,
                           FALSE);

        DPRINT1("USBPORT_RestoreDevice: OldDeviceHandle not valid\n");
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    if (!USBPORT_ValidateDeviceHandle(FdoDevice, NewDeviceHandle))
    {
        KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                           LOW_REALTIME_PRIORITY,
                           1,
                           FALSE);
        DPRINT1("USBPORT_RestoreDevice: NewDeviceHandle not valid\n");
        return STATUS_DEVICE_NOT_CONNECTED;
    }

    USBPORT_RemoveDeviceHandle(FdoDevice, OldDeviceHandle);
    USBPORT_AbortTransfers(FdoDevice, OldDeviceHandle);

    while (InterlockedDecrement(&OldDeviceHandle->DeviceHandleLock) >= 0)
    {
        InterlockedIncrement(&OldDeviceHandle->DeviceHandleLock);
        USBPORT_Wait(FdoDevice, 100);
    }

    if (sizeof(USB_DEVICE_DESCRIPTOR) == RtlCompareMemory(&NewDeviceHandle->DeviceDescriptor,
                                                          &OldDeviceHandle->DeviceDescriptor,
                                                          sizeof(USB_DEVICE_DESCRIPTOR)))
    {
        NewDeviceHandle->ConfigHandle = OldDeviceHandle->ConfigHandle;

        if (OldDeviceHandle->ConfigHandle)
        {
            RtlZeroMemory(&SetupPacket, sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

            SetupPacket.bmRequestType.Dir = BMREQUEST_HOST_TO_DEVICE;
            SetupPacket.bRequest = USB_REQUEST_SET_CONFIGURATION;
            SetupPacket.wValue.W = OldDeviceHandle->ConfigHandle->ConfigurationDescriptor->bConfigurationValue;
            SetupPacket.wIndex.W = 0;
            SetupPacket.wLength = 0;

            USBPORT_SendSetupPacket(NewDeviceHandle,
                                    FdoDevice,
                                    &SetupPacket,
                                    NULL,
                                    0,
                                    NULL,
                                    &USBDStatus);

            if (USBD_ERROR(USBDStatus))
                Status = USBPORT_USBDStatusToNtStatus(NULL, USBDStatus);

            if (NT_SUCCESS(Status))
            {
                iHandleList = NewDeviceHandle->ConfigHandle->InterfaceHandleList.Flink;

                while (iHandleList &&
                       iHandleList != &NewDeviceHandle->ConfigHandle->InterfaceHandleList)
                {
                    InterfaceHandle = CONTAINING_RECORD(iHandleList,
                                                        USBPORT_INTERFACE_HANDLE,
                                                        InterfaceLink);

                    if (InterfaceHandle->AlternateSetting)
                    {
                        RtlZeroMemory(&SetupPacket, sizeof(USB_DEFAULT_PIPE_SETUP_PACKET));

                        SetupPacket.bmRequestType.Dir = BMREQUEST_HOST_TO_DEVICE;
                        SetupPacket.bmRequestType.Type = BMREQUEST_STANDARD;
                        SetupPacket.bmRequestType.Recipient = BMREQUEST_TO_INTERFACE;

                        SetupPacket.bRequest = USB_REQUEST_SET_INTERFACE;
                        SetupPacket.wValue.W = InterfaceHandle->InterfaceDescriptor.bAlternateSetting;
                        SetupPacket.wIndex.W = InterfaceHandle->InterfaceDescriptor.bInterfaceNumber;
                        SetupPacket.wLength = 0;

                        USBPORT_SendSetupPacket(NewDeviceHandle,
                                                FdoDevice,
                                                &SetupPacket,
                                                NULL,
                                                0,
                                                NULL,
                                                &USBDStatus);
                    }

                    iHandleList = iHandleList->Flink;
                }
            }
        }

        if (NewDeviceHandle->Flags & DEVICE_HANDLE_FLAG_USB2HUB)
        {
            NewDeviceHandle->TtCount = OldDeviceHandle->TtCount;
        }

        while (!IsListEmpty(&OldDeviceHandle->PipeHandleList))
        {
            PipeHandle = CONTAINING_RECORD(OldDeviceHandle->PipeHandleList.Flink,
                                           USBPORT_PIPE_HANDLE,
                                           PipeLink);

            DPRINT("USBPORT_RestoreDevice: PipeHandle - %p\n", PipeHandle);

            USBPORT_RemovePipeHandle(OldDeviceHandle, PipeHandle);

            if (PipeHandle != &OldDeviceHandle->PipeHandle)
            {
                USBPORT_AddPipeHandle(NewDeviceHandle, PipeHandle);

                if (!(PipeHandle->Flags & PIPE_HANDLE_FLAG_NULL_PACKET_SIZE))
                {
                    Endpoint = PipeHandle->Endpoint;
                    Endpoint->DeviceHandle = NewDeviceHandle;
                    Endpoint->EndpointProperties.DeviceAddress = NewDeviceHandle->DeviceAddress;

                    Packet = &FdoExtension->MiniPortInterface->Packet;

                    if (!(Endpoint->Flags & ENDPOINT_FLAG_NUKE))
                    {
                        KeAcquireSpinLock(&FdoExtension->MiniportSpinLock,
                                          &OldIrql);

                        Packet->ReopenEndpoint(FdoExtension->MiniPortExt,
                                               &Endpoint->EndpointProperties,
                                               Endpoint + 1);

                        Packet->SetEndpointDataToggle(FdoExtension->MiniPortExt,
                                                      Endpoint + 1,
                                                      0);

                        Packet->SetEndpointStatus(FdoExtension->MiniPortExt,
                                                  Endpoint + 1,
                                                  USBPORT_ENDPOINT_RUN);

                        KeReleaseSpinLock(&FdoExtension->MiniportSpinLock,
                                          OldIrql);
                    }
                    else
                    {
                        MiniportCloseEndpoint(FdoDevice, Endpoint);

                        RtlZeroMemory(Endpoint + 1, Packet->MiniPortEndpointSize);

                        RtlZeroMemory((PVOID)Endpoint->EndpointProperties.BufferVA,
                                      Endpoint->EndpointProperties.BufferLength);

                        KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

                        Packet->QueryEndpointRequirements(FdoExtension->MiniPortExt,
                                                          &Endpoint->EndpointProperties,
                                                          &EndpointRequirements);

                        KeReleaseSpinLock(&FdoExtension->MiniportSpinLock,
                                          OldIrql);

                        MiniportOpenEndpoint(FdoDevice, Endpoint);

                        Endpoint->Flags &= ~(ENDPOINT_FLAG_NUKE |
                                             ENDPOINT_FLAG_ABORTING);

                        KeAcquireSpinLock(&Endpoint->EndpointSpinLock,
                                          &Endpoint->EndpointOldIrql);

                        if (Endpoint->StateLast == USBPORT_ENDPOINT_ACTIVE)
                        {
                            KeAcquireSpinLockAtDpcLevel(&FdoExtension->MiniportSpinLock);

                            Packet->SetEndpointState(FdoExtension->MiniPortExt,
                                                     Endpoint + 1,
                                                     USBPORT_ENDPOINT_ACTIVE);

                            KeReleaseSpinLockFromDpcLevel(&FdoExtension->MiniportSpinLock);
                        }

                        KeReleaseSpinLock(&Endpoint->EndpointSpinLock,
                                          Endpoint->EndpointOldIrql);
                    }
                }
            }
        }

        USBPORT_AddPipeHandle(OldDeviceHandle, &OldDeviceHandle->PipeHandle);
    }
    else
    {
        DPRINT1("USBPORT_RestoreDevice: New DeviceDescriptor != Old DeviceDescriptor\n");
        Status = STATUS_UNSUCCESSFUL;
    }

    USBPORT_NotifyMiniportRemoveDevice(FdoDevice, OldDeviceHandle);

    USBPORT_ClosePipe(OldDeviceHandle, FdoDevice, &OldDeviceHandle->PipeHandle);

    if (OldDeviceHandle->DeviceAddress != 0)
        USBPORT_FreeUsbAddress(FdoDevice, OldDeviceHandle->DeviceAddress);

    USBPORT_DetachChildDeviceHandles(FdoDevice, OldDeviceHandle, NT_SUCCESS(Status) ? NewDeviceHandle : NULL);

    KeReleaseSemaphore(&FdoExtension->DeviceSemaphore,
                       LOW_REALTIME_PRIORITY,
                       1,
                       FALSE);

    ExFreePoolWithTag(OldDeviceHandle, USB_PORT_TAG);

    return Status;
}

NTSTATUS
NTAPI
USBPORT_InitializeTT(IN PDEVICE_OBJECT FdoDevice,
                     IN PUSBPORT_DEVICE_HANDLE HubDeviceHandle,
                     IN ULONG TtNumber)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSB2_TT_EXTENSION TtExtension;
    ULONG ix;

    DPRINT("USBPORT_InitializeTT: HubDeviceHandle - %p, TtNumber - %X\n",
           HubDeviceHandle,
           TtNumber);

    FdoExtension = FdoDevice->DeviceExtension;

    TtExtension = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(USB2_TT_EXTENSION),
                                        USB_PORT_TAG);

    if (!TtExtension)
    {
        DPRINT1("USBPORT_InitializeTT: ExAllocatePoolWithTag return NULL\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DPRINT("USBPORT_InitializeTT: TtExtension - %p\n", TtExtension);

    RtlZeroMemory(TtExtension, sizeof(USB2_TT_EXTENSION));

    TtExtension->DeviceAddress = HubDeviceHandle->DeviceAddress;
    TtExtension->TtNumber = TtNumber;
    TtExtension->RootHubPdo = FdoExtension->RootHubPdo;
    TtExtension->BusBandwidth = TOTAL_USB11_BUS_BANDWIDTH;

    InitializeListHead(&TtExtension->EndpointList);

    /* 90% maximum allowed for periodic endpoints */
    for (ix = 0; ix < USB2_FRAMES; ix++)
    {
        TtExtension->Bandwidth[ix] = TtExtension->BusBandwidth -
                                     TtExtension->BusBandwidth / 10;
    }

    USBPORT_UpdateAllocatedBwTt(TtExtension);

    for (ix = 0; ix < USB2_FRAMES; ix++)
    {
        FdoExtension->Bandwidth[ix] -= TtExtension->MaxBandwidth;
    }

    USB2_InitTT(FdoExtension->Usb2Extension, &TtExtension->Tt);

    InsertTailList(&HubDeviceHandle->TtList, &TtExtension->Link);

    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
USBPORT_Initialize20Hub(IN PDEVICE_OBJECT FdoDevice,
                        IN PUSBPORT_DEVICE_HANDLE HubDeviceHandle,
                        IN ULONG TtCount)
{
    NTSTATUS Status;
    ULONG ix;

    DPRINT("USBPORT_Initialize20Hub: TtCount - %X\n", TtCount);

    if (!HubDeviceHandle)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (HubDeviceHandle->Flags & DEVICE_HANDLE_FLAG_ROOTHUB)
    {
        return STATUS_SUCCESS;
    }

    if (TtCount == 0)
    {
        HubDeviceHandle->TtCount = 0;
        return STATUS_SUCCESS;
    }

    for (ix = 0; ix < TtCount; ++ix)
    {
        Status = USBPORT_InitializeTT(FdoDevice, HubDeviceHandle, ix + 1);

        if (!NT_SUCCESS(Status))
            break;
    }

    HubDeviceHandle->TtCount = TtCount;

    return Status;
}
