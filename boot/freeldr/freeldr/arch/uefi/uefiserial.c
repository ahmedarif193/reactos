/*
 * UEFI Serial I/O Protocol Support
 * Provides serial output via UEFI Serial I/O Protocol
 * With PL011 UART fallback for ARM64 systems
 */

#include <freeldr.h>

//#if defined(_M_ARM64) || defined(__aarch64__)

#include <uefildr.h>
#include <arch/uefi/SerialIo.h>
#include <debug.h>
#if defined(_M_ARM64) || defined(__aarch64__)
#include <drivers/acpi/acpi.h>
#endif

extern EFI_SYSTEM_TABLE* GlobalSystemTable;
extern EFI_HANDLE GlobalImageHandle;

/* Serial I/O Protocol instance */
static EFI_SERIAL_IO_PROTOCOL* SerialIoProtocol = NULL;
static BOOLEAN SerialInitialized = FALSE;
static BOOLEAN FirmwareSerialDisabled = FALSE;

/* Serial I/O Protocol GUID */
EFI_GUID gEfiSerialIoProtocolGuid = EFI_SERIAL_IO_PROTOCOL_GUID;

#if defined(_M_ARM64) || defined(__aarch64__)
/* PL011 UART registers and addresses for ARM64 platforms
 *
 * Platform-specific UART addresses:
 *   - QEMU virt:    0x09000000
 *   - Raspberry Pi 5 (BCM2712): 0x107d001000 (UART0 on debug header)
 *   - Raspberry Pi 4 (BCM2711): 0xFE201000
 */
#if defined(TARGET_QEMU_VIRT)
#define PL011_UART_BASE    0x09000000ULL
#elif defined(TARGET_RPI4)
#define PL011_UART_BASE    0xFE201000ULL
#else
/* Default to Raspberry Pi 5 (BCM2712) */
#define PL011_UART_BASE    0x107D001000ULL
#endif
#define PL011_DR           0x000       /* Data Register */
#define PL011_FR           0x018       /* Flag Register */
#define PL011_FR_TXFF      (1 << 5)    /* Transmit FIFO Full */
#define PL011_FR_RXFE      (1 << 4)    /* Receive FIFO Empty */

/* PL011 UART access macros */
#define PL011_READ(offset) \
    (*(volatile UINT32*)((UINTN)Pl011UartBase + (offset)))
#define PL011_WRITE(offset, value) \
    (*(volatile UINT32*)((UINTN)Pl011UartBase + (offset)) = (value))

static BOOLEAN UsePL011Fallback = FALSE;
static BOOLEAN Pl011Present = FALSE;
static UINT64 Pl011UartBase = PL011_UART_BASE;

#define SPCR_SIGNATURE 0x52435053 /* "SPCR" */
#define SPCR_INTERFACE_ARM_PL011 0x0E
#define ACPI_GAS_SPACE_SYSTEM_MEMORY 0

/*
 * On some firmwares the PL011 MMIO range is not mapped while Boot Services
 * are active. Blindly touching 0x09000000 can raise a synchronous exception
 * early during DebugInit. Guard the probe by checking the UEFI memory map
 * first, and only perform the MMIO read if the address resides in an
 * EfiMemoryMappedIO/EfiMemoryMappedIOPortSpace descriptor.
 */
static BOOLEAN UefiMmioRangePresent(UINT64 PhysAddr, UINT64 Length)
{
    if (!GlobalSystemTable || !GlobalSystemTable->BootServices)
        return FALSE;

    EFI_BOOT_SERVICES* Bs = GlobalSystemTable->BootServices;
    EFI_STATUS Status;
    UINTN MapSize = 0, MapKey = 0, DescSize = 0; UINT32 DescVer = 0;

    /* First probe to get required size */
    Status = Bs->GetMemoryMap(&MapSize, NULL, &MapKey, &DescSize, &DescVer);
    if ((Status != EFI_BUFFER_TOO_SMALL && Status != EFI_SUCCESS) || DescSize == 0)
    {
        /* Fallback to a small buffer if firmware doesn't support NULL probe */
        MapSize = 16 * 1024;
        DescSize = sizeof(EFI_MEMORY_DESCRIPTOR);
    }

    EFI_MEMORY_DESCRIPTOR* Map = NULL;
    Status = Bs->AllocatePool(EfiLoaderData, MapSize, (VOID**)&Map);
    if (EFI_ERROR(Status) || !Map)
        return FALSE;

    UINTN Tmp = MapSize;
    Status = Bs->GetMemoryMap(&Tmp, Map, &MapKey, &DescSize, &DescVer);
    if (EFI_ERROR(Status) || Tmp < DescSize)
    {
        Bs->FreePool(Map);
        return FALSE;
    }

    UINTN Count = Tmp / DescSize;
    BOOLEAN present = FALSE;
    UINT64 Start = PhysAddr;
    UINT64 End   = PhysAddr + Length;

    for (UINTN i = 0; i < Count; ++i)
    {
        EFI_MEMORY_DESCRIPTOR* D = (EFI_MEMORY_DESCRIPTOR*)((UINT8*)Map + i * DescSize);
        UINT64 dStart = (UINT64)D->PhysicalStart;
        UINT64 dEnd   = dStart + ((UINT64)D->NumberOfPages << 12);
        if (Start >= dStart && End <= dEnd)
        {
            if (D->Type == EfiMemoryMappedIO ||
                D->Type == EfiMemoryMappedIOPortSpace ||
                D->Type == EfiReservedMemoryType)
            {
                present = TRUE;
            }
            break;
        }
    }

    Bs->FreePool(Map);
    return present;
}

static PRSDP UefiLocateRsdp(VOID)
{
    if (!GlobalSystemTable)
        return NULL;

    EFI_GUID Acpi20 = EFI_ACPI_20_TABLE_GUID;
    EFI_GUID Acpi10 = ACPI_10_TABLE_GUID;

    for (UINTN Index = 0; Index < GlobalSystemTable->NumberOfTableEntries; ++Index)
    {
        EFI_CONFIGURATION_TABLE *Entry = &GlobalSystemTable->ConfigurationTable[Index];

        if (!memcmp(&Entry->VendorGuid, &Acpi20, sizeof(EFI_GUID)) ||
            !memcmp(&Entry->VendorGuid, &Acpi10, sizeof(EFI_GUID)))
        {
            return (PRSDP)Entry->VendorTable;
        }
    }

    return NULL;
}

#include <pshpack1.h>
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
#include <poppack.h>

static PSPCR_TABLE UefiLocateSpcr(VOID)
{
    PRSDP Rsdp = UefiLocateRsdp();
    if (!Rsdp)
        return NULL;

    if (Rsdp->Revision >= 2 && Rsdp->XsdtAddress.QuadPart != 0)
    {
        PXSDT Xsdt = (PXSDT)(ULONG_PTR)Rsdp->XsdtAddress.QuadPart;
        if (!Xsdt)
            return NULL;

        ULONG EntryCount = 0;
        if (Xsdt->Header.Length > sizeof(DESCRIPTION_HEADER))
            EntryCount = (Xsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) / sizeof(PHYSICAL_ADDRESS);

        for (ULONG i = 0; i < EntryCount; ++i)
        {
            ULONG_PTR TablePa = (ULONG_PTR)Xsdt->Tables[i].QuadPart;
            if (TablePa == 0)
                continue;

            PSPCR_TABLE Spcr = (PSPCR_TABLE)TablePa;
            if (Spcr->Header.Signature == SPCR_SIGNATURE)
                return Spcr;
        }
    }

    if (Rsdp->RsdtAddress != 0)
    {
        PRSDT Rsdt = (PRSDT)(ULONG_PTR)Rsdp->RsdtAddress;
        if (!Rsdt)
            return NULL;

        ULONG EntryCount = 0;
        if (Rsdt->Header.Length > sizeof(DESCRIPTION_HEADER))
            EntryCount = (Rsdt->Header.Length - sizeof(DESCRIPTION_HEADER)) / sizeof(ULONG);

        for (ULONG i = 0; i < EntryCount; ++i)
        {
            ULONG_PTR TablePa = (ULONG_PTR)Rsdt->Tables[i];
            if (TablePa == 0)
                continue;

            PSPCR_TABLE Spcr = (PSPCR_TABLE)TablePa;
            if (Spcr->Header.Signature == SPCR_SIGNATURE)
                return Spcr;
        }
    }

    return NULL;
}

static BOOLEAN UefiUpdatePl011FromSpcr(VOID)
{
    PSPCR_TABLE Spcr = UefiLocateSpcr();
    if (!Spcr)
        return FALSE;

    if (Spcr->InterfaceType != SPCR_INTERFACE_ARM_PL011)
        return FALSE;

    if (Spcr->SerialPort.AddressSpaceID != ACPI_GAS_SPACE_SYSTEM_MEMORY)
        return FALSE;

    if (Spcr->SerialPort.Address.QuadPart == 0)
        return FALSE;

    Pl011UartBase = Spcr->SerialPort.Address.QuadPart;
    Pl011Present = TRUE;
    return TRUE;
}

/* Check if PL011 UART is present and accessible */
static BOOLEAN PL011IsPresent(VOID)
{
    UINTN fr_addr = (UINTN)Pl011UartBase + PL011_FR;

    /* Only attempt the MMIO read if the range is described by firmware */
    if (!UefiMmioRangePresent((UINT64)fr_addr, sizeof(UINT32)))
        return FALSE;

    volatile UINT32 *uart_fr = (volatile UINT32*)fr_addr;
    UINT32 fr_value;

    /* Read and validate the flag register */
    fr_value = *uart_fr;

    /* Check if value looks like valid PL011 FR (bits 0-7 used, upper bits reserved) */
    if ((fr_value & 0xFFFFFF00) == 0)
    {
        return TRUE;
    }

    return FALSE;
}

/* Send a byte via PL011 UART */
static VOID PL011PutByte(UCHAR ByteToSend)
{
    volatile UINT32 *uart_dr = (volatile UINT32*)((UINTN)Pl011UartBase + PL011_DR);
    volatile UINT32 *uart_fr = (volatile UINT32*)((UINTN)Pl011UartBase + PL011_FR);

    /* Wait until transmit FIFO is not full */
    while ((*uart_fr & PL011_FR_TXFF) != 0)
    {
    }

    /* Write the byte to data register */
    *uart_dr = (UINT32)ByteToSend;
}
#endif /* ARM64 */


/* Initialize UEFI Serial I/O */
static BOOLEAN UefiSerialInitialize(ULONG ComPort, ULONG BaudRate)
{
    EFI_STATUS Status;
    EFI_HANDLE* HandleBuffer = NULL;
    UINTN HandleCount = 0;
    UINTN Index;
    BOOLEAN Found = FALSE;

    if (!GlobalSystemTable || !GlobalSystemTable->BootServices) {
        return FALSE;
    }

    /* Locate all Serial I/O Protocol instances */
    Status = GlobalSystemTable->BootServices->LocateHandleBuffer(
        ByProtocol,
        &gEfiSerialIoProtocolGuid,
        NULL,
        &HandleCount,
        &HandleBuffer);

    if (EFI_ERROR(Status) || HandleCount == 0) {
        return FALSE;
    }

    /* Select the appropriate serial port (0 means auto-detect) */
    if (ComPort == 0 || ComPort > HandleCount) {
        ComPort = 1; /* Use first available port */
    }

    Index = ComPort - 1; /* Convert to 0-based index */

    if (Index < HandleCount) {
        /* Get the Serial I/O Protocol for the selected port */
        Status = GlobalSystemTable->BootServices->HandleProtocol(
            HandleBuffer[Index],
            &gEfiSerialIoProtocolGuid,
            (VOID**)&SerialIoProtocol);

        if (!EFI_ERROR(Status) && SerialIoProtocol) {
            /* Configure serial port attributes */
            Status = SerialIoProtocol->SetAttributes(
                SerialIoProtocol,
                BaudRate,
                0,  /* Use default FIFO depth */
                0,  /* No timeout */
                NoParity,
                8,  /* 8 data bits */
                OneStopBit);

            if (!EFI_ERROR(Status)) {
                /* Reset the serial device */
                SerialIoProtocol->Reset(SerialIoProtocol);
                Found = TRUE;
            }
        }
    }

    /* Free the handle buffer */
    if (HandleBuffer) {
        GlobalSystemTable->BootServices->FreePool(HandleBuffer);
    }

    return Found;
}

/* Send byte via Serial I/O Protocol */
static VOID UefiSerialPutByte(UCHAR ByteToSend)
{
    UINTN BufferSize = 1;

    if (SerialIoProtocol) {
        SerialIoProtocol->Write(SerialIoProtocol, &BufferSize, &ByteToSend);
    }
}

/* Enhanced RS232 compatibility functions with Serial I/O support */

BOOLEAN Rs232PortInitialize(IN ULONG ComPort, IN ULONG BaudRate)
{
    /* Check if already initialized */
    if (SerialInitialized) {
        return TRUE;
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    if (!Pl011Present)
        UefiUpdatePl011FromSpcr();
    if (!Pl011Present)
        Pl011Present = PL011IsPresent();
#endif

    /* Set defaults if not specified */
    if (ComPort == 0) {
        ComPort = 1;  /* Default to first available serial port */
    }
    if (BaudRate == 0) {
        BaudRate = 115200;  /* Standard baud rate */
    }

    /* Try to initialize UEFI Serial I/O Protocol */
    if (UefiSerialInitialize(ComPort, BaudRate)) {
        SerialInitialized = TRUE;
        return TRUE;
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    /* Try PL011 UART as fallback on ARM64 */
    if (Pl011Present) {
        UsePL011Fallback = TRUE;
        SerialInitialized = TRUE;
        return TRUE;
    }
#endif

    /* Mark as initialized for graceful degradation */
    SerialInitialized = TRUE;
    return TRUE;
}

BOOLEAN Rs232PortGetByte(PUCHAR ByteReceived)
{
    UINTN BufferSize = 1;
    EFI_STATUS Status;

    if (FirmwareSerialDisabled || !SerialIoProtocol) {
        return FALSE;
    }

    Status = SerialIoProtocol->Read(SerialIoProtocol, &BufferSize, ByteReceived);
    return (!EFI_ERROR(Status) && BufferSize == 1);
}

BOOLEAN Rs232PortPollByte(PUCHAR ByteReceived)
{
    /* Same as Rs232PortGetByte for UEFI */
    return Rs232PortGetByte(ByteReceived);
}

VOID Rs232PortPutByte(UCHAR ByteToSend)
{
    /* If Serial I/O Protocol is available, use it */
    if (!FirmwareSerialDisabled && SerialIoProtocol) {
        UefiSerialPutByte(ByteToSend);
        return;
    }

#if defined(_M_ARM64) || defined(__aarch64__)
    /* Use PL011 UART fallback if available */
    if (UsePL011Fallback) {
        PL011PutByte(ByteToSend);
        return;
    }
#endif
}

BOOLEAN Rs232PortInUse(PUCHAR Base)
{
    /* Not applicable for UEFI Serial I/O */
    (void)Base;
#if defined(_M_ARM64) || defined(__aarch64__)
    if (FirmwareSerialDisabled)
        return UsePL011Fallback;
    return (SerialIoProtocol != NULL || UsePL011Fallback);
#else
    if (FirmwareSerialDisabled)
        return FALSE;
    return (SerialIoProtocol != NULL);
#endif
}

VOID UefiSerialDisableFirmware(VOID)
{
    FirmwareSerialDisabled = TRUE;
    SerialIoProtocol = NULL;
#if defined(_M_ARM64) || defined(__aarch64__)
    if (Pl011Present)
        UsePL011Fallback = TRUE;
#endif
}


//#endif /* ARM64 */
