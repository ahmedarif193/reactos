#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .extern KiUserModeCallout

    .global KiCallUserMode
KiCallUserMode:
    sub     sp, sp, #KEXCEPTION_FRAME_LENGTH
    stp     x19, x20, [sp, #ExX19]
    stp     x21, x22, [sp, #ExX21]
    stp     x23, x24, [sp, #ExX23]
    stp     x25, x26, [sp, #ExX25]
    stp     x27, x28, [sp, #ExX27]
    str     x29, [sp, #ExFp]
    str     x30, [sp, #ExLr]
    str     x30, [sp, #ExReturn]

    mrs     x9, fpcr
    str     x9, [sp, #ExFpcr]
    mrs     x9, fpsr
    str     x9, [sp, #ExFpsr]

    str     x0, [sp, #ExOutputBuffer]
    str     x1, [sp, #ExOutputLength]

    mov     x0, sp
    bl      KiUserModeCallout

    ldr     x9, [sp, #ExFpcr]
    msr     fpcr, x9
    ldr     x9, [sp, #ExFpsr]
    msr     fpsr, x9

    ldp     x19, x20, [sp, #ExX19]
    ldp     x21, x22, [sp, #ExX21]
    ldp     x23, x24, [sp, #ExX23]
    ldp     x25, x26, [sp, #ExX25]
    ldp     x27, x28, [sp, #ExX27]
    ldr     x29, [sp, #ExFp]
    ldr     x30, [sp, #ExLr]
    add     sp, sp, #KEXCEPTION_FRAME_LENGTH
    ret

    .global KiCallbackReturn
KiCallbackReturn:
    mov     sp, x0
    mov     x9, x1

    ldr     x10, [sp, #ExFpcr]
    msr     fpcr, x10
    ldr     x10, [sp, #ExFpsr]
    msr     fpsr, x10

    ldp     x19, x20, [sp, #ExX19]
    ldp     x21, x22, [sp, #ExX21]
    ldp     x23, x24, [sp, #ExX23]
    ldp     x25, x26, [sp, #ExX25]
    ldp     x27, x28, [sp, #ExX27]
    ldr     x29, [sp, #ExFp]
    ldr     x30, [sp, #ExLr]

    add     sp, sp, #KEXCEPTION_FRAME_LENGTH

    mov     x0, x9
    ret
