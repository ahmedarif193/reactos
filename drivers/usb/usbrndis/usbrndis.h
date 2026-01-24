/*
 * PROJECT:     ReactOS USB RNDIS Network Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     USB RNDIS class driver header
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This is a clean-room implementation based on the Microsoft RNDIS specification
 * and the USB CDC (Communications Device Class) specification.
 */

#ifndef _USBRNDIS_H_
#define _USBRNDIS_H_

#include <ntddk.h>
#include <ndis.h>
#include <usbdi.h>
#include <usbbusif.h>
#include <usbdlib.h>

#define USBRNDIS_TAG 'DNRU'

/*
 * RNDIS Protocol Version
 * Per Microsoft RNDIS 1.0 specification
 */
#define RNDIS_MAJOR_VERSION     1
#define RNDIS_MINOR_VERSION     0

/*
 * RNDIS Message Types
 * These define the control channel message types used for device initialization,
 * configuration, and status queries.
 */
#define RNDIS_MSG_COMPLETION        0x80000000

#define RNDIS_MSG_PACKET            0x00000001  /* Data packet */
#define RNDIS_MSG_INIT              0x00000002  /* Initialize device */
#define RNDIS_MSG_INIT_C            (RNDIS_MSG_INIT | RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_HALT              0x00000003  /* Halt device */
#define RNDIS_MSG_QUERY             0x00000004  /* Query OID */
#define RNDIS_MSG_QUERY_C           (RNDIS_MSG_QUERY | RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_SET               0x00000005  /* Set OID */
#define RNDIS_MSG_SET_C             (RNDIS_MSG_SET | RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_RESET             0x00000006  /* Reset device */
#define RNDIS_MSG_RESET_C           (RNDIS_MSG_RESET | RNDIS_MSG_COMPLETION)
#define RNDIS_MSG_INDICATE          0x00000007  /* Indicate status */
#define RNDIS_MSG_KEEPALIVE         0x00000008  /* Keepalive message */
#define RNDIS_MSG_KEEPALIVE_C       (RNDIS_MSG_KEEPALIVE | RNDIS_MSG_COMPLETION)

/*
 * RNDIS Status Codes
 */
#define RNDIS_STATUS_SUCCESS            0x00000000
#define RNDIS_STATUS_PENDING            0x00000103
#define RNDIS_STATUS_FAILURE            0xC0000001
#define RNDIS_STATUS_NOT_SUPPORTED      0xC00000BB
#define RNDIS_STATUS_MEDIA_CONNECT      0x4001000B
#define RNDIS_STATUS_MEDIA_DISCONNECT   0x4001000C
#define RNDIS_STATUS_INVALID_DATA       0xC0010015

/*
 * RNDIS Medium Types
 */
#define RNDIS_MEDIUM_802_3              0x00000000

/*
 * RNDIS Physical Medium Types
 */
#define RNDIS_PHYSICAL_MEDIUM_UNSPECIFIED   0x00000000
#define RNDIS_PHYSICAL_MEDIUM_WIRELESS_LAN  0x00000001

/*
 * RNDIS Device Flags
 */
#define RNDIS_DF_CONNECTIONLESS         0x00000001
#define RNDIS_DF_CONNECTION_ORIENTED    0x00000002

/*
 * RNDIS Packet Filter Bits
 * Used with OID_GEN_CURRENT_PACKET_FILTER
 */
#define RNDIS_PACKET_TYPE_DIRECTED      0x00000001
#define RNDIS_PACKET_TYPE_MULTICAST     0x00000002
#define RNDIS_PACKET_TYPE_ALL_MULTICAST 0x00000004
#define RNDIS_PACKET_TYPE_BROADCAST     0x00000008
#define RNDIS_PACKET_TYPE_PROMISCUOUS   0x00000020

/*
 * Default packet filter for RNDIS devices
 */
#define RNDIS_DEFAULT_FILTER    (RNDIS_PACKET_TYPE_DIRECTED | \
                                 RNDIS_PACKET_TYPE_BROADCAST | \
                                 RNDIS_PACKET_TYPE_ALL_MULTICAST)

/*
 * NDIS OIDs (Object Identifiers)
 * Required OIDs for RNDIS device operation
 */
#define RNDIS_OID_GEN_SUPPORTED_LIST        0x00010101
#define RNDIS_OID_GEN_HARDWARE_STATUS       0x00010102
#define RNDIS_OID_GEN_MEDIA_SUPPORTED       0x00010103
#define RNDIS_OID_GEN_MEDIA_IN_USE          0x00010104
#define RNDIS_OID_GEN_MAXIMUM_FRAME_SIZE    0x00010106
#define RNDIS_OID_GEN_LINK_SPEED            0x00010107
#define RNDIS_OID_GEN_TRANSMIT_BLOCK_SIZE   0x0001010A
#define RNDIS_OID_GEN_RECEIVE_BLOCK_SIZE    0x0001010B
#define RNDIS_OID_GEN_VENDOR_ID             0x0001010C
#define RNDIS_OID_GEN_VENDOR_DESCRIPTION    0x0001010D
#define RNDIS_OID_GEN_CURRENT_PACKET_FILTER 0x0001010E
#define RNDIS_OID_GEN_MAXIMUM_TOTAL_SIZE    0x00010111
#define RNDIS_OID_GEN_MEDIA_CONNECT_STATUS  0x00010114
#define RNDIS_OID_GEN_PHYSICAL_MEDIUM       0x00010202
#define RNDIS_OID_GEN_XMIT_OK               0x00020101
#define RNDIS_OID_GEN_RCV_OK                0x00020102
#define RNDIS_OID_GEN_XMIT_ERROR            0x00020103
#define RNDIS_OID_GEN_RCV_ERROR             0x00020104
#define RNDIS_OID_GEN_RCV_NO_BUFFER         0x00020105

/* 802.3 (Ethernet) OIDs */
#define RNDIS_OID_802_3_PERMANENT_ADDRESS   0x01010101
#define RNDIS_OID_802_3_CURRENT_ADDRESS     0x01010102
#define RNDIS_OID_802_3_MULTICAST_LIST      0x01010103
#define RNDIS_OID_802_3_MAXIMUM_LIST_SIZE   0x01010104
#define RNDIS_OID_802_3_RCV_ERROR_ALIGNMENT 0x01020101
#define RNDIS_OID_802_3_XMIT_ONE_COLLISION  0x01020102
#define RNDIS_OID_802_3_XMIT_MORE_COLLISIONS 0x01020103

/*
 * USB CDC Class Request Codes
 * Used for encapsulated RNDIS command/response exchange
 */
#define USB_CDC_SEND_ENCAPSULATED_COMMAND   0x00
#define USB_CDC_GET_ENCAPSULATED_RESPONSE   0x01

/*
 * USB Class Codes
 */
#define USB_CLASS_COMM                  0x02
#define USB_CLASS_CDC_DATA              0x0A
#define USB_CLASS_WIRELESS_CONTROLLER   0xE0
#define USB_CLASS_MISC                  0xEF

/*
 * USB CDC Subclass for RNDIS
 */
#define USB_CDC_SUBCLASS_ACM            0x02

/*
 * USB CDC Protocol for RNDIS
 * RNDIS uses vendor-specific protocol 0xFF on ACM subclass
 */
#define USB_CDC_PROTOCOL_RNDIS          0xFF

/*
 * RNDIS Control Buffer Size
 * RNDIS spec requires minimum 1024 bytes, Windows uses 1025
 */
#define RNDIS_CONTROL_BUFFER_SIZE       1025

/*
 * RNDIS Control Timeout
 * 5 seconds matches USB 2.0 spec control timeout
 */
#define RNDIS_CONTROL_TIMEOUT_MS        5000

/*
 * Ethernet Constants
 */
#define ETHERNET_ADDRESS_LENGTH         6
#define ETHERNET_HEADER_SIZE            14
#define ETHERNET_MAX_FRAME_SIZE         1514
#define ETHERNET_MAX_MTU                1500
#define ETHERNET_MIN_MTU                64

/*
 * RNDIS Data Packet Constants
 */
#define RNDIS_PACKET_HEADER_SIZE        44
#define RNDIS_MAX_TRANSFER_SIZE         (ETHERNET_MAX_FRAME_SIZE + RNDIS_PACKET_HEADER_SIZE + 32)

/*
 * Maximum number of multicast addresses
 */
#define RNDIS_MAX_MULTICAST_ADDRESSES   32

#include <pshpack1.h>

/*
 * RNDIS Message Header
 * Common header for all RNDIS control messages
 */
typedef struct _RNDIS_MSG_HEADER {
    ULONG MessageType;          /* One of RNDIS_MSG_* */
    ULONG MessageLength;        /* Total message length including header */
    ULONG RequestId;            /* Unique request identifier */
    ULONG Status;               /* RNDIS_STATUS_* (for completions) */
} RNDIS_MSG_HEADER, *PRNDIS_MSG_HEADER;

/*
 * RNDIS Initialize Message (Host to Device)
 * Sent to initialize RNDIS protocol connection
 */
typedef struct _RNDIS_INIT_MSG {
    ULONG MessageType;          /* RNDIS_MSG_INIT */
    ULONG MessageLength;        /* 24 */
    ULONG RequestId;
    ULONG MajorVersion;         /* RNDIS major version (1) */
    ULONG MinorVersion;         /* RNDIS minor version (0) */
    ULONG MaxTransferSize;      /* Maximum control transfer size host can handle */
} RNDIS_INIT_MSG, *PRNDIS_INIT_MSG;

/*
 * RNDIS Initialize Completion (Device to Host)
 * Response to RNDIS_INIT_MSG
 */
typedef struct _RNDIS_INIT_CMPLT {
    ULONG MessageType;          /* RNDIS_MSG_INIT_C */
    ULONG MessageLength;
    ULONG RequestId;
    ULONG Status;               /* RNDIS_STATUS_SUCCESS on success */
    ULONG MajorVersion;
    ULONG MinorVersion;
    ULONG DeviceFlags;          /* RNDIS_DF_* */
    ULONG Medium;               /* RNDIS_MEDIUM_802_3 for Ethernet */
    ULONG MaxPacketsPerMessage; /* Usually 1 */
    ULONG MaxTransferSize;      /* Device's max transfer size */
    ULONG PacketAlignmentFactor;/* Data must be aligned to 1<<n bytes */
    ULONG AfListOffset;         /* Reserved, 0 */
    ULONG AfListSize;           /* Reserved, 0 */
} RNDIS_INIT_CMPLT, *PRNDIS_INIT_CMPLT;

/*
 * RNDIS Halt Message (Host to Device)
 * Sent to halt RNDIS connection (no response expected)
 */
typedef struct _RNDIS_HALT_MSG {
    ULONG MessageType;          /* RNDIS_MSG_HALT */
    ULONG MessageLength;        /* 12 */
    ULONG RequestId;
} RNDIS_HALT_MSG, *PRNDIS_HALT_MSG;

/*
 * RNDIS Query Message (Host to Device)
 * Used to query OID values
 */
typedef struct _RNDIS_QUERY_MSG {
    ULONG MessageType;          /* RNDIS_MSG_QUERY */
    ULONG MessageLength;
    ULONG RequestId;
    ULONG Oid;                  /* RNDIS_OID_* to query */
    ULONG InformationBufferLength;
    ULONG InformationBufferOffset; /* From RequestId field */
    ULONG DeviceVcHandle;       /* Reserved, 0 */
    /* Variable length data follows if InformationBufferLength > 0 */
} RNDIS_QUERY_MSG, *PRNDIS_QUERY_MSG;

/*
 * RNDIS Query Completion (Device to Host)
 */
typedef struct _RNDIS_QUERY_CMPLT {
    ULONG MessageType;          /* RNDIS_MSG_QUERY_C */
    ULONG MessageLength;
    ULONG RequestId;
    ULONG Status;
    ULONG InformationBufferLength;
    ULONG InformationBufferOffset; /* From RequestId field */
    /* Variable length data follows */
} RNDIS_QUERY_CMPLT, *PRNDIS_QUERY_CMPLT;

/*
 * RNDIS Set Message (Host to Device)
 * Used to set OID values
 */
typedef struct _RNDIS_SET_MSG {
    ULONG MessageType;          /* RNDIS_MSG_SET */
    ULONG MessageLength;
    ULONG RequestId;
    ULONG Oid;                  /* RNDIS_OID_* to set */
    ULONG InformationBufferLength;
    ULONG InformationBufferOffset; /* From RequestId field */
    ULONG DeviceVcHandle;       /* Reserved, 0 */
    /* Variable length data follows */
} RNDIS_SET_MSG, *PRNDIS_SET_MSG;

/*
 * RNDIS Set Completion (Device to Host)
 */
typedef struct _RNDIS_SET_CMPLT {
    ULONG MessageType;          /* RNDIS_MSG_SET_C */
    ULONG MessageLength;
    ULONG RequestId;
    ULONG Status;
} RNDIS_SET_CMPLT, *PRNDIS_SET_CMPLT;

/*
 * RNDIS Reset Message (Host to Device)
 */
typedef struct _RNDIS_RESET_MSG {
    ULONG MessageType;          /* RNDIS_MSG_RESET */
    ULONG MessageLength;        /* 12 */
    ULONG Reserved;
} RNDIS_RESET_MSG, *PRNDIS_RESET_MSG;

/*
 * RNDIS Reset Completion (Device to Host)
 */
typedef struct _RNDIS_RESET_CMPLT {
    ULONG MessageType;          /* RNDIS_MSG_RESET_C */
    ULONG MessageLength;
    ULONG Status;
    ULONG AddressingReset;      /* Non-zero if addressing needs to be re-initialized */
} RNDIS_RESET_CMPLT, *PRNDIS_RESET_CMPLT;

/*
 * RNDIS Indicate Status Message (Device to Host, unsolicited)
 */
typedef struct _RNDIS_INDICATE_MSG {
    ULONG MessageType;          /* RNDIS_MSG_INDICATE */
    ULONG MessageLength;
    ULONG Status;               /* Status code (e.g., RNDIS_STATUS_MEDIA_CONNECT) */
    ULONG StatusBufferLength;
    ULONG StatusBufferOffset;   /* From Status field */
} RNDIS_INDICATE_MSG, *PRNDIS_INDICATE_MSG;

/*
 * RNDIS Keepalive Message (Host to Device or Device to Host)
 */
typedef struct _RNDIS_KEEPALIVE_MSG {
    ULONG MessageType;          /* RNDIS_MSG_KEEPALIVE */
    ULONG MessageLength;        /* 12 */
    ULONG RequestId;
} RNDIS_KEEPALIVE_MSG, *PRNDIS_KEEPALIVE_MSG;

/*
 * RNDIS Keepalive Completion
 */
typedef struct _RNDIS_KEEPALIVE_CMPLT {
    ULONG MessageType;          /* RNDIS_MSG_KEEPALIVE_C */
    ULONG MessageLength;        /* 16 */
    ULONG RequestId;
    ULONG Status;
} RNDIS_KEEPALIVE_CMPLT, *PRNDIS_KEEPALIVE_CMPLT;

/*
 * RNDIS Data Packet Header
 * Encapsulates Ethernet frames for bulk data transfer
 */
typedef struct _RNDIS_PACKET_MSG {
    ULONG MessageType;          /* RNDIS_MSG_PACKET */
    ULONG MessageLength;        /* Total length including this header and data */
    ULONG DataOffset;           /* Offset from start of DataOffset field to data */
    ULONG DataLength;           /* Length of actual Ethernet frame data */
    ULONG OOBDataOffset;        /* Out-of-band data offset, usually 0 */
    ULONG OOBDataLength;        /* Out-of-band data length, usually 0 */
    ULONG NumOOBDataElements;   /* Number of OOB elements, usually 0 */
    ULONG PerPacketInfoOffset;  /* Per-packet info offset, usually 0 */
    ULONG PerPacketInfoLength;  /* Per-packet info length, usually 0 */
    ULONG VcHandle;             /* Reserved, 0 */
    ULONG Reserved;             /* Reserved, 0 */
    /* Variable length data follows at DataOffset */
} RNDIS_PACKET_MSG, *PRNDIS_PACKET_MSG;

#include <poppack.h>

C_ASSERT(sizeof(RNDIS_INIT_MSG) == 24);
C_ASSERT(sizeof(RNDIS_PACKET_MSG) == 44);

/*
 * Driver State Enumeration
 */
typedef enum _RNDIS_STATE {
    RndisStateUninitialized = 0,
    RndisStateInitializing,
    RndisStateInitialized,
    RndisStateDataInitialized,
    RndisStateHalted
} RNDIS_STATE;

/*
 * USB Endpoint Information
 */
typedef struct _RNDIS_USB_ENDPOINT {
    USBD_PIPE_HANDLE PipeHandle;
    UCHAR EndpointAddress;
    ULONG MaxPacketSize;
} RNDIS_USB_ENDPOINT, *PRNDIS_USB_ENDPOINT;

/*
 * RNDIS Adapter Context
 * Main driver context structure
 */
typedef struct _RNDIS_ADAPTER {
    /* NDIS Handle */
    NDIS_HANDLE MiniportAdapterHandle;

    /* Device Objects */
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;

    /* USB Interface */
    USB_BUS_INTERFACE_USBDI_V2 BusInterface;
    PUSB_DEVICE_DESCRIPTOR DeviceDescriptor;
    PUSB_CONFIGURATION_DESCRIPTOR ConfigurationDescriptor;
    PUSBD_INTERFACE_INFORMATION ControlInterface;
    PUSBD_INTERFACE_INFORMATION DataInterface;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;

    /* USB Endpoints */
    RNDIS_USB_ENDPOINT BulkInEndpoint;
    RNDIS_USB_ENDPOINT BulkOutEndpoint;
    RNDIS_USB_ENDPOINT InterruptEndpoint;
    UCHAR ControlInterfaceNumber;
    UCHAR DataInterfaceNumber;

    /* RNDIS Protocol State */
    RNDIS_STATE State;
    ULONG RequestId;
    ULONG MaxTransferSize;
    ULONG PacketAlignmentFactor;
    ULONG MaxPacketsPerMessage;

    /* Network Configuration */
    UCHAR PermanentMacAddress[ETHERNET_ADDRESS_LENGTH];
    UCHAR CurrentMacAddress[ETHERNET_ADDRESS_LENGTH];
    ULONG PacketFilter;
    ULONG LinkSpeed;                /* In 100 bps units */
    NDIS_MEDIA_STATE MediaState;

    /* Multicast List */
    ULONG MulticastListCount;
    UCHAR MulticastList[RNDIS_MAX_MULTICAST_ADDRESSES][ETHERNET_ADDRESS_LENGTH];

    /* Statistics */
    ULONG64 TxOkCount;
    ULONG64 RxOkCount;
    ULONG64 TxErrorCount;
    ULONG64 RxErrorCount;
    ULONG64 RxNoBufferCount;

    /* Control Transfer Buffer */
    PUCHAR ControlBuffer;

    /* Async I/O Tracking */
    LONG PendingIoCount;            /* Count of pending async I/O operations */
    KEVENT RemoveEvent;             /* Signaled when PendingIoCount reaches zero */
    BOOLEAN Halting;                /* Set TRUE during halt to stop resubmission */

    /* Transmit Resources */
    PUCHAR TxBuffer;
    NDIS_SPIN_LOCK TxLock;
    BOOLEAN TxBusy;
    PIRP TxIrp;                     /* Pending TX IRP for cancellation */
    PNDIS_PACKET PendingTxPacket;   /* Packet awaiting TX completion */
    URB TxUrb;

    /* Receive Resources */
    PUCHAR RxBuffer;
    NDIS_SPIN_LOCK RxLock;
    PIRP RxIrp;                     /* Pending RX IRP for cancellation */
    BOOLEAN RxSubmitted;            /* RX URB has been submitted */
    URB RxUrb;

    /* Work Items */
    NDIS_WORK_ITEM ResetWorkItem;
    BOOLEAN ResetPending;

    /* Synchronization */
    KEVENT ControlEvent;
    NDIS_SPIN_LOCK ControlLock;     /* Use NDIS spinlock for consistency */

} RNDIS_ADAPTER, *PRNDIS_ADAPTER;

/*
 * Function Prototypes - usbrndis.c
 */
NDIS_STATUS
NTAPI
RndisInitialize(
    OUT PNDIS_STATUS OpenErrorStatus,
    OUT PUINT SelectedMediumIndex,
    IN PNDIS_MEDIUM MediumArray,
    IN UINT MediumArraySize,
    IN NDIS_HANDLE MiniportAdapterHandle,
    IN NDIS_HANDLE WrapperConfigurationContext);

VOID
NTAPI
RndisHalt(
    IN NDIS_HANDLE MiniportAdapterContext);

NDIS_STATUS
NTAPI
RndisReset(
    OUT PBOOLEAN AddressingReset,
    IN NDIS_HANDLE MiniportAdapterContext);

/*
 * Function Prototypes - rndisusb.c (USB operations)
 */
NTSTATUS
RndisUsbGetDescriptors(
    IN PRNDIS_ADAPTER Adapter);

NTSTATUS
RndisUsbSelectConfiguration(
    IN PRNDIS_ADAPTER Adapter);

NTSTATUS
RndisUsbSendControlMessage(
    IN PRNDIS_ADAPTER Adapter,
    IN PVOID Buffer,
    IN ULONG BufferLength);

NTSTATUS
RndisUsbReceiveControlResponse(
    IN PRNDIS_ADAPTER Adapter,
    OUT PVOID Buffer,
    IN ULONG BufferLength,
    OUT PULONG BytesReceived);

NTSTATUS
RndisUsbSubmitBulkRead(
    IN PRNDIS_ADAPTER Adapter);

NTSTATUS
RndisUsbSubmitBulkWrite(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR Data,
    IN ULONG Length);

/*
 * Async I/O Helper Functions
 */
NTSTATUS
RndisAsyncUrbRequest(
    IN PRNDIS_ADAPTER Adapter,
    IN PURB Urb,
    IN PIO_COMPLETION_ROUTINE CompletionRoutine,
    IN PVOID Context,
    OUT PIRP *OutIrp OPTIONAL);

VOID
RndisDecrementPendingIo(
    IN PRNDIS_ADAPTER Adapter);

/*
 * Function Prototypes - rndisctl.c (RNDIS control protocol)
 */
NTSTATUS
RndisInitializeDevice(
    IN PRNDIS_ADAPTER Adapter);

NTSTATUS
RndisHaltDevice(
    IN PRNDIS_ADAPTER Adapter);

NTSTATUS
RndisQueryOid(
    IN PRNDIS_ADAPTER Adapter,
    IN ULONG Oid,
    OUT PVOID Buffer,
    IN ULONG BufferLength,
    OUT PULONG BytesWritten);

NTSTATUS
RndisSetOid(
    IN PRNDIS_ADAPTER Adapter,
    IN ULONG Oid,
    IN PVOID Buffer,
    IN ULONG BufferLength);

NTSTATUS
RndisSetPacketFilter(
    IN PRNDIS_ADAPTER Adapter,
    IN ULONG PacketFilter);

NTSTATUS
RndisGetMacAddress(
    IN PRNDIS_ADAPTER Adapter);

/*
 * Function Prototypes - rndisdata.c (Data transfer)
 */
NDIS_STATUS
NTAPI
RndisSend(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_PACKET Packet,
    IN UINT Flags);

VOID
NTAPI
RndisSendPackets(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PPNDIS_PACKET PacketArray,
    IN UINT NumberOfPackets);

VOID
RndisProcessReceivedPacket(
    IN PRNDIS_ADAPTER Adapter,
    IN PUCHAR Data,
    IN ULONG Length);

/*
 * Function Prototypes - rndisoid.c (OID handling)
 */
NDIS_STATUS
NTAPI
RndisQueryInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesWritten,
    OUT PULONG BytesNeeded);

NDIS_STATUS
NTAPI
RndisSetInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesRead,
    OUT PULONG BytesNeeded);

/*
 * Helper Macros
 */
#define RNDIS_GET_REQUEST_ID(Adapter) \
    InterlockedIncrement((PLONG)&(Adapter)->RequestId)

/*
 * Debug Macros
 */
#if DBG
#define RNDIS_DEBUG_PRINT(fmt, ...) \
    DbgPrint("USBRNDIS: " fmt, ##__VA_ARGS__)
#else
#define RNDIS_DEBUG_PRINT(fmt, ...)
#endif

#endif /* _USBRNDIS_H_ */
