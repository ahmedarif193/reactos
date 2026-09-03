#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .extern KiUserModeCallout

    .global KiArm64InvokeSystemCall
    .seh_proc KiArm64InvokeSystemCall
KiArm64InvokeSystemCall:
    /*
     * x0: system-service address
     * x1: saved x0-x7 array
     * x2: copied stack-argument array
     * w3: number of stack arguments (0 through 9)
     *
     * Keep the first eight arguments in registers, as on Windows ARM64, and
     * copy only arguments nine through seventeen to the outgoing stack area.
     */
    stp     x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov     x29, sp
    .seh_set_fp
    sub     sp, sp, #80
    .seh_stackalloc 80
    .seh_endprologue

    mov     x16, x0
    mov     x17, x1
    cbz     w3, 2f
    mov     x9, sp
1:
    ldr     x10, [x2], #8
    str     x10, [x9], #8
    subs    w3, w3, #1
    b.ne    1b
2:
    ldp     x0, x1, [x17, #0]
    ldp     x2, x3, [x17, #16]
    ldp     x4, x5, [x17, #32]
    ldp     x6, x7, [x17, #48]
    blr     x16

    add     sp, sp, #80
    ldp     x29, x30, [sp], #16
    ret
    .seh_endproc

    .global KiCallUserMode
    .seh_proc KiCallUserMode
KiCallUserMode:
    /*
     * SEH: keep the public KEXCEPTION_FRAME layout intact, then use its aligned
     * tail for the ARM64 ABI-nonvolatile d8-d15 register bank.
     */
    sub     sp, sp, #0x110
    .seh_stackalloc 0x110
    stp     x19, x20, [sp, #ExX19]
    .seh_save_regp x19, ExX19
    stp     x21, x22, [sp, #ExX21]
    .seh_save_regp x21, ExX21
    stp     x23, x24, [sp, #ExX23]
    .seh_save_regp x23, ExX23
    stp     x25, x26, [sp, #ExX25]
    .seh_save_regp x25, ExX25
    stp     x27, x28, [sp, #ExX27]
    .seh_save_regp x27, ExX27
    str     x29, [sp, #ExFp]
    .seh_save_reg x29, ExFp
    str     x30, [sp, #ExLr]
    .seh_save_reg x30, ExLr
    stp     d8, d9, [sp, #0xD0]
    .seh_save_fregp d8, 0xd0
    stp     d10, d11, [sp, #0xE0]
    .seh_save_fregp d10, 0xe0
    stp     d12, d13, [sp, #0xF0]
    .seh_save_fregp d12, 0xf0
    stp     d14, d15, [sp, #0x100]
    .seh_save_fregp d14, 0x100
    .seh_endprologue
    /* Save return address again (not tracked by SEH) */
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
    ldp     d8, d9, [sp, #0xD0]
    ldp     d10, d11, [sp, #0xE0]
    ldp     d12, d13, [sp, #0xF0]
    ldp     d14, d15, [sp, #0x100]
    add     sp, sp, #0x110
    ret
    .seh_endproc

    .global KiCallbackReturn
    .seh_proc KiCallbackReturn
KiCallbackReturn:
    /* SEH: This is a non-local return - restores frame from x0 */
    .seh_endprologue
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
    ldp     d8, d9, [sp, #0xD0]
    ldp     d10, d11, [sp, #0xE0]
    ldp     d12, d13, [sp, #0xF0]
    ldp     d14, d15, [sp, #0x100]

    add     sp, sp, #0x110

    mov     x0, x9
    ret
    .seh_endproc
