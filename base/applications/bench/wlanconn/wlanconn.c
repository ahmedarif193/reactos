/*
 * PROJECT:     ReactOS Native WiFi
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Command-line connect to a WPA2 network (bring-up test harness)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include <windows.h>
#include <wlanapi.h>
#include <stdio.h>
#include <wchar.h>

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

static BOOL
WlanXmlEscape(
    _Out_writes_(DestinationLength) PWCHAR Destination,
    _In_ SIZE_T DestinationLength,
    _In_ PCWSTR Source)
{
    SIZE_T Position = 0;

    while (*Source != L'\0')
    {
        PCWSTR Replacement = NULL;

        switch (*Source)
        {
            case L'&': Replacement = L"&amp;"; break;
            case L'<': Replacement = L"&lt;"; break;
            case L'>': Replacement = L"&gt;"; break;
            case L'\"': Replacement = L"&quot;"; break;
            case L'\'': Replacement = L"&apos;"; break;
        }

        if (Replacement != NULL)
        {
            SIZE_T ReplacementLength = wcslen(Replacement);

            if (Position + ReplacementLength >= DestinationLength)
                return FALSE;

            RtlCopyMemory(&Destination[Position], Replacement, ReplacementLength * sizeof(WCHAR));
            Position += ReplacementLength;
        }
        else
        {
            if (Position + 1 >= DestinationLength)
                return FALSE;

            Destination[Position++] = *Source;
        }

        Source++;
    }

    Destination[Position] = L'\0';
    return TRUE;
}

int
wmain(int argc, WCHAR **argv)
{
    HANDLE hClient = NULL;
    DWORD dwVersion = 0;
    PWLAN_INTERFACE_INFO_LIST pIfList = NULL;
    WLAN_CONNECTION_PARAMETERS Params;
    PCWSTR Ssid;
    PCWSTR Key;
    WCHAR SsidXml[512];
    WCHAR KeyXml[512];
    WCHAR ProfileXml[2048];
    INT ProfileLength;
    DWORD Result;
    DWORD Reason = 0;

    if (argc != 3)
    {
        wprintf(L"usage: wlanconn <ssid> <wpa2-passphrase>\n");
        return 1;
    }

    Ssid = argv[1];
    Key = argv[2];
    if (!WlanXmlEscape(SsidXml, ARRAYSIZE(SsidXml), Ssid) || !WlanXmlEscape(KeyXml, ARRAYSIZE(KeyXml), Key))
    {
        wprintf(L"WLANCONN: profile value is too long\n");
        return 1;
    }

    ProfileLength = _snwprintf(ProfileXml, ARRAYSIZE(ProfileXml) - 1, L"<?xml version=\"1.0\"?>\r\n" L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\r\n" L"<name>%s</name>\r\n" L"<SSIDConfig><SSID><name>%s</name></SSID></SSIDConfig>\r\n" L"<connectionType>ESS</connectionType>\r\n" L"<connectionMode>manual</connectionMode>\r\n" L"<MSM><security>\r\n" L"<authEncryption><authentication>WPA2PSK</authentication>" L"<encryption>AES</encryption><useOneX>false</useOneX></authEncryption>\r\n" L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected>" L"<keyMaterial>%s</keyMaterial></sharedKey>\r\n" L"</security></MSM>\r\n" L"</WLANProfile>\r\n", SsidXml, SsidXml, KeyXml);
    ProfileXml[ARRAYSIZE(ProfileXml) - 1] = L'\0';
    if (ProfileLength < 0 || ProfileLength >= ARRAYSIZE(ProfileXml))
    {
        wprintf(L"WLANCONN: profile XML is too long\n");
        return 1;
    }

    Result = WlanOpenHandle(2, NULL, &dwVersion, &hClient);
    if (Result != ERROR_SUCCESS)
    {
        wprintf(L"WLANCONN: WlanOpenHandle failed %lu\n", Result);
        return 1;
    }

    Result = WlanEnumInterfaces(hClient, NULL, &pIfList);
    if (Result != ERROR_SUCCESS || pIfList == NULL || pIfList->dwNumberOfItems == 0)
    {
        wprintf(L"WLANCONN: no WLAN interface (%lu)\n", Result);
        WlanCloseHandle(hClient, NULL);
        return 1;
    }

    Result = WlanSetProfile(hClient, &pIfList->InterfaceInfo[0].InterfaceGuid, 0, ProfileXml, NULL, TRUE, NULL, &Reason);
    wprintf(L"WLANCONN: WlanSetProfile 0x%lx (reason %lu)\n", Result, Reason);

    ZeroMemory(&Params, sizeof(Params));
    Params.wlanConnectionMode = wlan_connection_mode_profile;
    Params.strProfile = Ssid;
    Params.dot11BssType = dot11_BSS_type_infrastructure;

    Result = WlanConnect(hClient, &pIfList->InterfaceInfo[0].InterfaceGuid, &Params, NULL);
    wprintf(L"WLANCONN: WlanConnect 0x%lx\n", Result);

    Sleep(2000);

    WlanFreeMemory(pIfList);
    WlanCloseHandle(hClient, NULL);
    return 0;
}
