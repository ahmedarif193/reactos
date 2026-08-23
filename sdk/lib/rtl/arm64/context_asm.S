/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64 context capture
 */

#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .global RtlCaptureContext
    .seh_proc RtlCaptureContext
RtlCaptureContext:
    .seh_endprologue

    /*
     * Capture the caller-visible register state without calling back into C.
     * x0 is both the incoming argument and the value that belongs in Context.X0.
     * Save x9-x11 before using them as scratch so the context keeps the true
     * volatile register values seen by this routine's caller.
     */
    str     x9,  [x0, #CxX9]
    str     x10, [x0, #CxX10]
    str     x11, [x0, #CxX11]

    mov     x9, x0

    stp     x0,  x1,  [x9, #CxX0]
    stp     x2,  x3,  [x9, #CxX2]
    stp     x4,  x5,  [x9, #CxX4]
    stp     x6,  x7,  [x9, #CxX6]
    str     x8,        [x9, #CxX8]
    stp     x12, x13, [x9, #CxX12]
    stp     x14, x15, [x9, #CxX14]
    stp     x16, x17, [x9, #CxX16]
    stp     x18, x19, [x9, #CxX18]
    stp     x20, x21, [x9, #CxX20]
    stp     x22, x23, [x9, #CxX22]
    stp     x24, x25, [x9, #CxX24]
    stp     x26, x27, [x9, #CxX26]
    stp     x28, x29, [x9, #CxX28]
    str     x30,       [x9, #CxLr]

    mov     x10, sp
    str     x10, [x9, #CxSp]
    str     x30, [x9, #CxPc]

    mov     w10, #0x7
    movk    w10, #0x40, lsl #16
    str     w10, [x9, #CxContextFlags]

    mrs     x10, nzcv
    str     w10, [x9, #CxCpsr]

    /*
     * Clear FP/vector and debug state so callers do not see stale stack data
     * in portions of CONTEXT that are not captured here.
     */
    add     x10, x9, #CxV
    mov     x11, #(CONTEXT_FRAME_LENGTH - CxV)
1:
    stp     xzr, xzr, [x10], #16
    subs    x11, x11, #16
    b.ne    1b

    mrs     x10, fpcr
    str     w10, [x9, #CxFpcr]
    mrs     x10, fpsr
    str     w10, [x9, #CxFpsr]

    ret
    .seh_endproc

    /*
     * VOID NTAPI RtlpRestoreContextInternal(PCONTEXT Context)
     *
     * Load the integer register state, stack pointer and PC from Context and
     * branch to Context->Pc. Used to resume execution at a handler-patched
     * context (e.g. an __except body) - the kernel/user equivalent of a context
     * switch back into the unwound frame. Does NOT return.
     */
    .global RtlpRestoreContextInternal
    .seh_proc RtlpRestoreContextInternal
RtlpRestoreContextInternal:
    .seh_endprologue

    mov     x16, x0                 /* x16 (IP0) = Context base, scratch */

    /* Callee-saved integer registers x19..x28 */
    ldp     x19, x20, [x16, #CxX19]
    ldp     x21, x22, [x16, #CxX21]
    ldp     x23, x24, [x16, #CxX23]
    ldp     x25, x26, [x16, #CxX25]
    ldp     x27, x28, [x16, #CxX27]

    /* Callee-saved floating-point/SIMD registers and control state */
    ldp     q8,  q9,  [x16, #(CxV + 16 * 8)]
    ldp     q10, q11, [x16, #(CxV + 16 * 10)]
    ldp     q12, q13, [x16, #(CxV + 16 * 12)]
    ldp     q14, q15, [x16, #(CxV + 16 * 14)]
    ldr     w17, [x16, #CxFpcr]
    msr     fpcr, x17
    ldr     w17, [x16, #CxFpsr]
    msr     fpsr, x17

    /* Frame pointer (x29) and link register (x30) */
    ldr     x29, [x16, #CxFp]
    ldr     x30, [x16, #CxLr]

    /* Restore the stack pointer for the resumed frame */
    ldr     x17, [x16, #CxSp]
    mov     sp,  x17

    /* Target PC into x17 (IP1) for the final branch */
    ldr     x17, [x16, #CxPc]

    /* Volatile registers x2..x15 and x18 (x16/x17 stay as scratch) */
    ldp     x2,  x3,  [x16, #CxX2]
    ldp     x4,  x5,  [x16, #CxX4]
    ldp     x6,  x7,  [x16, #CxX6]
    ldp     x8,  x9,  [x16, #CxX8]
    ldp     x10, x11, [x16, #CxX10]
    ldp     x12, x13, [x16, #CxX12]
    ldp     x14, x15, [x16, #CxX14]
    ldr     x18,      [x16, #CxX18]

    /* x0/x1 last, since x16 aliases the context pointer until now */
    ldp     x0,  x1,  [x16, #CxX0]

    br      x17
    .seh_endproc

    /*
     * Invoke an unwind-consolidation callback from a synthetic context frame.
     * A nested unwind from the callback must start at the supplied context,
     * after the frames already consumed by the outer unwind.
     */
    .global RtlpArm64ConsolidateCallback
    .seh_proc RtlpArm64ConsolidateCallback
RtlpArm64ConsolidateCallback:
    stp     x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    sub     sp, sp, #CONTEXT_FRAME_LENGTH
    .seh_stackalloc CONTEXT_FRAME_LENGTH
    .seh_endprologue

    mov     x4, sp
    mov     x5, #(CONTEXT_FRAME_LENGTH / 16)
1:
    ldp     x6, x7, [x0], #16
    stp     x6, x7, [x4], #16
    subs    x5, x5, #1
    b.ne    1b

    mov     x0, x2
    b       RtlpArm64InvokeConsolidateCallback
    .seh_endproc

    .seh_proc RtlpArm64InvokeConsolidateCallback
RtlpArm64InvokeConsolidateCallback:
    .seh_context
    .seh_endprologue

    blr     x1
    str     x0, [sp, #CxPc]
    mov     x0, sp
    b       RtlpRestoreContextInternal
    .seh_endproc
