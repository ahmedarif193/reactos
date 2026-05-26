/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/early_uart.c
 * PURPOSE:         ARM64 early UART runtime state for kernel
 * COPYRIGHT:       Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#include <ntoskrnl.h>

#include <reactos/arm64/early_uart.h>

/* Global UART state - set from loader block during kernel init */
volatile UINT64 EarlyUartBaseAddress = 0;
volatile ARM64_PLATFORM_ID EarlyUartPlatformId = Arm64PlatformUnknown;
volatile ARM64_UART_INTERFACE EarlyUartInterface = Arm64UartUnknown;
volatile BOOLEAN EarlyUartInitialized = FALSE;

/*
 * Kernel never re-detects at runtime. FreeLDR did the SPCR/DBG2 walk and
 * passed the result through LoaderBlock; KiSystemStartup populates the
 * globals from it. These functions exist only to satisfy callers that
 * happen to use the shared early_uart.h API (e.g. KdPortInitializeEx
 * calling EarlyUartInitialize(0) defensively when KD comes up before
 * KiSystemStartup has run).
 */
ARM64_PLATFORM_ID
EarlyUartDetectPlatform(VOID)
{
    return EarlyUartInitialized ? EarlyUartPlatformId : Arm64PlatformUnknown;
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
        EarlyUartInterface = (UartInterfaceOverride < Arm64UartMax) ?
                             UartInterfaceOverride :
                             Arm64UartUnknown;
        EarlyUartPlatformId = Arm64PlatformGenericAcpi;
    }
    else
    {
        EarlyUartBaseAddress = 0;
        EarlyUartInterface = Arm64UartUnknown;
        EarlyUartPlatformId = Arm64PlatformUnknown;
    }

    EarlyUartInitialized = TRUE;
    return TRUE;
}
