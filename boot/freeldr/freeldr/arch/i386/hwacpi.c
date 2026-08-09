/*
 *  FreeLoader
 *
 *  Copyright (C) 2004  Eric Kohl
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <freeldr.h>
#include <reactos/drivers/acpi/acpi.h>

#include <debug.h>
DBG_DEFAULT_CHANNEL(HWDETECT);

BOOLEAN AcpiPresent = FALSE;

BOOLEAN IsAcpiPresent(VOID)
{
    return AcpiPresent;
}

static PRSDP_DESCRIPTOR
FindAcpiBios(VOID)
{
    PUCHAR Ptr;

    /* Find the 'Root System Descriptor Table Pointer' */
    Ptr = (PUCHAR)0xE0000;
    while ((ULONG_PTR)Ptr < 0x100000)
    {
        if (!memcmp(Ptr, "RSD PTR ", 8))
        {
            TRACE("ACPI supported\n");

            return (PRSDP_DESCRIPTOR)Ptr;
        }

        Ptr = (PUCHAR)((ULONG_PTR)Ptr + 0x10);
    }

    TRACE("ACPI not supported\n");

    return NULL;
}


VOID
DetectAcpiBios(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber)
{
    PCONFIGURATION_COMPONENT_DATA BiosKey;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PRSDP_DESCRIPTOR Rsdp;
    PACPI_BIOS_MULTI_NODE AcpiBiosData;
    ULONG TableSize, Size, Index;

    Rsdp = FindAcpiBios();

    if (Rsdp)
    {
        /* Set up the flag in the loader block */
        AcpiPresent = TRUE;

        /* Calculate the table size */
        TableSize = FIELD_OFFSET(ACPI_BIOS_MULTI_NODE, E820Entry) +
                    PcBiosMapCount * sizeof(ACPI_E820_ENTRY);

        /* Set 'Configuration Data' value */
        Size = FIELD_OFFSET(CM_PARTIAL_RESOURCE_LIST, PartialDescriptors[1]) + TableSize;
        PartialResourceList = FrLdrHeapAlloc(Size, TAG_HW_RESOURCE_LIST);
        if (PartialResourceList == NULL)
        {
            ERR("Failed to allocate resource descriptor\n");
            return;
        }

        RtlZeroMemory(PartialResourceList, Size);
        PartialResourceList->Version = 0;
        PartialResourceList->Revision = 0;
        PartialResourceList->Count = 1;

        PartialDescriptor = &PartialResourceList->PartialDescriptors[0];
        PartialDescriptor->Type = CmResourceTypeDeviceSpecific;
        PartialDescriptor->ShareDisposition = CmResourceShareUndetermined;
        PartialDescriptor->u.DeviceSpecificData.DataSize = TableSize;

        /* Fill the table */
        AcpiBiosData = (PACPI_BIOS_MULTI_NODE)(PartialDescriptor + 1);
        AcpiBiosData->RsdpAddress.QuadPart = (ULONG_PTR)Rsdp;

        if (Rsdp->revision > 0)
        {
            TRACE("ACPI >1.0, using XSDT address\n");
            AcpiBiosData->RsdtAddress.QuadPart = Rsdp->xsdt_physical_address;
        }
        else
        {
            TRACE("ACPI 1.0, using RSDT address\n");
            AcpiBiosData->RsdtAddress.LowPart = Rsdp->rsdt_physical_address;
        }

        AcpiBiosData->Count = PcBiosMapCount;
        for (Index = 0; Index < PcBiosMapCount; ++Index)
        {
            AcpiBiosData->E820Entry[Index].Base.QuadPart = PcBiosMemoryMap[Index].BaseAddress;
            AcpiBiosData->E820Entry[Index].Length.QuadPart = PcBiosMemoryMap[Index].Length;
            AcpiBiosData->E820Entry[Index].Type = PcBiosMemoryMap[Index].Type;
        }

        TRACE("RSDT %p, data size %x\n", Rsdp->rsdt_physical_address,
            TableSize);

        /* Create new bus key */
        FldrCreateComponentKey(SystemKey,
                               AdapterClass,
                               MultiFunctionAdapter,
                               0,
                               0,
                               0xFFFFFFFF,
                               "ACPI BIOS",
                               PartialResourceList,
                               Size,
                               &BiosKey);

        /* Increment bus number */
        (*BusNumber)++;
    }
}

/* EOF */
