/*
 * PROJECT:     ReactOS Native WiFi
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Command-line scan of available 802.11 networks
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <windows.h>
#include <wlanapi.h>
#include <stdio.h>

static void
WlanEmit(PCWSTR Format, ...)
{
    WCHAR WideBuffer[1024];
    CHAR AnsiBuffer[1024];
    va_list Args;

    va_start(Args, Format);
    _vsnwprintf(WideBuffer, ARRAYSIZE(WideBuffer) - 1, Format, Args);
    va_end(Args);
    WideBuffer[ARRAYSIZE(WideBuffer) - 1] = L'\0';

    fputws(WideBuffer, stdout);
    fflush(stdout);

    WideCharToMultiByte(CP_UTF8, 0, WideBuffer, -1, AnsiBuffer, sizeof(AnsiBuffer), NULL, NULL);
    OutputDebugStringA(AnsiBuffer);
}
#define wprintf WlanEmit

static PCWSTR
AuthName(DOT11_AUTH_ALGORITHM Auth)
{
    switch (Auth)
    {
        case DOT11_AUTH_ALGO_80211_OPEN:       return L"Open";
        case DOT11_AUTH_ALGO_80211_SHARED_KEY: return L"Shared";
        case DOT11_AUTH_ALGO_WPA:              return L"WPA";
        case DOT11_AUTH_ALGO_WPA_PSK:          return L"WPA-PSK";
        case DOT11_AUTH_ALGO_RSNA:             return L"WPA2";
        case DOT11_AUTH_ALGO_RSNA_PSK:         return L"WPA2-PSK";
        default:                               return L"?";
    }
}

/* The SSID is a raw byte array; render it as text for display. */
static VOID
SsidToString(const DOT11_SSID *Ssid, WCHAR *Buffer, int cch)
{
    int len;

    if (Ssid->uSSIDLength == 0)
    {
        lstrcpynW(Buffer, L"<hidden>", cch);
        return;
    }

    len = MultiByteToWideChar(CP_UTF8, 0, (const char *)Ssid->ucSSID,
                              Ssid->uSSIDLength, Buffer, cch - 1);
    if (len <= 0)
        len = 0;
    Buffer[len] = L'\0';
}

static VOID WINAPI
ScanNotify(PWLAN_NOTIFICATION_DATA Data, PVOID Context)
{
    if (Data != NULL && Data->NotificationSource == WLAN_NOTIFICATION_SOURCE_ACM && (Data->NotificationCode == wlan_notification_acm_scan_complete || Data->NotificationCode == wlan_notification_acm_scan_fail))
    {
        SetEvent((HANDLE)Context);
    }
}

static DWORD
ScanInterface(HANDLE hClient, const GUID *pGuid)
{
    DWORD dwResult, i;
    PWLAN_AVAILABLE_NETWORK_LIST pNetList = NULL;
    HANDLE ScanDone;
    DWORD PrevSource = 0;

    wprintf(L"  Scanning...\n");

    ScanDone = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ScanDone != NULL)
        WlanRegisterNotification(hClient, WLAN_NOTIFICATION_SOURCE_ACM, FALSE, ScanNotify, ScanDone, NULL, &PrevSource);

    dwResult = WlanScan(hClient, pGuid, NULL, NULL, NULL);
    if (dwResult != ERROR_SUCCESS)
        wprintf(L"  (WlanScan returned %lu; reading cached results anyway)\n", dwResult);

    if (ScanDone != NULL)
    {
        WaitForSingleObject(ScanDone, 10000);
        WlanRegisterNotification(hClient, WLAN_NOTIFICATION_SOURCE_NONE, FALSE, NULL, NULL, NULL, &PrevSource);
        CloseHandle(ScanDone);
    }
    else
    {
        Sleep(4000);
    }

    dwResult = WlanGetAvailableNetworkList(hClient, pGuid,
                   WLAN_AVAILABLE_NETWORK_INCLUDE_ALL_MANUAL_HIDDEN_PROFILES,
                   NULL, &pNetList);
    if (dwResult != ERROR_SUCCESS || pNetList == NULL)
    {
        wprintf(L"  WlanGetAvailableNetworkList failed: %lu\n", dwResult);
        return dwResult;
    }

    if (pNetList->dwNumberOfItems == 0)
    {
        wprintf(L"  No networks found.\n");
    }
    else
    {
        wprintf(L"  %-32s %5s  %-7s  %s\n", L"SSID", L"Sig%", L"Sec", L"Auth");
        wprintf(L"  ---------------------------------------------------------------\n");

        for (i = 0; i < pNetList->dwNumberOfItems; i++)
        {
            PWLAN_AVAILABLE_NETWORK n = &pNetList->Network[i];
            WCHAR szSsid[64];

            SsidToString(&n->dot11Ssid, szSsid, ARRAYSIZE(szSsid));
            wprintf(L"  %-32.32s %5lu  %-7s  %s%s\n",
                    szSsid,
                    n->wlanSignalQuality,
                    n->bSecurityEnabled ? L"secured" : L"open",
                    AuthName(n->dot11DefaultAuthAlgorithm),
                    (n->dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) ? L"  [connected]" : L"");
        }
        wprintf(L"  (%lu network(s))\n", pNetList->dwNumberOfItems);
    }

    WlanFreeMemory(pNetList);
    return ERROR_SUCCESS;
}

int
wmain(int argc, WCHAR *argv[])
{
    HANDLE hClient = NULL;
    DWORD dwVersion = 0, dwResult, i;
    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;

    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);

    wprintf(L"ReactOS Native WiFi scan\n\n");

    dwResult = WlanOpenHandle(WLAN_API_VERSION_2_0, NULL, &dwVersion, &hClient);
    if (dwResult != ERROR_SUCCESS)
    {
        wprintf(L"WlanOpenHandle failed: %lu "
                L"(is the WLAN AutoConfig service running?)\n", dwResult);
        return 1;
    }

    dwResult = WlanEnumInterfaces(hClient, NULL, &pIfList);
    if (dwResult != ERROR_SUCCESS || pIfList == NULL)
    {
        wprintf(L"WlanEnumInterfaces failed: %lu\n", dwResult);
        WlanCloseHandle(hClient, NULL);
        return 1;
    }

    if (pIfList->dwNumberOfItems == 0)
        wprintf(L"No wireless interfaces present.\n");

    for (i = 0; i < pIfList->dwNumberOfItems; i++)
    {
        PWLAN_INTERFACE_INFO pIf = &pIfList->InterfaceInfo[i];

        wprintf(L"Interface %lu: %s\n", i, pIf->strInterfaceDescription);
        ScanInterface(hClient, &pIf->InterfaceGuid);
        wprintf(L"\n");
    }

    WlanFreeMemory(pIfList);
    WlanCloseHandle(hClient, NULL);
    return 0;
}
