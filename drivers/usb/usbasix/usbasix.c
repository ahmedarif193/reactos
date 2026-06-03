/*
 * PROJECT:     ReactOS ASIX AX88772B USB Ethernet Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Clean-room NDIS miniport for ASIX AX88772B USB 2.0 Ethernet
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * based on the ASIX AX88772/AX88772B public USB command/register descriptions and USB/NDIS programming contracts.
 */

#include <ntddk.h>
#include <ndis.h>
#include <usbdi.h>
#include <usbbusif.h>
#include <usbdlib.h>
#include <debug.h>

#define USBASIX_TAG 'XASU'

#define ASIX_VID                    0x0B95
#define ASIX_AX88772B_PID           0x772B

#define ASIX_CMD_SOFT_MII           0x06
#define ASIX_CMD_READ_PHY           0x07
#define ASIX_CMD_HARD_MII           0x0A
#define ASIX_CMD_READ_RX_CTL        0x0F
#define ASIX_CMD_WRITE_RX_CTL       0x10
#define ASIX_CMD_READ_NODE_ID       0x13
#define ASIX_CMD_WRITE_MULTICAST    0x16
#define ASIX_CMD_READ_PHY_ADDR      0x19
#define ASIX_CMD_READ_MEDIUM_STATUS 0x1A
#define ASIX_CMD_WRITE_MEDIUM_MODE  0x1B
#define ASIX_CMD_WRITE_SW_RESET     0x20
#define ASIX_CMD_WRITE_PHY_SELECT   0x22

#define ASIX_RX_CTL_PRO             0x0001
#define ASIX_RX_CTL_AMALL           0x0002
#define ASIX_RX_CTL_AB              0x0008
#define ASIX_RX_CTL_AM              0x0010
#define ASIX_RX_CTL_AP              0x0020
#define ASIX_RX_CTL_SO              0x0080
#define ASIX_RX_CTL_MFB_16384       0x0300

#define ASIX_MEDIUM_FD              0x0002
#define ASIX_MEDIUM_AC              0x0004
#define ASIX_MEDIUM_RFC             0x0010
#define ASIX_MEDIUM_TFC             0x0020
#define ASIX_MEDIUM_RE              0x0100
#define ASIX_MEDIUM_PS              0x0200
#define ASIX_MEDIUM_100_FULL        (ASIX_MEDIUM_PS | ASIX_MEDIUM_RE | \
                                     ASIX_MEDIUM_TFC | ASIX_MEDIUM_RFC | \
                                     ASIX_MEDIUM_AC | ASIX_MEDIUM_FD)

#define ASIX_SW_RESET_RR            0x01
#define ASIX_SW_RESET_RT            0x02
#define ASIX_SW_RESET_PRL           0x08
#define ASIX_SW_RESET_IPRL          0x20

#define ASIX_PHY_SELECT_INTERNAL    0x01
#define ASIX_INTERNAL_PHY_ID        0x10

#define ASIX_INTERRUPT_LENGTH       8
#define ASIX_RX_BUFFER_SIZE         16384
#define ASIX_MAX_ETHERNET_FRAME     1518
#define ASIX_TX_HEADER_SIZE         4
#define ASIX_ETH_ADDRESS_LENGTH     6
#define ASIX_ETH_HEADER_LENGTH      14
#define ASIX_ETH_MTU                1500
#define ASIX_LINK_SPEED_100MBPS     100000000ULL
#define ASIX_LINK_SPEED_10MBPS      10000000ULL

#define MII_BMSR_LINK_STATUS        0x0004
#define MII_BMSR_AN_COMPLETE        0x0020
#define MII_ANLPAR_10_HALF          0x0020
#define MII_ANLPAR_10_FULL          0x0040
#define MII_ANLPAR_100_HALF         0x0080
#define MII_ANLPAR_100_FULL         0x0100

typedef struct _ASIX_ENDPOINT
{
    UCHAR EndpointAddress;
    USBD_PIPE_TYPE PipeType;
    USBD_PIPE_HANDLE PipeHandle;
    USHORT MaximumPacketSize;
} ASIX_ENDPOINT, *PASIX_ENDPOINT;

typedef struct _ASIX_ADAPTER
{
    NDIS_HANDLE MiniportAdapterHandle;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;
    USB_DEVICE_DESCRIPTOR *DeviceDescriptor;
    USB_CONFIGURATION_DESCRIPTOR *ConfigurationDescriptor;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;

    ASIX_ENDPOINT BulkIn;
    ASIX_ENDPOINT BulkOut;
    ASIX_ENDPOINT InterruptIn;

    UCHAR PermanentMacAddress[ASIX_ETH_ADDRESS_LENGTH];
    UCHAR CurrentMacAddress[ASIX_ETH_ADDRESS_LENGTH];
    UCHAR PhyId;
    ULONG PacketFilter;
    ULONG64 LinkSpeed;
    NDIS_MEDIA_CONNECT_STATE MediaState;

    NDIS_HANDLE RxNblPool;
    PUCHAR RxBuffer;
    PUCHAR TxBuffer;
    PUCHAR InterruptBuffer;

    URB RxUrb;
    URB TxUrb;
    URB InterruptUrb;
    PIRP RxIrp;
    PIRP TxIrp;
    PIRP InterruptIrp;
    PNET_BUFFER_LIST TxNbl;

    KDPC RxResubmitDpc;
    KDPC InterruptResubmitDpc;
    NDIS_SPIN_LOCK TxLock;
    KEVENT RemoveEvent;
    LONG PendingIoCount;
    LONG RxSubmitted;
    LONG InterruptSubmitted;
    BOOLEAN TxBusy;
    BOOLEAN Halting;
    BOOLEAN Paused;

    ULONG XmitOk;
    ULONG RcvOk;
    ULONG XmitError;
    ULONG RcvError;
    ULONG RcvNoBuffer;
} ASIX_ADAPTER, *PASIX_ADAPTER;

static NDIS_HANDLE g_AsixMiniportDriverHandle;

static IO_COMPLETION_ROUTINE AsixRxComplete;
static IO_COMPLETION_ROUTINE AsixTxComplete;
static IO_COMPLETION_ROUTINE AsixInterruptComplete;

static VOID NTAPI AsixRxResubmitDpc(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2);
static VOID NTAPI AsixInterruptResubmitDpc(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2);

static
PVOID
AsixAllocate(
    _In_ SIZE_T Size)
{
    PVOID Buffer;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, USBASIX_TAG);
    if (Buffer)
    {
        RtlZeroMemory(Buffer, Size);
    }

    return Buffer;
}

static
VOID
AsixFree(
    _In_opt_ PVOID Buffer)
{
    if (Buffer)
    {
        ExFreePoolWithTag(Buffer, USBASIX_TAG);
    }
}

static
VOID
AsixIncrementPendingIo(
    _In_ PASIX_ADAPTER Adapter)
{
    if (InterlockedIncrement(&Adapter->PendingIoCount) == 1)
    {
        KeClearEvent(&Adapter->RemoveEvent);
    }
}

static
VOID
AsixDecrementPendingIo(
    _In_ PASIX_ADAPTER Adapter)
{
    if (InterlockedDecrement(&Adapter->PendingIoCount) == 0)
    {
        KeSetEvent(&Adapter->RemoveEvent, IO_NO_INCREMENT, FALSE);
    }
}

static
NTSTATUS
AsixSubmitUrbSync(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PURB Urb)
{
    KEVENT Event;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(IOCTL_INTERNAL_USB_SUBMIT_URB,
                                        DeviceObject,
                                        NULL,
                                        0,
                                        NULL,
                                        0,
                                        TRUE,
                                        &Event,
                                        &IoStatus);
    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->Parameters.Others.Argument1 = Urb;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

static
NTSTATUS
AsixSubmitUrbAsync(
    _In_ PASIX_ADAPTER Adapter,
    _Inout_ PURB Urb,
    _In_ PIO_COMPLETION_ROUTINE CompletionRoutine,
    _Out_ PIRP *IrpSlot)
{
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    *IrpSlot = NULL;

    Irp = IoAllocateIrp(Adapter->LowerDeviceObject->StackSize, FALSE);
    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = Urb;

    IoSetCompletionRoutine(Irp, CompletionRoutine, Adapter, TRUE, TRUE, TRUE);

    AsixIncrementPendingIo(Adapter);
    *IrpSlot = Irp;

    Status = IoCallDriver(Adapter->LowerDeviceObject, Irp);
    if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
    {
        if (*IrpSlot == Irp)
        {
            *IrpSlot = NULL;
            IoFreeIrp(Irp);
            AsixDecrementPendingIo(Adapter);
        }
    }

    return Status;
}

static
NTSTATUS
AsixVendorCommand(
    _In_ PASIX_ADAPTER Adapter,
    _In_ UCHAR Request,
    _In_ USHORT Value,
    _In_ USHORT Index,
    _Inout_updates_bytes_opt_(Length) PVOID Buffer,
    _In_ USHORT Length,
    _In_ BOOLEAN Read)
{
    URB Urb;
    NTSTATUS Status;

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.UrbControlVendorClassRequest.Hdr.Length = sizeof(struct _URB_CONTROL_VENDOR_OR_CLASS_REQUEST);
    Urb.UrbControlVendorClassRequest.Hdr.Function = URB_FUNCTION_VENDOR_DEVICE;
    Urb.UrbControlVendorClassRequest.TransferFlags = Read ?
        (USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK) :
        USBD_TRANSFER_DIRECTION_OUT;
    Urb.UrbControlVendorClassRequest.Request = Request;
    Urb.UrbControlVendorClassRequest.Value = Value;
    Urb.UrbControlVendorClassRequest.Index = Index;
    Urb.UrbControlVendorClassRequest.TransferBuffer = Buffer;
    Urb.UrbControlVendorClassRequest.TransferBufferLength = Length;

    Status = AsixSubmitUrbSync(Adapter->LowerDeviceObject, &Urb);
    if (NT_SUCCESS(Status) && Read &&
        Urb.UrbControlVendorClassRequest.TransferBufferLength != Length)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
    }

    return Status;
}

static
NTSTATUS
AsixWriteCommand(
    _In_ PASIX_ADAPTER Adapter,
    _In_ UCHAR Request,
    _In_ USHORT Value,
    _In_ USHORT Index)
{
    return AsixVendorCommand(Adapter, Request, Value, Index, NULL, 0, FALSE);
}

static
NTSTATUS
AsixReadPhy(
    _In_ PASIX_ADAPTER Adapter,
    _In_ UCHAR Register,
    _Out_ PUSHORT Value)
{
    UCHAR Buffer[2];
    NTSTATUS Status;

    Status = AsixWriteCommand(Adapter, ASIX_CMD_SOFT_MII, 0, 0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = AsixVendorCommand(Adapter,
                               ASIX_CMD_READ_PHY,
                               Adapter->PhyId,
                               Register,
                               Buffer,
                               sizeof(Buffer),
                               TRUE);

    (VOID)AsixWriteCommand(Adapter, ASIX_CMD_HARD_MII, 0, 0);

    if (NT_SUCCESS(Status))
    {
        *Value = Buffer[0] | (Buffer[1] << 8);
    }

    return Status;
}

static
VOID
AsixUpdateLinkFromPhy(
    _In_ PASIX_ADAPTER Adapter)
{
    USHORT Bmsr;
    USHORT Anlpar;
    NDIS_MEDIA_CONNECT_STATE NewState;
    ULONG64 NewSpeed;

    NewState = MediaConnectStateDisconnected;
    NewSpeed = ASIX_LINK_SPEED_100MBPS;

    if (NT_SUCCESS(AsixReadPhy(Adapter, 1, &Bmsr)) && (Bmsr & MII_BMSR_LINK_STATUS))
    {
        NewState = MediaConnectStateConnected;

        if (NT_SUCCESS(AsixReadPhy(Adapter, 5, &Anlpar)) &&
            (Bmsr & MII_BMSR_AN_COMPLETE))
        {
            if (Anlpar & (MII_ANLPAR_100_FULL | MII_ANLPAR_100_HALF))
            {
                NewSpeed = ASIX_LINK_SPEED_100MBPS;
            }
            else if (Anlpar & (MII_ANLPAR_10_FULL | MII_ANLPAR_10_HALF))
            {
                NewSpeed = ASIX_LINK_SPEED_10MBPS;
            }
        }
    }

    Adapter->MediaState = NewState;
    Adapter->LinkSpeed = NewSpeed;
}

static
VOID
AsixIndicateLinkState(
    _In_ PASIX_ADAPTER Adapter)
{
    NDIS_STATUS_INDICATION Indication;
    NDIS_LINK_STATE LinkState;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(LinkState);
    LinkState.MediaConnectState = Adapter->MediaState;
    LinkState.MediaDuplexState = MediaDuplexStateFull;
    LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
    LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
    LinkState.PauseFunctions = NdisPauseFunctionsSendAndReceive;
    LinkState.AutoNegotiationFlags =
        NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED |
        NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED |
        NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;

    RtlZeroMemory(&Indication, sizeof(Indication));
    Indication.Header.Type = NDIS_OBJECT_TYPE_STATUS_INDICATION;
    Indication.Header.Revision = NDIS_STATUS_INDICATION_REVISION_1;
    Indication.Header.Size = sizeof(Indication);
    Indication.SourceHandle = Adapter->MiniportAdapterHandle;
    Indication.StatusCode = NDIS_STATUS_LINK_STATE;
    Indication.StatusBuffer = &LinkState;
    Indication.StatusBufferSize = sizeof(LinkState);

    NdisMIndicateStatusEx(Adapter->MiniportAdapterHandle, &Indication);
}

static
NTSTATUS
AsixGetDescriptor(
    _In_ PASIX_ADAPTER Adapter,
    _In_ UCHAR DescriptorType,
    _Outptr_result_bytebuffer_(*DescriptorLength) PVOID *Descriptor,
    _Inout_ PULONG DescriptorLength)
{
    PURB Urb;
    PVOID Buffer;
    NTSTATUS Status;

    Buffer = AsixAllocate(*DescriptorLength);
    if (!Buffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Urb = AsixAllocate(sizeof(URB));
    if (!Urb)
    {
        AsixFree(Buffer);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    UsbBuildGetDescriptorRequest(Urb,
                                 sizeof(struct _URB_CONTROL_DESCRIPTOR_REQUEST),
                                 DescriptorType,
                                 0,
                                 0,
                                 Buffer,
                                 NULL,
                                 *DescriptorLength,
                                 NULL);

    Status = AsixSubmitUrbSync(Adapter->LowerDeviceObject, Urb);
    if (NT_SUCCESS(Status))
    {
        *Descriptor = Buffer;
        *DescriptorLength = Urb->UrbControlDescriptorRequest.TransferBufferLength;
    }
    else
    {
        AsixFree(Buffer);
        *Descriptor = NULL;
    }

    AsixFree(Urb);
    return Status;
}

static
NTSTATUS
AsixReadDescriptors(
    _In_ PASIX_ADAPTER Adapter)
{
    USB_CONFIGURATION_DESCRIPTOR *Config;
    ULONG Length;
    NTSTATUS Status;

    Length = sizeof(USB_DEVICE_DESCRIPTOR);
    Status = AsixGetDescriptor(Adapter,
                               USB_DEVICE_DESCRIPTOR_TYPE,
                               (PVOID *)&Adapter->DeviceDescriptor,
                               &Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Adapter->DeviceDescriptor->idVendor != ASIX_VID ||
        Adapter->DeviceDescriptor->idProduct != ASIX_AX88772B_PID)
    {
        return STATUS_NOT_SUPPORTED;
    }

    Length = sizeof(USB_CONFIGURATION_DESCRIPTOR);
    Status = AsixGetDescriptor(Adapter,
                               USB_CONFIGURATION_DESCRIPTOR_TYPE,
                               (PVOID *)&Config,
                               &Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Length = Config->wTotalLength;
    AsixFree(Config);

    Status = AsixGetDescriptor(Adapter,
                               USB_CONFIGURATION_DESCRIPTOR_TYPE,
                               (PVOID *)&Adapter->ConfigurationDescriptor,
                               &Length);

    return Status;
}

static
VOID
AsixRecordEndpoint(
    _Inout_ PASIX_ADAPTER Adapter,
    _In_ PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor)
{
    UCHAR Attributes;
    PASIX_ENDPOINT Endpoint;

    Attributes = EndpointDescriptor->bmAttributes & USB_ENDPOINT_TYPE_MASK;
    Endpoint = NULL;

    if (Attributes == USB_ENDPOINT_TYPE_BULK)
    {
        Endpoint = USB_ENDPOINT_DIRECTION_IN(EndpointDescriptor->bEndpointAddress) ?
            &Adapter->BulkIn : &Adapter->BulkOut;
    }
    else if (Attributes == USB_ENDPOINT_TYPE_INTERRUPT &&
             USB_ENDPOINT_DIRECTION_IN(EndpointDescriptor->bEndpointAddress))
    {
        Endpoint = &Adapter->InterruptIn;
    }

    if (Endpoint)
    {
        Endpoint->EndpointAddress = EndpointDescriptor->bEndpointAddress;
        Endpoint->MaximumPacketSize = EndpointDescriptor->wMaxPacketSize;
    }
}

static
NTSTATUS
AsixFindEndpoints(
    _Inout_ PASIX_ADAPTER Adapter,
    _Out_ PUSB_INTERFACE_DESCRIPTOR *InterfaceDescriptor)
{
    PUCHAR Current;
    PUCHAR End;

    *InterfaceDescriptor = USBD_ParseConfigurationDescriptorEx(Adapter->ConfigurationDescriptor,
                                                               Adapter->ConfigurationDescriptor,
                                                               -1,
                                                               -1,
                                                               -1,
                                                               -1,
                                                               -1);
    if (!*InterfaceDescriptor)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    Current = (PUCHAR)*InterfaceDescriptor + (*InterfaceDescriptor)->bLength;
    End = (PUCHAR)Adapter->ConfigurationDescriptor +
          Adapter->ConfigurationDescriptor->wTotalLength;

    while (Current + sizeof(USB_COMMON_DESCRIPTOR) <= End)
    {
        PUSB_COMMON_DESCRIPTOR Common;

        Common = (PUSB_COMMON_DESCRIPTOR)Current;
        if (Common->bLength == 0 || Current + Common->bLength > End)
        {
            break;
        }

        if (Common->bDescriptorType == USB_INTERFACE_DESCRIPTOR_TYPE &&
            Current != (PUCHAR)*InterfaceDescriptor)
        {
            break;
        }

        if (Common->bDescriptorType == USB_ENDPOINT_DESCRIPTOR_TYPE &&
            Common->bLength >= sizeof(USB_ENDPOINT_DESCRIPTOR))
        {
            AsixRecordEndpoint(Adapter, (PUSB_ENDPOINT_DESCRIPTOR)Common);
        }

        Current += Common->bLength;
    }

    if (!Adapter->BulkIn.EndpointAddress || !Adapter->BulkOut.EndpointAddress)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
AsixSelectConfiguration(
    _Inout_ PASIX_ADAPTER Adapter)
{
    USB_INTERFACE_DESCRIPTOR *InterfaceDescriptor;
    USBD_INTERFACE_LIST_ENTRY InterfaceList[2];
    PURB Urb;
    NTSTATUS Status;
    ULONG i;

    Status = AsixFindEndpoints(Adapter, &InterfaceDescriptor);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(InterfaceList, sizeof(InterfaceList));
    InterfaceList[0].InterfaceDescriptor = InterfaceDescriptor;

    Urb = USBD_CreateConfigurationRequestEx(Adapter->ConfigurationDescriptor,
                                            InterfaceList);
    if (!Urb)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = AsixSubmitUrbSync(Adapter->LowerDeviceObject, Urb);
    if (!NT_SUCCESS(Status))
    {
        ExFreePool(Urb);
        return Status;
    }

    Adapter->ConfigurationHandle = Urb->UrbSelectConfiguration.ConfigurationHandle;

    if (!InterfaceList[0].Interface)
    {
        ExFreePool(Urb);
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (i = 0; i < InterfaceList[0].Interface->NumberOfPipes; i++)
    {
        PUSBD_PIPE_INFORMATION Pipe;

        Pipe = &InterfaceList[0].Interface->Pipes[i];
        if (Pipe->EndpointAddress == Adapter->BulkIn.EndpointAddress)
        {
            Adapter->BulkIn.PipeHandle = Pipe->PipeHandle;
            Adapter->BulkIn.PipeType = Pipe->PipeType;
            Adapter->BulkIn.MaximumPacketSize = Pipe->MaximumPacketSize;
        }
        else if (Pipe->EndpointAddress == Adapter->BulkOut.EndpointAddress)
        {
            Adapter->BulkOut.PipeHandle = Pipe->PipeHandle;
            Adapter->BulkOut.PipeType = Pipe->PipeType;
            Adapter->BulkOut.MaximumPacketSize = Pipe->MaximumPacketSize;
        }
        else if (Pipe->EndpointAddress == Adapter->InterruptIn.EndpointAddress)
        {
            Adapter->InterruptIn.PipeHandle = Pipe->PipeHandle;
            Adapter->InterruptIn.PipeType = Pipe->PipeType;
            Adapter->InterruptIn.MaximumPacketSize = Pipe->MaximumPacketSize;
        }
    }

    ExFreePool(Urb);

    if (!Adapter->BulkIn.PipeHandle || !Adapter->BulkOut.PipeHandle)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
AsixGetMacAddress(
    _Inout_ PASIX_ADAPTER Adapter)
{
    NTSTATUS Status;

    Status = AsixVendorCommand(Adapter,
                               ASIX_CMD_READ_NODE_ID,
                               0,
                               0,
                               Adapter->PermanentMacAddress,
                               ASIX_ETH_ADDRESS_LENGTH,
                               TRUE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if ((Adapter->PermanentMacAddress[0] & 0x01) ||
        RtlCompareMemory(Adapter->PermanentMacAddress,
                         "\x00\x00\x00\x00\x00\x00",
                         ASIX_ETH_ADDRESS_LENGTH) == ASIX_ETH_ADDRESS_LENGTH)
    {
        return STATUS_DEVICE_DATA_ERROR;
    }

    RtlCopyMemory(Adapter->CurrentMacAddress,
                  Adapter->PermanentMacAddress,
                  ASIX_ETH_ADDRESS_LENGTH);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AsixReadPhyId(
    _Inout_ PASIX_ADAPTER Adapter)
{
    UCHAR Buffer[2];
    NTSTATUS Status;

    Adapter->PhyId = ASIX_INTERNAL_PHY_ID;

    Status = AsixVendorCommand(Adapter,
                               ASIX_CMD_READ_PHY_ADDR,
                               0,
                               0,
                               Buffer,
                               sizeof(Buffer),
                               TRUE);
    if (NT_SUCCESS(Status) && (Buffer[0] & 0x1F) != 0)
    {
        Adapter->PhyId = Buffer[0] & 0x1F;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
AsixSetRxControl(
    _Inout_ PASIX_ADAPTER Adapter)
{
    USHORT RxCtl;

    RxCtl = ASIX_RX_CTL_SO | ASIX_RX_CTL_AB | ASIX_RX_CTL_MFB_16384;

    if (Adapter->PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
    {
        RxCtl |= ASIX_RX_CTL_PRO;
    }

    if (Adapter->PacketFilter & NDIS_PACKET_TYPE_ALL_MULTICAST)
    {
        RxCtl |= ASIX_RX_CTL_AMALL | ASIX_RX_CTL_AM;
    }
    else if (Adapter->PacketFilter & NDIS_PACKET_TYPE_MULTICAST)
    {
        RxCtl |= ASIX_RX_CTL_AM;
    }

    return AsixWriteCommand(Adapter, ASIX_CMD_WRITE_RX_CTL, RxCtl, 0);
}

static
NTSTATUS
AsixInitializeHardware(
    _Inout_ PASIX_ADAPTER Adapter)
{
    NTSTATUS Status;
    UCHAR MulticastFilter[8];

    Status = AsixWriteCommand(Adapter, ASIX_CMD_WRITE_SW_RESET, 0, 0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    NdisMSleep(150000);

    Status = AsixWriteCommand(Adapter,
                              ASIX_CMD_WRITE_SW_RESET,
                              ASIX_SW_RESET_PRL | ASIX_SW_RESET_IPRL,
                              0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    NdisMSleep(150000);

    Status = AsixWriteCommand(Adapter,
                              ASIX_CMD_WRITE_SW_RESET,
                              ASIX_SW_RESET_PRL | ASIX_SW_RESET_IPRL |
                              ASIX_SW_RESET_RR | ASIX_SW_RESET_RT,
                              0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = AsixWriteCommand(Adapter,
                              ASIX_CMD_WRITE_PHY_SELECT,
                              ASIX_PHY_SELECT_INTERNAL,
                              0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = AsixWriteCommand(Adapter,
                              ASIX_CMD_WRITE_MEDIUM_MODE,
                              ASIX_MEDIUM_100_FULL,
                              0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RtlZeroMemory(MulticastFilter, sizeof(MulticastFilter));
    Status = AsixVendorCommand(Adapter,
                               ASIX_CMD_WRITE_MULTICAST,
                               0,
                               0,
                               MulticastFilter,
                               sizeof(MulticastFilter),
                               FALSE);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return AsixSetRxControl(Adapter);
}

static
NDIS_STATUS
AsixSetRegistrationAttributes(
    _In_ PASIX_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Attributes;

    RtlZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    Attributes.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    Attributes.Header.Size = sizeof(Attributes);
    Attributes.MiniportAdapterContext = Adapter;
    Attributes.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM |
                                NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK |
                                NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND;
    Attributes.CheckForHangTimeInSeconds = 4;
    Attributes.InterfaceType = NdisInterfaceInternal;

    return NdisMSetMiniportAttributes(
        Adapter->MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Attributes);
}

static
NDIS_STATUS
AsixSetGeneralAttributes(
    _In_ PASIX_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES Attributes;

    RtlZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    Attributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    Attributes.Header.Size = sizeof(Attributes);
    Attributes.MediaType = NdisMedium802_3;
    Attributes.PhysicalMediumType = NdisPhysicalMedium802_3;
    Attributes.MtuSize = ASIX_ETH_MTU;
    Attributes.MaxXmitLinkSpeed = ASIX_LINK_SPEED_100MBPS;
    Attributes.XmitLinkSpeed = Adapter->LinkSpeed;
    Attributes.MaxRcvLinkSpeed = ASIX_LINK_SPEED_100MBPS;
    Attributes.RcvLinkSpeed = Adapter->LinkSpeed;
    Attributes.MediaConnectState = Adapter->MediaState;
    Attributes.MediaDuplexState = MediaDuplexStateFull;
    Attributes.LookaheadSize = ASIX_ETH_MTU;
    Attributes.MacOptions = NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                            NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                            NDIS_MAC_OPTION_NO_LOOPBACK |
                            NDIS_MAC_OPTION_FULL_DUPLEX;
    Attributes.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                        NDIS_PACKET_TYPE_MULTICAST |
                                        NDIS_PACKET_TYPE_ALL_MULTICAST |
                                        NDIS_PACKET_TYPE_BROADCAST |
                                        NDIS_PACKET_TYPE_PROMISCUOUS;
    Attributes.MaxMulticastListSize = 64;
    Attributes.MacAddressLength = ASIX_ETH_ADDRESS_LENGTH;
    RtlCopyMemory(Attributes.PermanentMacAddress,
                  Adapter->PermanentMacAddress,
                  ASIX_ETH_ADDRESS_LENGTH);
    RtlCopyMemory(Attributes.CurrentMacAddress,
                  Adapter->CurrentMacAddress,
                  ASIX_ETH_ADDRESS_LENGTH);
    Attributes.AccessType = NET_IF_ACCESS_BROADCAST;
    Attributes.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    Attributes.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    Attributes.IfType = IF_TYPE_ETHERNET_CSMACD;
    Attributes.IfConnectorPresent = TRUE;
    Attributes.SupportedPauseFunctions = NdisPauseFunctionsSendAndReceive;
    Attributes.SupportedStatistics = NDIS_STATISTICS_XMIT_OK_SUPPORTED |
                                     NDIS_STATISTICS_RCV_OK_SUPPORTED |
                                     NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
                                     NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
                                     NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED;
    Attributes.AutoNegotiationFlags =
        NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED |
        NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED |
        NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;

    return NdisMSetMiniportAttributes(
        Adapter->MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&Attributes);
}

static
NDIS_STATUS
AsixAllocateRxNblPool(
    _Inout_ PASIX_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = sizeof(PoolParameters);
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = USBASIX_TAG;

    Adapter->RxNblPool = NdisAllocateNetBufferListPool(
        Adapter->MiniportAdapterHandle,
        &PoolParameters);

    return Adapter->RxNblPool ? NDIS_STATUS_SUCCESS : NDIS_STATUS_RESOURCES;
}

static
NTSTATUS
AsixStartBulkRead(
    _Inout_ PASIX_ADAPTER Adapter)
{
    NTSTATUS Status;

    if (Adapter->Halting || Adapter->Paused || !Adapter->BulkIn.PipeHandle)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    if (InterlockedCompareExchange(&Adapter->RxSubmitted, 1, 0) != 0)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&Adapter->RxUrb, sizeof(Adapter->RxUrb));
    UsbBuildInterruptOrBulkTransferRequest(&Adapter->RxUrb,
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           Adapter->BulkIn.PipeHandle,
                                           Adapter->RxBuffer,
                                           NULL,
                                           ASIX_RX_BUFFER_SIZE,
                                           USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                                           NULL);

    Status = AsixSubmitUrbAsync(Adapter, &Adapter->RxUrb, AsixRxComplete, &Adapter->RxIrp);
    if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
    {
        InterlockedExchange(&Adapter->RxSubmitted, 0);
    }

    return Status;
}

static
NTSTATUS
AsixStartInterruptRead(
    _Inout_ PASIX_ADAPTER Adapter)
{
    NTSTATUS Status;

    if (Adapter->Halting || Adapter->Paused || !Adapter->InterruptIn.PipeHandle || !Adapter->InterruptBuffer)
    {
        return STATUS_DEVICE_NOT_READY;
    }

    if (InterlockedCompareExchange(&Adapter->InterruptSubmitted, 1, 0) != 0)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&Adapter->InterruptUrb, sizeof(Adapter->InterruptUrb));
    UsbBuildInterruptOrBulkTransferRequest(&Adapter->InterruptUrb,
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           Adapter->InterruptIn.PipeHandle,
                                           Adapter->InterruptBuffer,
                                           NULL,
                                           ASIX_INTERRUPT_LENGTH,
                                           USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                                           NULL);

    Status = AsixSubmitUrbAsync(Adapter, &Adapter->InterruptUrb, AsixInterruptComplete, &Adapter->InterruptIrp);
    if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
    {
        InterlockedExchange(&Adapter->InterruptSubmitted, 0);
    }

    return Status;
}

static
VOID
AsixCancelPendingIo(
    _Inout_ PASIX_ADAPTER Adapter,
    _In_ BOOLEAN CancelTransmit)
{
    if (Adapter->RxIrp)
    {
        IoCancelIrp(Adapter->RxIrp);
    }

    if (CancelTransmit && Adapter->TxIrp)
    {
        IoCancelIrp(Adapter->TxIrp);
    }

    if (Adapter->InterruptIrp)
    {
        IoCancelIrp(Adapter->InterruptIrp);
    }

    KeRemoveQueueDpc(&Adapter->RxResubmitDpc);
    KeRemoveQueueDpc(&Adapter->InterruptResubmitDpc);
}

static
VOID
AsixIndicateFrame(
    _In_ PASIX_ADAPTER Adapter,
    _In_reads_bytes_(FrameLength) PUCHAR Frame,
    _In_ ULONG FrameLength)
{
    PUCHAR FrameCopy;
    PMDL Mdl;
    PNET_BUFFER_LIST Nbl;
    ULONG ReceiveFlags;

    if (FrameLength < ASIX_ETH_HEADER_LENGTH || FrameLength > ASIX_MAX_ETHERNET_FRAME)
    {
        Adapter->RcvError++;
        return;
    }

    FrameCopy = AsixAllocate(FrameLength);
    if (!FrameCopy)
    {
        Adapter->RcvNoBuffer++;
        return;
    }

    RtlCopyMemory(FrameCopy, Frame, FrameLength);

    Mdl = IoAllocateMdl(FrameCopy, FrameLength, FALSE, FALSE, NULL);
    if (!Mdl)
    {
        Adapter->RcvNoBuffer++;
        AsixFree(FrameCopy);
        return;
    }

    MmBuildMdlForNonPagedPool(Mdl);

    Nbl = NdisAllocateNetBufferAndNetBufferList(Adapter->RxNblPool,
                                                0,
                                                0,
                                                Mdl,
                                                0,
                                                FrameLength);
    if (!Nbl)
    {
        Adapter->RcvNoBuffer++;
        IoFreeMdl(Mdl);
        AsixFree(FrameCopy);
        return;
    }

    ReceiveFlags = NDIS_RECEIVE_FLAGS_RESOURCES;
    if (KeGetCurrentIrql() == DISPATCH_LEVEL)
    {
        ReceiveFlags |= NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL;
    }

    NdisMIndicateReceiveNetBufferLists(Adapter->MiniportAdapterHandle,
                                       Nbl,
                                       NDIS_DEFAULT_PORT_NUMBER,
                                       1,
                                       ReceiveFlags);

    Adapter->RcvOk++;
    NdisFreeNetBufferList(Nbl);
    IoFreeMdl(Mdl);
    AsixFree(FrameCopy);
}

static
VOID
AsixProcessReceiveBuffer(
    _In_ PASIX_ADAPTER Adapter,
    _In_reads_bytes_(TransferLength) PUCHAR Buffer,
    _In_ ULONG TransferLength)
{
    ULONG Offset;

    Offset = 0;
    while (Offset + ASIX_TX_HEADER_SIZE <= TransferLength)
    {
        USHORT FrameLength;
        USHORT FrameLengthCheck;
        ULONG PaddedLength;

        FrameLength = Buffer[Offset] | (Buffer[Offset + 1] << 8);
        FrameLengthCheck = Buffer[Offset + 2] | (Buffer[Offset + 3] << 8);

        if ((USHORT)(FrameLength ^ FrameLengthCheck) != 0xFFFF)
        {
            Adapter->RcvError++;
            break;
        }

        if (FrameLength > ASIX_MAX_ETHERNET_FRAME ||
            Offset + ASIX_TX_HEADER_SIZE + FrameLength > TransferLength)
        {
            Adapter->RcvError++;
            break;
        }

        AsixIndicateFrame(Adapter,
                          Buffer + Offset + ASIX_TX_HEADER_SIZE,
                          FrameLength);

        PaddedLength = (FrameLength + 1) & ~1UL;
        Offset += ASIX_TX_HEADER_SIZE + PaddedLength;
    }
}

static
NTSTATUS
NTAPI
AsixRxComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PASIX_ADAPTER Adapter;
    ULONG TransferLength;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    Adapter = (PASIX_ADAPTER)Context;
    Status = Irp->IoStatus.Status;
    TransferLength = Adapter->RxUrb.UrbBulkOrInterruptTransfer.TransferBufferLength;

    Adapter->RxIrp = NULL;
    InterlockedExchange(&Adapter->RxSubmitted, 0);

    if (NT_SUCCESS(Status) && TransferLength != 0 && !Adapter->Paused)
    {
        AsixProcessReceiveBuffer(Adapter, Adapter->RxBuffer, TransferLength);
    }
    else if (!Adapter->Halting && !Adapter->Paused)
    {
        Adapter->RcvError++;
    }

    IoFreeIrp(Irp);
    AsixDecrementPendingIo(Adapter);

    if (!Adapter->Halting && !Adapter->Paused)
    {
        KeInsertQueueDpc(&Adapter->RxResubmitDpc, NULL, NULL);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
AsixInterruptComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PASIX_ADAPTER Adapter;
    NDIS_MEDIA_CONNECT_STATE OldState;
    ULONG TransferLength;

    UNREFERENCED_PARAMETER(DeviceObject);

    Adapter = (PASIX_ADAPTER)Context;
    TransferLength = Adapter->InterruptUrb.UrbBulkOrInterruptTransfer.TransferBufferLength;

    Adapter->InterruptIrp = NULL;
    InterlockedExchange(&Adapter->InterruptSubmitted, 0);

    if (NT_SUCCESS(Irp->IoStatus.Status) && TransferLength >= ASIX_INTERRUPT_LENGTH)
    {
        OldState = Adapter->MediaState;
        Adapter->MediaState = (Adapter->InterruptBuffer[2] & 0x01) ?
            MediaConnectStateConnected : MediaConnectStateDisconnected;
        if (Adapter->MediaState == MediaConnectStateConnected)
        {
            AsixUpdateLinkFromPhy(Adapter);
        }

        if (OldState != Adapter->MediaState)
        {
            AsixIndicateLinkState(Adapter);
        }
    }

    IoFreeIrp(Irp);
    AsixDecrementPendingIo(Adapter);

    if (!Adapter->Halting && !Adapter->Paused)
    {
        KeInsertQueueDpc(&Adapter->InterruptResubmitDpc, NULL, NULL);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
NTAPI
AsixTxComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PASIX_ADAPTER Adapter;
    PNET_BUFFER_LIST Nbl;
    ULONG CompleteFlags;

    UNREFERENCED_PARAMETER(DeviceObject);

    Adapter = (PASIX_ADAPTER)Context;
    Nbl = Adapter->TxNbl;
    Adapter->TxNbl = NULL;
    Adapter->TxIrp = NULL;

    if (Nbl)
    {
        if (NT_SUCCESS(Irp->IoStatus.Status))
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
            Adapter->XmitOk++;
        }
        else
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_FAILURE;
            Adapter->XmitError++;
        }

        CompleteFlags = (KeGetCurrentIrql() == DISPATCH_LEVEL) ?
            NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0;
        NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                        Nbl,
                                        CompleteFlags);
    }

    NdisAcquireSpinLock(&Adapter->TxLock);
    Adapter->TxBusy = FALSE;
    NdisReleaseSpinLock(&Adapter->TxLock);

    IoFreeIrp(Irp);
    AsixDecrementPendingIo(Adapter);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
NTAPI
AsixRxResubmitDpc(
    _In_ PKDPC Dpc,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Arg1,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    (VOID)AsixStartBulkRead((PASIX_ADAPTER)Context);
}

static
VOID
NTAPI
AsixInterruptResubmitDpc(
    _In_ PKDPC Dpc,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Arg1,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    (VOID)AsixStartInterruptRead((PASIX_ADAPTER)Context);
}

static
NDIS_STATUS
AsixCopyNetBuffer(
    _In_ PNET_BUFFER NetBuffer,
    _Out_writes_bytes_(DestinationLength) PUCHAR Destination,
    _In_ ULONG DestinationLength)
{
    PMDL Mdl;
    ULONG MdlOffset;
    ULONG BytesLeft;
    ULONG Copied;

    if (NetBuffer->DataLength > DestinationLength)
    {
        return NDIS_STATUS_INVALID_LENGTH;
    }

    Mdl = NetBuffer->CurrentMdl;
    MdlOffset = NetBuffer->CurrentMdlOffset;
    BytesLeft = NetBuffer->DataLength;
    Copied = 0;

    while (BytesLeft != 0 && Mdl != NULL)
    {
        PUCHAR SystemAddress;
        ULONG ByteCount;
        ULONG Chunk;

        SystemAddress = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
        if (!SystemAddress)
        {
            return NDIS_STATUS_RESOURCES;
        }

        ByteCount = MmGetMdlByteCount(Mdl);
        if (MdlOffset >= ByteCount)
        {
            MdlOffset -= ByteCount;
            Mdl = Mdl->Next;
            continue;
        }

        Chunk = ByteCount - MdlOffset;
        if (Chunk > BytesLeft)
        {
            Chunk = BytesLeft;
        }

        RtlCopyMemory(Destination + Copied,
                      SystemAddress + MdlOffset,
                      Chunk);
        Copied += Chunk;
        BytesLeft -= Chunk;
        MdlOffset = 0;
        Mdl = Mdl->Next;
    }

    return (BytesLeft == 0) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_INVALID_LENGTH;
}

static
NDIS_STATUS
AsixTransmitNbl(
    _Inout_ PASIX_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST Nbl)
{
    PNET_BUFFER NetBuffer;
    ULONG FrameLength;
    ULONG TransferLength;
    USHORT HeaderLength;
    NTSTATUS Status;

    if (Adapter->Halting)
    {
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_FAILURE;
        return NDIS_STATUS_FAILURE;
    }

    if (Adapter->Paused)
    {
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_PAUSED;
        return NDIS_STATUS_PAUSED;
    }

    if (Adapter->MediaState != MediaConnectStateConnected)
    {
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_FAILURE;
        return NDIS_STATUS_FAILURE;
    }

    NdisAcquireSpinLock(&Adapter->TxLock);
    if (Adapter->TxBusy)
    {
        NdisReleaseSpinLock(&Adapter->TxLock);
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_RESOURCES;
        return NDIS_STATUS_RESOURCES;
    }
    Adapter->TxBusy = TRUE;
    NdisReleaseSpinLock(&Adapter->TxLock);

    NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
    if (!NetBuffer)
    {
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_INVALID_DATA;
        goto Fail;
    }

    FrameLength = NetBuffer->DataLength;
    if (FrameLength < ASIX_ETH_HEADER_LENGTH || FrameLength > ASIX_MAX_ETHERNET_FRAME)
    {
        NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_INVALID_LENGTH;
        goto Fail;
    }

    HeaderLength = (USHORT)FrameLength;
    Adapter->TxBuffer[0] = (UCHAR)(HeaderLength & 0xFF);
    Adapter->TxBuffer[1] = (UCHAR)(HeaderLength >> 8);
    HeaderLength = (USHORT)~HeaderLength;
    Adapter->TxBuffer[2] = (UCHAR)(HeaderLength & 0xFF);
    Adapter->TxBuffer[3] = (UCHAR)(HeaderLength >> 8);

    NET_BUFFER_LIST_STATUS(Nbl) = AsixCopyNetBuffer(NetBuffer,
                                                    Adapter->TxBuffer + ASIX_TX_HEADER_SIZE,
                                                    ASIX_MAX_ETHERNET_FRAME);
    if (NET_BUFFER_LIST_STATUS(Nbl) != NDIS_STATUS_SUCCESS)
    {
        goto Fail;
    }

    if (FrameLength & 1)
    {
        Adapter->TxBuffer[ASIX_TX_HEADER_SIZE + FrameLength] = 0;
    }

    TransferLength = ASIX_TX_HEADER_SIZE + ((FrameLength + 1) & ~1UL);
    Adapter->TxNbl = Nbl;

    RtlZeroMemory(&Adapter->TxUrb, sizeof(Adapter->TxUrb));
    UsbBuildInterruptOrBulkTransferRequest(&Adapter->TxUrb,
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           Adapter->BulkOut.PipeHandle,
                                           Adapter->TxBuffer,
                                           NULL,
                                           TransferLength,
                                           USBD_TRANSFER_DIRECTION_OUT,
                                           NULL);

    Status = AsixSubmitUrbAsync(Adapter, &Adapter->TxUrb, AsixTxComplete, &Adapter->TxIrp);
    if (NT_SUCCESS(Status) || Status == STATUS_PENDING)
    {
        return NDIS_STATUS_SUCCESS;
    }

    Adapter->TxNbl = NULL;
    NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_FAILURE;

Fail:
    NdisAcquireSpinLock(&Adapter->TxLock);
    Adapter->TxBusy = FALSE;
    NdisReleaseSpinLock(&Adapter->TxLock);
    return NET_BUFFER_LIST_STATUS(Nbl);
}

static
VOID
NTAPI
AsixMiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PASIX_ADAPTER Adapter;
    PNET_BUFFER_LIST Current;
    PNET_BUFFER_LIST Next;
    ULONG CompleteFlags;

    Adapter = (PASIX_ADAPTER)MiniportAdapterContext;

    if (PortNumber != NDIS_DEFAULT_PORT_NUMBER)
    {
        for (Current = NetBufferLists; Current != NULL; Current = NET_BUFFER_LIST_NEXT_NBL(Current))
        {
            NET_BUFFER_LIST_STATUS(Current) = NDIS_STATUS_INVALID_PORT;
        }

        CompleteFlags = (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL) ?
            NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0;
        NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                        NetBufferLists,
                                        CompleteFlags);
        return;
    }

    for (Current = NetBufferLists; Current != NULL; Current = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Current);
        NET_BUFFER_LIST_NEXT_NBL(Current) = NULL;

        if (AsixTransmitNbl(Adapter, Current) == NDIS_STATUS_SUCCESS)
        {
            PNET_BUFFER_LIST Remaining;

            Remaining = Next;
            for (; Next != NULL; Next = NET_BUFFER_LIST_NEXT_NBL(Next))
            {
                NET_BUFFER_LIST_STATUS(Next) = NDIS_STATUS_RESOURCES;
            }

            if (Remaining)
            {
                CompleteFlags = (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL) ?
                    NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0;
                NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                                Remaining,
                                                CompleteFlags);
            }
            break;
        }

        CompleteFlags = (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL) ?
            NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0;
        NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                        Current,
                                        CompleteFlags);
    }
}

static
VOID
NTAPI
AsixMiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

static
VOID
NTAPI
AsixMiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    PASIX_ADAPTER Adapter;
    PIRP TxIrp;

    Adapter = (PASIX_ADAPTER)MiniportAdapterContext;
    TxIrp = NULL;

    NdisAcquireSpinLock(&Adapter->TxLock);
    if (Adapter->TxNbl != NULL &&
        NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Adapter->TxNbl) == CancelId)
    {
        TxIrp = Adapter->TxIrp;
    }
    NdisReleaseSpinLock(&Adapter->TxLock);

    if (TxIrp)
    {
        IoCancelIrp(TxIrp);
    }
}

static
BOOLEAN
NTAPI
AsixMiniportCheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    PASIX_ADAPTER Adapter;

    Adapter = (PASIX_ADAPTER)MiniportAdapterContext;
    return Adapter != NULL &&
           !Adapter->Halting &&
           !Adapter->Paused &&
           Adapter->TxBusy &&
           Adapter->TxIrp == NULL;
}

static
NDIS_STATUS
NTAPI
AsixMiniportReset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    PASIX_ADAPTER Adapter;

    Adapter = (PASIX_ADAPTER)MiniportAdapterContext;
    *AddressingReset = FALSE;
    return (Adapter != NULL && !Adapter->Halting) ?
        NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
}

static
NDIS_STATUS
AsixOidCopy(
    _Out_writes_bytes_to_(InformationBufferLength, *BytesWritten) PVOID InformationBuffer,
    _In_ UINT InformationBufferLength,
    _In_reads_bytes_(DataLength) const VOID *Data,
    _In_ UINT DataLength,
    _Out_ PUINT BytesWritten,
    _Out_ PUINT BytesNeeded)
{
    *BytesNeeded = DataLength;
    if (InformationBufferLength < DataLength)
    {
        *BytesWritten = 0;
        return NDIS_STATUS_BUFFER_TOO_SHORT;
    }

    RtlCopyMemory(InformationBuffer, Data, DataLength);
    *BytesWritten = DataLength;
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
AsixQueryOid(
    _In_ PASIX_ADAPTER Adapter,
    _In_ NDIS_OID Oid,
    _Out_writes_bytes_to_(InformationBufferLength, *BytesWritten) PVOID InformationBuffer,
    _In_ UINT InformationBufferLength,
    _Out_ PUINT BytesWritten,
    _Out_ PUINT BytesNeeded)
{
    static const NDIS_OID SupportedOids[] =
    {
        OID_GEN_SUPPORTED_LIST,
        OID_GEN_HARDWARE_STATUS,
        OID_GEN_MEDIA_SUPPORTED,
        OID_GEN_MEDIA_IN_USE,
        OID_GEN_MAXIMUM_FRAME_SIZE,
        OID_GEN_LINK_SPEED,
        OID_GEN_CURRENT_PACKET_FILTER,
        OID_GEN_MAXIMUM_TOTAL_SIZE,
        OID_GEN_MEDIA_CONNECT_STATUS,
        OID_GEN_XMIT_OK,
        OID_GEN_RCV_OK,
        OID_GEN_XMIT_ERROR,
        OID_GEN_RCV_ERROR,
        OID_GEN_RCV_NO_BUFFER,
        OID_GEN_LINK_STATE,
        OID_802_3_PERMANENT_ADDRESS,
        OID_802_3_CURRENT_ADDRESS,
        OID_802_3_MULTICAST_LIST,
        OID_802_3_MAXIMUM_LIST_SIZE,
        OID_GEN_PHYSICAL_MEDIUM
    };
    NDIS_MEDIUM Medium;
    NDIS_HARDWARE_STATUS HardwareStatus;
    NDIS_LINK_STATE LinkState;
    ULONG Value;

    *BytesWritten = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               SupportedOids,
                               sizeof(SupportedOids),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_HARDWARE_STATUS:
            HardwareStatus = NdisHardwareStatusReady;
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &HardwareStatus,
                               sizeof(HardwareStatus),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Medium = NdisMedium802_3;
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Medium,
                               sizeof(Medium),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Value = ASIX_ETH_MTU;
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_LINK_SPEED:
            Value = (ULONG)(Adapter->LinkSpeed / 100);
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_CURRENT_PACKET_FILTER:
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->PacketFilter,
                               sizeof(Adapter->PacketFilter),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
            Value = ASIX_MAX_ETHERNET_FRAME;
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Value = (Adapter->MediaState == MediaConnectStateConnected) ?
                NdisMediaStateConnected : NdisMediaStateDisconnected;
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_XMIT_OK:
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->XmitOk,
                               sizeof(Adapter->XmitOk),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_RCV_OK:
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->RcvOk,
                               sizeof(Adapter->RcvOk),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_XMIT_ERROR:
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->XmitError,
                               sizeof(Adapter->XmitError),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_RCV_ERROR:
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->RcvError,
                               sizeof(Adapter->RcvError),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_RCV_NO_BUFFER:
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->RcvNoBuffer,
                               sizeof(Adapter->RcvNoBuffer),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_LINK_STATE:
            RtlZeroMemory(&LinkState, sizeof(LinkState));
            LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            LinkState.Header.Size = sizeof(LinkState);
            LinkState.MediaConnectState = Adapter->MediaState;
            LinkState.MediaDuplexState = MediaDuplexStateFull;
            LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            LinkState.PauseFunctions = NdisPauseFunctionsSendAndReceive;
            LinkState.AutoNegotiationFlags =
                NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED |
                NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED |
                NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &LinkState,
                               sizeof(LinkState),
                               BytesWritten,
                               BytesNeeded);

        case OID_802_3_CURRENT_ADDRESS:
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               Adapter->CurrentMacAddress,
                               ASIX_ETH_ADDRESS_LENGTH,
                               BytesWritten,
                               BytesNeeded);

        case OID_802_3_PERMANENT_ADDRESS:
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               Adapter->PermanentMacAddress,
                               ASIX_ETH_ADDRESS_LENGTH,
                               BytesWritten,
                               BytesNeeded);

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Value = 64;
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_PHYSICAL_MEDIUM:
            Value = NdisPhysicalMedium802_3;
            return AsixOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static
NDIS_STATUS
AsixSetOid(
    _Inout_ PASIX_ADAPTER Adapter,
    _In_ NDIS_OID Oid,
    _In_reads_bytes_(InformationBufferLength) PVOID InformationBuffer,
    _In_ UINT InformationBufferLength,
    _Out_ PUINT BytesRead,
    _Out_ PUINT BytesNeeded)
{
    ULONG PacketFilter;

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_CURRENT_PACKET_FILTER:
            if (InformationBufferLength < sizeof(PacketFilter))
            {
                *BytesNeeded = sizeof(PacketFilter);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            PacketFilter = *(PULONG)InformationBuffer;
            Adapter->PacketFilter = PacketFilter;
            *BytesRead = sizeof(PacketFilter);
            return NT_SUCCESS(AsixSetRxControl(Adapter)) ?
                NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;

        case OID_802_3_MULTICAST_LIST:
            *BytesRead = InformationBufferLength;
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static
NDIS_STATUS
NTAPI
AsixMiniportOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PASIX_ADAPTER Adapter;

    Adapter = (PASIX_ADAPTER)MiniportAdapterContext;

    if (OidRequest->RequestType == NdisRequestQueryInformation)
    {
        return AsixQueryOid(Adapter,
                            OidRequest->DATA.QUERY_INFORMATION.Oid,
                            OidRequest->DATA.QUERY_INFORMATION.InformationBuffer,
                            OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength,
                            &OidRequest->DATA.QUERY_INFORMATION.BytesWritten,
                            &OidRequest->DATA.QUERY_INFORMATION.BytesNeeded);
    }

    if (OidRequest->RequestType == NdisRequestSetInformation)
    {
        return AsixSetOid(Adapter,
                          OidRequest->DATA.SET_INFORMATION.Oid,
                          OidRequest->DATA.SET_INFORMATION.InformationBuffer,
                          OidRequest->DATA.SET_INFORMATION.InformationBufferLength,
                          &OidRequest->DATA.SET_INFORMATION.BytesRead,
                          &OidRequest->DATA.SET_INFORMATION.BytesNeeded);
    }

    return NDIS_STATUS_NOT_SUPPORTED;
}

static
VOID
NTAPI
AsixMiniportCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static
NDIS_STATUS
NTAPI
AsixMiniportPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PASIX_ADAPTER Adapter;

    UNREFERENCED_PARAMETER(PauseParameters);

    Adapter = (PASIX_ADAPTER)MiniportAdapterContext;
    if (!Adapter || Adapter->Halting)
    {
        return NDIS_STATUS_FAILURE;
    }

    Adapter->Paused = TRUE;
    AsixCancelPendingIo(Adapter, FALSE);
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
AsixMiniportRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PASIX_ADAPTER Adapter;

    UNREFERENCED_PARAMETER(RestartParameters);

    Adapter = (PASIX_ADAPTER)MiniportAdapterContext;
    if (!Adapter || Adapter->Halting)
    {
        return NDIS_STATUS_FAILURE;
    }

    Adapter->Paused = FALSE;
    (VOID)AsixStartBulkRead(Adapter);
    (VOID)AsixStartInterruptRead(Adapter);
    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
AsixMiniportDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PASIX_ADAPTER Adapter;

    Adapter = (PASIX_ADAPTER)MiniportAdapterContext;
    if (!Adapter || !NetDevicePnPEvent)
    {
        return;
    }

    switch (NetDevicePnPEvent->DevicePnPEvent)
    {
        case NdisDevicePnPEventRemoved:
        case NdisDevicePnPEventSurpriseRemoved:
            Adapter->Halting = TRUE;
            AsixCancelPendingIo(Adapter, TRUE);
            break;

        default:
            break;
    }
}

static
VOID
NTAPI
AsixMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PASIX_ADAPTER Adapter;

    Adapter = (PASIX_ADAPTER)MiniportAdapterContext;
    if (!Adapter)
    {
        return;
    }

    Adapter->Halting = TRUE;

    if (ShutdownAction == NdisShutdownBugCheck)
    {
        return;
    }

    AsixCancelPendingIo(Adapter, TRUE);

    if (Adapter->LowerDeviceObject && KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        (VOID)AsixWriteCommand(Adapter, ASIX_CMD_WRITE_RX_CTL, 0, 0);
    }
}

static
VOID
NTAPI
AsixMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PASIX_ADAPTER Adapter;

    UNREFERENCED_PARAMETER(HaltAction);

    Adapter = (PASIX_ADAPTER)MiniportAdapterContext;
    if (!Adapter)
    {
        return;
    }

    Adapter->Halting = TRUE;
    AsixCancelPendingIo(Adapter, TRUE);

    if (Adapter->PendingIoCount > 0)
    {
        KeWaitForSingleObject(&Adapter->RemoveEvent, Executive, KernelMode, FALSE, NULL);
    }

    if (Adapter->LowerDeviceObject)
    {
        (VOID)AsixWriteCommand(Adapter, ASIX_CMD_WRITE_RX_CTL, 0, 0);
    }

    if (Adapter->RxNblPool)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
    }

    NdisFreeSpinLock(&Adapter->TxLock);
    AsixFree(Adapter->InterruptBuffer);
    AsixFree(Adapter->TxBuffer);
    AsixFree(Adapter->RxBuffer);
    AsixFree(Adapter->ConfigurationDescriptor);
    AsixFree(Adapter->DeviceDescriptor);
    AsixFree(Adapter);
}

static
NDIS_STATUS
NTAPI
AsixMiniportInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PASIX_ADAPTER Adapter;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;
    NDIS_STATUS NdisStatus;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    Adapter = AsixAllocate(sizeof(*Adapter));
    if (!Adapter)
    {
        return NDIS_STATUS_RESOURCES;
    }

    Adapter->MiniportAdapterHandle = MiniportAdapterHandle;
    Adapter->PacketFilter = NDIS_PACKET_TYPE_DIRECTED |
                            NDIS_PACKET_TYPE_BROADCAST |
                            NDIS_PACKET_TYPE_ALL_MULTICAST;
    Adapter->PhyId = ASIX_INTERNAL_PHY_ID;
    Adapter->LinkSpeed = ASIX_LINK_SPEED_100MBPS;
    Adapter->MediaState = MediaConnectStateDisconnected;

    KeInitializeEvent(&Adapter->RemoveEvent, NotificationEvent, TRUE);
    KeInitializeDpc(&Adapter->RxResubmitDpc, AsixRxResubmitDpc, Adapter);
    KeInitializeDpc(&Adapter->InterruptResubmitDpc, AsixInterruptResubmitDpc, Adapter);
    NdisAllocateSpinLock(&Adapter->TxLock);

    NdisStatus = AsixSetRegistrationAttributes(Adapter);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        goto Cleanup;
    }

    PhysicalDeviceObject = NULL;
    LowerDeviceObject = NULL;
    NdisMGetDeviceProperty(MiniportAdapterHandle,
                           &PhysicalDeviceObject,
                           NULL,
                           &LowerDeviceObject,
                           NULL,
                           NULL);

    if (!PhysicalDeviceObject || !LowerDeviceObject)
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    Adapter->PhysicalDeviceObject = PhysicalDeviceObject;
    Adapter->LowerDeviceObject = LowerDeviceObject;

    Status = AsixReadDescriptors(Adapter);
    if (!NT_SUCCESS(Status))
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    Status = AsixSelectConfiguration(Adapter);
    if (!NT_SUCCESS(Status))
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    Status = AsixReadPhyId(Adapter);
    if (!NT_SUCCESS(Status))
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    Status = AsixGetMacAddress(Adapter);
    if (!NT_SUCCESS(Status))
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    Adapter->RxBuffer = AsixAllocate(ASIX_RX_BUFFER_SIZE);
    Adapter->TxBuffer = AsixAllocate(ASIX_TX_HEADER_SIZE + ASIX_MAX_ETHERNET_FRAME + 1);
    if (Adapter->InterruptIn.PipeHandle)
    {
        Adapter->InterruptBuffer = AsixAllocate(ASIX_INTERRUPT_LENGTH);
    }

    if (!Adapter->RxBuffer || !Adapter->TxBuffer)
    {
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    NdisStatus = AsixAllocateRxNblPool(Adapter);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        goto Cleanup;
    }

    Status = AsixInitializeHardware(Adapter);
    if (!NT_SUCCESS(Status))
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    AsixUpdateLinkFromPhy(Adapter);

    NdisStatus = AsixSetGeneralAttributes(Adapter);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        goto Cleanup;
    }

    (VOID)AsixStartBulkRead(Adapter);
    (VOID)AsixStartInterruptRead(Adapter);

    DPRINT1("USBASIX: AX88772B initialized, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            Adapter->PermanentMacAddress[0],
            Adapter->PermanentMacAddress[1],
            Adapter->PermanentMacAddress[2],
            Adapter->PermanentMacAddress[3],
            Adapter->PermanentMacAddress[4],
            Adapter->PermanentMacAddress[5]);

    return NDIS_STATUS_SUCCESS;

Cleanup:
    AsixMiniportHaltEx(Adapter, NdisHaltDeviceInitializationFailed);
    return NdisStatus;
}

static
VOID
NTAPI
AsixMiniportUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (g_AsixMiniportDriverHandle)
    {
        NdisMDeregisterMiniportDriver(g_AsixMiniportDriverHandle);
        g_AsixMiniportDriverHandle = NULL;
    }
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;

    RtlZeroMemory(&Characteristics, sizeof(Characteristics));
    Characteristics.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    Characteristics.Header.Size = sizeof(Characteristics);
    Characteristics.MajorNdisVersion = 6;
    Characteristics.MinorNdisVersion = 20;
    Characteristics.MajorDriverVersion = 1;
    Characteristics.MinorDriverVersion = 0;
    Characteristics.InitializeHandlerEx = AsixMiniportInitializeEx;
    Characteristics.HaltHandlerEx = AsixMiniportHaltEx;
    Characteristics.UnloadHandler = AsixMiniportUnload;
    Characteristics.PauseHandler = AsixMiniportPause;
    Characteristics.RestartHandler = AsixMiniportRestart;
    Characteristics.OidRequestHandler = AsixMiniportOidRequest;
    Characteristics.SendNetBufferListsHandler = AsixMiniportSendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = AsixMiniportReturnNetBufferLists;
    Characteristics.CancelSendHandler = AsixMiniportCancelSend;
    Characteristics.CheckForHangHandlerEx = AsixMiniportCheckForHang;
    Characteristics.ResetHandlerEx = AsixMiniportReset;
    Characteristics.DevicePnPEventNotifyHandler = AsixMiniportDevicePnPEventNotify;
    Characteristics.ShutdownHandlerEx = AsixMiniportShutdownEx;
    Characteristics.CancelOidRequestHandler = AsixMiniportCancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject,
                                       RegistryPath,
                                       NULL,
                                       &Characteristics,
                                       &g_AsixMiniportDriverHandle);
}
