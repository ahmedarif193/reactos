#include <asm.h>
#include <ksarm64.h>

    .text
    .align  4

    .extern KiArm64HandleSynchronousException
    .extern KiArm64BugCheckEntry
    .extern KiTrapReturn
    .extern KiArm64VectorLogOnly

.equ ARM64_EARLY_STATE_VECTORID,      0x0
.equ ARM64_EARLY_STATE_ESR,           0x8
.equ ARM64_EARLY_STATE_FAR,           0x10
.equ ARM64_EARLY_STATE_ELR,           0x18
.equ ARM64_EARLY_STATE_SPSR,          0x20
.equ ARM64_EARLY_STATE_REGS,          0x28
.equ ARM64_EARLY_STATE_REG_SP,        0x120   /* X[0..30] = 0x28 + (31*8) = 0x120 */
.equ ARM64_EARLY_STATE_REG_PC,        0x128   /* Sp + 8 = 0x128 */
.equ ARM64_EARLY_STATE_REG_PSTATE,    0x130   /* Pc + 8 = 0x130 */
.equ ARM64_EARLY_STATE_TRAP_PTR,      0x138
.equ ARM64_EARLY_STATE_EXC_PTR,       0x140
.equ ARM64_EARLY_STATE_TRAP_FRAME,    0x148
.equ ARM64_EARLY_STATE_SIZE,          0x380

/*
 * ============================================================================
 * FUNCTION DEFINITIONS
 * All functions with SEH directives must be defined BEFORE the vector table
 * because .space directives interfere with SEH function size calculation.
 * ============================================================================
 */

/*
 * KiArm64SyncBugcheckLoop - Bugcheck loop for unrecoverable sync exceptions.
 * Entry: x0 = pointer to early state on stack
 * This function never returns.
 */
    .global KiArm64SyncBugcheckLoop
    .seh_proc KiArm64SyncBugcheckLoop
KiArm64SyncBugcheckLoop:
    .seh_endprologue
.Lbugcheck_loop:
    bl KiArm64BugCheckEntry
    b .Lbugcheck_loop
    .seh_endproc

/*
 * KiArm64SyncVectorCommon - Common synchronous exception handler
 * Entry: w0 = vector ID, x29/x30 saved on stack at [sp], sp = original - 16
 * This function sets up the early state and dispatches to C handler.
 * Never returns - either goes to KiTrapReturn or bugcheck loop.
 */
    .global KiArm64SyncVectorCommon
    .seh_proc KiArm64SyncVectorCommon
KiArm64SyncVectorCommon:
    .seh_endprologue
    /* Save vector ID in callee-saved register */
    mov w19, w0
    /* Large stack allocation for early state */
    sub sp, sp, #ARM64_EARLY_STATE_SIZE
    /* Save all registers */
    str x0,  [sp, #(ARM64_EARLY_STATE_REGS + 0 * 8)]
    str x1,  [sp, #(ARM64_EARLY_STATE_REGS + 1 * 8)]
    str x2,  [sp, #(ARM64_EARLY_STATE_REGS + 2 * 8)]
    str x3,  [sp, #(ARM64_EARLY_STATE_REGS + 3 * 8)]
    str x4,  [sp, #(ARM64_EARLY_STATE_REGS + 4 * 8)]
    str x5,  [sp, #(ARM64_EARLY_STATE_REGS + 5 * 8)]
    str x6,  [sp, #(ARM64_EARLY_STATE_REGS + 6 * 8)]
    str x7,  [sp, #(ARM64_EARLY_STATE_REGS + 7 * 8)]
    str x8,  [sp, #(ARM64_EARLY_STATE_REGS + 8 * 8)]
    str x9,  [sp, #(ARM64_EARLY_STATE_REGS + 9 * 8)]
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 10 * 8)]
    str x11, [sp, #(ARM64_EARLY_STATE_REGS + 11 * 8)]
    str x12, [sp, #(ARM64_EARLY_STATE_REGS + 12 * 8)]
    str x13, [sp, #(ARM64_EARLY_STATE_REGS + 13 * 8)]
    str x14, [sp, #(ARM64_EARLY_STATE_REGS + 14 * 8)]
    str x15, [sp, #(ARM64_EARLY_STATE_REGS + 15 * 8)]
    str x16, [sp, #(ARM64_EARLY_STATE_REGS + 16 * 8)]
    str x17, [sp, #(ARM64_EARLY_STATE_REGS + 17 * 8)]
    str x18, [sp, #(ARM64_EARLY_STATE_REGS + 18 * 8)]
    str x19, [sp, #(ARM64_EARLY_STATE_REGS + 19 * 8)]
    str x20, [sp, #(ARM64_EARLY_STATE_REGS + 20 * 8)]
    str x21, [sp, #(ARM64_EARLY_STATE_REGS + 21 * 8)]
    str x22, [sp, #(ARM64_EARLY_STATE_REGS + 22 * 8)]
    str x23, [sp, #(ARM64_EARLY_STATE_REGS + 23 * 8)]
    str x24, [sp, #(ARM64_EARLY_STATE_REGS + 24 * 8)]
    str x25, [sp, #(ARM64_EARLY_STATE_REGS + 25 * 8)]
    str x26, [sp, #(ARM64_EARLY_STATE_REGS + 26 * 8)]
    str x27, [sp, #(ARM64_EARLY_STATE_REGS + 27 * 8)]
    str x28, [sp, #(ARM64_EARLY_STATE_REGS + 28 * 8)]
    /* Get original x29/x30 from caller's frame (at sp + ARM64_EARLY_STATE_SIZE) */
    add x9, sp, #ARM64_EARLY_STATE_SIZE
    ldr x10, [x9]           /* Original x29 */
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 29 * 8)]
    ldr x11, [x9, #8]       /* Original x30 */
    str x11, [sp, #(ARM64_EARLY_STATE_REGS + 30 * 8)]
    /* Calculate original SP (before caller's push) */
    add x12, x9, #16
    str x12, [sp, #ARM64_EARLY_STATE_REG_SP]
    /* Save exception registers */
    mrs x13, elr_el1
    str x13, [sp, #ARM64_EARLY_STATE_ELR]
    str x13, [sp, #ARM64_EARLY_STATE_REG_PC]
    mrs x14, spsr_el1
    str x14, [sp, #ARM64_EARLY_STATE_SPSR]
    str x14, [sp, #ARM64_EARLY_STATE_REG_PSTATE]
    mrs x15, esr_el1
    str x15, [sp, #ARM64_EARLY_STATE_ESR]
    mrs x16, far_el1
    str x16, [sp, #ARM64_EARLY_STATE_FAR]
    /* Store vector ID */
    str x19, [sp, #ARM64_EARLY_STATE_VECTORID]
    str xzr, [sp, #ARM64_EARLY_STATE_TRAP_PTR]
    str xzr, [sp, #ARM64_EARLY_STATE_EXC_PTR]
    /* Memory barriers */
    dsb sy
    isb
    /* Call C handler */
    mov x0, sp
    bl KiArm64HandleSynchronousException
    dsb sy
    isb
    /* Check result */
    cbz x0, .Lsync_bugcheck
    /* Success path */
    dsb ishst
    ldr x2, [sp, #ARM64_EARLY_STATE_TRAP_PTR]
    ldr x3, [sp, #ARM64_EARLY_STATE_EXC_PTR]
    dsb ld
    add sp, sp, #ARM64_EARLY_STATE_SIZE
    /* Pop caller's frame */
    ldp x29, x30, [sp], #16
    /* Set up args for KiTrapReturn */
    mov x0, x2
    mov x1, x3
    dsb sy
    isb
    b KiTrapReturn
.Lsync_bugcheck:
    mov x0, sp
    b KiArm64SyncBugcheckLoop
    .seh_endproc

/* Sync handler: SyncSp0 (vector 0) */
    .global KiArm64PermVectorSyncSp0_Handler
    .seh_proc KiArm64PermVectorSyncSp0_Handler
KiArm64PermVectorSyncSp0_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov x29, sp
    .seh_set_fp
    .seh_endprologue
    mov w0, #0
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* Sync handler: SyncSpx (vector 4) */
    .global KiArm64PermVectorSyncSpx_Handler
    .seh_proc KiArm64PermVectorSyncSpx_Handler
KiArm64PermVectorSyncSpx_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov x29, sp
    .seh_set_fp
    .seh_endprologue
    mov w0, #4
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* Sync handler: SyncLowerA64 (vector 8) */
    .global KiArm64PermVectorSyncLowerA64_Handler
    .seh_proc KiArm64PermVectorSyncLowerA64_Handler
KiArm64PermVectorSyncLowerA64_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov x29, sp
    .seh_set_fp
    .seh_endprologue
    mov w0, #8
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* Sync handler: SyncLowerA32 (vector 12) */
    .global KiArm64PermVectorSyncLowerA32_Handler
    .seh_proc KiArm64PermVectorSyncLowerA32_Handler
KiArm64PermVectorSyncLowerA32_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov x29, sp
    .seh_set_fp
    .seh_endprologue
    mov w0, #12
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* SError handler: SErrorSp0 (vector 3) */
    .global KiArm64PermVectorSErrorSp0_Handler
    .seh_proc KiArm64PermVectorSErrorSp0_Handler
KiArm64PermVectorSErrorSp0_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov x29, sp
    .seh_set_fp
    .seh_endprologue
    mov w0, #3
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* SError handler: SErrorSpx (vector 7) */
    .global KiArm64PermVectorSErrorSpx_Handler
    .seh_proc KiArm64PermVectorSErrorSpx_Handler
KiArm64PermVectorSErrorSpx_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov x29, sp
    .seh_set_fp
    .seh_endprologue
    mov w0, #7
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* SError handler: SErrorLowerA64 (vector 11) */
    .global KiArm64PermVectorSErrorLowerA64_Handler
    .seh_proc KiArm64PermVectorSErrorLowerA64_Handler
KiArm64PermVectorSErrorLowerA64_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov x29, sp
    .seh_set_fp
    .seh_endprologue
    mov w0, #11
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* SError handler: SErrorLowerA32 (vector 15) */
    .global KiArm64PermVectorSErrorLowerA32_Handler
    .seh_proc KiArm64PermVectorSErrorLowerA32_Handler
KiArm64PermVectorSErrorLowerA32_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov x29, sp
    .seh_set_fp
    .seh_endprologue
    mov w0, #15
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* IRQ handler: IrqSp0 (vector 1) */
    .global KiArm64PermVectorIrqSp0_Handler
    .seh_proc KiArm64PermVectorIrqSp0_Handler
KiArm64PermVectorIrqSp0_Handler:
    sub sp, sp, #0xD0
    .seh_stackalloc 0xD0
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    dsb sy
    isb
    mov w0, #1
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #0xD0
    dsb sy
    isb
    eret
    .seh_endproc

/* FIQ handler: FiqSp0 (vector 2) */
    .global KiArm64PermVectorFiqSp0_Handler
    .seh_proc KiArm64PermVectorFiqSp0_Handler
KiArm64PermVectorFiqSp0_Handler:
    sub sp, sp, #0xD0
    .seh_stackalloc 0xD0
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    dsb sy
    isb
    mov w0, #2
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #0xD0
    dsb sy
    isb
    eret
    .seh_endproc

/* IRQ handler: IrqSpx (vector 5) - MAIN KERNEL IRQ VECTOR */
    .global KiArm64PermVectorIrqSpx_Handler
    .seh_proc KiArm64PermVectorIrqSpx_Handler
KiArm64PermVectorIrqSpx_Handler:
    sub sp, sp, #0xD0
    .seh_stackalloc 0xD0
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    dsb sy
    isb
    mov w0, #5
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #0xD0
    dsb sy
    isb
    eret
    .seh_endproc

/* FIQ handler: FiqSpx (vector 6) */
    .global KiArm64PermVectorFiqSpx_Handler
    .seh_proc KiArm64PermVectorFiqSpx_Handler
KiArm64PermVectorFiqSpx_Handler:
    sub sp, sp, #0xD0
    .seh_stackalloc 0xD0
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    dsb sy
    isb
    mov w0, #6
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #0xD0
    dsb sy
    isb
    eret
    .seh_endproc

/* IRQ handler: IrqLowerA64 (vector 9) */
    .global KiArm64PermVectorIrqLowerA64_Handler
    .seh_proc KiArm64PermVectorIrqLowerA64_Handler
KiArm64PermVectorIrqLowerA64_Handler:
    sub sp, sp, #0xD0
    .seh_stackalloc 0xD0
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    dsb sy
    isb
    mov w0, #9
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #0xD0
    dsb sy
    isb
    eret
    .seh_endproc

/* FIQ handler: FiqLowerA64 (vector 10) */
    .global KiArm64PermVectorFiqLowerA64_Handler
    .seh_proc KiArm64PermVectorFiqLowerA64_Handler
KiArm64PermVectorFiqLowerA64_Handler:
    sub sp, sp, #0xD0
    .seh_stackalloc 0xD0
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    dsb sy
    isb
    mov w0, #10
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #0xD0
    dsb sy
    isb
    eret
    .seh_endproc

/* IRQ handler: IrqLowerA32 (vector 13) */
    .global KiArm64PermVectorIrqLowerA32_Handler
    .seh_proc KiArm64PermVectorIrqLowerA32_Handler
KiArm64PermVectorIrqLowerA32_Handler:
    sub sp, sp, #0xD0
    .seh_stackalloc 0xD0
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    dsb sy
    isb
    mov w0, #13
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #0xD0
    dsb sy
    isb
    eret
    .seh_endproc

/* FIQ handler: FiqLowerA32 (vector 14) */
    .global KiArm64PermVectorFiqLowerA32_Handler
    .seh_proc KiArm64PermVectorFiqLowerA32_Handler
KiArm64PermVectorFiqLowerA32_Handler:
    sub sp, sp, #0xD0
    .seh_stackalloc 0xD0
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    dsb sy
    isb
    mov w0, #14
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #0xD0
    dsb sy
    isb
    eret
    .seh_endproc

/* Legacy symbols for external references */
.global KiArm64PermVectorSyncSp0
.set KiArm64PermVectorSyncSp0, KiArm64PermVectorSyncSp0_Handler
.global KiArm64PermVectorSyncSpx
.set KiArm64PermVectorSyncSpx, KiArm64PermVectorSyncSpx_Handler

/*
 * ============================================================================
 * ARM64 EXCEPTION VECTOR TABLE
 * This MUST come AFTER all SEH functions because .space interferes with
 * SEH function size calculations.
 *
 * CRITICAL: Each vector entry MUST be at a 0x80 (128) byte boundary!
 * The ARM64 architecture requires vectors to be aligned at 128-byte intervals.
 * Total table size = 16 entries * 128 bytes = 0x800 bytes
 *
 * The vector table is placed in a separate section ".pvec" with 2048-byte
 * alignment to ensure VBAR_EL1 requirements are met (bits [10:0] must be 0).
 * ============================================================================
 */

    .section .pvec, "ax"
    .balign 2048   /* Ensure 2048-byte alignment for VBAR_EL1 */

    .global KiArm64VectorTable
KiArm64VectorTable:

/* Entry 0 - 0x000: Sync, Current EL SP0 */
    b KiArm64PermVectorSyncSp0_Handler
    .space 0x7C

/* Entry 1 - 0x080: IRQ, Current EL SP0 */
    b KiArm64PermVectorIrqSp0_Handler
    .space 0x7C

/* Entry 2 - 0x100: FIQ, Current EL SP0 */
    b KiArm64PermVectorFiqSp0_Handler
    .space 0x7C

/* Entry 3 - 0x180: SError, Current EL SP0 */
    b KiArm64PermVectorSErrorSp0_Handler
    .space 0x7C

/* Entry 4 - 0x200: Sync, Current EL SPx */
    b KiArm64PermVectorSyncSpx_Handler
    .space 0x7C

/* Entry 5 - 0x280: IRQ, Current EL SPx - MAIN KERNEL IRQ VECTOR */
    b KiArm64PermVectorIrqSpx_Handler
    .space 0x7C

/* Entry 6 - 0x300: FIQ, Current EL SPx */
    b KiArm64PermVectorFiqSpx_Handler
    .space 0x7C

/* Entry 7 - 0x380: SError, Current EL SPx */
    b KiArm64PermVectorSErrorSpx_Handler
    .space 0x7C

/* Entry 8 - 0x400: Sync, Lower EL A64 */
    b KiArm64PermVectorSyncLowerA64_Handler
    .space 0x7C

/* Entry 9 - 0x480: IRQ, Lower EL A64 */
    b KiArm64PermVectorIrqLowerA64_Handler
    .space 0x7C

/* Entry 10 - 0x500: FIQ, Lower EL A64 */
    b KiArm64PermVectorFiqLowerA64_Handler
    .space 0x7C

/* Entry 11 - 0x580: SError, Lower EL A64 */
    b KiArm64PermVectorSErrorLowerA64_Handler
    .space 0x7C

/* Entry 12 - 0x600: Sync, Lower EL A32 */
    b KiArm64PermVectorSyncLowerA32_Handler
    .space 0x7C

/* Entry 13 - 0x680: IRQ, Lower EL A32 */
    b KiArm64PermVectorIrqLowerA32_Handler
    .space 0x7C

/* Entry 14 - 0x700: FIQ, Lower EL A32 */
    b KiArm64PermVectorFiqLowerA32_Handler
    .space 0x7C

/* Entry 15 - 0x780: SError, Lower EL A32 */
    b KiArm64PermVectorSErrorLowerA32_Handler
    .space 0x7C
