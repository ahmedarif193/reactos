/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Lower (protocol) edge: bind/unbind, OID requests, indications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"

#define NDEBUG
#include <debug.h>

/* ===========================================================================
 *  Synchronous request plumbing
 * ===========================================================================
 */

#define NWIFI_SYNC_OID_SIGNATURE 0x4F49574EUL /* "NWIO" */

typedef struct _NWIFI_SYNC_OID_CONTEXT
{
    ULONG Signature;
    NDIS_OID_REQUEST Request;
    NDIS_EVENT Event;
    NDIS_STATUS Status;
} NWIFI_SYNC_OID_CONTEXT, *PNWIFI_SYNC_OID_CONTEXT;

/* Synchronous OID round-trip: NdisOidRequest may pend, so wait on an event
 * signalled by the completion callback. */
NDIS_STATUS
NwifiProtocolDoRequest(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ NDIS_REQUEST_TYPE RequestType,
    _In_ NDIS_OID Oid,
    _Inout_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesProcessed)
{
    NWIFI_SYNC_OID_CONTEXT Context;
    PNDIS_OID_REQUEST Request = &Context.Request;
    NDIS_STATUS Status;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Signature = NWIFI_SYNC_OID_SIGNATURE;
    Context.Status = NDIS_STATUS_PENDING;
    NdisInitializeEvent(&Context.Event);

    Request->Header.Type = NDIS_OBJECT_TYPE_OID_REQUEST;
    Request->Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    Request->Header.Size = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
    Request->RequestType = RequestType;

    if (RequestType == NdisRequestSetInformation)
    {
        Request->DATA.SET_INFORMATION.Oid = Oid;
        Request->DATA.SET_INFORMATION.InformationBuffer = Buffer;
        Request->DATA.SET_INFORMATION.InformationBufferLength = Length;
    }
    else
    {
        Request->DATA.QUERY_INFORMATION.Oid = Oid;
        Request->DATA.QUERY_INFORMATION.InformationBuffer = Buffer;
        Request->DATA.QUERY_INFORMATION.InformationBufferLength = Length;
    }

    /* Completion routes only to this request's context and event. */
    Request->RequestId = &Context;

    Status = NdisOidRequest(Adapter->BindingHandle, Request);
    if (Status == NDIS_STATUS_PENDING)
    {
        /* NdisWaitEvent is alertable in this tree. Do not let an APC return
         * while the lower miniport still owns the stack request. */
        while (!NdisWaitEvent(&Context.Event, 0))
        {
            /* Wait until this request's completion signals its own event. */
        }
        Status = Context.Status;
    }

    if (BytesProcessed != NULL)
    {
        *BytesProcessed = (RequestType == NdisRequestSetInformation) ? Request->DATA.SET_INFORMATION.BytesRead : Request->DATA.QUERY_INFORMATION.BytesWritten;
    }

    return Status;
}

VOID
NTAPI
NwifiProtocolOidRequestComplete(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)ProtocolBindingContext;
    PNWIFI_SYNC_OID_CONTEXT Context;

    Context = CONTAINING_RECORD(OidRequest, NWIFI_SYNC_OID_CONTEXT, Request);
    if (OidRequest->RequestId == (PVOID)Context && Context->Signature == NWIFI_SYNC_OID_SIGNATURE)
    {
        Context->Status = Status;
        NdisSetEvent(&Context->Event);
        return;
    }

    /* Asynchronous OID completions belong to the MSM. */
    if (Adapter->MsmContext != NULL)
    {
        NwifiMsmOidComplete(Adapter, OidRequest, Status);
        return;
    }

    DPRINT("NWIFI: stray OID completion 0x%08X\n", Status);
}

/* ===========================================================================
 *  Open / close completion
 * ===========================================================================
 */
VOID
NTAPI
NwifiOpenAdapterCompleteEx(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ NDIS_STATUS Status)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)ProtocolBindingContext;

    Adapter->OpenStatus = Status;
    NdisSetEvent(&Adapter->OpenEvent);
}

VOID
NTAPI
NwifiCloseAdapterCompleteEx(
    _In_ NDIS_HANDLE ProtocolBindingContext)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)ProtocolBindingContext;

    NdisSetEvent(&Adapter->CloseEvent);
}

/* ===========================================================================
 *  Adapter context lifecycle
 * ===========================================================================
 */
static
VOID
NwifiDestroyAdapter(
    _In_ PNWIFI_ADAPTER Adapter)
{
    if (Adapter->TxNblPool != NULL)
    {
        NdisFreeNetBufferListPool(Adapter->TxNblPool);
    }
    if (Adapter->RxNblPool != NULL)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
    }
    if (Adapter->LowerDeviceNameStore.Buffer != NULL)
    {
        NwifiFree(Adapter->LowerDeviceNameStore.Buffer);
    }
    NdisFreeSpinLock(&Adapter->DataLock);
    NwifiFree(Adapter);
}

static
NDIS_STATUS
NwifiCreateNblPools(
    _In_ PNWIFI_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;

    RtlZeroMemory(&PoolParams, sizeof(PoolParams));
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(NET_BUFFER_LIST_POOL_PARAMETERS);
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.PoolTag = NWIFI_TAG;
    /* NDIS requires per-NBL context storage to use allocation alignment. */
    PoolParams.ContextSize = ALIGN_UP_BY(sizeof(NWIFI_NBL_CONTEXT),
                                         MEMORY_ALLOCATION_ALIGNMENT);

    /* TX pool: NBLs we build (802.11 frames) and send down to the dot11 NIC. */
    Adapter->TxNblPool = NdisAllocateNetBufferListPool(gNwifi.MiniportDriverHandle,
                                                       &PoolParams);
    if (Adapter->TxNblPool == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    /* RX pool: NBLs we build (802.3 frames) and indicate up to TCP/IP. */
    Adapter->RxNblPool = NdisAllocateNetBufferListPool(gNwifi.MiniportDriverHandle,
                                                       &PoolParams);
    if (Adapter->RxNblPool == NULL)
    {
        NdisFreeNetBufferListPool(Adapter->TxNblPool);
        Adapter->TxNblPool = NULL;
        return NDIS_STATUS_RESOURCES;
    }

    return NDIS_STATUS_SUCCESS;
}

/* Open the lower dot11 miniport, requesting the Native-802.11 medium.
 * Blocks on the open-complete event. */
static
NDIS_STATUS
NwifiOpenLowerAdapter(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ NDIS_HANDLE BindContext,
    _In_ PNDIS_BIND_PARAMETERS BindParameters)
{
    NDIS_OPEN_PARAMETERS OpenParameters;
    NDIS_MEDIUM MediumArray[1] = { NdisMediumNative802_11 };
    UINT SelectedMedium = 0;
    NDIS_STATUS Status;

    RtlZeroMemory(&OpenParameters, sizeof(OpenParameters));
    OpenParameters.Header.Type = NDIS_OBJECT_TYPE_OPEN_PARAMETERS;
    OpenParameters.Header.Revision = NDIS_OPEN_PARAMETERS_REVISION_1;
    OpenParameters.Header.Size = sizeof(NDIS_OPEN_PARAMETERS);
    OpenParameters.AdapterName = BindParameters->AdapterName;
    OpenParameters.MediumArray = MediumArray;
    OpenParameters.MediumArraySize = ARRAYSIZE(MediumArray);
    OpenParameters.SelectedMediumIndex = &SelectedMedium;
    OpenParameters.FrameTypeArray = NULL;
    OpenParameters.FrameTypeArraySize = 0;

    Adapter->OpenStatus = NDIS_STATUS_PENDING;
    NdisResetEvent(&Adapter->OpenEvent);

    Status = NdisOpenAdapterEx(gNwifi.ProtocolHandle,
                               (NDIS_HANDLE)Adapter,
                               &OpenParameters,
                               BindContext,
                               &Adapter->BindingHandle);
    if (Status == NDIS_STATUS_PENDING)
    {
        while (!NdisWaitEvent(&Adapter->OpenEvent, 0))
        {
            /* Retry an alertable wait until open completion. */
        }
        Status = Adapter->OpenStatus;
    }

    return Status;
}

/* ===========================================================================
 *  BindAdapterEx
 * ===========================================================================
 */
NDIS_STATUS
NTAPI
NwifiBindAdapterEx(
    _In_ NDIS_HANDLE ProtocolDriverContext,
    _In_ NDIS_HANDLE BindContext,
    _In_ PNDIS_BIND_PARAMETERS BindParameters)
{
    PNWIFI_ADAPTER Adapter;
    NDIS_STATUS Status;
    ULONG NameBytes;
    ULONG ExtStaOpMode;

    UNREFERENCED_PARAMETER(ProtocolDriverContext);

    DPRINT1("NWIFI: BindAdapterEx '%wZ'\n", BindParameters->AdapterName);

    /* Only bind Native-802.11 miniports; ignore anything else. */
    if (BindParameters->MediaType != NdisMediumNative802_11)
    {
        DPRINT1("NWIFI: skipping non-dot11 medium %d\n", BindParameters->MediaType);
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    Adapter = (PNWIFI_ADAPTER)NwifiAllocate(sizeof(NWIFI_ADAPTER));
    if (Adapter == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    InitializeListHead(&Adapter->Link);
    NdisAllocateSpinLock(&Adapter->DataLock);
    NdisInitializeEvent(&Adapter->OpenEvent);
    NdisInitializeEvent(&Adapter->CloseEvent);
    Adapter->State = NwifiStateInitializing;
    Adapter->InterfaceIndex = (ULONG)InterlockedIncrement(&gNwifi.NextInterfaceIndex) - 1;

    /* Copy the lower adapter device name so we keep it past this callback. */
    if (BindParameters->AdapterName != NULL &&
        BindParameters->AdapterName->Buffer != NULL)
    {
        NameBytes = BindParameters->AdapterName->Length;
        Adapter->LowerDeviceNameStore.Buffer = (PWCH)NwifiAllocate(NameBytes + sizeof(WCHAR));
        if (Adapter->LowerDeviceNameStore.Buffer == NULL)
        {
            Status = NDIS_STATUS_RESOURCES;
            goto FailEarly;
        }
        RtlCopyMemory(Adapter->LowerDeviceNameStore.Buffer,
                      BindParameters->AdapterName->Buffer, NameBytes);
        Adapter->LowerDeviceNameStore.Length = (USHORT)NameBytes;
        Adapter->LowerDeviceNameStore.MaximumLength = (USHORT)(NameBytes + sizeof(WCHAR));
        Adapter->LowerDeviceName = &Adapter->LowerDeviceNameStore;
    }

    Status = NwifiCreateNblPools(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        goto FailEarly;
    }

    Status = NwifiOpenLowerAdapter(Adapter, BindContext, BindParameters);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: NdisOpenAdapterEx failed 0x%08X\n", Status);
        goto FailEarly;
    }

    /* The bind parameters carry CurrentMacAddress, but re-query
     * OID_DOT11_MAC_ADDRESS so the upper miniport reports what the radio uses. */
    RtlCopyMemory(Adapter->MacAddress,
                  BindParameters->CurrentMacAddress, IEEE80211_ADDR_LEN);
    {
        DOT11_MAC_ADDRESS QueriedMac;
        ULONG Got = 0;
        Status = NwifiProtocolDoRequest(Adapter, NdisRequestQueryInformation,
                                        OID_DOT11_MAC_ADDRESS,
                                        &QueriedMac, sizeof(QueriedMac), &Got);
        if (Status == NDIS_STATUS_SUCCESS && Got >= IEEE80211_ADDR_LEN)
        {
            RtlCopyMemory(Adapter->MacAddress, QueriedMac, IEEE80211_ADDR_LEN);
        }
        /* Non-fatal: fall back to the bind-parameters MAC. */
    }

    /* A Native-802.11 miniport has no operating mode until one is selected;
     * ExtSTA must be set before the data path works. */
    {
        DOT11_CURRENT_OPERATION_MODE OpMode;
        RtlZeroMemory(&OpMode, sizeof(OpMode));
        OpMode.uCurrentOpMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
        ExtStaOpMode = 0;
        Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                        OID_DOT11_CURRENT_OPERATION_MODE,
                                        &OpMode, sizeof(OpMode), &ExtStaOpMode);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DPRINT1("NWIFI: set ExtSTA op-mode failed 0x%08X (continuing)\n", Status);
        }
    }

    /* Program the lower Native-802.11 receive edge once during bind. The
     * upper Ethernet packet filter is enforced in nwifi after 802.11-to-802.3
     * translation; forwarding each upper OID synchronously would block an OID
     * callback on arbitrary lower-driver work. */
    {
        ULONG Dot11PacketFilter =
            DOT11_PACKET_TYPE_DIRECTED_DATA |
            DOT11_PACKET_TYPE_MULTICAST_DATA |
            DOT11_PACKET_TYPE_ALL_MULTICAST_DATA |
            DOT11_PACKET_TYPE_BROADCAST_DATA;
        ULONG BytesRead = 0;

        Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                        OID_DOT11_CURRENT_PACKET_FILTER,
                                        &Dot11PacketFilter,
                                        sizeof(Dot11PacketFilter),
                                        &BytesRead);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DPRINT1("NWIFI: set lower packet filter failed 0x%08X\n", Status);
            goto FailAfterOpen;
        }
    }

    /* Create the MSM before publishing the adapter so control-channel callers
     * and indication paths always see it. */
    Status = NwifiMsmCreate(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: NwifiMsmCreate failed 0x%08X\n", Status);
        goto FailAfterOpen;
    }

    /* Publish the adapter before creating the upper miniport, so the miniport
     * InitializeEx callback can find it. */
    NdisAcquireSpinLock(&gNwifi.AdapterLock);
    InsertTailList(&gNwifi.AdapterList, &Adapter->Link);
    NdisReleaseSpinLock(&gNwifi.AdapterLock);

    /* Create the paired upper virtual miniport; NDIS responds by calling
     * NwifiMiniportInitializeEx, which recovers this NWIFI_ADAPTER via
     * NdisIMGetDeviceContext. */
    {
        /*
         * The upper miniport needs a device name distinct from the lower
         * adapter's FDO.  A fixed GUID name lets the registry pre-bind tcpip
         * to it (one instance; another dot11 NIC would need a distinct GUID).
         */
        static const WCHAR NwifiUpperName[] =
            L"\\Device\\{B1D2C3E4-5F6A-4B8C-9D0E-02524F535731}";
        NDIS_STRING UpperName;
        RtlInitUnicodeString(&UpperName, NwifiUpperName);
        Status = NdisIMInitializeDeviceInstanceEx(gNwifi.MiniportDriverHandle,
                                                  &UpperName,
                                                  (NDIS_HANDLE)Adapter);
    }
    if (Status != NDIS_STATUS_SUCCESS)
    {
        /* Non-fatal: the lower binding and MSM stay up for the control path
         * (enumerate/scan/BSS lists); only the 802.3 data path is offline
         * while NdisIMInitializeDeviceInstanceEx is unimplemented. */
        DPRINT1("NWIFI: NdisIMInitializeDeviceInstanceEx failed 0x%08X - "
                "control/scan path only, no 802.3 data path yet\n", Status);
        Status = NDIS_STATUS_SUCCESS;
    }

    DPRINT1("NWIFI: bound interface %u MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            Adapter->InterfaceIndex,
            Adapter->MacAddress[0], Adapter->MacAddress[1], Adapter->MacAddress[2],
            Adapter->MacAddress[3], Adapter->MacAddress[4], Adapter->MacAddress[5]);

    /* Scans are driven by the consumer via IOCTL_NWIFI_SCAN; the bind
     * callback must not block on an asynchronous scan request. */
    NwifiMsmInterfaceArrival(Adapter);
    return NDIS_STATUS_SUCCESS;

FailAfterOpen:
    /* Unpublish (if published) and close the lower adapter. */
    NdisAcquireSpinLock(&gNwifi.AdapterLock);
    if (!IsListEmpty(&Adapter->Link) && Adapter->Link.Flink != &Adapter->Link)
    {
        RemoveEntryList(&Adapter->Link);
        InitializeListHead(&Adapter->Link);
    }
    NdisReleaseSpinLock(&gNwifi.AdapterLock);

    if (Adapter->MsmContext != NULL)
    {
        NwifiMsmDestroy(Adapter);
    }

    NdisResetEvent(&Adapter->CloseEvent);
    if (NdisCloseAdapterEx(Adapter->BindingHandle) == NDIS_STATUS_PENDING)
    {
        while (!NdisWaitEvent(&Adapter->CloseEvent, 0))
        {
            /* Retry an alertable wait until close completion. */
        }
    }
    Adapter->BindingHandle = NULL;

FailEarly:
    NwifiDestroyAdapter(Adapter);
    return Status;
}

/* ===========================================================================
 *  UnbindAdapterEx
 * ===========================================================================
 */
NDIS_STATUS
NTAPI
NwifiUnbindAdapterEx(
    _In_ NDIS_HANDLE UnbindContext,
    _In_ NDIS_HANDLE ProtocolBindingContext)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)ProtocolBindingContext;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(UnbindContext);

    DPRINT1("NWIFI: UnbindAdapterEx interface %u\n", Adapter->InterfaceIndex);

    Adapter->State = NwifiStateHalting;

    /* Tear the upper miniport down first: NdisIMDeInitializeDeviceInstance
     * drives NwifiMiniportHaltEx, which drains the data path before the
     * lower binding is closed. */
    if (Adapter->MiniportInitialized && Adapter->MiniportAdapterHandle != NULL)
    {
        Status = NdisIMDeInitializeDeviceInstance(Adapter->MiniportAdapterHandle);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DPRINT1("NWIFI: IMDeInitialize failed 0x%08X\n", Status);
        }
    }

    /* Notify the control channel, then tear the MSM down (stops any
     * supplicant session). */
    NwifiMsmInterfaceRemoval(Adapter);
    NwifiMsmDestroy(Adapter);

    NdisAcquireSpinLock(&gNwifi.AdapterLock);
    RemoveEntryList(&Adapter->Link);
    NdisReleaseSpinLock(&gNwifi.AdapterLock);

    NdisResetEvent(&Adapter->CloseEvent);
    Status = NdisCloseAdapterEx(Adapter->BindingHandle);
    if (Status == NDIS_STATUS_PENDING)
    {
        while (!NdisWaitEvent(&Adapter->CloseEvent, 0))
        {
            /* Retry an alertable wait until close completion. */
        }
        Status = NDIS_STATUS_SUCCESS;
    }
    Adapter->BindingHandle = NULL;

    NwifiDestroyAdapter(Adapter);
    return NDIS_STATUS_SUCCESS;
}

/* ===========================================================================
 *  SetOptions / NetPnPEvent / Uninstall
 * ===========================================================================
 */
NDIS_STATUS
NTAPI
NwifiProtocolSetOptions(
    _In_ NDIS_HANDLE NdisDriverHandle,
    _In_ NDIS_HANDLE DriverContext)
{
    UNREFERENCED_PARAMETER(NdisDriverHandle);
    UNREFERENCED_PARAMETER(DriverContext);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NwifiProtocolNetPnPEvent(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)ProtocolBindingContext;
    PNET_PNP_EVENT_NOTIFICATION Notification = NetPnPEventNotification;

    if (Adapter == NULL || Notification == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    switch (Notification->NetPnPEvent.NetEvent)
    {
        case NetEventPause:
            NdisAcquireSpinLock(&Adapter->DataLock);
            if (Adapter->State != NwifiStateHalting)
                Adapter->State = NwifiStatePaused;
            NdisReleaseSpinLock(&Adapter->DataLock);
            return NDIS_STATUS_SUCCESS;

        case NetEventRestart:
            NdisAcquireSpinLock(&Adapter->DataLock);
            if (Adapter->State != NwifiStateHalting &&
                Adapter->MiniportInitialized)
            {
                Adapter->State = NwifiStateRunning;
            }
            NdisReleaseSpinLock(&Adapter->DataLock);
            return NDIS_STATUS_SUCCESS;

        case NetEventSetPower:
            if (Notification->NetPnPEvent.Buffer == NULL ||
                Notification->NetPnPEvent.BufferLength <
                    sizeof(NDIS_DEVICE_POWER_STATE))
            {
                return NDIS_STATUS_INVALID_PARAMETER;
            }
            return NDIS_STATUS_SUCCESS;

        case NetEventQueryPower:
        case NetEventQueryRemoveDevice:
        case NetEventCancelRemoveDevice:
        case NetEventReconfigure:
        case NetEventBindList:
        case NetEventBindsComplete:
            return NDIS_STATUS_SUCCESS;
        default:
            DPRINT("NWIFI: unhandled NetPnPEvent %d\n",
                   Notification->NetPnPEvent.NetEvent);
            return NDIS_STATUS_SUCCESS;
    }
}

VOID
NTAPI
NwifiProtocolUninstall(VOID)
{
    /* Nothing to free. */
}

/* ===========================================================================
 *  Status indications from the lower dot11 miniport
 * ===========================================================================
 */
VOID
NTAPI
NwifiProtocolStatusEx(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)ProtocolBindingContext;

    if (Adapter == NULL || StatusIndication == NULL)
    {
        return;
    }

    switch (StatusIndication->StatusCode)
    {
        case NDIS_STATUS_LINK_STATE:
            /* Cache the lower PHY rate, but do not mirror lower association
             * directly to the upper Ethernet edge.  For a secured network the
             * MSM must keep TCP/IP down until port authorization/keys complete. */
            if (StatusIndication->StatusBuffer != NULL &&
                StatusIndication->StatusBufferSize >= sizeof(NDIS_LINK_STATE))
            {
                PNDIS_LINK_STATE LinkState =
                    (PNDIS_LINK_STATE)StatusIndication->StatusBuffer;
                BOOLEAN Connected =
                    (LinkState->MediaConnectState == MediaConnectStateConnected);
                BOOLEAN UpperConnected;
                ULONG64 SpeedBps = 0;
                PNWIFI_MSM Msm = (PNWIFI_MSM)Adapter->MsmContext;

                if (Connected)
                {
                    SpeedBps = max(LinkState->XmitLinkSpeed,
                                   LinkState->RcvLinkSpeed);
                    if (SpeedBps == 0 || SpeedBps == NDIS_LINK_SPEED_UNKNOWN)
                    {
                        SpeedBps = NWIFI_DEFAULT_LINK_SPEED;
                    }
                }
                NdisAcquireSpinLock(&Adapter->DataLock);
                Adapter->LinkSpeedBps = SpeedBps;
                UpperConnected = Adapter->MediaConnected;
                NdisReleaseSpinLock(&Adapter->DataLock);

                if (Msm != NULL)
                {
                    NdisAcquireSpinLock(&Msm->Lock);
                    Msm->LinkSpeedKbps =
                        (SpeedBps / 1000 > MAXULONG)
                            ? MAXULONG : (ULONG)(SpeedBps / 1000);
                    NdisReleaseSpinLock(&Msm->Lock);
                }
                if (Connected && UpperConnected)
                {
                    /* The secure link was already authorized; publish the
                     * newly learned rate as a link-state update. */
                    NwifiIndicateLinkState(Adapter, TRUE);
                }
            }
            break;

        /* dot11 indications drive the station state machine (msm.c). */
        case NDIS_STATUS_DOT11_SCAN_CONFIRM:
        case NDIS_STATUS_DOT11_ASSOCIATION_START:
        case NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION:
        case NDIS_STATUS_DOT11_CONNECTION_START:
        case NDIS_STATUS_DOT11_CONNECTION_COMPLETION:
        case NDIS_STATUS_DOT11_ROAMING_START:
        case NDIS_STATUS_DOT11_ROAMING_COMPLETION:
        case NDIS_STATUS_DOT11_DISASSOCIATION:
        case NDIS_STATUS_DOT11_LINK_QUALITY:
            NwifiMsmIndicateStatus(Adapter, StatusIndication);
            break;

        case NDIS_STATUS_DOT11_PHY_STATE_CHANGED:
            DPRINT("NWIFI: dot11 PHY state changed\n");
            break;

        default:
            DPRINT("NWIFI: status 0x%08X\n", StatusIndication->StatusCode);
            break;
    }
}

/* ===========================================================================
 *  Data-path entry points (thin shims into datapath.c)
 * ===========================================================================
 */
VOID
NTAPI
NwifiProtocolReceiveNbl(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG NumberOfNetBufferLists,
    _In_ ULONG ReceiveFlags)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)ProtocolBindingContext;

    UNREFERENCED_PARAMETER(PortNumber);

    NwifiReceiveFromLower(Adapter, NetBufferLists,
                          NumberOfNetBufferLists, ReceiveFlags);
}

VOID
NTAPI
NwifiProtocolSendNblComplete(
    _In_ NDIS_HANDLE ProtocolBindingContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG SendCompleteFlags)
{
    PNWIFI_ADAPTER Adapter = (PNWIFI_ADAPTER)ProtocolBindingContext;

    NwifiSendCompleteFromLower(Adapter, NetBufferLists, SendCompleteFlags);
}
