/*
 * PROJECT:     ReactOS RP1 Ethernet Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * FILE:        drivers/network/dd/rp1gem/rp1gem.c
 * PURPOSE:     Raspberry Pi 5 RP1 Cadence GEM NDIS 6.20 miniport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rp1gem.h"

static NDIS_HANDLE Rp1GemDriverHandle;

static VOID
Rp1GemDrainTxCompletions(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG CompleteFlags);

static VOID
Rp1GemPollReceive(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG Budget);

static BOOLEAN
Rp1GemReceivePending(
    _In_ PRP1GEM_ADAPTER Adapter);

static VOID
Rp1GemWriteDiag(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG Stage,
    _In_ NDIS_STATUS LastStatus);

static const NDIS_OID Rp1GemSupportedOids[] =
{
    OID_GEN_SUPPORTED_LIST,
    OID_GEN_HARDWARE_STATUS,
    OID_GEN_MEDIA_SUPPORTED,
    OID_GEN_MEDIA_IN_USE,
    OID_GEN_MAXIMUM_LOOKAHEAD,
    OID_GEN_MAXIMUM_FRAME_SIZE,
    OID_GEN_LINK_SPEED,
    OID_GEN_TRANSMIT_BLOCK_SIZE,
    OID_GEN_RECEIVE_BLOCK_SIZE,
    OID_GEN_VENDOR_ID,
    OID_GEN_VENDOR_DESCRIPTION,
    OID_GEN_CURRENT_PACKET_FILTER,
    OID_GEN_CURRENT_LOOKAHEAD,
    OID_GEN_DRIVER_VERSION,
    OID_GEN_MAXIMUM_TOTAL_SIZE,
    OID_GEN_MAC_OPTIONS,
    OID_GEN_MEDIA_CONNECT_STATUS,
    OID_GEN_VENDOR_DRIVER_VERSION,
    OID_GEN_PHYSICAL_MEDIUM,
    OID_GEN_LINK_STATE,
    OID_GEN_XMIT_OK,
    OID_GEN_RCV_OK,
    OID_GEN_XMIT_ERROR,
    OID_GEN_RCV_ERROR,
    OID_GEN_RCV_NO_BUFFER,
    OID_802_3_PERMANENT_ADDRESS,
    OID_802_3_CURRENT_ADDRESS,
    OID_802_3_MULTICAST_LIST,
    OID_802_3_MAXIMUM_LIST_SIZE
};

static __inline ULONG
Rp1GemRead32(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->RegisterBase + Offset));
}

static __inline VOID
Rp1GemWrite32(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->RegisterBase + Offset), Value);
}

static BOOLEAN
Rp1GemIsValidMacAddress(
    _In_reads_(ETH_LENGTH_OF_ADDRESS) const UCHAR *MacAddress)
{
    ULONG i;
    BOOLEAN AllZero = TRUE;
    BOOLEAN AllOnes = TRUE;

    for (i = 0; i < ETH_LENGTH_OF_ADDRESS; i++)
    {
        if (MacAddress[i] != 0x00)
            AllZero = FALSE;
        if (MacAddress[i] != 0xff)
            AllOnes = FALSE;
    }

    return !AllZero && !AllOnes && !(MacAddress[0] & 0x01);
}

static PMDL
Rp1GemAllocateMdl(
    _In_ PVOID VirtualAddress,
    _In_ ULONG Length)
{
    PMDL Mdl;

    Mdl = IoAllocateMdl(VirtualAddress, Length, FALSE, FALSE, NULL);
    if (Mdl)
        MmBuildMdlForNonPagedPool(Mdl);

    return Mdl;
}

static VOID
Rp1GemSetDescriptorAddress(
    _In_ PRP1GEM_ADAPTER Adapter,
    _Inout_ PRP1GEM_DMA_DESCRIPTOR Descriptor,
    _In_ NDIS_PHYSICAL_ADDRESS PhysicalAddress,
    _In_ ULONG AddressFlags)
{
    Descriptor->AddressHigh = Adapter->Dma64Bit ? PhysicalAddress.HighPart : 0;
    KeMemoryBarrier();
    Descriptor->Address = PhysicalAddress.LowPart | AddressFlags;
}

static VOID
Rp1GemDcacheInvalidate(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    ULONG_PTR Addr = (ULONG_PTR)Base & ~(ULONG_PTR)63;
    ULONG_PTR End = (ULONG_PTR)Base + Length;

    __asm__ __volatile__("dsb ish" ::: "memory");
    while (Addr < End)
    {
        __asm__ __volatile__("dc ivac, %0" :: "r"(Addr) : "memory");
        Addr += 64;
    }
    __asm__ __volatile__("dsb ish" ::: "memory");
}

static VOID
Rp1GemDcacheClean(
    _In_ PVOID Base,
    _In_ SIZE_T Length)
{
    ULONG_PTR Addr = (ULONG_PTR)Base & ~(ULONG_PTR)63;
    ULONG_PTR End = (ULONG_PTR)Base + Length;

    __asm__ __volatile__("dsb ish" ::: "memory");
    while (Addr < End)
    {
        __asm__ __volatile__("dc cvac, %0" :: "r"(Addr) : "memory");
        Addr += 64;
    }
    __asm__ __volatile__("dsb ish" ::: "memory");
}

static VOID
Rp1GemRearmRxDescriptor(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG Index)
{
    ULONG Flags = (Index == (RP1GEM_RX_RING_SIZE - 1)) ? MACB_RX_WRAP : 0;

    Rp1GemDcacheInvalidate(Adapter->RxBuffers[Index].VirtualAddress, RP1GEM_BUFFER_SIZE);
    Adapter->RxRing[Index].Control = 0;
    KeMemoryBarrier();
    Rp1GemSetDescriptorAddress(Adapter,
                               &Adapter->RxRing[Index],
                               Adapter->RxBuffers[Index].PhysicalAddress,
                               Flags);
}

static ULONG
Rp1GemDiscardPendingReceive(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    ULONG Discarded = 0;

    if (!Adapter->DatapathReady || !Adapter->RxRing)
        return 0;

    NdisAcquireSpinLock(&Adapter->RxLock);
    while (Discarded < RP1GEM_RX_RING_SIZE)
    {
        ULONG Index = Adapter->RxTail;
        PRP1GEM_DMA_DESCRIPTOR Descriptor = &Adapter->RxRing[Index];

        KeMemoryBarrier();
        if (!(Descriptor->Address & MACB_RX_USED))
            break;

        Rp1GemRearmRxDescriptor(Adapter, Index);
        Adapter->RxTail = (Index + 1) % RP1GEM_RX_RING_SIZE;
        Discarded++;
    }
    NdisReleaseSpinLock(&Adapter->RxLock);

    if (Discarded)
    {
        Rp1GemWrite32(Adapter, MACB_RSR, MACB_RSR_ALL);
        DPRINT("RP1GEM: discarded %lu stale RX frames before interrupt enable\n",
               Discarded);
    }

    return Discarded;
}

static ULONG
Rp1GemBuildNcfgr(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    ULONG Ncfgr;

    Ncfgr = GEM_NCFGR_CLK_DIV96 | GEM_NCFGR_DBW128 | MACB_NCFGR_DRFCS | MACB_NCFGR_BIG | GEM_NCFGR_RXCOEN;

    if (Adapter->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
        Ncfgr |= MACB_NCFGR_CAF;

    if (Adapter->MediaConnectState == MediaConnectStateConnected)
    {
        if (Adapter->MediaDuplexState == MediaDuplexStateFull)
            Ncfgr |= MACB_NCFGR_FD;

        if (Adapter->LinkSpeed == RP1GEM_LINK_SPEED_1G)
            Ncfgr |= GEM_NCFGR_GBE;
        else if (Adapter->LinkSpeed == RP1GEM_LINK_SPEED_100M)
            Ncfgr |= MACB_NCFGR_SPD;
    }

    return Ncfgr;
}

static VOID
Rp1GemApplyMacConfiguration(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    Rp1GemWrite32(Adapter, MACB_NCFGR, Rp1GemBuildNcfgr(Adapter));
}

static VOID
Rp1GemSetRxTxEnabled(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ BOOLEAN Enable)
{
    ULONG Ncr;

    Ncr = Rp1GemRead32(Adapter, MACB_NCR);
    if (Enable)
        Ncr |= MACB_NCR_RE | MACB_NCR_TE;
    else
        Ncr &= ~(MACB_NCR_RE | MACB_NCR_TE | MACB_NCR_TSTART);
    Rp1GemWrite32(Adapter, MACB_NCR, Ncr);
}

static VOID
Rp1GemApplyLinkState(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    BOOLEAN EnableRxTx;

    EnableRxTx = Adapter->DatapathReady &&
                 Adapter->MediaConnectState == MediaConnectStateConnected;

    Rp1GemApplyMacConfiguration(Adapter);
    Rp1GemSetRxTxEnabled(Adapter, EnableRxTx);
}

static VOID
Rp1GemWriteMacAddress(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    ULONG Sa1b;
    ULONG Sa1t;

    Sa1b = ((ULONG)Adapter->CurrentMacAddress[0]) |
           ((ULONG)Adapter->CurrentMacAddress[1] << 8) |
           ((ULONG)Adapter->CurrentMacAddress[2] << 16) |
           ((ULONG)Adapter->CurrentMacAddress[3] << 24);
    Sa1t = ((ULONG)Adapter->CurrentMacAddress[4]) |
           ((ULONG)Adapter->CurrentMacAddress[5] << 8);

    Rp1GemWrite32(Adapter, GEM_SA1B, Sa1b);
    Rp1GemWrite32(Adapter, GEM_SA1T, Sa1t);
}

static VOID
Rp1GemEnableInterrupts(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    if (Adapter->RegisterBase)
    {
        Rp1GemWrite32(Adapter, MACB_IER, RP1GEM_INT_MASK);
    }
}

static VOID
Rp1GemClearInterruptStatus(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG Status)
{
    if (Adapter->InterruptClearOnWrite && Status)
        Rp1GemWrite32(Adapter, MACB_ISR, Status);
}

static VOID
Rp1GemDisableInterrupts(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    if (Adapter->RegisterBase)
        Rp1GemWrite32(Adapter, MACB_IDR, MACB_INT_ALL);
}

static VOID
Rp1GemStopHardware(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    ULONG Ncr;

    if (!Adapter->RegisterBase)
        return;

    Rp1GemDisableInterrupts(Adapter);
    Ncr = Rp1GemRead32(Adapter, MACB_NCR);
    Ncr &= ~(MACB_NCR_RE | MACB_NCR_TE | MACB_NCR_TSTART);
    Rp1GemWrite32(Adapter, MACB_NCR, Ncr);
    Rp1GemWrite32(Adapter, MACB_TSR, MACB_TSR_ALL);
    Rp1GemWrite32(Adapter, MACB_RSR, MACB_RSR_ALL);
}

static VOID
Rp1GemUnmapResources(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    if (Adapter->RegisterBase)
    {
        NdisMUnmapIoSpace(Adapter->MiniportHandle,
                          Adapter->RegisterBase,
                          Adapter->RegisterLength);
        Adapter->RegisterBase = NULL;
    }
}

static VOID
Rp1GemFreeDatapath(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    ULONG i;

    Adapter->DatapathReady = FALSE;

    for (i = 0; i < RP1GEM_TX_RING_SIZE; i++)
    {
        if (Adapter->TxBuffers[i].VirtualAddress)
        {
            MmFreeContiguousMemorySpecifyCache(Adapter->TxBuffers[i].VirtualAddress,
                                               RP1GEM_BUFFER_SIZE,
                                               MmCached);
            Adapter->TxBuffers[i].VirtualAddress = NULL;
        }
    }

    for (i = 0; i < RP1GEM_RX_RING_SIZE; i++)
    {
        if (Adapter->RxBuffers[i].NetBufferList)
        {
            NdisFreeNetBufferList(Adapter->RxBuffers[i].NetBufferList);
            Adapter->RxBuffers[i].NetBufferList = NULL;
            Adapter->RxBuffers[i].Indicated = FALSE;
        }
        if (Adapter->RxBuffers[i].Mdl)
        {
            IoFreeMdl(Adapter->RxBuffers[i].Mdl);
            Adapter->RxBuffers[i].Mdl = NULL;
        }
        if (Adapter->RxBuffers[i].VirtualAddress)
        {
            MmFreeContiguousMemorySpecifyCache(Adapter->RxBuffers[i].VirtualAddress,
                                               RP1GEM_BUFFER_SIZE,
                                               MmCached);
            Adapter->RxBuffers[i].VirtualAddress = NULL;
        }
    }

    if (Adapter->RxNblPool)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
        Adapter->RxNblPool = NULL;
    }

    if (Adapter->RxRing)
    {
        MmFreeContiguousMemorySpecifyCache(Adapter->RxRing,
                                           Adapter->RxRingLength,
                                           MmNonCached);
        Adapter->RxRing = NULL;
    }

    if (Adapter->TxRing)
    {
        MmFreeContiguousMemorySpecifyCache(Adapter->TxRing,
                                           Adapter->TxRingLength,
                                           MmNonCached);
        Adapter->TxRing = NULL;
    }
}

/*
 * Allocate a DMA-visible common buffer for descriptor rings / TX data.
 *
 * We use MmAllocateContiguousMemorySpecifyCache instead of
 * NdisMAllocateSharedMemory because Win11 ARM64 will not build a bus-master
 * DMA adapter object for a bare ACPI\PRP0001 node (it has no _CCA / DMA
 * properties), so NdisMAllocateSharedMemory fails there with
 * STATUS_INSUFFICIENT_RESOURCES (0xC000009A) -> MiniportInitializeEx fails ->
 * Code 10. ReactOS's NDIS is lenient and the same call succeeds, which is why
 * the identical .sys works on ROS but not Win11.
 *
 * The RP1 GEM DMA is cache-coherent and 1:1 mapped to system RAM on the RPi5
 * (the RX *data* buffers already use this exact MmAllocateContiguousMemory +
 * MmGetPhysicalAddress path and the link comes up at 1Gbps on ReactOS), so the
 * CPU physical address equals the device DMA address. MmNonCached keeps the
 * rings coherent without explicit cache maintenance, matching the previous
 * NdisMAllocateSharedMemory(Cached=FALSE) behaviour. This path is identical on
 * ReactOS and Win11, preserving parity.
 */
static PVOID
Rp1GemAllocateDmaCommonBuffer(
    _In_ ULONG Length,
    _Out_ PNDIS_PHYSICAL_ADDRESS Physical)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PVOID Va;

    Low.QuadPart = 0;
    High.QuadPart = ~0ULL;
    Boundary.QuadPart = 0;

    Va = MmAllocateContiguousMemorySpecifyCache(Length, Low, High, Boundary,
                                                MmNonCached);
    if (Va)
        *Physical = MmGetPhysicalAddress(Va);
    else
        Physical->QuadPart = 0;

    return Va;
}

/*
 * Allocate ONE DMA packet buffer per ring slot. RP1GEM_BUFFER_SIZE (1536) is
 * smaller than a page, so MmAllocateContiguousMemory is contractually
 * guaranteed to return a physically-contiguous block that does not cross a
 * page boundary -- without ever requesting a large contiguous run. The old
 * design allocated ONE 384KB contiguous area (1536 * 256) and sliced it; Win11
 * ARM64's fragmented allocator refuses that big run (STATUS_INSUFFICIENT_
 * RESOURCES), whereas per-slot single-page allocations always succeed. Buffers
 * need not be contiguous with each other -- each descriptor carries its own
 * buffer's physical address.
 *
 * Packet buffers are cached for CPU copy throughput. The driver performs
 * explicit cache maintenance before handing buffers to the GEM DMA engine.
 */
static PVOID
Rp1GemAllocateDmaPacketBuffer(
    _Out_ PNDIS_PHYSICAL_ADDRESS Physical)
{
    PHYSICAL_ADDRESS Low, High, Boundary;
    PVOID Va;

    Low.QuadPart = 0;
    High.QuadPart = ~0ULL;
    Boundary.QuadPart = 0;

    Va = MmAllocateContiguousMemorySpecifyCache(RP1GEM_BUFFER_SIZE, Low, High,
                                                Boundary, MmCached);
    if (Va)
        *Physical = MmGetPhysicalAddress(Va);
    else
        Physical->QuadPart = 0;

    return Va;
}

static NDIS_STATUS
Rp1GemAllocateReceivePath(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
    ULONG i;

    Adapter->DiagSubStage = 51;

    RtlZeroMemory(&PoolParams, sizeof(PoolParams));
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.PoolTag = RP1GEM_TAG;
    /*
     * DataSize MUST be 0: this driver attaches its OWN MDL/buffer to each
     * NET_BUFFER (see NdisAllocateNetBufferAndNetBufferList with an explicit
     * MdlChain below). The NDIS contract requires that when a pool is created
     * with a nonzero DataSize (pool-allocated data), the MdlChain passed at
     * allocation be NULL. Win11 enforces this and returns NULL for the
     * mismatched call (NBL alloc fails -> MiniportInitializeEx fails); ReactOS
     * is lenient and allowed DataSize=1536 + an explicit MdlChain. Zero works
     * on both -> parity preserved.
     */
    PoolParams.DataSize = 0;

    Adapter->RxNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportHandle,
                                                       &PoolParams);
    if (!Adapter->RxNblPool)
        return NDIS_STATUS_RESOURCES;

    Adapter->DiagSubStage = 52;
    Adapter->RxRingLength = sizeof(RP1GEM_DMA_DESCRIPTOR) * RP1GEM_RX_RING_SIZE;
    Adapter->RxRing = (PRP1GEM_DMA_DESCRIPTOR)
        Rp1GemAllocateDmaCommonBuffer(Adapter->RxRingLength,
                                      &Adapter->RxRingPhysical);
    if (!Adapter->RxRing)
        return NDIS_STATUS_RESOURCES;

    Adapter->DiagSubStage = 53;
    RtlZeroMemory(Adapter->RxRing, Adapter->RxRingLength);
    for (i = 0; i < RP1GEM_RX_RING_SIZE; i++)
    {
        Adapter->DiagLoopIndex = i;
        Adapter->DiagSubStage = 531;
        Adapter->RxBuffers[i].VirtualAddress =
            Rp1GemAllocateDmaPacketBuffer(&Adapter->RxBuffers[i].PhysicalAddress);
        if (!Adapter->RxBuffers[i].VirtualAddress)
            return NDIS_STATUS_RESOURCES;

        Adapter->DiagSubStage = 532;
        Adapter->RxBuffers[i].Mdl =
            Rp1GemAllocateMdl(Adapter->RxBuffers[i].VirtualAddress, RP1GEM_BUFFER_SIZE);
        if (!Adapter->RxBuffers[i].Mdl)
            return NDIS_STATUS_RESOURCES;
        Adapter->DiagSubStage = 533;
        Adapter->RxBuffers[i].NetBufferList =
            NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool, 0, 0,
                                                  Adapter->RxBuffers[i].Mdl, 0, 0);
        if (!Adapter->RxBuffers[i].NetBufferList)
            return NDIS_STATUS_RESOURCES;
        Adapter->RxBuffers[i].NetBufferList->SourceHandle = Adapter->MiniportHandle;

        Rp1GemRearmRxDescriptor(Adapter, i);
    }

    Adapter->RxTail = 0;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Rp1GemAllocateTransmitPath(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    ULONG i;

    Adapter->DiagSubStage = 55;
    Adapter->TxRingLength = sizeof(RP1GEM_DMA_DESCRIPTOR) * RP1GEM_TX_RING_SIZE;
    Adapter->TxRing = (PRP1GEM_DMA_DESCRIPTOR)
        Rp1GemAllocateDmaCommonBuffer(Adapter->TxRingLength,
                                      &Adapter->TxRingPhysical);
    if (!Adapter->TxRing)
        return NDIS_STATUS_RESOURCES;

    Adapter->DiagSubStage = 56;
    RtlZeroMemory(Adapter->TxRing, Adapter->TxRingLength);
    for (i = 0; i < RP1GEM_TX_RING_SIZE; i++)
    {
        ULONG Control = MACB_TX_USED;

        if (i == (RP1GEM_TX_RING_SIZE - 1))
            Control |= MACB_TX_WRAP;

        Adapter->DiagLoopIndex = i;
        Adapter->DiagSubStage = 561;
        Adapter->TxBuffers[i].VirtualAddress =
            Rp1GemAllocateDmaPacketBuffer(&Adapter->TxBuffers[i].PhysicalAddress);
        if (!Adapter->TxBuffers[i].VirtualAddress)
            return NDIS_STATUS_RESOURCES;
        Rp1GemSetDescriptorAddress(Adapter,
                                   &Adapter->TxRing[i],
                                   Adapter->TxBuffers[i].PhysicalAddress,
                                   0);
        Adapter->TxRing[i].Control = Control;
    }

    Adapter->TxHead = 0;
    Adapter->TxTail = 0;
    Adapter->TxFree = RP1GEM_TX_RING_SIZE;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Rp1GemInitializeDatapath(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    ULONG Dmacfg;
    ULONG Amp;
    ULONG Intmod;
    ULONG Ncr;

    if (!Adapter->Dma64Bit)
    {
        DPRINT1("RP1GEM: 64-bit GEM DMA descriptors are not available\n");
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    Status = Rp1GemAllocateReceivePath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Status = Rp1GemAllocateTransmitPath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
        goto Failure;

    Dmacfg = Rp1GemRead32(Adapter, GEM_DMACFG);
    Dmacfg &= ~(GEM_DMACFG_FBLDO_MASK |
                GEM_DMACFG_RXBS_MASK |
                GEM_DMACFG_ENDIA_DESC |
                GEM_DMACFG_ENDIA_PKT);
    Dmacfg |= GEM_DMACFG_FBLDO_INCR16 |
              GEM_DMACFG_RXBMS_FULL |
              GEM_DMACFG_TXPBMS |
              GEM_DMACFG_RXBS(RP1GEM_BUFFER_SIZE) |
              GEM_DMACFG_ADDR64;
    Rp1GemWrite32(Adapter, GEM_DMACFG, Dmacfg);

    Amp = Rp1GemRead32(Adapter, GEM_AMP);
    Amp &= ~(GEM_AMP_AR2R_MAX_PIPE_MASK |
             GEM_AMP_AW2W_MAX_PIPE_MASK |
             GEM_AMP_AW2B_FILL_MASK);
    Amp |= GEM_AMP_AR2R_MAX_PIPE(RP1GEM_AXI_MAX_PIPE) |
           GEM_AMP_AW2W_MAX_PIPE(RP1GEM_AXI_MAX_PIPE) |
           GEM_AMP_AW2B_FILL;
    Rp1GemWrite32(Adapter, GEM_AMP, Amp);

    /* Per-packet interrupts avoid delaying sparse RX traffic such as TCP ACKs. */
    Intmod = 0;
    Rp1GemWrite32(Adapter, GEM_INTMOD, Intmod);

    Rp1GemWrite32(Adapter, MACB_RBQPH, Adapter->RxRingPhysical.HighPart);
    Rp1GemWrite32(Adapter, MACB_TBQPH, Adapter->TxRingPhysical.HighPart);
    Rp1GemWrite32(Adapter, MACB_RBQP, Adapter->RxRingPhysical.LowPart);
    Rp1GemWrite32(Adapter, MACB_TBQP, Adapter->TxRingPhysical.LowPart);
    Rp1GemWrite32(Adapter, MACB_TSR, MACB_TSR_ALL);
    Rp1GemWrite32(Adapter, MACB_RSR, MACB_RSR_ALL);
    Rp1GemWrite32(Adapter, MACB_IDR, MACB_INT_ALL);

    Ncr = Rp1GemRead32(Adapter, MACB_NCR);
    Ncr &= ~(MACB_NCR_RE | MACB_NCR_TE | MACB_NCR_TSTART);
    Ncr |= MACB_NCR_MPE;
    Rp1GemWrite32(Adapter, MACB_NCR, Ncr);
    Adapter->DatapathReady = TRUE;
    Rp1GemApplyLinkState(Adapter);
    return NDIS_STATUS_SUCCESS;

Failure:
    Rp1GemFreeDatapath(Adapter);
    return Status;
}

static BOOLEAN
Rp1GemWaitMdioIdle(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    ULONG i;

    for (i = 0; i < 10000; i++)
    {
        if (Rp1GemRead32(Adapter, MACB_NSR) & MACB_NSR_IDLE)
            return TRUE;

        KeStallExecutionProcessor(10);
    }

    return FALSE;
}

static NDIS_STATUS
Rp1GemMdioRead(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ UCHAR Phy,
    _In_ UCHAR Reg,
    _Out_ PUSHORT Value)
{
    ULONG Man;

    if (!Rp1GemWaitMdioIdle(Adapter))
        return NDIS_STATUS_FAILURE;

    Man = MACB_MAN_VALUE(MACB_MAN_SOF_C22,
                         MACB_MAN_RW_READ,
                         Phy,
                         Reg,
                         MACB_MAN_CODE,
                         0);
    Rp1GemWrite32(Adapter, MACB_MAN, Man);

    if (!Rp1GemWaitMdioIdle(Adapter))
        return NDIS_STATUS_FAILURE;

    *Value = (USHORT)(Rp1GemRead32(Adapter, MACB_MAN) & 0xffff);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Rp1GemMdioWrite(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ UCHAR Phy,
    _In_ UCHAR Reg,
    _In_ USHORT Value)
{
    ULONG Man;

    if (!Rp1GemWaitMdioIdle(Adapter))
        return NDIS_STATUS_FAILURE;

    Man = MACB_MAN_VALUE(MACB_MAN_SOF_C22,
                         MACB_MAN_RW_WRITE,
                         Phy,
                         Reg,
                         MACB_MAN_CODE,
                         Value);
    Rp1GemWrite32(Adapter, MACB_MAN, Man);

    return Rp1GemWaitMdioIdle(Adapter) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
}

static NDIS_STATUS
Rp1GemMmdRead(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ UCHAR Devad,
    _In_ USHORT Reg,
    _Out_ PUSHORT Value)
{
    NDIS_STATUS Status;

    Status = Rp1GemMdioWrite(Adapter,
                             Adapter->PhyAddress,
                             MII_MMD_CTRL,
                             MII_MMD_CTRL_ADDR | Devad);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = Rp1GemMdioWrite(Adapter, Adapter->PhyAddress, MII_MMD_DATA, Reg);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = Rp1GemMdioWrite(Adapter,
                             Adapter->PhyAddress,
                             MII_MMD_CTRL,
                             MII_MMD_CTRL_NOINCR | Devad);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    return Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_MMD_DATA, Value);
}

static NDIS_STATUS
Rp1GemMmdWrite(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ UCHAR Devad,
    _In_ USHORT Reg,
    _In_ USHORT Value)
{
    NDIS_STATUS Status;

    Status = Rp1GemMdioWrite(Adapter,
                             Adapter->PhyAddress,
                             MII_MMD_CTRL,
                             MII_MMD_CTRL_ADDR | Devad);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = Rp1GemMdioWrite(Adapter, Adapter->PhyAddress, MII_MMD_DATA, Reg);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = Rp1GemMdioWrite(Adapter,
                             Adapter->PhyAddress,
                             MII_MMD_CTRL,
                             MII_MMD_CTRL_NOINCR | Devad);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    return Rp1GemMdioWrite(Adapter, Adapter->PhyAddress, MII_MMD_DATA, Value);
}

static BOOLEAN
Rp1GemIsValidPhyId(
    _In_ USHORT PhyId1,
    _In_ USHORT PhyId2)
{
    if ((PhyId1 == 0xffff) || (PhyId2 == 0xffff))
        return FALSE;

    return (PhyId1 != 0) || (PhyId2 != 0);
}

static BOOLEAN
Rp1GemReadPhyId(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ UCHAR Phy,
    _Out_ PUSHORT PhyId1,
    _Out_ PUSHORT PhyId2)
{
    NDIS_STATUS Status1, Status2;

    Status1 = Rp1GemMdioRead(Adapter, Phy, 2, PhyId1);
    Status2 = Rp1GemMdioRead(Adapter, Phy, 3, PhyId2);
    return Status1 == NDIS_STATUS_SUCCESS &&
           Status2 == NDIS_STATUS_SUCCESS &&
           Rp1GemIsValidPhyId(*PhyId1, *PhyId2);
}

static BOOLEAN
Rp1GemPhyIdMatchesModel(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG Model)
{
    ULONG PhyId;

    PhyId = ((ULONG)Adapter->PhyId1 << 16) | Adapter->PhyId2;
    return (PhyId & RP1GEM_PHY_ID_MODEL_MASK) == Model;
}

static NDIS_STATUS
Rp1GemBcm54xxAuxctlRead(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ USHORT Reg,
    _Out_ PUSHORT Value)
{
    NDIS_STATUS Status;

    Status = Rp1GemMdioWrite(Adapter,
                             Adapter->PhyAddress,
                             MII_BCM54XX_AUX_CTL,
                             MII_BCM54XX_AUXCTL_SHDWSEL_MASK |
                             (Reg << MII_BCM54XX_AUXCTL_SHDWSEL_READ_SHIFT));
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    return Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_BCM54XX_AUX_CTL, Value);
}

static NDIS_STATUS
Rp1GemBcm54xxAuxctlWrite(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ USHORT Reg,
    _In_ USHORT Value)
{
    return Rp1GemMdioWrite(Adapter,
                           Adapter->PhyAddress,
                           MII_BCM54XX_AUX_CTL,
                           Reg | Value);
}

static NDIS_STATUS
Rp1GemBcm54xxReadShadow(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ USHORT Shadow,
    _Out_ PUSHORT Value)
{
    NDIS_STATUS Status;

    Status = Rp1GemMdioWrite(Adapter,
                             Adapter->PhyAddress,
                             MII_BCM54XX_SHD,
                             MII_BCM54XX_SHD_VAL(Shadow));
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_BCM54XX_SHD, Value);
    if (Status == NDIS_STATUS_SUCCESS)
        *Value = MII_BCM54XX_SHD_DATA(*Value);

    return Status;
}

static NDIS_STATUS
Rp1GemBcm54xxWriteShadow(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ USHORT Shadow,
    _In_ USHORT Value)
{
    return Rp1GemMdioWrite(Adapter,
                           Adapter->PhyAddress,
                           MII_BCM54XX_SHD,
                           MII_BCM54XX_SHD_WRITE |
                           MII_BCM54XX_SHD_VAL(Shadow) |
                           MII_BCM54XX_SHD_DATA(Value));
}

static NDIS_STATUS
Rp1GemConfigureBcm54210eDelays(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    USHORT Value;

    Status = Rp1GemBcm54xxAuxctlRead(Adapter, MII_BCM54XX_AUXCTL_SHDWSEL_MISC, &Value);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Value |= MII_BCM54XX_AUXCTL_MISC_WREN |
             MII_BCM54XX_AUXCTL_SHDWSEL_MISC_RGMII_SKEW_EN;
    Status = Rp1GemBcm54xxAuxctlWrite(Adapter, MII_BCM54XX_AUXCTL_SHDWSEL_MISC, Value);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Status = Rp1GemBcm54xxReadShadow(Adapter, BCM54810_SHD_CLK_CTL, &Value);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Value |= BCM54810_SHD_CLK_CTL_GTXCLK_EN;
    return Rp1GemBcm54xxWriteShadow(Adapter, BCM54810_SHD_CLK_CTL, Value);
}

static NDIS_STATUS
Rp1GemConfigureBcm54210ePowerdown(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    USHORT Value, NewValue;

    Status = Rp1GemBcm54xxReadShadow(Adapter, BCM54XX_SHD_SCR3, &Value);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    NewValue = Value & ~BCM54XX_SHD_SCR3_DLLAPD_DIS;
    if (NewValue != Value)
    {
        Status = Rp1GemBcm54xxWriteShadow(Adapter, BCM54XX_SHD_SCR3, NewValue);
        if (Status != NDIS_STATUS_SUCCESS)
            return Status;
    }

    Status = Rp1GemBcm54xxReadShadow(Adapter, BCM54XX_SHD_APD, &Value);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    NewValue = Value | BCM54XX_SHD_APD_EN;
    if (NewValue == Value)
        return NDIS_STATUS_SUCCESS;

    return Rp1GemBcm54xxWriteShadow(Adapter, BCM54XX_SHD_APD, NewValue);
}

static NDIS_STATUS
Rp1GemDisableBcm54210eBrokenEee(
    _In_ PRP1GEM_ADAPTER Adapter,
    _Out_ PUSHORT EeeAdvertise)
{
    NDIS_STATUS Status;
    USHORT Value, NewValue;

    Status = Rp1GemMmdRead(Adapter, MDIO_MMD_AN, MDIO_AN_EEE_ADV, &Value);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    NewValue = Value & ~(MDIO_EEE_100TX | MDIO_EEE_1000T);
    if (NewValue != Value)
    {
        Status = Rp1GemMmdWrite(Adapter, MDIO_MMD_AN, MDIO_AN_EEE_ADV, NewValue);
        if (Status != NDIS_STATUS_SUCCESS)
            return Status;
    }

    *EeeAdvertise = NewValue;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Rp1GemConfigureBcm54xxPhyDsp(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NDIS_STATUS Status;

    Status = Rp1GemBcm54xxAuxctlWrite(Adapter,
                                      MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL,
                                      MII_BCM54XX_AUXCTL_ACTL_SMDSP_ENA |
                                      MII_BCM54XX_AUXCTL_ACTL_TX_6DB);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    return Rp1GemBcm54xxAuxctlWrite(Adapter,
                                    MII_BCM54XX_AUXCTL_SHDWSEL_AUXCTL,
                                    MII_BCM54XX_AUXCTL_ACTL_TX_6DB);
}

static VOID
Rp1GemProbePhy(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    USHORT PhyId1, PhyId2;
    UCHAR Phy;

    if (Rp1GemReadPhyId(Adapter, RP1GEM_PHY_ADDRESS, &PhyId1, &PhyId2))
    {
        Adapter->PhyAddress = RP1GEM_PHY_ADDRESS;
        Adapter->PhyId1 = PhyId1;
        Adapter->PhyId2 = PhyId2;
        DPRINT("RP1GEM: PHY%u ID1=0x%04x ID2=0x%04x\n",
               Adapter->PhyAddress,
               Adapter->PhyId1,
               Adapter->PhyId2);
        return;
    }

    DPRINT1("RP1GEM: PHY%u did not return a valid ID, scanning MDIO bus\n",
            RP1GEM_PHY_ADDRESS);

    for (Phy = 0; Phy < 32; Phy++)
    {
        if (Phy == RP1GEM_PHY_ADDRESS)
            continue;

        if (Rp1GemReadPhyId(Adapter, Phy, &PhyId1, &PhyId2))
        {
            Adapter->PhyAddress = Phy;
            Adapter->PhyId1 = PhyId1;
            Adapter->PhyId2 = PhyId2;
            DPRINT("RP1GEM: PHY%u ID1=0x%04x ID2=0x%04x\n",
                   Adapter->PhyAddress,
                   Adapter->PhyId1,
                   Adapter->PhyId2);
            return;
        }
    }

    Adapter->PhyAddress = RP1GEM_PHY_ADDRESS;
    DPRINT1("RP1GEM: no valid PHY ID found on MDIO bus\n");
}

static VOID
Rp1GemConfigurePhy(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NDIS_STATUS Status;
    USHORT Advertise, Ctrl1000, Bmcr, EeeAdvertise;

    if (!Rp1GemIsValidPhyId(Adapter->PhyId1, Adapter->PhyId2))
        return;

    EeeAdvertise = 0;

    if (Rp1GemPhyIdMatchesModel(Adapter, RP1GEM_PHY_ID_BCM54210E))
    {
        /*
         * Raspberry Pi 5 wires RP1 GEM to a BCM54210E in rgmii-id mode,
         * so the PHY must provide both RX skew and TX clock delay.
         */
        Status = Rp1GemConfigureBcm54210eDelays(Adapter);
        if (Status == NDIS_STATUS_SUCCESS)
            DPRINT("RP1GEM: BCM54210E RGMII-ID delays configured\n");
        else
            DPRINT1("RP1GEM: BCM54210E RGMII-ID configuration failed 0x%08x\n", Status);

        Status = Rp1GemConfigureBcm54210ePowerdown(Adapter);
        if (Status == NDIS_STATUS_SUCCESS)
            DPRINT("RP1GEM: BCM54210E auto power-down configured\n");
        else
            DPRINT1("RP1GEM: BCM54210E auto power-down configuration failed 0x%08x\n", Status);

        Status = Rp1GemConfigureBcm54xxPhyDsp(Adapter);
        if (Status == NDIS_STATUS_SUCCESS)
            DPRINT("RP1GEM: BCM54210E PHY DSP configured\n");
        else
            DPRINT1("RP1GEM: BCM54210E PHY DSP configuration failed 0x%08x\n", Status);

        Status = Rp1GemDisableBcm54210eBrokenEee(Adapter, &EeeAdvertise);
        if (Status == NDIS_STATUS_SUCCESS)
            DPRINT("RP1GEM: BCM54210E EEE advertisement masked EEE_ADV=0x%04x\n", EeeAdvertise);
        else
            DPRINT1("RP1GEM: BCM54210E EEE advertisement mask failed 0x%08x\n", Status);
    }

    Status = Rp1GemMdioWrite(Adapter,
                             Adapter->PhyAddress,
                             MII_ADVERTISE,
                             ADVERTISE_CSMA | ADVERTISE_ALL | ADVERTISE_PAUSE_CAP | ADVERTISE_PAUSE_ASYM);
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("RP1GEM: PHY%u advertisement write failed 0x%08x\n", Adapter->PhyAddress, Status);

    Status = Rp1GemMdioWrite(Adapter,
                             Adapter->PhyAddress,
                             MII_CTRL1000,
                             ADVERTISE_1000FULL);
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("RP1GEM: PHY%u 1000BASE-T advertisement write failed 0x%08x\n", Adapter->PhyAddress, Status);

    Status = Rp1GemMdioWrite(Adapter,
                             Adapter->PhyAddress,
                             MII_BMCR,
                             BMCR_ANENABLE | BMCR_ANRESTART);
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("RP1GEM: PHY%u autonegotiation restart failed 0x%08x\n", Adapter->PhyAddress, Status);

    Advertise = 0;
    Ctrl1000 = 0;
    Bmcr = 0;
    Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_ADVERTISE, &Advertise);
    Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_CTRL1000, &Ctrl1000);
    Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_BMCR, &Bmcr);

    DPRINT("RP1GEM: PHY%u autonegotiation restarted BMCR=0x%04x ANAR=0x%04x CTRL1000=0x%04x EEE_ADV=0x%04x\n",
           Adapter->PhyAddress,
           Bmcr,
           Advertise,
           Ctrl1000,
           EeeAdvertise);
}

static VOID
Rp1GemSetDisconnectedLink(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    Adapter->MediaConnectState = MediaConnectStateDisconnected;
    Adapter->MediaDuplexState = MediaDuplexStateUnknown;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
}

static BOOLEAN
Rp1GemRefreshLink(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ BOOLEAN ForceLog)
{
    NDIS_MEDIA_CONNECT_STATE OldConnectState;
    NDIS_MEDIA_DUPLEX_STATE OldDuplexState;
    ULONG64 OldLinkSpeed;
    USHORT Bmcr, Bmsr1, Bmsr2, Advertise, Lpa, Expansion, Ctrl1000, Stat1000, Estatus;
    BOOLEAN LinkChanged;
    PCSTR DuplexText;
    PCSTR MasterSlaveText;

    OldConnectState = Adapter->MediaConnectState;
    OldDuplexState = Adapter->MediaDuplexState;
    OldLinkSpeed = Adapter->LinkSpeed;
    Bmcr = 0;
    Bmsr2 = 0;
    Advertise = 0;
    Lpa = 0;
    Expansion = 0;
    Ctrl1000 = 0;
    Stat1000 = 0;
    Estatus = 0;

    if (!Rp1GemIsValidPhyId(Adapter->PhyId1, Adapter->PhyId2) ||
        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_BMCR, &Bmcr) != NDIS_STATUS_SUCCESS ||
        (Bmcr & BMCR_ANRESTART) ||
        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_BMSR, &Bmsr1) != NDIS_STATUS_SUCCESS ||
        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_BMSR, &Bmsr2) != NDIS_STATUS_SUCCESS ||
        !(Bmsr2 & BMSR_LSTATUS))
    {
        Rp1GemSetDisconnectedLink(Adapter);
    }
    else
    {
        Adapter->MediaConnectState = MediaConnectStateConnected;
        Adapter->MediaDuplexState = MediaDuplexStateHalf;
        Adapter->LinkSpeed = RP1GEM_LINK_SPEED_10M;

        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_LPA, &Lpa);
        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_STAT1000, &Stat1000);

        if (Stat1000 & LPA_1000FULL)
        {
            Adapter->LinkSpeed = RP1GEM_LINK_SPEED_1G;
            Adapter->MediaDuplexState = MediaDuplexStateFull;
        }
        else if (Stat1000 & LPA_1000HALF)
        {
            Adapter->LinkSpeed = RP1GEM_LINK_SPEED_1G;
        }
        else if (Lpa & ADVERTISE_100FULL)
        {
            Adapter->LinkSpeed = RP1GEM_LINK_SPEED_100M;
            Adapter->MediaDuplexState = MediaDuplexStateFull;
        }
        else if (Lpa & ADVERTISE_100HALF)
        {
            Adapter->LinkSpeed = RP1GEM_LINK_SPEED_100M;
        }
        else if (Lpa & ADVERTISE_10FULL)
        {
            Adapter->LinkSpeed = RP1GEM_LINK_SPEED_10M;
            Adapter->MediaDuplexState = MediaDuplexStateFull;
        }
    }

    LinkChanged = OldConnectState != Adapter->MediaConnectState ||
                  OldDuplexState != Adapter->MediaDuplexState ||
                  OldLinkSpeed != Adapter->LinkSpeed;
    if (LinkChanged && Adapter->RegisterBase)
        Rp1GemApplyLinkState(Adapter);

    if (LinkChanged || ForceLog)
    {
        DuplexText = "unknown";
        if (Adapter->MediaDuplexState == MediaDuplexStateFull)
            DuplexText = "full";
        else if (Adapter->MediaDuplexState == MediaDuplexStateHalf)
            DuplexText = "half";

        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_ADVERTISE, &Advertise);
        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_LPA, &Lpa);
        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_EXPANSION, &Expansion);
        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_CTRL1000, &Ctrl1000);
        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_STAT1000, &Stat1000);
        Rp1GemMdioRead(Adapter, Adapter->PhyAddress, MII_ESTATUS, &Estatus);

        MasterSlaveText = "unknown";
        if (Stat1000 & LPA_1000MSFAIL)
            MasterSlaveText = "fail";
        else if (Adapter->MediaConnectState == MediaConnectStateConnected)
            MasterSlaveText = (Stat1000 & LPA_1000MSRES) ? "master" : "slave";

        DPRINT("RP1GEM: PHY%u BMCR=0x%04x BMSR=0x%04x ANAR=0x%04x LPA=0x%04x EXP=0x%04x CTRL1000=0x%04x STAT1000=0x%04x ESTAT=0x%04x link=%s speed=%I64u duplex=%s 1000ms=%s rxok=%u/%u\n",
               Adapter->PhyAddress,
               Bmcr,
               Bmsr2,
               Advertise,
               Lpa,
               Expansion,
               Ctrl1000,
               Stat1000,
               Estatus,
               (Adapter->MediaConnectState == MediaConnectStateConnected) ? "up" : "down",
               Adapter->LinkSpeed,
               DuplexText,
               MasterSlaveText,
               (Stat1000 & LPA_1000LOCALRXOK) ? 1 : 0,
               (Stat1000 & LPA_1000REMRXOK) ? 1 : 0);
    }

    return LinkChanged;
}

static VOID
Rp1GemIndicateLinkState(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NDIS_STATUS_INDICATION StatusIndication;
    NDIS_LINK_STATE LinkState;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
    LinkState.MediaConnectState = Adapter->MediaConnectState;
    LinkState.MediaDuplexState = Adapter->MediaDuplexState;
    LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
    LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
    LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;

    RtlZeroMemory(&StatusIndication, sizeof(StatusIndication));
    StatusIndication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    StatusIndication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    StatusIndication.Header.Size = NDIS_SIZEOF_STATUS_INDICATION_REVISION_1;
    StatusIndication.SourceHandle = Adapter->MiniportHandle;
    StatusIndication.StatusCode = NDIS_STATUS_LINK_STATE;
    StatusIndication.StatusBuffer = &LinkState;
    StatusIndication.StatusBufferSize = sizeof(LinkState);

    NdisMIndicateStatusEx(Adapter->MiniportHandle, &StatusIndication);
}

/*
 * Runtime diagnostics. The link timer runs at DISPATCH_LEVEL, but Rp1GemWriteDiag
 * uses Zw* registry calls that require PASSIVE_LEVEL, so the timer queues this NDIS
 * IO work item (PASSIVE) to snapshot the live datapath state (NCR RE/TE, RSR/TSR,
 * interrupt + RX/TX packet counts) into HKLM\SOFTWARE\Rp1GemDiag for the Win11
 * launcher to read back -- there is no kernel debugger on the target.
 */
static VOID NTAPI
Rp1GemDiagWork(
    _In_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)WorkItemContext;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    if (Adapter)
    {
        Rp1GemWriteDiag(Adapter, 99, NDIS_STATUS_SUCCESS);
        InterlockedExchange(&Adapter->DiagWorkBusy, 0);
    }
}

static VOID NTAPI
Rp1GemLinkTimer(
    _In_ PVOID SystemSpecific1,
    _In_ PVOID FunctionContext,
    _In_ PVOID SystemSpecific2,
    _In_ PVOID SystemSpecific3)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)FunctionContext;
    BOOLEAN ForceLog;

    UNREFERENCED_PARAMETER(SystemSpecific1);
    UNREFERENCED_PARAMETER(SystemSpecific2);
    UNREFERENCED_PARAMETER(SystemSpecific3);

    if (!Adapter || !Adapter->RegisterBase)
        return;

    ForceLog = Adapter->LinkPollCount < 3;
    Adapter->LinkPollCount++;

    if (Rp1GemRefreshLink(Adapter, ForceLog))
        Rp1GemIndicateLinkState(Adapter);

    if (Adapter->DatapathReady &&
        Adapter->MediaConnectState == MediaConnectStateConnected)
    {
        Rp1GemPollReceive(Adapter, RP1GEM_RX_BUDGET);
        Rp1GemDrainTxCompletions(Adapter, 0);
    }

    /* Snapshot runtime datapath state to the registry (at PASSIVE via work item). */
    if (Adapter->DiagWorkItem &&
        InterlockedCompareExchange(&Adapter->DiagWorkBusy, 1, 0) == 0)
    {
        NdisQueueIoWorkItem(Adapter->DiagWorkItem, Rp1GemDiagWork, Adapter);
    }
}

static VOID
Rp1GemStartLinkTimer(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NdisMInitializeTimer(&Adapter->LinkTimer,
                         Adapter->MiniportHandle,
                         Rp1GemLinkTimer,
                         Adapter);
    Adapter->LinkTimerInitialized = TRUE;
    NdisMSetPeriodicTimer(&Adapter->LinkTimer, 1000);
}

static VOID
Rp1GemStopLinkTimer(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    BOOLEAN Cancelled;

    if (Adapter->LinkTimerInitialized)
    {
        NdisMCancelTimer(&Adapter->LinkTimer, &Cancelled);
        Adapter->LinkTimerInitialized = FALSE;

        /* Drain any in-flight LinkTimer DPC before HaltEx frees the adapter. */
        if (!Cancelled)
            KeFlushQueuedDpcs();
    }
}

static VOID
Rp1GemReadMacAddress(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    ULONG Sa1b = Rp1GemRead32(Adapter, GEM_SA1B);
    ULONG Sa1t = Rp1GemRead32(Adapter, GEM_SA1T);

    Adapter->PermanentMacAddress[0] = (UCHAR)(Sa1b & 0xff);
    Adapter->PermanentMacAddress[1] = (UCHAR)((Sa1b >> 8) & 0xff);
    Adapter->PermanentMacAddress[2] = (UCHAR)((Sa1b >> 16) & 0xff);
    Adapter->PermanentMacAddress[3] = (UCHAR)((Sa1b >> 24) & 0xff);
    Adapter->PermanentMacAddress[4] = (UCHAR)(Sa1t & 0xff);
    Adapter->PermanentMacAddress[5] = (UCHAR)((Sa1t >> 8) & 0xff);

    if (!Rp1GemIsValidMacAddress(Adapter->PermanentMacAddress))
    {
        static const UCHAR FallbackAddress[ETH_LENGTH_OF_ADDRESS] =
            { 0x02, 0x00, 0x00, 0x27, 0x12, 0x01 };

        DPRINT("RP1GEM: hardware MAC registers invalid SA1B=0x%08lx SA1T=0x%08lx\n",
               Sa1b,
               Sa1t);

        RtlCopyMemory(Adapter->PermanentMacAddress,
                      FallbackAddress,
                      sizeof(FallbackAddress));
        DPRINT("RP1GEM: using fallback locally-administered MAC address\n");
    }

    RtlCopyMemory(Adapter->CurrentMacAddress,
                  Adapter->PermanentMacAddress,
                  ETH_LENGTH_OF_ADDRESS);
    Rp1GemWriteMacAddress(Adapter);
}

static NDIS_STATUS
Rp1GemSetRegistrationAttributes(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RegAttrs;

    RtlZeroMemory(&RegAttrs, sizeof(RegAttrs));
    RegAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegAttrs.Header.Size = sizeof(RegAttrs);
    RegAttrs.MiniportAdapterContext = Adapter;
    RegAttrs.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
                              NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER |
                              NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    RegAttrs.CheckForHangTimeInSeconds = 4;
    RegAttrs.InterfaceType = NdisInterfaceInternal;

    return NdisMSetMiniportAttributes(
        Adapter->MiniportHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RegAttrs);
}

static NDIS_STATUS
Rp1GemSetGeneralAttributes(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GenAttrs;

    RtlZeroMemory(&GenAttrs, sizeof(GenAttrs));
    GenAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GenAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttrs.Header.Size = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttrs.MediaType = NdisMedium802_3;
    GenAttrs.PhysicalMediumType = NdisPhysicalMedium802_3;
    GenAttrs.MtuSize = RP1GEM_MTU;
    GenAttrs.MaxXmitLinkSpeed = RP1GEM_LINK_SPEED_1G;
    GenAttrs.MaxRcvLinkSpeed = RP1GEM_LINK_SPEED_1G;
    GenAttrs.XmitLinkSpeed = Adapter->LinkSpeed;
    GenAttrs.RcvLinkSpeed = Adapter->LinkSpeed;
    GenAttrs.MediaConnectState = Adapter->MediaConnectState;
    GenAttrs.MediaDuplexState = Adapter->MediaDuplexState;
    GenAttrs.LookaheadSize = Adapter->Lookahead;
    GenAttrs.MacOptions = RP1GEM_MAC_OPTIONS;
    GenAttrs.SupportedPacketFilters = RP1GEM_SUPPORTED_FILTERS;
    GenAttrs.MaxMulticastListSize = RP1GEM_MAX_MULTICAST;
    GenAttrs.MacAddressLength = ETH_LENGTH_OF_ADDRESS;
    RtlCopyMemory(GenAttrs.PermanentMacAddress,
                  Adapter->PermanentMacAddress,
                  ETH_LENGTH_OF_ADDRESS);
    RtlCopyMemory(GenAttrs.CurrentMacAddress,
                  Adapter->CurrentMacAddress,
                  ETH_LENGTH_OF_ADDRESS);
    GenAttrs.AccessType = NET_IF_ACCESS_BROADCAST;
    GenAttrs.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GenAttrs.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    GenAttrs.IfType = IF_TYPE_ETHERNET_CSMACD;
    GenAttrs.IfConnectorPresent = TRUE;
    GenAttrs.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    GenAttrs.SupportedOidList = (PNDIS_OID)Rp1GemSupportedOids;
    GenAttrs.SupportedOidListLength = sizeof(Rp1GemSupportedOids);
    GenAttrs.SupportedStatistics =
        NDIS_STATISTICS_XMIT_OK_SUPPORTED |
        NDIS_STATISTICS_RCV_OK_SUPPORTED |
        NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED;

    return NdisMSetMiniportAttributes(
        Adapter->MiniportHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&GenAttrs);
}

static NDIS_STATUS
Rp1GemMapResources(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ PNDIS_RESOURCE_LIST ResourceList)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG i;
    NDIS_STATUS Status;

    if (!ResourceList)
        return NDIS_STATUS_RESOURCES;

    Descriptor = &ResourceList->PartialDescriptors[0];

    /* Record what NDIS actually handed us so the Win11 diag readback can show
     * the CM resource types (e.g. Memory=3 vs MemoryLarge=7) and count. */
    Adapter->DiagResCount = ResourceList->Count;
    Adapter->DiagResTypes = 0;
    for (i = 0; i < ResourceList->Count && i < 4; i++)
        Adapter->DiagResTypes |= ((ULONG)Descriptor[i].Type & 0xff) << (i * 8);

    for (i = 0; i < ResourceList->Count; i++, Descriptor++)
    {
        switch (Descriptor->Type)
        {
            case CmResourceTypeMemory:
                if (!Adapter->RegisterBase)
                {
                    Adapter->RegisterPhysical = Descriptor->u.Memory.Start;
                    Adapter->RegisterLength = Descriptor->u.Memory.Length;
                    Status = NdisMMapIoSpace(&Adapter->RegisterBase,
                                             Adapter->MiniportHandle,
                                             Adapter->RegisterPhysical,
                                             Adapter->RegisterLength);
                    if (Status != NDIS_STATUS_SUCCESS)
                    {
                        DPRINT1("RP1GEM: NdisMMapIoSpace failed 0x%08x\n", Status);
                        return Status;
                    }

                    DPRINT("RP1GEM: MMIO phys=0x%I64x len=0x%lx va=%p\n",
                           Adapter->RegisterPhysical.QuadPart,
                           Adapter->RegisterLength,
                           Adapter->RegisterBase);
                }
                break;

            case CmResourceTypeInterrupt:
                Adapter->InterruptLevel = Descriptor->u.Interrupt.Level;
                Adapter->InterruptVector = Descriptor->u.Interrupt.Vector;
                Adapter->InterruptAffinity = Descriptor->u.Interrupt.Affinity;
                DPRINT("RP1GEM: interrupt level=%lu vector=%lu affinity=0x%Ix\n",
                       Adapter->InterruptLevel,
                       Adapter->InterruptVector,
                       Adapter->InterruptAffinity);
                break;

            default:
                break;
        }
    }

    return Adapter->RegisterBase ? NDIS_STATUS_SUCCESS : NDIS_STATUS_RESOURCES;
}

static VOID
Rp1GemProbeHardware(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    ULONG Ncr, Ncfgr, Mid, Dcfg1, Dcfg2, Dcfg6;

    Rp1GemWrite32(Adapter, MACB_IDR, 0xffffffff);
    Rp1GemWrite32(Adapter, MACB_TSR, MACB_TSR_ALL);
    Rp1GemWrite32(Adapter, MACB_RSR, MACB_RSR_ALL);

    Ncr = Rp1GemRead32(Adapter, MACB_NCR);
    Ncr &= ~(MACB_NCR_RE | MACB_NCR_TE | MACB_NCR_TSTART);
    Ncr |= MACB_NCR_MPE | MACB_NCR_CLRSTAT;
    Rp1GemWrite32(Adapter, MACB_NCR, Ncr);
    Rp1GemApplyMacConfiguration(Adapter);

    Ncr = Rp1GemRead32(Adapter, MACB_NCR);
    Rp1GemWrite32(Adapter, MACB_NCR, Ncr | MACB_NCR_MPE);

    Ncr = Rp1GemRead32(Adapter, MACB_NCR);
    Ncfgr = Rp1GemRead32(Adapter, MACB_NCFGR);
    Mid = Rp1GemRead32(Adapter, MACB_MID);
    Dcfg1 = Rp1GemRead32(Adapter, GEM_DCFG1);
    Dcfg2 = Rp1GemRead32(Adapter, GEM_DCFG2);
    Dcfg6 = Rp1GemRead32(Adapter, GEM_DCFG6);
    Adapter->Dma64Bit = (Dcfg6 & GEM_DCFG6_DAW64) != 0;
    Adapter->InterruptClearOnWrite = (Dcfg1 & GEM_DCFG1_IRQCOR) == 0;
    Rp1GemClearInterruptStatus(Adapter, MACB_INT_ALL);

    DPRINT("RP1GEM: regs NCR=0x%08lx NCFGR=0x%08lx NSR=0x%08lx MID=0x%08lx\n",
           Ncr,
           Ncfgr,
           Rp1GemRead32(Adapter, MACB_NSR),
           Mid);
    DPRINT("RP1GEM: GEM DCFG1=0x%08lx DCFG2=0x%08lx DCFG6=0x%08lx\n",
           Dcfg1,
           Dcfg2,
           Dcfg6);
    DPRINT("RP1GEM: DMA address width %s, ISR clear %s\n",
           Adapter->Dma64Bit ? "64-bit" : "32-bit",
           Adapter->InterruptClearOnWrite ? "write-one" : "read");

    Rp1GemReadMacAddress(Adapter);
    DPRINT("RP1GEM: MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
           Adapter->CurrentMacAddress[0],
           Adapter->CurrentMacAddress[1],
           Adapter->CurrentMacAddress[2],
           Adapter->CurrentMacAddress[3],
           Adapter->CurrentMacAddress[4],
           Adapter->CurrentMacAddress[5]);

    Rp1GemProbePhy(Adapter);
    Rp1GemConfigurePhy(Adapter);
    Rp1GemRefreshLink(Adapter, TRUE);
}

static VOID
Rp1GemDrainTxCompletions(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG CompleteFlags)
{
    UNREFERENCED_PARAMETER(CompleteFlags);

    if (!Adapter->DatapathReady)
        return;

    NdisAcquireSpinLock(&Adapter->TxLock);

    while (Adapter->TxFree < RP1GEM_TX_RING_SIZE)
    {
        ULONG Index = Adapter->TxTail;
        ULONG Control = Adapter->TxRing[Index].Control;

        if (!(Control & MACB_TX_USED))
            break;

        if (Control & MACB_TX_ERROR_MASK)
        {
            Adapter->TxErrors++;
        }
        else
        {
            Adapter->TxPackets++;
            Adapter->TxBytes += Adapter->TxBuffers[Index].Length;
        }

        Adapter->TxBuffers[Index].Length = 0;
        Adapter->TxTail = (Index + 1) % RP1GEM_TX_RING_SIZE;
        Adapter->TxFree++;
    }

    NdisReleaseSpinLock(&Adapter->TxLock);

    Rp1GemWrite32(Adapter, MACB_TSR, MACB_TSR_ALL);
}

static NDIS_STATUS
Rp1GemCopyNetBuffer(
    _In_ PNET_BUFFER NetBuffer,
    _Out_writes_bytes_(DestinationLength) PVOID Destination,
    _In_ ULONG DestinationLength,
    _Out_ PULONG BytesCopied)
{
    PMDL Mdl;
    ULONG MdlOffset;
    SIZE_T Remaining;
    ULONG Copied;

    *BytesCopied = 0;
    Remaining = NET_BUFFER_DATA_LENGTH(NetBuffer);
    if (Remaining > DestinationLength)
        return NDIS_STATUS_BUFFER_OVERFLOW;

    Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    MdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    Copied = 0;

    while (Remaining)
    {
        PVOID Source;
        ULONG MdlLength;
        ULONG CopyLength;

        if (!Mdl)
            return NDIS_STATUS_INVALID_PACKET;

        MdlLength = MmGetMdlByteCount(Mdl);
        if (MdlOffset >= MdlLength)
        {
            Mdl = Mdl->Next;
            MdlOffset = 0;
            continue;
        }

        CopyLength = MdlLength - MdlOffset;
        if (CopyLength > Remaining)
            CopyLength = (ULONG)Remaining;

        Source = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (!Source)
            return NDIS_STATUS_RESOURCES;

        RtlCopyMemory((PUCHAR)Destination + Copied,
                      (PUCHAR)Source + MdlOffset,
                      CopyLength);

        Copied += CopyLength;
        Remaining -= CopyLength;
        Mdl = Mdl->Next;
        MdlOffset = 0;
    }

    *BytesCopied = Copied;
    return NDIS_STATUS_SUCCESS;
}

static ULONG
Rp1GemReleaseReceiveNetBufferLists(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST NextNbl;
    ULONG Released = 0;

    for (Nbl = NetBufferLists; Nbl; Nbl = NextNbl)
    {
        ULONG Index;

        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        Index = (ULONG)(ULONG_PTR)NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0];

        NdisAcquireSpinLock(&Adapter->RxLock);
        if (Index < RP1GEM_RX_RING_SIZE &&
            Adapter->RxBuffers[Index].NetBufferList == Nbl)
        {
            Adapter->RxBuffers[Index].Indicated = FALSE;
            Rp1GemRearmRxDescriptor(Adapter, Index);
            Released++;
        }
        NdisReleaseSpinLock(&Adapter->RxLock);
    }

    return Released;
}

static BOOLEAN
Rp1GemReceivePending(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    BOOLEAN Pending;

    if (!Adapter->DatapathReady || !Adapter->RxRing)
        return FALSE;

    NdisAcquireSpinLock(&Adapter->RxLock);
    KeMemoryBarrier();
    Pending = !Adapter->RxBuffers[Adapter->RxTail].Indicated &&
              (Adapter->RxRing[Adapter->RxTail].Address & MACB_RX_USED) != 0;
    NdisReleaseSpinLock(&Adapter->RxLock);
    return Pending;
}

static VOID
Rp1GemPollReceive(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG Budget)
{
    PNET_BUFFER_LIST NblChain = NULL;
    PNET_BUFFER_LIST LastNbl = NULL;
    ULONG Received = 0;

    if (!Adapter->DatapathReady || !Adapter->RxRing)
        return;

    NdisAcquireSpinLock(&Adapter->RxLock);

    while (Budget-- > 0)
    {
        ULONG Index = Adapter->RxTail;
        PRP1GEM_DMA_DESCRIPTOR Descriptor = &Adapter->RxRing[Index];
        PRP1GEM_RX_BUFFER RxBuffer = &Adapter->RxBuffers[Index];
        ULONG Address = Descriptor->Address;
        ULONG Control;
        ULONG Length;
        PNET_BUFFER_LIST Nbl;

        KeMemoryBarrier();
        if (!(Address & MACB_RX_USED))
            break;

        KeMemoryBarrier();
        Control = Descriptor->Control;
        Length = Control & MACB_RX_FRMLEN_MASK;

        /* NDIS owns this descriptor until MiniportReturnNetBufferLists. */
        if (RxBuffer->Indicated)
            break;

        if ((Control & (MACB_RX_SOF | MACB_RX_EOF)) != (MACB_RX_SOF | MACB_RX_EOF) ||
            Length < ETH_LENGTH_OF_ADDRESS ||
            Length > RP1GEM_FRAME_SIZE)
        {
            Adapter->RxErrors++;
            Rp1GemRearmRxDescriptor(Adapter, Index);
            Adapter->RxTail = (Index + 1) % RP1GEM_RX_RING_SIZE;
            continue;
        }

        Rp1GemDcacheInvalidate(RxBuffer->VirtualAddress, Length);

        Nbl = RxBuffer->NetBufferList;
        {
            PNET_BUFFER Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
            NET_BUFFER_CURRENT_MDL(Nb) = RxBuffer->Mdl;
            NET_BUFFER_CURRENT_MDL_OFFSET(Nb) = 0;
            NET_BUFFER_DATA_LENGTH(Nb) = Length;
        }
        RxBuffer->Indicated = TRUE;
        Nbl->SourceHandle = Adapter->MiniportHandle;
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0] = (PVOID)(ULONG_PTR)Index;
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        if (!NblChain)
            NblChain = Nbl;
        else
            NET_BUFFER_LIST_NEXT_NBL(LastNbl) = Nbl;
        LastNbl = Nbl;
        Received++;
        Adapter->RxPackets++;
        Adapter->RxBytes += Length;
        Adapter->RxTail = (Index + 1) % RP1GEM_RX_RING_SIZE;
    }

    NdisReleaseSpinLock(&Adapter->RxLock);

    if (NblChain)
    {
        ULONG Flags = 0;

        if (KeGetCurrentIrql() == DISPATCH_LEVEL)
            Flags |= NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL;

        NdisMIndicateReceiveNetBufferLists(Adapter->MiniportHandle,
                                           NblChain,
                                           0,
                                           Received,
                                           Flags);
    }

    Rp1GemWrite32(Adapter, MACB_RSR, MACB_RSR_ALL);
}

static BOOLEAN NTAPI
Rp1GemInterrupt(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _Out_ PBOOLEAN QueueDefaultInterruptDpc,
    _Out_ PULONG TargetProcessors)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)MiniportInterruptContext;
    ULONG RawIsr;
    ULONG Isr;

    *QueueDefaultInterruptDpc = FALSE;
    *TargetProcessors = 0;

    if (!Adapter || !Adapter->RegisterBase)
        return FALSE;

    Adapter->InterruptIsrCount++;
    RawIsr = Rp1GemRead32(Adapter, MACB_ISR);
    Isr = RawIsr & RP1GEM_INT_MASK;
    Rp1GemClearInterruptStatus(Adapter, RawIsr);
    Adapter->InterruptLastRawIsr = RawIsr;
    Adapter->InterruptLastPending = Isr;
    if (!Isr)
    {
        Adapter->SpuriousInterruptCount++;
        return FALSE;
    }

    Adapter->InterruptRecognizedCount++;
    Rp1GemDisableInterrupts(Adapter);
    /* OR (not Exchange) so a re-entrant ISR keeps earlier pending causes. */
    InterlockedOr(&Adapter->InterruptPending, (LONG)Isr);
    *QueueDefaultInterruptDpc = TRUE;
    return TRUE;
}

static VOID NTAPI
Rp1GemInterruptDpc(
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PVOID MiniportDpcContext,
    _In_ PVOID ReceiveThrottleParameters,
    _In_ PVOID NdisReserved2)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)MiniportInterruptContext;
    PNDIS_RECEIVE_THROTTLE_PARAMETERS ThrottleParameters =
        (PNDIS_RECEIVE_THROTTLE_PARAMETERS)ReceiveThrottleParameters;
    BOOLEAN ThrottleReceive = FALSE;
    ULONG RxBudget = RP1GEM_RX_BUDGET;
    ULONG RawIsr;
    ULONG Pending;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(NdisReserved2);

    if (!Adapter || !Adapter->RegisterBase)
        return;

    Pending = (ULONG)InterlockedExchange(&Adapter->InterruptPending, 0);
    /* NDIS leaves interrupts masked across receive-throttle continuations. */
    RawIsr = Rp1GemRead32(Adapter, MACB_ISR);
    Rp1GemClearInterruptStatus(Adapter, RawIsr);
    Pending |= RawIsr & RP1GEM_INT_MASK;
    Adapter->InterruptDpcCount++;

    if (ThrottleParameters != NULL)
    {
        ThrottleParameters->MoreNblsPending = FALSE;
        if (ThrottleParameters->MaxNblsToIndicate != NDIS_INDICATE_ALL_NBLS)
        {
            ThrottleReceive = TRUE;
            RxBudget = min(RxBudget, ThrottleParameters->MaxNblsToIndicate);
            if (RxBudget == 0)
                RxBudget = 1;
        }
        else
        {
            RxBudget = RP1GEM_RX_RING_SIZE;
        }
    }

    if ((Pending & (MACB_INT_RCOMP | MACB_INT_ROVR | MACB_INT_HRESP)) ||
        Rp1GemReceivePending(Adapter))
    {
        Rp1GemPollReceive(Adapter, RxBudget);
    }

    if (Pending & (MACB_INT_TCOMP | MACB_INT_TXERR | MACB_INT_TUND | MACB_INT_RLE | MACB_INT_TXUBR))
        Rp1GemDrainTxCompletions(Adapter, NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);

    if (ThrottleReceive && Rp1GemReceivePending(Adapter))
    {
        ThrottleParameters->MoreNblsPending = TRUE;
        return;
    }

    Rp1GemEnableInterrupts(Adapter);

    /* Close the receive-throttle race between the pre-enable check and IER. */
    KeMemoryBarrier();
    if (ThrottleReceive && Rp1GemReceivePending(Adapter))
    {
        Rp1GemDisableInterrupts(Adapter);
        ThrottleParameters->MoreNblsPending = TRUE;
    }
}

static VOID NTAPI
Rp1GemDisableInterruptEx(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)MiniportInterruptContext;

    if (Adapter)
        Rp1GemDisableInterrupts(Adapter);
}

static VOID NTAPI
Rp1GemEnableInterruptEx(
    _In_ NDIS_HANDLE MiniportInterruptContext)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)MiniportInterruptContext;

    if (Adapter)
        Rp1GemEnableInterrupts(Adapter);
}

static NDIS_STATUS
Rp1GemRegisterInterrupt(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS IntChars;
    NDIS_STATUS Status;

    RtlZeroMemory(&IntChars, sizeof(IntChars));
    IntChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    IntChars.Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    IntChars.Header.Size = NDIS_SIZEOF_MINIPORT_INTERRUPT_CHARACTERISTICS_REVISION_1;
    IntChars.InterruptHandler = Rp1GemInterrupt;
    IntChars.InterruptDpcHandler = Rp1GemInterruptDpc;
    IntChars.DisableInterruptHandler = Rp1GemDisableInterruptEx;
    IntChars.EnableInterruptHandler = Rp1GemEnableInterruptEx;
    IntChars.InterruptType = NDIS_CONNECT_LINE_BASED;
    IntChars.MsiSupported = FALSE;

    Status = NdisMRegisterInterruptEx(Adapter->MiniportHandle,
                                      Adapter,
                                      &IntChars,
                                      &Adapter->InterruptHandle);
    if (Status == NDIS_STATUS_SUCCESS)
        DPRINT("RP1GEM: interrupt registered type=%lu\n", IntChars.InterruptType);
    else
        DPRINT1("RP1GEM: interrupt registration failed 0x%08x\n", Status);

    return Status;
}

static VOID
Rp1GemDeregisterInterrupt(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    if (Adapter->InterruptHandle)
    {
        Rp1GemDisableInterrupts(Adapter);
        NdisMDeregisterInterruptEx(Adapter->InterruptHandle);
        Adapter->InterruptHandle = NULL;
    }
}

static NDIS_STATUS
Rp1GemCopyQuery(
    _Inout_ PNDIS_OID_REQUEST OidRequest,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ ULONG Length)
{
    PVOID InfoBuffer = OidRequest->DATA.QUERY_INFORMATION.InformationBuffer;
    UINT InfoBufferLength = OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength;
    PUINT BytesWritten = &OidRequest->DATA.QUERY_INFORMATION.BytesWritten;
    PUINT BytesNeeded = &OidRequest->DATA.QUERY_INFORMATION.BytesNeeded;

    *BytesWritten = 0;
    *BytesNeeded = 0;

    if (InfoBufferLength < Length)
    {
        *BytesNeeded = Length;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    RtlCopyMemory(InfoBuffer, Source, Length);
    *BytesWritten = Length;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Rp1GemQueryInformation(
    _In_ PRP1GEM_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    NDIS_OID Oid = OidRequest->DATA.QUERY_INFORMATION.Oid;
    NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
    union
    {
        ULONG Ulong;
        USHORT Ushort;
        ULONG64 Ulong64;
        NDIS_MEDIUM Medium;
        NDIS_HARDWARE_STATUS HardwareStatus;
        NDIS_MEDIA_STATE MediaState;
        NDIS_PHYSICAL_MEDIUM PhysicalMedium;
        NDIS_LINK_STATE LinkState;
        UCHAR Mac[ETH_LENGTH_OF_ADDRESS];
    } Data;

    RtlZeroMemory(&Data, sizeof(Data));

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return Rp1GemCopyQuery(OidRequest,
                                   Rp1GemSupportedOids,
                                   sizeof(Rp1GemSupportedOids));

        case OID_GEN_HARDWARE_STATUS:
            Data.HardwareStatus = NdisHardwareStatusReady;
            return Rp1GemCopyQuery(OidRequest, &Data.HardwareStatus, sizeof(Data.HardwareStatus));

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Data.Medium = NdisMedium802_3;
            return Rp1GemCopyQuery(OidRequest, &Data.Medium, sizeof(Data.Medium));

        case OID_GEN_PHYSICAL_MEDIUM:
            Data.PhysicalMedium = NdisPhysicalMedium802_3;
            return Rp1GemCopyQuery(OidRequest, &Data.PhysicalMedium, sizeof(Data.PhysicalMedium));

        case OID_GEN_MAXIMUM_LOOKAHEAD:
        case OID_GEN_CURRENT_LOOKAHEAD:
        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Data.Ulong = RP1GEM_MTU;
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
        case OID_GEN_TRANSMIT_BLOCK_SIZE:
        case OID_GEN_RECEIVE_BLOCK_SIZE:
            Data.Ulong = RP1GEM_FRAME_SIZE;
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_LINK_SPEED:
            Data.Ulong = (ULONG)(Adapter->LinkSpeed / 100);
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_ID:
            Data.Ulong = ((ULONG)Adapter->PermanentMacAddress[0] << 16) |
                         ((ULONG)Adapter->PermanentMacAddress[1] << 8) |
                         Adapter->PermanentMacAddress[2];
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_VENDOR_DESCRIPTION:
        {
            static const CHAR Description[] = "ReactOS RP1 Cadence GEM Ethernet";
            return Rp1GemCopyQuery(OidRequest, Description, sizeof(Description));
        }

        case OID_GEN_DRIVER_VERSION:
        case OID_GEN_VENDOR_DRIVER_VERSION:
            Data.Ushort = RP1GEM_DRIVER_VERSION;
            return Rp1GemCopyQuery(OidRequest, &Data.Ushort, sizeof(Data.Ushort));

        case OID_GEN_CURRENT_PACKET_FILTER:
            Data.Ulong = Adapter->PacketFilter;
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MAC_OPTIONS:
            Data.Ulong = RP1GEM_MAC_OPTIONS;
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Data.MediaState = (Adapter->MediaConnectState == MediaConnectStateConnected) ?
                              NdisMediaStateConnected :
                              NdisMediaStateDisconnected;
            return Rp1GemCopyQuery(OidRequest, &Data.MediaState, sizeof(Data.MediaState));

        case OID_GEN_LINK_STATE:
            Data.LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Data.LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            Data.LinkState.Header.Size = sizeof(NDIS_LINK_STATE);
            Data.LinkState.MediaConnectState = Adapter->MediaConnectState;
            Data.LinkState.MediaDuplexState = Adapter->MediaDuplexState;
            Data.LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            Data.LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;
            return Rp1GemCopyQuery(OidRequest, &Data.LinkState, sizeof(Data.LinkState));

        case OID_GEN_XMIT_OK:
            Data.Ulong64 = Adapter->TxPackets;
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_OK:
            Data.Ulong64 = Adapter->RxPackets;
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_XMIT_ERROR:
            Data.Ulong64 = Adapter->TxErrors;
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_ERROR:
            Data.Ulong64 = Adapter->RxErrors;
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_GEN_RCV_NO_BUFFER:
            Data.Ulong64 = Adapter->RxNoBuffer;
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong64, sizeof(Data.Ulong64));

        case OID_802_3_PERMANENT_ADDRESS:
            RtlCopyMemory(Data.Mac, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
            return Rp1GemCopyQuery(OidRequest, Data.Mac, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_CURRENT_ADDRESS:
            RtlCopyMemory(Data.Mac, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);
            return Rp1GemCopyQuery(OidRequest, Data.Mac, ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MULTICAST_LIST:
            return Rp1GemCopyQuery(OidRequest,
                                   Adapter->MulticastList,
                                   Adapter->MulticastCount * ETH_LENGTH_OF_ADDRESS);

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Data.Ulong = RP1GEM_MAX_MULTICAST;
            return Rp1GemCopyQuery(OidRequest, &Data.Ulong, sizeof(Data.Ulong));

        default:
            Status = NDIS_STATUS_NOT_SUPPORTED;
            break;
    }

    return Status;
}

static NDIS_STATUS
Rp1GemSetInformation(
    _In_ PRP1GEM_ADAPTER Adapter,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    NDIS_OID Oid = OidRequest->DATA.SET_INFORMATION.Oid;
    PVOID InfoBuffer = OidRequest->DATA.SET_INFORMATION.InformationBuffer;
    UINT InfoBufferLength = OidRequest->DATA.SET_INFORMATION.InformationBufferLength;
    PUINT BytesRead = &OidRequest->DATA.SET_INFORMATION.BytesRead;
    PUINT BytesNeeded = &OidRequest->DATA.SET_INFORMATION.BytesNeeded;

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (InfoBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            Adapter->PacketFilter = *(PULONG)InfoBuffer;
            if (Adapter->RegisterBase)
                Rp1GemApplyMacConfiguration(Adapter);
            if (Adapter->MediaConnectState == MediaConnectStateConnected)
            {
                Rp1GemPollReceive(Adapter, RP1GEM_RX_BUDGET);
                Rp1GemDrainTxCompletions(Adapter, 0);
            }
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (InfoBufferLength < sizeof(ULONG))
            {
                *BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            Adapter->Lookahead = *(PULONG)InfoBuffer;
            if (Adapter->Lookahead > RP1GEM_MTU)
                Adapter->Lookahead = RP1GEM_MTU;
            *BytesRead = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_MULTICAST_LIST:
            if ((InfoBufferLength % ETH_LENGTH_OF_ADDRESS) != 0)
                return NDIS_STATUS_INVALID_LENGTH;

            if (InfoBufferLength > sizeof(Adapter->MulticastList))
            {
                *BytesNeeded = sizeof(Adapter->MulticastList);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            Adapter->MulticastCount = InfoBufferLength / ETH_LENGTH_OF_ADDRESS;
            RtlCopyMemory(Adapter->MulticastList, InfoBuffer, InfoBufferLength);
            *BytesRead = InfoBufferLength;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

/*
 * Win11-on-RPi5 init diagnostics. The same .sys brings the link up at 1Gbps on
 * ReactOS but fails MiniportInitializeEx on Win11 ARM64 (Code 10 / 0xC0000001),
 * and Win11 has no kernel debugger attached so DPRINT is invisible. Dump a
 * decisive snapshot to \Registry\Machine\SOFTWARE\Rp1GemDiag that the guest can
 * read back: which Stage we reached, the failing NDIS status, the CM resources
 * NDIS handed us, and live GEM register reads (MID/DCFG*) that reveal whether
 * the MMIO mapping is actually alive.
 */
static VOID
Rp1GemWriteDiag(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG Stage,
    _In_ NDIS_STATUS LastStatus)
{
    OBJECT_ATTRIBUTES ObjA;
    UNICODE_STRING KeyName, ValName;
    HANDLE hKey = NULL;
    NTSTATUS NtStatus;
    ULONG Disp;
    ULONG Mid = 0, Dcfg1 = 0, Dcfg6 = 0, Ncr = 0, Nsr = 0;
    ULONG Rsr = 0, Tsr = 0, Imr = 0;
    ULONG MacLo, MacHi;

    if (Adapter->RegisterBase)
    {
        Mid = Rp1GemRead32(Adapter, MACB_MID);
        Dcfg1 = Rp1GemRead32(Adapter, GEM_DCFG1);
        Dcfg6 = Rp1GemRead32(Adapter, GEM_DCFG6);
        Ncr = Rp1GemRead32(Adapter, MACB_NCR);
        Nsr = Rp1GemRead32(Adapter, MACB_NSR);
        Rsr = Rp1GemRead32(Adapter, MACB_RSR);
        Tsr = Rp1GemRead32(Adapter, MACB_TSR);
        Imr = Rp1GemRead32(Adapter, MACB_IMR);
    }
    MacLo = Adapter->CurrentMacAddress[0] |
            (Adapter->CurrentMacAddress[1] << 8) |
            (Adapter->CurrentMacAddress[2] << 16) |
            ((ULONG)Adapter->CurrentMacAddress[3] << 24);
    MacHi = Adapter->CurrentMacAddress[4] |
            (Adapter->CurrentMacAddress[5] << 8);

    RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\SOFTWARE\\Rp1GemDiag");
    InitializeObjectAttributes(&ObjA, &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL, NULL);
    NtStatus = ZwCreateKey(&hKey, KEY_ALL_ACCESS, &ObjA, 0, NULL,
                           REG_OPTION_NON_VOLATILE, &Disp);
    if (!NT_SUCCESS(NtStatus))
        return;

#define DIAG_SET(_name, _val) do {                                      \
        ULONG _v = (ULONG)(_val);                                       \
        RtlInitUnicodeString(&ValName, _name);                         \
        ZwSetValueKey(hKey, &ValName, 0, REG_DWORD, &_v, sizeof(_v));  \
    } while (0)

    DIAG_SET(L"DiagVer", 7);
    DIAG_SET(L"Stage", Stage);
    DIAG_SET(L"SubStage", Adapter->DiagSubStage);
    DIAG_SET(L"LoopIndex", Adapter->DiagLoopIndex);
    DIAG_SET(L"LastStatus", (ULONG)LastStatus);
    DIAG_SET(L"ResCount", Adapter->DiagResCount);
    DIAG_SET(L"ResTypes", Adapter->DiagResTypes);
    DIAG_SET(L"RegPhysHi", Adapter->RegisterPhysical.HighPart);
    DIAG_SET(L"RegPhysLo", Adapter->RegisterPhysical.LowPart);
    DIAG_SET(L"RegLen", Adapter->RegisterLength);
    DIAG_SET(L"RegMapped", Adapter->RegisterBase ? 1 : 0);
    DIAG_SET(L"MID", Mid);
    DIAG_SET(L"DCFG1", Dcfg1);
    DIAG_SET(L"DCFG6", Dcfg6);
    DIAG_SET(L"NCR", Ncr);
    DIAG_SET(L"NSR", Nsr);
    DIAG_SET(L"RSR", Rsr);
    DIAG_SET(L"TSR", Tsr);
    DIAG_SET(L"IMR", Imr);
    DIAG_SET(L"RxPackets", (ULONG)Adapter->RxPackets);
    DIAG_SET(L"TxPackets", (ULONG)Adapter->TxPackets);
    DIAG_SET(L"RxBytes", (ULONG)Adapter->RxBytes);
    DIAG_SET(L"TxBytes", (ULONG)Adapter->TxBytes);
    DIAG_SET(L"RxErrors", (ULONG)Adapter->RxErrors);
    DIAG_SET(L"TxErrors", (ULONG)Adapter->TxErrors);
    DIAG_SET(L"RxNoBuffer", (ULONG)Adapter->RxNoBuffer);
    DIAG_SET(L"IsrCount", Adapter->InterruptIsrCount);
    DIAG_SET(L"DpcCount", Adapter->InterruptDpcCount);
    DIAG_SET(L"IsrRecognized", Adapter->InterruptRecognizedCount);
    DIAG_SET(L"IsrSpurious", Adapter->SpuriousInterruptCount);
    DIAG_SET(L"IsrLastRaw", Adapter->InterruptLastRawIsr);
    DIAG_SET(L"MediaConnect", (ULONG)Adapter->MediaConnectState);
    DIAG_SET(L"Dma64", Adapter->Dma64Bit ? 1 : 0);
    DIAG_SET(L"IntVec", Adapter->InterruptVector);
    DIAG_SET(L"IntLvl", Adapter->InterruptLevel);
    DIAG_SET(L"MacLo", MacLo);
    DIAG_SET(L"MacHi", MacHi);
    DIAG_SET(L"PhyAddr", Adapter->PhyAddress);
    DIAG_SET(L"PhyId1", Adapter->PhyId1);
    DIAG_SET(L"PhyId2", Adapter->PhyId2);

#undef DIAG_SET

    ZwClose(hKey);
}

static NDIS_STATUS NTAPI
Rp1GemInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PRP1GEM_ADAPTER Adapter;
    NDIS_STATUS Status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    DPRINT("RP1GEM: MiniportInitializeEx\n");

    Adapter = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Adapter),
                                    RP1GEM_TAG);
    if (!Adapter)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->MiniportHandle = MiniportAdapterHandle;
    Adapter->Lookahead = RP1GEM_MTU;
    Adapter->LinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    Adapter->MediaConnectState = MediaConnectStateDisconnected;
    Adapter->MediaDuplexState = MediaDuplexStateUnknown;
    NdisAllocateSpinLock(&Adapter->RxLock);
    NdisAllocateSpinLock(&Adapter->TxLock);

    Status = Rp1GemSetRegistrationAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RP1GEM: registration attributes failed 0x%08x\n", Status);
        NdisFreeSpinLock(&Adapter->TxLock);
        NdisFreeSpinLock(&Adapter->RxLock);
        ExFreePoolWithTag(Adapter, RP1GEM_TAG);
        return Status;
    }

    NdisMGetDeviceProperty(MiniportAdapterHandle,
                           &Adapter->PhysicalDeviceObject,
                           NULL,
                           NULL,
                           NULL,
                           NULL);
    DPRINT("RP1GEM: PDO=%p resources=%p\n",
           Adapter->PhysicalDeviceObject,
           MiniportInitParameters->AllocatedResources);

    Status = Rp1GemMapResources(
        Adapter,
        (PNDIS_RESOURCE_LIST)MiniportInitParameters->AllocatedResources);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Rp1GemWriteDiag(Adapter, 3, Status);
        Rp1GemUnmapResources(Adapter);
        NdisFreeSpinLock(&Adapter->TxLock);
        NdisFreeSpinLock(&Adapter->RxLock);
        ExFreePoolWithTag(Adapter, RP1GEM_TAG);
        return Status;
    }

    Rp1GemProbeHardware(Adapter);

    /* Decisive snapshot: MMIO is mapped now, so MID/DCFG6/MAC reads here tell
     * us whether the register window is actually alive on Win11 ARM64. */
    Rp1GemWriteDiag(Adapter, 4, NDIS_STATUS_SUCCESS);

    Status = Rp1GemInitializeDatapath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RP1GEM: datapath initialization failed 0x%08x\n", Status);
        Rp1GemWriteDiag(Adapter, 5, Status);
        Rp1GemStopHardware(Adapter);
        Rp1GemFreeDatapath(Adapter);
        Rp1GemUnmapResources(Adapter);
        NdisFreeSpinLock(&Adapter->TxLock);
        NdisFreeSpinLock(&Adapter->RxLock);
        ExFreePoolWithTag(Adapter, RP1GEM_TAG);
        return Status;
    }

    Rp1GemWriteDiag(Adapter, 5, NDIS_STATUS_SUCCESS);

    Status = Rp1GemRegisterInterrupt(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Rp1GemWriteDiag(Adapter, 6, Status);
        Rp1GemStopHardware(Adapter);
        Rp1GemFreeDatapath(Adapter);
        Rp1GemUnmapResources(Adapter);
        NdisFreeSpinLock(&Adapter->TxLock);
        NdisFreeSpinLock(&Adapter->RxLock);
        ExFreePoolWithTag(Adapter, RP1GEM_TAG);
        return Status;
    }

    Rp1GemWriteDiag(Adapter, 6, NDIS_STATUS_SUCCESS);

    Status = Rp1GemSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RP1GEM: general attributes failed 0x%08x\n", Status);
        Rp1GemWriteDiag(Adapter, 7, Status);
        Rp1GemDeregisterInterrupt(Adapter);
        Rp1GemStopHardware(Adapter);
        Rp1GemFreeDatapath(Adapter);
        Rp1GemUnmapResources(Adapter);
        NdisFreeSpinLock(&Adapter->TxLock);
        NdisFreeSpinLock(&Adapter->RxLock);
        ExFreePoolWithTag(Adapter, RP1GEM_TAG);
        return Status;
    }

    Rp1GemDiscardPendingReceive(Adapter);
    Rp1GemEnableInterrupts(Adapter);
    Adapter->DiagWorkItem = NdisAllocateIoWorkItem(Adapter->MiniportHandle);
    Rp1GemStartLinkTimer(Adapter);
    Rp1GemWriteDiag(Adapter, 8, NDIS_STATUS_SUCCESS);
    DPRINT("RP1GEM: initialized\n");
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
Rp1GemHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    DPRINT("RP1GEM: HaltEx\n");

    if (!Adapter)
        return;

    Rp1GemStopLinkTimer(Adapter);
    if (Adapter->DiagWorkItem)
    {
        /* Timer is cancelled + DPCs flushed, so no new diag work will be queued.
         * Wait out any in-flight item (PASSIVE) before freeing it / the adapter. */
        ULONG Spin = 0;
        while (Adapter->DiagWorkBusy && Spin < 1000)
        {
            NdisMSleep(1000);
            Spin++;
        }
        NdisFreeIoWorkItem(Adapter->DiagWorkItem);
        Adapter->DiagWorkItem = NULL;
    }
    Rp1GemDeregisterInterrupt(Adapter);
    Rp1GemStopHardware(Adapter);
    Rp1GemFreeDatapath(Adapter);
    Rp1GemUnmapResources(Adapter);
    NdisFreeSpinLock(&Adapter->TxLock);
    NdisFreeSpinLock(&Adapter->RxLock);
    ExFreePoolWithTag(Adapter, RP1GEM_TAG);
}

static NDIS_STATUS NTAPI
Rp1GemPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(PauseParameters);
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
Rp1GemRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RestartParameters);
    return NDIS_STATUS_SUCCESS;
}

static VOID NTAPI
Rp1GemSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST NextNbl;
    PNET_BUFFER_LIST FailHead = NULL;
    PNET_BUFFER_LIST FailTail = NULL;
    PNET_BUFFER_LIST OkHead = NULL;
    PNET_BUFFER_LIST OkTail = NULL;
    ULONG CompleteFlags = 0;
    BOOLEAN KickTx = FALSE;

    UNREFERENCED_PARAMETER(PortNumber);

    if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags))
        CompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;

    Rp1GemDrainTxCompletions(Adapter, CompleteFlags);

    for (Nbl = NetBufferLists; Nbl; Nbl = NextNbl)
    {
        PNET_BUFFER NetBuffer;
        NDIS_STATUS Status = NDIS_STATUS_SUCCESS;
        ULONG Index = 0;
        ULONG Length = 0;
        ULONG NetBufferCount = 0;
        ULONG Offset;
        ULONG StartIndex;

        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
        if (!Adapter->DatapathReady ||
            Adapter->MediaConnectState != MediaConnectStateConnected)
        {
            Status = NDIS_STATUS_MEDIA_DISCONNECTED;
            goto FailNbl;
        }

        if (!NetBuffer)
        {
            Status = NDIS_STATUS_INVALID_PACKET;
            goto FailNbl;
        }

        for (; NetBuffer; NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            if (NET_BUFFER_DATA_LENGTH(NetBuffer) > RP1GEM_FRAME_SIZE ||
                NET_BUFFER_DATA_LENGTH(NetBuffer) < ETH_LENGTH_OF_ADDRESS)
            {
                Status = NDIS_STATUS_INVALID_LENGTH;
                goto FailNbl;
            }

            NetBufferCount++;
            if (NetBufferCount > RP1GEM_TX_RING_SIZE)
            {
                Status = NDIS_STATUS_RESOURCES;
                goto FailNbl;
            }
        }

        NdisAcquireSpinLock(&Adapter->TxLock);
        if (Adapter->TxFree < NetBufferCount)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            Status = NDIS_STATUS_RESOURCES;
            goto FailNbl;
        }

        StartIndex = Adapter->TxHead;
        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
        for (Offset = 0; NetBuffer; Offset++, NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            Index = (StartIndex + Offset) % RP1GEM_TX_RING_SIZE;
            Status = Rp1GemCopyNetBuffer(NetBuffer,
                                         Adapter->TxBuffers[Index].VirtualAddress,
                                         RP1GEM_BUFFER_SIZE,
                                         &Length);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                while (Offset > 0)
                {
                    Offset--;
                    Index = (StartIndex + Offset) % RP1GEM_TX_RING_SIZE;
                    Adapter->TxBuffers[Index].Length = 0;
                }
                NdisReleaseSpinLock(&Adapter->TxLock);
                goto FailNbl;
            }

            Adapter->TxBuffers[Index].Length = Length;
        }

        for (Offset = 0; Offset < NetBufferCount; Offset++)
        {
            Index = (StartIndex + Offset) % RP1GEM_TX_RING_SIZE;
            Length = Adapter->TxBuffers[Index].Length;
            Rp1GemDcacheClean(Adapter->TxBuffers[Index].VirtualAddress, Length);

            Rp1GemSetDescriptorAddress(Adapter,
                                       &Adapter->TxRing[Index],
                                       Adapter->TxBuffers[Index].PhysicalAddress,
                                       0);
            KeMemoryBarrier();
            Adapter->TxRing[Index].Control =
                Length |
                MACB_TX_LAST |
                ((Index == (RP1GEM_TX_RING_SIZE - 1)) ? MACB_TX_WRAP : 0);
        }

        Adapter->TxHead = (StartIndex + NetBufferCount) % RP1GEM_TX_RING_SIZE;
        Adapter->TxFree -= NetBufferCount;

        NdisReleaseSpinLock(&Adapter->TxLock);

        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        if (!OkHead)
            OkHead = Nbl;
        else
            NET_BUFFER_LIST_NEXT_NBL(OkTail) = Nbl;
        OkTail = Nbl;
        KickTx = TRUE;
        continue;

FailNbl:
        NET_BUFFER_LIST_STATUS(Nbl) = Status;
        Adapter->TxErrors++;
        if (!FailHead)
            FailHead = Nbl;
        else
            NET_BUFFER_LIST_NEXT_NBL(FailTail) = Nbl;
        FailTail = Nbl;
    }

    if (KickTx)
        Rp1GemWrite32(Adapter,
                      MACB_NCR,
                      Rp1GemRead32(Adapter, MACB_NCR) | MACB_NCR_TSTART);

    if (OkHead)
    {
        NET_BUFFER_LIST_NEXT_NBL(OkTail) = NULL;
        NdisMSendNetBufferListsComplete(Adapter->MiniportHandle, OkHead, CompleteFlags);
    }

    if (FailHead)
    {
        NET_BUFFER_LIST_NEXT_NBL(FailTail) = NULL;
        NdisMSendNetBufferListsComplete(Adapter->MiniportHandle, FailHead, CompleteFlags);
    }

    Rp1GemDrainTxCompletions(Adapter, CompleteFlags);
}

static VOID NTAPI
Rp1GemReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)MiniportAdapterContext;
    GROUP_AFFINITY TargetProcessor = {0};
    ULONG Released;
    LONG ReturnedSinceKick;

    UNREFERENCED_PARAMETER(ReturnFlags);

    if (Adapter)
    {
        Released = Rp1GemReleaseReceiveNetBufferLists(Adapter, NetBufferLists);
        ReturnedSinceKick = InterlockedExchangeAdd(&Adapter->RxReturnedSinceKick, Released) + Released;

        if (ReturnedSinceKick >= RP1GEM_RX_RETURN_BATCH &&
            Rp1GemReceivePending(Adapter))
        {
            InterlockedExchange(&Adapter->RxReturnedSinceKick, 0);
            TargetProcessor.Mask = Adapter->InterruptAffinity ? Adapter->InterruptAffinity : 1;
            NdisMQueueDpcEx(Adapter->InterruptHandle, 0, &TargetProcessor, NULL);
        }
    }
}

static VOID NTAPI
Rp1GemCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

static BOOLEAN NTAPI
Rp1GemCheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)MiniportAdapterContext;

    if (Adapter)
    {
        Rp1GemPollReceive(Adapter, RP1GEM_RX_BUDGET);
        Rp1GemDrainTxCompletions(Adapter, 0);
    }

    if (Adapter && Rp1GemRefreshLink(Adapter, FALSE))
        Rp1GemIndicateLinkState(Adapter);

    return FALSE;
}

static NDIS_STATUS NTAPI
Rp1GemReset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);

    if (AddressingReset)
        *AddressingReset = FALSE;

    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS NTAPI
Rp1GemOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)MiniportAdapterContext;

    switch (OidRequest->RequestType)
    {
        case NdisRequestQueryInformation:
        case NdisRequestQueryStatistics:
            return Rp1GemQueryInformation(Adapter, OidRequest);

        case NdisRequestSetInformation:
            return Rp1GemSetInformation(Adapter, OidRequest);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static VOID NTAPI
Rp1GemCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static VOID NTAPI
Rp1GemDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ struct _NET_DEVICE_PNP_EVENT *NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

static VOID NTAPI
Rp1GemShutdown(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(ShutdownAction);
}

static VOID NTAPI
Rp1GemUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (Rp1GemDriverHandle)
    {
        NdisMDeregisterMiniportDriver(Rp1GemDriverHandle);
        Rp1GemDriverHandle = NULL;
    }
}

NTSTATUS NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Chars;
    NDIS_STATUS Status;

    DPRINT("RP1GEM: DriverEntry\n");

    RtlZeroMemory(&Chars, sizeof(Chars));
    Chars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    /* Win11 ARM64 enforces an NDIS version floor of 6.30 (public WinSDK
     * km/ndis.h: NDIS_MIN_API=0x630 on ARM64/ARM, vs 0x400 on x86/x64).
     * A miniport declaring < 6.30 is rejected by NdisMRegisterMiniportDriver
     * with NDIS_STATUS_BAD_VERSION. Declare 6.30 with the matching
     * characteristics REVISION_2 (Direct-OID handlers stay NULL). ReactOS's
     * NDIS accepts this too; this keeps one binary working on both. */
    Chars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.MajorNdisVersion = 6;
    Chars.MinorNdisVersion = 30;
    Chars.MajorDriverVersion = 1;
    Chars.MinorDriverVersion = 0;
    Chars.InitializeHandlerEx = Rp1GemInitializeEx;
    Chars.HaltHandlerEx = Rp1GemHaltEx;
    Chars.UnloadHandler = Rp1GemUnload;
    Chars.PauseHandler = Rp1GemPause;
    Chars.RestartHandler = Rp1GemRestart;
    Chars.OidRequestHandler = Rp1GemOidRequest;
    Chars.SendNetBufferListsHandler = Rp1GemSendNetBufferLists;
    Chars.ReturnNetBufferListsHandler = Rp1GemReturnNetBufferLists;
    Chars.CancelSendHandler = Rp1GemCancelSend;
    Chars.CheckForHangHandlerEx = Rp1GemCheckForHang;
    Chars.ResetHandlerEx = Rp1GemReset;
    Chars.DevicePnPEventNotifyHandler = Rp1GemDevicePnPEventNotify;
    Chars.ShutdownHandlerEx = Rp1GemShutdown;
    Chars.CancelOidRequestHandler = Rp1GemCancelOidRequest;

    Status = NdisMRegisterMiniportDriver(DriverObject,
                                         RegistryPath,
                                         NULL,
                                         &Chars,
                                         &Rp1GemDriverHandle);
    if (Status != NDIS_STATUS_SUCCESS)
        DPRINT1("RP1GEM: NdisMRegisterMiniportDriver failed 0x%08x\n", Status);

    return Status;
}
