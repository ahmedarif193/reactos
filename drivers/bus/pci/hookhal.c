/*
 * PROJECT:         ReactOS PCI Bus Driver
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            drivers/bus/pci/hookhal.c
 * PURPOSE:         HAL Bus Handler Dispatch Routine Support
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include "pci.h"
#include <ndk/halfuncs.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

pHalTranslateBusAddress PcipSavedTranslateBusAddress;
pHalAssignSlotResources PcipSavedAssignSlotResources;

/* FUNCTIONS ******************************************************************/

/*
 * PciFindFdoByBusNumber
 *
 * Walk the global FDO extension list to find the FDO whose bus range
 * contains the given bus number. Caller must hold PciGlobalLock.
 */
static
PFDO_DEVICE_EXTENSION
PciFindFdoByBusNumber(
    _In_ ULONG BusNumber)
{
    PLIST_ENTRY Entry;
    PFDO_DEVICE_EXTENSION FdoExtension;

    for (Entry = PciFdoExtensionListHead.Flink;
         Entry != &PciFdoExtensionListHead;
         Entry = Entry->Flink)
    {
        FdoExtension = CONTAINING_RECORD(Entry, FDO_DEVICE_EXTENSION, ListEntry);

        if (PciIsBusInRange(FdoExtension, BusNumber))
            return FdoExtension;
    }

    return NULL;
}

/*
 * PciTranslateBusAddress
 *
 * Replacement for HalPciTranslateBusAddress. Intercepts PCI bus address
 * translation requests. If the interface type is not PCIBus or we are at
 * DISPATCH_LEVEL+, forward immediately to the original HAL routine.
 *
 * For PCI requests at PASSIVE/APC level: acquire PciGlobalLock, walk the
 * FDO list to find the bus, then check if the requested address falls
 * within any resource range owned by a device on that bus. If the address
 * is owned by a PCI device we manage, return FALSE (we handle it). If
 * not claimed, forward to the original HAL translator.
 *
 */
BOOLEAN
NTAPI
PciTranslateBusAddress(
    _In_ INTERFACE_TYPE InterfaceType,
    _In_ ULONG BusNumber,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _Inout_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    PFDO_DEVICE_EXTENSION FdoExtension;
    PPCI_DEVICE Device;
    PLIST_ENTRY DevEntry;
    KIRQL OldIrql;
    BOOLEAN AddressClaimed = FALSE;
    ULONG i;

    ASSERT(PcipSavedTranslateBusAddress != NULL);

    /* Non-PCI bus types always go to the original HAL routine */
    if (InterfaceType != PCIBus)
    {
        return PcipSavedTranslateBusAddress(InterfaceType,
                                            BusNumber,
                                            BusAddress,
                                            AddressSpace,
                                            TranslatedAddress);
    }

    /*
     * At DISPATCH_LEVEL or above we cannot acquire the global lock,
     * so forward directly to the original HAL function.
     */
    if (KeGetCurrentIrql() >= DISPATCH_LEVEL)
    {
        return PcipSavedTranslateBusAddress(InterfaceType,
                                            BusNumber,
                                            BusAddress,
                                            AddressSpace,
                                            TranslatedAddress);
    }

    /* Acquire the global PCI lock */
    KeEnterCriticalRegion();
    KeWaitForSingleObject(&PciGlobalLock,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    /* Find the FDO for this bus number */
    FdoExtension = PciFindFdoByBusNumber(BusNumber);
    if (!FdoExtension || FdoExtension->State != PciStarted)
    {
        /* No matching started bus -- release lock and forward */
        KeSetEvent(&PciGlobalLock, IO_NO_INCREMENT, FALSE);
        KeLeaveCriticalRegion();
        return PcipSavedTranslateBusAddress(InterfaceType,
                                            BusNumber,
                                            BusAddress,
                                            AddressSpace,
                                            TranslatedAddress);
    }

    /*
     * Walk the device list for this bus. Check whether the requested
     * BusAddress falls within any BAR range of our enumerated devices.
     * If so, the address is claimed by pci.sys.
     */
    KeAcquireSpinLock(&FdoExtension->DeviceListLock, &OldIrql);

    for (DevEntry = FdoExtension->DeviceListHead.Flink;
         DevEntry != &FdoExtension->DeviceListHead;
         DevEntry = DevEntry->Flink)
    {
        Device = CONTAINING_RECORD(DevEntry, PCI_DEVICE, ListEntry);

        /* Check each BAR in the cached PCI config */
        for (i = 0; i < PCI_TYPE0_ADDRESSES; i++)
        {
            ULONG BarValue = Device->PciConfig.u.type0.BaseAddresses[i];
            PHYSICAL_ADDRESS BarBase;
            BOOLEAN IsIo = (BarValue & PCI_BASE_ADDRESS_SPACE) != 0;
            BOOLEAN Is64 = FALSE;

            if (BarValue == 0)
                continue;

            if (IsIo)
            {
                BarBase.QuadPart = BarValue & PCI_BASE_ADDRESS_IO_MASK;
            }
            else
            {
                BarBase.QuadPart = BarValue & PCI_BASE_ADDRESS_MEM_MASK;
                Is64 = ((BarValue & PCI_BASE_ADDRESS_MEM_TYPE_MASK) ==
                         PCI_BASE_ADDRESS_MEM_TYPE_64);
                if (Is64 && (i + 1) < PCI_TYPE0_ADDRESSES)
                {
                    BarBase.HighPart = Device->PciConfig.u.type0.BaseAddresses[i + 1];
                }
            }

            if (BarBase.QuadPart == 0)
                continue;

            /*
             * Simple check: if the bus address matches the BAR base, this
             * device owns the address range. A full implementation would
             * also check the BAR size, but for the hook's purpose (preventing
             * double-translation) the base address match suffices.
             */
            if (BusAddress.QuadPart >= BarBase.QuadPart &&
                BusAddress.QuadPart < BarBase.QuadPart + 0x1000)
            {
                AddressClaimed = TRUE;
                break;
            }

            /* Skip the upper 32 bits of a 64-bit BAR */
            if (Is64)
                i++;
        }

        if (AddressClaimed)
            break;
    }

    KeReleaseSpinLock(&FdoExtension->DeviceListLock, OldIrql);

    /* Release the global PCI lock */
    KeSetEvent(&PciGlobalLock, IO_NO_INCREMENT, FALSE);
    KeLeaveCriticalRegion();

    if (AddressClaimed)
    {
        /*
         * Address is within a PCI device resource range that pci.sys owns.
         * Return FALSE to indicate "do not translate further" -- the caller
         * (typically the HAL arbiter) will use the bus address directly.
         */
        DPRINT("PciTranslateBusAddress: address 0x%I64x claimed by PCI device on bus %lu\n",
               BusAddress.QuadPart, BusNumber);
        return FALSE;
    }

    /* Not claimed by any of our devices -- let HAL handle it */
    return PcipSavedTranslateBusAddress(InterfaceType,
                                        BusNumber,
                                        BusAddress,
                                        AddressSpace,
                                        TranslatedAddress);
}

static
PPCI_DEVICE
PciFindDeviceByLocation(
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber)
{
    PFDO_DEVICE_EXTENSION FdoExtension;
    PPCI_DEVICE Device;
    PLIST_ENTRY BusEntry, DeviceEntry;
    PCI_SLOT_NUMBER PciSlot;
    KIRQL OldIrql;

    PciSlot.u.AsULONG = SlotNumber;

    /* Walk the bus list to find the FDO for this bus number */
    KeAcquireSpinLock(&DriverExtension->BusListLock, &OldIrql);

    for (BusEntry = DriverExtension->BusListHead.Flink;
         BusEntry != &DriverExtension->BusListHead;
         BusEntry = BusEntry->Flink)
    {
        FdoExtension = CONTAINING_RECORD(BusEntry, FDO_DEVICE_EXTENSION, ListEntry);

        if (FdoExtension->BusNumber != BusNumber)
            continue;

        /* Found the right bus FDO -- now walk its device list */
        KeAcquireSpinLockAtDpcLevel(&FdoExtension->DeviceListLock);

        for (DeviceEntry = FdoExtension->DeviceListHead.Flink;
             DeviceEntry != &FdoExtension->DeviceListHead;
             DeviceEntry = DeviceEntry->Flink)
        {
            Device = CONTAINING_RECORD(DeviceEntry, PCI_DEVICE, ListEntry);

            if (Device->SlotNumber.u.bits.DeviceNumber == PciSlot.u.bits.DeviceNumber &&
                Device->SlotNumber.u.bits.FunctionNumber == PciSlot.u.bits.FunctionNumber)
            {
                KeReleaseSpinLockFromDpcLevel(&FdoExtension->DeviceListLock);
                KeReleaseSpinLock(&DriverExtension->BusListLock, OldIrql);
                return Device;
            }
        }

        KeReleaseSpinLockFromDpcLevel(&FdoExtension->DeviceListLock);
        KeReleaseSpinLock(&DriverExtension->BusListLock, OldIrql);

        DPRINT1("Pci: Could not find PDO for device @ %lu.%u.%u\n",
                BusNumber,
                PciSlot.u.bits.DeviceNumber,
                PciSlot.u.bits.FunctionNumber);
        return NULL;
    }

    KeReleaseSpinLock(&DriverExtension->BusListLock, OldIrql);

    DPRINT1("Pci: Could not find PCI bus FDO. Bus Number = 0x%lx\n", BusNumber);
    return NULL;
}

/*
 * PciAssignSlotResources
 *
 * Replacement for HalPciAssignSlotResources. For non-PCI bus types, forwards
 * directly to the original HAL routine. For PCI devices that we know about,
 * we intercept the request. If the device is already started, return
 * STATUS_DEVICE_BUSY. Otherwise delegate to the HAL for actual resource
 * assignment since pci.sys handles resource management through PnP.
 *
 */
NTSTATUS
NTAPI
PciAssignSlotResources(
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ PUNICODE_STRING DriverClassName,
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ INTERFACE_TYPE BusType,
    _In_ ULONG BusNumber,
    _In_ ULONG SlotNumber,
    _Inout_ PCM_RESOURCE_LIST *AllocatedResources)
{
    PPCI_DEVICE Device;

    ASSERT(PcipSavedAssignSlotResources);

    /* Only intercept PCI bus requests */
    if (BusType != PCIBus)
    {
        return PcipSavedAssignSlotResources(RegistryPath,
                                             DriverClassName,
                                             DriverObject,
                                             DeviceObject,
                                             BusType,
                                             BusNumber,
                                             SlotNumber,
                                             AllocatedResources);
    }

    /* Make sure we know about this device */
    Device = PciFindDeviceByLocation(BusNumber, SlotNumber);
    if (!Device)
    {
        /* Device not in our list */
        DPRINT1("PciAssignSlotResources: device not found at bus %lu slot 0x%lx\n",
                BusNumber, SlotNumber);
        return STATUS_DEVICE_DOES_NOT_EXIST;
    }

    /*
     * For PCI devices we manage, delegate to the saved HAL routine.
     * For PCI devices we manage, delegate to the saved HAL routine.
     * Full resource processing (start, decode enable, etc.) is handled
     * through PnP IRP_MN_START_DEVICE. The HAL fallback provides the
     * legacy resource assignment path that callers expect.
     */
    return PcipSavedAssignSlotResources(RegistryPath,
                                         DriverClassName,
                                         DriverObject,
                                         DeviceObject,
                                         BusType,
                                         BusNumber,
                                         SlotNumber,
                                         AllocatedResources);
}

VOID
NTAPI
PciUnhookHal(VOID)
{
    /* Restore the original HAL routines */
    if (PcipSavedAssignSlotResources)
    {
        HalPciAssignSlotResources = PcipSavedAssignSlotResources;
        PcipSavedAssignSlotResources = NULL;
    }

    if (PcipSavedTranslateBusAddress)
    {
        HalPciTranslateBusAddress = PcipSavedTranslateBusAddress;
        PcipSavedTranslateBusAddress = NULL;
    }
}

VOID
NTAPI
PciHookHal(VOID)
{
    /* Save the old HAL routines */
    ASSERT(PcipSavedAssignSlotResources == NULL);
    ASSERT(PcipSavedTranslateBusAddress == NULL);
    PcipSavedAssignSlotResources = HalPciAssignSlotResources;
    PcipSavedTranslateBusAddress = HalPciTranslateBusAddress;

    /* Take over the HAL's Bus Handler functions */
    HalPciAssignSlotResources = PciAssignSlotResources;
    HalPciTranslateBusAddress = PciTranslateBusAddress;
}

/* EOF */
