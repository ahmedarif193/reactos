/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Virtio-console MMIO device emulation - structures and prototypes
 * COPYRIGHT:   Copyright 2026 Ahmed Arif
 *
 * Implements virtio 1.0+ MMIO transport (virtio-v1.0-cs04, Section 4.2)
 * with a virtio-console device (Section 5.3) in multiport mode.
 *
 * The device provides multiple independent terminal ports, each with
 * its own TX/RX virtqueue pair.  This replaces the single-channel
 * UART+PTY approach and allows multiple rosl terminals to connect
 * to the same VM simultaneously, each on its own port.
 *
 * Queue layout (multiport):
 *   Queue 0:  port0 receiveq  (device -> guest, host writes here)
 *   Queue 1:  port0 transmitq (guest -> device, guest writes here)
 *   Queue 2:  control receiveq  (device -> guest control messages)
 *   Queue 3:  control transmitq (guest -> device control messages)
 *   Queue 4:  port1 receiveq
 *   Queue 5:  port1 transmitq
 *   Queue 6:  port2 receiveq
 *   Queue 7:  port2 transmitq
 *   ...
 *
 * Port 0 is the primary console (hvc0), additional ports are hvc1, hvc2, etc.
 */

#pragma once

#include <rosv/rosv.h>
#include <rosv/virtio_blk.h>  /* Shared virtio MMIO register defs, virtqueue structs */

/* ---- Virtio-console device identity ------------------------------------- */

#define VIRTIO_ID_CONSOLE               3

/* ---- Virtio-console feature bits (Section 5.3.3) ------------------------ */

#define VIRTIO_CONSOLE_F_SIZE           (1ULL << 0)   /* Config has cols/rows */
#define VIRTIO_CONSOLE_F_MULTIPORT      (1ULL << 1)   /* Multiple ports supported */
#define VIRTIO_CONSOLE_F_EMERG_WRITE    (1ULL << 2)   /* Emergency write supported */

/* ---- Virtio-console config space (Section 5.3.4) ------------------------ */

#include <pshpack1.h>

typedef struct _VIRTIO_CONSOLE_CONFIG {
    USHORT Cols;                /* Console width (if VIRTIO_CONSOLE_F_SIZE) */
    USHORT Rows;                /* Console height (if VIRTIO_CONSOLE_F_SIZE) */
    ULONG  MaxNrPorts;          /* Max ports (if VIRTIO_CONSOLE_F_MULTIPORT) */
    ULONG  EmergWr;             /* Emergency write (if VIRTIO_CONSOLE_F_EMERG_WRITE) */
} VIRTIO_CONSOLE_CONFIG, *PVIRTIO_CONSOLE_CONFIG;

/* ---- Virtio-console control message (Section 5.3.6.1) ------------------- */

typedef struct _VIRTIO_CONSOLE_CONTROL {
    ULONG  Id;                  /* Port number */
    USHORT Event;               /* VIRTIO_CONSOLE_* event code */
    USHORT Value;               /* Event-specific value */
} VIRTIO_CONSOLE_CONTROL, *PVIRTIO_CONSOLE_CONTROL;

#include <poppack.h>

C_ASSERT(sizeof(VIRTIO_CONSOLE_CONTROL) == 8);

/* Control message event codes (matches Linux uapi/linux/virtio_console.h) */
#define VIRTIO_CONSOLE_DEVICE_READY     0   /* Driver -> device: driver is ready */
#define VIRTIO_CONSOLE_PORT_ADD         1   /* Device -> driver: new port available */
#define VIRTIO_CONSOLE_PORT_REMOVE      2   /* Device -> driver: port removed */
#define VIRTIO_CONSOLE_PORT_READY       3   /* Driver -> device: port driver ready */
#define VIRTIO_CONSOLE_CONSOLE_PORT     4   /* Device -> driver: port is a console */
#define VIRTIO_CONSOLE_RESIZE           5   /* Device -> driver: terminal resize */
#define VIRTIO_CONSOLE_PORT_OPEN        6   /* Bidirectional: port open/close */
#define VIRTIO_CONSOLE_PORT_NAME        7   /* Device -> driver: port name */

/* ---- MMIO addresses and IRQ --------------------------------------------- */

/* Placed at 0xFEB02000, after virtio-blk (0xFEB00000) and virtio-net (0xFEB01000) */
#define ROSV_VIRTIO_CON_MMIO_BASE       0xFEB02000ULL
#define ROSV_VIRTIO_CON_MMIO_SIZE       0x1000ULL   /* 4KB page */

/* Virtual IRQ line for virtio-console interrupt injection */
#define ROSV_VIRTIO_CON_IRQ             7

/* ---- Port and queue configuration --------------------------------------- */

#define ROSV_VIRTIO_CON_MAX_PORTS       4       /* Maximum number of console ports */
#define ROSV_VIRTIO_CON_QUEUE_SIZE      64      /* Virtqueue depth per queue */

/* Queue indices for a given port */
#define VIRTIO_CON_QUEUE_RX(port)       ((port) == 0 ? 0 : (2 + (port) * 2))
#define VIRTIO_CON_QUEUE_TX(port)       ((port) == 0 ? 1 : (3 + (port) * 2))
#define VIRTIO_CON_QUEUE_CTRL_RX        2   /* Control receiveq */
#define VIRTIO_CON_QUEUE_CTRL_TX        3   /* Control transmitq */

/* Total number of queues: 2 (port0) + 2 (ctrl) + 2 * (MAX_PORTS - 1) */
#define ROSV_VIRTIO_CON_NUM_QUEUES      (2 + 2 + 2 * (ROSV_VIRTIO_CON_MAX_PORTS - 1))

/* ---- Per-port host-side buffers ----------------------------------------- */

#define ROSV_VIRTIO_CON_PORT_BUF_SIZE   16384   /* Ring buffer for host<->guest data */

typedef struct _ROSV_VIRTIO_CON_PORT {
    BOOLEAN Active;             /* Port has been added (DEVICE_ADD sent) */
    BOOLEAN Open;               /* Port is open (PORT_OPEN received from guest) */
    BOOLEAN IsConsole;          /* Port 0 is marked as console port */
    BOOLEAN Allocated;          /* Port is allocated to a PTY session */

    /* Back-pointer to bound PTY (NULL if not bound to any PTY) */
    struct _ROSV_PTY_STATE *BoundPty;

    /* Host -> guest data buffer (written by host, read by device into RX vq) */
    UCHAR   TxBuf[ROSV_VIRTIO_CON_PORT_BUF_SIZE];
    ULONG   TxHead;            /* Producer index */
    ULONG   TxTail;            /* Consumer index */
    ULONG   TxCount;           /* Bytes buffered */
    KSPIN_LOCK TxLock;

    /* Guest -> host data buffer (written by guest via TX vq, read by host) */
    UCHAR   RxBuf[ROSV_VIRTIO_CON_PORT_BUF_SIZE];
    ULONG   RxHead;            /* Producer index */
    ULONG   RxTail;            /* Consumer index */
    ULONG   RxCount;           /* Bytes buffered */
    KSPIN_LOCK RxLock;
    volatile LONG TxProcessing; /* Guards recursive TX queue processing */

    /* Statistics */
    ULONG64 BytesIn;           /* Total bytes host -> guest */
    ULONG64 BytesOut;          /* Total bytes guest -> host */
} ROSV_VIRTIO_CON_PORT, *PROSV_VIRTIO_CON_PORT;

/* ---- Virtio-console device state ---------------------------------------- */

typedef struct _ROSV_VIRTIO_CON_STATE {
    /* MMIO transport state (same register model as virtio-blk/net) */
    ULONG   DeviceFeaturesSelPage;
    ULONG   DriverFeaturesSelPage;
    ULONG64 DeviceFeatures;
    ULONG64 DriverFeatures;
    ULONG   Status;
    ULONG   InterruptStatus;
    BOOLEAN InterruptPending;
    ULONG   ConfigGeneration;

    /* Queue selector and virtqueues */
    ULONG   QueueSel;
    ROSV_VIRTQUEUE Vq[ROSV_VIRTIO_CON_NUM_QUEUES];

    /* Console configuration */
    VIRTIO_CONSOLE_CONFIG Config;

    /* Per-port state */
    ROSV_VIRTIO_CON_PORT Ports[ROSV_VIRTIO_CON_MAX_PORTS];
    ULONG ActivePortCount;

    /* Control message queue: pending messages to send to guest */
    VIRTIO_CONSOLE_CONTROL CtrlPending[16];
    ULONG CtrlPendingHead;
    ULONG CtrlPendingTail;
    ULONG CtrlPendingCount;

    /* Driver readiness tracking */
    BOOLEAN DriverReady;        /* Guest driver sent DEVICE_READY */

    /* Parent VM (for GPA translation and interrupt injection) */
    PROSV_VM OwnerVm;

    /* Statistics */
    ULONG64 TotalBytesIn;
    ULONG64 TotalBytesOut;
} ROSV_VIRTIO_CON_STATE, *PROSV_VIRTIO_CON_STATE;

/* ---- Function prototypes (device/virtio_console.c) ---------------------- */

NTSTATUS
RosvVirtioConInitialize(
    _Out_ PROSV_VIRTIO_CON_STATE State,
    _In_ PROSV_VM Vm,
    _In_ ULONG NumPorts);

VOID
RosvVirtioConDestroy(
    _Inout_ PROSV_VIRTIO_CON_STATE State);

BOOLEAN
RosvVirtioConMmioRead(
    _In_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _Out_ PULONG64 Value);

BOOLEAN
RosvVirtioConMmioWrite(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG64 GuestPhysicalAddress,
    _In_ ULONG Size,
    _In_ ULONG64 Value);

BOOLEAN
RosvVirtioConHasPendingInterrupt(
    _In_ PROSV_VIRTIO_CON_STATE State);

VOID
RosvVirtioConClearPendingInterrupt(
    _Inout_ PROSV_VIRTIO_CON_STATE State);

/* Host writes data to a port (host -> guest via RX virtqueue) */
NTSTATUS
RosvVirtioConPortWrite(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_ PULONG BytesWritten);

/* Host reads data from a port (guest -> host via TX virtqueue) */
NTSTATUS
RosvVirtioConPortRead(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex,
    _Out_writes_bytes_(BufferSize) UCHAR *Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesRead);

ULONG
RosvVirtioConPortQueryTxSpace(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex);

NTSTATUS
RosvVirtioConPortUnread(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _Out_opt_ PULONG BytesUnwritten);

VOID
RosvVirtioConKickTx(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex);

/* Flush pending host->guest data into RX virtqueue descriptors */
VOID
RosvVirtioConFlushPortTx(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex);

/* Allocate a free vcon port for PTY binding (returns (ULONG)-1 if none free) */
ULONG
RosvVirtioConAllocatePort(
    _Inout_ PROSV_VIRTIO_CON_STATE State);

/* Release a previously allocated vcon port */
VOID
RosvVirtioConReleasePort(
    _Inout_ PROSV_VIRTIO_CON_STATE State,
    _In_ ULONG PortIndex);
