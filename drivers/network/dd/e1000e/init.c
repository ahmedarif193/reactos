/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NDIS 6.x Driver initialization and miniport registration
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 *
 * This file implements the NDIS 6.x DriverEntry and miniport initialization:
 *   - DriverEntry using NdisMRegisterMiniportDriver
 *   - E1000MiniportInitializeEx for adapter setup
 *   - E1000MiniportHaltEx for cleanup
 *   - MiniportDriverUnload
 */

/* Include initguid.h before any header that uses DEFINE_GUID to instantiate GUIDs */
#include <initguid.h>

#include "e1000.h"

/* Global driver handle */
NDIS_HANDLE g_NdisMiniportDriverHandle = NULL;

/* Driver object pointer for unload */
PDRIVER_OBJECT g_DriverObject = NULL;

/* Debug trace level */
#if DBG
#endif

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

static NDIS_STATUS
E1000SetRegistrationAttributes(
    _In_ PE1000_ADAPTER Adapter
    );

static NDIS_STATUS
E1000SetGeneralAttributes(
    _In_ PE1000_ADAPTER Adapter
    );

static NDIS_STATUS
E1000SetOffloadAttributes(
    _In_ PE1000_ADAPTER Adapter
    );

static NDIS_STATUS
E1000MapHardwareResources(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_RESOURCE_LIST ResourceList
    );


/* ============================================================================
 * DriverEntry - Main driver entry point
 *
 * Registers the miniport driver with NDIS 6.x using NdisMRegisterMiniportDriver.
 * ============================================================================ */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath
    )
{
    NDIS_STATUS Status;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS MiniportChars;

    DPRINT1("E1000: ========== DRIVER ENTRY ==========\n");
    DPRINT1("E1000: ReactOS Intel PRO/1000 NDIS 6.x Driver\n");
    DPRINT1("E1000: Version %u.%u, Build Date: " __DATE__ " " __TIME__ "\n",
             DRIVER_VERSION >> 8, DRIVER_VERSION & 0xFF);
    DPRINT1("E1000: NDIS Version: %u.%u\n", NDIS_MINIPORT_MAJOR_VERSION, NDIS_MINIPORT_MINOR_VERSION);

    g_DriverObject = DriverObject;

    /* Initialize the miniport characteristics structure */
    NdisZeroMemory(&MiniportChars, sizeof(MiniportChars));

    /* Set NDIS object header */
    MiniportChars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    MiniportChars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    MiniportChars.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);

    /* NDIS version requirements */
    MiniportChars.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    MiniportChars.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    MiniportChars.MajorDriverVersion = 2;
    MiniportChars.MinorDriverVersion = 0;

    /* Mandatory handlers - cast to proper pointer types */
    MiniportChars.InitializeHandlerEx = (MINIPORT_INITIALIZE *)&E1000MiniportInitializeEx;
    MiniportChars.HaltHandlerEx = (MINIPORT_HALT *)&E1000MiniportHaltEx;
    MiniportChars.UnloadHandler = (MINIPORT_UNLOAD *)&E1000MiniportDriverUnload;
    MiniportChars.PauseHandler = (MINIPORT_PAUSE *)&E1000MiniportPause;
    MiniportChars.RestartHandler = (MINIPORT_RESTART *)&E1000MiniportRestart;
    MiniportChars.OidRequestHandler = (MINIPORT_OID_REQUEST *)&E1000OidRequest;
    MiniportChars.SendNetBufferListsHandler = (MINIPORT_SEND_NET_BUFFER_LISTS *)&E1000SendNetBufferLists;
    MiniportChars.ReturnNetBufferListsHandler = (MINIPORT_RETURN_NET_BUFFER_LISTS *)&E1000ReturnNetBufferLists;
    MiniportChars.CancelSendHandler = (MINIPORT_CANCEL_SEND *)&E1000CancelSend;
    MiniportChars.DevicePnPEventNotifyHandler = (MINIPORT_DEVICE_PNP_EVENT_NOTIFY *)&E1000MiniportDevicePnPEventNotify;
    MiniportChars.ShutdownHandlerEx = (MINIPORT_SHUTDOWN *)&E1000MiniportShutdownEx;
    MiniportChars.CancelOidRequestHandler = (MINIPORT_CANCEL_OID_REQUEST *)&E1000CancelOidRequest;

    /* Optional handlers - set to NULL for now, NDIS will use defaults */
    MiniportChars.CheckForHangHandlerEx = NULL;
    MiniportChars.ResetHandlerEx = NULL;

    /* Set flags - NDIS 6.30 allows interrupt moderation */
    MiniportChars.Flags = 0;

    DPRINT("E1000: Registering miniport driver with NDIS 6.%d\n",
             NDIS_MINIPORT_MINOR_VERSION);

    /* Register with NDIS — NdisMRegisterMiniportDriver is the correct
     * MS DDK name; the dev branch had the M-less typo. */
    Status = NdisMRegisterMiniportDriver(
                DriverObject,
                RegistryPath,
                NULL,                       /* MiniportDriverContext */
                &MiniportChars,
                &g_NdisMiniportDriverHandle
                );

    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: NdisMRegisterMiniportDriver failed with status 0x%08x\n", Status);
        return Status;
    }

    DPRINT1("E1000: Driver registered successfully, handle = %p\n", g_NdisMiniportDriverHandle);

    return STATUS_SUCCESS;
}


/* ============================================================================
 * E1000MiniportDriverUnload - Unload the miniport driver
 *
 * Called when the driver is being unloaded from the system.
 * ============================================================================ */

VOID
NTAPI
E1000MiniportDriverUnload(
    _In_ PDRIVER_OBJECT DriverObject
    )
{
    UNREFERENCED_PARAMETER(DriverObject);

    DPRINT1("E1000: MiniportDriverUnload\n");

    if (g_NdisMiniportDriverHandle != NULL)
    {
        NdisMDeregisterMiniportDriver(g_NdisMiniportDriverHandle);
        g_NdisMiniportDriverHandle = NULL;
    }

    DPRINT1("E1000: Driver unloaded\n");
}


/* ============================================================================
 * E1000MiniportInitializeEx - Initialize a miniport adapter
 *
 * Called by NDIS when a new adapter is found. This function:
 *   1. Allocates and initializes the adapter context
 *   2. Reads hardware resources from the configuration
 *   3. Maps hardware resources (I/O ports, memory)
 *   4. Initializes the hardware
 *   5. Allocates TX/RX descriptor rings and buffers
 *   6. Registers the interrupt handler
 *   7. Sets adapter attributes with NDIS
 * ============================================================================ */

NDIS_STATUS
NTAPI
E1000MiniportInitializeEx(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters
    )
{
    PE1000_ADAPTER Adapter = NULL;
    NDIS_STATUS Status;
    PNDIS_RESOURCE_LIST ResourceList;
    ULONG i;

    UNREFERENCED_PARAMETER(MiniportDriverContext);

    DPRINT1("E1000: ========== ADAPTER INITIALIZATION START ==========\n");
    DPRINT1("E1000: MiniportInitializeEx - NdisMiniportHandle=%p\n", NdisMiniportHandle);

    /* Allocate adapter context */
    Adapter = NdisAllocateMemoryWithTagPriority(
                    NdisMiniportHandle,
                    sizeof(E1000_ADAPTER),
                    'E1K0',
                    NormalPoolPriority
                    );

    if (Adapter == NULL)
    {
        DPRINT1("E1000: Failed to allocate adapter context\n");
        return NDIS_STATUS_RESOURCES;
    }

    /* Zero-initialize the adapter structure */
    NdisZeroMemory(Adapter, sizeof(E1000_ADAPTER));

    /* Store handles */
    Adapter->MiniportAdapterHandle = NdisMiniportHandle;
    Adapter->NdisMiniportDriverHandle = g_NdisMiniportDriverHandle;

    /*
     * Get the PDO via the proper NDIS 6 API. NdisMGetDeviceProperty
     * returns the PDO/FDO/next-lower DO and (optionally) the resource
     * lists. The dev-nt6-1 NDIS 6 bridge populates these from the
     * NDIS6_ADAPTER_EXT recorded at AddDevice time.
     */
    NdisMGetDeviceProperty(NdisMiniportHandle,
                           &Adapter->PhysicalDeviceObject,
                           NULL,    /* FDO not needed */
                           NULL,    /* NextDeviceObject not needed */
                           NULL,    /* AllocatedResources — use param below */
                           NULL);
    if (Adapter->PhysicalDeviceObject != NULL)
    {
        DPRINT1("E1000: PDO=%p from NdisMGetDeviceProperty\n",
                Adapter->PhysicalDeviceObject);
    }
    else
    {
        DPRINT1("E1000: Warning - NdisMGetDeviceProperty returned NULL PDO\n");
    }

    /* Initialize spin locks */
    NdisAllocateSpinLock(&Adapter->SendLock);
    NdisAllocateSpinLock(&Adapter->ResetLock);

    /* Initialize default queue counts */
    Adapter->TxQueueCount = 1;  /* Start with single queue, upgrade to 2 for MSI-X */
    Adapter->RxQueueCount = 1;

    /* Set registration attributes first (required before other attributes) */
    Status = E1000SetRegistrationAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: Failed to set registration attributes: 0x%08x\n", Status);
        goto Cleanup;
    }

    /* Get allocated resources */
    ResourceList = MiniportInitParameters->AllocatedResources;
    if (ResourceList == NULL)
    {
        DPRINT1("E1000: No resources allocated for adapter\n");
        Status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    /* Parse PCI configuration to get VendorId, DeviceId */
    Status = E1000MapHardwareResources(Adapter, ResourceList);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: Failed to map hardware resources: 0x%08x\n", Status);
        goto Cleanup;
    }

    /* Verify this is a supported Intel E1000 device */
    if (!E1000RecognizeHardware(Adapter))
    {
        DPRINT1("E1000: Unrecognized hardware (Vendor=0x%04x, Device=0x%04x)\n",
                 Adapter->VendorId, Adapter->DeviceId);
        Status = NDIS_STATUS_ADAPTER_NOT_FOUND;
        goto Cleanup;
    }

    DPRINT1("E1000: Found Intel E1000 device (0x%04x:0x%04x), IsPCIe=%d\n",
             Adapter->VendorId, Adapter->DeviceId, Adapter->IsPCIe);

    /* Allocate adapter hardware resources (descriptor rings, buffers) */
    Status = E1000AllocateAdapterResources(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: Failed to allocate adapter resources: 0x%08x\n", Status);
        goto Cleanup;
    }

    /* Initialize hardware */
    Status = E1000InitializeHardware(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: Failed to initialize hardware: 0x%08x\n", Status);
        goto Cleanup;
    }

    /* Read MAC address from EEPROM */
    Status = E1000ReadMacAddress(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: Failed to read MAC address: 0x%08x\n", Status);
        goto Cleanup;
    }

    DPRINT1("E1000: MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
             Adapter->PermanentMacAddress[0], Adapter->PermanentMacAddress[1],
             Adapter->PermanentMacAddress[2], Adapter->PermanentMacAddress[3],
             Adapter->PermanentMacAddress[4], Adapter->PermanentMacAddress[5]);

    /* Copy permanent MAC to current MAC */
    NdisMoveMemory(Adapter->CurrentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);

    /* Initialize TX queues */
    for (i = 0; i < Adapter->TxQueueCount; i++)
    {
        Status = E1000InitializeTxQueue(Adapter, i);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DPRINT1("E1000: Failed to initialize TX queue %u: 0x%08x\n", i, Status);
            goto Cleanup;
        }
    }

    /* Initialize RX queues */
    for (i = 0; i < Adapter->RxQueueCount; i++)
    {
        Status = E1000InitializeRxQueue(Adapter, i);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DPRINT1("E1000: Failed to initialize RX queue %u: 0x%08x\n", i, Status);
            goto Cleanup;
        }
    }

    /* Register interrupt handler */
    Status = E1000RegisterInterrupt(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT("E1000: Failed to register interrupt: 0x%08x\n", Status);
        goto Cleanup;
    }

    /* Set general adapter attributes */
    Status = E1000SetGeneralAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: Failed to set general attributes: 0x%08x\n", Status);
        goto Cleanup;
    }

    /* Set offload attributes */
    Status = E1000SetOffloadAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: Failed to set offload attributes: 0x%08x\n", Status);
        goto Cleanup;
    }

    /* Setup link */
    Status = E1000SetupLink(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: Failed to setup link: 0x%08x\n", Status);
        /* Non-fatal - link may come up later */
    }

    /* Mark adapter as started */
    InterlockedOr(&Adapter->Flags, E1000_FLAG_ADAPTER_STARTED);

    /* NDIS 6 miniports are in the Paused state after MiniportInitializeEx;
     * MiniportRestart starts the datapath. E1000MiniportRestart requires
     * E1000_FLAG_ADAPTER_PAUSED, so set it here or the first protocol bind fails. */
    InterlockedOr(&Adapter->Flags, E1000_FLAG_ADAPTER_PAUSED);

    /* Enable interrupts */
    E1000EnableInterrupts(Adapter);

    /* Print initialization summary */
    DPRINT1("E1000: ========== INITIALIZATION COMPLETE ==========\n");
    DPRINT1("E1000: Device: 0x%04x:0x%04x, PCIe=%d\n",
             Adapter->VendorId, Adapter->DeviceId, Adapter->IsPCIe);
    DPRINT1("E1000: MAC Address: %02x:%02x:%02x:%02x:%02x:%02x\n",
             Adapter->PermanentMacAddress[0], Adapter->PermanentMacAddress[1],
             Adapter->PermanentMacAddress[2], Adapter->PermanentMacAddress[3],
             Adapter->PermanentMacAddress[4], Adapter->PermanentMacAddress[5]);
    DPRINT1("E1000: I/O Base: %p (PA: 0x%I64x, Len: %u)\n",
             Adapter->IoBase, Adapter->IoAddress.QuadPart, Adapter->IoLength);
    DPRINT("E1000: Interrupt: Vector=%u Level=%u Shared=%d\n",
             Adapter->InterruptVector, Adapter->InterruptLevel, Adapter->InterruptShared);
    DPRINT1("E1000: Queues: TX=%u RX=%u\n", Adapter->TxQueueCount, Adapter->RxQueueCount);
    DPRINT1("E1000: DMA Handle: %p, NBL Pool: %p\n",
             Adapter->DmaAdapterHandle, Adapter->RxNblPool);
    DPRINT1("E1000: ==============================================\n");

    return NDIS_STATUS_SUCCESS;

Cleanup:
    if (Adapter != NULL)
    {
        E1000FreeAdapterResources(Adapter);
        NdisFreeSpinLock(&Adapter->SendLock);
        NdisFreeSpinLock(&Adapter->ResetLock);
        NdisFreeMemory(Adapter, sizeof(E1000_ADAPTER), 0);
    }

    return Status;
}


/* ============================================================================
 * E1000MiniportHaltEx - Halt the miniport adapter
 *
 * Called by NDIS when the adapter is being removed or the driver unloaded.
 * ============================================================================ */

VOID
NTAPI
E1000MiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    ULONG i;

    UNREFERENCED_PARAMETER(HaltAction);

    DPRINT1("E1000: MiniportHaltEx - Halting adapter (Action=%d)\n", HaltAction);

    if (Adapter == NULL)
    {
        return;
    }

    /* Mark as halting */
    InterlockedOr(&Adapter->Flags, E1000_FLAG_ADAPTER_HALTING);
    InterlockedAnd(&Adapter->Flags, ~E1000_FLAG_ADAPTER_STARTED);

    /* Disable interrupts */
    E1000DisableInterrupts(Adapter);

    /* Deregister interrupt */
    E1000DeregisterInterrupt(Adapter);

    /* Stop transmit and receive engines */
    if (Adapter->IoBase != NULL)
    {
        E1000_WRITE_REG(Adapter, E1000_REG_TCTL, 0);
        E1000_WRITE_REG(Adapter, E1000_REG_RCTL, 0);
    }

    /* Free TX queues */
    for (i = 0; i < Adapter->TxQueueCount; i++)
    {
        E1000FreeTxQueue(&Adapter->TxQueues[i]);
    }

    /* Free RX queues */
    for (i = 0; i < Adapter->RxQueueCount; i++)
    {
        E1000FreeRxQueue(&Adapter->RxQueues[i]);
    }

    /* Free adapter resources */
    E1000FreeAdapterResources(Adapter);

    /* Release the bus interface reference if obtained */
    if (Adapter->BusInterfaceValid && Adapter->BusInterface.InterfaceDereference != NULL)
    {
        Adapter->BusInterface.InterfaceDereference(Adapter->BusInterface.Context);
        Adapter->BusInterfaceValid = FALSE;
    }

    /* Free spin locks */
    NdisFreeSpinLock(&Adapter->SendLock);
    NdisFreeSpinLock(&Adapter->ResetLock);

    /* Free adapter context */
    NdisFreeMemory(Adapter, sizeof(E1000_ADAPTER), 0);

    DPRINT1("E1000: Adapter halted\n");
}


/* ============================================================================
 * E1000MiniportShutdownEx - Shutdown handler for system shutdown
 * ============================================================================ */

VOID
NTAPI
E1000MiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;

    DPRINT1("E1000: MiniportShutdownEx (Action=%d)\n", ShutdownAction);

    if (Adapter == NULL || Adapter->IoBase == NULL)
    {
        return;
    }

    /* Disable interrupts */
    E1000DisableInterrupts(Adapter);

    /* Stop transmit and receive */
    E1000_WRITE_REG(Adapter, E1000_REG_TCTL, 0);
    E1000_WRITE_REG(Adapter, E1000_REG_RCTL, 0);

    /* If powering off, configure wake-on-LAN if enabled */
    if (ShutdownAction == NdisShutdownPowerOff && Adapter->WakeOnMagicPacket)
    {
        E1000ConfigureWakeOnLan(Adapter);
    }
}


/* ============================================================================
 * E1000MiniportDevicePnPEventNotify - PnP event notification
 * ============================================================================ */

VOID
NTAPI
E1000MiniportDevicePnPEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent
    )
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(Adapter);

    switch (NetDevicePnPEvent->DevicePnPEvent)
    {
        case NdisDevicePnPEventSurpriseRemoved:
            DPRINT1("E1000: Surprise removal detected\n");
            InterlockedOr(&Adapter->Flags, E1000_FLAG_ADAPTER_HALTING);
            break;

        case NdisDevicePnPEventPowerProfileChanged:
            DPRINT1("E1000: Power profile changed\n");
            break;

        default:
            DPRINT1("E1000: PnP event %d\n", NetDevicePnPEvent->DevicePnPEvent);
            break;
    }
}


/* ============================================================================
 * E1000SetRegistrationAttributes - Set miniport registration attributes
 * ============================================================================ */

static NDIS_STATUS
E1000SetRegistrationAttributes(
    _In_ PE1000_ADAPTER Adapter
    )
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RegAttrs;
    NDIS_STATUS Status;

    NdisZeroMemory(&RegAttrs, sizeof(RegAttrs));

    RegAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegAttrs.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES);

    RegAttrs.MiniportAdapterContext = Adapter;
    RegAttrs.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_HARDWARE_DEVICE |
                              NDIS_MINIPORT_ATTRIBUTES_BUS_MASTER |
                              NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    RegAttrs.CheckForHangTimeInSeconds = 4;
    RegAttrs.InterfaceType = NdisInterfacePci;

    Status = NdisMSetMiniportAttributes(
                Adapter->MiniportAdapterHandle,
                (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RegAttrs
                );

    return Status;
}


/* ============================================================================
 * E1000SetGeneralAttributes - Set miniport general attributes
 * ============================================================================ */

static NDIS_STATUS
E1000SetGeneralAttributes(
    _In_ PE1000_ADAPTER Adapter
    )
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
    GenAttrs.MtuSize = E1000_MAX_FRAME_SIZE - 14;  /* Subtract Ethernet header */
    GenAttrs.MaxXmitLinkSpeed = E1000_LINK_SPEED_1GBPS;
    GenAttrs.MaxRcvLinkSpeed = E1000_LINK_SPEED_1GBPS;
    GenAttrs.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    GenAttrs.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    GenAttrs.MediaConnectState = MediaConnectStateUnknown;
    GenAttrs.MediaDuplexState = MediaDuplexStateUnknown;

    /* Lookahead size */
    GenAttrs.LookaheadSize = E1000_MAX_FRAME_SIZE - 14;

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
    GenAttrs.MaxMulticastListSize = E1000_MAX_MULTICAST;

    /* MAC address length */
    GenAttrs.MacAddressLength = ETH_LENGTH_OF_ADDRESS;

    /* Copy MAC addresses */
    NdisMoveMemory(GenAttrs.PermanentMacAddress, Adapter->PermanentMacAddress, ETH_LENGTH_OF_ADDRESS);
    NdisMoveMemory(GenAttrs.CurrentMacAddress, Adapter->CurrentMacAddress, ETH_LENGTH_OF_ADDRESS);

    /* Statistics - indicate we support 64-bit counters */
    GenAttrs.SupportedStatistics =
        NDIS_STATISTICS_XMIT_OK_SUPPORTED |
        NDIS_STATISTICS_RCV_OK_SUPPORTED |
        NDIS_STATISTICS_XMIT_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_ERROR_SUPPORTED |
        NDIS_STATISTICS_RCV_NO_BUFFER_SUPPORTED |
        NDIS_STATISTICS_DIRECTED_BYTES_XMIT_SUPPORTED |
        NDIS_STATISTICS_DIRECTED_FRAMES_XMIT_SUPPORTED |
        NDIS_STATISTICS_MULTICAST_BYTES_XMIT_SUPPORTED |
        NDIS_STATISTICS_MULTICAST_FRAMES_XMIT_SUPPORTED |
        NDIS_STATISTICS_BROADCAST_BYTES_XMIT_SUPPORTED |
        NDIS_STATISTICS_BROADCAST_FRAMES_XMIT_SUPPORTED |
        NDIS_STATISTICS_DIRECTED_BYTES_RCV_SUPPORTED |
        NDIS_STATISTICS_DIRECTED_FRAMES_RCV_SUPPORTED |
        NDIS_STATISTICS_MULTICAST_BYTES_RCV_SUPPORTED |
        NDIS_STATISTICS_MULTICAST_FRAMES_RCV_SUPPORTED |
        NDIS_STATISTICS_BROADCAST_BYTES_RCV_SUPPORTED |
        NDIS_STATISTICS_BROADCAST_FRAMES_RCV_SUPPORTED |
        NDIS_STATISTICS_BYTES_RCV_SUPPORTED |
        NDIS_STATISTICS_BYTES_XMIT_SUPPORTED |
        NDIS_STATISTICS_GEN_STATISTICS_SUPPORTED;

    /* Record descriptor */
    GenAttrs.RecvScaleCapabilities = NULL;  /* Set in RSS initialization */

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

    /* Supported OIDs - will be set in oid.c */
    GenAttrs.SupportedOidList = NULL;
    GenAttrs.SupportedOidListLength = 0;

    Status = NdisMSetMiniportAttributes(
                Adapter->MiniportAdapterHandle,
                (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&GenAttrs
                );

    return Status;
}


/* ============================================================================
 * E1000SetOffloadAttributes - Set miniport offload attributes
 * ============================================================================ */

static NDIS_STATUS
E1000SetOffloadAttributes(
    _In_ PE1000_ADAPTER Adapter
    )
{
    NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES OffloadAttrs;
    NDIS_OFFLOAD DefaultOffload;
    NDIS_OFFLOAD HardwareOffload;
    NDIS_STATUS Status;

    NdisZeroMemory(&OffloadAttrs, sizeof(OffloadAttrs));
    NdisZeroMemory(&DefaultOffload, sizeof(DefaultOffload));
    NdisZeroMemory(&HardwareOffload, sizeof(HardwareOffload));

    OffloadAttrs.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES;
    OffloadAttrs.Header.Revision = NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1;
    OffloadAttrs.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES);

    /* Initialize offload capabilities */
    Status = E1000InitializeOffloadCapabilities(Adapter, &HardwareOffload);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        return Status;
    }

    /* Copy hardware capabilities to default (what's currently enabled) */
    NdisMoveMemory(&DefaultOffload, &HardwareOffload, sizeof(NDIS_OFFLOAD));

    OffloadAttrs.DefaultOffloadConfiguration = &DefaultOffload;
    OffloadAttrs.HardwareOffloadCapabilities = &HardwareOffload;

    /* TCP connection offload not supported */
    OffloadAttrs.DefaultTcpConnectionOffloadConfiguration = NULL;
    OffloadAttrs.TcpConnectionOffloadHardwareCapabilities = NULL;

    Status = NdisMSetMiniportAttributes(
                Adapter->MiniportAdapterHandle,
                (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&OffloadAttrs
                );

    return Status;
}


/* ============================================================================
 * E1000QueryBusInterface - Query BUS_INTERFACE_STANDARD from PDO
 *
 * This function sends an IRP_MN_QUERY_INTERFACE to the PDO to get the
 * BUS_INTERFACE_STANDARD interface for PCI configuration space access.
 * ============================================================================ */

static NDIS_STATUS
E1000QueryBusInterface(
    _In_ PE1000_ADAPTER Adapter
    )
{
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION IrpSp;
    PDEVICE_OBJECT TargetDevice;

    if (Adapter->PhysicalDeviceObject == NULL)
    {
        DPRINT1("E1000: No PDO available for bus interface query\n");
        return NDIS_STATUS_FAILURE;
    }

    /* Get the top of the device stack */
    TargetDevice = IoGetAttachedDeviceReference(Adapter->PhysicalDeviceObject);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                        TargetDevice,
                                        NULL,
                                        0,
                                        NULL,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL)
    {
        ObDereferenceObject(TargetDevice);
        DPRINT1("E1000: Failed to allocate IRP for bus interface query\n");
        return NDIS_STATUS_RESOURCES;
    }

    /* Initialize the IRP */
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    IrpSp = IoGetNextIrpStackLocation(Irp);
    IrpSp->MajorFunction = IRP_MJ_PNP;
    IrpSp->MinorFunction = IRP_MN_QUERY_INTERFACE;
    IrpSp->Parameters.QueryInterface.InterfaceType = &GUID_BUS_INTERFACE_STANDARD;
    IrpSp->Parameters.QueryInterface.Size = sizeof(BUS_INTERFACE_STANDARD);
    IrpSp->Parameters.QueryInterface.Version = 1;
    IrpSp->Parameters.QueryInterface.Interface = (PINTERFACE)&Adapter->BusInterface;
    IrpSp->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(TargetDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    ObDereferenceObject(TargetDevice);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("E1000: Bus interface query failed: 0x%x\n", Status);
        return NDIS_STATUS_FAILURE;
    }

    Adapter->BusInterfaceValid = TRUE;
    DPRINT1("E1000: Successfully obtained BUS_INTERFACE_STANDARD from PDO\n");

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000ReadPciConfig - Read PCI configuration space using bus interface
 *
 * This function uses the BUS_INTERFACE_STANDARD to read PCI config space
 * from the correct device (not bus 0, slot 0).
 * ============================================================================ */

static ULONG
E1000ReadPciConfig(
    _In_ PE1000_ADAPTER Adapter,
    _In_ ULONG Offset,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length
    )
{
    if (Adapter->BusInterfaceValid && Adapter->BusInterface.GetBusData != NULL)
    {
        return Adapter->BusInterface.GetBusData(
                    Adapter->BusInterface.Context,
                    PCI_WHICHSPACE_CONFIG,
                    Buffer,
                    Offset,
                    Length);
    }

    /* Fallback: use HAL with bus 0, slot 0 (this is wrong but better than nothing) */
    DPRINT1("E1000: Warning - Using fallback PCI config read (may read wrong device)\n");
    return HalGetBusDataByOffset(PCIConfiguration, 0, 0, Buffer, Offset, Length);
}


/* ============================================================================
 * E1000MapHardwareResources - Map PCI resources for the adapter
 * ============================================================================ */

static NDIS_STATUS
E1000MapHardwareResources(
    _In_ PE1000_ADAPTER Adapter,
    _In_ PNDIS_RESOURCE_LIST ResourceList
    )
{
    ULONG i;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource;
    BOOLEAN FoundMemory = FALSE;
    BOOLEAN FoundInterrupt = FALSE;

    DPRINT1("E1000: Parsing %u resources\n", ResourceList->Count);

    for (i = 0; i < ResourceList->Count; i++)
    {
        Resource = &ResourceList->PartialDescriptors[i];

        switch (Resource->Type)
        {
            case CmResourceTypeMemory:
                /*
                 * E1000 has multiple memory BARs:
                 *   BAR0: 128KB - Main registers
                 *   BAR1: 128KB - Flash (82574L only)
                 *   BAR2: 16KB - MSI-X tables
                 */
                if (Resource->u.Memory.Length == (128 * 1024) && !FoundMemory)
                {
                    Adapter->IoAddress = Resource->u.Memory.Start;
                    Adapter->IoLength = Resource->u.Memory.Length;

                    /* Map the memory region */
                    Adapter->IoBase = MmMapIoSpace(
                                        Adapter->IoAddress,
                                        Adapter->IoLength,
                                        MmNonCached
                                        );

                    if (Adapter->IoBase == NULL)
                    {
                        DPRINT1("E1000: Failed to map I/O memory\n");
                        return NDIS_STATUS_RESOURCES;
                    }

                    FoundMemory = TRUE;
                    DPRINT1("E1000: Mapped memory BAR0 at %p (PA: 0x%I64x, Len: %u)\n",
                             Adapter->IoBase, Adapter->IoAddress.QuadPart, Adapter->IoLength);
                }
                else if (Resource->u.Memory.Length == (16 * 1024))
                {
                    /* MSI-X BAR */
                    Adapter->MsixAddress = Resource->u.Memory.Start;
                    Adapter->MsixLength = Resource->u.Memory.Length;
                    DPRINT("E1000: Found MSI-X BAR at 0x%I64x (Len: %u)\n",
                             Adapter->MsixAddress.QuadPart, Adapter->MsixLength);
                }
                break;

            case CmResourceTypePort:
                Adapter->IoPortAddress = Resource->u.Port.Start.LowPart;
                Adapter->IoPortLength = Resource->u.Port.Length;
                DPRINT1("E1000: Found I/O port at 0x%x (Len: %u)\n",
                         Adapter->IoPortAddress, Adapter->IoPortLength);
                break;

            case CmResourceTypeInterrupt:
                /*
                 * Check if this is an MSI/MSI-X interrupt resource.
                 * CM_RESOURCE_INTERRUPT_MESSAGE (0x20) indicates MSI/MSI-X.
                 * The resource format differs between legacy and MSI:
                 *
                 * Legacy: u.Interrupt.Level, u.Interrupt.Vector, u.Interrupt.Affinity
                 * MSI:    u.MessageInterrupt.Translated.Level/Vector/Affinity
                 *         u.MessageInterrupt.Raw.MessageCount indicates number of MSI vectors
                 */
                DPRINT("E1000: Interrupt resource: Type=%d, Flags=0x%04x, ShareDisp=%d\n",
                         Resource->Type, Resource->Flags, Resource->ShareDisposition);

                if (Resource->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
                {
                    /* MSI or MSI-X interrupt */
                    Adapter->InterruptVector = Resource->u.MessageInterrupt.Translated.Vector;
                    Adapter->InterruptLevel = Resource->u.MessageInterrupt.Translated.Level;
                    Adapter->InterruptAffinity = Resource->u.MessageInterrupt.Translated.Affinity;
                    Adapter->InterruptModeType = Latched;  /* MSI is always edge-triggered */
                    Adapter->InterruptShared = FALSE;      /* MSI is typically exclusive */
                    Adapter->HasMessageInterrupt = TRUE;

                    /* Check if we have multiple MSI vectors (MSI-X) */
                    /* Note: MessageCount in Raw structure indicates allocated vectors */

                    DPRINT("E1000: MESSAGE interrupt detected!\n");
                    DPRINT1("E1000:   Translated: Vector=%u, Level=%u, Affinity=0x%Ix\n",
                             Resource->u.MessageInterrupt.Translated.Vector,
                             Resource->u.MessageInterrupt.Translated.Level,
                             (ULONG_PTR)Resource->u.MessageInterrupt.Translated.Affinity);
                    DPRINT1("E1000:   Raw: Reserved=%u, MessageCount=%u, Vector=%u, Affinity=0x%Ix\n",
                             Resource->u.MessageInterrupt.Raw.Reserved,
                             Resource->u.MessageInterrupt.Raw.MessageCount,
                             Resource->u.MessageInterrupt.Raw.Vector,
                             (ULONG_PTR)Resource->u.MessageInterrupt.Raw.Affinity);
                }
                else
                {
                    /* Legacy line-based interrupt */
                    Adapter->InterruptVector = Resource->u.Interrupt.Vector;
                    Adapter->InterruptLevel = Resource->u.Interrupt.Level;
                    Adapter->InterruptAffinity = Resource->u.Interrupt.Affinity;
                    Adapter->InterruptModeType = (Resource->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ?
                                                 Latched : LevelSensitive;
                    Adapter->InterruptShared = (Resource->ShareDisposition == CmResourceShareShared);
                    Adapter->HasMessageInterrupt = FALSE;

                    DPRINT("E1000: Legacy interrupt detected\n");
                    DPRINT1("E1000:   Vector=%u, Level=%u, Affinity=0x%Ix, Shared=%d\n",
                             Adapter->InterruptVector, Adapter->InterruptLevel,
                             (ULONG_PTR)Adapter->InterruptAffinity, Adapter->InterruptShared);
                }

                FoundInterrupt = TRUE;
                DPRINT("E1000: Found interrupt: Vector=%u, Level=%u, Shared=%d, MessageInt=%d\n",
                         Adapter->InterruptVector, Adapter->InterruptLevel,
                         Adapter->InterruptShared, Adapter->HasMessageInterrupt);
                break;

            default:
                DPRINT1("E1000: Unknown resource type %d\n", Resource->Type);
                break;
        }
    }

    if (!FoundMemory || !FoundInterrupt)
    {
        DPRINT("E1000: Missing required resources (Memory=%d, Interrupt=%d)\n",
                 FoundMemory, FoundInterrupt);
        return NDIS_STATUS_RESOURCES;
    }

    /*
     * Read PCI configuration using BUS_INTERFACE_STANDARD obtained from PDO.
     * This ensures we read from the correct PCI device, not bus 0 slot 0.
     */
    {
        PCI_COMMON_CONFIG PciConfig;
        ULONG BytesRead;

        /* Query the bus interface from PDO - this must be done first */
        if (E1000QueryBusInterface(Adapter) != NDIS_STATUS_SUCCESS)
        {
            DPRINT1("E1000: Warning - Failed to query bus interface, PCI config reads may fail\n");
        }

        BytesRead = E1000ReadPciConfig(Adapter, 0, &PciConfig, sizeof(PCI_COMMON_CONFIG));

        if (BytesRead >= PCI_COMMON_HDR_LENGTH)
        {
            Adapter->VendorId = PciConfig.VendorID;
            Adapter->DeviceId = PciConfig.DeviceID;
            Adapter->SubsystemVendorId = PciConfig.u.type0.SubVendorID;
            Adapter->SubsystemId = PciConfig.u.type0.SubSystemID;
            Adapter->RevisionId = PciConfig.RevisionID;

            DPRINT1("E1000: ========== PCI CONFIGURATION ==========\n");
            DPRINT1("E1000: Vendor=0x%04x Device=0x%04x SubVendor=0x%04x SubDevice=0x%04x\n",
                     Adapter->VendorId, Adapter->DeviceId,
                     Adapter->SubsystemVendorId, Adapter->SubsystemId);
            DPRINT1("E1000: Revision=%u Command=0x%04x Status=0x%04x\n",
                     Adapter->RevisionId, PciConfig.Command, PciConfig.Status);

            /* Parse PCI capability list to find and log MSI/MSI-X capabilities */
            if (PciConfig.Status & PCI_STATUS_CAPABILITIES_LIST)
            {
                UCHAR CapPtr = PciConfig.u.type0.CapabilitiesPtr & ~3; /* Align to DWORD */
                ULONG CapReadCount = 0;
                const ULONG MaxCaps = 48;

                DPRINT1("E1000: PCI Capability List:\n");

                while (CapPtr != 0 && CapReadCount < MaxCaps)
                {
                    UCHAR CapId, NextPtr;
                    UCHAR CapData[4];

                    E1000ReadPciConfig(Adapter, CapPtr, CapData, sizeof(CapData));
                    CapId = CapData[0];
                    NextPtr = CapData[1];

                    DPRINT1("E1000:   [0x%02x] Cap ID=0x%02x (%s)\n",
                             CapPtr, CapId,
                             (CapId == 0x05) ? "MSI" :
                             (CapId == 0x10) ? "PCIe" :
                             (CapId == 0x11) ? "MSI-X" :
                             (CapId == 0x01) ? "PMC" : "Other");

                    if (CapId == 0x05) /* MSI */
                    {
                        USHORT MsiControl;
                        ULONG MsiAddrLo;
                        USHORT MsiData;
                        UCHAR MsiMaxVectors, MsiEnableVectors;
                        BOOLEAN Is64Bit;

                        E1000ReadPciConfig(Adapter, CapPtr + 2, &MsiControl, sizeof(MsiControl));

                        MsiMaxVectors = 1 << ((MsiControl >> 1) & 0x7);
                        MsiEnableVectors = 1 << ((MsiControl >> 4) & 0x7);
                        Is64Bit = (MsiControl & 0x80) != 0;

                        DPRINT("E1000:       MSI Control=0x%04x Enable=%d MaxVectors=%u EnabledVectors=%u 64Bit=%d\n",
                                 MsiControl, (MsiControl & 0x01) ? 1 : 0,
                                 MsiMaxVectors, MsiEnableVectors, Is64Bit);

                        E1000ReadPciConfig(Adapter, CapPtr + 4, &MsiAddrLo, sizeof(MsiAddrLo));
                        DPRINT1("E1000:       Message Address Low=0x%08x\n", MsiAddrLo);

                        if (Is64Bit)
                        {
                            E1000ReadPciConfig(Adapter, CapPtr + 12, &MsiData, sizeof(MsiData));
                        }
                        else
                        {
                            E1000ReadPciConfig(Adapter, CapPtr + 8, &MsiData, sizeof(MsiData));
                        }
                        DPRINT1("E1000:       Message Data=0x%04x (vector %u)\n", MsiData, MsiData & 0xFF);
                    }
                    else if (CapId == 0x11) /* MSI-X */
                    {
                        USHORT MsixControl;
                        ULONG MsixTableOffset, MsixPbaOffset;
                        ULONG TableBir, PbaBir;
                        USHORT TableSize;

                        E1000ReadPciConfig(Adapter, CapPtr + 2, &MsixControl, sizeof(MsixControl));
                        E1000ReadPciConfig(Adapter, CapPtr + 4, &MsixTableOffset, sizeof(MsixTableOffset));
                        E1000ReadPciConfig(Adapter, CapPtr + 8, &MsixPbaOffset, sizeof(MsixPbaOffset));

                        TableBir = MsixTableOffset & 0x7;
                        MsixTableOffset &= ~0x7;
                        PbaBir = MsixPbaOffset & 0x7;
                        MsixPbaOffset &= ~0x7;
                        TableSize = (MsixControl & 0x7FF) + 1;

                        DPRINT("E1000:       MSI-X Control=0x%04x Enable=%d FunctionMask=%d TableSize=%u\n",
                                 MsixControl,
                                 (MsixControl & 0x8000) ? 1 : 0,
                                 (MsixControl & 0x4000) ? 1 : 0,
                                 TableSize);
                        DPRINT1("E1000:       Table: BAR%u Offset=0x%x  PBA: BAR%u Offset=0x%x\n",
                                 TableBir, MsixTableOffset, PbaBir, MsixPbaOffset);
                    }

                    CapPtr = NextPtr;
                    CapReadCount++;
                }
            }
            else
            {
                DPRINT1("E1000: No PCI capabilities (Status=0x%04x)\n", PciConfig.Status);
            }
            DPRINT1("E1000: ==========================================\n");
        }
        else
        {
            DPRINT1("E1000: Warning - Could not read PCI config space (read %u bytes)\n", BytesRead);
            /* Set default vendor ID for Intel */
            Adapter->VendorId = 0x8086;
        }
    }

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000AllocateAdapterResources - Allocate DMA resources for descriptor rings
 * ============================================================================ */

NDIS_STATUS
E1000AllocateAdapterResources(
    _In_ PE1000_ADAPTER Adapter
    )
{
    NDIS_STATUS Status;
    NDIS_SG_DMA_DESCRIPTION SgDmaDesc;
    NET_BUFFER_LIST_POOL_PARAMETERS NblPoolParams;

    /* Setup scatter-gather DMA */
    NdisZeroMemory(&SgDmaDesc, sizeof(SgDmaDesc));
    SgDmaDesc.Header.Type = NDIS_OBJECT_TYPE_SG_DMA_DESCRIPTION;
    SgDmaDesc.Header.Revision = NDIS_SG_DMA_DESCRIPTION_REVISION_1;
    SgDmaDesc.Header.Size = sizeof(NDIS_SG_DMA_DESCRIPTION);
    SgDmaDesc.Flags = NDIS_SG_DMA_64_BIT_ADDRESS;  /* E1000 supports 64-bit DMA */
    SgDmaDesc.MaximumPhysicalMapping = E1000_MAX_FRAME_SIZE;
    SgDmaDesc.ProcessSGListHandler = NULL;  /* Will process inline */
    SgDmaDesc.SharedMemAllocateCompleteHandler = NULL;

    Status = NdisMRegisterScatterGatherDma(
                Adapter->MiniportAdapterHandle,
                &SgDmaDesc,
                &Adapter->DmaAdapterHandle
                );

    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("E1000: Failed to register scatter-gather DMA: 0x%08x\n", Status);
        return Status;
    }

    /* Allocate NBL pool for receives */
    NdisZeroMemory(&NblPoolParams, sizeof(NblPoolParams));
    NblPoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    NblPoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
    NblPoolParams.Header.Size = sizeof(NET_BUFFER_LIST_POOL_PARAMETERS);
    NblPoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
    NblPoolParams.fAllocateNetBuffer = TRUE;
    NblPoolParams.ContextSize = 0;
    NblPoolParams.PoolTag = 'E1kR';
    /* Receive NBLs wrap the DMA ring buffer through an explicit MDL. */
    NblPoolParams.DataSize = 0;

    Adapter->RxNblPool = NdisAllocateNetBufferListPool(
                            Adapter->MiniportAdapterHandle,
                            &NblPoolParams
                            );

    if (Adapter->RxNblPool == NULL)
    {
        DPRINT1("E1000: Failed to allocate RX NBL pool\n");
        NdisMDeregisterScatterGatherDma(Adapter->DmaAdapterHandle);
        return NDIS_STATUS_RESOURCES;
    }

    DPRINT1("E1000: Allocated adapter resources - DMA handle=%p, NBL pool=%p\n",
             Adapter->DmaAdapterHandle, Adapter->RxNblPool);

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000FreeAdapterResources - Free DMA and NBL pool resources
 * ============================================================================ */

VOID
E1000FreeAdapterResources(
    _In_ PE1000_ADAPTER Adapter
    )
{
    /* Free NBL pool */
    if (Adapter->RxNblPool != NULL)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
        Adapter->RxNblPool = NULL;
    }

    /* Deregister scatter-gather DMA */
    if (Adapter->DmaAdapterHandle != NULL)
    {
        NdisMDeregisterScatterGatherDma(Adapter->DmaAdapterHandle);
        Adapter->DmaAdapterHandle = NULL;
    }

    /* Unmap I/O memory */
    if (Adapter->IoBase != NULL)
    {
        MmUnmapIoSpace(Adapter->IoBase, Adapter->IoLength);
        Adapter->IoBase = NULL;
    }

    /* Unmap MSI-X table */
    if (Adapter->MsixTableBase != NULL)
    {
        MmUnmapIoSpace(Adapter->MsixTableBase, Adapter->MsixLength);
        Adapter->MsixTableBase = NULL;
    }

    DPRINT1("E1000: Freed adapter resources\n");
}


/* ============================================================================
 * E1000ReadMacAddress - Read MAC address from EEPROM
 * ============================================================================ */

NDIS_STATUS
E1000ReadMacAddress(
    _In_ PE1000_ADAPTER Adapter
    )
{
    ULONG RalLow, RalHigh;

    /*
     * Read MAC address from RAL/RAH registers.
     * The hardware loads these from EEPROM during reset.
     */
    RalLow = E1000_READ_REG(Adapter, E1000_REG_RAL0);
    RalHigh = E1000_READ_REG(Adapter, E1000_REG_RAH0);

    Adapter->PermanentMacAddress[0] = (UCHAR)(RalLow & 0xFF);
    Adapter->PermanentMacAddress[1] = (UCHAR)((RalLow >> 8) & 0xFF);
    Adapter->PermanentMacAddress[2] = (UCHAR)((RalLow >> 16) & 0xFF);
    Adapter->PermanentMacAddress[3] = (UCHAR)((RalLow >> 24) & 0xFF);
    Adapter->PermanentMacAddress[4] = (UCHAR)(RalHigh & 0xFF);
    Adapter->PermanentMacAddress[5] = (UCHAR)((RalHigh >> 8) & 0xFF);

    /* Validate MAC address */
    if ((RalLow == 0 && RalHigh == 0) ||
        (RalLow == 0xFFFFFFFF && RalHigh == 0xFFFFFFFF))
    {
        DPRINT1("E1000: Invalid MAC address in RAL/RAH registers\n");
        return NDIS_STATUS_FAILURE;
    }

    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * E1000InitializeHardware - Initialize the E1000 hardware
 * ============================================================================ */

NDIS_STATUS
E1000InitializeHardware(
    _In_ PE1000_ADAPTER Adapter
    )
{
    /* Reset the hardware */
    return E1000ResetHardware(Adapter);
}
