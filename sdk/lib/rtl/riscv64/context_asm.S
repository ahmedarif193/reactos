/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 user-mode context and breakpoint support
 */

#include <asm/ksriscv64.h>

    .text
    .p2align 2
    .globl RtlCaptureContext
RtlCaptureContext:
    /* Save the complete integer bank before using a temporary. The stored a0
     * is the context pointer at this call boundary, and ra is also the Pc. */
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd x\n, CxX + 8 * \n(a0)
    .endr
    sd ra, CxPc(a0)

    /* The kernel enters user mode with FS=Dirty. Preserve the complete
     * RV64D bank and FCSR as part of the native CONTEXT contract. */
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fsd f\n, CxF + 8 * \n(a0)
    .endr
    frcsr t0
    sw t0, CxFcsr(a0)
    li t0, CONTEXT_FULL
    sw t0, CxFlags(a0)
    ret

    .p2align 2
    .globl DbgBreakPoint
    .globl DbgUserBreakPoint
DbgBreakPoint:
DbgUserBreakPoint:
    c.ebreak
    ret

    .p2align 2
    .globl DbgBreakPointWithStatus
DbgBreakPointWithStatus:
    c.ebreak
    ret

    /* Private execution-environment service: a0 selects the KD operation,
     * a1-a4 carry its arguments. Keep debugger EBREAKs distinct from calls. */
    .p2align 2
    .globl DebugService
DebugService:
    li t0, RISCV_DEBUG_SERVICE_CALL
    ecall
    ret

    .p2align 2
    .globl DebugService2
DebugService2:
    mv t1, a2
    mv a2, a1
    mv a1, a0
    mv a0, t1
    li t0, RISCV_DEBUG_SERVICE_CALL
    ecall
    ret
