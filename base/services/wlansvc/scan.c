/*
 * PROJECT:     ReactOS WLAN Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        base/services/wlansvc/scan.c
 * PURPOSE:     Scan orchestration, BSS cache, available-network/BSS lists
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * All routines here run with WlanSvcLock held by the caller.
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

VOID
WlanSvcFlushBssList(PWLANSVC_INTERFACE Iface)
{
    while (!IsListEmpty(&Iface->BssListHead))
    {
        PLIST_ENTRY entry = RemoveHeadList(&Iface->BssListHead);
        PWLANSVC_BSS_ENTRY bss =
            CONTAINING_RECORD(entry, WLANSVC_BSS_ENTRY, ListEntry);
        HeapFree(GetProcessHeap(), 0, bss);
    }
    Iface->BssCount = 0;
}

/* RSSI(-100..-50 dBm) -> link quality(0..100), matching Windows' mapping. */
static ULONG
WlanSvcRssiToQuality(LONG Rssi)
{
    if (Rssi <= -100)
        return 0;
    if (Rssi >= -50)
        return 100;
    return (ULONG)(2 * (Rssi + 100));
}

/*
 * Decode the security summary (auth + cipher) and supported rates from a BSS's
 * raw beacon/probe IE blob.  The capability Privacy bit is the fallback
 * "secured" signal when no RSN/WPA IE is present.
 */
static VOID
WlanSvcParseBssIes(PWLANSVC_BSS_ENTRY bss, const UCHAR *ie, ULONG ieLen)
{
    ULONG pos = 0;

    /* Default: open unless an RSN/WPA IE (or the Privacy bit) says otherwise. */
    if (bss->CapabilityInformation & 0x0010 /* Privacy */)
    {
        bss->SecurityEnabled = TRUE;
        bss->DefaultAuth = DOT11_AUTH_ALGO_80211_OPEN;   /* WEP-era default */
        bss->DefaultCipher = DOT11_CIPHER_ALGO_WEP;
    }
    else
    {
        bss->SecurityEnabled = FALSE;
        bss->DefaultAuth = DOT11_AUTH_ALGO_80211_OPEN;
        bss->DefaultCipher = DOT11_CIPHER_ALGO_NONE;
    }

    while (pos + 2 <= ieLen)
    {
        UCHAR id = ie[pos];
        UCHAR len = ie[pos + 1];
        const UCHAR *body = ie + pos + 2;

        if (pos + 2 + len > ieLen)
            break;

        switch (id)
        {
            case 1:   /* Supported Rates */
            case 50:  /* Extended Supported Rates */
            {
                UCHAR k;
                for (k = 0; k < len && bss->RateCount < DOT11_RATE_SET_MAX_LENGTH; k++)
                    bss->Rates[bss->RateCount++] = body[k];
                break;
            }

            case 48:  /* RSN (WPA2) */
            {
                /* Minimal RSN parse: presence => RSNA; pick the group cipher
                 * suite type to distinguish CCMP(4) from TKIP(2). */
                bss->SecurityEnabled = TRUE;
                bss->DefaultAuth = DOT11_AUTH_ALGO_RSNA_PSK;
                bss->DefaultCipher = DOT11_CIPHER_ALGO_CCMP;
                if (len >= 8)
                {
                    /* version(2) + group suite OUI(3) + group suite type(1) */
                    UCHAR groupType = body[2 + 3];
                    if (groupType == 2)
                        bss->DefaultCipher = DOT11_CIPHER_ALGO_TKIP;
                    else if (groupType == 4)
                        bss->DefaultCipher = DOT11_CIPHER_ALGO_CCMP;
                }
                break;
            }

            case 221: /* Vendor Specific -- WPA(1) uses OUI 00:50:F2 type 1 */
            {
                static const UCHAR WpaOui[4] = { 0x00, 0x50, 0xF2, 0x01 };
                if (len >= 4 && memcmp(body, WpaOui, 4) == 0 &&
                    !bss->SecurityEnabled)   /* RSN, if present, wins */
                {
                    bss->SecurityEnabled = TRUE;
                    bss->DefaultAuth = DOT11_AUTH_ALGO_WPA_PSK;
                    bss->DefaultCipher = DOT11_CIPHER_ALGO_TKIP;
                }
                break;
            }

            default:
                break;
        }

        pos += 2 + len;
    }
}

/* Convert one nwifi scan result into a cached service-side entry. */
static PWLANSVC_BSS_ENTRY
WlanSvcImportBss(PNWIFI_BSS_ENTRY src)
{
    PWLANSVC_BSS_ENTRY bss;
    const UCHAR *ie;
    ULONG ieLen;

    bss = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*bss));
    if (!bss)
        return NULL;

    bss->Ssid = src->Ssid;
    bss->Bssid = src->Bssid;
    bss->BssType = (DOT11_BSS_TYPE)src->BssType;
    bss->PhyType = (DOT11_PHY_TYPE)src->PhyType;
    bss->Rssi = src->Rssi;
    bss->LinkQuality = WlanSvcRssiToQuality(src->Rssi);
    bss->BeaconPeriod = src->BeaconPeriod;
    bss->CapabilityInformation = src->CapabilityInformation;
    bss->ChCenterFrequency = src->ChCenterFrequency;
    bss->HostTimestamp = 0;
    bss->BeaconTimestamp = 0;
    bss->RateCount = 0;

    /* The raw IE blob is appended after the fixed entry, at IeOffset. */
    ieLen = src->IeLength;
    if (ieLen > NWIFI_MAX_IE_SIZE)
        ieLen = NWIFI_MAX_IE_SIZE;
    ie = (const UCHAR *)src + src->IeOffset;

    WlanSvcParseBssIes(bss, ie, ieLen);

    bss->IeLength = ieLen;
    memcpy(bss->IeData, ie, ieLen);

    return bss;
}

/*
 * Rebuild the interface's BSS cache from nwifi's BSS list.  NWIFI_BSS_LIST is
 * variable-length: entries are walked from FirstEntryOffset, each advancing by
 * its own Size.  Lock held by caller.
 */
DWORD
WlanSvcRefreshBssCache(PWLANSVC_INTERFACE Iface)
{
    PNWIFI_BSS_LIST results = NULL;
    PNWIFI_BSS_ENTRY entry;
    ULONG off, i;
    DWORD dwResult;

    dwResult = NwifiGetBssList(Iface->NwifiIndex, Iface->UpperLuid,
                               &Iface->MacAddress, &results);
    if (dwResult != ERROR_SUCCESS || results == NULL)
        return (dwResult != ERROR_SUCCESS) ? dwResult : ERROR_NOT_ENOUGH_MEMORY;

    WlanSvcFlushBssList(Iface);

    off = results->FirstEntryOffset;
    for (i = 0; i < results->NumberOfItems && i < NWIFI_MAX_BSS_ENTRIES; i++)
    {
        PWLANSVC_BSS_ENTRY bss;

        if (off + FIELD_OFFSET(NWIFI_BSS_ENTRY, IeOffset) > results->Size)
            break;

        entry = (PNWIFI_BSS_ENTRY)((PUCHAR)results + off);
        if (entry->Size == 0 || off + entry->Size > results->Size)
            break;

        bss = WlanSvcImportBss(entry);
        if (bss != NULL)
        {
            InsertTailList(&Iface->BssListHead, &bss->ListEntry);
            Iface->BssCount++;
        }

        off += entry->Size;
    }

    HeapFree(GetProcessHeap(), 0, results);
    return ERROR_SUCCESS;
}

/*
 * Issue a scan and refresh the BSS cache.  The scan IOCTL only kicks
 * OID_DOT11_SCAN_REQUEST; the cache is seeded with whatever the driver already
 * holds and refreshed again on the scan-complete notification.
 */
DWORD
WlanSvcDoScan(PWLANSVC_INTERFACE Iface, PDOT11_SSID pSsid)
{
    DWORD dwResult;

    dwResult = NwifiScan(Iface->NwifiIndex, pSsid, dot11_BSS_type_any);
    if (dwResult != ERROR_SUCCESS && dwResult != ERROR_NOT_READY)
        return dwResult;

    return WlanSvcRefreshBssCache(Iface);
}

/* Find a stored profile targeting this BSS's SSID. */
static PWLANSVC_PROFILE
WlanSvcMatchProfileForBss(PWLANSVC_INTERFACE Iface, PWLANSVC_BSS_ENTRY bss)
{
    PLIST_ENTRY entry;

    for (entry = Iface->ProfileListHead.Flink;
         entry != &Iface->ProfileListHead;
         entry = entry->Flink)
    {
        PWLANSVC_PROFILE prof =
            CONTAINING_RECORD(entry, WLANSVC_PROFILE, ListEntry);

        if (prof->Ssid.uSSIDLength == bss->Ssid.uSSIDLength &&
            memcmp(prof->Ssid.ucSSID, bss->Ssid.ucSSID,
                   bss->Ssid.uSSIDLength) == 0)
        {
            return prof;
        }
    }
    return NULL;
}

/*
 * Build a WLAN_AVAILABLE_NETWORK_LIST from the BSS cache.  Networks are keyed
 * by (SSID, BSS type): same-SSID BSSes collapse into one entry, counted in
 * uNumberOfBssids, with signal/security taken from the strongest BSS.
 */
DWORD
WlanSvcBuildAvailableNetworkList(PWLANSVC_INTERFACE Iface,
                                 DWORD dwFlags,
                                 PWLAN_AVAILABLE_NETWORK_LIST *ppList)
{
    PWLAN_AVAILABLE_NETWORK_LIST list;
    PLIST_ENTRY entry;
    DWORD capacity, count = 0;
    SIZE_T size;

    UNREFERENCED_PARAMETER(dwFlags);
    *ppList = NULL;

    capacity = Iface->BssCount;
    /* Always allocate at least one slot so size math stays simple. */
    size = FIELD_OFFSET(WLAN_AVAILABLE_NETWORK_LIST, Network) +
           (SIZE_T)(capacity == 0 ? 1 : capacity) * sizeof(WLAN_AVAILABLE_NETWORK);

    list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!list)
        return ERROR_NOT_ENOUGH_MEMORY;

    for (entry = Iface->BssListHead.Flink;
         entry != &Iface->BssListHead;
         entry = entry->Flink)
    {
        PWLANSVC_BSS_ENTRY bss =
            CONTAINING_RECORD(entry, WLANSVC_BSS_ENTRY, ListEntry);
        PWLAN_AVAILABLE_NETWORK net = NULL;
        PWLANSVC_PROFILE prof;
        DWORD j;

        /* Coalesce by SSID + BSS type. */
        for (j = 0; j < count; j++)
        {
            if (list->Network[j].dot11Ssid.uSSIDLength == bss->Ssid.uSSIDLength &&
                memcmp(list->Network[j].dot11Ssid.ucSSID, bss->Ssid.ucSSID,
                       bss->Ssid.uSSIDLength) == 0 &&
                list->Network[j].dot11BssType == bss->BssType)
            {
                net = &list->Network[j];
                break;
            }
        }

        if (net == NULL)
        {
            net = &list->Network[count++];
            net->dot11Ssid = bss->Ssid;
            net->dot11BssType = bss->BssType;
            net->uNumberOfBssids = 0;
            net->bNetworkConnectable = TRUE;
            net->wlanNotConnectableReason = 0;
            net->uNumberOfPhyTypes = 1;
            net->dot11PhyTypes[0] = bss->PhyType;
            net->bMorePhyTypes = FALSE;
            net->wlanSignalQuality = bss->LinkQuality;
            net->bSecurityEnabled = bss->SecurityEnabled;
            net->dot11DefaultAuthAlgorithm = bss->DefaultAuth;
            net->dot11DefaultCipherAlgorithm = bss->DefaultCipher;
            net->dwFlags = 0;
            net->dwReserved = 0;

            prof = WlanSvcMatchProfileForBss(Iface, bss);
            if (prof)
            {
                net->dwFlags |= WLAN_AVAILABLE_NETWORK_HAS_PROFILE;
                wcsncpy(net->strProfileName, prof->Name, WLAN_MAX_NAME_LENGTH - 1);
            }

            if (Iface->Connected &&
                Iface->ConnectedSsid.uSSIDLength == bss->Ssid.uSSIDLength &&
                memcmp(Iface->ConnectedSsid.ucSSID, bss->Ssid.ucSSID,
                       bss->Ssid.uSSIDLength) == 0)
            {
                net->dwFlags |= WLAN_AVAILABLE_NETWORK_CONNECTED;
            }
        }
        else if (bss->LinkQuality > net->wlanSignalQuality)
        {
            /* keep the strongest BSS's signal/security summary */
            net->wlanSignalQuality = bss->LinkQuality;
            net->bSecurityEnabled = bss->SecurityEnabled;
            net->dot11DefaultAuthAlgorithm = bss->DefaultAuth;
            net->dot11DefaultCipherAlgorithm = bss->DefaultCipher;
        }

        net->uNumberOfBssids++;
    }

    list->dwNumberOfItems = count;
    list->dwIndex = 0;
    *ppList = list;
    return ERROR_SUCCESS;
}

/*
 * Build a flat WLAN_BSS_LIST, optionally filtered by SSID / BSS type /
 * security.  Returned as a self-contained buffer whose dwTotalSize covers the
 * whole allocation, matching the RPC contract (size_is(*dwBssListSize) LPBYTE).
 */
DWORD
WlanSvcBuildBssList(PWLANSVC_INTERFACE Iface,
                    PDOT11_SSID pSsid,
                    DOT11_BSS_TYPE BssType,
                    BOOL bSecurityEnabled,
                    PWLAN_BSS_LIST *ppList,
                    PDWORD pdwSize)
{
    PWLAN_BSS_LIST list;
    PLIST_ENTRY entry;
    DWORD count = 0;
    SIZE_T size;
    BOOL filterSsid = (pSsid != NULL && pSsid->uSSIDLength != 0);
    BOOL filterType = (BssType != dot11_BSS_type_any);

    *ppList = NULL;
    *pdwSize = 0;

    /* Count matches first. */
    for (entry = Iface->BssListHead.Flink;
         entry != &Iface->BssListHead;
         entry = entry->Flink)
    {
        PWLANSVC_BSS_ENTRY bss =
            CONTAINING_RECORD(entry, WLANSVC_BSS_ENTRY, ListEntry);

        if (filterSsid &&
            (bss->Ssid.uSSIDLength != pSsid->uSSIDLength ||
             memcmp(bss->Ssid.ucSSID, pSsid->ucSSID, pSsid->uSSIDLength) != 0))
            continue;
        if (filterType && bss->BssType != BssType)
            continue;
        if (bSecurityEnabled && !bss->SecurityEnabled)
            continue;

        count++;
    }

    size = FIELD_OFFSET(WLAN_BSS_LIST, wlanBssEntries) +
           (SIZE_T)(count == 0 ? 1 : count) * sizeof(WLAN_BSS_ENTRY);

    list = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
    if (!list)
        return ERROR_NOT_ENOUGH_MEMORY;

    list->dwTotalSize = (DWORD)size;
    list->dwNumberOfItems = count;

    count = 0;
    for (entry = Iface->BssListHead.Flink;
         entry != &Iface->BssListHead;
         entry = entry->Flink)
    {
        PWLANSVC_BSS_ENTRY bss =
            CONTAINING_RECORD(entry, WLANSVC_BSS_ENTRY, ListEntry);
        PWLAN_BSS_ENTRY out;
        ULONG r;

        if (filterSsid &&
            (bss->Ssid.uSSIDLength != pSsid->uSSIDLength ||
             memcmp(bss->Ssid.ucSSID, pSsid->ucSSID, pSsid->uSSIDLength) != 0))
            continue;
        if (filterType && bss->BssType != BssType)
            continue;
        if (bSecurityEnabled && !bss->SecurityEnabled)
            continue;

        out = &list->wlanBssEntries[count++];
        out->dot11Ssid = bss->Ssid;
        out->uPhyId = 0;
        out->dot11Bssid = bss->Bssid;
        out->dot11BssType = bss->BssType;
        out->dot11BssPhyType = bss->PhyType;
        out->lRssi = bss->Rssi;
        out->uLinkQuality = bss->LinkQuality;
        out->bInRegDomain = TRUE;
        out->usBeaconPeriod = bss->BeaconPeriod;
        out->ullTimestamp = bss->BeaconTimestamp;
        out->ullHostTimestamp = bss->HostTimestamp;
        out->usCapabilityInformation = bss->CapabilityInformation;
        out->ulChCenterFrequency = bss->ChCenterFrequency;
        out->wlanRateSet.uRateSetLength = bss->RateCount;
        for (r = 0; r < bss->RateCount; r++)
            out->wlanRateSet.usRateSet[r] = bss->Rates[r];
        /* No IE blob is appended after the entry array; report it as empty. */
        out->ulIeOffset = 0;
        out->ulIeSize = 0;
    }

    *ppList = list;
    *pdwSize = (DWORD)size;
    return ERROR_SUCCESS;
}
