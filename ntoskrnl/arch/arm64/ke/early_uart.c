/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/early_uart.c
 * PURPOSE:         ARM64 early UART runtime state for kernel
 * COPYRIGHT:       Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#include <ntoskrnl.h>

#define _EARLY_UART_IMPL
#include <reactos/arm64/early_uart.h>

/* Global UART state - set from loader block during kernel init */
volatile UINT64 EarlyUartBaseAddress = 0;
volatile ARM64_PLATFORM_ID EarlyUartPlatformId = Arm64PlatformUnknown;
volatile ARM64_UART_INTERFACE EarlyUartInterface = Arm64UartUnknown;
volatile BOOLEAN EarlyUartInitialized = FALSE;

ARM64_PLATFORM_ID
EarlyUartDetectPlatform(VOID)
{
    if (EarlyUartInitialized)
        return EarlyUartPlatformId;
    return Arm64PlatformQemuVirt;
}

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
        EarlyUartBaseAddress = UartBaseOverride;

        if (UartBaseOverride == ARM64_UART_QEMU_VIRT)
            EarlyUartPlatformId = Arm64PlatformQemuVirt;
        else if (UartBaseOverride == ARM64_UART_RPI3_BCM2837)
            EarlyUartPlatformId = Arm64PlatformRpi3;
        else if (UartBaseOverride == ARM64_UART_RPI4_BCM2711)
            EarlyUartPlatformId = Arm64PlatformRpi4;
        else if (UartBaseOverride == ARM64_UART_RPI5_BCM2712)
            EarlyUartPlatformId = Arm64PlatformRpi5;
        else
            EarlyUartPlatformId = Arm64PlatformGenericAcpi;
    }
    else
    {
        EarlyUartBaseAddress = ARM64_UART_DEFAULT;
        EarlyUartPlatformId = Arm64PlatformQemuVirt;
    }

    EarlyUartInterface = (UartInterfaceOverride != Arm64UartUnknown) ?
                         UartInterfaceOverride :
                         EarlyUartInferInterfaceFromAddress(EarlyUartBaseAddress);
    if (EarlyUartInterface == Arm64UartUnknown)
        EarlyUartInterface = Arm64UartPl011;

    EarlyUartInitialized = TRUE;
    return TRUE;
}
