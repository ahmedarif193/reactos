/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Miniport information callbacks
 * COPYRIGHT:   2013 Cameron Gutman (cameron.gutman@reactos.org)
 *              2018 Mark Jansen (mark.jansen@reactos.org)
 *              2024 ReactOS Team - Power management and improved statistics
 */

#include "nic.h"

#include <debug.h>

static NDIS_OID SupportedOidList[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_802_3_MULTICAST_LIST,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_GEN_PHYSICAL_MEDIUM,

    /* Statistics */
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,

    /* Power Management */
    OID_PNP_CAPABILITIES,
    OID_PNP_SET_POWER,
    OID_PNP_QUERY_POWER,
    OID_PNP_ENABLE_WAKE_UP,

    /* Task Offload - for future checksum offload reporting */
    /* OID_TCP_TASK_OFFLOAD - Add when full checksum offload OID support is needed */
};

static
ULONG64
NICQueryStatisticCounter(
    _In_ PE1000_ADAPTER Adapter,
    _In_ NDIS_OID Oid)
{
    /* Update statistics from hardware before returning */
    NICUpdateStatistics(Adapter);

    switch (Oid)
    {
    case OID_GEN_XMIT_OK:
        return Adapter->Statistics.TxPackets;

    case OID_GEN_RCV_OK:
        return Adapter->Statistics.RxPackets;

    case OID_GEN_XMIT_ERROR:
        return Adapter->Statistics.TxErrors;

    case OID_GEN_RCV_ERROR:
        return Adapter->Statistics.RxErrors;

    case OID_GEN_RCV_NO_BUFFER:
        return Adapter->Statistics.RxNoBuffer;

    default:
        return 0;
    }
}

static
NDIS_STATUS
NICFillPowerManagementCapabilities(
    _In_ PE1000_ADAPTER Adapter,
    _Out_ PNDIS_PNP_CAPABILITIES Capabilities)
{
    PNDIS_PM_WAKE_UP_CAPABILITIES WakeUpCaps;

    RtlZeroMemory(Capabilities, sizeof(NDIS_PNP_CAPABILITIES));

    WakeUpCaps = &Capabilities->WakeUpCapabilities;

    /* Report power management capabilities */
    WakeUpCaps->MinMagicPacketWakeUp = NdisDeviceStateD3;  /* Magic packet supported from D3 */
    WakeUpCaps->MinPatternWakeUp = NdisDeviceStateUnspecified;  /* Pattern match not currently supported */
    WakeUpCaps->MinLinkChangeWakeUp = NdisDeviceStateD3;  /* Link change wake from D3 */

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
MiniportQueryInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesWritten,
    OUT PULONG BytesNeeded)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    ULONG copyLength;
    PVOID copySource;
    NDIS_STATUS status;
    union _GENERIC_INFORMATION
    {
        USHORT Ushort;
        ULONG Ulong;
        ULONG64 Ulong64;
        NDIS_MEDIUM Medium;
        NDIS_PNP_CAPABILITIES PmCapabilities;
        NDIS_DEVICE_POWER_STATE PowerState;
    } GenericInfo;

    status = NDIS_STATUS_SUCCESS;
    copySource = &GenericInfo;
    copyLength = sizeof(ULONG);

    switch (Oid)
    {
    case OID_GEN_SUPPORTED_LIST:
        copySource = (PVOID)&SupportedOidList;
        copyLength = sizeof(SupportedOidList);
        break;

    case OID_GEN_CURRENT_PACKET_FILTER:
        GenericInfo.Ulong = Adapter->PacketFilter;
        break;

    case OID_GEN_HARDWARE_STATUS:
        GenericInfo.Ulong = (ULONG)NdisHardwareStatusReady;
        break;

    case OID_GEN_MEDIA_SUPPORTED:
    case OID_GEN_MEDIA_IN_USE:
    {
        GenericInfo.Medium = NdisMedium802_3;
        copyLength = sizeof(NDIS_MEDIUM);
        break;
    }

    case OID_GEN_RECEIVE_BLOCK_SIZE:
    case OID_GEN_TRANSMIT_BLOCK_SIZE:
    case OID_GEN_CURRENT_LOOKAHEAD:
    case OID_GEN_MAXIMUM_LOOKAHEAD:
    case OID_GEN_MAXIMUM_FRAME_SIZE:
        GenericInfo.Ulong = MAXIMUM_FRAME_SIZE - sizeof(ETH_HEADER);
        break;

    case OID_802_3_MULTICAST_LIST:
        copySource = Adapter->MulticastList;
        copyLength = Adapter->MulticastListSize * IEEE_802_ADDR_LENGTH;
        break;

    case OID_802_3_MAXIMUM_LIST_SIZE:
        GenericInfo.Ulong = MAXIMUM_MULTICAST_ADDRESSES;
        break;

    case OID_GEN_LINK_SPEED:
        GenericInfo.Ulong = Adapter->LinkSpeedMbps * 10000;
        break;

    case OID_GEN_TRANSMIT_BUFFER_SPACE:
        /* Report space based on descriptor ring size */
        GenericInfo.Ulong = MAXIMUM_FRAME_SIZE * NUM_TRANSMIT_DESCRIPTORS;
        break;

    case OID_GEN_RECEIVE_BUFFER_SPACE:
        /* Report space based on descriptor ring size */
        GenericInfo.Ulong = RECEIVE_BUFFER_SIZE * NUM_RECEIVE_DESCRIPTORS;
        break;

    case OID_GEN_VENDOR_ID:
        /* The 3 bytes of the MAC address is the vendor ID */
        GenericInfo.Ulong = 0;
        GenericInfo.Ulong |= (Adapter->PermanentMacAddress[0] << 16);
        GenericInfo.Ulong |= (Adapter->PermanentMacAddress[1] << 8);
        GenericInfo.Ulong |= (Adapter->PermanentMacAddress[2] & 0xFF);
        break;

    case OID_GEN_VENDOR_DESCRIPTION:
    {
        static UCHAR vendorDesc[] = "ReactOS Team";
        copySource = vendorDesc;
        copyLength = sizeof(vendorDesc);
        break;
    }

    case OID_GEN_VENDOR_DRIVER_VERSION:
        GenericInfo.Ulong = DRIVER_VERSION;
        break;

    case OID_GEN_DRIVER_VERSION:
    {
        copyLength = sizeof(USHORT);
        GenericInfo.Ushort = (NDIS_MINIPORT_MAJOR_VERSION << 8) + NDIS_MINIPORT_MINOR_VERSION;
        break;
    }

    case OID_GEN_MAXIMUM_TOTAL_SIZE:
        GenericInfo.Ulong = MAXIMUM_FRAME_SIZE;
        break;

    case OID_GEN_MAXIMUM_SEND_PACKETS:
        /*
         * Report the number of TX descriptors available for batch sends.
         * This is used by NDIS to determine how many packets can be
         * submitted in a single MiniportSendPackets call.
         */
        GenericInfo.Ulong = NUM_TRANSMIT_DESCRIPTORS;
        break;

    case OID_GEN_MAC_OPTIONS:
        GenericInfo.Ulong = NDIS_MAC_OPTION_RECEIVE_SERIALIZED |
            NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
            NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
            NDIS_MAC_OPTION_NO_LOOPBACK;
        break;

    case OID_GEN_MEDIA_CONNECT_STATUS:
        GenericInfo.Ulong = Adapter->MediaState;
        break;

    case OID_802_3_CURRENT_ADDRESS:
        copySource = Adapter->MulticastList[0].MacAddress;
        copyLength = IEEE_802_ADDR_LENGTH;
        break;

    case OID_802_3_PERMANENT_ADDRESS:
        copySource = Adapter->PermanentMacAddress;
        copyLength = IEEE_802_ADDR_LENGTH;
        break;

    case OID_GEN_XMIT_OK:
    case OID_GEN_RCV_OK:
    case OID_GEN_XMIT_ERROR:
    case OID_GEN_RCV_ERROR:
    case OID_GEN_RCV_NO_BUFFER:
    {
        GenericInfo.Ulong64 = NICQueryStatisticCounter(Adapter, Oid);

        *BytesNeeded = sizeof(ULONG64);
        if (InformationBufferLength >= sizeof(ULONG64))
        {
            *BytesWritten = sizeof(ULONG64);
            NdisMoveMemory(InformationBuffer, &GenericInfo.Ulong64, sizeof(ULONG64));
        }
        else if (InformationBufferLength >= sizeof(ULONG))
        {
            *BytesWritten = sizeof(ULONG);
            NdisMoveMemory(InformationBuffer, &GenericInfo.Ulong64, sizeof(ULONG));
        }
        else
        {
            *BytesWritten = 0;
            return NDIS_STATUS_BUFFER_TOO_SHORT;
        }
        return NDIS_STATUS_SUCCESS;
    }

    case OID_PNP_CAPABILITIES:
    {
        copyLength = sizeof(NDIS_PNP_CAPABILITIES);
        status = NICFillPowerManagementCapabilities(Adapter, &GenericInfo.PmCapabilities);
        break;
    }

    case OID_PNP_QUERY_POWER:
    {
        /*
         * Query whether the device can transition to the specified power state.
         * We support all power states, so always return success.
         */
        if (InformationBufferLength < sizeof(NDIS_DEVICE_POWER_STATE))
        {
            *BytesNeeded = sizeof(NDIS_DEVICE_POWER_STATE);
            *BytesWritten = 0;
            return NDIS_STATUS_BUFFER_TOO_SHORT;
        }

        NdisMoveMemory(&GenericInfo.PowerState, InformationBuffer, sizeof(NDIS_DEVICE_POWER_STATE));
        status = NICQueryPowerState(Adapter, GenericInfo.PowerState);

        *BytesWritten = 0;
        *BytesNeeded = 0;
        return status;
    }

    case OID_PNP_ENABLE_WAKE_UP:
    {
        GenericInfo.Ulong = 0;
        if (Adapter->WakeOnMagicPacket)
            GenericInfo.Ulong |= NDIS_PNP_WAKE_UP_MAGIC_PACKET;
        if (Adapter->WakeOnLinkChange)
            GenericInfo.Ulong |= NDIS_PNP_WAKE_UP_LINK_CHANGE;
        break;
    }

    case OID_GEN_PHYSICAL_MEDIUM:
    {
        GenericInfo.Ulong = NdisPhysicalMedium802_3;
        break;
    }

    default:
        NDIS_DbgPrint(MIN_TRACE, ("Unknown OID 0x%x(%s)\n", Oid, Oid2Str(Oid)));
        status = NDIS_STATUS_NOT_SUPPORTED;
        break;
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        if (copyLength > InformationBufferLength)
        {
            *BytesNeeded = copyLength;
            *BytesWritten = 0;
            status = NDIS_STATUS_BUFFER_TOO_SHORT;
        }
        else
        {
            NdisMoveMemory(InformationBuffer, copySource, copyLength);
            *BytesWritten = copyLength;
            *BytesNeeded = copyLength;
        }
    }
    else
    {
        *BytesWritten = 0;
        *BytesNeeded = 0;
    }

    NDIS_DbgPrint(MAX_TRACE, ("Query OID 0x%x(%s): Completed with status 0x%x (%d, %d)\n",
                              Oid, Oid2Str(Oid), status, *BytesWritten, *BytesNeeded));
    return status;
}

NDIS_STATUS
NTAPI
MiniportSetInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesRead,
    OUT PULONG BytesNeeded)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    ULONG genericUlong;
    NDIS_STATUS status;

    status = NDIS_STATUS_SUCCESS;

    switch (Oid)
    {
    case OID_GEN_CURRENT_PACKET_FILTER:
        if (InformationBufferLength < sizeof(ULONG))
        {
            *BytesRead = 0;
            *BytesNeeded = sizeof(ULONG);
            status = NDIS_STATUS_INVALID_LENGTH;
            break;
        }

        NdisMoveMemory(&genericUlong, InformationBuffer, sizeof(ULONG));

        if (genericUlong &
            ~(NDIS_PACKET_TYPE_DIRECTED |
              NDIS_PACKET_TYPE_MULTICAST |
              NDIS_PACKET_TYPE_ALL_MULTICAST |
              NDIS_PACKET_TYPE_BROADCAST |
              NDIS_PACKET_TYPE_PROMISCUOUS |
              NDIS_PACKET_TYPE_MAC_FRAME))
        {
            *BytesRead = sizeof(ULONG);
            *BytesNeeded = sizeof(ULONG);
            status = NDIS_STATUS_NOT_SUPPORTED;
            break;
        }

        if (Adapter->PacketFilter == genericUlong)
        {
            break;
        }

        Adapter->PacketFilter = genericUlong;

        status = NICApplyPacketFilter(Adapter);
        if (status != NDIS_STATUS_SUCCESS)
        {
            NDIS_DbgPrint(MIN_TRACE, ("Failed to apply new packet filter (0x%x)\n", status));
            break;
        }

        break;

    case OID_GEN_CURRENT_LOOKAHEAD:
        if (InformationBufferLength < sizeof(ULONG))
        {
            *BytesRead = 0;
            *BytesNeeded = sizeof(ULONG);
            status = NDIS_STATUS_INVALID_LENGTH;
            break;
        }

        NdisMoveMemory(&genericUlong, InformationBuffer, sizeof(ULONG));

        if (genericUlong > MAXIMUM_FRAME_SIZE - sizeof(ETH_HEADER))
        {
            status = NDIS_STATUS_INVALID_DATA;
        }
        else
        {
            /* Accept but ignore - we always indicate full packet */
        }

        break;

    case OID_802_3_MULTICAST_LIST:
        if (InformationBufferLength % IEEE_802_ADDR_LENGTH)
        {
            *BytesRead = 0;
            *BytesNeeded = InformationBufferLength + (InformationBufferLength % IEEE_802_ADDR_LENGTH);
            status = NDIS_STATUS_INVALID_LENGTH;
            break;
        }

        if (InformationBufferLength > sizeof(Adapter->MulticastList))
        {
            *BytesNeeded = sizeof(Adapter->MulticastList);
            *BytesRead = 0;
            status = NDIS_STATUS_MULTICAST_FULL;
            break;
        }

        NdisMoveMemory(Adapter->MulticastList, InformationBuffer, InformationBufferLength);

        Adapter->MulticastListSize = InformationBufferLength / IEEE_802_ADDR_LENGTH;

        NICUpdateMulticastList(Adapter);
        break;

    case OID_PNP_SET_POWER:
    {
        NDIS_DEVICE_POWER_STATE NewPowerState;

        if (InformationBufferLength < sizeof(NDIS_DEVICE_POWER_STATE))
        {
            *BytesRead = 0;
            *BytesNeeded = sizeof(NDIS_DEVICE_POWER_STATE);
            status = NDIS_STATUS_INVALID_LENGTH;
            break;
        }

        NdisMoveMemory(&NewPowerState, InformationBuffer, sizeof(NDIS_DEVICE_POWER_STATE));

        NDIS_DbgPrint(MID_TRACE, ("OID_PNP_SET_POWER: Transitioning to D%d\n",
                                  NewPowerState - NdisDeviceStateD0));

        status = NICSetPowerState(Adapter, NewPowerState);

        *BytesRead = sizeof(NDIS_DEVICE_POWER_STATE);
        *BytesNeeded = 0;
        return status;
    }

    case OID_PNP_ENABLE_WAKE_UP:
    {
        ULONG WakeUpFlags;

        if (InformationBufferLength < sizeof(ULONG))
        {
            *BytesRead = 0;
            *BytesNeeded = sizeof(ULONG);
            status = NDIS_STATUS_INVALID_LENGTH;
            break;
        }

        NdisMoveMemory(&WakeUpFlags, InformationBuffer, sizeof(ULONG));

        Adapter->WakeOnMagicPacket = (WakeUpFlags & NDIS_PNP_WAKE_UP_MAGIC_PACKET) ? TRUE : FALSE;
        Adapter->WakeOnLinkChange = (WakeUpFlags & NDIS_PNP_WAKE_UP_LINK_CHANGE) ? TRUE : FALSE;

        NDIS_DbgPrint(MID_TRACE, ("Wake-up enabled: Magic=%d, Link=%d\n",
                                  Adapter->WakeOnMagicPacket, Adapter->WakeOnLinkChange));

        *BytesRead = sizeof(ULONG);
        *BytesNeeded = 0;
        return NDIS_STATUS_SUCCESS;
    }

    default:
        NDIS_DbgPrint(MIN_TRACE, ("Unknown OID 0x%x(%s)\n", Oid, Oid2Str(Oid)));
        status = NDIS_STATUS_NOT_SUPPORTED;
        *BytesRead = 0;
        *BytesNeeded = 0;
        break;
    }

    if (status == NDIS_STATUS_SUCCESS)
    {
        *BytesRead = InformationBufferLength;
        *BytesNeeded = 0;
    }

    return status;
}
