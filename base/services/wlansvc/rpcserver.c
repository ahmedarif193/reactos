/*
 * PROJECT:     ReactOS WLAN Service
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/services/wlansvc/rpcserver.c
 * PURPOSE:     RPC server interface
 * COPYRIGHT:   Copyright 2009 Christoph von Wittich
 */

#include "precomp.h"

#define NDEBUG
#include <debug.h>

LIST_ENTRY WlanSvcHandleListHead;

DWORD WINAPI RpcThreadRoutine(LPVOID lpParameter)
{
    RPC_STATUS Status;

    WlanSvcInitialize();

    Status = RpcServerUseProtseqEpW(L"ncalrpc", 20, L"wlansvc", NULL);
    if (Status != RPC_S_OK)
    {
        DPRINT1("WLANSVC: RpcServerUseProtseqEpW failed (Status %lx)\n", Status);
        return 0;
    }

    Status = RpcServerRegisterIf(wlansvc_interface_v1_0_s_ifspec, NULL, NULL);
    if (Status != RPC_S_OK)
    {
        DPRINT1("WLANSVC: RpcServerRegisterIf failed (Status %lx)\n", Status);
        return 0;
    }

    Status = RpcServerListen(1, RPC_C_LISTEN_MAX_CALLS_DEFAULT, 0);
    if (Status != RPC_S_OK)
    {
        DPRINT1("WLANSVC: RpcServerListen failed (Status %lx)\n", Status);
    }

    return 0;
}

/* Lock must be held by the caller. */
PWLANSVCHANDLE WlanSvcGetHandleEntry(WLANSVC_RPC_HANDLE ClientHandle)
{
    PLIST_ENTRY CurrentEntry;
    PWLANSVCHANDLE lpWlanSvcHandle;

    CurrentEntry = WlanSvcHandleListHead.Flink;
    while (CurrentEntry != &WlanSvcHandleListHead)
    {
        lpWlanSvcHandle = CONTAINING_RECORD(CurrentEntry,
                                        WLANSVCHANDLE,
                                        WlanSvcHandleListEntry);
        CurrentEntry = CurrentEntry->Flink;

        if (lpWlanSvcHandle == (PWLANSVCHANDLE) ClientHandle)
            return lpWlanSvcHandle;
    }

    return NULL;
}

/* Validate an incoming client handle under the lock. */
static PWLANSVCHANDLE WlanSvcValidateHandle(WLANSVC_RPC_HANDLE ClientHandle)
{
    PWLANSVCHANDLE h;

    EnterCriticalSection(&WlanSvcLock);
    h = WlanSvcGetHandleEntry(ClientHandle);
    LeaveCriticalSection(&WlanSvcLock);
    return h;
}

/* TRUE if the RPC caller is an Administrator or LocalSystem (allowed to read plaintext keys). */
static BOOL WlanSvcClientIsPrivileged(VOID)
{
    SID_IDENTIFIER_AUTHORITY NtAuthority = {SECURITY_NT_AUTHORITY};
    PSID AdminSid = NULL;
    BOOL bIsMember = FALSE;

    if (RpcImpersonateClient(NULL) != RPC_S_OK)
        return FALSE;

    if (AllocateAndInitializeSid(&NtAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &AdminSid))
    {
        if (!CheckTokenMembership(NULL, AdminSid, &bIsMember))
            bIsMember = FALSE;
        FreeSid(AdminSid);
    }

    RpcRevertToSelf();
    return bIsMember;
}

DWORD _RpcOpenHandle(
    wchar_t *arg_1,
    DWORD dwClientVersion,
    DWORD *pdwNegotiatedVersion,
    LPWLANSVC_RPC_HANDLE phClientHandle)
{
    PWLANSVCHANDLE lpWlanSvcHandle;

    /* Service start is driven by the RPC thread, but a client may race it. */
    WlanSvcInitialize();

    lpWlanSvcHandle = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(WLANSVCHANDLE));
    if (lpWlanSvcHandle == NULL)
    {
        DPRINT1("Failed to allocate Heap!\n");
        return ERROR_NOT_ENOUGH_MEMORY;
    }

    if (dwClientVersion > 2)
        dwClientVersion = 2;

    if (dwClientVersion < 1)
        dwClientVersion = 1;

    lpWlanSvcHandle->dwClientVersion = dwClientVersion;
    lpWlanSvcHandle->dwNotifSource = WLAN_NOTIFICATION_SOURCE_NONE;
    InitializeListHead(&lpWlanSvcHandle->NotificationQueue);
    lpWlanSvcHandle->hNotifyEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    *pdwNegotiatedVersion = dwClientVersion;

    EnterCriticalSection(&WlanSvcLock);
    InsertTailList(&WlanSvcHandleListHead, &lpWlanSvcHandle->WlanSvcHandleListEntry);
    LeaveCriticalSection(&WlanSvcLock);

    *phClientHandle = lpWlanSvcHandle;

    return ERROR_SUCCESS;
}

DWORD _RpcCloseHandle(
    LPWLANSVC_RPC_HANDLE phClientHandle)
{
    PWLANSVCHANDLE lpWlanSvcHandle;
    HANDLE hEvent;

    EnterCriticalSection(&WlanSvcLock);
    lpWlanSvcHandle = WlanSvcGetHandleEntry(*phClientHandle);
    if (!lpWlanSvcHandle)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_INVALID_HANDLE;
    }

    RemoveEntryList(&lpWlanSvcHandle->WlanSvcHandleListEntry);
    WlanSvcDrainHandleQueue(lpWlanSvcHandle);
    hEvent = lpWlanSvcHandle->hNotifyEvent;
    lpWlanSvcHandle->hNotifyEvent = NULL;
    LeaveCriticalSection(&WlanSvcLock);

    /* Release any async getter parked on this handle, then tear it down. */
    if (hEvent != NULL)
    {
        SetEvent(hEvent);
        CloseHandle(hEvent);
    }

    HeapFree(GetProcessHeap(), 0, lpWlanSvcHandle);
    *phClientHandle = NULL;

    return ERROR_SUCCESS;
}

DWORD _RpcEnumInterfaces(
    WLANSVC_RPC_HANDLE hClientHandle,
    PWLAN_INTERFACE_INFO_LIST *ppInterfaceList)
{
    PWLAN_INTERFACE_INFO_LIST list;
    PLIST_ENTRY entry;
    DWORD count, idx;
    SIZE_T size;

    *ppInterfaceList = NULL;

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&WlanSvcLock);

    count = 0;
    for (entry = WlanSvcInterfaceListHead.Flink;
         entry != &WlanSvcInterfaceListHead;
         entry = entry->Flink)
    {
        count++;
    }

    size = FIELD_OFFSET(WLAN_INTERFACE_INFO_LIST, InterfaceInfo) +
           (SIZE_T)(count == 0 ? 1 : count) * sizeof(WLAN_INTERFACE_INFO);

    list = midl_user_allocate(size);
    if (!list)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    ZeroMemory(list, size);

    idx = 0;
    for (entry = WlanSvcInterfaceListHead.Flink;
         entry != &WlanSvcInterfaceListHead;
         entry = entry->Flink)
    {
        PWLANSVC_INTERFACE iface =
            CONTAINING_RECORD(entry, WLANSVC_INTERFACE, ListEntry);

        list->InterfaceInfo[idx].InterfaceGuid = iface->InterfaceGuid;
        wcsncpy(list->InterfaceInfo[idx].strInterfaceDescription,
                iface->Description, 255);
        list->InterfaceInfo[idx].isState = iface->State;
        idx++;
    }

    list->dwNumberOfItems = count;
    list->dwIndex = 0;

    LeaveCriticalSection(&WlanSvcLock);

    *ppInterfaceList = list;
    return ERROR_SUCCESS;
}

DWORD _RpcSetAutoConfigParameter(
    WLANSVC_RPC_HANDLE hClientHandle,
    long OpCode,
    DWORD dwDataSize,
    LPBYTE pData)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcQueryAutoConfigParameter(
    WLANSVC_RPC_HANDLE hClientHandle,
    DWORD OpCode,
    LPDWORD pdwDataSize,
    char **ppData,
    DWORD *pWlanOpcodeValueType)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcGetInterfaceCapability(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    PWLAN_INTERFACE_CAPABILITY *ppCapability)
{
    PWLANSVCHANDLE lpWlanSvcHandle;

    lpWlanSvcHandle = WlanSvcGetHandleEntry(hClientHandle);
    if (!lpWlanSvcHandle)
    {
        return ERROR_INVALID_HANDLE;
    }

    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcSetInterface(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    DWORD OpCode,
    DWORD dwDataSize,
    LPBYTE pData)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcQueryInterface(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    long OpCode,
    LPDWORD pdwDataSize,
    LPBYTE *ppData,
    LPDWORD pWlanOpcodeValueType)
{
    PWLANSVC_INTERFACE iface;
    DWORD dwResult;

    *ppData = NULL;
    *pdwDataSize = 0;

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(pInterfaceGuid);
    if (iface == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_FOUND;
    }

    dwResult = WlanSvcQueryInterface(iface, (WLAN_INTF_OPCODE)OpCode,
                                     pdwDataSize, ppData, pWlanOpcodeValueType);
    LeaveCriticalSection(&WlanSvcLock);
    return dwResult;
}

DWORD _RpcIhvControl(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    DWORD Type,
    DWORD dwInBufferSize,
    LPBYTE pInBuffer,
    DWORD dwOutBufferSize,
    LPBYTE pOutBuffer,
    LPDWORD pdwBytesReturned)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcScan(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    PDOT11_SSID pDot11Ssid,
    PWLAN_RAW_DATA pIeData)
{
    PWLANSVC_INTERFACE iface;
    PNWIFI_BSS_LIST bssList = NULL;
    GUID interfaceGuid;
    DOT11_MAC_ADDRESS macAddress;
    ULONG interfaceIndex;
    ULONG64 upperLuid;
    DWORD dwResult;

    UNREFERENCED_PARAMETER(pIeData);

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(pInterfaceGuid);
    if (iface == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_FOUND;
    }

    interfaceGuid = iface->InterfaceGuid;
    interfaceIndex = iface->NwifiIndex;
    upperLuid = iface->UpperLuid;
    macAddress = iface->MacAddress;
    LeaveCriticalSection(&WlanSvcLock);

    dwResult = NwifiScan(interfaceIndex, pDot11Ssid, dot11_BSS_type_any);
    if (dwResult == ERROR_SUCCESS || dwResult == ERROR_NOT_READY)
    {
        dwResult = NwifiGetBssList(interfaceIndex, upperLuid, &macAddress, &bssList);
        if (dwResult == ERROR_SUCCESS && bssList == NULL)
            dwResult = ERROR_NOT_ENOUGH_MEMORY;
    }

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(&interfaceGuid);
    if (iface == NULL || iface->NwifiIndex != interfaceIndex || iface->UpperLuid != upperLuid || memcmp(&iface->MacAddress, &macAddress, sizeof(macAddress)) != 0)
    {
        dwResult = ERROR_NOT_FOUND;
    }
    else if (dwResult == ERROR_SUCCESS)
    {
        dwResult = WlanSvcApplyBssCache(iface, bssList);
    }
    if (iface != NULL && dwResult == ERROR_SUCCESS)
        WlanSvcIndicateAcm(iface, wlan_notification_acm_scan_complete);
    LeaveCriticalSection(&WlanSvcLock);

    if (bssList != NULL)
        HeapFree(GetProcessHeap(), 0, bssList);

    return dwResult;
}

DWORD _RpcGetAvailableNetworkList(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    DWORD dwFlags,
    WLAN_AVAILABLE_NETWORK_LIST **ppAvailableNetworkList)
{
    PWLANSVC_INTERFACE iface;
    DWORD dwResult;

    *ppAvailableNetworkList = NULL;

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(pInterfaceGuid);
    if (iface == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_FOUND;
    }

    dwResult = WlanSvcBuildAvailableNetworkList(iface, dwFlags,
                                                ppAvailableNetworkList);
    LeaveCriticalSection(&WlanSvcLock);
    return dwResult;
}

DWORD _RpcGetNetworkBssList(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    PDOT11_SSID pDot11Ssid,
    short dot11BssType,
    DWORD bSecurityEnabled,
    LPDWORD dwBssListSize,
    LPBYTE *ppWlanBssList)
{
    PWLANSVC_INTERFACE iface;
    PWLAN_BSS_LIST list = NULL;
    DWORD dwResult, size = 0;

    *ppWlanBssList = NULL;
    *dwBssListSize = 0;

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(pInterfaceGuid);
    if (iface == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_FOUND;
    }

    dwResult = WlanSvcBuildBssList(iface, pDot11Ssid,
                                   (DOT11_BSS_TYPE)dot11BssType,
                                   bSecurityEnabled ? TRUE : FALSE,
                                   &list, &size);
    LeaveCriticalSection(&WlanSvcLock);

    if (dwResult != ERROR_SUCCESS)
        return dwResult;

    /*
     * The RPC contract returns the BSS list as size_is(*dwBssListSize) LPBYTE,
     * so hand back an RPC-owned copy of the flat buffer.
     */
    *ppWlanBssList = midl_user_allocate(size);
    if (*ppWlanBssList == NULL)
    {
        HeapFree(GetProcessHeap(), 0, list);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    memcpy(*ppWlanBssList, list, size);
    *dwBssListSize = size;

    HeapFree(GetProcessHeap(), 0, list);
    return ERROR_SUCCESS;
}

DWORD _RpcConnect(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    const PWLAN_CONNECTION_PARAMETERS *pConnectionParameters)
{
    PWLANSVC_INTERFACE iface;
    DWORD dwResult;

    if (pConnectionParameters == NULL || *pConnectionParameters == NULL)
        return ERROR_INVALID_PARAMETER;

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(pInterfaceGuid);
    if (iface == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_FOUND;
    }

    dwResult = WlanSvcConnect(iface, *pConnectionParameters);
    LeaveCriticalSection(&WlanSvcLock);
    return dwResult;
}

DWORD _RpcDisconnect(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGUID)
{
    PWLANSVC_INTERFACE iface;
    DWORD dwResult;

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(pInterfaceGUID);
    if (iface == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_FOUND;
    }

    dwResult = WlanSvcDisconnect(iface);
    LeaveCriticalSection(&WlanSvcLock);
    return dwResult;
}

DWORD _RpcRegisterNotification(
    WLANSVC_RPC_HANDLE hClientHandle,
    DWORD arg_2,
    LPDWORD pdwPrevNotifSource)
{
    PWLANSVCHANDLE h;

    EnterCriticalSection(&WlanSvcLock);
    h = WlanSvcGetHandleEntry(hClientHandle);
    if (h == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_INVALID_HANDLE;
    }

    if (pdwPrevNotifSource != NULL)
        *pdwPrevNotifSource = h->dwNotifSource;

    /* arg_2 is the requested WLAN_NOTIFICATION_SOURCE_* mask. */
    h->dwNotifSource = arg_2;

    /* On unsubscribe, wake any worker parked in _RpcAsyncGetNotification so
     * it can observe the NONE state and return. */
    if (arg_2 == WLAN_NOTIFICATION_SOURCE_NONE && h->hNotifyEvent != NULL)
        SetEvent(h->hNotifyEvent);

    LeaveCriticalSection(&WlanSvcLock);

    return ERROR_SUCCESS;
}

DWORD _RpcAsyncGetNotification(
    WLANSVC_RPC_HANDLE hClientHandle,
    PWLAN_NOTIFICATION_DATA *NotificationData)
{
    PWLANSVCHANDLE h;

    *NotificationData = NULL;

    /* The client's notification worker blocks here; WlanSvcDequeueNotification
     * waits internally and copes with the handle closing meanwhile. */
    EnterCriticalSection(&WlanSvcLock);
    h = WlanSvcGetHandleEntry(hClientHandle);
    LeaveCriticalSection(&WlanSvcLock);
    if (h == NULL)
        return ERROR_INVALID_HANDLE;

    return WlanSvcDequeueNotification(h, NotificationData);
}

DWORD _RpcSetProfileEapUserData(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    wchar_t *strProfileName,
    EAP_METHOD_TYPE MethodType,
    DWORD dwFlags,
    DWORD dwEapUserDataSize,
    LPBYTE pbEapUserData)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcSetProfile(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    DWORD dwFlags,
    wchar_t *strProfileXml,
    wchar_t *strAllUserProfileSecurity,
    BOOL bOverwrite,
    LPDWORD pdwReasonCode)
{
    PWLANSVC_INTERFACE iface;
    DWORD dwResult;

    UNREFERENCED_PARAMETER(strAllUserProfileSecurity);

    if (pdwReasonCode != NULL)
        *pdwReasonCode = WLAN_REASON_CODE_SUCCESS;

    if (strProfileXml == NULL)
        return ERROR_INVALID_PARAMETER;

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(pInterfaceGuid);
    if (iface == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_FOUND;
    }

    dwResult = WlanSvcSetProfile(iface, dwFlags, strProfileXml, bOverwrite,
                                 pdwReasonCode);
    LeaveCriticalSection(&WlanSvcLock);
    return dwResult;
}

DWORD _RpcGetProfile(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    wchar_t *strProfileName,
    wchar_t **pstrProfileXml,
    LPDWORD pdwFlags,
    LPDWORD pdwGrantedAccess)
{
    PWLANSVC_INTERFACE iface;
    DWORD dwResult;
    BOOL bPrivileged;

    *pstrProfileXml = NULL;

    if (strProfileName == NULL)
        return ERROR_INVALID_PARAMETER;

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    /* The cleartext <keyMaterial> PSK is only returned to privileged callers. */
    bPrivileged = WlanSvcClientIsPrivileged();

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(pInterfaceGuid);
    if (iface == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_FOUND;
    }

    dwResult = WlanSvcGetProfile(iface, strProfileName, bPrivileged, pstrProfileXml, pdwFlags);
    LeaveCriticalSection(&WlanSvcLock);

    if (dwResult == ERROR_SUCCESS && pdwGrantedAccess != NULL)
    {
        *pdwGrantedAccess = WLAN_READ_ACCESS | WLAN_EXECUTE_ACCESS;
        if (bPrivileged)
            *pdwGrantedAccess |= WLAN_WRITE_ACCESS;
    }

    return dwResult;
}

DWORD _RpcDeleteProfile(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    const wchar_t *strProfileName)
{
    PWLANSVC_INTERFACE iface;
    DWORD dwResult;

    if (strProfileName == NULL)
        return ERROR_INVALID_PARAMETER;

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(pInterfaceGuid);
    if (iface == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_FOUND;
    }

    dwResult = WlanSvcDeleteProfile(iface, strProfileName);
    LeaveCriticalSection(&WlanSvcLock);
    return dwResult;
}

DWORD _RpcRenameProfile(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    const wchar_t *strOldProfileName,
    const wchar_t *strNewProfileName)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcSetProfileList(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    DWORD dwItems,
    BYTE **strProfileNames)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcGetProfileList(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    PWLAN_PROFILE_INFO_LIST *ppProfileList)
{
    PWLANSVC_INTERFACE iface;
    PWLAN_PROFILE_INFO_LIST list;
    PLIST_ENTRY entry;
    DWORD count, idx;
    SIZE_T size;

    *ppProfileList = NULL;

    if (!WlanSvcValidateHandle(hClientHandle))
        return ERROR_INVALID_HANDLE;

    EnterCriticalSection(&WlanSvcLock);
    iface = WlanSvcFindInterface(pInterfaceGuid);
    if (iface == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_FOUND;
    }

    count = iface->ProfileCount;
    size = FIELD_OFFSET(WLAN_PROFILE_INFO_LIST, ProfileInfo) +
           (SIZE_T)(count == 0 ? 1 : count) * sizeof(WLAN_PROFILE_INFO);

    list = midl_user_allocate(size);
    if (list == NULL)
    {
        LeaveCriticalSection(&WlanSvcLock);
        return ERROR_NOT_ENOUGH_MEMORY;
    }
    ZeroMemory(list, size);

    idx = 0;
    for (entry = iface->ProfileListHead.Flink;
         entry != &iface->ProfileListHead;
         entry = entry->Flink)
    {
        PWLANSVC_PROFILE prof =
            CONTAINING_RECORD(entry, WLANSVC_PROFILE, ListEntry);
        wcsncpy(list->ProfileInfo[idx].strProfileName, prof->Name, 255);
        list->ProfileInfo[idx].dwFlags = prof->Flags;
        idx++;
    }

    list->dwNumberOfItems = count;
    list->dwIndex = 0;
    LeaveCriticalSection(&WlanSvcLock);

    *ppProfileList = list;
    return ERROR_SUCCESS;
}

DWORD _RpcSetProfilePosition(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    wchar_t *strProfileName,
    DWORD dwPosition)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcSetProfileCustomUserData(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    wchar_t *strProfileName,
    DWORD dwDataSize,
    LPBYTE pData)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcGetProfileCustomUserData(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    wchar_t *strProfileName,
    LPDWORD dwDataSize,
    LPBYTE *pData)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcSetFilterList(
    WLANSVC_RPC_HANDLE hClientHandle,
    short wlanFilterListType,
    PDOT11_NETWORK_LIST pNetworkList)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcGetFilterList(
    WLANSVC_RPC_HANDLE hClientHandle,
    short wlanFilterListType,
    PDOT11_NETWORK_LIST *pNetworkList)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcSetPsdIEDataList(
    WLANSVC_RPC_HANDLE hClientHandle,
    wchar_t *strFormat,
    DWORD dwDataListSize,
    LPBYTE pPsdIEDataList)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcSaveTemporaryProfile(
    WLANSVC_RPC_HANDLE hClientHandle,
    const GUID *pInterfaceGuid,
    wchar_t *strProfileName,
    wchar_t *strAllUserProfileSecurity,
    DWORD dwFlags,
    BOOL bOverWrite)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcIsUIRequestPending(
    wchar_t *arg_1,
    const GUID *pInterfaceGuid,
    struct_C *arg_3,
    LPDWORD arg_4)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcSetUIForwardingNetworkList(
    wchar_t *arg_1,
    GUID *arg_2,
    DWORD dwSize,
    GUID *arg_4)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcIsNetworkSuppressed(
    wchar_t *arg_1,
    DWORD arg_2,
    const GUID *pInterfaceGuid,
    LPDWORD arg_4)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcRemoveUIForwardingNetworkList(
    wchar_t *arg_1,
    const GUID *pInterfaceGuid)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcQueryExtUIRequest(
    wchar_t *arg_1,
    GUID *arg_2,
    GUID *arg_3,
    short arg_4,
    GUID *pInterfaceGuid,
    struct_C **arg_6)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcUIResponse(
    wchar_t *arg_1,
    struct_C *arg_2,
    struct_D *arg_3)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcGetProfileKeyInfo(
    wchar_t *arg_1,
    DWORD arg_2,
    const GUID *pInterfaceGuid,
    wchar_t *arg_4,
    DWORD arg_5,
    LPDWORD arg_6,
    char *arg_7,
    LPDWORD arg_8)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcAsyncDoPlap(
    wchar_t *arg_1,
    const GUID *pInterfaceGuid,
    wchar_t *arg_3,
    DWORD dwSize,
    struct_E arg_5[])
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcQueryPlapCredentials(
    wchar_t *arg_1,
    LPDWORD dwSize,
    struct_E **arg_3,
    wchar_t **arg_4,
    GUID *pInterfaceGuid,
    LPDWORD arg_6,
    LPDWORD arg_7,
    LPDWORD arg_8,
    LPDWORD arg_9)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcCancelPlap(
    wchar_t *arg_1,
    const GUID *pInterfaceGuid)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcSetSecuritySettings(
    WLANSVC_RPC_HANDLE hClientHandle,
    WLAN_SECURABLE_OBJECT SecurableObject,
    const wchar_t *strModifiedSDDL)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

DWORD _RpcGetSecuritySettings(
    WLANSVC_RPC_HANDLE hClientHandle,
    WLAN_SECURABLE_OBJECT SecurableObject,
    WLAN_OPCODE_VALUE_TYPE *pValueType,
    wchar_t **pstrCurrentSDDL,
    LPDWORD pdwGrantedAccess)
{
    UNIMPLEMENTED;
    return ERROR_CALL_NOT_IMPLEMENTED;
}

void __RPC_FAR * __RPC_USER midl_user_allocate(SIZE_T len)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len);
}


void __RPC_USER midl_user_free(void __RPC_FAR * ptr)
{
    HeapFree(GetProcessHeap(), 0, ptr);
}


void __RPC_USER WLANSVC_RPC_HANDLE_rundown(WLANSVC_RPC_HANDLE hClientHandle)
{
}
