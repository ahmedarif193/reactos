/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Native WiFi intermediate driver shared definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * NDIS 6.20 intermediate driver: the protocol edge binds a Native-802.11
 * (dot11) miniport and creates a paired upper virtual 802.3 miniport
 * (NdisIMInitializeDeviceInstanceEx) that translates 802.3 <-> 802.11.
 */

#ifndef _NWIFI_H_
#define _NWIFI_H_

#include <ndis.h>
#include <windot11.h>
#include <reactos/nwifi_ioctl.h>

#define NWIFI_TAG  'FIWN'   /* 'NWIF' */

/* Link speed reported to TCP/IP for the virtual Ethernet adapter until the MSM
 * learns the real PHY rate.  54 Mbit/s (classic 802.11g) is a safe stand-in. */
#define NWIFI_DEFAULT_LINK_SPEED    (54ULL * 1000000ULL)
#define NWIFI_MAX_LINK_SPEED        433300000ULL

/* ===========================================================================
 *  IEEE 802.11 framing
 * ===========================================================================
 */
#define IEEE80211_ADDR_LEN          6

/* Frame Control field (little-endian on the air). */
#define IEEE80211_FC0_VERSION_0     0x00
#define IEEE80211_FC0_TYPE_MASK     0x0C
#define IEEE80211_FC0_TYPE_DATA     0x08
#define IEEE80211_FC0_SUBTYPE_MASK  0xF0
#define IEEE80211_FC0_SUBTYPE_DATA  0x00
#define IEEE80211_FC0_SUBTYPE_QOS   0x80

#define IEEE80211_FC1_DIR_MASK      0x03
#define IEEE80211_FC1_DIR_NODS      0x00    /* STA <-> STA (IBSS) */
#define IEEE80211_FC1_DIR_TODS      0x01    /* STA  -> AP  (infra uplink) */
#define IEEE80211_FC1_DIR_FROMDS    0x02    /* AP   -> STA (infra downlink) */
#define IEEE80211_FC1_DIR_DSTODS    0x03    /* WDS */
#define IEEE80211_FC1_PROTECTED     0x40

/* 3-address 802.11 MAC header; Addr1/2/3 are interpreted per the ToDS/FromDS
 * bits.  Only the non-QoS 3-address data form is generated/parsed. */
#include <pshpack1.h>
typedef struct _IEEE80211_MAC_HEADER
{
    UCHAR  FrameControl[2];
    UCHAR  DurationId[2];
    UCHAR  Address1[IEEE80211_ADDR_LEN];
    UCHAR  Address2[IEEE80211_ADDR_LEN];
    UCHAR  Address3[IEEE80211_ADDR_LEN];
    UCHAR  SequenceControl[2];
} IEEE80211_MAC_HEADER, *PIEEE80211_MAC_HEADER;

/* RFC 1042 / 802.2 SNAP header that replaces the Ethernet Type field when an
 * Ethernet II frame is carried as an 802.11 MSDU. */
typedef struct _RFC1042_SNAP_HEADER
{
    UCHAR  Dsap;        /* 0xAA */
    UCHAR  Ssap;        /* 0xAA */
    UCHAR  Control;     /* 0x03 */
    UCHAR  Oui[3];      /* 0x00 0x00 0x00 (or 0x00-00-F8 for bridge tunnel) */
    UCHAR  EtherType[2];
} RFC1042_SNAP_HEADER, *PRFC1042_SNAP_HEADER;
#include <poppack.h>

#define IEEE80211_MAC_HEADER_LEN    sizeof(IEEE80211_MAC_HEADER)
#define RFC1042_SNAP_HEADER_LEN     sizeof(RFC1042_SNAP_HEADER)

/* 802.3 framing. */
#define ETH_ADDR_LEN                6
#define ETH_HEADER_LEN              14      /* dst(6)+src(6)+type(2) */
#define ETH_TYPE_OFFSET             12
#define ETH_MTU                     1500
#define ETH_MAX_FRAME               (ETH_HEADER_LEN + ETH_MTU)  /* 1514 */
#define NWIFI_MAX_MULTICAST_ADDRESSES 32
#define NWIFI_SUPPORTED_PACKET_FILTERS \
    (NDIS_PACKET_TYPE_DIRECTED | NDIS_PACKET_TYPE_MULTICAST | \
     NDIS_PACKET_TYPE_ALL_MULTICAST | NDIS_PACKET_TYPE_BROADCAST)

/* Largest 802.11 MSDU we build = SNAP + payload (no Ethernet addrs/type). */
#define NWIFI_MAX_80211_FRAME \
    (IEEE80211_MAC_HEADER_LEN + RFC1042_SNAP_HEADER_LEN + ETH_MTU)

/* EtherTypes that, per RFC 1042 / IEEE 802.1H, use the bridge-tunnel SNAP OUI
 * (00-00-F8) instead of the RFC 1042 OUI (00-00-00). */
#define ETHERTYPE_AARP              0x80F3
#define ETHERTYPE_IPX              0x8137

/* ===========================================================================
 *  Driver-wide state
 * ===========================================================================
 */
typedef struct _NWIFI_GLOBALS
{
    NDIS_HANDLE   MiniportDriverHandle;     /* NdisMRegisterMiniportDriver */
    NDIS_HANDLE   ProtocolHandle;           /* NdisRegisterProtocolDriver  */
    PDRIVER_OBJECT DriverObject;

    /* All bound lower adapters (NWIFI_ADAPTER.Link), guarded by AdapterLock. */
    LIST_ENTRY    AdapterList;
    NDIS_SPIN_LOCK AdapterLock;
    volatile LONG NextInterfaceIndex;

    /* Control device for the wlansvc IOCTL channel (\Device\Nwifi). */
    PDEVICE_OBJECT ControlDeviceObject;

    /* Pended IOCTL_NWIFI_GET_NOTIFICATION IRPs plus a small ring buffering
     * events that fired with no IRP waiting.  Guarded by NotifyLock. */
    LIST_ENTRY     NotifyIrpQueue;
    KSPIN_LOCK     NotifyLock;
#define NWIFI_NOTIFY_RING   16
    NWIFI_NOTIFICATION NotifyRing[NWIFI_NOTIFY_RING];
    ULONG          NotifyHead;      /* next slot to write */
    ULONG          NotifyTail;      /* next slot to read */
    ULONG          NotifyCount;     /* queued events with no waiter */
} NWIFI_GLOBALS, *PNWIFI_GLOBALS;

extern NWIFI_GLOBALS gNwifi;

typedef enum _NWIFI_STATE
{
    NwifiStateInitializing = 0,
    NwifiStatePaused,
    NwifiStateRunning,
    NwifiStateHalting
} NWIFI_STATE;

/* One bound dot11 device: the protocol binding to the lower miniport plus
 * the upper virtual miniport presented to TCP/IP. */
typedef struct _NWIFI_ADAPTER
{
    LIST_ENTRY    Link;
    ULONG         InterfaceIndex;

    /* ---- Lower (protocol) edge ---- */
    NDIS_HANDLE   BindingHandle;            /* NdisOpenAdapterEx result */
    PNDIS_STRING  LowerDeviceName;          /* copied from bind parameters */
    NDIS_STRING   LowerDeviceNameStore;

    /* ---- Upper (virtual miniport) edge ---- */
    NDIS_HANDLE   MiniportAdapterHandle;    /* set in MiniportInitializeEx */
    NDIS_STRING   UpperDeviceInstance;      /* IM device instance name */
    BOOLEAN       MiniportInitialized;

    /* ---- Shared identity / state ---- */
    UCHAR         MacAddress[IEEE80211_ADDR_LEN];   /* lower MAC == upper MAC */
    NWIFI_STATE   State;
    ULONG         CurrentPacketFilter;
    ULONG         CurrentLookahead;
    ULONG         MulticastAddressCount;
    UCHAR         MulticastAddresses[NWIFI_MAX_MULTICAST_ADDRESSES][ETH_ADDR_LEN];
    BOOLEAN       MediaConnected;
    ULONG64       LinkSpeedBps;

    /* Association state, owned by the MSM.  ToDS frames use Bssid as Address1. */
    UCHAR         Bssid[IEEE80211_ADDR_LEN];
    DOT11_SSID    Ssid;
    BOOLEAN       Associated;
    volatile LONG SequenceNumber;           /* 802.11 sequence counter (<<4) */

    /* ---- Data-path resources ---- */
    NDIS_HANDLE   TxNblPool;                /* NBLs we build to send down */
    NDIS_HANDLE   RxNblPool;                /* NBLs we indicate up */
    NDIS_SPIN_LOCK DataLock;

    /* In-flight accounting so Pause/Halt can drain safely. */
    volatile LONG PendingSends;             /* NBLs sent down, not completed */
    volatile LONG PendingReceives;          /* NBLs indicated up, not returned */

    /* ---- Lower-binding lifecycle completion ---- */
    NDIS_EVENT    OpenEvent;
    NDIS_STATUS   OpenStatus;
    NDIS_EVENT    CloseEvent;

    /* ---- Statistics ---- */
    volatile LONG64 TxOk;
    volatile LONG64 RxOk;
    volatile LONG64 TxError;
    volatile LONG64 RxError;
    volatile LONG64 TxBytes;
    volatile LONG64 RxBytes;

    /* Station management object; typed as PNWIFI_MSM (see msm.h), kept as
     * PVOID so this header need not pull in MSM internals. */
    PVOID         MsmContext;
} NWIFI_ADAPTER, *PNWIFI_ADAPTER;

/* Per-NBL bookkeeping for locally built NBLs, stored in the pool's per-NBL
 * context area so the free path can recover the original buffers. */
typedef struct _NWIFI_NBL_CONTEXT
{
    PNWIFI_ADAPTER Adapter;
    PNET_BUFFER_LIST OriginalNbl;   /* upper NBL that triggered a send-down */
    PUCHAR        DataBuffer;        /* allocated frame buffer for this NBL */
    ULONG         DataLength;
    PMDL          Mdl;
} NWIFI_NBL_CONTEXT, *PNWIFI_NBL_CONTEXT;

/* ===========================================================================
 *  Prototypes
 * ===========================================================================
 */

/* --- nwifi.c : DriverEntry + registration --------------------------------- */
DRIVER_INITIALIZE DriverEntry;
DRIVER_UNLOAD     NwifiUnload;

/* --- protocol.c : lower edge ---------------------------------------------- */
PROTOCOL_SET_OPTIONS                    NwifiProtocolSetOptions;
PROTOCOL_BIND_ADAPTER_EX                NwifiBindAdapterEx;
PROTOCOL_UNBIND_ADAPTER_EX              NwifiUnbindAdapterEx;
PROTOCOL_OPEN_ADAPTER_COMPLETE_EX       NwifiOpenAdapterCompleteEx;
PROTOCOL_CLOSE_ADAPTER_COMPLETE_EX      NwifiCloseAdapterCompleteEx;
PROTOCOL_OID_REQUEST_COMPLETE           NwifiProtocolOidRequestComplete;
PROTOCOL_STATUS_EX                      NwifiProtocolStatusEx;
PROTOCOL_NET_PNP_EVENT                  NwifiProtocolNetPnPEvent;
PROTOCOL_SEND_NET_BUFFER_LISTS_COMPLETE NwifiProtocolSendNblComplete;
PROTOCOL_RECEIVE_NET_BUFFER_LISTS       NwifiProtocolReceiveNbl;
PROTOCOL_UNINSTALL                      NwifiProtocolUninstall;

NDIS_STATUS NwifiProtocolDoRequest(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_ NDIS_OID Oid,
    _Inout_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesProcessed);

/* --- miniport.c : upper edge ---------------------------------------------- */
MINIPORT_INITIALIZE              NwifiMiniportInitializeEx;
MINIPORT_HALT                    NwifiMiniportHaltEx;
MINIPORT_PAUSE                   NwifiMiniportPauseEx;
MINIPORT_RESTART                 NwifiMiniportRestartEx;
MINIPORT_SHUTDOWN                NwifiMiniportShutdownEx;
MINIPORT_DEVICE_PNP_EVENT_NOTIFY NwifiMiniportDevicePnPEventNotify;
MINIPORT_CANCEL_SEND             NwifiMiniportCancelSend;
MINIPORT_RETURN_NET_BUFFER_LISTS NwifiMiniportReturnNetBufferLists;

/* --- datapath.c : 802.3 <-> 802.11 translation ---------------------------- */
MINIPORT_SEND_NET_BUFFER_LISTS   NwifiMiniportSendNetBufferLists;

VOID NwifiReceiveFromLower(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG NumberOfNetBufferLists,
    _In_ ULONG ReceiveFlags);

VOID NwifiSendCompleteFromLower(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG SendCompleteFlags);

VOID NwifiIndicateLinkState(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ BOOLEAN Connected);

/* --- oid.c : OID translation/forwarding ----------------------------------- */
MINIPORT_OID_REQUEST        NwifiMiniportOidRequest;
MINIPORT_CANCEL_OID_REQUEST NwifiMiniportCancelOidRequest;

extern NDIS_OID NwifiSupportedOids[];
extern ULONG    NwifiSupportedOidCount;

/* --- helpers (nwifi.c) ---------------------------------------------------- */
PVOID NwifiAllocate(_In_ SIZE_T Size);
VOID  NwifiFree(_In_ PVOID Buffer);

/* Find a bound adapter by its nwifi-local interface index.  Returns NULL if no
 * such interface is bound.  Caller must hold no locks; runs at <= DISPATCH. */
PNWIFI_ADAPTER NwifiFindAdapterByIndex(_In_ ULONG InterfaceIndex);

/* --- control.c : \Device\Nwifi control device + IOCTL dispatch ------------- */
NDIS_STATUS NwifiCreateControlDevice(VOID);
VOID        NwifiDeleteControlDevice(VOID);

/* Post an asynchronous interface event to the wlansvc notify channel.  Either
 * completes a pended IOCTL_NWIFI_GET_NOTIFICATION IRP, or rings the event for
 * the next re-issue.  Safe at <= DISPATCH_LEVEL. */
VOID NwifiQueueNotification(
    _In_ ULONG InterfaceIndex,
    _In_ NWIFI_NOTIFICATION_CODE Code,
    _In_ NDIS_STATUS StatusCode);

/* --- datapath.c : raw 802.11 TX inject (used by the supplicant for EAPOL) -- *
 * Build an 802.11 data frame around an L2 payload carrying its own EtherType
 * (e.g. EAPOL 0x888E) and send it down to the lower miniport. */
NDIS_STATUS NwifiInjectL2Frame(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_reads_bytes_(6) PUCHAR DestMac,
    _In_ USHORT EtherType,
    _In_reads_bytes_(PayloadLength) PUCHAR Payload,
    _In_ ULONG PayloadLength,
    _In_ BOOLEAN SendUnencrypted);

#include "msm.h"

#endif /* _NWIFI_H_ */
