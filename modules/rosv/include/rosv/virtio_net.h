/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Virtio-net MMIO device emulation - structures and prototypes
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements virtio 1.0+ MMIO transport (virtio-v1.0-cs04, Section 4.2)
 * with a virtio-net device (Section 5.1).
 *
 * The device provides two virtqueues:
 *   Queue 0: Receiveq (device writes packets to guest)
 *   Queue 1: Transmitq (guest writes packets to device)
 *
 * Packet format: virtio_net_hdr (12 bytes) + raw ethernet frame.
 */

#pragma once

#include <rosv/rosv.h>
#include <rosv/virtio_blk.h>  /* Shared virtio MMIO register defs, virtqueue structs */

/* ---- Virtio-net device identity ----------------------------------------- */

#define VIRTIO_ID_NET                   1

/* ---- Virtio-net feature bits (Section 5.1.3) ---------------------------- */

#define VIRTIO_NET_F_MAC                (1ULL << 5)   /* Device has given MAC address */
#define VIRTIO_NET_F_STATUS             (1ULL << 16)  /* Config status field available */
#define VIRTIO_NET_F_MRG_RXBUF         (1ULL << 15)  /* Merged receive buffers */
#define VIRTIO_NET_F_CTRL_VQ           (1ULL << 17)  /* Control channel available */
#define VIRTIO_NET_F_MQ                (1ULL << 22)  /* Multiqueue support */
#define VIRTIO_NET_F_MTU               (1ULL << 3)   /* MTU value in config space */
#define VIRTIO_NET_F_CSUM              (1ULL << 0)   /* Checksum offload */
#define VIRTIO_NET_F_GUEST_CSUM        (1ULL << 1)   /* Guest handles partial csum */

/* ---- Virtio-net config space (Section 5.1.4) ---------------------------- */

#define VIRTIO_NET_S_LINK_UP            1       /* Link is up */
#define VIRTIO_NET_S_ANNOUNCE           2       /* Announcement needed */

#include <pshpack1.h>

typedef struct _VIRTIO_NET_CONFIG {
    UCHAR  Mac[6];              /* MAC address */
    USHORT Status;              /* VIRTIO_NET_S_* */
    USHORT MaxVirtqueuePairs;   /* Number of TX/RX queue pairs */
    USHORT Mtu;                 /* Maximum transmission unit */
} VIRTIO_NET_CONFIG, *PVIRTIO_NET_CONFIG;

/* Virtio-net packet header (Section 5.1.6) - 12 bytes for non-merged mode */
typedef struct _VIRTIO_NET_HDR {
    UCHAR  Flags;               /* VIRTIO_NET_HDR_F_* */
    UCHAR  GsoType;             /* VIRTIO_NET_HDR_GSO_* */
    USHORT HdrLen;              /* Ethernet + IP + TCP/UDP header length */
    USHORT GsoSize;             /* Maximum segment size for GSO */
    USHORT CsumStart;           /* Offset to start checksumming */
    USHORT CsumOffset;          /* Offset within packet to store checksum */
    USHORT NumBuffers;          /* Only with VIRTIO_NET_F_MRG_RXBUF */
} VIRTIO_NET_HDR, *PVIRTIO_NET_HDR;

#include <poppack.h>

C_ASSERT(sizeof(VIRTIO_NET_HDR) == 12);

/* Net header flags */
#define VIRTIO_NET_HDR_F_NEEDS_CSUM     0x01
#define VIRTIO_NET_HDR_F_DATA_VALID     0x02

/* Net header GSO types */
#define VIRTIO_NET_HDR_GSO_NONE         0
#define VIRTIO_NET_HDR_GSO_TCPV4        1
#define VIRTIO_NET_HDR_GSO_UDP          3
#define VIRTIO_NET_HDR_GSO_TCPV6        4
#define VIRTIO_NET_HDR_GSO_ECN          0x80

/* ---- Virtio-net MMIO addresses and IRQ ---------------------------------- */

/* Placed at 0xFEB01000, immediately after the virtio-blk region (0xFEB00000).
 * Must not overlap with any other MMIO region (LAPIC, IOAPIC, virtio-blk). */
#define ROSV_VIRTIO_NET_MMIO_BASE       0xFEB01000ULL
#define ROSV_VIRTIO_NET_MMIO_SIZE       0x1000ULL   /* 4KB page */

/* Virtual IRQ line for virtio-net interrupt injection.
 * Uses IRQ 6, separate from virtio-blk IRQ 5. */
#define ROSV_VIRTIO_NET_IRQ             6

/* ---- Virtqueue indices -------------------------------------------------- */

#define VIRTIO_NET_QUEUE_RX             0   /* Receiveq: device -> guest */
#define VIRTIO_NET_QUEUE_TX             1   /* Transmitq: guest -> device */
#define VIRTIO_NET_NUM_QUEUES           2

/* ---- TX packet ring buffer (guest -> host) ------------------------------ */

#define ROSV_VIRTIO_NET_TX_RING_SIZE    1024
#define ROSV_VIRTIO_NET_RX_RING_SIZE    2048
#define ROSV_VIRTIO_NET_MAX_PACKET      1600  /* Same as ROSV_NET_PACKET_MAX */

/* Ring sizes must be powers of two for & (SIZE - 1) wraparound to work */
C_ASSERT((ROSV_VIRTIO_NET_TX_RING_SIZE & (ROSV_VIRTIO_NET_TX_RING_SIZE - 1)) == 0);
C_ASSERT((ROSV_VIRTIO_NET_RX_RING_SIZE & (ROSV_VIRTIO_NET_RX_RING_SIZE - 1)) == 0);

typedef struct _ROSV_VIRTIO_NET_TX_ENTRY {
    ULONG Length;                               /* Bytes used in Data[] (0 = empty) */
    ULONG64 EnqueueQpc;                         /* Enqueue timestamp for latency tracing */
    ULONG64 TraceSeq;                           /* Monotonic trace sequence number */
    UCHAR Data[ROSV_VIRTIO_NET_MAX_PACKET];     /* Raw ethernet frame (no virtio hdr) */
} ROSV_VIRTIO_NET_TX_ENTRY, *PROSV_VIRTIO_NET_TX_ENTRY;

/* ---- Interrupt coalescing state ----------------------------------------- */

/*
 * Reduces interrupt storms during high-throughput network I/O by batching
 * interrupts. An interrupt is injected only when:
 *   - PendingCount >= CoalesceMaxPackets, OR
 *   - Time since LastIrqQpc >= CoalesceMaxUsec, OR
 *   - The guest vCPU is halted (always wake it immediately).
 *
 * Default: CoalesceMaxPackets=1 (effectively disabled / inject every packet).
 * Tune via the CoalesceMaxPackets/CoalesceMaxUsec fields at runtime.
 */
typedef struct _ROSV_VIRTIO_NET_IRQ_COALESCE {
    ULONG   PendingCount;       /* Packets processed since last interrupt */
    LONG64  LastIrqQpc;         /* QPC tick of last interrupt injection */
    ULONG   CoalesceMaxPackets; /* Inject after N packets (0/1 = no coalescing) */
    ULONG   CoalesceMaxUsec;    /* Or after N microseconds (0 = no time limit) */
} ROSV_VIRTIO_NET_IRQ_COALESCE, *PROSV_VIRTIO_NET_IRQ_COALESCE;

/* ---- Virtio-net device state -------------------------------------------- */

typedef struct _ROSV_VIRTIO_NET_STATE {
    /* MMIO transport state (same register model as virtio-blk) */
    ULONG   DeviceFeaturesSelPage;
    ULONG   DriverFeaturesSelPage;
    ULONG64 DeviceFeatures;
    ULONG64 DriverFeatures;
    ULONG   Status;
    ULONG   InterruptStatus;
    BOOLEAN InterruptPending;
    KSPIN_LOCK InterruptLock;   /* Protects InterruptStatus + InterruptPending */
    ULONG   ConfigGeneration;

    /* Queue selector and virtqueues */
    ULONG   QueueSel;
    ROSV_VIRTQUEUE Vq[VIRTIO_NET_NUM_QUEUES]; /* [0]=RX, [1]=TX */

    /* Network device configuration */
    VIRTIO_NET_CONFIG Config;

    /* TX ring buffer: packets extracted from guest TX virtqueue,
     * waiting to be picked up by the host network backend.
     * (Guest TX -> host RX from the host's perspective.) */
    ROSV_VIRTIO_NET_TX_ENTRY TxRing[ROSV_VIRTIO_NET_TX_RING_SIZE];
    ULONG TxRingHead;      /* Next write position (producer: QUEUE_NOTIFY path) */
    ULONG TxRingTail;      /* Next read position (consumer: NET_RX IOCTL) */
    ULONG TxRingCount;     /* Current number of buffered packets */
    BOOLEAN TxBackpressure; /* Guest TX queue has pending work but TxRing is full */
    KSPIN_LOCK TxRingLock;  /* Protects TxRing* fields */
    KEVENT TxReadyEvent;   /* Signaled when packet enters TxRing (for blocking NET_RX) */
    FAST_MUTEX TxQueueMutex; /* Serialize guest TX virtqueue consumption */

    /* RX path: packets from the host network backend pushed to guest RX virtqueue.
     * If guest RX virtqueue has buffers available, inject directly.
     * Otherwise, buffer in a ring for later injection when guest posts
     * new RX buffers (QUEUE_NOTIFY on queue 0). */
    ROSV_VIRTIO_NET_TX_ENTRY RxRing[ROSV_VIRTIO_NET_RX_RING_SIZE];
    ULONG RxRingHead;      /* Next write position (producer: InjectRxPacket) */
    ULONG RxRingTail;      /* Next read position (consumer: QUEUE_NOTIFY on queue 0) */
    ULONG RxRingCount;     /* Current number of buffered packets */
    KSPIN_LOCK RxLock;      /* Protects RxRing* fields */
    KEVENT RxPendingEvent;  /* Signaled when host→guest packet injected (wakes vCPU yield) */
    FAST_MUTEX RxQueueMutex; /* Serialize guest RX virtqueue writes */

    /* Interrupt coalescing (reduces VM-exit rate during bulk transfers) */
    ROSV_VIRTIO_NET_IRQ_COALESCE IrqCoalesce;

    /* Parent VM (for GPA translation and interrupt injection) */
    PROSV_VM OwnerVm;

    /* Statistics */
    ULONG64 TxPackets;
    ULONG64 TxBytes;
    ULONG64 RxPackets;
    ULONG64 RxBytes;
    ULONG   TxDropCount;    /* Packets dropped due to TX ring full */
    ULONG   RxDropCount;    /* Packets dropped due to RX ring full */

    /* Perf tracing state */
    LARGE_INTEGER PerfFrequency;
    ULONG64 TxTraceSeqNext;
    ULONG64 RxTraceSeqNext;
} ROSV_VIRTIO_NET_STATE, *PROSV_VIRTIO_NET_STATE;

/* ---- Function prototypes (device/virtio_net.c) -------------------------- */

NTSTATUS
RosvVirtioNetInitialize(
    _Out_ PROSV_VIRTIO_NET_STATE State,
    _In_ PROSV_VM Vm,
    _In_reads_(6) const UCHAR *MacAddress);

VOID
RosvVirtioNetDestroy(
    _Inout_ PROSV_VIRTIO_NET_STATE State);

BOOLEAN
RosvVirtioNetMmioRead(
    _In_ PROSV_VIRTIO_NET_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _Out_ PULONG64 Value);

BOOLEAN
RosvVirtioNetMmioWrite(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _In_ ULONG64 Value);

BOOLEAN
RosvVirtioNetHasPendingInterrupt(
    _In_ PROSV_VIRTIO_NET_STATE State);

VOID
RosvVirtioNetClearPendingInterrupt(
    _Inout_ PROSV_VIRTIO_NET_STATE State);

VOID
RosvVirtioNetOnGuestEoi(
    _Inout_ PROSV_VIRTIO_NET_STATE State);

/* Interrupt coalescing: returns TRUE if the interrupt should be delivered now.
 * Called from the injection path; accounts for packet count, time, and guest HLT state. */
BOOLEAN
RosvVirtioNetShouldInjectIrq(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _In_ BOOLEAN GuestIsHalted);

/* Record that a virtio-net interrupt was actually injected (updates coalescing timers). */
VOID
RosvVirtioNetRecordIrqInjected(
    _Inout_ PROSV_VIRTIO_NET_STATE State);

/* TX dequeue: host backend picks up a packet the guest sent. */
BOOLEAN
RosvVirtioNetDequeueTxPacket(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _Out_ PROSV_NET_PACKET PacketOut);

/* RX inject: host backend pushes a packet to the guest. */
BOOLEAN
RosvVirtioNetInjectRxPacket(
    _Inout_ PROSV_VIRTIO_NET_STATE State,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);

/* RX backpressure: check if RX ring has space before attempting to enqueue.
 * Callers can use this to avoid calling InjectRxPacket when it will certainly drop. */
BOOLEAN
RosvVirtioNetRxRingHasSpace(
    _In_ PROSV_VIRTIO_NET_STATE State);
