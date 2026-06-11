/*
 * PROJECT:     ReactOS Virtual Native 802.11 (dot11) Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver header for a software-only virtual Native 802.11 miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#ifndef _VWIFI_H_
#define _VWIFI_H_

#include <ntddk.h>
#include <ndis.h>
#include <windot11.h>

#define VWIFI_TAG 'FIWV'   /* 'VWIF' */

#define VWIFI_ADDRESS_LENGTH        6
#define VWIFI_MAX_FRAME_SIZE        DOT11_MAX_PDU_SIZE  /* 2346 (802.11 MPDU)  */
#define VWIFI_MTU_SIZE              1500
#define VWIFI_LINK_SPEED_BPS        54000000ULL         /* ~54 Mbit/s (ERP)    */

/* OID_GEN_LINK_SPEED / DOT11 link speed is in units of 100 bit/s. */
#define VWIFI_LINK_SPEED_100BPS     (VWIFI_LINK_SPEED_BPS / 100)

/* Fake permanent MAC 02:52:4F:53:57:31  == "ROSW1" (locally administered). */
#define VWIFI_PERMANENT_MAC_0       0x02
#define VWIFI_PERMANENT_MAC_1       0x52
#define VWIFI_PERMANENT_MAC_2       0x4F
#define VWIFI_PERMANENT_MAC_3       0x53
#define VWIFI_PERMANENT_MAC_4       0x57
#define VWIFI_PERMANENT_MAC_5       0x31

/* Delay between OID_DOT11_SCAN_REQUEST and the SCAN_CONFIRM indication. */
#define VWIFI_SCAN_DELAY_MS         500

/* Default beacon period of the fake APs (TUs). */
#define VWIFI_BEACON_PERIOD         100

/* Maximum number of simulated BSS entries. */
#define VWIFI_MAX_BSS               2

/* Largest beacon-IE blob per BSS (SSID + supported-rates + DS-parameter IEs). */
#define VWIFI_MAX_BSS_IE            64

/* A simulated BSS (access point) the virtual radio "sees" on a scan. */
typedef struct _VWIFI_FAKE_BSS
{
    DOT11_MAC_ADDRESS Bssid;
    DOT11_BSS_TYPE BssType;
    UCHAR Ssid[DOT11_SSID_MAX_LENGTH];
    ULONG SsidLength;
    ULONG ChannelNumber;            /* logical channel (1..14)               */
    ULONG ChCenterFrequency;        /* MHz (e.g. 2412 for channel 1)         */
    LONG Rssi;                      /* dBm (e.g. -40)                        */
    ULONG LinkQuality;              /* 0..100                                */
    USHORT CapabilityInformation;  /* 802.11 capability bits                */
    DOT11_AUTH_ALGORITHM AuthAlgorithm; /* what this AP requires            */
} VWIFI_FAKE_BSS, *PVWIFI_FAKE_BSS;

typedef enum _VWIFI_CONN_STATE
{
    VWifiDisconnected = 0,
    VWifiScanning,
    VWifiAssociating,
    VWifiConnected
} VWIFI_CONN_STATE;

/* Asynchronous job to run (set by an OID, drained by the engine work item). */
typedef enum _VWIFI_JOB
{
    VWifiJobNone = 0,
    VWifiJobScan,           /* finish a scan -> SCAN_CONFIRM                  */
    VWifiJobConnect,        /* assoc-start/assoc-complete/connect-complete   */
    VWifiJobDisconnect      /* DISASSOCIATION + link down                    */
} VWIFI_JOB;

/* dot11 station configuration accepted/stored from OIDs. */
typedef struct _VWIFI_DOT11_STATE
{
    ULONG CurrentOperationMode;     /* DOT11_OPERATION_MODE_*                 */
    DOT11_PHY_TYPE CurrentPhyType;  /* dot11_phy_type_erp by default          */
    ULONG PacketFilter;             /* DOT11_PACKET_TYPE_* mask               */
    BOOLEAN RadioOn;                /* OID_DOT11_NIC_POWER_STATE              */
    BOOLEAN AutoConfigEnabled;
    ULONG CurrentChannel;
    ULONG RtsThreshold;
    ULONG FragmentationThreshold;
    ULONG BeaconPeriod;

    /* Desired-network selectors stored from the connect prologue. */
    DOT11_SSID DesiredSsid;         /* first entry of DESIRED_SSID_LIST       */
    BOOLEAN HaveDesiredSsid;
    DOT11_MAC_ADDRESS DesiredBssid; /* first entry of DESIRED_BSSID_LIST      */
    BOOLEAN HaveDesiredBssid;
    DOT11_BSS_TYPE DesiredBssType;

    DOT11_AUTH_ALGORITHM AuthAlgorithm;
    DOT11_CIPHER_ALGORITHM UnicastCipher;
    DOT11_CIPHER_ALGORITHM MulticastCipher;
} VWIFI_DOT11_STATE, *PVWIFI_DOT11_STATE;

typedef struct _VWIFI_ADAPTER
{
    NDIS_HANDLE MiniportAdapterHandle;

    /* MAC addressing. */
    UCHAR PermanentAddress[VWIFI_ADDRESS_LENGTH];
    UCHAR CurrentAddress[VWIFI_ADDRESS_LENGTH];

    /* Simulated radio. */
    VWIFI_DOT11_STATE Dot11;
    VWIFI_FAKE_BSS Bss[VWIFI_MAX_BSS];
    ULONG BssCount;

    /* Connection engine. */
    VWIFI_CONN_STATE ConnState;
    ULONG ConnectedBssIndex;        /* index into Bss[] while Connected       */
    NDIS_SPIN_LOCK Lock;            /* guards ConnState / job queue           */

    /* Lifecycle. */
    BOOLEAN Halting;
    BOOLEAN DataPathRunning;        /* between RestartEx and PauseEx          */

    /* Async engine: a one-shot timer fires a DPC, which queues an I/O work
     * item; the work item produces the dot11 status indications at
     * PASSIVE_LEVEL.  PendingJob carries which transition to run. */
    NDIS_HANDLE EngineTimer;
    NDIS_HANDLE EngineWorkItem;
    volatile LONG WorkPending;      /* 0/1: a work item is queued/running     */
    VWIFI_JOB PendingJob;

    /* Scan OID pending async completion via NdisMOidRequestComplete.
     * Guarded by Lock; at most one scan outstanding. */
    PNDIS_OID_REQUEST PendingScanOid;

    /* Statistics. */
    ULONG64 TxOkCount;
    ULONG64 TxErrorCount;
    ULONG64 TxBytes;
    ULONG64 RxOkCount;
    ULONG64 RxErrorCount;
    ULONG64 RxBytes;
} VWIFI_ADAPTER, *PVWIFI_ADAPTER;

/* vwifi.c */
DRIVER_INITIALIZE DriverEntry;

MINIPORT_INITIALIZE              VWifiMiniportInitializeEx;
MINIPORT_HALT                    VWifiMiniportHaltEx;
MINIPORT_PAUSE                   VWifiMiniportPauseEx;
MINIPORT_RESTART                 VWifiMiniportRestartEx;
MINIPORT_SHUTDOWN                VWifiMiniportShutdownEx;
MINIPORT_DEVICE_PNP_EVENT_NOTIFY VWifiMiniportDevicePnpEventNotify;
MINIPORT_UNLOAD                  VWifiMiniportUnload;

PVOID VWifiAllocate(IN ULONG Size);
VOID VWifiFree(IN PVOID Buffer);

/* engine.c */
NDIS_TIMER_FUNCTION       VWifiEngineTimerDpc;
NDIS_IO_WORKITEM_FUNCTION VWifiEngineWorker;
VOID VWifiScheduleJob(IN PVWIFI_ADAPTER Adapter, IN VWIFI_JOB Job, IN ULONG DelayMs);
LONG VWifiFindDesiredBss(IN PVWIFI_ADAPTER Adapter);
VOID VWifiInitFakeBssList(IN PVWIFI_ADAPTER Adapter);
ULONG VWifiBuildBssList(IN PVWIFI_ADAPTER Adapter, OUT PUCHAR Buffer, IN ULONG BufferLength,
                        OUT PULONG BytesNeeded);

VOID VWifiIndicateScanConfirm(IN PVWIFI_ADAPTER Adapter, IN NDIS_STATUS ScanStatus);
VOID VWifiIndicateAssocStart(IN PVWIFI_ADAPTER Adapter, IN PVWIFI_FAKE_BSS Bss);
VOID VWifiIndicateAssocComplete(IN PVWIFI_ADAPTER Adapter, IN PVWIFI_FAKE_BSS Bss,
                                IN DOT11_ASSOC_STATUS Status);
VOID VWifiIndicateConnectComplete(IN PVWIFI_ADAPTER Adapter, IN DOT11_ASSOC_STATUS Status);
VOID VWifiIndicateDisassociation(IN PVWIFI_ADAPTER Adapter, IN DOT11_ASSOC_STATUS Reason);
VOID VWifiIndicateLinkState(IN PVWIFI_ADAPTER Adapter, IN BOOLEAN Connected);

/* rxtx.c */
MINIPORT_SEND_NET_BUFFER_LISTS    VWifiMiniportSendNetBufferLists;
MINIPORT_RETURN_NET_BUFFER_LISTS  VWifiMiniportReturnNetBufferLists;
MINIPORT_CANCEL_SEND              VWifiMiniportCancelSend;

/* oid.c */
MINIPORT_OID_REQUEST              VWifiMiniportOidRequest;
MINIPORT_CANCEL_OID_REQUEST       VWifiMiniportCancelOidRequest;

#endif /* _VWIFI_H_ */
