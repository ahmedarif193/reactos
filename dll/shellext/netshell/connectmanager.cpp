/*
 * PROJECT:     ReactOS Shell
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     CNetConnectionManager class
 * COPYRIGHT:   Copyright 2008 Johannes Anderwald (johannes.anderwald@reactos.org)
 */

#include "precomp.h"

VOID NormalizeOperStatus(MIB_IFROW *IfEntry, NETCON_PROPERTIES * Props);

/***************************************************************
 * INetConnection Interface
 */

HRESULT
WINAPI
CNetConnection::Initialize(PINetConnectionItem pItem)
{
    m_Props = pItem->Props;
    m_dwAdapterIndex = pItem->dwAdapterIndex;

    if (pItem->Props.pszwName)
    {
        m_Props.pszwName = static_cast<PWSTR>(CoTaskMemAlloc((wcslen(pItem->Props.pszwName)+1)*sizeof(WCHAR)));
        if (m_Props.pszwName)
            wcscpy(m_Props.pszwName, pItem->Props.pszwName);
    }

    if (pItem->Props.pszwDeviceName)
    {
        m_Props.pszwDeviceName = static_cast<PWSTR>(CoTaskMemAlloc((wcslen(pItem->Props.pszwDeviceName)+1)*sizeof(WCHAR)));
        if (m_Props.pszwDeviceName)
            wcscpy(m_Props.pszwDeviceName, pItem->Props.pszwDeviceName);
    }

    return S_OK;
}

CNetConnection::~CNetConnection()
{
    CoTaskMemFree(m_Props.pszwName);
    CoTaskMemFree(m_Props.pszwDeviceName);
}

static BOOL
FindNetworkAdapter(HDEVINFO hInfo, SP_DEVINFO_DATA *pDevInfo, LPCWSTR pGuid)
{
    DWORD dwIndex, dwSize, dwType;
    HKEY hSubKey;
    WCHAR szDriver[MAX_PATH], szNetCfg[64], szDetail[MAX_PATH];

    for (dwIndex = 0; ; dwIndex++)
    {
        ZeroMemory(pDevInfo, sizeof(SP_DEVINFO_DATA));
        pDevInfo->cbSize = sizeof(SP_DEVINFO_DATA);

        if (!SetupDiEnumDeviceInfo(hInfo, dwIndex, pDevInfo))
            break;
        if (!SetupDiGetDeviceRegistryPropertyW(hInfo, pDevInfo, SPDRP_DRIVER, NULL,
                                               (LPBYTE)szDriver, sizeof(szDriver), &dwSize))
            continue;
        if (FAILED(StringCchPrintfW(szDetail, _countof(szDetail),
                                   L"SYSTEM\\CurrentControlSet\\Control\\Class\\%s", szDriver)))
            continue;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szDetail, 0, KEY_READ, &hSubKey) != ERROR_SUCCESS)
            continue;

        dwSize = sizeof(szNetCfg);
        if (RegQueryValueExW(hSubKey, L"NetCfgInstanceId", NULL, &dwType,
                            (LPBYTE)szNetCfg, &dwSize) != ERROR_SUCCESS || dwType != REG_SZ)
        {
            RegCloseKey(hSubKey);
            continue;
        }
        RegCloseKey(hSubKey);
        if (!_wcsicmp(pGuid, szNetCfg))
            return TRUE;
    }

    return FALSE;
}

static HRESULT
SetNetworkAdapterState(const GUID *pGuid, DWORD StateChange, DWORD ConfigFlags)
{
    HKEY hKey;
    WCHAR szGuid[40], szPath[MAX_PATH * 2];
    DWORD dwSize, dwType;
    LPWSTR pPnp;
    HDEVINFO hInfo;
    SP_DEVINFO_DATA DevInfo;
    SP_PROPCHANGE_PARAMS PropChangeParams;
    LSTATUS error;
    HRESULT hr = S_OK;

    if (!StringFromGUID2(*pGuid, szGuid, _countof(szGuid)))
        return E_INVALIDARG;

    hInfo = SetupDiGetClassDevsW(&GUID_DEVCLASS_NET, NULL, NULL, DIGCF_PRESENT);
    if (hInfo == INVALID_HANDLE_VALUE)
        return HRESULT_FROM_WIN32(GetLastError());
    if (!FindNetworkAdapter(hInfo, &DevInfo, szGuid))
        hr = HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
    else
    {
        ZeroMemory(&PropChangeParams, sizeof(PropChangeParams));
        PropChangeParams.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        PropChangeParams.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        PropChangeParams.StateChange = StateChange;
        PropChangeParams.Scope = DICS_FLAG_CONFIGSPECIFIC;

        if (!SetupDiSetClassInstallParamsW(hInfo, &DevInfo, &PropChangeParams.ClassInstallHeader,
                                          sizeof(PropChangeParams)) ||
            !SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, hInfo, &DevInfo))
            hr = HRESULT_FROM_WIN32(GetLastError());
    }
    SetupDiDestroyDeviceInfoList(hInfo);
    if (FAILED(hr))
        return hr;

    hr = StringCchPrintfW(szPath, _countof(szPath),
                         L"SYSTEM\\CurrentControlSet\\Control\\Network\\"
                         L"{4D36E972-E325-11CE-BFC1-08002BE10318}\\%s\\Connection", szGuid);
    if (FAILED(hr))
        return hr;

    error = RegOpenKeyExW(HKEY_LOCAL_MACHINE, szPath, 0, KEY_QUERY_VALUE, &hKey);
    if (error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(error);

    dwSize = 0;
    error = RegQueryValueExW(hKey, L"PnpInstanceID", NULL, &dwType, NULL, &dwSize);
    if (error != ERROR_SUCCESS || dwType != REG_SZ)
    {
        RegCloseKey(hKey);
        return HRESULT_FROM_WIN32(error != ERROR_SUCCESS ? error : ERROR_INVALID_DATA);
    }

    pPnp = static_cast<PWSTR>(CoTaskMemAlloc(dwSize));
    if (!pPnp)
    {
        RegCloseKey(hKey);
        return E_OUTOFMEMORY;
    }

    error = RegQueryValueExW(hKey, L"PnpInstanceID", NULL, &dwType, (LPBYTE)pPnp, &dwSize);
    RegCloseKey(hKey);
    if (error == ERROR_SUCCESS)
        hr = StringCchPrintfW(szPath, _countof(szPath),
                             L"System\\CurrentControlSet\\Hardware Profiles\\Current\\"
                             L"System\\CurrentControlSet\\Enum\\%s", pPnp);
    CoTaskMemFree(pPnp);
    if (error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(error);
    if (FAILED(hr))
        return hr;

    error = RegCreateKeyExW(HKEY_LOCAL_MACHINE, szPath, 0, NULL, 0, KEY_SET_VALUE,
                            NULL, &hKey, NULL);
    if (error != ERROR_SUCCESS)
        return HRESULT_FROM_WIN32(error);
    error = RegSetValueExW(hKey, L"CSConfigFlags", 0, REG_DWORD,
                           (const BYTE *)&ConfigFlags, sizeof(ConfigFlags));
    RegCloseKey(hKey);

    return HRESULT_FROM_WIN32(error);
}

HRESULT WINAPI
CNetConnection::Connect()
{
    return SetNetworkAdapterState(&m_Props.guidId, DICS_ENABLE, 0);
}

HRESULT WINAPI
CNetConnection::Disconnect()
{
    return SetNetworkAdapterState(&m_Props.guidId, DICS_DISABLE, 1);
}

HRESULT
WINAPI
CNetConnection::Delete()
{
    return E_NOTIMPL;
}

HRESULT
WINAPI
CNetConnection::Duplicate(
    LPCWSTR pszwDuplicateName,
    INetConnection **ppCon)
{
    return E_NOTIMPL;
}

HRESULT
WINAPI
CNetConnection::GetProperties(NETCON_PROPERTIES **ppProps)
{
    MIB_IFROW IfEntry;
    HKEY hKey;
    LPOLESTR pStr;
    WCHAR szName[140];
    DWORD dwShowIcon, dwNotifyDisconnect, dwType, dwSize;
    NETCON_PROPERTIES * pProperties;
    HRESULT hr;

    if (!ppProps)
        return E_POINTER;

    pProperties = static_cast<NETCON_PROPERTIES*>(CoTaskMemAlloc(sizeof(NETCON_PROPERTIES)));
    if (!pProperties)
        return E_OUTOFMEMORY;

    CopyMemory(pProperties, &m_Props, sizeof(NETCON_PROPERTIES));
    pProperties->pszwName = NULL;

    if (m_Props.pszwDeviceName)
    {
        pProperties->pszwDeviceName = static_cast<LPWSTR>(CoTaskMemAlloc((wcslen(m_Props.pszwDeviceName)+1)*sizeof(WCHAR)));
        if (pProperties->pszwDeviceName)
            wcscpy(pProperties->pszwDeviceName, m_Props.pszwDeviceName);
    }

    *ppProps = pProperties;

    /* get updated adapter characteristics */
    ZeroMemory(&IfEntry, sizeof(IfEntry));
    IfEntry.dwIndex = m_dwAdapterIndex;
    if (GetIfEntry(&IfEntry) != NO_ERROR)
        return NOERROR;

    NormalizeOperStatus(&IfEntry, pProperties);


    hr = StringFromCLSID((CLSID)m_Props.guidId, &pStr);
    if (SUCCEEDED(hr))
    {
        wcscpy(szName, L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\");
        wcscat(szName, pStr);
        wcscat(szName, L"\\Connection");

        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szName, 0, KEY_READ, &hKey) == ERROR_SUCCESS)
        {
            dwSize = sizeof(dwShowIcon);
            if (RegQueryValueExW(hKey, L"ShowIcon", NULL, &dwType, (LPBYTE)&dwShowIcon, &dwSize) == ERROR_SUCCESS && dwType == REG_DWORD)
            {
                if (dwShowIcon)
                    pProperties->dwCharacter |= NCCF_SHOW_ICON;
                else
                    pProperties->dwCharacter &= ~NCCF_SHOW_ICON;
            }

            dwSize = sizeof(dwNotifyDisconnect);
            if (RegQueryValueExW(hKey, L"IpCheckingEnabled", NULL, &dwType, (LPBYTE)&dwNotifyDisconnect, &dwSize) == ERROR_SUCCESS && dwType == REG_DWORD)
            {
                if (dwNotifyDisconnect)
                    pProperties->dwCharacter |= NCCF_NOTIFY_DISCONNECTED;
                else
                    pProperties->dwCharacter &= ~NCCF_NOTIFY_DISCONNECTED;
            }

            dwSize = sizeof(szName);
            if (RegQueryValueExW(hKey, L"Name", NULL, &dwType, (LPBYTE)szName, &dwSize) == ERROR_SUCCESS)
            {
                /* use updated name */
                dwSize = wcslen(szName) + 1;
                pProperties->pszwName = static_cast<PWSTR>(CoTaskMemAlloc(dwSize * sizeof(WCHAR)));
                if (pProperties->pszwName)
                    CopyMemory(pProperties->pszwName, szName, dwSize * sizeof(WCHAR));
            }
            else
            {
                /* use cached name */
                if (m_Props.pszwName)
                {
                    pProperties->pszwName = static_cast<PWSTR>(CoTaskMemAlloc((wcslen(m_Props.pszwName)+1)*sizeof(WCHAR)));
                    if (pProperties->pszwName)
                        wcscpy(pProperties->pszwName, m_Props.pszwName);
                }
            }
            RegCloseKey(hKey);
        }
        CoTaskMemFree(pStr);
    }

    /* Enable 'Rename' and 'Delete' for Adminstrators only */
    if (IsUserAdmin())
    {
        pProperties->dwCharacter |= NCCF_ALLOW_RENAME;

        /* Virtual network interfaces can be deleted */
        if (IfEntry.dwType == IF_TYPE_TUNNEL)
        {
            pProperties->dwCharacter |= NCCF_ALLOW_REMOVAL;
        }
    }
    else
    {
        pProperties->dwCharacter &= ~(NCCF_ALLOW_RENAME | NCCF_ALLOW_REMOVAL);
    }

    return S_OK;
}

HRESULT
WINAPI
CNetConnection::GetUiObjectClassId(CLSID *pclsid)
{
    if (m_Props.MediaType == NCM_LAN)
    {
        CopyMemory(pclsid, &CLSID_LanConnectionUi, sizeof(CLSID));
        return S_OK;
    }

    return E_NOTIMPL;
}

HRESULT
WINAPI
CNetConnection::Rename(LPCWSTR pszwDuplicateName)
{
    WCHAR szName[140];
    LPOLESTR pStr;
    DWORD dwSize;
    HKEY hKey;
    HRESULT hr;

    if (pszwDuplicateName == NULL || wcslen(pszwDuplicateName) == 0)
        return S_OK;

    if (m_Props.pszwName)
    {
        CoTaskMemFree(m_Props.pszwName);
        m_Props.pszwName = NULL;
    }

    dwSize = (wcslen(pszwDuplicateName) + 1) * sizeof(WCHAR);
    m_Props.pszwName = static_cast<PWSTR>(CoTaskMemAlloc(dwSize));
    if (m_Props.pszwName == NULL)
        return E_OUTOFMEMORY;

    wcscpy(m_Props.pszwName, pszwDuplicateName);

    hr = StringFromCLSID((CLSID)m_Props.guidId, &pStr);
    if (SUCCEEDED(hr))
    {
        wcscpy(szName, L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\");
        wcscat(szName, pStr);
        wcscat(szName, L"\\Connection");

        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szName, 0, KEY_WRITE, &hKey) == ERROR_SUCCESS)
        {
            RegSetValueExW(hKey, L"Name", NULL, REG_SZ, (LPBYTE)m_Props.pszwName, dwSize);
            RegCloseKey(hKey);
        }

        CoTaskMemFree(pStr);
    }

    return hr;
}

HRESULT WINAPI CNetConnection_CreateInstance(PINetConnectionItem pItem, REFIID riid, LPVOID * ppv)
{
    return ShellObjectCreatorInit<CNetConnection>(pItem, riid, ppv);
}



CNetConnectionManager::CNetConnectionManager() :
    m_pHead(NULL),
    m_pCurrent(NULL)
{
}

HRESULT
WINAPI
CNetConnectionManager::EnumConnections(
    NETCONMGR_ENUM_FLAGS Flags,
    IEnumNetConnection **ppEnum)
{
    TRACE("EnumConnections\n");

    if (!ppEnum)
        return E_POINTER;

    if (Flags != NCME_DEFAULT)
        return E_FAIL;

    *ppEnum = static_cast<IEnumNetConnection*>(this);
    AddRef();
    return S_OK;
}

/***************************************************************
 * IEnumNetConnection Interface
 */

HRESULT
WINAPI
CNetConnectionManager::Next(
    ULONG celt,
    INetConnection **rgelt,
    ULONG *pceltFetched)
{
    HRESULT hr;

    if (!pceltFetched || !rgelt)
        return E_POINTER;

    if (celt != 1)
        return E_FAIL;

    if (!m_pCurrent)
        return S_FALSE;

    hr = CNetConnection_CreateInstance(m_pCurrent, IID_PPV_ARG(INetConnection, rgelt));
    m_pCurrent = m_pCurrent->Next;

    return hr;
}

HRESULT
WINAPI
CNetConnectionManager::Skip(ULONG celt)
{
    while (m_pCurrent && celt-- > 0)
        m_pCurrent = m_pCurrent->Next;

    if (celt)
       return S_FALSE;
    else
       return S_OK;

}

HRESULT
WINAPI
CNetConnectionManager::Reset()
{
    m_pCurrent = m_pHead;
    return S_OK;
}

HRESULT
WINAPI
CNetConnectionManager::Clone(IEnumNetConnection **ppenum)
{
    return E_NOTIMPL;
}

BOOL
GetAdapterIndexFromNetCfgInstanceId(PIP_ADAPTER_INFO pAdapterInfo, LPWSTR szNetCfg, PDWORD pIndex)
{
    WCHAR szBuffer[50];
    IP_ADAPTER_INFO * pCurrentAdapter;

    pCurrentAdapter = pAdapterInfo;
    while (pCurrentAdapter)
    {
        szBuffer[0] = L'\0';
        if (MultiByteToWideChar(CP_ACP, 0, pCurrentAdapter->AdapterName, -1, szBuffer, sizeof(szBuffer)/sizeof(szBuffer[0])))
        {
            szBuffer[(sizeof(szBuffer)/sizeof(WCHAR))-1] = L'\0';
        }
        if (!_wcsicmp(szBuffer, szNetCfg))
        {
            *pIndex = pCurrentAdapter->Index;
            return TRUE;
        }
        pCurrentAdapter = pCurrentAdapter->Next;
    }
    return FALSE;
}

VOID
NormalizeOperStatus(
    MIB_IFROW *IfEntry,
    NETCON_PROPERTIES    * Props)
{
    switch (IfEntry->dwOperStatus)
    {
        case MIB_IF_OPER_STATUS_NON_OPERATIONAL:
            Props->Status = NCS_HARDWARE_DISABLED;
            break;
        case MIB_IF_OPER_STATUS_UNREACHABLE:
            Props->Status = NCS_DISCONNECTED;
            break;
        case MIB_IF_OPER_STATUS_DISCONNECTED:
            Props->Status = NCS_MEDIA_DISCONNECTED;
            break;
        case MIB_IF_OPER_STATUS_CONNECTING:
            Props->Status = NCS_CONNECTING;
            break;
        case MIB_IF_OPER_STATUS_CONNECTED:
            Props->Status = NCS_CONNECTED;
            break;
        case MIB_IF_OPER_STATUS_OPERATIONAL:
            Props->Status = NCS_CONNECTED;
            break;
        default:
            break;
    }
}

static BOOL
GuidFromAdapterName(LPCSTR pszName, GUID *pGuid)
{
    WCHAR szWide[64];
    ULONG h1 = 2166136261u, h2 = 0x9747b28cu;
    const char *p;

    ZeroMemory(pGuid, sizeof(*pGuid));
    if (!pszName || !pszName[0])
        return FALSE;
    if (MultiByteToWideChar(CP_ACP, 0, pszName, -1, szWide, _countof(szWide)) &&
        szWide[0] == L'{' && SUCCEEDED(CLSIDFromString(szWide, pGuid)))
        return TRUE;
    for (p = pszName; *p; p++)
    {
        h1 = (h1 ^ (UCHAR)*p) * 16777619u;
        h2 = (h2 ^ (UCHAR)*p) * 0x01000193u;
    }
    pGuid->Data1 = h1;
    pGuid->Data2 = (USHORT)(h2 >> 16);
    pGuid->Data3 = (USHORT)(h2 & 0xFFFF);
    pGuid->Data4[0] = (UCHAR)(h1 >> 24); pGuid->Data4[1] = (UCHAR)(h1 >> 16);
    pGuid->Data4[2] = (UCHAR)(h1 >> 8);  pGuid->Data4[3] = (UCHAR)h1;
    pGuid->Data4[4] = (UCHAR)(h2 >> 24); pGuid->Data4[5] = (UCHAR)(h2 >> 16);
    pGuid->Data4[6] = (UCHAR)(h2 >> 8);  pGuid->Data4[7] = (UCHAR)h2;
    return TRUE;
}

static PWSTR
DupConnectionString(LPCWSTR pszText)
{
    PWSTR pszCopy;
    if (!pszText)
        return NULL;
    pszCopy = static_cast<PWSTR>(CoTaskMemAlloc((wcslen(pszText) + 1) * sizeof(WCHAR)));
    if (pszCopy)
        wcscpy(pszCopy, pszText);
    return pszCopy;
}

HRESULT
CNetConnectionManager::EnumerateINetConnections()
{
    ULONG cbBuffer = 16 * 1024;
    ULONG ret = ERROR_BUFFER_OVERFLOW;
    PIP_ADAPTER_ADDRESSES pAddresses = NULL;
    PIP_ADAPTER_ADDRESSES pAdapter;
    PINetConnectionItem pCurrent = NULL;
    int attempt;

    for (attempt = 0; attempt < 3 && ret == ERROR_BUFFER_OVERFLOW; attempt++)
    {
        pAddresses = static_cast<PIP_ADAPTER_ADDRESSES>(CoTaskMemAlloc(cbBuffer));
        if (!pAddresses)
            return E_OUTOFMEMORY;
        ret = GetAdaptersAddresses(AF_UNSPEC,
                                   GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
                                   NULL, pAddresses, &cbBuffer);
        if (ret != NO_ERROR)
        {
            CoTaskMemFree(pAddresses);
            pAddresses = NULL;
        }
    }
    if (ret != NO_ERROR)
        return HRESULT_FROM_WIN32(ret);

    for (pAdapter = pAddresses; pAdapter != NULL; pAdapter = pAdapter->Next)
    {
        MIB_IFROW IfEntry;
        HKEY hSubKey;
        LPOLESTR pStr = NULL;
        WCHAR szName[200];
        WCHAR szValue[128];
        DWORD dwSize, dwShowIcon, dwNotifyDisconnect;
        PINetConnectionItem pNew;

        if (pAdapter->IfType == IF_TYPE_SOFTWARE_LOOPBACK ||
            pAdapter->IfType == IF_TYPE_TUNNEL)
            continue;

        ZeroMemory(&IfEntry, sizeof(IfEntry));
        IfEntry.dwIndex = pAdapter->IfIndex;
        if (GetIfEntry(&IfEntry) != NO_ERROR)
            continue;

        pNew = static_cast<PINetConnectionItem>(CoTaskMemAlloc(sizeof(INetConnectionItem)));
        if (!pNew)
            break;

        ZeroMemory(pNew, sizeof(INetConnectionItem));
        pNew->dwAdapterIndex = pAdapter->IfIndex;
        GuidFromAdapterName(pAdapter->AdapterName, &pNew->Props.guidId);
        NormalizeOperStatus(&IfEntry, &pNew->Props);

        switch (IfEntry.dwType)
        {
            case IF_TYPE_ETHERNET_CSMACD:
                pNew->Props.MediaType = NCM_LAN;
                break;
            case IF_TYPE_IEEE80211:
                pNew->Props.MediaType = NCM_SHAREDACCESSHOST_RAS;
                break;
            default:
                pNew->Props.MediaType = NCM_LAN;
                break;
        }
        pNew->Props.dwCharacter |= NCCF_SHOW_ICON;

        if (SUCCEEDED(StringFromCLSID(pNew->Props.guidId, &pStr)))
        {
            wcscpy(szName, L"SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}\\");
            wcscat(szName, pStr);
            wcscat(szName, L"\\Connection");
            CoTaskMemFree(pStr);
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, szName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS)
            {
                dwSize = sizeof(szValue);
                if (RegQueryValueExW(hSubKey, L"Name", NULL, NULL, (LPBYTE)szValue, &dwSize) == ERROR_SUCCESS && szValue[0])
                    pNew->Props.pszwName = DupConnectionString(szValue);
                dwSize = sizeof(dwShowIcon);
                dwShowIcon = 1;
                RegQueryValueExW(hSubKey, L"ShowIcon", NULL, NULL, (LPBYTE)&dwShowIcon, &dwSize);
                if (!dwShowIcon)
                    pNew->Props.dwCharacter &= ~NCCF_SHOW_ICON;
                dwSize = sizeof(dwNotifyDisconnect);
                if (RegQueryValueExW(hSubKey, L"IpCheckingEnabled", NULL, NULL, (LPBYTE)&dwNotifyDisconnect, &dwSize) == ERROR_SUCCESS)
                {
                    if (dwNotifyDisconnect)
                        pNew->Props.dwCharacter |= NCCF_NOTIFY_DISCONNECTED;
                }
                RegCloseKey(hSubKey);
            }
        }

        if (!pNew->Props.pszwName)
        {
            if (pAdapter->FriendlyName && pAdapter->FriendlyName[0])
                pNew->Props.pszwName = DupConnectionString(pAdapter->FriendlyName);
            else if (IfEntry.dwType == IF_TYPE_IEEE80211)
                pNew->Props.pszwName = DupConnectionString(L"Wireless Network Connection");
            else
                pNew->Props.pszwName = DupConnectionString(L"Local Area Connection");
        }

        if (pAdapter->Description && pAdapter->Description[0])
            pNew->Props.pszwDeviceName = DupConnectionString(pAdapter->Description);
        else if (IfEntry.bDescr[0])
        {
            MultiByteToWideChar(CP_ACP, 0, (LPCSTR)IfEntry.bDescr, -1, szValue, _countof(szValue));
            szValue[_countof(szValue) - 1] = 0;
            pNew->Props.pszwDeviceName = DupConnectionString(szValue);
        }
        else
            pNew->Props.pszwDeviceName = DupConnectionString(L"Network adapter");

        if (pCurrent)
            pCurrent->Next = pNew;
        else
            m_pHead = pNew;
        pCurrent = pNew;
    }

    CoTaskMemFree(pAddresses);

    m_pCurrent = m_pHead;
    return (m_pHead != NULL ? S_OK : S_FALSE);
}

HRESULT CNetConnectionManager::Initialize()
{
    HRESULT hr = EnumerateINetConnections();
    if (FAILED_UNEXPECTEDLY(hr))
    {
        /* If something went wrong during the enumeration print an error don't enumerate anything */
        m_pCurrent = m_pHead = NULL;
        return S_FALSE;
    }
    return S_OK;
}

HRESULT WINAPI CNetConnectionManager_CreateInstance(REFIID riid, LPVOID * ppv)
{
#if USE_CUSTOM_CONMGR
    return ShellObjectCreatorInit<CNetConnectionManager>(riid, ppv);
#else
    return CoCreateInstance(CLSID_ConnectionManager, NULL, CLSCTX_ALL, riid, ppv);
#endif
}
