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

static DWORD WINAPI
Worker(LPVOID Param)
{
    SOCKET Sock;
    struct sockaddr_in Addr;
    char *Buf;
    int n;
    DWORD Start;
    unsigned __int64 Total = 0;

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
        OutputDebugStringA("WIFIDL_UDP_START\n");

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
                for (;;)
                {
                    LONG64 Old = gPeakBps;
                    if (Bps <= Old)
                        break;
                    if (InterlockedCompareExchange64(&gPeakBps, Bps, Old) == Old)
                        break;
                }
                UwinStart = GetTickCount();
                UwinBytes = 0;
            }
        }
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
        DWORD ConnT0 = GetTickCount();

        ioctlsocket(Sock, FIONBIO, &Nb);
        connect(Sock, (struct sockaddr *)&Addr, sizeof(Addr));
        FD_ZERO(&Wfds);
        FD_SET(Sock, &Wfds);
        Tv.tv_sec = 10;
        Tv.tv_usec = 0;
        if (select(0, NULL, &Wfds, NULL, &Tv) <= 0)
        {
            OutputDebugStringA("WIFIDL_CONNECT timeout\n");
            closesocket(Sock);
            free(Buf);
            return 1;
        }
        getsockopt(Sock, SOL_SOCKET, SO_ERROR, (char *)&SoErr, &SoLen);
        if (SoErr != 0)
        {
            char Dbg[64];
            _snprintf(Dbg, sizeof(Dbg), "WIFIDL_CONNECT failed err=%d\n", SoErr);
            OutputDebugStringA(Dbg);
            closesocket(Sock);
            free(Buf);
            return 1;
        }
        OutputDebugStringA("WIFIDL_CONNECT ok\n");
        {
            char RDbg[48];
            _snprintf(RDbg, sizeof(RDbg), "WIFIDL_RTT_MS %lu\n", GetTickCount() - ConnT0);
            OutputDebugStringA(RDbg);
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
    else if (send(Sock, &gMode, 1, 0) != 1)
    {
        closesocket(Sock);
        free(Buf);
        return 1;
    }

    Start = GetTickCount();
    if (gMode == 'T')
    {
        while ((GetTickCount() - Start) < (DWORD)(gSeconds * 1000))
        {
            n = send(Sock, Buf, ETHBENCH_BUFSZ, 0);
            if (n <= 0)
                break;
            Total += (unsigned __int64)n;
        }
    }
    else
    {
        DWORD WinStart = Start;
        unsigned __int64 WinBytes = 0;
        DWORD MaxGap = 0, RvT0;
        while ((GetTickCount() - Start) < (DWORD)(gSeconds * 1000))
        {
            DWORD Span;
            RvT0 = GetTickCount();
            n = recv(Sock, Buf, ETHBENCH_BUFSZ, 0);
            {
                DWORD G = GetTickCount() - RvT0;
                if (G > MaxGap)
                {
                    char GDbg[48];
                    MaxGap = G;
                    _snprintf(GDbg, sizeof(GDbg), "WIFIDL_MAXGAP_MS %lu\n", MaxGap);
                    OutputDebugStringA(GDbg);
                }
            }
            if (n <= 0)
                break;
            Total += (unsigned __int64)n;
            WinBytes += (unsigned __int64)n;
            Span = GetTickCount() - WinStart;
            if (Span >= 1000)
            {
                LONG64 Bps = (LONG64)(WinBytes * 1000 / Span);
                for (;;)
                {
                    LONG64 Old = gPeakBps;
                    if (Bps <= Old)
                        break;
                    if (InterlockedCompareExchange64(&gPeakBps, Bps, Old) == Old)
                        break;
                }
                WinStart = GetTickCount();
                WinBytes = 0;
            }
        }
    }

    InterlockedAdd64(&gTotal, (LONG64)Total);
    closesocket(Sock);
    free(Buf);
    return 0;
}

int
main(int argc, char **argv)
{
    WSADATA Wsa;
    HANDLE Threads[ETHBENCH_MAXTHREADS];
    int NThreads, i;
    DWORD Start, Now;
    double Secs, Mbps, PeakMbps;

    if (argc < 5)
    {
        printf("usage: ethbench <tx|rx> <server-ip> <port> <seconds> [threads]\n");
        return 1;
    }

    if (argv[1][0] == 'h' || argv[1][0] == 'H')
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

    printf("ethbench: %s %s:%d mode=%c dur=%ds threads=%d\n", (gMode == 'T') ? "tx" : "rx", gIp, gPort, gMode, gSeconds, NThreads);

    gTotal = 0;
    gPeakBps = 0;
    Start = GetTickCount();
    for (i = 0; i < NThreads; i++)
        Threads[i] = CreateThread(NULL, 0, Worker, NULL, 0, NULL);
    for (i = 0; i < NThreads; i++)
    {
        if (Threads[i])
        {
            WaitForSingleObject(Threads[i], INFINITE);
            CloseHandle(Threads[i]);
        }
    }
    Now = GetTickCount();

    Secs = (Now - Start) / 1000.0;
    Mbps = (Secs > 0.0) ? ((double)gTotal * 8.0 / Secs / 1.0e6) : 0.0;
    PeakMbps = (double)gPeakBps * 8.0 / 1.0e6;
    printf("WIFIDL_RESULT mode=%c threads=%d bytes=%I64u secs=%.2f mbps=%.1f MBps=%.1f peakmbps=%.1f\n", gMode, NThreads, (unsigned __int64)gTotal, Secs, Mbps, Mbps / 8.0, PeakMbps);
    {
        char Dbg[256];
        _snprintf(Dbg, sizeof(Dbg), "WIFIDL_RESULT mode=%c threads=%d bytes=%I64u secs=%.2f mbps=%.1f peakmbps=%.1f\n", gMode, NThreads, (unsigned __int64)gTotal, Secs, Mbps, PeakMbps);
        OutputDebugStringA(Dbg);
    }

    WSACleanup();
    return 0;
}
