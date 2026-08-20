/*
 * PROJECT:     ReactOS Synopsys DWC2 USB Miniport Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     USBPORT miniport for the DWC2 host controller used by RPi3
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "usbdwc2.h"

#include <debug.h>

USBPORT_REGISTRATION_PACKET Dwc2RegPacket;

static ULONG
Dwc2ReadRegister(
    _In_ PDWC2_EXTENSION Extension,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((PULONG)(Extension->Registers + Offset));
}

static VOID
Dwc2WriteRegister(
    _In_ PDWC2_EXTENSION Extension,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((PULONG)(Extension->Registers + Offset), Value);
}

static BOOLEAN
Dwc2ShouldLogCount(
    _In_ ULONG Count)
{
    return Count <= 4 || !(Count & (Count - 1));
}

static VOID
Dwc2DumpControllerState(
    _In_ PDWC2_EXTENSION Extension,
    _In_ PCSTR Reason)
{
    DPRINT1("[DWC2] controller state (%s): GSNPSID=%08lx GHWCFG1=%08lx GHWCFG2=%08lx GHWCFG3=%08lx GHWCFG4=%08lx GOTGCTL=%08lx GAHBCFG=%08lx GUSBCFG=%08lx GRSTCTL=%08lx PCGCCTL=%08lx\n",
            Reason,
            Dwc2ReadRegister(Extension, DWC2_GSNPSID),
            Dwc2ReadRegister(Extension, DWC2_GHWCFG1),
            Dwc2ReadRegister(Extension, DWC2_GHWCFG2),
            Dwc2ReadRegister(Extension, DWC2_GHWCFG3),
            Dwc2ReadRegister(Extension, DWC2_GHWCFG4),
            Dwc2ReadRegister(Extension, DWC2_GOTGCTL),
            Dwc2ReadRegister(Extension, DWC2_GAHBCFG),
            Dwc2ReadRegister(Extension, DWC2_GUSBCFG),
            Dwc2ReadRegister(Extension, DWC2_GRSTCTL),
            Dwc2ReadRegister(Extension, DWC2_PCGCCTL));
    DPRINT1("[DWC2] host state (%s): GINTSTS=%08lx GINTMSK=%08lx GRXFSIZ=%08lx GNPTXFSIZ=%08lx GNPTXSTS=%08lx HPTXFSIZ=%08lx HPTXSTS=%08lx HCFG=%08lx HFIR=%08lx HFNUM=%08lx HAINT=%08lx HAINTMSK=%08lx HPRT0=%08lx\n",
            Reason,
            Dwc2ReadRegister(Extension, DWC2_GINTSTS),
            Dwc2ReadRegister(Extension, DWC2_GINTMSK),
            Dwc2ReadRegister(Extension, DWC2_GRXFSIZ),
            Dwc2ReadRegister(Extension, DWC2_GNPTXFSIZ),
            Dwc2ReadRegister(Extension, DWC2_GNPTXSTS),
            Dwc2ReadRegister(Extension, DWC2_HPTXFSIZ),
            Dwc2ReadRegister(Extension, DWC2_HPTXSTS),
            Dwc2ReadRegister(Extension, DWC2_HCFG),
            Dwc2ReadRegister(Extension, DWC2_HFIR),
            Dwc2ReadRegister(Extension, DWC2_HFNUM),
            Dwc2ReadRegister(Extension, DWC2_HAINT),
            Dwc2ReadRegister(Extension, DWC2_HAINTMSK),
            Dwc2ReadRegister(Extension, DWC2_HPRT0));
}

static VOID
Dwc2DumpChannelState(
    _In_ PDWC2_EXTENSION Extension,
    _In_ UCHAR Channel,
    _In_ PCSTR Reason)
{
    Dwc2DumpControllerState(Extension, Reason);
    if (Channel >= Extension->NumberOfChannels)
    {
        DPRINT1("[DWC2] channel state (%s): invalid channel=%u available=%lu\n", Reason, Channel, Extension->NumberOfChannels);
        return;
    }

    DPRINT1("[DWC2] channel state (%s): CH%u HCCHAR=%08lx HCSPLT=%08lx HCINT=%08lx HCINTMSK=%08lx HCTSIZ=%08lx HCDMA=%08lx\n",
            Reason,
            Channel,
            Dwc2ReadRegister(Extension, DWC2_HCCHAR(Channel)),
            Dwc2ReadRegister(Extension, DWC2_HCSPLT(Channel)),
            Dwc2ReadRegister(Extension, DWC2_HCINT(Channel)),
            Dwc2ReadRegister(Extension, DWC2_HCINTMSK(Channel)),
            Dwc2ReadRegister(Extension, DWC2_HCTSIZ(Channel)),
            Dwc2ReadRegister(Extension, DWC2_HCDMA(Channel)));
}

static VOID
Dwc2DumpTransferState(
    _In_ PDWC2_EXTENSION Extension,
    _In_opt_ PDWC2_TRANSFER Transfer,
    _In_ PCSTR Reason)
{
    PDWC2_ENDPOINT Endpoint;
    ULONG TransferFlags = 0;
    ULONG TransferLength = 0;

    if (!Transfer)
    {
        DPRINT1("[DWC2] transfer state (%s): NULL transfer\n", Reason);
        Dwc2DumpControllerState(Extension, Reason);
        return;
    }

    Endpoint = Transfer->Endpoint;
    if (Transfer->Parameters)
    {
        TransferFlags = Transfer->Parameters->TransferFlags;
        TransferLength = Transfer->Parameters->TransferBufferLength;
    }

    DPRINT1("[DWC2] transfer state (%s): transfer=%p endpoint=%p parameters=%p stage=%u status=%08lx channel=%u done=%u dir=%s bytes=%lu/%lu programmed=%lu dma=%08lx packets=%lu toggle=%lu nak=%lu pendingHcint=%08lx errors=%lu recoveries=%lu needsRetry=%u retryTime=%I64u completeSplit=%u flags=%08lx submit=%I64u stageStart=%I64u stageFrame=%08lx complete=%I64u now=%I64u\n",
            Reason,
            Transfer,
            Endpoint,
            Transfer->Parameters,
            Transfer->Stage,
            Transfer->UsbdStatus,
            Transfer->Channel,
            Transfer->Done,
            Transfer->DirectionIn ? "IN" : "OUT",
            Transfer->BytesTransferred,
            TransferLength,
            Transfer->ProgrammedLength,
            Transfer->ProgrammedDmaAddress,
            Transfer->InitialPacketCount,
            Transfer->DataToggle,
            Transfer->NakCount,
            Transfer->PendingChannelStatus,
            Transfer->ErrorCount,
            Transfer->RecoveryCount,
            Transfer->NeedsRetry,
            Transfer->RetryTime,
            Transfer->CompleteSplit,
            TransferFlags,
            Transfer->SubmitTime,
            Transfer->StageStartTime,
            Transfer->StageStartFrame,
            Transfer->CompletionTime,
            KeQueryInterruptTime());

    if (Transfer->Parameters && Endpoint &&
        Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_CONTROL)
    {
        DPRINT1("[DWC2] transfer setup (%s): %02x/%02x/%04x/%04x/%u\n",
                Reason,
                Transfer->Parameters->SetupPacket.bmRequestType.B,
                Transfer->Parameters->SetupPacket.bRequest,
                Transfer->Parameters->SetupPacket.wValue.W,
                Transfer->Parameters->SetupPacket.wIndex.W,
                Transfer->Parameters->SetupPacket.wLength);
    }

    if (Endpoint)
    {
        DPRINT1("[DWC2] endpoint state (%s): endpoint=%p state=%lu status=%lu addr=%u ep=%u type=%lu speed=%u mps=%lu period=%u interval=%u hub=%u port=%u tt=%u/%u split=%u channel=%u submits=%lu states=%lu buffer=%08lx/%p/%lu\n",
                Reason,
                Endpoint,
                Endpoint->State,
                Endpoint->Status,
                Endpoint->Properties.DeviceAddress,
                Endpoint->Properties.EndpointAddress,
                Endpoint->Properties.TransferType,
                Endpoint->Properties.DeviceSpeed,
                Endpoint->Properties.MaxPacketSize,
                Endpoint->Properties.Period,
                Endpoint->Properties.Reserved4,
                Endpoint->Properties.HubAddr,
                Endpoint->Properties.PortNumber,
                Endpoint->Properties.TtHubAddr,
                Endpoint->Properties.TtPortNumber,
                Endpoint->RequiresSplit,
                Endpoint->Channel,
                Endpoint->SubmitCount,
                Endpoint->StateChangeCount,
                Endpoint->BufferPA,
                Endpoint->BufferVA,
                Endpoint->BufferLength);
    }

    if (Transfer->Channel < Extension->NumberOfChannels)
        Dwc2DumpChannelState(Extension, Transfer->Channel, Reason);
    else
        Dwc2DumpControllerState(Extension, Reason);
}

static BOOLEAN
Dwc2ValidPort(
    _In_ USHORT Port)
{
    return Port == 1;
}

static ULONG
Dwc2EndpointDataCapacity(
    _In_ ULONG TransferType)
{
    return TransferType == USBPORT_TRANSFER_TYPE_BULK ? DWC2_BULK_TRANSFER_SIZE : DWC2_SMALL_TRANSFER_SIZE;
}

static ULONG
Dwc2CopySgToBuffer(
    _In_ PUSBPORT_SCATTER_GATHER_LIST SgList,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength)
{
    PUCHAR Base;
    ULONG Index;
    ULONG Copied = 0;

    if (BufferLength == 0)
        return 0;

    if (!SgList || !SgList->MappedSystemVa || !Buffer)
        return 0;

    Base = SgList->MappedSystemVa;

    for (Index = 0; Index < SgList->SgElementCount; Index++)
    {
        ULONG Offset = SgList->SgElement[Index].SgOffset;
        ULONG Length = SgList->SgElement[Index].SgTransferLength;

        if (Offset >= BufferLength)
            break;

        if (Length > BufferLength - Offset)
            Length = BufferLength - Offset;

        RtlCopyMemory((PUCHAR)Buffer + Offset, Base + Offset, Length);
        Copied += Length;
    }

    return Copied;
}

static ULONG
Dwc2CopyBufferToSg(
    _In_ PUSBPORT_SCATTER_GATHER_LIST SgList,
    _In_reads_bytes_(BufferLength) const VOID *Buffer,
    _In_ ULONG BufferLength)
{
    PUCHAR Base;
    ULONG Index;
    ULONG Copied = 0;

    if (BufferLength == 0)
        return 0;

    if (!SgList || !SgList->MappedSystemVa || !Buffer)
        return 0;

    Base = SgList->MappedSystemVa;

    for (Index = 0; Index < SgList->SgElementCount; Index++)
    {
        ULONG Offset = SgList->SgElement[Index].SgOffset;
        ULONG Length = SgList->SgElement[Index].SgTransferLength;

        if (Offset >= BufferLength)
            break;

        if (Length > BufferLength - Offset)
            Length = BufferLength - Offset;

        RtlCopyMemory(Base + Offset, (const PUCHAR)Buffer + Offset, Length);
        Copied += Length;
    }

    return Copied;
}

static ULONG
Dwc2EndpointType(
    _In_ ULONG TransferType)
{
    switch (TransferType)
    {
        case USBPORT_TRANSFER_TYPE_CONTROL:
            return DWC2_EPTYPE_CONTROL;

        case USBPORT_TRANSFER_TYPE_BULK:
            return DWC2_EPTYPE_BULK;

        case USBPORT_TRANSFER_TYPE_INTERRUPT:
            return DWC2_EPTYPE_INTERRUPT;

        default:
            return DWC2_EPTYPE_ISOCHRONOUS;
    }
}

static VOID
Dwc2WritePort(
    _In_ PDWC2_EXTENSION Extension,
    _In_ ULONG SetBits,
    _In_ ULONG ClearBits,
    _In_ ULONG AckBits)
{
    ULONG PortValue;

    PortValue = Dwc2ReadRegister(Extension, DWC2_HPRT0);
    PortValue &= ~DWC2_HPRT_W1C_MASK;
    PortValue &= ~ClearBits;
    PortValue |= SetBits;
    PortValue |= AckBits & DWC2_HPRT_W1C_MASK;
    Dwc2WriteRegister(Extension, DWC2_HPRT0, PortValue);
}

static VOID
Dwc2SetGlobalInterruptEnable(
    _In_ PDWC2_EXTENSION Extension,
    _In_ BOOLEAN Enable)
{
    ULONG AhbConfig;
    ULONG AhbConfigReadback;
    ULONG PendingInterrupts;
    ULONG RaceCount;

    AhbConfig = Dwc2ReadRegister(Extension, DWC2_GAHBCFG);

    if (Enable)
        AhbConfig |= DWC2_GAHBCFG_GLBL_INTR_EN;
    else
        AhbConfig &= ~DWC2_GAHBCFG_GLBL_INTR_EN;

    Dwc2WriteRegister(Extension, DWC2_GAHBCFG, AhbConfig);
    AhbConfigReadback = Dwc2ReadRegister(Extension, DWC2_GAHBCFG);
    if (!!(AhbConfigReadback & DWC2_GAHBCFG_GLBL_INTR_EN) != !!Enable)
    {
        PendingInterrupts = (ULONG)InterlockedCompareExchange(&Extension->PendingGlobalInterrupts, 0, 0);
        PendingInterrupts |= (ULONG)InterlockedCompareExchange(&Extension->PendingChannelInterrupts, 0, 0);
        PendingInterrupts |= Dwc2ReadRegister(Extension, DWC2_GINTSTS) & Dwc2ReadRegister(Extension, DWC2_GINTMSK);
        if (Enable && PendingInterrupts)
        {
            RaceCount = (ULONG)InterlockedIncrement(&Extension->GlobalEnableRaceCount);
            if (Dwc2ShouldLogCount(RaceCount))
                DPRINT1("[DWC2] global interrupt enable raced ISR masking pending=%08lx count=%lu\n", PendingInterrupts, RaceCount);
            return;
        }

        DPRINT1("[DWC2] global interrupt enable readback mismatch requested=%u wrote=%08lx read=%08lx pending=%08lx\n", Enable, AhbConfig, AhbConfigReadback, PendingInterrupts);
        Dwc2DumpControllerState(Extension, "global interrupt enable readback failure");
    }
}

static BOOLEAN
Dwc2CoreReset(
    _In_ PDWC2_EXTENSION Extension)
{
    ULONG Index;

    for (Index = 0; Index < 10000; Index++)
    {
        if (Dwc2ReadRegister(Extension, DWC2_GRSTCTL) & DWC2_GRSTCTL_AHBIDLE)
            break;

        KeStallExecutionProcessor(10);
    }

    if (Index == 10000)
    {
        DPRINT1("[DWC2] AHB did not become idle before reset\n");
        Dwc2DumpControllerState(Extension, "AHB idle timeout");
        return FALSE;
    }

    Dwc2WriteRegister(Extension, DWC2_GRSTCTL, DWC2_GRSTCTL_CSFTRST);

    for (Index = 0; Index < 10000; Index++)
    {
        if (!(Dwc2ReadRegister(Extension, DWC2_GRSTCTL) & DWC2_GRSTCTL_CSFTRST))
            break;

        KeStallExecutionProcessor(10);
    }

    if (Index == 10000)
    {
        DPRINT1("[DWC2] core soft reset timed out\n");
        Dwc2DumpControllerState(Extension, "core reset timeout");
        return FALSE;
    }

    /*
     * The PHY and internal state machines need time to settle after the
     * reset bit clears.  In particular, the BCM2837 core can otherwise
     * remain nominally in host mode while refusing to service a channel.
     */
    Dwc2RegPacket.UsbPortWait(Extension, 100);
    return TRUE;
}

static BOOLEAN
Dwc2FlushFifos(
    _In_ PDWC2_EXTENSION Extension)
{
    ULONG Index;

    Dwc2WriteRegister(Extension, DWC2_GRSTCTL, DWC2_GRSTCTL_TXFNUM_ALL | DWC2_GRSTCTL_TXFFLSH);
    for (Index = 0; Index < 1000; Index++)
    {
        if (!(Dwc2ReadRegister(Extension, DWC2_GRSTCTL) & DWC2_GRSTCTL_TXFFLSH))
            break;

        KeStallExecutionProcessor(10);
    }

    if (Index == 1000)
    {
        DPRINT1("[DWC2] transmit FIFO flush timed out GRSTCTL=%08lx\n", Dwc2ReadRegister(Extension, DWC2_GRSTCTL));
        Dwc2DumpControllerState(Extension, "TX FIFO flush timeout");
        return FALSE;
    }

    Dwc2WriteRegister(Extension, DWC2_GRSTCTL, DWC2_GRSTCTL_RXFFLSH);
    for (Index = 0; Index < 1000; Index++)
    {
        if (!(Dwc2ReadRegister(Extension, DWC2_GRSTCTL) & DWC2_GRSTCTL_RXFFLSH))
            break;

        KeStallExecutionProcessor(10);
    }

    if (Index == 1000)
    {
        DPRINT1("[DWC2] receive FIFO flush timed out GRSTCTL=%08lx\n", Dwc2ReadRegister(Extension, DWC2_GRSTCTL));
        Dwc2DumpControllerState(Extension, "RX FIFO flush timeout");
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
Dwc2HaltChannel(
    _In_ PDWC2_EXTENSION Extension,
    _In_ UCHAR Channel);

static VOID
Dwc2DiagnoseStalledChannels(
    _In_ PDWC2_EXTENSION Extension);

static BOOLEAN
Dwc2ConfigureFifos(
    _In_ PDWC2_EXTENSION Extension,
    _In_ ULONG HardwareConfig)
{
    ULONG FifoDepth;
    ULONG HardwareConfig3;
    ULONG NonPeriodicSize;
    ULONG PeriodicSize;
    ULONG RequiredDepth;

    if (!(HardwareConfig & DWC2_GHWCFG2_DYNAMIC_FIFO))
    {
        DPRINT1("[DWC2] static FIFO configuration retained GHWCFG2=%08lx GHWCFG3=%08lx RX=%08lx NP=%08lx P=%08lx\n",
                HardwareConfig,
                Dwc2ReadRegister(Extension, DWC2_GHWCFG3),
                Dwc2ReadRegister(Extension, DWC2_GRXFSIZ),
                Dwc2ReadRegister(Extension, DWC2_GNPTXFSIZ),
                Dwc2ReadRegister(Extension, DWC2_HPTXFSIZ));
        return TRUE;
    }

    HardwareConfig3 = Dwc2ReadRegister(Extension, DWC2_GHWCFG3);
    FifoDepth = (HardwareConfig3 & DWC2_GHWCFG3_DFIFO_DEPTH_MASK) >> DWC2_GHWCFG3_DFIFO_DEPTH_SHIFT;
    RequiredDepth = DWC2_HOST_RX_FIFO_SIZE + DWC2_HOST_NPERIODIC_TX_FIFO_SIZE + DWC2_HOST_PERIODIC_TX_FIFO_SIZE;
    DPRINT1("[DWC2] dynamic FIFO before partition depth=%lu required=%lu RX=%08lx NP=%08lx P=%08lx\n",
            FifoDepth,
            RequiredDepth,
            Dwc2ReadRegister(Extension, DWC2_GRXFSIZ),
            Dwc2ReadRegister(Extension, DWC2_GNPTXFSIZ),
            Dwc2ReadRegister(Extension, DWC2_HPTXFSIZ));
    if (FifoDepth < RequiredDepth)
    {
        DPRINT1("[DWC2] dynamic FIFO too small: depth=%lu required=%lu; preserving firmware partition\n", FifoDepth, RequiredDepth);
        Dwc2DumpControllerState(Extension, "dynamic FIFO too small");
        return TRUE;
    }

    NonPeriodicSize = (DWC2_HOST_NPERIODIC_TX_FIFO_SIZE << DWC2_FIFOSIZE_DEPTH_SHIFT) | DWC2_HOST_RX_FIFO_SIZE;
    PeriodicSize = (DWC2_HOST_PERIODIC_TX_FIFO_SIZE << DWC2_FIFOSIZE_DEPTH_SHIFT) | (DWC2_HOST_RX_FIFO_SIZE + DWC2_HOST_NPERIODIC_TX_FIFO_SIZE);
    Dwc2WriteRegister(Extension, DWC2_GRXFSIZ, DWC2_HOST_RX_FIFO_SIZE);
    Dwc2WriteRegister(Extension, DWC2_GNPTXFSIZ, NonPeriodicSize);
    Dwc2WriteRegister(Extension, DWC2_HPTXFSIZ, PeriodicSize);

    if (Dwc2ReadRegister(Extension, DWC2_GRXFSIZ) != DWC2_HOST_RX_FIFO_SIZE ||
        Dwc2ReadRegister(Extension, DWC2_GNPTXFSIZ) != NonPeriodicSize ||
        Dwc2ReadRegister(Extension, DWC2_HPTXFSIZ) != PeriodicSize)
    {
        DPRINT1("[DWC2] failed to program dynamic FIFO partition RX=%08lx NP=%08lx P=%08lx\n",
                Dwc2ReadRegister(Extension, DWC2_GRXFSIZ),
                Dwc2ReadRegister(Extension, DWC2_GNPTXFSIZ),
                Dwc2ReadRegister(Extension, DWC2_HPTXFSIZ));
        Dwc2DumpControllerState(Extension, "dynamic FIFO readback failure");
        return FALSE;
    }

    DPRINT1("[DWC2] dynamic FIFO depth=%lu configured RX=%lu NP=%lu P=%lu\n",
            FifoDepth,
            DWC2_HOST_RX_FIFO_SIZE,
            DWC2_HOST_NPERIODIC_TX_FIFO_SIZE,
            DWC2_HOST_PERIODIC_TX_FIFO_SIZE);
    return TRUE;
}

static BOOLEAN
Dwc2InitializeHardware(
    _In_ PDWC2_EXTENSION Extension)
{
    ULONG AhbConfig;
    ULONG HardwareConfig;
    ULONG HostConfig;
    ULONG Index;
    ULONG UsbConfig;

    Dwc2SetGlobalInterruptEnable(Extension, FALSE);
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, 0);
    Dwc2WriteRegister(Extension, DWC2_PCGCCTL, 0);

    /*
     * Reset first, then force host mode and give the core the full 25 ms to
     * settle: host-mode registers written while the mode is still switching
     * (HAINTMSK in particular) are silently lost on this core.
     */
    if (!Dwc2CoreReset(Extension))
    {
        DPRINT1("[DWC2] initialization failed during core reset\n");
        return FALSE;
    }

    Dwc2WriteRegister(Extension, DWC2_PCGCCTL, 0);
    UsbConfig = Dwc2ReadRegister(Extension, DWC2_GUSBCFG);
    UsbConfig |= DWC2_GUSBCFG_FORCEHOSTMODE;
    Dwc2WriteRegister(Extension, DWC2_GUSBCFG, UsbConfig);
    Dwc2RegPacket.UsbPortWait(Extension, 25);

    if (!(Dwc2ReadRegister(Extension, DWC2_GINTSTS) & DWC2_GINT_CURMODE_HOST))
    {
        DPRINT1("[DWC2] controller did not enter host mode (GINTSTS=%08lx)\n", Dwc2ReadRegister(Extension, DWC2_GINTSTS));
        Dwc2DumpControllerState(Extension, "host mode timeout");
        return FALSE;
    }

    HardwareConfig = Dwc2ReadRegister(Extension, DWC2_GHWCFG2);
    Extension->NumberOfChannels = ((HardwareConfig & DWC2_GHWCFG2_NUM_HOST_CHAN_MASK) >> DWC2_GHWCFG2_NUM_HOST_CHAN_SHIFT) + 1;
    if (Extension->NumberOfChannels == 0 || Extension->NumberOfChannels > DWC2_MAX_CHANNELS)
        Extension->NumberOfChannels = DWC2_MAX_CHANNELS;

    HostConfig = Dwc2ReadRegister(Extension, DWC2_HCFG);
    HostConfig &= ~DWC2_HCFG_FSLSPCLKSEL_MASK;
    HostConfig |= DWC2_HCFG_FSLSPCLKSEL_30_60_MHZ;
    Dwc2WriteRegister(Extension, DWC2_HCFG, HostConfig);

    if (!Dwc2ConfigureFifos(Extension, HardwareConfig))
    {
        DPRINT1("[DWC2] initialization failed while configuring FIFOs\n");
        return FALSE;
    }

    AhbConfig = Dwc2ReadRegister(Extension, DWC2_GAHBCFG);
    AhbConfig &= ~(DWC2_GAHBCFG_GLBL_INTR_EN | DWC2_GAHBCFG_AXI_BURST4_MASK);
    AhbConfig |= DWC2_GAHBCFG_DMA_EN | DWC2_GAHBCFG_WAIT_AXI_WRITES;
    Dwc2WriteRegister(Extension, DWC2_GAHBCFG, AhbConfig);

    if (!Dwc2FlushFifos(Extension))
    {
        DPRINT1("[DWC2] initialization failed while flushing FIFOs\n");
        return FALSE;
    }

    for (Index = 0; Index < Extension->NumberOfChannels; Index++)
    {
        if (!Dwc2HaltChannel(Extension, (UCHAR)Index))
        {
            DPRINT1("[DWC2] initialization failed while halting CH%lu\n", Index);
            return FALSE;
        }
        Extension->Channels[Index] = NULL;
    }

    Dwc2WriteRegister(Extension, DWC2_HAINTMSK, (1UL << Extension->NumberOfChannels) - 1);
    Dwc2WriteRegister(Extension, DWC2_GINTSTS, 0xFFFFFFFF);
    Extension->InterruptMask = DWC2_GINT_BASE_MASK;
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
    Dwc2WritePort(Extension, DWC2_HPRT_PWR, 0, 0);
    Dwc2RegPacket.UsbPortWait(Extension, 20);

    Dwc2DumpControllerState(Extension, "initialization complete");

    DPRINT1("[DWC2] initialized GSNPSID=%08lx GHWCFG2=%08lx channels=%lu GAHBCFG=%08lx PCGCCTL=%08lx RXFIFO=%08lx NPTXFIFO=%08lx PTXFIFO=%08lx HPRT0=%08lx\n",
            Dwc2ReadRegister(Extension, DWC2_GSNPSID),
            HardwareConfig,
            Extension->NumberOfChannels,
            Dwc2ReadRegister(Extension, DWC2_GAHBCFG),
            Dwc2ReadRegister(Extension, DWC2_PCGCCTL),
            Dwc2ReadRegister(Extension, DWC2_GRXFSIZ),
            Dwc2ReadRegister(Extension, DWC2_GNPTXFSIZ),
            Dwc2ReadRegister(Extension, DWC2_HPTXFSIZ),
            Dwc2ReadRegister(Extension, DWC2_HPRT0));
    return TRUE;
}

static UCHAR
Dwc2AllocateChannel(
    _In_ PDWC2_EXTENSION Extension,
    _In_ PDWC2_ENDPOINT Endpoint)
{
    ULONG Index;

    for (Index = 0; Index < Extension->NumberOfChannels; Index++)
    {
        if (!Extension->Channels[Index])
        {
            Extension->Channels[Index] = Endpoint;
            Endpoint->Channel = (UCHAR)Index;
            return (UCHAR)Index;
        }
    }

    return DWC2_INVALID_CHANNEL;
}

static VOID
Dwc2ReleaseChannel(
    _In_ PDWC2_EXTENSION Extension,
    _In_ PDWC2_TRANSFER Transfer)
{
    UCHAR Channel = Transfer->Channel;

    if (Channel < Extension->NumberOfChannels)
    {
        /*
         * The core reports transaction status before the channel finishes
         * halting.  Handing the slot to another endpoint while HCCHAR.CHENA
         * is still set reprograms a live channel: the late CHHLTD is then
         * attributed to the new owner and the non-periodic FIFO accounting
         * corrupts (observed as impossible GNPTXSTS free-space values),
         * after which armed channels never interrupt again.
         */
        if (Dwc2ReadRegister(Extension, DWC2_HCCHAR(Channel)) & DWC2_HCCHAR_CHENA)
        {
            if (!Dwc2HaltChannel(Extension, Channel))
            {
                DPRINT1("[DWC2] release could not halt CH%u; requesting controller reset endpoint=%p transfer=%p\n", Channel, Transfer->Endpoint, Transfer);
                Dwc2DumpChannelState(Extension, Channel, "release halt failure");
                if (Dwc2RegPacket.UsbPortInvalidateController)
                    Dwc2RegPacket.UsbPortInvalidateController(Extension, USBPORT_INVALIDATE_CONTROLLER_RESET);
            }
        }
        Dwc2WriteRegister(Extension, DWC2_HCINTMSK(Channel), 0);
        Dwc2WriteRegister(Extension, DWC2_HCINT(Channel), DWC2_HCINT_VALID_MASK);
        Extension->Channels[Channel] = NULL;
    }

    if (Transfer->Endpoint)
        Transfer->Endpoint->Channel = DWC2_INVALID_CHANNEL;

    Transfer->PendingChannelStatus = 0;
    Transfer->Channel = DWC2_INVALID_CHANNEL;
}

static ULONGLONG
Dwc2GetInterruptRetryDelay(
    _In_ PDWC2_ENDPOINT Endpoint)
{
    ULONG Interval = Endpoint->Properties.Reserved4;

    /*
     * USBPORT's Period is a legacy frame-scheduler bucket and is capped at
     * 32.  A high-speed interrupt endpoint's raw bInterval is instead an
     * exponent in 125-us microframes.  Keep the descriptor value so, for
     * example, the LAN9514 hub's bInterval 12 becomes 2048 microframes
     * (256 ms), not 32 microframes (4 ms).
     */
    if (Endpoint->Properties.DeviceSpeed == UsbHighSpeed)
    {
        if (!Interval)
            Interval = 1;
        if (Interval > 16)
            Interval = 16;

        return (1ULL << (Interval - 1)) * DWC2_MICROFRAME_100NS;
    }

    if (!Interval)
        Interval = Endpoint->Properties.Period ? Endpoint->Properties.Period : 1;

    return (ULONGLONG)Interval * DWC2_FRAME_100NS;
}

static VOID
NTAPI
Dwc2RetryTimerDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PDWC2_EXTENSION Extension = DeferredContext;
    KIRQL OldIrql;
    ULONG InvalidateStatus = 0;
    ULONG WakeCount = 0;
    BOOLEAN NotifyUsbPort = FALSE;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    if (!Extension)
        return;

    KeAcquireSpinLock(&Extension->RetryTimerLock, &OldIrql);
    Extension->RetryTimerDpcActive++;
    Extension->RetryTimerScheduled = 0;
    Extension->RetryTimerDueTime = 0;
    if (!Extension->Stopping && Extension->Started && !Extension->Suspended)
    {
        NotifyUsbPort = TRUE;
        WakeCount = (ULONG)InterlockedIncrement(&Extension->RetryTimerWakeCount);
    }
    KeReleaseSpinLock(&Extension->RetryTimerLock, OldIrql);

    if (NotifyUsbPort)
    {
        if (Dwc2RegPacket.UsbPortInvalidateController)
        {
            InvalidateStatus = Dwc2RegPacket.UsbPortInvalidateController(Extension, USBPORT_INVALIDATE_CONTROLLER_SOFT_INTERRUPT);
            if (InvalidateStatus)
                DPRINT1("[DWC2] retry timer soft interrupt request failed status=%08lx wake=%lu\n", InvalidateStatus, WakeCount);
        }
        else
        {
            DPRINT1("[DWC2] retry timer cannot wake USBPORT: callback is NULL wake=%lu\n", WakeCount);
        }

        if (Dwc2ShouldLogCount(WakeCount))
            DPRINT1("[DWC2] retry timer wake count=%lu status=%08lx\n", WakeCount, InvalidateStatus);
    }

    KeAcquireSpinLock(&Extension->RetryTimerLock, &OldIrql);
    Extension->RetryTimerDpcActive--;
    if (!Extension->RetryTimerScheduled && !Extension->RetryTimerDpcActive)
        KeSetEvent(&Extension->RetryTimerDpcEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Extension->RetryTimerLock, OldIrql);
}

static VOID
Dwc2ArmRetryTimer(
    _In_ PDWC2_EXTENSION Extension)
{
    LARGE_INTEGER DueTime;
    PLIST_ENTRY Entry;
    ULONGLONG CurrentTime;
    ULONGLONG EarliestTime = 0;
    ULONGLONG Delay;
    ULONG ArmCount = 0;
    ULONG WaitingCount = 0;
    KIRQL OldIrql;
    BOOLEAN Cancelled;

    /*
     * Do not count long retry intervals with GINTSTS.SOF.  At high speed that
     * creates 8000 ISR/DPC cycles per second and can starve the PnP worker.
     * The timer DPC only asks USBPORT for a serialized soft-interrupt pass;
     * endpoint and channel state remains protected by USBPORT's miniport lock.
     */
    if (!Extension->Started || Extension->Suspended || Extension->Stopping)
        return;

    for (Entry = Extension->EndpointList.Flink; Entry != &Extension->EndpointList; Entry = Entry->Flink)
    {
        PDWC2_ENDPOINT Endpoint = CONTAINING_RECORD(Entry, DWC2_ENDPOINT, Link);
        PDWC2_TRANSFER Transfer = Endpoint->Transfer;

        if (!Transfer || Transfer->Done || !Transfer->NeedsRetry)
            continue;

        WaitingCount++;
        if (!EarliestTime || Transfer->RetryTime < EarliestTime)
            EarliestTime = Transfer->RetryTime;
    }

    KeAcquireSpinLock(&Extension->RetryTimerLock, &OldIrql);
    if (Extension->Stopping)
    {
        KeReleaseSpinLock(&Extension->RetryTimerLock, OldIrql);
        return;
    }

    if (!EarliestTime)
    {
        if (Extension->RetryTimerScheduled)
        {
            Cancelled = KeCancelTimer(&Extension->RetryTimer);
            if (!Cancelled)
                Cancelled = KeRemoveQueueDpc(&Extension->RetryTimerDpc);
            if (Cancelled)
            {
                Extension->RetryTimerScheduled = 0;
                Extension->RetryTimerDueTime = 0;
                if (!Extension->RetryTimerDpcActive)
                    KeSetEvent(&Extension->RetryTimerDpcEvent, IO_NO_INCREMENT, FALSE);
            }
        }
        KeReleaseSpinLock(&Extension->RetryTimerLock, OldIrql);
        return;
    }

    if (Extension->RetryTimerScheduled && Extension->RetryTimerDueTime <= EarliestTime)
    {
        KeReleaseSpinLock(&Extension->RetryTimerLock, OldIrql);
        return;
    }

    CurrentTime = KeQueryInterruptTime();
    Delay = EarliestTime > CurrentTime ? EarliestTime - CurrentTime : 1;
    DueTime.QuadPart = -(LONGLONG)Delay;
    KeClearEvent(&Extension->RetryTimerDpcEvent);
    Extension->RetryTimerScheduled = 1;
    Extension->RetryTimerDueTime = EarliestTime;
    KeSetTimer(&Extension->RetryTimer, DueTime, &Extension->RetryTimerDpc);
    ArmCount = (ULONG)InterlockedIncrement(&Extension->RetryTimerArmCount);
    KeReleaseSpinLock(&Extension->RetryTimerLock, OldIrql);

    if (Dwc2ShouldLogCount(ArmCount))
        DPRINT1("[DWC2] retry timer armed count=%lu waiting=%lu delay100ns=%I64u due=%I64u now=%I64u\n", ArmCount, WaitingCount, Delay, EarliestTime, CurrentTime);
}

static VOID
Dwc2StopRetryTimer(
    _In_ PDWC2_EXTENSION Extension)
{
    KIRQL OldIrql;
    BOOLEAN Cancelled;

    KeAcquireSpinLock(&Extension->RetryTimerLock, &OldIrql);
    Extension->Stopping = 1;
    Cancelled = KeCancelTimer(&Extension->RetryTimer);
    if (!Cancelled)
        Cancelled = KeRemoveQueueDpc(&Extension->RetryTimerDpc);
    if (Cancelled)
    {
        Extension->RetryTimerScheduled = 0;
        Extension->RetryTimerDueTime = 0;
        if (!Extension->RetryTimerDpcActive)
            KeSetEvent(&Extension->RetryTimerDpcEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Extension->RetryTimerLock, OldIrql);

    KeWaitForSingleObject(&Extension->RetryTimerDpcEvent, Executive, KernelMode, FALSE, NULL);
}

static BOOLEAN
Dwc2HaltChannel(
    _In_ PDWC2_EXTENSION Extension,
    _In_ UCHAR Channel)
{
    ULONG Character;
    ULONG Index;
    BOOLEAN Halted = TRUE;

    if (Channel >= Extension->NumberOfChannels)
    {
        Dwc2DumpChannelState(Extension, Channel, "halt requested for invalid channel");
        return FALSE;
    }

    Character = Dwc2ReadRegister(Extension, DWC2_HCCHAR(Channel));
    if (Character & DWC2_HCCHAR_CHENA)
    {
        Dwc2WriteRegister(Extension, DWC2_HCCHAR(Channel), Character | DWC2_HCCHAR_CHDIS | DWC2_HCCHAR_CHENA);

        for (Index = 0; Index < 10000; Index++)
        {
            if (!(Dwc2ReadRegister(Extension, DWC2_HCCHAR(Channel)) & DWC2_HCCHAR_CHENA))
                break;
            KeStallExecutionProcessor(10);
        }

        if (Index == 10000)
        {
            DPRINT1("[DWC2] CH%u halt timed out HCCHAR=%08lx HCINT=%08lx\n", Channel, Dwc2ReadRegister(Extension, DWC2_HCCHAR(Channel)), Dwc2ReadRegister(Extension, DWC2_HCINT(Channel)));
            Dwc2DumpChannelState(Extension, Channel, "channel halt timeout");
            Halted = FALSE;
        }
    }

    Dwc2WriteRegister(Extension, DWC2_HCINTMSK(Channel), 0);
    Dwc2WriteRegister(Extension, DWC2_HCINT(Channel), DWC2_HCINT_VALID_MASK);
    return Halted;
}

static VOID
Dwc2UpdateSofMask(
    _In_ PDWC2_EXTENSION Extension)
{
    ULONG MaskReadback;
    /* USBPORT requests the next SOF for endpoint-state handoff; it is one-shot. */
    BOOLEAN EnableSof = Extension->SofRequested;

    if (EnableSof)
        Extension->InterruptMask |= DWC2_GINT_SOF;
    else
        Extension->InterruptMask &= ~DWC2_GINT_SOF;

    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
    MaskReadback = Dwc2ReadRegister(Extension, DWC2_GINTMSK);
    if (MaskReadback != Extension->InterruptMask)
    {
        DPRINT1("[DWC2] interrupt mask readback mismatch expected=%08lx actual=%08lx SOF=%u\n", Extension->InterruptMask, MaskReadback, EnableSof);
        Dwc2DumpControllerState(Extension, "interrupt mask readback failure");
    }
}

static VOID
Dwc2RequestOneShotSof(
    _In_ PDWC2_EXTENSION Extension,
    _In_ PCSTR Reason)
{
    ULONG RequestCount;

    Extension->SofRequested = TRUE;
    RequestCount = (ULONG)InterlockedIncrement(&Extension->SofRequestCount);
    Dwc2UpdateSofMask(Extension);
    if (Dwc2ShouldLogCount(RequestCount))
        DPRINT1("[DWC2] one-shot SOF requested count=%lu reason=%s frame=%08lx mask=%08lx\n", RequestCount, Reason, Dwc2ReadRegister(Extension, DWC2_HFNUM), Extension->InterruptMask);
}

static BOOLEAN
Dwc2ProgramStage(
    _In_ PDWC2_EXTENSION Extension,
    _In_ PDWC2_TRANSFER Transfer)
{
    PDWC2_ENDPOINT Endpoint;
    PUSBPORT_ENDPOINT_PROPERTIES Properties;
    ULONG Character;
    ULONG DirectionIn;
    ULONG DmaAddress;
    ULONG EndpointType;
    ULONG ImmediateCount;
    ULONG InvalidateStatus;
    ULONG MaximumStageLength;
    ULONG PacketCount;
    ULONG PacketSize;
    ULONG Pid;
    ULONG ImmediateInterrupts;
    ULONG ProgrammedCharacter;
    ULONG Split = 0;
    ULONG TransferDescriptor;
    ULONG TransferSize;
    UCHAR Channel;

    if (!Transfer || !Transfer->Endpoint || !Transfer->Parameters)
    {
        DPRINT1("[DWC2] cannot program incomplete transfer transfer=%p endpoint=%p parameters=%p\n",
                Transfer,
                Transfer ? Transfer->Endpoint : NULL,
                Transfer ? Transfer->Parameters : NULL);
        Dwc2DumpTransferState(Extension, Transfer, "incomplete transfer programming request");
        return FALSE;
    }

    Endpoint = Transfer->Endpoint;
    Properties = &Endpoint->Properties;
    Channel = Transfer->Channel;

    if (Transfer->Stage > Dwc2StageStatus || Transfer->BytesTransferred > Transfer->Parameters->TransferBufferLength)
    {
        DPRINT1("[DWC2] invalid transfer progress stage=%u bytes=%lu length=%lu\n", Transfer->Stage, Transfer->BytesTransferred, Transfer->Parameters->TransferBufferLength);
        Dwc2DumpTransferState(Extension, Transfer, "invalid transfer progress");
        return FALSE;
    }

    if (Channel >= Extension->NumberOfChannels)
    {
        Dwc2DumpTransferState(Extension, Transfer, "program requested for invalid channel");
        return FALSE;
    }

    PacketSize = Properties->MaxPacketSize;
    if (PacketSize == 0 || PacketSize > DWC2_HCCHAR_MPS_MASK)
    {
        DPRINT1("[DWC2] invalid packet size=%lu addr=%u ep=%u type=%lu\n", PacketSize, Properties->DeviceAddress, Properties->EndpointAddress, Properties->TransferType);
        Dwc2DumpTransferState(Extension, Transfer, "invalid packet size");
        return FALSE;
    }

    EndpointType = Dwc2EndpointType(Properties->TransferType);
    DirectionIn = Transfer->DirectionIn;

    if (Transfer->Stage == Dwc2StageSetup)
    {
        RtlCopyMemory(Endpoint->BufferVA, &Transfer->Parameters->SetupPacket, sizeof(Transfer->Parameters->SetupPacket));
        DmaAddress = Endpoint->BufferPA;
        TransferSize = sizeof(Transfer->Parameters->SetupPacket);
        PacketCount = 1;
        Pid = DWC2_HCTSIZ_PID_SETUP;
        DirectionIn = FALSE;
    }
    else if (Transfer->Stage == Dwc2StageStatus)
    {
        DmaAddress = Endpoint->BufferPA;
        TransferSize = 0;
        PacketCount = 1;
        Pid = DWC2_HCTSIZ_PID_DATA1;
        DirectionIn = !Transfer->DirectionIn;
    }
    else
    {
        ULONG Remaining = Transfer->Parameters->TransferBufferLength - Transfer->BytesTransferred;

        MaximumStageLength = PacketSize * 0x3FF;
        if (MaximumStageLength > DWC2_HCTSIZ_XFERSIZE_MASK)
            MaximumStageLength = DWC2_HCTSIZ_XFERSIZE_MASK;

        TransferSize = Remaining;
        if (TransferSize > MaximumStageLength)
            TransferSize = MaximumStageLength;

        DmaAddress = Endpoint->BufferPA + DWC2_SETUP_BUFFER_SIZE + Transfer->BytesTransferred;
        PacketCount = TransferSize ? (TransferSize + PacketSize - 1) / PacketSize : 1;
        Pid = Transfer->DataToggle ? DWC2_HCTSIZ_PID_DATA1 : DWC2_HCTSIZ_PID_DATA0;
    }

    Transfer->ProgrammedLength = TransferSize;
    Transfer->ProgrammedDmaAddress = DmaAddress;
    Transfer->InitialPacketCount = PacketCount;

    Character = ((ULONG)Properties->DeviceAddress & 0x7F) << DWC2_HCCHAR_DEVADDR_SHIFT;
    Character |= ((ULONG)Properties->EndpointAddress & 0x0F) << DWC2_HCCHAR_EPNUM_SHIFT;
    Character |= EndpointType << DWC2_HCCHAR_EPTYPE_SHIFT;
    Character |= PacketSize & DWC2_HCCHAR_MPS_MASK;
    Character |= DWC2_HCCHAR_MULTICNT_ONE;

    if (DirectionIn)
        Character |= DWC2_HCCHAR_EPDIR;

    if (Properties->DeviceSpeed == UsbLowSpeed)
        Character |= DWC2_HCCHAR_LSPDDEV;

    if (Properties->TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
    {
        if (!(Dwc2ReadRegister(Extension, DWC2_HFNUM) & 1))
            Character |= DWC2_HCCHAR_ODDFRM;
    }

    if (Endpoint->RequiresSplit)
    {
        Split = DWC2_HCSPLT_SPLTENA | DWC2_HCSPLT_XACTPOS_ALL;
        Split |= ((ULONG)Properties->TtHubAddr & 0x7F) << DWC2_HCSPLT_HUBADDR_SHIFT;
        Split |= (ULONG)Properties->TtPortNumber & 0x7F;
        if (Transfer->CompleteSplit)
            Split |= DWC2_HCSPLT_COMPSPLT;
    }

    Dwc2WriteRegister(Extension, DWC2_HCINTMSK(Channel), 0);
    Dwc2WriteRegister(Extension, DWC2_HCINT(Channel), DWC2_HCINT_VALID_MASK);
    Dwc2WriteRegister(Extension, DWC2_HCSPLT(Channel), Split);
    TransferDescriptor = (Pid << DWC2_HCTSIZ_PID_SHIFT) | (PacketCount << DWC2_HCTSIZ_PKTCNT_SHIFT) | TransferSize;
    Dwc2WriteRegister(Extension, DWC2_HCTSIZ(Channel), TransferDescriptor);
    Dwc2WriteRegister(Extension, DWC2_HCDMA(Channel), DmaAddress);
    Dwc2WriteRegister(Extension, DWC2_HCINTMSK(Channel), DWC2_HCINT_DRIVER_MASK);
    if (Dwc2ReadRegister(Extension, DWC2_HCSPLT(Channel)) != Split ||
        Dwc2ReadRegister(Extension, DWC2_HCTSIZ(Channel)) != TransferDescriptor ||
        Dwc2ReadRegister(Extension, DWC2_HCDMA(Channel)) != DmaAddress ||
        Dwc2ReadRegister(Extension, DWC2_HCINTMSK(Channel)) != DWC2_HCINT_DRIVER_MASK)
    {
        DPRINT1("[DWC2] CH%u programming readback mismatch expected split=%08lx size=%08lx dma=%08lx mask=%08lx\n",
                Channel,
                Split,
                TransferDescriptor,
                DmaAddress,
                DWC2_HCINT_DRIVER_MASK);
        Dwc2DumpTransferState(Extension, Transfer, "channel programming readback failure");
        return FALSE;
    }

    Transfer->StageStartTime = KeQueryInterruptTime();
    Transfer->StageStartFrame = Dwc2ReadRegister(Extension, DWC2_HFNUM);
    Transfer->HangDiagnosticCount = 0;
    Transfer->PendingChannelStatus = 0;
    KeMemoryBarrier();
    Dwc2WriteRegister(Extension, DWC2_HCCHAR(Channel), Character | DWC2_HCCHAR_CHENA);
    ProgrammedCharacter = Dwc2ReadRegister(Extension, DWC2_HCCHAR(Channel));
    ImmediateInterrupts = Dwc2ReadRegister(Extension, DWC2_HCINT(Channel)) & DWC2_HCINT_DRIVER_MASK;

    /*
     * A short transaction can finish before the platform interrupt reaches
     * USBPORT. Sample the channel before doing any serial output: on SMP the
     * interrupt DPC can already be advancing this transfer on another CPU.
     * Do not modify transfer state after CHENA is written. A coalesced soft
     * interrupt is sufficient to make USBPORT drain any terminal status that
     * is still pending; if the DPC already consumed it, the request is benign.
     */
    if (ImmediateInterrupts && !(ProgrammedCharacter & DWC2_HCCHAR_CHENA))
    {
        ImmediateCount = (ULONG)InterlockedIncrement(&Extension->ImmediateInterruptCount);
        if (Dwc2ShouldLogCount(ImmediateCount))
        {
            DPRINT1("[DWC2] CH%u terminal status observed immediately after arm HCINT=%08lx count=%lu; queuing soft DPC\n",
                    Channel,
                    ImmediateInterrupts,
                    ImmediateCount);
        }
        if (Dwc2RegPacket.UsbPortInvalidateController)
        {
            InvalidateStatus = Dwc2RegPacket.UsbPortInvalidateController(Extension, USBPORT_INVALIDATE_CONTROLLER_SOFT_INTERRUPT);
            if (InvalidateStatus)
            {
                DPRINT1("[DWC2] CH%u soft interrupt request failed status=%08lx HCINT=%08lx\n", Channel, InvalidateStatus, ImmediateInterrupts);
                Dwc2DumpChannelState(Extension, Channel, "soft interrupt request failure");
            }
        }
        else
        {
            DPRINT1("[DWC2] CH%u cannot request soft interrupt: USBPORT callback is NULL HCINT=%08lx\n", Channel, ImmediateInterrupts);
            Dwc2DumpChannelState(Extension, Channel, "missing soft interrupt callback");
        }
    }

    return TRUE;
}

static VOID
Dwc2FinishTransfer(
    _In_ PDWC2_EXTENSION Extension,
    _In_ PDWC2_TRANSFER Transfer,
    _In_ USBD_STATUS Status)
{
    PDWC2_ENDPOINT Endpoint = Transfer->Endpoint;

    Transfer->UsbdStatus = Status;
    Transfer->Done = TRUE;
    Transfer->Stage = Dwc2StageComplete;
    Transfer->NeedsRetry = FALSE;
    Transfer->CompletionTime = KeQueryInterruptTime();
    InterlockedIncrement(&Extension->CompletionCount);
    if (Status != USBD_STATUS_SUCCESS)
        InterlockedIncrement(&Extension->TransferErrorCount);
    Dwc2ReleaseChannel(Extension, Transfer);
    Dwc2UpdateSofMask(Extension);
    Dwc2ArmRetryTimer(Extension);

    if (Status != USBD_STATUS_SUCCESS)
    {
        DPRINT1("[DWC2] transfer failed ep=%p addr=%u endpoint=%u status=%08lx bytes=%lu\n",
                Endpoint,
                Endpoint->Properties.DeviceAddress,
                Endpoint->Properties.EndpointAddress,
                Status,
                Transfer->BytesTransferred);
    }
    Dwc2RegPacket.UsbPortInvalidateEndpoint(Extension, Endpoint);
}

static USBD_STATUS
Dwc2DecodeChannelError(
    _In_ ULONG InterruptStatus)
{
    if (InterruptStatus & DWC2_HCINT_STALL)
        return USBD_STATUS_STALL_PID;
    if (InterruptStatus & DWC2_HCINT_BBLERR)
        return USBD_STATUS_BABBLE_DETECTED;
    if (InterruptStatus & DWC2_HCINT_AHBERR)
        return USBD_STATUS_DATA_BUFFER_ERROR;
    if (InterruptStatus & (DWC2_HCINT_XACTERR | DWC2_HCINT_DATATGLERR | DWC2_HCINT_FRMOVRUN))
        return USBD_STATUS_XACT_ERROR;
    return USBD_STATUS_INTERNAL_HC_ERROR;
}

static VOID
Dwc2ScheduleRetry(
    _In_ PDWC2_EXTENSION Extension,
    _In_ PDWC2_TRANSFER Transfer,
    _In_ ULONGLONG Delay100ns)
{
    if (!Delay100ns)
        Delay100ns = 1;

    Transfer->RetryTime = KeQueryInterruptTime() + Delay100ns;
    Dwc2ReleaseChannel(Extension, Transfer);
    Transfer->NeedsRetry = TRUE;
    Transfer->RetryDiagnosticLogged = FALSE;
    if (Delay100ns <= DWC2_MICROFRAME_100NS)
        Dwc2RequestOneShotSof(Extension, "short transfer retry");
    Dwc2ArmRetryTimer(Extension);
}

static VOID
Dwc2ProcessChannel(
    _In_ PDWC2_EXTENSION Extension,
    _In_ UCHAR Channel)
{
    PDWC2_ENDPOINT Endpoint;
    PDWC2_TRANSFER Transfer;
    ULONG ActualLength;
    ULONG Character;
    ULONG CurrentDmaAddress;
    ULONG DeferredHalts;
    ULONG DmaProgress;
    ULONG InterruptStatus;
    ULONG LateStatus;
    ULONG PacketProgress;
    ULONG RemainingPackets;
    ULONG RemainingSize;
    ULONG RereadIndex;
    ULONG RetryDelay;
    ULONG TransferDescriptor;
    ULONG TransferredPackets;
    BOOLEAN ShortPacket;
    BOOLEAN StageOut;

    InterruptStatus = Dwc2ReadRegister(Extension, DWC2_HCINT(Channel)) & DWC2_HCINT_VALID_MASK;
    if (!InterruptStatus)
    {
        DPRINT1("[DWC2] CH%u selected for DPC with no HCINT pending=%08lx HAINT=%08lx GINTSTS=%08lx\n",
                Channel,
                (ULONG)InterlockedCompareExchange(&Extension->PendingChannelInterrupts, 0, 0),
                Dwc2ReadRegister(Extension, DWC2_HAINT),
                Dwc2ReadRegister(Extension, DWC2_GINTSTS));
        return;
    }

    TransferDescriptor = Dwc2ReadRegister(Extension, DWC2_HCTSIZ(Channel));
    CurrentDmaAddress = Dwc2ReadRegister(Extension, DWC2_HCDMA(Channel));
    Dwc2WriteRegister(Extension, DWC2_HCINT(Channel), InterruptStatus);
    Endpoint = Extension->Channels[Channel];

    if (!Endpoint || !Endpoint->Transfer)
    {
        DPRINT1("[DWC2] stray channel interrupt CH%u status=%08lx\n", Channel, InterruptStatus);
        Dwc2DumpChannelState(Extension, Channel, "stray channel interrupt");
        return;
    }

    Transfer = Endpoint->Transfer;
    if (Transfer->Channel != Channel || Transfer->Done)
    {
        DPRINT1("[DWC2] stale channel mapping CH%u status=%08lx transferChannel=%u done=%u endpointChannel=%u\n",
                Channel,
                InterruptStatus,
                Transfer->Channel,
                Transfer->Done,
                Endpoint->Channel);
        Dwc2DumpTransferState(Extension, Transfer, "stale channel mapping");
        return;
    }

    /*
     * The core posts transaction status (STALL/XACTERR/XFERCOMPL/...) before
     * the channel finishes halting.  Deciding a transfer's fate on a pre-halt
     * snapshot both misreads split status updates and frees the channel while
     * the hardware still owns it.  Accumulate pre-halt bits and decode only
     * once CHHLTD is observed.
     */
    if (!(InterruptStatus & DWC2_HCINT_CHHLTD))
    {
        Transfer->PendingChannelStatus |= InterruptStatus;
        if (InterruptStatus & DWC2_HCINT_ERROR_MASK)
        {
            Character = Dwc2ReadRegister(Extension, DWC2_HCCHAR(Channel));
            if ((Character & DWC2_HCCHAR_CHENA) && !(Character & DWC2_HCCHAR_CHDIS))
                Dwc2WriteRegister(Extension, DWC2_HCCHAR(Channel), Character | DWC2_HCCHAR_CHDIS | DWC2_HCCHAR_CHENA);
            DeferredHalts = (ULONG)InterlockedIncrement(&Extension->DeferredHaltCount);
            if (Dwc2ShouldLogCount(DeferredHalts))
                DPRINT1("[DWC2] CH%u status before halt HCINT=%08lx count=%lu; awaiting CHHLTD\n", Channel, InterruptStatus, DeferredHalts);
        }
        return;
    }

    InterruptStatus |= Transfer->PendingChannelStatus;
    Transfer->PendingChannelStatus = 0;

    if (!(InterruptStatus & DWC2_HCINT_REASON_MASK))
    {
        for (RereadIndex = 0; RereadIndex < 10; RereadIndex++)
        {
            LateStatus = Dwc2ReadRegister(Extension, DWC2_HCINT(Channel)) & DWC2_HCINT_VALID_MASK;
            if (LateStatus & DWC2_HCINT_REASON_MASK)
            {
                Dwc2WriteRegister(Extension, DWC2_HCINT(Channel), LateStatus);
                InterruptStatus |= LateStatus;
                break;
            }
            KeStallExecutionProcessor(1);
        }
    }

    if ((InterruptStatus & DWC2_HCINT_ACK) && Endpoint->RequiresSplit && !Transfer->CompleteSplit && !(InterruptStatus & DWC2_HCINT_XFERCOMPL))
    {
        Transfer->CompleteSplit = TRUE;
        Transfer->CsplitNyetCount = 0;
        /*
         * Issue the complete-split from this DPC, keeping the channel: the
         * result only stays collectable inside the start-split's frame, and
         * a timer hop wakes a tick later in a frame where the TT has
         * already flushed the transaction.
         */
        if (Dwc2ProgramStage(Extension, Transfer))
            return;
        Dwc2ScheduleRetry(Extension, Transfer, DWC2_MICROFRAME_100NS);
        return;
    }

    if ((InterruptStatus & (DWC2_HCINT_NAK | DWC2_HCINT_NYET)) && !(InterruptStatus & (DWC2_HCINT_XFERCOMPL | DWC2_HCINT_ERROR_MASK)))
    {
        Transfer->NakCount++;
        RetryDelay = DWC2_MICROFRAME_100NS;
        if ((InterruptStatus & DWC2_HCINT_NAK) && Transfer->CompleteSplit)
            Transfer->CompleteSplit = FALSE;
        /*
         * A NYETed complete-split is only collectable within the frame of
         * its start-split, so retry it immediately while still owning the
         * channel; once the attempts are exhausted the TT has abandoned
         * the transaction and only a fresh start-split makes progress.
         */
        if ((InterruptStatus & DWC2_HCINT_NYET) && Transfer->CompleteSplit)
        {
            if (++Transfer->CsplitNyetCount < DWC2_CSPLIT_NYET_LIMIT && Dwc2ProgramStage(Extension, Transfer))
                return;
            Transfer->CompleteSplit = FALSE;
            Transfer->CsplitNyetCount = 0;
        }
        if (Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT && !Transfer->CompleteSplit)
            RetryDelay = (ULONG)Dwc2GetInterruptRetryDelay(Endpoint);

        Dwc2ScheduleRetry(Extension, Transfer, RetryDelay);
        return;
    }

    if (InterruptStatus & DWC2_HCINT_ERROR_MASK)
    {
        if (!(InterruptStatus & (DWC2_HCINT_STALL | DWC2_HCINT_BBLERR | DWC2_HCINT_AHBERR)) &&
            Transfer->ErrorCount + 1 < DWC2_TRANSACTION_ERROR_LIMIT)
        {
            Transfer->ErrorCount++;
            if (Transfer->Stage == Dwc2StageData)
            {
                RemainingSize = TransferDescriptor & DWC2_HCTSIZ_XFERSIZE_MASK;
                ActualLength = RemainingSize <= Transfer->ProgrammedLength ? Transfer->ProgrammedLength - RemainingSize : 0;
                DmaProgress = CurrentDmaAddress - Transfer->ProgrammedDmaAddress;
                if (DmaProgress <= Transfer->ProgrammedLength && DmaProgress > ActualLength)
                    ActualLength = DmaProgress;
                TransferredPackets = ActualLength / Endpoint->Properties.MaxPacketSize;
                ActualLength = TransferredPackets * Endpoint->Properties.MaxPacketSize;
                Transfer->BytesTransferred += ActualLength;
                if (TransferredPackets & 1)
                    Transfer->DataToggle ^= 1;
            }
            DPRINT1("[DWC2] CH%u transaction error retry HCINT=%08lx count=%lu addr=%u ep=%u stage=%u bytes=%lu/%lu\n",
                    Channel,
                    InterruptStatus,
                    Transfer->ErrorCount,
                    Endpoint->Properties.DeviceAddress,
                    Endpoint->Properties.EndpointAddress,
                    Transfer->Stage,
                    Transfer->BytesTransferred,
                    Transfer->Parameters->TransferBufferLength);
            Dwc2ScheduleRetry(Extension, Transfer, DWC2_MICROFRAME_100NS);
            return;
        }

        Endpoint->Status = USBPORT_ENDPOINT_HALT;
        DPRINT1("[DWC2] CH%u error HCINT=%08lx HCTSIZ=%08lx HCDMA=%08lx HCCHAR=%08lx errors=%lu\n",
                Channel,
                InterruptStatus,
                TransferDescriptor,
                CurrentDmaAddress,
                Dwc2ReadRegister(Extension, DWC2_HCCHAR(Channel)),
                Transfer->ErrorCount);
        Dwc2DumpTransferState(Extension, Transfer, "channel transfer error");
        Dwc2FinishTransfer(Extension, Transfer, Dwc2DecodeChannelError(InterruptStatus));
        return;
    }

    if (!(InterruptStatus & DWC2_HCINT_XFERCOMPL))
    {
        DPRINT1("[DWC2] CH%u halted without completion status=%08lx stage=%u\n", Channel, InterruptStatus, Transfer->Stage);
        Dwc2DumpTransferState(Extension, Transfer, "channel halted without completion");
        Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_DEV_NOT_RESPONDING);
        return;
    }

    Transfer->ErrorCount = 0;

    RemainingSize = TransferDescriptor & DWC2_HCTSIZ_XFERSIZE_MASK;
    RemainingPackets = (TransferDescriptor & DWC2_HCTSIZ_PKTCNT_MASK) >> DWC2_HCTSIZ_PKTCNT_SHIFT;
    if (RemainingSize > Transfer->ProgrammedLength || RemainingPackets > Transfer->InitialPacketCount)
    {
        DPRINT1("[DWC2] CH%u invalid completion accounting HCTSIZ=%08lx programmed=%lu/%lu packets=%lu/%lu stage=%u\n",
                Channel,
                TransferDescriptor,
                Transfer->ProgrammedLength,
                Transfer->Parameters->TransferBufferLength,
                RemainingPackets,
                Transfer->InitialPacketCount,
                Transfer->Stage);
        Dwc2DumpTransferState(Extension, Transfer, "invalid completion accounting");
        Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
        return;
    }

    ActualLength = Transfer->ProgrammedLength - RemainingSize;
    DmaProgress = CurrentDmaAddress - Transfer->ProgrammedDmaAddress;
    if (DmaProgress <= Transfer->ProgrammedLength && DmaProgress > ActualLength)
    {
        ActualLength = DmaProgress;
    }

    /*
     * OUT stages need packet accounting: the core decrements only PktCnt for
     * them, and on split transactions the HCDMA readback does not advance
     * either, so a fully-ACKed SSPLIT/CSPLIT SETUP otherwise scores 0 bytes.
     */
    StageOut = Transfer->Stage == Dwc2StageSetup ||
               (Transfer->Stage == Dwc2StageData && !Transfer->DirectionIn) ||
               (Transfer->Stage == Dwc2StageStatus && Transfer->DirectionIn);
    if (StageOut)
    {
        PacketProgress = (Transfer->InitialPacketCount - RemainingPackets) * Endpoint->Properties.MaxPacketSize;
        if (PacketProgress > Transfer->ProgrammedLength)
            PacketProgress = Transfer->ProgrammedLength;
        if (PacketProgress > ActualLength)
            ActualLength = PacketProgress;
    }

    TransferredPackets = Transfer->InitialPacketCount;
    if (RemainingPackets < TransferredPackets)
        TransferredPackets -= RemainingPackets;
    else
        TransferredPackets = ActualLength ? (ActualLength + Endpoint->Properties.MaxPacketSize - 1) / Endpoint->Properties.MaxPacketSize : 1;

    /*
     * Split transactions move one packet per channel enable and halt with
     * XFERCOMPL while packets remain, so a stage is only over when the
     * device terminated it with a short or zero-length packet. Ending on a
     * full-packet boundary means the stage continues with a new enable.
     */
    ShortPacket = Transfer->ProgrammedLength != 0 &&
                  ActualLength < Transfer->ProgrammedLength &&
                  (ActualLength == 0 ||
                   (ActualLength % Endpoint->Properties.MaxPacketSize) != 0);
    Transfer->CompleteSplit = FALSE;
    Transfer->NakCount = 0;

    if (Transfer->Stage == Dwc2StageSetup)
    {
        if (ActualLength != sizeof(Transfer->Parameters->SetupPacket))
        {
            DPRINT1("[DWC2] CH%u setup completion length mismatch actual=%lu expected=%u HCTSIZ=%08lx HCDMA=%08lx/%08lx\n", Channel, ActualLength, (ULONG)sizeof(Transfer->Parameters->SetupPacket), TransferDescriptor, Transfer->ProgrammedDmaAddress, CurrentDmaAddress);
            Dwc2DumpTransferState(Extension, Transfer, "setup completion length mismatch");
            Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
            return;
        }
        Transfer->DataToggle = 1;
        if (Transfer->Parameters->TransferBufferLength)
            Transfer->Stage = Dwc2StageData;
        else
            Transfer->Stage = Dwc2StageStatus;

        if (!Dwc2ProgramStage(Extension, Transfer))
        {
            DPRINT1("[DWC2] CH%u failed to advance setup to stage=%u\n", Channel, Transfer->Stage);
            Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
        }
        return;
    }

    if (Transfer->Stage == Dwc2StageData)
    {
        Transfer->BytesTransferred += ActualLength;
        if (TransferredPackets & 1)
            Transfer->DataToggle ^= 1;

        if (!ShortPacket && Transfer->BytesTransferred < Transfer->Parameters->TransferBufferLength)
        {
            if (!Dwc2ProgramStage(Extension, Transfer))
            {
                DPRINT1("[DWC2] CH%u failed to continue data stage bytes=%lu/%lu\n", Channel, Transfer->BytesTransferred, Transfer->Parameters->TransferBufferLength);
                Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
            }
            return;
        }

        if (Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_CONTROL)
        {
            Transfer->Stage = Dwc2StageStatus;
            if (!Dwc2ProgramStage(Extension, Transfer))
            {
                DPRINT1("[DWC2] CH%u failed to advance data to status bytes=%lu/%lu\n", Channel, Transfer->BytesTransferred, Transfer->Parameters->TransferBufferLength);
                Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
            }
            return;
        }

        Endpoint->DataToggle = Transfer->DataToggle;
        Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_SUCCESS);
        return;
    }

    if (Transfer->Stage == Dwc2StageStatus)
    {
        if (ActualLength)
        {
            DPRINT1("[DWC2] CH%u status stage unexpectedly transferred %lu byte(s) HCTSIZ=%08lx\n", Channel, ActualLength, TransferDescriptor);
            Dwc2DumpTransferState(Extension, Transfer, "nonzero status-stage completion");
        }
        Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_SUCCESS);
        return;
    }

    DPRINT1("[DWC2] CH%u completion arrived in invalid stage=%u status=%08lx\n", Channel, Transfer->Stage, InterruptStatus);
    Dwc2DumpTransferState(Extension, Transfer, "completion in invalid transfer stage");
    Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
}

static VOID
Dwc2TryStartPending(
    _In_ PDWC2_EXTENSION Extension)
{
    PLIST_ENTRY Entry;
    ULONGLONG CurrentTime;

    if (!Extension->Started)
        return;

    CurrentTime = KeQueryInterruptTime();

    for (Entry = Extension->EndpointList.Flink; Entry != &Extension->EndpointList; Entry = Entry->Flink)
    {
        PDWC2_ENDPOINT Endpoint = CONTAINING_RECORD(Entry, DWC2_ENDPOINT, Link);
        PDWC2_TRANSFER Transfer = Endpoint->Transfer;
        UCHAR Channel;

        if (Endpoint->State != USBPORT_ENDPOINT_ACTIVE || !Transfer || Transfer->Done || Transfer->Channel != DWC2_INVALID_CHANNEL)
            continue;
        if (Transfer->NeedsRetry)
        {
            if (CurrentTime < Transfer->RetryTime)
                continue;
        }

        Channel = Dwc2AllocateChannel(Extension, Endpoint);
        if (Channel == DWC2_INVALID_CHANNEL)
        {
            Transfer->ChannelWaitCount++;
            if (Dwc2ShouldLogCount(Transfer->ChannelWaitCount))
                DPRINT1("[DWC2] channel allocation deferred count=%lu endpoint=%p transfer=%p addr=%u ep=%u type=%lu state=%lu retry=%u\n", Transfer->ChannelWaitCount, Endpoint, Transfer, Endpoint->Properties.DeviceAddress, Endpoint->Properties.EndpointAddress, Endpoint->Properties.TransferType, Endpoint->State, Transfer->NeedsRetry);
            break;
        }

        Transfer->Channel = Channel;
        Transfer->NeedsRetry = FALSE;
        Transfer->RetryTime = 0;
        if (!Dwc2ProgramStage(Extension, Transfer))
            Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
    }

    Dwc2UpdateSofMask(Extension);
    Dwc2ArmRetryTimer(Extension);
}

static MPSTATUS
NTAPI
Dwc2OpenEndpoint(
    _In_ PVOID MiniportExtension,
    _In_ PUSBPORT_ENDPOINT_PROPERTIES Properties,
    _In_ PVOID MiniportEndpoint)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;
    ULONGLONG RetryDelay = 0;
    KIRQL OldIrql;

    if (Properties->TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
    {
        DPRINT1("[DWC2] isochronous endpoint rejected addr=%u ep=%u mps=%lu\n", Properties->DeviceAddress, Properties->EndpointAddress, Properties->MaxPacketSize);
        return MP_STATUS_NOT_SUPPORTED;
    }
    if (!Properties->BufferVA ||
        Properties->BufferLength < DWC2_SETUP_BUFFER_SIZE + Dwc2EndpointDataCapacity(Properties->TransferType))
    {
        DPRINT1("[DWC2] endpoint buffer invalid VA=%p PA=%08lx length=%lu required=%lu\n",
                Properties->BufferVA,
                Properties->BufferPA,
                Properties->BufferLength,
                DWC2_SETUP_BUFFER_SIZE + Dwc2EndpointDataCapacity(Properties->TransferType));
        return MP_STATUS_NO_RESOURCES;
    }

    RtlZeroMemory(Endpoint, sizeof(*Endpoint));
    Endpoint->Properties = *Properties;
    Endpoint->BufferVA = (PVOID)Properties->BufferVA;
    Endpoint->BufferPA = Properties->BufferPA;
    Endpoint->BufferLength = Properties->BufferLength;
    Endpoint->State = USBPORT_ENDPOINT_PAUSED;
    Endpoint->Status = USBPORT_ENDPOINT_RUN;
    Endpoint->Channel = DWC2_INVALID_CHANNEL;
    Endpoint->RequiresSplit = Properties->DeviceSpeed != UsbHighSpeed && Properties->TtHubAddr != (USHORT)-1;
    Endpoint->OpenTime = KeQueryInterruptTime();
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    InsertTailList(&Extension->EndpointList, &Endpoint->Link);
    Endpoint->Listed = TRUE;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    if (Properties->TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
        RetryDelay = Dwc2GetInterruptRetryDelay(Endpoint);

    DPRINT1("[DWC2] endpoint open ep=%p addr=%u endpoint=%u type=%lu speed=%u mps=%lu period=%u interval=%u retry100ns=%I64u hub=%u port=%u tt=%u/%u split=%u state=%lu buffer=%08lx/%p\n",
            Endpoint,
            Properties->DeviceAddress,
            Properties->EndpointAddress,
            Properties->TransferType,
            Properties->DeviceSpeed,
            Properties->MaxPacketSize,
            Properties->Period,
            Properties->Reserved4,
            RetryDelay,
            Properties->HubAddr,
            Properties->PortNumber,
            Properties->TtHubAddr,
            Properties->TtPortNumber,
            Endpoint->RequiresSplit,
            Endpoint->State,
            Endpoint->BufferPA,
            Endpoint->BufferVA);
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2ReopenEndpoint(
    _In_ PVOID MiniportExtension,
    _In_ PUSBPORT_ENDPOINT_PROPERTIES Properties,
    _In_ PVOID MiniportEndpoint)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;
    KIRQL OldIrql;

    if (Properties->TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
    {
        DPRINT1("[DWC2] isochronous endpoint reopen rejected addr=%u ep=%u mps=%lu\n", Properties->DeviceAddress, Properties->EndpointAddress, Properties->MaxPacketSize);
        return MP_STATUS_NOT_SUPPORTED;
    }
    if (!Properties->BufferVA ||
        Properties->BufferLength < DWC2_SETUP_BUFFER_SIZE + Dwc2EndpointDataCapacity(Properties->TransferType))
    {
        DPRINT1("[DWC2] endpoint reopen buffer invalid endpoint=%p VA=%p PA=%08lx length=%lu required=%lu\n",
                Endpoint,
                Properties->BufferVA,
                Properties->BufferPA,
                Properties->BufferLength,
                DWC2_SETUP_BUFFER_SIZE + Dwc2EndpointDataCapacity(Properties->TransferType));
        return MP_STATUS_NO_RESOURCES;
    }

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Endpoint->Properties = *Properties;
    Endpoint->BufferVA = (PVOID)Properties->BufferVA;
    Endpoint->BufferPA = Properties->BufferPA;
    Endpoint->BufferLength = Properties->BufferLength;
    Endpoint->RequiresSplit = Properties->DeviceSpeed != UsbHighSpeed && Properties->TtHubAddr != (USHORT)-1;
    Endpoint->OpenTime = KeQueryInterruptTime();
    Endpoint->IdleDiagnosticLogged = FALSE;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    DPRINT1("[DWC2] endpoint reopen ep=%p addr=%u endpoint=%u type=%lu speed=%u mps=%lu period=%u interval=%u state=%lu submits=%lu\n", Endpoint, Properties->DeviceAddress, Properties->EndpointAddress, Properties->TransferType, Properties->DeviceSpeed, Properties->MaxPacketSize, Properties->Period, Properties->Reserved4, Endpoint->State, Endpoint->SubmitCount);
    return MP_STATUS_SUCCESS;
}

static VOID
NTAPI
Dwc2QueryEndpointRequirements(
    _In_ PVOID MiniportExtension,
    _In_ PUSBPORT_ENDPOINT_PROPERTIES Properties,
    _Out_ PUSBPORT_ENDPOINT_REQUIREMENTS Requirements)
{
    UNREFERENCED_PARAMETER(MiniportExtension);
    RtlZeroMemory(Requirements, sizeof(*Requirements));

    if (Properties->TransferType != USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
    {
        Requirements->MaxTransferSize = Dwc2EndpointDataCapacity(Properties->TransferType);
        Requirements->HeaderBufferSize = DWC2_SETUP_BUFFER_SIZE + Requirements->MaxTransferSize;
    }
}

static VOID
NTAPI
Dwc2CloseEndpoint(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint,
    _In_ BOOLEAN DisablePeriodic)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DisablePeriodic);

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (Endpoint->Transfer && Endpoint->Transfer->Channel != DWC2_INVALID_CHANNEL)
    {
        if (!Dwc2HaltChannel(Extension, Endpoint->Transfer->Channel))
        {
            DPRINT1("[DWC2] endpoint close could not halt CH%u; requesting controller reset endpoint=%p transfer=%p\n", Endpoint->Transfer->Channel, Endpoint, Endpoint->Transfer);
            Dwc2DumpTransferState(Extension, Endpoint->Transfer, "endpoint close halt failure");
            Dwc2RegPacket.UsbPortInvalidateController(Extension, USBPORT_INVALIDATE_CONTROLLER_RESET);
        }
        Dwc2ReleaseChannel(Extension, Endpoint->Transfer);
    }

    Endpoint->Transfer = NULL;
    if (Endpoint->Listed)
    {
        RemoveEntryList(&Endpoint->Link);
        Endpoint->Listed = FALSE;
    }

    Endpoint->State = USBPORT_ENDPOINT_CLOSED;
    Dwc2UpdateSofMask(Extension);
    Dwc2TryStartPending(Extension);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    DPRINT1("[DWC2] endpoint close ep=%p\n", Endpoint);
}

static MPSTATUS
NTAPI
Dwc2StartController(
    _In_ PVOID MiniportExtension,
    _In_ PUSBPORT_RESOURCES Resources)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    ULONG SnpsId;

    if ((Resources->ResourcesTypes & (USBPORT_RESOURCES_MEMORY | USBPORT_RESOURCES_INTERRUPT)) != (USBPORT_RESOURCES_MEMORY | USBPORT_RESOURCES_INTERRUPT))
    {
        DPRINT1("[DWC2] missing MMIO/interrupt resources (types=%08lx)\n", Resources->ResourcesTypes);
        return MP_STATUS_FAILURE;
    }

    if (!Resources->ResourceBase || Resources->IoSpaceLength < 0x1000)
    {
        DPRINT1("[DWC2] invalid MMIO resource base=%p length=%lu types=%08lx\n", Resources->ResourceBase, Resources->IoSpaceLength, Resources->ResourcesTypes);
        return MP_STATUS_FAILURE;
    }

    Extension->Registers = Resources->ResourceBase;
    Extension->RegisterLength = Resources->IoSpaceLength;
    Extension->PendingGlobalInterrupts = 0;
    Extension->PendingChannelInterrupts = 0;
    Extension->ImmediateInterruptCount = 0;
    Extension->DmaProgressFallbackCount = 0;
    Extension->GlobalEnableRaceCount = 0;
    Extension->RetryTimerArmCount = 0;
    Extension->RetryTimerWakeCount = 0;
    Extension->RetryTimerScheduled = 0;
    Extension->RetryTimerDpcActive = 0;
    Extension->SofRequestCount = 0;
    Extension->SofInterruptCount = 0;
    Extension->CompletionCount = 0;
    Extension->TransferErrorCount = 0;
    Extension->DeferredHaltCount = 0;
    Extension->WedgeRecoveryCount = 0;
    Extension->HeartbeatCount = 0;
    Extension->FifoCorruptionCount = 0;
    Extension->PortStatusQueryCount = 0;
    Extension->LastPortStatusRaw = 0xFFFFFFFF;
    Extension->LastPortStatusPacked = 0xFFFFFFFF;
    Extension->RetryTimerDueTime = 0;
    Extension->Stopping = 0;
    Extension->Suspended = FALSE;
    Extension->SofRequested = FALSE;
    KeInitializeSpinLock(&Extension->Lock);
    KeInitializeSpinLock(&Extension->RetryTimerLock);
    KeInitializeTimerEx(&Extension->RetryTimer, NotificationTimer);
    KeInitializeDpc(&Extension->RetryTimerDpc, Dwc2RetryTimerDpc, Extension);
    KeInitializeEvent(&Extension->RetryTimerDpcEvent, NotificationEvent, TRUE);
    InitializeListHead(&Extension->EndpointList);
    Extension->RootHubIrqEnabled = TRUE;
    Resources->Reserved &= ~USBPORT_RES_DMA_ADDR_MASK;
    Resources->Reserved |= USBPORT_RES_DMA_ADDR_32BIT;

    SnpsId = Dwc2ReadRegister(Extension, DWC2_GSNPSID);
    if ((SnpsId & DWC2_GSNPSID_MASK) != DWC2_GSNPSID_VALUE)
    {
        DPRINT1("[DWC2] invalid GSNPSID=%08lx at %p\n", SnpsId, Extension->Registers);
        Dwc2DumpControllerState(Extension, "invalid Synopsys ID");
        return MP_STATUS_HW_ERROR;
    }

    DPRINT1("[DWC2] start MMIO=%p length=%lu IRQ vector=%lu level=%u affinity=%p GSNPSID=%08lx\n",
            Extension->Registers,
            Extension->RegisterLength,
            Resources->InterruptVector,
            Resources->InterruptLevel,
            (PVOID)Resources->InterruptAffinity,
            SnpsId);

    if (!Dwc2InitializeHardware(Extension))
    {
        Dwc2DumpControllerState(Extension, "controller initialization failed");
        return MP_STATUS_HW_ERROR;
    }

    Extension->LastFrameNumber = Dwc2ReadRegister(Extension, DWC2_HFNUM) & DWC2_HFNUM_FRNUM_MASK;
    Extension->FrameHighBits = 0;
    Extension->Started = TRUE;
    Extension->LastConnectStatus = !!(Dwc2ReadRegister(Extension, DWC2_HPRT0) & DWC2_HPRT_CONNSTS);
    DPRINT1("[DWC2] start complete; waiting for USBPORT to enable interrupts\n");
    return MP_STATUS_SUCCESS;
}

static VOID
NTAPI
Dwc2StopController(
    _In_ PVOID MiniportExtension,
    _In_ BOOLEAN DisableInterrupts)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    ULONG Index;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DisableInterrupts);
    Extension->Started = FALSE;
    Dwc2StopRetryTimer(Extension);
    Dwc2SetGlobalInterruptEnable(Extension, FALSE);
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, 0);

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    for (Index = 0; Index < Extension->NumberOfChannels; Index++)
        Dwc2HaltChannel(Extension, (UCHAR)Index);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    DPRINT1("[DWC2] controller stopped\n");
}

static VOID
NTAPI
Dwc2SuspendController(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    Extension->Suspended = TRUE;
    Dwc2SetGlobalInterruptEnable(Extension, FALSE);
    DPRINT1("[DWC2] controller suspended\n");
}

static MPSTATUS
NTAPI
Dwc2ResumeController(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    KIRQL OldIrql;

    if (!Dwc2InitializeHardware(Extension))
    {
        Dwc2DumpControllerState(Extension, "controller resume failed");
        return MP_STATUS_HW_ERROR;
    }

    Extension->Suspended = FALSE;
    Extension->Started = TRUE;
    Dwc2SetGlobalInterruptEnable(Extension, TRUE);
    Dwc2RegPacket.UsbPortInvalidateRootHub(Extension);
    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Dwc2TryStartPending(Extension);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    DPRINT1("[DWC2] controller resumed\n");
    return MP_STATUS_SUCCESS;
}

static BOOLEAN
NTAPI
Dwc2InterruptService(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    ULONG InterruptStatus;

    if (!Extension->Started)
        return FALSE;

    InterruptStatus = Dwc2ReadRegister(Extension, DWC2_GINTSTS) & Dwc2ReadRegister(Extension, DWC2_GINTMSK);
    if (!InterruptStatus)
        return FALSE;

    InterlockedOr(&Extension->PendingGlobalInterrupts, (LONG)InterruptStatus);
    if (InterruptStatus & DWC2_GINT_HCHINT)
        InterlockedOr(&Extension->PendingChannelInterrupts, (LONG)(Dwc2ReadRegister(Extension, DWC2_HAINT) & Dwc2ReadRegister(Extension, DWC2_HAINTMSK)));

    Dwc2WriteRegister(Extension, DWC2_GINTSTS, InterruptStatus & ~(DWC2_GINT_HCHINT | DWC2_GINT_PRTINT));
    return TRUE;
}

static BOOLEAN
Dwc2ProcessEventsLocked(
    _In_ PDWC2_EXTENSION Extension)
{
    ULONG ChannelBits;
    ULONG DrainPass;
    ULONG GlobalInterrupts;
    ULONG Index;
    ULONG PendingChannels;
    ULONG SofInterruptCount;
    ULONG ValidChannelMask;
    BOOLEAN FromSof;
    BOOLEAN NotifyRootHub = FALSE;
    BOOLEAN SofWasRequested;

    GlobalInterrupts = (ULONG)InterlockedExchange(&Extension->PendingGlobalInterrupts, 0);
    GlobalInterrupts |= Dwc2ReadRegister(Extension, DWC2_GINTSTS) & Dwc2ReadRegister(Extension, DWC2_GINTMSK);
    Dwc2WriteRegister(Extension, DWC2_GINTSTS, GlobalInterrupts & ~(DWC2_GINT_HCHINT | DWC2_GINT_PRTINT));
    FromSof = !!(GlobalInterrupts & DWC2_GINT_SOF);
    if (FromSof)
    {
        SofWasRequested = Extension->SofRequested;
        Extension->SofRequested = FALSE;
        SofInterruptCount = (ULONG)InterlockedIncrement(&Extension->SofInterruptCount);
        Dwc2UpdateSofMask(Extension);
        if (!SofWasRequested || Dwc2ShouldLogCount(SofInterruptCount))
            DPRINT1("[DWC2] one-shot SOF serviced count=%lu requested=%u frame=%08lx mask=%08lx\n", SofInterruptCount, SofWasRequested, Dwc2ReadRegister(Extension, DWC2_HFNUM), Extension->InterruptMask);
    }

    if (GlobalInterrupts & (DWC2_GINT_PRTINT | DWC2_GINT_DISCONNINT | DWC2_GINT_WKUPINT))
    {
        DPRINT1("[DWC2] root interrupt GINTSTS=%08lx HPRT0=%08lx\n", GlobalInterrupts, Dwc2ReadRegister(Extension, DWC2_HPRT0));
        NotifyRootHub = TRUE;
    }

    /*
     * Drain until quiescent. Processing one completion can arm the next
     * control-transfer stage, and fast hardware may complete that stage while
     * this DPC is still running. A single HAINT snapshot strands that new
     * completion when the platform coalesces the asserted level interrupt.
     */
    for (DrainPass = 0; DrainPass < DWC2_DPC_DRAIN_LIMIT; DrainPass++)
    {
        ChannelBits = (ULONG)InterlockedExchange(&Extension->PendingChannelInterrupts, 0);
        /* Poll raw HAINT: a completion whose HAINTMSK lane was lost must still be reaped here. */
        ChannelBits |= Dwc2ReadRegister(Extension, DWC2_HAINT);
        ValidChannelMask = (1UL << Extension->NumberOfChannels) - 1;
        if (ChannelBits & ~ValidChannelMask)
        {
            DPRINT1("[DWC2] DPC received out-of-range channel bits=%08lx valid=%08lx channels=%lu pass=%lu\n", ChannelBits, ValidChannelMask, Extension->NumberOfChannels, DrainPass);
            Dwc2DumpControllerState(Extension, "out-of-range channel interrupt");
            ChannelBits &= ValidChannelMask;
        }

        for (Index = 0; Index < Extension->NumberOfChannels; Index++)
        {
            if (ChannelBits & (1UL << Index))
                Dwc2ProcessChannel(Extension, (UCHAR)Index);
        }

        Dwc2TryStartPending(Extension);
        PendingChannels = (ULONG)InterlockedCompareExchange(&Extension->PendingChannelInterrupts, 0, 0);
        PendingChannels |= Dwc2ReadRegister(Extension, DWC2_HAINT) & ValidChannelMask;
        if (!PendingChannels)
            break;
    }

    if (DrainPass == DWC2_DPC_DRAIN_LIMIT)
    {
        DPRINT1("[DWC2] interrupt drain budget exhausted pending=%08lx HAINT=%08lx GINTSTS=%08lx\n",
                PendingChannels,
                Dwc2ReadRegister(Extension, DWC2_HAINT),
                Dwc2ReadRegister(Extension, DWC2_GINTSTS));
        Dwc2DumpControllerState(Extension, "interrupt drain budget exhausted");
        for (Index = 0; Index < Extension->NumberOfChannels; Index++)
        {
            if (PendingChannels & (1UL << Index))
            {
                PDWC2_ENDPOINT Endpoint = Extension->Channels[Index];
                Dwc2DumpTransferState(Extension, Endpoint ? Endpoint->Transfer : NULL, "interrupt drain budget exhausted");
            }
        }
    }

    Dwc2DiagnoseStalledChannels(Extension);
    return NotifyRootHub;
}

static VOID
NTAPI
Dwc2InterruptDpc(
    _In_ PVOID MiniportExtension,
    _In_ BOOLEAN EnableInterrupts)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    KIRQL OldIrql;
    BOOLEAN NotifyRootHub;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    NotifyRootHub = Dwc2ProcessEventsLocked(Extension);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    if (NotifyRootHub)
        Dwc2RegPacket.UsbPortInvalidateRootHub(Extension);
    if (EnableInterrupts)
        Dwc2SetGlobalInterruptEnable(Extension, TRUE);
}

static MPSTATUS
NTAPI
Dwc2SubmitTransfer(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint,
    _In_ PUSBPORT_TRANSFER_PARAMETERS Parameters,
    _In_ PVOID MiniportTransfer,
    _In_ PUSBPORT_SCATTER_GATHER_LIST SgList)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;
    PDWC2_TRANSFER Transfer = MiniportTransfer;
    ULONG Copied;
    KIRQL OldIrql;

    if (Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
    {
        DPRINT1("[DWC2] isochronous transfer rejected ep=%p length=%lu\n", Endpoint, Parameters->TransferBufferLength);
        return MP_STATUS_NOT_SUPPORTED;
    }

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    if (!Extension->Started ||
        Endpoint->Transfer ||
        Parameters->TransferBufferLength > Endpoint->BufferLength - DWC2_SETUP_BUFFER_SIZE)
    {
        DPRINT1("[DWC2] submit rejected started=%u active=%p length=%lu maximum=%lu ep=%p state=%lu\n",
                Extension->Started,
                Endpoint->Transfer,
                Parameters->TransferBufferLength,
                Endpoint->BufferLength - DWC2_SETUP_BUFFER_SIZE,
                Endpoint,
                Endpoint->State);
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        return MP_STATUS_FAILURE;
    }

    RtlZeroMemory(Transfer, sizeof(*Transfer));
    Transfer->Parameters = Parameters;
    Transfer->SgList = SgList;
    Transfer->Endpoint = Endpoint;
    Transfer->UsbdStatus = USBD_STATUS_SUCCESS;
    Transfer->DirectionIn = !!(Parameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN);
    Transfer->DataToggle = Endpoint->DataToggle;
    Transfer->Channel = DWC2_INVALID_CHANNEL;
    Transfer->SubmitTime = KeQueryInterruptTime();
    Transfer->Stage = Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_CONTROL ? Dwc2StageSetup : Dwc2StageData;
    Endpoint->Transfer = Transfer;
    Endpoint->SubmitCount++;
    Endpoint->IdleDiagnosticLogged = FALSE;

    if (Dwc2ShouldLogCount(Endpoint->SubmitCount))
    {
        if (Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_CONTROL)
        {
            DPRINT1("[DWC2] control submit accepted endpoint=%p transfer=%p addr=%u state=%lu count=%lu dir=%s length=%lu setup=%02x/%02x/%04x/%04x/%u\n",
                    Endpoint,
                    Transfer,
                    Endpoint->Properties.DeviceAddress,
                    Endpoint->State,
                    Endpoint->SubmitCount,
                    Transfer->DirectionIn ? "IN" : "OUT",
                    Parameters->TransferBufferLength,
                    Parameters->SetupPacket.bmRequestType.B,
                    Parameters->SetupPacket.bRequest,
                    Parameters->SetupPacket.wValue.W,
                    Parameters->SetupPacket.wIndex.W,
                    Parameters->SetupPacket.wLength);
        }
        else
        {
            DPRINT1("[DWC2] transfer submit accepted endpoint=%p transfer=%p addr=%u ep=%u type=%lu state=%lu count=%lu dir=%s length=%lu interval=%u\n", Endpoint, Transfer, Endpoint->Properties.DeviceAddress, Endpoint->Properties.EndpointAddress, Endpoint->Properties.TransferType, Endpoint->State, Endpoint->SubmitCount, Transfer->DirectionIn ? "IN" : "OUT", Parameters->TransferBufferLength, Endpoint->Properties.Reserved4);
        }
    }

    if (!Transfer->DirectionIn && Parameters->TransferBufferLength)
    {
        Copied = Dwc2CopySgToBuffer(SgList, (PUCHAR)Endpoint->BufferVA + DWC2_SETUP_BUFFER_SIZE, Parameters->TransferBufferLength);
        if (Copied != Parameters->TransferBufferLength)
        {
            Endpoint->Transfer = NULL;
            DPRINT1("[DWC2] OUT bounce copy failed copied=%lu expected=%lu\n", Copied, Parameters->TransferBufferLength);
            KeReleaseSpinLock(&Extension->Lock, OldIrql);
            return MP_STATUS_FAILURE;
        }
    }

    KeMemoryBarrier();
    Dwc2TryStartPending(Extension);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    return MP_STATUS_SUCCESS;
}

static VOID
NTAPI
Dwc2AbortTransfer(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint,
    _In_ PVOID MiniportTransfer,
    _Out_ PULONG CompletedLength)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;
    PDWC2_TRANSFER Transfer = MiniportTransfer;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    *CompletedLength = Transfer->BytesTransferred;
    if (Transfer->Channel != DWC2_INVALID_CHANNEL)
    {
        if (!Dwc2HaltChannel(Extension, Transfer->Channel))
        {
            DPRINT1("[DWC2] abort could not halt CH%u; requesting controller reset endpoint=%p transfer=%p\n", Transfer->Channel, Endpoint, Transfer);
            Dwc2DumpTransferState(Extension, Transfer, "abort halt failure");
            Dwc2RegPacket.UsbPortInvalidateController(Extension, USBPORT_INVALIDATE_CONTROLLER_RESET);
        }
        Dwc2ReleaseChannel(Extension, Transfer);
    }

    Transfer->Done = TRUE;
    Transfer->NeedsRetry = FALSE;
    if (Endpoint->Transfer == Transfer)
        Endpoint->Transfer = NULL;

    Dwc2UpdateSofMask(Extension);
    Dwc2TryStartPending(Extension);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    DPRINT1("[DWC2] abort ep=%p transfer=%p completed=%lu\n", Endpoint, Transfer, *CompletedLength);
}

static ULONG
NTAPI
Dwc2GetEndpointState(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint)
{
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;

    UNREFERENCED_PARAMETER(MiniportExtension);
    return Endpoint->State;
}

static VOID
NTAPI
Dwc2SetEndpointState(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint,
    _In_ ULONG State)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;
    ULONG OldState;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    OldState = Endpoint->State;
    Endpoint->State = State;
    Endpoint->StateChangeCount++;
    if (Dwc2ShouldLogCount(Endpoint->StateChangeCount))
        DPRINT1("[DWC2] endpoint state change endpoint=%p addr=%u ep=%u type=%lu old=%lu new=%lu count=%lu transfer=%p channel=%u\n", Endpoint, Endpoint->Properties.DeviceAddress, Endpoint->Properties.EndpointAddress, Endpoint->Properties.TransferType, OldState, State, Endpoint->StateChangeCount, Endpoint->Transfer, Endpoint->Channel);
    if (State == USBPORT_ENDPOINT_ACTIVE)
        Dwc2TryStartPending(Extension);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static VOID
NTAPI
Dwc2PollEndpoint(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;
    PDWC2_TRANSFER Transfer;
    ULONG Copied;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Transfer = Endpoint->Transfer;
    if (!Transfer || !Transfer->Done)
    {
        KeReleaseSpinLock(&Extension->Lock, OldIrql);
        return;
    }

    if (Transfer->DirectionIn && Transfer->BytesTransferred && Transfer->UsbdStatus == USBD_STATUS_SUCCESS)
    {
        KeMemoryBarrier();
        Copied = Dwc2CopyBufferToSg(Transfer->SgList, (PUCHAR)Endpoint->BufferVA + DWC2_SETUP_BUFFER_SIZE, Transfer->BytesTransferred);
        if (Copied != Transfer->BytesTransferred)
        {
            DPRINT1("[DWC2] IN bounce copy failed endpoint=%p transfer=%p copied=%lu expected=%lu sg=%p elements=%lu mapped=%p\n",
                    Endpoint,
                    Transfer,
                    Copied,
                    Transfer->BytesTransferred,
                    Transfer->SgList,
                    Transfer->SgList ? Transfer->SgList->SgElementCount : 0,
                    Transfer->SgList ? Transfer->SgList->MappedSystemVa : NULL);
            Transfer->UsbdStatus = USBD_STATUS_DATA_BUFFER_ERROR;
            Dwc2DumpTransferState(Extension, Transfer, "IN bounce copy failure");
        }
    }

    Endpoint->Transfer = NULL;
    if (Transfer->UsbdStatus != USBD_STATUS_SUCCESS)
    {
        DPRINT1("[DWC2] completing to USBPORT endpoint=%p transfer=%p addr=%u ep=%u status=%08lx bytes=%lu/%lu\n",
                Endpoint,
                Transfer,
                Endpoint->Properties.DeviceAddress,
                Endpoint->Properties.EndpointAddress,
                Transfer->UsbdStatus,
                Transfer->BytesTransferred,
                Transfer->Parameters->TransferBufferLength);
    }
    Dwc2RegPacket.UsbPortCompleteTransfer(Extension, Endpoint, Transfer->Parameters, Transfer->UsbdStatus, Transfer->BytesTransferred);
    Dwc2TryStartPending(Extension);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static VOID
Dwc2Heartbeat(
    _In_ PDWC2_EXTENSION Extension)
{
    ULONG ChannelMask = 0;
    ULONG CorruptCount;
    ULONG HeartbeatCount;
    ULONG Index;
    ULONG NpTxDepth;
    ULONG NpTxStatus;

    for (Index = 0; Index < Extension->NumberOfChannels; Index++)
    {
        if (Extension->Channels[Index])
            ChannelMask |= 1UL << Index;
    }

    NpTxStatus = Dwc2ReadRegister(Extension, DWC2_GNPTXSTS);
    NpTxDepth = Dwc2ReadRegister(Extension, DWC2_GNPTXFSIZ) >> DWC2_FIFOSIZE_DEPTH_SHIFT;
    if ((NpTxStatus & DWC2_GNPTXSTS_FIFO_SPACE_MASK) > NpTxDepth)
    {
        CorruptCount = (ULONG)InterlockedIncrement(&Extension->FifoCorruptionCount);
        if (Dwc2ShouldLogCount(CorruptCount))
        {
            DPRINT1("[DWC2] non-periodic FIFO accounting corrupt GNPTXSTS=%08lx depth=%lu count=%lu\n", NpTxStatus, NpTxDepth, CorruptCount);
            Dwc2DumpControllerState(Extension, "non-periodic FIFO accounting corrupt");
        }
    }

    HeartbeatCount = (ULONG)InterlockedIncrement(&Extension->HeartbeatCount);
    if (Dwc2ShouldLogCount(HeartbeatCount))
    {
        DPRINT1("[DWC2] heartbeat count=%lu GINTSTS=%08lx GINTMSK=%08lx GAHBCFG=%08lx GNPTXSTS=%08lx HPTXSTS=%08lx HAINT=%08lx HPRT0=%08lx HFNUM=%08lx channels=%02lx completions=%ld errors=%ld deferredHalts=%ld recoveries=%ld immediates=%ld fifoCorrupt=%ld wakes=%ld\n",
                HeartbeatCount,
                Dwc2ReadRegister(Extension, DWC2_GINTSTS),
                Dwc2ReadRegister(Extension, DWC2_GINTMSK),
                Dwc2ReadRegister(Extension, DWC2_GAHBCFG),
                NpTxStatus,
                Dwc2ReadRegister(Extension, DWC2_HPTXSTS),
                Dwc2ReadRegister(Extension, DWC2_HAINT),
                Dwc2ReadRegister(Extension, DWC2_HPRT0),
                Dwc2ReadRegister(Extension, DWC2_HFNUM),
                ChannelMask,
                Extension->CompletionCount,
                Extension->TransferErrorCount,
                Extension->DeferredHaltCount,
                Extension->WedgeRecoveryCount,
                Extension->ImmediateInterruptCount,
                Extension->FifoCorruptionCount,
                Extension->RetryTimerWakeCount);
    }
}

static VOID
NTAPI
Dwc2CheckController(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    ULONG SnpsId;
    KIRQL OldIrql;

    if (!Extension->Started)
        return;

    SnpsId = Dwc2ReadRegister(Extension, DWC2_GSNPSID);
    if (SnpsId == 0xFFFFFFFF)
    {
        DPRINT1("[DWC2] controller registers disappeared GSNPSID=%08lx; requesting surprise removal\n", SnpsId);
        Dwc2DumpControllerState(Extension, "controller surprise removal");
        Dwc2RegPacket.UsbPortInvalidateController(Extension, USBPORT_INVALIDATE_CONTROLLER_SURPRISE_REMOVE);
        return;
    }

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Dwc2TryStartPending(Extension);
    Dwc2DiagnoseStalledChannels(Extension);
    Dwc2Heartbeat(Extension);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static ULONG
NTAPI
Dwc2Get32BitFrameNumber(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    ULONG CurrentFrame;

    CurrentFrame = Dwc2ReadRegister(Extension, DWC2_HFNUM) & DWC2_HFNUM_FRNUM_MASK;
    if (CurrentFrame < Extension->LastFrameNumber)
        Extension->FrameHighBits += DWC2_HFNUM_FRNUM_MASK + 1;

    Extension->LastFrameNumber = CurrentFrame;
    return Extension->FrameHighBits + CurrentFrame;
}

static VOID
NTAPI
Dwc2InterruptNextSof(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Dwc2RequestOneShotSof(Extension, "USBPORT endpoint handoff");
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static VOID
NTAPI
Dwc2EnableInterrupts(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
    Dwc2SetGlobalInterruptEnable(Extension, TRUE);
}

static VOID
NTAPI
Dwc2DisableInterrupts(
    _In_ PVOID MiniportExtension)
{
    Dwc2SetGlobalInterruptEnable(MiniportExtension, FALSE);
}

static VOID
Dwc2DiagnoseStalledChannels(
    _In_ PDWC2_EXTENSION Extension)
{
    ULONGLONG CurrentTime = KeQueryInterruptTime();
    PLIST_ENTRY Entry;
    ULONG Index;

    for (Index = 0; Index < Extension->NumberOfChannels; Index++)
    {
        PDWC2_ENDPOINT Endpoint = Extension->Channels[Index];
        PDWC2_TRANSFER Transfer;
        ULONG Character;
        ULONG RawInterrupt;
        ULONG Informational;
        ULONGLONG Elapsed;

        if (!Endpoint || !Endpoint->Transfer)
            continue;

        Transfer = Endpoint->Transfer;
        if (Transfer->Done || !Transfer->StageStartTime)
            continue;
        Elapsed = CurrentTime - Transfer->StageStartTime;
        if (Transfer->HangDiagnosticCount >= 32 ||
            Elapsed < (DWC2_HANG_DIAGNOSTIC_INTERVAL << Transfer->HangDiagnosticCount))
            continue;

        Character = Dwc2ReadRegister(Extension, DWC2_HCCHAR(Index));
        RawInterrupt = Dwc2ReadRegister(Extension, DWC2_HCINT(Index)) & DWC2_HCINT_VALID_MASK;
        Transfer->HangDiagnosticCount++;
        DPRINT1("[DWC2] CH%lu stalled count=%lu stage=%u addr=%u ep=%u enabled=%u HCINT=%08lx HFNUM=%08lx armFrame=%08lx elapsed100ns=%I64u recoveries=%lu\n",
                Index,
                Transfer->HangDiagnosticCount,
                Transfer->Stage,
                Endpoint->Properties.DeviceAddress,
                Endpoint->Properties.EndpointAddress,
                !!(Character & DWC2_HCCHAR_CHENA),
                RawInterrupt,
                Dwc2ReadRegister(Extension, DWC2_HFNUM),
                Transfer->StageStartFrame,
                Elapsed,
                Transfer->RecoveryCount);
        if (Transfer->HangDiagnosticCount == 1)
            Dwc2DumpChannelState(Extension, (UCHAR)Index, "active transfer timeout");

        /*
         * NAK/ACK/NYET bits latch even while masked, so an armed channel with
         * none of them (and no terminal status) has executed nothing since the
         * arm: the request never reached the bus and no interrupt will come.
         * Preserve the sampled bits for the CHHLTD decode, then recover the
         * wedge by halting and re-arming the stage a bounded number of times.
         */
        Informational = RawInterrupt & (DWC2_HCINT_NAK | DWC2_HCINT_ACK | DWC2_HCINT_NYET);
        if (Informational)
        {
            Transfer->PendingChannelStatus |= Informational;
            Dwc2WriteRegister(Extension, DWC2_HCINT(Index), Informational);
        }

        if (Elapsed >= DWC2_WEDGE_RECOVERY_INTERVAL && !(RawInterrupt & DWC2_HCINT_REASON_MASK))
        {
            BOOLEAN Halted;

            InterlockedIncrement(&Extension->WedgeRecoveryCount);
            Transfer->RecoveryCount++;
            DPRINT1("[DWC2] CH%lu wedged with no bus activity; recovery %lu/%u endpoint=%p transfer=%p stage=%u\n",
                    Index,
                    Transfer->RecoveryCount,
                    DWC2_WEDGE_RECOVERY_LIMIT,
                    Endpoint,
                    Transfer,
                    Transfer->Stage);
            Halted = Dwc2HaltChannel(Extension, (UCHAR)Index);
            if (Halted && Transfer->RecoveryCount <= DWC2_WEDGE_RECOVERY_LIMIT)
                Dwc2ScheduleRetry(Extension, Transfer, DWC2_FRAME_100NS * 10);
            else
                Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_DEV_NOT_RESPONDING);
        }
    }

    for (Entry = Extension->EndpointList.Flink; Entry != &Extension->EndpointList; Entry = Entry->Flink)
    {
        PDWC2_ENDPOINT Endpoint = CONTAINING_RECORD(Entry, DWC2_ENDPOINT, Link);
        PDWC2_TRANSFER Transfer = Endpoint->Transfer;

        if (!Transfer)
        {
            if (!Endpoint->IdleDiagnosticLogged && !Endpoint->SubmitCount && Endpoint->OpenTime && CurrentTime - Endpoint->OpenTime >= DWC2_HANG_DIAGNOSTIC_INTERVAL && Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_CONTROL && Endpoint->Properties.EndpointAddress == 0)
            {
                Endpoint->IdleDiagnosticLogged = TRUE;
                DPRINT1("[DWC2] control endpoint received no SubmitTransfer for >=250ms endpoint=%p addr=%u state=%lu states=%lu openElapsed100ns=%I64u channel=%u\n", Endpoint, Endpoint->Properties.DeviceAddress, Endpoint->State, Endpoint->StateChangeCount, CurrentTime - Endpoint->OpenTime, Endpoint->Channel);
                Dwc2DumpControllerState(Extension, "control endpoint idle before first submit");
            }
            continue;
        }

        if (Transfer->Done)
        {
            if (!Transfer->CompletionDiagnosticLogged && Transfer->CompletionTime && CurrentTime - Transfer->CompletionTime >= DWC2_HANG_DIAGNOSTIC_INTERVAL)
            {
                Transfer->CompletionDiagnosticLogged = TRUE;
                DPRINT1("[DWC2] completed transfer was not polled by USBPORT for >=250ms endpoint=%p transfer=%p addr=%u ep=%u status=%08lx bytes=%lu elapsed100ns=%I64u\n",
                        Endpoint,
                        Transfer,
                        Endpoint->Properties.DeviceAddress,
                        Endpoint->Properties.EndpointAddress,
                        Transfer->UsbdStatus,
                        Transfer->BytesTransferred,
                        CurrentTime - Transfer->CompletionTime);
                Dwc2DumpTransferState(Extension, Transfer, "USBPORT completion handoff timeout");
            }
            continue;
        }

        if (Transfer->NeedsRetry && !Transfer->RetryDiagnosticLogged && Transfer->RetryTime && CurrentTime >= Transfer->RetryTime && CurrentTime - Transfer->RetryTime >= DWC2_HANG_DIAGNOSTIC_INTERVAL)
        {
            Transfer->RetryDiagnosticLogged = TRUE;
            DPRINT1("[DWC2] retry deadline missed by >=250ms endpoint=%p transfer=%p addr=%u ep=%u type=%lu now=%I64u due=%I64u late100ns=%I64u timerScheduled=%ld timerActive=%ld timerDue=%I64u arms=%ld wakes=%ld\n", Endpoint, Transfer, Endpoint->Properties.DeviceAddress, Endpoint->Properties.EndpointAddress, Endpoint->Properties.TransferType, CurrentTime, Transfer->RetryTime, CurrentTime - Transfer->RetryTime, Extension->RetryTimerScheduled, Extension->RetryTimerDpcActive, Extension->RetryTimerDueTime, Extension->RetryTimerArmCount, Extension->RetryTimerWakeCount);
            Dwc2DumpTransferState(Extension, Transfer, "retry timer deadline missed");
        }

        if (Transfer->Channel == DWC2_INVALID_CHANNEL && !Transfer->NeedsRetry && !Transfer->QueueDiagnosticLogged && Transfer->SubmitTime && CurrentTime - Transfer->SubmitTime >= DWC2_HANG_DIAGNOSTIC_INTERVAL)
        {
            Transfer->QueueDiagnosticLogged = TRUE;
            DPRINT1("[DWC2] transfer queued without a channel for >=250ms endpoint=%p transfer=%p addr=%u ep=%u state=%lu elapsed100ns=%I64u\n",
                    Endpoint,
                    Transfer,
                    Endpoint->Properties.DeviceAddress,
                    Endpoint->Properties.EndpointAddress,
                    Endpoint->State,
                    CurrentTime - Transfer->SubmitTime);
            Dwc2DumpTransferState(Extension, Transfer, "channel allocation timeout");
            for (Index = 0; Index < Extension->NumberOfChannels; Index++)
            {
                PDWC2_ENDPOINT Owner = Extension->Channels[Index];
                if (Owner && Owner->Transfer)
                {
                    DPRINT1("[DWC2] channel owner CH%lu endpoint=%p transfer=%p addr=%u ep=%u stage=%u done=%u\n",
                            Index,
                            Owner,
                            Owner->Transfer,
                            Owner->Properties.DeviceAddress,
                            Owner->Properties.EndpointAddress,
                            Owner->Transfer->Stage,
                            Owner->Transfer->Done);
                }
            }
        }
    }
}

static VOID
NTAPI
Dwc2PollController(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    ULONG PortValue;
    ULONG Connected;
    KIRQL OldIrql;
    BOOLEAN NotifyRootHub;

    if (!Extension->Started)
        return;

    PortValue = Dwc2ReadRegister(Extension, DWC2_HPRT0);
    Connected = !!(PortValue & DWC2_HPRT_CONNSTS);
    NotifyRootHub = (PortValue & DWC2_HPRT_CHANGE_MASK) || Connected != Extension->LastConnectStatus;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    NotifyRootHub |= Dwc2ProcessEventsLocked(Extension);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);

    if (NotifyRootHub)
        Dwc2RegPacket.UsbPortInvalidateRootHub(Extension);
}

static VOID
NTAPI
Dwc2SetEndpointDataToggle(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint,
    _In_ ULONG DataToggle)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Endpoint->DataToggle = DataToggle ? 1 : 0;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static ULONG
NTAPI
Dwc2GetEndpointStatus(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint)
{
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;

    UNREFERENCED_PARAMETER(MiniportExtension);
    return Endpoint->Status;
}

static VOID
NTAPI
Dwc2SetEndpointStatus(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint,
    _In_ ULONG Status)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Endpoint->Status = Status;
    if (Status == USBPORT_ENDPOINT_RUN)
        Endpoint->DataToggle = 0;
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static VOID
NTAPI
Dwc2ResetController(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2InitializeHardware(Extension))
        DPRINT1("[DWC2] controller reset failed\n");
}

static VOID
NTAPI
Dwc2RootHubGetData(
    _In_ PVOID MiniportExtension,
    _Out_ PVOID RootHubDataPointer)
{
    PUSBPORT_ROOT_HUB_DATA RootHubData = RootHubDataPointer;
    USBPORT_HUB_20_CHARACTERISTICS Characteristics;

    UNREFERENCED_PARAMETER(MiniportExtension);
    RtlZeroMemory(RootHubData, sizeof(*RootHubData));
    Characteristics.AsUSHORT = 0;
    Characteristics.PowerControlMode = 1;
    Characteristics.NoOverCurrentProtection = 0;
    RootHubData->NumberOfPorts = 1;
    RootHubData->HubCharacteristics.Usb20HubCharacteristics = Characteristics;
    RootHubData->PowerOnToPowerGood = 10;
}

static MPSTATUS
NTAPI
Dwc2RootHubGetStatus(
    _In_ PVOID MiniportExtension,
    _Out_ PUSHORT Status)
{
    UNREFERENCED_PARAMETER(MiniportExtension);
    *Status = USB_GETSTATUS_SELF_POWERED;
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubGetPortStatus(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port,
    _Out_ PUSB_PORT_STATUS_AND_CHANGE PortStatus)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    ULONG Connected;
    ULONG PortValue;
    ULONG Speed;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;

    PortValue = Dwc2ReadRegister(Extension, DWC2_HPRT0);
    Connected = !!(PortValue & DWC2_HPRT_CONNSTS);
    RtlZeroMemory(PortStatus, sizeof(*PortStatus));
    PortStatus->PortStatus.Usb20PortStatus.CurrentConnectStatus = Connected;
    PortStatus->PortStatus.Usb20PortStatus.PortEnabledDisabled = !!(PortValue & DWC2_HPRT_ENA);
    PortStatus->PortStatus.Usb20PortStatus.Suspend = !!(PortValue & DWC2_HPRT_SUSP);
    PortStatus->PortStatus.Usb20PortStatus.OverCurrent = !!(PortValue & DWC2_HPRT_OVRCURRACT);
    PortStatus->PortStatus.Usb20PortStatus.Reset = !!(PortValue & DWC2_HPRT_RST);
    PortStatus->PortStatus.Usb20PortStatus.PortPower = !!(PortValue & DWC2_HPRT_PWR);

    Speed = (PortValue & DWC2_HPRT_SPD_MASK) >> DWC2_HPRT_SPD_SHIFT;
    if (Connected && Speed == DWC2_HPRT_SPD_LOW)
        PortStatus->PortStatus.Usb20PortStatus.LowSpeedDeviceAttached = 1;
    if (Connected && Speed == DWC2_HPRT_SPD_HIGH)
        PortStatus->PortStatus.Usb20PortStatus.HighSpeedDeviceAttached = 1;

    if ((PortValue & DWC2_HPRT_CONNDET) || Connected != Extension->LastConnectStatus)
        PortStatus->PortChange.Usb20PortChange.ConnectStatusChange = 1;
    PortStatus->PortChange.Usb20PortChange.PortEnableDisableChange = !!(PortValue & DWC2_HPRT_ENACHG);
    PortStatus->PortChange.Usb20PortChange.OverCurrentIndicatorChange = !!(PortValue & DWC2_HPRT_OVRCURRCHG);
    PortStatus->PortChange.Usb20PortChange.ResetChange = !!Extension->ResetChange;
    PortStatus->PortChange.Usb20PortChange.SuspendChange = !!Extension->SuspendChange;
    Extension->LastConnectStatus = Connected;

    if (PortValue != Extension->LastPortStatusRaw ||
        PortStatus->AsUlong32 != Extension->LastPortStatusPacked ||
        Dwc2ShouldLogCount((ULONG)InterlockedIncrement(&Extension->PortStatusQueryCount)))
    {
        Extension->LastPortStatusRaw = PortValue;
        Extension->LastPortStatusPacked = PortStatus->AsUlong32;
        DPRINT1("[DWC2] root status raw=%08lx status/change=%08lx speed=%lu queries=%ld\n", PortValue, PortStatus->AsUlong32, Speed, Extension->PortStatusQueryCount);
    }
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubGetHubStatus(
    _In_ PVOID MiniportExtension,
    _Out_ PUSB_HUB_STATUS_AND_CHANGE HubStatus)
{
    UNREFERENCED_PARAMETER(MiniportExtension);
    HubStatus->AsUlong32 = 0;
    return MP_STATUS_SUCCESS;
}

static VOID
NTAPI
Dwc2PortResetComplete(
    _In_ PVOID MiniportExtension,
    _In_ PVOID Context)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PUSHORT Port = Context;

    if (!Dwc2ValidPort(*Port))
        return;

    Dwc2WritePort(Extension, 0, DWC2_HPRT_RST, 0);
    Extension->ResetChange = 1;
    DPRINT1("[DWC2] root port reset complete HPRT0=%08lx\n", Dwc2ReadRegister(Extension, DWC2_HPRT0));
    Dwc2RegPacket.UsbPortInvalidateRootHub(Extension);
}

static MPSTATUS
NTAPI
Dwc2RootHubSetReset(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;

    Extension->ResetChange = 0;
    Dwc2WritePort(Extension, DWC2_HPRT_RST | DWC2_HPRT_PWR, 0, 0);
    DPRINT1("[DWC2] root port reset asserted HPRT0=%08lx\n", Dwc2ReadRegister(Extension, DWC2_HPRT0));
    Dwc2RegPacket.UsbPortRequestAsyncCallback(Extension, 50, &Port, sizeof(Port), Dwc2PortResetComplete);
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubSetPower(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    Dwc2WritePort(Extension, DWC2_HPRT_PWR, 0, 0);
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubSetEnable(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    if (Dwc2ReadRegister(Extension, DWC2_HPRT0) & DWC2_HPRT_ENA)
        return MP_STATUS_SUCCESS;
    if (Dwc2ReadRegister(Extension, DWC2_HPRT0) & DWC2_HPRT_CONNSTS)
        return Dwc2RootHubSetReset(Extension, Port);
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubSetSuspend(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    Extension->SuspendChange = 0;
    Dwc2WritePort(Extension, DWC2_HPRT_SUSP, 0, 0);
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubClearEnable(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    Dwc2WritePort(Extension, 0, 0, DWC2_HPRT_ENA);
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubClearPower(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    Dwc2WritePort(Extension, 0, DWC2_HPRT_PWR, 0);
    return MP_STATUS_SUCCESS;
}

static VOID
NTAPI
Dwc2PortResumeComplete(
    _In_ PVOID MiniportExtension,
    _In_ PVOID Context)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PUSHORT Port = Context;

    if (!Dwc2ValidPort(*Port))
        return;
    Dwc2WritePort(Extension, 0, DWC2_HPRT_RES | DWC2_HPRT_SUSP, 0);
    Extension->SuspendChange = 1;
    Dwc2RegPacket.UsbPortInvalidateRootHub(Extension);
}

static MPSTATUS
NTAPI
Dwc2RootHubClearSuspend(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    Dwc2WritePort(Extension, DWC2_HPRT_RES, 0, 0);
    Dwc2RegPacket.UsbPortRequestAsyncCallback(Extension, 20, &Port, sizeof(Port), Dwc2PortResumeComplete);
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubClearEnableChange(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    Dwc2WritePort(MiniportExtension, 0, 0, DWC2_HPRT_ENACHG);
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubClearConnectChange(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    Dwc2WritePort(Extension, 0, 0, DWC2_HPRT_CONNDET);
    Extension->LastConnectStatus = !!(Dwc2ReadRegister(Extension, DWC2_HPRT0) & DWC2_HPRT_CONNSTS);
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubClearResetChange(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    Extension->ResetChange = 0;
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubClearSuspendChange(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    Extension->SuspendChange = 0;
    return MP_STATUS_SUCCESS;
}

static MPSTATUS
NTAPI
Dwc2RootHubClearOvercurrentChange(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port)
{
    if (!Dwc2ValidPort(Port))
        return MP_STATUS_FAILURE;
    Dwc2WritePort(MiniportExtension, 0, 0, DWC2_HPRT_OVRCURRCHG);
    return MP_STATUS_SUCCESS;
}

static VOID
NTAPI
Dwc2RootHubDisableIrq(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Extension->RootHubIrqEnabled = FALSE;
    Extension->InterruptMask &= ~DWC2_GINT_PRTINT;
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static VOID
NTAPI
Dwc2RootHubEnableIrq(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Extension->Lock, &OldIrql);
    Extension->RootHubIrqEnabled = TRUE;
    Extension->InterruptMask |= DWC2_GINT_PRTINT;
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
    KeReleaseSpinLock(&Extension->Lock, OldIrql);
}

static MPSTATUS
NTAPI
Dwc2StartSendOnePacket(
    _In_ PVOID MiniportExtension,
    _In_ PVOID PacketParameters,
    _In_ PVOID Data,
    _Inout_ PULONG DataLength,
    _In_ PVOID BufferVA,
    _In_ PVOID BufferPA,
    _In_ ULONG BufferLength,
    _Out_ USBD_STATUS *UsbdStatus)
{
    UNREFERENCED_PARAMETER(MiniportExtension);
    UNREFERENCED_PARAMETER(PacketParameters);
    UNREFERENCED_PARAMETER(Data);
    UNREFERENCED_PARAMETER(DataLength);
    UNREFERENCED_PARAMETER(BufferVA);
    UNREFERENCED_PARAMETER(BufferPA);
    UNREFERENCED_PARAMETER(BufferLength);
    *UsbdStatus = USBD_STATUS_NOT_SUPPORTED;
    return MP_STATUS_NOT_SUPPORTED;
}

static MPSTATUS
NTAPI
Dwc2EndSendOnePacket(
    _In_ PVOID MiniportExtension,
    _In_ PVOID PacketParameters,
    _In_ PVOID Data,
    _Inout_ PULONG DataLength,
    _In_ PVOID BufferVA,
    _In_ PVOID BufferPA,
    _In_ ULONG BufferLength,
    _Out_ USBD_STATUS *UsbdStatus)
{
    return Dwc2StartSendOnePacket(MiniportExtension, PacketParameters, Data, DataLength, BufferVA, BufferPA, BufferLength, UsbdStatus);
}

static MPSTATUS
NTAPI
Dwc2PassThru(
    _In_ PVOID MiniportExtension,
    _In_ PVOID PassThruParameters,
    _In_ ULONG ParameterLength,
    _In_ PVOID Parameters)
{
    UNREFERENCED_PARAMETER(MiniportExtension);
    UNREFERENCED_PARAMETER(PassThruParameters);
    UNREFERENCED_PARAMETER(ParameterLength);
    UNREFERENCED_PARAMETER(Parameters);
    return MP_STATUS_NOT_SUPPORTED;
}

static BOOLEAN
NTAPI
Dwc2QueryPortAttributes(
    _In_ PVOID MiniportExtension,
    _In_ USHORT Port,
    _Out_ PULONG Attributes)
{
    UNREFERENCED_PARAMETER(MiniportExtension);
    UNREFERENCED_PARAMETER(Port);
    *Attributes = 0;
    return FALSE;
}

static VOID
NTAPI
Dwc2RebalanceEndpoint(
    _In_ PVOID MiniportExtension,
    _In_ PUSBPORT_ENDPOINT_PROPERTIES Properties,
    _In_ PVOID MiniportEndpoint)
{
    Dwc2ReopenEndpoint(MiniportExtension, Properties, MiniportEndpoint);
}

static VOID
NTAPI
Dwc2FlushInterrupts(
    _In_ PVOID MiniportExtension)
{
    Dwc2InterruptDpc(MiniportExtension, FALSE);
}

static VOID
NTAPI
Dwc2Unload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    RtlZeroMemory(&Dwc2RegPacket, sizeof(Dwc2RegPacket));
    Dwc2RegPacket.MiniPortVersion = USB_MINIPORT_VERSION_EHCI;
    Dwc2RegPacket.MiniPortFlags = USB_MINIPORT_FLAGS_INTERRUPT | USB_MINIPORT_FLAGS_MEMORY_IO | USB_MINIPORT_FLAGS_USB2 | USB_MINIPORT_FLAGS_USB2_DIRECT_FSLS;
    Dwc2RegPacket.MiniPortBusBandwidth = TOTAL_USB20_BUS_BANDWIDTH;
    Dwc2RegPacket.MiniPortExtensionSize = sizeof(DWC2_EXTENSION);
    Dwc2RegPacket.MiniPortEndpointSize = sizeof(DWC2_ENDPOINT);
    Dwc2RegPacket.MiniPortTransferSize = sizeof(DWC2_TRANSFER);
    Dwc2RegPacket.MiniPortResourcesSize = 0;
    Dwc2RegPacket.OpenEndpoint = Dwc2OpenEndpoint;
    Dwc2RegPacket.ReopenEndpoint = Dwc2ReopenEndpoint;
    Dwc2RegPacket.QueryEndpointRequirements = Dwc2QueryEndpointRequirements;
    Dwc2RegPacket.CloseEndpoint = Dwc2CloseEndpoint;
    Dwc2RegPacket.StartController = Dwc2StartController;
    Dwc2RegPacket.StopController = Dwc2StopController;
    Dwc2RegPacket.SuspendController = Dwc2SuspendController;
    Dwc2RegPacket.ResumeController = Dwc2ResumeController;
    Dwc2RegPacket.InterruptService = Dwc2InterruptService;
    Dwc2RegPacket.InterruptDpc = Dwc2InterruptDpc;
    Dwc2RegPacket.SubmitTransfer = Dwc2SubmitTransfer;
    Dwc2RegPacket.SubmitIsoTransfer = NULL;
    Dwc2RegPacket.AbortTransfer = Dwc2AbortTransfer;
    Dwc2RegPacket.GetEndpointState = Dwc2GetEndpointState;
    Dwc2RegPacket.SetEndpointState = Dwc2SetEndpointState;
    Dwc2RegPacket.PollEndpoint = Dwc2PollEndpoint;
    Dwc2RegPacket.CheckController = Dwc2CheckController;
    Dwc2RegPacket.Get32BitFrameNumber = Dwc2Get32BitFrameNumber;
    Dwc2RegPacket.InterruptNextSOF = Dwc2InterruptNextSof;
    Dwc2RegPacket.EnableInterrupts = Dwc2EnableInterrupts;
    Dwc2RegPacket.DisableInterrupts = Dwc2DisableInterrupts;
    Dwc2RegPacket.PollController = Dwc2PollController;
    Dwc2RegPacket.SetEndpointDataToggle = Dwc2SetEndpointDataToggle;
    Dwc2RegPacket.GetEndpointStatus = Dwc2GetEndpointStatus;
    Dwc2RegPacket.SetEndpointStatus = Dwc2SetEndpointStatus;
    Dwc2RegPacket.ResetController = Dwc2ResetController;
    Dwc2RegPacket.RH_GetRootHubData = Dwc2RootHubGetData;
    Dwc2RegPacket.RH_GetStatus = Dwc2RootHubGetStatus;
    Dwc2RegPacket.RH_GetPortStatus = Dwc2RootHubGetPortStatus;
    Dwc2RegPacket.RH_GetHubStatus = Dwc2RootHubGetHubStatus;
    Dwc2RegPacket.RH_SetFeaturePortReset = Dwc2RootHubSetReset;
    Dwc2RegPacket.RH_SetFeaturePortPower = Dwc2RootHubSetPower;
    Dwc2RegPacket.RH_SetFeaturePortEnable = Dwc2RootHubSetEnable;
    Dwc2RegPacket.RH_SetFeaturePortSuspend = Dwc2RootHubSetSuspend;
    Dwc2RegPacket.RH_ClearFeaturePortEnable = Dwc2RootHubClearEnable;
    Dwc2RegPacket.RH_ClearFeaturePortPower = Dwc2RootHubClearPower;
    Dwc2RegPacket.RH_ClearFeaturePortSuspend = Dwc2RootHubClearSuspend;
    Dwc2RegPacket.RH_ClearFeaturePortEnableChange = Dwc2RootHubClearEnableChange;
    Dwc2RegPacket.RH_ClearFeaturePortConnectChange = Dwc2RootHubClearConnectChange;
    Dwc2RegPacket.RH_ClearFeaturePortResetChange = Dwc2RootHubClearResetChange;
    Dwc2RegPacket.RH_ClearFeaturePortSuspendChange = Dwc2RootHubClearSuspendChange;
    Dwc2RegPacket.RH_ClearFeaturePortOvercurrentChange = Dwc2RootHubClearOvercurrentChange;
    Dwc2RegPacket.RH_DisableIrq = Dwc2RootHubDisableIrq;
    Dwc2RegPacket.RH_EnableIrq = Dwc2RootHubEnableIrq;
    Dwc2RegPacket.StartSendOnePacket = Dwc2StartSendOnePacket;
    Dwc2RegPacket.EndSendOnePacket = Dwc2EndSendOnePacket;
    Dwc2RegPacket.PassThru = Dwc2PassThru;
    Dwc2RegPacket.QueryPortAttributes = Dwc2QueryPortAttributes;
    Dwc2RegPacket.RebalanceEndpoint = Dwc2RebalanceEndpoint;
    Dwc2RegPacket.FlushInterrupts = Dwc2FlushInterrupts;
    Dwc2RegPacket.RH_ChirpRootPort = Dwc2RootHubSetReset;
    DriverObject->DriverUnload = Dwc2Unload;
    Status = USBPORT_RegisterUSBPortDriver(DriverObject, USB20_MINIPORT_INTERFACE_VERSION, &Dwc2RegPacket);
    DPRINT1("[DWC2] DriverEntry status=%08lx\n", Status);
    return Status;
}
