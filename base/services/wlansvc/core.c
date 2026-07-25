/*
 * PROJECT:     ReactOS WLAN Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        base/services/wlansvc/core.c
 * PURPOSE:     AutoConfig service core: interface list + lifetime + lookups
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

LIST_ENTRY       WlanSvcInterfaceListHead;
CRITICAL_SECTION WlanSvcLock;
static BOOL      WlanSvcInitialized = FALSE;
static INIT_ONCE WlanSvcInitOnce = INIT_ONCE_STATIC_INIT;

static WLAN_INTERFACE_STATE
NwifiLinkStatusToWlan(NWIFI_LINK_STATUS Status)
{
    switch (Status)
    {
        case NwifiLinkConnectedOpen:
        case NwifiLinkConnectedSecure:  return wlan_interface_state_connected;
        case NwifiLinkAssociating:      return wlan_interface_state_associating;
        case NwifiLinkScanning:         return wlan_interface_state_discovering;
        case NwifiLinkAuthenticating:   return wlan_interface_state_authenticating;
        case NwifiLinkDisconnected:
        default:                        return wlan_interface_state_disconnected;
    }
}

/*
 * nwifi keys adapters by local index + NET_LUID; the WLAN API is GUID-keyed.
 * Synthesise a stable per-interface GUID from the index, LUID and MAC.
 */
static VOID
WlanSvcSynthesizeGuid(PNWIFI_INTERFACE_REF Ref, GUID *pGuid)
{
    ZeroMemory(pGuid, sizeof(*pGuid));
    /* {00000000-0000-11EC-WIFI-<6-byte MAC>}, Data1 = nwifi index. */
    pGuid->Data1 = Ref->InterfaceIndex;
    pGuid->Data2 = (USHORT)(Ref->UpperLuid & 0xFFFF);
    pGuid->Data3 = 0x11EC;
    pGuid->Data4[0] = 'W';
    pGuid->Data4[1] = 'i';
    memcpy(&pGuid->Data4[2], Ref->MacAddress.ucDot11MacAddress, 6);
}

/* Pull the live link state from nwifi into an interface (lock held). */
static VOID
WlanSvcSeedLinkState(PWLANSVC_INTERFACE Iface)
{
    NWIFI_LINK_STATE ls;

    if (NwifiQueryState(Iface->NwifiIndex, Iface->UpperLuid,
                        &Iface->MacAddress, &ls) != ERROR_SUCCESS)
    {
        Iface->State = wlan_interface_state_disconnected;
        Iface->RadioOn = TRUE;
        return;
    }

    Iface->PhyType = (DOT11_PHY_TYPE)ls.PhyType;
    Iface->State = NwifiLinkStatusToWlan(ls.Status);
    Iface->RadioOn = TRUE;
    Iface->Rssi = ls.Rssi;

    if (ls.Status == NwifiLinkConnectedOpen ||
        ls.Status == NwifiLinkConnectedSecure)
    {
        Iface->Connected = TRUE;
        Iface->ConnectedSsid = ls.Ssid;
        Iface->ConnectedBssid = ls.Bssid;
    }
}

/*
 * Rebuild the interface list from nwifi's adapters.  Existing interfaces
 * (matched by nwifi index) keep their profile store and cached state; stale
 * ones are dropped.  Lock held by caller.
 */
static VOID
WlanSvcPopulateInterfacesLocked(VOID)
{
    PNWIFI_INTERFACE_LIST list = NULL;
    PLIST_ENTRY entry, next;
    DWORD dwResult, i;

    dwResult = NwifiEnumInterfaces(&list);
    if (dwResult != ERROR_SUCCESS || list == NULL)
    {
        DPRINT1("NwifiEnumInterfaces failed (0x%lx)\n", dwResult);
        return;
    }

    for (i = 0; i < list->NumberOfItems; i++)
    {
        PNWIFI_INTERFACE_REF ref = &list->Item[i];
        PWLANSVC_INTERFACE iface = WlanSvcFindInterfaceByIndex(ref->InterfaceIndex);

        if (iface != NULL)
        {
            /* Already known: refresh its identity + live state. */
            iface->UpperLuid = ref->UpperLuid;
            iface->MacAddress = ref->MacAddress;
            WlanSvcSeedLinkState(iface);
            continue;
        }

        iface = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*iface));
        if (!iface)
            continue;

        iface->NwifiIndex = ref->InterfaceIndex;
        iface->UpperLuid = ref->UpperLuid;
        iface->MacAddress = ref->MacAddress;
        WlanSvcSynthesizeGuid(ref, &iface->InterfaceGuid);
        _snwprintf(iface->Description, WLAN_MAX_NAME_LENGTH - 1,
                   L"ReactOS Native WiFi Adapter #%lu", ref->InterfaceIndex + 1);
        iface->PhyType = dot11_phy_type_erp;
        iface->AutoConfigEnabled = TRUE;

        InitializeListHead(&iface->BssListHead);
        InitializeListHead(&iface->ProfileListHead);
        iface->BssCount = 0;
        iface->ProfileCount = 0;
        iface->Connected = FALSE;
        iface->Rssi = -100;
        iface->LinkQuality = 0;
        iface->State = wlan_interface_state_disconnected;
        iface->RadioOn = TRUE;

        WlanSvcSeedLinkState(iface);

        InsertTailList(&WlanSvcInterfaceListHead, &iface->ListEntry);
        DPRINT("wlansvc: added interface '%S' (nwifi idx %lu)\n",
               iface->Description, ref->InterfaceIndex);
    }

    /* Drop interfaces nwifi no longer reports. */
    for (entry = WlanSvcInterfaceListHead.Flink;
         entry != &WlanSvcInterfaceListHead;
         entry = next)
    {
        PWLANSVC_INTERFACE iface =
            CONTAINING_RECORD(entry, WLANSVC_INTERFACE, ListEntry);
        BOOL stillPresent = FALSE;

        next = entry->Flink;

        for (i = 0; i < list->NumberOfItems; i++)
        {
            if (list->Item[i].InterfaceIndex == iface->NwifiIndex)
            {
                stillPresent = TRUE;
                break;
            }
        }

        if (!stillPresent)
        {
            RemoveEntryList(&iface->ListEntry);
            WlanSvcFlushBssList(iface);
            while (!IsListEmpty(&iface->ProfileListHead))
            {
                PLIST_ENTRY pe = RemoveHeadList(&iface->ProfileListHead);
                WlanSvcFreeProfile(CONTAINING_RECORD(pe, WLANSVC_PROFILE, ListEntry));
            }
            HeapFree(GetProcessHeap(), 0, iface);
        }
    }

    HeapFree(GetProcessHeap(), 0, list);
}

/* Public refresh entry (used by the notify worker on arrival/removal). */
VOID
WlanSvcRefreshInterfaces(VOID)
{
    WlanSvcPopulateInterfacesLocked();
}

/* Runs exactly once even though both the RPC listen thread and every RPC
 * client open call WlanSvcInitialize(). The previous plain-BOOL guard had a
 * check-then-act window in which a second thread re-ran
 * InitializeCriticalSection() on a WlanSvcLock the first thread was already
 * entering, losing the owner and blocking that thread forever. */
static BOOL CALLBACK
WlanSvcInitOnceCallback(
    PINIT_ONCE InitOnce,
    PVOID Parameter,
    PVOID *Context)
{
    UNREFERENCED_PARAMETER(InitOnce);
    UNREFERENCED_PARAMETER(Parameter);
    UNREFERENCED_PARAMETER(Context);

    InitializeCriticalSection(&WlanSvcLock);
    InitializeListHead(&WlanSvcInterfaceListHead);
    InitializeListHead(&WlanSvcHandleListHead);
    WlanSvcInitialized = TRUE;

    EnterCriticalSection(&WlanSvcLock);
    WlanSvcPopulateInterfacesLocked();
    LeaveCriticalSection(&WlanSvcLock);

    /* Start receiving asynchronous interface events from nwifi (no-op if the
     * driver is absent). */
    NwifiStartNotifyWorker();

    return TRUE;
}

VOID
WlanSvcInitialize(VOID)
{
    InitOnceExecuteOnce(&WlanSvcInitOnce, WlanSvcInitOnceCallback, NULL, NULL);
}

VOID
WlanSvcCleanup(VOID)
{
    PLIST_ENTRY entry;

    if (!WlanSvcInitialized)
        return;

    /* Stop the notify worker (and unblock its pended IOCTL) first. */
    NwifiStopNotifyWorker();

    EnterCriticalSection(&WlanSvcLock);
    while (!IsListEmpty(&WlanSvcInterfaceListHead))
    {
        PWLANSVC_INTERFACE iface;

        entry = RemoveHeadList(&WlanSvcInterfaceListHead);
        iface = CONTAINING_RECORD(entry, WLANSVC_INTERFACE, ListEntry);

        WlanSvcFlushBssList(iface);

        while (!IsListEmpty(&iface->ProfileListHead))
        {
            PWLANSVC_PROFILE prof;
            entry = RemoveHeadList(&iface->ProfileListHead);
            prof = CONTAINING_RECORD(entry, WLANSVC_PROFILE, ListEntry);
            WlanSvcFreeProfile(prof);
        }

        HeapFree(GetProcessHeap(), 0, iface);
    }
    LeaveCriticalSection(&WlanSvcLock);

    NwifiCloseControl();

    DeleteCriticalSection(&WlanSvcLock);
    WlanSvcInitialized = FALSE;
}

/* Lock must be held by caller. */
PWLANSVC_INTERFACE
WlanSvcFindInterface(const GUID *pInterfaceGuid)
{
    PLIST_ENTRY entry;

    if (pInterfaceGuid == NULL)
        return NULL;

    for (entry = WlanSvcInterfaceListHead.Flink;
         entry != &WlanSvcInterfaceListHead;
         entry = entry->Flink)
    {
        PWLANSVC_INTERFACE iface =
            CONTAINING_RECORD(entry, WLANSVC_INTERFACE, ListEntry);

        if (memcmp(&iface->InterfaceGuid, pInterfaceGuid, sizeof(GUID)) == 0)
            return iface;
    }

    return NULL;
}

/* Lock must be held by caller. */
PWLANSVC_INTERFACE
WlanSvcFindInterfaceByIndex(ULONG NwifiIndex)
{
    PLIST_ENTRY entry;

    for (entry = WlanSvcInterfaceListHead.Flink;
         entry != &WlanSvcInterfaceListHead;
         entry = entry->Flink)
    {
        PWLANSVC_INTERFACE iface =
            CONTAINING_RECORD(entry, WLANSVC_INTERFACE, ListEntry);

        if (iface->NwifiIndex == NwifiIndex)
            return iface;
    }

    return NULL;
}
