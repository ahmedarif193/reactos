/*
 * PROJECT:         ReactOS Run-Time Library
 * LICENSE:         MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:         ARM64 context capture
 */

#include <ksarm64.h>

    .text
    .p2align 2
    .global RtlCaptureContext

RtlCaptureContext:
    stp     x0, x1, [x0, #CxX0]
    stp     x2, x3, [x0, #CxX2]
    stp     x4, x5, [x0, #CxX4]
    stp     x6, x7, [x0, #CxX6]
    stp     x8, x9, [x0, #CxX8]
    stp     x10, x11, [x0, #CxX10]
    stp     x12, x13, [x0, #CxX12]
    stp     x14, x15, [x0, #CxX14]
    stp     x16, x17, [x0, #CxX16]
    stp     x18, x19, [x0, #CxX18]
    stp     x20, x21, [x0, #CxX20]
    stp     x22, x23, [x0, #CxX22]
    stp     x24, x25, [x0, #CxX24]
    stp     x26, x27, [x0, #CxX26]
    str     x28, [x0, #CxX28]
    str     x29, [x0, #CxFp]
    str     x30, [x0, #CxLr]

    mov     x1, sp
    str     x1, [x0, #CxSp]
    str     x30, [x0, #CxPc]

    stp     q0, q1, [x0, #CxV]
    stp     q2, q3, [x0, #(CxV + 0x20)]
    stp     q4, q5, [x0, #(CxV + 0x40)]
    stp     q6, q7, [x0, #(CxV + 0x60)]
    stp     q8, q9, [x0, #(CxV + 0x80)]
    stp     q10, q11, [x0, #(CxV + 0xA0)]
    stp     q12, q13, [x0, #(CxV + 0xC0)]
    stp     q14, q15, [x0, #(CxV + 0xE0)]
    stp     q16, q17, [x0, #(CxV + 0x100)]
    stp     q18, q19, [x0, #(CxV + 0x120)]
    stp     q20, q21, [x0, #(CxV + 0x140)]
    stp     q22, q23, [x0, #(CxV + 0x160)]
    stp     q24, q25, [x0, #(CxV + 0x180)]
    stp     q26, q27, [x0, #(CxV + 0x1A0)]
    stp     q28, q29, [x0, #(CxV + 0x1C0)]
    stp     q30, q31, [x0, #(CxV + 0x1E0)]

    mrs     x1, fpcr
    str     w1, [x0, #CxFpcr]
    mrs     x1, fpsr
    str     w1, [x0, #CxFpsr]
    mrs     x1, nzcv
    str     w1, [x0, #CxCpsr]

    movz    w1, #(CONTEXT_FULL & 0xffff)
    movk    w1, #((CONTEXT_FULL >> 16) & 0xffff), lsl #16
    str     w1, [x0, #CxContextFlags]
    ret
