/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* RISC-V64 integer LP64 handler and funclet call boundaries. */
#include <asm/ksriscv64.h>
    .text

    .macro save_frame
    addi sp, sp, -128
    .seh_set_cfa x2, 128
    sd ra, 120(sp)
    .seh_save_gpr x1, -8
    .set slot, 0
    .irp reg,8,9,18,19,20,21,22,23,24,25,26,27
    sd x\reg, slot(sp)
    .seh_save_gpr x\reg, slot - 128
    .set slot, slot + 8
    .endr
    .endm

    .macro restore_frame
    .seh_startepilogue
    .set slot, 0
    .irp reg,8,9,18,19,20,21,22,23,24,25,26,27
    ld x\reg, slot(sp)
    .seh_same_gpr x\reg
    .set slot, slot + 8
    .endr
    ld ra, 120(sp)
    .seh_same_gpr x1
    addi sp, sp, 128
    .seh_set_cfa x2, 0
    ret
    .seh_endepilogue
    .endm

    /* a0-a3: ordinary personality arguments; a4: routine.
     * Guard data is initialized before the prologue becomes handler-active.
     * The guard's establisher is SP, because this frame has an SP-based CFA. */
    .macro handler_wrapper name, guard, flags:vararg
    .globl \name
    .seh_proc \name
\name:
    .seh_handler \guard, \flags
    save_frame
    sd a3, 112(sp)
    .seh_endprologue
    jalr ra, a4, 0
    restore_frame
    .seh_endproc
    .endm

    handler_wrapper RtlpExecuteHandlerForException, RtlpRiscv64ExceptionHandler, @except
    handler_wrapper RtlpExecuteHandlerForUnwind, RtlpRiscv64UnwindHandler, @except, @unwind

    /* a0: exception pointers or abnormal flag, a1: parent establisher,
     * a2: funclet, a3: validated versioned nonvolatile snapshot.
     * Restore the runtime's own bank on return; preserve its own CFA even
     * while the funclet sees the parent's nonvolatile registers. */
    .globl RtlpRiscv64CallFunclet
    .seh_proc RtlpRiscv64CallFunclet
RtlpRiscv64CallFunclet:
    save_frame
    .seh_endprologue
    beqz a3, 9f
    lw t0, NvVersion(a3)
    li t1, RISCV64_NONVOLATILE_CONTEXT_VERSION
    bne t0, t1, 9f
    lw t0, NvSize(a3)
    li t1, NonvolatileContextLength
    bne t0, t1, 9f
    .set slot, NvS
    .irp reg,8,9,18,19,20,21,22,23,24,25,26,27
    ld x\reg, slot(a3)
    .set slot, slot + 8
    .endr
    jalr ra, a2, 0
    restore_frame
9:
    /* Corrupt internal snapshot: fail without executing a funclet. */
    .word 0
    j 9b
    .seh_endproc
