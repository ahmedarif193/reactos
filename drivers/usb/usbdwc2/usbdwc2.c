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
Dwc2ValidPort(
    _In_ USHORT Port)
{
    return Port == 1;
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

    AhbConfig = Dwc2ReadRegister(Extension, DWC2_GAHBCFG);

    if (Enable)
        AhbConfig |= DWC2_GAHBCFG_GLBL_INTR_EN;
    else
        AhbConfig &= ~DWC2_GAHBCFG_GLBL_INTR_EN;

    Dwc2WriteRegister(Extension, DWC2_GAHBCFG, AhbConfig);
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
        return FALSE;
    }

    KeStallExecutionProcessor(100);
    return TRUE;
}

static VOID
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

    Dwc2WriteRegister(Extension, DWC2_GRSTCTL, DWC2_GRSTCTL_RXFFLSH);
    for (Index = 0; Index < 1000; Index++)
    {
        if (!(Dwc2ReadRegister(Extension, DWC2_GRSTCTL) & DWC2_GRSTCTL_RXFFLSH))
            break;

        KeStallExecutionProcessor(10);
    }
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

    UsbConfig = Dwc2ReadRegister(Extension, DWC2_GUSBCFG);
    UsbConfig |= DWC2_GUSBCFG_FORCEHOSTMODE;
    Dwc2WriteRegister(Extension, DWC2_GUSBCFG, UsbConfig);
    Dwc2RegPacket.UsbPortWait(Extension, 25);

    if (!(Dwc2ReadRegister(Extension, DWC2_GINTSTS) & DWC2_GINT_CURMODE_HOST))
    {
        DPRINT1("[DWC2] controller did not enter host mode (GINTSTS=%08lx)\n", Dwc2ReadRegister(Extension, DWC2_GINTSTS));
        return FALSE;
    }

    if (!Dwc2CoreReset(Extension))
        return FALSE;

    HardwareConfig = Dwc2ReadRegister(Extension, DWC2_GHWCFG2);
    Extension->NumberOfChannels = ((HardwareConfig & DWC2_GHWCFG2_NUM_HOST_CHAN_MASK) >> DWC2_GHWCFG2_NUM_HOST_CHAN_SHIFT) + 1;
    if (Extension->NumberOfChannels == 0 || Extension->NumberOfChannels > DWC2_MAX_CHANNELS)
        Extension->NumberOfChannels = DWC2_MAX_CHANNELS;

    HostConfig = Dwc2ReadRegister(Extension, DWC2_HCFG);
    HostConfig &= ~DWC2_HCFG_FSLSPCLKSEL_MASK;
    HostConfig |= DWC2_HCFG_FSLSPCLKSEL_30_60_MHZ;
    Dwc2WriteRegister(Extension, DWC2_HCFG, HostConfig);

    AhbConfig = Dwc2ReadRegister(Extension, DWC2_GAHBCFG);
    AhbConfig &= ~DWC2_GAHBCFG_GLBL_INTR_EN;
    AhbConfig |= DWC2_GAHBCFG_DMA_EN | DWC2_GAHBCFG_HBSTLEN_INCR4;
    Dwc2WriteRegister(Extension, DWC2_GAHBCFG, AhbConfig);

    Dwc2FlushFifos(Extension);

    for (Index = 0; Index < Extension->NumberOfChannels; Index++)
    {
        Dwc2WriteRegister(Extension, DWC2_HCINTMSK(Index), 0);
        Dwc2WriteRegister(Extension, DWC2_HCINT(Index), DWC2_HCINT_VALID_MASK);
        Extension->Channels[Index] = NULL;
    }

    Dwc2WriteRegister(Extension, DWC2_HAINTMSK, (1UL << Extension->NumberOfChannels) - 1);
    Dwc2WriteRegister(Extension, DWC2_GINTSTS, 0xFFFFFFFF);
    Extension->InterruptMask = DWC2_GINT_BASE_MASK;
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
    Dwc2WritePort(Extension, DWC2_HPRT_PWR, 0, 0);
    Dwc2RegPacket.UsbPortWait(Extension, 20);

    DPRINT1("[DWC2] initialized GSNPSID=%08lx GHWCFG2=%08lx channels=%lu HPRT0=%08lx\n",
            Dwc2ReadRegister(Extension, DWC2_GSNPSID),
            HardwareConfig,
            Extension->NumberOfChannels,
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
        Dwc2WriteRegister(Extension, DWC2_HCINTMSK(Channel), 0);
        Dwc2WriteRegister(Extension, DWC2_HCINT(Channel), DWC2_HCINT_VALID_MASK);
        Extension->Channels[Channel] = NULL;
    }

    if (Transfer->Endpoint)
        Transfer->Endpoint->Channel = DWC2_INVALID_CHANNEL;

    Transfer->Channel = DWC2_INVALID_CHANNEL;
}

static VOID
Dwc2HaltChannel(
    _In_ PDWC2_EXTENSION Extension,
    _In_ UCHAR Channel)
{
    ULONG Character;

    if (Channel >= Extension->NumberOfChannels)
        return;

    Character = Dwc2ReadRegister(Extension, DWC2_HCCHAR(Channel));
    if (Character & DWC2_HCCHAR_CHENA)
        Dwc2WriteRegister(Extension, DWC2_HCCHAR(Channel), Character | DWC2_HCCHAR_CHDIS | DWC2_HCCHAR_CHENA);

    Dwc2WriteRegister(Extension, DWC2_HCINTMSK(Channel), 0);
    Dwc2WriteRegister(Extension, DWC2_HCINT(Channel), DWC2_HCINT_VALID_MASK);
}

static VOID
Dwc2UpdateSofMask(
    _In_ PDWC2_EXTENSION Extension)
{
    PLIST_ENTRY Entry;
    BOOLEAN NeedsSof = FALSE;

    for (Entry = Extension->EndpointList.Flink; Entry != &Extension->EndpointList; Entry = Entry->Flink)
    {
        PDWC2_ENDPOINT Endpoint = CONTAINING_RECORD(Entry, DWC2_ENDPOINT, Link);

        if (Endpoint->Transfer && Endpoint->Transfer->NeedsSof && !Endpoint->Transfer->Done)
        {
            NeedsSof = TRUE;
            break;
        }
    }

    if (NeedsSof)
        Extension->InterruptMask |= DWC2_GINT_SOF;
    else
        Extension->InterruptMask &= ~DWC2_GINT_SOF;

    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
}

static BOOLEAN
Dwc2ProgramStage(
    _In_ PDWC2_EXTENSION Extension,
    _In_ PDWC2_TRANSFER Transfer)
{
    PDWC2_ENDPOINT Endpoint = Transfer->Endpoint;
    PUSBPORT_ENDPOINT_PROPERTIES Properties = &Endpoint->Properties;
    ULONG Character;
    ULONG DirectionIn;
    ULONG DmaAddress;
    ULONG EndpointType;
    ULONG MaximumStageLength;
    ULONG PacketCount;
    ULONG PacketSize;
    ULONG Pid;
    ULONG Split = 0;
    ULONG TransferSize;
    UCHAR Channel = Transfer->Channel;

    if (Channel >= Extension->NumberOfChannels)
        return FALSE;

    PacketSize = Properties->MaxPacketSize;
    if (PacketSize == 0 || PacketSize > DWC2_HCCHAR_MPS_MASK)
        return FALSE;

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
    Transfer->InitialPacketCount = PacketCount;

    Character = ((ULONG)Properties->DeviceAddress & 0x7F) << DWC2_HCCHAR_DEVADDR_SHIFT;
    Character |= ((ULONG)Properties->EndpointAddress & 0x0F) << DWC2_HCCHAR_EPNUM_SHIFT;
    Character |= EndpointType << DWC2_HCCHAR_EPTYPE_SHIFT;
    Character |= PacketSize & DWC2_HCCHAR_MPS_MASK;

    if (DirectionIn)
        Character |= DWC2_HCCHAR_EPDIR;

    if (Properties->DeviceSpeed == UsbLowSpeed)
        Character |= DWC2_HCCHAR_LSPDDEV;

    if (Properties->TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
    {
        Character |= 1UL << DWC2_HCCHAR_MULTICNT_SHIFT;
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
    Dwc2WriteRegister(Extension, DWC2_HCTSIZ(Channel), (Pid << DWC2_HCTSIZ_PID_SHIFT) | (PacketCount << DWC2_HCTSIZ_PKTCNT_SHIFT) | TransferSize);
    Dwc2WriteRegister(Extension, DWC2_HCDMA(Channel), DmaAddress);
    Dwc2WriteRegister(Extension, DWC2_HCINTMSK(Channel), DWC2_HCINT_DRIVER_MASK);
    KeMemoryBarrier();
    Dwc2WriteRegister(Extension, DWC2_HCCHAR(Channel), Character | DWC2_HCCHAR_CHENA);

    if (Transfer->Stage != Dwc2StageData || Transfer->NakCount < 4)
    {
        DPRINT1("[DWC2] CH%u start stage=%u addr=%u ep=%u type=%lu dir=%s len=%lu pid=%lu split=%u/%u dma=%08lx\n",
                Channel,
                Transfer->Stage,
                Properties->DeviceAddress,
                Properties->EndpointAddress,
                Properties->TransferType,
                DirectionIn ? "IN" : "OUT",
                TransferSize,
                Pid,
                Endpoint->RequiresSplit,
                Transfer->CompleteSplit,
                DmaAddress);
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
    Transfer->NeedsSof = FALSE;
    Dwc2ReleaseChannel(Extension, Transfer);

    DPRINT1("[DWC2] transfer done ep=%p addr=%u endpoint=%u status=%08lx bytes=%lu\n",
            Endpoint,
            Endpoint->Properties.DeviceAddress,
            Endpoint->Properties.EndpointAddress,
            Status,
            Transfer->BytesTransferred);
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
Dwc2RetryAtNextSof(
    _In_ PDWC2_EXTENSION Extension,
    _In_ PDWC2_TRANSFER Transfer)
{
    Dwc2ReleaseChannel(Extension, Transfer);
    Transfer->NeedsSof = TRUE;
    Dwc2UpdateSofMask(Extension);
}

static VOID
Dwc2ProcessChannel(
    _In_ PDWC2_EXTENSION Extension,
    _In_ UCHAR Channel)
{
    PDWC2_ENDPOINT Endpoint;
    PDWC2_TRANSFER Transfer;
    ULONG ActualLength;
    ULONG InterruptStatus;
    ULONG RemainingPackets;
    ULONG RemainingSize;
    ULONG TransferredPackets;
    BOOLEAN ShortPacket;

    InterruptStatus = Dwc2ReadRegister(Extension, DWC2_HCINT(Channel)) & DWC2_HCINT_VALID_MASK;
    if (!InterruptStatus)
        return;

    Dwc2WriteRegister(Extension, DWC2_HCINT(Channel), InterruptStatus);
    Endpoint = Extension->Channels[Channel];

    if (!Endpoint || !Endpoint->Transfer)
    {
        DPRINT1("[DWC2] stray channel interrupt CH%u status=%08lx\n", Channel, InterruptStatus);
        return;
    }

    Transfer = Endpoint->Transfer;
    if (Transfer->Channel != Channel || Transfer->Done)
        return;

    if ((InterruptStatus & DWC2_HCINT_ACK) && Endpoint->RequiresSplit && !Transfer->CompleteSplit && !(InterruptStatus & DWC2_HCINT_XFERCOMPL))
    {
        Transfer->CompleteSplit = TRUE;
        Dwc2RetryAtNextSof(Extension, Transfer);
        return;
    }

    if ((InterruptStatus & (DWC2_HCINT_NAK | DWC2_HCINT_NYET)) && !(InterruptStatus & DWC2_HCINT_XFERCOMPL))
    {
        Transfer->NakCount++;
        if (Transfer->NakCount <= 4 || !(Transfer->NakCount & 0x3FF))
        {
            DPRINT1("[DWC2] CH%u retry status=%08lx count=%lu stage=%u split=%u\n",
                    Channel,
                    InterruptStatus,
                    Transfer->NakCount,
                    Transfer->Stage,
                    Transfer->CompleteSplit);
        }

        if ((InterruptStatus & DWC2_HCINT_CHHLTD) || Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT || Transfer->CompleteSplit)
            Dwc2RetryAtNextSof(Extension, Transfer);
        return;
    }

    if (InterruptStatus & (DWC2_HCINT_STALL | DWC2_HCINT_BBLERR | DWC2_HCINT_AHBERR | DWC2_HCINT_XACTERR | DWC2_HCINT_DATATGLERR | DWC2_HCINT_FRMOVRUN))
    {
        Endpoint->Status = USBPORT_ENDPOINT_HALT;
        DPRINT1("[DWC2] CH%u error HCINT=%08lx HCTSIZ=%08lx HCCHAR=%08lx\n",
                Channel,
                InterruptStatus,
                Dwc2ReadRegister(Extension, DWC2_HCTSIZ(Channel)),
                Dwc2ReadRegister(Extension, DWC2_HCCHAR(Channel)));
        Dwc2FinishTransfer(Extension, Transfer, Dwc2DecodeChannelError(InterruptStatus));
        return;
    }

    if (!(InterruptStatus & DWC2_HCINT_XFERCOMPL))
    {
        if (InterruptStatus & DWC2_HCINT_CHHLTD)
            Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_DEV_NOT_RESPONDING);
        return;
    }

    RemainingSize = Dwc2ReadRegister(Extension, DWC2_HCTSIZ(Channel)) & DWC2_HCTSIZ_XFERSIZE_MASK;
    RemainingPackets = (Dwc2ReadRegister(Extension, DWC2_HCTSIZ(Channel)) & DWC2_HCTSIZ_PKTCNT_MASK) >> DWC2_HCTSIZ_PKTCNT_SHIFT;
    ActualLength = Transfer->ProgrammedLength;
    if (RemainingSize < ActualLength)
        ActualLength -= RemainingSize;
    else
        ActualLength = 0;

    TransferredPackets = Transfer->InitialPacketCount;
    if (RemainingPackets < TransferredPackets)
        TransferredPackets -= RemainingPackets;
    else
        TransferredPackets = ActualLength ? (ActualLength + Endpoint->Properties.MaxPacketSize - 1) / Endpoint->Properties.MaxPacketSize : 1;

    ShortPacket = Transfer->ProgrammedLength != 0 && ActualLength < Transfer->ProgrammedLength;
    Transfer->CompleteSplit = FALSE;
    Transfer->NakCount = 0;

    if (Transfer->Stage == Dwc2StageSetup)
    {
        Transfer->DataToggle = 1;
        if (Transfer->Parameters->TransferBufferLength)
            Transfer->Stage = Dwc2StageData;
        else
            Transfer->Stage = Dwc2StageStatus;

        if (!Dwc2ProgramStage(Extension, Transfer))
            Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
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
                Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
            return;
        }

        if (Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_CONTROL)
        {
            Transfer->Stage = Dwc2StageStatus;
            if (!Dwc2ProgramStage(Extension, Transfer))
                Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
            return;
        }

        Endpoint->DataToggle = Transfer->DataToggle;
        Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_SUCCESS);
        return;
    }

    Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_SUCCESS);
}

static VOID
Dwc2TryStartPending(
    _In_ PDWC2_EXTENSION Extension,
    _In_ BOOLEAN FromSof)
{
    PLIST_ENTRY Entry;

    for (Entry = Extension->EndpointList.Flink; Entry != &Extension->EndpointList; Entry = Entry->Flink)
    {
        PDWC2_ENDPOINT Endpoint = CONTAINING_RECORD(Entry, DWC2_ENDPOINT, Link);
        PDWC2_TRANSFER Transfer = Endpoint->Transfer;
        UCHAR Channel;

        if (!Transfer || Transfer->Done || Transfer->Channel != DWC2_INVALID_CHANNEL)
            continue;
        if (Transfer->NeedsSof && !FromSof)
            continue;

        Channel = Dwc2AllocateChannel(Extension, Endpoint);
        if (Channel == DWC2_INVALID_CHANNEL)
            break;

        Transfer->Channel = Channel;
        Transfer->NeedsSof = FALSE;
        if (!Dwc2ProgramStage(Extension, Transfer))
            Dwc2FinishTransfer(Extension, Transfer, USBD_STATUS_INTERNAL_HC_ERROR);
    }

    Dwc2UpdateSofMask(Extension);
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

    if (Properties->TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
        return MP_STATUS_NOT_SUPPORTED;
    if (!Properties->BufferVA || Properties->BufferLength < DWC2_ENDPOINT_BUFFER_SIZE)
        return MP_STATUS_NO_RESOURCES;

    RtlZeroMemory(Endpoint, sizeof(*Endpoint));
    Endpoint->Properties = *Properties;
    Endpoint->BufferVA = (PVOID)Properties->BufferVA;
    Endpoint->BufferPA = Properties->BufferPA;
    Endpoint->BufferLength = Properties->BufferLength;
    Endpoint->State = USBPORT_ENDPOINT_PAUSED;
    Endpoint->Status = USBPORT_ENDPOINT_RUN;
    Endpoint->Channel = DWC2_INVALID_CHANNEL;
    Endpoint->RequiresSplit = Properties->DeviceSpeed != UsbHighSpeed && Properties->TtHubAddr != (USHORT)-1;
    InsertTailList(&Extension->EndpointList, &Endpoint->Link);
    Endpoint->Listed = TRUE;

    DPRINT1("[DWC2] endpoint open ep=%p addr=%u endpoint=%u type=%lu speed=%u mps=%lu hub=%u port=%u split=%u buffer=%08lx/%p\n",
            Endpoint,
            Properties->DeviceAddress,
            Properties->EndpointAddress,
            Properties->TransferType,
            Properties->DeviceSpeed,
            Properties->MaxPacketSize,
            Properties->HubAddr,
            Properties->PortNumber,
            Endpoint->RequiresSplit,
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
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;

    UNREFERENCED_PARAMETER(MiniportExtension);
    Endpoint->Properties = *Properties;
    Endpoint->BufferVA = (PVOID)Properties->BufferVA;
    Endpoint->BufferPA = Properties->BufferPA;
    Endpoint->BufferLength = Properties->BufferLength;
    Endpoint->RequiresSplit = Properties->DeviceSpeed != UsbHighSpeed && Properties->TtHubAddr != (USHORT)-1;
    DPRINT1("[DWC2] endpoint reopen ep=%p addr=%u endpoint=%u mps=%lu\n", Endpoint, Properties->DeviceAddress, Properties->EndpointAddress, Properties->MaxPacketSize);
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
        Requirements->HeaderBufferSize = DWC2_ENDPOINT_BUFFER_SIZE;
        Requirements->MaxTransferSize = DWC2_MAX_TRANSFER_SIZE;
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

    UNREFERENCED_PARAMETER(DisablePeriodic);

    if (Endpoint->Transfer && Endpoint->Transfer->Channel != DWC2_INVALID_CHANNEL)
    {
        Dwc2HaltChannel(Extension, Endpoint->Transfer->Channel);
        Dwc2ReleaseChannel(Extension, Endpoint->Transfer);
    }

    Endpoint->Transfer = NULL;
    if (Endpoint->Listed)
    {
        RemoveEntryList(&Endpoint->Link);
        Endpoint->Listed = FALSE;
    }

    Endpoint->State = USBPORT_ENDPOINT_CLOSED;
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
        return MP_STATUS_FAILURE;

    Extension->Registers = Resources->ResourceBase;
    Extension->RegisterLength = Resources->IoSpaceLength;
    InitializeListHead(&Extension->EndpointList);
    Extension->RootHubIrqEnabled = TRUE;
    Resources->Reserved &= ~USBPORT_RES_DMA_ADDR_MASK;
    Resources->Reserved |= USBPORT_RES_DMA_ADDR_32BIT;

    SnpsId = Dwc2ReadRegister(Extension, DWC2_GSNPSID);
    if ((SnpsId & DWC2_GSNPSID_MASK) != DWC2_GSNPSID_VALUE)
    {
        DPRINT1("[DWC2] invalid GSNPSID=%08lx at %p\n", SnpsId, Extension->Registers);
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
        return MP_STATUS_HW_ERROR;

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

    UNREFERENCED_PARAMETER(DisableInterrupts);
    Dwc2SetGlobalInterruptEnable(Extension, FALSE);
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, 0);

    for (Index = 0; Index < Extension->NumberOfChannels; Index++)
        Dwc2HaltChannel(Extension, (UCHAR)Index);

    Extension->Started = FALSE;
    DPRINT1("[DWC2] controller stopped\n");
}

static VOID
NTAPI
Dwc2SuspendController(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    Dwc2SetGlobalInterruptEnable(Extension, FALSE);
    Extension->Suspended = TRUE;
    DPRINT1("[DWC2] controller suspended\n");
}

static MPSTATUS
NTAPI
Dwc2ResumeController(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    if (!Dwc2InitializeHardware(Extension))
        return MP_STATUS_HW_ERROR;

    Extension->Suspended = FALSE;
    Extension->Started = TRUE;
    Dwc2SetGlobalInterruptEnable(Extension, TRUE);
    Dwc2RegPacket.UsbPortInvalidateRootHub(Extension);
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

    Extension->PendingGlobalInterrupts |= InterruptStatus;
    if (InterruptStatus & DWC2_GINT_HCHINT)
        Extension->PendingChannelInterrupts |= Dwc2ReadRegister(Extension, DWC2_HAINT) & Dwc2ReadRegister(Extension, DWC2_HAINTMSK);

    Dwc2WriteRegister(Extension, DWC2_GINTSTS, InterruptStatus & ~(DWC2_GINT_HCHINT | DWC2_GINT_PRTINT));
    return TRUE;
}

static VOID
NTAPI
Dwc2InterruptDpc(
    _In_ PVOID MiniportExtension,
    _In_ BOOLEAN EnableInterrupts)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    ULONG ChannelBits;
    ULONG GlobalInterrupts;
    ULONG Index;
    BOOLEAN FromSof;

    GlobalInterrupts = Extension->PendingGlobalInterrupts;
    Extension->PendingGlobalInterrupts = 0;
    ChannelBits = Extension->PendingChannelInterrupts;
    Extension->PendingChannelInterrupts = 0;

    GlobalInterrupts |= Dwc2ReadRegister(Extension, DWC2_GINTSTS) & Dwc2ReadRegister(Extension, DWC2_GINTMSK);
    ChannelBits |= Dwc2ReadRegister(Extension, DWC2_HAINT) & Dwc2ReadRegister(Extension, DWC2_HAINTMSK);
    FromSof = !!(GlobalInterrupts & DWC2_GINT_SOF);

    if (GlobalInterrupts & (DWC2_GINT_PRTINT | DWC2_GINT_DISCONNINT | DWC2_GINT_WKUPINT))
    {
        DPRINT1("[DWC2] root interrupt GINTSTS=%08lx HPRT0=%08lx\n", GlobalInterrupts, Dwc2ReadRegister(Extension, DWC2_HPRT0));
        Dwc2RegPacket.UsbPortInvalidateRootHub(Extension);
    }

    for (Index = 0; Index < Extension->NumberOfChannels; Index++)
    {
        if (ChannelBits & (1UL << Index))
            Dwc2ProcessChannel(Extension, (UCHAR)Index);
    }

    Dwc2TryStartPending(Extension, FromSof);
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

    if (!Extension->Started || Endpoint->Transfer || Parameters->TransferBufferLength > DWC2_MAX_TRANSFER_SIZE)
        return MP_STATUS_FAILURE;
    if (Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
        return MP_STATUS_NOT_SUPPORTED;

    RtlZeroMemory(Transfer, sizeof(*Transfer));
    Transfer->Parameters = Parameters;
    Transfer->SgList = SgList;
    Transfer->Endpoint = Endpoint;
    Transfer->UsbdStatus = USBD_STATUS_SUCCESS;
    Transfer->DirectionIn = !!(Parameters->TransferFlags & USBD_TRANSFER_DIRECTION_IN);
    Transfer->DataToggle = Endpoint->DataToggle;
    Transfer->Channel = DWC2_INVALID_CHANNEL;
    Transfer->Stage = Endpoint->Properties.TransferType == USBPORT_TRANSFER_TYPE_CONTROL ? Dwc2StageSetup : Dwc2StageData;
    Endpoint->Transfer = Transfer;

    if (!Transfer->DirectionIn && Parameters->TransferBufferLength)
    {
        Copied = Dwc2CopySgToBuffer(SgList, (PUCHAR)Endpoint->BufferVA + DWC2_SETUP_BUFFER_SIZE, Parameters->TransferBufferLength);
        if (Copied != Parameters->TransferBufferLength)
        {
            Endpoint->Transfer = NULL;
            DPRINT1("[DWC2] OUT bounce copy failed copied=%lu expected=%lu\n", Copied, Parameters->TransferBufferLength);
            return MP_STATUS_FAILURE;
        }
    }

    KeMemoryBarrier();
    Dwc2TryStartPending(Extension, FALSE);
    DPRINT1("[DWC2] submit ep=%p transfer=%p addr=%u endpoint=%u type=%lu dir=%s len=%lu channel=%u\n",
            Endpoint,
            Transfer,
            Endpoint->Properties.DeviceAddress,
            Endpoint->Properties.EndpointAddress,
            Endpoint->Properties.TransferType,
            Transfer->DirectionIn ? "IN" : "OUT",
            Parameters->TransferBufferLength,
            Transfer->Channel);
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

    *CompletedLength = Transfer->BytesTransferred;
    if (Transfer->Channel != DWC2_INVALID_CHANNEL)
    {
        Dwc2HaltChannel(Extension, Transfer->Channel);
        Dwc2ReleaseChannel(Extension, Transfer);
    }

    Transfer->Done = TRUE;
    Transfer->NeedsSof = FALSE;
    if (Endpoint->Transfer == Transfer)
        Endpoint->Transfer = NULL;

    Dwc2UpdateSofMask(Extension);
    Dwc2TryStartPending(Extension, FALSE);
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

    Endpoint->State = State;
    if (State == USBPORT_ENDPOINT_ACTIVE)
        Dwc2TryStartPending(Extension, FALSE);
}

static VOID
NTAPI
Dwc2PollEndpoint(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;
    PDWC2_TRANSFER Transfer = Endpoint->Transfer;
    ULONG Copied;

    if (!Transfer || !Transfer->Done)
        return;

    if (Transfer->DirectionIn && Transfer->BytesTransferred && Transfer->UsbdStatus == USBD_STATUS_SUCCESS)
    {
        KeMemoryBarrier();
        Copied = Dwc2CopyBufferToSg(Transfer->SgList, (PUCHAR)Endpoint->BufferVA + DWC2_SETUP_BUFFER_SIZE, Transfer->BytesTransferred);
        if (Copied != Transfer->BytesTransferred)
            Transfer->UsbdStatus = USBD_STATUS_DATA_BUFFER_ERROR;
    }

    Endpoint->Transfer = NULL;
    Dwc2RegPacket.UsbPortCompleteTransfer(Extension, Endpoint, Transfer->Parameters, Transfer->UsbdStatus, Transfer->BytesTransferred);
    Dwc2TryStartPending(Extension, FALSE);
}

static VOID
NTAPI
Dwc2CheckController(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    ULONG SnpsId;

    if (!Extension->Started)
        return;

    SnpsId = Dwc2ReadRegister(Extension, DWC2_GSNPSID);
    if (SnpsId == 0xFFFFFFFF)
        Dwc2RegPacket.UsbPortInvalidateController(Extension, USBPORT_INVALIDATE_CONTROLLER_SURPRISE_REMOVE);
}

static ULONG
NTAPI
Dwc2Get32BitFrameNumber(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    return Dwc2ReadRegister(Extension, DWC2_HFNUM) & DWC2_HFNUM_FRNUM_MASK;
}

static VOID
NTAPI
Dwc2InterruptNextSof(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    Extension->InterruptMask |= DWC2_GINT_SOF;
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
}

static VOID
NTAPI
Dwc2EnableInterrupts(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
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
NTAPI
Dwc2PollController(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;
    ULONG PortValue;
    ULONG Connected;

    if (!Extension->Started)
        return;

    PortValue = Dwc2ReadRegister(Extension, DWC2_HPRT0);
    Connected = !!(PortValue & DWC2_HPRT_CONNSTS);
    if ((PortValue & DWC2_HPRT_CHANGE_MASK) || Connected != Extension->LastConnectStatus)
        Dwc2RegPacket.UsbPortInvalidateRootHub(Extension);

    Dwc2InterruptDpc(Extension, FALSE);
}

static VOID
NTAPI
Dwc2SetEndpointDataToggle(
    _In_ PVOID MiniportExtension,
    _In_ PVOID MiniportEndpoint,
    _In_ ULONG DataToggle)
{
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;

    UNREFERENCED_PARAMETER(MiniportExtension);
    Endpoint->DataToggle = DataToggle ? 1 : 0;
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
    PDWC2_ENDPOINT Endpoint = MiniportEndpoint;

    UNREFERENCED_PARAMETER(MiniportExtension);
    Endpoint->Status = Status;
    if (Status == USBPORT_ENDPOINT_RUN)
        Endpoint->DataToggle = 0;
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

    DPRINT1("[DWC2] root status raw=%08lx status/change=%08lx speed=%lu\n", PortValue, PortStatus->AsUlong32, Speed);
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

    Extension->RootHubIrqEnabled = FALSE;
    Extension->InterruptMask &= ~DWC2_GINT_PRTINT;
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
}

static VOID
NTAPI
Dwc2RootHubEnableIrq(
    _In_ PVOID MiniportExtension)
{
    PDWC2_EXTENSION Extension = MiniportExtension;

    Extension->RootHubIrqEnabled = TRUE;
    Extension->InterruptMask |= DWC2_GINT_PRTINT;
    Dwc2WriteRegister(Extension, DWC2_GINTMSK, Extension->InterruptMask);
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
