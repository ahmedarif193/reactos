/*
 * PROJECT:     ReactOS Kernel  
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 User Mode Callbacks
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
KiCallUserMode(IN PVOID *OutputBuffer,
               IN PULONG OutputLength)
{
    PKTHREAD Thread;
    PKTRAP_FRAME TrapFrame;
    
    /* Get current thread */
    Thread = KeGetCurrentThread();
    
    /* Get trap frame */
    TrapFrame = Thread->TrapFrame;
    
    /* Make sure we have a trap frame */
    if (!TrapFrame)
    {
        return STATUS_INVALID_PARAMETER;
    }
    
    /* Save the current kernel state */
    /* This will be restored when returning from user mode */
    
    /* TODO: Implement actual user mode callback */
    /* For now, just return not implemented */
    
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KeUserModeCallback(IN ULONG RoutineIndex,
                   IN PVOID Argument,
                   IN ULONG ArgumentLength,
                   OUT PVOID *Result,
                   OUT PULONG ResultLength)
{
    /* ARM64 implementation of user mode callback */
    /* This is used for calling user32.dll functions from kernel */
    
    /* TODO: Implement when user32 support is added */
    
    return STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
KiInitializeUserApc(IN PKEXCEPTION_FRAME ExceptionFrame,
                    IN PKTRAP_FRAME TrapFrame,
                    IN PKNORMAL_ROUTINE NormalRoutine,
                    IN PVOID NormalContext,
                    IN PVOID SystemArgument1,
                    IN PVOID SystemArgument2)
{
    CONTEXT Context;
    ULONG_PTR Stack;
    PULONG_PTR UserStack;
    
    /* Get the current stack pointer from trap frame */
    Stack = TrapFrame->Sp;
    
    /* Align stack to 16 bytes (ARM64 requirement) */
    Stack &= ~0xF;
    
    /* Reserve space for context and APC parameters */
    Stack -= sizeof(CONTEXT) + 32;
    UserStack = (PULONG_PTR)Stack;
    
    /* Build a context record */
    Context.ContextFlags = CONTEXT_FULL;
    KeTrapFrameToContext(TrapFrame, ExceptionFrame, &Context);
    
    /* Copy context to user stack */
    RtlCopyMemory((PVOID)Stack, &Context, sizeof(CONTEXT));
    
    /* Setup APC parameters on stack */
    UserStack[0] = (ULONG_PTR)NormalRoutine;
    UserStack[1] = (ULONG_PTR)NormalContext;
    UserStack[2] = (ULONG_PTR)SystemArgument1;
    UserStack[3] = (ULONG_PTR)SystemArgument2;
    
    /* Update trap frame to execute APC dispatcher */
    TrapFrame->Elr = (ULONG_PTR)KeUserApcDispatcher;
    TrapFrame->Sp = Stack;
}

NTSTATUS
NTAPI
KeRaiseUserException(IN NTSTATUS ExceptionCode)
{
    /* ARM64 raise user exception stub */
    DPRINT1("ARM64: KeRaiseUserException not yet implemented\n");
    
    /* For now, return the exception code */
    return ExceptionCode;
}