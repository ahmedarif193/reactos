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
    size_t n = 0;

    if (outChars == 0)
        return FALSE;
    out[0] = L'\0';

    if (start == NULL)
        return FALSE;

    while (*start == L' ' || *start == L'\t' || *start == L'\r' || *start == L'\n')
        start++;

    end = wcschr(start, L'<');
    if (end == NULL)
        return FALSE;

    while (end > start && (end[-1] == L' ' || end[-1] == L'\t' ||
                           end[-1] == L'\r' || end[-1] == L'\n'))
        end--;

    while (start < end)
    {
        WCHAR value = *start++;

        if (value == L'&')
        {
            SIZE_T remaining = (SIZE_T)(end - start);

            if (remaining >= 4 && wcsncmp(start, L"amp;", 4) == 0)
            {
                value = L'&';
                start += 4;
            }
            else if (remaining >= 3 && wcsncmp(start, L"lt;", 3) == 0)
            {
                value = L'<';
                start += 3;
            }
            else if (remaining >= 3 && wcsncmp(start, L"gt;", 3) == 0)
            {
                value = L'>';
                start += 3;
            }
            else if (remaining >= 5 && wcsncmp(start, L"quot;", 5) == 0)
            {
                value = L'\"';
                start += 5;
            }
            else if (remaining >= 5 && wcsncmp(start, L"apos;", 5) == 0)
            {
                value = L'\'';
                start += 5;
            }
            else if (remaining >= 3 && start[0] == L'#')
            {
                LPCWSTR digit = start + 1;
                ULONG base = 10;
                ULONG code = 0;
                BOOL haveDigit = FALSE;

                if (digit < end && (*digit == L'x' || *digit == L'X'))
                {
                    base = 16;
                    digit++;
                }
                while (digit < end && *digit != L';')
                {
                    ULONG d;

                    if (*digit >= L'0' && *digit <= L'9')
                        d = *digit - L'0';
                    else if (base == 16 && *digit >= L'a' && *digit <= L'f')
                        d = *digit - L'a' + 10;
                    else if (base == 16 && *digit >= L'A' && *digit <= L'F')
                        d = *digit - L'A' + 10;
                    else
                        return FALSE;
                    if (d >= base || code > (0xFFFF - d) / base)
                        return FALSE;
                    code = code * base + d;
                    haveDigit = TRUE;
                    digit++;
                }
                if (!haveDigit || digit == end || code == 0 ||
                    (code >= 0xD800 && code <= 0xDFFF))
                {
                    return FALSE;
                }
                value = (WCHAR)code;
                start = digit + 1;
            }
            else
            {
                return FALSE;
            }
        }

        if (n + 1 >= outChars)
            return FALSE;
        out[n++] = value;
    }
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
    SIZE_T XmlBytes;

    if (Profile == NULL)
        return;
    if (Profile->Xml != NULL)
    {
        XmlBytes = (wcslen(Profile->Xml) + 1) * sizeof(WCHAR);
        SecureZeroMemory(Profile->Xml, XmlBytes);
        HeapFree(GetProcessHeap(), 0, Profile->Xml);
    }
    SecureZeroMemory(Profile, sizeof(*Profile));
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
        else if (_wcsicmp(buf, L"WPA3SAE") == 0)
            prof->Auth = DOT11_AUTH_ALGO_WPA3_SAE;
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
        {
            SecureZeroMemory(buf, sizeof(buf));
            WlanSvcFreeProfile(prof);
            return ERROR_BAD_PROFILE;
        }
        for (i = 0; i < klen; i++)
        {
            if (buf[i] > 0x7F)
            {
                SecureZeroMemory(buf, sizeof(buf));
                WlanSvcFreeProfile(prof);
                return ERROR_BAD_PROFILE;
            }
            prof->Key[i] = (UCHAR)buf[i];
        }
        prof->KeyLength = (ULONG)klen;
        SecureZeroMemory(buf, sizeof(buf));
    }

    if ((prof->Auth == DOT11_AUTH_ALGO_RSNA_PSK ||
         prof->Auth == DOT11_AUTH_ALGO_WPA_PSK) &&
        (prof->KeyLength < 8 || prof->KeyLength > NWIFI_MAX_PSK_BYTES))
    {
        WlanSvcFreeProfile(prof);
        return ERROR_BAD_PROFILE;
    }
    if ((prof->Auth == DOT11_AUTH_ALGO_RSNA_PSK ||
         prof->Auth == DOT11_AUTH_ALGO_WPA_PSK) &&
        prof->KeyLength == NWIFI_MAX_PSK_BYTES)
    {
        ULONG i;

        for (i = 0; i < prof->KeyLength; i++)
        {
            UCHAR c = prof->Key[i];

            if (!((c >= '0' && c <= '9') ||
                  (c >= 'a' && c <= 'f') ||
                  (c >= 'A' && c <= 'F')))
            {
                WlanSvcFreeProfile(prof);
                return ERROR_BAD_PROFILE;
            }
        }
    }
    if (prof->Auth == DOT11_AUTH_ALGO_WPA3_SAE &&
        (prof->KeyLength == 0 || prof->KeyLength >= NWIFI_MAX_PSK_BYTES))
    {
        WlanSvcFreeProfile(prof);
        return ERROR_BAD_PROFILE;
    }

    /* Duplicate the source XML for later retrieval. */
    xmlLen = (wcslen(Xml) + 1) * sizeof(WCHAR);
    prof->Xml = HeapAlloc(GetProcessHeap(), 0, xmlLen);
    if (prof->Xml == NULL)
    {
        WlanSvcFreeProfile(prof);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
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

/* Blank the <keyMaterial> text in a mutable XML copy (-> <keyMaterial></keyMaterial>). */
static VOID
WlanSvcRedactKeyMaterial(LPWSTR Xml)
{
    LPWSTR start = (LPWSTR)XmlFindOpen(Xml, L"keyMaterial");
    LPWSTR end;
    size_t tail;

    if (start == NULL)
        return;

    end = wcschr(start, L'<');
    if (end == NULL || end <= start)
        return;

    tail = wcslen(end) + 1;
    memmove(start, end, tail * sizeof(WCHAR));
}

DWORD
WlanSvcGetProfile(PWLANSVC_INTERFACE Iface,
                  LPCWSTR Name,
                  BOOL bPlaintextKey,
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

    /* Never hand the cleartext PSK to an unprivileged caller. */
    if (!bPlaintextKey)
        WlanSvcRedactKeyMaterial(copy);

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
