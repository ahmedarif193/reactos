/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS Ancillary Function Driver DLL
 * FILE:        dll/win32/msafd/misc/stubs.c
 * PURPOSE:     Stubs
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/09-2000 Created
 */

#include <msafd.h>

INT
WSPAPI
WSPCancelBlockingCall(
    OUT LPINT lpErrno)
{
    UNIMPLEMENTED;

    return 0;
}


BOOL
WSPAPI
WSPGetQOSByName(
    IN      SOCKET s,
    IN OUT  LPWSABUF lpQOSName,
    OUT     LPQOS lpQOS,
    OUT     LPINT lpErrno)
{
    UNIMPLEMENTED;

    return FALSE;
}


SOCKET
WSPAPI
WSPJoinLeaf(
    IN  SOCKET s,
    IN  CONST SOCKADDR *name,
    IN  INT namelen,
    IN  LPWSABUF lpCallerData,
    OUT LPWSABUF lpCalleeData,
    IN  LPQOS lpSQOS,
    IN  LPQOS lpGQOS,
    IN  DWORD dwFlags,
    OUT LPINT lpErrno)
{
    UNIMPLEMENTED;

    return (SOCKET)0;
}

BOOL
WSPAPI
WSPAcceptEx(
    IN SOCKET sListenSocket,
    IN SOCKET sAcceptSocket,
    OUT PVOID lpOutputBuffer,
    IN DWORD dwReceiveDataLength,
    IN DWORD dwLocalAddressLength,
    IN DWORD dwRemoteAddressLength,
    OUT LPDWORD lpdwBytesReceived,
    IN OUT LPOVERLAPPED lpOverlapped)
{
    PSOCKET_INFORMATION Socket;
    AFD_SUPER_ACCEPT_INFO AcceptInfo;
    PIO_STATUS_BLOCK IOSB;
    NTSTATUS Status;

    Socket = GetSocketStructure(sListenSocket);
    if (!Socket)
    {
        SetLastError(WSAENOTSOCK);
        return FALSE;
    }

    if (!lpOverlapped || !lpOutputBuffer || !GetSocketStructure(sAcceptSocket))
    {
        SetLastError(WSAEINVAL);
        return FALSE;
    }

    AcceptInfo.AcceptHandle = (HANDLE)sAcceptSocket;
    AcceptInfo.ReceiveDataLength = dwReceiveDataLength;
    AcceptInfo.LocalAddressLength = dwLocalAddressLength;
    AcceptInfo.RemoteAddressLength = dwRemoteAddressLength;

    IOSB = (PIO_STATUS_BLOCK)lpOverlapped;
    IOSB->Status = STATUS_PENDING;

    Status = NtDeviceIoControlFile((HANDLE)sListenSocket,
                                   lpOverlapped->hEvent,
                                   NULL,
                                   lpOverlapped->hEvent ? NULL : lpOverlapped,
                                   IOSB,
                                   IOCTL_AFD_SUPER_ACCEPT,
                                   lpOutputBuffer,
                                   dwReceiveDataLength + dwLocalAddressLength + dwRemoteAddressLength,
                                   &AcceptInfo,
                                   sizeof(AcceptInfo));

    if (Status == STATUS_SUCCESS && lpdwBytesReceived)
        *lpdwBytesReceived = (DWORD)IOSB->Information;

    SetLastError(TranslateNtStatusError(Status));

    return Status == STATUS_SUCCESS;
}

BOOL
WSPAPI
WSPDisconnectEx(
  IN SOCKET hSocket,
  IN LPOVERLAPPED lpOverlapped,
  IN DWORD dwFlags,
  IN DWORD reserved)
{
    UNIMPLEMENTED;

    return FALSE;
}

VOID
WSPAPI
WSPGetAcceptExSockaddrs(
    IN PVOID lpOutputBuffer,
    IN DWORD dwReceiveDataLength,
    IN DWORD dwLocalAddressLength,
    IN DWORD dwRemoteAddressLength,
    OUT struct sockaddr **LocalSockaddr,
    OUT LPINT LocalSockaddrLength,
    OUT struct sockaddr **RemoteSockaddr,
    OUT LPINT RemoteSockaddrLength)
{
    PCHAR Buffer = (PCHAR)lpOutputBuffer + dwReceiveDataLength;

    *LocalSockaddrLength = *(PINT)Buffer;
    *LocalSockaddr = (struct sockaddr *)(Buffer + sizeof(INT));

    Buffer += dwLocalAddressLength;
    *RemoteSockaddrLength = *(PINT)Buffer;
    *RemoteSockaddr = (struct sockaddr *)(Buffer + sizeof(INT));
}

/* EOF */
