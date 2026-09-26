/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/acpi/tables.c
 * PURPOSE:         ACPI table discovery and cache shared by the ACPI HALs
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <hal.h>
#include <ntstrsafe.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS ********************************************************************/

/* Loader-supplied override tables are packed on this boundary */
#define HALP_ACPI_OVERRIDE_ALIGNMENT 8

LIST_ENTRY HalpAcpiTableCacheList;
FAST_MUTEX HalpAcpiTableCacheLock;

PACPI_BIOS_MULTI_NODE HalpAcpiMultiNode;
PHYSICAL_ADDRESS HalpAcpiRsdpAddress;
PHYSICAL_ADDRESS HalpAcpiRootTablePhysicalAddress;

ULONG HalpInvalidAcpiTable;

/* PRIVATE FUNCTIONS **********************************************************/

PFN_COUNT
NTAPI
HalpAcpiPagesForRange(
    _In_ PHYSICAL_ADDRESS BaseAddress,
    _In_ ULONG Length)
{
    ULONGLONG Pages;

    /* A table need not start on a page boundary */
    Pages = ((BaseAddress.QuadPart & (PAGE_SIZE - 1)) + Length + PAGE_SIZE - 1) >> PAGE_SHIFT;
    return (PFN_COUNT)max(Pages, 1);
}

static
PVOID
HalpAcpiMapTable(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ PFN_COUNT PageCount)
{
    /* Phase 0 uses the HAL heap, phase 1 uses Mm */
    if (LoaderBlock)
        return HalpMapPhysicalMemory64(PhysicalAddress, PageCount);

    return MmMapIoSpace(PhysicalAddress, PageCount << PAGE_SHIFT, MmNonCached);
}

static
VOID
HalpAcpiUnmapTable(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PVOID VirtualAddress,
    _In_ PFN_COUNT PageCount)
{
    if (LoaderBlock)
        HalpUnmapVirtualAddress(VirtualAddress, PageCount);
    else
        MmUnmapIoSpace(VirtualAddress, PageCount << PAGE_SHIFT);
}

/*
 * A table is first mapped with two pages, which always covers its header.
 * Extend that mapping to the full length the header reports.
 */
static
PDESCRIPTION_HEADER
HalpAcpiRemapTable(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ PDESCRIPTION_HEADER Header,
    _Out_ PFN_COUNT *PageCount)
{
    *PageCount = HalpAcpiPagesForRange(PhysicalAddress, Header->Length);
    if (*PageCount == 2) return Header;

    HalpAcpiUnmapTable(LoaderBlock, Header, 2);
    Header = HalpAcpiMapTable(LoaderBlock, PhysicalAddress, *PageCount);
    if (!Header) DPRINT1("HAL: Failed to remap ACPI table.\n");

    return Header;
}

static
VOID
HalpAcpiValidateChecksum(
    _In_ PDESCRIPTION_HEADER Header)
{
    CHAR CheckSum = 0;
    PCHAR CurrentByte;

    /* All tables in ACPI 3.0 other than the FACP should have correct checksum */
    if ((Header->Signature == FADT_SIGNATURE) && (Header->Revision <= 2))
        return;

    /* Go to the end of the table */
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
        DPRINT1("Checksum failed on ACPI table %.4s\n", (PCSTR)&Header->Signature);
    }
}

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
                                                       HALP_ACPI_MAX_PHYSICAL_ADDRESS,
                                                       PageCount,
                                                       FALSE);
        if (PhysAddress.QuadPart)
        {
            /* Map it for the lifetime of the system */
            CachedTable = HalpAcpiMapCachedTable(PhysAddress, PageCount);
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

PHYSICAL_ADDRESS
NTAPI
HalpAcpiSelectFadtPointer(
    _In_ ULONG LegacyPointer,
    _In_ PHYSICAL_ADDRESS ExtendedPointer)
{
    PHYSICAL_ADDRESS Address;

    /* The 64-bit field takes precedence when the firmware provides it */
    if (ExtendedPointer.QuadPart)
        return ExtendedPointer;

    Address.QuadPart = LegacyPointer;
    return Address;
}

static
PDESCRIPTION_HEADER
HalpAcpiGetTableFromBios(
    _In_opt_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ ULONG Signature,
    _Out_ PFN_COUNT *PageCount)
{
    PHYSICAL_ADDRESS PhysicalAddress;
    PXSDT Xsdt;
    PRSDT Rsdt;
    PFADT Fadt;
    PDESCRIPTION_HEADER Header;
    ULONG TableLength;
    ULONG Offset;
    ULONG EntryCount, CurrentEntry;

    /* Should not query the RSDT/XSDT by itself */
    if ((Signature == RSDT_SIGNATURE) || (Signature == XSDT_SIGNATURE)) return NULL;

    /* Special case request for DSDT, because the FADT points to it */
    if (Signature == DSDT_SIGNATURE)
    {
        /* Grab the FADT */
        Fadt = HalpAcpiGetTable(LoaderBlock, FADT_SIGNATURE);
        if (!Fadt) return NULL;

        /* The DSDT may be above 4GB */
        PhysicalAddress = HalpAcpiSelectFadtPointer(Fadt->dsdt, Fadt->x_dsdt);
        if (!PhysicalAddress.QuadPart) return NULL;

        Header = HalpAcpiMapTable(LoaderBlock, PhysicalAddress, 2);
        if (!Header)
        {
            DPRINT1("HAL: Failed to map ACPI table.\n");
            return NULL;
        }

        /* Validate the signature */
        if (Header->Signature != DSDT_SIGNATURE)
        {
            HalpAcpiUnmapTable(LoaderBlock, Header, 2);
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
                PhysicalAddress.QuadPart = Rsdt->Tables[CurrentEntry];
            }
            else
            {
                /* Read the 64-bit physical address */
                PhysicalAddress = Xsdt->Tables[CurrentEntry];
            }

            /* Map just enough to read the signature */
            Header = HalpAcpiMapTable(LoaderBlock, PhysicalAddress, 2);
            if (!Header)
            {
                /* Game over */
                DPRINT1("HAL: Failed to map ACPI table.\n");
                return NULL;
            }

            DPRINT("Found ACPI table %.4s at 0x%p\n", (PCSTR)&Header->Signature, Header);
            if (Header->Signature == Signature) break;

            HalpAcpiUnmapTable(LoaderBlock, Header, 2);
        }

        /* Did we end up here back at the last entry? */
        if (CurrentEntry == EntryCount) return NULL;
    }

    /* Now map this table using its correct size */
    Header = HalpAcpiRemapTable(LoaderBlock, PhysicalAddress, Header, PageCount);
    if (!Header) return NULL;

    HalpAcpiValidateChecksum(Header);

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
        TableAddress = HalpAcpiGetTableFromBios(LoaderBlock, Signature, &PageCount);
        if (TableAddress)
        {
            /* Found it, copy it into our own memory */
            BiosCopy = HalpAcpiCopyBiosTable(LoaderBlock, TableAddress);

            /* Unmap the BIOS copy */
            HalpAcpiUnmapTable(LoaderBlock, TableAddress, PageCount);

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

/* An OEM field matches when it equals Match, padded with blanks or NULs */
static
BOOLEAN
HalpAcpiStringMatches(
    _In_reads_(FieldLength) const UCHAR *Field,
    _In_ ULONG FieldLength,
    _In_opt_ PCSTR Match)
{
    ULONG i;

    if (!Match)
        return TRUE;

    for (i = 0; (i < FieldLength) && (Match[i] != ANSI_NULL); ++i)
    {
        if (Field[i] != (UCHAR)Match[i])
            return FALSE;
    }

    if (i == FieldLength)
        return Match[i] == ANSI_NULL;

    for (; i < FieldLength; ++i)
    {
        if ((Field[i] != ' ') && (Field[i] != ANSI_NULL))
            return FALSE;
    }

    return TRUE;
}

PVOID
NTAPI
HaliGetCachedAcpiTable(
    _In_ ULONG Signature,
    _In_opt_ PCSTR OemId,
    _In_opt_ PCSTR OemTableId)
{
    PDESCRIPTION_HEADER TableHeader;

    TableHeader = HalAcpiGetTable(NULL, Signature);
    if (!TableHeader)
        return NULL;

    if (!HalpAcpiStringMatches(TableHeader->OEMID,
                               sizeof(TableHeader->OEMID),
                               OemId) ||
        !HalpAcpiStringMatches(TableHeader->OEMTableID,
                               sizeof(TableHeader->OEMTableID),
                               OemTableId))
    {
        return NULL;
    }

    return TableHeader;
}

static
NTSTATUS
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
                                                       HALP_ACPI_MAX_PHYSICAL_ADDRESS,
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

    /* Keep the RSDP for HalAcpiAuditInformation queries */
    HalpAcpiRsdpAddress = HalpAcpiMultiNode->RsdpAddress;

    /* Return the data */
    *AcpiMultiNode = HalpAcpiMultiNode;
    return STATUS_SUCCESS;
}

/*
 * The loader may pass replacement tables, packed on 8-byte boundaries.
 * They go ahead of the firmware tables, so a lookup finds them first.
 */
static
VOID
HalpAcpiApplyLoaderOverrides(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_reads_bytes_(OverrideSize) PVOID OverrideBuffer,
    _In_ ULONG OverrideSize)
{
    PDESCRIPTION_HEADER TableHeader, CachedHeader;
    PACPI_CACHED_TABLE CachedTable;
    PUCHAR Current = OverrideBuffer;
    ULONG Remaining = OverrideSize;
    ULONG Advance;
    ULONG Applied = 0;

    while (Remaining >= sizeof(DESCRIPTION_HEADER))
    {
        TableHeader = (PDESCRIPTION_HEADER)Current;
        if ((TableHeader->Length < sizeof(DESCRIPTION_HEADER)) ||
            (TableHeader->Length > Remaining))
        {
            DPRINT1("HAL: ACPI override %.4s has invalid length %lu\n",
                    (PCSTR)&TableHeader->Signature, TableHeader->Length);
            break;
        }

        CachedHeader = HalpAcpiCopyBiosTable(LoaderBlock, TableHeader);
        if (!CachedHeader)
        {
            DPRINT1("HAL: Failed to cache ACPI override %.4s\n",
                    (PCSTR)&TableHeader->Signature);
            break;
        }

        /* Prepend so the override supersedes the firmware copy */
        CachedTable = CONTAINING_RECORD(CachedHeader, ACPI_CACHED_TABLE, Header);
        InsertHeadList(&HalpAcpiTableCacheList, &CachedTable->Links);
        Applied++;

        Advance = min(ALIGN_UP_BY(TableHeader->Length, HALP_ACPI_OVERRIDE_ALIGNMENT), Remaining);
        Current += Advance;
        Remaining -= Advance;
    }

    DPRINT1("HAL: Applied %lu ACPI override table(s)\n", Applied);
}

NTSTATUS
NTAPI
HalpAcpiTableCacheInit(IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PACPI_BIOS_MULTI_NODE AcpiMultiNode;
    NTSTATUS Status = STATUS_SUCCESS;
    PHYSICAL_ADDRESS PhysicalAddress;
    PVOID MappedAddress;
    PFN_COUNT TablePages;
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
    HalpAcpiRootTablePhysicalAddress = PhysicalAddress;

    /* Map the RSDT, we assume it's about 2 pages */
    MappedAddress = HalpAcpiMapTable(LoaderBlock, PhysicalAddress, 2);
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
    MappedAddress = HalpAcpiRemapTable(LoaderBlock, PhysicalAddress, &Rsdt->Header, &TablePages);
    Rsdt = MappedAddress;
    if (!MappedAddress)
    {
        /* Fail, no memory */
        DPRINT1("HAL: Couldn't remap RSDT\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Now take the BIOS copy and make our own local copy */
    Rsdt = HalpAcpiCopyBiosTable(LoaderBlock, &Rsdt->Header);

    /* Get rid of the BIOS mapping */
    HalpAcpiUnmapTable(LoaderBlock, MappedAddress, TablePages);

    if (!Rsdt)
    {
        /* Fail, no memory */
        DPRINT1("HAL: Couldn't copy RSDT\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Cache the RSDT */
    HalpAcpiCacheTable(&Rsdt->Header);

    /* Did a compatible loader provide ACPI table overrides? */
    LoaderExtension = LoaderBlock ? LoaderBlock->Extension : NULL;
    if (LoaderExtension &&
        (LoaderExtension->Size >= RTL_SIZEOF_THROUGH_FIELD(LOADER_PARAMETER_EXTENSION, AcpiTableSize)) &&
        LoaderExtension->AcpiTable &&
        LoaderExtension->AcpiTableSize)
    {
        HalpAcpiApplyLoaderOverrides(LoaderBlock,
                                     LoaderExtension->AcpiTable,
                                     LoaderExtension->AcpiTableSize);
    }

    /* Done */
    return Status;
}

static
PCSTR
HalpAcpiVersionString(
    _In_ PFADT Fadt)
{
    // v1.0+: Revision is major version.
    // v5.1+: minor_revision is minor version.
    // v6.4+: errata bits are errata version.
    switch (Fadt->Header.Revision)
    {
        case 0: // Should not happen.
            return "Unknown_0";
        case 1:
            return "1.0-1.0b";
        case 2: // Should not happen.
            return "Unknown_2";
        case 3:
            return "1.5-2.0_C";
        case 4:
            return "3.0-4.0_A";
        case 5:
            if (Fadt->minor_revision == 0)
                return "5.0-5.0_B";
            if (Fadt->minor_revision == 1)
                return "5.1-5.1_B";
            break;
        case 6:
            if (Fadt->minor_revision == 0)
                return "6.0-6.0_A";
            if (Fadt->minor_revision == 1)
                return "6.1-6.1_A";
            if (Fadt->minor_revision == 2)
                return "6.2-6.2_B";
            if (Fadt->minor_revision == 3)
                return "6.3-6.3_A";
            if ((Fadt->minor_revision & 0x0F) == 0x04)
            {
                if ((Fadt->minor_revision & 0xF0) == 0x00)
                    return "6.4";
                if ((Fadt->minor_revision & 0xF0) == 0x10)
                    return "6.4_A";
            }
            else if (Fadt->minor_revision == 5) // v6.5_A too is documented as errata=0.
            {
                return "6.5-6.6";
            }
            break;
    }

    // Unknown past values, or newer than v6.6 (documented as 6.5).
    return NULL;
}

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiLogTables(
    _In_ PFADT Fadt)
{
    PLIST_ENTRY NextEntry;
    PACPI_CACHED_TABLE CachedTable;
    PCSTR AcpiVersion;
    CHAR AcpiLine[512];
    PSTR End = AcpiLine;
    size_t Remaining = sizeof(AcpiLine);

    /* Print the ACPI version */
    AcpiVersion = HalpAcpiVersionString(Fadt);
    if (AcpiVersion)
    {
        RtlStringCbPrintfExA(End, Remaining, &End, &Remaining, 0,
                             "ACPI v%s detected. Tables:", AcpiVersion);
    }
    else
    {
        RtlStringCbPrintfExA(End, Remaining, &End, &Remaining, 0,
                             "ACPI vUnknown_%u_%u detected. Tables:",
                             Fadt->Header.Revision, Fadt->minor_revision);
    }

    /* List cached tables */
    for (NextEntry = HalpAcpiTableCacheList.Flink;
         NextEntry != &HalpAcpiTableCacheList;
         NextEntry = NextEntry->Flink)
    {
        CachedTable = CONTAINING_RECORD(NextEntry, ACPI_CACHED_TABLE, Links);
        RtlStringCbPrintfExA(End, Remaining, &End, &Remaining, 0,
                             " [%.4s]", (PCSTR)&CachedTable->Header.Signature);
    }

    DPRINT1("%s\n", AcpiLine);
}

/* EOF */
