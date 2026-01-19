/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Hardware specific functions
 * COPYRIGHT:   2013 Cameron Gutman (cameron.gutman@reactos.org)
 *              2018 Mark Jansen (mark.jansen@reactos.org)
 *              2019 Victor Perevertkin (victor.perevertkin@reactos.org)
 *              2024 ReactOS Team - Modernization for 82574L PCIe support
 */

#ifndef _E1000_PCH_
#define _E1000_PCH_

#include <ndis.h>

#include "e1000hw.h"

#define E1000_TAG '001e'

#define MAXIMUM_FRAME_SIZE   1522
#define RECEIVE_BUFFER_SIZE  2048

#define DRIVER_VERSION 2

#define DEFAULT_INTERRUPT_MASK  (E1000_IMS_LSC | E1000_IMS_TXDW | E1000_IMS_TXQE | E1000_IMS_RXDMT0 | E1000_IMS_RXT0 | E1000_IMS_TXD_LOW)

/* Interrupt mode enumeration */
typedef enum _E1000_INTERRUPT_MODE {
    E1000_INTERRUPT_MODE_LEGACY = 0,    /* Line-based (INTx) interrupts */
    E1000_INTERRUPT_MODE_MSI,           /* Message Signaled Interrupts (single vector) */
    E1000_INTERRUPT_MODE_MSIX           /* MSI-X (multiple vectors) */
} E1000_INTERRUPT_MODE;

/* Power state enumeration (matches NDIS device states) */
typedef enum _E1000_POWER_STATE {
    E1000PowerStateD0 = 0,      /* Fully operational */
    E1000PowerStateD1,          /* Light sleep */
    E1000PowerStateD2,          /* Deeper sleep */
    E1000PowerStateD3           /* Lowest power, device may lose context */
} E1000_POWER_STATE;

/* Checksum offload capabilities */
typedef struct _E1000_CHECKSUM_OFFLOAD {
    BOOLEAN TxIpChecksum;       /* TX IP header checksum offload supported */
    BOOLEAN TxTcpChecksum;      /* TX TCP checksum offload supported */
    BOOLEAN TxUdpChecksum;      /* TX UDP checksum offload supported */
    BOOLEAN RxIpChecksum;       /* RX IP header checksum offload supported */
    BOOLEAN RxTcpChecksum;      /* RX TCP checksum offload supported */
    BOOLEAN RxUdpChecksum;      /* RX UDP checksum offload supported */

    /* Currently enabled settings (may be different from supported) */
    BOOLEAN TxChecksumEnabled;
    BOOLEAN RxChecksumEnabled;
} E1000_CHECKSUM_OFFLOAD, *PE1000_CHECKSUM_OFFLOAD;

/* Interrupt coalescing / adaptive moderation */
typedef struct _E1000_INTERRUPT_MODERATION {
    BOOLEAN AdaptiveEnabled;    /* TRUE if adaptive moderation is active */
    ULONG CurrentItr;           /* Current interrupt throttle rate */
    ULONG PacketsSinceLastAdjust;
    ULONG BytesSinceLastAdjust;
    ULONG AdjustmentInterval;   /* Ticks between adjustments */
} E1000_INTERRUPT_MODERATION, *PE1000_INTERRUPT_MODERATION;

/* Statistics counters (read from hardware) */
typedef struct _E1000_STATISTICS {
    ULONG64 TxPackets;
    ULONG64 RxPackets;
    ULONG64 TxBytes;
    ULONG64 RxBytes;
    ULONG64 TxErrors;
    ULONG64 RxErrors;
    ULONG64 RxNoBuffer;
    ULONG64 RxCrcErrors;
    ULONG64 RxAlignErrors;
    ULONG64 TxCollisions;
} E1000_STATISTICS, *PE1000_STATISTICS;

/* Device serial number (from PCIe capability) */
typedef struct _E1000_DEVICE_SERIAL_NUMBER {
    BOOLEAN Valid;
    UCHAR Serial[8];            /* 64-bit serial number */
} E1000_DEVICE_SERIAL_NUMBER, *PE1000_DEVICE_SERIAL_NUMBER;

typedef struct _E1000_ADAPTER
{
    /* NIC Memory */
    volatile PUCHAR IoBase;
    NDIS_PHYSICAL_ADDRESS IoAddress;
    ULONG IoLength;

    /* MSI-X BAR (if present) */
    volatile PUCHAR MsixBase;
    NDIS_PHYSICAL_ADDRESS MsixAddress;
    ULONG MsixLength;

    NDIS_HANDLE AdapterHandle;
    USHORT VendorID;
    USHORT DeviceID;
    USHORT SubsystemID;
    USHORT SubsystemVendorID;

    /* Device type flags */
    BOOLEAN IsPCIe;         /* TRUE if this is a PCIe device (82574L, etc.) */
    BOOLEAN HasFlash;       /* TRUE if NVM is flash instead of EEPROM */

    UCHAR PermanentMacAddress[IEEE_802_ADDR_LENGTH];

    struct {
        UCHAR MacAddress[IEEE_802_ADDR_LENGTH];
    } MulticastList[MAXIMUM_MULTICAST_ADDRESSES];
    ULONG MulticastListSize;

    ULONG LinkSpeedMbps;
    ULONG MediaState;
    ULONG PacketFilter;

    /* Io Port */
    ULONG IoPortAddress;
    ULONG IoPortLength;
    volatile PUCHAR IoPort;

    /* Interrupt - Legacy mode */
    ULONG InterruptVector;
    ULONG InterruptLevel;
    BOOLEAN InterruptShared;
    ULONG InterruptFlags;

    /* Legacy IRQ from PCI config space (for fallback) */
    UCHAR PciInterruptLine;

    NDIS_MINIPORT_INTERRUPT Interrupt;
    BOOLEAN InterruptRegistered;

    LONG InterruptMask;

    _Interlocked_
    volatile LONG InterruptPending;

    /* Interrupt mode (Legacy/MSI/MSI-X) */
    E1000_INTERRUPT_MODE InterruptMode;

    /* MSI-X support */
    ULONG MsixVectorCount;          /* Number of MSI-X vectors allocated (0-5) */
    BOOLEAN MsixEnabled;            /* TRUE if MSI-X is active */
    BOOLEAN MsiEnabled;             /* TRUE if MSI is active */

    /* Interrupt coalescing */
    E1000_INTERRUPT_MODERATION InterruptModeration;


    /* Transmit */
    PE1000_TRANSMIT_DESCRIPTOR TransmitDescriptors;
    NDIS_PHYSICAL_ADDRESS TransmitDescriptorsPa;

    PNDIS_PACKET TransmitPackets[NUM_TRANSMIT_DESCRIPTORS];

    ULONG CurrentTxDesc;
    ULONG LastTxDesc;
    BOOLEAN TxFull;

    /* TX lock for batch send */
    NDIS_SPIN_LOCK TxLock;


    /* Receive */
    PE1000_RECEIVE_DESCRIPTOR ReceiveDescriptors;
    NDIS_PHYSICAL_ADDRESS ReceiveDescriptorsPa;

    E1000_RCVBUF_SIZE ReceiveBufferType;
    volatile PUCHAR ReceiveBuffer;
    NDIS_PHYSICAL_ADDRESS ReceiveBufferPa;
    ULONG ReceiveBufferEntrySize;

    /* RX lock */
    NDIS_SPIN_LOCK RxLock;


    /* Checksum offload */
    E1000_CHECKSUM_OFFLOAD ChecksumOffload;

    /* Power Management */
    E1000_POWER_STATE CurrentPowerState;
    E1000_POWER_STATE RequestedPowerState;
    BOOLEAN WakeOnMagicPacket;          /* Wake on magic packet enabled */
    BOOLEAN WakeOnPatternMatch;         /* Wake on pattern match enabled */
    BOOLEAN WakeOnLinkChange;           /* Wake on link status change */
    NDIS_DEVICE_POWER_STATE NdisPowerState;

    /* Device serial number */
    E1000_DEVICE_SERIAL_NUMBER DeviceSerialNumber;

    /* Statistics */
    E1000_STATISTICS Statistics;

    /* AER - Advanced Error Reporting */
    BOOLEAN AerCapable;                 /* TRUE if AER capability detected */
    ULONG AerCapOffset;                 /* PCI config space offset for AER */
    ULONG LastUncorrectableError;       /* Last uncorrectable error status */
    ULONG LastCorrectableError;         /* Last correctable error status */

} E1000_ADAPTER, *PE1000_ADAPTER;


/* ============================================================================
 * Function Prototypes - Hardware Operations
 * ============================================================================ */

BOOLEAN
NTAPI
NICRecognizeHardware(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICInitializeAdapterResources(
    IN PE1000_ADAPTER Adapter,
    IN PNDIS_RESOURCE_LIST ResourceList);

NDIS_STATUS
NTAPI
NICAllocateIoResources(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICRegisterInterrupts(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICUnregisterInterrupts(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICReleaseIoResources(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICPowerOn(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICSoftReset(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICEnableTxRx(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICDisableTxRx(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICGetPermanentMacAddress(
    IN PE1000_ADAPTER Adapter,
    OUT PUCHAR MacAddress);

NDIS_STATUS
NTAPI
NICUpdateMulticastList(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICApplyPacketFilter(
    IN PE1000_ADAPTER Adapter);

VOID
NTAPI
NICUpdateLinkStatus(
    IN PE1000_ADAPTER Adapter);


/* ============================================================================
 * Function Prototypes - Checksum Offload
 * ============================================================================ */

VOID
NTAPI
NICInitializeChecksumOffload(
    IN PE1000_ADAPTER Adapter);

NDIS_STATUS
NTAPI
NICEnableChecksumOffload(
    IN PE1000_ADAPTER Adapter,
    IN BOOLEAN EnableTx,
    IN BOOLEAN EnableRx);

NDIS_STATUS
NTAPI
NICDisableChecksumOffload(
    IN PE1000_ADAPTER Adapter);


/* ============================================================================
 * Function Prototypes - Power Management
 * ============================================================================ */

NDIS_STATUS
NTAPI
NICSetPowerState(
    IN PE1000_ADAPTER Adapter,
    IN NDIS_DEVICE_POWER_STATE PowerState);

NDIS_STATUS
NTAPI
NICQueryPowerState(
    IN PE1000_ADAPTER Adapter,
    IN NDIS_DEVICE_POWER_STATE PowerState);

VOID
NTAPI
NICSaveDeviceState(
    IN PE1000_ADAPTER Adapter);

VOID
NTAPI
NICRestoreDeviceState(
    IN PE1000_ADAPTER Adapter);


/* ============================================================================
 * Function Prototypes - Statistics
 * ============================================================================ */

VOID
NTAPI
NICUpdateStatistics(
    IN PE1000_ADAPTER Adapter);


/* ============================================================================
 * Function Prototypes - Miniport Handlers
 * ============================================================================ */

NDIS_STATUS
NTAPI
MiniportSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_PACKET Packet,
    _In_ UINT Flags);

VOID
NTAPI
MiniportSendPackets(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PPNDIS_PACKET PacketArray,
    _In_ UINT NumberOfPackets);

NDIS_STATUS
NTAPI
MiniportSetInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesRead,
    OUT PULONG BytesNeeded);

NDIS_STATUS
NTAPI
MiniportQueryInformation(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN NDIS_OID Oid,
    IN PVOID InformationBuffer,
    IN ULONG InformationBufferLength,
    OUT PULONG BytesWritten,
    OUT PULONG BytesNeeded);

VOID
NTAPI
MiniportISR(
    OUT PBOOLEAN InterruptRecognized,
    OUT PBOOLEAN QueueMiniportHandleInterrupt,
    IN NDIS_HANDLE MiniportAdapterContext);

VOID
NTAPI
MiniportHandleInterrupt(
    IN NDIS_HANDLE MiniportAdapterContext);


/* ============================================================================
 * Inline Functions - Register Access
 * ============================================================================ */

FORCEINLINE
VOID
E1000ReadUlong(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG Address,
    _Out_ PULONG Value)
{
    NdisReadRegisterUlong((PULONG)(Adapter->IoBase + Address), Value);
}

FORCEINLINE
VOID
E1000WriteUlong(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value)
{
    NdisWriteRegisterUlong((PULONG)(Adapter->IoBase + Address), Value);
}

FORCEINLINE
VOID
E1000WriteIoUlong(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG Address,
    _In_ ULONG Value)
{
    volatile ULONG Dummy;

    NdisRawWritePortUlong((PULONG)(Adapter->IoPort), Address);
    NdisReadRegisterUlong((PULONG)(Adapter->IoBase + E1000_REG_STATUS), &Dummy);
    NdisRawWritePortUlong((PULONG)(Adapter->IoPort + 4), Value);
}

FORCEINLINE
VOID
NICApplyInterruptMask(
    _In_ PE1000_ADAPTER Adapter)
{
    E1000WriteUlong(Adapter, E1000_REG_IMS, Adapter->InterruptMask);
}

FORCEINLINE
VOID
NICDisableInterrupts(
    _In_ PE1000_ADAPTER Adapter)
{
    E1000WriteUlong(Adapter, E1000_REG_IMC, ~0);
}


/* ============================================================================
 * Inline Functions - Transmit Helpers
 * ============================================================================ */

FORCEINLINE
ULONG
NICGetFreeTxDescriptors(
    _In_ PE1000_ADAPTER Adapter)
{
    if (Adapter->TxFull)
        return 0;

    if (Adapter->CurrentTxDesc >= Adapter->LastTxDesc)
        return NUM_TRANSMIT_DESCRIPTORS - (Adapter->CurrentTxDesc - Adapter->LastTxDesc) - 1;
    else
        return Adapter->LastTxDesc - Adapter->CurrentTxDesc - 1;
}

FORCEINLINE
BOOLEAN
NICIsTxDescriptorAvailable(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG Count)
{
    return NICGetFreeTxDescriptors(Adapter) >= Count;
}


#endif /* _E1000_PCH_ */
