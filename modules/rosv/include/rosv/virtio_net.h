/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Virtio-net MMIO device emulation - structures and prototypes
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements virtio 1.0+ MMIO transport (virtio-v1.0-cs04, Section 4.2)
 * with a virtio-net device (Section 5.1).
 *
 * Multi-queue (VIRTIO_NET_F_MQ) layout:
 *   Queue 2*N:   Receiveq N  (device writes packets to guest)
 *   Queue 2*N+1: Transmitq N (guest writes packets to device)
 *
 * When MQ is not negotiated, only queue pair 0 (queues 0/1) is used.
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
#define VIRTIO_NET_F_HOST_TSO4         (1ULL << 11)  /* Host can handle TSOv4 */
#define VIRTIO_NET_F_HOST_TSO6         (1ULL << 12)  /* Host can handle TSOv6 */

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

/* ---- Multi-queue configuration ------------------------------------------ */

/* Maximum number of TX/RX queue pairs.  When VIRTIO_NET_F_MQ is negotiated,
 * the driver reads MaxVirtqueuePairs from config space and uses up to that
 * many pairs.  Each pair occupies two consecutive virtqueues:
 *   Queue 2*i     = Receiveq i  (even index, device -> guest)
 *   Queue 2*i + 1 = Transmitq i (odd index,  guest -> device)
 *
 * 4 pairs is a good default: matches typical multi-queue NIC vCPU affinity
 * without excessive memory overhead. */
#define ROSV_NET_MAX_QUEUE_PAIRS        4

/* Total number of virtqueues = 2 * MaxPairs (no control VQ for now) */
#define VIRTIO_NET_NUM_QUEUES           (ROSV_NET_MAX_QUEUE_PAIRS * 2)

/* Legacy single-pair queue indices (pair 0) */
#define VIRTIO_NET_QUEUE_RX             0   /* Receiveq 0: device -> guest */
#define VIRTIO_NET_QUEUE_TX             1   /* Transmitq 0: guest -> device */

/* Per virtio spec: even queue indices are RX, odd are TX */
#define VIRTIO_NET_QUEUE_IS_RX(idx)     (((idx) & 1) == 0)
#define VIRTIO_NET_QUEUE_IS_TX(idx)     (((idx) & 1) != 0)
#define VIRTIO_NET_QUEUE_PAIR(idx)      ((idx) / 2)

/* ---- TX packet ring buffer (guest -> host) ------------------------------ */

/* Dynamic ring sizing bounds (all must be powers of two).
 * Rings start at INITIAL, grow up to MAX, shrink down to MIN. */
#define ROSV_VIRTIO_NET_RING_SIZE_MIN   256
#define ROSV_VIRTIO_NET_RING_SIZE_MAX   4096
#define ROSV_VIRTIO_NET_RING_SIZE_INIT  2048

C_ASSERT((ROSV_VIRTIO_NET_RING_SIZE_MIN & (ROSV_VIRTIO_NET_RING_SIZE_MIN - 1)) == 0);
C_ASSERT((ROSV_VIRTIO_NET_RING_SIZE_MAX & (ROSV_VIRTIO_NET_RING_SIZE_MAX - 1)) == 0);
C_ASSERT((ROSV_VIRTIO_NET_RING_SIZE_INIT & (ROSV_VIRTIO_NET_RING_SIZE_INIT - 1)) == 0);
C_ASSERT(ROSV_VIRTIO_NET_RING_SIZE_MIN <= ROSV_VIRTIO_NET_RING_SIZE_INIT);
C_ASSERT(ROSV_VIRTIO_NET_RING_SIZE_INIT <= ROSV_VIRTIO_NET_RING_SIZE_MAX);

/* Ring resize thresholds and timing */
#define ROSV_VIRTIO_NET_RING_GROW_PCT   75  /* Grow when occupancy exceeds 75% */
#define ROSV_VIRTIO_NET_RING_SHRINK_PCT 25  /* Shrink when occupancy stays below 25% */
#define ROSV_VIRTIO_NET_RING_GROW_CHECKS  3 /* Consecutive checks above grow threshold */
#define ROSV_VIRTIO_NET_RING_SHRINK_SECS 10 /* Seconds below shrink threshold to trigger */
#define ROSV_VIRTIO_NET_TX_RING_SHRINK_FLOOR ROSV_VIRTIO_NET_RING_SIZE_INIT

#define ROSV_VIRTIO_NET_MAX_PACKET      1600  /* Same as ROSV_NET_PACKET_MAX */
#define ROSV_VIRTIO_NET_TSO_MAX_PACKET  65536 /* Max TSO/GSO packet from guest (64KB) */

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

/* ---- Per queue-pair state ------------------------------------------------ */

/*
 * Each TX/RX queue pair has its own ring buffers and synchronization
 * primitives.  This allows independent, lock-free processing on different
 * vCPUs or worker threads.
 *
 * All queue pairs share the same host backend: packets from any TX queue
 * are funneled into the single TxRing for the host backend to dequeue,
 * and RX packets from the host are distributed to pair 0's RX ring
 * (future: RSS-style hashing across pairs).
 */
typedef struct _ROSV_VIRTIO_NET_QUEUE_PAIR {
    /* TX side: guest -> host.
     * Dynamically sized: starts at RING_SIZE_INIT, grows/shrinks
     * between RING_SIZE_MIN and RING_SIZE_MAX. Always power-of-2. */
    ROSV_VIRTIO_NET_TX_ENTRY *TxRing;   /* Dynamically allocated ring */
    ULONG TxRingCapacity;  /* Current allocated entry count (power-of-2) */
    ULONG TxRingHead;
    ULONG TxRingTail;
    ULONG TxRingCount;
    ROSV_VIRTIO_NET_TX_ENTRY *TxDrainRing; /* Previous ring draining after resize */
    ULONG TxDrainCapacity;
    ULONG TxDrainTail;
    ULONG TxDrainCount;
    BOOLEAN TxBackpressure;
    KSPIN_LOCK TxRingLock;
    FAST_MUTEX TxQueueMutex;

    /* RX side: host -> guest.
     * Dynamically sized: same policy as TxRing. */
    ROSV_VIRTIO_NET_TX_ENTRY *RxRing;   /* Dynamically allocated ring */
    ULONG RxRingCapacity;  /* Current allocated entry count (power-of-2) */
    ULONG RxRingHead;
    ULONG RxRingTail;
    ULONG RxRingCount;
    ROSV_VIRTIO_NET_TX_ENTRY *RxDrainRing; /* Previous ring draining after resize */
    ULONG RxDrainCapacity;
    ULONG RxDrainTail;
    ULONG RxDrainCount;
    KSPIN_LOCK RxLock;
    FAST_MUTEX RxQueueMutex;

    /* Per-pair statistics */
    ULONG64 TxPackets;
    ULONG64 TxBytes;
    ULONG64 RxPackets;
    ULONG64 RxBytes;
    ULONG   TxDropCount;
    ULONG   RxDropCount;
} ROSV_VIRTIO_NET_QUEUE_PAIR, *PROSV_VIRTIO_NET_QUEUE_PAIR;

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

    /* Queue selector and virtqueues.
     * Layout: Vq[0]=RX0, Vq[1]=TX0, Vq[2]=RX1, Vq[3]=TX1, ... */
    ULONG   NumQueuePairs;                          /* Active queue pairs (1..ROSV_NET_MAX_QUEUE_PAIRS) */
    ULONG   QueueSel;
    ROSV_VIRTQUEUE Vq[VIRTIO_NET_NUM_QUEUES];       /* All RX/TX virtqueues */

    /* Network device configuration */
    VIRTIO_NET_CONFIG Config;

    /* Per queue-pair state (TX/RX rings, locks, stats).
     * Pair 0 is always active; pairs 1..NumQueuePairs-1 are active only
     * when the guest negotiates VIRTIO_NET_F_MQ.
     * All rings are dynamically allocated and resizable. */
    ROSV_VIRTIO_NET_QUEUE_PAIR QueuePairs[ROSV_NET_MAX_QUEUE_PAIRS];

    /* Shared events (pair 0 -- kept here for API compatibility) */
    KEVENT TxReadyEvent;   /* Signaled when any TX ring receives a packet */
    KEVENT RxPendingEvent;  /* Signaled when host->guest packet injected */

    /* Dynamic ring resize state.
     * A periodic DPC (1-second interval) samples ring occupancy. If the
     * occupancy exceeds GROW_PCT for GROW_CHECKS consecutive ticks the
     * ring doubles (capped at RING_SIZE_MAX). If it stays below SHRINK_PCT
     * for SHRINK_SECS consecutive seconds the ring halves (floor at
     * RING_SIZE_MIN, except TX stays at least at the initial single-queue
     * size). Resize swaps in a fresh active ring under the respective spin
     * lock and drains the old ring asynchronously to avoid bulk copies in
     * the hot lock-held path. */
    KTIMER      RingResizeTimer;
    KDPC        RingResizeDpc;
    ULONG       TxGrowCounter;      /* Consecutive ticks above grow threshold */
    ULONG       TxShrinkCounter;    /* Consecutive ticks below shrink threshold */
    ULONG       RxGrowCounter;
    ULONG       RxShrinkCounter;
    BOOLEAN     RingResizeTimerActive;

    /* Ring watermark / resize statistics */
    ULONG       TxRingHighWatermark; /* Peak TxRingCount seen */
    ULONG       RxRingHighWatermark; /* Peak RxRingCount seen */
    ULONG       RingResizeCount;     /* Total grow + shrink operations */

    /* Interrupt coalescing (reduces VM-exit rate during bulk transfers) */
    ROSV_VIRTIO_NET_IRQ_COALESCE IrqCoalesce;

    /* Parent VM (for GPA translation and interrupt injection) */
    PROSV_VM OwnerVm;

    /* Aggregate statistics (sum of all pairs for quick access) */
    ULONG64 TxPackets;
    ULONG64 TxBytes;
    ULONG64 RxPackets;
    ULONG64 RxBytes;
    ULONG   TxDropCount;
    ULONG   RxDropCount;

    /* Pre-allocated TSO scratch buffers (avoid large stack allocations).
     * TsoGatherBuf holds the full GSO packet from the guest (up to 64KB).
     * TsoSegBuf holds one output segment during segmentation.
     * Protected by the per-pair TxQueueMutex. */
    PUCHAR TsoGatherBuf;    /* ROSV_VIRTIO_NET_TSO_MAX_PACKET bytes */
    PUCHAR TsoSegBuf;       /* ROSV_VIRTIO_NET_MAX_PACKET bytes */

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

/* Start / stop the periodic ring-resize timer (1-second interval).
 * Called from Initialize and Destroy respectively. */
VOID
RosvVirtioNetStartResizeTimer(
    _Inout_ PROSV_VIRTIO_NET_STATE State);

VOID
RosvVirtioNetStopResizeTimer(
    _Inout_ PROSV_VIRTIO_NET_STATE State);
