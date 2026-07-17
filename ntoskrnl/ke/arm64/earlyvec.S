#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .extern KiArm64EarlyVectorHandler
    .extern KiArm64HandleSynchronousException
    .extern KiArm64BugCheckEntry
    .extern KiTrapReturn

.equ ARM64_EARLY_STATE_VECTORID,      0x0
.equ ARM64_EARLY_STATE_ESR,           0x8
.equ ARM64_EARLY_STATE_FAR,           0x10
.equ ARM64_EARLY_STATE_ELR,           0x18
.equ ARM64_EARLY_STATE_SPSR,          0x20
.equ ARM64_EARLY_STATE_REGS,          0x28
.equ ARM64_EARLY_STATE_REG_SP,        0x120
.equ ARM64_EARLY_STATE_REG_PC,        0x128
.equ ARM64_EARLY_STATE_REG_PSTATE,    0x130
.equ ARM64_EARLY_STATE_TRAP_PTR,      0x138
.equ ARM64_EARLY_STATE_EXC_PTR,       0x140
.equ ARM64_EARLY_STATE_EXC_FRAME,     0x148
.equ ARM64_EARLY_STATE_TRAP_FRAME,    0x210
.equ ARM64_EARLY_STATE_VFP,           0x370
.equ ARM64_EARLY_STATE_FPCR,          0x378
.equ ARM64_EARLY_STATE_FPSR,          0x37C
.equ ARM64_EARLY_STATE_NEON,          0x380
.equ ARM64_EARLY_STATE_SIZE,          0x590

/* Keep the Windows ARM64 16-byte red zone below the interrupted SP untouched. */
.equ ARM64_EARLY_ENTRY_FRAME_SIZE,    0x20

.macro ARM64_CAPTURE_EARLY_VFP
    str xzr, [sp, #ARM64_EARLY_STATE_VFP]
    mrs x9, fpcr
    str w9, [sp, #ARM64_EARLY_STATE_FPCR]
    mrs x9, fpsr
    str w9, [sp, #ARM64_EARLY_STATE_FPSR]
    add x9, sp, #ARM64_EARLY_STATE_NEON
    stp q0, q1, [x9, #0x000]
    stp q2, q3, [x9, #0x020]
    stp q4, q5, [x9, #0x040]
    stp q6, q7, [x9, #0x060]
    stp q8, q9, [x9, #0x080]
    stp q10, q11, [x9, #0x0A0]
    stp q12, q13, [x9, #0x0C0]
    stp q14, q15, [x9, #0x0E0]
    stp q16, q17, [x9, #0x100]
    stp q18, q19, [x9, #0x120]
    stp q20, q21, [x9, #0x140]
    stp q22, q23, [x9, #0x160]
    stp q24, q25, [x9, #0x180]
    stp q26, q27, [x9, #0x1A0]
    stp q28, q29, [x9, #0x1C0]
    stp q30, q31, [x9, #0x1E0]
.endm

/*
 * ============================================================================
 * FUNCTION DEFINITIONS
 * All functions with SEH directives MUST be defined BEFORE the vector table
 * because .space directives interfere with SEH function size calculations.
 * ============================================================================
 */

/* Sync exception handler: SyncSp0 (vector 0) */
    .global KiArm64VectorSyncSp0
    .seh_proc KiArm64VectorSyncSp0
KiArm64VectorSyncSp0:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #ARM64_EARLY_STATE_SIZE
    .seh_stackalloc ARM64_EARLY_STATE_SIZE
    .seh_endprologue
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
    ldr x9, [x29]
    str x9, [sp, #(ARM64_EARLY_STATE_REGS + 29 * 8)]
    ldr x10, [x29, #8]
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 30 * 8)]
    add x11, x29, #ARM64_EARLY_ENTRY_FRAME_SIZE
    str x11, [sp, #ARM64_EARLY_STATE_REG_SP]
    mrs x12, elr_el1
    str x12, [sp, #ARM64_EARLY_STATE_ELR]
    str x12, [sp, #ARM64_EARLY_STATE_REG_PC]
    mrs x13, spsr_el1
    str x13, [sp, #ARM64_EARLY_STATE_SPSR]
    str x13, [sp, #ARM64_EARLY_STATE_REG_PSTATE]
    mrs x14, esr_el1
    str x14, [sp, #ARM64_EARLY_STATE_ESR]
    mrs x15, far_el1
    str x15, [sp, #ARM64_EARLY_STATE_FAR]
    mov w16, #0
    str x16, [sp, #ARM64_EARLY_STATE_VECTORID]
    str xzr, [sp, #ARM64_EARLY_STATE_TRAP_PTR]
    str xzr, [sp, #ARM64_EARLY_STATE_EXC_PTR]
    ARM64_CAPTURE_EARLY_VFP
    mov x0, sp
    mov x19, sp
    bl KiArm64HandleSynchronousException
    cbz x0, .Lsync0_bugcheck
    ldr x2, [sp, #ARM64_EARLY_STATE_TRAP_PTR]
    ldr x3, [sp, #ARM64_EARLY_STATE_EXC_PTR]
    add sp, sp, #ARM64_EARLY_STATE_SIZE
    ldp x29, x30, [sp], #32
    mov x0, x2
    mov x1, x3
    bl KiTrapReturn
.Lsync0_hang:
    wfe
    b .Lsync0_hang
.Lsync0_bugcheck:
    mov x0, x19
    bl KiArm64BugCheckEntry
    b .Lsync0_hang
    .seh_endproc

/* Sync exception handler: SyncSpx (vector 4) */
    .global KiArm64VectorSyncSpx
    .seh_proc KiArm64VectorSyncSpx
KiArm64VectorSyncSpx:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #ARM64_EARLY_STATE_SIZE
    .seh_stackalloc ARM64_EARLY_STATE_SIZE
    .seh_endprologue
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
    ldr x9, [x29]
    str x9, [sp, #(ARM64_EARLY_STATE_REGS + 29 * 8)]
    ldr x10, [x29, #8]
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 30 * 8)]
    add x11, x29, #ARM64_EARLY_ENTRY_FRAME_SIZE
    str x11, [sp, #ARM64_EARLY_STATE_REG_SP]
    mrs x12, elr_el1
    str x12, [sp, #ARM64_EARLY_STATE_ELR]
    str x12, [sp, #ARM64_EARLY_STATE_REG_PC]
    mrs x13, spsr_el1
    str x13, [sp, #ARM64_EARLY_STATE_SPSR]
    str x13, [sp, #ARM64_EARLY_STATE_REG_PSTATE]
    mrs x14, esr_el1
    str x14, [sp, #ARM64_EARLY_STATE_ESR]
    mrs x15, far_el1
    str x15, [sp, #ARM64_EARLY_STATE_FAR]
    mov w16, #4
    str x16, [sp, #ARM64_EARLY_STATE_VECTORID]
    str xzr, [sp, #ARM64_EARLY_STATE_TRAP_PTR]
    str xzr, [sp, #ARM64_EARLY_STATE_EXC_PTR]
    ARM64_CAPTURE_EARLY_VFP
    mov x0, sp
    mov x19, sp
    bl KiArm64HandleSynchronousException
    cbz x0, .Lsync4_bugcheck
    ldr x2, [sp, #ARM64_EARLY_STATE_TRAP_PTR]
    ldr x3, [sp, #ARM64_EARLY_STATE_EXC_PTR]
    add sp, sp, #ARM64_EARLY_STATE_SIZE
    ldp x29, x30, [sp], #32
    mov x0, x2
    mov x1, x3
    bl KiTrapReturn
.Lsync4_hang:
    wfe
    b .Lsync4_hang
.Lsync4_bugcheck:
    mov x0, x19
    bl KiArm64BugCheckEntry
    b .Lsync4_hang
    .seh_endproc

/* Sync exception handler: SyncLowerA64 (vector 8) */
    .global KiArm64VectorSyncLowerA64
    .seh_proc KiArm64VectorSyncLowerA64
KiArm64VectorSyncLowerA64:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #ARM64_EARLY_STATE_SIZE
    .seh_stackalloc ARM64_EARLY_STATE_SIZE
    .seh_endprologue
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
    ldr x9, [x29]
    str x9, [sp, #(ARM64_EARLY_STATE_REGS + 29 * 8)]
    ldr x10, [x29, #8]
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 30 * 8)]
    /* BUG FIX: Lower-EL (vector 8) - user SP is in SP_EL0, not kernel stack */
    mrs x11, sp_el0
    str x11, [sp, #ARM64_EARLY_STATE_REG_SP]
    mrs x12, elr_el1
    str x12, [sp, #ARM64_EARLY_STATE_ELR]
    str x12, [sp, #ARM64_EARLY_STATE_REG_PC]
    mrs x13, spsr_el1
    str x13, [sp, #ARM64_EARLY_STATE_SPSR]
    str x13, [sp, #ARM64_EARLY_STATE_REG_PSTATE]
    mrs x14, esr_el1
    str x14, [sp, #ARM64_EARLY_STATE_ESR]
    mrs x15, far_el1
    str x15, [sp, #ARM64_EARLY_STATE_FAR]
    mov w16, #8
    str x16, [sp, #ARM64_EARLY_STATE_VECTORID]
    str xzr, [sp, #ARM64_EARLY_STATE_TRAP_PTR]
    str xzr, [sp, #ARM64_EARLY_STATE_EXC_PTR]
    ARM64_CAPTURE_EARLY_VFP
    mov x0, sp
    mov x19, sp
    bl KiArm64HandleSynchronousException
    cbz x0, .Lsync8_bugcheck
    ldr x2, [sp, #ARM64_EARLY_STATE_TRAP_PTR]
    ldr x3, [sp, #ARM64_EARLY_STATE_EXC_PTR]
    add sp, sp, #ARM64_EARLY_STATE_SIZE
    ldp x29, x30, [sp], #32
    mov x0, x2
    mov x1, x3
    bl KiTrapReturn
.Lsync8_hang:
    wfe
    b .Lsync8_hang
.Lsync8_bugcheck:
    mov x0, x19
    bl KiArm64BugCheckEntry
    b .Lsync8_hang
    .seh_endproc

/* IRQ handler: IrqSp0 (vector 1) */
    .global KiArm64VectorIrqSp0
    .seh_proc KiArm64VectorIrqSp0
KiArm64VectorIrqSp0:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #1
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lirq1_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lirq1_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lirq1_wfe:
    wfe
    b .Lirq1_wfe
    .seh_endproc

/* FIQ handler: FiqSp0 (vector 2) */
    .global KiArm64VectorFiqSp0
    .seh_proc KiArm64VectorFiqSp0
KiArm64VectorFiqSp0:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #2
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lfiq2_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lfiq2_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lfiq2_wfe:
    wfe
    b .Lfiq2_wfe
    .seh_endproc

/* SError handler: SErrorSp0 (vector 3) */
    .global KiArm64VectorSErrorSp0
    .seh_proc KiArm64VectorSErrorSp0
KiArm64VectorSErrorSp0:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #3
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lserr3_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lserr3_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lserr3_wfe:
    wfe
    b .Lserr3_wfe
    .seh_endproc

/* IRQ handler: IrqSpx (vector 5) */
    .global KiArm64VectorIrqSpx
    .seh_proc KiArm64VectorIrqSpx
KiArm64VectorIrqSpx:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #5
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lirq5_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lirq5_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lirq5_wfe:
    wfe
    b .Lirq5_wfe
    .seh_endproc

/* FIQ handler: FiqSpx (vector 6) */
    .global KiArm64VectorFiqSpx
    .seh_proc KiArm64VectorFiqSpx
KiArm64VectorFiqSpx:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #6
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lfiq6_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lfiq6_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lfiq6_wfe:
    wfe
    b .Lfiq6_wfe
    .seh_endproc

/* SError handler: SErrorSpx (vector 7) */
    .global KiArm64VectorSErrorSpx
    .seh_proc KiArm64VectorSErrorSpx
KiArm64VectorSErrorSpx:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #7
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lserr7_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lserr7_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lserr7_wfe:
    wfe
    b .Lserr7_wfe
    .seh_endproc

/* IRQ handler: IrqLowerA64 (vector 9) */
    .global KiArm64VectorIrqLowerA64
    .seh_proc KiArm64VectorIrqLowerA64
KiArm64VectorIrqLowerA64:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #9
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lirq9_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lirq9_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lirq9_wfe:
    wfe
    b .Lirq9_wfe
    .seh_endproc

/* FIQ handler: FiqLowerA64 (vector 10) */
    .global KiArm64VectorFiqLowerA64
    .seh_proc KiArm64VectorFiqLowerA64
KiArm64VectorFiqLowerA64:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #10
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lfiq10_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lfiq10_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lfiq10_wfe:
    wfe
    b .Lfiq10_wfe
    .seh_endproc

/* SError handler: SErrorLowerA64 (vector 11) */
    .global KiArm64VectorSErrorLowerA64
    .seh_proc KiArm64VectorSErrorLowerA64
KiArm64VectorSErrorLowerA64:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #11
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lserr11_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lserr11_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lserr11_wfe:
    wfe
    b .Lserr11_wfe
    .seh_endproc

/* Sync handler: SyncLowerA32 (vector 12) */
    .global KiArm64VectorSyncLowerA32
    .seh_proc KiArm64VectorSyncLowerA32
KiArm64VectorSyncLowerA32:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #12
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lsync12_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lsync12_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lsync12_wfe:
    wfe
    b .Lsync12_wfe
    .seh_endproc

/* IRQ handler: IrqLowerA32 (vector 13) */
    .global KiArm64VectorIrqLowerA32
    .seh_proc KiArm64VectorIrqLowerA32
KiArm64VectorIrqLowerA32:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #13
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lirq13_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lirq13_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lirq13_wfe:
    wfe
    b .Lirq13_wfe
    .seh_endproc

/* FIQ handler: FiqLowerA32 (vector 14) */
    .global KiArm64VectorFiqLowerA32
    .seh_proc KiArm64VectorFiqLowerA32
KiArm64VectorFiqLowerA32:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #14
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lfiq14_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lfiq14_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lfiq14_wfe:
    wfe
    b .Lfiq14_wfe
    .seh_endproc

/* SError handler: SErrorLowerA32 (vector 15) */
    .global KiArm64VectorSErrorLowerA32
    .seh_proc KiArm64VectorSErrorLowerA32
KiArm64VectorSErrorLowerA32:
    stp x29, x30, [sp, #-32]!
    .seh_save_fplr_x 32
    mov x29, sp
    .seh_set_fp
    sub sp, sp, #64
    .seh_stackalloc 64
    .seh_endprologue
    stp x0, x1, [sp, #0]
    stp x2, x3, [sp, #16]
    mrs x0, elr_el1
    mrs x1, spsr_el1
    stp x0, x1, [sp, #32]
    add x4, sp, #64
    str x4, [sp, #48]
    mov x3, sp
    mrs x1, esr_el1
    mrs x2, far_el1
    mov w0, #15
    bl KiArm64EarlyVectorHandler
    ldr x9, =KiArm64FinalVectorsInstalled
    ldrb w9, [x9]
    cbz w9, .Lserr15_hang
    ldp x10, x11, [sp, #32]
    msr elr_el1, x10
    msr spsr_el1, x11
    ldp x0, x1, [sp, #0]
    ldp x2, x3, [sp, #16]
    mov sp, x29
    ldp x29, x30, [sp], #32
    eret
.Lserr15_hang:
    mov sp, x29
    ldp x29, x30, [sp], #32
.Lserr15_wfe:
    wfe
    b .Lserr15_wfe
    .seh_endproc

/*
 * ============================================================================
 * ARM64 EARLY EXCEPTION VECTOR TABLE
 * This MUST come AFTER all SEH functions because .space interferes with
 * SEH function size calculations.
 *
 * CRITICAL: Each vector entry MUST be at a 0x80 (128) byte boundary!
 * The ARM64 architecture requires vectors to be aligned at 128-byte intervals.
 * Total table size = 16 entries * 128 bytes = 0x800 bytes
 *
 * The vector table is placed in a separate section ".evec" with 2048-byte
 * alignment to ensure VBAR_EL1 requirements are met (bits [10:0] must be 0).
 * ============================================================================
 */

    .section .evec, "ax"
    .balign 2048   /* Ensure 2048-byte alignment for VBAR_EL1 */

    .global KiArm64EarlyVectorTable
KiArm64EarlyVectorTable:

/* Entry 0 - 0x000: Sync, Current EL SP0 */
    b KiArm64VectorSyncSp0
    .space 0x7C

/* Entry 1 - 0x080: IRQ, Current EL SP0 */
    b KiArm64VectorIrqSp0
    .space 0x7C

/* Entry 2 - 0x100: FIQ, Current EL SP0 */
    b KiArm64VectorFiqSp0
    .space 0x7C

/* Entry 3 - 0x180: SError, Current EL SP0 */
    b KiArm64VectorSErrorSp0
    .space 0x7C

/* Entry 4 - 0x200: Sync, Current EL SPx */
    b KiArm64VectorSyncSpx
    .space 0x7C

/* Entry 5 - 0x280: IRQ, Current EL SPx */
    b KiArm64VectorIrqSpx
    .space 0x7C

/* Entry 6 - 0x300: FIQ, Current EL SPx */
    b KiArm64VectorFiqSpx
    .space 0x7C

/* Entry 7 - 0x380: SError, Current EL SPx */
    b KiArm64VectorSErrorSpx
    .space 0x7C

/* Entry 8 - 0x400: Sync, Lower EL A64 */
    b KiArm64VectorSyncLowerA64
    .space 0x7C

/* Entry 9 - 0x480: IRQ, Lower EL A64 */
    b KiArm64VectorIrqLowerA64
    .space 0x7C

/* Entry 10 - 0x500: FIQ, Lower EL A64 */
    b KiArm64VectorFiqLowerA64
    .space 0x7C

/* Entry 11 - 0x580: SError, Lower EL A64 */
    b KiArm64VectorSErrorLowerA64
    .space 0x7C

/* Entry 12 - 0x600: Sync, Lower EL A32 */
    b KiArm64VectorSyncLowerA32
    .space 0x7C

/* Entry 13 - 0x680: IRQ, Lower EL A32 */
    b KiArm64VectorIrqLowerA32
    .space 0x7C

/* Entry 14 - 0x700: FIQ, Lower EL A32 */
    b KiArm64VectorFiqLowerA32
    .space 0x7C

/* Entry 15 - 0x780: SError, Lower EL A32 */
    b KiArm64VectorSErrorLowerA32
    .space 0x7C
