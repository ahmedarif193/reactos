/*
 * PROJECT:     ReactOS Virtual Native 802.11 (dot11) Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NDIS OID query/set handling, including the OID_DOT11_* surface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * nwifi issues OID_DOT11_SCAN_REQUEST / CONNECT_REQUEST / DISCONNECT_REQUEST
 * as SetInformation requests (not method OIDs); they schedule the matching
 * asynchronous transition in engine.c.
 */

#include "vwifi.h"
#include <debug.h>

NDIS_OID VWifiSupportedOids[] =
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

    OID_DOT11_MPDU_MAX_LENGTH,
    OID_DOT11_OPERATION_MODE_CAPABILITY,
    OID_DOT11_CURRENT_OPERATION_MODE,
    OID_DOT11_CURRENT_PACKET_FILTER,
    OID_DOT11_CURRENT_PHY_TYPE,
    OID_DOT11_SUPPORTED_PHY_TYPES,
    OID_DOT11_NIC_POWER_STATE,
    OID_DOT11_HARDWARE_PHY_STATE,
    OID_DOT11_MAC_ADDRESS,
    OID_DOT11_PERMANENT_ADDRESS,
    OID_DOT11_CURRENT_ADDRESS,
    OID_DOT11_OPERATIONAL_RATE_SET,
    OID_DOT11_BEACON_PERIOD,
    OID_DOT11_CURRENT_CHANNEL_NUMBER,
    OID_DOT11_RTS_THRESHOLD,
    OID_DOT11_FRAGMENTATION_THRESHOLD,
    OID_DOT11_AUTO_CONFIG_ENABLED,
    OID_DOT11_SCAN_REQUEST,
    OID_DOT11_ENUM_BSS_LIST,
    OID_DOT11_FLUSH_BSS_LIST,
    OID_DOT11_DESIRED_SSID_LIST,
    OID_DOT11_DESIRED_BSSID_LIST,
    OID_DOT11_DESIRED_BSS_TYPE,
    OID_DOT11_CONNECT_REQUEST,
    OID_DOT11_DISCONNECT_REQUEST,
    OID_DOT11_RESET_REQUEST,
    OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM,
    OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM,
    OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM,
    OID_DOT11_CIPHER_DEFAULT_KEY,
    OID_DOT11_CIPHER_DEFAULT_KEY_ID,
    OID_DOT11_CIPHER_KEY_MAPPING_KEY,
};

ULONG VWifiSupportedOidCount =
    sizeof(VWifiSupportedOids) / sizeof(VWifiSupportedOids[0]);

static
NDIS_STATUS
VWifiOidQueryBuffer(
    IN PNDIS_OID_REQUEST Request,
    IN PVOID Data,
    IN ULONG Length)
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
VWifiOidQueryUlong(
    IN PNDIS_OID_REQUEST Request,
    IN ULONG Value)
{
    return VWifiOidQueryBuffer(Request, &Value, sizeof(ULONG));
}

static
NDIS_STATUS
VWifiOidQueryCounter(
    IN PNDIS_OID_REQUEST Request,
    IN ULONG64 Value)
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
VWifiOidQueryStatistics(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    NDIS_STATISTICS_INFO Stats;

    RtlZeroMemory(&Stats, sizeof(Stats));
    Stats.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Stats.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
    Stats.Header.Size = sizeof(NDIS_STATISTICS_INFO);
    Stats.SupportedStatistics =
        NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
        NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
        NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
        NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR |
        NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS;

    Stats.ifHCInOctets = Adapter->RxBytes;
    Stats.ifHCInUcastPkts = Adapter->RxOkCount;
    Stats.ifInErrors = Adapter->RxErrorCount;
    Stats.ifHCOutOctets = Adapter->TxBytes;
    Stats.ifHCOutUcastPkts = Adapter->TxOkCount;
    Stats.ifOutErrors = Adapter->TxErrorCount;

    return VWifiOidQueryBuffer(Request, &Stats, sizeof(Stats));
}

static
NDIS_STATUS
VWifiDot11QueryOperationModeCapability(
    IN PNDIS_OID_REQUEST Request)
{
    DOT11_OPERATION_MODE_CAPABILITY Cap;

    RtlZeroMemory(&Cap, sizeof(Cap));
    Cap.uReserved = 0;
    Cap.uMajorVersion = 2;
    Cap.uMinorVersion = 0;
    Cap.uNumOfTXBuffers = 4;
    Cap.uNumOfRXBuffers = 4;
    Cap.uOpModeCapability = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;

    return VWifiOidQueryBuffer(Request, &Cap, sizeof(Cap));
}

static
NDIS_STATUS
VWifiDot11QueryCurrentOperationMode(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    DOT11_CURRENT_OPERATION_MODE Mode;

    RtlZeroMemory(&Mode, sizeof(Mode));
    Mode.uReserved = 0;
    Mode.uCurrentOpMode = Adapter->Dot11.CurrentOperationMode;

    return VWifiOidQueryBuffer(Request, &Mode, sizeof(Mode));
}

static
NDIS_STATUS
VWifiDot11QuerySupportedPhyTypes(
    IN PNDIS_OID_REQUEST Request)
{
    DOT11_PHY_TYPE_LIST List;

    RtlZeroMemory(&List, sizeof(List));
    List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    List.Header.Revision = DOT11_PHY_TYPE_LIST_REVISION_1;
    List.Header.Size = sizeof(DOT11_PHY_TYPE_LIST);
    List.uNumOfEntries = 1;
    List.uTotalNumOfEntries = 1;
    List.dot11PhyType[0] = dot11_phy_type_erp;   /* 2.4 GHz 802.11g */

    return VWifiOidQueryBuffer(Request, &List, sizeof(List));
}

static
NDIS_STATUS
VWifiDot11QueryOperationalRateSet(
    IN PNDIS_OID_REQUEST Request)
{
    DOT11_RATE_SET RateSet;
    /* 802.11b/g rates in 500 kbit/s units (no basic-rate high bit here). */
    static const UCHAR Rates[] = { 2, 4, 11, 22, 12, 18, 24, 36 };

    RtlZeroMemory(&RateSet, sizeof(RateSet));
    RateSet.uRateSetLength = sizeof(Rates);
    RtlCopyMemory(RateSet.ucRateSet, Rates, sizeof(Rates));

    return VWifiOidQueryBuffer(Request, &RateSet, sizeof(RateSet));
}

static
NDIS_STATUS
VWifiDot11QueryEnumBssList(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    PUCHAR Buffer = Request->DATA.QUERY_INFORMATION.InformationBuffer;
    ULONG BufferLength = Request->DATA.QUERY_INFORMATION.InformationBufferLength;
    ULONG BytesNeeded = 0;
    ULONG Written;

    Written = VWifiBuildBssList(Adapter, Buffer, BufferLength, &BytesNeeded);
    if (Written == 0)
    {
        Request->DATA.QUERY_INFORMATION.BytesNeeded = BytesNeeded;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    Request->DATA.QUERY_INFORMATION.BytesWritten = Written;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
VWifiDot11Query(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    NDIS_OID Oid = Request->DATA.QUERY_INFORMATION.Oid;

    switch (Oid)
    {
        case OID_DOT11_MPDU_MAX_LENGTH:
            return VWifiOidQueryUlong(Request, VWIFI_MAX_FRAME_SIZE);

        case OID_DOT11_OPERATION_MODE_CAPABILITY:
            return VWifiDot11QueryOperationModeCapability(Request);

        case OID_DOT11_CURRENT_OPERATION_MODE:
            return VWifiDot11QueryCurrentOperationMode(Adapter, Request);

        case OID_DOT11_CURRENT_PHY_TYPE:
            return VWifiOidQueryUlong(Request, (ULONG)Adapter->Dot11.CurrentPhyType);

        case OID_DOT11_SUPPORTED_PHY_TYPES:
            return VWifiDot11QuerySupportedPhyTypes(Request);

        case OID_DOT11_CURRENT_PACKET_FILTER:
            return VWifiOidQueryUlong(Request, Adapter->Dot11.PacketFilter);

        case OID_DOT11_CURRENT_CHANNEL_NUMBER:
            return VWifiOidQueryUlong(Request, Adapter->Dot11.CurrentChannel);

        case OID_DOT11_RTS_THRESHOLD:
            return VWifiOidQueryUlong(Request, Adapter->Dot11.RtsThreshold);

        case OID_DOT11_FRAGMENTATION_THRESHOLD:
            return VWifiOidQueryUlong(Request, Adapter->Dot11.FragmentationThreshold);

        case OID_DOT11_BEACON_PERIOD:
            return VWifiOidQueryUlong(Request, Adapter->Dot11.BeaconPeriod);

        case OID_DOT11_NIC_POWER_STATE:
        case OID_DOT11_HARDWARE_PHY_STATE:
            return VWifiOidQueryUlong(Request, Adapter->Dot11.RadioOn ? TRUE : FALSE);

        case OID_DOT11_AUTO_CONFIG_ENABLED:
            /* DOT11_PHY_AUTO_CONFIG_ENABLED_FLAG == 0x00000001. */
            return VWifiOidQueryUlong(Request,
                                      Adapter->Dot11.AutoConfigEnabled ? 0x1 : 0);

        case OID_DOT11_MAC_ADDRESS:
        case OID_DOT11_CURRENT_ADDRESS:
            return VWifiOidQueryBuffer(Request, Adapter->CurrentAddress,
                                       VWIFI_ADDRESS_LENGTH);

        case OID_DOT11_PERMANENT_ADDRESS:
            return VWifiOidQueryBuffer(Request, Adapter->PermanentAddress,
                                       VWIFI_ADDRESS_LENGTH);

        case OID_DOT11_OPERATIONAL_RATE_SET:
            return VWifiDot11QueryOperationalRateSet(Request);

        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
            return VWifiOidQueryUlong(Request, (ULONG)Adapter->Dot11.AuthAlgorithm);

        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
            return VWifiOidQueryUlong(Request, (ULONG)Adapter->Dot11.UnicastCipher);

        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
            return VWifiOidQueryUlong(Request, (ULONG)Adapter->Dot11.MulticastCipher);

        case OID_DOT11_DESIRED_BSS_TYPE:
            return VWifiOidQueryUlong(Request, Adapter->Dot11.DesiredBssType);

        case OID_DOT11_ENUM_BSS_LIST:
            return VWifiDot11QueryEnumBssList(Adapter, Request);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static
NDIS_STATUS
VWifiOidQuery(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    NDIS_OID Oid = Request->DATA.QUERY_INFORMATION.Oid;

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return VWifiOidQueryBuffer(Request, VWifiSupportedOids,
                                       VWifiSupportedOidCount * sizeof(NDIS_OID));

        case OID_GEN_HARDWARE_STATUS:
            return VWifiOidQueryUlong(Request, NdisHardwareStatusReady);

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            return VWifiOidQueryUlong(Request, NdisMediumNative802_11);

        case OID_GEN_PHYSICAL_MEDIUM:
            return VWifiOidQueryUlong(Request, NdisPhysicalMediumNative802_11);

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            return VWifiOidQueryUlong(Request, VWIFI_MTU_SIZE);

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            return VWifiOidQueryUlong(Request, VWIFI_MAX_FRAME_SIZE);

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
        case OID_GEN_RECEIVE_BUFFER_SPACE:
            return VWifiOidQueryUlong(Request, VWIFI_MAX_FRAME_SIZE * 4);

        case OID_GEN_VENDOR_ID:
            return VWifiOidQueryUlong(Request,
                ((ULONG)Adapter->PermanentAddress[0] << 16) |
                ((ULONG)Adapter->PermanentAddress[1] << 8) |
                 (ULONG)Adapter->PermanentAddress[2]);

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Desc[] = "ReactOS Virtual Native 802.11 Adapter";
            return VWifiOidQueryBuffer(Request, (PVOID)Desc, sizeof(Desc));
        }

        case OID_GEN_VENDOR_DRIVER_VERSION:
            return VWifiOidQueryUlong(Request, 0x00010000);

        case OID_GEN_DRIVER_VERSION:
        {
            USHORT Version = (6 << 8) | 20;
            return VWifiOidQueryBuffer(Request, &Version, sizeof(USHORT));
        }

        case OID_GEN_CURRENT_PACKET_FILTER:
            return VWifiOidQueryUlong(Request, Adapter->Dot11.PacketFilter);

        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_LOOKAHEAD:
            return VWifiOidQueryUlong(Request, VWIFI_MAX_FRAME_SIZE);

        case OID_GEN_LINK_SPEED:
            /* Units of 100 bit/s; ~54 Mbit/s when associated, else 0. */
            return VWifiOidQueryUlong(Request,
                (Adapter->ConnState == VWifiConnected)
                    ? (ULONG)VWIFI_LINK_SPEED_100BPS : 0);

        case OID_GEN_MEDIA_CONNECT_STATUS:
            return VWifiOidQueryUlong(Request,
                (Adapter->ConnState == VWifiConnected) ? NdisMediaStateConnected
                                                       : NdisMediaStateDisconnected);

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            return VWifiOidQueryUlong(Request, 1);

        case OID_GEN_MAC_OPTIONS:
            return VWifiOidQueryUlong(Request,
                NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                NDIS_MAC_OPTION_NO_LOOPBACK);

        case OID_GEN_XMIT_OK:
            return VWifiOidQueryCounter(Request, Adapter->TxOkCount);
        case OID_GEN_RCV_OK:
            return VWifiOidQueryCounter(Request, Adapter->RxOkCount);
        case OID_GEN_XMIT_ERROR:
            return VWifiOidQueryCounter(Request, Adapter->TxErrorCount);
        case OID_GEN_RCV_ERROR:
            return VWifiOidQueryCounter(Request, Adapter->RxErrorCount);
        case OID_GEN_RCV_NO_BUFFER:
            return VWifiOidQueryCounter(Request, 0);

        case OID_GEN_STATISTICS:
            return VWifiOidQueryStatistics(Adapter, Request);

        default:
            return VWifiDot11Query(Adapter, Request);
    }
}

static
NDIS_STATUS
VWifiDot11SetDesiredSsidList(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;
    PDOT11_SSID_LIST List = (PDOT11_SSID_LIST)Buffer;

    if (Length < FIELD_OFFSET(DOT11_SSID_LIST, SSIDs))
    {
        Request->DATA.SET_INFORMATION.BytesNeeded =
            FIELD_OFFSET(DOT11_SSID_LIST, SSIDs);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    if (List->uNumOfEntries >= 1 &&
        Length >= FIELD_OFFSET(DOT11_SSID_LIST, SSIDs) + sizeof(DOT11_SSID))
    {
        Adapter->Dot11.DesiredSsid = List->SSIDs[0];
        Adapter->Dot11.HaveDesiredSsid =
            (List->SSIDs[0].uSSIDLength != 0);
    }
    else
    {
        /* Empty list == "connect to any SSID". */
        RtlZeroMemory(&Adapter->Dot11.DesiredSsid, sizeof(DOT11_SSID));
        Adapter->Dot11.HaveDesiredSsid = FALSE;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    Request->DATA.SET_INFORMATION.BytesRead = Length;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
VWifiDot11SetDesiredBssidList(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;
    PDOT11_BSSID_LIST List = (PDOT11_BSSID_LIST)Buffer;

    if (Length < FIELD_OFFSET(DOT11_BSSID_LIST, BSSIDs))
    {
        Request->DATA.SET_INFORMATION.BytesNeeded =
            FIELD_OFFSET(DOT11_BSSID_LIST, BSSIDs);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    if (List->uNumOfEntries >= 1 &&
        Length >= FIELD_OFFSET(DOT11_BSSID_LIST, BSSIDs) + sizeof(DOT11_MAC_ADDRESS))
    {
        RtlCopyMemory(Adapter->Dot11.DesiredBssid, List->BSSIDs[0],
                      VWIFI_ADDRESS_LENGTH);
        Adapter->Dot11.HaveDesiredBssid = TRUE;
    }
    else
    {
        Adapter->Dot11.HaveDesiredBssid = FALSE;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    Request->DATA.SET_INFORMATION.BytesRead = Length;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
VWifiDot11SetScanRequest(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;

    /* Accept either DOT11_SCAN_REQUEST or DOT11_SCAN_REQUEST_V2; the request
     * details are not needed for the simulation. */
    if (Length < FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer))
    {
        Request->DATA.SET_INFORMATION.BytesNeeded =
            FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer);
        return NDIS_STATUS_INVALID_LENGTH;
    }

    Request->DATA.SET_INFORMATION.BytesRead = Length;

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->ConnState == VWifiDisconnected)
    {
        Adapter->ConnState = VWifiScanning;
    }
    /* Complete the scan asynchronously like real radio hardware: return
     * NDIS_STATUS_PENDING and finish the OID via NdisMOidRequestComplete from
     * the engine.  Synchronous fallback if a scan is already outstanding. */
    if (Adapter->PendingScanOid == NULL && Adapter->MiniportAdapterHandle != NULL)
    {
        Adapter->PendingScanOid = Request;
        NdisReleaseSpinLock(&Adapter->Lock);
        DPRINT1("VWIFI: OID_DOT11_SCAN_REQUEST -> PENDING (async), scheduling SCAN_CONFIRM\n");
        VWifiScheduleJob(Adapter, VWifiJobScan, VWIFI_SCAN_DELAY_MS);
        return NDIS_STATUS_PENDING;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    DPRINT1("VWIFI: OID_DOT11_SCAN_REQUEST -> scheduling SCAN_CONFIRM (sync)\n");
    VWifiScheduleJob(Adapter, VWifiJobScan, VWIFI_SCAN_DELAY_MS);
    return NDIS_STATUS_SUCCESS;
}

/* OID_DOT11_CONNECT_REQUEST carries no data; the desired network comes from
 * the previously set DESIRED_SSID/BSSID lists. */
static
NDIS_STATUS
VWifiDot11SetConnectRequest(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    DPRINT1("VWIFI: OID_DOT11_CONNECT_REQUEST -> scheduling association\n");

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->ConnState = VWifiAssociating;
    NdisReleaseSpinLock(&Adapter->Lock);

    VWifiScheduleJob(Adapter, VWifiJobConnect, 50);

    Request->DATA.SET_INFORMATION.BytesRead =
        Request->DATA.SET_INFORMATION.InformationBufferLength;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
VWifiDot11SetDisconnectRequest(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    DPRINT1("VWIFI: OID_DOT11_DISCONNECT_REQUEST -> scheduling disassociation\n");

    VWifiScheduleJob(Adapter, VWifiJobDisconnect, 50);

    Request->DATA.SET_INFORMATION.BytesRead =
        Request->DATA.SET_INFORMATION.InformationBufferLength;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
VWifiDot11Set(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    NDIS_OID Oid = Request->DATA.SET_INFORMATION.Oid;
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;

    switch (Oid)
    {
        case OID_DOT11_CURRENT_PHY_TYPE:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->Dot11.CurrentPhyType = (DOT11_PHY_TYPE)*(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->Dot11.PacketFilter = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_CURRENT_OPERATION_MODE:
            if (Length < sizeof(DOT11_CURRENT_OPERATION_MODE))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded =
                    sizeof(DOT11_CURRENT_OPERATION_MODE);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->Dot11.CurrentOperationMode =
                ((PDOT11_CURRENT_OPERATION_MODE)Buffer)->uCurrentOpMode;
            Request->DATA.SET_INFORMATION.BytesRead =
                sizeof(DOT11_CURRENT_OPERATION_MODE);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_NIC_POWER_STATE:
            if (Length < sizeof(BOOLEAN))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(BOOLEAN);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Adapter->Dot11.RadioOn = *(PBOOLEAN)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(BOOLEAN);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_RTS_THRESHOLD:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->Dot11.RtsThreshold = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_FRAGMENTATION_THRESHOLD:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->Dot11.FragmentationThreshold = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_BEACON_PERIOD:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->Dot11.BeaconPeriod = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_CURRENT_CHANNEL_NUMBER:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->Dot11.CurrentChannel = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_AUTO_CONFIG_ENABLED:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->Dot11.AutoConfigEnabled = (*(PULONG)Buffer != 0);
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_OPERATIONAL_RATE_SET:
            /* Accept whatever rate set is programmed. */
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
        {
            PDOT11_AUTH_ALGORITHM_LIST List = (PDOT11_AUTH_ALGORITHM_LIST)Buffer;
            if (Length >= FIELD_OFFSET(DOT11_AUTH_ALGORITHM_LIST, AlgorithmIds) +
                              sizeof(DOT11_AUTH_ALGORITHM) &&
                List->uNumOfEntries >= 1)
            {
                Adapter->Dot11.AuthAlgorithm = List->AlgorithmIds[0];
            }
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }

        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
        {
            PDOT11_CIPHER_ALGORITHM_LIST List = (PDOT11_CIPHER_ALGORITHM_LIST)Buffer;
            if (Length >= FIELD_OFFSET(DOT11_CIPHER_ALGORITHM_LIST, AlgorithmIds) +
                              sizeof(DOT11_CIPHER_ALGORITHM) &&
                List->uNumOfEntries >= 1)
            {
                Adapter->Dot11.UnicastCipher = List->AlgorithmIds[0];
            }
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }

        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
        {
            PDOT11_CIPHER_ALGORITHM_LIST List = (PDOT11_CIPHER_ALGORITHM_LIST)Buffer;
            if (Length >= FIELD_OFFSET(DOT11_CIPHER_ALGORITHM_LIST, AlgorithmIds) +
                              sizeof(DOT11_CIPHER_ALGORITHM) &&
                List->uNumOfEntries >= 1)
            {
                Adapter->Dot11.MulticastCipher = List->AlgorithmIds[0];
            }
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }

        case OID_DOT11_DESIRED_BSS_TYPE:
            if (Length < sizeof(ULONG))
                return NDIS_STATUS_INVALID_LENGTH;
            Adapter->Dot11.DesiredBssType = (DOT11_BSS_TYPE)*(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_DESIRED_SSID_LIST:
            return VWifiDot11SetDesiredSsidList(Adapter, Request);

        case OID_DOT11_DESIRED_BSSID_LIST:
            return VWifiDot11SetDesiredBssidList(Adapter, Request);

        case OID_DOT11_SCAN_REQUEST:
            return VWifiDot11SetScanRequest(Adapter, Request);

        case OID_DOT11_CONNECT_REQUEST:
            return VWifiDot11SetConnectRequest(Adapter, Request);

        case OID_DOT11_DISCONNECT_REQUEST:
            return VWifiDot11SetDisconnectRequest(Adapter, Request);

        case OID_DOT11_RESET_REQUEST:
            /* Tear down any association and clear desired selectors. */
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->ConnState = VWifiDisconnected;
            Adapter->Dot11.HaveDesiredSsid = FALSE;
            Adapter->Dot11.HaveDesiredBssid = FALSE;
            NdisReleaseSpinLock(&Adapter->Lock);
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_FLUSH_BSS_LIST:
            /* The fake BSS list is static; nothing to flush. */
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_CIPHER_DEFAULT_KEY:
        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
        case OID_DOT11_CIPHER_KEY_MAPPING_KEY:
            /* Keys are acknowledged but unused; only open networks connect. */
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static
NDIS_STATUS
VWifiOidSet(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
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
            Adapter->Dot11.PacketFilter = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (Length < sizeof(ULONG))
            {
                Request->DATA.SET_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_NETWORK_LAYER_ADDRESSES:
        case OID_PNP_SET_POWER:
        case OID_PNP_ADD_WAKE_UP_PATTERN:
        case OID_PNP_REMOVE_WAKE_UP_PATTERN:
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;

        default:
            return VWifiDot11Set(Adapter, Request);
    }
}

/* Scan/connect/disconnect (and ENUM_BSS_LIST) may also arrive as method
 * requests; route them through the same transitions. */
static
NDIS_STATUS
VWifiOidMethod(
    IN PVWIFI_ADAPTER Adapter,
    IN PNDIS_OID_REQUEST Request)
{
    NDIS_OID Oid = Request->DATA.METHOD_INFORMATION.Oid;

    switch (Oid)
    {
        case OID_DOT11_SCAN_REQUEST:
            DPRINT1("VWIFI: OID_DOT11_SCAN_REQUEST (method) -> SCAN_CONFIRM\n");
            NdisAcquireSpinLock(&Adapter->Lock);
            if (Adapter->ConnState == VWifiDisconnected)
                Adapter->ConnState = VWifiScanning;
            NdisReleaseSpinLock(&Adapter->Lock);
            VWifiScheduleJob(Adapter, VWifiJobScan, VWIFI_SCAN_DELAY_MS);
            Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_CONNECT_REQUEST:
            DPRINT1("VWIFI: OID_DOT11_CONNECT_REQUEST (method) -> association\n");
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->ConnState = VWifiAssociating;
            NdisReleaseSpinLock(&Adapter->Lock);
            VWifiScheduleJob(Adapter, VWifiJobConnect, 50);
            Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_DISCONNECT_REQUEST:
            DPRINT1("VWIFI: OID_DOT11_DISCONNECT_REQUEST (method)\n");
            VWifiScheduleJob(Adapter, VWifiJobDisconnect, 50);
            Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
            return NDIS_STATUS_SUCCESS;

        case OID_DOT11_ENUM_BSS_LIST:
        {
            PUCHAR Out = Request->DATA.METHOD_INFORMATION.InformationBuffer;
            ULONG OutLen = Request->DATA.METHOD_INFORMATION.OutputBufferLength;
            ULONG Needed = 0;
            ULONG Written = VWifiBuildBssList(Adapter, Out, OutLen, &Needed);

            if (Written == 0)
            {
                Request->DATA.METHOD_INFORMATION.BytesNeeded = Needed;
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            Request->DATA.METHOD_INFORMATION.BytesWritten = Written;
            return NDIS_STATUS_SUCCESS;
        }

        case OID_DOT11_RESET_REQUEST:
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->ConnState = VWifiDisconnected;
            Adapter->Dot11.HaveDesiredSsid = FALSE;
            Adapter->Dot11.HaveDesiredBssid = FALSE;
            NdisReleaseSpinLock(&Adapter->Lock);
            Request->DATA.METHOD_INFORMATION.BytesWritten = 0;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

NDIS_STATUS
NTAPI
VWifiMiniportOidRequest(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_OID_REQUEST OidRequest)
{
    PVWIFI_ADAPTER Adapter = (PVWIFI_ADAPTER)MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return VWifiOidQuery(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return VWifiOidSet(Adapter, OidRequest);

        case NdisRequestMethod:
            return VWifiOidMethod(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

VOID
NTAPI
VWifiMiniportCancelOidRequest(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}
