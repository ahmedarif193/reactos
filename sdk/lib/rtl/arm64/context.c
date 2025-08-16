/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Context Initialization
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <rtl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
RtlInitializeContext(IN HANDLE ProcessHandle,
                     OUT PCONTEXT Context,
                     IN PVOID ThreadStartParam OPTIONAL,
                     IN PTHREAD_START_ROUTINE ThreadStartAddress,
                     IN PINITIAL_TEB InitialTeb)
{
    DPRINT("RtlInitializeContext for ARM64\n");
    
    /* Clear the context */
    RtlZeroMemory(Context, sizeof(CONTEXT));
    
    /* Set the context flags */
    Context->ContextFlags = CONTEXT_FULL;
    
    /* Set the stack pointer (SP) to the top of the stack minus space for parameters */
    Context->Sp = (ULONG_PTR)InitialTeb->StackBase - 16;  /* ARM64 requires 16-byte alignment */
    
    /* Set the program counter (PC) to the start address */
    Context->Pc = (ULONG_PTR)ThreadStartAddress;
    
    /* Set the first parameter (X0) if provided */
    if (ThreadStartParam)
    {
        Context->X0 = (ULONG_PTR)ThreadStartParam;
    }
    
    /* Set the frame pointer (X29/FP) to the stack pointer */
    Context->Fp = Context->Sp;
    
    /* Clear link register (X30/LR) - will be set by thread startup */
    Context->Lr = 0;
    
    /* Set SPSR to user mode with interrupts enabled */
    Context->Cpsr = 0x00000000;  /* EL0 (user mode) */
}

/* EOF */