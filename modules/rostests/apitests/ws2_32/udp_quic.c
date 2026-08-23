/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the UDP socket setup and message I/O used by QUIC (HTTP/3) clients
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "ws2_32.h"
#include <mswsock.h>

#ifndef IP_RECVECN
#define IP_RECVECN 50
#endif
#ifndef IP_ECN
#define IP_ECN 50
#endif
#ifndef IPV6_RECVECN
#define IPV6_RECVECN 50
#endif
#ifndef UDP_SEND_MSG_SIZE
#define UDP_SEND_MSG_SIZE 2
#endif
#ifndef UDP_RECV_MAX_COALESCED_SIZE
#define UDP_RECV_MAX_COALESCED_SIZE 3
#endif

#define RECV_TIMEOUT_MS 2000

typedef INT (WSAAPI *PWSASENDMSG)(SOCKET, LPWSAMSG, DWORD, LPDWORD, LPWSAOVERLAPPED, LPWSAOVERLAPPED_COMPLETION_ROUTINE);

static int
SetOption(
    _In_ SOCKET Socket,
    _In_ int Level,
    _In_ int Option,
    _In_ DWORD Value,
    _Out_ int *Error)
{
    int Result = setsockopt(Socket, Level, Option, (const char *)&Value, sizeof(Value));

    *Error = Result == SOCKET_ERROR ? WSAGetLastError() : 0;
    return Result;
}

static BOOL
WaitReadable(
    _In_ SOCKET Socket)
{
    struct timeval Timeout;
    fd_set ReadSet;

    FD_ZERO(&ReadSet);
    FD_SET(Socket, &ReadSet);
    Timeout.tv_sec = RECV_TIMEOUT_MS / 1000;
    Timeout.tv_usec = (RECV_TIMEOUT_MS % 1000) * 1000;

    return select(0, &ReadSet, NULL, NULL, &Timeout) == 1;
}

static void
TestQuicSocketSetup(
    _In_ SOCKET Socket,
    _In_ BOOL IsIpv6,
    _In_ PCSTR Tag)
{
    u_long One = 1;
    DWORD V6Only = 0xFFFFFFFF;
    int Length, Result, Error;

    Result = ioctlsocket(Socket, FIONBIO, &One);
    ok(Result == 0, "%s: FIONBIO failed with %d\n", Tag, WSAGetLastError());

    Length = sizeof(V6Only);
    Result = getsockopt(Socket, IPPROTO_IPV6, IPV6_V6ONLY, (char *)&V6Only, &Length);
    Error = Result == SOCKET_ERROR ? WSAGetLastError() : 0;
    ok(Result == 0, "%s: getsockopt IPV6_V6ONLY returned %d error %d\n", Tag, Result, Error);

    if (!IsIpv6)
    {
        Result = SetOption(Socket, IPPROTO_IP, IP_DONTFRAGMENT, 1, &Error);
        ok(Result == 0, "%s: IP_DONTFRAGMENT failed with %d\n", Tag, Error);

        Result = SetOption(Socket, IPPROTO_IP, IP_PKTINFO, 1, &Error);
        ok(Result == 0, "%s: IP_PKTINFO failed with %d\n", Tag, Error);

        Result = SetOption(Socket, IPPROTO_IP, IP_RECVECN, 1, &Error);
        ok(Result == 0, "%s: IP_RECVECN failed with %d\n", Tag, Error);
    }
    else
    {
        Result = SetOption(Socket, IPPROTO_IPV6, IPV6_DONTFRAG, 1, &Error);
        ok(Result == 0, "%s: IPV6_DONTFRAG failed with %d\n", Tag, Error);

        Result = SetOption(Socket, IPPROTO_IPV6, IPV6_PKTINFO, 1, &Error);
        ok(Result == 0, "%s: IPV6_PKTINFO failed with %d\n", Tag, Error);

        Result = SetOption(Socket, IPPROTO_IPV6, IPV6_RECVECN, 1, &Error);
        ok(Result == 0, "%s: IPV6_RECVECN failed with %d\n", Tag, Error);
    }

    Result = SetOption(Socket, IPPROTO_UDP, UDP_RECV_MAX_COALESCED_SIZE, 65535, &Error);
    ok(Result == 0, "%s: UDP_RECV_MAX_COALESCED_SIZE failed with %d\n", Tag, Error);

    Result = SetOption(Socket, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 1200, &Error);
    ok(Result == 0, "%s: UDP_SEND_MSG_SIZE failed with %d\n", Tag, Error);

    Result = SetOption(Socket, IPPROTO_UDP, UDP_SEND_MSG_SIZE, 0, &Error);
    ok(Result == 0, "%s: UDP_SEND_MSG_SIZE reset failed with %d\n", Tag, Error);
}

static void
TestIpv4MessageIo(void)
{
    struct sockaddr_in Address, From;
    SOCKET Receiver = INVALID_SOCKET;
    SOCKET Sender = INVALID_SOCKET;
    LPFN_WSARECVMSG pWSARecvMsg = NULL;
    PWSASENDMSG pWSASendMsg;
    GUID RecvMsgGuid = WSAID_WSARECVMSG;
    DWORD Bytes = 0;
    int AddressLength, Result, Error;

    Receiver = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Sender = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    ok(Receiver != INVALID_SOCKET && Sender != INVALID_SOCKET, "IPv4 UDP socket failed with %d\n", WSAGetLastError());
    if (Receiver == INVALID_SOCKET || Sender == INVALID_SOCKET)
        goto Cleanup;

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Address.sin_port = 0;

    Result = bind(Receiver, (const struct sockaddr *)&Address, sizeof(Address));
    ok(Result == 0, "IPv4 bind failed with %d\n", WSAGetLastError());
    AddressLength = sizeof(Address);
    Result = getsockname(Receiver, (struct sockaddr *)&Address, &AddressLength);
    ok(Result == 0, "IPv4 getsockname failed with %d\n", WSAGetLastError());

    TestQuicSocketSetup(Receiver, FALSE, "ipv4");

    Result = WSAIoctl(Receiver,
                      SIO_GET_EXTENSION_FUNCTION_POINTER,
                      &RecvMsgGuid,
                      sizeof(RecvMsgGuid),
                      &pWSARecvMsg,
                      sizeof(pWSARecvMsg),
                      &Bytes,
                      NULL,
                      NULL);
    ok(Result == 0 && pWSARecvMsg != NULL,
       "WSARecvMsg extension lookup returned %d error %d pointer %p\n",
       Result,
       Result == SOCKET_ERROR ? WSAGetLastError() : 0,
       pWSARecvMsg);

    pWSASendMsg = (PWSASENDMSG)GetProcAddress(GetModuleHandleW(L"ws2_32.dll"), "WSASendMsg");
    ok(pWSASendMsg != NULL, "WSASendMsg is not exported\n");

    Result = sendto(Sender, "Q", 1, 0, (const struct sockaddr *)&Address, sizeof(Address));
    ok(Result == 1, "sendto returned %d error %d\n", Result, WSAGetLastError());
    ok(WaitReadable(Receiver), "datagram did not become readable\n");

    if (pWSARecvMsg)
    {
        CHAR Control[256];
        CHAR Data[16];
        WSABUF Buffer;
        WSAMSG Message;
        WSACMSGHDR *Header;
        BOOL SawPktInfo = FALSE;

        Buffer.buf = Data;
        Buffer.len = sizeof(Data);
        ZeroMemory(&Message, sizeof(Message));
        Message.name = (LPSOCKADDR)&From;
        Message.namelen = sizeof(From);
        Message.lpBuffers = &Buffer;
        Message.dwBufferCount = 1;
        Message.Control.buf = Control;
        Message.Control.len = sizeof(Control);

        Bytes = 0;
        Result = pWSARecvMsg(Receiver, &Message, &Bytes, NULL, NULL);
        Error = Result == SOCKET_ERROR ? WSAGetLastError() : 0;
        ok(Result == 0 && Bytes == 1 && Data[0] == 'Q',
           "WSARecvMsg returned %d error %d bytes %lu\n",
           Result,
           Error,
           Bytes);

        if (Result == 0)
        {
            for (Header = WSA_CMSG_FIRSTHDR(&Message); Header; Header = WSA_CMSG_NXTHDR(&Message, Header))
            {
                if (Header->cmsg_level == IPPROTO_IP && Header->cmsg_type == IP_PKTINFO)
                {
                    IN_PKTINFO *Info = (IN_PKTINFO *)WSA_CMSG_DATA(Header);

                    SawPktInfo = TRUE;
                    ok(Info->ipi_addr.s_addr == htonl(INADDR_LOOPBACK),
                       "IP_PKTINFO destination is %08lx\n",
                       (ULONG)ntohl(Info->ipi_addr.s_addr));
                }
            }

            ok(SawPktInfo, "WSARecvMsg returned no IP_PKTINFO control data (control length %lu)\n", Message.Control.len);
            ok(From.sin_family == AF_INET && From.sin_addr.s_addr == htonl(INADDR_LOOPBACK),
               "WSARecvMsg source is family %u address %08lx\n",
               From.sin_family,
               (ULONG)ntohl(From.sin_addr.s_addr));
        }
    }
    else
    {
        recvfrom(Receiver, (char *)&Bytes, sizeof(Bytes), 0, NULL, NULL);
    }

    if (pWSASendMsg)
    {
        CHAR Control[WSA_CMSG_SPACE(sizeof(IN_PKTINFO))];
        WSABUF Buffer;
        WSAMSG Message;
        WSACMSGHDR *Header;
        IN_PKTINFO *Info;
        CHAR Data[16];

        Buffer.buf = "S";
        Buffer.len = 1;
        ZeroMemory(&Message, sizeof(Message));
        Message.name = (LPSOCKADDR)&Address;
        Message.namelen = sizeof(Address);
        Message.lpBuffers = &Buffer;
        Message.dwBufferCount = 1;
        Message.Control.buf = Control;
        Message.Control.len = sizeof(Control);
        ZeroMemory(Control, sizeof(Control));

        Header = WSA_CMSG_FIRSTHDR(&Message);
        Header->cmsg_len = WSA_CMSG_LEN(sizeof(IN_PKTINFO));
        Header->cmsg_level = IPPROTO_IP;
        Header->cmsg_type = IP_PKTINFO;
        Info = (IN_PKTINFO *)WSA_CMSG_DATA(Header);
        Info->ipi_addr.s_addr = htonl(INADDR_LOOPBACK);
        Info->ipi_ifindex = 0;

        Bytes = 0;
        Result = pWSASendMsg(Sender, &Message, 0, &Bytes, NULL, NULL);
        Error = Result == SOCKET_ERROR ? WSAGetLastError() : 0;
        ok(Result == 0 && Bytes == 1, "WSASendMsg returned %d error %d bytes %lu\n", Result, Error, Bytes);

        if (Result == 0)
        {
            ok(WaitReadable(Receiver), "WSASendMsg datagram did not become readable\n");
            Result = recvfrom(Receiver, Data, sizeof(Data), 0, NULL, NULL);
            ok(Result == 1 && Data[0] == 'S', "recvfrom after WSASendMsg returned %d error %d\n", Result, WSAGetLastError());
        }
    }

Cleanup:
    if (Receiver != INVALID_SOCKET)
        closesocket(Receiver);
    if (Sender != INVALID_SOCKET)
        closesocket(Sender);
}

static void
TestIpv6DualStackSetup(void)
{
    struct sockaddr_in6 Address;
    SOCKET Socket;
    DWORD Zero = 0;
    int Result;

    Socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
    ok(Socket != INVALID_SOCKET, "IPv6 UDP socket failed with %d\n", WSAGetLastError());
    if (Socket == INVALID_SOCKET)
        return;

    Result = setsockopt(Socket, IPPROTO_IPV6, IPV6_V6ONLY, (const char *)&Zero, sizeof(Zero));
    ok(Result == 0, "IPV6_V6ONLY=0 failed with %d\n", WSAGetLastError());

    ZeroMemory(&Address, sizeof(Address));
    Address.sin6_family = AF_INET6;
    Address.sin6_port = 0;

    Result = bind(Socket, (const struct sockaddr *)&Address, sizeof(Address));
    ok(Result == 0, "IPv6 bind failed with %d\n", WSAGetLastError());

    TestQuicSocketSetup(Socket, TRUE, "ipv6");

    closesocket(Socket);
}

START_TEST(udp_quic)
{
    WSADATA WsaData;
    int Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Result != 0)
    {
        skip("WSAStartup failed with %d\n", Result);
        return;
    }

    TestIpv4MessageIo();
    TestIpv6DualStackSetup();

    WSACleanup();
}
