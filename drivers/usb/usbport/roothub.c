/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     USBPort root hub implementation
 * COPYRIGHT:   Copyright 2017 Vadim Galyant <vgal@rambler.ru>
 */

#include "usbport.h"

#define NDEBUG
#include <debug.h>

#define NDEBUG_USBPORT_CORE
#include "usbdebug.h"

#if DBG
#ifndef USBPORT_DBG_RH_TRACE
#define USBPORT_DBG_RH_TRACE 0
#endif
#if USBPORT_DBG_RH_TRACE
#define USBPORT_RH_TRACE DPRINT1
#else
#define USBPORT_RH_TRACE(...) do { } while (0)
#endif
#else
#define USBPORT_RH_TRACE(...) do { } while (0)
#endif

#if DBG
static
VOID
USBPORT_DumpDeviceHandleList(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    PLIST_ENTRY Entry;
    ULONG Index = 0;
    PUSBPORT_RHDEVICE_EXTENSION RhExtension;
    PUSBPORT_DEVICE_HANDLE RootHubHandle = NULL;

    if (!FdoExtension)
    {
        USBPORT_RH_TRACE("USBPORT_DumpDeviceHandleList: FdoExtension is NULL\n");
        return;
    }

    RhExtension = USBPORT_GetRootHubExtension(FdoExtension);
    if (RhExtension)
        RootHubHandle = &RhExtension->DeviceHandle;

    UNREFERENCED_PARAMETER(RootHubHandle);

    USBPORT_RH_TRACE("USBPORT_DumpDeviceHandleList: Fdo=%p ListHead=%p RootHubHandle=%p\n",
                     FdoExtension->CommonExtension.SelfDevice,
                     &FdoExtension->DeviceHandleList,
                     RootHubHandle);

    Entry = FdoExtension->DeviceHandleList.Flink;

    while (Entry != &FdoExtension->DeviceHandleList && Index < 32)
    {
        PUSBPORT_DEVICE_HANDLE Handle;
        ULONG Flags;

        Handle = CONTAINING_RECORD(Entry,
                                   USBPORT_DEVICE_HANDLE,
                                   DeviceHandleLink);

        Flags = Handle->Flags;

        UNREFERENCED_PARAMETER(Flags);

        USBPORT_RH_TRACE("  [%lu] Handle=%p Flags=0x%lx Addr=%lu Removed=%lu Lock=%ld\n",
                         Index,
                         Handle,
                         Flags,
                         Handle->DeviceAddress,
                         (Flags & DEVICE_HANDLE_FLAG_REMOVED) ? 1 : 0,
                         Handle->DeviceHandleLock);

        Entry = Entry->Flink;
        Index++;
    }

    if (Entry != &FdoExtension->DeviceHandleList)
    {
        USBPORT_RH_TRACE("USBPORT_DumpDeviceHandleList: truncated output (more than %lu entries)\n",
                         Index);
    }
}
#else
#define USBPORT_DumpDeviceHandleList(x) ((VOID)0)
#endif

RHSTATUS
NTAPI
USBPORT_MPStatusToRHStatus(IN MPSTATUS MPStatus)
{
    RHSTATUS RHStatus = RH_STATUS_SUCCESS;

    //DPRINT("USBPORT_MPStatusToRHStatus: MPStatus - %x\n", MPStatus);

    if (MPStatus)
    {
        RHStatus = (MPStatus != MP_STATUS_FAILURE);
        ++RHStatus;
    }

    return RHStatus;
}

MPSTATUS
NTAPI
USBPORT_RH_SetFeatureUSB2PortPower(IN PDEVICE_OBJECT FdoDevice,
                                   IN USHORT Port)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PDEVICE_RELATIONS CompanionControllersList;
    PUSBPORT_REGISTRATION_PACKET CompanionPacket;
    PDEVICE_OBJECT CompanionFdoDevice;
    PUSBPORT_DEVICE_EXTENSION CompanionFdoExtension;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    USHORT ix;
    PDEVICE_OBJECT * Entry;
    ULONG NumController = 0;

    DPRINT("USBPORT_RootHub_PowerUsb2Port: FdoDevice - %p, Port - %p\n",
           FdoDevice,
           Port);

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    CompanionControllersList = USBPORT_FindCompanionControllers(FdoDevice,
                                                                FALSE,
                                                                TRUE);

    if (!CompanionControllersList)
    {
        Packet->RH_SetFeaturePortPower(FdoExtension->MiniPortExt, Port);
        return MP_STATUS_SUCCESS;
    }

    Entry = &CompanionControllersList->Objects[0];

    while (NumController < CompanionControllersList->Count)
    {
        CompanionFdoDevice = *Entry;

        CompanionFdoExtension = CompanionFdoDevice->DeviceExtension;
        CompanionPacket = &CompanionFdoExtension->MiniPortInterface->Packet;

        PdoExtension = USBPORT_GetRootHubExtension(CompanionFdoExtension);

        for (ix = 0;
             (PdoExtension->CommonExtension.PnpStateFlags & USBPORT_PNP_STATE_STARTED) &&
              ix < PdoExtension->RootHubDescriptors->Descriptor.bNumberOfPorts;
             ++ix)
        {
            CompanionPacket->RH_SetFeaturePortPower(CompanionFdoExtension->MiniPortExt,
                                                    ix + 1);
        }

        ++NumController;
        ++Entry;
    }

    Packet->RH_SetFeaturePortPower(FdoExtension->MiniPortExt, Port);

    if (CompanionControllersList)
    {
        ExFreePoolWithTag(CompanionControllersList, USB_PORT_TAG);
    }

    return MP_STATUS_SUCCESS;
}

RHSTATUS
NTAPI
USBPORT_RootHubClassCommand(IN PDEVICE_OBJECT FdoDevice,
                            IN PUSB_DEFAULT_PIPE_SETUP_PACKET SetupPacket,
                            IN PVOID Buffer,
                            IN PULONG BufferLength)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    USHORT Port;
    USHORT Feature;
    MPSTATUS MPStatus;
    RHSTATUS RHStatus = RH_STATUS_UNSUCCESSFUL;
    KIRQL OldIrql;

    DPRINT("USBPORT_RootHubClassCommand: USB command - %x, *BufferLength - %x\n",
           SetupPacket->bRequest,
           *BufferLength);

    FdoExtension = FdoDevice->DeviceExtension;
    PdoExtension = USBPORT_GetRootHubExtension(FdoExtension);
    Packet = &FdoExtension->MiniPortInterface->Packet;

    Port = SetupPacket->wIndex.W;

    switch (SetupPacket->bRequest)
    {
        case USB_REQUEST_GET_STATUS:
        {
            if (!Buffer)
            {
                return RHStatus;
            }

            *(PULONG)Buffer = 0;

            if (SetupPacket->bmRequestType.Recipient == BMREQUEST_TO_OTHER)
            {
                ASSERT(*BufferLength >= 4);

                if (Port > PdoExtension->RootHubDescriptors->Descriptor.bNumberOfPorts ||
                    Port <= 0  ||
                    SetupPacket->wLength < 4)
                {
                    return RHStatus;
                }

                KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

                MPStatus = Packet->RH_GetPortStatus(FdoExtension->MiniPortExt,
                                                    SetupPacket->wIndex.W,
                                                    Buffer);

                KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);
            }
            else
            {
                KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

                MPStatus = Packet->RH_GetHubStatus(FdoExtension->MiniPortExt,
                                                   Buffer);

                KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);
            }

            RHStatus = USBPORT_MPStatusToRHStatus(MPStatus);
            break;
        }

        case USB_REQUEST_CLEAR_FEATURE:
            Feature = SetupPacket->wValue.W;

            if ((SetupPacket->bmRequestType.Recipient) != USBPORT_RECIPIENT_PORT)
            {
                if (Feature == FEATURE_C_HUB_LOCAL_POWER)
                {
                    RHStatus = RH_STATUS_SUCCESS;
                    return RHStatus;
                }

                if (Feature == FEATURE_C_HUB_OVER_CURRENT)
                {
                    MPStatus = Packet->RH_ClearFeaturePortOvercurrentChange(FdoExtension->MiniPortExt,
                                                                            0);
                    RHStatus = USBPORT_MPStatusToRHStatus(MPStatus);
                    return RHStatus;
                }

                DPRINT1("USBPORT_RootHubClearFeature: unknown hub feature %x\n", Feature);
                return RHStatus;
            }

            switch (Feature)
            {
                case FEATURE_PORT_ENABLE:
                    MPStatus = Packet->RH_ClearFeaturePortEnable(FdoExtension->MiniPortExt,
                                                                 Port);
                    break;

                case FEATURE_PORT_SUSPEND:
                    MPStatus = Packet->RH_ClearFeaturePortSuspend(FdoExtension->MiniPortExt,
                                                                  Port);
                    break;

                case FEATURE_PORT_POWER:
                    MPStatus = Packet->RH_ClearFeaturePortPower(FdoExtension->MiniPortExt,
                                                                Port);
                    break;

                case FEATURE_C_PORT_CONNECTION:
                    MPStatus = Packet->RH_ClearFeaturePortConnectChange(FdoExtension->MiniPortExt,
                                                                        Port);
                    break;

                case FEATURE_C_PORT_ENABLE:
                     MPStatus = Packet->RH_ClearFeaturePortEnableChange(FdoExtension->MiniPortExt,
                                                                        Port);
                    break;

                case FEATURE_C_PORT_SUSPEND:
                    MPStatus = Packet->RH_ClearFeaturePortSuspendChange(FdoExtension->MiniPortExt,
                                                                        Port);
                    break;

                case FEATURE_C_PORT_OVER_CURRENT:
                    MPStatus = Packet->RH_ClearFeaturePortOvercurrentChange(FdoExtension->MiniPortExt,
                                                                            Port);
                    break;

                case FEATURE_C_PORT_RESET:
                    MPStatus = Packet->RH_ClearFeaturePortResetChange(FdoExtension->MiniPortExt,
                                                                      Port);
                    break;

                default:
                    DPRINT1("USBPORT_RootHubClassCommand: Not supported feature - %x\n",
                            Feature);
                    return RHStatus;
            }

            RHStatus = USBPORT_MPStatusToRHStatus(MPStatus);
            break;

        case USB_REQUEST_SET_FEATURE:
            if (SetupPacket->bmRequestType.Recipient != USBPORT_RECIPIENT_PORT)
            {
                return RHStatus;
            }

            Feature = SetupPacket->wValue.W;

            switch (Feature)
            {
                case FEATURE_PORT_ENABLE:
                    MPStatus = Packet->RH_SetFeaturePortEnable(FdoExtension->MiniPortExt,
                                                               Port);
                    break;

                case FEATURE_PORT_SUSPEND:
                    MPStatus = Packet->RH_SetFeaturePortSuspend(FdoExtension->MiniPortExt,
                                                                Port);
                    break;

                case FEATURE_PORT_RESET:
                    MPStatus = Packet->RH_SetFeaturePortReset(FdoExtension->MiniPortExt,
                                                              Port);
                    break;

                case FEATURE_PORT_POWER:
                    if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
                    {
                        MPStatus = USBPORT_RH_SetFeatureUSB2PortPower(FdoDevice, Port);
                    }
                    else
                    {
                        MPStatus = Packet->RH_SetFeaturePortPower(FdoExtension->MiniPortExt,
                                                                  Port);
                    }

                    break;

                default:
                    DPRINT1("USBPORT_RootHubClassCommand: Not supported feature - %x\n",
                            Feature);
                    return RHStatus;
            }

            RHStatus = USBPORT_MPStatusToRHStatus(MPStatus);
            break;

        case USB_REQUEST_GET_DESCRIPTOR:
            if (Buffer &&
                SetupPacket->bmRequestType.Dir == BMREQUEST_DEVICE_TO_HOST)
            {
                SIZE_T DescriptorLength;
                UCHAR DescriptorIndex;
                UCHAR DescriptorType;

                DescriptorIndex = SetupPacket->wValue.LowByte;
                DescriptorType = SetupPacket->wValue.HiByte;

                if (DescriptorIndex == 0 &&
                    (DescriptorType == PdoExtension->RootHubDescriptors->Descriptor.bDescriptorType ||
                     DescriptorType == USB_20_HUB_DESCRIPTOR_TYPE ||
                     DescriptorType == USB_30_HUB_DESCRIPTOR_TYPE))
                {
                    DescriptorLength = PdoExtension->RootHubDescriptors->Descriptor.bDescriptorLength;

                    if (*BufferLength < DescriptorLength)
                        DescriptorLength = *BufferLength;

                    RtlCopyMemory(Buffer,
                                  &PdoExtension->RootHubDescriptors->Descriptor,
                                  DescriptorLength);

                    *BufferLength = DescriptorLength;
                    RHStatus = RH_STATUS_SUCCESS;
                }
            }

            break;

        default:
            DPRINT1("USBPORT_RootHubClassCommand: Not supported USB request - %x\n",
                    SetupPacket->bRequest);
            //USB_REQUEST_SET_ADDRESS                   0x05
            //USB_REQUEST_SET_DESCRIPTOR                0x07
            //USB_REQUEST_GET_CONFIGURATION             0x08
            //USB_REQUEST_SET_CONFIGURATION             0x09
            //USB_REQUEST_GET_INTERFACE                 0x0A
            //USB_REQUEST_SET_INTERFACE                 0x0B
            //USB_REQUEST_SYNC_FRAME                    0x0C
            break;
    }

    return RHStatus;
}

RHSTATUS
NTAPI
USBPORT_RootHubStandardCommand(IN PDEVICE_OBJECT FdoDevice,
                               IN PUSB_DEFAULT_PIPE_SETUP_PACKET SetupPacket,
                               IN PVOID Buffer,
                               IN OUT PULONG TransferLength)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    SIZE_T Length;
    PVOID Descriptor;
    SIZE_T DescriptorLength;
    MPSTATUS MPStatus;
    RHSTATUS RHStatus = RH_STATUS_UNSUCCESSFUL;
    KIRQL OldIrql;

    DPRINT("USBPORT_RootHubStandardCommand: USB command - %x, TransferLength - %p\n",
           SetupPacket->bRequest,
           TransferLength);

    FdoExtension = FdoDevice->DeviceExtension;
    PdoExtension = USBPORT_GetRootHubExtension(FdoExtension);
    Packet = &FdoExtension->MiniPortInterface->Packet;

    switch (SetupPacket->bRequest)
    {
        case USB_REQUEST_GET_DESCRIPTOR:
            if (SetupPacket->wValue.LowByte ||
                !(SetupPacket->bmRequestType.Dir))
            {
                DPRINT1("USBPORT_RootHubStandardCommand: GET_DESCRIPTOR invalid params value=0x%04x dir=%u\n",
                        SetupPacket->wValue.W,
                        SetupPacket->bmRequestType.Dir);
                return RHStatus;
            }

            switch (SetupPacket->wValue.HiByte)
            {
                case USB_DEVICE_DESCRIPTOR_TYPE:
                    Descriptor = &PdoExtension->RootHubDescriptors->DeviceDescriptor;
                    DescriptorLength = sizeof(USB_DEVICE_DESCRIPTOR);
                    break;

                case USB_CONFIGURATION_DESCRIPTOR_TYPE:
                    Descriptor = &PdoExtension->RootHubDescriptors->ConfigDescriptor;
                    DescriptorLength = sizeof(USB_CONFIGURATION_DESCRIPTOR) +
                                       sizeof(USB_INTERFACE_DESCRIPTOR) +
                                       sizeof(USB_ENDPOINT_DESCRIPTOR);
                    break;

                default:
                    DPRINT1("USBPORT_RootHubStandardCommand: Not supported Descriptor Type - %x\n",
                            SetupPacket->wValue.HiByte);
                    return RHStatus;
            }

            if (!Descriptor)
            {
                return RHStatus;
            }

            USBPORT_RH_TRACE("USBPORT_RootHubStandardCommand: GET_DESCRIPTOR type=0x%x reqLen=%lu descLen=%lu\n",
                             SetupPacket->wValue.HiByte,
                             *TransferLength,
                             DescriptorLength);

            if (*TransferLength >= DescriptorLength)
                Length = DescriptorLength;
            else
                Length = *TransferLength;

            RtlCopyMemory(Buffer, Descriptor, Length);
            *TransferLength = Length;

            USBPORT_RH_TRACE("USBPORT_RootHubStandardCommand: descriptor copy len=%lu\n",
                             Length);

#if DBG
            if (Length && SetupPacket->wValue.HiByte == USB_CONFIGURATION_DESCRIPTOR_TYPE)
            {
                ULONG dumpLen = (Length < 64) ? Length : 64;
                ULONG ix;

                USBPORT_RH_TRACE("USBPORT_RootHubStandardCommand: config descriptor bytes (len=%lu)\n",
                                 Length);

                for (ix = 0; ix < dumpLen; ix++)
                {
                    USBPORT_RH_TRACE("  [%02lu] = %02x\n", ix, ((PUCHAR)Buffer)[ix]);
                }
            }
#endif

            RHStatus = RH_STATUS_SUCCESS;
            break;

        case USB_REQUEST_GET_STATUS:
            if (!SetupPacket->wValue.W &&
                 SetupPacket->wLength == sizeof(USHORT) &&
                 !SetupPacket->wIndex.W &&
                 SetupPacket->bmRequestType.Dir == BMREQUEST_DEVICE_TO_HOST)
            {
                KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

                MPStatus = Packet->RH_GetStatus(FdoExtension->MiniPortExt,
                                                Buffer);

                KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

                *TransferLength = sizeof(USHORT);
                RHStatus = USBPORT_MPStatusToRHStatus(MPStatus);
            }

            break;

        case USB_REQUEST_GET_CONFIGURATION:
            if (SetupPacket->wValue.W ||
                SetupPacket->wIndex.W ||
                SetupPacket->wLength != 1 ||
                SetupPacket->bmRequestType.Dir == BMREQUEST_HOST_TO_DEVICE)
            {
                DPRINT1("USBPORT_RootHubStandardCommand: GET_CONFIGURATION invalid params value=%u index=%u length=%u dir=%u\n",
                        SetupPacket->wValue.W,
                        SetupPacket->wIndex.W,
                        SetupPacket->wLength,
                        SetupPacket->bmRequestType.Dir);
                return RHStatus;
            }

            USBPORT_RH_TRACE("USBPORT_RootHubStandardCommand: GET_CONFIGURATION current=%u\n",
                             PdoExtension->ConfigurationValue);

            Length = 0;

            if (*TransferLength >= 1)
            {
                Length = 1;
                RtlCopyMemory(Buffer, &PdoExtension->ConfigurationValue, Length);
            }

            *TransferLength = Length;

            USBPORT_RH_TRACE("USBPORT_RootHubStandardCommand: returning config value len=%lu\n",
                             Length);

            RHStatus = RH_STATUS_SUCCESS;
            break;

        case USB_REQUEST_SET_CONFIGURATION:
            if (!SetupPacket->wIndex.W &&
                !SetupPacket->wLength &&
                !(SetupPacket->bmRequestType.Dir == BMREQUEST_DEVICE_TO_HOST))
            {
                if (SetupPacket->wValue.W == 0 ||
                    SetupPacket->wValue.W ==
                        PdoExtension->RootHubDescriptors->ConfigDescriptor.bConfigurationValue)
                {
                  PdoExtension->ConfigurationValue = SetupPacket->wValue.LowByte;
                  RHStatus = RH_STATUS_SUCCESS;
                }
            }

            break;

        case USB_REQUEST_SET_ADDRESS:
            if (!SetupPacket->wIndex.W &&
                !SetupPacket->wLength &&
                !(SetupPacket->bmRequestType.Dir))
            {
                PdoExtension->DeviceHandle.DeviceAddress = SetupPacket->wValue.LowByte;
                RHStatus = RH_STATUS_SUCCESS;
                break;
            }

            break;

        default:
            DPRINT1("USBPORT_RootHubStandardCommand: Not supported USB request - %x\n",
                    SetupPacket->bRequest);
            //USB_REQUEST_CLEAR_FEATURE                 0x01
            //USB_REQUEST_SET_FEATURE                   0x03
            //USB_REQUEST_SET_DESCRIPTOR                0x07
            //USB_REQUEST_GET_INTERFACE                 0x0A
            //USB_REQUEST_SET_INTERFACE                 0x0B
            //USB_REQUEST_SYNC_FRAME                    0x0C
            break;
    }

    return RHStatus;
}

RHSTATUS
NTAPI
USBPORT_RootHubEndpoint0(IN PUSBPORT_TRANSFER Transfer)
{
    PDEVICE_OBJECT FdoDevice;
    ULONG TransferLength;
    PVOID Buffer;
    PURB Urb;
    PUSB_DEFAULT_PIPE_SETUP_PACKET SetupPacket;
    UCHAR Type;
    RHSTATUS RHStatus;

    DPRINT("USBPORT_RootHubEndpoint0: Transfer - %p\n", Transfer);

    TransferLength = Transfer->TransferParameters.TransferBufferLength;
    Urb = Transfer->Urb;
    FdoDevice = Transfer->Endpoint->FdoDevice;

    if (TransferLength > 0)
        Buffer = Urb->UrbControlTransfer.TransferBufferMDL->MappedSystemVa;
    else
        Buffer = NULL;

    SetupPacket = (PUSB_DEFAULT_PIPE_SETUP_PACKET)Urb->UrbControlTransfer.SetupPacket;

    Type = SetupPacket->bmRequestType.Type;

    if (Type == BMREQUEST_STANDARD)
    {
        RHStatus = USBPORT_RootHubStandardCommand(FdoDevice,
                                                  SetupPacket,
                                                  Buffer,
                                                  &TransferLength);
    }
    else if (Type == BMREQUEST_CLASS)
    {
        RHStatus = USBPORT_RootHubClassCommand(FdoDevice,
                                               SetupPacket,
                                               Buffer,
                                               &TransferLength);
    }
    else
    {
        return RH_STATUS_UNSUCCESSFUL;
    }

    if (RHStatus == RH_STATUS_SUCCESS)
    {
        SIZE_T BytesToCopy;

        Transfer->CompletedTransferLen = TransferLength;

        if ((Transfer->Flags & TRANSFER_FLAG_BOUNCE) &&
            Transfer->BounceBuffer &&
            Transfer->BounceOriginalVa &&
            TransferLength)
        {
            BytesToCopy = TransferLength;

            if (BytesToCopy > Transfer->BounceBufferLength)
                BytesToCopy = Transfer->BounceBufferLength;

            if (BytesToCopy)
            {
                RtlCopyMemory(Transfer->BounceOriginalVa,
                              (PVOID)Transfer->BounceBuffer->VirtualAddress,
                              BytesToCopy);
                USBPORT_RH_TRACE("USBPORT_RootHubEndpoint0: copied %Iu bytes from bounce buffer to %p\n",
                                 BytesToCopy,
                                 Transfer->BounceOriginalVa);
            }
        }
    }

    return RHStatus;
}

RHSTATUS
NTAPI
USBPORT_RootHubSCE(IN PUSBPORT_TRANSFER Transfer)
{
    PUSBPORT_ENDPOINT Endpoint;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    ULONG TransferLength;
    USB_PORT_STATUS_AND_CHANGE PortStatus;
    USB_HUB_STATUS_AND_CHANGE HubStatus;
    PVOID Buffer;
    PUCHAR AddressBitMap;
    ULONG Port;
    PURB Urb;
    RHSTATUS RHStatus = RH_STATUS_NO_CHANGES;
    PUSB_HUB_DESCRIPTOR HubDescriptor;
    UCHAR NumberOfPorts;

    DPRINT("USBPORT_RootHubSCE: Transfer - %p\n", Transfer);

    Endpoint = Transfer->Endpoint;

    FdoExtension = Endpoint->FdoDevice->DeviceExtension;
    PdoExtension = USBPORT_GetRootHubExtension(FdoExtension);
    Packet = &FdoExtension->MiniPortInterface->Packet;

    HubDescriptor = &PdoExtension->RootHubDescriptors->Descriptor;
    NumberOfPorts = HubDescriptor->bNumberOfPorts;

    PortStatus.AsUlong32 = 0;
    HubStatus.AsUlong32 = 0;

    Urb = Transfer->Urb;
    TransferLength = Transfer->TransferParameters.TransferBufferLength;

    if (TransferLength)
    {
        Buffer = Urb->UrbControlTransfer.TransferBufferMDL->MappedSystemVa;
    }
    else
    {
        Buffer = NULL;
    }

    /* Check parameters */

    if (!Buffer)
    {
        /* Not valid parameter */
        DPRINT1("USBPORT_RootHubSCE: Error! Buffer is NULL\n");
        return RH_STATUS_UNSUCCESSFUL;
    }

    /* We need one bit for the hub and one bit per port. */
    {
        ULONG RequiredBytes;

        RequiredBytes = (((ULONG)NumberOfPorts + 1) + 7) >> 3;

        if (TransferLength < RequiredBytes)
        {
            /* Not valid parameters */
            DPRINT1("USBPORT_RootHubSCE: Error! TransferLength - %x, NumberOfPorts - %x (RequiredBytes=%lx)\n",
                    TransferLength,
                    NumberOfPorts,
                    RequiredBytes);

            return RH_STATUS_UNSUCCESSFUL;
        }
    }

    RtlZeroMemory(Buffer, TransferLength);

    AddressBitMap = Buffer;

    /* Scan all the ports for changes */
    for (Port = 1; Port <= NumberOfPorts; Port++)
    {
        DPRINT_CORE("USBPORT_RootHubSCE: Port - %p\n", Port);

        /* Request the port status from miniport */
        if (Packet->RH_GetPortStatus(FdoExtension->MiniPortExt,
                                     Port,
                                     &PortStatus))
        {
            /* Miniport returned an error */
            DPRINT1("USBPORT_RootHubSCE: RH_GetPortStatus failed\n");
            return RH_STATUS_UNSUCCESSFUL;
        }

        if (PortStatus.PortChange.Usb20PortChange.ConnectStatusChange ||
            PortStatus.PortChange.Usb20PortChange.PortEnableDisableChange ||
            PortStatus.PortChange.Usb20PortChange.SuspendChange ||
            PortStatus.PortChange.Usb20PortChange.OverCurrentIndicatorChange ||
            PortStatus.PortChange.Usb20PortChange.ResetChange ||
            PortStatus.PortChange.Usb30PortChange.ConnectStatusChange ||
            PortStatus.PortChange.Usb30PortChange.OverCurrentIndicatorChange ||
            PortStatus.PortChange.Usb30PortChange.ResetChange ||
            PortStatus.PortChange.Usb30PortChange.BHResetChange ||
            PortStatus.PortChange.Usb30PortChange.PortLinkStateChange ||
            PortStatus.PortChange.Usb30PortChange.PortConfigErrorChange)
        {
            /*
             * Encode port change bits per USB 2.0 spec:
             *  - bit 0 in the change bitmap is the HUB change.
             *  - bit N (1..bNumberOfPorts) corresponds to PORT N.
             *
             * Our usbhub logic interprets the bitmap as:
             *   bit 0  -> hub change
             *   bit N  -> port N (1..bNumberOfPorts)
             *
             * The map is byte-addressed, eight ports per byte.
             * Use 'Port' as the bit index so that port 1 maps to
             * bit 1, port 2 to bit 2, etc.
             */
            {
                ULONG BitIndex = Port;
                ULONG ByteIndex = BitIndex >> 3;
                UCHAR BitMask = (UCHAR)(1u << (BitIndex & 7));

                AddressBitMap[ByteIndex] |= BitMask;
            }
            RHStatus = RH_STATUS_SUCCESS;
        }
    }

    /* Request the hub status from miniport */
    if (!Packet->RH_GetHubStatus(FdoExtension->MiniPortExt, &HubStatus))
    {
        if (HubStatus.HubChange.LocalPowerChange == 1 ||
            HubStatus.HubChange.OverCurrentChange == 1)
        {
            /* At the hub status there is a change */
            AddressBitMap[0] |= 1;
            RHStatus = RH_STATUS_SUCCESS;
        }

        if (RHStatus == RH_STATUS_SUCCESS)
        {
            /* Done */
            Urb->UrbControlTransfer.TransferBufferLength = TransferLength;
            return RH_STATUS_SUCCESS;
        }

        if (RHStatus == RH_STATUS_NO_CHANGES)
        {
            /* No changes. Enable IRQs for miniport root hub */
            Packet->RH_EnableIrq(FdoExtension->MiniPortExt);
        }

        return RHStatus;
    }

    /* Miniport returned an error */
    DPRINT1("USBPORT_RootHubSCE: RH_GetHubStatus failed\n");
    return RH_STATUS_UNSUCCESSFUL;
}

VOID
NTAPI
USBPORT_RootHubEndpointWorker(IN PUSBPORT_ENDPOINT Endpoint)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PUSBPORT_TRANSFER Transfer;
    RHSTATUS RHStatus;
    USBD_STATUS USBDStatus;
    KIRQL OldIrql;

    DPRINT_CORE("USBPORT_RootHubEndpointWorker: Endpoint - %p\n", Endpoint);

    FdoDevice = Endpoint->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
    if (!(FdoExtension->Flags & USBPORT_FLAG_HC_SUSPEND))
    {
        Packet->CheckController(FdoExtension->MiniPortExt);
    }
    KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

    KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &Endpoint->EndpointOldIrql);

    Transfer = CONTAINING_RECORD(Endpoint->TransferList.Flink,
                                 USBPORT_TRANSFER,
                                 TransferLink);

    if (IsListEmpty(&Endpoint->TransferList) ||
        Endpoint->TransferList.Flink == NULL ||
        !Transfer)
    {
        if (Endpoint->StateLast == USBPORT_ENDPOINT_REMOVE)
        {
            ExInterlockedInsertTailList(&FdoExtension->EndpointClosedList,
                                        &Endpoint->CloseLink,
                                        &FdoExtension->EndpointClosedSpinLock);
       }

        KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);

        USBPORT_FlushCancelList(Endpoint);
        return;
    }

    if (Transfer->Flags & (TRANSFER_FLAG_ABORTED | TRANSFER_FLAG_CANCELED))
    {
        RemoveEntryList(&Transfer->TransferLink);
        InsertTailList(&Endpoint->CancelList, &Transfer->TransferLink);

        KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);
        USBPORT_FlushCancelList(Endpoint);
        return;
    }

    if (Transfer->Flags & TRANSFER_FLAG_COMPLETED)
    {
        KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);

        /* Completion already queued: drain DoneTransferList before reprocessing. */
        USBPORT_FlushDoneTransfers(FdoDevice);
        USBPORT_FlushCancelList(Endpoint);
        return;
    }

    KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);

    if (Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_CONTROL)
        RHStatus = USBPORT_RootHubEndpoint0(Transfer);
    else
        RHStatus = USBPORT_RootHubSCE(Transfer);

    if (RHStatus != RH_STATUS_NO_CHANGES)
    {
        if (RHStatus == RH_STATUS_SUCCESS)
            USBDStatus = USBD_STATUS_SUCCESS;
        else
            USBDStatus = USBD_STATUS_STALL_PID;

        KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &Endpoint->EndpointOldIrql);
        USBPORT_QueueDoneTransfer(Transfer, USBDStatus, TRUE);
        KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);

        /* Root hub control transfers are synchronous; flush done queue immediately. */
        USBPORT_FlushDoneTransfers(FdoDevice);
        USBPORT_FlushCancelList(Endpoint);
        return;
    }

    USBPORT_FlushCancelList(Endpoint);
}

NTSTATUS
NTAPI
USBPORT_RootHubCreateDevice(IN PDEVICE_OBJECT FdoDevice,
                            IN PDEVICE_OBJECT PdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PUSBPORT_DEVICE_HANDLE DeviceHandle;
    USBPORT_ROOT_HUB_DATA RootHubData;
    ULONG NumMaskByte;
    ULONG DescriptorsLength;
    PUSBPORT_RH_DESCRIPTORS Descriptors;
    PUSB_DEVICE_DESCRIPTOR RH_DeviceDescriptor;
    PUSB_CONFIGURATION_DESCRIPTOR RH_ConfigurationDescriptor;
    PUSB_INTERFACE_DESCRIPTOR RH_InterfaceDescriptor;
    PUSB_ENDPOINT_DESCRIPTOR RH_EndPointDescriptor;
    PUSB_HUB_DESCRIPTOR RH_HubDescriptor;
    ULONG ix;
    PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor;
    NTSTATUS Status;

    DPRINT("USBPORT_RootHubCreateDevice: FdoDevice - %p, PdoDevice - %p\n",
           FdoDevice,
           PdoDevice);

    FdoExtension = FdoDevice->DeviceExtension;
    PdoExtension = PdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    DeviceHandle = &PdoExtension->DeviceHandle;

    RtlZeroMemory(DeviceHandle, sizeof(USBPORT_DEVICE_HANDLE));
    InitializeListHead(&DeviceHandle->PipeHandleList);
    InitializeListHead(&DeviceHandle->TtList);

    USBPORT_AddDeviceHandle(FdoDevice, DeviceHandle);

#if DBG
    if (!USBPORT_ValidateDeviceHandle(FdoDevice, DeviceHandle))
    {
        USBPORT_RH_TRACE("USBPORT_RootHubCreateDevice: failed to validate root hub handle %p\n",
                         DeviceHandle);
        USBPORT_DumpDeviceHandleList(FdoExtension);
    }
    else
    {
        USBPORT_RH_TRACE("USBPORT_RootHubCreateDevice: registered root hub handle %p\n",
                         DeviceHandle);
        USBPORT_DumpDeviceHandleList(FdoExtension);
    }
#endif

    DeviceHandle->IsRootHub = TRUE;
    DeviceHandle->DeviceSpeed = UsbFullSpeed;
    DeviceHandle->Flags = DEVICE_HANDLE_FLAG_ROOTHUB;

    RtlZeroMemory(&RootHubData, sizeof(RootHubData));

    Packet->RH_GetRootHubData(FdoExtension->MiniPortExt, &RootHubData);

    if (RootHubData.NumberOfPorts == 0)
    {
#if DBG
        DPRINT1("USBPORT_RootHubCreateDevice: miniport reported 0 root hub ports, failing root hub start\n");
#endif
        return STATUS_UNSUCCESSFUL;
    }

    NumMaskByte = (RootHubData.NumberOfPorts - 1) / 8 + 1;

    DescriptorsLength = sizeof(USB_DEVICE_DESCRIPTOR) +
                        sizeof(USB_CONFIGURATION_DESCRIPTOR) +
                        sizeof(USB_INTERFACE_DESCRIPTOR) +
                        sizeof(USB_ENDPOINT_DESCRIPTOR) +
                        (sizeof(USB_HUB_DESCRIPTOR) + 2 * NumMaskByte);

    Descriptors = ExAllocatePoolWithTag(NonPagedPool,
                                        DescriptorsLength,
                                        USB_PORT_TAG);

    if (Descriptors)
    {
        RtlZeroMemory(Descriptors, DescriptorsLength);

        PdoExtension->RootHubDescriptors = Descriptors;

        RH_DeviceDescriptor = &PdoExtension->RootHubDescriptors->DeviceDescriptor;

        RH_DeviceDescriptor->bLength = sizeof(USB_DEVICE_DESCRIPTOR);
        RH_DeviceDescriptor->bDescriptorType = USB_DEVICE_DESCRIPTOR_TYPE;
        RH_DeviceDescriptor->bcdUSB = 0x100;
        RH_DeviceDescriptor->bDeviceClass = USB_DEVICE_CLASS_HUB;
        RH_DeviceDescriptor->bDeviceSubClass = 0x01;
        RH_DeviceDescriptor->bDeviceProtocol = 0x00;
        RH_DeviceDescriptor->bMaxPacketSize0 = 0x08;
        RH_DeviceDescriptor->idVendor = FdoExtension->VendorID;
        RH_DeviceDescriptor->idProduct = FdoExtension->DeviceID;
        RH_DeviceDescriptor->bcdDevice = FdoExtension->RevisionID;
        RH_DeviceDescriptor->iManufacturer = 0x00;
        RH_DeviceDescriptor->iProduct = 0x00;
        RH_DeviceDescriptor->iSerialNumber = 0x00;
        RH_DeviceDescriptor->bNumConfigurations = 0x01;

        RH_ConfigurationDescriptor = &PdoExtension->RootHubDescriptors->ConfigDescriptor;

        RH_ConfigurationDescriptor->bLength = sizeof(USB_CONFIGURATION_DESCRIPTOR);
        RH_ConfigurationDescriptor->bDescriptorType = USB_CONFIGURATION_DESCRIPTOR_TYPE;

        RH_ConfigurationDescriptor->wTotalLength = sizeof(USB_CONFIGURATION_DESCRIPTOR) +
                                                   sizeof(USB_INTERFACE_DESCRIPTOR) +
                                                   sizeof(USB_ENDPOINT_DESCRIPTOR);

        RH_ConfigurationDescriptor->bNumInterfaces = 0x01;
        RH_ConfigurationDescriptor->bConfigurationValue = 0x01;
        RH_ConfigurationDescriptor->iConfiguration = 0x00;
        RH_ConfigurationDescriptor->bmAttributes = USB_CONFIG_SELF_POWERED;
        RH_ConfigurationDescriptor->MaxPower = 0x00;

        PdoExtension->ConfigurationValue = RH_ConfigurationDescriptor->bConfigurationValue;

        RH_InterfaceDescriptor = &PdoExtension->RootHubDescriptors->InterfaceDescriptor;

        RH_InterfaceDescriptor->bLength = sizeof(USB_INTERFACE_DESCRIPTOR);
        RH_InterfaceDescriptor->bDescriptorType = USB_INTERFACE_DESCRIPTOR_TYPE;
        RH_InterfaceDescriptor->bInterfaceNumber = 0x00;
        RH_InterfaceDescriptor->bAlternateSetting = 0x00;
        RH_InterfaceDescriptor->bNumEndpoints = 0x01;
        RH_InterfaceDescriptor->bInterfaceClass = USB_DEVICE_CLASS_HUB;
        RH_InterfaceDescriptor->bInterfaceSubClass = 0x01;
        RH_InterfaceDescriptor->bInterfaceProtocol = 0x00;
        RH_InterfaceDescriptor->iInterface = 0x00;

        RH_EndPointDescriptor = &PdoExtension->RootHubDescriptors->EndPointDescriptor;

        RH_EndPointDescriptor->bLength = sizeof(USB_ENDPOINT_DESCRIPTOR);
        RH_EndPointDescriptor->bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
        RH_EndPointDescriptor->bEndpointAddress = 0x81;
        RH_EndPointDescriptor->bmAttributes = USB_ENDPOINT_TYPE_INTERRUPT; // SCE endpoint
        RH_EndPointDescriptor->wMaxPacketSize = 0x0008;
        RH_EndPointDescriptor->bInterval = 0x0C; // 12 msec

        RH_HubDescriptor = &PdoExtension->RootHubDescriptors->Descriptor;

        RH_HubDescriptor->bDescriptorLength = FIELD_OFFSET(USB_HUB_DESCRIPTOR, bRemoveAndPowerMask) + 2 * NumMaskByte;

        if (Packet->MiniPortVersion == USB_MINIPORT_VERSION_OHCI ||
            Packet->MiniPortVersion == USB_MINIPORT_VERSION_UHCI ||
            Packet->MiniPortVersion == USB_MINIPORT_VERSION_EHCI)
        {
            RH_HubDescriptor->bDescriptorType = USB_20_HUB_DESCRIPTOR_TYPE;
        }
        else if (Packet->MiniPortVersion == USB_MINIPORT_VERSION_XHCI)
        {
            RH_HubDescriptor->bDescriptorType = USB_30_HUB_DESCRIPTOR_TYPE;
        }
        else
        {
            DPRINT1("USBPORT_RootHubCreateDevice: Unknown MiniPortVersion - %x\n",
                    Packet->MiniPortVersion);
        }

        RH_HubDescriptor->bNumberOfPorts = RootHubData.NumberOfPorts;
        RH_HubDescriptor->wHubCharacteristics = RootHubData.HubCharacteristics.AsUSHORT;
        RH_HubDescriptor->bPowerOnToPowerGood = RootHubData.PowerOnToPowerGood;
        RH_HubDescriptor->bHubControlCurrent = RootHubData.HubControlCurrent;

        for (ix = 0; ix < NumMaskByte; ix += 2)
        {
            RH_HubDescriptor->bRemoveAndPowerMask[ix] = 0;
            RH_HubDescriptor->bRemoveAndPowerMask[ix + 1] = -1;
        }

        EndpointDescriptor = &DeviceHandle->PipeHandle.EndpointDescriptor;

        EndpointDescriptor->bLength = sizeof(USB_ENDPOINT_DESCRIPTOR);
        EndpointDescriptor->bDescriptorType = USB_ENDPOINT_DESCRIPTOR_TYPE;
        EndpointDescriptor->bEndpointAddress = 0x00;
        EndpointDescriptor->bmAttributes = USB_ENDPOINT_TYPE_CONTROL;
        EndpointDescriptor->wMaxPacketSize = 0x0040;
        EndpointDescriptor->bInterval = 0x00;

        Status = USBPORT_OpenPipe(FdoDevice,
                                  DeviceHandle,
                                  &DeviceHandle->PipeHandle,
                                  NULL);
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    return Status;
}

ULONG
NTAPI
USBPORT_InvalidateRootHub(PVOID MiniPortExtension)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PDEVICE_OBJECT PdoDevice;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSBPORT_ENDPOINT Endpoint = NULL;
    PUSBPORT_REGISTRATION_PACKET Packet;

    DPRINT("USBPORT_InvalidateRootHub ... \n");

    FdoExtension = (PUSBPORT_DEVICE_EXTENSION)((ULONG_PTR)MiniPortExtension -
                                               sizeof(USBPORT_DEVICE_EXTENSION));

    FdoDevice = FdoExtension->CommonExtension.SelfDevice;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    if (FdoExtension->Flags & USBPORT_FLAG_HC_SUSPEND &&
        FdoExtension->Flags & USBPORT_FLAG_HC_WAKE_SUPPORT &&
        FdoExtension->MiniPortFlags & USBPORT_MPFLAG_SUSPENDED &&
        FdoExtension->TimerFlags & USBPORT_TMFLAG_WAKE)
    {
        USBPORT_HcQueueWakeDpc(FdoDevice);
        return 0;
    }

    Packet->RH_DisableIrq(FdoExtension->MiniPortExt);

    PdoDevice = FdoExtension->RootHubPdo;
    PdoExtension = USBPORT_GetRootHubExtension(FdoExtension);

    if (PdoDevice && !PdoExtension)
    {
        DPRINT1("USBPORT_InvalidateRootHub: RootHubPdo=%p has NULL extension\n",
                PdoDevice);
        /* Keep port-change interrupts armed even if PDO is not ready yet */
        Packet->RH_EnableIrq(FdoExtension->MiniPortExt);
        return 0;
    }

    if (PdoExtension && (LONG_PTR)PdoExtension > 0)
    {
        DPRINT1("USBPORT_InvalidateRootHub: invalid RootHubExt=%p (pdo=%p)\n",
                PdoExtension,
                PdoDevice);
        Packet->RH_EnableIrq(FdoExtension->MiniPortExt);
        return 0;
    }

    if (PdoExtension)
    {
        /* If the PDO device handle is no longer valid, drop any cached endpoint */
        if (!USBPORT_ValidateDeviceHandle(FdoDevice, &PdoExtension->DeviceHandle))
        {
            if (PdoExtension->Endpoint)
            {
                DPRINT1("USBPORT_InvalidateRootHub: device handle invalid, dropping endpoint %p\n",
                        PdoExtension->Endpoint);
                PdoExtension->Endpoint = NULL;
            }

            USBPORT_DumpDeviceHandleList(FdoExtension);
            Packet->RH_EnableIrq(FdoExtension->MiniPortExt);
            return 0;
        }

        Endpoint = PdoExtension->Endpoint;

        if (!Endpoint)
        {
            /* No SCE pipe yet; keep port-change interrupts armed */
            DPRINT1("USBPORT_InvalidateRootHub: no SCE endpoint registered\n");
            Packet->RH_EnableIrq(FdoExtension->MiniPortExt);
            return 0;
        }

        if (Endpoint && Endpoint->FdoDevice == FdoDevice &&
            USBPORT_IsEndpointOnList(FdoExtension, Endpoint))
        {
            USBPORT_InvalidateEndpointHandler(FdoDevice,
                                              Endpoint,
                                              INVALIDATE_ENDPOINT_WORKER_THREAD);
            /*
             * Re-enable IRQs after queuing the endpoint worker. The worker will
             * process the current port changes. If new port changes occur while
             * the worker is processing, they will trigger new interrupts that
             * call InvalidateRootHub again. Without this, RhIrqEnabled stays
             * FALSE after processing changes, causing subsequent port events
             * to be deferred indefinitely (the re-plug detection bug).
             */
            Packet->RH_EnableIrq(FdoExtension->MiniPortExt);
        }
        else if (Endpoint)
        {
            DPRINT1("USBPORT_InvalidateRootHub: ignoring stale root hub endpoint %p (pdo=%p fdo=%p)\n",
                    Endpoint,
                    PdoDevice,
                    FdoDevice);
            PdoExtension->Endpoint = NULL;
            Packet->RH_EnableIrq(FdoExtension->MiniPortExt);
        }
    }
    else
    {
        /* No PDO yet, keep interrupts unmasked so we can catch the next change */
        Packet->RH_EnableIrq(FdoExtension->MiniPortExt);
    }

    return 0;
}

VOID
NTAPI
USBPORT_RootHubPowerAndChirpAllCcPorts(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    USBPORT_ROOT_HUB_DATA RootHubData;
    ULONG Port;
    PDEVICE_RELATIONS CompanionControllersList;
    PUSBPORT_DEVICE_EXTENSION CompanionFdoExtension;
    PUSBPORT_REGISTRATION_PACKET CompanionPacket;
    ULONG CompanionPorts;
    ULONG NumController;
    PDEVICE_OBJECT * Entry;
    ULONG NumPorts;

    DPRINT("USBPORT_RootHub_PowerAndChirpAllCcPorts: FdoDevice - %p\n",
           FdoDevice);

    FdoExtension = FdoDevice->DeviceExtension;

    Packet = &FdoExtension->MiniPortInterface->Packet;

    RtlZeroMemory(&RootHubData, sizeof(RootHubData));

    Packet->RH_GetRootHubData(FdoExtension->MiniPortExt,
                              &RootHubData);

    NumPorts = RootHubData.NumberOfPorts;

    for (Port = 1; Port <= NumPorts; ++Port)
    {
        Packet->RH_SetFeaturePortPower(FdoExtension->MiniPortExt, Port);
    }

    USBPORT_Wait(FdoDevice, 10);

    CompanionControllersList = USBPORT_FindCompanionControllers(FdoDevice,
                                                                FALSE,
                                                                TRUE);

    if (CompanionControllersList)
    {
        Entry = &CompanionControllersList->Objects[0];

        for (NumController = 0;
             NumController < CompanionControllersList->Count;
             NumController++)
        {
            CompanionPacket = &FdoExtension->MiniPortInterface->Packet;

            CompanionFdoExtension = (*Entry)->DeviceExtension;

            CompanionPacket->RH_GetRootHubData(CompanionFdoExtension->MiniPortExt,
                                               &RootHubData);

            CompanionPorts = RootHubData.NumberOfPorts;

            for (Port = 1; Port <= CompanionPorts; ++Port)
            {
                CompanionPacket->RH_SetFeaturePortPower(CompanionFdoExtension->MiniPortExt,
                                                        Port);
            }

            ++Entry;
        }

        ExFreePoolWithTag(CompanionControllersList, USB_PORT_TAG);
    }

    USBPORT_Wait(FdoDevice, 100);

    for (Port = 1; Port <= NumPorts; ++Port)
    {
        if (FdoExtension->MiniPortInterface->Version < 200)
        {
            break;
        }

        InterlockedIncrement((PLONG)&FdoExtension->ChirpRootPortLock);
        Packet->RH_ChirpRootPort(FdoExtension->MiniPortExt, Port);
        InterlockedDecrement((PLONG)&FdoExtension->ChirpRootPortLock);
    }
}
