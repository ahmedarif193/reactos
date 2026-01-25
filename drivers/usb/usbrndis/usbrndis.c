/*
 * PROJECT:     ReactOS USB RNDIS Network Driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NDIS 6.x miniport driver entry and initialization
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * This is a clean-room implementation based on the Microsoft RNDIS specification
 * and the USB CDC (Communications Device Class) specification.
 */

#include "usbrndis.h"

#define NDEBUG
#include <debug.h>

/* Global NDIS Miniport Driver Handle */
static NDIS_HANDLE g_NdisMiniportDriverHandle = NULL;

/* Driver object for unload */
static PDRIVER_OBJECT g_DriverObject = NULL;

/*
 * RndisAllocateMemory
 *
 * Helper function to allocate zeroed memory
 */
PVOID
RndisAllocateMemory(
    IN POOL_TYPE PoolType,
    IN SIZE_T Size)
{
    PVOID Buffer;

    UNREFERENCED_PARAMETER(PoolType);

    Buffer = NdisAllocateMemoryWithTagPriority(
        g_NdisMiniportDriverHandle,
        (UINT)Size,
        USBRNDIS_TAG,
        NormalPoolPriority);

    if (Buffer)
    {
        NdisZeroMemory(Buffer, Size);
    }

    return Buffer;
}

/*
 * RndisFreeMemory
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
 * RndisSetRegistrationAttributes
 *
 * Set NDIS 6.x registration attributes for the adapter
 */
static
NDIS_STATUS
RndisSetRegistrationAttributes(
    _In_ PRNDIS_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RegAttrs;
    NDIS_STATUS Status;

    NdisZeroMemory(&RegAttrs, sizeof(RegAttrs));

    RegAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegAttrs.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES);

    RegAttrs.MiniportAdapterContext = Adapter;
    RegAttrs.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_NDIS_WDM |
                              NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK |
                              NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND;
    RegAttrs.CheckForHangTimeInSeconds = 4;
    RegAttrs.InterfaceType = NdisInterfaceInternal;

    Status = NdisMSetMiniportAttributes(
        Adapter->MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RegAttrs);

    return Status;
}

/*
 * RndisSetGeneralAttributes
 *
 * Set NDIS 6.x general adapter attributes
 */
static
NDIS_STATUS
RndisSetGeneralAttributes(
    _In_ PRNDIS_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GenAttrs;
    NDIS_STATUS Status;

    NdisZeroMemory(&GenAttrs, sizeof(GenAttrs));

    GenAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GenAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
    GenAttrs.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES);

    /* Link capabilities */
    GenAttrs.MediaType = NdisMedium802_3;
    GenAttrs.PhysicalMediumType = NdisPhysicalMedium802_3;
    GenAttrs.MtuSize = ETHERNET_MAX_MTU;
    GenAttrs.MaxXmitLinkSpeed = 1000000000;  /* 1 Gbps */
    GenAttrs.MaxRcvLinkSpeed = 1000000000;
    GenAttrs.XmitLinkSpeed = Adapter->LinkSpeed;
    GenAttrs.RcvLinkSpeed = Adapter->LinkSpeed;
    GenAttrs.MediaConnectState = Adapter->MediaState;
    GenAttrs.MediaDuplexState = MediaDuplexStateFull;

    /* Lookahead size */
    GenAttrs.LookaheadSize = ETHERNET_MAX_FRAME_SIZE - ETHERNET_HEADER_SIZE;

    /* MAC options */
    GenAttrs.MacOptions = NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                          NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                          NDIS_MAC_OPTION_NO_LOOPBACK;

    /* Supported packet filters */
    GenAttrs.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                      NDIS_PACKET_TYPE_MULTICAST |
                                      NDIS_PACKET_TYPE_ALL_MULTICAST |
                                      NDIS_PACKET_TYPE_BROADCAST |
                                      NDIS_PACKET_TYPE_PROMISCUOUS;

    /* Maximum multicast addresses */
    GenAttrs.MaxMulticastListSize = RNDIS_MAX_MULTICAST_ADDRESSES;

    /* MAC address length */
    GenAttrs.MacAddressLength = ETH_LENGTH_OF_ADDRESS;

    /* Copy MAC addresses */
    NdisMoveMemory(GenAttrs.PermanentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
    NdisMoveMemory(GenAttrs.CurrentMacAddress, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);

    /* Statistics - indicate we support basic counters */
    GenAttrs.SupportedStatistics =
        NDIS_STATISTICS_XMIT_OK_SUPPORTED |
        NDIS_STATISTICS_RCV_OK_SUPPORTED |
        NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED;

    /* RSS capabilities - not supported */
    GenAttrs.RecvScaleCapabilities = NULL;

    /* Access type */
    GenAttrs.AccessType = NET_IF_ACCESS_BROADCAST;
    GenAttrs.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GenAttrs.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    GenAttrs.IfType = IF_TYPE_ETHERNET_CSMACD;
    GenAttrs.IfConnectorPresent = TRUE;

    /* Power management */
    GenAttrs.SupportedPauseFunctions = NdisPauseFunctionsUnsupported;
    GenAttrs.AutoNegotiationFlags =
        NDIS_LINK_STATE_XMIT_LINK_SPEED_AUTO_NEGOTIATED |
        NDIS_LINK_STATE_RCV_LINK_SPEED_AUTO_NEGOTIATED |
        NDIS_LINK_STATE_DUPLEX_AUTO_NEGOTIATED;

    /* Supported OIDs - set to NULL, will be handled by OidRequest */
    GenAttrs.SupportedOidList = NULL;
    GenAttrs.SupportedOidListLength = 0;

    Status = NdisMSetMiniportAttributes(
        Adapter->MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&GenAttrs);

    return Status;
}

/*
 * RndisAllocateNblPool
 *
 * Allocate NET_BUFFER_LIST pool for receive indications
 */
static
NDIS_STATUS
RndisAllocateNblPool(
    _In_ PRNDIS_ADAPTER Adapter)
{
    NET_BUFFER_LIST_POOL_PARAMETERS NblPoolParams;

    NdisZeroMemory(&NblPoolParams, sizeof(NblPoolParams));
    NblPoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    NblPoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    NblPoolParams.Header.Size = sizeof(NET_BUFFER_LIST_POOL_PARAMETERS);
    NblPoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    NblPoolParams.fAllocateNetBuffer = TRUE;
    NblPoolParams.ContextSize = 0;
    NblPoolParams.PoolTag = USBRNDIS_TAG;
    NblPoolParams.DataSize = RNDIS_MAX_TRANSFER_SIZE;

    Adapter->RxNblPool = NdisAllocateNetBufferListPool(
        Adapter->MiniportAdapterHandle,
        &NblPoolParams);

    if (Adapter->RxNblPool == NULL)
    {
        DPRINT1("USBRNDIS: Failed to allocate RX NBL pool\n");
        return NDIS_STATUS_RESOURCES;
    }

    DPRINT("USBRNDIS: Allocated RX NBL pool: %p\n", Adapter->RxNblPool);
    return NDIS_STATUS_SUCCESS;
}

/*
 * RndisMiniportInitializeEx
 *
 * NDIS 6.x adapter initialization handler
 */
NDIS_STATUS
NTAPI
RndisMiniportInitializeEx(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    NDIS_STATUS NdisStatus;
    NTSTATUS NtStatus;
    PRNDIS_ADAPTER Adapter = NULL;
    PDEVICE_OBJECT PhysicalDeviceObject;
    PDEVICE_OBJECT LowerDeviceObject;
    PDEVICE_OBJECT DeviceObject;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    DPRINT1("USBRNDIS: RndisMiniportInitializeEx called\n");

    /* Allocate adapter context */
    Adapter = NdisAllocateMemoryWithTagPriority(
        NdisMiniportHandle,
        sizeof(RNDIS_ADAPTER),
        USBRNDIS_TAG,
        NormalPoolPriority);

    if (!Adapter)
    {
        DPRINT1("USBRNDIS: Failed to allocate adapter context\n");
        return NDIS_STATUS_RESOURCES;
    }

    NdisZeroMemory(Adapter, sizeof(RNDIS_ADAPTER));

    /* Initialize adapter fields */
    Adapter->MiniportAdapterHandle = NdisMiniportHandle;
    Adapter->State = RndisStateUninitialized;
    Adapter->MediaState = MediaConnectStateDisconnected;
    Adapter->LinkSpeed = 100000000;  /* 100 Mbps default in bps for NDIS 6.x */
    Adapter->PacketFilter = 0;
    Adapter->RequestId = 1;
    Adapter->Halting = FALSE;
    Adapter->Paused = FALSE;
    Adapter->PendingIoCount = 0;
    Adapter->RxIrp = NULL;
    Adapter->TxIrp = NULL;
    Adapter->PendingTxNbl = NULL;
    Adapter->IsCdcEcm = FALSE;
    Adapter->IsCdcNcm = FALSE;
    Adapter->RxSubmitted = FALSE;
    Adapter->InterruptIrp = NULL;
    Adapter->InterruptSubmitted = FALSE;
    Adapter->InterruptBuffer = NULL;
    Adapter->InterruptBufferLength = 0;
    Adapter->RxConsecutiveErrors = 0;
    Adapter->DataInterfaceNumber = 0xFF;
    Adapter->DataAlternateSetting = 0;
    Adapter->RxNblPool = NULL;

    /* Initialize synchronization */
    KeInitializeEvent(&Adapter->ControlEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->RemoveEvent, NotificationEvent, TRUE);
    KeInitializeMutex(&Adapter->ControlMutex, 0);
    NdisAllocateSpinLock(&Adapter->TxLock);
    NdisAllocateSpinLock(&Adapter->RxLock);
    NdisAllocateSpinLock(&Adapter->InterruptLock);

    /* Initialize RX DPC and backoff timer early so halt can safely cancel them */
    RndisInitializeRxDpc(Adapter);
    RndisInitializeInterruptDpc(Adapter);

    /* Set registration attributes first (required before other attributes) */
    NdisStatus = RndisSetRegistrationAttributes(Adapter);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("USBRNDIS: Failed to set registration attributes: 0x%08x\n", NdisStatus);
        goto Cleanup;
    }

    /*
     * Get device objects from MiniportHandle.
     * In NDIS 6.x, NdisMiniportHandle is actually a DeviceObject.
     * The PDO is stored in DeviceExtension[1].
     */
    DeviceObject = (PDEVICE_OBJECT)NdisMiniportHandle;
    if (DeviceObject != NULL && DeviceObject->DeviceExtension != NULL)
    {
        PhysicalDeviceObject = ((PVOID*)DeviceObject->DeviceExtension)[1];
        LowerDeviceObject = IoGetAttachedDeviceReference(PhysicalDeviceObject);
        ObDereferenceObject(LowerDeviceObject);  /* Balance reference from IoGetAttachedDeviceReference */
    }
    else
    {
        DPRINT1("USBRNDIS: Failed to get device objects from MiniportHandle\n");
        NdisStatus = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    Adapter->PhysicalDeviceObject = PhysicalDeviceObject;
    Adapter->LowerDeviceObject = LowerDeviceObject;

    DPRINT1("USBRNDIS: PDO=%p LowerDO=%p (StackSize=%u)\n",
            PhysicalDeviceObject, LowerDeviceObject,
            LowerDeviceObject ? LowerDeviceObject->StackSize : 0);

    /* Allocate control buffer */
    Adapter->ControlBuffer = NdisAllocateMemoryWithTagPriority(
        NdisMiniportHandle,
        RNDIS_CONTROL_BUFFER_SIZE,
        USBRNDIS_TAG,
        NormalPoolPriority);
    if (!Adapter->ControlBuffer)
    {
        DPRINT1("USBRNDIS: Failed to allocate control buffer\n");
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    /* Get USB bus interface */
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
    Adapter->TxBuffer = NdisAllocateMemoryWithTagPriority(
        NdisMiniportHandle,
        RNDIS_MAX_TRANSFER_SIZE,
        USBRNDIS_TAG,
        NormalPoolPriority);
    if (!Adapter->TxBuffer)
    {
        DPRINT1("USBRNDIS: Failed to allocate TX buffer\n");
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    /* Allocate receive buffer */
    Adapter->RxBuffer = NdisAllocateMemoryWithTagPriority(
        NdisMiniportHandle,
        RNDIS_MAX_TRANSFER_SIZE,
        USBRNDIS_TAG,
        NormalPoolPriority);
    if (!Adapter->RxBuffer)
    {
        DPRINT1("USBRNDIS: Failed to allocate RX buffer\n");
        NdisStatus = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    if (Adapter->InterruptEndpoint.PipeHandle)
    {
        ULONG BufLen = Adapter->InterruptEndpoint.MaxPacketSize;

        if (BufLen < sizeof(USB_CDC_NOTIFICATION))
        {
            BufLen = sizeof(USB_CDC_NOTIFICATION);
        }

        Adapter->InterruptBuffer = NdisAllocateMemoryWithTagPriority(
            NdisMiniportHandle,
            BufLen,
            USBRNDIS_TAG,
            NormalPoolPriority);
        if (!Adapter->InterruptBuffer)
        {
            DPRINT1("USBRNDIS: Failed to allocate interrupt buffer\n");
        }
        else
        {
            Adapter->InterruptBufferLength = BufLen;
        }
    }

    /* Allocate NET_BUFFER_LIST pool for receives */
    NdisStatus = RndisAllocateNblPool(Adapter);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        goto Cleanup;
    }

    if (Adapter->IsCdcEcm || Adapter->IsCdcNcm)
    {
        /* CDC-ECM/NCM mode: No RNDIS initialization needed */
        if (Adapter->IsCdcNcm)
        {
            DPRINT1("USBRNDIS: CDC-NCM mode - using NTB framing\n");

            Adapter->NcmTxSequence = 0;
            Adapter->NcmNtbMaxSize = NCM_DEFAULT_NTB_MAX_SIZE;
            Adapter->NcmNtbOutMaxSize = NCM_DEFAULT_NTB_MAX_SIZE;
            Adapter->NcmNdpDivisor = NCM_DEFAULT_NDP_DIVISOR;
            Adapter->NcmNdpRemainder = NCM_DEFAULT_NDP_REMAINDER;
            Adapter->NcmNdpAlignment = NCM_DEFAULT_NDP_ALIGNMENT;

            RndisNcmSetup(Adapter);

            DPRINT1("USBRNDIS: NCM params after setup: MaxNTB=%lu Divisor=%u Remainder=%u Alignment=%u\n",
                    Adapter->NcmNtbMaxSize, Adapter->NcmNdpDivisor,
                    Adapter->NcmNdpRemainder, Adapter->NcmNdpAlignment);
        }
        else
        {
            DPRINT1("USBRNDIS: CDC-ECM mode - skipping RNDIS initialization\n");
        }

        Adapter->MaxTransferSize = RNDIS_MAX_TRANSFER_SIZE;
        Adapter->PacketAlignmentFactor = 0;
        Adapter->MaxPacketsPerMessage = 1;

        /*
         * Generate a locally-administered MAC address.
         * Use VID, PID, and interface number for stability across boots.
         * Byte 0 has bit 1 set (locally administered) and bit 0 clear (unicast).
         * Byte 5 uses control interface number XORed with bcdDevice for uniqueness
         * across multiple devices of the same VID/PID on different ports.
         */
        {
            USHORT bcdDevice = Adapter->DeviceDescriptor->bcdDevice;

            Adapter->PermanentMacAddress[0] = 0x02;  /* Locally administered, unicast */
            Adapter->PermanentMacAddress[1] = (UCHAR)(Adapter->DeviceDescriptor->idVendor >> 8);
            Adapter->PermanentMacAddress[2] = (UCHAR)(Adapter->DeviceDescriptor->idVendor & 0xFF);
            Adapter->PermanentMacAddress[3] = (UCHAR)(Adapter->DeviceDescriptor->idProduct >> 8);
            Adapter->PermanentMacAddress[4] = (UCHAR)(Adapter->DeviceDescriptor->idProduct & 0xFF);
            Adapter->PermanentMacAddress[5] = (UCHAR)(Adapter->ControlInterfaceNumber ^
                                                       (bcdDevice & 0xFF) ^
                                                       ((bcdDevice >> 8) & 0xFF));
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

        /* Set default packet filter */
        NtStatus = RndisSetPacketFilter(Adapter, RNDIS_DEFAULT_FILTER);
        if (!NT_SUCCESS(NtStatus))
        {
            DPRINT1("USBRNDIS: Failed to set packet filter (0x%08X)\n", NtStatus);
        }

        Adapter->State = RndisStateDataInitialized;
    }

    /* Query/set media connect status */
    if (Adapter->IsCdcEcm || Adapter->IsCdcNcm)
    {
        Adapter->MediaState = MediaConnectStateConnected;
        DPRINT1("USBRNDIS: %s media state: Connected (assumed)\n",
                Adapter->IsCdcNcm ? "CDC-NCM" : "CDC-ECM");
    }
    else
    {
        ULONG MediaConnectStatus = 0;
        ULONG BytesWritten = 0;

        NtStatus = RndisQueryOid(Adapter, RNDIS_OID_GEN_MEDIA_CONNECT_STATUS,
                                 &MediaConnectStatus, sizeof(MediaConnectStatus),
                                 &BytesWritten);

        if (NT_SUCCESS(NtStatus) && BytesWritten == sizeof(MediaConnectStatus))
        {
            if (MediaConnectStatus == 0)
            {
                Adapter->MediaState = MediaConnectStateConnected;
                DPRINT1("USBRNDIS: Media state: Connected\n");
            }
            else
            {
                Adapter->MediaState = MediaConnectStateDisconnected;
                DPRINT1("USBRNDIS: Media state: Disconnected\n");
            }
        }
        else
        {
            DPRINT1("USBRNDIS: Failed to query media status (0x%08X), assuming disconnected\n", NtStatus);
            Adapter->MediaState = MediaConnectStateDisconnected;
        }
    }

    /* Set general attributes */
    NdisStatus = RndisSetGeneralAttributes(Adapter);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("USBRNDIS: Failed to set general attributes: 0x%08x\n", NdisStatus);
        goto Cleanup;
    }

    /* Start receiving data */
    RndisUsbSubmitBulkRead(Adapter);
    if (Adapter->InterruptEndpoint.PipeHandle && Adapter->InterruptBuffer)
    {
        RndisUsbSubmitInterruptRead(Adapter);
    }

    DPRINT1("USBRNDIS: Initialization complete\n");
    return NDIS_STATUS_SUCCESS;

Cleanup:
    if (Adapter)
    {
        RndisMiniportHaltEx((NDIS_HANDLE)Adapter, NdisHaltDeviceInitializationFailed);
    }
    return NdisStatus;
}

/*
 * RndisMiniportHaltEx
 *
 * NDIS 6.x adapter halt handler
 */
VOID
NTAPI
RndisMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    PIRP RxIrp;
    PIRP TxIrp;

    UNREFERENCED_PARAMETER(HaltAction);

    DPRINT1("USBRNDIS: RndisMiniportHaltEx called (Action=%d)\n", HaltAction);

    if (!Adapter)
    {
        return;
    }

    /* Signal that we are halting */
    Adapter->Halting = TRUE;

    /* Cancel RX timers */
    KeCancelTimer(&Adapter->RxBackoffTimer);
    KeCancelTimer(&Adapter->RxDelayTimer);

    /* Remove any queued DPCs */
    KeRemoveQueueDpc(&Adapter->RxResubmitDpc);
    KeRemoveQueueDpc(&Adapter->RxBackoffDpc);
    KeRemoveQueueDpc(&Adapter->RxDelayDpc);
    KeRemoveQueueDpc(&Adapter->InterruptResubmitDpc);

    /* Acquire ControlMutex before sending halt */
    KeWaitForSingleObject(&Adapter->ControlMutex, Executive, KernelMode, FALSE, NULL);

    /* Send RNDIS halt message (skip for CDC-ECM/NCM) */
    if (Adapter->State >= RndisStateInitialized && !Adapter->IsCdcEcm && !Adapter->IsCdcNcm)
    {
        RndisHaltDevice(Adapter);
    }

    KeReleaseMutex(&Adapter->ControlMutex, FALSE);

    Adapter->State = RndisStateHalted;

    /* Cancel pending RX IRP */
    NdisAcquireSpinLock(&Adapter->RxLock);
    RxIrp = Adapter->RxIrp;
    Adapter->RxIrp = NULL;
    NdisReleaseSpinLock(&Adapter->RxLock);

    if (RxIrp)
    {
        DPRINT1("USBRNDIS: Cancelling pending RX IRP\n");
        IoCancelIrp(RxIrp);
    }

    /* Cancel pending TX IRP */
    NdisAcquireSpinLock(&Adapter->TxLock);
    TxIrp = Adapter->TxIrp;
    Adapter->TxIrp = NULL;
    NdisReleaseSpinLock(&Adapter->TxLock);

    if (TxIrp)
    {
        DPRINT1("USBRNDIS: Cancelling pending TX IRP\n");
        IoCancelIrp(TxIrp);
    }

    /* Cancel pending interrupt IRP */
    NdisAcquireSpinLock(&Adapter->InterruptLock);
    TxIrp = Adapter->InterruptIrp;
    Adapter->InterruptIrp = NULL;
    Adapter->InterruptSubmitted = FALSE;
    NdisReleaseSpinLock(&Adapter->InterruptLock);

    if (TxIrp)
    {
        DPRINT1("USBRNDIS: Cancelling pending interrupt IRP\n");
        IoCancelIrp(TxIrp);
    }

    /* Wait for all pending I/O to complete */
    if (Adapter->PendingIoCount > 0)
    {
        DPRINT1("USBRNDIS: Waiting for %ld pending I/O operations\n", Adapter->PendingIoCount);
        KeWaitForSingleObject(&Adapter->RemoveEvent, Executive, KernelMode, FALSE, NULL);
        DPRINT1("USBRNDIS: All pending I/O completed\n");
    }

    /* Release USB bus interface */
    if (Adapter->BusInterface.InterfaceDereference)
    {
        Adapter->BusInterface.InterfaceDereference(Adapter->BusInterface.BusContext);
        Adapter->BusInterface.InterfaceDereference = NULL;
    }

    /* Free NBL pool */
    if (Adapter->RxNblPool != NULL)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
        Adapter->RxNblPool = NULL;
    }

    /* Free spin locks */
    NdisFreeSpinLock(&Adapter->TxLock);
    NdisFreeSpinLock(&Adapter->RxLock);
    NdisFreeSpinLock(&Adapter->InterruptLock);

    /* Free buffers */
    if (Adapter->RxBuffer)
    {
        NdisFreeMemory(Adapter->RxBuffer, 0, 0);
        Adapter->RxBuffer = NULL;
    }

    if (Adapter->TxBuffer)
    {
        NdisFreeMemory(Adapter->TxBuffer, 0, 0);
        Adapter->TxBuffer = NULL;
    }

    if (Adapter->InterruptBuffer)
    {
        NdisFreeMemory(Adapter->InterruptBuffer, 0, 0);
        Adapter->InterruptBuffer = NULL;
    }

    if (Adapter->ControlBuffer)
    {
        NdisFreeMemory(Adapter->ControlBuffer, 0, 0);
        Adapter->ControlBuffer = NULL;
    }

    /* Free USB descriptors */
    if (Adapter->DeviceDescriptor)
    {
        NdisFreeMemory(Adapter->DeviceDescriptor, 0, 0);
        Adapter->DeviceDescriptor = NULL;
    }

    if (Adapter->ConfigurationDescriptor)
    {
        NdisFreeMemory(Adapter->ConfigurationDescriptor, 0, 0);
        Adapter->ConfigurationDescriptor = NULL;
    }

    if (Adapter->ControlInterface)
    {
        NdisFreeMemory(Adapter->ControlInterface, 0, 0);
        Adapter->ControlInterface = NULL;
    }

    if (Adapter->DataInterface)
    {
        NdisFreeMemory(Adapter->DataInterface, 0, 0);
        Adapter->DataInterface = NULL;
    }

    /* Free adapter context */
    NdisFreeMemory(Adapter, sizeof(RNDIS_ADAPTER), 0);

    DPRINT1("USBRNDIS: Halt complete\n");
}

/*
 * RndisMiniportPause
 *
 * NDIS 6.x pause handler - stop data path
 */
NDIS_STATUS
NTAPI
RndisMiniportPause(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS PauseParameters)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(PauseParameters);

    DPRINT1("USBRNDIS: RndisMiniportPause\n");

    Adapter->Paused = TRUE;

    return NDIS_STATUS_SUCCESS;
}

/*
 * RndisMiniportRestart
 *
 * NDIS 6.x restart handler - resume data path
 */
NDIS_STATUS
NTAPI
RndisMiniportRestart(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS RestartParameters)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(RestartParameters);

    DPRINT1("USBRNDIS: RndisMiniportRestart\n");

    Adapter->Paused = FALSE;

    return NDIS_STATUS_SUCCESS;
}

/*
 * RndisMiniportShutdownEx
 *
 * NDIS 6.x shutdown handler.
 *
 * IMPORTANT: This function can be called at DISPATCH_LEVEL during
 * bugcheck scenarios (NdisShutdownPowerOff). We MUST NOT call any
 * functions that wait (KeWaitForSingleObject, etc.) when at raised IRQL.
 *
 * RndisHaltDevice calls RndisUsbSendControlMessage which uses
 * RndisSyncUrbRequest - a synchronous wait. Therefore we can only
 * send the halt message if we're at PASSIVE_LEVEL.
 */
VOID
NTAPI
RndisMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;
    KIRQL CurrentIrql;

    DPRINT1("USBRNDIS: RndisMiniportShutdownEx (Action=%d)\n", ShutdownAction);

    if (!Adapter || Adapter->State < RndisStateInitialized ||
        Adapter->IsCdcEcm || Adapter->IsCdcNcm)
    {
        return;
    }

    /*
     * Mark as halting to prevent new I/O operations.
     * This is safe at any IRQL.
     */
    Adapter->Halting = TRUE;

    /*
     * Check current IRQL. RndisHaltDevice uses synchronous waits which
     * can only be called at PASSIVE_LEVEL. If we're at DISPATCH_LEVEL
     * (bugcheck scenario), we skip sending the halt message.
     * The device will be reset by the power cycle anyway.
     */
    CurrentIrql = KeGetCurrentIrql();
    if (CurrentIrql >= DISPATCH_LEVEL)
    {
        DPRINT1("USBRNDIS: At DISPATCH_LEVEL (IRQL=%d), skipping RNDIS halt message\n",
                CurrentIrql);
        return;
    }

    /* At PASSIVE_LEVEL, safe to send halt message */
    RndisHaltDevice(Adapter);
}

/*
 * RndisMiniportDevicePnPEventNotify
 *
 * NDIS 6.x PnP event notification
 */
VOID
NTAPI
RndisMiniportDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PRNDIS_ADAPTER Adapter = (PRNDIS_ADAPTER)MiniportAdapterContext;

    switch (NetDevicePnPEvent->DevicePnPEvent)
    {
        case NdisDevicePnPEventSurpriseRemoved:
            DPRINT1("USBRNDIS: Surprise removal detected\n");
            Adapter->Halting = TRUE;
            break;

        case NdisDevicePnPEventPowerProfileChanged:
            DPRINT1("USBRNDIS: Power profile changed\n");
            break;

        default:
            DPRINT1("USBRNDIS: PnP event %d\n", NetDevicePnPEvent->DevicePnPEvent);
            break;
    }
}

/*
 * RndisMiniportDriverUnload
 *
 * NDIS 6.x driver unload handler
 */
VOID
NTAPI
RndisMiniportDriverUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    DPRINT1("USBRNDIS: RndisMiniportDriverUnload\n");

    if (g_NdisMiniportDriverHandle != NULL)
    {
        NdisDeregisterMiniportDriver(g_NdisMiniportDriverHandle);
        g_NdisMiniportDriverHandle = NULL;
    }

    DPRINT1("USBRNDIS: Driver unloaded\n");
}

/*
 * DriverEntry
 *
 * Main entry point for the driver - NDIS 6.x registration
 */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_STATUS Status;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS MiniportChars;

    DPRINT1("USBRNDIS: ========== DRIVER ENTRY ==========\n");
    DPRINT1("USBRNDIS: ReactOS USB RNDIS NDIS 6.x Driver\n");
    DPRINT1("USBRNDIS: NDIS Version: %u.%u\n", NDIS_MINIPORT_MAJOR_VERSION, NDIS_MINIPORT_MINOR_VERSION);

    g_DriverObject = DriverObject;

    /* Initialize miniport characteristics structure */
    NdisZeroMemory(&MiniportChars, sizeof(MiniportChars));

    /* Set NDIS object header */
    MiniportChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    MiniportChars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    MiniportChars.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);

    /* NDIS version requirements */
    MiniportChars.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    MiniportChars.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    MiniportChars.MajorDriverVersion = 1;
    MiniportChars.MinorDriverVersion = 0;

    /* Mandatory handlers - cast to proper pointer types */
    MiniportChars.InitializeHandlerEx = (MINIPORT_INITIALIZE *)&RndisMiniportInitializeEx;
    MiniportChars.HaltHandlerEx = (MINIPORT_HALT *)&RndisMiniportHaltEx;
    MiniportChars.UnloadHandler = (MINIPORT_UNLOAD *)&RndisMiniportDriverUnload;
    MiniportChars.PauseHandler = (MINIPORT_PAUSE *)&RndisMiniportPause;
    MiniportChars.RestartHandler = (MINIPORT_RESTART *)&RndisMiniportRestart;
    MiniportChars.OidRequestHandler = (MINIPORT_OID_REQUEST *)&RndisOidRequest;
    MiniportChars.SendNetBufferListsHandler = (MINIPORT_SEND_NET_BUFFER_LISTS *)&RndisSendNetBufferLists;
    MiniportChars.ReturnNetBufferListsHandler = (MINIPORT_RETURN_NET_BUFFER_LISTS *)&RndisReturnNetBufferLists;
    MiniportChars.CancelSendHandler = (MINIPORT_CANCEL_SEND *)&RndisCancelSend;
    MiniportChars.DevicePnPEventNotifyHandler = (MINIPORT_DEVICE_PNP_EVENT_NOTIFY *)&RndisMiniportDevicePnPEventNotify;
    MiniportChars.ShutdownHandlerEx = (MINIPORT_SHUTDOWN *)&RndisMiniportShutdownEx;
    MiniportChars.CancelOidRequestHandler = (MINIPORT_CANCEL_OID_REQUEST *)&RndisCancelOidRequest;

    /* Optional handlers */
    MiniportChars.CheckForHangHandlerEx = NULL;
    MiniportChars.ResetHandlerEx = NULL;

    /* Flags */
    MiniportChars.Flags = 0;

    DPRINT1("USBRNDIS: Registering miniport driver with NDIS 6.%d\n",
            NDIS_MINIPORT_MINOR_VERSION);

    /* Register with NDIS */
    Status = NdisRegisterMiniportDriver(
        DriverObject,
        RegistryPath,
        NULL,
        &MiniportChars,
        &g_NdisMiniportDriverHandle);

    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("USBRNDIS: NdisRegisterMiniportDriver failed with status 0x%08x\n", Status);
        return Status;
    }

    DPRINT1("USBRNDIS: Driver registered successfully, handle = %p\n", g_NdisMiniportDriverHandle);
    return STATUS_SUCCESS;
}
