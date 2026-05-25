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

#include <reactos/arm64/early_uart.h>

/*
 * Global runtime-detected UART state.
 * These are accessed by the inline functions in early_uart.h.
 */
volatile UINT64 EarlyUartBaseAddress = 0;
volatile ARM64_PLATFORM_ID EarlyUartPlatformId = Arm64PlatformUnknown;
volatile ARM64_UART_INTERFACE EarlyUartInterface = Arm64UartUnknown;
volatile BOOLEAN EarlyUartInitialized = FALSE;

/* External UEFI globals */
extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/*
 * SPCR table definitions for ACPI Serial Port Console Redirection.
 */
#define SPCR_SIGNATURE          0x52435053  /* "SPCR" */
#define DBG2_SIGNATURE          0x32474244  /* "DBG2" */

/*
 * Serial sub-types per Microsoft DBG2 spec (mirrored in ACPICA actbl1.h).
 * DBG2.PortSubtype and modern SPCR.InterfaceType share this namespace.
 *
 * Note: very old SPCR firmware uses InterfaceType=0x0E for ARM PL011 (the
 * legacy SPCR encoding, predating the DBG2 unification). We accept both
 * 0x03 (current) and 0x0E (legacy SBSA-generic, used in the wild for
 * PL011-compatible UARTs) below.
 */
#define SERIAL_SUBTYPE_16550_COMPATIBLE 0x0000
#define SERIAL_SUBTYPE_16550_SUBSET     0x0001
#define SERIAL_SUBTYPE_ARM_PL011        0x0003
#define SERIAL_SUBTYPE_NS16550_NV       0x0005
#define SERIAL_SUBTYPE_ARM_SBSA_32      0x000D
#define SERIAL_SUBTYPE_ARM_SBSA_GENERIC 0x000E /* also legacy SPCR PL011 */
#define SERIAL_SUBTYPE_BCM2835          0x0010
#define SERIAL_SUBTYPE_16550_WITH_GAS   0x0012

#define DBG2_PORT_TYPE_SERIAL           0x8000

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

/* ACPI DBG2 (Debug Port Table 2) - Microsoft spec */
typedef struct _DBG2_TABLE
{
    DESCRIPTION_HEADER Header;
    ULONG OffsetDbgDeviceInfo;
    ULONG NumberDbgDeviceInfo;
} DBG2_TABLE, *PDBG2_TABLE;

typedef struct _DBG2_DEVICE_INFO
{
    UCHAR  Revision;
    USHORT Length;
    UCHAR  NumberOfGenericAddressRegisters;
    USHORT NameSpaceStringLength;
    USHORT NameSpaceStringOffset;
    USHORT OemDataLength;
    USHORT OemDataOffset;
    USHORT PortType;
    USHORT PortSubtype;
    USHORT Reserved;
    USHORT BaseAddressRegisterOffset;
    USHORT AddressSizeOffset;
} DBG2_DEVICE_INFO, *PDBG2_DEVICE_INFO;
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
 * Generic ACPI table lookup. Walks XSDT first (ACPI 2.0+), falls back to RSDT.
 * Returns a pointer to the table header, or NULL.
 */
static PDESCRIPTION_HEADER
EarlyUartFindAcpiTable(ULONG Signature)
{
    PRSDP Rsdp = EarlyUartLocateRsdp();
    if (!Rsdp)
        return NULL;

    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress.QuadPart != 0)
    {
        PXSDT Xsdt = (PXSDT)(UINTN)Rsdp->XsdtAddress.QuadPart;
        if (Xsdt && Xsdt->Header.Length > sizeof(DESCRIPTION_HEADER))
        {
            ULONG EntryCount = (Xsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) /
                               sizeof(PHYSICAL_ADDRESS);
            for (ULONG i = 0; i < EntryCount; ++i)
            {
                UINTN TablePa = (UINTN)Xsdt->Tables[i].QuadPart;
                if (TablePa == 0)
                    continue;
                PDESCRIPTION_HEADER Hdr = (PDESCRIPTION_HEADER)TablePa;
                if (Hdr->Signature == Signature)
                    return Hdr;
            }
        }
    }

    if (Rsdp->RsdtAddress != 0)
    {
        PRSDT Rsdt = (PRSDT)(UINTN)Rsdp->RsdtAddress;
        if (Rsdt && Rsdt->Header.Length > sizeof(DESCRIPTION_HEADER))
        {
            ULONG EntryCount = (Rsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) /
                               sizeof(ULONG);
            for (ULONG i = 0; i < EntryCount; ++i)
            {
                UINTN TablePa = (UINTN)Rsdt->Tables[i];
                if (TablePa == 0)
                    continue;
                PDESCRIPTION_HEADER Hdr = (PDESCRIPTION_HEADER)TablePa;
                if (Hdr->Signature == Signature)
                    return Hdr;
            }
        }
    }

    return NULL;
}

/*
 * Map a serial port subtype (used by both SPCR.InterfaceType and
 * DBG2.PortSubtype - they share Microsoft's serial subtype namespace) to an
 * ARM64_UART_INTERFACE. Unknown subtypes return Arm64UartUnknown so the
 * caller leaves the UART disabled rather than poking foreign registers as
 * if they were PL011.
 */
static ARM64_UART_INTERFACE
EarlyUartInterfaceFromSubtype(USHORT Subtype)
{
    switch (Subtype)
    {
        case SERIAL_SUBTYPE_ARM_PL011:
        case SERIAL_SUBTYPE_ARM_SBSA_32:
        case SERIAL_SUBTYPE_ARM_SBSA_GENERIC:
            return Arm64UartPl011;

        case SERIAL_SUBTYPE_16550_COMPATIBLE:
        case SERIAL_SUBTYPE_16550_SUBSET:
        case SERIAL_SUBTYPE_NS16550_NV:
        case SERIAL_SUBTYPE_16550_WITH_GAS:
        case SERIAL_SUBTYPE_BCM2835:    /* RPi mini UART - 16550-ish */
            return Arm64UartNs16550;

        /*
         * Qualcomm MSM/SDM/SM GENI/QUP, i.MX, OMAP, USIF, SAM5250, DCC: all
         * have their own register layouts. TODO: add proper drivers; until
         * then we report Unknown so EarlyUartReady() blocks any I/O and
         * EarlyUartPutc becomes a no-op rather than scribbling on whatever
         * device happens to live at that MMIO base.
         */
        default:
            return Arm64UartUnknown;
    }
}

/*
 * Locate SPCR table from ACPI tables.
 * Returns the UART base address if found, 0 otherwise.
 */
static UINT64
EarlyUartLocateSpcrAddress(_Out_opt_ ARM64_UART_INTERFACE *UartInterface)
{
    PSPCR_TABLE Spcr;

    if (UartInterface)
        *UartInterface = Arm64UartUnknown;

    Spcr = (PSPCR_TABLE)EarlyUartFindAcpiTable(SPCR_SIGNATURE);
    if (!Spcr || Spcr->Header.Length < sizeof(*Spcr))
        return 0;

    if (Spcr->SerialPort.AddressSpaceID != ACPI_GAS_SPACE_SYSTEM_MEMORY)
        return 0;

    if (Spcr->SerialPort.Address.QuadPart == 0)
        return 0;

    if (UartInterface)
        *UartInterface = EarlyUartInterfaceFromSubtype(Spcr->InterfaceType);

    return Spcr->SerialPort.Address.QuadPart;
}

/*
 * Locate the first usable serial entry in DBG2. DBG2 is what most modern
 * Windows-on-ARM firmware populates (Surface family, Qualcomm reference
 * boards) - sometimes in addition to SPCR, sometimes alone. Walked as a
 * fallback when SPCR is missing.
 */
static UINT64
EarlyUartLocateDbg2Address(_Out_opt_ ARM64_UART_INTERFACE *UartInterface)
{
    PDBG2_TABLE Dbg2;
    PUCHAR Cursor;
    PUCHAR TableEnd;
    ULONG Index;

    if (UartInterface)
        *UartInterface = Arm64UartUnknown;

    Dbg2 = (PDBG2_TABLE)EarlyUartFindAcpiTable(DBG2_SIGNATURE);
    if (!Dbg2 || Dbg2->Header.Length < sizeof(*Dbg2))
        return 0;

    TableEnd = (PUCHAR)Dbg2 + Dbg2->Header.Length;
    Cursor = (PUCHAR)Dbg2 + Dbg2->OffsetDbgDeviceInfo;
    if (Cursor < (PUCHAR)Dbg2 || Cursor > TableEnd)
        return 0;

    for (Index = 0; Index < Dbg2->NumberDbgDeviceInfo; ++Index)
    {
        PDBG2_DEVICE_INFO Dev;
        PGEN_ADDR Gas;

        if (Cursor + sizeof(*Dev) > TableEnd)
            break;

        Dev = (PDBG2_DEVICE_INFO)Cursor;

        if (Dev->Length < sizeof(*Dev) || Cursor + Dev->Length > TableEnd)
            break;

        if (Dev->PortType != DBG2_PORT_TYPE_SERIAL ||
            Dev->NumberOfGenericAddressRegisters == 0 ||
            Dev->BaseAddressRegisterOffset == 0 ||
            (USHORT)(Dev->BaseAddressRegisterOffset + sizeof(GEN_ADDR)) > Dev->Length)
        {
            Cursor += Dev->Length;
            continue;
        }

        Gas = (PGEN_ADDR)(Cursor + Dev->BaseAddressRegisterOffset);
        if (Gas->AddressSpaceID != ACPI_GAS_SPACE_SYSTEM_MEMORY ||
            Gas->Address.QuadPart == 0)
        {
            Cursor += Dev->Length;
            continue;
        }

        if (UartInterface)
            *UartInterface = EarlyUartInterfaceFromSubtype(Dev->PortSubtype);

        return Gas->Address.QuadPart;
    }

    return 0;
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
    ARM64_UART_INTERFACE UartInterface = Arm64UartUnknown;

    /* Method 1: ACPI SPCR table */
    SpcrAddress = EarlyUartLocateSpcrAddress(&UartInterface);
    if (SpcrAddress != 0)
    {
        EarlyUartBaseAddress = SpcrAddress;
        EarlyUartInterface = UartInterface;
        /* Match address to a known platform for diagnostic naming */
        if (SpcrAddress == ARM64_UART_QEMU_VIRT)
            EarlyUartPlatformId = Arm64PlatformQemuVirt;
        else if (SpcrAddress == ARM64_UART_RPI3_BCM2837)
            EarlyUartPlatformId = Arm64PlatformRpi3;
        else if (SpcrAddress == ARM64_UART_RPI4_BCM2711)
            EarlyUartPlatformId = Arm64PlatformRpi4;
        else if (SpcrAddress == ARM64_UART_RPI5_BCM2712)
            EarlyUartPlatformId = Arm64PlatformRpi5;
        else
            EarlyUartPlatformId = Arm64PlatformGenericAcpi;
        return EarlyUartPlatformId;
    }

    /* Method 2: ACPI DBG2 table (preferred by modern Windows-on-ARM firmware) */
    SpcrAddress = EarlyUartLocateDbg2Address(&UartInterface);
    if (SpcrAddress != 0)
    {
        EarlyUartBaseAddress = SpcrAddress;
        EarlyUartInterface = UartInterface;
        EarlyUartPlatformId = Arm64PlatformGenericAcpi;
        return EarlyUartPlatformId;
    }

    /* Method 3: SMBIOS system information (RPi/QEMU identification) */
    Platform = EarlyUartDetectFromSmbios();
    if (Platform != Arm64PlatformUnknown)
    {
        EarlyUartPlatformId = Platform;
        EarlyUartBaseAddress = EarlyUartGetAddressForPlatform(Platform);
        EarlyUartInterface = EarlyUartInferInterfaceFromAddress(EarlyUartBaseAddress);
        return EarlyUartPlatformId;
    }

    /*
     * Method 5: nothing detected. Leave the UART disabled rather than
     * guessing - the previous default (QEMU virt PL011 @ 0x09000000) caused
     * a synchronous abort when used on Qualcomm/other SoCs where that PA
     * is not MMIO. EarlyUartReady() returns FALSE and prints no-op.
     */
    EarlyUartPlatformId = Arm64PlatformUnknown;
    EarlyUartBaseAddress = 0;
    EarlyUartInterface = Arm64UartUnknown;

    return EarlyUartPlatformId;
}

/*
 * EarlyUartInitialize - Initialize early UART with runtime detection.
 */
BOOLEAN
EarlyUartInitialize(UINT64 UartBaseOverride)
{
    return EarlyUartInitializeWithInterface(UartBaseOverride, Arm64UartUnknown);
}

BOOLEAN
EarlyUartInitializeWithInterface(
    UINT64 UartBaseOverride,
    ARM64_UART_INTERFACE UartInterfaceOverride)
{
    if (EarlyUartInitialized)
        return TRUE;

    if (UartBaseOverride != 0)
    {
        /* Use provided address (e.g., from loader block in kernel) */
        EarlyUartBaseAddress = UartBaseOverride;
        EarlyUartPlatformId = Arm64PlatformGenericAcpi;  /* Unknown specific platform */
        EarlyUartInterface = (UartInterfaceOverride != Arm64UartUnknown) ?
                             UartInterfaceOverride :
                             EarlyUartInferInterfaceFromAddress(UartBaseOverride);
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
        EarlyUartInterface = Arm64UartPl011;
    }

    if (EarlyUartInterface == Arm64UartUnknown)
        EarlyUartInterface = Arm64UartPl011;

    EarlyUartInitialized = TRUE;

    return TRUE;
}
