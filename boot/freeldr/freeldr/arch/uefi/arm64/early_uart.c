/*
 * PROJECT:     FreeLoader ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        boot/freeldr/freeldr/arch/uefi/arm64/early_uart.c
 * PURPOSE:     ARM64 early UART runtime platform detection implementation
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 *
 * DESCRIPTION:
 *   This file implements runtime detection of the ARM64 platform and UART
 *   base address. It queries UEFI configuration tables (ACPI SPCR, SMBIOS)
 *   to determine the correct UART address for the current hardware.
 *
 *   Detection methods (in order of preference):
 *   1. ACPI SPCR (Serial Port Console Redirection) table
 *   2. SMBIOS system information (for Raspberry Pi identification)
 *   3. UEFI memory map probe for known UART addresses
 *   4. Default fallback to QEMU virt address
 */

#include <freeldr.h>
#include <uefildr.h>
#include <debug.h>
#include <drivers/acpi/acpi.h>

/* Define implementation flag to get variable definitions */
#define _EARLY_UART_IMPL
#include <reactos/arm64/early_uart.h>

/*
 * Global runtime-detected UART state.
 * These are accessed by the inline functions in early_uart.h.
 */
volatile UINT64 EarlyUartBaseAddress = 0;
volatile ARM64_PLATFORM_ID EarlyUartPlatformId = Arm64PlatformUnknown;
volatile BOOLEAN EarlyUartInitialized = FALSE;

/* External UEFI globals */
extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/*
 * SPCR table definitions for ACPI Serial Port Console Redirection.
 */
#define SPCR_SIGNATURE          0x52435053  /* "SPCR" */
#define SPCR_INTERFACE_16550    0x00
#define SPCR_INTERFACE_ARM_PL011 0x0E
#define SPCR_INTERFACE_ARM_SBSA 0x0E        /* Same as PL011 for our purposes */

#define ACPI_GAS_SPACE_SYSTEM_MEMORY    0
#define ACPI_GAS_SPACE_SYSTEM_IO        1

/* SMBIOS structures from shared header */
#include <reactos/arc/loaderblk.h>

#pragma pack(push, 1)
/* ACPI SPCR (Serial Port Console Redirection) table */
typedef struct _SPCR_TABLE
{
    DESCRIPTION_HEADER Header;
    UCHAR InterfaceType;
    UCHAR Reserved[3];
    GEN_ADDR SerialPort;
    UCHAR InterruptType;
    UCHAR PcInterrupt;
    ULONG Interrupt;
    UCHAR BaudRate;
    UCHAR Parity;
    UCHAR StopBits;
    UCHAR FlowControl;
    UCHAR TerminalType;
    UCHAR Reserved1;
    USHORT PciDeviceId;
    USHORT PciVendorId;
    UCHAR PciBus;
    UCHAR PciDevice;
    UCHAR PciFunction;
    ULONG PciFlags;
    UCHAR PciSegment;
    ULONG Reserved2;
} SPCR_TABLE, *PSPCR_TABLE;
#pragma pack(pop)

/*
 * Locate RSDP from UEFI configuration tables.
 */
static PRSDP
EarlyUartLocateRsdp(VOID)
{
    EFI_GUID Acpi20Guid = EFI_ACPI_20_TABLE_GUID;
    EFI_GUID Acpi10Guid = ACPI_10_TABLE_GUID;
    UINTN Index;

    if (!GlobalSystemTable)
        return NULL;

    /* Search for ACPI 2.0+ table first */
    for (Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; ++Index)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        if (!memcmp(&Entry->VendorGuid, &Acpi20Guid, sizeof(EFI_GUID)) ||
            !memcmp(&Entry->VendorGuid, &Acpi10Guid, sizeof(EFI_GUID)))
        {
            return (PRSDP)Entry->VendorTable;
        }
    }

    return NULL;
}

/*
 * Locate SPCR table from ACPI tables.
 * Returns the UART base address if found, 0 otherwise.
 */
static UINT64
EarlyUartLocateSpcrAddress(VOID)
{
    PRSDP Rsdp;
    PSPCR_TABLE Spcr = NULL;

    Rsdp = EarlyUartLocateRsdp();
    if (!Rsdp)
        return 0;

    /* Try XSDT first (ACPI 2.0+) */
    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress.QuadPart != 0)
    {
        PXSDT Xsdt = (PXSDT)(UINTN)Rsdp->XsdtAddress.QuadPart;
        if (Xsdt)
        {
            ULONG EntryCount = 0;
            if (Xsdt->Header.Length > sizeof(DESCRIPTION_HEADER))
                EntryCount = (Xsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) / sizeof(PHYSICAL_ADDRESS);

            for (ULONG i = 0; i < EntryCount; ++i)
            {
                UINTN TablePa = (UINTN)Xsdt->Tables[i].QuadPart;
                if (TablePa == 0)
                    continue;

                PSPCR_TABLE Candidate = (PSPCR_TABLE)TablePa;
                if (Candidate->Header.Signature == SPCR_SIGNATURE)
                {
                    Spcr = Candidate;
                    break;
                }
            }
        }
    }

    /* Fallback to RSDT (ACPI 1.0) */
    if (!Spcr && Rsdp->RsdtAddress != 0)
    {
        PRSDT Rsdt = (PRSDT)(UINTN)Rsdp->RsdtAddress;
        if (Rsdt)
        {
            ULONG EntryCount = 0;
            if (Rsdt->Header.Length > sizeof(DESCRIPTION_HEADER))
                EntryCount = (Rsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) / sizeof(ULONG);

            for (ULONG i = 0; i < EntryCount; ++i)
            {
                UINTN TablePa = (UINTN)Rsdt->Tables[i];
                if (TablePa == 0)
                    continue;

                PSPCR_TABLE Candidate = (PSPCR_TABLE)TablePa;
                if (Candidate->Header.Signature == SPCR_SIGNATURE)
                {
                    Spcr = Candidate;
                    break;
                }
            }
        }
    }

    if (!Spcr)
        return 0;

    /* Validate SPCR for PL011-compatible UART */
    if (Spcr->InterfaceType != SPCR_INTERFACE_ARM_PL011 &&
        Spcr->InterfaceType != SPCR_INTERFACE_16550)
    {
        /* Unknown interface type, but try anyway if it's memory-mapped */
    }

    if (Spcr->SerialPort.AddressSpaceID != ACPI_GAS_SPACE_SYSTEM_MEMORY)
    {
        /* Not memory-mapped I/O */
        return 0;
    }

    if (Spcr->SerialPort.Address.QuadPart == 0)
        return 0;

    return Spcr->SerialPort.Address.QuadPart;
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
 * Detect platform from SMBIOS system information.
 * Returns the platform ID if recognized, Arm64PlatformUnknown otherwise.
 */
static ARM64_PLATFORM_ID
EarlyUartDetectFromSmbios(VOID)
{
    EFI_GUID Smbios3Guid = SMBIOS3_TABLE_GUID;
    EFI_GUID SmbiosGuid = SMBIOS_TABLE_GUID;
    PSMBIOS_HEADER CurrentHeader;
    ULONGLONG TableAddress = 0;
    UINTN Index;

    if (!GlobalSystemTable)
        return Arm64PlatformUnknown;

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
                break;
            }
        }
    }

    if (TableAddress == 0)
        return Arm64PlatformUnknown;

    /* Walk SMBIOS structures looking for System Information (Type 1) */
    CurrentHeader = (PSMBIOS_HEADER)(UINTN)TableAddress;

    /* Limit iterations to prevent infinite loops */
    for (ULONG i = 0; i < 256 && CurrentHeader->Type != 127; ++i)
    {
        if (CurrentHeader->Type == 1)  /* System Information */
        {
            PSMBIOS_SYSTEM_INFO SysInfo = (PSMBIOS_SYSTEM_INFO)CurrentHeader;
            const CHAR *Manufacturer = SmbiosGetString(&SysInfo->Header, SysInfo->Manufacturer);
            const CHAR *ProductName = SmbiosGetString(&SysInfo->Header, SysInfo->ProductName);

            /* Check for Raspberry Pi */
            if (Manufacturer && ProductName)
            {
                /* Raspberry Pi firmware sets manufacturer to "Raspberry Pi" */
                if (strstr(Manufacturer, "Raspberry") ||
                    strstr(ProductName, "Raspberry"))
                {
                    /* Identify specific model */
                    if (strstr(ProductName, "Pi 5") ||
                        strstr(ProductName, "2712"))
                    {
                        return Arm64PlatformRpi5;
                    }
                    if (strstr(ProductName, "Pi 4") ||
                        strstr(ProductName, "2711"))
                    {
                        return Arm64PlatformRpi4;
                    }
                    if (strstr(ProductName, "Pi 3") ||
                        strstr(ProductName, "2837"))
                    {
                        return Arm64PlatformRpi3;
                    }
                    /* Unknown Raspberry Pi model, default to Pi 4 */
                    return Arm64PlatformRpi4;
                }

                /* Check for QEMU */
                if (strstr(Manufacturer, "QEMU") ||
                    strstr(ProductName, "QEMU"))
                {
                    return Arm64PlatformQemuVirt;
                }
            }

            break;  /* Found Type 1, done searching */
        }

        CurrentHeader = SmbiosNextStructure(CurrentHeader);
    }

    return Arm64PlatformUnknown;
}

/*
 * Check if a physical address range is mapped in UEFI memory map.
 * Used to probe for known UART addresses.
 */
static BOOLEAN
EarlyUartIsAddressMapped(UINT64 PhysAddr, UINT64 Length)
{
    EFI_BOOT_SERVICES *Bs;
    EFI_STATUS Status;
    EFI_MEMORY_DESCRIPTOR *Map = NULL;
    UINTN MapSize = 0, MapKey = 0, DescSize = 0;
    UINT32 DescVer = 0;
    BOOLEAN Found = FALSE;
    UINTN Count, i;
    UINT64 Start, End;

    if (!GlobalSystemTable || !GlobalSystemTable->BootServices)
        return FALSE;

    Bs = GlobalSystemTable->BootServices;

    /* Get required buffer size */
    Status = Bs->GetMemoryMap(&MapSize, NULL, &MapKey, &DescSize, &DescVer);
    if (Status != EFI_BUFFER_TOO_SMALL && Status != EFI_SUCCESS)
        return FALSE;

    if (DescSize == 0)
        DescSize = sizeof(EFI_MEMORY_DESCRIPTOR);

    /* Add some extra space for map changes */
    MapSize += DescSize * 4;

    Status = Bs->AllocatePool(EfiLoaderData, MapSize, (VOID**)&Map);
    if (EFI_ERROR(Status) || !Map)
        return FALSE;

    Status = Bs->GetMemoryMap(&MapSize, Map, &MapKey, &DescSize, &DescVer);
    if (EFI_ERROR(Status))
    {
        Bs->FreePool(Map);
        return FALSE;
    }

    Count = MapSize / DescSize;
    Start = PhysAddr;
    End = PhysAddr + Length;

    for (i = 0; i < Count; ++i)
    {
        EFI_MEMORY_DESCRIPTOR *Desc = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)Map + i * DescSize);
        UINT64 DescStart = (UINT64)Desc->PhysicalStart;
        UINT64 DescEnd = DescStart + ((UINT64)Desc->NumberOfPages << 12);

        if (Start >= DescStart && End <= DescEnd)
        {
            /* Check if it's a memory-mapped I/O region or reserved */
            if (Desc->Type == EfiMemoryMappedIO ||
                Desc->Type == EfiMemoryMappedIOPortSpace ||
                Desc->Type == EfiReservedMemoryType)
            {
                Found = TRUE;
            }
            break;
        }
    }

    Bs->FreePool(Map);
    return Found;
}

/*
 * Probe known UART addresses to detect platform.
 * This is a fallback when ACPI/SMBIOS don't provide information.
 */
static ARM64_PLATFORM_ID
EarlyUartProbeKnownAddresses(UINT64 *OutUartAddress)
{
    /* Known UART addresses in order of likelihood */
    static const struct {
        UINT64 Address;
        ARM64_PLATFORM_ID Platform;
    } KnownUarts[] = {
        { ARM64_UART_QEMU_VIRT,    Arm64PlatformQemuVirt },
        { ARM64_UART_RPI5_BCM2712, Arm64PlatformRpi5 },
        { ARM64_UART_RPI4_BCM2711, Arm64PlatformRpi4 },
        { ARM64_UART_RPI3_BCM2837, Arm64PlatformRpi3 },
    };

    for (ULONG i = 0; i < sizeof(KnownUarts) / sizeof(KnownUarts[0]); ++i)
    {
        if (EarlyUartIsAddressMapped(KnownUarts[i].Address, 0x1000))
        {
            *OutUartAddress = KnownUarts[i].Address;
            return KnownUarts[i].Platform;
        }
    }

    return Arm64PlatformUnknown;
}

/*
 * Get UART address for a known platform.
 */
static UINT64
EarlyUartGetAddressForPlatform(ARM64_PLATFORM_ID Platform)
{
    switch (Platform)
    {
        case Arm64PlatformQemuVirt:
            return ARM64_UART_QEMU_VIRT;
        case Arm64PlatformRpi3:
            return ARM64_UART_RPI3_BCM2837;
        case Arm64PlatformRpi4:
            return ARM64_UART_RPI4_BCM2711;
        case Arm64PlatformRpi5:
            return ARM64_UART_RPI5_BCM2712;
        case Arm64PlatformGenericAcpi:
        case Arm64PlatformUnknown:
        default:
            return 0;  /* SPCR will provide the address */
    }
}

/*
 * EarlyUartDetectPlatform - Main detection entry point.
 *
 * Detection strategy:
 * 1. Try ACPI SPCR table - gives us exact UART address
 * 2. Try SMBIOS - identifies the board for known platforms
 * 3. Probe memory map for known UART addresses
 * 4. Default to QEMU virt (development fallback)
 */
ARM64_PLATFORM_ID
EarlyUartDetectPlatform(VOID)
{
    UINT64 SpcrAddress;
    ARM64_PLATFORM_ID Platform;
    UINT64 ProbeAddress = 0;

    /* Method 1: ACPI SPCR table */
    SpcrAddress = EarlyUartLocateSpcrAddress();
    if (SpcrAddress != 0)
    {
        /* SPCR found - determine platform from address */
        EarlyUartBaseAddress = SpcrAddress;

        /* Match address to known platform */
        if (SpcrAddress == ARM64_UART_QEMU_VIRT)
        {
            EarlyUartPlatformId = Arm64PlatformQemuVirt;
        }
        else if (SpcrAddress == ARM64_UART_RPI3_BCM2837)
        {
            EarlyUartPlatformId = Arm64PlatformRpi3;
        }
        else if (SpcrAddress == ARM64_UART_RPI4_BCM2711)
        {
            EarlyUartPlatformId = Arm64PlatformRpi4;
        }
        else if (SpcrAddress == ARM64_UART_RPI5_BCM2712)
        {
            EarlyUartPlatformId = Arm64PlatformRpi5;
        }
        else
        {
            /* Unknown UART address from SPCR - use it anyway */
            EarlyUartPlatformId = Arm64PlatformGenericAcpi;
        }

        return EarlyUartPlatformId;
    }

    /* Method 2: SMBIOS system information */
    Platform = EarlyUartDetectFromSmbios();
    if (Platform != Arm64PlatformUnknown)
    {
        EarlyUartPlatformId = Platform;
        EarlyUartBaseAddress = EarlyUartGetAddressForPlatform(Platform);
        return EarlyUartPlatformId;
    }

    /* Method 3: Probe known UART addresses in memory map */
    Platform = EarlyUartProbeKnownAddresses(&ProbeAddress);
    if (Platform != Arm64PlatformUnknown && ProbeAddress != 0)
    {
        EarlyUartPlatformId = Platform;
        EarlyUartBaseAddress = ProbeAddress;
        return EarlyUartPlatformId;
    }

    /* Method 4: Default to QEMU virt for development */
    EarlyUartPlatformId = Arm64PlatformQemuVirt;
    EarlyUartBaseAddress = ARM64_UART_DEFAULT;

    return EarlyUartPlatformId;
}

/*
 * EarlyUartInitialize - Initialize early UART with runtime detection.
 */
BOOLEAN
EarlyUartInitialize(UINT64 UartBaseOverride)
{
    if (EarlyUartInitialized)
        return TRUE;

    if (UartBaseOverride != 0)
    {
        /* Use provided address (e.g., from loader block in kernel) */
        EarlyUartBaseAddress = UartBaseOverride;
        EarlyUartPlatformId = Arm64PlatformGenericAcpi;  /* Unknown specific platform */
    }
    else
    {
        /* Perform runtime detection */
        EarlyUartDetectPlatform();
    }

    if (EarlyUartBaseAddress == 0)
    {
        /* Detection failed, use default */
        EarlyUartBaseAddress = ARM64_UART_DEFAULT;
        EarlyUartPlatformId = Arm64PlatformQemuVirt;
    }

    EarlyUartInitialized = TRUE;

    return TRUE;
}
