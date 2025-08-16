/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 Exception Handling
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FORWARD DECLARATIONS ******************************************************/

KPROCESSOR_MODE
NTAPI
KiGetPreviousMode(IN PKTRAP_FRAME TrapFrame);

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
KiDispatchException(
    IN PEXCEPTION_RECORD ExceptionRecord,
    IN PKEXCEPTION_FRAME ExceptionFrame,
    IN PKTRAP_FRAME TrapFrame,
    IN KPROCESSOR_MODE PreviousMode,
    IN BOOLEAN FirstChance)
{
    CONTEXT Context;
    
    /* Build context from trap frame */
    RtlZeroMemory(&Context, sizeof(Context));
    Context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
    
    /* Copy registers from trap frame */
    Context.Sp = TrapFrame->Sp;
    Context.Pc = TrapFrame->Pc;
    Context.Cpsr = TrapFrame->Spsr;  /* ARM64 uses SPSR not CPSR in trap frames */
    
    /* Copy general purpose registers */
    RtlCopyMemory(&Context.X0, &TrapFrame->X0, sizeof(ULONG64) * 31);
    
    /* Handle the exception */
    if (FirstChance)
    {
        /* Try debugger first */
        if (KiDebugRoutine &&
            KiDebugRoutine(TrapFrame,
                          ExceptionFrame,
                          ExceptionRecord,
                          &Context,
                          PreviousMode,
                          FALSE))
        {
            /* Exception handled by debugger */
            goto Exit;
        }
        
        /* For user mode exceptions, try user mode handlers */
        if (PreviousMode == UserMode)
        {
            /* Dispatch to user mode */
            /* TODO: Implement user mode exception dispatch */
        }
    }
    
    /* Second chance - call debugger again */
    if (KiDebugRoutine &&
        KiDebugRoutine(TrapFrame,
                      ExceptionFrame,
                      ExceptionRecord,
                      &Context,
                      PreviousMode,
                      TRUE))
    {
        /* Exception handled by debugger */
        goto Exit;
    }
    
    /* Unhandled exception - bug check */
    KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED,
                 ExceptionRecord->ExceptionCode,
                 (ULONG_PTR)ExceptionRecord->ExceptionAddress,
                 ExceptionRecord->ExceptionInformation[0],
                 ExceptionRecord->ExceptionInformation[1]);
    
Exit:
    /* Restore context to trap frame */
    TrapFrame->Sp = Context.Sp;
    TrapFrame->Pc = Context.Pc;
    TrapFrame->Spsr = Context.Cpsr;  /* Copy back to SPSR */
    RtlCopyMemory(&TrapFrame->X0, &Context.X0, sizeof(ULONG64) * 31);
}

VOID
NTAPI
KiExceptionDispatch(
    IN PKTRAP_FRAME TrapFrame,
    IN ULONG ExceptionCode)
{
    EXCEPTION_RECORD ExceptionRecord;
    
    /* Build exception record */
    ExceptionRecord.ExceptionCode = ExceptionCode;
    ExceptionRecord.ExceptionFlags = 0;
    ExceptionRecord.ExceptionRecord = NULL;
    ExceptionRecord.ExceptionAddress = (PVOID)TrapFrame->Pc;
    ExceptionRecord.NumberParameters = 0;
    
    /* Dispatch the exception */
    KiDispatchException(&ExceptionRecord,
                        NULL,
                        TrapFrame,
                        KiGetPreviousMode(TrapFrame),
                        TRUE);
}

KPROCESSOR_MODE
NTAPI
KiGetPreviousMode(
    IN PKTRAP_FRAME TrapFrame)
{
    /* Check EL (Exception Level) from CPSR */
    if ((TrapFrame->Spsr & 0x0F) == 0)  /* Check SPSR for mode */
    {
        /* EL0 = User Mode */
        return UserMode;
    }
    else
    {
        /* EL1 or higher = Kernel Mode */
        return KernelMode;
    }
}

VOID
NTAPI
KiSynchronousException(
    IN PKTRAP_FRAME TrapFrame)
{
    ULONG Esr;
    ULONG ExceptionCode;
    
    /* Read ESR (Exception Syndrome Register) */
    __asm__ __volatile__("mrs %0, esr_el1" : "=r" (Esr));
    
    /* Decode exception class from ESR */
    ULONG ExceptionClass = (Esr >> 26) & 0x3F;
    
    switch (ExceptionClass)
    {
        case 0x15: /* SVC instruction execution */
            /* System call */
            KiSystemService(TrapFrame);
            break;
            
        case 0x24: /* Data abort from lower EL */
        case 0x25: /* Data abort from current EL */
            /* Page fault */
            ExceptionCode = STATUS_ACCESS_VIOLATION;
            KiExceptionDispatch(TrapFrame, ExceptionCode);
            break;
            
        case 0x20: /* Instruction abort from lower EL */
        case 0x21: /* Instruction abort from current EL */
            /* Instruction fetch fault */
            ExceptionCode = STATUS_ACCESS_VIOLATION;
            KiExceptionDispatch(TrapFrame, ExceptionCode);
            break;
            
        case 0x00: /* Unknown reason */
        case 0x07: /* FP/SIMD access */
        case 0x0C: /* HVC instruction */
        case 0x3C: /* BRK instruction */
            /* Breakpoint */
            ExceptionCode = STATUS_BREAKPOINT;
            KiExceptionDispatch(TrapFrame, ExceptionCode);
            break;
            
        default:
            /* Unknown exception */
            ExceptionCode = STATUS_ILLEGAL_INSTRUCTION;
            KiExceptionDispatch(TrapFrame, ExceptionCode);
            break;
    }
}

BOOLEAN
NTAPI
RtlDispatchException(IN PEXCEPTION_RECORD ExceptionRecord,
                     IN PCONTEXT Context)
{
    /* ARM64 exception dispatch stub */
    DPRINT1("ARM64: RtlDispatchException not yet implemented\n");
    
    /* For now, indicate that the exception was not handled */
    return FALSE;
}

/* EOF */