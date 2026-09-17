/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* RISC-V64 native CONTEXT continuation. */
#include <asm/ksriscv64.h>
    .text
    /* Direct continuation reserves t0 for the context pointer and t1 for the
     * target PC. Restore the advertised FP bank before the integer bank. */
    .p2align 1
    .globl RtlpRiscv64RestoreContext
RtlpRiscv64RestoreContext:
    mv t0, a0
    ld t1, CxPc(t0)
    lw t2, CxFlags(t0)
    li t3, CONTEXT_FLOATING_POINT
    and t2, t2, t3
    li t3, CONTEXT_FLOATING_POINT
    beq t2, t3, .Lrestore_fp
    j .Lrestore_integer
.Lrestore_fp:
    lw t2, CxFcsr(t0)
    fscsr t2
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fld f\n, CxF + 8 * \n(t0)
    .endr
.Lrestore_integer:
    .irp n,1,3,4,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    ld x\n, CxX + 8 * \n(t0)
    .endr
    ld sp, CxX + 8 * 2(t0)
    jalr zero, t1, 0
