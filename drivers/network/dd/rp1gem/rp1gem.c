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

static VOID
Rp1GemAckRp1Interrupt(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    if (!Adapter->Rp1ApbsBase)
        return;

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Adapter->Rp1ApbsBase +
                                  RP1GEM_PCIE_APBS_REG_SET +
                                  RP1GEM_MSIX_CFG(RP1GEM_INT_ETH)),
                         RP1GEM_MSIX_CFG_IACK);
}

static __inline ULONG
Rp1GemReadSideband32(
    _In_ PVOID Base,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + Offset));
}

static __inline VOID
Rp1GemWriteSideband32(
    _In_ PVOID Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + Offset), Value);
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
Rp1GemGetBufferPhysicalAddress(
    _Out_ PNDIS_PHYSICAL_ADDRESS PhysicalAddress,
    _In_ NDIS_PHYSICAL_ADDRESS Base,
    _In_ ULONG Index)
{
    PhysicalAddress->QuadPart = Base.QuadPart + ((ULONGLONG)Index * RP1GEM_BUFFER_SIZE);
}

static VOID
Rp1GemRearmRxDescriptor(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ ULONG Index)
{
    ULONG Flags = (Index == (RP1GEM_RX_RING_SIZE - 1)) ? MACB_RX_WRAP : 0;

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

    Ncfgr = GEM_NCFGR_CLK_DIV96 | GEM_NCFGR_DBW128 | MACB_NCFGR_DRFCS | MACB_NCFGR_BIG;

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
        Rp1GemAckRp1Interrupt(Adapter);
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
    if (Adapter->Rp1ApbsBase)
    {
        NdisMUnmapIoSpace(Adapter->MiniportHandle,
                          Adapter->Rp1ApbsBase,
                          Adapter->Rp1ApbsLength);
        Adapter->Rp1ApbsBase = NULL;
        Adapter->Rp1ApbsLength = 0;
    }

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

    if (Adapter->TxRing)
    {
        for (i = 0; i < RP1GEM_TX_RING_SIZE; i++)
        {
            if (Adapter->TxBuffers[i].NetBufferList)
            {
                NET_BUFFER_LIST_STATUS(Adapter->TxBuffers[i].NetBufferList) =
                    NDIS_STATUS_RESET_IN_PROGRESS;
                NdisMSendNetBufferListsComplete(Adapter->MiniportHandle,
                                                Adapter->TxBuffers[i].NetBufferList,
                                                0);
                Adapter->TxBuffers[i].NetBufferList = NULL;
            }
        }
    }

    for (i = 0; i < RP1GEM_RX_RING_SIZE; i++)
    {
        if (Adapter->RxBuffers[i].NetBufferList)
        {
            if (Adapter->RxBuffers[i].Mdl)
            {
                IoFreeMdl(Adapter->RxBuffers[i].Mdl);
                Adapter->RxBuffers[i].Mdl = NULL;
            }

            NdisFreeNetBufferList(Adapter->RxBuffers[i].NetBufferList);
            Adapter->RxBuffers[i].NetBufferList = NULL;
            Adapter->RxBuffers[i].Indicated = FALSE;
        }
    }

    if (Adapter->RxNblPool)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
        Adapter->RxNblPool = NULL;
    }

    if (Adapter->RxBufferArea)
    {
        NdisMFreeSharedMemory(Adapter->MiniportHandle,
                              Adapter->RxBufferAreaLength,
                              FALSE,
                              Adapter->RxBufferArea,
                              Adapter->RxBufferPhysical);
        Adapter->RxBufferArea = NULL;
    }

    if (Adapter->RxRing)
    {
        NdisMFreeSharedMemory(Adapter->MiniportHandle,
                              Adapter->RxRingLength,
                              FALSE,
                              Adapter->RxRing,
                              Adapter->RxRingPhysical);
        Adapter->RxRing = NULL;
    }

    if (Adapter->TxBufferArea)
    {
        NdisMFreeSharedMemory(Adapter->MiniportHandle,
                              Adapter->TxBufferAreaLength,
                              FALSE,
                              Adapter->TxBufferArea,
                              Adapter->TxBufferPhysical);
        Adapter->TxBufferArea = NULL;
    }

    if (Adapter->TxRing)
    {
        NdisMFreeSharedMemory(Adapter->MiniportHandle,
                              Adapter->TxRingLength,
                              FALSE,
                              Adapter->TxRing,
                              Adapter->TxRingPhysical);
        Adapter->TxRing = NULL;
    }
}

static NDIS_STATUS
Rp1GemAllocateReceivePath(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
    ULONG i;

    RtlZeroMemory(&PoolParams, sizeof(PoolParams));
    PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParams.Header.Size = sizeof(PoolParams);
    PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParams.fAllocateNetBuffer = TRUE;
    PoolParams.PoolTag = RP1GEM_TAG;
    PoolParams.DataSize = RP1GEM_BUFFER_SIZE;

    Adapter->RxNblPool = NdisAllocateNetBufferListPool(Adapter->MiniportHandle,
                                                       &PoolParams);
    if (!Adapter->RxNblPool)
        return NDIS_STATUS_RESOURCES;

    Adapter->RxRingLength = sizeof(RP1GEM_DMA_DESCRIPTOR) * RP1GEM_RX_RING_SIZE;
    NdisMAllocateSharedMemory(Adapter->MiniportHandle,
                              Adapter->RxRingLength,
                              FALSE,
                              (PVOID *)&Adapter->RxRing,
                              &Adapter->RxRingPhysical);
    if (!Adapter->RxRing)
        return NDIS_STATUS_RESOURCES;

    Adapter->RxBufferAreaLength = RP1GEM_BUFFER_SIZE * RP1GEM_RX_RING_SIZE;
    NdisMAllocateSharedMemory(Adapter->MiniportHandle,
                              Adapter->RxBufferAreaLength,
                              FALSE,
                              &Adapter->RxBufferArea,
                              &Adapter->RxBufferPhysical);
    if (!Adapter->RxBufferArea)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter->RxRing, Adapter->RxRingLength);
    for (i = 0; i < RP1GEM_RX_RING_SIZE; i++)
    {
        Adapter->RxBuffers[i].VirtualAddress =
            (PUCHAR)Adapter->RxBufferArea + (i * RP1GEM_BUFFER_SIZE);
        Rp1GemGetBufferPhysicalAddress(&Adapter->RxBuffers[i].PhysicalAddress,
                                       Adapter->RxBufferPhysical,
                                       i);
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

    Adapter->TxRingLength = sizeof(RP1GEM_DMA_DESCRIPTOR) * RP1GEM_TX_RING_SIZE;
    NdisMAllocateSharedMemory(Adapter->MiniportHandle,
                              Adapter->TxRingLength,
                              FALSE,
                              (PVOID *)&Adapter->TxRing,
                              &Adapter->TxRingPhysical);
    if (!Adapter->TxRing)
        return NDIS_STATUS_RESOURCES;

    Adapter->TxBufferAreaLength = RP1GEM_BUFFER_SIZE * RP1GEM_TX_RING_SIZE;
    NdisMAllocateSharedMemory(Adapter->MiniportHandle,
                              Adapter->TxBufferAreaLength,
                              FALSE,
                              &Adapter->TxBufferArea,
                              &Adapter->TxBufferPhysical);
    if (!Adapter->TxBufferArea)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Adapter->TxRing, Adapter->TxRingLength);
    for (i = 0; i < RP1GEM_TX_RING_SIZE; i++)
    {
        ULONG Control = MACB_TX_USED;

        if (i == (RP1GEM_TX_RING_SIZE - 1))
            Control |= MACB_TX_WRAP;

        Adapter->TxBuffers[i].VirtualAddress =
            (PUCHAR)Adapter->TxBufferArea + (i * RP1GEM_BUFFER_SIZE);
        Rp1GemGetBufferPhysicalAddress(&Adapter->TxBuffers[i].PhysicalAddress,
                                       Adapter->TxBufferPhysical,
                                       i);
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
                GEM_DMACFG_ENDIA_PKT |
                GEM_DMACFG_TXCOEN);
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

    Intmod = GEM_INTMOD_RX(RP1GEM_INTMOD_50US) |
             GEM_INTMOD_TX(RP1GEM_INTMOD_50US);
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

static VOID
Rp1GemResetPhy(
    _In_ PRP1GEM_ADAPTER Adapter)
{
    PHYSICAL_ADDRESS BarBase;
    PHYSICAL_ADDRESS GpioPhysical;
    PHYSICAL_ADDRESS RioPhysical;
    PHYSICAL_ADDRESS PadsPhysical;
    PVOID GpioBase;
    PVOID RioBase;
    PVOID PadsBase;
    ULONG Ctrl;
    ULONG Pad;

    if (Adapter->RegisterPhysical.QuadPart < RP1GEM_ETH_OFFSET)
        return;

    BarBase.QuadPart = Adapter->RegisterPhysical.QuadPart - RP1GEM_ETH_OFFSET;
    GpioPhysical.QuadPart = BarBase.QuadPart + RP1GEM_GPIO_OFFSET;
    RioPhysical.QuadPart = BarBase.QuadPart + RP1GEM_RIO_OFFSET;
    PadsPhysical.QuadPart = BarBase.QuadPart + RP1GEM_PADS_OFFSET;

    GpioBase = MmMapIoSpace(GpioPhysical, RP1GEM_GPIO_WINDOW_LENGTH, MmNonCached);
    RioBase = MmMapIoSpace(RioPhysical, RP1GEM_GPIO_WINDOW_LENGTH, MmNonCached);
    PadsBase = MmMapIoSpace(PadsPhysical, RP1GEM_GPIO_WINDOW_LENGTH, MmNonCached);
    if (!GpioBase || !RioBase || !PadsBase)
    {
        DPRINT1("RP1GEM: failed to map RP1 GPIO sideband for PHY reset\n");
        goto Cleanup;
    }

    /*
     * Raspberry Pi 5 connects the external Ethernet PHY reset to RP1 GPIO32,
     * active low.  Preload the low value before switching the pin to GPIO
     * output so the reset assertion is not skipped.
     */
    Rp1GemWriteSideband32(RioBase,
                          RP1GEM_PHY_RESET_RIO_BANK + RP1_RIO_OUT + RP1_CLR_OFFSET,
                          RP1GEM_PHY_RESET_BIT);
    Rp1GemWriteSideband32(RioBase,
                          RP1GEM_PHY_RESET_RIO_BANK + RP1_RIO_OE + RP1_SET_OFFSET,
                          RP1GEM_PHY_RESET_BIT);

    Pad = Rp1GemReadSideband32(PadsBase, RP1GEM_PHY_RESET_PAD_CTRL);
    Pad &= ~(RP1_PAD_PULL_MASK | RP1_PAD_OUT_DISABLE_MASK);
    Pad |= RP1_PAD_IN_ENABLE_MASK;
    Rp1GemWriteSideband32(PadsBase, RP1GEM_PHY_RESET_PAD_CTRL, Pad);

    Ctrl = Rp1GemReadSideband32(GpioBase, RP1GEM_PHY_RESET_GPIO_CTRL);
    Ctrl &= ~(RP1_GPIO_CTRL_FUNCSEL_MASK |
              RP1_GPIO_CTRL_OUTOVER_MASK |
              RP1_GPIO_CTRL_OEOVER_MASK |
              RP1_GPIO_CTRL_INOVER_MASK);
    Ctrl |= RP1_FSEL_GPIO;
    Rp1GemWriteSideband32(GpioBase, RP1GEM_PHY_RESET_GPIO_CTRL, Ctrl);

    KeStallExecutionProcessor(5000);
    Rp1GemWriteSideband32(RioBase,
                          RP1GEM_PHY_RESET_RIO_BANK + RP1_RIO_OUT + RP1_SET_OFFSET,
                          RP1GEM_PHY_RESET_BIT);
    KeStallExecutionProcessor(20000);

    DPRINT("RP1GEM: PHY reset GPIO%u toggled\n", RP1GEM_PHY_RESET_GPIO);

Cleanup:
    if (PadsBase)
        MmUnmapIoSpace(PadsBase, RP1GEM_GPIO_WINDOW_LENGTH);
    if (RioBase)
        MmUnmapIoSpace(RioBase, RP1GEM_GPIO_WINDOW_LENGTH);
    if (GpioBase)
        MmUnmapIoSpace(GpioBase, RP1GEM_GPIO_WINDOW_LENGTH);
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
    StatusIndication.Header.Size = sizeof(StatusIndication);
    StatusIndication.SourceHandle = Adapter->MiniportHandle;
    StatusIndication.StatusCode = NDIS_STATUS_LINK_STATE;
    StatusIndication.StatusBuffer = &LinkState;
    StatusIndication.StatusBufferSize = sizeof(LinkState);

    NdisMIndicateStatusEx(Adapter->MiniportHandle, &StatusIndication);
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
    }
}

static BOOLEAN
Rp1GemMailboxWaitClear(
    _In_ PVOID MailboxBase,
    _In_ ULONG Offset,
    _In_ ULONG Mask)
{
    ULONG i;

    for (i = 0; i < 1000000; i++)
    {
        if (!(Rp1GemReadSideband32(MailboxBase, Offset) & Mask))
            return TRUE;

        KeStallExecutionProcessor(1);
    }

    return FALSE;
}

static BOOLEAN
Rp1GemMailboxReadResponse(
    _In_ PVOID MailboxBase,
    _In_ ULONG ExpectedMessage)
{
    ULONG i;

    for (i = 0; i < 1000000; i++)
    {
        if (!(Rp1GemReadSideband32(MailboxBase, BCM2712_MBOX_MAIL0_STA) & BCM2712_MBOX_STATUS_EMPTY))
        {
            ULONG Message = Rp1GemReadSideband32(MailboxBase, BCM2712_MBOX_MAIL0_RD);

            if ((Message & 0xf) == BCM2712_MBOX_PROPERTY_CHANNEL &&
                (Message & ~0xf) == ExpectedMessage)
            {
                return TRUE;
            }
        }

        KeStallExecutionProcessor(1);
    }

    return FALSE;
}

static BOOLEAN
Rp1GemReadFirmwareMacProperty(
    _In_ ULONG Tag,
    _In_ PCSTR Name,
    _Out_writes_(ETH_LENGTH_OF_ADDRESS) UCHAR *MacAddress)
{
    PHYSICAL_ADDRESS LowAddress, HighAddress, BoundaryAddress, MailboxPhysical, MessagePhysical;
    PRP1GEM_MAILBOX_PROPERTY Message;
    PVOID MailboxBase;
    ULONG MailboxMessage;
    BOOLEAN Success = FALSE;

    LowAddress.QuadPart = 0;
    HighAddress.QuadPart = 0xffffffffULL;
    BoundaryAddress.QuadPart = 0;
    Message = MmAllocateContiguousMemorySpecifyCache(sizeof(*Message),
                                                     LowAddress,
                                                     HighAddress,
                                                     BoundaryAddress,
                                                     MmNonCached);
    if (!Message)
        return FALSE;

    MessagePhysical = MmGetPhysicalAddress(Message);
    if (MessagePhysical.HighPart != 0 || (MessagePhysical.LowPart & 0xf))
    {
        DPRINT1("RP1GEM: firmware MAC mailbox buffer unusable pa=0x%I64x\n",
                MessagePhysical.QuadPart);
        goto CleanupMessage;
    }

    MailboxPhysical.QuadPart = BCM2712_MBOX_PHYS;
    MailboxBase = MmMapIoSpace(MailboxPhysical, BCM2712_MBOX_LENGTH, MmNonCached);
    if (!MailboxBase)
        goto CleanupMessage;

    RtlZeroMemory(Message, sizeof(*Message));
    Message->Size = sizeof(*Message);
    Message->Code = 0;
    Message->Tag = Tag;
    Message->ValueSize = ETH_LENGTH_OF_ADDRESS;
    Message->RequestResponse = 0;
    Message->EndTag = 0;
    KeMemoryBarrier();

    MailboxMessage = MessagePhysical.LowPart | BCM2712_MBOX_PROPERTY_CHANNEL;
    if (!Rp1GemMailboxWaitClear(MailboxBase,
                                BCM2712_MBOX_MAIL1_STA,
                                BCM2712_MBOX_STATUS_FULL))
    {
        DPRINT1("RP1GEM: firmware MAC mailbox %s write FIFO stayed full\n", Name);
        goto CleanupMailbox;
    }

    Rp1GemWriteSideband32(MailboxBase, BCM2712_MBOX_MAIL1_WRT, MailboxMessage);
    if (!Rp1GemMailboxReadResponse(MailboxBase, MessagePhysical.LowPart))
    {
        DPRINT1("RP1GEM: firmware MAC mailbox %s timed out\n", Name);
        goto CleanupMailbox;
    }

    KeMemoryBarrier();
    if (Message->Code == BCM2712_MBOX_PROPERTY_SUCCESS &&
        (Message->RequestResponse & BCM2712_MBOX_TAG_RESPONSE) &&
        (Message->RequestResponse & ~BCM2712_MBOX_TAG_RESPONSE) >= ETH_LENGTH_OF_ADDRESS)
    {
        RtlCopyMemory(MacAddress, Message->Value, ETH_LENGTH_OF_ADDRESS);
        Success = Rp1GemIsValidMacAddress(MacAddress);
    }

    DPRINT("RP1GEM: firmware MAC %s tag=0x%08lx status=0x%08lx response=0x%08lx value=%02x:%02x:%02x:%02x:%02x:%02x valid=%u\n",
           Name,
           Tag,
           Message->Code,
           Message->RequestResponse,
           ((PUCHAR)Message->Value)[0],
           ((PUCHAR)Message->Value)[1],
           ((PUCHAR)Message->Value)[2],
           ((PUCHAR)Message->Value)[3],
           ((PUCHAR)Message->Value)[4],
           ((PUCHAR)Message->Value)[5],
           Success ? 1 : 0);

CleanupMailbox:
    MmUnmapIoSpace(MailboxBase, BCM2712_MBOX_LENGTH);

CleanupMessage:
    MmFreeContiguousMemorySpecifyCache(Message, sizeof(*Message), MmNonCached);
    return Success;
}

static BOOLEAN
Rp1GemReadFirmwareMacAddress(
    _Out_writes_(ETH_LENGTH_OF_ADDRESS) UCHAR *MacAddress)
{
    if (Rp1GemReadFirmwareMacProperty(BCM2712_MBOX_GET_ETHERNET_MAC,
                                      "ethernet",
                                      MacAddress))
    {
        return TRUE;
    }

    return Rp1GemReadFirmwareMacProperty(BCM2712_MBOX_GET_BOARD_MAC,
                                         "board",
                                         MacAddress);
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
        UCHAR FirmwareAddress[ETH_LENGTH_OF_ADDRESS];

        DPRINT("RP1GEM: hardware MAC registers invalid SA1B=0x%08lx SA1T=0x%08lx\n",
               Sa1b,
               Sa1t);

        if (Rp1GemReadFirmwareMacAddress(FirmwareAddress))
        {
            RtlCopyMemory(Adapter->PermanentMacAddress,
                          FirmwareAddress,
                          ETH_LENGTH_OF_ADDRESS);
            DPRINT("RP1GEM: using Raspberry Pi firmware MAC address\n");
        }
        else
        {
            RtlCopyMemory(Adapter->PermanentMacAddress,
                          FallbackAddress,
                          sizeof(FallbackAddress));
            DPRINT("RP1GEM: using fallback locally-administered MAC address\n");
        }
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
    GenAttrs.Header.Size = sizeof(GenAttrs);
    GenAttrs.MediaType = NdisMedium802_3;
    GenAttrs.PhysicalMediumType = (NDIS_MEDIUM)NdisPhysicalMedium802_3;
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

                    if (!Adapter->Rp1ApbsBase &&
                        Adapter->RegisterPhysical.QuadPart >= RP1GEM_ETH_OFFSET)
                    {
                        PHYSICAL_ADDRESS ApbsPhysical;

                        ApbsPhysical.QuadPart =
                            Adapter->RegisterPhysical.QuadPart -
                            RP1GEM_ETH_OFFSET +
                            RP1GEM_PCIE_APBS_OFFSET;
                        Adapter->Rp1ApbsLength = RP1GEM_PCIE_APBS_LENGTH;
                        Status = NdisMMapIoSpace(&Adapter->Rp1ApbsBase,
                                                 Adapter->MiniportHandle,
                                                 ApbsPhysical,
                                                 Adapter->Rp1ApbsLength);
                        if (Status == NDIS_STATUS_SUCCESS)
                        {
                            DPRINT("RP1GEM: RP1 APBS interrupt ack phys=0x%I64x len=0x%lx va=%p\n",
                                   ApbsPhysical.QuadPart,
                                   Adapter->Rp1ApbsLength,
                                   Adapter->Rp1ApbsBase);
                        }
                        else
                        {
                            Adapter->Rp1ApbsBase = NULL;
                            Adapter->Rp1ApbsLength = 0;
                            DPRINT1("RP1GEM: RP1 APBS interrupt ack map failed 0x%08x\n",
                                    Status);
                        }
                    }

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

    Rp1GemResetPhy(Adapter);
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
    PNET_BUFFER_LIST CompleteHead = NULL;
    PNET_BUFFER_LIST CompleteTail = NULL;

    if (!Adapter->DatapathReady)
        return;

    NdisAcquireSpinLock(&Adapter->TxLock);

    while (Adapter->TxFree < RP1GEM_TX_RING_SIZE)
    {
        ULONG Index = Adapter->TxTail;
        PRP1GEM_TX_BUFFER TxBuffer = &Adapter->TxBuffers[Index];
        PNET_BUFFER_LIST Nbl = TxBuffer->NetBufferList;
        ULONG Length = TxBuffer->Length;
        ULONG Control = Adapter->TxRing[Index].Control;

        if (!Nbl || !(Control & MACB_TX_USED))
            break;

        TxBuffer->NetBufferList = NULL;
        TxBuffer->Length = 0;
        Adapter->TxTail = (Index + 1) % RP1GEM_TX_RING_SIZE;
        Adapter->TxFree++;

        if (Control & MACB_TX_ERROR_MASK)
        {
            Adapter->TxErrors++;
            if (Nbl)
                NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_FAILURE;
        }
        else
        {
            Adapter->TxPackets++;
            Adapter->TxBytes += Length;
            if (Nbl)
                NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        }

        if (Nbl)
        {
            NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
            if (!CompleteHead)
                CompleteHead = Nbl;
            else
                NET_BUFFER_LIST_NEXT_NBL(CompleteTail) = Nbl;
            CompleteTail = Nbl;
        }
    }

    NdisReleaseSpinLock(&Adapter->TxLock);

    if (CompleteHead)
        NdisMSendNetBufferListsComplete(Adapter->MiniportHandle, CompleteHead, CompleteFlags);

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

static VOID
Rp1GemReleaseReceiveNetBufferLists(
    _In_ PRP1GEM_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferLists)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST NextNbl;

    for (Nbl = NetBufferLists; Nbl; Nbl = NextNbl)
    {
        ULONG Index;
        PMDL Mdl = NULL;

        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
        Index = (ULONG)(ULONG_PTR)NET_BUFFER_LIST_MINIPORT_RESERVED(Nbl)[0];

        NdisAcquireSpinLock(&Adapter->RxLock);
        if (Index < RP1GEM_RX_RING_SIZE &&
            Adapter->RxBuffers[Index].NetBufferList == Nbl)
        {
            Mdl = Adapter->RxBuffers[Index].Mdl;
            Adapter->RxBuffers[Index].Mdl = NULL;
            Adapter->RxBuffers[Index].NetBufferList = NULL;
            Adapter->RxBuffers[Index].Indicated = FALSE;
            Rp1GemRearmRxDescriptor(Adapter, Index);
        }
        NdisReleaseSpinLock(&Adapter->RxLock);

        if (Mdl)
            IoFreeMdl(Mdl);

        NdisFreeNetBufferList(Nbl);
    }
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
        PMDL Mdl;
        PNET_BUFFER_LIST Nbl;

        KeMemoryBarrier();
        if (!(Address & MACB_RX_USED))
            break;

        KeMemoryBarrier();
        Control = Descriptor->Control;
        Length = Control & MACB_RX_FRMLEN_MASK;

        if ((Control & (MACB_RX_SOF | MACB_RX_EOF)) != (MACB_RX_SOF | MACB_RX_EOF) ||
            Length < ETH_LENGTH_OF_ADDRESS ||
            Length > RP1GEM_FRAME_SIZE ||
            RxBuffer->Indicated)
        {
            Adapter->RxErrors++;
            Rp1GemRearmRxDescriptor(Adapter, Index);
            Adapter->RxTail = (Index + 1) % RP1GEM_RX_RING_SIZE;
            continue;
        }

        Mdl = Rp1GemAllocateMdl(RxBuffer->VirtualAddress, Length);
        if (!Mdl)
        {
            Adapter->RxNoBuffer++;
            Rp1GemRearmRxDescriptor(Adapter, Index);
            Adapter->RxTail = (Index + 1) % RP1GEM_RX_RING_SIZE;
            continue;
        }

        Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool,
                                                    0,
                                                    0,
                                                    Mdl,
                                                    0,
                                                    Length);
        if (!Nbl)
        {
            IoFreeMdl(Mdl);
            Adapter->RxNoBuffer++;
            Rp1GemRearmRxDescriptor(Adapter, Index);
            Adapter->RxTail = (Index + 1) % RP1GEM_RX_RING_SIZE;
            continue;
        }

        RxBuffer->Mdl = Mdl;
        RxBuffer->NetBufferList = Nbl;
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
        ULONG Flags = NDIS_RECEIVE_FLAGS_RESOURCES;

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
        Rp1GemAckRp1Interrupt(Adapter);
        return FALSE;
    }

    Adapter->InterruptRecognizedCount++;
    Rp1GemDisableInterrupts(Adapter);
    InterlockedExchange(&Adapter->InterruptPending, (LONG)Isr);
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
    ULONG Pending;

    UNREFERENCED_PARAMETER(MiniportDpcContext);
    UNREFERENCED_PARAMETER(ReceiveThrottleParameters);
    UNREFERENCED_PARAMETER(NdisReserved2);

    if (!Adapter || !Adapter->RegisterBase)
        return;

    Pending = (ULONG)InterlockedExchange(&Adapter->InterruptPending, 0);
    Adapter->InterruptDpcCount++;

    if (Pending & (MACB_INT_RCOMP | MACB_INT_RXUBR | MACB_INT_ROVR | MACB_INT_HRESP))
        Rp1GemPollReceive(Adapter, RP1GEM_RX_BUDGET);

    if (Pending & (MACB_INT_TCOMP | MACB_INT_TXERR | MACB_INT_TUND | MACB_INT_RLE | MACB_INT_TXUBR))
        Rp1GemDrainTxCompletions(Adapter, NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL);

    Rp1GemAckRp1Interrupt(Adapter);
    Rp1GemEnableInterrupts(Adapter);
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
    IntChars.Header.Size = sizeof(IntChars);
    IntChars.InterruptHandler = Rp1GemInterrupt;
    IntChars.InterruptDpcHandler = Rp1GemInterruptDpc;
    IntChars.DisableInterruptHandler = Rp1GemDisableInterruptEx;
    IntChars.EnableInterruptHandler = Rp1GemEnableInterruptEx;
    IntChars.InterruptType = NDIS_CONNECT_LINE_BASED;
    IntChars.MsiSupported = TRUE;

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
        Rp1GemUnmapResources(Adapter);
        NdisFreeSpinLock(&Adapter->TxLock);
        NdisFreeSpinLock(&Adapter->RxLock);
        ExFreePoolWithTag(Adapter, RP1GEM_TAG);
        return Status;
    }

    Rp1GemProbeHardware(Adapter);

    Status = Rp1GemInitializeDatapath(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RP1GEM: datapath initialization failed 0x%08x\n", Status);
        Rp1GemStopHardware(Adapter);
        Rp1GemFreeDatapath(Adapter);
        Rp1GemUnmapResources(Adapter);
        NdisFreeSpinLock(&Adapter->TxLock);
        NdisFreeSpinLock(&Adapter->RxLock);
        ExFreePoolWithTag(Adapter, RP1GEM_TAG);
        return Status;
    }

    Status = Rp1GemRegisterInterrupt(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Rp1GemStopHardware(Adapter);
        Rp1GemFreeDatapath(Adapter);
        Rp1GemUnmapResources(Adapter);
        NdisFreeSpinLock(&Adapter->TxLock);
        NdisFreeSpinLock(&Adapter->RxLock);
        ExFreePoolWithTag(Adapter, RP1GEM_TAG);
        return Status;
    }

    Status = Rp1GemSetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("RP1GEM: general attributes failed 0x%08x\n", Status);
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
    Rp1GemStartLinkTimer(Adapter);
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

        NextNbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
        if (!Adapter->DatapathReady ||
            Adapter->MediaConnectState != MediaConnectStateConnected)
        {
            Status = NDIS_STATUS_MEDIA_DISCONNECTED;
            goto FailNbl;
        }

        if (!NetBuffer || NET_BUFFER_NEXT_NB(NetBuffer))
        {
            Status = NDIS_STATUS_INVALID_PACKET;
            goto FailNbl;
        }

        if (NET_BUFFER_DATA_LENGTH(NetBuffer) > RP1GEM_FRAME_SIZE ||
            NET_BUFFER_DATA_LENGTH(NetBuffer) < ETH_LENGTH_OF_ADDRESS)
        {
            Status = NDIS_STATUS_INVALID_LENGTH;
            goto FailNbl;
        }

        NdisAcquireSpinLock(&Adapter->TxLock);
        if (Adapter->TxFree == 0)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            Status = NDIS_STATUS_RESOURCES;
            goto FailNbl;
        }

        Index = Adapter->TxHead;
        Status = Rp1GemCopyNetBuffer(NetBuffer,
                                     Adapter->TxBuffers[Index].VirtualAddress,
                                     RP1GEM_BUFFER_SIZE,
                                     &Length);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            NdisReleaseSpinLock(&Adapter->TxLock);
            goto FailNbl;
        }

        Adapter->TxBuffers[Index].NetBufferList = Nbl;
        Adapter->TxBuffers[Index].Length = Length;
        Adapter->TxHead = (Index + 1) % RP1GEM_TX_RING_SIZE;
        Adapter->TxFree--;

        Rp1GemSetDescriptorAddress(Adapter,
                                   &Adapter->TxRing[Index],
                                   Adapter->TxBuffers[Index].PhysicalAddress,
                                   0);
        KeMemoryBarrier();
        Adapter->TxRing[Index].Control =
            Length |
            MACB_TX_LAST |
            ((Index == (RP1GEM_TX_RING_SIZE - 1)) ? MACB_TX_WRAP : 0);

        NdisReleaseSpinLock(&Adapter->TxLock);
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

    if (FailHead)
        NdisMSendNetBufferListsComplete(Adapter->MiniportHandle, FailHead, CompleteFlags);

    Rp1GemDrainTxCompletions(Adapter, CompleteFlags);
}

static VOID NTAPI
Rp1GemReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PRP1GEM_ADAPTER Adapter = (PRP1GEM_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(ReturnFlags);

    if (Adapter)
        Rp1GemReleaseReceiveNetBufferLists(Adapter, NetBufferLists);
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
    Chars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Chars.Header.Size = sizeof(Chars);
    Chars.MajorNdisVersion = 6;
    Chars.MinorNdisVersion = 20;
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
