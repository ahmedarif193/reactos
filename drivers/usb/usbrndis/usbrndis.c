/*
 * PROJECT:     ReactOS USB RNDIS Network Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NDIS miniport driver entry and initialization
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This is a clean-room implementation based on the Microsoft RNDIS specification
 * and the USB CDC (Communications Device Class) specification.
 */

#include "usbrndis.h"

#define NDEBUG
#include <debug.h>

/* Global NDIS Wrapper Handle */
static NDIS_HANDLE g_NdisWrapperHandle = NULL;

/*
 * AllocateItem
 *
 * Helper function to allocate zeroed memory
 */
PVOID
RndisAllocateMemory(
    IN POOL_TYPE PoolType,
    IN SIZE_T Size)
{
    PVOID Buffer;
    NDIS_STATUS Status;

    Status = NdisAllocateMemoryWithTag(&Buffer, (UINT)Size, USBRNDIS_TAG);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        return NULL;
    }

    NdisZeroMemory(Buffer, Size);
    return Buffer;
}

/*
 * FreeItem
 *
 * Helper function to free memory
 */
VOID
RndisFreeMemory(
    IN PVOID Buffer)
{
    if (Buffer)
    {
        NdisFreeMemory(Buffer, 0, 0);
    }
}

/*
 * RndisDecrementPendingIo
 *
 * Decrement the pending I/O count and signal RemoveEvent if it reaches zero.
 * Called from async completion routines.
 */
VOID
RndisDecrementPendingIo(
    IN PRNDIS_ADAPTER Adapter)
{
    if (InterlockedDecrement(&Adapter->PendingIoCount) == 0)
    {
        KeSetEvent(&Adapter->RemoveEvent, IO_NO_INCREMENT, FALSE);
    }
}

/*
 * RndisSyncUrbRequest
 *
 * Submit a URB synchronously and wait for completion
 */
NTSTATUS
RndisSyncUrbRequest(
    IN PDEVICE_OBJECT DeviceObject,
    IN PURB Urb)
{
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildDeviceIoControlRequest(
        IOCTL_INTERNAL_USB_SUBMIT_URB,
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

/*
 * RndisGetBusInterface
 *
 * Query the USB bus interface from the lower driver
 */
static
NTSTATUS
RndisGetBusInterface(
    IN PDEVICE_OBJECT PhysicalDeviceObject,
    OUT PUSB_BUS_INTERFACE_USBDI_V2 BusInterface)
{
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatus;
    PIO_STACK_LOCATION IoStack;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(
        IRP_MJ_PNP,
        PhysicalDeviceObject,
        NULL,
        0,
        NULL,
        &Event,
        &IoStatus);

    if (!Irp)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    IoStack->Parameters.QueryInterface.InterfaceType = &USB_BUS_INTERFACE_USBDI_GUID;
    IoStack->Parameters.QueryInterface.Size = sizeof(USB_BUS_INTERFACE_USBDI_V2);
    IoStack->Parameters.QueryInterface.Version = USB_BUSIF_USBDI_VERSION_2;
    IoStack->Parameters.QueryInterface.Interface = (PINTERFACE)BusInterface;
    IoStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    Status = IoCallDriver(PhysicalDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

/*
 * MiniportInitialize
 *
 * NDIS calls this function to initialize the miniport adapter
 */
NDIS_STATUS
NTAPI
RndisInitialize(
    OUT PNDIS_STATUS OpenErrorStatus,
    OUT PUINT SelectedMediumIndex,
    IN PNDIS_MEDIUM MediumArray,
    IN UINT MediumArraySize,
    IN NDIS_HANDLE MiniportAdapterHandle,
    IN NDIS_HANDLE WrapperConfigurationContext)
{
    NDIS_STATUS NdisStatus;
    NTSTATUS NtStatus;
    PRNDIS_ADAPTER Adapter = NULL;
    UINT i;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;

    UNREFERENCED_PARAMETER(WrapperConfigurationContext);

    DPRINT1("USBRNDIS: RndisInitialize called\n");

    /* Initialize OpenErrorStatus */
    *OpenErrorStatus = NDIS_STATUS_SUCCESS;

    /* Find 802.3 (Ethernet) medium */
    for (i = 0; i < MediumArraySize; i++)
    {
        if (MediumArray[i] == NdisMedium802_3)
        {
            *SelectedMediumIndex = i;
            break;
        }
    }

    if (i == MediumArraySize)
    {
        DPRINT1("USBRNDIS: 802.3 medium not found\n");
        *OpenErrorStatus = NDIS_STATUS_UNSUPPORTED_MEDIA;
        return NDIS_STATUS_UNSUPPORTED_MEDIA;
    }

    /* Allocate adapter context */
    Adapter = RndisAllocateMemory(NonPagedPool, sizeof(RNDIS_ADAPTER));
    if (!Adapter)
    {
        DPRINT1("USBRNDIS: Failed to allocate adapter context\n");
        *OpenErrorStatus = NDIS_STATUS_RESOURCES;
        return NDIS_STATUS_RESOURCES;
    }

    /* Initialize adapter fields */
    Adapter->MiniportAdapterHandle = MiniportAdapterHandle;
    Adapter->State = RndisStateUninitialized;
    Adapter->MediaState = NdisMediaStateDisconnected;
    Adapter->LinkSpeed = 1000000; /* 100 Mbps default */
    Adapter->PacketFilter = 0;
    Adapter->RequestId = 1;
    Adapter->Halting = FALSE;
    Adapter->PendingIoCount = 0;
    Adapter->RxIrp = NULL;
    Adapter->TxIrp = NULL;
    Adapter->PendingTxPacket = NULL;
    Adapter->IsCdcEcm = FALSE;
    Adapter->IsCdcNcm = FALSE;
    Adapter->RxSubmitted = FALSE;
    Adapter->RxConsecutiveErrors = 0;
    Adapter->DataInterfaceNumber = 0xFF;  /* Sentinel - no data interface found yet */
    Adapter->DataAlternateSetting = 0;    /* Default to alt setting 0 */

    /* Initialize synchronization */
    KeInitializeEvent(&Adapter->ControlEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->RemoveEvent, NotificationEvent, TRUE); /* Initially signaled */
    KeInitializeMutex(&Adapter->ControlMutex, 0);  /* Mutex for control channel (PASSIVE_LEVEL) */
    NdisAllocateSpinLock(&Adapter->TxLock);
    NdisAllocateSpinLock(&Adapter->RxLock);

    /* Initialize RX DPC and backoff timer early so RndisHalt can safely cancel them */
    RndisInitializeRxDpc(Adapter);

    /* Get the physical device object */
    NdisMGetDeviceProperty(
        MiniportAdapterHandle,
        &PhysicalDeviceObject,
        NULL,
        &LowerDeviceObject,
        NULL,
        NULL);

    Adapter->PhysicalDeviceObject = PhysicalDeviceObject;
    Adapter->LowerDeviceObject = LowerDeviceObject;

    DPRINT1("USBRNDIS: PDO=%p LowerDO=%p (StackSize=%u)\n",
            PhysicalDeviceObject, LowerDeviceObject,
            LowerDeviceObject ? LowerDeviceObject->StackSize : 0);

    /* Set NDIS attributes */
    NdisMSetAttributesEx(
        MiniportAdapterHandle,
        (NDIS_HANDLE)Adapter,
        0,
        NDIS_ATTRIBUTE_DESERIALIZE | NDIS_ATTRIBUTE_NO_HALT_ON_SUSPEND,
        NdisInterfaceInternal);

    /* Allocate control buffer */
    Adapter->ControlBuffer = RndisAllocateMemory(NonPagedPool, RNDIS_CONTROL_BUFFER_SIZE);
    if (!Adapter->ControlBuffer)
    {
        DPRINT1("USBRNDIS: Failed to allocate control buffer\n");
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    /*
     * Get USB bus interface.
     * Note: Should query PDO, not LowerDeviceObject. However, on some systems
     * querying the PDO directly may not work, so we try PDO first, then fallback
     * to LowerDeviceObject.
     */
    NtStatus = RndisGetBusInterface(PhysicalDeviceObject, &Adapter->BusInterface);
    if (!NT_SUCCESS(NtStatus))
    {
        DPRINT("USBRNDIS: PDO query failed, trying LowerDeviceObject\n");
        NtStatus = RndisGetBusInterface(LowerDeviceObject, &Adapter->BusInterface);
        if (!NT_SUCCESS(NtStatus))
        {
            DPRINT1("USBRNDIS: Failed to get USB bus interface (0x%08X)\n", NtStatus);
            NdisStatus = NDIS_STATUS_FAILURE;
            goto Cleanup;
        }
    }

    /* Get USB descriptors */
    NtStatus = RndisUsbGetDescriptors(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        DPRINT1("USBRNDIS: Failed to get USB descriptors (0x%08X)\n", NtStatus);
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    /* Select USB configuration and interface */
    NtStatus = RndisUsbSelectConfiguration(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        DPRINT1("USBRNDIS: Failed to select USB configuration (0x%08X)\n", NtStatus);
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    /* Allocate transmit buffer */
    Adapter->TxBuffer = RndisAllocateMemory(NonPagedPool, RNDIS_MAX_TRANSFER_SIZE);
    if (!Adapter->TxBuffer)
    {
        DPRINT1("USBRNDIS: Failed to allocate TX buffer\n");
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    /* Allocate receive buffer */
    Adapter->RxBuffer = RndisAllocateMemory(NonPagedPool, RNDIS_MAX_TRANSFER_SIZE);
    if (!Adapter->RxBuffer)
    {
        DPRINT1("USBRNDIS: Failed to allocate RX buffer\n");
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    if (Adapter->IsCdcEcm || Adapter->IsCdcNcm)
    {
        /*
         * CDC-ECM/NCM mode: No RNDIS initialization needed.
         * For CDC-ECM: Ethernet frames are sent raw without wrapping.
         * For CDC-NCM: Ethernet frames are wrapped in NTB (NTH16/NDP16).
         */
        if (Adapter->IsCdcNcm)
        {
            DPRINT1("USBRNDIS: CDC-NCM mode - using NTB framing\n");

            /*
             * Initialize NCM parameters with safe defaults first.
             * These may be updated by RndisNcmSetup() if device responds.
             */
            Adapter->NcmTxSequence = 0;
            Adapter->NcmNtbMaxSize = NCM_DEFAULT_NTB_MAX_SIZE;
            Adapter->NcmNtbOutMaxSize = NCM_DEFAULT_NTB_MAX_SIZE;
            Adapter->NcmNdpDivisor = NCM_DEFAULT_NDP_DIVISOR;
            Adapter->NcmNdpRemainder = NCM_DEFAULT_NDP_REMAINDER;
            Adapter->NcmNdpAlignment = NCM_DEFAULT_NDP_ALIGNMENT;

            /* Perform NCM-specific setup (query params, set NTB size) */
            RndisNcmSetup(Adapter);

            DPRINT1("USBRNDIS: NCM params after setup: MaxNTB=%lu Divisor=%u Remainder=%u Alignment=%u\n",
                    Adapter->NcmNtbMaxSize, Adapter->NcmNdpDivisor,
                    Adapter->NcmNdpRemainder, Adapter->NcmNdpAlignment);
        }
        else
        {
            DPRINT1("USBRNDIS: CDC-ECM mode - skipping RNDIS initialization\n");
        }

        /* Set default values for CDC-ECM/NCM */
        Adapter->MaxTransferSize = RNDIS_MAX_TRANSFER_SIZE;
        Adapter->PacketAlignmentFactor = 0;
        Adapter->MaxPacketsPerMessage = 1;

        /*
         * Generate a locally-administered MAC address.
         * Locally-administered MACs have bit 1 of first byte set (0x02).
         * Use device VID/PID and interface number for uniqueness.
         *
         * TODO: Parse CDC Ethernet Networking Functional Descriptor (subtype 0x0F)
         * to get the actual MAC address from iMACAddress string descriptor index.
         */
        {
            LARGE_INTEGER TickCount;
            KeQueryTickCount(&TickCount);

            Adapter->PermanentMacAddress[0] = 0x02;  /* Locally administered, unicast */
            Adapter->PermanentMacAddress[1] = (UCHAR)(Adapter->DeviceDescriptor->idVendor >> 8);
            Adapter->PermanentMacAddress[2] = (UCHAR)(Adapter->DeviceDescriptor->idVendor & 0xFF);
            Adapter->PermanentMacAddress[3] = (UCHAR)(Adapter->DeviceDescriptor->idProduct >> 8);
            Adapter->PermanentMacAddress[4] = (UCHAR)(Adapter->DeviceDescriptor->idProduct & 0xFF);
            Adapter->PermanentMacAddress[5] = (UCHAR)(TickCount.LowPart & 0xFF);
        }
        NdisMoveMemory(Adapter->CurrentMacAddress, Adapter->PermanentMacAddress, 6);

        DPRINT1("USBRNDIS: %s MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                Adapter->IsCdcNcm ? "CDC-NCM" : "CDC-ECM",
                Adapter->PermanentMacAddress[0], Adapter->PermanentMacAddress[1],
                Adapter->PermanentMacAddress[2], Adapter->PermanentMacAddress[3],
                Adapter->PermanentMacAddress[4], Adapter->PermanentMacAddress[5]);

        Adapter->State = RndisStateDataInitialized;
    }
    else
    {
        /* Initialize RNDIS protocol */
        Adapter->State = RndisStateInitializing;
        NtStatus = RndisInitializeDevice(Adapter);
        if (!NT_SUCCESS(NtStatus))
        {
            DPRINT1("USBRNDIS: Failed to initialize RNDIS device (0x%08X)\n", NtStatus);
            NdisStatus = NDIS_STATUS_FAILURE;
            goto Cleanup;
        }

        Adapter->State = RndisStateInitialized;

        /* Get MAC address from device */
        NtStatus = RndisGetMacAddress(Adapter);
        if (!NT_SUCCESS(NtStatus))
        {
            DPRINT1("USBRNDIS: Failed to get MAC address (0x%08X)\n", NtStatus);
            NdisStatus = NDIS_STATUS_FAILURE;
            goto Cleanup;
        }

        DPRINT1("USBRNDIS: MAC Address: %02X:%02X:%02X:%02X:%02X:%02X\n",
                Adapter->PermanentMacAddress[0], Adapter->PermanentMacAddress[1],
                Adapter->PermanentMacAddress[2], Adapter->PermanentMacAddress[3],
                Adapter->PermanentMacAddress[4], Adapter->PermanentMacAddress[5]);

        /* Set default packet filter to enable data transfer */
        NtStatus = RndisSetPacketFilter(Adapter, RNDIS_DEFAULT_FILTER);
        if (!NT_SUCCESS(NtStatus))
        {
            DPRINT1("USBRNDIS: Failed to set packet filter (0x%08X)\n", NtStatus);
            /* Non-fatal, some devices may not support this */
        }

        Adapter->State = RndisStateDataInitialized;
    }

    /*
     * Query/set media connect status.
     * RNDIS devices: Query via RNDIS control message.
     * CDC-ECM/NCM: Assume connected - these don't use RNDIS protocol.
     *              Real media status would come from CDC notification endpoint.
     */
    if (Adapter->IsCdcEcm || Adapter->IsCdcNcm)
    {
        /*
         * CDC-ECM/NCM: Assume media connected.
         * The device is ready to use as soon as we select the data interface
         * alternate setting. If the device supports CDC notifications (interrupt
         * IN endpoint), we could monitor for NETWORK_CONNECTION notifications,
         * but for simplicity we assume connected.
         */
        Adapter->MediaState = NdisMediaStateConnected;
        DPRINT1("USBRNDIS: %s media state: Connected (assumed)\n",
                Adapter->IsCdcNcm ? "CDC-NCM" : "CDC-ECM");
    }
    else
    {
        /* RNDIS: Query media status via control message */
        ULONG MediaConnectStatus = 0;
        ULONG BytesWritten = 0;
        NTSTATUS NtStatus;

        NtStatus = RndisQueryOid(Adapter, RNDIS_OID_GEN_MEDIA_CONNECT_STATUS,
                                 &MediaConnectStatus, sizeof(MediaConnectStatus),
                                 &BytesWritten);

        if (NT_SUCCESS(NtStatus) && BytesWritten == sizeof(MediaConnectStatus))
        {
            /*
             * MediaConnectStatus: 0 = Connected, 1 = Disconnected
             * (per NDIS 5.1 specification)
             */
            if (MediaConnectStatus == 0)
            {
                Adapter->MediaState = NdisMediaStateConnected;
                DPRINT1("USBRNDIS: Media state: Connected\n");
            }
            else
            {
                Adapter->MediaState = NdisMediaStateDisconnected;
                DPRINT1("USBRNDIS: Media state: Disconnected\n");
            }
        }
        else
        {
            /*
             * Failed to query - assume disconnected for safety.
             * The device will send INDICATE messages when media connects.
             */
            DPRINT1("USBRNDIS: Failed to query media status (0x%08X), assuming disconnected\n", NtStatus);
            Adapter->MediaState = NdisMediaStateDisconnected;
        }
    }

    /* Start receiving data */
    RndisUsbSubmitBulkRead(Adapter);

    DPRINT1("USBRNDIS: Initialization complete\n");
    return NDIS_STATUS_SUCCESS;

Cleanup:
    /* Write OpenErrorStatus on failure */
    *OpenErrorStatus = NdisStatus;
    if (Adapter)
    {
        RndisHalt((NDIS_HANDLE)Adapter);
    }
    return NdisStatus;
}

/*
 * MiniportHalt
 *
 * NDIS calls this function to halt the miniport adapter.
 * Must cancel all pending I/O and wait for completion before freeing resources.
 */
VOID
NTAPI
RndisHalt(
    IN NDIS_HANDLE MiniportAdapterContext)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    PIRP RxIrp;
    PIRP TxIrp;

    DPRINT1("USBRNDIS: RndisHalt called\n");

    if (!Adapter)
    {
        return;
    }

    /* Signal that we are halting - prevents URB resubmission */
    Adapter->Halting = TRUE;

    /* Cancel RX timers if running */
    KeCancelTimer(&Adapter->RxBackoffTimer);
    KeCancelTimer(&Adapter->RxDelayTimer);

    /*
     * Remove any queued DPCs. KeCancelTimer prevents future expirations but
     * if a timer already expired and queued a DPC, it can still run.
     * We must remove queued DPCs to prevent use-after-free when we free
     * the adapter structure. KeRemoveQueueDpc returns TRUE if DPC was removed.
     */
    KeRemoveQueueDpc(&Adapter->RxResubmitDpc);
    KeRemoveQueueDpc(&Adapter->RxBackoffDpc);
    KeRemoveQueueDpc(&Adapter->RxDelayDpc);

    /*
     * Acquire ControlMutex before sending halt to ensure no control
     * channel operations are in progress. This synchronizes with
     * rndisctl.c functions that also hold this mutex.
     */
    KeWaitForSingleObject(&Adapter->ControlMutex, Executive, KernelMode, FALSE, NULL);

    /* Send RNDIS halt message to device (skip for CDC-ECM/NCM) */
    if (Adapter->State >= RndisStateInitialized && !Adapter->IsCdcEcm && !Adapter->IsCdcNcm)
    {
        RndisHaltDevice(Adapter);
    }

    /* Release mutex after halt message sent */
    KeReleaseMutex(&Adapter->ControlMutex, FALSE);

    Adapter->State = RndisStateHalted;

    /*
     * Cancel pending RX IRP.
     * Atomically capture-and-null the IRP pointer under lock.
     * This prevents the completion routine (which may run on another CPU)
     * from seeing a stale pointer after we've called IoCancelIrp.
     * The completion routine checks for NULL before using the IRP.
     */
    NdisAcquireSpinLock(&Adapter->RxLock);
    RxIrp = Adapter->RxIrp;
    Adapter->RxIrp = NULL;  /* Null it under lock to prevent completion routine race */
    NdisReleaseSpinLock(&Adapter->RxLock);

    if (RxIrp)
    {
        DPRINT1("USBRNDIS: Cancelling pending RX IRP\n");
        IoCancelIrp(RxIrp);
    }

    /*
     * Cancel pending TX IRP.
     * Same atomic capture-and-null pattern.
     */
    NdisAcquireSpinLock(&Adapter->TxLock);
    TxIrp = Adapter->TxIrp;
    Adapter->TxIrp = NULL;  /* Null it under lock to prevent completion routine race */
    NdisReleaseSpinLock(&Adapter->TxLock);

    if (TxIrp)
    {
        DPRINT1("USBRNDIS: Cancelling pending TX IRP\n");
        IoCancelIrp(TxIrp);
    }

    /*
     * Wait for all pending I/O to complete.
     * The RemoveEvent is signaled when PendingIoCount reaches zero.
     */
    if (Adapter->PendingIoCount > 0)
    {
        DPRINT1("USBRNDIS: Waiting for %ld pending I/O operations\n", Adapter->PendingIoCount);
        KeWaitForSingleObject(&Adapter->RemoveEvent, Executive, KernelMode, FALSE, NULL);
        DPRINT1("USBRNDIS: All pending I/O completed\n");
    }

    /* Now safe to free all resources */

    /*
     * Release the USB bus interface reference.
     * This must be done before freeing other resources.
     */
    if (Adapter->BusInterface.InterfaceDereference)
    {
        Adapter->BusInterface.InterfaceDereference(Adapter->BusInterface.BusContext);
        Adapter->BusInterface.InterfaceDereference = NULL;
    }

    /* Free spin locks (mutex doesn't need explicit cleanup) */
    NdisFreeSpinLock(&Adapter->TxLock);
    NdisFreeSpinLock(&Adapter->RxLock);

    /* Free buffers */
    if (Adapter->RxBuffer)
    {
        RndisFreeMemory(Adapter->RxBuffer);
        Adapter->RxBuffer = NULL;
    }

    if (Adapter->TxBuffer)
    {
        RndisFreeMemory(Adapter->TxBuffer);
        Adapter->TxBuffer = NULL;
    }

    if (Adapter->ControlBuffer)
    {
        RndisFreeMemory(Adapter->ControlBuffer);
        Adapter->ControlBuffer = NULL;
    }

    /* Free USB descriptors */
    if (Adapter->DeviceDescriptor)
    {
        RndisFreeMemory(Adapter->DeviceDescriptor);
        Adapter->DeviceDescriptor = NULL;
    }

    if (Adapter->ConfigurationDescriptor)
    {
        RndisFreeMemory(Adapter->ConfigurationDescriptor);
        Adapter->ConfigurationDescriptor = NULL;
    }

    if (Adapter->ControlInterface)
    {
        RndisFreeMemory(Adapter->ControlInterface);
        Adapter->ControlInterface = NULL;
    }

    if (Adapter->DataInterface)
    {
        RndisFreeMemory(Adapter->DataInterface);
        Adapter->DataInterface = NULL;
    }

    /* Free adapter context */
    RndisFreeMemory(Adapter);

    DPRINT1("USBRNDIS: Halt complete\n");
}

/*
 * MiniportReset
 *
 * NDIS calls this function to reset the miniport adapter
 */
NDIS_STATUS
NTAPI
RndisReset(
    OUT PBOOLEAN AddressingReset,
    IN NDIS_HANDLE MiniportAdapterContext)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    RNDIS_RESET_MSG ResetMsg;
    PRNDIS_RESET_CMPLT ResetCmplt;
    NTSTATUS Status;
    ULONG BytesReceived;

    DPRINT1("USBRNDIS: RndisReset called\n");

    *AddressingReset = FALSE;

    if (Adapter->State < RndisStateInitialized)
    {
        return NDIS_STATUS_FAILURE;
    }

    /* Build reset message */
    NdisZeroMemory(&ResetMsg, sizeof(ResetMsg));
    ResetMsg.MessageType = RNDIS_MSG_RESET;
    ResetMsg.MessageLength = sizeof(RNDIS_RESET_MSG);
    ResetMsg.Reserved = 0;

    /* Send reset command */
    Status = RndisUsbSendControlMessage(Adapter, &ResetMsg, sizeof(ResetMsg));
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBRNDIS: Failed to send reset message (0x%08X)\n", Status);
        return NDIS_STATUS_FAILURE;
    }

    /* Receive reset completion */
    Status = RndisUsbReceiveControlResponse(
        Adapter,
        Adapter->ControlBuffer,
        RNDIS_CONTROL_BUFFER_SIZE,
        &BytesReceived);

    if (!NT_SUCCESS(Status) || BytesReceived < sizeof(RNDIS_RESET_CMPLT))
    {
        DPRINT1("USBRNDIS: Failed to receive reset completion (0x%08X)\n", Status);
        return NDIS_STATUS_FAILURE;
    }

    ResetCmplt = (PRNDIS_RESET_CMPLT)Adapter->ControlBuffer;
    if (ResetCmplt->MessageType != RNDIS_MSG_RESET_C ||
        ResetCmplt->Status != RNDIS_STATUS_SUCCESS)
    {
        DPRINT1("USBRNDIS: Reset failed (type=0x%08X, status=0x%08X)\n",
                ResetCmplt->MessageType, ResetCmplt->Status);
        return NDIS_STATUS_FAILURE;
    }

    if (ResetCmplt->AddressingReset)
    {
        *AddressingReset = TRUE;
        /* Re-set MAC address and packet filter */
        RndisSetPacketFilter(Adapter, Adapter->PacketFilter);
    }

    DPRINT1("USBRNDIS: Reset complete\n");
    return NDIS_STATUS_SUCCESS;
}

/*
 * MiniportShutdown
 *
 * NDIS calls this function when the system is shutting down
 */
VOID
NTAPI
RndisShutdown(
    IN NDIS_HANDLE MiniportAdapterContext)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;

    DPRINT1("USBRNDIS: RndisShutdown called\n");

    if (Adapter && Adapter->State >= RndisStateInitialized)
    {
        RndisHaltDevice(Adapter);
    }
}

/*
 * MiniportCheckForHang
 *
 * NDIS calls this function periodically to check if the adapter is hung
 */
static
BOOLEAN
NTAPI
RndisCheckForHang(
    IN NDIS_HANDLE MiniportAdapterContext)
{
    /* For now, always return FALSE (not hung) */
    return FALSE;
}

/*
 * DriverEntry
 *
 * Main entry point for the driver
 */
NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NDIS_STATUS Status;
    NDIS_MINIPORT_CHARACTERISTICS Characteristics;

    DPRINT1("USBRNDIS: DriverEntry called\n");

    /* Initialize NDIS wrapper */
    NdisMInitializeWrapper(
        &g_NdisWrapperHandle,
        DriverObject,
        RegistryPath,
        NULL);

    if (!g_NdisWrapperHandle)
    {
        DPRINT1("USBRNDIS: NdisMInitializeWrapper failed\n");
        return STATUS_UNSUCCESSFUL;
    }

    /* Setup miniport characteristics */
    NdisZeroMemory(&Characteristics, sizeof(Characteristics));

    Characteristics.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    Characteristics.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;

    Characteristics.InitializeHandler = RndisInitialize;
    Characteristics.HaltHandler = RndisHalt;
    Characteristics.ResetHandler = RndisReset;
    Characteristics.QueryInformationHandler = RndisQueryInformation;
    Characteristics.SetInformationHandler = RndisSetInformation;
    Characteristics.SendHandler = RndisSend;
    Characteristics.SendPacketsHandler = RndisSendPackets;
    Characteristics.CheckForHangHandler = RndisCheckForHang;
    Characteristics.AdapterShutdownHandler = RndisShutdown;

    /* Register miniport */
    Status = NdisMRegisterMiniport(
        g_NdisWrapperHandle,
        &Characteristics,
        sizeof(Characteristics));

    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("USBRNDIS: NdisMRegisterMiniport failed (0x%08X)\n", Status);
        NdisTerminateWrapper(g_NdisWrapperHandle, NULL);
        return STATUS_UNSUCCESSFUL;
    }

    DPRINT1("USBRNDIS: Driver loaded successfully\n");
    return STATUS_SUCCESS;
}
