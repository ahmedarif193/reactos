/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/include/arm64pl011.h
 * PURPOSE:         ARM64 PL011 UART definitions with runtime platform detection
 * COPYRIGHT:       Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 *
 * DESCRIPTION:
 *   This header provides early UART support for the ARM64 kernel.
 *   The UART base address is detected at runtime by the bootloader and
 *   passed to the kernel via the loader block extension.
 *
 *   In the kernel, the UART is accessed via the private physical map.
 */

#pragma once

/*
 * Include the shared early_uart header which provides:
 * - Runtime-detected UART base address (EarlyUartBaseAddress)
 * - Platform identification (EarlyUartPlatformId)
 * - Inline functions for UART I/O (EarlyUartPutc, EarlyUartPuts, etc.)
 */
#include <reactos/arm64/early_uart.h>

/*
 * Legacy constants for backward compatibility.
 * New code should use the runtime-detected EarlyUartBaseAddress instead.
 *
 * IMPORTANT: These are FALLBACK values only. The actual UART address is
 * detected at boot time and stored in EarlyUartBaseAddress.
 */
#define KI_ARM64_PL011_BASE_DEFAULT     0x09000000UL  /* QEMU virt default */
#define KI_ARM64_PL011_FR_TXFF          (1U << 5)

/*
 * KI_ARM64_PL011_BASE - Get the UART base physical address.
 * Returns the runtime-detected address if initialized, otherwise the default.
 */
#define KI_ARM64_PL011_BASE \
    (EarlyUartInitialized ? EarlyUartBaseAddress : KI_ARM64_PL011_BASE_DEFAULT)

/*
 * KI_ARM64_PL011_VA - Get the UART virtual address via the private
 * physical map.
 */
#define KI_ARM64_PL011_VA \
    (ARM64_PHYS_MAP_BASE_VA + KI_ARM64_PL011_BASE)

/*
 * Kernel UART output macros.
 * These use the runtime-detected UART address via the early_uart functions.
 */
#define KiArm64UartPutc(Ch)         EarlyUartPutc(Ch)
#define KiArm64UartPuts(Str)        EarlyUartPuts(Str)
#define KiArm64UartPutHex(Val, N)   EarlyUartPutHex((Val), (N))

/*
 * KiArm64InitializeUart - Initialize kernel UART with address from loader.
 *
 * This should be called early in kernel initialization with the UART
 * base address that was detected by the bootloader. The address is
 * typically stored in the loader block extension.
 *
 * UartBase: Physical address of the PL011 UART (from loader block)
 *           Pass 0 to use the default/detected address.
 */
static __inline VOID
KiArm64InitializeUart(UINT64 UartBase)
{
    if (!EarlyUartInitialized)
    {
        EarlyUartInitialize(UartBase);
    }
}
