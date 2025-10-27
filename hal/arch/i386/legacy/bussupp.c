/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halx86/legacy/bussupp.c
 * PURPOSE:         HAL Legacy Bus Support Code
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include <intrin.h>
#include <reactos/hal/acpi_pci.h>
#define NDEBUG
#include <debug.h>

extern BOOLEAN HalpPciBusRangeKnown;
extern ULONG HalpMinPciBus, HalpMaxPciBus;

static
VOID
HalpTracePciRangeViolation(
    _In_z_ PCSTR ApiName,
    _In_ ULONG BusNumber,
    _In_ PVOID Caller)
{
    PVOID ImageBase = NULL;
    ULONG_PTR ImageOffset = 0;

    if (Caller && RtlPcToFileHeader(Caller, &ImageBase))
    {
        ImageOffset = (ULONG_PTR)Caller - (ULONG_PTR)ImageBase;
        DPRINT1("HAL: %s request for PCI bus %lu outside ACPI range [%lu-%lu] rejected (caller=%p image=%p+0x%Ix).\n",
                ApiName,
                BusNumber,
                HalpMinPciBus,
                HalpMaxPciBus,
                Caller,
                ImageBase,
                ImageOffset);
    }
    else
    {
        DPRINT1("HAL: %s request for PCI bus %lu outside ACPI range [%lu-%lu] rejected (caller=%p image=<unknown>).\n",
                ApiName,
                BusNumber,
                HalpMinPciBus,
                HalpMaxPciBus,
                Caller);
    }
}

CODE_SEG("INIT")
PBUS_HANDLER
NTAPI
HalpAllocateAndInitPciBusHandler(
    IN ULONG PciType,
    IN ULONG BusNo,
    IN BOOLEAN TestAllocation
);

/* Forward decl for internal allocator used in child bus creation */
PBUS_HANDLER
NTAPI
HalpAllocateBusHandler(IN INTERFACE_TYPE InterfaceType,
                       IN BUS_DATA_TYPE BusDataType,
                       IN ULONG BusNumber,
                       IN INTERFACE_TYPE ParentBusInterfaceType,
                       IN ULONG ParentBusNumber,
                       IN ULONG BusSpecificData);

/* Allocate a PCI child bus handler with a PCI parent and set ParentSlot */
#ifndef _MINIHAL_
static
PBUS_HANDLER
HalpAllocateAndInitPciChildBusHandler(
    IN ULONG PciType,
    IN ULONG ChildBusNo,
    IN ULONG ParentBusNo,
    IN PCI_SLOT_NUMBER ParentSlot)
{
    PBUS_HANDLER Bus;
    PPCIPBUSDATA BusData;

    /* Allocate handler with PCI parent */
    Bus = HalpAllocateBusHandler(PCIBus,
                                 PCIConfiguration,
                                 ChildBusNo,
                                 PCIBus,
                                 ParentBusNo,
                                 sizeof(PCIPBUSDATA));
    if (!Bus)
    {
        DPRINT1("HAL: Failed to allocate PCI child bus handler for bus %lu\n",
                ChildBusNo);
        return NULL;
    }

    if (!Bus->BusData)
    {
        DPRINT1("HAL: PCI child bus handler %p missing BusData\n", Bus);
        HalDereferenceBusHandler(Bus);
        return NULL;
    }

    if (!Bus->BusAddresses)
    {
        DPRINT1("HAL: PCI child bus handler %p missing address ranges\n", Bus);
        HalDereferenceBusHandler(Bus);
        return NULL;
    }

    /* Initialize like the root PCI bus handler does */
    Bus->GetBusData = HalpGetPCIData;
    Bus->SetBusData = HalpSetPCIData;
    Bus->GetInterruptVector = HalpGetPCIIntOnISABus;
    Bus->AdjustResourceList = HalpAdjustPCIResourceList;
    Bus->AssignSlotResources = HalpAssignPCISlotResources;
    Bus->BusAddresses->Dma.Limit = 0;

    BusData = (PPCIPBUSDATA)Bus->BusData;
    BusData->CommonData.Tag = PCI_DATA_TAG;
    BusData->CommonData.Version = PCI_DATA_VERSION;
    BusData->CommonData.ReadConfig = HalpReadPCIConfig;
    BusData->CommonData.WriteConfig = HalpWritePCIConfig;
    BusData->CommonData.Pin2Line = HalpPCIPin2ISALine;
    BusData->CommonData.Line2Pin = HalpPCIISALine2Pin;
    BusData->CommonData.ParentSlot = ParentSlot;
    BusData->MaxDevice = PCI_MAX_DEVICES;
    BusData->GetIrqRange = HalpGetISAFixedPCIIrq;
    BusData->ResourcesInitialized = FALSE;
    BusData->PciSegment = 0;
    BusData->AcpiRootConfigured = FALSE;
    BusData->AcpiRootInfo = NULL;
    BusData->IoWindowBase = HALP_INVALID_RANGE_BASE;
    BusData->IoWindowLimit = 0;
    BusData->MemoryWindowBase = HALP_INVALID_RANGE_BASE;
    BusData->MemoryWindowLimit = 0;
    BusData->PrefetchWindowBase = HALP_INVALID_RANGE_BASE;
    BusData->PrefetchWindowLimit = 0;
    BusData->BusNumbersConfigured = FALSE;
    BusData->BusNumberStart = 0;
    BusData->BusNumberEnd = 0;
    RtlZeroMemory(&BusData->OscInfo, sizeof(BusData->OscInfo));
    BusData->OscSupportSet = 0;
    BusData->OscControlRequest = 0;
    BusData->OscControlGranted = 0;
    BusData->OscNativeHotPlug = FALSE;
    BusData->OscNativePme = FALSE;
    BusData->OscNativeAer = FALSE;
    BusData->OscExpressCapability = FALSE;
    BusData->NativeExpressServicesConfigured = FALSE;

    RtlZeroMemory(BusData->ConfiguredBits, sizeof(BusData->ConfiguredBits));
    RtlInitializeBitMap(&BusData->DeviceConfigured,
                        BusData->ConfiguredBits,
                        sizeof(BusData->ConfiguredBits) * 8);

    switch (PciType)
    {
        case 1:
            RtlCopyMemory(&PCIConfigHandler, &PCIConfigHandlerType1, sizeof(PCIConfigHandler));
            BusData->Config.Type1.Address = PCI_TYPE1_ADDRESS_PORT;
            BusData->Config.Type1.Data = PCI_TYPE1_DATA_PORT;
            break;
        case 2:
            RtlCopyMemory(&PCIConfigHandler, &PCIConfigHandlerType2, sizeof(PCIConfigHandler));
            BusData->Config.Type2.CSE = PCI_TYPE2_CSE_PORT;
            BusData->Config.Type2.Forward = PCI_TYPE2_FORWARD_PORT;
            BusData->Config.Type2.Base = PCI_TYPE2_ADDRESS_BASE;
            BusData->MaxDevice = 16;
            break;
        default:
            DbgPrint("HAL: Unknown PCI type for child bus\n");
    }

    if (Bus->ParentHandler && Bus->ParentHandler->InterfaceType == PCIBus)
    {
        PPCIPBUSDATA ParentData = (PPCIPBUSDATA)Bus->ParentHandler->BusData;

        if (ParentData)
        {
            BusData->PciSegment = ParentData->PciSegment;

            if (ParentData->AcpiRootConfigured)
            {
                BusData->AcpiRootConfigured = TRUE;
                BusData->AcpiRootInfo = ParentData->AcpiRootInfo;
                BusData->IoWindowBase = ParentData->IoWindowBase;
                BusData->IoWindowLimit = ParentData->IoWindowLimit;
                BusData->MemoryWindowBase = ParentData->MemoryWindowBase;
                BusData->MemoryWindowLimit = ParentData->MemoryWindowLimit;
                BusData->PrefetchWindowBase = ParentData->PrefetchWindowBase;
                BusData->PrefetchWindowLimit = ParentData->PrefetchWindowLimit;
                BusData->BusNumbersConfigured = ParentData->BusNumbersConfigured;
                BusData->BusNumberStart = ParentData->BusNumberStart;
                BusData->BusNumberEnd = ParentData->BusNumberEnd;
                BusData->OscInfo = ParentData->OscInfo;
                BusData->OscSupportSet = ParentData->OscSupportSet;
                BusData->OscControlRequest = ParentData->OscControlRequest;
                BusData->OscControlGranted = ParentData->OscControlGranted;
                BusData->OscNativeHotPlug = ParentData->OscNativeHotPlug;
                BusData->OscNativePme = ParentData->OscNativePme;
                BusData->OscNativeAer = ParentData->OscNativeAer;
                BusData->OscExpressCapability = ParentData->OscExpressCapability;
                BusData->NativeExpressServicesConfigured = ParentData->NativeExpressServicesConfigured;

                HalpResetPciBusWindows(Bus);
                HalpFinalizePciBusRanges(Bus);
            }
        }
    }

    return Bus;
}
#endif // _MINIHAL_

CODE_SEG("INIT")
VOID
NTAPI
HalpFixupPciSupportedRanges(
    IN ULONG BusCount
);

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpGetChipHacks(
    IN USHORT VendorId,
    IN USHORT DeviceId,
    IN UCHAR RevisionId,
    IN PULONG HackFlags
);

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpGetPciBridgeConfig(
    IN ULONG PciType,
    IN PUCHAR BusCount
);

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsBridgeDevice(
    IN PPCI_COMMON_CONFIG PciData
);

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsIdeDevice(
    IN PPCI_COMMON_CONFIG PciData
);

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsRecognizedCard(
    IN PPCI_REGISTRY_INFO_INTERNAL PciRegistryInfo,
    IN PPCI_COMMON_CONFIG PciData,
    IN ULONG Flags
);

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsValidPCIDevice(
    IN PBUS_HANDLER BusHandler,
    IN PCI_SLOT_NUMBER Slot
);

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpMarkChipsetDecode(
    IN BOOLEAN OverrideEnable
);

CODE_SEG("INIT")
VOID
NTAPI
HalpRegisterInternalBusHandlers(
    VOID
);

CODE_SEG("INIT")
VOID
NTAPI
ShowSize(
    IN ULONG Size
);

/* GLOBALS ********************************************************************/

extern KSPIN_LOCK HalpPCIConfigLock;
ULONG HalpPciIrqMask;

/* PRIVATE FUNCTIONS **********************************************************/

PBUS_HANDLER
NTAPI
HalpAllocateBusHandler(IN INTERFACE_TYPE InterfaceType,
                       IN BUS_DATA_TYPE BusDataType,
                       IN ULONG BusNumber,
                       IN INTERFACE_TYPE ParentBusInterfaceType,
                       IN ULONG ParentBusNumber,
                       IN ULONG BusSpecificData)
{
    PBUS_HANDLER Bus;

    /* Register the bus handler */
    HalRegisterBusHandler(InterfaceType,
                          BusDataType,
                          BusNumber,
                          ParentBusInterfaceType,
                          ParentBusNumber,
                          BusSpecificData,
                          NULL,
                          &Bus);
    if (!Bus)
    {
        return NULL;
    }

    /* Check for a valid interface */
    if (InterfaceType != InterfaceTypeUndefined)
    {
        /* Allocate address ranges and zero them out */
        Bus->BusAddresses = ExAllocatePoolWithTag(NonPagedPoolMustSucceed,
                                                  sizeof(SUPPORTED_RANGES),
                                                  TAG_HAL);
        RtlZeroMemory(Bus->BusAddresses, sizeof(SUPPORTED_RANGES));

        /* Build the data structure */
        Bus->BusAddresses->Version = HAL_SUPPORTED_RANGE_VERSION;
        Bus->BusAddresses->Dma.Limit = 7;
        Bus->BusAddresses->Memory.Limit = 0xFFFFFFFF;
        Bus->BusAddresses->IO.Limit = 0xFFFF;
        Bus->BusAddresses->IO.SystemAddressSpace = 1;
        Bus->BusAddresses->PrefetchMemory.Base = 1;
    }

    /* Return the bus address */
    return Bus;
}

#ifndef _MINIHAL_
CODE_SEG("INIT")
VOID
NTAPI
HalpRegisterInternalBusHandlers(VOID)
{
    PBUS_HANDLER Bus;

    /* Only do processor 1 */
    if (KeGetCurrentPrcb()->Number) return;

    /* Register root support */
    HalpInitBusHandler();

    /* Allocate the system bus */
    Bus = HalpAllocateBusHandler(Internal,
                                 ConfigurationSpaceUndefined,
                                 0,
                                 InterfaceTypeUndefined,
                                 0,
                                 0);
    if (Bus)
    {
        /* Set it up */
        Bus->GetInterruptVector = HalpGetSystemInterruptVector;
        Bus->TranslateBusAddress = HalpTranslateSystemBusAddress;
    }

    /* Allocate the CMOS bus */
    Bus = HalpAllocateBusHandler(InterfaceTypeUndefined,
                                 Cmos,
                                 0,
                                 InterfaceTypeUndefined,
                                 0,
                                 0);
    if (Bus)
    {
        /* Set it up */
        Bus->GetBusData = HalpcGetCmosData;
        Bus->SetBusData = HalpcSetCmosData;
    }

    /* Allocate the CMOS bus */
    Bus = HalpAllocateBusHandler(InterfaceTypeUndefined,
                                 Cmos,
                                 1,
                                 InterfaceTypeUndefined,
                                 0,
                                 0);
    if (Bus)
    {
        /* Set it up */
        Bus->GetBusData = HalpcGetCmosData;
        Bus->SetBusData = HalpcSetCmosData;
    }

    /* Allocate ISA bus */
    Bus = HalpAllocateBusHandler(Isa,
                                 ConfigurationSpaceUndefined,
                                 0,
                                 Internal,
                                 0,
                                 0);
    if (Bus)
    {
        /* Set it up */
        Bus->GetBusData = HalpNoBusData;
        Bus->BusAddresses->Memory.Limit = 0xFFFFFF;
        Bus->TranslateBusAddress = HalpTranslateIsaBusAddress;
    }

    /* No support for EISA or MCA */
    ASSERT(HalpBusType == MACHINE_TYPE_ISA);
}
#endif // _MINIHAL_

#ifndef _MINIHAL_
CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpMarkChipsetDecode(BOOLEAN OverrideEnable)
{
    NTSTATUS Status;
    UNICODE_STRING KeyString;
    ULONG Data = OverrideEnable;
    HANDLE KeyHandle, Handle;

    /* Open CCS key */
    RtlInitUnicodeString(&KeyString,
                         L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET");
    Status = HalpOpenRegistryKey(&Handle, 0, &KeyString, KEY_ALL_ACCESS, FALSE);
    if (NT_SUCCESS(Status))
    {
        /* Open PNP Bios key */
        RtlInitUnicodeString(&KeyString, L"Control\\Biosinfo\\PNPBios");
        Status = HalpOpenRegistryKey(&KeyHandle,
                                     Handle,
                                     &KeyString,
                                     KEY_ALL_ACCESS,
                                     TRUE);

        /* Close root key */
        ZwClose(Handle);

        /* Check if PNP BIOS key exists */
        if (NT_SUCCESS(Status))
        {
            /* Set the override value */
            RtlInitUnicodeString(&KeyString, L"FullDecodeChipsetOverride");
            Status = ZwSetValueKey(KeyHandle,
                                   &KeyString,
                                   0,
                                   REG_DWORD,
                                   &Data,
                                   sizeof(Data));

            /* Close subkey */
            ZwClose(KeyHandle);
        }
    }

    /* Return status */
    return Status;
}

CODE_SEG("INIT")
PBUS_HANDLER
NTAPI
HalpAllocateAndInitPciBusHandler(IN ULONG PciType,
                                 IN ULONG BusNo,
                                 IN BOOLEAN TestAllocation)
{
    PBUS_HANDLER Bus;
    PPCIPBUSDATA BusData;

    /* Allocate the bus handler */
    Bus = HalpAllocateBusHandler(PCIBus,
                                 PCIConfiguration,
                                 BusNo,
                                 Internal,
                                 0,
                                 sizeof(PCIPBUSDATA));

    if (!Bus)
    {
        DPRINT1("HAL: Failed to allocate PCI bus handler for bus %lu\n", BusNo);
        return NULL;
    }

    if (!Bus->BusAddresses)
    {
        DPRINT1("HAL: Bus handler %p has no address ranges\n", Bus);
        HalDereferenceBusHandler(Bus);
        return NULL;
    }

    /* Set it up */
    Bus->GetBusData = HalpGetPCIData;
    Bus->SetBusData = HalpSetPCIData;
    Bus->GetInterruptVector = HalpGetPCIIntOnISABus;
    Bus->AdjustResourceList = HalpAdjustPCIResourceList;
    Bus->AssignSlotResources = HalpAssignPCISlotResources;
    Bus->BusAddresses->Dma.Limit = 0;

    if (!Bus->BusData)
    {
        DPRINT1("HAL: Bus handler %p missing BusData\n", Bus);
        HalDereferenceBusHandler(Bus);
        return NULL;
    }

    /* Get our custom bus data */
    BusData = (PPCIPBUSDATA)Bus->BusData;

    /* Setup custom bus data */
    BusData->CommonData.Tag = PCI_DATA_TAG;
    BusData->CommonData.Version = PCI_DATA_VERSION;
    BusData->CommonData.ReadConfig = HalpReadPCIConfig;
    BusData->CommonData.WriteConfig = HalpWritePCIConfig;
    BusData->CommonData.Pin2Line = HalpPCIPin2ISALine;
    BusData->CommonData.Line2Pin = HalpPCIISALine2Pin;
    BusData->MaxDevice = PCI_MAX_DEVICES;
    BusData->GetIrqRange = HalpGetISAFixedPCIIrq;
    BusData->ResourcesInitialized = FALSE;
    BusData->PciSegment = 0;
    BusData->AcpiRootConfigured = FALSE;

    /* Initialize the bitmap */
    RtlZeroMemory(BusData->ConfiguredBits, sizeof(BusData->ConfiguredBits));
    RtlInitializeBitMap(&BusData->DeviceConfigured,
                        BusData->ConfiguredBits,
                        sizeof(BusData->ConfiguredBits) * 8);

    /* Check the type of PCI bus */
    switch (PciType)
    {
        /* Type 1 PCI Bus */
        case 1:

            /* Copy the Type 1 handler data */
            RtlCopyMemory(&PCIConfigHandler,
                          &PCIConfigHandlerType1,
                          sizeof(PCIConfigHandler));

            /* Set correct I/O Ports */
            BusData->Config.Type1.Address = PCI_TYPE1_ADDRESS_PORT;
            BusData->Config.Type1.Data = PCI_TYPE1_DATA_PORT;
            break;

        /* Type 2 PCI Bus */
        case 2:

            /* Copy the Type 1 handler data */
            RtlCopyMemory(&PCIConfigHandler,
                          &PCIConfigHandlerType2,
                          sizeof (PCIConfigHandler));

            /* Set correct I/O Ports */
            BusData->Config.Type2.CSE = PCI_TYPE2_CSE_PORT;
            BusData->Config.Type2.Forward = PCI_TYPE2_FORWARD_PORT;
            BusData->Config.Type2.Base = PCI_TYPE2_ADDRESS_BASE;

            /* Only 16 devices supported, not 32 */
            BusData->MaxDevice = 16;
            break;

        default:

            /* Invalid type */
            DbgPrint("HAL: Unknown PCI type\n");
    }

    /* Return the bus handler */
    return Bus;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsValidPCIDevice(IN PBUS_HANDLER BusHandler,
                     IN PCI_SLOT_NUMBER Slot)
{
    UCHAR DataBuffer[PCI_COMMON_HDR_LENGTH];
    PPCI_COMMON_HEADER PciHeader = (PVOID)DataBuffer;
    ULONG i;
    ULONG_PTR Address;

    /* Read the PCI header */
    HalpReadPCIConfig(BusHandler, Slot, PciHeader, 0, PCI_COMMON_HDR_LENGTH);

    /* Make sure it's a valid device */
    if ((PciHeader->VendorID == PCI_INVALID_VENDORID) ||
        ((PciHeader->HeaderType & ~PCI_MULTIFUNCTION) != PCI_DEVICE_TYPE))
    {
        /* Bail out */
        return FALSE;
    }

    /* Make sure interrupt numbers make sense */
    if (((PciHeader->u.type0.InterruptPin) &&
         (PciHeader->u.type0.InterruptPin > 4)) ||
        (PciHeader->u.type0.InterruptLine & 0x70))
    {
        /* Bail out */
        return FALSE;
    }

    /* Now scan PCI BARs */
    for (i = 0; i < PCI_TYPE0_ADDRESSES; i++)
    {
        /* Check what kind of address it is */
        Address = PciHeader->u.type0.BaseAddresses[i];
        if (Address & PCI_ADDRESS_IO_SPACE)
        {
            /* Highest I/O port is 65535 */
            if (Address > 0xFFFF) return FALSE;
        }
        else
        {
            /* MMIO should be higher than 0x80000 */
            if ((Address > 0xF) && (Address < 0x80000)) return FALSE;
        }

        /* Is this a 64-bit address? */
        if (!(Address & PCI_ADDRESS_IO_SPACE) &&
            ((Address & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT))
        {
            /* Check the next-next entry, since this one 64-bits wide */
            i++;
        }
    }

    /* Header, interrupt and address data all make sense */
    return TRUE;
}

#define HALP_DEFAULT_IO_LIMIT   0xFFFFULL
#define HALP_MAX_IO_LIMIT       0xFFFFFFFFULL
#define HALP_DEFAULT_MEM_BASE   0xC0000000ULL
#define HALP_DEFAULT_MEM_LIMIT  0xFEBFFFFFULL

VOID
HalpResetPciBusWindows(
    PBUS_HANDLER BusHandler)
{
    PPCIPBUSDATA BusData;

    BusData = (PPCIPBUSDATA)BusHandler->BusData;
    if (!BusData)
    {
        return;
    }

    BusData->ResourcesInitialized = TRUE;

    if (BusData->AcpiRootConfigured)
    {
        if ((BusData->IoWindowBase != HALP_INVALID_RANGE_BASE) &&
            (BusData->IoWindowBase <= BusData->IoWindowLimit))
        {
            BusData->IoBase = BusData->IoWindowBase;
            BusData->IoLimit = BusData->IoWindowLimit;
        }
        else
        {
            BusData->IoBase = 0;
            BusData->IoLimit = HALP_DEFAULT_IO_LIMIT;
        }
        BusData->IoNext = BusData->IoBase;

        if ((BusData->MemoryWindowBase != HALP_INVALID_RANGE_BASE) &&
            (BusData->MemoryWindowBase <= BusData->MemoryWindowLimit))
        {
            BusData->MemoryBase = BusData->MemoryWindowBase;
            BusData->MemoryLimit = BusData->MemoryWindowLimit;
        }
        else
        {
            BusData->MemoryBase = HALP_DEFAULT_MEM_BASE;
            BusData->MemoryLimit = HALP_DEFAULT_MEM_LIMIT;
        }
        BusData->MemoryNext = BusData->MemoryBase;

        if ((BusData->PrefetchWindowBase != HALP_INVALID_RANGE_BASE) &&
            (BusData->PrefetchWindowBase <= BusData->PrefetchWindowLimit))
        {
            /* Leave as configured */
        }
        else
        {
            BusData->PrefetchWindowBase = HALP_INVALID_RANGE_BASE;
            BusData->PrefetchWindowLimit = 0;
        }
        return;
    }

    BusData->IoBase = 0;
    BusData->IoLimit = HALP_DEFAULT_IO_LIMIT;
    BusData->IoNext = BusData->IoBase;
    BusData->MemoryBase = HALP_DEFAULT_MEM_BASE;
    BusData->MemoryLimit = HALP_DEFAULT_MEM_LIMIT;
    BusData->MemoryNext = BusData->MemoryBase;
    BusData->IoWindowBase = HALP_INVALID_RANGE_BASE;
    BusData->IoWindowLimit = 0;
    BusData->MemoryWindowBase = HALP_INVALID_RANGE_BASE;
    BusData->MemoryWindowLimit = 0;
    BusData->PrefetchWindowBase = HALP_INVALID_RANGE_BASE;
    BusData->PrefetchWindowLimit = 0;
}

static
VOID
HalpExtendPciWindow(
    PBUS_HANDLER BusHandler,
    BOOLEAN IsIo,
    BOOLEAN IsPrefetch,
    ULONGLONG Base,
    ULONGLONG Limit)
{
    while (BusHandler && (BusHandler->InterfaceType == PCIBus))
    {
        PPCIPBUSDATA BusData;

        BusData = (PPCIPBUSDATA)BusHandler->BusData;
        if (!BusData)
        {
            break;
        }

        if (IsIo)
        {
            if (Base < BusData->IoWindowBase) BusData->IoWindowBase = Base;
            if (Limit > BusData->IoWindowLimit) BusData->IoWindowLimit = Limit;
        }
        else if (IsPrefetch)
        {
            if (Base < BusData->PrefetchWindowBase) BusData->PrefetchWindowBase = Base;
            if (Limit > BusData->PrefetchWindowLimit) BusData->PrefetchWindowLimit = Limit;
        }
        else
        {
            if (Base < BusData->MemoryWindowBase) BusData->MemoryWindowBase = Base;
            if (Limit > BusData->MemoryWindowLimit) BusData->MemoryWindowLimit = Limit;
        }

        BusHandler = BusHandler->ParentHandler;
    }
}

static
ULONGLONG
HalpPciCalculateBarLength(
    ULONGLONG Mask,
    BOOLEAN IsIo)
{
    ULONGLONG AddressMask;
    ULONGLONG Size;

    AddressMask = IsIo ? (ULONGLONG)PCI_ADDRESS_IO_ADDRESS_MASK
                       : 0xFFFFFFFFFFFFFFF0ULL;

    Size = Mask & AddressMask;
    if (!Size)
    {
        return 0;
    }

    Size &= ~(Size - 1);
    return Size;
}

static
VOID
HalpCollectPciBusWindows(
    PBUS_HANDLER BusHandler)
{
    union
    {
        PCI_COMMON_HEADER Header;
        PCI_COMMON_CONFIG Config;
    } PciBuffer;
    PCI_SLOT_NUMBER Slot;
    ULONG DeviceNumber, FunctionNumber;

    for (DeviceNumber = 0; DeviceNumber < PCI_MAX_DEVICES; DeviceNumber++)
    {
        Slot.u.bits.Reserved = 0;
        Slot.u.bits.DeviceNumber = DeviceNumber;

        for (FunctionNumber = 0; FunctionNumber < PCI_MAX_FUNCTION; FunctionNumber++)
        {
            ULONG BarLimit;
            ULONG BarNumber;
            BOOLEAN IsMultiFunction;

            Slot.u.bits.FunctionNumber = FunctionNumber;
            HalpReadPCIConfig(BusHandler,
                              Slot,
                              &PciBuffer.Config,
                              0,
                              PCI_COMMON_HDR_LENGTH);

            if (PciBuffer.Config.VendorID == PCI_INVALID_VENDORID)
            {
                if (FunctionNumber == 0) break;
                continue;
            }

            IsMultiFunction = (PciBuffer.Config.HeaderType & PCI_MULTIFUNCTION) ? TRUE : FALSE;

            switch (PciBuffer.Config.HeaderType & ~PCI_MULTIFUNCTION)
            {
                case PCI_DEVICE_TYPE:
                    BarLimit = PCI_TYPE0_ADDRESSES;
                    break;

                case PCI_BRIDGE_TYPE:
                    BarLimit = PCI_TYPE1_ADDRESSES;
                    break;

                default:
                    BarLimit = 0;
                    break;
            }

            for (BarNumber = 0; BarNumber < BarLimit; BarNumber++)
            {
                ULONG Offset;
                ULONG OriginalValue;
                ULONG MaskLow;
                ULONGLONG Mask;
                BOOLEAN IsIo;
                BOOLEAN Is64Bit = FALSE;
                BOOLEAN Prefetch = FALSE;
                ULONGLONG Length;
                ULONGLONG Base;
                ULONGLONG Limit;
                ULONG OriginalHigh = 0;

                Offset = FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[BarNumber]);
                OriginalValue = PciBuffer.Config.u.type0.BaseAddresses[BarNumber];

                MaskLow = 0xFFFFFFFF;
                HalpWritePCIConfig(BusHandler, Slot, &MaskLow, Offset, sizeof(MaskLow));
                HalpReadPCIConfig(BusHandler, Slot, &MaskLow, Offset, sizeof(MaskLow));

                IsIo = (MaskLow & PCI_ADDRESS_IO_SPACE) ? TRUE : FALSE;
                Mask = MaskLow;

                if (!IsIo)
                {
                    Prefetch = (MaskLow & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? TRUE : FALSE;
                    if ((MaskLow & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
                    {
                        ULONG MaskHigh;

                        MaskHigh = 0xFFFFFFFF;
                        HalpWritePCIConfig(BusHandler,
                                           Slot,
                                           &MaskHigh,
                                           Offset + sizeof(ULONG),
                                           sizeof(ULONG));
                        HalpReadPCIConfig(BusHandler,
                                          Slot,
                                          &MaskHigh,
                                          Offset + sizeof(ULONG),
                                          sizeof(ULONG));
                        Mask |= ((ULONGLONG)MaskHigh << 32);
                        Is64Bit = TRUE;
                        OriginalHigh = PciBuffer.Config.u.type0.BaseAddresses[BarNumber + 1];
                    }
                }

                HalpWritePCIConfig(BusHandler, Slot, &OriginalValue, Offset, sizeof(ULONG));
                if (Is64Bit)
                {
                    HalpWritePCIConfig(BusHandler,
                                       Slot,
                                       &OriginalHigh,
                                       Offset + sizeof(ULONG),
                                       sizeof(ULONG));
                }

                Length = HalpPciCalculateBarLength(Mask, IsIo);
                if (!Length)
                {
                    if (Is64Bit) BarNumber++;
                    continue;
                }

                if (IsIo)
                {
                    Base = OriginalValue & PCI_ADDRESS_IO_ADDRESS_MASK;
                }
                else
                {
                    Base = OriginalValue & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
                    if (Is64Bit)
                    {
                        Base |= ((ULONGLONG)OriginalHigh << 32);
                    }
                }

                if (!Base)
                {
                    if (Is64Bit) BarNumber++;
                    continue;
                }

                Limit = Base + Length - 1;
                HalpExtendPciWindow(BusHandler, IsIo, Prefetch, Base, Limit);

                if (Is64Bit)
                {
                    BarNumber++;
                }
            }

            if (!IsMultiFunction)
            {
                break;
            }
        }
    }
}

VOID
HalpFinalizePciBusRanges(
    PBUS_HANDLER BusHandler)
{
    PPCIPBUSDATA BusData;
    PSUPPORTED_RANGES Ranges;
    ULONGLONG IoLimit;
    ULONGLONG MemoryLimit;

    BusData = (PPCIPBUSDATA)BusHandler->BusData;
    Ranges = BusHandler->BusAddresses;
    if (!BusData || !Ranges)
    {
        return;
    }

    Ranges->Sorted = TRUE;
    Ranges->IO.Next = NULL;
    Ranges->Memory.Next = NULL;
    Ranges->PrefetchMemory.Next = NULL;

    IoLimit = HALP_MAX_IO_LIMIT;
    MemoryLimit = BusData->MemoryLimit ? BusData->MemoryLimit : HALP_DEFAULT_MEM_LIMIT;

    if ((BusData->IoWindowBase != HALP_INVALID_RANGE_BASE) &&
        (BusData->IoWindowBase <= BusData->IoWindowLimit))
    {
        Ranges->NoIO = 1;
        Ranges->IO.Base = BusData->IoWindowBase;
        Ranges->IO.Limit = BusData->IoWindowLimit;
    }
    else
    {
        Ranges->NoIO = 1;
        Ranges->IO.Base = 0;
        Ranges->IO.Limit = IoLimit;
    }
    Ranges->IO.SystemBase = 0;
    Ranges->IO.SystemAddressSpace = 1;

    if ((BusData->MemoryWindowBase != HALP_INVALID_RANGE_BASE) &&
        (BusData->MemoryWindowBase <= BusData->MemoryWindowLimit))
    {
        Ranges->NoMemory = 1;
        Ranges->Memory.Base = BusData->MemoryWindowBase;
        Ranges->Memory.Limit = BusData->MemoryWindowLimit;
    }
    else
    {
        Ranges->NoMemory = 1;
        Ranges->Memory.Base = 0;
        Ranges->Memory.Limit = MemoryLimit;
    }
    Ranges->Memory.SystemBase = 0;
    Ranges->Memory.SystemAddressSpace = 0;

    if ((BusData->PrefetchWindowBase != HALP_INVALID_RANGE_BASE) &&
        (BusData->PrefetchWindowBase <= BusData->PrefetchWindowLimit))
    {
        Ranges->NoPrefetchMemory = 1;
        Ranges->PrefetchMemory.Base = BusData->PrefetchWindowBase;
        Ranges->PrefetchMemory.Limit = BusData->PrefetchWindowLimit;
    }
    else
    {
        Ranges->NoPrefetchMemory = 0;
        Ranges->PrefetchMemory.Base = 0;
        Ranges->PrefetchMemory.Limit = 0;
    }
    Ranges->PrefetchMemory.SystemBase = 0;
    Ranges->PrefetchMemory.SystemAddressSpace = 0;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpGetChipHacks(IN USHORT VendorId,
                 IN USHORT DeviceId,
                 IN UCHAR RevisionId,
                 IN PULONG HackFlags)
{
    UNICODE_STRING KeyName, ValueName;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE KeyHandle;
    WCHAR Buffer[32];
    KEY_VALUE_PARTIAL_INFORMATION PartialInfo;
    ULONG ResultLength;

    /* Setup the object attributes for the key */
    RtlInitUnicodeString(&KeyName,
                         L"\\REGISTRY\\MACHINE\\SYSTEM\\CURRENTCONTROLSET\\"
                         L"Control\\HAL");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);

    /* Open the key */
    Status = ZwOpenKey(&KeyHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status)) return Status;

    /* Query value */
    swprintf(Buffer, L"%04X%04X", VendorId, DeviceId);
    RtlInitUnicodeString(&ValueName, Buffer);
    Status = ZwQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             &PartialInfo,
                             sizeof(PartialInfo),
                             &ResultLength);
    if (NT_SUCCESS(Status))
    {
        /* Return the flags */
        DbgPrint("\tFound HackFlags for your chipset\n");
        *HackFlags = *(PULONG)PartialInfo.Data;
        DbgPrint("\t\tHack Flags: %lx (Hack Revision: %lx-Your Revision: %lx)\n",
                 *HackFlags, HALP_REVISION_FROM_HACK_FLAGS(*HackFlags), RevisionId);

        /* Does it apply to this revision? */
        if ((RevisionId) && (RevisionId >= (HALP_REVISION_FROM_HACK_FLAGS(*HackFlags))))
        {
            /* Read the revision flags */
            *HackFlags = HALP_REVISION_HACK_FLAGS(*HackFlags);
        }

        /* Throw out revision data */
        *HackFlags = HALP_HACK_FLAGS(*HackFlags);
        if (!*HackFlags) DbgPrint("\tNo HackFlags for your chipset's revision!\n");
    }

    /* Close the handle and return */
    ZwClose(KeyHandle);
    return Status;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsRecognizedCard(IN PPCI_REGISTRY_INFO_INTERNAL PciRegistryInfo,
                     IN PPCI_COMMON_CONFIG PciData,
                     IN ULONG Flags)
{
    ULONG ElementCount, i;
    PPCI_CARD_DESCRIPTOR CardDescriptor;

    /* How many PCI Cards that we know about? */
    ElementCount = PciRegistryInfo->ElementCount;
    if (!ElementCount) return FALSE;

    /* Loop all descriptors */
    CardDescriptor = &PciRegistryInfo->CardList[0];
    for (i = 0; i < ElementCount; i++, CardDescriptor++)
    {
        /* Check for flag match */
        if (CardDescriptor->Flags != Flags) continue;

        /* Check for VID-PID match */
        if ((CardDescriptor->VendorID != PciData->VendorID) ||
            (CardDescriptor->DeviceID != PciData->DeviceID))
        {
            /* Skip */
            continue;
        }

        /* Check for revision match, if requested */
        if ((CardDescriptor->Flags & HALP_CHECK_CARD_REVISION_ID) &&
            (CardDescriptor->RevisionID != PciData->RevisionID))
        {
            /* Skip */
            continue;
        }

        /* Check what kind of device this is */
        switch (PCI_CONFIGURATION_TYPE(PciData))
        {
            /* CardBUS Bridge */
            case PCI_CARDBUS_BRIDGE_TYPE:

                /* This means the real device header is in the device-specific data */
                PciData = (PPCI_COMMON_CONFIG)PciData->DeviceSpecific;

            /* Normal PCI device */
            case PCI_DEVICE_TYPE:

                /* Check for subvendor match, if requested */
                if ((CardDescriptor->Flags & HALP_CHECK_CARD_SUBVENDOR_ID) &&
                    (CardDescriptor->SubsystemVendorID != PciData->u.type0.SubVendorID))
                {
                    /* Skip */
                    continue;
                }

                /* Check for subsystem match, if requested */
                if ((CardDescriptor->Flags & HALP_CHECK_CARD_SUBSYSTEM_ID) &&
                    (CardDescriptor->SubsystemID != PciData->u.type0.SubSystemID))
                {
                    /* Skip */
                    continue;
                }

                /* You made it! */
                return TRUE;

            /* PCI Bridge -- don't bother */
            case PCI_BRIDGE_TYPE:
            default:

                /* Recognize it */
                return TRUE;
        }
    }

    /* This means the card isn't recognized */
    return FALSE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsIdeDevice(IN PPCI_COMMON_CONFIG PciData)
{
    /* Simple test first */
    if ((PciData->BaseClass == PCI_CLASS_MASS_STORAGE_CTLR) &&
        (PciData->SubClass == PCI_SUBCLASS_MSC_IDE_CTLR))
    {
        /* The device is nice enough to admit it */
        return TRUE;
    }

    /* Symphony 82C101 */
    if (PciData->VendorID == 0x1C1C) return TRUE;

    /* ALi MS4803 or M5219 */
    if ((PciData->VendorID == 0x10B9) &&
        ((PciData->DeviceID == 0x5215) || (PciData->DeviceID == 0x5219)))
    {
        return TRUE;
    }

    /* Appian Technology */
    if ((PciData->VendorID == 0x1097) && (PciData->DeviceID == 0x38)) return TRUE;

    /* Compaq Triflex Dual EIDE Controller */
    if ((PciData->VendorID == 0xE11) && (PciData->DeviceID == 0xAE33)) return TRUE;

    /* Micron PC Tech RZ1000 */
    if ((PciData->VendorID == 0x1042) && (PciData->DeviceID == 0x1000)) return TRUE;

    /* SiS 85C601 or 5513 [IDE] */
    if ((PciData->VendorID == 0x1039) &&
        ((PciData->DeviceID == 0x601) || (PciData->DeviceID == 0x5513)))
    {
        return TRUE;
    }

    /* Symphony Labs W83769F */
    if ((PciData->VendorID == 0x10AD) &&
        ((PciData->DeviceID == 0x1) || (PciData->DeviceID == 0x150)))
    {
        return TRUE;
    }

    /* UMC UM8673F */
    if ((PciData->VendorID == 0x1060) && (PciData->DeviceID == 0x101)) return TRUE;

    /* You've survived */
    return FALSE;
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpIsBridgeDevice(IN PPCI_COMMON_CONFIG PciData)
{
    /* Either this is a PCI-to-PCI Bridge, or a CardBUS Bridge */
    return (((PCI_CONFIGURATION_TYPE(PciData) == PCI_BRIDGE_TYPE) &&
             (PciData->BaseClass == PCI_CLASS_BRIDGE_DEV) &&
             (PciData->SubClass == PCI_SUBCLASS_BR_PCI_TO_PCI)) ||
            ((PCI_CONFIGURATION_TYPE(PciData) == PCI_CARDBUS_BRIDGE_TYPE) &&
             (PciData->BaseClass == PCI_CLASS_BRIDGE_DEV) &&
             (PciData->SubClass == PCI_SUBCLASS_BR_CARDBUS)));
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
#ifndef _MINIHAL_
HalpGetPciBridgeConfig(IN ULONG PciType,
                       IN PUCHAR BusCount)
{
    PCI_SLOT_NUMBER PciSlot;
    ULONG i, j, k;
    union {
        PCI_COMMON_HEADER Header;
        PCI_COMMON_CONFIG Config;
    } PciBuffer;
    PPCI_COMMON_CONFIG PciData = &PciBuffer.Config;
    PBUS_HANDLER BusHandler;

    BOOLEAN Added = FALSE;
    /* Loop PCI buses */
    PciSlot.u.bits.Reserved = 0;
    for (i = 0; i < *BusCount; i++)
    {
        /* Get the bus handler */
        BusHandler = HalHandlerForBus(PCIBus, i);

        /* Loop every device */
        for (j = 0; j < PCI_MAX_DEVICES; j++)
        {
            /* Loop every function */
            PciSlot.u.bits.DeviceNumber = j;
            for (k = 0; k < PCI_MAX_FUNCTION; k++)
            {
                /* Build the final slot structure */
                PciSlot.u.bits.FunctionNumber = k;

                /* Read the configuration information */
                HalpReadPCIConfig(BusHandler,
                                  PciSlot,
                                  PciData,
                                  0,
                                  PCI_COMMON_HDR_LENGTH);

                /* Skip if this is an invalid function */
                if (PciData->VendorID == PCI_INVALID_VENDORID) continue;

                /* Make sure that this is a PCI bridge or a cardbus bridge */
                if (!HalpIsBridgeDevice(PciData)) continue;

                /* Found a bridge: read bus numbers */
                if ((PCI_CONFIGURATION_TYPE(PciData) == PCI_BRIDGE_TYPE) &&
                    (PciData->BaseClass == PCI_CLASS_BRIDGE_DEV) &&
                    (PciData->SubClass == PCI_SUBCLASS_BR_PCI_TO_PCI))
                {
                    UCHAR Sec = PciData->u.type1.SecondaryBus;
                    UCHAR Sub = PciData->u.type1.SubordinateBus;
                    UCHAR b;

                    if (Sec == 0 || Sub < Sec)
                    {
                        /* Ignore malformed entries */
                        continue;
                    }

                    /* Allocate handlers for each child bus if missing */
                    for (b = Sec; b <= Sub; b++)
                    {
                        if (!HalHandlerForBus(PCIBus, b))
                        {
                            if (HalpAllocateAndInitPciChildBusHandler(PciType, b, i, PciSlot))
                            {
                                Added = TRUE;
                                if (b + 1 > *BusCount) *BusCount = b + 1;
                            }
                        }
                    }
                }
            }
        }
    }

    /* Return whether we added any buses and need to rescan */
    return Added;
}
#else
HalpGetPciBridgeConfig(IN ULONG PciType,
                       IN PUCHAR BusCount)
{
    UNREFERENCED_PARAMETER(PciType);
    UNREFERENCED_PARAMETER(BusCount);
    return FALSE;
}
#endif // _MINIHAL_

CODE_SEG("INIT")
VOID
NTAPI
HalpFixupPciSupportedRanges(IN ULONG BusCount)
{
    ULONG i;
    PBUS_HANDLER Bus;
    PBUS_HANDLER SystemBus;
    PSUPPORTED_RANGES SystemRanges;

    DPRINT1("HalpFixupPciSupportedRanges %lu\n", BusCount);

    for (i = 0; i < BusCount; i++)
    {
        Bus = HalHandlerForBus(PCIBus, i);
        if (!Bus)
        {
            continue;
        }

        HalpResetPciBusWindows(Bus);
    }

    for (i = 0; i < BusCount; i++)
    {
        Bus = HalHandlerForBus(PCIBus, i);
        if (!Bus)
        {
            continue;
        }

        HalpCollectPciBusWindows(Bus);
    }

    for (i = 0; i < BusCount; i++)
    {
        Bus = HalHandlerForBus(PCIBus, i);
        if (!Bus)
        {
            continue;
        }

        HalpFinalizePciBusRanges(Bus);
    }

    SystemBus = HalHandlerForBus(Internal, 0);
    if (SystemBus)
    {
        SystemRanges = SystemBus->BusAddresses;
        if (SystemRanges)
        {
            SystemRanges->Sorted = TRUE;

            SystemRanges->NoIO = 1;
            SystemRanges->IO.Base = 0;
            SystemRanges->IO.Limit = HALP_MAX_IO_LIMIT;
            SystemRanges->IO.SystemBase = 0;
            SystemRanges->IO.SystemAddressSpace = 1;
            SystemRanges->IO.Next = NULL;

            SystemRanges->NoMemory = 1;
            SystemRanges->Memory.Base = 0;
            SystemRanges->Memory.Limit = HALP_DEFAULT_MEM_LIMIT;
            SystemRanges->Memory.SystemBase = 0;
            SystemRanges->Memory.SystemAddressSpace = 0;
            SystemRanges->Memory.Next = NULL;

            SystemRanges->NoPrefetchMemory = 0;
            SystemRanges->PrefetchMemory.Base = 0;
            SystemRanges->PrefetchMemory.Limit = 0;
            SystemRanges->PrefetchMemory.SystemBase = 0;
            SystemRanges->PrefetchMemory.SystemAddressSpace = 0;
            SystemRanges->PrefetchMemory.Next = NULL;
        }
    }
}

CODE_SEG("INIT")
VOID
NTAPI
ShowSize(ULONG x)
{
    if (!x) return;
    DbgPrint(" [size=");
    if (x < 1024)
    {
        DbgPrint("%d", (int) x);
    }
    else if (x < 1048576)
    {
        DbgPrint("%dK", (int)(x / 1024));
    }
    else if (x < 0x80000000)
    {
        DbgPrint("%dM", (int)(x / 1048576));
    }
    else
    {
        DbgPrint("%d", x);
    }
    DbgPrint("]");
}

/*
 * These includes are required to define
 * the ClassTable and VendorTable arrays.
 */
#define NEWLINE "\n" // "\r\n"
#include "pci_classes.h"
#include "pci_vendors.h"
CODE_SEG("INIT")
VOID
NTAPI
HalpDebugPciDumpBus(IN PBUS_HANDLER BusHandler,
                    IN PCI_SLOT_NUMBER PciSlot,
                    IN ULONG i,
                    IN ULONG j,
                    IN ULONG k,
                    IN PPCI_COMMON_CONFIG PciData)
{
    PCHAR p, ClassName, Boundary, SubClassName, VendorName, ProductName, SubVendorName;
    UCHAR HeaderType;
    ULONG Length;
    CHAR LookupString[16] = "";
    CHAR bSubClassName[64] = "Unknown";
    CHAR bVendorName[64] = "";
    CHAR bProductName[128] = "Unknown device";
    CHAR bSubVendorName[128] = "Unknown";
    ULONG Size, Mem, b;

    HeaderType = (PciData->HeaderType & ~PCI_MULTIFUNCTION);

    /* Isolate the class name */
    sprintf(LookupString, "C %02x  ", PciData->BaseClass);
    ClassName = strstr((PCHAR)ClassTable, LookupString);
    if (ClassName)
    {
        /* Isolate the subclass name */
        ClassName += strlen("C 00  ");
        Boundary = strstr(ClassName, NEWLINE "C ");
        sprintf(LookupString, NEWLINE "\t%02x  ", PciData->SubClass);
        SubClassName = strstr(ClassName, LookupString);
        if (Boundary && SubClassName > Boundary)
        {
            SubClassName = NULL;
        }
        if (!SubClassName)
        {
            SubClassName = ClassName;
        }
        else
        {
            SubClassName += strlen(NEWLINE "\t00  ");
        }
        /* Copy the subclass into our buffer */
        p = strpbrk(SubClassName, NEWLINE);
        Length = p - SubClassName;
        Length = min(Length, sizeof(bSubClassName) - 1);
        strncpy(bSubClassName, SubClassName, Length);
        bSubClassName[Length] = '\0';
    }

    /* Isolate the vendor name */
    sprintf(LookupString, NEWLINE "%04x  ", PciData->VendorID);
    VendorName = strstr((PCHAR)VendorTable, LookupString);
    if (VendorName)
    {
        /* Copy the vendor name into our buffer */
        VendorName += strlen(NEWLINE "0000  ");
        p = strpbrk(VendorName, NEWLINE);
        Length = p - VendorName;
        Length = min(Length, sizeof(bVendorName) - 1);
        strncpy(bVendorName, VendorName, Length);
        bVendorName[Length] = '\0';
        p += strlen(NEWLINE);
        while (*p == '\t' || *p == '#')
        {
            p = strpbrk(p, NEWLINE);
            p += strlen(NEWLINE);
        }
        Boundary = p;

        /* Isolate the product name */
        sprintf(LookupString, "\t%04x  ", PciData->DeviceID);
        ProductName = strstr(VendorName, LookupString);
        if (Boundary && ProductName >= Boundary)
        {
            ProductName = NULL;
        }
        if (ProductName)
        {
            /* Copy the product name into our buffer */
            ProductName += strlen("\t0000  ");
            p = strpbrk(ProductName, NEWLINE);
            Length = p - ProductName;
            Length = min(Length, sizeof(bProductName) - 1);
            strncpy(bProductName, ProductName, Length);
            bProductName[Length] = '\0';
            p += strlen(NEWLINE);
            while ((*p == '\t' && *(p + 1) == '\t') || *p == '#')
            {
                p = strpbrk(p, NEWLINE);
                p += strlen(NEWLINE);
            }
            Boundary = p;
            SubVendorName = NULL;

            if (HeaderType == PCI_DEVICE_TYPE)
            {
                /* Isolate the subvendor and subsystem name */
                sprintf(LookupString,
                        "\t\t%04x %04x  ",
                        PciData->u.type0.SubVendorID,
                        PciData->u.type0.SubSystemID);
                SubVendorName = strstr(ProductName, LookupString);
                if (Boundary && SubVendorName >= Boundary)
                {
                    SubVendorName = NULL;
                }
            }
            if (SubVendorName)
            {
                /* Copy the subvendor name into our buffer */
                SubVendorName += strlen("\t\t0000 0000  ");
                p = strpbrk(SubVendorName, NEWLINE);
                Length = p - SubVendorName;
                Length = min(Length, sizeof(bSubVendorName) - 1);
                strncpy(bSubVendorName, SubVendorName, Length);
                bSubVendorName[Length] = '\0';
            }
        }
    }

    /* Print out the data */
    DbgPrint("%02x:%02x.%x %s [%02x%02x]: %s %s [%04x:%04x] (rev %02x)\n",
             i,
             j,
             k,
             bSubClassName,
             PciData->BaseClass,
             PciData->SubClass,
             bVendorName,
             bProductName,
             PciData->VendorID,
             PciData->DeviceID,
             PciData->RevisionID);

    if (HeaderType == PCI_DEVICE_TYPE)
    {
        DbgPrint("\tSubsystem: %s [%04x:%04x]\n",
                 bSubVendorName,
                 PciData->u.type0.SubVendorID,
                 PciData->u.type0.SubSystemID);
    }

    /* Print out and decode flags */
    DbgPrint("\tFlags:");
    if (PciData->Command & PCI_ENABLE_BUS_MASTER) DbgPrint(" bus master,");
    if (PciData->Status & PCI_STATUS_66MHZ_CAPABLE) DbgPrint(" 66MHz,");
    if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x000) DbgPrint(" fast devsel,");
    if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x200) DbgPrint(" medium devsel,");
    if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x400) DbgPrint(" slow devsel,");
    if ((PciData->Status & PCI_STATUS_DEVSEL) == 0x600) DbgPrint(" unknown devsel,");
    DbgPrint(" latency %d", PciData->LatencyTimer);
    if (PciData->u.type0.InterruptPin != 0 &&
        PciData->u.type0.InterruptLine != 0 &&
        PciData->u.type0.InterruptLine != 0xFF) DbgPrint(", IRQ %02d", PciData->u.type0.InterruptLine);
    else if (PciData->u.type0.InterruptPin != 0) DbgPrint(", IRQ assignment required");
    DbgPrint("\n");

    if (HeaderType == PCI_BRIDGE_TYPE)
    {
        DbgPrint("\tBridge:");
        DbgPrint(" primary bus %d,", PciData->u.type1.PrimaryBus);
        DbgPrint(" secondary bus %d,", PciData->u.type1.SecondaryBus);
        DbgPrint(" subordinate bus %d,", PciData->u.type1.SubordinateBus);
        DbgPrint(" secondary latency %d", PciData->u.type1.SecondaryLatency);
        DbgPrint("\n");
    }

    /* Scan addresses */
    Size = 0;
    for (b = 0; b < (HeaderType == PCI_DEVICE_TYPE ? PCI_TYPE0_ADDRESSES : PCI_TYPE1_ADDRESSES); b++)
    {
        /* Check for a BAR */
        if (HeaderType != PCI_CARDBUS_BRIDGE_TYPE)
            Mem = PciData->u.type0.BaseAddresses[b];
        else
            Mem = 0;
        if (Mem)
        {
            ULONG PciBar = 0xFFFFFFFF;

            HalpWritePCIConfig(BusHandler,
                               PciSlot,
                               &PciBar,
                               FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.BaseAddresses[b]),
                               sizeof(ULONG));
            HalpReadPCIConfig(BusHandler,
                              PciSlot,
                              &PciBar,
                              FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.BaseAddresses[b]),
                              sizeof(ULONG));
            HalpWritePCIConfig(BusHandler,
                               PciSlot,
                               &Mem,
                               FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.BaseAddresses[b]),
                               sizeof(ULONG));

            /* Decode the address type */
            if (PciBar & PCI_ADDRESS_IO_SPACE)
            {
                /* Guess the size */
                Size = 1 << 2;
                while (!(PciBar & Size) && (Size)) Size <<= 1;

                /* Print it out */
                DbgPrint("\tI/O ports at %04lx", Mem & PCI_ADDRESS_IO_ADDRESS_MASK);
                ShowSize(Size);
            }
            else
            {
                /* Guess the size */
                Size = 1 << 4;
                while (!(PciBar & Size) && (Size)) Size <<= 1;

                /* Print it out */
                DbgPrint("\tMemory at %08lx (%d-bit, %sprefetchable)",
                         Mem & PCI_ADDRESS_MEMORY_ADDRESS_MASK,
                         (Mem & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_32BIT ? 32 : 64,
                         (Mem & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? "" : "non-");
                ShowSize(Size);
            }
            DbgPrint("\n");
        }
    }
}
#endif

CODE_SEG("INIT")
VOID
NTAPI
HalpInitializePciBus(VOID)
{

#ifndef _MINIHAL_
    PPCI_REGISTRY_INFO_INTERNAL PciRegistryInfo;
    UCHAR PciType;
    PCI_SLOT_NUMBER PciSlot;
    ULONG i, j, k;
    union {
        PCI_COMMON_HEADER Header;
        PCI_COMMON_CONFIG Config;
    } PciBuffer;
    PPCI_COMMON_CONFIG PciData = &PciBuffer.Config;
    PBUS_HANDLER BusHandler;
    ULONG HackFlags;
    BOOLEAN ExtendedAddressDecoding = FALSE;
    NTSTATUS Status;

    /* Query registry information */
    PciRegistryInfo = HalpQueryPciRegistryInfo();
    if (!PciRegistryInfo) return;

    /* Initialize the PCI configuration lock */
    KeInitializeSpinLock(&HalpPCIConfigLock);

    /* Get the type and free the info structure */
    PciType = PciRegistryInfo->HardwareMechanism & 0xF;

    /* Check if this is a type 2 PCI bus with at least one bus */
    if ((PciRegistryInfo->NoBuses) && (PciType == 2))
    {
        /* Setup the PCI slot */
        PciSlot.u.bits.Reserved = 0;
        PciSlot.u.bits.FunctionNumber = 0;

        /* Loop all slots */
        for (i = 0; i < 32; i++)
        {
            /* Try to setup a Type 2 PCI slot */
            PciType = 2;
            BusHandler = HalpAllocateAndInitPciBusHandler(2, 0, TRUE);
            if (!BusHandler) break;

            /* Now check if it's valid */
            if (HalpIsValidPCIDevice(BusHandler, PciSlot)) break;

            /* Heh, the BIOS lied... try Type 1 */
            PciType = 1;
            BusHandler = HalpAllocateAndInitPciBusHandler(1, 0, TRUE);
            if (!BusHandler) break;

            /* Now check if it's valid */
            if (HalpIsValidPCIDevice(BusHandler, PciSlot)) break;

            /* Keep trying */
            PciType = 2;
        }

        /* Now allocate the correct kind of handler */
        HalpAllocateAndInitPciBusHandler(PciType, 0, FALSE);
    }

    /* Okay, now loop all PCI bridges */
    do
    {
        /* Loop all PCI buses */
        for (i = 0; i < PciRegistryInfo->NoBuses; i++)
        {
            /* Check if we have a handler for it */
            if (!HalHandlerForBus(PCIBus, i))
            {
                /* Allocate it */
                HalpAllocateAndInitPciBusHandler(PciType, i, FALSE);
            }
        }
        /* Go to the next bridge */
    } while (HalpGetPciBridgeConfig(PciType, &PciRegistryInfo->NoBuses));

    /* Now build correct address range informaiton */
    HalpFixupPciSupportedRanges(PciRegistryInfo->NoBuses);

    /* Loop every bus */
    DbgPrint("\n====== PCI BUS HARDWARE DETECTION =======\n\n");
    PciSlot.u.bits.Reserved = 0;
    for (i = 0; i < PciRegistryInfo->NoBuses; i++)
    {
        /* Get the bus handler */
        BusHandler = HalHandlerForBus(PCIBus, i);

        /* Loop every device */
        for (j = 0; j < 32; j++)
        {
            /* Loop every function */
            PciSlot.u.bits.DeviceNumber = j;
            for (k = 0; k < 8; k++)
            {
                /* Build the final slot structure */
                PciSlot.u.bits.FunctionNumber = k;

                /* Read the configuration information */
                HalpReadPCIConfig(BusHandler,
                                  PciSlot,
                                  PciData,
                                  0,
                                  PCI_COMMON_HDR_LENGTH);

                /* Skip if this is an invalid function */
                if (PciData->VendorID == PCI_INVALID_VENDORID) continue;

                /* Print out the entry */
                HalpDebugPciDumpBus(BusHandler, PciSlot, i, j, k, PciData);

                /* Check if this is a Cardbus bridge */
                if (PCI_CONFIGURATION_TYPE(PciData) == PCI_CARDBUS_BRIDGE_TYPE)
                {
                    /* Not supported */
                    DbgPrint("\tDevice is a PCI Cardbus Bridge. It will not work!\n");
                    continue;
                }

                /* Check if this is a PCI device */
                if (PCI_CONFIGURATION_TYPE(PciData) != PCI_BRIDGE_TYPE)
                {
                    /* Check if it has an interrupt pin and line registered */
                    if ((PciData->u.type1.InterruptPin) &&
                        (PciData->u.type1.InterruptLine))
                    {
                        /* Check if this interrupt line is connected to the bus */
                        if (PciData->u.type1.InterruptLine < 16)
                        {
                            /* Is this an IDE device? */
                            if (!HalpIsIdeDevice(PciData))
                            {
                                /* We'll mask out this interrupt then */
                                DbgPrint("\tDevice is using IRQ %d! ISA Cards using that IRQ may fail!\n",
                                         PciData->u.type1.InterruptLine);
                                HalpPciIrqMask |= (1 << PciData->u.type1.InterruptLine);
                            }
                        }
                    }
                }

                /* Check for broken Intel chips */
                if (PciData->VendorID == 0x8086)
                {
                    /* Check for broken 82830 PCI controller */
                    if ((PciData->DeviceID == 0x04A3) &&
                        (PciData->RevisionID < 0x11))
                    {
                        /* Skip */
                        DbgPrint("\tDevice is a broken Intel 82430 PCI Controller. It will not work!\n\n");
                        continue;
                    }

                    /* Check for broken 82378 PCI-to-ISA Bridge */
                    if ((PciData->DeviceID == 0x0484) &&
                        (PciData->RevisionID <= 3))
                    {
                        /* Skip */
                        DbgPrint("\tDevice is a broken Intel 82378 PCI-to-ISA Bridge. It will not work!\n\n");
                        continue;
                    }

                    /* Check for broken 82450 PCI Bridge */
                    if ((PciData->DeviceID == 0x84C4) &&
                        (PciData->RevisionID <= 4))
                    {
                        DbgPrint("\tDevice is a Intel Orion 82450 PCI Bridge. It will not work!\n\n");
                        continue;
                    }
                }

                /* Do we know this card? */
                if (!ExtendedAddressDecoding)
                {
                    /* Check for it */
                    if (HalpIsRecognizedCard(PciRegistryInfo,
                                             PciData,
                                             HALP_CARD_FEATURE_FULL_DECODE))
                    {
                        /* We'll do chipset checks later */
                        DbgPrint("\tDevice has Extended Address Decoding. It may fail to work on older BIOSes!\n");
                        ExtendedAddressDecoding = TRUE;
                    }
                }

                /* Now check the registry for chipset hacks */
                Status = HalpGetChipHacks(PciData->VendorID,
                                          PciData->DeviceID,
                                          PciData->RevisionID,
                                          &HackFlags);
                if (NT_SUCCESS(Status))
                {
                    /* Check for broken ACPI routing */
                    if (HackFlags & HAL_PCI_CHIP_HACK_DISABLE_ACPI_IRQ_ROUTING)
                    {
                        DbgPrint("This chipset has broken ACPI IRQ Routing! Be aware!\n\n");
                        continue;
                    }

                    /* Check for broken ACPI timer */
                    if (HackFlags & HAL_PCI_CHIP_HACK_BROKEN_ACPI_TIMER)
                    {
                         DbgPrint("This chipset has a broken ACPI timer! Be aware!\n\n");
                         continue;
                    }

                    /* Check for hibernate-disable */
                    if (HackFlags & HAL_PCI_CHIP_HACK_DISABLE_HIBERNATE)
                    {
                        DbgPrint("This chipset has a broken PCI device which is incompatible with hibernation. Be aware!\n\n");
                        continue;
                    }

                    /* Check for USB controllers that generate SMIs */
                    if (HackFlags & HAL_PCI_CHIP_HACK_USB_SMI_DISABLE)
                    {
                        DbgPrint("This chipset has a USB controller which generates SMIs. ReactOS will likely fail to boot!\n\n");
                        continue;
                    }
                }

                /* Terminate the entry */
                DbgPrint("\n");
            }
        }
    }

    /* Initialize NMI Crash Flag */
    HalpGetNMICrashFlag();

    /* Free the registry data */
    ExFreePoolWithTag(PciRegistryInfo, TAG_HAL);

    /* Tell PnP if this hard supports correct decoding */
    HalpMarkChipsetDecode(ExtendedAddressDecoding);
    HalpPciLogEcamCoverage();
    DbgPrint("====== PCI BUS DETECTION COMPLETE =======\n\n");
#endif
}

#ifndef _MINIHAL_
CODE_SEG("INIT")
VOID
NTAPI
HalpInitBusHandlers(VOID)
{
    /* Register the HAL Bus Handler support */
    HalpRegisterInternalBusHandlers();
}

CODE_SEG("INIT")
VOID
NTAPI
HalpRegisterKdSupportFunctions(VOID)
{
    /* Guard against early call before HalPrivateDispatchTable is published */
#if defined(_M_AMD64) || defined(__x86_64__)
    if (HalPrivateDispatchTable == NULL)
    {
        return;
    }
#endif
    /* Register PCI Device Functions */
    KdSetupPciDeviceForDebugging = HalpSetupPciDeviceForDebugging;
    KdReleasePciDeviceforDebugging = HalpReleasePciDeviceForDebugging;

    /* Register memory functions */
#ifndef _MINIHAL_
#if (NTDDI_VERSION >= NTDDI_VISTA)
    KdMapPhysicalMemory64 = HalpMapPhysicalMemory64Vista;
    KdUnmapVirtualAddress = HalpUnmapVirtualAddressVista;
#else
    KdMapPhysicalMemory64 = HalpMapPhysicalMemory64;
    KdUnmapVirtualAddress = HalpUnmapVirtualAddress;
#endif
#endif

    /* Register ACPI stub */
    KdCheckPowerButton = HalpCheckPowerButton;
}
#endif // _MINIHAL_

NTSTATUS
NTAPI
HalpAssignSlotResources(IN PUNICODE_STRING RegistryPath,
                        IN PUNICODE_STRING DriverClassName,
                        IN PDRIVER_OBJECT DriverObject,
                        IN PDEVICE_OBJECT DeviceObject,
                        IN INTERFACE_TYPE BusType,
                        IN ULONG BusNumber,
                        IN ULONG SlotNumber,
                        IN OUT PCM_RESOURCE_LIST *AllocatedResources)
{
    PBUS_HANDLER Handler;
    NTSTATUS Status;
    PAGED_CODE();
    DPRINT1("Slot assignment for %d on bus %u\n", BusType, BusNumber);

    /* Find the handler */
    Handler = HalReferenceHandlerForBus(BusType, BusNumber);
    if (!Handler) return STATUS_NOT_FOUND;

    /* Do the assignment */
    Status = Handler->AssignSlotResources(Handler,
                                          Handler,
                                          RegistryPath,
                                          DriverClassName,
                                          DriverObject,
                                          DeviceObject,
                                          SlotNumber,
                                          AllocatedResources);

    /* Dereference the handler and return */
    HalDereferenceBusHandler(Handler);
    return Status;
}

BOOLEAN
NTAPI
HaliFindBusAddressTranslation(IN PHYSICAL_ADDRESS BusAddress,
                              IN OUT PULONG AddressSpace,
                              OUT PPHYSICAL_ADDRESS TranslatedAddress,
                              IN OUT PULONG_PTR Context,
                              IN BOOLEAN NextBus)
{
    PHAL_BUS_HANDLER BusHandler;
    PBUS_HANDLER Handler;
    PLIST_ENTRY NextEntry;

    /* Make sure we have a context */
    if (!Context)
        return FALSE;

    /* If we have data in the context, then this shouldn't be a new lookup */
    if ((*Context != 0) && (NextBus != FALSE))
        return FALSE;

    /* Find the bus handler */
    Handler = HalpContextToBusHandler(*Context);
    if (!Handler)
        return FALSE;

    /* Check if this is an ongoing lookup */
    if (NextBus)
    {
        /* Get the HAL bus handler */
        BusHandler = CONTAINING_RECORD(Handler, HAL_BUS_HANDLER, Handler);
        NextEntry = &BusHandler->AllHandlers;

        /* Get the next one if we were already with one */
        if (*Context)
            NextEntry = NextEntry->Flink;

        /* Start scanning */
        while (TRUE)
        {
            /* Check if this is the last one */
            if (NextEntry == &HalpAllBusHandlers)
            {
                /* Quit */
                *Context = 1;
                return FALSE;
            }

            /* Call this translator */
            BusHandler = CONTAINING_RECORD(NextEntry, HAL_BUS_HANDLER, AllHandlers);
            if (HalTranslateBusAddress(BusHandler->Handler.InterfaceType,
                                       BusHandler->Handler.BusNumber,
                                       BusAddress,
                                       AddressSpace,
                                       TranslatedAddress))
            {
                break;
            }

            /* Try the next one */
            NextEntry = NextEntry->Flink;
        }

        /* If we made it, we're done */
        *Context = (ULONG_PTR)&BusHandler->Handler;
        return TRUE;
    }

    /* Try the first one through */
    if (!HalTranslateBusAddress(Handler->InterfaceType,
                                Handler->BusNumber,
                                BusAddress,
                                AddressSpace,
                                TranslatedAddress))
    {
        return FALSE;
    }

    /* Remember for next time */
    *Context = (ULONG_PTR)Handler;
    return TRUE;
}

BOOLEAN
NTAPI
HaliTranslateBusAddress(IN INTERFACE_TYPE InterfaceType,
                        IN ULONG BusNumber,
                        IN PHYSICAL_ADDRESS BusAddress,
                        IN OUT PULONG AddressSpace,
                        OUT PPHYSICAL_ADDRESS TranslatedAddress)
{
    PBUS_HANDLER Handler;
    BOOLEAN Status;

    /* Find the handler */
    Handler = HalReferenceHandlerForBus(InterfaceType, BusNumber);
    if (!(Handler) || !(Handler->TranslateBusAddress))
    {
        //TODO enable thing for debug DPRINT1("No translator Interface: %x, Bus: %x, Handler: %p, BusAddress: %x!\n", InterfaceType, BusNumber, Handler, BusAddress);
        return FALSE;
    }

    /* Do the assignment */
    Status = Handler->TranslateBusAddress(Handler,
                                          Handler,
                                          BusAddress,
                                          AddressSpace,
                                          TranslatedAddress);

    /* Dereference the handler and return */
    HalDereferenceBusHandler(Handler);
    return Status;
}

/* PUBLIC FUNCTIONS **********************************************************/

#ifndef _MINIHAL_
/*
 * @implemented
 */
NTSTATUS
NTAPI
HalAdjustResourceList(IN PIO_RESOURCE_REQUIREMENTS_LIST *ResourceList)
{
    PBUS_HANDLER Handler;
    ULONG Status;
    PAGED_CODE();

    /* Find the handler */
    Handler = HalReferenceHandlerForBus((*ResourceList)->InterfaceType,
                                        (*ResourceList)->BusNumber);
    if (!Handler) return STATUS_SUCCESS;

    /* Do the assignment */
    Status = Handler->AdjustResourceList(Handler,
                                         Handler,
                                         ResourceList);

    /* Dereference the handler and return */
    HalDereferenceBusHandler(Handler);
    return Status;
}
#endif // _MINIHAL_

/*
 * @implemented
 */
NTSTATUS
NTAPI
HalAssignSlotResources(IN PUNICODE_STRING RegistryPath,
                       IN PUNICODE_STRING DriverClassName,
                       IN PDRIVER_OBJECT DriverObject,
                       IN PDEVICE_OBJECT DeviceObject,
                       IN INTERFACE_TYPE BusType,
                       IN ULONG BusNumber,
                       IN ULONG SlotNumber,
                       IN OUT PCM_RESOURCE_LIST *AllocatedResources)
{
    PAGED_CODE();

    /* Check the bus type */
    if (BusType != PCIBus)
    {
        /* Call our internal handler */
        return HalpAssignSlotResources(RegistryPath,
                                       DriverClassName,
                                       DriverObject,
                                       DeviceObject,
                                       BusType,
                                       BusNumber,
                                       SlotNumber,
                                       AllocatedResources);
    }
    else
    {
        /* Call the PCI registered function */
        return HalPciAssignSlotResources(RegistryPath,
                                         DriverClassName,
                                         DriverObject,
                                         DeviceObject,
                                         PCIBus,
                                         BusNumber,
                                         SlotNumber,
                                         AllocatedResources);
    }
}

#ifndef _MINIHAL_
/*
 * @implemented
 */
ULONG
NTAPI
HalGetBusData(IN BUS_DATA_TYPE BusDataType,
              IN ULONG BusNumber,
              IN ULONG SlotNumber,
              IN PVOID Buffer,
              IN ULONG Length)
{
    /* Call the extended function */
    return HalGetBusDataByOffset(BusDataType,
                                 BusNumber,
                                 SlotNumber,
                                 Buffer,
                                 0,
                                 Length);
}
#endif // _MINIHAL_

/*
 * @implemented
 */
ULONG
NTAPI
HalGetBusDataByOffset(IN BUS_DATA_TYPE BusDataType,
                      IN ULONG BusNumber,
                      IN ULONG SlotNumber,
                      IN PVOID Buffer,
                      IN ULONG Offset,
                      IN ULONG Length)
{
    PBUS_HANDLER Handler;
    ULONG Status;

    /* Find the handler */
    if ((BusDataType == PCIConfiguration) &&
        HalpPciBusRangeKnown &&
        (BusNumber < HalpMinPciBus || BusNumber > HalpMaxPciBus))
    {
        PVOID Caller = _ReturnAddress();
        HalpTracePciRangeViolation("HalGetBusDataByOffset",
                                   BusNumber,
                                   Caller);
        return 0;
    }

    Handler = HaliReferenceHandlerForConfigSpace(BusDataType, BusNumber);
    if (!Handler) return 0;

    /* Do the assignment */
    Status = Handler->GetBusData(Handler,
                                 Handler,
                                 SlotNumber,
                                 Buffer,
                                 Offset,
                                 Length);

    /* Dereference the handler and return */
    HalDereferenceBusHandler(Handler);
    return Status;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
HalQueryPciBusRange(
    _Out_opt_ PULONG MinBus,
    _Out_opt_ PULONG MaxBus)
{
    if (!HalpPciBusRangeKnown)
    {
        return FALSE;
    }

    if (MinBus)
    {
        *MinBus = HalpMinPciBus;
    }

    if (MaxBus)
    {
        *MaxBus = HalpMaxPciBus;
    }

    return TRUE;
}

#ifndef _MINIHAL_
/*
 * @implemented
 */
ULONG
NTAPI
HalGetInterruptVector(IN INTERFACE_TYPE InterfaceType,
                      IN ULONG BusNumber,
                      IN ULONG BusInterruptLevel,
                      IN ULONG BusInterruptVector,
                      OUT PKIRQL Irql,
                      OUT PKAFFINITY Affinity)
{
    PBUS_HANDLER Handler;
    ULONG Vector;
    PAGED_CODE();

    /* Defaults */
    *Irql = 0;
    *Affinity = 0;

    /* Find the handler */
    Handler = HalReferenceHandlerForBus(InterfaceType, BusNumber);
    if (!Handler) return 0;

    /* Do the assignment */
    Vector = Handler->GetInterruptVector(Handler,
                                         Handler,
                                         BusInterruptLevel,
                                         BusInterruptVector,
                                         Irql,
                                         Affinity);
    if ((Vector != IRQ2VECTOR(BusInterruptLevel)) ||
        (*Irql != VECTOR2IRQL(IRQ2VECTOR(BusInterruptLevel))))
    {
        DPRINT1("Returning IRQL %lx, Vector %lx for Level/Vector: %lx/%lx\n",
                *Irql, Vector, BusInterruptLevel, BusInterruptVector);
        DPRINT1("Old HAL would've returned IRQL %lx and Vector %lx\n",
                VECTOR2IRQL(IRQ2VECTOR(BusInterruptLevel)),
                IRQ2VECTOR(BusInterruptLevel));
    }

    /* Dereference the handler and return */
    HalDereferenceBusHandler(Handler);
    return Vector;
}

/*
 * @implemented
 */
ULONG
NTAPI
HalSetBusData(IN BUS_DATA_TYPE BusDataType,
              IN ULONG BusNumber,
              IN ULONG SlotNumber,
              IN PVOID Buffer,
              IN ULONG Length)
{
    /* Call the extended function */
    return HalSetBusDataByOffset(BusDataType,
                                 BusNumber,
                                 SlotNumber,
                                 Buffer,
                                 0,
                                 Length);
}

/*
 * @implemented
 */
ULONG
NTAPI
HalSetBusDataByOffset(IN BUS_DATA_TYPE BusDataType,
                      IN ULONG BusNumber,
                      IN ULONG SlotNumber,
                      IN PVOID Buffer,
                      IN ULONG Offset,
                      IN ULONG Length)
{
    PBUS_HANDLER Handler;
    ULONG Status;

    /* Find the handler */
    if ((BusDataType == PCIConfiguration) &&
        HalpPciBusRangeKnown &&
        (BusNumber < HalpMinPciBus || BusNumber > HalpMaxPciBus))
    {
        PVOID Caller = _ReturnAddress();
        HalpTracePciRangeViolation("HalSetBusDataByOffset",
                                   BusNumber,
                                   Caller);
        return 0;
    }

    Handler = HaliReferenceHandlerForConfigSpace(BusDataType, BusNumber);
    if (!Handler) return 0;

    /* Do the assignment */
    Status = Handler->SetBusData(Handler,
                                 Handler,
                                 SlotNumber,
                                 Buffer,
                                 Offset,
                                 Length);

    /* Dereference the handler and return */
    HalDereferenceBusHandler(Handler);
    return Status;
}
#endif // _MINIHAL_

/*
 * @implemented
 */
BOOLEAN
NTAPI
HalTranslateBusAddress(IN INTERFACE_TYPE InterfaceType,
                       IN ULONG BusNumber,
                       IN PHYSICAL_ADDRESS BusAddress,
                       IN OUT PULONG AddressSpace,
                       OUT PPHYSICAL_ADDRESS TranslatedAddress)
{
    /* Look as the bus type */
    if (InterfaceType == PCIBus)
    {
        /* Call the PCI registered function */
        return HalPciTranslateBusAddress(PCIBus,
                                         BusNumber,
                                         BusAddress,
                                         AddressSpace,
                                         TranslatedAddress);
    }
    else
    {
        /* Call the bus handler */
        return HaliTranslateBusAddress(InterfaceType,
                                       BusNumber,
                                       BusAddress,
                                       AddressSpace,
                                       TranslatedAddress);
    }
}

/* EOF */
