/*
 * PROJECT:     ReactOS USB RNDIS Network Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NDIS OID query/set handler
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This file handles NDIS OID requests from the network stack.
 * Many OIDs can be answered locally from cached information,
 * while others are forwarded to the RNDIS device.
 */

#include "usbrndis.h"

#define NDEBUG
#include <debug.h>

/* Supported OID list */
static const NDIS_OID SupportedOidList[] = {
    /* General OIDs */
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_MAXIMUM_SEND_PACKETS,

    /* Statistics OIDs */
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,

    /* 802.3 (Ethernet) OIDs */
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,
};

/* Vendor description */
static const CHAR VendorDescription[] = "USB RNDIS Network Adapter";

/*
 * RndisQueryInformation
 *
 * NDIS miniport query information handler
 */
NDIS_STATUS
NTAPI
RndisQueryInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesWritten,
    OUT PULONG BytesNeeded)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    PVOID SourceBuffer = NULL;
    ULONG SourceLength = 0;
    ULONG GenericUlong;
    USHORT GenericUshort;
    NDIS_MEDIUM Medium = NdisMedium802_3;
    NDIS_HARDWARE_STATUS HardwareStatus = NdisHardwareStatusReady;

    DPRINT("USBRNDIS: QueryInformation OID 0x%08X\n", Oid);

    *BytesWritten = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        /* === General Required OIDs === */

        case OID_GEN_SUPPORTED_LIST:
            SourceBuffer = (PVOID)SupportedOidList;
            SourceLength = sizeof(SupportedOidList);
            break;

        case OID_GEN_HARDWARE_STATUS:
            if (Adapter->State >= RndisStateInitialized)
            {
                HardwareStatus = NdisHardwareStatusReady;
            }
            else
            {
                HardwareStatus = NdisHardwareStatusNotReady;
            }
            SourceBuffer = &HardwareStatus;
            SourceLength = sizeof(HardwareStatus);
            break;

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            SourceBuffer = &Medium;
            SourceLength = sizeof(Medium);
            break;

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:
            GenericUlong = ETHERNET_MAX_FRAME_SIZE - ETHERNET_HEADER_SIZE;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            GenericUlong = ETHERNET_MAX_MTU;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_LINK_SPEED:
            GenericUlong = Adapter->LinkSpeed;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
            GenericUlong = ETHERNET_MAX_FRAME_SIZE;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_RECEIVE_BUFFER_SPACE:
            GenericUlong = RNDIS_MAX_TRANSFER_SIZE;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            GenericUlong = 1;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_VENDOR_ID:
            /*
             * NDIS expects a 24-bit OUI (Organizationally Unique Identifier).
             * Try to extract from MAC address OUI (first 3 bytes).
             * If MAC address is not set, return 0x00FFFFFF (unknown vendor).
             */
            if (Adapter->PermanentMacAddress[0] != 0 ||
                Adapter->PermanentMacAddress[1] != 0 ||
                Adapter->PermanentMacAddress[2] != 0)
            {
                GenericUlong = (Adapter->PermanentMacAddress[0] << 16) |
                               (Adapter->PermanentMacAddress[1] << 8) |
                               Adapter->PermanentMacAddress[2];
            }
            else
            {
                /* Unknown vendor OUI */
                GenericUlong = 0x00FFFFFF;
            }
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_VENDOR_DESCRIPTION:
            SourceBuffer = (PVOID)VendorDescription;
            SourceLength = sizeof(VendorDescription);
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            GenericUlong = Adapter->PacketFilter;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_DRIVER_VERSION:
            GenericUshort = (NDIS_MINIPORT_MAJOR_VERSION << 8) | NDIS_MINIPORT_MINOR_VERSION;
            SourceBuffer = &GenericUshort;
            SourceLength = sizeof(GenericUshort);
            break;

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
            GenericUlong = ETHERNET_MAX_FRAME_SIZE;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_MAC_OPTIONS:
            GenericUlong = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                           NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                           NDIS_MAC_OPTION_NO_LOOPBACK;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            GenericUlong = Adapter->MediaState;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            GenericUlong = 1;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        /* === Statistics OIDs === */

        case OID_GEN_XMIT_OK:
            GenericUlong = (ULONG)Adapter->TxOkCount;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_RCV_OK:
            GenericUlong = (ULONG)Adapter->RxOkCount;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_XMIT_ERROR:
            GenericUlong = (ULONG)Adapter->TxErrorCount;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_RCV_ERROR:
            GenericUlong = (ULONG)Adapter->RxErrorCount;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_GEN_RCV_NO_BUFFER:
            GenericUlong = (ULONG)Adapter->RxNoBufferCount;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        /* === 802.3 (Ethernet) OIDs === */

        case OID_802_3_PERMANENT_ADDRESS:
            SourceBuffer = Adapter->PermanentMacAddress;
            SourceLength = ETHERNET_ADDRESS_LENGTH;
            break;

        case OID_802_3_CURRENT_ADDRESS:
            SourceBuffer = Adapter->CurrentMacAddress;
            SourceLength = ETHERNET_ADDRESS_LENGTH;
            break;

        case OID_802_3_MULTICAST_LIST:
            SourceBuffer = Adapter->MulticastList;
            SourceLength = Adapter->MulticastListCount * ETHERNET_ADDRESS_LENGTH;
            break;

        case OID_802_3_MAXIMUM_LIST_SIZE:
            GenericUlong = RNDIS_MAX_MULTICAST_ADDRESSES;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        case OID_802_3_RCV_ERROR_ALIGNMENT:
        case OID_802_3_XMIT_ONE_COLLISION:
        case OID_802_3_XMIT_MORE_COLLISIONS:
            /* These are typically 0 for USB devices */
            GenericUlong = 0;
            SourceBuffer = &GenericUlong;
            SourceLength = sizeof(GenericUlong);
            break;

        default:
            DPRINT1("USBRNDIS: Unsupported OID 0x%08X\n", Oid);
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    if (Status == NDIS_STATUS_SUCCESS && SourceBuffer && SourceLength > 0)
    {
        if (InformationBufferLength < SourceLength)
        {
            *BytesNeeded = SourceLength;
            Status = NDIS_STATUS_BUFFER_TOO_SHORT;
        }
        else
        {
            NdisMoveMemory(InformationBuffer, SourceBuffer, SourceLength);
            *BytesWritten = SourceLength;
        }
    }

    return Status;
}

/*
 * RndisSetInformation
 *
 * NDIS miniport set information handler
 */
NDIS_STATUS
NTAPI
RndisSetInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesRead,
    OUT PULONG BytesNeeded)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    ULONG PacketFilter;
    NTSTATUS NtStatus;

    DPRINT("USBRNDIS: SetInformation OID 0x%08X\n", Oid);

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            NdisMoveMemory(&PacketFilter, InformationBuffer, sizeof(ULONG));
            DPRINT("USBRNDIS: Setting packet filter to 0x%08X\n", PacketFilter);

            /* Set filter on device */
            NtStatus = RndisSetPacketFilter(Adapter, PacketFilter);
            if (!NT_SUCCESS(NtStatus))
            {
                DPRINT1("USBRNDIS: Failed to set packet filter (0x%08X)\n", NtStatus);
                Status = NDIS_STATUS_FAILURE;
            }
            else
            {
                *BytesRead = sizeof(ULONG);
            }
            break;

        case OID_GEN_CURRENT_LOOKAHEAD:
            /* Accept any lookahead value */
            if (InformationBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
            }
            else
            {
                *BytesRead = sizeof(ULONG);
            }
            break;

        case OID_802_3_MULTICAST_LIST:
            if (InformationBufferLength % ETHERNET_ADDRESS_LENGTH != 0)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            if (InformationBufferLength / ETHERNET_ADDRESS_LENGTH > RNDIS_MAX_MULTICAST_ADDRESSES)
            {
                Status = NDIS_STATUS_MULTICAST_FULL;
                break;
            }

            /* Copy multicast list */
            NdisZeroMemory(Adapter->MulticastList, sizeof(Adapter->MulticastList));
            Adapter->MulticastListCount = InformationBufferLength / ETHERNET_ADDRESS_LENGTH;
            NdisMoveMemory(Adapter->MulticastList, InformationBuffer, InformationBufferLength);

            /* Optionally set on device */
            RndisSetOid(Adapter, RNDIS_OID_802_3_MULTICAST_LIST,
                        Adapter->MulticastList,
                        Adapter->MulticastListCount * ETHERNET_ADDRESS_LENGTH);

            *BytesRead = InformationBufferLength;
            break;

        default:
            DPRINT1("USBRNDIS: Unsupported SET OID 0x%08X\n", Oid);
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    return Status;
}
