/*
 * PROJECT:     ReactOS Broadcom/Cypress CYW43455 Native 802.11 Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NDIS 6.30 dot11 OID handlers, scan flow and BSS list
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"

#define NDEBUG
#include <debug.h>

static const DOT11_DATA_RATE_MAPPING_ENTRY CywDataRateMappings[] =
{
    { 0x02, 0, 0x02 }, /* 1 Mbps */
    { 0x04, 0, 0x04 }, /* 2 Mbps */
    { 0x0B, 0, 0x0B }, /* 5.5 Mbps */
    { 0x0C, 0, 0x0C }, /* 6 Mbps */
    { 0x12, 0, 0x12 }, /* 9 Mbps */
    { 0x16, 0, 0x16 }, /* 11 Mbps */
    { 0x18, 0, 0x18 }, /* 12 Mbps */
    { 0x24, 0, 0x24 }, /* 18 Mbps */
    { 0x30, 0, 0x30 }, /* 24 Mbps */
    { 0x48, 0, 0x48 }, /* 36 Mbps */
    { 0x60, 0, 0x60 }, /* 48 Mbps */
    { 0x6C, 0, 0x6C }, /* 54 Mbps */
};

C_ASSERT(RTL_NUMBER_OF(CywDataRateMappings) <= DOT11_RATE_SET_MAX_LENGTH);

UCHAR
CywDataRateIndexFromUnits(
    _In_ ULONG RateUnits500Kbps)
{
    UCHAR Index = CywDataRateMappings[0].ucDataRateIndex;
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(CywDataRateMappings); i++)
    {
        if (RateUnits500Kbps < CywDataRateMappings[i].usDataRateValue)
        {
            break;
        }
        Index = CywDataRateMappings[i].ucDataRateIndex;
    }
    return Index;
}

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
    OID_DOT11_DATA_RATE_MAPPING_TABLE,
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
ULONG64
CywReadCounter(
    _In_ volatile LONG64 *Counter)
{
    return (ULONG64)InterlockedCompareExchange64(Counter, 0, 0);
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
                                     CywChannelToFrequency(Src->ChannelNumber);
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
    ULONG SsidLength;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_ASSOCIATION_START_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(DOT11_ASSOCIATION_START_PARAMETERS);
    NdisAcquireSpinLock(&Adapter->Lock);
    RtlCopyMemory(Params.MacAddr,
                  Adapter->HasDesiredBssid ? Adapter->DesiredBssid
                                           : Adapter->ConnectedBssid,
                  CYW_ADDRESS_LENGTH);
    SsidLength = min(Adapter->DesiredSsidLength,
                     (ULONG)DOT11_SSID_MAX_LENGTH);
    Params.SSID.uSSIDLength = SsidLength;
    RtlCopyMemory(Params.SSID.ucSSID, Adapter->DesiredSsid, SsidLength);
    NdisReleaseSpinLock(&Adapter->Lock);

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
    Params.uStatus = Status;

    NdisAcquireSpinLock(&Adapter->Lock);
    RtlCopyMemory(Params.MacAddr, Adapter->ConnectedBssid, CYW_ADDRESS_LENGTH);
    if (Status == DOT11_ASSOC_STATUS_SUCCESS)
    {
        Params.AuthAlgo = Adapter->AuthAlgorithm;
        Params.UnicastCipher = Adapter->UnicastCipher;
        Params.MulticastCipher = Adapter->MulticastCipher;
        Params.bPortAuthorized = Adapter->PortAuthorized;
        Params.DSInfo = DOT11_DS_UNKNOWN;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

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
    DOT11_MAC_ADDRESS Bssid;
    LONG Rssi;

    if (!NT_SUCCESS(CywQueryRssi(Adapter, &Rssi)))
    {
        return;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->ConnectedRssi = Rssi;
    RtlCopyMemory(Bssid, Adapter->ConnectedBssid, CYW_ADDRESS_LENGTH);
    NdisReleaseSpinLock(&Adapter->Lock);

    RtlZeroMemory(&Buf, sizeof(Buf));
    Buf.Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Buf.Params.Header.Revision = DOT11_LINK_QUALITY_PARAMETERS_REVISION_1;
    Buf.Params.Header.Size = sizeof(DOT11_LINK_QUALITY_PARAMETERS);
    Buf.Params.uLinkQualityListSize = 1;
    Buf.Params.uLinkQualityListOffset = sizeof(DOT11_LINK_QUALITY_PARAMETERS);
    RtlCopyMemory(Buf.Entry.PeerMacAddr, Bssid, CYW_ADDRESS_LENGTH);
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
        DOT11_ASSOC_STATUS AssocStatus =
            (Status == NDIS_STATUS_SUCCESS) ? DOT11_ASSOC_STATUS_SUCCESS
                                             : DOT11_ASSOC_STATUS_FAILURE;

        CywIndicateAssocComplete(Adapter, AssocStatus);
        CywIndicateConnectComplete(Adapter, AssocStatus);
        NdisMOidRequestComplete(Adapter->MiniportAdapterHandle, Request, Status);
    }
}

static
VOID
CywCompleteDetachedOids(
    _In_ PCYW_ADAPTER Adapter,
    _In_opt_ PNDIS_OID_REQUEST ScanRequest,
    _In_opt_ PNDIS_OID_REQUEST ConnectRequest,
    _In_ NDIS_STATUS Status)
{
    BOOLEAN Indicate =
        (InterlockedCompareExchange(&Adapter->Halting, 0, 0) == 0);

    if (ScanRequest != NULL)
    {
        DOT11_STATUS_INDICATION Confirm;

        if (Indicate)
        {
            RtlZeroMemory(&Confirm, sizeof(Confirm));
            Confirm.uStatusType = DOT11_STATUS_SCAN_CONFIRM;
            Confirm.ndisStatus = Status;
            CywIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_SCAN_CONFIRM,
                                   &Confirm, sizeof(Confirm));
        }
        NdisMOidRequestComplete(Adapter->MiniportAdapterHandle, ScanRequest, Status);
    }
    if (ConnectRequest != NULL)
    {
        DOT11_ASSOC_STATUS AssocStatus =
            (Status == NDIS_STATUS_REQUEST_ABORTED) ? DOT11_ASSOC_STATUS_CANCELLED
                                                    : DOT11_ASSOC_STATUS_FAILURE;

        if (Indicate)
        {
            CywIndicateAssocComplete(Adapter, AssocStatus);
            CywIndicateConnectComplete(Adapter, AssocStatus);
        }
        NdisMOidRequestComplete(Adapter->MiniportAdapterHandle, ConnectRequest, Status);
    }
}

VOID
CywAbortPendingOids(
    _In_ PCYW_ADAPTER Adapter,
    _In_ NDIS_STATUS Status)
{
    PNDIS_OID_REQUEST ScanRequest;
    PNDIS_OID_REQUEST ConnectRequest;

    NdisAcquireSpinLock(&Adapter->Lock);
    ScanRequest = Adapter->PendingScanOid;
    ConnectRequest = Adapter->PendingConnectOid;
    Adapter->PendingScanOid = NULL;
    Adapter->PendingConnectOid = NULL;
    Adapter->ScanInProgress = FALSE;
    Adapter->ScanSeq++;
    Adapter->ConnectSeq++;
    NdisReleaseSpinLock(&Adapter->Lock);

    CywCompleteDetachedOids(Adapter, ScanRequest, ConnectRequest, Status);
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
    BOOLEAN LinkUp = FALSE;

    if (!Adapter->Halting)
    {
        if (NT_SUCCESS(CywQueryRate(Adapter, &Rate)) && Rate != 0)
        {
            SpeedBps = (ULONG64)Rate * 500000;  /* firmware units: 500 kbit/s */
        }
        NdisAcquireSpinLock(&Adapter->Lock);
        LinkUp = Adapter->LinkUp;
        if (LinkUp && Rate != 0)
        {
            Adapter->CurrentRateUnits500Kbps = Rate;
        }
        NdisReleaseSpinLock(&Adapter->Lock);
        if (LinkUp)
        {
            CywIndicateLinkState(Adapter, TRUE, SpeedBps);
            CywIndicateLinkQuality(Adapter);
        }
    }

    CywCompleteWorkItem(Adapter, NdisIoWorkItemHandle);
}

VOID
CywQueueLinkUpWork(
    _In_ PCYW_ADAPTER Adapter)
{
    CywQueueWorkItem(Adapter, CywLinkUpWorker, Adapter);
}

static
BOOLEAN
CywIndicateScanCompleteInternal(
    _In_ PCYW_ADAPTER Adapter,
    _In_ NDIS_STATUS ScanStatus,
    _In_ BOOLEAN CheckSequence,
    _In_ ULONG ExpectedSequence)
{
    DOT11_STATUS_INDICATION Confirm;
    PNDIS_OID_REQUEST Request;
    BOOLEAN WasInProgress;

    NdisAcquireSpinLock(&Adapter->Lock);
    if (CheckSequence && Adapter->ScanSeq != ExpectedSequence)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return FALSE;
    }
    WasInProgress = Adapter->ScanInProgress;
    Adapter->ScanInProgress = FALSE;
    Request = Adapter->PendingScanOid;
    Adapter->PendingScanOid = NULL;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (!WasInProgress)
    {
        return FALSE;
    }

    RtlZeroMemory(&Confirm, sizeof(Confirm));
    Confirm.uStatusType = DOT11_STATUS_SCAN_CONFIRM;
    Confirm.ndisStatus = ScanStatus;

    CywIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_SCAN_CONFIRM,
                           &Confirm, sizeof(Confirm));

    if (Request != NULL)
    {
        NdisMOidRequestComplete(Adapter->MiniportAdapterHandle, Request,
                                ScanStatus);
    }
    return TRUE;
}

VOID
CywIndicateScanComplete(
    _In_ PCYW_ADAPTER Adapter,
    _In_ NDIS_STATUS ScanStatus)
{
    CywIndicateScanCompleteInternal(Adapter, ScanStatus, FALSE, 0);
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

    for (;;)
    {
        BOOLEAN Changed;
        BOOLEAN Pending;
        ULONG Slice;

        NdisAcquireSpinLock(&Adapter->Lock);
        Pending = (Adapter->PendingScanOid != NULL);
        if (!Pending ||
            InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
        {
            InterlockedExchange(&Adapter->ScanWorkQueued, 0);
            NdisReleaseSpinLock(&Adapter->Lock);
            break;
        }
        Seq = Adapter->ScanSeq;
        NdisReleaseSpinLock(&Adapter->Lock);

        for (Slice = 0; Slice < CYW_ESCAN_TIMEOUT_SLICES; Slice++)
        {
            Delay.QuadPart = CYW_ESCAN_TIMEOUT_SLICE;
            KeDelayExecutionThread(KernelMode, FALSE, &Delay);

            NdisAcquireSpinLock(&Adapter->Lock);
            Changed = (Adapter->ScanSeq != Seq);
            NdisReleaseSpinLock(&Adapter->Lock);
            if (InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0 ||
                Changed)
            {
                break;
            }
        }

        NdisAcquireSpinLock(&Adapter->Lock);
        Changed = (Adapter->ScanSeq != Seq);
        Pending = (Adapter->PendingScanOid != NULL);
        if (!Pending ||
            InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
        {
            InterlockedExchange(&Adapter->ScanWorkQueued, 0);
            NdisReleaseSpinLock(&Adapter->Lock);
            break;
        }
        NdisReleaseSpinLock(&Adapter->Lock);

        if (!Changed)
        {
            CywIndicateScanCompleteInternal(Adapter,
                                             NDIS_STATUS_FAILURE,
                                             TRUE,
                                             Seq);
        }
    }

    CywCompleteWorkItem(Adapter, NdisIoWorkItemHandle);
}

static
NDIS_STATUS
CywOidScanRequest(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NTSTATUS Status;
    ULONG Minimum = FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer);

    if (Request->DATA.SET_INFORMATION.InformationBufferLength < Minimum)
    {
        Request->DATA.SET_INFORMATION.BytesNeeded = Minimum;
        return NDIS_STATUS_INVALID_LENGTH;
    }

    if (!Adapter->RadioOn)
    {
        return NDIS_STATUS_DOT11_POWER_STATE_INVALID;
    }
    Request->DATA.SET_INFORMATION.BytesRead =
        Request->DATA.SET_INFORMATION.InformationBufferLength;

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
        if (Status == STATUS_INVALID_PARAMETER)
        {
            return NDIS_STATUS_INVALID_DATA;
        }
        if (Status == STATUS_INSUFFICIENT_RESOURCES)
        {
            return NDIS_STATUS_RESOURCES;
        }
        return NDIS_STATUS_FAILURE;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    if (InterlockedCompareExchange(&Adapter->ScanWorkQueued, 1, 0) == 0)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        if (!CywQueueWorkItem(Adapter, CywScanWorker, Adapter))
        {
            NdisAcquireSpinLock(&Adapter->Lock);
            InterlockedExchange(&Adapter->ScanWorkQueued, 0);
            NdisReleaseSpinLock(&Adapter->Lock);
            CywIndicateScanComplete(Adapter, NDIS_STATUS_NOT_ACCEPTED);
        }
    }
    else
    {
        NdisReleaseSpinLock(&Adapter->Lock);
    }
    return NDIS_STATUS_PENDING;
}

typedef struct _CYW_CONNECT_WORK_CONTEXT
{
    PCYW_ADAPTER Adapter;
    ULONG Sequence;
} CYW_CONNECT_WORK_CONTEXT, *PCYW_CONNECT_WORK_CONTEXT;

/* Issues the connect; the link event completes the OID, this worker's delay
 * is only the timeout fallback (sequence-guarded against newer connects). */
static
VOID
CywConnectWorker(
    _In_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PCYW_CONNECT_WORK_CONTEXT Context =
        (PCYW_CONNECT_WORK_CONTEXT)WorkItemContext;
    PCYW_ADAPTER Adapter = Context->Adapter;
    ULONG Seq = Context->Sequence;
    LARGE_INTEGER Delay;
    NTSTATUS Status;

    CywFree(Context);

    KeWaitForSingleObject(&Adapter->ConnectLock,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->PendingConnectOid == NULL ||
        Adapter->ConnectSeq != Seq ||
        InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        KeReleaseMutex(&Adapter->ConnectLock, FALSE);
        CywCompleteWorkItem(Adapter, NdisIoWorkItemHandle);
        return;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    CywIndicateAssocStart(Adapter);
    Status = CywConnect(Adapter);
    KeReleaseMutex(&Adapter->ConnectLock, FALSE);
    if (!NT_SUCCESS(Status))
    {
        if (Adapter->ConnectSeq == Seq)
        {
            CywCompletePendingConnect(Adapter, NDIS_STATUS_FAILURE);
        }
        CywCompleteWorkItem(Adapter, NdisIoWorkItemHandle);
        return;
    }

    Delay.QuadPart = -30000000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    if (Adapter->ConnectSeq == Seq)
    {
        CywCompletePendingConnect(Adapter, NDIS_STATUS_FAILURE);
    }

    CywCompleteWorkItem(Adapter, NdisIoWorkItemHandle);
}

static
BOOLEAN
CywQueueConnectWorker(
    _In_ PCYW_ADAPTER Adapter,
    _In_ ULONG Sequence)
{
    PCYW_CONNECT_WORK_CONTEXT Context;

    Context = CywAllocate(sizeof(*Context));
    if (Context == NULL)
    {
        return FALSE;
    }
    Context->Adapter = Adapter;
    Context->Sequence = Sequence;
    if (!CywQueueWorkItem(Adapter, CywConnectWorker, Context))
    {
        CywFree(Context);
        return FALSE;
    }
    return TRUE;
}

static
NDIS_STATUS
CywOidConnectRequest(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    ULONG Sequence;

    if (!Adapter->RadioOn)
    {
        return NDIS_STATUS_DOT11_POWER_STATE_INVALID;
    }

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->DesiredSsidLength == 0)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return NDIS_STATUS_INVALID_DATA;
    }
    if (Adapter->PendingConnectOid != NULL)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return NDIS_STATUS_DOT11_MEDIA_IN_USE;
    }
    Adapter->PendingConnectOid = Request;
    Adapter->Associated = FALSE;
    Adapter->LinkUp = FALSE;
    Adapter->FirmwareSupplicant =
        (Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK ||
         Adapter->AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE);
    Adapter->FirmwareHandshakeComplete = FALSE;
    Adapter->PortAuthorized = FALSE;
    Sequence = ++Adapter->ConnectSeq;
    NdisReleaseSpinLock(&Adapter->Lock);

    Request->DATA.SET_INFORMATION.BytesRead =
        Request->DATA.SET_INFORMATION.InformationBufferLength;
    InterlockedExchange(&Adapter->JoinRetries, 0);
    if (!CywQueueConnectWorker(Adapter, Sequence))
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        if (Adapter->PendingConnectOid == Request)
        {
            Adapter->PendingConnectOid = NULL;
        }
        NdisReleaseSpinLock(&Adapter->Lock);
        return NDIS_STATUS_NOT_ACCEPTED;
    }
    return NDIS_STATUS_PENDING;
}

VOID
CywQueueConnectWork(
    _In_ PCYW_ADAPTER Adapter)
{
    BOOLEAN Queue;
    ULONG Sequence = 0;

    NdisAcquireSpinLock(&Adapter->Lock);
    Queue = (Adapter->PendingConnectOid != NULL && !Adapter->Halting);
    if (Queue)
    {
        Sequence = ++Adapter->ConnectSeq;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    if (Queue)
    {
        if (!CywQueueConnectWorker(Adapter, Sequence))
        {
            CywCompletePendingConnect(Adapter, NDIS_STATUS_FAILURE);
        }
    }
}

static
NDIS_STATUS
CywOidQuery(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NDIS_OID Oid = Request->DATA.QUERY_INFORMATION.Oid;
    ULONG BytesNeeded;

    Request->DATA.QUERY_INFORMATION.BytesWritten = 0;
    Request->DATA.QUERY_INFORMATION.BytesNeeded = 0;
    if (Request->DATA.QUERY_INFORMATION.InformationBufferLength != 0 &&
        Request->DATA.QUERY_INFORMATION.InformationBuffer == NULL)
    {
        return NDIS_STATUS_INVALID_DATA;
    }

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
        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Description[] = "Broadcom CYW43455 SDIO 802.11";
            return CywOidQueryBuffer(Request, (PVOID)Description, sizeof(Description));
        }
        case OID_GEN_VENDOR_DRIVER_VERSION:
            return CywOidQueryUlong(Request, 0x0620);
        case OID_GEN_DRIVER_VERSION:
        {
            USHORT Version = 0x0620;
            return CywOidQueryBuffer(Request, &Version, sizeof(Version));
        }
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
                return CywOidQueryUlong(Request,
                    (Rate > MAXULONG / 5000) ? MAXULONG : Rate * 5000);
            }
            return CywOidQueryUlong(Request, CYW_LINK_SPEED_BPS / 100);
        }
        case OID_GEN_XMIT_OK:
        {
            ULONG64 Count = CywReadCounter(&Adapter->TxOkCount);
            return CywOidQueryBuffer(Request, &Count, sizeof(Count));
        }
        case OID_GEN_RCV_OK:
        {
            ULONG64 Count = CywReadCounter(&Adapter->RxOkCount);
            return CywOidQueryBuffer(Request, &Count, sizeof(Count));
        }
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
            Stats.ifHCInUcastPkts = CywReadCounter(&Adapter->RxOkCount);
            Stats.ifHCOutUcastPkts = CywReadCounter(&Adapter->TxOkCount);
            Stats.ifOutErrors = CywReadCounter(&Adapter->TxErrCount);
            return CywOidQueryBuffer(Request, &Stats, sizeof(Stats));
        }
        case OID_DOT11_DATA_RATE_MAPPING_TABLE:
        {
            DOT11_DATA_RATE_MAPPING_TABLE Table;

            RtlZeroMemory(&Table, sizeof(Table));
            Table.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Table.Header.Revision = DOT11_DATA_RATE_MAPPING_TABLE_REVISION_1;
            Table.Header.Size = sizeof(DOT11_DATA_RATE_MAPPING_TABLE);
            Table.uDataRateMappingLength = RTL_NUMBER_OF(CywDataRateMappings);
            RtlCopyMemory(Table.DataRateMappingEntries, CywDataRateMappings,
                          sizeof(CywDataRateMappings));
            return CywOidQueryBuffer(Request, &Table, sizeof(Table));
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
            return CywOidQueryUlong(Request, Adapter->Dot11PacketFilter);
        case OID_DOT11_NIC_POWER_STATE:
        case OID_DOT11_HARDWARE_PHY_STATE:
            return CywOidQueryBuffer(Request, &Adapter->RadioOn,
                                     sizeof(Adapter->RadioOn));
        case OID_DOT11_AUTO_CONFIG_ENABLED:
            return CywOidQueryUlong(Request, Adapter->AutoConfigEnabled);
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
            NdisAcquireSpinLock(&Adapter->Lock);
            List.AlgorithmIds[0] = Adapter->AuthAlgorithm;
            NdisReleaseSpinLock(&Adapter->Lock);
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
            NdisAcquireSpinLock(&Adapter->Lock);
            List.AlgorithmIds[0] =
                (Oid == OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM)
                    ? Adapter->UnicastCipher : Adapter->MulticastCipher;
            NdisReleaseSpinLock(&Adapter->Lock);
            return CywOidQueryBuffer(Request, &List, sizeof(List));
        }
        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
        {
            ULONG KeyId;
            NdisAcquireSpinLock(&Adapter->Lock);
            KeyId = Adapter->DefaultKeyId;
            NdisReleaseSpinLock(&Adapter->Lock);
            return CywOidQueryUlong(Request, KeyId);
        }
        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

NTSTATUS
CywSetKey(
    _In_ PCYW_ADAPTER Adapter,
    _In_ BOOLEAN Pairwise,
    _In_opt_ PUCHAR Ea,
    _In_ DOT11_CIPHER_ALGORITHM Algorithm,
    _In_ BOOLEAN Delete,
    _In_reads_bytes_opt_(KeyLen) PUCHAR Key,
    _In_ ULONG KeyLen,
    _In_ ULONG KeyIndex)
{
    CYW_WSEC_KEY KeyData;
    PUCHAR Iv = NULL;
    ULONG MaterialLength = 0;
    NTSTATUS Status;

    RtlZeroMemory(&KeyData, sizeof(KeyData));
    KeyData.Index = KeyIndex;
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

    if (Delete)
    {
        KeyData.Algo = CYW_CRYPTO_ALGO_OFF;
        Status = CywFilIovarSet(Adapter, "wsec_key", &KeyData, sizeof(KeyData));
        goto Exit;
    }
    if (Key == NULL)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Exit;
    }

    switch (Algorithm)
    {
        case DOT11_CIPHER_ALGO_CCMP:
        {
            PDOT11_KEY_ALGO_CCMP Ccmp = (PDOT11_KEY_ALGO_CCMP)Key;
            ULONG HeaderLength = FIELD_OFFSET(DOT11_KEY_ALGO_CCMP, ucCCMPKey);

            if (KeyLen < HeaderLength || Ccmp->ulCCMPKeyLength != 16 ||
                Ccmp->ulCCMPKeyLength > KeyLen - HeaderLength)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }
            MaterialLength = Ccmp->ulCCMPKeyLength;
            RtlCopyMemory(KeyData.Data, Ccmp->ucCCMPKey, MaterialLength);
            Iv = Ccmp->ucIV48Counter;
            KeyData.Algo = CYW_CRYPTO_ALGO_AES_CCM;
            break;
        }
        case DOT11_CIPHER_ALGO_TKIP:
        {
            PDOT11_KEY_ALGO_TKIP_MIC Tkip = (PDOT11_KEY_ALGO_TKIP_MIC)Key;
            ULONG HeaderLength = FIELD_OFFSET(DOT11_KEY_ALGO_TKIP_MIC, ucTKIPMICKeys);

            if (KeyLen < HeaderLength || Tkip->ulTKIPKeyLength != 16 ||
                Tkip->ulMICKeyLength != 16 ||
                Tkip->ulTKIPKeyLength > MAXULONG - Tkip->ulMICKeyLength)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }
            MaterialLength = Tkip->ulTKIPKeyLength + Tkip->ulMICKeyLength;
            if (MaterialLength > sizeof(KeyData.Data) ||
                MaterialLength > KeyLen - HeaderLength)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }
            RtlCopyMemory(KeyData.Data, Tkip->ucTKIPMICKeys, MaterialLength);
            /* Native Wi-Fi supplies RX MIC then TX MIC. The firmware STA
             * key format expects those two 8-byte MIC keys in reverse order. */
            {
                UCHAR Mic[8];
                RtlCopyMemory(Mic, KeyData.Data + 16, sizeof(Mic));
                RtlCopyMemory(KeyData.Data + 16, KeyData.Data + 24, sizeof(Mic));
                RtlCopyMemory(KeyData.Data + 24, Mic, sizeof(Mic));
            }
            Iv = Tkip->ucIV48Counter;
            KeyData.Algo = CYW_CRYPTO_ALGO_TKIP;
            break;
        }
        case DOT11_CIPHER_ALGO_WEP40:
            if (KeyLen != 5)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }
            MaterialLength = KeyLen;
            RtlCopyMemory(KeyData.Data, Key, MaterialLength);
            KeyData.Algo = CYW_CRYPTO_ALGO_WEP1;
            break;
        case DOT11_CIPHER_ALGO_WEP104:
            if (KeyLen != 13)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Exit;
            }
            MaterialLength = KeyLen;
            RtlCopyMemory(KeyData.Data, Key, MaterialLength);
            KeyData.Algo = CYW_CRYPTO_ALGO_WEP128;
            break;
        default:
            Status = STATUS_NOT_SUPPORTED;
            goto Exit;
    }

    KeyData.Len = MaterialLength;
    if (Iv != NULL)
    {
        KeyData.Rxiv.Hi = ((ULONG)Iv[5] << 24) | ((ULONG)Iv[4] << 16) |
                          ((ULONG)Iv[3] << 8) | Iv[2];
        KeyData.Rxiv.Lo = (USHORT)(((USHORT)Iv[1] << 8) | Iv[0]);
        KeyData.IvInit = 1;
    }

    Status = CywFilIovarSet(Adapter, "wsec_key", &KeyData, sizeof(KeyData));

Exit:
    RtlSecureZeroMemory(&KeyData, sizeof(KeyData));
    return Status;
}

static
NDIS_STATUS
CywOidSetFirstAlgoId(
    _In_ PNDIS_OID_REQUEST Request,
    _Out_ PULONG Dest)
{
    /* DOT11_AUTH_ALGORITHM_LIST and DOT11_CIPHER_ALGORITHM_LIST share this layout. */
    PVOID Buffer = Request->DATA.SET_INFORMATION.InformationBuffer;
    ULONG Length = Request->DATA.SET_INFORMATION.InformationBufferLength;
    PDOT11_AUTH_ALGORITHM_LIST List = (PDOT11_AUTH_ALGORITHM_LIST)Buffer;
    ULONG Minimum = FIELD_OFFSET(DOT11_AUTH_ALGORITHM_LIST, AlgorithmIds) +
                    sizeof(List->AlgorithmIds[0]);

    if (Length < Minimum)
    {
        Request->DATA.SET_INFORMATION.BytesNeeded = Minimum;
        return NDIS_STATUS_INVALID_LENGTH;
    }
    if (List->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
        List->Header.Revision != DOT11_AUTH_ALGORITHM_LIST_REVISION_1 ||
        List->Header.Size < Minimum || List->uNumOfEntries != 1 ||
        List->uTotalNumOfEntries < List->uNumOfEntries)
    {
        return NDIS_STATUS_INVALID_DATA;
    }

    *Dest = List->AlgorithmIds[0];
    Request->DATA.SET_INFORMATION.BytesRead = Length;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
CywOidSetInvalidLength(
    _In_ PNDIS_OID_REQUEST Request,
    _In_ ULONG Needed)
{
    Request->DATA.SET_INFORMATION.BytesNeeded = Needed;
    return NDIS_STATUS_INVALID_LENGTH;
}

static
NDIS_STATUS
CywNtStatusToNdisStatus(
    _In_ NTSTATUS Status)
{
    if (NT_SUCCESS(Status))
    {
        return NDIS_STATUS_SUCCESS;
    }
    if (Status == STATUS_INSUFFICIENT_RESOURCES)
    {
        return NDIS_STATUS_RESOURCES;
    }
    if (Status == STATUS_NOT_SUPPORTED)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }
    if (Status == STATUS_INVALID_PARAMETER || Status == STATUS_BUFFER_TOO_SMALL)
    {
        return NDIS_STATUS_INVALID_DATA;
    }
    return NDIS_STATUS_FAILURE;
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

    Request->DATA.SET_INFORMATION.BytesRead = 0;
    Request->DATA.SET_INFORMATION.BytesNeeded = 0;
    if (Length != 0 && Buffer == NULL)
    {
        return NDIS_STATUS_INVALID_DATA;
    }

    switch (Oid)
    {
        case OID_DOT11_SCAN_REQUEST:
            return CywOidScanRequest(Adapter, Request);
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
            {
                return CywOidSetInvalidLength(Request, sizeof(ULONG));
            }
            if (*(PULONG)Buffer & ~(NDIS_PACKET_TYPE_DIRECTED |
                                    NDIS_PACKET_TYPE_MULTICAST |
                                    NDIS_PACKET_TYPE_ALL_MULTICAST |
                                    NDIS_PACKET_TYPE_BROADCAST))
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }
            Adapter->PacketFilter = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_CURRENT_PACKET_FILTER:
            if (Length < sizeof(ULONG))
            {
                return CywOidSetInvalidLength(Request, sizeof(ULONG));
            }
            if (*(PULONG)Buffer & ~(DOT11_PACKET_TYPE_DIRECTED_DATA |
                                    DOT11_PACKET_TYPE_MULTICAST_DATA |
                                    DOT11_PACKET_TYPE_BROADCAST_DATA |
                                    DOT11_PACKET_TYPE_ALL_MULTICAST_DATA))
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }
            Adapter->Dot11PacketFilter = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        case OID_GEN_CURRENT_LOOKAHEAD:
            if (Length < sizeof(ULONG))
            {
                return CywOidSetInvalidLength(Request, sizeof(ULONG));
            }
            if (*(PULONG)Buffer > DOT11_MAX_PDU_SIZE)
            {
                return NDIS_STATUS_INVALID_DATA;
            }
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_CURRENT_OPERATION_MODE:
            if (Length < sizeof(DOT11_CURRENT_OPERATION_MODE))
            {
                return CywOidSetInvalidLength(Request, sizeof(DOT11_CURRENT_OPERATION_MODE));
            }
            if (((PDOT11_CURRENT_OPERATION_MODE)Buffer)->uCurrentOpMode !=
                DOT11_OPERATION_MODE_EXTENSIBLE_STATION)
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }
            Adapter->CurrentOperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(DOT11_CURRENT_OPERATION_MODE);
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_NIC_POWER_STATE:
            if (Length < sizeof(BOOLEAN))
            {
                return CywOidSetInvalidLength(Request, sizeof(BOOLEAN));
            }
            {
                BOOLEAN On = *(PBOOLEAN)Buffer ? TRUE : FALSE;
                BOOLEAN WasUp;
                NTSTATUS Status = STATUS_SUCCESS;

                if (!On)
                {
                    CywAbortPendingOids(Adapter,
                                        NDIS_STATUS_DOT11_POWER_STATE_INVALID);
                    NdisAcquireSpinLock(&Adapter->Lock);
                    WasUp = Adapter->LinkUp;
                    NdisReleaseSpinLock(&Adapter->Lock);
                    Status = CywDisconnect(Adapter);
                    if (!NT_SUCCESS(Status))
                    {
                        return CywNtStatusToNdisStatus(Status);
                    }
                    if (WasUp)
                    {
                        CywIndicateDisassociation(Adapter);
                        CywIndicateLinkState(Adapter, FALSE, 0);
                    }
                }
                Adapter->RadioOn = On;
                Request->DATA.SET_INFORMATION.BytesRead = sizeof(BOOLEAN);
            }
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_FLUSH_BSS_LIST:
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->BssCount = 0;
            NdisReleaseSpinLock(&Adapter->Lock);
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_AUTO_CONFIG_ENABLED:
            if (Length < sizeof(ULONG))
            {
                return CywOidSetInvalidLength(Request, sizeof(ULONG));
            }
            if (*(PULONG)Buffer & ~0x00000003UL)
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }
            Adapter->AutoConfigEnabled = *(PULONG)Buffer;
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_DESIRED_SSID_LIST:
            if (Length < sizeof(DOT11_SSID_LIST))
            {
                return CywOidSetInvalidLength(Request, sizeof(DOT11_SSID_LIST));
            }
            {
                PDOT11_SSID_LIST List = (PDOT11_SSID_LIST)Buffer;
                if (List->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
                    List->Header.Revision != DOT11_SSID_LIST_REVISION_1 ||
                    List->Header.Size < sizeof(DOT11_SSID_LIST) ||
                    List->uNumOfEntries != 1 ||
                    List->uTotalNumOfEntries < List->uNumOfEntries ||
                    List->SSIDs[0].uSSIDLength > DOT11_SSID_MAX_LENGTH)
                {
                    return NDIS_STATUS_INVALID_DATA;
                }
                NdisAcquireSpinLock(&Adapter->Lock);
                Adapter->DesiredSsidLength = List->SSIDs[0].uSSIDLength;
                RtlCopyMemory(Adapter->DesiredSsid, List->SSIDs[0].ucSSID,
                              List->SSIDs[0].uSSIDLength);
                NdisReleaseSpinLock(&Adapter->Lock);
                Request->DATA.SET_INFORMATION.BytesRead = Length;
            }
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_DESIRED_BSSID_LIST:
            if (Length < sizeof(DOT11_BSSID_LIST))
            {
                return CywOidSetInvalidLength(Request, sizeof(DOT11_BSSID_LIST));
            }
            {
                PDOT11_BSSID_LIST List = (PDOT11_BSSID_LIST)Buffer;
                static const UCHAR Broadcast[CYW_ADDRESS_LENGTH] =
                    { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

                if (List->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
                    List->Header.Revision != DOT11_BSSID_LIST_REVISION_1 ||
                    List->Header.Size < sizeof(DOT11_BSSID_LIST) ||
                    List->uNumOfEntries != 1 ||
                    List->uTotalNumOfEntries < List->uNumOfEntries)
                {
                    return NDIS_STATUS_INVALID_DATA;
                }
                NdisAcquireSpinLock(&Adapter->Lock);
                RtlCopyMemory(Adapter->DesiredBssid, List->BSSIDs[0],
                              CYW_ADDRESS_LENGTH);
                Adapter->HasDesiredBssid =
                    !RtlEqualMemory(List->BSSIDs[0], Broadcast,
                                    CYW_ADDRESS_LENGTH);
                NdisReleaseSpinLock(&Adapter->Lock);
                Request->DATA.SET_INFORMATION.BytesRead = Length;
            }
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
        {
            ULONG Algorithm;
            NDIS_STATUS Status = CywOidSetFirstAlgoId(Request, &Algorithm);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                return Status;
            }
            if (Algorithm != DOT11_AUTH_ALGO_80211_OPEN &&
                Algorithm != DOT11_AUTH_ALGO_WPA_PSK &&
                Algorithm != DOT11_AUTH_ALGO_RSNA_PSK &&
                Algorithm != DOT11_AUTH_ALGO_WPA3_SAE)
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->AuthAlgorithm = Algorithm;
            NdisReleaseSpinLock(&Adapter->Lock);
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
        {
            ULONG Algorithm;
            NDIS_STATUS Status = CywOidSetFirstAlgoId(Request, &Algorithm);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                return Status;
            }
            if (Algorithm != DOT11_CIPHER_ALGO_NONE &&
                Algorithm != DOT11_CIPHER_ALGO_WEP40 &&
                Algorithm != DOT11_CIPHER_ALGO_WEP104 &&
                Algorithm != DOT11_CIPHER_ALGO_TKIP &&
                Algorithm != DOT11_CIPHER_ALGO_CCMP)
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }
            NdisAcquireSpinLock(&Adapter->Lock);
            if (Oid == OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM)
            {
                Adapter->UnicastCipher = Algorithm;
            }
            else
            {
                Adapter->MulticastCipher = Algorithm;
            }
            NdisReleaseSpinLock(&Adapter->Lock);
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_CIPHER_KEY_MAPPING_KEY:
        {
            ULONG ArrayHeader = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer);
            ULONG KeyHeader = FIELD_OFFSET(DOT11_CIPHER_KEY_MAPPING_KEY_VALUE, ucKey);
            PDOT11_BYTE_ARRAY ByteArray;
            ULONG Offset = 0;

            if (Length < ArrayHeader + KeyHeader)
            {
                return CywOidSetInvalidLength(Request, ArrayHeader + KeyHeader);
            }
            ByteArray = (PDOT11_BYTE_ARRAY)Buffer;
            if (ByteArray->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
                ByteArray->Header.Revision !=
                    DOT11_CIPHER_KEY_MAPPING_KEY_VALUE_BYTE_ARRAY_REVISION_1 ||
                ByteArray->Header.Size < sizeof(DOT11_BYTE_ARRAY) ||
                ByteArray->uTotalNumOfBytes < ByteArray->uNumOfBytes ||
                ByteArray->uNumOfBytes > Length - ArrayHeader ||
                ByteArray->uNumOfBytes < KeyHeader)
            {
                return NDIS_STATUS_INVALID_DATA;
            }

            while (Offset < ByteArray->uNumOfBytes)
            {
                PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE Value;
                ULONG Remaining = ByteArray->uNumOfBytes - Offset;
                ULONG EntryLength;
                NTSTATUS Status;

                if (Remaining < KeyHeader)
                {
                    return NDIS_STATUS_INVALID_DATA;
                }
                Value = (PDOT11_CIPHER_KEY_MAPPING_KEY_VALUE)
                    (ByteArray->ucBuffer + Offset);
                if (Value->usKeyLength > Remaining - KeyHeader)
                {
                    return NDIS_STATUS_INVALID_DATA;
                }
                EntryLength = KeyHeader + Value->usKeyLength;
                if (Value->Direction != DOT11_DIR_INBOUND &&
                    Value->Direction != DOT11_DIR_OUTBOUND &&
                    Value->Direction != DOT11_DIR_BOTH)
                {
                    return NDIS_STATUS_INVALID_DATA;
                }
                Status = CywSetKey(Adapter, TRUE, Value->PeerMacAddr,
                                   Value->AlgorithmId, Value->bDelete,
                                   Value->ucKey, Value->usKeyLength, 0);
                if (!NT_SUCCESS(Status))
                {
                    return CywNtStatusToNdisStatus(Status);
                }
                Offset += EntryLength;
            }
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_CIPHER_DEFAULT_KEY:
        {
            ULONG Hdr = FIELD_OFFSET(DOT11_CIPHER_DEFAULT_KEY_VALUE, ucKey);
            PDOT11_CIPHER_DEFAULT_KEY_VALUE Value;
            NTSTATUS Status;

            if (Length < Hdr)
            {
                return CywOidSetInvalidLength(Request, Hdr);
            }
            Value = (PDOT11_CIPHER_DEFAULT_KEY_VALUE)Buffer;
            if (Value->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
                Value->Header.Revision != DOT11_CIPHER_DEFAULT_KEY_VALUE_REVISION_1 ||
                Value->Header.Size < sizeof(DOT11_CIPHER_DEFAULT_KEY_VALUE) ||
                Value->uKeyIndex >= DOT11_MAX_NUM_DEFAULT_KEY)
            {
                return NDIS_STATUS_INVALID_DATA;
            }
            if (!Value->bDelete && Value->usKeyLength > Length - Hdr)
            {
                return NDIS_STATUS_INVALID_DATA;
            }

            if (Value->AlgorithmId == DOT11_CIPHER_ALGO_NONE)
            {
                /* The nwifi stack seeds credentials under an Algorithm==NONE
                 * sentinel for a firmware-offloaded WPA handshake. */
                if (!Value->bDelete &&
                    Value->usKeyLength > CYW_SAE_PASSWORD_MAX)
                {
                    return NDIS_STATUS_INVALID_DATA;
                }
                NdisAcquireSpinLock(&Adapter->Lock);
                RtlSecureZeroMemory(Adapter->SaePassword, sizeof(Adapter->SaePassword));
                Adapter->SaePasswordLen = 0;
                if (!Value->bDelete)
                {
                    RtlCopyMemory(Adapter->SaePassword, Value->ucKey,
                                  Value->usKeyLength);
                    Adapter->SaePasswordLen = Value->usKeyLength;
                }
                NdisReleaseSpinLock(&Adapter->Lock);
            }
            else
            {
                Status = CywSetKey(Adapter, FALSE, NULL, Value->AlgorithmId,
                                   Value->bDelete, Value->ucKey,
                                   Value->usKeyLength, Value->uKeyIndex);
                if (!NT_SUCCESS(Status))
                {
                    return CywNtStatusToNdisStatus(Status);
                }
                if (!Value->bDelete)
                {
                    Status = CywFilCmdSet(Adapter, BRCMF_C_SET_SCB_AUTHORIZE,
                                         Adapter->ConnectedBssid,
                                         CYW_ADDRESS_LENGTH);
                    if (!NT_SUCCESS(Status))
                    {
                        return CywNtStatusToNdisStatus(Status);
                    }
                }
            }
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
            if (Length < sizeof(ULONG))
            {
                return CywOidSetInvalidLength(Request, sizeof(ULONG));
            }
            if (*(PULONG)Buffer >= DOT11_MAX_NUM_DEFAULT_KEY)
            {
                return NDIS_STATUS_INVALID_DATA;
            }
            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->DefaultKeyId = *(PULONG)Buffer;
            NdisReleaseSpinLock(&Adapter->Lock);
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_CONNECT_REQUEST:
            return CywOidConnectRequest(Adapter, Request);
        case OID_DOT11_DISCONNECT_REQUEST:
        {
            BOOLEAN WasUp;
            NTSTATUS Status;

            NdisAcquireSpinLock(&Adapter->Lock);
            WasUp = Adapter->LinkUp;
            NdisReleaseSpinLock(&Adapter->Lock);

            Status = CywDisconnect(Adapter);
            if (WasUp)
            {
                CywIndicateDisassociation(Adapter);
            }
            CywIndicateLinkState(Adapter, FALSE, 0);
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return CywNtStatusToNdisStatus(Status);
        }
        case OID_DOT11_DESIRED_BSS_TYPE:
            if (Length < sizeof(DOT11_BSS_TYPE))
            {
                return CywOidSetInvalidLength(Request, sizeof(DOT11_BSS_TYPE));
            }
            if (*(DOT11_BSS_TYPE *)Buffer != dot11_BSS_type_infrastructure)
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(DOT11_BSS_TYPE);
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_RESET_REQUEST:
        {
            PDOT11_RESET_REQUEST Reset;
            BOOLEAN WasUp;
            NTSTATUS ResetStatus;
            NDIS_STATUS Status;
            DOT11_STATUS_INDICATION Confirm;
            static const UCHAR ZeroAddress[CYW_ADDRESS_LENGTH] = { 0 };

            if (Length < sizeof(DOT11_RESET_REQUEST))
            {
                return CywOidSetInvalidLength(Request, sizeof(DOT11_RESET_REQUEST));
            }
            Reset = (PDOT11_RESET_REQUEST)Buffer;
            if (Reset->dot11ResetType < dot11_reset_type_phy ||
                Reset->dot11ResetType > dot11_reset_type_phy_and_mac ||
                (Reset->dot11ResetType != dot11_reset_type_phy &&
                 ((Reset->dot11MacAddress[0] & 1) != 0 ||
                  RtlEqualMemory(Reset->dot11MacAddress, ZeroAddress,
                                 CYW_ADDRESS_LENGTH))))
            {
                return NDIS_STATUS_INVALID_DATA;
            }

            CywAbortPendingOids(Adapter, NDIS_STATUS_REQUEST_ABORTED);

            NdisAcquireSpinLock(&Adapter->Lock);
            WasUp = Adapter->LinkUp;
            NdisReleaseSpinLock(&Adapter->Lock);

            ResetStatus = CywDisconnect(Adapter);
            if (WasUp)
            {
                CywIndicateDisassociation(Adapter);
                CywIndicateLinkState(Adapter, FALSE, 0);
            }

            if (NT_SUCCESS(ResetStatus) &&
                Reset->dot11ResetType != dot11_reset_type_phy)
            {
                ResetStatus = CywFilIovarSet(Adapter, "cur_etheraddr",
                                             Reset->dot11MacAddress,
                                             CYW_ADDRESS_LENGTH);
                if (NT_SUCCESS(ResetStatus))
                {
                    RtlCopyMemory(Adapter->CurrentAddress,
                                  Reset->dot11MacAddress,
                                  CYW_ADDRESS_LENGTH);
                }
            }

            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->DesiredSsidLength = 0;
            Adapter->HasDesiredBssid = FALSE;
            RtlSecureZeroMemory(Adapter->SaePassword, sizeof(Adapter->SaePassword));
            Adapter->SaePasswordLen = 0;
            Adapter->DefaultKeyId = 0;
            if (Reset->bSetDefaultMIB)
            {
                Adapter->BssCount = 0;
                Adapter->AuthAlgorithm = DOT11_AUTH_ALGO_80211_OPEN;
                Adapter->UnicastCipher = DOT11_CIPHER_ALGO_NONE;
                Adapter->MulticastCipher = DOT11_CIPHER_ALGO_NONE;
            }
            NdisReleaseSpinLock(&Adapter->Lock);
            InterlockedExchange64(&Adapter->TxOkCount, 0);
            InterlockedExchange64(&Adapter->TxErrCount, 0);
            InterlockedExchange64(&Adapter->RxOkCount, 0);

            Status = CywNtStatusToNdisStatus(ResetStatus);
            RtlZeroMemory(&Confirm, sizeof(Confirm));
            Confirm.uStatusType = DOT11_STATUS_RESET_CONFIRM;
            Confirm.ndisStatus = Status;
            CywIndicateDot11Status(Adapter, NDIS_STATUS_MEDIA_SPECIFIC_INDICATION,
                                   &Confirm, sizeof(Confirm));
            Request->DATA.SET_INFORMATION.BytesRead = sizeof(DOT11_RESET_REQUEST);
            return Status;
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

    if (InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
    {
        return NDIS_STATUS_NOT_ACCEPTED;
    }

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return CywOidQuery(Adapter, OidRequest);
        case NdisRequestSetInformation:
            return CywOidSet(Adapter, OidRequest);
        case NdisRequestMethod:
            OidRequest->DATA.METHOD_INFORMATION.BytesWritten = 0;
            OidRequest->DATA.METHOD_INFORMATION.BytesRead = 0;
            OidRequest->DATA.METHOD_INFORMATION.BytesNeeded = 0;
            return NDIS_STATUS_NOT_SUPPORTED;
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
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;
    PNDIS_OID_REQUEST ScanRequest = NULL;
    PNDIS_OID_REQUEST ConnectRequest = NULL;

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->PendingScanOid != NULL &&
        Adapter->PendingScanOid->RequestId == RequestId)
    {
        ScanRequest = Adapter->PendingScanOid;
        Adapter->PendingScanOid = NULL;
        Adapter->ScanInProgress = FALSE;
        Adapter->ScanSeq++;
    }
    if (Adapter->PendingConnectOid != NULL &&
        Adapter->PendingConnectOid->RequestId == RequestId)
    {
        ConnectRequest = Adapter->PendingConnectOid;
        Adapter->PendingConnectOid = NULL;
        Adapter->ConnectSeq++;
    }
    NdisReleaseSpinLock(&Adapter->Lock);

    CywCompleteDetachedOids(Adapter, ScanRequest, ConnectRequest,
                            NDIS_STATUS_REQUEST_ABORTED);
}
