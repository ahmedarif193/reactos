/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware detection routines
 * COPYRIGHT:   Copyright 2022 Justin Miller <justinmiller100@gmail.com>
 */

/* INCLUDES ******************************************************************/

#include <uefildr.h>

#include <debug.h>
#include <drivers/acpi/acpi.h>
#include <arch/pc/pcbios.h>
#include <arch/pc/hardware.h>

DBG_DEFAULT_CHANNEL(WARNING);

/* Signature for the Boot Graphics Resource Table ("BGRT"). */
#define BGRT_SIGNATURE 0x54524742

/* SMBIOS structures are defined in <reactos/arc/loaderblk.h> included via pcbios.h */

/* GLOBALS *******************************************************************/

extern EFI_SYSTEM_TABLE * GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern UCHAR PcBiosDiskCount;
extern EFI_MEMORY_DESCRIPTOR* EfiMemoryMap;
extern UINT32 FreeldrDescCount;

BOOLEAN AcpiPresent = FALSE;

/* Cached BGRT table information (if present). */
static PBGRT_TABLE BgrtTable = NULL;

/* FUNCTIONS *****************************************************************/

BOOLEAN IsAcpiPresent(VOID)
{
    return AcpiPresent;
}

static
PRSDP_DESCRIPTOR
FindAcpiBios(VOID)
{
    UINTN i;
    RSDP_DESCRIPTOR* rsdp = NULL;
    EFI_GUID acpi2_guid = EFI_ACPI_20_TABLE_GUID;
    EFI_GUID acpi1_guid = EFI_ACPI_TABLE_GUID; /* Some firmware only advertises ACPI 1.0 here */

    /* Prefer ACPI 2.0+ */
    for (i = 0; i < GlobalSystemTable->NumberOfTableEntries; i++)
    {
        if (!memcmp(&GlobalSystemTable->ConfigurationTable[i].VendorGuid,
                    &acpi2_guid, sizeof(acpi2_guid)))
        {
            rsdp = (RSDP_DESCRIPTOR*)GlobalSystemTable->ConfigurationTable[i].VendorTable;
            if (rsdp)
                return rsdp;
        }
    }

    /* Fallback to ACPI 1.0 */
    for (i = 0; i < GlobalSystemTable->NumberOfTableEntries; i++)
    {
        if (!memcmp(&GlobalSystemTable->ConfigurationTable[i].VendorGuid,
                    &acpi1_guid, sizeof(acpi1_guid)))
        {
            rsdp = (RSDP_DESCRIPTOR*)GlobalSystemTable->ConfigurationTable[i].VendorTable;
            if (rsdp)
                return rsdp;
        }
    }

    return NULL;
}

/* Locate the BGRT table in the ACPI structures if one is present. */
static
PBGRT_TABLE
FindBgrtTable(
    _In_ PRSDP_DESCRIPTOR Rsdp)
{
    PDESCRIPTION_HEADER Header;
    PBGRT_TABLE Bgrt = NULL;
    ULONG *Tables;
    ULONGLONG *Tables64;
    ULONG TableCount;
    ULONG i;
    
    if (!Rsdp)
        return NULL;
    
    TRACE("Looking for BGRT table in ACPI tables\n");
    
    // Use XSDT for ACPI 2.0+, RSDT for ACPI 1.0
    if (Rsdp->revision > 0 && Rsdp->xsdt_physical_address)
    {
        // ACPI 2.0+ - Use XSDT
        PXSDT Xsdt = (PXSDT)(ULONG_PTR)Rsdp->xsdt_physical_address;
        if (!Xsdt)
        {
            TRACE("XSDT is NULL\n");
            return NULL;
        }
        
        // Calculate number of tables
        TableCount = (Xsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) / sizeof(ULONGLONG);
        Tables64 = (ULONGLONG *)((ULONG_PTR)Xsdt + sizeof(DESCRIPTION_HEADER));
        
        TRACE("XSDT has %lu tables\n", TableCount);
        
        // Search for BGRT table
        for (i = 0; i < TableCount; i++)
        {
            Header = (PDESCRIPTION_HEADER)(ULONG_PTR)Tables64[i];
            if (Header && Header->Signature == BGRT_SIGNATURE)
            {
                Bgrt = (PBGRT_TABLE)Header;
                TRACE("Found BGRT table at %p (from XSDT)\n", Bgrt);
                break;
            }
        }
    }
    else if (Rsdp->rsdt_physical_address)
    {
        // ACPI 1.0 - Use RSDT
        PRSDT Rsdt = (PRSDT)(ULONG_PTR)Rsdp->rsdt_physical_address;
        if (!Rsdt)
        {
            TRACE("RSDT is NULL\n");
            return NULL;
        }
        
        // Calculate number of tables
        TableCount = (Rsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) / sizeof(ULONG);
        Tables = (ULONG *)((ULONG_PTR)Rsdt + sizeof(DESCRIPTION_HEADER));
        
        TRACE("RSDT has %lu tables\n", TableCount);
        
        // Search for BGRT table
        for (i = 0; i < TableCount; i++)
        {
            Header = (PDESCRIPTION_HEADER)(ULONG_PTR)Tables[i];
            if (Header && Header->Signature == BGRT_SIGNATURE)
            {
                Bgrt = (PBGRT_TABLE)Header;
                TRACE("Found BGRT table at %p (from RSDT)\n", Bgrt);
                break;
            }
        }
    }
    
    // Validate and log BGRT information if found
    if (Bgrt)
    {
        TRACE("BGRT Version: %u\n", Bgrt->Version);
        TRACE("BGRT Status: 0x%02X\n", Bgrt->Status);
        TRACE("BGRT Image Type: %u\n", Bgrt->ImageType);
        TRACE("BGRT Logo Address: 0x%llX\n", Bgrt->LogoAddress);
        TRACE("BGRT Logo Position: (%lu, %lu)\n", Bgrt->OffsetX, Bgrt->OffsetY);
        
        // Validate that the image is valid
        if (!(Bgrt->Status & 0x01))
        {
            TRACE("BGRT image is not valid (status bit 0 not set)\n");
            return NULL;
        }
    }
    else
    {
        TRACE("BGRT table not found\n");
    }
    
    return Bgrt;
}

VOID
DetectAcpiBios(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber)
{
    PCONFIGURATION_COMPONENT_DATA BiosKey;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PRSDP_DESCRIPTOR Rsdp;
    PACPI_BIOS_DATA AcpiBiosData;
    ULONG TableSize;

    Rsdp = FindAcpiBios();

    if (Rsdp)
    {
        /* Set up the flag in the loader block */
        AcpiPresent = TRUE;

        /* Calculate the table size */
        TableSize = FreeldrDescCount * sizeof(BIOS_MEMORY_MAP) +
            sizeof(ACPI_BIOS_DATA) - sizeof(BIOS_MEMORY_MAP);

        /* Set 'Configuration Data' value */
        PartialResourceList = FrLdrHeapAlloc(sizeof(CM_PARTIAL_RESOURCE_LIST) +
                                             TableSize, TAG_HW_RESOURCE_LIST);
        if (PartialResourceList == NULL)
        {
            ERR("Failed to allocate resource descriptor\n");
            return;
        }

        RtlZeroMemory(PartialResourceList, sizeof(CM_PARTIAL_RESOURCE_LIST) + TableSize);
        PartialResourceList->Version = 0;
        PartialResourceList->Revision = 0;
        PartialResourceList->Count = 1;

        PartialDescriptor = &PartialResourceList->PartialDescriptors[0];
        PartialDescriptor->Type = CmResourceTypeDeviceSpecific;
        PartialDescriptor->ShareDisposition = CmResourceShareUndetermined;
        PartialDescriptor->u.DeviceSpecificData.DataSize = TableSize;

        /* Fill the table */
        AcpiBiosData = (PACPI_BIOS_DATA)&PartialResourceList->PartialDescriptors[1];

        AcpiBiosData->RSDPAddress.QuadPart = (ULONGLONG)(UINTN)Rsdp;

        if (Rsdp->revision > 0)
        {
            TRACE("ACPI >1.0, using XSDT address\n");
            AcpiBiosData->RSDTAddress.QuadPart = Rsdp->xsdt_physical_address;
        }
        else
        {
            TRACE("ACPI 1.0, using RSDT address\n");
            AcpiBiosData->RSDTAddress.LowPart = Rsdp->rsdt_physical_address;
        }

        AcpiBiosData->Count = FreeldrDescCount;
        memcpy(AcpiBiosData->MemoryMap, EfiMemoryMap,
            FreeldrDescCount * sizeof(BIOS_MEMORY_MAP));

        TRACE("RSDT %p, data size %x\n", Rsdp->rsdt_physical_address, TableSize);

        /* Create new bus key */
        FldrCreateComponentKey(SystemKey,
                               AdapterClass,
                               MultiFunctionAdapter,
                               0x0,
                               0x0,
                               0xFFFFFFFF,
                               "ACPI BIOS",
                               PartialResourceList,
                               sizeof(CM_PARTIAL_RESOURCE_LIST) + TableSize,
                               &BiosKey);

        /* Increment bus number */
        (*BusNumber)++;
    }
}

/*
 * Detect SMBIOS and create a component key to pass the table address to the kernel.
 * This is necessary for UEFI systems where SMBIOS is not in the legacy 0xF0000 range.
 */
VOID
DetectSmbios(PCONFIGURATION_COMPONENT_DATA SystemKey, ULONG *BusNumber)
{
    PCONFIGURATION_COMPONENT_DATA BiosKey;
    PCM_PARTIAL_RESOURCE_LIST PartialResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PSMBIOS_BIOS_DATA SmbiosBiosData;
    EFI_GUID Smbios3Guid = SMBIOS3_TABLE_GUID;
    EFI_GUID SmbiosGuid = SMBIOS_TABLE_GUID;
    UINTN Index;
    ULONGLONG EntryPointAddress = 0;
    ULONGLONG TableAddress = 0;
    ULONG TableSize = 0;
    UCHAR MajorVersion = 0;
    UCHAR MinorVersion = 0;
    UCHAR DmiRevision = 0;

    if (!GlobalSystemTable)
        return;

    /* Find SMBIOS entry point in UEFI configuration tables */
    for (Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; ++Index)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        /* Prefer SMBIOS 3.0 (64-bit) */
        if (!memcmp(&Entry->VendorGuid, &Smbios3Guid, sizeof(EFI_GUID)))
        {
            PSMBIOS3_ENTRY_POINT Entry3 = (PSMBIOS3_ENTRY_POINT)Entry->VendorTable;
            if (Entry3 && Entry3->TableAddress != 0)
            {
                EntryPointAddress = (ULONGLONG)(UINTN)Entry3;
                TableAddress = Entry3->TableAddress;
                TableSize = Entry3->MaxStructureSize;
                MajorVersion = Entry3->MajorVersion;
                MinorVersion = Entry3->MinorVersion;
                DmiRevision = 3;
                TRACE("Found SMBIOS 3.0 entry point at %p, table at 0x%llX\n",
                      Entry3, TableAddress);
                break;
            }
        }

        /* Fallback to SMBIOS 2.x (32-bit) */
        if (!memcmp(&Entry->VendorGuid, &SmbiosGuid, sizeof(EFI_GUID)))
        {
            PSMBIOS_ENTRY_POINT Entry2 = (PSMBIOS_ENTRY_POINT)Entry->VendorTable;
            if (Entry2 && Entry2->TableAddress != 0)
            {
                EntryPointAddress = (ULONGLONG)(UINTN)Entry2;
                TableAddress = (ULONGLONG)Entry2->TableAddress;
                TableSize = Entry2->TableLength;
                MajorVersion = Entry2->MajorVersion;
                MinorVersion = Entry2->MinorVersion;
                DmiRevision = 2;
                TRACE("Found SMBIOS 2.x entry point at %p, table at 0x%llX\n",
                      Entry2, TableAddress);
                break;
            }
        }
    }

    if (TableAddress == 0)
    {
        ERR("SMBIOS not found in EFI configuration tables\n");
        return;
    }

    ERR("DetectSmbios: Found SMBIOS %u.%u at 0x%llX, size 0x%lX\n",
        MajorVersion, MinorVersion, TableAddress, TableSize);

    /* Allocate the partial resource list */
    PartialResourceList = FrLdrHeapAlloc(sizeof(CM_PARTIAL_RESOURCE_LIST) +
                                         sizeof(SMBIOS_BIOS_DATA),
                                         TAG_HW_RESOURCE_LIST);
    if (PartialResourceList == NULL)
    {
        ERR("Failed to allocate resource descriptor for SMBIOS\n");
        return;
    }

    RtlZeroMemory(PartialResourceList,
                  sizeof(CM_PARTIAL_RESOURCE_LIST) + sizeof(SMBIOS_BIOS_DATA));
    PartialResourceList->Version = 0;
    PartialResourceList->Revision = 0;
    PartialResourceList->Count = 1;

    PartialDescriptor = &PartialResourceList->PartialDescriptors[0];
    PartialDescriptor->Type = CmResourceTypeDeviceSpecific;
    PartialDescriptor->ShareDisposition = CmResourceShareUndetermined;
    PartialDescriptor->u.DeviceSpecificData.DataSize = sizeof(SMBIOS_BIOS_DATA);

    /* Fill the SMBIOS data structure */
    SmbiosBiosData = (PSMBIOS_BIOS_DATA)&PartialResourceList->PartialDescriptors[1];
    SmbiosBiosData->EntryPointAddress.QuadPart = EntryPointAddress;
    SmbiosBiosData->TableAddress.QuadPart = TableAddress;
    SmbiosBiosData->TableSize = TableSize;
    SmbiosBiosData->MajorVersion = MajorVersion;
    SmbiosBiosData->MinorVersion = MinorVersion;
    SmbiosBiosData->DmiRevision = DmiRevision;

    TRACE("SMBIOS %u.%u (DMI rev %u), table size 0x%lX\n",
          MajorVersion, MinorVersion, DmiRevision, TableSize);

    /* Create new bus key for SMBIOS */
    FldrCreateComponentKey(SystemKey,
                           AdapterClass,
                           MultiFunctionAdapter,
                           0x0,
                           0x0,
                           0xFFFFFFFF,
                           "SMBIOS",
                           PartialResourceList,
                           sizeof(CM_PARTIAL_RESOURCE_LIST) + sizeof(SMBIOS_BIOS_DATA),
                           &BiosKey);

    ERR("DetectSmbios: Created SMBIOS MultiFunctionAdapter key at bus %lu\n", *BusNumber);

    /* Increment bus number */
    (*BusNumber)++;
}

PBGRT_TABLE
GetBgrtTable(VOID)
{
    if (!BgrtTable)
    {
        PRSDP_DESCRIPTOR Rsdp = FindAcpiBios();

        if (!Rsdp)
        {
            TRACE("GetBgrtTable: ACPI RSDP not found while probing for BGRT\n");
            return NULL;
        }

        BgrtTable = FindBgrtTable(Rsdp);
        if (BgrtTable)
        {
            TRACE("GetBgrtTable: BGRT table discovered on-demand\n");
        }
    }

    return BgrtTable;
}

/*
 * Get a string from SMBIOS structure.
 * Strings are stored after the formatted area, null-terminated, double-null at end.
 */
static const CHAR*
SmbiosGetString(PSMBIOS_HEADER Header, UCHAR StringIndex)
{
    const CHAR *Str;
    UCHAR Index;

    if (StringIndex == 0)
        return NULL;

    /* Strings start after the formatted part of the structure */
    Str = (const CHAR *)((UINTN)Header + Header->Length);
    Index = 1;

    while (Index < StringIndex)
    {
        /* Skip to next string */
        while (*Str != '\0')
            Str++;
        Str++;  /* Skip the null terminator */

        /* Check for end of strings (double null) */
        if (*Str == '\0')
            return NULL;

        Index++;
    }

    return (*Str != '\0') ? Str : NULL;
}

/*
 * Move to next SMBIOS structure.
 */
static PSMBIOS_HEADER
SmbiosNextStructure(PSMBIOS_HEADER Header)
{
    const CHAR *Str;

    /* Strings start after the formatted part */
    Str = (const CHAR *)((UINTN)Header + Header->Length);

    /* Skip all strings (null-terminated, double-null at end) */
    while (*Str != '\0' || *(Str + 1) != '\0')
        Str++;

    /* Skip the double null terminator */
    Str += 2;

    return (PSMBIOS_HEADER)Str;
}

/*
 * Get system identifier from SMBIOS Type 1 (System Information).
 * Returns TRUE if successful, FALSE otherwise.
 * The output buffer will contain "Manufacturer ProductName" string.
 */
static BOOLEAN
GetSmbiosSystemIdentifier(
    _Out_writes_(BufferSize) PCHAR Buffer,
    _In_ SIZE_T BufferSize)
{
    EFI_GUID Smbios3Guid = SMBIOS3_TABLE_GUID;
    EFI_GUID SmbiosGuid = SMBIOS_TABLE_GUID;
    PSMBIOS_HEADER CurrentHeader;
    ULONGLONG TableAddress = 0;
    UINTN Index;
    SIZE_T Len;

    if (!GlobalSystemTable || !Buffer || BufferSize == 0)
        return FALSE;

    /* Find SMBIOS entry point in UEFI configuration tables */
    for (Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; ++Index)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        /* Prefer SMBIOS 3.0 (64-bit) */
        if (!memcmp(&Entry->VendorGuid, &Smbios3Guid, sizeof(EFI_GUID)))
        {
            PSMBIOS3_ENTRY_POINT Entry3 = (PSMBIOS3_ENTRY_POINT)Entry->VendorTable;
            if (Entry3 && Entry3->TableAddress != 0)
            {
                TableAddress = Entry3->TableAddress;
                TRACE("Found SMBIOS 3.0 table at 0x%llX\n", TableAddress);
                break;
            }
        }

        /* Fallback to SMBIOS 2.x (32-bit) */
        if (!memcmp(&Entry->VendorGuid, &SmbiosGuid, sizeof(EFI_GUID)))
        {
            PSMBIOS_ENTRY_POINT Entry2 = (PSMBIOS_ENTRY_POINT)Entry->VendorTable;
            if (Entry2 && Entry2->TableAddress != 0)
            {
                TableAddress = (ULONGLONG)Entry2->TableAddress;
                TRACE("Found SMBIOS 2.x table at 0x%llX\n", TableAddress);
                break;
            }
        }
    }

    if (TableAddress == 0)
    {
        ERR("GetSmbiosSystemIdentifier: SMBIOS table not found in EFI config tables\n");
        return FALSE;
    }

    ERR("GetSmbiosSystemIdentifier: Found SMBIOS table at 0x%llX\n", TableAddress);

    /* Walk SMBIOS structures looking for System Information (Type 1) */
    CurrentHeader = (PSMBIOS_HEADER)(UINTN)TableAddress;

    /* Limit iterations to prevent infinite loops */
    for (Index = 0; Index < 256 && CurrentHeader->Type != 127; ++Index)
    {
        if (CurrentHeader->Type == 1)  /* System Information */
        {
            PSMBIOS_SYSTEM_INFO SysInfo = (PSMBIOS_SYSTEM_INFO)CurrentHeader;
            const CHAR *Manufacturer = SmbiosGetString(&SysInfo->Header, SysInfo->Manufacturer);
            const CHAR *ProductName = SmbiosGetString(&SysInfo->Header, SysInfo->ProductName);

            ERR("GetSmbiosSystemIdentifier: Found Type 1 at %p\n", SysInfo);
            ERR("GetSmbiosSystemIdentifier: Manufacturer index=%u, ProductName index=%u\n",
                SysInfo->Manufacturer, SysInfo->ProductName);
            ERR("GetSmbiosSystemIdentifier: Manufacturer='%s'\n", Manufacturer ? Manufacturer : "(null)");
            ERR("GetSmbiosSystemIdentifier: ProductName='%s'\n", ProductName ? ProductName : "(null)");

            /* Build the identifier string */
            Buffer[0] = '\0';

            if (Manufacturer && Manufacturer[0] != '\0')
            {
                Len = strlen(Manufacturer);
                if (Len >= BufferSize)
                    Len = BufferSize - 1;
                memcpy(Buffer, Manufacturer, Len);
                Buffer[Len] = '\0';

                /* Add space separator if we have product name too */
                if (ProductName && ProductName[0] != '\0' && Len + 1 < BufferSize)
                {
                    Buffer[Len] = ' ';
                    Buffer[Len + 1] = '\0';
                }
            }

            if (ProductName && ProductName[0] != '\0')
            {
                Len = strlen(Buffer);
                if (Len < BufferSize - 1)
                {
                    SIZE_T ProductLen = strlen(ProductName);
                    if (Len + ProductLen >= BufferSize)
                        ProductLen = BufferSize - Len - 1;
                    memcpy(Buffer + Len, ProductName, ProductLen);
                    Buffer[Len + ProductLen] = '\0';
                }
            }

            /* Return success if we got at least some info */
            if (Buffer[0] != '\0')
            {
                TRACE("System identifier: %s\n", Buffer);
                return TRUE;
            }

            break;
        }

        CurrentHeader = SmbiosNextStructure(CurrentHeader);
    }

    TRACE("SMBIOS Type 1 not found or empty\n");
    return FALSE;
}

PCONFIGURATION_COMPONENT_DATA
UefiHwDetect(
    _In_opt_ PCSTR Options)
{
    PCONFIGURATION_COMPONENT_DATA SystemKey;
    ULONG BusNumber = 0;
    CHAR SmbiosIdentifier[128];

    TRACE("DetectHardware()\n");

    /*
     * Create the 'System' key.
     * Try to get system identifier from SMBIOS (Manufacturer + ProductName).
     * This provides a descriptive identifier like "Dell Inc. OptiPlex 7010"
     * instead of a generic architecture string.
     * SMBIOS is available via EFI configuration tables on all UEFI platforms.
     */
    if (GetSmbiosSystemIdentifier(SmbiosIdentifier, sizeof(SmbiosIdentifier)))
    {
        ERR("UefiHwDetect: Using SMBIOS system identifier: '%s'\n", SmbiosIdentifier);
        FldrCreateSystemKey(&SystemKey, SmbiosIdentifier);
    }
    else
    {
        /* Fallback to architecture-specific generic identifier */
#if defined(_M_IX86) || defined(_M_AMD64)
        ERR("UefiHwDetect: SMBIOS failed, using fallback 'AT/AT COMPATIBLE'\n");
        FldrCreateSystemKey(&SystemKey, "AT/AT COMPATIBLE");
#elif defined(_M_IA64)
        ERR("UefiHwDetect: SMBIOS failed, using fallback 'Intel Itanium processor family'\n");
        FldrCreateSystemKey(&SystemKey, "Intel Itanium processor family");
#elif defined(_M_ARM) || defined(_M_ARM64) || defined(__aarch64__)
        ERR("UefiHwDetect: SMBIOS failed, using fallback 'ARM processor family'\n");
        FldrCreateSystemKey(&SystemKey, "ARM processor family");
#else
        #error Please define a system key for your architecture
#endif
    }

    /* Detect ACPI */
    DetectAcpiBios(SystemKey, &BusNumber);

    /* Detect SMBIOS and pass table address to kernel */
    DetectSmbios(SystemKey, &BusNumber);

    if (AcpiPresent)
    {
        PRSDP_DESCRIPTOR Rsdp = FindAcpiBios();
        if (Rsdp)
        {
            BgrtTable = FindBgrtTable(Rsdp);
            if (BgrtTable)
            {
                TRACE("BGRT table found and stored for later use\n");
            }
        }
    }

    TRACE("DetectHardware() Done\n");
    return SystemKey;
}
