/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     MSM (station management state machine) definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * The MSM is the host-side MLME for an extensible-station (ExtSTA)
 * Native-802.11 miniport: it drives the OID_DOT11_* scan/connect/key flow,
 * consumes NDIS_STATUS_DOT11_* indications, and runs the RSNA supplicant
 * (supplicant.c) over EAPOL-Key frames for a secured WPA2-PSK connect, while
 * honoring lower fullmac adapters that complete and authorize the port in
 * firmware (for example WPA3-SAE).
 */

#ifndef _NWIFI_MSM_H_
#define _NWIFI_MSM_H_

struct _NWIFI_ADAPTER;

/* Opaque supplicant session; supplicant.c owns the type. */
typedef struct _NWIFI_SUPPLICANT NWIFI_SUPPLICANT, *PNWIFI_SUPPLICANT;

/* Connect-engine state. */
typedef enum _NWIFI_MSM_STATE
{
    NwifiMsmIdle = 0,           /* no association in progress / disconnected */
    NwifiMsmScanning,           /* OID_DOT11_SCAN_REQUEST issued, awaiting confirm */
    NwifiMsmAssociating,        /* CONNECT_REQUEST issued, awaiting assoc/conn done */
    NwifiMsmHandshaking,        /* associated, WPA2 4-way handshake in progress */
    NwifiMsmConnectedOpen,      /* associated, open network (no security) */
    NwifiMsmConnectedSecure     /* associated, keys installed, encrypted link up */
} NWIFI_MSM_STATE;

/* One cached scan result: fixed summary plus the raw beacon/probe IEs (so
 * callers can parse the RSN IE), stored in a fixed tail. */
#define NWIFI_MAX_BSS_IE        512
#define NWIFI_MAX_BSS_CACHE     48

typedef struct _NWIFI_BSS_CACHE_ENTRY
{
    BOOLEAN          Valid;
    DOT11_MAC_ADDRESS Bssid;
    DOT11_SSID       Ssid;
    DOT11_BSS_TYPE   BssType;
    ULONG            PhyId;
    LONG             Rssi;
    ULONG            LinkQuality;
    ULONG            ChCenterFrequency;
    USHORT           BeaconPeriod;
    USHORT           CapabilityInformation;
    ULONG            IeLength;
    UCHAR            Ie[NWIFI_MAX_BSS_IE];
    ULONG64          HostTimestamp;          /* KeQueryInterruptTime when cached */
} NWIFI_BSS_CACHE_ENTRY, *PNWIFI_BSS_CACHE_ENTRY;

/* Parameters captured from an IOCTL_NWIFI_CONNECT; the connect FSM proceeds
 * across several indications. */
typedef struct _NWIFI_CONNECT_PARAMS
{
    DOT11_SSID             Ssid;
    DOT11_MAC_ADDRESS      DesiredBssid;       /* all-zero => any */
    BOOLEAN                HaveBssid;
    DOT11_BSS_TYPE         BssType;
    DOT11_AUTH_ALGORITHM   AuthAlgorithm;
    DOT11_CIPHER_ALGORITHM UnicastCipher;
    DOT11_CIPHER_ALGORITHM MulticastCipher;
    BOOLEAN                Secure;             /* encrypted network */
} NWIFI_CONNECT_PARAMS, *PNWIFI_CONNECT_PARAMS;

/* Per-interface MSM object (NWIFI_ADAPTER.MsmContext).  Mutable state is
 * guarded by Lock; synchronous OID round-trips run outside the lock. */
typedef struct _NWIFI_MSM
{
    struct _NWIFI_ADAPTER *Adapter;     /* back-pointer */

    NDIS_SPIN_LOCK   Lock;
    NWIFI_MSM_STATE  State;

    /* Currently-selected / associated BSS. */
    DOT11_MAC_ADDRESS CurrentBssid;
    DOT11_SSID       CurrentSsid;
    DOT11_PHY_TYPE   CurrentPhyType;
    LONG             CurrentRssi;
    ULONG            LinkSpeedKbps;

    /* In-flight connect request parameters. */
    NWIFI_CONNECT_PARAMS Connect;

    /* Scan state. */
    BOOLEAN          ScanInProgress;

    /* Scan-confirm may arrive at DISPATCH_LEVEL, but harvesting the BSS list
     * (OID_DOT11_ENUM_BSS_LIST) is a blocking OID needing PASSIVE_LEVEL. */
    volatile LONG    ScanWorkQueued;
    NDIS_STATUS      LastScanStatus;     /* ndisStatus from the scan-confirm */

    /* Every PASSIVE_LEVEL callback owns a one-shot NDIS work item.  This
     * rundown prevents handle reuse and keeps the MSM alive through unbind. */
    KSPIN_LOCK       WorkItemLock;
    volatile LONG    WorkItemsPending;
    volatile LONG    WorkItemsHalting;
    KEVENT           WorkItemsDrainedEvent;

    /* BSS cache (aggregated across ENUM_BSS_LIST snapshots). */
    NWIFI_BSS_CACHE_ENTRY BssCache[NWIFI_MAX_BSS_CACHE];
    ULONG            BssCount;

    /* Supplicant (secure connect only). */
    PNWIFI_SUPPLICANT Supplicant;
} NWIFI_MSM, *PNWIFI_MSM;

/* ===========================================================================
 *  MSM lifecycle
 * ===========================================================================
 */

/* Create/destroy the MSM object for an adapter (called from bind/unbind). */
NDIS_STATUS NwifiMsmCreate(_In_ struct _NWIFI_ADAPTER *Adapter);
VOID        NwifiMsmDestroy(_In_ struct _NWIFI_ADAPTER *Adapter);

/* ===========================================================================
 *  Control-channel entry points (driven by control.c IOCTL dispatch)
 * ===========================================================================
 */

/* Kick OID_DOT11_SCAN_REQUEST.  Completion arrives via the scan-confirm hook. */
NDIS_STATUS NwifiMsmStartScan(
    _In_ PNWIFI_MSM Msm,
    _In_ DOT11_BSS_TYPE BssType,
    _In_opt_ PDOT11_SSID Ssid);

/* Program the desired-network OIDs and drive OID_DOT11_CONNECT_REQUEST. */
NDIS_STATUS NwifiMsmConnect(
    _In_ PNWIFI_MSM Msm,
    _In_ PNWIFI_CONNECT_PARAMS Params);

/* Queue/retire a one-shot PASSIVE_LEVEL callback protected by MSM rundown. */
BOOLEAN NwifiMsmQueueWorkItem(
    _In_ PNWIFI_MSM Msm,
    _In_ NDIS_IO_WORKITEM_ROUTINE Routine,
    _In_opt_ PVOID Context);

VOID NwifiMsmCompleteWorkItem(
    _In_ PNWIFI_MSM Msm,
    _In_ NDIS_HANDLE WorkItem);

/* Publish an associated link after either host key installation or lower
 * fullmac authorization has completed. */
VOID NwifiMsmLinkUp(
    _In_ PNWIFI_MSM Msm,
    _In_ BOOLEAN Secure);

/* OID_DOT11_DISCONNECT_REQUEST. */
NDIS_STATUS NwifiMsmDisconnect(_In_ PNWIFI_MSM Msm);

/* Copy the aggregated BSS cache out to a wlansvc NWIFI_BSS_LIST buffer. */
NDIS_STATUS NwifiMsmGetBssList(
    _In_ PNWIFI_MSM Msm,
    _Out_writes_bytes_(OutLength) PVOID Out,
    _In_ ULONG OutLength,
    _Out_ PULONG BytesWritten);

/* Fill an NWIFI_LINK_STATE snapshot. */
VOID NwifiMsmQueryState(
    _In_ PNWIFI_MSM Msm,
    _Out_ PNWIFI_LINK_STATE State);

/* Seed/install a cipher key from IOCTL_NWIFI_SET_KEY.  When the key carries a
 * PSK/passphrase (Algorithm==NONE sentinel) it seeds the supplicant; otherwise
 * it is forwarded straight down as a cipher key. */
NDIS_STATUS NwifiMsmSetKey(
    _In_ PNWIFI_MSM Msm,
    _In_ PNWIFI_SET_KEY Key);

/* Map the connect FSM state to the wlansvc-facing NWIFI_LINK_STATUS. */
NWIFI_LINK_STATUS NwifiMsmLinkStatus(_In_ PNWIFI_MSM Msm);

/* ===========================================================================
 *  Indication entry points (called from protocol.c / datapath.c)
 * ===========================================================================
 */

/* protocol.c StatusEx: a dot11 status indication arrived from the lower NIC. */
VOID NwifiMsmIndicateStatus(
    _In_ struct _NWIFI_ADAPTER *Adapter,
    _In_ PNDIS_STATUS_INDICATION StatusIndication);

/* protocol.c OidRequestComplete: an async OID we issued has completed. */
VOID NwifiMsmOidComplete(
    _In_ struct _NWIFI_ADAPTER *Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status);

/* datapath.c RX: classify an inbound frame; returns TRUE if the MSM consumed it
 * (e.g. an EAPOL-Key frame handed to the supplicant) and it must NOT be
 * indicated up to TCP/IP. */
BOOLEAN NwifiMsmRxData(
    _In_ struct _NWIFI_ADAPTER *Adapter,
    _In_reads_bytes_(EthLength) PUCHAR Eth,
    _In_ ULONG EthLength);

/* bind/unbind: interface arrival/removal notifications for the control channel. */
VOID NwifiMsmInterfaceArrival(_In_ struct _NWIFI_ADAPTER *Adapter);
VOID NwifiMsmInterfaceRemoval(_In_ struct _NWIFI_ADAPTER *Adapter);

/* ===========================================================================
 *  Supplicant glue (supplicant.c) - called by the MSM
 * ===========================================================================
 */

/* Start a WPA2-PSK supplicant session for the current association. */
NDIS_STATUS NwifiSupplicantStart(_In_ PNWIFI_MSM Msm);

/* Send the cached credential to a lower fullmac adapter for firmware-owned
 * authentication (currently WPA3-SAE). */
NDIS_STATUS NwifiSupplicantSeedFirmware(_In_ PNWIFI_MSM Msm);

/* Stop the supplicant session (zeroise keys, disarm) but keep the object so a
 * concurrent RX-path EAPOL feed never dereferences freed memory. */
VOID NwifiSupplicantStop(_In_ PNWIFI_MSM Msm);

/* Free the supplicant object entirely.  Only call once the data path is
 * quiesced (NwifiMsmDestroy at unbind). */
VOID NwifiSupplicantFree(_In_ PNWIFI_MSM Msm);

/* Seed the supplicant PSK/passphrase from IOCTL_NWIFI_SET_KEY (before connect). */
NDIS_STATUS NwifiSupplicantSetPassphrase(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(Length) PUCHAR Passphrase,
    _In_ ULONG Length);

/* Tell the supplicant the authenticator (AP) MAC once association completes. */
VOID NwifiSupplicantSetApAddr(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(6) PUCHAR ApMac);

/* Feed an EAPOL-Key frame to the supplicant; transmits any reply and, on
 * COMPLETED, installs the derived PTK/GTK.  Returns TRUE if consumed. */
BOOLEAN NwifiSupplicantRxEapol(
    _In_ PNWIFI_MSM Msm,
    _In_reads_bytes_(Length) PUCHAR Eapol,
    _In_ ULONG Length,
    _In_ PUCHAR SrcMac,
    _In_ PUCHAR DstMac);

/* ===========================================================================
 *  Cipher-key install helpers (supplicant.c) - build the DOT11 cipher-key
 *  structs and push them down via OID_DOT11_CIPHER_*.
 * ===========================================================================
 */

/* OID_DOT11_CIPHER_KEY_MAPPING_KEY: install a pairwise (PTK) cipher key for a
 * specific peer. */
NDIS_STATUS NwifiInstallPairwiseKey(
    _In_ struct _NWIFI_ADAPTER *Adapter,
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_reads_bytes_(6) PUCHAR PeerMac,
    _In_reads_bytes_(KeyLength) PUCHAR Key,
    _In_ ULONG KeyLength);

/* OID_DOT11_CIPHER_DEFAULT_KEY (+ CIPHER_DEFAULT_KEY_ID): install a group (GTK)
 * / default cipher key at the given key index. */
NDIS_STATUS NwifiInstallGroupKey(
    _In_ struct _NWIFI_ADAPTER *Adapter,
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_ UCHAR KeyId,
    _In_reads_bytes_(KeyLength) PUCHAR Key,
    _In_ ULONG KeyLength);

#endif /* _NWIFI_MSM_H_ */
