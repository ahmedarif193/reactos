/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 longjmp helper
 */

    .text
    .align 2

    .global __longjmp_noframe
__longjmp_noframe:
    ldp     x19, x20, [x0, #16]
    ldp     x21, x22, [x0, #32]
    ldp     x23, x24, [x0, #48]
    ldp     x25, x26, [x0, #64]
    ldp     x27, x28, [x0, #80]
    ldp     x29, x30, [x0, #96]
    ldr     x2, [x0, #112]
    ldr     w3, [x0, #120]
    ldr     w4, [x0, #124]
    ldr     d8, [x0, #128]
    ldr     d9, [x0, #136]
    ldr     d10, [x0, #144]
    ldr     d11, [x0, #152]
    ldr     d12, [x0, #160]
    ldr     d13, [x0, #168]
    ldr     d14, [x0, #176]
    ldr     d15, [x0, #184]
    cbnz    w1, 1f
    mov     w1, #1
1:
    msr     fpcr, x3
    msr     fpsr, x4
    mov     sp, x2
    mov     w0, w1
    ret
