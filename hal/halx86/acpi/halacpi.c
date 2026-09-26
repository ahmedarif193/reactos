/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/halx86/acpi/halacpi.c
 * PURPOSE:         HAL ACPI Code
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
PHARDWARE_PTE HalpPteForFlush;
PVOID HalpVirtAddrForFlush;
PVOID HalpLowStub;

UCHAR HalpPicVectorOverrideTriggerMode[16];
UCHAR HalpPicVectorOverridePolarity[16]; /* Populated by MADT parsing for future IOAPIC use, currently unused */
BOOLEAN HalpPicVectorOverrideValid[16];

/* This determines the HAL type */
BOOLEAN HalDisableFirmwareMapper = TRUE;
PWCHAR HalHardwareIdString = L"acpipic_up";
PWCHAR HalName = L"ACPI Compatible Eisa/Isa HAL";

/* PRIVATE FUNCTIONS **********************************************************/

PVOID
NTAPI
HalpAcpiMapCachedTable(IN PHYSICAL_ADDRESS PhysicalAddress,
                       IN PFN_COUNT PageCount)
{
    /* HAL heap mappings stay valid for the lifetime of the system */
    return HalpMapPhysicalMemory64(PhysicalAddress, PageCount);
}

VOID
NTAPI
HaliAcpiTimerInit(IN ULONG TimerPort,
                  IN ULONG TimerValExt)
{
    PAGED_CODE();

    /* Is this in the init phase? */
    if (!TimerPort)
    {
        /* Get the data from the FADT */
        TimerPort = HalpFixedAcpiDescTable.pm_tmr_blk_io_port;
        TimerValExt = HalpFixedAcpiDescTable.flags & ACPI_TMR_VAL_EXT;
        DPRINT1("ACPI Timer at: %lXh (EXT: %lu)\n", TimerPort, TimerValExt);
    }

    /* FIXME: Now proceed to the timer initialization */
    //HalaAcpiTimerInit(TimerPort, TimerValExt);
}

BOOLEAN
NTAPI
HalpGetPmTimer(OUT PULONG Port,
               OUT PULONG Mask)
{
    PFADT Fadt = &HalpFixedAcpiDescTable;
    ULONGLONG Address;

    /* The fixed PM timer is optional, including on hardware-reduced ACPI. */
    if (Fadt->Header.Length < RTL_SIZEOF_THROUGH_FIELD(FADT, flags) ||
        (Fadt->flags & (1UL << 20)) || Fadt->pm_tmr_len != sizeof(ULONG))
    {
        return FALSE;
    }

    Address = Fadt->pm_tmr_blk_io_port;
    if (Fadt->Header.Length >= RTL_SIZEOF_THROUGH_FIELD(FADT, x_pm_tmr_blk) &&
        Fadt->x_pm_tmr_blk.Address.QuadPart != 0)
    {
        /* The extended address takes precedence. Only port I/O is used here;
         * an MMIO-only timer falls back to the existing RTC calibration. */
        if (Fadt->x_pm_tmr_blk.AddressSpaceID != 1 ||
            Fadt->x_pm_tmr_blk.BitOffset != 0)
        {
            return FALSE;
        }
        Address = Fadt->x_pm_tmr_blk.Address.QuadPart;
    }

    if (!Address || Address > 0xFFFC)
        return FALSE;

    *Port = (ULONG)Address;
    *Mask = (Fadt->flags & ACPI_TMR_VAL_EXT) ? MAXULONG : 0x00FFFFFF;
    return TRUE;
}

VOID
NTAPI
HalpAcpiReset(VOID)
{
    PFADT Fadt = &HalpFixedAcpiDescTable;
    PGEN_ADDR Register = &Fadt->reset_reg;
    PVOID Mapping = NULL;
    PHARDWARE_PTE PointerPte;

    /* Older FADTs do not contain the optional reset register. Use the cached
     * table: shutdown cannot allocate pool or acquire the ACPI table mutex. */
    if (Fadt->Header.Length < RTL_SIZEOF_THROUGH_FIELD(FADT, reset_val) ||
        !(Fadt->flags & (1UL << 10)) || Register->Address.QuadPart == 0 ||
        Register->BitOffset != 0)
    {
        return;
    }

    if (Register->AddressSpaceID == 1)
    {
        if (Register->Address.QuadPart > MAXUSHORT)
            return;

        DPRINT1("HAL: ACPI reset port %I64x value %02x\n",
                Register->Address.QuadPart, Fadt->reset_val);
        /* ACPI defines this as an 8-bit register. As in ACPICA, ignore an
         * incorrect firmware width for the system-I/O form. */
        WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)Register->Address.QuadPart, Fadt->reset_val);
    }
    else if (Register->AddressSpaceID == 0 && Register->BitWidth == 8)
    {
        Mapping = HalpMapPhysicalMemory64(Register->Address, 1);
        if (Mapping == NULL)
            return;

        PointerPte = HalAddressToPte(Mapping);
        PointerPte->CacheDisable = 1;
        PointerPte->WriteThrough = 1;
        HalpFlushTLB();

        DPRINT1("HAL: ACPI reset memory %I64x value %02x\n",
                Register->Address.QuadPart, Fadt->reset_val);
        WRITE_REGISTER_UCHAR((PUCHAR)Mapping, Fadt->reset_val);
        KeFlushWriteBuffer();
    }
    else
    {
        return;
    }

    /* Give the platform time to assert reset before trying the legacy path. */
    KeStallExecutionProcessor(100000);
    if (Mapping != NULL)
        HalpUnmapVirtualAddress(Mapping, 1);
    DPRINT1("HAL: ACPI reset returned; trying keyboard-controller reset\n");
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpSetupAcpiPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    NTSTATUS Status;
    PFADT Fadt;
    ULONG TableLength;
    PHYSICAL_ADDRESS PhysicalAddress;

    /* Only do this once */
    if (HalpProcessedACPIPhase0) return STATUS_SUCCESS;

    /* Claim the low AP stub before ACPI table copies consume low memory. */
    if (!HalpLowStubPhysicalAddress.QuadPart)
    {
        HalpLowStubPhysicalAddress.QuadPart = HalpAllocPhysicalMemory(LoaderBlock,
                                                                      0x100000,
                                                                      HALP_LOW_STUB_SIZE_IN_PAGES,
                                                                      FALSE);
        if (HalpLowStubPhysicalAddress.QuadPart)
        {
            HalpLowStub = HalpMapPhysicalMemory64(HalpLowStubPhysicalAddress, HALP_LOW_STUB_SIZE_IN_PAGES);
        }
        else
        {
            DPRINT1("HAL: No low memory for AP startup stub, SMP unavailable\n");
        }
    }

    /* Setup the ACPI table cache */
    Status = HalpAcpiTableCacheInit(LoaderBlock);
    if (!NT_SUCCESS(Status)) return Status;

    /* Let drivers look up cached tables through the HAL dispatch table */
    HalGetCachedAcpiTable = HaliGetCachedAcpiTable;

    /* Grab the FADT */
    Fadt = HalAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
    if (!Fadt)
    {
        /* Fail */
        DPRINT1("HAL: Didn't find the FACP\n");
        return STATUS_NOT_FOUND;
    }

    /* Assume typical size, otherwise whatever the descriptor table says */
    TableLength = sizeof(FADT);
    if (Fadt->Header.Length < sizeof(FADT)) TableLength = Fadt->Header.Length;

    /* Copy it in the HAL static buffer */
    RtlCopyMemory(&HalpFixedAcpiDescTable, Fadt, TableLength);

    /* Anything special this HAL needs to do? */
    HalpAcpiDetectMachineSpecificActions(LoaderBlock, &HalpFixedAcpiDescTable);

    /* Get the debug table for KD */
    HalpDebugPortTable = HalAcpiGetTable(LoaderBlock, DBGP_SIGNATURE);

    /* Initialize NUMA through the SRAT */
    HalpNumaInitializeStaticConfiguration(LoaderBlock);

    /* Initialize hotplug through the SRAT */
    HalpDynamicSystemResourceConfiguration(LoaderBlock);
    if (HalpAcpiSrat)
    {
        DPRINT1("Your machine has a SRAT, but NUMA/HotPlug are not supported!\n");
    }

    /* Can there be memory higher than 4GB? */
    if (HalpMaxHotPlugMemoryAddress.HighPart >= 1)
    {
        /* We'll need this for DMA later */
        HalpPhysicalMemoryMayAppearAbove4GB = TRUE;
    }

    /* Setup the ACPI timer */
    HaliAcpiTimerInit(0, 0);

    /* Grab a page for flushes */
    PhysicalAddress.QuadPart = 0x100000;
    HalpVirtAddrForFlush = HalpMapPhysicalMemory64(PhysicalAddress, 1);
    HalpPteForFlush = HalAddressToPte(HalpVirtAddrForFlush);

    /* Don't do this again */
    HalpProcessedACPIPhase0 = TRUE;

    /* Setup the boot table */
    HalpInitBootTable(LoaderBlock);

    /* Log some ACPI data */
    HalpAcpiLogTables(Fadt);

    /* Return success */
    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
NTAPI
HalpInitializePciBus(VOID)
{
    /* Setup the PCI stub support */
    HalpInitializePciStubs();

    /* Initialize PCIe extended config via ACPI MCFG/ECAM (later we will use APIC in x86) */
#ifdef _M_AMD64
    HalpAcpiPcieInitializeExtendedConfig();
#endif

    /* Set the NMI crash flag */
    HalpGetNMICrashFlag();
}

/*
 * @implemented
 */
CODE_SEG("INIT")
VOID
NTAPI
HalReportResourceUsage(VOID)
{
    INTERFACE_TYPE InterfaceType;
    UNICODE_STRING HalString;

    /* FIXME: Initialize DMA 64-bit support */

    /* FIXME: Initialize MCA bus */

    /* Initialize PCI bus. */
    HalpInitializePciBus();

    /* What kind of bus is this? */
    switch (HalpBusType)
    {
        /* ISA Machine */
        case MACHINE_TYPE_ISA:
            InterfaceType = Isa;
            break;

        /* EISA Machine */
        case MACHINE_TYPE_EISA:
            InterfaceType = Eisa;
            break;

        /* MCA Machine */
        case MACHINE_TYPE_MCA:
            InterfaceType = MicroChannel;
            break;

        /* Unknown */
        default:
            InterfaceType = Internal;
            break;
    }

    /* Build HAL usage */
    RtlInitUnicodeString(&HalString, HalName);
    HalpReportResourceUsage(&HalString, InterfaceType);

    /* Setup PCI debugging and Hibernation */
    HalpRegisterPciDebuggingDeviceInfo();
}

/* EOF */
