/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V call-time supervisor context capture
 */

#include <asm/ksriscv64.h>

    .section .text$RtlCaptureContext, "xr"
    .balign 4
    .global RtlCaptureContext
RtlCaptureContext:
    /* Save before using any temporary. a0 is the context pointer at the
     * call boundary; Pc/ra and sp describe return from this leaf routine. */
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd x\n, CxX + 8 * \n(a0)
    .endr
    sd ra, CxPc(a0)

    /* Reading FP registers with FS=Off would itself fault. An unavailable
     * bank is zeroed but not advertised as captured in ContextFlags. */
    csrr t0, sstatus
    li t1, 0x6000
    and t0, t0, t1
    beqz t0, .Lcapture_no_fp
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    fsd f\n, CxF + 8 * \n(a0)
    .endr
    frcsr t0
    sw t0, CxFcsr(a0)
    li t0, CONTEXT_FULL
    sw t0, CxFlags(a0)
    ret

.Lcapture_no_fp:
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    sd zero, CxF + 8 * \n(a0)
    .endr
    sw zero, CxFcsr(a0)
    li t0, CONTEXT_CONTROL | CONTEXT_INTEGER
    sw t0, CxFlags(a0)
    ret

    .section .text$DbgBreakPoint, "xr"
    .balign 4
    .global DbgBreakPoint
DbgBreakPoint:
    c.ebreak
    ret

    .section .text$DbgBreakPointWithStatus, "xr"
    .balign 4
    .global DbgBreakPointWithStatus
    .global RtlpBreakWithStatusInstruction
DbgBreakPointWithStatus:
RtlpBreakWithStatusInstruction:
    /* Preserve the caller's a0 in the interrupted register bank. This does
     * not define a KD packet or authorize the trap handler to continue. */
    c.ebreak
    ret
