# E1000 Driver NDIS 6.x Modernization Specification
COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)


## Document Information
- **Version:** 1.0
- **Date:** 2024
- **Target Hardware:** Intel 82574L Gigabit Network Connection (and compatible)
- **Target OS:** ReactOS (Windows 10 compatible NDIS 6.x)

---

## 1. Executive Summary

This document specifies the complete modernization of the ReactOS E1000 network driver from legacy NDIS 5.x to NDIS 6.x, enabling full utilization of modern hardware capabilities including MSI-X interrupts, Receive Side Scaling (RSS), hardware offloads, and improved power management.

### 1.1 Current State (NDIS 5.x Legacy)

| Component | Current Implementation |
|-----------|----------------------|
| NDIS Version | 5.0 (NDIS50_MINIPORT) |
| Packet Model | NDIS_PACKET |
| Send Path | MiniportSend / MiniportSendPackets |
| Receive Path | NdisMIndicateReceivePacket |
| Interrupts | NdisMRegisterInterrupt (line-based only) |
| DMA | NdisMInitializeScatterGatherDma |
| Offloads | None functional |
| Power | Basic D0-D3 |

### 1.2 Target State (NDIS 6.x Modern)

| Component | Target Implementation |
|-----------|----------------------|
| NDIS Version | 6.30+ (Windows 8+) |
| Packet Model | NET_BUFFER_LIST / NET_BUFFER |
| Send Path | MiniportSendNetBufferLists |
| Receive Path | NdisMIndicateReceiveNetBufferLists |
| Interrupts | MSI-X via IoConnectInterruptEx |
| DMA | NDIS SG-DMA with NET_BUFFER |
| Offloads | Checksum, LSO, RSS |
| Power | NDIS 6.x Power Management |

---

## 2. Architecture Overview

### 2.1 NDIS 6.x Driver Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        NDIS 6.x Framework                        │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────────────────┐  │
│  │   Send      │  │   Receive   │  │   Interrupt             │  │
│  │   Path      │  │   Path      │  │   Management            │  │
│  │             │  │             │  │                         │  │
│  │ NBL Queue   │  │ NBL Pool    │  │  MSI-X Vectors (0-4)    │  │
│  │ SG-DMA      │  │ Indication  │  │  - RxQ0, RxQ1           │  │
│  │ Completion  │  │ Return      │  │  - TxQ0, TxQ1           │  │
│  └─────────────┘  └─────────────┘  │  - Other/Link           │  │
│                                     └─────────────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────────────────────────────────────────────────────┐│
│  │                    Hardware Abstraction                      ││
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────────────┐ ││
│  │  │ TX Ring │  │ RX Ring │  │ MMIO    │  │ MSI-X Table     │ ││
│  │  │ 256 desc│  │ 256 desc│  │ Regs    │  │ BAR3 (16KB)     │ ││
│  │  └─────────┘  └─────────┘  └─────────┘  └─────────────────┘ ││
│  └─────────────────────────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────────────┐
│                Intel 82574L Hardware                             │
│  - PCIe x1 Gen1 (2.5 GT/s)                                      │
│  - MSI-X: 5 vectors                                              │
│  - Checksum Offload (IP, TCP, UDP)                              │
│  - TCP Segmentation Offload                                      │
│  - 1000/100/10 Mbps                                             │
└─────────────────────────────────────────────────────────────────┘
```

### 2.2 82574L Memory Map

| BAR | Size | Purpose |
|-----|------|---------|
| BAR0 | 128KB | Main Register Space |
| BAR1 | 128KB | Flash Memory (NVM) |
| BAR2 | 32B | I/O Ports (legacy) |
| BAR3 | 16KB | MSI-X Table & PBA |

### 2.3 MSI-X Vector Assignment (82574L)

| Vector | ICR Bit | Purpose | IVAR Config |
|--------|---------|---------|-------------|
| 0 | RXQ0 (bit 20) | Receive Queue 0 | IVAR0[7:0] |
| 1 | RXQ1 (bit 21) | Receive Queue 1 | IVAR0[23:16] |
| 2 | TXQ0 (bit 22) | Transmit Queue 0 | IVAR0[15:8] |
| 3 | TXQ1 (bit 23) | Transmit Queue 1 | IVAR0[31:24] |
| 4 | Other (bit 24) | Link, PHY, etc. | IVAR_MISC[7:0] |

---

## 3. Implementation Tasks

### Phase 1: Core NDIS 6.x Infrastructure

#### Task 1.1: Create NDIS 6.x Miniport Registration
**File:** `ndis.c` (rewrite)

**Current:**
```c
NDIS_MINIPORT_CHARACTERISTICS Characteristics = {0};
Characteristics.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION; // 5
Characteristics.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION; // 0
Characteristics.InitializeHandler = MiniportInitialize;
Characteristics.SendHandler = MiniportSend;
// ...
NdisMRegisterMiniport(WrapperHandle, &Characteristics, sizeof(Characteristics));
```

**Target:**
```c
NDIS_MINIPORT_DRIVER_CHARACTERISTICS MiniportCharacteristics;
NdisZeroMemory(&MiniportCharacteristics, sizeof(MiniportCharacteristics));

MiniportCharacteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
MiniportCharacteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
MiniportCharacteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;

MiniportCharacteristics.MajorNdisVersion = 6;
MiniportCharacteristics.MinorNdisVersion = 30;
MiniportCharacteristics.MajorDriverVersion = 2;
MiniportCharacteristics.MinorDriverVersion = 0;

// NDIS 6.x handlers
MiniportCharacteristics.InitializeHandlerEx = E1000MiniportInitializeEx;
MiniportCharacteristics.HaltHandlerEx = E1000MiniportHaltEx;
MiniportCharacteristics.PauseHandler = E1000MiniportPause;
MiniportCharacteristics.RestartHandler = E1000MiniportRestart;
MiniportCharacteristics.SendNetBufferListsHandler = E1000SendNetBufferLists;
MiniportCharacteristics.ReturnNetBufferListsHandler = E1000ReturnNetBufferLists;
MiniportCharacteristics.CancelSendHandler = E1000CancelSend;
MiniportCharacteristics.OidRequestHandler = E1000OidRequest;
MiniportCharacteristics.CancelOidRequestHandler = E1000CancelOidRequest;
MiniportCharacteristics.CheckForHangHandlerEx = E1000CheckForHangEx;
MiniportCharacteristics.ResetHandlerEx = E1000ResetEx;
MiniportCharacteristics.DevicePnPEventNotifyHandler = E1000DevicePnPEventNotify;
MiniportCharacteristics.ShutdownHandlerEx = E1000ShutdownEx;

Status = NdisMRegisterMiniportDriver(
    DriverObject,
    RegistryPath,
    NULL,  // MiniportDriverContext
    &MiniportCharacteristics,
    &NdisDriverHandle);
```

**Subtasks:**
- [ ] Remove all NDIS 5.x handler registrations
- [ ] Implement NDIS_MINIPORT_DRIVER_CHARACTERISTICS structure
- [ ] Implement DriverEntry with NdisMRegisterMiniportDriver
- [ ] Implement MiniportDriverUnload
- [ ] Store NdisDriverHandle globally

---

#### Task 1.2: Implement MiniportInitializeEx
**File:** `init.c` (new)

**Responsibilities:**
1. Allocate adapter context
2. Read PCI configuration
3. Map hardware resources (BARs)
4. Initialize MSI-X interrupts
5. Allocate NBL pools
6. Initialize descriptor rings
7. Configure hardware
8. Register with NDIS

**Key Structures:**
```c
typedef struct _E1000_ADAPTER {
    // NDIS 6.x handles
    NDIS_HANDLE             MiniportAdapterHandle;
    NDIS_HANDLE             NblPool;
    NDIS_HANDLE             NbPool;
    NDIS_HANDLE             InterruptHandle;

    // MSI-X
    ULONG                   MsixVectorCount;
    PIO_INTERRUPT_MESSAGE_INFO MsixMessageInfo;
    PKINTERRUPT             MsixInterrupts[E1000_MSIX_VECTOR_COUNT];

    // Queues (for RSS)
    E1000_RX_QUEUE          RxQueues[E1000_MAX_RX_QUEUES];
    E1000_TX_QUEUE          TxQueues[E1000_MAX_TX_QUEUES];

    // ... (rest of adapter context)
} E1000_ADAPTER, *PE1000_ADAPTER;
```

**Subtasks:**
- [ ] Define E1000_ADAPTER structure for NDIS 6.x
- [ ] Implement E1000MiniportInitializeEx
- [ ] Implement hardware resource mapping
- [ ] Implement NBL/NB pool allocation
- [ ] Implement adapter registration attributes

---

#### Task 1.3: Implement MiniportHaltEx
**File:** `init.c`

**Responsibilities:**
1. Disable interrupts
2. Stop DMA engines
3. Disconnect MSI-X interrupts
4. Free NBL pools
5. Unmap hardware resources
6. Free adapter context

**Subtasks:**
- [ ] Implement E1000MiniportHaltEx
- [ ] Ensure proper resource cleanup order
- [ ] Handle pending operations during halt

---

### Phase 2: NET_BUFFER_LIST Send/Receive Path

#### Task 2.1: Implement SendNetBufferLists
**File:** `send.c` (rewrite)

**Current (NDIS 5.x):**
```c
NDIS_STATUS MiniportSend(
    NDIS_HANDLE MiniportAdapterContext,
    PNDIS_PACKET Packet,
    UINT Flags);
```

**Target (NDIS 6.x):**
```c
VOID E1000SendNetBufferLists(
    NDIS_HANDLE MiniportAdapterContext,
    PNET_BUFFER_LIST NetBufferList,
    NDIS_PORT_NUMBER PortNumber,
    ULONG SendFlags)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST CurrentNbl;
    PNET_BUFFER CurrentNb;
    NDIS_STATUS Status;

    for (CurrentNbl = NetBufferList; CurrentNbl != NULL;
         CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
    {
        for (CurrentNb = NET_BUFFER_LIST_FIRST_NB(CurrentNbl);
             CurrentNb != NULL;
             CurrentNb = NET_BUFFER_NEXT_NB(CurrentNb))
        {
            Status = E1000TransmitNetBuffer(Adapter, CurrentNbl, CurrentNb);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                NET_BUFFER_LIST_STATUS(CurrentNbl) = Status;
                break;
            }
        }
    }

    // Process completions or queue for DPC
    if (!(SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL))
    {
        E1000ProcessTxCompletions(Adapter);
    }
}
```

**TX Descriptor Management:**
```c
typedef struct _E1000_TX_BUFFER {
    PNET_BUFFER_LIST    Nbl;
    PNET_BUFFER         Nb;
    ULONG               FirstDescriptor;
    ULONG               DescriptorCount;
    SCATTER_GATHER_LIST *SgList;
} E1000_TX_BUFFER, *PE1000_TX_BUFFER;

typedef struct _E1000_TX_QUEUE {
    // Descriptor ring
    PE1000_TRANSMIT_DESCRIPTOR  TxDescRing;
    PHYSICAL_ADDRESS            TxDescRingPa;
    ULONG                       TxDescCount;

    // Tracking
    PE1000_TX_BUFFER            TxBuffers;
    ULONG                       TxHead;     // Next to complete
    ULONG                       TxTail;     // Next to use
    ULONG                       TxFree;     // Free descriptors

    // Context descriptor for offloads
    BOOLEAN                     ContextValid;
    E1000_CONTEXT_DESCRIPTOR    CurrentContext;

    // Lock
    NDIS_SPIN_LOCK              TxLock;
} E1000_TX_QUEUE, *PE1000_TX_QUEUE;
```

**Subtasks:**
- [ ] Define E1000_TX_BUFFER tracking structure
- [ ] Define E1000_TX_QUEUE structure
- [ ] Implement E1000SendNetBufferLists
- [ ] Implement E1000TransmitNetBuffer (single NB processing)
- [ ] Implement scatter-gather handling for NET_BUFFER
- [ ] Implement context descriptor for checksum offload
- [ ] Implement E1000ProcessTxCompletions
- [ ] Implement send completion via NdisMSendNetBufferListsComplete

---

#### Task 2.2: Implement Receive Path with NBL
**File:** `receive.c` (new)

**Key Functions:**
```c
// Pre-allocate receive NBLs
NDIS_STATUS E1000AllocateRxResources(PE1000_ADAPTER Adapter);

// Receive indication
VOID E1000IndicateReceiveNetBufferLists(
    PE1000_ADAPTER Adapter,
    PE1000_RX_QUEUE RxQueue);

// Return handler
VOID E1000ReturnNetBufferLists(
    NDIS_HANDLE MiniportAdapterContext,
    PNET_BUFFER_LIST NetBufferLists,
    ULONG ReturnFlags);
```

**RX Buffer Management:**
```c
typedef struct _E1000_RX_BUFFER {
    PNET_BUFFER_LIST    Nbl;
    PNET_BUFFER         Nb;
    PMDL                Mdl;
    PVOID               VirtualAddress;
    PHYSICAL_ADDRESS    PhysicalAddress;
    ULONG               BufferSize;
} E1000_RX_BUFFER, *PE1000_RX_BUFFER;

typedef struct _E1000_RX_QUEUE {
    // Descriptor ring
    PE1000_RECEIVE_DESCRIPTOR   RxDescRing;
    PHYSICAL_ADDRESS            RxDescRingPa;
    ULONG                       RxDescCount;

    // Buffers
    PE1000_RX_BUFFER            RxBuffers;
    ULONG                       RxHead;     // Next to check
    ULONG                       RxTail;     // Last returned to HW

    // NBL pool for this queue
    NDIS_HANDLE                 RxNblPool;

    // Statistics
    ULONG64                     RxPackets;
    ULONG64                     RxBytes;
    ULONG64                     RxErrors;

    // Lock
    NDIS_SPIN_LOCK              RxLock;

    // RSS queue index
    ULONG                       QueueIndex;
} E1000_RX_QUEUE, *PE1000_RX_QUEUE;
```

**Subtasks:**
- [ ] Define E1000_RX_BUFFER structure
- [ ] Define E1000_RX_QUEUE structure
- [ ] Implement E1000AllocateRxResources
- [ ] Implement E1000FreeRxResources
- [ ] Implement E1000IndicateReceiveNetBufferLists
- [ ] Implement E1000ReturnNetBufferLists
- [ ] Implement per-packet checksum status reporting
- [ ] Implement RSS hash reporting in NBL OOB data

---

### Phase 3: MSI-X Interrupt Implementation

#### Task 3.1: MSI-X Initialization via IoConnectInterruptEx
**File:** `interrupt.c` (rewrite)

**Current (Legacy):**
```c
NdisMRegisterInterrupt(&Adapter->Interrupt, ...);
```

**Target (MSI-X):**
```c
NDIS_STATUS E1000RegisterMsixInterrupts(PE1000_ADAPTER Adapter)
{
    NTSTATUS Status;
    IO_CONNECT_INTERRUPT_PARAMETERS ConnectParams;
    ULONG i;

    // First, get MSI-X info from PCI
    Status = E1000GetMsixInfo(Adapter);
    if (!NT_SUCCESS(Status))
    {
        // Fall back to MSI or line-based
        return E1000RegisterMsiInterrupt(Adapter);
    }

    // Connect each MSI-X vector
    for (i = 0; i < Adapter->MsixVectorCount; i++)
    {
        RtlZeroMemory(&ConnectParams, sizeof(ConnectParams));
        ConnectParams.Version = CONNECT_MESSAGE_BASED;
        ConnectParams.MessageBased.PhysicalDeviceObject = Adapter->PhysicalDeviceObject;
        ConnectParams.MessageBased.ConnectionContext.InterruptObject =
            &Adapter->MsixInterrupts[i];
        ConnectParams.MessageBased.MessageServiceRoutine = E1000MsixIsr;
        ConnectParams.MessageBased.ServiceContext = &Adapter->MsixContext[i];
        ConnectParams.MessageBased.SpinLock = NULL;
        ConnectParams.MessageBased.SynchronizeIrql = PASSIVE_LEVEL;
        ConnectParams.MessageBased.FloatingSave = FALSE;

        Status = IoConnectInterruptEx(&ConnectParams);
        if (!NT_SUCCESS(Status))
        {
            // Cleanup and fallback
            E1000DisconnectMsixInterrupts(Adapter, i);
            return E1000RegisterMsiInterrupt(Adapter);
        }
    }

    // Configure IVAR registers
    E1000ConfigureIvarRegisters(Adapter);

    Adapter->InterruptMode = E1000_INTERRUPT_MODE_MSIX;
    return NDIS_STATUS_SUCCESS;
}
```

**MSI-X ISR:**
```c
BOOLEAN E1000MsixIsr(
    PKINTERRUPT Interrupt,
    PVOID ServiceContext,
    ULONG MessageId)
{
    PE1000_MSIX_CONTEXT Context = (PE1000_MSIX_CONTEXT)ServiceContext;
    PE1000_ADAPTER Adapter = Context->Adapter;

    switch (MessageId)
    {
    case E1000_MSIX_VECTOR_RXQ0:
        // Queue RX DPC for queue 0
        KeInsertQueueDpc(&Adapter->RxQueues[0].RxDpc, NULL, NULL);
        break;

    case E1000_MSIX_VECTOR_TXQ0:
        // Queue TX completion DPC for queue 0
        KeInsertQueueDpc(&Adapter->TxQueues[0].TxDpc, NULL, NULL);
        break;

    case E1000_MSIX_VECTOR_OTHER:
        // Handle link status, PHY, etc.
        KeInsertQueueDpc(&Adapter->OtherDpc, NULL, NULL);
        break;

    // ... other vectors
    }

    return TRUE;
}
```

**Subtasks:**
- [ ] Implement E1000GetMsixInfo (parse PCI MSI-X capability)
- [ ] Implement E1000RegisterMsixInterrupts
- [ ] Implement E1000DisconnectMsixInterrupts
- [ ] Implement E1000MsixIsr (per-vector ISR)
- [ ] Implement E1000ConfigureIvarRegisters
- [ ] Implement fallback to MSI (IoConnectInterruptEx MESSAGE_BASED)
- [ ] Implement fallback to line-based (IoConnectInterruptEx LINE_BASED)
- [ ] Implement per-queue DPCs for RX/TX processing

---

#### Task 3.2: Implement NDIS 6.x Interrupt DPC Model
**File:** `interrupt.c`

**Structures:**
```c
typedef struct _E1000_MSIX_CONTEXT {
    PE1000_ADAPTER      Adapter;
    ULONG               VectorIndex;
    ULONG               QueueIndex;
    KDPC                Dpc;
    ULONG64             InterruptCount;
} E1000_MSIX_CONTEXT, *PE1000_MSIX_CONTEXT;
```

**DPC Implementation:**
```c
VOID E1000RxQueueDpc(
    PKDPC Dpc,
    PVOID DeferredContext,
    PVOID SystemArgument1,
    PVOID SystemArgument2)
{
    PE1000_RX_QUEUE RxQueue = (PE1000_RX_QUEUE)DeferredContext;
    PE1000_ADAPTER Adapter = RxQueue->Adapter;

    // Process received packets
    E1000IndicateReceiveNetBufferLists(Adapter, RxQueue);

    // Re-enable interrupts for this queue
    E1000EnableQueueInterrupt(Adapter, RxQueue->QueueIndex, TRUE);
}
```

**Subtasks:**
- [ ] Define E1000_MSIX_CONTEXT structure
- [ ] Implement E1000RxQueueDpc
- [ ] Implement E1000TxQueueDpc
- [ ] Implement E1000OtherDpc (link status, errors)
- [ ] Implement interrupt enable/disable per vector
- [ ] Implement interrupt coalescing configuration

---

### Phase 4: Hardware Offloads

#### Task 4.1: Checksum Offload (NDIS 6.x Style)
**File:** `offload.c` (new)

**82574L Checksum Capabilities:**
- RX: IP, TCP, UDP checksum verification
- TX: IP, TCP, UDP checksum insertion (via context descriptors)

**NDIS 6.x Offload Registration:**
```c
VOID E1000SetOffloadAttributes(PE1000_ADAPTER Adapter)
{
    NDIS_OFFLOAD HardwareOffload;
    NDIS_OFFLOAD DefaultOffload;
    NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES OffloadAttributes;

    NdisZeroMemory(&HardwareOffload, sizeof(HardwareOffload));
    HardwareOffload.Header.Type = NDIS_OBJECT_TYPE_OFFLOAD;
    HardwareOffload.Header.Revision = NDIS_OFFLOAD_REVISION_3;
    HardwareOffload.Header.Size = NDIS_SIZEOF_NDIS_OFFLOAD_REVISION_3;

    // IPv4 Checksum
    HardwareOffload.Checksum.IPv4Receive.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    HardwareOffload.Checksum.IPv4Receive.IpChecksum = TRUE;
    HardwareOffload.Checksum.IPv4Receive.IpOptionsSupported = TRUE;
    HardwareOffload.Checksum.IPv4Receive.TcpChecksum = TRUE;
    HardwareOffload.Checksum.IPv4Receive.TcpOptionsSupported = TRUE;
    HardwareOffload.Checksum.IPv4Receive.UdpChecksum = TRUE;

    HardwareOffload.Checksum.IPv4Transmit.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    HardwareOffload.Checksum.IPv4Transmit.IpChecksum = TRUE;
    HardwareOffload.Checksum.IPv4Transmit.IpOptionsSupported = TRUE;
    HardwareOffload.Checksum.IPv4Transmit.TcpChecksum = TRUE;
    HardwareOffload.Checksum.IPv4Transmit.TcpOptionsSupported = TRUE;
    HardwareOffload.Checksum.IPv4Transmit.UdpChecksum = TRUE;

    // TCP Large Send Offload v1 (82574L supports TSO)
    HardwareOffload.LsoV1.IPv4.Encapsulation = NDIS_ENCAPSULATION_IEEE_802_3;
    HardwareOffload.LsoV1.IPv4.MaxOffLoadSize = 64000;
    HardwareOffload.LsoV1.IPv4.MinSegmentCount = 2;
    HardwareOffload.LsoV1.IPv4.TcpOptions = TRUE;
    HardwareOffload.LsoV1.IPv4.IpOptions = TRUE;

    // Set attributes
    NdisZeroMemory(&OffloadAttributes, sizeof(OffloadAttributes));
    OffloadAttributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES;
    OffloadAttributes.Header.Revision = NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1;
    OffloadAttributes.Header.Size = sizeof(OffloadAttributes);
    OffloadAttributes.HardwareOffloadCapabilities = &HardwareOffload;
    OffloadAttributes.DefaultOffloadConfiguration = &DefaultOffload;

    NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&OffloadAttributes);
}
```

**Context Descriptor for TX Offload:**
```c
VOID E1000SetupContextDescriptor(
    PE1000_TX_QUEUE TxQueue,
    PNET_BUFFER_LIST Nbl,
    PNDIS_TCP_IP_CHECKSUM_NET_BUFFER_LIST_INFO CsumInfo)
{
    PE1000_CONTEXT_DESCRIPTOR CtxDesc;
    ULONG CmdTypeLen = 0;

    CtxDesc = (PE1000_CONTEXT_DESCRIPTOR)&TxQueue->TxDescRing[TxQueue->TxTail];

    // IP checksum setup
    if (CsumInfo->Transmit.IsIPv4 && CsumInfo->Transmit.IpHeaderChecksum)
    {
        CtxDesc->IPCSS = E1000_CSUM_IP_START;   // 14 (after Ethernet header)
        CtxDesc->IPCSO = E1000_CSUM_IP_OFFSET;  // 24 (IP checksum field)
        CtxDesc->IPCSE = 0;                      // End of IP header
        CmdTypeLen |= E1000_TXD_CMD_IP;
    }

    // TCP/UDP checksum setup
    if (CsumInfo->Transmit.TcpChecksum || CsumInfo->Transmit.UdpChecksum)
    {
        CtxDesc->TUCSS = E1000_CSUM_TCP_START;  // After IP header
        CtxDesc->TUCSO = CsumInfo->Transmit.TcpChecksum ?
                         E1000_CSUM_TCP_OFFSET : E1000_CSUM_UDP_OFFSET;
        CtxDesc->TUCSE = 0;
        CmdTypeLen |= CsumInfo->Transmit.TcpChecksum ?
                      E1000_TXD_CMD_TCP : 0;
    }

    CmdTypeLen |= E1000_TXD_CMD_DEXT | E1000_TXD_DTYP_C;
    CtxDesc->CmdTypeLen = CmdTypeLen;
    CtxDesc->Status = 0;
    CtxDesc->HdrLen = 0;
    CtxDesc->MSS = 0;

    TxQueue->TxTail = (TxQueue->TxTail + 1) % TxQueue->TxDescCount;
    TxQueue->ContextValid = TRUE;
}
```

**Subtasks:**
- [ ] Implement E1000SetOffloadAttributes
- [ ] Implement E1000SetupContextDescriptor
- [ ] Implement TX checksum offload via context descriptors
- [ ] Implement RX checksum status reporting in NBL OOB
- [ ] Implement OID_TCP_OFFLOAD_PARAMETERS handling
- [ ] Implement LSO (TCP Segmentation Offload) support

---

#### Task 4.2: Receive Side Scaling (RSS)
**File:** `rss.c` (new)

**82574L RSS Capabilities:**
- 2 RX queues
- Toeplitz hash function
- 128-entry indirection table
- Support for IPv4/IPv6 TCP/UDP

**RSS Configuration:**
```c
typedef struct _E1000_RSS_CONFIG {
    BOOLEAN                 Enabled;
    ULONG                   HashFunction;       // Toeplitz
    ULONG                   HashType;           // IPv4, TCP, etc.
    UCHAR                   HashSecretKey[40];  // RSS key
    UCHAR                   IndirectionTable[128];
    PROCESSOR_NUMBER        QueueToProcessor[E1000_MAX_RX_QUEUES];
} E1000_RSS_CONFIG, *PE1000_RSS_CONFIG;

NDIS_STATUS E1000SetRssParameters(
    PE1000_ADAPTER Adapter,
    PNDIS_RECEIVE_SCALE_PARAMETERS RssParams)
{
    ULONG i;
    ULONG MrqcValue = 0;

    // Program RSS hash key (RSSRK registers)
    for (i = 0; i < 10; i++)
    {
        E1000WriteUlong(Adapter, E1000_REG_RSSRK(i),
            *(PULONG)&RssParams->HashSecretKey[i * 4]);
    }

    // Program indirection table (RETA registers)
    for (i = 0; i < 32; i++)
    {
        ULONG RetaValue = 0;
        RetaValue |= (RssParams->IndirectionTable[i * 4 + 0] & 0x1) << 0;
        RetaValue |= (RssParams->IndirectionTable[i * 4 + 1] & 0x1) << 8;
        RetaValue |= (RssParams->IndirectionTable[i * 4 + 2] & 0x1) << 16;
        RetaValue |= (RssParams->IndirectionTable[i * 4 + 3] & 0x1) << 24;
        E1000WriteUlong(Adapter, E1000_REG_RETA(i), RetaValue);
    }

    // Configure MRQC (Multiple Receive Queues Command)
    MrqcValue |= E1000_MRQC_ENABLE_RSS_2Q;
    if (RssParams->HashType & NDIS_HASH_IPV4)
        MrqcValue |= E1000_MRQC_RSS_FIELD_IPV4;
    if (RssParams->HashType & NDIS_HASH_TCP_IPV4)
        MrqcValue |= E1000_MRQC_RSS_FIELD_IPV4_TCP;
    // ... more hash types

    E1000WriteUlong(Adapter, E1000_REG_MRQC, MrqcValue);

    Adapter->RssConfig.Enabled = TRUE;
    return NDIS_STATUS_SUCCESS;
}
```

**Subtasks:**
- [ ] Define E1000_RSS_CONFIG structure
- [ ] Implement E1000InitializeRss
- [ ] Implement E1000SetRssParameters
- [ ] Implement RSS hash key programming (RSSRK)
- [ ] Implement indirection table programming (RETA)
- [ ] Implement MRQC configuration
- [ ] Implement RSS hash reporting in RX NBL OOB
- [ ] Handle OID_GEN_RECEIVE_SCALE_PARAMETERS

---

### Phase 5: Power Management

#### Task 5.1: NDIS 6.x Power Management
**File:** `power.c` (new)

**Power Management Capabilities:**
```c
VOID E1000SetPowerManagementCapabilities(PE1000_ADAPTER Adapter)
{
    NDIS_PM_CAPABILITIES PmCapabilities;

    NdisZeroMemory(&PmCapabilities, sizeof(PmCapabilities));
    PmCapabilities.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PmCapabilities.Header.Revision = NDIS_PM_CAPABILITIES_REVISION_2;
    PmCapabilities.Header.Size = NDIS_SIZEOF_NDIS_PM_CAPABILITIES_REVISION_2;

    // Supported power states
    PmCapabilities.SupportedWoLPacketPatterns =
        NDIS_PM_WOL_BITMAP_PATTERN_SUPPORTED |
        NDIS_PM_WOL_MAGIC_PACKET_SUPPORTED |
        NDIS_PM_WOL_IPV4_DEST_ADDR_WILDCARD_SUPPORTED;

    PmCapabilities.NumTotalWoLPatterns = 4;
    PmCapabilities.MaxWoLPatternSize = 128;
    PmCapabilities.MaxWoLPatternOffset = 0;

    // Wake on Magic Packet
    PmCapabilities.SupportedWakeUpEvents =
        NDIS_PM_WAKE_ON_LINK_CHANGE_ENABLED |
        NDIS_PM_WAKE_ON_MAGIC_PACKET_ENABLED;

    // Report via miniport attributes
    // ...
}
```

**Subtasks:**
- [ ] Implement E1000SetPowerManagementCapabilities
- [ ] Implement E1000SetWakeUpPattern (OID_PM_ADD_WOL_PATTERN)
- [ ] Implement E1000RemoveWakeUpPattern (OID_PM_REMOVE_WOL_PATTERN)
- [ ] Implement E1000EnableWakeOnLan
- [ ] Implement power state transitions (D0 ↔ D3)
- [ ] Save/restore hardware state across power transitions

---

### Phase 6: OID Handling

#### Task 6.1: Implement NDIS 6.x OID Request Handler
**File:** `oid.c` (new)

**OID Request Structure:**
```c
NDIS_STATUS E1000OidRequest(
    NDIS_HANDLE MiniportAdapterContext,
    PNDIS_OID_REQUEST OidRequest)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status;

    switch (OidRequest->RequestType)
    {
    case NdisRequestQueryInformation:
    case NdisRequestQueryStatistics:
        Status = E1000QueryInformation(Adapter, OidRequest);
        break;

    case NdisRequestSetInformation:
        Status = E1000SetInformation(Adapter, OidRequest);
        break;

    case NdisRequestMethod:
        Status = E1000MethodRequest(Adapter, OidRequest);
        break;

    default:
        Status = NDIS_STATUS_NOT_SUPPORTED;
    }

    return Status;
}
```

**Key OIDs to Implement:**
| OID | Type | Description |
|-----|------|-------------|
| OID_GEN_STATISTICS | Query | NDIS_STATISTICS_INFO |
| OID_GEN_INTERRUPT_MODERATION | Query/Set | Interrupt coalescing |
| OID_GEN_RECEIVE_SCALE_PARAMETERS | Set | RSS configuration |
| OID_TCP_OFFLOAD_PARAMETERS | Set | Offload enable/disable |
| OID_PM_ADD_WOL_PATTERN | Set | Wake pattern |
| OID_PM_PARAMETERS | Set | Power management |
| OID_GEN_RECEIVE_HASH | Query/Set | RSS hash |

**Subtasks:**
- [ ] Implement E1000OidRequest dispatcher
- [ ] Implement E1000QueryInformation
- [ ] Implement E1000SetInformation
- [ ] Implement all required NDIS 6.x OIDs
- [ ] Implement statistics (NDIS_STATISTICS_INFO)
- [ ] Implement E1000CancelOidRequest

---

### Phase 7: Pause/Restart and Flow Control

#### Task 7.1: Implement Pause/Restart Handlers
**File:** `control.c` (new)

```c
NDIS_STATUS E1000MiniportPause(
    NDIS_HANDLE MiniportAdapterContext,
    PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;

    // Stop accepting new sends
    InterlockedExchange(&Adapter->SendState, E1000_SEND_PAUSED);

    // Wait for pending sends to complete
    E1000WaitForPendingSends(Adapter);

    // Disable RX
    E1000DisableReceive(Adapter);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS E1000MiniportRestart(
    NDIS_HANDLE MiniportAdapterContext,
    PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;

    // Re-enable RX
    E1000EnableReceive(Adapter);

    // Allow sends
    InterlockedExchange(&Adapter->SendState, E1000_SEND_RUNNING);

    return NDIS_STATUS_SUCCESS;
}
```

**Subtasks:**
- [ ] Implement E1000MiniportPause
- [ ] Implement E1000MiniportRestart
- [ ] Implement send state tracking
- [ ] Implement graceful pending operation handling

---

## 4. File Structure (New)

```
drivers/network/dd/e1000/
├── e1000.h              # Main header (adapter structure, constants)
├── e1000hw.h            # Hardware register definitions (existing, updated)
├── debug.h              # Debug macros and structures
├── init.c               # MiniportInitializeEx, MiniportHaltEx
├── send.c               # SendNetBufferLists, TX completion
├── receive.c            # RX indication, ReturnNetBufferLists
├── interrupt.c          # MSI-X, ISR, DPC handlers
├── oid.c                # OID request handling
├── offload.c            # Checksum, LSO offload
├── rss.c                # Receive Side Scaling
├── power.c              # Power management
├── control.c            # Pause/Restart, reset
├── hardware.c           # Low-level hardware access
├── debug.c              # Debug utilities
├── e1000.rc             # Version resource
├── nete1000.inf         # INF file
└── CMakeLists.txt       # Build configuration
```

---

## 5. Build Configuration Changes

### CMakeLists.txt Updates:
```cmake
# Update NDIS version
add_definitions(
    -DNDIS630_MINIPORT=1
    -DNDIS_MINIPORT_DRIVER=1
    -DNDIS_WDM=1
)

# Remove legacy defines
# -DNDIS50_MINIPORT (remove)
# -DNDIS_LEGACY_MINIPORT (remove)

# Add new source files
list(APPEND SOURCE
    init.c
    send.c
    receive.c
    interrupt.c
    oid.c
    offload.c
    rss.c
    power.c
    control.c
    hardware.c
    debug.c
)
```

---

## 6. Testing Plan

### 6.1 Unit Tests
| Test | Description |
|------|-------------|
| MSI-X Vector Registration | Verify all 5 vectors connect |
| NBL Pool Allocation | Verify RX/TX NBL pools |
| Descriptor Ring Init | Verify 256-entry rings |
| Context Descriptor | Verify checksum offload setup |

### 6.2 Functional Tests
| Test | Description |
|------|-------------|
| Basic TX/RX | Ping, iperf baseline |
| Checksum Offload | Verify HW checksum with captures |
| MSI-X Interrupts | Verify per-queue interrupt delivery |
| RSS | Verify traffic distribution across queues |
| Power Management | Verify D0/D3 transitions, WoL |
| Pause/Restart | Verify graceful handling |

### 6.3 Performance Tests
| Test | Target |
|------|--------|
| TCP Throughput | >900 Mbps |
| UDP Throughput | >950 Mbps |
| Latency | <100µs |
| CPU Utilization | <30% at line rate |

---

## 7. Dependencies

### 7.1 ReactOS NDIS 6.x Requirements
- NDIS 6.30+ wrapper implementation
- NET_BUFFER_LIST support
- NdisMRegisterMiniportDriver API
- NdisMIndicateReceiveNetBufferLists API
- IoConnectInterruptEx support

### 7.2 HAL Requirements
- MSI-X interrupt support
- IoConnectInterruptEx with CONNECT_MESSAGE_BASED

---

## 8. Risk Assessment

| Risk | Impact | Mitigation |
|------|--------|------------|
| ReactOS NDIS 6.x incomplete | High | Implement missing NDIS APIs as needed |
| MSI-X HAL support missing | Medium | Fall back to MSI or line-based |
| RSS not fully supported | Low | Single queue still works |
| LSO complexity | Medium | Implement checksum first, LSO later |

---

## 9. Timeline Estimate

| Phase | Tasks | Complexity |
|-------|-------|------------|
| Phase 1 | Core NDIS 6.x Infrastructure | High |
| Phase 2 | NBL Send/Receive | High |
| Phase 3 | MSI-X Interrupts | High |
| Phase 4 | Hardware Offloads | Medium |
| Phase 5 | Power Management | Medium |
| Phase 6 | OID Handling | Medium |
| Phase 7 | Pause/Restart | Low |

---

## 10. References

1. Intel 82574L Datasheet (316398)
2. NDIS 6.30 Miniport Driver Design Guide (Microsoft)
3. Network Driver Interface Specification 6.x (Microsoft)
4. Windows Driver Kit Documentation
5. ReactOS NDIS Implementation Source

---

## Appendix A: 82574L Register Quick Reference

### Key Registers for NDIS 6.x Features

| Register | Offset | Purpose |
|----------|--------|---------|
| CTRL | 0x0000 | Device Control |
| STATUS | 0x0008 | Device Status |
| EERD | 0x0014 | EEPROM Read |
| ICR | 0x00C0 | Interrupt Cause Read |
| IMS | 0x00D0 | Interrupt Mask Set |
| IMC | 0x00D8 | Interrupt Mask Clear |
| RCTL | 0x0100 | Receive Control |
| TCTL | 0x0400 | Transmit Control |
| RDBAL/H | 0x2800/4 | RX Descriptor Base |
| TDBAL/H | 0x3800/4 | TX Descriptor Base |
| RXCSUM | 0x5000 | RX Checksum Control |
| MRQC | 0x5818 | Multiple RX Queues |
| IVAR0 | 0x1700 | Interrupt Vector Allocation |
| IVAR_MISC | 0x1740 | Miscellaneous IVAR |
| EITR[0-4] | 0x1680+ | Extended Interrupt Throttle |

---

*End of Specification Document*
