/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Upper (virtual miniport) edge: init/halt/pause/restart
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"

#define NDEBUG
#include <debug.h>

/* ===========================================================================
 *  Adapter attributes - present a standard 802.3 Ethernet NIC upward
 * ===========================================================================
 */
static
NDIS_STATUS
NwifiSetMiniportAttributes(
    _In_ PNWIFI_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RegAttr;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GenAttr;
    NDIS_STATUS Status;

    RtlZeroMemory(&RegAttr, sizeof(RegAttr));
    RegAttr.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegAttr.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegAttr.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES);
    RegAttr.MiniportAdapterContext = (NDIS_HANDLE)Adapter;
    RegAttr.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND |
                             NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    RegAttr.CheckForHangTimeInSeconds = 0;
    RegAttr.InterfaceType = NdisInterfaceInternal;

    Status = NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                 (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RegAttr);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: SetMiniportAttributes(registration) failed 0x%08X\n", Status);
        return Status;
    }

    RtlZeroMemory(&GenAttr, sizeof(GenAttr));
    GenAttr.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GenAttr.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttr.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES);

    /* Plain Ethernet upward so TCP/IP binds unchanged; PhysicalMediumType
     * Native802_11 keeps the wireless nature discoverable (matches Windows
     * nwifi: MediaType 802_3, PhysicalMediumType NativeWifi). */
    GenAttr.MediaType = NdisMedium802_3;
    GenAttr.PhysicalMediumType = NdisPhysicalMediumNative802_11;
    GenAttr.MtuSize = ETH_MTU;
    GenAttr.MaxXmitLinkSpeed = NWIFI_DEFAULT_LINK_SPEED;
    GenAttr.MaxRcvLinkSpeed = NWIFI_DEFAULT_LINK_SPEED;
    GenAttr.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    GenAttr.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    GenAttr.MediaConnectState = MediaConnectStateDisconnected;
    GenAttr.MediaDuplexState = MediaDuplexStateFull;
    GenAttr.LookaheadSize = ETH_MAX_FRAME;
    GenAttr.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    GenAttr.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST;
    GenAttr.MaxMulticastListSize = 32;
    GenAttr.MacAddressLength = ETH_ADDR_LEN;
    RtlCopyMemory(GenAttr.PermanentMacAddress, Adapter->MacAddress, ETH_ADDR_LEN);
    RtlCopyMemory(GenAttr.CurrentMacAddress, Adapter->MacAddress, ETH_ADDR_LEN);
    GenAttr.RecvScaleCapabilities = NULL;
    GenAttr.AccessType = NET_IF_ACCESS_BROADCAST;
    GenAttr.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GenAttr.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    GenAttr.IfType = IF_TYPE_IEEE80211;     /* reported as a wireless interface */
    GenAttr.IfConnectorPresent = TRUE;
    GenAttr.SupportedStatistics = 0;
    GenAttr.SupportedOidList = NwifiSupportedOids;
    GenAttr.SupportedOidListLength = NwifiSupportedOidCount * sizeof(NDIS_OID);

    Status = NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
                 (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&GenAttr);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: SetMiniportAttributes(general) failed 0x%08X\n", Status);
    }
    return Status;
}

/* ===========================================================================
 *  MiniportInitializeEx - called in response to NdisIMInitializeDeviceInstanceEx
 * ===========================================================================
 */
NDIS_STATUS
NTAPI
NwifiMiniportInitializeEx(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PNWIFI_ADAPTER Adapter;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    /* The device context we passed to NdisIMInitializeDeviceInstanceEx. */
    Adapter = (PNWIFI_ADAPTER)NdisIMGetDeviceContext(NdisMiniportHandle);
    if (Adapter == NULL)
    {
        DPRINT1("NWIFI: MiniportInitializeEx with no device context\n");
        return NDIS_STATUS_FAILURE;
    }

    DPRINT1("NWIFI: MiniportInitializeEx interface %u\n", Adapter->InterfaceIndex);

    Adapter->MiniportAdapterHandle = NdisMiniportHandle;

    Status = NwifiSetMiniportAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Adapter->MiniportAdapterHandle = NULL;
        return Status;
    }

    /* Default receive filter; refined by OID_GEN_CURRENT_PACKET_FILTER sets. */
    Adapter->CurrentPacketFilter = 0;
    Adapter->CurrentLookahead = ETH_MAX_FRAME;
    Adapter->MiniportInitialized = TRUE;
    Adapter->State = NwifiStatePaused;  /* NDIS starts a miniport paused */

    return NDIS_STATUS_SUCCESS;
}

/* ===========================================================================
 *  MiniportHaltEx
 * ===========================================================================
 */
VOID
NTAPI
NwifiMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    DPRINT1("NWIFI: MiniportHaltEx interface %u\n", Adapter->InterfaceIndex);

    Adapter->State = NwifiStateHalting;

    /* NDIS pauses a miniport before halting it; spin until in-flight
     * sends/receives drain so pools are not freed under an indication. */
    while (Adapter->PendingReceives != 0 || Adapter->PendingSends != 0)
    {
        NdisMSleep(1000);
    }

    Adapter->MiniportAdapterHandle = NULL;
    Adapter->MiniportInitialized = FALSE;

    /* The NWIFI_ADAPTER is not freed here: its lifetime is owned by the
     * protocol edge (UnbindAdapterEx), which drives this halt. */
}

/* ===========================================================================
 *  MiniportPauseEx / MiniportRestartEx
 * ===========================================================================
 */
NDIS_STATUS
NTAPI
NwifiMiniportPauseEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS MiniportPauseParameters)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(MiniportPauseParameters);

    DPRINT1("NWIFI: MiniportPauseEx interface %u\n", Adapter->InterfaceIndex);

    NdisAcquireSpinLock(&Adapter->DataLock);
    Adapter->State = NwifiStatePaused;
    NdisReleaseSpinLock(&Adapter->DataLock);

    /* NDIS guarantees no new sends after this returns; wait for in-flight
     * sends/receives to drain.  Receives arriving while paused are dropped
     * in the data path. */
    while (Adapter->PendingSends != 0 || Adapter->PendingReceives != 0)
    {
        NdisMSleep(1000);
    }

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NwifiMiniportRestartEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS MiniportRestartParameters)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(MiniportRestartParameters);

    DPRINT1("NWIFI: MiniportRestartEx interface %u\n", Adapter->InterfaceIndex);

    NdisAcquireSpinLock(&Adapter->DataLock);
    Adapter->State = NwifiStateRunning;
    NdisReleaseSpinLock(&Adapter->DataLock);

    /* Re-assert the last known link state to TCP/IP. */
    NwifiIndicateLinkState(Adapter, Adapter->MediaConnected);

    return NDIS_STATUS_SUCCESS;
}

/* ===========================================================================
 *  Miscellaneous miniport handlers
 * ===========================================================================
 */
VOID
NTAPI
NwifiMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    Adapter->State = NwifiStateHalting;
    /* Best-effort quiesce; the machine is going down. */
}

VOID
NTAPI
NwifiMiniportDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

VOID
NTAPI
NwifiMiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
    /* Sends are forwarded straight down; nothing queued to cancel by id. */
}
