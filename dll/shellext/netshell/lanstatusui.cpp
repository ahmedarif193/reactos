/*
 * PROJECT:     ReactOS Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     CLanStatus: Lan connection status dialog
 * COPYRIGHT:   Copyright 2008 Johannes Anderwald (johannes.anderwald@reactos.org)
 */

#include "precomp.h"


#include <wlanapi.h>

typedef DWORD (WINAPI *PFN_NS_WLANOPENHANDLE)(DWORD, PVOID, PDWORD, PHANDLE);
typedef DWORD (WINAPI *PFN_NS_WLANCLOSEHANDLE)(HANDLE, PVOID);
typedef DWORD (WINAPI *PFN_NS_WLANENUMINTERFACES)(HANDLE, PVOID, PWLAN_INTERFACE_INFO_LIST *);
typedef DWORD (WINAPI *PFN_NS_WLANQUERYINTERFACE)(HANDLE, const GUID *, WLAN_INTF_OPCODE, PVOID, PDWORD, PVOID *, WLAN_OPCODE_VALUE_TYPE *);
typedef VOID (WINAPI *PFN_NS_WLANFREEMEMORY)(PVOID);

static HMODULE g_hWlanApi = NULL;
static PFN_NS_WLANOPENHANDLE g_pfnWlanOpen;
static PFN_NS_WLANCLOSEHANDLE g_pfnWlanClose;
static PFN_NS_WLANENUMINTERFACES g_pfnWlanEnum;
static PFN_NS_WLANQUERYINTERFACE g_pfnWlanQuery;
static PFN_NS_WLANFREEMEMORY g_pfnWlanFree;

static int
NetShellWifiQuality(const GUID *pConnGuid)
{
    HANDLE hWlan = NULL;
    DWORD dwVersion = 0;
    PWLAN_INTERFACE_INFO_LIST pList = NULL;
    int nQuality = -1;

    if (!g_hWlanApi)
    {
        g_hWlanApi = LoadLibraryW(L"wlanapi.dll");
        if (!g_hWlanApi)
            return -1;
        g_pfnWlanOpen = (PFN_NS_WLANOPENHANDLE)GetProcAddress(g_hWlanApi, "WlanOpenHandle");
        g_pfnWlanClose = (PFN_NS_WLANCLOSEHANDLE)GetProcAddress(g_hWlanApi, "WlanCloseHandle");
        g_pfnWlanEnum = (PFN_NS_WLANENUMINTERFACES)GetProcAddress(g_hWlanApi, "WlanEnumInterfaces");
        g_pfnWlanQuery = (PFN_NS_WLANQUERYINTERFACE)GetProcAddress(g_hWlanApi, "WlanQueryInterface");
        g_pfnWlanFree = (PFN_NS_WLANFREEMEMORY)GetProcAddress(g_hWlanApi, "WlanFreeMemory");
    }
    if (!g_pfnWlanOpen || !g_pfnWlanClose || !g_pfnWlanEnum || !g_pfnWlanQuery || !g_pfnWlanFree)
        return -1;
    if (g_pfnWlanOpen(2, NULL, &dwVersion, &hWlan) != ERROR_SUCCESS || !hWlan)
        return -1;
    if (g_pfnWlanEnum(hWlan, NULL, &pList) == ERROR_SUCCESS && pList)
    {
        for (DWORD i = 0; i < pList->dwNumberOfItems; i++)
        {
            WLAN_INTERFACE_INFO *pInfo = &pList->InterfaceInfo[i];
            DWORD cb = 0;
            PVOID pData = NULL;
            WLAN_OPCODE_VALUE_TYPE vt;
            if (pConnGuid && !IsEqualGUID(pInfo->InterfaceGuid, *pConnGuid) && pList->dwNumberOfItems > 1)
                continue;
            if (pInfo->isState != wlan_interface_state_connected)
                continue;
            if (g_pfnWlanQuery(hWlan, &pInfo->InterfaceGuid, wlan_intf_opcode_current_connection,
                               NULL, &cb, &pData, &vt) == ERROR_SUCCESS && pData)
            {
                WLAN_CONNECTION_ATTRIBUTES *pAttr = (WLAN_CONNECTION_ATTRIBUTES *)pData;
                nQuality = (int)pAttr->wlanAssociationAttributes.wlanSignalQuality;
                g_pfnWlanFree(pData);
                break;
            }
        }
        g_pfnWlanFree(pList);
    }
    g_pfnWlanClose(hWlan, NULL);
    return nQuality;
}

#define NETTIMERID 0xFABC

CLanStatus::CLanStatus() :
    m_lpNetMan(NULL),
    m_pHead(NULL),
    m_hwndRetry(NULL)
{
}

LRESULT CALLBACK
CLanStatus::RetryWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    CLanStatus *pThis;

    if (uMsg == WM_NCCREATE)
    {
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)((CREATESTRUCTW *)lParam)->lpCreateParams);
        return TRUE;
    }
    pThis = (CLanStatus *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    if (pThis && (uMsg == WM_TIMER || (uMsg == WM_DEVICECHANGE && wParam == 0x0007)))
    {
        pThis->InitializeNetTaskbarNotifications();
        return 0;
    }
    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}

VOID
CLanStatus::StartRetry()
{
    WNDCLASSW wc;

    if (m_hwndRetry)
        return;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = RetryWndProc;
    wc.hInstance = netshell_hInstance;
    wc.lpszClassName = L"NetshellLanStatusRetry";
    RegisterClassW(&wc);
    m_hwndRetry = CreateWindowExW(0, wc.lpszClassName, NULL, WS_OVERLAPPED, 0, 0, 0, 0,
                                  NULL, NULL, netshell_hInstance, this);
    if (m_hwndRetry)
        SetTimer(m_hwndRetry, 1, 5000, NULL);
}

VOID
CLanStatus::StopRetry()
{
    if (!m_hwndRetry)
        return;
    KillTimer(m_hwndRetry, 1);
    DestroyWindow(m_hwndRetry);
    m_hwndRetry = NULL;
}

HRESULT
CLanStatus::InitializeNetTaskbarNotifications()
{
    HRESULT hr = S_OK;

    if (!m_pHead)
        hr = EnumerateTrayConnections();
    if (!m_pHead)
    {
        TRACE("LanStatus: no network connections enumerated (hr %08x), retrying later\n", hr);
        StartRetry();
        return S_OK;
    }
    StopRetry();
    return S_OK;
}

VOID
UpdateLanStatusUiDlg(
    HWND hwndDlg,
    MIB_IFROW *IfEntry,
    LANSTATUSUI_CONTEXT *pContext)
{
    WCHAR szFormat[MAX_PATH] = {0};
    WCHAR szBuffer[MAX_PATH] = {0};
    SYSTEMTIME TimeConnected;
    DWORD DurationSeconds;
    WCHAR Buffer[100];
    WCHAR DayBuffer[30];
    WCHAR LocBuffer[50];

#if 0
    ULONGLONG Ticks;
#else
    DWORD Ticks;
#endif

    if (IfEntry->dwSpeed < 1000)
    {
        if (LoadStringW(netshell_hInstance, IDS_FORMAT_BIT, szFormat, sizeof(szFormat)/sizeof(WCHAR)))
        {
            _swprintf(szBuffer, szFormat, IfEntry->dwSpeed);
            SendDlgItemMessageW(hwndDlg, IDC_SPEED, WM_SETTEXT, 0, (LPARAM)szBuffer);
        }
    }
    else if (IfEntry->dwSpeed < 1000000)
    {
        if (LoadStringW(netshell_hInstance, IDS_FORMAT_KBIT, szFormat, sizeof(szFormat)/sizeof(WCHAR)))
        {
            _swprintf(szBuffer, szFormat, IfEntry->dwSpeed/1000);
            SendDlgItemMessageW(hwndDlg, IDC_SPEED, WM_SETTEXT, 0, (LPARAM)szBuffer);
        }
    }
    else if (IfEntry->dwSpeed < 1000000000)
    {
        if (LoadStringW(netshell_hInstance, IDS_FORMAT_MBIT, szFormat, sizeof(szFormat)/sizeof(WCHAR)))
        {
            _swprintf(szBuffer, szFormat, IfEntry->dwSpeed/1000000);
            SendDlgItemMessageW(hwndDlg, IDC_SPEED, WM_SETTEXT, 0, (LPARAM)szBuffer);
        }
    }
    else
    {
        if (LoadStringW(netshell_hInstance, IDS_FORMAT_GBIT, szFormat, sizeof(szFormat)/sizeof(WCHAR)))
        {
            _swprintf(szBuffer, szFormat, IfEntry->dwSpeed/1000000000);
            SendDlgItemMessageW(hwndDlg, IDC_SPEED, WM_SETTEXT, 0, (LPARAM)szBuffer);
        }
    }

    if (StrFormatByteSizeW(IfEntry->dwInOctets, szBuffer, _countof(szBuffer)))
    {
        SendDlgItemMessageW(hwndDlg, IDC_RECEIVED, WM_SETTEXT, 0, (LPARAM)szBuffer);
    }

    if (StrFormatByteSizeW(IfEntry->dwOutOctets, szBuffer, _countof(szBuffer)))
    {
        SendDlgItemMessageW(hwndDlg, IDC_SEND, WM_SETTEXT, 0, (LPARAM)szBuffer);
    }

#if 0
    Ticks = GetTickCount64();
#else
    Ticks = GetTickCount();
#endif

    DurationSeconds = Ticks / 1000;
    TimeConnected.wSecond = (DurationSeconds % 60);
    TimeConnected.wMinute = (DurationSeconds / 60) % 60;
    TimeConnected.wHour = (DurationSeconds / (60 * 60)) % 24;
    TimeConnected.wDay = DurationSeconds / (60 * 60 * 24);

    if (!GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &TimeConnected, L"HH':'mm':'ss", LocBuffer, sizeof(LocBuffer) / sizeof(LocBuffer[0])))
        return;

    if (!TimeConnected.wDay)
    {
        SendDlgItemMessageW(hwndDlg, IDC_DURATION, WM_SETTEXT, 0, (LPARAM)LocBuffer);
    }
    else
    {
        if (TimeConnected.wDay == 1)
        {
            if (!LoadStringW(netshell_hInstance, IDS_DURATION_DAY, DayBuffer, sizeof(DayBuffer) / sizeof(DayBuffer[0])))
                DayBuffer[0] = L'\0';
        }
        else
        {
            if (!LoadStringW(netshell_hInstance, IDS_DURATION_DAYS, DayBuffer, sizeof(DayBuffer) / sizeof(DayBuffer[0])))
                DayBuffer[0] = L'\0';
        }
        _swprintf(Buffer, DayBuffer, TimeConnected.wDay, LocBuffer);
        SendDlgItemMessageW(hwndDlg, IDC_DURATION, WM_SETTEXT, 0, (LPARAM)Buffer);
    }

}

VOID
UpdateLanStatus(HWND hwndDlg, LANSTATUSUI_CONTEXT * pContext)
{
    MIB_IFROW IfEntry;
    HICON hIcon, hOldIcon = NULL;
    NOTIFYICONDATAW nid;
    NETCON_PROPERTIES * pProperties = NULL;

    ZeroMemory(&IfEntry, sizeof(IfEntry));
    IfEntry.dwIndex = pContext->dwAdapterIndex;
    if (GetIfEntry(&IfEntry) != NO_ERROR)
    {
        return;
    }

    if (pContext->Status == (UINT)-1)
    {
        /*
         * On first execution, pContext->dw[In|Out]Octets will be zero while
         * the interface info is already refreshed with non-null data, so a
         * gap is normal and does not correspond to an effective TX or RX packet.
         */
        pContext->dwInOctets = IfEntry.dwInOctets;
        pContext->dwOutOctets = IfEntry.dwOutOctets;
    }

    hIcon = NULL;
    if (IfEntry.dwType == IF_TYPE_IEEE80211)
    {
        BOOL bUp = (IfEntry.dwOperStatus == MIB_IF_OPER_STATUS_CONNECTED ||
                    IfEntry.dwOperStatus == MIB_IF_OPER_STATUS_OPERATIONAL);
        UINT nWanted = 10;
        UINT nIcon = IDI_NET_TRAY_WIFIOFF;
        if (bUp)
        {
            NETCON_PROPERTIES *pProps = NULL;
            GUID guid;
            const GUID *pGuid = NULL;
            int nQuality;
            if (pContext->pNet->GetProperties(&pProps) == S_OK && pProps)
            {
                guid = pProps->guidId;
                pGuid = &guid;
                NcFreeNetconProperties(pProps);
            }
            nQuality = NetShellWifiQuality(pGuid);
            if (nQuality < 0 || nQuality >= 75)
            {
                nWanted = 6;
                nIcon = IDI_NET_TRAY_WIFI1;
            }
            else if (nQuality >= 50)
            {
                nWanted = 7;
                nIcon = IDI_NET_TRAY_WIFI2;
            }
            else if (nQuality >= 25)
            {
                nWanted = 8;
                nIcon = IDI_NET_TRAY_WIFI3;
            }
            else
            {
                nWanted = 9;
                nIcon = IDI_NET_TRAY_WIFI4;
            }
        }
        if (pContext->Status != nWanted)
        {
            hIcon = (HICON)LoadImage(netshell_hInstance, MAKEINTRESOURCE(nIcon), IMAGE_ICON, 32, 32, LR_SHARED);
            pContext->Status = nWanted;
        }
    }
    else if (IfEntry.dwOperStatus == MIB_IF_OPER_STATUS_CONNECTED || IfEntry.dwOperStatus == MIB_IF_OPER_STATUS_OPERATIONAL)
    {
        if (pContext->dwInOctets == IfEntry.dwInOctets && pContext->dwOutOctets == IfEntry.dwOutOctets && pContext->Status  != 0)
        {
            hIcon = (HICON)LoadImage(netshell_hInstance, MAKEINTRESOURCE(IDI_NET_TRAY_WIRED), IMAGE_ICON, 32, 32, LR_SHARED);
            pContext->Status = 0;
        }
        else if (pContext->dwInOctets != IfEntry.dwInOctets && pContext->dwOutOctets != IfEntry.dwOutOctets && pContext->Status  != 1)
        {
            hIcon = (HICON)LoadImage(netshell_hInstance, MAKEINTRESOURCE(IDI_NET_TRAY_WIRED), IMAGE_ICON, 32, 32, LR_SHARED);
            pContext->Status = 1;
        }
        else if (pContext->dwInOctets != IfEntry.dwInOctets && pContext->Status  != 2)
        {
            hIcon = (HICON)LoadImage(netshell_hInstance, MAKEINTRESOURCE(IDI_NET_TRAY_WIRED), IMAGE_ICON, 32, 32, LR_SHARED);
            pContext->Status = 2;
        }
        else if (pContext->dwOutOctets != IfEntry.dwOutOctets && pContext->Status  != 3)
        {
            hIcon = (HICON)LoadImage(netshell_hInstance, MAKEINTRESOURCE(IDI_NET_TRAY_WIRED), IMAGE_ICON, 32, 32, LR_SHARED);
            pContext->Status = 3;
        }
    }
    else if (IfEntry.dwOperStatus == MIB_IF_OPER_STATUS_UNREACHABLE || IfEntry.dwOperStatus == MIB_IF_OPER_STATUS_DISCONNECTED)
    {
        if (pContext->Status != 4)
        {
            hIcon = (HICON)LoadImage(netshell_hInstance, MAKEINTRESOURCE(IDI_NET_TRAY_OFF), IMAGE_ICON, 32, 32, LR_SHARED);
            pContext->Status = 4;
        }
    }
    else if (IfEntry.dwOperStatus == MIB_IF_OPER_STATUS_NON_OPERATIONAL)
    {
        if (pContext->Status != 5)
        {
            hIcon = (HICON)LoadImage(netshell_hInstance, MAKEINTRESOURCE(IDI_NET_TRAY_OFF), IMAGE_ICON, 32, 32, LR_SHARED);
            pContext->Status = 5;
        }
    }

    if (hwndDlg && hIcon)
    {
        hOldIcon = (HICON)SendDlgItemMessageW(hwndDlg, IDC_NETSTAT, STM_SETICON, (WPARAM)hIcon, 0);
        if (hOldIcon)
            DestroyIcon(hOldIcon);
    }

    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.uID = pContext->uID;
    nid.hWnd = pContext->hwndStatusDlg;
    nid.uVersion = NOTIFYICON_VERSION;

    if (pContext->pNet->GetProperties(&pProperties) == S_OK)
    {
        if (pProperties->dwCharacter & NCCF_SHOW_ICON)
        {
            nid.hIcon = (HICON)CopyImage(hIcon, IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON),
                                         LR_COPYFROMRESOURCE);

            if (nid.hIcon)
                nid.uFlags |= NIF_ICON;

            nid.uFlags |= NIF_STATE;
            nid.dwState = 0;
            nid.dwStateMask = NIS_HIDDEN;

            if (pProperties->pszwName)
            {
                if (wcslen(pProperties->pszwName) * sizeof(WCHAR) < sizeof(nid.szTip))
                {
                    nid.uFlags |= NIF_TIP;
                    wcscpy(nid.szTip, pProperties->pszwName);
                }
                else
                {
                    CopyMemory(nid.szTip, pProperties->pszwName, sizeof(nid.szTip) - sizeof(WCHAR));
                    nid.szTip[(sizeof(nid.szTip)/sizeof(WCHAR))-1] = L'\0';
                    nid.uFlags |= NIF_TIP;
                }
            }
        }
        else
        {
            nid.uFlags |= NIF_STATE;
            nid.dwState = NIS_HIDDEN;
            nid.dwStateMask = NIS_HIDDEN;

        }
        NcFreeNetconProperties(pProperties);
    }

    Shell_NotifyIconW(NIM_MODIFY, &nid);

    if (nid.uFlags & NIF_ICON)
        DestroyIcon(nid.hIcon);

    pContext->dwInOctets = IfEntry.dwInOctets;
    pContext->dwOutOctets = IfEntry.dwOutOctets;

    if (hwndDlg)
        UpdateLanStatusUiDlg(hwndDlg, &IfEntry, pContext);
}


VOID
InitializeLANStatusUiDlg(HWND hwndDlg, LANSTATUSUI_CONTEXT * pContext)
{
    WCHAR szBuffer[MAX_PATH] = {0};
    NETCON_PROPERTIES * pProperties;

    if (pContext->pNet->GetProperties(&pProperties) != S_OK)
        return;

    if (pProperties->Status == NCS_DISCONNECTED)
        LoadStringW(netshell_hInstance, IDS_STATUS_UNREACHABLE, szBuffer, MAX_PATH);
    else if (pProperties->Status == NCS_MEDIA_DISCONNECTED)
        LoadStringW(netshell_hInstance, IDS_STATUS_DISCONNECTED, szBuffer, MAX_PATH);
    else if (pProperties->Status == NCS_CONNECTING)
        LoadStringW(netshell_hInstance, IDS_STATUS_CONNECTING, szBuffer, MAX_PATH);
    else if (pProperties->Status == NCS_CONNECTED)
         LoadStringW(netshell_hInstance, IDS_STATUS_CONNECTED, szBuffer, MAX_PATH);

    SendDlgItemMessageW(hwndDlg, IDC_STATUS, WM_SETTEXT, 0, (LPARAM)szBuffer);

    pContext->dwInOctets = 0;
    pContext->dwOutOctets = 0;

    /* update adapter info */
    pContext->Status = -1;
    UpdateLanStatus(hwndDlg, pContext);
    NcFreeNetconProperties(pProperties);
}

static
VOID
InsertColumnToListView(
    HWND hDlgCtrl,
    UINT ResId,
    UINT SubItem,
    UINT Size)
{
    WCHAR szBuffer[200];
    LVCOLUMNW lc;

    if (!LoadStringW(netshell_hInstance, ResId, szBuffer, sizeof(szBuffer)/sizeof(WCHAR)))
        return;

    memset(&lc, 0, sizeof(LV_COLUMN) );
    lc.mask = LVCF_WIDTH | LVCF_TEXT | LVCF_SUBITEM | LVCF_FMT;
    lc.iSubItem   = SubItem;
    lc.fmt = LVCFMT_FIXED_WIDTH;
    lc.cx         = Size;
    lc.cchTextMax = wcslen(szBuffer);
    lc.pszText    = szBuffer;

    (void)SendMessageW(hDlgCtrl, LVM_INSERTCOLUMNW, SubItem, (LPARAM)&lc);
}

static
VOID
AddIPAddressToListView(
    HWND hDlgCtrl,
    PIP_ADDR_STRING pAddr,
    INT Index)
{
    LVITEMW li;
    PIP_ADDR_STRING pCur;
    WCHAR szBuffer[100];
    UINT SubIndex;

    ZeroMemory(&li, sizeof(LVITEMW));
    li.mask = LVIF_TEXT;
    li.iItem = Index;
    pCur = pAddr;
    SubIndex = 0;

    do
    {
        if (SubIndex)
        {
            ZeroMemory(&li, sizeof(LVITEMW));
            li.mask = LVIF_TEXT;
            li.iItem = Index;
            li.iSubItem = 0;
            li.pszText = (LPWSTR)L"";
            li.iItem = SendMessageW(hDlgCtrl, LVM_INSERTITEMW, 0, (LPARAM)&li);
        }

        if (MultiByteToWideChar(CP_ACP, 0, pCur->IpAddress.String, -1, szBuffer, sizeof(szBuffer)/sizeof(WCHAR)))
        {
            li.pszText = szBuffer;
            li.iSubItem = 1;
            li.iItem = Index++;
            SendMessageW(hDlgCtrl, LVM_SETITEMW, 0, (LPARAM)&li);
        }
        SubIndex++;
        pCur = pCur->Next;
    } while (pCur && pCur->IpAddress.String[0]);
}

static
INT
InsertItemToListView(
    HWND hDlgCtrl,
    UINT ResId)
{
    LVITEMW li;
    WCHAR szBuffer[100];

    ZeroMemory(&li, sizeof(LVITEMW));
    li.mask = LVIF_TEXT;
    li.iItem = ListView_GetItemCount(hDlgCtrl);
    if (LoadStringW(netshell_hInstance, ResId, szBuffer, sizeof(szBuffer)/sizeof(WCHAR)))
    {
        li.pszText = szBuffer;
        return (INT)SendMessageW(hDlgCtrl, LVM_INSERTITEMW, 0, (LPARAM)&li);
    }
    return -1;
}

static
BOOL
tmToStr(
    IN struct tm *pTM,
    OUT LPWSTR szBuffer,
    IN UINT nBufferSize)
{
    SYSTEMTIME st;
    CString strBufferDate;
    CString strBufferTime;
    UINT nCharDate, nCharTime;
    BOOL bResult = FALSE;

    st.wYear = pTM->tm_year + 1900;
    st.wMonth = pTM->tm_mon + 1;
    st.wDay = pTM->tm_mday;
    st.wHour = pTM->tm_hour;
    st.wMinute = pTM->tm_min;
    st.wSecond = pTM->tm_sec;

    /* Check required size before cpy/cat */
    nCharDate = GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, NULL, 0) + 1;
    nCharTime = GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, NULL, 0) + 1;

    if (GetDateFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, strBufferDate.GetBuffer(nCharDate), nCharDate) &&
        GetTimeFormatW(LOCALE_USER_DEFAULT, 0, &st, NULL, strBufferTime.GetBuffer(nCharTime), nCharTime))
    {
        StringCbCopy(szBuffer, nBufferSize, strBufferDate);
        StringCbCat(szBuffer, nBufferSize, L" ");
        StringCbCat(szBuffer, nBufferSize, strBufferTime);
        bResult = TRUE;
    }
    strBufferDate.ReleaseBuffer();
    strBufferTime.ReleaseBuffer();

    return bResult;
}

INT_PTR
CALLBACK
LANStatusUiDetailsDlg(
    HWND hwndDlg,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam
)
{
    LANSTATUSUI_CONTEXT * pContext;
    LVITEMW li;
    WCHAR szBuffer[100];
    PIP_ADAPTER_INFO pAdapterInfo, pCurAdapter;
    PIP_PER_ADAPTER_INFO pPerAdapter;
    DWORD dwSize;
    HWND hDlgCtrl;
    RECT rect;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pContext = (LANSTATUSUI_CONTEXT*)lParam;

            hDlgCtrl = GetDlgItem(hwndDlg, IDC_DETAILS);

            /* get client rect */
            GetClientRect(hDlgCtrl, &rect);

            /* calculate column width */
            dwSize = rect.right / 2;

            InsertColumnToListView(hDlgCtrl, IDS_PROPERTY, 0, dwSize);
            InsertColumnToListView(hDlgCtrl, IDS_VALUE, 1, dwSize);

            dwSize = 0;
            pCurAdapter = NULL;
            pAdapterInfo = NULL;
            if (GetAdaptersInfo(NULL, &dwSize) == ERROR_BUFFER_OVERFLOW)
            {
                pAdapterInfo = static_cast<PIP_ADAPTER_INFO>(CoTaskMemAlloc(dwSize));
                if (pAdapterInfo)
                {
                    if (GetAdaptersInfo(pAdapterInfo, &dwSize) == NO_ERROR)
                    {
                        pCurAdapter = pAdapterInfo;
                        while (pCurAdapter && pCurAdapter->Index != pContext->dwAdapterIndex)
                            pCurAdapter = pCurAdapter->Next;

                        if (pCurAdapter->Index != pContext->dwAdapterIndex)
                            pCurAdapter = NULL;
                    }
                }
            }

            ZeroMemory(&li, sizeof(LVITEMW));
            li.mask = LVIF_TEXT;
            li.iSubItem = 1;
            li.pszText = szBuffer;

            if (pCurAdapter)
            {
                li.iItem = InsertItemToListView(hDlgCtrl, IDS_PHYSICAL_ADDRESS);
                if (li.iItem >= 0)
                {
                    _swprintf(szBuffer, L"%02x-%02x-%02x-%02x-%02x-%02x",pCurAdapter->Address[0], pCurAdapter->Address[1],
                              pCurAdapter->Address[2], pCurAdapter->Address[3], pCurAdapter->Address[4], pCurAdapter->Address[5]);
                    SendMessageW(hDlgCtrl, LVM_SETITEMW, 0, (LPARAM)&li);
                }
                li.iItem = InsertItemToListView(hDlgCtrl, IDS_IP_ADDRESS);
                if (li.iItem >= 0)
                    if (MultiByteToWideChar(CP_ACP, 0, pCurAdapter->IpAddressList.IpAddress.String, -1, szBuffer, sizeof(szBuffer)/sizeof(WCHAR)))
                        SendMessageW(hDlgCtrl, LVM_SETITEMW, 0, (LPARAM)&li);

                li.iItem = InsertItemToListView(hDlgCtrl, IDS_SUBNET_MASK);
                if (li.iItem >= 0)
                    if (MultiByteToWideChar(CP_ACP, 0, pCurAdapter->IpAddressList.IpMask.String, -1, szBuffer, sizeof(szBuffer)/sizeof(WCHAR)))
                        SendMessageW(hDlgCtrl, LVM_SETITEMW, 0, (LPARAM)&li);

                li.iItem = InsertItemToListView(hDlgCtrl, IDS_DEF_GATEWAY);
                if (li.iItem >= 0 && pCurAdapter->GatewayList.IpAddress.String[0] != '0')
                {
                    if (MultiByteToWideChar(CP_ACP, 0, pCurAdapter->GatewayList.IpAddress.String, -1, szBuffer, sizeof(szBuffer)/sizeof(WCHAR)))
                        SendMessageW(hDlgCtrl, LVM_SETITEMW, 0, (LPARAM)&li);
                }

                li.iItem = InsertItemToListView(hDlgCtrl, IDS_DHCP_SERVER);
                if (li.iItem >= 0 && pCurAdapter->DhcpServer.IpAddress.String[0] != '0')
                {
                    if (MultiByteToWideChar(CP_ACP, 0, pCurAdapter->DhcpServer.IpAddress.String, -1, szBuffer, sizeof(szBuffer)/sizeof(WCHAR)))
                        SendMessageW(hDlgCtrl, LVM_SETITEMW, 0, (LPARAM)&li);
                }

                li.iItem = InsertItemToListView(hDlgCtrl, IDS_LEASE_OBTAINED);
                if (li.iItem >= 0 && pCurAdapter->LeaseObtained != NULL)
                {
                    struct tm *leaseOptained;

                    leaseOptained = localtime(&pCurAdapter->LeaseObtained);

                    if (tmToStr(leaseOptained, szBuffer, _countof(szBuffer)))
                        SendMessageW(hDlgCtrl, LVM_SETITEMW, 0, (LPARAM)&li);
                }

                li.iItem = InsertItemToListView(hDlgCtrl, IDS_LEASE_EXPIRES);
                if (li.iItem >= 0 && pCurAdapter->LeaseExpires != NULL)
                {
                    struct tm *leaseExpire;

                    leaseExpire = localtime(&pCurAdapter->LeaseExpires);

                    if (tmToStr(leaseExpire, szBuffer, _countof(szBuffer)))
                        SendMessageW(hDlgCtrl, LVM_SETITEMW, 0, (LPARAM)&li);
                }
            }

            dwSize = 0;
            li.iItem = InsertItemToListView(hDlgCtrl, IDS_DNS_SERVERS);
            if (GetPerAdapterInfo(pContext->dwAdapterIndex, NULL, &dwSize) == ERROR_BUFFER_OVERFLOW)
            {
                pPerAdapter = static_cast<PIP_PER_ADAPTER_INFO>(CoTaskMemAlloc(dwSize));
                if (pPerAdapter)
                {
                    if (GetPerAdapterInfo(pContext->dwAdapterIndex, pPerAdapter, &dwSize) == ERROR_SUCCESS)
                    {
                        if (li.iItem >= 0)
                            AddIPAddressToListView(hDlgCtrl, &pPerAdapter->DnsServerList, li.iItem);
                    }
                    CoTaskMemFree(pPerAdapter);
                }
            }

            if (pCurAdapter)
            {
                li.iItem = InsertItemToListView(hDlgCtrl, IDS_WINS_SERVERS);
                if (pCurAdapter->HaveWins)
                {
                    AddIPAddressToListView(hDlgCtrl, &pCurAdapter->PrimaryWinsServer, li.iItem);
                    AddIPAddressToListView(hDlgCtrl, &pCurAdapter->SecondaryWinsServer, li.iItem+1);
                }
            }

            CoTaskMemFree(pAdapterInfo);
            break;

        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_CLOSE)
            {
                EndDialog(hwndDlg, FALSE);
                break;
            }
    }

    return FALSE;
}

INT_PTR
CALLBACK
LANStatusUiAdvancedDlg(
    HWND hwndDlg,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    WCHAR szBuffer[100] = {0};
    PROPSHEETPAGE *page;
    LANSTATUSUI_CONTEXT * pContext;
    DWORD dwIpAddr;


    switch (uMsg)
    {
        case WM_INITDIALOG:
            page = (PROPSHEETPAGE*)lParam;
            pContext = (LANSTATUSUI_CONTEXT*)page->lParam;
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)pContext);
            if (pContext->DHCPEnabled)
                LoadStringW(netshell_hInstance, IDS_ASSIGNED_DHCP, szBuffer, sizeof(szBuffer)/sizeof(WCHAR));
            else
                LoadStringW(netshell_hInstance, IDS_ASSIGNED_MANUAL, szBuffer, sizeof(szBuffer)/sizeof(WCHAR));

            szBuffer[(sizeof(szBuffer)/sizeof(WCHAR))-1] = L'\0';
            SendDlgItemMessageW(hwndDlg, IDC_DETAILSTYPE, WM_SETTEXT, 0, (LPARAM)szBuffer);


            dwIpAddr = ntohl(pContext->IpAddress);
            _swprintf(szBuffer, L"%u.%u.%u.%u", FIRST_IPADDRESS(dwIpAddr), SECOND_IPADDRESS(dwIpAddr),
                      THIRD_IPADDRESS(dwIpAddr), FOURTH_IPADDRESS(dwIpAddr));
            SendDlgItemMessageW(hwndDlg, IDC_DETAILSIP, WM_SETTEXT, 0, (LPARAM)szBuffer);

            dwIpAddr = ntohl(pContext->SubnetMask);
            _swprintf(szBuffer, L"%u.%u.%u.%u", FIRST_IPADDRESS(dwIpAddr), SECOND_IPADDRESS(dwIpAddr),
                      THIRD_IPADDRESS(dwIpAddr), FOURTH_IPADDRESS(dwIpAddr));
            SendDlgItemMessageW(hwndDlg, IDC_DETAILSSUBNET, WM_SETTEXT, 0, (LPARAM)szBuffer);

            dwIpAddr = ntohl(pContext->Gateway);
            if (dwIpAddr)
            {
                _swprintf(szBuffer, L"%u.%u.%u.%u", FIRST_IPADDRESS(dwIpAddr), SECOND_IPADDRESS(dwIpAddr),
                          THIRD_IPADDRESS(dwIpAddr), FOURTH_IPADDRESS(dwIpAddr));
                SendDlgItemMessageW(hwndDlg, IDC_DETAILSGATEWAY, WM_SETTEXT, 0, (LPARAM)szBuffer);
            }
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wParam) == IDC_DETAILS)
            {
                pContext = (LANSTATUSUI_CONTEXT*)GetWindowLongPtr(hwndDlg, DWLP_USER);
                if (pContext)
                {
                    DialogBoxParamW(netshell_hInstance, MAKEINTRESOURCEW(IDD_LAN_NETSTATUSDETAILS), GetParent(hwndDlg),
                                    LANStatusUiDetailsDlg, (LPARAM)pContext);
                }
            }
            break;
        default:
            break;
    }
    return FALSE;
}

VOID
DisableNetworkAdapter(INetConnection * pNet, LANSTATUSUI_CONTEXT * pContext, HWND hwndDlg)
{
    HRESULT hr = pNet->Disconnect();
    if (FAILED_UNEXPECTEDLY(hr))
        return;

    NOTIFYICONDATAW nid;

    PropSheet_PressButton(GetParent(hwndDlg), PSBTN_CANCEL);
    ZeroMemory(&nid, sizeof(nid));
    nid.cbSize = sizeof(nid);
    nid.uID = pContext->uID;
    nid.hWnd = pContext->hwndDlg;
    nid.uFlags = NIF_STATE;
    nid.dwState = NIS_HIDDEN;
    nid.dwStateMask = NIS_HIDDEN;

    Shell_NotifyIconW(NIM_MODIFY, &nid);
}


INT_PTR
CALLBACK
LANStatusUiDlg(
    HWND hwndDlg,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    PROPSHEETPAGE *page;
    LANSTATUSUI_CONTEXT * pContext;
    LPPSHNOTIFY lppsn;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            page = (PROPSHEETPAGE*)lParam;
            pContext = (LANSTATUSUI_CONTEXT*)page->lParam;
            pContext->hwndDlg = hwndDlg;
            InitializeLANStatusUiDlg(hwndDlg, pContext);
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)pContext);
            return TRUE;
        case WM_COMMAND:
            pContext = (LANSTATUSUI_CONTEXT*)GetWindowLongPtr(hwndDlg, DWLP_USER);
            if (LOWORD(wParam) == IDC_STATUS_PROPERTIES)
            {
                if (pContext)
                {
                    ShowNetConnectionProperties(pContext->pNet, GetParent(pContext->hwndDlg));
                    BringWindowToTop(GetParent(pContext->hwndDlg));
                }
                break;
            }
            else if (LOWORD(wParam) == IDC_ENDISABLE)
            {
                DisableNetworkAdapter(pContext->pNet, pContext, hwndDlg);
                break;
            }
        case WM_NOTIFY:
            lppsn = (LPPSHNOTIFY) lParam;
            if (lppsn->hdr.code == PSN_APPLY || lppsn->hdr.code == PSN_RESET)
            {
                pContext = (LANSTATUSUI_CONTEXT*)GetWindowLongPtr(hwndDlg, DWLP_USER);
                SetWindowLongPtr(hwndDlg, DWLP_MSGRESULT, PSNRET_NOERROR);
                pContext->hwndDlg = NULL;
                return TRUE;
            }
            break;
    }
    return FALSE;
}

VOID
InitializePropertyDialog(
    LANSTATUSUI_CONTEXT * pContext,
    NETCON_PROPERTIES * pProperties)
{
    DWORD dwSize, dwAdapterIndex, dwResult;
    LPOLESTR pStr;
    IP_ADAPTER_INFO *pAdapterInfo, *pCurAdapter;

    if (FAILED(StringFromCLSID((CLSID)pProperties->guidId, &pStr)))
    {
        return;
    }

    /* get the IfTable */
    dwSize = 0;
    dwResult = GetAdaptersInfo(NULL, &dwSize);
    if (dwResult!= ERROR_BUFFER_OVERFLOW)
    {
        CoTaskMemFree(pStr);
        return;
    }

    pAdapterInfo = static_cast<PIP_ADAPTER_INFO>(CoTaskMemAlloc(dwSize));
    if (!pAdapterInfo)
    {
        CoTaskMemFree(pAdapterInfo);
        CoTaskMemFree(pStr);
        return;
    }

    if (GetAdaptersInfo(pAdapterInfo, &dwSize) != NO_ERROR)
    {
        CoTaskMemFree(pAdapterInfo);
        CoTaskMemFree(pStr);
        return;
    }

    if (!GetAdapterIndexFromNetCfgInstanceId(pAdapterInfo, pStr, &dwAdapterIndex))
        dwAdapterIndex = pContext->dwAdapterIndex;

    pCurAdapter = pAdapterInfo;
    while (pCurAdapter && pCurAdapter->Index != dwAdapterIndex)
        pCurAdapter = pCurAdapter->Next;
    if (!pCurAdapter)
    {
        CoTaskMemFree(pAdapterInfo);
        CoTaskMemFree(pStr);
        return;
    }


    pContext->IpAddress = inet_addr(pCurAdapter->IpAddressList.IpAddress.String);
    pContext->SubnetMask = inet_addr(pCurAdapter->IpAddressList.IpMask.String);
    pContext->Gateway = inet_addr(pCurAdapter->GatewayList.IpAddress.String);
    pContext->DHCPEnabled = pCurAdapter->DhcpEnabled;
    CoTaskMemFree(pStr);
    CoTaskMemFree(pAdapterInfo);
    pContext->dwAdapterIndex = dwAdapterIndex;
}

static int CALLBACK
PropSheetProc(HWND hwndDlg, UINT uMsg, LPARAM lParam)
{
    // NOTE: This callback is needed to set large icon correctly.
    HICON hIcon;
    switch (uMsg)
    {
        case PSCB_INITIALIZED:
        {
            hIcon = LoadIconW(netshell_hInstance, MAKEINTRESOURCEW(IDI_NET_IDLE));
            SendMessageW(hwndDlg, WM_SETICON, ICON_BIG, (LPARAM)hIcon);
            break;
        }
    }
    return 0;
}

VOID
ShowStatusPropertyDialog(
    LANSTATUSUI_CONTEXT *pContext,
    HWND hwndDlg)
{
    HPROPSHEETPAGE hppages[2];
    PROPSHEETHEADERW pinfo;
    NETCON_PROPERTIES * pProperties = NULL;

    ZeroMemory(&pinfo, sizeof(PROPSHEETHEADERW));
    ZeroMemory(hppages, sizeof(hppages));
    pinfo.dwSize = sizeof(PROPSHEETHEADERW);
    pinfo.dwFlags = PSH_NOCONTEXTHELP | PSH_PROPTITLE | PSH_NOAPPLYNOW |
                    PSH_USEICONID | PSH_USECALLBACK;
    pinfo.phpage = hppages;
    pinfo.hwndParent = hwndDlg;
    pinfo.hInstance = netshell_hInstance;
    pinfo.pszIcon = MAKEINTRESOURCEW(IDI_NET_IDLE);
    pinfo.pfnCallback = PropSheetProc;

    if (pContext->pNet->GetProperties(&pProperties) == S_OK)
    {
        if (pProperties->pszwName)
        {
            pinfo.pszCaption = pProperties->pszwName;
            pinfo.dwFlags |= PSH_PROPTITLE;
        }
        InitializePropertyDialog(pContext, pProperties);
        if (pProperties->MediaType == NCM_LAN && pProperties->Status == NCS_CONNECTED)
        {
            hppages[0] = InitializePropertySheetPage(MAKEINTRESOURCEW(IDD_LAN_NETSTATUS), LANStatusUiDlg, (LPARAM)pContext, NULL);
            if (hppages[0])
               pinfo.nPages++;

            hppages[pinfo.nPages] = InitializePropertySheetPage(MAKEINTRESOURCEW(IDD_LAN_NETSTATUSADVANCED), LANStatusUiAdvancedDlg, (LPARAM)pContext, NULL);
            if (hppages[pinfo.nPages])
               pinfo.nPages++;

            if (pinfo.nPages)
            {
                PropertySheetW(&pinfo);
            }
        }
        else if (pProperties->Status == NCS_MEDIA_DISCONNECTED || pProperties->Status == NCS_DISCONNECTED ||
                 pProperties->Status == NCS_HARDWARE_DISABLED)
        {
            ShowNetConnectionProperties(pContext->pNet, pContext->hwndDlg);
        }

        NcFreeNetconProperties(pProperties);
    }
}

VOID ShowNetworkIconContextMenu(
    _In_ HWND hwndOwner,
    _In_ LANSTATUSUI_CONTEXT *pContext)
{
    if (!pContext || !pContext->pNet)
        return;

    HMENU hMenu = CreatePopupMenu();
    if (!hMenu)
        return;

    POINT pt;
    GetCursorPos(&pt);

    SetForegroundWindow(hwndOwner);

    // The context menu items, set to their default values.
    struct
    {
        UINT uID;
        UINT uFlags;
        UINT_PTR uIDNewItem;
    } MenuItems[] =
    {
        {IDS_NET_ACTIVATE, MF_GRAYED, IDM_NETICON_ENABLE},
        {IDS_NET_STATUS, MF_GRAYED, IDM_NETICON_STATUS},
        {IDS_NET_REPAIR, MF_GRAYED, IDM_NETICON_REPAIR},
        {UINT_MAX, 0, 0}, // Separator
        {IDS_NET_OPEN_CONNECTIONS, MF_ENABLED, IDM_NETICON_OPEN_CONNECTIONS},
        {IDS_NET_PROPERTIES, MF_ENABLED | MFS_DEFAULT, IDM_NETICON_PROPERTIES},
    };

    NETCON_PROPERTIES *pProps = NULL;
    HRESULT hr = pContext->pNet->GetProperties(&pProps);
    if (SUCCEEDED(hr) && pProps)
    {
        if (pProps->Status == NCS_HARDWARE_DISABLED ||
            pProps->Status == NCS_MEDIA_DISCONNECTED ||
            pProps->Status == NCS_DISCONNECTED)
        {
            MenuItems[0].uID = IDS_NET_ACTIVATE;
            MenuItems[0].uFlags = MF_ENABLED | MFS_DEFAULT;
            MenuItems[0].uIDNewItem = IDM_NETICON_ENABLE;
            MenuItems[5].uFlags = MF_ENABLED;
        }
        else
        {
            MenuItems[0].uID = IDS_NET_DEACTIVATE;
            MenuItems[0].uFlags = MF_ENABLED;
            MenuItems[0].uIDNewItem = IDM_NETICON_DISABLE;
        }

        if (pProps->Status == NCS_CONNECTED)
        {
            MenuItems[1].uFlags = MF_ENABLED;
            MenuItems[2].uFlags = MF_ENABLED;
        }
        else if (pProps->Status == NCS_CONNECTING)
        {
            MenuItems[1].uFlags = MF_ENABLED;
            MenuItems[2].uFlags = MF_GRAYED;
        }
        else
        {
            MenuItems[1].uFlags = MF_GRAYED;
            MenuItems[2].uFlags = MF_GRAYED;
        }

        NcFreeNetconProperties(pProps);
        pProps = NULL;
    }
    else
    {
        MenuItems[0].uFlags = MF_GRAYED;
        MenuItems[1].uFlags = MF_GRAYED;
        MenuItems[2].uFlags = MF_GRAYED;
        MenuItems[5].uFlags = MF_GRAYED;
    }

    // Set the "Properties" item as default, if the Network "Enable/Disable" item isn't.
    if (!(MenuItems[0].uFlags & MFS_DEFAULT))
        MenuItems[5].uFlags |= MFS_DEFAULT;

    WCHAR szMenuItem[128];

    for (USHORT i = 0; i < _countof(MenuItems); ++i)
    {
        if (MenuItems[i].uID != UINT_MAX)
        {
            if (LoadStringW(netshell_hInstance, MenuItems[i].uID, szMenuItem, _countof(szMenuItem)))
                AppendMenuW(hMenu, MF_STRING | MenuItems[i].uFlags, MenuItems[i].uIDNewItem, szMenuItem);
        }
        else
        {
            AppendMenuW(hMenu, MF_SEPARATOR, 0, NULL);
        }
    }

    TrackPopupMenuEx(hMenu, TPM_LEFTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON, pt.x, pt.y, hwndOwner, NULL);

    PostMessage(hwndOwner, WM_NULL, 0, 0);

    DestroyMenu(hMenu);
}

HRESULT RepairConnection(INetConnection *pNet, HWND hwndOwner)
{
    SHELL_ErrorBox(hwndOwner, HRESULT_FROM_WIN32(ERROR_NOT_SUPPORTED));

    return E_NOTIMPL;
}

INT_PTR
CALLBACK
LANStatusDlg(
    HWND hwndDlg,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam)
{
    LANSTATUSUI_CONTEXT * pContext;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pContext = (LANSTATUSUI_CONTEXT *)lParam;
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)lParam);
            pContext->hwndStatusDlg = hwndDlg;
            pContext->nIDEvent = SetTimer(hwndDlg, NETTIMERID, 1000, NULL);
            return TRUE;

        case WM_DESTROY:
            pContext = (LANSTATUSUI_CONTEXT*)GetWindowLongPtr(hwndDlg, DWLP_USER);
            if (pContext && pContext->nIDEvent)
            {
                KillTimer(hwndDlg, pContext->nIDEvent);
                pContext->nIDEvent = 0;
            }
            SetWindowLongPtr(hwndDlg, DWLP_USER, (LONG_PTR)NULL);
            break;

        case WM_TIMER:
            pContext = (LANSTATUSUI_CONTEXT*)GetWindowLongPtr(hwndDlg, DWLP_USER);
            if (wParam == (WPARAM)pContext->nIDEvent)
            {
                UpdateLanStatus(pContext->hwndDlg, pContext);
            }
            break;

        case WM_SHOWSTATUSDLG:
            pContext = (LANSTATUSUI_CONTEXT*)GetWindowLongPtr(hwndDlg, DWLP_USER);
            if (!pContext)
                break;

            switch (LOWORD(lParam))
            {
                case WM_LBUTTONUP:
                {
                    HWND hwndTaskbar = FindWindowW(L"Shell_TrayWnd", NULL);
                    if (hwndTaskbar &&
                        SendMessageW(hwndTaskbar, WM_USER + 271, 0, 0))
                    {
                        break;
                    }
                }
                case WM_LBUTTONDBLCLK:
                    if (pContext->hwndDlg)
                    {
                        HWND hwndSheet = GetParent(pContext->hwndDlg);
                        if (hwndSheet)
                        {
                           ShowWindow(hwndSheet, SW_RESTORE);
                           SetForegroundWindow(hwndSheet);
                           BringWindowToTop(hwndSheet);
                        }
                    }
                    else
                    {
                        ShowStatusPropertyDialog(pContext, hwndDlg);
                    }
                    break;

                case WM_RBUTTONUP:
                case WM_CONTEXTMENU:
                    ShowNetworkIconContextMenu(hwndDlg, pContext);
                    break;
            }
            break;

        case WM_COMMAND:
        {
            pContext = (LANSTATUSUI_CONTEXT*)GetWindowLongPtr(hwndDlg, DWLP_USER);
            if (!pContext || !pContext->pNet)
                break;

            switch (LOWORD(wParam))
            {
                case IDM_NETICON_ENABLE:
                    pContext->pNet->Connect();
                    UpdateLanStatus(NULL, pContext);
                    break;

                case IDM_NETICON_DISABLE:
                    pContext->pNet->Disconnect();
                    UpdateLanStatus(NULL, pContext);
                    break;

                case IDM_NETICON_STATUS:
                    if (pContext->hwndDlg)
                    {
                        HWND hwndSheet = GetParent(pContext->hwndDlg);
                        if (hwndSheet)
                        {
                            ShowWindow(hwndSheet, SW_RESTORE);
                            SetForegroundWindow(hwndSheet);
                            BringWindowToTop(hwndSheet);
                        }
                    }
                    else
                    {
                        ShowStatusPropertyDialog(pContext, hwndDlg);
                    }
                    break;

                case IDM_NETICON_REPAIR:
                    RepairConnection(pContext->pNet, hwndDlg);
                    break;

                case IDM_NETICON_PROPERTIES:
                    ShowNetConnectionProperties(pContext->pNet, hwndDlg);
                    break;

                case IDM_NETICON_OPEN_CONNECTIONS:
                    ShellExecuteW(hwndDlg, NULL, L"control", L"netconnections", NULL, SW_SHOWNORMAL);
                    break;
            }
            break;
        }
    }
    return FALSE;
}

HRESULT
CLanStatus::EnumerateTrayConnections()
{
    NOTIFYICONDATAW nid;
    HWND hwndDlg;
    CComPtr<INetConnectionManager> pNetConMan;
    CComPtr<IEnumNetConnection> pEnumCon;
    CComPtr<INetConnection> pNetCon;
    NETCON_PROPERTIES* pProps;
    HRESULT hr;
    ULONG Count;
    ULONG Index;
    NOTIFICATION_ITEM * pItem, *pLast = NULL;
    LANSTATUSUI_CONTEXT * pContext;

    TRACE("InitializeNetTaskbarNotifications\n");

    if (m_pHead)
    {
       pItem = m_pHead;
       while (pItem)
       {
           hr = pItem->pNet->GetProperties(&pProps);
           if (SUCCEEDED(hr))
           {
                ZeroMemory(&nid, sizeof(nid));
                nid.cbSize = sizeof(nid);
                nid.uID = pItem->uID;
                nid.hWnd = pItem->hwndDlg;
                nid.uFlags = NIF_STATE;
                if (pProps->dwCharacter & NCCF_SHOW_ICON)
                    nid.dwState = 0;
                else
                    nid.dwState = NIS_HIDDEN;

                nid.dwStateMask = NIS_HIDDEN;
                Shell_NotifyIconW(NIM_MODIFY, &nid);
                NcFreeNetconProperties(pProps);
           }
           pItem = pItem->pNext;
       }
       return S_OK;
    }
    /* get an instance to of IConnectionManager */
    hr = CNetConnectionManager_CreateInstance(IID_PPV_ARG(INetConnectionManager, &pNetConMan));
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    hr = pNetConMan->EnumConnections(NCME_DEFAULT, &pEnumCon);
    if (FAILED_UNEXPECTEDLY(hr))
        return hr;

    Index = 1;
    while (TRUE)
    {
        pNetCon.Release();
        hr = pEnumCon->Next(1, &pNetCon, &Count);
        if (hr != S_OK)
            break;

        TRACE("new connection\n");
        pItem = static_cast<NOTIFICATION_ITEM*>(CoTaskMemAlloc(sizeof(NOTIFICATION_ITEM)));
        if (!pItem)
            break;

        pContext = static_cast<LANSTATUSUI_CONTEXT*>(CoTaskMemAlloc(sizeof(LANSTATUSUI_CONTEXT)));
        if (!pContext)
        {
            CoTaskMemFree(pItem);
            break;
        }

        ZeroMemory(pContext, sizeof(LANSTATUSUI_CONTEXT));
        pContext->uID = Index;
        pContext->pNet = pNetCon;
        pContext->Status = -1;
        pContext->dwAdapterIndex = Index;
        pItem->uID = Index;
        pItem->pNext = NULL;
        pItem->pNet = pNetCon;
        pItem->pNet->AddRef();
        hwndDlg = CreateDialogParamW(netshell_hInstance, MAKEINTRESOURCEW(IDD_STATUS), NULL, LANStatusDlg, (LPARAM)pContext);
        if (!hwndDlg)
        {
            ERR("CreateDialogParamW failed\n");
            continue;
        }

        ZeroMemory(&nid, sizeof(nid));
        nid.cbSize = sizeof(nid);
        nid.uID = Index++;
        nid.uFlags = NIF_MESSAGE;
        nid.uVersion = NOTIFYICON_VERSION;
        nid.uCallbackMessage = WM_SHOWSTATUSDLG;
        nid.hWnd = hwndDlg;

        hr = pNetCon->GetProperties(&pProps);
        if (SUCCEEDED(hr))
        {
            CopyMemory(&pItem->guidItem, &pProps->guidId, sizeof(GUID));
            if (!(pProps->dwCharacter & NCCF_SHOW_ICON))
            {
                nid.dwState = NIS_HIDDEN;
                nid.dwStateMask = NIS_HIDDEN;
                nid.uFlags |= NIF_STATE;
            }
            nid.hIcon = (HICON)LoadImage(netshell_hInstance,
                                         MAKEINTRESOURCE(pProps->Status == NCS_CONNECTED ?
                                                         IDI_NET_TRAY_WIRED : IDI_NET_TRAY_OFF),
                                         IMAGE_ICON,
                                         GetSystemMetrics(SM_CXSMICON),
                                         GetSystemMetrics(SM_CYSMICON), 0);

            if (nid.hIcon)
                nid.uFlags |= NIF_ICON;

            if (pProps->pszwName)
            {
                if (wcslen(pProps->pszwName) * sizeof(WCHAR) < sizeof(nid.szTip))
                {
                    wcscpy(nid.szTip, pProps->pszwName);
                }
                else
                {
                    CopyMemory(nid.szTip, pProps->pszwName, sizeof(nid.szTip) - sizeof(WCHAR));
                    nid.szTip[_countof(nid.szTip) - 1] = L'\0';
                }
                nid.uFlags |= NIF_TIP;
            }
            NcFreeNetconProperties(pProps);
        }
        pContext->hwndStatusDlg = hwndDlg;
        pItem->hwndDlg = hwndDlg;

        TRACE("LanStatus tray icon %u state %lu flags %lx icon %p\n", nid.uID, nid.dwState, nid.uFlags, nid.hIcon);
        if (Shell_NotifyIconW(NIM_ADD, &nid))
        {
            if (pLast)
                pLast->pNext = pItem;
            else
                m_pHead = pItem;

            pLast = pItem;
            Index++;
        }
        else
        {
            ERR("Shell_NotifyIconW failed\n");
            CoTaskMemFree(pItem);
        }

        if (nid.uFlags & NIF_ICON)
            DestroyIcon(nid.hIcon);
    }

    m_lpNetMan = pNetConMan;
    return S_OK;
}

HRESULT
CLanStatus::ShowStatusDialogByCLSID(const GUID *pguidCmdGroup)
{
    NOTIFICATION_ITEM *pItem;

    pItem = m_pHead;
    while (pItem)
    {
        if (IsEqualGUID(pItem->guidItem, *pguidCmdGroup))
        {
            SendMessageW(pItem->hwndDlg, WM_SHOWSTATUSDLG, 0, WM_LBUTTONDBLCLK);
            return S_OK;
        }
        pItem = pItem->pNext;
    }

    ERR("not found\n");
    return E_FAIL;
}

HRESULT
WINAPI
CLanStatus::QueryStatus(
    const GUID *pguidCmdGroup,
    ULONG cCmds,
    OLECMD *prgCmds,
    OLECMDTEXT *pCmdText)
{
    MessageBoxW(NULL, pCmdText->rgwz, L"IOleCommandTarget_fnQueryStatus", MB_OK);
    return E_NOTIMPL;
}

HRESULT
WINAPI
CLanStatus::Exec(
    const GUID *pguidCmdGroup,
    DWORD nCmdID,
    DWORD nCmdexecopt,
    VARIANT *pvaIn,
    VARIANT *pvaOut)
{
    if (pguidCmdGroup)
    {
        if (IsEqualGUID(*pguidCmdGroup, CGID_ShellServiceObject))
        {
            return InitializeNetTaskbarNotifications();
        }
        else
        {
            /* invoke status dialog */
            return ShowStatusDialogByCLSID(pguidCmdGroup);
        }
    }
    return S_OK;
}
