/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 Context Switching
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* INCLUDES ******************************************************************/

.text

/* MACROS ********************************************************************/

/* Thread context offsets - these should match KTHREAD structure */
#define KTHREAD_KERNEL_STACK    0x28
#define KTHREAD_TRAP_FRAME      0x90
#define KTHREAD_CALLBACK_STACK  0x98
#define KTHREAD_APC_STATE       0x30
#define KTHREAD_CONTEXT_SWITCHES 0x48

/* Context frame offsets */
#define CONTEXT_X19             0x00
#define CONTEXT_X20             0x08
#define CONTEXT_X21             0x10
#define CONTEXT_X22             0x18
#define CONTEXT_X23             0x20
#define CONTEXT_X24             0x28
#define CONTEXT_X25             0x30
#define CONTEXT_X26             0x38
#define CONTEXT_X27             0x40
#define CONTEXT_X28             0x48
#define CONTEXT_FP              0x50
#define CONTEXT_LR              0x58
#define CONTEXT_SP              0x60
#define CONTEXT_PC              0x68
#define CONTEXT_CPSR            0x70
#define CONTEXT_FPSR            0x78
#define CONTEXT_FPCR            0x80
#define CONTEXT_V               0x90  /* NEON/FP registers start here */

/* FUNCTIONS *****************************************************************/

/*
 * KiSwapContext(
 *     IN PKTHREAD OldThread,      // X0
 *     IN PKTHREAD NewThread)      // X1
 * 
 * Performs thread context switch
 */
.global KiSwapContext
KiSwapContext:
    /* Save callee-saved registers of old thread */
    sub sp, sp, #0x1A0
    
    /* Save general purpose registers x19-x28 */
    stp x19, x20, [sp, #CONTEXT_X19]
    stp x21, x22, [sp, #CONTEXT_X21]
    stp x23, x24, [sp, #CONTEXT_X23]
    stp x25, x26, [sp, #CONTEXT_X25]
    stp x27, x28, [sp, #CONTEXT_X27]
    
    /* Save frame pointer and link register */
    stp x29, x30, [sp, #CONTEXT_FP]
    
    /* Save stack pointer */
    mov x2, sp
    add x2, x2, #0x1A0  /* Adjust for frame */
    str x2, [sp, #CONTEXT_SP]
    
    /* Save FP/NEON registers if used */
    mrs x2, fpcr
    mrs x3, fpsr
    stp x2, x3, [sp, #CONTEXT_FPCR]
    
    /* Save NEON registers q8-q15 (callee-saved) */
    stp q8, q9, [sp, #CONTEXT_V]
    stp q10, q11, [sp, #CONTEXT_V + 0x20]
    stp q12, q13, [sp, #CONTEXT_V + 0x40]
    stp q14, q15, [sp, #CONTEXT_V + 0x60]
    
    /* Save old thread's kernel stack */
    mov x3, sp
    str x3, [x0, #KTHREAD_KERNEL_STACK]
    
    /* Get PCR (Processor Control Region) */
    mrs x2, tpidr_el1
    
    /* Update current thread in PCR */
    str x1, [x2, #0x8]  /* Prcb.CurrentThread */
    
    /* Increment context switch count */
    ldr x3, [x1, #KTHREAD_CONTEXT_SWITCHES]
    add x3, x3, #1
    str x3, [x1, #KTHREAD_CONTEXT_SWITCHES]
    
    /* Load new thread's kernel stack */
    ldr x3, [x1, #KTHREAD_KERNEL_STACK]
    mov sp, x3
    
    /* Restore NEON registers */
    ldp q8, q9, [sp, #CONTEXT_V]
    ldp q10, q11, [sp, #CONTEXT_V + 0x20]
    ldp q12, q13, [sp, #CONTEXT_V + 0x40]
    ldp q14, q15, [sp, #CONTEXT_V + 0x60]
    
    /* Restore FP control registers */
    ldp x2, x3, [sp, #CONTEXT_FPCR]
    msr fpcr, x2
    msr fpsr, x3
    
    /* Restore general purpose registers */
    ldp x19, x20, [sp, #CONTEXT_X19]
    ldp x21, x22, [sp, #CONTEXT_X21]
    ldp x23, x24, [sp, #CONTEXT_X23]
    ldp x25, x26, [sp, #CONTEXT_X25]
    ldp x27, x28, [sp, #CONTEXT_X27]
    ldp x29, x30, [sp, #CONTEXT_FP]
    
    /* Clean up stack frame */
    add sp, sp, #0x1A0
    
    /* Return to new thread */
    ret

/*
 * KiThreadStartup(
 *     IN PKSTART_ROUTINE StartRoutine,  // X0
 *     IN PVOID StartContext)             // X1
 * 
 * Start execution of a new thread
 */
.global KiThreadStartupAsm
KiThreadStartupAsm:
    /* Enable interrupts */
    msr daifclr, #2
    
    /* Lower IRQL to PASSIVE_LEVEL */
    mov x0, #0
    bl KeLowerIrql
    
    /* Call the thread's start routine */
    mov x2, x0  /* Save StartRoutine */
    mov x0, x1  /* Pass StartContext as parameter */
    blr x2      /* Call StartRoutine(StartContext) */
    
    /* Thread should never return, but if it does... */
    mov x0, #0x44  /* THREAD_TERMINATE bug check */
    bl KeBugCheck
    
    /* Should never reach here */
1:  wfe
    b 1b

/*
 * KiSwitchKernelStack(
 *     IN PVOID StackBase,         // X0
 *     IN PVOID StackLimit)        // X1
 * 
 * Switch to a new kernel stack
 */
.global KiSwitchKernelStack
KiSwitchKernelStack:
    /* Save current context on old stack */
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    
    /* Calculate offset from old to new stack */
    mov x2, sp
    sub x3, x0, x1      /* New stack size */
    sub x3, x0, x3      /* New stack top */
    
    /* Copy old stack contents to new stack */
    mov x4, sp
    mov x5, x3
1:
    ldp x6, x7, [x4], #16
    stp x6, x7, [x5], #16
    cmp x4, x29
    b.lt 1b
    
    /* Calculate new SP */
    sub x3, x3, x2
    add sp, x0, x3
    
    /* Update PCR with new stack limits */
    mrs x2, tpidr_el1
    str x0, [x2, #0x20]  /* Prcb.KernelStack */
    str x1, [x2, #0x28]  /* Prcb.StackLimit */
    
    /* Restore context from new stack */
    ldp x29, x30, [sp], #16
    ret

/*
 * KiInitializeContextThread(
 *     IN PKTHREAD Thread,         // X0
 *     IN PKSYSTEM_ROUTINE SystemRoutine, // X1
 *     IN PKSTART_ROUTINE StartRoutine,   // X2
 *     IN PVOID StartContext,      // X3
 *     IN PCONTEXT Context)        // X4
 * 
 * Initialize thread context for first run
 */
.global KiInitializeContextThreadAsm
KiInitializeContextThreadAsm:
    /* Get the thread's kernel stack */
    ldr x5, [x0, #KTHREAD_KERNEL_STACK]
    
    /* Reserve space for context frame */
    sub x5, x5, #0x1A0
    
    /* Clear the context frame */
    mov x6, #0
    mov x7, x5
    mov x8, #0x1A0
1:
    str xzr, [x7], #8
    subs x8, x8, #8
    b.ne 1b
    
    /* Setup initial context - use ldr for external symbol */
    ldr x6, =KiThreadStartup
    str x6, [x5, #CONTEXT_PC]
    str x2, [x5, #CONTEXT_X19]  /* StartRoutine */
    str x3, [x5, #CONTEXT_X20]  /* StartContext */
    
    /* If SystemRoutine provided, use it instead */
    cbz x1, 2f
    str x1, [x5, #CONTEXT_PC]
2:
    
    /* If Context provided, copy user context */
    cbz x4, 3f
    /* TODO: Copy user context to thread */
3:
    
    /* Save the initial kernel stack in thread */
    str x5, [x0, #KTHREAD_KERNEL_STACK]
    
    ret

/*
 * KiDispatchInterrupt(VOID)
 * 
 * Dispatch pending DPCs and check for thread scheduling
 */
.global KiDispatchInterrupt
KiDispatchInterrupt:
    /* Save registers */
    stp x29, x30, [sp, #-16]!
    mov x29, sp
    
    /* Get PCR */
    mrs x0, tpidr_el1
    
    /* Check for pending DPCs */
    ldrb w1, [x0, #0x31]  /* Prcb.DpcInterruptRequested */
    cbz w1, 1f
    
    /* Clear DPC interrupt request */
    strb wzr, [x0, #0x31]
    
    /* Process DPC queue */
    bl KiRetireDpcList
    
1:
    /* Check for thread quantum end */
    ldr x1, [x0, #0x8]   /* Prcb.CurrentThread */
    ldrb w2, [x1, #0x50] /* Thread.Quantum */
    cbz w2, 2f
    
    /* Decrement quantum */
    sub w2, w2, #1
    strb w2, [x1, #0x50]
    cbnz w2, 3f
    
2:
    /* Quantum expired, request thread switch */
    mov w2, #1
    strb w2, [x0, #0x30]  /* Prcb.QuantumEnd */
    
3:
    /* Restore registers and return */
    ldp x29, x30, [sp], #16
    ret

/*
 * KiCallUserMode(
 *     IN PVOID *OutputBuffer,     // X0
 *     IN PULONG OutputLength)     // X1
 * 
 * Transition to user mode
 */
.global KiCallUserModeAsm
KiCallUserModeAsm:
    /* Get current thread */
    mrs x2, tpidr_el1
    ldr x2, [x2, #0x8]
    
    /* Get trap frame */
    ldr x3, [x2, #KTHREAD_TRAP_FRAME]
    
    /* Set user mode in SPSR */
    ldr x4, [x3, #0x108]  /* TrapFrame.Cpsr */
    and x4, x4, #~0x0F    /* Clear mode bits */
    str x4, [x3, #0x108]
    
    /* Setup return to user mode */
    msr spsr_el1, x4
    ldr x4, [x3, #0x100]  /* TrapFrame.Pc */
    msr elr_el1, x4
    
    /* Switch to user stack */
    ldr x4, [x3, #0xF8]   /* TrapFrame.Sp */
    msr sp_el0, x4
    
    /* Return to user mode */
    eret

/*
 * KiServiceExit(
 *     IN PKTRAP_FRAME TrapFrame,  // X0
 *     IN NTSTATUS Status)          // X1
 * 
 * Exit from system service
 */
.global KiServiceExit
KiServiceExit:
    /* Store return value in trap frame */
    str x1, [x0, #0x00]  /* TrapFrame.X0 = Status */
    
    /* Check for pending APCs */
    bl KiCheckForApcDelivery
    
    /* Restore trap frame and return to user mode */
    mov sp, x0
    
    /* Restore CPSR */
    ldr x1, [sp, #0x108]
    msr spsr_el1, x1
    
    /* Restore PC */
    ldr x1, [sp, #0x100]
    msr elr_el1, x1
    
    /* Restore SP */
    ldr x1, [sp, #0xF8]
    msr sp_el0, x1
    
    /* Restore general purpose registers */
    ldr x30, [sp, #0xF0]
    ldp x28, x29, [sp, #0xE0]
    ldp x26, x27, [sp, #0xD0]
    ldp x24, x25, [sp, #0xC0]
    ldp x22, x23, [sp, #0xB0]
    ldp x20, x21, [sp, #0xA0]
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8, x9, [sp, #0x40]
    ldp x6, x7, [sp, #0x30]
    ldp x4, x5, [sp, #0x20]
    ldp x2, x3, [sp, #0x10]
    ldp x0, x1, [sp, #0x00]
    
    /* Return from exception */
    add sp, sp, #0x390
    eret

/* EOF */