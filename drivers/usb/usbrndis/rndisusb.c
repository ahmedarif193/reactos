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

/* Enable debug output for troubleshooting */
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
static VOID NTAPI RndisTxResubmitDpc(PKDPC Dpc, PVOID DeferredContext, PVOID SystemArgument1, PVOID SystemArgument2);

/* Forward declaration for alternate setting selection */
static NTSTATUS RndisUsbSelectAlternate(IN PRNDIS_ADAPTER Adapter, IN UCHAR InterfaceNumber, IN UCHAR AlternateSetting);

static __inline
VOID
RndisMaybeDeferIrpFree(
    _In_ PRNDIS_ADAPTER Adapter,
    _Inout_ PIRP Irp,
    _Inout_ PIRP *IrpToFree)
{
    if (Adapter->Halting)
    {
        if (InterlockedCompareExchangePointer((PVOID *)IrpToFree, Irp, NULL) != NULL)
        {
            /* Should not happen; avoid leaking IRP */
            IoFreeIrp(Irp);
        }
    }
    else
    {
        IoFreeIrp(Irp);
    }
}

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
 * RndisUsbGetCdcMacAddress
 *
 * Read the MAC address from a USB string descriptor for CDC-ECM/NCM devices.
 * The MAC is stored as a 12-character Unicode hex string (e.g., "080027989E79").
 *
 * Returns STATUS_SUCCESS if MAC was successfully read, or error if not available.
 */
NTSTATUS
RndisUsbGetCdcMacAddress(
    IN PRNDIS_ADAPTER Adapter,
    OUT PUCHAR MacAddress)
{
    NTSTATUS Status;
    PUSB_STRING_DESCRIPTOR StringDesc;
    ULONG DescLength;
    UCHAR i;
    WCHAR HexByte[3];
    ULONG ByteValue;

    /* Check if we have a valid MAC string index */
    if (Adapter->CdcMacAddressIndex == 0)
    {
        DPRINT("USBRNDIS: No MAC address string index available\n");
        return STATUS_NOT_FOUND;
    }

    /* Allocate buffer for string descriptor (256 bytes max per USB spec) */
    DescLength = 256;
    Status = RndisUsbGetDescriptor(
        Adapter,
        USB_STRING_DESCRIPTOR_TYPE,
        Adapter->CdcMacAddressIndex,
        0x0409,  /* English (US) language ID */
        (PVOID *)&StringDesc,
        &DescLength);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: Failed to get MAC string descriptor (0x%08X)\n", Status);
        return Status;
    }

    /*
     * Validate string descriptor:
     * - bLength should be at least 26 bytes (2 + 12*2 for 12 hex chars as Unicode)
     * - bDescriptorType should be USB_STRING_DESCRIPTOR_TYPE
     * - String length should be exactly 12 Unicode characters (24 bytes)
     */
    if (StringDesc->bLength < 26 ||
        StringDesc->bDescriptorType != USB_STRING_DESCRIPTOR_TYPE)
    {
        DPRINT1("USBRNDIS: Invalid MAC string descriptor (length=%u type=%u)\n",
                StringDesc->bLength, StringDesc->bDescriptorType);
        RndisFreeMemory(StringDesc);
        return STATUS_INVALID_PARAMETER;
    }

    /* String length is (bLength - 2) / 2 Unicode chars, should be 12 for MAC */
    if ((StringDesc->bLength - 2) / 2 < 12)
    {
        DPRINT1("USBRNDIS: MAC string too short (%u chars)\n",
                (StringDesc->bLength - 2) / 2);
        RndisFreeMemory(StringDesc);
        return STATUS_INVALID_PARAMETER;
    }

    DPRINT1("USBRNDIS: MAC string from USB: %.*ls\n",
            12, StringDesc->bString);

    /* Parse 12 hex characters into 6 bytes */
    HexByte[2] = L'\0';
    for (i = 0; i < 6; i++)
    {
        HexByte[0] = StringDesc->bString[i * 2];
        HexByte[1] = StringDesc->bString[i * 2 + 1];

        /* Convert hex pair to byte value */
        ByteValue = 0;
        if (HexByte[0] >= L'0' && HexByte[0] <= L'9')
            ByteValue = (HexByte[0] - L'0') << 4;
        else if (HexByte[0] >= L'A' && HexByte[0] <= L'F')
            ByteValue = (HexByte[0] - L'A' + 10) << 4;
        else if (HexByte[0] >= L'a' && HexByte[0] <= L'f')
            ByteValue = (HexByte[0] - L'a' + 10) << 4;

        if (HexByte[1] >= L'0' && HexByte[1] <= L'9')
            ByteValue |= (HexByte[1] - L'0');
        else if (HexByte[1] >= L'A' && HexByte[1] <= L'F')
            ByteValue |= (HexByte[1] - L'A' + 10);
        else if (HexByte[1] >= L'a' && HexByte[1] <= L'f')
            ByteValue |= (HexByte[1] - L'a' + 10);

        MacAddress[i] = (UCHAR)ByteValue;
    }

    DPRINT1("USBRNDIS: Parsed MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
            MacAddress[0], MacAddress[1], MacAddress[2],
            MacAddress[3], MacAddress[4], MacAddress[5]);

    RndisFreeMemory(StringDesc);
    return STATUS_SUCCESS;
}

/*
 * RndisUsbFindCdcEthernetDescriptor
 *
 * Scan CDC class-specific descriptors following an interface to find the
 * Ethernet Networking Functional Descriptor and extract the iMACAddress
 * string index.
 */
static
VOID
RndisUsbFindCdcEthernetDescriptor(
    IN PRNDIS_ADAPTER Adapter,
    IN PUSB_INTERFACE_DESCRIPTOR InterfaceDesc,
    IN PUCHAR DescEnd)
{
    PUCHAR CurrentDesc;
    PUSB_CDC_ETHERNET_DESCRIPTOR EthDesc;

    CurrentDesc = (PUCHAR)InterfaceDesc + InterfaceDesc->bLength;

    /* Scan class-specific descriptors (type 0x24 = CS_INTERFACE) */
    while (CurrentDesc < DescEnd)
    {
        /* Check for CS_INTERFACE descriptor */
        if (CurrentDesc[1] == USB_CDC_CS_INTERFACE)
        {
            /* Check for Ethernet Networking Functional Descriptor (subtype 0x0F) */
            if (CurrentDesc[2] == USB_CDC_SUBTYPE_ETHERNET && CurrentDesc[0] >= 13)
            {
                EthDesc = (PUSB_CDC_ETHERNET_DESCRIPTOR)CurrentDesc;

                DPRINT1("USBRNDIS: Found CDC Ethernet Descriptor: iMACAddress=%u MaxSegment=%u\n",
                        EthDesc->iMACAddress, EthDesc->wMaxSegmentSize);

                Adapter->CdcMacAddressIndex = EthDesc->iMACAddress;
                return;
            }
        }
        else if (CurrentDesc[1] == USB_INTERFACE_DESCRIPTOR_TYPE ||
                 CurrentDesc[1] == USB_ENDPOINT_DESCRIPTOR_TYPE)
        {
            /* Reached next interface or endpoints, stop scanning */
            break;
        }

        /* Move to next descriptor */
        if (CurrentDesc[0] == 0)
        {
            break;
        }
        CurrentDesc += CurrentDesc[0];
    }

    DPRINT1("USBRNDIS: CDC Ethernet Descriptor not found, will use generated MAC\n");
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
                RndisUsbFindCdcEthernetDescriptor(Adapter, InterfaceDesc, DescEnd);
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
                RndisUsbFindCdcEthernetDescriptor(Adapter, InterfaceDesc, DescEnd);
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
                Adapter->InterruptEndpoint.MaxPacketSize = Pipe->MaximumPacketSize;
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
     * The interrupt endpoint (if present) is used for CDC notifications
     * such as NETWORK_CONNECTION. RNDIS RESPONSE_AVAILABLE is still
     * handled via polling in RndisCommand().
     */

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
 * RndisUsbSetEthernetPacketFilter
 *
 * Send CDC-ECM/NCM SET_ETHERNET_PACKET_FILTER request.
 */
NTSTATUS
RndisUsbSetEthernetPacketFilter(
    IN PRNDIS_ADAPTER Adapter,
    IN USHORT PacketFilter)
{
    PURB Urb;
    NTSTATUS Status;

    DPRINT("USBRNDIS: CDC set packet filter 0x%04X\n", PacketFilter);

    Urb = RndisAllocateMemory(NonPagedPool, sizeof(URB));
    if (!Urb)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Urb->UrbControlVendorClassRequest.Hdr.Length =
        sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb->UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_CLASS_INTERFACE;
    Urb->UrbControlVendorClassRequest.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
    Urb->UrbControlVendorClassRequest.TransferBufferLength = 0;
    Urb->UrbControlVendorClassRequest.TransferBuffer = NULL;
    Urb->UrbControlVendorClassRequest.TransferBufferMDL = NULL;
    Urb->UrbControlVendorClassRequest.Request = USB_CDC_SET_ETHERNET_PACKET_FILTER;
    Urb->UrbControlVendorClassRequest.Value = PacketFilter;
    Urb->UrbControlVendorClassRequest.Index = Adapter->ControlInterfaceNumber;

    Status = RndisSyncUrbRequest(Adapter->LowerDeviceObject, Urb);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: SET_ETHERNET_PACKET_FILTER failed (0x%08X)\n", Status);
    }

    RndisFreeMemory(Urb);
    return Status;
}

/*
 * RndisUsbSetEthernetMulticastFilters
 *
 * Send CDC-ECM/NCM SET_ETHERNET_MULTICAST_FILTERS request.
 */
NTSTATUS
RndisUsbSetEthernetMulticastFilters(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR MulticastList,
    IN USHORT AddressCount)
{
    PURB Urb;
    NTSTATUS Status;
    ULONG BufferLength;
    PUCHAR Buffer;

    BufferLength = (ULONG)AddressCount * ETH_LENGTH_OF_ADDRESS;
    Buffer = NULL;

    if (BufferLength > 0)
    {
        Buffer = RndisAllocateMemory(NonPagedPool, BufferLength);
        if (!Buffer)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }
        RtlCopyMemory(Buffer, MulticastList, BufferLength);
    }

    Urb = RndisAllocateMemory(NonPagedPool, sizeof(URB));
    if (!Urb)
    {
        if (Buffer)
        {
            RndisFreeMemory(Buffer);
        }
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Urb->UrbControlVendorClassRequest.Hdr.Length =
        sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb->UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_CLASS_INTERFACE;
    Urb->UrbControlVendorClassRequest.TransferFlags = USBD_TRANSFER_DIRECTION_OUT;
    Urb->UrbControlVendorClassRequest.TransferBufferLength = BufferLength;
    Urb->UrbControlVendorClassRequest.TransferBuffer = Buffer;
    Urb->UrbControlVendorClassRequest.TransferBufferMDL = NULL;
    Urb->UrbControlVendorClassRequest.Request = USB_CDC_SET_ETHERNET_MULTICAST_FILTERS;
    Urb->UrbControlVendorClassRequest.Value = AddressCount;
    Urb->UrbControlVendorClassRequest.Index = Adapter->ControlInterfaceNumber;

    Status = RndisSyncUrbRequest(Adapter->LowerDeviceObject, Urb);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: SET_ETHERNET_MULTICAST_FILTERS failed (0x%08X)\n", Status);
    }

    RndisFreeMemory(Urb);
    if (Buffer)
    {
        RndisFreeMemory(Buffer);
    }

    return Status;
}

/*
 * RndisUsbGetEthernetStatistic
 *
 * Query a CDC-ECM/NCM device for a specific Ethernet statistic.
 * Per USB CDC ECM 1.2 specification, section 6.2.4.
 *
 * Parameters:
 *   Adapter - Adapter context
 *   FeatureSelector - Statistic to query (CDC_ECM_STAT_*)
 *   StatisticValue - Output: 32-bit statistic value from device
 *
 * Returns:
 *   STATUS_SUCCESS on success
 *   STATUS_NOT_SUPPORTED if the device doesn't support the statistic
 *   Error status on failure
 *
 * Note: Not all CDC-ECM/NCM devices support this request or all statistics.
 *       The bmEthernetStatistics field in the CDC Ethernet descriptor
 *       indicates which statistics are supported.
 */
NTSTATUS
RndisUsbGetEthernetStatistic(
    IN PRNDIS_ADAPTER Adapter,
    IN USHORT FeatureSelector,
    OUT PULONG StatisticValue)
{
    PURB Urb;
    NTSTATUS Status;
    PULONG ResultBuffer;

    /* Allocate a buffer for the 4-byte result */
    ResultBuffer = RndisAllocateMemory(NonPagedPool, sizeof(ULONG));
    if (!ResultBuffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    *ResultBuffer = 0;

    Urb = RndisAllocateMemory(NonPagedPool, sizeof(URB));
    if (!Urb)
    {
        RndisFreeMemory(ResultBuffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Build GET_ETHERNET_STATISTIC request.
     * Request type: Class, Interface, Device-to-Host
     * wValue: Feature selector (statistic type)
     * wIndex: Interface number
     * wLength: 4 (size of ULONG statistic)
     */
    Urb->UrbControlVendorClassRequest.Hdr.Length =
        sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb->UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_CLASS_INTERFACE;
    Urb->UrbControlVendorClassRequest.TransferFlags = USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK;
    Urb->UrbControlVendorClassRequest.TransferBufferLength = sizeof(ULONG);
    Urb->UrbControlVendorClassRequest.TransferBuffer = ResultBuffer;
    Urb->UrbControlVendorClassRequest.TransferBufferMDL = NULL;
    Urb->UrbControlVendorClassRequest.Request = USB_CDC_GET_ETHERNET_STATISTIC;
    Urb->UrbControlVendorClassRequest.Value = FeatureSelector;
    Urb->UrbControlVendorClassRequest.Index = Adapter->ControlInterfaceNumber;

    Status = RndisSyncUrbRequest(Adapter->LowerDeviceObject, Urb);

    if (NT_SUCCESS(Status))
    {
        *StatisticValue = *ResultBuffer;
        DPRINT("USBRNDIS: GET_ETHERNET_STATISTIC selector=%u value=%lu\n",
               FeatureSelector, *StatisticValue);
    }
    else
    {
        /*
         * Many devices don't support this request or specific statistics.
         * STALL response typically means not supported.
         */
        DPRINT("USBRNDIS: GET_ETHERNET_STATISTIC selector=%u failed (0x%08X)\n",
               FeatureSelector, Status);
        *StatisticValue = 0;
    }

    RndisFreeMemory(Urb);
    RndisFreeMemory(ResultBuffer);
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
 *
 * NAPI-style: Reset budget at start of DPC to allow fresh burst.
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
        /* Reset RX budget for fresh polling burst */
        InterlockedExchange(&Adapter->RxHot.RxBudgetRemaining, RX_BUDGET_PACKETS);
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
    InterlockedExchange((PLONG)&Adapter->RxHot.RxConsecutiveErrors, 0);

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

    /* Clear the coalescing flag */
    InterlockedExchange(&Adapter->RxDelayScheduled, 0);

    /* Resubmit RX after delay, but only if not already submitted */
    if (!Adapter->Halting && !Adapter->RxHot.RxSubmitted)
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
    Adapter->RxDelayScheduled = 0;
}

static
VOID
NTAPI
RndisInterruptResubmitDpc(
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
        RndisUsbSubmitInterruptRead(Adapter);
    }
}

/*
 * RndisInterruptDelayDpc
 *
 * DPC routine called when interrupt delay timer expires.
 * This provides a small delay before resubmitting the interrupt IN
 * transfer after a NAK or transient error, preventing CPU spinning.
 */
static
VOID
NTAPI
RndisInterruptDelayDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    /* Clear the coalescing flag */
    InterlockedExchange(&Adapter->InterruptDelayScheduled, 0);

    /* Resubmit interrupt read after delay */
    if (!Adapter->Halting && !Adapter->InterruptSubmitted)
    {
        RndisUsbSubmitInterruptRead(Adapter);
    }
}

VOID
RndisInitializeInterruptDpc(
    IN PRNDIS_ADAPTER Adapter)
{
    KeInitializeDpc(&Adapter->InterruptResubmitDpc, RndisInterruptResubmitDpc, Adapter);
    KeInitializeTimer(&Adapter->InterruptDelayTimer);
    KeInitializeDpc(&Adapter->InterruptDelayDpc, RndisInterruptDelayDpc, Adapter);
    Adapter->InterruptDelayScheduled = 0;
    Adapter->InterruptConsecutiveErrors = 0;
}

static
VOID
NTAPI
RndisTxResubmitDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    InterlockedExchange(&Adapter->TxHot.TxResubmitScheduled, 0);

    if (Adapter->Halting)
    {
        return;
    }

    if (Adapter->TxHot.TxNcmPartialNbl != NULL && Adapter->IsCdcNcm)
    {
        RndisNcmContinueTx(Adapter);
        return;
    }

    RndisTxDequeueAndSend(Adapter);
}

VOID
RndisInitializeTxDpc(
    IN PRNDIS_ADAPTER Adapter)
{
    KeInitializeDpc(&Adapter->TxResubmitDpc, RndisTxResubmitDpc, Adapter);
    Adapter->TxHot.TxResubmitScheduled = 0;
}

static
VOID
RndisIndicateLinkState(
    IN PRNDIS_ADAPTER Adapter)
{
    NDIS_STATUS_INDICATION Indication;
    NDIS_LINK_STATE LinkState;

    NdisZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    LinkState.MediaConnectState = Adapter->MediaState;
    LinkState.MediaDuplexState = MediaDuplexStateFull;
    LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
    LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
    LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;

    NdisZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(NDIS_STATUS_INDICATION);
    Indication.SourceHandle = Adapter->MiniportAdapterHandle;
    Indication.StatusCode = NDIS_STATUS_LINK_STATE;
    Indication.StatusBuffer = &LinkState;
    Indication.StatusBufferSize = sizeof(LinkState);

    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);
}

/*
 * RndisInterruptComplete
 *
 * Completion routine for async interrupt IN URB (CDC notifications).
 */
static
NTSTATUS
NTAPI
RndisInterruptComplete(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    IN PVOID Context)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)Context;
    NTSTATUS Status;
    ULONG TransferLength;

    UNREFERENCED_PARAMETER(DeviceObject);

    Status = Irp->IoStatus.Status;
    TransferLength = Adapter->InterruptUrb.UrbBulkOrInterruptTransfer.TransferBufferLength;

    NdisAcquireSpinLock(&Adapter->InterruptLock);
    Adapter->InterruptIrp = NULL;
    Adapter->InterruptSubmitted = FALSE;
    NdisReleaseSpinLock(&Adapter->InterruptLock);

    RndisMaybeDeferIrpFree(Adapter, Irp, &Adapter->InterruptIrpToFree);

    if (NT_SUCCESS(Status) && TransferLength >= sizeof(USB_CDC_NOTIFICATION))
    {
        PUSB_CDC_NOTIFICATION Notification;
        UCHAR NotifyType;

        Notification = (PUSB_CDC_NOTIFICATION)Adapter->InterruptBuffer;
        NotifyType = Notification->bNotificationType;

        switch (NotifyType)
        {
            case USB_CDC_NOTIFICATION_NETWORK_CONNECTION:
            {
                NDIS_MEDIA_CONNECT_STATE NewState;

                NewState = (Notification->wValue != 0) ?
                           MediaConnectStateConnected :
                           MediaConnectStateDisconnected;

                DPRINT1("USBRNDIS: CDC NETWORK_CONNECTION notification: %s\n",
                        (NewState == MediaConnectStateConnected) ? "Connected" : "Disconnected");

                if (Adapter->MediaState != NewState)
                {
                    Adapter->MediaState = NewState;
                    RndisIndicateLinkState(Adapter);
                }
                break;
            }

            case USB_CDC_NOTIFICATION_CONNECTION_SPEED_CHANGE:
            {
                /*
                 * CONNECTION_SPEED_CHANGE (0x2A) notification contains
                 * download and upload bit rates following the 8-byte header.
                 * Per USB CDC ECM 1.2 specification, Table 6.
                 */
                if (TransferLength >= sizeof(USB_CDC_NOTIFICATION) + sizeof(USB_CDC_SPEED_CHANGE))
                {
                    PUSB_CDC_SPEED_CHANGE SpeedChange;
                    ULONG64 NewLinkSpeed;

                    SpeedChange = (PUSB_CDC_SPEED_CHANGE)(Adapter->InterruptBuffer + sizeof(USB_CDC_NOTIFICATION));

                    /*
                     * Use download (DL) bit rate as link speed.
                     * NDIS 6.x uses bps units for link speed.
                     */
                    NewLinkSpeed = SpeedChange->DLBitRate;

                    DPRINT1("USBRNDIS: CDC SPEED_CHANGE notification: DL=%lu bps, UL=%lu bps\n",
                            SpeedChange->DLBitRate, SpeedChange->ULBitRate);

                    if (Adapter->LinkSpeed != NewLinkSpeed)
                    {
                        Adapter->LinkSpeed = NewLinkSpeed;
                        RndisIndicateLinkState(Adapter);
                    }
                }
                else
                {
                    DPRINT1("USBRNDIS: CDC SPEED_CHANGE notification too short (%lu bytes)\n",
                            TransferLength);
                }
                break;
            }

            case USB_CDC_NOTIFICATION_RESPONSE_AVAILABLE:
                /*
                 * RESPONSE_AVAILABLE (0x01) indicates an RNDIS response is ready.
                 * This is handled via polling in RndisCommand() for RNDIS devices.
                 * For CDC-ECM/NCM, this notification is not expected.
                 */
                DPRINT("USBRNDIS: CDC RESPONSE_AVAILABLE notification\n");
                break;

            default:
                DPRINT1("USBRNDIS: Unknown CDC notification type 0x%02X\n", NotifyType);
                break;
        }
    }

    RndisDecrementPendingIo(Adapter);

    if (!Adapter->Halting && Status != STATUS_CANCELLED)
    {
        KeInsertQueueDpc(&Adapter->InterruptResubmitDpc, NULL, NULL);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
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
    TransferLength = Adapter->RxHot.RxUrb.UrbBulkOrInterruptTransfer.TransferBufferLength;

    /* Clear RX IRP pointer under lock */
    NdisAcquireSpinLock(&Adapter->RxLock);
    Adapter->RxHot.RxIrp = NULL;
    Adapter->RxHot.RxSubmitted = FALSE;
    NdisReleaseSpinLock(&Adapter->RxLock);

    /* Free or defer IRP based on halt state */
    RndisMaybeDeferIrpFree(Adapter, Irp, &Adapter->RxIrpToFree);

    /* Process received data if successful and not halting */
    if (NT_SUCCESS(Status) && TransferLength > 0 && !Adapter->Halting)
    {
        LONG BudgetRemaining;

        Adapter->RxHot.RxConsecutiveErrors = 0;  /* Reset error counter on success */

        /*
         * NAPI-style budget tracking: Decrement budget for each received packet.
         * For NCM, one USB transfer may contain multiple datagrams, but we count
         * each USB completion as one budget unit since RndisProcessReceivedPacket
         * handles the demux internally.
         */
        BudgetRemaining = InterlockedDecrement(&Adapter->RxHot.RxBudgetRemaining);

        RndisProcessReceivedPacket(Adapter, Adapter->RxHot.RxBuffer, TransferLength);

        /*
         * If budget exhausted, use delayed resubmit to yield to other work.
         * Reset budget when deferring to allow fresh burst on next DPC.
         */
        if (BudgetRemaining <= 0)
        {
            InterlockedExchange(&Adapter->RxHot.RxBudgetRemaining, RX_BUDGET_PACKETS);
            UseDelayedResubmit = TRUE;
        }
        /* Data received and budget ok - use immediate resubmission for low latency */
    }
    else if (NT_SUCCESS(Status) && TransferLength == 0)
    {
        /* Zero-length transfer - not an error, just no data */
        Adapter->RxHot.RxConsecutiveErrors = 0;
        /* Use delayed resubmission to avoid tight loop */
        UseDelayedResubmit = TRUE;
    }
    else if (!NT_SUCCESS(Status) && Status != STATUS_CANCELLED)
    {
        USBD_STATUS UsbdStatus = Adapter->RxHot.RxUrb.UrbBulkOrInterruptTransfer.Hdr.Status;

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
            Adapter->RxHot.RxConsecutiveErrors = 0;  /* Reset counter for NAKs */
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
            ConsecutiveErrors = InterlockedIncrement((PLONG)&Adapter->RxHot.RxConsecutiveErrors);
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
            /*
             * Delay 20ms before resubmitting - prevents CPU spin on NAK.
             * Use InterlockedExchange to coalesce multiple timer requests
             * and prevent timer/DPC churn under persistent NAK.
             */
            if (InterlockedExchange(&Adapter->RxDelayScheduled, 1) == 0)
            {
                LARGE_INTEGER DueTime;
                DueTime.QuadPart = -200000LL; /* 20ms in 100ns units, negative = relative */
                KeSetTimer(&Adapter->RxDelayTimer, DueTime, &Adapter->RxDelayDpc);
            }
        }
        else
        {
            /*
             * Immediate resubmission via DPC for data reception.
             * Set DPC affinity to current CPU to keep RX processing
             * on the same CPU as the completion for better cache locality.
             */
            CCHAR CpuNumber = (CCHAR)KeGetCurrentProcessorNumber();
            KeSetTargetProcessorDpc(&Adapter->RxResubmitDpc, CpuNumber);
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
    if (Adapter->RxHot.RxSubmitted)
    {
        NdisReleaseSpinLock(&Adapter->RxLock);
        return STATUS_SUCCESS; /* Already pending */
    }
    Adapter->RxHot.RxSubmitted = TRUE;
    NdisReleaseSpinLock(&Adapter->RxLock);

    /* Build the URB */
    Urb = &Adapter->RxHot.RxUrb;
    NdisZeroMemory(Urb, sizeof(URB));

    UsbBuildInterruptOrBulkTransferRequest(
        Urb,
        sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
        Adapter->BulkInEndpoint.PipeHandle,
        Adapter->RxHot.RxBuffer,
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
        if (Adapter->RxHot.RxSubmitted)
        {
            Adapter->RxHot.RxIrp = Irp;
        }
        /* else completion already ran, IRP is freed, don't store */
        NdisReleaseSpinLock(&Adapter->RxLock);
    }
    else if (!NT_SUCCESS(Status))
    {
        /* Failed to submit - completion won't run, clean up ourselves */
        NdisAcquireSpinLock(&Adapter->RxLock);
        Adapter->RxHot.RxSubmitted = FALSE;
        NdisReleaseSpinLock(&Adapter->RxLock);
    }
    /* else STATUS_SUCCESS: completion already ran (sync completion) */

    return Status;
}

/*
 * RndisUsbSubmitInterruptRead
 *
 * Submit an async interrupt IN URB for CDC notifications.
 */
NTSTATUS
RndisUsbSubmitInterruptRead(
    IN PRNDIS_ADAPTER Adapter)
{
    PURB Urb;
    NTSTATUS Status;
    PIRP Irp;

    if (!Adapter->InterruptEndpoint.PipeHandle || !Adapter->InterruptBuffer)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Adapter->Halting)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    NdisAcquireSpinLock(&Adapter->InterruptLock);
    if (Adapter->InterruptSubmitted)
    {
        NdisReleaseSpinLock(&Adapter->InterruptLock);
        return STATUS_SUCCESS;
    }
    Adapter->InterruptSubmitted = TRUE;
    NdisReleaseSpinLock(&Adapter->InterruptLock);

    Urb = &Adapter->InterruptUrb;
    NdisZeroMemory(Urb, sizeof(URB));

    UsbBuildInterruptOrBulkTransferRequest(
        Urb,
        sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
        Adapter->InterruptEndpoint.PipeHandle,
        Adapter->InterruptBuffer,
        NULL,
        Adapter->InterruptBufferLength,
        USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
        NULL);

    Status = RndisAsyncUrbRequest(Adapter, Urb, RndisInterruptComplete, Adapter, &Irp);
    if (Status == STATUS_PENDING)
    {
        NdisAcquireSpinLock(&Adapter->InterruptLock);
        if (Adapter->InterruptSubmitted)
        {
            Adapter->InterruptIrp = Irp;
        }
        NdisReleaseSpinLock(&Adapter->InterruptLock);
    }
    else if (!NT_SUCCESS(Status))
    {
        NdisAcquireSpinLock(&Adapter->InterruptLock);
        Adapter->InterruptSubmitted = FALSE;
        NdisReleaseSpinLock(&Adapter->InterruptLock);
    }

    return Status;
}

/*
 * RndisTxComplete
 *
 * Completion routine for async TX URB.
 * Called at DISPATCH_LEVEL when the USB stack completes the bulk OUT request.
 * NDIS 6.x version using NET_BUFFER_LIST.
 *
 * For NCM multi-datagram batching, the pending NBL chain may contain
 * multiple NBLs that were batched into a single NTB.
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
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST CurrentNbl;
    PNET_BUFFER_LIST PartialNbl;
    NDIS_STATUS NdisStatus;
    ULONG SendCompleteFlags;
    ULONG NblCount;
    BOOLEAN HasPartial;

    UNREFERENCED_PARAMETER(DeviceObject);

    Status = Irp->IoStatus.Status;
    /* Retrieve the pending NBL chain and clear TX state under lock */
    NdisAcquireSpinLock(&Adapter->TxLock);
    Nbl = Adapter->TxHot.PendingTxNbl;
    NblCount = Adapter->TxHot.PendingTxNblCount;
    PartialNbl = Adapter->TxHot.TxNcmPartialNbl;
    Adapter->TxHot.PendingTxNbl = NULL;
    Adapter->TxHot.PendingTxNblCount = 0;
    Adapter->TxHot.TxIrp = NULL;
    HasPartial = (PartialNbl != NULL);
    Adapter->TxHot.TxBusy = HasPartial ? TRUE : FALSE;
    NdisReleaseSpinLock(&Adapter->TxLock);

    /* Free or defer IRP based on halt state */
    RndisMaybeDeferIrpFree(Adapter, Irp, &Adapter->TxIrpToFree);

    /*
     * For NCM segmentation, an NTB may carry partial data from a single NBL.
     * In that case, don't complete any NBLs yet; just continue sending.
     */
    if (Nbl == NULL && HasPartial)
    {
        if (NT_SUCCESS(Status) && !Adapter->Halting)
        {
            if (InterlockedExchange(&Adapter->TxHot.TxResubmitScheduled, 1) == 0)
            {
                /* Set DPC affinity to current CPU for cache locality */
                CCHAR CpuNumber = (CCHAR)KeGetCurrentProcessorNumber();
                KeSetTargetProcessorDpc(&Adapter->TxResubmitDpc, CpuNumber);
                KeInsertQueueDpc(&Adapter->TxResubmitDpc, NULL, NULL);
            }
        }
        else
        {
            /* Fail the partial NBL on error */
            NdisAcquireSpinLock(&Adapter->TxLock);
            Adapter->TxHot.TxNcmPartialNbl = NULL;
            Adapter->TxHot.TxNcmPartialNb = NULL;
            Adapter->TxHot.TxBusy = FALSE;
            NdisReleaseSpinLock(&Adapter->TxLock);

            NET_BUFFER_LIST_STATUS(PartialNbl) = NDIS_STATUS_FAILURE;
            NdisMSendNetBufferListsComplete(
                Adapter->MiniportAdapterHandle,
                PartialNbl,
                NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);
        }

        RndisDecrementPendingIo(Adapter);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    /* Update statistics and determine NDIS status */
    if (NT_SUCCESS(Status))
    {
        DPRINT("USBRNDIS: TX complete, %lu packet(s) sent successfully\n", NblCount ? NblCount : 1);
        /*
         * For NCM batching, update TxOkCount for each NBL in the batch.
         * If NblCount is 0, it's a non-batched send (single NBL).
         */
        if (NblCount > 1)
        {
            Adapter->TxOkCount += NblCount;
        }
        else
        {
            Adapter->TxOkCount++;
        }
        NdisStatus = NDIS_STATUS_SUCCESS;
    }
    else
    {
        DPRINT1("USBRNDIS: TX failed with status 0x%08X\n", Status);
        if (NblCount > 1)
        {
            Adapter->TxErrorCount += NblCount;
        }
        else
        {
            Adapter->TxErrorCount++;
        }
        NdisStatus = NDIS_STATUS_FAILURE;
    }

    /*
     * Always complete owned NBLs here. Completion may be synchronous or
     * asynchronous; SendNetBufferLists does not complete on its own.
     */
    SendCompleteFlags = (KeGetCurrentIrql() == DISPATCH_LEVEL) ?
                        NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0;

    if (Nbl)
    {
        /* Set status on all NBLs in the chain */
        for (CurrentNbl = Nbl; CurrentNbl != NULL; CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NdisStatus;
        }

        /* Complete the entire chain */
        NdisMSendNetBufferListsComplete(
            Adapter->MiniportAdapterHandle,
            Nbl,
            SendCompleteFlags);
    }

    /* Kick queued sends if any remain using lock-free queue */
    if (!Adapter->Halting)
    {
        BOOLEAN ShouldKick = FALSE;

        NdisAcquireSpinLock(&Adapter->TxLock);
        if (!RndisTxQueueIsEmpty(Adapter) && Adapter->TxHot.TxNcmPartialNbl == NULL)
        {
            ShouldKick = TRUE;
        }
        NdisReleaseSpinLock(&Adapter->TxLock);

        if (ShouldKick)
        {
            RndisTxKick(Adapter);
        }
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
    Urb = &Adapter->TxHot.TxUrb;
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
        if (Adapter->TxHot.TxBusy)
        {
            Adapter->TxHot.TxIrp = Irp;
        }
        /* else completion already ran, IRP is freed, don't store */
        NdisReleaseSpinLock(&Adapter->TxLock);
    }
    /* else sync completion: completion already ran */

    return Status;
}
