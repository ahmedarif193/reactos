/*
 * PROJECT:     ReactOS Native WiFi (802.11) stack
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     MSM (station management state machine)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "nwifi.h"

#define NDEBUG
#include <debug.h>

/* Forward declarations. */
static VOID NwifiAggregateBssList(_In_ PNWIFI_MSM Msm);

/* ===========================================================================
 *  Small helpers
 * ===========================================================================
 */
static
__inline
PNWIFI_MSM
NwifiMsmFromAdapter(
    _In_ PNWIFI_ADAPTER Adapter)
{
    return (PNWIFI_MSM)Adapter->MsmContext;
}

static
BOOLEAN
NwifiMacIsZero(
    _In_ PUCHAR Mac)
{
    ULONG i;
    for (i = 0; i < IEEE80211_ADDR_LEN; i++)
    {
        if (Mac[i] != 0)
            return FALSE;
    }
    return TRUE;
}

/* TODO: map the lower miniport's PHY id to a real DOT11_PHY_TYPE; report
 * ERP (802.11g) for now. */
static
DOT11_PHY_TYPE
NwifiPhyTypeFromId(
    _In_ ULONG PhyId)
{
    UNREFERENCED_PARAMETER(PhyId);
    return dot11_phy_type_erp;
}

/* ===========================================================================
 *  MSM lifecycle
 * ===========================================================================
 */
NDIS_STATUS
NwifiMsmCreate(
    _In_ PNWIFI_ADAPTER Adapter)
{
    PNWIFI_MSM Msm;

    Msm = (PNWIFI_MSM)NwifiAllocate(sizeof(NWIFI_MSM));
    if (Msm == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    Msm->Adapter = Adapter;
    NdisAllocateSpinLock(&Msm->Lock);
    Msm->State = NwifiMsmIdle;
    Msm->BssCount = 0;
    Msm->Supplicant = NULL;
    Msm->LinkSpeedKbps = (ULONG)(NWIFI_DEFAULT_LINK_SPEED / 1000ULL);
    KeInitializeSpinLock(&Msm->WorkItemLock);
    KeInitializeEvent(&Msm->WorkItemsDrainedEvent, NotificationEvent, TRUE);

    Adapter->MsmContext = Msm;
    return NDIS_STATUS_SUCCESS;
}

BOOLEAN
NwifiMsmQueueWorkItem(
    _In_ PNWIFI_MSM Msm,
    _In_ NDIS_IO_WORKITEM_ROUTINE Routine,
    _In_opt_ PVOID Context)
{
    NDIS_HANDLE WorkItem;
    KIRQL OldIrql;
    BOOLEAN Queue = FALSE;

    if (Routine == NULL ||
        gNwifi.MiniportDriverHandle == NULL)
    {
        return FALSE;
    }

    WorkItem = NdisAllocateIoWorkItem(gNwifi.MiniportDriverHandle);
    if (WorkItem == NULL)
    {
        return FALSE;
    }

    KeAcquireSpinLock(&Msm->WorkItemLock, &OldIrql);
    if (Msm->WorkItemsHalting == 0 && Msm->WorkItemsPending < MAXLONG)
    {
        if (Msm->WorkItemsPending++ == 0)
        {
            KeResetEvent(&Msm->WorkItemsDrainedEvent);
        }
        Queue = TRUE;
    }
    KeReleaseSpinLock(&Msm->WorkItemLock, OldIrql);

    if (Queue)
    {
        NdisQueueIoWorkItem(WorkItem, Routine, Context);
    }
    else
    {
        NdisFreeIoWorkItem(WorkItem);
    }
    return Queue;
}

VOID
NwifiMsmCompleteWorkItem(
    _In_ PNWIFI_MSM Msm,
    _In_ NDIS_HANDLE WorkItem)
{
    KIRQL OldIrql;

    NdisFreeIoWorkItem(WorkItem);

    KeAcquireSpinLock(&Msm->WorkItemLock, &OldIrql);
    ASSERT(Msm->WorkItemsPending > 0);
    if (Msm->WorkItemsPending > 0 && --Msm->WorkItemsPending == 0)
    {
        KeSetEvent(&Msm->WorkItemsDrainedEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Msm->WorkItemLock, OldIrql);
}

VOID
NwifiMsmDestroy(
    _In_ PNWIFI_ADAPTER Adapter)
{
    PNWIFI_MSM Msm = NwifiMsmFromAdapter(Adapter);
    KIRQL OldIrql;

    if (Msm == NULL)
    {
        return;
    }

    InterlockedExchangePointer((PVOID volatile *)&Adapter->MsmContext, NULL);
    KeAcquireSpinLock(&Msm->WorkItemLock, &OldIrql);
    Msm->WorkItemsHalting = 1;
    KeReleaseSpinLock(&Msm->WorkItemLock, OldIrql);
    NwifiSupplicantStop(Msm);
    KeWaitForSingleObject(&Msm->WorkItemsDrainedEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
    NwifiSupplicantFree(Msm);
    NdisFreeSpinLock(&Msm->Lock);
    NwifiFree(Msm);
}

/* Deferred (PASSIVE_LEVEL) scan-confirm handler. */
static
VOID
NTAPI
NwifiMsmScanWorker(
    _In_opt_ PVOID WorkItemContext,
    _In_     NDIS_HANDLE NdisIoWorkItemHandle)
{
    PNWIFI_MSM Msm = (PNWIFI_MSM)WorkItemContext;
    NDIS_STATUS ScanStatus;

    if (Msm == NULL)
    {
        return;
    }

    NwifiAggregateBssList(Msm);

    NdisAcquireSpinLock(&Msm->Lock);
    Msm->ScanInProgress = FALSE;
    if (Msm->State == NwifiMsmScanning)
        Msm->State = NwifiMsmIdle;
    ScanStatus = Msm->LastScanStatus;
    InterlockedExchange(&Msm->ScanWorkQueued, 0);
    NdisReleaseSpinLock(&Msm->Lock);

    NwifiQueueNotification(Msm->Adapter->InterfaceIndex,
                           NwifiNotifyScanComplete, ScanStatus);
    NwifiMsmCompleteWorkItem(Msm, NdisIoWorkItemHandle);
}

/* ===========================================================================
 *  Programming helpers: push the desired-network OIDs to the lower miniport
 * ===========================================================================
 */

/* OID_DOT11_DESIRED_SSID_LIST: a one-entry SSID list (or empty for "any"). */
static
NDIS_STATUS
NwifiProgramDesiredSsid(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PDOT11_SSID Ssid)
{
    DOT11_SSID_LIST List;

    RtlZeroMemory(&List, sizeof(List));
    List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    List.Header.Revision = DOT11_SSID_LIST_REVISION_1;
    List.Header.Size = sizeof(DOT11_SSID_LIST);
    List.uNumOfEntries = 1;
    List.uTotalNumOfEntries = 1;
    List.SSIDs[0] = *Ssid;

    return NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                  OID_DOT11_DESIRED_SSID_LIST,
                                  &List, sizeof(List), NULL);
}

/* OID_DOT11_DESIRED_BSSID_LIST: a one-entry BSSID list, or the broadcast
 * address to mean "any BSSID". */
static
NDIS_STATUS
NwifiProgramDesiredBssid(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_opt_ PUCHAR Bssid)
{
    DOT11_BSSID_LIST List;
    static const UCHAR Broadcast[IEEE80211_ADDR_LEN] =
        { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

    RtlZeroMemory(&List, sizeof(List));
    List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    List.Header.Revision = DOT11_BSSID_LIST_REVISION_1;
    List.Header.Size = sizeof(List);
    List.uNumOfEntries = 1;
    List.uTotalNumOfEntries = 1;
    RtlCopyMemory(List.BSSIDs[0],
                  (Bssid != NULL && !NwifiMacIsZero(Bssid)) ? Bssid : Broadcast,
                  IEEE80211_ADDR_LEN);

    return NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                  OID_DOT11_DESIRED_BSSID_LIST,
                                  &List, sizeof(List),
                                  NULL);
}

/* OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM: a one-entry auth-algorithm list. */
static
NDIS_STATUS
NwifiProgramAuthAlgorithm(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ DOT11_AUTH_ALGORITHM Algorithm)
{
    DOT11_AUTH_ALGORITHM_LIST List;

    RtlZeroMemory(&List, sizeof(List));
    List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    List.Header.Revision = DOT11_AUTH_ALGORITHM_LIST_REVISION_1;
    List.Header.Size = sizeof(DOT11_AUTH_ALGORITHM_LIST);
    List.uNumOfEntries = 1;
    List.uTotalNumOfEntries = 1;
    List.AlgorithmIds[0] = Algorithm;

    return NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                  OID_DOT11_ENABLED_AUTHENTICATION_ALGORITHM,
                                  &List, sizeof(List), NULL);
}

/* OID_DOT11_ENABLED_UNICAST/MULTICAST_CIPHER_ALGORITHM. */
static
NDIS_STATUS
NwifiProgramCipherAlgorithm(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ NDIS_OID Oid,
    _In_ DOT11_CIPHER_ALGORITHM Algorithm)
{
    DOT11_CIPHER_ALGORITHM_LIST List;

    RtlZeroMemory(&List, sizeof(List));
    List.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    List.Header.Revision = DOT11_CIPHER_ALGORITHM_LIST_REVISION_1;
    List.Header.Size = sizeof(DOT11_CIPHER_ALGORITHM_LIST);
    List.uNumOfEntries = 1;
    List.uTotalNumOfEntries = 1;
    List.AlgorithmIds[0] = Algorithm;

    return NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                  Oid, &List, sizeof(List), NULL);
}

/* OID_DOT11_DESIRED_BSS_TYPE: a bare DOT11_BSS_TYPE. */
static
NDIS_STATUS
NwifiProgramBssType(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ DOT11_BSS_TYPE BssType)
{
    DOT11_BSS_TYPE Value = BssType;
    return NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                  OID_DOT11_DESIRED_BSS_TYPE,
                                  &Value, sizeof(Value), NULL);
}

/* ===========================================================================
 *  Scan
 * ===========================================================================
 */
NDIS_STATUS
NwifiMsmStartScan(
    _In_ PNWIFI_MSM Msm,
    _In_ DOT11_BSS_TYPE BssType,
    _In_opt_ PDOT11_SSID Ssid)
{
    PNWIFI_ADAPTER Adapter = Msm->Adapter;
    DOT11_SCAN_REQUEST_V2 *Scan;
    ULONG ScanSize;
    NDIS_STATUS Status;

    NdisAcquireSpinLock(&Msm->Lock);
    if (Msm->ScanInProgress || Msm->WorkItemsHalting != 0)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_DOT11_MEDIA_IN_USE;
    }
    Msm->ScanInProgress = TRUE;
    /* Scanning does not change an established association state; only move to
     * Scanning when idle so a scan during connect does not stomp the FSM. */
    if (Msm->State == NwifiMsmIdle)
    {
        Msm->State = NwifiMsmScanning;
    }
    NdisReleaseSpinLock(&Msm->Lock);

    /* OID_DOT11_SCAN_REQUEST takes a DOT11_SCAN_REQUEST_V2 with a trailing
     * SSID list: one SSID (directed) or a zero-length SSID (broadcast). */
    ScanSize = FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer) + sizeof(DOT11_SSID);
    Scan = (DOT11_SCAN_REQUEST_V2 *)NwifiAllocate(ScanSize);
    if (Scan == NULL)
    {
        NdisAcquireSpinLock(&Msm->Lock);
        Msm->ScanInProgress = FALSE;
        if (Msm->State == NwifiMsmScanning)
            Msm->State = NwifiMsmIdle;
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_RESOURCES;
    }

    Scan->dot11BSSType = BssType;
    RtlFillMemory(Scan->dot11BSSID, IEEE80211_ADDR_LEN, 0xFF);    /* any BSSID */
    Scan->dot11ScanType = dot11_scan_type_auto;
    Scan->bRestrictedScan = FALSE;
    Scan->udot11SSIDsOffset = FIELD_OFFSET(DOT11_SCAN_REQUEST_V2, ucBuffer);
    Scan->uNumOfdot11SSIDs = 1;
    Scan->bUseRequestIE = FALSE;
    Scan->uRequestIDsOffset = 0;
    Scan->uNumOfRequestIDs = 0;
    Scan->uPhyTypeInfosOffset = 0;
    Scan->uNumOfPhyTypeInfos = 0;
    Scan->uIEsOffset = 0;
    Scan->uIEsLength = 0;
    {
        PDOT11_SSID ScanSsid = (PDOT11_SSID)Scan->ucBuffer;
        if (Ssid != NULL && Ssid->uSSIDLength != 0)
        {
            *ScanSsid = *Ssid;
        }
        else
        {
            RtlZeroMemory(ScanSsid, sizeof(DOT11_SSID));   /* broadcast scan */
        }
    }

    Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                    OID_DOT11_SCAN_REQUEST,
                                    Scan, ScanSize, NULL);
    NwifiFree(Scan);

    if (Status != NDIS_STATUS_SUCCESS && Status != NDIS_STATUS_PENDING)
    {
        DPRINT1("NWIFI: scan request failed 0x%08X\n", Status);
        NdisAcquireSpinLock(&Msm->Lock);
        Msm->ScanInProgress = FALSE;
        if (Msm->State == NwifiMsmScanning)
            Msm->State = NwifiMsmIdle;
        NdisReleaseSpinLock(&Msm->Lock);
    }

    /* The completion (NDIS_STATUS_DOT11_SCAN_CONFIRM) drives BSS aggregation. */
    return Status;
}

/* Pull the lower miniport's BSS list (OID_DOT11_ENUM_BSS_LIST: a
 * DOT11_BYTE_ARRAY of packed DOT11_BSS_ENTRY records) and merge it into the
 * cache, de-duplicating by BSSID. */
static
VOID
NwifiAggregateBssList(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_ADAPTER Adapter = Msm->Adapter;
    PDOT11_BYTE_ARRAY ByteArray;
    ULONG BufferSize = 16 * 1024;       /* generous; typical lists are < 4 KB */
    ULONG Got = 0;
    NDIS_STATUS Status;
    PUCHAR Cursor;
    PUCHAR End;
    ULONG64 Now;

    ByteArray = (PDOT11_BYTE_ARRAY)NwifiAllocate(BufferSize);
    if (ByteArray == NULL)
    {
        return;
    }

    Status = NwifiProtocolDoRequest(Adapter, NdisRequestQueryInformation,
                                    OID_DOT11_ENUM_BSS_LIST,
                                    ByteArray, BufferSize, &Got);
    if (Status != NDIS_STATUS_SUCCESS ||
        Got < FIELD_OFFSET(DOT11_BYTE_ARRAY, ucBuffer))
    {
        DPRINT("NWIFI: ENUM_BSS_LIST failed 0x%08X got %u\n", Status, Got);
        NwifiFree(ByteArray);
        return;
    }

    Now = (ULONG64)KeQueryInterruptTime();
    Cursor = ByteArray->ucBuffer;
    End = ByteArray->ucBuffer + ByteArray->uNumOfBytes;
    if (End > (PUCHAR)ByteArray + Got)
    {
        End = (PUCHAR)ByteArray + Got;          /* clamp to what we received */
    }

    NdisAcquireSpinLock(&Msm->Lock);

    while (Cursor + FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) <= End)
    {
        PDOT11_BSS_ENTRY Entry = (PDOT11_BSS_ENTRY)Cursor;
        ULONG EntrySize = FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer) + Entry->uBufferLength;
        ULONG i;
        PNWIFI_BSS_CACHE_ENTRY Slot = NULL;
        DOT11_SSID Ssid;
        ULONG IeCopy;

        /* Guard against a malformed / truncated trailing entry. */
        if (Cursor + EntrySize > End || EntrySize < FIELD_OFFSET(DOT11_BSS_ENTRY, ucBuffer))
        {
            break;
        }

        /* Extract the SSID from the beacon/probe IEs (element id 0). */
        RtlZeroMemory(&Ssid, sizeof(Ssid));
        {
            PUCHAR Ie = Entry->ucBuffer;
            ULONG IeLen = Entry->uBufferLength;
            ULONG Off = 0;
            while (Off + 2 <= IeLen)
            {
                UCHAR ElemId = Ie[Off];
                UCHAR ElemLen = Ie[Off + 1];
                if (Off + 2 + ElemLen > IeLen)
                    break;
                if (ElemId == 0)        /* SSID element */
                {
                    Ssid.uSSIDLength = (ElemLen <= DOT11_SSID_MAX_LENGTH)
                                           ? ElemLen : DOT11_SSID_MAX_LENGTH;
                    RtlCopyMemory(Ssid.ucSSID, &Ie[Off + 2], Ssid.uSSIDLength);
                    break;
                }
                Off += 2 + ElemLen;
            }
        }

        /* De-duplicate by BSSID; otherwise take a free/oldest slot. */
        for (i = 0; i < Msm->BssCount; i++)
        {
            if (Msm->BssCache[i].Valid &&
                RtlCompareMemory(Msm->BssCache[i].Bssid, Entry->dot11BSSID,
                                 IEEE80211_ADDR_LEN) == IEEE80211_ADDR_LEN)
            {
                Slot = &Msm->BssCache[i];
                break;
            }
        }
        if (Slot == NULL)
        {
            if (Msm->BssCount < NWIFI_MAX_BSS_CACHE)
            {
                Slot = &Msm->BssCache[Msm->BssCount++];
            }
            else
            {
                /* Evict the oldest entry. */
                ULONG Oldest = 0;
                for (i = 1; i < Msm->BssCount; i++)
                {
                    if (Msm->BssCache[i].HostTimestamp <
                        Msm->BssCache[Oldest].HostTimestamp)
                        Oldest = i;
                }
                Slot = &Msm->BssCache[Oldest];
            }
        }

        RtlZeroMemory(Slot, sizeof(*Slot));
        Slot->Valid = TRUE;
        RtlCopyMemory(Slot->Bssid, Entry->dot11BSSID, IEEE80211_ADDR_LEN);
        Slot->Ssid = Ssid;
        Slot->BssType = Entry->dot11BSSType;
        Slot->PhyId = Entry->uPhyId;
        Slot->Rssi = Entry->lRSSI;
        Slot->LinkQuality = Entry->uLinkQuality;
        Slot->ChCenterFrequency = Entry->PhySpecificInfo.uChCenterFrequency;
        Slot->BeaconPeriod = Entry->usBeaconPeriod;
        Slot->CapabilityInformation = Entry->usCapabilityInformation;
        Slot->HostTimestamp = Now;
        IeCopy = Entry->uBufferLength;
        if (IeCopy > NWIFI_MAX_BSS_IE)
            IeCopy = NWIFI_MAX_BSS_IE;
        Slot->IeLength = IeCopy;
        RtlCopyMemory(Slot->Ie, Entry->ucBuffer, IeCopy);

        Cursor += EntrySize;
    }

    NdisReleaseSpinLock(&Msm->Lock);
    NwifiFree(ByteArray);
}

/* ===========================================================================
 *  Connect
 * ===========================================================================
 */
NDIS_STATUS
NwifiMsmConnect(
    _In_ PNWIFI_MSM Msm,
    _In_ PNWIFI_CONNECT_PARAMS Params)
{
    PNWIFI_ADAPTER Adapter = Msm->Adapter;
    NDIS_STATUS Status;

    NdisAcquireSpinLock(&Msm->Lock);
    if (Msm->State != NwifiMsmIdle)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return NDIS_STATUS_MEDIA_BUSY;
    }
    Msm->Connect = *Params;
    Msm->CurrentSsid = Params->Ssid;
    Msm->State = NwifiMsmAssociating;
    NdisReleaseSpinLock(&Msm->Lock);

    Status = NwifiProgramBssType(Adapter, Params->BssType);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: DESIRED_BSS_TYPE failed 0x%08X\n", Status);
        goto ConnectFailed;
    }

    Status = NwifiProgramDesiredSsid(Adapter, &Params->Ssid);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: DESIRED_SSID_LIST failed 0x%08X\n", Status);
        goto ConnectFailed;
    }

    Status = NwifiProgramDesiredBssid(
        Adapter, Params->HaveBssid ? Params->DesiredBssid : NULL);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: DESIRED_BSSID_LIST failed 0x%08X\n", Status);
        goto ConnectFailed;
    }

    Status = NwifiProgramAuthAlgorithm(Adapter, Params->AuthAlgorithm);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: ENABLED_AUTH_ALGO failed 0x%08X\n", Status);
        goto ConnectFailed;
    }

    Status = NwifiProgramCipherAlgorithm(
        Adapter,
        OID_DOT11_ENABLED_UNICAST_CIPHER_ALGORITHM,
        Params->UnicastCipher);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: unicast cipher failed 0x%08X\n", Status);
        goto ConnectFailed;
    }
    Status = NwifiProgramCipherAlgorithm(
        Adapter,
        OID_DOT11_ENABLED_MULTICAST_CIPHER_ALGORITHM,
        Params->MulticastCipher);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("NWIFI: multicast cipher failed 0x%08X\n", Status);
        goto ConnectFailed;
    }

    /* Fullmac adapters complete WPA2-PSK and WPA3-SAE in firmware after the
     * credential is seeded through the lower miniport. */
    if (Params->Secure)
    {
        if (Params->AuthAlgorithm == DOT11_AUTH_ALGO_RSNA_PSK ||
            Params->AuthAlgorithm == DOT11_AUTH_ALGO_WPA3_SAE)
        {
            Status = NwifiSupplicantSeedFirmware(Msm);
        }
        else
        {
            Status = NDIS_STATUS_NOT_SUPPORTED;
        }
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DPRINT1("NWIFI: security setup failed 0x%08X\n", Status);
            goto ConnectFailed;
        }
    }
    else
    {
        NwifiSupplicantStop(Msm);
    }

    /* OID_DOT11_CONNECT_REQUEST has no payload; the lower miniport runs
     * scan/join/auth/assoc and reports progress through the
     * NDIS_STATUS_DOT11_* indications. */
    Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                    OID_DOT11_CONNECT_REQUEST,
                                    NULL, 0, NULL);
    if (Status != NDIS_STATUS_SUCCESS && Status != NDIS_STATUS_PENDING)
    {
        DPRINT1("NWIFI: CONNECT_REQUEST failed 0x%08X\n", Status);
        NdisAcquireSpinLock(&Msm->Lock);
        Msm->State = NwifiMsmIdle;
        NdisReleaseSpinLock(&Msm->Lock);
        NwifiSupplicantStop(Msm);
        return Status;
    }

    return NDIS_STATUS_SUCCESS;

ConnectFailed:
    NdisAcquireSpinLock(&Msm->Lock);
    Msm->State = NwifiMsmIdle;
    NdisReleaseSpinLock(&Msm->Lock);
    NwifiSupplicantStop(Msm);
    return Status;
}

/* ===========================================================================
 *  Disconnect
 * ===========================================================================
 */
NDIS_STATUS
NwifiMsmDisconnect(
    _In_ PNWIFI_MSM Msm)
{
    PNWIFI_ADAPTER Adapter = Msm->Adapter;
    NDIS_STATUS Status;

    NwifiSupplicantStop(Msm);

    Status = NwifiProtocolDoRequest(Adapter, NdisRequestSetInformation,
                                    OID_DOT11_DISCONNECT_REQUEST,
                                    NULL, 0, NULL);

    NdisAcquireSpinLock(&Msm->Lock);
    Msm->State = NwifiMsmIdle;
    NdisReleaseSpinLock(&Msm->Lock);

    NdisAcquireSpinLock(&Adapter->DataLock);
    Adapter->Associated = FALSE;
    NdisReleaseSpinLock(&Adapter->DataLock);

    NwifiIndicateLinkState(Adapter, FALSE);
    NwifiQueueNotification(Adapter->InterfaceIndex, NwifiNotifyDisconnected,
                           NDIS_STATUS_SUCCESS);
    return Status;
}

/* ===========================================================================
 *  Establish the link once associated (open) or handshake-complete (secure)
 * ===========================================================================
 */
VOID
NwifiMsmLinkUp(
    _In_ PNWIFI_MSM Msm,
    _In_ BOOLEAN Secure)
{
    PNWIFI_ADAPTER Adapter = Msm->Adapter;
    DOT11_MAC_ADDRESS Bssid;
    DOT11_SSID Ssid;

    NdisAcquireSpinLock(&Msm->Lock);
    if (Msm->State != NwifiMsmAssociating &&
        Msm->State != NwifiMsmHandshaking)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        return;
    }
    Msm->State = Secure ? NwifiMsmConnectedSecure : NwifiMsmConnectedOpen;
    RtlCopyMemory(Bssid, Msm->CurrentBssid, IEEE80211_ADDR_LEN);
    Ssid = Msm->CurrentSsid;
    NdisReleaseSpinLock(&Msm->Lock);

    NdisAcquireSpinLock(&Adapter->DataLock);
    RtlCopyMemory(Adapter->Bssid, Bssid, IEEE80211_ADDR_LEN);
    Adapter->Ssid = Ssid;
    Adapter->Associated = TRUE;
    NdisReleaseSpinLock(&Adapter->DataLock);

    NwifiIndicateLinkState(Adapter, TRUE);
    NwifiQueueNotification(Adapter->InterfaceIndex, NwifiNotifyConnectComplete,
                           NDIS_STATUS_SUCCESS);
}

/* ===========================================================================
 *  Status indication consumption (from protocol.c StatusEx)
 * ===========================================================================
 */
VOID
NwifiMsmIndicateStatus(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    PNWIFI_MSM Msm = NwifiMsmFromAdapter(Adapter);
    PVOID Buffer = StatusIndication->StatusBuffer;
    ULONG BufferSize = StatusIndication->StatusBufferSize;

    if (Msm == NULL)
    {
        return;
    }

    switch (StatusIndication->StatusCode)
    {
        case NDIS_STATUS_DOT11_SCAN_CONFIRM:
        {
            /* This indication may be at DISPATCH_LEVEL; harvesting the BSS
             * list is a blocking OID, so defer it to a one-shot work item. */
            NDIS_STATUS ScanStatus = NDIS_STATUS_SUCCESS;
            BOOLEAN Queue = FALSE;
            if (Buffer != NULL && BufferSize >= sizeof(DOT11_STATUS_INDICATION))
            {
                ScanStatus = ((PDOT11_STATUS_INDICATION)Buffer)->ndisStatus;
            }

            NdisAcquireSpinLock(&Msm->Lock);
            Msm->LastScanStatus = ScanStatus;
            if (Msm->ScanInProgress && Msm->ScanWorkQueued == 0)
            {
                Msm->ScanWorkQueued = 1;
                Queue = TRUE;
            }
            NdisReleaseSpinLock(&Msm->Lock);

            if (Queue &&
                !NwifiMsmQueueWorkItem(Msm, NwifiMsmScanWorker, Msm))
            {
                NdisAcquireSpinLock(&Msm->Lock);
                Msm->ScanWorkQueued = 0;
                Msm->ScanInProgress = FALSE;
                if (Msm->State == NwifiMsmScanning)
                    Msm->State = NwifiMsmIdle;
                NdisReleaseSpinLock(&Msm->Lock);
                NwifiQueueNotification(Adapter->InterfaceIndex,
                                       NwifiNotifyScanComplete,
                                       NDIS_STATUS_RESOURCES);
            }
            break;
        }

        case NDIS_STATUS_DOT11_ASSOCIATION_START:
        {
            if (Buffer != NULL &&
                BufferSize >= sizeof(DOT11_ASSOCIATION_START_PARAMETERS))
            {
                PDOT11_ASSOCIATION_START_PARAMETERS P =
                    (PDOT11_ASSOCIATION_START_PARAMETERS)Buffer;
                NdisAcquireSpinLock(&Msm->Lock);
                RtlCopyMemory(Msm->CurrentBssid, P->MacAddr, IEEE80211_ADDR_LEN);
                if (P->SSID.uSSIDLength != 0)
                    Msm->CurrentSsid = P->SSID;
                if (Msm->State == NwifiMsmIdle)
                    Msm->State = NwifiMsmAssociating;
                NdisReleaseSpinLock(&Msm->Lock);
                DPRINT("NWIFI: assoc start with %02x:%02x:%02x:%02x:%02x:%02x\n",
                       P->MacAddr[0], P->MacAddr[1], P->MacAddr[2],
                       P->MacAddr[3], P->MacAddr[4], P->MacAddr[5]);
            }
            break;
        }

        case NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION:
        {
            if (Buffer != NULL &&
                BufferSize >= sizeof(DOT11_ASSOCIATION_COMPLETION_PARAMETERS))
            {
                PDOT11_ASSOCIATION_COMPLETION_PARAMETERS P =
                    (PDOT11_ASSOCIATION_COMPLETION_PARAMETERS)Buffer;

                if (P->uStatus == DOT11_ASSOC_STATUS_SUCCESS)
                {
                    BOOLEAN Secure;

                    NdisAcquireSpinLock(&Msm->Lock);
                    RtlCopyMemory(Msm->CurrentBssid, P->MacAddr, IEEE80211_ADDR_LEN);
                    Msm->CurrentPhyType = NwifiPhyTypeFromId(0);
                    Secure = Msm->Connect.Secure;
                    NdisReleaseSpinLock(&Msm->Lock);

                    if (!Secure)
                    {
                        /* Open network: associated == connected. */
                        NwifiMsmLinkUp(Msm, FALSE);
                    }
                    else if (P->bPortAuthorized)
                    {
                        /* A fullmac adapter completed authentication and key
                         * installation in firmware; no host EAPOL follows. */
                        NwifiSupplicantStop(Msm);
                        NwifiMsmLinkUp(Msm, TRUE);
                        DPRINT1("NWIFI: lower miniport authorized secure port\n");
                    }
                    else
                    {
                        /* EAPOL frames sent during the handshake are ToDS
                         * data frames with Address1 = BSSID, so publish the
                         * BSSID before the link is up. */
                        NdisAcquireSpinLock(&Adapter->DataLock);
                        RtlCopyMemory(Adapter->Bssid, P->MacAddr, IEEE80211_ADDR_LEN);
                        NdisReleaseSpinLock(&Adapter->DataLock);

                        if (Msm->Supplicant != NULL)
                        {
                            NwifiSupplicantSetApAddr(Msm, P->MacAddr);
                        }

                        NdisAcquireSpinLock(&Msm->Lock);
                        Msm->State = NwifiMsmHandshaking;
                        NdisReleaseSpinLock(&Msm->Lock);
                        DPRINT1("NWIFI: associated, awaiting 4-way handshake\n");
                    }
                }
                else
                {
                    DPRINT1("NWIFI: association failed status 0x%08X\n", P->uStatus);
                    NdisAcquireSpinLock(&Msm->Lock);
                    Msm->State = NwifiMsmIdle;
                    NdisReleaseSpinLock(&Msm->Lock);
                    NwifiSupplicantStop(Msm);
                    NwifiQueueNotification(Adapter->InterfaceIndex,
                                           NwifiNotifyConnectComplete,
                                           NDIS_STATUS_DOT11_ASSOCIATION_COMPLETION);
                }
            }
            break;
        }

        case NDIS_STATUS_DOT11_CONNECTION_START:
            DPRINT("NWIFI: connection start\n");
            break;

        case NDIS_STATUS_DOT11_CONNECTION_COMPLETION:
        {
            if (Buffer != NULL &&
                BufferSize >= sizeof(DOT11_CONNECTION_COMPLETION_PARAMETERS))
            {
                PDOT11_CONNECTION_COMPLETION_PARAMETERS P =
                    (PDOT11_CONNECTION_COMPLETION_PARAMETERS)Buffer;
                if (P->uStatus != DOT11_CONNECTION_STATUS_SUCCESS)
                {
                    DPRINT1("NWIFI: connection failed status 0x%08X\n", P->uStatus);
                    NdisAcquireSpinLock(&Msm->Lock);
                    Msm->State = NwifiMsmIdle;
                    NdisReleaseSpinLock(&Msm->Lock);
                    NwifiSupplicantStop(Msm);
                    NdisAcquireSpinLock(&Adapter->DataLock);
                    Adapter->Associated = FALSE;
                    NdisReleaseSpinLock(&Adapter->DataLock);
                    NwifiIndicateLinkState(Adapter, FALSE);
                    NwifiQueueNotification(Adapter->InterfaceIndex,
                                           NwifiNotifyConnectComplete,
                                           NDIS_STATUS_DOT11_CONNECTION_COMPLETION);
                }
                /* On success the link is brought up by the association
                 * completion (open) or the handshake completion (secure). */
            }
            break;
        }

        case NDIS_STATUS_DOT11_DISASSOCIATION:
        {
            DOT11_ASSOC_STATUS Reason = DOT11_ASSOC_STATUS_UNREACHABLE;
            if (Buffer != NULL &&
                BufferSize >= sizeof(DOT11_DISASSOCIATION_PARAMETERS))
            {
                Reason = ((PDOT11_DISASSOCIATION_PARAMETERS)Buffer)->uReason;
            }
            DPRINT1("NWIFI: disassociation reason 0x%08X\n", Reason);

            NwifiSupplicantStop(Msm);
            NdisAcquireSpinLock(&Msm->Lock);
            Msm->State = NwifiMsmIdle;
            NdisReleaseSpinLock(&Msm->Lock);
            NdisAcquireSpinLock(&Adapter->DataLock);
            Adapter->Associated = FALSE;
            NdisReleaseSpinLock(&Adapter->DataLock);

            NwifiIndicateLinkState(Adapter, FALSE);
            NwifiQueueNotification(Adapter->InterfaceIndex, NwifiNotifyDisconnected,
                                   NDIS_STATUS_DOT11_DISASSOCIATION);
            break;
        }

        case NDIS_STATUS_DOT11_ROAMING_START:
        case NDIS_STATUS_DOT11_ROAMING_COMPLETION:
            /* A roam re-runs association; the new BSSID is learned from the
             * next ASSOCIATION_START/COMPLETION pair. */
            DPRINT("NWIFI: roaming indication 0x%08X\n", StatusIndication->StatusCode);
            break;

        case NDIS_STATUS_DOT11_LINK_QUALITY:
            if (Buffer != NULL &&
                BufferSize >= sizeof(DOT11_LINK_QUALITY_PARAMETERS))
            {
                PDOT11_LINK_QUALITY_PARAMETERS Lq =
                    (PDOT11_LINK_QUALITY_PARAMETERS)Buffer;
                if (Lq->uLinkQualityListSize >= 1 &&
                    Lq->uLinkQualityListOffset <= BufferSize &&
                    BufferSize - Lq->uLinkQualityListOffset >=
                        sizeof(DOT11_LINK_QUALITY_ENTRY))
                {
                    PDOT11_LINK_QUALITY_ENTRY Entry =
                        (PDOT11_LINK_QUALITY_ENTRY)
                        ((PUCHAR)Buffer + Lq->uLinkQualityListOffset);
                    UCHAR Quality = Entry->ucLinkQuality > 100
                                        ? 100 : Entry->ucLinkQuality;

                    /* Inverse of the quality = 2 * (RSSI + 100) mapping */
                    NdisAcquireSpinLock(&Msm->Lock);
                    Msm->CurrentRssi = -100 + (LONG)(Quality / 2);
                    NdisReleaseSpinLock(&Msm->Lock);
                }
            }
            break;

        default:
            DPRINT("NWIFI: MSM unhandled indication 0x%08X\n",
                   StatusIndication->StatusCode);
            break;
    }
}

/* ===========================================================================
 *  Async OID completion (from protocol.c OidRequestComplete)
 * ===========================================================================
 */
VOID
NwifiMsmOidComplete(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    /* All MSM OID round-trips are currently synchronous
     * (NwifiProtocolDoRequest), so async completions are informational only. */
    UNREFERENCED_PARAMETER(Adapter);
    DPRINT("NWIFI: MSM async OID 0x%p completed 0x%08X\n",
           OidRequest->RequestId, Status);
}

/* ===========================================================================
 *  RX classification (from datapath.c)
 * ===========================================================================
 */
BOOLEAN
NwifiMsmRxData(
    _In_ PNWIFI_ADAPTER Adapter,
    _In_reads_bytes_(EthLength) PUCHAR Eth,
    _In_ ULONG EthLength)
{
    PNWIFI_MSM Msm = NwifiMsmFromAdapter(Adapter);
    USHORT EtherType;

    if (Msm == NULL || EthLength < ETH_HEADER_LEN)
    {
        return FALSE;
    }

    EtherType = (USHORT)((Eth[ETH_TYPE_OFFSET] << 8) | Eth[ETH_TYPE_OFFSET + 1]);

    /* EAPOL (802.1X) frames belong to the supplicant, not to TCP/IP. */
    if (EtherType == 0x888E)
    {
        PUCHAR Eapol = Eth + ETH_HEADER_LEN;
        ULONG EapolLen = EthLength - ETH_HEADER_LEN;
        return NwifiSupplicantRxEapol(Msm, Eapol, EapolLen,
                                      &Eth[ETH_ADDR_LEN] /* src */,
                                      &Eth[0] /* dst */);
    }

    return FALSE;
}

/* ===========================================================================
 *  BSS list export to wlansvc (IOCTL_NWIFI_GET_BSS_LIST)
 * ===========================================================================
 */
NDIS_STATUS
NwifiMsmGetBssList(
    _In_ PNWIFI_MSM Msm,
    _Out_writes_bytes_(OutLength) PVOID Out,
    _In_ ULONG OutLength,
    _Out_ PULONG BytesWritten)
{
    PNWIFI_BSS_LIST List = (PNWIFI_BSS_LIST)Out;
    PUCHAR Cursor;
    ULONG Needed;
    ULONG i;
    ULONG Count;

    *BytesWritten = 0;

    NdisAcquireSpinLock(&Msm->Lock);

    /* Compute required size: header + one NWIFI_BSS_ENTRY (with IE tail) each. */
    Needed = sizeof(NWIFI_BSS_LIST);
    Count = 0;
    for (i = 0; i < Msm->BssCount; i++)
    {
        if (!Msm->BssCache[i].Valid)
            continue;
        Needed += sizeof(NWIFI_BSS_ENTRY) + Msm->BssCache[i].IeLength;
        Count++;
    }

    if (OutLength < Needed)
    {
        NdisReleaseSpinLock(&Msm->Lock);
        *BytesWritten = Needed;          /* tell the caller how much to allocate */
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    RtlZeroMemory(List, sizeof(NWIFI_BSS_LIST));
    List->Size = Needed;
    List->InterfaceIndex = Msm->Adapter->InterfaceIndex;
    List->NumberOfItems = Count;
    List->FirstEntryOffset = sizeof(NWIFI_BSS_LIST);

    Cursor = (PUCHAR)Out + sizeof(NWIFI_BSS_LIST);
    for (i = 0; i < Msm->BssCount; i++)
    {
        PNWIFI_BSS_CACHE_ENTRY Src = &Msm->BssCache[i];
        PNWIFI_BSS_ENTRY Dst = (PNWIFI_BSS_ENTRY)Cursor;
        ULONG EntrySize;

        if (!Src->Valid)
            continue;

        EntrySize = sizeof(NWIFI_BSS_ENTRY) + Src->IeLength;
        RtlZeroMemory(Dst, sizeof(NWIFI_BSS_ENTRY));
        Dst->Size = EntrySize;
        RtlCopyMemory(Dst->Bssid, Src->Bssid, IEEE80211_ADDR_LEN);
        Dst->Ssid = Src->Ssid;
        Dst->BssType = Src->BssType;
        Dst->PhyType = NwifiPhyTypeFromId(Src->PhyId);
        Dst->Rssi = Src->Rssi;
        Dst->ChCenterFrequency = Src->ChCenterFrequency;
        Dst->BeaconPeriod = Src->BeaconPeriod;
        Dst->CapabilityInformation = Src->CapabilityInformation;
        Dst->IeOffset = sizeof(NWIFI_BSS_ENTRY);
        Dst->IeLength = Src->IeLength;
        RtlCopyMemory(Cursor + sizeof(NWIFI_BSS_ENTRY), Src->Ie, Src->IeLength);

        Cursor += EntrySize;
    }

    NdisReleaseSpinLock(&Msm->Lock);
    *BytesWritten = Needed;
    return NDIS_STATUS_SUCCESS;
}

/* ===========================================================================
 *  Link-state query (IOCTL_NWIFI_QUERY_STATE)
 * ===========================================================================
 */
NWIFI_LINK_STATUS
NwifiMsmLinkStatus(
    _In_ PNWIFI_MSM Msm)
{
    NWIFI_LINK_STATUS Status;

    switch (Msm->State)
    {
        case NwifiMsmScanning:        Status = NwifiLinkScanning; break;
        case NwifiMsmAssociating:     Status = NwifiLinkAssociating; break;
        case NwifiMsmHandshaking:     Status = NwifiLinkAssociating; break;
        case NwifiMsmConnectedOpen:   Status = NwifiLinkConnectedOpen; break;
        case NwifiMsmConnectedSecure: Status = NwifiLinkConnectedSecure; break;
        case NwifiMsmIdle:
        default:                      Status = NwifiLinkDisconnected; break;
    }
    return Status;
}

VOID
NwifiMsmQueryState(
    _In_ PNWIFI_MSM Msm,
    _Out_ PNWIFI_LINK_STATE State)
{
    RtlZeroMemory(State, sizeof(NWIFI_LINK_STATE));
    State->Size = sizeof(NWIFI_LINK_STATE);

    NdisAcquireSpinLock(&Msm->Lock);
    State->InterfaceIndex = Msm->Adapter->InterfaceIndex;
    State->Status = NwifiMsmLinkStatus(Msm);
    RtlCopyMemory(State->Bssid, Msm->CurrentBssid, IEEE80211_ADDR_LEN);
    State->Ssid = Msm->CurrentSsid;
    State->PhyType = Msm->CurrentPhyType;
    State->Rssi = Msm->CurrentRssi;
    State->LinkSpeedKbps = Msm->LinkSpeedKbps;
    NdisReleaseSpinLock(&Msm->Lock);
}

/* ===========================================================================
 *  Key install (IOCTL_NWIFI_SET_KEY)
 * ===========================================================================
 */
NDIS_STATUS
NwifiMsmSetKey(
    _In_ PNWIFI_MSM Msm,
    _In_ PNWIFI_SET_KEY Key)
{
    if (Key->KeyLength > sizeof(Key->KeyData) ||
        Key->KeyId >= DOT11_MAX_NUM_DEFAULT_KEY)
    {
        return NDIS_STATUS_INVALID_DATA;
    }

    /* Algorithm == DOT11_CIPHER_ALGO_NONE with a non-zero key is the wlansvc
     * convention for "WPA2 passphrase/PSK": seed the supplicant.  Any other
     * algorithm is a static cipher key forwarded straight down. */
    if (Key->Algorithm == DOT11_CIPHER_ALGO_NONE && Key->KeyLength != 0)
    {
        return NwifiSupplicantSetPassphrase(Msm, Key->KeyData, Key->KeyLength);
    }

    /* Pairwise => key-mapping key, otherwise default (group) key. */
    if (Key->IsPairwise)
    {
        return NwifiInstallPairwiseKey(Msm->Adapter, Key->Algorithm, Key->PeerMac,
                                       Key->KeyData, Key->KeyLength);
    }
    return NwifiInstallGroupKey(Msm->Adapter, Key->Algorithm, Key->KeyId,
                                Key->KeyData, Key->KeyLength);
}

/* ===========================================================================
 *  Interface arrival / removal notifications
 * ===========================================================================
 */
VOID
NwifiMsmInterfaceArrival(
    _In_ PNWIFI_ADAPTER Adapter)
{
    NwifiQueueNotification(Adapter->InterfaceIndex, NwifiNotifyInterfaceArrival,
                           NDIS_STATUS_SUCCESS);
}

VOID
NwifiMsmInterfaceRemoval(
    _In_ PNWIFI_ADAPTER Adapter)
{
    NwifiQueueNotification(Adapter->InterfaceIndex, NwifiNotifyInterfaceRemoval,
                           NDIS_STATUS_SUCCESS);
}
