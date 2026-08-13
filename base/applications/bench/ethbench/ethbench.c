#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ETHBENCH_BUFSZ (256 * 1024)
#define ETHBENCH_SOBUF (1024 * 1024)
#define ETHBENCH_MAXTHREADS 32

static char gMode;
static const char *gIp;
static int gPort;
static int gSeconds;
static volatile LONG64 gTotal;
static volatile LONG64 gPeakBps;
static volatile LONG gDataStart;
static volatile LONG gDataEnd;
static BOOL gLoopback;
static HANDLE gLoopReady;
static volatile LONG gLoopFailed;
static const char *gPath = "/";
static const char *gHost = "";

static void
SetSocketBuffers(SOCKET Sock)
{
    int v = ETHBENCH_SOBUF;
    setsockopt(Sock, SOL_SOCKET, SO_SNDBUF, (const char *)&v, sizeof(v));
    setsockopt(Sock, SOL_SOCKET, SO_RCVBUF, (const char *)&v, sizeof(v));
    v = 1;
    setsockopt(Sock, IPPROTO_TCP, TCP_NODELAY, (const char *)&v, sizeof(v));
}

static int
WaitForSocket(SOCKET Sock, BOOL Write)
{
    fd_set Fds;
    struct timeval Tv;

    FD_ZERO(&Fds);
    FD_SET(Sock, &Fds);
    Tv.tv_sec = 1;
    Tv.tv_usec = 0;
    return select(0, Write ? NULL : &Fds, Write ? &Fds : NULL, NULL, &Tv);
}

static void
UpdatePeak(LONG64 Bps)
{
    for (;;)
    {
        LONG64 Old = gPeakBps;
        if (Bps <= Old)
            break;
        if (InterlockedCompareExchange64(&gPeakBps, Bps, Old) == Old)
            break;
    }
}

static void
FinishMeasurement(DWORD Start)
{
    DWORD End = GetTickCount();
    LONG Old;

    InterlockedCompareExchange(&gDataStart, (LONG)Start, 0);
    for (;;)
    {
        Old = gDataEnd;
        if (Old != 0 && (LONG)(End - (DWORD)Old) <= 0)
            break;
        if (InterlockedCompareExchange(&gDataEnd, (LONG)End, Old) == Old)
            break;
    }
}

static DWORD WINAPI
LoopbackPeer(LPVOID Param)
{
    SOCKET Listener = INVALID_SOCKET;
    SOCKET Peer = INVALID_SOCKET;
    struct sockaddr_in Addr;
    char *Buf = NULL;
    DWORD Started;
    u_long NonBlocking = 1;
    int n;

    (void)Param;
    Listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Listener == INVALID_SOCKET)
        goto Failure;

    SetSocketBuffers(Listener);
    memset(&Addr, 0, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons((u_short)gPort);
    Addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(Listener, (struct sockaddr *)&Addr, sizeof(Addr)) == SOCKET_ERROR ||
        listen(Listener, 1) == SOCKET_ERROR)
    {
        goto Failure;
    }

    SetEvent(gLoopReady);
    Peer = accept(Listener, NULL, NULL);
    if (Peer == INVALID_SOCKET)
        goto Failure;

    SetSocketBuffers(Peer);
    if (ioctlsocket(Peer, FIONBIO, &NonBlocking) == SOCKET_ERROR)
        goto Failure;

    Buf = (char *)malloc(ETHBENCH_BUFSZ);
    if (!Buf)
        goto Failure;
    memset(Buf, 0xA5, ETHBENCH_BUFSZ);

    Started = GetTickCount();
    if (gMode == 'T')
    {
        while ((GetTickCount() - Started) < (DWORD)((gSeconds + 5) * 1000))
        {
            n = recv(Peer, Buf, ETHBENCH_BUFSZ, 0);
            if (n == 0)
                break;
            if (n == SOCKET_ERROR)
            {
                if (WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    if (WaitForSocket(Peer, FALSE) == SOCKET_ERROR)
                        break;
                    continue;
                }
                break;
            }
        }
    }
    else
    {
        while ((GetTickCount() - Started) < (DWORD)((gSeconds + 5) * 1000))
        {
            n = send(Peer, Buf, ETHBENCH_BUFSZ, 0);
            if (n == 0)
                break;
            if (n == SOCKET_ERROR)
            {
                if (WSAGetLastError() == WSAEWOULDBLOCK)
                {
                    if (WaitForSocket(Peer, TRUE) == SOCKET_ERROR)
                        break;
                    continue;
                }
                break;
            }
        }
    }

    free(Buf);
    closesocket(Peer);
    closesocket(Listener);
    return 0;

Failure:
    InterlockedExchange(&gLoopFailed, 1);
    SetEvent(gLoopReady);
    if (Buf)
        free(Buf);
    if (Peer != INVALID_SOCKET)
        closesocket(Peer);
    if (Listener != INVALID_SOCKET)
        closesocket(Listener);
    return 1;
}

static BOOL
TransferEndedEarly(DWORD Start, int Result)
{
    DWORD Elapsed = GetTickCount() - Start;
    char Dbg[96];

    if (Elapsed + 1000 >= (DWORD)(gSeconds * 1000))
        return FALSE;

    _snprintf(Dbg, sizeof(Dbg), "ETHBENCH_IO_FAILED mode=%c result=%d error=%d elapsed_ms=%lu\n", gMode, Result, Result == SOCKET_ERROR ? WSAGetLastError() : 0, Elapsed);
    OutputDebugStringA(Dbg);
    return TRUE;
}

static DWORD WINAPI
Worker(LPVOID Param)
{
    SOCKET Sock;
    struct sockaddr_in Addr;
    char *Buf;
    int n;
    DWORD Start;
    unsigned __int64 Total = 0;
    BOOL Failed = FALSE;

    (void)Param;
    Buf = (char *)malloc(ETHBENCH_BUFSZ);
    if (!Buf)
        return 1;
    memset(Buf, 0xA5, ETHBENCH_BUFSZ);

    if (gMode == 'U')
    {
        SOCKET Us;
        struct sockaddr_in Sa, From;
        int FromLen;
        DWORD Ut0, UwinStart;
        unsigned __int64 Ubytes = 0, UwinBytes = 0;
        int Rcvto = 2000;

        Us = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (Us == INVALID_SOCKET)
        {
            free(Buf);
            return 1;
        }
        SetSocketBuffers(Us);
        setsockopt(Us, SOL_SOCKET, SO_RCVTIMEO, (const char *)&Rcvto, sizeof(Rcvto));

        memset(&Sa, 0, sizeof(Sa));
        Sa.sin_family = AF_INET;
        Sa.sin_port = htons((u_short)gPort);
        Sa.sin_addr.s_addr = inet_addr(gIp);

        sendto(Us, "U", 1, 0, (struct sockaddr *)&Sa, sizeof(Sa));
        OutputDebugStringA("ETHBENCH_UDP_START\n");

        Ut0 = GetTickCount();
        UwinStart = Ut0;
        while ((GetTickCount() - Ut0) < (DWORD)(gSeconds * 1000))
        {
            DWORD Span;
            fd_set Rfds;
            struct timeval Tv;

            FD_ZERO(&Rfds);
            FD_SET(Us, &Rfds);
            Tv.tv_sec = 1;
            Tv.tv_usec = 0;
            if (select(0, &Rfds, NULL, NULL, &Tv) <= 0)
            {
                sendto(Us, "U", 1, 0, (struct sockaddr *)&Sa, sizeof(Sa));
                continue;
            }
            FromLen = sizeof(From);
            n = recvfrom(Us, Buf, ETHBENCH_BUFSZ, 0, (struct sockaddr *)&From, &FromLen);
            if (n <= 0)
                continue;
            Ubytes += (unsigned __int64)n;
            UwinBytes += (unsigned __int64)n;
            Span = GetTickCount() - UwinStart;
            if (Span >= 1000)
            {
                LONG64 Bps = (LONG64)(UwinBytes * 1000 / Span);
                UpdatePeak(Bps);
                UwinStart = GetTickCount();
                UwinBytes = 0;
            }
        }
        FinishMeasurement(Ut0);
        InterlockedAdd64(&gTotal, (LONG64)Ubytes);
        closesocket(Us);
        free(Buf);
        return 0;
    }

    Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (Sock == INVALID_SOCKET)
    {
        free(Buf);
        return 1;
    }
    SetSocketBuffers(Sock);

    memset(&Addr, 0, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons((u_short)gPort);
    Addr.sin_addr.s_addr = inet_addr(gIp);

    {
        u_long Nb = 1;
        fd_set Wfds;
        struct timeval Tv;
        int SoErr = 0, SoLen = sizeof(SoErr);

        ioctlsocket(Sock, FIONBIO, &Nb);
        connect(Sock, (struct sockaddr *)&Addr, sizeof(Addr));
        FD_ZERO(&Wfds);
        FD_SET(Sock, &Wfds);
        Tv.tv_sec = 10;
        Tv.tv_usec = 0;
        if (select(0, NULL, &Wfds, NULL, &Tv) <= 0)
        {
            OutputDebugStringA("ETHBENCH_CONNECT timeout\n");
            closesocket(Sock);
            free(Buf);
            return 1;
        }
        getsockopt(Sock, SOL_SOCKET, SO_ERROR, (char *)&SoErr, &SoLen);
        if (SoErr != 0)
        {
            char Dbg[64];
            _snprintf(Dbg, sizeof(Dbg), "ETHBENCH_CONNECT failed err=%d\n", SoErr);
            OutputDebugStringA(Dbg);
            closesocket(Sock);
            free(Buf);
            return 1;
        }
        Nb = 0;
        ioctlsocket(Sock, FIONBIO, &Nb);
    }
    if (gMode == 'H')
    {
        int ReqLen = _snprintf(Buf, ETHBENCH_BUFSZ, "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\nUser-Agent: ethbench\r\n\r\n", gPath, gHost);
        if (ReqLen <= 0 || send(Sock, Buf, ReqLen, 0) != ReqLen)
        {
            closesocket(Sock);
            free(Buf);
            return 1;
        }
    }
    else if (!gLoopback && send(Sock, &gMode, 1, 0) != 1)
    {
        closesocket(Sock);
        free(Buf);
        return 1;
    }

    {
        u_long Nb = 1;
        if (ioctlsocket(Sock, FIONBIO, &Nb) == SOCKET_ERROR)
        {
            closesocket(Sock);
            free(Buf);
            return 1;
        }
    }

    Start = GetTickCount();
    InterlockedCompareExchange(&gDataStart, (LONG)Start, 0);
    if (gMode == 'T')
    {
        DWORD WinStart = Start;
        unsigned __int64 WinBytes = 0;
        while ((GetTickCount() - Start) < (DWORD)(gSeconds * 1000))
        {
            DWORD Span;
            n = send(Sock, Buf, ETHBENCH_BUFSZ, 0);
            if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
            {
                if (WaitForSocket(Sock, TRUE) == SOCKET_ERROR)
                {
                    Failed = TransferEndedEarly(Start, SOCKET_ERROR);
                    break;
                }
                continue;
            }
            if (n <= 0)
            {
                Failed = TransferEndedEarly(Start, n);
                break;
            }
            Total += (unsigned __int64)n;
            WinBytes += (unsigned __int64)n;
            Span = GetTickCount() - WinStart;
            if (Span >= 1000)
            {
                UpdatePeak((LONG64)(WinBytes * 1000 / Span));
                WinStart = GetTickCount();
                WinBytes = 0;
            }
        }
    }
    else
    {
        DWORD WinStart = Start;
        unsigned __int64 WinBytes = 0;

        while ((GetTickCount() - Start) < (DWORD)(gSeconds * 1000))
        {
            DWORD Span;
            n = recv(Sock, Buf, ETHBENCH_BUFSZ, 0);
            if (n == SOCKET_ERROR && WSAGetLastError() == WSAEWOULDBLOCK)
            {
                if (WaitForSocket(Sock, FALSE) == SOCKET_ERROR)
                {
                    Failed = gMode != 'H' && TransferEndedEarly(Start, SOCKET_ERROR);
                    break;
                }
                continue;
            }
            if (n <= 0)
            {
                Failed = gMode != 'H' && TransferEndedEarly(Start, n);
                break;
            }
            Total += (unsigned __int64)n;
            WinBytes += (unsigned __int64)n;
            Span = GetTickCount() - WinStart;
            if (Span >= 1000)
            {
                LONG64 Bps = (LONG64)(WinBytes * 1000 / Span);
                UpdatePeak(Bps);
                WinStart = GetTickCount();
                WinBytes = 0;
            }
        }
    }

    FinishMeasurement(Start);
    InterlockedAdd64(&gTotal, (LONG64)Total);
    if (gLoopback)
        shutdown(Sock, SD_BOTH);
    closesocket(Sock);
    free(Buf);
    return Failed ? 1 : 0;
}

int
main(int argc, char **argv)
{
    WSADATA Wsa;
    HANDLE Threads[ETHBENCH_MAXTHREADS];
    HANDLE PeerThread = NULL;
    int NThreads, i;
    DWORD ExitCode;
    BOOL Failed = FALSE;
    double Secs, Mbps, PeakMbps;

    if (argc < 5)
    {
        printf("usage: ethbench <tx|rx|loop-tx|loop-rx> <server-ip> <port> <seconds> [threads]\n");
        return 1;
    }

    if (_stricmp(argv[1], "loop-tx") == 0)
    {
        gLoopback = TRUE;
        gMode = 'T';
    }
    else if (_stricmp(argv[1], "loop-rx") == 0)
    {
        gLoopback = TRUE;
        gMode = 'R';
    }
    else if (argv[1][0] == 'h' || argv[1][0] == 'H')
        gMode = 'H';
    else if (argv[1][0] == 'u' || argv[1][0] == 'U')
        gMode = 'U';
    else if (argv[1][0] == 'r' || argv[1][0] == 'R')
        gMode = 'R';
    else
        gMode = 'T';
    gIp = argv[2];
    gPort = atoi(argv[3]);
    gSeconds = atoi(argv[4]);
    if (gSeconds <= 0)
        gSeconds = 10;
    NThreads = (argc >= 6) ? atoi(argv[5]) : 1;
    if (NThreads < 1)
        NThreads = 1;
    if (NThreads > ETHBENCH_MAXTHREADS)
        NThreads = ETHBENCH_MAXTHREADS;
    if (gLoopback && NThreads != 1)
    {
        printf("ethbench: loopback supports one TCP stream\n");
        return 1;
    }
    if (gMode == 'H')
    {
        gPath = (argc >= 7) ? argv[6] : "/";
        gHost = (argc >= 8) ? argv[7] : gIp;
    }

    if (WSAStartup(MAKEWORD(2, 2), &Wsa) != 0)
    {
        printf("ethbench: WSAStartup failed\n");
        return 1;
    }
    if (gLoopback)
    {
        gLoopReady = CreateEvent(NULL, TRUE, FALSE, NULL);
        if (!gLoopReady)
        {
            WSACleanup();
            return 1;
        }
        gLoopFailed = 0;
        PeerThread = CreateThread(NULL, 0, LoopbackPeer, NULL, 0, NULL);
        if (!PeerThread ||
            WaitForSingleObject(gLoopReady, 10000) != WAIT_OBJECT_0 ||
            gLoopFailed)
        {
            if (PeerThread)
                CloseHandle(PeerThread);
            CloseHandle(gLoopReady);
            WSACleanup();
            return 1;
        }
    }

    printf("ethbench: %s %s:%d mode=%c dur=%ds threads=%d scope=%s\n", (gMode == 'T') ? "tx" : "rx", gIp, gPort, gMode, gSeconds, NThreads, gLoopback ? "loopback" : "network");

    gTotal = 0;
    gPeakBps = 0;
    gDataStart = 0;
    gDataEnd = 0;
    for (i = 0; i < NThreads; i++)
    {
        Threads[i] = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
        if (!Threads[i])
            Failed = TRUE;
    }
    for (i = 0; i < NThreads; i++)
    {
        if (Threads[i])
        {
            WaitForSingleObject(Threads[i], INFINITE);
            if (!GetExitCodeThread(Threads[i], &ExitCode) || ExitCode != 0)
                Failed = TRUE;
            CloseHandle(Threads[i]);
        }
    }
    if (PeerThread)
    {
        if (WaitForSingleObject(PeerThread, 7000) != WAIT_OBJECT_0)
            Failed = TRUE;
        else if (!GetExitCodeThread(PeerThread, &ExitCode) || ExitCode != 0)
            Failed = TRUE;
        CloseHandle(PeerThread);
        CloseHandle(gLoopReady);
    }

    Secs = (gDataStart != 0 && gDataEnd != 0) ?
           ((DWORD)gDataEnd - (DWORD)gDataStart) / 1000.0 : 0.0;
    Mbps = (Secs > 0.0) ? ((double)gTotal * 8.0 / Secs / 1.0e6) : 0.0;
    PeakMbps = (double)gPeakBps * 8.0 / 1.0e6;
    printf("ETHBENCH_RESULT scope=%s mode=%c threads=%d bytes=%I64u secs=%.2f mbps=%.1f MBps=%.1f peakmbps=%.1f\n", gLoopback ? "loopback" : "network", gMode, NThreads, (unsigned __int64)gTotal, Secs, Mbps, Mbps / 8.0, PeakMbps);
    {
        char Dbg[256];
        _snprintf(Dbg, sizeof(Dbg), "ETHBENCH_RESULT scope=%s mode=%c threads=%d bytes=%I64u secs=%.2f mbps=%.1f peakmbps=%.1f\n", gLoopback ? "loopback" : "network", gMode, NThreads, (unsigned __int64)gTotal, Secs, Mbps, PeakMbps);
        OutputDebugStringA(Dbg);
    }

    WSACleanup();
    return (Failed || gTotal == 0) ? 1 : 0;
}
