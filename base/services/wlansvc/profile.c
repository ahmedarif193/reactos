/*
 * PROJECT:     ReactOS WLAN Service
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        base/services/wlansvc/profile.c
 * PURPOSE:     Profile storage + minimal WLAN profile XML parser
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Minimal element-driven parser for the Windows WLANProfile XML schema (not a
 * full XML parser), covering open and WPA2-PSK profiles; no EAP/enterprise.
 *
 * All store routines run with WlanSvcLock held by the caller.
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

/*
 * Return a pointer just past the first "<tag ...>", or NULL.  Tag matching is
 * case-insensitive and tolerates attributes.
 */
static LPCWSTR
XmlFindOpen(LPCWSTR xml, LPCWSTR tag)
{
    size_t tlen = wcslen(tag);
    LPCWSTR p = xml;

    while ((p = wcschr(p, L'<')) != NULL)
    {
        LPCWSTR q = p + 1;
        if (_wcsnicmp(q, tag, tlen) == 0)
        {
            WCHAR c = q[tlen];
            if (c == L'>' || c == L' ' || c == L'\t' || c == L'\r' || c == L'\n' || c == L'/')
            {
                /* advance to end of the opening tag */
                LPCWSTR gt = wcschr(q, L'>');
                if (gt == NULL)
                    return NULL;
                if (gt > q && gt[-1] == L'/')
                    return NULL;   /* self-closed, no text content */
                return gt + 1;
            }
        }
        p++;
    }
    return NULL;
}

/*
 * Extract the text content of the first <tag>...</tag> into out (NUL-terminated,
 * up to outChars-1 chars).  Returns TRUE on success.  Leading/trailing ASCII
 * whitespace is trimmed.
 */
static BOOL
XmlGetText(LPCWSTR xml, LPCWSTR tag, LPWSTR out, size_t outChars)
{
    LPCWSTR start = XmlFindOpen(xml, tag);
    LPCWSTR end;
    size_t n;

    if (start == NULL)
        return FALSE;

    while (*start == L' ' || *start == L'\t' || *start == L'\r' || *start == L'\n')
        start++;

    end = wcschr(start, L'<');
    if (end == NULL)
        return FALSE;

    n = (size_t)(end - start);
    while (n > 0 && (start[n - 1] == L' ' || start[n - 1] == L'\t' ||
                     start[n - 1] == L'\r' || start[n - 1] == L'\n'))
        n--;

    if (n >= outChars)
        n = outChars - 1;
    memcpy(out, start, n * sizeof(WCHAR));
    out[n] = L'\0';
    return TRUE;
}

/* Convert a UTF-16 SSID name into a DOT11_SSID (octet copy of the low byte). */
static BOOL
SsidFromText(LPCWSTR text, PDOT11_SSID ssid)
{
    size_t len = wcslen(text);
    size_t i;

    if (len == 0 || len > DOT11_SSID_MAX_LENGTH)
        return FALSE;

    ssid->uSSIDLength = (ULONG)len;
    for (i = 0; i < len; i++)
        ssid->ucSSID[i] = (UCHAR)text[i];
    return TRUE;
}

VOID
WlanSvcFreeProfile(PWLANSVC_PROFILE Profile)
{
    if (Profile == NULL)
        return;
    if (Profile->Xml != NULL)
        HeapFree(GetProcessHeap(), 0, Profile->Xml);
    HeapFree(GetProcessHeap(), 0, Profile);
}

/*
 * Parse profile XML into a freshly-allocated WLANSVC_PROFILE.  Caller owns the
 * result (free with WlanSvcFreeProfile).  The original XML is duplicated and
 * stored verbatim for WlanGetProfile.
 */
DWORD
WlanSvcParseProfileXml(LPCWSTR Xml, PWLANSVC_PROFILE *ppProfile)
{
    PWLANSVC_PROFILE prof;
    WCHAR buf[512];
    size_t xmlLen;

    *ppProfile = NULL;
    if (Xml == NULL)
        return ERROR_INVALID_PARAMETER;

    prof = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*prof));
    if (prof == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    /* Profile <name> (the top-level one, used as the store key). */
    if (XmlGetText(Xml, L"name", buf, ARRAYSIZE(buf)))
        wcsncpy(prof->Name, buf, WLAN_MAX_NAME_LENGTH - 1);

    /* The SSID's own <name> lives under <SSIDConfig><SSID>; search from there
     * so the profile-level <name> is not matched again. */
    {
        LPCWSTR ssidScope = wcsstr(Xml, L"<SSID>");
        if (ssidScope == NULL)
            ssidScope = wcsstr(Xml, L"<SSIDConfig");
        if (ssidScope != NULL && XmlGetText(ssidScope, L"name", buf, ARRAYSIZE(buf)))
            SsidFromText(buf, &prof->Ssid);
    }

    /* If no SSID element, fall back to the profile name as the SSID. */
    if (prof->Ssid.uSSIDLength == 0 && prof->Name[0] != L'\0')
        SsidFromText(prof->Name, &prof->Ssid);

    if (prof->Name[0] == L'\0' && prof->Ssid.uSSIDLength != 0)
    {
        ULONG i;
        for (i = 0; i < prof->Ssid.uSSIDLength; i++)
            prof->Name[i] = (WCHAR)prof->Ssid.ucSSID[i];
        prof->Name[prof->Ssid.uSSIDLength] = L'\0';
    }

    if (prof->Name[0] == L'\0' || prof->Ssid.uSSIDLength == 0)
    {
        WlanSvcFreeProfile(prof);
        return ERROR_BAD_PROFILE;
    }

    /* connectionType (ESS=infrastructure, IBSS=adhoc). */
    prof->BssType = dot11_BSS_type_infrastructure;
    if (XmlGetText(Xml, L"connectionType", buf, ARRAYSIZE(buf)))
    {
        if (_wcsicmp(buf, L"IBSS") == 0)
            prof->BssType = dot11_BSS_type_independent;
    }

    prof->AutoConnect = TRUE;
    if (XmlGetText(Xml, L"connectionMode", buf, ARRAYSIZE(buf)))
    {
        if (_wcsicmp(buf, L"manual") == 0)
            prof->AutoConnect = FALSE;
    }

    prof->Auth = DOT11_AUTH_ALGO_80211_OPEN;
    prof->Cipher = DOT11_CIPHER_ALGO_NONE;
    prof->SecurityEnabled = FALSE;

    if (XmlGetText(Xml, L"authentication", buf, ARRAYSIZE(buf)))
    {
        if (_wcsicmp(buf, L"WPA2PSK") == 0 || _wcsicmp(buf, L"RSNAPSK") == 0)
            prof->Auth = DOT11_AUTH_ALGO_RSNA_PSK;
        else if (_wcsicmp(buf, L"WPAPSK") == 0)
            prof->Auth = DOT11_AUTH_ALGO_WPA_PSK;
        else if (_wcsicmp(buf, L"WPA2") == 0)
            prof->Auth = DOT11_AUTH_ALGO_RSNA;
        else if (_wcsicmp(buf, L"WPA") == 0)
            prof->Auth = DOT11_AUTH_ALGO_WPA;
        else if (_wcsicmp(buf, L"shared") == 0)
            prof->Auth = DOT11_AUTH_ALGO_80211_SHARED_KEY;
        else /* "open" */
            prof->Auth = DOT11_AUTH_ALGO_80211_OPEN;
    }

    if (XmlGetText(Xml, L"encryption", buf, ARRAYSIZE(buf)))
    {
        if (_wcsicmp(buf, L"AES") == 0)
            prof->Cipher = DOT11_CIPHER_ALGO_CCMP;
        else if (_wcsicmp(buf, L"TKIP") == 0)
            prof->Cipher = DOT11_CIPHER_ALGO_TKIP;
        else if (_wcsicmp(buf, L"WEP") == 0)
            prof->Cipher = DOT11_CIPHER_ALGO_WEP;
        else
            prof->Cipher = DOT11_CIPHER_ALGO_NONE;
    }

    prof->SecurityEnabled = (prof->Auth != DOT11_AUTH_ALGO_80211_OPEN);

    /* keyMaterial (passphrase / PSK), carried opaquely for the supplicant. */
    if (XmlGetText(Xml, L"keyMaterial", buf, ARRAYSIZE(buf)))
    {
        size_t klen = wcslen(buf);
        size_t i;
        if (klen > NWIFI_MAX_PSK_BYTES)
            klen = NWIFI_MAX_PSK_BYTES;
        for (i = 0; i < klen; i++)
            prof->Key[i] = (UCHAR)buf[i];
        prof->KeyLength = (ULONG)klen;
        SecureZeroMemory(buf, sizeof(buf));
    }

    /* Duplicate the source XML for later retrieval. */
    xmlLen = (wcslen(Xml) + 1) * sizeof(WCHAR);
    prof->Xml = HeapAlloc(GetProcessHeap(), 0, xmlLen);
    if (prof->Xml != NULL)
        memcpy(prof->Xml, Xml, xmlLen);

    *ppProfile = prof;
    return ERROR_SUCCESS;
}

/* Lock held by caller. */
PWLANSVC_PROFILE
WlanSvcFindProfile(PWLANSVC_INTERFACE Iface, LPCWSTR Name)
{
    PLIST_ENTRY entry;

    if (Name == NULL)
        return NULL;

    for (entry = Iface->ProfileListHead.Flink;
         entry != &Iface->ProfileListHead;
         entry = entry->Flink)
    {
        PWLANSVC_PROFILE prof =
            CONTAINING_RECORD(entry, WLANSVC_PROFILE, ListEntry);
        if (_wcsicmp(prof->Name, Name) == 0)
            return prof;
    }
    return NULL;
}

DWORD
WlanSvcSetProfile(PWLANSVC_INTERFACE Iface,
                  DWORD dwFlags,
                  LPCWSTR Xml,
                  BOOL bOverwrite,
                  PDWORD pdwReasonCode)
{
    PWLANSVC_PROFILE prof, existing;
    DWORD dwResult;

    if (pdwReasonCode)
        *pdwReasonCode = WLAN_REASON_CODE_SUCCESS;

    dwResult = WlanSvcParseProfileXml(Xml, &prof);
    if (dwResult != ERROR_SUCCESS)
    {
        if (pdwReasonCode)
            *pdwReasonCode = dwResult; /* coarse reason code */
        return dwResult;
    }

    prof->Flags = dwFlags;

    existing = WlanSvcFindProfile(Iface, prof->Name);
    if (existing != NULL)
    {
        if (!bOverwrite)
        {
            WlanSvcFreeProfile(prof);
            return ERROR_ALREADY_EXISTS;
        }
        RemoveEntryList(&existing->ListEntry);
        Iface->ProfileCount--;
        WlanSvcFreeProfile(existing);
    }

    InsertTailList(&Iface->ProfileListHead, &prof->ListEntry);
    Iface->ProfileCount++;

    WlanSvcIndicateAcm(Iface, wlan_notification_acm_profile_change);
    return ERROR_SUCCESS;
}

DWORD
WlanSvcGetProfile(PWLANSVC_INTERFACE Iface,
                  LPCWSTR Name,
                  LPWSTR *ppXml,
                  PDWORD pdwFlags)
{
    PWLANSVC_PROFILE prof;
    size_t len;
    LPWSTR copy;

    *ppXml = NULL;

    prof = WlanSvcFindProfile(Iface, Name);
    if (prof == NULL)
        return ERROR_NOT_FOUND;

    if (prof->Xml == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;

    len = (wcslen(prof->Xml) + 1) * sizeof(WCHAR);
    copy = midl_user_allocate(len);
    if (copy == NULL)
        return ERROR_NOT_ENOUGH_MEMORY;
    memcpy(copy, prof->Xml, len);

    *ppXml = copy;
    if (pdwFlags)
        *pdwFlags = prof->Flags;
    return ERROR_SUCCESS;
}

DWORD
WlanSvcDeleteProfile(PWLANSVC_INTERFACE Iface, LPCWSTR Name)
{
    PWLANSVC_PROFILE prof = WlanSvcFindProfile(Iface, Name);

    if (prof == NULL)
        return ERROR_NOT_FOUND;

    RemoveEntryList(&prof->ListEntry);
    Iface->ProfileCount--;
    WlanSvcFreeProfile(prof);

    WlanSvcIndicateAcm(Iface, wlan_notification_acm_profile_change);
    return ERROR_SUCCESS;
}
