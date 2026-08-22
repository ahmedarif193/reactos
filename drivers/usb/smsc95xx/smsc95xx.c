/*
 * PROJECT:     ReactOS SMSC LAN95xx USB Ethernet Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NDIS 6 miniport for SMSC/Microchip LAN95xx USB Ethernet
 *
 * This is an original implementation based on the public LAN95xx register,
 * USB framing, and PHY programming descriptions.  The ReactOS USB/NDIS
 * integration follows the contracts used by the other in-tree USB miniports.
 */

#include <ntddk.h>
#include <ndis.h>
#include <usbdi.h>
#include <usbbusif.h>
#include <usbdlib.h>
#include <debug.h>

#define SMSC_TAG                    'CsmS'

#define SMSC_VENDOR_ID              0x0424

#define SMSC_WRITE_REGISTER         0xA0
#define SMSC_READ_REGISTER          0xA1

#define SMSC_ID_REV                 0x0000
#define SMSC_INT_STS                0x0008
#define SMSC_TX_CFG                 0x0010
#define SMSC_HW_CFG                 0x0014
#define SMSC_LED_GPIO_CFG           0x0024
#define SMSC_AFC_CFG                0x002C
#define SMSC_E2P_CMD                0x0030
#define SMSC_E2P_DATA               0x0034
#define SMSC_BURST_CAP              0x0038
#define SMSC_INT_EP_CTL             0x0068
#define SMSC_BULK_IN_DLY            0x006C
#define SMSC_MAC_CR                 0x0100
#define SMSC_ADDRH                  0x0104
#define SMSC_ADDRL                  0x0108
#define SMSC_HASHH                  0x010C
#define SMSC_HASHL                  0x0110
#define SMSC_MII_ADDR               0x0114
#define SMSC_MII_DATA               0x0118
#define SMSC_FLOW                   0x011C
#define SMSC_VLAN1                  0x0120
#define SMSC_COE_CR                 0x0130

#define SMSC_HW_CFG_BIR             0x00001000
#define SMSC_HW_CFG_RXDOFF          0x00000600
#define SMSC_HW_CFG_MEF             0x00000020
#define SMSC_HW_CFG_BCE             0x00000002
#define SMSC_HW_CFG_LRST            0x00000008

#define SMSC_TX_CFG_ON              0x00000004
#define SMSC_INT_STS_CLEAR_ALL      0xFFFFFFFF
#define SMSC_INT_EP_CTL_PHY_INT     0x00008000
#define SMSC_INT_DATA_PHY_INT       0x00008000

#define SMSC_LED_GPIO_SPEED         0x01000000
#define SMSC_LED_GPIO_LINK          0x00100000
#define SMSC_LED_GPIO_DUPLEX        0x00010000
#define SMSC_AFC_CFG_DEFAULT        0x00F830A1

#define SMSC_MAC_CR_RXALL           0x80000000
#define SMSC_MAC_CR_RCVOWN          0x00800000
#define SMSC_MAC_CR_FDPX            0x00100000
#define SMSC_MAC_CR_MCPAS           0x00080000
#define SMSC_MAC_CR_PRMS            0x00040000
#define SMSC_MAC_CR_HPFILT          0x00002000
#define SMSC_MAC_CR_BCAST           0x00000800
#define SMSC_MAC_CR_TXEN            0x00000008
#define SMSC_MAC_CR_RXEN            0x00000004

#define SMSC_MII_WRITE              0x00000002
#define SMSC_MII_BUSY               0x00000001
#define SMSC_MII_BMCR               0
#define SMSC_MII_BMSR               1
#define SMSC_MII_ADVERTISE          4
#define SMSC_MII_PHY_SPECIAL        31
#define SMSC_MII_PHY_INT_SOURCE     29
#define SMSC_MII_PHY_INT_MASK       30
#define SMSC_MII_BMCR_RESET         0x8000
#define SMSC_MII_BMCR_AN_ENABLE     0x1000
#define SMSC_MII_BMCR_POWER_DOWN    0x0800
#define SMSC_MII_BMCR_ISOLATE       0x0400
#define SMSC_MII_BMCR_AN_RESTART    0x0200
#define SMSC_MII_BMSR_LINK          0x0004
#define SMSC_MII_ADVERTISE_DEFAULT  0x01E1
#define SMSC_MII_PHY_INT_DEFAULT    0x0050
#define SMSC_MII_SPECIAL_SPEED      0x001C
#define SMSC_MII_SPECIAL_10_HALF    0x0004
#define SMSC_MII_SPECIAL_100_HALF   0x0008
#define SMSC_MII_SPECIAL_10_FULL    0x0014
#define SMSC_MII_SPECIAL_100_FULL   0x0018

#define SMSC_E2P_BUSY               0x80000000
#define SMSC_E2P_TIMEOUT            0x00000400
#define SMSC_E2P_ADDRESS_MASK       0x000001FF
#define SMSC_EEPROM_MAC_OFFSET      1

#define SMSC_TX_CMD_A_FIRST         0x00002000
#define SMSC_TX_CMD_A_LAST          0x00001000
#define SMSC_TX_CMD_A_LENGTH_MASK   0x000007FF
#define SMSC_TX_CMD_B_LENGTH_MASK   0x000007FF
#define SMSC_RX_STATUS_LENGTH       0x3FFF0000
#define SMSC_RX_STATUS_ERROR        0x00008000

#define SMSC_INTERRUPT_LENGTH       4
#define SMSC_RX_BUFFER_SIZE         2048
#define SMSC_MAX_ETHERNET_FRAME     1518
#define SMSC_MAX_RX_FRAME           (SMSC_MAX_ETHERNET_FRAME + 4)
#define SMSC_TX_HEADER_SIZE         8
#define SMSC_RX_HEADER_SIZE         4
#define SMSC_ETH_ADDRESS_LENGTH     6
#define SMSC_ETH_HEADER_LENGTH      14
#define SMSC_ETH_MTU                1500
#define SMSC_LINK_SPEED_100MBPS     100000000ULL
#define SMSC_LINK_SPEED_10MBPS      10000000ULL
#define SMSC_TX_HANG_CHECK_LIMIT    2

typedef struct _SMSC_ENDPOINT
{
    UCHAR EndpointAddress;
    USBD_PIPE_TYPE PipeType;
    USBD_PIPE_HANDLE PipeHandle;
    USHORT MaximumPacketSize;
} SMSC_ENDPOINT, *PSMSC_ENDPOINT;

typedef struct _SMSC_ASYNC_REQUEST SMSC_ASYNC_REQUEST, *PSMSC_ASYNC_REQUEST;

typedef struct _SMSC_ADAPTER
{
    NDIS_HANDLE MiniportAdapterHandle;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;
    USB_DEVICE_DESCRIPTOR *DeviceDescriptor;
    USB_CONFIGURATION_DESCRIPTOR *ConfigurationDescriptor;
    USBD_CONFIGURATION_HANDLE ConfigurationHandle;

    SMSC_ENDPOINT BulkIn;
    SMSC_ENDPOINT BulkOut;
    SMSC_ENDPOINT InterruptIn;

    UCHAR PermanentMacAddress[SMSC_ETH_ADDRESS_LENGTH];
    UCHAR CurrentMacAddress[SMSC_ETH_ADDRESS_LENGTH];
    UCHAR PhyId;
    ULONG DeviceIdRevision;
    ULONG MacControl;
    ULONG PacketFilter;
    ULONG CurrentLookahead;
    UCHAR MulticastList[64][SMSC_ETH_ADDRESS_LENGTH];
    ULONG MulticastAddressCount;
    ULONG64 LinkSpeed;
    NDIS_MEDIA_CONNECT_STATE MediaState;
    NDIS_MEDIA_DUPLEX_STATE DuplexState;

    NDIS_HANDLE RxNblPool;
    PUCHAR RxBuffer;
    PUCHAR TxBuffer;
    PUCHAR InterruptBuffer;

    URB RxUrb;
    URB TxUrb;
    URB InterruptUrb;
    PSMSC_ASYNC_REQUEST RxRequest;
    PSMSC_ASYNC_REQUEST TxRequest;
    PSMSC_ASYNC_REQUEST InterruptRequest;

    PNET_BUFFER_LIST TxQueueHead;
    PNET_BUFFER_LIST TxQueueTail;
    PNET_BUFFER_LIST TxActiveNbl;
    PNET_BUFFER TxActiveNetBuffer;
    NDIS_STATUS TxActiveStatus;

    KDPC RxResubmitDpc;
    KDPC InterruptResubmitDpc;
    NDIS_HANDLE LinkWorkItem;
    NDIS_SPIN_LOCK StateLock;
    KMUTEX MacControlLock;
    KEVENT RemoveEvent;
    LONG PendingIoCount;
    LONG RxSubmitted;
    LONG InterruptSubmitted;
    LONG LinkWorkQueued;
    ULONG TxProgress;
    ULONG TxLastProgress;
    ULONG TxStallChecks;
    BOOLEAN TxBusy;
    BOOLEAN TxPumpActive;
    BOOLEAN TxAbortActive;
    BOOLEAN TxNeedsZlp;
    BOOLEAN TxSendingZlp;
    BOOLEAN Halting;
    BOOLEAN Paused;
    BOOLEAN Resetting;

    ULONG64 XmitOk;
    ULONG64 RcvOk;
    ULONG64 XmitError;
    ULONG64 RcvError;
    ULONG64 RcvNoBuffer;
} SMSC_ADAPTER, *PSMSC_ADAPTER;

struct _SMSC_ASYNC_REQUEST
{
    /*
     * The base reference owns both the IRP and one adapter rundown hold.
     * Cancellation takes a transient reference so completion cannot free the
     * IRP while IoCancelIrp is still using it.
    */
    PSMSC_ADAPTER Adapter;
    PIRP Irp;
    LONG ReferenceCount;
    LONG CompletionCalled;
};

static NDIS_HANDLE g_SmscMiniportDriverHandle;

static IO_COMPLETION_ROUTINE SmscRxComplete;
static IO_COMPLETION_ROUTINE SmscTxComplete;
static IO_COMPLETION_ROUTINE SmscInterruptComplete;

static VOID NTAPI SmscRxResubmitDpc(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2);
static VOID NTAPI SmscInterruptResubmitDpc(PKDPC Dpc, PVOID Context, PVOID Arg1, PVOID Arg2);
static NDIS_IO_WORKITEM_FUNCTION SmscLinkWorkItem;
static VOID SmscRunTxPump(PSMSC_ADAPTER Adapter);

static
PVOID
SmscAllocate(
    _In_ SIZE_T Size)
{
    PVOID Buffer;

    Buffer = ExAllocatePoolWithTag(NonPagedPool, Size, SMSC_TAG);
    if (Buffer)
    {
        RtlZeroMemory(Buffer, Size);
    }

    return Buffer;
}

static
VOID
SmscFree(
    _In_opt_ PVOID Buffer)
{
    if (Buffer)
    {
        ExFreePoolWithTag(Buffer, SMSC_TAG);
    }
}

static
VOID
SmscAcquireRundownLocked(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    if (InterlockedIncrement(&Adapter->PendingIoCount) == 1)
    {
        KeClearEvent(&Adapter->RemoveEvent);
    }
}

static
VOID
SmscReleaseRundown(
    _In_ PSMSC_ADAPTER Adapter)
{
    if (InterlockedDecrement(&Adapter->PendingIoCount) == 0)
    {
        KeSetEvent(&Adapter->RemoveEvent, IO_NO_INCREMENT, FALSE);
    }
}

static
BOOLEAN
SmscTryAcquireRundown(
    _Inout_ PSMSC_ADAPTER Adapter,
    _In_ BOOLEAN RequireRunning)
{
    BOOLEAN Acquired;

    NdisAcquireSpinLock(&Adapter->StateLock);
    Acquired = !Adapter->Halting &&
               (!RequireRunning || (!Adapter->Paused && !Adapter->Resetting));
    if (Acquired)
    {
        SmscAcquireRundownLocked(Adapter);
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    return Acquired;
}

static
BOOLEAN
SmscIsRunning(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    BOOLEAN Running;

    NdisAcquireSpinLock(&Adapter->StateLock);
    Running = !Adapter->Halting && !Adapter->Paused && !Adapter->Resetting;
    NdisReleaseSpinLock(&Adapter->StateLock);

    return Running;
}

static
NTSTATUS
SmscSubmitUrbSync(
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
VOID
SmscDereferenceAsyncRequest(
    _Inout_ PSMSC_ASYNC_REQUEST Request)
{
    PSMSC_ADAPTER Adapter;

    if (InterlockedDecrement(&Request->ReferenceCount) != 0)
    {
        return;
    }

    Adapter = Request->Adapter;
    IoFreeIrp(Request->Irp);
    SmscFree(Request);
    SmscReleaseRundown(Adapter);
}

static
PSMSC_ASYNC_REQUEST
SmscReferenceAsyncRequest(
    _Inout_ PSMSC_ADAPTER Adapter,
    _Inout_ PSMSC_ASYNC_REQUEST *RequestSlot)
{
    PSMSC_ASYNC_REQUEST Request;

    NdisAcquireSpinLock(&Adapter->StateLock);
    Request = *RequestSlot;
    if (Request)
    {
        InterlockedIncrement(&Request->ReferenceCount);
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    return Request;
}

static
VOID
SmscCancelAsyncRequest(
    _Inout_opt_ PSMSC_ASYNC_REQUEST Request)
{
    if (Request)
    {
        IoCancelIrp(Request->Irp);
        SmscDereferenceAsyncRequest(Request);
    }
}

static
NTSTATUS
SmscSubmitUrbAsync(
    _In_ PSMSC_ADAPTER Adapter,
    _Inout_ PURB Urb,
    _In_ PIO_COMPLETION_ROUTINE CompletionRoutine,
    _Inout_ PSMSC_ASYNC_REQUEST *RequestSlot)
{
    PSMSC_ASYNC_REQUEST Request;
    PIRP Irp;
    PIO_STACK_LOCATION IoStack;
    BOOLEAN CompletionCalled;
    NTSTATUS Status;

    Request = SmscAllocate(sizeof(*Request));
    if (!Request)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp = IoAllocateIrp(Adapter->LowerDeviceObject->StackSize, FALSE);
    if (!Irp)
    {
        SmscFree(Request);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Request->Adapter = Adapter;
    Request->Irp = Irp;
    Request->ReferenceCount = 2; /* Completion ownership and the submitter. */

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_INTERNAL_DEVICE_CONTROL;
    IoStack->Parameters.DeviceIoControl.IoControlCode = IOCTL_INTERNAL_USB_SUBMIT_URB;
    IoStack->Parameters.Others.Argument1 = Urb;

    IoSetCompletionRoutine(Irp, CompletionRoutine, Request, TRUE, TRUE, TRUE);

    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->Halting || Adapter->Paused || Adapter->Resetting || *RequestSlot)
    {
        NdisReleaseSpinLock(&Adapter->StateLock);
        IoFreeIrp(Irp);
        SmscFree(Request);
        return STATUS_DEVICE_NOT_READY;
    }

    SmscAcquireRundownLocked(Adapter);
    *RequestSlot = Request;
    NdisReleaseSpinLock(&Adapter->StateLock);

    Status = IoCallDriver(Adapter->LowerDeviceObject, Irp);
    CompletionCalled =
        InterlockedCompareExchange(&Request->CompletionCalled, 0, 0) != 0;

    if (Status != STATUS_PENDING && !CompletionCalled)
    {
        NdisAcquireSpinLock(&Adapter->StateLock);
        if (*RequestSlot == Request)
        {
            *RequestSlot = NULL;
        }
        NdisReleaseSpinLock(&Adapter->StateLock);

        SmscDereferenceAsyncRequest(Request);
        if (NT_SUCCESS(Status))
        {
            Status = STATUS_UNSUCCESSFUL;
        }
    }

    SmscDereferenceAsyncRequest(Request);

    /* An inline completion has already consumed the caller's operation. */
    return CompletionCalled ? STATUS_PENDING : Status;
}

static
NTSTATUS
SmscVendorCommand(
    _In_ PSMSC_ADAPTER Adapter,
    _In_ UCHAR Request,
    _In_ USHORT Value,
    _In_ USHORT Index,
    _Inout_updates_bytes_opt_(Length) PVOID Buffer,
    _In_ USHORT Length,
    _In_ BOOLEAN Read)
{
    URB Urb;
    NTSTATUS Status;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

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

    Status = SmscSubmitUrbSync(Adapter->LowerDeviceObject, &Urb);
    if (NT_SUCCESS(Status) && Read &&
        Urb.UrbControlVendorClassRequest.TransferBufferLength != Length)
    {
        Status = STATUS_DEVICE_DATA_ERROR;
    }

    return Status;
}

static
ULONG
SmscReadLittleEndian32(
    _In_reads_(4) const UCHAR *Buffer)
{
    return ((ULONG)Buffer[0]) |
           ((ULONG)Buffer[1] << 8) |
           ((ULONG)Buffer[2] << 16) |
           ((ULONG)Buffer[3] << 24);
}

static
VOID
SmscWriteLittleEndian32(
    _Out_writes_(4) UCHAR *Buffer,
    _In_ ULONG Value)
{
    Buffer[0] = (UCHAR)Value;
    Buffer[1] = (UCHAR)(Value >> 8);
    Buffer[2] = (UCHAR)(Value >> 16);
    Buffer[3] = (UCHAR)(Value >> 24);
}

static
ULONG
SmscEthernetCrc(
    _In_reads_(SMSC_ETH_ADDRESS_LENGTH) const UCHAR *Address)
{
    ULONG Crc;
    ULONG ReversedCrc;
    ULONG ByteIndex;
    ULONG BitIndex;

    Crc = 0xFFFFFFFF;
    for (ByteIndex = 0; ByteIndex < SMSC_ETH_ADDRESS_LENGTH; ByteIndex++)
    {
        UCHAR CurrentByte;

        CurrentByte = Address[ByteIndex];
        for (BitIndex = 0; BitIndex < 8; BitIndex++)
        {
            ULONG Carry;

            Carry = (Crc ^ CurrentByte) & 1;
            Crc >>= 1;
            if (Carry)
            {
                Crc ^= 0xEDB88320;
            }
            CurrentByte >>= 1;
        }
    }

    ReversedCrc = 0;
    for (BitIndex = 0; BitIndex < 32; BitIndex++)
    {
        ReversedCrc = (ReversedCrc << 1) | (Crc & 1);
        Crc >>= 1;
    }

    return ReversedCrc;
}

static
NTSTATUS
SmscReadRegister(
    _In_ PSMSC_ADAPTER Adapter,
    _In_ USHORT Register,
    _Out_ PULONG Value)
{
    UCHAR Buffer[4];
    NTSTATUS Status;

    Status = SmscVendorCommand(Adapter,
                               SMSC_READ_REGISTER,
                               0,
                               Register,
                               Buffer,
                               sizeof(Buffer),
                               TRUE);
    if (NT_SUCCESS(Status))
    {
        *Value = SmscReadLittleEndian32(Buffer);
    }

    return Status;
}

static
NTSTATUS
SmscWriteRegister(
    _In_ PSMSC_ADAPTER Adapter,
    _In_ USHORT Register,
    _In_ ULONG Value)
{
    UCHAR Buffer[4];

    SmscWriteLittleEndian32(Buffer, Value);
    return SmscVendorCommand(Adapter,
                             SMSC_WRITE_REGISTER,
                             0,
                             Register,
                             Buffer,
                             sizeof(Buffer),
                             FALSE);
}

static
NTSTATUS
SmscWaitForMii(
    _In_ PSMSC_ADAPTER Adapter)
{
    ULONG Value;
    ULONG Retry;
    NTSTATUS Status;

    for (Retry = 0; Retry < 100; Retry++)
    {
        Status = SmscReadRegister(Adapter, SMSC_MII_ADDR, &Value);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (!(Value & SMSC_MII_BUSY))
        {
            return STATUS_SUCCESS;
        }

        NdisMSleep(10000);
    }

    return STATUS_IO_TIMEOUT;
}

static
NTSTATUS
SmscReadPhy(
    _In_ PSMSC_ADAPTER Adapter,
    _In_ UCHAR Register,
    _Out_ PUSHORT Value)
{
    ULONG Command;
    ULONG Data;
    NTSTATUS Status;

    Status = SmscWaitForMii(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Command = ((ULONG)(Adapter->PhyId & 0x1F) << 11) |
              ((ULONG)(Register & 0x1F) << 6) |
              SMSC_MII_BUSY;
    Status = SmscWriteRegister(Adapter, SMSC_MII_ADDR, Command);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscWaitForMii(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscReadRegister(Adapter, SMSC_MII_DATA, &Data);
    if (NT_SUCCESS(Status))
    {
        *Value = (USHORT)Data;
    }

    return Status;
}

static
NTSTATUS
SmscWritePhy(
    _In_ PSMSC_ADAPTER Adapter,
    _In_ UCHAR Register,
    _In_ USHORT Value)
{
    ULONG Command;
    NTSTATUS Status;

    Status = SmscWaitForMii(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscWriteRegister(Adapter, SMSC_MII_DATA, Value);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Command = ((ULONG)(Adapter->PhyId & 0x1F) << 11) |
              ((ULONG)(Register & 0x1F) << 6) |
              SMSC_MII_WRITE |
              SMSC_MII_BUSY;
    Status = SmscWriteRegister(Adapter, SMSC_MII_ADDR, Command);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return SmscWaitForMii(Adapter);
}

static
VOID
SmscUpdateLinkFromPhy(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    USHORT Bmsr;
    USHORT PhySpecial;
    NDIS_MEDIA_CONNECT_STATE NewState;
    NDIS_MEDIA_DUPLEX_STATE NewDuplex;
    ULONG64 NewSpeed;
    ULONG NewMacControl;

    NewState = MediaConnectStateDisconnected;
    NewSpeed = SMSC_LINK_SPEED_100MBPS;
    NewDuplex = MediaDuplexStateUnknown;

    /* BMSR link is latched low, so use the second read. */
    if (NT_SUCCESS(SmscReadPhy(Adapter, SMSC_MII_BMSR, &Bmsr)) &&
        NT_SUCCESS(SmscReadPhy(Adapter, SMSC_MII_BMSR, &Bmsr)) &&
        (Bmsr & SMSC_MII_BMSR_LINK))
    {
        NewState = MediaConnectStateConnected;
        if (NT_SUCCESS(SmscReadPhy(Adapter, SMSC_MII_PHY_SPECIAL, &PhySpecial)))
        {
            switch (PhySpecial & SMSC_MII_SPECIAL_SPEED)
            {
                case SMSC_MII_SPECIAL_10_HALF:
                    NewSpeed = SMSC_LINK_SPEED_10MBPS;
                    NewDuplex = MediaDuplexStateHalf;
                    break;

                case SMSC_MII_SPECIAL_10_FULL:
                    NewSpeed = SMSC_LINK_SPEED_10MBPS;
                    NewDuplex = MediaDuplexStateFull;
                    break;

                case SMSC_MII_SPECIAL_100_HALF:
                    NewDuplex = MediaDuplexStateHalf;
                    break;

                case SMSC_MII_SPECIAL_100_FULL:
                    NewDuplex = MediaDuplexStateFull;
                    break;

                default:
                    break;
            }
        }
    }

    NdisAcquireSpinLock(&Adapter->StateLock);
    Adapter->MediaState = NewState;
    Adapter->LinkSpeed = NewSpeed;
    Adapter->DuplexState = NewDuplex;
    NdisReleaseSpinLock(&Adapter->StateLock);

    if (NewState == MediaConnectStateConnected)
    {
        KeWaitForSingleObject(&Adapter->MacControlLock,
                              Executive,
                              KernelMode,
                              FALSE,
                              NULL);
        NewMacControl = Adapter->MacControl;
        if (NewDuplex == MediaDuplexStateFull)
        {
            NewMacControl |= SMSC_MAC_CR_FDPX;
            NewMacControl &= ~SMSC_MAC_CR_RCVOWN;
        }
        else if (NewDuplex == MediaDuplexStateHalf)
        {
            NewMacControl &= ~SMSC_MAC_CR_FDPX;
            NewMacControl |= SMSC_MAC_CR_RCVOWN;
        }

        if (NewMacControl != Adapter->MacControl &&
            NT_SUCCESS(SmscWriteRegister(Adapter, SMSC_MAC_CR, NewMacControl)))
        {
            Adapter->MacControl = NewMacControl;
        }
        KeReleaseMutex(&Adapter->MacControlLock, FALSE);
    }
}

static
VOID
SmscIndicateLinkState(
    _In_ PSMSC_ADAPTER Adapter)
{
    NDIS_STATUS_INDICATION Indication;
    NDIS_LINK_STATE LinkState;

    RtlZeroMemory(&LinkState, sizeof(LinkState));
    LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
    LinkState.Header.Size = sizeof(LinkState);
    NdisAcquireSpinLock(&Adapter->StateLock);
    LinkState.MediaConnectState = Adapter->MediaState;
    LinkState.MediaDuplexState = Adapter->DuplexState;
    LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
    LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
    NdisReleaseSpinLock(&Adapter->StateLock);
    LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;
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
SmscGetDescriptor(
    _In_ PSMSC_ADAPTER Adapter,
    _In_ UCHAR DescriptorType,
    _Outptr_result_bytebuffer_(*DescriptorLength) PVOID *Descriptor,
    _Inout_ PULONG DescriptorLength)
{
    PURB Urb;
    PVOID Buffer;
    NTSTATUS Status;

    Buffer = SmscAllocate(*DescriptorLength);
    if (!Buffer)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Urb = SmscAllocate(sizeof(URB));
    if (!Urb)
    {
        SmscFree(Buffer);
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

    Status = SmscSubmitUrbSync(Adapter->LowerDeviceObject, Urb);
    if (NT_SUCCESS(Status))
    {
        *Descriptor = Buffer;
        *DescriptorLength = Urb->UrbControlDescriptorRequest.TransferBufferLength;
    }
    else
    {
        SmscFree(Buffer);
        *Descriptor = NULL;
    }

    SmscFree(Urb);
    return Status;
}

static
BOOLEAN
SmscIsSupportedProduct(
    _In_ USHORT ProductId)
{
    switch (ProductId)
    {
        case 0x9500:
        case 0x9505:
        case 0x9530:
        case 0x9730:
        case 0x9E00:
        case 0x9E01:
        case 0x9E08:
        case 0xEC00: /* LAN9512/LAN9514 Ethernet function */
        case 0x9900:
        case 0x9901:
        case 0x9902:
        case 0x9903:
        case 0x9904:
        case 0x9905:
        case 0x9906:
        case 0x9907:
        case 0x9908:
        case 0x9909:
            return TRUE;

        default:
            return FALSE;
    }
}

static
NTSTATUS
SmscReadDescriptors(
    _In_ PSMSC_ADAPTER Adapter)
{
    USB_CONFIGURATION_DESCRIPTOR *Config;
    ULONG Length;
    NTSTATUS Status;

    Length = sizeof(USB_DEVICE_DESCRIPTOR);
    Status = SmscGetDescriptor(Adapter,
                               USB_DEVICE_DESCRIPTOR_TYPE,
                               (PVOID *)&Adapter->DeviceDescriptor,
                               &Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Adapter->DeviceDescriptor->idVendor != SMSC_VENDOR_ID ||
        !SmscIsSupportedProduct(Adapter->DeviceDescriptor->idProduct))
    {
        return STATUS_NOT_SUPPORTED;
    }

    Length = sizeof(USB_CONFIGURATION_DESCRIPTOR);
    Status = SmscGetDescriptor(Adapter,
                               USB_CONFIGURATION_DESCRIPTOR_TYPE,
                               (PVOID *)&Config,
                               &Length);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Length = Config->wTotalLength;
    SmscFree(Config);

    Status = SmscGetDescriptor(Adapter,
                               USB_CONFIGURATION_DESCRIPTOR_TYPE,
                               (PVOID *)&Adapter->ConfigurationDescriptor,
                               &Length);

    return Status;
}

static
VOID
SmscRecordEndpoint(
    _Inout_ PSMSC_ADAPTER Adapter,
    _In_ PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor)
{
    UCHAR Attributes;
    PSMSC_ENDPOINT Endpoint;

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
SmscFindEndpoints(
    _Inout_ PSMSC_ADAPTER Adapter,
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
            SmscRecordEndpoint(Adapter, (PUSB_ENDPOINT_DESCRIPTOR)Common);
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
SmscSelectConfiguration(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    USB_INTERFACE_DESCRIPTOR *InterfaceDescriptor;
    USBD_INTERFACE_LIST_ENTRY InterfaceList[2];
    PURB Urb;
    NTSTATUS Status;
    ULONG i;

    Status = SmscFindEndpoints(Adapter, &InterfaceDescriptor);
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

    Status = SmscSubmitUrbSync(Adapter->LowerDeviceObject, Urb);
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
BOOLEAN
SmscIsValidMacAddress(
    _In_reads_(SMSC_ETH_ADDRESS_LENGTH) const UCHAR *Address)
{
    static const UCHAR ZeroAddress[SMSC_ETH_ADDRESS_LENGTH] = {0};
    static const UCHAR BroadcastAddress[SMSC_ETH_ADDRESS_LENGTH] =
        {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

    return !(Address[0] & 0x01) &&
           RtlCompareMemory(Address, ZeroAddress, sizeof(ZeroAddress)) != sizeof(ZeroAddress) &&
           RtlCompareMemory(Address, BroadcastAddress, sizeof(BroadcastAddress)) != sizeof(BroadcastAddress);
}

static
NTSTATUS
SmscReadEepromByte(
    _In_ PSMSC_ADAPTER Adapter,
    _In_ USHORT Address,
    _Out_ PUCHAR Value)
{
    ULONG Command;
    ULONG Data;
    ULONG Retry;
    NTSTATUS Status;

    for (Retry = 0; Retry < 100; Retry++)
    {
        Status = SmscReadRegister(Adapter, SMSC_E2P_CMD, &Command);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (!(Command & SMSC_E2P_BUSY))
        {
            break;
        }

        NdisMSleep(10000);
    }

    if (Retry == 100)
    {
        return STATUS_IO_TIMEOUT;
    }

    Command = SMSC_E2P_BUSY | (Address & SMSC_E2P_ADDRESS_MASK);
    Status = SmscWriteRegister(Adapter, SMSC_E2P_CMD, Command);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (Retry = 0; Retry < 100; Retry++)
    {
        NdisMSleep(10000);
        Status = SmscReadRegister(Adapter, SMSC_E2P_CMD, &Command);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (!(Command & SMSC_E2P_BUSY))
        {
            if (Command & SMSC_E2P_TIMEOUT)
            {
                return STATUS_IO_TIMEOUT;
            }

            Status = SmscReadRegister(Adapter, SMSC_E2P_DATA, &Data);
            if (NT_SUCCESS(Status))
            {
                *Value = (UCHAR)Data;
            }
            return Status;
        }
    }

    return STATUS_IO_TIMEOUT;
}

static
VOID
SmscGenerateFallbackMacAddress(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    LARGE_INTEGER Time;
    ULONG Seed;
    ULONG Index;

    KeQuerySystemTime(&Time);
    Seed = Time.LowPart ^ Time.HighPart ^ Adapter->DeviceIdRevision;
    Seed ^= ((ULONG)Adapter->DeviceDescriptor->idVendor << 16) |
            Adapter->DeviceDescriptor->idProduct;

    Adapter->PermanentMacAddress[0] = 0x02;
    for (Index = 1; Index < SMSC_ETH_ADDRESS_LENGTH; Index++)
    {
        Seed ^= Seed << 13;
        Seed ^= Seed >> 17;
        Seed ^= Seed << 5;
        Adapter->PermanentMacAddress[Index] = (UCHAR)Seed;
    }

    DPRINT1("SMSC95XX: device has no valid programmed MAC; using a locally administered address\n");
}

static
NTSTATUS
SmscGetMacAddress(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    ULONG AddressLow;
    ULONG AddressHigh;
    ULONG Index;
    NTSTATUS Status;

    Status = SmscReadRegister(Adapter, SMSC_ADDRL, &AddressLow);
    if (NT_SUCCESS(Status))
    {
        Status = SmscReadRegister(Adapter, SMSC_ADDRH, &AddressHigh);
    }

    if (NT_SUCCESS(Status))
    {
        Adapter->PermanentMacAddress[0] = (UCHAR)AddressLow;
        Adapter->PermanentMacAddress[1] = (UCHAR)(AddressLow >> 8);
        Adapter->PermanentMacAddress[2] = (UCHAR)(AddressLow >> 16);
        Adapter->PermanentMacAddress[3] = (UCHAR)(AddressLow >> 24);
        Adapter->PermanentMacAddress[4] = (UCHAR)AddressHigh;
        Adapter->PermanentMacAddress[5] = (UCHAR)(AddressHigh >> 8);
    }

    if (!NT_SUCCESS(Status) || !SmscIsValidMacAddress(Adapter->PermanentMacAddress))
    {
        for (Index = 0; Index < SMSC_ETH_ADDRESS_LENGTH; Index++)
        {
            Status = SmscReadEepromByte(Adapter,
                                        SMSC_EEPROM_MAC_OFFSET + (USHORT)Index,
                                        &Adapter->PermanentMacAddress[Index]);
            if (!NT_SUCCESS(Status))
            {
                break;
            }
        }

        if (!NT_SUCCESS(Status) || !SmscIsValidMacAddress(Adapter->PermanentMacAddress))
        {
            SmscGenerateFallbackMacAddress(Adapter);
        }
    }

    RtlCopyMemory(Adapter->CurrentMacAddress,
                  Adapter->PermanentMacAddress,
                  SMSC_ETH_ADDRESS_LENGTH);
    return STATUS_SUCCESS;
}

static
NTSTATUS
SmscProgramMacAddress(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    ULONG AddressLow;
    ULONG AddressHigh;
    NTSTATUS Status;

    AddressLow = ((ULONG)Adapter->CurrentMacAddress[0]) |
                 ((ULONG)Adapter->CurrentMacAddress[1] << 8) |
                 ((ULONG)Adapter->CurrentMacAddress[2] << 16) |
                 ((ULONG)Adapter->CurrentMacAddress[3] << 24);
    AddressHigh = ((ULONG)Adapter->CurrentMacAddress[4]) |
                  ((ULONG)Adapter->CurrentMacAddress[5] << 8);

    Status = SmscWriteRegister(Adapter, SMSC_ADDRL, AddressLow);
    if (NT_SUCCESS(Status))
    {
        Status = SmscWriteRegister(Adapter, SMSC_ADDRH, AddressHigh);
    }

    return Status;
}

static
NTSTATUS
SmscSetRxControl(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    ULONG HashHigh;
    ULONG HashLow;
    ULONG MacControl;
    ULONG PacketFilter;
    ULONG Index;
    NTSTATUS Status;

    HashHigh = 0;
    HashLow = 0;
    KeWaitForSingleObject(&Adapter->MacControlLock,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    NdisAcquireSpinLock(&Adapter->StateLock);
    PacketFilter = Adapter->PacketFilter;
    MacControl = Adapter->MacControl;
    MacControl &= ~(SMSC_MAC_CR_RXALL |
                    SMSC_MAC_CR_MCPAS |
                    SMSC_MAC_CR_PRMS |
                    SMSC_MAC_CR_HPFILT |
                    SMSC_MAC_CR_BCAST);

    if (PacketFilter & NDIS_PACKET_TYPE_PROMISCUOUS)
    {
        MacControl |= SMSC_MAC_CR_PRMS;
    }
    else if (PacketFilter & NDIS_PACKET_TYPE_ALL_MULTICAST)
    {
        MacControl |= SMSC_MAC_CR_MCPAS;
    }
    else if ((PacketFilter & NDIS_PACKET_TYPE_MULTICAST) &&
             Adapter->MulticastAddressCount != 0)
    {
        MacControl |= SMSC_MAC_CR_HPFILT;
        for (Index = 0; Index < Adapter->MulticastAddressCount; Index++)
        {
            ULONG HashBit;
            ULONG HashMask;

            HashBit = (SmscEthernetCrc(Adapter->MulticastList[Index]) >> 26) & 0x3F;
            HashMask = 1UL << (HashBit & 0x1F);
            if (HashBit & 0x20)
            {
                HashHigh |= HashMask;
            }
            else
            {
                HashLow |= HashMask;
            }
        }
    }

    if (!(PacketFilter &
          (NDIS_PACKET_TYPE_BROADCAST | NDIS_PACKET_TYPE_PROMISCUOUS)))
    {
        MacControl |= SMSC_MAC_CR_BCAST;
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    Status = SmscWriteRegister(Adapter, SMSC_HASHH, HashHigh);
    if (NT_SUCCESS(Status))
    {
        Status = SmscWriteRegister(Adapter, SMSC_HASHL, HashLow);
    }
    if (NT_SUCCESS(Status))
    {
        Status = SmscWriteRegister(Adapter, SMSC_MAC_CR, MacControl);
    }
    if (NT_SUCCESS(Status))
    {
        Adapter->MacControl = MacControl;
    }

    KeReleaseMutex(&Adapter->MacControlLock, FALSE);
    return Status;
}

static
NTSTATUS
SmscInitializeHardware(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    NTSTATUS Status;
    ULONG Value;
    ULONG Retry;
    USHORT PhyControl;
    USHORT PhyInterruptSource;

    Status = SmscWriteRegister(Adapter, SMSC_HW_CFG, SMSC_HW_CFG_LRST);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (Retry = 0; Retry < 100; Retry++)
    {
        NdisMSleep(10000);
        Status = SmscReadRegister(Adapter, SMSC_HW_CFG, &Value);
        if (!NT_SUCCESS(Status) || !(Value & SMSC_HW_CFG_LRST))
        {
            break;
        }
    }
    if (!NT_SUCCESS(Status) || Retry == 100)
    {
        return NT_SUCCESS(Status) ? STATUS_IO_TIMEOUT : Status;
    }

    Status = SmscReadRegister(Adapter, SMSC_ID_REV, &Adapter->DeviceIdRevision);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscProgramMacAddress(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscReadRegister(Adapter, SMSC_HW_CFG, &Value);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Value |= SMSC_HW_CFG_BIR;
    Value &= ~(SMSC_HW_CFG_MEF | SMSC_HW_CFG_BCE | SMSC_HW_CFG_RXDOFF);
    Status = SmscWriteRegister(Adapter, SMSC_HW_CFG, Value);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscWriteRegister(Adapter, SMSC_BURST_CAP, 0);
    if (NT_SUCCESS(Status))
        Status = SmscWriteRegister(Adapter, SMSC_BULK_IN_DLY, 0);
    if (NT_SUCCESS(Status))
        Status = SmscWriteRegister(Adapter, SMSC_INT_STS, SMSC_INT_STS_CLEAR_ALL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscReadRegister(Adapter, SMSC_LED_GPIO_CFG, &Value);
    if (NT_SUCCESS(Status))
    {
        Value |= SMSC_LED_GPIO_SPEED | SMSC_LED_GPIO_LINK | SMSC_LED_GPIO_DUPLEX;
        Status = SmscWriteRegister(Adapter, SMSC_LED_GPIO_CFG, Value);
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscWriteRegister(Adapter, SMSC_FLOW, 0);
    if (NT_SUCCESS(Status))
        Status = SmscWriteRegister(Adapter, SMSC_AFC_CFG, SMSC_AFC_CFG_DEFAULT);
    if (NT_SUCCESS(Status))
        Status = SmscWriteRegister(Adapter, SMSC_VLAN1, 0x8100);
    if (NT_SUCCESS(Status))
        Status = SmscWriteRegister(Adapter, SMSC_COE_CR, 0);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscReadRegister(Adapter, SMSC_MAC_CR, &Value);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    KeWaitForSingleObject(&Adapter->MacControlLock,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
    Adapter->MacControl = Value | SMSC_MAC_CR_TXEN | SMSC_MAC_CR_RXEN;
    KeReleaseMutex(&Adapter->MacControlLock, FALSE);
    Status = SmscSetRxControl(Adapter);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscReadRegister(Adapter, SMSC_INT_EP_CTL, &Value);
    if (NT_SUCCESS(Status))
    {
        Value |= SMSC_INT_EP_CTL_PHY_INT;
        Status = SmscWriteRegister(Adapter, SMSC_INT_EP_CTL, Value);
    }
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscWritePhy(Adapter, SMSC_MII_BMCR, SMSC_MII_BMCR_RESET);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    for (Retry = 0; Retry < 100; Retry++)
    {
        NdisMSleep(10000);
        Status = SmscReadPhy(Adapter, SMSC_MII_BMCR, &PhyControl);
        if (!NT_SUCCESS(Status) || !(PhyControl & SMSC_MII_BMCR_RESET))
        {
            break;
        }
    }
    if (!NT_SUCCESS(Status) || Retry == 100)
    {
        return NT_SUCCESS(Status) ? STATUS_IO_TIMEOUT : Status;
    }

    Status = SmscWritePhy(Adapter,
                          SMSC_MII_ADVERTISE,
                          SMSC_MII_ADVERTISE_DEFAULT);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscReadPhy(Adapter,
                         SMSC_MII_PHY_INT_SOURCE,
                         &PhyInterruptSource);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = SmscWritePhy(Adapter,
                          SMSC_MII_PHY_INT_MASK,
                          SMSC_MII_PHY_INT_DEFAULT);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    Status = SmscReadPhy(Adapter, SMSC_MII_BMCR, &PhyControl);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    PhyControl &= ~(SMSC_MII_BMCR_POWER_DOWN | SMSC_MII_BMCR_ISOLATE);
    PhyControl |= SMSC_MII_BMCR_AN_ENABLE | SMSC_MII_BMCR_AN_RESTART;
    Status = SmscWritePhy(Adapter, SMSC_MII_BMCR, PhyControl);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return SmscWriteRegister(Adapter, SMSC_TX_CFG, SMSC_TX_CFG_ON);
}

static
NDIS_STATUS
SmscSetRegistrationAttributes(
    _In_ PSMSC_ADAPTER Adapter)
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
SmscSetGeneralAttributes(
    _In_ PSMSC_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES Attributes;

    RtlZeroMemory(&Attributes, sizeof(Attributes));
    Attributes.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    Attributes.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    Attributes.Header.Size = sizeof(Attributes);
    Attributes.MediaType = NdisMedium802_3;
    Attributes.PhysicalMediumType = NdisPhysicalMedium802_3;
    Attributes.MtuSize = SMSC_ETH_MTU;
    Attributes.MaxXmitLinkSpeed = SMSC_LINK_SPEED_100MBPS;
    Attributes.XmitLinkSpeed = Adapter->LinkSpeed;
    Attributes.MaxRcvLinkSpeed = SMSC_LINK_SPEED_100MBPS;
    Attributes.RcvLinkSpeed = Adapter->LinkSpeed;
    Attributes.MediaConnectState = Adapter->MediaState;
    Attributes.MediaDuplexState = Adapter->DuplexState;
    Attributes.LookaheadSize = SMSC_ETH_MTU;
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
    Attributes.MacAddressLength = SMSC_ETH_ADDRESS_LENGTH;
    RtlCopyMemory(Attributes.PermanentMacAddress,
                  Adapter->PermanentMacAddress,
                  SMSC_ETH_ADDRESS_LENGTH);
    RtlCopyMemory(Attributes.CurrentMacAddress,
                  Adapter->CurrentMacAddress,
                  SMSC_ETH_ADDRESS_LENGTH);
    Attributes.AccessType = NET_IF_ACCESS_BROADCAST;
    Attributes.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    Attributes.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    Attributes.IfType = IF_TYPE_ETHERNET_CSMACD;
    Attributes.IfConnectorPresent = TRUE;
    Attributes.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
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
SmscAllocateRxNblPool(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS PoolParameters;

    RtlZeroMemory(&PoolParameters, sizeof(PoolParameters));
    PoolParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PoolParameters.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    PoolParameters.Header.Size = sizeof(PoolParameters);
    PoolParameters.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    PoolParameters.fAllocateNetBuffer = TRUE;
    PoolParameters.PoolTag = SMSC_TAG;

    Adapter->RxNblPool = NdisAllocateNetBufferListPool(
        Adapter->MiniportAdapterHandle,
        &PoolParameters);

    return Adapter->RxNblPool ? NDIS_STATUS_SUCCESS : NDIS_STATUS_RESOURCES;
}

static
NTSTATUS
SmscStartBulkRead(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    NTSTATUS Status;

    if (!SmscTryAcquireRundown(Adapter, TRUE))
    {
        return STATUS_DEVICE_NOT_READY;
    }

    if (!Adapter->BulkIn.PipeHandle)
    {
        SmscReleaseRundown(Adapter);
        return STATUS_DEVICE_NOT_READY;
    }

    if (InterlockedCompareExchange(&Adapter->RxSubmitted, 1, 0) != 0)
    {
        SmscReleaseRundown(Adapter);
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&Adapter->RxUrb, sizeof(Adapter->RxUrb));
    UsbBuildInterruptOrBulkTransferRequest(&Adapter->RxUrb,
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           Adapter->BulkIn.PipeHandle,
                                           Adapter->RxBuffer,
                                           NULL,
                                           SMSC_RX_BUFFER_SIZE,
                                           USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                                           NULL);

    Status = SmscSubmitUrbAsync(Adapter,
                                &Adapter->RxUrb,
                                SmscRxComplete,
                                &Adapter->RxRequest);
    if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
    {
        InterlockedExchange(&Adapter->RxSubmitted, 0);
    }

    SmscReleaseRundown(Adapter);
    return Status;
}

static
NTSTATUS
SmscStartInterruptRead(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    NTSTATUS Status;

    if (!SmscTryAcquireRundown(Adapter, TRUE))
    {
        return STATUS_DEVICE_NOT_READY;
    }

    if (!Adapter->InterruptIn.PipeHandle || !Adapter->InterruptBuffer)
    {
        SmscReleaseRundown(Adapter);
        return STATUS_DEVICE_NOT_READY;
    }

    if (InterlockedCompareExchange(&Adapter->InterruptSubmitted, 1, 0) != 0)
    {
        SmscReleaseRundown(Adapter);
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&Adapter->InterruptUrb, sizeof(Adapter->InterruptUrb));
    UsbBuildInterruptOrBulkTransferRequest(&Adapter->InterruptUrb,
                                           sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
                                           Adapter->InterruptIn.PipeHandle,
                                           Adapter->InterruptBuffer,
                                           NULL,
                                           SMSC_INTERRUPT_LENGTH,
                                           USBD_TRANSFER_DIRECTION_IN | USBD_SHORT_TRANSFER_OK,
                                           NULL);

    Status = SmscSubmitUrbAsync(Adapter,
                                &Adapter->InterruptUrb,
                                SmscInterruptComplete,
                                &Adapter->InterruptRequest);
    if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
    {
        InterlockedExchange(&Adapter->InterruptSubmitted, 0);
    }

    SmscReleaseRundown(Adapter);
    return Status;
}

static
VOID
SmscCancelPendingIo(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    PSMSC_ASYNC_REQUEST RxRequest;
    PSMSC_ASYNC_REQUEST InterruptRequest;

    RxRequest = SmscReferenceAsyncRequest(Adapter, &Adapter->RxRequest);
    InterruptRequest =
        SmscReferenceAsyncRequest(Adapter, &Adapter->InterruptRequest);

    if (KeRemoveQueueDpc(&Adapter->RxResubmitDpc))
    {
        SmscReleaseRundown(Adapter);
    }
    if (KeRemoveQueueDpc(&Adapter->InterruptResubmitDpc))
    {
        SmscReleaseRundown(Adapter);
    }

    SmscCancelAsyncRequest(RxRequest);
    SmscCancelAsyncRequest(InterruptRequest);
}

static
VOID
SmscIndicateFrame(
    _In_ PSMSC_ADAPTER Adapter,
    _In_reads_bytes_(FrameLength) PUCHAR Frame,
    _In_ ULONG FrameLength)
{
    PUCHAR FrameCopy;
    PMDL Mdl;
    PNET_BUFFER_LIST Nbl;
    ULONG ReceiveFlags;

    if (FrameLength < SMSC_ETH_HEADER_LENGTH || FrameLength > SMSC_MAX_ETHERNET_FRAME)
    {
        Adapter->RcvError++;
        return;
    }

    FrameCopy = SmscAllocate(FrameLength);
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
        SmscFree(FrameCopy);
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
        SmscFree(FrameCopy);
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
    SmscFree(FrameCopy);
}

static
VOID
SmscProcessReceiveBuffer(
    _In_ PSMSC_ADAPTER Adapter,
    _In_reads_bytes_(TransferLength) PUCHAR Buffer,
    _In_ ULONG TransferLength)
{
    ULONG Offset;

    Offset = 0;
    while (Offset + SMSC_RX_HEADER_SIZE <= TransferLength)
    {
        ULONG ReceiveStatus;
        ULONG FrameWithFcsLength;
        ULONG FrameLength;
        ULONG PaddedLength;

        ReceiveStatus = SmscReadLittleEndian32(Buffer + Offset);
        FrameWithFcsLength = (ReceiveStatus & SMSC_RX_STATUS_LENGTH) >> 16;

        if (FrameWithFcsLength < SMSC_ETH_HEADER_LENGTH + 4 ||
            FrameWithFcsLength > SMSC_MAX_RX_FRAME ||
            Offset + SMSC_RX_HEADER_SIZE + FrameWithFcsLength > TransferLength)
        {
            Adapter->RcvError++;
            break;
        }

        FrameLength = FrameWithFcsLength - 4;
        if (ReceiveStatus & SMSC_RX_STATUS_ERROR)
        {
            Adapter->RcvError++;
        }
        else
        {
            SmscIndicateFrame(Adapter,
                              Buffer + Offset + SMSC_RX_HEADER_SIZE,
                              FrameLength);
        }

        PaddedLength = (FrameWithFcsLength + 3) & ~3UL;
        Offset += SMSC_RX_HEADER_SIZE + PaddedLength;
    }
}

static
VOID
SmscQueueResubmitDpc(
    _Inout_ PSMSC_ADAPTER Adapter,
    _Inout_ PKDPC Dpc)
{
    BOOLEAN Queue;

    NdisAcquireSpinLock(&Adapter->StateLock);
    Queue = !Adapter->Halting && !Adapter->Paused && !Adapter->Resetting;
    if (Queue)
    {
        /* A queued DPC is an asynchronous adapter user in its own right. */
        SmscAcquireRundownLocked(Adapter);
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    if (Queue && !KeInsertQueueDpc(Dpc, NULL, NULL))
    {
        SmscReleaseRundown(Adapter);
    }
}

static
NTSTATUS
NTAPI
SmscRxComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PSMSC_ADAPTER Adapter;
    PSMSC_ASYNC_REQUEST Request;
    ULONG TransferLength;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    Request = (PSMSC_ASYNC_REQUEST)Context;
    InterlockedExchange(&Request->CompletionCalled, 1);
    Adapter = Request->Adapter;
    Status = Irp->IoStatus.Status;
    TransferLength = Adapter->RxUrb.UrbBulkOrInterruptTransfer.TransferBufferLength;

    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->RxRequest == Request)
    {
        Adapter->RxRequest = NULL;
    }
    NdisReleaseSpinLock(&Adapter->StateLock);
    InterlockedExchange(&Adapter->RxSubmitted, 0);

    if (NT_SUCCESS(Status) && TransferLength != 0 && SmscIsRunning(Adapter))
    {
        SmscProcessReceiveBuffer(Adapter, Adapter->RxBuffer, TransferLength);
    }
    else if (SmscIsRunning(Adapter))
    {
        Adapter->RcvError++;
    }

    SmscQueueResubmitDpc(Adapter, &Adapter->RxResubmitDpc);
    SmscDereferenceAsyncRequest(Request);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
SmscQueueLinkWork(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    BOOLEAN Queue;

    NdisAcquireSpinLock(&Adapter->StateLock);
    Queue = !Adapter->Halting &&
            !Adapter->Paused &&
            !Adapter->Resetting &&
            Adapter->LinkWorkItem &&
            Adapter->LinkWorkQueued == 0;
    if (Queue)
    {
        Adapter->LinkWorkQueued = 1;
        SmscAcquireRundownLocked(Adapter);
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    if (Queue)
    {
        NdisQueueIoWorkItem(Adapter->LinkWorkItem, SmscLinkWorkItem, Adapter);
    }
}

static
VOID
NTAPI
SmscLinkWorkItem(
    _In_ PVOID WorkItemContext,
    _In_ NDIS_HANDLE NdisIoWorkItemHandle)
{
    PSMSC_ADAPTER Adapter;
    NDIS_MEDIA_CONNECT_STATE OldState;
    NDIS_MEDIA_DUPLEX_STATE OldDuplex;
    ULONG64 OldSpeed;
    USHORT InterruptSource;
    BOOLEAN LinkChanged;

    UNREFERENCED_PARAMETER(NdisIoWorkItemHandle);

    Adapter = (PSMSC_ADAPTER)WorkItemContext;
    if (SmscIsRunning(Adapter))
    {
        NdisAcquireSpinLock(&Adapter->StateLock);
        OldState = Adapter->MediaState;
        OldDuplex = Adapter->DuplexState;
        OldSpeed = Adapter->LinkSpeed;
        NdisReleaseSpinLock(&Adapter->StateLock);

        (VOID)SmscReadPhy(Adapter, SMSC_MII_PHY_INT_SOURCE, &InterruptSource);
        SmscUpdateLinkFromPhy(Adapter);
        (VOID)SmscSetRxControl(Adapter);

        NdisAcquireSpinLock(&Adapter->StateLock);
        LinkChanged = OldState != Adapter->MediaState ||
                      OldDuplex != Adapter->DuplexState ||
                      OldSpeed != Adapter->LinkSpeed;
        NdisReleaseSpinLock(&Adapter->StateLock);
        if (LinkChanged)
        {
            SmscIndicateLinkState(Adapter);
        }
    }

    NdisAcquireSpinLock(&Adapter->StateLock);
    Adapter->LinkWorkQueued = 0;
    NdisReleaseSpinLock(&Adapter->StateLock);
    SmscReleaseRundown(Adapter);
}

static
NTSTATUS
NTAPI
SmscInterruptComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PSMSC_ADAPTER Adapter;
    PSMSC_ASYNC_REQUEST Request;
    ULONG TransferLength;
    ULONG InterruptData;

    UNREFERENCED_PARAMETER(DeviceObject);

    Request = (PSMSC_ASYNC_REQUEST)Context;
    InterlockedExchange(&Request->CompletionCalled, 1);
    Adapter = Request->Adapter;
    TransferLength = Adapter->InterruptUrb.UrbBulkOrInterruptTransfer.TransferBufferLength;

    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->InterruptRequest == Request)
    {
        Adapter->InterruptRequest = NULL;
    }
    NdisReleaseSpinLock(&Adapter->StateLock);
    InterlockedExchange(&Adapter->InterruptSubmitted, 0);

    if (NT_SUCCESS(Irp->IoStatus.Status) &&
        TransferLength >= SMSC_INTERRUPT_LENGTH &&
        SmscIsRunning(Adapter))
    {
        InterruptData = SmscReadLittleEndian32(Adapter->InterruptBuffer);
        if (InterruptData & SMSC_INT_DATA_PHY_INT)
        {
            SmscQueueLinkWork(Adapter);
        }
    }

    SmscQueueResubmitDpc(Adapter, &Adapter->InterruptResubmitDpc);
    SmscDereferenceAsyncRequest(Request);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
SmscCompleteSendList(
    _In_ PSMSC_ADAPTER Adapter,
    _In_opt_ PNET_BUFFER_LIST NetBufferLists)
{
    ULONG CompleteFlags;

    if (!NetBufferLists)
    {
        return;
    }

    CompleteFlags = (KeGetCurrentIrql() == DISPATCH_LEVEL) ?
        NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0;
    NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                    NetBufferLists,
                                    CompleteFlags);
}

static
NDIS_STATUS
SmscStoppedSendStatusLocked(
    _In_ PSMSC_ADAPTER Adapter)
{
    if (Adapter->Halting)
    {
        return NDIS_STATUS_FAILURE;
    }
    if (Adapter->Resetting)
    {
        return NDIS_STATUS_RESET_IN_PROGRESS;
    }
    if (Adapter->Paused)
    {
        return NDIS_STATUS_PAUSED;
    }

    return NDIS_STATUS_REQUEST_ABORTED;
}

static
PNET_BUFFER_LIST
SmscDetachActiveSendLocked(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    PNET_BUFFER_LIST Nbl;

    Nbl = Adapter->TxActiveNbl;
    if (!Nbl)
    {
        return NULL;
    }

    NET_BUFFER_LIST_STATUS(Nbl) = Adapter->TxActiveStatus;
    Adapter->TxActiveNbl = NULL;
    Adapter->TxActiveNetBuffer = NULL;
    Adapter->TxBusy = FALSE;
    Adapter->TxAbortActive = FALSE;
    Adapter->TxNeedsZlp = FALSE;
    Adapter->TxSendingZlp = FALSE;
    Adapter->TxProgress++;
    return Nbl;
}

static
VOID
SmscCompleteStoppedActiveSend(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    PNET_BUFFER_LIST Nbl;

    Nbl = NULL;
    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->TxActiveNbl &&
        !Adapter->TxRequest &&
        !Adapter->TxPumpActive &&
        (Adapter->Halting || Adapter->Paused || Adapter->Resetting ||
         Adapter->TxAbortActive))
    {
        Adapter->TxActiveNetBuffer = NULL;
        Nbl = SmscDetachActiveSendLocked(Adapter);
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    SmscCompleteSendList(Adapter, Nbl);
}

static
NTSTATUS
NTAPI
SmscTxComplete(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context)
{
    PSMSC_ADAPTER Adapter;
    PSMSC_ASYNC_REQUEST Request;
    NTSTATUS CompletionStatus;

    UNREFERENCED_PARAMETER(DeviceObject);

    Request = (PSMSC_ASYNC_REQUEST)Context;
    InterlockedExchange(&Request->CompletionCalled, 1);
    Adapter = Request->Adapter;
    CompletionStatus = Irp->IoStatus.Status;

    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->TxRequest == Request)
    {
        Adapter->TxRequest = NULL;
    }
    Adapter->TxProgress++;

    if (Adapter->TxActiveNbl)
    {
        if (Adapter->Halting || Adapter->Paused || Adapter->Resetting ||
            Adapter->TxAbortActive)
        {
            if (Adapter->Halting || Adapter->Paused || Adapter->Resetting)
            {
                Adapter->TxActiveStatus = SmscStoppedSendStatusLocked(Adapter);
            }
            Adapter->TxActiveNetBuffer = NULL;
            Adapter->TxNeedsZlp = FALSE;
            Adapter->TxSendingZlp = FALSE;
        }
        else if (NT_SUCCESS(CompletionStatus) &&
                 Adapter->TxNeedsZlp &&
                 !Adapter->TxSendingZlp)
        {
            Adapter->TxSendingZlp = TRUE;
        }
        else
        {
            if (NT_SUCCESS(CompletionStatus))
            {
                Adapter->XmitOk++;
            }
            else
            {
                if (Adapter->TxActiveStatus == NDIS_STATUS_SUCCESS)
                {
                    Adapter->TxActiveStatus = NDIS_STATUS_FAILURE;
                }
                Adapter->XmitError++;
            }

            if (Adapter->TxActiveNetBuffer)
            {
                Adapter->TxActiveNetBuffer =
                    NET_BUFFER_NEXT_NB(Adapter->TxActiveNetBuffer);
            }
            Adapter->TxNeedsZlp = FALSE;
            Adapter->TxSendingZlp = FALSE;
        }
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    SmscRunTxPump(Adapter);
    SmscCompleteStoppedActiveSend(Adapter);
    SmscDereferenceAsyncRequest(Request);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
VOID
NTAPI
SmscRxResubmitDpc(
    _In_ PKDPC Dpc,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Arg1,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    (VOID)SmscStartBulkRead((PSMSC_ADAPTER)Context);
    SmscReleaseRundown((PSMSC_ADAPTER)Context);
}

static
VOID
NTAPI
SmscInterruptResubmitDpc(
    _In_ PKDPC Dpc,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Context,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Arg1,
    _In_reads_opt_(_Inexpressible_("varies")) PVOID Arg2)
{
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Arg1);
    UNREFERENCED_PARAMETER(Arg2);
    (VOID)SmscStartInterruptRead((PSMSC_ADAPTER)Context);
    SmscReleaseRundown((PSMSC_ADAPTER)Context);
}

static
NDIS_STATUS
SmscCopyNetBuffer(
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
VOID
SmscRunTxPump(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER NetBuffer;
    ULONG FrameLength;
    ULONG TransferLength;
    ULONG TxCommandA;
    ULONG TxCommandB;
    BOOLEAN SendingZlp;
    NDIS_STATUS NdisStatus;
    NTSTATUS Status;

    /* One pump serializes the shared TX buffer across all queued NBLs/NBs. */
    if (!SmscTryAcquireRundown(Adapter, TRUE))
    {
        return;
    }

    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->TxPumpActive)
    {
        NdisReleaseSpinLock(&Adapter->StateLock);
        SmscReleaseRundown(Adapter);
        return;
    }
    Adapter->TxPumpActive = TRUE;
    NdisReleaseSpinLock(&Adapter->StateLock);

    for (;;)
    {
        Nbl = NULL;
        NetBuffer = NULL;
        SendingZlp = FALSE;

        NdisAcquireSpinLock(&Adapter->StateLock);
        if (Adapter->Halting || Adapter->Paused || Adapter->Resetting)
        {
            if (Adapter->TxActiveNbl)
            {
                Adapter->TxActiveStatus = SmscStoppedSendStatusLocked(Adapter);
                Adapter->TxAbortActive = TRUE;
                Adapter->TxActiveNetBuffer = NULL;
                if (!Adapter->TxRequest)
                {
                    Nbl = SmscDetachActiveSendLocked(Adapter);
                }
            }

            Adapter->TxPumpActive = FALSE;
            NdisReleaseSpinLock(&Adapter->StateLock);
            SmscCompleteSendList(Adapter, Nbl);
            break;
        }

        if (Adapter->TxRequest)
        {
            Adapter->TxPumpActive = FALSE;
            NdisReleaseSpinLock(&Adapter->StateLock);
            break;
        }

        if (Adapter->TxActiveNbl && Adapter->TxAbortActive)
        {
            Adapter->TxActiveNetBuffer = NULL;
        }

        if (!Adapter->TxActiveNbl && Adapter->TxQueueHead)
        {
            Adapter->TxActiveNbl = Adapter->TxQueueHead;
            Adapter->TxQueueHead =
                NET_BUFFER_LIST_NEXT_NBL(Adapter->TxActiveNbl);
            if (!Adapter->TxQueueHead)
            {
                Adapter->TxQueueTail = NULL;
            }
            NET_BUFFER_LIST_NEXT_NBL(Adapter->TxActiveNbl) = NULL;
            Adapter->TxActiveNetBuffer =
                NET_BUFFER_LIST_FIRST_NB(Adapter->TxActiveNbl);
            Adapter->TxActiveStatus = NDIS_STATUS_SUCCESS;
            Adapter->TxAbortActive = FALSE;
            Adapter->TxBusy = TRUE;
            Adapter->TxProgress++;

            if (!Adapter->TxActiveNetBuffer)
            {
                Adapter->TxActiveStatus = NDIS_STATUS_INVALID_DATA;
            }
            else if (Adapter->MediaState != MediaConnectStateConnected)
            {
                Adapter->TxActiveStatus = NDIS_STATUS_FAILURE;
                Adapter->TxActiveNetBuffer = NULL;
                Adapter->XmitError++;
            }
        }

        if (!Adapter->TxActiveNbl)
        {
            Adapter->TxPumpActive = FALSE;
            NdisReleaseSpinLock(&Adapter->StateLock);
            break;
        }

        if (!Adapter->TxActiveNetBuffer)
        {
            Nbl = SmscDetachActiveSendLocked(Adapter);
            NdisReleaseSpinLock(&Adapter->StateLock);
            SmscCompleteSendList(Adapter, Nbl);
            continue;
        }

        NetBuffer = Adapter->TxActiveNetBuffer;
        SendingZlp = Adapter->TxSendingZlp;
        NdisReleaseSpinLock(&Adapter->StateLock);

        if (SendingZlp)
        {
            TransferLength = 0;
        }
        else
        {
            FrameLength = NetBuffer->DataLength;
            if (FrameLength < SMSC_ETH_HEADER_LENGTH ||
                FrameLength > SMSC_MAX_ETHERNET_FRAME)
            {
                NdisStatus = NDIS_STATUS_INVALID_LENGTH;
            }
            else
            {
                TxCommandA = (FrameLength & SMSC_TX_CMD_A_LENGTH_MASK) |
                             SMSC_TX_CMD_A_FIRST |
                             SMSC_TX_CMD_A_LAST;
                TxCommandB = FrameLength & SMSC_TX_CMD_B_LENGTH_MASK;
                SmscWriteLittleEndian32(Adapter->TxBuffer, TxCommandA);
                SmscWriteLittleEndian32(Adapter->TxBuffer + sizeof(ULONG),
                                        TxCommandB);

                NdisStatus = SmscCopyNetBuffer(
                    NetBuffer,
                    Adapter->TxBuffer + SMSC_TX_HEADER_SIZE,
                    SMSC_MAX_ETHERNET_FRAME);
            }

            if (NdisStatus != NDIS_STATUS_SUCCESS)
            {
                NdisAcquireSpinLock(&Adapter->StateLock);
                if (Adapter->TxActiveNetBuffer == NetBuffer &&
                    !Adapter->TxRequest)
                {
                    if (Adapter->TxActiveStatus == NDIS_STATUS_SUCCESS)
                    {
                        Adapter->TxActiveStatus = NdisStatus;
                    }
                    Adapter->XmitError++;
                    Adapter->TxActiveNetBuffer = NET_BUFFER_NEXT_NB(NetBuffer);
                    Adapter->TxProgress++;
                }
                NdisReleaseSpinLock(&Adapter->StateLock);
                continue;
            }

            TransferLength = SMSC_TX_HEADER_SIZE + FrameLength;
        }

        RtlZeroMemory(&Adapter->TxUrb, sizeof(Adapter->TxUrb));
        UsbBuildInterruptOrBulkTransferRequest(
            &Adapter->TxUrb,
            sizeof(struct _URB_BULK_OR_INTERRUPT_TRANSFER),
            Adapter->BulkOut.PipeHandle,
            Adapter->TxBuffer,
            NULL,
            TransferLength,
            USBD_TRANSFER_DIRECTION_OUT,
            NULL);

        NdisAcquireSpinLock(&Adapter->StateLock);
        if (Adapter->Halting || Adapter->Paused || Adapter->Resetting ||
            Adapter->TxAbortActive ||
            Adapter->TxActiveNetBuffer != NetBuffer ||
            Adapter->TxRequest)
        {
            if (Adapter->Halting || Adapter->Paused || Adapter->Resetting)
            {
                Adapter->TxActiveStatus = SmscStoppedSendStatusLocked(Adapter);
                Adapter->TxAbortActive = TRUE;
                Adapter->TxActiveNetBuffer = NULL;
            }
            NdisReleaseSpinLock(&Adapter->StateLock);
            continue;
        }

        if (!SendingZlp)
        {
            Adapter->TxNeedsZlp =
                Adapter->BulkOut.MaximumPacketSize != 0 &&
                (TransferLength % Adapter->BulkOut.MaximumPacketSize) == 0;
            Adapter->TxSendingZlp = FALSE;
        }
        NdisReleaseSpinLock(&Adapter->StateLock);

        Status = SmscSubmitUrbAsync(Adapter,
                                    &Adapter->TxUrb,
                                    SmscTxComplete,
                                    &Adapter->TxRequest);
        if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
        {
            NdisAcquireSpinLock(&Adapter->StateLock);
            if (Adapter->TxActiveNbl &&
                Adapter->TxActiveNetBuffer == NetBuffer &&
                !Adapter->TxRequest)
            {
                Adapter->TxProgress++;
                if (Adapter->Halting || Adapter->Paused || Adapter->Resetting ||
                    Adapter->TxAbortActive)
                {
                    if (Adapter->Halting || Adapter->Paused || Adapter->Resetting)
                    {
                        Adapter->TxActiveStatus =
                            SmscStoppedSendStatusLocked(Adapter);
                    }
                    Adapter->TxActiveNetBuffer = NULL;
                }
                else
                {
                    if (Adapter->TxActiveStatus == NDIS_STATUS_SUCCESS)
                    {
                        Adapter->TxActiveStatus =
                            (Status == STATUS_INSUFFICIENT_RESOURCES) ?
                            NDIS_STATUS_RESOURCES : NDIS_STATUS_FAILURE;
                    }
                    Adapter->XmitError++;
                    Adapter->TxActiveNetBuffer = NET_BUFFER_NEXT_NB(NetBuffer);
                }
                Adapter->TxNeedsZlp = FALSE;
                Adapter->TxSendingZlp = FALSE;
            }
            NdisReleaseSpinLock(&Adapter->StateLock);
            continue;
        }

        NdisAcquireSpinLock(&Adapter->StateLock);
        if (Adapter->TxRequest)
        {
            Adapter->TxPumpActive = FALSE;
            NdisReleaseSpinLock(&Adapter->StateLock);
            break;
        }
        NdisReleaseSpinLock(&Adapter->StateLock);
        /* The request completed inline and advanced the active NET_BUFFER. */
    }

    SmscReleaseRundown(Adapter);
}

static
VOID
NTAPI
SmscMiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PSMSC_ADAPTER Adapter;
    PNET_BUFFER_LIST Current;
    PNET_BUFFER_LIST Next;
    PNET_BUFFER_LIST RejectedHead;
    PNET_BUFFER_LIST RejectedTail;
    BOOLEAN RundownAcquired;
    NDIS_STATUS RejectStatus;
    ULONG CompleteFlags;

    Adapter = (PSMSC_ADAPTER)MiniportAdapterContext;
    RundownAcquired = SmscTryAcquireRundown(Adapter, TRUE);

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
        if (RundownAcquired)
        {
            SmscReleaseRundown(Adapter);
        }
        return;
    }

    RejectedHead = NULL;
    RejectedTail = NULL;

    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->Halting)
    {
        RejectStatus = NDIS_STATUS_FAILURE;
    }
    else if (Adapter->Paused)
    {
        RejectStatus = NDIS_STATUS_PAUSED;
    }
    else if (Adapter->Resetting)
    {
        RejectStatus = NDIS_STATUS_RESET_IN_PROGRESS;
    }
    else
    {
        RejectStatus = NDIS_STATUS_SUCCESS;
    }

    for (Current = NetBufferLists; Current; Current = Next)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Current);
        NET_BUFFER_LIST_NEXT_NBL(Current) = NULL;

        if (RejectStatus == NDIS_STATUS_SUCCESS)
        {
            if (Adapter->TxQueueTail)
            {
                NET_BUFFER_LIST_NEXT_NBL(Adapter->TxQueueTail) = Current;
            }
            else
            {
                Adapter->TxQueueHead = Current;
            }
            Adapter->TxQueueTail = Current;
        }
        else
        {
            NET_BUFFER_LIST_STATUS(Current) = RejectStatus;
            if (RejectedTail)
            {
                NET_BUFFER_LIST_NEXT_NBL(RejectedTail) = Current;
            }
            else
            {
                RejectedHead = Current;
            }
            RejectedTail = Current;
        }
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    SmscCompleteSendList(Adapter, RejectedHead);
    if (RejectStatus == NDIS_STATUS_SUCCESS)
    {
        SmscRunTxPump(Adapter);
    }
    if (RundownAcquired)
    {
        SmscReleaseRundown(Adapter);
    }
}

static
VOID
NTAPI
SmscMiniportReturnNetBufferLists(
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
SmscMiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    PSMSC_ADAPTER Adapter;
    PSMSC_ASYNC_REQUEST TxRequest;
    PNET_BUFFER_LIST CancelledHead;
    PNET_BUFFER_LIST CancelledTail;
    PNET_BUFFER_LIST Current;
    PNET_BUFFER_LIST Next;
    PNET_BUFFER_LIST Previous;

    Adapter = (PSMSC_ADAPTER)MiniportAdapterContext;
    if (!Adapter || !SmscTryAcquireRundown(Adapter, TRUE))
    {
        return;
    }
    TxRequest = NULL;
    CancelledHead = NULL;
    CancelledTail = NULL;
    Previous = NULL;

    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->TxActiveNbl &&
        NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Adapter->TxActiveNbl) == CancelId)
    {
        Adapter->TxActiveStatus = NDIS_STATUS_REQUEST_ABORTED;
        Adapter->TxAbortActive = TRUE;
        if (Adapter->TxRequest)
        {
            TxRequest = Adapter->TxRequest;
            InterlockedIncrement(&TxRequest->ReferenceCount);
        }
    }

    Current = Adapter->TxQueueHead;
    while (Current)
    {
        Next = NET_BUFFER_LIST_NEXT_NBL(Current);
        if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Current) == CancelId)
        {
            if (Previous)
            {
                NET_BUFFER_LIST_NEXT_NBL(Previous) = Next;
            }
            else
            {
                Adapter->TxQueueHead = Next;
            }
            if (Adapter->TxQueueTail == Current)
            {
                Adapter->TxQueueTail = Previous;
            }

            NET_BUFFER_LIST_NEXT_NBL(Current) = NULL;
            NET_BUFFER_LIST_STATUS(Current) = NDIS_STATUS_REQUEST_ABORTED;
            if (CancelledTail)
            {
                NET_BUFFER_LIST_NEXT_NBL(CancelledTail) = Current;
            }
            else
            {
                CancelledHead = Current;
            }
            CancelledTail = Current;
        }
        else
        {
            Previous = Current;
        }
        Current = Next;
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    SmscCancelAsyncRequest(TxRequest);
    SmscCompleteSendList(Adapter, CancelledHead);
    SmscRunTxPump(Adapter);
    SmscReleaseRundown(Adapter);
}

static
BOOLEAN
NTAPI
SmscMiniportCheckForHang(
    _In_ NDIS_HANDLE MiniportAdapterContext)
{
    PSMSC_ADAPTER Adapter;
    BOOLEAN Hung;

    Adapter = (PSMSC_ADAPTER)MiniportAdapterContext;
    if (!Adapter)
    {
        return FALSE;
    }
    if (!SmscTryAcquireRundown(Adapter, TRUE))
    {
        return FALSE;
    }

    SmscQueueLinkWork(Adapter);

    Hung = FALSE;
    NdisAcquireSpinLock(&Adapter->StateLock);
    if (!Adapter->Halting && !Adapter->Paused && !Adapter->Resetting &&
        Adapter->TxBusy && Adapter->TxRequest)
    {
        if (Adapter->TxProgress != Adapter->TxLastProgress)
        {
            Adapter->TxLastProgress = Adapter->TxProgress;
            Adapter->TxStallChecks = 0;
        }
        else
        {
            if (Adapter->TxStallChecks < SMSC_TX_HANG_CHECK_LIMIT)
            {
                Adapter->TxStallChecks++;
            }
            Hung = Adapter->TxStallChecks >= SMSC_TX_HANG_CHECK_LIMIT;
        }
    }
    else
    {
        Adapter->TxLastProgress = Adapter->TxProgress;
        Adapter->TxStallChecks = 0;
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    SmscReleaseRundown(Adapter);
    return Hung;
}

static
VOID
SmscStopTransmits(
    _Inout_ PSMSC_ADAPTER Adapter,
    _In_ NDIS_STATUS StopStatus)
{
    PSMSC_ASYNC_REQUEST TxRequest;
    PNET_BUFFER_LIST CompleteHead;
    PNET_BUFFER_LIST CompleteTail;
    PNET_BUFFER_LIST Current;

    TxRequest = NULL;
    CompleteHead = NULL;
    CompleteTail = NULL;

    NdisAcquireSpinLock(&Adapter->StateLock);
    Current = Adapter->TxQueueHead;
    Adapter->TxQueueHead = NULL;
    Adapter->TxQueueTail = NULL;
    while (Current)
    {
        PNET_BUFFER_LIST Next;

        Next = NET_BUFFER_LIST_NEXT_NBL(Current);
        NET_BUFFER_LIST_NEXT_NBL(Current) = NULL;
        NET_BUFFER_LIST_STATUS(Current) = StopStatus;
        if (CompleteTail)
        {
            NET_BUFFER_LIST_NEXT_NBL(CompleteTail) = Current;
        }
        else
        {
            CompleteHead = Current;
        }
        CompleteTail = Current;
        Current = Next;
    }

    if (Adapter->TxActiveNbl)
    {
        Adapter->TxActiveStatus = StopStatus;
        Adapter->TxAbortActive = TRUE;
        if (Adapter->TxRequest)
        {
            TxRequest = Adapter->TxRequest;
            InterlockedIncrement(&TxRequest->ReferenceCount);
        }
        else if (!Adapter->TxPumpActive)
        {
            PNET_BUFFER_LIST ActiveNbl;

            Adapter->TxActiveNetBuffer = NULL;
            ActiveNbl = SmscDetachActiveSendLocked(Adapter);
            NET_BUFFER_LIST_NEXT_NBL(ActiveNbl) = CompleteHead;
            CompleteHead = ActiveNbl;
            if (!CompleteTail)
            {
                CompleteTail = ActiveNbl;
            }
        }
    }
    NdisReleaseSpinLock(&Adapter->StateLock);

    SmscCancelAsyncRequest(TxRequest);
    SmscCompleteSendList(Adapter, CompleteHead);
}

static
VOID
SmscWaitForRundown(
    _In_ PSMSC_ADAPTER Adapter)
{
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    KeWaitForSingleObject(&Adapter->RemoveEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
}

static
NTSTATUS
SmscResetPipe(
    _In_ PSMSC_ADAPTER Adapter,
    _In_ USBD_PIPE_HANDLE PipeHandle)
{
    URB Urb;

    if (!PipeHandle)
    {
        return STATUS_SUCCESS;
    }

    RtlZeroMemory(&Urb, sizeof(Urb));
    Urb.UrbPipeRequest.Hdr.Length = sizeof(struct _URB_PIPE_REQUEST);
    Urb.UrbPipeRequest.Hdr.Function =
        URB_FUNCTION_SYNC_RESET_PIPE_AND_CLEAR_STALL;
    Urb.UrbPipeRequest.PipeHandle = PipeHandle;
    return SmscSubmitUrbSync(Adapter->LowerDeviceObject, &Urb);
}

static
VOID
SmscDisableHardware(
    _Inout_ PSMSC_ADAPTER Adapter)
{
    ULONG MacControl;

    if (!Adapter->LowerDeviceObject || KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return;
    }

    KeWaitForSingleObject(&Adapter->MacControlLock,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);
    MacControl = Adapter->MacControl &
                 ~(SMSC_MAC_CR_TXEN | SMSC_MAC_CR_RXEN);
    if (NT_SUCCESS(SmscWriteRegister(Adapter, SMSC_MAC_CR, MacControl)))
    {
        Adapter->MacControl = MacControl;
    }
    (VOID)SmscWriteRegister(Adapter, SMSC_TX_CFG, 0);
    KeReleaseMutex(&Adapter->MacControlLock, FALSE);
}

static
NDIS_STATUS
NTAPI
SmscMiniportReset(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _Out_ PBOOLEAN AddressingReset)
{
    PSMSC_ADAPTER Adapter;
    BOOLEAN RestartDataPath;
    NTSTATUS PipeStatus;
    NTSTATUS Status;

    Adapter = (PSMSC_ADAPTER)MiniportAdapterContext;
    *AddressingReset = FALSE;
    if (!Adapter || KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return NDIS_STATUS_NOT_RESETTABLE;
    }

    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->Halting || Adapter->Resetting)
    {
        NdisReleaseSpinLock(&Adapter->StateLock);
        return NDIS_STATUS_RESET_IN_PROGRESS;
    }
    Adapter->Resetting = TRUE;
    RestartDataPath = !Adapter->Paused;
    NdisReleaseSpinLock(&Adapter->StateLock);

    SmscStopTransmits(Adapter, NDIS_STATUS_RESET_IN_PROGRESS);
    SmscCancelPendingIo(Adapter);
    SmscWaitForRundown(Adapter);
    SmscCompleteStoppedActiveSend(Adapter);

    Status = STATUS_SUCCESS;
    PipeStatus = SmscResetPipe(Adapter, Adapter->BulkOut.PipeHandle);
    if (!NT_SUCCESS(PipeStatus))
    {
        Status = PipeStatus;
    }
    PipeStatus = SmscResetPipe(Adapter, Adapter->BulkIn.PipeHandle);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(PipeStatus))
    {
        Status = PipeStatus;
    }
    PipeStatus = SmscResetPipe(Adapter, Adapter->InterruptIn.PipeHandle);
    if (NT_SUCCESS(Status) && !NT_SUCCESS(PipeStatus))
    {
        Status = PipeStatus;
    }

    if (NT_SUCCESS(Status))
    {
        Status = SmscInitializeHardware(Adapter);
    }
    if (NT_SUCCESS(Status))
    {
        SmscUpdateLinkFromPhy(Adapter);
    }

    NdisAcquireSpinLock(&Adapter->StateLock);
    Adapter->Resetting = FALSE;
    Adapter->TxLastProgress = Adapter->TxProgress;
    Adapter->TxStallChecks = 0;
    NdisReleaseSpinLock(&Adapter->StateLock);

    if (NT_SUCCESS(Status) && RestartDataPath)
    {
        Status = SmscStartBulkRead(Adapter);
        if ((NT_SUCCESS(Status) || Status == STATUS_PENDING) &&
            Adapter->InterruptIn.PipeHandle)
        {
            Status = SmscStartInterruptRead(Adapter);
        }
        if (NT_SUCCESS(Status) || Status == STATUS_PENDING)
        {
            SmscQueueLinkWork(Adapter);
            return NDIS_STATUS_SUCCESS;
        }
    }
    else if (NT_SUCCESS(Status))
    {
        return NDIS_STATUS_SUCCESS;
    }

    return NDIS_STATUS_FAILURE;
}

static
NDIS_STATUS
SmscOidCopy(
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
SmscQueryOid(
    _In_ PSMSC_ADAPTER Adapter,
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
        OID_GEN_MAXIMUM_LOOKAHEAD,
        OID_GEN_CURRENT_LOOKAHEAD,
        OID_GEN_LINK_SPEED,
        OID_GEN_CURRENT_PACKET_FILTER,
        OID_GEN_MAXIMUM_TOTAL_SIZE,
        OID_GEN_MEDIA_CONNECT_STATUS,
        OID_GEN_XMIT_OK,
        OID_GEN_RCV_OK,
        OID_GEN_XMIT_ERROR,
        OID_GEN_RCV_ERROR,
        OID_GEN_RCV_NO_BUFFER,
        OID_GEN_STATISTICS,
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
    NDIS_STATISTICS_INFO Statistics;
    ULONG Value;

    *BytesWritten = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_SUPPORTED_LIST:
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               SupportedOids,
                               sizeof(SupportedOids),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_HARDWARE_STATUS:
            HardwareStatus = NdisHardwareStatusReady;
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &HardwareStatus,
                               sizeof(HardwareStatus),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_MEDIA_SUPPORTED:
        case OID_GEN_MEDIA_IN_USE:
            Medium = NdisMedium802_3;
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Medium,
                               sizeof(Medium),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            Value = SMSC_ETH_MTU;
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_MAXIMUM_LOOKAHEAD:
            Value = SMSC_ETH_MTU;
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_CURRENT_LOOKAHEAD:
            NdisAcquireSpinLock(&Adapter->StateLock);
            Value = Adapter->CurrentLookahead;
            NdisReleaseSpinLock(&Adapter->StateLock);
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_LINK_SPEED:
            Value = (ULONG)(Adapter->LinkSpeed / 100);
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_CURRENT_PACKET_FILTER:
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->PacketFilter,
                               sizeof(Adapter->PacketFilter),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
            Value = SMSC_MAX_ETHERNET_FRAME;
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_MEDIA_CONNECT_STATUS:
            Value = (Adapter->MediaState == MediaConnectStateConnected) ?
                NdisMediaStateConnected : NdisMediaStateDisconnected;
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_XMIT_OK:
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->XmitOk,
                               sizeof(Adapter->XmitOk),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_RCV_OK:
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->RcvOk,
                               sizeof(Adapter->RcvOk),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_XMIT_ERROR:
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->XmitError,
                               sizeof(Adapter->XmitError),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_RCV_ERROR:
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->RcvError,
                               sizeof(Adapter->RcvError),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_RCV_NO_BUFFER:
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Adapter->RcvNoBuffer,
                               sizeof(Adapter->RcvNoBuffer),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_STATISTICS:
            RtlZeroMemory(&Statistics, sizeof(Statistics));
            Statistics.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Statistics.Header.Revision = NDIS_STATISTICS_INFO_REVISION_1;
            Statistics.Header.Size = sizeof(Statistics);
            Statistics.SupportedStatistics =
                NDIS_STATISTICS_FLAGS_VALID_RCV_DISCARDS |
                NDIS_STATISTICS_FLAGS_VALID_RCV_ERROR |
                NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR;
            Statistics.ifInDiscards = Adapter->RcvNoBuffer;
            Statistics.ifInErrors = Adapter->RcvError;
            Statistics.ifOutErrors = Adapter->XmitError;
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Statistics,
                               sizeof(Statistics),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_LINK_STATE:
            RtlZeroMemory(&LinkState, sizeof(LinkState));
            LinkState.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            LinkState.Header.Revision = NDIS_LINK_STATE_REVISION_1;
            LinkState.Header.Size = sizeof(LinkState);
            LinkState.MediaConnectState = Adapter->MediaState;
            LinkState.MediaDuplexState = Adapter->DuplexState;
            LinkState.XmitLinkSpeed = Adapter->LinkSpeed;
            LinkState.RcvLinkSpeed = Adapter->LinkSpeed;
            LinkState.PauseFunctions = NdisPauseFunctionsUnsupported;
            LinkState.AutoNegotiationFlags =
                NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED |
                NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED |
                NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &LinkState,
                               sizeof(LinkState),
                               BytesWritten,
                               BytesNeeded);

        case OID_802_3_CURRENT_ADDRESS:
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               Adapter->CurrentMacAddress,
                               SMSC_ETH_ADDRESS_LENGTH,
                               BytesWritten,
                               BytesNeeded);

        case OID_802_3_PERMANENT_ADDRESS:
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               Adapter->PermanentMacAddress,
                               SMSC_ETH_ADDRESS_LENGTH,
                               BytesWritten,
                               BytesNeeded);

        case OID_802_3_MULTICAST_LIST:
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               Adapter->MulticastList,
                               Adapter->MulticastAddressCount * SMSC_ETH_ADDRESS_LENGTH,
                               BytesWritten,
                               BytesNeeded);

        case OID_802_3_MAXIMUM_LIST_SIZE:
            Value = 64;
            return SmscOidCopy(InformationBuffer,
                               InformationBufferLength,
                               &Value,
                               sizeof(Value),
                               BytesWritten,
                               BytesNeeded);

        case OID_GEN_PHYSICAL_MEDIUM:
            Value = NdisPhysicalMedium802_3;
            return SmscOidCopy(InformationBuffer,
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
SmscSetOid(
    _Inout_ PSMSC_ADAPTER Adapter,
    _In_ NDIS_OID Oid,
    _In_reads_bytes_(InformationBufferLength) PVOID InformationBuffer,
    _In_ UINT InformationBufferLength,
    _Out_ PUINT BytesRead,
    _Out_ PUINT BytesNeeded)
{
    ULONG Index;
    ULONG Lookahead;
    ULONG PacketFilter;
    const ULONG SupportedFilters = NDIS_PACKET_TYPE_DIRECTED |
                                   NDIS_PACKET_TYPE_MULTICAST |
                                   NDIS_PACKET_TYPE_ALL_MULTICAST |
                                   NDIS_PACKET_TYPE_BROADCAST |
                                   NDIS_PACKET_TYPE_PROMISCUOUS;

    *BytesRead = 0;
    *BytesNeeded = 0;

    switch (Oid)
    {
        case OID_GEN_CURRENT_LOOKAHEAD:
            if (InformationBufferLength < sizeof(Lookahead))
            {
                *BytesNeeded = sizeof(Lookahead);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            RtlCopyMemory(&Lookahead, InformationBuffer, sizeof(Lookahead));
            if (Lookahead > SMSC_ETH_MTU)
            {
                return NDIS_STATUS_INVALID_DATA;
            }

            NdisAcquireSpinLock(&Adapter->StateLock);
            Adapter->CurrentLookahead = Lookahead;
            NdisReleaseSpinLock(&Adapter->StateLock);
            *BytesRead = sizeof(Lookahead);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_PACKET_FILTER:
            if (InformationBufferLength < sizeof(PacketFilter))
            {
                *BytesNeeded = sizeof(PacketFilter);
                return NDIS_STATUS_INVALID_LENGTH;
            }

            RtlCopyMemory(&PacketFilter, InformationBuffer, sizeof(PacketFilter));
            if (PacketFilter & ~SupportedFilters)
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }

            NdisAcquireSpinLock(&Adapter->StateLock);
            Adapter->PacketFilter = PacketFilter;
            NdisReleaseSpinLock(&Adapter->StateLock);
            *BytesRead = sizeof(PacketFilter);
            if (KeGetCurrentIrql() == PASSIVE_LEVEL)
            {
                return NT_SUCCESS(SmscSetRxControl(Adapter)) ?
                    NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
            }

            SmscQueueLinkWork(Adapter);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_MULTICAST_LIST:
            if ((InformationBufferLength % SMSC_ETH_ADDRESS_LENGTH) != 0 ||
                InformationBufferLength > 64 * SMSC_ETH_ADDRESS_LENGTH)
            {
                *BytesNeeded = 64 * SMSC_ETH_ADDRESS_LENGTH;
                return NDIS_STATUS_INVALID_LENGTH;
            }

            for (Index = 0;
                 Index < InformationBufferLength / SMSC_ETH_ADDRESS_LENGTH;
                 Index++)
            {
                PUCHAR Address;

                Address = (PUCHAR)InformationBuffer +
                          Index * SMSC_ETH_ADDRESS_LENGTH;
                if (!(Address[0] & 0x01))
                {
                    return NDIS_STATUS_INVALID_DATA;
                }
            }

            NdisAcquireSpinLock(&Adapter->StateLock);
            RtlCopyMemory(Adapter->MulticastList,
                          InformationBuffer,
                          InformationBufferLength);
            Adapter->MulticastAddressCount =
                InformationBufferLength / SMSC_ETH_ADDRESS_LENGTH;
            NdisReleaseSpinLock(&Adapter->StateLock);
            *BytesRead = InformationBufferLength;
            if (KeGetCurrentIrql() == PASSIVE_LEVEL)
            {
                return NT_SUCCESS(SmscSetRxControl(Adapter)) ?
                    NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
            }

            SmscQueueLinkWork(Adapter);
            return NDIS_STATUS_SUCCESS;

        default:
            return NDIS_STATUS_NOT_SUPPORTED;
    }
}

static
NDIS_STATUS
NTAPI
SmscMiniportOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PSMSC_ADAPTER Adapter;

    Adapter = (PSMSC_ADAPTER)MiniportAdapterContext;

    if (OidRequest->RequestType == NdisRequestQueryInformation ||
        OidRequest->RequestType == NdisRequestQueryStatistics)
    {
        return SmscQueryOid(Adapter,
                            OidRequest->DATA.QUERY_INFORMATION.Oid,
                            OidRequest->DATA.QUERY_INFORMATION.InformationBuffer,
                            OidRequest->DATA.QUERY_INFORMATION.InformationBufferLength,
                            &OidRequest->DATA.QUERY_INFORMATION.BytesWritten,
                            &OidRequest->DATA.QUERY_INFORMATION.BytesNeeded);
    }

    if (OidRequest->RequestType == NdisRequestSetInformation)
    {
        return SmscSetOid(Adapter,
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
SmscMiniportCancelOidRequest(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID RequestId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(RequestId);
}

static
NDIS_STATUS
NTAPI
SmscMiniportPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PSMSC_ADAPTER Adapter;

    UNREFERENCED_PARAMETER(PauseParameters);

    Adapter = (PSMSC_ADAPTER)MiniportAdapterContext;
    if (!Adapter || KeGetCurrentIrql() != PASSIVE_LEVEL)
    {
        return NDIS_STATUS_FAILURE;
    }

    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->Halting || Adapter->Resetting)
    {
        NdisReleaseSpinLock(&Adapter->StateLock);
        return NDIS_STATUS_FAILURE;
    }
    Adapter->Paused = TRUE;
    NdisReleaseSpinLock(&Adapter->StateLock);

    /* Pause completes synchronously only after every data-path user exits. */
    SmscStopTransmits(Adapter, NDIS_STATUS_PAUSED);
    SmscCancelPendingIo(Adapter);
    SmscWaitForRundown(Adapter);
    SmscCompleteStoppedActiveSend(Adapter);
    return NDIS_STATUS_SUCCESS;
}

static
NDIS_STATUS
NTAPI
SmscMiniportRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PSMSC_ADAPTER Adapter;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RestartParameters);

    Adapter = (PSMSC_ADAPTER)MiniportAdapterContext;
    if (!Adapter)
    {
        return NDIS_STATUS_FAILURE;
    }

    NdisAcquireSpinLock(&Adapter->StateLock);
    if (Adapter->Halting || Adapter->Resetting)
    {
        NdisReleaseSpinLock(&Adapter->StateLock);
        return NDIS_STATUS_FAILURE;
    }
    Adapter->Paused = FALSE;
    Adapter->TxLastProgress = Adapter->TxProgress;
    Adapter->TxStallChecks = 0;
    NdisReleaseSpinLock(&Adapter->StateLock);

    Status = SmscStartBulkRead(Adapter);
    if ((NT_SUCCESS(Status) || Status == STATUS_PENDING) &&
        Adapter->InterruptIn.PipeHandle)
    {
        Status = SmscStartInterruptRead(Adapter);
    }
    if (!NT_SUCCESS(Status) && Status != STATUS_PENDING)
    {
        NdisAcquireSpinLock(&Adapter->StateLock);
        Adapter->Paused = TRUE;
        NdisReleaseSpinLock(&Adapter->StateLock);
        SmscCancelPendingIo(Adapter);
        if (KeGetCurrentIrql() == PASSIVE_LEVEL)
        {
            SmscWaitForRundown(Adapter);
        }
        return NDIS_STATUS_FAILURE;
    }

    SmscQueueLinkWork(Adapter);
    return NDIS_STATUS_SUCCESS;
}

static
VOID
NTAPI
SmscMiniportDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PSMSC_ADAPTER Adapter;

    Adapter = (PSMSC_ADAPTER)MiniportAdapterContext;
    if (!Adapter || !NetDevicePnPEvent)
    {
        return;
    }

    switch (NetDevicePnPEvent->DevicePnPEvent)
    {
        case NdisDevicePnPEventRemoved:
        case NdisDevicePnPEventSurpriseRemoved:
            NdisAcquireSpinLock(&Adapter->StateLock);
            Adapter->Halting = TRUE;
            Adapter->Paused = TRUE;
            NdisReleaseSpinLock(&Adapter->StateLock);
            SmscStopTransmits(Adapter, NDIS_STATUS_FAILURE);
            SmscCancelPendingIo(Adapter);
            break;

        default:
            break;
    }
}

static
VOID
NTAPI
SmscMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PSMSC_ADAPTER Adapter;

    Adapter = (PSMSC_ADAPTER)MiniportAdapterContext;
    if (!Adapter)
    {
        return;
    }

    NdisAcquireSpinLock(&Adapter->StateLock);
    Adapter->Halting = TRUE;
    Adapter->Paused = TRUE;
    NdisReleaseSpinLock(&Adapter->StateLock);

    if (ShutdownAction == NdisShutdownBugCheck)
    {
        return;
    }

    SmscStopTransmits(Adapter, NDIS_STATUS_FAILURE);
    SmscCancelPendingIo(Adapter);
    if (KeGetCurrentIrql() == PASSIVE_LEVEL)
    {
        SmscWaitForRundown(Adapter);
        SmscCompleteStoppedActiveSend(Adapter);
    }
    SmscDisableHardware(Adapter);
}

static
VOID
NTAPI
SmscMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PSMSC_ADAPTER Adapter;

    UNREFERENCED_PARAMETER(HaltAction);

    Adapter = (PSMSC_ADAPTER)MiniportAdapterContext;
    if (!Adapter)
    {
        return;
    }

    NdisAcquireSpinLock(&Adapter->StateLock);
    Adapter->Halting = TRUE;
    Adapter->Paused = TRUE;
    NdisReleaseSpinLock(&Adapter->StateLock);

    SmscStopTransmits(Adapter, NDIS_STATUS_FAILURE);
    SmscCancelPendingIo(Adapter);
    SmscWaitForRundown(Adapter);
    SmscCompleteStoppedActiveSend(Adapter);

    if (Adapter->LinkWorkItem)
    {
        NdisFreeIoWorkItem(Adapter->LinkWorkItem);
        Adapter->LinkWorkItem = NULL;
    }

    SmscDisableHardware(Adapter);

    if (Adapter->RxNblPool)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
    }

    NdisFreeSpinLock(&Adapter->StateLock);
    SmscFree(Adapter->InterruptBuffer);
    SmscFree(Adapter->TxBuffer);
    SmscFree(Adapter->RxBuffer);
    SmscFree(Adapter->ConfigurationDescriptor);
    SmscFree(Adapter->DeviceDescriptor);
    SmscFree(Adapter);
}

static
NDIS_STATUS
NTAPI
SmscMiniportInitializeEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PSMSC_ADAPTER Adapter;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;
    NDIS_STATUS NdisStatus;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    Adapter = SmscAllocate(sizeof(*Adapter));
    if (!Adapter)
    {
        return NDIS_STATUS_RESOURCES;
    }

    Adapter->MiniportAdapterHandle = MiniportAdapterHandle;
    Adapter->PacketFilter = NDIS_PACKET_TYPE_DIRECTED |
                            NDIS_PACKET_TYPE_BROADCAST |
                            NDIS_PACKET_TYPE_ALL_MULTICAST;
    Adapter->CurrentLookahead = SMSC_ETH_MTU;
    Adapter->PhyId = 1;
    Adapter->LinkSpeed = SMSC_LINK_SPEED_100MBPS;
    Adapter->MediaState = MediaConnectStateDisconnected;
    Adapter->DuplexState = MediaDuplexStateUnknown;
    Adapter->Paused = TRUE;

    KeInitializeEvent(&Adapter->RemoveEvent, NotificationEvent, TRUE);
    KeInitializeDpc(&Adapter->RxResubmitDpc, SmscRxResubmitDpc, Adapter);
    KeInitializeDpc(&Adapter->InterruptResubmitDpc, SmscInterruptResubmitDpc, Adapter);
    NdisAllocateSpinLock(&Adapter->StateLock);
    KeInitializeMutex(&Adapter->MacControlLock, 0);

    NdisStatus = SmscSetRegistrationAttributes(Adapter);
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
    Adapter->LinkWorkItem = NdisAllocateIoWorkItem(MiniportAdapterHandle);
    if (!Adapter->LinkWorkItem)
    {
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    Status = SmscReadDescriptors(Adapter);
    if (!NT_SUCCESS(Status))
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    Status = SmscSelectConfiguration(Adapter);
    if (!NT_SUCCESS(Status))
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    Status = SmscReadRegister(Adapter, SMSC_ID_REV, &Adapter->DeviceIdRevision);
    if (!NT_SUCCESS(Status))
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    Status = SmscGetMacAddress(Adapter);
    if (!NT_SUCCESS(Status))
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    Adapter->RxBuffer = SmscAllocate(SMSC_RX_BUFFER_SIZE);
    Adapter->TxBuffer = SmscAllocate(SMSC_TX_HEADER_SIZE + SMSC_MAX_ETHERNET_FRAME);
    if (Adapter->InterruptIn.PipeHandle)
    {
        Adapter->InterruptBuffer = SmscAllocate(SMSC_INTERRUPT_LENGTH);
    }

    if (!Adapter->RxBuffer ||
        !Adapter->TxBuffer ||
        (Adapter->InterruptIn.PipeHandle && !Adapter->InterruptBuffer))
    {
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    NdisStatus = SmscAllocateRxNblPool(Adapter);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        goto Cleanup;
    }

    Status = SmscInitializeHardware(Adapter);
    if (!NT_SUCCESS(Status))
    {
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    SmscUpdateLinkFromPhy(Adapter);

    NdisStatus = SmscSetGeneralAttributes(Adapter);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        goto Cleanup;
    }

    DPRINT1("SMSC95XX: VID %04x PID %04x ID_REV %08lx initialized, MAC %02x:%02x:%02x:%02x:%02x:%02x\n",
            Adapter->DeviceDescriptor->idVendor,
            Adapter->DeviceDescriptor->idProduct,
            Adapter->DeviceIdRevision,
            Adapter->PermanentMacAddress[0],
            Adapter->PermanentMacAddress[1],
            Adapter->PermanentMacAddress[2],
            Adapter->PermanentMacAddress[3],
            Adapter->PermanentMacAddress[4],
            Adapter->PermanentMacAddress[5]);

    return NDIS_STATUS_SUCCESS;

Cleanup:
    SmscMiniportHaltEx(Adapter, NdisHaltDeviceInitializationFailed);
    return NdisStatus;
}

static
VOID
NTAPI
SmscMiniportUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (g_SmscMiniportDriverHandle)
    {
        NdisMDeregisterMiniportDriver(g_SmscMiniportDriverHandle);
        g_SmscMiniportDriverHandle = NULL;
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
    Characteristics.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Characteristics.Header.Size = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Characteristics.MajorNdisVersion = 6;
    Characteristics.MinorNdisVersion = 20;
    Characteristics.MajorDriverVersion = 1;
    Characteristics.MinorDriverVersion = 0;
    Characteristics.InitializeHandlerEx = SmscMiniportInitializeEx;
    Characteristics.HaltHandlerEx = SmscMiniportHaltEx;
    Characteristics.UnloadHandler = SmscMiniportUnload;
    Characteristics.PauseHandler = SmscMiniportPause;
    Characteristics.RestartHandler = SmscMiniportRestart;
    Characteristics.OidRequestHandler = SmscMiniportOidRequest;
    Characteristics.SendNetBufferListsHandler = SmscMiniportSendNetBufferLists;
    Characteristics.ReturnNetBufferListsHandler = SmscMiniportReturnNetBufferLists;
    Characteristics.CancelSendHandler = SmscMiniportCancelSend;
    Characteristics.CheckForHangHandlerEx = SmscMiniportCheckForHang;
    Characteristics.ResetHandlerEx = SmscMiniportReset;
    Characteristics.DevicePnPEventNotifyHandler = SmscMiniportDevicePnPEventNotify;
    Characteristics.ShutdownHandlerEx = SmscMiniportShutdownEx;
    Characteristics.CancelOidRequestHandler = SmscMiniportCancelOidRequest;

    return NdisMRegisterMiniportDriver(DriverObject,
                                       RegistryPath,
                                       NULL,
                                       &Characteristics,
                                       &g_SmscMiniportDriverHandle);
}
