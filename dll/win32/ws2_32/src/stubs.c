/*
 * WS2_32 Unimplemented API Stubs
 * Copyright 2024 ReactOS Team
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include "../inc/ws2_32.h"

#define NDEBUG
#include <debug.h>

/* Stub macro for unimplemented functions */
#define STUB_FUNC(name) \
    DPRINT1("%s: stub\n", #name); \
    WSASetLastError(WSAEOPNOTSUPP); \
    return SOCKET_ERROR;

/* Windows Vista+ Extended Address Info Functions */
INT WSAAPI FreeAddrInfoEx(PADDRINFOEXA pAddrInfoEx)
{
    STUB_FUNC(FreeAddrInfoEx);
}

INT WSAAPI FreeAddrInfoExW(PADDRINFOEXW pAddrInfoEx)
{
    STUB_FUNC(FreeAddrInfoExW);
}

INT WSAAPI GetAddrInfoExA(PCSTR pName, PCSTR pServiceName, DWORD dwNameSpace,
    LPGUID lpNspId, const ADDRINFOEXA *hints, PADDRINFOEXA *ppResult,
    struct timeval *timeout, LPOVERLAPPED lpOverlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE lpCompletionRoutine,
    LPHANDLE lpNameHandle)
{
    STUB_FUNC(GetAddrInfoExA);
}

INT WSAAPI GetAddrInfoExW(PCWSTR pName, PCWSTR pServiceName, DWORD dwNameSpace,
    LPGUID lpNspId, const ADDRINFOEXW *hints, PADDRINFOEXW *ppResult,
    struct timeval *timeout, LPOVERLAPPED lpOverlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE lpCompletionRoutine,
    LPHANDLE lpNameHandle)
{
    STUB_FUNC(GetAddrInfoExW);
}

INT WSAAPI SetAddrInfoExA(PCSTR pName, PCSTR pServiceName, SOCKET_ADDRESS *pAddresses,
    DWORD dwAddressCount, LPBLOB lpBlob, DWORD dwFlags, DWORD dwNameSpace,
    LPGUID lpNspId, struct timeval *timeout, LPOVERLAPPED lpOverlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE lpCompletionRoutine,
    LPHANDLE lpNameHandle)
{
    STUB_FUNC(SetAddrInfoExA);
}

INT WSAAPI SetAddrInfoExW(PCWSTR pName, PCWSTR pServiceName, SOCKET_ADDRESS *pAddresses,
    DWORD dwAddressCount, LPBLOB lpBlob, DWORD dwFlags, DWORD dwNameSpace,
    LPGUID lpNspId, struct timeval *timeout, LPOVERLAPPED lpOverlapped,
    LPLOOKUPSERVICE_COMPLETION_ROUTINE lpCompletionRoutine,
    LPHANDLE lpNameHandle)
{
    STUB_FUNC(SetAddrInfoExW);
}

/* Vista+ Provider Functions */
INT WSAAPI WSAAdvertiseProvider(const GUID *puuidProviderId, const LPNSPV2_ROUTINE pNSPv2ProviderInfo)
{
    STUB_FUNC(WSAAdvertiseProvider);
}

INT WSAAPI WSAUnadvertiseProvider(const GUID *puuidProviderId)
{
    STUB_FUNC(WSAUnadvertiseProvider);
}

INT WSAAPI WSAProviderCompleteAsyncCall(HANDLE hAsyncCall, INT iRetCode)
{
    STUB_FUNC(WSAProviderCompleteAsyncCall);
}

/* Vista+ Connection Functions */
INT WSAAPI WSAConnectByList(SOCKET s, PSOCKET_ADDRESS_LIST SocketAddress,
    LPDWORD LocalAddressLength, LPSOCKADDR LocalAddress,
    LPDWORD RemoteAddressLength, LPSOCKADDR RemoteAddress,
    const struct timeval *timeout, LPWSAOVERLAPPED Reserved)
{
    STUB_FUNC(WSAConnectByList);
}

BOOL WSAAPI WSAConnectByNameA(SOCKET s, LPCSTR nodename, LPCSTR servicename,
    LPDWORD LocalAddressLength, LPSOCKADDR LocalAddress,
    LPDWORD RemoteAddressLength, LPSOCKADDR RemoteAddress,
    const struct timeval *timeout, LPWSAOVERLAPPED Reserved)
{
    DPRINT1("WSAConnectByNameA: stub\n");
    WSASetLastError(WSAEOPNOTSUPP);
    return FALSE;
}

BOOL WSAAPI WSAConnectByNameW(SOCKET s, LPCWSTR nodename, LPCWSTR servicename,
    LPDWORD LocalAddressLength, LPSOCKADDR LocalAddress,
    LPDWORD RemoteAddressLength, LPSOCKADDR RemoteAddress,
    const struct timeval *timeout, LPWSAOVERLAPPED Reserved)
{
    DPRINT1("WSAConnectByNameW: stub\n");
    WSASetLastError(WSAEOPNOTSUPP);
    return FALSE;
}

/* Vista+ Namespace Provider Functions */
INT WSAAPI WSAEnumNameSpaceProvidersExA(LPDWORD lpdwBufferLength, LPWSANAMESPACE_INFOEXA lpnspBuffer)
{
    STUB_FUNC(WSAEnumNameSpaceProvidersExA);
}

INT WSAAPI WSAEnumNameSpaceProvidersExW(LPDWORD lpdwBufferLength, LPWSANAMESPACE_INFOEXW lpnspBuffer)
{
    STUB_FUNC(WSAEnumNameSpaceProvidersExW);
}

/* Vista+ Poll Function */
INT WSAAPI WSAPoll(LPWSAPOLLFD fdArray, ULONG fds, INT timeout)
{
    STUB_FUNC(WSAPoll);
}

/* Vista+ Message Functions */
INT WSAAPI WSASendMsg(SOCKET s, LPWSAMSG lpMsg, DWORD dwFlags,
    LPDWORD lpNumberOfBytesSent, LPWSAOVERLAPPED lpOverlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    STUB_FUNC(WSASendMsg);
}

INT WSAAPI WSARecvMsg(SOCKET s, LPWSAMSG lpMsg, LPDWORD lpNumberOfBytesRecvd,
    LPWSAOVERLAPPED lpOverlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE lpCompletionRoutine)
{
    STUB_FUNC(WSARecvMsg);
}

/* Vista+ Security Functions */
INT WSAAPI WSASetSocketSecurity(SOCKET s, PVOID SecuritySettings,
    ULONG SecuritySettingsLen, LPWSAOVERLAPPED Overlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine)
{
    STUB_FUNC(WSASetSocketSecurity);
}

INT WSAAPI WSAQuerySocketSecurity(SOCKET s, PVOID SecurityQueryTemplate,
    ULONG SecurityQueryTemplateLen, PVOID SecurityQueryInfo,
    ULONG *SecurityQueryInfoLen, LPWSAOVERLAPPED Overlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine)
{
    STUB_FUNC(WSAQuerySocketSecurity);
}

INT WSAAPI WSASetSocketPeerTargetName(SOCKET s, PVOID PeerTargetName)
{
    STUB_FUNC(WSASetSocketPeerTargetName);
}

INT WSAAPI WSAImpersonateSocketPeer(SOCKET s, const struct sockaddr *PeerAddr, ULONG PeerAddrLen)
{
    STUB_FUNC(WSAImpersonateSocketPeer);
}

INT WSAAPI WSARevertImpersonation(VOID)
{
    STUB_FUNC(WSARevertImpersonation);
}

/* Vista+ Address/String Conversion */
INT WSAAPI WSAAddressToStringA(LPSOCKADDR lpsaAddress, DWORD dwAddressLength,
    LPWSAPROTOCOL_INFOA lpProtocolInfo, LPSTR lpszAddressString,
    LPDWORD lpdwAddressStringLength)
{
    STUB_FUNC(WSAAddressToStringA);
}

INT WSAAPI WSAAddressToStringW(LPSOCKADDR lpsaAddress, DWORD dwAddressLength,
    LPWSAPROTOCOL_INFOW lpProtocolInfo, LPWSTR lpszAddressString,
    LPDWORD lpdwAddressStringLength)
{
    STUB_FUNC(WSAAddressToStringW);
}

INT WSAAPI WSAStringToAddressA(LPSTR AddressString, INT AddressFamily,
    LPWSAPROTOCOL_INFOA lpProtocolInfo, LPSOCKADDR lpAddress,
    LPINT lpAddressLength)
{
    STUB_FUNC(WSAStringToAddressA);
}

INT WSAAPI WSAStringToAddressW(LPWSTR AddressString, INT AddressFamily,
    LPWSAPROTOCOL_INFOW lpProtocolInfo, LPSOCKADDR lpAddress,
    LPINT lpAddressLength)
{
    STUB_FUNC(WSAStringToAddressW);
}

/* Vista+ NSP I/O Control */
INT WSAAPI WSANSPIoctl(HANDLE hLookup, DWORD dwControlCode, LPVOID lpvInBuffer,
    DWORD cbInBuffer, LPVOID lpvOutBuffer, DWORD cbOutBuffer,
    LPDWORD lpcbBytesReturned, LPWSACOMPLETION lpCompletion)
{
    STUB_FUNC(WSANSPIoctl);
}

/* Vista+ QOS Functions */
INT WSAAPI WSAGetQOSByName(SOCKET s, LPWSABUF lpQOSName, LPQOS lpQOS)
{
    STUB_FUNC(WSAGetQOSByName);
}

/* Vista+ Service Functions */
INT WSAAPI WSASetServiceA(LPWSAQUERYSETA lpqsRegInfo, WSAESETSERVICEOP essoperation, DWORD dwControlFlags)
{
    STUB_FUNC(WSASetServiceA);
}

INT WSAAPI WSASetServiceW(LPWSAQUERYSETW lpqsRegInfo, WSAESETSERVICEOP essoperation, DWORD dwControlFlags)
{
    STUB_FUNC(WSASetServiceW);
}

INT WSAAPI WSAGetServiceClassInfoA(LPGUID lpProviderId, LPGUID lpServiceClassId,
    LPDWORD lpdwBufSize, LPWSASERVICECLASSINFOA lpServiceClassInfo)
{
    STUB_FUNC(WSAGetServiceClassInfoA);
}

INT WSAAPI WSAGetServiceClassInfoW(LPGUID lpProviderId, LPGUID lpServiceClassId,
    LPDWORD lpdwBufSize, LPWSASERVICECLASSINFOW lpServiceClassInfo)
{
    STUB_FUNC(WSAGetServiceClassInfoW);
}

INT WSAAPI WSAGetServiceClassNameByClassIdA(LPGUID lpServiceClassId, LPSTR lpszServiceClassName,
    LPDWORD lpdwBufferLength)
{
    STUB_FUNC(WSAGetServiceClassNameByClassIdA);
}

INT WSAAPI WSAGetServiceClassNameByClassIdW(LPGUID lpServiceClassId, LPWSTR lpszServiceClassName,
    LPDWORD lpdwBufferLength)
{
    STUB_FUNC(WSAGetServiceClassNameByClassIdW);
}

/* WSC 32-bit Provider Functions (64-bit Windows only) */
#ifdef _WIN64
INT WSAAPI WSCDeinstallProvider32(LPGUID lpProviderId, LPINT lpErrno)
{
    STUB_FUNC(WSCDeinstallProvider32);
}

INT WSAAPI WSCEnableNSProvider32(LPGUID lpProviderId, BOOL fEnable)
{
    STUB_FUNC(WSCEnableNSProvider32);
}

INT WSAAPI WSCEnumNameSpaceProviders32(LPDWORD lpdwBufferLength, LPWSANAMESPACE_INFOW lpnspBuffer)
{
    STUB_FUNC(WSCEnumNameSpaceProviders32);
}

INT WSAAPI WSCEnumNameSpaceProvidersEx32(LPDWORD lpdwBufferLength, LPWSANAMESPACE_INFOEXW lpnspBuffer)
{
    STUB_FUNC(WSCEnumNameSpaceProvidersEx32);
}

INT WSAAPI WSCEnumProtocols32(LPINT lpiProtocols, LPWSAPROTOCOL_INFOW lpProtocolBuffer,
    LPDWORD lpdwBufferLength, LPINT lpErrno)
{
    STUB_FUNC(WSCEnumProtocols32);
}

INT WSAAPI WSCGetProviderPath32(LPGUID lpProviderId, LPWSTR lpszProviderDllPath,
    LPINT lpProviderDllPathLen, LPINT lpErrno)
{
    STUB_FUNC(WSCGetProviderPath32);
}

INT WSAAPI WSCInstallNameSpace32(LPWSTR lpszIdentifier, LPWSTR lpszPathName,
    DWORD dwNameSpace, DWORD dwVersion, LPGUID lpProviderId)
{
    STUB_FUNC(WSCInstallNameSpace32);
}

INT WSAAPI WSCInstallNameSpaceEx32(LPWSTR lpszIdentifier, LPWSTR lpszPathName,
    DWORD dwNameSpace, DWORD dwVersion, LPGUID lpProviderId, LPBLOB lpProviderInfo)
{
    STUB_FUNC(WSCInstallNameSpaceEx32);
}

INT WSAAPI WSCInstallProvider64_32(LPGUID lpProviderId, const WCHAR *lpszProviderDllPath,
    const LPWSAPROTOCOL_INFOW lpProtocolInfoList, DWORD dwNumberOfEntries, LPINT lpErrno)
{
    STUB_FUNC(WSCInstallProvider64_32);
}

INT WSAAPI WSCInstallProviderAndChains64_32(LPGUID lpProviderId, const LPWSTR lpszProviderDllPath,
    const LPWSTR lpszLspName, LPWSTR lpszProtocolList, DWORD dwServiceFlags, 
    LPWSAPROTOCOL_INFOW lpProtocolInfoList, DWORD dwNumberOfEntries, LPDWORD lpdwCatalogEntryId, LPINT lpErrno)
{
    STUB_FUNC(WSCInstallProviderAndChains64_32);
}

INT WSAAPI WSCGetProviderInfo32(LPGUID lpProviderId, WSC_PROVIDER_INFO_TYPE InfoType,
    PBYTE Info, size_t *InfoSize, DWORD Flags, LPINT lpErrno)
{
    STUB_FUNC(WSCGetProviderInfo32);
}

INT WSAAPI WSCSetProviderInfo32(LPGUID lpProviderId, WSC_PROVIDER_INFO_TYPE InfoType,
    PBYTE Info, size_t InfoSize, DWORD Flags, LPINT lpErrno)
{
    STUB_FUNC(WSCSetProviderInfo32);
}

INT WSAAPI WSCUnInstallNameSpace32(LPGUID lpProviderId)
{
    STUB_FUNC(WSCUnInstallNameSpace32);
}

INT WSAAPI WSCUpdateProvider32(LPGUID lpProviderId, const WCHAR *lpszProviderDllPath,
    const LPWSAPROTOCOL_INFOW lpProtocolInfoList, DWORD dwNumberOfEntries, LPINT lpErrno)
{
    STUB_FUNC(WSCUpdateProvider32);
}

INT WSAAPI WSCWriteNameSpaceOrder32(LPGUID lpProviderId, DWORD dwNumberOfEntries)
{
    STUB_FUNC(WSCWriteNameSpaceOrder32);
}

INT WSAAPI WSCWriteProviderOrder32(LPDWORD lpwdCatalogEntryId, DWORD dwNumberOfEntries)
{
    STUB_FUNC(WSCWriteProviderOrder32);
}
#endif /* _WIN64 */

/* Vista+ Application Category Functions */
INT WSAAPI WSCGetApplicationCategory(LPCWSTR lpApplicationPath, DWORD dwApplicationPathLength,
    LPCWSTR lpApplicationName, DWORD dwApplicationNameLength,
    LPDWORD pdwPermittedLspCategories, LPINT lpErrno)
{
    STUB_FUNC(WSCGetApplicationCategory);
}

INT WSAAPI WSCSetApplicationCategory(LPCWSTR lpApplicationPath, DWORD dwApplicationPathLength,
    LPCWSTR lpApplicationName, DWORD dwApplicationNameLength,
    DWORD dwPermittedLspCategories, LPDWORD pReserved, LPINT lpErrno)
{
    STUB_FUNC(WSCSetApplicationCategory);
}

/* Vista+ Provider Info Functions */
INT WSAAPI WSCGetProviderInfo(LPGUID lpProviderId, WSC_PROVIDER_INFO_TYPE InfoType,
    PBYTE Info, size_t *InfoSize, DWORD Flags, LPINT lpErrno)
{
    STUB_FUNC(WSCGetProviderInfo);
}

INT WSAAPI WSCSetProviderInfo(LPGUID lpProviderId, WSC_PROVIDER_INFO_TYPE InfoType,
    PBYTE Info, size_t InfoSize, DWORD Flags, LPINT lpErrno)
{
    STUB_FUNC(WSCSetProviderInfo);
}

/* Vista+ Namespace Functions */
INT WSAAPI WSCInstallNameSpaceEx(LPWSTR lpszIdentifier, LPWSTR lpszPathName,
    DWORD dwNameSpace, DWORD dwVersion, LPGUID lpProviderId, LPBLOB lpProviderInfo)
{
    STUB_FUNC(WSCInstallNameSpaceEx);
}

/* Vista+ LSP Event Function */
INT WSAAPI WahWriteLSPEvent(DWORD EventType, PVOID EventData)
{
    STUB_FUNC(WahWriteLSPEvent);
}

/* Duplicate Socket Functions */
INT WSAAPI WSADuplicateSocketA(SOCKET s, DWORD dwProcessId, LPWSAPROTOCOL_INFOA lpProtocolInfo)
{
    STUB_FUNC(WSADuplicateSocketA);
}

INT WSAAPI WSADuplicateSocketW(SOCKET s, DWORD dwProcessId, LPWSAPROTOCOL_INFOW lpProtocolInfo)
{
    STUB_FUNC(WSADuplicateSocketW);
}

/* Overlapped Result Function */
BOOL WSAAPI WSAGetOverlappedResult(SOCKET s, LPWSAOVERLAPPED lpOverlapped,
    LPDWORD lpcbTransfer, BOOL fWait, LPDWORD lpdwFlags)
{
    DPRINT1("WSAGetOverlappedResult: stub\n");
    WSASetLastError(WSAEOPNOTSUPP);
    return FALSE;
}

/* EOF */