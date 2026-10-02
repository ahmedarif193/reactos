/*
 * PROJECT:     LiberNT kernel-mode tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform datagram callout test, user-mode part
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <kmt_test.h>
#include <winsock2.h>

#include "wfp_udp.h"

static ULONG TestAddress;

static
ULONG
FindLocalAddress(VOID)
{
    struct hostent *Host;
    char Name[256];
    ULONG Address;
    int Index;

    if (gethostname(Name, sizeof(Name)) != 0)
    {
        return 0;
    }

    Host = gethostbyname(Name);
    if (Host == NULL || Host->h_addrtype != AF_INET)
    {
        return 0;
    }

    for (Index = 0; Host->h_addr_list[Index] != NULL; Index++)
    {
        CopyMemory(&Address, Host->h_addr_list[Index], sizeof(Address));
        if (Address != 0 && (ntohl(Address) >> 24) != 127)
        {
            return Address;
        }
    }
    return 0;
}

static
SOCKET
OpenSocket(
    _In_ USHORT Port)
{
    struct sockaddr_in Address;
    SOCKET Socket;

    Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Socket != INVALID_SOCKET, "socket failed: %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
    {
        return Socket;
    }

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = TestAddress;
    Address.sin_port = htons(Port);
    ok(bind(Socket, (struct sockaddr *)&Address, sizeof(Address)) == 0, "bind failed: %d\n", WSAGetLastError());
    return Socket;
}

static
VOID
Send(
    _In_ SOCKET Socket,
    _In_ USHORT Port,
    _In_ PCSTR Text)
{
    struct sockaddr_in Address;
    int Length = (int)strlen(Text);
    int Sent;

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = TestAddress;
    Address.sin_port = htons(Port);
    Sent = sendto(Socket, Text, Length, 0, (struct sockaddr *)&Address, sizeof(Address));
    ok(Sent == Length, "sendto returned %d, error %d\n", Sent, WSAGetLastError());
}

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
USHORT
Receive(
    _In_ SOCKET Socket,
    _In_ PCSTR Expected)
{
    struct sockaddr_in Address;
    int AddressLength = sizeof(Address);
    char Buffer[32];
    int Length;

    ok(Readable(Socket, 3000), "Nothing received, expected \"%s\"\n", Expected);
    if (!Readable(Socket, 0))
    {
        return 0;
    }

    ZeroMemory(&Address, sizeof(Address));
    Length = recvfrom(Socket, Buffer, sizeof(Buffer) - 1, 0, (struct sockaddr *)&Address, &AddressLength);
    ok(Length == (int)strlen(Expected), "recvfrom returned %d, error %d\n", Length, WSAGetLastError());
    if (Length < 0)
    {
        return 0;
    }

    Buffer[Length] = ANSI_NULL;
    ok(!strcmp(Buffer, Expected), "Received \"%s\", expected \"%s\"\n", Buffer, Expected);
    ok(Address.sin_addr.s_addr == TestAddress, "Sender address 0x%08lx\n", ntohl(Address.sin_addr.s_addr));
    return ntohs(Address.sin_port);
}

static
USHORT
LocalPort(
    _In_ SOCKET Socket)
{
    struct sockaddr_in Address;
    int Length = sizeof(Address);

    ZeroMemory(&Address, sizeof(Address));
    ok(getsockname(Socket, (struct sockaddr *)&Address, &Length) == 0, "getsockname failed: %d\n", WSAGetLastError());
    return ntohs(Address.sin_port);
}

START_TEST(WfpUdp)
{
    SOCKET Sockets[7];
    SOCKET Server, Client, Proxied, PlainServer, PlainClient;
    WSADATA WsaData;
    USHORT Port;
    DWORD Error;
    ULONG Index;

    Error = WSAStartup(MAKEWORD(2, 2), &WsaData);
    ok_eq_int(Error, 0);
    if (Error)
    {
        return;
    }

    Error = KmtLoadAndOpenDriver(L"WfpUdp", TRUE);
    ok_eq_int(Error, ERROR_SUCCESS);
    if (Error)
    {
        WSACleanup();
        return;
    }

    Error = KmtSendToDriver(IOCTL_WFPUDP_SETUP);
    ok_eq_int(Error, ERROR_SUCCESS);

    TestAddress = htonl(INADDR_LOOPBACK);
    Sockets[5] = Sockets[6] = INVALID_SOCKET;
    Sockets[0] = Server = OpenSocket(WFPUDP_SERVER_PORT);
    Sockets[1] = Client = OpenSocket(0);
    Sockets[2] = Proxied = OpenSocket(0);
    Sockets[3] = PlainServer = OpenSocket(WFPUDP_PLAIN_PORT);
    Sockets[4] = PlainClient = OpenSocket(0);
    if (Server != INVALID_SOCKET && Client != INVALID_SOCKET && Proxied != INVALID_SOCKET &&
        PlainServer != INVALID_SOCKET && PlainClient != INVALID_SOCKET)
    {
        Send(Client, WFPUDP_SERVER_PORT, "PING0001");
        Port = Receive(Server, "PING0001");
        ok_eq_uint(Port, LocalPort(Client));
        Send(Server, Port, "PONG0001");
        Port = Receive(Client, "PONG0001");
        ok_eq_uint(Port, WFPUDP_SERVER_PORT);
        Send(Client, WFPUDP_SERVER_PORT, "PING0002");
        Port = Receive(Server, "PING0002");
        ok_eq_uint(Port, LocalPort(Client));
        Send(PlainClient, WFPUDP_PLAIN_PORT, "PLAIN001");
        Port = Receive(PlainServer, "PLAIN001");
        ok_eq_uint(Port, LocalPort(PlainClient));

        Error = KmtSendToDriver(IOCTL_WFPUDP_OBSERVED);
        ok_eq_int(Error, ERROR_SUCCESS);

        Send(Client, WFPUDP_SERVER_PORT, "DROP0004");
        ok(!Readable(Server, 150), "Blocked outbound datagram was delivered\n");
        Send(Client, WFPUDP_SERVER_PORT, "DRPI0005");
        ok(!Readable(Server, 150), "Blocked inbound datagram was delivered\n");

        Error = KmtSendToDriver(IOCTL_WFPUDP_BLOCKED);
        ok_eq_int(Error, ERROR_SUCCESS);

        Error = KmtSendToDriver(IOCTL_WFPUDP_REMOVE);
        ok_eq_int(Error, ERROR_SUCCESS);
        Send(Client, WFPUDP_SERVER_PORT, "PING0006");
        Port = Receive(Server, "PING0006");
        ok_eq_uint(Port, LocalPort(Client));
        Error = KmtSendToDriver(IOCTL_WFPUDP_REMOVED);
        ok_eq_int(Error, ERROR_SUCCESS);

        Error = KmtSendToDriver(IOCTL_WFPUDP_REDIRECT);
        ok_eq_int(Error, ERROR_SUCCESS);

        Send(Proxied, WFPUDP_PROXY_PORT, "PING0003");
        Port = Receive(Server, "PING0003");
        ok_eq_uint(Port, LocalPort(Proxied));
        Send(Server, Port, "PONG0003");

        Error = KmtSendToDriver(IOCTL_WFPUDP_REDIRECTED);
        ok_eq_int(Error, ERROR_SUCCESS);
        ok(!Readable(Proxied, 100), "Loopback datagram was delivered by receive injection\n");

        TestAddress = FindLocalAddress();
        if (!skip(TestAddress != 0, "No address outside the loopback network\n"))
        {
            closesocket(Server);
            Sockets[0] = INVALID_SOCKET;
            Sockets[5] = OpenSocket(WFPUDP_SERVER_PORT);
            Sockets[6] = OpenSocket(0);
        }
        if (Sockets[5] != INVALID_SOCKET && Sockets[6] != INVALID_SOCKET)
        {
            Error = KmtSendUlongToDriver(IOCTL_WFPUDP_REDIRECT_LOCAL, ntohl(TestAddress));
            ok_eq_int(Error, ERROR_SUCCESS);

            Send(Sockets[6], WFPUDP_PROXY_PORT, "PING0007");
            Port = Receive(Sockets[5], "PING0007");
            ok_eq_uint(Port, LocalPort(Sockets[6]));
            Send(Sockets[5], Port, "PONG0007");
            Port = Receive(Sockets[6], "PONG0007");
            ok_eq_uint(Port, WFPUDP_PROXY_PORT);

            Error = KmtSendToDriver(IOCTL_WFPUDP_REDIRECTED_LOCAL);
            ok_eq_int(Error, ERROR_SUCCESS);
        }
    }

    for (Index = 0; Index < RTL_NUMBER_OF(Sockets); Index++)
    {
        if (Sockets[Index] != INVALID_SOCKET)
        {
            closesocket(Sockets[Index]);
        }
    }
    Error = KmtSendToDriver(IOCTL_WFPUDP_CLOSED);
    ok_eq_int(Error, ERROR_SUCCESS);
    Error = KmtSendToDriver(IOCTL_WFPUDP_TEARDOWN);
    ok_eq_int(Error, ERROR_SUCCESS);

    KmtCloseDriver();
    KmtUnloadDriver();
    WSACleanup();
}
