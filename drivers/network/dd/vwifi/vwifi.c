/*
 * PROJECT:     ReactOS Virtual Native 802.11 (dot11) Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver entry and NDIS 6.20 miniport lifecycle
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "vwifi.h"
#include <debug.h>

extern NDIS_OID VWifiSupportedOids[];
extern ULONG VWifiSupportedOidCount;

static NDIS_HANDLE gMiniportDriverHandle = NULL;

PVOID
VWifiAllocate(
    IN ULONG Size)
{
    PVOID Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, VWIFI_TAG);
    if (Buffer)
    {
        RtlZeroMemory(Buffer, Size);
    }
    return Buffer;
}

VOID
VWifiFree(
    IN PVOID Buffer)
{
    if (Buffer)
    {
        ExFreePoolWithTag(Buffer, VWIFI_TAG);
    }
}

static
NDIS_STATUS
VWifiSetMiniportAttributes(
    IN PVWIFI_ADAPTER Adapter)
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
        DPRINT1("VWIFI: SetMiniportAttributes(registration) failed 0x%08X\n", Status);
        return Status;
    }

    RtlZeroMemory(&GenAttr, sizeof(GenAttr));
    GenAttr.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GenAttr.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttr.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES);

    /* nwifi.sys binds only to miniports advertising native 802.11 media. */
    GenAttr.MediaType = NdisMediumNative802_11;
    GenAttr.PhysicalMediumType = NdisPhysicalMediumNative802_11;
    GenAttr.MtuSize = VWIFI_MTU_SIZE;
    GenAttr.MaxXmitLinkSpeed = VWIFI_LINK_SPEED_BPS;
    GenAttr.MaxRcvLinkSpeed = VWIFI_LINK_SPEED_BPS;
    GenAttr.XmitLinkSpeed = VWIFI_LINK_SPEED_BPS;
    GenAttr.RcvLinkSpeed = VWIFI_LINK_SPEED_BPS;
    GenAttr.MediaConnectState = MediaConnectStateDisconnected;
    GenAttr.MediaDuplexState = MediaDuplexStateFull;
    GenAttr.LookaheadSize = VWIFI_MAX_FRAME_SIZE;
    GenAttr.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    GenAttr.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST;
    GenAttr.MaxMulticastListSize = 32;
    GenAttr.MacAddressLength = VWIFI_ADDRESS_LENGTH;
    RtlCopyMemory(GenAttr.PermanentMacAddress, Adapter->PermanentAddress,
                  VWIFI_ADDRESS_LENGTH);
    RtlCopyMemory(GenAttr.CurrentMacAddress, Adapter->CurrentAddress,
                  VWIFI_ADDRESS_LENGTH);
    GenAttr.RecvScaleCapabilities = NULL;
    GenAttr.AccessType = NET_IF_ACCESS_BROADCAST;
    GenAttr.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GenAttr.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    GenAttr.IfType = IF_TYPE_IEEE80211;
    GenAttr.IfConnectorPresent = TRUE;
    GenAttr.SupportedStatistics = 0;
    GenAttr.SupportedOidList = VWifiSupportedOids;
    GenAttr.SupportedOidListLength = VWifiSupportedOidCount * sizeof(NDIS_OID);

    Status = NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&GenAttr);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("VWIFI: SetMiniportAttributes(general) failed 0x%08X\n", Status);
    }
    return Status;
}

NDIS_STATUS
NTAPI
VWifiMiniportInitializeEx(
    IN NDIS_HANDLE NdisMiniportHandle,
    IN NDIS_HANDLE MiniportDriverContext,
    IN PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PVWIFI_ADAPTER Adapter;
    NDIS_STATUS Status;
    NDIS_TIMER_CHARACTERISTICS TimerChars;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    DPRINT1("VWIFI: MiniportInitializeEx (virtual radio)\n");

    Adapter = VWifiAllocate(sizeof(VWIFI_ADAPTER));
    if (!Adapter)
    {
        return NDIS_STATUS_RESOURCES;
    }

    Adapter->MiniportAdapterHandle = NdisMiniportHandle;
    NdisAllocateSpinLock(&Adapter->Lock);

    Adapter->PermanentAddress[0] = VWIFI_PERMANENT_MAC_0;
    Adapter->PermanentAddress[1] = VWIFI_PERMANENT_MAC_1;
    Adapter->PermanentAddress[2] = VWIFI_PERMANENT_MAC_2;
    Adapter->PermanentAddress[3] = VWIFI_PERMANENT_MAC_3;
    Adapter->PermanentAddress[4] = VWIFI_PERMANENT_MAC_4;
    Adapter->PermanentAddress[5] = VWIFI_PERMANENT_MAC_5;
    RtlCopyMemory(Adapter->CurrentAddress, Adapter->PermanentAddress,
                  VWIFI_ADDRESS_LENGTH);

    /* Defaults: an extensible STA on the ERP (802.11g) PHY. */
    Adapter->Dot11.CurrentOperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
    Adapter->Dot11.CurrentPhyType = dot11_phy_type_erp;
    Adapter->Dot11.RadioOn = TRUE;
    Adapter->Dot11.AutoConfigEnabled = TRUE;
    Adapter->Dot11.CurrentChannel = 1;
    Adapter->Dot11.BeaconPeriod = VWIFI_BEACON_PERIOD;
    Adapter->Dot11.RtsThreshold = 2347;
    Adapter->Dot11.FragmentationThreshold = 2346;
    Adapter->Dot11.DesiredBssType = dot11_BSS_type_infrastructure;
    Adapter->Dot11.AuthAlgorithm = DOT11_AUTH_ALGO_80211_OPEN;
    Adapter->Dot11.UnicastCipher = DOT11_CIPHER_ALGO_NONE;
    Adapter->Dot11.MulticastCipher = DOT11_CIPHER_ALGO_NONE;

    Adapter->ConnState = VWifiDisconnected;
    Adapter->PendingJob = VWifiJobNone;

    VWifiInitFakeBssList(Adapter);

    Status = VWifiSetMiniportAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        goto Fail;
    }

    /* Async engine: one-shot timer (DPC) + I/O work item; the work item
     * produces the dot11 indications at PASSIVE_LEVEL. */
    RtlZeroMemory(&TimerChars, sizeof(TimerChars));
    TimerChars.Header.Type = NDIS_OBJECT_TYPE_TIMER_CHARACTERISTICS;
    TimerChars.Header.Revision = NDIS_TIMER_CHARACTERISTICS_REVISION_1;
    TimerChars.Header.Size = NDIS_SIZEOF_TIMER_CHARACTERISTICS_REVISION_1;
    TimerChars.TimerFunction = VWifiEngineTimerDpc;
    TimerChars.FunctionContext = Adapter;
    Status = NdisAllocateTimerObject(NdisMiniportHandle, &TimerChars,
                                     &Adapter->EngineTimer);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        goto Fail;
    }

    Adapter->EngineWorkItem = NdisAllocateIoWorkItem(NdisMiniportHandle);
    if (!Adapter->EngineWorkItem)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    DPRINT1("VWIFI: MiniportInitializeEx succeeded\n");
    return NDIS_STATUS_SUCCESS;

Fail:
    if (Adapter->EngineWorkItem)
    {
        NdisFreeIoWorkItem(Adapter->EngineWorkItem);
    }
    if (Adapter->EngineTimer)
    {
        NdisFreeTimerObject(Adapter->EngineTimer);
    }
    NdisFreeSpinLock(&Adapter->Lock);
    VWifiFree(Adapter);
    return Status;
}

VOID
NTAPI
VWifiMiniportHaltEx(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_HALT_ACTION HaltAction)
{
    PVWIFI_ADAPTER Adapter = (PVWIFI_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    DPRINT1("VWIFI: MiniportHaltEx\n");

    Adapter->Halting = TRUE;
    Adapter->DataPathRunning = FALSE;

    /* Stop the async engine and wait for any in-flight work item to drain. */
    if (Adapter->EngineTimer)
    {
        NdisCancelTimerObject(Adapter->EngineTimer);
    }
    while (InterlockedCompareExchange(&Adapter->WorkPending, 0, 0) != 0)
    {
        NdisMSleep(1000);
    }

    if (Adapter->EngineWorkItem)
    {
        NdisFreeIoWorkItem(Adapter->EngineWorkItem);
        Adapter->EngineWorkItem = NULL;
    }
    if (Adapter->EngineTimer)
    {
        NdisFreeTimerObject(Adapter->EngineTimer);
        Adapter->EngineTimer = NULL;
    }

    NdisFreeSpinLock(&Adapter->Lock);
    VWifiFree(Adapter);
}

NDIS_STATUS
NTAPI
VWifiMiniportPauseEx(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_MINIPORT_PAUSE_PARAMETERS MiniportPauseParameters)
{
    PVWIFI_ADAPTER Adapter = (PVWIFI_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(MiniportPauseParameters);

    DPRINT1("VWIFI: MiniportPauseEx\n");

    Adapter->DataPathRunning = FALSE;

    /* Cancel the timer and wait for any running work item to finish. */
    if (Adapter->EngineTimer)
    {
        NdisCancelTimerObject(Adapter->EngineTimer);
    }
    while (InterlockedCompareExchange(&Adapter->WorkPending, 0, 0) != 0)
    {
        NdisMSleep(1000);
    }

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
VWifiMiniportRestartEx(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_MINIPORT_RESTART_PARAMETERS MiniportRestartParameters)
{
    PVWIFI_ADAPTER Adapter = (PVWIFI_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(MiniportRestartParameters);

    DPRINT1("VWIFI: MiniportRestartEx\n");

    Adapter->DataPathRunning = TRUE;

    VWifiIndicateLinkState(Adapter, Adapter->ConnState == VWifiConnected);

    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
VWifiMiniportShutdownEx(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PVWIFI_ADAPTER Adapter = (PVWIFI_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ShutdownAction);

    Adapter->DataPathRunning = FALSE;
}

VOID
NTAPI
VWifiMiniportDevicePnpEventNotify(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

VOID
NTAPI
VWifiMiniportUnload(
    IN PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    DPRINT1("VWIFI: DriverUnload\n");

    if (gMiniportDriverHandle)
    {
        NdisMDeregisterMiniportDriver(gMiniportDriverHandle);
        gMiniportDriverHandle = NULL;
    }
}

NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Chars;
    NDIS_STATUS Status;

    DPRINT1("VWIFI: DriverEntry\n");

    RtlZeroMemory(&Chars, sizeof(Chars));
    Chars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Chars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);
    Chars.MajorNdisVersion = 6;
    Chars.MinorNdisVersion = 20;
    Chars.MajorDriverVersion = 1;
    Chars.MinorDriverVersion = 0;

    Chars.InitializeHandlerEx = VWifiMiniportInitializeEx;
    Chars.HaltHandlerEx = VWifiMiniportHaltEx;
    Chars.UnloadHandler = VWifiMiniportUnload;
    Chars.PauseHandler = VWifiMiniportPauseEx;
    Chars.RestartHandler = VWifiMiniportRestartEx;
    Chars.OidRequestHandler = VWifiMiniportOidRequest;
    Chars.SendNetBufferListsHandler = VWifiMiniportSendNetBufferLists;
    Chars.ReturnNetBufferListsHandler = VWifiMiniportReturnNetBufferLists;
    Chars.CancelSendHandler = VWifiMiniportCancelSend;
    Chars.CheckForHangHandlerEx = NULL;
    Chars.ResetHandlerEx = NULL;
    Chars.DevicePnPEventNotifyHandler = VWifiMiniportDevicePnpEventNotify;
    Chars.ShutdownHandlerEx = VWifiMiniportShutdownEx;
    Chars.CancelOidRequestHandler = VWifiMiniportCancelOidRequest;

    Status = NdisMRegisterMiniportDriver(DriverObject, RegistryPath, NULL,
                                         &Chars, &gMiniportDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("VWIFI: NdisMRegisterMiniportDriver failed 0x%08X\n", Status);
    }

    return Status;
}
