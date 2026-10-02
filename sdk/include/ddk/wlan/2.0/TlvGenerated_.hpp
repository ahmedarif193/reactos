/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     WiFiCx TLV generator and parser interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "arrayofelements.hpp"

typedef struct _WDI_START_AP_PARAMETERS
{
    UINT32 BeaconPeriod;
    UINT32 DTIMPeriod;
    BOOLEAN ExcludeUnencrypted;
    BOOLEAN DataRateSupport802_11b;
    BOOLEAN AcceptNonP2PClients;
    BOOLEAN MustUseSpecifiedChannels;
    BOOLEAN PreferOverStation;
#ifdef __cplusplus
    _WDI_START_AP_PARAMETERS() : BeaconPeriod( 0 ), DTIMPeriod( 0 ), ExcludeUnencrypted( FALSE ), DataRateSupport802_11b( FALSE ), AcceptNonP2PClients( FALSE ), MustUseSpecifiedChannels( FALSE ), PreferOverStation( FALSE )
    {
    };
#endif
} WDI_START_AP_PARAMETERS, *PWDI_START_AP_PARAMETERS;

typedef struct _WDI_START_AP_PARAMETERS_V1_0_10
{
    UINT32 BeaconPeriod;
    UINT32 DTIMPeriod;
    BOOLEAN ExcludeUnencrypted;
    BOOLEAN DataRateSupport802_11b;
    BOOLEAN AcceptNonP2PClients;
    BOOLEAN MustUseSpecifiedChannels;
#ifdef __cplusplus
    _WDI_START_AP_PARAMETERS_V1_0_10() : BeaconPeriod( 0 ), DTIMPeriod( 0 ), ExcludeUnencrypted( FALSE ), DataRateSupport802_11b( FALSE ), AcceptNonP2PClients( FALSE ), MustUseSpecifiedChannels( FALSE )
    {
    };
#endif
} WDI_START_AP_PARAMETERS_V1_0_10, *PWDI_START_AP_PARAMETERS_V1_0_10;

typedef struct _WDI_START_AP_PARAMETERS_V1_0_1
{
    UINT32 BeaconPeriod;
    UINT32 DTIMPeriod;
    BOOLEAN ExcludeUnencrypted;
    BOOLEAN DataRateSupport802_11b;
#ifdef __cplusplus
    _WDI_START_AP_PARAMETERS_V1_0_1() : BeaconPeriod( 0 ), DTIMPeriod( 0 ), ExcludeUnencrypted( FALSE ), DataRateSupport802_11b( FALSE )
    {
    };
#endif
} WDI_START_AP_PARAMETERS_V1_0_1, *PWDI_START_AP_PARAMETERS_V1_0_1;

typedef struct _WDI_DELETE_PORT_PARAMETERS
{
    UINT16 PortNumber;
#ifdef __cplusplus
    _WDI_DELETE_PORT_PARAMETERS() : PortNumber( 0 )
    {
    };
#endif
} WDI_DELETE_PORT_PARAMETERS, *PWDI_DELETE_PORT_PARAMETERS;

typedef struct _WDI_CREATE_PORT_PARAMETERS
{
    UINT16 OpModeMask;
    UINT32 NdisPortNumber;
#ifdef __cplusplus
    _WDI_CREATE_PORT_PARAMETERS() : OpModeMask( 0 ), NdisPortNumber( 0 )
    {
    };
#endif
} WDI_CREATE_PORT_PARAMETERS, *PWDI_CREATE_PORT_PARAMETERS;

typedef struct _WDI_DOT11_RESET_PARAMETERS
{
    UINT8 SetDefaultMIB;
#ifdef __cplusplus
    _WDI_DOT11_RESET_PARAMETERS() : SetDefaultMIB( 0 )
    {
    };
#endif
} WDI_DOT11_RESET_PARAMETERS, *PWDI_DOT11_RESET_PARAMETERS;

typedef struct _WDI_SET_DEFAULT_KEY_ID_STRUCT
{
    UINT32 KeyID;
#ifdef __cplusplus
    _WDI_SET_DEFAULT_KEY_ID_STRUCT() : KeyID( 0 )
    {
    };
#endif
} WDI_SET_DEFAULT_KEY_ID_STRUCT, *PWDI_SET_DEFAULT_KEY_ID_STRUCT;

typedef struct _WDI_PMK_NAME_STRUCT
{
    WDI_TYPE_PMK_NAME pmkname;
#ifdef __cplusplus
    _WDI_PMK_NAME_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_PMK_NAME_STRUCT ) );
    };
#endif
} WDI_PMK_NAME_STRUCT, *PWDI_PMK_NAME_STRUCT;

typedef struct _WDI_PRIVACY_EXEMPTION_PARAMETERS_STRUCT
{
    UINT16 EtherType;
    WDI_EXEMPTION_ACTION_TYPE ExemptionActionType;
    WDI_EXEMPTION_PACKET_TYPE ExemptionPacketType;
#ifdef __cplusplus
    _WDI_PRIVACY_EXEMPTION_PARAMETERS_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_PRIVACY_EXEMPTION_PARAMETERS_STRUCT ) );
    };
#endif
} WDI_PRIVACY_EXEMPTION_PARAMETERS_STRUCT, *PWDI_PRIVACY_EXEMPTION_PARAMETERS_STRUCT;

typedef struct _WDI_LINK_STATE_CHANGE_PARAMETERS_STRUCT
{
    WDI_MAC_ADDRESS PeerMACAddress;
    UINT32 TxLinkSpeed;
    UINT32 RxLinkSpeed;
    UINT8 LinkQuality;
#ifdef __cplusplus
    _WDI_LINK_STATE_CHANGE_PARAMETERS_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_LINK_STATE_CHANGE_PARAMETERS_STRUCT ) );
    };
#endif
} WDI_LINK_STATE_CHANGE_PARAMETERS_STRUCT, *PWDI_LINK_STATE_CHANGE_PARAMETERS_STRUCT;

typedef struct _WDI_LINK_INFO_PARAMETERS_STRUCT
{
    UINT32 LinkID;
    WDI_MAC_ADDRESS LocalLinkMACAddress;
    WDI_MAC_ADDRESS PeerLinkMACAddress;
    WDI_CHANNEL_NUMBER ChannelNumber;
    WDI_BAND_ID BandId;
    INT32 RSSI;
    UINT32 Bandwidth;
    UINT32 TxMCS;
    UINT32 RxMCS;
#ifdef __cplusplus
    _WDI_LINK_INFO_PARAMETERS_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_LINK_INFO_PARAMETERS_STRUCT ) );
    };
#endif
} WDI_LINK_INFO_PARAMETERS_STRUCT, *PWDI_LINK_INFO_PARAMETERS_STRUCT;

typedef struct _WDI_LINK_INFO_PARAMETERS_STRUCT_V2_0_3
{
    UINT32 LocalLinkID;
    UINT32 PeerLinkID;
    WDI_MAC_ADDRESS LocalLinkMACAddress;
    WDI_MAC_ADDRESS PeerLinkMACAddress;
    WDI_CHANNEL_NUMBER ChannelNumber;
    WDI_BAND_ID BandId;
    INT32 RSSI;
    UINT32 Bandwidth;
    UINT32 MCS;
#ifdef __cplusplus
    _WDI_LINK_INFO_PARAMETERS_STRUCT_V2_0_3()
    {
        memset( this, 0, sizeof( _WDI_LINK_INFO_PARAMETERS_STRUCT_V2_0_3 ) );
    };
#endif
} WDI_LINK_INFO_PARAMETERS_STRUCT_V2_0_3, *PWDI_LINK_INFO_PARAMETERS_STRUCT_V2_0_3;

typedef struct _WDI_P2P_CHANNEL
{
    UINT8 CountryOrRegionString[3];
    UINT8 OperatingClass;
    WDI_CHANNEL_NUMBER ChannelNumber;
#ifdef __cplusplus
    _WDI_P2P_CHANNEL()
    {
        memset( this, 0, sizeof( _WDI_P2P_CHANNEL ) );
    };
#endif
} WDI_P2P_CHANNEL, *PWDI_P2P_CHANNEL;

typedef struct _WDI_P2P_DEVICE_TYPE
{
    UINT16 CategoryID;
    UINT8 OUI[4];
    UINT16 SubcategoryID;
#ifdef __cplusplus
    _WDI_P2P_DEVICE_TYPE()
    {
        memset( this, 0, sizeof( _WDI_P2P_DEVICE_TYPE ) );
    };
#endif
} WDI_P2P_DEVICE_TYPE, *PWDI_P2P_DEVICE_TYPE;

typedef struct _WDI_P2P_DEVICE_INFO_PARAMETERS
{
    UINT8 DeviceAddress[6];
    UINT16 ConfigurationMethods;
    WDI_P2P_DEVICE_TYPE DeviceType;
#ifdef __cplusplus
    _WDI_P2P_DEVICE_INFO_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_P2P_DEVICE_INFO_PARAMETERS ) );
    };
#endif
} WDI_P2P_DEVICE_INFO_PARAMETERS, *PWDI_P2P_DEVICE_INFO_PARAMETERS;

typedef struct _WDI_TCP_RSC_STATISTICS_STRUCT
{
    UINT64 CoalescedPkts;
    UINT64 CoalescedOctets;
    UINT64 CoalesceEvents;
    UINT64 Aborts;
#ifdef __cplusplus
    _WDI_TCP_RSC_STATISTICS_STRUCT() : CoalescedPkts( 0 ), CoalescedOctets( 0 ), CoalesceEvents( 0 ), Aborts( 0 )
    {
    };
#endif
} WDI_TCP_RSC_STATISTICS_STRUCT, *PWDI_TCP_RSC_STATISTICS_STRUCT;

typedef struct _WDI_P2P_DEVICE_CAPABILITY_PARAMETERS
{
    UINT8 DeviceCapabilityBitmap;
    UINT8 OSSetDeviceCapabilities;
    UINT32 WPSVersionsEnabled;
#ifdef __cplusplus
    _WDI_P2P_DEVICE_CAPABILITY_PARAMETERS() : DeviceCapabilityBitmap( 0 ), OSSetDeviceCapabilities( 0 ), WPSVersionsEnabled( 0 )
    {
    };
#endif
} WDI_P2P_DEVICE_CAPABILITY_PARAMETERS, *PWDI_P2P_DEVICE_CAPABILITY_PARAMETERS;

typedef struct _WDI_BSSID_INFO_STRUCT
{
    UINT8 APReachability;
    UINT8 Security;
    UINT8 KeyScope;
    UINT8 SpectrumManagement;
    UINT8 QoS;
    UINT8 APSD;
    UINT8 RadioMeasurement;
    UINT8 DelayedBlockAck;
    UINT8 ImmediateBlockAck;
    UINT8 MobilityDomain;
    UINT8 HighThroughput;
#ifdef __cplusplus
    _WDI_BSSID_INFO_STRUCT() : APReachability( 0 ), Security( 0 ), KeyScope( 0 ), SpectrumManagement( 0 ), QoS( 0 ), APSD( 0 ), RadioMeasurement( 0 ), DelayedBlockAck( 0 ), ImmediateBlockAck( 0 ), MobilityDomain( 0 ), HighThroughput( 0 )
    {
    };
#endif
} WDI_BSSID_INFO_STRUCT, *PWDI_BSSID_INFO_STRUCT;

typedef struct _WDI_P2P_GROUP_OWNER_CAPABILITY_PARAMETERS
{
    UINT8 GroupCapabilityBitmap;
    UINT8 OSSetGroupCapabilities;
    UINT32 MaximumGroupLimit;
#ifdef __cplusplus
    _WDI_P2P_GROUP_OWNER_CAPABILITY_PARAMETERS() : GroupCapabilityBitmap( 0 ), OSSetGroupCapabilities( 0 ), MaximumGroupLimit( 0 )
    {
    };
#endif
} WDI_P2P_GROUP_OWNER_CAPABILITY_PARAMETERS, *PWDI_P2P_GROUP_OWNER_CAPABILITY_PARAMETERS;

typedef struct _WDI_P2P_DISCOVER_MODE
{
    WDI_P2P_DISCOVER_TYPE DiscoveryType;
    BOOLEAN ForcedDiscovery;
    WDI_P2P_SCAN_TYPE ScanType;
    WDI_P2P_SERVICE_DISCOVERY_TYPE ServiceDiscoveryType;
    BOOLEAN ScanRepeatCount;
    UINT32 TimeBetweenScans;
#ifdef __cplusplus
    _WDI_P2P_DISCOVER_MODE()
    {
        memset( this, 0, sizeof( _WDI_P2P_DISCOVER_MODE ) );
    };
#endif
} WDI_P2P_DISCOVER_MODE, *PWDI_P2P_DISCOVER_MODE;

typedef struct _WDI_P2P_BACKGROUND_DISCOVER_MODE
{
    WDI_P2P_DISCOVER_TYPE DiscoveryType;
    WDI_P2P_SERVICE_DISCOVERY_TYPE ServiceDiscoveryType;
    UINT32 DeviceVisiblilityTimeout;
#ifdef __cplusplus
    _WDI_P2P_BACKGROUND_DISCOVER_MODE()
    {
        memset( this, 0, sizeof( _WDI_P2P_BACKGROUND_DISCOVER_MODE ) );
    };
#endif
} WDI_P2P_BACKGROUND_DISCOVER_MODE, *PWDI_P2P_BACKGROUND_DISCOVER_MODE;

typedef struct _WDI_SCAN_DWELL_TIME
{
    UINT32 ActiveChannelDwellTime;
    UINT32 PassiveChannelDwellTime;
    UINT32 MaximumScanTime;
#ifdef __cplusplus
    _WDI_SCAN_DWELL_TIME() : ActiveChannelDwellTime( 0 ), PassiveChannelDwellTime( 0 ), MaximumScanTime( 0 )
    {
    };
#endif
} WDI_SCAN_DWELL_TIME, *PWDI_SCAN_DWELL_TIME;

typedef struct _WDI_P2P_LISTEN_DURATION
{
    UINT32 CycleDuration;
    UINT32 ListenActiveDuration;
#ifdef __cplusplus
    _WDI_P2P_LISTEN_DURATION() : CycleDuration( 0 ), ListenActiveDuration( 0 )
    {
    };
#endif
} WDI_P2P_LISTEN_DURATION, *PWDI_P2P_LISTEN_DURATION;

typedef struct _WDI_CANCEL_PARAMETERS_STRUCT
{
    NDIS_OID OriginalTaskOID;
    UINT32 OriginalTransactionId;
    WDI_PORT_ID OriginalPortId;
#ifdef __cplusplus
    _WDI_CANCEL_PARAMETERS_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_CANCEL_PARAMETERS_STRUCT ) );
    };
#endif
} WDI_CANCEL_PARAMETERS_STRUCT, *PWDI_CANCEL_PARAMETERS_STRUCT;

typedef struct _WDI_PM_PROTOCOL_OFFLOAD_IPv4ARP_STRUCT
{
    UINT32 ProtocolOffloadId;
    UINT8 RemoteIPV4Address[4];
    UINT8 HostIPV4Address[4];
    WDI_MAC_ADDRESS MacAddress;
#ifdef __cplusplus
    _WDI_PM_PROTOCOL_OFFLOAD_IPv4ARP_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_PM_PROTOCOL_OFFLOAD_IPv4ARP_STRUCT ) );
    };
#endif
} WDI_PM_PROTOCOL_OFFLOAD_IPv4ARP_STRUCT, *PWDI_PM_PROTOCOL_OFFLOAD_IPv4ARP_STRUCT;

typedef struct _WDI_PM_PROTOCOL_OFFLOAD_IPv6_STRUCT
{
    UINT32 ProtocolOffloadId;
    UINT8 RemoteIPV6Address[16];
    UINT8 SolicitdNodeIPv6Address[16];
    UINT8 TargetIPV6Address1[16];
    UINT8 TargetIPV6Address2[16];
    WDI_MAC_ADDRESS MacAddress;
#ifdef __cplusplus
    _WDI_PM_PROTOCOL_OFFLOAD_IPv6_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_PM_PROTOCOL_OFFLOAD_IPv6_STRUCT ) );
    };
#endif
} WDI_PM_PROTOCOL_OFFLOAD_IPv6_STRUCT, *PWDI_PM_PROTOCOL_OFFLOAD_IPv6_STRUCT;

typedef struct _WDI_PM_PROTOCOL_OFFLOAD_80211RSN_REKEY_STRUCT
{
    UINT32 ProtocolOffloadId;
    UINT64 ReplayCounter;
    UINT8 KCK_CONTENT[16];
    UINT8 KEK_CONTENT[16];
#ifdef __cplusplus
    _WDI_PM_PROTOCOL_OFFLOAD_80211RSN_REKEY_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_PM_PROTOCOL_OFFLOAD_80211RSN_REKEY_STRUCT ) );
    };
#endif
} WDI_PM_PROTOCOL_OFFLOAD_80211RSN_REKEY_STRUCT, *PWDI_PM_PROTOCOL_OFFLOAD_80211RSN_REKEY_STRUCT;

typedef struct _WDI_SCAN_MODE
{
    BOOLEAN ScanRepeatCount;
    WDI_SCAN_TYPE AllowedScanType;
    BOOLEAN LiveUpdatesNeeded;
    WDI_SCAN_TRIGGER ScanTrigger;
#ifdef __cplusplus
    _WDI_SCAN_MODE()
    {
        memset( this, 0, sizeof( _WDI_SCAN_MODE ) );
    };
#endif
} WDI_SCAN_MODE, *PWDI_SCAN_MODE;

typedef struct _WDI_CREATE_PORT_COMPLETE_STRUCT
{
    WDI_MAC_ADDRESS MacAddress;
    UINT16 PortNumber;
#ifdef __cplusplus
    _WDI_CREATE_PORT_COMPLETE_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_CREATE_PORT_COMPLETE_STRUCT ) );
    };
#endif
} WDI_CREATE_PORT_COMPLETE_STRUCT, *PWDI_CREATE_PORT_COMPLETE_STRUCT;

typedef struct _WDI_P2P_GO_NEGOTIATION_REQUEST_PARAMETERS
{
    UINT8 GOIntent;
    BOOLEAN TieBreaker;
    UINT16 GOConfigTimeout;
    UINT16 ClientConfigTimeout;
    WDI_MAC_ADDRESS IntendedInterfaceAddress;
    UINT8 GroupCapability;
    UINT8 OSSetGroupCapabilityBitmask;
#ifdef __cplusplus
    _WDI_P2P_GO_NEGOTIATION_REQUEST_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_P2P_GO_NEGOTIATION_REQUEST_PARAMETERS ) );
    };
#endif
} WDI_P2P_GO_NEGOTIATION_REQUEST_PARAMETERS, *PWDI_P2P_GO_NEGOTIATION_REQUEST_PARAMETERS;

typedef struct _WDI_SIGNAL_INFO
{
    INT32 RSSI;
    UINT32 LinkQuality;
#ifdef __cplusplus
    _WDI_SIGNAL_INFO() : RSSI( 0 ), LinkQuality( 0 )
    {
    };
#endif
} WDI_SIGNAL_INFO, *PWDI_SIGNAL_INFO;

typedef struct _WDI_BSS_ENTRY_CHANNEL_INFO
{
    WDI_CHANNEL_NUMBER ChannelNumber;
    WDI_BAND_ID BandId;
#ifdef __cplusplus
    _WDI_BSS_ENTRY_CHANNEL_INFO()
    {
        memset( this, 0, sizeof( _WDI_BSS_ENTRY_CHANNEL_INFO ) );
    };
#endif
} WDI_BSS_ENTRY_CHANNEL_INFO, *PWDI_BSS_ENTRY_CHANNEL_INFO;

typedef struct _WDI_BSS_ENTRY_AGE_INFO
{
    UINT64 HostTimeStamp;
    BOOLEAN CachedInformation;
#ifdef __cplusplus
    _WDI_BSS_ENTRY_AGE_INFO() : HostTimeStamp( 0 ), CachedInformation( FALSE )
    {
    };
#endif
} WDI_BSS_ENTRY_AGE_INFO, *PWDI_BSS_ENTRY_AGE_INFO;

typedef struct _WDI_P2P_ACTION_FRAME_REQUEST_PARAMETERS
{
    WDI_P2P_ACTION_FRAME_TYPE RequestFrameType;
    WDI_MAC_ADDRESS PeerDeviceAddress;
    UINT8 DialogToken;
    UINT32 SendTimeout;
    UINT32 PostACKDwellTime;
#ifdef __cplusplus
    _WDI_P2P_ACTION_FRAME_REQUEST_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_P2P_ACTION_FRAME_REQUEST_PARAMETERS ) );
    };
#endif
} WDI_P2P_ACTION_FRAME_REQUEST_PARAMETERS, *PWDI_P2P_ACTION_FRAME_REQUEST_PARAMETERS;

typedef struct _WDI_MAC_STATISTICS_PARAMETERS
{
    WDI_MAC_ADDRESS MACAddress;
    UINT64 TransmittedFrameCount;
    UINT64 ReceivedFrameCount;
    UINT64 WEPExcludedCount;
    UINT64 TKIPLocalMICFailures;
    UINT64 TKIPReplays;
    UINT64 TKIPICVErrorCount;
    UINT64 CCMPFormatErrors;
    UINT64 CCMPReplays;
    UINT64 CCMPDecryptErrors;
    UINT64 WEPUndecryptableCount;
    UINT64 WEPICVErrorCount;
    UINT64 DecryptSuccessCount;
    UINT64 DecryptFailureCount;
#ifdef __cplusplus
    _WDI_MAC_STATISTICS_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_MAC_STATISTICS_PARAMETERS ) );
    };
#endif
} WDI_MAC_STATISTICS_PARAMETERS, *PWDI_MAC_STATISTICS_PARAMETERS;

typedef struct _WDI_PHY_STATISTICS_PARAMETERS
{
    WDI_PHY_TYPE PhyType;
    UINT64 TransmittedFrameCount;
    UINT64 GroupTransmittedFrameCount;
    UINT64 FailedCount;
    UINT64 RetryCount;
    UINT64 MultipleRetryCount;
    UINT64 MaxTXLifetimeExceededCount;
    UINT64 TransmittedFragmentCount;
    UINT64 RTSSuccessCount;
    UINT64 RTSFailureCount;
    UINT64 ACKFailureCount;
    UINT64 ReceivedFrameCount;
    UINT64 GroupReceivedFrameCount;
    UINT64 PromiscuousReceivedFrameCount;
    UINT64 MaxRXLifetimeExceededCount;
    UINT64 FrameDuplicateCount;
    UINT64 ReceivedFragmentCount;
    UINT64 PromiscuousReceivedFragmentCount;
    UINT64 FCSErrorCount;
#ifdef __cplusplus
    _WDI_PHY_STATISTICS_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_PHY_STATISTICS_PARAMETERS ) );
    };
#endif
} WDI_PHY_STATISTICS_PARAMETERS, *PWDI_PHY_STATISTICS_PARAMETERS;

typedef struct _WDI_P2P_INVITATION_REQUEST_PARAMETERS
{
    UINT16 GOConfigTimeout;
    UINT16 ClientConfigTimeout;
    UINT8 InvitationFlags;
    BOOLEAN IsLocalGO;
#ifdef __cplusplus
    _WDI_P2P_INVITATION_REQUEST_PARAMETERS() : GOConfigTimeout( 0 ), ClientConfigTimeout( 0 ), InvitationFlags( 0 ), IsLocalGO( FALSE )
    {
    };
#endif
} WDI_P2P_INVITATION_REQUEST_PARAMETERS, *PWDI_P2P_INVITATION_REQUEST_PARAMETERS;

typedef struct _WDI_LINK_QUALITY_BAR_MAP_PARAMETERS
{
    UINT8 StartRange;
    UINT8 EndRange;
    UINT8 BarNumber;
#ifdef __cplusplus
    _WDI_LINK_QUALITY_BAR_MAP_PARAMETERS() : StartRange( 0 ), EndRange( 0 ), BarNumber( 0 )
    {
    };
#endif
} WDI_LINK_QUALITY_BAR_MAP_PARAMETERS, *PWDI_LINK_QUALITY_BAR_MAP_PARAMETERS;

typedef struct _WDI_LOW_LATENCY_CONNECTION_QUALITY_PARAMETERS
{
    UINT8 MaximumOffChannelOperationTime;
    UINT8 RoamingNeededLinkQualityThreshold;
#ifdef __cplusplus
    _WDI_LOW_LATENCY_CONNECTION_QUALITY_PARAMETERS() : MaximumOffChannelOperationTime( 0 ), RoamingNeededLinkQualityThreshold( 0 )
    {
    };
#endif
} WDI_LOW_LATENCY_CONNECTION_QUALITY_PARAMETERS, *PWDI_LOW_LATENCY_CONNECTION_QUALITY_PARAMETERS;

typedef struct _WDI_GET_AUTO_POWER_SAVE_STRUCT
{
    UINT8 EnableAutoPSM;
    UINT8 bReserved;
    UINT16 wReserved;
    UINT16 BeaconInterval;
    UINT8 ListenInterval;
    UINT8 ListenIntervalInDx;
    WDI_POWER_SAVE_LEVEL PowerMode;
    WDI_POWER_SAVE_LEVEL PowerModeinDx;
    WDI_POWER_MODE_REASON_CODE Reason;
    UINT64 MillisecondsSinceStart;
    UINT64 MillisecondsInPowerSave;
    UINT64 ReceivedMulticastPackets;
    UINT64 SentMulticastPackets;
    UINT64 ReceivedUnicastPackets;
    UINT64 SentUnicastPacket;
#ifdef __cplusplus
    _WDI_GET_AUTO_POWER_SAVE_STRUCT() : EnableAutoPSM( 0 ), bReserved( 0 ), wReserved( 0 ), BeaconInterval( 0 ), ListenInterval( 0 ), ListenIntervalInDx( 0 ), PowerMode( (WDI_POWER_SAVE_LEVEL)0 ), PowerModeinDx( (WDI_POWER_SAVE_LEVEL)0 ), Reason( (WDI_POWER_MODE_REASON_CODE)0 ), MillisecondsSinceStart( 0 ), MillisecondsInPowerSave( 0 ), ReceivedMulticastPackets( 0 ), SentMulticastPackets( 0 ), ReceivedUnicastPackets( 0 ), SentUnicastPacket( 0 )
    {
    };
#endif
} WDI_GET_AUTO_POWER_SAVE_STRUCT, *PWDI_GET_AUTO_POWER_SAVE_STRUCT;

typedef struct _WDI_P2P_PROVISION_DISCOVERY_REQUEST_PARAMETERS
{
    UINT8 GroupCapability;
    UINT8 OSSetGroupCapabilityBitmask;
#ifdef __cplusplus
    _WDI_P2P_PROVISION_DISCOVERY_REQUEST_PARAMETERS() : GroupCapability( 0 ), OSSetGroupCapabilityBitmask( 0 )
    {
    };
#endif
} WDI_P2P_PROVISION_DISCOVERY_REQUEST_PARAMETERS, *PWDI_P2P_PROVISION_DISCOVERY_REQUEST_PARAMETERS;

typedef struct _WDI_P2P_PROVISION_DISCOVERY_RESPONSE_PARAMETERS
{
    UINT8 GroupCapability;
    UINT8 OSSetGroupCapabilityBitmask;
#ifdef __cplusplus
    _WDI_P2P_PROVISION_DISCOVERY_RESPONSE_PARAMETERS() : GroupCapability( 0 ), OSSetGroupCapabilityBitmask( 0 )
    {
    };
#endif
} WDI_P2P_PROVISION_DISCOVERY_RESPONSE_PARAMETERS, *PWDI_P2P_PROVISION_DISCOVERY_RESPONSE_PARAMETERS;

typedef struct _WDI_P2P_PROVISION_SERVICE_ATTRIBUTES
{
    UINT8 StatusCode;
    WDI_MAC_ADDRESS IntendedInterfaceAddress;
    UINT8 ConnectionCapability;
    UINT32 FeatureCapability;
    UINT32 AdvertisementID;
    WDI_MAC_ADDRESS ServiceAddress;
    UINT32 SessionID;
    WDI_MAC_ADDRESS SessionAddress;
    UINT16 GOConfigTimeout;
    UINT16 ClientConfigTimeout;
    UINT8 UsePersistentGroup;
    UINT8 FollowOnProvisionDiscovery;
#ifdef __cplusplus
    _WDI_P2P_PROVISION_SERVICE_ATTRIBUTES()
    {
        memset( this, 0, sizeof( _WDI_P2P_PROVISION_SERVICE_ATTRIBUTES ) );
    };
#endif
} WDI_P2P_PROVISION_SERVICE_ATTRIBUTES, *PWDI_P2P_PROVISION_SERVICE_ATTRIBUTES;

typedef struct _WDI_COMMUNICATION_CAPABILITIES
{
    UINT32 MaxCommandSize;
#ifdef __cplusplus
    _WDI_COMMUNICATION_CAPABILITIES() : MaxCommandSize( 0 )
    {
    };
#endif
} WDI_COMMUNICATION_CAPABILITIES, *PWDI_COMMUNICATION_CAPABILITIES;

typedef struct _WDI_INTERFACE_CAPABILITIES
{
    UINT32 MTUSize;
    UINT32 MaxMultiCastListSize;
    UINT16 BackFillSize;
    WDI_MAC_ADDRESS Address;
    UINT32 MaxTxRate;
    UINT32 MaxRxRate;
    BOOLEAN HardwareRadioState;
    BOOLEAN SoftwareRadioState;
    BOOLEAN SupportPLR;
    BOOLEAN SupportFLR;
    BOOLEAN ActionFramesSupported;
    UINT8 NumRxStreams;
    UINT8 NumTxStreams;
    UINT8 NumChannels;
    BOOLEAN AntennaDiversity;
    BOOLEAN Support_eCSA;
    BOOLEAN MACAddressRandomization;
    WDI_MAC_ADDRESS MACAddressRandomizationMask;
    WDI_BLUETOOTH_COEXISTENCE_SUPPORT BluetoothCoexistenceSupport;
    BOOLEAN SupportsNonWdiOidRequests;
    BOOLEAN FastTransitionSupported;
    BOOLEAN MU_MIMOSupported;
    BOOLEAN CannotSupportMiracastSink;
    BOOLEAN BSSTransitionSupported;
    BOOLEAN IPDockingSupported;
    BOOLEAN SAEAuthenticationSupported;
    BOOLEAN MBOSupported;
    BOOLEAN BeaconReportsImplemented;
#ifdef __cplusplus
    _WDI_INTERFACE_CAPABILITIES()
    {
        memset( this, 0, sizeof( _WDI_INTERFACE_CAPABILITIES ) );
    };
#endif
} WDI_INTERFACE_CAPABILITIES, *PWDI_INTERFACE_CAPABILITIES;

typedef struct _WDI_INTERFACE_CAPABILITIESV1_0_20
{
    UINT32 MTUSize;
    UINT32 MaxMultiCastListSize;
    UINT16 BackFillSize;
    WDI_MAC_ADDRESS Address;
    UINT32 MaxTxRate;
    UINT32 MaxRxRate;
    BOOLEAN HardwareRadioState;
    BOOLEAN SoftwareRadioState;
    BOOLEAN SupportPLR;
    BOOLEAN SupportFLR;
    BOOLEAN ActionFramesSupported;
    UINT8 NumRxStreams;
    UINT8 NumTxStreams;
    UINT8 NumChannels;
    BOOLEAN AntennaDiversity;
    BOOLEAN Support_eCSA;
    BOOLEAN MACAddressRandomization;
    WDI_MAC_ADDRESS MACAddressRandomizationMask;
    WDI_BLUETOOTH_COEXISTENCE_SUPPORT BluetoothCoexistenceSupport;
    BOOLEAN SupportsNonWdiOidRequests;
    BOOLEAN FastTransitionSupported;
    BOOLEAN MU_MIMOSupported;
    BOOLEAN CannotSupportMiracastSink;
    BOOLEAN BSSTransitionSupported;
    BOOLEAN IPDockingSupported;
#ifdef __cplusplus
    _WDI_INTERFACE_CAPABILITIESV1_0_20()
    {
        memset( this, 0, sizeof( _WDI_INTERFACE_CAPABILITIESV1_0_20 ) );
    };
#endif
} WDI_INTERFACE_CAPABILITIESV1_0_20, *PWDI_INTERFACE_CAPABILITIESV1_0_20;

typedef struct _WDI_INTERFACE_CAPABILITIESV1_0_1
{
    UINT32 MTUSize;
    UINT32 MaxMultiCastListSize;
    UINT16 BackFillSize;
    WDI_MAC_ADDRESS Address;
    UINT32 MaxTxRate;
    UINT32 MaxRxRate;
    BOOLEAN HardwareRadioState;
    BOOLEAN SoftwareRadioState;
    BOOLEAN SupportPLR;
    BOOLEAN SupportFLR;
    BOOLEAN ActionFramesSupported;
    UINT8 NumRxStreams;
    UINT8 NumTxStreams;
    UINT8 NumChannels;
    BOOLEAN AntennaDiversity;
    BOOLEAN Support_eCSA;
    BOOLEAN MACAddressRandomization;
    WDI_MAC_ADDRESS MACAddressRandomizationMask;
    WDI_BLUETOOTH_COEXISTENCE_SUPPORT BluetoothCoexistenceSupport;
    BOOLEAN SupportsNonWdiOidRequests;
    BOOLEAN FastTransitionSupported;
    BOOLEAN MU_MIMOSupported;
    BOOLEAN CannotSupportMiracastSink;
    BOOLEAN BSSTransitionSupported;
#ifdef __cplusplus
    _WDI_INTERFACE_CAPABILITIESV1_0_1()
    {
        memset( this, 0, sizeof( _WDI_INTERFACE_CAPABILITIESV1_0_1 ) );
    };
#endif
} WDI_INTERFACE_CAPABILITIESV1_0_1, *PWDI_INTERFACE_CAPABILITIESV1_0_1;

typedef struct _WDI_INTERFACE_CAPABILITIESV1_0
{
    UINT32 MTUSize;
    UINT32 MaxMultiCastListSize;
    UINT16 BackFillSize;
    WDI_MAC_ADDRESS Address;
    UINT32 MaxTxRate;
    UINT32 MaxRxRate;
    BOOLEAN HardwareRadioState;
    BOOLEAN SoftwareRadioState;
    BOOLEAN SupportPLR;
    BOOLEAN SupportFLR;
    BOOLEAN ActionFramesSupported;
    UINT8 NumRxStreams;
    UINT8 NumTxStreams;
    UINT8 NumChannels;
    BOOLEAN AntennaDiversity;
    BOOLEAN Support_eCSA;
    BOOLEAN MACAddressRandomization;
    WDI_MAC_ADDRESS MACAddressRandomizationMask;
    WDI_BLUETOOTH_COEXISTENCE_SUPPORT BluetoothCoexistenceSupport;
    BOOLEAN SupportsNonWdiOidRequests;
    BOOLEAN FastTransitionSupported;
    BOOLEAN MU_MIMOSupported;
    BOOLEAN CannotSupportMiracastSink;
#ifdef __cplusplus
    _WDI_INTERFACE_CAPABILITIESV1_0()
    {
        memset( this, 0, sizeof( _WDI_INTERFACE_CAPABILITIESV1_0 ) );
    };
#endif
} WDI_INTERFACE_CAPABILITIESV1_0, *PWDI_INTERFACE_CAPABILITIESV1_0;

typedef struct _WDI_ALGO_PAIRS
{
    WDI_AUTH_ALGORITHM AuthAlgorithm;
    WDI_CIPHER_ALGORITHM CipherAlgorithm;
#ifdef __cplusplus
    _WDI_ALGO_PAIRS()
    {
        memset( this, 0, sizeof( _WDI_ALGO_PAIRS ) );
    };
#endif
} WDI_ALGO_PAIRS, *PWDI_ALGO_PAIRS;

typedef struct _WDI_STATION_CAPABILITIES
{
    UINT32 ScanSSIDListSize;
    UINT32 DesiredBSSIDListSize;
    UINT32 DesiredSSIDListSize;
    UINT32 PrivacyExemptionListSize;
    UINT32 KeyMappingTableSize;
    UINT32 DefaultKeyTableSize;
    UINT32 WEPKeyValueMaxLength;
    UINT32 MaxNumPerSTA;
    UINT8 SupportedQOSFlags;
    UINT8 HostFIPSModeImplemented;
    UINT8 MFPCapable;
    BOOLEAN AutoPowerSaveMode;
    BOOLEAN BSSListCachemanagement;
    BOOLEAN ConnectBSSSelectionOverride;
    UINT32 uMaxNetworkOffloadListSize;
    BOOLEAN HESSIDConnectionSupported;
    BOOLEAN DisconnectedStandbySupported;
    BOOLEAN FTMAsInitiatorSupport;
    UINT32 FTMNumberOfSupportedTargets;
    BOOLEAN HostWPA3FIPSModeEnabled;
#ifdef __cplusplus
    _WDI_STATION_CAPABILITIES() : ScanSSIDListSize( 0 ), DesiredBSSIDListSize( 0 ), DesiredSSIDListSize( 0 ), PrivacyExemptionListSize( 0 ), KeyMappingTableSize( 0 ), DefaultKeyTableSize( 0 ), WEPKeyValueMaxLength( 0 ), MaxNumPerSTA( 0 ), SupportedQOSFlags( 0 ), HostFIPSModeImplemented( 0 ), MFPCapable( 0 ), AutoPowerSaveMode( FALSE ), BSSListCachemanagement( FALSE ), ConnectBSSSelectionOverride( FALSE ), uMaxNetworkOffloadListSize( 0 ), HESSIDConnectionSupported( FALSE ), DisconnectedStandbySupported( FALSE ), FTMAsInitiatorSupport( FALSE ), FTMNumberOfSupportedTargets( 0 ), HostWPA3FIPSModeEnabled( FALSE )
    {
    };
#endif
} WDI_STATION_CAPABILITIES, *PWDI_STATION_CAPABILITIES;

typedef struct _WDI_STATION_CAPABILITIESV1_1_8
{
    UINT32 ScanSSIDListSize;
    UINT32 DesiredBSSIDListSize;
    UINT32 DesiredSSIDListSize;
    UINT32 PrivacyExemptionListSize;
    UINT32 KeyMappingTableSize;
    UINT32 DefaultKeyTableSize;
    UINT32 WEPKeyValueMaxLength;
    UINT32 MaxNumPerSTA;
    UINT8 SupportedQOSFlags;
    UINT8 HostFIPSModeImplemented;
    UINT8 MFPCapable;
    BOOLEAN AutoPowerSaveMode;
    BOOLEAN BSSListCachemanagement;
    BOOLEAN ConnectBSSSelectionOverride;
    UINT32 uMaxNetworkOffloadListSize;
    BOOLEAN HESSIDConnectionSupported;
    BOOLEAN DisconnectedStandbySupported;
    BOOLEAN FTMAsInitiatorSupport;
    UINT32 FTMNumberOfSupportedTargets;
#ifdef __cplusplus
    _WDI_STATION_CAPABILITIESV1_1_8() : ScanSSIDListSize( 0 ), DesiredBSSIDListSize( 0 ), DesiredSSIDListSize( 0 ), PrivacyExemptionListSize( 0 ), KeyMappingTableSize( 0 ), DefaultKeyTableSize( 0 ), WEPKeyValueMaxLength( 0 ), MaxNumPerSTA( 0 ), SupportedQOSFlags( 0 ), HostFIPSModeImplemented( 0 ), MFPCapable( 0 ), AutoPowerSaveMode( FALSE ), BSSListCachemanagement( FALSE ), ConnectBSSSelectionOverride( FALSE ), uMaxNetworkOffloadListSize( 0 ), HESSIDConnectionSupported( FALSE ), DisconnectedStandbySupported( FALSE ), FTMAsInitiatorSupport( FALSE ), FTMNumberOfSupportedTargets( 0 )
    {
    };
#endif
} WDI_STATION_CAPABILITIESV1_1_8, *PWDI_STATION_CAPABILITIESV1_1_8;

typedef struct _WDI_STATION_CAPABILITIESV1_0
{
    UINT32 ScanSSIDListSize;
    UINT32 DesiredBSSIDListSize;
    UINT32 DesiredSSIDListSize;
    UINT32 PrivacyExemptionListSize;
    UINT32 KeyMappingTableSize;
    UINT32 DefaultKeyTableSize;
    UINT32 WEPKeyValueMaxLength;
    UINT32 MaxNumPerSTA;
    UINT8 SupportedQOSFlags;
    UINT8 HostFIPSModeImplemented;
    UINT8 MFPCapable;
    BOOLEAN AutoPowerSaveMode;
    BOOLEAN BSSListCachemanagement;
    BOOLEAN ConnectBSSSelectionOverride;
    UINT32 uMaxNetworkOffloadListSize;
    BOOLEAN HESSIDConnectionSupported;
    BOOLEAN DisconnectedStandbySupported;
#ifdef __cplusplus
    _WDI_STATION_CAPABILITIESV1_0() : ScanSSIDListSize( 0 ), DesiredBSSIDListSize( 0 ), DesiredSSIDListSize( 0 ), PrivacyExemptionListSize( 0 ), KeyMappingTableSize( 0 ), DefaultKeyTableSize( 0 ), WEPKeyValueMaxLength( 0 ), MaxNumPerSTA( 0 ), SupportedQOSFlags( 0 ), HostFIPSModeImplemented( 0 ), MFPCapable( 0 ), AutoPowerSaveMode( FALSE ), BSSListCachemanagement( FALSE ), ConnectBSSSelectionOverride( FALSE ), uMaxNetworkOffloadListSize( 0 ), HESSIDConnectionSupported( FALSE ), DisconnectedStandbySupported( FALSE )
    {
    };
#endif
} WDI_STATION_CAPABILITIESV1_0, *PWDI_STATION_CAPABILITIESV1_0;

typedef struct _WDI_COUNTRY_REGION_LIST
{
    UINT8 CountryOrRegionString[3];
#ifdef __cplusplus
    _WDI_COUNTRY_REGION_LIST()
    {
        memset( this, 0, sizeof( _WDI_COUNTRY_REGION_LIST ) );
    };
#endif
} WDI_COUNTRY_REGION_LIST, *PWDI_COUNTRY_REGION_LIST;

typedef struct _WDI_AP_CAPABILITIES
{
    UINT32 ScanSSIDListSize;
    UINT32 DesiredSSIDListSize;
    UINT32 PrivacyExemptionListSize;
    UINT32 AssociationTableSize;
    UINT32 KeyMappingTableSize;
    UINT32 DefaultKeyTableSize;
    UINT32 WEPKeyValueMaxLength;
    BOOLEAN RadarDetectionSupported;
#ifdef __cplusplus
    _WDI_AP_CAPABILITIES() : ScanSSIDListSize( 0 ), DesiredSSIDListSize( 0 ), PrivacyExemptionListSize( 0 ), AssociationTableSize( 0 ), KeyMappingTableSize( 0 ), DefaultKeyTableSize( 0 ), WEPKeyValueMaxLength( 0 ), RadarDetectionSupported( FALSE )
    {
    };
#endif
} WDI_AP_CAPABILITIES, *PWDI_AP_CAPABILITIES;

typedef struct _WDI_VIRTUALIZATION_CAPABILITIES
{
    UINT8 ExtSTACount;
    UINT8 WiFiDirectGroupCount;
    UINT8 APCount;
    UINT8 SimultaneousGoCount;
    UINT8 SimultaneousMultiChannelOperationLimit;
    UINT8 SimultaneousSTAWFDClientCount;
#ifdef __cplusplus
    _WDI_VIRTUALIZATION_CAPABILITIES() : ExtSTACount( 0 ), WiFiDirectGroupCount( 0 ), APCount( 0 ), SimultaneousGoCount( 0 ), SimultaneousMultiChannelOperationLimit( 0 ), SimultaneousSTAWFDClientCount( 0 )
    {
    };
#endif
} WDI_VIRTUALIZATION_CAPABILITIES, *PWDI_VIRTUALIZATION_CAPABILITIES;

typedef struct _WDI_P2P_CAPABILITIES
{
    UINT8 ConcurrentGOCount;
    UINT8 ConcurrentClientCount;
    UINT32 WPSVersionSupport;
    BOOLEAN ServiceDiscoverySupported;
    BOOLEAN ServiceNameDiscoverySupported;
    BOOLEAN ServiceInformationDiscoverySupported;
    UINT32 ServiceNameAdvertisementsMaxBytesSupported;
    UINT32 ServiceInformationAdvertisementsMaxBytesSupported;
    BOOLEAN BackgroundDiscoverySupported;
    BOOLEAN ClientDiscoverabilitySupported;
    BOOLEAN InfrastructureManagementSupported;
    UINT8 MaxSecondaryAdapterTypeListSize;
    UINT8 DeviceAddress[6];
    UINT32 DiscoveryFilterListSize;
    UINT8 GOClientTableSize;
    UINT32 MaxVendorSpecificExtensionIESize;
    BOOLEAN PassiveAvailabilityListenStateSupported;
    BOOLEAN OperatingChannelUpdatesSupported;
    BOOLEAN GOon5GHzBandSupported;
    BOOLEAN ASP20ServiceNameDiscoverySupported;
    BOOLEAN ASP20ServiceInformationDiscoverySupported;
#ifdef __cplusplus
    _WDI_P2P_CAPABILITIES()
    {
        memset( this, 0, sizeof( _WDI_P2P_CAPABILITIES ) );
    };
#endif
} WDI_P2P_CAPABILITIES, *PWDI_P2P_CAPABILITIES;

typedef struct _WDI_P2P_CAPABILITIES_V1_0_10
{
    UINT8 ConcurrentGOCount;
    UINT8 ConcurrentClientCount;
    UINT32 WPSVersionSupport;
    BOOLEAN ServiceDiscoverySupported;
    BOOLEAN ServiceNameDiscoverySupported;
    BOOLEAN ServiceInformationDiscoverySupported;
    UINT32 ServiceNameAdvertisementsMaxBytesSupported;
    UINT32 ServiceInformationAdvertisementsMaxBytesSupported;
    BOOLEAN BackgroundDiscoverySupported;
    BOOLEAN ClientDiscoverabilitySupported;
    BOOLEAN InfrastructureManagementSupported;
    UINT8 MaxSecondaryAdapterTypeListSize;
    UINT8 DeviceAddress[6];
    UINT32 DiscoveryFilterListSize;
    UINT8 GOClientTableSize;
    UINT32 MaxVendorSpecificExtensionIESize;
    BOOLEAN PassiveAvailabilityListenStateSupported;
    BOOLEAN OperatingChannelUpdatesSupported;
    BOOLEAN GOon5GHzBandSupported;
#ifdef __cplusplus
    _WDI_P2P_CAPABILITIES_V1_0_10()
    {
        memset( this, 0, sizeof( _WDI_P2P_CAPABILITIES_V1_0_10 ) );
    };
#endif
} WDI_P2P_CAPABILITIES_V1_0_10, *PWDI_P2P_CAPABILITIES_V1_0_10;

typedef struct _WDI_P2P_CAPABILITIES_V1_0_1
{
    UINT8 ConcurrentGOCount;
    UINT8 ConcurrentClientCount;
    UINT32 WPSVersionSupport;
    BOOLEAN ServiceDiscoverySupported;
    BOOLEAN ServiceNameDiscoverySupported;
    BOOLEAN ServiceInformationDiscoverySupported;
    UINT32 ServiceNameAdvertisementsMaxBytesSupported;
    UINT32 ServiceInformationAdvertisementsMaxBytesSupported;
    BOOLEAN BackgroundDiscoverySupported;
    BOOLEAN ClientDiscoverabilitySupported;
    BOOLEAN InfrastructureManagementSupported;
    UINT8 MaxSecondaryAdapterTypeListSize;
    UINT8 DeviceAddress[6];
    UINT32 DiscoveryFilterListSize;
    UINT8 GOClientTableSize;
    UINT32 MaxVendorSpecificExtensionIESize;
    BOOLEAN PassiveAvailabilityListenStateSupported;
    BOOLEAN OperatingChannelUpdatesSupported;
#ifdef __cplusplus
    _WDI_P2P_CAPABILITIES_V1_0_1()
    {
        memset( this, 0, sizeof( _WDI_P2P_CAPABILITIES_V1_0_1 ) );
    };
#endif
} WDI_P2P_CAPABILITIES_V1_0_1, *PWDI_P2P_CAPABILITIES_V1_0_1;

typedef struct _WDI_DATAPATH_CAPABILITIES
{
    UINT32 InterconnectType;
    UINT8 MaxNumPeers;
    UINT8 TxTargetPriorityQueueing;
    UINT16 TxMaxScatterGatherElementsPerFrame;
    UINT8 TxExplicitSendCompleteFlagRequired;
    UINT16 TxMinEffectiveFrameSize;
    UINT16 TxFrameSizeGranularity;
    UINT8 RxTxForwarding;
    UINT32 RxMaxThroughput;
#ifdef __cplusplus
    _WDI_DATAPATH_CAPABILITIES() : InterconnectType( 0 ), MaxNumPeers( 0 ), TxTargetPriorityQueueing( 0 ), TxMaxScatterGatherElementsPerFrame( 0 ), TxExplicitSendCompleteFlagRequired( 0 ), TxMinEffectiveFrameSize( 0 ), TxFrameSizeGranularity( 0 ), RxTxForwarding( 0 ), RxMaxThroughput( 0 )
    {
    };
#endif
} WDI_DATAPATH_CAPABILITIES, *PWDI_DATAPATH_CAPABILITIES;

typedef struct _WDI_BAND_CAPABILITIES
{
    WDI_BAND_ID BandID;
    BOOLEAN BandState;
#ifdef __cplusplus
    _WDI_BAND_CAPABILITIES()
    {
        memset( this, 0, sizeof( _WDI_BAND_CAPABILITIES ) );
    };
#endif
} WDI_BAND_CAPABILITIES, *PWDI_BAND_CAPABILITIES;

typedef struct _WDI_PHY_CAPABILITIES
{
    WDI_PHY_TYPE PhyType;
    UINT8 SupportsCFPoll;
    UINT32 MPDUMaxLength;
    UINT32 TemperatureClass;
    UINT32 DiversitySupport;
#ifdef __cplusplus
    _WDI_PHY_CAPABILITIES()
    {
        memset( this, 0, sizeof( _WDI_PHY_CAPABILITIES ) );
    };
#endif
} WDI_PHY_CAPABILITIES, *PWDI_PHY_CAPABILITIES;

typedef struct _WDI_PM_CAPABILITIES
{
    UINT32 Flags;
    UINT32 SupportedWoLPacketPatterns;
    UINT32 NumTotalWoLPatterns;
    UINT32 MaxWoLPatternSize;
    UINT32 MaxWoLPatternOffset;
    UINT32 MaxWoLPacketSaveBuffer;
    UINT32 SupportedProtocolOffloads;
    UINT32 NumArpOffloadIPv4Addresses;
    UINT32 NumNSOffloadIPv6Addresses;
    NDIS_DEVICE_POWER_STATE MinMagicPacketWakeUp;
    NDIS_DEVICE_POWER_STATE MinPatternWakeUp;
    NDIS_DEVICE_POWER_STATE MinLinkChangeWakeUp;
    UINT32 SupportedWakeUpEvents;
    UINT32 MediaSpecificWakeUpEvents;
#ifdef __cplusplus
    _WDI_PM_CAPABILITIES() : Flags( 0 ), SupportedWoLPacketPatterns( 0 ), NumTotalWoLPatterns( 0 ), MaxWoLPatternSize( 0 ), MaxWoLPatternOffset( 0 ), MaxWoLPacketSaveBuffer( 0 ), SupportedProtocolOffloads( 0 ), NumArpOffloadIPv4Addresses( 0 ), NumNSOffloadIPv6Addresses( 0 ), MinMagicPacketWakeUp( (NDIS_DEVICE_POWER_STATE)0 ), MinPatternWakeUp( (NDIS_DEVICE_POWER_STATE)0 ), MinLinkChangeWakeUp( (NDIS_DEVICE_POWER_STATE)0 ), SupportedWakeUpEvents( 0 ), MediaSpecificWakeUpEvents( 0 )
    {
    };
#endif
} WDI_PM_CAPABILITIES, *PWDI_PM_CAPABILITIES;

typedef struct _WDI_RECEIVE_COALESCING_CAPABILITIES
{
    UINT32 EnableFilterTypes;
    UINT32 EnabledQueueTypes;
    UINT32 NumQueues;
    UINT32 SupportedQueueProperties;
    UINT32 SupportedFilterTests;
    UINT32 SupportedHeaders;
    UINT32 SupportedMacHeaderFields;
    UINT32 MaxMacHeaderFilters;
    UINT32 MaxQueueGroups;
    UINT32 MaxQueuesPerQueueGroup;
    UINT32 MinLookaheadSplitSize;
    UINT32 MaxLookaheadSplitSize;
    UINT32 SupportedARPHeaderFields;
    UINT32 SupportedIPv4HeaderFields;
    UINT32 SupportedIPv6HeaderFields;
    UINT32 SupportedUdpHeaderFields;
    UINT32 MaxFieldTestsPerPacketCoalescingFilter;
    UINT32 MaxPacketCoalescingFilters;
#ifdef __cplusplus
    _WDI_RECEIVE_COALESCING_CAPABILITIES() : EnableFilterTypes( 0 ), EnabledQueueTypes( 0 ), NumQueues( 0 ), SupportedQueueProperties( 0 ), SupportedFilterTests( 0 ), SupportedHeaders( 0 ), SupportedMacHeaderFields( 0 ), MaxMacHeaderFilters( 0 ), MaxQueueGroups( 0 ), MaxQueuesPerQueueGroup( 0 ), MinLookaheadSplitSize( 0 ), MaxLookaheadSplitSize( 0 ), SupportedARPHeaderFields( 0 ), SupportedIPv4HeaderFields( 0 ), SupportedIPv6HeaderFields( 0 ), SupportedUdpHeaderFields( 0 ), MaxFieldTestsPerPacketCoalescingFilter( 0 ), MaxPacketCoalescingFilters( 0 )
    {
    };
#endif
} WDI_RECEIVE_COALESCING_CAPABILITIES, *PWDI_RECEIVE_COALESCING_CAPABILITIES;

typedef struct _WDI_DATA_RATE_LIST
{
    UINT8 DataRateFlag;
    UINT16 DataRateValue;
#ifdef __cplusplus
    _WDI_DATA_RATE_LIST() : DataRateFlag( 0 ), DataRateValue( 0 )
    {
    };
#endif
} WDI_DATA_RATE_LIST, *PWDI_DATA_RATE_LIST;

typedef struct _WDI_CHECKSUM_OFFLOAD_V4_PARAMETERS
{
    UINT32 Encapsulation;
    UINT32 IpOptionsSupported;
    UINT32 TcpOptionsSupported;
    UINT32 TcpChecksum;
    UINT32 UdpChecksum;
    UINT32 IpChecksum;
#ifdef __cplusplus
    _WDI_CHECKSUM_OFFLOAD_V4_PARAMETERS() : Encapsulation( 0 ), IpOptionsSupported( 0 ), TcpOptionsSupported( 0 ), TcpChecksum( 0 ), UdpChecksum( 0 ), IpChecksum( 0 )
    {
    };
#endif
} WDI_CHECKSUM_OFFLOAD_V4_PARAMETERS, *PWDI_CHECKSUM_OFFLOAD_V4_PARAMETERS;

typedef struct _WDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS_STRUCT
{
    UINT8 IPv4Checksum;
    UINT8 TCPIPv4Checksum;
    UINT8 UDPIPv4Checksum;
    UINT8 TCPIPv6Checksum;
    UINT8 UDPIPv6Checksum;
    UINT8 LsoV1;
    UINT8 IPsecV1;
    UINT8 LsoV2IPv4;
    UINT8 LsoV2IPv6;
    UINT8 TcpConnectionIPv4;
    UINT8 TcpConnectionIPv6;
    UINT8 RscIPv4;
    UINT8 RscIPv6;
    UINT32 Flags;
    UINT8 IPsecV2;
    UINT8 IPsecV2IPv4;
    UINT8 EncapsulatedPacketTaskOffload;
    UINT8 EncapsulationTypes;
#ifdef __cplusplus
    _WDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS_STRUCT() : IPv4Checksum( 0 ), TCPIPv4Checksum( 0 ), UDPIPv4Checksum( 0 ), TCPIPv6Checksum( 0 ), UDPIPv6Checksum( 0 ), LsoV1( 0 ), IPsecV1( 0 ), LsoV2IPv4( 0 ), LsoV2IPv6( 0 ), TcpConnectionIPv4( 0 ), TcpConnectionIPv6( 0 ), RscIPv4( 0 ), RscIPv6( 0 ), Flags( 0 ), IPsecV2( 0 ), IPsecV2IPv4( 0 ), EncapsulatedPacketTaskOffload( 0 ), EncapsulationTypes( 0 )
    {
    };
#endif
} WDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS_STRUCT, *PWDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS_STRUCT;

typedef struct _WDI_CHECKSUM_OFFLOAD_V6_PARAMETERS
{
    UINT32 Encapsulation;
    UINT32 IpExtensionHeadersSupported;
    UINT32 TcpOptionsSupported;
    UINT32 TcpChecksum;
    UINT32 UdpChecksum;
#ifdef __cplusplus
    _WDI_CHECKSUM_OFFLOAD_V6_PARAMETERS() : Encapsulation( 0 ), IpExtensionHeadersSupported( 0 ), TcpOptionsSupported( 0 ), TcpChecksum( 0 ), UdpChecksum( 0 )
    {
    };
#endif
} WDI_CHECKSUM_OFFLOAD_V6_PARAMETERS, *PWDI_CHECKSUM_OFFLOAD_V6_PARAMETERS;

typedef struct _WDI_LSO_V1_CAPABILITIES
{
    UINT32 Encapsulation;
    UINT32 MaxOffLoadSize;
    UINT32 MinSegmentCount;
    UINT32 TcpOptions;
    UINT32 IpOptions;
#ifdef __cplusplus
    _WDI_LSO_V1_CAPABILITIES() : Encapsulation( 0 ), MaxOffLoadSize( 0 ), MinSegmentCount( 0 ), TcpOptions( 0 ), IpOptions( 0 )
    {
    };
#endif
} WDI_LSO_V1_CAPABILITIES, *PWDI_LSO_V1_CAPABILITIES;

typedef struct _WDI_IPV4_LSO_V2
{
    UINT32 EncapsulationV4;
    UINT32 MaxOffloadSizeV4;
    UINT32 MinSegmentCountV4;
#ifdef __cplusplus
    _WDI_IPV4_LSO_V2() : EncapsulationV4( 0 ), MaxOffloadSizeV4( 0 ), MinSegmentCountV4( 0 )
    {
    };
#endif
} WDI_IPV4_LSO_V2, *PWDI_IPV4_LSO_V2;

typedef struct _WDI_IPV6_LSO_V2
{
    UINT32 EncapsulationV6;
    UINT32 MaxOffloadSizeV6;
    UINT32 MinSegmentCountV6;
    UINT32 IpExtensionHeadersSupported;
    UINT32 TcpOptionsSupported;
#ifdef __cplusplus
    _WDI_IPV6_LSO_V2() : EncapsulationV6( 0 ), MaxOffloadSizeV6( 0 ), MinSegmentCountV6( 0 ), IpExtensionHeadersSupported( 0 ), TcpOptionsSupported( 0 )
    {
    };
#endif
} WDI_IPV6_LSO_V2, *PWDI_IPV6_LSO_V2;

typedef struct _WDI_RECEIVE_COALESCE_OFFLOAD_CAPABILITIES
{
    UINT8 Ipv4Enabled;
    UINT8 Ipv6Enabled;
#ifdef __cplusplus
    _WDI_RECEIVE_COALESCE_OFFLOAD_CAPABILITIES() : Ipv4Enabled( 0 ), Ipv6Enabled( 0 )
    {
    };
#endif
} WDI_RECEIVE_COALESCE_OFFLOAD_CAPABILITIES, *PWDI_RECEIVE_COALESCE_OFFLOAD_CAPABILITIES;

typedef struct _WDI_OFFLOAD_SCOPE
{
    UINT8 ChecksumOffloadSupportedOnAllPorts;
    UINT8 LsoV1OffloadSupportedOnAllPorts;
    UINT8 LsoV2OffloadSupportedOnAllPorts;
    UINT8 RscOffloadSupportedOnAllPorts;
#ifdef __cplusplus
    _WDI_OFFLOAD_SCOPE() : ChecksumOffloadSupportedOnAllPorts( 0 ), LsoV1OffloadSupportedOnAllPorts( 0 ), LsoV2OffloadSupportedOnAllPorts( 0 ), RscOffloadSupportedOnAllPorts( 0 )
    {
    };
#endif
} WDI_OFFLOAD_SCOPE, *PWDI_OFFLOAD_SCOPE;

typedef struct _WDI_P2P_SEND_ACTION_FRAME_RESPONSE_PARAMETERS
{
    WDI_P2P_ACTION_FRAME_TYPE ResponseFrameType;
    WDI_MAC_ADDRESS PeerDeviceAddress;
    UINT8 DialogToken;
    UINT32 SendTimeout;
    UINT32 PostACKDwellTime;
#ifdef __cplusplus
    _WDI_P2P_SEND_ACTION_FRAME_RESPONSE_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_P2P_SEND_ACTION_FRAME_RESPONSE_PARAMETERS ) );
    };
#endif
} WDI_P2P_SEND_ACTION_FRAME_RESPONSE_PARAMETERS, *PWDI_P2P_SEND_ACTION_FRAME_RESPONSE_PARAMETERS;

typedef struct _WDI_P2P_GO_NEGOTIATION_RESPONSE_PARAMETERS
{
    UINT8 StatusCode;
    UINT8 GOIntent;
    BOOLEAN TieBreaker;
    UINT16 GOConfigTimeout;
    UINT16 ClientConfigTimeout;
    WDI_MAC_ADDRESS IntendedInterfaceAddress;
    UINT8 GroupCapability;
    UINT8 OSSetGroupCapabilityBitmask;
#ifdef __cplusplus
    _WDI_P2P_GO_NEGOTIATION_RESPONSE_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_P2P_GO_NEGOTIATION_RESPONSE_PARAMETERS ) );
    };
#endif
} WDI_P2P_GO_NEGOTIATION_RESPONSE_PARAMETERS, *PWDI_P2P_GO_NEGOTIATION_RESPONSE_PARAMETERS;

typedef struct _WDI_P2P_GO_NEGOTIATION_CONFIRMATION_PARAMETERS
{
    UINT8 StatusCode;
    UINT8 GroupCapability;
    UINT8 OSSetGroupCapabilityBitmask;
#ifdef __cplusplus
    _WDI_P2P_GO_NEGOTIATION_CONFIRMATION_PARAMETERS() : StatusCode( 0 ), GroupCapability( 0 ), OSSetGroupCapabilityBitmask( 0 )
    {
    };
#endif
} WDI_P2P_GO_NEGOTIATION_CONFIRMATION_PARAMETERS, *PWDI_P2P_GO_NEGOTIATION_CONFIRMATION_PARAMETERS;

typedef struct _WDI_P2P_INVITATION_RESPONSE_PARAMETERS
{
    UINT8 StatusCode;
    UINT16 GOConfigTimeout;
    UINT16 ClientConfigTimeout;
#ifdef __cplusplus
    _WDI_P2P_INVITATION_RESPONSE_PARAMETERS() : StatusCode( 0 ), GOConfigTimeout( 0 ), ClientConfigTimeout( 0 )
    {
    };
#endif
} WDI_P2P_INVITATION_RESPONSE_PARAMETERS, *PWDI_P2P_INVITATION_RESPONSE_PARAMETERS;

typedef struct _WDI_P2P_INCOMING_ACTION_FRAME_PARAMETERS
{
    WDI_P2P_ACTION_FRAME_TYPE ActionFrameType;
    WDI_MAC_ADDRESS PeerDeviceAddress;
    UINT8 DialogToken;
#ifdef __cplusplus
    _WDI_P2P_INCOMING_ACTION_FRAME_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_P2P_INCOMING_ACTION_FRAME_PARAMETERS ) );
    };
#endif
} WDI_P2P_INCOMING_ACTION_FRAME_PARAMETERS, *PWDI_P2P_INCOMING_ACTION_FRAME_PARAMETERS;

typedef struct _WDI_AP_ASSOCIATION_RESPONSE_PARAMETERS
{
    BOOLEAN AcceptRequest;
    UINT16 ReasonCode;
#ifdef __cplusplus
    _WDI_AP_ASSOCIATION_RESPONSE_PARAMETERS() : AcceptRequest( FALSE ), ReasonCode( 0 )
    {
    };
#endif
} WDI_AP_ASSOCIATION_RESPONSE_PARAMETERS, *PWDI_AP_ASSOCIATION_RESPONSE_PARAMETERS;

typedef struct _WDI_AP_ASSOCIATION_REQUEST_PARAMETERS
{
    WDI_MAC_ADDRESS PeerMacAddress;
    BOOLEAN IsReassociationRequest;
#ifdef __cplusplus
    _WDI_AP_ASSOCIATION_REQUEST_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_AP_ASSOCIATION_REQUEST_PARAMETERS ) );
    };
#endif
} WDI_AP_ASSOCIATION_REQUEST_PARAMETERS, *PWDI_AP_ASSOCIATION_REQUEST_PARAMETERS;

typedef struct _WDI_DISCONNECT_PARAMETERS
{
    WDI_MAC_ADDRESS MacAddress;
    UINT16 Disassociation80211Reason;
#ifdef __cplusplus
    _WDI_DISCONNECT_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_DISCONNECT_PARAMETERS ) );
    };
#endif
} WDI_DISCONNECT_PARAMETERS, *PWDI_DISCONNECT_PARAMETERS;

typedef struct _WDI_P2P_SEND_ACTION_FRAME_RESULT_PARAMETERS
{
    WDI_MAC_ADDRESS PeerDeviceAddress;
    UINT8 DialogToken;
#ifdef __cplusplus
    _WDI_P2P_SEND_ACTION_FRAME_RESULT_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_P2P_SEND_ACTION_FRAME_RESULT_PARAMETERS ) );
    };
#endif
} WDI_P2P_SEND_ACTION_FRAME_RESULT_PARAMETERS, *PWDI_P2P_SEND_ACTION_FRAME_RESULT_PARAMETERS;

typedef struct _WDI_ASSOC_RESULT_PARAMETERS
{
    WDI_MAC_ADDRESS PeerMACAddress;
    BOOLEAN IsReassociationRequest;
    BOOLEAN IsReAssociationResponse;
    WDI_AUTH_ALGORITHM AuthAlgorithm;
    WDI_CIPHER_ALGORITHM UnicastCipherAlgorithm;
    WDI_CIPHER_ALGORITHM MulticastCipherAlgorithm;
#ifdef __cplusplus
    _WDI_ASSOC_RESULT_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_ASSOC_RESULT_PARAMETERS ) );
    };
#endif
} WDI_ASSOC_RESULT_PARAMETERS, *PWDI_ASSOC_RESULT_PARAMETERS;

typedef struct _WDI_DISCONNECT_INDICATION_PARAMETERS
{
    WDI_MAC_ADDRESS MacAddress;
    WDI_ASSOC_STATUS DisassociationWABIReason;
#ifdef __cplusplus
    _WDI_DISCONNECT_INDICATION_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_DISCONNECT_INDICATION_PARAMETERS ) );
    };
#endif
} WDI_DISCONNECT_INDICATION_PARAMETERS, *PWDI_DISCONNECT_INDICATION_PARAMETERS;

typedef struct _WDI_RADIO_STATUS
{
    BOOLEAN HardwareState;
    BOOLEAN SoftwareState;
#ifdef __cplusplus
    _WDI_RADIO_STATUS() : HardwareState( FALSE ), SoftwareState( FALSE )
    {
    };
#endif
} WDI_RADIO_STATUS, *PWDI_RADIO_STATUS;

typedef struct _WDI_CIPHER_KEY_ID_STRUCT
{
    UINT32 CipherKeyID;
#ifdef __cplusplus
    _WDI_CIPHER_KEY_ID_STRUCT() : CipherKeyID( 0 )
    {
    };
#endif
} WDI_CIPHER_KEY_ID_STRUCT, *PWDI_CIPHER_KEY_ID_STRUCT;

typedef struct _WDI_CIPHER_KEY_TYPE_STRUCT
{
    WDI_CIPHER_ALGORITHM CipherAlgorithm;
    WDI_CIPHER_KEY_DIRECTION Direction;
    UINT8 Static;
    WDI_CIPHER_KEY_TYPE KeyType;
#ifdef __cplusplus
    _WDI_CIPHER_KEY_TYPE_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_CIPHER_KEY_TYPE_STRUCT ) );
    };
#endif
} WDI_CIPHER_KEY_TYPE_STRUCT, *PWDI_CIPHER_KEY_TYPE_STRUCT;

typedef struct _WDI_RECEIVE_SEQUENCE_COUNT_STRUCT
{
    UINT8 ReceiveSequenceCount[6];
#ifdef __cplusplus
    _WDI_RECEIVE_SEQUENCE_COUNT_STRUCT()
    {
        memset( this, 0, sizeof( _WDI_RECEIVE_SEQUENCE_COUNT_STRUCT ) );
    };
#endif
} WDI_RECEIVE_SEQUENCE_COUNT_STRUCT, *PWDI_RECEIVE_SEQUENCE_COUNT_STRUCT;

typedef struct _WDI_ACTION_FRAME_REQUEST_PARAMETERS
{
    WDI_CHANNEL_NUMBER ChannelNumber;
    WDI_BAND_ID BandId;
    WDI_MAC_ADDRESS DestinationAddress;
    UINT32 SendTimeout;
    UINT32 PostACKDwellTime;
#ifdef __cplusplus
    _WDI_ACTION_FRAME_REQUEST_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_ACTION_FRAME_REQUEST_PARAMETERS ) );
    };
#endif
} WDI_ACTION_FRAME_REQUEST_PARAMETERS, *PWDI_ACTION_FRAME_REQUEST_PARAMETERS;

typedef struct _WDI_CONNECTION_SETTINGS
{
    BOOLEAN RoamRequest;
    BOOLEAN HiddenNetwork;
    BOOLEAN ExcludeUnencrypted;
    BOOLEAN MFPEnabled;
    BOOLEAN HostFIPSModeEnabled;
    UINT32 RoamNeededReason;
    WDI_ROAM_TRIGGER RoamTrigger;
    BOOLEAN BSSTransitionSupported;
    BOOLEAN MloConnectionSupported;
    BOOLEAN IsFIPSConnection;
    BOOLEAN MSCSSupported;
    BOOLEAN DSCPToUPMappingSupported;
#ifdef __cplusplus
    _WDI_CONNECTION_SETTINGS()
    {
        memset( this, 0, sizeof( _WDI_CONNECTION_SETTINGS ) );
    };
#endif
} WDI_CONNECTION_SETTINGS, *PWDI_CONNECTION_SETTINGS;

typedef struct _WDI_CONNECTION_SETTINGSV2_0_8
{
    BOOLEAN RoamRequest;
    BOOLEAN HiddenNetwork;
    BOOLEAN ExcludeUnencrypted;
    BOOLEAN MFPEnabled;
    BOOLEAN HostFIPSModeEnabled;
    UINT32 RoamNeededReason;
    WDI_ROAM_TRIGGER RoamTrigger;
    BOOLEAN BSSTransitionSupported;
    BOOLEAN MloConnectionSupported;
    BOOLEAN IsFIPSConnection;
#ifdef __cplusplus
    _WDI_CONNECTION_SETTINGSV2_0_8()
    {
        memset( this, 0, sizeof( _WDI_CONNECTION_SETTINGSV2_0_8 ) );
    };
#endif
} WDI_CONNECTION_SETTINGSV2_0_8, *PWDI_CONNECTION_SETTINGSV2_0_8;

typedef struct _WDI_CONNECTION_SETTINGSV2_0_7
{
    BOOLEAN RoamRequest;
    BOOLEAN HiddenNetwork;
    BOOLEAN ExcludeUnencrypted;
    BOOLEAN MFPEnabled;
    BOOLEAN HostFIPSModeEnabled;
    UINT32 RoamNeededReason;
    WDI_ROAM_TRIGGER RoamTrigger;
    BOOLEAN BSSTransitionSupported;
    BOOLEAN MloConnectionSupported;
#ifdef __cplusplus
    _WDI_CONNECTION_SETTINGSV2_0_7()
    {
        memset( this, 0, sizeof( _WDI_CONNECTION_SETTINGSV2_0_7 ) );
    };
#endif
} WDI_CONNECTION_SETTINGSV2_0_7, *PWDI_CONNECTION_SETTINGSV2_0_7;

typedef struct _WDI_CONNECTION_SETTINGSV1_0_1
{
    BOOLEAN RoamRequest;
    BOOLEAN HiddenNetwork;
    BOOLEAN ExcludeUnencrypted;
    BOOLEAN MFPEnabled;
    BOOLEAN HostFIPSModeEnabled;
    UINT32 RoamNeededReason;
    WDI_ROAM_TRIGGER RoamTrigger;
    BOOLEAN BSSTransitionSupported;
#ifdef __cplusplus
    _WDI_CONNECTION_SETTINGSV1_0_1()
    {
        memset( this, 0, sizeof( _WDI_CONNECTION_SETTINGSV1_0_1 ) );
    };
#endif
} WDI_CONNECTION_SETTINGSV1_0_1, *PWDI_CONNECTION_SETTINGSV1_0_1;

typedef struct _WDI_CONNECTION_SETTINGSV1_0
{
    BOOLEAN RoamRequest;
    BOOLEAN HiddenNetwork;
    BOOLEAN ExcludeUnencrypted;
    BOOLEAN MFPEnabled;
    BOOLEAN HostFIPSModeEnabled;
    UINT32 RoamNeededReason;
    WDI_ROAM_TRIGGER RoamTrigger;
#ifdef __cplusplus
    _WDI_CONNECTION_SETTINGSV1_0()
    {
        memset( this, 0, sizeof( _WDI_CONNECTION_SETTINGSV1_0 ) );
    };
#endif
} WDI_CONNECTION_SETTINGSV1_0, *PWDI_CONNECTION_SETTINGSV1_0;

typedef struct _WDI_BSS_SELECTION_PARAMETERS_CONTAINER
{
    UINT32 SelectionFlags;
#ifdef __cplusplus
    _WDI_BSS_SELECTION_PARAMETERS_CONTAINER() : SelectionFlags( 0 )
    {
    };
#endif
} WDI_BSS_SELECTION_PARAMETERS_CONTAINER, *PWDI_BSS_SELECTION_PARAMETERS_CONTAINER;

typedef struct _WDI_ASSOCIATION_RESULT_PARAMETERS
{
    WDI_ASSOC_STATUS AssociationStatus;
    UINT32 StatusCode;
    BOOLEAN ReAssociation;
    WDI_AUTH_ALGORITHM AuthAlgorithm;
    WDI_CIPHER_ALGORITHM UnicastCipherAlgorithm;
    WDI_CIPHER_ALGORITHM MulticastDataCipherAlgorithm;
    WDI_CIPHER_ALGORITHM MulticastMgmtCipherAlgorithm;
    BOOLEAN FourAddressSupported;
    BOOLEAN PortAuthorized;
    BOOLEAN WMMQoSEnabled;
    WDI_DS_INFO DSInfo;
    UINT32 AssociationComebackTime;
    WDI_BAND_ID BandID;
    UINT32 IHVAssociationStatus;
    WDI_DISABLE_DATA_PATH_OFFLOADS_SCENARIO DisableDataPathOffloadsScenario;
#ifdef __cplusplus
    _WDI_ASSOCIATION_RESULT_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_ASSOCIATION_RESULT_PARAMETERS ) );
    };
#endif
} WDI_ASSOCIATION_RESULT_PARAMETERS, *PWDI_ASSOCIATION_RESULT_PARAMETERS;

typedef struct _WDI_ASSOCIATION_RESULT_PARAMETERSV1_1_10
{
    WDI_ASSOC_STATUS AssociationStatus;
    UINT32 StatusCode;
    BOOLEAN ReAssociation;
    WDI_AUTH_ALGORITHM AuthAlgorithm;
    WDI_CIPHER_ALGORITHM UnicastCipherAlgorithm;
    WDI_CIPHER_ALGORITHM MulticastDataCipherAlgorithm;
    WDI_CIPHER_ALGORITHM MulticastMgmtCipherAlgorithm;
    BOOLEAN FourAddressSupported;
    BOOLEAN PortAuthorized;
    BOOLEAN WMMQoSEnabled;
    WDI_DS_INFO DSInfo;
    UINT32 AssociationComebackTime;
    WDI_BAND_ID BandID;
    UINT32 IHVAssociationStatus;
#ifdef __cplusplus
    _WDI_ASSOCIATION_RESULT_PARAMETERSV1_1_10()
    {
        memset( this, 0, sizeof( _WDI_ASSOCIATION_RESULT_PARAMETERSV1_1_10 ) );
    };
#endif
} WDI_ASSOCIATION_RESULT_PARAMETERSV1_1_10, *PWDI_ASSOCIATION_RESULT_PARAMETERSV1_1_10;

typedef struct _WDI_TKIP_MIC_FAILURE_PARAMETERS
{
    BOOLEAN DefaultKeyFailure;
    UINT32 KeyIndex;
    WDI_MAC_ADDRESS PeerMacAddress;
#ifdef __cplusplus
    _WDI_TKIP_MIC_FAILURE_PARAMETERS()
    {
        memset( this, 0, sizeof( _WDI_TKIP_MIC_FAILURE_PARAMETERS ) );
    };
#endif
} WDI_TKIP_MIC_FAILURE_PARAMETERS, *PWDI_TKIP_MIC_FAILURE_PARAMETERS;

typedef struct _WDI_NETWORK_LIST_OFFLOAD_CONFIG
{
    UINT32 ulFlags;
    UINT32 DelayStartInSeconds;
    UINT32 FastScanPeriodinSeconds;
    UINT32 FastScanIterations;
    UINT32 SlowScanPeriodinSeconds;
#ifdef __cplusplus
    _WDI_NETWORK_LIST_OFFLOAD_CONFIG() : ulFlags( 0 ), DelayStartInSeconds( 0 ), FastScanPeriodinSeconds( 0 ), FastScanIterations( 0 ), SlowScanPeriodinSeconds( 0 )
    {
    };
#endif
} WDI_NETWORK_LIST_OFFLOAD_CONFIG, *PWDI_NETWORK_LIST_OFFLOAD_CONFIG;

typedef struct _WDI_WAKE_EVENT_MASK
{
    UINT32 EnabledWoLPacketPatterns;
    UINT32 EnabledProtocolOffloads;
    UINT32 WakeUpFlags;
    UINT32 MediaSpecificWakeUpEvents;
#ifdef __cplusplus
    _WDI_WAKE_EVENT_MASK() : EnabledWoLPacketPatterns( 0 ), EnabledProtocolOffloads( 0 ), WakeUpFlags( 0 ), MediaSpecificWakeUpEvents( 0 )
    {
    };
#endif
} WDI_WAKE_EVENT_MASK, *PWDI_WAKE_EVENT_MASK;

typedef struct _WDI_RECEIVE_COALESCING_CONFIG
{
    UINT32 QueueId;
    UINT32 FilterId;
    UINT32 MaxCoalescingDelay;
#ifdef __cplusplus
    _WDI_RECEIVE_COALESCING_CONFIG() : QueueId( 0 ), FilterId( 0 ), MaxCoalescingDelay( 0 )
    {
    };
#endif
} WDI_RECEIVE_COALESCING_CONFIG, *PWDI_RECEIVE_COALESCING_CONFIG;

typedef struct _WDI_RECEIVE_COALESCING_FIELD
{
    UINT32 Flags;
    NDIS_FRAME_HEADER FrameHeader;
    NDIS_RECEIVE_FILTER_TEST ReceiveFilterTest;
    UINT32 HeaderField;
    WDI_BYTE16 FieldValue;
    WDI_BYTE16 ResultValue;
#ifdef __cplusplus
    _WDI_RECEIVE_COALESCING_FIELD()
    {
        memset( this, 0, sizeof( _WDI_RECEIVE_COALESCING_FIELD ) );
    };
#endif
} WDI_RECEIVE_COALESCING_FIELD, *PWDI_RECEIVE_COALESCING_FIELD;

typedef struct _WDI_IPv4_TCP_SYNC
{
    UINT32 PatternId;
    WDI_BYTE4 IPv4SourceAddr;
    WDI_BYTE4 IPv4DestAddr;
    UINT16 TcpSourcePort;
    UINT16 TcpDestPort;
#ifdef __cplusplus
    _WDI_IPv4_TCP_SYNC()
    {
        memset( this, 0, sizeof( _WDI_IPv4_TCP_SYNC ) );
    };
#endif
} WDI_IPv4_TCP_SYNC, *PWDI_IPv4_TCP_SYNC;

typedef struct _WDI_IPv6TCP_SYNC
{
    UINT32 PatternId;
    WDI_BYTE16 IPv6SourceAddr;
    WDI_BYTE16 IPv6DestAddr;
    UINT16 TcpSourcePort;
    UINT16 TcpDestPort;
#ifdef __cplusplus
    _WDI_IPv6TCP_SYNC()
    {
        memset( this, 0, sizeof( _WDI_IPv6TCP_SYNC ) );
    };
#endif
} WDI_IPv6TCP_SYNC, *PWDI_IPv6TCP_SYNC;

typedef struct _WDI_GUID
{
    GUID Guid;
#ifdef __cplusplus
    _WDI_GUID()
    {
        memset( this, 0, sizeof( _WDI_GUID ) );
    };
#endif
} WDI_GUID, *PWDI_GUID;

typedef struct _WDI_OS_POWER_MANAGEMENT_FEATURES
{
    WDI_OS_POWER_MANAGEMENT_FLAGS EnableFlags;
#ifdef __cplusplus
    _WDI_OS_POWER_MANAGEMENT_FEATURES() : EnableFlags( (WDI_OS_POWER_MANAGEMENT_FLAGS)0 )
    {
    };
#endif
} WDI_OS_POWER_MANAGEMENT_FEATURES, *PWDI_OS_POWER_MANAGEMENT_FEATURES;

struct ArrayOfElementsOfUINT8
{
    UINT32 ElementCount;
    UINT8* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<UINT8> ) == sizeof( struct ArrayOfElementsOfUINT8 ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<UINT8> WDI_SSID;
#else
typedef struct ArrayOfElementsOfUINT8 WDI_SSID;
#endif

#ifdef __cplusplus
typedef ArrayOfElements<UINT8> WDI_BYTE_BLOB;
#else
typedef struct ArrayOfElementsOfUINT8 WDI_BYTE_BLOB;
#endif

#ifdef __cplusplus
typedef ArrayOfElements<UINT8> WDI_PRIVATE_BYTE_BLOB;
#else
typedef struct ArrayOfElementsOfUINT8 WDI_PRIVATE_BYTE_BLOB;
#endif

struct ArrayOfElementsOfCHAR
{
    UINT32 ElementCount;
    CHAR* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<CHAR> ) == sizeof( struct ArrayOfElementsOfCHAR ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<CHAR> WDI_STRING_Z;
#else
typedef struct ArrayOfElementsOfCHAR WDI_STRING_Z;
#endif

typedef WDI_MAC_ADDRESS WDI_MAC_ADDRESS_CONTAINER;

struct ArrayOfElementsOfWDI_MAC_ADDRESS
{
    UINT32 ElementCount;
    WDI_MAC_ADDRESS* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_MAC_ADDRESS> ) == sizeof( struct ArrayOfElementsOfWDI_MAC_ADDRESS ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_MAC_ADDRESS> WDI_ADDRESS_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_MAC_ADDRESS WDI_ADDRESS_LIST_CONTAINER;
#endif

typedef NDIS_GUID WDI_NDIS_GUID_CONTAINER;

typedef WDI_IHV_TASK_PRIORITY WDI_IHV_TASK_PRIORITY_CONTAINER;

typedef INT32 INT32_CONTAINER;

typedef UINT32 UINT32_CONTAINER;

struct ArrayOfElementsOfUINT32
{
    UINT32 ElementCount;
    UINT32* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<UINT32> ) == sizeof( struct ArrayOfElementsOfUINT32 ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<UINT32> UINT32_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfUINT32 UINT32_LIST_CONTAINER;
#endif

typedef UINT64 UINT64_CONTAINER;

typedef WDI_OPERATION_MODE WDI_OPERATION_MODE_CONTAINER;

typedef UINT16 UINT16_CONTAINER;

struct ArrayOfElementsOfUINT16
{
    UINT32 ElementCount;
    UINT16* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<UINT16> ) == sizeof( struct ArrayOfElementsOfUINT16 ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<UINT16> UINT16_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfUINT16 UINT16_LIST_CONTAINER;
#endif

typedef UINT8 UINT8_CONTAINER;

typedef BOOLEAN BOOL_CONTAINER;

typedef WDI_ASSOC_STATUS WDI_ROAMING_NEEDED_CONTAINER;

typedef WDI_CONNECTION_QUALITY_HINT WDI_CONNECTION_QUALITY_CONTAINER;

typedef WDI_SAE_INDICATION_TYPE WDI_SAE_INDICATION_TYPE_CONTAINER;

typedef WDI_SAE_REQUEST_TYPE WDI_SAE_REQUEST_TYPE_CONTAINER;

typedef WDI_SAE_STATUS WDI_SAE_STATUS_CONTAINER;

typedef WDI_TCP_RSC_STATISTICS_STRUCT WDI_TCP_RSC_STATISTICS_CONTAINER;

typedef WDI_P2P_CHANNEL WDI_P2P_CHANNEL_CONTAINER;

typedef WDI_P2P_CHANNEL_INDICATE_REASON WDI_P2P_CHANNEL_INDICATE_REASON_CONTAINER;

typedef WDI_LOW_LATENCY_CONNECTION_QUALITY_PARAMETERS WDI_LOW_LATENCY_CONNECTION_QUALITY_PARAMETERS_CONTAINER;

typedef WDI_PMK_NAME_STRUCT WDI_TYPE_PMK_NAME_CONTAINER;

typedef NDIS_STATUS WDI_STATUS_CONTAINER;

typedef WDI_START_AP_PARAMETERS WDI_START_AP_PARAMETERS_CONTAINER;

typedef WDI_START_AP_PARAMETERS_V1_0_10 WDI_START_AP_PARAMETERS_CONTAINER_V1_0_10;

typedef WDI_START_AP_PARAMETERS_V1_0_1 WDI_START_AP_PARAMETERS_CONTAINER_V1_0_1;

typedef WDI_BSSID_INFO_STRUCT WDI_BSSID_INFO_CONTAINER;

struct ArrayOfElementsOfWDI_AUTH_ALGORITHM
{
    UINT32 ElementCount;
    WDI_AUTH_ALGORITHM* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_AUTH_ALGORITHM> ) == sizeof( struct ArrayOfElementsOfWDI_AUTH_ALGORITHM ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_AUTH_ALGORITHM> WDI_AUTH_ALGORITHM_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_AUTH_ALGORITHM WDI_AUTH_ALGORITHM_LIST_CONTAINER;
#endif

struct ArrayOfElementsOfWDI_CIPHER_ALGORITHM
{
    UINT32 ElementCount;
    WDI_CIPHER_ALGORITHM* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_CIPHER_ALGORITHM> ) == sizeof( struct ArrayOfElementsOfWDI_CIPHER_ALGORITHM ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_CIPHER_ALGORITHM> WDI_CIPHER_ALGORITHM_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_CIPHER_ALGORITHM WDI_CIPHER_ALGORITHM_LIST_CONTAINER;
#endif

struct ArrayOfElementsOfRSNA_AKM_SUITE
{
    UINT32 ElementCount;
    RSNA_AKM_SUITE* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<RSNA_AKM_SUITE> ) == sizeof( struct ArrayOfElementsOfRSNA_AKM_SUITE ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<RSNA_AKM_SUITE> RSNA_AKM_SUITE_CONTAINER;
#else
typedef struct ArrayOfElementsOfRSNA_AKM_SUITE RSNA_AKM_SUITE_CONTAINER;
#endif

typedef RSNA_AKM_SUITE RSNA_AKM_SUITE_CONTAINERV2_0_4;

struct ArrayOfElementsOfRSNA_AKM_CIPHER_PAIR
{
    UINT32 ElementCount;
    RSNA_AKM_CIPHER_PAIR* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<RSNA_AKM_CIPHER_PAIR> ) == sizeof( struct ArrayOfElementsOfRSNA_AKM_CIPHER_PAIR ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<RSNA_AKM_CIPHER_PAIR> RSNA_AKM_CIPHER_PAIR_CONTAINER;
#else
typedef struct ArrayOfElementsOfRSNA_AKM_CIPHER_PAIR RSNA_AKM_CIPHER_PAIR_CONTAINER;
#endif

typedef WDI_PHY_TYPE WDI_PHY_TYPE_CONTAINER;

typedef struct _WDI_ADDITIONAL_IES_CONTAINER
{
    struct _WDI_ADDITIONAL_IES_CONTAINER_Optional
    {
        UINT32 AdditionalBeaconIEs_IsPresent : 1;
        UINT32 AdditionalProbeResponseIEs_IsPresent : 1;
        UINT32 AdditionalProbeRequestDefaultIEs_IsPresent : 1;
#ifdef __cplusplus
        _WDI_ADDITIONAL_IES_CONTAINER_Optional() : AdditionalBeaconIEs_IsPresent( FALSE ), AdditionalProbeResponseIEs_IsPresent( FALSE ), AdditionalProbeRequestDefaultIEs_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_BYTE_BLOB AdditionalBeaconIEs;
    WDI_BYTE_BLOB AdditionalProbeResponseIEs;
    WDI_BYTE_BLOB AdditionalProbeRequestDefaultIEs;
} WDI_ADDITIONAL_IES_CONTAINER, *PWDI_ADDITIONAL_IES_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_ADDITIONAL_IES_CONTAINER * pField );
    }
}
#endif
typedef WDI_P2P_DEVICE_INFO_PARAMETERS WDI_P2P_DEVICE_INFO_PARAMETERS_CONTAINER;

typedef struct _WDI_P2P_DEVICE_INFO_CONTAINER
{
    WDI_P2P_DEVICE_INFO_PARAMETERS_CONTAINER DeviceInfoParameters;
    WDI_BYTE_BLOB DeviceName;
} WDI_P2P_DEVICE_INFO_CONTAINER, *PWDI_P2P_DEVICE_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_DEVICE_INFO_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_NEIGHBOR_REPORT_ENTRY_CONTAINER
{
    WDI_MAC_ADDRESS_CONTAINER Bssid;
    WDI_BSSID_INFO_CONTAINER BSSIDInfo;
    UINT8_CONTAINER OperatingClass;
    UINT8_CONTAINER ChannelNumber;
    WDI_PHY_TYPE_CONTAINER PhyType;
#ifdef __cplusplus
    _WDI_NEIGHBOR_REPORT_ENTRY_CONTAINER() : OperatingClass( (UINT8_CONTAINER)0 ), ChannelNumber( (UINT8_CONTAINER)0 ), PhyType( (WDI_PHY_TYPE_CONTAINER)0 )
    {
        memset( &Bssid, 0, sizeof( Bssid ) );
    };
#endif
} WDI_NEIGHBOR_REPORT_ENTRY_CONTAINER, *PWDI_NEIGHBOR_REPORT_ENTRY_CONTAINER;
typedef WDI_P2P_DEVICE_CAPABILITY_PARAMETERS WDI_P2P_DEVICE_CAPABILITY_CONTAINER;

typedef WDI_P2P_GROUP_OWNER_CAPABILITY_PARAMETERS WDI_P2P_GROUP_OWNER_CAPABILITY_CONTAINER;

struct ArrayOfElementsOfWDI_P2P_DEVICE_TYPE
{
    UINT32 ElementCount;
    WDI_P2P_DEVICE_TYPE* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_P2P_DEVICE_TYPE> ) == sizeof( struct ArrayOfElementsOfWDI_P2P_DEVICE_TYPE ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_P2P_DEVICE_TYPE> WDI_P2P_SECONDARY_DEVICE_TYPE_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_P2P_DEVICE_TYPE WDI_P2P_SECONDARY_DEVICE_TYPE_CONTAINER;
#endif

typedef WDI_P2P_SERVICE_NAME_HASH WDI_P2P_SERVICE_NAME_HASH_CONTAINER;

typedef struct _WDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER
{
    struct _WDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER_Optional
    {
        UINT32 ServiceInformation_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER_Optional() : ServiceInformation_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_BYTE_BLOB ServiceName;
    WDI_P2P_SERVICE_NAME_HASH_CONTAINER ServiceNameHash;
    WDI_BYTE_BLOB ServiceInformation;
    UINT16_CONTAINER ServiceUpdateIndicator;
    UINT8_CONTAINER ServiceTransactionId;
#ifdef __cplusplus
    _WDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER() : ServiceUpdateIndicator( (UINT16_CONTAINER)0 ), ServiceTransactionId( (UINT8_CONTAINER)0 )
    {
        memset( &ServiceNameHash, 0, sizeof( ServiceNameHash ) );
    };
#endif
} WDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER, *PWDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20
{
    struct _WDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20_Optional
    {
        UINT32 ServiceInformation_IsPresent : 1;
        UINT32 ServiceUpdateIndicator_IsPresent : 1;
        UINT32 ServiceTransactionId_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20_Optional() : ServiceInformation_IsPresent( FALSE ), ServiceUpdateIndicator_IsPresent( FALSE ), ServiceTransactionId_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_BYTE_BLOB ServiceName;
    WDI_BYTE_BLOB InstanceName;
    WDI_BYTE_BLOB ServiceInformation;
    UINT16_CONTAINER ServiceUpdateIndicator;
    UINT8_CONTAINER ServiceTransactionId;
#ifdef __cplusplus
    _WDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20() : ServiceUpdateIndicator( (UINT16_CONTAINER)0 ), ServiceTransactionId( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20, *PWDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20 * pField );
    }
}
#endif

typedef struct _WDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER
{
    struct _WDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER_Optional
    {
        UINT32 ServiceInformation_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER_Optional() : ServiceInformation_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_BYTE_BLOB ServiceName;
    WDI_BYTE_BLOB ServiceInformation;
    UINT8_CONTAINER ServiceStatus;
    UINT32_CONTAINER AdvertisementID;
    UINT16_CONTAINER ConfigurationMethods;
#ifdef __cplusplus
    _WDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER() : ServiceStatus( (UINT8_CONTAINER)0 ), AdvertisementID( (UINT32_CONTAINER)0 ), ConfigurationMethods( (UINT16_CONTAINER)0 )
    {
    };
#endif
} WDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER, *PWDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER
{
    struct _WDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER_Optional
    {
        UINT32 ServiceInformation_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER_Optional() : ServiceInformation_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_BYTE_BLOB ServiceName;
    WDI_P2P_SERVICE_NAME_HASH_CONTAINER ServiceNameHash;
    WDI_BYTE_BLOB ServiceInformation;
    UINT8_CONTAINER ServiceStatus;
    UINT32_CONTAINER AdvertisementID;
    UINT16_CONTAINER ConfigurationMethods;
#ifdef __cplusplus
    _WDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER() : ServiceStatus( (UINT8_CONTAINER)0 ), AdvertisementID( (UINT32_CONTAINER)0 ), ConfigurationMethods( (UINT16_CONTAINER)0 )
    {
        memset( &ServiceNameHash, 0, sizeof( ServiceNameHash ) );
    };
#endif
} WDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER, *PWDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20
{
    struct _WDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20_Optional
    {
        UINT32 ServiceInformation_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20_Optional() : ServiceInformation_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_BYTE_BLOB ServiceType;
    WDI_P2P_SERVICE_NAME_HASH_CONTAINER ServiceTypeHash;
    WDI_BYTE_BLOB InstanceName;
    WDI_P2P_SERVICE_NAME_HASH_CONTAINER InstanceNameHash;
    WDI_BYTE_BLOB ServiceInformation;
    UINT8_CONTAINER ServiceStatus;
    UINT32_CONTAINER AdvertisementID;
    UINT16_CONTAINER ConfigurationMethods;
#ifdef __cplusplus
    _WDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20() : ServiceStatus( (UINT8_CONTAINER)0 ), AdvertisementID( (UINT32_CONTAINER)0 ), ConfigurationMethods( (UINT16_CONTAINER)0 )
    {
        memset( &ServiceTypeHash, 0, sizeof( ServiceTypeHash ) );
        memset( &InstanceNameHash, 0, sizeof( InstanceNameHash ) );
    };
#endif
} WDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20, *PWDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20 * pField );
    }
}
#endif

typedef struct _WDI_P2P_ADVERTISED_PREFIX_ENTRY_CONTAINER
{
    WDI_BYTE_BLOB PrefixName;
    WDI_P2P_SERVICE_NAME_HASH_CONTAINER ServicePrefixHash;
#ifdef __cplusplus
    _WDI_P2P_ADVERTISED_PREFIX_ENTRY_CONTAINER()
    {
        memset( &ServicePrefixHash, 0, sizeof( ServicePrefixHash ) );
    };
#endif
} WDI_P2P_ADVERTISED_PREFIX_ENTRY_CONTAINER, *PWDI_P2P_ADVERTISED_PREFIX_ENTRY_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_ADVERTISED_PREFIX_ENTRY_CONTAINER * pField );
    }
}
#endif
struct ArrayOfElementsOfWDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER
{
    UINT32 ElementCount;
    WDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20
{
    UINT32 ElementCount;
    WDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20> ) == sizeof( struct ArrayOfElementsOfWDI_P2P_ASP2_ADVERTISED_SERVICE_ENTRY_CONTAINERV1_0_20 ) );
#endif
struct ArrayOfElementsOfWDI_P2P_ADVERTISED_PREFIX_ENTRY_CONTAINER
{
    UINT32 ElementCount;
    WDI_P2P_ADVERTISED_PREFIX_ENTRY_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_P2P_ADVERTISED_PREFIX_ENTRY_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_P2P_ADVERTISED_PREFIX_ENTRY_CONTAINER ) );
#endif

typedef struct _WDI_P2P_ADVERTISED_SERVICES_CONTAINER
{
    struct _WDI_P2P_ADVERTISED_SERVICES_CONTAINER_Optional
    {
        UINT32 ServiceEntry_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_ADVERTISED_SERVICES_CONTAINER_Optional() : ServiceEntry_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

#ifdef __cplusplus
    ArrayOfElements<WDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER> ServiceEntry;
#else
    struct ArrayOfElementsOfWDI_P2P_ADVERTISED_SERVICE_ENTRY_CONTAINER ServiceEntry;
#endif
    UINT16_CONTAINER ServiceUpdateIndicator;
#ifdef __cplusplus
    _WDI_P2P_ADVERTISED_SERVICES_CONTAINER() : ServiceUpdateIndicator( (UINT16_CONTAINER)0 )
    {
    };
#endif
} WDI_P2P_ADVERTISED_SERVICES_CONTAINER, *PWDI_P2P_ADVERTISED_SERVICES_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_ADVERTISED_SERVICES_CONTAINER * pField );
    }
}
#endif
typedef WDI_P2P_LISTEN_STATE WDI_P2P_LISTEN_STATE_CONTAINER;

typedef WDI_P2P_DISCOVER_MODE WDI_P2P_DISCOVER_MODE_CONTAINER;

typedef WDI_P2P_BACKGROUND_DISCOVER_MODE WDI_P2P_BACKGROUND_DISCOVER_MODE_CONTAINER;

typedef WDI_SCAN_DWELL_TIME WDI_SCAN_DWELL_TIME_CONTAINER;

struct ArrayOfElementsOfWDI_CHANNEL_NUMBER
{
    UINT32 ElementCount;
    WDI_CHANNEL_NUMBER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_CHANNEL_NUMBER> ) == sizeof( struct ArrayOfElementsOfWDI_CHANNEL_NUMBER ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_CHANNEL_NUMBER> WDI_CHANNEL_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_CHANNEL_NUMBER WDI_CHANNEL_LIST_CONTAINER;
#endif

typedef WDI_BAND_ID WDI_BAND_ID_CONTAINER;

struct ArrayOfElementsOfWDI_BAND_ID
{
    UINT32 ElementCount;
    WDI_BAND_ID* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_BAND_ID> ) == sizeof( struct ArrayOfElementsOfWDI_BAND_ID ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_BAND_ID> WDI_BAND_ID_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_BAND_ID WDI_BAND_ID_LIST_CONTAINER;
#endif

typedef struct _WDI_AP_BAND_CHANNEL_LIST_CONTAINER
{
    struct _WDI_AP_BAND_CHANNEL_LIST_CONTAINER_Optional
    {
        UINT32 ChannelList_IsPresent : 1;
#ifdef __cplusplus
        _WDI_AP_BAND_CHANNEL_LIST_CONTAINER_Optional() : ChannelList_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_BAND_ID_CONTAINER BandID;
    WDI_CHANNEL_LIST_CONTAINER ChannelList;
#ifdef __cplusplus
    _WDI_AP_BAND_CHANNEL_LIST_CONTAINER() : BandID( (WDI_BAND_ID_CONTAINER)0 )
    {
    };
#endif
} WDI_AP_BAND_CHANNEL_LIST_CONTAINER, *PWDI_AP_BAND_CHANNEL_LIST_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_AP_BAND_CHANNEL_LIST_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_SCAN_BAND_CHANNEL_LIST_CONTAINER
{
    WDI_BAND_ID_CONTAINER BandID;
    WDI_CHANNEL_LIST_CONTAINER ChannelList;
#ifdef __cplusplus
    _WDI_SCAN_BAND_CHANNEL_LIST_CONTAINER() : BandID( (WDI_BAND_ID_CONTAINER)0 )
    {
    };
#endif
} WDI_SCAN_BAND_CHANNEL_LIST_CONTAINER, *PWDI_SCAN_BAND_CHANNEL_LIST_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SCAN_BAND_CHANNEL_LIST_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_AP_BAND_INFORMATION_CONTAINER
{
    WDI_BAND_ID_CONTAINER BandID;
    UINT8_CONTAINER IsPCPinPBSS;
#ifdef __cplusplus
    _WDI_AP_BAND_INFORMATION_CONTAINER() : BandID( (WDI_BAND_ID_CONTAINER)0 ), IsPCPinPBSS( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_AP_BAND_INFORMATION_CONTAINER, *PWDI_AP_BAND_INFORMATION_CONTAINER;

typedef struct _WDI_BAND_CHANNEL_LIST_CONTAINER
{
    struct _WDI_BAND_CHANNEL_LIST_CONTAINER_Optional
    {
        UINT32 ChannelList_IsPresent : 1;
#ifdef __cplusplus
        _WDI_BAND_CHANNEL_LIST_CONTAINER_Optional() : ChannelList_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_BAND_ID_CONTAINER BandID;
    WDI_CHANNEL_LIST_CONTAINER ChannelList;
#ifdef __cplusplus
    _WDI_BAND_CHANNEL_LIST_CONTAINER() : BandID( (WDI_BAND_ID_CONTAINER)0 )
    {
    };
#endif
} WDI_BAND_CHANNEL_LIST_CONTAINER, *PWDI_BAND_CHANNEL_LIST_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_BAND_CHANNEL_LIST_CONTAINER * pField );
    }
}
#endif
typedef WDI_SIGNAL_INFO WDI_SIGNAL_INFO_CONTAINER;

typedef WDI_BSS_ENTRY_CHANNEL_INFO WDI_CHANNEL_INFO_CONTAINER;

typedef struct _WDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER
{
    struct _WDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER_Optional
    {
        UINT32 ColocatedAP_IsPresent : 1;
#ifdef __cplusplus
        _WDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER_Optional() : ColocatedAP_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER Bssid;
    UINT8_CONTAINER ChannelNumber;
    WDI_CHANNEL_INFO_CONTAINER ColocatedAP;
#ifdef __cplusplus
    _WDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER() : ChannelNumber( (UINT8_CONTAINER)0 )
    {
        memset( &Bssid, 0, sizeof( Bssid ) );
    };
#endif
} WDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER, *PWDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER;
typedef WDI_BSS_ENTRY_AGE_INFO WDI_AGE_INFO_CONTAINER;

struct ArrayOfElementsOfWDI_LINK_QUALITY_BAR_MAP_PARAMETERS
{
    UINT32 ElementCount;
    WDI_LINK_QUALITY_BAR_MAP_PARAMETERS* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_LINK_QUALITY_BAR_MAP_PARAMETERS> ) == sizeof( struct ArrayOfElementsOfWDI_LINK_QUALITY_BAR_MAP_PARAMETERS ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_LINK_QUALITY_BAR_MAP_PARAMETERS> WDI_LINK_QUALITY_BAR_MAP_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_LINK_QUALITY_BAR_MAP_PARAMETERS WDI_LINK_QUALITY_BAR_MAP_CONTAINER;
#endif

typedef WDI_SCAN_TYPE WDI_ADAPTER_NLO_SCAN_MODE_CONTAINER;

struct ArrayOfElementsOfWDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER
{
    UINT32 ElementCount;
    WDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER ) );
#endif

typedef struct _WDI_BSS_ENTRY_CONTAINER
{
    struct _WDI_BSS_ENTRY_CONTAINER_Optional
    {
        UINT32 ProbeResponseFrame_IsPresent : 1;
        UINT32 BeaconFrame_IsPresent : 1;
        UINT32 DeviceSpecificContext_IsPresent : 1;
        UINT32 EntryAgeInfo_IsPresent : 1;
        UINT32 P2PDiscoveredServiceEntry_IsPresent : 1;
#ifdef __cplusplus
        _WDI_BSS_ENTRY_CONTAINER_Optional() : ProbeResponseFrame_IsPresent( FALSE ), BeaconFrame_IsPresent( FALSE ), DeviceSpecificContext_IsPresent( FALSE ), EntryAgeInfo_IsPresent( FALSE ), P2PDiscoveredServiceEntry_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER BSSID;
    WDI_BYTE_BLOB ProbeResponseFrame;
    WDI_BYTE_BLOB BeaconFrame;
    WDI_SIGNAL_INFO_CONTAINER SignalInfo;
    WDI_CHANNEL_INFO_CONTAINER ChannelInfo;
    WDI_BYTE_BLOB DeviceSpecificContext;
    WDI_AGE_INFO_CONTAINER EntryAgeInfo;
#ifdef __cplusplus
    ArrayOfElements<WDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER> P2PDiscoveredServiceEntry;
#else
    struct ArrayOfElementsOfWDI_P2P_DISCOVERED_SERVICE_ENTRY_CONTAINER P2PDiscoveredServiceEntry;
#endif
#ifdef __cplusplus
    _WDI_BSS_ENTRY_CONTAINER()
    {
        memset( &BSSID, 0, sizeof( BSSID ) );
    };
#endif
} WDI_BSS_ENTRY_CONTAINER, *PWDI_BSS_ENTRY_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_BSS_ENTRY_CONTAINER * pField );
    }
}
#endif
typedef WDI_P2P_LISTEN_DURATION WDI_P2P_LISTEN_DURATION_CONTAINER;

struct ArrayOfElementsOfWDI_SCAN_BAND_CHANNEL_LIST_CONTAINER
{
    UINT32 ElementCount;
    WDI_SCAN_BAND_CHANNEL_LIST_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_SCAN_BAND_CHANNEL_LIST_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_SCAN_BAND_CHANNEL_LIST_CONTAINER ) );
#endif

typedef struct _WDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER
{
    WDI_P2P_LISTEN_DURATION_CONTAINER ListenDuration;
#ifdef __cplusplus
    ArrayOfElements<WDI_SCAN_BAND_CHANNEL_LIST_CONTAINER> BandChannelList;
#else
    struct ArrayOfElementsOfWDI_SCAN_BAND_CHANNEL_LIST_CONTAINER BandChannelList;
#endif
} WDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER, *PWDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER * pField );
    }
}
#endif
typedef WDI_ACTION_FRAME_REQUEST_PARAMETERS WDI_SEND_ACTION_FRAME_REQUEST_PARAMETERS_CONTAINER;

typedef WDI_P2P_ACTION_FRAME_REQUEST_PARAMETERS WDI_P2P_SEND_ACTION_FRAME_REQUEST_PARAMETERS_CONTAINER;

typedef WDI_P2P_GO_NEGOTIATION_REQUEST_PARAMETERS WDI_P2P_GO_NEGOTIATION_REQUEST_PARAMETERS_CONTAINER;

typedef struct _WDI_P2P_GO_NEGOTIATION_REQUEST_INFO_CONTAINER
{
    struct _WDI_P2P_GO_NEGOTIATION_REQUEST_INFO_CONTAINER_Optional
    {
        UINT32 RemoteListenChannel_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_GO_NEGOTIATION_REQUEST_INFO_CONTAINER_Optional() : RemoteListenChannel_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_GO_NEGOTIATION_REQUEST_PARAMETERS_CONTAINER RequestParams;
    WDI_P2P_CHANNEL_CONTAINER RemoteListenChannel;
} WDI_P2P_GO_NEGOTIATION_REQUEST_INFO_CONTAINER, *PWDI_P2P_GO_NEGOTIATION_REQUEST_INFO_CONTAINER;
typedef WDI_P2P_INVITATION_REQUEST_PARAMETERS WDI_P2P_INVITATION_REQUEST_PARAMETERS_CONTAINER;

typedef struct _WDI_P2P_GROUP_ID
{
    WDI_MAC_ADDRESS_CONTAINER DeviceAddress;
    WDI_SSID GroupSSID;
#ifdef __cplusplus
    _WDI_P2P_GROUP_ID()
    {
        memset( &DeviceAddress, 0, sizeof( DeviceAddress ) );
    };
#endif
} WDI_P2P_GROUP_ID, *PWDI_P2P_GROUP_ID;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_GROUP_ID * pField );
    }
}
#endif

typedef struct _WDI_P2P_INVITATION_REQUEST_INFO_CONTAINER
{
    struct _WDI_P2P_INVITATION_REQUEST_INFO_CONTAINER_Optional
    {
        UINT32 GroupBSSID_IsPresent : 1;
        UINT32 OperatingChannel_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_INVITATION_REQUEST_INFO_CONTAINER_Optional() : GroupBSSID_IsPresent( FALSE ), OperatingChannel_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_INVITATION_REQUEST_PARAMETERS_CONTAINER RequestParams;
    WDI_MAC_ADDRESS_CONTAINER GroupBSSID;
    WDI_P2P_CHANNEL_CONTAINER OperatingChannel;
    WDI_P2P_GROUP_ID GroupID;
#ifdef __cplusplus
    _WDI_P2P_INVITATION_REQUEST_INFO_CONTAINER()
    {
        memset( &GroupBSSID, 0, sizeof( GroupBSSID ) );
    };
#endif
} WDI_P2P_INVITATION_REQUEST_INFO_CONTAINER, *PWDI_P2P_INVITATION_REQUEST_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_INVITATION_REQUEST_INFO_CONTAINER * pField );
    }
}
#endif
typedef WDI_P2P_PROVISION_DISCOVERY_REQUEST_PARAMETERS WDI_P2P_PROVISION_DISCOVERY_REQUEST_PARAMETERS_CONTAINER;

typedef WDI_P2P_PROVISION_DISCOVERY_RESPONSE_PARAMETERS WDI_P2P_PROVISION_DISCOVERY_RESPONSE_PARAMETERS_CONTAINER;

typedef WDI_P2P_PROVISION_SERVICE_ATTRIBUTES WDI_P2P_PROVISION_SERVICE_ATTRIBUTES_CONTAINER;

typedef struct _WDI_P2P_PROVISION_DISCOVERY_REQUEST_INFO_CONTAINER
{
    struct _WDI_P2P_PROVISION_DISCOVERY_REQUEST_INFO_CONTAINER_Optional
    {
        UINT32 GroupID_IsPresent : 1;
        UINT32 ServiceAttributes_IsPresent : 1;
        UINT32 PersistentGroupID_IsPresent : 1;
        UINT32 ServiceSessionInfo_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_PROVISION_DISCOVERY_REQUEST_INFO_CONTAINER_Optional() : GroupID_IsPresent( FALSE ), ServiceAttributes_IsPresent( FALSE ), PersistentGroupID_IsPresent( FALSE ), ServiceSessionInfo_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_PROVISION_DISCOVERY_REQUEST_PARAMETERS_CONTAINER RequestParams;
    WDI_P2P_GROUP_ID GroupID;
    WDI_P2P_PROVISION_SERVICE_ATTRIBUTES_CONTAINER ServiceAttributes;
    WDI_P2P_GROUP_ID PersistentGroupID;
    WDI_BYTE_BLOB ServiceSessionInfo;
} WDI_P2P_PROVISION_DISCOVERY_REQUEST_INFO_CONTAINER, *PWDI_P2P_PROVISION_DISCOVERY_REQUEST_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_PROVISION_DISCOVERY_REQUEST_INFO_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_P2P_PROVISION_DISCOVERY_RESPONSE_INFO_CONTAINER
{
    struct _WDI_P2P_PROVISION_DISCOVERY_RESPONSE_INFO_CONTAINER_Optional
    {
        UINT32 ServiceAttributes_IsPresent : 1;
        UINT32 GroupID_IsPresent : 1;
        UINT32 PersistentGroupID_IsPresent : 1;
        UINT32 ServiceSessionInfo_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_PROVISION_DISCOVERY_RESPONSE_INFO_CONTAINER_Optional() : ServiceAttributes_IsPresent( FALSE ), GroupID_IsPresent( FALSE ), PersistentGroupID_IsPresent( FALSE ), ServiceSessionInfo_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_PROVISION_DISCOVERY_RESPONSE_PARAMETERS_CONTAINER ResponseParams;
    WDI_P2P_PROVISION_SERVICE_ATTRIBUTES_CONTAINER ServiceAttributes;
    WDI_P2P_GROUP_ID GroupID;
    WDI_P2P_GROUP_ID PersistentGroupID;
    WDI_BYTE_BLOB ServiceSessionInfo;
} WDI_P2P_PROVISION_DISCOVERY_RESPONSE_INFO_CONTAINER, *PWDI_P2P_PROVISION_DISCOVERY_RESPONSE_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_PROVISION_DISCOVERY_RESPONSE_INFO_CONTAINER * pField );
    }
}
#endif
typedef WDI_P2P_SEND_ACTION_FRAME_RESPONSE_PARAMETERS WDI_P2P_SEND_ACTION_FRAME_RESPONSE_PARAMETERS_CONTAINER;

typedef WDI_P2P_GO_NEGOTIATION_RESPONSE_PARAMETERS WDI_P2P_GO_NEGOTIATION_RESPONSE_PARAMETERS_CONTAINER;

typedef WDI_PM_PROTOCOL_OFFLOAD_IPv4ARP_STRUCT WDI_PM_PROTOCOL_OFFLOAD_IPv4ARP_CONTAINER;

typedef WDI_PM_PROTOCOL_OFFLOAD_IPv6_STRUCT WDI_PM_PROTOCOL_OFFLOAD_IPv6NS_CONTAINER;

typedef WDI_PM_PROTOCOL_OFFLOAD_80211RSN_REKEY_STRUCT WDI_PM_PROTOCOL_OFFLOAD_80211RSN_REKEY_CONTAINER_V1_1_5;

typedef struct _WDI_P2P_GO_NEGOTIATION_RESPONSE_INFO_CONTAINER
{
    struct _WDI_P2P_GO_NEGOTIATION_RESPONSE_INFO_CONTAINER_Optional
    {
        UINT32 GroupID_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_GO_NEGOTIATION_RESPONSE_INFO_CONTAINER_Optional() : GroupID_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_GO_NEGOTIATION_RESPONSE_PARAMETERS_CONTAINER ResponseParams;
    WDI_P2P_GROUP_ID GroupID;
} WDI_P2P_GO_NEGOTIATION_RESPONSE_INFO_CONTAINER, *PWDI_P2P_GO_NEGOTIATION_RESPONSE_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_GO_NEGOTIATION_RESPONSE_INFO_CONTAINER * pField );
    }
}
#endif
typedef WDI_P2P_GO_NEGOTIATION_CONFIRMATION_PARAMETERS WDI_P2P_GO_NEGOTIATION_CONFIRMATION_PARAMETERS_CONTAINER;

typedef WDI_MAC_STATISTICS_PARAMETERS WDI_MAC_STATISTICS_CONTAINER;

typedef WDI_PHY_STATISTICS_PARAMETERS WDI_PHY_STATISTICS_CONTAINER;

typedef WDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS_STRUCT WDI_SET_OFFLOAD_PARAMS_CONTAINER;

typedef struct _WDI_P2P_GO_NEGOTIATION_CONFIRMATION_INFO_CONTAINER
{
    struct _WDI_P2P_GO_NEGOTIATION_CONFIRMATION_INFO_CONTAINER_Optional
    {
        UINT32 GroupID_IsPresent : 1;
        UINT32 RemoteListenChannel_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_GO_NEGOTIATION_CONFIRMATION_INFO_CONTAINER_Optional() : GroupID_IsPresent( FALSE ), RemoteListenChannel_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_GO_NEGOTIATION_CONFIRMATION_PARAMETERS_CONTAINER ConfirmationParams;
    WDI_P2P_GROUP_ID GroupID;
    WDI_P2P_CHANNEL_CONTAINER RemoteListenChannel;
} WDI_P2P_GO_NEGOTIATION_CONFIRMATION_INFO_CONTAINER, *PWDI_P2P_GO_NEGOTIATION_CONFIRMATION_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_GO_NEGOTIATION_CONFIRMATION_INFO_CONTAINER * pField );
    }
}
#endif
typedef WDI_P2P_INVITATION_RESPONSE_PARAMETERS WDI_P2P_INVITATION_RESPONSE_PARAMETERS_CONTAINER;

typedef struct _WDI_P2P_INVITATION_RESPONSE_INFO_CONTAINER
{
    struct _WDI_P2P_INVITATION_RESPONSE_INFO_CONTAINER_Optional
    {
        UINT32 GroupBSSID_IsPresent : 1;
        UINT32 OperatingChannel_IsPresent : 1;
#ifdef __cplusplus
        _WDI_P2P_INVITATION_RESPONSE_INFO_CONTAINER_Optional() : GroupBSSID_IsPresent( FALSE ), OperatingChannel_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_INVITATION_RESPONSE_PARAMETERS_CONTAINER ResponseParams;
    WDI_MAC_ADDRESS_CONTAINER GroupBSSID;
    WDI_P2P_CHANNEL_CONTAINER OperatingChannel;
#ifdef __cplusplus
    _WDI_P2P_INVITATION_RESPONSE_INFO_CONTAINER()
    {
        memset( &GroupBSSID, 0, sizeof( GroupBSSID ) );
    };
#endif
} WDI_P2P_INVITATION_RESPONSE_INFO_CONTAINER, *PWDI_P2P_INVITATION_RESPONSE_INFO_CONTAINER;
typedef WDI_P2P_INCOMING_ACTION_FRAME_PARAMETERS WDI_P2P_INCOMING_ACTION_FRAME_PARAMETERS_CONTAINER;

typedef struct _WDI_INCOMING_P2P_ACTION_FRAME_INFO
{
    struct _WDI_INCOMING_P2P_ACTION_FRAME_INFO_Optional
    {
        UINT32 DeviceContext_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INCOMING_P2P_ACTION_FRAME_INFO_Optional() : DeviceContext_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_INCOMING_ACTION_FRAME_PARAMETERS_CONTAINER FrameParams;
    WDI_BYTE_BLOB FrameIEs;
    WDI_BYTE_BLOB DeviceContext;
} WDI_INCOMING_P2P_ACTION_FRAME_INFO, *PWDI_INCOMING_P2P_ACTION_FRAME_INFO;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_INCOMING_P2P_ACTION_FRAME_INFO * pField );
    }
}
#endif
typedef WDI_AP_ASSOCIATION_RESPONSE_PARAMETERS WDI_AP_ASSOCIATION_RESPONSE_PARAMETERS_CONTAINER;

typedef WDI_AP_ASSOCIATION_REQUEST_PARAMETERS WDI_AP_ASSOCIATION_REQUEST_PARAMETERS_CONTAINER;

typedef struct _WDI_INCOMING_ASSOCIATION_REQUEST_INFO_CONTAINER
{
    struct _WDI_INCOMING_ASSOCIATION_REQUEST_INFO_CONTAINER_Optional
    {
        UINT32 AssocRequestDeviceContext_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INCOMING_ASSOCIATION_REQUEST_INFO_CONTAINER_Optional() : AssocRequestDeviceContext_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_AP_ASSOCIATION_REQUEST_PARAMETERS_CONTAINER AssocRequestParams;
    WDI_BYTE_BLOB AssocRequestFrame;
    WDI_BYTE_BLOB AssocRequestDeviceContext;
} WDI_INCOMING_ASSOCIATION_REQUEST_INFO_CONTAINER, *PWDI_INCOMING_ASSOCIATION_REQUEST_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_INCOMING_ASSOCIATION_REQUEST_INFO_CONTAINER * pField );
    }
}
#endif
typedef WDI_DISCONNECT_PARAMETERS WDI_DISCONNECT_PARAMETERS_CONTAINER;

typedef WDI_P2P_SEND_ACTION_FRAME_RESULT_PARAMETERS WDI_SEND_ACTION_FRAME_RESULT_PARAMETERS_CONTAINER;

typedef struct _WDI_P2P_SEND_ACTION_FRAME_RESULT_CONTAINER
{
    WDI_SEND_ACTION_FRAME_RESULT_PARAMETERS_CONTAINER FrameParameters;
    WDI_BYTE_BLOB FrameIEs;
} WDI_P2P_SEND_ACTION_FRAME_RESULT_CONTAINER, *PWDI_P2P_SEND_ACTION_FRAME_RESULT_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_SEND_ACTION_FRAME_RESULT_CONTAINER * pField );
    }
}
#endif
typedef WDI_ASSOC_RESULT_PARAMETERS WDI_ASSOC_RESPONSE_RESULT_PARAMETERS_CONTAINER;

struct ArrayOfElementsOfWDI_PHY_TYPE
{
    UINT32 ElementCount;
    WDI_PHY_TYPE* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_PHY_TYPE> ) == sizeof( struct ArrayOfElementsOfWDI_PHY_TYPE ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_PHY_TYPE> WDI_PHY_TYPE_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_PHY_TYPE WDI_PHY_TYPE_LIST_CONTAINER;
#endif

typedef WDI_DISCONNECT_INDICATION_PARAMETERS WDI_DISCONNECT_INDICATION_PARAMETERS_CONTAINER;

typedef WDI_P2P_GO_INTERNAL_RESET_POLICY WDI_P2P_GO_INTERNAL_RESET_POLICY_CONTAINER;

typedef WDI_CHECKSUM_OFFLOAD_V4_PARAMETERS WDI_CHECKSUM_OFFLOAD_V4_PARAMETERS_CONTAINER;

typedef WDI_CHECKSUM_OFFLOAD_V6_PARAMETERS WDI_CHECKSUM_OFFLOAD_V6_PARAMETERS_CONTAINER;

typedef struct _WDI_IPV4_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER
{
    WDI_CHECKSUM_OFFLOAD_V4_PARAMETERS_CONTAINER IpV4Transmit;
    WDI_CHECKSUM_OFFLOAD_V4_PARAMETERS_CONTAINER IpV4Receive;
} WDI_IPV4_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER, *PWDI_IPV4_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER;

typedef struct _WDI_IPV6_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER
{
    WDI_CHECKSUM_OFFLOAD_V6_PARAMETERS_CONTAINER IpV6Transmit;
    WDI_CHECKSUM_OFFLOAD_V6_PARAMETERS_CONTAINER IpV6Receive;
} WDI_IPV6_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER, *PWDI_IPV6_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER;

typedef struct _WDI_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER
{
    WDI_IPV4_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER ipv4ChecksumOffload;
    WDI_IPV6_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER ipv6ChecksumOffload;
} WDI_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER, *PWDI_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER;
typedef WDI_LSO_V1_CAPABILITIES WDI_LSO_V1_CAPABILITIES_CONTAINER;

typedef WDI_IPV4_LSO_V2 WDI_IPV4_LSO_V2_CONTAINER;

typedef WDI_IPV6_LSO_V2 WDI_IPV6_LSO_V2_CONTAINER;

typedef WDI_CANCEL_PARAMETERS_STRUCT WDI_CANCEL_PARAMETERS_CONTAINER;

typedef WDI_CREATE_PORT_COMPLETE_STRUCT WDI_PORT_ATTRIBUTES_CONTAINER;

typedef WDI_RECEIVE_COALESCE_OFFLOAD_CAPABILITIES WDI_RECEIVE_COALESCE_OFFLOAD_CAPABILITIES_CONTAINER;

typedef WDI_OFFLOAD_SCOPE WDI_OFFLOAD_SCOPE_CONTAINER;

typedef struct _WDI_LSO_V2_CAPABILITIES_CONTAINER
{
    WDI_IPV4_LSO_V2_CONTAINER Ipv4LSO;
    WDI_IPV6_LSO_V2_CONTAINER Ipv6LSO;
} WDI_LSO_V2_CAPABILITIES_CONTAINER, *PWDI_LSO_V2_CAPABILITIES_CONTAINER;

typedef struct _WDI_TCP_OFFLOAD_CAPABILITIES_CONTAINER
{
    WDI_CHECKSUM_OFFLOAD_CAPABILITIES_CONTAINER ChecksumOffload;
    WDI_LSO_V1_CAPABILITIES_CONTAINER LsoV1Capabilities;
    WDI_LSO_V2_CAPABILITIES_CONTAINER LsoV2Capabilities;
    WDI_RECEIVE_COALESCE_OFFLOAD_CAPABILITIES_CONTAINER ReceiveOffloadCapabilities;
    WDI_OFFLOAD_SCOPE_CONTAINER OffloadScope;
} WDI_TCP_OFFLOAD_CAPABILITIES_CONTAINER, *PWDI_TCP_OFFLOAD_CAPABILITIES_CONTAINER;
typedef WDI_DELETE_PORT_PARAMETERS WDI_TASK_DELETE_PORT_PARAMETERS_CONTAINER;

typedef WDI_CREATE_PORT_PARAMETERS WDI_TASK_CREATE_PORT_PARAMETERS_CONTAINER;

typedef WDI_DOT11_RESET_PARAMETERS WDI_DOT11_RESET_PARAMETERS_CONTAINER;

typedef WDI_SCAN_MODE WDI_SCAN_PARAMETERS_CONTAINER;

typedef WDI_COMMUNICATION_CAPABILITIES WDI_COMMUNICATION_CAPABILITIES_CONTAINER;

typedef WDI_INTERFACE_CAPABILITIES WDI_INTERFACE_CAPABILITIES_CONTAINER;

typedef WDI_INTERFACE_CAPABILITIESV1_0_20 WDI_INTERFACE_CAPABILITIES_CONTAINER_V1_0_20;

typedef WDI_INTERFACE_CAPABILITIESV1_0_1 WDI_INTERFACE_CAPABILITIES_CONTAINER_V1_0_1;

typedef WDI_INTERFACE_CAPABILITIESV1_0 WDI_INTERFACE_CAPABILITIES_CONTAINER_V1_0;

struct ArrayOfElementsOfWDI_ALGO_PAIRS
{
    UINT32 ElementCount;
    WDI_ALGO_PAIRS* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_ALGO_PAIRS> ) == sizeof( struct ArrayOfElementsOfWDI_ALGO_PAIRS ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_ALGO_PAIRS> WDI_ALGO_PAIRS_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_ALGO_PAIRS WDI_ALGO_PAIRS_LIST_CONTAINER;
#endif

typedef WDI_STATION_CAPABILITIES WDI_STATION_CAPABILITIES_CONTAINER;

typedef WDI_STATION_CAPABILITIESV1_1_8 WDI_STATION_CAPABILITIES_CONTAINERV1_1_8;

typedef WDI_STATION_CAPABILITIESV1_0 WDI_STATION_CAPABILITIES_CONTAINERV1_0;

typedef WDI_AP_CAPABILITIES WDI_AP_CAPABILITIES_CONTAINER;

struct ArrayOfElementsOfWDI_COUNTRY_REGION_LIST
{
    UINT32 ElementCount;
    WDI_COUNTRY_REGION_LIST* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_COUNTRY_REGION_LIST> ) == sizeof( struct ArrayOfElementsOfWDI_COUNTRY_REGION_LIST ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_COUNTRY_REGION_LIST> WDI_COUNTRY_REGION_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_COUNTRY_REGION_LIST WDI_COUNTRY_REGION_LIST_CONTAINER;
#endif

typedef WDI_VIRTUALIZATION_CAPABILITIES WDI_VIRTUALIZATION_CAPABILITIES_CONTAINER;

typedef WDI_P2P_CAPABILITIES WDI_P2P_CAPABILITIES_CONTAINER;

typedef WDI_P2P_CAPABILITIES_V1_0_10 WDI_P2P_CAPABILITIES_CONTAINER_V1_0_10;

typedef WDI_P2P_CAPABILITIES_V1_0_1 WDI_P2P_CAPABILITIES_CONTAINER_V1_0_1;

typedef WDI_DATAPATH_CAPABILITIES WDI_DATAPATH_CAPABILITIES_CONTAINER;

struct ArrayOfElementsOfWDI_BAND_CHANNEL_MAPPING_ENTRY
{
    UINT32 ElementCount;
    WDI_BAND_CHANNEL_MAPPING_ENTRY* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_BAND_CHANNEL_MAPPING_ENTRY> ) == sizeof( struct ArrayOfElementsOfWDI_BAND_CHANNEL_MAPPING_ENTRY ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_BAND_CHANNEL_MAPPING_ENTRY> WDI_BAND_CHANNEL_MAPPING_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_BAND_CHANNEL_MAPPING_ENTRY WDI_BAND_CHANNEL_MAPPING_CONTAINER;
#endif

struct ArrayOfElementsOfWDI_CHANNEL_MAPPING_ENTRY
{
    UINT32 ElementCount;
    WDI_CHANNEL_MAPPING_ENTRY* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_CHANNEL_MAPPING_ENTRY> ) == sizeof( struct ArrayOfElementsOfWDI_CHANNEL_MAPPING_ENTRY ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_CHANNEL_MAPPING_ENTRY> WDI_CHANNEL_MAPPING_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_CHANNEL_MAPPING_ENTRY WDI_CHANNEL_MAPPING_CONTAINER;
#endif

typedef WDI_BAND_CAPABILITIES WDI_BAND_CAPABILITIES_CONTAINER;

typedef WDI_PHY_CAPABILITIES WDI_PHY_CAPABILITIES_CONTAINER;

struct ArrayOfElementsOfWDI_DATA_RATE_LIST
{
    UINT32 ElementCount;
    WDI_DATA_RATE_LIST* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_DATA_RATE_LIST> ) == sizeof( struct ArrayOfElementsOfWDI_DATA_RATE_LIST ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_DATA_RATE_LIST> WDI_PHY_DATA_RATE_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_DATA_RATE_LIST WDI_PHY_DATA_RATE_LIST_CONTAINER;
#endif

typedef WDI_PM_CAPABILITIES WDI_PM_CAPABILITIES_CONTAINER;

typedef WDI_RECEIVE_COALESCING_CAPABILITIES WDI_RECEIVE_COALESCING_CAPABILITIES_CONTAINER;

typedef WDI_PRIVACY_EXEMPTION_PARAMETERS_STRUCT WDI_PRIVACY_EXEMPTION_LIST_CONTAINER;

typedef WDI_OS_POWER_MANAGEMENT_FEATURES WDI_OS_POWER_MGMT_FEATURES_CONTAINER;

typedef struct _WDI_STATION_ATTRIBUTES_CONTAINER
{
    struct _WDI_STATION_ATTRIBUTES_CONTAINER_Optional
    {
        UINT32 UnicastAlgorithms_IsPresent : 1;
        UINT32 MulticastDataAlgorithms_IsPresent : 1;
        UINT32 MulticastManagementAlgorithms_IsPresent : 1;
#ifdef __cplusplus
        _WDI_STATION_ATTRIBUTES_CONTAINER_Optional() : UnicastAlgorithms_IsPresent( FALSE ), MulticastDataAlgorithms_IsPresent( FALSE ), MulticastManagementAlgorithms_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_STATION_CAPABILITIES_CONTAINER StationCapabilities;
    WDI_ALGO_PAIRS_LIST_CONTAINER UnicastAlgorithms;
    WDI_ALGO_PAIRS_LIST_CONTAINER MulticastDataAlgorithms;
    WDI_ALGO_PAIRS_LIST_CONTAINER MulticastManagementAlgorithms;
} WDI_STATION_ATTRIBUTES_CONTAINER, *PWDI_STATION_ATTRIBUTES_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_STATION_ATTRIBUTES_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_AP_ATTRIBUTES_CONTAINER
{
    WDI_AP_CAPABILITIES_CONTAINER APCapabilities;
    WDI_ALGO_PAIRS_LIST_CONTAINER UnicastAlgorithms;
    WDI_ALGO_PAIRS_LIST_CONTAINER MulticastDataAlgorithms;
} WDI_AP_ATTRIBUTES_CONTAINER, *PWDI_AP_ATTRIBUTES_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_AP_ATTRIBUTES_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_VIRTUALIZATION_ATTRIBUTES_CONTAINER
{
    WDI_VIRTUALIZATION_CAPABILITIES_CONTAINER VirtualizationCapabilities;
} WDI_VIRTUALIZATION_ATTRIBUTES_CONTAINER, *PWDI_VIRTUALIZATION_ATTRIBUTES_CONTAINER;

typedef struct _WDI_P2P_ATTRIBUTES_CONTAINER
{
    WDI_P2P_CAPABILITIES_CONTAINER P2PCapabilities;
    WDI_ADDRESS_LIST_CONTAINER InterfaceAddressList;
} WDI_P2P_ATTRIBUTES_CONTAINER, *PWDI_P2P_ATTRIBUTES_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_ATTRIBUTES_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_BAND_INFO_CONTAINER
{
    WDI_BAND_CAPABILITIES_CONTAINER BandCapabilities;
    WDI_PHY_TYPE_LIST_CONTAINER ValidPhyTypes;
    WDI_CHANNEL_MAPPING_CONTAINER ValidChannelTypes;
    UINT32_LIST_CONTAINER ChannelWidthList;
} WDI_BAND_INFO_CONTAINER, *PWDI_BAND_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_BAND_INFO_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_PHY_INFO_CONTAINER
{
    WDI_PHY_CAPABILITIES_CONTAINER PhyCapabilities;
    UINT32_LIST_CONTAINER TxPowerLevelList;
    WDI_PHY_DATA_RATE_LIST_CONTAINER DataRateList;
} WDI_PHY_INFO_CONTAINER, *PWDI_PHY_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_PHY_INFO_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_COMMUNICATION_ATTRIBUTES_CONTAINER
{
    struct _WDI_COMMUNICATION_ATTRIBUTES_CONTAINER_Optional
    {
        UINT32 CommunicationCapabilities_IsPresent : 1;
#ifdef __cplusplus
        _WDI_COMMUNICATION_ATTRIBUTES_CONTAINER_Optional() : CommunicationCapabilities_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_COMMUNICATION_CAPABILITIES_CONTAINER CommunicationCapabilities;
} WDI_COMMUNICATION_ATTRIBUTES_CONTAINER, *PWDI_COMMUNICATION_ATTRIBUTES_CONTAINER;

typedef struct _WDI_INTERFACE_ATTRIBUTES_CONTAINER
{
    struct _WDI_INTERFACE_ATTRIBUTES_CONTAINER_Optional
    {
        UINT32 NonWdiOidsList_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INTERFACE_ATTRIBUTES_CONTAINER_Optional() : NonWdiOidsList_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_INTERFACE_CAPABILITIES_CONTAINER InterfaceCapabilities;
    WDI_STRING_Z FirmwareVersion;
    UINT32_LIST_CONTAINER NonWdiOidsList;
} WDI_INTERFACE_ATTRIBUTES_CONTAINER, *PWDI_INTERFACE_ATTRIBUTES_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_INTERFACE_ATTRIBUTES_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_DATAPATH_ATTRIBUTES_CONTAINER
{
    struct _WDI_DATAPATH_ATTRIBUTES_CONTAINER_Optional
    {
        UINT32 DataPathCapabilities_IsPresent : 1;
#ifdef __cplusplus
        _WDI_DATAPATH_ATTRIBUTES_CONTAINER_Optional() : DataPathCapabilities_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_DATAPATH_CAPABILITIES_CONTAINER DataPathCapabilities;
} WDI_DATAPATH_ATTRIBUTES_CONTAINER, *PWDI_DATAPATH_ATTRIBUTES_CONTAINER;
typedef WDI_RADIO_STATUS WDI_RADIO_STATUS_CONTAINER;

typedef WDI_GET_AUTO_POWER_SAVE_STRUCT WDI_GET_AUTO_POWER_SAVE_CONTAINER;

typedef WDI_CONNECTION_SETTINGS WDI_CONNECTION_SETTINGS_CONTAINER;

typedef WDI_CONNECTION_SETTINGSV2_0_8 WDI_CONNECTION_SETTINGS_CONTAINER_V2_0_8;

typedef WDI_CONNECTION_SETTINGSV2_0_7 WDI_CONNECTION_SETTINGS_CONTAINER_V2_0_7;

typedef WDI_CONNECTION_SETTINGSV1_0_1 WDI_CONNECTION_SETTINGS_CONTAINER_V1_0_1;

typedef WDI_CONNECTION_SETTINGSV1_0 WDI_CONNECTION_SETTINGS_CONTAINER_V1_0;

typedef WDI_ASSOCIATION_RESULT_PARAMETERS WDI_ASSOCIATION_RESULT_PARAMETERS_CONTAINER;

typedef WDI_ASSOCIATION_RESULT_PARAMETERSV1_1_10 WDI_ASSOCIATION_RESULT_PARAMETERS_CONTAINERV1_1_10;

typedef WDI_CIPHER_ALGORITHM WDI_CIPHER_ALGORITHM_CONTAINER;

struct ArrayOfElementsOfWDI_ETHERTYPE_ENCAPSULATION_ENTRY
{
    UINT32 ElementCount;
    WDI_ETHERTYPE_ENCAPSULATION_ENTRY* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_ETHERTYPE_ENCAPSULATION_ENTRY> ) == sizeof( struct ArrayOfElementsOfWDI_ETHERTYPE_ENCAPSULATION_ENTRY ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<WDI_ETHERTYPE_ENCAPSULATION_ENTRY> WDI_ETHERTYPE_ENCAPSULATION_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfWDI_ETHERTYPE_ENCAPSULATION_ENTRY WDI_ETHERTYPE_ENCAPSULATION_LIST_CONTAINER;
#endif

typedef struct _WDI_FT_REASSOC_PARAMETERS_CONTAINER
{
    WDI_BYTE_BLOB MDE;
    WDI_BYTE_BLOB FTE;
    WDI_TYPE_PMK_NAME_CONTAINER PMKR0Name;
} WDI_FT_REASSOC_PARAMETERS_CONTAINER, *PWDI_FT_REASSOC_PARAMETERS_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_FT_REASSOC_PARAMETERS_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_FT_INITIAL_ASSOC_PARAMETERS_CONTAINER
{
    WDI_BYTE_BLOB MDE;
} WDI_FT_INITIAL_ASSOC_PARAMETERS_CONTAINER, *PWDI_FT_INITIAL_ASSOC_PARAMETERS_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_FT_INITIAL_ASSOC_PARAMETERS_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_SAE_COMMIT_PARAMS
{
    struct _WDI_SAE_COMMIT_PARAMS_Optional
    {
        UINT32 Scalar_IsPresent : 1;
        UINT32 Element_IsPresent : 1;
        UINT32 AntiCloggingToken_IsPresent : 1;
        UINT32 RejectedGroups_IsPresent : 1;
        UINT32 RsnaAkmSuite_IsPresent : 1;
        UINT32 CipherAlgorithm_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SAE_COMMIT_PARAMS_Optional() : Scalar_IsPresent( FALSE ), Element_IsPresent( FALSE ), AntiCloggingToken_IsPresent( FALSE ), RejectedGroups_IsPresent( FALSE ), RsnaAkmSuite_IsPresent( FALSE ), CipherAlgorithm_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    UINT16_CONTAINER FiniteCyclicGroup;
    WDI_BYTE_BLOB Scalar;
    WDI_BYTE_BLOB Element;
    WDI_BYTE_BLOB AntiCloggingToken;
    WDI_BYTE_BLOB RejectedGroups;
    RSNA_AKM_SUITE_CONTAINER RsnaAkmSuite;
    WDI_CIPHER_ALGORITHM_CONTAINER CipherAlgorithm;
    UINT16_CONTAINER StatusCode;
#ifdef __cplusplus
    _WDI_SAE_COMMIT_PARAMS() : FiniteCyclicGroup( (UINT16_CONTAINER)0 ), CipherAlgorithm( (WDI_CIPHER_ALGORITHM_CONTAINER)0 ), StatusCode( (UINT16_CONTAINER)0 )
    {
    };
#endif
} WDI_SAE_COMMIT_PARAMS, *PWDI_SAE_COMMIT_PARAMS;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SAE_COMMIT_PARAMS * pField );
    }
}
#endif

typedef struct _WDI_SAE_COMMIT_PARAMS_2_0_4
{
    struct _WDI_SAE_COMMIT_PARAMS_2_0_4_Optional
    {
        UINT32 Scalar_IsPresent : 1;
        UINT32 Element_IsPresent : 1;
        UINT32 AntiCloggingToken_IsPresent : 1;
        UINT32 RejectedGroups_IsPresent : 1;
        UINT32 RsnaAkmSuite_IsPresent : 1;
        UINT32 CipherAlgorithm_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SAE_COMMIT_PARAMS_2_0_4_Optional() : Scalar_IsPresent( FALSE ), Element_IsPresent( FALSE ), AntiCloggingToken_IsPresent( FALSE ), RejectedGroups_IsPresent( FALSE ), RsnaAkmSuite_IsPresent( FALSE ), CipherAlgorithm_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    UINT16_CONTAINER FiniteCyclicGroup;
    WDI_BYTE_BLOB Scalar;
    WDI_BYTE_BLOB Element;
    WDI_BYTE_BLOB AntiCloggingToken;
    WDI_BYTE_BLOB RejectedGroups;
    RSNA_AKM_SUITE_CONTAINER RsnaAkmSuite;
    WDI_CIPHER_ALGORITHM_CONTAINER CipherAlgorithm;
#ifdef __cplusplus
    _WDI_SAE_COMMIT_PARAMS_2_0_4() : FiniteCyclicGroup( (UINT16_CONTAINER)0 ), CipherAlgorithm( (WDI_CIPHER_ALGORITHM_CONTAINER)0 )
    {
    };
#endif
} WDI_SAE_COMMIT_PARAMS_2_0_4, *PWDI_SAE_COMMIT_PARAMS_2_0_4;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SAE_COMMIT_PARAMS_2_0_4 * pField );
    }
}
#endif

typedef struct _WDI_SAE_COMMIT_PARAMS_1_1_10
{
    struct _WDI_SAE_COMMIT_PARAMS_1_1_10_Optional
    {
        UINT32 Scalar_IsPresent : 1;
        UINT32 Element_IsPresent : 1;
        UINT32 AntiCloggingToken_IsPresent : 1;
        UINT32 RejectedGroups_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SAE_COMMIT_PARAMS_1_1_10_Optional() : Scalar_IsPresent( FALSE ), Element_IsPresent( FALSE ), AntiCloggingToken_IsPresent( FALSE ), RejectedGroups_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    UINT16_CONTAINER FiniteCyclicGroup;
    WDI_BYTE_BLOB Scalar;
    WDI_BYTE_BLOB Element;
    WDI_BYTE_BLOB AntiCloggingToken;
    WDI_BYTE_BLOB RejectedGroups;
#ifdef __cplusplus
    _WDI_SAE_COMMIT_PARAMS_1_1_10() : FiniteCyclicGroup( (UINT16_CONTAINER)0 )
    {
    };
#endif
} WDI_SAE_COMMIT_PARAMS_1_1_10, *PWDI_SAE_COMMIT_PARAMS_1_1_10;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SAE_COMMIT_PARAMS_1_1_10 * pField );
    }
}
#endif

typedef struct _WDI_SAE_CONFIRM_FRAME
{
    UINT16_CONTAINER SendConfirm;
    WDI_BYTE_BLOB Confirm;
#ifdef __cplusplus
    _WDI_SAE_CONFIRM_FRAME() : SendConfirm( (UINT16_CONTAINER)0 )
    {
    };
#endif
} WDI_SAE_CONFIRM_FRAME, *PWDI_SAE_CONFIRM_FRAME;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SAE_CONFIRM_FRAME * pField );
    }
}
#endif

typedef struct _WDI_HESSID_INFO_CONTAINER
{
    UINT8_CONTAINER AccessNetworkType;
    WDI_ADDRESS_LIST_CONTAINER HESSIDList;
    WDI_BYTE_BLOB HSIndicationElement;
#ifdef __cplusplus
    _WDI_HESSID_INFO_CONTAINER() : AccessNetworkType( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_HESSID_INFO_CONTAINER, *PWDI_HESSID_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_HESSID_INFO_CONTAINER * pField );
    }
}
#endif
struct ArrayOfElementsOfWDI_SSID
{
    UINT32 ElementCount;
    WDI_SSID* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_SSID> ) == sizeof( struct ArrayOfElementsOfWDI_SSID ) );
#endif
struct ArrayOfElementsOfRSNA_AKM_SUITE_CONTAINERV2_0_4
{
    UINT32 ElementCount;
    RSNA_AKM_SUITE_CONTAINERV2_0_4* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<RSNA_AKM_SUITE_CONTAINERV2_0_4> ) == sizeof( struct ArrayOfElementsOfRSNA_AKM_SUITE_CONTAINERV2_0_4 ) );
#endif

typedef struct _WDI_CONNECT_PARAMETERS_CONTAINER
{
    struct _WDI_CONNECT_PARAMETERS_CONTAINER_Optional
    {
        UINT32 HESSIDInfo_IsPresent : 1;
        UINT32 AssociationRequestVendorIE_IsPresent : 1;
        UINT32 ActivePhyTypeList_IsPresent : 1;
        UINT32 DisallowedBSSIDs_IsPresent : 1;
        UINT32 AllowedBSSIDs_IsPresent : 1;
        UINT32 OWEDHIE_IsPresent : 1;
        UINT32 UnavailableBandList_IsPresent : 1;
        UINT32 RsnaAkmCipherSuites_IsPresent : 1;
        UINT32 RsnaAkmSuites_IsPresent : 1;
#ifdef __cplusplus
        _WDI_CONNECT_PARAMETERS_CONTAINER_Optional() : HESSIDInfo_IsPresent( FALSE ), AssociationRequestVendorIE_IsPresent( FALSE ), ActivePhyTypeList_IsPresent( FALSE ), DisallowedBSSIDs_IsPresent( FALSE ), AllowedBSSIDs_IsPresent( FALSE ), OWEDHIE_IsPresent( FALSE ), UnavailableBandList_IsPresent( FALSE ), RsnaAkmCipherSuites_IsPresent( FALSE ), RsnaAkmSuites_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_CONNECTION_SETTINGS_CONTAINER ConnectionSettings;
#ifdef __cplusplus
    ArrayOfElements<WDI_SSID> SSIDList;
#else
    struct ArrayOfElementsOfWDI_SSID SSIDList;
#endif
    WDI_HESSID_INFO_CONTAINER HESSIDInfo;
    WDI_AUTH_ALGORITHM_LIST_CONTAINER AuthenticationAlgorithms;
    WDI_CIPHER_ALGORITHM_LIST_CONTAINER MulticastCipherAlgorithms;
    WDI_CIPHER_ALGORITHM_LIST_CONTAINER UnicastCipherAlgorithms;
    WDI_BYTE_BLOB AssociationRequestVendorIE;
    WDI_PHY_TYPE_LIST_CONTAINER ActivePhyTypeList;
    WDI_ADDRESS_LIST_CONTAINER DisallowedBSSIDs;
    WDI_ADDRESS_LIST_CONTAINER AllowedBSSIDs;
    WDI_BYTE_BLOB OWEDHIE;
    WDI_BAND_ID_LIST_CONTAINER UnavailableBandList;
    RSNA_AKM_CIPHER_PAIR_CONTAINER RsnaAkmCipherSuites;
    RSNA_AKM_SUITE_CONTAINER RsnaAkmSuites;
} WDI_CONNECT_PARAMETERS_CONTAINER, *PWDI_CONNECT_PARAMETERS_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_CONNECT_PARAMETERS_CONTAINER * pField );
    }
}
#endif
typedef WDI_BSS_SELECTION_PARAMETERS_CONTAINER WDI_BSS_SELECTION_PARAMETERS_CONTAINER;

typedef struct _WDI_CONNECT_BSS_ENTRY_CONTAINER
{
    struct _WDI_CONNECT_BSS_ENTRY_CONTAINER_Optional
    {
        UINT32 ProbeResponseFrame_IsPresent : 1;
        UINT32 BeaconFrame_IsPresent : 1;
        UINT32 DeviceSpecificContext_IsPresent : 1;
        UINT32 PMKID_IsPresent : 1;
        UINT32 AssociationRequestVendorIE_IsPresent : 1;
        UINT32 FTInitialAssocParameters_IsPresent : 1;
        UINT32 FTReAssocParameters_IsPresent : 1;
        UINT32 BSSSelectionParameters_IsPresent : 1;
#ifdef __cplusplus
        _WDI_CONNECT_BSS_ENTRY_CONTAINER_Optional() : ProbeResponseFrame_IsPresent( FALSE ), BeaconFrame_IsPresent( FALSE ), DeviceSpecificContext_IsPresent( FALSE ), PMKID_IsPresent( FALSE ), AssociationRequestVendorIE_IsPresent( FALSE ), FTInitialAssocParameters_IsPresent( FALSE ), FTReAssocParameters_IsPresent( FALSE ), BSSSelectionParameters_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER BSSID;
    WDI_BYTE_BLOB ProbeResponseFrame;
    WDI_BYTE_BLOB BeaconFrame;
    WDI_SIGNAL_INFO_CONTAINER SignalInfo;
    WDI_CHANNEL_INFO_CONTAINER ChannelInfo;
    WDI_BYTE_BLOB DeviceSpecificContext;
    WDI_BYTE_BLOB PMKID;
    WDI_BYTE_BLOB AssociationRequestVendorIE;
    WDI_FT_INITIAL_ASSOC_PARAMETERS_CONTAINER FTInitialAssocParameters;
    WDI_FT_REASSOC_PARAMETERS_CONTAINER FTReAssocParameters;
    WDI_BSS_SELECTION_PARAMETERS_CONTAINER BSSSelectionParameters;
#ifdef __cplusplus
    _WDI_CONNECT_BSS_ENTRY_CONTAINER()
    {
        memset( &BSSID, 0, sizeof( BSSID ) );
    };
#endif
} WDI_CONNECT_BSS_ENTRY_CONTAINER, *PWDI_CONNECT_BSS_ENTRY_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_CONNECT_BSS_ENTRY_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_ASSOCIATION_RESULT_CONTAINER
{
    struct _WDI_ASSOCIATION_RESULT_CONTAINER_Optional
    {
        UINT32 AssociationRequestFrame_IsPresent : 1;
        UINT32 AssociationResponseFrame_IsPresent : 1;
        UINT32 AuthenticationResponseFrame_IsPresent : 1;
        UINT32 BeaconProbeResponse_IsPresent : 1;
        UINT32 EthertypeEncapTable_IsPresent : 1;
        UINT32 LocalMloLinkBssId_IsPresent : 1;
#ifdef __cplusplus
        _WDI_ASSOCIATION_RESULT_CONTAINER_Optional() : AssociationRequestFrame_IsPresent( FALSE ), AssociationResponseFrame_IsPresent( FALSE ), AuthenticationResponseFrame_IsPresent( FALSE ), BeaconProbeResponse_IsPresent( FALSE ), EthertypeEncapTable_IsPresent( FALSE ), LocalMloLinkBssId_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER BSSID;
    WDI_ASSOCIATION_RESULT_PARAMETERS_CONTAINER AssociationResultParameters;
    WDI_BYTE_BLOB AssociationRequestFrame;
    WDI_BYTE_BLOB AssociationResponseFrame;
    WDI_BYTE_BLOB AuthenticationResponseFrame;
    WDI_BYTE_BLOB BeaconProbeResponse;
    WDI_ETHERTYPE_ENCAPSULATION_LIST_CONTAINER EthertypeEncapTable;
    WDI_PHY_TYPE_LIST_CONTAINER ActivePhyTypeList;
    WDI_MAC_ADDRESS_CONTAINER LocalMloLinkBssId;
#ifdef __cplusplus
    _WDI_ASSOCIATION_RESULT_CONTAINER()
    {
        memset( &BSSID, 0, sizeof( BSSID ) );
        memset( &LocalMloLinkBssId, 0, sizeof( LocalMloLinkBssId ) );
    };
#endif
} WDI_ASSOCIATION_RESULT_CONTAINER, *PWDI_ASSOCIATION_RESULT_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_ASSOCIATION_RESULT_CONTAINER * pField );
    }
}
#endif
typedef WDI_TKIP_MIC_FAILURE_PARAMETERS WDI_TKIP_MIC_FAILURE_PARAMETERS_CONTAINER;

typedef WDI_CIPHER_KEY_ID_STRUCT WDI_CIPHER_KEY_ID_CONTAINER;

typedef WDI_SET_DEFAULT_KEY_ID_STRUCT WDI_SET_DEFAULT_KEY_ID_CONTAINER;

typedef WDI_CIPHER_KEY_TYPE WDI_CIPHER_KEY_TYPE_CONTAINER;

typedef WDI_CIPHER_KEY_TYPE_STRUCT WDI_CIPHER_KEY_TYPE_INFO_CONTAINER;

typedef WDI_RECEIVE_SEQUENCE_COUNT_STRUCT WDI_RECEIVE_SEQUENCE_COUNT_CONTAINER;

typedef WDI_LINK_STATE_CHANGE_PARAMETERS_STRUCT WDI_LINK_STATE_CHANGE_PARAMETERS_CONTAINER;

typedef WDI_LINK_INFO_PARAMETERS_STRUCT WDI_LINK_INFO_CONTAINER;

typedef WDI_LINK_INFO_PARAMETERS_STRUCT_V2_0_3 WDI_LINK_INFO_CONTAINER_V2_0_3;

typedef struct _WDI_CIPHER_KEY_TKIP_INFO_CONTAINER
{
    WDI_PRIVATE_BYTE_BLOB TKIPKey;
    WDI_PRIVATE_BYTE_BLOB TKIPMIC;
} WDI_CIPHER_KEY_TKIP_INFO_CONTAINER, *PWDI_CIPHER_KEY_TKIP_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_CIPHER_KEY_TKIP_INFO_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER
{
    struct _WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_Optional
    {
        UINT32 CCMPKey_IsPresent : 1;
        UINT32 GCMPKey_IsPresent : 1;
        UINT32 TKIPInfo_IsPresent : 1;
        UINT32 BIPKey_IsPresent : 1;
        UINT32 WEPKey_IsPresent : 1;
        UINT32 IHVKey_IsPresent : 1;
        UINT32 GCMP_256Key_IsPresent : 1;
        UINT32 BIP_GMAC_256Key_IsPresent : 1;
        UINT32 LinkId_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_Optional() : CCMPKey_IsPresent( FALSE ), GCMPKey_IsPresent( FALSE ), TKIPInfo_IsPresent( FALSE ), BIPKey_IsPresent( FALSE ), WEPKey_IsPresent( FALSE ), IHVKey_IsPresent( FALSE ), GCMP_256Key_IsPresent( FALSE ), BIP_GMAC_256Key_IsPresent( FALSE ), LinkId_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_CIPHER_KEY_TYPE_CONTAINER CipherKeyType;
    WDI_CIPHER_ALGORITHM_CONTAINER CipherAlgorithm;
    WDI_RECEIVE_SEQUENCE_COUNT_CONTAINER ReceiveSequenceCount;
    WDI_PRIVATE_BYTE_BLOB CCMPKey;
    WDI_PRIVATE_BYTE_BLOB GCMPKey;
    WDI_CIPHER_KEY_TKIP_INFO_CONTAINER TKIPInfo;
    WDI_PRIVATE_BYTE_BLOB BIPKey;
    WDI_PRIVATE_BYTE_BLOB WEPKey;
    WDI_PRIVATE_BYTE_BLOB IHVKey;
    WDI_PRIVATE_BYTE_BLOB GCMP_256Key;
    WDI_PRIVATE_BYTE_BLOB BIP_GMAC_256Key;
    UINT32_CONTAINER LinkId;
#ifdef __cplusplus
    _WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER() : CipherKeyType( (WDI_CIPHER_KEY_TYPE_CONTAINER)0 ), CipherAlgorithm( (WDI_CIPHER_ALGORITHM_CONTAINER)0 ), LinkId( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER, *PWDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1
{
    struct _WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1_Optional
    {
        UINT32 CCMPKey_IsPresent : 1;
        UINT32 GCMPKey_IsPresent : 1;
        UINT32 TKIPInfo_IsPresent : 1;
        UINT32 BIPKey_IsPresent : 1;
        UINT32 WEPKey_IsPresent : 1;
        UINT32 IHVKey_IsPresent : 1;
        UINT32 GCMP_256Key_IsPresent : 1;
        UINT32 BIP_GMAC_256Key_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1_Optional() : CCMPKey_IsPresent( FALSE ), GCMPKey_IsPresent( FALSE ), TKIPInfo_IsPresent( FALSE ), BIPKey_IsPresent( FALSE ), WEPKey_IsPresent( FALSE ), IHVKey_IsPresent( FALSE ), GCMP_256Key_IsPresent( FALSE ), BIP_GMAC_256Key_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_CIPHER_KEY_TYPE_CONTAINER CipherKeyType;
    WDI_CIPHER_ALGORITHM_CONTAINER CipherAlgorithm;
    WDI_RECEIVE_SEQUENCE_COUNT_CONTAINER ReceiveSequenceCount;
    WDI_PRIVATE_BYTE_BLOB CCMPKey;
    WDI_PRIVATE_BYTE_BLOB GCMPKey;
    WDI_CIPHER_KEY_TKIP_INFO_CONTAINER TKIPInfo;
    WDI_PRIVATE_BYTE_BLOB BIPKey;
    WDI_PRIVATE_BYTE_BLOB WEPKey;
    WDI_PRIVATE_BYTE_BLOB IHVKey;
    WDI_PRIVATE_BYTE_BLOB GCMP_256Key;
    WDI_PRIVATE_BYTE_BLOB BIP_GMAC_256Key;
#ifdef __cplusplus
    _WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1() : CipherKeyType( (WDI_CIPHER_KEY_TYPE_CONTAINER)0 ), CipherAlgorithm( (WDI_CIPHER_ALGORITHM_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1, *PWDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1 * pField );
    }
}
#endif

typedef struct _WDI_PM_RSN_KEY_INFO_CONTAINER
{
    UINT32_CONTAINER ProtocolOffloadId;
    UINT64_CONTAINER ReplayCounter;
    WDI_PRIVATE_BYTE_BLOB KCK_CONTENT;
    WDI_PRIVATE_BYTE_BLOB KEK_CONTENT;
#ifdef __cplusplus
    _WDI_PM_RSN_KEY_INFO_CONTAINER() : ProtocolOffloadId( (UINT32_CONTAINER)0 ), ReplayCounter( (UINT64_CONTAINER)0 )
    {
    };
#endif
} WDI_PM_RSN_KEY_INFO_CONTAINER, *PWDI_PM_RSN_KEY_INFO_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_PM_RSN_KEY_INFO_CONTAINER * pField );
    }
}
#endif
typedef WDI_PM_PROTOCOL_OFFLOAD_80211RSN_REKEY_STRUCT WDI_PM_RSN_KEY_INFO_CONTAINERV1_1_8;

struct ArrayOfElementsOfWDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER
{
    UINT32 ElementCount;
    WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1
{
    UINT32 ElementCount;
    WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1> ) == sizeof( struct ArrayOfElementsOfWDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER_V1 ) );
#endif

typedef struct _WDI_RSN_OFFLOAD_KEYS_CONTAINER
{
    struct _WDI_RSN_OFFLOAD_KEYS_CONTAINER_Optional
    {
        UINT32 CipherKey_IsPresent : 1;
#ifdef __cplusplus
        _WDI_RSN_OFFLOAD_KEYS_CONTAINER_Optional() : CipherKey_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_PM_RSN_KEY_INFO_CONTAINER RsnKeyInfo;
#ifdef __cplusplus
    ArrayOfElements<WDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER> CipherKey;
#else
    struct ArrayOfElementsOfWDI_SET_CONFIGURED_CIPHER_KEY_CONTAINER CipherKey;
#endif
} WDI_RSN_OFFLOAD_KEYS_CONTAINER, *PWDI_RSN_OFFLOAD_KEYS_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_RSN_OFFLOAD_KEYS_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_SET_ADD_CIPHER_KEYS_CONTAINER
{
    struct _WDI_SET_ADD_CIPHER_KEYS_CONTAINER_Optional
    {
        UINT32 PeerMacAddress_IsPresent : 1;
        UINT32 CipherKeyID_IsPresent : 1;
        UINT32 ReceiveSequenceCount_IsPresent : 1;
        UINT32 CCMPKey_IsPresent : 1;
        UINT32 GCMPKey_IsPresent : 1;
        UINT32 TKIPInfo_IsPresent : 1;
        UINT32 BIPKey_IsPresent : 1;
        UINT32 WEPKey_IsPresent : 1;
        UINT32 IHVKey_IsPresent : 1;
        UINT32 GCMP_256Key_IsPresent : 1;
        UINT32 BIP_GMAC_256Key_IsPresent : 1;
        UINT32 LinkId_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_ADD_CIPHER_KEYS_CONTAINER_Optional() : PeerMacAddress_IsPresent( FALSE ), CipherKeyID_IsPresent( FALSE ), ReceiveSequenceCount_IsPresent( FALSE ), CCMPKey_IsPresent( FALSE ), GCMPKey_IsPresent( FALSE ), TKIPInfo_IsPresent( FALSE ), BIPKey_IsPresent( FALSE ), WEPKey_IsPresent( FALSE ), IHVKey_IsPresent( FALSE ), GCMP_256Key_IsPresent( FALSE ), BIP_GMAC_256Key_IsPresent( FALSE ), LinkId_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER PeerMacAddress;
    WDI_CIPHER_KEY_ID_CONTAINER CipherKeyID;
    WDI_CIPHER_KEY_TYPE_INFO_CONTAINER CipherKeyTypeInfo;
    WDI_RECEIVE_SEQUENCE_COUNT_CONTAINER ReceiveSequenceCount;
    WDI_PRIVATE_BYTE_BLOB CCMPKey;
    WDI_PRIVATE_BYTE_BLOB GCMPKey;
    WDI_CIPHER_KEY_TKIP_INFO_CONTAINER TKIPInfo;
    WDI_PRIVATE_BYTE_BLOB BIPKey;
    WDI_PRIVATE_BYTE_BLOB WEPKey;
    WDI_PRIVATE_BYTE_BLOB IHVKey;
    WDI_PRIVATE_BYTE_BLOB GCMP_256Key;
    WDI_PRIVATE_BYTE_BLOB BIP_GMAC_256Key;
    UINT32_CONTAINER LinkId;
#ifdef __cplusplus
    _WDI_SET_ADD_CIPHER_KEYS_CONTAINER() : LinkId( (UINT32_CONTAINER)0 )
    {
        memset( &PeerMacAddress, 0, sizeof( PeerMacAddress ) );
    };
#endif
} WDI_SET_ADD_CIPHER_KEYS_CONTAINER, *PWDI_SET_ADD_CIPHER_KEYS_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SET_ADD_CIPHER_KEYS_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1
{
    struct _WDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1_Optional
    {
        UINT32 PeerMacAddress_IsPresent : 1;
        UINT32 CipherKeyID_IsPresent : 1;
        UINT32 ReceiveSequenceCount_IsPresent : 1;
        UINT32 CCMPKey_IsPresent : 1;
        UINT32 GCMPKey_IsPresent : 1;
        UINT32 TKIPInfo_IsPresent : 1;
        UINT32 BIPKey_IsPresent : 1;
        UINT32 WEPKey_IsPresent : 1;
        UINT32 IHVKey_IsPresent : 1;
        UINT32 GCMP_256Key_IsPresent : 1;
        UINT32 BIP_GMAC_256Key_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1_Optional() : PeerMacAddress_IsPresent( FALSE ), CipherKeyID_IsPresent( FALSE ), ReceiveSequenceCount_IsPresent( FALSE ), CCMPKey_IsPresent( FALSE ), GCMPKey_IsPresent( FALSE ), TKIPInfo_IsPresent( FALSE ), BIPKey_IsPresent( FALSE ), WEPKey_IsPresent( FALSE ), IHVKey_IsPresent( FALSE ), GCMP_256Key_IsPresent( FALSE ), BIP_GMAC_256Key_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER PeerMacAddress;
    WDI_CIPHER_KEY_ID_CONTAINER CipherKeyID;
    WDI_CIPHER_KEY_TYPE_INFO_CONTAINER CipherKeyTypeInfo;
    WDI_RECEIVE_SEQUENCE_COUNT_CONTAINER ReceiveSequenceCount;
    WDI_PRIVATE_BYTE_BLOB CCMPKey;
    WDI_PRIVATE_BYTE_BLOB GCMPKey;
    WDI_CIPHER_KEY_TKIP_INFO_CONTAINER TKIPInfo;
    WDI_PRIVATE_BYTE_BLOB BIPKey;
    WDI_PRIVATE_BYTE_BLOB WEPKey;
    WDI_PRIVATE_BYTE_BLOB IHVKey;
    WDI_PRIVATE_BYTE_BLOB GCMP_256Key;
    WDI_PRIVATE_BYTE_BLOB BIP_GMAC_256Key;
#ifdef __cplusplus
    _WDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1()
    {
        memset( &PeerMacAddress, 0, sizeof( PeerMacAddress ) );
    };
#endif
} WDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1, *PWDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1 * pField );
    }
}
#endif

typedef struct _WDI_SET_DELETE_CIPHER_KEYS_CONTAINER
{
    struct _WDI_SET_DELETE_CIPHER_KEYS_CONTAINER_Optional
    {
        UINT32 PeerMacAddress_IsPresent : 1;
        UINT32 CipherKeyID_IsPresent : 1;
        UINT32 LinkId_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_DELETE_CIPHER_KEYS_CONTAINER_Optional() : PeerMacAddress_IsPresent( FALSE ), CipherKeyID_IsPresent( FALSE ), LinkId_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER PeerMacAddress;
    WDI_CIPHER_KEY_ID_CONTAINER CipherKeyID;
    WDI_CIPHER_KEY_TYPE_INFO_CONTAINER CipherKeyTypeInfo;
    UINT32_CONTAINER LinkId;
#ifdef __cplusplus
    _WDI_SET_DELETE_CIPHER_KEYS_CONTAINER() : LinkId( (UINT32_CONTAINER)0 )
    {
        memset( &PeerMacAddress, 0, sizeof( PeerMacAddress ) );
    };
#endif
} WDI_SET_DELETE_CIPHER_KEYS_CONTAINER, *PWDI_SET_DELETE_CIPHER_KEYS_CONTAINER;

typedef struct _WDI_SET_DELETE_CIPHER_KEYS_CONTAINER_V1
{
    struct _WDI_SET_DELETE_CIPHER_KEYS_CONTAINER_V1_Optional
    {
        UINT32 PeerMacAddress_IsPresent : 1;
        UINT32 CipherKeyID_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_DELETE_CIPHER_KEYS_CONTAINER_V1_Optional() : PeerMacAddress_IsPresent( FALSE ), CipherKeyID_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER PeerMacAddress;
    WDI_CIPHER_KEY_ID_CONTAINER CipherKeyID;
    WDI_CIPHER_KEY_TYPE_INFO_CONTAINER CipherKeyTypeInfo;
#ifdef __cplusplus
    _WDI_SET_DELETE_CIPHER_KEYS_CONTAINER_V1()
    {
        memset( &PeerMacAddress, 0, sizeof( PeerMacAddress ) );
    };
#endif
} WDI_SET_DELETE_CIPHER_KEYS_CONTAINER_V1, *PWDI_SET_DELETE_CIPHER_KEYS_CONTAINER_V1;
typedef WDI_NETWORK_LIST_OFFLOAD_CONFIG WDI_NETWORK_LIST_OFFLOAD_CONFIG_CONTAINER;

typedef struct _WDI_SSID_OFFLOAD_CONTAINER
{
    struct _WDI_SSID_OFFLOAD_CONTAINER_Optional
    {
        UINT32 IsDirectedProbeForHiddenPermitted_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SSID_OFFLOAD_CONTAINER_Optional() : IsDirectedProbeForHiddenPermitted_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_SSID SsidToScan;
    WDI_ALGO_PAIRS_LIST_CONTAINER UnicastAlgorithms;
    WDI_BAND_CHANNEL_MAPPING_CONTAINER BandChannelHintList;
    BOOL_CONTAINER IsDirectedProbeForHiddenPermitted;
#ifdef __cplusplus
    _WDI_SSID_OFFLOAD_CONTAINER() : IsDirectedProbeForHiddenPermitted( (BOOL_CONTAINER)0 )
    {
    };
#endif
} WDI_SSID_OFFLOAD_CONTAINER, *PWDI_SSID_OFFLOAD_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SSID_OFFLOAD_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_SSID_OFFLOAD_CONTAINERv1_1_9
{
    struct _WDI_SSID_OFFLOAD_CONTAINERv1_1_9_Optional
    {
        UINT32 IsDirectedProbeForHiddenPermitted_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SSID_OFFLOAD_CONTAINERv1_1_9_Optional() : IsDirectedProbeForHiddenPermitted_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_SSID SsidToScan;
    WDI_ALGO_PAIRS_LIST_CONTAINER UnicastAlgorithms;
    WDI_CHANNEL_MAPPING_CONTAINER ChannellHintList;
    BOOL_CONTAINER IsDirectedProbeForHiddenPermitted;
#ifdef __cplusplus
    _WDI_SSID_OFFLOAD_CONTAINERv1_1_9() : IsDirectedProbeForHiddenPermitted( (BOOL_CONTAINER)0 )
    {
    };
#endif
} WDI_SSID_OFFLOAD_CONTAINERv1_1_9, *PWDI_SSID_OFFLOAD_CONTAINERv1_1_9;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_SSID_OFFLOAD_CONTAINERv1_1_9 * pField );
    }
}
#endif
struct ArrayOfElementsOfWDI_SSID_OFFLOAD_CONTAINER
{
    UINT32 ElementCount;
    WDI_SSID_OFFLOAD_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_SSID_OFFLOAD_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_SSID_OFFLOAD_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_SSID_OFFLOAD_CONTAINERv1_1_9
{
    UINT32 ElementCount;
    WDI_SSID_OFFLOAD_CONTAINERv1_1_9* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_SSID_OFFLOAD_CONTAINERv1_1_9> ) == sizeof( struct ArrayOfElementsOfWDI_SSID_OFFLOAD_CONTAINERv1_1_9 ) );
#endif

typedef struct _WDI_NETWORK_LIST_OFFLOAD_INFO
{
    struct _WDI_NETWORK_LIST_OFFLOAD_INFO_Optional
    {
        UINT32 SsidOffload_IsPresent : 1;
#ifdef __cplusplus
        _WDI_NETWORK_LIST_OFFLOAD_INFO_Optional() : SsidOffload_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_NETWORK_LIST_OFFLOAD_CONFIG_CONTAINER NetworkListOffloadConfig;
#ifdef __cplusplus
    ArrayOfElements<WDI_SSID_OFFLOAD_CONTAINER> SsidOffload;
#else
    struct ArrayOfElementsOfWDI_SSID_OFFLOAD_CONTAINER SsidOffload;
#endif
} WDI_NETWORK_LIST_OFFLOAD_INFO, *PWDI_NETWORK_LIST_OFFLOAD_INFO;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_NETWORK_LIST_OFFLOAD_INFO * pField );
    }
}
#endif
typedef WDI_WAKE_EVENT_MASK WDI_WAKE_EVENT_MASK_CONTAINER;

typedef WDI_RECEIVE_COALESCING_CONFIG WDI_RECEIVE_COALESCING_CONFIG_CONTAINER;

typedef WDI_RECEIVE_COALESCING_FIELD WDI_RECEIVE_COALESCING_FIELD_CONTAINER;

struct ArrayOfElementsOfWDI_RECEIVE_COALESCING_FIELD_CONTAINER
{
    UINT32 ElementCount;
    WDI_RECEIVE_COALESCING_FIELD_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_RECEIVE_COALESCING_FIELD_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_RECEIVE_COALESCING_FIELD_CONTAINER ) );
#endif

typedef struct _ADD_RECEIVE_COALESCING_INFO
{
    struct _ADD_RECEIVE_COALESCING_INFO_Optional
    {
        UINT32 ReceiveCoalescingField_IsPresent : 1;
#ifdef __cplusplus
        _ADD_RECEIVE_COALESCING_INFO_Optional() : ReceiveCoalescingField_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_RECEIVE_COALESCING_CONFIG_CONTAINER ReceivingCoalesingConfig;
#ifdef __cplusplus
    ArrayOfElements<WDI_RECEIVE_COALESCING_FIELD_CONTAINER> ReceiveCoalescingField;
#else
    struct ArrayOfElementsOfWDI_RECEIVE_COALESCING_FIELD_CONTAINER ReceiveCoalescingField;
#endif
} ADD_RECEIVE_COALESCING_INFO, *PADD_RECEIVE_COALESCING_INFO;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ ADD_RECEIVE_COALESCING_INFO * pField );
    }
}
#endif

typedef struct _WDI_PACKET_PATTERN_CONTAINER
{
    UINT32_CONTAINER WakePacketPatternId;
    WDI_BYTE_BLOB PacketPattern;
    WDI_BYTE_BLOB PacketPatternMask;
#ifdef __cplusplus
    _WDI_PACKET_PATTERN_CONTAINER() : WakePacketPatternId( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_PACKET_PATTERN_CONTAINER, *PWDI_PACKET_PATTERN_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_PACKET_PATTERN_CONTAINER * pField );
    }
}
#endif
typedef WDI_IPv4_TCP_SYNC WDI_IPv4_TCP_SYNC_CONTAINER;

typedef WDI_IPv6TCP_SYNC WDI_IPv6TCP_SYNC_CONTAINER;

typedef struct _WDI_P2P_CHANNEL_ENTRIES_CONTAINER
{
    UINT8_CONTAINER OperatingClass;
    WDI_CHANNEL_LIST_CONTAINER ChannelList;
#ifdef __cplusplus
    _WDI_P2P_CHANNEL_ENTRIES_CONTAINER() : OperatingClass( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_P2P_CHANNEL_ENTRIES_CONTAINER, *PWDI_P2P_CHANNEL_ENTRIES_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_CHANNEL_ENTRIES_CONTAINER * pField );
    }
}
#endif
struct ArrayOfElementsOfWDI_P2P_CHANNEL_ENTRIES_CONTAINER
{
    UINT32 ElementCount;
    WDI_P2P_CHANNEL_ENTRIES_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_P2P_CHANNEL_ENTRIES_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_P2P_CHANNEL_ENTRIES_CONTAINER ) );
#endif

typedef struct _WDI_P2P_CHANNEL_LIST_CONTAINER
{
    WDI_COUNTRY_REGION_LIST_CONTAINER CountryRegionList;
#ifdef __cplusplus
    ArrayOfElements<WDI_P2P_CHANNEL_ENTRIES_CONTAINER> ChannelEntries;
#else
    struct ArrayOfElementsOfWDI_P2P_CHANNEL_ENTRIES_CONTAINER ChannelEntries;
#endif
} WDI_P2P_CHANNEL_LIST_CONTAINER, *PWDI_P2P_CHANNEL_LIST_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_P2P_CHANNEL_LIST_CONTAINER * pField );
    }
}
#endif
typedef WDI_GUID WDI_GUID_CONTAINER;

struct ArrayOfElementsOfGUID
{
    UINT32 ElementCount;
    GUID* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<GUID> ) == sizeof( struct ArrayOfElementsOfGUID ) );
#endif
#ifdef __cplusplus
typedef ArrayOfElements<GUID> WDI_GUID_LIST_CONTAINER;
#else
typedef struct ArrayOfElementsOfGUID WDI_GUID_LIST_CONTAINER;
#endif

typedef ULONG TEST_CONTAINER;

struct ArrayOfElementsOfTEST_CONTAINER
{
    UINT32 ElementCount;
    TEST_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<TEST_CONTAINER> ) == sizeof( struct ArrayOfElementsOfTEST_CONTAINER ) );
#endif

typedef struct _TEST_AGGREGATE_CONTAINER
{
#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> TestField;
#else
    struct ArrayOfElementsOfTEST_CONTAINER TestField;
#endif
} TEST_AGGREGATE_CONTAINER, *PTEST_AGGREGATE_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ TEST_AGGREGATE_CONTAINER * pField );
    }
}
#endif

typedef struct _WDI_FTM_TARGET_BSS_ENTRY_CONTAINER
{
    struct _WDI_FTM_TARGET_BSS_ENTRY_CONTAINER_Optional
    {
        UINT32 ProbeResponseFrame_IsPresent : 1;
        UINT32 BeaconFrame_IsPresent : 1;
        UINT32 DeviceSpecificContext_IsPresent : 1;
#ifdef __cplusplus
        _WDI_FTM_TARGET_BSS_ENTRY_CONTAINER_Optional() : ProbeResponseFrame_IsPresent( FALSE ), BeaconFrame_IsPresent( FALSE ), DeviceSpecificContext_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER BSSID;
    WDI_BYTE_BLOB ProbeResponseFrame;
    WDI_BYTE_BLOB BeaconFrame;
    WDI_SIGNAL_INFO_CONTAINER SignalInfo;
    WDI_CHANNEL_INFO_CONTAINER ChannelInfo;
    WDI_BYTE_BLOB DeviceSpecificContext;
    BOOL_CONTAINER RequestLCIReport;
#ifdef __cplusplus
    _WDI_FTM_TARGET_BSS_ENTRY_CONTAINER() : RequestLCIReport( (BOOL_CONTAINER)0 )
    {
        memset( &BSSID, 0, sizeof( BSSID ) );
    };
#endif
} WDI_FTM_TARGET_BSS_ENTRY_CONTAINER, *PWDI_FTM_TARGET_BSS_ENTRY_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_FTM_TARGET_BSS_ENTRY_CONTAINER * pField );
    }
}
#endif
typedef WDI_FTM_RESPONSE_STATUS WDI_FTM_RESPONSE_STATUS_CONTAINER;

typedef WDI_FTM_BANDWIDTH WDI_FTM_BANDWIDTH_USED_CONTAINER;

typedef WDI_FTM_PROPAGATION WDI_FTM_PROPAGATION_PROPERTY_CONTAINER;

typedef WDI_LCI_REPORT_STATUS WDI_LCI_REPORT_STATUS_CONTAINER;

typedef struct _WDI_FTM_RESPONSE_CONTAINER
{
    struct _WDI_FTM_RESPONSE_CONTAINER_Optional
    {
        UINT32 RetryAfter_IsPresent : 1;
        UINT32 NumberOfMeasurements_IsPresent : 1;
        UINT32 SignalInfo_IsPresent : 1;
        UINT32 BandwidthUsed_IsPresent : 1;
        UINT32 RTT_IsPresent : 1;
        UINT32 RTTVariance_IsPresent : 1;
        UINT32 PropagationProperty_IsPresent : 1;
        UINT32 LCIStatus_IsPresent : 1;
        UINT32 LCIReport_IsPresent : 1;
#ifdef __cplusplus
        _WDI_FTM_RESPONSE_CONTAINER_Optional() : RetryAfter_IsPresent( FALSE ), NumberOfMeasurements_IsPresent( FALSE ), SignalInfo_IsPresent( FALSE ), BandwidthUsed_IsPresent( FALSE ), RTT_IsPresent( FALSE ), RTTVariance_IsPresent( FALSE ), PropagationProperty_IsPresent( FALSE ), LCIStatus_IsPresent( FALSE ), LCIReport_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER BSSID;
    WDI_FTM_RESPONSE_STATUS_CONTAINER Status;
    UINT16_CONTAINER RetryAfter;
    UINT16_CONTAINER NumberOfMeasurements;
    WDI_SIGNAL_INFO_CONTAINER SignalInfo;
    WDI_FTM_BANDWIDTH_USED_CONTAINER BandwidthUsed;
    INT32_CONTAINER RTT;
    UINT64_CONTAINER RTTVariance;
    WDI_FTM_PROPAGATION_PROPERTY_CONTAINER PropagationProperty;
    WDI_LCI_REPORT_STATUS_CONTAINER LCIStatus;
    WDI_BYTE_BLOB LCIReport;
#ifdef __cplusplus
    _WDI_FTM_RESPONSE_CONTAINER() : Status( (WDI_FTM_RESPONSE_STATUS_CONTAINER)0 ), RetryAfter( (UINT16_CONTAINER)0 ), NumberOfMeasurements( (UINT16_CONTAINER)0 ), BandwidthUsed( (WDI_FTM_BANDWIDTH_USED_CONTAINER)0 ), RTT( (INT32_CONTAINER)0 ), RTTVariance( (UINT64_CONTAINER)0 ), PropagationProperty( (WDI_FTM_PROPAGATION_PROPERTY_CONTAINER)0 ), LCIStatus( (WDI_LCI_REPORT_STATUS_CONTAINER)0 )
    {
        memset( &BSSID, 0, sizeof( BSSID ) );
    };
#endif
} WDI_FTM_RESPONSE_CONTAINER, *PWDI_FTM_RESPONSE_CONTAINER;
#ifdef __cplusplus
namespace WDI_TLV
{
    namespace PARSER
    {
        void MarkArrayOfElementFieldsAsCopied( _Inout_ WDI_FTM_RESPONSE_CONTAINER * pField );
    }
}
#endif

typedef EmptyMessageStructureType WDI_TASK_OPEN_PARAMETERS, *PWDI_TASK_OPEN_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_CLOSE_PARAMETERS, *PWDI_TASK_CLOSE_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_HOST_DETECT_ERROR_PARAMETERS, *PWDI_SET_HOST_DETECT_ERROR_PARAMETERS;

struct ArrayOfElementsOfWDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER
{
    UINT32 ElementCount;
    WDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER ) );
#endif

typedef struct _WDI_SCAN_PARAMETERS
{
    struct _WDI_SCAN_PARAMETERS_Optional
    {
        UINT32 VendorIEs_IsPresent : 1;
        UINT32 BandChannelList_IsPresent : 1;
        UINT32 SixGHzBandChannelList_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SCAN_PARAMETERS_Optional() : VendorIEs_IsPresent( FALSE ), BandChannelList_IsPresent( FALSE ), SixGHzBandChannelList_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER BSSID;
#ifdef __cplusplus
    ArrayOfElements<WDI_SSID> SSIDList;
#else
    struct ArrayOfElementsOfWDI_SSID SSIDList;
#endif
    WDI_BYTE_BLOB VendorIEs;
    WDI_SCAN_PARAMETERS_CONTAINER ScanModeParameters;
    WDI_SCAN_DWELL_TIME_CONTAINER DwellTime;
#ifdef __cplusplus
    ArrayOfElements<WDI_SCAN_BAND_CHANNEL_LIST_CONTAINER> BandChannelList;
#else
    struct ArrayOfElementsOfWDI_SCAN_BAND_CHANNEL_LIST_CONTAINER BandChannelList;
#endif
#ifdef __cplusplus
    ArrayOfElements<WDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER> SixGHzBandChannelList;
#else
    struct ArrayOfElementsOfWDI_6_GHZ_BAND_CHANNEL_LIST_CONTAINER SixGHzBandChannelList;
#endif
#ifdef __cplusplus
    _WDI_SCAN_PARAMETERS()
    {
        memset( &BSSID, 0, sizeof( BSSID ) );
    };
#endif
} WDI_SCAN_PARAMETERS, *PWDI_SCAN_PARAMETERS;

typedef EmptyMessageStructureType WDI_SCAN_RESULTS, *PWDI_SCAN_RESULTS;

struct ArrayOfElementsOfWDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER
{
    UINT32 ElementCount;
    WDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_P2P_SERVICE_NAME_HASH_CONTAINER
{
    UINT32 ElementCount;
    WDI_P2P_SERVICE_NAME_HASH_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_P2P_SERVICE_NAME_HASH_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_P2P_SERVICE_NAME_HASH_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER
{
    UINT32 ElementCount;
    WDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20
{
    UINT32 ElementCount;
    WDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20> ) == sizeof( struct ArrayOfElementsOfWDI_P2P_ASP2_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINERV1_0_20 ) );
#endif

typedef struct _WDI_TASK_P2P_DISCOVER_PARAMETERS
{
    struct _WDI_TASK_P2P_DISCOVER_PARAMETERS_Optional
    {
        UINT32 DiscoveryChannelSettings_IsPresent : 1;
        UINT32 SSIDList_IsPresent : 1;
        UINT32 ServiceNameHash_IsPresent : 1;
        UINT32 VendorIEs_IsPresent : 1;
        UINT32 ServiceInformationDiscoveryEntry_IsPresent : 1;
        UINT32 bIncludeListenChannel_IsPresent : 1;
#ifdef __cplusplus
        _WDI_TASK_P2P_DISCOVER_PARAMETERS_Optional() : DiscoveryChannelSettings_IsPresent( FALSE ), SSIDList_IsPresent( FALSE ), ServiceNameHash_IsPresent( FALSE ), VendorIEs_IsPresent( FALSE ), ServiceInformationDiscoveryEntry_IsPresent( FALSE ), bIncludeListenChannel_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_DISCOVER_MODE_CONTAINER DiscoverMode;
    WDI_SCAN_DWELL_TIME_CONTAINER DwellTime;
#ifdef __cplusplus
    ArrayOfElements<WDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER> DiscoveryChannelSettings;
#else
    struct ArrayOfElementsOfWDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER DiscoveryChannelSettings;
#endif
#ifdef __cplusplus
    ArrayOfElements<WDI_SSID> SSIDList;
#else
    struct ArrayOfElementsOfWDI_SSID SSIDList;
#endif
#ifdef __cplusplus
    ArrayOfElements<WDI_P2P_SERVICE_NAME_HASH_CONTAINER> ServiceNameHash;
#else
    struct ArrayOfElementsOfWDI_P2P_SERVICE_NAME_HASH_CONTAINER ServiceNameHash;
#endif
    WDI_BYTE_BLOB VendorIEs;
#ifdef __cplusplus
    ArrayOfElements<WDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER> ServiceInformationDiscoveryEntry;
#else
    struct ArrayOfElementsOfWDI_P2P_SERVICE_INFORMATION_DISCOVERY_ENTRY_CONTAINER ServiceInformationDiscoveryEntry;
#endif
    BOOL_CONTAINER bIncludeListenChannel;
#ifdef __cplusplus
    _WDI_TASK_P2P_DISCOVER_PARAMETERS() : bIncludeListenChannel( (BOOL_CONTAINER)0 )
    {
    };
#endif
} WDI_TASK_P2P_DISCOVER_PARAMETERS, *PWDI_TASK_P2P_DISCOVER_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_P2P_DISCOVER_RESULTS, *PWDI_TASK_P2P_DISCOVER_RESULTS;

typedef struct _WDI_SET_P2P_START_BACKGROUND_DISCOVERY_PARAMETERS
{
    struct _WDI_SET_P2P_START_BACKGROUND_DISCOVERY_PARAMETERS_Optional
    {
        UINT32 DiscoveryChannelSettings_IsPresent : 1;
        UINT32 DeviceFilterList_IsPresent : 1;
        UINT32 ServiceNameHash_IsPresent : 1;
        UINT32 VendorIEs_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_P2P_START_BACKGROUND_DISCOVERY_PARAMETERS_Optional() : DiscoveryChannelSettings_IsPresent( FALSE ), DeviceFilterList_IsPresent( FALSE ), ServiceNameHash_IsPresent( FALSE ), VendorIEs_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_BACKGROUND_DISCOVER_MODE_CONTAINER DiscoverMode;
#ifdef __cplusplus
    ArrayOfElements<WDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER> DiscoveryChannelSettings;
#else
    struct ArrayOfElementsOfWDI_P2P_DISCOVERY_CHANNEL_SETTINGS_CONTAINER DiscoveryChannelSettings;
#endif
    WDI_ADDRESS_LIST_CONTAINER DeviceFilterList;
#ifdef __cplusplus
    ArrayOfElements<WDI_P2P_SERVICE_NAME_HASH_CONTAINER> ServiceNameHash;
#else
    struct ArrayOfElementsOfWDI_P2P_SERVICE_NAME_HASH_CONTAINER ServiceNameHash;
#endif
    WDI_BYTE_BLOB VendorIEs;
} WDI_SET_P2P_START_BACKGROUND_DISCOVERY_PARAMETERS, *PWDI_SET_P2P_START_BACKGROUND_DISCOVERY_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_P2P_START_BACKGROUND_DISCOVERY_RESULTS, *PWDI_SET_P2P_START_BACKGROUND_DISCOVERY_RESULTS;

typedef EmptyMessageStructureType WDI_SET_P2P_STOP_BACKGROUND_DISCOVERY_PARAMETERS, *PWDI_SET_P2P_STOP_BACKGROUND_DISCOVERY_PARAMETERS;

struct ArrayOfElementsOfWDI_CONNECT_BSS_ENTRY_CONTAINER
{
    UINT32 ElementCount;
    WDI_CONNECT_BSS_ENTRY_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_CONNECT_BSS_ENTRY_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_CONNECT_BSS_ENTRY_CONTAINER ) );
#endif

typedef struct _WDI_TASK_CONNECT_PARAMETERS
{
    WDI_CONNECT_PARAMETERS_CONTAINER ConnectParameters;
#ifdef __cplusplus
    ArrayOfElements<WDI_CONNECT_BSS_ENTRY_CONTAINER> PreferredBSSEntryList;
#else
    struct ArrayOfElementsOfWDI_CONNECT_BSS_ENTRY_CONTAINER PreferredBSSEntryList;
#endif
} WDI_TASK_CONNECT_PARAMETERS, *PWDI_TASK_CONNECT_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_CONNECT_RESULTS, *PWDI_TASK_CONNECT_RESULTS;

typedef struct _WDI_TASK_DOT11_RESET_PARAMETERS
{
    struct _WDI_TASK_DOT11_RESET_PARAMETERS_Optional
    {
        UINT32 ResetMACAddress_IsPresent : 1;
        UINT32 ResetMloLinkMACAddresses_IsPresent : 1;
#ifdef __cplusplus
        _WDI_TASK_DOT11_RESET_PARAMETERS_Optional() : ResetMACAddress_IsPresent( FALSE ), ResetMloLinkMACAddresses_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_DOT11_RESET_PARAMETERS_CONTAINER Dot11ResetParameters;
    WDI_MAC_ADDRESS_CONTAINER ResetMACAddress;
    WDI_ADDRESS_LIST_CONTAINER ResetMloLinkMACAddresses;
#ifdef __cplusplus
    _WDI_TASK_DOT11_RESET_PARAMETERS()
    {
        memset( &ResetMACAddress, 0, sizeof( ResetMACAddress ) );
    };
#endif
} WDI_TASK_DOT11_RESET_PARAMETERS, *PWDI_TASK_DOT11_RESET_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_DOT11_RESET_RESULTS, *PWDI_TASK_DOT11_RESET_RESULTS;

typedef struct _WDI_TASK_DISCONNECT_PARAMETERS
{
    WDI_DISCONNECT_PARAMETERS_CONTAINER DisconnectParameters;
} WDI_TASK_DISCONNECT_PARAMETERS, *PWDI_TASK_DISCONNECT_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_DISCONNECT_RESULTS, *PWDI_TASK_DISCONNECT_RESULTS;

typedef struct _WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_PARAMETERS
{
    struct _WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_PARAMETERS_Optional
    {
        UINT32 GONegotiationRequestInfo_IsPresent : 1;
        UINT32 InvitationRequestInfo_IsPresent : 1;
        UINT32 ProvisionDiscoveryRequestInfo_IsPresent : 1;
        UINT32 VendorIEs_IsPresent : 1;
#ifdef __cplusplus
        _WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_PARAMETERS_Optional() : GONegotiationRequestInfo_IsPresent( FALSE ), InvitationRequestInfo_IsPresent( FALSE ), ProvisionDiscoveryRequestInfo_IsPresent( FALSE ), VendorIEs_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_SEND_ACTION_FRAME_REQUEST_PARAMETERS_CONTAINER RequestParams;
    WDI_P2P_GO_NEGOTIATION_REQUEST_INFO_CONTAINER GONegotiationRequestInfo;
    WDI_P2P_INVITATION_REQUEST_INFO_CONTAINER InvitationRequestInfo;
    WDI_P2P_PROVISION_DISCOVERY_REQUEST_INFO_CONTAINER ProvisionDiscoveryRequestInfo;
    WDI_BSS_ENTRY_CONTAINER DeviceDescriptor;
    WDI_BYTE_BLOB VendorIEs;
} WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_PARAMETERS, *PWDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_RESULTS, *PWDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_RESULTS;

typedef struct _WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_PARAMETERS
{
    struct _WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_PARAMETERS_Optional
    {
        UINT32 GONegotiationResponseInfo_IsPresent : 1;
        UINT32 GONegotiationConfirmationInfo_IsPresent : 1;
        UINT32 InvitationResponseInfo_IsPresent : 1;
        UINT32 ProvisionDiscoveryResponseInfo_IsPresent : 1;
        UINT32 VendorIEs_IsPresent : 1;
#ifdef __cplusplus
        _WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_PARAMETERS_Optional() : GONegotiationResponseInfo_IsPresent( FALSE ), GONegotiationConfirmationInfo_IsPresent( FALSE ), InvitationResponseInfo_IsPresent( FALSE ), ProvisionDiscoveryResponseInfo_IsPresent( FALSE ), VendorIEs_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_SEND_ACTION_FRAME_RESPONSE_PARAMETERS_CONTAINER ResponseParams;
    WDI_P2P_GO_NEGOTIATION_RESPONSE_INFO_CONTAINER GONegotiationResponseInfo;
    WDI_P2P_GO_NEGOTIATION_CONFIRMATION_INFO_CONTAINER GONegotiationConfirmationInfo;
    WDI_P2P_INVITATION_RESPONSE_INFO_CONTAINER InvitationResponseInfo;
    WDI_P2P_PROVISION_DISCOVERY_RESPONSE_INFO_CONTAINER ProvisionDiscoveryResponseInfo;
    WDI_INCOMING_P2P_ACTION_FRAME_INFO PrecedingFrameInformation;
    WDI_BYTE_BLOB VendorIEs;
} WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_PARAMETERS, *PWDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_RESULTS, *PWDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_RESULTS;

typedef struct _WDI_SET_RADIO_STATE_PARAMETERS
{
    UINT8_CONTAINER SoftwareRadioState;
#ifdef __cplusplus
    _WDI_SET_RADIO_STATE_PARAMETERS() : SoftwareRadioState( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_RADIO_STATE_PARAMETERS, *PWDI_SET_RADIO_STATE_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_RADIO_STATE_RESULTS, *PWDI_SET_RADIO_STATE_RESULTS;

typedef struct _WDI_TASK_CREATE_PORT_PARAMETERS
{
    struct _WDI_TASK_CREATE_PORT_PARAMETERS_Optional
    {
        UINT32 MacAddress_IsPresent : 1;
#ifdef __cplusplus
        _WDI_TASK_CREATE_PORT_PARAMETERS_Optional() : MacAddress_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_TASK_CREATE_PORT_PARAMETERS_CONTAINER CreatePortParameters;
    WDI_MAC_ADDRESS_CONTAINER MacAddress;
#ifdef __cplusplus
    _WDI_TASK_CREATE_PORT_PARAMETERS()
    {
        memset( &MacAddress, 0, sizeof( MacAddress ) );
    };
#endif
} WDI_TASK_CREATE_PORT_PARAMETERS, *PWDI_TASK_CREATE_PORT_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_CREATE_PORT_RESULTS, *PWDI_TASK_CREATE_PORT_RESULTS;

typedef struct _WDI_TASK_DELETE_PORT_PARAMETERS
{
    WDI_TASK_DELETE_PORT_PARAMETERS_CONTAINER DeletePortParameters;
} WDI_TASK_DELETE_PORT_PARAMETERS, *PWDI_TASK_DELETE_PORT_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_DELETE_PORT_RESULTS, *PWDI_TASK_DELETE_PORT_RESULTS;

struct ArrayOfElementsOfWDI_AP_BAND_CHANNEL_LIST_CONTAINER
{
    UINT32 ElementCount;
    WDI_AP_BAND_CHANNEL_LIST_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_AP_BAND_CHANNEL_LIST_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_AP_BAND_CHANNEL_LIST_CONTAINER ) );
#endif

typedef struct _WDI_TASK_START_AP_PARAMETERS
{
    struct _WDI_TASK_START_AP_PARAMETERS_Optional
    {
        UINT32 AdvertisedOperatingChannel_IsPresent : 1;
        UINT32 APBandChannelList_IsPresent : 1;
#ifdef __cplusplus
        _WDI_TASK_START_AP_PARAMETERS_Optional() : AdvertisedOperatingChannel_IsPresent( FALSE ), APBandChannelList_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_SSID DesiredSSID;
    WDI_START_AP_PARAMETERS_CONTAINER StaticParameters;
    WDI_AUTH_ALGORITHM_LIST_CONTAINER AuthenticationAlgorithms;
    WDI_CIPHER_ALGORITHM_LIST_CONTAINER MulticastCipherAlgorithms;
    WDI_CIPHER_ALGORITHM_LIST_CONTAINER UnicastCipherAlgorithms;
    WDI_P2P_CHANNEL_CONTAINER AdvertisedOperatingChannel;
#ifdef __cplusplus
    ArrayOfElements<WDI_AP_BAND_CHANNEL_LIST_CONTAINER> APBandChannelList;
#else
    struct ArrayOfElementsOfWDI_AP_BAND_CHANNEL_LIST_CONTAINER APBandChannelList;
#endif
} WDI_TASK_START_AP_PARAMETERS, *PWDI_TASK_START_AP_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_START_AP_RESULTS, *PWDI_TASK_START_AP_RESULTS;

typedef EmptyMessageStructureType WDI_TASK_STOP_AP_PARAMETERS, *PWDI_TASK_STOP_AP_PARAMETERS;

typedef struct _WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_PARAMETERS
{
    struct _WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_PARAMETERS_Optional
    {
        UINT32 VendorIEs_IsPresent : 1;
        UINT32 WFDAssociationStatus_IsPresent : 1;
#ifdef __cplusplus
        _WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_PARAMETERS_Optional() : VendorIEs_IsPresent( FALSE ), WFDAssociationStatus_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_AP_ASSOCIATION_RESPONSE_PARAMETERS_CONTAINER AssocResponseParameters;
    WDI_BYTE_BLOB VendorIEs;
    WDI_INCOMING_ASSOCIATION_REQUEST_INFO_CONTAINER IncomingRequestInfo;
    UINT8_CONTAINER WFDAssociationStatus;
#ifdef __cplusplus
    _WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_PARAMETERS() : WFDAssociationStatus( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_PARAMETERS, *PWDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_RESULTS, *PWDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_RESULTS;

typedef EmptyMessageStructureType WDI_TASK_SET_READY_TO_RECEIVE_INDICATIONS_PARAMETERS, *PWDI_TASK_SET_READY_TO_RECEIVE_INDICATIONS_PARAMETERS;

typedef struct _WDI_SET_POWER_PARAMETERS
{
    struct _WDI_SET_POWER_PARAMETERS_Optional
    {
        UINT32 WakeEventMask_IsPresent : 1;
        UINT32 SetPowerDxReason_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_POWER_PARAMETERS_Optional() : WakeEventMask_IsPresent( FALSE ), SetPowerDxReason_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    UINT32_CONTAINER PowerState;
    WDI_WAKE_EVENT_MASK_CONTAINER WakeEventMask;
    UINT32_CONTAINER SetPowerDxReason;
#ifdef __cplusplus
    _WDI_SET_POWER_PARAMETERS() : PowerState( (UINT32_CONTAINER)0 ), SetPowerDxReason( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_POWER_PARAMETERS, *PWDI_SET_POWER_PARAMETERS;

typedef struct _WDI_SET_POWER_RESULTS
{
    struct _WDI_SET_POWER_RESULTS_Optional
    {
        UINT32 bResumeRequired_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_POWER_RESULTS_Optional() : bResumeRequired_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    BOOL_CONTAINER bResumeRequired;
#ifdef __cplusplus
    _WDI_SET_POWER_RESULTS() : bResumeRequired( (BOOL_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_POWER_RESULTS, *PWDI_SET_POWER_RESULTS;

typedef EmptyMessageStructureType WDI_INDICATION_DISCONNECT_COMPLETE_PARAMETERS, *PWDI_INDICATION_DISCONNECT_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_SET_RADIO_STATE_COMPLETE_PARAMETERS, *PWDI_INDICATION_SET_RADIO_STATE_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_SET_READY_TO_RECEIVE_INDICATIONS_COMPLETE_PARAMETERS, *PWDI_INDICATION_SET_READY_TO_RECEIVE_INDICATIONS_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_OPERATION_MODE_PARAMETERS, *PWDI_SET_OPERATION_MODE_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_END_DWELL_TIME_PARAMETERS, *PWDI_SET_END_DWELL_TIME_PARAMETERS;

typedef struct _WDI_SET_P2P_LISTEN_STATE_PARAMETERS
{
    struct _WDI_SET_P2P_LISTEN_STATE_PARAMETERS_Optional
    {
        UINT32 ListenChannel_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_P2P_LISTEN_STATE_PARAMETERS_Optional() : ListenChannel_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_P2P_LISTEN_STATE_CONTAINER ListenState;
    WDI_P2P_CHANNEL_CONTAINER ListenChannel;
    WDI_P2P_LISTEN_DURATION_CONTAINER ListenDuration;
#ifdef __cplusplus
    _WDI_SET_P2P_LISTEN_STATE_PARAMETERS() : ListenState( (WDI_P2P_LISTEN_STATE_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_P2P_LISTEN_STATE_PARAMETERS, *PWDI_SET_P2P_LISTEN_STATE_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_P2P_LISTEN_STATE_RESULTS, *PWDI_SET_P2P_LISTEN_STATE_RESULTS;

struct ArrayOfElementsOfWDI_PRIVACY_EXEMPTION_LIST_CONTAINER
{
    UINT32 ElementCount;
    WDI_PRIVACY_EXEMPTION_LIST_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_PRIVACY_EXEMPTION_LIST_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_PRIVACY_EXEMPTION_LIST_CONTAINER ) );
#endif

typedef struct _WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS
{
    struct _WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS_Optional
    {
        UINT32 PrivacyExemptionEntry_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS_Optional() : PrivacyExemptionEntry_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

#ifdef __cplusplus
    ArrayOfElements<WDI_PRIVACY_EXEMPTION_LIST_CONTAINER> PrivacyExemptionEntry;
#else
    struct ArrayOfElementsOfWDI_PRIVACY_EXEMPTION_LIST_CONTAINER PrivacyExemptionEntry;
#endif
} WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS, *PWDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_PRIVACY_EXEMPTION_LIST_RESULTS, *PWDI_SET_PRIVACY_EXEMPTION_LIST_RESULTS;

struct ArrayOfElementsOfWDI_SET_ADD_CIPHER_KEYS_CONTAINER
{
    UINT32 ElementCount;
    WDI_SET_ADD_CIPHER_KEYS_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_SET_ADD_CIPHER_KEYS_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_SET_ADD_CIPHER_KEYS_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1
{
    UINT32 ElementCount;
    WDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1> ) == sizeof( struct ArrayOfElementsOfWDI_SET_ADD_CIPHER_KEYS_CONTAINER_V1 ) );
#endif

typedef struct _WDI_SET_ADD_CIPHER_KEYS_PARAMETERS
{
#ifdef __cplusplus
    ArrayOfElements<WDI_SET_ADD_CIPHER_KEYS_CONTAINER> SetCipherKey;
#else
    struct ArrayOfElementsOfWDI_SET_ADD_CIPHER_KEYS_CONTAINER SetCipherKey;
#endif
} WDI_SET_ADD_CIPHER_KEYS_PARAMETERS, *PWDI_SET_ADD_CIPHER_KEYS_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_ADD_CIPHER_KEYS_RESULTS, *PWDI_SET_ADD_CIPHER_KEYS_RESULTS;

struct ArrayOfElementsOfWDI_SET_DELETE_CIPHER_KEYS_CONTAINER
{
    UINT32 ElementCount;
    WDI_SET_DELETE_CIPHER_KEYS_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_SET_DELETE_CIPHER_KEYS_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_SET_DELETE_CIPHER_KEYS_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_SET_DELETE_CIPHER_KEYS_CONTAINER_V1
{
    UINT32 ElementCount;
    WDI_SET_DELETE_CIPHER_KEYS_CONTAINER_V1* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_SET_DELETE_CIPHER_KEYS_CONTAINER_V1> ) == sizeof( struct ArrayOfElementsOfWDI_SET_DELETE_CIPHER_KEYS_CONTAINER_V1 ) );
#endif

typedef struct _WDI_SET_DELETE_CIPHER_KEYS_PARAMETERS
{
#ifdef __cplusplus
    ArrayOfElements<WDI_SET_DELETE_CIPHER_KEYS_CONTAINER> CipherKeyInfo;
#else
    struct ArrayOfElementsOfWDI_SET_DELETE_CIPHER_KEYS_CONTAINER CipherKeyInfo;
#endif
} WDI_SET_DELETE_CIPHER_KEYS_PARAMETERS, *PWDI_SET_DELETE_CIPHER_KEYS_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_DELETE_CIPHER_KEYS_RESULTS, *PWDI_SET_DELETE_CIPHER_KEYS_RESULTS;

typedef struct _WDI_SET_DEFAULT_KEY_ID_PARAMETERS
{
    WDI_SET_DEFAULT_KEY_ID_CONTAINER DefaultKeyIdParameters;
} WDI_SET_DEFAULT_KEY_ID_PARAMETERS, *PWDI_SET_DEFAULT_KEY_ID_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_DEFAULT_KEY_ID_RESULTS, *PWDI_SET_DEFAULT_KEY_ID_RESULTS;

typedef struct _WDI_SET_CONNECTION_QUALITY_PARAMETERS
{
    struct _WDI_SET_CONNECTION_QUALITY_PARAMETERS_Optional
    {
        UINT32 LowLatencyParameters_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_CONNECTION_QUALITY_PARAMETERS_Optional() : LowLatencyParameters_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_CONNECTION_QUALITY_CONTAINER Quality;
    WDI_LOW_LATENCY_CONNECTION_QUALITY_PARAMETERS_CONTAINER LowLatencyParameters;
#ifdef __cplusplus
    _WDI_SET_CONNECTION_QUALITY_PARAMETERS() : Quality( (WDI_CONNECTION_QUALITY_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_CONNECTION_QUALITY_PARAMETERS, *PWDI_SET_CONNECTION_QUALITY_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_CONNECTION_QUALITY_RESULTS, *PWDI_SET_CONNECTION_QUALITY_RESULTS;

typedef EmptyMessageStructureType WDI_GET_STATISTICS_INPUTS, *PWDI_GET_STATISTICS_INPUTS;

struct ArrayOfElementsOfWDI_MAC_STATISTICS_CONTAINER
{
    UINT32 ElementCount;
    WDI_MAC_STATISTICS_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_MAC_STATISTICS_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_MAC_STATISTICS_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_PHY_STATISTICS_CONTAINER
{
    UINT32 ElementCount;
    WDI_PHY_STATISTICS_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_PHY_STATISTICS_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_PHY_STATISTICS_CONTAINER ) );
#endif

typedef struct _WDI_GET_STATISTICS_PARAMETERS
{
#ifdef __cplusplus
    ArrayOfElements<WDI_MAC_STATISTICS_CONTAINER> PeerMACStatistics;
#else
    struct ArrayOfElementsOfWDI_MAC_STATISTICS_CONTAINER PeerMACStatistics;
#endif
#ifdef __cplusplus
    ArrayOfElements<WDI_PHY_STATISTICS_CONTAINER> PhyStatistics;
#else
    struct ArrayOfElementsOfWDI_PHY_STATISTICS_CONTAINER PhyStatistics;
#endif
} WDI_GET_STATISTICS_PARAMETERS, *PWDI_GET_STATISTICS_PARAMETERS;

typedef struct _WDI_SET_RECEIVE_PACKET_FILTER_PARAMETERS
{
    UINT32_CONTAINER PacketFilterType;
#ifdef __cplusplus
    _WDI_SET_RECEIVE_PACKET_FILTER_PARAMETERS() : PacketFilterType( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_RECEIVE_PACKET_FILTER_PARAMETERS, *PWDI_SET_RECEIVE_PACKET_FILTER_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_RECEIVE_PACKET_FILTER_RESULTS, *PWDI_SET_RECEIVE_PACKET_FILTER_RESULTS;

typedef EmptyMessageStructureType WDI_GET_ADAPTER_CAPABILITIES_INPUTS, *PWDI_GET_ADAPTER_CAPABILITIES_INPUTS;

struct ArrayOfElementsOfWDI_BAND_INFO_CONTAINER
{
    UINT32 ElementCount;
    WDI_BAND_INFO_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_BAND_INFO_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_BAND_INFO_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_PHY_INFO_CONTAINER
{
    UINT32 ElementCount;
    WDI_PHY_INFO_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_PHY_INFO_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_PHY_INFO_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_NDIS_GUID_CONTAINER
{
    UINT32 ElementCount;
    WDI_NDIS_GUID_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_NDIS_GUID_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_NDIS_GUID_CONTAINER ) );
#endif

typedef struct _WDI_GET_ADAPTER_CAPABILITIES_PARAMETERS
{
    struct _WDI_GET_ADAPTER_CAPABILITIES_PARAMETERS_Optional
    {
        UINT32 CommunicationAttributes_IsPresent : 1;
        UINT32 APAttributes_IsPresent : 1;
        UINT32 VirtualizationAttributes_IsPresent : 1;
        UINT32 P2PAttributes_IsPresent : 1;
        UINT32 DatapathAttributes_IsPresent : 1;
        UINT32 BandInfo_IsPresent : 1;
        UINT32 PhyInfo_IsPresent : 1;
        UINT32 PmCapabilities_IsPresent : 1;
        UINT32 CountryRegionList_IsPresent : 1;
        UINT32 ReceiveCoalescingCapabilities_IsPresent : 1;
        UINT32 TcpOffloadCapabilities_IsPresent : 1;
        UINT32 SupportedGuids_IsPresent : 1;
        UINT32 OsPowerMgmtFeatures_IsPresent : 1;
#ifdef __cplusplus
        _WDI_GET_ADAPTER_CAPABILITIES_PARAMETERS_Optional() : CommunicationAttributes_IsPresent( FALSE ), APAttributes_IsPresent( FALSE ), VirtualizationAttributes_IsPresent( FALSE ), P2PAttributes_IsPresent( FALSE ), DatapathAttributes_IsPresent( FALSE ), BandInfo_IsPresent( FALSE ), PhyInfo_IsPresent( FALSE ), PmCapabilities_IsPresent( FALSE ), CountryRegionList_IsPresent( FALSE ), ReceiveCoalescingCapabilities_IsPresent( FALSE ), TcpOffloadCapabilities_IsPresent( FALSE ), SupportedGuids_IsPresent( FALSE ), OsPowerMgmtFeatures_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_COMMUNICATION_ATTRIBUTES_CONTAINER CommunicationAttributes;
    WDI_INTERFACE_ATTRIBUTES_CONTAINER InterfaceAttributes;
    WDI_STATION_ATTRIBUTES_CONTAINER StationAttributes;
    WDI_AP_ATTRIBUTES_CONTAINER APAttributes;
    WDI_VIRTUALIZATION_ATTRIBUTES_CONTAINER VirtualizationAttributes;
    WDI_P2P_ATTRIBUTES_CONTAINER P2PAttributes;
    WDI_DATAPATH_ATTRIBUTES_CONTAINER DatapathAttributes;
#ifdef __cplusplus
    ArrayOfElements<WDI_BAND_INFO_CONTAINER> BandInfo;
#else
    struct ArrayOfElementsOfWDI_BAND_INFO_CONTAINER BandInfo;
#endif
#ifdef __cplusplus
    ArrayOfElements<WDI_PHY_INFO_CONTAINER> PhyInfo;
#else
    struct ArrayOfElementsOfWDI_PHY_INFO_CONTAINER PhyInfo;
#endif
    WDI_PM_CAPABILITIES_CONTAINER PmCapabilities;
    WDI_COUNTRY_REGION_LIST_CONTAINER CountryRegionList;
    WDI_RECEIVE_COALESCING_CAPABILITIES_CONTAINER ReceiveCoalescingCapabilities;
    WDI_TCP_OFFLOAD_CAPABILITIES_CONTAINER TcpOffloadCapabilities;
#ifdef __cplusplus
    ArrayOfElements<WDI_NDIS_GUID_CONTAINER> SupportedGuids;
#else
    struct ArrayOfElementsOfWDI_NDIS_GUID_CONTAINER SupportedGuids;
#endif
    WDI_OS_POWER_MGMT_FEATURES_CONTAINER OsPowerMgmtFeatures;
} WDI_GET_ADAPTER_CAPABILITIES_PARAMETERS, *PWDI_GET_ADAPTER_CAPABILITIES_PARAMETERS;

typedef struct _WDI_NETWORK_LIST_OFFLOAD_PARAMETERS
{
    WDI_NETWORK_LIST_OFFLOAD_INFO NetworkListOffload;
} WDI_NETWORK_LIST_OFFLOAD_PARAMETERS, *PWDI_NETWORK_LIST_OFFLOAD_PARAMETERS;

typedef EmptyMessageStructureType WDI_NETWORK_LIST_OFFLOAD_RESULTS, *PWDI_NETWORK_LIST_OFFLOAD_RESULTS;

typedef struct _WDI_SET_RECEIVE_COALESCING_PARAMETERS
{
    ADD_RECEIVE_COALESCING_INFO AddReceiveCoalescing;
} WDI_SET_RECEIVE_COALESCING_PARAMETERS, *PWDI_SET_RECEIVE_COALESCING_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_RECEIVE_COALESCING_RESULTS, *PWDI_SET_RECEIVE_COALESCING_RESULTS;

typedef struct _WDI_GET_BSS_ENTRY_LIST_UPDATE_PARAMETERS
{
    WDI_SSID SSID;
} WDI_GET_BSS_ENTRY_LIST_UPDATE_PARAMETERS, *PWDI_GET_BSS_ENTRY_LIST_UPDATE_PARAMETERS;

typedef EmptyMessageStructureType WDI_GET_BSS_ENTRY_LIST_UPDATE_RESULTS, *PWDI_GET_BSS_ENTRY_LIST_UPDATE_RESULTS;

typedef struct _WDI_SET_OWE_DH_IE_PARAMETERS
{
    WDI_BYTE_BLOB OWEDHIE;
} WDI_SET_OWE_DH_IE_PARAMETERS, *PWDI_SET_OWE_DH_IE_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_OWE_DH_IE_RESULTS, *PWDI_SET_OWE_DH_IE_RESULTS;

typedef struct _WDI_INDICATION_DISASSOCIATION_PARAMETERS
{
    struct _WDI_INDICATION_DISASSOCIATION_PARAMETERS_Optional
    {
        UINT32 DeauthFrame_IsPresent : 1;
        UINT32 DisassociationFrame_IsPresent : 1;
        UINT32 NeedPeerStateCleanup_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INDICATION_DISASSOCIATION_PARAMETERS_Optional() : DeauthFrame_IsPresent( FALSE ), DisassociationFrame_IsPresent( FALSE ), NeedPeerStateCleanup_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_DISCONNECT_INDICATION_PARAMETERS_CONTAINER DisconnectIndicationParameters;
    WDI_BYTE_BLOB DeauthFrame;
    WDI_BYTE_BLOB DisassociationFrame;
    BOOL_CONTAINER NeedPeerStateCleanup;
#ifdef __cplusplus
    _WDI_INDICATION_DISASSOCIATION_PARAMETERS() : NeedPeerStateCleanup( (BOOL_CONTAINER)0 )
    {
    };
#endif
} WDI_INDICATION_DISASSOCIATION_PARAMETERS, *PWDI_INDICATION_DISASSOCIATION_PARAMETERS;

typedef struct _WDI_INDICATION_ROAMING_NEEDED_PARAMETERS
{
    WDI_ROAMING_NEEDED_CONTAINER RoamingReason;
#ifdef __cplusplus
    _WDI_INDICATION_ROAMING_NEEDED_PARAMETERS() : RoamingReason( (WDI_ROAMING_NEEDED_CONTAINER)0 )
    {
    };
#endif
} WDI_INDICATION_ROAMING_NEEDED_PARAMETERS, *PWDI_INDICATION_ROAMING_NEEDED_PARAMETERS;

struct ArrayOfElementsOfWDI_LINK_INFO_CONTAINER
{
    UINT32 ElementCount;
    WDI_LINK_INFO_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_LINK_INFO_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_LINK_INFO_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_LINK_INFO_CONTAINER_V2_0_3
{
    UINT32 ElementCount;
    WDI_LINK_INFO_CONTAINER_V2_0_3* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_LINK_INFO_CONTAINER_V2_0_3> ) == sizeof( struct ArrayOfElementsOfWDI_LINK_INFO_CONTAINER_V2_0_3 ) );
#endif

typedef struct _WDI_INDICATION_LINK_STATE_CHANGE_PARAMETERS
{
    WDI_LINK_STATE_CHANGE_PARAMETERS_CONTAINER LinkStateChangeParameters;
#ifdef __cplusplus
    ArrayOfElements<WDI_LINK_INFO_CONTAINER> LinkInfo;
#else
    struct ArrayOfElementsOfWDI_LINK_INFO_CONTAINER LinkInfo;
#endif
} WDI_INDICATION_LINK_STATE_CHANGE_PARAMETERS, *PWDI_INDICATION_LINK_STATE_CHANGE_PARAMETERS;

typedef struct _WDI_INDICATION_P2P_ACTION_FRAME_RECEIVED_PARAMETERS
{
    WDI_INCOMING_P2P_ACTION_FRAME_INFO FrameInfo;
} WDI_INDICATION_P2P_ACTION_FRAME_RECEIVED_PARAMETERS, *PWDI_INDICATION_P2P_ACTION_FRAME_RECEIVED_PARAMETERS;

typedef struct _WDI_INDICATION_AP_ASSOCIATION_REQUEST_PARAMETERS
{
    WDI_INCOMING_ASSOCIATION_REQUEST_INFO_CONTAINER IncomingRequestInfo;
} WDI_INDICATION_AP_ASSOCIATION_REQUEST_PARAMETERS, *PWDI_INDICATION_AP_ASSOCIATION_REQUEST_PARAMETERS;

struct ArrayOfElementsOfWDI_BSS_ENTRY_CONTAINER
{
    UINT32 ElementCount;
    WDI_BSS_ENTRY_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_BSS_ENTRY_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_BSS_ENTRY_CONTAINER ) );
#endif

typedef struct _WDI_INDICATION_NLO_DISCOVERY_PARAMETERS
{
#ifdef __cplusplus
    ArrayOfElements<WDI_BSS_ENTRY_CONTAINER> DeviceDescriptor;
#else
    struct ArrayOfElementsOfWDI_BSS_ENTRY_CONTAINER DeviceDescriptor;
#endif
} WDI_INDICATION_NLO_DISCOVERY_PARAMETERS, *PWDI_INDICATION_NLO_DISCOVERY_PARAMETERS;

typedef struct _WDI_INDICATION_WAKE_REASON_PARAMETERS
{
    struct _WDI_INDICATION_WAKE_REASON_PARAMETERS_Optional
    {
        UINT32 WakePacket_IsPresent : 1;
        UINT32 WakePacketPatternId_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INDICATION_WAKE_REASON_PARAMETERS_Optional() : WakePacket_IsPresent( FALSE ), WakePacketPatternId_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    UINT32_CONTAINER WakeEventCode;
    WDI_BYTE_BLOB WakePacket;
    UINT32_CONTAINER WakePacketPatternId;
#ifdef __cplusplus
    _WDI_INDICATION_WAKE_REASON_PARAMETERS() : WakeEventCode( (UINT32_CONTAINER)0 ), WakePacketPatternId( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_INDICATION_WAKE_REASON_PARAMETERS, *PWDI_INDICATION_WAKE_REASON_PARAMETERS;

typedef struct _WDI_INDICATION_TKIP_MIC_FAILURE_PARAMETERS
{
    WDI_TKIP_MIC_FAILURE_PARAMETERS_CONTAINER FailureInfo;
} WDI_INDICATION_TKIP_MIC_FAILURE_PARAMETERS, *PWDI_INDICATION_TKIP_MIC_FAILURE_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_SCAN_COMPLETE_PARAMETERS, *PWDI_INDICATION_SCAN_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_P2P_DISCOVERY_COMPLETE_PARAMETERS, *PWDI_INDICATION_P2P_DISCOVERY_COMPLETE_PARAMETERS;

typedef struct _WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS
{
    struct _WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS_Optional
    {
        UINT32 DeviceDescriptor_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS_Optional() : DeviceDescriptor_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

#ifdef __cplusplus
    ArrayOfElements<WDI_BSS_ENTRY_CONTAINER> DeviceDescriptor;
#else
    struct ArrayOfElementsOfWDI_BSS_ENTRY_CONTAINER DeviceDescriptor;
#endif
} WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS, *PWDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_DOT11_RESET_COMPLETE_PARAMETERS, *PWDI_INDICATION_DOT11_RESET_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_CONNECT_COMPLETE_PARAMETERS, *PWDI_INDICATION_CONNECT_COMPLETE_PARAMETERS;

typedef struct _WDI_INDICATION_P2P_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS
{
    WDI_P2P_SEND_ACTION_FRAME_RESULT_CONTAINER SendActionFrameResult;
} WDI_INDICATION_P2P_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS, *PWDI_INDICATION_P2P_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS;

typedef struct _WDI_INDICATION_P2P_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS
{
    WDI_P2P_SEND_ACTION_FRAME_RESULT_CONTAINER SendActionFrameResult;
} WDI_INDICATION_P2P_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS, *PWDI_INDICATION_P2P_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS;

typedef struct _WDI_INDICATION_RADIO_STATUS_PARAMETERS
{
    WDI_RADIO_STATUS_CONTAINER RadioState;
} WDI_INDICATION_RADIO_STATUS_PARAMETERS, *PWDI_INDICATION_RADIO_STATUS_PARAMETERS;

typedef struct _WDI_INDICATION_CREATE_PORT_COMPLETE_PARAMETERS
{
    WDI_PORT_ATTRIBUTES_CONTAINER PortAttributes;
} WDI_INDICATION_CREATE_PORT_COMPLETE_PARAMETERS, *PWDI_INDICATION_CREATE_PORT_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_DELETE_PORT_COMPLETE_PARAMETERS, *PWDI_INDICATION_DELETE_PORT_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_START_AP_COMPLETE_PARAMETERS, *PWDI_INDICATION_START_AP_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_STOP_AP_COMPLETE_PARAMETERS, *PWDI_INDICATION_STOP_AP_COMPLETE_PARAMETERS;

typedef struct _WDI_INDICATION_STOP_AP_PARAMETERS
{
    UINT32_CONTAINER Reason;
#ifdef __cplusplus
    _WDI_INDICATION_STOP_AP_PARAMETERS() : Reason( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_INDICATION_STOP_AP_PARAMETERS, *PWDI_INDICATION_STOP_AP_PARAMETERS;

typedef struct _WDI_INDICATION_CAN_SUSTAIN_AP_PARAMETERS
{
    UINT32_CONTAINER Reason;
#ifdef __cplusplus
    _WDI_INDICATION_CAN_SUSTAIN_AP_PARAMETERS() : Reason( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_INDICATION_CAN_SUSTAIN_AP_PARAMETERS, *PWDI_INDICATION_CAN_SUSTAIN_AP_PARAMETERS;

typedef struct _WDI_INDICATION_SEND_AP_ASSOCIATION_RESPONSE_COMPLETE_PARAMETERS
{
    WDI_ASSOC_RESPONSE_RESULT_PARAMETERS_CONTAINER AssocResponseResult;
    WDI_BYTE_BLOB AssocResponseFrame;
    WDI_BYTE_BLOB BeaconIEs;
    WDI_PHY_TYPE_LIST_CONTAINER ActivePhyList;
} WDI_INDICATION_SEND_AP_ASSOCIATION_RESPONSE_COMPLETE_PARAMETERS, *PWDI_INDICATION_SEND_AP_ASSOCIATION_RESPONSE_COMPLETE_PARAMETERS;

struct ArrayOfElementsOfWDI_ASSOCIATION_RESULT_CONTAINER
{
    UINT32 ElementCount;
    WDI_ASSOCIATION_RESULT_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_ASSOCIATION_RESULT_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_ASSOCIATION_RESULT_CONTAINER ) );
#endif

typedef struct _WDI_INDICATION_ASSOCIATION_RESULT_LIST
{
#ifdef __cplusplus
    ArrayOfElements<WDI_ASSOCIATION_RESULT_CONTAINER> AssociationResults;
#else
    struct ArrayOfElementsOfWDI_ASSOCIATION_RESULT_CONTAINER AssociationResults;
#endif
} WDI_INDICATION_ASSOCIATION_RESULT_LIST, *PWDI_INDICATION_ASSOCIATION_RESULT_LIST;

typedef EmptyMessageStructureType WDI_GET_AUTO_POWER_SAVE_PARAMETERS, *PWDI_GET_AUTO_POWER_SAVE_PARAMETERS;

typedef struct _WDI_GET_AUTO_POWER_SAVE_RESULTS
{
    WDI_GET_AUTO_POWER_SAVE_CONTAINER AutoPowerSaveParameters;
} WDI_GET_AUTO_POWER_SAVE_RESULTS, *PWDI_GET_AUTO_POWER_SAVE_RESULTS;

struct ArrayOfElementsOfWDI_PACKET_PATTERN_CONTAINER
{
    UINT32 ElementCount;
    WDI_PACKET_PATTERN_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_PACKET_PATTERN_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_PACKET_PATTERN_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_IPv4_TCP_SYNC_CONTAINER
{
    UINT32 ElementCount;
    WDI_IPv4_TCP_SYNC_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_IPv4_TCP_SYNC_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_IPv4_TCP_SYNC_CONTAINER ) );
#endif
struct ArrayOfElementsOfWDI_IPv6TCP_SYNC_CONTAINER
{
    UINT32 ElementCount;
    WDI_IPv6TCP_SYNC_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_IPv6TCP_SYNC_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_IPv6TCP_SYNC_CONTAINER ) );
#endif

typedef struct _WDI_SET_ADD_WOL_PATTERN_PARAMETERS
{
    struct _WDI_SET_ADD_WOL_PATTERN_PARAMETERS_Optional
    {
        UINT32 WakePacketPattern_IsPresent : 1;
        UINT32 WakePacketMagicPacketPatternId_IsPresent : 1;
        UINT32 WakePacketIpv4TcpSync_IsPresent : 1;
        UINT32 WakePacketIpv6TcpSync_IsPresent : 1;
        UINT32 WakePacketEapolRequestIdPatternId_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_ADD_WOL_PATTERN_PARAMETERS_Optional() : WakePacketPattern_IsPresent( FALSE ), WakePacketMagicPacketPatternId_IsPresent( FALSE ), WakePacketIpv4TcpSync_IsPresent( FALSE ), WakePacketIpv6TcpSync_IsPresent( FALSE ), WakePacketEapolRequestIdPatternId_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

#ifdef __cplusplus
    ArrayOfElements<WDI_PACKET_PATTERN_CONTAINER> WakePacketPattern;
#else
    struct ArrayOfElementsOfWDI_PACKET_PATTERN_CONTAINER WakePacketPattern;
#endif
    UINT32_CONTAINER WakePacketMagicPacketPatternId;
#ifdef __cplusplus
    ArrayOfElements<WDI_IPv4_TCP_SYNC_CONTAINER> WakePacketIpv4TcpSync;
#else
    struct ArrayOfElementsOfWDI_IPv4_TCP_SYNC_CONTAINER WakePacketIpv4TcpSync;
#endif
#ifdef __cplusplus
    ArrayOfElements<WDI_IPv6TCP_SYNC_CONTAINER> WakePacketIpv6TcpSync;
#else
    struct ArrayOfElementsOfWDI_IPv6TCP_SYNC_CONTAINER WakePacketIpv6TcpSync;
#endif
    UINT32_CONTAINER WakePacketEapolRequestIdPatternId;
#ifdef __cplusplus
    _WDI_SET_ADD_WOL_PATTERN_PARAMETERS() : WakePacketMagicPacketPatternId( (UINT32_CONTAINER)0 ), WakePacketEapolRequestIdPatternId( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_ADD_WOL_PATTERN_PARAMETERS, *PWDI_SET_ADD_WOL_PATTERN_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_ADD_WOL_PATTERN_RESULTS, *PWDI_SET_ADD_WOL_PATTERN_RESULTS;

typedef struct _WDI_SET_REMOVE_WOL_PATTERN_PARAMETERS
{
    UINT32_CONTAINER PatternId;
#ifdef __cplusplus
    _WDI_SET_REMOVE_WOL_PATTERN_PARAMETERS() : PatternId( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_REMOVE_WOL_PATTERN_PARAMETERS, *PWDI_SET_REMOVE_WOL_PATTERN_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_REMOVE_WOL_PATTERN_RESULTS, *PWDI_SET_REMOVE_WOL_PATTERN_RESULTS;

typedef struct _WDI_SET_MULTICAST_LIST_PARAMETERS
{
    struct _WDI_SET_MULTICAST_LIST_PARAMETERS_Optional
    {
        UINT32 MulticastList_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_MULTICAST_LIST_PARAMETERS_Optional() : MulticastList_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_ADDRESS_LIST_CONTAINER MulticastList;
} WDI_SET_MULTICAST_LIST_PARAMETERS, *PWDI_SET_MULTICAST_LIST_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_MULTICAST_LIST_RESULTS, *PWDI_SET_MULTICAST_LIST_RESULTS;

typedef struct _WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_PARAMETERS
{
    struct _WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_PARAMETERS_Optional
    {
        UINT32 ipv4ARPOffload_IsPresent : 1;
        UINT32 ipv6ARPOffload_IsPresent : 1;
        UINT32 DOT11RSNREKeyOffload_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_PARAMETERS_Optional() : ipv4ARPOffload_IsPresent( FALSE ), ipv6ARPOffload_IsPresent( FALSE ), DOT11RSNREKeyOffload_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_PM_PROTOCOL_OFFLOAD_IPv4ARP_CONTAINER ipv4ARPOffload;
    WDI_PM_PROTOCOL_OFFLOAD_IPv6NS_CONTAINER ipv6ARPOffload;
    WDI_RSN_OFFLOAD_KEYS_CONTAINER DOT11RSNREKeyOffload;
} WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_PARAMETERS, *PWDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_RESULTS, *PWDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_RESULTS;

typedef struct _WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_PARAMETERS
{
    UINT32_CONTAINER RemovePMOffload;
#ifdef __cplusplus
    _WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_PARAMETERS() : RemovePMOffload( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_PARAMETERS, *PWDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_RESULTS, *PWDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_RESULTS;

typedef struct _WDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL_PARAMETERS
{
    WDI_P2P_CHANNEL_CONTAINER Channel;
    WDI_P2P_CHANNEL_INDICATE_REASON_CONTAINER IndicateReason;
    WDI_AP_BAND_INFORMATION_CONTAINER APBandInformation;
#ifdef __cplusplus
    _WDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL_PARAMETERS() : IndicateReason( (WDI_P2P_CHANNEL_INDICATE_REASON_CONTAINER)0 )
    {
    };
#endif
} WDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL_PARAMETERS, *PWDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL_PARAMETERS;

typedef struct _WDI_INDICATION_P2P_OPERATING_CHANNEL_ATTRIBUTES_PARAMETERS
{
    WDI_P2P_CHANNEL_CONTAINER PreferredOperatingChannel;
    WDI_P2P_CHANNEL_LIST_CONTAINER ChannelList;
    WDI_P2P_CHANNEL_CONTAINER PreferredListenChannel;
} WDI_INDICATION_P2P_OPERATING_CHANNEL_ATTRIBUTES_PARAMETERS, *PWDI_INDICATION_P2P_OPERATING_CHANNEL_ATTRIBUTES_PARAMETERS;

typedef struct _WDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS
{
    struct _WDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS_Optional
    {
        UINT32 MacAddress_IsPresent : 1;
        UINT32 UnreachableDetectionThreshold_IsPresent : 1;
        UINT32 GroupOwnerResetPolicy_IsPresent : 1;
        UINT32 DisallowedBandList_IsPresent : 1;
        UINT32 AdapterNloScanMode_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS_Optional() : MacAddress_IsPresent( FALSE ), UnreachableDetectionThreshold_IsPresent( FALSE ), GroupOwnerResetPolicy_IsPresent( FALSE ), DisallowedBandList_IsPresent( FALSE ), AdapterNloScanMode_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER MacAddress;
    UINT32_CONTAINER UnreachableDetectionThreshold;
    WDI_P2P_GO_INTERNAL_RESET_POLICY_CONTAINER GroupOwnerResetPolicy;
    WDI_BAND_ID_LIST_CONTAINER DisallowedBandList;
    WDI_LINK_QUALITY_BAR_MAP_CONTAINER LinkQualityBarMap;
    WDI_ADAPTER_NLO_SCAN_MODE_CONTAINER AdapterNloScanMode;
    BOOL_CONTAINER PLDRSupport;
#ifdef __cplusplus
    _WDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS() : UnreachableDetectionThreshold( (UINT32_CONTAINER)0 ), GroupOwnerResetPolicy( (WDI_P2P_GO_INTERNAL_RESET_POLICY_CONTAINER)0 ), AdapterNloScanMode( (WDI_ADAPTER_NLO_SCAN_MODE_CONTAINER)0 ), PLDRSupport( (BOOL_CONTAINER)0 )
    {
        memset( &MacAddress, 0, sizeof( MacAddress ) );
    };
#endif
} WDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS, *PWDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_FIRMWARE_CONFIGURATION_RESULTS, *PWDI_SET_FIRMWARE_CONFIGURATION_RESULTS;

typedef EmptyMessageStructureType WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_INPUTS, *PWDI_GET_RECEIVE_COALESCING_MATCH_COUNT_INPUTS;

typedef struct _WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_PARAMETERS
{
    UINT64_CONTAINER MatchCount;
#ifdef __cplusplus
    _WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_PARAMETERS() : MatchCount( (UINT64_CONTAINER)0 )
    {
    };
#endif
} WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_PARAMETERS, *PWDI_GET_RECEIVE_COALESCING_MATCH_COUNT_PARAMETERS;

typedef struct _WDI_SET_CLEAR_RECEIVE_COALESCING_PARAMETERS
{
    UINT32_CONTAINER FilterId;
#ifdef __cplusplus
    _WDI_SET_CLEAR_RECEIVE_COALESCING_PARAMETERS() : FilterId( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_CLEAR_RECEIVE_COALESCING_PARAMETERS, *PWDI_SET_CLEAR_RECEIVE_COALESCING_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_CLEAR_RECEIVE_COALESCING_RESULTS, *PWDI_SET_CLEAR_RECEIVE_COALESCING_RESULTS;

typedef struct _WDI_GET_PM_PROTOCOL_OFFLOAD_PARAMETERS
{
    UINT32_CONTAINER GetPMOffload;
#ifdef __cplusplus
    _WDI_GET_PM_PROTOCOL_OFFLOAD_PARAMETERS() : GetPMOffload( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_GET_PM_PROTOCOL_OFFLOAD_PARAMETERS, *PWDI_GET_PM_PROTOCOL_OFFLOAD_PARAMETERS;

typedef struct _WDI_GET_PM_PROTOCOL_OFFLOAD_RESULTS
{
    struct _WDI_GET_PM_PROTOCOL_OFFLOAD_RESULTS_Optional
    {
        UINT32 ipv4ARPOffload_IsPresent : 1;
        UINT32 ipv6ARPOffload_IsPresent : 1;
        UINT32 DOT11RSNREKeyOffload_IsPresent : 1;
#ifdef __cplusplus
        _WDI_GET_PM_PROTOCOL_OFFLOAD_RESULTS_Optional() : ipv4ARPOffload_IsPresent( FALSE ), ipv6ARPOffload_IsPresent( FALSE ), DOT11RSNREKeyOffload_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_PM_PROTOCOL_OFFLOAD_IPv4ARP_CONTAINER ipv4ARPOffload;
    WDI_PM_PROTOCOL_OFFLOAD_IPv6NS_CONTAINER ipv6ARPOffload;
    WDI_RSN_OFFLOAD_KEYS_CONTAINER DOT11RSNREKeyOffload;
} WDI_GET_PM_PROTOCOL_OFFLOAD_RESULTS, *PWDI_GET_PM_PROTOCOL_OFFLOAD_RESULTS;

typedef struct _WDI_SET_ADVERTISEMENT_INFORMATION_PARAMETERS
{
    struct _WDI_SET_ADVERTISEMENT_INFORMATION_PARAMETERS_Optional
    {
        UINT32 AdditionalIEs_IsPresent : 1;
        UINT32 DeviceInformation_IsPresent : 1;
        UINT32 DeviceCapability_IsPresent : 1;
        UINT32 GroupOwnerCapability_IsPresent : 1;
        UINT32 SecondaryDeviceTypeList_IsPresent : 1;
        UINT32 AdvertisedServices_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_ADVERTISEMENT_INFORMATION_PARAMETERS_Optional() : AdditionalIEs_IsPresent( FALSE ), DeviceInformation_IsPresent( FALSE ), DeviceCapability_IsPresent( FALSE ), GroupOwnerCapability_IsPresent( FALSE ), SecondaryDeviceTypeList_IsPresent( FALSE ), AdvertisedServices_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_ADDITIONAL_IES_CONTAINER AdditionalIEs;
    WDI_P2P_DEVICE_INFO_CONTAINER DeviceInformation;
    WDI_P2P_DEVICE_CAPABILITY_CONTAINER DeviceCapability;
    WDI_P2P_GROUP_OWNER_CAPABILITY_CONTAINER GroupOwnerCapability;
    WDI_P2P_SECONDARY_DEVICE_TYPE_CONTAINER SecondaryDeviceTypeList;
    WDI_P2P_ADVERTISED_SERVICES_CONTAINER AdvertisedServices;
} WDI_SET_ADVERTISEMENT_INFORMATION_PARAMETERS, *PWDI_SET_ADVERTISEMENT_INFORMATION_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_ADVERTISEMENT_INFORMATION_RESULTS, *PWDI_SET_ADVERTISEMENT_INFORMATION_RESULTS;

typedef struct _WDI_TASK_CHANGE_OPERATION_MODE_PARAMETERS
{
    WDI_OPERATION_MODE_CONTAINER OperationMode;
#ifdef __cplusplus
    _WDI_TASK_CHANGE_OPERATION_MODE_PARAMETERS() : OperationMode( (WDI_OPERATION_MODE_CONTAINER)0 )
    {
    };
#endif
} WDI_TASK_CHANGE_OPERATION_MODE_PARAMETERS, *PWDI_TASK_CHANGE_OPERATION_MODE_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_CHANGE_OPERATION_MODE_RESULTS, *PWDI_TASK_CHANGE_OPERATION_MODE_RESULTS;

typedef EmptyMessageStructureType WDI_INDICATION_CHANGE_OPERATION_MODE_COMPLETE_PARAMETERS, *PWDI_INDICATION_CHANGE_OPERATION_MODE_COMPLETE_PARAMETERS;

struct ArrayOfElementsOfWDI_BYTE_BLOB
{
    UINT32 ElementCount;
    WDI_BYTE_BLOB* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_BYTE_BLOB> ) == sizeof( struct ArrayOfElementsOfWDI_BYTE_BLOB ) );
#endif

typedef struct _WDI_IHV_REQUEST_PARAMETERS
{
    struct _WDI_IHV_REQUEST_PARAMETERS_Optional
    {
        UINT32 IHVData_IsPresent : 1;
#ifdef __cplusplus
        _WDI_IHV_REQUEST_PARAMETERS_Optional() : IHVData_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

#ifdef __cplusplus
    ArrayOfElements<WDI_BYTE_BLOB> IHVData;
#else
    struct ArrayOfElementsOfWDI_BYTE_BLOB IHVData;
#endif
} WDI_IHV_REQUEST_PARAMETERS, *PWDI_IHV_REQUEST_PARAMETERS;

typedef struct _WDI_INDICATION_IHV_EVENT_PARAMETERS
{
    struct _WDI_INDICATION_IHV_EVENT_PARAMETERS_Optional
    {
        UINT32 IHVData_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INDICATION_IHV_EVENT_PARAMETERS_Optional() : IHVData_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

#ifdef __cplusplus
    ArrayOfElements<WDI_BYTE_BLOB> IHVData;
#else
    struct ArrayOfElementsOfWDI_BYTE_BLOB IHVData;
#endif
} WDI_INDICATION_IHV_EVENT_PARAMETERS, *PWDI_INDICATION_IHV_EVENT_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_OPEN_COMPLETE_PARAMETERS, *PWDI_INDICATION_OPEN_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_CLOSE_COMPLETE_PARAMETERS, *PWDI_INDICATION_CLOSE_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_FLUSH_BSS_ENTRY_PARAMETERS, *PWDI_SET_FLUSH_BSS_ENTRY_PARAMETERS;

typedef struct _WDI_INDICATION_ASSOCIATION_PARAMS_REQUEST_PARAMETERS
{
    struct _WDI_INDICATION_ASSOCIATION_PARAMS_REQUEST_PARAMETERS_Optional
    {
        UINT32 BSSEntryList_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INDICATION_ASSOCIATION_PARAMS_REQUEST_PARAMETERS_Optional() : BSSEntryList_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    UINT16_LIST_CONTAINER AssociationParameterTypes;
#ifdef __cplusplus
    ArrayOfElements<WDI_BSS_ENTRY_CONTAINER> BSSEntryList;
#else
    struct ArrayOfElementsOfWDI_BSS_ENTRY_CONTAINER BSSEntryList;
#endif
} WDI_INDICATION_ASSOCIATION_PARAMS_REQUEST_PARAMETERS, *PWDI_INDICATION_ASSOCIATION_PARAMS_REQUEST_PARAMETERS;

typedef struct _WDI_SET_ASSOCIATION_PARAMS_PARAMETERS
{
#ifdef __cplusplus
    ArrayOfElements<WDI_CONNECT_BSS_ENTRY_CONTAINER> BSSEntryList;
#else
    struct ArrayOfElementsOfWDI_CONNECT_BSS_ENTRY_CONTAINER BSSEntryList;
#endif
} WDI_SET_ASSOCIATION_PARAMS_PARAMETERS, *PWDI_SET_ASSOCIATION_PARAMS_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_ASSOCIATION_PARAMS_RESULTS, *PWDI_SET_ASSOCIATION_PARAMS_RESULTS;

typedef struct _WDI_TASK_ROAM_PARAMETERS
{
    WDI_CONNECT_PARAMETERS_CONTAINER ConnectParameters;
#ifdef __cplusplus
    ArrayOfElements<WDI_CONNECT_BSS_ENTRY_CONTAINER> PreferredBSSEntryList;
#else
    struct ArrayOfElementsOfWDI_CONNECT_BSS_ENTRY_CONTAINER PreferredBSSEntryList;
#endif
} WDI_TASK_ROAM_PARAMETERS, *PWDI_TASK_ROAM_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_ROAM_RESULTS, *PWDI_TASK_ROAM_RESULTS;

typedef EmptyMessageStructureType WDI_INDICATION_ROAM_COMPLETE_PARAMETERS, *PWDI_INDICATION_ROAM_COMPLETE_PARAMETERS;

typedef struct _WDI_TASK_SEND_REQUEST_ACTION_FRAME_PARAMETERS
{
    WDI_SEND_ACTION_FRAME_REQUEST_PARAMETERS_CONTAINER RequestParams;
    WDI_BYTE_BLOB ActionFrameBody;
} WDI_TASK_SEND_REQUEST_ACTION_FRAME_PARAMETERS, *PWDI_TASK_SEND_REQUEST_ACTION_FRAME_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_SEND_REQUEST_ACTION_FRAME_RESULTS, *PWDI_TASK_SEND_REQUEST_ACTION_FRAME_RESULTS;

typedef struct _WDI_INDICATION_ACTION_FRAME_RECEIVED_PARAMETERS
{
    WDI_MAC_ADDRESS_CONTAINER SourceAddress;
    WDI_CHANNEL_INFO_CONTAINER ChannelInfo;
    WDI_BYTE_BLOB ActionFrameBody;
#ifdef __cplusplus
    _WDI_INDICATION_ACTION_FRAME_RECEIVED_PARAMETERS()
    {
        memset( &SourceAddress, 0, sizeof( SourceAddress ) );
    };
#endif
} WDI_INDICATION_ACTION_FRAME_RECEIVED_PARAMETERS, *PWDI_INDICATION_ACTION_FRAME_RECEIVED_PARAMETERS;

typedef struct _WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG_PARAMETERS
{
    struct _WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG_PARAMETERS_Optional
    {
        UINT32 TcpOffloadCapabilities_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG_PARAMETERS_Optional() : TcpOffloadCapabilities_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_TCP_OFFLOAD_CAPABILITIES_CONTAINER TcpOffloadCapabilities;
} WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG_PARAMETERS, *PWDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG_PARAMETERS;

typedef struct _WDI_TASK_SEND_RESPONSE_ACTION_FRAME_PARAMETERS
{
    WDI_SEND_ACTION_FRAME_REQUEST_PARAMETERS_CONTAINER ResponseParams;
    WDI_BYTE_BLOB ActionFrameBody;
} WDI_TASK_SEND_RESPONSE_ACTION_FRAME_PARAMETERS, *PWDI_TASK_SEND_RESPONSE_ACTION_FRAME_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_SEND_RESPONSE_ACTION_FRAME_RESULTS, *PWDI_TASK_SEND_RESPONSE_ACTION_FRAME_RESULTS;

typedef EmptyMessageStructureType WDI_INDICATION_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS, *PWDI_INDICATION_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS, *PWDI_INDICATION_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS;

typedef EmptyMessageStructureType WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_INPUTS, *PWDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_INPUTS;

typedef struct _WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_PARAMETERS
{
    UINT8_CONTAINER NextDialogToken;
#ifdef __cplusplus
    _WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_PARAMETERS() : NextDialogToken( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_PARAMETERS, *PWDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_PARAMETERS;

typedef struct _WDI_SET_P2P_WPS_ENABLED_PARAMETERS
{
    UINT8_CONTAINER WPSEnabled;
#ifdef __cplusplus
    _WDI_SET_P2P_WPS_ENABLED_PARAMETERS() : WPSEnabled( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_P2P_WPS_ENABLED_PARAMETERS, *PWDI_SET_P2P_WPS_ENABLED_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_P2P_WPS_ENABLED_RESULTS, *PWDI_SET_P2P_WPS_ENABLED_RESULTS;

typedef struct _WDI_TASK_IHV_PARAMETERS
{
    struct _WDI_TASK_IHV_PARAMETERS_Optional
    {
        UINT32 IHVSpecificContext_IsPresent : 1;
#ifdef __cplusplus
        _WDI_TASK_IHV_PARAMETERS_Optional() : IHVSpecificContext_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_BYTE_BLOB IHVSpecificContext;
} WDI_TASK_IHV_PARAMETERS, *PWDI_TASK_IHV_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_IHV_RESULTS, *PWDI_TASK_IHV_RESULTS;

struct ArrayOfElementsOfWDI_FTM_TARGET_BSS_ENTRY_CONTAINER
{
    UINT32 ElementCount;
    WDI_FTM_TARGET_BSS_ENTRY_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_FTM_TARGET_BSS_ENTRY_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_FTM_TARGET_BSS_ENTRY_CONTAINER ) );
#endif

typedef struct _WDI_TASK_REQUEST_FTM_PARAMETERS
{
    UINT32_CONTAINER FTMRequestTimeout;
#ifdef __cplusplus
    ArrayOfElements<WDI_FTM_TARGET_BSS_ENTRY_CONTAINER> FTMTargetList;
#else
    struct ArrayOfElementsOfWDI_FTM_TARGET_BSS_ENTRY_CONTAINER FTMTargetList;
#endif
#ifdef __cplusplus
    _WDI_TASK_REQUEST_FTM_PARAMETERS() : FTMRequestTimeout( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_TASK_REQUEST_FTM_PARAMETERS, *PWDI_TASK_REQUEST_FTM_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_REQUEST_FTM_RESULTS, *PWDI_TASK_REQUEST_FTM_RESULTS;

struct ArrayOfElementsOfWDI_FTM_RESPONSE_CONTAINER
{
    UINT32 ElementCount;
    WDI_FTM_RESPONSE_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_FTM_RESPONSE_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_FTM_RESPONSE_CONTAINER ) );
#endif

typedef struct _WDI_INDICATION_REQUEST_FTM_COMPLETE_PARAMETERS
{
#ifdef __cplusplus
    ArrayOfElements<WDI_FTM_RESPONSE_CONTAINER> FTMResponseList;
#else
    struct ArrayOfElementsOfWDI_FTM_RESPONSE_CONTAINER FTMResponseList;
#endif
} WDI_INDICATION_REQUEST_FTM_COMPLETE_PARAMETERS, *PWDI_INDICATION_REQUEST_FTM_COMPLETE_PARAMETERS;

typedef struct _WDI_INDICATION_IHV_TASK_REQUEST_PARAMETERS
{
    struct _WDI_INDICATION_IHV_TASK_REQUEST_PARAMETERS_Optional
    {
        UINT32 IHVSpecificContext_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INDICATION_IHV_TASK_REQUEST_PARAMETERS_Optional() : IHVSpecificContext_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_IHV_TASK_PRIORITY_CONTAINER RequestedPriority;
    WDI_BYTE_BLOB IHVSpecificContext;
#ifdef __cplusplus
    _WDI_INDICATION_IHV_TASK_REQUEST_PARAMETERS() : RequestedPriority( (WDI_IHV_TASK_PRIORITY_CONTAINER)0 )
    {
    };
#endif
} WDI_INDICATION_IHV_TASK_REQUEST_PARAMETERS, *PWDI_INDICATION_IHV_TASK_REQUEST_PARAMETERS;

typedef EmptyMessageStructureType WDI_INDICATION_IHV_TASK_COMPLETE_PARAMETERS, *PWDI_INDICATION_IHV_TASK_COMPLETE_PARAMETERS;

typedef struct _WDI_TASK_ABORT_PARAMETERS
{
    WDI_CANCEL_PARAMETERS_CONTAINER CancelParameters;
} WDI_TASK_ABORT_PARAMETERS, *PWDI_TASK_ABORT_PARAMETERS;

typedef EmptyMessageStructureType WDI_TASK_ABORT_RESULTS, *PWDI_TASK_ABORT_RESULTS;

typedef struct _WDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS
{
    WDI_SET_OFFLOAD_PARAMS_CONTAINER SetOffloadParameters;
} WDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS, *PWDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_TCP_OFFLOAD_PARAMS_RESULTS, *PWDI_SET_TCP_OFFLOAD_PARAMS_RESULTS;

typedef EmptyMessageStructureType WDI_TCP_RSC_STATISTICS_INPUTS, *PWDI_TCP_RSC_STATISTICS_INPUTS;

typedef struct _WDI_TCP_RSC_STATISTICS_PARAMETERS
{
    WDI_TCP_RSC_STATISTICS_CONTAINER RscStatsParams;
} WDI_TCP_RSC_STATISTICS_PARAMETERS, *PWDI_TCP_RSC_STATISTICS_PARAMETERS;

typedef struct _WDI_SET_ENCAPSULATION_OFFLOAD_PARAMS_PARAMETERS
{
    UINT8_CONTAINER EnabledV4;
    UINT8_CONTAINER EnabledV6;
#ifdef __cplusplus
    _WDI_SET_ENCAPSULATION_OFFLOAD_PARAMS_PARAMETERS() : EnabledV4( (UINT8_CONTAINER)0 ), EnabledV6( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_ENCAPSULATION_OFFLOAD_PARAMS_PARAMETERS, *PWDI_SET_ENCAPSULATION_OFFLOAD_PARAMS_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_ENCAPSULATION_OFFLOAD_RESULTS, *PWDI_SET_ENCAPSULATION_OFFLOAD_RESULTS;

typedef struct _WDI_INDICATION_FT_ASSOC_PARAMS_NEEDED_PARAMETERS
{
    WDI_MAC_ADDRESS_CONTAINER BssId;
    WDI_BYTE_BLOB AuthRequest;
    WDI_BYTE_BLOB AuthResponse;
#ifdef __cplusplus
    _WDI_INDICATION_FT_ASSOC_PARAMS_NEEDED_PARAMETERS()
    {
        memset( &BssId, 0, sizeof( BssId ) );
    };
#endif
} WDI_INDICATION_FT_ASSOC_PARAMS_NEEDED_PARAMETERS, *PWDI_INDICATION_FT_ASSOC_PARAMS_NEEDED_PARAMETERS;

typedef struct _WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_COMMAND
{
    struct _WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_COMMAND_Optional
    {
        UINT32 RSNIE_IsPresent : 1;
        UINT32 MDE_IsPresent : 1;
        UINT32 FTE_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_COMMAND_Optional() : RSNIE_IsPresent( FALSE ), MDE_IsPresent( FALSE ), FTE_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_STATUS_CONTAINER status;
    WDI_BYTE_BLOB RSNIE;
    WDI_BYTE_BLOB MDE;
    WDI_BYTE_BLOB FTE;
#ifdef __cplusplus
    _WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_COMMAND() : status( (WDI_STATUS_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_COMMAND, *PWDI_SET_FAST_BSS_TRANSITION_PARAMETERS_COMMAND;

typedef EmptyMessageStructureType WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_RESULTS, *PWDI_SET_FAST_BSS_TRANSITION_PARAMETERS_RESULTS;

typedef struct _WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED_PARAMETERS
{
    struct _WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED_PARAMETERS_Optional
    {
        UINT32 SAECommitFrame_IsPresent : 1;
        UINT32 SAEConfirmFrame_IsPresent : 1;
        UINT32 SAEStatus_IsPresent : 1;
        UINT32 LocalMloLinkBssId_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED_PARAMETERS_Optional() : SAECommitFrame_IsPresent( FALSE ), SAEConfirmFrame_IsPresent( FALSE ), SAEStatus_IsPresent( FALSE ), LocalMloLinkBssId_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER BssId;
    WDI_SAE_INDICATION_TYPE_CONTAINER SAEIndicationType;
    WDI_BYTE_BLOB SAECommitFrame;
    WDI_BYTE_BLOB SAEConfirmFrame;
    WDI_SAE_STATUS_CONTAINER SAEStatus;
    WDI_MAC_ADDRESS_CONTAINER LocalMloLinkBssId;
#ifdef __cplusplus
    _WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED_PARAMETERS() : SAEIndicationType( (WDI_SAE_INDICATION_TYPE_CONTAINER)0 ), SAEStatus( (WDI_SAE_STATUS_CONTAINER)0 )
    {
        memset( &BssId, 0, sizeof( BssId ) );
        memset( &LocalMloLinkBssId, 0, sizeof( LocalMloLinkBssId ) );
    };
#endif
} WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED_PARAMETERS, *PWDI_INDICATION_SAE_AUTH_PARAMS_NEEDED_PARAMETERS;

typedef struct _WDI_SET_SAE_AUTH_PARAMS_COMMAND
{
    struct _WDI_SET_SAE_AUTH_PARAMS_COMMAND_Optional
    {
        UINT32 SAECommitParams_IsPresent : 1;
        UINT32 SAEConfirmParams_IsPresent : 1;
        UINT32 SAEStatus_IsPresent : 1;
#ifdef __cplusplus
        _WDI_SET_SAE_AUTH_PARAMS_COMMAND_Optional() : SAECommitParams_IsPresent( FALSE ), SAEConfirmParams_IsPresent( FALSE ), SAEStatus_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_MAC_ADDRESS_CONTAINER BssId;
    WDI_SAE_REQUEST_TYPE_CONTAINER SAERequestType;
    WDI_SAE_COMMIT_PARAMS SAECommitParams;
    WDI_SAE_CONFIRM_FRAME SAEConfirmParams;
    WDI_SAE_STATUS_CONTAINER SAEStatus;
#ifdef __cplusplus
    _WDI_SET_SAE_AUTH_PARAMS_COMMAND() : SAERequestType( (WDI_SAE_REQUEST_TYPE_CONTAINER)0 ), SAEStatus( (WDI_SAE_STATUS_CONTAINER)0 )
    {
        memset( &BssId, 0, sizeof( BssId ) );
    };
#endif
} WDI_SET_SAE_AUTH_PARAMS_COMMAND, *PWDI_SET_SAE_AUTH_PARAMS_COMMAND;

typedef EmptyMessageStructureType WDI_SET_SAE_AUTH_PARAMS_RESULTS, *PWDI_SET_SAE_AUTH_PARAMS_RESULTS;

typedef EmptyMessageStructureType WDI_INDICATION_FIRMWARE_STALLED_PARAMETERS, *PWDI_INDICATION_FIRMWARE_STALLED_PARAMETERS;

struct ArrayOfElementsOfWDI_NEIGHBOR_REPORT_ENTRY_CONTAINER
{
    UINT32 ElementCount;
    WDI_NEIGHBOR_REPORT_ENTRY_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_NEIGHBOR_REPORT_ENTRY_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_NEIGHBOR_REPORT_ENTRY_CONTAINER ) );
#endif

typedef struct _WDI_SET_NEIGHBOR_REPORT_ENTRIES_PARAMETERS
{
#ifdef __cplusplus
    ArrayOfElements<WDI_NEIGHBOR_REPORT_ENTRY_CONTAINER> NeighborReportEntry;
#else
    struct ArrayOfElementsOfWDI_NEIGHBOR_REPORT_ENTRY_CONTAINER NeighborReportEntry;
#endif
} WDI_SET_NEIGHBOR_REPORT_ENTRIES_PARAMETERS, *PWDI_SET_NEIGHBOR_REPORT_ENTRIES_PARAMETERS;

typedef EmptyMessageStructureType WDI_SET_NEIGHBOR_REPORT_ENTRIES_RESULTS, *PWDI_SET_NEIGHBOR_REPORT_ENTRIES_RESULTS;

typedef EmptyMessageStructureType WDI_GET_SUPPORTED_DEVICE_SERVICES_INPUTS, *PWDI_GET_SUPPORTED_DEVICE_SERVICES_INPUTS;

typedef struct _WDI_GET_SUPPORTED_DEVICE_SERVICES_PARAMETERS
{
    WDI_GUID_LIST_CONTAINER DeviceServiceGUIDList;
} WDI_GET_SUPPORTED_DEVICE_SERVICES_PARAMETERS, *PWDI_GET_SUPPORTED_DEVICE_SERVICES_PARAMETERS;

typedef struct _WDI_DEVICE_SERVICE_COMMAND_INPUTS
{
    struct _WDI_DEVICE_SERVICE_COMMAND_INPUTS_Optional
    {
        UINT32 Params_IsPresent : 1;
#ifdef __cplusplus
        _WDI_DEVICE_SERVICE_COMMAND_INPUTS_Optional() : Params_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_GUID_CONTAINER DeviceServiceGUID;
    UINT32_CONTAINER Opcode;
#ifdef __cplusplus
    ArrayOfElements<WDI_BYTE_BLOB> Params;
#else
    struct ArrayOfElementsOfWDI_BYTE_BLOB Params;
#endif
#ifdef __cplusplus
    _WDI_DEVICE_SERVICE_COMMAND_INPUTS() : Opcode( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_DEVICE_SERVICE_COMMAND_INPUTS, *PWDI_DEVICE_SERVICE_COMMAND_INPUTS;

typedef struct _WDI_DEVICE_SERVICE_COMMAND_PARAMETERS
{
    struct _WDI_DEVICE_SERVICE_COMMAND_PARAMETERS_Optional
    {
        UINT32 Params_IsPresent : 1;
#ifdef __cplusplus
        _WDI_DEVICE_SERVICE_COMMAND_PARAMETERS_Optional() : Params_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

#ifdef __cplusplus
    ArrayOfElements<WDI_BYTE_BLOB> Params;
#else
    struct ArrayOfElementsOfWDI_BYTE_BLOB Params;
#endif
} WDI_DEVICE_SERVICE_COMMAND_PARAMETERS, *PWDI_DEVICE_SERVICE_COMMAND_PARAMETERS;

typedef struct _WDI_INDICATION_CIPHER_KEY_UPDATED_PARAMETERS
{
    WDI_RSN_OFFLOAD_KEYS_CONTAINER DOT11RSNREKeyOffload;
} WDI_INDICATION_CIPHER_KEY_UPDATED_PARAMETERS, *PWDI_INDICATION_CIPHER_KEY_UPDATED_PARAMETERS;

typedef struct _WDI_INDICATION_DEVICE_SERVICE_EVENT_PARAMETERS
{
    struct _WDI_INDICATION_DEVICE_SERVICE_EVENT_PARAMETERS_Optional
    {
        UINT32 Params_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INDICATION_DEVICE_SERVICE_EVENT_PARAMETERS_Optional() : Params_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    WDI_GUID_CONTAINER DeviceServiceGUID;
    UINT32_CONTAINER Opcode;
#ifdef __cplusplus
    ArrayOfElements<WDI_BYTE_BLOB> Params;
#else
    struct ArrayOfElementsOfWDI_BYTE_BLOB Params;
#endif
#ifdef __cplusplus
    _WDI_INDICATION_DEVICE_SERVICE_EVENT_PARAMETERS() : Opcode( (UINT32_CONTAINER)0 )
    {
    };
#endif
} WDI_INDICATION_DEVICE_SERVICE_EVENT_PARAMETERS, *PWDI_INDICATION_DEVICE_SERVICE_EVENT_PARAMETERS;

struct ArrayOfElementsOfWDI_BAND_CHANNEL_LIST_CONTAINER
{
    UINT32 ElementCount;
    WDI_BAND_CHANNEL_LIST_CONTAINER* pElements;
    BOOLEAN MemoryInternallyAllocated;
};
#ifdef __cplusplus
C_ASSERT( sizeof( ArrayOfElements<WDI_BAND_CHANNEL_LIST_CONTAINER> ) == sizeof( struct ArrayOfElementsOfWDI_BAND_CHANNEL_LIST_CONTAINER ) );
#endif

typedef struct _WDI_INDICATION_SECONDARY_STA_CONNECTIVITY_PARAMETERS
{
    struct _WDI_INDICATION_SECONDARY_STA_CONNECTIVITY_PARAMETERS_Optional
    {
        UINT32 AvailableBandChannelList_IsPresent : 1;
#ifdef __cplusplus
        _WDI_INDICATION_SECONDARY_STA_CONNECTIVITY_PARAMETERS_Optional() : AvailableBandChannelList_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

    UINT8_CONTAINER LimitedConnectivity;
#ifdef __cplusplus
    ArrayOfElements<WDI_BAND_CHANNEL_LIST_CONTAINER> AvailableBandChannelList;
#else
    struct ArrayOfElementsOfWDI_BAND_CHANNEL_LIST_CONTAINER AvailableBandChannelList;
#endif
#ifdef __cplusplus
    _WDI_INDICATION_SECONDARY_STA_CONNECTIVITY_PARAMETERS() : LimitedConnectivity( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_INDICATION_SECONDARY_STA_CONNECTIVITY_PARAMETERS, *PWDI_INDICATION_SECONDARY_STA_CONNECTIVITY_PARAMETERS;

typedef struct _WDI_SET_LOCATION_PRIVACY_PARAMETERS
{
    UINT8_CONTAINER LocationAllowed;
#ifdef __cplusplus
    _WDI_SET_LOCATION_PRIVACY_PARAMETERS() : LocationAllowed( (UINT8_CONTAINER)0 )
    {
    };
#endif
} WDI_SET_LOCATION_PRIVACY_PARAMETERS, *PWDI_SET_LOCATION_PRIVACY_PARAMETERS;

typedef struct _WDI_TASK_TEST_PARAMETERS
{
    struct _WDI_TASK_TEST_PARAMETERS_Optional
    {
        UINT32 Newsocoomomc_IsPresent : 1;
        UINT32 Newsocoomosc_IsPresent : 1;
        UINT32 Newsocoosomc_IsPresent : 1;
        UINT32 Newsocoososc_IsPresent : 1;
        UINT32 Newsocromomc_IsPresent : 1;
        UINT32 Newsocromosc_IsPresent : 1;
        UINT32 Newsocrosomc_IsPresent : 1;
        UINT32 Newsocrososc_IsPresent : 1;
        UINT32 Newnocmc_IsPresent : 1;
        UINT32 Newnocsc_IsPresent : 1;
#ifdef __cplusplus
        _WDI_TASK_TEST_PARAMETERS_Optional() : Newsocoomomc_IsPresent( FALSE ), Newsocoomosc_IsPresent( FALSE ), Newsocoosomc_IsPresent( FALSE ), Newsocoososc_IsPresent( FALSE ), Newsocromomc_IsPresent( FALSE ), Newsocromosc_IsPresent( FALSE ), Newsocrosomc_IsPresent( FALSE ), Newsocrososc_IsPresent( FALSE ), Newnocmc_IsPresent( FALSE ), Newnocsc_IsPresent( FALSE )
        {
        };
#endif
    } Optional;

#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> Newsocoomomc;
#else
    struct ArrayOfElementsOfTEST_CONTAINER Newsocoomomc;
#endif
    TEST_CONTAINER Newsocoomosc;
#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> Newsocoosomc;
#else
    struct ArrayOfElementsOfTEST_CONTAINER Newsocoosomc;
#endif
    TEST_CONTAINER Newsocoososc;
#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> Newsocromomc;
#else
    struct ArrayOfElementsOfTEST_CONTAINER Newsocromomc;
#endif
    TEST_CONTAINER Newsocromosc;
#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> Newsocrosomc;
#else
    struct ArrayOfElementsOfTEST_CONTAINER Newsocrosomc;
#endif
    TEST_CONTAINER Newsocrososc;
#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> Newsrcoomomc;
#else
    struct ArrayOfElementsOfTEST_CONTAINER Newsrcoomomc;
#endif
    TEST_CONTAINER Newsrcoomosc;
#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> Newsrcoosomc;
#else
    struct ArrayOfElementsOfTEST_CONTAINER Newsrcoosomc;
#endif
    TEST_AGGREGATE_CONTAINER Newsrcoososc;
#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> Newsrcromomc;
#else
    struct ArrayOfElementsOfTEST_CONTAINER Newsrcromomc;
#endif
    TEST_CONTAINER Newsrcromosc;
#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> Newsrcrosomc;
#else
    struct ArrayOfElementsOfTEST_CONTAINER Newsrcrosomc;
#endif
    TEST_CONTAINER Newsrcrososc;
#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> Newnocmc;
#else
    struct ArrayOfElementsOfTEST_CONTAINER Newnocmc;
#endif
    TEST_CONTAINER Newnocsc;
#ifdef __cplusplus
    ArrayOfElements<TEST_CONTAINER> Newnrcmc;
#else
    struct ArrayOfElementsOfTEST_CONTAINER Newnrcmc;
#endif
    TEST_CONTAINER Newnrcsc;
#ifdef __cplusplus
    _WDI_TASK_TEST_PARAMETERS() : Newsocoomosc( (TEST_CONTAINER)0 ), Newsocoososc( (TEST_CONTAINER)0 ), Newsocromosc( (TEST_CONTAINER)0 ), Newsocrososc( (TEST_CONTAINER)0 ), Newsrcoomosc( (TEST_CONTAINER)0 ), Newsrcromosc( (TEST_CONTAINER)0 ), Newsrcrososc( (TEST_CONTAINER)0 ), Newnocsc( (TEST_CONTAINER)0 ), Newnrcsc( (TEST_CONTAINER)0 )
    {
    };
#endif
} WDI_TASK_TEST_PARAMETERS, *PWDI_TASK_TEST_PARAMETERS;

#ifdef __cplusplus
extern "C" {
#endif
    NDIS_STATUS __stdcall GenerateWdiTaskOpen(
        _In_opt_ WDI_TASK_OPEN_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskOpen(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_OPEN_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskOpen( _In_ WDI_TASK_OPEN_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskClose(
        _In_opt_ WDI_TASK_CLOSE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskClose(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_CLOSE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskClose( _In_ WDI_TASK_CLOSE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetHostDetectError(
        _In_opt_ WDI_SET_HOST_DETECT_ERROR_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetHostDetectError(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_HOST_DETECT_ERROR_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetHostDetectError( _In_ WDI_SET_HOST_DETECT_ERROR_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskScanToIhv(
        _In_ WDI_SCAN_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SCAN_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskScanToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskScanToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SCAN_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskScanToIhv( _In_ WDI_SCAN_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskScanFromIhv(
        _In_opt_ WDI_SCAN_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskScanFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SCAN_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskScanFromIhv( _In_ WDI_SCAN_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskP2pDiscoverToIhv(
        _In_ WDI_TASK_P2P_DISCOVER_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_P2P_DISCOVER_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskP2pDiscoverToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskP2pDiscoverToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_P2P_DISCOVER_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskP2pDiscoverToIhv( _In_ WDI_TASK_P2P_DISCOVER_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskP2pDiscoverFromIhv(
        _In_opt_ WDI_TASK_P2P_DISCOVER_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskP2pDiscoverFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_P2P_DISCOVER_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskP2pDiscoverFromIhv( _In_ WDI_TASK_P2P_DISCOVER_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetP2pStartBackgroundDiscoveryToIhv(
        _In_ WDI_SET_P2P_START_BACKGROUND_DISCOVERY_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_P2P_START_BACKGROUND_DISCOVERY_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetP2pStartBackgroundDiscoveryToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetP2pStartBackgroundDiscoveryToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_P2P_START_BACKGROUND_DISCOVERY_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetP2pStartBackgroundDiscoveryToIhv( _In_ WDI_SET_P2P_START_BACKGROUND_DISCOVERY_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetP2pStartBackgroundDiscoveryFromIhv(
        _In_opt_ WDI_SET_P2P_START_BACKGROUND_DISCOVERY_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetP2pStartBackgroundDiscoveryFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_P2P_START_BACKGROUND_DISCOVERY_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetP2pStartBackgroundDiscoveryFromIhv( _In_ WDI_SET_P2P_START_BACKGROUND_DISCOVERY_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetP2pStopBackgroundDiscovery(
        _In_opt_ WDI_SET_P2P_STOP_BACKGROUND_DISCOVERY_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetP2pStopBackgroundDiscovery(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_P2P_STOP_BACKGROUND_DISCOVERY_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetP2pStopBackgroundDiscovery( _In_ WDI_SET_P2P_STOP_BACKGROUND_DISCOVERY_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskConnectToIhv(
        _In_ WDI_TASK_CONNECT_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_CONNECT_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskConnectToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskConnectToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_CONNECT_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskConnectToIhv( _In_ WDI_TASK_CONNECT_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskConnectFromIhv(
        _In_opt_ WDI_TASK_CONNECT_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskConnectFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_CONNECT_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskConnectFromIhv( _In_ WDI_TASK_CONNECT_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskDot11ResetToIhv(
        _In_ WDI_TASK_DOT11_RESET_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_DOT11_RESET_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskDot11ResetToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskDot11ResetToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_DOT11_RESET_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskDot11ResetToIhv( _In_ WDI_TASK_DOT11_RESET_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskDot11ResetFromIhv(
        _In_opt_ WDI_TASK_DOT11_RESET_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskDot11ResetFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_DOT11_RESET_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskDot11ResetFromIhv( _In_ WDI_TASK_DOT11_RESET_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskDisconnectToIhv(
        _In_ WDI_TASK_DISCONNECT_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_DISCONNECT_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskDisconnectToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskDisconnectToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_DISCONNECT_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskDisconnectToIhv( _In_ WDI_TASK_DISCONNECT_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskDisconnectFromIhv(
        _In_opt_ WDI_TASK_DISCONNECT_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskDisconnectFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_DISCONNECT_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskDisconnectFromIhv( _In_ WDI_TASK_DISCONNECT_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskP2pSendRequestActionFrameToIhv(
        _In_ WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskP2pSendRequestActionFrameToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskP2pSendRequestActionFrameToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskP2pSendRequestActionFrameToIhv( _In_ WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskP2pSendRequestActionFrameFromIhv(
        _In_opt_ WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskP2pSendRequestActionFrameFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskP2pSendRequestActionFrameFromIhv( _In_ WDI_TASK_P2P_SEND_REQUEST_ACTION_FRAME_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskP2pSendResponseActionFrameToIhv(
        _In_ WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskP2pSendResponseActionFrameToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskP2pSendResponseActionFrameToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskP2pSendResponseActionFrameToIhv( _In_ WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskP2pSendResponseActionFrameFromIhv(
        _In_opt_ WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskP2pSendResponseActionFrameFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskP2pSendResponseActionFrameFromIhv( _In_ WDI_TASK_P2P_SEND_RESPONSE_ACTION_FRAME_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskSetRadioStateToIhv(
        _In_ WDI_SET_RADIO_STATE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_RADIO_STATE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskSetRadioStateToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskSetRadioStateToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_RADIO_STATE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskSetRadioStateToIhv( _In_ WDI_SET_RADIO_STATE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskSetRadioStateFromIhv(
        _In_opt_ WDI_SET_RADIO_STATE_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskSetRadioStateFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_RADIO_STATE_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskSetRadioStateFromIhv( _In_ WDI_SET_RADIO_STATE_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskCreatePortToIhv(
        _In_ WDI_TASK_CREATE_PORT_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_CREATE_PORT_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskCreatePortToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskCreatePortToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_CREATE_PORT_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskCreatePortToIhv( _In_ WDI_TASK_CREATE_PORT_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskCreatePortFromIhv(
        _In_opt_ WDI_TASK_CREATE_PORT_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskCreatePortFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_CREATE_PORT_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskCreatePortFromIhv( _In_ WDI_TASK_CREATE_PORT_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskDeletePortToIhv(
        _In_ WDI_TASK_DELETE_PORT_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_DELETE_PORT_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskDeletePortToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskDeletePortToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_DELETE_PORT_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskDeletePortToIhv( _In_ WDI_TASK_DELETE_PORT_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskDeletePortFromIhv(
        _In_opt_ WDI_TASK_DELETE_PORT_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskDeletePortFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_DELETE_PORT_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskDeletePortFromIhv( _In_ WDI_TASK_DELETE_PORT_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskStartApToIhv(
        _In_ WDI_TASK_START_AP_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_START_AP_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskStartApToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskStartApToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_START_AP_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskStartApToIhv( _In_ WDI_TASK_START_AP_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskStartApFromIhv(
        _In_opt_ WDI_TASK_START_AP_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskStartApFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_START_AP_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskStartApFromIhv( _In_ WDI_TASK_START_AP_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskStopAp(
        _In_opt_ WDI_TASK_STOP_AP_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskStopAp(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_STOP_AP_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskStopAp( _In_ WDI_TASK_STOP_AP_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskSendApAssociationResponseToIhv(
        _In_ WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskSendApAssociationResponseToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskSendApAssociationResponseToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskSendApAssociationResponseToIhv( _In_ WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskSendApAssociationResponseFromIhv(
        _In_opt_ WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskSendApAssociationResponseFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskSendApAssociationResponseFromIhv( _In_ WDI_TASK_SEND_AP_ASSOCIATION_RESPONSE_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskSetReadyToReceiveIndications(
        _In_opt_ WDI_TASK_SET_READY_TO_RECEIVE_INDICATIONS_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskSetReadyToReceiveIndications(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_SET_READY_TO_RECEIVE_INDICATIONS_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskSetReadyToReceiveIndications( _In_ WDI_TASK_SET_READY_TO_RECEIVE_INDICATIONS_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetPowerStateToIhv(
        _In_ WDI_SET_POWER_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_POWER_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetPowerStateToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetPowerStateToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_POWER_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetPowerStateToIhv( _In_ WDI_SET_POWER_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetPowerStateFromIhv(
        _In_ WDI_SET_POWER_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_POWER_RESULTS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetPowerStateFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetPowerStateFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_POWER_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetPowerStateFromIhv( _In_ WDI_SET_POWER_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationDisconnectCompleteFromIhv(
        _In_opt_ WDI_INDICATION_DISCONNECT_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationDisconnectCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_DISCONNECT_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationDisconnectCompleteFromIhv( _In_ WDI_INDICATION_DISCONNECT_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationSetRadioStateCompleteFromIhv(
        _In_opt_ WDI_INDICATION_SET_RADIO_STATE_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationSetRadioStateCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_SET_RADIO_STATE_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationSetRadioStateCompleteFromIhv( _In_ WDI_INDICATION_SET_RADIO_STATE_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationSetReadyToReceiveIndicationsCompleteFromIhv(
        _In_opt_ WDI_INDICATION_SET_READY_TO_RECEIVE_INDICATIONS_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationSetReadyToReceiveIndicationsCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_SET_READY_TO_RECEIVE_INDICATIONS_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationSetReadyToReceiveIndicationsCompleteFromIhv( _In_ WDI_INDICATION_SET_READY_TO_RECEIVE_INDICATIONS_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetOperationMode(
        _In_opt_ WDI_SET_OPERATION_MODE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetOperationMode(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_OPERATION_MODE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetOperationMode( _In_ WDI_SET_OPERATION_MODE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetEndDwellTime(
        _In_opt_ WDI_SET_END_DWELL_TIME_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetEndDwellTime(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_END_DWELL_TIME_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetEndDwellTime( _In_ WDI_SET_END_DWELL_TIME_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetP2pListenStateToIhv(
        _In_ WDI_SET_P2P_LISTEN_STATE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_P2P_LISTEN_STATE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetP2pListenStateToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetP2pListenStateToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_P2P_LISTEN_STATE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetP2pListenStateToIhv( _In_ WDI_SET_P2P_LISTEN_STATE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetP2pListenStateFromIhv(
        _In_opt_ WDI_SET_P2P_LISTEN_STATE_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetP2pListenStateFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_P2P_LISTEN_STATE_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetP2pListenStateFromIhv( _In_ WDI_SET_P2P_LISTEN_STATE_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetPrivacyExemptionListToIhv(
        _In_ WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetPrivacyExemptionListToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetPrivacyExemptionListToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetPrivacyExemptionListToIhv( _In_ WDI_SET_PRIVACY_EXEMPTION_LIST_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetPrivacyExemptionListFromIhv(
        _In_opt_ WDI_SET_PRIVACY_EXEMPTION_LIST_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetPrivacyExemptionListFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_PRIVACY_EXEMPTION_LIST_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetPrivacyExemptionListFromIhv( _In_ WDI_SET_PRIVACY_EXEMPTION_LIST_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAddCipherKeysToIhv(
        _In_ WDI_SET_ADD_CIPHER_KEYS_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_ADD_CIPHER_KEYS_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetAddCipherKeysToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetAddCipherKeysToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_ADD_CIPHER_KEYS_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAddCipherKeysToIhv( _In_ WDI_SET_ADD_CIPHER_KEYS_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAddCipherKeysFromIhv(
        _In_opt_ WDI_SET_ADD_CIPHER_KEYS_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetAddCipherKeysFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_ADD_CIPHER_KEYS_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAddCipherKeysFromIhv( _In_ WDI_SET_ADD_CIPHER_KEYS_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetDeleteCipherKeysToIhv(
        _In_ WDI_SET_DELETE_CIPHER_KEYS_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_DELETE_CIPHER_KEYS_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetDeleteCipherKeysToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetDeleteCipherKeysToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_DELETE_CIPHER_KEYS_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetDeleteCipherKeysToIhv( _In_ WDI_SET_DELETE_CIPHER_KEYS_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetDeleteCipherKeysFromIhv(
        _In_opt_ WDI_SET_DELETE_CIPHER_KEYS_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetDeleteCipherKeysFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_DELETE_CIPHER_KEYS_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetDeleteCipherKeysFromIhv( _In_ WDI_SET_DELETE_CIPHER_KEYS_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetDefaultKeyIdToIhv(
        _In_ WDI_SET_DEFAULT_KEY_ID_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_DEFAULT_KEY_ID_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetDefaultKeyIdToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetDefaultKeyIdToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_DEFAULT_KEY_ID_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetDefaultKeyIdToIhv( _In_ WDI_SET_DEFAULT_KEY_ID_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetDefaultKeyIdFromIhv(
        _In_opt_ WDI_SET_DEFAULT_KEY_ID_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetDefaultKeyIdFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_DEFAULT_KEY_ID_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetDefaultKeyIdFromIhv( _In_ WDI_SET_DEFAULT_KEY_ID_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetConnectionQualityToIhv(
        _In_ WDI_SET_CONNECTION_QUALITY_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_CONNECTION_QUALITY_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetConnectionQualityToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetConnectionQualityToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_CONNECTION_QUALITY_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetConnectionQualityToIhv( _In_ WDI_SET_CONNECTION_QUALITY_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetConnectionQualityFromIhv(
        _In_opt_ WDI_SET_CONNECTION_QUALITY_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetConnectionQualityFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_CONNECTION_QUALITY_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetConnectionQualityFromIhv( _In_ WDI_SET_CONNECTION_QUALITY_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetStatisticsToIhv(
        _In_opt_ WDI_GET_STATISTICS_INPUTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiGetStatisticsToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_GET_STATISTICS_INPUTS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetStatisticsToIhv( _In_ WDI_GET_STATISTICS_INPUTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetStatisticsFromIhv(
        _In_ WDI_GET_STATISTICS_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_GET_STATISTICS_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiGetStatisticsFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiGetStatisticsFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_GET_STATISTICS_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetStatisticsFromIhv( _In_ WDI_GET_STATISTICS_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetReceivePacketFilterToIhv(
        _In_ WDI_SET_RECEIVE_PACKET_FILTER_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_RECEIVE_PACKET_FILTER_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetReceivePacketFilterToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetReceivePacketFilterToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_RECEIVE_PACKET_FILTER_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetReceivePacketFilterToIhv( _In_ WDI_SET_RECEIVE_PACKET_FILTER_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetReceivePacketFilterFromIhv(
        _In_opt_ WDI_SET_RECEIVE_PACKET_FILTER_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetReceivePacketFilterFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_RECEIVE_PACKET_FILTER_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetReceivePacketFilterFromIhv( _In_ WDI_SET_RECEIVE_PACKET_FILTER_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetAdapterCapabilitiesToIhv(
        _In_opt_ WDI_GET_ADAPTER_CAPABILITIES_INPUTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiGetAdapterCapabilitiesToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_GET_ADAPTER_CAPABILITIES_INPUTS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetAdapterCapabilitiesToIhv( _In_ WDI_GET_ADAPTER_CAPABILITIES_INPUTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetAdapterCapabilitiesFromIhv(
        _In_ WDI_GET_ADAPTER_CAPABILITIES_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_GET_ADAPTER_CAPABILITIES_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiGetAdapterCapabilitiesFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiGetAdapterCapabilitiesFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_GET_ADAPTER_CAPABILITIES_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetAdapterCapabilitiesFromIhv( _In_ WDI_GET_ADAPTER_CAPABILITIES_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetNetworkListOffloadToIhv(
        _In_ WDI_NETWORK_LIST_OFFLOAD_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_NETWORK_LIST_OFFLOAD_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetNetworkListOffloadToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetNetworkListOffloadToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_NETWORK_LIST_OFFLOAD_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetNetworkListOffloadToIhv( _In_ WDI_NETWORK_LIST_OFFLOAD_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetNetworkListOffloadFromIhv(
        _In_opt_ WDI_NETWORK_LIST_OFFLOAD_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetNetworkListOffloadFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_NETWORK_LIST_OFFLOAD_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetNetworkListOffloadFromIhv( _In_ WDI_NETWORK_LIST_OFFLOAD_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetReceiveCoalescingToIhv(
        _In_ WDI_SET_RECEIVE_COALESCING_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_RECEIVE_COALESCING_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetReceiveCoalescingToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetReceiveCoalescingToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_RECEIVE_COALESCING_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetReceiveCoalescingToIhv( _In_ WDI_SET_RECEIVE_COALESCING_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetReceiveCoalescingFromIhv(
        _In_opt_ WDI_SET_RECEIVE_COALESCING_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetReceiveCoalescingFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_RECEIVE_COALESCING_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetReceiveCoalescingFromIhv( _In_ WDI_SET_RECEIVE_COALESCING_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetBssEntryListToIhv(
        _In_ WDI_GET_BSS_ENTRY_LIST_UPDATE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_GET_BSS_ENTRY_LIST_UPDATE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiGetBssEntryListToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiGetBssEntryListToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_GET_BSS_ENTRY_LIST_UPDATE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetBssEntryListToIhv( _In_ WDI_GET_BSS_ENTRY_LIST_UPDATE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetBssEntryListFromIhv(
        _In_opt_ WDI_GET_BSS_ENTRY_LIST_UPDATE_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiGetBssEntryListFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_GET_BSS_ENTRY_LIST_UPDATE_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetBssEntryListFromIhv( _In_ WDI_GET_BSS_ENTRY_LIST_UPDATE_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetOweDhIeToIhv(
        _In_ WDI_SET_OWE_DH_IE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_OWE_DH_IE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetOweDhIeToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetOweDhIeToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_OWE_DH_IE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetOweDhIeToIhv( _In_ WDI_SET_OWE_DH_IE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetOweDhIeFromIhv(
        _In_opt_ WDI_SET_OWE_DH_IE_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetOweDhIeFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_OWE_DH_IE_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetOweDhIeFromIhv( _In_ WDI_SET_OWE_DH_IE_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationDisassociationFromIhv(
        _In_ WDI_INDICATION_DISASSOCIATION_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_DISASSOCIATION_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationDisassociationFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationDisassociationFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_DISASSOCIATION_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationDisassociationFromIhv( _In_ WDI_INDICATION_DISASSOCIATION_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationRoamingNeededFromIhv(
        _In_ WDI_INDICATION_ROAMING_NEEDED_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_ROAMING_NEEDED_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationRoamingNeededFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationRoamingNeededFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_ROAMING_NEEDED_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationRoamingNeededFromIhv( _In_ WDI_INDICATION_ROAMING_NEEDED_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationLinkStateChangeFromIhv(
        _In_ WDI_INDICATION_LINK_STATE_CHANGE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_LINK_STATE_CHANGE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationLinkStateChangeFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationLinkStateChangeFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_LINK_STATE_CHANGE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationLinkStateChangeFromIhv( _In_ WDI_INDICATION_LINK_STATE_CHANGE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationP2pActionFrameReceivedFromIhv(
        _In_ WDI_INDICATION_P2P_ACTION_FRAME_RECEIVED_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_P2P_ACTION_FRAME_RECEIVED_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationP2pActionFrameReceivedFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationP2pActionFrameReceivedFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_P2P_ACTION_FRAME_RECEIVED_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationP2pActionFrameReceivedFromIhv( _In_ WDI_INDICATION_P2P_ACTION_FRAME_RECEIVED_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationApAssociationRequestReceivedFromIhv(
        _In_ WDI_INDICATION_AP_ASSOCIATION_REQUEST_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_AP_ASSOCIATION_REQUEST_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationApAssociationRequestReceivedFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationApAssociationRequestReceivedFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_AP_ASSOCIATION_REQUEST_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationApAssociationRequestReceivedFromIhv( _In_ WDI_INDICATION_AP_ASSOCIATION_REQUEST_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationNloDiscoveryFromIhv(
        _In_ WDI_INDICATION_NLO_DISCOVERY_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_NLO_DISCOVERY_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationNloDiscoveryFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationNloDiscoveryFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_NLO_DISCOVERY_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationNloDiscoveryFromIhv( _In_ WDI_INDICATION_NLO_DISCOVERY_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationWakeReasonFromIhv(
        _In_ WDI_INDICATION_WAKE_REASON_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_WAKE_REASON_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationWakeReasonFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationWakeReasonFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_WAKE_REASON_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationWakeReasonFromIhv( _In_ WDI_INDICATION_WAKE_REASON_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationTkipMicFailureFromIhv(
        _In_ WDI_INDICATION_TKIP_MIC_FAILURE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_TKIP_MIC_FAILURE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationTkipMicFailureFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationTkipMicFailureFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_TKIP_MIC_FAILURE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationTkipMicFailureFromIhv( _In_ WDI_INDICATION_TKIP_MIC_FAILURE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationScanCompleteFromIhv(
        _In_opt_ WDI_INDICATION_SCAN_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationScanCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_SCAN_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationScanCompleteFromIhv( _In_ WDI_INDICATION_SCAN_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationP2pDiscoveryCompleteFromIhv(
        _In_opt_ WDI_INDICATION_P2P_DISCOVERY_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationP2pDiscoveryCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_P2P_DISCOVERY_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationP2pDiscoveryCompleteFromIhv( _In_ WDI_INDICATION_P2P_DISCOVERY_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationBssEntryListFromIhv(
        _In_ WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationBssEntryListFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationBssEntryListFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationBssEntryListFromIhv( _In_ WDI_INDICATION_BSS_ENTRY_LIST_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationDot11ResetCompleteFromIhv(
        _In_opt_ WDI_INDICATION_DOT11_RESET_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationDot11ResetCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_DOT11_RESET_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationDot11ResetCompleteFromIhv( _In_ WDI_INDICATION_DOT11_RESET_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationConnectCompleteFromIhv(
        _In_opt_ WDI_INDICATION_CONNECT_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationConnectCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_CONNECT_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationConnectCompleteFromIhv( _In_ WDI_INDICATION_CONNECT_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationP2pSendRequestActionFrameCompleteFromIhv(
        _In_ WDI_INDICATION_P2P_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_P2P_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationP2pSendRequestActionFrameCompleteFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationP2pSendRequestActionFrameCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_P2P_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationP2pSendRequestActionFrameCompleteFromIhv( _In_ WDI_INDICATION_P2P_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationP2pSendResponseActionFrameCompleteFromIhv(
        _In_ WDI_INDICATION_P2P_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_P2P_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationP2pSendResponseActionFrameCompleteFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationP2pSendResponseActionFrameCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_P2P_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationP2pSendResponseActionFrameCompleteFromIhv( _In_ WDI_INDICATION_P2P_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationRadioStatusFromIhv(
        _In_ WDI_INDICATION_RADIO_STATUS_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_RADIO_STATUS_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationRadioStatusFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationRadioStatusFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_RADIO_STATUS_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationRadioStatusFromIhv( _In_ WDI_INDICATION_RADIO_STATUS_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationCreatePortCompleteFromIhv(
        _In_ WDI_INDICATION_CREATE_PORT_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_CREATE_PORT_COMPLETE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationCreatePortCompleteFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationCreatePortCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_CREATE_PORT_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationCreatePortCompleteFromIhv( _In_ WDI_INDICATION_CREATE_PORT_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationDeletePortCompleteFromIhv(
        _In_opt_ WDI_INDICATION_DELETE_PORT_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationDeletePortCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_DELETE_PORT_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationDeletePortCompleteFromIhv( _In_ WDI_INDICATION_DELETE_PORT_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationStartApCompleteFromIhv(
        _In_opt_ WDI_INDICATION_START_AP_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationStartApCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_START_AP_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationStartApCompleteFromIhv( _In_ WDI_INDICATION_START_AP_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationStopApCompleteFromIhv(
        _In_opt_ WDI_INDICATION_STOP_AP_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationStopApCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_STOP_AP_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationStopApCompleteFromIhv( _In_ WDI_INDICATION_STOP_AP_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationStopApFromIhv(
        _In_ WDI_INDICATION_STOP_AP_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_STOP_AP_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationStopApFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationStopApFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_STOP_AP_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationStopApFromIhv( _In_ WDI_INDICATION_STOP_AP_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationCanSustainApFromIhv(
        _In_ WDI_INDICATION_CAN_SUSTAIN_AP_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_CAN_SUSTAIN_AP_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationCanSustainApFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationCanSustainApFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_CAN_SUSTAIN_AP_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationCanSustainApFromIhv( _In_ WDI_INDICATION_CAN_SUSTAIN_AP_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationSendApAssociationResponseCompleteFromIhv(
        _In_ WDI_INDICATION_SEND_AP_ASSOCIATION_RESPONSE_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_SEND_AP_ASSOCIATION_RESPONSE_COMPLETE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationSendApAssociationResponseCompleteFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationSendApAssociationResponseCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_SEND_AP_ASSOCIATION_RESPONSE_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationSendApAssociationResponseCompleteFromIhv( _In_ WDI_INDICATION_SEND_AP_ASSOCIATION_RESPONSE_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationAssociationResultFromIhv(
        _In_ WDI_INDICATION_ASSOCIATION_RESULT_LIST const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_ASSOCIATION_RESULT_LIST const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationAssociationResultFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationAssociationResultFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_ASSOCIATION_RESULT_LIST* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationAssociationResultFromIhv( _In_ WDI_INDICATION_ASSOCIATION_RESULT_LIST* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetAutoPowerSaveToIhv(
        _In_opt_ WDI_GET_AUTO_POWER_SAVE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiGetAutoPowerSaveToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_GET_AUTO_POWER_SAVE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetAutoPowerSaveToIhv( _In_ WDI_GET_AUTO_POWER_SAVE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetAutoPowerSaveFromIhv(
        _In_ WDI_GET_AUTO_POWER_SAVE_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_GET_AUTO_POWER_SAVE_RESULTS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiGetAutoPowerSaveFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiGetAutoPowerSaveFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_GET_AUTO_POWER_SAVE_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetAutoPowerSaveFromIhv( _In_ WDI_GET_AUTO_POWER_SAVE_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAddWolPatternToIhv(
        _In_ WDI_SET_ADD_WOL_PATTERN_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_ADD_WOL_PATTERN_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetAddWolPatternToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetAddWolPatternToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_ADD_WOL_PATTERN_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAddWolPatternToIhv( _In_ WDI_SET_ADD_WOL_PATTERN_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAddWolPatternFromIhv(
        _In_opt_ WDI_SET_ADD_WOL_PATTERN_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetAddWolPatternFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_ADD_WOL_PATTERN_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAddWolPatternFromIhv( _In_ WDI_SET_ADD_WOL_PATTERN_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetRemoveWolPatternToIhv(
        _In_ WDI_SET_REMOVE_WOL_PATTERN_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_REMOVE_WOL_PATTERN_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetRemoveWolPatternToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetRemoveWolPatternToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_REMOVE_WOL_PATTERN_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetRemoveWolPatternToIhv( _In_ WDI_SET_REMOVE_WOL_PATTERN_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetRemoveWolPatternFromIhv(
        _In_opt_ WDI_SET_REMOVE_WOL_PATTERN_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetRemoveWolPatternFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_REMOVE_WOL_PATTERN_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetRemoveWolPatternFromIhv( _In_ WDI_SET_REMOVE_WOL_PATTERN_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetMulticastListToIhv(
        _In_ WDI_SET_MULTICAST_LIST_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_MULTICAST_LIST_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetMulticastListToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetMulticastListToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_MULTICAST_LIST_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetMulticastListToIhv( _In_ WDI_SET_MULTICAST_LIST_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetMulticastListFromIhv(
        _In_opt_ WDI_SET_MULTICAST_LIST_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetMulticastListFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_MULTICAST_LIST_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetMulticastListFromIhv( _In_ WDI_SET_MULTICAST_LIST_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAddPmProtocolOffloadToIhv(
        _In_ WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetAddPmProtocolOffloadToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetAddPmProtocolOffloadToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAddPmProtocolOffloadToIhv( _In_ WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAddPmProtocolOffloadFromIhv(
        _In_opt_ WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetAddPmProtocolOffloadFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAddPmProtocolOffloadFromIhv( _In_ WDI_SET_ADD_PM_PROTOCOL_OFFLOAD_PARAMETERS_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetRemovePmProtocolOffloadToIhv(
        _In_ WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetRemovePmProtocolOffloadToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetRemovePmProtocolOffloadToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetRemovePmProtocolOffloadToIhv( _In_ WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetRemovePmProtocolOffloadFromIhv(
        _In_opt_ WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetRemovePmProtocolOffloadFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetRemovePmProtocolOffloadFromIhv( _In_ WDI_SET_REMOVE_PM_PROTOCOL_OFFLOAD_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationP2pGroupOperatingChannelFromIhv(
        _In_ WDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationP2pGroupOperatingChannelFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationP2pGroupOperatingChannelFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationP2pGroupOperatingChannelFromIhv( _In_ WDI_INDICATION_P2P_GROUP_OPERATING_CHANNEL_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationP2pOperatingChannelAttributesFromIhv(
        _In_ WDI_INDICATION_P2P_OPERATING_CHANNEL_ATTRIBUTES_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_P2P_OPERATING_CHANNEL_ATTRIBUTES_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationP2pOperatingChannelAttributesFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationP2pOperatingChannelAttributesFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_P2P_OPERATING_CHANNEL_ATTRIBUTES_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationP2pOperatingChannelAttributesFromIhv( _In_ WDI_INDICATION_P2P_OPERATING_CHANNEL_ATTRIBUTES_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAdapterConfigurationToIhv(
        _In_ WDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetAdapterConfigurationToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetAdapterConfigurationToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAdapterConfigurationToIhv( _In_ WDI_SET_FIRMWARE_CONFIGURATION_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAdapterConfigurationFromIhv(
        _In_opt_ WDI_SET_FIRMWARE_CONFIGURATION_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetAdapterConfigurationFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_FIRMWARE_CONFIGURATION_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAdapterConfigurationFromIhv( _In_ WDI_SET_FIRMWARE_CONFIGURATION_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetReceiveCoalescingMatchCountToIhv(
        _In_opt_ WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_INPUTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiGetReceiveCoalescingMatchCountToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_INPUTS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetReceiveCoalescingMatchCountToIhv( _In_ WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_INPUTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetReceiveCoalescingMatchCountFromIhv(
        _In_ WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiGetReceiveCoalescingMatchCountFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiGetReceiveCoalescingMatchCountFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetReceiveCoalescingMatchCountFromIhv( _In_ WDI_GET_RECEIVE_COALESCING_MATCH_COUNT_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetClearReceiveCoalescingToIhv(
        _In_ WDI_SET_CLEAR_RECEIVE_COALESCING_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_CLEAR_RECEIVE_COALESCING_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetClearReceiveCoalescingToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetClearReceiveCoalescingToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_CLEAR_RECEIVE_COALESCING_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetClearReceiveCoalescingToIhv( _In_ WDI_SET_CLEAR_RECEIVE_COALESCING_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetClearReceiveCoalescingFromIhv(
        _In_opt_ WDI_SET_CLEAR_RECEIVE_COALESCING_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetClearReceiveCoalescingFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_CLEAR_RECEIVE_COALESCING_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetClearReceiveCoalescingFromIhv( _In_ WDI_SET_CLEAR_RECEIVE_COALESCING_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetPmProtocolOffloadToIhv(
        _In_ WDI_GET_PM_PROTOCOL_OFFLOAD_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_GET_PM_PROTOCOL_OFFLOAD_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiGetPmProtocolOffloadToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiGetPmProtocolOffloadToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_GET_PM_PROTOCOL_OFFLOAD_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetPmProtocolOffloadToIhv( _In_ WDI_GET_PM_PROTOCOL_OFFLOAD_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetPmProtocolOffloadFromIhv(
        _In_ WDI_GET_PM_PROTOCOL_OFFLOAD_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_GET_PM_PROTOCOL_OFFLOAD_RESULTS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiGetPmProtocolOffloadFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiGetPmProtocolOffloadFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_GET_PM_PROTOCOL_OFFLOAD_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetPmProtocolOffloadFromIhv( _In_ WDI_GET_PM_PROTOCOL_OFFLOAD_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAdvertisementInformationToIhv(
        _In_ WDI_SET_ADVERTISEMENT_INFORMATION_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_ADVERTISEMENT_INFORMATION_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetAdvertisementInformationToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetAdvertisementInformationToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_ADVERTISEMENT_INFORMATION_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAdvertisementInformationToIhv( _In_ WDI_SET_ADVERTISEMENT_INFORMATION_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAdvertisementInformationFromIhv(
        _In_opt_ WDI_SET_ADVERTISEMENT_INFORMATION_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetAdvertisementInformationFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_ADVERTISEMENT_INFORMATION_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAdvertisementInformationFromIhv( _In_ WDI_SET_ADVERTISEMENT_INFORMATION_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskChangeOperationModeToIhv(
        _In_ WDI_TASK_CHANGE_OPERATION_MODE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_CHANGE_OPERATION_MODE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskChangeOperationModeToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskChangeOperationModeToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_CHANGE_OPERATION_MODE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskChangeOperationModeToIhv( _In_ WDI_TASK_CHANGE_OPERATION_MODE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskChangeOperationModeFromIhv(
        _In_opt_ WDI_TASK_CHANGE_OPERATION_MODE_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskChangeOperationModeFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_CHANGE_OPERATION_MODE_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskChangeOperationModeFromIhv( _In_ WDI_TASK_CHANGE_OPERATION_MODE_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationChangeOperationModeCompleteFromIhv(
        _In_opt_ WDI_INDICATION_CHANGE_OPERATION_MODE_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationChangeOperationModeCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_CHANGE_OPERATION_MODE_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationChangeOperationModeCompleteFromIhv( _In_ WDI_INDICATION_CHANGE_OPERATION_MODE_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIhvRequest(
        _In_ WDI_IHV_REQUEST_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_IHV_REQUEST_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIhvRequest( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIhvRequest(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_IHV_REQUEST_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIhvRequest( _In_ WDI_IHV_REQUEST_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationIhvEventFromIhv(
        _In_ WDI_INDICATION_IHV_EVENT_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_IHV_EVENT_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationIhvEventFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationIhvEventFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_IHV_EVENT_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationIhvEventFromIhv( _In_ WDI_INDICATION_IHV_EVENT_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationOpenCompleteFromIhv(
        _In_opt_ WDI_INDICATION_OPEN_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationOpenCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_OPEN_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationOpenCompleteFromIhv( _In_ WDI_INDICATION_OPEN_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationCloseCompleteFromIhv(
        _In_opt_ WDI_INDICATION_CLOSE_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationCloseCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_CLOSE_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationCloseCompleteFromIhv( _In_ WDI_INDICATION_CLOSE_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetFlushBssEntry(
        _In_opt_ WDI_SET_FLUSH_BSS_ENTRY_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetFlushBssEntry(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_FLUSH_BSS_ENTRY_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetFlushBssEntry( _In_ WDI_SET_FLUSH_BSS_ENTRY_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationAssociationParametersRequestFromIhv(
        _In_ WDI_INDICATION_ASSOCIATION_PARAMS_REQUEST_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_ASSOCIATION_PARAMS_REQUEST_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationAssociationParametersRequestFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationAssociationParametersRequestFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_ASSOCIATION_PARAMS_REQUEST_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationAssociationParametersRequestFromIhv( _In_ WDI_INDICATION_ASSOCIATION_PARAMS_REQUEST_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAssociationParametersToIhv(
        _In_ WDI_SET_ASSOCIATION_PARAMS_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_ASSOCIATION_PARAMS_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetAssociationParametersToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetAssociationParametersToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_ASSOCIATION_PARAMS_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAssociationParametersToIhv( _In_ WDI_SET_ASSOCIATION_PARAMS_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetAssociationParametersFromIhv(
        _In_opt_ WDI_SET_ASSOCIATION_PARAMS_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetAssociationParametersFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_ASSOCIATION_PARAMS_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetAssociationParametersFromIhv( _In_ WDI_SET_ASSOCIATION_PARAMS_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskRoamToIhv(
        _In_ WDI_TASK_ROAM_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_ROAM_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskRoamToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskRoamToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_ROAM_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskRoamToIhv( _In_ WDI_TASK_ROAM_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskRoamFromIhv(
        _In_opt_ WDI_TASK_ROAM_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskRoamFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_ROAM_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskRoamFromIhv( _In_ WDI_TASK_ROAM_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationRoamCompleteFromIhv(
        _In_opt_ WDI_INDICATION_ROAM_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationRoamCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_ROAM_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationRoamCompleteFromIhv( _In_ WDI_INDICATION_ROAM_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskSendRequestActionFrameToIhv(
        _In_ WDI_TASK_SEND_REQUEST_ACTION_FRAME_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_SEND_REQUEST_ACTION_FRAME_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskSendRequestActionFrameToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskSendRequestActionFrameToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_SEND_REQUEST_ACTION_FRAME_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskSendRequestActionFrameToIhv( _In_ WDI_TASK_SEND_REQUEST_ACTION_FRAME_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskSendRequestActionFrameFromIhv(
        _In_opt_ WDI_TASK_SEND_REQUEST_ACTION_FRAME_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskSendRequestActionFrameFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_SEND_REQUEST_ACTION_FRAME_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskSendRequestActionFrameFromIhv( _In_ WDI_TASK_SEND_REQUEST_ACTION_FRAME_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationActionFrameReceivedFromIhv(
        _In_ WDI_INDICATION_ACTION_FRAME_RECEIVED_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_ACTION_FRAME_RECEIVED_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationActionFrameReceivedFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationActionFrameReceivedFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_ACTION_FRAME_RECEIVED_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationActionFrameReceivedFromIhv( _In_ WDI_INDICATION_ACTION_FRAME_RECEIVED_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationTaskOffloadCurrentConfigFromIhv(
        _In_ WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationTaskOffloadCurrentConfigFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationTaskOffloadCurrentConfigFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationTaskOffloadCurrentConfigFromIhv( _In_ WDI_INDICATION_TASK_OFFLOAD_CURRENT_CONFIG_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskSendResponseActionFrameToIhv(
        _In_ WDI_TASK_SEND_RESPONSE_ACTION_FRAME_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_SEND_RESPONSE_ACTION_FRAME_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskSendResponseActionFrameToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskSendResponseActionFrameToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_SEND_RESPONSE_ACTION_FRAME_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskSendResponseActionFrameToIhv( _In_ WDI_TASK_SEND_RESPONSE_ACTION_FRAME_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskSendResponseActionFrameFromIhv(
        _In_opt_ WDI_TASK_SEND_RESPONSE_ACTION_FRAME_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskSendResponseActionFrameFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_SEND_RESPONSE_ACTION_FRAME_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskSendResponseActionFrameFromIhv( _In_ WDI_TASK_SEND_RESPONSE_ACTION_FRAME_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationSendRequestActionFrameCompleteFromIhv(
        _In_opt_ WDI_INDICATION_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationSendRequestActionFrameCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationSendRequestActionFrameCompleteFromIhv( _In_ WDI_INDICATION_SEND_REQUEST_ACTION_FRAME_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationSendResponseActionFrameCompleteFromIhv(
        _In_opt_ WDI_INDICATION_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationSendResponseActionFrameCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationSendResponseActionFrameCompleteFromIhv( _In_ WDI_INDICATION_SEND_RESPONSE_ACTION_FRAME_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetNextActionFrameDialogTokenToIhv(
        _In_opt_ WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_INPUTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiGetNextActionFrameDialogTokenToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_INPUTS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetNextActionFrameDialogTokenToIhv( _In_ WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_INPUTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetNextActionFrameDialogTokenFromIhv(
        _In_ WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiGetNextActionFrameDialogTokenFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiGetNextActionFrameDialogTokenFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetNextActionFrameDialogTokenFromIhv( _In_ WDI_GET_NEXT_ACTION_FRAME_DIALOG_TOKEN_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetP2pWpsEnabledToIhv(
        _In_ WDI_SET_P2P_WPS_ENABLED_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_P2P_WPS_ENABLED_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetP2pWpsEnabledToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetP2pWpsEnabledToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_P2P_WPS_ENABLED_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetP2pWpsEnabledToIhv( _In_ WDI_SET_P2P_WPS_ENABLED_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetP2pWpsEnabledFromIhv(
        _In_opt_ WDI_SET_P2P_WPS_ENABLED_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetP2pWpsEnabledFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_P2P_WPS_ENABLED_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetP2pWpsEnabledFromIhv( _In_ WDI_SET_P2P_WPS_ENABLED_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskIhvToIhv(
        _In_ WDI_TASK_IHV_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_IHV_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskIhvToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskIhvToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_IHV_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskIhvToIhv( _In_ WDI_TASK_IHV_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskIhvFromIhv(
        _In_opt_ WDI_TASK_IHV_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskIhvFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_IHV_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskIhvFromIhv( _In_ WDI_TASK_IHV_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskRequestFtmToIhv(
        _In_ WDI_TASK_REQUEST_FTM_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_REQUEST_FTM_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTaskRequestFtmToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTaskRequestFtmToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_REQUEST_FTM_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskRequestFtmToIhv( _In_ WDI_TASK_REQUEST_FTM_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTaskRequestFtmFromIhv(
        _In_opt_ WDI_TASK_REQUEST_FTM_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTaskRequestFtmFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_REQUEST_FTM_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTaskRequestFtmFromIhv( _In_ WDI_TASK_REQUEST_FTM_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationRequestFtmCompleteFromIhv(
        _In_ WDI_INDICATION_REQUEST_FTM_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_REQUEST_FTM_COMPLETE_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationRequestFtmCompleteFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationRequestFtmCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_REQUEST_FTM_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationRequestFtmCompleteFromIhv( _In_ WDI_INDICATION_REQUEST_FTM_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationIhvTaskRequestFromIhv(
        _In_ WDI_INDICATION_IHV_TASK_REQUEST_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_IHV_TASK_REQUEST_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationIhvTaskRequestFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationIhvTaskRequestFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_IHV_TASK_REQUEST_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationIhvTaskRequestFromIhv( _In_ WDI_INDICATION_IHV_TASK_REQUEST_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationIhvTaskCompleteFromIhv(
        _In_opt_ WDI_INDICATION_IHV_TASK_COMPLETE_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationIhvTaskCompleteFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_IHV_TASK_COMPLETE_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationIhvTaskCompleteFromIhv( _In_ WDI_INDICATION_IHV_TASK_COMPLETE_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiAbortTaskToIhv(
        _In_ WDI_TASK_ABORT_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_ABORT_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiAbortTaskToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiAbortTaskToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_ABORT_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiAbortTaskToIhv( _In_ WDI_TASK_ABORT_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiAbortTaskFromIhv(
        _In_opt_ WDI_TASK_ABORT_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiAbortTaskFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TASK_ABORT_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiAbortTaskFromIhv( _In_ WDI_TASK_ABORT_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetTcpOffloadParametersToIhv(
        _In_ WDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetTcpOffloadParametersToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetTcpOffloadParametersToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetTcpOffloadParametersToIhv( _In_ WDI_SET_TCP_OFFLOAD_PARAMS_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetTcpOffloadParametersFromIhv(
        _In_opt_ WDI_SET_TCP_OFFLOAD_PARAMS_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetTcpOffloadParametersFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_TCP_OFFLOAD_PARAMS_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetTcpOffloadParametersFromIhv( _In_ WDI_SET_TCP_OFFLOAD_PARAMS_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTcpRscStatisticsToIhv(
        _In_opt_ WDI_TCP_RSC_STATISTICS_INPUTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiTcpRscStatisticsToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_TCP_RSC_STATISTICS_INPUTS* pParsedMessage );
    void __stdcall CleanupParsedWdiTcpRscStatisticsToIhv( _In_ WDI_TCP_RSC_STATISTICS_INPUTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTcpRscStatisticsFromIhv(
        _In_ WDI_TCP_RSC_STATISTICS_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TCP_RSC_STATISTICS_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTcpRscStatisticsFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTcpRscStatisticsFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TCP_RSC_STATISTICS_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTcpRscStatisticsFromIhv( _In_ WDI_TCP_RSC_STATISTICS_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetEncapsulationOffloadToIhv(
        _In_ WDI_SET_ENCAPSULATION_OFFLOAD_PARAMS_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_ENCAPSULATION_OFFLOAD_PARAMS_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetEncapsulationOffloadToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetEncapsulationOffloadToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_ENCAPSULATION_OFFLOAD_PARAMS_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetEncapsulationOffloadToIhv( _In_ WDI_SET_ENCAPSULATION_OFFLOAD_PARAMS_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetEncapsulationOffloadFromIhv(
        _In_opt_ WDI_SET_ENCAPSULATION_OFFLOAD_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetEncapsulationOffloadFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_ENCAPSULATION_OFFLOAD_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetEncapsulationOffloadFromIhv( _In_ WDI_SET_ENCAPSULATION_OFFLOAD_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationFtAssocParamsNeededFromIhv(
        _In_ WDI_INDICATION_FT_ASSOC_PARAMS_NEEDED_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_FT_ASSOC_PARAMS_NEEDED_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationFtAssocParamsNeededFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationFtAssocParamsNeededFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_FT_ASSOC_PARAMS_NEEDED_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationFtAssocParamsNeededFromIhv( _In_ WDI_INDICATION_FT_ASSOC_PARAMS_NEEDED_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetFastBssTransitionParametersToIhv(
        _In_ WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_COMMAND const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_COMMAND const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetFastBssTransitionParametersToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetFastBssTransitionParametersToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_COMMAND* pParsedMessage );
    void __stdcall CleanupParsedWdiSetFastBssTransitionParametersToIhv( _In_ WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_COMMAND* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetFastBssTransitionParametersFromIhv(
        _In_opt_ WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetFastBssTransitionParametersFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetFastBssTransitionParametersFromIhv( _In_ WDI_SET_FAST_BSS_TRANSITION_PARAMETERS_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationSaeAuthParamsNeededFromIhv(
        _In_ WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationSaeAuthParamsNeededFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationSaeAuthParamsNeededFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationSaeAuthParamsNeededFromIhv( _In_ WDI_INDICATION_SAE_AUTH_PARAMS_NEEDED_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetSaeAuthParamsToIhv(
        _In_ WDI_SET_SAE_AUTH_PARAMS_COMMAND const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_SAE_AUTH_PARAMS_COMMAND const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetSaeAuthParamsToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetSaeAuthParamsToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_SAE_AUTH_PARAMS_COMMAND* pParsedMessage );
    void __stdcall CleanupParsedWdiSetSaeAuthParamsToIhv( _In_ WDI_SET_SAE_AUTH_PARAMS_COMMAND* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetSaeAuthParamsFromIhv(
        _In_opt_ WDI_SET_SAE_AUTH_PARAMS_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetSaeAuthParamsFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_SAE_AUTH_PARAMS_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetSaeAuthParamsFromIhv( _In_ WDI_SET_SAE_AUTH_PARAMS_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationFirmwareStalledFromIhv(
        _In_opt_ WDI_INDICATION_FIRMWARE_STALLED_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiIndicationFirmwareStalledFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_INDICATION_FIRMWARE_STALLED_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationFirmwareStalledFromIhv( _In_ WDI_INDICATION_FIRMWARE_STALLED_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetNeighborReportEntriesToIhv(
        _In_ WDI_SET_NEIGHBOR_REPORT_ENTRIES_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_NEIGHBOR_REPORT_ENTRIES_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetNeighborReportEntriesToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetNeighborReportEntriesToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_NEIGHBOR_REPORT_ENTRIES_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetNeighborReportEntriesToIhv( _In_ WDI_SET_NEIGHBOR_REPORT_ENTRIES_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetNeighborReportEntriesFromIhv(
        _In_opt_ WDI_SET_NEIGHBOR_REPORT_ENTRIES_RESULTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiSetNeighborReportEntriesFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_SET_NEIGHBOR_REPORT_ENTRIES_RESULTS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetNeighborReportEntriesFromIhv( _In_ WDI_SET_NEIGHBOR_REPORT_ENTRIES_RESULTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetSupportedDeviceServicesToIhv(
        _In_opt_ WDI_GET_SUPPORTED_DEVICE_SERVICES_INPUTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );

    NDIS_STATUS __stdcall ParseWdiGetSupportedDeviceServicesToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_opt_ WDI_GET_SUPPORTED_DEVICE_SERVICES_INPUTS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetSupportedDeviceServicesToIhv( _In_ WDI_GET_SUPPORTED_DEVICE_SERVICES_INPUTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiGetSupportedDeviceServicesFromIhv(
        _In_ WDI_GET_SUPPORTED_DEVICE_SERVICES_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_GET_SUPPORTED_DEVICE_SERVICES_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiGetSupportedDeviceServicesFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiGetSupportedDeviceServicesFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_GET_SUPPORTED_DEVICE_SERVICES_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiGetSupportedDeviceServicesFromIhv( _In_ WDI_GET_SUPPORTED_DEVICE_SERVICES_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiDeviceServiceCommandToIhv(
        _In_ WDI_DEVICE_SERVICE_COMMAND_INPUTS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_DEVICE_SERVICE_COMMAND_INPUTS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiDeviceServiceCommandToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiDeviceServiceCommandToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_DEVICE_SERVICE_COMMAND_INPUTS* pParsedMessage );
    void __stdcall CleanupParsedWdiDeviceServiceCommandToIhv( _In_ WDI_DEVICE_SERVICE_COMMAND_INPUTS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiDeviceServiceCommandFromIhv(
        _In_ WDI_DEVICE_SERVICE_COMMAND_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_DEVICE_SERVICE_COMMAND_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiDeviceServiceCommandFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiDeviceServiceCommandFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_DEVICE_SERVICE_COMMAND_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiDeviceServiceCommandFromIhv( _In_ WDI_DEVICE_SERVICE_COMMAND_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationCipherKeyUpdatedFromIhv(
        _In_ WDI_INDICATION_CIPHER_KEY_UPDATED_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_CIPHER_KEY_UPDATED_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationCipherKeyUpdatedFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationCipherKeyUpdatedFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_CIPHER_KEY_UPDATED_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationCipherKeyUpdatedFromIhv( _In_ WDI_INDICATION_CIPHER_KEY_UPDATED_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationDeviceServiceEventFromIhv(
        _In_ WDI_INDICATION_DEVICE_SERVICE_EVENT_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_DEVICE_SERVICE_EVENT_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationDeviceServiceEventFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationDeviceServiceEventFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_DEVICE_SERVICE_EVENT_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationDeviceServiceEventFromIhv( _In_ WDI_INDICATION_DEVICE_SERVICE_EVENT_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiIndicationSecondaryStaConnectivityFromIhv(
        _In_ WDI_INDICATION_SECONDARY_STA_CONNECTIVITY_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_INDICATION_SECONDARY_STA_CONNECTIVITY_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiIndicationSecondaryStaConnectivityFromIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiIndicationSecondaryStaConnectivityFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_INDICATION_SECONDARY_STA_CONNECTIVITY_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiIndicationSecondaryStaConnectivityFromIhv( _In_ WDI_INDICATION_SECONDARY_STA_CONNECTIVITY_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiSetLocationPrivacyToIhv(
        _In_ WDI_SET_LOCATION_PRIVACY_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_SET_LOCATION_PRIVACY_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiSetLocationPrivacyToIhv( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiSetLocationPrivacyToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_SET_LOCATION_PRIVACY_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiSetLocationPrivacyToIhv( _In_ WDI_SET_LOCATION_PRIVACY_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall GenerateWdiTestTask(
        _In_ WDI_TASK_TEST_PARAMETERS const * pInput,
        _In_ ULONG ReservedHeaderLength,
        _In_ PCTLV_CONTEXT Context,
        _Out_ ULONG* pBufferLength,
        _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer );
#ifdef __cplusplus
    extern "C++" inline NDIS_STATUS __stdcall Generate( _In_ WDI_TASK_TEST_PARAMETERS const * pInput, _In_ ULONG ReservedHeaderLength, _In_ PCTLV_CONTEXT Context, _Out_ ULONG* pBufferLength, _Outptr_result_buffer_( *pBufferLength ) UINT8** ppBuffer )
    {
        return GenerateWdiTestTask( pInput, ReservedHeaderLength, Context, pBufferLength, ppBuffer );
    }
#endif

    NDIS_STATUS __stdcall ParseWdiTestTask(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ PCTLV_CONTEXT Context,
        _Out_ WDI_TASK_TEST_PARAMETERS* pParsedMessage );
    void __stdcall CleanupParsedWdiTestTask( _In_ WDI_TASK_TEST_PARAMETERS* pParsedMessage );

    NDIS_STATUS __stdcall ParseToIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ USHORT MessageId,
        _In_ PCTLV_CONTEXT Context,
        _Outptr_result_nullonfailure_ PVOID* ppParsedOutput );

    NDIS_STATUS __stdcall FreeParsedToIhv(
        _In_ USHORT MessageId,
        _In_opt_ PVOID pParsedOutput );

    NDIS_STATUS __stdcall ParseFromIhv(
        _In_ ULONG BufferLength,
        _In_reads_bytes_( BufferLength ) UINT8 const * pBuffer,
        _In_ USHORT MessageId,
        _In_ PCTLV_CONTEXT Context,
        _Outptr_result_nullonfailure_ PVOID* ppParsedOutput );

    NDIS_STATUS __stdcall FreeParsedFromIhv(
        _In_ USHORT MessageId,
        _In_opt_ PVOID pParsedOutput );

#ifdef __cplusplus
}
#endif

#ifndef _WDI_NO_TLV_P_G_ALIASES
#ifdef _WDI_WIFI_SYS

#define GenerateWdiTaskScan GenerateWdiTaskScanToIhv
#define ParseWdiTaskScan ParseWdiTaskScanFromIhv
#define CleanupParsedWdiTaskScan CleanupParsedWdiTaskScanFromIhv
#define GenerateWdiTaskP2pDiscover GenerateWdiTaskP2pDiscoverToIhv
#define ParseWdiTaskP2pDiscover ParseWdiTaskP2pDiscoverFromIhv
#define CleanupParsedWdiTaskP2pDiscover CleanupParsedWdiTaskP2pDiscoverFromIhv
#define GenerateWdiSetP2pStartBackgroundDiscovery GenerateWdiSetP2pStartBackgroundDiscoveryToIhv
#define ParseWdiSetP2pStartBackgroundDiscovery ParseWdiSetP2pStartBackgroundDiscoveryFromIhv
#define CleanupParsedWdiSetP2pStartBackgroundDiscovery CleanupParsedWdiSetP2pStartBackgroundDiscoveryFromIhv
#define GenerateWdiTaskConnect GenerateWdiTaskConnectToIhv
#define ParseWdiTaskConnect ParseWdiTaskConnectFromIhv
#define CleanupParsedWdiTaskConnect CleanupParsedWdiTaskConnectFromIhv
#define GenerateWdiTaskDot11Reset GenerateWdiTaskDot11ResetToIhv
#define ParseWdiTaskDot11Reset ParseWdiTaskDot11ResetFromIhv
#define CleanupParsedWdiTaskDot11Reset CleanupParsedWdiTaskDot11ResetFromIhv
#define GenerateWdiTaskDisconnect GenerateWdiTaskDisconnectToIhv
#define ParseWdiTaskDisconnect ParseWdiTaskDisconnectFromIhv
#define CleanupParsedWdiTaskDisconnect CleanupParsedWdiTaskDisconnectFromIhv
#define GenerateWdiTaskP2pSendRequestActionFrame GenerateWdiTaskP2pSendRequestActionFrameToIhv
#define ParseWdiTaskP2pSendRequestActionFrame ParseWdiTaskP2pSendRequestActionFrameFromIhv
#define CleanupParsedWdiTaskP2pSendRequestActionFrame CleanupParsedWdiTaskP2pSendRequestActionFrameFromIhv
#define GenerateWdiTaskP2pSendResponseActionFrame GenerateWdiTaskP2pSendResponseActionFrameToIhv
#define ParseWdiTaskP2pSendResponseActionFrame ParseWdiTaskP2pSendResponseActionFrameFromIhv
#define CleanupParsedWdiTaskP2pSendResponseActionFrame CleanupParsedWdiTaskP2pSendResponseActionFrameFromIhv
#define GenerateWdiTaskSetRadioState GenerateWdiTaskSetRadioStateToIhv
#define ParseWdiTaskSetRadioState ParseWdiTaskSetRadioStateFromIhv
#define CleanupParsedWdiTaskSetRadioState CleanupParsedWdiTaskSetRadioStateFromIhv
#define GenerateWdiTaskCreatePort GenerateWdiTaskCreatePortToIhv
#define ParseWdiTaskCreatePort ParseWdiTaskCreatePortFromIhv
#define CleanupParsedWdiTaskCreatePort CleanupParsedWdiTaskCreatePortFromIhv
#define GenerateWdiTaskDeletePort GenerateWdiTaskDeletePortToIhv
#define ParseWdiTaskDeletePort ParseWdiTaskDeletePortFromIhv
#define CleanupParsedWdiTaskDeletePort CleanupParsedWdiTaskDeletePortFromIhv
#define GenerateWdiTaskStartAp GenerateWdiTaskStartApToIhv
#define ParseWdiTaskStartAp ParseWdiTaskStartApFromIhv
#define CleanupParsedWdiTaskStartAp CleanupParsedWdiTaskStartApFromIhv
#define GenerateWdiTaskSendApAssociationResponse GenerateWdiTaskSendApAssociationResponseToIhv
#define ParseWdiTaskSendApAssociationResponse ParseWdiTaskSendApAssociationResponseFromIhv
#define CleanupParsedWdiTaskSendApAssociationResponse CleanupParsedWdiTaskSendApAssociationResponseFromIhv
#define GenerateWdiSetPowerState GenerateWdiSetPowerStateToIhv
#define ParseWdiSetPowerState ParseWdiSetPowerStateFromIhv
#define CleanupParsedWdiSetPowerState CleanupParsedWdiSetPowerStateFromIhv
#define ParseWdiIndicationDisconnectComplete ParseWdiIndicationDisconnectCompleteFromIhv
#define CleanupParsedWdiIndicationDisconnectComplete CleanupParsedWdiIndicationDisconnectCompleteFromIhv
#define ParseWdiIndicationSetRadioStateComplete ParseWdiIndicationSetRadioStateCompleteFromIhv
#define CleanupParsedWdiIndicationSetRadioStateComplete CleanupParsedWdiIndicationSetRadioStateCompleteFromIhv
#define ParseWdiIndicationSetReadyToReceiveIndicationsComplete ParseWdiIndicationSetReadyToReceiveIndicationsCompleteFromIhv
#define CleanupParsedWdiIndicationSetReadyToReceiveIndicationsComplete CleanupParsedWdiIndicationSetReadyToReceiveIndicationsCompleteFromIhv
#define GenerateWdiSetP2pListenState GenerateWdiSetP2pListenStateToIhv
#define ParseWdiSetP2pListenState ParseWdiSetP2pListenStateFromIhv
#define CleanupParsedWdiSetP2pListenState CleanupParsedWdiSetP2pListenStateFromIhv
#define GenerateWdiSetPrivacyExemptionList GenerateWdiSetPrivacyExemptionListToIhv
#define ParseWdiSetPrivacyExemptionList ParseWdiSetPrivacyExemptionListFromIhv
#define CleanupParsedWdiSetPrivacyExemptionList CleanupParsedWdiSetPrivacyExemptionListFromIhv
#define GenerateWdiSetAddCipherKeys GenerateWdiSetAddCipherKeysToIhv
#define ParseWdiSetAddCipherKeys ParseWdiSetAddCipherKeysFromIhv
#define CleanupParsedWdiSetAddCipherKeys CleanupParsedWdiSetAddCipherKeysFromIhv
#define GenerateWdiSetDeleteCipherKeys GenerateWdiSetDeleteCipherKeysToIhv
#define ParseWdiSetDeleteCipherKeys ParseWdiSetDeleteCipherKeysFromIhv
#define CleanupParsedWdiSetDeleteCipherKeys CleanupParsedWdiSetDeleteCipherKeysFromIhv
#define GenerateWdiSetDefaultKeyId GenerateWdiSetDefaultKeyIdToIhv
#define ParseWdiSetDefaultKeyId ParseWdiSetDefaultKeyIdFromIhv
#define CleanupParsedWdiSetDefaultKeyId CleanupParsedWdiSetDefaultKeyIdFromIhv
#define GenerateWdiSetConnectionQuality GenerateWdiSetConnectionQualityToIhv
#define ParseWdiSetConnectionQuality ParseWdiSetConnectionQualityFromIhv
#define CleanupParsedWdiSetConnectionQuality CleanupParsedWdiSetConnectionQualityFromIhv
#define GenerateWdiGetStatistics GenerateWdiGetStatisticsToIhv
#define ParseWdiGetStatistics ParseWdiGetStatisticsFromIhv
#define CleanupParsedWdiGetStatistics CleanupParsedWdiGetStatisticsFromIhv
#define GenerateWdiSetReceivePacketFilter GenerateWdiSetReceivePacketFilterToIhv
#define ParseWdiSetReceivePacketFilter ParseWdiSetReceivePacketFilterFromIhv
#define CleanupParsedWdiSetReceivePacketFilter CleanupParsedWdiSetReceivePacketFilterFromIhv
#define GenerateWdiGetAdapterCapabilities GenerateWdiGetAdapterCapabilitiesToIhv
#define ParseWdiGetAdapterCapabilities ParseWdiGetAdapterCapabilitiesFromIhv
#define CleanupParsedWdiGetAdapterCapabilities CleanupParsedWdiGetAdapterCapabilitiesFromIhv
#define GenerateWdiSetNetworkListOffload GenerateWdiSetNetworkListOffloadToIhv
#define ParseWdiSetNetworkListOffload ParseWdiSetNetworkListOffloadFromIhv
#define CleanupParsedWdiSetNetworkListOffload CleanupParsedWdiSetNetworkListOffloadFromIhv
#define GenerateWdiSetReceiveCoalescing GenerateWdiSetReceiveCoalescingToIhv
#define ParseWdiSetReceiveCoalescing ParseWdiSetReceiveCoalescingFromIhv
#define CleanupParsedWdiSetReceiveCoalescing CleanupParsedWdiSetReceiveCoalescingFromIhv
#define GenerateWdiGetBssEntryList GenerateWdiGetBssEntryListToIhv
#define ParseWdiGetBssEntryList ParseWdiGetBssEntryListFromIhv
#define CleanupParsedWdiGetBssEntryList CleanupParsedWdiGetBssEntryListFromIhv
#define GenerateWdiSetOweDhIe GenerateWdiSetOweDhIeToIhv
#define ParseWdiSetOweDhIe ParseWdiSetOweDhIeFromIhv
#define CleanupParsedWdiSetOweDhIe CleanupParsedWdiSetOweDhIeFromIhv
#define ParseWdiIndicationDisassociation ParseWdiIndicationDisassociationFromIhv
#define CleanupParsedWdiIndicationDisassociation CleanupParsedWdiIndicationDisassociationFromIhv
#define ParseWdiIndicationRoamingNeeded ParseWdiIndicationRoamingNeededFromIhv
#define CleanupParsedWdiIndicationRoamingNeeded CleanupParsedWdiIndicationRoamingNeededFromIhv
#define ParseWdiIndicationLinkStateChange ParseWdiIndicationLinkStateChangeFromIhv
#define CleanupParsedWdiIndicationLinkStateChange CleanupParsedWdiIndicationLinkStateChangeFromIhv
#define ParseWdiIndicationP2pActionFrameReceived ParseWdiIndicationP2pActionFrameReceivedFromIhv
#define CleanupParsedWdiIndicationP2pActionFrameReceived CleanupParsedWdiIndicationP2pActionFrameReceivedFromIhv
#define ParseWdiIndicationApAssociationRequestReceived ParseWdiIndicationApAssociationRequestReceivedFromIhv
#define CleanupParsedWdiIndicationApAssociationRequestReceived CleanupParsedWdiIndicationApAssociationRequestReceivedFromIhv
#define ParseWdiIndicationNloDiscovery ParseWdiIndicationNloDiscoveryFromIhv
#define CleanupParsedWdiIndicationNloDiscovery CleanupParsedWdiIndicationNloDiscoveryFromIhv
#define ParseWdiIndicationWakeReason ParseWdiIndicationWakeReasonFromIhv
#define CleanupParsedWdiIndicationWakeReason CleanupParsedWdiIndicationWakeReasonFromIhv
#define ParseWdiIndicationTkipMicFailure ParseWdiIndicationTkipMicFailureFromIhv
#define CleanupParsedWdiIndicationTkipMicFailure CleanupParsedWdiIndicationTkipMicFailureFromIhv
#define ParseWdiIndicationScanComplete ParseWdiIndicationScanCompleteFromIhv
#define CleanupParsedWdiIndicationScanComplete CleanupParsedWdiIndicationScanCompleteFromIhv
#define ParseWdiIndicationP2pDiscoveryComplete ParseWdiIndicationP2pDiscoveryCompleteFromIhv
#define CleanupParsedWdiIndicationP2pDiscoveryComplete CleanupParsedWdiIndicationP2pDiscoveryCompleteFromIhv
#define ParseWdiIndicationBssEntryList ParseWdiIndicationBssEntryListFromIhv
#define CleanupParsedWdiIndicationBssEntryList CleanupParsedWdiIndicationBssEntryListFromIhv
#define ParseWdiIndicationDot11ResetComplete ParseWdiIndicationDot11ResetCompleteFromIhv
#define CleanupParsedWdiIndicationDot11ResetComplete CleanupParsedWdiIndicationDot11ResetCompleteFromIhv
#define ParseWdiIndicationConnectComplete ParseWdiIndicationConnectCompleteFromIhv
#define CleanupParsedWdiIndicationConnectComplete CleanupParsedWdiIndicationConnectCompleteFromIhv
#define ParseWdiIndicationP2pSendRequestActionFrameComplete ParseWdiIndicationP2pSendRequestActionFrameCompleteFromIhv
#define CleanupParsedWdiIndicationP2pSendRequestActionFrameComplete CleanupParsedWdiIndicationP2pSendRequestActionFrameCompleteFromIhv
#define ParseWdiIndicationP2pSendResponseActionFrameComplete ParseWdiIndicationP2pSendResponseActionFrameCompleteFromIhv
#define CleanupParsedWdiIndicationP2pSendResponseActionFrameComplete CleanupParsedWdiIndicationP2pSendResponseActionFrameCompleteFromIhv
#define ParseWdiIndicationRadioStatus ParseWdiIndicationRadioStatusFromIhv
#define CleanupParsedWdiIndicationRadioStatus CleanupParsedWdiIndicationRadioStatusFromIhv
#define ParseWdiIndicationCreatePortComplete ParseWdiIndicationCreatePortCompleteFromIhv
#define CleanupParsedWdiIndicationCreatePortComplete CleanupParsedWdiIndicationCreatePortCompleteFromIhv
#define ParseWdiIndicationDeletePortComplete ParseWdiIndicationDeletePortCompleteFromIhv
#define CleanupParsedWdiIndicationDeletePortComplete CleanupParsedWdiIndicationDeletePortCompleteFromIhv
#define ParseWdiIndicationStartApComplete ParseWdiIndicationStartApCompleteFromIhv
#define CleanupParsedWdiIndicationStartApComplete CleanupParsedWdiIndicationStartApCompleteFromIhv
#define ParseWdiIndicationStopApComplete ParseWdiIndicationStopApCompleteFromIhv
#define CleanupParsedWdiIndicationStopApComplete CleanupParsedWdiIndicationStopApCompleteFromIhv
#define ParseWdiIndicationStopAp ParseWdiIndicationStopApFromIhv
#define CleanupParsedWdiIndicationStopAp CleanupParsedWdiIndicationStopApFromIhv
#define ParseWdiIndicationCanSustainAp ParseWdiIndicationCanSustainApFromIhv
#define CleanupParsedWdiIndicationCanSustainAp CleanupParsedWdiIndicationCanSustainApFromIhv
#define ParseWdiIndicationSendApAssociationResponseComplete ParseWdiIndicationSendApAssociationResponseCompleteFromIhv
#define CleanupParsedWdiIndicationSendApAssociationResponseComplete CleanupParsedWdiIndicationSendApAssociationResponseCompleteFromIhv
#define ParseWdiIndicationAssociationResult ParseWdiIndicationAssociationResultFromIhv
#define CleanupParsedWdiIndicationAssociationResult CleanupParsedWdiIndicationAssociationResultFromIhv
#define GenerateWdiGetAutoPowerSave GenerateWdiGetAutoPowerSaveToIhv
#define ParseWdiGetAutoPowerSave ParseWdiGetAutoPowerSaveFromIhv
#define CleanupParsedWdiGetAutoPowerSave CleanupParsedWdiGetAutoPowerSaveFromIhv
#define GenerateWdiSetAddWolPattern GenerateWdiSetAddWolPatternToIhv
#define ParseWdiSetAddWolPattern ParseWdiSetAddWolPatternFromIhv
#define CleanupParsedWdiSetAddWolPattern CleanupParsedWdiSetAddWolPatternFromIhv
#define GenerateWdiSetRemoveWolPattern GenerateWdiSetRemoveWolPatternToIhv
#define ParseWdiSetRemoveWolPattern ParseWdiSetRemoveWolPatternFromIhv
#define CleanupParsedWdiSetRemoveWolPattern CleanupParsedWdiSetRemoveWolPatternFromIhv
#define GenerateWdiSetMulticastList GenerateWdiSetMulticastListToIhv
#define ParseWdiSetMulticastList ParseWdiSetMulticastListFromIhv
#define CleanupParsedWdiSetMulticastList CleanupParsedWdiSetMulticastListFromIhv
#define GenerateWdiSetAddPmProtocolOffload GenerateWdiSetAddPmProtocolOffloadToIhv
#define ParseWdiSetAddPmProtocolOffload ParseWdiSetAddPmProtocolOffloadFromIhv
#define CleanupParsedWdiSetAddPmProtocolOffload CleanupParsedWdiSetAddPmProtocolOffloadFromIhv
#define GenerateWdiSetRemovePmProtocolOffload GenerateWdiSetRemovePmProtocolOffloadToIhv
#define ParseWdiSetRemovePmProtocolOffload ParseWdiSetRemovePmProtocolOffloadFromIhv
#define CleanupParsedWdiSetRemovePmProtocolOffload CleanupParsedWdiSetRemovePmProtocolOffloadFromIhv
#define ParseWdiIndicationP2pGroupOperatingChannel ParseWdiIndicationP2pGroupOperatingChannelFromIhv
#define CleanupParsedWdiIndicationP2pGroupOperatingChannel CleanupParsedWdiIndicationP2pGroupOperatingChannelFromIhv
#define ParseWdiIndicationP2pOperatingChannelAttributes ParseWdiIndicationP2pOperatingChannelAttributesFromIhv
#define CleanupParsedWdiIndicationP2pOperatingChannelAttributes CleanupParsedWdiIndicationP2pOperatingChannelAttributesFromIhv
#define GenerateWdiSetAdapterConfiguration GenerateWdiSetAdapterConfigurationToIhv
#define ParseWdiSetAdapterConfiguration ParseWdiSetAdapterConfigurationFromIhv
#define CleanupParsedWdiSetAdapterConfiguration CleanupParsedWdiSetAdapterConfigurationFromIhv
#define GenerateWdiGetReceiveCoalescingMatchCount GenerateWdiGetReceiveCoalescingMatchCountToIhv
#define ParseWdiGetReceiveCoalescingMatchCount ParseWdiGetReceiveCoalescingMatchCountFromIhv
#define CleanupParsedWdiGetReceiveCoalescingMatchCount CleanupParsedWdiGetReceiveCoalescingMatchCountFromIhv
#define GenerateWdiSetClearReceiveCoalescing GenerateWdiSetClearReceiveCoalescingToIhv
#define ParseWdiSetClearReceiveCoalescing ParseWdiSetClearReceiveCoalescingFromIhv
#define CleanupParsedWdiSetClearReceiveCoalescing CleanupParsedWdiSetClearReceiveCoalescingFromIhv
#define GenerateWdiGetPmProtocolOffload GenerateWdiGetPmProtocolOffloadToIhv
#define ParseWdiGetPmProtocolOffload ParseWdiGetPmProtocolOffloadFromIhv
#define CleanupParsedWdiGetPmProtocolOffload CleanupParsedWdiGetPmProtocolOffloadFromIhv
#define GenerateWdiSetAdvertisementInformation GenerateWdiSetAdvertisementInformationToIhv
#define ParseWdiSetAdvertisementInformation ParseWdiSetAdvertisementInformationFromIhv
#define CleanupParsedWdiSetAdvertisementInformation CleanupParsedWdiSetAdvertisementInformationFromIhv
#define GenerateWdiTaskChangeOperationMode GenerateWdiTaskChangeOperationModeToIhv
#define ParseWdiTaskChangeOperationMode ParseWdiTaskChangeOperationModeFromIhv
#define CleanupParsedWdiTaskChangeOperationMode CleanupParsedWdiTaskChangeOperationModeFromIhv
#define ParseWdiIndicationChangeOperationModeComplete ParseWdiIndicationChangeOperationModeCompleteFromIhv
#define CleanupParsedWdiIndicationChangeOperationModeComplete CleanupParsedWdiIndicationChangeOperationModeCompleteFromIhv
#define ParseWdiIndicationIhvEvent ParseWdiIndicationIhvEventFromIhv
#define CleanupParsedWdiIndicationIhvEvent CleanupParsedWdiIndicationIhvEventFromIhv
#define ParseWdiIndicationOpenComplete ParseWdiIndicationOpenCompleteFromIhv
#define CleanupParsedWdiIndicationOpenComplete CleanupParsedWdiIndicationOpenCompleteFromIhv
#define ParseWdiIndicationCloseComplete ParseWdiIndicationCloseCompleteFromIhv
#define CleanupParsedWdiIndicationCloseComplete CleanupParsedWdiIndicationCloseCompleteFromIhv
#define ParseWdiIndicationAssociationParametersRequest ParseWdiIndicationAssociationParametersRequestFromIhv
#define CleanupParsedWdiIndicationAssociationParametersRequest CleanupParsedWdiIndicationAssociationParametersRequestFromIhv
#define GenerateWdiSetAssociationParameters GenerateWdiSetAssociationParametersToIhv
#define ParseWdiSetAssociationParameters ParseWdiSetAssociationParametersFromIhv
#define CleanupParsedWdiSetAssociationParameters CleanupParsedWdiSetAssociationParametersFromIhv
#define GenerateWdiTaskRoam GenerateWdiTaskRoamToIhv
#define ParseWdiTaskRoam ParseWdiTaskRoamFromIhv
#define CleanupParsedWdiTaskRoam CleanupParsedWdiTaskRoamFromIhv
#define ParseWdiIndicationRoamComplete ParseWdiIndicationRoamCompleteFromIhv
#define CleanupParsedWdiIndicationRoamComplete CleanupParsedWdiIndicationRoamCompleteFromIhv
#define GenerateWdiTaskSendRequestActionFrame GenerateWdiTaskSendRequestActionFrameToIhv
#define ParseWdiTaskSendRequestActionFrame ParseWdiTaskSendRequestActionFrameFromIhv
#define CleanupParsedWdiTaskSendRequestActionFrame CleanupParsedWdiTaskSendRequestActionFrameFromIhv
#define ParseWdiIndicationActionFrameReceived ParseWdiIndicationActionFrameReceivedFromIhv
#define CleanupParsedWdiIndicationActionFrameReceived CleanupParsedWdiIndicationActionFrameReceivedFromIhv
#define ParseWdiIndicationTaskOffloadCurrentConfig ParseWdiIndicationTaskOffloadCurrentConfigFromIhv
#define CleanupParsedWdiIndicationTaskOffloadCurrentConfig CleanupParsedWdiIndicationTaskOffloadCurrentConfigFromIhv
#define GenerateWdiTaskSendResponseActionFrame GenerateWdiTaskSendResponseActionFrameToIhv
#define ParseWdiTaskSendResponseActionFrame ParseWdiTaskSendResponseActionFrameFromIhv
#define CleanupParsedWdiTaskSendResponseActionFrame CleanupParsedWdiTaskSendResponseActionFrameFromIhv
#define ParseWdiIndicationSendRequestActionFrameComplete ParseWdiIndicationSendRequestActionFrameCompleteFromIhv
#define CleanupParsedWdiIndicationSendRequestActionFrameComplete CleanupParsedWdiIndicationSendRequestActionFrameCompleteFromIhv
#define ParseWdiIndicationSendResponseActionFrameComplete ParseWdiIndicationSendResponseActionFrameCompleteFromIhv
#define CleanupParsedWdiIndicationSendResponseActionFrameComplete CleanupParsedWdiIndicationSendResponseActionFrameCompleteFromIhv
#define GenerateWdiGetNextActionFrameDialogToken GenerateWdiGetNextActionFrameDialogTokenToIhv
#define ParseWdiGetNextActionFrameDialogToken ParseWdiGetNextActionFrameDialogTokenFromIhv
#define CleanupParsedWdiGetNextActionFrameDialogToken CleanupParsedWdiGetNextActionFrameDialogTokenFromIhv
#define GenerateWdiSetP2pWpsEnabled GenerateWdiSetP2pWpsEnabledToIhv
#define ParseWdiSetP2pWpsEnabled ParseWdiSetP2pWpsEnabledFromIhv
#define CleanupParsedWdiSetP2pWpsEnabled CleanupParsedWdiSetP2pWpsEnabledFromIhv
#define GenerateWdiTaskIhv GenerateWdiTaskIhvToIhv
#define ParseWdiTaskIhv ParseWdiTaskIhvFromIhv
#define CleanupParsedWdiTaskIhv CleanupParsedWdiTaskIhvFromIhv
#define GenerateWdiTaskRequestFtm GenerateWdiTaskRequestFtmToIhv
#define ParseWdiTaskRequestFtm ParseWdiTaskRequestFtmFromIhv
#define CleanupParsedWdiTaskRequestFtm CleanupParsedWdiTaskRequestFtmFromIhv
#define ParseWdiIndicationRequestFtmComplete ParseWdiIndicationRequestFtmCompleteFromIhv
#define CleanupParsedWdiIndicationRequestFtmComplete CleanupParsedWdiIndicationRequestFtmCompleteFromIhv
#define ParseWdiIndicationIhvTaskRequest ParseWdiIndicationIhvTaskRequestFromIhv
#define CleanupParsedWdiIndicationIhvTaskRequest CleanupParsedWdiIndicationIhvTaskRequestFromIhv
#define ParseWdiIndicationIhvTaskComplete ParseWdiIndicationIhvTaskCompleteFromIhv
#define CleanupParsedWdiIndicationIhvTaskComplete CleanupParsedWdiIndicationIhvTaskCompleteFromIhv
#define GenerateWdiAbortTask GenerateWdiAbortTaskToIhv
#define ParseWdiAbortTask ParseWdiAbortTaskFromIhv
#define CleanupParsedWdiAbortTask CleanupParsedWdiAbortTaskFromIhv
#define GenerateWdiSetTcpOffloadParameters GenerateWdiSetTcpOffloadParametersToIhv
#define ParseWdiSetTcpOffloadParameters ParseWdiSetTcpOffloadParametersFromIhv
#define CleanupParsedWdiSetTcpOffloadParameters CleanupParsedWdiSetTcpOffloadParametersFromIhv
#define GenerateWdiTcpRscStatistics GenerateWdiTcpRscStatisticsToIhv
#define ParseWdiTcpRscStatistics ParseWdiTcpRscStatisticsFromIhv
#define CleanupParsedWdiTcpRscStatistics CleanupParsedWdiTcpRscStatisticsFromIhv
#define GenerateWdiSetEncapsulationOffload GenerateWdiSetEncapsulationOffloadToIhv
#define ParseWdiSetEncapsulationOffload ParseWdiSetEncapsulationOffloadFromIhv
#define CleanupParsedWdiSetEncapsulationOffload CleanupParsedWdiSetEncapsulationOffloadFromIhv
#define ParseWdiIndicationFtAssocParamsNeeded ParseWdiIndicationFtAssocParamsNeededFromIhv
#define CleanupParsedWdiIndicationFtAssocParamsNeeded CleanupParsedWdiIndicationFtAssocParamsNeededFromIhv
#define GenerateWdiSetFastBssTransitionParameters GenerateWdiSetFastBssTransitionParametersToIhv
#define ParseWdiSetFastBssTransitionParameters ParseWdiSetFastBssTransitionParametersFromIhv
#define CleanupParsedWdiSetFastBssTransitionParameters CleanupParsedWdiSetFastBssTransitionParametersFromIhv
#define ParseWdiIndicationSaeAuthParamsNeeded ParseWdiIndicationSaeAuthParamsNeededFromIhv
#define CleanupParsedWdiIndicationSaeAuthParamsNeeded CleanupParsedWdiIndicationSaeAuthParamsNeededFromIhv
#define GenerateWdiSetSaeAuthParams GenerateWdiSetSaeAuthParamsToIhv
#define ParseWdiSetSaeAuthParams ParseWdiSetSaeAuthParamsFromIhv
#define CleanupParsedWdiSetSaeAuthParams CleanupParsedWdiSetSaeAuthParamsFromIhv
#define ParseWdiIndicationFirmwareStalled ParseWdiIndicationFirmwareStalledFromIhv
#define CleanupParsedWdiIndicationFirmwareStalled CleanupParsedWdiIndicationFirmwareStalledFromIhv
#define GenerateWdiSetNeighborReportEntries GenerateWdiSetNeighborReportEntriesToIhv
#define ParseWdiSetNeighborReportEntries ParseWdiSetNeighborReportEntriesFromIhv
#define CleanupParsedWdiSetNeighborReportEntries CleanupParsedWdiSetNeighborReportEntriesFromIhv
#define GenerateWdiGetSupportedDeviceServices GenerateWdiGetSupportedDeviceServicesToIhv
#define ParseWdiGetSupportedDeviceServices ParseWdiGetSupportedDeviceServicesFromIhv
#define CleanupParsedWdiGetSupportedDeviceServices CleanupParsedWdiGetSupportedDeviceServicesFromIhv
#define GenerateWdiDeviceServiceCommand GenerateWdiDeviceServiceCommandToIhv
#define ParseWdiDeviceServiceCommand ParseWdiDeviceServiceCommandFromIhv
#define CleanupParsedWdiDeviceServiceCommand CleanupParsedWdiDeviceServiceCommandFromIhv
#define ParseWdiIndicationCipherKeyUpdated ParseWdiIndicationCipherKeyUpdatedFromIhv
#define CleanupParsedWdiIndicationCipherKeyUpdated CleanupParsedWdiIndicationCipherKeyUpdatedFromIhv
#define ParseWdiIndicationDeviceServiceEvent ParseWdiIndicationDeviceServiceEventFromIhv
#define CleanupParsedWdiIndicationDeviceServiceEvent CleanupParsedWdiIndicationDeviceServiceEventFromIhv
#define ParseWdiIndicationSecondaryStaConnectivity ParseWdiIndicationSecondaryStaConnectivityFromIhv
#define CleanupParsedWdiIndicationSecondaryStaConnectivity CleanupParsedWdiIndicationSecondaryStaConnectivityFromIhv
#define GenerateWdiSetLocationPrivacy GenerateWdiSetLocationPrivacyToIhv
#define Parse ParseFromIhv
#define FreeParsed FreeParsedFromIhv

#else

#define ParseWdiTaskScan ParseWdiTaskScanToIhv
#define CleanupParsedWdiTaskScan CleanupParsedWdiTaskScanToIhv
#define GenerateWdiTaskScan GenerateWdiTaskScanFromIhv
#define ParseWdiTaskP2pDiscover ParseWdiTaskP2pDiscoverToIhv
#define CleanupParsedWdiTaskP2pDiscover CleanupParsedWdiTaskP2pDiscoverToIhv
#define GenerateWdiTaskP2pDiscover GenerateWdiTaskP2pDiscoverFromIhv
#define ParseWdiSetP2pStartBackgroundDiscovery ParseWdiSetP2pStartBackgroundDiscoveryToIhv
#define CleanupParsedWdiSetP2pStartBackgroundDiscovery CleanupParsedWdiSetP2pStartBackgroundDiscoveryToIhv
#define GenerateWdiSetP2pStartBackgroundDiscovery GenerateWdiSetP2pStartBackgroundDiscoveryFromIhv
#define ParseWdiTaskConnect ParseWdiTaskConnectToIhv
#define CleanupParsedWdiTaskConnect CleanupParsedWdiTaskConnectToIhv
#define GenerateWdiTaskConnect GenerateWdiTaskConnectFromIhv
#define ParseWdiTaskDot11Reset ParseWdiTaskDot11ResetToIhv
#define CleanupParsedWdiTaskDot11Reset CleanupParsedWdiTaskDot11ResetToIhv
#define GenerateWdiTaskDot11Reset GenerateWdiTaskDot11ResetFromIhv
#define ParseWdiTaskDisconnect ParseWdiTaskDisconnectToIhv
#define CleanupParsedWdiTaskDisconnect CleanupParsedWdiTaskDisconnectToIhv
#define GenerateWdiTaskDisconnect GenerateWdiTaskDisconnectFromIhv
#define ParseWdiTaskP2pSendRequestActionFrame ParseWdiTaskP2pSendRequestActionFrameToIhv
#define CleanupParsedWdiTaskP2pSendRequestActionFrame CleanupParsedWdiTaskP2pSendRequestActionFrameToIhv
#define GenerateWdiTaskP2pSendRequestActionFrame GenerateWdiTaskP2pSendRequestActionFrameFromIhv
#define ParseWdiTaskP2pSendResponseActionFrame ParseWdiTaskP2pSendResponseActionFrameToIhv
#define CleanupParsedWdiTaskP2pSendResponseActionFrame CleanupParsedWdiTaskP2pSendResponseActionFrameToIhv
#define GenerateWdiTaskP2pSendResponseActionFrame GenerateWdiTaskP2pSendResponseActionFrameFromIhv
#define ParseWdiTaskSetRadioState ParseWdiTaskSetRadioStateToIhv
#define CleanupParsedWdiTaskSetRadioState CleanupParsedWdiTaskSetRadioStateToIhv
#define GenerateWdiTaskSetRadioState GenerateWdiTaskSetRadioStateFromIhv
#define ParseWdiTaskCreatePort ParseWdiTaskCreatePortToIhv
#define CleanupParsedWdiTaskCreatePort CleanupParsedWdiTaskCreatePortToIhv
#define GenerateWdiTaskCreatePort GenerateWdiTaskCreatePortFromIhv
#define ParseWdiTaskDeletePort ParseWdiTaskDeletePortToIhv
#define CleanupParsedWdiTaskDeletePort CleanupParsedWdiTaskDeletePortToIhv
#define GenerateWdiTaskDeletePort GenerateWdiTaskDeletePortFromIhv
#define ParseWdiTaskStartAp ParseWdiTaskStartApToIhv
#define CleanupParsedWdiTaskStartAp CleanupParsedWdiTaskStartApToIhv
#define GenerateWdiTaskStartAp GenerateWdiTaskStartApFromIhv
#define ParseWdiTaskSendApAssociationResponse ParseWdiTaskSendApAssociationResponseToIhv
#define CleanupParsedWdiTaskSendApAssociationResponse CleanupParsedWdiTaskSendApAssociationResponseToIhv
#define GenerateWdiTaskSendApAssociationResponse GenerateWdiTaskSendApAssociationResponseFromIhv
#define ParseWdiSetPowerState ParseWdiSetPowerStateToIhv
#define CleanupParsedWdiSetPowerState CleanupParsedWdiSetPowerStateToIhv
#define GenerateWdiSetPowerState GenerateWdiSetPowerStateFromIhv
#define GenerateWdiIndicationDisconnectComplete GenerateWdiIndicationDisconnectCompleteFromIhv
#define GenerateWdiIndicationSetRadioStateComplete GenerateWdiIndicationSetRadioStateCompleteFromIhv
#define GenerateWdiIndicationSetReadyToReceiveIndicationsComplete GenerateWdiIndicationSetReadyToReceiveIndicationsCompleteFromIhv
#define ParseWdiSetP2pListenState ParseWdiSetP2pListenStateToIhv
#define CleanupParsedWdiSetP2pListenState CleanupParsedWdiSetP2pListenStateToIhv
#define GenerateWdiSetP2pListenState GenerateWdiSetP2pListenStateFromIhv
#define ParseWdiSetPrivacyExemptionList ParseWdiSetPrivacyExemptionListToIhv
#define CleanupParsedWdiSetPrivacyExemptionList CleanupParsedWdiSetPrivacyExemptionListToIhv
#define GenerateWdiSetPrivacyExemptionList GenerateWdiSetPrivacyExemptionListFromIhv
#define ParseWdiSetAddCipherKeys ParseWdiSetAddCipherKeysToIhv
#define CleanupParsedWdiSetAddCipherKeys CleanupParsedWdiSetAddCipherKeysToIhv
#define GenerateWdiSetAddCipherKeys GenerateWdiSetAddCipherKeysFromIhv
#define ParseWdiSetDeleteCipherKeys ParseWdiSetDeleteCipherKeysToIhv
#define CleanupParsedWdiSetDeleteCipherKeys CleanupParsedWdiSetDeleteCipherKeysToIhv
#define GenerateWdiSetDeleteCipherKeys GenerateWdiSetDeleteCipherKeysFromIhv
#define ParseWdiSetDefaultKeyId ParseWdiSetDefaultKeyIdToIhv
#define CleanupParsedWdiSetDefaultKeyId CleanupParsedWdiSetDefaultKeyIdToIhv
#define GenerateWdiSetDefaultKeyId GenerateWdiSetDefaultKeyIdFromIhv
#define ParseWdiSetConnectionQuality ParseWdiSetConnectionQualityToIhv
#define CleanupParsedWdiSetConnectionQuality CleanupParsedWdiSetConnectionQualityToIhv
#define GenerateWdiSetConnectionQuality GenerateWdiSetConnectionQualityFromIhv
#define ParseWdiGetStatistics ParseWdiGetStatisticsToIhv
#define CleanupParsedWdiGetStatistics CleanupParsedWdiGetStatisticsToIhv
#define GenerateWdiGetStatistics GenerateWdiGetStatisticsFromIhv
#define ParseWdiSetReceivePacketFilter ParseWdiSetReceivePacketFilterToIhv
#define CleanupParsedWdiSetReceivePacketFilter CleanupParsedWdiSetReceivePacketFilterToIhv
#define GenerateWdiSetReceivePacketFilter GenerateWdiSetReceivePacketFilterFromIhv
#define ParseWdiGetAdapterCapabilities ParseWdiGetAdapterCapabilitiesToIhv
#define CleanupParsedWdiGetAdapterCapabilities CleanupParsedWdiGetAdapterCapabilitiesToIhv
#define GenerateWdiGetAdapterCapabilities GenerateWdiGetAdapterCapabilitiesFromIhv
#define ParseWdiSetNetworkListOffload ParseWdiSetNetworkListOffloadToIhv
#define CleanupParsedWdiSetNetworkListOffload CleanupParsedWdiSetNetworkListOffloadToIhv
#define GenerateWdiSetNetworkListOffload GenerateWdiSetNetworkListOffloadFromIhv
#define ParseWdiSetReceiveCoalescing ParseWdiSetReceiveCoalescingToIhv
#define CleanupParsedWdiSetReceiveCoalescing CleanupParsedWdiSetReceiveCoalescingToIhv
#define GenerateWdiSetReceiveCoalescing GenerateWdiSetReceiveCoalescingFromIhv
#define ParseWdiGetBssEntryList ParseWdiGetBssEntryListToIhv
#define CleanupParsedWdiGetBssEntryList CleanupParsedWdiGetBssEntryListToIhv
#define GenerateWdiGetBssEntryList GenerateWdiGetBssEntryListFromIhv
#define ParseWdiSetOweDhIe ParseWdiSetOweDhIeToIhv
#define CleanupParsedWdiSetOweDhIe CleanupParsedWdiSetOweDhIeToIhv
#define GenerateWdiSetOweDhIe GenerateWdiSetOweDhIeFromIhv
#define GenerateWdiIndicationDisassociation GenerateWdiIndicationDisassociationFromIhv
#define GenerateWdiIndicationRoamingNeeded GenerateWdiIndicationRoamingNeededFromIhv
#define GenerateWdiIndicationLinkStateChange GenerateWdiIndicationLinkStateChangeFromIhv
#define GenerateWdiIndicationP2pActionFrameReceived GenerateWdiIndicationP2pActionFrameReceivedFromIhv
#define GenerateWdiIndicationApAssociationRequestReceived GenerateWdiIndicationApAssociationRequestReceivedFromIhv
#define GenerateWdiIndicationNloDiscovery GenerateWdiIndicationNloDiscoveryFromIhv
#define GenerateWdiIndicationWakeReason GenerateWdiIndicationWakeReasonFromIhv
#define GenerateWdiIndicationTkipMicFailure GenerateWdiIndicationTkipMicFailureFromIhv
#define GenerateWdiIndicationScanComplete GenerateWdiIndicationScanCompleteFromIhv
#define GenerateWdiIndicationP2pDiscoveryComplete GenerateWdiIndicationP2pDiscoveryCompleteFromIhv
#define GenerateWdiIndicationBssEntryList GenerateWdiIndicationBssEntryListFromIhv
#define GenerateWdiIndicationDot11ResetComplete GenerateWdiIndicationDot11ResetCompleteFromIhv
#define GenerateWdiIndicationConnectComplete GenerateWdiIndicationConnectCompleteFromIhv
#define GenerateWdiIndicationP2pSendRequestActionFrameComplete GenerateWdiIndicationP2pSendRequestActionFrameCompleteFromIhv
#define GenerateWdiIndicationP2pSendResponseActionFrameComplete GenerateWdiIndicationP2pSendResponseActionFrameCompleteFromIhv
#define GenerateWdiIndicationRadioStatus GenerateWdiIndicationRadioStatusFromIhv
#define GenerateWdiIndicationCreatePortComplete GenerateWdiIndicationCreatePortCompleteFromIhv
#define GenerateWdiIndicationDeletePortComplete GenerateWdiIndicationDeletePortCompleteFromIhv
#define GenerateWdiIndicationStartApComplete GenerateWdiIndicationStartApCompleteFromIhv
#define GenerateWdiIndicationStopApComplete GenerateWdiIndicationStopApCompleteFromIhv
#define GenerateWdiIndicationStopAp GenerateWdiIndicationStopApFromIhv
#define GenerateWdiIndicationCanSustainAp GenerateWdiIndicationCanSustainApFromIhv
#define GenerateWdiIndicationSendApAssociationResponseComplete GenerateWdiIndicationSendApAssociationResponseCompleteFromIhv
#define GenerateWdiIndicationAssociationResult GenerateWdiIndicationAssociationResultFromIhv
#define ParseWdiGetAutoPowerSave ParseWdiGetAutoPowerSaveToIhv
#define CleanupParsedWdiGetAutoPowerSave CleanupParsedWdiGetAutoPowerSaveToIhv
#define GenerateWdiGetAutoPowerSave GenerateWdiGetAutoPowerSaveFromIhv
#define ParseWdiSetAddWolPattern ParseWdiSetAddWolPatternToIhv
#define CleanupParsedWdiSetAddWolPattern CleanupParsedWdiSetAddWolPatternToIhv
#define GenerateWdiSetAddWolPattern GenerateWdiSetAddWolPatternFromIhv
#define ParseWdiSetRemoveWolPattern ParseWdiSetRemoveWolPatternToIhv
#define CleanupParsedWdiSetRemoveWolPattern CleanupParsedWdiSetRemoveWolPatternToIhv
#define GenerateWdiSetRemoveWolPattern GenerateWdiSetRemoveWolPatternFromIhv
#define ParseWdiSetMulticastList ParseWdiSetMulticastListToIhv
#define CleanupParsedWdiSetMulticastList CleanupParsedWdiSetMulticastListToIhv
#define GenerateWdiSetMulticastList GenerateWdiSetMulticastListFromIhv
#define ParseWdiSetAddPmProtocolOffload ParseWdiSetAddPmProtocolOffloadToIhv
#define CleanupParsedWdiSetAddPmProtocolOffload CleanupParsedWdiSetAddPmProtocolOffloadToIhv
#define GenerateWdiSetAddPmProtocolOffload GenerateWdiSetAddPmProtocolOffloadFromIhv
#define ParseWdiSetRemovePmProtocolOffload ParseWdiSetRemovePmProtocolOffloadToIhv
#define CleanupParsedWdiSetRemovePmProtocolOffload CleanupParsedWdiSetRemovePmProtocolOffloadToIhv
#define GenerateWdiSetRemovePmProtocolOffload GenerateWdiSetRemovePmProtocolOffloadFromIhv
#define GenerateWdiIndicationP2pGroupOperatingChannel GenerateWdiIndicationP2pGroupOperatingChannelFromIhv
#define GenerateWdiIndicationP2pOperatingChannelAttributes GenerateWdiIndicationP2pOperatingChannelAttributesFromIhv
#define ParseWdiSetAdapterConfiguration ParseWdiSetAdapterConfigurationToIhv
#define CleanupParsedWdiSetAdapterConfiguration CleanupParsedWdiSetAdapterConfigurationToIhv
#define GenerateWdiSetAdapterConfiguration GenerateWdiSetAdapterConfigurationFromIhv
#define ParseWdiGetReceiveCoalescingMatchCount ParseWdiGetReceiveCoalescingMatchCountToIhv
#define CleanupParsedWdiGetReceiveCoalescingMatchCount CleanupParsedWdiGetReceiveCoalescingMatchCountToIhv
#define GenerateWdiGetReceiveCoalescingMatchCount GenerateWdiGetReceiveCoalescingMatchCountFromIhv
#define ParseWdiSetClearReceiveCoalescing ParseWdiSetClearReceiveCoalescingToIhv
#define CleanupParsedWdiSetClearReceiveCoalescing CleanupParsedWdiSetClearReceiveCoalescingToIhv
#define GenerateWdiSetClearReceiveCoalescing GenerateWdiSetClearReceiveCoalescingFromIhv
#define ParseWdiGetPmProtocolOffload ParseWdiGetPmProtocolOffloadToIhv
#define CleanupParsedWdiGetPmProtocolOffload CleanupParsedWdiGetPmProtocolOffloadToIhv
#define GenerateWdiGetPmProtocolOffload GenerateWdiGetPmProtocolOffloadFromIhv
#define ParseWdiSetAdvertisementInformation ParseWdiSetAdvertisementInformationToIhv
#define CleanupParsedWdiSetAdvertisementInformation CleanupParsedWdiSetAdvertisementInformationToIhv
#define GenerateWdiSetAdvertisementInformation GenerateWdiSetAdvertisementInformationFromIhv
#define ParseWdiTaskChangeOperationMode ParseWdiTaskChangeOperationModeToIhv
#define CleanupParsedWdiTaskChangeOperationMode CleanupParsedWdiTaskChangeOperationModeToIhv
#define GenerateWdiTaskChangeOperationMode GenerateWdiTaskChangeOperationModeFromIhv
#define GenerateWdiIndicationChangeOperationModeComplete GenerateWdiIndicationChangeOperationModeCompleteFromIhv
#define GenerateWdiIndicationIhvEvent GenerateWdiIndicationIhvEventFromIhv
#define GenerateWdiIndicationOpenComplete GenerateWdiIndicationOpenCompleteFromIhv
#define GenerateWdiIndicationCloseComplete GenerateWdiIndicationCloseCompleteFromIhv
#define GenerateWdiIndicationAssociationParametersRequest GenerateWdiIndicationAssociationParametersRequestFromIhv
#define ParseWdiSetAssociationParameters ParseWdiSetAssociationParametersToIhv
#define CleanupParsedWdiSetAssociationParameters CleanupParsedWdiSetAssociationParametersToIhv
#define GenerateWdiSetAssociationParameters GenerateWdiSetAssociationParametersFromIhv
#define ParseWdiTaskRoam ParseWdiTaskRoamToIhv
#define CleanupParsedWdiTaskRoam CleanupParsedWdiTaskRoamToIhv
#define GenerateWdiTaskRoam GenerateWdiTaskRoamFromIhv
#define GenerateWdiIndicationRoamComplete GenerateWdiIndicationRoamCompleteFromIhv
#define ParseWdiTaskSendRequestActionFrame ParseWdiTaskSendRequestActionFrameToIhv
#define CleanupParsedWdiTaskSendRequestActionFrame CleanupParsedWdiTaskSendRequestActionFrameToIhv
#define GenerateWdiTaskSendRequestActionFrame GenerateWdiTaskSendRequestActionFrameFromIhv
#define GenerateWdiIndicationActionFrameReceived GenerateWdiIndicationActionFrameReceivedFromIhv
#define GenerateWdiIndicationTaskOffloadCurrentConfig GenerateWdiIndicationTaskOffloadCurrentConfigFromIhv
#define ParseWdiTaskSendResponseActionFrame ParseWdiTaskSendResponseActionFrameToIhv
#define CleanupParsedWdiTaskSendResponseActionFrame CleanupParsedWdiTaskSendResponseActionFrameToIhv
#define GenerateWdiTaskSendResponseActionFrame GenerateWdiTaskSendResponseActionFrameFromIhv
#define GenerateWdiIndicationSendRequestActionFrameComplete GenerateWdiIndicationSendRequestActionFrameCompleteFromIhv
#define GenerateWdiIndicationSendResponseActionFrameComplete GenerateWdiIndicationSendResponseActionFrameCompleteFromIhv
#define ParseWdiGetNextActionFrameDialogToken ParseWdiGetNextActionFrameDialogTokenToIhv
#define CleanupParsedWdiGetNextActionFrameDialogToken CleanupParsedWdiGetNextActionFrameDialogTokenToIhv
#define GenerateWdiGetNextActionFrameDialogToken GenerateWdiGetNextActionFrameDialogTokenFromIhv
#define ParseWdiSetP2pWpsEnabled ParseWdiSetP2pWpsEnabledToIhv
#define CleanupParsedWdiSetP2pWpsEnabled CleanupParsedWdiSetP2pWpsEnabledToIhv
#define GenerateWdiSetP2pWpsEnabled GenerateWdiSetP2pWpsEnabledFromIhv
#define ParseWdiTaskIhv ParseWdiTaskIhvToIhv
#define CleanupParsedWdiTaskIhv CleanupParsedWdiTaskIhvToIhv
#define GenerateWdiTaskIhv GenerateWdiTaskIhvFromIhv
#define ParseWdiTaskRequestFtm ParseWdiTaskRequestFtmToIhv
#define CleanupParsedWdiTaskRequestFtm CleanupParsedWdiTaskRequestFtmToIhv
#define GenerateWdiTaskRequestFtm GenerateWdiTaskRequestFtmFromIhv
#define GenerateWdiIndicationRequestFtmComplete GenerateWdiIndicationRequestFtmCompleteFromIhv
#define GenerateWdiIndicationIhvTaskRequest GenerateWdiIndicationIhvTaskRequestFromIhv
#define GenerateWdiIndicationIhvTaskComplete GenerateWdiIndicationIhvTaskCompleteFromIhv
#define ParseWdiAbortTask ParseWdiAbortTaskToIhv
#define CleanupParsedWdiAbortTask CleanupParsedWdiAbortTaskToIhv
#define GenerateWdiAbortTask GenerateWdiAbortTaskFromIhv
#define ParseWdiSetTcpOffloadParameters ParseWdiSetTcpOffloadParametersToIhv
#define CleanupParsedWdiSetTcpOffloadParameters CleanupParsedWdiSetTcpOffloadParametersToIhv
#define GenerateWdiSetTcpOffloadParameters GenerateWdiSetTcpOffloadParametersFromIhv
#define ParseWdiTcpRscStatistics ParseWdiTcpRscStatisticsToIhv
#define CleanupParsedWdiTcpRscStatistics CleanupParsedWdiTcpRscStatisticsToIhv
#define GenerateWdiTcpRscStatistics GenerateWdiTcpRscStatisticsFromIhv
#define ParseWdiSetEncapsulationOffload ParseWdiSetEncapsulationOffloadToIhv
#define CleanupParsedWdiSetEncapsulationOffload CleanupParsedWdiSetEncapsulationOffloadToIhv
#define GenerateWdiSetEncapsulationOffload GenerateWdiSetEncapsulationOffloadFromIhv
#define GenerateWdiIndicationFtAssocParamsNeeded GenerateWdiIndicationFtAssocParamsNeededFromIhv
#define ParseWdiSetFastBssTransitionParameters ParseWdiSetFastBssTransitionParametersToIhv
#define CleanupParsedWdiSetFastBssTransitionParameters CleanupParsedWdiSetFastBssTransitionParametersToIhv
#define GenerateWdiSetFastBssTransitionParameters GenerateWdiSetFastBssTransitionParametersFromIhv
#define GenerateWdiIndicationSaeAuthParamsNeeded GenerateWdiIndicationSaeAuthParamsNeededFromIhv
#define ParseWdiSetSaeAuthParams ParseWdiSetSaeAuthParamsToIhv
#define CleanupParsedWdiSetSaeAuthParams CleanupParsedWdiSetSaeAuthParamsToIhv
#define GenerateWdiSetSaeAuthParams GenerateWdiSetSaeAuthParamsFromIhv
#define GenerateWdiIndicationFirmwareStalled GenerateWdiIndicationFirmwareStalledFromIhv
#define ParseWdiSetNeighborReportEntries ParseWdiSetNeighborReportEntriesToIhv
#define CleanupParsedWdiSetNeighborReportEntries CleanupParsedWdiSetNeighborReportEntriesToIhv
#define GenerateWdiSetNeighborReportEntries GenerateWdiSetNeighborReportEntriesFromIhv
#define ParseWdiGetSupportedDeviceServices ParseWdiGetSupportedDeviceServicesToIhv
#define CleanupParsedWdiGetSupportedDeviceServices CleanupParsedWdiGetSupportedDeviceServicesToIhv
#define GenerateWdiGetSupportedDeviceServices GenerateWdiGetSupportedDeviceServicesFromIhv
#define ParseWdiDeviceServiceCommand ParseWdiDeviceServiceCommandToIhv
#define CleanupParsedWdiDeviceServiceCommand CleanupParsedWdiDeviceServiceCommandToIhv
#define GenerateWdiDeviceServiceCommand GenerateWdiDeviceServiceCommandFromIhv
#define GenerateWdiIndicationCipherKeyUpdated GenerateWdiIndicationCipherKeyUpdatedFromIhv
#define GenerateWdiIndicationDeviceServiceEvent GenerateWdiIndicationDeviceServiceEventFromIhv
#define GenerateWdiIndicationSecondaryStaConnectivity GenerateWdiIndicationSecondaryStaConnectivityFromIhv
#define ParseWdiSetLocationPrivacy ParseWdiSetLocationPrivacyToIhv
#define CleanupParsedWdiSetLocationPrivacy CleanupParsedWdiSetLocationPrivacyToIhv
#define Parse ParseToIhv
#define FreeParsed FreeParsedToIhv

#endif
#endif
