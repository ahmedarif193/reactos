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
    UCHAR IeScratch[CYW_MAX_BSS_IE];
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
        IeLengths[i] = CywBuildBeaconIes(&Adapter->Bss[i], IeScratch, sizeof(IeScratch));
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

        (VOID)CywBuildBeaconIes(Src, Entry->ucBuffer, IeLen);

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
    RtlCopyMemory(Params.MacAddr, Adapter->ConnectedBssid, CYW_ADDRESS_LENGTH);
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
    _In_ BOOLEAN Connected)
{
    NDIS_LINK_STATE LinkState;
    NDIS_STATUS_INDICATION Indication;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    LinkState.MediaConnectState =
        Connected ? MediaConnectStateConnected : MediaConnectStateDisconnected;
    LinkState.MediaDuplexState = MediaDuplexStateFull;
    LinkState.XmitLinkSpeed =
        Connected ? CYW_LINK_SPEED_BPS : NDIS_LINK_SPEED_UNKNOWN;
    LinkState.RcvLinkSpeed =
        Connected ? CYW_LINK_SPEED_BPS : NDIS_LINK_SPEED_UNKNOWN;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(NDIS_STATUS_INDICATION);
    Indication.SourceHandle = Adapter->MiniportAdapterHandle;
    Indication.StatusCode = NDIS_STATUS_LINK_STATE;
    Indication.StatusBuffer = &LinkState;
    Indication.StatusBufferSize = sizeof(LinkState);

    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);
}

VOID
CywIndicateScanComplete(
    _In_ PCYW_ADAPTER Adapter,
    _In_ NDIS_STATUS ScanStatus)
{
    NDIS_STATUS_INDICATION Indication;
    DOT11_STATUS_INDICATION Confirm;
    BOOLEAN WasInProgress;

    NdisAcquireSpinLock(&Adapter->Lock);
    WasInProgress = Adapter->ScanInProgress;
    Adapter->ScanInProgress = FALSE;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (!WasInProgress)
    {
        return;
    }

    RtlZeroMemory(&Confirm, sizeof(Confirm));
    Confirm.uStatusType = DOT11_STATUS_SCAN_CONFIRM;
    Confirm.ndisStatus = ScanStatus;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(NDIS_STATUS_INDICATION);
    Indication.SourceHandle = Adapter->MiniportAdapterHandle;
    Indication.StatusCode = NDIS_STATUS_DOT11_SCAN_CONFIRM;
    Indication.StatusBuffer = &Confirm;
    Indication.StatusBufferSize = sizeof(Confirm);

    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);
}

static
VOID
CywScanWorker(
    _In_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)WorkItemContext;
    PNDIS_OID_REQUEST Request;
    LARGE_INTEGER Delay;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    Delay.QuadPart = -40000000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    NdisAcquireSpinLock(&Adapter->Lock);
    Request = Adapter->PendingScanOid;
    Adapter->PendingScanOid = NULL;
    NdisReleaseSpinLock(&Adapter->Lock);

    CywIndicateScanComplete(Adapter, NDIS_STATUS_SUCCESS);

    if (Request != NULL)
    {
        NdisMOidRequestComplete(Adapter->MiniportAdapterHandle, Request, NDIS_STATUS_SUCCESS);
    }
}

static
NDIS_STATUS
CywOidScanRequest(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NTSTATUS Status;

    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->PendingScanOid != NULL)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return NDIS_STATUS_DOT11_MEDIA_IN_USE;
    }
    Adapter->PendingScanOid = Request;
    NdisReleaseSpinLock(&Adapter->Lock);

    Status = CywScanStart(Adapter, NULL);
    if (!NT_SUCCESS(Status))
    {
        NdisAcquireSpinLock(&Adapter->Lock);
        Adapter->PendingScanOid = NULL;
        NdisReleaseSpinLock(&Adapter->Lock);
        return NDIS_STATUS_FAILURE;
    }

    NdisQueueIoWorkItem(Adapter->InterruptWorkItem, CywScanWorker, Adapter);
    return NDIS_STATUS_PENDING;
}

static
VOID
CywConnectWorker(
    _In_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)WorkItemContext;
    PNDIS_OID_REQUEST Request;
    LARGE_INTEGER Delay;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    CywIndicateAssocStart(Adapter);
    CywConnect(Adapter);

    Delay.QuadPart = -30000000;
    KeDelayExecutionThread(KernelMode, FALSE, &Delay);

    NdisAcquireSpinLock(&Adapter->Lock);
    Request = Adapter->PendingConnectOid;
    Adapter->PendingConnectOid = NULL;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (Request != NULL)
    {
        NdisMOidRequestComplete(Adapter->MiniportAdapterHandle, Request,
                                NDIS_STATUS_SUCCESS);
    }
}

static
NDIS_STATUS
CywOidConnectRequest(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST Request)
{
    NdisAcquireSpinLock(&Adapter->Lock);
    if (Adapter->PendingConnectOid != NULL)
    {
        NdisReleaseSpinLock(&Adapter->Lock);
        return NDIS_STATUS_DOT11_MEDIA_IN_USE;
    }
    Adapter->PendingConnectOid = Request;
    NdisReleaseSpinLock(&Adapter->Lock);

    NdisQueueIoWorkItem(Adapter->InterruptWorkItem, CywConnectWorker, Adapter);
    return NDIS_STATUS_PENDING;
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
            return CywOidQueryUlong(Request, CYW_LINK_SPEED_BPS / 100);
        case OID_GEN_MEDIA_CONNECT_STATUS:
            return CywOidQueryUlong(Request, MediaConnectStateDisconnected);
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

    DPRINT1("CYW: set %s key index %lu len %lu\n",
            Pairwise ? "pairwise" : "group", KeyIndex, KeyLen);
    return CywFilIovarSet(Adapter, "wsec_key", &KeyData, sizeof(KeyData));
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
                Adapter->RadioOn = *(PBOOLEAN)Buffer;
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
        case OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM:
            if (Length >= sizeof(DOT11_AUTH_ALGORITHM_LIST))
            {
                PDOT11_AUTH_ALGORITHM_LIST List = (PDOT11_AUTH_ALGORITHM_LIST)Buffer;
                if (List->uNumOfEntries >= 1)
                {
                    Adapter->AuthAlgorithm = List->AlgorithmIds[0];
                }
            }
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM:
            if (Length >= sizeof(DOT11_CIPHER_ALGORITHM_LIST))
            {
                PDOT11_CIPHER_ALGORITHM_LIST List = (PDOT11_CIPHER_ALGORITHM_LIST)Buffer;
                if (List->uNumOfEntries >= 1)
                {
                    Adapter->UnicastCipher = List->AlgorithmIds[0];
                }
            }
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM:
            if (Length >= sizeof(DOT11_CIPHER_ALGORITHM_LIST))
            {
                PDOT11_CIPHER_ALGORITHM_LIST List = (PDOT11_CIPHER_ALGORITHM_LIST)Buffer;
                if (List->uNumOfEntries >= 1)
                {
                    Adapter->MulticastCipher = List->AlgorithmIds[0];
                }
            }
            return NDIS_STATUS_SUCCESS;
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
                    CywSetKey(Adapter, FALSE, NULL, Value->ucKey,
                              Value->usKeyLength, Value->uKeyIndex);
                }
            }
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        }
        case OID_DOT11_CIPHER_DEFAULT_KEY_ID:
            Request->DATA.SET_INFORMATION.BytesRead = Length;
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_CONNECT_REQUEST:
            return CywOidConnectRequest(Adapter, Request);
        case OID_DOT11_DISCONNECT_REQUEST:
            CywDisconnect(Adapter);
            CywIndicateLinkState(Adapter, FALSE);
            return NDIS_STATUS_SUCCESS;
        case OID_DOT11_DESIRED_BSS_TYPE:
        case OID_DOT11_RESET_REQUEST:
            return NDIS_STATUS_SUCCESS;
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
