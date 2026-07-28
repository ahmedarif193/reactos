/*
 * PROJECT:     ReactOS VMX Hypervisor Launcher (rosl_netd.exe)
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     User-mode network backend with full NAT (TCP/UDP/DNS forwarding)
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * NOTE:
 * This process attaches a backend and polls NET_RX / NET_GET_STATS.
 * It implements a full user-space NAT similar to QEMU's SLiRP:
 *   - ARP replies for gateway (10.0.2.2) and DNS (10.0.2.3)
 *   - DHCP server for guest IP assignment
 *   - ICMP echo reply for gateway
 *   - TCP proxy: full connection tracking with seq/ack translation
 *   - UDP forwarding: stateful NAT for UDP sessions
 *   - DNS forwarding: guest DNS to host real DNS or 8.8.8.8 fallback
 * A background receive thread polls host sockets and injects response
 * packets back to the guest.
 *
 * TODO:
 * - Add IPv6 support (NDP/RA + NAT66 or proxy mode).
 * - Add inbound port-forwarding (host->guest NAT rules).
 * - Add IPv4 fragment reassembly + outbound fragmentation path.
 * - Add richer ICMP translation (dest-unreach/time-exceeded propagation).
 * - Harden TCP state tracking (options/window scaling/SACK-aware behavior).
 * - Move NAT lookup from linear scan to hashed buckets for scale.
 */

/* Ensure select() can monitor the full NAT table. Must be set before winsock2.h. */
#if !defined(FD_SETSIZE) || (FD_SETSIZE < 256)
#undef FD_SETSIZE
#define FD_SETSIZE 256
#endif

/* Winsock2 headers MUST come before windows.h */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winioctl.h>
#include <iphlpapi.h>
/* icmpapi.h removed -- persistent ICMP worker uses raw sockets, not IcmpSendEcho */
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "log_style.h"

/* DbgPrint from ntdll - writes to kernel debug output (serial port) */
ULONG __cdecl DbgPrint(PCSTR Format, ...);

#define ROSV_DEVICE_PATH "\\\\.\\RosvHypervisor"

#define ROSV_IOCTL_TYPE     0x8000
#define ROSV_IOCTL_NET_ATTACH    CTL_CODE(ROSV_IOCTL_TYPE, 0x80C, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_NET_DETACH    CTL_CODE(ROSV_IOCTL_TYPE, 0x80D, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_NET_TX        CTL_CODE(ROSV_IOCTL_TYPE, 0x80E, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_NET_RX        CTL_CODE(ROSV_IOCTL_TYPE, 0x80F, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define ROSV_IOCTL_NET_GET_STATS CTL_CODE(ROSV_IOCTL_TYPE, 0x810, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define ROSV_NET_PACKET_MAX  1600

typedef enum _ROSV_NET_BACKEND_TYPE {
    RosvNetBackendNone = 0,
    RosvNetBackendSlirp = 1,
    RosvNetBackendNetio = 2
} ROSV_NET_BACKEND_TYPE;

typedef struct _ROSV_NET_ATTACH_REQUEST {
    ULONG BackendType;
    ULONG Flags;
    UCHAR GuestMac[6];
    UCHAR Reserved[2];
} ROSV_NET_ATTACH_REQUEST;

typedef struct _ROSV_NET_PACKET {
    ULONG Length;
    UCHAR Data[ROSV_NET_PACKET_MAX];
} ROSV_NET_PACKET;

typedef struct _ROSV_NET_STATS {
    ULONG BackendType;
    ULONG Attached;
    ULONGLONG TxPackets;
    ULONGLONG TxBytes;
    ULONGLONG RxPackets;
    ULONGLONG RxBytes;
} ROSV_NET_STATS;

/* ------------------------------------------------------------------ */
/* Protocol constants                                                   */
/* ------------------------------------------------------------------ */

#define ETH_TYPE_IPV4   0x0800
#define ETH_TYPE_ARP    0x0806
#define IP_PROTO_ICMP   1
#define IP_PROTO_TCP    6
#define IP_PROTO_UDP    17

#define DHCP_CLIENT_PORT    68
#define DHCP_SERVER_PORT    67
#define DHCP_MAGIC_COOKIE   0x63825363UL

#define TCP_FLAG_FIN    0x01
#define TCP_FLAG_SYN    0x02
#define TCP_FLAG_RST    0x04
#define TCP_FLAG_PSH    0x08
#define TCP_FLAG_ACK    0x10

#define TCP_MSS         1460
#define TCP_WINDOW_SIZE 65535

#define ICMP_ECHO_TIMEOUT_MS 2000
#define ICMP_MAX_INFLIGHT 16
#define ICMP_QUEUE_SIZE 32
#define PERF_SLOW_IOCTL_US 5000

typedef struct _NETD_ICMP_ECHO_INFO {
    BOOL Valid;
    BOOL IsRequest;
    UCHAR SrcIp[4];
    UCHAR DstIp[4];
    USHORT Id;
    USHORT Seq;
} NETD_ICMP_ECHO_INFO;

/* ------------------------------------------------------------------ */
/* NAT connection tracking                                              */
/* ------------------------------------------------------------------ */

#define NAT_TABLE_SIZE  256
#define NAT_TCP_TIMEOUT_MS  300000   /* 5 minutes */
#define NAT_TCP_CLOSE_TIMEOUT_MS 30000 /* 30 seconds after FIN from host */
#define NAT_UDP_TIMEOUT_MS   30000   /* 30 seconds */
/* TODO: make table size/runtime caps configurable via CLI args. */

#if NAT_TABLE_SIZE > FD_SETSIZE
#error NAT_TABLE_SIZE must be <= FD_SETSIZE for select()
#endif

typedef enum {
    NAT_PROTO_TCP,
    NAT_PROTO_UDP
} NAT_PROTOCOL;

typedef enum {
    TCP_STATE_SYN_SENT,
    TCP_STATE_ESTABLISHED,
    TCP_STATE_FIN_WAIT,
    TCP_STATE_CLOSE_WAIT,
    TCP_STATE_CLOSED
} TCP_STATE;

typedef struct _NAT_ENTRY {
    BOOL InUse;
    NAT_PROTOCOL Protocol;

    /* Guest-side addressing */
    UCHAR GuestIp[4];
    USHORT GuestPort;

    /* External destination */
    UCHAR DestIp[4];
    USHORT DestPort;

    /* Host-side Winsock socket */
    SOCKET HostSocket;

    /* TCP state machine */
    TCP_STATE TcpState;
    ULONG GuestSeqBase;      /* Guest's ISN (from SYN) */
    ULONG GuestAckBase;      /* Our ISN for guest-facing traffic */
    ULONG GuestSeqNext;      /* Next expected seq from guest */
    ULONG HostAckNext;       /* Next ack to send to guest = GuestAckBase + BytesRecvFromHost */
    ULONG BytesSentToHost;   /* Total payload bytes forwarded to host */
    ULONG BytesRecvFromHost; /* Total payload bytes received from host */

    /* Timeout */
    DWORD LastActivityTick;
    DWORD TimeoutMs;
} NAT_ENTRY;

typedef struct _ICMP_ECHO_TASK {
    UCHAR SrcIp[4];
    UCHAR DstIp[4];
    USHORT IcmpId;
    USHORT IcmpSeq;
    ULONGLONG TraceQueuedQpc;
    ULONG PayloadLen;
    UCHAR Payload[1];
} ICMP_ECHO_TASK;

/*
 * In-flight ICMP echo tracking entry.
 * The persistent ICMP worker thread uses these to match incoming replies
 * back to the original guest request.
 */
typedef struct _ICMP_INFLIGHT_ENTRY {
    BOOL Active;
    USHORT IcmpId;
    USHORT IcmpSeq;
    UCHAR SrcIp[4];   /* Guest source IP (reply destination) */
    UCHAR DstIp[4];   /* External destination */
    ULONGLONG QueuedQpc;
    ULONGLONG SentQpc;
    DWORD DeadlineTick; /* GetTickCount() deadline for timeout */
} ICMP_INFLIGHT_ENTRY;

/* ------------------------------------------------------------------ */
/* Globals                                                              */
/* ------------------------------------------------------------------ */

static HANDLE g_hDev = INVALID_HANDLE_VALUE;
static HANDLE g_hDevTx = INVALID_HANDLE_VALUE;
static volatile BOOL g_Running = TRUE;
static BOOL g_Verbose = FALSE;
static ULONG g_DhcpRxCount = 0;
static ULONG g_DhcpTxCount = 0;
static ULONG g_DhcpTxFailCount = 0;
static ULONG g_RxPacketCount = 0;
static const UCHAR g_HostMac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x02 };
static const UCHAR g_HostIp[4] = { 10, 0, 2, 2 };
static const UCHAR g_GuestIp[4] = { 10, 0, 2, 15 };
static const UCHAR g_GuestMask[4] = { 255, 255, 255, 0 };
static const UCHAR g_DnsIp[4] = { 10, 0, 2, 3 };
static const UCHAR g_GuestMacDefault[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };

/* NAT table and synchronization */
static NAT_ENTRY g_NatTable[NAT_TABLE_SIZE];
static CRITICAL_SECTION g_NatLock;

/* Host DNS server IP (resolved at startup, fallback 8.8.8.8) */
static UCHAR g_HostDnsIp[4] = { 8, 8, 8, 8 };

/* Incrementing IP identification counter */
static volatile LONG g_IpIdCounter = 1;

/* Receive thread handle */
static HANDLE g_RecvThread = NULL;

/* Count of active async ICMP workers (kept for shutdown drain) */
static volatile LONG g_IcmpInFlight = 0;

/* Persistent ICMP worker thread and its shared state */
static HANDLE g_IcmpThread = NULL;
static SOCKET g_IcmpSocket = INVALID_SOCKET;
static CRITICAL_SECTION g_IcmpLock;
static ICMP_ECHO_TASK *g_IcmpQueue[ICMP_QUEUE_SIZE];
static ULONG g_IcmpQueueHead = 0; /* Next slot to dequeue */
static ULONG g_IcmpQueueTail = 0; /* Next slot to enqueue */
static ICMP_INFLIGHT_ENTRY g_IcmpInflight[ICMP_MAX_INFLIGHT];

/* ISN counter for TCP connections */
static volatile LONG g_TcpIsnCounter = 0x10000;

/* Guest MAC learned from first packet (or default) */
static UCHAR g_GuestMac[6] = { 0x52, 0x54, 0x00, 0x12, 0x34, 0x56 };
static LARGE_INTEGER g_PerfQpcFreq = { 0 };

/* ------------------------------------------------------------------ */
/* Forward declarations                                                 */
/* ------------------------------------------------------------------ */

static BOOL
IoctlOnHandle(
    HANDLE DevHandle,
    DWORD Code,
    void *InBuf,
    DWORD InLen,
    void *OutBuf,
    DWORD OutLen,
    DWORD *BytesOut);

static BOOL
Ioctl(
    DWORD Code,
    void *InBuf,
    DWORD InLen,
    void *OutBuf,
    DWORD OutLen,
    DWORD *BytesOut);

static void
NetdLog(
    const char *Fmt,
    ...);

static void
HandleGuestPacket(
    _In_reads_bytes_(Packet->Length) const ROSV_NET_PACKET *Packet);

/* IcmpStatusName removed -- was only used by old thread-per-ping worker */

static ULONGLONG
NetdReadQpc(
    VOID)
{
    LARGE_INTEGER Counter;
    QueryPerformanceCounter(&Counter);
    return (ULONGLONG)Counter.QuadPart;
}

static ULONGLONG
NetdQpcDeltaUs(
    _In_ ULONGLONG StartQpc,
    _In_ ULONGLONG EndQpc)
{
    ULONGLONG DeltaQpc;

    if (EndQpc < StartQpc || g_PerfQpcFreq.QuadPart <= 0)
        return 0;

    DeltaQpc = EndQpc - StartQpc;
    return (DeltaQpc * 1000000ULL) / (ULONGLONG)g_PerfQpcFreq.QuadPart;
}

static void
NetdDecodeIcmpEcho(
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _Out_ NETD_ICMP_ECHO_INFO *Info)
{
    ULONG EthType;
    ULONG Ihl;
    const UCHAR *Ip;
    const UCHAR *Icmp;

    ZeroMemory(Info, sizeof(*Info));

    if (Frame == NULL || FrameLength < 14 + 20 + 8)
        return;

    EthType = ((ULONG)Frame[12] << 8) | Frame[13];
    if (EthType != ETH_TYPE_IPV4)
        return;

    Ip = Frame + 14;
    if ((Ip[0] >> 4) != 4)
        return;

    Ihl = (ULONG)(Ip[0] & 0x0F) * 4;
    if (Ihl < 20 || FrameLength < 14 + Ihl + 8)
        return;

    if (Ip[9] != IP_PROTO_ICMP)
        return;

    Icmp = Ip + Ihl;
    if (!(Icmp[0] == 8 || Icmp[0] == 0) || Icmp[1] != 0)
        return;

    Info->Valid = TRUE;
    Info->IsRequest = (Icmp[0] == 8);
    memcpy(Info->SrcIp, &Ip[12], sizeof(Info->SrcIp));
    memcpy(Info->DstIp, &Ip[16], sizeof(Info->DstIp));
    Info->Id = (USHORT)(((USHORT)Icmp[4] << 8) | Icmp[5]);
    Info->Seq = (USHORT)(((USHORT)Icmp[6] << 8) | Icmp[7]);
}

/* ------------------------------------------------------------------ */
/* Generic IP address extraction (works for any IPv4 packet)            */
/* ------------------------------------------------------------------ */

typedef struct _NETD_IP_ADDR_INFO {
    BOOL Valid;
    UCHAR SrcIp[4];
    UCHAR DstIp[4];
    UCHAR Protocol;
} NETD_IP_ADDR_INFO;

static void
NetdExtractIpAddrs(
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _Out_ NETD_IP_ADDR_INFO *Info)
{
    ULONG EthType;
    const UCHAR *Ip;

    ZeroMemory(Info, sizeof(*Info));

    if (Frame == NULL || FrameLength < 14 + 20)
        return;

    EthType = ((ULONG)Frame[12] << 8) | Frame[13];
    if (EthType != ETH_TYPE_IPV4)
        return;

    Ip = Frame + 14;
    if ((Ip[0] >> 4) != 4)
        return;

    Info->Valid = TRUE;
    Info->Protocol = Ip[9];
    memcpy(Info->SrcIp, &Ip[12], sizeof(Info->SrcIp));
    memcpy(Info->DstIp, &Ip[16], sizeof(Info->DstIp));
}

static const char *
NetdProtoName(
    _In_ UCHAR Protocol)
{
    switch (Protocol)
    {
    case IP_PROTO_ICMP: return "icmp";
    case IP_PROTO_TCP:  return "tcp";
    case IP_PROTO_UDP:  return "udp";
    default:            return "other";
    }
}

/* ------------------------------------------------------------------ */
/* Byte-order helpers                                                   */
/* ------------------------------------------------------------------ */

static USHORT
ReadBe16(
    _In_reads_bytes_(2) const UCHAR *Ptr)
{
    return (USHORT)(((USHORT)Ptr[0] << 8) | Ptr[1]);
}

static void
WriteBe16(
    _Out_writes_bytes_(2) UCHAR *Ptr,
    _In_ USHORT Value)
{
    Ptr[0] = (UCHAR)((Value >> 8) & 0xFF);
    Ptr[1] = (UCHAR)(Value & 0xFF);
}

static ULONG
ReadBe32(
    _In_reads_bytes_(4) const UCHAR *Ptr)
{
    return ((ULONG)Ptr[0] << 24) |
           ((ULONG)Ptr[1] << 16) |
           ((ULONG)Ptr[2] << 8) |
           (ULONG)Ptr[3];
}

static void
WriteBe32(
    _Out_writes_bytes_(4) UCHAR *Ptr,
    _In_ ULONG Value)
{
    Ptr[0] = (UCHAR)((Value >> 24) & 0xFF);
    Ptr[1] = (UCHAR)((Value >> 16) & 0xFF);
    Ptr[2] = (UCHAR)((Value >> 8) & 0xFF);
    Ptr[3] = (UCHAR)(Value & 0xFF);
}

/* ------------------------------------------------------------------ */
/* Checksums                                                            */
/* ------------------------------------------------------------------ */

static USHORT
InternetChecksum(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Sum = 0;
    ULONG i;

    for (i = 0; i + 1 < Length; i += 2)
    {
        Sum += ReadBe16(&Data[i]);
        if (Sum > 0xFFFF)
            Sum = (Sum & 0xFFFF) + 1;
    }

    if (i < Length)
    {
        Sum += ((ULONG)Data[i] << 8);
        if (Sum > 0xFFFF)
            Sum = (Sum & 0xFFFF) + 1;
    }

    return (USHORT)(~(USHORT)Sum);
}

static USHORT
UdpChecksum(
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_reads_bytes_(UdpLength) const UCHAR *Udp,
    _In_ ULONG UdpLength)
{
    ULONG Sum = 0;
    ULONG i;

    for (i = 0; i < 4; i += 2)
    {
        Sum += ReadBe16(&SrcIp[i]);
        Sum += ReadBe16(&DstIp[i]);
    }
    Sum += IP_PROTO_UDP;
    Sum += UdpLength;

    for (i = 0; i + 1 < UdpLength; i += 2)
    {
        Sum += ReadBe16(&Udp[i]);
    }
    if (i < UdpLength)
        Sum += ((ULONG)Udp[i] << 8);

    while (Sum > 0xFFFF)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);

    Sum = (~Sum) & 0xFFFF;
    return (USHORT)(Sum == 0 ? 0xFFFF : Sum);
}

static USHORT
TcpChecksum(
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_reads_bytes_(TcpLength) const UCHAR *Tcp,
    _In_ ULONG TcpLength)
{
    ULONG Sum = 0;
    ULONG i;

    /* Pseudo-header */
    for (i = 0; i < 4; i += 2)
    {
        Sum += ReadBe16(&SrcIp[i]);
        Sum += ReadBe16(&DstIp[i]);
    }
    Sum += IP_PROTO_TCP;
    Sum += TcpLength;

    /* TCP header + payload */
    for (i = 0; i + 1 < TcpLength; i += 2)
    {
        Sum += ReadBe16(&Tcp[i]);
    }
    if (i < TcpLength)
        Sum += ((ULONG)Tcp[i] << 8);

    while (Sum > 0xFFFF)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);

    Sum = (~Sum) & 0xFFFF;
    return (USHORT)(Sum == 0 ? 0xFFFF : Sum);
}

/* ------------------------------------------------------------------ */
/* Send a raw Ethernet frame to the guest                               */
/* ------------------------------------------------------------------ */

static BOOL
SendPacketToGuest(
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length)
{
    ROSV_NET_PACKET Packet;
    DWORD Ret;
    HANDLE DevHandle;

    if (Frame == NULL || Length == 0 || Length > ROSV_NET_PACKET_MAX)
        return FALSE;

    ZeroMemory(&Packet, sizeof(Packet));
    Packet.Length = Length;
    memcpy(Packet.Data, Frame, Length);
    /* Keep host->guest traffic off the main NET_RX handle so synchronous
     * DeviceIoControl calls from different threads do not serialize. */
    DevHandle = (g_hDevTx != INVALID_HANDLE_VALUE) ? g_hDevTx : g_hDev;
    return IoctlOnHandle(DevHandle,
                         ROSV_IOCTL_NET_TX,
                         &Packet,
                         sizeof(ULONG) + Length,
                         NULL,
                         0,
                         &Ret);
}

/* ------------------------------------------------------------------ */
/* Packet construction helpers                                          */
/* ------------------------------------------------------------------ */

/*
 * Build an Ethernet + IPv4 header in Frame[0..33].
 * Returns pointer to start of transport header (Frame + 14 + 20 = Frame + 34).
 * Caller fills in transport layer and then must finalize IP checksum via
 * FinalizeIpHeader() after setting total length.
 */
static UCHAR *
BuildEthIpHeader(
    _Out_writes_bytes_(ROSV_NET_PACKET_MAX) UCHAR *Frame,
    _In_reads_bytes_(6) const UCHAR *DstMac,
    _In_reads_bytes_(6) const UCHAR *SrcMac,
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_ UCHAR Protocol,
    _In_ USHORT IpTotalLength)
{
    USHORT IpId;
    UCHAR *Ip;

    /* Ethernet header */
    memcpy(&Frame[0], DstMac, 6);
    memcpy(&Frame[6], SrcMac, 6);
    WriteBe16(&Frame[12], ETH_TYPE_IPV4);

    /* IPv4 header (20 bytes, no options) */
    Ip = &Frame[14];
    Ip[0] = 0x45;                          /* Version 4, IHL 5 */
    Ip[1] = 0x00;                          /* DSCP/ECN */
    WriteBe16(&Ip[2], IpTotalLength);      /* Total length */
    IpId = (USHORT)InterlockedIncrement(&g_IpIdCounter);
    WriteBe16(&Ip[4], IpId);               /* Identification */
    WriteBe16(&Ip[6], 0x4000);             /* Don't fragment */
    Ip[8] = 64;                            /* TTL */
    Ip[9] = Protocol;
    Ip[10] = 0;                            /* Checksum (zero before calc) */
    Ip[11] = 0;
    memcpy(&Ip[12], SrcIp, 4);
    memcpy(&Ip[16], DstIp, 4);

    /* Compute IP header checksum */
    WriteBe16(&Ip[10], InternetChecksum(Ip, 20));

    return &Frame[34]; /* Start of transport header */
}

/*
 * Build and send a TCP segment to the guest.
 * SeqNum and AckNum are in host byte order (will be converted).
 * Flags is a combination of TCP_FLAG_*.
 * Payload/PayloadLen may be NULL/0 for control segments (SYN-ACK, ACK, FIN, RST).
 * If IncludeMss is TRUE, appends MSS option (making TCP header 24 bytes).
 */
static BOOL
SendTcpToGuest(
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_ USHORT SrcPort,
    _In_ USHORT DstPort,
    _In_ ULONG SeqNum,
    _In_ ULONG AckNum,
    _In_ UCHAR Flags,
    _In_reads_bytes_opt_(PayloadLen) const UCHAR *Payload,
    _In_ ULONG PayloadLen,
    _In_ BOOL IncludeMss)
{
    UCHAR Frame[ROSV_NET_PACKET_MAX];
    UCHAR *Tcp;
    ULONG TcpHeaderLen;
    ULONG TcpTotalLen;
    ULONG IpTotalLen;
    ULONG FrameLen;

    TcpHeaderLen = IncludeMss ? 24 : 20;
    TcpTotalLen = TcpHeaderLen + PayloadLen;
    IpTotalLen = 20 + TcpTotalLen;
    FrameLen = 14 + IpTotalLen;

    if (FrameLen > sizeof(Frame))
        return FALSE;

    ZeroMemory(Frame, sizeof(Frame));
    Tcp = BuildEthIpHeader(Frame, g_GuestMac, g_HostMac, SrcIp, DstIp,
                           IP_PROTO_TCP, (USHORT)IpTotalLen);

    /* TCP header */
    WriteBe16(&Tcp[0], SrcPort);
    WriteBe16(&Tcp[2], DstPort);
    WriteBe32(&Tcp[4], SeqNum);
    WriteBe32(&Tcp[8], AckNum);

    /* Data offset (in 32-bit words) + flags */
    if (IncludeMss)
        Tcp[12] = (UCHAR)((TcpHeaderLen / 4) << 4); /* 6 << 4 = 0x60 */
    else
        Tcp[12] = (UCHAR)((TcpHeaderLen / 4) << 4); /* 5 << 4 = 0x50 */
    Tcp[13] = Flags;

    WriteBe16(&Tcp[14], TCP_WINDOW_SIZE);  /* Window size */
    /* Checksum at [16..17], urgent at [18..19] - zeroed */

    /* MSS option: Kind=2, Len=4, MSS=1460 */
    if (IncludeMss)
    {
        Tcp[20] = 0x02;
        Tcp[21] = 0x04;
        WriteBe16(&Tcp[22], TCP_MSS);
    }

    /* Copy payload after TCP header */
    if (Payload != NULL && PayloadLen > 0)
    {
        memcpy(&Tcp[TcpHeaderLen], Payload, PayloadLen);
    }

    /* TCP checksum using IP src/dst from the frame we just built */
    Tcp[16] = 0;
    Tcp[17] = 0;
    WriteBe16(&Tcp[16], TcpChecksum(&Frame[26], &Frame[30], Tcp, TcpTotalLen));

    /* Pad to minimum Ethernet frame */
    if (FrameLen < 60)
    {
        ZeroMemory(&Frame[FrameLen], 60 - FrameLen);
        FrameLen = 60;
    }

    return SendPacketToGuest(Frame, FrameLen);
}

/*
 * Build and send a UDP datagram to the guest.
 */
static BOOL
SendUdpToGuest(
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_ USHORT SrcPort,
    _In_ USHORT DstPort,
    _In_reads_bytes_(PayloadLen) const UCHAR *Payload,
    _In_ ULONG PayloadLen)
{
    UCHAR Frame[ROSV_NET_PACKET_MAX];
    UCHAR *Udp;
    ULONG UdpTotalLen;
    ULONG IpTotalLen;
    ULONG FrameLen;

    UdpTotalLen = 8 + PayloadLen;
    IpTotalLen = 20 + UdpTotalLen;
    FrameLen = 14 + IpTotalLen;

    if (FrameLen > sizeof(Frame))
        return FALSE;

    ZeroMemory(Frame, sizeof(Frame));
    Udp = BuildEthIpHeader(Frame, g_GuestMac, g_HostMac, SrcIp, DstIp,
                           IP_PROTO_UDP, (USHORT)IpTotalLen);

    /* UDP header */
    WriteBe16(&Udp[0], SrcPort);
    WriteBe16(&Udp[2], DstPort);
    WriteBe16(&Udp[4], (USHORT)UdpTotalLen);
    /* Checksum at [6..7] zeroed, will fill below */

    /* Copy payload */
    if (Payload != NULL && PayloadLen > 0)
    {
        memcpy(&Udp[8], Payload, PayloadLen);
    }

    /* UDP checksum */
    Udp[6] = 0;
    Udp[7] = 0;
    WriteBe16(&Udp[6], UdpChecksum(&Frame[26], &Frame[30], Udp, UdpTotalLen));

    /* Pad to minimum Ethernet frame */
    if (FrameLen < 60)
    {
        ZeroMemory(&Frame[FrameLen], 60 - FrameLen);
        FrameLen = 60;
    }

    return SendPacketToGuest(Frame, FrameLen);
}

/* ------------------------------------------------------------------ */
/* NAT table operations                                                 */
/* ------------------------------------------------------------------ */

static void
NatTableInit(void)
{
    ULONG i;
    InitializeCriticalSection(&g_NatLock);
    for (i = 0; i < NAT_TABLE_SIZE; i++)
    {
        g_NatTable[i].InUse = FALSE;
        g_NatTable[i].HostSocket = INVALID_SOCKET;
    }
}

static void
NatTableDestroy(void)
{
    ULONG i;
    EnterCriticalSection(&g_NatLock);
    for (i = 0; i < NAT_TABLE_SIZE; i++)
    {
        if (g_NatTable[i].InUse && g_NatTable[i].HostSocket != INVALID_SOCKET)
        {
            closesocket(g_NatTable[i].HostSocket);
            g_NatTable[i].HostSocket = INVALID_SOCKET;
        }
        g_NatTable[i].InUse = FALSE;
    }
    LeaveCriticalSection(&g_NatLock);
    DeleteCriticalSection(&g_NatLock);
}

/*
 * Find an existing NAT entry by guest IP:port + dest IP:port + protocol.
 * Caller must hold g_NatLock.
 */
static NAT_ENTRY *
NatLookup(
    _In_ NAT_PROTOCOL Proto,
    _In_reads_bytes_(4) const UCHAR *GuestIp,
    _In_ USHORT GuestPort,
    _In_reads_bytes_(4) const UCHAR *DestIp,
    _In_ USHORT DestPort)
{
    ULONG i;
    /* TODO: replace linear search with hashed 5-tuple buckets. */
    for (i = 0; i < NAT_TABLE_SIZE; i++)
    {
        NAT_ENTRY *E = &g_NatTable[i];
        if (E->InUse &&
            E->Protocol == Proto &&
            E->GuestPort == GuestPort &&
            E->DestPort == DestPort &&
            memcmp(E->GuestIp, GuestIp, 4) == 0 &&
            memcmp(E->DestIp, DestIp, 4) == 0)
        {
            return E;
        }
    }
    return NULL;
}

/*
 * Allocate a free NAT entry.
 * Caller must hold g_NatLock.
 */
static NAT_ENTRY *
NatAllocate(void)
{
    ULONG i;
    for (i = 0; i < NAT_TABLE_SIZE; i++)
    {
        if (!g_NatTable[i].InUse)
        {
            ZeroMemory(&g_NatTable[i], sizeof(NAT_ENTRY));
            g_NatTable[i].InUse = TRUE;
            g_NatTable[i].HostSocket = INVALID_SOCKET;
            g_NatTable[i].LastActivityTick = GetTickCount();
            return &g_NatTable[i];
        }
    }
    return NULL;
}

/*
 * Free a NAT entry and close its socket.
 * Caller must hold g_NatLock.
 */
static void
NatFree(
    _Inout_ NAT_ENTRY *Entry)
{
    if (Entry->HostSocket != INVALID_SOCKET)
    {
        closesocket(Entry->HostSocket);
        Entry->HostSocket = INVALID_SOCKET;
    }
    Entry->InUse = FALSE;
}

/*
 * Clean up timed-out entries.
 * Caller must hold g_NatLock.
 */
static void
NatCleanupExpired(void)
{
    ULONG i;
    DWORD Now = GetTickCount();

    for (i = 0; i < NAT_TABLE_SIZE; i++)
    {
        NAT_ENTRY *E = &g_NatTable[i];
        if (!E->InUse)
            continue;

        /* Handle tick count wraparound gracefully */
        if ((Now - E->LastActivityTick) > E->TimeoutMs)
        {
            if (E->Protocol == NAT_PROTO_TCP &&
                E->TcpState != TCP_STATE_CLOSED)
            {
                NetdLog("[NAT] TCP timeout %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
                        E->GuestIp[0], E->GuestIp[1], E->GuestIp[2], E->GuestIp[3],
                        E->GuestPort,
                        E->DestIp[0], E->DestIp[1], E->DestIp[2], E->DestIp[3],
                        E->DestPort);
            }
            NatFree(E);
        }
    }
}

/* ------------------------------------------------------------------ */
/* DNS server resolution                                                */
/* ------------------------------------------------------------------ */

static void
ResolveHostDns(void)
{
    FIXED_INFO *Info = NULL;
    ULONG InfoSize = 0;
    DWORD Err;
    unsigned int a, b, c, d;

    /* First call to get required buffer size */
    Err = GetNetworkParams(NULL, &InfoSize);
    if (Err != ERROR_BUFFER_OVERFLOW)
    {
        NetdLog("[DNS] GetNetworkParams size query failed (err=%lu), using 8.8.8.8\n", Err);
        return;
    }

    Info = (FIXED_INFO *)malloc(InfoSize);
    if (Info == NULL)
    {
        NetdLog("[DNS] malloc(%lu) failed for FIXED_INFO, using 8.8.8.8\n", InfoSize);
        return;
    }

    Err = GetNetworkParams(Info, &InfoSize);
    if (Err != ERROR_SUCCESS)
    {
        NetdLog("[DNS] GetNetworkParams failed (err=%lu), using 8.8.8.8\n", Err);
        free(Info);
        return;
    }

    if (Info->DnsServerList.IpAddress.String[0] != '\0')
    {
        if (sscanf(Info->DnsServerList.IpAddress.String, "%u.%u.%u.%u",
                   &a, &b, &c, &d) == 4)
        {
            g_HostDnsIp[0] = (UCHAR)a;
            g_HostDnsIp[1] = (UCHAR)b;
            g_HostDnsIp[2] = (UCHAR)c;
            g_HostDnsIp[3] = (UCHAR)d;
            NetdLog("[DNS] Host DNS server: %u.%u.%u.%u\n", a, b, c, d);
        }
        else
        {
            NetdLog("[DNS] Failed to parse DNS '%s', using 8.8.8.8\n",
                    Info->DnsServerList.IpAddress.String);
        }
    }
    else
    {
        NetdLog("[DNS] No DNS server configured, using 8.8.8.8\n");
    }

    free(Info);
}

/* ------------------------------------------------------------------ */
/* ARP handler (enhanced: responds for both gateway and DNS IPs)        */
/* ------------------------------------------------------------------ */

static BOOL
BuildAndSendArpReply(
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength)
{
    UCHAR Reply[60];
    const UCHAR *Arp;
    const UCHAR *SenderMac;
    const UCHAR *SenderIp;
    const UCHAR *TargetIp;

    if (FrameLength < 42)
        return FALSE;

    Arp = Frame + 14;
    if (g_Verbose)
    {
        NetdLog("[RX] ARP op=%u sip=%u.%u.%u.%u tip=%u.%u.%u.%u\n",
                ReadBe16(&Arp[6]),
                Arp[14], Arp[15], Arp[16], Arp[17],
                Arp[24], Arp[25], Arp[26], Arp[27]);
    }

    if (ReadBe16(&Arp[0]) != 1 || ReadBe16(&Arp[2]) != ETH_TYPE_IPV4 ||
        Arp[4] != 6 || Arp[5] != 4 || ReadBe16(&Arp[6]) != 1)
    {
        return FALSE;
    }

    SenderMac = &Arp[8];
    SenderIp = &Arp[14];
    TargetIp = &Arp[24];

    /* Respond for gateway IP (10.0.2.2) and DNS IP (10.0.2.3) */
    if (memcmp(TargetIp, g_HostIp, 4) != 0 &&
        memcmp(TargetIp, g_DnsIp, 4) != 0)
    {
        return FALSE;
    }

    ZeroMemory(Reply, sizeof(Reply));
    memcpy(&Reply[0], SenderMac, 6);
    memcpy(&Reply[6], g_HostMac, 6);
    WriteBe16(&Reply[12], ETH_TYPE_ARP);

    WriteBe16(&Reply[14], 1);
    WriteBe16(&Reply[16], ETH_TYPE_IPV4);
    Reply[18] = 6;
    Reply[19] = 4;
    WriteBe16(&Reply[20], 2);

    memcpy(&Reply[22], g_HostMac, 6);
    memcpy(&Reply[28], TargetIp, 4);       /* Reply with the requested IP */
    memcpy(&Reply[32], SenderMac, 6);
    memcpy(&Reply[38], SenderIp, 4);

    if (g_Verbose)
    {
        NetdLog("[TX] ARP reply sip=%u.%u.%u.%u tip=%u.%u.%u.%u\n",
                TargetIp[0], TargetIp[1], TargetIp[2], TargetIp[3],
                SenderIp[0], SenderIp[1], SenderIp[2], SenderIp[3]);
    }

    return SendPacketToGuest(Reply, sizeof(Reply));
}

/* ------------------------------------------------------------------ */
/* ICMP echo reply handler (unchanged)                                  */
/* ------------------------------------------------------------------ */

static BOOL
BuildAndSendIcmpEchoReply(
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength)
{
    UCHAR Reply[ROSV_NET_PACKET_MAX];
    ULONG EthPayloadLen;
    ULONG IpHeaderLen;
    ULONG IpTotalLen;
    ULONG ReplyLen;
    UCHAR *Ip;
    UCHAR *Icmp;
    ULONG IcmpLen;
    USHORT Frag;
    USHORT EchoId, EchoSeq;
    ULONGLONG StartQpc = NetdReadQpc();
    ULONGLONG EndQpc;
    ULONGLONG TotalUs;
    BOOL Sent;

    if (FrameLength < 14 + 20)
        return FALSE;

    if (ReadBe16(&Frame[12]) != ETH_TYPE_IPV4)
        return FALSE;

    EthPayloadLen = FrameLength - 14;
    IpHeaderLen = (ULONG)(Frame[14] & 0x0F) * 4;
    if ((Frame[14] >> 4) != 4 || IpHeaderLen < 20 || IpHeaderLen > EthPayloadLen)
        return FALSE;

    IpTotalLen = ReadBe16(&Frame[16]);
    if (IpTotalLen < IpHeaderLen || IpTotalLen > EthPayloadLen)
        return FALSE;

    if (Frame[23] != IP_PROTO_ICMP)
        return FALSE;

    Frag = ReadBe16(&Frame[20]);
    /* TODO: support reassembly for fragmented ICMP echo requests. */
    if ((Frag & 0x3FFF) != 0)
        return FALSE;

    if (memcmp(&Frame[30], g_HostIp, 4) != 0 &&
        memcmp(&Frame[30], g_DnsIp, 4) != 0)
        return FALSE;

    if (IpTotalLen < IpHeaderLen + 8)
        return FALSE;

    if (Frame[14 + IpHeaderLen] != 8 || Frame[14 + IpHeaderLen + 1] != 0)
        return FALSE;

    EchoId = (USHORT)(((USHORT)Frame[14 + IpHeaderLen + 4] << 8) |
                      Frame[14 + IpHeaderLen + 5]);
    EchoSeq = (USHORT)(((USHORT)Frame[14 + IpHeaderLen + 6] << 8) |
                       Frame[14 + IpHeaderLen + 7]);

    ReplyLen = 14 + IpTotalLen;
    if (ReplyLen > sizeof(Reply))
        return FALSE;

    memcpy(Reply, Frame, ReplyLen);
    if (ReplyLen < 60)
    {
        ZeroMemory(Reply + ReplyLen, 60 - ReplyLen);
        ReplyLen = 60;
    }

    memcpy(&Reply[0], &Frame[6], 6);
    memcpy(&Reply[6], g_HostMac, 6);

    Ip = &Reply[14];
    /* Reply from the exact virtual IP that was pinged (gateway or DNS). */
    memcpy(&Ip[12], &Frame[30], 4);
    memcpy(&Ip[16], &Frame[26], 4);
    Ip[8] = 64;
    Ip[10] = 0;
    Ip[11] = 0;
    WriteBe16(&Ip[10], InternetChecksum(Ip, IpHeaderLen));

    Icmp = Ip + IpHeaderLen;
    IcmpLen = IpTotalLen - IpHeaderLen;
    Icmp[0] = 0;
    Icmp[1] = 0;
    Icmp[2] = 0;
    Icmp[3] = 0;
    WriteBe16(&Icmp[2], InternetChecksum(Icmp, IcmpLen));

    Sent = SendPacketToGuest(Reply, ReplyLen);
    EndQpc = NetdReadQpc();
    TotalUs = NetdQpcDeltaUs(StartQpc, EndQpc);

    NetdLog("[PERF][ICMP-LOCAL] id=0x%04X seq=%u total_us=%llu sent=%u dst=%u.%u.%u.%u src=%u.%u.%u.%u\n",
            EchoId,
            EchoSeq,
            TotalUs,
            Sent ? 1 : 0,
            Frame[30], Frame[31], Frame[32], Frame[33],
            Frame[26], Frame[27], Frame[28], Frame[29]);

    return Sent;
}

/* ------------------------------------------------------------------ */
/* ICMP echo forwarding via persistent worker thread                    */
/* ------------------------------------------------------------------ */

/*
 * Send an ICMP echo reply packet back to the guest.
 * Called by the persistent ICMP worker when a matching reply arrives.
 */
static BOOL
IcmpInjectReplyToGuest(
    _In_ const ICMP_INFLIGHT_ENTRY *Entry,
    _In_reads_bytes_(RxIcmpLen) const UCHAR *RxIcmp,
    _In_ int RxIcmpLen,
    _In_ UCHAR HostTtl)
{
    UCHAR Reply[ROSV_NET_PACKET_MAX];
    UCHAR *OutIp;
    UCHAR *OutIcmp;
    ULONG OutPayloadLen;
    ULONG OutIcmpLen;
    ULONG OutIpLen;
    ULONG OutFrameLen;
    USHORT IpId;

    OutPayloadLen = (ULONG)(RxIcmpLen - 8);
    if (OutPayloadLen > (ROSV_NET_PACKET_MAX - (14 + 20 + 8)))
        OutPayloadLen = ROSV_NET_PACKET_MAX - (14 + 20 + 8);

    if (!g_Running || g_hDev == INVALID_HANDLE_VALUE)
        return FALSE;

    ZeroMemory(Reply, sizeof(Reply));
    memcpy(&Reply[0], g_GuestMac, 6);
    memcpy(&Reply[6], g_HostMac, 6);
    WriteBe16(&Reply[12], ETH_TYPE_IPV4);

    OutIcmp = Reply + 14 + 20;
    OutIcmpLen = 8 + OutPayloadLen;
    OutIpLen = 20 + OutIcmpLen;
    OutFrameLen = 14 + OutIpLen;

    OutIp = &Reply[14];
    OutIp[0] = 0x45;
    OutIp[1] = 0x00;
    WriteBe16(&OutIp[2], (USHORT)OutIpLen);
    IpId = (USHORT)InterlockedIncrement(&g_IpIdCounter);
    WriteBe16(&OutIp[4], IpId);
    OutIp[6] = 0x00;
    OutIp[7] = 0x00;
    OutIp[8] = HostTtl;
    OutIp[9] = IP_PROTO_ICMP;
    memcpy(&OutIp[12], Entry->DstIp, 4);
    memcpy(&OutIp[16], Entry->SrcIp, 4);
    OutIp[10] = 0;
    OutIp[11] = 0;
    WriteBe16(&OutIp[10], InternetChecksum(OutIp, 20));

    OutIcmp[0] = 0;  /* Type: Echo Reply */
    OutIcmp[1] = 0;  /* Code: 0 */
    OutIcmp[2] = 0;
    OutIcmp[3] = 0;
    WriteBe16(&OutIcmp[4], Entry->IcmpId);
    WriteBe16(&OutIcmp[6], Entry->IcmpSeq);

    if (OutPayloadLen > 0)
        memcpy(&OutIcmp[8], &RxIcmp[8], OutPayloadLen);

    WriteBe16(&OutIcmp[2], InternetChecksum(OutIcmp, OutIcmpLen));

    if (OutFrameLen < 60)
    {
        ZeroMemory(Reply + OutFrameLen, 60 - OutFrameLen);
        OutFrameLen = 60;
    }

    return SendPacketToGuest(Reply, OutFrameLen);
}

/*
 * Persistent ICMP worker thread.
 *
 * Owns a single raw ICMP socket. Drains queued echo requests from
 * g_IcmpQueue, sends them out, and uses select() to receive replies.
 * Matches replies to in-flight entries by (Id, Seq) and injects the
 * response back to the guest. Entries time out after ICMP_ECHO_TIMEOUT_MS.
 */
static DWORD WINAPI
IcmpWorkerThread(
    _In_ LPVOID Param)
{
    fd_set ReadFds;
    struct timeval Tv;
    UCHAR RecvBuf[1500];
    UCHAR SendBuf[8 + 1500];
    struct sockaddr_in DestSa, FromSa;
    int FromLen;

    (void)Param;

    NetdLog("[ICMP] Persistent worker thread started\n");

    while (g_Running)
    {
        ULONG i;
        DWORD Now;
        BOOL HaveInflight = FALSE;
        int SelectResult;

        /* --- Phase 1: Drain queued tasks, send them out, register in-flight --- */
        EnterCriticalSection(&g_IcmpLock);
        while (g_IcmpQueueHead != g_IcmpQueueTail)
        {
            ICMP_ECHO_TASK *Task = g_IcmpQueue[g_IcmpQueueHead];
            g_IcmpQueue[g_IcmpQueueHead] = NULL;
            g_IcmpQueueHead = (g_IcmpQueueHead + 1) % ICMP_QUEUE_SIZE;
            LeaveCriticalSection(&g_IcmpLock);

            if (Task != NULL && g_IcmpSocket != INVALID_SOCKET)
            {
                int SendLen;
                USHORT SendChecksum;
                ULONGLONG QueueWaitUs;
                BOOL Sent = FALSE;
                ULONG SlotIdx;

                QueueWaitUs = NetdQpcDeltaUs(Task->TraceQueuedQpc, NetdReadQpc());

                /* Build ICMP echo request */
                SendLen = 8 + (int)Task->PayloadLen;
                if (SendLen > (int)sizeof(SendBuf))
                    SendLen = (int)sizeof(SendBuf);

                ZeroMemory(SendBuf, (size_t)SendLen);
                SendBuf[0] = 8;   /* Type: Echo Request */
                SendBuf[1] = 0;   /* Code: 0 */
                WriteBe16(&SendBuf[4], Task->IcmpId);
                WriteBe16(&SendBuf[6], Task->IcmpSeq);
                if (Task->PayloadLen > 0 && Task->PayloadLen <= sizeof(SendBuf) - 8)
                    memcpy(&SendBuf[8], Task->Payload, Task->PayloadLen);

                SendChecksum = InternetChecksum(SendBuf, (ULONG)SendLen);
                WriteBe16(&SendBuf[2], SendChecksum);

                ZeroMemory(&DestSa, sizeof(DestSa));
                DestSa.sin_family = AF_INET;
                memcpy(&DestSa.sin_addr, Task->DstIp, 4);

                if (sendto(g_IcmpSocket, (const char *)SendBuf, SendLen, 0,
                           (struct sockaddr *)&DestSa, sizeof(DestSa)) != SOCKET_ERROR)
                {
                    Sent = TRUE;
                }
                else
                {
                    int Err = WSAGetLastError();
                    NetdLog("[PERF][ICMP-FWD] id=0x%04X seq=%u queue_us=%llu ok=0 err=sendto_%d %u.%u.%u.%u->%u.%u.%u.%u\n",
                            Task->IcmpId, Task->IcmpSeq, QueueWaitUs, Err,
                            Task->SrcIp[0], Task->SrcIp[1], Task->SrcIp[2], Task->SrcIp[3],
                            Task->DstIp[0], Task->DstIp[1], Task->DstIp[2], Task->DstIp[3]);
                }

                if (Sent)
                {
                    /* Find a free in-flight slot */
                    SlotIdx = ICMP_MAX_INFLIGHT; /* sentinel: no slot */
                    for (i = 0; i < ICMP_MAX_INFLIGHT; i++)
                    {
                        if (!g_IcmpInflight[i].Active)
                        {
                            SlotIdx = i;
                            break;
                        }
                    }

                    if (SlotIdx < ICMP_MAX_INFLIGHT)
                    {
                        ICMP_INFLIGHT_ENTRY *E = &g_IcmpInflight[SlotIdx];
                        E->Active = TRUE;
                        E->IcmpId = Task->IcmpId;
                        E->IcmpSeq = Task->IcmpSeq;
                        memcpy(E->SrcIp, Task->SrcIp, 4);
                        memcpy(E->DstIp, Task->DstIp, 4);
                        E->QueuedQpc = Task->TraceQueuedQpc;
                        E->SentQpc = NetdReadQpc();
                        E->DeadlineTick = GetTickCount() + ICMP_ECHO_TIMEOUT_MS;

                        NetdLog("[PERF][ICMP-FWD] id=0x%04X seq=%u queue_us=%llu sent=1 %u.%u.%u.%u->%u.%u.%u.%u\n",
                                Task->IcmpId, Task->IcmpSeq, QueueWaitUs,
                                Task->SrcIp[0], Task->SrcIp[1], Task->SrcIp[2], Task->SrcIp[3],
                                Task->DstIp[0], Task->DstIp[1], Task->DstIp[2], Task->DstIp[3]);
                    }
                    else
                    {
                        NetdLog("[ICMP] No free in-flight slot for id=0x%04X seq=%u\n",
                                Task->IcmpId, Task->IcmpSeq);
                    }
                }

                InterlockedDecrement(&g_IcmpInFlight);
            }
            else if (Task != NULL)
            {
                InterlockedDecrement(&g_IcmpInFlight);
            }

            free(Task);
            EnterCriticalSection(&g_IcmpLock);
        }
        LeaveCriticalSection(&g_IcmpLock);

        /* --- Phase 2: Check if any in-flight entries exist --- */
        for (i = 0; i < ICMP_MAX_INFLIGHT; i++)
        {
            if (g_IcmpInflight[i].Active)
            {
                HaveInflight = TRUE;
                break;
            }
        }

        if (!HaveInflight)
        {
            /* Nothing pending -- sleep briefly, then loop back to drain queue */
            Sleep(1);
            continue;
        }

        /* --- Phase 3: select() on the persistent socket for replies --- */
        FD_ZERO(&ReadFds);
        FD_SET(g_IcmpSocket, &ReadFds);

        Tv.tv_sec = 0;
        Tv.tv_usec = 1000; /* 1ms poll interval */

        SelectResult = select(0, &ReadFds, NULL, NULL, &Tv);

        if (SelectResult > 0 && FD_ISSET(g_IcmpSocket, &ReadFds))
        {
            /* Read all available replies in a burst */
            for (;;)
            {
                int RecvLen;
                int IpHdrLen;
                UCHAR *RxIcmp;
                int RxIcmpLen;
                USHORT RxId, RxSeq;

                FromLen = sizeof(FromSa);
                RecvLen = recvfrom(g_IcmpSocket, (char *)RecvBuf, sizeof(RecvBuf), 0,
                                   (struct sockaddr *)&FromSa, &FromLen);
                if (RecvLen == SOCKET_ERROR)
                    break;

                if (RecvLen < 20)
                    continue;

                IpHdrLen = (RecvBuf[0] & 0x0F) * 4;
                if (RecvLen < IpHdrLen + 8)
                    continue;

                RxIcmp = RecvBuf + IpHdrLen;
                RxIcmpLen = RecvLen - IpHdrLen;

                /* Must be echo reply (type 0, code 0) */
                if (RxIcmp[0] != 0)
                    continue;

                RxId = ReadBe16(&RxIcmp[4]);
                RxSeq = ReadBe16(&RxIcmp[6]);

                /* Match against in-flight entries */
                for (i = 0; i < ICMP_MAX_INFLIGHT; i++)
                {
                    ICMP_INFLIGHT_ENTRY *E = &g_IcmpInflight[i];
                    if (E->Active && E->IcmpId == RxId && E->IcmpSeq == RxSeq)
                    {
                        ULONGLONG RttUs = NetdQpcDeltaUs(E->SentQpc, NetdReadQpc());
                        ULONGLONG TotalUs = NetdQpcDeltaUs(E->QueuedQpc, NetdReadQpc());
                        ULONGLONG InjectStartQpc;
                        ULONGLONG InjectUs;
                        BOOL InjectOk;

                        InjectStartQpc = NetdReadQpc();
                        InjectOk = IcmpInjectReplyToGuest(E, RxIcmp, RxIcmpLen, RecvBuf[8]);
                        InjectUs = NetdQpcDeltaUs(InjectStartQpc, NetdReadQpc());

                        TotalUs = NetdQpcDeltaUs(E->QueuedQpc, NetdReadQpc());
                        NetdLog("[PERF][ICMP-FWD] id=0x%04X seq=%u host_rtt_us=%llu host_rtt_ms=%lu inject_us=%llu total_us=%llu ok=%u status=0 %u.%u.%u.%u->%u.%u.%u.%u\n",
                                E->IcmpId, E->IcmpSeq, RttUs, (ULONG)(RttUs / 1000),
                                InjectUs, TotalUs, InjectOk ? 1 : 0,
                                E->SrcIp[0], E->SrcIp[1], E->SrcIp[2], E->SrcIp[3],
                                E->DstIp[0], E->DstIp[1], E->DstIp[2], E->DstIp[3]);

                        E->Active = FALSE;
                        break;
                    }
                }
            }
        }

        /* --- Phase 4: Expire timed-out in-flight entries --- */
        Now = GetTickCount();
        for (i = 0; i < ICMP_MAX_INFLIGHT; i++)
        {
            ICMP_INFLIGHT_ENTRY *E = &g_IcmpInflight[i];
            if (E->Active && (LONG)(Now - E->DeadlineTick) >= 0)
            {
                ULONGLONG TotalUs = NetdQpcDeltaUs(E->QueuedQpc, NetdReadQpc());
                NetdLog("[PERF][ICMP-FWD] id=0x%04X seq=%u total_us=%llu ok=0 status=11010 (timeout) %u.%u.%u.%u->%u.%u.%u.%u\n",
                        E->IcmpId, E->IcmpSeq, TotalUs,
                        E->SrcIp[0], E->SrcIp[1], E->SrcIp[2], E->SrcIp[3],
                        E->DstIp[0], E->DstIp[1], E->DstIp[2], E->DstIp[3]);
                E->Active = FALSE;
            }
        }
    }

    NetdLog("[ICMP] Persistent worker thread stopped\n");
    return 0;
}

/*
 * Initialize persistent ICMP worker: create socket, start thread.
 * Called once at startup after Winsock init.
 */
static BOOL
IcmpWorkerInit(void)
{
    u_long NonBlock = 1;

    InitializeCriticalSection(&g_IcmpLock);
    ZeroMemory(g_IcmpQueue, sizeof(g_IcmpQueue));
    ZeroMemory(g_IcmpInflight, sizeof(g_IcmpInflight));
    g_IcmpQueueHead = 0;
    g_IcmpQueueTail = 0;

    g_IcmpSocket = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (g_IcmpSocket == INVALID_SOCKET)
    {
        NetdLog("[ICMP] socket(SOCK_RAW, IPPROTO_ICMP) failed (err=%d)\n", WSAGetLastError());
        return FALSE;
    }

    /* Non-blocking so the worker thread can poll with select() */
    ioctlsocket(g_IcmpSocket, FIONBIO, &NonBlock);

    g_IcmpThread = CreateThread(NULL, 0, IcmpWorkerThread, NULL, 0, NULL);
    if (g_IcmpThread == NULL)
    {
        NetdLog("[ICMP] CreateThread for ICMP worker failed (err=%lu)\n", GetLastError());
        closesocket(g_IcmpSocket);
        g_IcmpSocket = INVALID_SOCKET;
        return FALSE;
    }

    NetdLog("[OK]   ICMP persistent worker initialized\n");
    return TRUE;
}

/*
 * Shut down the persistent ICMP worker.
 */
static void
IcmpWorkerDestroy(void)
{
    ULONG i;

    if (g_IcmpThread != NULL)
    {
        WaitForSingleObject(g_IcmpThread, 5000);
        CloseHandle(g_IcmpThread);
        g_IcmpThread = NULL;
    }

    if (g_IcmpSocket != INVALID_SOCKET)
    {
        closesocket(g_IcmpSocket);
        g_IcmpSocket = INVALID_SOCKET;
    }

    /* Free any remaining queued tasks */
    EnterCriticalSection(&g_IcmpLock);
    for (i = 0; i < ICMP_QUEUE_SIZE; i++)
    {
        if (g_IcmpQueue[i] != NULL)
        {
            free(g_IcmpQueue[i]);
            g_IcmpQueue[i] = NULL;
        }
    }
    LeaveCriticalSection(&g_IcmpLock);
    DeleteCriticalSection(&g_IcmpLock);
}

/*
 * Queue an ICMP echo request for the persistent worker thread.
 * Replaces the old thread-per-ping ForwardIcmpEcho.
 */
static BOOL
ForwardIcmpEcho(
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength)
{
    ULONG EthPayloadLen;
    ULONG IpHeaderLen;
    ULONG IpTotalLen;
    const UCHAR *Icmp;
    ULONG IcmpLen;
    ULONG IcmpPayloadLen;
    SIZE_T TaskSize;
    ICMP_ECHO_TASK *Task;
    ULONG NextTail;

    if (FrameLength < 14 + 20 + 8)
        return FALSE;

    if (ReadBe16(&Frame[12]) != ETH_TYPE_IPV4)
        return FALSE;

    EthPayloadLen = FrameLength - 14;
    IpHeaderLen = (ULONG)(Frame[14] & 0x0F) * 4;
    if ((Frame[14] >> 4) != 4 || IpHeaderLen < 20 || IpHeaderLen > EthPayloadLen)
        return FALSE;

    IpTotalLen = ReadBe16(&Frame[16]);
    if (IpTotalLen < IpHeaderLen + 8 || IpTotalLen > EthPayloadLen)
        return FALSE;

    if (Frame[23] != IP_PROTO_ICMP)
        return FALSE;

    Icmp = &Frame[14 + IpHeaderLen];
    IcmpLen = IpTotalLen - IpHeaderLen;
    if (IcmpLen < 8)
        return FALSE;

    /* Only forward echo requests (type 8, code 0) */
    if (Icmp[0] != 8 || Icmp[1] != 0)
        return FALSE;

    IcmpPayloadLen = IcmpLen - 8;
    if (IcmpPayloadLen > 0xFFFF)
        return FALSE;

    if (g_IcmpSocket == INVALID_SOCKET)
    {
        NetdLog("[ICMP] No persistent socket, dropping echo to %u.%u.%u.%u\n",
                Frame[30], Frame[31], Frame[32], Frame[33]);
        return FALSE;
    }

    if (InterlockedIncrement(&g_IcmpInFlight) > ICMP_MAX_INFLIGHT)
    {
        InterlockedDecrement(&g_IcmpInFlight);
        NetdLog("[ICMP] Too many ICMP in-flight, dropping echo to %u.%u.%u.%u\n",
                Frame[30], Frame[31], Frame[32], Frame[33]);
        return FALSE;
    }

    TaskSize = sizeof(ICMP_ECHO_TASK);
    if (IcmpPayloadLen > 0)
    {
        TaskSize += (SIZE_T)(IcmpPayloadLen - 1);
    }

    Task = (ICMP_ECHO_TASK *)malloc(TaskSize);
    if (Task == NULL)
    {
        InterlockedDecrement(&g_IcmpInFlight);
        NetdLog("[ICMP] malloc(%llu) failed\n", (unsigned long long)TaskSize);
        return FALSE;
    }

    memcpy(Task->SrcIp, &Frame[26], 4);
    memcpy(Task->DstIp, &Frame[30], 4);
    Task->IcmpId = ReadBe16(&Icmp[4]);
    Task->IcmpSeq = ReadBe16(&Icmp[6]);
    Task->TraceQueuedQpc = NetdReadQpc();
    Task->PayloadLen = IcmpPayloadLen;
    if (IcmpPayloadLen > 0)
    {
        memcpy(Task->Payload, &Icmp[8], IcmpPayloadLen);
    }

    /* Enqueue into the ring buffer */
    EnterCriticalSection(&g_IcmpLock);
    NextTail = (g_IcmpQueueTail + 1) % ICMP_QUEUE_SIZE;
    if (NextTail == g_IcmpQueueHead)
    {
        /* Queue full */
        LeaveCriticalSection(&g_IcmpLock);
        free(Task);
        InterlockedDecrement(&g_IcmpInFlight);
        NetdLog("[ICMP] Queue full, dropping echo to %u.%u.%u.%u\n",
                Frame[30], Frame[31], Frame[32], Frame[33]);
        return FALSE;
    }
    g_IcmpQueue[g_IcmpQueueTail] = Task;
    g_IcmpQueueTail = NextTail;
    LeaveCriticalSection(&g_IcmpLock);

    return TRUE;
}

/* ------------------------------------------------------------------ */
/* DHCP handler (unchanged)                                             */
/* ------------------------------------------------------------------ */

static ULONG
DhcpAppendOption(
    _Out_writes_bytes_(Capacity) UCHAR *Options,
    _In_ ULONG Capacity,
    _In_ ULONG Offset,
    _In_ UCHAR Code,
    _In_reads_bytes_(DataLength) const UCHAR *Data,
    _In_ ULONG DataLength)
{
    if (Offset + 2 + DataLength > Capacity)
        return Offset;

    Options[Offset++] = Code;
    Options[Offset++] = (UCHAR)DataLength;
    if (DataLength != 0)
        memcpy(&Options[Offset], Data, DataLength);
    return Offset + DataLength;
}

static UCHAR
DhcpParseMessageType(
    _In_reads_bytes_(OptionsLength) const UCHAR *Options,
    _In_ ULONG OptionsLength,
    _Out_opt_ BOOL *HasRequestedIp,
    _Out_writes_bytes_opt_(4) UCHAR *RequestedIp,
    _Out_opt_ BOOL *HasServerId,
    _Out_writes_bytes_opt_(4) UCHAR *ServerId)
{
    ULONG Offset = 0;
    UCHAR MsgType = 0;

    if (HasRequestedIp) *HasRequestedIp = FALSE;
    if (HasServerId) *HasServerId = FALSE;

    while (Offset < OptionsLength)
    {
        UCHAR Code = Options[Offset++];
        UCHAR Length;

        if (Code == 0xFF)
            break;
        if (Code == 0x00)
            continue;
        if (Offset >= OptionsLength)
            break;

        Length = Options[Offset++];
        if (Offset + Length > OptionsLength)
            break;

        if (Code == 53 && Length >= 1)
        {
            MsgType = Options[Offset];
        }
        else if (Code == 50 && Length >= 4 && HasRequestedIp && RequestedIp)
        {
            memcpy(RequestedIp, &Options[Offset], 4);
            *HasRequestedIp = TRUE;
        }
        else if (Code == 54 && Length >= 4 && HasServerId && ServerId)
        {
            memcpy(ServerId, &Options[Offset], 4);
            *HasServerId = TRUE;
        }

        Offset += Length;
    }

    return MsgType;
}

static BOOL
BuildAndSendDhcpReply(
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength)
{
    UCHAR Reply[ROSV_NET_PACKET_MAX];
    const UCHAR *Ip;
    const UCHAR *Udp;
    const UCHAR *Bootp;
    const UCHAR *DhcpOptions;
    UCHAR *OutIp;
    UCHAR *OutUdp;
    UCHAR *OutBootp;
    UCHAR *OutOptions;
    ULONG EthPayloadLength;
    ULONG IpHeaderLength;
    ULONG IpTotalLength;
    ULONG UdpLength;
    ULONG UdpPayloadLength;
    ULONG OutOptionsLength;
    ULONG OutBootpLength;
    ULONG OutIpLength;
    ULONG OutFrameLength;
    UCHAR MessageType;
    UCHAR ReplyMessageType;
    BOOL HasRequestedIp;
    BOOL HasServerId;
    UCHAR RequestedIp[4];
    UCHAR ServerId[4];
    UCHAR LeaseSeconds[4];
    UCHAR RenewalSeconds[4];
    UCHAR RebindSeconds[4];
    UCHAR BroadcastIp[4];
    UCHAR MtuOption[2];
    BOOL Sent;
    ULONG OptionsCapacity;

    if (FrameLength < 14 + 20 + 8 + 240)
        return FALSE;
    if (ReadBe16(&Frame[12]) != ETH_TYPE_IPV4)
        return FALSE;

    Ip = Frame + 14;
    EthPayloadLength = FrameLength - 14;
    IpHeaderLength = (ULONG)(Ip[0] & 0x0F) * 4;
    if ((Ip[0] >> 4) != 4 || IpHeaderLength < 20 || IpHeaderLength > EthPayloadLength)
        return FALSE;
    if (Ip[9] != IP_PROTO_UDP)
        return FALSE;
    if ((ReadBe16(&Ip[6]) & 0x3FFF) != 0)
        return FALSE;

    IpTotalLength = ReadBe16(&Ip[2]);
    if (IpTotalLength < IpHeaderLength + 8 || IpTotalLength > EthPayloadLength)
        return FALSE;

    Udp = Ip + IpHeaderLength;
    UdpLength = ReadBe16(&Udp[4]);
    if (UdpLength < 8 || UdpLength > (IpTotalLength - IpHeaderLength))
        return FALSE;
    if (ReadBe16(&Udp[0]) != DHCP_CLIENT_PORT || ReadBe16(&Udp[2]) != DHCP_SERVER_PORT)
        return FALSE;

    UdpPayloadLength = UdpLength - 8;
    if (UdpPayloadLength < 240)
        return FALSE;

    Bootp = Udp + 8;
    if (Bootp[0] != 1 || Bootp[1] != 1 || Bootp[2] != 6)
        return FALSE;
    if (ReadBe32(&Bootp[236]) != DHCP_MAGIC_COOKIE)
        return FALSE;

    DhcpOptions = &Bootp[240];
    MessageType = DhcpParseMessageType(DhcpOptions,
                                       UdpPayloadLength - 240,
                                       &HasRequestedIp,
                                       RequestedIp,
                                       &HasServerId,
                                       ServerId);

    if (MessageType == 1) /* DHCPDISCOVER */
    {
        ReplyMessageType = 2; /* DHCPOFFER */
    }
    else if (MessageType == 3) /* DHCPREQUEST */
    {
        ReplyMessageType = 5; /* DHCPACK */
        if (HasServerId && memcmp(ServerId, g_HostIp, sizeof(g_HostIp)) != 0)
            return FALSE;
        if (HasRequestedIp && memcmp(RequestedIp, g_GuestIp, sizeof(g_GuestIp)) != 0)
            return FALSE;
    }
    else
    {
        return FALSE;
    }

    g_DhcpRxCount++;

    ZeroMemory(Reply, sizeof(Reply));

    memset(&Reply[0], 0xFF, 6);            /* Broadcast */
    memcpy(&Reply[6], g_HostMac, 6);
    WriteBe16(&Reply[12], ETH_TYPE_IPV4);

    OutIp = &Reply[14];
    OutIp[0] = 0x45;
    OutIp[1] = 0x00;
    OutIp[4] = Ip[4];
    OutIp[5] = Ip[5];
    OutIp[6] = 0x00;
    OutIp[7] = 0x00;
    OutIp[8] = 64;
    OutIp[9] = IP_PROTO_UDP;
    memcpy(&OutIp[12], g_HostIp, 4);
    memset(&OutIp[16], 0xFF, 4);           /* 255.255.255.255 */

    OutUdp = OutIp + 20;
    WriteBe16(&OutUdp[0], DHCP_SERVER_PORT);
    WriteBe16(&OutUdp[2], DHCP_CLIENT_PORT);

    OutBootp = OutUdp + 8;
    OutBootp[0] = 2;                        /* BOOTREPLY */
    OutBootp[1] = 1;                        /* Ethernet */
    OutBootp[2] = 6;                        /* MAC length */
    OutBootp[3] = 0;
    memcpy(&OutBootp[4], &Bootp[4], 4);     /* XID */
    memcpy(&OutBootp[8], &Bootp[8], 2);     /* secs */
    memcpy(&OutBootp[10], &Bootp[10], 2);   /* flags */
    memcpy(&OutBootp[12], &Bootp[12], 4);   /* ciaddr */
    memcpy(&OutBootp[16], g_GuestIp, 4);    /* yiaddr */
    memcpy(&OutBootp[20], g_HostIp, 4);     /* siaddr */
    memcpy(&OutBootp[28], &Bootp[28], 16);  /* chaddr */
    WriteBe32(&OutBootp[236], DHCP_MAGIC_COOKIE);

    /* Maximum bytes available for DHCP options in the reply frame. */
    OptionsCapacity = sizeof(Reply) - (14 + 20 + 8 + 240);

    OutOptions = &OutBootp[240];
    OutOptionsLength = 0;
    OutOptionsLength = DhcpAppendOption(OutOptions, OptionsCapacity,
                                        OutOptionsLength, 53, &ReplyMessageType, 1);
    OutOptionsLength = DhcpAppendOption(OutOptions, OptionsCapacity,
                                        OutOptionsLength, 54, g_HostIp, 4);
    OutOptionsLength = DhcpAppendOption(OutOptions, OptionsCapacity,
                                        OutOptionsLength, 1, g_GuestMask, 4);
    OutOptionsLength = DhcpAppendOption(OutOptions, OptionsCapacity,
                                        OutOptionsLength, 3, g_HostIp, 4);
    OutOptionsLength = DhcpAppendOption(OutOptions, OptionsCapacity,
                                        OutOptionsLength, 6, g_DnsIp, 4);

    WriteBe32(LeaseSeconds, 3600);
    WriteBe32(RenewalSeconds, 1800);
    WriteBe32(RebindSeconds, 3150);
    OutOptionsLength = DhcpAppendOption(OutOptions, OptionsCapacity,
                                        OutOptionsLength, 51, LeaseSeconds, sizeof(LeaseSeconds));
    OutOptionsLength = DhcpAppendOption(OutOptions, OptionsCapacity,
                                        OutOptionsLength, 58, RenewalSeconds, sizeof(RenewalSeconds));
    OutOptionsLength = DhcpAppendOption(OutOptions, OptionsCapacity,
                                        OutOptionsLength, 59, RebindSeconds, sizeof(RebindSeconds));

    BroadcastIp[0] = g_GuestIp[0] | (UCHAR)(~g_GuestMask[0]);
    BroadcastIp[1] = g_GuestIp[1] | (UCHAR)(~g_GuestMask[1]);
    BroadcastIp[2] = g_GuestIp[2] | (UCHAR)(~g_GuestMask[2]);
    BroadcastIp[3] = g_GuestIp[3] | (UCHAR)(~g_GuestMask[3]);
    OutOptionsLength = DhcpAppendOption(OutOptions, OptionsCapacity,
                                        OutOptionsLength, 28, BroadcastIp, sizeof(BroadcastIp));

    MtuOption[0] = 0x05;
    MtuOption[1] = 0xDC; /* 1500 */
    OutOptionsLength = DhcpAppendOption(OutOptions, OptionsCapacity,
                                        OutOptionsLength, 26, MtuOption, sizeof(MtuOption));

    if (OutOptionsLength < OptionsCapacity)
        OutOptions[OutOptionsLength++] = 0xFF; /* end */

    /* Keep DHCP payload at least 300 bytes for conservative client compatibility. */
    if (OutOptionsLength < 60)
        OutOptionsLength = 60;

    OutBootpLength = 240 + OutOptionsLength;
    OutIpLength = 20 + 8 + OutBootpLength;
    OutFrameLength = 14 + OutIpLength;

    WriteBe16(&OutIp[2], (USHORT)OutIpLength);
    OutIp[10] = 0;
    OutIp[11] = 0;
    WriteBe16(&OutIp[10], InternetChecksum(OutIp, 20));

    WriteBe16(&OutUdp[4], (USHORT)(8 + OutBootpLength));
    OutUdp[6] = 0;
    OutUdp[7] = 0;
    WriteBe16(&OutUdp[6], UdpChecksum(&OutIp[12], &OutIp[16], OutUdp, 8 + OutBootpLength));

    if (OutFrameLength < 60)
    {
        ZeroMemory(&Reply[OutFrameLength], 60 - OutFrameLength);
        OutFrameLength = 60;
    }

    Sent = SendPacketToGuest(Reply, OutFrameLength);
    if (Sent)
    {
        g_DhcpTxCount++;
        if (g_Verbose || g_DhcpTxCount <= 4 || (g_DhcpTxCount % 128) == 0)
        {
            NetdLog("[NET] DHCP %s sent (rx=%lu tx=%lu fail=%lu xid=0x%08X yiaddr=%u.%u.%u.%u)\n",
                    (ReplyMessageType == 2) ? "OFFER" : "ACK",
                    g_DhcpRxCount,
                    g_DhcpTxCount,
                    g_DhcpTxFailCount,
                    ReadBe32(&OutBootp[4]),
                    g_GuestIp[0], g_GuestIp[1], g_GuestIp[2], g_GuestIp[3]);
        }
    }
    else
    {
        g_DhcpTxFailCount++;
        NetdLog("[WARN] DHCP %s send failed (rx=%lu tx=%lu fail=%lu xid=0x%08X)\n",
                (ReplyMessageType == 2) ? "OFFER" : "ACK",
                g_DhcpRxCount,
                g_DhcpTxCount,
                g_DhcpTxFailCount,
                ReadBe32(&OutBootp[4]));
    }

    return Sent;
}

/* ------------------------------------------------------------------ */
/* TCP handler: guest -> host forwarding                                */
/* ------------------------------------------------------------------ */

static void
HandleGuestTcp(
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_ ULONG IpHeaderLen)
{
    const UCHAR *Tcp;
    USHORT SrcPort, DstPort;
    ULONG SeqNum, AckNum;
    UCHAR Flags;
    ULONG TcpHeaderLen;
    ULONG TcpPayloadLen;
    ULONG EthPayloadLen;
    ULONG IpTotalLen;
    const UCHAR *Payload;
    NAT_ENTRY *Entry;

    if (FrameLength < 14 + IpHeaderLen)
        return;

    EthPayloadLen = FrameLength - 14;
    IpTotalLen = ReadBe16(&Frame[16]);
    if (IpTotalLen < IpHeaderLen + 20 || IpTotalLen > EthPayloadLen)
        return;

    Tcp = &Frame[14 + IpHeaderLen];
    TcpHeaderLen = (ULONG)((Tcp[12] >> 4) & 0x0F) * 4;
    if (TcpHeaderLen < 20 || IpTotalLen < IpHeaderLen + TcpHeaderLen)
        return;

    SrcPort = ReadBe16(&Tcp[0]);
    DstPort = ReadBe16(&Tcp[2]);
    SeqNum = ReadBe32(&Tcp[4]);
    AckNum = ReadBe32(&Tcp[8]);
    Flags = Tcp[13];
    TcpPayloadLen = IpTotalLen - IpHeaderLen - TcpHeaderLen;
    Payload = Tcp + TcpHeaderLen;

    if (g_Verbose)
    {
        char flagstr[32];
        int fi = 0;
        if (Flags & TCP_FLAG_SYN) flagstr[fi++] = 'S';
        if (Flags & TCP_FLAG_ACK) flagstr[fi++] = 'A';
        if (Flags & TCP_FLAG_FIN) flagstr[fi++] = 'F';
        if (Flags & TCP_FLAG_RST) flagstr[fi++] = 'R';
        if (Flags & TCP_FLAG_PSH) flagstr[fi++] = 'P';
        flagstr[fi] = '\0';
        NetdLog("[TCP] %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u [%s] seq=%u ack=%u len=%lu\n",
                SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort,
                flagstr, SeqNum, AckNum, TcpPayloadLen);
    }

    /* TODO: validate/check TCP checksum on ingress before NAT handling. */
    EnterCriticalSection(&g_NatLock);

    Entry = NatLookup(NAT_PROTO_TCP, SrcIp, SrcPort, DstIp, DstPort);

    /* ---- SYN: new connection ---- */
    if ((Flags & TCP_FLAG_SYN) && !(Flags & TCP_FLAG_ACK))
    {
        SOCKET Sock;
        struct sockaddr_in Addr;
        u_long NonBlock = 1;
        ULONG OurIsn;
        int ConnResult;
        UCHAR RealDstIp[4];

        if (Entry != NULL)
        {
            /* SYN retransmit while connect pending — just ignore */
            if (Entry->TcpState == TCP_STATE_SYN_SENT)
            {
                Entry->LastActivityTick = GetTickCount();
                LeaveCriticalSection(&g_NatLock);
                return;
            }
            /* SYN retransmit on established — resend SYN-ACK */
            if (Entry->TcpState == TCP_STATE_ESTABLISHED)
            {
                Entry->LastActivityTick = GetTickCount();
                LeaveCriticalSection(&g_NatLock);
                SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                               Entry->GuestAckBase,
                               Entry->GuestSeqBase + 1,
                               TCP_FLAG_SYN | TCP_FLAG_ACK,
                               NULL, 0, TRUE);
                return;
            }
            /* Old entry in closing state - recycle */
            NatFree(Entry);
            Entry = NULL;
        }

        Entry = NatAllocate();
        if (Entry == NULL)
        {
            LeaveCriticalSection(&g_NatLock);
            NetdLog("[TCP] NAT table full, dropping SYN from %u.%u.%u.%u:%u\n",
                    SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort);
            /* Send RST */
            SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                           0, SeqNum + 1,
                           TCP_FLAG_RST | TCP_FLAG_ACK,
                           NULL, 0, FALSE);
            return;
        }

        /* Resolve real destination IP: if guest sends to DNS IP (10.0.2.3),
         * use host DNS; if to gateway (10.0.2.2), use loopback; else real IP */
        memcpy(RealDstIp, DstIp, 4);
        if (memcmp(DstIp, g_DnsIp, 4) == 0)
            memcpy(RealDstIp, g_HostDnsIp, 4);
        else if (memcmp(DstIp, g_HostIp, 4) == 0)
        {
            RealDstIp[0] = 127; RealDstIp[1] = 0;
            RealDstIp[2] = 0;   RealDstIp[3] = 1;
        }

        Sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (Sock == INVALID_SOCKET)
        {
            NatFree(Entry);
            LeaveCriticalSection(&g_NatLock);
            NetdLog("[TCP] socket() failed (err=%d)\n", WSAGetLastError());
            SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                           0, SeqNum + 1,
                           TCP_FLAG_RST | TCP_FLAG_ACK,
                           NULL, 0, FALSE);
            return;
        }

        ioctlsocket(Sock, FIONBIO, &NonBlock);

        ZeroMemory(&Addr, sizeof(Addr));
        Addr.sin_family = AF_INET;
        Addr.sin_port = htons(DstPort);
        memcpy(&Addr.sin_addr, RealDstIp, 4);

        ConnResult = connect(Sock, (struct sockaddr *)&Addr, sizeof(Addr));
        /* Non-blocking connect returns SOCKET_ERROR with WSAEWOULDBLOCK */
        if (ConnResult == SOCKET_ERROR && WSAGetLastError() != WSAEWOULDBLOCK)
        {
            int WsaErr = WSAGetLastError();
            closesocket(Sock);
            NatFree(Entry);
            LeaveCriticalSection(&g_NatLock);
            NetdLog("[TCP] connect() to %u.%u.%u.%u:%u failed (err=%d)\n",
                    RealDstIp[0], RealDstIp[1], RealDstIp[2], RealDstIp[3],
                    DstPort, WsaErr);
            SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                           0, SeqNum + 1,
                           TCP_FLAG_RST | TCP_FLAG_ACK,
                           NULL, 0, FALSE);
            return;
        }

        /* Set up NAT entry */
        OurIsn = (ULONG)InterlockedAdd(&g_TcpIsnCounter, 64240);

        Entry->Protocol = NAT_PROTO_TCP;
        memcpy(Entry->GuestIp, SrcIp, 4);
        Entry->GuestPort = SrcPort;
        memcpy(Entry->DestIp, DstIp, 4);
        Entry->DestPort = DstPort;
        Entry->HostSocket = Sock;
        Entry->TcpState = TCP_STATE_SYN_SENT;
        Entry->GuestSeqBase = SeqNum;
        Entry->GuestAckBase = OurIsn;
        Entry->GuestSeqNext = SeqNum + 1; /* SYN consumes one sequence number */
        Entry->HostAckNext = OurIsn + 1;  /* Our SYN-ACK consumes one */
        Entry->BytesSentToHost = 0;
        Entry->BytesRecvFromHost = 0;
        Entry->TimeoutMs = NAT_TCP_TIMEOUT_MS;
        Entry->LastActivityTick = GetTickCount();

        LeaveCriticalSection(&g_NatLock);

        NetdLog("[TCP] New connection %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u (real %u.%u.%u.%u)\n",
                SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort,
                RealDstIp[0], RealDstIp[1], RealDstIp[2], RealDstIp[3]);

        /* SYN-ACK is deferred until host connect() completes (receive thread).
         * This avoids the guest sending data before the host socket is connected. */
        return;
    }

    /* All subsequent packets require an existing entry */
    if (Entry == NULL)
    {
        LeaveCriticalSection(&g_NatLock);
        if (!(Flags & TCP_FLAG_RST))
        {
            /* Send RST for unknown connection */
            if (Flags & TCP_FLAG_ACK)
            {
                SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                               AckNum, 0,
                               TCP_FLAG_RST,
                               NULL, 0, FALSE);
            }
            else
            {
                ULONG RstAck = SeqNum + TcpPayloadLen;
                if (Flags & TCP_FLAG_SYN) RstAck++;
                if (Flags & TCP_FLAG_FIN) RstAck++;
                SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                               0, RstAck,
                               TCP_FLAG_RST | TCP_FLAG_ACK,
                               NULL, 0, FALSE);
            }
        }
        return;
    }

    Entry->LastActivityTick = GetTickCount();

    /* ---- ACK/FIN after host already closed ---- */
    if (Entry->TcpState == TCP_STATE_CLOSE_WAIT)
    {
        if (Flags & TCP_FLAG_FIN)
        {
            ULONG AckSeq = Entry->GuestAckBase + Entry->BytesRecvFromHost + 2;
            NatFree(Entry);
            LeaveCriticalSection(&g_NatLock);
            SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                           AckSeq, SeqNum + TcpPayloadLen + 1,
                           TCP_FLAG_ACK,
                           NULL, 0, FALSE);
            return;
        }

        if (Flags & TCP_FLAG_ACK)
        {
            NatFree(Entry);
            LeaveCriticalSection(&g_NatLock);
            return;
        }
    }

    /* ---- RST from guest ---- */
    if (Flags & TCP_FLAG_RST)
    {
        NetdLog("[TCP] RST from guest %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
                SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort);
        NatFree(Entry);
        LeaveCriticalSection(&g_NatLock);
        return;
    }

    /* ---- ACK to our SYN-ACK (connection established) ---- */
    if (Entry->TcpState == TCP_STATE_SYN_SENT && (Flags & TCP_FLAG_ACK))
    {
        Entry->TcpState = TCP_STATE_ESTABLISHED;
        if (g_Verbose)
        {
            NetdLog("[TCP] Connection established %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
                    SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                    DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort);
        }
    }

    /* ---- Data from guest ---- */
    if (TcpPayloadLen > 0 &&
        Entry->TcpState == TCP_STATE_ESTABLISHED)
    {
        int Sent;

        /* Drop retransmissions: if SeqNum != GuestSeqNext, the data was already
         * forwarded to the host socket (or is out-of-order).  Sending it again
         * would duplicate bytes on the host TCP stream, corrupting HTTP etc. */
        if (SeqNum != Entry->GuestSeqNext)
        {
            ULONG DupAckNum = Entry->GuestAckBase + Entry->BytesRecvFromHost + 1;
            ULONG DupAckSeq = Entry->GuestSeqNext;
            LeaveCriticalSection(&g_NatLock);
            NetdLog("[TCP] Dropping retransmit seq=%u expected=%u len=%lu %u.%u.%u.%u:%u\n",
                    SeqNum, DupAckSeq, TcpPayloadLen,
                    SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort);
            SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                           DupAckNum, DupAckSeq,
                           TCP_FLAG_ACK,
                           NULL, 0, FALSE);
            return;
        }

        /* Forward payload to host socket. */
        Sent = send(Entry->HostSocket, (const char *)Payload, (int)TcpPayloadLen, 0);
        if (Sent == SOCKET_ERROR)
        {
            int WsaErr = WSAGetLastError();
            if (WsaErr == WSAEWOULDBLOCK)
            {
                /* Send buffer full — let guest TCP retransmit */
                LeaveCriticalSection(&g_NatLock);
                return;
            }

            {
                ULONG RstSeq = Entry->GuestAckBase + Entry->BytesRecvFromHost + 1;
                ULONG RstAck = SeqNum + TcpPayloadLen;

                NetdLog("[TCP] send() failed on %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u (err=%d)\n",
                        SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                        DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort, WsaErr);
                NatFree(Entry);
                LeaveCriticalSection(&g_NatLock);
                SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                               RstSeq, RstAck,
                               TCP_FLAG_RST | TCP_FLAG_ACK,
                               NULL, 0, FALSE);
                return;
            }
        }

        if (Sent <= 0)
        {
            LeaveCriticalSection(&g_NatLock);
            return;
        }

        /* send() may complete partially; ACK only what reached host socket. */
        {
            ULONG AckedLen = (ULONG)Sent;
            ULONG AckSeq = SeqNum + AckedLen;
            ULONG AckNum = Entry->GuestAckBase + Entry->BytesRecvFromHost + 1;

            Entry->BytesSentToHost += AckedLen;
            Entry->GuestSeqNext = AckSeq;

            /* ACK the guest's data */
            LeaveCriticalSection(&g_NatLock);
            SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                           AckNum, AckSeq,
                           TCP_FLAG_ACK,
                           NULL, 0, FALSE);
        }
        return;
    }

    /* ---- FIN from guest ---- */
    if (Flags & TCP_FLAG_FIN)
    {
        NetdLog("[TCP] FIN from guest %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
                SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort);

        Entry->TcpState = TCP_STATE_FIN_WAIT;

        /* Shutdown the host socket send side so host gets FIN too */
        shutdown(Entry->HostSocket, SD_SEND);

        /* ACK the FIN (FIN consumes one seq number) */
        LeaveCriticalSection(&g_NatLock);
        SendTcpToGuest(DstIp, SrcIp, DstPort, SrcPort,
                       Entry->GuestAckBase + Entry->BytesRecvFromHost + 1,
                       SeqNum + TcpPayloadLen + 1,
                       TCP_FLAG_ACK,
                       NULL, 0, FALSE);
        return;
    }

    LeaveCriticalSection(&g_NatLock);
}

/* ------------------------------------------------------------------ */
/* UDP handler: guest -> host forwarding                                */
/* ------------------------------------------------------------------ */

static void
HandleGuestUdp(
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_ ULONG IpHeaderLen)
{
    const UCHAR *Udp;
    USHORT SrcPort, DstPort;
    ULONG UdpTotalLen;
    ULONG UdpPayloadLen;
    const UCHAR *Payload;
    NAT_ENTRY *Entry;
    ULONG IpTotalLen;
    struct sockaddr_in Addr;
    UCHAR RealDstIp[4];
    int Sent;

    IpTotalLen = ReadBe16(&Frame[16]);
    if (IpTotalLen < IpHeaderLen + 8)
        return;

    Udp = &Frame[14 + IpHeaderLen];
    SrcPort = ReadBe16(&Udp[0]);
    DstPort = ReadBe16(&Udp[2]);
    UdpTotalLen = ReadBe16(&Udp[4]);
    if (UdpTotalLen < 8)
        return;
    UdpPayloadLen = UdpTotalLen - 8;
    Payload = Udp + 8;

    /* Sanity: make sure payload fits in frame */
    if (14 + IpHeaderLen + UdpTotalLen > FrameLength)
        return;

    if (g_Verbose)
    {
        NetdLog("[UDP] %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u len=%lu\n",
                SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort,
                UdpPayloadLen);
    }

    /* TODO: add configurable inbound UDP port-forwarding rules. */
    /* Resolve real destination: DNS 10.0.2.3 -> host DNS, gateway -> loopback */
    memcpy(RealDstIp, DstIp, 4);
    if (memcmp(DstIp, g_DnsIp, 4) == 0)
        memcpy(RealDstIp, g_HostDnsIp, 4);
    else if (memcmp(DstIp, g_HostIp, 4) == 0)
    {
        RealDstIp[0] = 127; RealDstIp[1] = 0;
        RealDstIp[2] = 0;   RealDstIp[3] = 1;
    }

    EnterCriticalSection(&g_NatLock);

    Entry = NatLookup(NAT_PROTO_UDP, SrcIp, SrcPort, DstIp, DstPort);
    if (Entry == NULL)
    {
        SOCKET Sock;
        u_long NonBlock = 1;

        Entry = NatAllocate();
        if (Entry == NULL)
        {
            LeaveCriticalSection(&g_NatLock);
            NetdLog("[UDP] NAT table full, dropping packet from %u.%u.%u.%u:%u\n",
                    SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort);
            return;
        }

        Sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if (Sock == INVALID_SOCKET)
        {
            NatFree(Entry);
            LeaveCriticalSection(&g_NatLock);
            NetdLog("[UDP] socket() failed (err=%d)\n", WSAGetLastError());
            return;
        }

        ioctlsocket(Sock, FIONBIO, &NonBlock);

        Entry->Protocol = NAT_PROTO_UDP;
        memcpy(Entry->GuestIp, SrcIp, 4);
        Entry->GuestPort = SrcPort;
        memcpy(Entry->DestIp, DstIp, 4);
        Entry->DestPort = DstPort;
        Entry->HostSocket = Sock;
        Entry->TimeoutMs = NAT_UDP_TIMEOUT_MS;
        Entry->LastActivityTick = GetTickCount();

        if (memcmp(DstIp, g_DnsIp, 4) == 0 && DstPort == 53)
        {
            NetdLog("[DNS] New DNS query from %u.%u.%u.%u:%u -> forwarding to %u.%u.%u.%u\n",
                    SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                    g_HostDnsIp[0], g_HostDnsIp[1], g_HostDnsIp[2], g_HostDnsIp[3]);
        }
        else if (g_Verbose)
        {
            NetdLog("[UDP] New session %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u (real %u.%u.%u.%u)\n",
                    SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                    DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort,
                    RealDstIp[0], RealDstIp[1], RealDstIp[2], RealDstIp[3]);
        }
    }

    Entry->LastActivityTick = GetTickCount();

    /* sendto() the UDP payload to the real destination */
    ZeroMemory(&Addr, sizeof(Addr));
    Addr.sin_family = AF_INET;
    Addr.sin_port = htons(DstPort);
    memcpy(&Addr.sin_addr, RealDstIp, 4);

    Sent = sendto(Entry->HostSocket, (const char *)Payload, (int)UdpPayloadLen, 0,
                  (struct sockaddr *)&Addr, sizeof(Addr));
    if (Sent == SOCKET_ERROR)
    {
        int WsaErr = WSAGetLastError();
        if (WsaErr != WSAEWOULDBLOCK)
        {
            NetdLog("[UDP] sendto() failed for %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u (err=%d)\n",
                    SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                    RealDstIp[0], RealDstIp[1], RealDstIp[2], RealDstIp[3],
                    DstPort, WsaErr);
        }
    }

    LeaveCriticalSection(&g_NatLock);
}

/* ------------------------------------------------------------------ */
/* Receive thread: host -> guest packet flow                            */
/* ------------------------------------------------------------------ */

static DWORD WINAPI
NatReceiveThread(
    _In_ LPVOID Param)
{
    fd_set ReadFds;
    fd_set WriteFds;
    fd_set ExceptFds;
    struct timeval Tv;
    DWORD CleanupTick;

    (void)Param;

    CleanupTick = GetTickCount();

    NetdLog("[NAT] Receive thread started\n");

    while (g_Running)
    {
        SOCKET MaxSock = 0;
        int SelectCount;
        ULONG i;
        BOOL HavePendingConnects = FALSE;
        int NumFds = 0;

        FD_ZERO(&ReadFds);
        FD_ZERO(&WriteFds);
        FD_ZERO(&ExceptFds);

        EnterCriticalSection(&g_NatLock);

        for (i = 0; i < NAT_TABLE_SIZE; i++)
        {
            NAT_ENTRY *E = &g_NatTable[i];
            if (!E->InUse || E->HostSocket == INVALID_SOCKET)
                continue;

            /* For TCP, only select on established or SYN_SENT connections */
            if (E->Protocol == NAT_PROTO_TCP)
            {
                if (E->TcpState == TCP_STATE_CLOSED)
                    continue;
                /* SYN_SENT: poll for connect completion via WriteFds */
                if (E->TcpState == TCP_STATE_SYN_SENT)
                {
                    FD_SET(E->HostSocket, &WriteFds);
                    FD_SET(E->HostSocket, &ExceptFds);
                    HavePendingConnects = TRUE;
                    if (E->HostSocket > MaxSock)
                        MaxSock = E->HostSocket;
                    NumFds++;
                    continue;
                }
            }

            FD_SET(E->HostSocket, &ReadFds);
            FD_SET(E->HostSocket, &ExceptFds);
            if (E->HostSocket > MaxSock)
                MaxSock = E->HostSocket;
            NumFds++;
        }

        LeaveCriticalSection(&g_NatLock);

        if (NumFds == 0)
        {
            /* No active sockets - just sleep briefly and check for timeouts */
            Sleep(1);

            /* Periodic cleanup */
            if ((GetTickCount() - CleanupTick) >= 5000)
            {
                CleanupTick = GetTickCount();
                EnterCriticalSection(&g_NatLock);
                NatCleanupExpired();
                LeaveCriticalSection(&g_NatLock);
            }
            continue;
        }

        Tv.tv_sec = 0;
        Tv.tv_usec = 1000; /* 1ms — keep latency low for interactive traffic */

        SelectCount = select(0, &ReadFds,
                            HavePendingConnects ? &WriteFds : NULL,
                            &ExceptFds, &Tv);
        if (SelectCount == SOCKET_ERROR)
        {
            int WsaErr = WSAGetLastError();
            if (WsaErr == WSAENOTSOCK)
            {
                /* Another thread closed a socket after fd_set snapshot.
                 * Rebuild the set on the next loop iteration. */
                continue;
            }
            if (WsaErr != WSAEINTR)
            {
                NetdLog("[NAT] select() failed (err=%d)\n", WsaErr);
                Sleep(10);
            }
            continue;
        }

        if (SelectCount > 0)
        {
            EnterCriticalSection(&g_NatLock);

            for (i = 0; i < NAT_TABLE_SIZE; i++)
            {
                NAT_ENTRY *E = &g_NatTable[i];
                if (!E->InUse || E->HostSocket == INVALID_SOCKET)
                    continue;

                /* Check for connect completion on SYN_SENT entries */
                if (E->Protocol == NAT_PROTO_TCP &&
                    E->TcpState == TCP_STATE_SYN_SENT &&
                    FD_ISSET(E->HostSocket, &WriteFds))
                {
                    /* Host connect() succeeded — send SYN-ACK to guest */
                    UCHAR gip[4], dip[4];
                    USHORT gport, dport;
                    ULONG isn, gseq;

                    E->TcpState = TCP_STATE_ESTABLISHED;
                    E->LastActivityTick = GetTickCount();

                    memcpy(gip, E->GuestIp, 4);
                    memcpy(dip, E->DestIp, 4);
                    gport = E->GuestPort;
                    dport = E->DestPort;
                    isn = E->GuestAckBase;
                    gseq = E->GuestSeqBase + 1;

                    LeaveCriticalSection(&g_NatLock);

                    NetdLog("[TCP] Connected %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
                            gip[0], gip[1], gip[2], gip[3], gport,
                            dip[0], dip[1], dip[2], dip[3], dport);

                    SendTcpToGuest(dip, gip, dport, gport,
                                   isn, gseq,
                                   TCP_FLAG_SYN | TCP_FLAG_ACK,
                                   NULL, 0, TRUE);

                    EnterCriticalSection(&g_NatLock);
                    continue;
                }

                /* Check for connect failure on SYN_SENT entries */
                if (E->Protocol == NAT_PROTO_TCP &&
                    E->TcpState == TCP_STATE_SYN_SENT &&
                    FD_ISSET(E->HostSocket, &ExceptFds))
                {
                    UCHAR gip[4], dip[4];
                    USHORT gport, dport;
                    ULONG gseq;

                    memcpy(gip, E->GuestIp, 4);
                    memcpy(dip, E->DestIp, 4);
                    gport = E->GuestPort;
                    dport = E->DestPort;
                    gseq = E->GuestSeqBase + 1;

                    NetdLog("[TCP] Connect failed %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
                            gip[0], gip[1], gip[2], gip[3], gport,
                            dip[0], dip[1], dip[2], dip[3], dport);

                    NatFree(E);
                    LeaveCriticalSection(&g_NatLock);

                    SendTcpToGuest(dip, gip, dport, gport,
                                   0, gseq,
                                   TCP_FLAG_RST | TCP_FLAG_ACK,
                                   NULL, 0, FALSE);

                    EnterCriticalSection(&g_NatLock);
                    continue;
                }

                /* Check for errors */
                if (FD_ISSET(E->HostSocket, &ExceptFds))
                {
                    if (E->Protocol == NAT_PROTO_TCP)
                    {
                        NetdLog("[TCP] Socket exception on %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
                                E->GuestIp[0], E->GuestIp[1], E->GuestIp[2], E->GuestIp[3],
                                E->GuestPort,
                                E->DestIp[0], E->DestIp[1], E->DestIp[2], E->DestIp[3],
                                E->DestPort);

                        /* Send RST to guest */
                        {
                            UCHAR gip[4], dip[4];
                            USHORT gport, dport;
                            ULONG ackBase, recvd;

                            memcpy(gip, E->GuestIp, 4);
                            memcpy(dip, E->DestIp, 4);
                            gport = E->GuestPort;
                            dport = E->DestPort;
                            ackBase = E->GuestAckBase;
                            recvd = E->BytesRecvFromHost;
                            NatFree(E);
                            LeaveCriticalSection(&g_NatLock);

                            SendTcpToGuest(dip, gip, dport, gport,
                                           ackBase + recvd + 1, 0,
                                           TCP_FLAG_RST,
                                           NULL, 0, FALSE);

                            EnterCriticalSection(&g_NatLock);
                        }
                    }
                    else
                    {
                        NatFree(E);
                    }
                    continue;
                }

                /* Check for readable data */
                if (!FD_ISSET(E->HostSocket, &ReadFds))
                    continue;

                if (E->Protocol == NAT_PROTO_TCP)
                {
                    UCHAR RecvBuf[TCP_MSS];
                    int Received;
                    UCHAR gip[4], dip[4];
                    USHORT gport, dport;
                    ULONG seqNum;

                    Received = recv(E->HostSocket, (char *)RecvBuf, sizeof(RecvBuf), 0);

                    if (Received > 0)
                    {
                        E->LastActivityTick = GetTickCount();

                        /* Our sequence number for this data:
                         * GuestAckBase + 1 (for SYN) + BytesRecvFromHost */
                        seqNum = E->GuestAckBase + 1 + E->BytesRecvFromHost;
                        E->BytesRecvFromHost += (ULONG)Received;

                        memcpy(gip, E->GuestIp, 4);
                        memcpy(dip, E->DestIp, 4);
                        gport = E->GuestPort;
                        dport = E->DestPort;

                        /* Must also set GuestSeqNext for ACK field */
                        {
                            ULONG ackToGuest = E->GuestSeqNext;
                            LeaveCriticalSection(&g_NatLock);

                            SendTcpToGuest(dip, gip, dport, gport,
                                           seqNum, ackToGuest,
                                           TCP_FLAG_ACK | TCP_FLAG_PSH,
                                           RecvBuf, (ULONG)Received, FALSE);

                            EnterCriticalSection(&g_NatLock);
                        }
                    }
                    else if (Received == 0)
                    {
                        /* Host closed connection - send FIN to guest */
                        NetdLog("[TCP] Host closed %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
                                E->GuestIp[0], E->GuestIp[1], E->GuestIp[2], E->GuestIp[3],
                                E->GuestPort,
                                E->DestIp[0], E->DestIp[1], E->DestIp[2], E->DestIp[3],
                                E->DestPort);

                        {
                            UCHAR gip2[4], dip2[4];
                            USHORT gport2, dport2;
                            ULONG finSeq, finAck;

                            memcpy(gip2, E->GuestIp, 4);
                            memcpy(dip2, E->DestIp, 4);
                            gport2 = E->GuestPort;
                            dport2 = E->DestPort;
                            finSeq = E->GuestAckBase + 1 + E->BytesRecvFromHost;
                            finAck = E->GuestSeqNext;

                            E->TcpState = TCP_STATE_CLOSE_WAIT;
                            E->TimeoutMs = NAT_TCP_CLOSE_TIMEOUT_MS;
                            E->LastActivityTick = GetTickCount();
                            if (E->HostSocket != INVALID_SOCKET)
                            {
                                shutdown(E->HostSocket, SD_BOTH);
                                closesocket(E->HostSocket);
                                E->HostSocket = INVALID_SOCKET;
                            }

                            LeaveCriticalSection(&g_NatLock);

                            SendTcpToGuest(dip2, gip2, dport2, gport2,
                                           finSeq, finAck,
                                           TCP_FLAG_FIN | TCP_FLAG_ACK,
                                           NULL, 0, FALSE);

                            EnterCriticalSection(&g_NatLock);
                        }
                    }
                    else
                    {
                        int WsaErr = WSAGetLastError();
                        if (WsaErr != WSAEWOULDBLOCK)
                        {
                            NetdLog("[TCP] recv() error on %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u (err=%d)\n",
                                    E->GuestIp[0], E->GuestIp[1], E->GuestIp[2], E->GuestIp[3],
                                    E->GuestPort,
                                    E->DestIp[0], E->DestIp[1], E->DestIp[2], E->DestIp[3],
                                    E->DestPort, WsaErr);

                            {
                                UCHAR gip3[4], dip3[4];
                                USHORT gport3, dport3;
                                ULONG rstSeq;

                                memcpy(gip3, E->GuestIp, 4);
                                memcpy(dip3, E->DestIp, 4);
                                gport3 = E->GuestPort;
                                dport3 = E->DestPort;
                                rstSeq = E->GuestAckBase + 1 + E->BytesRecvFromHost;
                                NatFree(E);
                                LeaveCriticalSection(&g_NatLock);

                                SendTcpToGuest(dip3, gip3, dport3, gport3,
                                               rstSeq, 0,
                                               TCP_FLAG_RST,
                                               NULL, 0, FALSE);

                                EnterCriticalSection(&g_NatLock);
                            }
                        }
                    }
                }
                else if (E->Protocol == NAT_PROTO_UDP)
                {
                    UCHAR RecvBuf[ROSV_NET_PACKET_MAX - 42]; /* Max UDP payload */
                    struct sockaddr_in FromAddr;
                    int FromLen = sizeof(FromAddr);
                    int Received;

                    Received = recvfrom(E->HostSocket, (char *)RecvBuf, sizeof(RecvBuf), 0,
                                        (struct sockaddr *)&FromAddr, &FromLen);

                    if (Received > 0)
                    {
                        UCHAR gip[4], dip[4];
                        USHORT gport, dport;

                        E->LastActivityTick = GetTickCount();

                        memcpy(gip, E->GuestIp, 4);
                        memcpy(dip, E->DestIp, 4);
                        gport = E->GuestPort;
                        dport = E->DestPort;

                        LeaveCriticalSection(&g_NatLock);

                        /* Send UDP response to guest.
                         * Source IP is the original dest IP the guest sent to
                         * (e.g., 10.0.2.3 for DNS), NOT the real host IP. */
                        SendUdpToGuest(dip, gip, dport, gport,
                                       RecvBuf, (ULONG)Received);

                        EnterCriticalSection(&g_NatLock);
                    }
                    else if (Received == SOCKET_ERROR)
                    {
                        int WsaErr = WSAGetLastError();
                        if (WsaErr != WSAEWOULDBLOCK &&
                            WsaErr != WSAECONNRESET &&
                            WsaErr != WSAEMSGSIZE)
                        {
                            NetdLog("[UDP] recvfrom() error (err=%d)\n", WsaErr);
                        }
                    }
                }
            }

            LeaveCriticalSection(&g_NatLock);
        }

        /* Periodic cleanup */
        if ((GetTickCount() - CleanupTick) >= 5000)
        {
            CleanupTick = GetTickCount();
            EnterCriticalSection(&g_NatLock);
            NatCleanupExpired();
            LeaveCriticalSection(&g_NatLock);
        }
    }

    NetdLog("[NAT] Receive thread stopped\n");
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main packet handler (enhanced with TCP/UDP dispatch)                 */
/* ------------------------------------------------------------------ */

static void
HandleGuestPacket(
    _In_reads_bytes_(Packet->Length) const ROSV_NET_PACKET *Packet)
{
    USHORT EthType;
    ULONG Ihl;
    UCHAR Proto;
    const UCHAR *SrcIp;
    const UCHAR *DstIp;

    if (Packet->Length < 14)
        return;

    /* Learn guest MAC from the first packet we see */
    if (Packet->Data[6] != 0 || Packet->Data[7] != 0 ||
        Packet->Data[8] != 0 || Packet->Data[9] != 0 ||
        Packet->Data[10] != 0 || Packet->Data[11] != 0)
    {
        memcpy(g_GuestMac, &Packet->Data[6], 6);
    }

    EthType = ReadBe16(&Packet->Data[12]);
    if (g_Verbose)
    {
        NetdLog("[RX] Packet %lu bytes eth=0x%04X dst=%02X:%02X:%02X:%02X:%02X:%02X src=%02X:%02X:%02X:%02X:%02X:%02X\n",
                Packet->Length,
                EthType,
                Packet->Data[0], Packet->Data[1], Packet->Data[2],
                Packet->Data[3], Packet->Data[4], Packet->Data[5],
                Packet->Data[6], Packet->Data[7], Packet->Data[8],
                Packet->Data[9], Packet->Data[10], Packet->Data[11]);
    }

    switch (EthType)
    {
        case ETH_TYPE_ARP:
            BuildAndSendArpReply(Packet->Data, Packet->Length);
            return;

        case ETH_TYPE_IPV4:
            break;

        default:
            /* TODO: add IPv6/NDP handling path in addition to ARP/IPv4. */
            return;
    }

    /* IPv4 processing */
    if (Packet->Length < 14 + 20)
        return;

    Ihl = (ULONG)(Packet->Data[14] & 0x0F) * 4;
    if ((Packet->Data[14] >> 4) != 4 || Ihl < 20)
        return;
    if (Packet->Length < 14 + Ihl)
        return;

    Proto = Packet->Data[23];
    SrcIp = &Packet->Data[26];
    DstIp = &Packet->Data[30];

    if (g_Verbose)
    {
        NetdLog("[RX] IPv4 proto=%u src=%u.%u.%u.%u dst=%u.%u.%u.%u\n",
                Proto,
                SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3],
                DstIp[0], DstIp[1], DstIp[2], DstIp[3]);
    }

    /* Try DHCP first (UDP port 67/68) */
    if (Proto == IP_PROTO_UDP)
    {
        if (BuildAndSendDhcpReply(Packet->Data, Packet->Length))
            return;
        /* Not DHCP - handle as regular UDP */
        HandleGuestUdp(Packet->Data, Packet->Length, SrcIp, DstIp, Ihl);
        return;
    }

    /* ICMP: local echo for gateway/DNS, forward to real host for external */
    if (Proto == IP_PROTO_ICMP)
    {
        NETD_ICMP_ECHO_INFO EchoInfo;

        NetdDecodeIcmpEcho(Packet->Data, Packet->Length, &EchoInfo);
        if (EchoInfo.Valid)
        {
            NetdLog("[PERF][ICMP] guest->netd id=0x%04X seq=%u type=%s len=%lu %u.%u.%u.%u->%u.%u.%u.%u\n",
                    EchoInfo.Id,
                    EchoInfo.Seq,
                    EchoInfo.IsRequest ? "echo-req" : "echo-reply",
                    Packet->Length,
                    EchoInfo.SrcIp[0], EchoInfo.SrcIp[1], EchoInfo.SrcIp[2], EchoInfo.SrcIp[3],
                    EchoInfo.DstIp[0], EchoInfo.DstIp[1], EchoInfo.DstIp[2], EchoInfo.DstIp[3]);
        }

        if (memcmp(DstIp, g_HostIp, 4) == 0 ||
            memcmp(DstIp, g_DnsIp, 4) == 0)
        {
            BuildAndSendIcmpEchoReply(Packet->Data, Packet->Length);
        }
        else
        {
            ForwardIcmpEcho(Packet->Data, Packet->Length);
        }
        return;
    }

    /* TCP forwarding */
    if (Proto == IP_PROTO_TCP)
    {
        HandleGuestTcp(Packet->Data, Packet->Length, SrcIp, DstIp, Ihl);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Logging                                                              */
/* ------------------------------------------------------------------ */

static void
NetdLog(
    _In_z_ _Printf_format_string_ const char *Fmt,
    ...)
{
    char Buffer[512];
    va_list Args;

    va_start(Args, Fmt);
    vsnprintf(Buffer, sizeof(Buffer), Fmt, Args);
    va_end(Args);

    Buffer[sizeof(Buffer) - 1] = '\0';
    RoslWriteStyledLog(Buffer);
    DbgPrint("rosl-netd: %s", Buffer);
}

/* ------------------------------------------------------------------ */
/* Console control handler                                              */
/* ------------------------------------------------------------------ */

static BOOL WINAPI
CtrlHandler(
    DWORD Type)
{
    (void)Type;
    g_Running = FALSE;
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* IOCTL wrapper                                                        */
/* ------------------------------------------------------------------ */

static BOOL
IoctlOnHandle(
    HANDLE DevHandle,
    DWORD Code,
    void *InBuf,
    DWORD InLen,
    void *OutBuf,
    DWORD OutLen,
    DWORD *BytesOut)
{
    DWORD Ret = 0;
    ULONGLONG StartQpc;
    ULONGLONG EndQpc;
    ULONGLONG IoctlUs;
    BOOL Ok;

    if (DevHandle == INVALID_HANDLE_VALUE)
    {
        SetLastError(ERROR_INVALID_HANDLE);
        if (BytesOut != NULL)
            *BytesOut = 0;
        return FALSE;
    }

    StartQpc = NetdReadQpc();
    Ok = DeviceIoControl(DevHandle,
                         Code,
                         InBuf,
                         InLen,
                         OutBuf,
                         OutLen,
                         &Ret,
                         NULL);
    EndQpc = NetdReadQpc();
    IoctlUs = NetdQpcDeltaUs(StartQpc, EndQpc);

    if (BytesOut != NULL)
        *BytesOut = Ret;

    if ((Code == ROSV_IOCTL_NET_RX || Code == ROSV_IOCTL_NET_TX) && Ok)
    {
        const ROSV_NET_PACKET *Packet = NULL;
        NETD_ICMP_ECHO_INFO EchoInfo;
        NETD_IP_ADDR_INFO IpInfo;
        BOOL ShouldLog = FALSE;
        const char *OpName = (Code == ROSV_IOCTL_NET_RX) ? "NET_RX" : "NET_TX";

        ZeroMemory(&EchoInfo, sizeof(EchoInfo));
        ZeroMemory(&IpInfo, sizeof(IpInfo));
        if (Code == ROSV_IOCTL_NET_RX &&
            OutBuf != NULL &&
            Ret >= sizeof(ULONG))
        {
            Packet = (const ROSV_NET_PACKET *)OutBuf;
            if (Packet->Length > 0 &&
                Packet->Length <= ROSV_NET_PACKET_MAX &&
                Ret >= (sizeof(ULONG) + Packet->Length))
            {
                NetdExtractIpAddrs(Packet->Data, Packet->Length, &IpInfo);
                NetdDecodeIcmpEcho(Packet->Data, Packet->Length, &EchoInfo);
                ShouldLog = EchoInfo.Valid || (IoctlUs >= PERF_SLOW_IOCTL_US);
            }
        }
        else if (Code == ROSV_IOCTL_NET_TX &&
                 InBuf != NULL &&
                 InLen >= sizeof(ULONG))
        {
            Packet = (const ROSV_NET_PACKET *)InBuf;
            if (Packet->Length > 0 &&
                Packet->Length <= ROSV_NET_PACKET_MAX &&
                InLen >= (sizeof(ULONG) + Packet->Length))
            {
                NetdExtractIpAddrs(Packet->Data, Packet->Length, &IpInfo);
                NetdDecodeIcmpEcho(Packet->Data, Packet->Length, &EchoInfo);
                ShouldLog = EchoInfo.Valid || (IoctlUs >= PERF_SLOW_IOCTL_US);
            }
        }

        if (ShouldLog && Packet != NULL)
        {
            if (EchoInfo.Valid)
            {
                NetdLog("[PERF] %s ioctl_us=%llu len=%lu proto=icmp id=0x%04X seq=%u %u.%u.%u.%u->%u.%u.%u.%u\n",
                        OpName,
                        IoctlUs,
                        Packet->Length,
                        EchoInfo.Id,
                        EchoInfo.Seq,
                        IpInfo.SrcIp[0], IpInfo.SrcIp[1], IpInfo.SrcIp[2], IpInfo.SrcIp[3],
                        IpInfo.DstIp[0], IpInfo.DstIp[1], IpInfo.DstIp[2], IpInfo.DstIp[3]);
            }
            else
            {
                const char *ProtoStr = IpInfo.Valid ? NetdProtoName(IpInfo.Protocol) : "arp";
                NetdLog("[PERF] %s ioctl_us=%llu len=%lu proto=%s %u.%u.%u.%u->%u.%u.%u.%u\n",
                        OpName,
                        IoctlUs,
                        Packet->Length,
                        ProtoStr,
                        IpInfo.SrcIp[0], IpInfo.SrcIp[1], IpInfo.SrcIp[2], IpInfo.SrcIp[3],
                        IpInfo.DstIp[0], IpInfo.DstIp[1], IpInfo.DstIp[2], IpInfo.DstIp[3]);
            }
        }
    }

    return Ok;
}

static BOOL
Ioctl(
    DWORD Code,
    void *InBuf,
    DWORD InLen,
    void *OutBuf,
    DWORD OutLen,
    DWORD *BytesOut)
{
    return IoctlOnHandle(g_hDev, Code, InBuf, InLen, OutBuf, OutLen, BytesOut);
}

/* ------------------------------------------------------------------ */
/* Argument parser                                                      */
/* ------------------------------------------------------------------ */

static int
ParseArgs(
    int argc,
    char **argv,
    ROSV_NET_BACKEND_TYPE *BackendOut,
    DWORD *PollMsOut,
    BOOL *VerboseOut)
{
    int i;
    *BackendOut = RosvNetBackendSlirp;
    *PollMsOut = 1;
    *VerboseOut = FALSE;

    for (i = 1; i < argc; ++i)
    {
        if (_stricmp(argv[i], "--help") == 0)
        {
            NetdLog("Usage: rosl_netd.exe [--backend=slirp|none] [--poll-ms=N] [--verbose]\n");
            NetdLog("       backend=netio is handled in-kernel and should not launch rosl_netd\n");
            return 1;
        }

        if (strncmp(argv[i], "--backend=", 10) == 0)
        {
            const char *Value = argv[i] + 10;
            if (_stricmp(Value, "slirp") == 0)
            {
                *BackendOut = RosvNetBackendSlirp;
                continue;
            }
            if (_stricmp(Value, "none") == 0)
            {
                *BackendOut = RosvNetBackendNone;
                continue;
            }
            if (_stricmp(Value, "netio") == 0)
            {
                NetdLog("[FAIL] backend=netio is internal to rosv.sys; rosl_netd only supports slirp|none\n");
                return -1;
            }

            NetdLog("[FAIL] Unknown backend: %s\n", Value);
            return -1;
        }

        if (strncmp(argv[i], "--poll-ms=", 10) == 0)
        {
            ULONG PollMs = (ULONG)atoi(argv[i] + 10);
            if (PollMs == 0 || PollMs > 5000)
            {
                NetdLog("[FAIL] Invalid --poll-ms value: %s\n", argv[i] + 10);
                return -1;
            }

            *PollMsOut = PollMs;
            continue;
        }

        if (_stricmp(argv[i], "--verbose") == 0)
        {
            *VerboseOut = TRUE;
            continue;
        }

        NetdLog("[FAIL] Unknown argument: %s\n", argv[i]);
        return -1;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Entry point                                                          */
/* ------------------------------------------------------------------ */

int
main(
    int argc,
    char **argv)
{
    ROSV_NET_BACKEND_TYPE BackendType;
    ROSV_NET_ATTACH_REQUEST Attach;
    ROSV_NET_PACKET Packet;
    ROSV_NET_STATS Stats;
    DWORD PollMs;
    DWORD Ret;
    DWORD LastStatsTick;
    ULONGLONG LastTxPackets;
    ULONGLONG LastRxPackets;
    BOOL Verbose;
    int ArgStatus;
    DWORD OpenRetryCount = 0;
    DWORD AttachRetryCount = 0;
    WSADATA WsaData;
    int WsaResult;

    NetdLog("=== rosl_netd - ROSV network backend daemon (NAT) ===\n");
    if (!QueryPerformanceFrequency(&g_PerfQpcFreq) || g_PerfQpcFreq.QuadPart <= 0)
    {
        g_PerfQpcFreq.QuadPart = 1;
        NetdLog("[WARN] QueryPerformanceFrequency failed, latency timings degraded\n");
    }
    else
    {
        NetdLog("[INFO] QPC frequency: %lld Hz\n", g_PerfQpcFreq.QuadPart);
    }

    /* Initialize Winsock */
    WsaResult = WSAStartup(MAKEWORD(2, 2), &WsaData);
    if (WsaResult != 0)
    {
        NetdLog("[FAIL] WSAStartup failed (err=%d)\n", WsaResult);
        return 1;
    }
    NetdLog("[OK]   Winsock %u.%u initialized\n",
            LOBYTE(WsaData.wVersion), HIBYTE(WsaData.wVersion));

    /* Initialize NAT table */
    NatTableInit();

    /* Resolve host DNS server */
    ResolveHostDns();

    ArgStatus = ParseArgs(argc, argv, &BackendType, &PollMs, &Verbose);
    if (ArgStatus != 0)
    {
        NatTableDestroy();
        WSACleanup();
        return ArgStatus > 0 ? 0 : 1;
    }
    g_Verbose = Verbose;

    if (!SetConsoleCtrlHandler(CtrlHandler, TRUE))
    {
        NetdLog("[WARN] SetConsoleCtrlHandler failed (err=%lu)\n", GetLastError());
    }

    while (g_Running)
    {
        g_hDev = CreateFileA(ROSV_DEVICE_PATH,
                             GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE,
                             NULL,
                             OPEN_EXISTING,
                             FILE_ATTRIBUTE_NORMAL,
                             NULL);
        if (g_hDev != INVALID_HANDLE_VALUE)
        {
            break;
        }

        if (OpenRetryCount == 0 || (OpenRetryCount % 50) == 0)
        {
            NetdLog("[WAIT] %s not ready yet (err=%lu), retrying...\n",
                    ROSV_DEVICE_PATH,
                    GetLastError());
        }
        OpenRetryCount++;
        Sleep(PollMs);
    }

    if (g_hDev == INVALID_HANDLE_VALUE)
    {
        NetdLog("[FAIL] Cannot open %s (daemon stopping)\n", ROSV_DEVICE_PATH);
        NatTableDestroy();
        WSACleanup();
        return 1;
    }

    ZeroMemory(&Attach, sizeof(Attach));
    Attach.BackendType = (ULONG)BackendType;
    memcpy(Attach.GuestMac, g_GuestMacDefault, sizeof(Attach.GuestMac));

    while (g_Running)
    {
        if (Ioctl(ROSV_IOCTL_NET_ATTACH,
                  &Attach,
                  sizeof(Attach),
                  NULL,
                  0,
                  &Ret))
        {
            break;
        }

        if (AttachRetryCount == 0 || (AttachRetryCount % 100) == 0)
        {
            NetdLog("[WAIT] NET_ATTACH pending (err=%lu), waiting for VM...\n",
                    GetLastError());
        }
        AttachRetryCount++;
        Sleep(PollMs);
    }

    if (!g_Running)
    {
        CloseHandle(g_hDev);
        g_hDev = INVALID_HANDLE_VALUE;
        NetdLog("[INFO] Stopped before NET_ATTACH completed\n");
        NatTableDestroy();
        WSACleanup();
        return 1;
    }

    NetdLog("[OK]   Attached backend=%lu (poll=%lu ms). Ctrl+C to stop.\n",
            (ULONG)BackendType, PollMs);

    g_hDevTx = CreateFileA(ROSV_DEVICE_PATH,
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           NULL);
    if (g_hDevTx == INVALID_HANDLE_VALUE)
    {
        NetdLog("[WARN] Dedicated NET_TX handle open failed (err=%lu), using shared handle\n",
                GetLastError());
    }
    else
    {
        NetdLog("[OK]   Dedicated NET_TX handle opened\n");
    }

    /* Start persistent ICMP worker thread */
    if (!IcmpWorkerInit())
    {
        NetdLog("[WARN] ICMP worker init failed, external ping will not work\n");
    }

    /* Start receive thread for host->guest traffic */
    g_RecvThread = CreateThread(NULL, 0, NatReceiveThread, NULL, 0, NULL);
    if (g_RecvThread == NULL)
    {
        NetdLog("[WARN] Failed to create receive thread (err=%lu)\n", GetLastError());
    }
    else
    {
        NetdLog("[OK]   NAT receive thread started\n");
    }

    /* Request 1ms timer resolution so Sleep(1) actually sleeps ~1ms
     * instead of the default 15.6ms granularity. */
    timeBeginPeriod(1);

    /* Note: avoid boosting thread/process priority here — on single-CPU
     * systems the rosl_netd polling loop will starve the VM. */

    LastStatsTick = GetTickCount();
    LastTxPackets = (ULONGLONG)-1;
    LastRxPackets = (ULONGLONG)-1;
    while (g_Running)
    {
        BOOL GotPacket = FALSE;

        /* Drain all available packets before sleeping (avoids 1-packet-per-poll bottleneck) */
        for (;;)
        {
            ZeroMemory(&Packet, sizeof(Packet));
            if (!Ioctl(ROSV_IOCTL_NET_RX, NULL, 0, &Packet, sizeof(Packet), &Ret))
                break;

            if (Ret < sizeof(Packet.Length) || Packet.Length == 0)
                break;

            if (Packet.Length > ROSV_NET_PACKET_MAX ||
                Ret < (sizeof(Packet.Length) + Packet.Length))
            {
                if (g_Verbose)
                {
                    NetdLog("[WARN] NET_RX short buffer: ret=%lu packet_len=%lu\n",
                            Ret, Packet.Length);
                }
                break;
            }

            GotPacket = TRUE;
            g_RxPacketCount++;
            if (g_RxPacketCount <= 4)
            {
                NetdLog("[NET] rx packet #%lu len=%lu\n",
                        g_RxPacketCount,
                        Packet.Length);
            }

            if (BackendType == RosvNetBackendSlirp)
            {
                HandleGuestPacket(&Packet);
                if (g_RxPacketCount <= 4)
                {
                    NetdLog("[NET] rx packet #%lu handled\n", g_RxPacketCount);
                }
            }
        }

        if ((GetTickCount() - LastStatsTick) >= 1000)
        {
            LastStatsTick = GetTickCount();
            ZeroMemory(&Stats, sizeof(Stats));
            if (Ioctl(ROSV_IOCTL_NET_GET_STATS, NULL, 0, &Stats, sizeof(Stats), &Ret))
            {
                if (LastTxPackets == (ULONGLONG)-1 ||
                    Stats.TxPackets != LastTxPackets ||
                    Stats.RxPackets != LastRxPackets)
                {
                    NetdLog("[NET] stats tx=%llu rx=%llu txB=%llu rxB=%llu\n",
                            Stats.TxPackets,
                            Stats.RxPackets,
                            Stats.TxBytes,
                            Stats.RxBytes);
                    LastTxPackets = Stats.TxPackets;
                    LastRxPackets = Stats.RxPackets;
                }

                if (g_Verbose)
                {
                    NetdLog("[STAT] attached=%lu backend=%lu tx=%llu/%lluB rx=%llu/%lluB\n",
                            Stats.Attached,
                            Stats.BackendType,
                            Stats.TxPackets,
                            Stats.TxBytes,
                            Stats.RxPackets,
                            Stats.RxBytes);
                }
            }
            else
            {
                NetdLog("[WARN] NET_GET_STATS failed (err=%lu)\n", GetLastError());
            }
        }

        /* No userspace sleep needed — kernel NET_RX blocks for up to 2ms
         * when no packet is available, providing natural backpressure. */
        (void)GotPacket;
    }

    g_Running = FALSE;

    /* Stop receive thread */
    if (g_RecvThread != NULL)
    {
        DWORD WaitRes;
        NetdLog("[INFO] Waiting for receive thread to stop...\n");
        WaitRes = WaitForSingleObject(g_RecvThread, INFINITE);
        if (WaitRes != WAIT_OBJECT_0)
        {
            NetdLog("[WARN] Receive thread wait failed (res=%lu err=%lu)\n",
                    WaitRes, GetLastError());
        }
        CloseHandle(g_RecvThread);
        g_RecvThread = NULL;
    }

    /* Stop persistent ICMP worker thread */
    IcmpWorkerDestroy();

    /* Restore default timer resolution */
    timeEndPeriod(1);

    /* Clean up NAT table (closes all sockets) */
    NatTableDestroy();

    if (!Ioctl(ROSV_IOCTL_NET_DETACH, NULL, 0, NULL, 0, &Ret))
    {
        NetdLog("[WARN] NET_DETACH failed (err=%lu)\n", GetLastError());
    }
    else
    {
        NetdLog("[OK]   Detached backend\n");
    }

    if (g_hDevTx != INVALID_HANDLE_VALUE)
    {
        CloseHandle(g_hDevTx);
        g_hDevTx = INVALID_HANDLE_VALUE;
    }

    CloseHandle(g_hDev);
    g_hDev = INVALID_HANDLE_VALUE;

    WSACleanup();
    NetdLog("[DONE] rosl_netd stopped\n");
    return 0;
}
