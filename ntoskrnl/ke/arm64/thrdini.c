/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Thread Initialization
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

VOID
NTAPI
KiInitializeContextThread(IN PKTHREAD Thread,
                          IN PKSYSTEM_ROUTINE SystemRoutine,
                          IN PKSTART_ROUTINE StartRoutine,
                          IN PVOID StartContext,
                          IN PCONTEXT ContextFrame)
{
    PKTRAP_FRAME TrapFrame;
    ULONG_PTR InitialStack;
    
    /* Get the initial stack */
    InitialStack = (ULONG_PTR)Thread->InitialStack;
    
    /* Align stack to 16 bytes (ARM64 requirement) */
    InitialStack &= ~0xF;
    
    /* Setup the trap frame */
    TrapFrame = (PKTRAP_FRAME)(InitialStack - sizeof(KTRAP_FRAME));
    
    /* Zero the trap frame */
    RtlZeroMemory(TrapFrame, sizeof(KTRAP_FRAME));
    
    /* Set the thread start address */
    TrapFrame->Pc = (ULONG64)StartRoutine;
    
    /* Set the start context in X0 */
    TrapFrame->X0 = (ULONG64)StartContext;
    
    /* Set the system routine in X1 */
    TrapFrame->X1 = (ULONG64)SystemRoutine;
    
    /* Setup SPSR for EL1 */
    TrapFrame->Spsr = 0x5; /* EL1h mode, interrupts disabled */
    
    /* If we have a context frame, copy it */
    if (ContextFrame)
    {
        /* Copy general purpose registers */
        TrapFrame->X0 = ContextFrame->X[0];
        TrapFrame->X1 = ContextFrame->X[1];
        TrapFrame->X2 = ContextFrame->X[2];
        TrapFrame->X3 = ContextFrame->X[3];
        TrapFrame->X4 = ContextFrame->X[4];
        TrapFrame->X5 = ContextFrame->X[5];
        TrapFrame->X6 = ContextFrame->X[6];
        TrapFrame->X7 = ContextFrame->X[7];
        
        /* Copy special registers */
        TrapFrame->Sp = ContextFrame->Sp;
        TrapFrame->Pc = ContextFrame->Pc;
        TrapFrame->Spsr = ContextFrame->Cpsr;
    }
    
    /* Set the kernel stack */
    Thread->KernelStack = TrapFrame;
    
    /* Set the trap frame */
    Thread->TrapFrame = TrapFrame;
    
    /* Initialize NPX state */
    Thread->NpxState = 0;
    
    /* Set initial thread state */
    Thread->State = Initialized;
}

VOID
FASTCALL
KiSwapContextInternal(IN PKTHREAD OldThread,
                      IN PKTHREAD NewThread)
{
    PKPCR Pcr;
    PKPRCB Prcb;
    
    /* Get the PCR from TPIDR_EL1 */
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r" (Pcr));
    Prcb = Pcr->Prcb;
    
    /* Update current thread */
    Prcb->CurrentThread = NewThread;
    
    /* Perform the context switch - KiSwapContext needs IRQL not thread */
    KiSwapContext(APC_LEVEL, OldThread);
}

BOOLEAN
FASTCALL
KiSwapContextExit(IN PKTHREAD OldThread,
                  IN PKSWITCHFRAME SwitchFrame)
{
    PKIPCR Pcr;
    PKPRCB Prcb;
    PKTHREAD NewThread;
    
    /* Get the PCR and PRCB */
    Pcr = (PKIPCR)KeGetPcr();
    Prcb = Pcr->Prcb;
    
    /* Get the new thread */
    NewThread = Prcb->NextThread;
    
    /* Clear next thread */
    Prcb->NextThread = NULL;
    
    /* Switch to new thread */
    KiSwapContextInternal(OldThread, NewThread);
    
    /* Return TRUE to indicate we switched */
    return TRUE;
}

VOID
NTAPI
KiThreadStartup(IN PKTHREAD Thread)
{
    PKSTART_ROUTINE StartRoutine;
    PVOID StartContext;
    PKSYSTEM_ROUTINE SystemRoutine;
    PKTRAP_FRAME TrapFrame;
    
    /* Get the trap frame */
    TrapFrame = (PKTRAP_FRAME)Thread->TrapFrame;
    
    /* Get the start parameters */
    StartContext = (PVOID)TrapFrame->X0;
    SystemRoutine = (PKSYSTEM_ROUTINE)TrapFrame->X1;
    StartRoutine = (PKSTART_ROUTINE)TrapFrame->Pc;
    
    /* Lower IRQL to PASSIVE_LEVEL */
    KfLowerIrql(PASSIVE_LEVEL);
    
    /* Enable interrupts */
    _enable();
    
    /* Call the system routine if present */
    if (SystemRoutine)
    {
        SystemRoutine(StartRoutine, StartContext);
    }
    else if (StartRoutine)
    {
        /* Call the start routine directly */
        StartRoutine(StartContext);
    }
    
    /* We should never get here */
    KeBugCheck(NO_USER_MODE_CONTEXT);
}

PVOID
NTAPI
KeSwitchKernelStack(IN PVOID StackBase,
                    IN PVOID StackLimit)
{
    PKTHREAD Thread = KeGetCurrentThread();
    PVOID OldStackBase;
    
    /* Save old stack base */
    OldStackBase = Thread->InitialStack;
    
    /* Update thread stack information */
    Thread->StackBase = StackBase;
    Thread->StackLimit = (ULONG_PTR)StackLimit;
    Thread->InitialStack = StackBase;
    Thread->KernelStack = StackBase;
    
    /* ARM64: Switch SP to new stack */
    /* This is simplified - real implementation would need to copy current stack frame */
    __asm__ __volatile__("mov sp, %0" : : "r" (StackBase));
    
    return OldStackBase;
}

/* ARM64 specific structures - removed duplicate definitions, use headers instead */

VOID
NTAPI
KiRundownThread(IN PKTHREAD Thread)
{
    /* ARM64 thread rundown stub */
    DPRINT1("ARM64: KiRundownThread called for thread %p\n", Thread);
    
    /* TODO: Implement proper thread rundown for ARM64
     * - Clean up thread-specific resources
     * - Release thread locks
     * - Clean up FPU/NEON state
     */
}