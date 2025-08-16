/*
 * PROJECT:     ReactOS ARM64 HAL
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     HAL Initialization for ARM64
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <hal.h>

/* Undefine the HAL macros that conflict with our function definitions */
#undef HalInitSystem
#undef HalInitializeProcessor
#undef HalReturnToFirmware
#undef HalReportResourceUsage

/* GLOBALS *******************************************************************/

BOOLEAN HalpPciLockSettings = FALSE;
ULONG HalpBusType = 0;

/* FUNCTIONS *****************************************************************/

BOOLEAN
NTAPI
HalInitSystem(
    IN ULONG BootPhase,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    if (BootPhase == 0)
    {
        /* Phase 0 initialization */
        
        /* Initialize the GIC (Generic Interrupt Controller) */
        HalpInitializeGIC();
        
        /* Initialize timer */
        HalpInitializeTimer();
        
        /* Initialize interrupts */
        HalpInitializeInterrupts();
    }
    else if (BootPhase == 1)
    {
        /* Phase 1 initialization */
        
        /* Map physical memory regions */
        HalpMapPhysicalMemory();
    }
    
    return TRUE;
}

BOOLEAN
NTAPI
HalInitializeProcessor(
    IN ULONG ProcessorNumber,
    IN PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    /* Initialize per-processor state */
    
    /* Enable local interrupts */
    _enable();
    
    return TRUE;
}

VOID
NTAPI
HalReturnToFirmware(
    IN FIRMWARE_REENTRY Action)
{
    /* Handle return to firmware */
    switch (Action)
    {
        case HalPowerDownRoutine:
        case HalRestartRoutine:
        case HalRebootRoutine:
            /* Reset the system via PSCI */
            __asm__ __volatile__(
                "movz x0, #0x0009, lsl #0\n"
                "movk x0, #0x8400, lsl #16\n"
                "smc #0"
            );
            break;
            
        case HalHaltRoutine:
        default:
            /* Halt the processor */
            _disable();
            for (;;)
            {
                __halt();
            }
            break;
    }
}

VOID
NTAPI
HalReportResourceUsage(VOID)
{
    /* Report HAL resource usage */
    /* TODO: Implement resource reporting */
}

/* EOF */