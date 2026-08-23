/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests nonblocking connect completion through select
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "ws2_32.h"

#define CLIENT_COUNT 16
#define TEST_ROUNDS 32
#define SELECT_TIMEOUT_SECONDS 5

static BOOL
SendProbe(
    _In_ SOCKET Socket,
    _In_ ULONG Round,
    _In_ ULONG Index)
{
    const char Probe = 'C';
    int Result;

    Result = send(Socket, &Probe, sizeof(Probe), 0);
    if (Result != sizeof(Probe))
    {
        ok(FALSE,
           "round %lu client %lu: send returned %d, error %d\n",
           Round,
           Index,
           Result,
           WSAGetLastError());
        return FALSE;
    }

    return TRUE;
}

static BOOL
DrainServerConnections(
    _In_ SOCKET ListenSocket,
    _In_ ULONG Round)
{
    ULONG Accepted;

    for (Accepted = 0; Accepted < CLIENT_COUNT; ++Accepted)
    {
        struct timeval Timeout;
        fd_set ReadSet;
        SOCKET AcceptedSocket;
        char Probe;
        int Result;

        FD_ZERO(&ReadSet);
        FD_SET(ListenSocket, &ReadSet);
        Timeout.tv_sec = SELECT_TIMEOUT_SECONDS;
        Timeout.tv_usec = 0;

        Result = select(0, &ReadSet, NULL, NULL, &Timeout);
        if (Result != 1 || !FD_ISSET(ListenSocket, &ReadSet))
        {
            ok(FALSE,
               "round %lu: listener select returned %d after %lu accepts, error %d\n",
               Round,
               Result,
               Accepted,
               WSAGetLastError());
            return FALSE;
        }

        AcceptedSocket = accept(ListenSocket, NULL, NULL);
        if (AcceptedSocket == INVALID_SOCKET)
        {
            ok(FALSE,
               "round %lu: accept %lu failed with %d\n",
               Round,
               Accepted,
               WSAGetLastError());
            return FALSE;
        }

        FD_ZERO(&ReadSet);
        FD_SET(AcceptedSocket, &ReadSet);
        Timeout.tv_sec = SELECT_TIMEOUT_SECONDS;
        Timeout.tv_usec = 0;

        Result = select(0, &ReadSet, NULL, NULL, &Timeout);
        if (Result != 1 || !FD_ISSET(AcceptedSocket, &ReadSet))
        {
            ok(FALSE,
               "round %lu: accepted socket %lu did not become readable, result %d error %d\n",
               Round,
               Accepted,
               Result,
               WSAGetLastError());
            closesocket(AcceptedSocket);
            return FALSE;
        }

        Result = recv(AcceptedSocket, &Probe, sizeof(Probe), 0);
        ok(Result == sizeof(Probe) && Probe == 'C',
           "round %lu: recv %lu returned %d byte %#x, error %d\n",
           Round,
           Accepted,
           Result,
           Result == sizeof(Probe) ? (UCHAR)Probe : 0,
           WSAGetLastError());
        closesocket(AcceptedSocket);

        if (Result != sizeof(Probe) || Probe != 'C')
            return FALSE;
    }

    return TRUE;
}

START_TEST(connect_select)
{
    WSADATA WsaData;
    struct sockaddr_in ServerAddress;
    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET Clients[CLIENT_COUNT];
    BOOL Pending[CLIENT_COUNT];
    ULONG TotalPending = 0;
    ULONG Round;
    BOOL ContinueTesting = TRUE;
    int AddressLength;
    int Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Result != 0)
    {
        skip("WSAStartup failed with %d\n", Result);
        return;
    }

    ListenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ListenSocket == INVALID_SOCKET)
    {
        skip("listener socket creation failed with %d\n", WSAGetLastError());
        WSACleanup();
        return;
    }

    ZeroMemory(&ServerAddress, sizeof(ServerAddress));
    ServerAddress.sin_family = AF_INET;
    ServerAddress.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ServerAddress.sin_port = 0;

    Result = bind(ListenSocket,
                  (const struct sockaddr *)&ServerAddress,
                  sizeof(ServerAddress));
    if (Result == SOCKET_ERROR)
    {
        skip("listener bind failed with %d\n", WSAGetLastError());
        goto Cleanup;
    }

    AddressLength = sizeof(ServerAddress);
    Result = getsockname(ListenSocket,
                         (struct sockaddr *)&ServerAddress,
                         &AddressLength);
    if (Result == SOCKET_ERROR)
    {
        skip("listener getsockname failed with %d\n", WSAGetLastError());
        goto Cleanup;
    }

    Result = listen(ListenSocket, SOMAXCONN);
    if (Result == SOCKET_ERROR)
    {
        skip("listener listen failed with %d\n", WSAGetLastError());
        goto Cleanup;
    }

    for (Round = 0; Round < TEST_ROUNDS; ++Round)
    {
        ULONG Index;
        ULONG PendingCount = 0;
        ULONG SentCount = 0;
        u_long NonBlocking = 1;

        for (Index = 0; Index < CLIENT_COUNT; ++Index)
        {
            Clients[Index] = INVALID_SOCKET;
            Pending[Index] = FALSE;
        }

        for (Index = 0; Index < CLIENT_COUNT; ++Index)
        {
            int Error;

            Clients[Index] = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
            if (Clients[Index] == INVALID_SOCKET)
            {
                ok(FALSE,
                   "round %lu client %lu: socket failed with %d\n",
                   Round,
                   Index,
                   WSAGetLastError());
                break;
            }

            Result = ioctlsocket(Clients[Index], FIONBIO, &NonBlocking);
            if (Result == SOCKET_ERROR)
            {
                ok(FALSE,
                   "round %lu client %lu: FIONBIO failed with %d\n",
                   Round,
                   Index,
                   WSAGetLastError());
                break;
            }

            Result = connect(Clients[Index],
                             (const struct sockaddr *)&ServerAddress,
                             sizeof(ServerAddress));
            if (Result == 0)
            {
                if (SendProbe(Clients[Index], Round, Index))
                    ++SentCount;
                continue;
            }

            Error = WSAGetLastError();
            if (Error != WSAEWOULDBLOCK)
            {
                ok(FALSE,
                   "round %lu client %lu: connect failed with %d\n",
                   Round,
                   Index,
                   Error);
                break;
            }

            Pending[Index] = TRUE;
            ++PendingCount;
            ++TotalPending;
        }

        while (PendingCount != 0)
        {
            struct timeval Timeout;
            fd_set WriteSet;
            fd_set ExceptSet;

            FD_ZERO(&WriteSet);
            FD_ZERO(&ExceptSet);
            for (Index = 0; Index < CLIENT_COUNT; ++Index)
            {
                if (Pending[Index])
                {
                    FD_SET(Clients[Index], &WriteSet);
                    FD_SET(Clients[Index], &ExceptSet);
                }
            }

            Timeout.tv_sec = SELECT_TIMEOUT_SECONDS;
            Timeout.tv_usec = 0;
            Result = select(0, NULL, &WriteSet, &ExceptSet, &Timeout);
            if (Result == SOCKET_ERROR)
            {
                ok(FALSE,
                   "round %lu: connect select failed with %d (%lu pending)\n",
                   Round,
                   WSAGetLastError(),
                   PendingCount);
                break;
            }
            if (Result == 0)
            {
                ok(FALSE,
                   "round %lu: connect select timed out with %lu pending\n",
                   Round,
                   PendingCount);
                break;
            }

            for (Index = 0; Index < CLIENT_COUNT; ++Index)
            {
                int SocketError = 0;
                int SocketErrorLength = sizeof(SocketError);
                BOOL Writable;
                BOOL Exceptional;

                Writable = FD_ISSET(Clients[Index], &WriteSet) != 0;
                Exceptional = FD_ISSET(Clients[Index], &ExceptSet) != 0;

                if (!Pending[Index] || (!Writable && !Exceptional))
                {
                    continue;
                }

                Result = getsockopt(Clients[Index],
                                    SOL_SOCKET,
                                    SO_ERROR,
                                    (char *)&SocketError,
                                    &SocketErrorLength);
                ok(Result == 0,
                   "round %lu client %lu socket %Ix: SO_ERROR query failed with %d (write %u except %u)\n",
                   Round,
                   Index,
                   (ULONG_PTR)Clients[Index],
                   WSAGetLastError(),
                   Writable,
                   Exceptional);
                ok(SocketError == 0,
                   "round %lu client %lu socket %Ix: asynchronous connect failed with %d (write %u except %u)\n",
                   Round,
                   Index,
                   (ULONG_PTR)Clients[Index],
                   SocketError,
                   Writable,
                   Exceptional);
                ok(Writable,
                   "round %lu client %lu socket %Ix: successful connect was not writable (SO_ERROR %d, except %u)\n",
                   Round,
                   Index,
                   (ULONG_PTR)Clients[Index],
                   SocketError,
                   Exceptional);

                Pending[Index] = FALSE;
                --PendingCount;
                if (Result == 0 && SocketError == 0 &&
                    Writable &&
                    SendProbe(Clients[Index], Round, Index))
                {
                    ++SentCount;
                }
            }
        }

        ok(SentCount == CLIENT_COUNT,
           "round %lu: sent on %lu of %u connected sockets\n",
           Round,
           SentCount,
           CLIENT_COUNT);

        if (SentCount == CLIENT_COUNT)
            ContinueTesting = DrainServerConnections(ListenSocket, Round);

        for (Index = 0; Index < CLIENT_COUNT; ++Index)
        {
            if (Clients[Index] != INVALID_SOCKET)
                closesocket(Clients[Index]);
        }

        if (SentCount != CLIENT_COUNT || !ContinueTesting)
            break;
    }

    ok(TotalPending != 0,
       "all %u loopback connects completed synchronously; pending select path was not exercised\n",
       CLIENT_COUNT * TEST_ROUNDS);
    trace("exercised %lu pending nonblocking connects across %u rounds\n",
          TotalPending,
          TEST_ROUNDS);

Cleanup:
    if (ListenSocket != INVALID_SOCKET)
        closesocket(ListenSocket);
    WSACleanup();
}
