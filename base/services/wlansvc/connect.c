/*
 * PROJECT:     ReactOS WLAN Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        base/services/wlansvc/connect.c
 * PURPOSE:     Connect / disconnect / interface state query
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * All routines run with WlanSvcLock held by the caller.
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

/* Find the strongest cached BSS matching an SSID (+ optional BSS type). */
static PWLANSVC_BSS_ENTRY
WlanSvcPickBss(PWLANSVC_INTERFACE Iface, PDOT11_SSID pSsid, DOT11_BSS_TYPE BssType)
{
    PLIST_ENTRY entry;
    PWLANSVC_BSS_ENTRY best = NULL;

    for (entry = Iface->BssListHead.Flink;
         entry != &Iface->BssListHead;
         entry = entry->Flink)
    {
        PWLANSVC_BSS_ENTRY bss =
            CONTAINING_RECORD(entry, WLANSVC_BSS_ENTRY, ListEntry);

        if (pSsid->uSSIDLength != bss->Ssid.uSSIDLength ||
            memcmp(pSsid->ucSSID, bss->Ssid.ucSSID, pSsid->uSSIDLength) != 0)
            continue;
        if (BssType != dot11_BSS_type_any && bss->BssType != BssType)
            continue;

        if (best == NULL || bss->Rssi > best->Rssi)
            best = bss;
    }
    return best;
}

/*
 * Push the profile's PSK down to nwifi as a default key for the supplicant.
 * The key travels separately from the connect request.  No-op for open nets.
 */
static DWORD
WlanSvcInstallProfileKey(PWLANSVC_INTERFACE Iface,
                         PWLANSVC_PROFILE prof,
                         DOT11_CIPHER_ALGORITHM cipher)
{
    NWIFI_SET_KEY key;

    if (prof == NULL || prof->KeyLength == 0)
        return ERROR_SUCCESS;       /* open network: nothing to install */

    ZeroMemory(&key, sizeof(key));
    key.Size = sizeof(key);
    key.InterfaceIndex = Iface->NwifiIndex;
    key.Algorithm = DOT11_CIPHER_ALGO_NONE;
    key.IsPairwise = FALSE;         /* PSK/group default key */
    key.KeyId = 0;
    key.KeyLength = (prof->KeyLength <= sizeof(key.KeyData))
                        ? prof->KeyLength : sizeof(key.KeyData);
    memcpy(key.KeyData, prof->Key, key.KeyLength);

    return NwifiSetKey(&key);
}

/*
 * Connect using a stored/temporary profile or a discovery SSID.  Completion
 * is asynchronous: nwifi reports the association result on the notify
 * channel, which raises the connection-complete ACM notification.
 */
DWORD
WlanSvcConnect(PWLANSVC_INTERFACE Iface, const PWLAN_CONNECTION_PARAMETERS pParams)
{
    NWIFI_CONNECT_REQUEST req;
    PWLANSVC_PROFILE prof = NULL;
    PWLANSVC_BSS_ENTRY bss;
    DOT11_SSID ssid;
    DOT11_BSS_TYPE bssType;
    DOT11_AUTH_ALGORITHM auth = DOT11_AUTH_ALGO_80211_OPEN;
    DOT11_CIPHER_ALGORITHM cipher = DOT11_CIPHER_ALGO_NONE;
    WCHAR profileName[WLAN_MAX_NAME_LENGTH] = L"";
    DWORD dwResult;

    ZeroMemory(&ssid, sizeof(ssid));
    bssType = pParams->dot11BssType;
    if (bssType == 0)
        bssType = dot11_BSS_type_infrastructure;

    /* Resolve the target network from the connection parameters. */
    switch (pParams->wlanConnectionMode)
    {
        case wlan_connection_mode_profile:
        case wlan_connection_mode_temporary_profile:
            if (pParams->strProfile == NULL)
                return ERROR_INVALID_PARAMETER;
            prof = WlanSvcFindProfile(Iface, pParams->strProfile);
            if (prof == NULL)
                return ERROR_NOT_FOUND;
            ssid = prof->Ssid;
            bssType = prof->BssType;
            auth = prof->Auth;
            cipher = prof->Cipher;
            wcsncpy(profileName, prof->Name, WLAN_MAX_NAME_LENGTH - 1);
            break;

        case wlan_connection_mode_discovery_secure:
        case wlan_connection_mode_discovery_unsecure:
        case wlan_connection_mode_auto:
            if (pParams->pDot11Ssid == NULL)
                return ERROR_INVALID_PARAMETER;
            ssid = *pParams->pDot11Ssid;
            break;

        default:
            return ERROR_INVALID_PARAMETER;
    }

    if (ssid.uSSIDLength == 0 || ssid.uSSIDLength > DOT11_SSID_MAX_LENGTH)
        return ERROR_INVALID_PARAMETER;

    WlanSvcIndicateAcm(Iface, wlan_notification_acm_connection_start);

    /* Choose a BSS from the cache; fall back to a fresh scan if none. */
    bss = WlanSvcPickBss(Iface, &ssid, bssType);
    if (bss == NULL)
    {
        WlanSvcDoScan(Iface, &ssid);
        bss = WlanSvcPickBss(Iface, &ssid, bssType);
    }

    /* If the BSS advertises security and the profile did not pin it, adopt
     * the BSS's advertised auth/cipher (open networks stay open). */
    if (bss != NULL && bss->SecurityEnabled &&
        auth == DOT11_AUTH_ALGO_80211_OPEN)
    {
        auth = bss->DefaultAuth;
        cipher = bss->DefaultCipher;
    }

    /* Install the PSK first so it is in place before association starts. */
    if (prof != NULL && prof->KeyLength != 0)
        WlanSvcInstallProfileKey(Iface, prof, cipher);

    ZeroMemory(&req, sizeof(req));
    req.Size = sizeof(req);
    req.InterfaceIndex = Iface->NwifiIndex;
    req.Ssid = ssid;
    req.BssType = bssType;
    req.AuthAlgorithm = auth;
    req.UnicastCipher = cipher;
    req.MulticastCipher = cipher;
    if (bss != NULL)
        req.DesiredBssid = bss->Bssid;  /* all-zero => any BSSID for the SSID */

    /* Remember the intended connection so the async complete can finalise it. */
    Iface->ConnectedSsid = ssid;
    Iface->ConnectedBssType = bssType;
    Iface->ConnectedAuth = auth;
    Iface->ConnectedCipher = cipher;
    wcsncpy(Iface->ConnectedProfile, profileName, WLAN_MAX_NAME_LENGTH - 1);
    if (bss != NULL)
        Iface->ConnectedBssid = bss->Bssid;
    else
        ZeroMemory(&Iface->ConnectedBssid, sizeof(DOT11_MAC_ADDRESS));

    Iface->State = wlan_interface_state_associating;
    dwResult = NwifiConnect(&req);
    if (dwResult != ERROR_SUCCESS)
    {
        Iface->State = wlan_interface_state_disconnected;
        Iface->ConnectedProfile[0] = L'\0';
        WlanSvcIndicateConnection(Iface,
                                  wlan_notification_acm_connection_attempt_fail,
                                  pParams->wlanConnectionMode, profileName,
                                  dwResult);
        return dwResult;
    }

    return ERROR_SUCCESS;
}

DWORD
WlanSvcDisconnect(PWLANSVC_INTERFACE Iface)
{
    DWORD dwResult;

    WlanSvcIndicateAcm(Iface, wlan_notification_acm_disconnecting);
    Iface->State = wlan_interface_state_disconnecting;

    dwResult = NwifiDisconnect(Iface->NwifiIndex, Iface->UpperLuid,
                               &Iface->MacAddress);

    Iface->Connected = FALSE;
    ZeroMemory(&Iface->ConnectedSsid, sizeof(DOT11_SSID));
    ZeroMemory(&Iface->ConnectedBssid, sizeof(DOT11_MAC_ADDRESS));
    Iface->ConnectedProfile[0] = L'\0';
    Iface->State = wlan_interface_state_disconnected;
    Iface->Rssi = -100;
    Iface->LinkQuality = 0;

    WlanSvcIndicateAcm(Iface, wlan_notification_acm_disconnected);
    return dwResult;
}

/*
 * Refresh the cached connection state from nwifi's live link state.  Called
 * from the notify worker on connect/link-up events.  Lock held by caller.
 */
VOID
WlanSvcUpdateLinkState(PWLANSVC_INTERFACE Iface)
{
    NWIFI_LINK_STATE ls;

    if (NwifiQueryState(Iface->NwifiIndex, Iface->UpperLuid,
                        &Iface->MacAddress, &ls) != ERROR_SUCCESS)
        return;

    Iface->PhyType = (DOT11_PHY_TYPE)ls.PhyType;
    Iface->Rssi = ls.Rssi;
    Iface->LinkQuality = (ls.Rssi <= -100) ? 0 :
                         (ls.Rssi >= -50)  ? 100 :
                         (ULONG)(2 * (ls.Rssi + 100));

    if (ls.Status == NwifiLinkConnectedOpen ||
        ls.Status == NwifiLinkConnectedSecure)
    {
        Iface->Connected = TRUE;
        Iface->State = wlan_interface_state_connected;
        Iface->ConnectedSsid = ls.Ssid;
        Iface->ConnectedBssid = ls.Bssid;
    }
    else if (ls.Status == NwifiLinkDisconnected)
    {
        Iface->Connected = FALSE;
        Iface->State = wlan_interface_state_disconnected;
    }
}

/* Helper: allocate an RPC-owned out buffer and copy a value into it. */
static DWORD
WlanSvcReturnBlob(const void *src, DWORD size, LPBYTE *ppData, PDWORD pdwSize)
{
    LPBYTE buf = midl_user_allocate(size);
    if (buf == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(buf, src, size);
    *ppData = buf;
    *pdwSize = size;
    return ERROR_SUCCESS;
}

/* WlanQueryInterface backend; unsupported opcodes fail with ERROR_NOT_SUPPORTED. */
DWORD
WlanSvcQueryInterface(PWLANSVC_INTERFACE Iface,
                      WLAN_INTF_OPCODE OpCode,
                      PDWORD pdwDataSize,
                      LPBYTE *ppData,
                      PDWORD pValueType)
{
    *pValueType = wlan_opcode_value_type_query_only;

    switch (OpCode)
    {
        case wlan_intf_opcode_interface_state:
        {
            WLAN_INTERFACE_STATE st = Iface->State;
            return WlanSvcReturnBlob(&st, sizeof(st), ppData, pdwDataSize);
        }

        case wlan_intf_opcode_autoconf_enabled:
        {
            BOOL b = Iface->AutoConfigEnabled;
            return WlanSvcReturnBlob(&b, sizeof(b), ppData, pdwDataSize);
        }

        case wlan_intf_opcode_bss_type:
        {
            DOT11_BSS_TYPE bt = Iface->Connected ? Iface->ConnectedBssType
                                                 : dot11_BSS_type_infrastructure;
            return WlanSvcReturnBlob(&bt, sizeof(bt), ppData, pdwDataSize);
        }

        case wlan_intf_opcode_radio_state:
        {
            WLAN_RADIO_STATE rs;
            ZeroMemory(&rs, sizeof(rs));
            rs.dwNumberOfPhys = 1;
            rs.PhyRadioState[0].dwPhyIndex = 0;
            rs.PhyRadioState[0].dot11SoftwareRadioState =
                Iface->RadioOn ? dot11_radio_state_on : dot11_radio_state_off;
            rs.PhyRadioState[0].dot11HardwareRadioState =
                Iface->RadioOn ? dot11_radio_state_on : dot11_radio_state_off;
            return WlanSvcReturnBlob(&rs, sizeof(rs), ppData, pdwDataSize);
        }

        case wlan_intf_opcode_rssi:
        {
            LONG rssi = Iface->Rssi;
            return WlanSvcReturnBlob(&rssi, sizeof(rssi), ppData, pdwDataSize);
        }

        case wlan_intf_opcode_current_connection:
        {
            WLAN_CONNECTION_ATTRIBUTES ca;

            if (!Iface->Connected)
                return ERROR_INVALID_STATE;

            ZeroMemory(&ca, sizeof(ca));
            ca.isState = Iface->State;
            ca.wlanConnectionMode = (Iface->ConnectedProfile[0] != L'\0')
                                        ? wlan_connection_mode_profile
                                        : wlan_connection_mode_discovery_unsecure;
            wcsncpy(ca.strProfileName, Iface->ConnectedProfile,
                    WLAN_MAX_NAME_LENGTH - 1);

            ca.wlanAssociationAttributes.dot11Ssid = Iface->ConnectedSsid;
            ca.wlanAssociationAttributes.dot11BssType = Iface->ConnectedBssType;
            ca.wlanAssociationAttributes.dot11Bssid = Iface->ConnectedBssid;
            ca.wlanAssociationAttributes.dot11PhyType = Iface->PhyType;
            ca.wlanAssociationAttributes.uDot11PhyIndex = 0;
            ca.wlanAssociationAttributes.wlanSignalQuality = Iface->LinkQuality;
            ca.wlanAssociationAttributes.ulRxRate = 54000;
            ca.wlanAssociationAttributes.ulTxRate = 54000;

            ca.wlanSecurityAttributes.bSecurityEnabled =
                (Iface->ConnectedAuth != DOT11_AUTH_ALGO_80211_OPEN);
            ca.wlanSecurityAttributes.bOneXEnabled = FALSE;
            ca.wlanSecurityAttributes.dot11AuthAlgorithm = Iface->ConnectedAuth;
            ca.wlanSecurityAttributes.dot11CipherAlgorithm = Iface->ConnectedCipher;

            return WlanSvcReturnBlob(&ca, sizeof(ca), ppData, pdwDataSize);
        }

        default:
            DPRINT1("WlanSvcQueryInterface: unsupported opcode 0x%lx\n",
                    (DWORD)OpCode);
            return ERROR_NOT_SUPPORTED;
    }
}
