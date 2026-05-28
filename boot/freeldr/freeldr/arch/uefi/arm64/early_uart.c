/*
 * PROJECT:     FreeLoader ARM64
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        boot/freeldr/freeldr/arch/uefi/arm64/early_uart.c
 * PURPOSE:     ARM64 early UART discovery via ACPI SPCR and DBG2.
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 *
 * If neither table is present, or the reported PortSubtype has no driver in
 * EarlyUartInterfaceFromSubtype(), the UART stays disabled. No guessing
 * from "known" addresses, no QEMU/RPi defaults - those silently scribble
 * on unrelated MMIO and take a synchronous abort on most platforms.
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
volatile BOOLEAN EarlyUartHardwareInitialized = FALSE;
volatile UINT32 EarlyUartRxCachedBytes = 0;
volatile UINT32 EarlyUartRxCachedByteCount = 0;

/* External UEFI globals */
extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;
extern VOID UefiSerialPutChar(_In_ UCHAR Character);

static VOID
EarlyUartDiagPutChar(_In_ UCHAR Character)
{
    if (Character == '\n')
        UefiSerialPutChar('\r');

    UefiSerialPutChar(Character);
}

static VOID
EarlyUartDiagPuts(_In_ PCSTR String)
{
    if (String == NULL)
        return;

    while (*String)
        EarlyUartDiagPutChar(*String++);
}

static VOID
EarlyUartDiagHex(_In_ UINT64 Value, _In_ ULONG Nibbles)
{
    static const CHAR HexDigits[] = "0123456789ABCDEF";
    LONG Index;

    if (Nibbles > 16)
        Nibbles = 16;

    for (Index = (LONG)Nibbles - 1; Index >= 0; --Index)
        EarlyUartDiagPutChar(HexDigits[(Value >> (Index * 4)) & 0xFULL]);
}

static VOID
EarlyUartDiagDec(_In_ ULONG Value)
{
    CHAR Buffer[11];
    ULONG Position = 0;

    if (Value == 0)
    {
        EarlyUartDiagPutChar('0');
        return;
    }

    while (Value != 0 && Position < sizeof(Buffer))
    {
        Buffer[Position++] = '0' + (Value % 10);
        Value /= 10;
    }

    while (Position != 0)
        EarlyUartDiagPutChar(Buffer[--Position]);
}

static PCSTR
EarlyUartDiagInterfaceName(_In_ ARM64_UART_INTERFACE Interface)
{
    switch (Interface)
    {
        case Arm64UartPl011:
            return "pl011";

        case Arm64UartNs16550:
            return "ns16550";

        case Arm64UartQcomGeni:
            return "qcom-geni";

        case Arm64UartBcm2835Mini:
            return "bcm2835-mini";

        default:
            return "unknown";
    }
}

static UINT32
EarlyUartDiagRead32(_In_ UINT64 Address)
{
    return *(volatile UINT32 *)(ULONG_PTR)Address;
}

static VOID
EarlyUartDiagBcmProbe(_In_ UINT64 Address)
{
    UINT32 AuxRelativeLsr;
    UINT32 MiniUartRelativeLsr;

    AuxRelativeLsr = EarlyUartDiagRead32(Address + ARM64_BCM2835_MINI_UART_LSR);
    MiniUartRelativeLsr = EarlyUartDiagRead32(Address +
                                              ARM64_BCM2835_MINI_UART_LSR -
                                              ARM64_BCM2835_MINI_UART_IO);

    EarlyUartDiagPuts("[EUART] BCM probe: lsr@base+0x");
    EarlyUartDiagHex(ARM64_BCM2835_MINI_UART_LSR, 3);
    EarlyUartDiagPuts("=0x");
    EarlyUartDiagHex(AuxRelativeLsr, 8);
    EarlyUartDiagPuts(" lsr@base+0x");
    EarlyUartDiagHex(ARM64_BCM2835_MINI_UART_LSR -
                     ARM64_BCM2835_MINI_UART_IO, 3);
    EarlyUartDiagPuts("=0x");
    EarlyUartDiagHex(MiniUartRelativeLsr, 8);
    EarlyUartDiagPuts("\n");
}

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
#define SERIAL_SUBTYPE_SDM845_1_8432MHZ 0x0011
#define SERIAL_SUBTYPE_16550_WITH_GAS   0x0012
#define SERIAL_SUBTYPE_SDM845_7_372MHZ  0x0013

#define DBG2_PORT_TYPE_SERIAL           0x8000

#define ACPI_GAS_SPACE_SYSTEM_MEMORY    0
#define ACPI_GAS_SPACE_SYSTEM_IO        1

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
            return Arm64UartNs16550;

        case SERIAL_SUBTYPE_BCM2835:    /* Raspberry Pi AUX mini UART */
            return Arm64UartBcm2835Mini;

        case SERIAL_SUBTYPE_SDM845_1_8432MHZ:
        case SERIAL_SUBTYPE_SDM845_7_372MHZ:
            return Arm64UartQcomGeni;

        /*
         * Older Qualcomm MSM debug UARTs, i.MX, OMAP, USIF, SAM5250, DCC:
         * all have their own register layouts. Keep them disabled until they
         * have explicit drivers.
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

    EarlyUartDiagPuts("[EUART] SPCR: search\n");

    Spcr = (PSPCR_TABLE)EarlyUartFindAcpiTable(SPCR_SIGNATURE);
    if (!Spcr)
    {
        EarlyUartDiagPuts("[EUART] SPCR: not found\n");
        return 0;
    }

    if (Spcr->Header.Length < sizeof(*Spcr))
    {
        EarlyUartDiagPuts("[EUART] SPCR: short length=0x");
        EarlyUartDiagHex(Spcr->Header.Length, 8);
        EarlyUartDiagPuts("\n");
        return 0;
    }

    EarlyUartDiagPuts("[EUART] SPCR: type=0x");
    EarlyUartDiagHex(Spcr->InterfaceType, 2);
    EarlyUartDiagPuts(" space=0x");
    EarlyUartDiagHex(Spcr->SerialPort.AddressSpaceID, 2);
    EarlyUartDiagPuts(" addr=0x");
    EarlyUartDiagHex(Spcr->SerialPort.Address.QuadPart, 16);
    EarlyUartDiagPuts("\n");

    if (Spcr->SerialPort.AddressSpaceID != ACPI_GAS_SPACE_SYSTEM_MEMORY)
    {
        EarlyUartDiagPuts("[EUART] SPCR: skip non-MMIO GAS\n");
        return 0;
    }

    if (Spcr->SerialPort.Address.QuadPart == 0)
    {
        EarlyUartDiagPuts("[EUART] SPCR: skip zero address\n");
        return 0;
    }

    if (UartInterface)
    {
        *UartInterface = EarlyUartInterfaceFromSubtype(Spcr->InterfaceType);
        EarlyUartDiagPuts("[EUART] SPCR: interface=");
        EarlyUartDiagPuts(EarlyUartDiagInterfaceName(*UartInterface));
        EarlyUartDiagPuts("\n");
    }

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

    EarlyUartDiagPuts("[EUART] DBG2: search\n");

    Dbg2 = (PDBG2_TABLE)EarlyUartFindAcpiTable(DBG2_SIGNATURE);
    if (!Dbg2)
    {
        EarlyUartDiagPuts("[EUART] DBG2: not found\n");
        return 0;
    }

    if (Dbg2->Header.Length < sizeof(*Dbg2))
    {
        EarlyUartDiagPuts("[EUART] DBG2: short length=0x");
        EarlyUartDiagHex(Dbg2->Header.Length, 8);
        EarlyUartDiagPuts("\n");
        return 0;
    }

    EarlyUartDiagPuts("[EUART] DBG2: count=");
    EarlyUartDiagDec(Dbg2->NumberDbgDeviceInfo);
    EarlyUartDiagPuts(" offset=0x");
    EarlyUartDiagHex(Dbg2->OffsetDbgDeviceInfo, 8);
    EarlyUartDiagPuts(" length=0x");
    EarlyUartDiagHex(Dbg2->Header.Length, 8);
    EarlyUartDiagPuts("\n");

    TableEnd = (PUCHAR)Dbg2 + Dbg2->Header.Length;
    Cursor = (PUCHAR)Dbg2 + Dbg2->OffsetDbgDeviceInfo;
    if (Cursor < (PUCHAR)Dbg2 || Cursor > TableEnd)
    {
        EarlyUartDiagPuts("[EUART] DBG2: bad device-info offset\n");
        return 0;
    }

    for (Index = 0; Index < Dbg2->NumberDbgDeviceInfo; ++Index)
    {
        PDBG2_DEVICE_INFO Dev;
        PGEN_ADDR Gas;

        if (Cursor + sizeof(*Dev) > TableEnd)
        {
            EarlyUartDiagPuts("[EUART] DBG2: truncated entry header at index ");
            EarlyUartDiagDec(Index);
            EarlyUartDiagPuts("\n");
            break;
        }

        Dev = (PDBG2_DEVICE_INFO)Cursor;

        if (Dev->Length < sizeof(*Dev) || Cursor + Dev->Length > TableEnd)
        {
            EarlyUartDiagPuts("[EUART] DBG2: bad entry length at index ");
            EarlyUartDiagDec(Index);
            EarlyUartDiagPuts(" length=0x");
            EarlyUartDiagHex(Dev->Length, 4);
            EarlyUartDiagPuts("\n");
            break;
        }

        EarlyUartDiagPuts("[EUART] DBG2: entry ");
        EarlyUartDiagDec(Index);
        EarlyUartDiagPuts(" type=0x");
        EarlyUartDiagHex(Dev->PortType, 4);
        EarlyUartDiagPuts(" subtype=0x");
        EarlyUartDiagHex(Dev->PortSubtype, 4);
        EarlyUartDiagPuts(" regs=");
        EarlyUartDiagDec(Dev->NumberOfGenericAddressRegisters);
        EarlyUartDiagPuts(" baroff=0x");
        EarlyUartDiagHex(Dev->BaseAddressRegisterOffset, 4);
        EarlyUartDiagPuts("\n");

        if (Dev->PortType != DBG2_PORT_TYPE_SERIAL ||
            Dev->NumberOfGenericAddressRegisters == 0 ||
            Dev->BaseAddressRegisterOffset == 0 ||
            (USHORT)(Dev->BaseAddressRegisterOffset + sizeof(GEN_ADDR)) > Dev->Length)
        {
            EarlyUartDiagPuts("[EUART] DBG2: skip unusable entry\n");
            Cursor += Dev->Length;
            continue;
        }

        Gas = (PGEN_ADDR)(Cursor + Dev->BaseAddressRegisterOffset);
        EarlyUartDiagPuts("[EUART] DBG2: GAS space=0x");
        EarlyUartDiagHex(Gas->AddressSpaceID, 2);
        EarlyUartDiagPuts(" addr=0x");
        EarlyUartDiagHex(Gas->Address.QuadPart, 16);
        EarlyUartDiagPuts("\n");

        if (Gas->AddressSpaceID != ACPI_GAS_SPACE_SYSTEM_MEMORY ||
            Gas->Address.QuadPart == 0)
        {
            EarlyUartDiagPuts("[EUART] DBG2: skip non-MMIO or zero GAS\n");
            Cursor += Dev->Length;
            continue;
        }

        if (UartInterface)
        {
            *UartInterface = EarlyUartInterfaceFromSubtype(Dev->PortSubtype);
            EarlyUartDiagPuts("[EUART] DBG2: interface=");
            EarlyUartDiagPuts(EarlyUartDiagInterfaceName(*UartInterface));
            EarlyUartDiagPuts("\n");
        }

        return Gas->Address.QuadPart;
    }

    EarlyUartDiagPuts("[EUART] DBG2: no usable serial entry\n");
    return 0;
}

ARM64_PLATFORM_ID
EarlyUartDetectPlatform(VOID)
{
    ARM64_UART_INTERFACE Interface;
    UINT64 Address;

    EarlyUartDiagPuts("[EUART] detect: begin\n");

    Address = EarlyUartLocateSpcrAddress(&Interface);
    if (Address == 0)
        Address = EarlyUartLocateDbg2Address(&Interface);

    if (Address == 0)
    {
        EarlyUartBaseAddress = 0;
        EarlyUartInterface = Arm64UartUnknown;
        EarlyUartPlatformId = Arm64PlatformUnknown;
        EarlyUartDiagPuts("[EUART] detect: no UART\n");
        return EarlyUartPlatformId;
    }

    EarlyUartBaseAddress = Address;
    EarlyUartInterface = Interface;
    EarlyUartPlatformId = Arm64PlatformGenericAcpi;

    EarlyUartDiagPuts("[EUART] detect: base=0x");
    EarlyUartDiagHex(EarlyUartBaseAddress, 16);
    EarlyUartDiagPuts(" interface=");
    EarlyUartDiagPuts(EarlyUartDiagInterfaceName(EarlyUartInterface));
    EarlyUartDiagPuts(" ready=");
    EarlyUartDiagDec(EarlyUartReady() ? 1 : 0);
    EarlyUartDiagPuts("\n");

    if (EarlyUartInterface == Arm64UartBcm2835Mini)
        EarlyUartDiagBcmProbe(EarlyUartBaseAddress);

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
    {
        EarlyUartDiagPuts("[EUART] init: already initialized base=0x");
        EarlyUartDiagHex(EarlyUartBaseAddress, 16);
        EarlyUartDiagPuts(" interface=");
        EarlyUartDiagPuts(EarlyUartDiagInterfaceName(EarlyUartInterface));
        EarlyUartDiagPuts(" ready=");
        EarlyUartDiagDec(EarlyUartReady() ? 1 : 0);
        EarlyUartDiagPuts("\n");
        return TRUE;
    }

    EarlyUartDiagPuts("[EUART] init: override=0x");
    EarlyUartDiagHex(UartBaseOverride, 16);
    EarlyUartDiagPuts(" override-interface=");
    EarlyUartDiagPuts(EarlyUartDiagInterfaceName(UartInterfaceOverride));
    EarlyUartDiagPuts("\n");

    if (UartBaseOverride != 0)
    {
        EarlyUartBaseAddress = UartBaseOverride;
        EarlyUartPlatformId = Arm64PlatformGenericAcpi;
        /* Trust the caller's interface choice. If they didn't know, leave it
         * Unknown - EarlyUartReady() returns FALSE and prints no-op. Don't
         * guess from address: there's no reliable mapping from arbitrary MMIO
         * bases to register layouts. */
        EarlyUartInterface = (UartInterfaceOverride < Arm64UartMax) ?
                             UartInterfaceOverride :
                             Arm64UartUnknown;
    }
    else
    {
        /* Perform runtime detection (sets globals or leaves them at Unknown) */
        EarlyUartDetectPlatform();
    }

    EarlyUartInitialized = TRUE;

    EarlyUartDiagPuts("[EUART] init: done base=0x");
    EarlyUartDiagHex(EarlyUartBaseAddress, 16);
    EarlyUartDiagPuts(" interface=");
    EarlyUartDiagPuts(EarlyUartDiagInterfaceName(EarlyUartInterface));
    EarlyUartDiagPuts(" ready=");
    EarlyUartDiagDec(EarlyUartReady() ? 1 : 0);
    EarlyUartDiagPuts("\n");

    return TRUE;
}
