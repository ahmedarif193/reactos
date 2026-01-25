/*
 * PROJECT:     ReactOS USB RNDIS Network Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NDIS 6.x OID request handler
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This file handles NDIS OID requests from the network stack using the
 * NDIS 6.x MiniportOidRequest unified handler pattern.
 */

#include "usbrndis.h"

/* Enable debug output for OID troubleshooting */
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
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_PHYSICAL_MEDIUM,

    /* NDIS 6.x OIDs */
    OID_GEN_LINK_SPEED_EX,
    OID_GEN_MAX_LINK_SPEED,
    OID_GEN_MEDIA_CONNECT_STATUS_EX,
    OID_GEN_MEDIA_DUPLEX_STATE,
    OID_GEN_LINK_STATE,
    OID_GEN_INTERRUPT_MODERATION,
    OID_GEN_STATISTICS,

    /* Statistics OIDs */
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_GEN_BYTES_XMIT,
    OID_GEN_BYTES_RCV,

    /* 802.3 (Ethernet) OIDs */
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE,
    OID_802_3_RCV_ERROR_ALIGNMENT,
    OID_802_3_XMIT_ONE_COLLISION,
    OID_802_3_XMIT_MORE_COLLISIONS,

    /* Power Management OIDs */
    OID_PNP_CAPABILITIES,
    OID_PNP_SET_POWER,
    OID_PNP_QUERY_POWER,
};

/* Vendor description */
static const CHAR VendorDescription[] = "USB RNDIS Network Adapter";

/*
 * NDIS 6.x OID values (if not defined in headers)
 */
#ifndef OID_GEN_LINK_SPEED_EX
#define OID_GEN_LINK_SPEED_EX               0x00010206
#endif

#ifndef OID_GEN_MAX_LINK_SPEED
#define OID_GEN_MAX_LINK_SPEED              0x00010207
#endif

#ifndef OID_GEN_MEDIA_CONNECT_STATUS_EX
#define OID_GEN_MEDIA_CONNECT_STATUS_EX     0x00010208
#endif

#ifndef OID_GEN_MEDIA_DUPLEX_STATE
#define OID_GEN_MEDIA_DUPLEX_STATE          0x00010209
#endif

#ifndef OID_GEN_LINK_STATE
#define OID_GEN_LINK_STATE                  0x0001020A
#endif

#ifndef OID_GEN_STATISTICS
#define OID_GEN_STATISTICS                  0x00020106
#endif

/*
 * NDIS_LINK_SPEED structure for NDIS 6.x
 */
#ifndef NDIS_LINK_SPEED_DEFINED
typedef struct _NDIS_LINK_SPEED {
    ULONG64 XmitLinkSpeed;
    ULONG64 RcvLinkSpeed;
} NDIS_LINK_SPEED, *PNDIS_LINK_SPEED;
#define NDIS_LINK_SPEED_DEFINED 1
#endif

/*
 * NDIS_STATISTICS_INFO structure
 */
#ifndef NDIS_STATISTICS_INFO_REVISION_1
#define NDIS_STATISTICS_INFO_REVISION_1     1
#endif

#ifndef NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV
#define NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV           0x00000001
#define NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT          0x00000002
#define NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS        0x00000004
#define NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR           0x00000008
#define NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR          0x00000010
#define NDIS_STATISTICS_FLAGS_VALID_XMIT_DISCARDS       0x00000020
#endif

#ifndef NDIS_STATISTICS_INFO_DEFINED
typedef struct _NDIS_STATISTICS_INFO {
    NDIS_OBJECT_HEADER  Header;
    ULONG               SupportedStatistics;
    ULONG64             ifInDiscards;
    ULONG64             ifInErrors;
    ULONG64             ifHCInOctets;
    ULONG64             ifHCInUcastPkts;
    ULONG64             ifHCInMulticastPkts;
    ULONG64             ifHCInBroadcastPkts;
    ULONG64             ifHCOutOctets;
    ULONG64             ifHCOutUcastPkts;
    ULONG64             ifHCOutMulticastPkts;
    ULONG64             ifHCOutBroadcastPkts;
    ULONG64             ifOutErrors;
    ULONG64             ifOutDiscards;
    ULONG64             ifHCInUcastOctets;
    ULONG64             ifHCInMulticastOctets;
    ULONG64             ifHCInBroadcastOctets;
    ULONG64             ifHCOutUcastOctets;
    ULONG64             ifHCOutMulticastOctets;
    ULONG64             ifHCOutBroadcastOctets;
} NDIS_STATISTICS_INFO, *PNDIS_STATISTICS_INFO;
#define NDIS_STATISTICS_INFO_DEFINED 1
#endif

/*
 * NDIS_INTERRUPT_MODERATION_PARAMETERS for NDIS 6.x
 */
#ifndef OID_GEN_INTERRUPT_MODERATION
#define OID_GEN_INTERRUPT_MODERATION        0x00010209
#endif

#ifndef NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1
#define NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1      16
#endif

#ifndef NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1
#define NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1     1
#endif

#ifndef NDIS_INTERRUPT_MODERATION_PARAMETERS_DEFINED
typedef struct _NDIS_INTERRUPT_MODERATION_PARAMETERS {
    NDIS_OBJECT_HEADER          Header;
    ULONG                       Flags;
    ULONG                       InterruptModeration;  /* NdisInterruptModerationXxx value */
} NDIS_INTERRUPT_MODERATION_PARAMETERS, *PNDIS_INTERRUPT_MODERATION_PARAMETERS;
#define NDIS_INTERRUPT_MODERATION_PARAMETERS_DEFINED 1
#endif

/*
 * RndisQueryInformation
 *
 * Handle query OID requests
 */
NDIS_STATUS
RndisQueryInformation(
    _In_ PRNDIS_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest)
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
        NDIS_INTERRUPT_MODERATION_PARAMETERS IntMod;
        UCHAR MacAddress[ETH_LENGTH_OF_ADDRESS];
    } Data;

    /* Log all OID queries for debugging */
    DPRINT1("USBRNDIS: QueryInformation OID 0x%08X, BufferLen=%lu\n", Oid, InfoBufferLength);

    *BytesWritten = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        /* === General Required OIDs === */

        case OID_GEN_SUPPORTED_LIST:
            CopySource = (PVOID)SupportedOidList;
            CopyLength = sizeof(SupportedOidList);
            break;

        case OID_GEN_HARDWARE_STATUS:
            if (Adapter->State >= RndisStateInitialized)
            {
                Data.HardwareStatus = NdisHardwareStatusReady;
            }
            else
            {
                Data.HardwareStatus = NdisHardwareStatusNotReady;
            }
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
            Data.Ulong = ETHERNET_MAX_FRAME_SIZE - ETHERNET_HEADER_SIZE;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data.Ulong = ETHERNET_MAX_MTU;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_LINK_SPEED:
            /* NDIS 5.x style: return in 100 bps units */
            Data.Ulong = (ULONG)(Adapter->LinkSpeed / 100);
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_LINK_SPEED_EX:
        case OID_GEN_MAX_LINK_SPEED:
            /* NDIS 6.x style: return in bps */
            Data.LinkSpeed.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkSpeed.RcvLinkSpeed = Adapter->LinkSpeed;
            CopySource = &Data.LinkSpeed;
            CopyLength = sizeof(NDIS_LINK_SPEED);
            break;

        case OID_GEN_TRANSMIT_BUFFER_SPACE:
            Data.Ulong = ETHERNET_MAX_FRAME_SIZE;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_RECEIVE_BUFFER_SPACE:
            Data.Ulong = RNDIS_MAX_TRANSFER_SIZE;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            Data.Ulong = 1;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_VENDOR_ID:
            if (Adapter->PermanentMacAddress[0] != 0 ||
                Adapter->PermanentMacAddress[1] != 0 ||
                Adapter->PermanentMacAddress[2] != 0)
            {
                Data.Ulong = ((ULONG)Adapter->PermanentMacAddress[0] << 16) |
                             ((ULONG)Adapter->PermanentMacAddress[1] << 8) |
                             (ULONG)Adapter->PermanentMacAddress[2];
            }
            else
            {
                Data.Ulong = 0x00FFFFFF;
            }
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_VENDOR_DESCRIPTION:
            CopySource = (PVOID)VendorDescription;
            CopyLength = sizeof(VendorDescription);
            break;

        case OID_GEN_VENDOR_DRIVER_VERSION:
            Data.Ulong = 0x00010000;  /* Version 1.0 */
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

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
            Data.Ulong = ETHERNET_MAX_FRAME_SIZE;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_MAC_OPTIONS:
            Data.Ulong = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            /* NDIS 5.x style: 0 = connected, 1 = disconnected */
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
            Data.MediaDuplexState = MediaDuplexStateFull;
            CopySource = &Data.MediaDuplexState;
            CopyLength = sizeof(NDIS_MEDIA_DUPLEX_STATE);
            break;

        case OID_GEN_LINK_STATE:
            NdisZeroMemory(&Data.LinkState, sizeof(NDIS_LINK_STATE));
            Data.LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            Data.LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
            Data.LinkState.MediaConnectState = Adapter->MediaState;
            Data.LinkState.MediaDuplexState = MediaDuplexStateFull;
            Data.LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;
            CopySource = &Data.LinkState;
            CopyLength = sizeof(NDIS_LINK_STATE);
            break;

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            Data.Ulong = 1;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_GEN_INTERRUPT_MODERATION:
            /*
             * USB RNDIS doesn't support interrupt moderation.
             * Return a properly formatted response indicating this.
             */
            {
                NDIS_INTERRUPT_MODERATION_PARAMETERS IntMod;
                NdisZeroMemory(&IntMod, sizeof(IntMod));
                IntMod.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
                IntMod.Header.Revision = NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
                IntMod.Header.Size = NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1;
                IntMod.Flags = 0;
                IntMod.InterruptModeration = NdisInterruptModerationNotSupported;
                NdisMoveMemory(&Data.IntMod, &IntMod, sizeof(IntMod));
                CopySource = &Data.IntMod;
                CopyLength = sizeof(NDIS_INTERRUPT_MODERATION_PARAMETERS);
            }
            break;

        /* === Statistics OIDs === */
        /*
         * These statistics OIDs support both ULONG (4-byte) and ULONG64 (8-byte)
         * responses for NDIS 5.x/6.x compatibility. Return the format that matches
         * the caller's buffer size.
         */

        case OID_GEN_XMIT_OK:
            if (InfoBufferLength >= sizeof(ULONG64))
            {
                Data.Ulong64 = Adapter->TxOkCount;
                CopySource = &Data.Ulong64;
                CopyLength = sizeof(ULONG64);
            }
            else
            {
                Data.Ulong = (ULONG)Adapter->TxOkCount;
                CopySource = &Data.Ulong;
                CopyLength = sizeof(ULONG);
            }
            break;

        case OID_GEN_RCV_OK:
            if (InfoBufferLength >= sizeof(ULONG64))
            {
                Data.Ulong64 = Adapter->RxOkCount;
                CopySource = &Data.Ulong64;
                CopyLength = sizeof(ULONG64);
            }
            else
            {
                Data.Ulong = (ULONG)Adapter->RxOkCount;
                CopySource = &Data.Ulong;
                CopyLength = sizeof(ULONG);
            }
            break;

        case OID_GEN_XMIT_ERROR:
            if (InfoBufferLength >= sizeof(ULONG64))
            {
                Data.Ulong64 = Adapter->TxErrorCount;
                CopySource = &Data.Ulong64;
                CopyLength = sizeof(ULONG64);
            }
            else
            {
                Data.Ulong = (ULONG)Adapter->TxErrorCount;
                CopySource = &Data.Ulong;
                CopyLength = sizeof(ULONG);
            }
            break;

        case OID_GEN_RCV_ERROR:
            if (InfoBufferLength >= sizeof(ULONG64))
            {
                Data.Ulong64 = Adapter->RxErrorCount;
                CopySource = &Data.Ulong64;
                CopyLength = sizeof(ULONG64);
            }
            else
            {
                Data.Ulong = (ULONG)Adapter->RxErrorCount;
                CopySource = &Data.Ulong;
                CopyLength = sizeof(ULONG);
            }
            break;

        case OID_GEN_RCV_NO_BUFFER:
            if (InfoBufferLength >= sizeof(ULONG64))
            {
                Data.Ulong64 = Adapter->RxNoBufferCount;
                CopySource = &Data.Ulong64;
                CopyLength = sizeof(ULONG64);
            }
            else
            {
                Data.Ulong = (ULONG)Adapter->RxNoBufferCount;
                CopySource = &Data.Ulong;
                CopyLength = sizeof(ULONG);
            }
            break;

        case OID_GEN_BYTES_XMIT:
            if (InfoBufferLength >= sizeof(ULONG64))
            {
                Data.Ulong64 = Adapter->TxBytes;
                CopySource = &Data.Ulong64;
                CopyLength = sizeof(ULONG64);
            }
            else
            {
                Data.Ulong = (ULONG)Adapter->TxBytes;
                CopySource = &Data.Ulong;
                CopyLength = sizeof(ULONG);
            }
            break;

        case OID_GEN_BYTES_RCV:
            if (InfoBufferLength >= sizeof(ULONG64))
            {
                Data.Ulong64 = Adapter->RxBytes;
                CopySource = &Data.Ulong64;
                CopyLength = sizeof(ULONG64);
            }
            else
            {
                Data.Ulong = (ULONG)Adapter->RxBytes;
                CopySource = &Data.Ulong;
                CopyLength = sizeof(ULONG);
            }
            break;

        case OID_GEN_STATISTICS:
            NdisZeroMemory(&Data.Statistics, sizeof(NDIS_STATISTICS_INFO));
            Data.Statistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.Statistics.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
            Data.Statistics.Header.Size = sizeof(NDIS_STATISTICS_INFO);
            Data.Statistics.SupportedStatistics =
                NDIS_STATISTICS_FLAGS_VALID_BYTES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_BYTES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR;
            Data.Statistics.ifHCInOctets = Adapter->RxBytes;
            Data.Statistics.ifHCOutOctets = Adapter->TxBytes;
            Data.Statistics.ifInErrors = Adapter->RxErrorCount;
            Data.Statistics.ifOutErrors = Adapter->TxErrorCount;
            Data.Statistics.ifHCInUcastPkts = Adapter->RxOkCount;
            Data.Statistics.ifHCOutUcastPkts = Adapter->TxOkCount;
            CopySource = &Data.Statistics;
            CopyLength = sizeof(NDIS_STATISTICS_INFO);
            break;

        /* === 802.3 (Ethernet) OIDs === */

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
            CopyLength = Adapter->MulticastListCount * ETH_LENGTH_OF_ADDRESS;
            break;

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Data.Ulong = RNDIS_MAX_MULTICAST_ADDRESSES;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        case OID_802_3_RCV_ERROR_ALIGNMENT:
        case OID_802_3_XMIT_ONE_COLLISION:
        case OID_802_3_XMIT_MORE_COLLISIONS:
            Data.Ulong = 0;
            CopySource = &Data.Ulong;
            CopyLength = sizeof(ULONG);
            break;

        /* === Power Management OIDs === */

        case OID_PNP_CAPABILITIES:
            *BytesNeeded = sizeof(NDIS_PNP_CAPABILITIES);
            if (InfoBufferLength < sizeof(NDIS_PNP_CAPABILITIES))
            {
                Status = NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            else
            {
                PNDIS_PNP_CAPABILITIES PmCaps = (PNDIS_PNP_CAPABILITIES)InfoBuffer;
                NdisZeroMemory(PmCaps, sizeof(NDIS_PNP_CAPABILITIES));
                PmCaps->WakeUpCapabilities.MinMagicPacketWakeUp = NdisDeviceStateUnspecified;
                PmCaps->WakeUpCapabilities.MinPatternWakeUp = NdisDeviceStateUnspecified;
                PmCaps->WakeUpCapabilities.MinLinkChangeWakeUp = NdisDeviceStateUnspecified;
                *BytesWritten = sizeof(NDIS_PNP_CAPABILITIES);
            }
            return Status;

        case OID_PNP_QUERY_POWER:
            /* We support all power states */
            return NDIS_STATUS_SUCCESS;

        default:
            DPRINT1("USBRNDIS: Unsupported query OID 0x%08X\n", Oid);
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
            DPRINT1("USBRNDIS: OID 0x%08X BUFFER_TOO_SHORT (need %lu, have %lu)\n",
                    Oid, CopyLength, InfoBufferLength);
        }
        else
        {
            NdisMoveMemory(InfoBuffer, CopySource, CopyLength);
            *BytesWritten = CopyLength;
            DPRINT1("USBRNDIS: OID 0x%08X SUCCESS (wrote %lu bytes)\n", Oid, CopyLength);
        }
    }

    return Status;
}

/*
 * RndisSetInformation
 *
 * Handle set OID requests
 */
NDIS_STATUS
RndisSetInformation(
    _In_ PRNDIS_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    NDIS_OID Oid = OidRequest->DATA.SET_INFORMATION.Oid;
    PVOID InfoBuffer = OidRequest->DATA.SET_INFORMATION.InformationBuffer;
    ULONG InfoBufferLength = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;
    PUINT BytesRead = &OidRequest->DATA.SET_INFORMATION.BytesRead;
    PUINT BytesNeeded = &OidRequest->DATA.SET_INFORMATION.BytesNeeded;

    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    ULONG PacketFilter;
    NTSTATUS NtStatus;

    DPRINT("USBRNDIS: SetInformation OID 0x%08X\n", Oid);

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (InfoBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            PacketFilter = *(PULONG)InfoBuffer;
            DPRINT("USBRNDIS: Setting packet filter to 0x%08X\n", PacketFilter);

            /* Validate filter bits */
            if (PacketFilter & ~(NDIS_PACKET_TYPE_DIRECTED |
                                 NDIS_PACKET_TYPE_MULTICAST |
                                 NDIS_PACKET_TYPE_ALL_MULTICAST |
                                 NDIS_PACKET_TYPE_BROADCAST |
                                 NDIS_PACKET_TYPE_PROMISCUOUS))
            {
                Status = NDIS_STATUS_NOT_SUPPORTED;
                break;
            }

            /* Set filter on device */
            NtStatus = RndisSetPacketFilter(Adapter, PacketFilter);
            if (!NT_SUCCESS(NtStatus))
            {
                DPRINT1("USBRNDIS: Failed to set packet filter (0x%08X)\n", NtStatus);

                /*
                 * For CDC-NCM devices, SET_ETHERNET_PACKET_FILTER may not be supported
                 * (e.g., VirtualBox CDC-NCM returns STALL). This is non-fatal because
                 * CDC-NCM devices typically receive all packets anyway.
                 *
                 * Only treat "not supported" errors as non-fatal. Hard I/O failures
                 * like device removed should propagate the error.
                 */
                if (Adapter->IsCdcNcm &&
                    NtStatus != STATUS_NO_SUCH_DEVICE &&
                    NtStatus != STATUS_DEVICE_NOT_CONNECTED &&
                    NtStatus != STATUS_DEVICE_REMOVED &&
                    NtStatus != STATUS_DELETE_PENDING &&
                    NtStatus != STATUS_INSUFFICIENT_RESOURCES)
                {
                    DPRINT1("USBRNDIS: CDC-NCM: Packet filter not supported, proceeding anyway\n");
                    Adapter->PacketFilter = PacketFilter;
                    *BytesRead = sizeof(ULONG);
                    Status = NDIS_STATUS_SUCCESS;
                }
                else
                {
                    Status = NDIS_STATUS_FAILURE;
                }
            }
            else
            {
                Adapter->PacketFilter = PacketFilter;
                *BytesRead = sizeof(ULONG);
            }
            break;

        case OID_GEN_CURRENT_LOOKAHEAD:
            /* Accept any lookahead value */
            if (InfoBufferLength < sizeof(ULONG))
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
            if (InfoBufferLength % ETH_LENGTH_OF_ADDRESS != 0)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                break;
            }

            if (InfoBufferLength / ETH_LENGTH_OF_ADDRESS > RNDIS_MAX_MULTICAST_ADDRESSES)
            {
                *BytesNeeded = RNDIS_MAX_MULTICAST_ADDRESSES * ETH_LENGTH_OF_ADDRESS;
                Status = NDIS_STATUS_MULTICAST_FULL;
                break;
            }

            /* Copy multicast list */
            NdisZeroMemory(Adapter->MulticastList, sizeof(Adapter->MulticastList));
            Adapter->MulticastListCount = InfoBufferLength / ETH_LENGTH_OF_ADDRESS;
            NdisMoveMemory(Adapter->MulticastList, InfoBuffer, InfoBufferLength);

            /* Program device filters */
            if (Adapter->IsCdcEcm || Adapter->IsCdcNcm)
            {
                NtStatus = RndisUsbSetEthernetMulticastFilters(
                    Adapter,
                    (PUCHAR)Adapter->MulticastList,
                    (USHORT)Adapter->MulticastListCount);
                if (!NT_SUCCESS(NtStatus))
                {
                    Status = NDIS_STATUS_FAILURE;
                    break;
                }
            }
            else
            {
                RndisSetOid(Adapter, RNDIS_OID_802_3_MULTICAST_LIST,
                            Adapter->MulticastList,
                            Adapter->MulticastListCount * ETH_LENGTH_OF_ADDRESS);
            }

            *BytesRead = InfoBufferLength;
            break;

        case OID_PNP_SET_POWER:
            if (InfoBufferLength < sizeof(NDIS_DEVICE_POWER_STATE))
            {
                *BytesNeeded = sizeof(NDIS_DEVICE_POWER_STATE);
                Status = NDIS_STATUS_INVALID_LENGTH;
            }
            else
            {
                /* Accept power state change */
                *BytesRead = sizeof(NDIS_DEVICE_POWER_STATE);
            }
            break;

        default:
            DPRINT1("USBRNDIS: Unsupported SET OID 0x%08X\n", Oid);
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    return Status;
}

/*
 * RndisOidRequest
 *
 * NDIS 6.x unified OID request handler
 */
NDIS_STATUS
NTAPI
RndisOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            Status = RndisQueryInformation(Adapter, OidRequest);
            break;

        case NdisRequestSetInformation:
            Status = RndisSetInformation(Adapter, OidRequest);
            break;

        case NdisRequestMethod:
            /* Method requests not supported */
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;

        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    return Status;
}

/*
 * RndisCancelOidRequest
 *
 * NDIS 6.x OID request cancellation handler
 */
VOID
NTAPI
RndisCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);

    /* We complete OID requests synchronously, so nothing to cancel */
}
