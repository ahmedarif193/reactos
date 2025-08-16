/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Process/Thread Context Management
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
PspGetContext(IN PKTRAP_FRAME TrapFrame,
              IN PVOID NonVolatileContext,
              IN OUT PCONTEXT Context)
{
    /* Check what to get */
    if (Context->ContextFlags & CONTEXT_CONTROL)
    {
        /* Get control registers */
        Context->Sp = TrapFrame->Sp;
        Context->Pc = TrapFrame->Pc;
        Context->Cpsr = TrapFrame->Spsr;
    }
    
    if (Context->ContextFlags & CONTEXT_INTEGER)
    {
        /* Get integer registers */
        Context->X[0] = TrapFrame->X0;
        Context->X[1] = TrapFrame->X1;
        Context->X[2] = TrapFrame->X2;
        Context->X[3] = TrapFrame->X3;
        Context->X[4] = TrapFrame->X4;
        Context->X[5] = TrapFrame->X5;
        Context->X[6] = TrapFrame->X6;
        Context->X[7] = TrapFrame->X7;
        Context->X[8] = TrapFrame->X8;
        Context->X[9] = TrapFrame->X9;
        Context->X[10] = TrapFrame->X10;
        Context->X[11] = TrapFrame->X11;
        Context->X[12] = TrapFrame->X12;
        Context->X[13] = TrapFrame->X13;
        Context->X[14] = TrapFrame->X14;
        Context->X[15] = TrapFrame->X15;
        Context->X[16] = TrapFrame->X16;
        Context->X[17] = TrapFrame->X17;
        Context->X[18] = TrapFrame->X18;
        /* FIXME TODO: X19-X28 are callee-saved and should be in KEXCEPTION_FRAME */
        /* For now, zero them out */
        Context->X[19] = 0;
        Context->X[20] = 0;
        Context->X[21] = 0;
        Context->X[22] = 0;
        Context->X[23] = 0;
        Context->X[24] = 0;
        Context->X[25] = 0;
        Context->X[26] = 0;
        Context->X[27] = 0;
        Context->X[28] = 0;
        Context->Fp = TrapFrame->Fp;
        Context->Lr = TrapFrame->Lr;
    }
    
    if (Context->ContextFlags & CONTEXT_FLOATING_POINT)
    {
        /* Get floating point registers */
        /* TODO: Copy FP/NEON registers */
    }
    
    if (Context->ContextFlags & CONTEXT_DEBUG_REGISTERS)
    {
        /* Get debug registers */
        /* TODO: Copy debug registers */
    }
}

VOID
NTAPI
PspSetContext(IN OUT PKTRAP_FRAME TrapFrame,
              OUT PVOID NonVolatileContext,
              IN PCONTEXT Context,
              IN KPROCESSOR_MODE Mode)
{
    /* Check what to set */
    if (Context->ContextFlags & CONTEXT_CONTROL)
    {
        /* Set control registers */
        TrapFrame->Sp = Context->Sp;
        TrapFrame->Pc = Context->Pc;
        TrapFrame->Spsr = Context->Cpsr;
    }
    
    if (Context->ContextFlags & CONTEXT_INTEGER)
    {
        /* Set integer registers */
        TrapFrame->X0 = Context->X[0];
        TrapFrame->X1 = Context->X[1];
        TrapFrame->X2 = Context->X[2];
        TrapFrame->X3 = Context->X[3];
        TrapFrame->X4 = Context->X[4];
        TrapFrame->X5 = Context->X[5];
        TrapFrame->X6 = Context->X[6];
        TrapFrame->X7 = Context->X[7];
        TrapFrame->X8 = Context->X[8];
        TrapFrame->X9 = Context->X[9];
        TrapFrame->X10 = Context->X[10];
        TrapFrame->X11 = Context->X[11];
        TrapFrame->X12 = Context->X[12];
        TrapFrame->X13 = Context->X[13];
        TrapFrame->X14 = Context->X[14];
        TrapFrame->X15 = Context->X[15];
        TrapFrame->X16 = Context->X[16];
        TrapFrame->X17 = Context->X[17];
        TrapFrame->X18 = Context->X[18];
        /* FIXME TODO: X19-X28 are callee-saved and should be in KEXCEPTION_FRAME */
        /* Cannot set them in TrapFrame as they don't exist there */
        TrapFrame->Fp = Context->Fp;
        TrapFrame->Lr = Context->Lr;
    }
    
    if (Context->ContextFlags & CONTEXT_FLOATING_POINT)
    {
        /* Set floating point registers */
        /* TODO: Copy FP/NEON registers */
    }
    
    if (Context->ContextFlags & CONTEXT_DEBUG_REGISTERS)
    {
        /* Set debug registers */
        /* TODO: Copy debug registers */
    }
}

VOID
NTAPI
KeTrapFrameToContext(IN PKTRAP_FRAME TrapFrame,
                     IN PKEXCEPTION_FRAME ExceptionFrame,
                     IN OUT PCONTEXT Context)
{
    /* Convert trap frame to context */
    PspGetContext(TrapFrame, NULL, Context);
}

VOID
NTAPI
KeContextToTrapFrame(IN PCONTEXT Context,
                     IN OUT PKEXCEPTION_FRAME ExceptionFrame,
                     IN OUT PKTRAP_FRAME TrapFrame,
                     IN ULONG ContextFlags,
                     IN KPROCESSOR_MODE PreviousMode)
{
    /* Convert context to trap frame */
    PspSetContext(TrapFrame, NULL, Context, PreviousMode);
}

VOID
NTAPI
PspGetOrSetContextKernelRoutine(IN PKAPC Apc,
                                IN OUT PKNORMAL_ROUTINE *NormalRoutine,
                                IN OUT PVOID *NormalContext,
                                IN OUT PVOID *SystemArgument1,
                                IN OUT PVOID *SystemArgument2)
{
    /* ARM64 implementation stub */
    DPRINT("PspGetOrSetContextKernelRoutine for ARM64\n");
    
    /* This should handle get/set context operations */
    /* For now, just do nothing */
    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
}

/* ARM64 structures - removed duplicate definitions, use headers instead */