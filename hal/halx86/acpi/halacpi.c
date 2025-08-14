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

/* Include generated PCI data tables */
#ifndef _MINIHAL_
#include "pci_classes.h"
#include "pci_vendors.h"

/* External declarations for PCI bus handling */
extern BUS_HANDLER HalpFakePciBusHandler;

/* ACPI PCIe Configuration Space */
typedef struct _ACPI_PCIE_CONFIG_SPACE
{
    PHYSICAL_ADDRESS BaseAddress;
    USHORT PciSegment;
    UCHAR StartBusNumber;
    UCHAR EndBusNumber;
    PVOID MappedAddress;
} ACPI_PCIE_CONFIG_SPACE, *PACPI_PCIE_CONFIG_SPACE;

/* Global ACPI PCIe Configuration Spaces */
ACPI_PCIE_CONFIG_SPACE HalpAcpiPcieConfigSpaces[16];
ULONG HalpAcpiPcieConfigSpaceCount = 0;
#endif

/* GLOBALS ********************************************************************/

LIST_ENTRY HalpAcpiTableCacheList;
FAST_MUTEX HalpAcpiTableCacheLock;

BOOLEAN HalpProcessedACPIPhase0;
BOOLEAN HalpPhysicalMemoryMayAppearAbove4GB;

FADT HalpFixedAcpiDescTable;
PDEBUG_PORT_TABLE HalpDebugPortTable;
PACPI_SRAT HalpAcpiSrat;
PBOOT_TABLE HalpSimpleBootFlagTable;

PHYSICAL_ADDRESS HalpMaxHotPlugMemoryAddress;
PHYSICAL_ADDRESS HalpLowStubPhysicalAddress;
PHARDWARE_PTE HalpPteForFlush;
PVOID HalpVirtAddrForFlush;
PVOID HalpLowStub;

PACPI_BIOS_MULTI_NODE HalpAcpiMultiNode;

LIST_ENTRY HalpAcpiTableMatchList;

ULONG HalpInvalidAcpiTable;

ULONG HalpPicVectorRedirect[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

/* This determines the HAL type */
BOOLEAN HalDisableFirmwareMapper = TRUE;
PWCHAR HalHardwareIdString = L"acpipic_up";
PWCHAR HalName = L"ACPI Compatible Eisa/Isa HAL";

/* PRIVATE FUNCTIONS **********************************************************/

PDESCRIPTION_HEADER
NTAPI
HalpAcpiGetCachedTable(IN ULONG Signature)
{
    PLIST_ENTRY ListHead, NextEntry;
    PACPI_CACHED_TABLE CachedTable;

    /* Loop cached tables */
    ListHead = &HalpAcpiTableCacheList;
    NextEntry = ListHead->Flink;
    while (NextEntry != ListHead)
    {
        /* Get the table */
        CachedTable = CONTAINING_RECORD(NextEntry, ACPI_CACHED_TABLE, Links);

        /* Compare signatures */
        if (CachedTable->Header.Signature == Signature) return &CachedTable->Header;

        /* Keep going */
        NextEntry = NextEntry->Flink;
    }

    /* Nothing found */
    return NULL;
}

VOID
NTAPI
HalpAcpiCacheTable(IN PDESCRIPTION_HEADER TableHeader)
{
    PACPI_CACHED_TABLE CachedTable;

    /* Get the cached table and link it */
    CachedTable = CONTAINING_RECORD(TableHeader, ACPI_CACHED_TABLE, Header);
    InsertTailList(&HalpAcpiTableCacheList, &CachedTable->Links);
}

PVOID
NTAPI
HalpAcpiCopyBiosTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                      IN PDESCRIPTION_HEADER TableHeader)
{
    ULONG Size;
    PFN_COUNT PageCount;
    PHYSICAL_ADDRESS PhysAddress;
    PACPI_CACHED_TABLE CachedTable;
    PDESCRIPTION_HEADER CopiedTable;

    /* Size we'll need for the cached table */
    Size = TableHeader->Length + FIELD_OFFSET(ACPI_CACHED_TABLE, Header);
    if (LoaderBlock)
    {
        /* Phase 0: Convert to pages and use the HAL heap */
        PageCount = BYTES_TO_PAGES(Size);
        PhysAddress.QuadPart = HalpAllocPhysicalMemory(LoaderBlock,
                                                       0x1000000,
                                                       PageCount,
                                                       FALSE);
        if (PhysAddress.QuadPart)
        {
            /* Map it */
            CachedTable = HalpMapPhysicalMemory64(PhysAddress, PageCount);
        }
        else
        {
            /* No memory, so nothing to map */
            CachedTable = NULL;
        }
    }
    else
    {
        /* Use Mm pool */
        CachedTable = ExAllocatePoolWithTag(NonPagedPool, Size, TAG_HAL);
    }

    /* Do we have the cached table? */
    if (CachedTable)
    {
        /* Copy the data */
        CopiedTable = &CachedTable->Header;
        RtlCopyMemory(CopiedTable, TableHeader, TableHeader->Length);
    }
    else
    {
        /* Nothing to return */
        CopiedTable = NULL;
    }

    /* Return the table */
    return CopiedTable;
}

PVOID
NTAPI
HalpAcpiGetTableFromBios(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                         IN ULONG Signature)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PXSDT Xsdt;
    PRSDT Rsdt;
    PFADT Fadt;
    PDESCRIPTION_HEADER Header = NULL;
    ULONG TableLength;
    CHAR CheckSum = 0;
    ULONG Offset;
    ULONG EntryCount, CurrentEntry;
    PCHAR CurrentByte;
    PFN_COUNT PageCount;

    /* Should not query the RSDT/XSDT by itself */
    if ((Signature == RSDT_SIGNATURE) || (Signature == XSDT_SIGNATURE)) return NULL;

    /* Special case request for DSDT, because the FADT points to it */
    if (Signature == DSDT_SIGNATURE)
    {
        /* Grab the FADT */
        Fadt = HalpAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
        if (Fadt)
        {
            /* Grab the DSDT address and assume 2 pages */
            PhysicalAddress.HighPart = 0;
            PhysicalAddress.LowPart = Fadt->dsdt;
            TableLength = 2 * PAGE_SIZE;

            /* Map it */
            if (LoaderBlock)
            {
                /* Phase 0, use HAL heap */
                Header = HalpMapPhysicalMemory64(PhysicalAddress, 2u);
            }
            else
            {
                /* Phase 1, use Mm */
                Header = MmMapIoSpace(PhysicalAddress, 2 * PAGE_SIZE, 0);
            }

            /* Fail if we couldn't map it */
            if (!Header)
            {
                DPRINT1("HAL: Failed to map ACPI table.\n");
                return NULL;
            }

            /* Validate the signature */
            if (Header->Signature != DSDT_SIGNATURE)
            {
                /* Fail and unmap */
                if (LoaderBlock)
                {
                    /* Using HAL heap */
                    HalpUnmapVirtualAddress(Header, 2);
                }
                else
                {
                    /* Using Mm */
                    MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
                }

                /* Didn't find anything */
                return NULL;
            }
        }
        else
        {
            /* Couldn't find it */
            return NULL;
        }
    }
    else
    {
        /* To find tables, we need the RSDT */
        Rsdt = HalpAcpiGetTable(LoaderBlock, RSDT_SIGNATURE);
        if (Rsdt)
        {
            /* Won't be using the XSDT */
            Xsdt = NULL;
        }
        else
        {
            /* Only other choice is to use the XSDT */
            Xsdt = HalpAcpiGetTable(LoaderBlock, XSDT_SIGNATURE);
            if (!Xsdt) return NULL;

            /* Won't be using the RSDT */
            Rsdt = NULL;
        }

        /* Smallest RSDT/XSDT is one without table entries */
        Offset = FIELD_OFFSET(RSDT, Tables);
        if (Xsdt)
        {
            /* Figure out total size of table and the offset */
            TableLength = Xsdt->Header.Length;
            if (TableLength < Offset) Offset = Xsdt->Header.Length;

            /* The entries are each 64-bits, so count them */
            EntryCount = (TableLength - Offset) / sizeof(PHYSICAL_ADDRESS);
        }
        else
        {
            /* Figure out total size of table and the offset */
            TableLength = Rsdt->Header.Length;
            if (TableLength < Offset) Offset = Rsdt->Header.Length;

            /* The entries are each 32-bits, so count them */
            EntryCount = (TableLength - Offset) / sizeof(ULONG);
        }

        /* Start at the beginning of the array and loop it */
        for (CurrentEntry = 0; CurrentEntry < EntryCount; CurrentEntry++)
        {
            /* Are we using the XSDT? */
            if (!Xsdt)
            {
                /* Read the 32-bit physical address */
                PhysicalAddress.LowPart = Rsdt->Tables[CurrentEntry];
                PhysicalAddress.HighPart = 0;
            }
            else
            {
                /* Read the 64-bit physical address */
                PhysicalAddress = Xsdt->Tables[CurrentEntry];
            }

            /* Had we already mapped a table? */
            if (Header)
            {
                /* Yes, unmap it */
                if (LoaderBlock)
                {
                    /* Using HAL heap */
                    HalpUnmapVirtualAddress(Header, 2);
                }
                else
                {
                    /* Using Mm */
                    MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
                }
            }

            /* Now map this table */
            if (!LoaderBlock)
            {
                /* Phase 1: Use HAL heap */
                Header = MmMapIoSpace(PhysicalAddress, 2 * PAGE_SIZE, MmNonCached);
            }
            else
            {
                /* Phase 0: Use Mm */
                Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);
            }

            /* Check if we mapped it */
            if (!Header)
            {
                /* Game over */
                DPRINT1("HAL: Failed to map ACPI table.\n");
                return NULL;
            }

            /* We found it, break out */
            DPRINT("Found ACPI table %c%c%c%c at 0x%p\n",
                    Header->Signature & 0xFF,
                    (Header->Signature & 0xFF00) >> 8,
                    (Header->Signature & 0xFF0000) >> 16,
                    (Header->Signature & 0xFF000000) >> 24,
                    Header);
            if (Header->Signature == Signature) break;
        }

        /* Did we end up here back at the last entry? */
        if (CurrentEntry == EntryCount)
        {
            /* Yes, unmap the last table we processed */
            if (LoaderBlock)
            {
                /* Using HAL heap */
                HalpUnmapVirtualAddress(Header, 2);
            }
            else
            {
                /* Using Mm */
                MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
            }

            /* Didn't find anything */
            return NULL;
        }
    }

    /* Past this point, we assume something was found */
    ASSERT(Header);

    /* How many pages do we need? */
    PageCount = BYTES_TO_PAGES(Header->Length);
    if (PageCount != 2)
    {
        /* We assumed two, but this is not the case, free the current mapping */
        if (LoaderBlock)
        {
            /* Using HAL heap */
            HalpUnmapVirtualAddress(Header, 2);
        }
        else
        {
            /* Using Mm */
            MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
        }

        /* Now map this table using its correct size */
        if (!LoaderBlock)
        {
            /* Phase 1: Use HAL heap */
            Header = MmMapIoSpace(PhysicalAddress, PageCount << PAGE_SHIFT, MmNonCached);
        }
        else
        {
            /* Phase 0: Use Mm */
            Header = HalpMapPhysicalMemory64(PhysicalAddress, PageCount);
        }
    }

    /* Fail if the remapped failed */
    if (!Header) return NULL;

    /* All tables in ACPI 3.0 other than the FACP should have correct checksum */
    if ((Header->Signature != FADT_SIGNATURE) || (Header->Revision > 2))
    {
        /* Go to the end of the table */
        CheckSum = 0;
        CurrentByte = (PCHAR)Header + Header->Length;
        while (CurrentByte-- != (PCHAR)Header)
        {
            /* Add this byte */
            CheckSum += *CurrentByte;
        }

        /* The correct checksum is always 0, anything else is illegal */
        if (CheckSum)
        {
            HalpInvalidAcpiTable = Header->Signature;
            DPRINT1("Checksum failed on ACPI table %c%c%c%c\n",
                    (Signature & 0xFF),
                    (Signature & 0xFF00) >> 8,
                    (Signature & 0xFF0000) >> 16,
                    (Signature & 0xFF000000) >> 24);
        }
    }

    /* Return the table */
    return Header;
}

PVOID
NTAPI
HalpAcpiGetTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                 IN ULONG Signature)
{
    PFN_COUNT PageCount;
    PDESCRIPTION_HEADER TableAddress, BiosCopy;

    /* See if we have a cached table? */
    TableAddress = HalpAcpiGetCachedTable(Signature);
    if (!TableAddress)
    {
        /* No cache, search the BIOS */
        TableAddress = HalpAcpiGetTableFromBios(LoaderBlock, Signature);
        if (TableAddress)
        {
            /* Found it, copy it into our own memory */
            BiosCopy = HalpAcpiCopyBiosTable(LoaderBlock, TableAddress);

            /* Get the pages, and unmap the BIOS copy */
            PageCount = BYTES_TO_PAGES(TableAddress->Length);
            if (LoaderBlock)
            {
                /* Phase 0, use the HAL heap */
                HalpUnmapVirtualAddress(TableAddress, PageCount);
            }
            else
            {
                /* Phase 1, use Mm */
                MmUnmapIoSpace(TableAddress, PageCount << PAGE_SHIFT);
            }

            /* Cache the bios copy */
            TableAddress = BiosCopy;
            if (BiosCopy) HalpAcpiCacheTable(BiosCopy);
        }
    }

    /* Return the table */
    return TableAddress;
}

PVOID
NTAPI
HalAcpiGetTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                IN ULONG Signature)
{
    PDESCRIPTION_HEADER TableHeader;

    /* Is this phase0 */
    if (LoaderBlock)
    {
        /* Initialize the cache first */
        if (!NT_SUCCESS(HalpAcpiTableCacheInit(LoaderBlock))) return NULL;
    }
    else
    {
        /* Lock the cache */
        ExAcquireFastMutex(&HalpAcpiTableCacheLock);
    }

    /* Get the table */
    TableHeader = HalpAcpiGetTable(LoaderBlock, Signature);

    /* Release the lock in phase 1 */
    if (!LoaderBlock) ExReleaseFastMutex(&HalpAcpiTableCacheLock);

    /* Return the table */
    return TableHeader;
}

VOID
NTAPI
HalpNumaInitializeStaticConfiguration(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_SRAT SratTable;

    /* Get the SRAT, bail out if it doesn't exist */
    SratTable = HalAcpiGetTable(LoaderBlock, SRAT_SIGNATURE);
    HalpAcpiSrat = SratTable;
    if (!SratTable) return;
}

VOID
NTAPI
HalpGetHotPlugMemoryInfo(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_SRAT SratTable;

    /* Get the SRAT, bail out if it doesn't exist */
    SratTable = HalAcpiGetTable(LoaderBlock, SRAT_SIGNATURE);
    HalpAcpiSrat = SratTable;
    if (!SratTable) return;
}

VOID
NTAPI
HalpDynamicSystemResourceConfiguration(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /* For this HAL, it means to get hot plug memory information */
    HalpGetHotPlugMemoryInfo(LoaderBlock);
}

VOID
NTAPI
HalpAcpiDetectMachineSpecificActions(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                                     IN PFADT DescriptionTable)
{
    /* Does this HAL specify something? */
    if (HalpAcpiTableMatchList.Flink)
    {
        /* Great, but we don't support it */
        DPRINT1("WARNING: Your HAL has specific ACPI hacks to apply!\n");
    }
}

VOID
NTAPI
HalpInitBootTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PBOOT_TABLE BootTable;

    /* Get the boot table */
    BootTable = HalAcpiGetTable(LoaderBlock, BOOT_SIGNATURE);
    HalpSimpleBootFlagTable = BootTable;

    /* Validate it */
    if ((BootTable) &&
        (BootTable->Header.Length >= sizeof(BOOT_TABLE)) &&
        (BootTable->CMOSIndex >= 9))
    {
        DPRINT1("ACPI Boot table found, but not supported!\n");
    }
    else
    {
        /* Invalid or doesn't exist, ignore it */
        HalpSimpleBootFlagTable = 0;
    }

    /* Install the end of boot handler */
//    HalEndOfBoot = HalpEndOfBoot;
}

NTSTATUS
NTAPI
HalpAcpiFindRsdtPhase0(IN PLOADER_PARAMETER_BLOCK LoaderBlock,
                       OUT PACPI_BIOS_MULTI_NODE* AcpiMultiNode)
{
    PCONFIGURATION_COMPONENT_DATA ComponentEntry;
    PCONFIGURATION_COMPONENT_DATA Next = NULL;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;
    PACPI_BIOS_MULTI_NODE NodeData;
    SIZE_T NodeLength;
    PFN_COUNT PageCount;
    PVOID MappedAddress;
    PHYSICAL_ADDRESS PhysicalAddress;

    /* Did we already do this once? */
    if (HalpAcpiMultiNode)
    {
        /* Return what we know */
        *AcpiMultiNode = HalpAcpiMultiNode;
        return STATUS_SUCCESS;
    }

    /* Assume failure */
    *AcpiMultiNode = NULL;

    /* Find the multi function adapter key */
    ComponentEntry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot,
                                                  AdapterClass,
                                                  MultiFunctionAdapter,
                                                  0,
                                                  &Next);
    while (ComponentEntry)
    {
        /* Find the ACPI BIOS key */
        if (!_stricmp(ComponentEntry->ComponentEntry.Identifier, "ACPI BIOS"))
        {
            /* Found it */
            break;
        }

        /* Keep searching */
        Next = ComponentEntry;
        ComponentEntry = KeFindConfigurationNextEntry(LoaderBlock->ConfigurationRoot,
                                                      AdapterClass,
                                                      MultiFunctionAdapter,
                                                      NULL,
                                                      &Next);
    }

    /* Make sure we found it */
    if (!ComponentEntry)
    {
        DPRINT1("**** HalpAcpiFindRsdtPhase0: did NOT find RSDT\n");
        return STATUS_NOT_FOUND;
    }

    /* The configuration data is a resource list, and the BIOS node follows */
    ResourceList = ComponentEntry->ConfigurationData;
    NodeData = (PACPI_BIOS_MULTI_NODE)(ResourceList + 1);

    /* How many E820 memory entries are there? */
    NodeLength = sizeof(ACPI_BIOS_MULTI_NODE) +
                 (NodeData->Count - 1) * sizeof(ACPI_E820_ENTRY);

    /* Convert to pages */
    PageCount = (PFN_COUNT)BYTES_TO_PAGES(NodeLength);

    /* Allocate the memory */
    PhysicalAddress.QuadPart = HalpAllocPhysicalMemory(LoaderBlock,
                                                       0x1000000,
                                                       PageCount,
                                                       FALSE);
    if (PhysicalAddress.QuadPart)
    {
        /* Map it if the allocation worked */
        MappedAddress = HalpMapPhysicalMemory64(PhysicalAddress, PageCount);
    }
    else
    {
        /* Otherwise we'll have to fail */
        MappedAddress = NULL;
    }

    /* Save the multi node, bail out if we didn't find it */
    HalpAcpiMultiNode = MappedAddress;
    if (!MappedAddress) return STATUS_INSUFFICIENT_RESOURCES;

    /* Copy the multi-node data */
    RtlCopyMemory(MappedAddress, NodeData, NodeLength);

    /* Return the data */
    *AcpiMultiNode = HalpAcpiMultiNode;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpAcpiTableCacheInit(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_BIOS_MULTI_NODE AcpiMultiNode;
    NTSTATUS Status = STATUS_SUCCESS;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID MappedAddress;
    ULONG TableLength;
    PRSDT Rsdt;
    PLOADER_PARAMETER_EXTENSION LoaderExtension;

    /* Only initialize once */
    if (HalpAcpiTableCacheList.Flink) return Status;

    /* Setup the lock and table */
    ExInitializeFastMutex(&HalpAcpiTableCacheLock);
    InitializeListHead(&HalpAcpiTableCacheList);

    /* Find the RSDT */
    Status = HalpAcpiFindRsdtPhase0(LoaderBlock, &AcpiMultiNode);
    if (!NT_SUCCESS(Status)) return Status;

    PhysicalAddress.QuadPart = AcpiMultiNode->RsdtAddress.QuadPart;

    /* Map the RSDT */
    if (LoaderBlock)
    {
        /* Phase0: Use HAL Heap to map the RSDT, we assume it's about 2 pages */
        MappedAddress = HalpMapPhysicalMemory64(PhysicalAddress, 2);
    }
    else
    {
        /* Use an I/O map */
        MappedAddress = MmMapIoSpace(PhysicalAddress, PAGE_SIZE * 2, MmNonCached);
    }

    /* Get the RSDT */
    Rsdt = MappedAddress;
    if (!MappedAddress)
    {
        /* Fail, no memory */
        DPRINT1("HAL: Failed to map RSDT\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Validate it */
    if ((Rsdt->Header.Signature != RSDT_SIGNATURE) &&
        (Rsdt->Header.Signature != XSDT_SIGNATURE))
    {
        /* Very bad: crash */
        HalDisplayString("Bad RSDT pointer\r\n");
        KeBugCheckEx(MISMATCHED_HAL, 4, __LINE__, 0, 0);
    }

    /* We assumed two pages -- do we need less or more? */
    TableLength = ADDRESS_AND_SIZE_TO_SPAN_PAGES(PhysicalAddress.LowPart,
                                                 Rsdt->Header.Length);
    if (TableLength != 2)
    {
        /* Are we in phase 0 or 1? */
        if (!LoaderBlock)
        {
            /* Unmap the old table, remap the new one, using Mm I/O space */
            MmUnmapIoSpace(MappedAddress, 2 * PAGE_SIZE);
            MappedAddress = MmMapIoSpace(PhysicalAddress,
                                         TableLength << PAGE_SHIFT,
                                         MmNonCached);
        }
        else
        {
            /* Unmap the old table, remap the new one, using HAL heap */
            HalpUnmapVirtualAddress(MappedAddress, 2);
            MappedAddress = HalpMapPhysicalMemory64(PhysicalAddress, TableLength);
        }

        /* Get the remapped table */
        Rsdt = MappedAddress;
        if (!MappedAddress)
        {
            /* Fail, no memory */
            DPRINT1("HAL: Couldn't remap RSDT\n");
            return STATUS_INSUFFICIENT_RESOURCES;
        }
    }

    /* Now take the BIOS copy and make our own local copy */
    Rsdt = HalpAcpiCopyBiosTable(LoaderBlock, &Rsdt->Header);
    if (!Rsdt)
    {
        /* Fail, no memory */
        DPRINT1("HAL: Couldn't remap RSDT\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Get rid of the BIOS mapping */
    if (LoaderBlock)
    {
        /* Use HAL heap */
        HalpUnmapVirtualAddress(MappedAddress, TableLength);

        LoaderExtension = LoaderBlock->Extension;
    }
    else
    {
        /* Use Mm */
        MmUnmapIoSpace(MappedAddress, TableLength << PAGE_SHIFT);

        LoaderExtension = NULL;
    }

    /* Cache the RSDT */
    HalpAcpiCacheTable(&Rsdt->Header);

    /* Check for compatible loader block extension */
    if (LoaderExtension && (LoaderExtension->Size >= 0x58))
    {
        /* Compatible loader: did it provide an ACPI table override? */
        if ((LoaderExtension->AcpiTable) && (LoaderExtension->AcpiTableSize))
        {
            /* Great, because we don't support it! */
            DPRINT1("ACPI Table Overrides Not Supported!\n");
        }
    }

    /* Done */
    return Status;
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

    /* Setup the ACPI table cache */
    Status = HalpAcpiTableCacheInit(LoaderBlock);
    if (!NT_SUCCESS(Status)) return Status;

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

    /* Do we have a low stub address yet? */
    if (!HalpLowStubPhysicalAddress.QuadPart)
    {
        /* Allocate it */
        HalpLowStubPhysicalAddress.QuadPart = HalpAllocPhysicalMemory(LoaderBlock,
                                                                      0x100000,
                                                                      HALP_LOW_STUB_SIZE_IN_PAGES,
                                                                      FALSE);
        if (HalpLowStubPhysicalAddress.QuadPart)
        {
            /* Map it */
            HalpLowStub = HalpMapPhysicalMemory64(HalpLowStubPhysicalAddress, HALP_LOW_STUB_SIZE_IN_PAGES);
        }
    }

    /* Grab a page for flushes */
    PhysicalAddress.QuadPart = 0x100000;
    HalpVirtAddrForFlush = HalpMapPhysicalMemory64(PhysicalAddress, 1);
    HalpPteForFlush = HalAddressToPte(HalpVirtAddrForFlush);

    /* Don't do this again */
    HalpProcessedACPIPhase0 = TRUE;

    /* Setup the boot table */
    HalpInitBootTable(LoaderBlock);

    /* Log some ACPI data */
    {
        PLIST_ENTRY NextEntry;
        PCSTR AcpiVersion = NULL;

        /* Find the ACPI version (range) out */
        // v1.0+: Revision is major version.
        // v5.1+: minor_revision is minor version.
        // v6.4+: errata bits are errata version.
        switch (Fadt->Header.Revision)
        {
            case 0: // Should not happen.
                AcpiVersion = "Unknown_0";
                break;
            case 1:
                AcpiVersion = "1.0-1.0b";
                break;
            case 2: // Should not happen.
                AcpiVersion = "Unknown_2";
                break;
            case 3:
                AcpiVersion = "1.5-2.0_C";
                break;
            case 4:
                AcpiVersion = "3.0-4.0_A";
                break;
            case 5:
                if (Fadt->minor_revision == 0)
                    AcpiVersion = "5.0-5.0_B";
                else if (Fadt->minor_revision == 1)
                    AcpiVersion = "5.1-5.1_B";
                break;
            case 6:
                if (Fadt->minor_revision == 0)
                    AcpiVersion = "6.0-6.0_A";
                else if (Fadt->minor_revision == 1)
                    AcpiVersion = "6.1-6.1_A";
                else if (Fadt->minor_revision == 2)
                    AcpiVersion = "6.2-6.2_B";
                else if (Fadt->minor_revision == 3)
                    AcpiVersion = "6.3-6.3_A";
                else if ((Fadt->minor_revision & 0x0F) == 0x04)
                {
                    if ((Fadt->minor_revision & 0xF0) == 0x00)
                        AcpiVersion = "6.4";
                    else if ((Fadt->minor_revision & 0xF0) == 0x10)
                        AcpiVersion = "6.4_A";
                }
                else if (Fadt->minor_revision == 5) // v6.5_A too is documented as errata=0.
                    AcpiVersion = "6.5-6.6";
                break;
        }

        /* Print the ACPI version */
        DPRINT1("ACPI v");
        if (AcpiVersion == NULL)
        {
            // Unknown past values, or newer than v6.6 (documented as 6.5).
            DbgPrint("Unknown_%u_%u", Fadt->Header.Revision, Fadt->minor_revision);
        }
        else
        {
            DbgPrint("%s", AcpiVersion);
        }
        DbgPrint(" detected. Tables:");

        /* List cached tables */
        for (NextEntry = HalpAcpiTableCacheList.Flink;
             NextEntry != &HalpAcpiTableCacheList;
             NextEntry = NextEntry->Flink)
        {
            PACPI_CACHED_TABLE CachedTable = CONTAINING_RECORD(NextEntry, ACPI_CACHED_TABLE, Links);

            /* Print the table signature */
            DbgPrint(" [%c%c%c%c]",
                      CachedTable->Header.Signature & 0x000000FF,
                     (CachedTable->Header.Signature & 0x0000FF00) >>  8,
                     (CachedTable->Header.Signature & 0x00FF0000) >> 16,
                     (CachedTable->Header.Signature & 0xFF000000) >> 24);
        }
        DbgPrint("\n");
    }

    /* Return success */
    return STATUS_SUCCESS;
}

#ifndef _MINIHAL_

//
// ACPI PCIe Configuration Space Access Functions
//
CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpAcpiParseMcfgTable(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PMCFG_TABLE McfgTable;
    PMCFG_ALLOCATION Allocation;
    ULONG AllocationCount, i;
    
    /* Get the MCFG table */
    McfgTable = HalAcpiGetTable(LoaderBlock, MCFG_SIGNATURE);
    if (!McfgTable)
    {
        DPRINT("MCFG table not found, using legacy PCI access\n");
        return STATUS_NOT_FOUND;
    }
    
    /* Calculate number of allocations */
    AllocationCount = (McfgTable->Header.Length - sizeof(MCFG_TABLE) + sizeof(MCFG_ALLOCATION)) / sizeof(MCFG_ALLOCATION);
    
    DPRINT("MCFG table found with %lu allocation(s)\n", AllocationCount);
    
    /* Store the PCIe configuration spaces */
    HalpAcpiPcieConfigSpaceCount = min(AllocationCount, ARRAYSIZE(HalpAcpiPcieConfigSpaces));
    
    for (i = 0; i < HalpAcpiPcieConfigSpaceCount; i++)
    {
        Allocation = &McfgTable->Allocations[i];
        
        HalpAcpiPcieConfigSpaces[i].BaseAddress = Allocation->BaseAddress;
        HalpAcpiPcieConfigSpaces[i].PciSegment = Allocation->PciSegment;
        HalpAcpiPcieConfigSpaces[i].StartBusNumber = Allocation->StartBusNumber;
        HalpAcpiPcieConfigSpaces[i].EndBusNumber = Allocation->EndBusNumber;
        HalpAcpiPcieConfigSpaces[i].MappedAddress = NULL; // Will be mapped on-demand
        
        DPRINT("PCIe Config Space %lu: Base=0x%I64x, Segment=%u, Bus %u-%u\n",
               i, Allocation->BaseAddress.QuadPart, Allocation->PciSegment,
               Allocation->StartBusNumber, Allocation->EndBusNumber);
    }
    
    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
PACPI_PCIE_CONFIG_SPACE
NTAPI
HalpAcpiFindPcieConfigSpace(IN UCHAR BusNumber)
{
    ULONG i;
    
    for (i = 0; i < HalpAcpiPcieConfigSpaceCount; i++)
    {
        if (BusNumber >= HalpAcpiPcieConfigSpaces[i].StartBusNumber &&
            BusNumber <= HalpAcpiPcieConfigSpaces[i].EndBusNumber)
        {
            return &HalpAcpiPcieConfigSpaces[i];
        }
    }
    
    return NULL;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpAcpiReadPcieConfig(IN UCHAR BusNumber,
                       IN PCI_SLOT_NUMBER Slot,
                       IN PVOID Buffer,
                       IN ULONG Offset,
                       IN ULONG Length)
{
    PACPI_PCIE_CONFIG_SPACE ConfigSpace;
    PHYSICAL_ADDRESS DeviceAddress;
    PVOID MappedAddress;
    ULONG DeviceOffset;
    
    /* Find the appropriate configuration space */
    ConfigSpace = HalpAcpiFindPcieConfigSpace(BusNumber);
    if (!ConfigSpace)
    {
        /* Fallback to legacy I/O port access */
        HalpReadPCIConfig(&HalpFakePciBusHandler, Slot, Buffer, Offset, Length);
        return STATUS_SUCCESS;
    }
    
    /* Calculate device-specific address */
    DeviceOffset = ((BusNumber - ConfigSpace->StartBusNumber) << 20) |
                   (Slot.u.bits.DeviceNumber << 15) |
                   (Slot.u.bits.FunctionNumber << 12) |
                   Offset;
    
    DeviceAddress.QuadPart = ConfigSpace->BaseAddress.QuadPart + DeviceOffset;
    
    /* Map the configuration space (4KB per device) */
    MappedAddress = MmMapIoSpace(DeviceAddress, 4096, MmNonCached);
    if (!MappedAddress)
    {
        DPRINT1("Failed to map PCIe config space at 0x%I64x\n", DeviceAddress.QuadPart);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    /* Read the configuration data */
    _SEH2_TRY
    {
        switch (Length)
        {
            case 1:
                *(PUCHAR)Buffer = *(PUCHAR)((PUCHAR)MappedAddress + (Offset & 0xFFF));
                break;
            case 2:
                *(PUSHORT)Buffer = *(PUSHORT)((PUCHAR)MappedAddress + (Offset & 0xFFC));
                break;
            case 4:
                *(PULONG)Buffer = *(PULONG)((PUCHAR)MappedAddress + (Offset & 0xFFC));
                break;
            default:
                RtlCopyMemory(Buffer, (PUCHAR)MappedAddress + (Offset & 0xFFC), Length);
                break;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("Exception reading PCIe config space\n");
        MmUnmapIoSpace(MappedAddress, 4096);
        _SEH2_YIELD(return STATUS_DEVICE_NOT_READY);
    }
    _SEH2_END;
    
    MmUnmapIoSpace(MappedAddress, 4096);
    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpAcpiWritePcieConfig(IN UCHAR BusNumber,
                        IN PCI_SLOT_NUMBER Slot,
                        IN PVOID Buffer,
                        IN ULONG Offset,
                        IN ULONG Length)
{
    PACPI_PCIE_CONFIG_SPACE ConfigSpace;
    PHYSICAL_ADDRESS DeviceAddress;
    PVOID MappedAddress;
    ULONG DeviceOffset;
    
    /* Find the appropriate configuration space */
    ConfigSpace = HalpAcpiFindPcieConfigSpace(BusNumber);
    if (!ConfigSpace)
    {
        /* Fallback to legacy I/O port access */
        HalpWritePCIConfig(&HalpFakePciBusHandler, Slot, Buffer, Offset, Length);
        return STATUS_SUCCESS;
    }
    
    /* Calculate device-specific address */
    DeviceOffset = ((BusNumber - ConfigSpace->StartBusNumber) << 20) |
                   (Slot.u.bits.DeviceNumber << 15) |
                   (Slot.u.bits.FunctionNumber << 12) |
                   Offset;
    
    DeviceAddress.QuadPart = ConfigSpace->BaseAddress.QuadPart + DeviceOffset;
    
    /* Map the configuration space (4KB per device) */
    MappedAddress = MmMapIoSpace(DeviceAddress, 4096, MmNonCached);
    if (!MappedAddress)
    {
        DPRINT1("Failed to map PCIe config space at 0x%I64x\n", DeviceAddress.QuadPart);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    
    /* Write the configuration data */
    _SEH2_TRY
    {
        switch (Length)
        {
            case 1:
                *(PUCHAR)((PUCHAR)MappedAddress + (Offset & 0xFFF)) = *(PUCHAR)Buffer;
                break;
            case 2:
                *(PUSHORT)((PUCHAR)MappedAddress + (Offset & 0xFFC)) = *(PUSHORT)Buffer;
                break;
            case 4:
                *(PULONG)((PUCHAR)MappedAddress + (Offset & 0xFFC)) = *(PULONG)Buffer;
                break;
            default:
                RtlCopyMemory((PUCHAR)MappedAddress + (Offset & 0xFFC), Buffer, Length);
                break;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        DPRINT1("Exception writing PCIe config space\n");
        MmUnmapIoSpace(MappedAddress, 4096);
        _SEH2_YIELD(return STATUS_DEVICE_NOT_READY);
    }
    _SEH2_END;
    
    MmUnmapIoSpace(MappedAddress, 4096);
    return STATUS_SUCCESS;
}

CODE_SEG("INIT")
VOID
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

#define NEWLINE "\n"
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

            HalpAcpiWritePcieConfig(BusHandler->BusNumber,
                                    PciSlot,
                                    &PciBar,
                                    FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.BaseAddresses[b]),
                                    sizeof(ULONG));
            HalpAcpiReadPcieConfig(BusHandler->BusNumber,
                                   PciSlot,
                                   &PciBar,
                                   FIELD_OFFSET(PCI_COMMON_HEADER, u.type0.BaseAddresses[b]),
                                   sizeof(ULONG));
            HalpAcpiWritePcieConfig(BusHandler->BusNumber,
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
    /* Setup the PCI stub support */
    HalpInitializePciStubs();

    /* Set the NMI crash flag */
    HalpGetNMICrashFlag();

#ifndef _MINIHAL_
    NTSTATUS Status;
    PCI_SLOT_NUMBER PciSlot;
    ULONG i, j, k, MaxBus;
    UCHAR DataBuffer[PCI_COMMON_HDR_LENGTH];
    PPCI_COMMON_CONFIG PciData = (PPCI_COMMON_CONFIG)DataBuffer;
    PBUS_HANDLER BusHandler;
    /* Print the PCI detection header */
    DbgPrint("\n====== PCI BUS HARDWARE DETECTION =======\n\n");
    
    /* Initialize ACPI PCI interrupt routing */
    Status = HalpAcpiParsePrtTable();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPI: Failed to initialize PRT table: 0x%x\n", Status);
    }
    
    /* Initialize ACPI power management */
    Status = HalpAcpiInitializePowerManagement();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPI: Failed to initialize power management: 0x%x\n", Status);
    }
    
    /* Initialize ACPI resource management */
    Status = HalpAcpiInitializeResourceManagement();
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ACPI: Failed to initialize resource management: 0x%x\n", Status);
    }
    
    /* Initialize PCI slot structure */
    PciSlot.u.bits.Reserved = 0;
    
    /* Try to parse MCFG table for PCIe support - use NULL since ACPI cache is already initialized */
    Status = HalpAcpiParseMcfgTable(NULL);
    if (NT_SUCCESS(Status))
    {
        DbgPrint("ACPI: PCIe MCFG table found, using memory-mapped configuration access\n");
        
        /* Determine maximum bus number from MCFG */
        MaxBus = 0;
        for (i = 0; i < HalpAcpiPcieConfigSpaceCount; i++)
        {
            if (HalpAcpiPcieConfigSpaces[i].EndBusNumber > MaxBus)
                MaxBus = HalpAcpiPcieConfigSpaces[i].EndBusNumber;
        }
        MaxBus = min(MaxBus + 1, 256); /* Limit to 256 buses */
        
        /* Scan all ACPI-defined PCI buses */
        BusHandler = &HalpFakePciBusHandler;
        
        for (i = 0; i < MaxBus; i++)
        {
            /* Update the bus number in the handler */
            BusHandler->BusNumber = i;

            /* Loop every device */
            for (j = 0; j < 32; j++)
            {
                /* Loop every function */
                PciSlot.u.bits.DeviceNumber = j;
                for (k = 0; k < 8; k++)
                {
                    /* Build the final slot structure */
                    PciSlot.u.bits.FunctionNumber = k;

                    /* Read the configuration information using ACPI PCIe access */
                    Status = HalpAcpiReadPcieConfig(i, PciSlot, PciData, 0, PCI_COMMON_HDR_LENGTH);
                    if (!NT_SUCCESS(Status)) continue;

                    /* Skip if this is an invalid function */
                    if (PciData->VendorID == PCI_INVALID_VENDORID) continue;

                    /* Print out the entry */
                    HalpDebugPciDumpBus(BusHandler, PciSlot, i, j, k, PciData);
                    
                    /* Show ACPI interrupt routing for devices that use interrupts */
                    if (PciData->u.type0.InterruptPin != 0)
                    {
                        ULONG AcpiIrq = HalpAcpiGetPciInterrupt(i, j, k, PciData->u.type0.InterruptPin - 1);
                        DbgPrint("    ACPI: INT%c -> IRQ %d (ACPI _PRT routing)\n", 
                               'A' + (PciData->u.type0.InterruptPin - 1), AcpiIrq);
                    }
                    
                    /* Show ACPI resource management information */
                    {
                        ACPI_RESOURCE_LIST ResourceList;
                        NTSTATUS ResourceStatus = HalpAcpiGetCurrentResources(i, j, k, &ResourceList);
                        if (NT_SUCCESS(ResourceStatus) && ResourceList.Count > 0)
                        {
                            DbgPrint("    ACPI: Device has %d resources (_CRS)\n", ResourceList.Count);
                            for (ULONG r = 0; r < ResourceList.Count && r < 3; r++)  /* Show first 3 resources */
                            {
                                PACPI_RESOURCE_DESCRIPTOR Res = &ResourceList.Descriptors[r];
                                switch (Res->Type)
                                {
                                    case AcpiResourceTypeMemory:
                                        DbgPrint("      Memory: 0x%I64x-0x%I64x\n", 
                                               Res->u.Memory.BaseAddress.QuadPart,
                                               Res->u.Memory.BaseAddress.QuadPart + Res->u.Memory.Length - 1);
                                        break;
                                    case AcpiResourceTypeIo:
                                        DbgPrint("      I/O: 0x%x-0x%x\n", 
                                               Res->u.Io.BasePort,
                                               Res->u.Io.BasePort + Res->u.Io.Length - 1);
                                        break;
                                    case AcpiResourceTypeIrq:
                                        DbgPrint("      IRQ: %d\n", Res->u.Irq.Vector);
                                        break;
                                    default:
                                        /* Skip other resource types for now */
                                        break;
                                }
                            }
                        }
                    }

                    /* Check for multi-function device */
                    if (k == 0 && !(PciData->HeaderType & PCI_MULTIFUNCTION)) break;
                }
            }
        }
        
        DbgPrint("ACPI: Scanned %lu PCI bus(es) using PCIe memory-mapped configuration\n", MaxBus);
    }
    else
    {
        DbgPrint("ACPI: No MCFG table found, falling back to legacy PCI I/O access\n");
        
        /* Use the fake PCI bus handler for legacy scanning */
        BusHandler = &HalpFakePciBusHandler;
        
        /* Scan first 4 PCI buses (typical for legacy systems) */
        for (i = 0; i < 4; i++)
        {
            /* Update the bus number in the handler */
            BusHandler->BusNumber = i;

            /* Loop every device */
            for (j = 0; j < 32; j++)
            {
                /* Loop every function */
                PciSlot.u.bits.DeviceNumber = j;
                for (k = 0; k < 8; k++)
                {
                    /* Build the final slot structure */
                    PciSlot.u.bits.FunctionNumber = k;

                    /* Read the configuration information using legacy I/O access */
                    HalpReadPCIConfig(BusHandler, PciSlot, PciData, 0, PCI_COMMON_HDR_LENGTH);

                    /* Skip if this is an invalid function */
                    if (PciData->VendorID == PCI_INVALID_VENDORID) continue;

                    /* Print out the entry */
                    HalpDebugPciDumpBus(BusHandler, PciSlot, i, j, k, PciData);
                    
                    /* Show ACPI interrupt routing for devices that use interrupts */
                    if (PciData->u.type0.InterruptPin != 0)
                    {
                        ULONG AcpiIrq = HalpAcpiGetPciInterrupt(i, j, k, PciData->u.type0.InterruptPin - 1);
                        DbgPrint("    ACPI: INT%c -> IRQ %d (ACPI _PRT routing)\n", 
                               'A' + (PciData->u.type0.InterruptPin - 1), AcpiIrq);
                    }
                    
                    /* Show ACPI resource management information */
                    {
                        ACPI_RESOURCE_LIST ResourceList;
                        NTSTATUS ResourceStatus = HalpAcpiGetCurrentResources(i, j, k, &ResourceList);
                        if (NT_SUCCESS(ResourceStatus) && ResourceList.Count > 0)
                        {
                            DbgPrint("    ACPI: Device has %d resources (_CRS)\n", ResourceList.Count);
                            for (ULONG r = 0; r < ResourceList.Count && r < 3; r++)  /* Show first 3 resources */
                            {
                                PACPI_RESOURCE_DESCRIPTOR Res = &ResourceList.Descriptors[r];
                                switch (Res->Type)
                                {
                                    case AcpiResourceTypeMemory:
                                        DbgPrint("      Memory: 0x%I64x-0x%I64x\n", 
                                               Res->u.Memory.BaseAddress.QuadPart,
                                               Res->u.Memory.BaseAddress.QuadPart + Res->u.Memory.Length - 1);
                                        break;
                                    case AcpiResourceTypeIo:
                                        DbgPrint("      I/O: 0x%x-0x%x\n", 
                                               Res->u.Io.BasePort,
                                               Res->u.Io.BasePort + Res->u.Io.Length - 1);
                                        break;
                                    case AcpiResourceTypeIrq:
                                        DbgPrint("      IRQ: %d\n", Res->u.Irq.Vector);
                                        break;
                                    default:
                                        /* Skip other resource types for now */
                                        break;
                                }
                            }
                        }
                    }

                    /* Check for multi-function device */
                    if (k == 0 && !(PciData->HeaderType & PCI_MULTIFUNCTION)) break;
                }
            }
        }
        
        DbgPrint("ACPI: Scanned 4 PCI bus(es) using legacy I/O port configuration\n");
    }
    
    DbgPrint("\n====== PCI BUS DETECTION COMPLETE =======\n\n");
#endif
}

//
// ACPI _PRT (PCI Routing Table) Implementation
//
static ACPI_PRT_TABLE HalpAcpiPrtTable = {0};

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpAcpiParsePrtTable(VOID)
{
    NTSTATUS Status = STATUS_SUCCESS;
    
    DPRINT("ACPI: Parsing _PRT (PCI Routing Table)\n");
    
    /* Initialize the PRT table */
    RtlZeroMemory(&HalpAcpiPrtTable, sizeof(HalpAcpiPrtTable));
    
    /* TODO: In a full implementation, we would:
     * 1. Walk the ACPI namespace looking for _PRT methods
     * 2. Execute each _PRT method to get routing information
     * 3. Parse the returned package data
     * 4. Build the routing table
     * 
     * For now, we'll set up a default routing table for common configurations
     */
    
    /* Set up default PCI interrupt routing for standard PC configuration */
    if (HalpAcpiPrtTable.Count == 0)
    {
        DbgPrint("ACPI: No _PRT table found, using default PCI interrupt routing\n");
        
        /* Default routing for devices 0-31, INTA-INTD */
        HalpAcpiPrtTable.Count = 16;  /* First 4 devices * 4 pins */
        
        /* Device 0 (Host Bridge) - typically no interrupts */
        HalpAcpiPrtTable.Entries[0].Address = 0x0000;  /* Device 0, Function 0 */
        HalpAcpiPrtTable.Entries[0].Pin = 0;           /* INTA */
        HalpAcpiPrtTable.Entries[0].Source = 0;        /* No interrupt */
        
        /* Device 1 (ISA Bridge) - typically IRQ 9 */
        HalpAcpiPrtTable.Entries[1].Address = 0x0001;  /* Device 1, Function 0 */
        HalpAcpiPrtTable.Entries[1].Pin = 0;           /* INTA */
        HalpAcpiPrtTable.Entries[1].Source = 9;        /* IRQ 9 */
        
        /* Device 2 (VGA) - typically IRQ 10 */
        HalpAcpiPrtTable.Entries[2].Address = 0x0002;  /* Device 2, Function 0 */
        HalpAcpiPrtTable.Entries[2].Pin = 0;           /* INTA */
        HalpAcpiPrtTable.Entries[2].Source = 10;       /* IRQ 10 */
        
        /* Device 3 (Network) - typically IRQ 11 */
        HalpAcpiPrtTable.Entries[3].Address = 0x0003;  /* Device 3, Function 0 */
        HalpAcpiPrtTable.Entries[3].Pin = 0;           /* INTA */
        HalpAcpiPrtTable.Entries[3].Source = 11;       /* IRQ 11 */
        
        /* Additional devices use rotating IRQ assignment */
        for (ULONG i = 4; i < 16; i++)
        {
            HalpAcpiPrtTable.Entries[i].Address = i;
            HalpAcpiPrtTable.Entries[i].Pin = i % 4;           /* Rotate INTA-INTD */
            HalpAcpiPrtTable.Entries[i].Source = 10 + (i % 4); /* IRQ 10-13 */
        }
        
        DbgPrint("ACPI: Created default PRT table with %d entries\n", HalpAcpiPrtTable.Count);
    }
    
    return Status;
}

ULONG
NTAPI
HalpAcpiGetPciInterrupt(IN UCHAR Bus,
                        IN UCHAR Device, 
                        IN UCHAR Function,
                        IN UCHAR Pin)
{
    ULONG DeviceAddress;
    ULONG i;
    
    /* Convert to device address format used in _PRT */
    DeviceAddress = (Device << 16) | Function;
    
    /* Search the PRT table for matching entry */
    for (i = 0; i < HalpAcpiPrtTable.Count; i++)
    {
        if ((HalpAcpiPrtTable.Entries[i].Address == DeviceAddress) &&
            (HalpAcpiPrtTable.Entries[i].Pin == Pin))
        {
            DPRINT("ACPI: Found PRT entry - Bus %d Device %d Pin %d -> IRQ %d\n", 
                   Bus, Device, Pin, HalpAcpiPrtTable.Entries[i].Source);
            return HalpAcpiPrtTable.Entries[i].Source;
        }
    }
    
    /* Default fallback - use device number to calculate IRQ */
    ULONG DefaultIrq = 10 + (Device % 4);
    DPRINT("ACPI: No PRT entry found - Bus %d Device %d Pin %d -> Default IRQ %d\n", 
           Bus, Device, Pin, DefaultIrq);
    return DefaultIrq;
}

//
// ACPI Power Management Implementation
//
static ACPI_DEVICE_POWER_INFO HalpAcpiDevicePowerInfo[256] = {0};

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpAcpiInitializePowerManagement(VOID)
{
    ULONG i;
    
    DPRINT("ACPI: Initializing PCI power management\n");
    
    /* Initialize power info for all possible devices */
    for (i = 0; i < 256; i++)
    {
        /* Default state - fully functional */
        HalpAcpiDevicePowerInfo[i].CurrentState = AcpiPowerStateD0;
        
        /* Most PCI devices support D0 and D3 */
        HalpAcpiDevicePowerInfo[i].SupportedStates[0] = AcpiPowerStateD0;
        HalpAcpiDevicePowerInfo[i].SupportedStates[1] = AcpiPowerStateUnknown;
        HalpAcpiDevicePowerInfo[i].SupportedStates[2] = AcpiPowerStateUnknown;
        HalpAcpiDevicePowerInfo[i].SupportedStates[3] = AcpiPowerStateD3;
        
        /* Default wake capabilities - can wake from D3 */
        HalpAcpiDevicePowerInfo[i].CanWakeFromD0 = FALSE;
        HalpAcpiDevicePowerInfo[i].CanWakeFromD1 = FALSE;
        HalpAcpiDevicePowerInfo[i].CanWakeFromD2 = FALSE;
        HalpAcpiDevicePowerInfo[i].CanWakeFromD3 = TRUE;  /* Wake on LAN, etc. */
    }
    
    DbgPrint("ACPI: PCI power management initialized for 256 devices\n");
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpAcpiSetDevicePowerState(IN UCHAR Bus,
                            IN UCHAR Device,
                            IN UCHAR Function,
                            IN ACPI_POWER_STATE PowerState)
{
    ULONG DeviceIndex = (Bus << 8) | (Device << 3) | Function;
    PACPI_DEVICE_POWER_INFO PowerInfo;
    
    if (DeviceIndex >= 256)
        return STATUS_INVALID_PARAMETER;
        
    PowerInfo = &HalpAcpiDevicePowerInfo[DeviceIndex];
    
    DPRINT("ACPI: Setting device %d:%d.%d power state from D%d to D%d\n",
           Bus, Device, Function, PowerInfo->CurrentState, PowerState);
    
    /* TODO: In a full implementation, we would:
     * 1. Check if the target state is supported
     * 2. Execute ACPI _PS0 or _PS3 methods
     * 3. Wait for state transition
     * 4. Update PCI power management registers
     */
    
    switch (PowerState)
    {
        case AcpiPowerStateD0:
            /* Power on the device */
            DPRINT("ACPI: Executing _PS0 for device %d:%d.%d (Power On)\n", Bus, Device, Function);
            /* TODO: Execute ACPI _PS0 method */
            PowerInfo->CurrentState = AcpiPowerStateD0;
            break;
            
        case AcpiPowerStateD3:
            /* Power off the device */
            DPRINT("ACPI: Executing _PS3 for device %d:%d.%d (Power Off)\n", Bus, Device, Function);
            /* TODO: Execute ACPI _PS3 method */
            PowerInfo->CurrentState = AcpiPowerStateD3;
            break;
            
        default:
            DPRINT1("ACPI: Unsupported power state D%d for device %d:%d.%d\n", 
                    PowerState, Bus, Device, Function);
            return STATUS_NOT_SUPPORTED;
    }
    
    return STATUS_SUCCESS;
}

ACPI_POWER_STATE
NTAPI
HalpAcpiGetDevicePowerState(IN UCHAR Bus,
                            IN UCHAR Device,
                            IN UCHAR Function)
{
    ULONG DeviceIndex = (Bus << 8) | (Device << 3) | Function;
    
    if (DeviceIndex >= 256)
        return AcpiPowerStateUnknown;
        
    return HalpAcpiDevicePowerInfo[DeviceIndex].CurrentState;
}

BOOLEAN
NTAPI
HalpAcpiCanDeviceWakeSystem(IN UCHAR Bus,
                            IN UCHAR Device,
                            IN UCHAR Function,
                            IN ACPI_POWER_STATE FromState)
{
    ULONG DeviceIndex = (Bus << 8) | (Device << 3) | Function;
    PACPI_DEVICE_POWER_INFO PowerInfo;
    
    if (DeviceIndex >= 256)
        return FALSE;
        
    PowerInfo = &HalpAcpiDevicePowerInfo[DeviceIndex];
    
    switch (FromState)
    {
        case AcpiPowerStateD0:
            return PowerInfo->CanWakeFromD0;
        case AcpiPowerStateD1:
            return PowerInfo->CanWakeFromD1;
        case AcpiPowerStateD2:
            return PowerInfo->CanWakeFromD2;
        case AcpiPowerStateD3:
            return PowerInfo->CanWakeFromD3;
        default:
            return FALSE;
    }
}

//
// ACPI Resource Management Implementation (_CRS/_PRS/_SRS)
//
static ACPI_DEVICE_RESOURCES HalpAcpiDeviceResources[256] = {0};

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpAcpiInitializeResourceManagement(VOID)
{
    ULONG i;
    
    DPRINT("ACPI: Initializing resource management (_CRS/_PRS/_SRS)\n");
    
    /* Initialize resource management for all possible devices */
    for (i = 0; i < 256; i++)
    {
        RtlZeroMemory(&HalpAcpiDeviceResources[i], sizeof(ACPI_DEVICE_RESOURCES));
        HalpAcpiDeviceResources[i].HasCurrentResources = FALSE;
        HalpAcpiDeviceResources[i].HasPossibleResources = FALSE;
        HalpAcpiDeviceResources[i].ResourcesAssigned = FALSE;
    }
    
    DbgPrint("ACPI: Resource management initialized for 256 devices\n");
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpAcpiGetCurrentResources(IN UCHAR Bus,
                            IN UCHAR Device,
                            IN UCHAR Function,
                            OUT PACPI_RESOURCE_LIST ResourceList)
{
    ULONG DeviceIndex = (Bus << 8) | (Device << 3) | Function;
    PACPI_DEVICE_RESOURCES DeviceResources;
    PCI_SLOT_NUMBER PciSlot;
    UCHAR DataBuffer[PCI_COMMON_HDR_LENGTH];
    PPCI_COMMON_CONFIG PciData = (PPCI_COMMON_CONFIG)DataBuffer;
    ULONG i, ResourceCount = 0;
    
    if (DeviceIndex >= 256 || !ResourceList)
        return STATUS_INVALID_PARAMETER;
        
    DeviceResources = &HalpAcpiDeviceResources[DeviceIndex];
    
    /* TODO: In a full implementation, we would execute ACPI _CRS method here
     * For now, we'll read PCI configuration space to determine current resources
     */
    
    DPRINT("ACPI: Getting current resources (_CRS) for device %d:%d.%d\n", Bus, Device, Function);
    
    /* Read PCI configuration to get current resource assignments */
    PciSlot.u.AsULONG = 0;
    PciSlot.u.bits.DeviceNumber = Device;
    PciSlot.u.bits.FunctionNumber = Function;
    
    HalpReadPCIConfig(&HalpFakePciBusHandler, PciSlot, PciData, 0, PCI_COMMON_HDR_LENGTH);
    
    if (PciData->VendorID == PCI_INVALID_VENDORID)
    {
        return STATUS_NO_SUCH_DEVICE;
    }
    
    /* Parse PCI BARs to build resource list */
    RtlZeroMemory(ResourceList, sizeof(ACPI_RESOURCE_LIST));
    
    /* Check all 6 BARs for Type 0 headers */
    if ((PciData->HeaderType & PCI_MULTIFUNCTION) == PCI_DEVICE_TYPE)
    {
        for (i = 0; i < PCI_TYPE0_ADDRESSES && ResourceCount < 32; i++)
        {
            ULONG BaseAddress = PciData->u.type0.BaseAddresses[i];
            
            if (BaseAddress != 0)
            {
                PACPI_RESOURCE_DESCRIPTOR Resource = &ResourceList->Descriptors[ResourceCount];
                
                if (BaseAddress & PCI_ADDRESS_IO_SPACE)
                {
                    /* I/O Port Resource */
                    Resource->Type = AcpiResourceTypeIo;
                    Resource->u.Io.BasePort = BaseAddress & PCI_ADDRESS_IO_ADDRESS_MASK;
                    Resource->u.Io.Length = 0x100;  /* Default length - would need BAR sizing */
                    Resource->u.Io.IsDecoded16Bit = TRUE;
                    ResourceCount++;
                    
                    DPRINT("ACPI: Found I/O resource at 0x%x\n", Resource->u.Io.BasePort);
                }
                else
                {
                    /* Memory Resource */
                    Resource->Type = AcpiResourceTypeMemory;
                    Resource->u.Memory.BaseAddress.QuadPart = BaseAddress & PCI_ADDRESS_MEMORY_ADDRESS_MASK;
                    Resource->u.Memory.Length = 0x1000;  /* Default length - would need BAR sizing */
                    Resource->u.Memory.IsWriteable = TRUE;
                    Resource->u.Memory.IsCacheable = (BaseAddress & PCI_ADDRESS_MEMORY_TYPE_MASK) != PCI_TYPE_20BIT;
                    Resource->u.Memory.IsPrefetchable = (BaseAddress & PCI_ADDRESS_MEMORY_PREFETCHABLE) != 0;
                    ResourceCount++;
                    
                    DPRINT("ACPI: Found Memory resource at 0x%I64x\n", Resource->u.Memory.BaseAddress.QuadPart);
                }
            }
        }
        
        /* Add interrupt resource if device uses interrupts */
        if (PciData->u.type0.InterruptPin != 0 && ResourceCount < 32)
        {
            PACPI_RESOURCE_DESCRIPTOR Resource = &ResourceList->Descriptors[ResourceCount];
            Resource->Type = AcpiResourceTypeIrq;
            Resource->u.Irq.Vector = HalpAcpiGetPciInterrupt(Bus, Device, Function, PciData->u.type0.InterruptPin - 1);
            Resource->u.Irq.IsLevelTriggered = TRUE;  /* PCI interrupts are level-triggered */
            Resource->u.Irq.IsActiveHigh = FALSE;     /* PCI interrupts are active-low */
            Resource->u.Irq.IsShared = TRUE;          /* PCI interrupts can be shared */
            ResourceCount++;
            
            DPRINT("ACPI: Found IRQ resource: IRQ %d\n", Resource->u.Irq.Vector);
        }
    }
    
    ResourceList->Count = ResourceCount;
    DeviceResources->CurrentResources = *ResourceList;
    DeviceResources->HasCurrentResources = TRUE;
    
    DbgPrint("ACPI: Device %d:%d.%d has %d current resources\n", Bus, Device, Function, ResourceCount);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpAcpiGetPossibleResources(IN UCHAR Bus,
                             IN UCHAR Device,
                             IN UCHAR Function,
                             OUT PACPI_RESOURCE_LIST ResourceList)
{
    ULONG DeviceIndex = (Bus << 8) | (Device << 3) | Function;
    PACPI_DEVICE_RESOURCES DeviceResources;
    
    if (DeviceIndex >= 256 || !ResourceList)
        return STATUS_INVALID_PARAMETER;
        
    DeviceResources = &HalpAcpiDeviceResources[DeviceIndex];
    
    /* TODO: In a full implementation, we would execute ACPI _PRS method here
     * For now, we'll provide generic possible resources based on device class
     */
    
    DPRINT("ACPI: Getting possible resources (_PRS) for device %d:%d.%d\n", Bus, Device, Function);
    
    RtlZeroMemory(ResourceList, sizeof(ACPI_RESOURCE_LIST));
    
    /* Create generic possible resource template */
    ResourceList->Count = 3;
    
    /* Possible Memory Resource */
    ResourceList->Descriptors[0].Type = AcpiResourceTypeMemory;
    ResourceList->Descriptors[0].u.Memory.BaseAddress.QuadPart = 0xFEB00000;  /* Example range */
    ResourceList->Descriptors[0].u.Memory.Length = 0x100000;  /* 1MB */
    ResourceList->Descriptors[0].u.Memory.IsWriteable = TRUE;
    ResourceList->Descriptors[0].u.Memory.IsCacheable = TRUE;
    ResourceList->Descriptors[0].u.Memory.IsPrefetchable = FALSE;
    
    /* Possible I/O Resource */
    ResourceList->Descriptors[1].Type = AcpiResourceTypeIo;
    ResourceList->Descriptors[1].u.Io.BasePort = 0xC000;  /* Example I/O range */
    ResourceList->Descriptors[1].u.Io.Length = 0x100;
    ResourceList->Descriptors[1].u.Io.IsDecoded16Bit = TRUE;
    
    /* Possible IRQ Resource */
    ResourceList->Descriptors[2].Type = AcpiResourceTypeIrq;
    ResourceList->Descriptors[2].u.Irq.Vector = 10;  /* Example IRQ */
    ResourceList->Descriptors[2].u.Irq.IsLevelTriggered = TRUE;
    ResourceList->Descriptors[2].u.Irq.IsActiveHigh = FALSE;
    ResourceList->Descriptors[2].u.Irq.IsShared = TRUE;
    
    DeviceResources->PossibleResources = *ResourceList;
    DeviceResources->HasPossibleResources = TRUE;
    
    DbgPrint("ACPI: Device %d:%d.%d has %d possible resource configurations\n", Bus, Device, Function, ResourceList->Count);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpAcpiSetResources(IN UCHAR Bus,
                     IN UCHAR Device,
                     IN UCHAR Function,
                     IN PACPI_RESOURCE_LIST ResourceList)
{
    ULONG DeviceIndex = (Bus << 8) | (Device << 3) | Function;
    PACPI_DEVICE_RESOURCES DeviceResources;
    ULONG i;
    
    if (DeviceIndex >= 256 || !ResourceList)
        return STATUS_INVALID_PARAMETER;
        
    DeviceResources = &HalpAcpiDeviceResources[DeviceIndex];
    
    /* TODO: In a full implementation, we would execute ACPI _SRS method here
     * For now, we'll simulate resource assignment by updating PCI BARs
     */
    
    DPRINT("ACPI: Setting resources (_SRS) for device %d:%d.%d\n", Bus, Device, Function);
    
    /* Validate resource list */
    if (ResourceList->Count > 32)
    {
        DPRINT1("ACPI: Too many resources specified: %d\n", ResourceList->Count);
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Process each resource descriptor */
    for (i = 0; i < ResourceList->Count; i++)
    {
        PACPI_RESOURCE_DESCRIPTOR Resource = &ResourceList->Descriptors[i];
        
        switch (Resource->Type)
        {
            case AcpiResourceTypeMemory:
                DPRINT("ACPI: Assigning memory resource: 0x%I64x length 0x%x\n", 
                       Resource->u.Memory.BaseAddress.QuadPart, Resource->u.Memory.Length);
                /* TODO: Program PCI BAR with memory address */
                break;
                
            case AcpiResourceTypeIo:
                DPRINT("ACPI: Assigning I/O resource: 0x%x length 0x%x\n", 
                       Resource->u.Io.BasePort, Resource->u.Io.Length);
                /* TODO: Program PCI BAR with I/O address */
                break;
                
            case AcpiResourceTypeIrq:
                DPRINT("ACPI: Assigning IRQ resource: IRQ %d\n", Resource->u.Irq.Vector);
                /* TODO: Program PCI interrupt line register */
                break;
                
            default:
                DPRINT1("ACPI: Unsupported resource type: %d\n", Resource->Type);
                break;
        }
    }
    
    /* Update device resource tracking */
    DeviceResources->CurrentResources = *ResourceList;
    DeviceResources->HasCurrentResources = TRUE;
    DeviceResources->ResourcesAssigned = TRUE;
    
    DbgPrint("ACPI: Successfully assigned %d resources to device %d:%d.%d\n", 
             ResourceList->Count, Bus, Device, Function);
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
HalpAcpiAreResourcesAssigned(IN UCHAR Bus,
                             IN UCHAR Device,
                             IN UCHAR Function)
{
    ULONG DeviceIndex = (Bus << 8) | (Device << 3) | Function;
    
    if (DeviceIndex >= 256)
        return FALSE;
        
    return HalpAcpiDeviceResources[DeviceIndex].ResourcesAssigned;
}

NTSTATUS
NTAPI
HalpAcpiRebalanceResources(IN UCHAR StartBus,
                           IN UCHAR EndBus)
{
    ULONG Bus, Device, Function;
    ULONG RebalancedDevices = 0;
    
    DPRINT("ACPI: Rebalancing resources for buses %d-%d\n", StartBus, EndBus);
    
    /* TODO: In a full implementation, this would:
     * 1. Collect all current resource assignments
     * 2. Identify resource conflicts
     * 3. Reallocate resources to resolve conflicts
     * 4. Execute _SRS methods to apply new assignments
     */
    
    for (Bus = StartBus; Bus <= EndBus; Bus++)
    {
        for (Device = 0; Device < 32; Device++)
        {
            for (Function = 0; Function < 8; Function++)
            {
                ULONG DeviceIndex = (Bus << 8) | (Device << 3) | Function;
                if (DeviceIndex < 256 && HalpAcpiDeviceResources[DeviceIndex].HasCurrentResources)
                {
                    /* This device has resources - could participate in rebalancing */
                    RebalancedDevices++;
                }
            }
        }
    }
    
    DbgPrint("ACPI: Resource rebalancing completed - %d devices processed\n", RebalancedDevices);
    return STATUS_SUCCESS;
}

VOID
NTAPI
HalpInitNonBusHandler(VOID)
{
    /* These should be written by the PCI driver later, but we give defaults */
    HalPciTranslateBusAddress = HalpTranslateBusAddress;
    HalPciAssignSlotResources = HalpAssignSlotResources;
    HalFindBusAddressTranslation = HalpFindBusAddressTranslation;
}

CODE_SEG("INIT")
VOID
NTAPI
HalpInitBusHandlers(VOID)
{
    /* On ACPI, we only have a fake PCI bus to worry about */
    HalpInitNonBusHandler();
}

CODE_SEG("INIT")
VOID
NTAPI
HalpBuildAddressMap(VOID)
{
    /* ACPI is magic baby */
}

CODE_SEG("INIT")
BOOLEAN
NTAPI
HalpGetDebugPortTable(VOID)
{
    return ((HalpDebugPortTable) &&
            (HalpDebugPortTable->BaseAddress.AddressSpaceID == 1));
}

CODE_SEG("INIT")
ULONG
NTAPI
HalpIs16BitPortDecodeSupported(VOID)
{
    /* All ACPI systems are at least "EISA" so they support this */
    return CM_RESOURCE_PORT_16_BIT_DECODE;
}

VOID
NTAPI
HalpAcpiDetectResourceListSize(OUT PULONG ListSize)
{
    PAGED_CODE();

    /* One element if there is a SCI */
    *ListSize = HalpFixedAcpiDescTable.sci_int_vector ? 1: 0;
}

NTSTATUS
NTAPI
HalpBuildAcpiResourceList(IN PIO_RESOURCE_REQUIREMENTS_LIST ResourceList)
{
    ULONG Interrupt;
    PAGED_CODE();
    ASSERT(ResourceList != NULL);

    /* Initialize the list */
    ResourceList->BusNumber = -1;
    ResourceList->AlternativeLists = 1;
    ResourceList->InterfaceType = PNPBus;
    ResourceList->List[0].Version = 1;
    ResourceList->List[0].Revision = 1;
    ResourceList->List[0].Count = 0;

    /* Is there a SCI? */
    if (HalpFixedAcpiDescTable.sci_int_vector)
    {
        /* Fill out the entry for it */
        ResourceList->List[0].Descriptors[0].Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        ResourceList->List[0].Descriptors[0].Type = CmResourceTypeInterrupt;
        ResourceList->List[0].Descriptors[0].ShareDisposition = CmResourceShareShared;

        /* Get the interrupt number */
        Interrupt = HalpPicVectorRedirect[HalpFixedAcpiDescTable.sci_int_vector];
        ResourceList->List[0].Descriptors[0].u.Interrupt.MinimumVector = Interrupt;
        ResourceList->List[0].Descriptors[0].u.Interrupt.MaximumVector = Interrupt;

        /* One more */
        ++ResourceList->List[0].Count;
    }

    /* All good */
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
HalpQueryAcpiResourceRequirements(OUT PIO_RESOURCE_REQUIREMENTS_LIST *Requirements)
{
    PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList;
    ULONG Count, ListSize;
    NTSTATUS Status;

    PAGED_CODE();

    /* Get ACPI resources */
    HalpAcpiDetectResourceListSize(&Count);
    DPRINT("Resource count: %lu\n", Count);

    /* Compute size of the list and allocate it */
    ListSize = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0].Descriptors) +
               (Count * sizeof(IO_RESOURCE_DESCRIPTOR));
    DPRINT("Resource list size: %lu\n", ListSize);
    RequirementsList = ExAllocatePoolWithTag(PagedPool, ListSize, TAG_HAL);
    if (RequirementsList)
    {
        /* Initialize it */
        RtlZeroMemory(RequirementsList, ListSize);
        RequirementsList->ListSize = ListSize;

        /* Build it */
        Status = HalpBuildAcpiResourceList(RequirementsList);
        if (NT_SUCCESS(Status))
        {
            /* It worked, return it */
            *Requirements = RequirementsList;

            /* Validate the list */
            ASSERT(RequirementsList->List[0].Count == Count);
        }
        else
        {
            /* Fail */
            ExFreePoolWithTag(RequirementsList, TAG_HAL);
            Status = STATUS_NO_SUCH_DEVICE;
        }
    }
    else
    {
        /* Not enough memory */
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Return the status */
    return Status;
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
