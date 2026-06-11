/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Upper-miniport OID request handling
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"

#define NDEBUG
#include <debug.h>

#define NWIFI_VENDOR_DESCRIPTION    "ReactOS Native WiFi Virtual Adapter"
#define NWIFI_VENDOR_DRIVER_VERSION 0x00010000
#define NWIFI_DRIVER_VERSION        ((6 << 8) | 20)  /* NDIS 6.20 */

/* Standard Ethernet OID set advertised to TCP/IP.  dot11 OIDs are exposed to
 * the WLAN service via the control IOCTL channel, not via this miniport. */
NDIS_OID NwifiSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_TRANSMIT_BUFFER_SPACE,
    OID_GEN_RECEIVE_BUFFER_SPACE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_LINK_SPEED,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_STATISTICS,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,
};

ULONG NwifiSupportedOidCount = RTL_NUMBER_OF(NwifiSupportedOids);

/* ===========================================================================
 *  Query helpers
 * ===========================================================================
 */
static
NDIS_STATUS
NwifiQueryBuffer(
    _In_ PNDIS_OID_REQUEST Request,
    _In_reads_bytes_(Length) PVOID Data,
    _In_ ULONG Length)
{
    if (Request->DATA.QUERY_INFORMATION.InformationBufferLength < Length)
    {
        Request->DATA.QUERY_INFORMATION.BytesNeeded = Length;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }
    RtlCopyMemory(Request->DATA.QUERY_INFORMATION.InformationBuffer, Data, Length);
    Request->DATA.QUERY_INFORMATION.BytesWritten = Length;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NwifiQueryUlong(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG Value)
{
    return NwifiQueryBuffer(Request, &Value, sizeof(ULONG));
}

static
NDIS_STATUS
NwifiQueryCounter(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG64 Value)
{
    ULONG Length = Request->DATA.QUERY_INFORMATION.InformationBufferLength;
    PVOID Buffer = Request->DATA.QUERY_INFORMATION.InformationBuffer;

    if (Length >= sizeof(ULONG64))
    {
        *(ULONG64 *)Buffer = Value;
        Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG64);
        return NDIS_STATUS_SUCCESS;
    }
    if (Length >= sizeof(ULONG))
    {
        *(ULONG *)Buffer = (ULONG)Value;
        Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
        Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG64);
        return NDIS_STATUS_SUCCESS;
    }
    Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
    return NDIS_STATUS_BUFFER_TOO_SHORT;
}

static
NDIS_STATUS
NwifiQueryStatistics(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS_STATISTICS_INFO Stats;

    if (Request->DATA.QUERY_INFORMATION.InformationBufferLength < sizeof(Stats))
    {
        Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(Stats);
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    RtlZeroMemory(&Stats, sizeof(Stats));
    Stats.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Stats.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
    Stats.Header.Size = sizeof(NDIS_STATISTICS_INFO);
    Stats.SupportedStatistics =
        NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
        NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
        NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
        NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
        NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
        NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR;
    Stats.ifHCInOctets = Adapter->RxBytes;
    Stats.ifHCInUcastPkts = Adapter->RxOk;
    Stats.ifHCOutOctets = Adapter->TxBytes;
    Stats.ifHCOutUcastPkts = Adapter->TxOk;
    Stats.ifInErrors = Adapter->RxError;
    Stats.ifOutErrors = Adapter->TxError;

    RtlCopyMemory(Request->DATA.QUERY_INFORMATION.InformationBuffer, &Stats, sizeof(Stats));
    Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(Stats);
    return NDIS_STATUS_SUCCESS;
}

/* ===========================================================================
 *  Query dispatch (local answers - present a working Ethernet NIC)
 * ===========================================================================
 */
static
NDIS_STATUS
NwifiQueryInformation(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS_OID Oid = Request->DATA.QUERY_INFORMATION.Oid;

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return NwifiQueryBuffer(Request, NwifiSupportedOids,
                                    NwifiSupportedOidCount * sizeof(NDIS_OID));

        case OID_GEN_HARDWARE_STATUS:
            return NwifiQueryUlong(Request, NdisHardwareStatusReady);

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            return NwifiQueryUlong(Request, NdisMedium802_3);

        case OID_GEN_PHYSICAL_MEDIUM:
            return NwifiQueryUlong(Request, NdisPhysicalMediumNative802_11);

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            return NwifiQueryUlong(Request, ETH_MTU);

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            return NwifiQueryUlong(Request, ETH_MAX_FRAME);

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
        case OID_GEN_RECEIVE_BUFFER_SPACE:
            return NwifiQueryUlong(Request, ETH_MAX_FRAME * 64);

        case OID_GEN_VENDOR_ID:
            return NwifiQueryUlong(Request, 0x00FFFFFF);

        case OID_GEN_VENDOR_DESCRIPTION:
            return NwifiQueryBuffer(Request, NWIFI_VENDOR_DESCRIPTION,
                                    sizeof(NWIFI_VENDOR_DESCRIPTION));

        case OID_GEN_VENDOR_DRIVER_VERSION:
            return NwifiQueryUlong(Request, NWIFI_VENDOR_DRIVER_VERSION);

        case OID_GEN_DRIVER_VERSION:
        {
            USHORT Version = NWIFI_DRIVER_VERSION;
            return NwifiQueryBuffer(Request, &Version, sizeof(Version));
        }

        case OID_GEN_CURRENT_PACKET_FILTER:
            return NwifiQueryUlong(Request, Adapter->CurrentPacketFilter);

        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_LOOKAHEAD:
            return NwifiQueryUlong(Request, ETH_MAX_FRAME);

        case OID_GEN_LINK_SPEED:
            /* 100 bps units. */
            return NwifiQueryUlong(Request,
                Adapter->MediaConnected ? (54000000UL / 100UL) : 0);

        case OID_GEN_MEDIA_CONNECT_STATUS:
            return NwifiQueryUlong(Request,
                Adapter->MediaConnected ? NdisMediaStateConnected
                                        : NdisMediaStateDisconnected);

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            return NwifiQueryUlong(Request, 16);

        case OID_GEN_MAC_OPTIONS:
            return NwifiQueryUlong(Request,
                NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                NDIS_MAC_OPTION_NO_LOOPBACK);

        case OID_GEN_XMIT_OK:    return NwifiQueryCounter(Request, Adapter->TxOk);
        case OID_GEN_RCV_OK:     return NwifiQueryCounter(Request, Adapter->RxOk);
        case OID_GEN_XMIT_ERROR: return NwifiQueryCounter(Request, Adapter->TxError);
        case OID_GEN_RCV_ERROR:  return NwifiQueryCounter(Request, Adapter->RxError);
        case OID_GEN_RCV_NO_BUFFER: return NwifiQueryCounter(Request, 0);
        case OID_GEN_STATISTICS: return NwifiQueryStatistics(Adapter, Request);

        case OID_802_3_PERMANENT_ADDRESS:
        case OID_802_3_CURRENT_ADDRESS:
            return NwifiQueryBuffer(Request, Adapter->MacAddress, ETH_ADDR_LEN);

        case OID_802_3_MAXIMUM_LIST_SIZE:
            return NwifiQueryUlong(Request, 32);

        case OID_802_3_RCV_ERROR_ALIGNMENT:
        case OID_802_3_XMIT_ONE_COLLISION:
        case OID_802_3_XMIT_MORE_COLLISIONS:
            return NwifiQueryCounter(Request, 0);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

/* ===========================================================================
 *  Set dispatch
 * ===========================================================================
 */
static
NDIS_STATUS
NwifiSetInformation(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS_OID Oid = Request->DATA.SET_INFORMATION.Oid;
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;

    switch (Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->CurrentPacketFilter = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            /* TODO: translate to OID_DOT11_CURRENT_PACKET_FILTER and forward down. */
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->CurrentLookahead = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_MULTICAST_LIST:
            /* Accept; the open data path indicates all received frames anyway. */
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

/* ===========================================================================
 *  MiniportOidRequest entry point
 * ===========================================================================
 */
NDIS_STATUS
NTAPI
NwifiMiniportOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return NwifiQueryInformation(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return NwifiSetInformation(Adapter, OidRequest);

        case NdisRequestMethod:
            /* dot11 method OIDs (scan, connect, key install) go through the
             * control device (control.c), not this Ethernet edge. */
            return NDIS_STATUS_NOT_SUPPORTED;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

VOID
NTAPI
NwifiMiniportCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
    /* OID requests are serviced synchronously; nothing to cancel. */
}
