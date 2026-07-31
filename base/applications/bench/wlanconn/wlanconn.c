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

typedef struct _WLANCONN_SCAN_WAIT
{
    HANDLE Event;
    DWORD Code;
} WLANCONN_SCAN_WAIT, *PWLANCONN_SCAN_WAIT;

static VOID WINAPI
WlanScanNotify(PWLAN_NOTIFICATION_DATA Data, PVOID Context)
{
    PWLANCONN_SCAN_WAIT Wait = (PWLANCONN_SCAN_WAIT)Context;

    if (Data == NULL || Wait == NULL ||
        Data->NotificationSource != WLAN_NOTIFICATION_SOURCE_ACM)
    {
        return;
    }
    if (Data->NotificationCode == wlan_notification_acm_scan_complete ||
        Data->NotificationCode == wlan_notification_acm_scan_fail)
    {
        Wait->Code = Data->NotificationCode;
        SetEvent(Wait->Event);
    }
}

static BOOL
WlanLoadKeyFile(
    _In_ PCWSTR Path,
    _Out_writes_(KeyChars) PWCHAR Key,
    _In_ DWORD KeyChars)
{
    HANDLE File;
    CHAR Bytes[256];
    DWORD Size;
    DWORD Read = 0;
    INT Chars;

    RtlZeroMemory(Bytes, sizeof(Bytes));
    Key[0] = L'\0';
    File = CreateFileW(Path, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (File == INVALID_HANDLE_VALUE)
        return FALSE;

    Size = GetFileSize(File, NULL);
    if (Size == INVALID_FILE_SIZE || Size == 0 || Size >= sizeof(Bytes) ||
        !ReadFile(File, Bytes, Size, &Read, NULL) || Read != Size)
    {
        CloseHandle(File);
        SecureZeroMemory(Bytes, sizeof(Bytes));
        return FALSE;
    }
    CloseHandle(File);

    while (Size != 0 && (Bytes[Size - 1] == '\r' || Bytes[Size - 1] == '\n'))
        Size--;
    if (Size == 0 || memchr(Bytes, '\0', Size) != NULL)
    {
        SecureZeroMemory(Bytes, sizeof(Bytes));
        return FALSE;
    }

    Chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                Bytes, Size, Key, KeyChars - 1);
    SecureZeroMemory(Bytes, sizeof(Bytes));
    if (Chars <= 0 || (DWORD)Chars >= KeyChars)
        return FALSE;
    Key[Chars] = L'\0';
    return TRUE;
}

static BOOL
WlanValidWpa2Key(PCWSTR Key)
{
    SIZE_T Length = wcslen(Key);
    SIZE_T Index;

    if (Length < 8 || Length > 64)
        return FALSE;
    if (Length != 64)
        return TRUE;
    for (Index = 0; Index < Length; Index++)
    {
        WCHAR Character = Key[Index];

        if (!((Character >= L'0' && Character <= L'9') ||
              (Character >= L'a' && Character <= L'f') ||
              (Character >= L'A' && Character <= L'F')))
        {
            return FALSE;
        }
    }
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
    WCHAR Key[128];
    WCHAR SsidXml[512];
    WCHAR KeyXml[512];
    WCHAR ProfileXml[2048];
    CHAR SsidBytes[DOT11_SSID_MAX_LENGTH + 1];
    INT SsidByteLength;
    INT ProfileLength;
    DWORD Result;
    DWORD Reason = 0;
    DWORD PreviousSource = 0;
    DWORD DataSize;
    WLAN_OPCODE_VALUE_TYPE ValueType;
    PWLAN_CONNECTION_ATTRIBUTES Attributes = NULL;
    WLANCONN_SCAN_WAIT ScanWait;
    BOOL ProfileSet = FALSE;
    BOOL Connected = FALSE;
    ULONG Attempt;

    RtlZeroMemory(Key, sizeof(Key));
    RtlZeroMemory(KeyXml, sizeof(KeyXml));
    RtlZeroMemory(ProfileXml, sizeof(ProfileXml));

    if (argc != 3 && argc != 4)
    {
        wprintf(L"usage: wlanconn <ssid> <wpa2-passphrase>\n"
                L"       wlanconn <ssid> --key-file <path>\n");
        return 1;
    }

    Ssid = argv[1];
    if (argc == 4)
    {
        if (_wcsicmp(argv[2], L"--key-file") != 0 ||
            !WlanLoadKeyFile(argv[3], Key, ARRAYSIZE(Key)))
        {
            wprintf(L"WLANCONN: cannot read key file\n");
            goto Cleanup;
        }
    }
    else
    {
        SIZE_T KeyLength = wcslen(argv[2]);

        if (KeyLength >= ARRAYSIZE(Key))
        {
            wprintf(L"WLANCONN: key is too long\n");
            goto Cleanup;
        }
        wcscpy(Key, argv[2]);
        SecureZeroMemory(argv[2], KeyLength * sizeof(WCHAR));
    }
    if (!WlanValidWpa2Key(Key))
    {
        wprintf(L"WLANCONN: invalid WPA2 key length or format\n");
        goto Cleanup;
    }

    SsidByteLength = WideCharToMultiByte(CP_UTF8, 0, Ssid, -1,
                                         SsidBytes, sizeof(SsidBytes),
                                         NULL, NULL);
    if (SsidByteLength <= 1 || SsidByteLength - 1 > DOT11_SSID_MAX_LENGTH)
    {
        wprintf(L"WLANCONN: invalid SSID\n");
        goto Cleanup;
    }
    SsidByteLength--;

    if (!WlanXmlEscape(SsidXml, ARRAYSIZE(SsidXml), Ssid) ||
        !WlanXmlEscape(KeyXml, ARRAYSIZE(KeyXml), Key))
    {
        wprintf(L"WLANCONN: profile value is too long\n");
        goto Cleanup;
    }

    ProfileLength = _snwprintf(ProfileXml, ARRAYSIZE(ProfileXml) - 1, L"<?xml version=\"1.0\"?>\r\n" L"<WLANProfile xmlns=\"http://www.microsoft.com/networking/WLAN/profile/v1\">\r\n" L"<name>%s</name>\r\n" L"<SSIDConfig><SSID><name>%s</name></SSID></SSIDConfig>\r\n" L"<connectionType>ESS</connectionType>\r\n" L"<connectionMode>manual</connectionMode>\r\n" L"<MSM><security>\r\n" L"<authEncryption><authentication>WPA2PSK</authentication>" L"<encryption>AES</encryption><useOneX>false</useOneX></authEncryption>\r\n" L"<sharedKey><keyType>passPhrase</keyType><protected>false</protected>" L"<keyMaterial>%s</keyMaterial></sharedKey>\r\n" L"</security></MSM>\r\n" L"</WLANProfile>\r\n", SsidXml, SsidXml, KeyXml);
    ProfileXml[ARRAYSIZE(ProfileXml) - 1] = L'\0';
    if (ProfileLength < 0 || ProfileLength >= ARRAYSIZE(ProfileXml))
    {
        wprintf(L"WLANCONN: profile XML is too long\n");
        goto Cleanup;
    }

    Result = WlanOpenHandle(2, NULL, &dwVersion, &hClient);
    if (Result != ERROR_SUCCESS)
    {
        wprintf(L"WLANCONN: WlanOpenHandle failed %lu\n", Result);
        goto Cleanup;
    }

    Result = WlanEnumInterfaces(hClient, NULL, &pIfList);
    if (Result != ERROR_SUCCESS || pIfList == NULL || pIfList->dwNumberOfItems == 0)
    {
        wprintf(L"WLANCONN: no WLAN interface (%lu)\n", Result);
        goto Cleanup;
    }

    RtlZeroMemory(&ScanWait, sizeof(ScanWait));
    ScanWait.Event = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (ScanWait.Event == NULL)
    {
        wprintf(L"WLANCONN: cannot create scan event\n");
        goto Cleanup;
    }
    Result = WlanRegisterNotification(hClient, WLAN_NOTIFICATION_SOURCE_ACM,
                                      FALSE, WlanScanNotify, &ScanWait,
                                      NULL, &PreviousSource);
    if (Result != ERROR_SUCCESS)
    {
        wprintf(L"WLANCONN: scan notification registration failed %lu\n", Result);
        CloseHandle(ScanWait.Event);
        goto Cleanup;
    }
    Result = WlanScan(hClient, &pIfList->InterfaceInfo[0].InterfaceGuid,
                      NULL, NULL, NULL);
    if (Result != ERROR_SUCCESS ||
        WaitForSingleObject(ScanWait.Event, 15000) != WAIT_OBJECT_0 ||
        ScanWait.Code != wlan_notification_acm_scan_complete)
    {
        wprintf(L"WLANCONN: scan did not complete (%lu)\n", Result);
        WlanRegisterNotification(hClient, WLAN_NOTIFICATION_SOURCE_NONE,
                                 FALSE, NULL, NULL, NULL, &PreviousSource);
        CloseHandle(ScanWait.Event);
        goto Cleanup;
    }
    WlanRegisterNotification(hClient, WLAN_NOTIFICATION_SOURCE_NONE,
                             FALSE, NULL, NULL, NULL, &PreviousSource);
    CloseHandle(ScanWait.Event);

    Result = WlanSetProfile(hClient, &pIfList->InterfaceInfo[0].InterfaceGuid, 0, ProfileXml, NULL, TRUE, NULL, &Reason);
    wprintf(L"WLANCONN: WlanSetProfile 0x%lx (reason %lu)\n", Result, Reason);
    SecureZeroMemory(Key, sizeof(Key));
    SecureZeroMemory(KeyXml, sizeof(KeyXml));
    SecureZeroMemory(ProfileXml, sizeof(ProfileXml));
    if (Result != ERROR_SUCCESS)
        goto Cleanup;
    ProfileSet = TRUE;

    ZeroMemory(&Params, sizeof(Params));
    Params.wlanConnectionMode = wlan_connection_mode_profile;
    Params.strProfile = Ssid;
    Params.dot11BssType = dot11_BSS_type_infrastructure;

    Result = WlanConnect(hClient, &pIfList->InterfaceInfo[0].InterfaceGuid, &Params, NULL);
    wprintf(L"WLANCONN: WlanConnect 0x%lx\n", Result);
    if (Result != ERROR_SUCCESS)
        goto Cleanup;

    for (Attempt = 0; Attempt < 120; Attempt++)
    {
        DataSize = 0;
        Attributes = NULL;
        Result = WlanQueryInterface(hClient,
                                    &pIfList->InterfaceInfo[0].InterfaceGuid,
                                    wlan_intf_opcode_current_connection,
                                    NULL,
                                    &DataSize,
                                    (PVOID *)&Attributes,
                                    &ValueType);
        if (Result == ERROR_SUCCESS && Attributes != NULL)
        {
            PDOT11_SSID ConnectedSsid =
                &Attributes->wlanAssociationAttributes.dot11Ssid;

            if (Attributes->isState == wlan_interface_state_connected &&
                ConnectedSsid->uSSIDLength == (ULONG)SsidByteLength &&
                memcmp(ConnectedSsid->ucSSID, SsidBytes, SsidByteLength) == 0)
            {
                PDOT11_MAC_ADDRESS Bssid =
                    &Attributes->wlanAssociationAttributes.dot11Bssid;

                wprintf(L"WLANCONN: connected BSSID "
                        L"%02x:%02x:%02x:%02x:%02x:%02x signal %lu%% "
                        L"RX %lu.%03lu Mbit/s TX %lu.%03lu Mbit/s\n",
                        (*Bssid)[0], (*Bssid)[1], (*Bssid)[2],
                        (*Bssid)[3], (*Bssid)[4], (*Bssid)[5],
                        Attributes->wlanAssociationAttributes.wlanSignalQuality,
                        Attributes->wlanAssociationAttributes.ulRxRate / 1000,
                        Attributes->wlanAssociationAttributes.ulRxRate % 1000,
                        Attributes->wlanAssociationAttributes.ulTxRate / 1000,
                        Attributes->wlanAssociationAttributes.ulTxRate % 1000);
                Connected = TRUE;
            }
            WlanFreeMemory(Attributes);
            Attributes = NULL;
            if (Connected)
                break;
        }
        Sleep(250);
    }
    if (!Connected)
        wprintf(L"WLANCONN: connection was not authorized within 30 seconds\n");

Cleanup:
    if (Attributes != NULL)
        WlanFreeMemory(Attributes);
    if (ProfileSet && hClient != NULL && pIfList != NULL &&
        pIfList->dwNumberOfItems != 0)
    {
        WlanDeleteProfile(hClient,
                          &pIfList->InterfaceInfo[0].InterfaceGuid,
                          Ssid,
                          NULL);
    }
    SecureZeroMemory(Key, sizeof(Key));
    SecureZeroMemory(KeyXml, sizeof(KeyXml));
    SecureZeroMemory(ProfileXml, sizeof(ProfileXml));
    if (pIfList != NULL)
        WlanFreeMemory(pIfList);
    if (hClient != NULL)
        WlanCloseHandle(hClient, NULL);
    return Connected ? 0 : 1;
}
