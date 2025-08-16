/*
 * PROJECT:     ReactOS ARM64 HAL
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Processor Support for ARM64
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <hal.h>

/* Undefine the HAL macros that conflict with our function definitions */
#undef HalAllProcessorsStarted
#undef HalStartNextProcessor
#undef HalRequestIpi

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
HalProcessorIdle(VOID)
{
    /* Enter idle state - Wait For Interrupt */
    __halt();
}

BOOLEAN
NTAPI
HalAllProcessorsStarted(VOID)
{
    /* Return TRUE when all processors have started */
    return TRUE;
}

VOID
NTAPI
HalRequestIpi(
    IN KAFFINITY TargetProcessors)
{
    /* Send Inter-Processor Interrupt */
    /* TODO: Implement IPI via GIC */
    
    /* For now, use SGI (Software Generated Interrupt) */
    if (HalpGicDistributor)
    {
        /* Send SGI to target processors */
        ULONG SgiValue = 0x02000000 | (TargetProcessors << 16) | 0;
        HalpGicDistributor->SoftwareInt = SgiValue;
    }
}

VOID
NTAPI
HalStartNextProcessor(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN PKPROCESSOR_STATE ProcessorState)
{
    /* Start next processor */
    /* TODO: Implement MP startup via PSCI */
}

ULONG
NTAPI
HalStartProfileInterrupt(
    IN KPROFILE_SOURCE ProfileSource)
{
    /* Start profiling interrupt */
    /* TODO: Implement profiling */
    return 0;
}

VOID
NTAPI
HalStopProfileInterrupt(
    IN KPROFILE_SOURCE ProfileSource)
{
    /* Stop profiling interrupt */
    /* TODO: Implement profiling */
}

ULONG_PTR
NTAPI
HalSetProfileInterval(
    IN ULONG_PTR Interval)
{
    /* Set profiling interval */
    /* TODO: Implement profiling */
    return Interval;
}

/* EOF */