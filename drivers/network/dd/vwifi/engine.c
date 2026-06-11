/*
 * PROJECT:     ReactOS Virtual Native 802.11 (dot11) Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Simulated scan/connect engine and dot11 status indications
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * OID requests schedule a deferred transition through a one-shot NDIS timer
 * (DPC), which queues an NDIS I/O work item; the work item runs at
 * PASSIVE_LEVEL and produces the NDIS_STATUS_DOT11_* indications.
 */

#include "vwifi.h"
#include <debug.h>

/* IEEE 802.11 information-element ids. */
#define VWIFI_IE_SSID               0
#define VWIFI_IE_SUPPORTED_RATES    1
#define VWIFI_IE_DS_PARAMETER_SET   3

/* 802.11 capability bits. */
#define VWIFI_CAP_ESS               0x0001
#define VWIFI_CAP_PRIVACY           0x0010
#define VWIFI_CAP_SHORT_PREAMBLE    0x0020

/* 2.4 GHz channel number to centre frequency in MHz. */
static
ULONG
VWifiChannelToFrequency(
    IN ULONG Channel)
{
    if (Channel == 0)
        return 0;
    if (Channel == 14)
        return 2484;
    return 2407 + (Channel * 5);
}

VOID
VWifiInitFakeBssList(
    IN PVWIFI_ADAPTER Adapter)
{
    PVWIFI_FAKE_BSS Bss;

    Adapter->BssCount = 0;

    /* "ReactOS-Open": an open infrastructure AP on channel 1. */
    Bss = &Adapter->Bss[Adapter->BssCount++];
    Bss->Bssid[0] = 0x02; Bss->Bssid[1] = 0x11; Bss->Bssid[2] = 0x22;
    Bss->Bssid[3] = 0x33; Bss->Bssid[4] = 0x44; Bss->Bssid[5] = 0x55;
    Bss->BssType = dot11_BSS_type_infrastructure;
    RtlCopyMemory(Bss->Ssid, "ReactOS-Open", sizeof("ReactOS-Open") - 1);
    Bss->SsidLength = sizeof("ReactOS-Open") - 1;
    Bss->ChannelNumber = 1;
    Bss->ChCenterFrequency = VWifiChannelToFrequency(1);    /* 2412 MHz */
    Bss->Rssi = -40;
    Bss->LinkQuality = 90;
    Bss->CapabilityInformation = VWIFI_CAP_ESS | VWIFI_CAP_SHORT_PREAMBLE;
    Bss->AuthAlgorithm = DOT11_AUTH_ALGO_80211_OPEN;

    /* "ReactOS-WPA2": a secured AP shown in scans; the connect engine
     * rejects association to it (no WPA2 support). */
    Bss = &Adapter->Bss[Adapter->BssCount++];
    Bss->Bssid[0] = 0x02; Bss->Bssid[1] = 0x11; Bss->Bssid[2] = 0x22;
    Bss->Bssid[3] = 0x33; Bss->Bssid[4] = 0x44; Bss->Bssid[5] = 0x66;
    Bss->BssType = dot11_BSS_type_infrastructure;
    RtlCopyMemory(Bss->Ssid, "ReactOS-WPA2", sizeof("ReactOS-WPA2") - 1);
    Bss->SsidLength = sizeof("ReactOS-WPA2") - 1;
    Bss->ChannelNumber = 6;
    Bss->ChCenterFrequency = VWifiChannelToFrequency(6);    /* 2437 MHz */
    Bss->Rssi = -55;
    Bss->LinkQuality = 70;
    Bss->CapabilityInformation = VWIFI_CAP_ESS | VWIFI_CAP_PRIVACY |
                                 VWIFI_CAP_SHORT_PREAMBLE;
    Bss->AuthAlgorithm = DOT11_AUTH_ALGO_RSNA_PSK;
}

/* Build the minimal beacon-IE blob (SSID, supported rates, DS parameters) for
 * a fake BSS.  nwifi parses the SSID IE out of this blob, so it must be a
 * well-formed sequence of {id,len,value} elements. */
static
ULONG
VWifiBuildBeaconIes(
    IN PVWIFI_FAKE_BSS Bss,
    OUT PUCHAR Buffer,
    IN ULONG BufferLength)
{
    /* 802.11b/g supported rates, in 500 kbit/s units; high bit = basic rate.
     * 1, 2, 5.5, 11 (basic) + 6, 9, 12, 18 Mbit/s. */
    static const UCHAR Rates[] = { 0x82, 0x84, 0x8B, 0x96, 0x0C, 0x12, 0x18, 0x24 };
    ULONG Offset = 0;

    if (Offset + 2 + Bss->SsidLength > BufferLength)
        return Offset;
    Buffer[Offset++] = VWIFI_IE_SSID;
    Buffer[Offset++] = (UCHAR)Bss->SsidLength;
    RtlCopyMemory(&Buffer[Offset], Bss->Ssid, Bss->SsidLength);
    Offset += Bss->SsidLength;

    if (Offset + 2 + sizeof(Rates) > BufferLength)
        return Offset;
    Buffer[Offset++] = VWIFI_IE_SUPPORTED_RATES;
    Buffer[Offset++] = (UCHAR)sizeof(Rates);
    RtlCopyMemory(&Buffer[Offset], Rates, sizeof(Rates));
    Offset += sizeof(Rates);

    if (Offset + 3 > BufferLength)
        return Offset;
    Buffer[Offset++] = VWIFI_IE_DS_PARAMETER_SET;
    Buffer[Offset++] = 1;
    Buffer[Offset++] = (UCHAR)Bss->ChannelNumber;

    return Offset;
}

/* Serialize the fake BSS database into the DOT11_BYTE_ARRAY of packed
 * variable-length DOT11_BSS_ENTRY records returned by OID_DOT11_ENUM_BSS_LIST.
 * Returns the bytes written; *BytesNeeded carries the full size required. */
ULONG
VWifiBuildBssList(
    IN PVWIFI_ADAPTER Adapter,
    OUT PUCHAR Buffer,
    IN ULONG BufferLength,
    OUT PULONG BytesNeeded)
{
    UCHAR IeScratch[VWIFI_MAX_BSS_IE];
    ULONG IeLengths[VWIFI_MAX_BSS];
    ULONG i;
    ULONG PayloadSize = 0;
    ULONG TotalSize;
    PDOT11_BYTE_ARRAY ByteArray;
    PUCHAR Cursor;
    ULONG64 Now;

    /* First pass: size the packed payload. */
    for (i = 0; i < Adapter->BssCount; i++)
    {
        IeLengths[i] = VWifiBuildBeaconIes(&Adapter->Bss[i], IeScratch,
                                           sizeof(IeScratch));
        PayloadSize += FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + IeLengths[i];
    }

    TotalSize = FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer) + PayloadSize;
    *BytesNeeded = TotalSize;

    if (Buffer == NULL || BufferLength < TotalSize)
    {
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

    for (i = 0; i < Adapter->BssCount; i++)
    {
        PVWIFI_FAKE_BSS Src = &Adapter->Bss[i];
        PDOT11_BSS_ENTRY Entry = (PDOT11_BSS_ENTRY)Cursor;
        ULONG IeLen = IeLengths[i];

        RtlZeroMemory(Entry, FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer));
        Entry->uPhyId = 0;
        Entry->PhySpecificInfo.uChCenterFrequency = Src->ChCenterFrequency;
        RtlCopyMemory(Entry->dot11BSSID, Src->Bssid, VWIFI_ADDRESS_LENGTH);
        Entry->dot11BSSType = Src->BssType;
        Entry->lRSSI = Src->Rssi;
        Entry->uLinkQuality = Src->LinkQuality;
        Entry->bInRegDomain = TRUE;
        Entry->usBeaconPeriod = (USHORT)Adapter->Dot11.BeaconPeriod;
        Entry->ullTimestamp = Now;
        Entry->ullHostTimestamp = Now;
        Entry->usCapabilityInformation = Src->CapabilityInformation;
        Entry->uBufferLength = IeLen;

        (VOID)VWifiBuildBeaconIes(Src, Entry->ucBuffer, IeLen);

        Cursor += FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + IeLen;
    }

    return TotalSize;
}

/* Resolve the desired SSID/BSSID stored from the connect prologue to an index
 * into the fake BSS list, or -1 if no BSS matches. */
LONG
VWifiFindDesiredBss(
    IN PVWIFI_ADAPTER Adapter)
{
    ULONG i;
    BOOLEAN WantSsid = Adapter->Dot11.HaveDesiredSsid &&
                       Adapter->Dot11.DesiredSsid.uSSIDLength != 0;
    BOOLEAN WantBssid = Adapter->Dot11.HaveDesiredBssid;
    static const UCHAR Broadcast[VWIFI_ADDRESS_LENGTH] =
        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    for (i = 0; i < Adapter->BssCount; i++)
    {
        PVWIFI_FAKE_BSS Bss = &Adapter->Bss[i];

        if (WantSsid)
        {
            if (Adapter->Dot11.DesiredSsid.uSSIDLength != Bss->SsidLength ||
                RtlCompareMemory(Adapter->Dot11.DesiredSsid.ucSSID, Bss->Ssid,
                                 Bss->SsidLength) != Bss->SsidLength)
            {
                continue;
            }
        }

        if (WantBssid &&
            RtlCompareMemory(Adapter->Dot11.DesiredBssid, Broadcast,
                             VWIFI_ADDRESS_LENGTH) != VWIFI_ADDRESS_LENGTH)
        {
            if (RtlCompareMemory(Adapter->Dot11.DesiredBssid, Bss->Bssid,
                                 VWIFI_ADDRESS_LENGTH) != VWIFI_ADDRESS_LENGTH)
            {
                continue;
            }
        }

        /* No SSID and no specific BSSID requested matches the first BSS. */
        return (LONG)i;
    }

    return -1;
}

static
VOID
VWifiIndicateDot11Status(
    IN PVWIFI_ADAPTER Adapter,
    IN NDIS_STATUS StatusCode,
    IN PVOID Payload,
    IN ULONG PayloadSize)
{
    NDIS_STATUS_INDICATION Indication;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(NDIS_STATUS_INDICATION);
    Indication.SourceHandle = Adapter->MiniportAdapterHandle;
    Indication.StatusCode = StatusCode;
    Indication.StatusBuffer = Payload;
    Indication.StatusBufferSize = PayloadSize;

    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);
}

VOID
VWifiIndicateScanConfirm(
    IN PVWIFI_ADAPTER Adapter,
    IN NDIS_STATUS ScanStatus)
{
    DOT11_STATUS_INDICATION Confirm;

    RtlZeroMemory(&Confirm, sizeof(Confirm));
    Confirm.uStatusType = DOT11_STATUS_SCAN_CONFIRM;
    Confirm.ndisStatus = ScanStatus;

    VWifiIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_SCAN_CONFIRM,
                             &Confirm, sizeof(Confirm));
}

VOID
VWifiIndicateAssocStart(
    IN PVWIFI_ADAPTER Adapter,
    IN PVWIFI_FAKE_BSS Bss)
{
    DOT11_ASSOCIATION_START_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_ASSOCIATION_START_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(DOT11_ASSOCIATION_START_PARAMETERS);
    RtlCopyMemory(Params.MacAddr, Bss->Bssid, VWIFI_ADDRESS_LENGTH);
    Params.SSID.uSSIDLength = Bss->SsidLength;
    RtlCopyMemory(Params.SSID.ucSSID, Bss->Ssid, Bss->SsidLength);
    Params.uIHVDataOffset = 0;
    Params.uIHVDataSize = 0;

    VWifiIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_ASSOCIATION_START,
                             &Params, sizeof(Params));
}

VOID
VWifiIndicateAssocComplete(
    IN PVWIFI_ADAPTER Adapter,
    IN PVWIFI_FAKE_BSS Bss,
    IN DOT11_ASSOC_STATUS Status)
{
    DOT11_ASSOCIATION_COMPLETION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_ASSOCIATION_COMPLETION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(DOT11_ASSOCIATION_COMPLETION_PARAMETERS);
    RtlCopyMemory(Params.MacAddr, Bss->Bssid, VWIFI_ADDRESS_LENGTH);
    Params.uStatus = Status;

    if (Status == DOT11_ASSOC_STATUS_SUCCESS)
    {
        Params.bReAssocReq = FALSE;
        Params.bReAssocResp = FALSE;
        Params.AuthAlgo = Adapter->Dot11.AuthAlgorithm;
        Params.UnicastCipher = Adapter->Dot11.UnicastCipher;
        Params.MulticastCipher = Adapter->Dot11.MulticastCipher;
        Params.bFourAddressSupported = FALSE;
        /* Open networks have an authorized port the moment they associate;
         * for a secured link nwifi keeps the port unauthorized until the
         * supplicant completes the handshake. */
        Params.bPortAuthorized =
            (Adapter->Dot11.AuthAlgorithm == DOT11_AUTH_ALGO_80211_OPEN);
        Params.ucActiveQoSProtocol = 0;
        Params.DSInfo = DOT11_DS_UNKNOWN;
    }

    VWifiIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION,
                             &Params, sizeof(Params));
}

VOID
VWifiIndicateConnectComplete(
    IN PVWIFI_ADAPTER Adapter,
    IN DOT11_ASSOC_STATUS Status)
{
    DOT11_CONNECTION_COMPLETION_PARAMETERS Params;

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_CONNECTION_COMPLETION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(DOT11_CONNECTION_COMPLETION_PARAMETERS);
    Params.uStatus = Status;

    VWifiIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_CONNECTION_COMPLETION,
                             &Params, sizeof(Params));
}

VOID
VWifiIndicateDisassociation(
    IN PVWIFI_ADAPTER Adapter,
    IN DOT11_ASSOC_STATUS Reason)
{
    DOT11_DISASSOCIATION_PARAMETERS Params;
    UCHAR Bssid[VWIFI_ADDRESS_LENGTH] = { 0 };

    if (Adapter->ConnState == VWifiConnected &&
        Adapter->ConnectedBssIndex < Adapter->BssCount)
    {
        RtlCopyMemory(Bssid, Adapter->Bss[Adapter->ConnectedBssIndex].Bssid,
                      VWIFI_ADDRESS_LENGTH);
    }

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Params.Header.Revision = DOT11_DISASSOCIATION_PARAMETERS_REVISION_1;
    Params.Header.Size = sizeof(DOT11_DISASSOCIATION_PARAMETERS);
    RtlCopyMemory(Params.MacAddr, Bssid, VWIFI_ADDRESS_LENGTH);
    Params.uReason = Reason;

    VWifiIndicateDot11Status(Adapter, NDIS_STATUS_DOT11_DISASSOCIATION,
                             &Params, sizeof(Params));
}

/* A Native 802.11 miniport still reports MediaConnectState; nwifi uses it to
 * raise its own virtual-802.3 link. */
VOID
VWifiIndicateLinkState(
    IN PVWIFI_ADAPTER Adapter,
    IN BOOLEAN Connected)
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
    if (Connected)
    {
        LinkState.XmitLinkSpeed = VWIFI_LINK_SPEED_BPS;
        LinkState.RcvLinkSpeed = VWIFI_LINK_SPEED_BPS;
    }
    else
    {
        LinkState.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
        LinkState.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    }
    LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;

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

/* Schedule a deferred job: a one-shot timer fires after DelayMs and queues the
 * work item.  Only one job is in flight at a time (the upper edge serializes
 * scan/connect/disconnect requests). */
VOID
VWifiScheduleJob(
    IN PVWIFI_ADAPTER Adapter,
    IN VWIFI_JOB Job,
    IN ULONG DelayMs)
{
    LARGE_INTEGER Due;

    NdisAcquireSpinLock(&Adapter->Lock);
    Adapter->PendingJob = Job;
    NdisReleaseSpinLock(&Adapter->Lock);

    Due.QuadPart = Int32x32To64((LONG)DelayMs, -10000);     /* relative, ms */
    NdisSetTimerObject(Adapter->EngineTimer, Due, 0, NULL);
}

VOID
NTAPI
VWifiEngineTimerDpc(
    IN PVOID SystemSpecific1,
    IN PVOID FunctionContext,
    IN PVOID SystemSpecific2,
    IN PVOID SystemSpecific3)
{
    PVWIFI_ADAPTER Adapter = (PVWIFI_ADAPTER)FunctionContext;

    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);

    if (Adapter->Halting)
    {
        return;
    }

    /* Hand off to PASSIVE_LEVEL: indications are produced from the work item. */
    if (InterlockedCompareExchange(&Adapter->WorkPending, 1, 0) == 0)
    {
        NdisQueueIoWorkItem(Adapter->EngineWorkItem, VWifiEngineWorker, Adapter);
    }
}

VOID
NTAPI
VWifiEngineWorker(
    IN PVOID WorkItemContext,
    IN NDIS_HANDLE NdisIoWorkItemHandle)
{
    PVWIFI_ADAPTER Adapter = (PVWIFI_ADAPTER)WorkItemContext;
    VWIFI_JOB Job;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    NdisAcquireSpinLock(&Adapter->Lock);
    Job = Adapter->PendingJob;
    Adapter->PendingJob = VWifiJobNone;
    NdisReleaseSpinLock(&Adapter->Lock);

    if (Adapter->Halting)
    {
        InterlockedExchange(&Adapter->WorkPending, 0);
        return;
    }

    switch (Job)
    {
        case VWifiJobScan:
        {
            NdisAcquireSpinLock(&Adapter->Lock);
            if (Adapter->ConnState == VWifiScanning)
                Adapter->ConnState = VWifiDisconnected;
            NdisReleaseSpinLock(&Adapter->Lock);

            DPRINT1("VWIFI: scan complete -> SCAN_CONFIRM\n");
            VWifiIndicateScanConfirm(Adapter, NDIS_STATUS_SUCCESS);

            /* Complete the SCAN_REQUEST that returned NDIS_STATUS_PENDING. */
            {
                PNDIS_OID_REQUEST ScanOid;
                NdisAcquireSpinLock(&Adapter->Lock);
                ScanOid = Adapter->PendingScanOid;
                Adapter->PendingScanOid = NULL;
                NdisReleaseSpinLock(&Adapter->Lock);
                if (ScanOid != NULL && Adapter->MiniportAdapterHandle != NULL)
                {
                    NdisMOidRequestComplete(Adapter->MiniportAdapterHandle,
                                            ScanOid, NDIS_STATUS_SUCCESS);
                }
            }
            break;
        }

        case VWifiJobConnect:
        {
            LONG BssIndex = VWifiFindDesiredBss(Adapter);

            if (BssIndex < 0)
            {
                DPRINT1("VWIFI: connect: no matching BSS -> failure\n");
                NdisAcquireSpinLock(&Adapter->Lock);
                Adapter->ConnState = VWifiDisconnected;
                NdisReleaseSpinLock(&Adapter->Lock);
                VWifiIndicateConnectComplete(Adapter,
                                             DOT11_ASSOC_STATUS_CANDIDATE_LIST_EXHAUSTED);
                break;
            }

            {
                PVWIFI_FAKE_BSS Bss = &Adapter->Bss[BssIndex];

                /* Association completes only for open networks; WPA2 is not
                 * implemented, so a secured BSS reports failure. */
                if (Bss->AuthAlgorithm != DOT11_AUTH_ALGO_80211_OPEN &&
                    Adapter->Dot11.AuthAlgorithm == DOT11_AUTH_ALGO_80211_OPEN)
                {
                    DPRINT1("VWIFI: connect: BSS requires security (stubbed)\n");
                    NdisAcquireSpinLock(&Adapter->Lock);
                    Adapter->ConnState = VWifiDisconnected;
                    NdisReleaseSpinLock(&Adapter->Lock);
                    VWifiIndicateAssocComplete(Adapter, Bss,
                                               DOT11_ASSOC_STATUS_FAILURE);
                    VWifiIndicateConnectComplete(Adapter,
                                                 DOT11_ASSOC_STATUS_FAILURE);
                    break;
                }

                DPRINT1("VWIFI: connect -> assoc-start/complete/connect-complete\n");

                NdisAcquireSpinLock(&Adapter->Lock);
                Adapter->ConnState = VWifiAssociating;
                NdisReleaseSpinLock(&Adapter->Lock);
                VWifiIndicateAssocStart(Adapter, Bss);

                VWifiIndicateAssocComplete(Adapter, Bss,
                                           DOT11_ASSOC_STATUS_SUCCESS);

                VWifiIndicateConnectComplete(Adapter,
                                             DOT11_CONNECTION_STATUS_SUCCESS);

                NdisAcquireSpinLock(&Adapter->Lock);
                Adapter->ConnState = VWifiConnected;
                Adapter->ConnectedBssIndex = (ULONG)BssIndex;
                Adapter->Dot11.CurrentChannel = Bss->ChannelNumber;
                NdisReleaseSpinLock(&Adapter->Lock);

                VWifiIndicateLinkState(Adapter, TRUE);
            }
            break;
        }

        case VWifiJobDisconnect:
        {
            DPRINT1("VWIFI: disconnect -> DISASSOCIATION + link down\n");
            VWifiIndicateDisassociation(Adapter,
                                        DOT11_DISASSOC_REASON_OS);

            NdisAcquireSpinLock(&Adapter->Lock);
            Adapter->ConnState = VWifiDisconnected;
            NdisReleaseSpinLock(&Adapter->Lock);

            VWifiIndicateLinkState(Adapter, FALSE);
            break;
        }

        case VWifiJobNone:
        default:
            break;
    }

    InterlockedExchange(&Adapter->WorkPending, 0);
}
