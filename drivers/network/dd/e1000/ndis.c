/*
 * PROJECT:     ReactOS Intel PRO/1000 Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Driver entrypoint
 * COPYRIGHT:   Copyright 2013 Cameron Gutman (cameron.gutman@reactos.org)
 *              Copyright 2018 Mark Jansen (mark.jansen@reactos.org)
 *              Copyright 2024 ReactOS Team - Enhanced debug logging
 */

#include "nic.h"

#include <debug.h>


/* ============================================================================
 * Miniport Reset Handler
 * ============================================================================ */

NDIS_STATUS
NTAPI
MiniportReset(
    OUT PBOOLEAN AddressingReset,
    IN NDIS_HANDLE MiniportAdapterContext)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS Status;

    E1000_INIT_DBG(("MiniportReset: entering\n"));

    *AddressingReset = FALSE;

    E1000_STAT_INC32(ResetCount);

    /* Disable TX/RX first */
    E1000_INIT_DBG(("MiniportReset: disabling TX/RX\n"));
    NICDisableTxRx(Adapter);

    /* Perform soft reset */
    E1000_INIT_DBG(("MiniportReset: performing soft reset\n"));
    Status = NICSoftReset(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("NICSoftReset failed (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportReset: soft reset FAILED 0x%x\n", Status));
        return Status;
    }

    /* Re-enable TX/RX */
    E1000_INIT_DBG(("MiniportReset: re-enabling TX/RX\n"));
    Status = NICEnableTxRx(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("NICEnableTxRx failed (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportReset: enable TX/RX FAILED 0x%x\n", Status));
        return Status;
    }

    /* Re-apply interrupt mask */
    E1000_INIT_DBG(("MiniportReset: applying interrupt mask 0x%08x\n", Adapter->InterruptMask));
    NICApplyInterruptMask(Adapter);

    E1000_INIT_DBG(("MiniportReset: complete SUCCESS\n"));
    return NDIS_STATUS_SUCCESS;
}


/* ============================================================================
 * Miniport Halt Handler
 * ============================================================================ */

VOID
NTAPI
MiniportHalt(
    IN NDIS_HANDLE MiniportAdapterContext)
{
    PE1000_ADAPTER Adapter = (PE1000_ADAPTER)MiniportAdapterContext;

    NDIS_DbgPrint(MID_TRACE, ("MiniportHalt called\n"));
    E1000_INIT_DBG(("MiniportHalt: entering\n"));

    ASSERT(Adapter != NULL);
    if (Adapter == NULL)
    {
        NDIS_DbgPrint(MIN_TRACE, ("MiniportHalt: NULL adapter context!\n"));
        return;
    }

#if DBG
    /* Dump final statistics before shutdown */
    E1000_DumpStatistics();
    E1000_DumpDriverState(Adapter);
#endif

    /* First disable sending / receiving */
    E1000_INIT_DBG(("MiniportHalt: disabling TX/RX\n"));
    NICDisableTxRx(Adapter);

    /* Then unregister interrupts */
    E1000_INIT_DBG(("MiniportHalt: unregistering interrupts\n"));
    NICUnregisterInterrupts(Adapter);

    /* Free spin locks */
    E1000_INIT_DBG(("MiniportHalt: freeing spin locks\n"));
    NdisFreeSpinLock(&Adapter->TxLock);
    NdisFreeSpinLock(&Adapter->RxLock);

    /* Finally, free other resources (Ports, IO ranges,...) */
    E1000_INIT_DBG(("MiniportHalt: releasing IO resources\n"));
    NICReleaseIoResources(Adapter);

    /* Destroy the adapter context */
    E1000_INIT_DBG(("MiniportHalt: freeing adapter context %p\n", Adapter));
    NdisFreeMemory(Adapter, sizeof(*Adapter), 0);

    E1000_INIT_DBG(("MiniportHalt: complete\n"));
}


/* ============================================================================
 * Miniport Initialize Handler
 * ============================================================================ */

NDIS_STATUS
NTAPI
MiniportInitialize(
    OUT PNDIS_STATUS OpenErrorStatus,
    OUT PUINT SelectedMediumIndex,
    IN PNDIS_MEDIUM MediumArray,
    IN UINT MediumArraySize,
    IN NDIS_HANDLE MiniportAdapterHandle,
    IN NDIS_HANDLE WrapperConfigurationContext)
{
    PE1000_ADAPTER Adapter;
    NDIS_STATUS Status;
    UINT i;
    PNDIS_RESOURCE_LIST ResourceList;
    UINT ResourceListSize;
    PCI_COMMON_CONFIG PciConfig;

    NDIS_DbgPrint(MID_TRACE, ("MiniportInitialize called\n"));
    E1000_INIT_DBG(("MiniportInitialize: ========== DRIVER INITIALIZATION START ==========\n"));
    E1000_INIT_DBG(("MiniportInitialize: AdapterHandle=%p\n", MiniportAdapterHandle));

    E1000_STAT_INC32(InitAttempts);

    /* Initialize debug subsystem */
    E1000_InitDebug();

    /* Make sure the medium is supported */
    E1000_INIT_DBG(("MiniportInitialize: checking for 802.3 medium support\n"));
    for (i = 0; i < MediumArraySize; i++)
    {
        if (MediumArray[i] == NdisMedium802_3)
        {
            *SelectedMediumIndex = i;
            E1000_INIT_DBG(("MiniportInitialize: 802.3 medium found at index %u\n", i));
            break;
        }
    }

    if (i == MediumArraySize)
    {
        NDIS_DbgPrint(MIN_TRACE, ("802.3 medium was not found in the medium array\n"));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - 802.3 medium not found\n"));
        E1000_STAT_INC32(InitFailed);
        return NDIS_STATUS_UNSUPPORTED_MEDIA;
    }

    ResourceList = NULL;
    ResourceListSize = 0;

    /* Allocate our adapter context */
    E1000_INIT_DBG(("MiniportInitialize: allocating adapter context (%u bytes)\n", (ULONG)sizeof(E1000_ADAPTER)));
    Status = NdisAllocateMemoryWithTag((PVOID*)&Adapter,
                                       sizeof(*Adapter),
                                       E1000_TAG);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Failed to allocate adapter context (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - could not allocate adapter context 0x%x\n", Status));
        E1000_STAT_INC32(InitFailed);
        return NDIS_STATUS_RESOURCES;
    }

    E1000_INIT_DBG(("MiniportInitialize: adapter context allocated at %p\n", Adapter));

    RtlZeroMemory(Adapter, sizeof(*Adapter));
    Adapter->AdapterHandle = MiniportAdapterHandle;

    /* Notify NDIS of some characteristics of our NIC */
    E1000_INIT_DBG(("MiniportInitialize: setting NDIS attributes (bus master, PCI)\n"));
    NdisMSetAttributesEx(MiniportAdapterHandle,
                         Adapter,
                         0,
                         NDIS_ATTRIBUTE_BUS_MASTER,
                         NdisInterfacePci);

    /* Read PCI configuration */
    E1000_INIT_DBG(("MiniportInitialize: reading PCI configuration\n"));
    NdisReadPciSlotInformation(Adapter->AdapterHandle,
                               0,
                               FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID),
                               &PciConfig, sizeof(PciConfig));

    Adapter->VendorID = PciConfig.VendorID;
    Adapter->DeviceID = PciConfig.DeviceID;
    Adapter->SubsystemID = PciConfig.u.type0.SubSystemID;
    Adapter->SubsystemVendorID = PciConfig.u.type0.SubVendorID;

    /* Save the legacy interrupt line from PCI config space.
     * This is the IRQ assigned by BIOS/UEFI for legacy interrupt mode.
     * We'll use this as a fallback if MSI resources are provided but we
     * can't use them (e.g., ReactOS doesn't fully support MSI yet).
     */
    Adapter->PciInterruptLine = PciConfig.u.type0.InterruptLine;

    E1000_INIT_DBG(("MiniportInitialize: PCI IDs - Vendor=%04x Device=%04x Subsys=%04x SubVendor=%04x\n",
                    Adapter->VendorID, Adapter->DeviceID,
                    Adapter->SubsystemID, Adapter->SubsystemVendorID));
    E1000_INIT_DBG(("MiniportInitialize: PCI InterruptLine (legacy IRQ) = %u\n", Adapter->PciInterruptLine));

    /* Log PCI command/status registers for debugging */
    E1000_HW_DBG(("PCI Command=0x%04x Status=0x%04x RevID=0x%02x\n",
                  PciConfig.Command, PciConfig.Status, PciConfig.RevisionID));

    /*
     * Parse PCI capability list to find and log MSI/MSI-X capabilities.
     * This helps us understand what the device supports and what's enabled.
     */
    if (PciConfig.Status & PCI_STATUS_CAPABILITIES_LIST)
    {
        UCHAR CapPtr = PciConfig.u.type0.CapabilitiesPtr & ~3; /* Align to DWORD */
        ULONG CapReadCount = 0;
        const ULONG MaxCaps = 48; /* Prevent infinite loops */

        DbgPrint("E1000: PCI Capability List:\n");

        while (CapPtr != 0 && CapReadCount < MaxCaps)
        {
            UCHAR CapId, NextPtr;
            UCHAR CapData[4];

            NdisReadPciSlotInformation(Adapter->AdapterHandle,
                                       0,
                                       CapPtr,
                                       CapData, sizeof(CapData));
            CapId = CapData[0];
            NextPtr = CapData[1];

            DbgPrint("E1000:   [0x%02x] Cap ID=0x%02x (%s)\n",
                     CapPtr, CapId,
                     (CapId == 0x05) ? "MSI" :
                     (CapId == 0x10) ? "PCIe" :
                     (CapId == 0x11) ? "MSI-X" :
                     (CapId == 0x01) ? "PMC" : "Other");

            if (CapId == 0x05) /* MSI */
            {
                USHORT MsiControl;
                ULONG MsiAddrLo, MsiAddrHi = 0;
                USHORT MsiData;
                UCHAR MsiMaxVectors;
                UCHAR MsiEnableVectors;
                BOOLEAN Is64Bit;

                NdisReadPciSlotInformation(Adapter->AdapterHandle, 0,
                                           CapPtr + 2, &MsiControl, sizeof(MsiControl));

                MsiMaxVectors = 1 << ((MsiControl >> 1) & 0x7); /* Bits 3:1 = MMC */
                MsiEnableVectors = 1 << ((MsiControl >> 4) & 0x7); /* Bits 6:4 = MME */
                Is64Bit = (MsiControl & 0x80) != 0;

                DbgPrint("E1000:       MSI Control=0x%04x\n", MsiControl);
                DbgPrint("E1000:       Enable=%d MaxVectors=%u EnabledVectors=%u 64Bit=%d\n",
                         (MsiControl & 0x01) ? 1 : 0, MsiMaxVectors, MsiEnableVectors, Is64Bit);

                NdisReadPciSlotInformation(Adapter->AdapterHandle, 0,
                                           CapPtr + 4, &MsiAddrLo, sizeof(MsiAddrLo));
                DbgPrint("E1000:       Message Address Low=0x%08x\n", MsiAddrLo);

                if (Is64Bit)
                {
                    NdisReadPciSlotInformation(Adapter->AdapterHandle, 0,
                                               CapPtr + 8, &MsiAddrHi, sizeof(MsiAddrHi));
                    NdisReadPciSlotInformation(Adapter->AdapterHandle, 0,
                                               CapPtr + 12, &MsiData, sizeof(MsiData));
                    DbgPrint("E1000:       Message Address High=0x%08x\n", MsiAddrHi);
                }
                else
                {
                    NdisReadPciSlotInformation(Adapter->AdapterHandle, 0,
                                               CapPtr + 8, &MsiData, sizeof(MsiData));
                }
                DbgPrint("E1000:       Message Data=0x%04x (vector %u)\n", MsiData, MsiData & 0xFF);
            }
            else if (CapId == 0x11) /* MSI-X */
            {
                USHORT MsixControl;
                ULONG MsixTableOffset, MsixPbaOffset;
                ULONG TableBir, PbaBir;
                USHORT TableSize;

                NdisReadPciSlotInformation(Adapter->AdapterHandle, 0,
                                           CapPtr + 2, &MsixControl, sizeof(MsixControl));
                NdisReadPciSlotInformation(Adapter->AdapterHandle, 0,
                                           CapPtr + 4, &MsixTableOffset, sizeof(MsixTableOffset));
                NdisReadPciSlotInformation(Adapter->AdapterHandle, 0,
                                           CapPtr + 8, &MsixPbaOffset, sizeof(MsixPbaOffset));

                TableBir = MsixTableOffset & 0x7;
                MsixTableOffset &= ~0x7;
                PbaBir = MsixPbaOffset & 0x7;
                MsixPbaOffset &= ~0x7;
                TableSize = (MsixControl & 0x7FF) + 1;

                DbgPrint("E1000:       MSI-X Control=0x%04x\n", MsixControl);
                DbgPrint("E1000:       Enable=%d FunctionMask=%d TableSize=%u\n",
                         (MsixControl & 0x8000) ? 1 : 0,
                         (MsixControl & 0x4000) ? 1 : 0,
                         TableSize);
                DbgPrint("E1000:       Table: BAR%u Offset=0x%x\n", TableBir, MsixTableOffset);
                DbgPrint("E1000:       PBA:   BAR%u Offset=0x%x\n", PbaBir, MsixPbaOffset);
            }

            CapPtr = NextPtr;
            CapReadCount++;
        }
    }
    else
    {
        DbgPrint("E1000: No PCI capabilities (Status=0x%04x)\n", PciConfig.Status);
    }

    /* Recognize hardware */
    E1000_INIT_DBG(("MiniportInitialize: recognizing hardware\n"));
    if (!NICRecognizeHardware(Adapter))
    {
        NDIS_DbgPrint(MIN_TRACE, ("Hardware not recognized\n"));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - hardware not recognized\n"));
        Status = NDIS_STATUS_UNSUPPORTED_MEDIA;
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }

    E1000_INIT_DBG(("MiniportInitialize: hardware recognized - PCIe=%s\n",
                    Adapter->IsPCIe ? "Yes" : "No"));

    /* Get our resources for IRQ and IO base information */
    E1000_INIT_DBG(("MiniportInitialize: querying adapter resources\n"));
    NdisMQueryAdapterResources(&Status,
                               WrapperConfigurationContext,
                               ResourceList,
                               &ResourceListSize);
    if (Status != NDIS_STATUS_RESOURCES)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unexpected failure of NdisMQueryAdapterResources (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - NdisMQueryAdapterResources unexpected result 0x%x\n", Status));
        Status = NDIS_STATUS_FAILURE;
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }

    E1000_INIT_DBG(("MiniportInitialize: resource list size=%u bytes\n", ResourceListSize));

    Status = NdisAllocateMemoryWithTag((PVOID*)&ResourceList,
                                ResourceListSize,
                                E1000_TAG);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Failed to allocate resource list (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - could not allocate resource list 0x%x\n", Status));
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }

    NdisMQueryAdapterResources(&Status,
                               WrapperConfigurationContext,
                               ResourceList,
                               &ResourceListSize);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unexpected failure of NdisMQueryAdapterResources (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - NdisMQueryAdapterResources second call 0x%x\n", Status));
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }

    ASSERT(ResourceList->Version == 1);
    ASSERT(ResourceList->Revision == 1);

    E1000_INIT_DBG(("MiniportInitialize: resource list: version=%u revision=%u count=%u\n",
                    ResourceList->Version, ResourceList->Revision, ResourceList->Count));

    /* Initialize adapter resources */
    E1000_INIT_DBG(("MiniportInitialize: initializing adapter resources\n"));
    Status = NICInitializeAdapterResources(Adapter, ResourceList);

    NdisFreeMemory(ResourceList, ResourceListSize, 0);
    ResourceList = NULL;

    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Adapter didn't receive enough resources\n"));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - adapter resources initialization 0x%x\n", Status));
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }

    E1000_INIT_DBG(("MiniportInitialize: resources - IoAddr=0x%I64x IoLen=%u IoPort=%u PortLen=%u\n",
                    Adapter->IoAddress.QuadPart, Adapter->IoLength,
                    Adapter->IoPortAddress, Adapter->IoPortLength));
    E1000_INIT_DBG(("MiniportInitialize: interrupt - Vector=%u Level=%u Shared=%s\n",
                    Adapter->InterruptVector, Adapter->InterruptLevel,
                    Adapter->InterruptShared ? "Yes" : "No"));

    /* Allocate the DMA resources */
    E1000_INIT_DBG(("MiniportInitialize: initializing scatter-gather DMA (MaxFrameSize=%u)\n", MAXIMUM_FRAME_SIZE));
    Status = NdisMInitializeScatterGatherDma(MiniportAdapterHandle,
                                             TRUE,
                                             MAXIMUM_FRAME_SIZE);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to configure DMA\n"));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - DMA initialization 0x%x\n", Status));
        Status = NDIS_STATUS_RESOURCES;
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }
    E1000_INIT_DBG(("MiniportInitialize: DMA initialized successfully\n"));

    /* Allocate IO resources */
    E1000_INIT_DBG(("MiniportInitialize: allocating IO resources\n"));
    Status = NICAllocateIoResources(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to allocate resources\n"));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - IO resource allocation 0x%x\n", Status));
        Status = NDIS_STATUS_RESOURCES;
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }

    E1000_INIT_DBG(("MiniportInitialize: IO mapped - Base=%p TxDesc=%p RxDesc=%p RxBuf=%p\n",
                    Adapter->IoBase, Adapter->TransmitDescriptors,
                    Adapter->ReceiveDescriptors, Adapter->ReceiveBuffer));

    /* Initialize spin locks for TX/RX synchronization */
    E1000_INIT_DBG(("MiniportInitialize: allocating spin locks\n"));
    NdisAllocateSpinLock(&Adapter->TxLock);
    NdisAllocateSpinLock(&Adapter->RxLock);

    /* Power on adapter */
    E1000_INIT_DBG(("MiniportInitialize: powering on NIC\n"));
    E1000_POWER_DBG(("Powering on adapter\n"));
    Status = NICPowerOn(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to power on NIC (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - power on 0x%x\n", Status));
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }
    E1000_POWER_DBG(("Adapter powered on successfully\n"));

    /* Soft reset */
    E1000_INIT_DBG(("MiniportInitialize: performing soft reset\n"));
    Status = NICSoftReset(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to reset the NIC (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - soft reset 0x%x\n", Status));
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }
    E1000_INIT_DBG(("MiniportInitialize: soft reset complete\n"));

    /* Get MAC address */
    E1000_INIT_DBG(("MiniportInitialize: reading permanent MAC address\n"));
    Status = NICGetPermanentMacAddress(Adapter, Adapter->PermanentMacAddress);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to get the fixed MAC address (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - MAC address read 0x%x\n", Status));
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }

    E1000_INIT_DBG(("MiniportInitialize: MAC address=%02x:%02x:%02x:%02x:%02x:%02x\n",
                    Adapter->PermanentMacAddress[0], Adapter->PermanentMacAddress[1],
                    Adapter->PermanentMacAddress[2], Adapter->PermanentMacAddress[3],
                    Adapter->PermanentMacAddress[4], Adapter->PermanentMacAddress[5]));

    RtlCopyMemory(Adapter->MulticastList[0].MacAddress, Adapter->PermanentMacAddress, IEEE_802_ADDR_LENGTH);

    E1000_INIT_DBG(("MiniportInitialize: updating multicast list\n"));
    NICUpdateMulticastList(Adapter);

    /* Update link state and speed */
    E1000_INIT_DBG(("MiniportInitialize: checking link status\n"));
    NICUpdateLinkStatus(Adapter);
    E1000_LINK_DBG(("Initial link state: %s, speed=%u Mbps\n",
                    Adapter->MediaState == NdisMediaStateConnected ? "Connected" : "Disconnected",
                    Adapter->LinkSpeedMbps));

    /* Initialize and enable hardware checksum offload */
    E1000_INIT_DBG(("MiniportInitialize: initializing checksum offload\n"));
    NICInitializeChecksumOffload(Adapter);
    NICEnableChecksumOffload(Adapter, TRUE, TRUE);
    E1000_CSUM_DBG(("Checksum offload enabled: TX=%s RX=%s\n",
                    Adapter->ChecksumOffload.TxChecksumEnabled ? "Yes" : "No",
                    Adapter->ChecksumOffload.RxChecksumEnabled ? "Yes" : "No"));

    /* Register interrupts */
    E1000_INIT_DBG(("MiniportInitialize: registering interrupts\n"));
    Status = NICRegisterInterrupts(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to register interrupt (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - interrupt registration 0x%x\n", Status));
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }
    E1000_INT_DBG(("Interrupts registered: mode=%s\n",
                   Adapter->InterruptMode == E1000_INTERRUPT_MODE_LEGACY ? "Legacy" :
                   Adapter->InterruptMode == E1000_INTERRUPT_MODE_MSI ? "MSI" : "MSI-X"));

    /* Enable interrupts on the NIC */
    Adapter->InterruptMask = DEFAULT_INTERRUPT_MASK;
    E1000_INIT_DBG(("MiniportInitialize: enabling interrupts (mask=0x%08x)\n", Adapter->InterruptMask));
    NICApplyInterruptMask(Adapter);

    /* Turn on TX and RX now */
    E1000_INIT_DBG(("MiniportInitialize: enabling TX and RX\n"));
    Status = NICEnableTxRx(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to enable TX and RX (0x%x)\n", Status));
        E1000_INIT_DBG(("MiniportInitialize: FAILED - enable TX/RX 0x%x\n", Status));
        E1000_STAT_INC32(InitFailed);
        goto Cleanup;
    }

    E1000_STAT_INC32(InitSuccess);

    E1000_INIT_DBG(("MiniportInitialize: ========== INITIALIZATION COMPLETE ==========\n"));
    E1000_INIT_DBG(("MiniportInitialize: TX descriptors=%u RX descriptors=%u RX buffer size=%u\n",
                    NUM_TRANSMIT_DESCRIPTORS, NUM_RECEIVE_DESCRIPTORS, RECEIVE_BUFFER_SIZE));

#if DBG
    /* Dump initial driver state */
    E1000_DumpDriverState(Adapter);
#endif

    return NDIS_STATUS_SUCCESS;

Cleanup:
    E1000_INIT_DBG(("MiniportInitialize: CLEANUP - initialization failed\n"));

    if (ResourceList != NULL)
    {
        NdisFreeMemory(ResourceList, ResourceListSize, 0);
    }

    MiniportHalt(Adapter);

    return Status;
}


/* ============================================================================
 * Driver Entry Point
 * ============================================================================ */

NTSTATUS
NTAPI
DriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    NDIS_HANDLE WrapperHandle;
    NDIS_MINIPORT_CHARACTERISTICS Characteristics = { 0 };
    NDIS_STATUS Status;

    DbgPrint("E1000: DriverEntry - ReactOS Intel PRO/1000 Driver\n");
    DbgPrint("E1000: Version %u.%u, Build Date: " __DATE__ " " __TIME__ "\n",
             DRIVER_VERSION >> 8, DRIVER_VERSION & 0xFF);

    E1000_INIT_DBG(("DriverEntry: DriverObject=%p RegistryPath=%wZ\n", DriverObject, RegistryPath));

    Characteristics.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    Characteristics.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    Characteristics.CheckForHangHandler = NULL;
    Characteristics.DisableInterruptHandler = NULL;
    Characteristics.EnableInterruptHandler = NULL;
    Characteristics.HaltHandler = MiniportHalt;
    Characteristics.HandleInterruptHandler = MiniportHandleInterrupt;
    Characteristics.InitializeHandler = MiniportInitialize;
    Characteristics.ISRHandler = MiniportISR;
    Characteristics.QueryInformationHandler = MiniportQueryInformation;
    Characteristics.ReconfigureHandler = NULL;
    Characteristics.ResetHandler = MiniportReset;
    Characteristics.SendHandler = MiniportSend;
    Characteristics.SetInformationHandler = MiniportSetInformation;
    Characteristics.TransferDataHandler = NULL;
    Characteristics.ReturnPacketHandler = NULL;
    Characteristics.SendPacketsHandler = MiniportSendPackets;
    Characteristics.AllocateCompleteHandler = NULL;

    E1000_INIT_DBG(("DriverEntry: NDIS version %u.%u\n",
                    NDIS_MINIPORT_MAJOR_VERSION, NDIS_MINIPORT_MINOR_VERSION));

    NdisMInitializeWrapper(&WrapperHandle, DriverObject, RegistryPath, NULL);
    if (!WrapperHandle)
    {
        DbgPrint("E1000: NdisMInitializeWrapper failed\n");
        E1000_INIT_DBG(("DriverEntry: FAILED - NdisMInitializeWrapper returned NULL\n"));
        return NDIS_STATUS_FAILURE;
    }

    E1000_INIT_DBG(("DriverEntry: wrapper initialized, handle=%p\n", WrapperHandle));

    Status = NdisMRegisterMiniport(WrapperHandle, &Characteristics, sizeof(Characteristics));
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DbgPrint("E1000: NdisMRegisterMiniport failed (0x%x)\n", Status);
        E1000_INIT_DBG(("DriverEntry: FAILED - NdisMRegisterMiniport 0x%x\n", Status));
        NdisTerminateWrapper(WrapperHandle, 0);
        return NDIS_STATUS_FAILURE;
    }

    DbgPrint("E1000: Driver loaded successfully\n");
    E1000_INIT_DBG(("DriverEntry: miniport registered successfully\n"));

    return NDIS_STATUS_SUCCESS;
}
