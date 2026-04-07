/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x OID request handling
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This file implements NDIS 6.x OID request handling:
 *   - E1000OidRequest - Main OID handler
 *   - E1000QueryInformation - Query OIDs
 *   - E1000SetInformation - Set OIDs
 *   - E1000CancelOidRequest - Cancel pending OID
 */

#include "e1000.h"

/* ============================================================================
 * Supported OID List
 * ============================================================================ */

static NDIS_OID SupportedOidList[] =
{
    /* General Required OIDs */
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
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_LINK_SPEED_EX,
    OID_GEN_MAX_LINK_SPEED,
    OID_GEN_MEDIA_CONNECT_STATUS_EX,
    OID_GEN_MEDIA_DUPLEX_STATE,
    OID_GEN_LINK_STATE,
    OID_GEN_INTERRUPT_MODERATION,

    /* Statistics OIDs */
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_STATISTICS,
    OID_GEN_BYTES_XMIT,
    OID_GEN_BYTES_RCV,

    /* 802.3 OIDs */
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,

    /* Offload OIDs */
    OID_TCP_OFFLOAD_CURRENT_CONFIG,
    OID_TCP_OFFLOAD_PARAMETERS,
    OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES,
    OID_OFFLOAD_ENCAPSULATION,

    /* RSS OIDs */
    OID_GEN_RECEIVE_SCALE_CAPABILITIES,
    OID_GEN_RECEIVE_SCALE_PARAMETERS,

    /* Power Management OIDs */
    OID_PNP_CAPABILITIES,
    OID_PNP_SET_POWER,
    OID_PNP_QUERY_POWER,
    OID_PNP_ENABLE_WAKE_UP,
};


/* ============================================================================
 * E1000OidRequest - Main OID request handler
 *
 * Dispatches OID requests to the appropriate query or set handler.
 * ============================================================================ */

NDIS_STATUS
NTAPI
E1000OidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            Status = E1000QueryInformation(Adapter, OidRequest);
            break;

        case NdisRequestSetInformation:
            Status = E1000SetInformation(Adapter, OidRequest);
            break;

        case NdisRequestMethod:
            /* Method requests not currently supported */
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;

        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    return Status;
}


/* ============================================================================
 * E1000QueryInformation - Handle query OID requests
 * ============================================================================ */

NDIS_STATUS
E1000QueryInformation(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest
    )
{
    NDIS_OID Oid = OidRequest->DATA.QUERY_INFORMATION.Oid;
    PVOID InfoBuffer = OidRequest->DATA.QUERY_INFORMATION.InformationBuffer;
    ULONG InfoBufferLength = OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
    PUINT BytesWritten = &OidRequest->DATA.QUERY_INFORMATION.BytesWritten;
    PUINT BytesNeeded = &OidRequest->DATA.QUERY_INFORMATION.BytesNeeded;

    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    ULONG CopyLength = 0;
    PVOID CopySource = NULL;

    /* Union for temporary data storage */
    union
    {
        ULONG Ulong;
        ULONG64 Ulong64;
        USHORT Ushort;
        NDIS_MEDIUM Medium;
        NDIS_HARDWARE_STATUS HardwareStatus;
        NDIS_MEDIA_CONNECT_STATE MediaConnectState;
        NDIS_MEDIA_DUPLEX_STATE MediaDuplexState;
        NDIS_PHYSICAL_MEDIUM PhysicalMedium;
        NDIS_LINK_STATE LinkState;
        NDIS_LINK_SPEED LinkSpeed;
        NDIS_STATISTICS_INFO Statistics;
        NDIS_INTERRUPT_MODERATION_PARAMETERS IntModeration;
        UCHAR MacAddress[ETH_LENGTH_OF_ADDRESS];
    } Data;

    *BytesWritten = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            CopySource = SupportedOidList;
            CopyLength = sizeof(SupportedOidList);
            break;

        case OID_GEN_HARDWARE_STATUS:
            Data.HardwareStatus = NdisHardwareStatusReady;
            CopySource = &Data.HardwareStatus;
            CopyLength = sizeof(NDIS_HARDWARE_STATUS);
            break;

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Data.Medium = NdisMedium802_3;
            CopySource = &Data.Medium;
            CopyLength = sizeof(NDIS_MEDIUM);
            break;

        case OID_GEN_PHYSICAL_MEDIUM:
            Data.PhysicalMedium = NdisPhysicalMedium802_3;
            CopySource = &Data.PhysicalMedium;
            CopyLength = sizeof(NDIS_PHYSICAL_MEDIUM);
            break;

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:
            Data.Ulong = E1000_MAX_FRAME_SIZE - 14;  /* Minus Ethernet header */
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data.Ulong = E1000_MAX_FRAME_SIZE - 14;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            Data.Ulong = E1000_MAX_FRAME_SIZE;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_LINK_SPEED:
            Data.Ulong = (ULONG)(Adapter->LinkSpeed / 100);  /* Convert to 100bps units */
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_LINK_SPEED_EX:
        case OID_GEN_MAX_LINK_SPEED:
            Data.LinkSpeed.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkSpeed.RcvLinkSpeed = Adapter->LinkSpeed;
            CopySource = &Data.LinkSpeed;
            CopyLength = sizeof(NDIS_LINK_SPEED);
            break;

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
            Data.Ulong = E1000_MAX_FRAME_SIZE * E1000_NUM_TX_DESC;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_RECEIVE_BUFFER_SPACE:
            Data.Ulong = E1000_RX_BUFFER_SIZE * E1000_NUM_RX_DESC;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_VENDOR_ID:
            Data.Ulong = ((ULONG)Adapter->PermanentMacAddress[0] << 16) |
                         ((ULONG)Adapter->PermanentMacAddress[1] << 8) |
                         (ULONG)Adapter->PermanentMacAddress[2];
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const char VendorDesc[] = "ReactOS Intel E1000 Driver";
            CopySource = (PVOID)VendorDesc;
            CopyLength = sizeof(VendorDesc);
            break;
        }

        case OID_GEN_VENDOR_DRIVER_VERSION:
            Data.Ulong = DRIVER_VERSION;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_DRIVER_VERSION:
            Data.Ushort = (NDIS_MINIPORT_MAJOR_VERSION << 8) | NDIS_MINIPORT_MINOR_VERSION;
            CopySource = &Data.Ushort;
            CopyLength = sizeof(USHORT);
            break;

        case OID_GEN_CURRENT_PACKET_FILTER:
            Data.Ulong = Adapter->PacketFilter;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_MAC_OPTIONS:
            Data.Ulong = NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_NO_LOOPBACK |
                         NDIS_MAC_OPTION_8021P_PRIORITY |
                         NDIS_MAC_OPTION_8021Q_VLAN;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Data.Ulong = (Adapter->MediaState == MediaConnectStateConnected) ?
                         NdisMediaStateConnected : NdisMediaStateDisconnected;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS_EX:
            Data.MediaConnectState = Adapter->MediaState;
            CopySource = &Data.MediaConnectState;
            CopyLength = sizeof(NDIS_MEDIA_CONNECT_STATE);
            break;

        case OID_GEN_MEDIA_DUPLEX_STATE:
            Data.MediaDuplexState = Adapter->FullDuplex ? MediaDuplexStateFull : MediaDuplexStateHalf;
            CopySource = &Data.MediaDuplexState;
            CopyLength = sizeof(NDIS_MEDIA_DUPLEX_STATE);
            break;

        case OID_GEN_LINK_STATE:
            NdisZeroMemory(&Data.LinkState, sizeof(NDIS_LINK_STATE));
            Data.LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            Data.LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
            Data.LinkState.MediaConnectState = Adapter->MediaState;
            Data.LinkState.MediaDuplexState = Adapter->FullDuplex ? MediaDuplexStateFull : MediaDuplexStateHalf;
            Data.LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;
            CopySource = &Data.LinkState;
            CopyLength = sizeof(NDIS_LINK_STATE);
            break;

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            Data.Ulong = E1000_NUM_TX_DESC;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_XMIT_OK:
            Data.Ulong64 = Adapter->Statistics.TxPackets;
            CopySource = &Data.Ulong64;
            CopyLength = sizeof(ULONG64);
            break;

        case OID_GEN_RCV_OK:
            Data.Ulong64 = Adapter->Statistics.RxPackets;
            CopySource = &Data.Ulong64;
            CopyLength = sizeof(ULONG64);
            break;

        case OID_GEN_XMIT_ERROR:
            Data.Ulong64 = Adapter->Statistics.TxErrors;
            CopySource = &Data.Ulong64;
            CopyLength = sizeof(ULONG64);
            break;

        case OID_GEN_RCV_ERROR:
            Data.Ulong64 = Adapter->Statistics.RxErrors;
            CopySource = &Data.Ulong64;
            CopyLength = sizeof(ULONG64);
            break;

        case OID_GEN_RCV_NO_BUFFER:
            Data.Ulong64 = Adapter->Statistics.RxNoBuffer;
            CopySource = &Data.Ulong64;
            CopyLength = sizeof(ULONG64);
            break;

        case OID_GEN_BYTES_XMIT:
            Data.Ulong64 = Adapter->Statistics.TxBytes;
            CopySource = &Data.Ulong64;
            CopyLength = sizeof(ULONG64);
            break;

        case OID_GEN_BYTES_RCV:
            Data.Ulong64 = Adapter->Statistics.RxBytes;
            CopySource = &Data.Ulong64;
            CopyLength = sizeof(ULONG64);
            break;

        case OID_GEN_STATISTICS:
            NdisZeroMemory(&Data.Statistics, sizeof(NDIS_STATISTICS_INFO));
            Data.Statistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.Statistics.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
            Data.Statistics.Header.Size = sizeof(NDIS_STATISTICS_INFO);
            Data.Statistics.SupportedStatistics =
                NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR |
                NDIS_STATISTICS_FLAGS_VALID_XMIT_DISCARDS;
            Data.Statistics.ifHCInOctets = Adapter->Statistics.RxBytes;
            Data.Statistics.ifHCOutOctets = Adapter->Statistics.TxBytes;
            Data.Statistics.ifInDiscards = Adapter->Statistics.RxNoBuffer;
            Data.Statistics.ifInErrors = Adapter->Statistics.RxErrors;
            Data.Statistics.ifOutErrors = Adapter->Statistics.TxErrors;
            Data.Statistics.ifOutDiscards = Adapter->Statistics.TxAbortedErrors;
            CopySource = &Data.Statistics;
            CopyLength = sizeof(NDIS_STATISTICS_INFO);
            break;

        case OID_GEN_INTERRUPT_MODERATION:
            NdisZeroMemory(&Data.IntModeration, sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS));
            Data.IntModeration.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.IntModeration.Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
            Data.IntModeration.Header.Size = sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS);
            Data.IntModeration.Flags = NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_RESET;
            Data.IntModeration.InterruptModeration = NdisInterruptModerationEnabled;
            CopySource = &Data.IntModeration;
            CopyLength = sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS);
            break;

        case OID_802_3_PERMANENT_ADDRESS:
            NdisMoveMemory(Data.MacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
            CopySource = Data.MacAddress;
            CopyLength = ETH_LENGTH_OF_ADDRESS;
            break;

        case OID_802_3_CURRENT_ADDRESS:
            NdisMoveMemory(Data.MacAddress, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
            CopySource = Data.MacAddress;
            CopyLength = ETH_LENGTH_OF_ADDRESS;
            break;

        case OID_802_3_MULTICAST_LIST:
            CopySource = Adapter->MulticastList;
            CopyLength = Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS;
            break;

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Data.Ulong = E1000_MAX_MULTICAST;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_PNP_CAPABILITIES:
            /* Return PM capabilities - basic support */
            *BytesNeeded = sizeof(NDIS_PNP_CAPABILITIES);
            if (InfoBufferLength < sizeof(NDIS_PNP_CAPABILITIES))
            {
                Status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            else
            {
                PNDIS_PNP_CAPABILITIES PmCaps = (PNDIS_PNP_CAPABILITIES)InfoBuffer;
                NdisZeroMemory(PmCaps, sizeof(NDIS_PNP_CAPABILITIES));
                PmCaps->WakeUpCapabilities.MinMagicPacketWakeUp = NdisDeviceStateD3;
                PmCaps->WakeUpCapabilities.MinPatternWakeUp = NdisDeviceStateUnspecified;
                PmCaps->WakeUpCapabilities.MinLinkChangeWakeUp = NdisDeviceStateD3;
                *BytesWritten = sizeof(NDIS_PNP_CAPABILITIES);
            }
            return Status;

        case OID_PNP_QUERY_POWER:
            /* We support all power states */
            Status = NDIS_STATUS_SUCCESS;
            return Status;

        default:
            DPRINT1("E1000: Unsupported query OID 0x%08x\n", Oid);
            Status = NDIS_STATUS_NOT_SUPPORTED;
            return Status;
    }

    /* Copy data to output buffer */
    if (Status == NDIS_STATUS_SUCCESS && CopySource != NULL)
    {
        if (InfoBufferLength < CopyLength)
        {
            *BytesNeeded = CopyLength;
            Status = NDIS_STATUS_BUFFER_TOO_SHORT;
        }
        else
        {
            NdisMoveMemory(InfoBuffer, CopySource, CopyLength);
            *BytesWritten = CopyLength;
        }
    }

    return Status;
}


/* ============================================================================
 * E1000SetInformation - Handle set OID requests
 * ============================================================================ */

NDIS_STATUS
E1000SetInformation(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest
    )
{
    NDIS_OID Oid = OidRequest->DATA.SET_INFORMATION.Oid;
    PVOID InfoBuffer = OidRequest->DATA.SET_INFORMATION.InformationBuffer;
    ULONG InfoBufferLength = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;
    PUINT BytesRead = &OidRequest->DATA.SET_INFORMATION.BytesRead;
    PUINT BytesNeeded = &OidRequest->DATA.SET_INFORMATION.BytesNeeded;

    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (InfoBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
            }
            else
            {
                ULONG PacketFilter = *(PULONG)InfoBuffer;

                /* Validate filter */
                if (PacketFilter & ~(NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST |
                                     NDIS_PACKET_TYPE_PROMISCUOUS))
                {
                    Status = NDIS_STATUS_NOT_SUPPORTED;
                }
                else
                {
                    Status = E1000SetPacketFilter(Adapter, PacketFilter);
                    if (Status == NDIS_STATUS_SUCCESS)
                    {
                        Adapter->PacketFilter = PacketFilter;
                        *BytesRead = sizeof(ULONG);
                    }
                }
            }
            break;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (InfoBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
            }
            else
            {
                ULONG Lookahead = *(PULONG)InfoBuffer;
                if (Lookahead > E1000_MAX_FRAME_SIZE - 14)
                {
                    Status = NDIS_STATUS_INVALID_DATA;
                }
                else
                {
                    Adapter->LookaheadSize = Lookahead;
                    *BytesRead = sizeof(ULONG);
                }
            }
            break;

        case OID_802_3_MULTICAST_LIST:
            if (InfoBufferLength % ETH_LENGTH_OF_ADDRESS != 0)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
            }
            else if (InfoBufferLength > E1000_MAX_MULTICAST * ETH_LENGTH_OF_ADDRESS)
            {
                *BytesNeeded = E1000_MAX_MULTICAST * ETH_LENGTH_OF_ADDRESS;
                Status = NDIS_STATUS_MULTICAST_FULL;
            }
            else
            {
                ULONG Count = InfoBufferLength / ETH_LENGTH_OF_ADDRESS;
                Status = E1000SetMulticastList(Adapter, InfoBuffer, Count);
                if (Status == NDIS_STATUS_SUCCESS)
                {
                    NdisMoveMemory(Adapter->MulticastList, InfoBuffer, InfoBufferLength);
                    Adapter->MulticastCount = Count;
                    *BytesRead = InfoBufferLength;
                }
            }
            break;

        case OID_GEN_INTERRUPT_MODERATION:
            if (InfoBufferLength < sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS))
            {
                *BytesNeeded = sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS);
                Status = NDIS_STATUS_INVALID_LENGTH;
            }
            else
            {
                /* Accept but don't modify - would require reset */
                *BytesRead = sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS);
            }
            break;

        case OID_PNP_SET_POWER:
            if (InfoBufferLength < sizeof(NDIS_DEVICE_POWER_STATE))
            {
                *BytesNeeded = sizeof(NDIS_DEVICE_POWER_STATE);
                Status = NDIS_STATUS_INVALID_LENGTH;
            }
            else
            {
                NDIS_DEVICE_POWER_STATE NewState = *(PNDIS_DEVICE_POWER_STATE)InfoBuffer;
                Status = E1000SetPower(Adapter, NewState);
                *BytesRead = sizeof(NDIS_DEVICE_POWER_STATE);
            }
            break;

        case OID_PNP_ENABLE_WAKE_UP:
            if (InfoBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
            }
            else
            {
                ULONG WakeFlags = *(PULONG)InfoBuffer;
                Adapter->WakeOnMagicPacket = (WakeFlags & NDIS_PNP_WAKE_UP_MAGIC_PACKET) ? TRUE : FALSE;
                Adapter->WakeOnPattern = (WakeFlags & NDIS_PNP_WAKE_UP_PATTERN_MATCH) ? TRUE : FALSE;
                Adapter->WakeOnLinkChange = (WakeFlags & NDIS_PNP_WAKE_UP_LINK_CHANGE) ? TRUE : FALSE;
                *BytesRead = sizeof(ULONG);
            }
            break;

        case OID_TCP_OFFLOAD_PARAMETERS:
            if (InfoBufferLength < sizeof(NDIS_OFFLOAD_PARAMETERS))
            {
                *BytesNeeded = sizeof(NDIS_OFFLOAD_PARAMETERS);
                Status = NDIS_STATUS_INVALID_LENGTH;
            }
            else
            {
                Status = E1000SetOffloadParameters(Adapter, (PNDIS_OFFLOAD_PARAMETERS)InfoBuffer);
                if (Status == NDIS_STATUS_SUCCESS)
                {
                    *BytesRead = sizeof(NDIS_OFFLOAD_PARAMETERS);
                }
            }
            break;

        case OID_GEN_RECEIVE_SCALE_PARAMETERS:
            Status = E1000ConfigureRss(Adapter,
                                       (PNDIS_RECEIVE_SCALE_PARAMETERS)InfoBuffer,
                                       InfoBufferLength);
            if (Status == NDIS_STATUS_SUCCESS)
            {
                *BytesRead = InfoBufferLength;
            }
            break;

        default:
            DPRINT1("E1000: Unsupported set OID 0x%08x\n", Oid);
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    return Status;
}


/* ============================================================================
 * E1000CancelOidRequest - Cancel a pending OID request
 * ============================================================================ */

VOID
NTAPI
E1000CancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(Adapter);
    UNREFERENCED_PARAMETER(RequestId);

    /* We complete OID requests synchronously, so nothing to cancel */
}
