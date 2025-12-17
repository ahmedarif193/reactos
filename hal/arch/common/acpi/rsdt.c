/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            hal/arch/common/acpi/rsdt.c
 * PURPOSE:         RSDT/XSDT enumeration and table caching
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

#include <hal.h>
#include <halacpi.h>
#include <ntifs.h>
#include <reactos/drivers/acpi/acpi.h>
#define NDEBUG
#include <debug.h>

extern PHYSICAL_ADDRESS HalpAcpiRootTablePhysicalAddress;

static
PHYSICAL_ADDRESS
HalpAcpiSelectFadtPointerLocal(
    _In_ ULONG LegacyPointer,
    _In_ PHYSICAL_ADDRESS ExtendedPointer);

static
VOID
HalpAcpiCacheSingleTableAtAddress(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PHYSICAL_ADDRESS PhysicalAddress)
{
    PDESCRIPTION_HEADER Header;
    PFN_COUNT PageCount;
    CHAR CheckSum;
    PCHAR CurrentByte;

    /* Map an initial 2 pages to read the header */
    if (LoaderBlock)
    {
        Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);
    }
    else
    {
        Header = MmMapIoSpace(PhysicalAddress, 2 * PAGE_SIZE, MmNonCached);
    }

    if (!Header)
    {
        DPRINT1("HAL: Failed to map ACPI table at %I64x for enumeration\n",
                (unsigned long long)PhysicalAddress.QuadPart);
        return;
    }

    /* Skip root tables; they are handled separately */
    if ((Header->Signature == RSDT_SIGNATURE) ||
        (Header->Signature == XSDT_SIGNATURE))
    {
        if (LoaderBlock)
            HalpUnmapVirtualAddress(Header, 2);
        else
            MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
        return;
    }

    /* Do not duplicate already cached tables */
    if (HalpAcpiGetCachedTable(Header->Signature))
    {
        if (LoaderBlock)
            HalpUnmapVirtualAddress(Header, 2);
        else
            MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
        return;
    }

    /* Remap with the exact size if needed */
    PageCount = BYTES_TO_PAGES(Header->Length);
    if (PageCount != 2)
    {
        if (LoaderBlock)
            HalpUnmapVirtualAddress(Header, 2);
        else
            MmUnmapIoSpace(Header, 2 * PAGE_SIZE);

        if (LoaderBlock)
            Header = HalpMapPhysicalMemory64(PhysicalAddress, PageCount);
        else
            Header = MmMapIoSpace(PhysicalAddress, PageCount << PAGE_SHIFT, MmNonCached);

        if (!Header)
        {
            DPRINT1("HAL: Failed to remap ACPI table at %I64x for enumeration\n",
                    (unsigned long long)PhysicalAddress.QuadPart);
            return;
        }
    }

    /* Checksum validation for ACPI 3.0+ non-FADT tables */
    if ((Header->Signature != FADT_SIGNATURE) || (Header->Revision > 2))
    {
        CheckSum = 0;
        CurrentByte = (PCHAR)Header + Header->Length;
        while (CurrentByte-- != (PCHAR)Header)
        {
            CheckSum += *CurrentByte;
        }

        if (CheckSum)
        {
            DPRINT1("HAL: Early ACPI table checksum verification FAILED for %c%c%c%c (enum)\n",
                    Header->Signature & 0xFF,
                    (Header->Signature & 0xFF00) >> 8,
                    (Header->Signature & 0xFF0000) >> 16,
                    (Header->Signature & 0xFF000000) >> 24);
        }
        else
        {
            DPRINT("HAL: Early ACPI table checksum verification OK for %c%c%c%c (enum)\n",
                   Header->Signature & 0xFF,
                   (Header->Signature & 0xFF00) >> 8,
                   (Header->Signature & 0xFF0000) >> 16,
                   (Header->Signature & 0xFF000000) >> 24);
        }
    }

    /* Copy into HAL-owned memory and cache it */
    {
        PDESCRIPTION_HEADER CachedCopy;

        CachedCopy = HalpAcpiCopyBiosTable(LoaderBlock, Header);

        if (LoaderBlock)
            HalpUnmapVirtualAddress(Header, PageCount);
        else
            MmUnmapIoSpace(Header, PageCount << PAGE_SHIFT);

        if (!CachedCopy)
            return;

        HalpAcpiCacheTable(CachedCopy);
    }
}

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiEnumerateRootTables(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PRSDT Root)
{
    ULONG TableLength;
    ULONG Offset;
    ULONG EntryCount;
    ULONG Index;
    PHYSICAL_ADDRESS PhysicalAddress;

    if (!Root)
        return;

    /* Determine table format and iterate entries */
    if (Root->Header.Signature == RSDT_SIGNATURE)
    {
        TableLength = Root->Header.Length;
        Offset = FIELD_OFFSET(RSDT, Tables);
        if (TableLength < Offset)
            return;

        EntryCount = (TableLength - Offset) / sizeof(ULONG);
        for (Index = 0; Index < EntryCount; ++Index)
        {
            PhysicalAddress.LowPart = Root->Tables[Index];
            PhysicalAddress.HighPart = 0;
            HalpAcpiCacheSingleTableAtAddress(LoaderBlock, PhysicalAddress);
        }
    }
    else if (Root->Header.Signature == XSDT_SIGNATURE)
    {
        PXSDT Xsdt = (PXSDT)Root;

        TableLength = Xsdt->Header.Length;
        Offset = FIELD_OFFSET(XSDT, Tables);
        if (TableLength < Offset)
            return;

        EntryCount = (TableLength - Offset) / sizeof(PHYSICAL_ADDRESS);
        for (Index = 0; Index < EntryCount; ++Index)
        {
            PhysicalAddress = Xsdt->Tables[Index];
            HalpAcpiCacheSingleTableAtAddress(LoaderBlock, PhysicalAddress);
        }
    }
}

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiReserveRootTables(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PRSDT Root;
    PXSDT Xsdt;
    ULONG TableLength;
    ULONG Offset;
    ULONG EntryCount;
    ULONG Index;
    PHYSICAL_ADDRESS PhysicalAddress;
    PDESCRIPTION_HEADER Header;

    Root = HalAcpiGetTable(LoaderBlock, RSDT_SIGNATURE);
    if (!Root)
    {
        Xsdt = HalAcpiGetTable(LoaderBlock, XSDT_SIGNATURE);
        Root = (PRSDT)Xsdt;
    }

    if (!Root)
        return;

    if (Root->Header.Signature == RSDT_SIGNATURE)
    {
        TableLength = Root->Header.Length;
        Offset = FIELD_OFFSET(RSDT, Tables);
        if (TableLength < Offset)
            return;

        EntryCount = (TableLength - Offset) / sizeof(ULONG);
        for (Index = 0; Index < EntryCount; ++Index)
        {
            PhysicalAddress.LowPart = Root->Tables[Index];
            PhysicalAddress.HighPart = 0;

            if (LoaderBlock)
                Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);
            else
                Header = MmMapIoSpace(PhysicalAddress, 2 * PAGE_SIZE, MmNonCached);

            if (!Header)
                continue;

            if ((Header->Signature != RSDT_SIGNATURE) &&
                (Header->Signature != XSDT_SIGNATURE))
            {
                DPRINT1("ACPI: Reserving %c%c%c%c table memory at [mem %I64x-%I64x]\n",
                        Header->Signature & 0xFF,
                        (Header->Signature & 0xFF00) >> 8,
                        (Header->Signature & 0xFF0000) >> 16,
                        (Header->Signature & 0xFF000000) >> 24,
                        (unsigned long long)PhysicalAddress.QuadPart,
                        (unsigned long long)(PhysicalAddress.QuadPart + Header->Length - 1));
            }

            if (LoaderBlock)
                HalpUnmapVirtualAddress(Header, 2);
            else
                MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
        }
    }
    else if (Root->Header.Signature == XSDT_SIGNATURE)
    {
        Xsdt = (PXSDT)Root;

        TableLength = Xsdt->Header.Length;
        Offset = FIELD_OFFSET(XSDT, Tables);
        if (TableLength < Offset)
            return;

        EntryCount = (TableLength - Offset) / sizeof(PHYSICAL_ADDRESS);
        for (Index = 0; Index < EntryCount; ++Index)
        {
            PhysicalAddress = Xsdt->Tables[Index];

            if (LoaderBlock)
                Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);
            else
                Header = MmMapIoSpace(PhysicalAddress, 2 * PAGE_SIZE, MmNonCached);

            if (!Header)
                continue;

            if ((Header->Signature != RSDT_SIGNATURE) &&
                (Header->Signature != XSDT_SIGNATURE))
            {
                DPRINT1("ACPI: Reserving %c%c%c%c table memory at [mem %I64x-%I64x]\n",
                        Header->Signature & 0xFF,
                        (Header->Signature & 0xFF00) >> 8,
                        (Header->Signature & 0xFF0000) >> 16,
                        (Header->Signature & 0xFF000000) >> 24,
                        (unsigned long long)PhysicalAddress.QuadPart,
                        (unsigned long long)(PhysicalAddress.QuadPart + Header->Length - 1));
            }

            if (LoaderBlock)
                HalpUnmapVirtualAddress(Header, 2);
            else
                MmUnmapIoSpace(Header, 2 * PAGE_SIZE);
        }
    }

    /*
     * Reserve DSDT and FACS memory using the FADT pointers,
     * so the reservation block matches Linux.
     */
    {
        PHYSICAL_ADDRESS DsdtPhys;
        PDESCRIPTION_HEADER DsdtHeader;

        DsdtPhys = HalpAcpiSelectFadtPointerLocal(HalpFixedAcpiDescTable.dsdt,
                                                  HalpFixedAcpiDescTable.x_dsdt);
        if (DsdtPhys.QuadPart)
        {
            DsdtHeader = HalpMapPhysicalMemory64(DsdtPhys, 2);
            if (DsdtHeader && (DsdtHeader->Signature == DSDT_SIGNATURE))
            {
                DPRINT1("ACPI: Reserving DSDT table memory at [mem %I64x-%I64x]\n",
                        (unsigned long long)DsdtPhys.QuadPart,
                        (unsigned long long)(DsdtPhys.QuadPart + DsdtHeader->Length - 1));
            }

            if (DsdtHeader)
                HalpUnmapVirtualAddress(DsdtHeader, 2);
        }
    }

    {
        PHYSICAL_ADDRESS FacsPhys;
        PFACS Facs;

        FacsPhys = HalpAcpiSelectFadtPointerLocal(HalpFixedAcpiDescTable.facs,
                                                  HalpFixedAcpiDescTable.x_firmware_ctrl);
        if (FacsPhys.QuadPart)
        {
            Facs = HalpMapPhysicalMemory64(FacsPhys, 1);
            if (Facs && (Facs->Signature == FACS_SIGNATURE))
            {
                DPRINT1("ACPI: Reserving FACS table memory at [mem %I64x-%I64x]\n",
                        (unsigned long long)FacsPhys.QuadPart,
                        (unsigned long long)(FacsPhys.QuadPart + Facs->Length - 1));
            }

            if (Facs)
                HalpUnmapVirtualAddress(Facs, 1);
        }
    }
}

static
PHYSICAL_ADDRESS
HalpAcpiSelectFadtPointerLocal(
    _In_ ULONG LegacyPointer,
    _In_ PHYSICAL_ADDRESS ExtendedPointer)
{
    PHYSICAL_ADDRESS Address;

    if (ExtendedPointer.QuadPart)
    {
        Address = ExtendedPointer;
    }
    else
    {
        Address.QuadPart = 0;
        Address.LowPart = LegacyPointer;
    }

    return Address;
}

CODE_SEG("INIT")
VOID
NTAPI
HalpAcpiLogRootTablesOrdered(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock,
    _In_ PFADT Fadt)
{
    PRSDT Root;
    PXSDT Xsdt;
    ULONG TableLength;
    ULONG Offset;
    ULONG EntryCount;
    ULONG Index;
    PHYSICAL_ADDRESS PhysicalAddress;
    PDESCRIPTION_HEADER Header;
    CHAR OemId[7];
    CHAR OemTableId[9];
    CHAR CreatorId[5];

    /* Find the cached root table */
    Root = HalAcpiGetTable(LoaderBlock, RSDT_SIGNATURE);
    if (!Root)
    {
        Xsdt = HalAcpiGetTable(LoaderBlock, XSDT_SIGNATURE);
        Root = (PRSDT)Xsdt;
    }

    if (!Root)
        return;

    /*
     * Log the root table (RSDT/XSDT) like Linux, using the original
     * physical address recorded during Phase 0.
     */
    {
        CHAR OemIdRoot[7];
        CHAR OemTableIdRoot[9];
        CHAR CreatorIdRoot[5];

        RtlCopyMemory(OemIdRoot, Root->Header.OEMID, sizeof(Root->Header.OEMID));
        OemIdRoot[sizeof(Root->Header.OEMID)] = '\0';
        RtlCopyMemory(OemTableIdRoot, Root->Header.OEMTableID, sizeof(Root->Header.OEMTableID));
        OemTableIdRoot[sizeof(Root->Header.OEMTableID)] = '\0';
        RtlCopyMemory(CreatorIdRoot, Root->Header.CreatorID, sizeof(Root->Header.CreatorID));
        CreatorIdRoot[sizeof(Root->Header.CreatorID)] = '\0';

        DPRINT1("ACPI: %c%c%c%c %016I64x %06lx (v%02u %s %s %08lx %s %08lx)\n",
                Root->Header.Signature & 0xFF,
                (Root->Header.Signature & 0xFF00) >> 8,
                (Root->Header.Signature & 0xFF0000) >> 16,
                (Root->Header.Signature & 0xFF000000) >> 24,
                (unsigned long long)HalpAcpiRootTablePhysicalAddress.QuadPart,
                (ULONG)Root->Header.Length,
                (ULONG)Root->Header.Revision,
                OemIdRoot,
                OemTableIdRoot,
                Root->Header.OEMRevision,
                CreatorIdRoot,
                Root->Header.CreatorRev);
    }

    /*
     * First pass: log the FADT/FACP header so it appears immediately
     * after the root table, as in Linux.
     */
    if (Root->Header.Signature == RSDT_SIGNATURE)
    {
        TableLength = Root->Header.Length;
        Offset = FIELD_OFFSET(RSDT, Tables);
        if (TableLength >= Offset)
        {
            EntryCount = (TableLength - Offset) / sizeof(ULONG);
            for (Index = 0; Index < EntryCount; ++Index)
            {
                PhysicalAddress.LowPart = Root->Tables[Index];
                PhysicalAddress.HighPart = 0;

                Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);
                if (!Header)
                    continue;

                if (Header->Signature == FADT_SIGNATURE)
                {
                    RtlCopyMemory(OemId, Header->OEMID, sizeof(Header->OEMID));
                    OemId[sizeof(Header->OEMID)] = '\0';
                    RtlCopyMemory(OemTableId, Header->OEMTableID, sizeof(Header->OEMTableID));
                    OemTableId[sizeof(Header->OEMTableID)] = '\0';
                    RtlCopyMemory(CreatorId, Header->CreatorID, sizeof(Header->CreatorID));
                    CreatorId[sizeof(Header->CreatorID)] = '\0';

                    DPRINT1("ACPI: %c%c%c%c %016I64x %06lx (v%02u %s %s %08lx %s %08lx)\n",
                            Header->Signature & 0xFF,
                            (Header->Signature & 0xFF00) >> 8,
                            (Header->Signature & 0xFF0000) >> 16,
                            (Header->Signature & 0xFF000000) >> 24,
                            (unsigned long long)PhysicalAddress.QuadPart,
                            (ULONG)Header->Length,
                            (ULONG)Header->Revision,
                            OemId,
                            OemTableId,
                            Header->OEMRevision,
                            CreatorId,
                            Header->CreatorRev);

                    HalpUnmapVirtualAddress(Header, 2);
                    break;
                }

                HalpUnmapVirtualAddress(Header, 2);
            }
        }
    }
    else if (Root->Header.Signature == XSDT_SIGNATURE)
    {
        Xsdt = (PXSDT)Root;

        TableLength = Xsdt->Header.Length;
        Offset = FIELD_OFFSET(XSDT, Tables);
        if (TableLength >= Offset)
        {
            EntryCount = (TableLength - Offset) / sizeof(PHYSICAL_ADDRESS);
            for (Index = 0; Index < EntryCount; ++Index)
            {
                PhysicalAddress = Xsdt->Tables[Index];

                Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);
                if (!Header)
                    continue;

                if (Header->Signature == FADT_SIGNATURE)
                {
                    RtlCopyMemory(OemId, Header->OEMID, sizeof(Header->OEMID));
                    OemId[sizeof(Header->OEMID)] = '\0';
                    RtlCopyMemory(OemTableId, Header->OEMTableID, sizeof(Header->OEMTableID));
                    OemTableId[sizeof(Header->OEMTableID)] = '\0';
                    RtlCopyMemory(CreatorId, Header->CreatorID, sizeof(Header->CreatorID));
                    CreatorId[sizeof(Header->CreatorID)] = '\0';

                    DPRINT1("ACPI: %c%c%c%c %016I64x %06lx (v%02u %s %s %08lx %s %08lx)\n",
                            Header->Signature & 0xFF,
                            (Header->Signature & 0xFF00) >> 8,
                            (Header->Signature & 0xFF0000) >> 16,
                            (Header->Signature & 0xFF000000) >> 24,
                            (unsigned long long)PhysicalAddress.QuadPart,
                            (ULONG)Header->Length,
                            (ULONG)Header->Revision,
                            OemId,
                            OemTableId,
                            Header->OEMRevision,
                            CreatorId,
                            Header->CreatorRev);

                    HalpUnmapVirtualAddress(Header, 2);
                    break;
                }

                HalpUnmapVirtualAddress(Header, 2);
            }
        }
    }

    /* Log FADT-linked DSDT and FACS immediately after FADT */
    if (Fadt)
    {
        PHYSICAL_ADDRESS DsdtPhys;
        PDESCRIPTION_HEADER DsdtHeader;

        DsdtPhys = HalpAcpiSelectFadtPointerLocal(Fadt->dsdt, Fadt->x_dsdt);
        if (DsdtPhys.QuadPart)
        {
            DsdtHeader = HalpMapPhysicalMemory64(DsdtPhys, 2);
            if (DsdtHeader && (DsdtHeader->Signature == DSDT_SIGNATURE))
            {
                RtlCopyMemory(OemId, DsdtHeader->OEMID, sizeof(DsdtHeader->OEMID));
                OemId[sizeof(DsdtHeader->OEMID)] = '\0';
                RtlCopyMemory(OemTableId, DsdtHeader->OEMTableID, sizeof(DsdtHeader->OEMTableID));
                OemTableId[sizeof(DsdtHeader->OEMTableID)] = '\0';
                RtlCopyMemory(CreatorId, DsdtHeader->CreatorID, sizeof(DsdtHeader->CreatorID));
                CreatorId[sizeof(DsdtHeader->CreatorID)] = '\0';

                DPRINT1("ACPI: DSDT %016I64x %06lx (v%02u %s %s %08lx %s %08lx)\n",
                        (unsigned long long)DsdtPhys.QuadPart,
                        (ULONG)DsdtHeader->Length,
                        (ULONG)DsdtHeader->Revision,
                        OemId,
                        OemTableId,
                        DsdtHeader->OEMRevision,
                        CreatorId,
                        DsdtHeader->CreatorRev);
            }

            if (DsdtHeader)
                HalpUnmapVirtualAddress(DsdtHeader, 2);
        }

        {
            PHYSICAL_ADDRESS FacsPhys;
            PFACS Facs;

            FacsPhys = HalpAcpiSelectFadtPointerLocal(Fadt->facs, Fadt->x_firmware_ctrl);
            if (FacsPhys.QuadPart)
            {
                Facs = HalpMapPhysicalMemory64(FacsPhys, 1);
                if (Facs && (Facs->Signature == FACS_SIGNATURE))
                {
                    DPRINT1("ACPI: FACS %016I64x %06lx\n",
                            (unsigned long long)FacsPhys.QuadPart,
                            (ULONG)Facs->Length);
                }

                if (Facs)
                    HalpUnmapVirtualAddress(Facs, 1);
            }
        }
    }

    /*
     * Second pass: log all other non-root, non-FADT tables (APIC, HPET,
     * MCFG, WAET, etc.) in the order they appear in the root, matching
     * Linux after the FACP/DSDT/FACS header lines.
     */
    if (Root->Header.Signature == RSDT_SIGNATURE)
    {
        TableLength = Root->Header.Length;
        Offset = FIELD_OFFSET(RSDT, Tables);
        if (TableLength < Offset)
            return;

        EntryCount = (TableLength - Offset) / sizeof(ULONG);
        for (Index = 0; Index < EntryCount; ++Index)
        {
            PhysicalAddress.LowPart = Root->Tables[Index];
            PhysicalAddress.HighPart = 0;

            Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);
            if (!Header)
                continue;

            if ((Header->Signature != RSDT_SIGNATURE) &&
                (Header->Signature != XSDT_SIGNATURE) &&
                (Header->Signature != FADT_SIGNATURE))
            {
                RtlCopyMemory(OemId, Header->OEMID, sizeof(Header->OEMID));
                OemId[sizeof(Header->OEMID)] = '\0';
                RtlCopyMemory(OemTableId, Header->OEMTableID, sizeof(Header->OEMTableID));
                OemTableId[sizeof(Header->OEMTableID)] = '\0';
                RtlCopyMemory(CreatorId, Header->CreatorID, sizeof(Header->CreatorID));
                CreatorId[sizeof(Header->CreatorID)] = '\0';

                DPRINT1("ACPI: %c%c%c%c %016I64x %06lx (v%02u %s %s %08lx %s %08lx)\n",
                        Header->Signature & 0xFF,
                        (Header->Signature & 0xFF00) >> 8,
                        (Header->Signature & 0xFF0000) >> 16,
                        (Header->Signature & 0xFF000000) >> 24,
                        (unsigned long long)PhysicalAddress.QuadPart,
                        (ULONG)Header->Length,
                        (ULONG)Header->Revision,
                        OemId,
                        OemTableId,
                        Header->OEMRevision,
                        CreatorId,
                        Header->CreatorRev);
            }

            HalpUnmapVirtualAddress(Header, 2);
        }
    }
    else if (Root->Header.Signature == XSDT_SIGNATURE)
    {
        Xsdt = (PXSDT)Root;

        TableLength = Xsdt->Header.Length;
        Offset = FIELD_OFFSET(XSDT, Tables);
        if (TableLength < Offset)
            return;

        EntryCount = (TableLength - Offset) / sizeof(PHYSICAL_ADDRESS);
        for (Index = 0; Index < EntryCount; ++Index)
        {
            PhysicalAddress = Xsdt->Tables[Index];

            Header = HalpMapPhysicalMemory64(PhysicalAddress, 2);
            if (!Header)
                continue;

            if ((Header->Signature != RSDT_SIGNATURE) &&
                (Header->Signature != XSDT_SIGNATURE) &&
                (Header->Signature != FADT_SIGNATURE))
            {
                RtlCopyMemory(OemId, Header->OEMID, sizeof(Header->OEMID));
                OemId[sizeof(Header->OEMID)] = '\0';
                RtlCopyMemory(OemTableId, Header->OEMTableID, sizeof(Header->OEMTableID));
                OemTableId[sizeof(Header->OEMTableID)] = '\0';
                RtlCopyMemory(CreatorId, Header->CreatorID, sizeof(Header->CreatorID));
                CreatorId[sizeof(Header->CreatorID)] = '\0';

                DPRINT1("ACPI: %c%c%c%c %016I64x %06lx (v%02u %s %s %08lx %s %08lx)\n",
                        Header->Signature & 0xFF,
                        (Header->Signature & 0xFF00) >> 8,
                        (Header->Signature & 0xFF0000) >> 16,
                        (Header->Signature & 0xFF000000) >> 24,
                        (unsigned long long)PhysicalAddress.QuadPart,
                        (ULONG)Header->Length,
                        (ULONG)Header->Revision,
                        OemId,
                        OemTableId,
                        Header->OEMRevision,
                        CreatorId,
                        Header->CreatorRev);
            }

            HalpUnmapVirtualAddress(Header, 2);
        }
    }
}
