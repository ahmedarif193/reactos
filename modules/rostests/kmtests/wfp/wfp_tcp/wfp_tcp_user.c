/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform stream callout test, user-mode part
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winsock2.h>

#include "wfp_tcp.h"

static
BOOL
Readable(
    _In_ SOCKET Socket,
    _In_ LONG Milliseconds)
{
    struct timeval Timeout;
    fd_set Set;

    FD_ZERO(&Set);
    FD_SET(Socket, &Set);
    Timeout.tv_sec = Milliseconds / 1000;
    Timeout.tv_usec = (Milliseconds % 1000) * 1000;
    return select(0, &Set, NULL, NULL, &Timeout) == 1;
}

static
VOID
Send(
    _In_ SOCKET Socket,
    _In_ PCSTR Text)
{
    int Length = (int)strlen(Text);
    int Sent = send(Socket, Text, Length, 0);

    ok(Sent == Length, "send returned %d, error %d\n", Sent, WSAGetLastError());
}

static
VOID
Receive(
    _In_ SOCKET Socket,
    _In_ PCSTR Expected)
{
    int Length = (int)strlen(Expected), Total = 0, Received;
    char Buffer[64];

    while (Total < Length && Readable(Socket, 3000))
    {
        Received = recv(Socket, Buffer + Total, Length - Total, 0);
        if (Received <= 0)
        {
            break;
        }
        Total += Received;
    }
    ok(Total == Length && !memcmp(Buffer, Expected, Length), "Received %d bytes, expected \"%s\"\n", Total, Expected);
}

static
VOID
ReceiveEnd(
    _In_ SOCKET Socket)
{
    char Buffer[8];
    int Received = -1;

    if (Readable(Socket, 3000))
    {
        Received = recv(Socket, Buffer, sizeof(Buffer), 0);
    }
    ok(Received == 0, "recv returned %d, error %d\n", Received, WSAGetLastError());
}

START_TEST(WfpTcp)
{
    SOCKET Listener, Client, Server = INVALID_SOCKET;
    struct sockaddr_in Address;
    WSADATA WsaData;
    DWORD Error;

    Error = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok_eq_int(Error, 0);
    if (Error)
    {
        return;
    }

    Error = KmtLoadAndOpenDriver(L"WfpTcp", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        WSACleanup();
        return;
    }

    Error = KmtSendToDriver(IOCTL_WFPTCP_SETUP);
    ok_eq_int(Error, ERROR_SUCCESS);

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Address.sin_port = htons(WFPTCP_SERVER_PORT);

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    ok(Listener != INVALID_SOCKET && Client != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    ok(bind(Listener, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed: %d\n", WSAGetLastError());
    ok(listen(Listener, 1) == 0, "listen failed: %d\n", WSAGetLastError());
    ok(connect(Client, (struct sockaddr *)&Address, sizeof(Address)) == 0, "connect failed: %d\n", WSAGetLastError());
    if (Readable(Listener, 3000))
    {
        Server = accept(Listener, NULL, NULL);
    }
    ok(Server != INVALID_SOCKET, "accept failed: %d\n", WSAGetLastError());

    Error = KmtSendToDriver(IOCTL_WFPTCP_CONNECTED);
    ok_eq_int(Error, ERROR_SUCCESS);

    if (Server != INVALID_SOCKET)
    {
        Send(Client, "HELLO001");
        Receive(Server, "HELLO001");
        Send(Server, "REPLY001-and-more");
        Receive(Client, "REPLY001-and-more");
        Send(Client, "HELLO002");
        Receive(Server, "HELLO002");
    }

    Error = KmtSendToDriver(IOCTL_WFPTCP_EXCHANGED);
    ok_eq_int(Error, ERROR_SUCCESS);

    if (Server != INVALID_SOCKET)
    {
        ok(shutdown(Client, SD_SEND) == 0, "shutdown failed: %d\n", WSAGetLastError());
        ReceiveEnd(Server);
        ok(shutdown(Server, SD_SEND) == 0, "shutdown failed: %d\n", WSAGetLastError());
        ReceiveEnd(Client);
    }

    Error = KmtSendToDriver(IOCTL_WFPTCP_SHUTDOWN);
    ok_eq_int(Error, ERROR_SUCCESS);

    closesocket(Client);
    if (Server != INVALID_SOCKET)
    {
        closesocket(Server);
    }
    closesocket(Listener);

    Error = KmtSendToDriver(IOCTL_WFPTCP_CLOSED);
    ok_eq_int(Error, ERROR_SUCCESS);
    Error = KmtSendToDriver(IOCTL_WFPTCP_TEARDOWN);
    ok_eq_int(Error, ERROR_SUCCESS);

    KmtCloseDriver();
    KmtUnloadDriver();
    WSACleanup();
}
