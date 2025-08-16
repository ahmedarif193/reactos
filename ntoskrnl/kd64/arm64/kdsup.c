/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 Kernel Debugger Support
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
KdpAllowDisable(VOID)
{
    /* ARM64: Debugger can be disabled */
    return STATUS_SUCCESS;
}

KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(
    _In_ ULONG ProcessorIndex)
{
    /* Switch to specified processor for debugging */
    /* On ARM64, we need to send an IPI to the target processor */
    
    /* For now, just return continue status */
    return ContinueProcessorReselected;
}