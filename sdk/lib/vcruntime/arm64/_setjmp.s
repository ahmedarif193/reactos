/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Implementation of _setjmp for ARM64
 * COPYRIGHT:   Copyright Timo Kreuzer <timo.kreuzer@reactos.org>
 */

    .text
    .align 2

    .global _setjmp
    .global _setjmpex
    .global __intrinsic_setjmp
    .global __intrinsic_setjmpex
_setjmp:
_setjmpex:
__intrinsic_setjmp:
__intrinsic_setjmpex:
    str     x1, [x0, #0]
    str     xzr, [x0, #8]
    stp     x19, x20, [x0, #16]
    stp     x21, x22, [x0, #32]
    stp     x23, x24, [x0, #48]
    stp     x25, x26, [x0, #64]
    stp     x27, x28, [x0, #80]
    stp     x29, x30, [x0, #96]
    mov     x2, sp
    str     x2, [x0, #112]
    mrs     x2, fpcr
    str     w2, [x0, #120]
    mrs     x2, fpsr
    str     w2, [x0, #124]
    stp     d8, d9, [x0, #128]
    stp     d10, d11, [x0, #144]
    stp     d12, d13, [x0, #160]
    stp     d14, d15, [x0, #176]
    mov     w0, wzr
    ret
