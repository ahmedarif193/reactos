/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     nwifi <-> wlansvc kernel/user control interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _NWIFI_IOCTL_H_
#define _NWIFI_IOCTL_H_

#include <windot11.h>

/* User-visible name of the nwifi control device object / symbolic link. */
#define NWIFI_CONTROL_DEVICE_NAME       L"\\Device\\Nwifi"
#define NWIFI_CONTROL_SYMBOLIC_NAME     L"\\DosDevices\\Nwifi"
#define NWIFI_CONTROL_WIN32_NAME        L"\\\\.\\Nwifi"

/*
 * IOCTL code space.  FILE_DEVICE_NETWORK, buffered I/O, FILE_ANY_ACCESS.
 * Function codes start at 0x800 (the vendor-reserved range).
 */
#ifndef CTL_CODE
#include <winioctl.h>
#endif

#define NWIFI_IOCTL_BASE                0x800

#define _NWIFI_CTL(_fn) \
    CTL_CODE(FILE_DEVICE_NETWORK, (NWIFI_IOCTL_BASE + (_fn)), \
             METHOD_BUFFERED, FILE_ANY_ACCESS)

/* --- Interface enumeration ------------------------------------------------ */
/* OUT: NWIFI_INTERFACE_LIST. Lists every dot11 adapter nwifi has bound. */
#define IOCTL_NWIFI_ENUM_INTERFACES     _NWIFI_CTL(0x00)

/* --- Scan ----------------------------------------------------------------- */
/* IN: NWIFI_SCAN_REQUEST.   Issues OID_DOT11_SCAN_REQUEST on the lower miniport.
 * Completion is asynchronous; the scan-complete event arrives on the notify
 * channel and results are read with IOCTL_NWIFI_GET_BSS_LIST. */
#define IOCTL_NWIFI_SCAN                _NWIFI_CTL(0x01)

/* OUT: NWIFI_BSS_LIST (variable). Snapshot of the aggregated BSS list. */
#define IOCTL_NWIFI_GET_BSS_LIST        _NWIFI_CTL(0x02)

/* --- Connect / disconnect ------------------------------------------------- */
/* IN: NWIFI_CONNECT_REQUEST. Starts the connect state machine. */
#define IOCTL_NWIFI_CONNECT             _NWIFI_CTL(0x03)
/* IN: NWIFI_INTERFACE_REF.   Disconnect / OID_DOT11_DISCONNECT_REQUEST. */
#define IOCTL_NWIFI_DISCONNECT          _NWIFI_CTL(0x04)

/* --- Status / query ------------------------------------------------------- */
/* IN: NWIFI_INTERFACE_REF  OUT: NWIFI_LINK_STATE. Current association state. */
#define IOCTL_NWIFI_QUERY_STATE         _NWIFI_CTL(0x05)

/* --- Supplicant key install ----------------------------------------------- */
/* IN: NWIFI_SET_KEY. Forwarded as OID_DOT11_CIPHER_DEFAULT_KEY / mapping key. */
#define IOCTL_NWIFI_SET_KEY             _NWIFI_CTL(0x06)

/* --- Asynchronous notification channel ------------------------------------ */
/* This IOCTL pends in the driver and completes when the next interface event
 * occurs; the caller re-issues it in a loop.  OUT: NWIFI_NOTIFICATION. */
#define IOCTL_NWIFI_GET_NOTIFICATION    _NWIFI_CTL(0x07)

/* ===========================================================================
 *  Payload structures
 * ===========================================================================
 */

/* A logical reference to one bound interface.  The "Luid" matches the upper
 * virtual miniport's NET_LUID so wlansvc can correlate with the IP stack. */
typedef struct _NWIFI_INTERFACE_REF
{
    ULONG  Size;            /* sizeof(NWIFI_INTERFACE_REF) for versioning */
    ULONG  InterfaceIndex;  /* nwifi-local index (0..N-1) */
    ULONG64 UpperLuid;      /* NET_LUID of the presented 802.3 miniport */
    DOT11_MAC_ADDRESS MacAddress;
} NWIFI_INTERFACE_REF, *PNWIFI_INTERFACE_REF;

typedef struct _NWIFI_INTERFACE_LIST
{
    ULONG Size;
    ULONG NumberOfItems;
    NWIFI_INTERFACE_REF Item[1];    /* NumberOfItems entries */
} NWIFI_INTERFACE_LIST, *PNWIFI_INTERFACE_LIST;

typedef struct _NWIFI_SCAN_REQUEST
{
    ULONG Size;
    ULONG InterfaceIndex;
    DOT11_BSS_TYPE BssType;
    DOT11_SSID Ssid;                /* zero-length SSID => broadcast scan */
} NWIFI_SCAN_REQUEST, *PNWIFI_SCAN_REQUEST;

/* One scan result entry; the raw beacon/probe IEs follow the fixed part. */
typedef struct _NWIFI_BSS_ENTRY
{
    ULONG Size;                     /* size of this entry incl. trailing IEs */
    DOT11_MAC_ADDRESS Bssid;
    DOT11_SSID Ssid;
    DOT11_BSS_TYPE BssType;
    DOT11_PHY_TYPE PhyType;
    LONG  Rssi;
    ULONG ChCenterFrequency;
    USHORT BeaconPeriod;
    USHORT CapabilityInformation;
    ULONG IeOffset;                 /* offset from entry start to IE blob */
    ULONG IeLength;
} NWIFI_BSS_ENTRY, *PNWIFI_BSS_ENTRY;

typedef struct _NWIFI_BSS_LIST
{
    ULONG Size;                     /* total bytes returned */
    ULONG InterfaceIndex;
    ULONG NumberOfItems;
    ULONG FirstEntryOffset;         /* offset to first NWIFI_BSS_ENTRY */
    /* NumberOfItems variable-length NWIFI_BSS_ENTRY records follow. */
} NWIFI_BSS_LIST, *PNWIFI_BSS_LIST;

typedef struct _NWIFI_CONNECT_REQUEST
{
    ULONG Size;
    ULONG InterfaceIndex;
    DOT11_SSID Ssid;
    DOT11_MAC_ADDRESS DesiredBssid; /* all-zero => any BSSID for the SSID */
    DOT11_BSS_TYPE BssType;
    DOT11_AUTH_ALGORITHM AuthAlgorithm;
    DOT11_CIPHER_ALGORITHM UnicastCipher;
    DOT11_CIPHER_ALGORITHM MulticastCipher;
    /* The PSK/PMK is installed separately via IOCTL_NWIFI_SET_KEY. */
} NWIFI_CONNECT_REQUEST, *PNWIFI_CONNECT_REQUEST;

typedef enum _NWIFI_LINK_STATUS
{
    NwifiLinkDisconnected = 0,
    NwifiLinkScanning,
    NwifiLinkAuthenticating,
    NwifiLinkAssociating,
    NwifiLinkConnectedOpen,         /* associated, no security */
    NwifiLinkConnectedSecure        /* 4-way handshake done */
} NWIFI_LINK_STATUS;

typedef struct _NWIFI_LINK_STATE
{
    ULONG Size;
    ULONG InterfaceIndex;
    NWIFI_LINK_STATUS Status;
    DOT11_MAC_ADDRESS Bssid;
    DOT11_SSID Ssid;
    DOT11_PHY_TYPE PhyType;
    LONG  Rssi;
    ULONG LinkSpeedKbps;
} NWIFI_LINK_STATE, *PNWIFI_LINK_STATE;

typedef struct _NWIFI_SET_KEY
{
    ULONG Size;
    ULONG InterfaceIndex;
    DOT11_CIPHER_ALGORITHM Algorithm;
    BOOLEAN IsPairwise;             /* TRUE: mapping key, FALSE: default/group */
    UCHAR  KeyId;
    DOT11_MAC_ADDRESS PeerMac;      /* for pairwise keys */
    ULONG  KeyLength;
    UCHAR  KeyData[64];             /* CCMP=16, plus room for RSC/TKIP MIC */
} NWIFI_SET_KEY, *PNWIFI_SET_KEY;

typedef enum _NWIFI_NOTIFICATION_CODE
{
    NwifiNotifyNone = 0,
    NwifiNotifyScanComplete,
    NwifiNotifyConnectComplete,
    NwifiNotifyDisconnected,
    NwifiNotifyLinkUp,
    NwifiNotifyLinkDown,
    NwifiNotifyInterfaceArrival,
    NwifiNotifyInterfaceRemoval
} NWIFI_NOTIFICATION_CODE;

typedef struct _NWIFI_NOTIFICATION
{
    ULONG Size;
    ULONG InterfaceIndex;
    NWIFI_NOTIFICATION_CODE Code;
    NDIS_STATUS StatusCode;         /* NDIS_STATUS_DOT11_* where applicable */
} NWIFI_NOTIFICATION, *PNWIFI_NOTIFICATION;

#endif /* _NWIFI_IOCTL_H_ */
