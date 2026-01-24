/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x Main header file
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This is the new NDIS 6.x header file for the E1000 driver.
 * It replaces the legacy nic.h for NDIS 6.30+ builds.
 */

#pragma once

/*
 * NDIS version configuration:
 * - NDIS620_MINIPORT and NDIS_MINIPORT_DRIVER are defined via CMakeLists.txt
 * - ReactOS ndis.h supports up to NDIS 6.20; the driver is designed for 6.30+
 * - Version macros are set by the build system to avoid conflicts with ndis.h
 */

#include <ndis.h>
#include <ntintsafe.h>
#include <wdmguid.h>  /* For GUID_BUS_INTERFACE_STANDARD */

/* Include compatibility layer for missing NDIS 6.x APIs */
#include "ndis6_compat.h"

/* Include hardware definitions */
#include "e1000hw.h"

/* Include debug support */
#include "debug.h"

/* ============================================================================
 * Driver Version Information
 * ============================================================================ */

#define DRIVER_VERSION         0x0200       /* 2.00 - NDIS 6.x version */
#define DRIVER_VENDOR_ID       0x00FFFFFF   /* 3-byte vendor code from MAC */

/* ============================================================================
 * Hardware Limits and Configuration
 * ============================================================================ */

#define HW_VENDOR_INTEL        0x8086

/* Ring sizes - power of 2 for hardware requirements */
#define E1000_NUM_TX_DESC          256
#define E1000_NUM_RX_DESC          256
#define E1000_MAX_TX_QUEUES        2
#define E1000_MAX_RX_QUEUES        2
#define E1000_MAX_MSIX_VECTORS     5   /* 82574L supports 5 MSI-X vectors */

/* Buffer sizes */
#define E1000_RX_BUFFER_SIZE       2048
#define E1000_MAX_FRAME_SIZE       1522    /* ETH_FRAME_LEN + VLAN tag */
#define E1000_MIN_FRAME_SIZE       60
#define E1000_MAX_JUMBO_FRAME_SIZE 9014

/* Multicast list */
#define E1000_MAX_MULTICAST        32

/* Link speeds (in bits per second for NDIS 6.x) */
#define E1000_LINK_SPEED_10MBPS    10000000ULL       /* 10 Mbps = 10,000,000 bps */
#define E1000_LINK_SPEED_100MBPS   100000000ULL      /* 100 Mbps = 100,000,000 bps */
#define E1000_LINK_SPEED_1GBPS     1000000000ULL     /* 1 Gbps = 1,000,000,000 bps */

/* ============================================================================
 * NDIS 6.x Object IDs for Registration
 * ============================================================================ */

#define E1000_NDIS_OBJECT_TYPE_MINIPORT_INIT_PARAMETERS     0x80
#define E1000_NDIS_OBJECT_TYPE_MINIPORT_ADD_DEVICE_PARAMS   0x81

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

typedef struct _E1000_ADAPTER E1000_ADAPTER, *PE1000_ADAPTER;
typedef struct _E1000_TX_QUEUE E1000_TX_QUEUE, *PE1000_TX_QUEUE;
typedef struct _E1000_RX_QUEUE E1000_RX_QUEUE, *PE1000_RX_QUEUE;
typedef struct _E1000_TX_BUFFER E1000_TX_BUFFER, *PE1000_TX_BUFFER;
typedef struct _E1000_RX_BUFFER E1000_RX_BUFFER, *PE1000_RX_BUFFER;

/* ============================================================================
 * Transmit Buffer Structure
 *
 * Tracks individual transmit buffers for completion handling
 * ============================================================================ */

typedef struct _E1000_TX_BUFFER
{
    /* The NET_BUFFER_LIST this buffer belongs to */
    PNET_BUFFER_LIST    NetBufferList;

    /* The NET_BUFFER within the NBL */
    PNET_BUFFER         NetBuffer;

    /* Physical address of the buffer for DMA */
    NDIS_PHYSICAL_ADDRESS PhysicalAddress;

    /* Length of data in this buffer */
    ULONG               Length;

    /* Scatter-gather list (if used) */
    PSCATTER_GATHER_LIST SgList;

    /* MDL for the data */
    PMDL                Mdl;

    /* Virtual address from mapping */
    PVOID               VirtualAddress;

    /* Flags */
    ULONG               Flags;
#define E1000_TX_BUFFER_IN_USE       0x00000001
#define E1000_TX_BUFFER_FIRST_SEG    0x00000002
#define E1000_TX_BUFFER_LAST_SEG     0x00000004
#define E1000_TX_BUFFER_CONTEXT_DESC 0x00000008

} E1000_TX_BUFFER, *PE1000_TX_BUFFER;

/* ============================================================================
 * Receive Buffer Structure
 *
 * Pre-allocated receive buffers with NBLs
 * ============================================================================ */

typedef struct _E1000_RX_BUFFER
{
    /* Pre-allocated NET_BUFFER_LIST for this receive buffer */
    PNET_BUFFER_LIST    NetBufferList;

    /* MDL describing the receive buffer */
    PMDL                Mdl;

    /* Virtual address of the receive buffer */
    PUCHAR              VirtualAddress;

    /* Physical address for DMA */
    NDIS_PHYSICAL_ADDRESS PhysicalAddress;

    /* Length of allocated buffer */
    ULONG               BufferLength;

    /* Flags */
    ULONG               Flags;
#define E1000_RX_BUFFER_IN_USE       0x00000001
#define E1000_RX_BUFFER_RETURNED     0x00000002

} E1000_RX_BUFFER, *PE1000_RX_BUFFER;

/* ============================================================================
 * Transmit Queue Structure
 *
 * Manages a single transmit queue (82574L supports 2 TX queues)
 *
 * Structure is aligned to cache line boundary (64 bytes) for performance.
 * This prevents false sharing when TX and RX queues are accessed from
 * different CPUs, which is common in MSI-X configurations.
 * ============================================================================ */

/* Cache line size for alignment - typical x86/x64 value */
#define E1000_CACHE_LINE_SIZE       64

/* TX Queue Flags */
#define E1000_TX_QUEUE_STOPPED      0x00000001  /* Queue stopped due to no descriptors */

/* TX queue wake threshold - wake queue when this many descriptors available */
#define E1000_TX_WAKE_THRESHOLD     32

typedef struct DECLSPEC_ALIGN(E1000_CACHE_LINE_SIZE) _E1000_TX_QUEUE
{
    /* Back-pointer to adapter */
    PE1000_ADAPTER          Adapter;

    /* Queue index (0 or 1 for 82574L) */
    ULONG                   QueueIndex;

    /* Descriptor ring */
    volatile PE1000_TRANSMIT_DESCRIPTOR Descriptors;
    NDIS_PHYSICAL_ADDRESS   DescriptorsPa;

    /* TX buffer tracking array */
    PE1000_TX_BUFFER        Buffers;

    /* Ring indices */
    ULONG                   Head;           /* Next descriptor to reclaim (completed) */
    ULONG                   Tail;           /* Next descriptor to use (submit) */
    ULONG                   Count;          /* Total descriptors in ring */

    /* Available descriptor count */
    LONG                    Available;

    /* Spin lock for queue access */
    NDIS_SPIN_LOCK          Lock;

    /* DPC for TX completion processing */
    KDPC                    CompletionDpc;

    /* Statistics */
    ULONG64                 PacketsSent;
    ULONG64                 BytesSent;
    ULONG64                 SendErrors;

    /* Interrupt vector for this queue (MSI-X) */
    ULONG                   InterruptVector;

    /* Watchdog timer for stuck TX detection */
    LARGE_INTEGER           LastCompletionTime;
    BOOLEAN                 Watchdog;

    /* TX Queue Flow Control */
    volatile LONG           Flags;          /* E1000_TX_QUEUE_STOPPED, etc. */

} E1000_TX_QUEUE, *PE1000_TX_QUEUE;

/* ============================================================================
 * Receive Queue Structure
 *
 * Manages a single receive queue (82574L supports 2 RX queues)
 *
 * Structure is aligned to cache line boundary (64 bytes) for performance.
 * This prevents false sharing when TX and RX queues are accessed from
 * different CPUs, which is common in MSI-X configurations.
 * ============================================================================ */

/* RX buffer write threshold - batch RDT updates until this many cleaned */
#define E1000_RX_BUFFER_WRITE       16

/* Default RX budget per DPC - process up to this many packets per interrupt */
#define E1000_RX_DEFAULT_BUDGET     64

typedef struct DECLSPEC_ALIGN(E1000_CACHE_LINE_SIZE) _E1000_RX_QUEUE
{
    /* Back-pointer to adapter */
    PE1000_ADAPTER          Adapter;

    /* Queue index (0 or 1 for 82574L) */
    ULONG                   QueueIndex;

    /* Descriptor ring */
    volatile PE1000_RECEIVE_DESCRIPTOR Descriptors;
    NDIS_PHYSICAL_ADDRESS   DescriptorsPa;

    /* RX buffer pool */
    PE1000_RX_BUFFER        Buffers;

    /* Ring indices */
    ULONG                   Head;           /* Next descriptor to check */
    ULONG                   Tail;           /* Last descriptor given to hardware */
    ULONG                   Count;          /* Total descriptors in ring */

    /* NET_BUFFER_LIST pool for this queue */
    NDIS_HANDLE             NblPool;

    /* Lookaside list for RX buffers */
    NPAGED_LOOKASIDE_LIST   BufferLookaside;

    /* Spin lock for queue access */
    NDIS_SPIN_LOCK          Lock;

    /* DPC for RX indication */
    KDPC                    IndicateDpc;

    /* Statistics */
    ULONG64                 PacketsReceived;
    ULONG64                 BytesReceived;
    ULONG64                 ReceiveErrors;
    ULONG64                 DroppedPackets;

    /* Interrupt vector for this queue (MSI-X) */
    ULONG                   InterruptVector;

    /* Batched RDT update tracking */
    ULONG                   CleanedCount;   /* Buffers cleaned since last RDT update */

} E1000_RX_QUEUE, *PE1000_RX_QUEUE;

/* ============================================================================
 * MSI-X Interrupt Information
 * ============================================================================ */

typedef struct _E1000_MSIX_INFO
{
    /* MSI-X message info from IoConnectInterruptEx */
    IO_INTERRUPT_MESSAGE_INFO   *MessageInfo;

    /* Interrupt objects for each vector */
    PKINTERRUPT             InterruptObject[E1000_MAX_MSIX_VECTORS];

    /* DPCs for each vector */
    KDPC                    Dpc[E1000_MAX_MSIX_VECTORS];

    /* Number of allocated MSI-X vectors */
    ULONG                   VectorCount;

    /* Message IDs */
    ULONG                   MessageIds[E1000_MAX_MSIX_VECTORS];

    /* Interrupt affinity */
    KAFFINITY               Affinity[E1000_MAX_MSIX_VECTORS];

} E1000_MSIX_INFO, *PE1000_MSIX_INFO;

/* ============================================================================
 * Checksum Offload Configuration
 * ============================================================================ */

typedef struct _E1000_CHECKSUM_OFFLOAD
{
    /* TX offload capabilities enabled */
    BOOLEAN                 TxIpChecksumEnabled;
    BOOLEAN                 TxTcpChecksumEnabled;
    BOOLEAN                 TxUdpChecksumEnabled;

    /* RX offload capabilities enabled */
    BOOLEAN                 RxIpChecksumEnabled;
    BOOLEAN                 RxTcpChecksumEnabled;
    BOOLEAN                 RxUdpChecksumEnabled;

} E1000_CHECKSUM_OFFLOAD, *PE1000_CHECKSUM_OFFLOAD;

/* ============================================================================
 * Receive Side Scaling (RSS) Configuration
 * ============================================================================ */

typedef struct _E1000_RSS_CONFIG
{
    /* RSS enabled flag */
    BOOLEAN                 Enabled;

    /* Number of RSS queues */
    ULONG                   QueueCount;

    /* Hash types enabled */
    ULONG                   HashTypes;

    /* Hash function */
    ULONG                   HashFunction;

    /* Secret key for hash calculation */
    UCHAR                   HashKey[E1000_RSSRK_SIZE * sizeof(ULONG)];

    /* Indirection table */
    UCHAR                   IndirectionTable[E1000_RETA_SIZE * sizeof(ULONG)];

    /* Base CPU number */
    ULONG                   BaseCpuNumber;

    /* Processor count */
    USHORT                  ProcessorCount;

    /* Processor masks */
    PPROCESSOR_NUMBER       ProcessorMasks;

} E1000_RSS_CONFIG, *PE1000_RSS_CONFIG;

/* ============================================================================
 * Interrupt Mode Enumeration
 * ============================================================================ */

typedef enum _E1000_INTERRUPT_MODE
{
    E1000InterruptModeLegacy,   /* Line-based interrupts */
    E1000InterruptModeMsi,      /* Message Signaled Interrupts */
    E1000InterruptModeMsix      /* MSI-X with per-queue vectors */
} E1000_INTERRUPT_MODE;

/* ============================================================================
 * Power State for NDIS 6.x
 * ============================================================================ */

typedef enum _E1000_POWER_STATE
{
    E1000PowerStateD0 = 0,      /* Fully operational */
    E1000PowerStateD1,          /* Light sleep */
    E1000PowerStateD2,          /* Deep sleep */
    E1000PowerStateD3           /* Off (wake-on-LAN possible) */
} E1000_POWER_STATE;

/* ============================================================================
 * Statistics Structure for NDIS 6.x
 * ============================================================================ */

typedef struct _E1000_STATISTICS
{
    /* General statistics (aligned for atomic access) */
    ULONG64 DECLSPEC_ALIGN(8) TxPackets;
    ULONG64 DECLSPEC_ALIGN(8) RxPackets;
    ULONG64 DECLSPEC_ALIGN(8) TxBytes;
    ULONG64 DECLSPEC_ALIGN(8) RxBytes;
    ULONG64 DECLSPEC_ALIGN(8) TxErrors;
    ULONG64 DECLSPEC_ALIGN(8) RxErrors;
    ULONG64 DECLSPEC_ALIGN(8) RxNoBuffer;

    /* Detailed error counters */
    ULONG64 TxAbortedErrors;
    ULONG64 TxCarrierErrors;
    ULONG64 TxWindowErrors;
    ULONG64 RxCrcErrors;
    ULONG64 RxFrameErrors;
    ULONG64 RxFifoErrors;
    ULONG64 RxMissedErrors;
    ULONG64 RxLengthErrors;

    /* Multicast/broadcast */
    ULONG64 RxMulticast;
    ULONG64 TxMulticast;
    ULONG64 RxBroadcast;
    ULONG64 TxBroadcast;

} E1000_STATISTICS, *PE1000_STATISTICS;

/* ============================================================================
 * Device Serial Number Structure (for PCIe devices)
 * ============================================================================ */

typedef struct _E1000_DEVICE_SERIAL_NUMBER {
    BOOLEAN Valid;
    UCHAR Serial[8];            /* 64-bit serial number */
} E1000_DEVICE_SERIAL_NUMBER, *PE1000_DEVICE_SERIAL_NUMBER;

/* ============================================================================
 * Main Adapter Structure for NDIS 6.x
 *
 * This is the primary context structure for the E1000 NDIS 6.x miniport.
 * ============================================================================ */

typedef struct _E1000_ADAPTER
{
    /* ========== NDIS Handles ========== */

    /* Handle from NdisMRegisterMiniportDriver */
    NDIS_HANDLE             NdisMiniportDriverHandle;

    /* Handle from MiniportInitializeEx */
    NDIS_HANDLE             MiniportAdapterHandle;

    /* Interrupt handle from NdisMRegisterInterruptEx */
    NDIS_HANDLE             InterruptHandle;

    /* ========== PCI Bus Interface for NDIS 6.x ========== */

    /* Physical Device Object - needed for PCI config space access */
    PDEVICE_OBJECT          PhysicalDeviceObject;

    /* Bus interface for PCI config space reads/writes */
    BUS_INTERFACE_STANDARD  BusInterface;
    BOOLEAN                 BusInterfaceValid;

    /* ========== Device Identification ========== */

    USHORT                  VendorId;
    USHORT                  DeviceId;
    USHORT                  SubsystemVendorId;
    USHORT                  SubsystemId;
    UCHAR                   RevisionId;

    /* Device type flags */
    BOOLEAN                 IsPCIe;             /* PCIe vs PCI device */
    BOOLEAN                 HasFlash;           /* Has flash NVM */

    /* ========== Hardware Resources ========== */

    /* Memory-mapped I/O base */
    PUCHAR                  IoBase;
    NDIS_PHYSICAL_ADDRESS   IoAddress;
    ULONG                   IoLength;

    /* I/O port access (for register writes during reset) */
    PUCHAR                  IoPort;
    ULONG                   IoPortAddress;
    ULONG                   IoPortLength;

    /* MSI-X BAR mapping */
    PUCHAR                  MsixTableBase;
    NDIS_PHYSICAL_ADDRESS   MsixAddress;
    ULONG                   MsixLength;

    /* ========== MAC Address ========== */

    UCHAR                   PermanentMacAddress[ETH_LENGTH_OF_ADDRESS];
    UCHAR                   CurrentMacAddress[ETH_LENGTH_OF_ADDRESS];

    /* ========== Interrupt Configuration ========== */

    E1000_INTERRUPT_MODE    InterruptMode;
    E1000_MSIX_INFO         MsixInfo;

    /* Legacy interrupt info */
    ULONG                   InterruptVector;
    ULONG                   InterruptLevel;
    ULONG                   InterruptAffinity;
    KINTERRUPT_MODE         InterruptModeType;
    BOOLEAN                 InterruptShared;
    BOOLEAN                 HasMessageInterrupt;  /* TRUE if MSI/MSI-X resource detected */

    /* Interrupt mask register value */
    ULONG                   InterruptMask;

    /* Pending interrupt status (for DPC processing) */
    ULONG                   InterruptPending;

    /* ========== Transmit Queues ========== */

    E1000_TX_QUEUE          TxQueues[E1000_MAX_TX_QUEUES];
    ULONG                   TxQueueCount;

    /* Pending send NBL list */
    PNET_BUFFER_LIST        PendingSendHead;
    PNET_BUFFER_LIST        PendingSendTail;
    NDIS_SPIN_LOCK          SendLock;

    /* ========== Receive Queues ========== */

    E1000_RX_QUEUE          RxQueues[E1000_MAX_RX_QUEUES];
    ULONG                   RxQueueCount;

    /* NET_BUFFER_LIST pool for receives */
    NDIS_HANDLE             RxNblPool;

    /* ========== Offload Configuration ========== */

    E1000_CHECKSUM_OFFLOAD  ChecksumOffload;
    E1000_RSS_CONFIG        RssConfig;

    /* ========== Link State ========== */

    NDIS_MEDIA_CONNECT_STATE MediaState;
    ULONG64                 LinkSpeed;          /* In 100 bps units */
    BOOLEAN                 FullDuplex;
    BOOLEAN                 AutoNegotiate;

    /* ========== Packet Filter ========== */

    ULONG                   PacketFilter;

    /* Multicast list */
    UCHAR                   MulticastList[E1000_MAX_MULTICAST][ETH_LENGTH_OF_ADDRESS];
    ULONG                   MulticastCount;

    /* ========== Power Management ========== */

    E1000_POWER_STATE       CurrentPowerState;
    DEVICE_POWER_STATE      NdisPowerState;
    BOOLEAN                 WakeOnMagicPacket;
    BOOLEAN                 WakeOnPattern;
    BOOLEAN                 WakeOnLinkChange;

    /* ========== Statistics ========== */

    E1000_STATISTICS        Statistics;

    /* ========== Device Serial Number ========== */

    E1000_DEVICE_SERIAL_NUMBER DeviceSerialNumber;

    /* ========== State Flags ========== */

    LONG                    Flags;
#define E1000_FLAG_ADAPTER_STARTED      0x00000001
#define E1000_FLAG_ADAPTER_PAUSED       0x00000002
#define E1000_FLAG_ADAPTER_PAUSING      0x00000004
#define E1000_FLAG_ADAPTER_HALTING      0x00000008
#define E1000_FLAG_ADAPTER_RESET        0x00000010
#define E1000_FLAG_LINK_UP              0x00000020
#define E1000_FLAG_INTERRUPT_ENABLED    0x00000040

    /* Reset spin lock */
    NDIS_SPIN_LOCK          ResetLock;

    /* ========== DMA Allocator ========== */

    NDIS_HANDLE             DmaAdapterHandle;
    NDIS_SG_DMA_DESCRIPTION SgDmaDescription;

    /* ========== OID Request ========== */

    PNDIS_OID_REQUEST       PendingOidRequest;

    /* ========== Lookahead size ========== */
    ULONG                   LookaheadSize;

    /* ========== Adaptive Interrupt Throttle Rate (ITR) ========== */

    /*
     * Traffic tracking for adaptive ITR adjustment.
     * Updated during DPC processing to measure traffic patterns.
     */
    ULONG64                 TotalRxPackets;     /* Packets received in current interval */
    ULONG64                 TotalTxPackets;     /* Packets transmitted in current interval */
    ULONG64                 TotalRxBytes;       /* Bytes received in current interval */
    ULONG64                 TotalTxBytes;       /* Bytes transmitted in current interval */

    /*
     * Current ITR configuration.
     * Uses Linux e1000e-style adaptive interrupt moderation.
     * ITR values are in 256ns units for the hardware register.
     *
     * ItrSetting modes:
     *   0=lowest_latency: ITR 5000 (~780 int/sec) - responsive for small packets
     *   1=low_latency:    ITR 10000 (~390 int/sec) - balanced
     *   2=bulk_latency:   ITR 20000 (~195 int/sec) - throughput optimized
     *   3=dynamic:        Auto-adjust based on traffic patterns
     */
    ULONG                   CurrentItr;         /* Current ITR register value (256ns units) */
    ULONG                   ItrSetting;         /* ITR mode setting */
#define E1000_ITR_SETTING_LOWEST_LATENCY    0   /* High int rate for responsiveness */
#define E1000_ITR_SETTING_LOW_LATENCY       1   /* Balanced mode */
#define E1000_ITR_SETTING_BULK_LATENCY      2   /* Low int rate for throughput */
#define E1000_ITR_SETTING_DYNAMIC           3   /* Auto-adjust based on traffic */

    /* Last ITR adjustment timestamp */
    LARGE_INTEGER           LastItrUpdateTime;

    /* ========== Budget-Based RX Processing ========== */

    /*
     * Indicates more NBLs are pending after hitting budget limit.
     * When TRUE, interrupts should NOT be re-enabled to allow NDIS to reschedule.
     */
    volatile BOOLEAN        MoreNblsPending;

} E1000_ADAPTER, *PE1000_ADAPTER;


/* ============================================================================
 * Compatibility Macros for shared code (hardware.c, etc.)
 *
 * These macros provide backward compatibility with code that was
 * originally written for the NDIS 5.1 E1000_NIC_ADAPTER structure.
 * ============================================================================ */

/* Adapter handle alias */
#define AdapterHandle   MiniportAdapterHandle

/* Interrupt mode values for backward compatibility */
#define E1000_INTERRUPT_MODE_LEGACY     E1000InterruptModeLegacy
#define E1000_INTERRUPT_MODE_MSI        E1000InterruptModeMsi
#define E1000_INTERRUPT_MODE_MSIX       E1000InterruptModeMsix

/* Simplified read/write functions (implemented as macros) */
#define E1000ReadUlong(Adapter, Reg, pValue) \
    do { *(pValue) = READ_REGISTER_ULONG((PULONG)((Adapter)->IoBase + (Reg))); } while(0)

#define E1000WriteUlong(Adapter, Reg, Value) \
    WRITE_REGISTER_ULONG((PULONG)((Adapter)->IoBase + (Reg)), (Value))


/* ============================================================================
 * Hardware Register Access Macros
 * ============================================================================ */

#define E1000_READ_REG(Adapter, Reg) \
    READ_REGISTER_ULONG((PULONG)((Adapter)->IoBase + (Reg)))

#define E1000_WRITE_REG(Adapter, Reg, Value) \
    WRITE_REGISTER_ULONG((PULONG)((Adapter)->IoBase + (Reg)), (Value))

#define E1000_READ_REG_ARRAY(Adapter, Reg, Index) \
    READ_REGISTER_ULONG((PULONG)((Adapter)->IoBase + (Reg) + ((Index) << 2)))

#define E1000_WRITE_REG_ARRAY(Adapter, Reg, Index, Value) \
    WRITE_REGISTER_ULONG((PULONG)((Adapter)->IoBase + (Reg) + ((Index) << 2)), (Value))


/* ============================================================================
 * Function Prototypes - init.c
 * ============================================================================ */

DRIVER_INITIALIZE DriverEntry;

MINIPORT_INITIALIZE_EX E1000MiniportInitializeEx;
MINIPORT_HALT_EX E1000MiniportHaltEx;
MINIPORT_UNLOAD E1000MiniportDriverUnload;
MINIPORT_SHUTDOWN_EX E1000MiniportShutdownEx;
MINIPORT_DEVICE_PNP_EVENT_NOTIFY E1000MiniportDevicePnPEventNotify;

NDIS_STATUS
E1000AllocateAdapterResources(
    _In_ PE1000_ADAPTER Adapter
    );

VOID
E1000FreeAdapterResources(
    _In_ PE1000_ADAPTER Adapter
    );

NDIS_STATUS
E1000ReadMacAddress(
    _In_ PE1000_ADAPTER Adapter
    );

NDIS_STATUS
E1000InitializeHardware(
    _In_ PE1000_ADAPTER Adapter
    );


/* ============================================================================
 * Function Prototypes - send.c
 * ============================================================================ */

MINIPORT_SEND_NET_BUFFER_LISTS E1000SendNetBufferLists;
MINIPORT_CANCEL_SEND E1000CancelSend;

NDIS_STATUS
E1000InitializeTxQueue(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG QueueIndex
    );

VOID
E1000FreeTxQueue(
    _In_ PE1000_TX_QUEUE TxQueue
    );

VOID
E1000ProcessTxCompletions(
    _In_ PE1000_TX_QUEUE TxQueue
    );

KDEFERRED_ROUTINE E1000TxCompletionDpc;


/* ============================================================================
 * Function Prototypes - receive.c
 * ============================================================================ */

MINIPORT_RETURN_NET_BUFFER_LISTS E1000ReturnNetBufferLists;

NDIS_STATUS
E1000InitializeRxQueue(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG QueueIndex
    );

VOID
E1000FreeRxQueue(
    _In_ PE1000_RX_QUEUE RxQueue
    );

ULONG
E1000IndicateReceive(
    _In_ PE1000_RX_QUEUE RxQueue,
    _In_ ULONG Budget
    );

NDIS_STATUS
E1000AllocateRxBuffers(
    _In_ PE1000_RX_QUEUE RxQueue
    );

VOID
E1000RefillRxBuffers(
    _In_ PE1000_RX_QUEUE RxQueue
    );

KDEFERRED_ROUTINE E1000RxIndicateDpc;


/* ============================================================================
 * Function Prototypes - interrupt.c
 * ============================================================================ */

MINIPORT_ISR E1000MiniportInterrupt;
MINIPORT_INTERRUPT_DPC E1000MiniportInterruptDpc;
MINIPORT_DISABLE_INTERRUPT E1000MiniportDisableInterruptEx;
MINIPORT_ENABLE_INTERRUPT E1000MiniportEnableInterruptEx;

NDIS_STATUS
E1000RegisterInterrupt(
    _In_ PE1000_ADAPTER Adapter
    );

VOID
E1000DeregisterInterrupt(
    _In_ PE1000_ADAPTER Adapter
    );

NDIS_STATUS
E1000EnableMsixInterrupts(
    _In_ PE1000_ADAPTER Adapter
    );

VOID
E1000DisableInterrupts(
    _In_ PE1000_ADAPTER Adapter
    );

VOID
E1000EnableInterrupts(
    _In_ PE1000_ADAPTER Adapter
    );


/* ============================================================================
 * Function Prototypes - oid.c
 * ============================================================================ */

MINIPORT_OID_REQUEST E1000OidRequest;
MINIPORT_CANCEL_OID_REQUEST E1000CancelOidRequest;

NDIS_STATUS
E1000QueryInformation(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest
    );

NDIS_STATUS
E1000SetInformation(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest
    );


/* ============================================================================
 * Function Prototypes - control.c
 * ============================================================================ */

MINIPORT_PAUSE E1000MiniportPause;
MINIPORT_RESTART E1000MiniportRestart;
MINIPORT_RESET_EX E1000MiniportResetEx;
MINIPORT_CHECK_FOR_HANG_EX E1000MiniportCheckForHangEx;


/* ============================================================================
 * Function Prototypes - power.c
 * ============================================================================ */

NDIS_STATUS
E1000SetPower(
    _In_ PE1000_ADAPTER Adapter,
    _In_ NDIS_DEVICE_POWER_STATE PowerState
    );

NDIS_STATUS
E1000PowerUp(
    _In_ PE1000_ADAPTER Adapter
    );

NDIS_STATUS
E1000PowerDown(
    _In_ PE1000_ADAPTER Adapter,
    _In_ NDIS_DEVICE_POWER_STATE PowerState
    );

VOID
E1000ConfigureWakeOnLan(
    _In_ PE1000_ADAPTER Adapter
    );


/* ============================================================================
 * Function Prototypes - offload.c
 * ============================================================================ */

NDIS_STATUS
E1000InitializeOffloadCapabilities(
    _In_ PE1000_ADAPTER Adapter,
    _Out_ PNDIS_OFFLOAD OffloadCapabilities
    );

NDIS_STATUS
E1000SetOffloadParameters(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_OFFLOAD_PARAMETERS OffloadParams
    );


/* ============================================================================
 * Function Prototypes - rss.c
 * ============================================================================ */

NDIS_STATUS
E1000InitializeRss(
    _In_ PE1000_ADAPTER Adapter
    );

NDIS_STATUS
E1000ConfigureRss(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_RECEIVE_SCALE_PARAMETERS RssParams,
    _In_ ULONG Size
    );

VOID
E1000DisableRss(
    _In_ PE1000_ADAPTER Adapter
    );


/* ============================================================================
 * Function Prototypes - hardware.c (existing, updated)
 * ============================================================================ */

BOOLEAN
E1000RecognizeHardware(
    _In_ PE1000_ADAPTER Adapter
    );

NDIS_STATUS
E1000ResetHardware(
    _In_ PE1000_ADAPTER Adapter
    );

NDIS_STATUS
E1000SetupLink(
    _In_ PE1000_ADAPTER Adapter
    );

VOID
E1000UpdateLinkStatus(
    _In_ PE1000_ADAPTER Adapter
    );

VOID
E1000UpdateStatistics(
    _In_ PE1000_ADAPTER Adapter
    );

NDIS_STATUS
E1000SetPacketFilter(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG PacketFilter
    );

NDIS_STATUS
E1000SetMulticastList(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PUCHAR MulticastList,
    _In_ ULONG MulticastCount
    );


/* ============================================================================
 * Inline Helper Functions
 * ============================================================================ */

/*
 * E1000GetFreeDescriptorCount - Get number of free TX descriptors
 */
FORCEINLINE
ULONG
E1000GetFreeDescriptorCount(
    _In_ PE1000_TX_QUEUE TxQueue
    )
{
    return (ULONG)InterlockedCompareExchange(&TxQueue->Available, 0, 0);
}

/*
 * E1000IsAdapterPaused - Check if adapter is paused
 */
FORCEINLINE
BOOLEAN
E1000IsAdapterPaused(
    _In_ PE1000_ADAPTER Adapter
    )
{
    return (InterlockedCompareExchange(&Adapter->Flags, 0, 0) &
            (E1000_FLAG_ADAPTER_PAUSED | E1000_FLAG_ADAPTER_PAUSING)) != 0;
}

/*
 * E1000IsAdapterStarted - Check if adapter is started
 */
FORCEINLINE
BOOLEAN
E1000IsAdapterStarted(
    _In_ PE1000_ADAPTER Adapter
    )
{
    return (InterlockedCompareExchange(&Adapter->Flags, 0, 0) &
            E1000_FLAG_ADAPTER_STARTED) != 0;
}

/* End of e1000.h */
