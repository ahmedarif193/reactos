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
     * VOID NTAPI RtlRestoreContext(PCONTEXT Context, PEXCEPTION_RECORD Record)
     *
     * Load the integer register state, stack pointer and PC from Context and
     * branch to Context->Pc. Used to resume execution at a handler-patched
     * context (e.g. an __except body) - the kernel/user equivalent of a context
     * switch back into the unwound frame. Does NOT return. The ExceptionRecord
     * argument (x1) is accepted for ABI compatibility and ignored here (no
     * collided-unwind handling).
     */
    .global RtlRestoreContext
    .seh_proc RtlRestoreContext
RtlRestoreContext:
    .seh_endprologue

    mov     x16, x0                 /* x16 (IP0) = Context base, scratch */

    /* Callee-saved integer registers x19..x28 */
    ldp     x19, x20, [x16, #CxX19]
    ldp     x21, x22, [x16, #CxX21]
    ldp     x23, x24, [x16, #CxX23]
    ldp     x25, x26, [x16, #CxX25]
    ldp     x27, x28, [x16, #CxX27]

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
