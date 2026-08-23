/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests the NSPR style loopback socket pair used as a pollable event
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include "ws2_32.h"

#define PAIR_ROUNDS 8
#define ACCEPT_TIMEOUT_MS 200
#define READY_TIMEOUT_MS 6000
#define READY_LIMIT_MS 1000
#define IMMEDIATE_LIMIT_MS 100
#define SIGNAL_DELAY_MS 100

typedef struct _SIGNAL_CONTEXT
{
    SOCKET Writer;
    int Result;
    int Error;
} SIGNAL_CONTEXT, *PSIGNAL_CONTEXT;

static BOOL
SetNonBlocking(
    _In_ SOCKET Socket)
{
    u_long One = 1;

    return ioctlsocket(Socket, FIONBIO, &One) == 0;
}

static void
SetNoDelay(
    _In_ SOCKET Socket)
{
    BOOL Value = TRUE;

    setsockopt(Socket, IPPROTO_TCP, TCP_NODELAY, (const char *)&Value, sizeof(Value));
}

static void
SetRecvBuffer(
    _In_ SOCKET Socket)
{
    int Value = 65535;

    setsockopt(Socket, SOL_SOCKET, SO_RCVBUF, (const char *)&Value, sizeof(Value));
}

static int
WaitReadable(
    _In_ SOCKET Socket,
    _In_ ULONG TimeoutMs,
    _Out_ PULONG ElapsedMs)
{
    struct timeval Timeout;
    fd_set ReadSet, ExceptSet;
    ULONG Start;
    int Result;

    FD_ZERO(&ReadSet);
    FD_SET(Socket, &ReadSet);
    FD_ZERO(&ExceptSet);
    FD_SET(Socket, &ExceptSet);
    Timeout.tv_sec = TimeoutMs / 1000;
    Timeout.tv_usec = (TimeoutMs % 1000) * 1000;

    Start = GetTickCount();
    Result = select(0, &ReadSet, NULL, &ExceptSet, &Timeout);
    *ElapsedMs = GetTickCount() - Start;

    if (Result == 1 && FD_ISSET(Socket, &ExceptSet) && !FD_ISSET(Socket, &ReadSet))
        return -2;

    if (Result == 1 && !FD_ISSET(Socket, &ReadSet))
        return -3;

    return Result;
}

static SOCKET
AcceptLikeNspr(
    _In_ SOCKET Listener,
    _In_ ULONG Round,
    _Out_ PULONG ElapsedMs,
    _Out_ PULONG Waits)
{
    ULONG Start = GetTickCount();
    ULONG Remaining = ACCEPT_TIMEOUT_MS;
    SOCKET Accepted;

    *Waits = 0;

    for (;;)
    {
        struct timeval Timeout;
        fd_set ReadSet;
        ULONG WaitStart, WaitElapsed;
        int Error, Result;

        Accepted = accept(Listener, NULL, NULL);
        if (Accepted != INVALID_SOCKET)
            break;

        Error = WSAGetLastError();
        if (Error != WSAEWOULDBLOCK)
        {
            ok(FALSE, "round %lu: accept failed with %d\n", Round, Error);
            break;
        }

        FD_ZERO(&ReadSet);
        FD_SET(Listener, &ReadSet);
        Timeout.tv_sec = Remaining / 1000;
        Timeout.tv_usec = (Remaining % 1000) * 1000;

        ++*Waits;
        WaitStart = GetTickCount();
        Result = select(0, &ReadSet, NULL, NULL, &Timeout);
        WaitElapsed = GetTickCount() - WaitStart;

        if (Result == SOCKET_ERROR)
        {
            ok(FALSE, "round %lu: listener select failed with %d\n", Round, WSAGetLastError());
            break;
        }

        if (Result == 0)
        {
            if (WaitElapsed >= Remaining)
            {
                ok(FALSE, "round %lu: accept timed out after %lu waits\n", Round, *Waits);
                break;
            }

            Remaining -= WaitElapsed;
        }
    }

    *ElapsedMs = GetTickCount() - Start;
    return Accepted;
}

static BOOL
CreatePair(
    _In_ ULONG Round,
    _In_ BOOL TuneRecvBuffer,
    _Out_ SOCKET *Reader,
    _Out_ SOCKET *Writer)
{
    struct sockaddr_in Address;
    SOCKET Listener = INVALID_SOCKET;
    SOCKET Client = INVALID_SOCKET;
    SOCKET Accepted = INVALID_SOCKET;
    ULONG AcceptElapsed, AcceptWaits;
    int AddressLength;
    int Result;

    *Reader = INVALID_SOCKET;
    *Writer = INVALID_SOCKET;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Listener == INVALID_SOCKET)
    {
        ok(FALSE, "round %lu: listener socket failed with %d\n", Round, WSAGetLastError());
        return FALSE;
    }

    ok(SetNonBlocking(Listener), "round %lu: listener FIONBIO failed with %d\n", Round, WSAGetLastError());
    if (TuneRecvBuffer)
        SetRecvBuffer(Listener);
    SetNoDelay(Listener);

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Address.sin_port = 0;

    Result = bind(Listener, (const struct sockaddr *)&Address, sizeof(Address));
    if (Result == SOCKET_ERROR)
    {
        ok(FALSE, "round %lu: bind failed with %d\n", Round, WSAGetLastError());
        goto Failed;
    }

    AddressLength = sizeof(Address);
    Result = getsockname(Listener, (struct sockaddr *)&Address, &AddressLength);
    if (Result == SOCKET_ERROR)
    {
        ok(FALSE, "round %lu: getsockname failed with %d\n", Round, WSAGetLastError());
        goto Failed;
    }

    Result = listen(Listener, 5);
    if (Result == SOCKET_ERROR)
    {
        ok(FALSE, "round %lu: listen failed with %d\n", Round, WSAGetLastError());
        goto Failed;
    }

    Client = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Client == INVALID_SOCKET)
    {
        ok(FALSE, "round %lu: client socket failed with %d\n", Round, WSAGetLastError());
        goto Failed;
    }

    ok(SetNonBlocking(Client), "round %lu: client FIONBIO failed with %d\n", Round, WSAGetLastError());
    if (TuneRecvBuffer)
        SetRecvBuffer(Client);
    SetNoDelay(Client);

    Result = connect(Client, (const struct sockaddr *)&Address, sizeof(Address));
    if (Result == SOCKET_ERROR)
    {
        int Error = WSAGetLastError();

        ok(Error == WSAEWOULDBLOCK, "round %lu: connect failed with %d\n", Round, Error);
        if (Error != WSAEWOULDBLOCK)
            goto Failed;
    }

    Accepted = AcceptLikeNspr(Listener, Round, &AcceptElapsed, &AcceptWaits);
    if (Accepted == INVALID_SOCKET)
        goto Failed;

    ok(AcceptElapsed < READY_LIMIT_MS,
       "round %lu: accept took %lu ms after %lu waits\n",
       Round,
       AcceptElapsed,
       AcceptWaits);

    if (TuneRecvBuffer)
        SetRecvBuffer(Accepted);
    SetNoDelay(Accepted);

    closesocket(Listener);

    *Reader = Accepted;
    *Writer = Client;
    return TRUE;

Failed:
    if (Listener != INVALID_SOCKET)
        closesocket(Listener);
    if (Client != INVALID_SOCKET)
        closesocket(Client);
    if (Accepted != INVALID_SOCKET)
        closesocket(Accepted);
    return FALSE;
}

static void
Clear(
    _In_ SOCKET Reader,
    _In_ ULONG Round,
    _In_ char Expected)
{
    char Buffer[2048];
    int Result;
    int Error;

    Result = recv(Reader, Buffer, sizeof(Buffer), 0);
    ok(Result == 1 && Buffer[0] == Expected,
       "round %lu: first recv returned %d (%c) error %d\n",
       Round,
       Result,
       Result == 1 ? Buffer[0] : '?',
       Result == SOCKET_ERROR ? WSAGetLastError() : 0);

    Result = recv(Reader, Buffer, sizeof(Buffer), 0);
    Error = Result == SOCKET_ERROR ? WSAGetLastError() : 0;
    ok(Result == SOCKET_ERROR && Error == WSAEWOULDBLOCK,
       "round %lu: drain recv returned %d error %d\n",
       Round,
       Result,
       Error);
}

static DWORD
WINAPI
SignalThread(
    _In_ PVOID Parameter)
{
    PSIGNAL_CONTEXT Context = Parameter;

    Sleep(SIGNAL_DELAY_MS);
    Context->Result = send(Context->Writer, "M", 1, 0);
    Context->Error = Context->Result == SOCKET_ERROR ? WSAGetLastError() : 0;
    return 0;
}

static void
TestPairRound(
    _In_ ULONG Round,
    _In_ BOOL TuneRecvBuffer)
{
    SOCKET Reader, Writer;
    SIGNAL_CONTEXT Context;
    HANDLE Thread;
    ULONG Elapsed;
    int Result;

    if (!CreatePair(Round, TuneRecvBuffer, &Reader, &Writer))
        return;

    Result = WaitReadable(Reader, 0, &Elapsed);
    ok(Result == 0, "round %lu: idle reader select returned %d\n", Round, Result);

    Result = send(Writer, "I", 1, 0);
    ok(Result == 1, "round %lu: prime send returned %d error %d\n", Round, Result, WSAGetLastError());

    Result = WaitReadable(Reader, READY_TIMEOUT_MS, &Elapsed);
    ok(Result == 1, "round %lu: primed reader select returned %d error %d after %lu ms\n", Round, Result, WSAGetLastError(), Elapsed);
    ok(Elapsed < READY_LIMIT_MS, "round %lu: primed reader ready after %lu ms\n", Round, Elapsed);
    if (Result == 1)
        Clear(Reader, Round, 'I');

    Context.Writer = Writer;
    Context.Result = 0;
    Context.Error = 0;
    Thread = CreateThread(NULL, 0, SignalThread, &Context, 0, NULL);
    ok(Thread != NULL, "round %lu: CreateThread failed with %lu\n", Round, GetLastError());

    Result = WaitReadable(Reader, READY_TIMEOUT_MS, &Elapsed);
    ok(Result == 1, "round %lu: signalled reader select returned %d error %d after %lu ms\n", Round, Result, WSAGetLastError(), Elapsed);
    ok(Elapsed >= SIGNAL_DELAY_MS / 2 && Elapsed < READY_LIMIT_MS, "round %lu: signalled reader ready after %lu ms\n", Round, Elapsed);

    if (Thread)
    {
        WaitForSingleObject(Thread, INFINITE);
        CloseHandle(Thread);
        ok(Context.Result == 1, "round %lu: signal send returned %d error %d\n", Round, Context.Result, Context.Error);
    }

    if (Result == 1)
        Clear(Reader, Round, 'M');

    closesocket(Writer);

    Result = WaitReadable(Reader, READY_TIMEOUT_MS, &Elapsed);
    ok(Result == 1, "round %lu: closed reader select returned %d after %lu ms\n", Round, Result, Elapsed);
    ok(Elapsed < READY_LIMIT_MS, "round %lu: closed reader ready after %lu ms\n", Round, Elapsed);

    closesocket(Reader);
}

static void
TestIdleNonBlockingAccept(void)
{
    struct sockaddr_in Address;
    SOCKET Listener;
    SOCKET Accepted;
    ULONG Start, Elapsed;
    int Error, Result;

    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Listener == INVALID_SOCKET)
    {
        ok(FALSE, "idle listener socket failed with %d\n", WSAGetLastError());
        return;
    }

    ok(SetNonBlocking(Listener), "idle listener FIONBIO failed with %d\n", WSAGetLastError());

    ZeroMemory(&Address, sizeof(Address));
    Address.sin_family = AF_INET;
    Address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Address.sin_port = 0;

    Result = bind(Listener, (const struct sockaddr *)&Address, sizeof(Address));
    ok(Result == 0, "idle listener bind failed with %d\n", WSAGetLastError());
    Result = listen(Listener, 5);
    ok(Result == 0, "idle listener listen failed with %d\n", WSAGetLastError());

    Start = GetTickCount();
    Accepted = accept(Listener, NULL, NULL);
    Error = Accepted == INVALID_SOCKET ? WSAGetLastError() : 0;
    Elapsed = GetTickCount() - Start;
    ok(Accepted == INVALID_SOCKET && Error == WSAEWOULDBLOCK,
       "idle nonblocking accept returned %Iu error %d\n",
       (ULONG_PTR)Accepted,
       Error);
    ok(Elapsed < IMMEDIATE_LIMIT_MS, "idle nonblocking accept took %lu ms\n", Elapsed);
    if (Accepted != INVALID_SOCKET)
        closesocket(Accepted);

    Result = WaitReadable(Listener, ACCEPT_TIMEOUT_MS, &Elapsed);
    ok(Result == 0, "idle listener select returned %d\n", Result);
    ok(Elapsed >= ACCEPT_TIMEOUT_MS / 2 && Elapsed < READY_LIMIT_MS,
       "idle listener select waited %lu ms\n",
       Elapsed);

    closesocket(Listener);
}

START_TEST(pollable_event)
{
    WSADATA WsaData;
    ULONG Round;
    int Result;

    Result = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (Result != 0)
    {
        skip("WSAStartup failed with %d\n", Result);
        return;
    }

    TestIdleNonBlockingAccept();

    for (Round = 0; Round < PAIR_ROUNDS; ++Round)
        TestPairRound(Round, (Round & 1) == 0);

    WSACleanup();
}
