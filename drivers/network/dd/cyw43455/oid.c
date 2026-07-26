/*
 * PROJECT:     ReactOS Broadcom/Cypress CYW43455 Native 802.11 Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NDIS 6.20 dot11 OID handlers, scan flow and BSS list
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"

#define NDEBUG
#include <debug.h>

NDIS_OID CywSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_LINK_SPEED,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_MAXIMUM_SEND_PACKETS,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_STATISTICS,
    OID_DOT11_MPDU_MAX_LENGTH,
    OID_DOT11_OPERATION_MODE_CAPABILITY,
    OID_DOT11_CURRENT_OPERATION_MODE,
    OID_DOT11_CURRENT_PACKET_FILTER,
    OID_DOT11_CURRENT_PHY_TYPE,
    OID_DOT11_SUPPORTED_PHY_TYPES,
    OID_DOT11_NIC_POWER_STATE,
    OID_DOT11_HARDWARE_PHY_STATE,
    OID_DOT11_MAC_ADDRESS,
    OID_DOT11_PERMANENT_ADDRESS,
    OID_DOT11_CURRENT_ADDRESS,
    OID_DOT11_CURRENT_CHANNEL_NUMBER,
    OID_DOT11_AUTO_CONFIG_ENABLED,
    OID_DOT11_SCAN_REQUEST,
    OID_DOT11_ENUM_BSS_LIST,
    OID_DOT11_FLUSH_BSS_LIST,
    OID_DOT11_DESIRED_SSID_LIST,
    OID_DOT11_DESIRED_BSS_TYPE,
    OID_DOT11_DESIRED_BSSID_LIST,
    OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM,
    OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM,
    OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM,
    OID_DOT11_CIPHER_DEFAULT_KEY_ID,
    OID_DOT11_CIPHER_DEFAULT_KEY,
    OID_DOT11_CIPHER_KEY_MAPPING_KEY,
    OID_DOT11_CONNECT_REQUEST,
    OID_DOT11_DISCONNECT_REQUEST,
    OID_DOT11_RESET_REQUEST,
};

ULONG CywSupportedOidCount = sizeof(CywSupportedOids) / sizeof(CywSupportedOids[0]);

static
NDIS_STATUS
CywOidQueryBuffer(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ PVOID Data,
    _In_ ULONG Length)
{
    if (Request->DATA.QUERY_INFORMATION.InformationBufferLength < Length)
    {
        Request->DATA.QUERY_INFORMATION.BytesNeeded = Length;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }
    RtlCopyMemory(Request->DATA.QUERY_INFORMATION.InformationBuffer, Data, Length);
    Request->DATA.QUERY_INFORMATION.BytesWritten = Length;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
CywOidQueryUlong(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG Value)
{
    return CywOidQueryBuffer(Request, &Value, sizeof(ULONG));
}

static
ULONG
CywBuildBeaconIes(
    _In_ PCYW_BSS Bss,
    _Out_ PUCHAR Buffer,
    _In_ ULONG BufferLength)
{
    ULONG Offset = 0;

    if (BufferLength < 2 + Bss->SsidLength + 9)
    {
        return 0;
    }

    Buffer[Offset++] = 0x00;
    Buffer[Offset++] = (UCHAR)Bss->SsidLength;
    RtlCopyMemory(Buffer + Offset, Bss->Ssid, Bss->SsidLength);
    Offset += Bss->SsidLength;

    Buffer[Offset++] = 0x01;
    Buffer[Offset++] = 4;
    Buffer[Offset++] = 0x82;
    Buffer[Offset++] = 0x84;
    Buffer[Offset++] = 0x8B;
    Buffer[Offset++] = 0x96;

    Buffer[Offset++] = 0x03;
    Buffer[Offset++] = 1;
    Buffer[Offset++] = (UCHAR)Bss->ChannelNumber;

    return Offset;
}

ULONG
CywBuildBssList(
    _In_ PCYW_ADAPTER Adapter,
    _Out_ PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG BytesNeeded)
{
    ULONG IeLengths[CYW_MAX_BSS];
    ULONG i;
    ULONG PayloadSize = 0;
    ULONG TotalSize;
    PDOT11_BYTE_ARRAY ByteArray;
    PUCHAR Cursor;
    ULONG64 Now;
    ULONG Count;

    NdisAcquireSpinLock(&Adapter->Lock);
    Count = Adapter->BssCount;
    if (Count > CYW_MAX_BSS)
    {
        Count = CYW_MAX_BSS;
    }

    for (i = 0; i < Count; i++)
    {
        /* Real IEs captured from the scan; else the synthesized fallback of
         * SSID TLV (2 + len) + rates (6) + DS (3). */
        IeLengths[i] = Adapter->Bss[i].IeLength != 0
            ? Adapter->Bss[i].IeLength
            : Adapter->Bss[i].SsidLength + 11;
        PayloadSize += FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + IeLengths[i];
    }

    TotalSize = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + PayloadSize;
    *BytesNeeded = TotalSize;

    if (Buffer == NULL || BufferLength < TotalSize)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return 0;
    }

    ByteArray = (PDOT11_BYTE_ARRAY)Buffer;
    ByteArray->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    ByteArray->Header.Revision = DOT11_BYTE_ARRAY_REVISION_1;
    ByteArray->Header.Size = sizeof(DOT11_BYTE_ARRAY);
    ByteArray->uNumOfBytes = PayloadSize;
    ByteArray->uTotalNumOfBytes = PayloadSize;

    Now = (ULONG64)KeQueryInterruptTime();
    Cursor = ByteArray->ucBuffer;

    for (i = 0; i < Count; i++)
    {
        PCYW_BSS Src = &Adapter->Bss[i];
        PDOT11_BSS_ENTRY Entry = (PDOT11_BSS_ENTRY)Cursor;
        ULONG IeLen = IeLengths[i];

        RtlZeroMemory(Entry, FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer));
        Entry->uPhyId = 0;
        Entry->PhySpecificInfo.uChCenterFrequency =
            Src->ChCenterFrequency ? Src->ChCenterFrequency :
            (Src->ChannelNumber > 14 ? 5000 + Src->ChannelNumber * 5
                                     : 2407 + Src->ChannelNumber * 5);
        RtlCopyMemory(Entry->dot11BSSID, Src->Bssid, CYW_ADDRESS_LENGTH);
        Entry->dot11BSSType = Src->BssType;
        Entry->lRSSI = Src->Rssi;
        Entry->uLinkQuality = Src->LinkQuality;
        Entry->bInRegDomain = TRUE;
        Entry->usBeaconPeriod = Src->BeaconPeriod ? Src->BeaconPeriod : 100;
        Entry->ullTimestamp = Now;
        Entry->ullHostTimestamp = Now;
        Entry->usCapabilityInformation = Src->CapabilityInformation;
        Entry->uBufferLength = IeLen;

        if (Src->IeLength != 0)
        {
            RtlCopyMemory(Entry->ucBuffer, Src->IeBlob, IeLen);
        }
        else
        {
            (VOID)CywBuildBeaconIes(Src, Entry->ucBuffer, IeLen);
        }

        Cursor += FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + IeLen;
    }

    NdisReleaseSpinLock(&Adapter->Lock);
    return TotalSize;
}

static
VOID
CywIndicateDot11Status(
    _In_ PCYW_ADAPTER Adapter,
    _In_ NDIS_STATUS StatusCode,
    _In_ PVOID Buffer,
    _In_ ULONG BufferSize)
{
    NDIS_STATUS_INDICATION Indication;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(NDIS_STATUS_INDICATION);
    Indication.SourceHandle = Adapter->MiniportAdapterHandle;
    Indication.StatusCode = StatusCode;
    Indication.StatusBuffer = Buffer;
    Indication.StatusBufferSize = BufferSize;

    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);
}

VOID
CywIndicateAssocStart(
    _In_ PCYW_ADAPTER Adapter)
{
    DOT11_ASSOCIATION_START_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_ASSOCIATION_START_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(DOT11_ASSOCIATION_START_PARAMETERS);
    RtlCopyMemory(Params.MacAddr,
                  Adapter->HasDesiredBssid ? Adapter->DesiredBssid
                                           : Adapter->ConnectedBssid,
                  CYW_ADDRESS_LENGTH);
    Params.SSID.uSSIDLength = Adapter->DesiredSsidLength;
    RtlCopyMemory(Params.SSID.ucSSID, Adapter->DesiredSsid, Adapter->DesiredSsidLength);

    CywIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_ASSOCIATION_START,
                           &Params, sizeof(Params));
}

VOID
CywIndicateAssocComplete(
    _In_ PCYW_ADAPTER Adapter,
    _In_ DOT11_ASSOC_STATUS Status)
{
    DOT11_ASSOCIATION_COMPLETION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_ASSOCIATION_COMPLETION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(DOT11_ASSOCIATION_COMPLETION_PARAMETERS);
    RtlCopyMemory(Params.MacAddr, Adapter->ConnectedBssid, CYW_ADDRESS_LENGTH);
    Params.uStatus = Status;

    if (Status == DOT11_ASSOC_STATUS_SUCCESS)
    {
        Params.AuthAlgo = Adapter->AuthAlgorithm;
        Params.UnicastCipher = Adapter->UnicastCipher;
        Params.MulticastCipher = Adapter->MulticastCipher;
        Params.bPortAuthorized =
            (Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_80211_OPEN);
        Params.DSInfo = DOT11_DS_UNKNOWN;
    }

    CywIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION,
                           &Params, sizeof(Params));
}

VOID
CywIndicateConnectComplete(
    _In_ PCYW_ADAPTER Adapter,
    _In_ DOT11_ASSOC_STATUS Status)
{
    DOT11_CONNECTION_COMPLETION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_CONNECTION_COMPLETION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(DOT11_CONNECTION_COMPLETION_PARAMETERS);
    Params.uStatus = Status;

    CywIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_CONNECTION_COMPLETION,
                           &Params, sizeof(Params));
}

VOID
CywIndicateLinkState(
    _In_ PCYW_ADAPTER Adapter,
    _In_ BOOLEAN Connected,
    _In_ ULONG64 SpeedBps)
{
    NDIS_LINK_STATE LinkState;

    if (Connected && SpeedBps == 0)
    {
        SpeedBps = CYW_LINK_SPEED_BPS;
    }

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    LinkState.MediaConnectState =
        Connected ? MediaConnectStateConnected : MediaConnectStateDisconnected;
    LinkState.MediaDuplexState = MediaDuplexStateFull;
    LinkState.XmitLinkSpeed =
        Connected ? SpeedBps : NDIS_LINK_SPEED_UNKNOWN;
    LinkState.RcvLinkSpeed =
        Connected ? SpeedBps : NDIS_LINK_SPEED_UNKNOWN;

    CywIndicateDot11Status(Adapter, NDIS_STATUS_LINK_STATE,
                           &LinkState, sizeof(LinkState));
}

VOID
CywIndicateDisassociation(
    _In_ PCYW_ADAPTER Adapter)
{
    DOT11_DISASSOCIATION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_DISASSOCIATION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(DOT11_DISASSOCIATION_PARAMETERS);
    RtlCopyMemory(Params.MacAddr, Adapter->ConnectedBssid, CYW_ADDRESS_LENGTH);
    Params.uReason = DOT11_ASSOC_STATUS_UNREACHABLE;

    CywIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_DISASSOCIATION,
                           &Params, sizeof(Params));
}

VOID
CywIndicateLinkQuality(
    _In_ PCYW_ADAPTER Adapter)
{
    struct
    {
        DOT11_LINK_QUALITY_PARAMETERS Params;
        DOT11_LINK_QUALITY_ENTRY Entry;
    } Buf;
    LONG Rssi;

    if (!NT_SUCCESS(CywQueryRssi(Adapter, &Rssi)))
    {
        return;
    }

    RtlZeroMemory(&Buf, sizeof(Buf));
    Buf.Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Buf.Params.Header.Revision = DOT11_LINK_QUALITY_PARAMETERS_REVISION_1;
    Buf.Params.Header.Size = sizeof(DOT11_LINK_QUALITY_PARAMETERS);
    Buf.Params.uLinkQualityListSize = 1;
    Buf.Params.uLinkQualityListOffset = sizeof(DOT11_LINK_QUALITY_PARAMETERS);
    RtlCopyMemory(Buf.Entry.PeerMacAddr, Adapter->ConnectedBssid, CYW_ADDRESS_LENGTH);
    Buf.Entry.ucLinkQuality = (Rssi >= -50) ? 100 :
                              (Rssi <= -100) ? 0 : (UCHAR)(2 * (Rssi + 100));

    CywIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_LINK_QUALITY,
                           &Buf, sizeof(Buf));
}

VOID
CywCompletePendingConnect(
    _In_ PCYW_ADAPTER Adapter,
    _In_ NDIS_STATUS Status)
{
    PNDIS_OID_REQUEST Request;

    NdisAcquireSpinLock(&Adapter->Lock);
    Request = Adapter->PendingConnectOid;
    Adapter->PendingConnectOid = NULL;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (Request != NULL)
    {
        NdisMOidRequestComplete(Adapter->MiniportAdapterHandle, Request, Status);
    }
}

/* Runs at PASSIVE off the bus thread: fetches the live rate and RSSI and
 * indicates the connected link state and link quality. */
static
VOID
CywLinkUpWorker(
    _In_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)WorkItemContext;
    ULONG Rate = 0;
    ULONG64 SpeedBps = 0;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    if (Adapter->LinkUp && !Adapter->Halting)
    {
        if (NT_SUCCESS(CywQueryRate(Adapter, &Rate)) && Rate != 0)
        {
            SpeedBps = (ULONG64)Rate * 500000;  /* firmware units: 500 kbit/s */
        }
        CywIndicateLinkState(Adapter, TRUE, SpeedBps);
        CywIndicateLinkQuality(Adapter);
    }

    InterlockedDecrement(&Adapter->WorkItemsPending);
}

VOID
CywQueueLinkUpWork(
    _In_ PCYW_ADAPTER Adapter)
{
    if (Adapter->LinkWorkItem != NULL && !Adapter->Halting)
    {
        InterlockedIncrement(&Adapter->WorkItemsPending);
        NdisQueueIoWorkItem(Adapter->LinkWorkItem, CywLinkUpWorker, Adapter);
    }
}

VOID
CywIndicateScanComplete(
    _In_ PCYW_ADAPTER Adapter,
    _In_ NDIS_STATUS ScanStatus)
{
    DOT11_STATUS_INDICATION Confirm;
    PNDIS_OID_REQUEST Request;
    BOOLEAN WasInProgress;

    NdisAcquireSpinLock(&Adapter->Lock);
    WasInProgress = Adapter->ScanInProgress;
    Adapter->ScanInProgress = FALSE;
    Request = Adapter->PendingScanOid;
    Adapter->PendingScanOid = NULL;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (!WasInProgress)
    {
        return;
    }

    RtlZeroMemory(&Confirm, sizeof(Confirm));
    Confirm.uStatusType = DOT11_STATUS_SCAN_CONFIRM;
    Confirm.ndisStatus = ScanStatus;

    CywIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_SCAN_CONFIRM,
                           &Confirm, sizeof(Confirm));

    if (Request != NULL)
    {
        NdisMOidRequestComplete(Adapter->MiniportAdapterHandle, Request,
                                NDIS_STATUS_SUCCESS);
    }
}

/* Timeout fallback: the escan-complete event normally finishes the scan OID
 * well before this fires. The sequence check keeps a stale fallback from
 * completing a newer scan early. */
static
VOID
CywScanWorker(
    _In_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)WorkItemContext;
    ULONG Seq;
    LARGE_INTEGER Delay;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    for (;;)
    {
        ULONG Slice;

        Seq = Adapter->ScanSeq;

        for (Slice = 0; Slice < CYW_ESCAN_TIMEOUT_SLICES; Slice++)
        {
            Delay.QuadPart = CYW_ESCAN_TIMEOUT_SLICE;
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);

            if (Adapter->Halting || (Adapter->ScanSeq != Seq))
            {
                break;
            }
        }

        if (Adapter->Halting)
        {
            break;
        }

        if (Adapter->ScanSeq == Seq)
        {
            CywIndicateScanComplete(Adapter, NDIS_STATUS_SUCCESS);
            break;
        }

        if (Adapter->PendingScanOid == NULL)
        {
            break;
        }
    }

    InterlockedExchange(&Adapter->ScanWorkQueued, 0);
    InterlockedDecrement(&Adapter->WorkItemsPending);
}

static
NDIS_STATUS
CywOidScanRequest(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NTSTATUS Status;

    if (!Adapter->RadioOn)
    {
        return NDIS_STATUS_DOT11_POWER_STATE_INVALID;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->PendingScanOid != NULL)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return NDIS_STATUS_DOT11_MEDIA_IN_USE;
    }
    Adapter->PendingScanOid = Request;
    Adapter->ScanSeq++;
    NdisReleaseSpinLock(&Adapter->Lock);

    Status = CywScanStart(Adapter,
        (PDOT11_SCAN_REQUEST_V2)Request->DATA.SET_INFORMATION.InformationBuffer,
        Request->DATA.SET_INFORMATION.InformationBufferLength);
    if (!NT_SUCCESS(Status))
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->PendingScanOid = NULL;
        NdisReleaseSpinLock(&Adapter->Lock);
        return NDIS_STATUS_FAILURE;
    }

    if (InterlockedCompareExchange(&Adapter->ScanWorkQueued, 1, 0) == 0)
    {
        InterlockedIncrement(&Adapter->WorkItemsPending);
        NdisQueueIoWorkItem(Adapter->InterruptWorkItem, CywScanWorker, Adapter);
    }
    return NDIS_STATUS_PENDING;
}

/* Issues the connect; the link event completes the OID, this worker's delay
 * is only the timeout fallback (sequence-guarded against newer connects). */
static
VOID
CywConnectWorker(
    _In_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)WorkItemContext;
    ULONG Seq = Adapter->ConnectSeq;
    LARGE_INTEGER Delay;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    CywIndicateAssocStart(Adapter);
    CywConnect(Adapter);

    Delay.QuadPart = -30000000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    if (Adapter->ConnectSeq == Seq)
    {
        CywCompletePendingConnect(Adapter, NDIS_STATUS_SUCCESS);
    }

    InterlockedDecrement(&Adapter->WorkItemsPending);
}

static
NDIS_STATUS
CywOidConnectRequest(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    if (!Adapter->RadioOn)
    {
        return NDIS_STATUS_DOT11_POWER_STATE_INVALID;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->PendingConnectOid != NULL)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return NDIS_STATUS_DOT11_MEDIA_IN_USE;
    }
    Adapter->PendingConnectOid = Request;
    Adapter->ConnectSeq++;
    NdisReleaseSpinLock(&Adapter->Lock);

    InterlockedExchange(&Adapter->JoinRetries, 0);
    InterlockedIncrement(&Adapter->WorkItemsPending);
    NdisQueueIoWorkItem(Adapter->ConnectWorkItem, CywConnectWorker, Adapter);
    return NDIS_STATUS_PENDING;
}

VOID
CywQueueConnectWork(
    _In_ PCYW_ADAPTER Adapter)
{
    InterlockedIncrement(&Adapter->WorkItemsPending);
    NdisQueueIoWorkItem(Adapter->ConnectWorkItem, CywConnectWorker, Adapter);
}

static
NDIS_STATUS
CywOidQuery(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS_OID Oid = Request->DATA.QUERY_INFORMATION.Oid;
    ULONG BytesNeeded;

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return CywOidQueryBuffer(Request, CywSupportedOids,
                                     CywSupportedOidCount * sizeof(NDIS_OID));
        case OID_GEN_HARDWARE_STATUS:
            return CywOidQueryUlong(Request, NdisHardwareStatusReady);
        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            return CywOidQueryUlong(Request, NdisMediumNative802_11);
        case OID_GEN_PHYSICAL_MEDIUM:
            return CywOidQueryUlong(Request, NdisPhysicalMediumNative802_11);
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            return CywOidQueryUlong(Request, CYW_MTU_SIZE);
        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            return CywOidQueryUlong(Request, DOT11_MAX_PDU_SIZE);
        case OID_GEN_VENDOR_ID:
            return CywOidQueryUlong(Request, 0x00904C00);
        case OID_GEN_VENDOR_DRIVER_VERSION:
        case OID_GEN_DRIVER_VERSION:
            return CywOidQueryUlong(Request, 0x0620);
        case OID_GEN_CURRENT_PACKET_FILTER:
            return CywOidQueryUlong(Request, Adapter->PacketFilter);
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_LOOKAHEAD:
            return CywOidQueryUlong(Request, DOT11_MAX_PDU_SIZE);
        case OID_GEN_LINK_SPEED:
        {
            ULONG Rate = 0;
            if (Adapter->LinkUp &&
                NT_SUCCESS(CywQueryRate(Adapter, &Rate)) && Rate != 0)
            {
                /* firmware units of 500 kbit/s -> NDIS units of 100 bit/s */
                return CywOidQueryUlong(Request, Rate * 5000);
            }
            return CywOidQueryUlong(Request, CYW_LINK_SPEED_BPS / 100);
        }
        case OID_GEN_XMIT_OK:
            return CywOidQueryUlong(Request, (ULONG)Adapter->TxOkCount);
        case OID_GEN_RCV_OK:
            return CywOidQueryUlong(Request, (ULONG)Adapter->RxOkCount);
        case OID_GEN_STATISTICS:
        {
            NDIS_STATISTICS_INFO Stats;
            RtlZeroMemory(&Stats, sizeof(Stats));
            Stats.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Stats.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
            Stats.Header.Size = NDIS_SIZEOF_STATISTICS_INFO_REVISION_1;
            Stats.SupportedStatistics =
                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
                NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
                NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR;
            Stats.ifHCInUcastPkts = Adapter->RxOkCount;
            Stats.ifHCOutUcastPkts = Adapter->TxOkCount;
            Stats.ifOutErrors = Adapter->TxErrCount;
            return CywOidQueryBuffer(Request, &Stats, sizeof(Stats));
        }
        case OID_DOT11_CURRENT_CHANNEL_NUMBER:
        {
            CYW_CHANNEL_INFO_LE Ci;
            RtlZeroMemory(&Ci, sizeof(Ci));
            if (NT_SUCCESS(CywFilCmdGet(Adapter, BRCMF_C_GET_CHANNEL,
                                        &Ci, sizeof(Ci))))
            {
                return CywOidQueryUlong(Request, Ci.HwChannel);
            }
            return NDIS_STATUS_FAILURE;
        }
        case OID_GEN_MEDIA_CONNECT_STATUS:
            return CywOidQueryUlong(Request, Adapter->LinkUp
                ? MediaConnectStateConnected : MediaConnectStateDisconnected);
        case OID_GEN_MAXIMUM_SEND_PACKETS:
            return CywOidQueryUlong(Request, 16);
        case OID_GEN_MAC_OPTIONS:
            return CywOidQueryUlong(Request, NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                                             NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                                             NDIS_MAC_OPTION_NO_LOOPBACK);
        case OID_DOT11_MPDU_MAX_LENGTH:
            return CywOidQueryUlong(Request, DOT11_MAX_PDU_SIZE);
        case OID_DOT11_CURRENT_OPERATION_MODE:
        {
            DOT11_CURRENT_OPERATION_MODE Mode;
            RtlZeroMemory(&Mode, sizeof(Mode));
            Mode.uCurrentOpMode = Adapter->CurrentOperationMode;
            return CywOidQueryBuffer(Request, &Mode, sizeof(Mode));
        }
        case OID_DOT11_CURRENT_PHY_TYPE:
            return CywOidQueryUlong(Request, Adapter->CurrentPhyType);
        case OID_DOT11_OPERATION_MODE_CAPABILITY:
        {
            DOT11_OPERATION_MODE_CAPABILITY Cap;
            RtlZeroMemory(&Cap, sizeof(Cap));
            Cap.uMajorVersion = 2;
            Cap.uMinorVersion = 0;
            Cap.uNumOfTXBuffers = 4;
            Cap.uNumOfRXBuffers = 4;
            Cap.uOpModeCapability = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
            return CywOidQueryBuffer(Request, &Cap, sizeof(Cap));
        }
        case OID_DOT11_SUPPORTED_PHY_TYPES:
        {
            DOT11_PHY_TYPE_LIST List;
            RtlZeroMemory(&List, sizeof(List));
            List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            List.Header.Revision = DOT11_PHY_TYPE_LIST_REVISION_1;
            List.Header.Size = sizeof(DOT11_PHY_TYPE_LIST);
            List.uNumOfEntries = 1;
            List.uTotalNumOfEntries = 1;
            List.dot11PhyType[0] = dot11_phy_type_ht;
            return CywOidQueryBuffer(Request, &List, sizeof(List));
        }
        case OID_DOT11_CURRENT_PACKET_FILTER:
            return CywOidQueryUlong(Request, Adapter->PacketFilter);
        case OID_DOT11_NIC_POWER_STATE:
        case OID_DOT11_HARDWARE_PHY_STATE:
            return CywOidQueryUlong(Request, Adapter->RadioOn);
        case OID_DOT11_AUTO_CONFIG_ENABLED:
            return CywOidQueryUlong(Request, 0x00000003);
        case OID_DOT11_MAC_ADDRESS:
        case OID_DOT11_CURRENT_ADDRESS:
            return CywOidQueryBuffer(Request, Adapter->CurrentAddress, CYW_ADDRESS_LENGTH);
        case OID_DOT11_PERMANENT_ADDRESS:
            return CywOidQueryBuffer(Request, Adapter->PermanentAddress, CYW_ADDRESS_LENGTH);
        case OID_DOT11_ENUM_BSS_LIST:
        {
            ULONG Written = CywBuildBssList(Adapter,
                Request->DATA.QUERY_INFORMATION.InformationBuffer,
                Request->DATA.QUERY_INFORMATION.InformationBufferLength,
                &BytesNeeded);
            if (Written == 0)
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = BytesNeeded;
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            Request->DATA.QUERY_INFORMATION.BytesWritten = Written;
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_DESIRED_SSID_LIST:
        {
            DOT11_SSID_LIST List;
            RtlZeroMemory(&List, sizeof(List));
            List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            List.Header.Revision = DOT11_SSID_LIST_REVISION_1;
            List.Header.Size = sizeof(List);
            List.uNumOfEntries = 1;
            List.uTotalNumOfEntries = 1;
            NdisAcquireSpinLock(&Adapter->Lock);
            List.SSIDs[0].uSSIDLength = Adapter->DesiredSsidLength;
            RtlCopyMemory(List.SSIDs[0].ucSSID, Adapter->DesiredSsid,
                          Adapter->DesiredSsidLength);
            NdisReleaseSpinLock(&Adapter->Lock);
            return CywOidQueryBuffer(Request, &List, sizeof(List));
        }
        case OID_DOT11_DESIRED_BSSID_LIST:
        {
            DOT11_BSSID_LIST List;
            RtlZeroMemory(&List, sizeof(List));
            List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            List.Header.Revision = DOT11_BSSID_LIST_REVISION_1;
            List.Header.Size = sizeof(List);
            List.uNumOfEntries = 1;
            List.uTotalNumOfEntries = 1;
            NdisAcquireSpinLock(&Adapter->Lock);
            if (Adapter->HasDesiredBssid)
            {
                RtlCopyMemory(List.BSSIDs[0], Adapter->DesiredBssid,
                              CYW_ADDRESS_LENGTH);
            }
            else
            {
                RtlFillMemory(List.BSSIDs[0], CYW_ADDRESS_LENGTH, 0xFF);
            }
            NdisReleaseSpinLock(&Adapter->Lock);
            return CywOidQueryBuffer(Request, &List, sizeof(List));
        }
        case OID_DOT11_DESIRED_BSS_TYPE:
            return CywOidQueryUlong(Request, dot11_BSS_type_infrastructure);
        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
        {
            DOT11_AUTH_ALGORITHM_LIST List;
            RtlZeroMemory(&List, sizeof(List));
            List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            List.Header.Revision = DOT11_AUTH_ALGORITHM_LIST_REVISION_1;
            List.Header.Size = sizeof(List);
            List.uNumOfEntries = 1;
            List.uTotalNumOfEntries = 1;
            List.AlgorithmIds[0] = Adapter->AuthAlgorithm;
            return CywOidQueryBuffer(Request, &List, sizeof(List));
        }
        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
        {
            DOT11_CIPHER_ALGORITHM_LIST List;
            RtlZeroMemory(&List, sizeof(List));
            List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            List.Header.Revision = DOT11_CIPHER_ALGORITHM_LIST_REVISION_1;
            List.Header.Size = sizeof(List);
            List.uNumOfEntries = 1;
            List.uTotalNumOfEntries = 1;
            List.AlgorithmIds[0] =
                (Oid == OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM)
                    ? Adapter->UnicastCipher : Adapter->MulticastCipher;
            return CywOidQueryBuffer(Request, &List, sizeof(List));
        }
        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
            return CywOidQueryUlong(Request, Adapter->DefaultKeyId);
        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS
CywSetKey(
    _In_ PCYW_ADAPTER Adapter,
    _In_ BOOLEAN Pairwise,
    _In_opt_ PUCHAR Ea,
    _In_ PUCHAR Key,
    _In_ ULONG KeyLen,
    _In_ ULONG KeyIndex)
{
    CYW_WSEC_KEY KeyData;
    ULONG Copy = KeyLen;

    if (Copy > sizeof(KeyData.Data))
    {
        Copy = sizeof(KeyData.Data);
    }

    RtlZeroMemory(&KeyData, sizeof(KeyData));
    KeyData.Index = KeyIndex;
    KeyData.Len = KeyLen;
    RtlCopyMemory(KeyData.Data, Key, Copy);
    KeyData.Algo = CYW_CRYPTO_ALGO_AES_CCM;
    if (Pairwise)
    {
        KeyData.Flags = 0;
        if (Ea != NULL)
        {
            RtlCopyMemory(KeyData.Ea, Ea, CYW_ADDRESS_LENGTH);
        }
    }
    else
    {
        KeyData.Flags = CYW_WSEC_PRIMARY_KEY;
    }

    return CywFilIovarSet(Adapter, "wsec_key", &KeyData, sizeof(KeyData));
}

static
NDIS_STATUS
CywOidSetFirstAlgoId(
    _In_ PVOID Buffer,
    _In_ ULONG Length,
    _Out_ PULONG Dest)
{
    /* DOT11_AUTH_ALGORITHM_LIST and DOT11_CIPHER_ALGORITHM_LIST share this layout. */
    PDOT11_AUTH_ALGORITHM_LIST List = (PDOT11_AUTH_ALGORITHM_LIST)Buffer;

    if (Length >= sizeof(DOT11_AUTH_ALGORITHM_LIST) && List->uNumOfEntries >= 1)
    {
        *Dest = List->AlgorithmIds[0];
    }
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
CywOidSet(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS_OID Oid = Request->DATA.SET_INFORMATION.Oid;
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;

    switch (Oid)
    {
        case OID_DOT11_SCAN_REQUEST:
            return CywOidScanRequest(Adapter, Request);
        case OID_GEN_CURRENT_PACKET_FILTER:
        case OID_DOT11_CURRENT_PACKET_FILTER:
            if (Length >= sizeof(ULONG))
            {
                Adapter->PacketFilter = *(PULONG)Buffer;
                Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            }
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_CURRENT_OPERATION_MODE:
            if (Length >= sizeof(DOT11_CURRENT_OPERATION_MODE))
            {
                Adapter->CurrentOperationMode =
                    ((PDOT11_CURRENT_OPERATION_MODE)Buffer)->uCurrentOpMode;
                Request->DATA.SET_INFORMATION.BytesRead = sizeof(DOT11_CURRENT_OPERATION_MODE);
            }
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_NIC_POWER_STATE:
            if (Length >= sizeof(BOOLEAN))
            {
                BOOLEAN On = *(PBOOLEAN)Buffer;
                BOOLEAN WasUp;

                Adapter->RadioOn = On;
                if (!On)
                {
                    NdisAcquireSpinLock(&Adapter->Lock);
                    WasUp = Adapter->LinkUp;
                    NdisReleaseSpinLock(&Adapter->Lock);
                    CywDisconnect(Adapter);
                    if (WasUp)
                    {
                        CywIndicateDisassociation(Adapter);
                        CywIndicateLinkState(Adapter, FALSE, 0);
                    }
                }
            }
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_FLUSH_BSS_LIST:
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->BssCount = 0;
            NdisReleaseSpinLock(&Adapter->Lock);
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_DESIRED_SSID_LIST:
            if (Length >= sizeof(DOT11_SSID_LIST))
            {
                PDOT11_SSID_LIST List = (PDOT11_SSID_LIST)Buffer;
                if (List->uNumOfEntries >= 1 &&
                    List->SSIDs[0].uSSIDLength <= DOT11_SSID_MAX_LENGTH)
                {
                    NdisAcquireSpinLock(&Adapter->Lock);
                    Adapter->DesiredSsidLength = List->SSIDs[0].uSSIDLength;
                    RtlCopyMemory(Adapter->DesiredSsid, List->SSIDs[0].ucSSID,
                                  List->SSIDs[0].uSSIDLength);
                    NdisReleaseSpinLock(&Adapter->Lock);
                }
            }
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_DESIRED_BSSID_LIST:
            if (Length >= sizeof(DOT11_BSSID_LIST))
            {
                PDOT11_BSSID_LIST List = (PDOT11_BSSID_LIST)Buffer;
                if (List->uNumOfEntries >= 1)
                {
                    static const UCHAR Broadcast[CYW_ADDRESS_LENGTH] =
                        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

                    NdisAcquireSpinLock(&Adapter->Lock);
                    RtlCopyMemory(Adapter->DesiredBssid, List->BSSIDs[0],
                                  CYW_ADDRESS_LENGTH);
                    Adapter->HasDesiredBssid =
                        !RtlEqualMemory(List->BSSIDs[0], Broadcast,
                                        CYW_ADDRESS_LENGTH);
                    NdisReleaseSpinLock(&Adapter->Lock);
                }
            }
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
            return CywOidSetFirstAlgoId(Buffer, Length, &Adapter->AuthAlgorithm);
        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
            return CywOidSetFirstAlgoId(Buffer, Length, &Adapter->UnicastCipher);
        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
            return CywOidSetFirstAlgoId(Buffer, Length, &Adapter->MulticastCipher);
        case OID_DOT11_CIPHER_KEY_MAPPING_KEY:
        {
            ULONG Hdr = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) +
                        FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey);
            if (Length >= Hdr)
            {
                PDOT11_BYTE_ARRAY ByteArray = (PDOT11_BYTE_ARRAY)Buffer;
                PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE Value =
                    (PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE)ByteArray->ucBuffer;
                if (Length >= Hdr + Value->usKeyLength)
                {
                    CywSetKey(Adapter, TRUE, Value->PeerMacAddr, Value->ucKey,
                              Value->usKeyLength, 0);
                }
            }
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_CIPHER_DEFAULT_KEY:
        {
            ULONG Hdr = FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey);
            if (Length >= Hdr)
            {
                PDOT11_CIPHER_DEFAULT_KEY_VALUE Value =
                    (PDOT11_CIPHER_DEFAULT_KEY_VALUE)Buffer;
                if (Length >= Hdr + Value->usKeyLength)
                {
                    if (Value->AlgorithmId == DOT11_CIPHER_ALGO_NONE)
                    {
                        /* The nwifi stack seeds passphrases under an
                         * Algorithm==NONE sentinel; keep it for the
                         * firmware-offloaded SAE handshake. */
                        if (Value->usKeyLength <= CYW_SAE_PASSWORD_MAX)
                        {
                            RtlCopyMemory(Adapter->SaePassword, Value->ucKey,
                                          Value->usKeyLength);
                            Adapter->SaePasswordLen = Value->usKeyLength;
                        }
                    }
                    else
                    {
                        CywSetKey(Adapter, FALSE, NULL, Value->ucKey,
                                  Value->usKeyLength, Value->uKeyIndex);
                        CywFilCmdSet(Adapter, BRCMF_C_SET_SCB_AUTHORIZE,
                                     Adapter->ConnectedBssid, CYW_ADDRESS_LENGTH);
                    }
                }
            }
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
            if (Length >= sizeof(ULONG))
            {
                Adapter->DefaultKeyId = *(PULONG)Buffer;
            }
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_CONNECT_REQUEST:
            return CywOidConnectRequest(Adapter, Request);
        case OID_DOT11_DISCONNECT_REQUEST:
        {
            BOOLEAN WasUp;

            NdisAcquireSpinLock(&Adapter->Lock);
            WasUp = Adapter->LinkUp;
            NdisReleaseSpinLock(&Adapter->Lock);

            CywDisconnect(Adapter);
            if (WasUp)
            {
                CywIndicateDisassociation(Adapter);
            }
            CywIndicateLinkState(Adapter, FALSE, 0);
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_DESIRED_BSS_TYPE:
            /* Only infrastructure STA is supported; accept and ignore */
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_RESET_REQUEST:
        {
            BOOLEAN WasUp;

            NdisAcquireSpinLock(&Adapter->Lock);
            WasUp = Adapter->LinkUp;
            NdisReleaseSpinLock(&Adapter->Lock);

            CywDisconnect(Adapter);
            if (WasUp)
            {
                CywIndicateDisassociation(Adapter);
                CywIndicateLinkState(Adapter, FALSE, 0);
            }

            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->DesiredSsidLength = 0;
            Adapter->HasDesiredBssid = FALSE;
            NdisReleaseSpinLock(&Adapter->Lock);
            RtlSecureZeroMemory(Adapter->SaePassword, sizeof(Adapter->SaePassword));
            Adapter->SaePasswordLen = 0;
            Adapter->DefaultKeyId = 0;

            if (Length >= sizeof(DOT11_RESET_REQUEST) &&
                ((PDOT11_RESET_REQUEST)Buffer)->bSetDefaultMIB)
            {
                NdisAcquireSpinLock(&Adapter->Lock);
                Adapter->BssCount = 0;
                NdisReleaseSpinLock(&Adapter->Lock);
                Adapter->AuthAlgorithm = DOT11_AUTH_ALGO_80211_OPEN;
                Adapter->UnicastCipher = DOT11_CIPHER_ALGO_NONE;
                Adapter->MulticastCipher = DOT11_CIPHER_ALGO_NONE;
            }
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }
        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

NDIS_STATUS
NTAPI
CywMiniportOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return CywOidQuery(Adapter, OidRequest);
        case NdisRequestSetInformation:
        case NdisRequestMethod:
            return CywOidSet(Adapter, OidRequest);
        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

VOID
NTAPI
CywMiniportCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}
