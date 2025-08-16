/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 Exception Vector Table and Trap Handlers
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

/* Exception vector table alignment */
.text
.align 11

/* Exception vector table */
.global KiARM64ExceptionVectors
KiARM64ExceptionVectors:

/* Current EL with SP0 */
.align 7
KiSynchronousExceptionSP0:
    b KiUnexpectedInterrupt

.align 7
KiIrqSP0:
    b KiUnexpectedInterrupt

.align 7
KiFiqSP0:
    b KiUnexpectedInterrupt

.align 7
KiSystemErrorSP0:
    b KiUnexpectedInterrupt

/* Current EL with SPx */
.align 7
KiSynchronousExceptionSPx:
    /* Save trap frame */
    sub sp, sp, #0x390  /* sizeof(KTRAP_FRAME) */
    
    /* Save general purpose registers */
    stp x0, x1, [sp, #0x00]
    stp x2, x3, [sp, #0x10]
    stp x4, x5, [sp, #0x20]
    stp x6, x7, [sp, #0x30]
    stp x8, x9, [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    stp x20, x21, [sp, #0xA0]
    stp x22, x23, [sp, #0xB0]
    stp x24, x25, [sp, #0xC0]
    stp x26, x27, [sp, #0xD0]
    stp x28, x29, [sp, #0xE0]
    str x30, [sp, #0xF0]
    
    /* Save SP and PC */
    mrs x0, sp_el0
    str x0, [sp, #0xF8]
    mrs x0, elr_el1
    str x0, [sp, #0x100]
    
    /* Save CPSR */
    mrs x0, spsr_el1
    str x0, [sp, #0x108]
    
    /* Call exception handler */
    mov x0, sp
    bl KiSynchronousException
    
    /* Restore CPSR */
    ldr x0, [sp, #0x108]
    msr spsr_el1, x0
    
    /* Restore PC */
    ldr x0, [sp, #0x100]
    msr elr_el1, x0
    
    /* Restore SP */
    ldr x0, [sp, #0xF8]
    msr sp_el0, x0
    
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

.align 7
KiIrqSPx:
    /* Save trap frame */
    sub sp, sp, #0x390
    
    /* Save general purpose registers */
    stp x0, x1, [sp, #0x00]
    stp x2, x3, [sp, #0x10]
    stp x4, x5, [sp, #0x20]
    stp x6, x7, [sp, #0x30]
    stp x8, x9, [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    stp x20, x21, [sp, #0xA0]
    stp x22, x23, [sp, #0xB0]
    stp x24, x25, [sp, #0xC0]
    stp x26, x27, [sp, #0xD0]
    stp x28, x29, [sp, #0xE0]
    str x30, [sp, #0xF0]
    
    /* Save SP and PC */
    mrs x0, sp_el0
    str x0, [sp, #0xF8]
    mrs x0, elr_el1
    str x0, [sp, #0x100]
    
    /* Save CPSR */
    mrs x0, spsr_el1
    str x0, [sp, #0x108]
    
    /* Call interrupt handler */
    mov x0, sp
    bl KiInterruptDispatch
    
    /* Restore CPSR */
    ldr x0, [sp, #0x108]
    msr spsr_el1, x0
    
    /* Restore PC */
    ldr x0, [sp, #0x100]
    msr elr_el1, x0
    
    /* Restore SP */
    ldr x0, [sp, #0xF8]
    msr sp_el0, x0
    
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

.align 7
KiFiqSPx:
    b KiUnexpectedInterrupt

.align 7
KiSystemErrorSPx:
    b KiUnexpectedInterrupt

/* Lower EL using AArch64 */
.align 7
KiSynchronousExceptionLowerEL:
    /* Save trap frame */
    sub sp, sp, #0x390
    
    /* Save general purpose registers */
    stp x0, x1, [sp, #0x00]
    stp x2, x3, [sp, #0x10]
    stp x4, x5, [sp, #0x20]
    stp x6, x7, [sp, #0x30]
    stp x8, x9, [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    stp x20, x21, [sp, #0xA0]
    stp x22, x23, [sp, #0xB0]
    stp x24, x25, [sp, #0xC0]
    stp x26, x27, [sp, #0xD0]
    stp x28, x29, [sp, #0xE0]
    str x30, [sp, #0xF0]
    
    /* Save SP and PC */
    mrs x0, sp_el0
    str x0, [sp, #0xF8]
    mrs x0, elr_el1
    str x0, [sp, #0x100]
    
    /* Save CPSR */
    mrs x0, spsr_el1
    str x0, [sp, #0x108]
    
    /* Call exception handler */
    mov x0, sp
    bl KiSynchronousException
    
    /* Restore CPSR */
    ldr x0, [sp, #0x108]
    msr spsr_el1, x0
    
    /* Restore PC */
    ldr x0, [sp, #0x100]
    msr elr_el1, x0
    
    /* Restore SP */
    ldr x0, [sp, #0xF8]
    msr sp_el0, x0
    
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

.align 7
KiIrqLowerEL:
    /* Save trap frame */
    sub sp, sp, #0x390
    
    /* Save general purpose registers */
    stp x0, x1, [sp, #0x00]
    stp x2, x3, [sp, #0x10]
    stp x4, x5, [sp, #0x20]
    stp x6, x7, [sp, #0x30]
    stp x8, x9, [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    stp x20, x21, [sp, #0xA0]
    stp x22, x23, [sp, #0xB0]
    stp x24, x25, [sp, #0xC0]
    stp x26, x27, [sp, #0xD0]
    stp x28, x29, [sp, #0xE0]
    str x30, [sp, #0xF0]
    
    /* Save SP and PC */
    mrs x0, sp_el0
    str x0, [sp, #0xF8]
    mrs x0, elr_el1
    str x0, [sp, #0x100]
    
    /* Save CPSR */
    mrs x0, spsr_el1
    str x0, [sp, #0x108]
    
    /* Call interrupt handler */
    mov x0, sp
    bl KiInterruptDispatch
    
    /* Restore CPSR */
    ldr x0, [sp, #0x108]
    msr spsr_el1, x0
    
    /* Restore PC */
    ldr x0, [sp, #0x100]
    msr elr_el1, x0
    
    /* Restore SP */
    ldr x0, [sp, #0xF8]
    msr sp_el0, x0
    
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

.align 7
KiFiqLowerEL:
    b KiUnexpectedInterrupt

.align 7
KiSystemErrorLowerEL:
    b KiUnexpectedInterrupt

/* Lower EL using AArch32 */
.align 7
KiSynchronousExceptionLowerEL32:
    b KiUnexpectedInterrupt

.align 7
KiIrqLowerEL32:
    b KiUnexpectedInterrupt

.align 7
KiFiqLowerEL32:
    b KiUnexpectedInterrupt

.align 7
KiSystemErrorLowerEL32:
    b KiUnexpectedInterrupt

/* Unexpected interrupt handler */
KiUnexpectedInterrupt:
    /* Infinite loop for now */
1:  wfe
    b 1b

/* System service handler - Real implementation in syscall.c */
.global KiSystemServiceAsm
KiSystemServiceAsm:
    /* TODO: Implement system service dispatch */
    ret

/* Interrupt dispatch handler */
.global KiInterruptDispatch
KiInterruptDispatch:
    /* TODO: Implement interrupt dispatch */
    ret

/* Initialize exception vectors */
.global KiInitializeExceptionVectors
KiInitializeExceptionVectors:
    /* Load address of exception vector table */
    adr x0, KiARM64ExceptionVectors
    
    /* Set VBAR_EL1 to point to our exception vector table */
    msr vbar_el1, x0
    
    /* Synchronize */
    isb
    
    ret

/* EOF */