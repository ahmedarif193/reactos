/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Virtio-blk MMIO device emulation - structures and prototypes
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements virtio 1.0+ MMIO transport (virtio-v1.0-cs04, Section 4.2)
 * with a virtio-blk device (Section 5.2).
 */

#pragma once

#include <rosv/rosv.h>

/* ---- Virtio MMIO transport registers (Section 4.2.2) -------------------- */

/* Device register offsets from MMIO base address */
#define VIRTIO_MMIO_MAGIC_VALUE         0x000   /* R   - Magic value "virt" = 0x74726976 */
#define VIRTIO_MMIO_VERSION             0x004   /* R   - Device version (2 for modern) */
#define VIRTIO_MMIO_DEVICE_ID           0x008   /* R   - Virtio subsystem device ID */
#define VIRTIO_MMIO_VENDOR_ID           0x00C   /* R   - Virtio subsystem vendor ID */
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010   /* R   - Device features (selected page) */
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014   /* W   - Device features page selector */
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020   /* W   - Driver features (selected page) */
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024   /* W   - Driver features page selector */
#define VIRTIO_MMIO_QUEUE_SEL           0x030   /* W   - Queue index selector */
#define VIRTIO_MMIO_QUEUE_NUM_MAX       0x034   /* R   - Maximum queue size */
#define VIRTIO_MMIO_QUEUE_NUM           0x038   /* W   - Queue size (set by driver) */
#define VIRTIO_MMIO_QUEUE_READY         0x044   /* RW  - Queue ready bit */
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050   /* W   - Queue notification */
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060   /* R   - Interrupt status */
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064   /* W   - Interrupt acknowledge */
#define VIRTIO_MMIO_STATUS              0x070   /* RW  - Device status */
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080   /* W   - Descriptor table address low 32 */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084   /* W   - Descriptor table address high 32 */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090   /* W   - Available ring address low 32 */
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094   /* W   - Available ring address high 32 */
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0A0   /* W   - Used ring address low 32 */
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0A4   /* W   - Used ring address high 32 */
#define VIRTIO_MMIO_CONFIG_GENERATION   0x0FC   /* R   - Config space atomicity generation */
#define VIRTIO_MMIO_CONFIG_SPACE        0x100   /* RW  - Device-specific config starts here */

/* MMIO register space size (covers registers + config space) */
#define VIRTIO_MMIO_REG_SIZE            0x200

/* ---- Virtio MMIO magic and identity ------------------------------------- */

#define VIRTIO_MMIO_MAGIC               0x74726976  /* "virt" in little-endian */
#define VIRTIO_MMIO_VERSION_2           2           /* Modern (non-legacy) */

/* Device IDs (Section 5) */
#define VIRTIO_ID_BLOCK                 2

/* Vendor ID - use the standard QEMU vendor for Linux driver compatibility */
#define VIRTIO_VENDOR_ROSV              0x554D4551  /* "QEMU" - matches what Linux expects */

/* ---- Virtio device status bits (Section 2.1) ---------------------------- */

#define VIRTIO_STATUS_ACKNOWLEDGE       0x01
#define VIRTIO_STATUS_DRIVER            0x02
#define VIRTIO_STATUS_DRIVER_OK         0x04
#define VIRTIO_STATUS_FEATURES_OK       0x08
#define VIRTIO_STATUS_DEVICE_NEEDS_RESET 0x40
#define VIRTIO_STATUS_FAILED            0x80

/* ---- Virtio feature bits ------------------------------------------------ */

/* Common feature bits (Section 6) */
#define VIRTIO_F_VERSION_1              (1ULL << 32)  /* Virtio 1.0 compliance */
#define VIRTIO_F_ACCESS_PLATFORM        (1ULL << 33)

/* Block device feature bits (Section 5.2.3) */
#define VIRTIO_BLK_F_SIZE_MAX           (1ULL << 1)
#define VIRTIO_BLK_F_SEG_MAX            (1ULL << 2)
#define VIRTIO_BLK_F_GEOMETRY           (1ULL << 4)
#define VIRTIO_BLK_F_RO                 (1ULL << 5)   /* Read-only device */
#define VIRTIO_BLK_F_BLK_SIZE           (1ULL << 6)
#define VIRTIO_BLK_F_FLUSH              (1ULL << 9)
#define VIRTIO_BLK_F_TOPOLOGY           (1ULL << 10)
#define VIRTIO_BLK_F_MQ                 (1ULL << 12)

/* ---- Virtio interrupt status bits --------------------------------------- */

#define VIRTIO_INT_VRING                0x01    /* Used buffer notification */
#define VIRTIO_INT_CONFIG               0x02    /* Config change notification */

/* ---- Virtio split virtqueue structures (Section 2.6) -------------------- */

/* Descriptor flags */
#define VRING_DESC_F_NEXT               0x01    /* Descriptor continues via 'next' */
#define VRING_DESC_F_WRITE              0x02    /* Device writes (vs reads) */
#define VRING_DESC_F_INDIRECT           0x04    /* Buffer contains list of descriptors */

/* Available ring flags */
#define VRING_AVAIL_F_NO_INTERRUPT      0x01    /* Suppress interrupts */

/* Used ring flags */
#define VRING_USED_F_NO_NOTIFY          0x01    /* Suppress notifications */

#include <pshpack1.h>

/* Virtqueue descriptor (16 bytes) */
typedef struct _VRING_DESC {
    ULONG64 Addr;           /* Guest physical address of buffer */
    ULONG   Len;            /* Length of buffer in bytes */
    USHORT  Flags;          /* VRING_DESC_F_* */
    USHORT  Next;           /* Next descriptor index (if VRING_DESC_F_NEXT) */
} VRING_DESC, *PVRING_DESC;

C_ASSERT(sizeof(VRING_DESC) == 16);

/* Available ring header (4 bytes + entries) */
typedef struct _VRING_AVAIL {
    USHORT Flags;           /* VRING_AVAIL_F_* */
    USHORT Idx;             /* Next index to be used by driver */
    /* USHORT Ring[];        -- variable length array of descriptor indices */
} VRING_AVAIL, *PVRING_AVAIL;

/* Used ring element (8 bytes) */
typedef struct _VRING_USED_ELEM {
    ULONG Id;               /* Descriptor chain head index */
    ULONG Len;              /* Total bytes written by device */
} VRING_USED_ELEM, *PVRING_USED_ELEM;

/* Used ring header (4 bytes + elements) */
typedef struct _VRING_USED {
    USHORT Flags;           /* VRING_USED_F_* */
    USHORT Idx;             /* Next index to be used by device */
    /* VRING_USED_ELEM Ring[]; -- variable length array */
} VRING_USED, *PVRING_USED;

#include <poppack.h>

/* ---- Virtio-blk request header (Section 5.2.6) ------------------------- */

#define VIRTIO_BLK_T_IN                 0       /* Read from device */
#define VIRTIO_BLK_T_OUT                1       /* Write to device */
#define VIRTIO_BLK_T_FLUSH              4       /* Flush */
#define VIRTIO_BLK_T_GET_ID            8       /* Get device ID string */

/* Request status values */
#define VIRTIO_BLK_S_OK                 0
#define VIRTIO_BLK_S_IOERR             1
#define VIRTIO_BLK_S_UNSUPP            2

#include <pshpack1.h>

/* Block request header (16 bytes) */
typedef struct _VIRTIO_BLK_REQ_HDR {
    ULONG   Type;           /* VIRTIO_BLK_T_* */
    ULONG   Reserved;       /* Reserved / ioprio */
    ULONG64 Sector;         /* Start sector (512-byte units) */
} VIRTIO_BLK_REQ_HDR, *PVIRTIO_BLK_REQ_HDR;

C_ASSERT(sizeof(VIRTIO_BLK_REQ_HDR) == 16);

/* Block device configuration (Section 5.2.4)
 *
 * When VIRTIO_BLK_F_MQ is negotiated the config space includes a num_queues
 * field at offset 0x24 telling the driver how many request virtqueues are
 * available.  Each queue is independently selectable via QUEUE_SEL and can
 * be serviced by a separate I/O thread or vCPU.
 *
 * Layout (packed):
 *   0x00  ULONG64  Capacity
 *   0x08  ULONG    SizeMax
 *   0x0C  ULONG    SegMax
 *   0x10  Geometry (4 bytes)
 *   0x14  ULONG    BlkSize
 *   0x18  Topology (8 bytes -- we pad with zeros)
 *   0x20  UCHAR    Writeback
 *   0x21  UCHAR    Unused0
 *   0x22  USHORT   NumQueues    <-- VIRTIO_BLK_F_MQ
 */
typedef struct _VIRTIO_BLK_CONFIG {
    ULONG64 Capacity;       /* Disk size in 512-byte sectors */
    ULONG   SizeMax;        /* Max segment size (if VIRTIO_BLK_F_SIZE_MAX) */
    ULONG   SegMax;         /* Max number of segments (if VIRTIO_BLK_F_SEG_MAX) */
    /* Geometry (if VIRTIO_BLK_F_GEOMETRY) */
    struct {
        USHORT Cylinders;
        UCHAR  Heads;
        UCHAR  Sectors;
    } Geometry;
    ULONG   BlkSize;        /* Block size (if VIRTIO_BLK_F_BLK_SIZE) */
    /* Topology (if VIRTIO_BLK_F_TOPOLOGY) -- zeroed, present for correct
     * offset alignment of NumQueues per the virtio 1.0 spec. */
    struct {
        UCHAR  PhysicalBlockExp;
        UCHAR  AlignmentOffset;
        USHORT MinIoSize;
        ULONG  OptIoSize;
    } Topology;
    UCHAR   Writeback;      /* Write cache mode (0=writethrough) */
    UCHAR   Unused0;
    USHORT  NumQueues;      /* Number of request queues (if VIRTIO_BLK_F_MQ) */
} VIRTIO_BLK_CONFIG, *PVIRTIO_BLK_CONFIG;

#include <poppack.h>

/* ---- Virtqueue state ---------------------------------------------------- */

#define ROSV_VIRTIO_QUEUE_SIZE_MAX      256     /* Maximum virtqueue depth */

/* ---- Multi-queue configuration ------------------------------------------ */

/* Maximum number of request virtqueues for virtio-blk multi-queue (MQ).
 * Each queue is independently addressable via QUEUE_SEL / QUEUE_NOTIFY
 * and can be serviced by a dedicated worker thread.
 *
 * When the guest does NOT negotiate VIRTIO_BLK_F_MQ, only queue 0 is used
 * (single-queue legacy behaviour).  When MQ is negotiated, all NumQueues
 * queues become active.
 *
 * 4 queues is the sweet-spot for a single-disk VM: enough to saturate a
 * modern NVMe backend without excessive per-queue memory overhead. */
#define ROSV_BLK_NUM_QUEUES             4

typedef struct _ROSV_VIRTQUEUE {
    /* Queue configuration (set by driver during init) */
    ULONG   Num;                /* Queue size (number of descriptors) */
    BOOLEAN Ready;              /* Queue is ready for use */

    /* Guest physical addresses of ring components */
    ULONG64 DescGpa;            /* Descriptor table GPA */
    ULONG64 AvailGpa;           /* Available ring GPA */
    ULONG64 UsedGpa;            /* Used ring GPA */

    /* Cached host virtual addresses (pre-translated from GPA on QUEUE_READY).
     * Eliminates per-descriptor GPA->HVA translation overhead in hot paths. */
    PVOID   DescHva;            /* Cached HVA of descriptor table base */
    PVOID   AvailHva;           /* Cached HVA of available ring base */
    PVOID   UsedHva;            /* Cached HVA of used ring base */

    /* Device-side index tracking */
    USHORT  LastAvailIdx;       /* Last available index we consumed */
} ROSV_VIRTQUEUE, *PROSV_VIRTQUEUE;

/* ---- Disk I/O mode ------------------------------------------------------ */

typedef enum _ROSV_DISK_MODE {
    ROSV_DISK_MODE_RAMDISK      = 0,    /* Full memory mapping (current, for small images) */
    ROSV_DISK_MODE_DEMAND_PAGED = 1     /* ZwReadFile on demand (for large VHDX files) */
} ROSV_DISK_MODE;

/* ---- COW (Copy-On-Write) sector cache ----------------------------------- */
/* Guest writes go to in-memory sector cache instead of the backing file.
 * This allows the VHDX to reside on read-only media (e.g. LiveCD ISO)
 * while the guest sees a fully read-write block device.
 * Only dirty sectors are stored — typically a few MB for a systemd boot. */

#define ROSV_COW_HASH_BITS      12
#define ROSV_COW_HASH_SIZE      (1 << ROSV_COW_HASH_BITS)  /* 4096 buckets */
#define ROSV_COW_SECTOR_SIZE    512

typedef struct _ROSV_COW_ENTRY {
    ULONG64 SectorNumber;              /* Guest sector index */
    struct _ROSV_COW_ENTRY *Next;      /* Hash chain */
    UCHAR Data[ROSV_COW_SECTOR_SIZE];  /* Sector data */
} ROSV_COW_ENTRY, *PROSV_COW_ENTRY;

typedef struct _ROSV_COW_CACHE {
    PROSV_COW_ENTRY Buckets[ROSV_COW_HASH_SIZE];   /* Hash table */
    ULONG64 DirtyCount;                             /* Number of dirty sectors */
    ULONG64 DirtyBytes;                             /* Total bytes in cache */
} ROSV_COW_CACHE, *PROSV_COW_CACHE;

/* ---- Virtio-blk device state -------------------------------------------- */

/* Guest physical address for virtio MMIO region.
 * Placed below the PCI hole, above common device MMIO regions.
 * Must not overlap with LAPIC (0xFEE00000) or IOAPIC (0xFEC00000). */
#define ROSV_VIRTIO_BLK_MMIO_BASE      0xFEB00000ULL
#define ROSV_VIRTIO_BLK_MMIO_SIZE      0x1000ULL   /* 4KB page */

/* Virtual IRQ line for virtio-blk interrupt injection */
#define ROSV_VIRTIO_BLK_IRQ            5

/* Device ID string (20 bytes, VIRTIO_BLK_T_GET_ID) */
#define ROSV_VIRTIO_BLK_ID             "rosv-virtio-disk0"
#define ROSV_VIRTIO_BLK_ID_LEN         20

typedef struct _ROSV_VIRTIO_BLK_STATE {
    /* MMIO transport state */
    ULONG   DeviceFeaturesSelPage;      /* Selected features page (0 or 1) */
    ULONG   DriverFeaturesSelPage;      /* Selected driver features page */
    ULONG64 DeviceFeatures;             /* Features offered by device */
    ULONG64 DriverFeatures;             /* Features accepted by driver */
    ULONG   Status;                     /* Device status register */
    ULONG   InterruptStatus;            /* Pending interrupt flags */
    BOOLEAN InterruptPending;           /* Edge-triggered injection latch */
    ULONG   ConfigGeneration;           /* Config space change counter */

    /* Virtqueues — multi-queue (VIRTIO_BLK_F_MQ).
     *
     * NumQueues is set at init time:
     *   - Always ROSV_BLK_NUM_QUEUES in the device config space.
     *   - The guest may choose to use fewer; only queues with Ready==TRUE
     *     will be serviced on QUEUE_NOTIFY.
     *
     * QueueSel tracks the MMIO QUEUE_SEL register: all queue-specific
     * register accesses (QUEUE_NUM, QUEUE_READY, QUEUE_DESC_*, etc.)
     * operate on Vqs[QueueSel].
     */
    ULONG   NumQueues;                  /* Number of advertised request queues */
    ULONG   QueueSel;                   /* Currently selected queue index (MMIO QUEUE_SEL) */
    ROSV_VIRTQUEUE Vqs[ROSV_BLK_NUM_QUEUES]; /* Request queues [0..NumQueues-1] */

    /* Block device configuration */
    VIRTIO_BLK_CONFIG Config;

    /* Host-side backing storage */
    PVOID   DiskImageBase;              /* Host VA of memory-mapped disk/VHDX file (NULL for demand-paged) */
    ULONG64 DiskImageSize;              /* Size in bytes (file size for VHDX, image size for raw) */
    BOOLEAN ReadOnly;                   /* If TRUE, reject write requests */

    /* Disk backend type (raw flat image or VHDX) */
    ULONG   BackendType;               /* 0=ROSV_DISK_BACKEND_RAW, 1=ROSV_DISK_BACKEND_VHDX */

    /* Disk I/O mode */
    ROSV_DISK_MODE Mode;               /* RAMDISK (memory-mapped) or DEMAND_PAGED (ZwReadFile) */
    HANDLE  DiskFileHandle;             /* File handle for demand-paged I/O (NULL for ramdisk) */
    HANDLE  DiskFileHandleAlt;          /* Optional alternate handle for timeout failover */
    ULONG   PreferredReadHandle;        /* 0=DiskFileHandle, 1=DiskFileHandleAlt */
    HANDLE  DemandIoThreadHandle;       /* Dedicated thread for blocking demand-paged reads */
    PKTHREAD DemandIoThreadObject;      /* Referenced thread object for shutdown wait */
    KEVENT  DemandIoRequestEvent;       /* Signaled when a demand read request is ready */
    KEVENT  DemandIoCompleteEvent;      /* Signaled when a demand read request completes */
    FAST_MUTEX DemandIoLock;            /* Serializes request handoff state */
    LONG    DemandIoStop;               /* 1 => worker thread should exit */
    LONG    DemandIoRequestPending;     /* 1 while request is owned by worker thread */
    ULONG64 DemandIoSector;             /* Request: starting 512-byte sector */
    ULONG   DemandIoSectorCount;        /* Request: number of sectors */
    PVOID   DemandIoBuffer;             /* Request: output buffer */
    NTSTATUS DemandIoStatus;            /* Completion status from worker */

    /* Parent VM (for GPA translation and interrupt injection) */
    PROSV_VM OwnerVm;

    /* Statistics */
    ULONG64 ReadOps;
    ULONG64 ReadBytes;
    ULONG64 WriteOps;
    ULONG64 WriteBytes;

    /* COW (copy-on-write) sector cache for read-only backing images */
    ROSV_COW_CACHE CowCache;

    /* ---- Async request queue ------------------------------------------------
     *
     * Decouples virtqueue parsing (VM-exit path) from actual I/O processing.
     * The VM-exit handler enqueues parsed request metadata into this ring;
     * a dedicated worker thread dequeues entries, performs the I/O, pushes
     * used-ring completions, and injects a batch interrupt.
     *
     * When the async queue is full the VM-exit path falls back to inline
     * synchronous processing, so correctness is never lost.
     */
#define ROSV_BLK_ASYNC_QUEUE_SIZE 32

    struct {
        struct {
            ULONG DescIdx;      /* Head descriptor index (for used-ring push) */
            ULONG Type;         /* VIRTIO_BLK_T_IN / OUT / FLUSH / GET_ID */
            ULONG64 Sector;     /* Starting 512-byte sector from request header */
            ULONG DataLen;      /* Total data payload length (bytes) */
            ULONG64 DataGpa;    /* Guest physical address of data buffer */
            ULONG64 StatusGpa;  /* Guest physical address of status byte */
        } Entries[ROSV_BLK_ASYNC_QUEUE_SIZE];
        volatile LONG Head;     /* Producer index (VM-exit path writes) */
        volatile LONG Tail;     /* Consumer index (worker thread reads) */
        volatile LONG Count;    /* Number of entries currently queued */
        KEVENT WorkAvailable;   /* Signaled when new work is enqueued */
        KEVENT WorkComplete;    /* Signaled when worker drains a batch (for shutdown) */
        volatile LONG Running;  /* 1 while worker thread should keep running */
        HANDLE WorkerThread;    /* Worker thread handle */
        PKTHREAD WorkerThreadObject; /* Referenced thread object for shutdown wait */
    } AsyncQueue;
} ROSV_VIRTIO_BLK_STATE, *PROSV_VIRTIO_BLK_STATE;

/* ---- Function prototypes (device/virtio_blk.c) -------------------------- */

NTSTATUS
RosvVirtioBlkInitialize(
    _Out_ PROSV_VIRTIO_BLK_STATE State,
    _In_ PROSV_VM Vm,
    _In_opt_ PVOID DiskImageBase,
    _In_ ULONG64 DiskImageSize,
    _In_ BOOLEAN ReadOnly,
    _In_ ULONG BackendType);

VOID
RosvVirtioBlkDestroy(
    _Inout_ PROSV_VIRTIO_BLK_STATE State);

VOID
RosvVirtioBlkSetDiskImage(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ PVOID DiskImageBase,
    _In_ ULONG64 DiskImageSize,
    _In_ BOOLEAN ReadOnly);

BOOLEAN
RosvVirtioBlkMmioRead(
    _In_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _Out_ PULONG64 Value);

BOOLEAN
RosvVirtioBlkMmioWrite(
    _Inout_ PROSV_VIRTIO_BLK_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _In_ ULONG64 Value);

BOOLEAN
RosvVirtioBlkHasPendingInterrupt(
    _In_ PROSV_VIRTIO_BLK_STATE State);

VOID
RosvVirtioBlkClearPendingInterrupt(
    _Inout_ PROSV_VIRTIO_BLK_STATE State);

VOID
RosvVirtioBlkOnGuestEoi(
    _Inout_ PROSV_VIRTIO_BLK_STATE State);

/* ---- COW cache function prototypes (device/virtio_blk.c) ---------------- */

VOID
RosvCowInit(
    _Out_ PROSV_COW_CACHE Cache);

VOID
RosvCowDestroy(
    _Inout_ PROSV_COW_CACHE Cache);

PROSV_COW_ENTRY
RosvCowLookup(
    _In_ PROSV_COW_CACHE Cache,
    _In_ ULONG64 Sector);

NTSTATUS
RosvCowWrite(
    _Inout_ PROSV_COW_CACHE Cache,
    _In_ ULONG64 Sector,
    _In_ const VOID *Data);
