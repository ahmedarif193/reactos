/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     In-kernel NAT backend for virtio-net using NETIO/WSK
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 */

#include <ntifs.h>
#include <rosv/rosv.h>
#include <rosv/vm.h>
#include <rosv/net_backend.h>
#include <wsk.h>
#include <ndis.h>

/* netiodef.h declares this extern but only netio.sys normally defines it. */
CONST DL_EUI48 eui48_broadcast = {{ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }};

NTSYSAPI
NTSTATUS
NTAPI
ZwYieldExecution(
    VOID);

#define ROSV_NET_TAG                'nNvR'
#define ROSV_NET_TCP_MSS            1460
#define ROSV_NET_WINDOW_SIZE        65535
#define ROSV_NET_NAT_TABLE_SIZE     64
#define ROSV_NET_TCP_TIMEOUT_MS     300000
#define ROSV_NET_TCP_CLOSE_TIMEOUT_MS 30000
#define ROSV_NET_UDP_TIMEOUT_MS     30000
#define ROSV_NET_ICMP_TIMEOUT_MS    10000
#define ROSV_NET_PUMP_IDLE_TIMEOUT_MS 100
#define ROSV_NET_GUEST_INJECT_RETRY_COUNT 4
#define ROSV_NET_TRACE_TCP_GUEST_MAX 4096
#define ROSV_NET_TRACE_TCP_HOST_MAX 4096
#define ROSV_NET_TRACE_UDP_MAX     1024
#define ROSV_NET_TRACE_ICMP_MAX    256
#define ROSV_NET_SLOW_IO_LOG_MS    20

#define ETH_TYPE_IPV4               0x0800
#define ETH_TYPE_ARP                0x0806
#define IP_PROTO_ICMP               1
#define IP_PROTO_TCP                6
#define IP_PROTO_UDP                17

#define DHCP_CLIENT_PORT            68
#define DHCP_SERVER_PORT            67
#define DHCP_MAGIC_COOKIE           0x63825363UL

#define TCP_FLAG_FIN                0x01
#define TCP_FLAG_SYN                0x02
#define TCP_FLAG_RST                0x04
#define TCP_FLAG_PSH                0x08
#define TCP_FLAG_ACK                0x10

typedef enum _ROSV_NAT_PROTOCOL {
    RosvNatProtoTcp = 0,
    RosvNatProtoUdp = 1,
    RosvNatProtoIcmp = 2
} ROSV_NAT_PROTOCOL;

typedef enum _ROSV_TCP_STATE {
    RosvTcpConnecting = 0,
    RosvTcpSynAckSent,
    RosvTcpEstablished,
    RosvTcpFinWait,
    RosvTcpCloseWait,
    RosvTcpClosed
} ROSV_TCP_STATE;

typedef struct _ROSV_NAT_ENTRY {
    struct _ROSV_NET_BACKEND_STATE *Backend;
    BOOLEAN InUse;
    BOOLEAN Deleted;  /* Tombstone for open-addressing hash table */
    BOOLEAN Closing;
    BOOLEAN ThreadRunning;
    BOOLEAN RecvQueued;
    BOOLEAN RecvPosted;
    BOOLEAN GuestSendPending;
    BOOLEAN GuestSendAdvanceRecv;
    BOOLEAN GuestSendRepostRecv;
    BOOLEAN GuestSendClearEntry;
    ROSV_NAT_PROTOCOL Protocol;
    ROSV_TCP_STATE TcpState;
    UCHAR GuestIp[4];
    USHORT GuestPort;
    UCHAR DestIp[4];
    USHORT DestPort;
    UCHAR RealDestIp[4];
    USHORT RealDestPort;
    PWSK_SOCKET Socket;
    ULONG GuestSeqBase;
    ULONG GuestAckBase;
    ULONG GuestSeqNext;
    ULONG BytesSentToHost;
    ULONG BytesRecvFromHost;
    ULONGLONG LastActivityMs;
    ULONG TimeoutMs;
    ULONG Generation;
    ULONG GuestSendSeq;
    ULONG GuestSendAck;
    ULONG GuestSendLength;
    UCHAR GuestSendFlags;
    NTSTATUS RecvStatus;
    SIZE_T RecvBytes;
    ULONG RecvControlLength;
    ULONG RecvControlFlags;
    SOCKADDR_IN RecvRemoteAddr;
    PMDL RecvMdl;
    PIRP RecvIrp;
    WSK_BUF RecvWskBuf;
    UCHAR RecvBuffer[ROSV_NET_PACKET_MAX];
} ROSV_NAT_ENTRY, *PROSV_NAT_ENTRY;

struct _ROSV_NET_BACKEND_STATE {
    PROSV_VM Vm;
    FAST_MUTEX Lock;
    BOOLEAN Running;
    BOOLEAN Started;
    volatile LONG ActiveIoThreads;
    KEVENT IoThreadsDoneEvent;

    WSK_CLIENT_DISPATCH ClientDispatch;
    WSK_CLIENT_NPI ClientNpi;
    WSK_REGISTRATION WskRegistration;
    WSK_PROVIDER_NPI ProviderNpi;

    HANDLE PumpThreadHandle;
    PKTHREAD PumpThreadObject;

    ROSV_NAT_ENTRY NatTable[ROSV_NET_NAT_TABLE_SIZE];
    KSPIN_LOCK CompletionLock;
    KEVENT CompletionEvent;
    PROSV_NAT_ENTRY CompletionQueue[ROSV_NET_NAT_TABLE_SIZE];
    ULONG CompletionHead;
    ULONG CompletionTail;
    ULONG CompletionCount;

    ULONG64 TxPackets;
    ULONG64 TxBytes;
    ULONG64 RxPackets;
    ULONG64 RxBytes;

    UCHAR HostMac[6];
    UCHAR HostIp[4];
    UCHAR GuestIp[4];
    UCHAR GuestMask[4];
    UCHAR DnsIp[4];
    UCHAR GuestMac[6];
    UCHAR HostDnsIp[4];

    volatile LONG TcpIsnCounter;
    volatile LONG IpIdCounter;
    volatile LONG TraceTcpGuestCount;
    volatile LONG TraceTcpHostCount;
    volatile LONG TraceUdpCount;
    volatile LONG TraceIcmpCount;
    volatile LONG NatGenerationCounter;

};

static VOID
RosvNetNatReleaseClosed(
    _Inout_ PROSV_NAT_ENTRY Entry);

static VOID
RosvNetCleanupExpired(
    _Inout_ PROSV_NET_BACKEND_STATE Backend);

static BOOLEAN
RosvNetFlushPendingTcpSend(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _Inout_ PROSV_NAT_ENTRY Entry);

static USHORT
RosvNetReadBe16(
    _In_reads_bytes_(2) const UCHAR *Ptr)
{
    return (USHORT)(((USHORT)Ptr[0] << 8) | Ptr[1]);
}

static ULONG
RosvNetReadBe32(
    _In_reads_bytes_(4) const UCHAR *Ptr)
{
    return ((ULONG)Ptr[0] << 24) |
           ((ULONG)Ptr[1] << 16) |
           ((ULONG)Ptr[2] << 8) |
           (ULONG)Ptr[3];
}

static VOID
RosvNetWriteBe16(
    _Out_writes_bytes_(2) UCHAR *Ptr,
    _In_ USHORT Value)
{
    Ptr[0] = (UCHAR)((Value >> 8) & 0xFF);
    Ptr[1] = (UCHAR)(Value & 0xFF);
}

static VOID
RosvNetWriteBe32(
    _Out_writes_bytes_(4) UCHAR *Ptr,
    _In_ ULONG Value)
{
    Ptr[0] = (UCHAR)((Value >> 24) & 0xFF);
    Ptr[1] = (UCHAR)((Value >> 16) & 0xFF);
    Ptr[2] = (UCHAR)((Value >> 8) & 0xFF);
    Ptr[3] = (UCHAR)(Value & 0xFF);
}

static USHORT
RosvNetHostToNet16(
    _In_ USHORT Value)
{
    return (USHORT)((Value >> 8) | (Value << 8));
}

static ULONGLONG
RosvNetNowMs(VOID)
{
    return KeQueryInterruptTime() / 10000ULL;
}

static const char *
RosvNetTcpStateName(
    _In_ ROSV_TCP_STATE State)
{
    switch (State)
    {
        case RosvTcpConnecting:
            return "connecting";
        case RosvTcpSynAckSent:
            return "syn-ack";
        case RosvTcpEstablished:
            return "established";
        case RosvTcpFinWait:
            return "fin-wait";
        case RosvTcpCloseWait:
            return "close-wait";
        case RosvTcpClosed:
        default:
            return "closed";
    }
}

static VOID
RosvNetTcpFlagsToString(
    _In_ UCHAR Flags,
    _Out_writes_bytes_(BufferSize) PCHAR Buffer,
    _In_ SIZE_T BufferSize)
{
    SIZE_T Offset;

    if (BufferSize == 0)
        return;

    Offset = 0;
    if ((Flags & TCP_FLAG_SYN) && Offset + 1 < BufferSize)
        Buffer[Offset++] = 'S';
    if ((Flags & TCP_FLAG_ACK) && Offset + 1 < BufferSize)
        Buffer[Offset++] = 'A';
    if ((Flags & TCP_FLAG_FIN) && Offset + 1 < BufferSize)
        Buffer[Offset++] = 'F';
    if ((Flags & TCP_FLAG_RST) && Offset + 1 < BufferSize)
        Buffer[Offset++] = 'R';
    if ((Flags & TCP_FLAG_PSH) && Offset + 1 < BufferSize)
        Buffer[Offset++] = 'P';

    if (Offset == 0 && BufferSize > 1)
        Buffer[Offset++] = '-';

    Buffer[Offset] = '\0';
}

static BOOLEAN
RosvNetIpIsZero(
    _In_reads_(4) const UCHAR *Ip)
{
    return (BOOLEAN)(Ip[0] == 0 && Ip[1] == 0 && Ip[2] == 0 && Ip[3] == 0);
}

static USHORT
RosvNetInternetChecksum(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length)
{
    ULONG Sum;
    ULONG i;

    Sum = 0;
    /* Process 4 bytes at a time, defer carry folding */
    for (i = 0; i + 3 < Length; i += 4)
    {
        Sum += RosvNetReadBe16(&Data[i]);
        Sum += RosvNetReadBe16(&Data[i + 2]);
    }
    /* Handle remaining 2 bytes */
    if (i + 1 < Length)
    {
        Sum += RosvNetReadBe16(&Data[i]);
        i += 2;
    }
    /* Handle odd trailing byte */
    if (i < Length)
        Sum += ((ULONG)Data[i] << 8);

    /* Fold 32-bit sum to 16 bits */
    while (Sum > 0xFFFF)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);

    return (USHORT)(~(USHORT)Sum);
}

static USHORT
RosvNetUdpChecksum(
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_reads_bytes_(UdpLength) const UCHAR *Udp,
    _In_ ULONG UdpLength)
{
    ULONG Sum;
    ULONG i;

    /* Pseudo-header: src + dst IPs, protocol, length */
    Sum = RosvNetReadBe16(&SrcIp[0]) + RosvNetReadBe16(&SrcIp[2]) +
          RosvNetReadBe16(&DstIp[0]) + RosvNetReadBe16(&DstIp[2]) +
          IP_PROTO_UDP + UdpLength;

    /* Process 4 bytes at a time, defer carry folding */
    for (i = 0; i + 3 < UdpLength; i += 4)
    {
        Sum += RosvNetReadBe16(&Udp[i]);
        Sum += RosvNetReadBe16(&Udp[i + 2]);
    }
    if (i + 1 < UdpLength)
    {
        Sum += RosvNetReadBe16(&Udp[i]);
        i += 2;
    }
    if (i < UdpLength)
        Sum += ((ULONG)Udp[i] << 8);

    while (Sum > 0xFFFF)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);

    Sum = (~Sum) & 0xFFFF;
    return (USHORT)(Sum == 0 ? 0xFFFF : Sum);
}

static USHORT
RosvNetTcpChecksum(
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_reads_bytes_(TcpLength) const UCHAR *Tcp,
    _In_ ULONG TcpLength)
{
    ULONG Sum;
    ULONG i;

    /* Pseudo-header: src + dst IPs, protocol, length */
    Sum = RosvNetReadBe16(&SrcIp[0]) + RosvNetReadBe16(&SrcIp[2]) +
          RosvNetReadBe16(&DstIp[0]) + RosvNetReadBe16(&DstIp[2]) +
          IP_PROTO_TCP + TcpLength;

    /* Process 4 bytes at a time, defer carry folding */
    for (i = 0; i + 3 < TcpLength; i += 4)
    {
        Sum += RosvNetReadBe16(&Tcp[i]);
        Sum += RosvNetReadBe16(&Tcp[i + 2]);
    }
    if (i + 1 < TcpLength)
    {
        Sum += RosvNetReadBe16(&Tcp[i]);
        i += 2;
    }
    if (i < TcpLength)
        Sum += ((ULONG)Tcp[i] << 8);

    while (Sum > 0xFFFF)
        Sum = (Sum & 0xFFFF) + (Sum >> 16);

    Sum = (~Sum) & 0xFFFF;
    return (USHORT)(Sum == 0 ? 0xFFFF : Sum);
}

static NTSTATUS NTAPI
RosvNetCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    Event = (PKEVENT)Context;
    KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
RosvNetAllocateSyncIrp(
    _Out_ PIRP *IrpOut,
    _Out_ PKEVENT EventOut)
{
    PIRP Irp;

    Irp = IoAllocateIrp(1, FALSE);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeInitializeEvent(EventOut, NotificationEvent, FALSE);
    IoSetCompletionRoutine(Irp, RosvNetCompletion, EventOut, TRUE, TRUE, TRUE);
    Irp->Tail.Overlay.Thread = PsGetCurrentThread();
    *IrpOut = Irp;
    return STATUS_SUCCESS;
}

static NTSTATUS
RosvNetWaitSyncIrp(
    _Inout_ PIRP Irp,
    _Inout_ PKEVENT Event,
    _In_ NTSTATUS Status,
    _Out_opt_ PULONG_PTR Information,
    _In_opt_z_ const char *Operation)
{
    NTSTATUS FinalStatus;
    ULONGLONG StartMs;
    ULONGLONG ElapsedMs;
    ULONG_PTR IoInfo;

    FinalStatus = Status;
    StartMs = RosvNetNowMs();
    if (FinalStatus == STATUS_PENDING)
    {
        KeWaitForSingleObject(Event, Executive, KernelMode, FALSE, NULL);
        FinalStatus = Irp->IoStatus.Status;
    }

    IoInfo = Irp->IoStatus.Information;
    ElapsedMs = RosvNetNowMs() - StartMs;
    if (Information != NULL)
        *Information = IoInfo;

    if (!NT_SUCCESS(FinalStatus) || ElapsedMs >= ROSV_NET_SLOW_IO_LOG_MS)
    {
        DbgPrint("[ROSV:NET] syncio op=%s status=0x%08X info=%Iu wait_ms=%llu\n",
                 Operation != NULL ? Operation : "irp",
                 FinalStatus,
                 (SIZE_T)IoInfo,
                 (unsigned long long)ElapsedMs);
    }

    IoFreeIrp(Irp);
    return FinalStatus;
}

static NTSTATUS
RosvNetBuildWskBuf(
    _Inout_updates_bytes_(Length) VOID *Buffer,
    _In_ SIZE_T Length,
    _Out_ PMDL *MdlOut,
    _Out_ PWSK_BUF WskBuf)
{
    PMDL Mdl;

    if (Length == 0)
        return STATUS_INVALID_PARAMETER;

    Mdl = IoAllocateMdl(Buffer, (ULONG)Length, FALSE, FALSE, NULL);
    if (Mdl == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    MmBuildMdlForNonPagedPool(Mdl);
    WskBuf->Mdl = Mdl;
    WskBuf->Offset = 0;
    WskBuf->Length = Length;
    *MdlOut = Mdl;
    return STATUS_SUCCESS;
}

static NTSTATUS
RosvNetSocketCreate(
    _In_ PROSV_NET_BACKEND_STATE Backend,
    _In_ USHORT SocketType,
    _In_ ULONG Protocol,
    _In_ ULONG Flags,
    _Out_ PWSK_SOCKET *SocketOut)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    ULONG_PTR Information;
    const WSK_PROVIDER_DISPATCH *Dispatch;

    Dispatch = Backend->ProviderNpi.Dispatch;
    if (Dispatch == NULL)
        return STATUS_DEVICE_NOT_READY;

    Status = RosvNetAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Dispatch->WskSocket(Backend->ProviderNpi.Client,
                                 AF_INET,
                                 SocketType,
                                 Protocol,
                                 Flags,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 NULL,
                                 Irp);
    Information = 0;
    Status = RosvNetWaitSyncIrp(Irp, &Event, Status, &Information, "socket");
    if (!NT_SUCCESS(Status))
        return Status;

    *SocketOut = (PWSK_SOCKET)Information;
    return STATUS_SUCCESS;
}

static NTSTATUS
RosvNetSocketBind(
    _In_ PWSK_SOCKET Socket,
    _In_ PSOCKADDR LocalAddress)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch;

    Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch;
    Status = RosvNetAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Dispatch->WskBind(Socket, LocalAddress, 0, Irp);
    return RosvNetWaitSyncIrp(Irp, &Event, Status, NULL, "bind");
}

static NTSTATUS
RosvNetSocketConnect(
    _In_ PWSK_SOCKET Socket,
    _In_ PSOCKADDR RemoteAddress)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch;

    Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch;
    Status = RosvNetAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Dispatch->WskConnect(Socket, RemoteAddress, 0, Irp);
    return RosvNetWaitSyncIrp(Irp, &Event, Status, NULL, "connect");
}

static NTSTATUS
RosvNetSocketSend(
    _In_ PWSK_SOCKET Socket,
    _Inout_updates_bytes_(Length) VOID *Buffer,
    _In_ SIZE_T Length,
    _Out_opt_ PSIZE_T BytesSent)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PMDL Mdl;
    WSK_BUF WskBuf;
    ULONG_PTR Information;
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch;

    if (BytesSent != NULL)
        *BytesSent = 0;

    Status = RosvNetBuildWskBuf(Buffer, Length, &Mdl, &WskBuf);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RosvNetAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        return Status;
    }

    Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch;
    Status = Dispatch->WskSend(Socket, &WskBuf, 0, Irp);
    Information = 0;
    Status = RosvNetWaitSyncIrp(Irp, &Event, Status, &Information, "send");
    IoFreeMdl(Mdl);

    if (NT_SUCCESS(Status) && BytesSent != NULL)
        *BytesSent = (SIZE_T)Information;

    return Status;
}

static NTSTATUS
RosvNetSocketReceive(
    _In_ PWSK_SOCKET Socket,
    _Inout_updates_bytes_(Length) VOID *Buffer,
    _In_ SIZE_T Length,
    _Out_opt_ PSIZE_T BytesReceived)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PMDL Mdl;
    WSK_BUF WskBuf;
    ULONG_PTR Information;
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch;

    if (BytesReceived != NULL)
        *BytesReceived = 0;

    Status = RosvNetBuildWskBuf(Buffer, Length, &Mdl, &WskBuf);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RosvNetAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        return Status;
    }

    Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch;
    Status = Dispatch->WskReceive(Socket, &WskBuf, 0, Irp);
    Information = 0;
    Status = RosvNetWaitSyncIrp(Irp, &Event, Status, &Information, "recv");
    IoFreeMdl(Mdl);

    if (NT_SUCCESS(Status) && BytesReceived != NULL)
        *BytesReceived = (SIZE_T)Information;

    return Status;
}

static NTSTATUS
RosvNetSocketDisconnect(
    _In_ PWSK_SOCKET Socket,
    _In_ ULONG Flags)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch;

    Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Socket->Dispatch;
    Status = RosvNetAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = Dispatch->WskDisconnect(Socket, NULL, Flags, Irp);
    return RosvNetWaitSyncIrp(Irp, &Event, Status, NULL, "disconnect");
}

static NTSTATUS
RosvNetSocketSendTo(
    _In_ PWSK_SOCKET Socket,
    _Inout_updates_bytes_(Length) VOID *Buffer,
    _In_ SIZE_T Length,
    _In_ PSOCKADDR RemoteAddress,
    _Out_opt_ PSIZE_T BytesSent)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PMDL Mdl;
    WSK_BUF WskBuf;
    ULONG_PTR Information;
    PWSK_PROVIDER_DATAGRAM_DISPATCH Dispatch;

    if (BytesSent != NULL)
        *BytesSent = 0;

    Status = RosvNetBuildWskBuf(Buffer, Length, &Mdl, &WskBuf);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RosvNetAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        return Status;
    }

    Dispatch = (PWSK_PROVIDER_DATAGRAM_DISPATCH)Socket->Dispatch;
    Status = Dispatch->WskSendTo(Socket, &WskBuf, 0, RemoteAddress, 0, NULL, Irp);
    Information = 0;
    Status = RosvNetWaitSyncIrp(Irp, &Event, Status, &Information, "sendto");
    IoFreeMdl(Mdl);

    if (NT_SUCCESS(Status) && BytesSent != NULL)
        *BytesSent = (SIZE_T)Information;

    return Status;
}

static NTSTATUS
RosvNetSocketReceiveFrom(
    _In_ PWSK_SOCKET Socket,
    _Inout_updates_bytes_(Length) VOID *Buffer,
    _In_ SIZE_T Length,
    _Out_opt_ PSOCKADDR RemoteAddress,
    _Out_opt_ PSIZE_T BytesReceived)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PMDL Mdl;
    WSK_BUF WskBuf;
    ULONG ControlLength;
    ULONG ControlFlags;
    ULONG_PTR Information;
    PWSK_PROVIDER_DATAGRAM_DISPATCH Dispatch;

    if (BytesReceived != NULL)
        *BytesReceived = 0;

    Status = RosvNetBuildWskBuf(Buffer, Length, &Mdl, &WskBuf);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RosvNetAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        return Status;
    }

    Dispatch = (PWSK_PROVIDER_DATAGRAM_DISPATCH)Socket->Dispatch;
    ControlLength = 0;
    ControlFlags = 0;
    Status = Dispatch->WskReceiveFrom(Socket,
                                      &WskBuf,
                                      0,
                                      RemoteAddress,
                                      &ControlLength,
                                      NULL,
                                      &ControlFlags,
                                      Irp);
    Information = 0;
    Status = RosvNetWaitSyncIrp(Irp, &Event, Status, &Information, "recvfrom");
    IoFreeMdl(Mdl);

    if (NT_SUCCESS(Status) && BytesReceived != NULL)
        *BytesReceived = (SIZE_T)Information;

    return Status;
}

static VOID
RosvNetSocketClose(
    _In_opt_ PWSK_SOCKET Socket)
{
    PIRP Irp;
    KEVENT Event;
    NTSTATUS Status;
    PWSK_PROVIDER_BASIC_DISPATCH Dispatch;

    if (Socket == NULL)
        return;

    Dispatch = (PWSK_PROVIDER_BASIC_DISPATCH)Socket->Dispatch;
    Status = RosvNetAllocateSyncIrp(&Irp, &Event);
    if (!NT_SUCCESS(Status))
        return;

    Status = Dispatch->WskCloseSocket(Socket, Irp);
    (void)RosvNetWaitSyncIrp(Irp, &Event, Status, NULL, "close");
}

static VOID
RosvNetNatClearEntry(
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    PROSV_NET_BACKEND_STATE Backend;
    PMDL RecvMdl;
    PIRP RecvIrp;
    WSK_BUF RecvWskBuf;

    Backend = Entry->Backend;
    RecvMdl = Entry->RecvMdl;
    RecvIrp = Entry->RecvIrp;
    RecvWskBuf = Entry->RecvWskBuf;

    RtlZeroMemory(Entry, sizeof(*Entry));
    Entry->Backend = Backend;
    Entry->Deleted = TRUE; /* Tombstone: preserves probe chain for open addressing */
    Entry->RecvMdl = RecvMdl;
    Entry->RecvIrp = RecvIrp;
    Entry->RecvWskBuf = RecvWskBuf;
}

static VOID
RosvNetFreeRecvResources(
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    if (Entry->RecvIrp != NULL)
    {
        IoFreeIrp(Entry->RecvIrp);
        Entry->RecvIrp = NULL;
    }

    if (Entry->RecvMdl != NULL)
    {
        IoFreeMdl(Entry->RecvMdl);
        Entry->RecvMdl = NULL;
    }

    RtlZeroMemory(&Entry->RecvWskBuf, sizeof(Entry->RecvWskBuf));
}

static
VOID
RosvNetQueueRecvCompletion(
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    PROSV_NET_BACKEND_STATE Backend;
    KIRQL OldIrql;
    BOOLEAN Signal;

    Backend = Entry->Backend;
    if (Backend == NULL)
        return;

    Signal = FALSE;
    KeAcquireSpinLock(&Backend->CompletionLock, &OldIrql);
    if (!Entry->RecvQueued &&
        Backend->CompletionCount < ROSV_NET_NAT_TABLE_SIZE)
    {
        Backend->CompletionQueue[Backend->CompletionHead] = Entry;
        Backend->CompletionHead =
            (Backend->CompletionHead + 1) % ROSV_NET_NAT_TABLE_SIZE;
        Backend->CompletionCount++;
        Entry->RecvQueued = TRUE;
        Signal = TRUE;
    }
    KeReleaseSpinLock(&Backend->CompletionLock, OldIrql);

    if (Signal)
        KeSetEvent(&Backend->CompletionEvent, IO_NO_INCREMENT, FALSE);
    else if (!Entry->RecvQueued)
    {
        DbgPrint("[ROSV:NET] recv completion queue full proto=%u bytes=%Iu status=0x%08X count=%lu\n",
                 Entry->Protocol,
                 Entry->RecvBytes,
                 Entry->RecvStatus,
                 Backend->CompletionCount);
    }
}

static NTSTATUS NTAPI
RosvNetRecvCompletion(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PROSV_NAT_ENTRY Entry;
    ULONG PostedGeneration;

    UNREFERENCED_PARAMETER(DeviceObject);

    Entry = (PROSV_NAT_ENTRY)Context;
    PostedGeneration = PtrToUlong(Irp->Tail.Overlay.DriverContext[0]);
    if (PostedGeneration != Entry->Generation)
    {
        DbgPrint("[ROSV:NET] stale recv completion proto=%u status=0x%08X bytes=%Iu posted_gen=%lu entry_gen=%lu\n",
                 Entry->Protocol,
                 Irp->IoStatus.Status,
                 (SIZE_T)(NT_SUCCESS(Irp->IoStatus.Status) ? Irp->IoStatus.Information : 0),
                 PostedGeneration,
                 Entry->Generation);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    Entry->RecvStatus = Irp->IoStatus.Status;
    Entry->RecvBytes = NT_SUCCESS(Irp->IoStatus.Status) ? (SIZE_T)Irp->IoStatus.Information : 0;
    Entry->RecvPosted = FALSE;
    if (!NT_SUCCESS(Entry->RecvStatus) || Entry->RecvBytes == 0 || Entry->Protocol == RosvNatProtoTcp)
    {
        DbgPrint("[ROSV:NET] recv complete proto=%u status=0x%08X bytes=%Iu queued=%u posted=%u\n",
                 Entry->Protocol,
                 Entry->RecvStatus,
                 Entry->RecvBytes,
                 Entry->RecvQueued ? 1 : 0,
                 Entry->RecvPosted ? 1 : 0);
    }
    RosvNetQueueRecvCompletion(Entry);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
RosvNetEnsureRecvResources(
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    if (Entry->RecvMdl == NULL)
    {
        Entry->RecvMdl = IoAllocateMdl(Entry->RecvBuffer,
                                       sizeof(Entry->RecvBuffer),
                                       FALSE,
                                       FALSE,
                                       NULL);
        if (Entry->RecvMdl == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        MmBuildMdlForNonPagedPool(Entry->RecvMdl);
        Entry->RecvWskBuf.Mdl = Entry->RecvMdl;
        Entry->RecvWskBuf.Offset = 0;
        Entry->RecvWskBuf.Length = 0;
    }

    if (Entry->RecvIrp == NULL)
    {
        Entry->RecvIrp = IoAllocateIrp(1, FALSE);
        if (Entry->RecvIrp == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
RosvNetPostTcpReceive(
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    NTSTATUS Status;
    PWSK_PROVIDER_CONNECTION_DISPATCH Dispatch;

    if (Entry->Socket == NULL || Entry->RecvPosted)
        return STATUS_INVALID_DEVICE_STATE;

    Status = RosvNetEnsureRecvResources(Entry);
    if (!NT_SUCCESS(Status))
        return Status;

    Entry->RecvWskBuf.Offset = 0;
    Entry->RecvWskBuf.Length = ROSV_NET_TCP_MSS;
    Entry->RecvStatus = STATUS_PENDING;
    Entry->RecvBytes = 0;
    Entry->RecvPosted = TRUE;
    IoReuseIrp(Entry->RecvIrp, STATUS_UNSUCCESSFUL);
    Entry->RecvIrp->Tail.Overlay.DriverContext[0] = UlongToPtr(Entry->Generation);
    IoSetCompletionRoutine(Entry->RecvIrp,
                           RosvNetRecvCompletion,
                           Entry,
                           TRUE,
                           TRUE,
                           TRUE);

    Dispatch = (PWSK_PROVIDER_CONNECTION_DISPATCH)Entry->Socket->Dispatch;
    Status = Dispatch->WskReceive(Entry->Socket,
                                  &Entry->RecvWskBuf,
                                  0,
                                  Entry->RecvIrp);
    if (Status != STATUS_PENDING)
    {
        DbgPrint("[ROSV:NET] tcp recv post immediate status=0x%08X bytes=%Iu queued=%u\n",
                 Status,
                 (SIZE_T)Entry->RecvIrp->IoStatus.Information,
                 Entry->RecvQueued ? 1 : 0);
    }
    if (Status != STATUS_PENDING && !Entry->RecvQueued)
    {
        Entry->RecvStatus = Status;
        Entry->RecvBytes = (SIZE_T)Entry->RecvIrp->IoStatus.Information;
        Entry->RecvPosted = FALSE;
        RosvNetQueueRecvCompletion(Entry);
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
RosvNetPostUdpReceive(
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    NTSTATUS Status;
    PWSK_PROVIDER_DATAGRAM_DISPATCH Dispatch;

    if (Entry->Socket == NULL || Entry->RecvPosted)
        return STATUS_INVALID_DEVICE_STATE;

    Status = RosvNetEnsureRecvResources(Entry);
    if (!NT_SUCCESS(Status))
        return Status;

    Entry->RecvWskBuf.Offset = 0;
    Entry->RecvWskBuf.Length = sizeof(Entry->RecvBuffer);
    Entry->RecvControlLength = 0;
    Entry->RecvControlFlags = 0;
    RtlZeroMemory(&Entry->RecvRemoteAddr, sizeof(Entry->RecvRemoteAddr));
    Entry->RecvStatus = STATUS_PENDING;
    Entry->RecvBytes = 0;
    Entry->RecvPosted = TRUE;
    IoReuseIrp(Entry->RecvIrp, STATUS_UNSUCCESSFUL);
    Entry->RecvIrp->Tail.Overlay.DriverContext[0] = UlongToPtr(Entry->Generation);
    IoSetCompletionRoutine(Entry->RecvIrp,
                           RosvNetRecvCompletion,
                           Entry,
                           TRUE,
                           TRUE,
                           TRUE);

    Dispatch = (PWSK_PROVIDER_DATAGRAM_DISPATCH)Entry->Socket->Dispatch;
    Status = Dispatch->WskReceiveFrom(Entry->Socket,
                                      &Entry->RecvWskBuf,
                                      0,
                                      (PSOCKADDR)&Entry->RecvRemoteAddr,
                                      &Entry->RecvControlLength,
                                      NULL,
                                      &Entry->RecvControlFlags,
                                      Entry->RecvIrp);
    if (Status != STATUS_PENDING && !Entry->RecvQueued)
    {
        Entry->RecvStatus = Status;
        Entry->RecvBytes = (SIZE_T)Entry->RecvIrp->IoStatus.Information;
        Entry->RecvPosted = FALSE;
        RosvNetQueueRecvCompletion(Entry);
    }

    return STATUS_SUCCESS;
}

static VOID
RosvNetBuildWildcardAddress(
    _Out_ PSOCKADDR_IN Address)
{
    RtlZeroMemory(Address, sizeof(*Address));
    Address->sin_family = AF_INET;
    Address->sin_port = 0;
    Address->sin_addr.S_un.S_addr = 0;
}

static VOID
RosvNetBuildAddress(
    _Out_ PSOCKADDR_IN Address,
    _In_reads_(4) const UCHAR *Ip,
    _In_ USHORT Port)
{
    RtlZeroMemory(Address, sizeof(*Address));
    Address->sin_family = AF_INET;
    Address->sin_port = RosvNetHostToNet16(Port);
    RtlCopyMemory(&Address->sin_addr, Ip, sizeof(Address->sin_addr));
}

static UCHAR *
RosvNetBuildEthIpHeader(
    _Out_writes_bytes_(ROSV_NET_PACKET_MAX) UCHAR *Frame,
    _In_reads_bytes_(6) const UCHAR *DstMac,
    _In_reads_bytes_(6) const UCHAR *SrcMac,
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_ UCHAR Protocol,
    _In_ USHORT IpTotalLength,
    _Inout_ PROSV_NET_BACKEND_STATE Backend)
{
    USHORT IpId;
    UCHAR *Ip;

    RtlCopyMemory(&Frame[0], DstMac, 6);
    RtlCopyMemory(&Frame[6], SrcMac, 6);
    RosvNetWriteBe16(&Frame[12], ETH_TYPE_IPV4);

    Ip = &Frame[14];
    Ip[0] = 0x45;
    Ip[1] = 0x00;
    RosvNetWriteBe16(&Ip[2], IpTotalLength);
    IpId = (USHORT)InterlockedIncrement(&Backend->IpIdCounter);
    RosvNetWriteBe16(&Ip[4], IpId);
    RosvNetWriteBe16(&Ip[6], 0x4000);
    Ip[8] = 64;
    Ip[9] = Protocol;
    Ip[10] = 0;
    Ip[11] = 0;
    RtlCopyMemory(&Ip[12], SrcIp, 4);
    RtlCopyMemory(&Ip[16], DstIp, 4);
    RosvNetWriteBe16(&Ip[10], RosvNetInternetChecksum(Ip, 20));

    return &Frame[34];
}

static BOOLEAN
RosvNetSendPacketToGuest(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_bytes_(Length) const UCHAR *Frame,
    _In_ ULONG Length)
{
    ULONG Attempt;

    if (!Backend->Running)
        return FALSE;

    for (Attempt = 0; Attempt < ROSV_NET_GUEST_INJECT_RETRY_COUNT; Attempt++)
    {
        if (RosvVirtioNetInjectRxPacket(&Backend->Vm->VirtioNet, Frame, Length))
        {
            Backend->RxPackets++;
            Backend->RxBytes += Length;
            if (Attempt != 0)
            {
                DbgPrint("[ROSV:NET] RX inject len=%lu succeeded after retries=%lu\n",
                         Length,
                         Attempt);
            }
            return TRUE;
        }

        if (!Backend->Running)
            return FALSE;

        if (Backend->Vm != NULL)
            KeSetEvent(&Backend->Vm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);

        if ((Attempt + 1) < ROSV_NET_GUEST_INJECT_RETRY_COUNT)
        {
            DbgPrint("[ROSV:NET] RX inject retry len=%lu attempt=%lu\n",
                     Length,
                     Attempt + 1);
            (void)ZwYieldExecution();
        }
    }

    DbgPrint("[ROSV:NET] RX inject failed len=%lu\n", Length);
    return FALSE;
}

static BOOLEAN
RosvNetSendTcpToGuest(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_ USHORT SrcPort,
    _In_ USHORT DstPort,
    _In_ ULONG SeqNum,
    _In_ ULONG AckNum,
    _In_ UCHAR Flags,
    _In_reads_bytes_opt_(PayloadLen) const UCHAR *Payload,
    _In_ ULONG PayloadLen,
    _In_ BOOLEAN IncludeMss)
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

    /* Padding to min Ethernet frame is handled below, not full buffer */
    Tcp = RosvNetBuildEthIpHeader(Frame,
                                  Backend->GuestMac,
                                  Backend->HostMac,
                                  SrcIp,
                                  DstIp,
                                  IP_PROTO_TCP,
                                  (USHORT)IpTotalLen,
                                  Backend);

    RosvNetWriteBe16(&Tcp[0], SrcPort);
    RosvNetWriteBe16(&Tcp[2], DstPort);
    RosvNetWriteBe32(&Tcp[4], SeqNum);
    RosvNetWriteBe32(&Tcp[8], AckNum);
    Tcp[12] = (UCHAR)((TcpHeaderLen / 4) << 4);
    Tcp[13] = Flags;
    RosvNetWriteBe16(&Tcp[14], ROSV_NET_WINDOW_SIZE);

    if (IncludeMss)
    {
        Tcp[20] = 0x02;
        Tcp[21] = 0x04;
        RosvNetWriteBe16(&Tcp[22], ROSV_NET_TCP_MSS);
    }

    if (Payload != NULL && PayloadLen > 0)
        RtlCopyMemory(&Tcp[TcpHeaderLen], Payload, PayloadLen);

    Tcp[16] = 0;
    Tcp[17] = 0;
    RosvNetWriteBe16(&Tcp[16], RosvNetTcpChecksum(&Frame[26], &Frame[30], Tcp, TcpTotalLen));

    if (FrameLen < 60)
    {
        RtlZeroMemory(&Frame[FrameLen], 60 - FrameLen);
        FrameLen = 60;
    }

    return RosvNetSendPacketToGuest(Backend, Frame, FrameLen);
}

static BOOLEAN
RosvNetSendUdpToGuest(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
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

    /* Padding to min Ethernet frame is handled below, not full buffer */
    Udp = RosvNetBuildEthIpHeader(Frame,
                                  Backend->GuestMac,
                                  Backend->HostMac,
                                  SrcIp,
                                  DstIp,
                                  IP_PROTO_UDP,
                                  (USHORT)IpTotalLen,
                                  Backend);

    RosvNetWriteBe16(&Udp[0], SrcPort);
    RosvNetWriteBe16(&Udp[2], DstPort);
    RosvNetWriteBe16(&Udp[4], (USHORT)UdpTotalLen);
    if (Payload != NULL && PayloadLen > 0)
        RtlCopyMemory(&Udp[8], Payload, PayloadLen);

    Udp[6] = 0;
    Udp[7] = 0;
    RosvNetWriteBe16(&Udp[6], RosvNetUdpChecksum(&Frame[26], &Frame[30], Udp, UdpTotalLen));

    if (FrameLen < 60)
    {
        RtlZeroMemory(&Frame[FrameLen], 60 - FrameLen);
        FrameLen = 60;
    }

    return RosvNetSendPacketToGuest(Backend, Frame, FrameLen);
}

static BOOLEAN
RosvNetSendIcmpToGuest(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_bytes_(4) const UCHAR *SrcIp,
    _In_reads_bytes_(4) const UCHAR *DstIp,
    _In_reads_bytes_(IcmpLen) const UCHAR *IcmpData,
    _In_ ULONG IcmpLen)
{
    UCHAR Frame[ROSV_NET_PACKET_MAX];
    UCHAR *Icmp;
    ULONG IpTotalLen;
    ULONG FrameLen;

    IpTotalLen = 20 + IcmpLen;
    FrameLen = 14 + IpTotalLen;

    if (FrameLen > sizeof(Frame))
        return FALSE;

    /* Padding to min Ethernet frame is handled below, not full buffer */
    Icmp = RosvNetBuildEthIpHeader(Frame,
                                   Backend->GuestMac,
                                   Backend->HostMac,
                                   SrcIp,
                                   DstIp,
                                   IP_PROTO_ICMP,
                                   (USHORT)IpTotalLen,
                                   Backend);

    RtlCopyMemory(Icmp, IcmpData, IcmpLen);

    if (FrameLen < 60)
    {
        RtlZeroMemory(&Frame[FrameLen], 60 - FrameLen);
        FrameLen = 60;
    }

    return RosvNetSendPacketToGuest(Backend, Frame, FrameLen);
}

static VOID
RosvNetProcessIcmpRecvCompletion(
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    PROSV_NET_BACKEND_STATE Backend;
    UCHAR GuestIp[4];
    UCHAR DestIp[4];
    USHORT IcmpId;
    PWSK_SOCKET Socket;
    NTSTATUS Status;
    SIZE_T BytesReceived;
    BOOLEAN Repost;
    const UCHAR *RecvIcmp;
    ULONG RecvIcmpLen;

    Backend = Entry->Backend;
    Socket = NULL;
    IcmpId = 0;
    Status = Entry->RecvStatus;
    BytesReceived = Entry->RecvBytes;
    Repost = FALSE;

    ExAcquireFastMutex(&Backend->Lock);
    if (!Entry->InUse || Entry->Closing || !Backend->Running)
    {
        ExReleaseFastMutex(&Backend->Lock);
        return;
    }

    if (!NT_SUCCESS(Status))
    {
        DbgPrint("[ROSV:NET] ICMP recv failed status=0x%08X\n", Status);
        Socket = Entry->Socket;
        Entry->Socket = NULL;
        Entry->ThreadRunning = FALSE;
        RosvNetNatClearEntry(Entry);
        ExReleaseFastMutex(&Backend->Lock);

        if (Socket != NULL)
            RosvNetSocketClose(Socket);
        return;
    }

    RtlCopyMemory(GuestIp, Entry->GuestIp, 4);
    RtlCopyMemory(DestIp, Entry->DestIp, 4);
    IcmpId = Entry->GuestPort;
    Entry->LastActivityMs = RosvNetNowMs();
    Repost = (BOOLEAN)(Entry->Socket != NULL && !Entry->Closing);
    ExReleaseFastMutex(&Backend->Lock);

    /*
     * RawIP TDI returns data including the IP header on receive.
     * Skip it to get the ICMP payload.
     */
    if (BytesReceived > 0)
    {
        RecvIcmp = Entry->RecvBuffer;
        RecvIcmpLen = (ULONG)BytesReceived;

        /* Skip IP header if present (RawIP TDI includes it on receive) */
        if (RecvIcmpLen >= 20 && (RecvIcmp[0] >> 4) == 4)
        {
            ULONG IpHdrLen = (ULONG)(RecvIcmp[0] & 0x0F) * 4;
            if (IpHdrLen >= 20 && IpHdrLen < RecvIcmpLen)
            {
                DbgPrint("[ROSV:NET] ICMP recv: stripping IP hdr len=%u from %u bytes\n",
                         IpHdrLen, RecvIcmpLen);
                RecvIcmp += IpHdrLen;
                RecvIcmpLen -= IpHdrLen;
            }
        }

        /* Verify it's an ICMP echo reply (type 0) with matching ID */
        if (RecvIcmpLen >= 8 && RecvIcmp[0] == 0 && RecvIcmp[1] == 0)
        {
            USHORT RecvId = RosvNetReadBe16(&RecvIcmp[4]);
            if (RecvId == IcmpId)
            {
                DbgPrint("[ROSV:NET] ICMP reply %u.%u.%u.%u -> %u.%u.%u.%u id=%u seq=%u len=%u\n",
                         DestIp[0], DestIp[1], DestIp[2], DestIp[3],
                         GuestIp[0], GuestIp[1], GuestIp[2], GuestIp[3],
                         IcmpId, RosvNetReadBe16(&RecvIcmp[6]), RecvIcmpLen);

                (void)RosvNetSendIcmpToGuest(Backend,
                                             DestIp,
                                             GuestIp,
                                             RecvIcmp,
                                             RecvIcmpLen);
            }
            else
            {
                DbgPrint("[ROSV:NET] ICMP reply id mismatch: got %u expected %u\n", RecvId, IcmpId);
            }
        }
        else if (RecvIcmpLen >= 8)
        {
            DbgPrint("[ROSV:NET] ICMP recv type=%u code=%u (not echo reply)\n", RecvIcmp[0], RecvIcmp[1]);
        }
    }

    if (Repost)
    {
        ExAcquireFastMutex(&Backend->Lock);
        if (Entry->InUse &&
            !Entry->Closing &&
            Entry->Socket != NULL &&
            Backend->Running)
        {
            (void)RosvNetPostUdpReceive(Entry);
        }
        ExReleaseFastMutex(&Backend->Lock);
    }
}

static VOID
RosvNetProcessTcpRecvCompletion(
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    PROSV_NET_BACKEND_STATE Backend;
    UCHAR GuestIp[4];
    UCHAR DestIp[4];
    USHORT GuestPort;
    USHORT DestPort;
    ULONG SeqNum;
    ULONG AckNum;
    ULONG FinSeq;
    ULONG FinAck;
    ULONG RstSeq;
    PWSK_SOCKET Socket;
    NTSTATUS Status;
    SIZE_T BytesReceived;
    BOOLEAN SendData;
    BOOLEAN SendFin;
    BOOLEAN SendRst;
    BOOLEAN ShouldLog;
    const char *StateName;

    Backend = Entry->Backend;
    Socket = NULL;
    GuestPort = 0;
    DestPort = 0;
    SeqNum = 0;
    AckNum = 0;
    FinSeq = 0;
    FinAck = 0;
    RstSeq = 0;
    SendData = FALSE;
    SendFin = FALSE;
    SendRst = FALSE;
    ShouldLog = FALSE;
    StateName = "closed";
    BytesReceived = Entry->RecvBytes;
    Status = Entry->RecvStatus;

    ExAcquireFastMutex(&Backend->Lock);
    if (!Entry->InUse || Entry->Closing || !Backend->Running)
    {
        ExReleaseFastMutex(&Backend->Lock);
        return;
    }

    Entry->LastActivityMs = RosvNetNowMs();
    RtlCopyMemory(GuestIp, Entry->GuestIp, 4);
    RtlCopyMemory(DestIp, Entry->DestIp, 4);
    GuestPort = Entry->GuestPort;
    DestPort = Entry->DestPort;
    StateName = RosvNetTcpStateName(Entry->TcpState);
    ShouldLog = (InterlockedIncrement(&Backend->TraceTcpHostCount) <= ROSV_NET_TRACE_TCP_HOST_MAX) ||
                !NT_SUCCESS(Status) ||
                BytesReceived == 0;

    if (!NT_SUCCESS(Status))
    {
        Socket = Entry->Socket;
        RstSeq = Entry->GuestAckBase + 1 + Entry->BytesRecvFromHost;
        Entry->Socket = NULL;
        Entry->ThreadRunning = FALSE;
        Entry->TcpState = RosvTcpClosed;
        Entry->GuestSendPending = TRUE;
        Entry->GuestSendAdvanceRecv = FALSE;
        Entry->GuestSendRepostRecv = FALSE;
        Entry->GuestSendClearEntry = TRUE;
        Entry->GuestSendSeq = RstSeq;
        Entry->GuestSendAck = 0;
        Entry->GuestSendLength = 0;
        Entry->GuestSendFlags = TCP_FLAG_RST;
        SendRst = TRUE;
        ExReleaseFastMutex(&Backend->Lock);
    }
    else if (BytesReceived == 0)
    {
        Socket = Entry->Socket;
        FinSeq = Entry->GuestAckBase + 1 + Entry->BytesRecvFromHost;
        FinAck = Entry->GuestSeqNext;
        Entry->Socket = NULL;
        Entry->ThreadRunning = FALSE;
        Entry->TcpState = RosvTcpCloseWait;
        Entry->TimeoutMs = ROSV_NET_TCP_CLOSE_TIMEOUT_MS;
        Entry->GuestSendPending = TRUE;
        Entry->GuestSendAdvanceRecv = FALSE;
        Entry->GuestSendRepostRecv = FALSE;
        Entry->GuestSendClearEntry = FALSE;
        Entry->GuestSendSeq = FinSeq;
        Entry->GuestSendAck = FinAck;
        Entry->GuestSendLength = 0;
        Entry->GuestSendFlags = TCP_FLAG_FIN | TCP_FLAG_ACK;
        SendFin = TRUE;
        ExReleaseFastMutex(&Backend->Lock);
    }
    else
    {
        SeqNum = Entry->GuestAckBase + 1 + Entry->BytesRecvFromHost;
        AckNum = Entry->GuestSeqNext;
        Entry->GuestSendPending = TRUE;
        Entry->GuestSendAdvanceRecv = TRUE;
        Entry->GuestSendRepostRecv = TRUE;
        Entry->GuestSendClearEntry = FALSE;
        Entry->GuestSendSeq = SeqNum;
        Entry->GuestSendAck = AckNum;
        Entry->GuestSendLength = (ULONG)BytesReceived;
        Entry->GuestSendFlags = TCP_FLAG_ACK | TCP_FLAG_PSH;
        SendData = TRUE;
        Socket = Entry->Socket;
        ExReleaseFastMutex(&Backend->Lock);
    }

    if (ShouldLog)
    {
        DbgPrint("[ROSV:NET] TCP host %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u state=%s status=0x%08X bytes=%Iu send=%s%s%s repost=%u seq=%lu ack=%lu\n",
                 DestIp[0], DestIp[1], DestIp[2], DestIp[3], DestPort,
                 GuestIp[0], GuestIp[1], GuestIp[2], GuestIp[3], GuestPort,
                 StateName,
                 Status,
                 BytesReceived,
                 SendData ? "data" : "",
                 SendFin ? "fin" : "",
                 SendRst ? "rst" : "",
                 SendData ? 1 : 0,
                 SendData ? SeqNum : (SendFin ? FinSeq : RstSeq),
                 SendData ? AckNum : (SendFin ? FinAck : 0));
    }

    if (Socket != NULL && (SendFin || SendRst))
        RosvNetSocketClose(Socket);

    (void)RosvNetFlushPendingTcpSend(Backend, Entry);
}

static VOID
RosvNetProcessUdpRecvCompletion(
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    PROSV_NET_BACKEND_STATE Backend;
    UCHAR GuestIp[4];
    UCHAR DestIp[4];
    USHORT GuestPort;
    USHORT DestPort;
    PWSK_SOCKET Socket;
    NTSTATUS Status;
    SIZE_T BytesReceived;
    BOOLEAN Repost;
    BOOLEAN ShouldLog;

    Backend = Entry->Backend;
    Socket = NULL;
    GuestPort = 0;
    DestPort = 0;
    Status = Entry->RecvStatus;
    BytesReceived = Entry->RecvBytes;
    Repost = FALSE;
    ShouldLog = (InterlockedIncrement(&Backend->TraceUdpCount) <= ROSV_NET_TRACE_UDP_MAX) ||
                !NT_SUCCESS(Status);

    ExAcquireFastMutex(&Backend->Lock);
    if (!Entry->InUse || Entry->Closing || !Backend->Running)
    {
        ExReleaseFastMutex(&Backend->Lock);
        return;
    }

    if (!NT_SUCCESS(Status))
    {
        Socket = Entry->Socket;
        Entry->Socket = NULL;
        Entry->ThreadRunning = FALSE;
        RosvNetNatClearEntry(Entry);
        ExReleaseFastMutex(&Backend->Lock);

        if (Socket != NULL)
            RosvNetSocketClose(Socket);
        return;
    }

    RtlCopyMemory(GuestIp, Entry->GuestIp, 4);
    RtlCopyMemory(DestIp, Entry->DestIp, 4);
    GuestPort = Entry->GuestPort;
    DestPort = Entry->DestPort;
    Entry->LastActivityMs = RosvNetNowMs();
    Repost = (BOOLEAN)(Entry->Socket != NULL && !Entry->Closing);
    ExReleaseFastMutex(&Backend->Lock);

    if (ShouldLog)
    {
        DbgPrint("[ROSV:NET] UDP host %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u status=0x%08X bytes=%Iu repost=%u\n",
                 DestIp[0], DestIp[1], DestIp[2], DestIp[3], DestPort,
                 GuestIp[0], GuestIp[1], GuestIp[2], GuestIp[3], GuestPort,
                 Status,
                 BytesReceived,
                 Repost ? 1 : 0);
    }

    if (BytesReceived > 0)
    {
        (void)RosvNetSendUdpToGuest(Backend,
                                    DestIp,
                                    GuestIp,
                                    DestPort,
                                    GuestPort,
                                    Entry->RecvBuffer,
                                    (ULONG)BytesReceived);
    }

    if (Repost)
    {
        ExAcquireFastMutex(&Backend->Lock);
        if (Entry->InUse &&
            !Entry->Closing &&
            Entry->Socket != NULL &&
            Backend->Running)
        {
            (void)RosvNetPostUdpReceive(Entry);
        }
        ExReleaseFastMutex(&Backend->Lock);
    }
}

static VOID
RosvNetDrainRecvCompletions(
    _Inout_ PROSV_NET_BACKEND_STATE Backend)
{
    PROSV_NAT_ENTRY Entries[ROSV_NET_NAT_TABLE_SIZE];
    ULONG Count;
    ULONG i;
    KIRQL OldIrql;

    Count = 0;
    KeAcquireSpinLock(&Backend->CompletionLock, &OldIrql);
    while (Backend->CompletionCount > 0 &&
           Count < RTL_NUMBER_OF(Entries))
    {
        Entries[Count] = Backend->CompletionQueue[Backend->CompletionTail];
        Backend->CompletionTail =
            (Backend->CompletionTail + 1) % ROSV_NET_NAT_TABLE_SIZE;
        Backend->CompletionCount--;
        if (Entries[Count] != NULL)
            Entries[Count]->RecvQueued = FALSE;
        Count++;
    }
    if (Backend->CompletionCount == 0)
        KeClearEvent(&Backend->CompletionEvent);
    KeReleaseSpinLock(&Backend->CompletionLock, OldIrql);

    for (i = 0; i < Count; i++)
    {
        if (Entries[i] == NULL)
            continue;

        if (Entries[i]->Protocol == RosvNatProtoTcp)
            RosvNetProcessTcpRecvCompletion(Entries[i]);
        else if (Entries[i]->Protocol == RosvNatProtoUdp)
            RosvNetProcessUdpRecvCompletion(Entries[i]);
        else if (Entries[i]->Protocol == RosvNatProtoIcmp)
            RosvNetProcessIcmpRecvCompletion(Entries[i]);
    }
}

static VOID
RosvNetPostPendingTcpReceives(
    _Inout_ PROSV_NET_BACKEND_STATE Backend)
{
    ULONG i;

    ExAcquireFastMutex(&Backend->Lock);
    for (i = 0; i < ROSV_NET_NAT_TABLE_SIZE; i++)
    {
        PROSV_NAT_ENTRY Entry;
        NTSTATUS Status;

        Entry = &Backend->NatTable[i];
        if (!Entry->InUse ||
            Entry->Protocol != RosvNatProtoTcp ||
            Entry->Closing ||
            Entry->ThreadRunning ||
            Entry->Socket == NULL ||
            Entry->RecvPosted ||
            Entry->RecvQueued ||
            Entry->GuestSendPending)
        {
            continue;
        }

        if (Entry->TcpState != RosvTcpSynAckSent &&
            Entry->TcpState != RosvTcpEstablished &&
            Entry->TcpState != RosvTcpFinWait)
        {
            continue;
        }

        /* Async TDI receive IRPs are thread-owned. Post them from the long-lived
         * pump thread so they do not get cancelled when the connect worker exits.
         */
        Status = RosvNetPostTcpReceive(Entry);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("[ROSV:NET] deferred TCP receive post failed %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u status=0x%08X\n",
                     Entry->GuestIp[0], Entry->GuestIp[1], Entry->GuestIp[2], Entry->GuestIp[3], Entry->GuestPort,
                     Entry->DestIp[0], Entry->DestIp[1], Entry->DestIp[2], Entry->DestIp[3], Entry->DestPort,
                     Status);
        }
    }
    ExReleaseFastMutex(&Backend->Lock);
}

static BOOLEAN
RosvNetFlushPendingTcpSend(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    UCHAR GuestIp[4];
    UCHAR DestIp[4];
    UCHAR Payload[ROSV_NET_TCP_MSS];
    USHORT GuestPort;
    USHORT DestPort;
    ULONG Generation;
    ULONG SeqNum;
    ULONG AckNum;
    ULONG PayloadLen;
    UCHAR Flags;
    BOOLEAN AdvanceRecv;
    BOOLEAN RepostRecv;
    BOOLEAN ClearEntry;
    BOOLEAN Sent;

    Sent = FALSE;
    Generation = 0;
    SeqNum = 0;
    AckNum = 0;
    PayloadLen = 0;
    Flags = 0;
    AdvanceRecv = FALSE;
    RepostRecv = FALSE;
    ClearEntry = FALSE;
    GuestPort = 0;
    DestPort = 0;
    RtlZeroMemory(GuestIp, sizeof(GuestIp));
    RtlZeroMemory(DestIp, sizeof(DestIp));

    ExAcquireFastMutex(&Backend->Lock);
    if (!Backend->Running ||
        !Entry->InUse ||
        !Entry->GuestSendPending)
    {
        ExReleaseFastMutex(&Backend->Lock);
        return FALSE;
    }

    Generation = Entry->Generation;
    RtlCopyMemory(GuestIp, Entry->GuestIp, sizeof(GuestIp));
    RtlCopyMemory(DestIp, Entry->DestIp, sizeof(DestIp));
    GuestPort = Entry->GuestPort;
    DestPort = Entry->DestPort;
    SeqNum = Entry->GuestSendSeq;
    AckNum = Entry->GuestSendAck;
    PayloadLen = Entry->GuestSendLength;
    Flags = Entry->GuestSendFlags;
    AdvanceRecv = Entry->GuestSendAdvanceRecv;
    RepostRecv = Entry->GuestSendRepostRecv;
    ClearEntry = Entry->GuestSendClearEntry;
    if (PayloadLen > sizeof(Payload))
        PayloadLen = sizeof(Payload);
    if (PayloadLen > 0)
        RtlCopyMemory(Payload, Entry->RecvBuffer, PayloadLen);
    ExReleaseFastMutex(&Backend->Lock);

    Sent = RosvNetSendTcpToGuest(Backend,
                                 DestIp,
                                 GuestIp,
                                 DestPort,
                                 GuestPort,
                                 SeqNum,
                                 AckNum,
                                 Flags,
                                 PayloadLen > 0 ? Payload : NULL,
                                 PayloadLen,
                                 FALSE);
    if (!Sent)
        return FALSE;

    ExAcquireFastMutex(&Backend->Lock);
    if (!Backend->Running ||
        !Entry->InUse ||
        Entry->Generation != Generation ||
        !Entry->GuestSendPending)
    {
        ExReleaseFastMutex(&Backend->Lock);
        return TRUE;
    }

    if (AdvanceRecv)
        Entry->BytesRecvFromHost += PayloadLen;

    Entry->GuestSendPending = FALSE;
    Entry->GuestSendAdvanceRecv = FALSE;
    Entry->GuestSendRepostRecv = FALSE;
    Entry->GuestSendClearEntry = FALSE;
    Entry->GuestSendSeq = 0;
    Entry->GuestSendAck = 0;
    Entry->GuestSendLength = 0;
    Entry->GuestSendFlags = 0;

    if (ClearEntry)
    {
        RosvNetNatClearEntry(Entry);
        ExReleaseFastMutex(&Backend->Lock);
        return TRUE;
    }

    if (RepostRecv &&
        Entry->Socket != NULL &&
        !Entry->Closing &&
        !Entry->RecvPosted &&
        Backend->Running)
    {
        (void)RosvNetPostTcpReceive(Entry);
    }
    ExReleaseFastMutex(&Backend->Lock);
    return TRUE;
}

static VOID
RosvNetFlushPendingTcpSends(
    _Inout_ PROSV_NET_BACKEND_STATE Backend)
{
    ULONG i;

    for (i = 0; i < ROSV_NET_NAT_TABLE_SIZE; i++)
    {
        PROSV_NAT_ENTRY Entry = &Backend->NatTable[i];

        if (!Entry->InUse ||
            Entry->Protocol != RosvNatProtoTcp ||
            !Entry->GuestSendPending)
        {
            continue;
        }

        (void)RosvNetFlushPendingTcpSend(Backend, Entry);
    }
}

static BOOLEAN
RosvNetBuildAndSendArpReply(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
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
    if (RosvNetReadBe16(&Arp[0]) != 1 ||
        RosvNetReadBe16(&Arp[2]) != ETH_TYPE_IPV4 ||
        Arp[4] != 6 ||
        Arp[5] != 4 ||
        RosvNetReadBe16(&Arp[6]) != 1)
    {
        return FALSE;
    }

    SenderMac = &Arp[8];
    SenderIp = &Arp[14];
    TargetIp = &Arp[24];

    if (RtlCompareMemory(TargetIp, Backend->HostIp, 4) != 4 &&
        RtlCompareMemory(TargetIp, Backend->DnsIp, 4) != 4)
    {
        return FALSE;
    }

    RtlZeroMemory(Reply, sizeof(Reply));
    RtlCopyMemory(&Reply[0], SenderMac, 6);
    RtlCopyMemory(&Reply[6], Backend->HostMac, 6);
    RosvNetWriteBe16(&Reply[12], ETH_TYPE_ARP);
    RosvNetWriteBe16(&Reply[14], 1);
    RosvNetWriteBe16(&Reply[16], ETH_TYPE_IPV4);
    Reply[18] = 6;
    Reply[19] = 4;
    RosvNetWriteBe16(&Reply[20], 2);
    RtlCopyMemory(&Reply[22], Backend->HostMac, 6);
    RtlCopyMemory(&Reply[28], TargetIp, 4);
    RtlCopyMemory(&Reply[32], SenderMac, 6);
    RtlCopyMemory(&Reply[38], SenderIp, 4);

    return RosvNetSendPacketToGuest(Backend, Reply, sizeof(Reply));
}

static ULONG
RosvNetDhcpAppendOption(
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
        RtlCopyMemory(&Options[Offset], Data, DataLength);

    return Offset + DataLength;
}

static UCHAR
RosvNetDhcpParseMessageType(
    _In_reads_bytes_(OptionsLength) const UCHAR *Options,
    _In_ ULONG OptionsLength,
    _Out_opt_ BOOLEAN *HasRequestedIp,
    _Out_writes_bytes_opt_(4) UCHAR *RequestedIp,
    _Out_opt_ BOOLEAN *HasServerId,
    _Out_writes_bytes_opt_(4) UCHAR *ServerId)
{
    ULONG Offset;
    UCHAR MsgType;

    Offset = 0;
    MsgType = 0;
    if (HasRequestedIp != NULL)
        *HasRequestedIp = FALSE;
    if (HasServerId != NULL)
        *HasServerId = FALSE;

    while (Offset < OptionsLength)
    {
        UCHAR Code;
        UCHAR Length;

        Code = Options[Offset++];
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
        else if (Code == 50 && Length >= 4 && HasRequestedIp != NULL && RequestedIp != NULL)
        {
            RtlCopyMemory(RequestedIp, &Options[Offset], 4);
            *HasRequestedIp = TRUE;
        }
        else if (Code == 54 && Length >= 4 && HasServerId != NULL && ServerId != NULL)
        {
            RtlCopyMemory(ServerId, &Options[Offset], 4);
            *HasServerId = TRUE;
        }

        Offset += Length;
    }

    return MsgType;
}

static BOOLEAN
RosvNetBuildAndSendDhcpReply(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
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
    BOOLEAN HasRequestedIp;
    BOOLEAN HasServerId;
    UCHAR RequestedIp[4];
    UCHAR ServerId[4];
    UCHAR LeaseSeconds[4];
    UCHAR RenewalSeconds[4];
    UCHAR RebindSeconds[4];
    UCHAR BroadcastIp[4];
    UCHAR MtuOption[2];
    ULONG OptionsCapacity;

    if (FrameLength < 14 + 20 + 8 + 240)
        return FALSE;
    if (RosvNetReadBe16(&Frame[12]) != ETH_TYPE_IPV4)
        return FALSE;

    Ip = Frame + 14;
    EthPayloadLength = FrameLength - 14;
    IpHeaderLength = (ULONG)(Ip[0] & 0x0F) * 4;
    if ((Ip[0] >> 4) != 4 || IpHeaderLength < 20 || IpHeaderLength > EthPayloadLength)
        return FALSE;
    if (Ip[9] != IP_PROTO_UDP)
        return FALSE;
    if ((RosvNetReadBe16(&Ip[6]) & 0x3FFF) != 0)
        return FALSE;

    IpTotalLength = RosvNetReadBe16(&Ip[2]);
    if (IpTotalLength < IpHeaderLength + 8 || IpTotalLength > EthPayloadLength)
        return FALSE;

    Udp = Ip + IpHeaderLength;
    UdpLength = RosvNetReadBe16(&Udp[4]);
    if (UdpLength < 8 || UdpLength > (IpTotalLength - IpHeaderLength))
        return FALSE;
    if (RosvNetReadBe16(&Udp[0]) != DHCP_CLIENT_PORT ||
        RosvNetReadBe16(&Udp[2]) != DHCP_SERVER_PORT)
        return FALSE;

    UdpPayloadLength = UdpLength - 8;
    if (UdpPayloadLength < 240)
        return FALSE;

    Bootp = Udp + 8;
    if (Bootp[0] != 1 || Bootp[1] != 1 || Bootp[2] != 6)
        return FALSE;
    if (RosvNetReadBe32(&Bootp[236]) != DHCP_MAGIC_COOKIE)
        return FALSE;

    DhcpOptions = &Bootp[240];
    MessageType = RosvNetDhcpParseMessageType(DhcpOptions,
                                              UdpPayloadLength - 240,
                                              &HasRequestedIp,
                                              RequestedIp,
                                              &HasServerId,
                                              ServerId);
    if (MessageType == 1)
    {
        ReplyMessageType = 2;
    }
    else if (MessageType == 3)
    {
        ReplyMessageType = 5;
        if (HasServerId && RtlCompareMemory(ServerId, Backend->HostIp, 4) != 4)
            return FALSE;
        if (HasRequestedIp && RtlCompareMemory(RequestedIp, Backend->GuestIp, 4) != 4)
            return FALSE;
    }
    else
    {
        return FALSE;
    }

    RtlZeroMemory(Reply, sizeof(Reply));
    RtlFillMemory(&Reply[0], 6, 0xFF);
    RtlCopyMemory(&Reply[6], Backend->HostMac, 6);
    RosvNetWriteBe16(&Reply[12], ETH_TYPE_IPV4);

    OutIp = &Reply[14];
    OutIp[0] = 0x45;
    OutIp[1] = 0x00;
    OutIp[4] = Ip[4];
    OutIp[5] = Ip[5];
    OutIp[6] = 0x00;
    OutIp[7] = 0x00;
    OutIp[8] = 64;
    OutIp[9] = IP_PROTO_UDP;
    RtlCopyMemory(&OutIp[12], Backend->HostIp, 4);
    RtlFillMemory(&OutIp[16], 4, 0xFF);

    OutUdp = OutIp + 20;
    RosvNetWriteBe16(&OutUdp[0], DHCP_SERVER_PORT);
    RosvNetWriteBe16(&OutUdp[2], DHCP_CLIENT_PORT);

    OutBootp = OutUdp + 8;
    OutBootp[0] = 2;
    OutBootp[1] = 1;
    OutBootp[2] = 6;
    OutBootp[3] = 0;
    RtlCopyMemory(&OutBootp[4], &Bootp[4], 4);
    RtlCopyMemory(&OutBootp[8], &Bootp[8], 2);
    RtlCopyMemory(&OutBootp[10], &Bootp[10], 2);
    RtlCopyMemory(&OutBootp[12], &Bootp[12], 4);
    RtlCopyMemory(&OutBootp[16], Backend->GuestIp, 4);
    RtlCopyMemory(&OutBootp[20], Backend->HostIp, 4);
    RtlCopyMemory(&OutBootp[28], &Bootp[28], 16);
    RosvNetWriteBe32(&OutBootp[236], DHCP_MAGIC_COOKIE);

    OptionsCapacity = sizeof(Reply) - (14 + 20 + 8 + 240);
    OutOptions = &OutBootp[240];
    OutOptionsLength = 0;
    OutOptionsLength = RosvNetDhcpAppendOption(OutOptions, OptionsCapacity, OutOptionsLength, 53, &ReplyMessageType, 1);
    OutOptionsLength = RosvNetDhcpAppendOption(OutOptions, OptionsCapacity, OutOptionsLength, 54, Backend->HostIp, 4);
    OutOptionsLength = RosvNetDhcpAppendOption(OutOptions, OptionsCapacity, OutOptionsLength, 1, Backend->GuestMask, 4);
    OutOptionsLength = RosvNetDhcpAppendOption(OutOptions, OptionsCapacity, OutOptionsLength, 3, Backend->HostIp, 4);
    OutOptionsLength = RosvNetDhcpAppendOption(OutOptions, OptionsCapacity, OutOptionsLength, 6, Backend->DnsIp, 4);

    RosvNetWriteBe32(LeaseSeconds, 3600);
    RosvNetWriteBe32(RenewalSeconds, 1800);
    RosvNetWriteBe32(RebindSeconds, 3150);
    OutOptionsLength = RosvNetDhcpAppendOption(OutOptions, OptionsCapacity, OutOptionsLength, 51, LeaseSeconds, 4);
    OutOptionsLength = RosvNetDhcpAppendOption(OutOptions, OptionsCapacity, OutOptionsLength, 58, RenewalSeconds, 4);
    OutOptionsLength = RosvNetDhcpAppendOption(OutOptions, OptionsCapacity, OutOptionsLength, 59, RebindSeconds, 4);

    BroadcastIp[0] = Backend->GuestIp[0] | (UCHAR)(~Backend->GuestMask[0]);
    BroadcastIp[1] = Backend->GuestIp[1] | (UCHAR)(~Backend->GuestMask[1]);
    BroadcastIp[2] = Backend->GuestIp[2] | (UCHAR)(~Backend->GuestMask[2]);
    BroadcastIp[3] = Backend->GuestIp[3] | (UCHAR)(~Backend->GuestMask[3]);
    OutOptionsLength = RosvNetDhcpAppendOption(OutOptions, OptionsCapacity, OutOptionsLength, 28, BroadcastIp, 4);

    MtuOption[0] = 0x05;
    MtuOption[1] = 0xDC;
    OutOptionsLength = RosvNetDhcpAppendOption(OutOptions, OptionsCapacity, OutOptionsLength, 26, MtuOption, 2);

    if (OutOptionsLength < OptionsCapacity)
        OutOptions[OutOptionsLength++] = 0xFF;
    if (OutOptionsLength < 60)
        OutOptionsLength = 60;

    OutBootpLength = 240 + OutOptionsLength;
    OutIpLength = 20 + 8 + OutBootpLength;
    OutFrameLength = 14 + OutIpLength;

    RosvNetWriteBe16(&OutIp[2], (USHORT)OutIpLength);
    OutIp[10] = 0;
    OutIp[11] = 0;
    RosvNetWriteBe16(&OutIp[10], RosvNetInternetChecksum(OutIp, 20));

    RosvNetWriteBe16(&OutUdp[4], (USHORT)(8 + OutBootpLength));
    OutUdp[6] = 0;
    OutUdp[7] = 0;
    RosvNetWriteBe16(&OutUdp[6], RosvNetUdpChecksum(&OutIp[12], &OutIp[16], OutUdp, 8 + OutBootpLength));

    if (OutFrameLength < 60)
    {
        RtlZeroMemory(&Reply[OutFrameLength], 60 - OutFrameLength);
        OutFrameLength = 60;
    }

    return RosvNetSendPacketToGuest(Backend, Reply, OutFrameLength);
}

static VOID
RosvNetResolveRealDestination(
    _In_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_(4) const UCHAR *GuestDestIp,
    _Out_writes_(4) UCHAR *RealDestIp)
{
    RtlCopyMemory(RealDestIp, GuestDestIp, 4);

    if (RtlCompareMemory(GuestDestIp, Backend->DnsIp, 4) == 4)
    {
        RtlCopyMemory(RealDestIp, Backend->HostDnsIp, 4);
    }
    else if (RtlCompareMemory(GuestDestIp, Backend->HostIp, 4) == 4)
    {
        RealDestIp[0] = 127;
        RealDestIp[1] = 0;
        RealDestIp[2] = 0;
        RealDestIp[3] = 1;
    }
}

static ULONG
RosvNetNatHash(
    _In_ ROSV_NAT_PROTOCOL Protocol,
    _In_reads_(4) const UCHAR *GuestIp,
    _In_ USHORT GuestPort,
    _In_reads_(4) const UCHAR *DestIp,
    _In_ USHORT DestPort)
{
    ULONG Hash;

    Hash = (ULONG)Protocol;
    Hash ^= *(const ULONG *)GuestIp;
    Hash ^= ((ULONG)GuestPort << 16) | (ULONG)DestPort;
    Hash ^= *(const ULONG *)DestIp;
    Hash = (Hash ^ (Hash >> 16)) * 0x45d9f3b;
    Hash = (Hash ^ (Hash >> 16));
    return Hash % ROSV_NET_NAT_TABLE_SIZE;
}

static PROSV_NAT_ENTRY
RosvNetNatLookup(
    _In_ PROSV_NET_BACKEND_STATE Backend,
    _In_ ROSV_NAT_PROTOCOL Protocol,
    _In_reads_(4) const UCHAR *GuestIp,
    _In_ USHORT GuestPort,
    _In_reads_(4) const UCHAR *DestIp,
    _In_ USHORT DestPort)
{
    ULONG Start;
    ULONG i;
    ULONG Idx;

    Start = RosvNetNatHash(Protocol, GuestIp, GuestPort, DestIp, DestPort);
    for (i = 0; i < ROSV_NET_NAT_TABLE_SIZE; i++)
    {
        PROSV_NAT_ENTRY Entry;

        Idx = (Start + i) % ROSV_NET_NAT_TABLE_SIZE;
        Entry = &Backend->NatTable[Idx];

        /* Empty non-tombstone slot = end of probe chain */
        if (!Entry->InUse && !Entry->Deleted)
            return NULL;

        /* Skip tombstones */
        if (!Entry->InUse)
            continue;

        if (Entry->Protocol == Protocol &&
            Entry->GuestPort == GuestPort &&
            Entry->DestPort == DestPort &&
            RtlCompareMemory(Entry->GuestIp, GuestIp, 4) == 4 &&
            RtlCompareMemory(Entry->DestIp, DestIp, 4) == 4)
        {
            return Entry;
        }
    }

    return NULL;
}

static PROSV_NAT_ENTRY
RosvNetNatAllocate(
    _In_ PROSV_NET_BACKEND_STATE Backend,
    _In_ ROSV_NAT_PROTOCOL Protocol,
    _In_reads_(4) const UCHAR *GuestIp,
    _In_ USHORT GuestPort,
    _In_reads_(4) const UCHAR *DestIp,
    _In_ USHORT DestPort)
{
    ULONG Start;
    ULONG i;
    ULONG Idx;

    Start = RosvNetNatHash(Protocol, GuestIp, GuestPort, DestIp, DestPort);
    for (i = 0; i < ROSV_NET_NAT_TABLE_SIZE; i++)
    {
        PROSV_NAT_ENTRY Entry;
        PROSV_NET_BACKEND_STATE SavedBackend;
        PMDL SavedRecvMdl;
        PIRP SavedRecvIrp;
        WSK_BUF SavedRecvWskBuf;

        Idx = (Start + i) % ROSV_NET_NAT_TABLE_SIZE;
        Entry = &Backend->NatTable[Idx];
        if (!Entry->InUse)
        {
            /* Reuse tombstone or empty slot */
            SavedBackend = Entry->Backend;
            SavedRecvMdl = Entry->RecvMdl;
            SavedRecvIrp = Entry->RecvIrp;
            SavedRecvWskBuf = Entry->RecvWskBuf;
            RtlZeroMemory(Entry, sizeof(*Entry));
            Entry->Backend = SavedBackend;
            Entry->RecvMdl = SavedRecvMdl;
            Entry->RecvIrp = SavedRecvIrp;
            Entry->RecvWskBuf = SavedRecvWskBuf;
            Entry->InUse = TRUE;
            Entry->Deleted = FALSE;
            Entry->LastActivityMs = RosvNetNowMs();
            Entry->Generation = (ULONG)InterlockedIncrement(&Backend->NatGenerationCounter);
            return Entry;
        }
    }

    return NULL;
}

static VOID
RosvNetNatReleaseClosed(
    _Inout_ PROSV_NAT_ENTRY Entry)
{
    if (!Entry->ThreadRunning &&
        Entry->Socket == NULL &&
        (Entry->TcpState == RosvTcpClosed ||
         Entry->Protocol == RosvNatProtoUdp ||
         Entry->Protocol == RosvNatProtoIcmp))
    {
        RosvNetNatClearEntry(Entry);
    }
}

static VOID
RosvNetIoThreadLeave(
    _Inout_ PROSV_NET_BACKEND_STATE Backend)
{
    if (InterlockedDecrement(&Backend->ActiveIoThreads) == 0)
        KeSetEvent(&Backend->IoThreadsDoneEvent, IO_NO_INCREMENT, FALSE);
}

static NTSTATUS
RosvNetSpawnThread(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_ PKSTART_ROUTINE StartRoutine,
    _In_ PVOID Context)
{
    HANDLE ThreadHandle;
    NTSTATUS Status;

    if (InterlockedIncrement(&Backend->ActiveIoThreads) == 1)
        KeClearEvent(&Backend->IoThreadsDoneEvent);

    ThreadHandle = NULL;
    Status = PsCreateSystemThread(&ThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  StartRoutine,
                                  Context);
    if (!NT_SUCCESS(Status))
    {
        RosvNetIoThreadLeave(Backend);
        return Status;
    }

    ZwClose(ThreadHandle);
    return STATUS_SUCCESS;
}

/*
 * Compact NAT table by clearing all tombstones and reinserting active entries.
 * Must be called under Backend->Lock.
 */
static VOID
RosvNetNatCompact(
    _Inout_ PROSV_NET_BACKEND_STATE Backend)
{
    ULONG i;
    ULONG ActiveCount;
    BOOLEAN HasTombstones;

    /*
     * We need to collect active entries, clear the table, and reinsert.
     * Since entries contain embedded buffers (RecvBuffer[]) and MDLs pointing
     * to them, we cannot simply move entries. Instead, clear tombstone flags
     * only when safe: a tombstone is safe to clear when there are no active
     * entries after it (in probe chain terms) that would become unreachable.
     *
     * Simple approach: count active entries and tombstones. If no tombstones,
     * nothing to do. If all entries are tombstones (no active), clear all.
     */
    ActiveCount = 0;
    HasTombstones = FALSE;
    for (i = 0; i < ROSV_NET_NAT_TABLE_SIZE; i++)
    {
        if (Backend->NatTable[i].InUse)
            ActiveCount++;
        else if (Backend->NatTable[i].Deleted)
            HasTombstones = TRUE;
    }

    if (!HasTombstones)
        return;

    if (ActiveCount == 0)
    {
        /* No active entries: safe to clear all tombstones */
        for (i = 0; i < ROSV_NET_NAT_TABLE_SIZE; i++)
            Backend->NatTable[i].Deleted = FALSE;
    }
}

static VOID
RosvNetCleanupExpired(
    _Inout_ PROSV_NET_BACKEND_STATE Backend)
{
    ULONG i;
    ULONGLONG NowMs;
    PWSK_SOCKET Sockets[ROSV_NET_NAT_TABLE_SIZE];
    ULONG SocketCount;

    SocketCount = 0;
    RtlZeroMemory(Sockets, sizeof(Sockets));

    ExAcquireFastMutex(&Backend->Lock);
    NowMs = RosvNetNowMs();
    for (i = 0; i < ROSV_NET_NAT_TABLE_SIZE; i++)
    {
        PROSV_NAT_ENTRY Entry;

        Entry = &Backend->NatTable[i];
        if (!Entry->InUse)
            continue;

        if (Entry->ThreadRunning)
            continue;

        if ((NowMs - Entry->LastActivityMs) > Entry->TimeoutMs)
        {
            if (Entry->Socket != NULL &&
                SocketCount < RTL_NUMBER_OF(Sockets))
            {
                Sockets[SocketCount++] = Entry->Socket;
            }
            Entry->Socket = NULL;
            Entry->ThreadRunning = FALSE;
            Entry->TcpState = RosvTcpClosed;
            RosvNetNatClearEntry(Entry);
        }
        else
            RosvNetNatReleaseClosed(Entry);
    }
    RosvNetNatCompact(Backend);
    ExReleaseFastMutex(&Backend->Lock);

    for (i = 0; i < SocketCount; i++)
    {
        if (Sockets[i] != NULL)
            RosvNetSocketClose(Sockets[i]);
    }
}

static VOID
RosvNetHandleGuestTcp(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _In_reads_(4) const UCHAR *SrcIp,
    _In_reads_(4) const UCHAR *DstIp,
    _In_ ULONG IpHeaderLen);

static VOID
RosvNetHandleGuestUdp(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _In_reads_(4) const UCHAR *SrcIp,
    _In_reads_(4) const UCHAR *DstIp,
    _In_ ULONG IpHeaderLen);

static VOID
RosvNetHandleGuestIcmp(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _In_reads_(4) const UCHAR *SrcIp,
    _In_reads_(4) const UCHAR *DstIp,
    _In_ ULONG IpHeaderLen);

static VOID
RosvNetHandleGuestPacket(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_bytes_(Packet->Length) const PROSV_NET_PACKET Packet)
{
    USHORT EthType;
    ULONG Ihl;
    UCHAR Proto;
    const UCHAR *SrcIp;
    const UCHAR *DstIp;

    if (Packet->Length < 14)
        return;

    if (!RosvNetIpIsZero(&Packet->Data[6]))
        RtlCopyMemory(Backend->GuestMac, &Packet->Data[6], 6);

    EthType = RosvNetReadBe16(&Packet->Data[12]);
    switch (EthType)
    {
        case ETH_TYPE_ARP:
            (void)RosvNetBuildAndSendArpReply(Backend, Packet->Data, Packet->Length);
            return;

        case ETH_TYPE_IPV4:
            break;

        default:
            return;
    }

    if (Packet->Length < 14 + 20)
        return;

    Ihl = (ULONG)(Packet->Data[14] & 0x0F) * 4;
    if ((Packet->Data[14] >> 4) != 4 || Ihl < 20 || Packet->Length < 14 + Ihl)
        return;

    Proto = Packet->Data[23];
    SrcIp = &Packet->Data[26];
    DstIp = &Packet->Data[30];

    if (Proto == IP_PROTO_UDP)
    {
        if (RosvNetBuildAndSendDhcpReply(Backend, Packet->Data, Packet->Length))
            return;
        RosvNetHandleGuestUdp(Backend, Packet->Data, Packet->Length, SrcIp, DstIp, Ihl);
        return;
    }

    if (Proto == IP_PROTO_ICMP)
    {
        RosvNetHandleGuestIcmp(Backend, Packet->Data, Packet->Length, SrcIp, DstIp, Ihl);
        return;
    }

    if (Proto == IP_PROTO_TCP)
    {
        RosvNetHandleGuestTcp(Backend, Packet->Data, Packet->Length, SrcIp, DstIp, Ihl);
        return;
    }
}

static VOID NTAPI
RosvNetTcpThreadProc(
    _In_ PVOID Context)
{
    PROSV_NAT_ENTRY Entry;
    PROSV_NET_BACKEND_STATE Backend;
    SOCKADDR_IN RemoteAddr;
    PWSK_SOCKET Socket;
    NTSTATUS Status;
    BOOLEAN SendSynAck;
    BOOLEAN SendRst;
    ULONG RstSeq;
    ULONG RstAck;
    UCHAR GuestIp[4];
    UCHAR DestIp[4];
    USHORT GuestPort;
    USHORT DestPort;
    ULONG SeqNum;
    ULONG AckNum;
    BOOLEAN WakePump;

    Entry = (PROSV_NAT_ENTRY)Context;
    Backend = Entry->Backend;
    Socket = Entry->Socket;
    SendSynAck = FALSE;
    SendRst = FALSE;
    RstSeq = 0;
    RstAck = 0;
    GuestPort = 0;
    DestPort = 0;
    SeqNum = 0;
    AckNum = 0;
    WakePump = FALSE;

    RosvNetBuildAddress(&RemoteAddr, Entry->RealDestIp, Entry->RealDestPort);
    Status = RosvNetSocketConnect(Socket, (PSOCKADDR)&RemoteAddr);
    DbgPrint("[ROSV:NET] TCP connect result %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u status=0x%08X\n",
             Entry->GuestIp[0], Entry->GuestIp[1], Entry->GuestIp[2], Entry->GuestIp[3], Entry->GuestPort,
             Entry->DestIp[0], Entry->DestIp[1], Entry->DestIp[2], Entry->DestIp[3], Entry->DestPort,
             Status);

    ExAcquireFastMutex(&Backend->Lock);
    if (!Entry->InUse || Entry->Closing || !Backend->Running || Entry->Socket != Socket)
    {
        ExReleaseFastMutex(&Backend->Lock);
        RosvNetSocketClose(Socket);
        RosvNetIoThreadLeave(Backend);
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    if (!NT_SUCCESS(Status))
    {
        RtlCopyMemory(GuestIp, Entry->GuestIp, 4);
        RtlCopyMemory(DestIp, Entry->DestIp, 4);
        GuestPort = Entry->GuestPort;
        DestPort = Entry->DestPort;
        RstAck = Entry->GuestSeqBase + 1;
        RosvNetNatClearEntry(Entry);
        ExReleaseFastMutex(&Backend->Lock);

        RosvNetSocketClose(Socket);
        (void)RosvNetSendTcpToGuest(Backend,
                                    DestIp,
                                    GuestIp,
                                    DestPort,
                                    GuestPort,
                                    0,
                                    RstAck,
                                    TCP_FLAG_RST | TCP_FLAG_ACK,
                                    NULL,
                                    0,
                                    FALSE);
        RosvNetIoThreadLeave(Backend);
        PsTerminateSystemThread(STATUS_SUCCESS);
        return;
    }

    Entry->TcpState = RosvTcpSynAckSent;
    Entry->LastActivityMs = RosvNetNowMs();
    RtlCopyMemory(GuestIp, Entry->GuestIp, 4);
    RtlCopyMemory(DestIp, Entry->DestIp, 4);
    GuestPort = Entry->GuestPort;
    DestPort = Entry->DestPort;
    SeqNum = Entry->GuestAckBase;
    AckNum = Entry->GuestSeqBase + 1;
    SendSynAck = TRUE;
    ExReleaseFastMutex(&Backend->Lock);

    if (SendSynAck)
    {
        (void)RosvNetSendTcpToGuest(Backend,
                                    DestIp,
                                    GuestIp,
                                    DestPort,
                                    GuestPort,
                                    SeqNum,
                                    AckNum,
                                    TCP_FLAG_SYN | TCP_FLAG_ACK,
                                    NULL,
                                    0,
                                    TRUE);
    }

    ExAcquireFastMutex(&Backend->Lock);
    if (Entry->InUse && Entry->Socket == Socket)
    {
        Entry->ThreadRunning = FALSE;
        WakePump = TRUE;
    }
    ExReleaseFastMutex(&Backend->Lock);

    if (WakePump)
        KeSetEvent(&Backend->CompletionEvent, IO_NO_INCREMENT, FALSE);

    if (SendRst)
    {
        (void)RosvNetSendTcpToGuest(Backend,
                                    DestIp,
                                    GuestIp,
                                    DestPort,
                                    GuestPort,
                                    RstSeq,
                                    0,
                                    TCP_FLAG_RST,
                                    NULL,
                                    0,
                                    FALSE);
    }

    RosvNetIoThreadLeave(Backend);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID NTAPI
RosvNetUdpThreadProc(
    _In_ PVOID Context)
{
    UNREFERENCED_PARAMETER(Context);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

static VOID
RosvNetHandleGuestUdp(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _In_reads_(4) const UCHAR *SrcIp,
    _In_reads_(4) const UCHAR *DstIp,
    _In_ ULONG IpHeaderLen)
{
    const UCHAR *Udp;
    USHORT SrcPort;
    USHORT DstPort;
    ULONG IpTotalLen;
    ULONG UdpTotalLen;
    ULONG UdpPayloadLen;
    const UCHAR *Payload;
    PROSV_NAT_ENTRY Entry;
    UCHAR RealDstIp[4];
    SOCKADDR_IN BindAddr;
    SOCKADDR_IN Addr;
    PWSK_SOCKET Socket;
    NTSTATUS Status;
    SIZE_T BytesSent;
    BOOLEAN ShouldLog;

    IpTotalLen = RosvNetReadBe16(&Frame[16]);
    if (IpTotalLen < IpHeaderLen + 8)
        return;

    Udp = &Frame[14 + IpHeaderLen];
    SrcPort = RosvNetReadBe16(&Udp[0]);
    DstPort = RosvNetReadBe16(&Udp[2]);
    UdpTotalLen = RosvNetReadBe16(&Udp[4]);
    if (UdpTotalLen < 8 || 14 + IpHeaderLen + UdpTotalLen > FrameLength)
        return;

    UdpPayloadLen = UdpTotalLen - 8;
    Payload = Udp + 8;
    RosvNetResolveRealDestination(Backend, DstIp, RealDstIp);
    ShouldLog = (InterlockedIncrement(&Backend->TraceUdpCount) <= ROSV_NET_TRACE_UDP_MAX) ||
                DstPort == 53;

    ExAcquireFastMutex(&Backend->Lock);
    Entry = RosvNetNatLookup(Backend, RosvNatProtoUdp, SrcIp, SrcPort, DstIp, DstPort);
    if (Entry == NULL)
    {
        Entry = RosvNetNatAllocate(Backend, RosvNatProtoUdp, SrcIp, SrcPort, DstIp, DstPort);
        if (Entry != NULL)
        {
            Entry->Backend = Backend;
            Entry->Protocol = RosvNatProtoUdp;
            RtlCopyMemory(Entry->GuestIp, SrcIp, 4);
            Entry->GuestPort = SrcPort;
            RtlCopyMemory(Entry->DestIp, DstIp, 4);
            Entry->DestPort = DstPort;
            RtlCopyMemory(Entry->RealDestIp, RealDstIp, 4);
            Entry->RealDestPort = DstPort;
            Entry->TimeoutMs = ROSV_NET_UDP_TIMEOUT_MS;
            Entry->ThreadRunning = FALSE;
        }
    }
    if (Entry == NULL)
    {
        ExReleaseFastMutex(&Backend->Lock);
        return;
    }
    Socket = Entry->Socket;
    ExReleaseFastMutex(&Backend->Lock);

    if (Socket == NULL)
    {
        Status = RosvNetSocketCreate(Backend,
                                     SOCK_DGRAM,
                                     IPPROTO_UDP,
                                     WSK_FLAG_DATAGRAM_SOCKET,
                                     &Socket);
        if (!NT_SUCCESS(Status))
            goto UdpFail;

        RosvNetBuildWildcardAddress(&BindAddr);
        Status = RosvNetSocketBind(Socket, (PSOCKADDR)&BindAddr);
        if (!NT_SUCCESS(Status))
            goto UdpFail;

        ExAcquireFastMutex(&Backend->Lock);
        if (!Entry->InUse || Entry->Closing || Entry->Socket != NULL)
        {
            ExReleaseFastMutex(&Backend->Lock);
            RosvNetSocketClose(Socket);
            return;
        }
        Entry->Socket = Socket;
        Status = RosvNetPostUdpReceive(Entry);
        ExReleaseFastMutex(&Backend->Lock);
        if (!NT_SUCCESS(Status))
            goto UdpFail;
    }

    RosvNetBuildAddress(&Addr, RealDstIp, DstPort);
    BytesSent = 0;
    Status = RosvNetSocketSendTo(Socket, (PVOID)Payload, UdpPayloadLen, (PSOCKADDR)&Addr, &BytesSent);
    if (NT_SUCCESS(Status))
    {
        if (ShouldLog)
        {
            DbgPrint("[ROSV:NET] UDP guest %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u real=%u.%u.%u.%u bytes=%Iu\n",
                     SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                     DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort,
                     RealDstIp[0], RealDstIp[1], RealDstIp[2], RealDstIp[3],
                     BytesSent);
        }
        ExAcquireFastMutex(&Backend->Lock);
        if (Entry->InUse)
            Entry->LastActivityMs = RosvNetNowMs();
        ExReleaseFastMutex(&Backend->Lock);
    }
    else if (ShouldLog)
    {
        DbgPrint("[ROSV:NET] UDP guest send failed %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u real=%u.%u.%u.%u status=0x%08X\n",
                 SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                 DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort,
                 RealDstIp[0], RealDstIp[1], RealDstIp[2], RealDstIp[3],
                 Status);
    }
    return;

UdpFail:
    if (Socket != NULL)
        RosvNetSocketClose(Socket);

    ExAcquireFastMutex(&Backend->Lock);
    if (Entry->InUse)
        RosvNetNatClearEntry(Entry);
    ExReleaseFastMutex(&Backend->Lock);
}

static VOID
RosvNetHandleGuestIcmp(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _In_reads_(4) const UCHAR *SrcIp,
    _In_reads_(4) const UCHAR *DstIp,
    _In_ ULONG IpHeaderLen)
{
    const UCHAR *IcmpHdr;
    ULONG IpTotalLen;
    ULONG IcmpLen;
    USHORT IcmpId;
    USHORT Frag;
    PROSV_NAT_ENTRY Entry;
    UCHAR RealDstIp[4];
    SOCKADDR_IN BindAddr;
    SOCKADDR_IN Addr;
    PWSK_SOCKET Socket;
    NTSTATUS Status;

    IpTotalLen = RosvNetReadBe16(&Frame[16]);
    if (IpTotalLen < IpHeaderLen + 8)
        return;

    /* Only handle unfragmented ICMP echo requests (type 8, code 0) */
    Frag = RosvNetReadBe16(&Frame[20]);
    if ((Frag & 0x3FFF) != 0)
        return;

    IcmpHdr = &Frame[14 + IpHeaderLen];
    IcmpLen = IpTotalLen - IpHeaderLen;

    if (IcmpHdr[0] != 8 || IcmpHdr[1] != 0)
    {
        DbgPrint("[ROSV:NET] ICMP non-echo type=%u code=%u %u.%u.%u.%u -> %u.%u.%u.%u\n",
                 IcmpHdr[0], IcmpHdr[1],
                 SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3],
                 DstIp[0], DstIp[1], DstIp[2], DstIp[3]);
        return;
    }

    IcmpId = RosvNetReadBe16(&IcmpHdr[4]);
    RosvNetResolveRealDestination(Backend, DstIp, RealDstIp);

    DbgPrint("[ROSV:NET] ICMP echo req %u.%u.%u.%u -> %u.%u.%u.%u real=%u.%u.%u.%u id=%u seq=%u len=%u\n",
             SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3],
             DstIp[0], DstIp[1], DstIp[2], DstIp[3],
             RealDstIp[0], RealDstIp[1], RealDstIp[2], RealDstIp[3],
             IcmpId, RosvNetReadBe16(&IcmpHdr[6]), IcmpLen);

    /* NAT lookup using ICMP identifier as "port" */
    ExAcquireFastMutex(&Backend->Lock);
    Entry = RosvNetNatLookup(Backend, RosvNatProtoIcmp, SrcIp, IcmpId, DstIp, 0);
    if (Entry == NULL)
    {
        Entry = RosvNetNatAllocate(Backend, RosvNatProtoIcmp, SrcIp, IcmpId, DstIp, 0);
        if (Entry != NULL)
        {
            Entry->Backend = Backend;
            Entry->Protocol = RosvNatProtoIcmp;
            RtlCopyMemory(Entry->GuestIp, SrcIp, 4);
            Entry->GuestPort = IcmpId;
            RtlCopyMemory(Entry->DestIp, DstIp, 4);
            Entry->DestPort = 0;
            RtlCopyMemory(Entry->RealDestIp, RealDstIp, 4);
            Entry->RealDestPort = 0;
            Entry->TimeoutMs = ROSV_NET_ICMP_TIMEOUT_MS;
            Entry->ThreadRunning = FALSE;
        }
    }
    if (Entry == NULL)
    {
        ExReleaseFastMutex(&Backend->Lock);
        DbgPrint("[ROSV:NET] ICMP NAT table full\n");
        return;
    }
    Socket = Entry->Socket;
    ExReleaseFastMutex(&Backend->Lock);

    if (Socket == NULL)
    {
        /* Create raw ICMP socket (routed to \Device\RawIp\1 via NETIO) */
        Status = RosvNetSocketCreate(Backend,
                                     SOCK_RAW,
                                     IPPROTO_ICMP,
                                     WSK_FLAG_DATAGRAM_SOCKET,
                                     &Socket);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("[ROSV:NET] ICMP socket create failed 0x%08X\n", Status);
            goto IcmpFail;
        }

        RosvNetBuildWildcardAddress(&BindAddr);
        Status = RosvNetSocketBind(Socket, (PSOCKADDR)&BindAddr);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("[ROSV:NET] ICMP socket bind failed 0x%08X\n", Status);
            goto IcmpFail;
        }

        ExAcquireFastMutex(&Backend->Lock);
        if (!Entry->InUse || Entry->Closing || Entry->Socket != NULL)
        {
            ExReleaseFastMutex(&Backend->Lock);
            RosvNetSocketClose(Socket);
            return;
        }
        Entry->Socket = Socket;
        Status = RosvNetPostUdpReceive(Entry);
        ExReleaseFastMutex(&Backend->Lock);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("[ROSV:NET] ICMP post recv failed 0x%08X\n", Status);
            goto IcmpFail;
        }
    }

    RosvNetBuildAddress(&Addr, RealDstIp, 0);
    Status = RosvNetSocketSendTo(Socket, (PVOID)IcmpHdr, IcmpLen, (PSOCKADDR)&Addr, NULL);
    if (NT_SUCCESS(Status))
    {
        ExAcquireFastMutex(&Backend->Lock);
        if (Entry->InUse)
            Entry->LastActivityMs = RosvNetNowMs();
        ExReleaseFastMutex(&Backend->Lock);
        return;
    }

    DbgPrint("[ROSV:NET] ICMP sendto failed 0x%08X\n", Status);
    return;

IcmpFail:
    if (Socket != NULL)
        RosvNetSocketClose(Socket);

    ExAcquireFastMutex(&Backend->Lock);
    if (Entry->InUse)
        RosvNetNatClearEntry(Entry);
    ExReleaseFastMutex(&Backend->Lock);
}

static VOID
RosvNetHandleGuestTcp(
    _Inout_ PROSV_NET_BACKEND_STATE Backend,
    _In_reads_bytes_(FrameLength) const UCHAR *Frame,
    _In_ ULONG FrameLength,
    _In_reads_(4) const UCHAR *SrcIp,
    _In_reads_(4) const UCHAR *DstIp,
    _In_ ULONG IpHeaderLen)
{
    const UCHAR *Tcp;
    USHORT SrcPort;
    USHORT DstPort;
    ULONG SeqNum;
    ULONG AckNum;
    UCHAR Flags;
    ULONG TcpHeaderLen;
    ULONG TcpPayloadLen;
    ULONG EthPayloadLen;
    ULONG IpTotalLen;
    const UCHAR *Payload;
    PROSV_NAT_ENTRY Entry;
    PROSV_NAT_ENTRY NewEntry;
    UCHAR RealDstIp[4];
    PWSK_SOCKET Socket;
    SOCKADDR_IN BindAddr;
    NTSTATUS Status;
    SIZE_T BytesSent;
    ULONG OurIsn;
    ULONG SendSeq;
    ULONG SendAck;
    ULONG DuplicateBytes;
    BOOLEAN SendSynAck;
    BOOLEAN SendAckOnly;
    BOOLEAN ShouldLogTcp;
    char FlagStr[8];
    const char *StateName;
    UCHAR GuestIpCopy[4];
    UCHAR DestIpCopy[4];
    USHORT GuestPortCopy;
    USHORT DestPortCopy;

    if (FrameLength < 14 + IpHeaderLen)
        return;

    EthPayloadLen = FrameLength - 14;
    IpTotalLen = RosvNetReadBe16(&Frame[16]);
    if (IpTotalLen < IpHeaderLen + 20 || IpTotalLen > EthPayloadLen)
        return;

    Tcp = &Frame[14 + IpHeaderLen];
    TcpHeaderLen = (ULONG)((Tcp[12] >> 4) & 0x0F) * 4;
    if (TcpHeaderLen < 20 || IpTotalLen < IpHeaderLen + TcpHeaderLen)
        return;

    SrcPort = RosvNetReadBe16(&Tcp[0]);
    DstPort = RosvNetReadBe16(&Tcp[2]);
    SeqNum = RosvNetReadBe32(&Tcp[4]);
    AckNum = RosvNetReadBe32(&Tcp[8]);
    Flags = Tcp[13];
    TcpPayloadLen = IpTotalLen - IpHeaderLen - TcpHeaderLen;
    Payload = Tcp + TcpHeaderLen;
    SendSynAck = FALSE;
    SendSeq = 0;
    SendAck = 0;
    DuplicateBytes = 0;
    Socket = NULL;
    BytesSent = 0;
    SendAckOnly = FALSE;
    ShouldLogTcp = FALSE;
    StateName = "none";
    RtlZeroMemory(FlagStr, sizeof(FlagStr));

    ExAcquireFastMutex(&Backend->Lock);
    Entry = RosvNetNatLookup(Backend, RosvNatProtoTcp, SrcIp, SrcPort, DstIp, DstPort);
    ShouldLogTcp = (InterlockedIncrement(&Backend->TraceTcpGuestCount) <= ROSV_NET_TRACE_TCP_GUEST_MAX) ||
                   (Flags & (TCP_FLAG_SYN | TCP_FLAG_FIN | TCP_FLAG_RST)) != 0 ||
                   TcpPayloadLen != 0;
    RosvNetTcpFlagsToString(Flags, FlagStr, sizeof(FlagStr));
    if (Entry != NULL)
        StateName = RosvNetTcpStateName(Entry->TcpState);

    if (ShouldLogTcp)
    {
        DbgPrint("[ROSV:NET] TCP guest %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u flags=%s seq=%lu ack=%lu len=%lu state=%s\n",
                 SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                 DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort,
                 FlagStr,
                 SeqNum,
                 AckNum,
                 TcpPayloadLen,
                 StateName);
    }

    if ((Flags & TCP_FLAG_SYN) && !(Flags & TCP_FLAG_ACK))
    {
        if (Entry != NULL)
        {
            if (Entry->TcpState == RosvTcpSynAckSent ||
                Entry->TcpState == RosvTcpEstablished)
            {
                RtlCopyMemory(GuestIpCopy, Entry->GuestIp, 4);
                RtlCopyMemory(DestIpCopy, Entry->DestIp, 4);
                GuestPortCopy = Entry->GuestPort;
                DestPortCopy = Entry->DestPort;
                SendSeq = Entry->GuestAckBase;
                SendAck = Entry->GuestSeqBase + 1;
                Entry->LastActivityMs = RosvNetNowMs();
                SendSynAck = TRUE;
                ExReleaseFastMutex(&Backend->Lock);
                if (SendSynAck)
                {
                    (void)RosvNetSendTcpToGuest(Backend,
                                                DestIpCopy,
                                                GuestIpCopy,
                                                DestPortCopy,
                                                GuestPortCopy,
                                                SendSeq,
                                                SendAck,
                                                TCP_FLAG_SYN | TCP_FLAG_ACK,
                                                NULL,
                                                0,
                                                TRUE);
                }
                return;
            }

            if (!Entry->ThreadRunning)
                RosvNetNatClearEntry(Entry);
            else
            {
                ExReleaseFastMutex(&Backend->Lock);
                return;
            }
        }

        NewEntry = RosvNetNatAllocate(Backend, RosvNatProtoTcp, SrcIp, SrcPort, DstIp, DstPort);
        if (NewEntry == NULL)
        {
            ExReleaseFastMutex(&Backend->Lock);
            (void)RosvNetSendTcpToGuest(Backend,
                                        DstIp,
                                        SrcIp,
                                        DstPort,
                                        SrcPort,
                                        0,
                                        SeqNum + 1,
                                        TCP_FLAG_RST | TCP_FLAG_ACK,
                                        NULL,
                                        0,
                                        FALSE);
            return;
        }

        RosvNetResolveRealDestination(Backend, DstIp, RealDstIp);
        NewEntry->Backend = Backend;
        NewEntry->Protocol = RosvNatProtoTcp;
        RtlCopyMemory(NewEntry->GuestIp, SrcIp, 4);
        NewEntry->GuestPort = SrcPort;
        RtlCopyMemory(NewEntry->DestIp, DstIp, 4);
        NewEntry->DestPort = DstPort;
        RtlCopyMemory(NewEntry->RealDestIp, RealDstIp, 4);
        NewEntry->RealDestPort = DstPort;
        NewEntry->TcpState = RosvTcpConnecting;
        NewEntry->GuestSeqBase = SeqNum;
        OurIsn = (ULONG)(InterlockedExchangeAdd(&Backend->TcpIsnCounter, 64240) + 64240);
        NewEntry->GuestAckBase = OurIsn;
        NewEntry->GuestSeqNext = SeqNum + 1;
        NewEntry->TimeoutMs = ROSV_NET_TCP_TIMEOUT_MS;
        NewEntry->ThreadRunning = TRUE;
        ExReleaseFastMutex(&Backend->Lock);

        Status = RosvNetSocketCreate(Backend,
                                     SOCK_STREAM,
                                     IPPROTO_TCP,
                                     WSK_FLAG_CONNECTION_SOCKET,
                                     &Socket);
        if (!NT_SUCCESS(Status))
            goto TcpCreateFail;

        RosvNetBuildWildcardAddress(&BindAddr);
        Status = RosvNetSocketBind(Socket, (PSOCKADDR)&BindAddr);
        if (!NT_SUCCESS(Status))
            goto TcpCreateFail;

        if (ShouldLogTcp)
        {
            DbgPrint("[ROSV:NET] TCP connect start %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u real=%u.%u.%u.%u\n",
                     SrcIp[0], SrcIp[1], SrcIp[2], SrcIp[3], SrcPort,
                     DstIp[0], DstIp[1], DstIp[2], DstIp[3], DstPort,
                     RealDstIp[0], RealDstIp[1], RealDstIp[2], RealDstIp[3]);
        }

        ExAcquireFastMutex(&Backend->Lock);
        if (!NewEntry->InUse || NewEntry->Closing)
        {
            ExReleaseFastMutex(&Backend->Lock);
            RosvNetSocketClose(Socket);
            return;
        }
        NewEntry->Socket = Socket;
        ExReleaseFastMutex(&Backend->Lock);

        Status = RosvNetSpawnThread(Backend, RosvNetTcpThreadProc, NewEntry);
        if (!NT_SUCCESS(Status))
            goto TcpCreateFail;
        return;

TcpCreateFail:
        if (Socket != NULL)
            RosvNetSocketClose(Socket);
        ExAcquireFastMutex(&Backend->Lock);
        if (NewEntry->InUse)
            RosvNetNatClearEntry(NewEntry);
        ExReleaseFastMutex(&Backend->Lock);
        (void)RosvNetSendTcpToGuest(Backend,
                                    DstIp,
                                    SrcIp,
                                    DstPort,
                                    SrcPort,
                                    0,
                                    SeqNum + 1,
                                    TCP_FLAG_RST | TCP_FLAG_ACK,
                                    NULL,
                                    0,
                                    FALSE);
        return;
    }

    if (Entry == NULL)
    {
        ExReleaseFastMutex(&Backend->Lock);
        if (!(Flags & TCP_FLAG_RST))
        {
            if (Flags & TCP_FLAG_ACK)
            {
                (void)RosvNetSendTcpToGuest(Backend,
                                            DstIp,
                                            SrcIp,
                                            DstPort,
                                            SrcPort,
                                            AckNum,
                                            0,
                                            TCP_FLAG_RST,
                                            NULL,
                                            0,
                                            FALSE);
            }
            else
            {
                ULONG RstAck;

                RstAck = SeqNum + TcpPayloadLen;
                if (Flags & TCP_FLAG_SYN)
                    RstAck++;
                if (Flags & TCP_FLAG_FIN)
                    RstAck++;
                (void)RosvNetSendTcpToGuest(Backend,
                                            DstIp,
                                            SrcIp,
                                            DstPort,
                                            SrcPort,
                                            0,
                                            RstAck,
                                            TCP_FLAG_RST | TCP_FLAG_ACK,
                                            NULL,
                                            0,
                                            FALSE);
            }
        }
        return;
    }

    /*
     * FIX 3: Copy IP addresses and ports from entry ONCE.
     * These locals are used for all subsequent send operations after
     * the lock is released (Entry pointer may become invalid).
     */
    RtlCopyMemory(GuestIpCopy, Entry->GuestIp, 4);
    RtlCopyMemory(DestIpCopy, Entry->DestIp, 4);
    GuestPortCopy = Entry->GuestPort;
    DestPortCopy = Entry->DestPort;
    Entry->LastActivityMs = RosvNetNowMs();

    /*
     * FIX 2: Process all state transitions under the single lock hold
     * acquired at function entry. Only release before blocking operations
     * (socket close, socket send). Re-acquire once after blocking if needed.
     */

    if (Flags & TCP_FLAG_RST)
    {
        Socket = Entry->Socket;
        Entry->Socket = NULL;
        Entry->ThreadRunning = FALSE;
        Entry->TcpState = RosvTcpClosed;
        RosvNetNatReleaseClosed(Entry);
        ExReleaseFastMutex(&Backend->Lock);
        if (Socket != NULL)
            RosvNetSocketClose(Socket);
        return;
    }

    /* Simplified TCP close: we skip LAST_ACK state entirely.
     * When host closed (BytesReceived==0), we sent FIN-ACK to guest and entered CloseWait.
     * Now when the guest ACKs our FIN (or sends its own FIN), we clear the NAT entry.
     * This is not fully RFC-793 compliant but sufficient for a NAT proxy. */
    if (Entry->TcpState == RosvTcpCloseWait)
    {
        if ((Flags & TCP_FLAG_ACK) || (Flags & TCP_FLAG_FIN))
        {
            SendSeq = Entry->GuestAckBase + 1 + Entry->BytesRecvFromHost;
            SendAck = SeqNum + TcpPayloadLen + ((Flags & TCP_FLAG_FIN) ? 1 : 0);
            DbgPrint("[ROSV:NET] TCP close-wait cleared %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u "
                     "flags=%s\n",
                     GuestIpCopy[0], GuestIpCopy[1], GuestIpCopy[2], GuestIpCopy[3], GuestPortCopy,
                     DestIpCopy[0], DestIpCopy[1], DestIpCopy[2], DestIpCopy[3], DestPortCopy,
                     FlagStr);
            RosvNetNatClearEntry(Entry);
            ExReleaseFastMutex(&Backend->Lock);
            if (Flags & TCP_FLAG_FIN)
            {
                (void)RosvNetSendTcpToGuest(Backend,
                                            DestIpCopy,
                                            GuestIpCopy,
                                            DestPortCopy,
                                            GuestPortCopy,
                                            SendSeq,
                                            SendAck,
                                            TCP_FLAG_ACK,
                                            NULL,
                                            0,
                                            FALSE);
            }
            return;
        }
    }

    if (Entry->TcpState == RosvTcpFinWait)
    {
        if ((Flags & TCP_FLAG_FIN) || TcpPayloadLen != 0)
        {
            SendSeq = Entry->GuestAckBase + 1 + Entry->BytesRecvFromHost;
            SendAck = Entry->GuestSeqNext;
            ExReleaseFastMutex(&Backend->Lock);
            (void)RosvNetSendTcpToGuest(Backend,
                                        DestIpCopy,
                                        GuestIpCopy,
                                        DestPortCopy,
                                        GuestPortCopy,
                                        SendSeq,
                                        SendAck,
                                        TCP_FLAG_ACK,
                                        NULL,
                                        0,
                                        FALSE);
            return;
        }
    }

    if (Entry->TcpState == RosvTcpSynAckSent && (Flags & TCP_FLAG_ACK))
        Entry->TcpState = RosvTcpEstablished;

    if (TcpPayloadLen > 0 &&
        (Entry->TcpState == RosvTcpSynAckSent || Entry->TcpState == RosvTcpEstablished) &&
        Entry->Socket != NULL &&
        !Entry->Closing)
    {
        if (SeqNum < Entry->GuestSeqNext)
        {
            DuplicateBytes = Entry->GuestSeqNext - SeqNum;
            if (DuplicateBytes >= TcpPayloadLen)
            {
                SendSeq = Entry->GuestAckBase + 1 + Entry->BytesRecvFromHost;
                SendAck = Entry->GuestSeqNext;
                SendAckOnly = TRUE;
                ExReleaseFastMutex(&Backend->Lock);
                goto TcpSendAckOnly;
            }

            Payload += DuplicateBytes;
            TcpPayloadLen -= DuplicateBytes;
            SeqNum = Entry->GuestSeqNext;
        }
        else if (SeqNum > Entry->GuestSeqNext)
        {
            SendSeq = Entry->GuestAckBase + 1 + Entry->BytesRecvFromHost;
            SendAck = Entry->GuestSeqNext;
            SendAckOnly = TRUE;
            ExReleaseFastMutex(&Backend->Lock);
            goto TcpSendAckOnly;
        }

        Socket = Entry->Socket;
        Entry->TcpState = RosvTcpEstablished;
        ExReleaseFastMutex(&Backend->Lock);

        /* Blocking send - lock released */
        BytesSent = 0;
        Status = RosvNetSocketSend(Socket, (PVOID)Payload, TcpPayloadLen, &BytesSent);
        if (ShouldLogTcp)
        {
            DbgPrint("[ROSV:NET] TCP send host %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u status=0x%08X bytes=%Iu requested=%lu\n",
                     GuestIpCopy[0], GuestIpCopy[1], GuestIpCopy[2], GuestIpCopy[3], GuestPortCopy,
                     DestIpCopy[0], DestIpCopy[1], DestIpCopy[2], DestIpCopy[3], DestPortCopy,
                     Status,
                     BytesSent,
                     TcpPayloadLen);
        }

        /*
         * Single re-acquire after blocking send to update entry state.
         * Handles both success and failure in one lock acquisition.
         */
        ExAcquireFastMutex(&Backend->Lock);
        Entry = RosvNetNatLookup(Backend, RosvNatProtoTcp, GuestIpCopy, GuestPortCopy, DestIpCopy, DestPortCopy);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("[ROSV:NET] TCP send to host FAILED %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u "
                     "status=0x%08X bytes=%Iu requested=%lu\n",
                     GuestIpCopy[0], GuestIpCopy[1], GuestIpCopy[2], GuestIpCopy[3], GuestPortCopy,
                     DestIpCopy[0], DestIpCopy[1], DestIpCopy[2], DestIpCopy[3], DestPortCopy,
                     Status, BytesSent, TcpPayloadLen);
            if (Entry != NULL)
            {
                Entry->Closing = TRUE;
                Entry->Socket = NULL;
                Entry->TcpState = RosvTcpClosed;
                Entry->ThreadRunning = FALSE;
                RosvNetNatReleaseClosed(Entry);
            }
            ExReleaseFastMutex(&Backend->Lock);

            RosvNetSocketClose(Socket);
            (void)RosvNetSendTcpToGuest(Backend,
                                        DestIpCopy,
                                        GuestIpCopy,
                                        DestPortCopy,
                                        GuestPortCopy,
                                        0,
                                        SeqNum + TcpPayloadLen,
                                        TCP_FLAG_RST | TCP_FLAG_ACK,
                                        NULL,
                                        0,
                                        FALSE);
            return;
        }

        /* Zero-byte success means host peer closed gracefully (like recv returning 0).
         * Don't RST — just ACK what we got and let the recv-completion path handle FIN. */
        if (BytesSent == 0)
        {
            DbgPrint("[ROSV:NET] TCP send returned 0 bytes (host may have closed) "
                     "%u.%u.%u.%u:%u -> %u.%u.%u.%u:%u\n",
                     GuestIpCopy[0], GuestIpCopy[1], GuestIpCopy[2], GuestIpCopy[3], GuestPortCopy,
                     DestIpCopy[0], DestIpCopy[1], DestIpCopy[2], DestIpCopy[3], DestPortCopy);
            if (Entry != NULL)
            {
                SendSeq = Entry->GuestAckBase + 1 + Entry->BytesRecvFromHost;
                SendAck = Entry->GuestSeqNext;
            }
            ExReleaseFastMutex(&Backend->Lock);

            if (Entry != NULL)
            {
                (void)RosvNetSendTcpToGuest(Backend,
                                            DestIpCopy,
                                            GuestIpCopy,
                                            DestPortCopy,
                                            GuestPortCopy,
                                            SendSeq,
                                            SendAck,
                                            TCP_FLAG_ACK,
                                            NULL,
                                            0,
                                            FALSE);
            }
            return;
        }

        if (Entry != NULL)
        {
            Entry->BytesSentToHost += (ULONG)BytesSent;
            Entry->GuestSeqNext = SeqNum + (ULONG)BytesSent;
            SendSeq = Entry->GuestAckBase + 1 + Entry->BytesRecvFromHost;
            SendAck = Entry->GuestSeqNext;
        }
        ExReleaseFastMutex(&Backend->Lock);

        if (Entry != NULL)
        {
            (void)RosvNetSendTcpToGuest(Backend,
                                        DestIpCopy,
                                        GuestIpCopy,
                                        DestPortCopy,
                                        GuestPortCopy,
                                        SendSeq,
                                        SendAck,
                                        TCP_FLAG_ACK,
                                        NULL,
                                        0,
                                        FALSE);
        }
        return;
    }

    if (Flags & TCP_FLAG_FIN)
    {
        Socket = Entry->Socket;
        SendSeq = Entry->GuestAckBase + 1 + Entry->BytesRecvFromHost;
        SendAck = SeqNum + TcpPayloadLen + 1;
        ExReleaseFastMutex(&Backend->Lock);

        if (Socket != NULL)
        {
            Status = RosvNetSocketDisconnect(Socket, 0);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint("[ROSV:NET] TCP guest FIN disconnect failed %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u "
                         "status=0x%08X\n",
                         GuestIpCopy[0], GuestIpCopy[1], GuestIpCopy[2], GuestIpCopy[3], GuestPortCopy,
                         DestIpCopy[0], DestIpCopy[1], DestIpCopy[2], DestIpCopy[3], DestPortCopy,
                         Status);
                RosvNetSocketClose(Socket);
                ExAcquireFastMutex(&Backend->Lock);
                Entry = RosvNetNatLookup(Backend,
                                         RosvNatProtoTcp,
                                         GuestIpCopy,
                                         GuestPortCopy,
                                         DestIpCopy,
                                         DestPortCopy);
                if (Entry != NULL && Entry->Socket == Socket)
                {
                    Entry->Closing = TRUE;
                    Entry->Socket = NULL;
                    Entry->ThreadRunning = FALSE;
                    Entry->TcpState = RosvTcpClosed;
                    RosvNetNatReleaseClosed(Entry);
                }
                ExReleaseFastMutex(&Backend->Lock);
                (void)RosvNetSendTcpToGuest(Backend,
                                            DestIpCopy,
                                            GuestIpCopy,
                                            DestPortCopy,
                                            GuestPortCopy,
                                            0,
                                            SendAck,
                                            TCP_FLAG_RST | TCP_FLAG_ACK,
                                            NULL,
                                            0,
                                            FALSE);
                return;
            }
        }

        ExAcquireFastMutex(&Backend->Lock);
        Entry = RosvNetNatLookup(Backend,
                                 RosvNatProtoTcp,
                                 GuestIpCopy,
                                 GuestPortCopy,
                                 DestIpCopy,
                                 DestPortCopy);
        if (Entry != NULL && Entry->Socket == Socket && !Entry->Closing)
        {
            Entry->TcpState = RosvTcpFinWait;
            Entry->GuestSeqNext = SendAck;
            Entry->LastActivityMs = RosvNetNowMs();
        }
        ExReleaseFastMutex(&Backend->Lock);

        (void)RosvNetSendTcpToGuest(Backend,
                                    DestIpCopy,
                                    GuestIpCopy,
                                    DestPortCopy,
                                    GuestPortCopy,
                                    SendSeq,
                                    SendAck,
                                    TCP_FLAG_ACK,
                                    NULL,
                                    0,
                                    FALSE);
        return;
    }

    /* Pure ACK with no data/FIN/RST in established state — nothing to forward to host.
     * This is normal TCP behavior (e.g. ACKing received data). */
    if (ShouldLogTcp && !SendAckOnly && TcpPayloadLen == 0)
    {
        DbgPrint("[ROSV:NET] TCP pure ACK %u.%u.%u.%u:%u -> %u.%u.%u.%u:%u "
                 "seq=%lu ack=%lu state=%s (no action)\n",
                 GuestIpCopy[0], GuestIpCopy[1], GuestIpCopy[2], GuestIpCopy[3], GuestPortCopy,
                 DestIpCopy[0], DestIpCopy[1], DestIpCopy[2], DestIpCopy[3], DestPortCopy,
                 SeqNum, AckNum, StateName);
    }
    ExReleaseFastMutex(&Backend->Lock);
    if (SendAckOnly)
    {
TcpSendAckOnly:
        (void)RosvNetSendTcpToGuest(Backend,
                                    DestIpCopy,
                                    GuestIpCopy,
                                    DestPortCopy,
                                    GuestPortCopy,
                                    SendSeq,
                                    SendAck,
                                    TCP_FLAG_ACK,
                                    NULL,
                                    0,
                                    FALSE);
    }
}

static NTSTATUS NTAPI
RosvNetClientEvent(
    _In_opt_ PVOID ClientContext,
    _In_ ULONG EventType,
    _In_reads_bytes_opt_(InformationLength) PVOID Information,
    _In_ SIZE_T InformationLength)
{
    UNREFERENCED_PARAMETER(ClientContext);
    UNREFERENCED_PARAMETER(EventType);
    UNREFERENCED_PARAMETER(Information);
    UNREFERENCED_PARAMETER(InformationLength);
    return STATUS_SUCCESS;
}

static VOID NTAPI
RosvNetPumpThreadProc(
    _In_ PVOID Context)
{
    PROSV_NET_BACKEND_STATE Backend;
    PROSV_NET_PACKET Packet;
    LARGE_INTEGER Timeout;

    Backend = (PROSV_NET_BACKEND_STATE)Context;
    Packet = (PROSV_NET_PACKET)ExAllocatePoolZero(NonPagedPool,
                                                  sizeof(*Packet),
                                                  ROSV_NET_TAG);
    if (Packet == NULL)
    {
        PsTerminateSystemThread(STATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    Timeout.QuadPart = -(LONGLONG)(ROSV_NET_PUMP_IDLE_TIMEOUT_MS * 1000 * 10);
    while (Backend->Running)
    {
        BOOLEAN DidWork;
        PVOID WaitObjects[2];
        KWAIT_BLOCK WaitBlocks[2];
        NTSTATUS WaitStatus;

        DidWork = FALSE;
        RosvNetDrainRecvCompletions(Backend);
        RosvNetFlushPendingTcpSends(Backend);
        RosvNetPostPendingTcpReceives(Backend);
        while (Backend->Running)
        {
            RtlZeroMemory(Packet, sizeof(*Packet));
            if (!RosvVirtioNetDequeueTxPacket(&Backend->Vm->VirtioNet, Packet) ||
                Packet->Length == 0)
            {
                break;
            }

            Backend->TxPackets++;
            Backend->TxBytes += Packet->Length;
            RosvNetHandleGuestPacket(Backend, Packet);
            DidWork = TRUE;
        }

        RosvNetCleanupExpired(Backend);
        if (!Backend->Running)
            break;

        if (DidWork)
            continue;

        WaitObjects[0] = &Backend->Vm->VirtioNet.TxReadyEvent;
        WaitObjects[1] = &Backend->CompletionEvent;
        WaitStatus = KeWaitForMultipleObjects(2,
                                              WaitObjects,
                                              WaitAny,
                                              Executive,
                                              KernelMode,
                                              FALSE,
                                              &Timeout,
                                              WaitBlocks);

        if (WaitStatus == STATUS_WAIT_0)
        {
            RtlZeroMemory(Packet, sizeof(*Packet));
            if (RosvVirtioNetDequeueTxPacket(&Backend->Vm->VirtioNet, Packet) &&
                Packet->Length != 0)
            {
                Backend->TxPackets++;
                Backend->TxBytes += Packet->Length;
                RosvNetHandleGuestPacket(Backend, Packet);
            }
        }
        else if (WaitStatus == STATUS_WAIT_1)
        {
            RosvNetDrainRecvCompletions(Backend);
            RosvNetPostPendingTcpReceives(Backend);
        }
    }

    ExFreePoolWithTag(Packet, ROSV_NET_TAG);
    PsTerminateSystemThread(STATUS_SUCCESS);
}

NTSTATUS
RosvNetBackendCreate(
    _Inout_ PROSV_VM Vm)
{
    PROSV_NET_BACKEND_STATE Backend;

    if (Vm->NetBackend != NULL)
        return STATUS_SUCCESS;

    Backend = (PROSV_NET_BACKEND_STATE)ExAllocatePoolZero(NonPagedPool,
                                                          sizeof(*Backend),
                                                          ROSV_NET_TAG);
    if (Backend == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Backend->Vm = Vm;
    ExInitializeFastMutex(&Backend->Lock);
    KeInitializeEvent(&Backend->IoThreadsDoneEvent, NotificationEvent, TRUE);
    Backend->ClientDispatch.Version = MAKE_WSK_VERSION(1, 0);
    Backend->ClientDispatch.Reserved = 0;
    Backend->ClientDispatch.WskClientEvent = RosvNetClientEvent;
    Backend->ClientNpi.ClientContext = Backend;
    Backend->ClientNpi.Dispatch = &Backend->ClientDispatch;

    Backend->HostMac[0] = 0x52;
    Backend->HostMac[1] = 0x54;
    Backend->HostMac[2] = 0x00;
    Backend->HostMac[3] = 0x12;
    Backend->HostMac[4] = 0x34;
    Backend->HostMac[5] = 0x02;
    Backend->HostIp[0] = 10;
    Backend->HostIp[1] = 0;
    Backend->HostIp[2] = 2;
    Backend->HostIp[3] = 2;
    Backend->GuestIp[0] = 10;
    Backend->GuestIp[1] = 0;
    Backend->GuestIp[2] = 2;
    Backend->GuestIp[3] = 15;
    Backend->GuestMask[0] = 255;
    Backend->GuestMask[1] = 255;
    Backend->GuestMask[2] = 255;
    Backend->GuestMask[3] = 0;
    Backend->DnsIp[0] = 10;
    Backend->DnsIp[1] = 0;
    Backend->DnsIp[2] = 2;
    Backend->DnsIp[3] = 3;
    Backend->GuestMac[0] = 0x52;
    Backend->GuestMac[1] = 0x54;
    Backend->GuestMac[2] = 0x00;
    Backend->GuestMac[3] = 0x12;
    Backend->GuestMac[4] = 0x34;
    Backend->GuestMac[5] = 0x56;
    Backend->HostDnsIp[0] = 8;
    Backend->HostDnsIp[1] = 8;
    Backend->HostDnsIp[2] = 8;
    Backend->HostDnsIp[3] = 8;
    Backend->TcpIsnCounter = 0x10000;
    Backend->IpIdCounter = 1;
    KeInitializeSpinLock(&Backend->CompletionLock);
    KeInitializeEvent(&Backend->CompletionEvent, NotificationEvent, FALSE);
    Backend->CompletionHead = 0;
    Backend->CompletionTail = 0;
    Backend->CompletionCount = 0;
    Vm->NetBackend = Backend;
    return STATUS_SUCCESS;
}

NTSTATUS
RosvNetBackendStart(
    _Inout_ PROSV_VM Vm)
{
    PROSV_NET_BACKEND_STATE Backend;
    NTSTATUS Status;

    Status = RosvNetBackendCreate(Vm);
    if (!NT_SUCCESS(Status))
        return Status;

    Backend = Vm->NetBackend;
    if (Backend->Started)
        return STATUS_SUCCESS;

    Status = WskRegister(&Backend->ClientNpi, &Backend->WskRegistration);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = WskCaptureProviderNPI(&Backend->WskRegistration,
                                   WSK_INFINITE_WAIT,
                                   &Backend->ProviderNpi);
    if (!NT_SUCCESS(Status))
    {
        WskDeregister(&Backend->WskRegistration);
        return Status;
    }

    Backend->Running = TRUE;
    Backend->PumpThreadHandle = NULL;
    Backend->PumpThreadObject = NULL;
    Status = PsCreateSystemThread(&Backend->PumpThreadHandle,
                                  THREAD_ALL_ACCESS,
                                  NULL,
                                  NULL,
                                  NULL,
                                  RosvNetPumpThreadProc,
                                  Backend);
    if (!NT_SUCCESS(Status))
    {
        Backend->Running = FALSE;
        WskReleaseProviderNPI(&Backend->WskRegistration);
        WskDeregister(&Backend->WskRegistration);
        RtlZeroMemory(&Backend->ProviderNpi, sizeof(Backend->ProviderNpi));
        return Status;
    }

    Status = ObReferenceObjectByHandle(Backend->PumpThreadHandle,
                                       THREAD_ALL_ACCESS,
                                       NULL,
                                       KernelMode,
                                       (PVOID *)&Backend->PumpThreadObject,
                                       NULL);
    if (!NT_SUCCESS(Status))
    {
        ZwClose(Backend->PumpThreadHandle);
        Backend->PumpThreadHandle = NULL;
        Backend->Running = FALSE;
        WskReleaseProviderNPI(&Backend->WskRegistration);
        WskDeregister(&Backend->WskRegistration);
        RtlZeroMemory(&Backend->ProviderNpi, sizeof(Backend->ProviderNpi));
        return Status;
    }

    Backend->Started = TRUE;
    return STATUS_SUCCESS;
}

VOID
RosvNetBackendStop(
    _Inout_ PROSV_VM Vm)
{
    PROSV_NET_BACKEND_STATE Backend;
    PWSK_SOCKET Sockets[ROSV_NET_NAT_TABLE_SIZE];
    ULONG i;

    Backend = Vm->NetBackend;
    if (Backend == NULL || !Backend->Started)
        return;

    Backend->Running = FALSE;
    KeSetEvent(&Vm->VirtioNet.TxReadyEvent, IO_NO_INCREMENT, FALSE);
    KeSetEvent(&Backend->CompletionEvent, IO_NO_INCREMENT, FALSE);

    RtlZeroMemory(Sockets, sizeof(Sockets));
    ExAcquireFastMutex(&Backend->Lock);
    for (i = 0; i < ROSV_NET_NAT_TABLE_SIZE; i++)
    {
        if (!Backend->NatTable[i].InUse)
            continue;
        Backend->NatTable[i].Closing = TRUE;
        Sockets[i] = Backend->NatTable[i].Socket;
        Backend->NatTable[i].Socket = NULL;
    }
    ExReleaseFastMutex(&Backend->Lock);

    for (i = 0; i < ROSV_NET_NAT_TABLE_SIZE; i++)
    {
        if (Sockets[i] != NULL)
            RosvNetSocketClose(Sockets[i]);
    }

    if (Backend->PumpThreadObject != NULL)
    {
        KeWaitForSingleObject(Backend->PumpThreadObject,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        ObDereferenceObject(Backend->PumpThreadObject);
        Backend->PumpThreadObject = NULL;
    }

    if (Backend->PumpThreadHandle != NULL)
    {
        ZwClose(Backend->PumpThreadHandle);
        Backend->PumpThreadHandle = NULL;
    }

    if (InterlockedCompareExchange(&Backend->ActiveIoThreads, 0, 0) > 0)
    {
        KeWaitForSingleObject(&Backend->IoThreadsDoneEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
    }

    WskReleaseProviderNPI(&Backend->WskRegistration);
    WskDeregister(&Backend->WskRegistration);
    RtlZeroMemory(&Backend->ProviderNpi, sizeof(Backend->ProviderNpi));
    for (i = 0; i < ROSV_NET_NAT_TABLE_SIZE; i++)
        RosvNetFreeRecvResources(&Backend->NatTable[i]);
    RtlZeroMemory(Backend->NatTable, sizeof(Backend->NatTable));
    Backend->Started = FALSE;
}

VOID
RosvNetBackendDestroy(
    _Inout_ PROSV_VM Vm)
{
    if (Vm->NetBackend == NULL)
        return;

    RosvNetBackendStop(Vm);
    ExFreePoolWithTag(Vm->NetBackend, ROSV_NET_TAG);
    Vm->NetBackend = NULL;
}

VOID
RosvNetBackendKick(
    _In_ PROSV_VM Vm)
{
    PROSV_NET_BACKEND_STATE Backend;

    if (Vm == NULL)
        return;

    Backend = Vm->NetBackend;
    if (Backend == NULL || !Backend->Started || !Backend->Running)
        return;

    KeSetEvent(&Backend->CompletionEvent, IO_NO_INCREMENT, FALSE);
}

BOOLEAN
RosvNetBackendIsActive(
    _In_ PROSV_VM Vm)
{
    return (BOOLEAN)(Vm != NULL &&
                     Vm->NetBackend != NULL &&
                     Vm->NetBackend->Started);
}

VOID
RosvNetBackendQueryStats(
    _In_ PROSV_VM Vm,
    _Out_opt_ PULONG64 TxPackets,
    _Out_opt_ PULONG64 TxBytes,
    _Out_opt_ PULONG64 RxPackets,
    _Out_opt_ PULONG64 RxBytes)
{
    PROSV_NET_BACKEND_STATE Backend;

    Backend = Vm->NetBackend;
    if (TxPackets != NULL)
        *TxPackets = 0;
    if (TxBytes != NULL)
        *TxBytes = 0;
    if (RxPackets != NULL)
        *RxPackets = 0;
    if (RxBytes != NULL)
        *RxBytes = 0;

    if (Backend == NULL)
        return;

    if (TxPackets != NULL)
        *TxPackets = Backend->TxPackets;
    if (TxBytes != NULL)
        *TxBytes = Backend->TxBytes;
    if (RxPackets != NULL)
        *RxPackets = Backend->RxPackets;
    if (RxBytes != NULL)
        *RxBytes = Backend->RxBytes;
}
