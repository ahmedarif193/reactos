/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Bluetooth profile driver interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef __BTHDDI_H__
#define __BTHDDI_H__

#ifdef __cplusplus
extern "C" {
#endif

#define __BTHDDI_H__

#if ((NTDDI_VERSION >= NTDDI_VISTA))

#define BTHPORT_CONTEXT_SIZE            (4)

#define BTHPORT_RESERVED_FIELD_SIZE     (2)

typedef PVOID L2CAP_CHANNEL_HANDLE;

typedef PVOID L2CAP_SERVER_HANDLE;

typedef PVOID SCO_SERVER_HANDLE;

typedef enum _BRB_TYPE
{
    BRB_HCI_GET_LOCAL_BD_ADDR                       = 0x0001,
    BRB_L2CA_REGISTER_SERVER                        = 0x0100,
    BRB_L2CA_UNREGISTER_SERVER                      = 0x0101,
    BRB_L2CA_OPEN_CHANNEL                           = 0x0102,
    BRB_L2CA_OPEN_CHANNEL_RESPONSE                  = 0x0103,
    BRB_L2CA_CLOSE_CHANNEL                          = 0x0104,
    BRB_L2CA_ACL_TRANSFER                           = 0x0105,
    BRB_L2CA_UPDATE_CHANNEL                         = 0x0106,
    BRB_L2CA_PING                                   = 0x0107,
    BRB_L2CA_INFO_REQUEST                           = 0x0108,
    BRB_REGISTER_PSM                                = 0x0109,
    BRB_UNREGISTER_PSM                              = 0x010a,
    BRB_SCO_REGISTER_SERVER                         = 0x0200,
    BRB_SCO_UNREGISTER_SERVER                       = 0x0201,
    BRB_SCO_OPEN_CHANNEL                            = 0x0202,
    BRB_SCO_OPEN_CHANNEL_RESPONSE                   = 0x0203,
    BRB_SCO_CLOSE_CHANNEL                           = 0x0204,
    BRB_SCO_TRANSFER                                = 0x0205,
    BRB_SCO_GET_CHANNEL_INFO                        = 0x0207,
    BRB_SCO_GET_SYSTEM_INFO                         = 0x0209,
    BRB_SCO_FLUSH_CHANNEL                           = 0x020a,
    BRB_SCO_OPEN_UNMANAGED_CHANNEL                  = 0x0210,
    BRB_SCO_OPEN_UNMANAGED_CHANNEL_RESPONSE         = 0x0211,
#if (NTDDI_VERSION >= NTDDI_WIN8)
    BRB_L2CA_OPEN_ENHANCED_CHANNEL                  = 0x0212,
    BRB_L2CA_OPEN_ENHANCED_CHANNEL_RESPONSE         = 0x0213,
#endif
    BRB_ACL_GET_MODE                                = 0x0300,
    BRB_ACL_ENTER_ACTIVE_MODE                       = 0x0301,
    BRB_STORED_LINK_KEY                             = 0x0310,
    BRB_GET_DEVICE_INTERFACE_STRING                 = 0x0320,
} BRB_TYPE;

typedef enum _BRB_VERSION
{
    BLUETOOTH_V1 = 0,
    BLUETOOTH_V2,
} BRB_VERSION;

typedef struct _BRB_HEADER
{
    LIST_ENTRY ListEntry;
    ULONG Length;
    USHORT Version;
    USHORT Type;
    ULONG BthportFlags;
    NTSTATUS Status;
    BTHSTATUS BtStatus;
    PVOID Context[BTHPORT_CONTEXT_SIZE];
    PVOID ClientContext[BTHPORT_CONTEXT_SIZE];
    ULONG Reserved[BTHPORT_RESERVED_FIELD_SIZE];
} BRB_HEADER;

typedef struct _L2CAP_CONFIG_RANGE
{
    USHORT Min;
    USHORT Max;
} L2CAP_CONFIG_RANGE, *PL2CAP_CONFIG_RANGE;

typedef struct _L2CAP_CONFIG_VALUE_RANGE
{
    USHORT Min;
    USHORT Preferred;
    USHORT Max;
} L2CAP_CONFIG_VALUE_RANGE, *PL2CAP_CONFIG_VALUE_RANGE;

#include <pshpack1.h>
typedef struct _L2CAP_FLOWSPEC
{
    UCHAR Flags;
    UCHAR ServiceType;
    ULONG TokenRate;
    ULONG TokenBucketSize;
    ULONG PeakBandwidth;
    ULONG Latency;
    ULONG DelayVariation;
} L2CAP_FLOWSPEC, *PL2CAP_FLOWSPEC;
#include <poppack.h>

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#include <pshpack1.h>
typedef struct _L2CAP_RETRANSMISSION_AND_FLOW_CONTROL
{
    UCHAR Mode;
    UCHAR TxWindowSize;
    UCHAR MaxTransmit;
    USHORT RetransmissionTO;
    USHORT MonitorTO;
    USHORT MaxPDUSize;
} L2CAP_RETRANSMISSION_AND_FLOW_CONTROL, *PL2CAP_RETRANSMISSION_AND_FLOW_CONTROL;
#include <poppack.h>

#include <pshpack1.h>
typedef struct _L2CAP_EXTENDED_FLOW_SPEC
{
    UCHAR Identifier;
    UCHAR ServiceType;
    USHORT MaxSDUSize;
    ULONG SDUInterArrivalTime;
    ULONG AccessLatency;
    ULONG FlushTimeout;
} L2CAP_EXTENDED_FLOW_SPEC, *PL2CAP_EXTENDED_FLOW_SPEC;
#include <poppack.h>

#endif

#define CO_DYNAMIC              (0x0001)

#define CO_FIXED                (0x0002)

#define CO_UNKNOWN              (0x0004)

#define VALID_CO_FLAGS       (CO_DYNAMIC | CO_FIXED)

#define IS_CO_TYPE_HINT(type)       (((type) & 0x80) == 0x80)

#define IS_CO_TYPE_REQUIRED(type)   (((type) & 0x80) == 0x00)

typedef UCHAR  CO_TYPE, *PCO_TYPE;

typedef UCHAR  CO_LENGTH, *PCO_LENGTH;

typedef USHORT CO_MTU, *PCO_MTU;

typedef USHORT CO_FLUSHTO, *PCO_FLUSHTO;

#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef UCHAR CO_FCS, *PCO_FCS;

typedef USHORT CO_EXTENDED_WINDOW_SIZE, *PCO_EXTENDED_WINDOW_SIZE;

#endif

#include <pshpack1.h>
typedef struct _CO_HEADER
{
    CO_TYPE Type;
    CO_LENGTH Length;
} CO_HEADER;
#include <poppack.h>

typedef USHORT CONNECTION_HANDLE, *PCONNECTION_HANDLE;

typedef PVOID SCO_CHANNEL_HANDLE, *PSCO_CHANNEL_HANDLE;

typedef enum _SCO_RETRANSMISSION_EFFORT
{
    SCO_RETRANSMISSION_NONE          =   0x00,
    SCO_RETRANSMISSION_MIN1_POWER    =   0x01,
    SCO_RETRANSMISSION_MIN1_QUALITY  =   0x02,
    SCO_RETRANSMISSION_DONT_CARE     =   0xFF
} SCO_RETRANSMISSION_EFFORT, *PSCO_RETRANSMISSION_EFFORT;

#define SCO_VS_IN_CODING_MASK           (0x0300)

#define SCO_VS_IN_CODING_LINEAR         (0x0000)

#define SCO_VS_IN_CODING_MULAW          (0x0100)

#define SCO_VS_IN_CODING_ALAW           (0x0200)

#define SCO_VS_IN_DATA_FORMAT_MASK      (0x00C0)

#define SCO_VS_IN_DATA_FORMAT_1C        (0x0000)

#define SCO_VS_IN_DATA_FORMAT_2C        (0x0040)

#define SCO_VS_IN_DATA_FORMAT_SM        (0x0080)

#define SCO_VS_IN_DATA_FORMAT_US        (0x00C0)

#define SCO_VS_IN_SAMPLE_SIZE_MASK      (0x0020)

#define SCO_VS_IN_SAMPLE_SIZE_8BIT      (0x0000)

#define SCO_VS_IN_SAMPLE_SIZE_16BIT     (0x0020)

#define SCO_VS_PCM_BIT_POS_MASK         (0x001C)

#define SCO_VS_AIR_CODING_FORMAT_MASK   (0x0003)

#define SCO_VS_AIR_CODING_FORMAT_CVSD   (0x0000)

#define SCO_VS_AIR_CODING_FORMAT_MULAW  (0x0001)

#define SCO_VS_AIR_CODING_FORMAT_ALAW   (0x0002)

#define SCO_VS_AIR_CODING_DATA          (0x0003)

#define SCO_VS_SETTING_DEFAULT          (0x0060)

typedef enum _SCO_LINK_TYPE
{
    ScoLinkType  = 0x00,
    eScoLinkType = 0x02,
} SCO_LINK_TYPE, *PSCO_LINK_TYPE;

typedef enum _CODING_FORMAT
{
    ScoCodingFormatULaw             = 0x00,
    ScoCodingFormatALaw             = 0x01,
    ScoCodingFormatCVSD             = 0x02,
    ScoCodingFormatTransparent      = 0x03,
    ScoCodingFormatLinearPCM        = 0x04,
    ScoCodingFormatMSBC             = 0x05,
    ScoCodingFormatVendorSpecific   = 0xFF,
} CODING_FORMAT, *PCODING_FORMAT;

#define IS_CODING_FORMAT_RESERVED(fmt) ((fmt < ScoCodingFormatVendorSpecific)  && fmt > ScoCodingFormatMSBC)

#define SCO_DATAPATH_HCI (0x0)

#define SCO_DATAPATH_PCM (0x1)

typedef enum _PCM_DATA_FORMAT
{
    ScoPCMCFormatNA             = 0x00,
    ScoPCMFormat1sComplement    = 0x01,
    ScoPCMFormat2sComplement    = 0x02,
    ScoPCMFormatSignMagnitude   = 0x03,
    ScoPCMFormatUnsigned        = 0x04
} PCM_DATA_FORMAT, *PPCM_DATA_FORMAT;

#define IS_PCM_FORMAT_RESERVED(fmt) ((fmt > ScoPCMFormatUnsigned))

#define SCO_HV1                         (0x0001)

#define SCO_HV2                         (0x0002)

#define SCO_HV3                         (0x0004)

#define SCO_EV3                         (0x0008)

#define SCO_EV4                         (0x0010)

#define SCO_EV5                         (0x0020)

#define SCO_PKT_ALL                     (0x003F)

#define SCO_NO2EV3                      (0x0040)

#define SCO_NO3EV3                      (0x0080)

#define SCO_NO2EV5                      (0x0100)

#define SCO_NO3EV5                      (0x0200)

#define PKT_ALL                 (SCO_HV1 | SCO_HV2 | SCO_HV3 | SCO_EV3 | SCO_EV4 | SCO_EV5)

#define PKT_EDR_ESCO_NONE       (SCO_NO2EV3 | SCO_NO3EV3 | SCO_NO2EV5 | SCO_NO3EV5)

#define PKT_HV1                 (SCO_HV1 | PKT_EDR_ESCO_NONE)

#define PKT_HV2                 (SCO_HV2 | PKT_EDR_ESCO_NONE)

#define PKT_HV3                 (SCO_HV3 | PKT_EDR_ESCO_NONE)

#define PKT_BR_SCO_ALL          (PKT_HV1 | PKT_HV2 | PKT_HV3)

#define PKT_EV3                 (SCO_EV3 | PKT_EDR_ESCO_NONE)

#define PKT_EV4                 (SCO_EV4 | PKT_EDR_ESCO_NONE)

#define PKT_EV5                 (SCO_EV5 | PKT_EDR_ESCO_NONE)

#define PKT_BR_ESCO_ALL         (PKT_EV3 | PKT_EV4 | PKT_EV5)

#define PKT_2EV3                (PKT_EDR_ESCO_NONE ^ SCO_NO2EV3)

#define PKT_3EV3                (PKT_EDR_ESCO_NONE ^ SCO_NO3EV3)

#define PKT_2EV5                (PKT_EDR_ESCO_NONE ^ SCO_NO2EV5)

#define PKT_3EV5                (PKT_EDR_ESCO_NONE ^ SCO_NO3EV5)

#define PKT_EDR_ESCO_ALL        (~PKT_EDR_ESCO_NONE)

#define PKT_ESCO_ALL            (SCO_EV3 | SCO_EV4 | SCO_EV5)

#define SCO_CF_LINK_AUTHENTICATED       (0x00020000)

#define SCO_CF_LINK_ENCRYPTED           (0x00040000)

#define SCO_CF_LINK_SUPPRESS_PIN        (0x00080000)

#define SCO_CALLBACK_DISCONNECT         (0x00000001)

#define SCO_VALID_CALLBACK_FLAGS        (SCO_CALLBACK_DISCONNECT)

typedef enum _SCO_INDICATION_CODE
{
    ScoIndicationAddReference = 0,
    ScoIndicationReleaseReference,
    ScoIndicationRemoteConnect,
    ScoIndicationRemoteDisconnect,
} SCO_INDICATION_CODE, *PSCO_INDICATION_CODE;

typedef enum _SCO_DISCONNECT_REASON
{
    ScoHciDisconnect = 0,
    ScoDisconnectRequest,
    ScoRadioPoweredDown,
    ScoHardwareRemoval,
} SCO_DISCONNECT_REASON, *PSCO_DISCONNECT_REASON;

typedef struct _SCO_INDICATION_PARAMETERS
{
    SCO_CHANNEL_HANDLE ConnectionHandle;
    BTH_ADDR BtAddress;
    union
    {
        struct
        {
            struct
            {
                SCO_LINK_TYPE LinkType;
            } Request;
        } Connect;
        struct
        {
            SCO_DISCONNECT_REASON Reason;
            BOOLEAN CloseNow;
        } Disconnect;
    } Parameters;
} SCO_INDICATION_PARAMETERS, *PSCO_INDICATION_PARAMETERS;

typedef void (*PFNSCO_INDICATION_CALLBACK)(
    _In_ PVOID Context,
    _In_ SCO_INDICATION_CODE Indication,
    _In_ PSCO_INDICATION_PARAMETERS Parameters);

#define SCO_INDICATION_SCO_REQUEST      (0x00000001)

#define SCO_INDICATION_ESCO_REQUEST     (0x00000002)

struct _BRB_SCO_REGISTER_SERVER
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    ULONG Reserved;
    ULONG IndicationFlags;
    PFNSCO_INDICATION_CALLBACK  IndicationCallback;
    PVOID IndicationCallbackContext;
    PVOID ReferenceObject;
    SCO_SERVER_HANDLE ServerHandle;
};

struct _BRB_SCO_UNREGISTER_SERVER
{
    BRB_HEADER  Hdr;
    BTH_ADDR    BtAddress;
    PVOID       ServerHandle;
};

#define SCO_CONNECT_RSP_RESPONSE_SUCCESS            (0x00)

#define SCO_CONNECT_RSP_RESPONSE_NO_RESOURCES       (0x0D)

#define SCO_CONNECT_RSP_RESPONSE_SECURITY_BLOCK     (0x0E)

#define SCO_CONNECT_RSP_RESPONSE_BAD_BD_ADDR        (0x0F)

struct _BRB_SCO_OPEN_CHANNEL
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    ULONG TransmitBandwidth;
    ULONG ReceiveBandwidth;
    USHORT MaxLatency;
    USHORT PacketType;
    USHORT ContentFormat;
    USHORT Reserved;
    SCO_RETRANSMISSION_EFFORT RetransmissionEffort;
    ULONG ChannelFlags;
    ULONG CallbackFlags;
    PFNSCO_INDICATION_CALLBACK Callback;
    PVOID CallbackContext;
    PVOID ReferenceObject;
    SCO_CHANNEL_HANDLE ChannelHandle;
    UCHAR Response;
};

struct _BRB_SCO_CLOSE_CHANNEL
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    SCO_CHANNEL_HANDLE ChannelHandle;
};

#define SCO_FLUSH_DIRECTION_OUT         (0x00000001)

#define SCO_FLUSH_DIRECTION_IN          (0x00000002)

struct _BRB_SCO_FLUSH_CHANNEL
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    SCO_CHANNEL_HANDLE ChannelHandle;
    ULONG FlushFlags;
};

typedef struct _BASEBAND_CHANNEL_INFO
{
    UCHAR Transmission_Interval;
    UCHAR Retransmission_Window;
    UCHAR AirMode;
    USHORT Rx_Packet_Length;
    USHORT Tx_Packet_Length;
} BASEBAND_CHANNEL_INFO, *PBASEBAND_CHANNEL_INFO;

struct _BRB_SCO_GET_CHANNEL_INFO
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    SCO_CHANNEL_HANDLE ChannelHandle;
    ULONG InfoFlags;
    ULONG TransmitBandwidth;
    ULONG ReceiveBandwidth;
    USHORT MaxLatency;
    USHORT PacketType;
    USHORT ContentFormat;
    USHORT Reserved;
    SCO_RETRANSMISSION_EFFORT RetransmissionEffort;
    ULONG ChannelFlags;
    CONNECTION_HANDLE HciConnectionHandle;
    SCO_LINK_TYPE LinkType;
    BASEBAND_CHANNEL_INFO BasebandInfo;
};

#define SCO_INFO_BASEBAND_AVAILABLE (0x00000001)

struct _BRB_SCO_TRANSFER
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    SCO_CHANNEL_HANDLE ChannelHandle;
    ULONG TransferFlags;
    ULONG BufferSize;
    PVOID Buffer;
    PMDL BufferMDL;
    ULONGLONG DataTag;
};

#define SCO_TRANSFER_DIRECTION_OUT      (0x00000000)

#define SCO_TRANSFER_DIRECTION_IN       (0x00000001)

#define SCO_TRANSFER_VALID_FLAGS (SCO_TRANSFER_DIRECTION_OUT | SCO_TRANSFER_DIRECTION_IN)

struct _BRB_SCO_GET_SYSTEM_INFO
{
    BRB_HEADER Hdr;
    ULONG Features;
    ULONG MaxChannels;
    ULONG TransferUnit;
    USHORT PacketTypes;
    USHORT DataFormats;
    ULONG Reserved;
};

#define SCO_FEATURE_SCO_LINKS               (0x00000001)

#define SCO_FEATURE_ESCO_LINKS              (0x00000002)

#define SCO_FEATURE_STREAM_OFFSET_DATA_TAG  (0x00000010)

#define SCO_DATA_FORMAT_MU_LAW_LOG      (0x0001)

#define SCO_DATA_FORMAT_A_LAW_LOG       (0x0002)

#define SCO_DATA_FORMAT_CVSD            (0x0004)

#define SCO_DATA_FORMAT_TRANSPARENT     (0x0008)

#define SCO_DATA_FORMAT_ALL             (0x000F)

#define SCO_DATA_FORMAT_VENDOR_SPECIFIC (0x00FF)

#define ALL_RESERVED_DATA_FORMAT_BITS_ARE_ZERO(format) ((format >) ? FALSE : TRUE)

#define L2CAP_FLOW_SERVICE_TYPE_NOTRAFFIC       (0)

#define L2CAP_FLOW_SERVICE_TYPE_BESTEFFORT      (1)

#define L2CAP_FLOW_SERVICE_TYPE_GUARANTEED       (2)

#define CONNECT_RSP_RESULT_SUCCESS           (0x0)

#define CONNECT_RSP_RESULT_PENDING           (0x1)

#define CONNECT_RSP_RESULT_PSM_NEG           (0x2)

#define CONNECT_RSP_RESULT_SECURITY_BLOCK    (0x3)

#define CONNECT_RSP_RESULT_NO_RESOURCES      (0x4)

#define CONNECT_RSP_STATUS_NO_INFORMATION           (0x00)

#define CONNECT_RSP_STATUS_AUTHENTICATION_PENDING   (0x01)

#define CONNECT_RSP_STATUS_AUTHORIZATION_PENDING    (0x02)

#define CONFIG_STATUS_SUCCESS                   (0)

#define CONFIG_STATUS_INVALID_PARAMETER         (1)

#define CONFIG_STATUS_REJECT                    (2)

#define CONFIG_STATUS_UNKNOWN_OPTION            (3)

#define CONFIG_STATUS_DISCONNECT                (0xFFF)

#define CONNECTION_PARAMETERS_ACCEPTED          (0x0000)

#define CONNECTION_PARAMETERS_REJECTED          (0x0001)

#define L2CAP_MIN_FLUSHTO                   (1)

#define L2CAP_MAX_FLUSHTO                   (0xFFFF)

#define L2CAP_DEFAULT_FLUSHTO               (L2CAP_MAX_FLUSHTO)

#define L2CAP_NO_REXMIT_FLUSHTO             (L2CAP_MIN_FLUSHTO)

#define L2CAP_INFINITE_FLUSHTO              (L2CAP_MAX_FLUSHTO)

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define L2CAP_RAF_BASIC_MODE                        (0x00)

#define L2CAP_RAF_ENHANCED_RETRANSMISSION_MODE      (0x03)

#define L2CAP_RAF_STREAMING_MODE                    (0x04)

#define L2CAP_RAF_DEFAULT_MAX_PDU_SIZE              (0x1F40)

#define L2CAP_RAF_DEFAULT_TX_WINDOW_SIZE            (0x3F)

#define L2CAP_RAF_DEFAULT_MAXTRANSMIT               (0x10)

#define L2CAP_RAF_VALID_CONFIG_REQUEST_RETRANSMISSION_TO  \
                                                    (0x00)

#define L2CAP_RAF_VALID_CONFIG_REQUEST_MONITOR_TO   (0x00)

#define L2CAP_NO_FCS                        (0x00)

#define L2CAP_16_BIT_FCS                    (0x01)

#define L2CAP_DEFAULT_FCS                   (L2CAP_16_BIT_FCS)

#endif

#define CFG_MTU                     (0x00000001)

#define CFG_FLUSHTO                 (0x00000002)

#define CFG_QOS                     (0x00000004)

#define CFG_EXTRA                   (0x00000008)

#define CFG_LINKTO                  (0x00000010)

#define CFG_QOS_LOCAL               (0x00000020)

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define CFG_ENHANCED                (0x00000040)

#define CFG_FCS                     (0x00000080)

#define CM_BASIC                    (0x00000001)

#define CM_RETRANSMISSION_AND_FLOW  (0x00000002)

#define CM_STREAMING                (0x00000004)

#endif

#define CF_ROLE_EITHER          (0x00000000)

#define CF_ROLE_SLAVE           (0x00000001)

#define CF_ROLE_MASTER          (0x00000002)

#define CF_ROLE_MASK            (CF_ROLE_EITHER | CF_ROLE_SLAVE  | CF_ROLE_MASTER)

#define CF_LINK_NOTHING         (0x00010000)

#define CF_LINK_AUTHENTICATED   (0x00020000)

#define CF_LINK_ENCRYPTED       (0x00040000)

#define CF_LINK_SUPPRESS_PIN    (0x00080000)

#define CF_QUEUE_KEEP_OLD       (0x00000020)

#define CF_QUEUE_KEEP_NEW       (0x00000040)

#define CF_QUEUE_MASK (CF_QUEUE_KEEP_OLD | CF_QUEUE_KEEP_NEW)

#define CALLBACK_DISCONNECT             (0x00000001)

#define CALLBACK_CONFIG_QOS             (0x00000002)

#define CALLBACK_CONFIG_EXTRA_OUT       (0x00000004)

#define CALLBACK_CONFIG_EXTRA_IN        (0x00000008)

#define CALLBACK_RECONFIG               (0x00000010)

#define CALLBACK_ROLE_CHANGE            (0x00000020)

#define CALLBACK_RECV_PACKET            (0x00000040)

typedef struct _INDICATION_PARAMETERS *PINDICATION_PARAMETERS;

typedef struct _INDICATION_PARAMETERS_ENHANCED *PINDICATION_PARAMETERS_ENHANCED;

typedef enum _INDICATION_CODE
{
    IndicationAddReference = 0,
    IndicationReleaseReference,
    IndicationRemoteConnect,
    IndicationRemoteDisconnect,
    IndicationRemoteConfigRequest,
    IndicationRemoteConfigResponse,
    IndicationFreeExtraOptions,
    IndicationRecvPacket,
    IndicationPairDevice,
    IndicationUnpairDevice,
    IndicationUnpersonalizeDevice,
#if (NTDDI_VERSION >= NTDDI_WIN8)
    IndicationRemoteConnectLE,
#endif
} INDICATION_CODE, *PINDICATION_CODE;

typedef void (*PFNBTHPORT_INDICATION_CALLBACK)(
    _In_opt_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS Parameters);

#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef void (*PFNBTHPORT_INDICATION_CALLBACK_ENHANCED)(
    _In_opt_ PVOID Context,
    _In_ INDICATION_CODE Indication,
    _In_ PINDICATION_PARAMETERS_ENHANCED Parameters);

#endif

typedef struct _L2CAP_CONFIG_OPTION
{
    CO_HEADER Header;
    VOID UNALIGNED *DynamicBuffer;
    UCHAR FixedBuffer[4];
    USHORT Flags;
} L2CAP_CONFIG_OPTION, *PL2CAP_CONFIG_OPTION;

typedef struct _CHANNEL_CONFIG_PARAMETERS
{
    ULONG Flags;
    CO_MTU Mtu;
    CO_FLUSHTO FlushTO;
    ULONG NumExtraOptions;
    PL2CAP_CONFIG_OPTION ExtraOptions;
    L2CAP_FLOWSPEC Flow;
} CHANNEL_CONFIG_PARAMETERS, *PCHANNEL_CONFIG_PARAMETERS;

#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef struct _CHANNEL_CONFIG_PARAMETERS_ENHANCED
{
    ULONG Flags;
    CO_MTU Mtu;
    CO_FLUSHTO FlushTO;
    ULONG NumExtraOptions;
    PL2CAP_CONFIG_OPTION ExtraOptions;
    L2CAP_FLOWSPEC Flow;
    L2CAP_RETRANSMISSION_AND_FLOW_CONTROL RetransmissionAndFlow;
    CO_FCS  Fcs;
    L2CAP_EXTENDED_FLOW_SPEC ExtendedFlowSpec;
    CO_EXTENDED_WINDOW_SIZE ExtendedWindowSize;
} CHANNEL_CONFIG_PARAMETERS_ENHANCED, *PCHANNEL_CONFIG_PARAMETERS_ENHANCED;

typedef struct _CHANNEL_CONFIG_RESULTS_ENHANCED
{
    CHANNEL_CONFIG_PARAMETERS_ENHANCED Params;
    ULONG ExtraOptionsBufferSize;
} CHANNEL_CONFIG_RESULTS_ENHANCED, *PCHANNEL_CONFIG_RESULTS_ENHANCED;

#endif

typedef struct _CHANNEL_CONFIG_RESULTS
{
    CHANNEL_CONFIG_PARAMETERS Params;
    ULONG ExtraOptionsBufferSize;
} CHANNEL_CONFIG_RESULTS, *PCHANNEL_CONFIG_RESULTS;

typedef enum _L2CAP_DISCONNECT_REASON
{
    HciDisconnect = 0,
    L2capDisconnectRequest,
    RadioPoweredDown,
    HardwareRemoval,
} L2CAP_DISCONNECT_REASON;

typedef struct _INDICATION_PARAMETERS
{
    L2CAP_CHANNEL_HANDLE ConnectionHandle;
    IN BTH_ADDR BtAddress;
    union
    {
        struct
        {
            struct
            {
                OUT USHORT PSM;
            } Request;
        } Connect;
        struct
        {
            CHANNEL_CONFIG_PARAMETERS CurrentParams;
            CHANNEL_CONFIG_PARAMETERS RequestedParams;
            CHANNEL_CONFIG_PARAMETERS ResponseParams;
            USHORT Response;
        } ConfigRequest;
        struct
        {
            CHANNEL_CONFIG_PARAMETERS CurrentParams;
            CHANNEL_CONFIG_PARAMETERS RequestedParams;
            CHANNEL_CONFIG_PARAMETERS RejectedParams;
            PCO_TYPE UnknownTypes;
            ULONG NumUnknownTypes;
            CHANNEL_CONFIG_PARAMETERS NewRequestParams;
            USHORT Response;
        } ConfigResponse;
        struct
        {
            ULONG NumExtraOptions;
            PL2CAP_CONFIG_OPTION ExtraOptions;
        } FreeExtraOptions;
        struct
        {
            L2CAP_DISCONNECT_REASON Reason;
            BOOLEAN CloseNow;
        } Disconnect;
        struct
        {
            ULONG PacketLength;
            ULONG TotalQueueLength;
        } RecvPacket;
    } Parameters;
} INDICATION_PARAMETERS, *PINDICATION_PARAMETERS;

#if ((NTDDI_VERSION >= NTDDI_WIN8))

typedef struct _INDICATION_PARAMETERS_ENHANCED
{
    L2CAP_CHANNEL_HANDLE ConnectionHandle;
    IN BTH_ADDR BtAddress;
    union
    {
        struct
        {
            struct
            {
                OUT USHORT PSM;
            } Request;
        } Connect;
        struct
        {
            CHANNEL_CONFIG_PARAMETERS_ENHANCED CurrentParams;
            CHANNEL_CONFIG_PARAMETERS_ENHANCED RequestedParams;
            CHANNEL_CONFIG_PARAMETERS_ENHANCED ResponseParams;
            USHORT Response;
        } ConfigRequest;
        struct
        {
            CHANNEL_CONFIG_PARAMETERS_ENHANCED CurrentParams;
            CHANNEL_CONFIG_PARAMETERS_ENHANCED RequestedParams;
            CHANNEL_CONFIG_PARAMETERS_ENHANCED RejectedParams;
            PCO_TYPE UnknownTypes;
            ULONG NumUnknownTypes;
            CHANNEL_CONFIG_PARAMETERS_ENHANCED NewRequestParams;
            USHORT Response;
        } ConfigResponse;
        struct
        {
            ULONG NumExtraOptions;
            PL2CAP_CONFIG_OPTION ExtraOptions;
        } FreeExtraOptions;
        struct
        {
            L2CAP_DISCONNECT_REASON Reason;
            BOOLEAN CloseNow;
        } Disconnect;
        struct
        {
            ULONG PacketLength;
            ULONG TotalQueueLength;
        } RecvPacket;
        PVOID Reserved;
    } Parameters;
} INDICATION_PARAMETERS_ENHANCED, *PINDICATION_PARAMETERS_ENHANCED;

#endif

#define INDICATION_PAIR_DEVICE          (0x00000001)

#define INDICATION_UNPAIR_DEVICE        (0x00000002)

#define INDICATION_UNPERSONALIZE_DEVICE (0x00000004)

#if ((NTDDI_VERSION >= NTDDI_WIN8))

#define INDICATION_LE_DEVICE            (0x00010000)

#endif

struct _BRB_L2CA_REGISTER_SERVER
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    USHORT PSM;
    ULONG IndicationFlags;
    PFNBTHPORT_INDICATION_CALLBACK IndicationCallback;
    PVOID IndicationCallbackContext;
    PVOID ReferenceObject;
    OUT L2CAP_SERVER_HANDLE ServerHandle;
};

struct _BRB_L2CA_UNREGISTER_SERVER
{
    BRB_HEADER  Hdr;
    BTH_ADDR    BtAddress;
    PVOID       ServerHandle;
    USHORT      Psm;
};

#if ((NTDDI_VERSION >= NTDDI_WIN8))

struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL
{
    BRB_HEADER Hdr;
    L2CAP_CHANNEL_HANDLE ChannelHandle;
    union
    {
        struct
        {
            USHORT Response;
            USHORT ResponseStatus;
        };
        USHORT Psm;
    };
    ULONG ChannelFlags;
    BTH_ADDR BtAddress;
    struct
    {
        ULONG Flags;
        L2CAP_CONFIG_VALUE_RANGE Mtu;
        L2CAP_CONFIG_VALUE_RANGE FlushTO;
        L2CAP_FLOWSPEC Flow;
        USHORT LinkTO;
        ULONG NumExtraOptions;
        PL2CAP_CONFIG_OPTION ExtraOptions;
        struct
        {
            UCHAR ServiceType;
            ULONG Latency;
        } LocalQos;
        struct
        {
            ULONG Flags;
            L2CAP_RETRANSMISSION_AND_FLOW_CONTROL RetransmissionAndFlow;
        } ModeConfig;
        USHORT Fcs;
        L2CAP_EXTENDED_FLOW_SPEC ExtendedFlowSpec;
        USHORT ExtendedWindowSize;
    } ConfigOut;
    struct
    {
        ULONG Flags;
        L2CAP_CONFIG_VALUE_RANGE Mtu;
        L2CAP_CONFIG_RANGE FlushTO;
    } ConfigIn;
    ULONG CallbackFlags;
    PFNBTHPORT_INDICATION_CALLBACK_ENHANCED Callback;
    PVOID CallbackContext;
    PVOID ReferenceObject;
    CHANNEL_CONFIG_RESULTS_ENHANCED OutResults;
    CHANNEL_CONFIG_RESULTS_ENHANCED InResults;
    UCHAR IncomingQueueDepth;
    PVOID Reserved;
};

#endif

struct _BRB_L2CA_OPEN_CHANNEL
{
    BRB_HEADER Hdr;
    L2CAP_CHANNEL_HANDLE ChannelHandle;
    union
    {
        struct
        {
            USHORT Response;
            USHORT ResponseStatus;
        };
        USHORT Psm;
    };
    ULONG ChannelFlags;
    BTH_ADDR BtAddress;
    struct
    {
        ULONG Flags;
        L2CAP_CONFIG_VALUE_RANGE Mtu;
        L2CAP_CONFIG_VALUE_RANGE FlushTO;
        L2CAP_FLOWSPEC Flow;
        USHORT LinkTO;
        ULONG NumExtraOptions;
        PL2CAP_CONFIG_OPTION ExtraOptions;
        struct
        {
            UCHAR ServiceType;
            ULONG Latency;
        } LocalQos;
    } ConfigOut;
    struct
    {
        ULONG Flags;
        L2CAP_CONFIG_VALUE_RANGE Mtu;
        L2CAP_CONFIG_RANGE FlushTO;
    } ConfigIn;
    ULONG CallbackFlags;
    PFNBTHPORT_INDICATION_CALLBACK Callback;
    PVOID CallbackContext;
    PVOID ReferenceObject;
    CHANNEL_CONFIG_RESULTS OutResults;
    CHANNEL_CONFIG_RESULTS InResults;
    UCHAR IncomingQueueDepth;
};

struct _BRB_L2CA_CLOSE_CHANNEL
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    L2CAP_CHANNEL_HANDLE ChannelHandle;
};

#define ACL_TRANSFER_DIRECTION_OUT      (0x00000000)

#define ACL_TRANSFER_DIRECTION_IN       (0x00000001)

#define ACL_SHORT_TRANSFER_OK           (0x00000002)

#define ACL_TRANSFER_TIMEOUT            (0x00000004)

struct _BRB_L2CA_ACL_TRANSFER
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    L2CAP_CHANNEL_HANDLE ChannelHandle;
    ULONG TransferFlags;
    ULONG BufferSize;
    PVOID Buffer;
    PMDL BufferMDL;
    LONGLONG Timeout;
    ULONG RemainingBufferSize;
};

struct _BRB_GET_LOCAL_BD_ADDR
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
};

struct _BRB_GET_DEVICE_INTERFACE_STRING
{
    BRB_HEADER Hdr;
    PWCHAR DeviceInterfaceString;
    ULONG DeviceInterfaceStringCbLength;
};

struct _BRB_L2CA_PING
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    UCHAR PingRequestLength;
    UCHAR PingRequestData[MAX_L2CAP_PING_DATA_LENGTH];
    UCHAR PingResponseLength;
    UCHAR PingResponseData[MAX_L2CAP_PING_DATA_LENGTH];
};

struct _BRB_L2CA_UPDATE_CHANNEL
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    L2CAP_CHANNEL_HANDLE ChannelHandle;
    ULONG NewChannelFlags;
    ULONG FailedChannelFlags;
};

struct _BRB_PSM
{
    BRB_HEADER Hdr;
    USHORT Psm;
};

typedef enum _ACL_MODE
{
    ACL_MODE_ACTIVE         = 0x0,
    ACL_MODE_HOLD           = 0x1,
    ACL_MODE_SNIFF          = 0x2,
    ACL_MODE_PARK           = 0x3,
    ACL_MODE_ENTER_ACTIVE   = 0x4,
    ACL_MODE_ENTER_HOLD     = 0x5,
    ACL_MODE_ENTER_SNIFF    = 0x6,
    ACL_MODE_ENTER_PARK     = 0x7,
    ACL_DISCONNECTED        = 0x8,
} ACL_MODE;

struct _BRB_ACL_GET_MODE
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
    ACL_MODE AclMode;
};

struct _BRB_ACL_ENTER_ACTIVE_MODE
{
    BRB_HEADER Hdr;
    BTH_ADDR BtAddress;
};

typedef struct _BRB
{
    union
    {
        struct _BRB_HEADER BrbHeader;
        struct _BRB_GET_DEVICE_INTERFACE_STRING BrbGetDeviceInterfaceString;
        struct _BRB_GET_LOCAL_BD_ADDR BrbGetLocalBdAddress;
        struct _BRB_ACL_GET_MODE BrbAclGetMode;
        struct _BRB_ACL_ENTER_ACTIVE_MODE BrbAclEnterActiveMode;
        struct _BRB_PSM BrbPsm;
        struct _BRB_L2CA_REGISTER_SERVER BrbL2caRegisterServer;
        struct _BRB_L2CA_UNREGISTER_SERVER BrbL2caUnregisterServer;
        struct _BRB_L2CA_OPEN_CHANNEL BrbL2caOpenChannel;
        struct _BRB_L2CA_CLOSE_CHANNEL BrbL2caCloseChannel;
        struct _BRB_L2CA_PING BrbL2caPing;
        struct _BRB_L2CA_ACL_TRANSFER BrbL2caAclTransfer;
        struct _BRB_L2CA_UPDATE_CHANNEL BrbL2caUpdateChannel;
#if (NTDDI_VERSION >= NTDDI_WIN8)
        struct _BRB_L2CA_OPEN_ENHANCED_CHANNEL BrbL2caOpenEnhancedChannel;
#endif
        struct _BRB_SCO_REGISTER_SERVER BrbScoRegisterServer;
        struct _BRB_SCO_UNREGISTER_SERVER BrbScoUnregisterServer;
        struct _BRB_SCO_OPEN_CHANNEL BrbScoOpenChannel;
        struct _BRB_SCO_CLOSE_CHANNEL BrbScoCloseChannel;
        struct _BRB_SCO_FLUSH_CHANNEL BrbScoFlushChannel;
        struct _BRB_SCO_TRANSFER BrbScoTransfer;
        struct _BRB_SCO_GET_CHANNEL_INFO BrbScoGetChannelInfo;
        struct _BRB_SCO_GET_SYSTEM_INFO BrbScoGetSystemInfo;
    };
} BRB, *PBRB;

_IRQL_requires_same_
_Must_inspect_result_
_When_(return!=0, __drv_allocatesMem(Mem))
typedef PBRB (*PFNBTH_ALLOCATE_BRB)(_In_ BRB_TYPE brbType, _In_ ULONG tag);

_IRQL_requires_same_
typedef VOID (*PFNBTH_FREE_BRB)(_In_ __drv_freesMem(Mem) PBRB pBrb);

_IRQL_requires_same_
typedef VOID (*PFNBTH_INITIALIZE_BRB)(_Inout_updates_(_Inexpressible_("varies")) PBRB pBrb, _In_ BRB_TYPE brbType);

_IRQL_requires_same_
typedef VOID (*PFNBTH_REUSE_BRB)(_Inout_ PBRB pBrb, _In_ BRB_TYPE brbType);

_IRQL_requires_same_
_Must_inspect_result_
typedef BOOLEAN(*PFNBTH_IS_BLUETOOTH_VERSION_AVAILABLE)(_In_ UCHAR MajorVersion, _In_ UCHAR MinorVersion);

typedef struct _BTH_PROFILE_DRIVER_INTERFACE
{
    INTERFACE Interface;
    PFNBTH_ALLOCATE_BRB BthAllocateBrb;
    PFNBTH_FREE_BRB BthFreeBrb;
    PFNBTH_INITIALIZE_BRB BthInitializeBrb;
    PFNBTH_REUSE_BRB BthReuseBrb;
    PFNBTH_IS_BLUETOOTH_VERSION_AVAILABLE IsBluetoothVersionAvailable;
} BTH_PROFILE_DRIVER_INTERFACE, *PBTH_PROFILE_DRIVER_INTERFACE;

#define BTHDDI_ENUMERATOR_INTERFACE_VERSION_FOR_QI          (0x0200)

#define BTHDDI_PROFILE_DRIVER_INTERFACE_VERSION_FOR_QI      (0x0200)

typedef enum _ENUMERATOR_ACTION
{
    ENUMERATOR_ACTION_CREATE = 0,
    ENUMERATOR_ACTION_REMOVE,
    ENUMERATOR_ACTION_DESTROY,
    ENUMERATOR_ACTION_MAX,
} ENUMERATOR_ACTION, *PENUMERATOR_ACTION;

typedef enum _ENUMERATOR_TYPE
{
    ENUMERATOR_TYPE_PROTOCOL = 0,
    ENUMERATOR_TYPE_SERVICE,
    ENUMERATOR_TYPE_DEVICE,
    ENUMERATOR_TYPE_MAX,
} ENUMERATOR_TYPE, *PENUMERATOR_TYPE;

#define BTH_ENUMERATORFL_INCOMING 0x00000001

#define BTH_ENUMERATORFL_OUTGOING 0x00000002

#define BTH_ENUMERATORFL_REENUM   0x00000004

typedef struct _BTH_ENUMERATOR_INFO
{
    ENUMERATOR_TYPE EnumeratorType;
    ENUMERATOR_ACTION Action;
    ULONG Port;
    ULONG Flags;
    GUID Guid;
    ULONG InstanceId;
    WCHAR InstanceIdStr[BTH_MAX_SERVICE_NAME_SIZE];
    USHORT Vid;
    USHORT Pid;
    USHORT Mfg;
    USHORT LocalMfg;
    USHORT VidType;
    WCHAR ServiceName[BTH_MAX_SERVICE_NAME_SIZE];
    CHAR SdpPriLangServiceName[BTH_MAX_SERVICE_NAME_SIZE];
    WCHAR DeviceString[BTH_MAX_SERVICE_NAME_SIZE];
} BTH_ENUMERATOR_INFO, *PBTH_ENUMERATOR_INFO;

#endif

#ifdef __cplusplus
}
#endif

#endif
