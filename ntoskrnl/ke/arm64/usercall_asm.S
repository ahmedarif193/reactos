#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .extern KiUserModeCallout

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
