/*
 * ntddndis.h
 *
 * NDIS device driver interface
 *
 * This file is part of the w32api package.
 *
 * Contributors:
 *   Created by Casper S. Hornstrup <chorns@users.sourceforge.net>
 *
 * THIS SOFTWARE IS NOT COPYRIGHTED
 *
 * This source code is offered for use in the public domain. You may
 * use, modify or distribute it freely.
 *
 * This code is distributed in the hope that it will be useful but
 * WITHOUT ANY WARRANTY. ALL WARRANTIES, EXPRESS OR IMPLIED ARE HEREBY
 * DISCLAIMED. This includes but is not limited to warranties of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 */

#ifndef _NTDDNDIS_
#define _NTDDNDIS_

#include <ifdef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NDIS 6.30+ Hyper-V switch identifiers. These are scalar ABI types shared
 * by ntddndis.h consumers and the kernel-mode NET_PNP notification surface. */
typedef ULONG NDIS_NIC_SWITCH_ID, *PNDIS_NIC_SWITCH_ID;
typedef ULONG NDIS_NIC_SWITCH_VPORT_ID, *PNDIS_NIC_SWITCH_VPORT_ID;

typedef enum _NDIS_WAN_QUALITY {
	NdisWanRaw,
	NdisWanErrorControl,
	NdisWanReliable
} NDIS_WAN_QUALITY, *PNDIS_WAN_QUALITY;

typedef enum _NDIS_DEVICE_POWER_STATE {
  NdisDeviceStateUnspecified = 0,
  NdisDeviceStateD0,
  NdisDeviceStateD1,
  NdisDeviceStateD2,
  NdisDeviceStateD3,
  NdisDeviceStateMaximum
} NDIS_DEVICE_POWER_STATE, *PNDIS_DEVICE_POWER_STATE;

typedef enum _NDIS_802_11_WEP_STATUS
{
    Ndis802_11WEPEnabled,
    Ndis802_11Encryption1Enabled = Ndis802_11WEPEnabled,
    Ndis802_11WEPDisabled,
    Ndis802_11EncryptionDisabled = Ndis802_11WEPDisabled,
    Ndis802_11WEPKeyAbsent,
    Ndis802_11Encryption1KeyAbsent = Ndis802_11WEPKeyAbsent,
    Ndis802_11WEPNotSupported,
    Ndis802_11EncryptionNotSupported = Ndis802_11WEPNotSupported,
    Ndis802_11Encryption2Enabled,
    Ndis802_11Encryption2KeyAbsent,
    Ndis802_11Encryption3Enabled,
    Ndis802_11Encryption3KeyAbsent
} NDIS_802_11_WEP_STATUS, *PNDIS_802_11_WEP_STATUS,
  NDIS_802_11_ENCRYPTION_STATUS, *PNDIS_802_11_ENCRYPTION_STATUS;

typedef enum _NDIS_802_11_AUTHENTICATION_MODE
{
    Ndis802_11AuthModeOpen,
    Ndis802_11AuthModeShared,
    Ndis802_11AuthModeAutoSwitch,
    Ndis802_11AuthModeWPA,
    Ndis802_11AuthModeWPAPSK,
    Ndis802_11AuthModeWPANone,
    Ndis802_11AuthModeWPA2,
    Ndis802_11AuthModeWPA2PSK,
    Ndis802_11AuthModeMax
} NDIS_802_11_AUTHENTICATION_MODE, *PNDIS_802_11_AUTHENTICATION_MODE;

typedef enum _NDIS_802_11_NETWORK_INFRASTRUCTURE
{
    Ndis802_11IBSS,
    Ndis802_11Infrastructure,
    Ndis802_11AutoUnknown,
    Ndis802_11InfrastructureMax
} NDIS_802_11_NETWORK_INFRASTRUCTURE, *PNDIS_802_11_NETWORK_INFRASTRUCTURE;

typedef enum _NDIS_802_11_NETWORK_TYPE
{
    Ndis802_11FH,
    Ndis802_11DS,
    Ndis802_11OFDM5,
    Ndis802_11OFDM24,
    Ndis802_11Automode,
    Ndis802_11NetworkTypeMax
} NDIS_802_11_NETWORK_TYPE, *PNDIS_802_11_NETWORK_TYPE;

typedef struct _NDIS_OBJECT_HEADER
{
    UCHAR Type;
    UCHAR Revision;
    USHORT Size;
} NDIS_OBJECT_HEADER, *PNDIS_OBJECT_HEADER;

#define NDIS_802_11_LENGTH_SSID  32
#define NDIS_802_11_LENGTH_RATES 8

typedef UCHAR NDIS_802_11_MAC_ADDRESS[6];
typedef LONG NDIS_802_11_RSSI;
typedef UCHAR NDIS_802_11_RATES[NDIS_802_11_LENGTH_RATES];

typedef struct _NDIS_802_11_SSID
{
    ULONG SsidLength;
    UCHAR Ssid[NDIS_802_11_LENGTH_SSID];
} NDIS_802_11_SSID, *PNDIS_802_11_SSID;

typedef struct _NDIS_802_11_CONFIGURATION_FH
{
    ULONG Length;
    ULONG HopPattern;
    ULONG HopSet;
    ULONG DwellTime;
} NDIS_802_11_CONFIGURATION_FH, *PNDIS_802_11_CONFIGURATION_FH;

typedef struct _NDIS_802_11_CONFIGURATION
{
    ULONG Length;
    ULONG BeaconPeriod;
    ULONG ATIMWindow;
    ULONG DSConfig;
    NDIS_802_11_CONFIGURATION_FH FHConfig;
} NDIS_802_11_CONFIGURATION, *PNDIS_802_11_CONFIGURATION;

typedef struct _NDIS_WLAN_BSSID
{
    ULONG Length;
    NDIS_802_11_MAC_ADDRESS MacAddress;
    UCHAR Reserved[2];
    NDIS_802_11_SSID Ssid;
    ULONG Privacy;
    NDIS_802_11_RSSI Rssi;
    NDIS_802_11_NETWORK_TYPE NetworkTypeInUse;
    NDIS_802_11_CONFIGURATION Configuration;
    NDIS_802_11_NETWORK_INFRASTRUCTURE InfrastructureMode;
    NDIS_802_11_RATES SupportedRates;
} NDIS_WLAN_BSSID, *PNDIS_WLAN_BSSID;

typedef struct _NDIS_802_11_BSSID_LIST
{
    ULONG NumberOfItems;
    NDIS_WLAN_BSSID Bssid[1];
} NDIS_802_11_BSSID_LIST, *PNDIS_802_11_BSSID_LIST;

typedef struct _NDIS_802_11_WEP
{
    ULONG Length;
    ULONG KeyIndex;
    ULONG KeyLength;
    UCHAR KeyMaterial[1];
} NDIS_802_11_WEP, *PNDIS_802_11_WEP;

typedef ULONGLONG NDIS_802_11_KEY_RSC;

typedef struct _NDIS_802_11_KEY
{
    ULONG Length;
    ULONG KeyIndex;
    ULONG KeyLength;
    NDIS_802_11_MAC_ADDRESS BSSID;
    NDIS_802_11_KEY_RSC KeyRSC;
    UCHAR KeyMaterial[1];
} NDIS_802_11_KEY, *PNDIS_802_11_KEY;

typedef struct _NDIS_PM_WAKE_UP_CAPABILITIES {
  NDIS_DEVICE_POWER_STATE  MinMagicPacketWakeUp;
  NDIS_DEVICE_POWER_STATE  MinPatternWakeUp;
  NDIS_DEVICE_POWER_STATE  MinLinkChangeWakeUp;
} NDIS_PM_WAKE_UP_CAPABILITIES, *PNDIS_PM_WAKE_UP_CAPABILITIES;

/* NDIS_PNP_CAPABILITIES.Flags constants */
#define NDIS_DEVICE_WAKE_UP_ENABLE                0x00000001
#define NDIS_DEVICE_WAKE_ON_PATTERN_MATCH_ENABLE  0x00000002
#define NDIS_DEVICE_WAKE_ON_MAGIC_PACKET_ENABLE   0x00000004

typedef struct _NDIS_PNP_CAPABILITIES {
  ULONG  Flags;
  NDIS_PM_WAKE_UP_CAPABILITIES  WakeUpCapabilities;
} NDIS_PNP_CAPABILITIES, *PNDIS_PNP_CAPABILITIES;

/* NDIS 6.20 power-management capabilities (REVISION_1 form). Target of
 * NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES::PowerManagementCapabilitiesEx. */
#define NDIS_PM_CAPABILITIES_REVISION_1  1



/* Type (OID_GEN_VLAN_ID) */
typedef ULONG NDIS_VLAN_ID;

/* NDIS driver medium (OID_GEN_MEDIA_SUPPORTED / OID_GEN_MEDIA_IN_USE) */
typedef enum _NDIS_MEDIUM {
    NdisMedium802_3,
    NdisMedium802_5,
    NdisMediumFddi,
    NdisMediumWan,
    NdisMediumLocalTalk,
    NdisMediumDix,          // Not a real medium
    NdisMediumArcnetRaw,
    NdisMediumArcnet878_2,
    NdisMediumAtm,
    NdisMediumWirelessWan,
    NdisMediumIrda,
    NdisMediumBpc,
    NdisMediumCoWan,
    NdisMedium1394,
    NdisMediumInfiniBand,
#if ((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6)
    NdisMediumTunnel,
    NdisMediumNative802_11,
    NdisMediumLoopback,
#endif
#if (NTDDI_VERSION >= NTDDI_WIN7)
    NdisMediumWiMAX,
    NdisMediumIP,
#endif
    NdisMediumMax           // Not a real medium
} NDIS_MEDIUM, *PNDIS_MEDIUM;

/* Physical medium type (OID_GEN_PHYSICAL_MEDIUM) */
typedef enum _NDIS_PHYSICAL_MEDIUM
{
    // Windows Server 2003 SP1 Platform SDK (or earlier?)
    NdisPhysicalMediumUnspecified,
    NdisPhysicalMediumWirelessLan,
    NdisPhysicalMediumCableModem,
    NdisPhysicalMediumPhoneLine,
    NdisPhysicalMediumPowerLine,
    NdisPhysicalMediumDSL,
    NdisPhysicalMediumFibreChannel,
    NdisPhysicalMedium1394,
    NdisPhysicalMediumWirelessWan,
    NdisPhysicalMediumNative802_11,
    NdisPhysicalMediumBluetooth,
    // Windows SDK 7.1A (or earlier?)
    NdisPhysicalMediumInfiniband,
    NdisPhysicalMediumWiMax,
    NdisPhysicalMediumUWB,
    NdisPhysicalMedium802_3,
    NdisPhysicalMedium802_5,
    NdisPhysicalMediumIrda,
    NdisPhysicalMediumWiredWAN,
    NdisPhysicalMediumWiredCoWan,
    NdisPhysicalMediumOther,
    // Windows SDK 10.0.16299.0 (or earlier?)
    NdisPhysicalMediumNative802_15_4,
    // Always
    NdisPhysicalMediumMax               // Not a real physical medium
} NDIS_PHYSICAL_MEDIUM, *PNDIS_PHYSICAL_MEDIUM;

typedef ULONG NDIS_OID, *PNDIS_OID;

/* Required Object IDs (OIDs) */
#define OID_GEN_SUPPORTED_LIST            0x00010101
#define OID_GEN_HARDWARE_STATUS           0x00010102
#define OID_GEN_MEDIA_SUPPORTED           0x00010103
#define OID_GEN_MEDIA_IN_USE              0x00010104
#define OID_GEN_MAXIMUM_LOOKAHEAD         0x00010105
#define OID_GEN_MAXIMUM_FRAME_SIZE        0x00010106
#define OID_GEN_LINK_SPEED                0x00010107
#define OID_GEN_TRANSMIT_BUFFER_SPACE     0x00010108
#define OID_GEN_RECEIVE_BUFFER_SPACE      0x00010109
#define OID_GEN_TRANSMIT_BLOCK_SIZE       0x0001010A
#define OID_GEN_RECEIVE_BLOCK_SIZE        0x0001010B
#define OID_GEN_VENDOR_ID                 0x0001010C
#define OID_GEN_VENDOR_DESCRIPTION        0x0001010D
#define OID_GEN_CURRENT_PACKET_FILTER     0x0001010E
#define OID_GEN_CURRENT_LOOKAHEAD         0x0001010F
#define OID_GEN_DRIVER_VERSION            0x00010110
#define OID_GEN_MAXIMUM_TOTAL_SIZE        0x00010111
#define OID_GEN_PROTOCOL_OPTIONS          0x00010112
#define OID_GEN_MAC_OPTIONS               0x00010113
#define OID_GEN_MEDIA_CONNECT_STATUS      0x00010114
#define OID_GEN_MAXIMUM_SEND_PACKETS      0x00010115
#define OID_GEN_VENDOR_DRIVER_VERSION     0x00010116
#define OID_GEN_SUPPORTED_GUIDS           0x00010117
#define OID_GEN_NETWORK_LAYER_ADDRESSES   0x00010118
#define OID_GEN_TRANSPORT_HEADER_OFFSET   0x00010119
#define OID_GEN_MACHINE_NAME              0x0001021A
#define OID_GEN_RNDIS_CONFIG_PARAMETER    0x0001021B
#define OID_GEN_VLAN_ID                   0x0001021C

/* Optional OIDs */
#define OID_GEN_MEDIA_CAPABILITIES        0x00010201
#define OID_GEN_PHYSICAL_MEDIUM           0x00010202

/* Required statistics OIDs */
#define OID_GEN_XMIT_OK                   0x00020101
#define OID_GEN_RCV_OK                    0x00020102
#define OID_GEN_XMIT_ERROR                0x00020103
#define OID_GEN_RCV_ERROR                 0x00020104
#define OID_GEN_RCV_NO_BUFFER             0x00020105

/* Optional statistics OIDs */
#define OID_GEN_DIRECTED_BYTES_XMIT       0x00020201
#define OID_GEN_DIRECTED_FRAMES_XMIT      0x00020202
#define OID_GEN_MULTICAST_BYTES_XMIT      0x00020203
#define OID_GEN_MULTICAST_FRAMES_XMIT     0x00020204
#define OID_GEN_BROADCAST_BYTES_XMIT      0x00020205
#define OID_GEN_BROADCAST_FRAMES_XMIT     0x00020206
#define OID_GEN_DIRECTED_BYTES_RCV        0x00020207
#define OID_GEN_DIRECTED_FRAMES_RCV       0x00020208
#define OID_GEN_MULTICAST_BYTES_RCV       0x00020209
#define OID_GEN_MULTICAST_FRAMES_RCV      0x0002020A
#define OID_GEN_BROADCAST_BYTES_RCV       0x0002020B
#define OID_GEN_BROADCAST_FRAMES_RCV      0x0002020C
#define OID_GEN_RCV_CRC_ERROR             0x0002020D
#define OID_GEN_TRANSMIT_QUEUE_LENGTH     0x0002020E
#define OID_GEN_GET_TIME_CAPS             0x0002020F
#define OID_GEN_GET_NETCARD_TIME          0x00020210
#define OID_GEN_NETCARD_LOAD              0x00020211
#define OID_GEN_DEVICE_PROFILE            0x00020212
#define OID_GEN_INIT_TIME_MS              0x00020213
#define OID_GEN_RESET_COUNTS              0x00020214
#define OID_GEN_MEDIA_SENSE_COUNTS        0x00020215
#define OID_GEN_FRIENDLY_NAME             0x00020216
#define OID_GEN_MINIPORT_INFO             0x00020217
#define OID_GEN_RESET_VERIFY_PARAMETERS   0x00020218

/* IEEE 802.3 (Ethernet) OIDs */
#define NDIS_802_3_MAC_OPTION_PRIORITY    0x00000001

#define OID_802_3_PERMANENT_ADDRESS       0x01010101
#define OID_802_3_CURRENT_ADDRESS         0x01010102
#define OID_802_3_MULTICAST_LIST          0x01010103
#define OID_802_3_MAXIMUM_LIST_SIZE       0x01010104
#define OID_802_3_MAC_OPTIONS             0x01010105
#define OID_802_3_RCV_ERROR_ALIGNMENT     0x01020101
#define OID_802_3_XMIT_ONE_COLLISION      0x01020102
#define OID_802_3_XMIT_MORE_COLLISIONS    0x01020103
#define OID_802_3_XMIT_DEFERRED           0x01020201
#define OID_802_3_XMIT_MAX_COLLISIONS     0x01020202
#define OID_802_3_RCV_OVERRUN             0x01020203
#define OID_802_3_XMIT_UNDERRUN           0x01020204
#define OID_802_3_XMIT_HEARTBEAT_FAILURE  0x01020205
#define OID_802_3_XMIT_TIMES_CRS_LOST     0x01020206
#define OID_802_3_XMIT_LATE_COLLISIONS    0x01020207

/* IEEE 802.11 (WLAN) OIDs */
#define OID_802_11_BSSID                        0x0D010101
#define OID_802_11_SSID                         0x0D010102
#define OID_802_11_NETWORK_TYPES_SUPPORTED      0x0D010203
#define OID_802_11_NETWORK_TYPE_IN_USE          0x0D010204
#define OID_802_11_TX_POWER_LEVEL               0x0D010205
#define OID_802_11_RSSI                         0x0D010206
#define OID_802_11_RSSI_TRIGGER                 0x0D010207
#define OID_802_11_INFRASTRUCTURE_MODE          0x0D010108
#define OID_802_11_FRAGMENTATION_THRESHOLD      0x0D010209
#define OID_802_11_RTS_THRESHOLD                0x0D01020A
#define OID_802_11_NUMBER_OF_ANTENNAS           0x0D01020B
#define OID_802_11_RX_ANTENNA_SELECTED          0x0D01020C
#define OID_802_11_TX_ANTENNA_SELECTED          0x0D01020D
#define OID_802_11_SUPPORTED_RATES              0x0D01020E
#define OID_802_11_DESIRED_RATES                0x0D010210
#define OID_802_11_CONFIGURATION                0x0D010211
#define OID_802_11_STATISTICS                   0x0D020212
#define OID_802_11_ADD_WEP                      0x0D010113
#define OID_802_11_REMOVE_WEP                   0x0D010114
#define OID_802_11_DISASSOCIATE                 0x0D010115
#define OID_802_11_POWER_MODE                   0x0D010216
#define OID_802_11_BSSID_LIST                   0x0D010217
#define OID_802_11_AUTHENTICATION_MODE          0x0D010118
#define OID_802_11_PRIVACY_FILTER               0x0D010119
#define OID_802_11_BSSID_LIST_SCAN              0x0D01011A
#define OID_802_11_WEP_STATUS                   0x0D01011B
/* New name is supposed to better reflect the extended set of encryption status */
#define OID_802_11_ENCRYPTION_STATUS            OID_802_11_WEP_STATUS
#define OID_802_11_RELOAD_DEFAULTS              0x0D01011C
/* Allows key mapping and default keys */
#define OID_802_11_ADD_KEY                      0x0D01011D
#define OID_802_11_REMOVE_KEY                   0x0D01011E
#define OID_802_11_ASSOCIATION_INFORMATION      0x0D01011F
#define OID_802_11_TEST                         0x0D010120
#define OID_802_11_MEDIA_STREAM_MODE            0x0D010121
#define OID_802_11_CAPABILITY                   0x0D010122
#define OID_802_11_PMKID                        0x0D010123
#define OID_802_11_NON_BCAST_SSID_LIST          0x0D010124
#define OID_802_11_RADIO_STATUS                 0x0D010125

/* PnP and Power Management (PM) OIDs */
#define OID_PNP_CAPABILITIES                    0xFD010100
#define OID_PNP_SET_POWER                       0xFD010101
#define OID_PNP_QUERY_POWER                     0xFD010102
#define OID_PNP_ADD_WAKE_UP_PATTERN             0xFD010103
#define OID_PNP_REMOVE_WAKE_UP_PATTERN          0xFD010104
#define OID_PNP_WAKE_UP_PATTERN_LIST            0xFD010105
#define OID_PNP_ENABLE_WAKE_UP                  0xFD010106

/* Optional PnP/PM statistics OIDs */
#define OID_PNP_WAKE_UP_OK                      0xFD020200
#define OID_PNP_WAKE_UP_ERROR                   0xFD020201

#define NDIS_PNP_WAKE_UP_MAGIC_PACKET           0x00000001
#define NDIS_PNP_WAKE_UP_PATTERN_MATCH          0x00000002
#define NDIS_PNP_WAKE_UP_LINK_CHANGE            0x00000004

/* TCP and IP OIDs */
#define OID_TCP_TASK_OFFLOAD                    0xFC010201
#define OID_TCP_TASK_IPSEC_ADD_SA               0xFC010202
#define OID_TCP_TASK_IPSEC_DELETE_SA            0xFC010203
#define OID_TCP_SAN_SUPPORT                     0xFC010204
#define OID_TCP_TASK_IPSEC_ADD_UDPESP_SA        0xFC010205
#define OID_TCP_TASK_IPSEC_DELETE_UDPESP_SA     0xFC010206
#define OID_TCP4_OFFLOAD_STATS                  0xFC010207
#define OID_TCP6_OFFLOAD_STATS                  0xFC010208
#define OID_IP4_OFFLOAD_STATS                   0xFC010209
#define OID_IP6_OFFLOAD_STATS                   0xFC01020A

/* New NDIS 6 offload OIDs */
#define OID_TCP_OFFLOAD_CURRENT_CONFIG                     0xFC01020B /* NDIS 5 handled. Query only */
#define OID_TCP_OFFLOAD_PARAMETERS                         0xFC01020C /* Set only */
#define OID_TCP_OFFLOAD_HARDWARE_CAPABILITIES              0xFC01020D /* Query only */
#define OID_TCP_CONNECTION_OFFLOAD_CURRENT_CONFIG          0xFC01020E /* Query only */
#define OID_TCP_CONNECTION_OFFLOAD_HARDWARE_CAPABILITIES   0xFC01020F /* Query only */
#define OID_OFFLOAD_ENCAPSULATION                          0x0101010A

/* Obsolete FFP defines */
#define OID_FFP_SUPPORT                         0xFC010210
#define OID_FFP_FLUSH                           0xFC010211
#define OID_FFP_CONTROL                         0xFC010212
#define OID_FFP_PARAMS                          0xFC010213
#define OID_FFP_DATA                            0xFC010214

#define OID_FFP_DRIVER_STATS                    0xFC020210
#define OID_FFP_ADAPTER_STATS                   0xFC020211

/* OID_GEN_MINIPORT_INFO constants */
#define NDIS_MINIPORT_BUS_MASTER                      0x00000001
#define NDIS_MINIPORT_WDM_DRIVER                      0x00000002
#define NDIS_MINIPORT_SG_LIST                         0x00000004
#define NDIS_MINIPORT_SUPPORTS_MEDIA_QUERY            0x00000008
#define NDIS_MINIPORT_INDICATES_PACKETS               0x00000010
#define NDIS_MINIPORT_IGNORE_PACKET_QUEUE             0x00000020
#define NDIS_MINIPORT_IGNORE_REQUEST_QUEUE            0x00000040
#define NDIS_MINIPORT_IGNORE_TOKEN_RING_ERRORS        0x00000080
#define NDIS_MINIPORT_INTERMEDIATE_DRIVER             0x00000100
#define NDIS_MINIPORT_IS_NDIS_5                       0x00000200
#define NDIS_MINIPORT_IS_CO                           0x00000400
#define NDIS_MINIPORT_DESERIALIZE                     0x00000800
#define NDIS_MINIPORT_REQUIRES_MEDIA_POLLING          0x00001000
#define NDIS_MINIPORT_SUPPORTS_MEDIA_SENSE            0x00002000
#define NDIS_MINIPORT_NETBOOT_CARD                    0x00004000
#define NDIS_MINIPORT_PM_SUPPORTED                    0x00008000
#define NDIS_MINIPORT_SUPPORTS_MAC_ADDRESS_OVERWRITE  0x00010000
#define NDIS_MINIPORT_USES_SAFE_BUFFER_APIS           0x00020000
#define NDIS_MINIPORT_HIDDEN                          0x00040000
#define NDIS_MINIPORT_SWENUM                          0x00080000
#define NDIS_MINIPORT_SURPRISE_REMOVE_OK              0x00100000
#define NDIS_MINIPORT_NO_HALT_ON_SUSPEND              0x00200000
#define NDIS_MINIPORT_HARDWARE_DEVICE                 0x00400000
#define NDIS_MINIPORT_SUPPORTS_CANCEL_SEND_PACKETS    0x00800000
#define NDIS_MINIPORT_64BITS_DMA                      0x01000000

/* Full duplex driver */
#define NDIS_MAC_OPTION_FULL_DUPLEX                      0x00000010 /* Deprecated flag */

#define NDIS_MAC_OPTION_EOTX_INDICATION                  0x00000020
#define NDIS_MAC_OPTION_8021P_PRIORITY                   0x00000040
#define NDIS_MAC_OPTION_SUPPORTS_MAC_ADDRESS_OVERWRITE   0x00000080
#define NDIS_MAC_OPTION_RECEIVE_AT_DPC                   0x00000100
#define NDIS_MAC_OPTION_8021Q_VLAN                       0x00000200
#define NDIS_MAC_OPTION_RESERVED                         0x80000000

#define _NDIS_CONTROL_CODE(request, method) \
    CTL_CODE(FILE_DEVICE_PHYSICAL_NETCARD, request, method, FILE_ANY_ACCESS)

#define IOCTL_NDIS_QUERY_GLOBAL_STATS   _NDIS_CONTROL_CODE(0x00, METHOD_OUT_DIRECT) // 0x170002

#define IOCTL_NDIS_RESERVED7            _NDIS_CONTROL_CODE(0x0F, METHOD_OUT_DIRECT) // 0x17003e

/* Hardware status codes (OID_GEN_HARDWARE_STATUS) */
typedef enum _NDIS_HARDWARE_STATUS {
  NdisHardwareStatusReady,
  NdisHardwareStatusInitializing,
  NdisHardwareStatusReset,
  NdisHardwareStatusClosing,
  NdisHardwareStatusNotReady
} NDIS_HARDWARE_STATUS, *PNDIS_HARDWARE_STATUS;

/* OID_GEN_GET_TIME_CAPS */
typedef struct _GEN_GET_TIME_CAPS {
  ULONG Flags;
  ULONG ClockPrecision;
} GEN_GET_TIME_CAPS, *PGEN_GET_TIME_CAPS;

/* OID_GEN_GET_NETCARD_TIME */
typedef struct _GEN_GET_NETCARD_TIME {
  ULONGLONG ReadTime;
} GEN_GET_NETCARD_TIME, *PGEN_GET_NETCARD_TIME;

/* State of the LAN media (OID_GEN_MEDIA_CONNECT_STATUS) */
typedef enum _NDIS_MEDIA_STATE {
  NdisMediaStateConnected,
  NdisMediaStateDisconnected
} NDIS_MEDIA_STATE, *PNDIS_MEDIA_STATE;

#ifndef _NDIS_
typedef int NDIS_STATUS, *PNDIS_STATUS;
#endif

/* OID_GEN_SUPPORTED_GUIDS */
typedef struct _NDIS_GUID {
  GUID Guid;
  union {
    NDIS_OID Oid;
    NDIS_STATUS Status;
  } u;
  ULONG Size;
  ULONG Flags;
} NDIS_GUID, *PNDIS_GUID;

typedef struct _NDIS_PM_PACKET_PATTERN {
  ULONG Priority;
  ULONG Reserved;
  ULONG MaskSize;
  ULONG PatternOffset;
  ULONG PatternSize;
  ULONG PatternFlags;
} NDIS_PM_PACKET_PATTERN, *PNDIS_PM_PACKET_PATTERN;

/* OID_GEN_NETWORK_LAYER_ADDRESSES */
typedef struct _NETWORK_ADDRESS {
  USHORT AddressLength;
  USHORT AddressType;
  UCHAR Address[1];
} NETWORK_ADDRESS, *PNETWORK_ADDRESS;

typedef struct _NETWORK_ADDRESS_LIST {
  LONG AddressCount;
  USHORT AddressType;
  NETWORK_ADDRESS Address[1];
} NETWORK_ADDRESS_LIST, *PNETWORK_ADDRESS_LIST;

/* OID_GEN_TRANSPORT_HEADER_OFFSET */
typedef struct _TRANSPORT_HEADER_OFFSET {
  USHORT ProtocolType;
  USHORT HeaderOffset;
} TRANSPORT_HEADER_OFFSET, *PTRANSPORT_HEADER_OFFSET;

/* OID_GEN_CO_LINK_SPEED / OID_GEN_CO_MINIMUM_LINK_SPEED */
typedef struct _NDIS_CO_LINK_SPEED {
  ULONG Outbound;
  ULONG Inbound;
} NDIS_CO_LINK_SPEED, *PNDIS_CO_LINK_SPEED;

#define NDIS_PACKET_TYPE_DIRECTED               0x00000001
#define NDIS_PACKET_TYPE_MULTICAST              0x00000002
#define NDIS_PACKET_TYPE_ALL_MULTICAST          0x00000004
#define NDIS_PACKET_TYPE_BROADCAST              0x00000008
#define NDIS_PACKET_TYPE_SOURCE_ROUTING         0x00000010
#define NDIS_PACKET_TYPE_PROMISCUOUS            0x00000020
#define NDIS_PACKET_TYPE_SMT                    0x00000040
#define NDIS_PACKET_TYPE_ALL_LOCAL              0x00000080
#define NDIS_PACKET_TYPE_GROUP                  0x00001000
#define NDIS_PACKET_TYPE_ALL_FUNCTIONAL         0x00002000
#define NDIS_PACKET_TYPE_FUNCTIONAL             0x00004000
#define NDIS_PACKET_TYPE_MAC_FRAME              0x00008000
#define NDIS_PACKET_TYPE_NO_LOCAL               0x00010000

#if !defined(NDIS_SUPPORT_NDIS6) || NDIS_SUPPORT_NDIS6

#if ((NDIS_SUPPORT_NDIS61))

#define NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_HARDWARE_ASSIST_ATTRIBUTES    0xAF

#endif
#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_OBJECT_TYPE_QOS_CAPABILITIES                               0xB5

#define NDIS_OBJECT_TYPE_QOS_PARAMETERS                                 0xB6

#define NDIS_OBJECT_TYPE_QOS_CLASSIFICATION_ELEMENT                     0xB7

#define NDIS_OBJECT_TYPE_SWITCH_OPTIONAL_HANDLERS                       0xB8

#endif

#define NDIS_OBJECT_REVISION_1 1

#if (((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6))

typedef enum _NDIS_INTERRUPT_MODERATION
{
    NdisInterruptModerationUnknown,
    NdisInterruptModerationNotSupported,
    NdisInterruptModerationEnabled,
    NdisInterruptModerationDisabled
} NDIS_INTERRUPT_MODERATION, *PNDIS_INTERRUPT_MODERATION;

#define NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_RESET            0x00000001

#define NDIS_INTERRUPT_MODERATION_CHANGE_NEEDS_REINITIALIZE     0x00000002

#define NDIS_INTERRUPT_MODERATION_PARAMETERS_REVISION_1    1

typedef struct _NDIS_INTERRUPT_MODERATION_PARAMETERS
{
    NDIS_OBJECT_HEADER Header;
    ULONG Flags;
    NDIS_INTERRUPT_MODERATION InterruptModeration;
}NDIS_INTERRUPT_MODERATION_PARAMETERS, *PNDIS_INTERRUPT_MODERATION_PARAMETERS;

#define NDIS_SIZEOF_INTERRUPT_MODERATION_PARAMETERS_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_INTERRUPT_MODERATION_PARAMETERS, InterruptModeration)

#define OID_GEN_RECEIVE_SCALE_CAPABILITIES      0x00010203

#define OID_GEN_RECEIVE_SCALE_PARAMETERS        0x00010204

#define OID_GEN_MAX_LINK_SPEED                  0x00010206

#endif
#if (((NTDDI_VERSION >= NTDDI_WIN10_RS3) || NDIS_SUPPORT_NDIS680))

#define OID_GEN_RECEIVE_SCALE_PARAMETERS_V2     0x00010214

#endif
#if (((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6))

#define OID_GEN_MEDIA_CONNECT_STATUS_EX         0x0001028A

#define OID_GEN_LINK_SPEED_EX                   0x0001028B

#define OID_GEN_MEDIA_DUPLEX_STATE              0x0001028C

#define OID_GEN_BYTES_RCV                       0x00020219

#define OID_GEN_BYTES_XMIT                      0x0002021A

#define OID_GEN_RCV_DISCARDS                    0x0002021B

#define OID_GEN_XMIT_DISCARDS                   0x0002021C

#define OID_802_3_ADD_MULTICAST_ADDRESS         0x01010208

#define OID_802_3_DELETE_MULTICAST_ADDRESS      0x01010209

#endif

#define NDIS_ETH_TYPE_802_1X            0x888e

#define NDIS_ETH_TYPE_802_1Q            0x8100

#if (((NTDDI_VERSION >= NTDDI_WIN7) || NDIS_SUPPORT_NDIS620))

#define OID_PM_PARAMETERS                       0xFD010109

#define OID_PM_ADD_WOL_PATTERN                  0xFD01010A

#define OID_PM_REMOVE_WOL_PATTERN               0xFD01010B

#define OID_PM_ADD_PROTOCOL_OFFLOAD             0xFD01010D

#define OID_PM_REMOVE_PROTOCOL_OFFLOAD          0xFD01010F

#define OID_RECEIVE_FILTER_ALLOCATE_QUEUE               0x00010223

#define OID_RECEIVE_FILTER_FREE_QUEUE                   0x00010224

#define OID_RECEIVE_FILTER_QUEUE_PARAMETERS             0x00010226

#define OID_RECEIVE_FILTER_SET_FILTER                   0x00010227

#define OID_RECEIVE_FILTER_CLEAR_FILTER                 0x00010228

#define OID_RECEIVE_FILTER_QUEUE_ALLOCATION_COMPLETE    0x0001022B

#endif
#if (((NTDDI_VERSION >= NTDDI_WIN8) || NDIS_SUPPORT_NDIS630))

#define OID_NIC_SWITCH_ALLOCATE_VF                    0x00010245

#define OID_SWITCH_PROPERTY_ADD                       0x00010263

#define OID_SWITCH_PROPERTY_UPDATE                    0x00010264

#define OID_SWITCH_PROPERTY_DELETE                    0x00010265

#define OID_SWITCH_PROPERTY_ENUM                      0x00010266

#define OID_SWITCH_FEATURE_STATUS_QUERY               0x00010267

#define OID_SWITCH_NIC_REQUEST                        0x00010270

#define OID_SWITCH_PORT_PROPERTY_ADD                  0x00010271

#define OID_SWITCH_PORT_PROPERTY_UPDATE               0x00010272

#define OID_SWITCH_PORT_PROPERTY_DELETE               0x00010273

#define OID_SWITCH_PORT_PROPERTY_ENUM                 0x00010274

#define OID_SWITCH_PARAMETERS                         0x00010275

#define OID_SWITCH_PORT_ARRAY                         0x00010276

#define OID_SWITCH_NIC_ARRAY                          0x00010277

#define OID_SWITCH_PORT_CREATE                        0x00010278

#define OID_SWITCH_PORT_DELETE                        0x00010279

#define OID_SWITCH_NIC_CREATE                         0x0001027A

#define OID_SWITCH_NIC_CONNECT                        0x0001027B

#define OID_SWITCH_NIC_DISCONNECT                     0x0001027C

#define OID_SWITCH_NIC_DELETE                         0x0001027D

#define OID_SWITCH_PORT_FEATURE_STATUS_QUERY          0x0001027E

#define OID_SWITCH_PORT_TEARDOWN                      0x0001027F

#define OID_SWITCH_NIC_SAVE                           0x00010290

#define OID_SWITCH_NIC_SAVE_COMPLETE                  0x00010291

#define OID_SWITCH_NIC_RESTORE                        0x00010292

#define OID_SWITCH_NIC_RESTORE_COMPLETE               0x00010293

#define OID_SWITCH_NIC_UPDATED                        0x00010294

#define OID_SWITCH_PORT_UPDATED                       0x00010295

#endif
#if ((NTDDI_VERSION >= NTDDI_WIN10_RS3) || (NDIS_SUPPORT_NDIS680))

#define OID_GEN_RSS_SET_INDIRECTION_TABLE_ENTRIES     0x000102C0

#endif
#if (((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6))

typedef struct _NDIS_LINK_SPEED
{
    ULONG64     XmitLinkSpeed;
    ULONG64     RcvLinkSpeed;
} NDIS_LINK_SPEED, *PNDIS_LINK_SPEED;

typedef IF_COUNTED_STRING NDIS_IF_COUNTED_STRING, *PNDIS_IF_COUNTED_STRING;

#define NDIS_MAX_PHYS_ADDRESS_LENGTH IF_MAX_PHYS_ADDRESS_LENGTH

#define NDIS_OFFLOAD_PARAMETERS_NO_CHANGE                  0

#define NDIS_OFFLOAD_PARAMETERS_TX_RX_DISABLED             1

#define NDIS_OFFLOAD_PARAMETERS_TX_ENABLED_RX_DISABLED     2

#define NDIS_OFFLOAD_PARAMETERS_RX_ENABLED_TX_DISABLED     3

#define NDIS_OFFLOAD_PARAMETERS_TX_RX_ENABLED              4

#define NDIS_OFFLOAD_PARAMETERS_REVISION_1            1

typedef struct _NDIS_OFFLOAD_PARAMETERS
{
    NDIS_OBJECT_HEADER      Header;
    UCHAR                   IPv4Checksum;
    UCHAR                   TCPIPv4Checksum;
    UCHAR                   UDPIPv4Checksum;
    UCHAR                   TCPIPv6Checksum;
    UCHAR                   UDPIPv6Checksum;
    UCHAR                   LsoV1;
    UCHAR                   IPsecV1;
    UCHAR                   LsoV2IPv4;
    UCHAR                   LsoV2IPv6;
    UCHAR                   TcpConnectionIPv4;
    UCHAR                   TcpConnectionIPv6;
    ULONG                   Flags;
#if (NDIS_SUPPORT_NDIS61)
    UCHAR                   IPsecV2;
    UCHAR                   IPsecV2IPv4;
#endif
#if (NDIS_SUPPORT_NDIS630)
    struct
    {
        UCHAR               RscIPv4;
        UCHAR               RscIPv6;
    };
    struct
    {
        UCHAR               EncapsulatedPacketTaskOffload;
        UCHAR               EncapsulationTypes;
    };
#endif
#if (NDIS_SUPPORT_NDIS650)
    union _ENCAPSULATION_PROTOCOL_PARAMETERS {
        struct _VXLAN_PARAMETERS {
            USHORT VxlanUDPPortNumber;
        } VxlanParameters;
        ULONG Value;
    } EncapsulationProtocolParameters;
#endif
#if (NDIS_SUPPORT_NDIS683)
    struct
    {
        UCHAR               IPv4;
        UCHAR               IPv6;
    } UdpSegmentation;
#endif
#if (NDIS_SUPPORT_NDIS689)
    struct
    {
        UCHAR               Enabled;
    } UdpRsc;
#endif
} NDIS_OFFLOAD_PARAMETERS, *PNDIS_OFFLOAD_PARAMETERS;

#define NDIS_ENCAPSULATION_NOT_SUPPORTED        0x00000000

#define NDIS_ENCAPSULATION_IEEE_802_3           0x00000002

#define NDIS_OFFLOAD_REVISION_1    1

#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_OFFLOAD_REVISION_3    3

#endif

#define NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_1   RTL_SIZEOF_THROUGH_FIELD(NDIS_OFFLOAD, Flags)

#if ((NDIS_SUPPORT_NDIS620))

typedef enum _NDIS_PM_WOL_PACKET
{
    NdisPMWoLPacketUnspecified,
    NdisPMWoLPacketBitmapPattern,
    NdisPMWoLPacketMagicPacket,
    NdisPMWoLPacketIPv4TcpSyn,
    NdisPMWoLPacketIPv6TcpSyn,
    NdisPMWoLPacketEapolRequestIdMessage,
    NdisPMWoLPacketMaximum
}NDIS_PM_WOL_PACKET, *PNDIS_PM_WOL_PACKET;

#define NDIS_PM_MAX_STRING_SIZE 64

typedef struct _NDIS_PM_COUNTED_STRING
{
    USHORT      Length;
    WCHAR       String[NDIS_PM_MAX_STRING_SIZE + 1];
} NDIS_PM_COUNTED_STRING, *PNDIS_PM_COUNTED_STRING;

#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_PM_CAPABILITIES_REVISION_2              2

#endif

typedef struct _NDIS_PM_CAPABILITIES
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    ULONG                   SupportedWoLPacketPatterns;
    ULONG                   NumTotalWoLPatterns;
    ULONG                   MaxWoLPatternSize;
    ULONG                   MaxWoLPatternOffset;
    ULONG                   MaxWoLPacketSaveBuffer;
    ULONG                   SupportedProtocolOffloads;
    ULONG                   NumArpOffloadIPv4Addresses;
    ULONG                   NumNSOffloadIPv6Addresses;
    NDIS_DEVICE_POWER_STATE MinMagicPacketWakeUp;
    NDIS_DEVICE_POWER_STATE MinPatternWakeUp;
    NDIS_DEVICE_POWER_STATE MinLinkChangeWakeUp;
#if (NDIS_SUPPORT_NDIS630)
    ULONG                   SupportedWakeUpEvents;
    ULONG                   MediaSpecificWakeUpEvents;
#endif
}NDIS_PM_CAPABILITIES, *PNDIS_PM_CAPABILITIES;

#define NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_CAPABILITIES, MinLinkChangeWakeUp)

#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_2     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_PM_CAPABILITIES, MediaSpecificWakeUpEvents)

#endif

typedef struct _NDIS_PM_WOL_PATTERN
{
    NDIS_OBJECT_HEADER          Header;
    ULONG                       Flags;
    ULONG                       Priority;
    NDIS_PM_WOL_PACKET          WoLPacketType;
    NDIS_PM_COUNTED_STRING      FriendlyName;
    ULONG                       PatternId;
    ULONG                       NextWoLPatternOffset;
    union _WOL_PATTERN
    {
        struct _IPV4_TCP_SYN_WOL_PACKET_PARAMETERS
        {
            ULONG   Flags;
            UCHAR   IPv4SourceAddress[4];
            UCHAR   IPv4DestAddress[4];
            USHORT  TCPSourcePortNumber;
            USHORT  TCPDestPortNumber;
        }IPv4TcpSynParameters;
        struct _IPV6_TCP_SYN_WOL_PACKET_PARAMETERS
        {
            ULONG   Flags;
            UCHAR   IPv6SourceAddress[16];
            UCHAR   IPv6DestAddress[16];
            USHORT  TCPSourcePortNumber;
            USHORT  TCPDestPortNumber;
        }IPv6TcpSynParameters;
        struct _EAPOL_REQUEST_ID_MESSAGE_WOL_PACKET_PARAMETERS
        {
            ULONG   Flags;
        } EapolRequestIdMessageParameters;
        struct _WOL_BITMAP_PATTERN
        {
            ULONG   Flags;
            ULONG   MaskOffset;
            ULONG   MaskSize;
            ULONG   PatternOffset;
            ULONG   PatternSize;
        }WoLBitMapPattern;
    }WoLPattern;
}NDIS_PM_WOL_PATTERN, *PNDIS_PM_WOL_PATTERN;

#define NDIS_RECEIVE_FILTER_MAC_HEADER_SUPPORTED            0x00000001

#define NDIS_RECEIVE_FILTER_MAC_HEADER_DEST_ADDR_SUPPORTED      0x00000001

#define NDIS_RECEIVE_FILTER_MAC_HEADER_VLAN_ID_SUPPORTED        0x00000008

#define NDIS_RECEIVE_FILTER_TEST_HEADER_FIELD_EQUAL_SUPPORTED               0x00000001

#define NDIS_RECEIVE_FILTER_VM_QUEUE_SUPPORTED                      0x00000002

#define NDIS_RECEIVE_FILTER_LOOKAHEAD_SPLIT_SUPPORTED               0x00000004

#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_RECEIVE_FILTER_DYNAMIC_PROCESSOR_AFFINITY_CHANGE_SUPPORTED 0x00000008

#endif

#define NDIS_RECEIVE_FILTER_VMQ_FILTERS_ENABLED                     0x00000001

#define NDIS_RECEIVE_FILTER_VM_QUEUES_ENABLED                       0x00000001

typedef struct _NDIS_RECEIVE_FILTER_CAPABILITIES
{
    _In_  NDIS_OBJECT_HEADER          Header;
    _In_  ULONG                       Flags;
    _In_  ULONG                       EnabledFilterTypes;
    _In_  ULONG                       EnabledQueueTypes;
    _In_  ULONG                       NumQueues;
    _In_  ULONG                       SupportedQueueProperties;
    _In_  ULONG                       SupportedFilterTests;
    _In_  ULONG                       SupportedHeaders;
    _In_  ULONG                       SupportedMacHeaderFields;
    _In_  ULONG                       MaxMacHeaderFilters;
    _In_  ULONG                       MaxQueueGroups;
    _In_  ULONG                       MaxQueuesPerQueueGroup;
    _In_  ULONG                       MinLookaheadSplitSize;
    _In_  ULONG                       MaxLookaheadSplitSize;
#if (NDIS_SUPPORT_NDIS630)
    _In_  ULONG                       SupportedARPHeaderFields;
    _In_  ULONG                       SupportedIPv4HeaderFields;
    _In_  ULONG                       SupportedIPv6HeaderFields;
    _In_  ULONG                       SupportedUdpHeaderFields;
    _In_  ULONG                       MaxFieldTestsPerPacketCoalescingFilter;
    _In_  ULONG                       MaxPacketCoalescingFilters;
    _In_  ULONG                       NdisReserved;
#endif
} NDIS_RECEIVE_FILTER_CAPABILITIES, *PNDIS_RECEIVE_FILTER_CAPABILITIES;

#define NDIS_SIZEOF_RECEIVE_FILTER_CAPABILITIES_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_CAPABILITIES, MaxLookaheadSplitSize)

typedef struct _NDIS_NIC_SWITCH_CAPABILITIES
{
    _In_  NDIS_OBJECT_HEADER          Header;
    _In_  ULONG                       Flags;
    _In_  ULONG                       NdisReserved1;
    _In_  ULONG                       NumTotalMacAddresses;
    _In_  ULONG                       NumMacAddressesPerPort;
    _In_  ULONG                       NumVlansPerPort;
    _In_  ULONG                       NdisReserved2;
    _In_  ULONG                       NdisReserved3;
#if (NDIS_SUPPORT_NDIS630)
    _In_  ULONG                       NicSwitchCapabilities;
    _In_  ULONG                       MaxNumSwitches;
    _In_  ULONG                       MaxNumVPorts;
    _In_  ULONG                       NdisReserved4;
    _In_  ULONG                       MaxNumVFs;
    _In_  ULONG                       MaxNumQueuePairs;
    _In_  ULONG                       NdisReserved5;
    _In_  ULONG                       NdisReserved6;
    _In_  ULONG                       NdisReserved7;
    _In_  ULONG                       MaxNumQueuePairsPerNonDefaultVPort;
    _In_  ULONG                       NdisReserved8;
    _In_  ULONG                       NdisReserved9;
    _In_  ULONG                       NdisReserved10;
    _In_  ULONG                       NdisReserved11;
    _In_  ULONG                       NdisReserved12;
    _In_  ULONG                       MaxNumMacAddresses;
    _In_  ULONG                       NdisReserved13;
    _In_  ULONG                       NdisReserved14;
    _In_  ULONG                       NdisReserved15;
    _In_  ULONG                       NdisReserved16;
    _In_  ULONG                       NdisReserved17;
#endif
#if (NDIS_SUPPORT_NDIS660)
    _In_  ULONG                       MaxNumRssCapableNonDefaultPFVPorts;
    _In_  ULONG                       NumberOfIndirectionTableEntriesForDefaultVPort;
    _In_  ULONG                       NumberOfIndirectionTableEntriesPerNonDefaultPFVPort;
    _In_  ULONG                       MaxNumQueuePairsForDefaultVPort;
#endif
}NDIS_NIC_SWITCH_CAPABILITIES, *PNDIS_NIC_SWITCH_CAPABILITIES;

#define NDIS_SIZEOF_NIC_SWITCH_CAPABILITIES_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_NIC_SWITCH_CAPABILITIES, NdisReserved3)

typedef ULONG NDIS_RECEIVE_QUEUE_ID, *PNDIS_RECEIVE_QUEUE_ID;

typedef ULONG NDIS_RECEIVE_QUEUE_GROUP_ID, *PNDIS_RECEIVE_QUEUE_GROUP_ID;

#define NDIS_DEFAULT_RECEIVE_QUEUE_ID               0

typedef ULONG NDIS_RECEIVE_FILTER_ID, *PNDIS_RECEIVE_FILTER_ID;

typedef enum _NDIS_RECEIVE_FILTER_TYPE
{
    NdisReceiveFilterTypeUndefined,
    NdisReceiveFilterTypeVMQueue,
    NdisReceiveFilterTypePacketCoalescing,
    NdisReceiveFilterTypeMaximum
}NDIS_RECEIVE_FILTER_TYPE, *PNDIS_RECEIVE_FILTER_TYPE;

typedef enum _NDIS_FRAME_HEADER
{
    NdisFrameHeaderUndefined,
    NdisFrameHeaderMac,
    NdisFrameHeaderArp,
    NdisFrameHeaderIPv4,
    NdisFrameHeaderIPv6,
    NdisFrameHeaderUdp,
    NdisFrameHeaderMaximum
}NDIS_FRAME_HEADER, *PNDIS_FRAME_HEADER;

typedef enum _NDIS_MAC_HEADER_FIELD
{
    NdisMacHeaderFieldUndefined,
    NdisMacHeaderFieldDestinationAddress,
    NdisMacHeaderFieldSourceAddress,
    NdisMacHeaderFieldProtocol,
    NdisMacHeaderFieldVlanId,
    NdisMacHeaderFieldPriority,
    NdisMacHeaderFieldPacketType,
    NdisMacHeaderFieldMaximum
}NDIS_MAC_HEADER_FIELD, *PNDIS_MAC_HEADER_FIELD;

typedef enum _NDIS_ARP_HEADER_FIELD
{
    NdisARPHeaderFieldUndefined,
    NdisARPHeaderFieldOperation,
    NdisARPHeaderFieldSPA,
    NdisARPHeaderFieldTPA,
    NdisARPHeaderFieldMaximum
} NDIS_ARP_HEADER_FIELD, *PNDIS_ARP_HEADER_FIELD;

typedef enum _NDIS_IPV4_HEADER_FIELD
{
    NdisIPv4HeaderFieldUndefined,
    NdisIPv4HeaderFieldProtocol,
    NdisIPv4HeaderFieldMaximum
}NDIS_IPV4_HEADER_FIELD, *PNDIS_IPV4_HEADER_FIELD;

typedef enum _NDIS_IPV6_HEADER_FIELD
{
    NdisIPv6HeaderFieldUndefined,
    NdisIPv6HeaderFieldProtocol,
    NdisIPv6HeaderFieldMaximum
}NDIS_IPV6_HEADER_FIELD, *PNDIS_IPV6_HEADER_FIELD;

typedef enum _NDIS_UDP_HEADER_FIELD
{
    NdisUdpHeaderFieldUndefined,
    NdisUdpHeaderFieldDestinationPort,
    NdisUdpHeaderFieldMaximum
}NDIS_UDP_HEADER_FIELD, *PNDIS_UDP_HEADER_FIELD;

typedef enum _NDIS_RECEIVE_FILTER_TEST
{
    NdisReceiveFilterTestUndefined,
    NdisReceiveFilterTestEqual,
    NdisReceiveFilterTestMaskEqual,
    NdisReceiveFilterTestNotEqual,
    NdisReceiveFilterTestMaximum
}NDIS_RECEIVE_FILTER_TEST, *PNDIS_RECEIVE_FILTER_TEST;

#define NDIS_RECEIVE_FILTER_FIELD_MAC_HEADER_VLAN_UNTAGGED_OR_ZERO  0x00000001

typedef struct _NDIS_RECEIVE_FILTER_FIELD_PARAMETERS
{
    _In_ NDIS_OBJECT_HEADER       Header;
    _In_ ULONG                    Flags;
    _In_ NDIS_FRAME_HEADER        FrameHeader;
    _In_ NDIS_RECEIVE_FILTER_TEST ReceiveFilterTest;
    _In_ union _HEADER_FIELD
    {
        NDIS_MAC_HEADER_FIELD       MacHeaderField;
        NDIS_ARP_HEADER_FIELD       ArpHeaderField;
        NDIS_IPV4_HEADER_FIELD      IPv4HeaderField;
        NDIS_IPV6_HEADER_FIELD      IPv6HeaderField;
        NDIS_UDP_HEADER_FIELD       UdpHeaderField;
    }HeaderField;
    _In_ union _FIELD_VALUE
    {
        UCHAR               FieldByteValue;
        USHORT              FieldShortValue;
        ULONG               FieldLongValue;
        ULONG64             FieldLong64Value;
        UCHAR               FieldByteArrayValue[16];
    }FieldValue;
    _In_ union _RESULT_VALUE
    {
        UCHAR               ResultByteValue;
        USHORT              ResultShortValue;
        ULONG               ResultLongValue;
        ULONG64             ResultLong64Value;
        UCHAR               ResultByteArrayValue[16];
    }ResultValue;
}NDIS_RECEIVE_FILTER_FIELD_PARAMETERS, *PNDIS_RECEIVE_FILTER_FIELD_PARAMETERS;

#define NDIS_RECEIVE_FILTER_PARAMETERS_REVISION_1       1

typedef
  _When_(FieldParametersArrayNumElements == 0, _Struct_size_bytes_(sizeof(NDIS_RECEIVE_FILTER_PARAMETERS)))
  _When_(FieldParametersArrayNumElements > 0, _Struct_size_bytes_(FieldParametersArrayOffset +FieldParametersArrayNumElements*FieldParametersArrayElementSize))
    struct _NDIS_RECEIVE_FILTER_PARAMETERS
{
    _In_    NDIS_OBJECT_HEADER                     Header;
    _In_    ULONG                                  Flags;
    _In_    NDIS_RECEIVE_FILTER_TYPE               FilterType;
    _In_    NDIS_RECEIVE_QUEUE_ID                  QueueId;
    _Inout_ NDIS_RECEIVE_FILTER_ID                 FilterId;
    _In_    ULONG                                  FieldParametersArrayOffset;
    _In_    ULONG                                  FieldParametersArrayNumElements;
    _In_    ULONG                                  FieldParametersArrayElementSize;
    _In_    ULONG                                  RequestedFilterIdBitCount;
#if (NDIS_SUPPORT_NDIS630)
    _In_    ULONG                                  MaxCoalescingDelay;
    _In_    NDIS_NIC_SWITCH_VPORT_ID               VPortId;
#endif
}NDIS_RECEIVE_FILTER_PARAMETERS, *PNDIS_RECEIVE_FILTER_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_FILTER_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_PARAMETERS, RequestedFilterIdBitCount)

#define NDIS_RECEIVE_FILTER_CLEAR_PARAMETERS_REVISION_1       1

typedef struct _NDIS_RECEIVE_FILTER_CLEAR_PARAMETERS
{
    _In_ NDIS_OBJECT_HEADER                           Header;
    _In_ ULONG                                        Flags;
    _In_    NDIS_RECEIVE_QUEUE_ID                     QueueId;
    _In_ NDIS_RECEIVE_FILTER_ID                       FilterId;
}NDIS_RECEIVE_FILTER_CLEAR_PARAMETERS, *PNDIS_RECEIVE_FILTER_CLEAR_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_FILTER_CLEAR_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_FILTER_CLEAR_PARAMETERS, FilterId)

typedef enum _NDIS_RECEIVE_QUEUE_TYPE
{
    NdisReceiveQueueTypeUnspecified,
    NdisReceiveQueueTypeVMQueue,
    NdisReceiveQueueTypeMaximum
}NDIS_RECEIVE_QUEUE_TYPE, *PNDIS_RECEIVE_QUEUE_TYPE;

#define NDIS_RECEIVE_QUEUE_PARAMETERS_LOOKAHEAD_SPLIT_REQUIRED                  0x00000002

#define NDIS_RECEIVE_QUEUE_PARAMETERS_FLAGS_CHANGED                             0x00010000

#define NDIS_RECEIVE_QUEUE_PARAMETERS_PROCESSOR_AFFINITY_CHANGED                0x00020000

#define NDIS_RECEIVE_QUEUE_PARAMETERS_SUGGESTED_RECV_BUFFER_NUMBERS_CHANGED     0x00040000

#define NDIS_RECEIVE_QUEUE_PARAMETERS_CHANGE_MASK                               0xFFFF0000

typedef NDIS_IF_COUNTED_STRING NDIS_QUEUE_NAME, *PNDIS_QUEUE_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_VM_NAME, *PNDIS_VM_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_VM_FRIENDLYNAME, *PNDIS_VM_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_NAME, *PNDIS_SWITCH_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_FRIENDLYNAME, *PNDIS_SWITCH_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_PORT_NAME, *PNDIS_SWITCH_PORT_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_PORT_FRIENDLYNAME, *PNDIS_SWITCH_PORT_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_NIC_NAME, *PNDIS_SWITCH_NIC_NAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_NIC_FRIENDLYNAME, *PNDIS_SWITCH_NIC_FRIENDLYNAME;

typedef NDIS_IF_COUNTED_STRING NDIS_SWITCH_EXTENSION_FRIENDLYNAME, *PNDIS_SWITCH_EXTENSION_FRIENDLYNAME;

#if ((NDIS_SUPPORT_NDIS650))

typedef ULONG NDIS_QOS_SQ_ID, *PNDIS_QOS_SQ_ID;

#endif

#define NDIS_RECEIVE_QUEUE_PARAMETERS_REVISION_1       1

typedef struct _NDIS_RECEIVE_QUEUE_PARAMETERS
{
    _In_    NDIS_OBJECT_HEADER               Header;
    _Inout_ ULONG                            Flags;
    _In_    NDIS_RECEIVE_QUEUE_TYPE          QueueType;
    _Inout_ NDIS_RECEIVE_QUEUE_ID            QueueId;
    _Inout_ NDIS_RECEIVE_QUEUE_GROUP_ID      QueueGroupId;
    _In_    GROUP_AFFINITY                   ProcessorAffinity;
    _In_    ULONG                            NumSuggestedReceiveBuffers;
   _Out_    ULONG                            MSIXTableEntry;
    _In_    ULONG                            LookaheadSize;
    _In_    NDIS_VM_NAME                     VmName;
    _In_    NDIS_QUEUE_NAME                  QueueName;
#if (NDIS_SUPPORT_NDIS630)
    _In_    ULONG                            PortId;
    _Out_   ULONG                            InterruptCoalescingDomainId;
#endif
#if (NDIS_SUPPORT_NDIS650)
    _In_    NDIS_QOS_SQ_ID                   QosSqId;
#endif
}NDIS_RECEIVE_QUEUE_PARAMETERS, *PNDIS_RECEIVE_QUEUE_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_QUEUE_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_PARAMETERS, QueueName)

#define NDIS_RECEIVE_QUEUE_FREE_PARAMETERS_REVISION_1       1

typedef struct _NDIS_RECEIVE_QUEUE_FREE_PARAMETERS
{
    _In_ NDIS_OBJECT_HEADER                   Header;
    _In_ ULONG                                Flags;
    _In_ NDIS_RECEIVE_QUEUE_ID                QueueId;
}NDIS_RECEIVE_QUEUE_FREE_PARAMETERS, *PNDIS_RECEIVE_QUEUE_FREE_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_QUEUE_FREE_PARAMETERS_REVISION_1     \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_FREE_PARAMETERS, QueueId)

typedef enum _NDIS_RECEIVE_QUEUE_OPERATIONAL_STATE
{
    NdisReceiveQueueOperationalStateUndefined,
    NdisReceiveQueueOperationalStateRunning,
    NdisReceiveQueueOperationalStatePaused,
    NdisReceiveQueueOperationalStateDmaStopped,
    NdisReceiveQueueOperationalStateMaximum
}NDIS_RECEIVE_QUEUE_OPERATIONAL_STATE, *PNDIS_RECEIVE_QUEUE_OPERATIONAL_STATE;

typedef struct _NDIS_RECEIVE_FILTER_INFO
{
    NDIS_OBJECT_HEADER                       Header;
    ULONG                                    Flags;
    NDIS_RECEIVE_FILTER_TYPE                 FilterType;
    NDIS_RECEIVE_FILTER_ID                   FilterId;
}NDIS_RECEIVE_FILTER_INFO, *PNDIS_RECEIVE_FILTER_INFO;

typedef struct _NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_PARAMETERS
{
    _In_  NDIS_OBJECT_HEADER      Header;
    _In_  ULONG                   Flags;
    _In_  NDIS_RECEIVE_QUEUE_ID   QueueId;
    _Out_ NDIS_STATUS             CompletionStatus;
}NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_PARAMETERS, *PNDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_PARAMETERS;

#define NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY_REVISION_1         1

typedef
        _When_(NumElements >= 1,
            _Struct_size_bytes_(FirstElementOffset + NumElements*ElementSize))
        _When_(NumElements == 0,
            _Struct_size_bytes_(sizeof(NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY)))
    struct _NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY
{
    NDIS_OBJECT_HEADER                          Header;
    ULONG                                       Flags;
    ULONG                                       FirstElementOffset;
    ULONG                                       NumElements;
    ULONG                                       ElementSize;
}NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY, *PNDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY;

#define NDIS_SIZEOF_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_QUEUE_ALLOCATION_COMPLETE_ARRAY, ElementSize)

#endif
#if (((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6))

typedef ULONG NDIS_RSS_CAPS_FLAGS;

typedef struct _NDIS_RECEIVE_SCALE_CAPABILITIES
{
    NDIS_OBJECT_HEADER    Header;
    NDIS_RSS_CAPS_FLAGS   CapabilitiesFlags;
    ULONG                 NumberOfInterruptMessages;
    ULONG                 NumberOfReceiveQueues;
    #if (NDIS_SUPPORT_NDIS630)
    USHORT                NumberOfIndirectionTableEntries;
    #endif
} NDIS_RECEIVE_SCALE_CAPABILITIES, *PNDIS_RECEIVE_SCALE_CAPABILITIES;

#define NDIS_HASH_FUNCTION_MASK                 0x000000FF

#define NDIS_HASH_TYPE_MASK                     0x00FFFF00

#define NDIS_RSS_PARAM_FLAG_BASE_CPU_UNCHANGED              0x0001

#define NDIS_RSS_PARAM_FLAG_HASH_INFO_UNCHANGED             0x0002

#define NDIS_RSS_PARAM_FLAG_ITABLE_UNCHANGED                0x0004

#define NDIS_RSS_PARAM_FLAG_HASH_KEY_UNCHANGED              0x0008

#define NDIS_RSS_PARAM_FLAG_DISABLE_RSS                     0x0010

#define NDIS_RECEIVE_SCALE_PARAMETERS_REVISION_1     1

typedef struct _NDIS_RECEIVE_SCALE_PARAMETERS
{
    NDIS_OBJECT_HEADER      Header;
    USHORT                  Flags;
    USHORT                  BaseCpuNumber;
    ULONG                   HashInformation;
    USHORT                  IndirectionTableSize;
    ULONG                   IndirectionTableOffset;
    USHORT                  HashSecretKeySize;
    ULONG                   HashSecretKeyOffset;
#if NDIS_SUPPORT_NDIS620
    ULONG                   ProcessorMasksOffset;
    ULONG                   NumberOfProcessorMasks;
    ULONG                   ProcessorMasksEntrySize;
#endif
#if NDIS_SUPPORT_NDIS660
    PROCESSOR_NUMBER        DefaultProcessorNumber;
#endif
} NDIS_RECEIVE_SCALE_PARAMETERS, *PNDIS_RECEIVE_SCALE_PARAMETERS;

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS, HashSecretKeyOffset)

#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_1      128
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_1        40

#if (NDIS_SUPPORT_NDIS620)

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_2    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS, ProcessorMasksEntrySize)

#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_2      (128*sizeof(PROCESSOR_NUMBER))
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_2        40

#endif
#if (NDIS_SUPPORT_NDIS660)

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_REVISION_3    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS, DefaultProcessorNumber)

#define NDIS_RSS_INDIRECTION_TABLE_MAX_SIZE_REVISION_3      (128*sizeof(PROCESSOR_NUMBER))
#define NDIS_RSS_HASH_SECRET_KEY_MAX_SIZE_REVISION_3        40

#endif
#if (NDIS_SUPPORT_NDIS680)

#define NDIS_RECEIVE_SCALE_PARAM_ENABLE_RSS                 0x00000001

#define NDIS_RECEIVE_SCALE_PARAM_HASH_INFO_CHANGED          0x00000002

#define NDIS_RECEIVE_SCALE_PARAM_HASH_KEY_CHANGED           0x00000004

#define NDIS_RECEIVE_SCALE_PARAM_NUMBER_OF_QUEUES_CHANGED   0x00000008

#define NDIS_RECEIVE_SCALE_PARAM_NUMBER_OF_ENTRIES_CHANGED  0x00000010

typedef struct _NDIS_RECEIVE_SCALE_PARAMETERS_V2
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    ULONG                   HashInformation;
    ULONG                   HashSecretKeySize;
    ULONG                   HashSecretKeyOffset;
    ULONG                   NumberOfQueues;
    ULONG                   NumberOfIndirectionTableEntries;
} NDIS_RECEIVE_SCALE_PARAMETERS_V2, *PNDIS_RECEIVE_SCALE_PARAMETERS_V2;

#define NDIS_SIZEOF_RECEIVE_SCALE_PARAMETERS_V2_REVISION_1 \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RECEIVE_SCALE_PARAMETERS_V2, \
                                 NumberOfIndirectionTableEntries)

#define NDIS_RSS_SET_INDIRECTION_ENTRY_FLAG_PRIMARY_PROCESSOR   0x00000001

#define NDIS_RSS_SET_INDIRECTION_ENTRY_FLAG_DEFAULT_PROCESSOR   0x00000002

typedef struct _NDIS_RSS_SET_INDIRECTION_ENTRY
{
    NDIS_NIC_SWITCH_ID          SwitchId;
    NDIS_NIC_SWITCH_VPORT_ID    VPortId;
    ULONG                       Flags;
    USHORT                      IndirectionTableIndex;
    PROCESSOR_NUMBER            TargetProcessorNumber;
    NDIS_STATUS                 EntryStatus;
} NDIS_RSS_SET_INDIRECTION_ENTRY, *PNDIS_RSS_SET_INDIRECTION_ENTRY;

typedef struct _NDIS_RSS_SET_INDIRECTION_ENTRIES
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    ULONG                   RssEntrySize;
    ULONG                   RssEntryTableOffset;
    ULONG                   NumberOfRssEntries;
} NDIS_RSS_SET_INDIRECTION_ENTRIES, *PNDIS_RSS_SET_INDIRECTION_ENTRIES;

#define NDIS_SIZEOF_RSS_SET_INDIRECTION_ENTRIES_REVISION_1 \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RSS_SET_INDIRECTION_ENTRIES, \
                                 NumberOfRssEntries)

#endif
#if (NDIS_SUPPORT_NDIS620)

typedef struct _NDIS_RSS_PROCESSOR
{
    PROCESSOR_NUMBER ProcNum;
    USHORT           PreferenceIndex;
    USHORT           Reserved;
} NDIS_RSS_PROCESSOR, *PNDIS_RSS_PROCESSOR;

#define NDIS_SIZEOF_RSS_PROCESSOR_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RSS_PROCESSOR, PreferenceIndex)

#define NDIS_RSS_PROCESSOR_INFO_REVISION_1      1

#if ((NDIS_SUPPORT_NDIS630))

#define NDIS_RSS_PROCESSOR_INFO_REVISION_2      2

typedef enum _NDIS_RSS_PROFILE
{
    NdisRssProfileClosest = 1,
    NdisRssProfileClosestStatic,
    NdisRssProfileNuma,
    NdisRssProfileNumaStatic,
    NdisRssProfileConservative,
#if (NDIS_SUPPORT_NDIS688)
    NdisRssProfileBalanced,
#endif
    NdisRssProfileMaximum,
} NDIS_RSS_PROFILE, *PNDIS_RSS_PROFILE;

#endif

typedef struct _NDIS_RSS_PROCESSOR_INFO
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    PROCESSOR_NUMBER        RssBaseProcessor;
    ULONG                   MaxNumRssProcessors;
    USHORT                  PreferredNumaNode;
    ULONG                   RssProcessorArrayOffset;
    ULONG                   RssProcessorCount;
    ULONG                   RssProcessorEntrySize;
#if (NDIS_SUPPORT_NDIS630)
    PROCESSOR_NUMBER        RssMaxProcessor;
    NDIS_RSS_PROFILE        RssProfile;
#endif
} NDIS_RSS_PROCESSOR_INFO, *PNDIS_RSS_PROCESSOR_INFO;

#define NDIS_SIZEOF_RSS_PROCESSOR_INFO_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RSS_PROCESSOR_INFO, RssProcessorEntrySize)

#if (NDIS_SUPPORT_NDIS630)
#define NDIS_SIZEOF_RSS_PROCESSOR_INFO_REVISION_2    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_RSS_PROCESSOR_INFO, RssProfile)
#endif

#endif
#if ((NDIS_SUPPORT_NDIS630))

#define OID_QOS_PARAMETERS                      0xFC050003

#define NDIS_QOS_MAXIMUM_PRIORITIES         8

#define NDIS_QOS_MAXIMUM_TRAFFIC_CLASSES    8

#define NDIS_QOS_CAPABILITIES_STRICT_TSA_SUPPORTED      0x00000001

#define NDIS_QOS_CAPABILITIES_REVISION_1    1

typedef _Struct_size_bytes_(Header.Size) struct _NDIS_QOS_CAPABILITIES
{
    _In_ NDIS_OBJECT_HEADER Header;
    _In_ ULONG              Flags;
    _In_ ULONG              MaxNumTrafficClasses;
    _In_ _Field_range_(<=, MaxNumTrafficClasses)
         ULONG              MaxNumEtsCapableTrafficClasses;
    _In_ _Field_range_(<=, MaxNumTrafficClasses)
         ULONG              MaxNumPfcEnabledTrafficClasses;
} NDIS_QOS_CAPABILITIES, *PNDIS_QOS_CAPABILITIES;

#define NDIS_SIZEOF_QOS_CAPABILITIES_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_QOS_CAPABILITIES, MaxNumPfcEnabledTrafficClasses)

#define NDIS_QOS_CLASSIFICATION_ENFORCED_BY_MINIPORT    0x01000000

#define NDIS_QOS_CONDITION_DEFAULT          0x1

#define NDIS_QOS_CONDITION_TCP_PORT         0x2

#define NDIS_QOS_CONDITION_ETHERTYPE        0x5

#define NDIS_QOS_CONDITION_MAXIMUM          0x7

#define NDIS_QOS_ACTION_PRIORITY            0x0

#define NDIS_QOS_ACTION_MAXIMUM             0x1

#define NDIS_QOS_CLASSIFICATION_ELEMENT_REVISION_1   1

typedef _Struct_size_bytes_(Header.Size) struct _NDIS_QOS_CLASSIFICATION_ELEMENT
{
    _In_    NDIS_OBJECT_HEADER  Header;
    _Inout_ ULONG               Flags;
    _In_ _Field_range_(<, NDIS_QOS_CONDITION_MAXIMUM)
            USHORT              ConditionSelector;
    _In_    USHORT              ConditionField;
    _In_ _Field_range_(<, NDIS_QOS_ACTION_MAXIMUM)
            USHORT              ActionSelector;
    _In_    USHORT              ActionField;
} NDIS_QOS_CLASSIFICATION_ELEMENT, *PNDIS_QOS_CLASSIFICATION_ELEMENT;

#define NDIS_SIZEOF_QOS_CLASSIFICATION_ELEMENT_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_QOS_CLASSIFICATION_ELEMENT, ActionField)

#define NDIS_QOS_PARAMETERS_ETS_CONFIGURED              0x00000002

#define NDIS_QOS_PARAMETERS_PFC_CONFIGURED              0x00000200

#define NDIS_QOS_PARAMETERS_CLASSIFICATION_CONFIGURED   0x00020000

#define NDIS_QOS_TSA_ETS                    0x2

#define NDIS_QOS_PARAMETERS_REVISION_1   1

typedef _Struct_size_bytes_(Header.Size) struct _NDIS_QOS_PARAMETERS
{
    _In_ NDIS_OBJECT_HEADER Header;
    _In_ ULONG              Flags;
    _In_ _Field_range_(0, NDIS_QOS_MAXIMUM_TRAFFIC_CLASSES)
         ULONG              NumTrafficClasses;
    _In_ UCHAR              PriorityAssignmentTable[NDIS_QOS_MAXIMUM_PRIORITIES];
    _In_ UCHAR              TcBandwidthAssignmentTable[NDIS_QOS_MAXIMUM_TRAFFIC_CLASSES];
    _In_ UCHAR              TsaAssignmentTable[NDIS_QOS_MAXIMUM_TRAFFIC_CLASSES];
    _In_ _Field_range_(0, (1ul << NDIS_QOS_MAXIMUM_PRIORITIES) - 1)
         ULONG              PfcEnable;
    _In_ ULONG              NumClassificationElements;
    _In_ ULONG              ClassificationElementSize;
    _In_ ULONG              FirstClassificationElementOffset;
} NDIS_QOS_PARAMETERS, *PNDIS_QOS_PARAMETERS;

#define NDIS_SIZEOF_QOS_PARAMETERS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_QOS_PARAMETERS, FirstClassificationElementOffset)

typedef NDIS_IF_COUNTED_STRING NDIS_NIC_SWITCH_FRIENDLYNAME, *PNDIS_NIC_SWITCH_FRIENDLYNAME;

#define NDIS_INVALID_VPORT_ID               (ULONG) -1

#define NDIS_DEFAULT_SWITCH_ID              0

#define NDIS_INVALID_SWITCH_ID              (ULONG) -1

typedef enum _NDIS_NIC_SWITCH_TYPE
{
    NdisNicSwitchTypeUnspecified,
    NdisNicSwitchTypeExternal,
    NdisNicSwitchTypeMax
} NDIS_NIC_SWITCH_TYPE, *PNDIS_NIC_SWITCH_TYPE;

typedef struct _NDIS_NIC_SWITCH_INFO
{
    _In_        NDIS_OBJECT_HEADER              Header;
    _In_        ULONG                           Flags;
    _In_        NDIS_NIC_SWITCH_TYPE            SwitchType;
    _In_        NDIS_NIC_SWITCH_ID              SwitchId;
    _In_        NDIS_NIC_SWITCH_FRIENDLYNAME    SwitchFriendlyName;
    _In_        ULONG                           NumVFs;
    _In_        ULONG                           NumAllocatedVFs;
    _In_        ULONG                           NumVPorts;
    _In_        ULONG                           NumActiveVPorts;
    _In_        ULONG                           NumQueuePairsForDefaultVPort;
    _In_        ULONG                           NumQueuePairsForNonDefaultVPorts;
    _In_        ULONG                           NumActiveDefaultVPortMacAddresses;
    _In_        ULONG                           NumActiveNonDefaultVPortMacAddresses;
    _In_        ULONG                           NumActiveDefaultVPortVlanIds;
    _In_        ULONG                           NumActiveNonDefaultVPortVlanIds;
}NDIS_NIC_SWITCH_INFO, *PNDIS_NIC_SWITCH_INFO;

#if ((NDIS_SUPPORT_NDIS650))
#if !defined(_NDIS_SWITCH_PORT_ID)

#define _NDIS_SWITCH_PORT_ID NDIS_SWITCH_PORT_ID

typedef UINT32 NDIS_SWITCH_PORT_ID, *PNDIS_SWITCH_PORT_ID;

typedef USHORT NDIS_SWITCH_NIC_INDEX, *PNDIS_SWITCH_NIC_INDEX;

#endif
#endif
#if !defined(_NDIS_SWITCH_PORT_ID)

#define _NDIS_SWITCH_PORT_ID NDIS_SWITCH_PORT_ID

typedef UINT32 NDIS_SWITCH_PORT_ID, *PNDIS_SWITCH_PORT_ID;

typedef USHORT NDIS_SWITCH_NIC_INDEX, *PNDIS_SWITCH_NIC_INDEX;

#endif

typedef GUID NDIS_SWITCH_OBJECT_INSTANCE_ID, *PNDIS_SWITCH_OBJECT_INSTANCE_ID;

typedef GUID NDIS_SWITCH_OBJECT_ID, *PNDIS_SWITCH_OBJECT_ID;

typedef USHORT NDIS_SWITCH_OBJECT_VERSION, *PNDIS_SWITCH_OBJECT_VERSION;

typedef USHORT NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION, *PNDIS_SWITCH_OBJECT_SERIALIZATION_VERSION;

#define NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION_1       1

typedef enum _NDIS_SWITCH_PORT_PROPERTY_TYPE
{
    NdisSwitchPortPropertyTypeUndefined,
    NdisSwitchPortPropertyTypeCustom,
    NdisSwitchPortPropertyTypeSecurity,
    NdisSwitchPortPropertyTypeVlan,
    NdisSwitchPortPropertyTypeProfile,
    NdisSwitchPortPropertyTypeIsolation,
    NdisSwitchPortPropertyTypeRoutingDomain,
    NdisSwitchPortPropertyTypeMaximum
} NDIS_SWITCH_PORT_PROPERTY_TYPE, *PNDIS_SWITCH_PORT_PROPERTY_TYPE;

typedef enum _NDIS_SWITCH_PORT_VLAN_MODE
{
    NdisSwitchPortVlanModeUnknown      = 0,
    NdisSwitchPortVlanModeAccess       = 1,
    NdisSwitchPortVlanModeTrunk        = 2,
    NdisSwitchPortVlanModePrivate      = 3,
    NdisSwitchPortVlanModeMax          = 4
} NDIS_SWITCH_PORT_VLAN_MODE, *PNDIS_SWITCH_PORT_VLAN_MODE;

typedef enum _NDIS_SWITCH_PORT_PVLAN_MODE
{
    NdisSwitchPortPvlanModeUndefined = 0,
    NdisSwitchPortPvlanModeIsolated,
    NdisSwitchPortPvlanModeCommunity,
    NdisSwitchPortPvlanModePromiscuous
} NDIS_SWITCH_PORT_PVLAN_MODE, *PNDIS_SWITCH_PORT_PVLAN_MODE;

#define NDIS_SWITCH_PORT_PROPERTY_VLAN_REVISION_1       1

typedef struct _NDIS_SWITCH_PORT_PROPERTY_VLAN
{
    NDIS_OBJECT_HEADER          Header;
    ULONG                       Flags;
    NDIS_SWITCH_PORT_VLAN_MODE  OperationMode;
    union
    {
        struct
        {
            UINT16              AccessVlanId;
            UINT16              NativeVlanId;
            UINT64              PruneVlanIdArray[64];
            UINT64              TrunkVlanIdArray[64];
        } VlanProperties;
        struct
        {
            NDIS_SWITCH_PORT_PVLAN_MODE
                                PvlanMode;
            UINT16              PrimaryVlanId;
            union
            {
                UINT16          SecondaryVlanId;
                UINT64          SecondaryVlanIdArray[64];
            };
        } PvlanProperties;
    };
} NDIS_SWITCH_PORT_PROPERTY_VLAN, *PNDIS_SWITCH_PORT_PROPERTY_VLAN;

#define NDIS_SWITCH_PORT_PROPERTY_PARAMETERS_REVISION_1     1

typedef struct _NDIS_SWITCH_PORT_PROPERTY_PARAMETERS
{
    NDIS_OBJECT_HEADER                      Header;
    ULONG                                   Flags;
    NDIS_SWITCH_PORT_ID                     PortId;
    NDIS_SWITCH_PORT_PROPERTY_TYPE          PropertyType;
    NDIS_SWITCH_OBJECT_ID                   PropertyId;
    NDIS_SWITCH_OBJECT_VERSION              PropertyVersion;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION SerializationVersion;
    NDIS_SWITCH_OBJECT_INSTANCE_ID          PropertyInstanceId;
    ULONG                                   PropertyBufferLength;
    ULONG                                   PropertyBufferOffset;
    ULONG                                   Reserved;
} NDIS_SWITCH_PORT_PROPERTY_PARAMETERS, *PNDIS_SWITCH_PORT_PROPERTY_PARAMETERS;

#define NDIS_SIZEOF_NDIS_SWITCH_PORT_PROPERTY_PARAMETERS_REVISION_1    \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_PORT_PROPERTY_PARAMETERS, Reserved)

#define NDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS_REVISION_1  1

typedef struct _NDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS
{
    NDIS_OBJECT_HEADER                      Header;
    ULONG                                   Flags;
    NDIS_SWITCH_PORT_ID                     PortId;
    NDIS_SWITCH_PORT_PROPERTY_TYPE          PropertyType;
    NDIS_SWITCH_OBJECT_ID                   PropertyId;
    NDIS_SWITCH_OBJECT_INSTANCE_ID          PropertyInstanceId;
} NDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS, *PNDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS;

#define NDIS_SIZEOF_NDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS_REVISION_1  \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_PORT_PROPERTY_DELETE_PARAMETERS, PropertyInstanceId)

#define NDIS_SWITCH_PORT_PROPERTY_ENUM_PARAMETERS_REVISION_1    1

typedef struct _NDIS_SWITCH_PORT_PROPERTY_ENUM_PARAMETERS
{
    NDIS_OBJECT_HEADER                      Header;
    ULONG                                   Flags;
    NDIS_SWITCH_PORT_ID                     PortId;
    NDIS_SWITCH_PORT_PROPERTY_TYPE          PropertyType;
    NDIS_SWITCH_OBJECT_ID                   PropertyId;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION SerializationVersion;
    ULONG                                   FirstPropertyOffset;
    ULONG                                   NumProperties;
    USHORT                                  Reserved;
} NDIS_SWITCH_PORT_PROPERTY_ENUM_PARAMETERS, *PNDIS_SWITCH_PORT_PROPERTY_ENUM_PARAMETERS;

#define NDIS_SWITCH_PORT_PROPERTY_ENUM_PARAMETERS_GET_FIRST_INFO(_PortEnumParams_)\
            ((PNDIS_SWITCH_PORT_PROPERTY_ENUM_INFO)((PUCHAR)(_PortEnumParams_) + \
                (_PortEnumParams_)->FirstPropertyOffset))

typedef struct _NDIS_SWITCH_PORT_PROPERTY_ENUM_INFO
{
    NDIS_OBJECT_HEADER                      Header;
    ULONG                                   Flags;
    NDIS_SWITCH_OBJECT_VERSION              PropertyVersion;
    NDIS_SWITCH_OBJECT_INSTANCE_ID          PropertyInstanceId;
    ULONG                                   QwordAlignedPropertyBufferLength;
    ULONG                                   PropertyBufferLength;
    ULONG                                   PropertyBufferOffset;
} NDIS_SWITCH_PORT_PROPERTY_ENUM_INFO, *PNDIS_SWITCH_PORT_PROPERTY_ENUM_INFO;

#define NDIS_SWITCH_PORT_PROPERTY_ENUM_INFO_GET_PROPERTY(_PortEnumInfo_)\
            ((PVOID)((PUCHAR)(_PortEnumInfo_) + (_PortEnumInfo_)->PropertyBufferOffset))

typedef enum _NDIS_SWITCH_PORT_FEATURE_STATUS_TYPE
{
    NdisSwitchPortFeatureStatusTypeUndefined,
    NdisSwitchPortFeatureStatusTypeCustom,
    NdisSwitchPortFeatureStatusTypeMaximum
} NDIS_SWITCH_PORT_FEATURE_STATUS_TYPE, *PNDIS_SWITCH_PORT_FEATURE_STATUS_TYPE;

typedef struct _NDIS_SWITCH_PORT_FEATURE_STATUS_PARAMETERS
{
    NDIS_OBJECT_HEADER                            Header;
    ULONG                                         Flags;
    NDIS_SWITCH_PORT_ID                           PortId;
    NDIS_SWITCH_PORT_FEATURE_STATUS_TYPE          FeatureStatusType;
    NDIS_SWITCH_OBJECT_ID                         FeatureStatusId;
    NDIS_SWITCH_OBJECT_VERSION                    FeatureStatusVersion;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION      SerializationVersion;
    NDIS_SWITCH_OBJECT_INSTANCE_ID                FeatureStatusInstanceId;
    ULONG                                         FeatureStatusBufferLength;
    ULONG                                         FeatureStatusBufferOffset;
    ULONG                                         Reserved;
} NDIS_SWITCH_PORT_FEATURE_STATUS_PARAMETERS, *PNDIS_SWITCH_PORT_FEATURE_STATUS_PARAMETERS;

typedef enum _NDIS_SWITCH_PROPERTY_TYPE
{
    NdisSwitchPropertyTypeUndefined,
    NdisSwitchPropertyTypeCustom,
    NdisSwitchPropertyTypeMaximum
} NDIS_SWITCH_PROPERTY_TYPE, *PNDIS_SWITCH_PROPERTY_TYPE;

typedef struct _NDIS_SWITCH_PROPERTY_CUSTOM
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    ULONG                   PropertyBufferLength;
    ULONG                   PropertyBufferOffset;
} NDIS_SWITCH_PROPERTY_CUSTOM, *PNDIS_SWITCH_PROPERTY_CUSTOM;

#define NDIS_SWITCH_PROPERTY_CUSTOM_GET_BUFFER(_SwitchPropertyCustom_)\
            ((PVOID)((PUCHAR)(_SwitchPropertyCustom_) + (_SwitchPropertyCustom_)->PropertyBufferOffset))

#define NDIS_SWITCH_PROPERTY_PARAMETERS_REVISION_1  1

typedef struct _NDIS_SWITCH_PROPERTY_PARAMETERS
{
    NDIS_OBJECT_HEADER                         Header;
    ULONG                                      Flags;
    NDIS_SWITCH_PROPERTY_TYPE                  PropertyType;
    NDIS_SWITCH_OBJECT_ID                      PropertyId;
    NDIS_SWITCH_OBJECT_VERSION                 PropertyVersion;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION   SerializationVersion;
    NDIS_SWITCH_OBJECT_INSTANCE_ID             PropertyInstanceId;
    ULONG                                      PropertyBufferLength;
    ULONG                                      PropertyBufferOffset;
} NDIS_SWITCH_PROPERTY_PARAMETERS, *PNDIS_SWITCH_PROPERTY_PARAMETERS;

#define NDIS_SIZEOF_NDIS_SWITCH_PROPERTY_PARAMETERS_REVISION_1   \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_PROPERTY_PARAMETERS, PropertyBufferOffset)

#define NDIS_SWITCH_PROPERTY_PARAMETERS_GET_PROPERTY(_SwitchParameters_)\
            ((PVOID)((PUCHAR)(_SwitchParameters_) + (_SwitchParameters_)->PropertyBufferOffset))

#define NDIS_SWITCH_PROPERTY_DELETE_PARAMETERS_REVISION_1  1

typedef struct _NDIS_SWITCH_PROPERTY_DELETE_PARAMETERS
{
    NDIS_OBJECT_HEADER                 Header;
    ULONG                              Flags;
    NDIS_SWITCH_PROPERTY_TYPE          PropertyType;
    NDIS_SWITCH_OBJECT_ID              PropertyId;
    NDIS_SWITCH_OBJECT_INSTANCE_ID     PropertyInstanceId;
} NDIS_SWITCH_PROPERTY_DELETE_PARAMETERS, *PNDIS_SWITCH_PROPERTY_DELETE_PARAMETERS;

#define NDIS_SIZEOF_NDIS_SWITCH_PROPERTY_DELETE_PARAMETERS_REVISION_1   \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_PROPERTY_DELETE_PARAMETERS, PropertyInstanceId)

typedef struct _NDIS_SWITCH_PROPERTY_ENUM_INFO
{
    NDIS_OBJECT_HEADER               Header;
    ULONG                            Flags;
    NDIS_SWITCH_OBJECT_INSTANCE_ID   PropertyInstanceId;
    NDIS_SWITCH_OBJECT_VERSION       PropertyVersion;
    ULONG                            QwordAlignedPropertyBufferLength;
    ULONG                            PropertyBufferLength;
    ULONG                            PropertyBufferOffset;
} NDIS_SWITCH_PROPERTY_ENUM_INFO, *PNDIS_SWITCH_PROPERTY_ENUM_INFO;

#define NDIS_SWITCH_PROPERTY_ENUM_INFO_GET_NEXT(_SwitchEnumInfo_)\
            ((PNDIS_SWITCH_PROPERTY_ENUM_INFO)\
                ((ULONG_PTR)(_SwitchEnumInfo_) +\
                (_SwitchEnumInfo_)->QwordAlignedPropertyBufferLength +\
                 sizeof(NDIS_SWITCH_PROPERTY_ENUM_INFO)))

#define NDIS_SWITCH_PROPERTY_ENUM_INFO_GET_PROPERTY(_SwitchEnumInfo_)\
            ((PVOID)((PUCHAR)(_SwitchEnumInfo_) + (_SwitchEnumInfo_)->PropertyBufferOffset))

#define NDIS_SWITCH_PROPERTY_ENUM_PARAMETERS_REVISION_1  1

typedef struct _NDIS_SWITCH_PROPERTY_ENUM_PARAMETERS
{
    NDIS_OBJECT_HEADER                         Header;
    ULONG                                      Flags;
    NDIS_SWITCH_PROPERTY_TYPE                  PropertyType;
    NDIS_SWITCH_OBJECT_ID                      PropertyId;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION   SerializationVersion;
    ULONG                                      FirstPropertyOffset;
    ULONG                                      NumProperties;
} NDIS_SWITCH_PROPERTY_ENUM_PARAMETERS, *PNDIS_SWITCH_PROPERTY_ENUM_PARAMETERS;

#define NDIS_SWITCH_PROPERTY_ENUM_PARAMETERS_GET_FIRST_INFO(_SwitchEnumParams_)\
            ((PNDIS_SWITCH_PROPERTY_ENUM_INFO)((PUCHAR)(_SwitchEnumParams_) + \
                (_SwitchEnumParams_)->FirstPropertyOffset))

#define NDIS_SWITCH_FEATURE_STATUS_PARAMETERS_REVISION_1         1

typedef enum _NDIS_SWITCH_FEATURE_STATUS_TYPE
{
    NdisSwitchFeatureStatusTypeUndefined,
    NdisSwitchFeatureStatusTypeCustom,
    NdisSwitchFeatureStatusTypeMaximum
} NDIS_SWITCH_FEATURE_STATUS_TYPE, *PNDIS_SWITCH_FEATURE_STATUS_TYPE;

typedef struct _NDIS_SWITCH_FEATURE_STATUS_PARAMETERS
{
    NDIS_OBJECT_HEADER                               Header;
    ULONG                                            Flags;
    NDIS_SWITCH_FEATURE_STATUS_TYPE                  FeatureStatusType;
    NDIS_SWITCH_OBJECT_ID                            FeatureStatusId;
    NDIS_SWITCH_OBJECT_INSTANCE_ID                   FeatureStatusInstanceId;
    NDIS_SWITCH_OBJECT_VERSION                       FeatureStatusVersion;
    NDIS_SWITCH_OBJECT_SERIALIZATION_VERSION         SerializationVersion;
    ULONG                                            FeatureStatusBufferOffset;
    ULONG                                            FeatureStatusBufferLength;
} NDIS_SWITCH_FEATURE_STATUS_PARAMETERS, *PNDIS_SWITCH_FEATURE_STATUS_PARAMETERS;

#define NDIS_SIZEOF_NDIS_SWITCH_FEATURE_STATUS_PARAMETERS_REVISION_1       \
            RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_FEATURE_STATUS_PARAMETERS, FeatureStatusBufferLength)

#define NDIS_SWITCH_FEATURE_STATUS_CUSTOM_REVISION_1       1

typedef struct _NDIS_SWITCH_FEATURE_STATUS_CUSTOM
{
    NDIS_OBJECT_HEADER      Header;
    ULONG                   Flags;
    ULONG                   FeatureStatusCustomBufferLength;
    ULONG                   FeatureStatusCustomBufferOffset;
} NDIS_SWITCH_FEATURE_STATUS_CUSTOM, *PNDIS_SWITCH_FEATURE_STATUS_CUSTOM;

#define NDIS_SIZEOF_NDIS_SWITCH_FEATURE_STATUS_CUSTOM_REVISION_1       \
        RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_FEATURE_STATUS_CUSTOM, FeatureStatusCustomBufferOffset)

#define NDIS_SWITCH_PARAMETERS_REVISION_1      1

typedef struct _NDIS_SWITCH_PARAMETERS
{
  NDIS_OBJECT_HEADER        Header;
  ULONG                     Flags;
  NDIS_SWITCH_NAME          SwitchName;
  NDIS_SWITCH_FRIENDLYNAME  SwitchFriendlyName;
  UINT32                    NumSwitchPorts;
  BOOLEAN                   IsActive;
} NDIS_SWITCH_PARAMETERS, *PNDIS_SWITCH_PARAMETERS;

typedef enum _NDIS_SWITCH_PORT_TYPE
{
    NdisSwitchPortTypeGeneric    = 0,
    NdisSwitchPortTypeExternal   = 1,
    NdisSwitchPortTypeSynthetic  = 2,
    NdisSwitchPortTypeEmulated   = 3,
    NdisSwitchPortTypeInternal   = 4
} NDIS_SWITCH_PORT_TYPE;

typedef enum _NDIS_SWITCH_PORT_STATE
{
    NdisSwitchPortStateUnknown      = 0,
    NdisSwitchPortStateCreated      = 1,
    NdisSwitchPortStateTeardown     = 2,
    NdisSwitchPortStateDeleted      = 3
} NDIS_SWITCH_PORT_STATE;

typedef struct _NDIS_SWITCH_PORT_PARAMETERS
{
  NDIS_OBJECT_HEADER                Header;
  ULONG                             Flags;
  NDIS_SWITCH_PORT_ID               PortId;
  NDIS_SWITCH_PORT_NAME             PortName;
  NDIS_SWITCH_PORT_FRIENDLYNAME     PortFriendlyName;
  NDIS_SWITCH_PORT_TYPE             PortType;
  BOOLEAN                           IsValidationPort;
  NDIS_SWITCH_PORT_STATE            PortState;
} NDIS_SWITCH_PORT_PARAMETERS, *PNDIS_SWITCH_PORT_PARAMETERS;

#define NDIS_SWITCH_PORT_PARAMETERS_REVISION_1      1

#define NDIS_SIZEOF_NDIS_SWITCH_PORT_PARAMETERS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_PORT_PARAMETERS, PortState)

typedef struct _NDIS_SWITCH_PORT_ARRAY
{
  NDIS_OBJECT_HEADER        Header;
  ULONG                     Flags;
  USHORT                    FirstElementOffset;
  ULONG                     NumElements;
  ULONG                     ElementSize;
} NDIS_SWITCH_PORT_ARRAY, *PNDIS_SWITCH_PORT_ARRAY;

#define NDIS_SWITCH_PORT_ARRAY_REVISION_1      1

typedef enum _NDIS_SWITCH_NIC_TYPE
{
    NdisSwitchNicTypeExternal      = 0,
    NdisSwitchNicTypeSynthetic     = 1,
    NdisSwitchNicTypeEmulated      = 2,
    NdisSwitchNicTypeInternal      = 3
} NDIS_SWITCH_NIC_TYPE;

typedef enum _NDIS_SWITCH_NIC_STATE
{
    NdisSwitchNicStateUnknown       = 0,
    NdisSwitchNicStateCreated       = 1,
    NdisSwitchNicStateConnected     = 2,
    NdisSwitchNicStateDisconnected  = 3,
    NdisSwitchNicStateDeleted       = 4
} NDIS_SWITCH_NIC_STATE;

typedef struct _NDIS_SWITCH_NIC_PARAMETERS
{
    NDIS_OBJECT_HEADER              Header;
    ULONG                           Flags;
    NDIS_SWITCH_NIC_NAME            NicName;
    NDIS_SWITCH_NIC_FRIENDLYNAME    NicFriendlyName;
    NDIS_SWITCH_PORT_ID             PortId;
    NDIS_SWITCH_NIC_INDEX           NicIndex;
    NDIS_SWITCH_NIC_TYPE            NicType;
    NDIS_SWITCH_NIC_STATE           NicState;
    NDIS_VM_NAME                    VmName;
    NDIS_VM_FRIENDLYNAME            VmFriendlyName;
    GUID                            NetCfgInstanceId;
    ULONG                           MTU;
    USHORT                          NumaNodeId;
    UCHAR                           PermanentMacAddress[NDIS_MAX_PHYS_ADDRESS_LENGTH];
    UCHAR                           VMMacAddress[NDIS_MAX_PHYS_ADDRESS_LENGTH];
    UCHAR                           CurrentMacAddress[NDIS_MAX_PHYS_ADDRESS_LENGTH];
    BOOLEAN                         VFAssigned;
#if defined(NDIS_SUPPORT_NDIS640)
    ULONG64                         NdisReserved[2];
#endif
} NDIS_SWITCH_NIC_PARAMETERS, *PNDIS_SWITCH_NIC_PARAMETERS;

#define NDIS_SWITCH_NIC_PARAMETERS_REVISION_1      1

#define NDIS_SIZEOF_NDIS_SWITCH_NIC_PARAMETERS_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_NIC_PARAMETERS, VFAssigned)

typedef struct _NDIS_SWITCH_NIC_ARRAY
{
  NDIS_OBJECT_HEADER        Header;
  ULONG                     Flags;
  USHORT                    FirstElementOffset;
  ULONG                     NumElements;
  ULONG                     ElementSize;
} NDIS_SWITCH_NIC_ARRAY, *PNDIS_SWITCH_NIC_ARRAY;

#define NDIS_SWITCH_NIC_AT_ARRAY_INDEX(_NicArray_, _Index_)\
    ((PNDIS_SWITCH_NIC_PARAMETERS)((PUCHAR)(_NicArray_) + \
                        (_NicArray_)->FirstElementOffset + \
                        ((_NicArray_)->ElementSize * (_Index_))))

typedef struct _NDIS_OID_REQUEST NDIS_OID_REQUEST, *PNDIS_OID_REQUEST;

typedef struct _NDIS_SWITCH_NIC_OID_REQUEST
{
    NDIS_OBJECT_HEADER          Header;
    ULONG                       Flags;
    NDIS_SWITCH_PORT_ID         SourcePortId;
    NDIS_SWITCH_NIC_INDEX       SourceNicIndex;
    NDIS_SWITCH_PORT_ID         DestinationPortId;
    NDIS_SWITCH_NIC_INDEX       DestinationNicIndex;
    PNDIS_OID_REQUEST           OidRequest;
} NDIS_SWITCH_NIC_OID_REQUEST, *PNDIS_SWITCH_NIC_OID_REQUEST;

#define NDIS_SWITCH_NIC_OID_REQUEST_REVISION_1     1

#define NDIS_SIZEOF_NDIS_SWITCH_NIC_OID_REQUEST_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_NIC_OID_REQUEST, OidRequest)

typedef struct _NDIS_SWITCH_NIC_SAVE_STATE
{
  NDIS_OBJECT_HEADER                    Header;
  ULONG                                 Flags;
  NDIS_SWITCH_PORT_ID                   PortId;
  NDIS_SWITCH_NIC_INDEX                 NicIndex;
  GUID                                  ExtensionId;
  NDIS_SWITCH_EXTENSION_FRIENDLYNAME    ExtensionFriendlyName;
  GUID                                  FeatureClassId;
  USHORT                                SaveDataSize;
  USHORT                                SaveDataOffset;
#if (NDIS_SUPPORT_NDIS650)
  ULONG                                 SaveDataSizeOverflow;
#endif
} NDIS_SWITCH_NIC_SAVE_STATE, *PNDIS_SWITCH_NIC_SAVE_STATE;

#define NDIS_SWITCH_NIC_SAVE_STATE_REVISION_1      1

#define NDIS_SIZEOF_NDIS_SWITCH_NIC_SAVE_STATE_REVISION_1 \
    RTL_SIZEOF_THROUGH_FIELD(NDIS_SWITCH_NIC_SAVE_STATE,  SaveDataOffset)

#endif
#endif
#endif
#if ((NDIS_SUPPORT_NDIS650))

typedef struct _NDIS_GFT_OFFLOAD_CAPABILITIES
{
    NDIS_OBJECT_HEADER                  Header;
    ULONG                               Flags;
    ULONG                               CounterCapabilities;
    ULONG                               SupportedTableTypes;
    ULONG                               SupportedEncapsulationTypes;
    ULONG                               SupportedIngressExactMatchTableActions;
    ULONG                               SupportedEgressExactMatchTableActions;
    ULONG                               SoftwareSupportedIngressExactMatchTableActions;
    ULONG                               SoftwareSupportedEgressExactMatchTableActions;
    ULONG                               SupportedIngressWildcardMatchTableActions;
    ULONG                               SupportedEgressWildcardMatchTableActions;
    ULONG                               SoftwareSupportedIngressWildcardMatchTableActions;
    ULONG                               SoftwareSupportedEgressWildcardMatchTableActions;
    ULONG                               NumPacketCounterObjects;
    ULONG                               NumByteCounterObjects;
    ULONG                               NumPacketByteCounterObjects;
    ULONG                               NumPacketByteCounterAndStateObjects;
    ULONG                               NumCounterObjectsPerIngressExactMatchFlowEntry;
    ULONG                               NumCounterObjectsPerEgressExactMatchFlowEntry;
    ULONG                               NumCounterObjectsPerIngressWildcardMatchFlowEntry;
    ULONG                               NumCounterObjectsPerEgressWildcardMatchFlowEntry;
} NDIS_GFT_OFFLOAD_CAPABILITIES, *PNDIS_GFT_OFFLOAD_CAPABILITIES;

typedef ULONG NDIS_QOS_SQ_ID, *PNDIS_QOS_SQ_ID;

#endif

#define NDIS_OFFLOAD_PARAMETERS_DEFINED 1
#define NDIS_RECEIVE_SCALE_PARAMETERS_DEFINED 1
#define NDIS_INTERRUPT_MODERATION_PARAMETERS_DEFINED 1
#define NDIS_LINK_SPEED_DEFINED 1

#endif

#define NDIS_ETH_TYPE_IPV4              0x0800

#define NDIS_ETH_TYPE_ARP               0x0806

#define NDIS_ETH_TYPE_IPV6              0x86dd

#define NDIS_ETH_TYPE_SLOW_PROTOCOL     0x8809

#ifdef __cplusplus
}
#endif

#if ((NTDDI_VERSION >= NTDDI_VISTA) || NDIS_SUPPORT_NDIS6)
#ifndef __WINDOT11_H__
#include <windot11.h>
#endif
#endif

#endif /* _NTDDNDIS_ */
