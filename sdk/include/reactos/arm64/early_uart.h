/*
 * PROJECT:     ReactOS ARM64 Kernel/Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        sdk/include/reactos/arm64/early_uart.h
 * PURPOSE:     ARM64 early UART with runtime ACPI-based detection
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 *
 * DESCRIPTION:
 *   Early UART for FreeLoader and ntoskrnl. The base address and register
 *   layout are determined at runtime from ACPI SPCR or DBG2 (the only two
 *   firmware-blessed mechanisms for serial console discovery on ARM64).
 *
 *   If neither table is present, or the reported port subtype has no driver, 
 *   the UART stays disabled and all output becomes a no-op.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Whether early UART discovery has run, and whether we have a usable port.
 * Anything more granular (which RPi model, etc.) belongs in higher layers.
 */
typedef enum _ARM64_PLATFORM_ID
{
    Arm64PlatformUnknown = 0,
    Arm64PlatformGenericAcpi,   /* SPCR/DBG2 gave us a usable serial port */
    Arm64PlatformMax
} ARM64_PLATFORM_ID;

/*
 * UART register interface type. Set from the SPCR InterfaceType / DBG2
 * PortSubtype. Arm64UartUnknown means we have no driver for this port and
 * must not perform I/O on it - even if a base address was reported.
 */
typedef enum _ARM64_UART_INTERFACE
{
    Arm64UartUnknown = 0,
    Arm64UartPl011,
    Arm64UartNs16550,
    Arm64UartMax
} ARM64_UART_INTERFACE;

/*
 * PL011 UART register offsets
 */
#define ARM64_PL011_DR              0x000   /* Data Register */
#define ARM64_PL011_FR              0x018   /* Flag Register */
#define ARM64_PL011_IBRD            0x024   /* Integer Baud Rate Divisor */
#define ARM64_PL011_FBRD            0x028   /* Fractional Baud Rate Divisor */
#define ARM64_PL011_LCR_H           0x02C   /* Line Control Register */
#define ARM64_PL011_CR              0x030   /* Control Register */
#define ARM64_PL011_IMSC            0x038   /* Interrupt Mask Set/Clear */

/* Flag Register bits */
#define ARM64_PL011_FR_TXFF         (1U << 5)   /* Transmit FIFO Full */
#define ARM64_PL011_FR_RXFE         (1U << 4)   /* Receive FIFO Empty */
#define ARM64_PL011_FR_BUSY         (1U << 3)   /* UART Busy */

/*
 * NS16550-compatible UART register offsets and bits.
 */
#define ARM64_NS16550_RBR           0x000   /* Receive Buffer Register */
#define ARM64_NS16550_THR           0x000   /* Transmit Holding Register */
#define ARM64_NS16550_LSR           0x005   /* Line Status Register */
#define ARM64_NS16550_LSR_DR        0x01    /* Data Ready */
#define ARM64_NS16550_LSR_THRE      0x20    /* Transmit Holding Register Empty */

/*
 * Global runtime-detected UART state.
 * These are set during early boot by the detection code.
 *
 * Declaration: Always declare these as extern here.
 * The actual definitions are in:
 *   - Bootloader: boot/freeldr/freeldr/arch/uefi/arm64/early_uart.c
 *   - Kernel: ntoskrnl/arch/arm64/ke/early_uart.c
 */
extern volatile UINT64 EarlyUartBaseAddress;
extern volatile ARM64_PLATFORM_ID EarlyUartPlatformId;
extern volatile ARM64_UART_INTERFACE EarlyUartInterface;
extern volatile BOOLEAN EarlyUartInitialized;

/*
 * Kernel-side physical-map base used by EarlyUartPhysToVa below.
 * Must match ARM64_PHYS_MAP_BASE in ntoskrnl/arch/arm64/ke/boot.c, where the
 * early identity map is established.
 */
#define ARM64_PHYS_MAP_BASE_VA      0xFFFFFC0000000000ULL

/*
 * EarlyUartPhysToVa - Convert physical UART address to kernel virtual address.
 * In the kernel context, UART is accessed via the private physical map.
 * In the bootloader context (pre-MMU or identity mapped), VA == PA.
 */
#if defined(_NTOSKRNL_) || defined(_NTOS_)
#define EarlyUartPhysToVa(PhysAddr) (ARM64_PHYS_MAP_BASE_VA + (PhysAddr))
#else
/* Bootloader uses identity mapping or pre-MMU access */
#define EarlyUartPhysToVa(PhysAddr) (PhysAddr)
#endif

/*
 * Inline UART access macros.
 * These read/write to the runtime-detected UART address.
 *
 * Note: Use ULONG_PTR for portability between bootloader (UEFI) and kernel contexts.
 * UINTN is a UEFI-only type; ULONG_PTR is defined in both environments.
 */
#define EARLY_UART_READ(offset) \
    (*(volatile UINT32*)(ULONG_PTR)(EarlyUartPhysToVa(EarlyUartBaseAddress) + (offset)))

#define EARLY_UART_WRITE(offset, value) \
    (*(volatile UINT32*)(ULONG_PTR)(EarlyUartPhysToVa(EarlyUartBaseAddress) + (offset)) = (value))

#define EARLY_UART_READ8(offset) \
    (*(volatile UCHAR*)(ULONG_PTR)(EarlyUartPhysToVa(EarlyUartBaseAddress) + (offset)))

#define EARLY_UART_WRITE8(offset, value) \
    (*(volatile UCHAR*)(ULONG_PTR)(EarlyUartPhysToVa(EarlyUartBaseAddress) + (offset)) = (UCHAR)(value))

static __inline BOOLEAN
EarlyUartReady(VOID)
{
    return EarlyUartInitialized &&
           EarlyUartBaseAddress != 0 &&
           EarlyUartInterface != Arm64UartUnknown;
}

/*
 * EarlyUartPutc - Output a single character to the early UART.
 * Waits for transmit FIFO to have space.
 */
static __inline VOID
EarlyUartPutc(CHAR Ch)
{
    if (!EarlyUartReady())
        return;

    if (EarlyUartInterface == Arm64UartNs16550)
    {
        while (!(EARLY_UART_READ8(ARM64_NS16550_LSR) & ARM64_NS16550_LSR_THRE))
        {
            __asm__ __volatile__("yield");
        }

        EARLY_UART_WRITE8(ARM64_NS16550_THR, (UCHAR)Ch);
    }
    else
    {
        while (EARLY_UART_READ(ARM64_PL011_FR) & ARM64_PL011_FR_TXFF)
        {
            __asm__ __volatile__("yield");
        }

        EARLY_UART_WRITE(ARM64_PL011_DR, (UINT32)(UCHAR)Ch);
    }
}

/*
 * EarlyUartGetc - Poll one character from the early UART.
 * Returns TRUE when a byte was read, FALSE if no byte is available.
 */
static __inline BOOLEAN
EarlyUartGetc(_Out_ UCHAR *Byte)
{
    if (!EarlyUartReady() || Byte == NULL)
        return FALSE;

    if (EarlyUartInterface == Arm64UartNs16550)
    {
        if (!(EARLY_UART_READ8(ARM64_NS16550_LSR) & ARM64_NS16550_LSR_DR))
            return FALSE;

        *Byte = EARLY_UART_READ8(ARM64_NS16550_RBR);
    }
    else
    {
        if (EARLY_UART_READ(ARM64_PL011_FR) & ARM64_PL011_FR_RXFE)
            return FALSE;

        *Byte = (UCHAR)(EARLY_UART_READ(ARM64_PL011_DR) & 0xFF);
    }

    return TRUE;
}

/*
 * EarlyUartDrainReceiveFifo - Drop stale input before a protocol takes over.
 */
static __inline VOID
EarlyUartDrainReceiveFifo(VOID)
{
    UCHAR Byte;
    ULONG Guard;

    for (Guard = 2048; Guard > 0; Guard--)
    {
        if (!EarlyUartGetc(&Byte))
            break;
    }
}

/*
 * EarlyUartPuts - Output a null-terminated string to the early UART.
 * Handles CR/LF conversion (adds CR before LF).
 */
static __inline VOID
EarlyUartPuts(const CHAR *String)
{
    if (!EarlyUartReady() || !String)
        return;

    while (*String)
    {
        if (*String == '\n')
            EarlyUartPutc('\r');
        EarlyUartPutc(*String++);
    }
}

/*
 * EarlyUartPutHex - Output a hexadecimal value.
 * Nibbles parameter specifies how many hex digits to output (1-16).
 */
static __inline VOID
EarlyUartPutHex(UINT64 Value, UINT32 Nibbles)
{
    static const CHAR HexDigits[] = "0123456789ABCDEF";
    INT32 Index;

    if (!EarlyUartReady())
        return;

    if (Nibbles > 16)
        Nibbles = 16;

    for (Index = (INT32)Nibbles - 1; Index >= 0; --Index)
    {
        UINT32 Shift = (UINT32)Index * 4;
        EarlyUartPutc(HexDigits[(Value >> Shift) & 0xFULL]);
    }
}

/*
 * EarlyUartPutDec - Output a decimal value (unsigned 32-bit).
 */
static __inline VOID
EarlyUartPutDec(UINT32 Value)
{
    CHAR Buffer[12];  /* Max 10 digits for 32-bit + sign + null */
    UINT32 Pos = 0;

    if (!EarlyUartReady())
        return;

    if (Value == 0)
    {
        EarlyUartPutc('0');
        return;
    }

    /* Build string in reverse */
    while (Value && Pos < sizeof(Buffer) - 1)
    {
        Buffer[Pos++] = (CHAR)('0' + (Value % 10));
        Value /= 10;
    }

    /* Output in correct order */
    while (Pos > 0)
    {
        EarlyUartPutc(Buffer[--Pos]);
    }
}

/*
 * EarlyUartDetectPlatform - Walk ACPI SPCR then DBG2 (in that order). Sets
 * the globals on success. Must be called while UEFI tables are still mapped
 * (i.e. before ExitBootServices). Kernel doesn't re-detect; FreeLDR passes
 * the result through the loader block.
 *
 * Returns Arm64PlatformGenericAcpi on success, Arm64PlatformUnknown if no
 * serial console was discovered.
 */
ARM64_PLATFORM_ID
EarlyUartDetectPlatform(VOID);

/*
 * EarlyUartInitialize / EarlyUartInitializeWithInterface - main entry points.
 *
 *   UartBase == 0:               run EarlyUartDetectPlatform()
 *   UartBase != 0, iface == Unk: caller doesn't know the register layout;
 *                                we record the base but leave the UART off
 *                                (EarlyUartReady() returns FALSE)
 *   UartBase != 0, iface valid:  use as-is (loader-block path)
 *
 * Always returns TRUE; the "initialised" bit just means detection has run.
 * Use EarlyUartReady() to check whether I/O is actually safe.
 */
BOOLEAN
EarlyUartInitialize(UINT64 UartBaseOverride);

BOOLEAN
EarlyUartInitializeWithInterface(
    UINT64 UartBaseOverride,
    ARM64_UART_INTERFACE UartInterfaceOverride);

/*
 * EarlyUartGetBaseAddress - Get the detected UART base address.
 * Returns 0 if not yet detected or detection failed.
 */
static __inline UINT64
EarlyUartGetBaseAddress(VOID)
{
    return EarlyUartBaseAddress;
}

/*
 * EarlyUartIsInitialized - Has detection run (regardless of outcome)?
 * Use EarlyUartReady() above when you actually want to know if I/O is safe.
 */
static __inline BOOLEAN
EarlyUartIsInitialized(VOID)
{
    return EarlyUartInitialized;
}

#ifdef __cplusplus
}
#endif
