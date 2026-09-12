/*
 * PROJECT:     ReactOS WinSock Helper DLL for AF_UNIX
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Local stream socket helper
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "wshunix.h"

#define NDEBUG
#include <debug.h>

BOOL
EXPORT
DllMain(HANDLE hInstDll, ULONG dwReason, PVOID Reserved)
{
    UNREFERENCED_PARAMETER(Reserved);

    if (dwReason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(hInstDll);

    return TRUE;
}

INT
EXPORT
WSHOpenSocket(
    IN OUT PINT AddressFamily,
    IN OUT PINT SocketType,
    IN OUT PINT Protocol,
    OUT PUNICODE_STRING TransportDeviceName,
    OUT PVOID HelperDllSocketContext,
    OUT PDWORD NotificationEvents)
{
    return WSHOpenSocket2(AddressFamily,
                          SocketType,
                          Protocol,
                          0,
                          0,
                          TransportDeviceName,
                          HelperDllSocketContext,
                          NotificationEvents);
}

INT
EXPORT
WSHOpenSocket2(
    IN OUT PINT AddressFamily,
    IN OUT PINT SocketType,
    IN OUT PINT Protocol,
    IN GROUP Group,
    IN DWORD Flags,
    OUT PUNICODE_STRING TransportDeviceName,
    OUT PVOID *HelperDllSocketContext,
    OUT PDWORD NotificationEvents)
{
    PUNIX_SOCKET_CONTEXT Context;
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(UNIX_DEVICE_NAME);

    UNREFERENCED_PARAMETER(Group);

    if (*AddressFamily != AF_UNIX)
        return WSAEAFNOSUPPORT;

    if (*SocketType != SOCK_STREAM)
        return WSAESOCKTNOSUPPORT;

    if (*Protocol != 0)
        return WSAEPROTONOSUPPORT;

    RtlInitUnicodeString(TransportDeviceName, NULL);

    TransportDeviceName->MaximumLength = DeviceName.Length + sizeof(UNICODE_NULL);
    TransportDeviceName->Buffer = HeapAlloc(GetProcessHeap(),
                                            HEAP_ZERO_MEMORY,
                                            TransportDeviceName->MaximumLength);
    if (!TransportDeviceName->Buffer)
        return WSAENOBUFS;

    RtlAppendUnicodeStringToString(TransportDeviceName, &DeviceName);

    Context = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Context));
    if (!Context)
    {
        HeapFree(GetProcessHeap(), 0, TransportDeviceName->Buffer);
        TransportDeviceName->Buffer = NULL;
        return WSAENOBUFS;
    }

    Context->AddressFamily = *AddressFamily;
    Context->SocketType = *SocketType;
    Context->Protocol = *Protocol;
    Context->Flags = Flags;

    *HelperDllSocketContext = Context;
    *NotificationEvents = WSH_NOTIFY_CLOSE;

    return NO_ERROR;
}

INT
EXPORT
WSHNotify(
    IN PVOID HelperDllSocketContext,
    IN SOCKET SocketHandle,
    IN HANDLE TdiAddressObjectHandle,
    IN HANDLE TdiConnectionObjectHandle,
    IN DWORD NotifyEvent)
{
    UNREFERENCED_PARAMETER(SocketHandle);
    UNREFERENCED_PARAMETER(TdiAddressObjectHandle);
    UNREFERENCED_PARAMETER(TdiConnectionObjectHandle);

    if (NotifyEvent == WSH_NOTIFY_CLOSE)
        HeapFree(GetProcessHeap(), 0, HelperDllSocketContext);

    return NO_ERROR;
}

INT
EXPORT
WSHGetSockaddrType(
    IN PSOCKADDR Sockaddr,
    IN DWORD SockaddrLength,
    OUT PSOCKADDR_INFO SockaddrInfo)
{
    PSOCKADDR_UN Local = (PSOCKADDR_UN)Sockaddr;

    if (!Local || !SockaddrInfo || SockaddrLength < sizeof(ADDRESS_FAMILY))
        return WSAEINVAL;

    if (Local->sun_family != AF_UNIX)
        return WSAEINVAL;

    if (SockaddrLength <= sizeof(ADDRESS_FAMILY) || Local->sun_path[0] == '\0')
    {
        SockaddrInfo->AddressInfo = SockaddrAddressInfoWildcard;
        SockaddrInfo->EndpointInfo = SockaddrEndpointInfoWildcard;
    }
    else
    {
        SockaddrInfo->AddressInfo = SockaddrAddressInfoNormal;
        SockaddrInfo->EndpointInfo = SockaddrEndpointInfoNormal;
    }

    return NO_ERROR;
}

INT
EXPORT
WSHGetWildcardSockaddr(
    IN PVOID HelperDllSocketContext,
    OUT PSOCKADDR Sockaddr,
    OUT PINT SockaddrLength)
{
    UNREFERENCED_PARAMETER(HelperDllSocketContext);

    if (!Sockaddr || !SockaddrLength || *SockaddrLength < (INT)sizeof(SOCKADDR_UN))
        return WSAEFAULT;

    RtlZeroMemory(Sockaddr, sizeof(SOCKADDR_UN));
    Sockaddr->sa_family = AF_UNIX;

    return NO_ERROR;
}

INT
EXPORT
WSHGetBroadcastSockaddr(
    IN PVOID HelperDllSocketContext,
    OUT PSOCKADDR Sockaddr,
    OUT PINT SockaddrLength)
{
    UNREFERENCED_PARAMETER(HelperDllSocketContext);
    UNREFERENCED_PARAMETER(Sockaddr);
    UNREFERENCED_PARAMETER(SockaddrLength);

    return WSAEOPNOTSUPP;
}

DWORD
EXPORT
WSHGetWinsockMapping(
    OUT PWINSOCK_MAPPING Mapping,
    IN DWORD MappingLength)
{
    DWORD Rows = 1;
    DWORD Columns = 3;
    DWORD Size = 2 * sizeof(DWORD) + Columns * Rows * sizeof(DWORD);

    if (MappingLength < Size)
        return Size;

    Mapping->Rows = Rows;
    Mapping->Columns = Columns;

    Mapping->Mapping[0].AddressFamily = AF_UNIX;
    Mapping->Mapping[0].SocketType = SOCK_STREAM;
    Mapping->Mapping[0].Protocol = 0;

    return NO_ERROR;
}

INT
EXPORT
WSHGetSocketInformation(
    IN PVOID HelperDllSocketContext,
    IN SOCKET SocketHandle,
    IN HANDLE TdiAddressObjectHandle,
    IN HANDLE TdiConnectionObjectHandle,
    IN INT Level,
    IN INT OptionName,
    OUT PCHAR OptionValue,
    OUT PINT OptionLength)
{
    UNREFERENCED_PARAMETER(HelperDllSocketContext);
    UNREFERENCED_PARAMETER(SocketHandle);
    UNREFERENCED_PARAMETER(TdiAddressObjectHandle);
    UNREFERENCED_PARAMETER(TdiConnectionObjectHandle);
    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(OptionName);
    UNREFERENCED_PARAMETER(OptionValue);
    UNREFERENCED_PARAMETER(OptionLength);

    return WSAENOPROTOOPT;
}

INT
EXPORT
WSHSetSocketInformation(
    IN PVOID HelperDllSocketContext,
    IN SOCKET SocketHandle,
    IN HANDLE TdiAddressObjectHandle,
    IN HANDLE TdiConnectionObjectHandle,
    IN INT Level,
    IN INT OptionName,
    IN PCHAR OptionValue,
    IN INT OptionLength)
{
    UNREFERENCED_PARAMETER(HelperDllSocketContext);
    UNREFERENCED_PARAMETER(SocketHandle);
    UNREFERENCED_PARAMETER(TdiAddressObjectHandle);
    UNREFERENCED_PARAMETER(TdiConnectionObjectHandle);
    UNREFERENCED_PARAMETER(OptionValue);
    UNREFERENCED_PARAMETER(OptionLength);

    if (Level == SOL_SOCKET)
        return NO_ERROR;

    UNREFERENCED_PARAMETER(OptionName);

    return WSAENOPROTOOPT;
}

INT
EXPORT
WSHAddressToString(
    IN LPSOCKADDR Address,
    IN INT AddressLength,
    IN LPWSAPROTOCOL_INFOW ProtocolInfo,
    OUT LPWSTR AddressString,
    IN OUT LPDWORD AddressStringLength)
{
    PSOCKADDR_UN Local = (PSOCKADDR_UN)Address;
    DWORD Needed;
    DWORD Index;

    UNREFERENCED_PARAMETER(ProtocolInfo);

    if (!Local || AddressLength < (INT)sizeof(ADDRESS_FAMILY) || !AddressStringLength)
        return WSAEINVAL;

    Needed = 0;
    while (Needed < UNIX_PATH_MAX &&
           (INT)(Needed + sizeof(ADDRESS_FAMILY)) < AddressLength &&
           Local->sun_path[Needed] != '\0')
    {
        Needed++;
    }

    if (*AddressStringLength < Needed + 1)
    {
        *AddressStringLength = Needed + 1;
        return WSAEFAULT;
    }

    for (Index = 0; Index < Needed; Index++)
        AddressString[Index] = (WCHAR)(UCHAR)Local->sun_path[Index];

    AddressString[Needed] = UNICODE_NULL;
    *AddressStringLength = Needed + 1;

    return NO_ERROR;
}

INT
EXPORT
WSHStringToAddress(
    IN LPWSTR AddressString,
    IN DWORD AddressFamily,
    IN LPWSAPROTOCOL_INFOW ProtocolInfo,
    OUT LPSOCKADDR Address,
    IN OUT LPDWORD AddressLength)
{
    PSOCKADDR_UN Local = (PSOCKADDR_UN)Address;
    DWORD Index;

    UNREFERENCED_PARAMETER(ProtocolInfo);

    if (AddressFamily != AF_UNIX)
        return WSAEAFNOSUPPORT;

    if (!AddressString || !Local || !AddressLength ||
        *AddressLength < sizeof(SOCKADDR_UN))
    {
        return WSAEFAULT;
    }

    RtlZeroMemory(Local, sizeof(SOCKADDR_UN));
    Local->sun_family = AF_UNIX;

    for (Index = 0; Index < UNIX_PATH_MAX - 1 && AddressString[Index]; Index++)
        Local->sun_path[Index] = (CHAR)AddressString[Index];

    *AddressLength = sizeof(SOCKADDR_UN);

    return NO_ERROR;
}

INT
EXPORT
WSHIoctl(
    IN PVOID HelperDllSocketContext,
    IN SOCKET SocketHandle,
    IN HANDLE TdiAddressObjectHandle,
    IN HANDLE TdiConnectionObjectHandle,
    IN DWORD IoControlCode,
    IN LPVOID InputBuffer,
    IN DWORD InputBufferLength,
    IN LPVOID OutputBuffer,
    IN DWORD OutputBufferLength,
    OUT LPDWORD NumberOfBytesReturned,
    IN LPWSAOVERLAPPED Overlapped,
    IN LPWSAOVERLAPPED_COMPLETION_ROUTINE CompletionRoutine,
    OUT LPBOOL NeedsCompletion)
{
    UNREFERENCED_PARAMETER(HelperDllSocketContext);
    UNREFERENCED_PARAMETER(SocketHandle);
    UNREFERENCED_PARAMETER(TdiAddressObjectHandle);
    UNREFERENCED_PARAMETER(TdiConnectionObjectHandle);
    UNREFERENCED_PARAMETER(IoControlCode);
    UNREFERENCED_PARAMETER(InputBuffer);
    UNREFERENCED_PARAMETER(InputBufferLength);
    UNREFERENCED_PARAMETER(OutputBuffer);
    UNREFERENCED_PARAMETER(OutputBufferLength);
    UNREFERENCED_PARAMETER(Overlapped);
    UNREFERENCED_PARAMETER(CompletionRoutine);

    if (NumberOfBytesReturned)
        *NumberOfBytesReturned = 0;

    if (NeedsCompletion)
        *NeedsCompletion = TRUE;

    return WSAEINVAL;
}

INT
EXPORT
WSHJoinLeaf(
    IN PVOID HelperDllSocketContext,
    IN SOCKET SocketHandle,
    IN HANDLE TdiAddressObjectHandle,
    IN HANDLE TdiConnectionObjectHandle,
    IN PVOID LeafHelperDllSocketContext,
    IN SOCKET LeafSocketHandle,
    IN PSOCKADDR Sockaddr,
    IN DWORD SockaddrLength,
    IN LPWSABUF CallerData,
    IN LPWSABUF CalleeData,
    IN LPQOS SocketQOS,
    IN LPQOS GroupQOS,
    IN DWORD Flags)
{
    UNREFERENCED_PARAMETER(HelperDllSocketContext);
    UNREFERENCED_PARAMETER(SocketHandle);
    UNREFERENCED_PARAMETER(TdiAddressObjectHandle);
    UNREFERENCED_PARAMETER(TdiConnectionObjectHandle);
    UNREFERENCED_PARAMETER(LeafHelperDllSocketContext);
    UNREFERENCED_PARAMETER(LeafSocketHandle);
    UNREFERENCED_PARAMETER(Sockaddr);
    UNREFERENCED_PARAMETER(SockaddrLength);
    UNREFERENCED_PARAMETER(CallerData);
    UNREFERENCED_PARAMETER(CalleeData);
    UNREFERENCED_PARAMETER(SocketQOS);
    UNREFERENCED_PARAMETER(GroupQOS);
    UNREFERENCED_PARAMETER(Flags);

    return WSAEOPNOTSUPP;
}

INT
EXPORT
WSHEnumProtocols(
    IN LPINT lpiProtocols,
    IN LPWSTR lpTransportKeyName,
    IN OUT LPVOID lpProtocolBuffer,
    IN OUT LPDWORD lpdwBufferLength)
{
    UNREFERENCED_PARAMETER(lpiProtocols);
    UNREFERENCED_PARAMETER(lpTransportKeyName);
    UNREFERENCED_PARAMETER(lpProtocolBuffer);
    UNREFERENCED_PARAMETER(lpdwBufferLength);

    return WSAEOPNOTSUPP;
}

INT
EXPORT
WSHGetProviderGuid(
    IN LPWSTR ProviderName,
    OUT LPGUID ProviderGuid)
{
    static const GUID UnixProviderGuid =
        {0xa00943d9, 0x9c2e, 0x4633, {0x9b, 0x59, 0x00, 0x57, 0xa3, 0x16, 0x09, 0x94}};

    UNREFERENCED_PARAMETER(ProviderName);

    if (!ProviderGuid)
        return WSAEFAULT;

    *ProviderGuid = UnixProviderGuid;

    return NO_ERROR;
}

INT
EXPORT
WSHGetWSAProtocolInfo(
    IN LPWSTR ProviderName,
    OUT LPWSAPROTOCOL_INFOW *ProtocolInfo,
    OUT LPDWORD ProtocolInfoEntries)
{
    UNREFERENCED_PARAMETER(ProviderName);
    UNREFERENCED_PARAMETER(ProtocolInfo);
    UNREFERENCED_PARAMETER(ProtocolInfoEntries);

    return WSAEOPNOTSUPP;
}
