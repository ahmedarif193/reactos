/*
 * PROJECT:     ReactOS USB RNDIS Network Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     USB communication layer for RNDIS
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This file handles USB descriptor parsing, configuration selection,
 * and USB transfer operations for RNDIS protocol messages.
 */

#include "usbrndis.h"

#define NDEBUG
#include <debug.h>

/* External helper functions from usbrndis.c */
extern PVOID RndisAllocateMemory(IN POOL_TYPE PoolType, IN SIZE_T Size);
extern VOID RndisFreeMemory(IN PVOID Buffer);
extern NTSTATUS RndisSyncUrbRequest(IN PDEVICE_OBJECT DeviceObject, IN PURB Urb);
extern VOID RndisDecrementPendingIo(IN PRNDIS_ADAPTER Adapter);

/* Forward declarations for completion routines */
static IO_COMPLETION_ROUTINE RndisRxComplete;
static IO_COMPLETION_ROUTINE RndisTxComplete;

/* Forward declarations for DPC routines */
static VOID NTAPI RndisRxResubmitDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
static VOID NTAPI RndisRxBackoffDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);
static VOID NTAPI RndisRxDelayDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);

/* Forward declaration for alternate setting selection */
static NTSTATUS RndisUsbSelectAlternate(IN PRNDIS_ADAPTER Adapter, IN UCHAR InterfaceNumber, IN UCHAR AlternateSetting);

/*
 * RndisUsbGetDescriptor
 *
 * Retrieve a USB descriptor from the device
 */
static
NTSTATUS
RndisUsbGetDescriptor(
    IN PRNDIS_ADAPTER Adapter,
    IN UCHAR DescriptorType,
    IN UCHAR DescriptorIndex,
    IN USHORT LanguageId,
    OUT PVOID *Descriptor,
    IN OUT PULONG DescriptorLength)
{
    PURB Urb;
    NTSTATUS Status;
    PVOID Buffer;

    /* Allocate buffer for descriptor */
    Buffer = RndisAllocateMemory(NonPagedPool, *DescriptorLength);
    if (!Buffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Allocate URB */
    Urb = RndisAllocateMemory(NonPagedPool, sizeof(URB));
    if (!Urb)
    {
        RndisFreeMemory(Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Build descriptor request */
    UsbBuildGetDescriptorRequest(
        Urb,
        sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
        DescriptorType,
        DescriptorIndex,
        LanguageId,
        Buffer,
        NULL,
        *DescriptorLength,
        NULL);

    /* Submit request */
    Status = RndisSyncUrbRequest(Adapter->LowerDeviceObject, Urb);

    if (NT_SUCCESS(Status))
    {
        *Descriptor = Buffer;
        *DescriptorLength = Urb->UrbControlDescriptorRequest.TransferBufferLength;
    }
    else
    {
        RndisFreeMemory(Buffer);
        *Descriptor = NULL;
    }

    RndisFreeMemory(Urb);
    return Status;
}

/*
 * RndisUsbGetDescriptors
 *
 * Get device and configuration descriptors
 */
NTSTATUS
RndisUsbGetDescriptors(
    IN PRNDIS_ADAPTER Adapter)
{
    NTSTATUS Status;
    ULONG DescriptorLength;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigDesc;

    DPRINT("USBRNDIS: Getting USB descriptors\n");

    /* Get device descriptor */
    DescriptorLength = sizeof(USB_DEVICE_DESCRIPTOR);
    Status = RndisUsbGetDescriptor(
        Adapter,
        USB_DEVICE_DESCRIPTOR_TYPE,
        0,
        0,
        (PVOID*)&Adapter->DeviceDescriptor,
        &DescriptorLength);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: Failed to get device descriptor (0x%08X)\n", Status);
        return Status;
    }

    DPRINT("USBRNDIS: Device: VID=%04X PID=%04X Class=%02X SubClass=%02X Protocol=%02X\n",
           Adapter->DeviceDescriptor->idVendor,
           Adapter->DeviceDescriptor->idProduct,
           Adapter->DeviceDescriptor->bDeviceClass,
           Adapter->DeviceDescriptor->bDeviceSubClass,
           Adapter->DeviceDescriptor->bDeviceProtocol);

    /* Get configuration descriptor header first */
    DescriptorLength = sizeof(USB_CONFIGURATION_DESCRIPTOR);
    Status = RndisUsbGetDescriptor(
        Adapter,
        USB_CONFIGURATION_DESCRIPTOR_TYPE,
        0,
        0,
        (PVOID*)&ConfigDesc,
        &DescriptorLength);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: Failed to get config descriptor header (0x%08X)\n", Status);
        return Status;
    }

    /* Get total configuration descriptor length */
    DescriptorLength = ConfigDesc->wTotalLength;
    RndisFreeMemory(ConfigDesc);

    /* Get full configuration descriptor */
    Status = RndisUsbGetDescriptor(
        Adapter,
        USB_CONFIGURATION_DESCRIPTOR_TYPE,
        0,
        0,
        (PVOID*)&Adapter->ConfigurationDescriptor,
        &DescriptorLength);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: Failed to get full config descriptor (0x%08X)\n", Status);
        return Status;
    }

    DPRINT("USBRNDIS: Configuration: TotalLength=%u NumInterfaces=%u\n",
           Adapter->ConfigurationDescriptor->wTotalLength,
           Adapter->ConfigurationDescriptor->bNumInterfaces);

    return STATUS_SUCCESS;
}

/*
 * RndisUsbFindEndpoints
 *
 * Find bulk IN, bulk OUT, and interrupt endpoints in the interface
 */
static
NTSTATUS
RndisUsbFindEndpoints(
    IN PRNDIS_ADAPTER Adapter,
    IN PUSB_INTERFACE_DESCRIPTOR InterfaceDesc,
    IN PUCHAR DescEnd,
    IN BOOLEAN IsDataInterface)
{
    PUCHAR CurrentDesc;
    PUSB_ENDPOINT_DESCRIPTOR EndpointDesc;
    UCHAR EndpointsFound = 0;

    CurrentDesc = (PUCHAR)InterfaceDesc + InterfaceDesc->bLength;

    /* Scan for endpoint descriptors */
    while (CurrentDesc < DescEnd)
    {
        if (CurrentDesc[1] == USB_ENDPOINT_DESCRIPTOR_TYPE)
        {
            EndpointDesc = (PUSB_ENDPOINT_DESCRIPTOR)CurrentDesc;

            DPRINT("USBRNDIS: Found endpoint: Address=0x%02X Attributes=0x%02X MaxPacket=%u\n",
                   EndpointDesc->bEndpointAddress,
                   EndpointDesc->bmAttributes,
                   EndpointDesc->wMaxPacketSize);

            if ((EndpointDesc->bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_BULK)
            {
                if (USB_ENDPOINT_DIRECTION_IN(EndpointDesc->bEndpointAddress))
                {
                    if (IsDataInterface)
                    {
                        Adapter->BulkInEndpoint.EndpointAddress = EndpointDesc->bEndpointAddress;
                        Adapter->BulkInEndpoint.MaxPacketSize = EndpointDesc->wMaxPacketSize;
                        DPRINT("USBRNDIS: Bulk IN endpoint: 0x%02X\n", EndpointDesc->bEndpointAddress);
                    }
                }
                else
                {
                    if (IsDataInterface)
                    {
                        Adapter->BulkOutEndpoint.EndpointAddress = EndpointDesc->bEndpointAddress;
                        Adapter->BulkOutEndpoint.MaxPacketSize = EndpointDesc->wMaxPacketSize;
                        DPRINT("USBRNDIS: Bulk OUT endpoint: 0x%02X\n", EndpointDesc->bEndpointAddress);
                    }
                }
                EndpointsFound++;
            }
            else if ((EndpointDesc->bmAttributes & USB_ENDPOINT_TYPE_MASK) == USB_ENDPOINT_TYPE_INTERRUPT)
            {
                if (!IsDataInterface)
                {
                    Adapter->InterruptEndpoint.EndpointAddress = EndpointDesc->bEndpointAddress;
                    Adapter->InterruptEndpoint.MaxPacketSize = EndpointDesc->wMaxPacketSize;
                    DPRINT("USBRNDIS: Interrupt endpoint: 0x%02X\n", EndpointDesc->bEndpointAddress);
                }
                EndpointsFound++;
            }
        }
        else if (CurrentDesc[1] == USB_INTERFACE_DESCRIPTOR_TYPE)
        {
            /* Reached next interface, stop scanning */
            break;
        }

        /* Move to next descriptor */
        if (CurrentDesc[0] == 0)
        {
            break;
        }
        CurrentDesc += CurrentDesc[0];
    }

    return (EndpointsFound > 0) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

/*
 * RndisUsbParseConfiguration
 *
 * Parse configuration descriptor to find RNDIS interfaces and endpoints.
 * RNDIS typically uses:
 *   - Control interface (CDC ACM, subclass 0x02, protocol 0xFF)
 *   - Data interface (CDC Data, class 0x0A)
 * Or it may use:
 *   - Wireless controller class (0xE0, subclass 0x01, protocol 0x03)
 *
 * TODO: Parse CDC Union functional descriptor to properly map control->data
 * interface relationship. The CDC Union descriptor (bDescriptorSubtype 0x06)
 * identifies which interface is the "master" (control) and which is the
 * "subordinate" (data). This is important for devices with multiple
 * CDC function groups.
 *
 * TODO: Handle alternate settings properly. Some devices have alternate
 * setting 0 with zero endpoints (bandwidth conservation) and alternate
 * setting 1 with the actual bulk endpoints. We should select the alternate
 * setting with endpoints.
 */
static
NTSTATUS
RndisUsbParseConfiguration(
    IN PRNDIS_ADAPTER Adapter)
{
    PUCHAR DescStart;
    PUCHAR DescEnd;
    PUCHAR CurrentDesc;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDesc;
    BOOLEAN FoundControlInterface = FALSE;

    DescStart = (PUCHAR)Adapter->ConfigurationDescriptor;
    DescEnd = DescStart + Adapter->ConfigurationDescriptor->wTotalLength;
    CurrentDesc = DescStart + Adapter->ConfigurationDescriptor->bLength;

    /* Scan all interfaces */
    while (CurrentDesc < DescEnd)
    {
        if (CurrentDesc[1] == USB_INTERFACE_DESCRIPTOR_TYPE)
        {
            InterfaceDesc = (PUSB_INTERFACE_DESCRIPTOR)CurrentDesc;

            DPRINT("USBRNDIS: Interface %u: Class=0x%02X SubClass=0x%02X Protocol=0x%02X Endpoints=%u\n",
                   InterfaceDesc->bInterfaceNumber,
                   InterfaceDesc->bInterfaceClass,
                   InterfaceDesc->bInterfaceSubClass,
                   InterfaceDesc->bInterfaceProtocol,
                   InterfaceDesc->bNumEndpoints);

            /* Check for RNDIS control interface patterns */
            /* Pattern 1: CDC ACM with vendor protocol (standard RNDIS) */
            if (InterfaceDesc->bInterfaceClass == USB_CLASS_COMM &&
                InterfaceDesc->bInterfaceSubClass == USB_CDC_SUBCLASS_ACM &&
                InterfaceDesc->bInterfaceProtocol == USB_CDC_PROTOCOL_RNDIS)
            {
                DPRINT("USBRNDIS: Found RNDIS control interface (CDC ACM)\n");
                Adapter->ControlInterfaceNumber = InterfaceDesc->bInterfaceNumber;
                FoundControlInterface = TRUE;
                RndisUsbFindEndpoints(Adapter, InterfaceDesc, DescEnd, FALSE);
            }
            /* Pattern 2: Wireless controller (RNDIS over WiFi/cellular) */
            else if (InterfaceDesc->bInterfaceClass == USB_CLASS_WIRELESS_CONTROLLER &&
                     InterfaceDesc->bInterfaceSubClass == 0x01 &&
                     InterfaceDesc->bInterfaceProtocol == 0x03)
            {
                DPRINT("USBRNDIS: Found RNDIS interface (Wireless controller)\n");
                Adapter->ControlInterfaceNumber = InterfaceDesc->bInterfaceNumber;
                FoundControlInterface = TRUE;
                RndisUsbFindEndpoints(Adapter, InterfaceDesc, DescEnd, TRUE);
                /* Wireless RNDIS often combines control and data */
                Adapter->DataInterfaceNumber = InterfaceDesc->bInterfaceNumber;
            }
            /* Pattern 3: Miscellaneous class (ActiveSync RNDIS) */
            else if (InterfaceDesc->bInterfaceClass == USB_CLASS_MISC &&
                     InterfaceDesc->bInterfaceSubClass == 0x01 &&
                     InterfaceDesc->bInterfaceProtocol == 0x01)
            {
                DPRINT("USBRNDIS: Found RNDIS interface (Miscellaneous/ActiveSync)\n");
                Adapter->ControlInterfaceNumber = InterfaceDesc->bInterfaceNumber;
                FoundControlInterface = TRUE;
                RndisUsbFindEndpoints(Adapter, InterfaceDesc, DescEnd, FALSE);
            }
            /* Pattern 4: CDC-ECM (Ethernet Control Model) - no RNDIS wrapping needed */
            else if (InterfaceDesc->bInterfaceClass == USB_CLASS_COMM &&
                     InterfaceDesc->bInterfaceSubClass == USB_CDC_SUBCLASS_ECM)
            {
                DPRINT1("USBRNDIS: Found CDC-ECM interface (Ethernet Control Model)\n");
                Adapter->ControlInterfaceNumber = InterfaceDesc->bInterfaceNumber;
                Adapter->IsCdcEcm = TRUE;  /* CDC-ECM mode - no RNDIS messages */
                FoundControlInterface = TRUE;
                RndisUsbFindEndpoints(Adapter, InterfaceDesc, DescEnd, FALSE);
            }
            /* Pattern 5: CDC-NCM (Network Control Model) - uses NTB framing */
            else if (InterfaceDesc->bInterfaceClass == USB_CLASS_COMM &&
                     InterfaceDesc->bInterfaceSubClass == USB_CDC_SUBCLASS_NCM)
            {
                /*
                 * CDC-NCM uses NTB (Network Transfer Block) framing which wraps
                 * Ethernet frames in NTH16/NDP16 structures. This requires special
                 * TX/RX handling in rndisdata.c.
                 */
                DPRINT1("USBRNDIS: Found CDC-NCM interface (Network Control Model)\n");
                Adapter->ControlInterfaceNumber = InterfaceDesc->bInterfaceNumber;
                Adapter->IsCdcNcm = TRUE;  /* CDC-NCM mode - NTB framing required */
                FoundControlInterface = TRUE;
                RndisUsbFindEndpoints(Adapter, InterfaceDesc, DescEnd, FALSE);
            }
            /* Check for CDC Data interface */
            else if (InterfaceDesc->bInterfaceClass == USB_CLASS_CDC_DATA)
            {
                DPRINT("USBRNDIS: Found CDC Data interface (Alt=%u Endpoints=%u)\n",
                       InterfaceDesc->bAlternateSetting, InterfaceDesc->bNumEndpoints);
                /*
                 * CDC Data interfaces often have:
                 * - Alternate setting 0 with 0 endpoints (inactive)
                 * - Alternate setting 1 with 2 bulk endpoints (active)
                 * We want the one with endpoints.
                 */
                if (InterfaceDesc->bNumEndpoints >= 2)
                {
                    Adapter->DataInterfaceNumber = InterfaceDesc->bInterfaceNumber;
                    Adapter->DataAlternateSetting = InterfaceDesc->bAlternateSetting;
                    RndisUsbFindEndpoints(Adapter, InterfaceDesc, DescEnd, TRUE);
                    DPRINT("USBRNDIS: Using CDC Data Alt %u with %u endpoints\n",
                           InterfaceDesc->bAlternateSetting, InterfaceDesc->bNumEndpoints);
                }
            }
        }

        /* Move to next descriptor */
        if (CurrentDesc[0] == 0)
        {
            break;
        }
        CurrentDesc += CurrentDesc[0];
    }

    if (!FoundControlInterface)
    {
        DPRINT1("USBRNDIS: No RNDIS control interface found\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /* Verify we have required endpoints */
    if (Adapter->BulkInEndpoint.EndpointAddress == 0 ||
        Adapter->BulkOutEndpoint.EndpointAddress == 0)
    {
        DPRINT1("USBRNDIS: Missing bulk endpoints (IN=0x%02X OUT=0x%02X)\n",
                Adapter->BulkInEndpoint.EndpointAddress,
                Adapter->BulkOutEndpoint.EndpointAddress);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    return STATUS_SUCCESS;
}

/*
 * RndisUsbSelectConfiguration
 *
 * Select the USB configuration and claim interfaces
 */
NTSTATUS
RndisUsbSelectConfiguration(
    IN PRNDIS_ADAPTER Adapter)
{
    NTSTATUS Status;
    PURB Urb;
    PUSBD_INTERFACE_LIST_ENTRY InterfaceList;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDescriptor;
    ULONG InterfaceCount = 0;
    ULONG i;

    DPRINT("USBRNDIS: Selecting USB configuration\n");

    /* Parse configuration to find interfaces */
    Status = RndisUsbParseConfiguration(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /*
     * Count interfaces we need to claim.
     * DataInterfaceNumber == 0xFF means no separate data interface was found.
     */
    InterfaceCount = 1; /* At least control interface */
    if (Adapter->DataInterfaceNumber != 0xFF &&
        Adapter->DataInterfaceNumber != Adapter->ControlInterfaceNumber)
    {
        InterfaceCount = 2; /* Separate data interface */
    }

    /* Allocate interface list (plus terminator) */
    InterfaceList = RndisAllocateMemory(NonPagedPool,
        sizeof(USBD_INTERFACE_LIST_ENTRY) * (InterfaceCount + 1));
    if (!InterfaceList)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Find interface descriptors and add to list */
    i = 0;

    /* Find control interface descriptor */
    InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(
        Adapter->ConfigurationDescriptor,
        Adapter->ConfigurationDescriptor,
        Adapter->ControlInterfaceNumber,
        -1, /* Any alternate setting */
        -1, -1, -1);

    if (InterfaceDescriptor)
    {
        InterfaceList[i].InterfaceDescriptor = InterfaceDescriptor;
        InterfaceList[i].Interface = NULL;
        i++;
    }

    /* Find data interface descriptor if separate */
    if (InterfaceCount > 1)
    {
        /*
         * For CDC-NCM/ECM, the data interface has alternate settings:
         * - Alt 0: 0 endpoints (inactive)
         * - Alt 1: 2 bulk endpoints (active)
         * We must select the alternate setting with endpoints.
         */
        InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(
            Adapter->ConfigurationDescriptor,
            Adapter->ConfigurationDescriptor,
            Adapter->DataInterfaceNumber,
            Adapter->DataAlternateSetting, /* Use the alt setting with endpoints */
            -1, -1, -1);

        if (InterfaceDescriptor)
        {
            DPRINT1("USBRNDIS: Data interface descriptor: Alt=%u Endpoints=%u\n",
                   InterfaceDescriptor->bAlternateSetting,
                   InterfaceDescriptor->bNumEndpoints);
            InterfaceList[i].InterfaceDescriptor = InterfaceDescriptor;
            InterfaceList[i].Interface = NULL;
            i++;
        }
        else
        {
            DPRINT1("USBRNDIS: Failed to find data interface %u alt %u\n",
                   Adapter->DataInterfaceNumber, Adapter->DataAlternateSetting);
        }
    }

    /* Terminate list */
    InterfaceList[i].InterfaceDescriptor = NULL;

    /* Create configuration URB */
    Urb = USBD_CreateConfigurationRequestEx(
        Adapter->ConfigurationDescriptor,
        InterfaceList);

    if (!Urb)
    {
        RndisFreeMemory(InterfaceList);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Submit configuration request */
    Status = RndisSyncUrbRequest(Adapter->LowerDeviceObject, Urb);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: Failed to select configuration (0x%08X)\n", Status);
        ExFreePool(Urb);
        RndisFreeMemory(InterfaceList);
        return Status;
    }

    /* Save configuration handle */
    Adapter->ConfigurationHandle = Urb->UrbSelectConfiguration.ConfigurationHandle;

    /* Save interface information and pipe handles */
    for (i = 0; InterfaceList[i].InterfaceDescriptor != NULL; i++)
    {
        PUSBD_INTERFACE_INFORMATION InterfaceInfo;
        ULONG j;

        InterfaceInfo = InterfaceList[i].Interface;
        if (!InterfaceInfo)
        {
            continue;
        }

        DPRINT("USBRNDIS: Interface %u configured: %u pipes\n",
               InterfaceInfo->InterfaceNumber,
               InterfaceInfo->NumberOfPipes);

        /* Copy interface info */
        if (InterfaceInfo->InterfaceNumber == Adapter->ControlInterfaceNumber)
        {
            Adapter->ControlInterface = RndisAllocateMemory(NonPagedPool, InterfaceInfo->Length);
            if (Adapter->ControlInterface)
            {
                RtlCopyMemory(Adapter->ControlInterface, InterfaceInfo, InterfaceInfo->Length);
            }
        }

        if (InterfaceInfo->InterfaceNumber == Adapter->DataInterfaceNumber)
        {
            Adapter->DataInterface = RndisAllocateMemory(NonPagedPool, InterfaceInfo->Length);
            if (Adapter->DataInterface)
            {
                RtlCopyMemory(Adapter->DataInterface, InterfaceInfo, InterfaceInfo->Length);
            }
        }

        /* Get pipe handles for endpoints */
        for (j = 0; j < InterfaceInfo->NumberOfPipes; j++)
        {
            PUSBD_PIPE_INFORMATION Pipe = &InterfaceInfo->Pipes[j];

            DPRINT1("USBRNDIS: Config Pipe %lu: Address=0x%02X Type=%u Handle=%p\n",
                    j, Pipe->EndpointAddress, Pipe->PipeType, Pipe->PipeHandle);

            if (Pipe->EndpointAddress == Adapter->BulkInEndpoint.EndpointAddress)
            {
                Adapter->BulkInEndpoint.PipeHandle = Pipe->PipeHandle;
                DPRINT1("USBRNDIS: Matched Bulk IN endpoint 0x%02X -> Handle=%p\n",
                        Pipe->EndpointAddress, Pipe->PipeHandle);
            }
            else if (Pipe->EndpointAddress == Adapter->BulkOutEndpoint.EndpointAddress)
            {
                Adapter->BulkOutEndpoint.PipeHandle = Pipe->PipeHandle;
                DPRINT1("USBRNDIS: Matched Bulk OUT endpoint 0x%02X -> Handle=%p\n",
                        Pipe->EndpointAddress, Pipe->PipeHandle);
            }
            else if (Pipe->EndpointAddress == Adapter->InterruptEndpoint.EndpointAddress)
            {
                Adapter->InterruptEndpoint.PipeHandle = Pipe->PipeHandle;
            }
        }
    }

    ExFreePool(Urb);
    RndisFreeMemory(InterfaceList);

    /* Verify we got pipe handles */
    if (!Adapter->BulkInEndpoint.PipeHandle || !Adapter->BulkOutEndpoint.PipeHandle)
    {
        DPRINT1("USBRNDIS: Missing pipe handles\n");
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    /*
     * TODO: The interrupt endpoint (if present) should be used for
     * "response available" notifications from the device. Per CDC-ACM,
     * the device sends a RESPONSE_AVAILABLE notification (0x01) on the
     * interrupt endpoint when a response to GET_ENCAPSULATED_RESPONSE
     * is ready. This allows for event-driven response retrieval instead
     * of polling.
     *
     * Current implementation uses polling in RndisCommand() which works
     * but is less efficient. A future version could:
     * 1. Submit an async interrupt IN URB
     * 2. On completion with RESPONSE_AVAILABLE, call GET_ENCAPSULATED_RESPONSE
     * 3. Resubmit the interrupt URB for next notification
     */
    if (Adapter->InterruptEndpoint.PipeHandle)
    {
        DPRINT("USBRNDIS: Interrupt endpoint available but not used (polling mode)\n");
    }

    /*
     * For CDC-ECM/NCM, the data interface typically has:
     * - Alt 0: No endpoints (inactive)
     * - Alt 1: Bulk IN/OUT endpoints (active)
     *
     * USBD_CreateConfigurationRequestEx should select the alternate setting
     * we specified in the interface list. However, if we didn't get pipe handles,
     * we may need to explicitly select the alternate setting.
     *
     * Only call SELECT_INTERFACE if we don't have pipe handles yet - calling it
     * when already selected can cause long delays (10+ seconds) on some USB stacks.
     */
    if ((Adapter->IsCdcEcm || Adapter->IsCdcNcm) &&
        Adapter->DataAlternateSetting > 0 &&
        Adapter->DataInterfaceNumber != Adapter->ControlInterfaceNumber &&
        (!Adapter->BulkInEndpoint.PipeHandle || !Adapter->BulkOutEndpoint.PipeHandle))
    {
        NTSTATUS AltStatus;
        DPRINT1("USBRNDIS: No pipe handles from config, selecting alt %u explicitly\n",
                Adapter->DataAlternateSetting);
        AltStatus = RndisUsbSelectAlternate(Adapter,
                                            Adapter->DataInterfaceNumber,
                                            Adapter->DataAlternateSetting);
        if (!NT_SUCCESS(AltStatus))
        {
            DPRINT1("USBRNDIS: Failed to select data interface alt %u (0x%08X)\n",
                    Adapter->DataAlternateSetting, AltStatus);
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    DPRINT("USBRNDIS: Configuration selected successfully\n");
    return STATUS_SUCCESS;
}

/*
 * RndisUsbSelectAlternate
 *
 * Select an alternate setting for an interface.
 * Required for CDC-ECM/NCM to activate the data interface endpoints.
 */
static
NTSTATUS
RndisUsbSelectAlternate(
    IN PRNDIS_ADAPTER Adapter,
    IN UCHAR InterfaceNumber,
    IN UCHAR AlternateSetting)
{
    PURB Urb;
    NTSTATUS Status;
    PUSB_INTERFACE_DESCRIPTOR InterfaceDesc;
    ULONG UrbSize;

    DPRINT1("USBRNDIS: Selecting alternate setting %u for interface %u\n",
            AlternateSetting, InterfaceNumber);

    /* Find the interface descriptor for the alternate setting */
    InterfaceDesc = USBD_ParseConfigurationDescriptorEx(
        Adapter->ConfigurationDescriptor,
        Adapter->ConfigurationDescriptor,
        InterfaceNumber,
        AlternateSetting,
        -1, -1, -1);

    if (!InterfaceDesc)
    {
        DPRINT1("USBRNDIS: Interface descriptor not found\n");
        return STATUS_NOT_FOUND;
    }

    /* Calculate URB size for interface with endpoints */
    UrbSize = GET_SELECT_INTERFACE_REQUEST_SIZE(InterfaceDesc->bNumEndpoints);

    Urb = RndisAllocateMemory(NonPagedPool, UrbSize);
    if (!Urb)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Build select interface URB */
    UsbBuildSelectInterfaceRequest(
        Urb,
        (USHORT)UrbSize,
        Adapter->ConfigurationHandle,
        InterfaceNumber,
        AlternateSetting);

    /* Fill in interface descriptor info */
    Urb->UrbSelectInterface.Interface.Length =
        sizeof(USBD_INTERFACE_INFORMATION) +
        (InterfaceDesc->bNumEndpoints - 1) * sizeof(USBD_PIPE_INFORMATION);
    Urb->UrbSelectInterface.Interface.InterfaceNumber = InterfaceNumber;
    Urb->UrbSelectInterface.Interface.AlternateSetting = AlternateSetting;
    Urb->UrbSelectInterface.Interface.NumberOfPipes = InterfaceDesc->bNumEndpoints;

    Status = RndisSyncUrbRequest(Adapter->LowerDeviceObject, Urb);

    if (NT_SUCCESS(Status))
    {
        PUSBD_INTERFACE_INFORMATION InterfaceInfo = &Urb->UrbSelectInterface.Interface;
        ULONG j;

        DPRINT1("USBRNDIS: Interface %u alt %u selected, %u pipes\n",
                InterfaceNumber, AlternateSetting, InterfaceInfo->NumberOfPipes);

        /* Update pipe handles from the new interface info */
        for (j = 0; j < InterfaceInfo->NumberOfPipes; j++)
        {
            PUSBD_PIPE_INFORMATION Pipe = &InterfaceInfo->Pipes[j];

            DPRINT1("USBRNDIS: Pipe %u: Address=0x%02X Type=%u Handle=%p MaxPacket=%u\n",
                    j, Pipe->EndpointAddress, Pipe->PipeType, Pipe->PipeHandle,
                    Pipe->MaximumPacketSize);

            if (Pipe->EndpointAddress == Adapter->BulkInEndpoint.EndpointAddress)
            {
                Adapter->BulkInEndpoint.PipeHandle = Pipe->PipeHandle;
                Adapter->BulkInEndpoint.MaxPacketSize = Pipe->MaximumPacketSize;
            }
            else if (Pipe->EndpointAddress == Adapter->BulkOutEndpoint.EndpointAddress)
            {
                Adapter->BulkOutEndpoint.PipeHandle = Pipe->PipeHandle;
                Adapter->BulkOutEndpoint.MaxPacketSize = Pipe->MaximumPacketSize;
            }
        }
    }
    else
    {
        DPRINT1("USBRNDIS: Select interface failed (0x%08X)\n", Status);
    }

    RndisFreeMemory(Urb);
    return Status;
}

/*
 * RndisUsbSendControlMessage
 *
 * Send an RNDIS control message to the device using
 * CDC SEND_ENCAPSULATED_COMMAND request
 */
NTSTATUS
RndisUsbSendControlMessage(
    IN PRNDIS_ADAPTER Adapter,
    IN PVOID Buffer,
    IN ULONG BufferLength)
{
    PURB Urb;
    NTSTATUS Status;

    DPRINT("USBRNDIS: Sending control message (%u bytes)\n", BufferLength);

    Urb = RndisAllocateMemory(NonPagedPool, sizeof(URB));
    if (!Urb)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Build vendor/class request for SEND_ENCAPSULATED_COMMAND */
    Urb->UrbControlVendorClassRequest.Hdr.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb->UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_CLASS_INTERFACE;
    Urb->UrbControlVendorClassRequest.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
    Urb->UrbControlVendorClassRequest.TransferBufferLength = BufferLength;
    Urb->UrbControlVendorClassRequest.TransferBuffer = Buffer;
    Urb->UrbControlVendorClassRequest.TransferBufferMDL = NULL;
    Urb->UrbControlVendorClassRequest.Request = USB_CDC_SEND_ENCAPSULATED_COMMAND;
    Urb->UrbControlVendorClassRequest.Value = 0;
    Urb->UrbControlVendorClassRequest.Index = Adapter->ControlInterfaceNumber;

    Status = RndisSyncUrbRequest(Adapter->LowerDeviceObject, Urb);

    RndisFreeMemory(Urb);
    return Status;
}

/*
 * RndisUsbReceiveControlResponse
 *
 * Receive an RNDIS control response from the device using
 * CDC GET_ENCAPSULATED_RESPONSE request
 */
NTSTATUS
RndisUsbReceiveControlResponse(
    IN PRNDIS_ADAPTER Adapter,
    OUT PVOID Buffer,
    IN ULONG BufferLength,
    OUT PULONG BytesReceived)
{
    PURB Urb;
    NTSTATUS Status;

    DPRINT("USBRNDIS: Receiving control response (max %u bytes)\n", BufferLength);

    *BytesReceived = 0;

    Urb = RndisAllocateMemory(NonPagedPool, sizeof(URB));
    if (!Urb)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Build vendor/class request for GET_ENCAPSULATED_RESPONSE */
    Urb->UrbControlVendorClassRequest.Hdr.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb->UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_CLASS_INTERFACE;
    Urb->UrbControlVendorClassRequest.TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
    Urb->UrbControlVendorClassRequest.TransferBufferLength = BufferLength;
    Urb->UrbControlVendorClassRequest.TransferBuffer = Buffer;
    Urb->UrbControlVendorClassRequest.TransferBufferMDL = NULL;
    Urb->UrbControlVendorClassRequest.Request = USB_CDC_GET_ENCAPSULATED_RESPONSE;
    Urb->UrbControlVendorClassRequest.Value = 0;
    Urb->UrbControlVendorClassRequest.Index = Adapter->ControlInterfaceNumber;

    Status = RndisSyncUrbRequest(Adapter->LowerDeviceObject, Urb);

    if (NT_SUCCESS(Status))
    {
        *BytesReceived = Urb->UrbControlVendorClassRequest.TransferBufferLength;
        DPRINT("USBRNDIS: Received %u bytes\n", *BytesReceived);
    }

    RndisFreeMemory(Urb);
    return Status;
}

/*
 * RndisNcmSetNtbInputSize
 *
 * Set the maximum NTB input size for CDC-NCM device.
 * This tells the device the maximum size of NTBs we can receive.
 * Per USB CDC NCM 1.0 specification, section 6.2.5.
 */
static
NTSTATUS
RndisNcmSetNtbInputSize(
    IN PRNDIS_ADAPTER Adapter,
    IN ULONG NtbInputSize)
{
    PURB Urb;
    NTSTATUS Status;
    PULONG SizeBuffer;

    DPRINT1("USBRNDIS: Setting NCM NTB input size to %lu\n", NtbInputSize);

    /* Allocate buffer for the size value */
    SizeBuffer = RndisAllocateMemory(NonPagedPool, sizeof(ULONG));
    if (!SizeBuffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    *SizeBuffer = NtbInputSize;

    Urb = RndisAllocateMemory(NonPagedPool, sizeof(URB));
    if (!Urb)
    {
        RndisFreeMemory(SizeBuffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Build class request for SET_NTB_INPUT_SIZE */
    Urb->UrbControlVendorClassRequest.Hdr.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb->UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_CLASS_INTERFACE;
    Urb->UrbControlVendorClassRequest.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
    Urb->UrbControlVendorClassRequest.TransferBufferLength = sizeof(ULONG);
    Urb->UrbControlVendorClassRequest.TransferBuffer = SizeBuffer;
    Urb->UrbControlVendorClassRequest.TransferBufferMDL = NULL;
    Urb->UrbControlVendorClassRequest.Request = USB_CDC_NCM_SET_NTB_INPUT_SIZE;
    Urb->UrbControlVendorClassRequest.Value = 0;
    Urb->UrbControlVendorClassRequest.Index = Adapter->ControlInterfaceNumber;

    Status = RndisSyncUrbRequest(Adapter->LowerDeviceObject, Urb);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: SET_NTB_INPUT_SIZE failed (0x%08X)\n", Status);
    }

    RndisFreeMemory(Urb);
    RndisFreeMemory(SizeBuffer);
    return Status;
}

/*
 * RndisNcmGetNtbParameters
 *
 * Query NCM NTB parameters from the device.
 * Per USB CDC NCM 1.0 specification, section 6.2.1.
 */
static
NTSTATUS
RndisNcmGetNtbParameters(
    IN PRNDIS_ADAPTER Adapter)
{
    PURB Urb;
    NTSTATUS Status;
    PNCM_NTB_PARAMETERS Params;

    DPRINT1("USBRNDIS: Getting NCM NTB parameters\n");

    /* Allocate buffer for parameters */
    Params = RndisAllocateMemory(NonPagedPool, sizeof(NCM_NTB_PARAMETERS));
    if (!Params)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Urb = RndisAllocateMemory(NonPagedPool, sizeof(URB));
    if (!Urb)
    {
        RndisFreeMemory(Params);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Build class request for GET_NTB_PARAMETERS */
    Urb->UrbControlVendorClassRequest.Hdr.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb->UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_CLASS_INTERFACE;
    Urb->UrbControlVendorClassRequest.TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
    Urb->UrbControlVendorClassRequest.TransferBufferLength = sizeof(NCM_NTB_PARAMETERS);
    Urb->UrbControlVendorClassRequest.TransferBuffer = Params;
    Urb->UrbControlVendorClassRequest.TransferBufferMDL = NULL;
    Urb->UrbControlVendorClassRequest.Request = USB_CDC_NCM_GET_NTB_PARAMETERS;
    Urb->UrbControlVendorClassRequest.Value = 0;
    Urb->UrbControlVendorClassRequest.Index = Adapter->ControlInterfaceNumber;

    Status = RndisSyncUrbRequest(Adapter->LowerDeviceObject, Urb);

    if (NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: NCM NTB Parameters:\n");
        DPRINT1("  wLength: %u\n", Params->wLength);
        DPRINT1("  bmNtbFormatsSupported: 0x%04X\n", Params->bmNtbFormatsSupported);
        DPRINT1("  dwNtbInMaxSize: %lu\n", Params->dwNtbInMaxSize);
        DPRINT1("  wNdpInDivisor: %u\n", Params->wNdpInDivisor);
        DPRINT1("  wNdpInPayloadRemainder: %u\n", Params->wNdpInPayloadRemainder);
        DPRINT1("  wNdpInAlignment: %u\n", Params->wNdpInAlignment);
        DPRINT1("  dwNtbOutMaxSize: %lu\n", Params->dwNtbOutMaxSize);
        DPRINT1("  wNdpOutDivisor: %u\n", Params->wNdpOutDivisor);
        DPRINT1("  wNdpOutPayloadRemainder: %u\n", Params->wNdpOutPayloadRemainder);
        DPRINT1("  wNdpOutAlignment: %u\n", Params->wNdpOutAlignment);
        DPRINT1("  wNtbOutMaxDatagrams: %u\n", Params->wNtbOutMaxDatagrams);

        /*
         * Store parameters in adapter context.
         * For TX (building NTBs), use OUT parameters.
         * For RX (SET_NTB_INPUT_SIZE), use our buffer size capped to IN max.
         */
        Adapter->NcmNtbMaxSize = (Params->dwNtbInMaxSize < RNDIS_MAX_TRANSFER_SIZE) ?
                                  Params->dwNtbInMaxSize : RNDIS_MAX_TRANSFER_SIZE;
        Adapter->NcmNtbOutMaxSize = (Params->dwNtbOutMaxSize < RNDIS_MAX_TRANSFER_SIZE) ?
                                    Params->dwNtbOutMaxSize : RNDIS_MAX_TRANSFER_SIZE;
        Adapter->NcmNdpDivisor = Params->wNdpOutDivisor;
        Adapter->NcmNdpRemainder = Params->wNdpOutPayloadRemainder;
        Adapter->NcmNdpAlignment = Params->wNdpOutAlignment;

        /* Ensure minimum alignment */
        if (Adapter->NcmNdpAlignment == 0)
        {
            Adapter->NcmNdpAlignment = 4;
        }
        if (Adapter->NcmNdpDivisor == 0)
        {
            Adapter->NcmNdpDivisor = 4;
        }
    }
    else
    {
        DPRINT1("USBRNDIS: GET_NTB_PARAMETERS failed (0x%08X), using defaults\n", Status);
        /* Use defaults already set in usbrndis.c */
    }

    RndisFreeMemory(Urb);
    RndisFreeMemory(Params);
    return Status;
}

/*
 * RndisNcmSetup
 *
 * Perform CDC-NCM specific setup after configuration is selected.
 * This includes:
 * 1. Query NTB parameters from device
 * 2. Set NTB input size to tell device our max receive buffer
 */
NTSTATUS
RndisNcmSetup(
    IN PRNDIS_ADAPTER Adapter)
{
    NTSTATUS Status;

    DPRINT1("USBRNDIS: Performing CDC-NCM setup\n");

    /* Query device's NTB parameters */
    Status = RndisNcmGetNtbParameters(Adapter);
    /* Continue even if this fails - we'll use defaults */

    /* Tell device our max NTB receive size */
    Status = RndisNcmSetNtbInputSize(Adapter, Adapter->NcmNtbMaxSize);
    if (!NT_SUCCESS(Status))
    {
        /* Some devices may not support this command, continue anyway */
        DPRINT1("USBRNDIS: SET_NTB_INPUT_SIZE failed, continuing\n");
    }

    DPRINT1("USBRNDIS: CDC-NCM setup complete\n");
    return STATUS_SUCCESS;
}

/*
 * RndisAsyncUrbRequest
 *
 * Submit a URB asynchronously with a completion routine.
 * Increments PendingIoCount which must be decremented in the completion routine.
 *
 * Returns STATUS_PENDING on async submit, or STATUS_SUCCESS/error if completed
 * synchronously (in which case completion routine has already run).
 */
NTSTATUS
RndisAsyncUrbRequest(
    IN PRNDIS_ADAPTER Adapter,
    IN PURB Urb,
    IN PIO_COMPLETION_ROUTINE CompletionRoutine,
    IN PVOID Context,
    OUT PIRP *OutIrp OPTIONAL)
{
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    /* Increment pending I/O count before submitting */
    if (InterlockedIncrement(&Adapter->PendingIoCount) == 1)
    {
        /* Reset the event since we now have pending I/O */
        KeClearEvent(&Adapter->RemoveEvent);
    }

    /* Allocate IRP */
    Irp = IoAllocateIrp(Adapter->LowerDeviceObject->StackSize, FALSE);
    if (!Irp)
    {
        DPRINT1("USBRNDIS: Failed to allocate IRP for async URB\n");
        RndisDecrementPendingIo(Adapter);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Set up the IRP stack location */
    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = Urb;

    /* Set completion routine - will be called regardless of status */
    IoSetCompletionRoutine(Irp, CompletionRoutine, Context, TRUE, TRUE, TRUE);

    /* Return IRP pointer if requested (for cancellation) */
    if (OutIrp)
    {
        *OutIrp = Irp;
    }

    /* Submit to lower driver */
    Status = IoCallDriver(Adapter->LowerDeviceObject, Irp);

    /*
     * IoCallDriver returns STATUS_PENDING if the IRP will be completed async,
     * or the actual completion status if it completed synchronously.
     * In both cases, our completion routine has been/will be called.
     * Return the status so callers know if they should store the IRP pointer.
     */
    return Status;
}

/*
 * RndisRxResubmitDpc
 *
 * DPC routine to resubmit RX URB. Called at DISPATCH_LEVEL to avoid
 * stack overflow from synchronous completion in the completion routine.
 */
static
VOID
NTAPI
RndisRxResubmitDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (!Adapter->Halting)
    {
        RndisUsbSubmitBulkRead(Adapter);
    }
}

/*
 * RndisRxBackoffDpc
 *
 * DPC routine called when RX backoff timer expires.
 * Resets error counter and resumes RX after backoff period.
 */
static
VOID
NTAPI
RndisRxBackoffDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    DPRINT("USBRNDIS: RX backoff timer expired, resuming RX\n");

    /* Reset error counter */
    InterlockedExchange((PLONG)&Adapter->RxConsecutiveErrors, 0);

    /* Resume RX */
    if (!Adapter->Halting)
    {
        RndisUsbSubmitBulkRead(Adapter);
    }
}

/*
 * RndisRxDelayDpc
 *
 * DPC routine called when RX delay timer expires (after NAK).
 * This provides a small delay before resubmitting after device NAK
 * to prevent CPU spinning when no data is available.
 */
static
VOID
NTAPI
RndisRxDelayDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    /* Resubmit RX after delay */
    if (!Adapter->Halting)
    {
        RndisUsbSubmitBulkRead(Adapter);
    }
}

/*
 * RndisInitializeRxDpc
 *
 * Initialize DPC and timer for RX resubmission and backoff recovery.
 * Called from adapter initialization.
 */
VOID
RndisInitializeRxDpc(
    IN PRNDIS_ADAPTER Adapter)
{
    KeInitializeDpc(&Adapter->RxResubmitDpc, RndisRxResubmitDpc, Adapter);
    KeInitializeTimer(&Adapter->RxBackoffTimer);
    KeInitializeDpc(&Adapter->RxBackoffDpc, RndisRxBackoffDpc, Adapter);
    KeInitializeTimer(&Adapter->RxDelayTimer);
    KeInitializeDpc(&Adapter->RxDelayDpc, RndisRxDelayDpc, Adapter);
}

/*
 * RndisRxComplete
 *
 * Completion routine for async RX URB.
 * Called at DISPATCH_LEVEL when the USB stack completes the bulk IN request.
 */
static
NTSTATUS
NTAPI
RndisRxComplete(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)Context;
    NTSTATUS Status;
    ULONG TransferLength;
    BOOLEAN ShouldResubmit = TRUE;
    BOOLEAN UseDelayedResubmit = FALSE;
    ULONG ConsecutiveErrors;

    UNREFERENCED_PARAMETER(DeviceObject);

    Status = Irp->IoStatus.Status;
    TransferLength = Adapter->RxUrb.UrbBulkOrInterruptTransfer.TransferBufferLength;

    /* Clear RX IRP pointer under lock */
    NdisAcquireSpinLock(&Adapter->RxLock);
    Adapter->RxIrp = NULL;
    Adapter->RxSubmitted = FALSE;
    NdisReleaseSpinLock(&Adapter->RxLock);

    /* Free the IRP */
    IoFreeIrp(Irp);

    /* Process received data if successful and not halting */
    if (NT_SUCCESS(Status) && TransferLength > 0 && !Adapter->Halting)
    {
        DPRINT("USBRNDIS: RX complete, %u bytes received\n", TransferLength);
        Adapter->RxConsecutiveErrors = 0;  /* Reset error counter on success */
        RndisProcessReceivedPacket(Adapter, Adapter->RxBuffer, TransferLength);
        /* Data received - use immediate resubmission for low latency */
    }
    else if (NT_SUCCESS(Status) && TransferLength == 0)
    {
        /* Zero-length transfer - not an error, just no data */
        Adapter->RxConsecutiveErrors = 0;
        /* Use delayed resubmission to avoid tight loop */
        UseDelayedResubmit = TRUE;
    }
    else if (!NT_SUCCESS(Status) && Status != STATUS_CANCELLED)
    {
        USBD_STATUS UsbdStatus = Adapter->RxUrb.UrbBulkOrInterruptTransfer.Hdr.Status;

        /*
         * STATUS_DEVICE_BUSY (0x80000011) with USBD_STATUS=0 indicates the
         * device NAKed the transfer - this is normal when no data is available.
         * Treat this as "no data" rather than an error, but use delayed
         * resubmission to prevent CPU spinning.
         *
         * Similarly, STATUS_IO_TIMEOUT can occur briefly during power
         * state transitions.
         */
        if ((Status == STATUS_DEVICE_BUSY && UsbdStatus == USBD_STATUS_SUCCESS) ||
            Status == STATUS_IO_TIMEOUT)
        {
            /* Not a real error - just no data available */
            DPRINT("USBRNDIS: RX NAK (no data available)\n");
            Adapter->RxConsecutiveErrors = 0;  /* Reset counter for NAKs */
            /*
             * Use delayed resubmission to prevent infinite tight loop.
             * Without this delay, the driver would spin at 100% CPU.
             */
            UseDelayedResubmit = TRUE;
        }
        else
        {
            DPRINT1("USBRNDIS: RX failed with status 0x%08X (USBD_STATUS=0x%08X)\n",
                    Status, UsbdStatus);
            Adapter->RxErrorCount++;

            /*
             * Track consecutive errors for backoff.
             * If we get too many errors in a row, stop resubmitting to avoid
             * loop-hammering a broken device.
             */
            ConsecutiveErrors = InterlockedIncrement((PLONG)&Adapter->RxConsecutiveErrors);
            if (ConsecutiveErrors > 10)
            {
                DPRINT1("USBRNDIS: Too many consecutive RX errors (%u), entering backoff\n",
                        ConsecutiveErrors);
                ShouldResubmit = FALSE;

                /*
                 * Start backoff timer to recover after 1 second.
                 * This prevents permanent RX failure and allows recovery
                 * if the device recovers from its error state.
                 */
                {
                    LARGE_INTEGER DueTime;
                    DueTime.QuadPart = -10000000LL; /* 1 second in 100ns units, negative = relative */
                    KeSetTimer(&Adapter->RxBackoffTimer, DueTime, &Adapter->RxBackoffDpc);
                }
            }
            else
            {
                /* Use delayed resubmission for transient errors */
                UseDelayedResubmit = TRUE;
            }
        }
    }

    /* Decrement pending I/O count */
    RndisDecrementPendingIo(Adapter);

    /*
     * Resubmit RX URB if not halting and not in error backoff.
     * CRITICAL: We use a DPC to avoid stack overflow from synchronous
     * completion. If RndisUsbSubmitBulkRead completes synchronously,
     * it would call this completion routine again, causing infinite
     * recursion and stack overflow.
     *
     * For NAK/no-data cases, use a timer-based delay (20ms) to prevent
     * CPU spinning. For successful data reception, use immediate DPC
     * for low latency.
     */
    if (!Adapter->Halting && ShouldResubmit)
    {
        if (UseDelayedResubmit)
        {
            /* Delay 20ms before resubmitting - prevents CPU spin on NAK */
            LARGE_INTEGER DueTime;
            DueTime.QuadPart = -200000LL; /* 20ms in 100ns units, negative = relative */
            KeSetTimer(&Adapter->RxDelayTimer, DueTime, &Adapter->RxDelayDpc);
        }
        else
        {
            /* Immediate resubmission via DPC for data reception */
            KeInsertQueueDpc(&Adapter->RxResubmitDpc, NULL, NULL);
        }
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * RndisUsbSubmitBulkRead
 *
 * Submit an asynchronous bulk read request for receiving data packets.
 * The completion routine will process received data and resubmit.
 */
NTSTATUS
RndisUsbSubmitBulkRead(
    IN PRNDIS_ADAPTER Adapter)
{
    PURB Urb;
    NTSTATUS Status;
    PIRP Irp;

    if (!Adapter->BulkInEndpoint.PipeHandle)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* Check if halting */
    if (Adapter->Halting)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    /* Check if already submitted */
    NdisAcquireSpinLock(&Adapter->RxLock);
    if (Adapter->RxSubmitted)
    {
        NdisReleaseSpinLock(&Adapter->RxLock);
        return STATUS_SUCCESS; /* Already pending */
    }
    Adapter->RxSubmitted = TRUE;
    NdisReleaseSpinLock(&Adapter->RxLock);

    DPRINT1("USBRNDIS: Submitting async bulk read (PipeHandle=%p)\n",
            Adapter->BulkInEndpoint.PipeHandle);

    /* Build the URB */
    Urb = &Adapter->RxUrb;
    NdisZeroMemory(Urb, sizeof(URB));

    UsbBuildInterruptOrBulkTransferRequest(
        Urb,
        sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
        Adapter->BulkInEndpoint.PipeHandle,
        Adapter->RxBuffer,
        NULL,
        RNDIS_MAX_TRANSFER_SIZE,
        USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
        NULL);

    /* Submit asynchronously */
    Status = RndisAsyncUrbRequest(Adapter, Urb, RndisRxComplete, Adapter, &Irp);
    if (Status == STATUS_PENDING)
    {
        /*
         * Store IRP for cancellation, but only if completion hasn't run yet.
         * Check RxSubmitted - if completion ran, it set RxSubmitted = FALSE
         * and freed the IRP, so we must not store a freed pointer.
         */
        NdisAcquireSpinLock(&Adapter->RxLock);
        if (Adapter->RxSubmitted)
        {
            Adapter->RxIrp = Irp;
        }
        /* else completion already ran, IRP is freed, don't store */
        NdisReleaseSpinLock(&Adapter->RxLock);
    }
    else if (!NT_SUCCESS(Status))
    {
        /* Failed to submit - completion won't run, clean up ourselves */
        NdisAcquireSpinLock(&Adapter->RxLock);
        Adapter->RxSubmitted = FALSE;
        NdisReleaseSpinLock(&Adapter->RxLock);
    }
    /* else STATUS_SUCCESS: completion already ran (sync completion) */

    return Status;
}

/*
 * RndisTxComplete
 *
 * Completion routine for async TX URB.
 * Called at DISPATCH_LEVEL when the USB stack completes the bulk OUT request.
 */
static
NTSTATUS
NTAPI
RndisTxComplete(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)Context;
    NTSTATUS Status;
    PNDIS_PACKET Packet;
    NDIS_STATUS NdisStatus;
    BOOLEAN WasPending;

    UNREFERENCED_PARAMETER(DeviceObject);

    Status = Irp->IoStatus.Status;
    WasPending = Irp->PendingReturned;

    /* Retrieve the pending packet and clear TX state under lock */
    NdisAcquireSpinLock(&Adapter->TxLock);
    Packet = Adapter->PendingTxPacket;
    Adapter->PendingTxPacket = NULL;
    Adapter->TxIrp = NULL;
    Adapter->TxBusy = FALSE;
    NdisReleaseSpinLock(&Adapter->TxLock);

    /* Free the IRP */
    IoFreeIrp(Irp);

    /* Update statistics and determine NDIS status */
    if (NT_SUCCESS(Status))
    {
        DPRINT("USBRNDIS: TX complete, packet sent successfully\n");
        Adapter->TxOkCount++;
        NdisStatus = NDIS_STATUS_SUCCESS;
    }
    else
    {
        DPRINT1("USBRNDIS: TX failed with status 0x%08X\n", Status);
        Adapter->TxErrorCount++;
        NdisStatus = NDIS_STATUS_FAILURE;
    }

    /*
     * Only call NdisMSendComplete if the IRP was actually pended.
     * If WasPending is FALSE, the IRP completed synchronously and
     * RndisSend() will return the status directly (not NDIS_STATUS_PENDING).
     * Calling NdisMSendComplete before returning PENDING is undefined in NDIS5.
     */
    if (Packet && WasPending)
    {
        NdisMSendComplete(Adapter->MiniportAdapterHandle, Packet, NdisStatus);
    }

    /* Decrement pending I/O count */
    RndisDecrementPendingIo(Adapter);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

/*
 * RndisUsbSubmitBulkWrite
 *
 * Submit an async bulk write request for sending data packets.
 * Returns STATUS_PENDING on success - completion handled via callback.
 */
NTSTATUS
RndisUsbSubmitBulkWrite(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR Data,
    IN ULONG Length)
{
    PURB Urb;
    NTSTATUS Status;
    PIRP Irp;

    if (!Adapter->BulkOutEndpoint.PipeHandle)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Adapter->Halting)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    DPRINT("USBRNDIS: Submitting async bulk write (%u bytes)\n", Length);

    /* Build the URB */
    Urb = &Adapter->TxUrb;
    NdisZeroMemory(Urb, sizeof(URB));

    UsbBuildInterruptOrBulkTransferRequest(
        Urb,
        sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
        Adapter->BulkOutEndpoint.PipeHandle,
        Data,
        NULL,
        Length,
        USBD_TRANSFER_DIRECTION_OUT,
        NULL);

    /* Submit asynchronously */
    Status = RndisAsyncUrbRequest(Adapter, Urb, RndisTxComplete, Adapter, &Irp);
    if (Status == STATUS_PENDING)
    {
        /*
         * Store IRP for cancellation, but only if completion hasn't run yet.
         * Check TxBusy - if completion ran, it set TxBusy = FALSE
         * and freed the IRP, so we must not store a freed pointer.
         */
        NdisAcquireSpinLock(&Adapter->TxLock);
        if (Adapter->TxBusy)
        {
            Adapter->TxIrp = Irp;
        }
        /* else completion already ran, IRP is freed, don't store */
        NdisReleaseSpinLock(&Adapter->TxLock);
    }
    /* else sync completion: completion already ran */

    return Status;
}
