/*
 * WS2_32 Simple Stubs
 * Copyright 2024 ReactOS Team
 */

#include "../inc/ws2_32.h"

#define NDEBUG
#include <debug.h>

/* Macro for simple stub functions */
#define STUB_FUNC_INT(name) \
    DPRINT1("%s: stub\n", #name); \
    WSASetLastError(WSAEOPNOTSUPP); \
    return SOCKET_ERROR;

#define STUB_FUNC_BOOL(name) \
    DPRINT1("%s: stub\n", #name); \
    WSASetLastError(WSAEOPNOTSUPP); \
    return FALSE;

/* Only implement the truly missing functions that are causing link errors */

INT WSAAPI FreeAddrInfoEx(PADDRINFOEXA pAddrInfoEx)
{
    STUB_FUNC_INT(FreeAddrInfoEx);
}

INT WSAAPI FreeAddrInfoExW(PADDRINFOEXW pAddrInfoEx)
{
    STUB_FUNC_INT(FreeAddrInfoExW);
}

INT WSAAPI GetAddrInfoExA(PCSTR pName, PCSTR pServiceName, DWORD dwNameSpace,
    LPGUID lpNspId, const ADDRINFOEXA *hints, PADDRINFOEXA *ppResult, 
    struct timeval *timeout, LPOVERLAPPED lpOverlapped, 
    LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine, LPHANDLE lpNameHandle)
{
    STUB_FUNC_INT(GetAddrInfoExA);
}

INT WSAAPI GetAddrInfoExW(PCWSTR pName, PCWSTR pServiceName, DWORD dwNameSpace,
    LPGUID lpNspId, const ADDRINFOEXW *hints, PADDRINFOEXW *ppResult,
    struct timeval *timeout, LPOVERLAPPED lpOverlapped,
    LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine, LPHANDLE lpNameHandle)
{
    STUB_FUNC_INT(GetAddrInfoExW);
}

INT WSAAPI SetAddrInfoExA(PCSTR pName, PCSTR pServiceName, SOCKET_ADDRESS *pAddresses,
    DWORD dwAddressCount, LPBLOB lpBlob, DWORD dwFlags, DWORD dwNameSpace,
    LPGUID lpNspId, struct timeval *timeout, LPOVERLAPPED lpOverlapped,
    LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine, LPHANDLE lpNameHandle)
{
    STUB_FUNC_INT(SetAddrInfoExA);
}

INT WSAAPI SetAddrInfoExW(PCWSTR pName, PCWSTR pServiceName, SOCKET_ADDRESS *pAddresses,
    DWORD dwAddressCount, LPBLOB lpBlob, DWORD dwFlags, DWORD dwNameSpace,
    LPGUID lpNspId, struct timeval *timeout, LPOVERLAPPED lpOverlapped,
    LPOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine, LPHANDLE lpNameHandle)
{
    STUB_FUNC_INT(SetAddrInfoExW);
}

INT WSAAPI WSAAdvertiseProvider(const GUID *puuidProviderId, const NSPV2_ROUTINE *const pNSPv2ProviderInfo)
{
    STUB_FUNC_INT(WSAAdvertiseProvider);
}

INT WSAAPI WSAUnadvertiseProvider(const GUID *puuidProviderId)
{
    STUB_FUNC_INT(WSAUnadvertiseProvider);
}

INT WSAAPI WSAProviderCompleteAsyncCall(HANDLE hAsyncCall, INT iRetCode)
{
    STUB_FUNC_INT(WSAProviderCompleteAsyncCall);
}

INT WSAAPI WSAConnectByList(SOCKET s, PSOCKET_ADDRESS_LIST SocketAddress, 
    LPDWORD LocalAddressLength, LPSOCKADDR LocalAddress,
    LPDWORD RemoteAddressLength, LPSOCKADDR RemoteAddress,
    const struct timeval *timeout, LPWSAOVERLAPPED Reserved)
{
    STUB_FUNC_INT(WSAConnectByList);
}

BOOL WSAAPI WSAConnectByNameA(SOCKET s, LPCSTR nodename, LPCSTR servicename,
    LPDWORD LocalAddressLength, LPSOCKADDR LocalAddress,
    LPDWORD RemoteAddressLength, LPSOCKADDR RemoteAddress,
    const struct timeval *timeout, LPWSAOVERLAPPED Reserved)
{
    STUB_FUNC_BOOL(WSAConnectByNameA);
}

BOOL WSAAPI WSAConnectByNameW(SOCKET s, LPWSTR nodename, LPWSTR servicename,
    LPDWORD LocalAddressLength, LPSOCKADDR LocalAddress,
    LPDWORD RemoteAddressLength, LPSOCKADDR RemoteAddress,
    const struct timeval *timeout, LPWSAOVERLAPPED Reserved)
{
    STUB_FUNC_BOOL(WSAConnectByNameW);
}

INT WSAAPI WSAEnumNameSpaceProvidersExA(LPDWORD lpdwBufferLength, LPWSANAMESPACE_INFOEXA lpnspBuffer)
{
    STUB_FUNC_INT(WSAEnumNameSpaceProvidersExA);
}

INT WSAAPI WSAEnumNameSpaceProvidersExW(LPDWORD lpdwBufferLength, LPWSANAMESPACE_INFOEXW lpnspBuffer)
{
    STUB_FUNC_INT(WSAEnumNameSpaceProvidersExW);
}

INT WSAAPI WSAPoll(LPWSAPOLLFD fdArray, ULONG fds, INT timeout)
{
    STUB_FUNC_INT(WSAPoll);
}

INT WSAAPI WSASendMsg(SOCKET s, LPWSAMSG lpMsg, DWORD dwFlags, LPDWORD lpNumberOfBytesSent,
    LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    STUB_FUNC_INT(WSASendMsg);
}

INT WSAAPI WSARecvMsg(SOCKET s, LPWSAMSG lpMsg, LPDWORD lpNumberOfBytesRecvd,
    LPWSAOVERLAPPED lpOverlapped, LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    STUB_FUNC_INT(WSARecvMsg);
}

INT WSAAPI WSASetSocketSecurity(SOCKET s, PVOID SecuritySettings, 
    ULONG SecuritySettingsLen, LPWSAOVERLAPPED Overlapped, 
    LPWSAOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine)
{
    STUB_FUNC_INT(WSASetSocketSecurity);
}

INT WSAAPI WSAQuerySocketSecurity(SOCKET s, PVOID SecurityQueryTemplate,
    ULONG SecurityQueryTemplateLen, PVOID SecurityQueryInfo, 
    ULONG *SecurityQueryInfoLen, LPWSAOVERLAPPED Overlapped, 
    LPWSAOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine)
{
    STUB_FUNC_INT(WSAQuerySocketSecurity);
}

INT WSAAPI WSASetSocketPeerTargetName(SOCKET s, 
    PVOID PeerTargetName)
{
    STUB_FUNC_INT(WSASetSocketPeerTargetName);
}

INT WSAAPI WSAImpersonateSocketPeer(SOCKET s, const struct sockaddr *PeerAddr, ULONG PeerAddrLen)
{
    STUB_FUNC_INT(WSAImpersonateSocketPeer);
}

INT WSAAPI WSARevertImpersonation(VOID)
{
    STUB_FUNC_INT(WSARevertImpersonation);
}

/* Already implemented functions removed */

/* 64-bit only functions */
#ifdef _WIN64
INT WSAAPI WSCDeinstallProvider32(LPGUID lpProviderId, LPINT lpErrno)
{
    STUB_FUNC_INT(WSCDeinstallProvider32);
}

INT WSAAPI WSCEnableNSProvider32(LPGUID lpProviderId, BOOL fEnable)
{
    STUB_FUNC_INT(WSCEnableNSProvider32);
}

INT WSAAPI WSCEnumNameSpaceProviders32(LPDWORD lpdwBufferLength, LPWSANAMESPACE_INFOW lpnspBuffer)
{
    STUB_FUNC_INT(WSCEnumNameSpaceProviders32);
}

INT WSAAPI WSCEnumNameSpaceProvidersEx32(LPDWORD lpdwBufferLength, LPWSANAMESPACE_INFOEXW lpnspBuffer)
{
    STUB_FUNC_INT(WSCEnumNameSpaceProvidersEx32);
}

INT WSAAPI WSCEnumProtocols32(LPINT lpiProtocols, LPWSAPROTOCOL_INFOW lpProtocolBuffer,
    LPDWORD lpdwBufferLength, LPINT lpErrno)
{
    STUB_FUNC_INT(WSCEnumProtocols32);
}

INT WSAAPI WSCGetProviderPath32(LPGUID lpProviderId, LPWSTR lpszProviderDllPath,
    LPINT lpProviderDllPathLen, LPINT lpErrno)
{
    STUB_FUNC_INT(WSCGetProviderPath32);
}

INT WSAAPI WSCInstallNameSpace32(LPWSTR lpszIdentifier, LPWSTR lpszPathName,
    DWORD dwNameSpace, DWORD dwVersion, LPGUID lpProviderId)
{
    STUB_FUNC_INT(WSCInstallNameSpace32);
}

INT WSAAPI WSCInstallNameSpaceEx32(LPWSTR lpszIdentifier, LPWSTR lpszPathName,
    DWORD dwNameSpace, DWORD dwVersion, LPGUID lpProviderId, LPBLOB lpProviderInfo)
{
    STUB_FUNC_INT(WSCInstallNameSpaceEx32);
}

INT WSAAPI WSCInstallProvider64_32(LPGUID lpProviderId, const WCHAR *lpszProviderDllPath,
    const LPWSAPROTOCOL_INFOW lpProtocolInfoList, DWORD dwNumberOfEntries, LPINT lpErrno)
{
    STUB_FUNC_INT(WSCInstallProvider64_32);
}

INT WSAAPI WSCInstallProviderAndChains64_32(LPGUID lpProviderId, const LPWSTR lpszProviderDllPath,
    const LPWSTR lpszLspName, LPWSTR lpszProtocolList, DWORD dwServiceFlags, 
    LPWSAPROTOCOL_INFOW lpProtocolInfoList, DWORD dwNumberOfEntries, LPDWORD lpdwCatalogEntryId, LPINT lpErrno)
{
    STUB_FUNC_INT(WSCInstallProviderAndChains64_32);
}

/* WSCGetProviderInfo32 already implemented in spinstal.c */

INT WSAAPI WSCSetProviderInfo32(LPGUID lpProviderId, WSC_PROVIDER_INFO_TYPE InfoType,
    PBYTE Info, size_t InfoSize, DWORD Flags, LPINT lpErrno)
{
    STUB_FUNC_INT(WSCSetProviderInfo32);
}

INT WSAAPI WSCUnInstallNameSpace32(LPGUID lpProviderId)
{
    STUB_FUNC_INT(WSCUnInstallNameSpace32);
}

INT WSAAPI WSCUpdateProvider32(LPGUID lpProviderId, const WCHAR *lpszProviderDllPath,
    const LPWSAPROTOCOL_INFOW lpProtocolInfoList, DWORD dwNumberOfEntries, LPINT lpErrno)
{
    STUB_FUNC_INT(WSCUpdateProvider32);
}

INT WSAAPI WSCWriteNameSpaceOrder32(LPGUID lpProviderId, DWORD dwNumberOfEntries)
{
    STUB_FUNC_INT(WSCWriteNameSpaceOrder32);
}

INT WSAAPI WSCWriteProviderOrder32(LPDWORD lpwdCatalogEntryId, DWORD dwNumberOfEntries)
{
    STUB_FUNC_INT(WSCWriteProviderOrder32);
}
#endif /* _WIN64 */

INT WSAAPI WSCGetApplicationCategory(LPCWSTR lpApplicationPath, DWORD dwApplicationPathLength,
    LPCWSTR lpApplicationName, DWORD dwApplicationNameLength,
    LPDWORD pdwPermittedLspCategories, LPINT lpErrno)
{
    STUB_FUNC_INT(WSCGetApplicationCategory);
}

/* WSCSetApplicationCategory and WSCGetProviderInfo already implemented in spinstal.c */

INT WSAAPI WSCSetProviderInfo(LPGUID lpProviderId, WSC_PROVIDER_INFO_TYPE InfoType,
    PBYTE Info, size_t InfoSize, DWORD Flags, LPINT lpErrno)
{
    STUB_FUNC_INT(WSCSetProviderInfo);
}

INT WSAAPI WSCInstallNameSpaceEx(LPWSTR lpszIdentifier, LPWSTR lpszPathName,
    DWORD dwNameSpace, DWORD dwVersion, LPGUID lpProviderId, LPBLOB lpProviderInfo)
{
    STUB_FUNC_INT(WSCInstallNameSpaceEx);
}

INT WSAAPI WahWriteLSPEvent(DWORD EventType, LPVOID EventData)
{
    STUB_FUNC_INT(WahWriteLSPEvent);
}

/* EOF */