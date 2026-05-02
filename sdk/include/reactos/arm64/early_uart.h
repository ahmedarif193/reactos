/*
 * PROJECT:     ReactOS ARM64 Kernel/Bootloader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        sdk/include/reactos/arm64/early_uart.h
 * PURPOSE:     ARM64 early UART with runtime platform detection
 * COPYRIGHT:   Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 *
 * DESCRIPTION:
 *   This header provides early UART support for both FreeLoader and ntoskrnl
 *   with RUNTIME detection of the UART base address. The platform is detected
 *   at boot time via ACPI SPCR table, SMBIOS, or other firmware mechanisms.
 *
 *   NO compile-time #ifdef TARGET_xxx switches - detection is pure runtime.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Platform identification enum.
 * Detected at runtime, NOT at compile time.
 */
typedef enum _ARM64_PLATFORM_ID
{
    Arm64PlatformUnknown = 0,
    Arm64PlatformQemuVirt,      /* QEMU virt machine */
    Arm64PlatformRpi3,          /* Raspberry Pi 3B (BCM2837) */
    Arm64PlatformRpi4,          /* Raspberry Pi 4 (BCM2711) */
    Arm64PlatformRpi5,          /* Raspberry Pi 5 (BCM2712) */
    Arm64PlatformGenericAcpi,   /* Generic ACPI-based platform with SPCR */
    Arm64PlatformMax
} ARM64_PLATFORM_ID;

/*
 * Known UART base addresses for various platforms.
 * These are physical addresses.
 */
#define ARM64_UART_QEMU_VIRT        0x09000000ULL
#define ARM64_UART_RPI3_BCM2837     0x3F201000ULL
#define ARM64_UART_RPI4_BCM2711     0xFE201000ULL
#define ARM64_UART_RPI5_BCM2712     0x107D001000ULL

/* Default fallback (QEMU virt for development) */
#define ARM64_UART_DEFAULT          ARM64_UART_QEMU_VIRT

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
extern volatile BOOLEAN EarlyUartInitialized;

/*
 * KSEG0 base address for kernel virtual address calculation.
 * The kernel maps physical memory starting at this VA.
 */
#define ARM64_KSEG0_BASE_VA         0xFFFF800000000000ULL

/*
 * EarlyUartPhysToVa - Convert physical UART address to kernel virtual address.
 * In the kernel context, UART is accessed via KSEG0 mapping.
 * In the bootloader context (pre-MMU or identity mapped), VA == PA.
 */
#if defined(_NTOSKRNL_) || defined(_NTOS_)
#define EarlyUartPhysToVa(PhysAddr) (ARM64_KSEG0_BASE_VA + (PhysAddr))
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

/*
 * EarlyUartPutc - Output a single character to the early UART.
 * Waits for transmit FIFO to have space.
 */
static __inline VOID
EarlyUartPutc(CHAR Ch)
{
    if (!EarlyUartInitialized || EarlyUartBaseAddress == 0)
        return;

    /* Wait until transmit FIFO is not full */
    while (EARLY_UART_READ(ARM64_PL011_FR) & ARM64_PL011_FR_TXFF)
    {
        /* Yield to reduce power consumption while waiting */
        __asm__ __volatile__("yield");
    }

    /* Write the character */
    EARLY_UART_WRITE(ARM64_PL011_DR, (UINT32)(UCHAR)Ch);
}

/*
 * EarlyUartPuts - Output a null-terminated string to the early UART.
 * Handles CR/LF conversion (adds CR before LF).
 */
static __inline VOID
EarlyUartPuts(const CHAR *String)
{
    if (!EarlyUartInitialized || EarlyUartBaseAddress == 0 || !String)
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

    if (!EarlyUartInitialized || EarlyUartBaseAddress == 0)
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

    if (!EarlyUartInitialized || EarlyUartBaseAddress == 0)
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
 * Function declarations for runtime detection.
 * These are implemented in early_uart.c for the bootloader.
 */

/*
 * EarlyUartDetectPlatform - Detect the ARM64 platform at runtime.
 *
 * This function queries ACPI SPCR table to get the UART address,
 * and/or SMBIOS to identify the board (e.g., Raspberry Pi).
 *
 * Must be called BEFORE ExitBootServices when UEFI tables are available.
 * For kernel, the detected values are passed via loader block.
 *
 * Returns: The detected platform ID, or Arm64PlatformUnknown if detection fails.
 */
ARM64_PLATFORM_ID
EarlyUartDetectPlatform(VOID);

/*
 * EarlyUartInitialize - Initialize early UART with runtime detection.
 *
 * This is the main entry point for UART initialization.
 * It calls EarlyUartDetectPlatform and sets up the global state.
 *
 * For bootloader: Call this early in boot, before ExitBootServices.
 * For kernel: Call with the address passed from the loader.
 *
 * UartBaseOverride: If non-zero, use this address instead of detecting.
 *                   This allows kernel to use address from loader block.
 *
 * Returns: TRUE if UART was initialized successfully.
 */
BOOLEAN
EarlyUartInitialize(UINT64 UartBaseOverride);

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
 * EarlyUartGetPlatformId - Get the detected platform ID.
 */
static __inline ARM64_PLATFORM_ID
EarlyUartGetPlatformId(VOID)
{
    return EarlyUartPlatformId;
}

/*
 * EarlyUartIsInitialized - Check if early UART is ready for use.
 */
static __inline BOOLEAN
EarlyUartIsInitialized(VOID)
{
    return EarlyUartInitialized;
}

/*
 * Platform name strings for debug output.
 */
static __inline const CHAR*
EarlyUartGetPlatformName(ARM64_PLATFORM_ID PlatformId)
{
    switch (PlatformId)
    {
        case Arm64PlatformQemuVirt:     return "QEMU virt";
        case Arm64PlatformRpi3:         return "Raspberry Pi 3";
        case Arm64PlatformRpi4:         return "Raspberry Pi 4";
        case Arm64PlatformRpi5:         return "Raspberry Pi 5";
        case Arm64PlatformGenericAcpi:  return "Generic ACPI";
        default:                        return "Unknown";
    }
}

#ifdef __cplusplus
}
#endif
